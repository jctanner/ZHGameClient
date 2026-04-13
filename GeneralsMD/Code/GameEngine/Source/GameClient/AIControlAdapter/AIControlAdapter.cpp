#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapter.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

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
	static const char* const kAutonomySciencePlan[] = {
		"SCIENCE_ScudLauncher",
		"SCIENCE_CashBounty1",
		"SCIENCE_CashBounty2",
		"SCIENCE_CashBounty3"
	};

	struct AutonomyUpgradePlanEntry
	{
		const char* producerKind;
		const char* upgradeName;
	};

	static const AutonomyUpgradePlanEntry kAutonomyUpgradePlan[] = {
		{ "any", "Upgrade_GLAScorpionRocket" },
		{ "any", "Upgrade_InfantryCaptureBuilding" },
		{ "black_market", "Upgrade_GLAWorkerShoes" },
		{ "black_market", "Upgrade_GLAAPBullets" },
		{ "palace", "Upgrade_GLAFortifiedStructure" },
		{ "palace", "Upgrade_GLAAnthraxBeta" },
		{ "palace", "Upgrade_GLAToxinShells" },
		{ "palace", "Chem_Upgrade_GLAAnthraxGamma" },
		{ "palace", "Upgrade_GLAArmTheMob" },
		{ "black_market", "Upgrade_GLAAPRockets" },
		{ "black_market", "Upgrade_GLABuggyAmmo" },
		{ "black_market", "Upgrade_GLAJunkRepair" },
		{ "black_market", "Upgrade_GLARadarVanScan" },
		{ "black_market", "Upgrade_GLACamoNetting" }
	};

	static std::string inferAutonomyScudLauncherTemplate(const Player* player)
	{
		if (player == nullptr)
		{
			return std::string();
		}

		auto containsAsciiLower = [](std::string haystack, const char* needle) -> bool
		{
			if (needle == nullptr || *needle == '\0')
			{
				return false;
			}
			std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::string n = needle;
			std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return haystack.find(n) != std::string::npos;
		};

		const std::string side = player->getSide().str();
		const std::string baseSide = player->getBaseSide().str();
		if (containsAsciiLower(side, "chem") || containsAsciiLower(baseSide, "chem"))
		{
			return "Chem_GLAVehicleScudLauncher";
		}
		if (containsAsciiLower(side, "stealth") || containsAsciiLower(baseSide, "stealth") || containsAsciiLower(side, "slth") || containsAsciiLower(baseSide, "slth"))
		{
			return "Slth_GLAVehicleScudLauncher";
		}
		if (containsAsciiLower(side, "demo") || containsAsciiLower(baseSide, "demo"))
		{
			return "Demo_GLAVehicleScudLauncher";
		}
		return "GLAVehicleScudLauncher";
	}

	static Real clampUnitFloat(Real value, Real minimumValue, Real maximumValue)
	{
		return std::max(minimumValue, std::min(maximumValue, value));
	}

	static std::string normalizeAsciiLower(const std::string& value)
	{
		std::string out = value;
		std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

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

	struct AutonomyState
	{
		std::string mode;
		std::string profile;
		bool paused;
		bool hasAttackAutomationEnabledOverride;
		bool attackAutomationEnabled;
		bool captureTech;
		bool allowSuperweapons;
		bool hasExplicitPlayerIndex;
		Int playerIndex;
		bool hasExplicitTargetPlayerIndex;
		Int targetPlayerIndex;
		Real economyBias;
		Real aggressionBias;
		Real defenseBias;
		Real expansionBias;
		Real sprawlMultiplier;
		Real zoneRadius;
		DWORD lastAppliedTick;
		DWORD nextMacroTick;
		DWORD nextProductionTick;
		DWORD nextTechTick;
		DWORD nextGuardTick;
		DWORD nextSupplyBuildTick;
		DWORD nextBarracksBuildTick;
		DWORD nextArmsBuildTick;
		DWORD nextPalaceBuildTick;
		DWORD nextMarketBuildTick;
		DWORD nextTunnelBuildTick;
		DWORD nextStingerBuildTick;
		std::size_t nextZoneIndex;
		bool hasLastZone;
		UnsignedInt lastZoneAnchorId;
		bool lastZoneIsMainBase;
		Real lastZoneCenterX;
		Real lastZoneCenterY;
		std::string lastDecisionCategory;
		std::string lastDecisionCommand;
		std::string lastDecisionReason;
		nlohmann::json telemetryZones;
		nlohmann::json telemetryEvents;
	};

	#include "AIControlAdapterLog.inl"

	#include "AIControlAdapterTransport.inl"

	#include "AIControlAdapterObjectCache.inl"

	#include "AIControlAdapterWorkersState.inl"

	#include "AIControlAdapterAutomation.inl"

	#include "AIControlAdapterAutonomy.inl"

	class AIControlAdapterState
	{
	public:
		AIControlAdapterState() :
			m_transport(&m_log)
		{
			m_transport.setMessageHandler(&AIControlAdapterState::onMessage, this);
			m_log.resetFile(true);
			adapterLog("adapter_start pid=%lu session=%s", static_cast<unsigned long>(::GetCurrentProcessId()), m_log.sessionId().c_str());
		}

		~AIControlAdapterState()
		{
		}

		void reset()
		{
			m_transport.clearLineBuffer();
			m_lastAutoSupplySourceByPlayer.clear();
			m_reservedBuildLocations.clear();
			m_buildExpansionRadiusByKey.clear();
			m_cache.reset();
			m_automation.reset();
			resetAutonomyState();
			m_transport.resetClientConnection();
		}

		void update()
		{
			m_transport.ensurePipeCreated();
			if (m_transport.pipe() == INVALID_HANDLE_VALUE)
			{
				return;
			}

			m_transport.acceptClientIfAvailable();
			if (!m_transport.hasClient())
			{
				return;
			}

			m_transport.readIncomingData();
			evaluateAutomationRules();
		}

	private:
		AdapterLog m_log;
		AdapterTransport m_transport;

		static void onMessage(void* context, const std::string& line)
		{
			static_cast<AIControlAdapterState*>(context)->handleMessage(line);
		}

		void adapterLog(const char* format, ...)
		{
			va_list args;
			va_start(args, format);
			char buf[4096];
			vsnprintf(buf, sizeof(buf), format, args);
			va_end(args);
			m_log.log("%s", buf);
		}

		void sendJsonLine(const nlohmann::json& payload) { m_transport.sendJsonLine(payload); }
		void sendProtocolError(const std::string& requestId, const char* code, const char* reason) { m_transport.sendProtocolError(requestId, code, reason); }
		void sendActionAck(const std::string& requestId, bool ok, const char* code = nullptr, const char* reason = nullptr) { m_transport.sendActionAck(requestId, ok, code, reason); }
		void sendQueryResult(const std::string& requestId, const nlohmann::json& result) { m_transport.sendQueryResult(requestId, result); }
		void sendQueryError(const std::string& requestId, const char* code, const char* reason) { m_transport.sendQueryError(requestId, code, reason); }
		std::unordered_map<Int, Int> m_lastAutoSupplySourceByPlayer;
		AdapterWorkersState m_workers;
		std::vector<PendingBuildLocationReservation> m_reservedBuildLocations;
		std::unordered_map<std::string, Real> m_buildExpansionRadiusByKey;
		AdapterObjectCache m_cache;
		AdapterAutomationState m_automation;
		AdapterAutonomyState m_autonomy;

		void resetAutonomyState()
		{
			m_autonomy.state.mode = "manual";
			m_autonomy.state.profile = "sprawl_balanced";
			m_autonomy.state.paused = false;
			m_autonomy.state.hasAttackAutomationEnabledOverride = false;
			m_autonomy.state.attackAutomationEnabled = true;
			m_autonomy.state.captureTech = true;
			m_autonomy.state.allowSuperweapons = false;
			m_autonomy.state.hasExplicitPlayerIndex = false;
			m_autonomy.state.playerIndex = -1;
			m_autonomy.state.hasExplicitTargetPlayerIndex = false;
			m_autonomy.state.targetPlayerIndex = -1;
			m_autonomy.state.economyBias = 0.65f;
			m_autonomy.state.aggressionBias = 0.35f;
			m_autonomy.state.defenseBias = 0.45f;
			m_autonomy.state.expansionBias = 0.55f;
			m_autonomy.state.sprawlMultiplier = 1.5f;
			m_autonomy.state.zoneRadius = 450.0f;
			m_autonomy.state.lastAppliedTick = 0u;
			m_autonomy.state.nextMacroTick = 0u;
			m_autonomy.state.nextProductionTick = 0u;
			m_autonomy.state.nextTechTick = 0u;
			m_autonomy.state.nextGuardTick = 0u;
			m_autonomy.state.nextSupplyBuildTick = 0u;
			m_autonomy.state.nextBarracksBuildTick = 0u;
			m_autonomy.state.nextArmsBuildTick = 0u;
			m_autonomy.state.nextPalaceBuildTick = 0u;
			m_autonomy.state.nextMarketBuildTick = 0u;
			m_autonomy.state.nextTunnelBuildTick = 0u;
			m_autonomy.state.nextStingerBuildTick = 0u;
			m_autonomy.state.nextZoneIndex = 0u;
			m_autonomy.state.hasLastZone = false;
			m_autonomy.state.lastZoneAnchorId = 0u;
			m_autonomy.state.lastZoneIsMainBase = false;
			m_autonomy.state.lastZoneCenterX = 0.0f;
			m_autonomy.state.lastZoneCenterY = 0.0f;
			m_autonomy.state.lastDecisionCategory.clear();
			m_autonomy.state.lastDecisionCommand.clear();
			m_autonomy.state.lastDecisionReason.clear();
			m_autonomy.state.telemetryZones = nlohmann::json::array();
			m_autonomy.state.telemetryEvents = nlohmann::json::array();
		}

		bool isAutonomyModeActive() const
		{
			if (m_autonomy.state.paused)
			{
				return false;
			}
			return m_autonomy.state.mode == "autonomous" || m_autonomy.state.mode == "hybrid";
		}

		Player* resolveAutonomyPlayer() const
		{
			if (m_autonomy.state.hasExplicitPlayerIndex)
			{
				return getPlayerByIndex(m_autonomy.state.playerIndex);
			}
			return ThePlayerList != nullptr ? ThePlayerList->getLocalPlayer() : nullptr;
		}

		void clearAutonomyManagedRules()
		{
			clearWorkerAutomationRule();
			clearStashWorkerAutomationRule();
			clearAttackAutomationRule();
			clearCaptureAutomationRule();
			clearRadarVanAutomationRule();
		}

		void recordAutonomyTelemetryEvent(
			const char* kind,
			const std::string& label,
			const std::string& reason,
			const Coord3D* position = nullptr)
		{
			if (kind == nullptr || kind[0] == '\0')
			{
				return;
			}

			nlohmann::json event = nlohmann::json::object({
				{"kind", kind},
				{"label", label},
				{"reason", reason},
				{"tick", static_cast<UnsignedInt>(::GetTickCount())}
			});
			if (position != nullptr)
			{
				event["x"] = position->x;
				event["y"] = position->y;
			}

			if (!m_autonomy.state.telemetryEvents.is_array())
			{
				m_autonomy.state.telemetryEvents = nlohmann::json::array();
			}
			m_autonomy.state.telemetryEvents.push_back(event);
			while (m_autonomy.state.telemetryEvents.size() > 12u)
			{
				m_autonomy.state.telemetryEvents.erase(m_autonomy.state.telemetryEvents.begin());
			}
		}

		void applyAutonomyRules()
		{
			clearAutonomyManagedRules();
			if (!isAutonomyModeActive())
			{
				m_autonomy.state.lastAppliedTick = ::GetTickCount();
				adapterLog("autonomy_rules_inactive mode=%s paused=%d", m_autonomy.state.mode.c_str(), m_autonomy.state.paused ? 1 : 0);
				return;
			}

			const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
			const bool isBalancedSprawl = (profile == "sprawl_balanced");
			const bool isSprawlStyle = (profile == "sprawl" || isBalancedSprawl);
			const Real econ = clampUnitFloat(m_autonomy.state.economyBias, 0.0f, 1.0f);
			const Real aggro = clampUnitFloat(m_autonomy.state.aggressionBias, 0.0f, 1.0f);
			const Real defense = clampUnitFloat(m_autonomy.state.defenseBias, 0.0f, 1.0f);
			const Real expansion = clampUnitFloat(m_autonomy.state.expansionBias, 0.0f, 1.0f);

			m_automation.workerRule.enabled = true;
			m_automation.workerRule.hasExplicitPlayerIndex = m_autonomy.state.hasExplicitPlayerIndex;
			m_automation.workerRule.playerIndex = m_autonomy.state.playerIndex;
			m_automation.workerRule.hasExplicitProducerKind = true;
			m_automation.workerRule.producerKind = "command_center";
			m_automation.workerRule.minIdleWorkers = (econ >= 0.70f || profile == "economic" || isSprawlStyle) ? 2 : 1;
			m_automation.workerRule.queueCount = (econ >= 0.75f || profile == "economic" || isSprawlStyle) ? 2 : 1;
			m_automation.workerRule.cooldownMs = (profile == "aggressive") ? 1500u : 2500u;
			m_automation.workerRule.nextAllowedTick = 0u;
			if (profile == "sprawl")
			{
				m_automation.workerRule.minIdleWorkers = 3;
				m_automation.workerRule.queueCount = 1;
				m_automation.workerRule.cooldownMs = 2000u;
			}
			else if (isBalancedSprawl)
			{
				m_automation.workerRule.minIdleWorkers = 2;
				m_automation.workerRule.queueCount = 1;
				m_automation.workerRule.cooldownMs = 2500u;
			}

			m_automation.stashWorkerRule.enabled = profile != "builtin_passthrough";
			m_automation.stashWorkerRule.hasExplicitPlayerIndex = m_autonomy.state.hasExplicitPlayerIndex;
			m_automation.stashWorkerRule.playerIndex = m_autonomy.state.playerIndex;
			m_automation.stashWorkerRule.targetWorkersPerStash = 8;
			if (profile == "economic" || isSprawlStyle || econ >= 0.70f)
			{
				m_automation.stashWorkerRule.targetWorkersPerStash = 10;
			}
			else if (profile == "aggressive")
			{
				m_automation.stashWorkerRule.targetWorkersPerStash = 7;
			}
			m_automation.stashWorkerRule.cooldownMs = 5000u;
			m_automation.stashWorkerRule.nextAllowedTick = 0u;
			m_automation.stashWorkerRule.servicedStashIds.clear();
			if (profile == "sprawl")
			{
				m_automation.stashWorkerRule.targetWorkersPerStash = 3;
				m_automation.stashWorkerRule.cooldownMs = 12000u;
			}
			else if (isBalancedSprawl)
			{
				m_automation.stashWorkerRule.targetWorkersPerStash = 6;
				m_automation.stashWorkerRule.cooldownMs = 8000u;
			}

			m_automation.radarVanRule.enabled = (profile != "defensive" && profile != "builtin_passthrough");
			m_automation.radarVanRule.hasExplicitPlayerIndex = m_autonomy.state.hasExplicitPlayerIndex;
			m_automation.radarVanRule.playerIndex = m_autonomy.state.playerIndex;
			m_automation.radarVanRule.minCount = (profile == "tech") ? 2 : 1;
			m_automation.radarVanRule.cooldownMs = 12000u;
			m_automation.radarVanRule.nextAllowedTick = 0u;

			m_automation.attackRule.enabled = profile != "builtin_passthrough";
			if (m_autonomy.state.hasAttackAutomationEnabledOverride)
			{
				m_automation.attackRule.enabled = m_autonomy.state.attackAutomationEnabled && profile != "builtin_passthrough";
			}
			m_automation.attackRule.hasExplicitPlayerIndex = m_autonomy.state.hasExplicitPlayerIndex;
			m_automation.attackRule.playerIndex = m_autonomy.state.playerIndex;
			m_automation.attackRule.minUnits = 38;
			m_automation.attackRule.groupSize = 28;
			m_automation.attackRule.distance = 3000.0f + (expansion * 400.0f);
			m_automation.attackRule.cooldownMs = 18000u;
			m_automation.attackRule.nextAllowedTick = 0u;
			if (profile == "aggressive" || aggro >= 0.70f)
			{
				m_automation.attackRule.minUnits = 24;
				m_automation.attackRule.groupSize = 20;
				m_automation.attackRule.cooldownMs = 12000u;
			}
			else if (profile == "economic")
			{
				m_automation.attackRule.minUnits = 50;
				m_automation.attackRule.groupSize = 34;
				m_automation.attackRule.cooldownMs = 22000u;
			}
			else if (profile == "defensive" || defense >= 0.70f)
			{
				m_automation.attackRule.minUnits = 60;
				m_automation.attackRule.groupSize = 40;
				m_automation.attackRule.cooldownMs = 26000u;
			}
			else if (profile == "tech")
			{
				m_automation.attackRule.minUnits = 44;
				m_automation.attackRule.groupSize = 30;
				m_automation.attackRule.cooldownMs = 20000u;
			}
			else if (profile == "sprawl")
			{
				m_automation.attackRule.minUnits = 70;
				m_automation.attackRule.groupSize = 45;
				m_automation.attackRule.cooldownMs = 26000u;
			}
			else if (isBalancedSprawl)
			{
				m_automation.attackRule.minUnits = 55;
				m_automation.attackRule.groupSize = 28;
				m_automation.attackRule.cooldownMs = 22000u;
			}

			m_automation.captureRule.enabled = m_autonomy.state.captureTech && profile != "builtin_passthrough";
			m_automation.captureRule.hasExplicitPlayerIndex = m_autonomy.state.hasExplicitPlayerIndex;
			m_automation.captureRule.playerIndex = m_autonomy.state.playerIndex;
			m_automation.captureRule.preferIdle = true;
			m_automation.captureRule.maxConcurrent = (profile == "tech") ? 3 : 2;
			m_automation.captureRule.cooldownMs = 5000u;
			m_automation.captureRule.nextAllowedTick = 0u;
			m_automation.captureRule.pendingTargetsUntilTick.clear();
			m_automation.captureRule.pendingSourcesUntilTick.clear();

			m_autonomy.state.lastAppliedTick = ::GetTickCount();
			m_autonomy.state.nextMacroTick = 0u;
			m_autonomy.state.nextProductionTick = 0u;
			m_autonomy.state.nextTechTick = 0u;
			m_autonomy.state.nextGuardTick = 0u;
			adapterLog(
				"autonomy_apply mode=%s profile=%s paused=%d economy_bias=%.2f aggression_bias=%.2f defense_bias=%.2f expansion_bias=%.2f capture_tech=%d target_player=%d",
				m_autonomy.state.mode.c_str(),
				m_autonomy.state.profile.c_str(),
				m_autonomy.state.paused ? 1 : 0,
				static_cast<double>(m_autonomy.state.economyBias),
				static_cast<double>(m_autonomy.state.aggressionBias),
				static_cast<double>(m_autonomy.state.defenseBias),
				static_cast<double>(m_autonomy.state.expansionBias),
				m_autonomy.state.captureTech ? 1 : 0,
				m_autonomy.state.hasExplicitTargetPlayerIndex ? m_autonomy.state.targetPlayerIndex : -1);
		}

		void kickAutonomyEvaluation()
		{
			if (!isAutonomyModeActive())
			{
				return;
			}

			m_autonomy.state.nextMacroTick = 0u;
			m_autonomy.state.nextProductionTick = 0u;
			m_autonomy.state.nextTechTick = 0u;
			m_autonomy.state.nextGuardTick = 0u;
			Player* player = resolveAutonomyPlayer();
			const DWORD now = ::GetTickCount();
			Int playerIndex = -1;
			UnsignedInt money = 0u;
			Int ownedUnits = -1;
			Int ownedBuildings = -1;
			Int idleWorkers = -1;
			if (player != nullptr)
			{
				playerIndex = player->getPlayerIndex();
				const Money* wallet = player->getMoney();
				if (wallet != nullptr)
				{
					money = wallet->countMoney();
				}
				refreshOwnedObjectCache(player);
				ownedUnits = m_cache.unitsTotal;
				ownedBuildings = m_cache.buildingsTotal;
				idleWorkers = m_cache.idleWorkersTotal;
			}
			adapterLog(
				"autonomy_kickoff mode=%s profile=%s player=%d now=%lu timers[macro=%lu prod=%lu tech=%lu guard=%lu] ready[macro=%d prod=%d tech=%d guard=%d] assets[units=%d buildings=%d idle_workers=%d money=%lu]",
				m_autonomy.state.mode.c_str(),
				m_autonomy.state.profile.c_str(),
				playerIndex,
				static_cast<unsigned long>(now),
				static_cast<unsigned long>(m_autonomy.state.nextMacroTick),
				static_cast<unsigned long>(m_autonomy.state.nextProductionTick),
				static_cast<unsigned long>(m_autonomy.state.nextTechTick),
				static_cast<unsigned long>(m_autonomy.state.nextGuardTick),
				AIControlAdapterHasTickElapsed(m_autonomy.state.nextMacroTick, now) ? 1 : 0,
				AIControlAdapterHasTickElapsed(m_autonomy.state.nextProductionTick, now) ? 1 : 0,
				AIControlAdapterHasTickElapsed(m_autonomy.state.nextTechTick, now) ? 1 : 0,
				AIControlAdapterHasTickElapsed(m_autonomy.state.nextGuardTick, now) ? 1 : 0,
				ownedUnits,
				ownedBuildings,
				idleWorkers,
				static_cast<unsigned long>(money));
			evaluateAutomationRules();
		}

		void evaluateAutonomyMacro()
		{
			if (!isAutonomyModeActive())
			{
				return;
			}

			Player* player = resolveAutonomyPlayer();
			if (player == nullptr)
			{
				adapterLog("autonomy_macro_blocked reason=player_not_found");
				return;
			}

			const DWORD now = ::GetTickCount();
			adapterLog(
				"autonomy_macro_enter player=%d now=%lu timers[macro=%lu prod=%lu tech=%lu guard=%lu]",
				player->getPlayerIndex(),
				static_cast<unsigned long>(now),
				static_cast<unsigned long>(m_autonomy.state.nextMacroTick),
				static_cast<unsigned long>(m_autonomy.state.nextProductionTick),
				static_cast<unsigned long>(m_autonomy.state.nextTechTick),
				static_cast<unsigned long>(m_autonomy.state.nextGuardTick));
			UnsignedInt money = 0u;
			const Money* wallet = player->getMoney();
			if (wallet != nullptr)
			{
				money = wallet->countMoney();
			}

			struct AutonomyMacroCounts
			{
				Int barracks;
				Int barracksInProgress;
				Int armsDealers;
				Int armsDealersInProgress;
				Int palaces;
				Int palacesInProgress;
				Int blackMarkets;
				Int blackMarketsInProgress;
				Int tunnels;
				Int tunnelsInProgress;
				Int stingers;
				Int stingersInProgress;
				Int supplyStashes;
				Int supplyStashesInProgress;
				Int mobileUnits;
				Int workers;
				Int radarVans;
				Int soldiers;
				Int rpg;
				Int quads;
				Int scorpions;
				Int scudLaunchers;
			} counts = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

			struct AutonomyZone
			{
				Coord3D center;
				ObjectID anchorId;
				bool isMainBase;
			};
			std::vector<AutonomyZone> zones;

			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				AutonomyMacroCounts* counts = static_cast<AutonomyMacroCounts*>(userData);
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				if (obj->isKindOf(KINDOF_STRUCTURE))
				{
					const bool underConstruction = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
					const bool countAsInProgress = underConstruction && static_cast<Int>(obj->getBuilderID()) > 0;
					auto incrementStructureCount = [&](Int& completeCount, Int& inProgressCount) -> void
					{
						if (countAsInProgress)
						{
							++inProgressCount;
						}
						else
						{
							++completeCount;
						}
					};
					if (containsIgnoreCase(name, "supplystash") || containsIgnoreCase(name, "supplycenter"))
					{
						incrementStructureCount(counts->supplyStashes, counts->supplyStashesInProgress);
					}
					else if (containsIgnoreCase(name, "barracks"))
					{
						incrementStructureCount(counts->barracks, counts->barracksInProgress);
					}
					else if (containsIgnoreCase(name, "armsdealer"))
					{
						incrementStructureCount(counts->armsDealers, counts->armsDealersInProgress);
					}
					else if (isPalaceTemplateName(name))
					{
						incrementStructureCount(counts->palaces, counts->palacesInProgress);
					}
					else if (containsIgnoreCase(name, "black") && containsIgnoreCase(name, "market"))
					{
						incrementStructureCount(counts->blackMarkets, counts->blackMarketsInProgress);
					}
					else if (containsIgnoreCase(name, "tunnelnetwork"))
					{
						incrementStructureCount(counts->tunnels, counts->tunnelsInProgress);
					}
					else if (containsIgnoreCase(name, "stingersite"))
					{
						incrementStructureCount(counts->stingers, counts->stingersInProgress);
					}
					return;
				}

				++counts->mobileUnits;
				if (obj->isKindOf(KINDOF_DOZER))
				{
					++counts->workers;
				}
				if (containsIgnoreCase(name, "radarvan"))
				{
					++counts->radarVans;
				}
				if (containsIgnoreCase(name, "soldier"))
				{
					++counts->soldiers;
				}
				if (containsIgnoreCase(name, "rpg"))
				{
					++counts->rpg;
				}
				if (containsIgnoreCase(name, "quad"))
				{
					++counts->quads;
				}
				if (containsIgnoreCase(name, "scorpion"))
				{
					++counts->scorpions;
				}
				if (containsIgnoreCase(name, "scudlauncher"))
				{
					++counts->scudLaunchers;
				}
			}, &counts);
			adapterLog(
				"autonomy_macro_phase phase=counts player=%d supply=%d barracks=%d arms=%d workers=%d",
				player->getPlayerIndex(),
				counts.supplyStashes + counts.supplyStashesInProgress,
				counts.barracks + counts.barracksInProgress,
				counts.armsDealers + counts.armsDealersInProgress,
				counts.workers);

			auto pushZone = [&](Object* anchor, bool isMainBase) -> void
			{
				if (anchor == nullptr || anchor->getPosition() == nullptr)
				{
					return;
				}
				AutonomyZone zone = {};
				zone.center = *anchor->getPosition();
				zone.anchorId = anchor->getID();
				zone.isMainBase = isMainBase;
				for (std::size_t i = 0; i < zones.size(); ++i)
				{
					const Real dx = zones[i].center.x - zone.center.x;
					const Real dy = zones[i].center.y - zone.center.y;
					if ((dx * dx) + (dy * dy) < (220.0f * 220.0f))
					{
						return;
					}
				}
				zones.push_back(zone);
			};

			if (Object* cc = findPrimaryCommandCenter(player))
			{
				pushZone(cc, true);
			}
			for (Object* obj = TheGameLogic != nullptr ? TheGameLogic->getFirstObject() : nullptr; obj != nullptr; obj = obj->getNextObject())
			{
				if (obj->isEffectivelyDead())
				{
					continue;
				}
				if (obj->getControllingPlayer() != player)
				{
					continue;
				}
				if (!obj->isKindOf(KINDOF_STRUCTURE))
				{
					continue;
				}
				if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					continue;
				}
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				if (!(containsIgnoreCase(name, "supplystash") || containsIgnoreCase(name, "supplycenter")))
				{
					continue;
				}
				pushZone(obj, false);
			}

			AutonomyZone activeZone = {};
			bool hasActiveZone = false;
			Int activeZoneIndex = -1;
			if (!zones.empty())
			{
				m_autonomy.state.nextZoneIndex = zones.empty() ? 0u : (m_autonomy.state.nextZoneIndex % zones.size());
				activeZone = zones[m_autonomy.state.nextZoneIndex];
				hasActiveZone = true;
				activeZoneIndex = static_cast<Int>(m_autonomy.state.nextZoneIndex);
			}
			adapterLog(
				"autonomy_macro_phase phase=zones player=%d zones=%d has_active=%d",
				player->getPlayerIndex(),
				static_cast<int>(zones.size()),
				hasActiveZone ? 1 : 0);
			m_autonomy.state.hasLastZone = hasActiveZone;
			m_autonomy.state.lastZoneAnchorId = hasActiveZone ? static_cast<UnsignedInt>(activeZone.anchorId) : 0u;
			m_autonomy.state.lastZoneIsMainBase = hasActiveZone && activeZone.isMainBase;
			m_autonomy.state.lastZoneCenterX = hasActiveZone ? activeZone.center.x : 0.0f;
			m_autonomy.state.lastZoneCenterY = hasActiveZone ? activeZone.center.y : 0.0f;
			Int mainZoneIndex = -1;
			Real sprawlAxisDx = 0.0f;
			Real sprawlAxisDy = 0.0f;
			for (std::size_t i = 0; i < zones.size(); ++i)
			{
				if (zones[i].isMainBase)
				{
					mainZoneIndex = static_cast<Int>(i);
					break;
				}
			}
			if (mainZoneIndex >= 0)
			{
				Real furthestDistSq = -1.0f;
				for (std::size_t i = 0; i < zones.size(); ++i)
				{
					if (static_cast<Int>(i) == mainZoneIndex)
					{
						continue;
					}
					const Real dx = zones[i].center.x - zones[static_cast<std::size_t>(mainZoneIndex)].center.x;
					const Real dy = zones[i].center.y - zones[static_cast<std::size_t>(mainZoneIndex)].center.y;
					const Real distSq = (dx * dx) + (dy * dy);
					if (distSq > furthestDistSq)
					{
						furthestDistSq = distSq;
						sprawlAxisDx = dx;
						sprawlAxisDy = dy;
					}
				}
			}
			const Real sprawlAxisLenSq = (sprawlAxisDx * sprawlAxisDx) + (sprawlAxisDy * sprawlAxisDy);
			if (sprawlAxisLenSq > 1.0f)
			{
				const Real invLen = 1.0f / std::sqrt(sprawlAxisLenSq);
				sprawlAxisDx *= invLen;
				sprawlAxisDy *= invLen;
			}
			else
			{
				sprawlAxisDx = 0.0f;
				sprawlAxisDy = -1.0f;
			}

			const UnsignedInt telemetryNowTick = ::GetTickCount();
			AIControlAdapterMapPoint recentAttackTarget = { 0.0f, 0.0f };
			const bool hasRecentAttackTarget = AIControlAdapterTryGetRecentAttackTarget(
				m_autonomy.state.telemetryEvents,
				telemetryNowTick,
				45000u,
				recentAttackTarget);

			const GameSlot* selfSlot = findSlotForPlayer(player);
			const Int selfTeamNumber = selfSlot != nullptr ? selfSlot->getTeamNumber() : -1;
			bool hasPreferredEnemyBase = false;
			AIControlAdapterMapPoint preferredEnemyBase = { 0.0f, 0.0f };
			std::vector<AIControlAdapterMapPoint> knownEnemyBasePositions;
			auto captureEnemyBasePosition = [&](Player* candidate, bool preferred) -> void
			{
				if (candidate == nullptr || candidate == player || ThePlayerList == nullptr)
				{
					return;
				}
				const GameSlot* candidateSlot = findSlotForPlayer(candidate);
				if (candidateSlot != nullptr && selfSlot != nullptr)
				{
					const Int candidateTeamNumber = candidateSlot->getTeamNumber();
					if (candidateTeamNumber >= 0 && selfTeamNumber >= 0 && candidateTeamNumber == selfTeamNumber)
					{
						return;
					}
				}
				AIControlAdapterMapPoint candidatePos = { 0.0f, 0.0f };
				if (!AIControlAdapterTryReadMapPosition(buildPlayerMapPositionSummary(candidate), candidatePos))
				{
					return;
				}
				if (preferred)
				{
					preferredEnemyBase = candidatePos;
					hasPreferredEnemyBase = true;
					return;
				}
				knownEnemyBasePositions.push_back(candidatePos);
			};

			if (m_autonomy.state.hasExplicitPlayerIndex)
			{
				captureEnemyBasePosition(getPlayerByIndex(m_autonomy.state.playerIndex), true);
			}
			if (ThePlayerList != nullptr)
			{
				const Int playerCount = ThePlayerList->getPlayerCount();
				for (Int i = 0; i < playerCount; ++i)
				{
					Player* candidate = ThePlayerList->getNthPlayer(i);
					if (candidate == nullptr)
					{
						continue;
					}
					if (hasPreferredEnemyBase && candidate->getPlayerIndex() == m_autonomy.state.playerIndex)
					{
						continue;
					}
					captureEnemyBasePosition(candidate, false);
				}
			}

			auto resolveZoneFrontDirection = [&](const AutonomyZone& zone, Real& outDx, Real& outDy, const char*& outSource) -> void
			{
				const AIControlAdapterZoneFrontDirectionResult result = AIControlAdapterResolveZoneFrontDirection(
					{ zone.center.x, zone.center.y },
					hasRecentAttackTarget,
					recentAttackTarget,
					hasPreferredEnemyBase,
					preferredEnemyBase,
					knownEnemyBasePositions,
					sprawlAxisDx,
					sprawlAxisDy);
				outDx = result.dx;
				outDy = result.dy;
				outSource = result.source;
			};

			struct AutonomyZoneCounts
			{
				Int supplyStashes;
				Int supplyStashesInProgress;
				Int barracks;
				Int barracksInProgress;
				Int armsDealers;
				Int armsDealersInProgress;
				Int palaces;
				Int palacesInProgress;
				Int blackMarkets;
				Int blackMarketsInProgress;
				Int tunnels;
				Int tunnelsInProgress;
				Int stingers;
				Int stingersInProgress;
			};
			std::vector<AutonomyZoneCounts> zoneCounts(zones.size(), AutonomyZoneCounts{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
			if (!zones.empty())
			{
				const Real zoneRadiusSq = std::max<Real>(160.0f, m_autonomy.state.zoneRadius) * std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
				for (Object* obj = TheGameLogic != nullptr ? TheGameLogic->getFirstObject() : nullptr; obj != nullptr; obj = obj->getNextObject())
				{
					if (obj->isEffectivelyDead() || obj->getControllingPlayer() != player || !obj->isKindOf(KINDOF_STRUCTURE) || obj->getPosition() == nullptr)
					{
						continue;
					}
					const ThingTemplate* tt = obj->getTemplate();
					const std::string name = tt != nullptr ? tt->getName().str() : "";
					const bool underConstruction = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
					const bool countAsInProgress = underConstruction && static_cast<Int>(obj->getBuilderID()) > 0;
					auto incrementZoneStructureCount = [&](AutonomyZoneCounts& counts, Int AutonomyZoneCounts::* completeField, Int AutonomyZoneCounts::* inProgressField) -> void
					{
						if (countAsInProgress)
						{
							++(counts.*inProgressField);
						}
						else
						{
							++(counts.*completeField);
						}
					};
					for (std::size_t zoneIdx = 0; zoneIdx < zones.size(); ++zoneIdx)
					{
						const Real dx = obj->getPosition()->x - zones[zoneIdx].center.x;
						const Real dy = obj->getPosition()->y - zones[zoneIdx].center.y;
						if ((dx * dx) + (dy * dy) > zoneRadiusSq)
						{
							continue;
						}
						if (containsIgnoreCase(name, "supplystash") || containsIgnoreCase(name, "supplycenter"))
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::supplyStashes, &AutonomyZoneCounts::supplyStashesInProgress);
						}
						else if (containsIgnoreCase(name, "barracks"))
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::barracks, &AutonomyZoneCounts::barracksInProgress);
						}
						else if (containsIgnoreCase(name, "armsdealer"))
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::armsDealers, &AutonomyZoneCounts::armsDealersInProgress);
						}
						else if (containsIgnoreCase(name, "palace"))
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::palaces, &AutonomyZoneCounts::palacesInProgress);
						}
						else if (containsIgnoreCase(name, "black") && containsIgnoreCase(name, "market"))
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::blackMarkets, &AutonomyZoneCounts::blackMarketsInProgress);
						}
						else if (containsIgnoreCase(name, "tunnelnetwork"))
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::tunnels, &AutonomyZoneCounts::tunnelsInProgress);
						}
						else if (containsIgnoreCase(name, "stingersite"))
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::stingers, &AutonomyZoneCounts::stingersInProgress);
						}
					}
				}
			}
			AutonomyZoneCounts activeZoneCounts = (activeZoneIndex >= 0 && activeZoneIndex < static_cast<Int>(zoneCounts.size()))
				? zoneCounts[static_cast<std::size_t>(activeZoneIndex)]
				: AutonomyZoneCounts{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			m_autonomy.state.telemetryZones = nlohmann::json::array();
			for (std::size_t i = 0; i < zones.size(); ++i)
			{
				const Int zoneSupply = zoneCounts[i].supplyStashes + zoneCounts[i].supplyStashesInProgress;
				const Int zoneBarracks = zoneCounts[i].barracks + zoneCounts[i].barracksInProgress;
				const Int zoneArms = zoneCounts[i].armsDealers + zoneCounts[i].armsDealersInProgress;
				const Int zonePalaces = zoneCounts[i].palaces + zoneCounts[i].palacesInProgress;
				const Int zoneMarkets = zoneCounts[i].blackMarkets + zoneCounts[i].blackMarketsInProgress;
				const Int zoneTunnels = zoneCounts[i].tunnels + zoneCounts[i].tunnelsInProgress;
				const Int zoneStingers = zoneCounts[i].stingers + zoneCounts[i].stingersInProgress;
				const bool isActiveZone = hasActiveZone && static_cast<Int>(i) == activeZoneIndex;
				const bool zoneNeedsFollowup =
					zoneSupply > 0
					&& (zoneTunnels < 1 || zoneBarracks < 1 || zoneArms < 1 || zoneStingers < 1);
				const bool zoneDeveloped =
					zoneSupply > 0
					&& (zoneBarracks + zoneArms + zoneTunnels + zoneStingers) >= 3;
				const Real zoneRadius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
				Real zoneFrontDx = sprawlAxisDx;
				Real zoneFrontDy = sprawlAxisDy;
				const char* frontSource = "sprawl_axis";
				resolveZoneFrontDirection(zones[i], zoneFrontDx, zoneFrontDy, frontSource);
				const AIControlAdapterZoneFrontRearPoints zonePoints = AIControlAdapterBuildZoneFrontRearPoints(
					{ zones[i].center.x, zones[i].center.y },
					zoneRadius,
					zoneFrontDx,
					zoneFrontDy);
				m_autonomy.state.telemetryZones.push_back(nlohmann::json::object({
					{"anchor_id", static_cast<UnsignedInt>(zones[i].anchorId)},
					{"is_main_base", zones[i].isMainBase},
					{"active", isActiveZone},
					{"center_x", zones[i].center.x},
					{"center_y", zones[i].center.y},
					{"front_point_x", zonePoints.frontPoint.x},
					{"front_point_y", zonePoints.frontPoint.y},
					{"rear_point_x", zonePoints.rearPoint.x},
					{"rear_point_y", zonePoints.rearPoint.y},
					{"front_source", frontSource},
					{"supply_stashes", zoneSupply},
					{"barracks", zoneBarracks},
					{"arms_dealers", zoneArms},
					{"palaces", zonePalaces},
					{"black_markets", zoneMarkets},
					{"tunnels", zoneTunnels},
					{"stingers", zoneStingers},
					{"developed", zoneDeveloped},
					{"needs_followup", zoneNeedsFollowup}
				}));
			}
			adapterLog(
				"autonomy_macro_phase phase=zone_counts player=%d zone_supply=%d zone_barracks=%d zone_arms=%d",
				player->getPlayerIndex(),
				activeZoneCounts.supplyStashes + activeZoneCounts.supplyStashesInProgress,
				activeZoneCounts.barracks + activeZoneCounts.barracksInProgress,
				activeZoneCounts.armsDealers + activeZoneCounts.armsDealersInProgress);

			const Int totalSupplyStashes = counts.supplyStashes + counts.supplyStashesInProgress;
			const Int totalBarracks = counts.barracks + counts.barracksInProgress;
			const Int totalArmsDealers = counts.armsDealers + counts.armsDealersInProgress;
			const Int totalPalaces = counts.palaces + counts.palacesInProgress;
			const Int totalBlackMarkets = counts.blackMarkets + counts.blackMarketsInProgress;
			const Int totalTunnels = counts.tunnels + counts.tunnelsInProgress;
			const Int totalStingers = counts.stingers + counts.stingersInProgress;
			const Int totalZoneSupplyStashes = activeZoneCounts.supplyStashes + activeZoneCounts.supplyStashesInProgress;
			const Int totalZoneBarracks = activeZoneCounts.barracks + activeZoneCounts.barracksInProgress;
			const Int totalZoneArmsDealers = activeZoneCounts.armsDealers + activeZoneCounts.armsDealersInProgress;
			const Int totalZonePalaces = activeZoneCounts.palaces + activeZoneCounts.palacesInProgress;
			const Int totalZoneTunnels = activeZoneCounts.tunnels + activeZoneCounts.tunnelsInProgress;
			const Int totalZoneStingers = activeZoneCounts.stingers + activeZoneCounts.stingersInProgress;
			adapterLog(
				"autonomy_macro_phase phase=totals player=%d total_supply=%d total_barracks=%d total_arms=%d total_palaces=%d total_markets=%d",
				player->getPlayerIndex(),
				totalSupplyStashes,
				totalBarracks,
				totalArmsDealers,
				totalPalaces,
				totalBlackMarkets);

			auto tryCommand = [&](const char* requestIdPrefix, const char* cmd, const nlohmann::json& args, std::string& outReason) -> bool
			{
				char requestIdBuffer[96];
				sprintf_s(
					requestIdBuffer,
					"%s_%08X_%08X",
					requestIdPrefix,
					static_cast<unsigned int>(player->getPlayerIndex()),
					static_cast<unsigned int>(now));
				nlohmann::json message = {
					{"type", "SessionCommand"},
					{"request_id", std::string(requestIdBuffer)},
					{"cmd", std::string(cmd)},
					{"args", args}
				};
				if (m_autonomy.state.hasExplicitPlayerIndex)
				{
					message["args"]["player_index"] = m_autonomy.state.playerIndex;
				}

				bool ok = false;
				const std::string cmdString = cmd;
				if (cmdString == "Game.BuildSupplyStashSmart")
				{
					ok = executeGameBuildSupplyStashSmart(message, outReason);
				}
				else if (cmdString == "Game.BuildBarracksSmart")
				{
					ok = executeGameBuildBarracksSmart(message, outReason);
				}
				else if (cmdString == "Game.BuildArmsDealerSmart")
				{
					ok = executeGameBuildArmsDealerSmart(message, outReason);
				}
				else if (cmdString == "Game.BuildPalaceSmart")
				{
					ok = executeGameBuildPalaceSmart(message, outReason);
				}
				else if (cmdString == "Game.BuildBlackMarketSmart")
				{
					ok = executeGameBuildBlackMarketSmart(message, outReason);
				}
				else if (cmdString == "Game.BuildTunnelNetwork")
				{
					nlohmann::json tunneledMessage = message;
					tunneledMessage["cmd"] = "Game.BuildBarracksSmart";
					tunneledMessage["args"]["building_template"] = "GLATunnelNetwork";
					ok = executeGameBuildBarracksSmart(tunneledMessage, outReason);
				}
				else if (cmdString == "Game.BuildStingerSite")
				{
					nlohmann::json stingerMessage = message;
					stingerMessage["cmd"] = "Game.BuildBarracksSmart";
					stingerMessage["args"]["building_template"] = "GLAStingerSite";
					ok = executeGameBuildBarracksSmart(stingerMessage, outReason);
				}
				else if (cmdString == "Game.QueueSoldiersAllBarracks")
				{
					ok = executeGameQueueSoldiersAllBarracks(message, outReason);
				}
				else if (cmdString == "Game.QueueRpgTroopersAllBarracks")
				{
					ok = executeGameQueueRpgTroopersAllBarracks(message, outReason);
				}
				else if (cmdString == "Game.QueueQuadsAllWarFactories")
				{
					ok = executeGameQueueQuadsAllWarFactories(message, outReason);
				}
				else if (cmdString == "Game.QueueScorpionsAllWarFactories")
				{
					ok = executeGameQueueScorpionsAllWarFactories(message, outReason);
				}
				else if (cmdString == "Game.QueueScudLauncher")
				{
					nlohmann::json queuedMessage = message;
					if (!queuedMessage["args"].is_object())
					{
						queuedMessage["args"] = nlohmann::json::object();
					}
					queuedMessage["cmd"] = "Game.QueueUnit";
					queuedMessage["args"]["producer_kind"] = "any";
					queuedMessage["args"]["unit_template"] = inferAutonomyScudLauncherTemplate(player);
					ok = executeGameQueueUnit(queuedMessage, outReason);
				}
				else if (cmdString == "Game.PurchaseScience")
				{
					ok = executeGamePurchaseScience(message, outReason);
				}
				else if (cmdString == "Game.QueueUpgrade")
				{
					ok = executeGameQueueUpgrade(message, outReason);
				}
				else if (cmdString == "Game.GuardAllIdleGroundCombat")
				{
					ok = executeGameGuardAllIdleGroundCombat(message, outReason);
				}
				else
				{
					outReason = "unsupported_autonomy_command";
					return false;
				}

				adapterLog(
					"autonomy_command request_id=%s cmd=%s money=%lu ok=%d reason=%s",
					requestIdBuffer,
					cmd,
					static_cast<unsigned long>(money),
					ok ? 1 : 0,
					ok ? "" : outReason.c_str());
				if (ok)
				{
					m_cache.valid = false;
				}
				return ok;
			};

			auto tryZoneCommand = [&](const char* requestIdPrefix, const char* cmd, nlohmann::json args, std::string& outReason) -> bool
			{
				if (!hasActiveZone)
				{
					return tryCommand(requestIdPrefix, cmd, args, outReason);
				}
				Coord3D zoneCenter = activeZone.center;
				Real zoneRadius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
				const std::string cmdString = cmd != nullptr ? cmd : "";
				Real zoneFrontDx = sprawlAxisDx;
				Real zoneFrontDy = sprawlAxisDy;
				const char* ignoredFrontSource = "sprawl_axis";
				resolveZoneFrontDirection(activeZone, zoneFrontDx, zoneFrontDy, ignoredFrontSource);
				const bool hasAxis = (std::fabs(zoneFrontDx) > 0.0001f) || (std::fabs(zoneFrontDy) > 0.0001f);
				if (hasAxis && (cmdString == "Game.BuildTunnelNetwork" || cmdString == "Game.BuildStingerSite"))
				{
					const Real frontOffset = activeZone.isMainBase ? 0.45f : 0.72f;
					zoneCenter.x += zoneFrontDx * zoneRadius * frontOffset;
					zoneCenter.y += zoneFrontDy * zoneRadius * frontOffset;
					zoneRadius = std::max<Real>(96.0f, zoneRadius * 0.32f);
				}
				else if (hasAxis && (cmdString == "Game.BuildBarracksSmart" || cmdString == "Game.BuildArmsDealerSmart"))
				{
					const Real lineOffset = activeZone.isMainBase ? 0.18f : 0.38f;
					zoneCenter.x += zoneFrontDx * zoneRadius * lineOffset;
					zoneCenter.y += zoneFrontDy * zoneRadius * lineOffset;
					zoneRadius = std::max<Real>(120.0f, zoneRadius * 0.42f);
				}
				args["zone_center"] = nlohmann::json::object({
					{"x", zoneCenter.x},
					{"y", zoneCenter.y}
				});
				args["zone_radius"] = zoneRadius;
				args["strict_zone"] = false;
				return tryCommand(requestIdPrefix, cmd, args, outReason);
			};

			auto trySpecificZoneCommand = [&](const char* requestIdPrefix, const char* cmd, const AutonomyZone& zone, nlohmann::json args, std::string& outReason) -> bool
			{
				Coord3D zoneCenter = zone.center;
				Real zoneRadius = std::max<Real>(220.0f, m_autonomy.state.zoneRadius * 1.35f);
				const std::string cmdString = cmd != nullptr ? cmd : "";
				Real zoneFrontDx = sprawlAxisDx;
				Real zoneFrontDy = sprawlAxisDy;
				const char* ignoredFrontSource = "sprawl_axis";
				resolveZoneFrontDirection(zone, zoneFrontDx, zoneFrontDy, ignoredFrontSource);
				const bool hasAxis = (std::fabs(zoneFrontDx) > 0.0001f) || (std::fabs(zoneFrontDy) > 0.0001f);
				if (hasAxis && (cmdString == "Game.BuildTunnelNetwork" || cmdString == "Game.BuildStingerSite"))
				{
					const Real frontOffset = zone.isMainBase ? 0.45f : 0.72f;
					zoneCenter.x += zoneFrontDx * zoneRadius * frontOffset;
					zoneCenter.y += zoneFrontDy * zoneRadius * frontOffset;
					zoneRadius = std::max<Real>(96.0f, zoneRadius * 0.32f);
				}
				else if (hasAxis && (cmdString == "Game.BuildBarracksSmart" || cmdString == "Game.BuildArmsDealerSmart"))
				{
					const Real lineOffset = zone.isMainBase ? 0.18f : 0.38f;
					zoneCenter.x += zoneFrontDx * zoneRadius * lineOffset;
					zoneCenter.y += zoneFrontDy * zoneRadius * lineOffset;
					zoneRadius = std::max<Real>(120.0f, zoneRadius * 0.42f);
				}
				args["zone_center"] = nlohmann::json::object({
					{"x", zoneCenter.x},
					{"y", zoneCenter.y}
				});
				args["zone_radius"] = zoneRadius;
				args["strict_zone"] = false;
				return tryCommand(requestIdPrefix, cmd, args, outReason);
			};

			auto isSettlingSensitiveBuild = [&](const char* cmd) -> bool
			{
				return AIControlAdapterIsSettlingSensitiveBuild(cmd);
			};

			auto tryMacroBuildWithFallback = [&](const char* cmd, bool preferZone, std::string& outReason) -> bool
			{
				bool ok = preferZone
					? tryZoneCommand("auto_macro", cmd, nlohmann::json::object(), outReason)
					: tryCommand("auto_macro", cmd, nlohmann::json::object(), outReason);
				if (ok || outReason != "construct_site_not_created")
				{
					return ok;
				}
				if (isSettlingSensitiveBuild(cmd))
				{
					return false;
				}

				if (!preferZone && hasActiveZone)
				{
					return tryZoneCommand("auto_macro", cmd, nlohmann::json::object(), outReason);
				}
				if (preferZone)
				{
					return tryCommand("auto_macro", cmd, nlohmann::json::object(), outReason);
				}
				return false;
			};

			auto getBuildCooldownTick = [&](const char* cmd) -> DWORD*
			{
				const std::string cmdString = cmd != nullptr ? cmd : "";
				if (cmdString == "Game.BuildSupplyStashSmart")
				{
					return &m_autonomy.state.nextSupplyBuildTick;
				}
				if (cmdString == "Game.BuildBarracksSmart")
				{
					return &m_autonomy.state.nextBarracksBuildTick;
				}
				if (cmdString == "Game.BuildArmsDealerSmart")
				{
					return &m_autonomy.state.nextArmsBuildTick;
				}
				if (cmdString == "Game.BuildPalaceSmart")
				{
					return &m_autonomy.state.nextPalaceBuildTick;
				}
				if (cmdString == "Game.BuildBlackMarketSmart")
				{
					return &m_autonomy.state.nextMarketBuildTick;
				}
				if (cmdString == "Game.BuildTunnelNetwork")
				{
					return &m_autonomy.state.nextTunnelBuildTick;
				}
				if (cmdString == "Game.BuildStingerSite")
				{
					return &m_autonomy.state.nextStingerBuildTick;
				}
				return nullptr;
			};

			auto isBuildAttemptReady = [&](const char* cmd, Int inProgressCount) -> bool
			{
				if (inProgressCount > 0)
				{
					return false;
				}
				DWORD* nextAllowedTick = getBuildCooldownTick(cmd);
				return nextAllowedTick == nullptr || AIControlAdapterHasTickElapsed(*nextAllowedTick, now);
			};

			auto recordBuildAttempt = [&](const char* cmd, bool success, const std::string& outReason)
			{
				DWORD* nextAllowedTick = getBuildCooldownTick(cmd);
				if (nextAllowedTick == nullptr)
				{
					return;
				}
				const DWORD delayMs = AIControlAdapterGetBuildRetryDelayMs(cmd, success, outReason.c_str());
				*nextAllowedTick = now + delayMs;
			};

			if (AIControlAdapterHasTickElapsed(m_autonomy.state.nextMacroTick, now))
			{
				std::string reason;
				bool issued = false;
				const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
				const bool isBalancedSprawl = (profile == "sprawl_balanced");
				const bool isSprawlStyle = (profile == "sprawl" || isBalancedSprawl);
				const Real sprawlMultiplier = std::max<Real>(0.5f, std::min<Real>(10.0f, m_autonomy.state.sprawlMultiplier));
				const Int sprawlSupplyCap = std::max<Int>(1, static_cast<Int>(std::floor((isBalancedSprawl ? 3.0f : 4.0f) * sprawlMultiplier)));
				const Int sprawlBarracksCap = std::max<Int>(1, static_cast<Int>(std::floor((isBalancedSprawl ? 1.5f : 2.0f) * sprawlMultiplier)));
				const Int sprawlArmsCap = std::max<Int>(1, static_cast<Int>(std::floor((isBalancedSprawl ? 2.0f : 3.0f) * sprawlMultiplier)));
				const Int sprawlMarketCap = std::max<Int>(1, static_cast<Int>(std::floor((isBalancedSprawl ? 6.0f : 8.0f) * sprawlMultiplier)));
				const Int sprawlTunnelCap = std::max<Int>(1, static_cast<Int>(std::floor((isBalancedSprawl ? 5.0f : 8.0f) * sprawlMultiplier)));
				const Int sprawlStingerCap = std::max<Int>(1, static_cast<Int>(std::floor((isBalancedSprawl ? 4.0f : 6.0f) * sprawlMultiplier)));
				const UnsignedInt reserveCash = isBalancedSprawl ? 10000u : 5000u;
				const UnsignedInt blackMarketCost = 2500u;
				const bool openingInfrastructureReady = counts.supplyStashes >= 1 && counts.barracks >= 1 && counts.armsDealers >= 1;
				const bool openingEconomyReady = counts.supplyStashes >= 2 || counts.blackMarkets >= 1;
				const bool canScaleMilitaryProduction = !isBalancedSprawl || (openingInfrastructureReady && openingEconomyReady);
				const Int effectiveBarracksCap = isBalancedSprawl ? (canScaleMilitaryProduction ? sprawlBarracksCap : 1) : sprawlBarracksCap;
				const Int effectiveArmsCap = isBalancedSprawl ? (canScaleMilitaryProduction ? sprawlArmsCap : 1) : sprawlArmsCap;
				const Int sprawlDesiredMarketCount =
					std::max<Int>(
						isBalancedSprawl ? 2 : 1,
						std::min<Int>(
							sprawlMarketCap,
							std::max<Int>(
								isBalancedSprawl ? totalSupplyStashes : (totalSupplyStashes / 2),
								isBalancedSprawl ? ((totalBarracks + totalArmsDealers + 1) / 2) : ((totalBarracks + totalArmsDealers) / 4))));
				const bool remoteZoneHasStash = hasActiveZone && !activeZone.isMainBase && totalZoneSupplyStashes > 0;
				const bool remoteZoneNeedsFollowup =
					remoteZoneHasStash
					&& (totalZoneTunnels < 1
						|| totalZoneBarracks < 1
						|| totalZoneArmsDealers < 1
						|| totalZoneStingers < 1);
				const bool activeZoneIsDeveloped =
					hasActiveZone
					&& totalZoneSupplyStashes > 0
					&& (totalZoneBarracks + totalZoneArmsDealers + totalZoneTunnels + totalZoneStingers) >= 3;
				const bool shouldThrottleExtraStashGrowth =
					isSprawlStyle
					&& totalPalaces > 0
					&& totalBlackMarkets < std::max<Int>(isBalancedSprawl ? 2 : 1, isBalancedSprawl ? totalSupplyStashes : (totalSupplyStashes / 2))
					&& activeZoneIsDeveloped;
				const bool shouldPrioritizeMarketGrowth =
					isSprawlStyle
					&& totalPalaces > 0
					&& money >= (isBalancedSprawl ? (reserveCash + blackMarketCost) : blackMarketCost)
					&& (!isBalancedSprawl || counts.blackMarketsInProgress < 1)
					&& totalBlackMarkets < sprawlDesiredMarketCount;
				const bool shouldPreserveReserve =
					isBalancedSprawl
					&& (money < reserveCash || counts.blackMarketsInProgress > 0)
					&& totalPalaces > 0
					&& totalBlackMarkets > 0;
				const bool shouldForceEcoRecovery =
					isBalancedSprawl
					&& money < reserveCash
					&& totalSupplyStashes > 0;
				Int stashZoneCount = 0;
				Int developedZoneCount = 0;
				for (std::size_t zoneIdx = 0; zoneIdx < zoneCounts.size(); ++zoneIdx)
				{
					const Int zoneSupply = zoneCounts[zoneIdx].supplyStashes + zoneCounts[zoneIdx].supplyStashesInProgress;
					const Int zoneBarracks = zoneCounts[zoneIdx].barracks + zoneCounts[zoneIdx].barracksInProgress;
					const Int zoneArms = zoneCounts[zoneIdx].armsDealers + zoneCounts[zoneIdx].armsDealersInProgress;
					const Int zoneTunnels = zoneCounts[zoneIdx].tunnels + zoneCounts[zoneIdx].tunnelsInProgress;
					const Int zoneStingers = zoneCounts[zoneIdx].stingers + zoneCounts[zoneIdx].stingersInProgress;
					if (zoneSupply > 0)
					{
						++stashZoneCount;
					}
					if (zoneSupply > 0 && (zoneBarracks + zoneArms + zoneTunnels + zoneStingers) >= 3)
					{
						++developedZoneCount;
					}
				}
				const Int desiredZoneCount = std::max<Int>(1, sprawlSupplyCap);
				const char* requiredOpeningBuild = AIControlAdapterGetRequiredOpeningBuild({
					counts.supplyStashes,
					counts.barracks,
					counts.armsDealers
				});
				const bool canAttemptBlackMarketNow = AIControlAdapterCanAttemptBlackMarket({
					totalPalaces > 0,
					isBalancedSprawl,
					money,
					reserveCash,
					counts.blackMarketsInProgress
				});
				const char* ecoRecoveryBuild = AIControlAdapterGetEcoRecoveryBuild({
					isBalancedSprawl,
					totalSupplyStashes,
					totalPalaces,
					totalBlackMarkets,
					sprawlDesiredMarketCount,
					shouldThrottleExtraStashGrowth,
					canAttemptBlackMarketNow
				});
				const bool balancedZoneCanAddBarracks = !isBalancedSprawl || !hasActiveZone || totalZoneBarracks < 1;
				const bool balancedZoneCanAddArmsDealer = !isBalancedSprawl || !hasActiveZone || totalZoneArmsDealers < 1;
				const bool balancedZoneCanAddPalace = !isBalancedSprawl || !hasActiveZone || totalZonePalaces < 1;
				std::string chosenCommand = "none";
				if (requiredOpeningBuild != nullptr)
				{
					chosenCommand = requiredOpeningBuild;
					if (std::strcmp(requiredOpeningBuild, "Game.BuildSupplyStashSmart") == 0)
					{
						if (counts.supplyStashesInProgress > 0)
						{
							reason = "opening_wait_supply_stash";
						}
						else if (isBuildAttemptReady(requiredOpeningBuild, counts.supplyStashesInProgress) && money >= 1200u)
						{
							issued = tryMacroBuildWithFallback(requiredOpeningBuild, false, reason);
							recordBuildAttempt(requiredOpeningBuild, issued, reason);
						}
						else
						{
							reason = money < 1200u ? "opening_wait_money" : "opening_wait_supply_stash";
						}
					}
					else if (std::strcmp(requiredOpeningBuild, "Game.BuildBarracksSmart") == 0)
					{
						if (counts.barracksInProgress > 0)
						{
							reason = "opening_wait_barracks";
						}
						else if (isBuildAttemptReady(requiredOpeningBuild, counts.barracksInProgress) && money >= 600u)
						{
							issued = tryMacroBuildWithFallback(requiredOpeningBuild, false, reason);
							recordBuildAttempt(requiredOpeningBuild, issued, reason);
						}
						else
						{
							reason = money < 600u ? "opening_wait_money" : "opening_wait_barracks";
						}
					}
					else if (std::strcmp(requiredOpeningBuild, "Game.BuildArmsDealerSmart") == 0)
					{
						if (counts.armsDealersInProgress > 0)
						{
							reason = "opening_wait_arms_dealer";
						}
						else if (isBuildAttemptReady(requiredOpeningBuild, counts.armsDealersInProgress) && money >= 2500u)
						{
							issued = tryMacroBuildWithFallback(requiredOpeningBuild, false, reason);
							recordBuildAttempt(requiredOpeningBuild, issued, reason);
						}
						else
						{
							reason = money < 2500u ? "opening_wait_money" : "opening_wait_arms_dealer";
						}
					}
				}
				else if (totalSupplyStashes < 2 && (profile == "economic" || isSprawlStyle || m_autonomy.state.expansionBias >= 0.70f))
				{
					if (totalSupplyStashes < 2
						&& counts.supplyStashesInProgress < 1
						&& isBuildAttemptReady("Game.BuildSupplyStashSmart", counts.supplyStashesInProgress)
						&& money >= 1600u)
					{
						chosenCommand = "Game.BuildSupplyStashSmart";
						issued = tryMacroBuildWithFallback("Game.BuildSupplyStashSmart", false, reason);
						recordBuildAttempt("Game.BuildSupplyStashSmart", issued, reason);
					}
				}
				else if (totalPalaces < 1)
				{
					if (openingInfrastructureReady
						&& counts.workers >= 8
						&& totalPalaces < 1
						&& balancedZoneCanAddPalace
						&& counts.palacesInProgress < 1
						&& isBuildAttemptReady("Game.BuildPalaceSmart", counts.palacesInProgress)
						&& money >= 5000u)
					{
						chosenCommand = "Game.BuildPalaceSmart";
						issued = tryMacroBuildWithFallback("Game.BuildPalaceSmart", false, reason);
						recordBuildAttempt("Game.BuildPalaceSmart", issued, reason);
					}
				}
				else if (shouldForceEcoRecovery && ecoRecoveryBuild != nullptr)
				{
					chosenCommand = ecoRecoveryBuild;
					if (std::strcmp(ecoRecoveryBuild, "Game.BuildBlackMarketSmart") == 0)
					{
						if (counts.blackMarketsInProgress > 0)
						{
							reason = "eco_recovery_wait_market";
						}
						else if (isBuildAttemptReady(ecoRecoveryBuild, counts.blackMarketsInProgress) && canAttemptBlackMarketNow)
						{
							issued = tryMacroBuildWithFallback(ecoRecoveryBuild, hasActiveZone, reason);
							recordBuildAttempt(ecoRecoveryBuild, issued, reason);
						}
						else
						{
							reason = "eco_recovery_wait_market";
						}
					}
					else if (std::strcmp(ecoRecoveryBuild, "Game.BuildSupplyStashSmart") == 0)
					{
						if (counts.supplyStashesInProgress > 0)
						{
							reason = "eco_recovery_wait_supply";
						}
						else if (isBuildAttemptReady(ecoRecoveryBuild, counts.supplyStashesInProgress) && money >= 1800u)
						{
							issued = tryMacroBuildWithFallback(ecoRecoveryBuild, false, reason);
							recordBuildAttempt(ecoRecoveryBuild, issued, reason);
						}
						else
						{
							reason = money < 1800u ? "eco_recovery_wait_money" : "eco_recovery_wait_supply";
						}
					}
				}
				else if (isSprawlStyle
					&& remoteZoneNeedsFollowup
					&& totalZoneTunnels < 1
					&& activeZoneCounts.tunnelsInProgress < 1
					&& isBuildAttemptReady("Game.BuildTunnelNetwork", activeZoneCounts.tunnelsInProgress)
					&& money >= 900u)
				{
					chosenCommand = "Game.BuildTunnelNetwork";
					issued = tryMacroBuildWithFallback("Game.BuildTunnelNetwork", true, reason);
					recordBuildAttempt("Game.BuildTunnelNetwork", issued, reason);
				}
				else if (canScaleMilitaryProduction
					&& isSprawlStyle
					&& remoteZoneNeedsFollowup
					&& totalZoneBarracks < 1
					&& activeZoneCounts.barracksInProgress < 1
					&& isBuildAttemptReady("Game.BuildBarracksSmart", activeZoneCounts.barracksInProgress)
					&& money >= (isBalancedSprawl ? reserveCash : 800u))
				{
					chosenCommand = "Game.BuildBarracksSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBarracksSmart", true, reason);
					recordBuildAttempt("Game.BuildBarracksSmart", issued, reason);
				}
				else if (canScaleMilitaryProduction
					&& isSprawlStyle
					&& remoteZoneNeedsFollowup
					&& totalZoneArmsDealers < 1
					&& activeZoneCounts.armsDealersInProgress < 1
					&& isBuildAttemptReady("Game.BuildArmsDealerSmart", activeZoneCounts.armsDealersInProgress)
					&& money >= (isBalancedSprawl ? reserveCash : 2600u))
				{
					chosenCommand = "Game.BuildArmsDealerSmart";
					issued = tryMacroBuildWithFallback("Game.BuildArmsDealerSmart", true, reason);
					recordBuildAttempt("Game.BuildArmsDealerSmart", issued, reason);
				}
				else if (isSprawlStyle
					&& remoteZoneNeedsFollowup
					&& totalZoneStingers < 1
					&& activeZoneCounts.stingersInProgress < 1
					&& isBuildAttemptReady("Game.BuildStingerSite", activeZoneCounts.stingersInProgress)
					&& money >= (isBalancedSprawl ? 1800u : 1200u))
				{
					chosenCommand = "Game.BuildStingerSite";
					issued = tryMacroBuildWithFallback("Game.BuildStingerSite", true, reason);
					recordBuildAttempt("Game.BuildStingerSite", issued, reason);
				}
				else if (isSprawlStyle
					&& !remoteZoneNeedsFollowup
					&& stashZoneCount < desiredZoneCount
					&& counts.supplyStashesInProgress < 1
					&& isBuildAttemptReady("Game.BuildSupplyStashSmart", counts.supplyStashesInProgress)
					&& money >= (isBalancedSprawl ? (reserveCash + 1800u) : 1800u))
				{
					chosenCommand = "Game.BuildSupplyStashSmart";
					issued = tryMacroBuildWithFallback("Game.BuildSupplyStashSmart", false, reason);
					recordBuildAttempt("Game.BuildSupplyStashSmart", issued, reason);
				}
				else if (isSprawlStyle
					&& totalBlackMarkets < 1
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress)
					&& canAttemptBlackMarketNow)
				{
					chosenCommand = "Game.BuildBlackMarketSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBlackMarketSmart", false, reason);
					recordBuildAttempt("Game.BuildBlackMarketSmart", issued, reason);
				}
				else if (shouldPrioritizeMarketGrowth
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress)
					&& canAttemptBlackMarketNow)
				{
					chosenCommand = "Game.BuildBlackMarketSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBlackMarketSmart", false, reason);
					recordBuildAttempt("Game.BuildBlackMarketSmart", issued, reason);
				}
				else if (!shouldPreserveReserve
					&& canScaleMilitaryProduction
					&& isSprawlStyle
					&& totalBarracks < effectiveBarracksCap
					&& balancedZoneCanAddBarracks
					&& counts.barracksInProgress < 1
					&& isBuildAttemptReady("Game.BuildBarracksSmart", counts.barracksInProgress)
					&& money >= (isBalancedSprawl ? reserveCash : 800u))
				{
					chosenCommand = "Game.BuildBarracksSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBarracksSmart", true, reason);
					recordBuildAttempt("Game.BuildBarracksSmart", issued, reason);
				}
				else if (!shouldPreserveReserve
					&& canScaleMilitaryProduction
					&& isSprawlStyle
					&& totalArmsDealers < effectiveArmsCap
					&& balancedZoneCanAddArmsDealer
					&& counts.armsDealersInProgress < 1
					&& isBuildAttemptReady("Game.BuildArmsDealerSmart", counts.armsDealersInProgress)
					&& money >= (isBalancedSprawl ? reserveCash : 2600u))
				{
					chosenCommand = "Game.BuildArmsDealerSmart";
					issued = tryMacroBuildWithFallback("Game.BuildArmsDealerSmart", true, reason);
					recordBuildAttempt("Game.BuildArmsDealerSmart", issued, reason);
				}
				else if (totalPalaces > 0
					&& totalBlackMarkets < ((profile == "economic" || profile == "tech") ? 2 : 1)
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress)
					&& canAttemptBlackMarketNow)
				{
					chosenCommand = "Game.BuildBlackMarketSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBlackMarketSmart", false, reason);
					recordBuildAttempt("Game.BuildBlackMarketSmart", issued, reason);
				}
				else if (isSprawlStyle
					&& totalBlackMarkets < sprawlMarketCap
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress)
					&& canAttemptBlackMarketNow)
				{
					chosenCommand = "Game.BuildBlackMarketSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBlackMarketSmart", true, reason);
					recordBuildAttempt("Game.BuildBlackMarketSmart", issued, reason);
				}
				else if (!shouldPreserveReserve
					&& isSprawlStyle
					&& totalTunnels < sprawlTunnelCap
					&& counts.tunnelsInProgress < 1
					&& isBuildAttemptReady("Game.BuildTunnelNetwork", counts.tunnelsInProgress)
					&& money >= (isBalancedSprawl ? 1400u : 900u))
				{
					chosenCommand = "Game.BuildTunnelNetwork";
					issued = tryMacroBuildWithFallback("Game.BuildTunnelNetwork", true, reason);
					recordBuildAttempt("Game.BuildTunnelNetwork", issued, reason);
				}
				else if (!shouldPreserveReserve
					&& isSprawlStyle
					&& totalStingers < sprawlStingerCap
					&& counts.stingersInProgress < 1
					&& isBuildAttemptReady("Game.BuildStingerSite", counts.stingersInProgress)
					&& money >= (isBalancedSprawl ? 1800u : 1200u))
				{
					chosenCommand = "Game.BuildStingerSite";
					issued = tryMacroBuildWithFallback("Game.BuildStingerSite", true, reason);
					recordBuildAttempt("Game.BuildStingerSite", issued, reason);
				}
				else if (!shouldPreserveReserve
					&& isSprawlStyle
					&& !shouldThrottleExtraStashGrowth
					&& totalSupplyStashes < sprawlSupplyCap
					&& counts.supplyStashesInProgress < 1
					&& isBuildAttemptReady("Game.BuildSupplyStashSmart", counts.supplyStashesInProgress)
					&& money >= (isBalancedSprawl ? 2200u : 1800u))
				{
					chosenCommand = "Game.BuildSupplyStashSmart";
					issued = tryMacroBuildWithFallback("Game.BuildSupplyStashSmart", false, reason);
					recordBuildAttempt("Game.BuildSupplyStashSmart", issued, reason);
				}
				if (!issued && reason.empty())
				{
					if (shouldPreserveReserve)
					{
						reason = "macro_hold_reserve";
					}
					else if (shouldForceEcoRecovery)
					{
						reason = "macro_wait_eco_recovery";
					}
					else if (totalBlackMarkets < sprawlDesiredMarketCount && !canAttemptBlackMarketNow)
					{
						reason = "macro_wait_market_cash";
					}
					else if (remoteZoneNeedsFollowup)
					{
						reason = "macro_wait_zone_followup";
					}
					else if (stashZoneCount < desiredZoneCount)
					{
						reason = "macro_wait_zone_expansion";
					}
					else
					{
						reason = "macro_no_priority";
					}
				}

				m_autonomy.state.lastDecisionCategory = "macro";
				m_autonomy.state.lastDecisionCommand = chosenCommand;
				m_autonomy.state.lastDecisionReason = issued ? "ok" : reason;
				if (!chosenCommand.empty())
				{
					recordAutonomyTelemetryEvent(
						"macro",
						chosenCommand,
						issued ? "ok" : reason,
						hasActiveZone ? &activeZone.center : nullptr);
				}
				adapterLog(
					"autonomy_tick category=macro profile=%s money=%lu selected_zone=%lu zone_main=%d zone_center=(%.1f,%.1f) "
					"global[supply=%d barracks=%d arms=%d palace=%d markets=%d tunnels=%d stingers=%d workers=%d radar=%d soldiers=%d rpg=%d quads=%d scorpions=%d scuds=%d] "
					"zones[current=%d developed=%d desired=%d] zone[supply=%d barracks=%d arms=%d markets=%d tunnels=%d stingers=%d] action=%s issued=%d reason=%s",
					profile.c_str(),
					static_cast<unsigned long>(money),
					hasActiveZone ? static_cast<unsigned long>(activeZone.anchorId) : 0ul,
					(hasActiveZone && activeZone.isMainBase) ? 1 : 0,
					hasActiveZone ? activeZone.center.x : 0.0f,
					hasActiveZone ? activeZone.center.y : 0.0f,
					counts.supplyStashes,
					counts.barracks,
					counts.armsDealers,
					counts.palaces,
					counts.blackMarkets,
					counts.tunnels,
					counts.stingers,
					counts.workers,
					counts.radarVans,
					counts.soldiers,
					counts.rpg,
					counts.quads,
					counts.scorpions,
					counts.scudLaunchers,
					stashZoneCount,
					developedZoneCount,
					desiredZoneCount,
					activeZoneCounts.supplyStashes,
					activeZoneCounts.barracks,
					activeZoneCounts.armsDealers,
					activeZoneCounts.blackMarkets,
					activeZoneCounts.tunnels,
					activeZoneCounts.stingers,
					chosenCommand.c_str(),
					issued ? 1 : 0,
					(issued ? "ok" : reason.c_str()));

				if (isSprawlStyle && hasActiveZone && !zones.empty())
				{
					m_autonomy.state.nextZoneIndex = (m_autonomy.state.nextZoneIndex + 1u) % zones.size();
				}
				m_autonomy.state.nextMacroTick = now + (issued ? 3000u : 2000u);
			}

			if (AIControlAdapterHasTickElapsed(m_autonomy.state.nextProductionTick, now))
			{
				std::string reason;
				bool issued = false;
				const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
				const bool isBalancedSprawl = (profile == "sprawl_balanced");
				const Int armyCap = isBalancedSprawl ? 100 : 9999;
				const Int armyCount = std::max<Int>(0, counts.mobileUnits - counts.workers);
				const UnsignedInt reserveCash = isBalancedSprawl ? 10000u : 0u;
				const bool openingInfrastructureReady = totalSupplyStashes >= 1 && totalBarracks >= 1 && totalArmsDealers >= 1;
				const bool openingEconomyReady = totalSupplyStashes >= 2 || totalBlackMarkets >= 1;
				const bool wasRecoveringFromReserve =
					(m_autonomy.state.lastDecisionCategory == "production" && m_autonomy.state.lastDecisionReason == "reserve_cash_recovery");
				const bool shouldPauseCombatProduction = AIControlAdapterShouldPauseCombatProduction({
					isBalancedSprawl,
					openingInfrastructureReady,
					openingEconomyReady,
					wasRecoveringFromReserve,
					money,
					reserveCash,
					counts.blackMarketsInProgress,
					counts.supplyStashesInProgress
				});
				const bool wasArmyCapReached =
					(m_autonomy.state.lastDecisionCategory == "production" && m_autonomy.state.lastDecisionReason == "army_cap_reached");
				const bool shouldHoldArmyCap = AIControlAdapterShouldHoldArmyCap({
					shouldPauseCombatProduction,
					isBalancedSprawl,
					wasArmyCapReached,
					armyCount,
					armyCap
				});
				const Real zoneRadiusSq = std::max<Real>(160.0f, m_autonomy.state.zoneRadius) * std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
				std::string chosenCommand = "none";

				auto queueAtPreferredZoneProducer = [&](bool wantBarracks, const std::string& unitTemplateName, const char* cmdName, std::string& outReason) -> bool
				{
					if (!hasActiveZone || unitTemplateName.empty())
					{
						outReason = "no_zone_producer";
						return false;
					}

					std::vector<Object*> candidates;
					for (Object* obj = TheGameLogic != nullptr ? TheGameLogic->getFirstObject() : nullptr; obj != nullptr; obj = obj->getNextObject())
					{
						if (obj->isEffectivelyDead() || obj->getControllingPlayer() != player || !obj->isKindOf(KINDOF_STRUCTURE) || obj->getPosition() == nullptr)
						{
							continue;
						}
						if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
						{
							continue;
						}
						const Real dx = obj->getPosition()->x - activeZone.center.x;
						const Real dy = obj->getPosition()->y - activeZone.center.y;
						if ((dx * dx) + (dy * dy) > zoneRadiusSq)
						{
							continue;
						}
						const ThingTemplate* tt = obj->getTemplate();
						const std::string name = tt != nullptr ? tt->getName().str() : "";
						if (wantBarracks)
						{
							if (!containsIgnoreCase(name, "barracks"))
							{
								continue;
							}
						}
						else
						{
							if (!(containsIgnoreCase(name, "warfactory") || containsIgnoreCase(name, "armsdealer")))
							{
								continue;
							}
						}
						candidates.push_back(obj);
					}

					if (candidates.empty())
					{
						outReason = "no_zone_producer";
						return false;
					}

					std::sort(candidates.begin(), candidates.end(), [&](Object* lhs, Object* rhs) -> bool
					{
						const Real ldx = lhs->getPosition()->x - activeZone.center.x;
						const Real ldy = lhs->getPosition()->y - activeZone.center.y;
						const Real rdx = rhs->getPosition()->x - activeZone.center.x;
						const Real rdy = rhs->getPosition()->y - activeZone.center.y;
						return (ldx * ldx) + (ldy * ldy) < (rdx * rdx) + (rdy * rdy);
					});

					nlohmann::json queuedMessage = {
						{"type", "SessionCommand"},
						{"request_id", std::string(cmdName)},
						{"cmd", "Game.QueueUnit"},
						{"args", nlohmann::json::object({
							{"producer_kind", "any"},
							{"unit_template", unitTemplateName}
						})}
					};
					if (m_autonomy.state.hasExplicitPlayerIndex)
					{
						queuedMessage["args"]["player_index"] = m_autonomy.state.playerIndex;
					}

					for (Object* producer : candidates)
					{
						queuedMessage["args"]["producer_object_id"] = static_cast<Int>(producer->getID());
						std::string queueReason;
						if (executeGameQueueUnit(queuedMessage, queueReason))
						{
							return true;
						}
						outReason = queueReason;
						if (queueReason == "no_money")
						{
							break;
						}
					}
					return false;
				};

				auto tryChosenProduction = [&](const char* preferredCommand) -> bool
				{
					if (preferredCommand == nullptr || *preferredCommand == '\0')
					{
						return false;
					}

					if (std::strcmp(preferredCommand, "Game.QueueScudLauncher") == 0)
					{
						chosenCommand = "Game.QueueScudLauncher";
						issued = tryCommand("auto_prod", "Game.QueueScudLauncher", nlohmann::json::object({ {"count", 1} }), reason);
						return issued;
					}

					if (std::strcmp(preferredCommand, "Game.QueueQuadsAllWarFactories") == 0)
					{
						chosenCommand = "Game.QueueQuadsAllWarFactories";
						if (isBalancedSprawl)
						{
							Object* referenceFactory = nullptr;
							for (Object* obj = TheGameLogic != nullptr ? TheGameLogic->getFirstObject() : nullptr; obj != nullptr; obj = obj->getNextObject())
							{
								if (obj->isEffectivelyDead() || obj->getControllingPlayer() != player || !obj->isKindOf(KINDOF_STRUCTURE))
								{
									continue;
								}
								const ThingTemplate* tt = obj->getTemplate();
								const std::string name = tt != nullptr ? tt->getName().str() : "";
								if (containsIgnoreCase(name, "warfactory") || containsIgnoreCase(name, "armsdealer"))
								{
									referenceFactory = obj;
									break;
								}
							}
							issued = queueAtPreferredZoneProducer(false, inferQuadTemplateForProducer(referenceFactory), "auto_prod_zone_quad", reason);
						}
						if (!issued)
						{
							issued = tryCommand("auto_prod", "Game.QueueQuadsAllWarFactories", nlohmann::json::object({ {"count", 1} }), reason);
						}
						return issued;
					}

					if (std::strcmp(preferredCommand, "Game.QueueScorpionsAllWarFactories") == 0)
					{
						chosenCommand = "Game.QueueScorpionsAllWarFactories";
						if (isBalancedSprawl)
						{
							Object* referenceFactory = nullptr;
							for (Object* obj = TheGameLogic != nullptr ? TheGameLogic->getFirstObject() : nullptr; obj != nullptr; obj = obj->getNextObject())
							{
								if (obj->isEffectivelyDead() || obj->getControllingPlayer() != player || !obj->isKindOf(KINDOF_STRUCTURE))
								{
									continue;
								}
								const ThingTemplate* tt = obj->getTemplate();
								const std::string name = tt != nullptr ? tt->getName().str() : "";
								if (containsIgnoreCase(name, "warfactory") || containsIgnoreCase(name, "armsdealer"))
								{
									referenceFactory = obj;
									break;
								}
							}
							issued = queueAtPreferredZoneProducer(false, inferScorpionTemplateForProducer(referenceFactory), "auto_prod_zone_scorpion", reason);
						}
						if (!issued)
						{
							issued = tryCommand("auto_prod", "Game.QueueScorpionsAllWarFactories", nlohmann::json::object({ {"count", 1} }), reason);
						}
						return issued;
					}

					if (std::strcmp(preferredCommand, "Game.QueueRpgTroopersAllBarracks") == 0)
					{
						chosenCommand = "Game.QueueRpgTroopersAllBarracks";
						if (isBalancedSprawl)
						{
							Object* referenceBarracks = nullptr;
							for (Object* obj = TheGameLogic != nullptr ? TheGameLogic->getFirstObject() : nullptr; obj != nullptr; obj = obj->getNextObject())
							{
								if (obj->isEffectivelyDead() || obj->getControllingPlayer() != player || !obj->isKindOf(KINDOF_STRUCTURE))
								{
									continue;
								}
								const ThingTemplate* tt = obj->getTemplate();
								const std::string name = tt != nullptr ? tt->getName().str() : "";
								if (containsIgnoreCase(name, "barracks"))
								{
									referenceBarracks = obj;
									break;
								}
							}
							issued = queueAtPreferredZoneProducer(true, inferRpgTemplateForProducer(referenceBarracks), "auto_prod_zone_rpg", reason);
						}
						if (!issued)
						{
							issued = tryCommand("auto_prod", "Game.QueueRpgTroopersAllBarracks", nlohmann::json::object({ {"count", 1} }), reason);
						}
						return issued;
					}

					if (std::strcmp(preferredCommand, "Game.QueueSoldiersAllBarracks") == 0)
					{
						chosenCommand = "Game.QueueSoldiersAllBarracks";
						if (isBalancedSprawl)
						{
							Object* referenceBarracks = nullptr;
							for (Object* obj = TheGameLogic != nullptr ? TheGameLogic->getFirstObject() : nullptr; obj != nullptr; obj = obj->getNextObject())
							{
								if (obj->isEffectivelyDead() || obj->getControllingPlayer() != player || !obj->isKindOf(KINDOF_STRUCTURE))
								{
									continue;
								}
								const ThingTemplate* tt = obj->getTemplate();
								const std::string name = tt != nullptr ? tt->getName().str() : "";
								if (containsIgnoreCase(name, "barracks"))
								{
									referenceBarracks = obj;
									break;
								}
							}
							issued = queueAtPreferredZoneProducer(true, inferSoldierTemplateForPlayer(player, referenceBarracks), "auto_prod_zone_soldier", reason);
						}
						if (!issued)
						{
							issued = tryCommand("auto_prod", "Game.QueueSoldiersAllBarracks", nlohmann::json::object({ {"count", 1} }), reason);
						}
						return issued;
					}
					return issued;
				};
				const char* pauseReason = openingInfrastructureReady ? "reserve_cash_recovery" : "opening_not_ready";
				const AIControlAdapterProductionChoiceResult preferredProduction = AIControlAdapterChoosePreferredProductionCommand({
					shouldPauseCombatProduction,
					pauseReason,
					shouldHoldArmyCap,
					isBalancedSprawl,
					profile.c_str(),
					money,
					counts.barracks,
					counts.armsDealers,
					counts.palaces,
					counts.soldiers,
					counts.rpg,
					counts.quads,
					counts.scorpions,
					counts.scudLaunchers,
					counts.radarVans,
					armyCount,
					armyCap
				});

				if (shouldPauseCombatProduction)
				{
					reason = preferredProduction.reason != nullptr ? preferredProduction.reason : pauseReason;
				}
				else if (shouldHoldArmyCap)
				{
					reason = preferredProduction.reason != nullptr ? preferredProduction.reason : "army_cap_reached";
				}
				else if (preferredProduction.command != nullptr)
				{
					tryChosenProduction(preferredProduction.command);
					if (!issued
						&& reason == "queue_full"
						&& (std::strcmp(preferredProduction.command, "Game.QueueRpgTroopersAllBarracks") == 0
							|| std::strcmp(preferredProduction.command, "Game.QueueSoldiersAllBarracks") == 0))
					{
						tryChosenProduction("Game.QueueQuadsAllWarFactories");
					}
				}
				m_autonomy.state.lastDecisionCategory = "production";
				m_autonomy.state.lastDecisionCommand = chosenCommand;
				m_autonomy.state.lastDecisionReason = issued ? "ok" : reason;
				if (!chosenCommand.empty())
				{
					Coord3D zoneCenter = {};
					zoneCenter.x = m_autonomy.state.lastZoneCenterX;
					zoneCenter.y = m_autonomy.state.lastZoneCenterY;
					zoneCenter.z = 0.0f;
					recordAutonomyTelemetryEvent(
						"production",
						chosenCommand,
						issued ? "ok" : reason,
						m_autonomy.state.hasLastZone ? &zoneCenter : nullptr);
				}
				adapterLog(
					"autonomy_tick category=production profile=%s money=%lu action=%s issued=%d reason=%s units[soldiers=%d rpg=%d quads=%d scorpions=%d scuds=%d radar=%d]",
					profile.c_str(),
					static_cast<unsigned long>(money),
					chosenCommand.c_str(),
					issued ? 1 : 0,
					(issued ? "ok" : reason.c_str()),
					counts.soldiers,
					counts.rpg,
					counts.quads,
					counts.scorpions,
					counts.scudLaunchers,
					counts.radarVans);
				m_autonomy.state.nextProductionTick = now + AIControlAdapterGetProductionRetryDelayMs(issued, reason.c_str());
			}

			if (AIControlAdapterHasTickElapsed(m_autonomy.state.nextTechTick, now))
			{
				const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
				if (profile == "sprawl" || profile == "sprawl_balanced" || profile == "tech")
				{
					bool issued = false;
					std::string reason;
					const bool hasScienceEconomy = totalSupplyStashes >= 2;
					const bool hasScienceAdvanced = totalPalaces > 0;

					if (money >= 1000u)
					{
						for (int i = 0; i < static_cast<int>(sizeof(kAutonomySciencePlan) / sizeof(kAutonomySciencePlan[0])); ++i)
						{
							const std::string scienceName = kAutonomySciencePlan[i];
							if (scienceName == "SCIENCE_ScudLauncher" && !hasScienceAdvanced)
							{
								continue;
							}
							if (scienceName.find("SCIENCE_CashBounty") == 0 && !hasScienceEconomy)
							{
								continue;
							}
							nlohmann::json args = nlohmann::json::object({
								{"science_name", scienceName}
							});
							if (tryCommand("auto_tech", "Game.PurchaseScience", args, reason))
							{
								issued = true;
								m_autonomy.state.lastDecisionCategory = "tech";
								m_autonomy.state.lastDecisionCommand = "Game.PurchaseScience";
								m_autonomy.state.lastDecisionReason = std::string("ok:") + scienceName;
								Coord3D zoneCenter = {};
								zoneCenter.x = m_autonomy.state.lastZoneCenterX;
								zoneCenter.y = m_autonomy.state.lastZoneCenterY;
								zoneCenter.z = 0.0f;
								recordAutonomyTelemetryEvent(
									"tech",
									"Game.PurchaseScience",
									m_autonomy.state.lastDecisionReason,
									m_autonomy.state.hasLastZone ? &zoneCenter : nullptr);
								break;
							}
							if (AIControlAdapterShouldAbortSciencePlanForTick(reason.c_str()))
							{
								break;
							}
						}
					}

					if (!issued && money >= 1500u)
					{
						for (int i = 0; i < static_cast<int>(sizeof(kAutonomyUpgradePlan) / sizeof(kAutonomyUpgradePlan[0])); ++i)
						{
							const std::string producerKind = kAutonomyUpgradePlan[i].producerKind;
							if (producerKind == "black_market" && totalBlackMarkets < 1)
							{
								continue;
							}
							if (producerKind == "palace" && totalPalaces < 1)
							{
								continue;
							}
							if (producerKind == "any" && totalBarracks < 1 && totalArmsDealers < 1 && totalPalaces < 1)
							{
								continue;
							}
							nlohmann::json args = nlohmann::json::object({
								{"producer_kind", producerKind},
								{"upgrade_name", std::string(kAutonomyUpgradePlan[i].upgradeName)}
							});
							if (tryCommand("auto_tech", "Game.QueueUpgrade", args, reason))
							{
								issued = true;
								m_autonomy.state.lastDecisionCategory = "tech";
								m_autonomy.state.lastDecisionCommand = "Game.QueueUpgrade";
								m_autonomy.state.lastDecisionReason = std::string("ok:") + kAutonomyUpgradePlan[i].upgradeName;
								Coord3D zoneCenter = {};
								zoneCenter.x = m_autonomy.state.lastZoneCenterX;
								zoneCenter.y = m_autonomy.state.lastZoneCenterY;
								zoneCenter.z = 0.0f;
								recordAutonomyTelemetryEvent(
									"tech",
									"Game.QueueUpgrade",
									m_autonomy.state.lastDecisionReason,
									m_autonomy.state.hasLastZone ? &zoneCenter : nullptr);
								break;
							}
							if (AIControlAdapterShouldAbortUpgradePlanForTick(reason.c_str()))
							{
								break;
							}
						}
					}

					if (!issued)
					{
						m_autonomy.state.lastDecisionCategory = "tech";
						m_autonomy.state.lastDecisionCommand = "none";
						m_autonomy.state.lastDecisionReason = reason.empty() ? "tech_prereq_missing" : reason;
					}

					m_autonomy.state.nextTechTick = now + AIControlAdapterGetTechRetryDelayMs(issued, m_autonomy.state.lastDecisionReason.c_str());
				}
				else
				{
					m_autonomy.state.nextTechTick = now + 5000u;
				}
			}

			if (AIControlAdapterHasTickElapsed(m_autonomy.state.nextGuardTick, now))
			{
				const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
				const Int combatCount = counts.soldiers + counts.rpg + counts.quads + counts.scorpions;
				const DWORD cadenceMs = (profile == "aggressive") ? 5000u : (((profile == "sprawl" || profile == "sprawl_balanced" || profile == "defensive")) ? 7000u : 6000u);
				if (combatCount > 0)
				{
					std::string reason;
					const bool guarded = tryCommand("auto_guard", "Game.GuardAllIdleGroundCombat", nlohmann::json::object(), reason);
					m_autonomy.state.nextGuardTick = now + (guarded ? cadenceMs : 4000u);
				}
				else
				{
					m_autonomy.state.nextGuardTick = now + 3000u;
				}
			}
		}

		bool parseAutonomyMode(const nlohmann::json& args, std::string& outMode, std::string& reason) const
		{
			const auto modeIt = args.find("mode");
			if (modeIt == args.end() || !modeIt->is_string())
			{
				reason = "missing_mode";
				return false;
			}
			const std::string mode = normalizeAsciiLower(modeIt->get<std::string>());
			if (mode != "manual" && mode != "hybrid" && mode != "autonomous")
			{
				reason = "invalid_mode";
				return false;
			}
			outMode = mode;
			return true;
		}

		bool configureAutonomy(const nlohmann::json& message, std::string& reason)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto profileIt = argsIt->find("profile");
			if (profileIt != argsIt->end())
			{
				if (!profileIt->is_string())
				{
					reason = "invalid_profile";
					return false;
				}
				const std::string profile = normalizeAsciiLower(profileIt->get<std::string>());
				if (profile != "standard"
					&& profile != "aggressive"
					&& profile != "economic"
					&& profile != "defensive"
					&& profile != "tech"
					&& profile != "builtin_passthrough"
					&& profile != "sprawl"
					&& profile != "sprawl_balanced")
				{
					reason = "invalid_profile";
					return false;
				}
				m_autonomy.state.profile = profile;
			}

			auto parseBias = [&](const char* key, Real& target) -> bool
			{
				const auto it = argsIt->find(key);
				if (it == argsIt->end())
				{
					return true;
				}
				if (!it->is_number())
				{
					reason = std::string("invalid_") + key;
					return false;
				}
				target = clampUnitFloat(it->get<Real>(), 0.0f, 1.0f);
				return true;
			};
			if (!parseBias("economy_bias", m_autonomy.state.economyBias)
				|| !parseBias("aggression_bias", m_autonomy.state.aggressionBias)
				|| !parseBias("defense_bias", m_autonomy.state.defenseBias)
				|| !parseBias("expansion_bias", m_autonomy.state.expansionBias))
			{
				return false;
			}

			const auto sprawlMultiplierIt = argsIt->find("sprawl_multiplier");
			if (sprawlMultiplierIt != argsIt->end())
			{
				if (!sprawlMultiplierIt->is_number())
				{
					reason = "invalid_sprawl_multiplier";
					return false;
				}
				m_autonomy.state.sprawlMultiplier = std::max<Real>(0.5f, std::min<Real>(10.0f, sprawlMultiplierIt->get<Real>()));
			}

			const auto zoneRadiusIt = argsIt->find("zone_radius");
			if (zoneRadiusIt != argsIt->end())
			{
				if (!zoneRadiusIt->is_number())
				{
					reason = "invalid_zone_radius";
					return false;
				}
				m_autonomy.state.zoneRadius = std::max<Real>(120.0f, std::min<Real>(4000.0f, zoneRadiusIt->get<Real>()));
			}

			const auto captureIt = argsIt->find("capture_tech");
			if (captureIt != argsIt->end())
			{
				if (!captureIt->is_boolean())
				{
					reason = "invalid_capture_tech";
					return false;
				}
				m_autonomy.state.captureTech = captureIt->get<bool>();
			}

			const auto attackEnabledIt = argsIt->find("attack_enabled");
			if (attackEnabledIt != argsIt->end())
			{
				if (!attackEnabledIt->is_boolean())
				{
					reason = "invalid_attack_enabled";
					return false;
				}
				m_autonomy.state.hasAttackAutomationEnabledOverride = true;
				m_autonomy.state.attackAutomationEnabled = attackEnabledIt->get<bool>();
			}

			const auto superIt = argsIt->find("allow_superweapons");
			if (superIt != argsIt->end())
			{
				if (!superIt->is_boolean())
				{
					reason = "invalid_allow_superweapons";
					return false;
				}
				m_autonomy.state.allowSuperweapons = superIt->get<bool>();
			}

			const auto playerIndexIt = argsIt->find("player_index");
			if (playerIndexIt != argsIt->end())
			{
				if (!playerIndexIt->is_number_integer())
				{
					reason = "invalid_player_index";
					return false;
				}
				m_autonomy.state.hasExplicitPlayerIndex = true;
				m_autonomy.state.playerIndex = playerIndexIt->get<Int>();
			}

			const auto targetPlayerIt = argsIt->find("target_player_index");
			if (targetPlayerIt != argsIt->end())
			{
				if (!targetPlayerIt->is_number_integer())
				{
					reason = "invalid_target_player_index";
					return false;
				}
				m_autonomy.state.hasExplicitTargetPlayerIndex = true;
				m_autonomy.state.targetPlayerIndex = targetPlayerIt->get<Int>();
			}

			if (isAutonomyModeActive())
			{
				applyAutonomyRules();
				kickAutonomyEvaluation();
			}
			return true;
		}

		bool setAutonomyMode(const nlohmann::json& message, std::string& reason)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			std::string mode;
			if (!parseAutonomyMode(*argsIt, mode, reason))
			{
				return false;
			}

			m_autonomy.state.mode = mode;
			m_autonomy.state.paused = false;
			applyAutonomyRules();
			kickAutonomyEvaluation();
			return true;
		}

		void pauseAutonomy()
		{
			m_autonomy.state.paused = true;
			applyAutonomyRules();
		}

		void resumeAutonomy()
		{
			m_autonomy.state.paused = false;
			applyAutonomyRules();
			kickAutonomyEvaluation();
		}

		void resetAutonomy()
		{
			clearAutonomyManagedRules();
			resetAutonomyState();
			adapterLog("autonomy_reset");
		}

		nlohmann::json buildAutonomyStatus()
		{
			nlohmann::json result = nlohmann::json::object();
			result["mode"] = m_autonomy.state.mode;
			result["profile"] = m_autonomy.state.profile;
			result["paused"] = m_autonomy.state.paused;
			result["active"] = isAutonomyModeActive();
			result["capture_tech"] = m_autonomy.state.captureTech;
			result["allow_superweapons"] = m_autonomy.state.allowSuperweapons;
			result["attack_enabled"] = m_automation.attackRule.enabled;
			result["economy_bias"] = m_autonomy.state.economyBias;
			result["aggression_bias"] = m_autonomy.state.aggressionBias;
			result["defense_bias"] = m_autonomy.state.defenseBias;
			result["expansion_bias"] = m_autonomy.state.expansionBias;
			result["sprawl_multiplier"] = m_autonomy.state.sprawlMultiplier;
			result["zone_radius"] = m_autonomy.state.zoneRadius;
			result["target_player_index"] = m_autonomy.state.hasExplicitTargetPlayerIndex ? m_autonomy.state.targetPlayerIndex : -1;
			result["last_applied_tick"] = static_cast<UnsignedInt>(m_autonomy.state.lastAppliedTick);
			result["selected_zone"] = nlohmann::json::object({
				{"active", m_autonomy.state.hasLastZone},
				{"anchor_id", m_autonomy.state.hasLastZone ? m_autonomy.state.lastZoneAnchorId : 0u},
				{"is_main_base", m_autonomy.state.hasLastZone ? m_autonomy.state.lastZoneIsMainBase : false},
				{"center_x", m_autonomy.state.hasLastZone ? m_autonomy.state.lastZoneCenterX : 0.0f},
				{"center_y", m_autonomy.state.hasLastZone ? m_autonomy.state.lastZoneCenterY : 0.0f}
			});

			Player* player = resolveAutonomyPlayer();
			if (player != nullptr)
			{
				UnsignedInt money = 0u;
				const Money* wallet = player->getMoney();
				if (wallet != nullptr)
				{
					money = wallet->countMoney();
				}

				result["player"] = buildLocalPlayerSummary(player);
				result["player_index"] = player->getPlayerIndex();
				result["money"] = money;
				result["rank_level"] = player->getRankLevel();

				struct AutonomyAssetCountContext
				{
					Int units;
					Int buildings;
					Int workers;
					Int supplyStashes;
					Int barracks;
					Int armsDealers;
					Int palaces;
					Int blackMarkets;
					Int scudStorms;
					Int radarVans;
				} counts = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

				player->iterateObjects([](Object* obj, void* userData)
				{
					if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
					{
						return;
					}
					AutonomyAssetCountContext* counts = static_cast<AutonomyAssetCountContext*>(userData);
					const ThingTemplate* tt = obj->getTemplate();
					const std::string name = tt != nullptr ? tt->getName().str() : "";
					if (obj->isKindOf(KINDOF_STRUCTURE))
					{
						if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
						{
							return;
						}
						++counts->buildings;
						if (containsIgnoreCase(name, "supplystash") || containsIgnoreCase(name, "supplycenter"))
						{
							++counts->supplyStashes;
						}
						if (containsIgnoreCase(name, "barracks"))
						{
							++counts->barracks;
						}
						if (containsIgnoreCase(name, "armsdealer"))
						{
							++counts->armsDealers;
						}
						if (isPalaceTemplateName(name))
						{
							++counts->palaces;
						}
						if (containsIgnoreCase(name, "black") && containsIgnoreCase(name, "market"))
						{
							++counts->blackMarkets;
						}
						if (containsIgnoreCase(name, "scud") && containsIgnoreCase(name, "storm"))
						{
							++counts->scudStorms;
						}
					}
					else
					{
						++counts->units;
						if (obj->isKindOf(KINDOF_DOZER))
						{
							++counts->workers;
						}
						if (containsIgnoreCase(name, "radarvan"))
						{
							++counts->radarVans;
						}
					}
				}, &counts);

				result["assets"] = nlohmann::json::object({
					{"units", counts.units},
					{"buildings", counts.buildings},
					{"workers", counts.workers},
					{"supply_stashes", counts.supplyStashes},
					{"barracks", counts.barracks},
					{"arms_dealers", counts.armsDealers},
					{"palaces", counts.palaces},
					{"black_markets", counts.blackMarkets},
					{"scud_storms", counts.scudStorms},
					{"radar_vans", counts.radarVans}
				});
			}

			result["rules"] = nlohmann::json::object({
				{"worker", nlohmann::json::object({
					{"enabled", m_automation.workerRule.enabled},
					{"min_idle_workers", m_automation.workerRule.minIdleWorkers},
					{"queue_count", m_automation.workerRule.queueCount},
					{"producer_kind", m_automation.workerRule.hasExplicitProducerKind ? m_automation.workerRule.producerKind : "default"}
				})},
				{"stash_worker", nlohmann::json::object({
					{"enabled", m_automation.stashWorkerRule.enabled},
					{"target_workers_per_stash", m_automation.stashWorkerRule.targetWorkersPerStash}
				})},
				{"attack", nlohmann::json::object({
					{"enabled", m_automation.attackRule.enabled},
					{"min_units", m_automation.attackRule.minUnits},
					{"group_size", m_automation.attackRule.groupSize},
					{"distance", m_automation.attackRule.distance}
				})},
				{"capture", nlohmann::json::object({
					{"enabled", m_automation.captureRule.enabled},
					{"max_concurrent", m_automation.captureRule.maxConcurrent},
					{"prefer_idle", m_automation.captureRule.preferIdle}
				})},
				{"radar_van", nlohmann::json::object({
					{"enabled", m_automation.radarVanRule.enabled},
					{"min_count", m_automation.radarVanRule.minCount}
				})}
			});

			result["last_decision"] = nlohmann::json::object({
				{"category", m_autonomy.state.lastDecisionCategory},
				{"command", m_autonomy.state.lastDecisionCommand},
				{"reason", m_autonomy.state.lastDecisionReason}
			});

			return result;
		}

		nlohmann::json buildAutonomyTelemetry()
		{
			nlohmann::json result = buildAutonomyStatus();
			result["telemetry_tick"] = static_cast<UnsignedInt>(::GetTickCount());
			result["zones"] = m_autonomy.state.telemetryZones.is_array() ? m_autonomy.state.telemetryZones : nlohmann::json::array();
			result["recent_events"] = m_autonomy.state.telemetryEvents.is_array() ? m_autonomy.state.telemetryEvents : nlohmann::json::array();
			if (m_autonomy.state.telemetryZones.is_array())
			{
				Int currentZoneCount = 0;
				Int developedZoneCount = 0;
				for (const auto& zone : m_autonomy.state.telemetryZones)
				{
					if (!zone.is_object())
					{
						continue;
					}
					const Int supplyStashes = zone.value("supply_stashes", 0);
					if (supplyStashes > 0)
					{
						++currentZoneCount;
					}
					if (zone.value("developed", false))
					{
						++developedZoneCount;
					}
				}
				result["current_zone_count"] = currentZoneCount;
				result["developed_zone_count"] = developedZoneCount;
			}
			const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
			if (profile == "sprawl" || profile == "sprawl_balanced")
			{
				const bool isBalancedSprawl = (profile == "sprawl_balanced");
				const Real sprawlMultiplier = std::max<Real>(0.5f, std::min<Real>(10.0f, m_autonomy.state.sprawlMultiplier));
				result["desired_zone_count"] = std::max<Int>(
					1,
					static_cast<Int>(std::floor((isBalancedSprawl ? 3.0f : 4.0f) * sprawlMultiplier)));
			}
			if (m_autonomy.state.telemetryZones.is_array() && !m_autonomy.state.telemetryZones.empty())
			{
				for (const auto& zone : m_autonomy.state.telemetryZones)
				{
					if (!zone.is_object() || !zone.value("is_main_base", false))
					{
						continue;
					}
					result["main_zone_anchor_id"] = zone.value("anchor_id", 0u);
					break;
				}
			}
			if (m_autonomy.state.telemetryZones.is_array() && m_autonomy.state.telemetryZones.size() >= 2u)
			{
				const auto* mainZone = static_cast<const nlohmann::json*>(nullptr);
				const auto* furthestZone = static_cast<const nlohmann::json*>(nullptr);
				Real furthestDistSq = -1.0f;
				for (const auto& zone : m_autonomy.state.telemetryZones)
				{
					if (zone.is_object() && zone.value("is_main_base", false))
					{
						mainZone = &zone;
						break;
					}
				}
				if (mainZone != nullptr)
				{
					const Real mainX = mainZone->value("center_x", 0.0f);
					const Real mainY = mainZone->value("center_y", 0.0f);
					for (const auto& zone : m_autonomy.state.telemetryZones)
					{
						if (!zone.is_object() || zone.value("is_main_base", false))
						{
							continue;
						}
						const Real dx = zone.value("center_x", 0.0f) - mainX;
						const Real dy = zone.value("center_y", 0.0f) - mainY;
						const Real distSq = (dx * dx) + (dy * dy);
						if (distSq > furthestDistSq)
						{
							furthestDistSq = distSq;
							furthestZone = &zone;
							result["sprawl_axis_dx"] = dx;
							result["sprawl_axis_dy"] = dy;
						}
					}
				}
				if (furthestZone != nullptr)
				{
					result["furthest_zone_anchor_id"] = furthestZone->value("anchor_id", 0u);
				}
			}
			return result;
		}

		#include "AIControlAdapterProtocol.inl"

		void evaluateAutomationRules()
		{
			evaluateAutonomyMacro();
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
			const auto it = m_automation.captureRule.pendingSourcesUntilTick.find(sourceId);
			if (it == m_automation.captureRule.pendingSourcesUntilTick.end())
			{
				return false;
			}
			const DWORD now = ::GetTickCount();
			return AIControlAdapterIsTickInFuture(it->second, now);
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
			if (m_automation.captureRule.pendingTargetsUntilTick.empty())
			{
				// fall through and still prune pending sources
			}
			const DWORD now = ::GetTickCount();
			for (auto it = m_automation.captureRule.pendingTargetsUntilTick.begin(); it != m_automation.captureRule.pendingTargetsUntilTick.end(); )
			{
				if (AIControlAdapterHasTickElapsed(it->second, now))
				{
					it = m_automation.captureRule.pendingTargetsUntilTick.erase(it);
				}
				else
				{
					++it;
				}
			}
			for (auto it = m_automation.captureRule.pendingSourcesUntilTick.begin(); it != m_automation.captureRule.pendingSourcesUntilTick.end(); )
			{
				if (AIControlAdapterHasTickElapsed(it->second, now))
				{
					it = m_automation.captureRule.pendingSourcesUntilTick.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		void evaluateWorkerAutomationRule()
		{
			if (!m_automation.workerRule.enabled)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			if (AIControlAdapterIsTickInFuture(m_automation.workerRule.nextAllowedTick, now))
			{
				return;
			}

			Player* player = nullptr;
			if (m_automation.workerRule.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_automation.workerRule.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}

			if (player == nullptr)
			{
				m_automation.workerRule.nextAllowedTick = now + m_automation.workerRule.cooldownMs;
				adapterLog("automation_worker_rule_skip reason=player_not_found");
				return;
			}

			refreshOwnedObjectCache(player);
			const Int idleWorkers = m_cache.idleWorkersTotal;
			if (idleWorkers >= m_automation.workerRule.minIdleWorkers)
			{
				return;
			}

			nlohmann::json args = nlohmann::json::object();
			args["count"] = m_automation.workerRule.queueCount;
			if (m_automation.workerRule.hasExplicitProducerKind && !m_automation.workerRule.producerKind.empty())
			{
				args["producer_kind"] = m_automation.workerRule.producerKind;
			}
			if (m_automation.workerRule.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_automation.workerRule.playerIndex;
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
			m_automation.workerRule.nextAllowedTick = now + m_automation.workerRule.cooldownMs;
			adapterLog(
				"automation_worker_rule request_id=%s player=%d idle_workers=%d min_idle_workers=%d queue_count=%d producer_kind=%s ok=%d reason=%s",
				requestIdBuffer,
				player->getPlayerIndex(),
				idleWorkers,
				m_automation.workerRule.minIdleWorkers,
				m_automation.workerRule.queueCount,
				m_automation.workerRule.hasExplicitProducerKind ? m_automation.workerRule.producerKind.c_str() : "default",
				ok ? 1 : 0,
				ok ? "" : reason.c_str());

			if (ok)
			{
				m_cache.valid = false;
			}
		}

		void evaluateStashWorkerAutomationRule()
		{
			if (!m_automation.stashWorkerRule.enabled)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			if (AIControlAdapterIsTickInFuture(m_automation.stashWorkerRule.nextAllowedTick, now))
			{
				return;
			}

			Player* player = nullptr;
			if (m_automation.stashWorkerRule.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_automation.stashWorkerRule.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}
			if (player == nullptr)
			{
				m_automation.stashWorkerRule.nextAllowedTick = now + m_automation.stashWorkerRule.cooldownMs;
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
				m_automation.stashWorkerRule.nextAllowedTick = now + m_automation.stashWorkerRule.cooldownMs;
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
				if (m_automation.stashWorkerRule.servicedStashIds.find(stashId) != m_automation.stashWorkerRule.servicedStashIds.end())
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
				{"count", m_automation.stashWorkerRule.targetWorkersPerStash},
				{"producer_kind", "supply_stash"},
				{"producer_object_id", static_cast<Int>(targetStash->getID())}
			};
			if (m_automation.stashWorkerRule.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_automation.stashWorkerRule.playerIndex;
			}

			nlohmann::json message = {
				{"type", "SessionCommand"},
				{"request_id", std::string(requestIdBuffer)},
				{"cmd", "Game.BuildWorker"},
				{"args", args}
			};

			std::string reason;
			const bool ok = executeGameBuildWorker(message, reason);
			m_automation.stashWorkerRule.nextAllowedTick = now + m_automation.stashWorkerRule.cooldownMs;
			if (ok)
			{
				m_automation.stashWorkerRule.servicedStashIds.insert(static_cast<Int>(targetStash->getID()));
			}
			adapterLog(
				"automation_stash_worker_rule request_id=%s player=%d stash_id=%d target_workers=%d serviced=%d ok=%d reason=%s",
				requestIdBuffer,
				player->getPlayerIndex(),
				static_cast<Int>(targetStash->getID()),
				m_automation.stashWorkerRule.targetWorkersPerStash,
				ok ? 1 : 0,
				ok ? 1 : 0,
				ok ? "" : reason.c_str());
		}

		void evaluateAttackAutomationRule()
		{
			if (!m_automation.attackRule.enabled)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			if (AIControlAdapterIsTickInFuture(m_automation.attackRule.nextAllowedTick, now))
			{
				return;
			}

			Player* player = nullptr;
			if (m_automation.attackRule.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_automation.attackRule.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}

			if (player == nullptr)
			{
				m_automation.attackRule.nextAllowedTick = now + m_automation.attackRule.cooldownMs;
				adapterLog("automation_attack_rule_skip reason=player_not_found");
				return;
			}

			std::vector<Object*> combatUnits;
			collectCombatUnitsForRaid(player, combatUnits);
			const Int combatUnitCount = static_cast<Int>(combatUnits.size());
			if (combatUnitCount < m_automation.attackRule.minUnits)
			{
				return;
			}

			nlohmann::json args = nlohmann::json::object({
				{"min_units", m_automation.attackRule.minUnits},
				{"group_size", m_automation.attackRule.groupSize},
				{"distance", m_automation.attackRule.distance}
			});
			if (m_automation.attackRule.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_automation.attackRule.playerIndex;
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
			m_automation.attackRule.nextAllowedTick = now + m_automation.attackRule.cooldownMs;
			adapterLog(
				"automation_attack_rule request_id=%s player=%d combat_units=%d min_units=%d group_size=%d distance=%.1f ok=%d reason=%s",
				requestIdBuffer,
				player->getPlayerIndex(),
				combatUnitCount,
				m_automation.attackRule.minUnits,
				m_automation.attackRule.groupSize,
				static_cast<double>(m_automation.attackRule.distance),
				ok ? 1 : 0,
				ok ? "" : reason.c_str());
		}

		void evaluateRadarVanAutomationRule()
		{
			if (!m_automation.radarVanRule.enabled)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			if (AIControlAdapterIsTickInFuture(m_automation.radarVanRule.nextAllowedTick, now))
			{
				return;
			}

			Player* player = nullptr;
			if (m_automation.radarVanRule.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_automation.radarVanRule.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}
			if (player == nullptr)
			{
				m_automation.radarVanRule.nextAllowedTick = now + m_automation.radarVanRule.cooldownMs;
				adapterLog("automation_radar_van_rule_skip reason=player_not_found");
				return;
			}

			struct RadarVanCountContext
			{
				Int supplyStashCount;
				Int barracksCount;
				Int blackMarketCount;
				Int armsCount;
				Int radarVanCount;
				Int combatVehicleCount;
			} counts = { 0, 0, 0, 0, 0, 0 };
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
				if (containsIgnoreCase(name, "supplystash") || containsIgnoreCase(name, "supplycenter"))
				{
					++counts->supplyStashCount;
				}
				if (containsIgnoreCase(name, "barracks"))
				{
					++counts->barracksCount;
				}
				if (containsIgnoreCase(name, "black") && containsIgnoreCase(name, "market"))
				{
					++counts->blackMarketCount;
				}
				if (containsIgnoreCase(name, "armsdealer"))
				{
					++counts->armsCount;
				}
				if (containsIgnoreCase(name, "radarvan"))
				{
					++counts->radarVanCount;
				}
				if (containsIgnoreCase(name, "quad")
					|| containsIgnoreCase(name, "scorpion")
					|| containsIgnoreCase(name, "scudlauncher"))
				{
					++counts->combatVehicleCount;
				}
			}, &counts);
			const Int supplyStashCount = counts.supplyStashCount;
			const Int barracksCount = counts.barracksCount;
			const Int blackMarketCount = counts.blackMarketCount;
			const Int armsCount = counts.armsCount;
			const Int radarVanCount = counts.radarVanCount;
			const Int combatVehicleCount = counts.combatVehicleCount;
			const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
			const bool isBalancedSprawl = (profile == "sprawl_balanced");
			const UnsignedInt reserveCash = isBalancedSprawl ? 10000u : 0u;
			const bool openingInfrastructureReady = supplyStashCount >= 1 && barracksCount >= 1 && armsCount >= 1;
			const bool openingEconomyReady = supplyStashCount >= 2 || blackMarketCount >= 1;
			const bool wasRecoveringFromReserve =
				(m_autonomy.state.lastDecisionCategory == "production" && m_autonomy.state.lastDecisionReason == "reserve_cash_recovery");
			const bool shouldPauseCombatProduction = AIControlAdapterShouldPauseCombatProduction({
				isBalancedSprawl,
				openingInfrastructureReady,
				openingEconomyReady,
				wasRecoveringFromReserve,
				player->getMoney()->countMoney(),
				reserveCash,
				0,
				0
			});
			const bool wasArmyCapReached =
				(m_autonomy.state.lastDecisionCategory == "production" && m_autonomy.state.lastDecisionReason == "army_cap_reached");
			const bool shouldHoldArmyCap = AIControlAdapterShouldHoldArmyCap({
				shouldPauseCombatProduction,
				isBalancedSprawl,
				wasArmyCapReached,
				combatVehicleCount,
				isBalancedSprawl ? 100 : 9999
			});

			if (!AIControlAdapterShouldQueueRadarVan({
				shouldPauseCombatProduction,
				shouldHoldArmyCap,
				armsCount,
				radarVanCount,
				combatVehicleCount,
				m_automation.radarVanRule.minCount
			}))
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
			if (m_automation.radarVanRule.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_automation.radarVanRule.playerIndex;
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
				if (m_automation.radarVanRule.hasExplicitPlayerIndex)
				{
					fallbackMessage["args"]["player_index"] = m_automation.radarVanRule.playerIndex;
				}
				ok = executeGameQueueRadarVansAllWarFactories(fallbackMessage, reason);
			}

			m_automation.radarVanRule.nextAllowedTick = now + m_automation.radarVanRule.cooldownMs;
			adapterLog(
				"automation_radar_van_rule request_id=%s player=%d arms=%d radar_vans=%d min_count=%d ok=%d reason=%s",
				requestIdBuffer,
				player->getPlayerIndex(),
				armsCount,
				radarVanCount,
				m_automation.radarVanRule.minCount,
				ok ? 1 : 0,
				ok ? "" : reason.c_str());
		}

		void evaluateCaptureAutomationRule()
		{
			if (!m_automation.captureRule.enabled)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			pruneCaptureAutomationPendingTargets();
			if (AIControlAdapterIsTickInFuture(m_automation.captureRule.nextAllowedTick, now))
			{
				return;
			}

			Player* player = nullptr;
			if (m_automation.captureRule.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_automation.captureRule.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}

			if (player == nullptr)
			{
				m_automation.captureRule.nextAllowedTick = now + m_automation.captureRule.cooldownMs;
				adapterLog("automation_capture_rule_skip reason=player_not_found");
				return;
			}

			const Int pendingCount = static_cast<Int>(m_automation.captureRule.pendingTargetsUntilTick.size());
			if (pendingCount >= m_automation.captureRule.maxConcurrent)
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
			collectCaptureSourcesForPlayer(player, m_automation.captureRule.preferIdle, sources);
			if (sources.empty())
			{
				m_automation.captureRule.nextAllowedTick = now + m_automation.captureRule.cooldownMs;
				adapterLog(
					"automation_capture_rule_skip player=%d reason=no_capture_sources pending=%d max_concurrent=%d prefer_idle=%d",
					player->getPlayerIndex(),
					pendingCount,
					m_automation.captureRule.maxConcurrent,
					m_automation.captureRule.preferIdle ? 1 : 0);
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

			Int assignmentsRemaining = std::max<Int>(0, m_automation.captureRule.maxConcurrent - pendingCount);
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
				if (m_automation.captureRule.pendingTargetsUntilTick.find(targetId) != m_automation.captureRule.pendingTargetsUntilTick.end())
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
				if (m_automation.captureRule.hasExplicitPlayerIndex)
				{
					args["player_index"] = m_automation.captureRule.playerIndex;
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
					m_automation.captureRule.maxConcurrent,
					ok ? 1 : 0,
					ok ? "" : reason.c_str());
				if (!ok)
				{
					continue;
				}
				recordAutonomyTelemetryEvent(
					"capture",
					"Game.CaptureBuilding",
					"ok",
					target->getPosition());

				++sentOk;
				--assignmentsRemaining;
				const DWORD pendingUntil = now + std::max<DWORD>(m_automation.captureRule.cooldownMs, 20000u);
				const Int sourceId = static_cast<Int>(selectedSource->getID());
				m_automation.captureRule.pendingTargetsUntilTick[targetId] = pendingUntil;
				if (sourceId > 0)
				{
					m_automation.captureRule.pendingSourcesUntilTick[sourceId] = pendingUntil;
				}
				sources.erase(std::remove(sources.begin(), sources.end(), selectedSource), sources.end());
			}

			if (sentOk > 0)
			{
				m_automation.captureRule.nextAllowedTick = now + m_automation.captureRule.cooldownMs;
			}
			else if (!lastReason.empty())
			{
				m_automation.captureRule.nextAllowedTick = now + std::max<DWORD>(m_automation.captureRule.cooldownMs, 2000u);
				adapterLog(
					"automation_capture_rule_skip player=%d reason=%s pending=%d max_concurrent=%d",
					player->getPlayerIndex(),
					lastReason.c_str(),
					pendingCount,
					m_automation.captureRule.maxConcurrent);
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

			m_automation.workerRule = rule;
			adapterLog(
				"automation_worker_rule_configured enabled=%d player=%d explicit_player=%d min_idle_workers=%d queue_count=%d producer_kind=%s cooldown_ms=%lu",
				m_automation.workerRule.enabled ? 1 : 0,
				m_automation.workerRule.playerIndex,
				m_automation.workerRule.hasExplicitPlayerIndex ? 1 : 0,
				m_automation.workerRule.minIdleWorkers,
				m_automation.workerRule.queueCount,
				m_automation.workerRule.hasExplicitProducerKind ? m_automation.workerRule.producerKind.c_str() : "default",
				static_cast<unsigned long>(m_automation.workerRule.cooldownMs));
			return true;
		}

		void clearWorkerAutomationRule()
		{
			m_automation.workerRule.enabled = false;
			m_automation.workerRule.hasExplicitPlayerIndex = false;
			m_automation.workerRule.hasExplicitProducerKind = false;
			m_automation.workerRule.playerIndex = -1;
			m_automation.workerRule.minIdleWorkers = 0;
			m_automation.workerRule.queueCount = 1;
			m_automation.workerRule.producerKind.clear();
			m_automation.workerRule.cooldownMs = 3000u;
			m_automation.workerRule.nextAllowedTick = 0u;
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

			m_automation.attackRule = rule;
			adapterLog(
				"automation_attack_rule_configured enabled=%d player=%d explicit_player=%d min_units=%d group_size=%d distance=%.1f cooldown_ms=%lu",
				m_automation.attackRule.enabled ? 1 : 0,
				m_automation.attackRule.playerIndex,
				m_automation.attackRule.hasExplicitPlayerIndex ? 1 : 0,
				m_automation.attackRule.minUnits,
				m_automation.attackRule.groupSize,
				static_cast<double>(m_automation.attackRule.distance),
				static_cast<unsigned long>(m_automation.attackRule.cooldownMs));
			return true;
		}

		void clearAttackAutomationRule()
		{
			m_automation.attackRule.enabled = false;
			m_automation.attackRule.hasExplicitPlayerIndex = false;
			m_automation.attackRule.playerIndex = -1;
			m_automation.attackRule.minUnits = 40;
			m_automation.attackRule.groupSize = 30;
			m_automation.attackRule.distance = 3000.0f;
			m_automation.attackRule.cooldownMs = 15000u;
			m_automation.attackRule.nextAllowedTick = 0u;
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

			m_automation.captureRule = rule;
			adapterLog(
				"automation_capture_rule_configured enabled=%d player=%d explicit_player=%d max_concurrent=%d prefer_idle=%d cooldown_ms=%lu",
				m_automation.captureRule.enabled ? 1 : 0,
				m_automation.captureRule.playerIndex,
				m_automation.captureRule.hasExplicitPlayerIndex ? 1 : 0,
				m_automation.captureRule.maxConcurrent,
				m_automation.captureRule.preferIdle ? 1 : 0,
				static_cast<unsigned long>(m_automation.captureRule.cooldownMs));
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

			m_automation.radarVanRule = rule;
			adapterLog(
				"automation_radar_van_rule_configured enabled=%d player=%d explicit_player=%d min_count=%d cooldown_ms=%lu",
				m_automation.radarVanRule.enabled ? 1 : 0,
				m_automation.radarVanRule.playerIndex,
				m_automation.radarVanRule.hasExplicitPlayerIndex ? 1 : 0,
				m_automation.radarVanRule.minCount,
				static_cast<unsigned long>(m_automation.radarVanRule.cooldownMs));
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

			m_automation.stashWorkerRule = rule;
			adapterLog(
				"automation_stash_worker_rule_configured enabled=%d player=%d explicit_player=%d target_workers_per_stash=%d cooldown_ms=%lu",
				m_automation.stashWorkerRule.enabled ? 1 : 0,
				m_automation.stashWorkerRule.playerIndex,
				m_automation.stashWorkerRule.hasExplicitPlayerIndex ? 1 : 0,
				m_automation.stashWorkerRule.targetWorkersPerStash,
				static_cast<unsigned long>(m_automation.stashWorkerRule.cooldownMs));
			return true;
		}

		void clearStashWorkerAutomationRule()
		{
			m_automation.stashWorkerRule.enabled = false;
			m_automation.stashWorkerRule.hasExplicitPlayerIndex = false;
			m_automation.stashWorkerRule.playerIndex = -1;
			m_automation.stashWorkerRule.targetWorkersPerStash = 9;
			m_automation.stashWorkerRule.cooldownMs = 4000u;
			m_automation.stashWorkerRule.nextAllowedTick = 0u;
			m_automation.stashWorkerRule.servicedStashIds.clear();
			adapterLog("automation_stash_worker_rule_cleared");
		}

		void clearRadarVanAutomationRule()
		{
			m_automation.radarVanRule.enabled = false;
			m_automation.radarVanRule.hasExplicitPlayerIndex = false;
			m_automation.radarVanRule.playerIndex = -1;
			m_automation.radarVanRule.minCount = 1;
			m_automation.radarVanRule.cooldownMs = 12000u;
			m_automation.radarVanRule.nextAllowedTick = 0u;
			adapterLog("automation_radar_van_rule_cleared");
		}

		void clearCaptureAutomationRule()
		{
			m_automation.captureRule.enabled = false;
			m_automation.captureRule.hasExplicitPlayerIndex = false;
			m_automation.captureRule.preferIdle = true;
			m_automation.captureRule.playerIndex = -1;
			m_automation.captureRule.maxConcurrent = 3;
			m_automation.captureRule.cooldownMs = 4000u;
			m_automation.captureRule.nextAllowedTick = 0u;
			m_automation.captureRule.pendingTargetsUntilTick.clear();
			m_automation.captureRule.pendingSourcesUntilTick.clear();
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

		#include "AIControlAdapterSkirmish.inl"

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



