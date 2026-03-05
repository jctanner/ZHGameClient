#include "PreRTS.h"

#include "GameClient/AIControlAdapter.h"

#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/BuildAssistant.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
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
				sendProtocolError(std::string(), "bad_request", "invalid_json");
				return;
			}

			const std::string type = getJsonString(message, "type");
			const std::string requestId = getJsonString(message, "request_id");

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
					{"capabilities", nlohmann::json::array({"session", "menu_click", "menu_set_text", "chat_send", "game_query", "game_queue_unit"})}
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

			sendActionAck(requestId, false, "unsupported_cmd", "unsupported_session_command");
		}

		nlohmann::json buildLocalPlayerSummary(const Player* player) const
		{
			const PlayerType playerType = player->getPlayerType();
			const bool isAi = (playerType == PLAYER_COMPUTER);
			nlohmann::json local = {
				{"player_index", player->getPlayerIndex()},
				{"player_name_key", KEYNAME(player->getPlayerNameKey()).str()},
				{"side", player->getSide().str()},
				{"base_side", player->getBaseSide().str()},
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

			Object* producer = resolveProducerFromArgs(player, message, true, reason);
			if (producer == nullptr)
			{
				return false;
			}

			std::string unitTemplateName;
			const auto argsIt = message.find("args");
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
				queueArgs["producer_kind"] = "command_center";
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
				queueArgs["producer_kind"] = "command_center";
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
