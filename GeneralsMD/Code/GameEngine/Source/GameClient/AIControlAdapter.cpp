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
#include "Common/MessageStream.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
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
#include <cstdarg>
#include <cstdio>
#include <share.h>

namespace
{
	struct PendingBuildLocationReservation
	{
		Int playerIndex;
		std::string templateName;
		Coord3D location;
		Real radiusSq;
		DWORD untilTick;
	};

	class AIControlAdapterState
	{
	public:
		AIControlAdapterState() :
			m_pipe(INVALID_HANDLE_VALUE),
			m_hasClient(false),
			m_adapterLog(nullptr),
			m_adapterLogPath("D:\\logs\\adapter.log"),
			m_ownedCacheValid(false),
			m_ownedCachePlayerIndex(-1),
			m_ownedCacheUnitsTotal(0),
			m_ownedCacheBuildingsTotal(0),
			m_ownedCacheIdleWorkersTotal(0),
			m_ownedCacheVersion(0),
			m_ownedCacheLastRefreshTick(0u)
		{
			char buffer[32];
			sprintf_s(buffer, "%08X%08X", static_cast<unsigned int>(::GetCurrentProcessId()), static_cast<unsigned int>(::GetTickCount()));
			m_sessionId = buffer;
			adapterLog("adapter_start pid=%lu session=%s", static_cast<unsigned long>(::GetCurrentProcessId()), m_sessionId.c_str());
		}

		~AIControlAdapterState()
		{
			closePipe();
			closeAdapterLog();
		}

		void reset()
		{
			m_lineBuffer.clear();
			m_lastAutoSupplySourceByPlayer.clear();
			m_reservedBuildLocations.clear();
			m_buildExpansionRadiusByKey.clear();
			m_ownedCacheValid = false;
			m_ownedCachePlayerIndex = -1;
			m_ownedCacheUnitsTotal = 0;
			m_ownedCacheBuildingsTotal = 0;
			m_ownedCacheIdleWorkersTotal = 0;
			m_ownedCacheUnits = nlohmann::json::array();
			m_ownedCacheBuildings = nlohmann::json::array();
			m_ownedCacheIdleWorkers = nlohmann::json::array();
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
		FILE* m_adapterLog;
		std::string m_adapterLogPath;
		std::string m_lineBuffer;
		std::string m_sessionId;
		std::unordered_map<Int, Int> m_lastAutoSupplySourceByPlayer;
		std::unordered_map<Int, ObjectID> m_lastSelectedWorkerByPlayer;
		std::unordered_map<ObjectID, DWORD> m_reservedWorkersUntilTick;
		std::vector<PendingBuildLocationReservation> m_reservedBuildLocations;
		std::unordered_map<std::string, Real> m_buildExpansionRadiusByKey;
		bool m_ownedCacheValid;
		Int m_ownedCachePlayerIndex;
		Int m_ownedCacheUnitsTotal;
		Int m_ownedCacheBuildingsTotal;
		Int m_ownedCacheIdleWorkersTotal;
		UnsignedInt m_ownedCacheVersion;
		DWORD m_ownedCacheLastRefreshTick;
		nlohmann::json m_ownedCacheUnits;
		nlohmann::json m_ownedCacheBuildings;
		nlohmann::json m_ownedCacheIdleWorkers;

		#include "AIControlAdapterTransport.inl"

		#include "AIControlAdapterProtocol.inl"

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

		#include "AIControlAdapterGameActions.inl"

		#include "AIControlAdapterGameQuery.inl"

		#include "AIControlAdapterUI.inl"
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



