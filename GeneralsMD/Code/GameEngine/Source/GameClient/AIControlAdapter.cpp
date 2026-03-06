#include "PreRTS.h"

#include "GameClient/AIControlAdapter.h"

#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/BuildAssistant.h"
#include "Common/KindOf.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameClient/TerrainVisual.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GeneralsOnline/json.hpp"
#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"

#include <windows.h>

#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace
{
	class AIControlAdapterState
	{
	public:
		AIControlAdapterState() :
			m_pipe(INVALID_HANDLE_VALUE),
			m_hasClient(false)
		{
			char buffer[32];
			sprintf_s(buffer, "%08X%08X", static_cast<unsigned int>(::GetCurrentProcessId()), static_cast<unsigned int>(::GetTickCount()));
			m_sessionId = buffer;
		}

		~AIControlAdapterState()
		{
			closePipe();
		}

		void reset()
		{
			m_lineBuffer.clear();
			m_lastAutoSupplySourceByPlayer.clear();
			resetClientConnection();
		}

		void update()
		{
			ensurePipeCreated();
			if (m_pipe == INVALID_HANDLE_VALUE)
			{
				return;
			}

			acceptClientIfAvailable();
			if (!m_hasClient)
			{
				return;
			}

			readIncomingData();
		}

	private:
		static const DWORD PIPE_BUFFER_SIZE = 8192;

		HANDLE m_pipe;
		bool m_hasClient;
		std::string m_lineBuffer;
		std::string m_sessionId;
		std::unordered_map<Int, Int> m_lastAutoSupplySourceByPlayer;

		void ensurePipeCreated()
		{
			if (m_pipe != INVALID_HANDLE_VALUE)
			{
				return;
			}

			m_pipe = ::CreateNamedPipeA(
				"\\\\.\\pipe\\zh_ai_control",
				PIPE_ACCESS_DUPLEX,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
				1,
				PIPE_BUFFER_SIZE,
				PIPE_BUFFER_SIZE,
				0,
				nullptr);
		}

		void closePipe()
		{
			if (m_pipe == INVALID_HANDLE_VALUE)
			{
				return;
			}

			resetClientConnection();
			::CloseHandle(m_pipe);
			m_pipe = INVALID_HANDLE_VALUE;
		}

		void resetClientConnection()
		{
			if (m_pipe != INVALID_HANDLE_VALUE)
			{
				::DisconnectNamedPipe(m_pipe);
			}
			m_hasClient = false;
		}

		void acceptClientIfAvailable()
		{
			if (m_hasClient || m_pipe == INVALID_HANDLE_VALUE)
			{
				return;
			}

			if (::ConnectNamedPipe(m_pipe, nullptr))
			{
				m_hasClient = true;
				return;
			}

			DWORD error = ::GetLastError();
			if (error == ERROR_PIPE_CONNECTED)
			{
				m_hasClient = true;
				return;
			}

			if (error == ERROR_NO_DATA)
			{
				::DisconnectNamedPipe(m_pipe);
			}
		}

		void readIncomingData()
		{
			while (m_hasClient)
			{
				DWORD bytesAvailable = 0;
				if (!::PeekNamedPipe(m_pipe, nullptr, 0, nullptr, &bytesAvailable, nullptr))
				{
					resetClientConnection();
					return;
				}

				if (bytesAvailable == 0)
				{
					return;
				}

				char buffer[2048];
				DWORD requested = bytesAvailable;
				if (requested > sizeof(buffer))
				{
					requested = sizeof(buffer);
				}

				DWORD bytesRead = 0;
				if (!::ReadFile(m_pipe, buffer, requested, &bytesRead, nullptr) || bytesRead == 0)
				{
					resetClientConnection();
					return;
				}

				m_lineBuffer.append(buffer, bytesRead);
				processBufferedLines();
			}
		}

		void processBufferedLines()
		{
			for (;;)
			{
				const std::size_t newline = m_lineBuffer.find('\n');
				if (newline == std::string::npos)
				{
					return;
				}

				std::string line = m_lineBuffer.substr(0, newline);
				m_lineBuffer.erase(0, newline + 1);

				if (!line.empty() && line[line.size() - 1] == '\r')
				{
					line.erase(line.size() - 1);
				}

				if (line.empty())
				{
					continue;
				}

				handleMessage(line);
			}
		}

		static std::string getJsonString(const nlohmann::json& obj, const char* key)
		{
			const auto it = obj.find(key);
			if (it == obj.end() || !it->is_string())
			{
				return std::string();
			}
			return it->get<std::string>();
		}

		static std::string normalizeControlId(const std::string& controlId)
		{
			// Backward-compatible alias: "SoloPlay" maps to the real single-player button.
			if (controlId == "MainMenu.wnd:ButtonSoloPlay")
			{
				return "MainMenu.wnd:ButtonSinglePlayer";
			}
			// Convenience alias for LAN lobby back button.
			if (controlId == "ButtonBack")
			{
				return "LanLobbyMenu.wnd:ButtonBack";
			}
			if (controlId == "ButtonCreateGame")
			{
				return "LanLobbyMenu.wnd:ButtonHost";
			}
			if (controlId == "ButtonJoinGame")
			{
				return "LanLobbyMenu.wnd:ButtonJoin";
			}
			if (controlId == "ButtonDirectConnect")
			{
				return "LanLobbyMenu.wnd:ButtonDirectConnect";
			}
			return controlId;
		}

		GameWindow* findFirstUsableControl(const std::vector<std::string>& decoratedIds) const
		{
			if (TheNameKeyGenerator == nullptr || TheWindowManager == nullptr)
			{
				return nullptr;
			}

			for (const std::string& id : decoratedIds)
			{
				const NameKeyType key = TheNameKeyGenerator->nameToKey(id.c_str());
				GameWindow* control = TheWindowManager->winGetWindowFromId(nullptr, key);
				if (control == nullptr)
				{
					continue;
				}

				const UnsignedInt status = control->winGetStatus();
				if ((status & WIN_STATUS_HIDDEN) != 0u)
				{
					continue;
				}
				if ((status & WIN_STATUS_ENABLED) == 0u)
				{
					continue;
				}
				if (control->winGetParent() == nullptr)
				{
					continue;
				}

				return control;
			}

			return nullptr;
		}

		void sendJsonLine(const nlohmann::json& payload)
		{
			if (!m_hasClient)
			{
				return;
			}

			std::string line = payload.dump();
			line.push_back('\n');

			DWORD bytesWritten = 0;
			if (!::WriteFile(m_pipe, line.data(), static_cast<DWORD>(line.size()), &bytesWritten, nullptr))
			{
				resetClientConnection();
			}
		}

		void sendProtocolError(const std::string& requestId, const char* code, const char* reason)
		{
			DEBUG_LOG(("[AICTRL] protocol_error request_id=%s code=%s reason=%s",
				requestId.c_str(),
				code != nullptr ? code : "",
				reason != nullptr ? reason : ""));

			nlohmann::json reply = {
				{"type", "Error"},
				{"request_id", requestId},
				{"code", code},
				{"reason", reason}
			};
			sendJsonLine(reply);
		}

		void sendActionAck(const std::string& requestId, bool ok, const char* code = nullptr, const char* reason = nullptr)
		{
			DEBUG_LOG(("[AICTRL] action_ack request_id=%s ok=%d code=%s reason=%s",
				requestId.c_str(),
				ok ? 1 : 0,
				code != nullptr ? code : "",
				reason != nullptr ? reason : ""));

			nlohmann::json reply = {
				{"type", "ActionAck"},
				{"request_id", requestId},
				{"ok", ok}
			};

			if (!ok)
			{
				reply["code"] = code != nullptr ? code : "internal_error";
				reply["reason"] = reason != nullptr ? reason : "request_failed";
			}

			sendJsonLine(reply);
		}

		void sendQueryResult(const std::string& requestId, const nlohmann::json& result)
		{
			DEBUG_LOG(("[AICTRL] query_result request_id=%s ok=1", requestId.c_str()));

			nlohmann::json reply = {
				{"type", "QueryResult"},
				{"request_id", requestId},
				{"ok", true},
				{"result", result}
			};
			sendJsonLine(reply);
		}

		void sendQueryError(const std::string& requestId, const char* code, const char* reason)
		{
			DEBUG_LOG(("[AICTRL] query_result request_id=%s ok=0 code=%s reason=%s",
				requestId.c_str(),
				code != nullptr ? code : "",
				reason != nullptr ? reason : ""));

			nlohmann::json reply = {
				{"type", "QueryResult"},
				{"request_id", requestId},
				{"ok", false},
				{"code", code != nullptr ? code : "invalid_state"},
				{"reason", reason != nullptr ? reason : "query_failed"}
			};
			sendJsonLine(reply);
		}

		void handleMessage(const std::string& line)
		{
			const nlohmann::json message = nlohmann::json::parse(line, nullptr, false);
			if (message.is_discarded() || !message.is_object())
			{
				DEBUG_LOG(("[AICTRL] recv invalid_json"));
				sendProtocolError(std::string(), "bad_request", "invalid_json");
				return;
			}

			const std::string type = getJsonString(message, "type");
			const std::string requestId = getJsonString(message, "request_id");
			DEBUG_LOG(("[AICTRL] recv type=%s request_id=%s", type.c_str(), requestId.c_str()));

			if (type.empty())
			{
				sendProtocolError(requestId, "bad_request", "missing_type");
				return;
			}

			if (type == "Hello")
			{
				nlohmann::json reply = {
					{"type", "HelloAck"},
					{"request_id", requestId},
					{"ok", true},
					{"protocol", "zh-ai-control-v1"},
					{"adapter_version", "0.1.0"},
					{"session_id", m_sessionId},
					{"capabilities", nlohmann::json::array({
						"session",
						"menu_click",
						"menu_set_text",
						"chat_send",
						"game_query",
						"game_queue_unit",
						"game_supply_build",
						"game_supply_build_smart",
						"game_barracks_build_smart"
					})}
				};
				sendJsonLine(reply);
				return;
			}

			if (type == "Ping")
			{
				nlohmann::json reply = {
					{"type", "Pong"},
					{"request_id", requestId}
				};
				sendJsonLine(reply);
				return;
			}

			if (type != "SessionCommand")
			{
				sendProtocolError(requestId, "bad_request", "unsupported_type");
				return;
			}

			handleSessionCommand(message, requestId);
		}

		void handleSessionCommand(const nlohmann::json& message, const std::string& requestId)
		{
			const std::string cmd = getJsonString(message, "cmd");
			DEBUG_LOG(("[AICTRL] session_cmd request_id=%s cmd=%s", requestId.c_str(), cmd.c_str()));
			if (cmd.empty())
			{
				sendActionAck(requestId, false, "bad_request", "missing_cmd");
				return;
			}

			if (cmd == "Menu.Click")
			{
				std::string reason;
				if (!executeMenuClick(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Menu.SetText")
			{
				std::string reason;
				if (!executeMenuSetText(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Menu.ListControls")
			{
				sendQueryResult(requestId, buildControlsInventory(message));
				return;
			}

			if (cmd == "Chat.Send")
			{
				std::string reason;
				if (!executeChatSend(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.Query")
			{
				nlohmann::json result;
				std::string reason;
				if (!executeGameQuery(message, result, reason))
				{
					sendQueryError(requestId, "invalid_state", reason.c_str());
					return;
				}

				sendQueryResult(requestId, result);
				return;
			}

			if (cmd == "Game.QueueUnit")
			{
				std::string reason;
				if (!executeGameQueueUnit(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildWorker")
			{
				std::string reason;
				if (!executeGameBuildWorker(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.FindSupplySources")
			{
				nlohmann::json result;
				std::string reason;
				if (!executeGameFindSupplySources(message, result, reason))
				{
					sendQueryError(requestId, "invalid_state", reason.c_str());
					return;
				}
				sendQueryResult(requestId, result);
				return;
			}

			if (cmd == "Game.FindBuildLocationNearSupply")
			{
				nlohmann::json result;
				std::string reason;
				if (!executeGameFindBuildLocationNearSupply(message, result, reason))
				{
					sendQueryError(requestId, "invalid_state", reason.c_str());
					return;
				}
				sendQueryResult(requestId, result);
				return;
			}

			if (cmd == "Game.DozerConstruct")
			{
				std::string reason;
				if (!executeGameDozerConstruct(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildSupplyStashAuto")
			{
				std::string reason;
				if (!executeGameBuildSupplyStashAuto(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildSupplyStashSmart")
			{
				std::string reason;
				if (!executeGameBuildSupplyStashSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildBarracksSmart")
			{
				std::string reason;
				if (!executeGameBuildBarracksSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildCommandCenterSmart")
			{
				std::string reason;
				if (!executeGameBuildCommandCenterSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.AttackMove")
			{
				std::string reason;
				if (!executeGameAttackMove(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			sendActionAck(requestId, false, "unsupported_cmd", "unsupported_session_command");
		}

		nlohmann::json buildLocalPlayerSummary(const Player* player) const
		{
			const PlayerType playerType = player->getPlayerType();
			const bool isAi = (playerType == PLAYER_COMPUTER);
			const Color playerColor = player->getPlayerColor();
			const std::string displayName = unicodeToUtf8(const_cast<Player*>(player)->getPlayerDisplayName());
			nlohmann::json local = {
				{"player_index", player->getPlayerIndex()},
				{"name", displayName},
				{"player_name_key", KEYNAME(player->getPlayerNameKey()).str()},
				{"side", player->getSide().str()},
				{"base_side", player->getBaseSide().str()},
				{"color", formatColorHex(playerColor)},
				{"color_argb", static_cast<Int>(playerColor)},
				{"color_hex", formatColorHex(playerColor)},
				{"rank_level", player->getRankLevel()},
				{"player_type", static_cast<Int>(playerType)},
				{"player_type_name", isAi ? "computer" : "human"},
				{"is_ai", isAi}
			};

			const PlayerTemplate* playerTemplate = player->getPlayerTemplate();
			if (playerTemplate != nullptr)
			{
				local["template_name"] = playerTemplate->getName().str();
				local["template_side"] = playerTemplate->getSide().str();
				local["template_base_side"] = playerTemplate->getBaseSide().str();
			}

			const GameSlot* slot = findSlotForPlayer(player);
			if (slot != nullptr)
			{
				local["team_id"] = slot->getTeamNumber();
				local["team"] = slot->getTeamNumber();
				local["slot_color_index"] = slot->getColor();
				local["slot_start_pos"] = slot->getStartPos();
				local["start_pos_index"] = slot->getStartPos();
				local["start_position_index"] = slot->getStartPos();
				if (displayName.empty())
				{
					const std::string slotName = unicodeToUtf8(slot->getName());
					if (!slotName.empty())
					{
						local["name"] = slotName;
					}
				}
			}

			nlohmann::json mapPos = buildPlayerMapPositionSummary(player);
			if (!mapPos.is_null() && mapPos.is_object())
			{
				local["map_position"] = mapPos;
				local["start_position"] = mapPos;
				const auto xIt = mapPos.find("x");
				const auto yIt = mapPos.find("y");
				if (xIt != mapPos.end() && yIt != mapPos.end())
				{
					local["x"] = *xIt;
					local["y"] = *yIt;
				}
			}

			return local;
		}

		Player* getPlayerByIndex(Int playerIndex) const
		{
			if (ThePlayerList == nullptr)
			{
				return nullptr;
			}

			const Int count = ThePlayerList->getPlayerCount();
			for (Int i = 0; i < count; ++i)
			{
				Player* player = ThePlayerList->getNthPlayer(i);
				if (player != nullptr && player->getPlayerIndex() == playerIndex)
				{
					return player;
				}
			}
			return nullptr;
		}

		static std::string formatColorHex(Color argb)
		{
			char buffer[16];
			sprintf_s(buffer, "#%08X", static_cast<unsigned int>(argb));
			return std::string(buffer);
		}

		static std::string unicodeToUtf8(const UnicodeString& text)
		{
			const WideChar* wide = text.str();
			if (wide == nullptr || wide[0] == 0)
			{
				return std::string();
			}

			const int utf8LenWithNull = ::WideCharToMultiByte(
				CP_UTF8,
				0,
				wide,
				-1,
				nullptr,
				0,
				nullptr,
				nullptr);
			if (utf8LenWithNull <= 1)
			{
				return std::string();
			}

			std::string out;
			out.resize(static_cast<std::size_t>(utf8LenWithNull));
			::WideCharToMultiByte(
				CP_UTF8,
				0,
				wide,
				-1,
				&out[0],
				utf8LenWithNull,
				nullptr,
				nullptr);
			if (!out.empty() && out[out.size() - 1] == '\0')
			{
				out.resize(out.size() - 1);
			}
			return out;
		}

		const GameSlot* findSlotForPlayer(const Player* player) const
		{
			if (player == nullptr || TheGameInfo == nullptr)
			{
				return nullptr;
			}

			const Int idx = player->getPlayerIndex();
			if (idx >= 0 && idx < MAX_SLOTS)
			{
				return TheGameInfo->getConstSlot(idx);
			}
			return nullptr;
		}

		nlohmann::json buildPlayerMapPositionSummary(const Player* player) const
		{
			nlohmann::json mapPos = nlohmann::json::object();
			if (player == nullptr)
			{
				return mapPos;
			}

			struct PositionProbeContext
			{
				Bool hasCommandCenter;
				Coord3D commandCenterPos;
				Bool hasStructure;
				Coord3D structurePos;
				UnsignedInt count;
				Real sumX;
				Real sumY;
				Real sumZ;
			};

			PositionProbeContext ctx;
			ctx.hasCommandCenter = false;
			ctx.commandCenterPos.x = 0.0f;
			ctx.commandCenterPos.y = 0.0f;
			ctx.commandCenterPos.z = 0.0f;
			ctx.hasStructure = false;
			ctx.structurePos.x = 0.0f;
			ctx.structurePos.y = 0.0f;
			ctx.structurePos.z = 0.0f;
			ctx.count = 0;
			ctx.sumX = 0.0f;
			ctx.sumY = 0.0f;
			ctx.sumZ = 0.0f;

			auto callback = [](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}

				const Coord3D* pos = obj->getPosition();
				if (pos == nullptr)
				{
					return;
				}

				PositionProbeContext* probe = static_cast<PositionProbeContext*>(userData);
				probe->sumX += pos->x;
				probe->sumY += pos->y;
				probe->sumZ += pos->z;
				++probe->count;

				if (!probe->hasCommandCenter && obj->isKindOf(KINDOF_COMMANDCENTER))
				{
					probe->commandCenterPos = *pos;
					probe->hasCommandCenter = true;
				}
				if (!probe->hasStructure && obj->isKindOf(KINDOF_STRUCTURE))
				{
					probe->structurePos = *pos;
					probe->hasStructure = true;
				}
			};

			const_cast<Player*>(player)->iterateObjects(callback, &ctx);

			const GameSlot* slot = findSlotForPlayer(player);
			if (slot != nullptr)
			{
				mapPos["start_pos_index"] = slot->getStartPos();
				mapPos["start_position_index"] = slot->getStartPos();
			}

			if (ctx.hasCommandCenter)
			{
				mapPos["x"] = ctx.commandCenterPos.x;
				mapPos["y"] = ctx.commandCenterPos.y;
				mapPos["z"] = ctx.commandCenterPos.z;
				mapPos["source"] = "command_center";
				return mapPos;
			}

			if (ctx.hasStructure)
			{
				mapPos["x"] = ctx.structurePos.x;
				mapPos["y"] = ctx.structurePos.y;
				mapPos["z"] = ctx.structurePos.z;
				mapPos["source"] = "structure";
				return mapPos;
			}

			if (ctx.count > 0)
			{
				const Real inv = 1.0f / static_cast<Real>(ctx.count);
				mapPos["x"] = ctx.sumX * inv;
				mapPos["y"] = ctx.sumY * inv;
				mapPos["z"] = ctx.sumZ * inv;
				mapPos["source"] = "centroid";
				return mapPos;
			}

			mapPos["source"] = "unknown";
			return mapPos;
		}

		static bool containsIgnoreCase(const std::string& haystack, const char* needle)
		{
			if (needle == nullptr || *needle == '\0')
			{
				return false;
			}

			std::string h = haystack;
			std::string n = needle;
			std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return h.find(n) != std::string::npos;
		}

		static std::string inferWorkerTemplateForPlayer(const Player* player)
		{
			if (player == nullptr)
			{
				return std::string();
			}

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				return "GLAInfantryWorker";
			}
			if (containsIgnoreCase(side, "china") || containsIgnoreCase(baseSide, "china"))
			{
				return "ChinaVehicleDozer";
			}
			if (containsIgnoreCase(side, "america") || containsIgnoreCase(baseSide, "america") || containsIgnoreCase(side, "usa") || containsIgnoreCase(baseSide, "usa"))
			{
				return "AmericaVehicleDozer";
			}
			return std::string();
		}

		struct ProducerSearchContext
		{
			Object* found;
			bool requireCommandCenter;
		};

		static void findProducerCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr)
			{
				return;
			}

			ProducerSearchContext* ctx = static_cast<ProducerSearchContext*>(userData);
			if (ctx->found != nullptr)
			{
				return;
			}
			if (obj->isEffectivelyDead())
			{
				return;
			}
			if (ctx->requireCommandCenter && !obj->isKindOf(KINDOF_COMMANDCENTER))
			{
				return;
			}

			ProductionUpdateInterface* production = obj->getProductionUpdateInterface();
			if (production == nullptr)
			{
				return;
			}

			ctx->found = obj;
		}

		Player* resolvePlayerFromArgs(const nlohmann::json& message, std::string& reason)
		{
			if (ThePlayerList == nullptr)
			{
				reason = "player_state_not_ready";
				return nullptr;
			}

			Player* player = ThePlayerList->getLocalPlayer();
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto playerIndexIt = argsIt->find("player_index");
				if (playerIndexIt != argsIt->end() && playerIndexIt->is_number_integer())
				{
					player = getPlayerByIndex(playerIndexIt->get<Int>());
					if (player == nullptr)
					{
						reason = "player_not_found";
						return nullptr;
					}
				}
			}

			if (player == nullptr)
			{
				reason = "local_player_missing";
				return nullptr;
			}
			return player;
		}

		Object* resolveProducerFromArgs(Player* player, const nlohmann::json& message, bool requireCommandCenter, std::string& reason)
		{
			if (player == nullptr)
			{
				reason = "player_not_found";
				return nullptr;
			}
			if (TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return nullptr;
			}

			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto producerIdIt = argsIt->find("producer_object_id");
				if (producerIdIt != argsIt->end() && producerIdIt->is_number_integer())
				{
					const Int producerId = producerIdIt->get<Int>();
					if (producerId <= 0)
					{
						reason = "invalid_producer_object_id";
						return nullptr;
					}

					Object* producer = TheGameLogic->findObjectByID(static_cast<ObjectID>(producerId));
					if (producer == nullptr)
					{
						reason = "producer_not_found";
						return nullptr;
					}
					if (producer->getControllingPlayer() != player)
					{
						reason = "producer_not_owned";
						return nullptr;
					}
					if (requireCommandCenter && !producer->isKindOf(KINDOF_COMMANDCENTER))
					{
						reason = "producer_not_command_center";
						return nullptr;
					}
					if (producer->getProductionUpdateInterface() == nullptr)
					{
						reason = "producer_not_factory";
						return nullptr;
					}
					return producer;
				}
			}

			ProducerSearchContext ctx = { nullptr, requireCommandCenter };
			player->iterateObjects(findProducerCallback, &ctx);
			if (ctx.found == nullptr)
			{
				reason = requireCommandCenter ? "command_center_not_found" : "producer_not_found";
				return nullptr;
			}
			return ctx.found;
		}

		struct WorkerSearchContext
		{
			Object* firstDozer;
			Object* firstIdleDozer;
		};

		static void findWorkerCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr)
			{
				return;
			}
			if (obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_DOZER))
			{
				return;
			}

			WorkerSearchContext* ctx = static_cast<WorkerSearchContext*>(userData);
			if (ctx->firstDozer == nullptr)
			{
				ctx->firstDozer = obj;
			}
			if (ctx->firstIdleDozer == nullptr)
			{
				AIUpdateInterface* ai = obj->getAI();
				if (ai != nullptr && ai->isIdle())
				{
					ctx->firstIdleDozer = obj;
				}
			}
		}

		Object* resolveWorkerFromArgs(Player* player, const nlohmann::json& message, bool requireIdle, std::string& reason)
		{
			if (player == nullptr)
			{
				reason = "player_not_found";
				return nullptr;
			}
			if (TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return nullptr;
			}

			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto workerIdIt = argsIt->find("worker_object_id");
				if (workerIdIt != argsIt->end() && workerIdIt->is_number_integer())
				{
					const Int workerId = workerIdIt->get<Int>();
					if (workerId <= 0)
					{
						reason = "invalid_worker_object_id";
						return nullptr;
					}

					Object* worker = TheGameLogic->findObjectByID(static_cast<ObjectID>(workerId));
					if (worker == nullptr)
					{
						reason = "worker_not_found";
						return nullptr;
					}
					if (worker->getControllingPlayer() != player)
					{
						reason = "worker_not_owned";
						return nullptr;
					}
					if (!worker->isKindOf(KINDOF_DOZER))
					{
						reason = "worker_not_dozer";
						return nullptr;
					}
					if (requireIdle)
					{
						AIUpdateInterface* ai = worker->getAI();
						if (ai == nullptr || !ai->isIdle())
						{
							reason = "worker_not_idle";
							return nullptr;
						}
					}
					return worker;
				}
			}

			WorkerSearchContext ctx = { nullptr, nullptr };
			player->iterateObjects(findWorkerCallback, &ctx);
			if (requireIdle)
			{
				if (ctx.firstIdleDozer == nullptr)
				{
					reason = "idle_worker_not_found";
					return nullptr;
				}
				return ctx.firstIdleDozer;
			}
			if (ctx.firstDozer == nullptr)
			{
				reason = "worker_not_found";
				return nullptr;
			}
			return ctx.firstDozer;
		}

		struct SupplySourceInfo
		{
			Object* source;
			Int boxesStored;
			Int cashValue;
		};

		bool collectSupplySources(Int minimumCash, std::vector<SupplySourceInfo>& outSources)
		{
			if (TheGameLogic == nullptr || TheNameKeyGenerator == nullptr)
			{
				return false;
			}

			const NameKeyType warehouseKey = TheNameKeyGenerator->nameToKey("SupplyWarehouseDockUpdate");
			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (obj->isEffectivelyDead())
				{
					continue;
				}

				SupplyWarehouseDockUpdate* warehouse = (SupplyWarehouseDockUpdate*)obj->findUpdateModule(warehouseKey);
				if (warehouse == nullptr)
				{
					continue;
				}

				const Int boxes = warehouse->getBoxesStored();
				const Int cash = boxes * static_cast<Int>(TheGlobalData->m_baseValuePerSupplyBox);
				if (cash < minimumCash)
				{
					continue;
				}

				outSources.push_back({ obj, boxes, cash });
			}

			return true;
		}

		static Real distanceSq2D(const Coord3D* a, const Coord3D* b)
		{
			const Real dx = a->x - b->x;
			const Real dy = a->y - b->y;
			return dx * dx + dy * dy;
		}

		static bool isSupplyDropoffTemplateName(const std::string& templateName)
		{
			if (!containsIgnoreCase(templateName, "supply"))
			{
				return false;
			}
			if (containsIgnoreCase(templateName, "stash") ||
				containsIgnoreCase(templateName, "center") ||
				containsIgnoreCase(templateName, "dropzone"))
			{
				return true;
			}
			return false;
		}

		struct NearbySupplyDropoffSearchContext
		{
			const Coord3D* sourcePos;
			Real maxDistSq;
			NameKeyType centerDockKey;
			bool found;
		};

		static void findNearbySupplyDropoffCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_STRUCTURE))
			{
				return;
			}
			if (obj->isKindOf(KINDOF_FS_SUPPLY_CENTER) || obj->isKindOf(KINDOF_FS_SUPPLY_DROPZONE))
			{
				NearbySupplyDropoffSearchContext* ctx = static_cast<NearbySupplyDropoffSearchContext*>(userData);
				const Coord3D* pos = obj->getPosition();
				if (ctx->sourcePos != nullptr && pos != nullptr && distanceSq2D(ctx->sourcePos, pos) <= ctx->maxDistSq)
				{
					ctx->found = true;
				}
				return;
			}
			const ThingTemplate* tt = obj->getTemplate();
			if (tt == nullptr)
			{
				return;
			}
			const std::string name = tt->getName().str();
			if (!isSupplyDropoffTemplateName(name))
			{
				NearbySupplyDropoffSearchContext* ctx = static_cast<NearbySupplyDropoffSearchContext*>(userData);
				if (ctx->centerDockKey != NAMEKEY_INVALID && obj->findUpdateModule(ctx->centerDockKey) == nullptr)
				{
					return;
				}
			}
			NearbySupplyDropoffSearchContext* ctx = static_cast<NearbySupplyDropoffSearchContext*>(userData);
			const Coord3D* pos = obj->getPosition();
			if (ctx->sourcePos == nullptr || pos == nullptr)
			{
				return;
			}
			if (distanceSq2D(ctx->sourcePos, pos) <= ctx->maxDistSq)
			{
				ctx->found = true;
			}
		}

		static bool hasNearbyOwnedSupplyDropoff(Player* player, Object* supplySource, Real maxDistance)
		{
			if (player == nullptr || supplySource == nullptr)
			{
				return false;
			}
			NameKeyType centerDockKey = NAMEKEY_INVALID;
			if (TheNameKeyGenerator != nullptr)
			{
				centerDockKey = TheNameKeyGenerator->nameToKey("SupplyCenterDockUpdate");
			}
			NearbySupplyDropoffSearchContext ctx = {
				supplySource->getPosition(),
				maxDistance * maxDistance,
				centerDockKey,
				false
			};
			player->iterateObjects(findNearbySupplyDropoffCallback, &ctx);
			return ctx.found;
		}

		struct NearbyBarracksSearchContext
		{
			const Coord3D* targetPos;
			Real maxDistSq;
			bool found;
		};

		static void findNearbyBarracksCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_STRUCTURE))
			{
				return;
			}
			const ThingTemplate* tt = obj->getTemplate();
			if (tt == nullptr)
			{
				return;
			}
			const std::string name = tt->getName().str();
			if (!containsIgnoreCase(name, "barracks"))
			{
				return;
			}

			NearbyBarracksSearchContext* ctx = static_cast<NearbyBarracksSearchContext*>(userData);
			const Coord3D* pos = obj->getPosition();
			if (ctx->targetPos == nullptr || pos == nullptr)
			{
				return;
			}
			if (distanceSq2D(ctx->targetPos, pos) <= ctx->maxDistSq)
			{
				ctx->found = true;
			}
		}

		static bool hasNearbyOwnedBarracks(Player* player, const Coord3D* pos, Real maxDistance)
		{
			if (player == nullptr || pos == nullptr)
			{
				return false;
			}
			NearbyBarracksSearchContext ctx = { pos, maxDistance * maxDistance, false };
			player->iterateObjects(findNearbyBarracksCallback, &ctx);
			return ctx.found;
		}

		Object* chooseClosestSupplySource(
			const std::vector<SupplySourceInfo>& sources,
			const Coord3D* origin,
			Player* player,
			bool preferUnclaimed,
			Int avoidSourceId = -1)
		{
			Object* best = nullptr;
			Real bestDistSq = 0.0f;
			Object* bestUnclaimed = nullptr;
			Real bestUnclaimedDistSq = 0.0f;

			for (const SupplySourceInfo& info : sources)
			{
				if (info.source == nullptr || info.source->getPosition() == nullptr)
				{
					continue;
				}
				if (avoidSourceId > 0 && static_cast<Int>(info.source->getID()) == avoidSourceId)
				{
					continue;
				}
				const Real distSq = distanceSq2D(info.source->getPosition(), origin);
				if (best == nullptr || distSq < bestDistSq)
				{
					best = info.source;
					bestDistSq = distSq;
				}

				if (!preferUnclaimed)
				{
					continue;
				}
				if (hasNearbyOwnedSupplyDropoff(player, info.source, 650.0f))
				{
					continue;
				}
				if (bestUnclaimed == nullptr || distSq < bestUnclaimedDistSq)
				{
					bestUnclaimed = info.source;
					bestUnclaimedDistSq = distSq;
				}
			}

			if (bestUnclaimed != nullptr)
			{
				return bestUnclaimed;
			}
			return best;
		}

		bool findBuildLocationNearSupply(Player* player, Object* worker, Object* supplySource, const ThingTemplate* buildingTemplate, Coord3D& outLocation, Real& outAngle)
		{
			if (player == nullptr || worker == nullptr || supplySource == nullptr || buildingTemplate == nullptr || TheBuildAssistant == nullptr)
			{
				return false;
			}

			const Coord3D* supplyPos = supplySource->getPosition();
			if (supplyPos == nullptr)
			{
				return false;
			}

			const Real placeAngle = buildingTemplate->getPlacementViewAngle();
			const Real baseRadius = supplySource->getGeometryInfo().getBoundingCircleRadius() + 20.0f;
			const UnsignedInt legalOpts =
				BuildAssistant::TERRAIN_RESTRICTIONS |
				BuildAssistant::CLEAR_PATH |
				BuildAssistant::NO_OBJECT_OVERLAP |
				BuildAssistant::SHROUD_REVEALED;
			const Coord3D* workerPos = worker->getPosition();

			bool found = false;
			Real bestDistSq = 0.0f;
			Coord3D best = *supplyPos;
			best.z = 0.0f;

			for (Real ring = baseRadius; ring <= baseRadius + 450.0f; ring += 20.0f)
			{
				for (Int i = 0; i < 36; ++i)
				{
					const Real theta = static_cast<Real>(i) * (6.28318530717958647692f / 36.0f);
					Coord3D candidate = *supplyPos;
					candidate.x += std::cos(theta) * ring;
					candidate.y += std::sin(theta) * ring;
					candidate.z = 0.0f;

					if (TheBuildAssistant->isLocationLegalToBuild(&candidate, buildingTemplate, placeAngle, legalOpts, worker, nullptr) != LBC_OK)
					{
						continue;
					}

					const Real distSq = workerPos != nullptr ? distanceSq2D(&candidate, workerPos) : 0.0f;
					if (!found || distSq < bestDistSq)
					{
						found = true;
						bestDistSq = distSq;
						best = candidate;
					}
				}
			}

			if (TheTerrainVisual != nullptr)
			{
				TheTerrainVisual->removeAllBibs();
			}

			if (!found)
			{
				return false;
			}

			outLocation = best;
			outAngle = placeAngle;
			return true;
		}

		std::string inferSupplyBuildingTemplateForPlayer(const Player* player, Object* worker = nullptr) const
		{
			if (player == nullptr || TheThingFactory == nullptr)
			{
				return std::string();
			}

			auto canBuildTemplate = [&](const std::string& templateName) -> bool
			{
				if (templateName.empty())
				{
					return false;
				}
				if (worker == nullptr || TheBuildAssistant == nullptr)
				{
					return true;
				}
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(templateName.c_str()), false);
				if (tt == nullptr)
				{
					return false;
				}
				return TheBuildAssistant->isPossibleToMakeUnit(worker, tt) == TRUE;
			};

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (canBuildTemplate("GLASupplyStash"))
				{
					return "GLASupplyStash";
				}
			}
			if (containsIgnoreCase(side, "china") || containsIgnoreCase(baseSide, "china"))
			{
				if (canBuildTemplate("ChinaSupplyCenter"))
				{
					return "ChinaSupplyCenter";
				}
			}
			if (containsIgnoreCase(side, "america") || containsIgnoreCase(baseSide, "america") || containsIgnoreCase(side, "usa") || containsIgnoreCase(baseSide, "usa"))
			{
				if (canBuildTemplate("AmericaSupplyCenter"))
				{
					return "AmericaSupplyCenter";
				}
			}

			const char* knownCandidates[] = {
				"GLASupplyStash",
				"GLASupplyCenter",
				"ChinaSupplyCenter",
				"AmericaSupplyCenter"
			};
			for (const char* candidate : knownCandidates)
			{
				if (canBuildTemplate(candidate))
				{
					return candidate;
				}
			}

			if (worker != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "supply"))
					{
						continue;
					}
					if (!containsIgnoreCase(name, "stash") && !containsIgnoreCase(name, "center"))
					{
						continue;
					}
					if (containsIgnoreCase(name, "dock") || containsIgnoreCase(name, "warehouse"))
					{
						continue;
					}
					if (TheBuildAssistant->isPossibleToMakeUnit(worker, tt) != TRUE)
					{
						continue;
					}
					return name;
				}
			}
			return std::string();
		}

		std::string inferBarracksTemplateForPlayer(const Player* player, Object* worker = nullptr) const
		{
			if (player == nullptr || TheThingFactory == nullptr)
			{
				return std::string();
			}

			auto canBuildTemplate = [&](const std::string& templateName) -> bool
			{
				if (templateName.empty())
				{
					return false;
				}
				if (worker == nullptr || TheBuildAssistant == nullptr)
				{
					return true;
				}
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(templateName.c_str()), false);
				if (tt == nullptr)
				{
					return false;
				}
				return TheBuildAssistant->isPossibleToMakeUnit(worker, tt) == TRUE;
			};

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (canBuildTemplate("GLABarracks"))
				{
					return "GLABarracks";
				}
			}
			if (containsIgnoreCase(side, "china") || containsIgnoreCase(baseSide, "china"))
			{
				if (canBuildTemplate("ChinaBarracks"))
				{
					return "ChinaBarracks";
				}
			}
			if (containsIgnoreCase(side, "america") || containsIgnoreCase(baseSide, "america") || containsIgnoreCase(side, "usa") || containsIgnoreCase(baseSide, "usa"))
			{
				if (canBuildTemplate("AmericaBarracks"))
				{
					return "AmericaBarracks";
				}
			}

			const char* knownCandidates[] = {
				"GLABarracks",
				"ChinaBarracks",
				"AmericaBarracks"
			};
			for (const char* candidate : knownCandidates)
			{
				if (canBuildTemplate(candidate))
				{
					return candidate;
				}
			}

			if (worker != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "barracks"))
					{
						continue;
					}
					if (TheBuildAssistant->isPossibleToMakeUnit(worker, tt) != TRUE)
					{
						continue;
					}
					return name;
				}
			}
			return std::string();
		}

		std::string inferCommandCenterTemplateForPlayer(const Player* player, Object* worker = nullptr) const
		{
			if (player == nullptr || TheThingFactory == nullptr)
			{
				return std::string();
			}

			auto canBuildTemplate = [&](const std::string& templateName) -> bool
			{
				if (templateName.empty())
				{
					return false;
				}
				if (worker == nullptr || TheBuildAssistant == nullptr)
				{
					return true;
				}
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(templateName.c_str()), false);
				if (tt == nullptr)
				{
					return false;
				}
				return TheBuildAssistant->isPossibleToMakeUnit(worker, tt) == TRUE;
			};

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (canBuildTemplate("GLACommandCenter"))
				{
					return "GLACommandCenter";
				}
			}
			if (containsIgnoreCase(side, "china") || containsIgnoreCase(baseSide, "china"))
			{
				if (canBuildTemplate("ChinaCommandCenter"))
				{
					return "ChinaCommandCenter";
				}
			}
			if (containsIgnoreCase(side, "america") || containsIgnoreCase(baseSide, "america") || containsIgnoreCase(side, "usa") || containsIgnoreCase(baseSide, "usa"))
			{
				if (canBuildTemplate("AmericaCommandCenter"))
				{
					return "AmericaCommandCenter";
				}
			}

			const char* knownCandidates[] = {
				"GLACommandCenter",
				"ChinaCommandCenter",
				"AmericaCommandCenter"
			};
			for (const char* candidate : knownCandidates)
			{
				if (canBuildTemplate(candidate))
				{
					return candidate;
				}
			}

			if (worker != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "command") || !containsIgnoreCase(name, "center"))
					{
						continue;
					}
					if (TheBuildAssistant->isPossibleToMakeUnit(worker, tt) != TRUE)
					{
						continue;
					}
					return name;
				}
			}
			return std::string();
		}

		struct CommandCenterSearchContext
		{
			Object* firstCommandCenter;
		};

		static void findCommandCenterCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_COMMANDCENTER))
			{
				return;
			}

			CommandCenterSearchContext* ctx = static_cast<CommandCenterSearchContext*>(userData);
			if (ctx->firstCommandCenter == nullptr)
			{
				ctx->firstCommandCenter = obj;
			}
		}

		Object* findPrimaryCommandCenter(Player* player) const
		{
			if (player == nullptr)
			{
				return nullptr;
			}
			CommandCenterSearchContext ctx = { nullptr };
			player->iterateObjects(findCommandCenterCallback, &ctx);
			return ctx.firstCommandCenter;
		}

		bool findBuildLocationAroundAnchor(
			Player* player,
			Object* worker,
			Object* anchor,
			const ThingTemplate* buildingTemplate,
			Coord3D& outLocation,
			Real& outAngle)
		{
			if (player == nullptr || worker == nullptr || buildingTemplate == nullptr || TheBuildAssistant == nullptr)
			{
				return false;
			}

			const Coord3D* anchorPos = anchor != nullptr ? anchor->getPosition() : nullptr;
			if (anchorPos == nullptr)
			{
				anchorPos = worker->getPosition();
			}
			if (anchorPos == nullptr)
			{
				return false;
			}

			const Real placeAngle = buildingTemplate->getPlacementViewAngle();
			const Real anchorRadius = anchor != nullptr ? anchor->getGeometryInfo().getBoundingCircleRadius() : 80.0f;
			const Real baseRadius = anchorRadius + 90.0f;
			const UnsignedInt legalOpts =
				BuildAssistant::TERRAIN_RESTRICTIONS |
				BuildAssistant::CLEAR_PATH |
				BuildAssistant::NO_OBJECT_OVERLAP |
				BuildAssistant::SHROUD_REVEALED;
			const Coord3D* workerPos = worker->getPosition();

			bool found = false;
			Real bestDistSq = 0.0f;
			Coord3D best = *anchorPos;
			best.z = 0.0f;
			const bool spacingBarracks = containsIgnoreCase(buildingTemplate->getName().str(), "barracks");
			const Real barracksSpacingRadius = 280.0f;

			for (Int pass = 0; pass < 2 && !found; ++pass)
			{
				const bool enforceSpacing = spacingBarracks && pass == 0;
				for (Real ring = baseRadius; ring <= baseRadius + 600.0f; ring += 24.0f)
				{
					for (Int i = 0; i < 48; ++i)
					{
						const Real theta = static_cast<Real>(i) * (6.28318530717958647692f / 48.0f);
						Coord3D candidate = *anchorPos;
						candidate.x += std::cos(theta) * ring;
						candidate.y += std::sin(theta) * ring;
						candidate.z = 0.0f;

						if (TheBuildAssistant->isLocationLegalToBuild(&candidate, buildingTemplate, placeAngle, legalOpts, worker, nullptr) != LBC_OK)
						{
							continue;
						}
						if (enforceSpacing && hasNearbyOwnedBarracks(player, &candidate, barracksSpacingRadius))
						{
							continue;
						}

						const Real distSq = workerPos != nullptr ? distanceSq2D(&candidate, workerPos) : 0.0f;
						if (!found || distSq < bestDistSq)
						{
							found = true;
							bestDistSq = distSq;
							best = candidate;
						}
					}
				}
			}

			if (TheTerrainVisual != nullptr)
			{
				TheTerrainVisual->removeAllBibs();
			}

			if (!found)
			{
				return false;
			}

			outLocation = best;
			outAngle = placeAngle;
			return true;
		}

		bool executeConstructAtLocation(Object* worker, const ThingTemplate* buildingTemplate, const Coord3D& location, Real angle, std::string& reason)
		{
			if (worker == nullptr || buildingTemplate == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = worker->getControllingPlayer();
			if (player == nullptr)
			{
				reason = "player_not_found";
				return false;
			}

			const Money* wallet = player->getMoney();
			const UnsignedInt currentMoney = wallet != nullptr ? wallet->countMoney() : 0u;
			if (buildingTemplate->calcCostToBuild(player) > currentMoney)
			{
				reason = "no_money";
				return false;
			}

			const UnsignedInt legalOpts =
				BuildAssistant::TERRAIN_RESTRICTIONS |
				BuildAssistant::CLEAR_PATH |
				BuildAssistant::NO_OBJECT_OVERLAP |
				BuildAssistant::SHROUD_REVEALED;
			const LegalBuildCode preLegal = TheBuildAssistant->isLocationLegalToBuild(&location, buildingTemplate, angle, legalOpts, worker, nullptr);
			if (preLegal != LBC_OK)
			{
				switch (preLegal)
				{
				case LBC_SHROUD:
					reason = "blocked_by_shroud";
					break;
				case LBC_OBJECTS_IN_THE_WAY:
					reason = "blocked_by_objects";
					break;
				case LBC_NO_CLEAR_PATH:
					reason = "no_clear_path";
					break;
				case LBC_TOO_CLOSE_TO_SUPPLIES:
					reason = "too_close_to_supply";
					break;
				case LBC_RESTRICTED_TERRAIN:
				case LBC_NOT_FLAT_ENOUGH:
				default:
					reason = "no_legal_build_location";
					break;
				}
				return false;
			}

			if (TheBuildAssistant->buildObjectNow(worker, buildingTemplate, &location, angle, player) == nullptr)
			{
				const CanMakeType canMake = TheBuildAssistant->canMakeUnit(worker, buildingTemplate);
				switch (canMake)
				{
				case CANMAKE_NO_PREREQ:
					reason = "no_prereq";
					return false;
				case CANMAKE_NO_MONEY:
					reason = "no_money";
					return false;
				case CANMAKE_FACTORY_IS_DISABLED:
					reason = "factory_disabled";
					return false;
				default:
					break;
				}

				const LegalBuildCode postLegal = TheBuildAssistant->isLocationLegalToBuild(&location, buildingTemplate, angle, legalOpts, worker, nullptr);
				switch (postLegal)
				{
				case LBC_SHROUD:
					reason = "blocked_by_shroud";
					return false;
				case LBC_OBJECTS_IN_THE_WAY:
					reason = "blocked_by_objects";
					return false;
				case LBC_NO_CLEAR_PATH:
					reason = "no_clear_path";
					return false;
				case LBC_TOO_CLOSE_TO_SUPPLIES:
					reason = "too_close_to_supply";
					return false;
				default:
					break;
				}

				reason = "construct_failed";
				return false;
			}

			return true;
		}

		bool executeGameQueueUnit(const nlohmann::json& message, std::string& reason)
		{
			if (TheThingFactory == nullptr)
			{
				reason = "thing_factory_not_ready";
				return false;
			}
			if (TheBuildAssistant == nullptr)
			{
				reason = "build_assistant_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string unitTemplateName = getJsonString(*argsIt, "unit_template");
			if (unitTemplateName.empty())
			{
				reason = "missing_unit_template";
				return false;
			}

			const std::string producerKind = getJsonString(*argsIt, "producer_kind");
			const bool requireCommandCenter = producerKind.empty() || producerKind == "command_center";

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* producer = resolveProducerFromArgs(player, message, requireCommandCenter, reason);
			if (producer == nullptr)
			{
				return false;
			}

			const ThingTemplate* unitTemplate = TheThingFactory->findTemplate(AsciiString(unitTemplateName.c_str()), false);
			if (unitTemplate == nullptr)
			{
				reason = "unit_template_not_found";
				return false;
			}

			ProductionUpdateInterface* production = producer->getProductionUpdateInterface();
			if (production == nullptr)
			{
				reason = "producer_not_factory";
				return false;
			}

			const CanMakeType canMake = TheBuildAssistant->canMakeUnit(producer, unitTemplate);
			if (canMake != CANMAKE_OK)
			{
				switch (canMake)
				{
				case CANMAKE_NO_PREREQ:
					reason = "no_prereq";
					break;
				case CANMAKE_NO_MONEY:
					reason = "no_money";
					break;
				case CANMAKE_FACTORY_IS_DISABLED:
					reason = "factory_disabled";
					break;
				case CANMAKE_QUEUE_FULL:
					reason = "queue_full";
					break;
				case CANMAKE_PARKING_PLACES_FULL:
					reason = "parking_full";
					break;
				case CANMAKE_MAXED_OUT_FOR_PLAYER:
					reason = "maxed_out_for_player";
					break;
				default:
					reason = "cannot_make_unit";
					break;
				}
				return false;
			}

			const ProductionID productionId = production->requestUniqueUnitID();
			if (!production->queueCreateUnit(unitTemplate, productionId))
			{
				reason = "unit_queue_rejected_internal";
				return false;
			}

			return true;
		}

		bool executeGameBuildWorker(const nlohmann::json& message, std::string& reason)
		{
			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			bool requireCommandCenter = true;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const std::string producerKind = getJsonString(*argsIt, "producer_kind");
				if (!producerKind.empty() && producerKind == "any")
				{
					requireCommandCenter = false;
				}
			}

			Object* producer = resolveProducerFromArgs(player, message, requireCommandCenter, reason);
			if (producer == nullptr)
			{
				return false;
			}

			std::string unitTemplateName;
			if (argsIt != message.end() && argsIt->is_object())
			{
				unitTemplateName = getJsonString(*argsIt, "unit_template");
			}

			// Explicit template requested: use it directly.
			if (!unitTemplateName.empty())
			{
				nlohmann::json queuedMessage = message;
				nlohmann::json queueArgs = nlohmann::json::object();
				if (argsIt != message.end() && argsIt->is_object())
				{
					queueArgs = *argsIt;
				}
				queueArgs["unit_template"] = unitTemplateName;
				queueArgs["producer_kind"] = requireCommandCenter ? "command_center" : "any";
				queuedMessage["args"] = queueArgs;
				return executeGameQueueUnit(queuedMessage, reason);
			}

			// Try best-guess template first.
			std::vector<std::string> candidates;
			const std::string inferred = inferWorkerTemplateForPlayer(player);
			if (!inferred.empty())
			{
				candidates.push_back(inferred);
			}
			candidates.push_back("GLAInfantryWorker");
			candidates.push_back("AmericaVehicleDozer");
			candidates.push_back("ChinaVehicleDozer");

			auto queueCandidate = [&](const std::string& candidateTemplate) -> bool
			{
				nlohmann::json queueArgs = nlohmann::json::object();
				if (argsIt != message.end() && argsIt->is_object())
				{
					queueArgs = *argsIt;
				}
				queueArgs["unit_template"] = candidateTemplate;
				queueArgs["producer_kind"] = requireCommandCenter ? "command_center" : "any";
				nlohmann::json queuedMessage = message;
				queuedMessage["args"] = queueArgs;
				std::string candidateReason;
				if (executeGameQueueUnit(queuedMessage, candidateReason))
				{
					return true;
				}
				reason = candidateReason;
				return false;
			};

			for (const std::string& candidate : candidates)
			{
				if (candidate.empty())
				{
					continue;
				}
				if (queueCandidate(candidate))
				{
					return true;
				}
			}

			// Fallback scan: pick first Worker/Dozer template that this command center can make.
			if (TheThingFactory != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string templateName = tt->getName().str();
					if (!containsIgnoreCase(templateName, "worker") && !containsIgnoreCase(templateName, "dozer"))
					{
						continue;
					}
					if (TheBuildAssistant->canMakeUnit(producer, tt) != CANMAKE_OK)
					{
						continue;
					}
					if (queueCandidate(templateName))
					{
						return true;
					}
				}
			}
			return false;
		}

		bool executeGameFindSupplySources(const nlohmann::json& message, nlohmann::json& result, std::string& reason)
		{
			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Int minimumCash = 1;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto minCashIt = argsIt->find("minimum_cash");
				if (minCashIt != argsIt->end() && minCashIt->is_number_integer())
				{
					minimumCash = minCashIt->get<Int>();
				}
			}

			std::vector<SupplySourceInfo> sources;
			if (!collectSupplySources(minimumCash, sources))
			{
				reason = "supply_scan_not_ready";
				return false;
			}

			nlohmann::json rows = nlohmann::json::array();
			for (const SupplySourceInfo& info : sources)
			{
				const Coord3D* pos = info.source->getPosition();
				rows.push_back(nlohmann::json{
					{"object_id", static_cast<Int>(info.source->getID())},
					{"owner_player_index", info.source->getControllingPlayer() != nullptr ? info.source->getControllingPlayer()->getPlayerIndex() : -1},
					{"template_name", info.source->getTemplate() != nullptr ? info.source->getTemplate()->getName().str() : ""},
					{"boxes_stored", info.boxesStored},
					{"cash_value", info.cashValue},
					{"x", pos != nullptr ? pos->x : 0.0f},
					{"y", pos != nullptr ? pos->y : 0.0f},
					{"z", pos != nullptr ? pos->z : 0.0f}
				});
			}

			result = nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"minimum_cash", minimumCash},
				{"count", rows.size()},
				{"sources", rows}
			};
			return true;
		}

		bool executeGameFindBuildLocationNearSupply(const nlohmann::json& message, nlohmann::json& result, std::string& reason)
		{
			if (TheThingFactory == nullptr)
			{
				reason = "thing_factory_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* worker = resolveWorkerFromArgs(player, message, false, reason);
			if (worker == nullptr)
			{
				return false;
			}

			std::string buildingTemplateName;
			Int minimumCash = 1;
			Int requestedSupplyId = -1;
			Int avoidSupplyId = -1;
			bool rememberSelectedSupply = false;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const std::string requestedTemplate = getJsonString(*argsIt, "building_template");
				if (!requestedTemplate.empty())
				{
					buildingTemplateName = requestedTemplate;
				}
				const auto minCashIt = argsIt->find("minimum_cash");
				if (minCashIt != argsIt->end() && minCashIt->is_number_integer())
				{
					minimumCash = minCashIt->get<Int>();
				}
				const auto sourceIdIt = argsIt->find("supply_source_id");
				if (sourceIdIt != argsIt->end() && sourceIdIt->is_number_integer())
				{
					requestedSupplyId = sourceIdIt->get<Int>();
				}
				const auto avoidIdIt = argsIt->find("avoid_supply_source_id");
				if (avoidIdIt != argsIt->end() && avoidIdIt->is_number_integer())
				{
					avoidSupplyId = avoidIdIt->get<Int>();
				}
				const auto rememberIt = argsIt->find("remember_supply_choice");
				if (rememberIt != argsIt->end() && rememberIt->is_boolean())
				{
					rememberSelectedSupply = rememberIt->get<bool>();
				}
			}

			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferSupplyBuildingTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "supply_building_template_unknown";
				return false;
			}

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			std::vector<SupplySourceInfo> sources;
			if (!collectSupplySources(minimumCash, sources))
			{
				reason = "supply_scan_not_ready";
				return false;
			}
			if (sources.empty())
			{
				reason = "supply_source_not_found";
				return false;
			}

			Object* selectedSupply = nullptr;
			if (requestedSupplyId > 0)
			{
				for (const SupplySourceInfo& info : sources)
				{
					if (static_cast<Int>(info.source->getID()) == requestedSupplyId)
					{
						selectedSupply = info.source;
						break;
					}
				}
				if (selectedSupply == nullptr)
				{
					reason = "supply_source_not_found";
					return false;
				}
			}
			else
			{
				selectedSupply = chooseClosestSupplySource(sources, worker->getPosition(), player, true, avoidSupplyId);
				if (selectedSupply == nullptr)
				{
					reason = "supply_source_not_found";
					return false;
				}
			}

			Coord3D location;
			Real angle = 0.0f;
			if (!findBuildLocationNearSupply(player, worker, selectedSupply, buildingTemplate, location, angle))
			{
				reason = "no_legal_build_location";
				return false;
			}

			result = nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"worker_object_id", static_cast<Int>(worker->getID())},
				{"supply_source_id", static_cast<Int>(selectedSupply->getID())},
				{"building_template", buildingTemplateName},
				{"angle", angle},
				{"location", nlohmann::json{{"x", location.x}, {"y", location.y}, {"z", location.z}}}
			};
			if (rememberSelectedSupply)
			{
				m_lastAutoSupplySourceByPlayer[player->getPlayerIndex()] = static_cast<Int>(selectedSupply->getID());
			}
			return true;
		}

		bool executeGameDozerConstruct(const nlohmann::json& message, std::string& reason)
		{
			nlohmann::json buildResult;
			if (!executeGameFindBuildLocationNearSupply(message, buildResult, reason))
			{
				return false;
			}

			if (TheGameLogic == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			const std::string buildingTemplateName = argsIt != message.end() && argsIt->is_object()
				? getJsonString(*argsIt, "building_template")
				: std::string();
			const std::string finalTemplateName = !buildingTemplateName.empty()
				? buildingTemplateName
				: buildResult.value("building_template", std::string());

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(finalTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			const Int workerId = buildResult.value("worker_object_id", 0);
			if (workerId <= 0)
			{
				reason = "worker_not_found";
				return false;
			}
			Object* worker = TheGameLogic->findObjectByID(static_cast<ObjectID>(workerId));
			if (worker == nullptr)
			{
				reason = "worker_not_found";
				return false;
			}

			const auto loc = buildResult["location"];
			Coord3D location;
			location.x = loc.value("x", 0.0f);
			location.y = loc.value("y", 0.0f);
			location.z = 0.0f;
			const Real angle = buildResult.value("angle", buildingTemplate->getPlacementViewAngle());

			Player* player = worker->getControllingPlayer();
			if (player == nullptr)
			{
				reason = "player_not_found";
				return false;
			}
			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildSupplyStashAuto(const nlohmann::json& message, std::string& reason)
		{
			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* idleWorker = resolveWorkerFromArgs(player, message, true, reason);
			if (idleWorker == nullptr)
			{
				return false;
			}

			std::string buildingTemplateName;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				buildingTemplateName = getJsonString(*argsIt, "building_template");
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferSupplyBuildingTemplateForPlayer(player, idleWorker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "supply_building_template_unknown";
				return false;
			}

			nlohmann::json bridged = message;
			nlohmann::json bridgedArgs = nlohmann::json::object();
			if (argsIt != message.end() && argsIt->is_object())
			{
				bridgedArgs = *argsIt;
			}
			bridgedArgs["building_template"] = buildingTemplateName;
			bridgedArgs["remember_supply_choice"] = true;
			const auto lastSupplyIt = m_lastAutoSupplySourceByPlayer.find(player->getPlayerIndex());
			if (lastSupplyIt != m_lastAutoSupplySourceByPlayer.end() && lastSupplyIt->second > 0)
			{
				bridgedArgs["avoid_supply_source_id"] = lastSupplyIt->second;
			}
			bridgedArgs["worker_object_id"] = static_cast<Int>(idleWorker->getID());
			bridged["args"] = bridgedArgs;

			return executeGameDozerConstruct(bridged, reason);
		}

		bool executeGameBuildSupplyStashSmart(const nlohmann::json& message, std::string& reason)
		{
			// First try the normal auto-build path.
			std::string buildReason;
			if (executeGameBuildSupplyStashAuto(message, buildReason))
			{
				return true;
			}

			// If failure is unrelated to placement/movement legality, surface it as-is.
			if (buildReason != "no_legal_build_location" &&
				buildReason != "construct_failed" &&
				buildReason != "blocked_by_shroud" &&
				buildReason != "blocked_by_objects" &&
				buildReason != "no_clear_path" &&
				buildReason != "too_close_to_supply")
			{
				reason = buildReason;
				return false;
			}

			if (TheThingFactory == nullptr || TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			std::string buildingTemplateName;
			Int minimumCash = 1;
			Int requestedSupplyId = -1;
			Int avoidSupplyId = -1;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				buildingTemplateName = getJsonString(*argsIt, "building_template");
				const auto minCashIt = argsIt->find("minimum_cash");
				if (minCashIt != argsIt->end() && minCashIt->is_number_integer())
				{
					minimumCash = minCashIt->get<Int>();
				}
				const auto sourceIdIt = argsIt->find("supply_source_id");
				if (sourceIdIt != argsIt->end() && sourceIdIt->is_number_integer())
				{
					requestedSupplyId = sourceIdIt->get<Int>();
				}
			}
			if (requestedSupplyId <= 0)
			{
				const auto lastSupplyIt = m_lastAutoSupplySourceByPlayer.find(player->getPlayerIndex());
				if (lastSupplyIt != m_lastAutoSupplySourceByPlayer.end() && lastSupplyIt->second > 0)
				{
					avoidSupplyId = lastSupplyIt->second;
				}
			}

			Object* worker = resolveWorkerFromArgs(player, message, false, reason);
			if (worker == nullptr)
			{
				return false;
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferSupplyBuildingTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "supply_building_template_unknown";
				return false;
			}

			std::vector<SupplySourceInfo> sources;
			if (!collectSupplySources(minimumCash, sources))
			{
				reason = "supply_scan_not_ready";
				return false;
			}
			if (sources.empty())
			{
				reason = "supply_source_not_found";
				return false;
			}

			Object* selectedSupply = nullptr;
			if (requestedSupplyId > 0)
			{
				for (const SupplySourceInfo& info : sources)
				{
					if (static_cast<Int>(info.source->getID()) == requestedSupplyId)
					{
						selectedSupply = info.source;
						break;
					}
				}
				if (selectedSupply == nullptr)
				{
					reason = "supply_source_not_found";
					return false;
				}
			}
			else
			{
				selectedSupply = chooseClosestSupplySource(sources, worker->getPosition(), player, true, avoidSupplyId);
				if (selectedSupply == nullptr)
				{
					reason = "supply_source_not_found";
					return false;
				}
			}
			m_lastAutoSupplySourceByPlayer[player->getPlayerIndex()] = static_cast<Int>(selectedSupply->getID());

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			Coord3D revealPos;
			Real revealAngle = 0.0f;
			if (findBuildLocationNearSupply(player, worker, selectedSupply, buildingTemplate, revealPos, revealAngle))
			{
				// Placement should now be legal; retry construct immediately.
				std::string retryReason;
				if (executeGameBuildSupplyStashAuto(message, retryReason))
				{
					return true;
				}
				reason = retryReason;
				return false;
			}

			const Coord3D* supplyPos = selectedSupply->getPosition();
			if (supplyPos == nullptr)
			{
				reason = "supply_source_not_found";
				return false;
			}

			AIUpdateInterface* ai = worker->getAI();
			if (ai == nullptr)
			{
				reason = "worker_no_ai";
				return false;
			}

			// Move toward the supply zone to reveal shroud near likely stash placements.
			Coord3D target = *supplyPos;
			const Coord3D* workerPos = worker->getPosition();
			if (workerPos != nullptr)
			{
				Real dx = supplyPos->x - workerPos->x;
				Real dy = supplyPos->y - workerPos->y;
				const Real lenSq = dx * dx + dy * dy;
				if (lenSq > 1.0f)
				{
					const Real invLen = 1.0f / std::sqrt(lenSq);
					const Real stopDist = selectedSupply->getGeometryInfo().getBoundingCircleRadius() + 140.0f;
					target.x = supplyPos->x - dx * invLen * stopDist;
					target.y = supplyPos->y - dy * invLen * stopDist;
				}
			}
			target.z = 0.0f;
			ai->aiMoveToPosition(&target, CMD_FROM_AI);
			return true;
		}

		bool executeGameBuildBarracksSmart(const nlohmann::json& message, std::string& reason)
		{
			if (TheThingFactory == nullptr || TheGameLogic == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* worker = resolveWorkerFromArgs(player, message, true, reason);
			if (worker == nullptr)
			{
				return false;
			}

			std::string buildingTemplateName;
			Int requestedAnchorId = -1;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				buildingTemplateName = getJsonString(*argsIt, "building_template");
				const auto anchorIdIt = argsIt->find("anchor_object_id");
				if (anchorIdIt != argsIt->end() && anchorIdIt->is_number_integer())
				{
					requestedAnchorId = anchorIdIt->get<Int>();
				}
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferBarracksTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "barracks_template_unknown";
				return false;
			}

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			Object* anchor = nullptr;
			if (requestedAnchorId > 0)
			{
				anchor = TheGameLogic->findObjectByID(static_cast<ObjectID>(requestedAnchorId));
				if (anchor == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				if (anchor->getControllingPlayer() != player)
				{
					reason = "anchor_not_owned";
					return false;
				}
			}
			if (anchor == nullptr)
			{
				anchor = findPrimaryCommandCenter(player);
			}

			Coord3D location;
			Real angle = 0.0f;
			if (!findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle))
			{
				AIUpdateInterface* ai = worker->getAI();
				if (ai == nullptr)
				{
					reason = "worker_no_ai";
					return false;
				}

				const Coord3D* moveTarget = anchor != nullptr ? anchor->getPosition() : worker->getPosition();
				if (moveTarget == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				Coord3D target = *moveTarget;
				target.z = 0.0f;
				ai->aiMoveToPosition(&target, CMD_FROM_AI);
				return true;
			}

			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildCommandCenterSmart(const nlohmann::json& message, std::string& reason)
		{
			if (TheThingFactory == nullptr || TheGameLogic == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* worker = resolveWorkerFromArgs(player, message, true, reason);
			if (worker == nullptr)
			{
				return false;
			}

			Int requestedAnchorId = -1;
			std::string buildingTemplateName;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				buildingTemplateName = getJsonString(*argsIt, "building_template");
				const auto anchorIdIt = argsIt->find("anchor_object_id");
				if (anchorIdIt != argsIt->end() && anchorIdIt->is_number_integer())
				{
					requestedAnchorId = anchorIdIt->get<Int>();
				}
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferCommandCenterTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "command_center_template_unknown";
				return false;
			}

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			Object* anchor = nullptr;
			if (requestedAnchorId > 0)
			{
				anchor = TheGameLogic->findObjectByID(static_cast<ObjectID>(requestedAnchorId));
				if (anchor == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				if (anchor->getControllingPlayer() != player)
				{
					reason = "anchor_not_owned";
					return false;
				}
			}
			if (anchor == nullptr)
			{
				anchor = findPrimaryCommandCenter(player);
			}
			if (anchor == nullptr)
			{
				reason = "anchor_not_found";
				return false;
			}

			Coord3D location;
			Real angle = 0.0f;
			if (!findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle))
			{
				AIUpdateInterface* ai = worker->getAI();
				if (ai == nullptr)
				{
					reason = "worker_no_ai";
					return false;
				}

				const Coord3D* moveTarget = anchor->getPosition();
				if (moveTarget == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				Coord3D target = *moveTarget;
				target.z = 0.0f;
				ai->aiMoveToPosition(&target, CMD_FROM_AI);
				return true;
			}

			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameAttackMove(const nlohmann::json& message, std::string& reason)
		{
			if (TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto xIt = argsIt->find("x");
			const auto yIt = argsIt->find("y");
			if (xIt == argsIt->end() || yIt == argsIt->end() || !xIt->is_number() || !yIt->is_number())
			{
				reason = "missing_target_position";
				return false;
			}

			Coord3D target;
			target.x = xIt->get<Real>();
			target.y = yIt->get<Real>();
			target.z = 0.0f;

			std::vector<Int> objectIds;
			const auto objectIdsIt = argsIt->find("object_ids");
			if (objectIdsIt != argsIt->end() && objectIdsIt->is_array())
			{
				for (const auto& idNode : *objectIdsIt)
				{
					if (idNode.is_number_integer())
					{
						const Int id = idNode.get<Int>();
						if (id > 0)
						{
							objectIds.push_back(id);
						}
					}
				}
			}
			if (objectIds.empty())
			{
				const auto objectIdIt = argsIt->find("object_id");
				if (objectIdIt != argsIt->end() && objectIdIt->is_number_integer())
				{
					const Int id = objectIdIt->get<Int>();
					if (id > 0)
					{
						objectIds.push_back(id);
					}
				}
			}
			if (objectIds.empty())
			{
				reason = "missing_object_ids";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Int commanded = 0;
			for (Int id : objectIds)
			{
				Object* obj = TheGameLogic->findObjectByID(static_cast<ObjectID>(id));
				if (obj == nullptr || obj->isEffectivelyDead())
				{
					continue;
				}
				if (obj->getControllingPlayer() != player)
				{
					continue;
				}

				AIUpdateInterface* ai = obj->getAI();
				if (ai == nullptr)
				{
					continue;
				}

				ai->aiAttackMoveToPosition(&target, 0, CMD_FROM_AI);
				++commanded;
			}

			if (commanded == 0)
			{
				reason = "no_valid_objects";
				return false;
			}
			return true;
		}

		nlohmann::json buildPlayerDetails(Player* player) const
		{
			return nlohmann::json{
				{"player", buildLocalPlayerSummary(player)},
				{"resources", buildResourcesSummary(player)},
				{"units", buildUnitCountsSummary(player)}
			};
		}

		nlohmann::json buildResourcesSummary(const Player* player) const
		{
			UnsignedInt money = 0u;
			const Money* wallet = player->getMoney();
			if (wallet != nullptr)
			{
				money = wallet->countMoney();
			}

			return nlohmann::json{
				{"money", money},
				{"skill_points", player->getSkillPoints()},
				{"science_purchase_points", player->getSciencePurchasePoints()},
				{"rank_level", player->getRankLevel()}
			};
		}

		nlohmann::json buildUnitCountsSummary(Player* player) const
		{
			const KindOfMaskType none = KINDOFMASK_NONE;
			const KindOfMaskType structureMask = MAKE_KINDOF_MASK(KINDOF_STRUCTURE);
			const KindOfMaskType infantryMask = MAKE_KINDOF_MASK(KINDOF_INFANTRY);
			const KindOfMaskType vehicleMask = MAKE_KINDOF_MASK(KINDOF_VEHICLE);
			const KindOfMaskType aircraftMask = MAKE_KINDOF_MASK(KINDOF_AIRCRAFT);
			const KindOfMaskType dozerMask = MAKE_KINDOF_MASK(KINDOF_DOZER);
			const KindOfMaskType harvesterMask = MAKE_KINDOF_MASK(KINDOF_HARVESTER);

			return nlohmann::json{
				{"buildings", player->countBuildings()},
				{"units_total", player->countObjects(none, structureMask)},
				{"infantry", player->countObjects(infantryMask, none)},
				{"vehicles", player->countObjects(vehicleMask, none)},
				{"aircraft", player->countObjects(aircraftMask, none)},
				{"dozers", player->countObjects(dozerMask, none)},
				{"harvesters", player->countObjects(harvesterMask, none)},
				{"objects_total", player->countObjects(none, none)}
			};
		}

		static const char* classifyObjectClass(const Object* obj)
		{
			if (obj == nullptr)
			{
				return "unknown";
			}
			if (obj->isKindOf(KINDOF_STRUCTURE))
			{
				return "building";
			}
			if (obj->isKindOf(KINDOF_INFANTRY))
			{
				return "infantry";
			}
			if (obj->isKindOf(KINDOF_VEHICLE))
			{
				return "vehicle";
			}
			if (obj->isKindOf(KINDOF_AIRCRAFT))
			{
				return "aircraft";
			}
			return "unit";
		}

		nlohmann::json buildObjectSummaryRow(const Object* obj, bool includeIdleState) const
		{
			nlohmann::json row = nlohmann::json::object();
			if (obj == nullptr)
			{
				return row;
			}

			const Coord3D* pos = obj->getPosition();
			row["id"] = obj->getID();
			row["template"] = obj->getTemplate() != nullptr ? obj->getTemplate()->getName().str() : "";
			row["class"] = classifyObjectClass(obj);
			row["x"] = pos != nullptr ? pos->x : 0.0f;
			row["y"] = pos != nullptr ? pos->y : 0.0f;
			row["z"] = pos != nullptr ? pos->z : 0.0f;
			row["under_construction"] = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
			if (includeIdleState)
			{
				const AIUpdateInterface* ai = obj->getAI();
				row["idle"] = ai != nullptr ? ai->isIdle() : false;
			}
			return row;
		}

		struct OwnedObjectCollectContext
		{
			AIControlAdapterState* self;
			nlohmann::json* units;
			nlohmann::json* buildings;
		};

		static void collectOwnedObjectsCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			OwnedObjectCollectContext* ctx = static_cast<OwnedObjectCollectContext*>(userData);
			nlohmann::json row = ctx->self->buildObjectSummaryRow(obj, true);
			if (obj->isKindOf(KINDOF_STRUCTURE))
			{
				ctx->buildings->push_back(row);
			}
			else
			{
				ctx->units->push_back(row);
			}
		}

		nlohmann::json buildOwnedObjectsSummary(Player* player)
		{
			nlohmann::json units = nlohmann::json::array();
			nlohmann::json buildings = nlohmann::json::array();
			if (player == nullptr)
			{
				return nlohmann::json{
					{"player_index", -1},
					{"units", units},
					{"buildings", buildings}
				};
			}

			OwnedObjectCollectContext ctx = { this, &units, &buildings };
			player->iterateObjects(collectOwnedObjectsCallback, &ctx);

			return nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"units", units},
				{"buildings", buildings}
			};
		}

		nlohmann::json buildVisibleEnemiesSummary(Player* localPlayer)
		{
			nlohmann::json enemies = nlohmann::json::array();
			if (localPlayer == nullptr || ThePlayerList == nullptr)
			{
				return nlohmann::json{
					{"count", 0},
					{"enemies", enemies}
				};
			}

			const Int playerCount = ThePlayerList->getPlayerCount();
			for (Int i = 0; i < playerCount; ++i)
			{
				Player* enemyPlayer = ThePlayerList->getNthPlayer(i);
				if (enemyPlayer == nullptr || enemyPlayer == localPlayer)
				{
					continue;
				}
				if (enemyPlayer == ThePlayerList->getNeutralPlayer())
				{
					continue;
				}

				struct EnemyObjectCollectContext
				{
					AIControlAdapterState* self;
					Player* localPlayer;
					nlohmann::json* enemies;
				};
				EnemyObjectCollectContext ctx = { this, localPlayer, &enemies };

				enemyPlayer->iterateObjects([](Object* obj, void* userData)
				{
					if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
					{
						return;
					}
					EnemyObjectCollectContext* ctx = static_cast<EnemyObjectCollectContext*>(userData);
					if (!obj->isLogicallyVisible())
					{
						return;
					}
					nlohmann::json row = ctx->self->buildObjectSummaryRow(obj, false);
					Player* owner = obj->getControllingPlayer();
					row["player_index"] = owner != nullptr ? owner->getPlayerIndex() : -1;
					ctx->enemies->push_back(row);
				}, &ctx);
			}

			return nlohmann::json{
				{"count", enemies.size()},
				{"enemies", enemies}
			};
		}

		bool executeGameQuery(const nlohmann::json& message, nlohmann::json& result, std::string& reason)
		{
			if (ThePlayerList == nullptr)
			{
				reason = "player_state_not_ready";
				return false;
			}

			Player* localPlayer = ThePlayerList->getLocalPlayer();
			if (localPlayer == nullptr)
			{
				reason = "local_player_missing";
				return false;
			}

			std::string path = "game.status";
			bool hasPlayerIndex = false;
			Int requestedPlayerIndex = -1;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const std::string requestedPath = getJsonString(*argsIt, "path");
				if (!requestedPath.empty())
				{
					path = requestedPath;
				}

				const auto playerIndexIt = argsIt->find("player_index");
				if (playerIndexIt != argsIt->end() && playerIndexIt->is_number_integer())
				{
					hasPlayerIndex = true;
					requestedPlayerIndex = playerIndexIt->get<Int>();
				}
			}

			Player* selectedPlayer = localPlayer;
			if (hasPlayerIndex)
			{
				selectedPlayer = getPlayerByIndex(requestedPlayerIndex);
				if (selectedPlayer == nullptr)
				{
					reason = "player_not_found";
					return false;
				}
			}

			if (path == "game.local_player")
			{
				result = buildLocalPlayerSummary(localPlayer);
				return true;
			}

			if (path == "game.resources")
			{
				result = buildResourcesSummary(selectedPlayer);
				return true;
			}

			if (path == "game.faction")
			{
				const PlayerTemplate* playerTemplate = selectedPlayer->getPlayerTemplate();
				result = nlohmann::json{
					{"side", selectedPlayer->getSide().str()},
					{"base_side", selectedPlayer->getBaseSide().str()},
					{"template_name", playerTemplate != nullptr ? playerTemplate->getName().str() : ""},
					{"template_side", playerTemplate != nullptr ? playerTemplate->getSide().str() : ""},
					{"template_base_side", playerTemplate != nullptr ? playerTemplate->getBaseSide().str() : ""},
					{"player_index", selectedPlayer->getPlayerIndex()}
				};
				return true;
			}

			if (path == "game.units")
			{
				result = buildUnitCountsSummary(selectedPlayer);
				result["player_index"] = selectedPlayer->getPlayerIndex();
				return true;
			}

			if (path == "game.objects")
			{
				result = buildOwnedObjectsSummary(selectedPlayer);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			if (path == "game.visible_enemies")
			{
				// Visibility is always from local player's perspective.
				result = buildVisibleEnemiesSummary(localPlayer);
				return true;
			}

			if (path == "game.player")
			{
				result = buildPlayerDetails(selectedPlayer);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			if (path == "game.players")
			{
				nlohmann::json players = nlohmann::json::array();
				const Int count = ThePlayerList->getPlayerCount();
				for (Int i = 0; i < count; ++i)
				{
					Player* player = ThePlayerList->getNthPlayer(i);
					if (player == nullptr)
					{
						continue;
					}

					nlohmann::json row = buildPlayerDetails(player);
					row["is_local_player"] = (player == localPlayer);
					players.push_back(row);
				}

				result = nlohmann::json{
					{"count", players.size()},
					{"players", players}
				};
				return true;
			}

			if (path == "game.status" || path == "game.summary" || path == "game.all")
			{
				result = buildPlayerDetails(selectedPlayer);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			reason = "unsupported_query_path";
			return false;
		}

		static const char* classifyControlType(UnsignedInt style)
		{
			if ((style & GWS_PUSH_BUTTON) != 0u)
			{
				return "button";
			}
			if ((style & GWS_ENTRY_FIELD) != 0u)
			{
				return "text_entry";
			}
			if ((style & GWS_COMBO_BOX) != 0u)
			{
				return "combo_box";
			}
			if ((style & GWS_SCROLL_LISTBOX) != 0u)
			{
				return "list_box";
			}
			if ((style & GWS_CHECK_BOX) != 0u)
			{
				return "check_box";
			}
			if ((style & GWS_RADIO_BUTTON) != 0u)
			{
				return "radio_button";
			}
			return "other";
		}

		static bool includeType(const std::string& requestedKind, const char* actualKind)
		{
			if (requestedKind == "all")
			{
				return true;
			}
			if (requestedKind == "button")
			{
				return strcmp(actualKind, "button") == 0;
			}
			if (requestedKind == "text_entry")
			{
				return strcmp(actualKind, "text_entry") == 0;
			}
			return true;
		}

		void collectControlsRecursive(GameWindow* window, bool includeHidden, const std::string& kind, nlohmann::json& controls)
		{
			for (GameWindow* current = window; current != nullptr; current = current->winGetNext())
			{
				WinInstanceData* inst = current->winGetInstanceData();
				if (inst != nullptr)
				{
					const UnsignedInt status = current->winGetStatus();
					const bool isHidden = (status & WIN_STATUS_HIDDEN) != 0u;
					const bool isEnabled = (status & WIN_STATUS_ENABLED) != 0u;
					const UnsignedInt style = current->winGetStyle();
					const char* controlType = classifyControlType(style);

					if ((style & GWS_GADGET_WINDOW) != 0u && (!isHidden || includeHidden) && includeType(kind, controlType))
					{
						nlohmann::json control = {
							{"controlId", inst->m_decoratedNameString.str()},
							{"windowId", current->winGetWindowId()},
							{"type", controlType},
							{"hidden", isHidden},
							{"enabled", isEnabled}
						};
						controls.push_back(control);
					}
				}

				GameWindow* child = current->winGetChild();
				if (child != nullptr)
				{
					collectControlsRecursive(child, includeHidden, kind, controls);
				}
			}
		}

		nlohmann::json buildControlsInventory(const nlohmann::json& message)
		{
			bool includeHidden = false;
			std::string kind = "all";

			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto hiddenIt = argsIt->find("include_hidden");
				if (hiddenIt != argsIt->end() && hiddenIt->is_boolean())
				{
					includeHidden = hiddenIt->get<bool>();
				}

				const auto kindIt = argsIt->find("kind");
				if (kindIt != argsIt->end() && kindIt->is_string())
				{
					kind = kindIt->get<std::string>();
				}
			}

			nlohmann::json controls = nlohmann::json::array();
			if (TheWindowManager != nullptr)
			{
				GameWindow* root = TheWindowManager->winGetWindowList();
				if (root != nullptr)
				{
					collectControlsRecursive(root, includeHidden, kind, controls);
				}
			}

			nlohmann::json result = {
				{"kind", kind},
				{"include_hidden", includeHidden},
				{"count", controls.size()},
				{"controls", controls}
			};
			return result;
		}

		bool executeChatSend(const nlohmann::json& message, std::string& reason)
		{
			if (TheNetwork == nullptr)
			{
				reason = "network_not_ready";
				return false;
			}
			if (ThePlayerList == nullptr || TheNameKeyGenerator == nullptr)
			{
				reason = "player_state_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string text = getJsonString(*argsIt, "text");
			std::string scope = getJsonString(*argsIt, "scope");
			if (scope.empty())
			{
				scope = "everyone";
			}

			AsciiString asciiText(text.c_str());
			UnicodeString msg;
			msg.translate(asciiText);
			msg.trim();
			if (msg.isEmpty())
			{
				reason = "missing_text";
				return false;
			}

			const Player* localPlayer = ThePlayerList->getLocalPlayer();
			if (localPlayer == nullptr)
			{
				reason = "local_player_missing";
				return false;
			}

			Int playerMask = 0;
			AsciiString playerName;
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				playerName.format("player%d", i);
				const Player* player = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(playerName));
				if (player == nullptr)
				{
					continue;
				}

				if (scope == "everyone")
				{
					if (TheGameInfo == nullptr || !TheGameInfo->getConstSlot(i)->isMuted())
					{
						playerMask |= (1 << i);
					}
				}
				else if (scope == "allies")
				{
					if ((player->getRelationship(localPlayer->getDefaultTeam()) == ALLIES &&
						localPlayer->getRelationship(player->getDefaultTeam()) == ALLIES) || player == localPlayer)
					{
						playerMask |= (1 << i);
					}
				}
				else if (scope == "players")
				{
					if (player == localPlayer)
					{
						playerMask |= (1 << i);
					}
				}
				else
				{
					reason = "invalid_scope";
					return false;
				}
			}

			if (TheLanguageFilter != nullptr)
			{
				TheLanguageFilter->filterLine(msg);
			}

			TheNetwork->sendChat(msg, playerMask);
			return true;
		}

		bool executeMenuClick(const nlohmann::json& message, std::string& reason)
		{
			if (TheShell == nullptr || !TheShell->isShellActive())
			{
				reason = "shell_not_active";
				return false;
			}

			if (TheWindowManager == nullptr || TheNameKeyGenerator == nullptr)
			{
				reason = "ui_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string controlId = getJsonString(*argsIt, "controlId");
			if (controlId.empty())
			{
				reason = "missing_controlId";
				return false;
			}

			const std::string normalizedControlId = normalizeControlId(controlId);
			const NameKeyType key = TheNameKeyGenerator->nameToKey(normalizedControlId.c_str());
			GameWindow* control = TheWindowManager->winGetWindowFromId(nullptr, key);

			// Context-aware back aliasing for menus that use different Back control names.
			if (control == nullptr && (controlId == "ButtonBack" || controlId == "MainMenu.wnd:ButtonBack"))
			{
				control = findFirstUsableControl({
					"MainMenu.wnd:ButtonSingleBack",
					"MainMenu.wnd:ButtonMultiBack",
					"MainMenu.wnd:ButtonLoadReplayBack",
					"MainMenu.wnd:ButtonDiffBack",
					"LanLobbyMenu.wnd:ButtonBack",
					"NetworkDirectConnect.wnd:ButtonBack"
				});
			}
			if (control == nullptr)
			{
				reason = "control_not_found";
				return false;
			}

			const UnsignedInt status = control->winGetStatus();
			if ((status & WIN_STATUS_HIDDEN) != 0u)
			{
				reason = "control_hidden";
				return false;
			}

			if ((status & WIN_STATUS_ENABLED) == 0u)
			{
				reason = "control_disabled";
				return false;
			}

			GameWindow* parent = control->winGetParent();
			if (parent == nullptr)
			{
				reason = "control_parent_missing";
				return false;
			}

			TheWindowManager->winSendSystemMsg(parent, GBM_SELECTED, (WindowMsgData)control, control->winGetWindowId());
			return true;
		}

		bool executeMenuSetText(const nlohmann::json& message, std::string& reason)
		{
			if (TheShell == nullptr || !TheShell->isShellActive())
			{
				reason = "shell_not_active";
				return false;
			}

			if (TheWindowManager == nullptr || TheNameKeyGenerator == nullptr)
			{
				reason = "ui_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string controlId = getJsonString(*argsIt, "controlId");
			const std::string text = getJsonString(*argsIt, "text");
			if (controlId.empty())
			{
				reason = "missing_controlId";
				return false;
			}

			const std::string normalizedControlId = normalizeControlId(controlId);
			const NameKeyType key = TheNameKeyGenerator->nameToKey(normalizedControlId.c_str());
			GameWindow* control = TheWindowManager->winGetWindowFromId(nullptr, key);
			if (control == nullptr)
			{
				reason = "control_not_found";
				return false;
			}

			const UnsignedInt status = control->winGetStatus();
			if ((status & WIN_STATUS_HIDDEN) != 0u)
			{
				reason = "control_hidden";
				return false;
			}

			if ((status & WIN_STATUS_ENABLED) == 0u)
			{
				reason = "control_disabled";
				return false;
			}

			const UnsignedInt style = control->winGetStyle();
			if ((style & GWS_ENTRY_FIELD) == 0u)
			{
				reason = "control_not_text_entry";
				return false;
			}

			AsciiString asciiText(text.c_str());
			UnicodeString unicodeText;
			unicodeText.translate(asciiText);
			GadgetTextEntrySetText(control, unicodeText);
			return true;
		}
	};

	AIControlAdapterState g_adapterState;
}

void AIControlAdapterUpdate()
{
	g_adapterState.update();
}

void AIControlAdapterReset()
{
	g_adapterState.reset();
}
