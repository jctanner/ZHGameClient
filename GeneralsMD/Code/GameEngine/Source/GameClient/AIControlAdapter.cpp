#include "PreRTS.h"

#include "GameClient/AIControlAdapter.h"

#include "Common/NameKeyGenerator.h"
#include "Common/ActionManager.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
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
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
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
#include <map>
#include <set>
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

	struct WorkerAutomationRule
	{
		bool enabled;
		bool hasExplicitPlayerIndex;
		bool hasExplicitProducerKind;
		Int playerIndex;
		Int minIdleWorkers;
		Int queueCount;
		std::string producerKind;
		DWORD cooldownMs;
		DWORD nextAllowedTick;
	};

	struct AttackAutomationRule
	{
		bool enabled;
		bool hasExplicitPlayerIndex;
		Int playerIndex;
		Int minUnits;
		Int groupSize;
		Real distance;
		DWORD cooldownMs;
		DWORD nextAllowedTick;
	};

	struct CaptureAutomationRule
	{
		bool enabled;
		bool hasExplicitPlayerIndex;
		bool preferIdle;
		Int playerIndex;
		Int maxConcurrent;
		DWORD cooldownMs;
		DWORD nextAllowedTick;
		std::unordered_map<Int, DWORD> pendingTargetsUntilTick;
		std::unordered_map<Int, DWORD> pendingSourcesUntilTick;
	};

	struct RadarVanAutomationRule
	{
		bool enabled;
		bool hasExplicitPlayerIndex;
		Int playerIndex;
		Int minCount;
		DWORD cooldownMs;
		DWORD nextAllowedTick;
	};

	struct StashWorkerAutomationRule
	{
		bool enabled;
		bool hasExplicitPlayerIndex;
		Int playerIndex;
		Int targetWorkersPerStash;
		DWORD cooldownMs;
		DWORD nextAllowedTick;
		std::set<Int> servicedStashIds;
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
			m_workerAutomationRule.enabled = false;
			m_workerAutomationRule.hasExplicitPlayerIndex = false;
			m_workerAutomationRule.hasExplicitProducerKind = false;
			m_workerAutomationRule.playerIndex = -1;
			m_workerAutomationRule.minIdleWorkers = 0;
			m_workerAutomationRule.queueCount = 1;
			m_workerAutomationRule.producerKind.clear();
			m_workerAutomationRule.cooldownMs = 3000u;
			m_workerAutomationRule.nextAllowedTick = 0u;
			m_attackAutomationRule.enabled = false;
			m_attackAutomationRule.hasExplicitPlayerIndex = false;
			m_attackAutomationRule.playerIndex = -1;
			m_attackAutomationRule.minUnits = 40;
			m_attackAutomationRule.groupSize = 30;
			m_attackAutomationRule.distance = 3000.0f;
			m_attackAutomationRule.cooldownMs = 15000u;
			m_attackAutomationRule.nextAllowedTick = 0u;
			m_captureAutomationRule.enabled = false;
			m_captureAutomationRule.hasExplicitPlayerIndex = false;
			m_captureAutomationRule.preferIdle = true;
			m_captureAutomationRule.playerIndex = -1;
			m_captureAutomationRule.maxConcurrent = 3;
			m_captureAutomationRule.cooldownMs = 4000u;
			m_captureAutomationRule.nextAllowedTick = 0u;
			m_captureAutomationRule.pendingTargetsUntilTick.clear();
			m_captureAutomationRule.pendingSourcesUntilTick.clear();
			m_radarVanAutomationRule.enabled = false;
			m_radarVanAutomationRule.hasExplicitPlayerIndex = false;
			m_radarVanAutomationRule.playerIndex = -1;
			m_radarVanAutomationRule.minCount = 1;
			m_radarVanAutomationRule.cooldownMs = 12000u;
			m_radarVanAutomationRule.nextAllowedTick = 0u;
			m_stashWorkerAutomationRule.enabled = false;
			m_stashWorkerAutomationRule.hasExplicitPlayerIndex = false;
			m_stashWorkerAutomationRule.playerIndex = -1;
			m_stashWorkerAutomationRule.targetWorkersPerStash = 9;
			m_stashWorkerAutomationRule.cooldownMs = 4000u;
			m_stashWorkerAutomationRule.nextAllowedTick = 0u;
			m_stashWorkerAutomationRule.servicedStashIds.clear();
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
			m_workerAutomationRule.enabled = false;
			m_workerAutomationRule.hasExplicitPlayerIndex = false;
			m_workerAutomationRule.hasExplicitProducerKind = false;
			m_workerAutomationRule.playerIndex = -1;
			m_workerAutomationRule.minIdleWorkers = 0;
			m_workerAutomationRule.queueCount = 1;
			m_workerAutomationRule.producerKind.clear();
			m_workerAutomationRule.cooldownMs = 3000u;
			m_workerAutomationRule.nextAllowedTick = 0u;
			m_attackAutomationRule.enabled = false;
			m_attackAutomationRule.hasExplicitPlayerIndex = false;
			m_attackAutomationRule.playerIndex = -1;
			m_attackAutomationRule.minUnits = 40;
			m_attackAutomationRule.groupSize = 30;
			m_attackAutomationRule.distance = 3000.0f;
			m_attackAutomationRule.cooldownMs = 15000u;
			m_attackAutomationRule.nextAllowedTick = 0u;
			m_captureAutomationRule.enabled = false;
			m_captureAutomationRule.hasExplicitPlayerIndex = false;
			m_captureAutomationRule.preferIdle = true;
			m_captureAutomationRule.playerIndex = -1;
			m_captureAutomationRule.maxConcurrent = 3;
			m_captureAutomationRule.cooldownMs = 4000u;
			m_captureAutomationRule.nextAllowedTick = 0u;
			m_captureAutomationRule.pendingTargetsUntilTick.clear();
			m_captureAutomationRule.pendingSourcesUntilTick.clear();
			m_radarVanAutomationRule.enabled = false;
			m_radarVanAutomationRule.hasExplicitPlayerIndex = false;
			m_radarVanAutomationRule.playerIndex = -1;
			m_radarVanAutomationRule.minCount = 1;
			m_radarVanAutomationRule.cooldownMs = 12000u;
			m_radarVanAutomationRule.nextAllowedTick = 0u;
			m_stashWorkerAutomationRule.enabled = false;
			m_stashWorkerAutomationRule.hasExplicitPlayerIndex = false;
			m_stashWorkerAutomationRule.playerIndex = -1;
			m_stashWorkerAutomationRule.targetWorkersPerStash = 9;
			m_stashWorkerAutomationRule.cooldownMs = 4000u;
			m_stashWorkerAutomationRule.nextAllowedTick = 0u;
			m_stashWorkerAutomationRule.servicedStashIds.clear();
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
			evaluateAutomationRules();
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
		WorkerAutomationRule m_workerAutomationRule;
		AttackAutomationRule m_attackAutomationRule;
		CaptureAutomationRule m_captureAutomationRule;
		RadarVanAutomationRule m_radarVanAutomationRule;
		StashWorkerAutomationRule m_stashWorkerAutomationRule;

		#include "AIControlAdapterTransport.inl"

		#include "AIControlAdapterProtocol.inl"

		void evaluateAutomationRules()
		{
			evaluateStashWorkerAutomationRule();
			evaluateWorkerAutomationRule();
			evaluateRadarVanAutomationRule();
			evaluateAttackAutomationRule();
			evaluateCaptureAutomationRule();
		}

		void collectCombatUnitsForRaid(Player* player, std::vector<Object*>& outUnits) const
		{
			outUnits.clear();
			if (player == nullptr)
			{
				return;
			}

			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				if (obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_DOZER) || obj->isKindOf(KINDOF_HARVESTER))
				{
					return;
				}
				if (!obj->isKindOf(KINDOF_INFANTRY) && !obj->isKindOf(KINDOF_VEHICLE) && !obj->isKindOf(KINDOF_AIRCRAFT))
				{
					return;
				}
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				if (containsIgnoreCase(name, "radar"))
				{
					return;
				}
				if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					return;
				}
				if (obj->getAI() == nullptr)
				{
					return;
				}

				std::vector<Object*>* units = static_cast<std::vector<Object*>*>(userData);
				units->push_back(obj);
			}, &outUnits);
		}

		void collectCapturableTargetsForPlayer(Player* player, std::vector<Object*>& outTargets) const
		{
			outTargets.clear();
			if (player == nullptr || TheGameLogic == nullptr)
			{
				return;
			}

			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (obj->isEffectivelyDead())
				{
					continue;
				}
				if (!obj->isKindOf(KINDOF_STRUCTURE) || !obj->isKindOf(KINDOF_CAPTURABLE))
				{
					continue;
				}
				if (obj->getControllingPlayer() == player)
				{
					continue;
				}
				outTargets.push_back(obj);
			}
		}

		bool isCaptureSourceTemporarilyReserved(const Object* source) const
		{
			if (source == nullptr)
			{
				return false;
			}
			const Int sourceId = static_cast<Int>(source->getID());
			if (sourceId <= 0)
			{
				return false;
			}
			const auto it = m_captureAutomationRule.pendingSourcesUntilTick.find(sourceId);
			if (it == m_captureAutomationRule.pendingSourcesUntilTick.end())
			{
				return false;
			}
			const DWORD now = ::GetTickCount();
			return static_cast<LONG>(it->second - now) > 0;
		}

		void collectCaptureSourcesForPlayer(Player* player, bool preferIdle, std::vector<Object*>& outSources) const
		{
			outSources.clear();
			if (player == nullptr || TheActionManager == nullptr)
			{
				return;
			}

			struct CaptureSourceSearchContext
			{
				const AIControlAdapterState* self;
				bool preferIdle;
				std::vector<Object*>* outSources;
			} ctx = { this, preferIdle, &outSources };

			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				CaptureSourceSearchContext* ctx = static_cast<CaptureSourceSearchContext*>(userData);
				if (ctx->self == nullptr || ctx->outSources == nullptr)
				{
					return;
				}
				if (!obj->hasSpecialPower(SPECIAL_INFANTRY_CAPTURE_BUILDING) && !obj->hasSpecialPower(SPECIAL_BLACKLOTUS_CAPTURE_BUILDING))
				{
					return;
				}
				if (ctx->self->isCaptureSourceTemporarilyReserved(obj))
				{
					return;
				}
				if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					return;
				}

				AIUpdateInterface* ai = obj->getAI();
				const bool isIdle = (ai != nullptr && ai->isIdle() && !ai->isBusy());
				if (ctx->preferIdle && !isIdle)
				{
					return;
				}

				ctx->outSources->push_back(obj);
			}, &ctx);

			if (!outSources.empty() || !preferIdle)
			{
				return;
			}

			ctx.preferIdle = false;
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				CaptureSourceSearchContext* ctx = static_cast<CaptureSourceSearchContext*>(userData);
				if (ctx->self == nullptr || ctx->outSources == nullptr)
				{
					return;
				}
				if (!obj->hasSpecialPower(SPECIAL_INFANTRY_CAPTURE_BUILDING) && !obj->hasSpecialPower(SPECIAL_BLACKLOTUS_CAPTURE_BUILDING))
				{
					return;
				}
				if (ctx->self->isCaptureSourceTemporarilyReserved(obj))
				{
					return;
				}
				if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					return;
				}
				ctx->outSources->push_back(obj);
			}, &ctx);
		}

		void pruneCaptureAutomationPendingTargets()
		{
			if (m_captureAutomationRule.pendingTargetsUntilTick.empty())
			{
				// fall through and still prune pending sources
			}
			const DWORD now = ::GetTickCount();
			for (auto it = m_captureAutomationRule.pendingTargetsUntilTick.begin(); it != m_captureAutomationRule.pendingTargetsUntilTick.end(); )
			{
				if (static_cast<LONG>(it->second - now) <= 0)
				{
					it = m_captureAutomationRule.pendingTargetsUntilTick.erase(it);
				}
				else
				{
					++it;
				}
			}
			for (auto it = m_captureAutomationRule.pendingSourcesUntilTick.begin(); it != m_captureAutomationRule.pendingSourcesUntilTick.end(); )
			{
				if (static_cast<LONG>(it->second - now) <= 0)
				{
					it = m_captureAutomationRule.pendingSourcesUntilTick.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		void evaluateWorkerAutomationRule()
		{
			if (!m_workerAutomationRule.enabled)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			if (static_cast<LONG>(m_workerAutomationRule.nextAllowedTick - now) > 0)
			{
				return;
			}

			Player* player = nullptr;
			if (m_workerAutomationRule.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_workerAutomationRule.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}

			if (player == nullptr)
			{
				m_workerAutomationRule.nextAllowedTick = now + m_workerAutomationRule.cooldownMs;
				adapterLog("automation_worker_rule_skip reason=player_not_found");
				return;
			}

			refreshOwnedObjectCache(player);
			const Int idleWorkers = m_ownedCacheIdleWorkersTotal;
			if (idleWorkers >= m_workerAutomationRule.minIdleWorkers)
			{
				return;
			}

			nlohmann::json args = nlohmann::json::object();
			args["count"] = m_workerAutomationRule.queueCount;
			if (m_workerAutomationRule.hasExplicitProducerKind && !m_workerAutomationRule.producerKind.empty())
			{
				args["producer_kind"] = m_workerAutomationRule.producerKind;
			}
			if (m_workerAutomationRule.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_workerAutomationRule.playerIndex;
			}

			char requestIdBuffer[64];
			sprintf_s(
				requestIdBuffer,
				"auto_worker_%08X_%08X",
				static_cast<unsigned int>(player->getPlayerIndex()),
				static_cast<unsigned int>(now));

			nlohmann::json message = {
				{"type", "SessionCommand"},
				{"request_id", std::string(requestIdBuffer)},
				{"cmd", "Game.BuildWorker"},
				{"args", args}
			};

			std::string reason;
			const bool ok = executeGameBuildWorker(message, reason);
			m_workerAutomationRule.nextAllowedTick = now + m_workerAutomationRule.cooldownMs;
			adapterLog(
				"automation_worker_rule request_id=%s player=%d idle_workers=%d min_idle_workers=%d queue_count=%d producer_kind=%s ok=%d reason=%s",
				requestIdBuffer,
				player->getPlayerIndex(),
				idleWorkers,
				m_workerAutomationRule.minIdleWorkers,
				m_workerAutomationRule.queueCount,
				m_workerAutomationRule.hasExplicitProducerKind ? m_workerAutomationRule.producerKind.c_str() : "default",
				ok ? 1 : 0,
				ok ? "" : reason.c_str());

			if (ok)
			{
				m_ownedCacheValid = false;
			}
		}

		void evaluateStashWorkerAutomationRule()
		{
			if (!m_stashWorkerAutomationRule.enabled)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			if (static_cast<LONG>(m_stashWorkerAutomationRule.nextAllowedTick - now) > 0)
			{
				return;
			}

			Player* player = nullptr;
			if (m_stashWorkerAutomationRule.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_stashWorkerAutomationRule.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}
			if (player == nullptr)
			{
				m_stashWorkerAutomationRule.nextAllowedTick = now + m_stashWorkerAutomationRule.cooldownMs;
				adapterLog("automation_stash_worker_rule_skip reason=player_not_found");
				return;
			}

			std::vector<Object*> stashes;
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}

				std::vector<Object*>* stashes = static_cast<std::vector<Object*>*>(userData);
				if (stashes == nullptr)
				{
					return;
				}

				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				if (!obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION)
					&& (containsIgnoreCase(name, "supplystash") || containsIgnoreCase(name, "supplycenter")))
				{
					stashes->push_back(obj);
				}
			}, &stashes);

			if (stashes.empty())
			{
				m_stashWorkerAutomationRule.nextAllowedTick = now + m_stashWorkerAutomationRule.cooldownMs;
				adapterLog("automation_stash_worker_rule_skip player=%d reason=no_stashes", player->getPlayerIndex());
				return;
			}

			Object* targetStash = nullptr;
			for (Object* stash : stashes)
			{
				if (stash == nullptr)
				{
					continue;
				}
				const Int stashId = static_cast<Int>(stash->getID());
				if (m_stashWorkerAutomationRule.servicedStashIds.find(stashId) != m_stashWorkerAutomationRule.servicedStashIds.end())
				{
					continue;
				}
				targetStash = stash;
				break;
			}

			if (targetStash == nullptr)
			{
				return;
			}

			char requestIdBuffer[64];
			sprintf_s(
				requestIdBuffer,
				"auto_stash_worker_%08X_%08X",
				static_cast<unsigned int>(player->getPlayerIndex()),
				static_cast<unsigned int>(now));

			nlohmann::json args = {
				{"count", m_stashWorkerAutomationRule.targetWorkersPerStash},
				{"producer_kind", "supply_stash"},
				{"producer_object_id", static_cast<Int>(targetStash->getID())}
			};
			if (m_stashWorkerAutomationRule.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_stashWorkerAutomationRule.playerIndex;
			}

			nlohmann::json message = {
				{"type", "SessionCommand"},
				{"request_id", std::string(requestIdBuffer)},
				{"cmd", "Game.BuildWorker"},
				{"args", args}
			};

			std::string reason;
			const bool ok = executeGameBuildWorker(message, reason);
			m_stashWorkerAutomationRule.nextAllowedTick = now + m_stashWorkerAutomationRule.cooldownMs;
			if (ok)
			{
				m_stashWorkerAutomationRule.servicedStashIds.insert(static_cast<Int>(targetStash->getID()));
			}
			adapterLog(
				"automation_stash_worker_rule request_id=%s player=%d stash_id=%d target_workers=%d serviced=%d ok=%d reason=%s",
				requestIdBuffer,
				player->getPlayerIndex(),
				static_cast<Int>(targetStash->getID()),
				m_stashWorkerAutomationRule.targetWorkersPerStash,
				ok ? 1 : 0,
				ok ? 1 : 0,
				ok ? "" : reason.c_str());
		}

		void evaluateAttackAutomationRule()
		{
			if (!m_attackAutomationRule.enabled)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			if (static_cast<LONG>(m_attackAutomationRule.nextAllowedTick - now) > 0)
			{
				return;
			}

			Player* player = nullptr;
			if (m_attackAutomationRule.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_attackAutomationRule.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}

			if (player == nullptr)
			{
				m_attackAutomationRule.nextAllowedTick = now + m_attackAutomationRule.cooldownMs;
				adapterLog("automation_attack_rule_skip reason=player_not_found");
				return;
			}

			std::vector<Object*> combatUnits;
			collectCombatUnitsForRaid(player, combatUnits);
			const Int combatUnitCount = static_cast<Int>(combatUnits.size());
			if (combatUnitCount < m_attackAutomationRule.minUnits)
			{
				return;
			}

			nlohmann::json args = nlohmann::json::object({
				{"min_units", m_attackAutomationRule.minUnits},
				{"group_size", m_attackAutomationRule.groupSize},
				{"distance", m_attackAutomationRule.distance}
			});
			if (m_attackAutomationRule.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_attackAutomationRule.playerIndex;
			}

			char requestIdBuffer[64];
			sprintf_s(
				requestIdBuffer,
				"auto_attack_%08X_%08X",
				static_cast<unsigned int>(player->getPlayerIndex()),
				static_cast<unsigned int>(now));

			nlohmann::json message = {
				{"type", "SessionCommand"},
				{"request_id", std::string(requestIdBuffer)},
				{"cmd", "Game.AttackMove.RaidSmart"},
				{"args", args}
			};

			std::string reason;
			const bool ok = executeGameAttackMoveRaidSmart(message, reason);
			m_attackAutomationRule.nextAllowedTick = now + m_attackAutomationRule.cooldownMs;
			adapterLog(
				"automation_attack_rule request_id=%s player=%d combat_units=%d min_units=%d group_size=%d distance=%.1f ok=%d reason=%s",
				requestIdBuffer,
				player->getPlayerIndex(),
				combatUnitCount,
				m_attackAutomationRule.minUnits,
				m_attackAutomationRule.groupSize,
				static_cast<double>(m_attackAutomationRule.distance),
				ok ? 1 : 0,
				ok ? "" : reason.c_str());
		}

		void evaluateRadarVanAutomationRule()
		{
			if (!m_radarVanAutomationRule.enabled)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			if (static_cast<LONG>(m_radarVanAutomationRule.nextAllowedTick - now) > 0)
			{
				return;
			}

			Player* player = nullptr;
			if (m_radarVanAutomationRule.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_radarVanAutomationRule.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}
			if (player == nullptr)
			{
				m_radarVanAutomationRule.nextAllowedTick = now + m_radarVanAutomationRule.cooldownMs;
				adapterLog("automation_radar_van_rule_skip reason=player_not_found");
				return;
			}

			struct RadarVanCountContext
			{
				Int armsCount;
				Int radarVanCount;
			} counts = { 0, 0 };
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				RadarVanCountContext* counts = static_cast<RadarVanCountContext*>(userData);
				if (counts == nullptr)
				{
					return;
				}
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				if (containsIgnoreCase(name, "armsdealer"))
				{
					++counts->armsCount;
				}
				if (containsIgnoreCase(name, "radarvan"))
				{
					++counts->radarVanCount;
				}
			}, &counts);
			const Int armsCount = counts.armsCount;
			const Int radarVanCount = counts.radarVanCount;

			if (armsCount <= 0 || radarVanCount >= m_radarVanAutomationRule.minCount)
			{
				return;
			}

			char requestIdBuffer[64];
			sprintf_s(
				requestIdBuffer,
				"auto_radar_van_%08X_%08X",
				static_cast<unsigned int>(player->getPlayerIndex()),
				static_cast<unsigned int>(now));

			nlohmann::json args = nlohmann::json::object();
			if (m_radarVanAutomationRule.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_radarVanAutomationRule.playerIndex;
			}

			nlohmann::json message = {
				{"type", "SessionCommand"},
				{"request_id", std::string(requestIdBuffer)},
				{"cmd", "Game.QueueRadarVan"},
				{"args", args}
			};

			std::string reason;
			bool ok = executeGameQueueRadarVan(message, reason);
			if (!ok)
			{
				nlohmann::json fallbackMessage = {
					{"type", "SessionCommand"},
					{"request_id", std::string(requestIdBuffer)},
					{"cmd", "Game.QueueRadarVansAllWarFactories"},
					{"args", nlohmann::json::object({{"count", 1}})}
				};
				if (m_radarVanAutomationRule.hasExplicitPlayerIndex)
				{
					fallbackMessage["args"]["player_index"] = m_radarVanAutomationRule.playerIndex;
				}
				ok = executeGameQueueRadarVansAllWarFactories(fallbackMessage, reason);
			}

			m_radarVanAutomationRule.nextAllowedTick = now + m_radarVanAutomationRule.cooldownMs;
			adapterLog(
				"automation_radar_van_rule request_id=%s player=%d arms=%d radar_vans=%d min_count=%d ok=%d reason=%s",
				requestIdBuffer,
				player->getPlayerIndex(),
				armsCount,
				radarVanCount,
				m_radarVanAutomationRule.minCount,
				ok ? 1 : 0,
				ok ? "" : reason.c_str());
		}

		void evaluateCaptureAutomationRule()
		{
			if (!m_captureAutomationRule.enabled)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			pruneCaptureAutomationPendingTargets();
			if (static_cast<LONG>(m_captureAutomationRule.nextAllowedTick - now) > 0)
			{
				return;
			}

			Player* player = nullptr;
			if (m_captureAutomationRule.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_captureAutomationRule.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}

			if (player == nullptr)
			{
				m_captureAutomationRule.nextAllowedTick = now + m_captureAutomationRule.cooldownMs;
				adapterLog("automation_capture_rule_skip reason=player_not_found");
				return;
			}

			const Int pendingCount = static_cast<Int>(m_captureAutomationRule.pendingTargetsUntilTick.size());
			if (pendingCount >= m_captureAutomationRule.maxConcurrent)
			{
				return;
			}

			std::vector<Object*> targets;
			collectCapturableTargetsForPlayer(player, targets);
			if (targets.empty())
			{
				return;
			}

			std::vector<Object*> sources;
			collectCaptureSourcesForPlayer(player, m_captureAutomationRule.preferIdle, sources);
			if (sources.empty())
			{
				m_captureAutomationRule.nextAllowedTick = now + m_captureAutomationRule.cooldownMs;
				adapterLog(
					"automation_capture_rule_skip player=%d reason=no_capture_sources pending=%d max_concurrent=%d prefer_idle=%d",
					player->getPlayerIndex(),
					pendingCount,
					m_captureAutomationRule.maxConcurrent,
					m_captureAutomationRule.preferIdle ? 1 : 0);
				return;
			}

			Coord3D anchor;
			anchor.x = 0.0f;
			anchor.y = 0.0f;
			anchor.z = 0.0f;
			bool hasAnchor = false;
			Object* cc = findPrimaryCommandCenter(player);
			if (cc != nullptr && cc->getPosition() != nullptr)
			{
				anchor = *cc->getPosition();
				hasAnchor = true;
			}
			if (!hasAnchor)
			{
				nlohmann::json playerPos = buildPlayerMapPositionSummary(player);
				const auto pxIt = playerPos.find("x");
				const auto pyIt = playerPos.find("y");
				if (pxIt != playerPos.end() && pyIt != playerPos.end() && pxIt->is_number() && pyIt->is_number())
				{
					anchor.x = pxIt->get<Real>();
					anchor.y = pyIt->get<Real>();
					anchor.z = 0.0f;
					hasAnchor = true;
				}
			}

			std::sort(targets.begin(), targets.end(), [&](const Object* lhs, const Object* rhs) -> bool
			{
				const Coord3D* lp = lhs != nullptr ? lhs->getPosition() : nullptr;
				const Coord3D* rp = rhs != nullptr ? rhs->getPosition() : nullptr;
				if (!hasAnchor || lp == nullptr || rp == nullptr)
				{
					const Int lid = lhs != nullptr ? static_cast<Int>(lhs->getID()) : 0;
					const Int rid = rhs != nullptr ? static_cast<Int>(rhs->getID()) : 0;
					return lid < rid;
				}
				const Real ldx = lp->x - anchor.x;
				const Real ldy = lp->y - anchor.y;
				const Real rdx = rp->x - anchor.x;
				const Real rdy = rp->y - anchor.y;
				return ((ldx * ldx) + (ldy * ldy)) < ((rdx * rdx) + (rdy * rdy));
			});

			Int assignmentsRemaining = std::max<Int>(0, m_captureAutomationRule.maxConcurrent - pendingCount);
			Int sentOk = 0;
			Int attemptsRemaining = std::max<Int>(assignmentsRemaining, 1) * 4;
			std::string lastReason;
			for (Object* target : targets)
			{
				if (assignmentsRemaining <= 0 || attemptsRemaining <= 0 || target == nullptr)
				{
					break;
				}
				const Int targetId = static_cast<Int>(target->getID());
				if (targetId <= 0)
				{
					continue;
				}
				if (m_captureAutomationRule.pendingTargetsUntilTick.find(targetId) != m_captureAutomationRule.pendingTargetsUntilTick.end())
				{
					continue;
				}

				Object* selectedSource = nullptr;
				for (Object* candidate : sources)
				{
					if (candidate == nullptr || candidate->isEffectivelyDead())
					{
						continue;
					}
					if (!TheActionManager->canCaptureBuilding(candidate, target, CMD_FROM_PLAYER))
					{
						continue;
					}
					selectedSource = candidate;
					break;
				}
				if (selectedSource == nullptr)
				{
					continue;
				}

				char requestIdBuffer[64];
				sprintf_s(
					requestIdBuffer,
					"auto_capture_%08X_%08X",
					static_cast<unsigned int>(targetId),
					static_cast<unsigned int>(now));

				nlohmann::json args = nlohmann::json::object({
					{"target_object_id", targetId},
					{"source_object_id", static_cast<Int>(selectedSource->getID())}
				});
				if (m_captureAutomationRule.hasExplicitPlayerIndex)
				{
					args["player_index"] = m_captureAutomationRule.playerIndex;
				}

				nlohmann::json message = {
					{"type", "SessionCommand"},
					{"request_id", std::string(requestIdBuffer)},
					{"cmd", "Game.CaptureBuilding"},
					{"args", args}
				};

				std::string reason;
				--attemptsRemaining;
				const bool ok = executeGameCaptureBuilding(message, reason);
				lastReason = reason;
				adapterLog(
					"automation_capture_rule request_id=%s player=%d source_id=%d target_id=%d pending=%d max_concurrent=%d ok=%d reason=%s",
					requestIdBuffer,
					player->getPlayerIndex(),
					static_cast<Int>(selectedSource->getID()),
					targetId,
					pendingCount + sentOk,
					m_captureAutomationRule.maxConcurrent,
					ok ? 1 : 0,
					ok ? "" : reason.c_str());
				if (!ok)
				{
					continue;
				}

				++sentOk;
				--assignmentsRemaining;
				const DWORD pendingUntil = now + std::max<DWORD>(m_captureAutomationRule.cooldownMs, 20000u);
				const Int sourceId = static_cast<Int>(selectedSource->getID());
				m_captureAutomationRule.pendingTargetsUntilTick[targetId] = pendingUntil;
				if (sourceId > 0)
				{
					m_captureAutomationRule.pendingSourcesUntilTick[sourceId] = pendingUntil;
				}
				sources.erase(std::remove(sources.begin(), sources.end(), selectedSource), sources.end());
			}

			if (sentOk > 0)
			{
				m_captureAutomationRule.nextAllowedTick = now + m_captureAutomationRule.cooldownMs;
			}
			else if (!lastReason.empty())
			{
				m_captureAutomationRule.nextAllowedTick = now + std::max<DWORD>(m_captureAutomationRule.cooldownMs, 2000u);
				adapterLog(
					"automation_capture_rule_skip player=%d reason=%s pending=%d max_concurrent=%d",
					player->getPlayerIndex(),
					lastReason.c_str(),
					pendingCount,
					m_captureAutomationRule.maxConcurrent);
			}
		}

		bool configureWorkerAutomationRule(const nlohmann::json& message, std::string& reason)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			WorkerAutomationRule rule;
			rule.enabled = true;
			rule.hasExplicitPlayerIndex = false;
			rule.hasExplicitProducerKind = false;
			rule.playerIndex = -1;
			rule.minIdleWorkers = 0;
			rule.queueCount = 1;
			rule.producerKind.clear();
			rule.cooldownMs = 3000u;
			rule.nextAllowedTick = 0u;

			const auto enabledIt = argsIt->find("enabled");
			if (enabledIt != argsIt->end())
			{
				if (!enabledIt->is_boolean())
				{
					reason = "invalid_enabled";
					return false;
				}
				rule.enabled = enabledIt->get<bool>();
			}

			const auto playerIndexIt = argsIt->find("player_index");
			if (playerIndexIt != argsIt->end())
			{
				if (!playerIndexIt->is_number_integer())
				{
					reason = "invalid_player_index";
					return false;
				}
				rule.hasExplicitPlayerIndex = true;
				rule.playerIndex = playerIndexIt->get<Int>();
			}

			const auto minIdleIt = argsIt->find("min_idle_workers");
			if (minIdleIt == argsIt->end() || !minIdleIt->is_number_integer())
			{
				reason = "missing_min_idle_workers";
				return false;
			}
			rule.minIdleWorkers = minIdleIt->get<Int>();
			if (rule.minIdleWorkers < 0)
			{
				reason = "invalid_min_idle_workers";
				return false;
			}

			const auto queueCountIt = argsIt->find("queue_count");
			if (queueCountIt != argsIt->end())
			{
				if (!queueCountIt->is_number_integer())
				{
					reason = "invalid_queue_count";
					return false;
				}
				rule.queueCount = queueCountIt->get<Int>();
			}
			if (rule.queueCount <= 0)
			{
				reason = "invalid_queue_count";
				return false;
			}

			const auto producerKindIt = argsIt->find("producer_kind");
			if (producerKindIt != argsIt->end())
			{
				if (!producerKindIt->is_string())
				{
					reason = "invalid_producer_kind";
					return false;
				}
				rule.hasExplicitProducerKind = true;
				rule.producerKind = producerKindIt->get<std::string>();
			}

			const auto cooldownIt = argsIt->find("cooldown_ms");
			if (cooldownIt != argsIt->end())
			{
				if (!cooldownIt->is_number_integer())
				{
					reason = "invalid_cooldown_ms";
					return false;
				}
				const Int cooldownValue = cooldownIt->get<Int>();
				if (cooldownValue < 0)
				{
					reason = "invalid_cooldown_ms";
					return false;
				}
				rule.cooldownMs = static_cast<DWORD>(cooldownValue);
			}

			m_workerAutomationRule = rule;
			adapterLog(
				"automation_worker_rule_configured enabled=%d player=%d explicit_player=%d min_idle_workers=%d queue_count=%d producer_kind=%s cooldown_ms=%lu",
				m_workerAutomationRule.enabled ? 1 : 0,
				m_workerAutomationRule.playerIndex,
				m_workerAutomationRule.hasExplicitPlayerIndex ? 1 : 0,
				m_workerAutomationRule.minIdleWorkers,
				m_workerAutomationRule.queueCount,
				m_workerAutomationRule.hasExplicitProducerKind ? m_workerAutomationRule.producerKind.c_str() : "default",
				static_cast<unsigned long>(m_workerAutomationRule.cooldownMs));
			return true;
		}

		void clearWorkerAutomationRule()
		{
			m_workerAutomationRule.enabled = false;
			m_workerAutomationRule.hasExplicitPlayerIndex = false;
			m_workerAutomationRule.hasExplicitProducerKind = false;
			m_workerAutomationRule.playerIndex = -1;
			m_workerAutomationRule.minIdleWorkers = 0;
			m_workerAutomationRule.queueCount = 1;
			m_workerAutomationRule.producerKind.clear();
			m_workerAutomationRule.cooldownMs = 3000u;
			m_workerAutomationRule.nextAllowedTick = 0u;
			adapterLog("automation_worker_rule_cleared");
		}

		bool configureAttackAutomationRule(const nlohmann::json& message, std::string& reason)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			AttackAutomationRule rule;
			rule.enabled = true;
			rule.hasExplicitPlayerIndex = false;
			rule.playerIndex = -1;
			rule.minUnits = 40;
			rule.groupSize = 30;
			rule.distance = 3000.0f;
			rule.cooldownMs = 15000u;
			rule.nextAllowedTick = 0u;

			const auto enabledIt = argsIt->find("enabled");
			if (enabledIt != argsIt->end())
			{
				if (!enabledIt->is_boolean())
				{
					reason = "invalid_enabled";
					return false;
				}
				rule.enabled = enabledIt->get<bool>();
			}

			const auto playerIndexIt = argsIt->find("player_index");
			if (playerIndexIt != argsIt->end())
			{
				if (!playerIndexIt->is_number_integer())
				{
					reason = "invalid_player_index";
					return false;
				}
				rule.hasExplicitPlayerIndex = true;
				rule.playerIndex = playerIndexIt->get<Int>();
			}

			const auto minUnitsIt = argsIt->find("min_units");
			if (minUnitsIt == argsIt->end() || !minUnitsIt->is_number_integer())
			{
				reason = "missing_min_units";
				return false;
			}
			rule.minUnits = std::max<Int>(1, minUnitsIt->get<Int>());

			const auto groupSizeIt = argsIt->find("group_size");
			if (groupSizeIt != argsIt->end())
			{
				if (!groupSizeIt->is_number_integer())
				{
					reason = "invalid_group_size";
					return false;
				}
				rule.groupSize = std::max<Int>(1, groupSizeIt->get<Int>());
			}

			const auto distanceIt = argsIt->find("distance");
			if (distanceIt != argsIt->end())
			{
				if (!distanceIt->is_number())
				{
					reason = "invalid_distance";
					return false;
				}
				rule.distance = std::max<Real>(256.0f, distanceIt->get<Real>());
			}

			const auto cooldownIt = argsIt->find("cooldown_ms");
			if (cooldownIt != argsIt->end())
			{
				if (!cooldownIt->is_number_integer())
				{
					reason = "invalid_cooldown_ms";
					return false;
				}
				const Int cooldownValue = cooldownIt->get<Int>();
				if (cooldownValue < 0)
				{
					reason = "invalid_cooldown_ms";
					return false;
				}
				rule.cooldownMs = static_cast<DWORD>(cooldownValue);
			}

			m_attackAutomationRule = rule;
			adapterLog(
				"automation_attack_rule_configured enabled=%d player=%d explicit_player=%d min_units=%d group_size=%d distance=%.1f cooldown_ms=%lu",
				m_attackAutomationRule.enabled ? 1 : 0,
				m_attackAutomationRule.playerIndex,
				m_attackAutomationRule.hasExplicitPlayerIndex ? 1 : 0,
				m_attackAutomationRule.minUnits,
				m_attackAutomationRule.groupSize,
				static_cast<double>(m_attackAutomationRule.distance),
				static_cast<unsigned long>(m_attackAutomationRule.cooldownMs));
			return true;
		}

		void clearAttackAutomationRule()
		{
			m_attackAutomationRule.enabled = false;
			m_attackAutomationRule.hasExplicitPlayerIndex = false;
			m_attackAutomationRule.playerIndex = -1;
			m_attackAutomationRule.minUnits = 40;
			m_attackAutomationRule.groupSize = 30;
			m_attackAutomationRule.distance = 3000.0f;
			m_attackAutomationRule.cooldownMs = 15000u;
			m_attackAutomationRule.nextAllowedTick = 0u;
			adapterLog("automation_attack_rule_cleared");
		}

		bool configureCaptureAutomationRule(const nlohmann::json& message, std::string& reason)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			CaptureAutomationRule rule;
			rule.enabled = true;
			rule.hasExplicitPlayerIndex = false;
			rule.preferIdle = true;
			rule.playerIndex = -1;
			rule.maxConcurrent = 3;
			rule.cooldownMs = 4000u;
			rule.nextAllowedTick = 0u;
			rule.pendingTargetsUntilTick.clear();
			rule.pendingSourcesUntilTick.clear();

			const auto enabledIt = argsIt->find("enabled");
			if (enabledIt != argsIt->end())
			{
				if (!enabledIt->is_boolean())
				{
					reason = "invalid_enabled";
					return false;
				}
				rule.enabled = enabledIt->get<bool>();
			}

			const auto playerIndexIt = argsIt->find("player_index");
			if (playerIndexIt != argsIt->end())
			{
				if (!playerIndexIt->is_number_integer())
				{
					reason = "invalid_player_index";
					return false;
				}
				rule.hasExplicitPlayerIndex = true;
				rule.playerIndex = playerIndexIt->get<Int>();
			}

			const auto maxConcurrentIt = argsIt->find("max_concurrent");
			if (maxConcurrentIt == argsIt->end() || !maxConcurrentIt->is_number_integer())
			{
				reason = "missing_max_concurrent";
				return false;
			}
			rule.maxConcurrent = std::max<Int>(1, maxConcurrentIt->get<Int>());

			const auto preferIdleIt = argsIt->find("prefer_idle");
			if (preferIdleIt != argsIt->end())
			{
				if (!preferIdleIt->is_boolean())
				{
					reason = "invalid_prefer_idle";
					return false;
				}
				rule.preferIdle = preferIdleIt->get<bool>();
			}

			const auto cooldownIt = argsIt->find("cooldown_ms");
			if (cooldownIt != argsIt->end())
			{
				if (!cooldownIt->is_number_integer())
				{
					reason = "invalid_cooldown_ms";
					return false;
				}
				const Int cooldownValue = cooldownIt->get<Int>();
				if (cooldownValue < 0)
				{
					reason = "invalid_cooldown_ms";
					return false;
				}
				rule.cooldownMs = static_cast<DWORD>(cooldownValue);
			}

			m_captureAutomationRule = rule;
			adapterLog(
				"automation_capture_rule_configured enabled=%d player=%d explicit_player=%d max_concurrent=%d prefer_idle=%d cooldown_ms=%lu",
				m_captureAutomationRule.enabled ? 1 : 0,
				m_captureAutomationRule.playerIndex,
				m_captureAutomationRule.hasExplicitPlayerIndex ? 1 : 0,
				m_captureAutomationRule.maxConcurrent,
				m_captureAutomationRule.preferIdle ? 1 : 0,
				static_cast<unsigned long>(m_captureAutomationRule.cooldownMs));
			return true;
		}

		bool configureRadarVanAutomationRule(const nlohmann::json& message, std::string& reason)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			RadarVanAutomationRule rule;
			rule.enabled = true;
			rule.hasExplicitPlayerIndex = false;
			rule.playerIndex = -1;
			rule.minCount = 1;
			rule.cooldownMs = 12000u;
			rule.nextAllowedTick = 0u;

			const auto enabledIt = argsIt->find("enabled");
			if (enabledIt != argsIt->end())
			{
				if (!enabledIt->is_boolean())
				{
					reason = "invalid_enabled";
					return false;
				}
				rule.enabled = enabledIt->get<bool>();
			}
			const auto playerIndexIt = argsIt->find("player_index");
			if (playerIndexIt != argsIt->end())
			{
				if (!playerIndexIt->is_number_integer())
				{
					reason = "invalid_player_index";
					return false;
				}
				rule.hasExplicitPlayerIndex = true;
				rule.playerIndex = playerIndexIt->get<Int>();
			}
			const auto minCountIt = argsIt->find("min_count");
			if (minCountIt == argsIt->end() || !minCountIt->is_number_integer())
			{
				reason = "missing_min_count";
				return false;
			}
			rule.minCount = std::max<Int>(0, minCountIt->get<Int>());
			const auto cooldownIt = argsIt->find("cooldown_ms");
			if (cooldownIt != argsIt->end())
			{
				if (!cooldownIt->is_number_integer())
				{
					reason = "invalid_cooldown_ms";
					return false;
				}
				const Int cooldownValue = cooldownIt->get<Int>();
				if (cooldownValue < 0)
				{
					reason = "invalid_cooldown_ms";
					return false;
				}
				rule.cooldownMs = static_cast<DWORD>(cooldownValue);
			}

			m_radarVanAutomationRule = rule;
			adapterLog(
				"automation_radar_van_rule_configured enabled=%d player=%d explicit_player=%d min_count=%d cooldown_ms=%lu",
				m_radarVanAutomationRule.enabled ? 1 : 0,
				m_radarVanAutomationRule.playerIndex,
				m_radarVanAutomationRule.hasExplicitPlayerIndex ? 1 : 0,
				m_radarVanAutomationRule.minCount,
				static_cast<unsigned long>(m_radarVanAutomationRule.cooldownMs));
			return true;
		}

		bool configureStashWorkerAutomationRule(const nlohmann::json& message, std::string& reason)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			StashWorkerAutomationRule rule;
			rule.enabled = true;
			rule.hasExplicitPlayerIndex = false;
			rule.playerIndex = -1;
			rule.targetWorkersPerStash = 9;
			rule.cooldownMs = 4000u;
			rule.nextAllowedTick = 0u;
			rule.servicedStashIds.clear();

			const auto enabledIt = argsIt->find("enabled");
			if (enabledIt != argsIt->end())
			{
				if (!enabledIt->is_boolean())
				{
					reason = "invalid_enabled";
					return false;
				}
				rule.enabled = enabledIt->get<bool>();
			}

			const auto playerIndexIt = argsIt->find("player_index");
			if (playerIndexIt != argsIt->end())
			{
				if (!playerIndexIt->is_number_integer())
				{
					reason = "invalid_player_index";
					return false;
				}
				rule.hasExplicitPlayerIndex = true;
				rule.playerIndex = playerIndexIt->get<Int>();
			}

			const auto targetIt = argsIt->find("target_workers_per_stash");
			if (targetIt == argsIt->end() || !targetIt->is_number_integer())
			{
				reason = "missing_target_workers_per_stash";
				return false;
			}
			rule.targetWorkersPerStash = targetIt->get<Int>();
			if (rule.targetWorkersPerStash < 0)
			{
				reason = "invalid_target_workers_per_stash";
				return false;
			}

			const auto cooldownIt = argsIt->find("cooldown_ms");
			if (cooldownIt != argsIt->end())
			{
				if (!cooldownIt->is_number_integer())
				{
					reason = "invalid_cooldown_ms";
					return false;
				}
				const Int cooldownValue = cooldownIt->get<Int>();
				if (cooldownValue < 0)
				{
					reason = "invalid_cooldown_ms";
					return false;
				}
				rule.cooldownMs = static_cast<DWORD>(cooldownValue);
			}

			m_stashWorkerAutomationRule = rule;
			adapterLog(
				"automation_stash_worker_rule_configured enabled=%d player=%d explicit_player=%d target_workers_per_stash=%d cooldown_ms=%lu",
				m_stashWorkerAutomationRule.enabled ? 1 : 0,
				m_stashWorkerAutomationRule.playerIndex,
				m_stashWorkerAutomationRule.hasExplicitPlayerIndex ? 1 : 0,
				m_stashWorkerAutomationRule.targetWorkersPerStash,
				static_cast<unsigned long>(m_stashWorkerAutomationRule.cooldownMs));
			return true;
		}

		void clearStashWorkerAutomationRule()
		{
			m_stashWorkerAutomationRule.enabled = false;
			m_stashWorkerAutomationRule.hasExplicitPlayerIndex = false;
			m_stashWorkerAutomationRule.playerIndex = -1;
			m_stashWorkerAutomationRule.targetWorkersPerStash = 9;
			m_stashWorkerAutomationRule.cooldownMs = 4000u;
			m_stashWorkerAutomationRule.nextAllowedTick = 0u;
			m_stashWorkerAutomationRule.servicedStashIds.clear();
			adapterLog("automation_stash_worker_rule_cleared");
		}

		void clearRadarVanAutomationRule()
		{
			m_radarVanAutomationRule.enabled = false;
			m_radarVanAutomationRule.hasExplicitPlayerIndex = false;
			m_radarVanAutomationRule.playerIndex = -1;
			m_radarVanAutomationRule.minCount = 1;
			m_radarVanAutomationRule.cooldownMs = 12000u;
			m_radarVanAutomationRule.nextAllowedTick = 0u;
			adapterLog("automation_radar_van_rule_cleared");
		}

		void clearCaptureAutomationRule()
		{
			m_captureAutomationRule.enabled = false;
			m_captureAutomationRule.hasExplicitPlayerIndex = false;
			m_captureAutomationRule.preferIdle = true;
			m_captureAutomationRule.playerIndex = -1;
			m_captureAutomationRule.maxConcurrent = 3;
			m_captureAutomationRule.cooldownMs = 4000u;
			m_captureAutomationRule.nextAllowedTick = 0u;
			m_captureAutomationRule.pendingTargetsUntilTick.clear();
			m_captureAutomationRule.pendingSourcesUntilTick.clear();
			adapterLog("automation_capture_rule_cleared");
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



