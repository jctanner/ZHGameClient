#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapter.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCaptureManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCombatTask.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCounterbatteryManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTechManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterProductionManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterZoneManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterDefenseManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterEconomyManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterEnemyMemory.h"
#include "GameClient/AIControlAdapter/AIControlAdapterGarrisonManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterGlaUsaStrategyManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildDispatcher.h"
#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildSnapshotBuilder.h"
#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildTelemetrySerializer.h"
#include "GameClient/AIControlAdapter/AIControlAdapterProfilePolicyManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterRaidManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterScheduler.h"
#include "GameClient/AIControlAdapter/AIControlAdapterScudStormManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterScoutingManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterStrategicFoundationSurvivalManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendSnapshotBuilder.h"
#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendTelemetrySerializer.h"
#include "GameClient/AIControlAdapter/AIControlAdapterSurvivalPolicyManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTaskReservation.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTemplateInferenceService.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTerrainMapCacheService.h"
#include "GameClient/AIControlAdapter/AIControlAdapterUiUtils.h"
#include "GameClient/AIControlAdapter/AIControlAdapterWMDTarget.h"
#include "GameClient/AIControlAdapter/AIControlAdapterWorkerShuttleManager.h"

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
#include "Common/GlobalData.h"
#include "Common/KindOf.h"
#include "Common/MessageStream.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
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
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

extern void skirmishUpdateSlotList();

#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <iterator>
#include <cstdarg>
#include <cstdio>
#include <share.h>

namespace
{
	static const char* const kAIControlAdapterBuildMarker = "codex-2026-05-10-capture-scud-upgrade-v2";

	// GLA science purchase plan (priority order)
	// See projects/docs/gla-science-analysis.md for full prerequisite details
	//
	// Strategy: Purchase ScudLauncher at Rank 1, then save all points for Cash Bounty chain at Rank 3+
	// Note: Rank 2 point will be intentionally unspent (saving for Cash Bounty)
	static const char* const kAutonomySciencePlan[] = {
		// Rank 1 sciences (prerequisite: SCIENCE_GLA + SCIENCE_Rank1)
		"SCIENCE_ScudLauncher",      // Unlocks Scud Launcher vehicle

		// Rank 3 sciences (prerequisite: SCIENCE_GLA + SCIENCE_Rank3)
		"SCIENCE_CashBounty1",       // Cash Bounty level 1
		"SCIENCE_CashBounty2",       // Requires CashBounty1 + Rank3
		"SCIENCE_CashBounty3"        // Requires CashBounty2 + Rank3

		// Skipped sciences (not currently prioritized):
		// - SCIENCE_MarauderTank (Rank 1) - Unlocks Marauder Tank
		// - SCIENCE_TechnicalTraining (Rank 1) - Technical Training upgrade
		// - SCIENCE_Hijacker (Rank 3) - Unlocks Hijacker infantry
		// - SCIENCE_RebelAmbush1/2/3 (Rank 3+) - Rebel Ambush chain
		// - SCIENCE_EmergencyRepair1/2/3 (Rank 3+) - Emergency Repair chain
		// - SCIENCE_AnthraxBomb (Rank 5) - Anthrax Bomb special power
		// - SCIENCE_SneakAttack (Rank 5) - Sneak Attack special power
	};

	struct AutonomyUpgradePlanEntry
	{
		const char* producerKind;
		const char* upgradeName;
	};

	static const AutonomyUpgradePlanEntry kAutonomyUpgradePlan[] = {
		{ "arms_dealer", "Upgrade_GLAScorpionRocket" },
		{ "barracks", "Upgrade_InfantryCaptureBuilding" },
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

	static Real clampUnitFloat(Real value, Real minimumValue, Real maximumValue)
	{
		return std::max(minimumValue, std::min(maximumValue, value));
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
		bool disabledLogEmitted;
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

	struct AutonomyTrackedObjectHealth
	{
		Real health;
		DWORD tick;
	};

	struct AutonomyZoneThreatState
	{
		DWORD lastSeenTick;
		UnsignedInt damagedObjectId;
		Real positionX;
		Real positionY;
		Real damageDelta;
		std::string level;
		std::string sourceType;
		std::string response;
		std::string reason;
		int localEnemyCount = 0;
		int enemyArtilleryCount = 0;
		bool recentWmd = false;
		int damagedStructures = 0;
		int destroyedStructures = 0;
	};

	struct AutonomyZoneDefenseAllocation
	{
		UnsignedInt zoneAnchorId = 0;
		std::string threatLevel;
		int threatSeverity = 0;
		Real targetX = 0.0f;
		Real targetY = 0.0f;
		std::vector<unsigned int> assignedUnitIds;
		std::vector<UnsignedInt> donorZoneIds;
		UnsignedInt taskId = 0;
		std::vector<UnsignedInt> taskIds;
		DWORD createdTick = 0u;
		DWORD lastCommandTick = 0u;
		DWORD holdUntilTick = 0u;
		DWORD expiryTick = 0u;
		std::string state;
	};

	struct AutonomyZoneDefenseReserveState
	{
		std::string posture = "interior";
		std::string reason = "not_evaluated";
		DWORD lastThreatTick = 0;
		int recentAttackCount = 0;
		int resident = 0;
		int floor = 0;
		int surplus = 0;
		int deficit = 0;
		bool activeThreat = false;
		bool productionNeeded = false;
		bool donorAllowed = false;
	};

	struct AutonomyStrategicFoundationState
	{
		std::string templateName;
		Real lastHealth = -1.0f;
		Real x = 0.0f;
		Real y = 0.0f;
		Real z = 0.0f;
		DWORD firstSeenTick = 0u;
		DWORD lastSeenTick = 0u;
		DWORD lastProgressTick = 0u;
		DWORD lastRecoveryTick = 0u;
		int recoveryAttempts = 0;
		bool stopIssued = false;
		std::string reason;
	};

	struct AutonomyGarrisonInfantryAssignment
	{
		UnsignedInt unitId = 0;
		std::string templateName;
		Real lastX = 0.0f;
		Real lastY = 0.0f;
		Real lastDistance = -1.0f;
		bool entered = false;
		bool enteredPendingVerification = false;
		bool outside = false;
		bool nearby = false;
		DWORD assignedTick = 0u;
		DWORD lastProgressTick = 0u;
		DWORD lastCommandTick = 0u;
		std::string state = "assigned";
		std::string reason = "assigned";
	};

	struct AutonomyGarrisonAssignment
	{
		UnsignedInt structureId = 0;
		UnsignedInt zoneAnchorId = 0;
		std::string templateName;
		Real x = 0.0f;
		Real y = 0.0f;
		int desiredInfantry = 0;
		int estimatedCapacity = 0;
		std::vector<unsigned int> infantryIds;
		std::vector<AutonomyGarrisonInfantryAssignment> infantry;
		UnsignedInt taskId = 0;
		DWORD assignedTick = 0u;
		DWORD lastProgressTick = 0u;
		DWORD lastCommandTick = 0u;
		std::string state;
		std::string reason;
		std::string releaseReason;
	};

	struct AutonomyWorkerShuttleAssignment
	{
		UnsignedInt taskId = 0;
		UnsignedInt workerId = 0;
		UnsignedInt technicalId = 0;
		std::string templateName;
		Coord3D targetPosition;
		DWORD createdTick = 0u;
		DWORD lastCommandTick = 0u;
		DWORD lastProgressTick = 0u;
		int commandReissueCount = 0;
		bool constructReissued = false;
		std::string state = "assigned";
		std::string reason = "assigned";
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
		bool hasUrgentZoneGapThresholdOverride;
		Int urgentZoneGapThresholdOverride;
		bool hasMaxConcurrentExpansionStashesOverride;
		Int maxConcurrentExpansionStashesOverride;
		bool hasAllowExpansionBeforeFullRemoteFollowupOverride;
		bool allowExpansionBeforeFullRemoteFollowupOverride;
		bool hasExpansionHighCashFloatThresholdOverride;
		UnsignedInt expansionHighCashFloatThresholdOverride;
		bool debugDrawEnabled;
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
		DWORD nextLocalWorkerLiquidityTick;
		DWORD nextGarrisonTick;
		DWORD nextScoutTick;
		UnsignedInt lastScoutRandomObjectiveId;
		Real lastScoutRandomObjectiveX;
		Real lastScoutRandomObjectiveY;
		DWORD lastScoutRevealTick;
		std::string lastScoutRandomDirection;
		std::unordered_map<std::string, DWORD> scoutReservationLogTickByKey;
		DWORD nextCounterbatteryTick;
		DWORD nextCounterbatteryProductionTick;
		std::size_t nextZoneIndex;
		bool hasLastZone;
		UnsignedInt lastZoneAnchorId;
		bool lastZoneIsMainBase;
		Real lastZoneCenterX;
		Real lastZoneCenterY;
		std::string lastDecisionCategory;
		std::string lastDecisionCommand;
		std::string lastDecisionReason;
		bool telemetryZonesDirty;
		UnsignedInt zonesSnapshotVersion;
		DWORD zonesSnapshotTick;
		bool hasCashRateSample;
		UnsignedInt lastCashRateMoney;
		DWORD lastCashRateTick;
		Real smoothedNetCashPerMinute;
		nlohmann::json telemetryZones;
		nlohmann::json telemetryEvents;
		std::unordered_map<UnsignedInt, AutonomyTrackedObjectHealth> trackedStructureHealth;
		std::unordered_map<UnsignedInt, AutonomyZoneThreatState> zoneThreats;
		std::unordered_map<UnsignedInt, AutonomyZoneDefenseAllocation> zoneDefenseAllocations;
		std::unordered_map<UnsignedInt, AutonomyZoneDefenseReserveState> zoneDefenseReserves;
		std::unordered_map<UnsignedInt, AutonomyStrategicFoundationState> strategicFoundationHealth;
		std::unordered_map<UnsignedInt, AutonomyGarrisonAssignment> garrisonAssignments;
		std::unordered_map<UnsignedInt, AutonomyWorkerShuttleAssignment> workerShuttleAssignments;

		nlohmann::json zoneThreatTelemetry;
		nlohmann::json staticDefenseTelemetry;
		nlohmann::json palaceRedundancyTelemetry;
		nlohmann::json garrisonTelemetry;
		nlohmann::json scoutingTelemetry;
		nlohmann::json counterbatteryTelemetry;
		nlohmann::json brutalPressureTelemetry;
		nlohmann::json emergencySurvivalTelemetry;
		nlohmann::json survivalPolicyTelemetry;
		nlohmann::json glaUsaStrategyTelemetry;
		nlohmann::json strategicSpendTelemetry;
		nlohmann::json macroBuildTelemetry;
		nlohmann::json mainBaseCriticalOverrideTelemetry;
		nlohmann::json zoneDefenseReserveTelemetry;
		nlohmann::json pathingTelemetry;
		DWORD matchOutcomeStartTick = 0u;
		std::string loggedTerminalMatchOutcomeState;
		nlohmann::json lastActiveMatchOutcome;
		nlohmann::json lastTerminalMatchOutcome;
		nlohmann::json durableUnknownMatchOutcome;
		nlohmann::json lastActiveMatchSnapshot;
		nlohmann::json finalDiagnosticSnapshot;
		nlohmann::json finalRunSummary;
		nlohmann::json finalMatchOutcome;
		std::string activeMatchIdentity;
		std::string loggedFinalSnapshotIdentity;

		// Phase 7: Zone Defense Response state
		UnsignedInt lastDefendedZoneAnchor = 0;
		DWORD lastDefenseResponseTick = 0;
		int lastDefendedThreatSeverity = 0;

		// Phase 5.8: Pending foothold zone anchors
		struct PendingFootholdAnchor
		{
			UnsignedInt syntheticAnchorId;
			ZoneAnchorType anchorType;
			Real targetX;
			Real targetY;
			DWORD createdTick;
			std::string reason;
			bool buildIssued;
			std::string buildCommand;

			PendingFootholdAnchor()
				: syntheticAnchorId(0)
				, anchorType(ZoneAnchorType::StrategicFoothold)
				, targetX(0.0f)
				, targetY(0.0f)
				, createdTick(0)
				, buildIssued(false)
			{}
		};
		std::vector<PendingFootholdAnchor> pendingFootholdAnchors;
		UnsignedInt nextSyntheticAnchorId = 1000000;  // Start at 1M to avoid conflicts with real object IDs
	};

	#include "AIControlAdapterLog.inl"

	#include "AIControlAdapterTransport.inl"

	#include "AIControlAdapterObjectCache.inl"

	#include "AIControlAdapterWorkersState.inl"

	#include "AIControlAdapterAutomation.inl"

	#include "AIControlAdapterAutonomy.inl"

	#include "AIControlAdapterSchedulerIntegration.inl"

	class AIControlAdapterState
	{
	public:
		AIControlAdapterState() :
			m_transport(&m_log)
		{
			m_transport.setMessageHandler(&AIControlAdapterState::onMessage, this);
			m_log.resetFile(true);
			adapterLog(
				"adapter_start pid=%lu session=%s build=%s",
				static_cast<unsigned long>(::GetCurrentProcessId()),
				m_log.sessionId().c_str(),
				kAIControlAdapterBuildMarker);
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
			m_terrainMapCacheService.clear();
			m_mapFileTerrainMergeLoggedKeys.clear();
			m_transport.resetClientConnection();
		}

		void update()
		{
			m_transport.ensurePipeCreated();
			m_transport.ensureTcpCreated();
			if (!m_transport.hasListener())
			{
				return;
			}

			m_transport.acceptClientIfAvailable();
			if (m_transport.hasClient())
			{
				m_transport.readIncomingData();
			}

			// Autonomy should run even when no client is connected
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
		AIControlAdapterTerrainMapCacheService m_terrainMapCacheService;
		AIControlAdapterWorkerShuttleManager m_workerShuttleManager;
		std::set<std::string> m_mapFileTerrainMergeLoggedKeys;

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
			m_autonomy.state.hasUrgentZoneGapThresholdOverride = false;
			m_autonomy.state.urgentZoneGapThresholdOverride = 0;
			m_autonomy.state.hasMaxConcurrentExpansionStashesOverride = false;
			m_autonomy.state.maxConcurrentExpansionStashesOverride = 0;
			m_autonomy.state.hasAllowExpansionBeforeFullRemoteFollowupOverride = false;
			m_autonomy.state.allowExpansionBeforeFullRemoteFollowupOverride = false;
			m_autonomy.state.hasExpansionHighCashFloatThresholdOverride = false;
			m_autonomy.state.expansionHighCashFloatThresholdOverride = 0u;
			m_autonomy.state.debugDrawEnabled = false;
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
			m_autonomy.state.nextLocalWorkerLiquidityTick = 0u;
			m_autonomy.state.nextGarrisonTick = 0u;
			m_autonomy.state.nextScoutTick = 0u;
			m_autonomy.state.lastScoutRandomObjectiveId = 0u;
			m_autonomy.state.lastScoutRandomObjectiveX = 0.0f;
			m_autonomy.state.lastScoutRandomObjectiveY = 0.0f;
			m_autonomy.state.lastScoutRevealTick = 0u;
			m_autonomy.state.lastScoutRandomDirection.clear();
			m_autonomy.state.scoutReservationLogTickByKey.clear();
			m_autonomy.state.nextCounterbatteryTick = 0u;
			m_autonomy.state.nextCounterbatteryProductionTick = 0u;
			m_autonomy.state.nextZoneIndex = 0u;
			m_autonomy.state.hasLastZone = false;
			m_autonomy.state.lastZoneAnchorId = 0u;
			m_autonomy.state.lastZoneIsMainBase = false;
			m_autonomy.state.lastZoneCenterX = 0.0f;
			m_autonomy.state.lastZoneCenterY = 0.0f;
			m_autonomy.state.lastDecisionCategory.clear();
			m_autonomy.state.lastDecisionCommand.clear();
			m_autonomy.state.lastDecisionReason.clear();
			m_autonomy.state.telemetryZonesDirty = true;
			m_autonomy.state.zonesSnapshotVersion = 0u;
			m_autonomy.state.zonesSnapshotTick = 0u;
			m_autonomy.state.hasCashRateSample = false;
			m_autonomy.state.lastCashRateMoney = 0u;
			m_autonomy.state.lastCashRateTick = 0u;
			m_autonomy.state.smoothedNetCashPerMinute = 0.0f;
			m_autonomy.state.telemetryZones = nlohmann::json::array();
			m_autonomy.state.telemetryEvents = nlohmann::json::array();
			m_autonomy.state.trackedStructureHealth.clear();
			m_autonomy.state.zoneThreats.clear();
			m_autonomy.state.zoneDefenseAllocations.clear();
			m_autonomy.state.zoneDefenseReserves.clear();
			m_autonomy.state.strategicFoundationHealth.clear();
			m_autonomy.state.garrisonAssignments.clear();
			m_autonomy.state.zoneThreatTelemetry = nlohmann::json::array();
			m_autonomy.state.staticDefenseTelemetry = nlohmann::json::array();
			m_autonomy.state.palaceRedundancyTelemetry = nlohmann::json::array();
			m_autonomy.state.garrisonTelemetry = nlohmann::json::array();
			m_autonomy.state.scoutingTelemetry = nlohmann::json::object();
			m_autonomy.state.counterbatteryTelemetry = nlohmann::json::object();
			m_autonomy.state.brutalPressureTelemetry = nlohmann::json::object();
			m_autonomy.state.emergencySurvivalTelemetry = nlohmann::json::object();
			m_autonomy.state.survivalPolicyTelemetry = nlohmann::json::object();
			m_autonomy.state.glaUsaStrategyTelemetry = nlohmann::json::object();
			m_autonomy.state.strategicSpendTelemetry =
				AIControlAdapterStrategicSpendTelemetrySerializer().BuildDefaultTelemetry();
			m_autonomy.state.macroBuildTelemetry =
				AIControlAdapterMacroBuildTelemetrySerializer().BuildDefaultTelemetry();
			m_autonomy.state.mainBaseCriticalOverrideTelemetry = nlohmann::json::object({
				{"active", false},
				{"zone", 0},
				{"reason", "not_evaluated"},
				{"local_enemies", 0},
				{"recent_wmd", false},
				{"damaged_structures", 0},
				{"destroyed_structures", 0},
				{"assigned_units", 0},
				{"requested_units", 0}
			});
			m_autonomy.state.zoneDefenseReserveTelemetry = nlohmann::json::array();
			m_autonomy.state.pathingTelemetry = nlohmann::json::object();
			m_autonomy.state.lastActiveMatchOutcome = nlohmann::json();
			m_autonomy.state.lastTerminalMatchOutcome = nlohmann::json();
			m_autonomy.state.durableUnknownMatchOutcome = nlohmann::json();
			m_autonomy.state.lastActiveMatchSnapshot = nlohmann::json();
			m_autonomy.state.finalDiagnosticSnapshot = nlohmann::json();
			m_autonomy.state.finalRunSummary = nlohmann::json();
			m_autonomy.state.finalMatchOutcome = nlohmann::json();
			m_autonomy.state.loggedTerminalMatchOutcomeState.clear();
			m_autonomy.state.loggedFinalSnapshotIdentity.clear();
			m_autonomy.state.activeMatchIdentity.clear();
			if (m_autonomy.state.matchOutcomeStartTick == 0u)
			{
				m_autonomy.state.matchOutcomeStartTick = ::GetTickCount();
			}
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

		struct AutomationOwnedObjectSnapshot
		{
			Object* object;
			std::string name;
			bool isStructure;
			bool underConstruction;
			bool isSupplyStructure;
			bool isBarracks;
			bool isArmsDealer;
			bool isPalace;
			bool isBlackMarket;
			bool isScudStorm;
			bool isRadarVan;
			bool isTechnical;
			bool isQuad;
			bool isScorpion;
			bool isScudLauncher;
			bool isDozer;
			bool isHarvester;
			bool isInfantry;
			bool isVehicle;
			bool isAircraft;
			bool hasCapturePower;
			bool hasAI;
		};

		void collectOwnedAutomationObjects(Player* player, std::vector<AutomationOwnedObjectSnapshot>& outObjects) const
		{
			outObjects.clear();
			if (player == nullptr)
			{
				return;
			}

			outObjects.reserve(256);
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}

				std::vector<AutomationOwnedObjectSnapshot>* outObjects =
					static_cast<std::vector<AutomationOwnedObjectSnapshot>*>(userData);
				if (outObjects == nullptr)
				{
					return;
				}

				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				const bool isStructure = obj->isKindOf(KINDOF_STRUCTURE);
				const bool underConstruction = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
				const bool isArmsDealer = containsIgnoreCase(name, "armsdealer");

				outObjects->push_back(AutomationOwnedObjectSnapshot{
					obj,
					name,
					isStructure,
					underConstruction,
					containsIgnoreCase(name, "supplystash") || containsIgnoreCase(name, "supplycenter"),
					containsIgnoreCase(name, "barracks"),
					isArmsDealer,
					isPalaceTemplateName(name),
					containsIgnoreCase(name, "black") && containsIgnoreCase(name, "market"),
					containsIgnoreCase(name, "scud") && containsIgnoreCase(name, "storm"),
					containsIgnoreCase(name, "radarvan"),
					containsIgnoreCase(name, "technical"),
					containsIgnoreCase(name, "quad"),
					containsIgnoreCase(name, "scorpion"),
					containsIgnoreCase(name, "scudlauncher"),
					obj->isKindOf(KINDOF_DOZER),
					obj->isKindOf(KINDOF_HARVESTER),
					obj->isKindOf(KINDOF_INFANTRY),
					obj->isKindOf(KINDOF_VEHICLE),
					obj->isKindOf(KINDOF_AIRCRAFT),
					obj->hasSpecialPower(SPECIAL_INFANTRY_CAPTURE_BUILDING) || obj->hasSpecialPower(SPECIAL_BLACKLOTUS_CAPTURE_BUILDING),
					obj->getAI() != nullptr
				});
			}, &outObjects);
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

		AIControlAdapterProfilePolicyConfig resolveAutonomyProfilePolicyConfig() const
		{
			AIControlAdapterProfilePolicyRequest request;
			request.profile = m_autonomy.state.profile;
			request.economyBias = static_cast<float>(m_autonomy.state.economyBias);
			request.aggressionBias = static_cast<float>(m_autonomy.state.aggressionBias);
			request.defenseBias = static_cast<float>(m_autonomy.state.defenseBias);
			request.expansionBias = static_cast<float>(m_autonomy.state.expansionBias);
			request.sprawlMultiplier = static_cast<float>(m_autonomy.state.sprawlMultiplier);
			request.overrides.hasUrgentZoneGapThreshold = m_autonomy.state.hasUrgentZoneGapThresholdOverride;
			request.overrides.urgentZoneGapThreshold = m_autonomy.state.urgentZoneGapThresholdOverride;
			request.overrides.hasMaxConcurrentExpansionStashes = m_autonomy.state.hasMaxConcurrentExpansionStashesOverride;
			request.overrides.maxConcurrentExpansionStashes = m_autonomy.state.maxConcurrentExpansionStashesOverride;
			request.overrides.hasAllowExpansionBeforeFullRemoteFollowup = m_autonomy.state.hasAllowExpansionBeforeFullRemoteFollowupOverride;
			request.overrides.allowExpansionBeforeFullRemoteFollowup = m_autonomy.state.allowExpansionBeforeFullRemoteFollowupOverride;
			request.overrides.hasExpansionHighCashFloatThreshold = m_autonomy.state.hasExpansionHighCashFloatThresholdOverride;
			request.overrides.expansionHighCashFloatThreshold = m_autonomy.state.expansionHighCashFloatThresholdOverride;
			return AIControlAdapterProfilePolicyManager().Resolve(request);
		}

		void applyAutonomyRules()
		{
			if (!isAutonomyModeActive())
			{
				clearAutonomyManagedRules();
				m_autonomy.state.lastAppliedTick = ::GetTickCount();
				adapterLog("autonomy_rules_inactive mode=%s paused=%d", m_autonomy.state.mode.c_str(), m_autonomy.state.paused ? 1 : 0);
				return;
			}

			const std::string profile = AIControlAdapterUiUtils::NormalizeAsciiLower(m_autonomy.state.profile);
			const Real expansion = clampUnitFloat(m_autonomy.state.expansionBias, 0.0f, 1.0f);
			const AIControlAdapterProfilePolicyConfig policyConfig = resolveAutonomyProfilePolicyConfig();

			m_automation.workerRule.enabled = true;
			m_automation.workerRule.hasExplicitPlayerIndex = m_autonomy.state.hasExplicitPlayerIndex;
			m_automation.workerRule.playerIndex = m_autonomy.state.playerIndex;
			m_automation.workerRule.hasExplicitProducerKind = true;
			m_automation.workerRule.producerKind = "command_center";
			m_automation.workerRule.minIdleWorkers = policyConfig.workerMinIdle;
			m_automation.workerRule.queueCount = policyConfig.workerQueueCount;
			m_automation.workerRule.cooldownMs = policyConfig.workerCooldownMs;

			m_automation.stashWorkerRule.enabled = profile != "builtin_passthrough";
			m_automation.stashWorkerRule.hasExplicitPlayerIndex = m_autonomy.state.hasExplicitPlayerIndex;
			m_automation.stashWorkerRule.playerIndex = m_autonomy.state.playerIndex;
			m_automation.stashWorkerRule.targetWorkersPerStash = policyConfig.stashWorkersPerStash;
			m_automation.stashWorkerRule.cooldownMs = policyConfig.stashWorkerCooldownMs;

			const AIControlAdapterProfilePolicyManager profilePolicyManager;
			m_automation.radarVanRule.enabled = profilePolicyManager.AllowsRadarVanAutomation(policyConfig);
			m_automation.radarVanRule.hasExplicitPlayerIndex = m_autonomy.state.hasExplicitPlayerIndex;
			m_automation.radarVanRule.playerIndex = m_autonomy.state.playerIndex;
			m_automation.radarVanRule.minCount = profilePolicyManager.ResolveRadarVanMinCount(policyConfig);
			m_automation.radarVanRule.cooldownMs = 12000u;

			m_automation.attackRule.enabled = profile != "builtin_passthrough";
			if (m_autonomy.state.hasAttackAutomationEnabledOverride)
			{
				m_automation.attackRule.enabled = m_autonomy.state.attackAutomationEnabled && profile != "builtin_passthrough";
			}
			m_automation.attackRule.hasExplicitPlayerIndex = m_autonomy.state.hasExplicitPlayerIndex;
			m_automation.attackRule.playerIndex = m_autonomy.state.playerIndex;
			m_automation.attackRule.minUnits = policyConfig.attackMinUnits;
			m_automation.attackRule.groupSize = policyConfig.attackGroupSize;
			m_automation.attackRule.distance = 3000.0f + (expansion * 400.0f);
			m_automation.attackRule.cooldownMs = policyConfig.attackCooldownMs;

			const bool captureWasEnabled = m_automation.captureRule.enabled;
			m_automation.captureRule.enabled = m_autonomy.state.captureTech && profile != "builtin_passthrough";
			m_automation.captureRule.hasExplicitPlayerIndex = m_autonomy.state.hasExplicitPlayerIndex;
			m_automation.captureRule.playerIndex = m_autonomy.state.playerIndex;
			m_automation.captureRule.preferIdle = true;
			m_automation.captureRule.maxConcurrent = 4;
			m_automation.captureRule.cooldownMs = 4000u;
			if (m_automation.captureRule.enabled)
			{
				m_automation.captureRule.disabledLogEmitted = false;
			}
			else if (captureWasEnabled != m_automation.captureRule.enabled)
			{
				m_automation.captureRule.nextAllowedTick = 0u;
			}

			// Phase 9: Combat task maintenance
			const DWORD now = ::GetTickCount();
			m_autonomy.combatTaskManager.updateTasks(now);
			m_autonomy.combatTaskManager.pruneExpiredTasks(now);

			// Prune dead units from active combat tasks
			Player* player = nullptr;
			if (m_autonomy.state.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_autonomy.state.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}

			if (player != nullptr)
			{
				std::vector<CombatTask*> activeTasks = m_autonomy.combatTaskManager.findActiveTasks();
				for (CombatTask* task : activeTasks)
				{
					std::vector<unsigned int> deadUnitIds;
					for (unsigned int unitId : task->assignedUnitIds)
					{
						Object* obj = TheGameLogic != nullptr ? TheGameLogic->findObjectByID((ObjectID)unitId) : nullptr;
						if (obj == nullptr || obj->isEffectivelyDead())
						{
							deadUnitIds.push_back(unitId);
						}
					}

					if (!deadUnitIds.empty())
					{
						m_autonomy.combatTaskManager.removeDeadUnits(task->taskId, deadUnitIds);

						adapterLog(
							"combat_task_dead_units task=%u type=%s dead_count=%d remaining=%d min_viable=%d",
							task->taskId,
							task->type == CombatTaskType::Attack ? "attack" :
								task->type == CombatTaskType::Defense ? "defense" : "guard",
							static_cast<int>(deadUnitIds.size()),
							static_cast<int>(task->assignedUnitIds.size()),
							task->minimumViableCount);

						// Fail task if below minimum viable count
						if (static_cast<int>(task->assignedUnitIds.size()) < task->minimumViableCount)
						{
							m_autonomy.combatTaskManager.failTask(
								task->taskId,
								"casualties_below_minimum");

							adapterLog(
								"combat_task_failed task=%u type=%s reason=casualties_below_minimum remaining=%d min=%d",
								task->taskId,
								task->type == CombatTaskType::Attack ? "attack" :
									task->type == CombatTaskType::Defense ? "defense" : "guard",
								static_cast<int>(task->assignedUnitIds.size()),
								task->minimumViableCount);
						}
					}
				}
			}

			m_autonomy.state.lastAppliedTick = now;
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
			const AIControlAdapterAutonomyTickSchedule tickSchedule =
				AIControlAdapterScheduler::EvaluateAutonomyTickSchedule(
					now,
					m_autonomy.state.nextMacroTick,
					m_autonomy.state.nextProductionTick,
					m_autonomy.state.nextTechTick,
					m_autonomy.state.nextGuardTick);
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
				tickSchedule.macroDue ? 1 : 0,
				tickSchedule.productionDue ? 1 : 0,
				tickSchedule.techDue ? 1 : 0,
				tickSchedule.guardDue ? 1 : 0,
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
			const AIControlAdapterAutonomyTickSchedule tickSchedule =
				AIControlAdapterScheduler::EvaluateAutonomyTickSchedule(
					now,
					m_autonomy.state.nextMacroTick,
					m_autonomy.state.nextProductionTick,
					m_autonomy.state.nextTechTick,
					m_autonomy.state.nextGuardTick);
			const bool macroDue = tickSchedule.macroDue;
			const bool productionDue = tickSchedule.productionDue;
			const bool techDue = tickSchedule.techDue;
			if (!tickSchedule.anyDue)
			{
				adapterLog(
					"autonomy_macro_skip_not_due player=%d now=%lu timers[macro=%lu prod=%lu tech=%lu guard=%lu]",
					player->getPlayerIndex(),
					static_cast<unsigned long>(now),
					static_cast<unsigned long>(m_autonomy.state.nextMacroTick),
					static_cast<unsigned long>(m_autonomy.state.nextProductionTick),
					static_cast<unsigned long>(m_autonomy.state.nextTechTick),
					static_cast<unsigned long>(m_autonomy.state.nextGuardTick));
				return;
			}
			if (tickSchedule.guardOnly)
			{
				struct GuardOnlyCounts
				{
					Int soldiers;
					Int rpg;
					Int quads;
					Int scorpions;
				} guardCounts = { 0, 0, 0, 0 };

				player->iterateObjects([](Object* obj, void* userData)
				{
					if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
					{
						return;
					}

					GuardOnlyCounts* counts = static_cast<GuardOnlyCounts*>(userData);
					if (counts == nullptr)
					{
						return;
					}

					const ThingTemplate* tt = obj->getTemplate();
					const std::string name = tt != nullptr ? tt->getName().str() : "";
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
				}, &guardCounts);

				const Int combatCount = guardCounts.soldiers + guardCounts.rpg + guardCounts.quads + guardCounts.scorpions;
				const AIControlAdapterProfilePolicyManager profilePolicyManager;
				const DWORD cadenceMs = profilePolicyManager.ResolveGuardCadenceMs(resolveAutonomyProfilePolicyConfig());
				if (combatCount > 0)
				{
					char requestIdBuffer[96];
					sprintf_s(
						requestIdBuffer,
						"auto_guard_%08X_%08X",
						static_cast<unsigned int>(player->getPlayerIndex()),
						static_cast<unsigned int>(now));
					nlohmann::json message = {
						{"type", "SessionCommand"},
						{"request_id", std::string(requestIdBuffer)},
						{"cmd", "Game.GuardAllIdleGroundCombat"},
						{"args", nlohmann::json::object()}
					};
					if (m_autonomy.state.hasExplicitPlayerIndex)
					{
						message["args"]["player"] = player->getPlayerIndex();
					}
					std::string reason;
					const bool guarded = executeGameGuardAllIdleGroundCombat(message, reason);
					adapterLog(
						"autonomy_command request_id=%s cmd=Game.GuardAllIdleGroundCombat money=%lu ok=%d reason=%s",
						requestIdBuffer,
						0ul,
						guarded ? 1 : 0,
						guarded ? "" : reason.c_str());
					m_autonomy.state.nextGuardTick = now + (guarded ? cadenceMs : 4000u);
				}
				else
				{
					m_autonomy.state.nextGuardTick = now + 3000u;
				}
				return;
			}
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
			if (!m_autonomy.state.hasCashRateSample)
			{
				m_autonomy.state.hasCashRateSample = true;
				m_autonomy.state.lastCashRateMoney = money;
				m_autonomy.state.lastCashRateTick = now;
				m_autonomy.state.smoothedNetCashPerMinute = 0.0f;
			}
			else
			{
				const DWORD elapsedMs = now - m_autonomy.state.lastCashRateTick;
				if (elapsedMs >= 2000u)
				{
					const int moneyDelta = static_cast<int>(money) - static_cast<int>(m_autonomy.state.lastCashRateMoney);
					const Real sampleNetCashPerMinute =
						(static_cast<Real>(moneyDelta) * 60000.0f) / static_cast<Real>(elapsedMs);
					m_autonomy.state.smoothedNetCashPerMinute =
						(m_autonomy.state.smoothedNetCashPerMinute * 0.7f) + (sampleNetCashPerMinute * 0.3f);
					m_autonomy.state.lastCashRateMoney = money;
					m_autonomy.state.lastCashRateTick = now;
				}
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
				Int technicals;
				Int quads;
				Int scorpions;
				Int rocketBuggies;
				Int scudLaunchers;
				Int queuedProductionEntries;
				Int queuedTechnicals;
				Int queuedQuads;
				Int queuedScorpions;
				Int queuedRocketBuggies;
				Int queuedScudLaunchers;
				Int warFactoryLikeProducers;
			} counts = {};

			struct AutonomyOwnedObjectSnapshot
			{
				Object* object;
				std::string name;
				bool isStructure;
				bool underConstruction;
				bool countAsInProgress;
				bool isSupplyStructure;
				bool isBarracks;
				bool isArmsDealer;
				bool isPalace;
				bool isBlackMarket;
				bool isTunnel;
				bool isStinger;
				bool isWarFactoryLike;
				bool isDozer;
				bool isRadarVan;
				bool isSoldier;
				bool isRpg;
				bool isTechnical;
				bool isQuad;
				bool isScorpion;
				bool isRocketBuggy;
				bool isScudLauncher;
				bool hasCapturePower;
			};

			struct SnapshotBuildContext
			{
				Player* player;
				AutonomyMacroCounts* counts;
				std::vector<AutonomyOwnedObjectSnapshot>* ownedObjects;
			};

			std::vector<AutonomyOwnedObjectSnapshot> ownedObjects;
			ownedObjects.reserve(256);

			struct AutonomyZone
			{
				Coord3D center;
				ObjectID anchorId;
				bool isMainBase;
				ZoneAnchorType anchorType;

				AutonomyZone()
					: anchorId(static_cast<ObjectID>(0))
					, isMainBase(false)
					, anchorType(ZoneAnchorType::SupplyStash)
				{
					center.x = 0.0f;
					center.y = 0.0f;
					center.z = 0.0f;
				}
			};
			std::vector<AutonomyZone> zones;
			SnapshotBuildContext snapshotBuildContext = { player, &counts, &ownedObjects };

			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}

				SnapshotBuildContext* ctx = static_cast<SnapshotBuildContext*>(userData);
				if (ctx == nullptr || ctx->counts == nullptr || ctx->ownedObjects == nullptr)
				{
					return;
				}

				Player* owner = ctx->player;
				AutonomyMacroCounts* counts = ctx->counts;
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				const bool isStructure = obj->isKindOf(KINDOF_STRUCTURE);
				const bool underConstruction = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
				const bool countAsInProgress = isStructure && underConstruction && static_cast<Int>(obj->getBuilderID()) > 0;
				const bool isSupplyStructure = containsIgnoreCase(name, "supplystash") || containsIgnoreCase(name, "supplycenter");
				const bool isBarracks = containsIgnoreCase(name, "barracks");
				const bool isArmsDealer = containsIgnoreCase(name, "armsdealer");
				const bool isPalace = isPalaceTemplateName(name);
				const bool isBlackMarket = containsIgnoreCase(name, "black") && containsIgnoreCase(name, "market");
				const bool isTunnel = containsIgnoreCase(name, "tunnelnetwork");
				const bool isStinger = containsIgnoreCase(name, "stingersite");
				const bool isWarFactoryLike = containsIgnoreCase(name, "warfactory") || isArmsDealer;
				const bool isDozer = obj->isKindOf(KINDOF_DOZER);
				const bool isRadarVan = containsIgnoreCase(name, "radarvan");
				const bool isSoldier = containsIgnoreCase(name, "soldier");
				const bool isRpg = containsIgnoreCase(name, "rpg");
					const bool isTechnical = containsIgnoreCase(name, "technical");
					const bool isQuad = containsIgnoreCase(name, "quad");
					const bool isScorpion = containsIgnoreCase(name, "scorpion");
					const bool isRocketBuggy = containsIgnoreCase(name, "rocketbuggy");
					const bool isScudLauncher = containsIgnoreCase(name, "scudlauncher");
					const bool hasCapturePower =
						obj->hasSpecialPower(SPECIAL_INFANTRY_CAPTURE_BUILDING)
						|| obj->hasSpecialPower(SPECIAL_BLACKLOTUS_CAPTURE_BUILDING);

					ctx->ownedObjects->push_back(AutonomyOwnedObjectSnapshot{
						obj,
					name,
					isStructure,
					underConstruction,
					countAsInProgress,
					isSupplyStructure,
					isBarracks,
					isArmsDealer,
					isPalace,
					isBlackMarket,
					isTunnel,
					isStinger,
					isWarFactoryLike,
					isDozer,
						isRadarVan,
						isSoldier,
						isRpg,
						isTechnical,
						isQuad,
						isScorpion,
						isRocketBuggy,
						isScudLauncher,
						hasCapturePower
					});

				if (isStructure)
				{
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
					if (isSupplyStructure)
					{
						incrementStructureCount(counts->supplyStashes, counts->supplyStashesInProgress);
					}
					else if (isBarracks)
					{
						incrementStructureCount(counts->barracks, counts->barracksInProgress);
					}
					else if (isArmsDealer)
					{
						incrementStructureCount(counts->armsDealers, counts->armsDealersInProgress);
					}
					else if (isPalace)
					{
						incrementStructureCount(counts->palaces, counts->palacesInProgress);
					}
					else if (isBlackMarket)
					{
						incrementStructureCount(counts->blackMarkets, counts->blackMarketsInProgress);
					}
					else if (isTunnel)
					{
						incrementStructureCount(counts->tunnels, counts->tunnelsInProgress);
					}
					else if (isStinger)
					{
						incrementStructureCount(counts->stingers, counts->stingersInProgress);
					}
					if (isWarFactoryLike && !underConstruction)
					{
						ProductionUpdateInterface* production = obj->getProductionUpdateInterface();
						if (production != nullptr)
						{
							++counts->warFactoryLikeProducers;
							counts->queuedProductionEntries += static_cast<Int>(production->getProductionCount());

							if (TheThingFactory != nullptr)
							{
								const std::string technicalTemplateName = AIControlAdapterTemplateInferenceService::InferTechnicalTemplateForProducer(obj);
								if (!technicalTemplateName.empty())
								{
									const ThingTemplate* technicalTemplate = TheThingFactory->findTemplate(AsciiString(technicalTemplateName.c_str()), false);
									if (technicalTemplate != nullptr)
									{
										counts->queuedTechnicals += static_cast<Int>(production->countUnitTypeInQueue(technicalTemplate));
									}
								}

								const std::string quadTemplateName = AIControlAdapterTemplateInferenceService::InferQuadTemplateForProducer(obj);
								if (!quadTemplateName.empty())
								{
									const ThingTemplate* quadTemplate = TheThingFactory->findTemplate(AsciiString(quadTemplateName.c_str()), false);
									if (quadTemplate != nullptr)
									{
										counts->queuedQuads += static_cast<Int>(production->countUnitTypeInQueue(quadTemplate));
									}
								}

								const std::string scorpionTemplateName = AIControlAdapterTemplateInferenceService::InferScorpionTemplateForProducer(obj);
								if (!scorpionTemplateName.empty())
								{
									const ThingTemplate* scorpionTemplate = TheThingFactory->findTemplate(AsciiString(scorpionTemplateName.c_str()), false);
									if (scorpionTemplate != nullptr)
									{
										counts->queuedScorpions += static_cast<Int>(production->countUnitTypeInQueue(scorpionTemplate));
									}
								}

								const std::string buggyTemplateName = AIControlAdapterTemplateInferenceService::InferRocketBuggyTemplateForProducer(obj);
								if (!buggyTemplateName.empty())
								{
									const ThingTemplate* buggyTemplate = TheThingFactory->findTemplate(AsciiString(buggyTemplateName.c_str()), false);
									if (buggyTemplate != nullptr)
									{
										counts->queuedRocketBuggies += static_cast<Int>(production->countUnitTypeInQueue(buggyTemplate));
									}
								}

								if (owner != nullptr)
								{
									const std::string scudLauncherTemplateName = AIControlAdapterTemplateInferenceService::InferScudLauncherTemplate(owner);
									if (!scudLauncherTemplateName.empty())
									{
										const ThingTemplate* scudLauncherTemplate = TheThingFactory->findTemplate(AsciiString(scudLauncherTemplateName.c_str()), false);
										if (scudLauncherTemplate != nullptr)
										{
											counts->queuedScudLaunchers += static_cast<Int>(production->countUnitTypeInQueue(scudLauncherTemplate));
										}
									}
								}
							}
						}
					}
					return;
				}

				++counts->mobileUnits;
				if (isDozer)
				{
					++counts->workers;
				}
				if (isRadarVan)
				{
					++counts->radarVans;
				}
				if (isSoldier)
				{
					++counts->soldiers;
				}
				if (isRpg)
				{
					++counts->rpg;
				}
				if (isTechnical)
				{
					++counts->technicals;
				}
				if (isQuad)
				{
					++counts->quads;
				}
				if (isScorpion)
				{
					++counts->scorpions;
				}
				if (isRocketBuggy)
				{
					++counts->rocketBuggies;
				}
				if (isScudLauncher)
				{
					++counts->scudLaunchers;
				}
				if (isWarFactoryLike)
				{
					ProductionUpdateInterface* production = obj->getProductionUpdateInterface();
					if (production != nullptr)
					{
						++counts->warFactoryLikeProducers;
						counts->queuedProductionEntries += static_cast<Int>(production->getProductionCount());

						if (TheThingFactory != nullptr)
						{
							const std::string technicalTemplateName = AIControlAdapterTemplateInferenceService::InferTechnicalTemplateForProducer(obj);
							if (!technicalTemplateName.empty())
							{
								const ThingTemplate* technicalTemplate = TheThingFactory->findTemplate(AsciiString(technicalTemplateName.c_str()), false);
								if (technicalTemplate != nullptr)
								{
									counts->queuedTechnicals += static_cast<Int>(production->countUnitTypeInQueue(technicalTemplate));
								}
							}

							const std::string quadTemplateName = AIControlAdapterTemplateInferenceService::InferQuadTemplateForProducer(obj);
							if (!quadTemplateName.empty())
							{
								const ThingTemplate* quadTemplate = TheThingFactory->findTemplate(AsciiString(quadTemplateName.c_str()), false);
								if (quadTemplate != nullptr)
								{
									counts->queuedQuads += static_cast<Int>(production->countUnitTypeInQueue(quadTemplate));
								}
							}

							const std::string scorpionTemplateName = AIControlAdapterTemplateInferenceService::InferScorpionTemplateForProducer(obj);
							if (!scorpionTemplateName.empty())
							{
								const ThingTemplate* scorpionTemplate = TheThingFactory->findTemplate(AsciiString(scorpionTemplateName.c_str()), false);
								if (scorpionTemplate != nullptr)
								{
									counts->queuedScorpions += static_cast<Int>(production->countUnitTypeInQueue(scorpionTemplate));
								}
							}

							const std::string buggyTemplateName = AIControlAdapterTemplateInferenceService::InferRocketBuggyTemplateForProducer(obj);
							if (!buggyTemplateName.empty())
							{
								const ThingTemplate* buggyTemplate = TheThingFactory->findTemplate(AsciiString(buggyTemplateName.c_str()), false);
								if (buggyTemplate != nullptr)
								{
									counts->queuedRocketBuggies += static_cast<Int>(production->countUnitTypeInQueue(buggyTemplate));
								}
							}

							if (owner != nullptr)
							{
								const std::string scudLauncherTemplateName = AIControlAdapterTemplateInferenceService::InferScudLauncherTemplate(owner);
								if (!scudLauncherTemplateName.empty())
								{
									const ThingTemplate* scudLauncherTemplate = TheThingFactory->findTemplate(AsciiString(scudLauncherTemplateName.c_str()), false);
									if (scudLauncherTemplate != nullptr)
									{
										counts->queuedScudLaunchers += static_cast<Int>(production->countUnitTypeInQueue(scudLauncherTemplate));
									}
								}
							}
						}
					}
				}
			}, &snapshotBuildContext);
			adapterLog(
				"autonomy_macro_phase phase=counts player=%d supply=%d barracks=%d arms=%d workers=%d",
				player->getPlayerIndex(),
				counts.supplyStashes + counts.supplyStashesInProgress,
				counts.barracks + counts.barracksInProgress,
				counts.armsDealers + counts.armsDealersInProgress,
				counts.workers);

			auto pushZone = [&](Object* anchor, bool isMainBase, ZoneAnchorType anchorType) -> void
			{
				if (anchor == nullptr || anchor->getPosition() == nullptr)
				{
					return;
				}
				AutonomyZone zone = {};
				zone.center = *anchor->getPosition();
				zone.anchorId = anchor->getID();
				zone.isMainBase = isMainBase;
				zone.anchorType = anchorType;
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
				pushZone(cc, true, ZoneAnchorType::MainBase);
			}
			std::string mapName;
			if (TheGlobalData != nullptr)
			{
				mapName = TheGlobalData->m_mapName.str();
			}
			if (mapName.empty() && TheTerrainLogic != nullptr)
			{
				mapName = TheTerrainLogic->getSourceFilename().str();
			}
			bool hasMainBaseTerrainAnchor = false;
			float mainBaseTerrainX = 0.0f;
			float mainBaseTerrainY = 0.0f;
			for (std::size_t zoneIdx = 0; zoneIdx < zones.size(); ++zoneIdx)
			{
				if (zones[zoneIdx].isMainBase)
				{
					hasMainBaseTerrainAnchor = true;
					mainBaseTerrainX = zones[zoneIdx].center.x;
					mainBaseTerrainY = zones[zoneIdx].center.y;
					break;
				}
			}
				const AIControlAdapterTerrainFacts extractedTerrainFacts = buildEngineTerrainFacts(
					mapName,
					hasMainBaseTerrainAnchor,
					mainBaseTerrainX,
					mainBaseTerrainY);
				const AIControlAdapterTerrainFacts fixtureTerrainFacts = AIControlAdapterBuildTerrainFacts(
					mapName,
					hasMainBaseTerrainAnchor,
					mainBaseTerrainX,
					mainBaseTerrainY);
				const AIControlAdapterTerrainFacts mapFileCacheFacts = loadMapFileCacheFactsForMap(mapName);
				const AIControlAdapterTerrainFacts terrainFacts = AIControlAdapterMergeMapFileCacheFacts(
					extractedTerrainFacts,
					fixtureTerrainFacts,
					mapFileCacheFacts);
				m_autonomy.state.pathingTelemetry = AIControlAdapterSerializeTerrainFacts(terrainFacts);
				const std::string mapFileCacheKey = AIControlAdapterNormalizeMapFileCacheKey(mapName);
				if (m_mapFileTerrainMergeLoggedKeys.insert(mapFileCacheKey).second)
				{
					adapterLog(
						"map_file_cache_merge map=%s cache_features=%d runtime_features=%d fixture_features=%d final_features=%d reason=%s",
						mapName.empty() ? "unknown" : mapName.c_str(),
						static_cast<int>(mapFileCacheFacts.features.size()),
						static_cast<int>(extractedTerrainFacts.features.size()),
						static_cast<int>(fixtureTerrainFacts.features.size()),
						static_cast<int>(terrainFacts.features.size()),
						mapFileCacheFacts.mapFileCacheTelemetry.is_object()
							? mapFileCacheFacts.mapFileCacheTelemetry.value("reason", std::string("not_evaluated")).c_str()
							: "not_evaluated");
				}
				adapterLog(
					"terrain_extraction_probe map=%s source=%s extent=%.1f,%.1f,%.1f,%.1f cliffs=%d bridges=%d waypoints=%d features=%d reason=%s",
					mapName.empty() ? "unknown" : mapName.c_str(),
					extractedTerrainFacts.source.c_str(),
					extractedTerrainFacts.extraction.extent.minX,
					extractedTerrainFacts.extraction.extent.minY,
					extractedTerrainFacts.extraction.extent.maxX,
					extractedTerrainFacts.extraction.extent.maxY,
					extractedTerrainFacts.extraction.cliffSamples,
					extractedTerrainFacts.extraction.bridgeCount,
					extractedTerrainFacts.extraction.waypointCount,
					static_cast<int>(extractedTerrainFacts.features.size()),
					extractedTerrainFacts.extraction.reason.c_str());
				adapterLog(
					"terrain_sample_summary map=%s sample_step=%d blocked=%d passable=%d cliff=%d unknown=%d",
					mapName.empty() ? "unknown" : mapName.c_str(),
					extractedTerrainFacts.extraction.sampleStep,
					extractedTerrainFacts.extraction.blockedSamples,
					extractedTerrainFacts.extraction.passableSamples,
					extractedTerrainFacts.extraction.cliffSamples,
					extractedTerrainFacts.extraction.unknownSamples);
				adapterLog(
					"terrain_extraction_source map=%s selected=%s fallback=%s reason=%s",
					mapName.empty() ? "unknown" : mapName.c_str(),
					terrainFacts.source.c_str(),
					terrainFacts.extraction.fallbackSource.empty() ? "none" : terrainFacts.extraction.fallbackSource.c_str(),
					terrainFacts.extraction.reason.empty() ? "not_evaluated" : terrainFacts.extraction.reason.c_str());
				adapterLog(
					"terrain_facts_loaded map=%s features=%d source=%s",
				mapName.empty() ? "unknown" : mapName.c_str(),
				static_cast<int>(terrainFacts.features.size()),
				terrainFacts.source.c_str());
			for (std::size_t terrainIdx = 0; terrainIdx < terrainFacts.features.size(); ++terrainIdx)
			{
				const AIControlAdapterTerrainFeature& feature = terrainFacts.features[terrainIdx];
				adapterLog(
					"terrain_feature id=%s kind=%s source=%s points=%d",
					feature.id.c_str(),
					feature.kind.c_str(),
					feature.source.c_str(),
					static_cast<int>(feature.points.size()));
			}
			for (std::size_t i = 0; i < ownedObjects.size(); ++i)
			{
				const AutonomyOwnedObjectSnapshot& owned = ownedObjects[i];
				if (!owned.isStructure || owned.underConstruction || !owned.isSupplyStructure)
				{
					continue;
				}
				pushZone(owned.object, false, ZoneAnchorType::SupplyStash);
			}

			// Phase 5.8: Add captured structure zone anchors
			// Scan for owned capturable/tech structures that can become zone anchors
			if (TheGameLogic != nullptr)
			{
				for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
				{
					if (obj->isEffectivelyDead())
					{
						continue;
					}
					// Must be a structure and capturable (tech building or static structure)
					if (!obj->isKindOf(KINDOF_STRUCTURE) || !obj->isKindOf(KINDOF_CAPTURABLE))
					{
						continue;
					}
					// Must be owned by the player (captured, not neutral/enemy)
					if (obj->getControllingPlayer() != player)
					{
						continue;
					}
					// Must not be under construction
					if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
					{
						continue;
					}
					// Must have position
					const Coord3D* pos = obj->getPosition();
					if (pos == nullptr)
					{
						continue;
					}

					// Check if too close to existing zones (using same 220 threshold as pushZone)
					bool tooClose = false;
					for (std::size_t i = 0; i < zones.size(); ++i)
					{
						const Real dx = zones[i].center.x - pos->x;
						const Real dy = zones[i].center.y - pos->y;
						if ((dx * dx) + (dy * dy) < (220.0f * 220.0f))
						{
							tooClose = true;
							break;
						}
					}
					if (tooClose)
					{
						continue;
					}

					// Phase 5.8: Verify nearby build space for Tunnel or Stinger
					// Check for crowding by nearby friendly structures
					bool hasBuildSpace = false;
					const float buildSpaceCheckRadius = 120.0f; // Typical GLA structure footprint + spacing
					int nearbyFriendlyStructures = 0;

					for (std::size_t i = 0; i < ownedObjects.size(); ++i)
					{
						const AutonomyOwnedObjectSnapshot& owned = ownedObjects[i];
						if (!owned.isStructure)
						{
							continue;
						}
						const Coord3D* ownedPos = owned.object->getPosition();
						if (ownedPos == nullptr)
						{
							continue;
						}
						const float dx = ownedPos->x - pos->x;
						const float dy = ownedPos->y - pos->y;
						const float distSq = (dx * dx) + (dy * dy);

						if (distSq < (buildSpaceCheckRadius * buildSpaceCheckRadius))
						{
							++nearbyFriendlyStructures;
						}
					}

					// Heuristic: if fewer than 3 friendly structures in build radius, assume space is available
					// This is a rough check since we don't have access to engine placement APIs
					hasBuildSpace = (nearbyFriendlyStructures < 3);

					if (!hasBuildSpace)
					{
						const ThingTemplate* tt = obj->getTemplate();
						const std::string name = tt != nullptr ? tt->getName().str() : "";
						adapterLog(
							"zone_anchor_rejected type=captured_structure object_id=%u x=%.1f y=%.1f template=%s reason=no_build_space nearby_structures=%d",
							static_cast<unsigned int>(obj->getID()),
							pos->x,
							pos->y,
							name.c_str(),
							nearbyFriendlyStructures);
						continue;
					}

					// Log candidate and add zone
					const ThingTemplate* tt = obj->getTemplate();
					const std::string name = tt != nullptr ? tt->getName().str() : "";
					adapterLog(
						"zone_anchor_candidate type=captured_structure object_id=%u x=%.1f y=%.1f template=%s reason=owned_capturable has_build_space=1",
						static_cast<unsigned int>(obj->getID()),
						pos->x,
						pos->y,
						name.c_str());

					AutonomyZone zone = {};
					zone.center = *pos;
					zone.anchorId = obj->getID();
					zone.isMainBase = false;
					zone.anchorType = ZoneAnchorType::CapturedStructure;
					zones.push_back(zone);

					adapterLog(
						"zone_anchor_selected type=captured_structure source_object=%u x=%.1f y=%.1f reason=new_zone",
						static_cast<unsigned int>(obj->getID()),
						pos->x,
						pos->y);
				}
			}

			// Phase 5.8: Convert pending foothold anchors to actual zones if structures exist nearby
			std::vector<UnsignedInt> convertedFootholds;
			for (std::size_t i = 0; i < m_autonomy.state.pendingFootholdAnchors.size(); ++i)
			{
				AutonomyState::PendingFootholdAnchor& pending = m_autonomy.state.pendingFootholdAnchors[i];

				// Check if any friendly Tunnel or Stinger exists near target
				bool foundNearbyStructure = false;
				ObjectID nearbyStructureId = static_cast<ObjectID>(0);
				for (std::size_t j = 0; j < ownedObjects.size(); ++j)
				{
					const AutonomyOwnedObjectSnapshot& owned = ownedObjects[j];
					if (!owned.isStructure || owned.underConstruction)
					{
						continue;
					}

					// Check for Tunnel Network or Stinger Site
					if (!owned.isTunnel && !owned.isStinger)
					{
						continue;
					}

					// Check distance to pending target
					const Coord3D* ownedPos = owned.object->getPosition();
					if (ownedPos == nullptr)
					{
						continue;
					}

					const float dx = ownedPos->x - pending.targetX;
					const float dy = ownedPos->y - pending.targetY;
					const float distSq = (dx * dx) + (dy * dy);
					const float maxDistSq = 250.0f * 250.0f; // Structure within 250 units validates the foothold

					if (distSq < maxDistSq)
					{
						foundNearbyStructure = true;
						nearbyStructureId = owned.object->getID();
						break;
					}
				}

				if (foundNearbyStructure)
				{
					// Convert pending foothold to actual zone
					AutonomyZone zone = {};
					zone.center.x = pending.targetX;
					zone.center.y = pending.targetY;
					zone.center.z = 0.0f;
					zone.anchorId = static_cast<ObjectID>(pending.syntheticAnchorId);
					zone.isMainBase = false;
					zone.anchorType = pending.anchorType;
					zones.push_back(zone);

					adapterLog(
						"zone_anchor_selected type=%s source_object=%u x=%.1f y=%.1f reason=foothold_structure_completed nearby_structure=%u",
						zoneAnchorTypeToString(pending.anchorType),
						static_cast<unsigned int>(pending.syntheticAnchorId),
						pending.targetX,
						pending.targetY,
						static_cast<unsigned int>(nearbyStructureId));

					convertedFootholds.push_back(static_cast<UnsignedInt>(i));
				}
			}

			// Remove converted pending footholds
			for (std::size_t i = convertedFootholds.size(); i > 0; --i)
			{
				const std::size_t idx = static_cast<std::size_t>(convertedFootholds[i - 1]);
				m_autonomy.state.pendingFootholdAnchors.erase(
					m_autonomy.state.pendingFootholdAnchors.begin() + idx);
			}

			// Phase 5.8: Validate captured-structure and foothold zone anchors
			// Remove zones whose anchors are destroyed, recaptured, or missing
			std::vector<std::size_t> zonesToRemove;
			for (std::size_t i = 0; i < zones.size(); ++i)
			{
				const AutonomyZone& zone = zones[i];

				// Skip main base and supply stash zones - they're validated elsewhere
				if (zone.anchorType == ZoneAnchorType::MainBase || zone.anchorType == ZoneAnchorType::SupplyStash)
				{
					continue;
				}

				// Synthetic anchor IDs (>= 1000000) are for foothold zones - validate structure presence
				const bool isSyntheticAnchor = static_cast<UnsignedInt>(zone.anchorId) >= 1000000u;

				if (zone.anchorType == ZoneAnchorType::CapturedStructure && !isSyntheticAnchor)
				{
					// Real object anchor - check if object still exists and is friendly-owned
					Object* anchorObj = nullptr;
					if (TheGameLogic != nullptr)
					{
						for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
						{
							if (obj->getID() == zone.anchorId)
							{
								anchorObj = obj;
								break;
							}
						}
					}

					bool shouldRemove = false;
					std::string removeReason;

					if (anchorObj == nullptr)
					{
						shouldRemove = true;
						removeReason = "missing";
					}
					else if (anchorObj->isEffectivelyDead())
					{
						shouldRemove = true;
						removeReason = "destroyed";
					}
					else if (anchorObj->getControllingPlayer() != player)
					{
						shouldRemove = true;
						removeReason = "recaptured";
					}

					if (shouldRemove)
					{
						// Check if there's a nearby friendly support structure to re-anchor to
						Object* reanchorCandidate = nullptr;
						for (std::size_t j = 0; j < ownedObjects.size(); ++j)
						{
							const AutonomyOwnedObjectSnapshot& owned = ownedObjects[j];
							if (!owned.isStructure || owned.underConstruction)
							{
								continue;
							}

							// Look for Tunnel, Stinger, or Supply structures
							if (!owned.isTunnel && !owned.isStinger && !owned.isSupplyStructure)
							{
								continue;
							}

							const Coord3D* ownedPos = owned.object->getPosition();
							if (ownedPos == nullptr)
							{
								continue;
							}

							const float dx = ownedPos->x - zone.center.x;
							const float dy = ownedPos->y - zone.center.y;
							const float distSq = (dx * dx) + (dy * dy);
							const float reanchorDistSq = 180.0f * 180.0f; // Re-anchor if support structure within 180 units

							if (distSq < reanchorDistSq)
							{
								reanchorCandidate = owned.object;
								break;
							}
						}

						if (reanchorCandidate != nullptr)
						{
							// Re-anchor zone to nearby support structure
							zones[i].anchorId = reanchorCandidate->getID();
							zones[i].anchorType = ZoneAnchorType::SupplyStash; // Re-anchor as supply zone

							adapterLog(
								"zone_anchor_reanchored old_object=%u new_object=%u type=%s reason=friendly_support_nearby old_reason=%s",
								static_cast<unsigned int>(zone.anchorId),
								static_cast<unsigned int>(reanchorCandidate->getID()),
								zoneAnchorTypeToString(zones[i].anchorType),
								removeReason.c_str());
						}
						else
						{
							// No re-anchor candidate - remove zone
							adapterLog(
								"zone_anchor_removed type=captured_structure source_object=%u reason=%s x=%.1f y=%.1f",
								static_cast<unsigned int>(zone.anchorId),
								removeReason.c_str(),
								zone.center.x,
								zone.center.y);
							zonesToRemove.push_back(i);
						}
					}
				}
				else if ((zone.anchorType == ZoneAnchorType::StrategicFoothold || zone.anchorType == ZoneAnchorType::MarketFoothold) && isSyntheticAnchor)
				{
					// Foothold zones - validate that support structures still exist nearby
					bool foundSupport = false;
					for (std::size_t j = 0; j < ownedObjects.size(); ++j)
					{
						const AutonomyOwnedObjectSnapshot& owned = ownedObjects[j];
						if (!owned.isStructure || owned.underConstruction)
						{
							continue;
						}

						// Look for Tunnel or Stinger
						if (!owned.isTunnel && !owned.isStinger)
						{
							continue;
						}

						const Coord3D* ownedPos = owned.object->getPosition();
						if (ownedPos == nullptr)
						{
							continue;
						}

						const float dx = ownedPos->x - zone.center.x;
						const float dy = ownedPos->y - zone.center.y;
						const float distSq = (dx * dx) + (dy * dy);
						const float supportDistSq = 250.0f * 250.0f; // Same threshold as foothold conversion

						if (distSq < supportDistSq)
						{
							foundSupport = true;
							break;
						}
					}

					if (!foundSupport)
					{
						// No supporting structures - remove foothold zone
						adapterLog(
							"zone_anchor_removed type=%s source_object=%u reason=no_support_structures x=%.1f y=%.1f",
							zoneAnchorTypeToString(zone.anchorType),
							static_cast<unsigned int>(zone.anchorId),
							zone.center.x,
							zone.center.y);
						zonesToRemove.push_back(i);
					}
				}
			}

			// Remove invalidated zones
			for (std::size_t i = zonesToRemove.size(); i > 0; --i)
			{
				const std::size_t idx = zonesToRemove[i - 1];
				zones.erase(zones.begin() + idx);
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

			// Helper: Find strategic foothold placement coordinates
			auto findFootholdPlacement = [&](std::string& outReason) -> Coord3D
			{
				Coord3D candidate = {};
				candidate.x = 0.0f;
				candidate.y = 0.0f;
				candidate.z = 0.0f;
				outReason = "no_candidate";

				// Need at least one existing zone to determine sprawl direction
				if (zones.empty())
				{
					outReason = "no_existing_zones";
					return candidate;
				}

				// Find furthest zone along sprawl axis
				float furthestDot = -1e10f;
				const AutonomyZone* furthestZone = nullptr;
				for (std::size_t i = 0; i < zones.size(); ++i)
				{
					const float dot = (zones[i].center.x * sprawlAxisDx) + (zones[i].center.y * sprawlAxisDy);
					if (dot > furthestDot)
					{
						furthestDot = dot;
						furthestZone = &zones[i];
					}
				}

				if (furthestZone == nullptr)
				{
					outReason = "no_furthest_zone";
					return candidate;
				}

				// Project beyond furthest zone along sprawl axis by 500 units
				const float extensionDistance = 500.0f;
				candidate.x = furthestZone->center.x + (sprawlAxisDx * extensionDistance);
				candidate.y = furthestZone->center.y + (sprawlAxisDy * extensionDistance);
				candidate.z = 0.0f;

				// Validate minimum distance from all existing zones
				const float minDistSq = 220.0f * 220.0f;
				for (std::size_t i = 0; i < zones.size(); ++i)
				{
					const float dx = candidate.x - zones[i].center.x;
					const float dy = candidate.y - zones[i].center.y;
					const float distSq = (dx * dx) + (dy * dy);
					if (distSq < minDistSq)
					{
						outReason = "too_close";
						candidate.x = 0.0f;
						candidate.y = 0.0f;
						return candidate;
					}
				}

				// Validate against map boundaries (rough approximation)
				if (candidate.x < 100.0f || candidate.x > 6000.0f || candidate.y < 100.0f || candidate.y > 6000.0f)
				{
					outReason = "out_of_bounds";
					candidate.x = 0.0f;
					candidate.y = 0.0f;
					return candidate;
				}

				outReason = "valid_foothold_location";
				return candidate;
			};

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

			// Phase 5.5: Strategic placement helpers for high-value structures
			enum class StrategicStructureRole
			{
				Income,
				Tech,
				Superweapon
			};

			struct StrategicPlacementChoice
			{
				bool hasPlacement = false;
				unsigned int zoneAnchorId = 0;
				bool isMainBase = false;
				float zoneCenterX = 0.0f;
				float zoneCenterY = 0.0f;
				float zoneRadius = 0.0f;
				std::string source;
				std::string reason;
				int score = 0;                      // Phase 5.7: Zone score for this placement
				unsigned int zoneMarketCount = 0;   // Phase 5.7: Black Markets in selected zone
				unsigned int totalMarkets = 0;      // Phase 5.7: Total completed Black Markets
			};

			// Phase 5.7: Distributed safe economy placement with zone scoring
			auto chooseStrategicPlacement = [&](
				const std::vector<AutonomyZone>& zones,
				const std::vector<AutonomyZoneCounts>& zoneCounts,
				Int activeZoneIndex,
				StrategicStructureRole role,
				Real zoneRadius,
				unsigned int completedBlackMarkets) -> StrategicPlacementChoice
			{
				StrategicPlacementChoice choice;

				if (zones.empty())
				{
					choice.hasPlacement = false;
					choice.source = "unavailable";
					choice.reason = "no_zones";
					return choice;
				}

				// Helper to apply rear-side bias for income/tech structures
				auto applyRearBias = [&](const AutonomyZone& zone, float& centerX, float& centerY, Real& radius, std::string& reason) -> const char*
				{
					// Income, tech, and WMD structures should prefer rear/interior placement.
					if (role == StrategicStructureRole::Income || role == StrategicStructureRole::Tech || role == StrategicStructureRole::Superweapon)
					{
						Real zoneFrontDx = sprawlAxisDx;
						Real zoneFrontDy = sprawlAxisDy;
						const char* ignoredSource = "sprawl_axis";
						resolveZoneFrontDirection(zone, zoneFrontDx, zoneFrontDy, ignoredSource);
						const AIControlAdapterZoneTerrainResult terrainZone = AIControlAdapterApplyZoneTerrainFacts(
							terrainFacts,
							{ zone.center.x, zone.center.y, static_cast<float>(radius), static_cast<float>(zoneFrontDx), static_cast<float>(zoneFrontDy), zone.isMainBase });
						if (terrainZone.terrainLimited || terrainZone.hasEntrance)
						{
							centerX = terrainZone.rearPoint.x;
							centerY = terrainZone.rearPoint.y;
							radius = std::max<Real>(140.0f, terrainZone.effectiveRadius * (role == StrategicStructureRole::Superweapon ? 0.38f : 0.45f));
							reason += terrainZone.hasEntrance ? "_terrain_rear" : "_terrain_interior";
							return terrainZone.hasEntrance ? "rear" : "interior";
						}

						const bool hasAxis = (std::fabs(zoneFrontDx) > 0.0001f) || (std::fabs(zoneFrontDy) > 0.0001f);
						if (hasAxis)
						{
							// Push toward rear (negative offset from front direction)
							const Real rearOffset = role == StrategicStructureRole::Superweapon ? (zone.isMainBase ? -0.38f : -0.48f) : (zone.isMainBase ? -0.25f : -0.35f);
							centerX += zoneFrontDx * radius * rearOffset;
							centerY += zoneFrontDy * radius * rearOffset;
							reason += "_terrain_fallback";
							return "fallback";
						}
					}
					return "interior";
				};

				// Phase 5.7: Count Black Markets per zone
				std::vector<unsigned int> marketsPerZone(zones.size(), 0u);
				for (std::size_t i = 0; i < zones.size() && i < zoneCounts.size(); ++i)
				{
					marketsPerZone[i] = zoneCounts[i].blackMarkets;
				}

				// Phase 5.7: Score all zones and choose the best one
				int bestScore = -999999;
				int bestZoneIndex = -1;

				const unsigned int mainBaseMarketThreshold = 6u; // Prefer main base for first 6 markets

				for (std::size_t i = 0; i < zones.size(); ++i)
				{
					const AutonomyZone& zone = zones[i];
					const AutonomyZoneCounts& counts = (i < zoneCounts.size()) ? zoneCounts[i] : AutonomyZoneCounts{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
					const unsigned int zoneMarkets = marketsPerZone[i];

					int score = 0;

					// Main base bonus for first few markets
					if (zone.isMainBase && completedBlackMarkets < mainBaseMarketThreshold)
					{
						score += 100; // Strong preference for early markets
					}
					else if (zone.isMainBase)
					{
						score += 30; // Moderate preference after threshold
					}

					// Heavy penalty for active zone (avoid forward placement)
					if (activeZoneIndex >= 0 && static_cast<Int>(i) == activeZoneIndex)
					{
						score -= 200;
					}

					// Penalty per existing Black Market in this zone (distribute markets)
					score -= static_cast<int>(zoneMarkets) * 40;

					// Bonus for developed zone (has infrastructure)
					const bool isDeveloped = (counts.barracks > 0 || counts.armsDealers > 0 || counts.supplyStashes > 0);
					if (isDeveloped)
					{
						score += 50;
					}

					// Bonus for defensive structures (tunnels, stingers)
					if (counts.tunnels > 0 || counts.stingers > 0)
					{
						score += 20;
					}

					Real scoreFrontDx = sprawlAxisDx;
					Real scoreFrontDy = sprawlAxisDy;
					const char* ignoredScoreSource = "sprawl_axis";
					resolveZoneFrontDirection(zone, scoreFrontDx, scoreFrontDy, ignoredScoreSource);
					const AIControlAdapterZoneTerrainResult terrainScore = AIControlAdapterApplyZoneTerrainFacts(
						terrainFacts,
						{ zone.center.x, zone.center.y, static_cast<float>(zoneRadius), static_cast<float>(scoreFrontDx), static_cast<float>(scoreFrontDy), zone.isMainBase });
					const bool tinyEffectiveZone = terrainScore.effectiveRadius < std::max<Real>(220.0f, zoneRadius * 0.45f);
					if (tinyEffectiveZone)
					{
						score -= 120;
					}
					else if (terrainScore.terrainLimited)
					{
						score -= 30;
					}
					adapterLog(
						"expansion_terrain_score anchor=%u effective_radius=%.1f defensible=%d route_estimate=%s score=%d reason=%s",
						static_cast<unsigned int>(zone.anchorId),
						terrainScore.effectiveRadius,
						tinyEffectiveZone ? 0 : 1,
						"unknown",
						score,
						terrainScore.terrainLimited ? terrainScore.reason : "distance_scoring");

					// Update best zone
					if (score > bestScore)
					{
						bestScore = score;
						bestZoneIndex = static_cast<int>(i);
					}
				}

				// If we found a zone with non-terrible score, use it
				if (bestZoneIndex >= 0 && bestScore > -200)
				{
					const AutonomyZone& zone = zones[static_cast<std::size_t>(bestZoneIndex)];
					const unsigned int zoneMarkets = marketsPerZone[static_cast<std::size_t>(bestZoneIndex)];

					choice.hasPlacement = true;
					choice.zoneAnchorId = zone.anchorId;
					choice.isMainBase = zone.isMainBase;
					choice.zoneCenterX = zone.center.x;
					choice.zoneCenterY = zone.center.y;
					choice.zoneRadius = zoneRadius;
					choice.score = bestScore;
					choice.zoneMarketCount = zoneMarkets;
					choice.totalMarkets = completedBlackMarkets;

					// Determine source based on zone type
					if (zone.isMainBase)
					{
						if (completedBlackMarkets < mainBaseMarketThreshold)
						{
							choice.reason = "main_base_early_market";
						}
						else
						{
							choice.reason = "main_base_best_score";
						}
					}
					else
					{
						choice.reason = "distributed_placement";
					}
					// Apply rear-side bias
					const char* placementRole = applyRearBias(zone, choice.zoneCenterX, choice.zoneCenterY, choice.zoneRadius, choice.reason);
					choice.source = zone.isMainBase ? std::string("main_base_") + placementRole : std::string("rear_zone_") + placementRole;
					adapterLog(
						"terrain_placement_choice template=%s zone=%u role=%s x=%.1f y=%.1f reason=%s",
						role == StrategicStructureRole::Superweapon ? "GLAScudStorm" :
							(role == StrategicStructureRole::Tech ? "GLAPalace" : "GLABlackMarket"),
						choice.zoneAnchorId,
						placementRole,
						choice.zoneCenterX,
						choice.zoneCenterY,
						choice.reason.c_str());

					return choice;
				}

				// Fallback: all zones have terrible scores, use first available with least markets
				int fallbackZone = -1;
				unsigned int minMarkets = 999999u;
				for (std::size_t i = 0; i < zones.size(); ++i)
				{
					if (marketsPerZone[i] < minMarkets)
					{
						minMarkets = marketsPerZone[i];
						fallbackZone = static_cast<int>(i);
					}
				}

				if (fallbackZone >= 0)
				{
					const AutonomyZone& zone = zones[static_cast<std::size_t>(fallbackZone)];
					choice.hasPlacement = true;
					choice.zoneAnchorId = zone.anchorId;
					choice.isMainBase = zone.isMainBase;
					choice.zoneCenterX = zone.center.x;
					choice.zoneCenterY = zone.center.y;
					choice.zoneRadius = zoneRadius;
					choice.score = bestScore;
					choice.zoneMarketCount = marketsPerZone[static_cast<std::size_t>(fallbackZone)];
					choice.totalMarkets = completedBlackMarkets;
					choice.reason = "all_zones_poor_score_using_fallback";

					// Apply rear-side bias (even for fallback)
					const char* placementRole = applyRearBias(zone, choice.zoneCenterX, choice.zoneCenterY, choice.zoneRadius, choice.reason);

					choice.source = std::string("fallback_") + placementRole;
					adapterLog(
						"terrain_placement_choice template=%s zone=%u role=%s x=%.1f y=%.1f reason=%s",
						role == StrategicStructureRole::Superweapon ? "GLAScudStorm" :
							(role == StrategicStructureRole::Tech ? "GLAPalace" : "GLABlackMarket"),
						choice.zoneAnchorId,
						placementRole,
						choice.zoneCenterX,
						choice.zoneCenterY,
						choice.reason.c_str());
					return choice;
				}

				// No placement available
				choice.hasPlacement = false;
				choice.source = "unavailable";
				choice.reason = "no_zones_available";
				return choice;
			};

			std::vector<AutonomyZoneCounts> zoneCounts(zones.size(), AutonomyZoneCounts{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
			if (!zones.empty())
			{
				const Real zoneRadiusSq = std::max<Real>(160.0f, m_autonomy.state.zoneRadius) * std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
				for (std::size_t i = 0; i < ownedObjects.size(); ++i)
				{
					const AutonomyOwnedObjectSnapshot& owned = ownedObjects[i];
					if (!owned.isStructure || owned.object == nullptr || owned.object->getPosition() == nullptr)
					{
						continue;
					}
					auto incrementZoneStructureCount = [&](AutonomyZoneCounts& counts, Int AutonomyZoneCounts::* completeField, Int AutonomyZoneCounts::* inProgressField) -> void
					{
						if (owned.countAsInProgress)
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
						const Real dx = owned.object->getPosition()->x - zones[zoneIdx].center.x;
						const Real dy = owned.object->getPosition()->y - zones[zoneIdx].center.y;
						if ((dx * dx) + (dy * dy) > zoneRadiusSq)
						{
							continue;
						}
						if (owned.isSupplyStructure)
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::supplyStashes, &AutonomyZoneCounts::supplyStashesInProgress);
						}
						else if (owned.isBarracks)
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::barracks, &AutonomyZoneCounts::barracksInProgress);
						}
						else if (owned.isArmsDealer)
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::armsDealers, &AutonomyZoneCounts::armsDealersInProgress);
						}
						else if (owned.isPalace)
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::palaces, &AutonomyZoneCounts::palacesInProgress);
						}
						else if (owned.isBlackMarket)
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::blackMarkets, &AutonomyZoneCounts::blackMarketsInProgress);
						}
						else if (owned.isTunnel)
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::tunnels, &AutonomyZoneCounts::tunnelsInProgress);
						}
						else if (owned.isStinger)
						{
							incrementZoneStructureCount(zoneCounts[zoneIdx], &AutonomyZoneCounts::stingers, &AutonomyZoneCounts::stingersInProgress);
						}
					}
				}
			}
			AutonomyZoneCounts activeZoneCounts = (activeZoneIndex >= 0 && activeZoneIndex < static_cast<Int>(zoneCounts.size()))
				? zoneCounts[static_cast<std::size_t>(activeZoneIndex)]
				: AutonomyZoneCounts{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

			if (!zones.empty() && !m_autonomy.state.zoneThreats.empty())
			{
				const DWORD zonePriorityNow = ::GetTickCount();
				Int threatenedZoneIndex = -1;
				DWORD newestThreatTick = 0u;
				for (std::size_t zoneIdx = 0; zoneIdx < zones.size(); ++zoneIdx)
				{
					const UnsignedInt zoneAnchorId = static_cast<UnsignedInt>(zones[zoneIdx].anchorId);
					const auto threatIt = m_autonomy.state.zoneThreats.find(zoneAnchorId);
					if (threatIt == m_autonomy.state.zoneThreats.end())
					{
						continue;
					}
					if (zonePriorityNow - threatIt->second.lastSeenTick > 45000u)
					{
						continue;
					}
					if (threatenedZoneIndex < 0 || static_cast<LONG>(threatIt->second.lastSeenTick - newestThreatTick) > 0)
					{
						threatenedZoneIndex = static_cast<Int>(zoneIdx);
						newestThreatTick = threatIt->second.lastSeenTick;
					}
				}
				if (threatenedZoneIndex >= 0)
				{
					activeZoneIndex = threatenedZoneIndex;
					activeZone = zones[static_cast<std::size_t>(activeZoneIndex)];
					hasActiveZone = true;
					m_autonomy.state.nextZoneIndex = static_cast<std::size_t>(activeZoneIndex);
					m_autonomy.state.hasLastZone = true;
					m_autonomy.state.lastZoneAnchorId = static_cast<UnsignedInt>(activeZone.anchorId);
					m_autonomy.state.lastZoneIsMainBase = activeZone.isMainBase;
					m_autonomy.state.lastZoneCenterX = activeZone.center.x;
					m_autonomy.state.lastZoneCenterY = activeZone.center.y;
					activeZoneCounts = zoneCounts[static_cast<std::size_t>(activeZoneIndex)];
				}
			}

			bool zoneThreatsChanged = false;
			std::set<UnsignedInt> visibleStructureIds;
			const DWORD threatNowTick = ::GetTickCount();
			const Real threatAssociationRadius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius) * 1.25f;
			const Real threatAssociationRadiusSq = threatAssociationRadius * threatAssociationRadius;
			if (!zones.empty())
			{
				for (std::size_t i = 0; i < ownedObjects.size(); ++i)
				{
					const AutonomyOwnedObjectSnapshot& owned = ownedObjects[i];
					if (!owned.isStructure || owned.object == nullptr || owned.object->getPosition() == nullptr)
					{
						continue;
					}

					BodyModuleInterface* body = owned.object->getBodyModule();
					if (body == nullptr)
					{
						continue;
					}

					const UnsignedInt objectId = static_cast<UnsignedInt>(owned.object->getID());
					const Real health = body->getHealth();
					const Real maxHealth = body->getMaxHealth();
					if (maxHealth <= 0.0f)
					{
						continue;
					}
					visibleStructureIds.insert(objectId);

					auto trackedIt = m_autonomy.state.trackedStructureHealth.find(objectId);
					if (trackedIt != m_autonomy.state.trackedStructureHealth.end())
					{
						const Real damageDelta = trackedIt->second.health - health;
						const Real meaningfulDamage = std::max<Real>(25.0f, maxHealth * 0.02f);
						if (damageDelta >= meaningfulDamage)
						{
							Int nearestZoneIndex = -1;
							Real nearestDistSq = threatAssociationRadiusSq;
							for (std::size_t zoneIdx = 0; zoneIdx < zones.size(); ++zoneIdx)
							{
								const Real dx = owned.object->getPosition()->x - zones[zoneIdx].center.x;
								const Real dy = owned.object->getPosition()->y - zones[zoneIdx].center.y;
								const Real distSq = (dx * dx) + (dy * dy);
								if (distSq <= nearestDistSq)
								{
									nearestDistSq = distSq;
									nearestZoneIndex = static_cast<Int>(zoneIdx);
								}
							}

							if (nearestZoneIndex >= 0)
							{
								const Real healthFraction = health / maxHealth;
								const Real damageFraction = damageDelta / maxHealth;
								const char* level = "low";
								if (healthFraction <= 0.35f || damageFraction >= 0.15f)
								{
									level = "high";
								}
								else if (damageFraction >= 0.05f)
								{
									level = "medium";
								}

								const AutonomyZone& threatZone = zones[static_cast<std::size_t>(nearestZoneIndex)];
								AutonomyZoneThreatState& threat = m_autonomy.state.zoneThreats[static_cast<UnsignedInt>(threatZone.anchorId)];
								const bool sameFreshThreat = threat.lastSeenTick != 0u && (threatNowTick - threat.lastSeenTick) <= 3000u;
								const int previousDamagedStructures = sameFreshThreat ? threat.damagedStructures : 0;
								const int previousDestroyedStructures = sameFreshThreat ? threat.destroyedStructures : 0;
								const int damagedStructures = previousDamagedStructures + 1;
								const int destroyedStructures = previousDestroyedStructures + (healthFraction <= 0.05f ? 1 : 0);
								const Real localEnemyRadius = std::max<Real>(450.0f, m_autonomy.state.zoneRadius * 1.75f);
								const Real artilleryRadius = std::max<Real>(1600.0f, m_autonomy.state.zoneRadius * 4.0f);
								const ZoneThreatEvidence evidence = collectZoneThreatEvidence(
									player,
									owned.object->getPosition()->x,
									owned.object->getPosition()->y,
									localEnemyRadius,
									artilleryRadius);
								const AIControlAdapterZoneThreatSourceResult source = AIControlAdapterClassifyZoneThreatSource({
									evidence.localEnemyCount,
									evidence.enemyArtilleryCount,
									evidence.recentWmd,
									static_cast<float>(damageFraction),
									static_cast<float>(damageDelta),
									damagedStructures,
									destroyedStructures
								});
								level = source.severity;
								const std::string sourceType = source.type;
								const std::string response = source.response;
								const std::string sourceReason = source.reason;
								threat.lastSeenTick = threatNowTick;
								threat.damagedObjectId = objectId;
								threat.positionX = owned.object->getPosition()->x;
								threat.positionY = owned.object->getPosition()->y;
								threat.damageDelta = damageDelta;
								threat.level = level;
								threat.sourceType = sourceType;
								threat.response = response;
								threat.reason = sourceReason;
								threat.localEnemyCount = evidence.localEnemyCount;
								threat.enemyArtilleryCount = evidence.enemyArtilleryCount;
								threat.recentWmd = evidence.recentWmd;
								threat.damagedStructures = damagedStructures;
								threat.destroyedStructures = destroyedStructures;
								adapterLog(
									"zone_threat_evidence zone=%u local_enemies=%d enemy_artillery=%d recent_wmd=%d damaged_structures=%d destroyed_structures=%d reason=%s",
									static_cast<unsigned int>(threatZone.anchorId),
									evidence.localEnemyCount,
									evidence.enemyArtilleryCount,
									evidence.recentWmd ? 1 : 0,
									damagedStructures,
									destroyedStructures,
									sourceReason.c_str());
								adapterLog(
									"zone_threat_source zone=%u type=%s severity=%s response=%s reason=%s",
									static_cast<unsigned int>(threatZone.anchorId),
									sourceType.c_str(),
									level,
									response.c_str(),
									sourceReason.c_str());
								zoneThreatsChanged = true;
							}
						}
					}

					m_autonomy.state.trackedStructureHealth[objectId] = AutonomyTrackedObjectHealth{ health, threatNowTick };
				}
			}

			for (auto it = m_autonomy.state.trackedStructureHealth.begin(); it != m_autonomy.state.trackedStructureHealth.end(); )
			{
				if (visibleStructureIds.find(it->first) == visibleStructureIds.end())
				{
					it = m_autonomy.state.trackedStructureHealth.erase(it);
				}
				else
				{
					++it;
				}
			}

			for (auto it = m_autonomy.state.zoneThreats.begin(); it != m_autonomy.state.zoneThreats.end(); )
			{
				if (threatNowTick - it->second.lastSeenTick > 45000u)
				{
					it = m_autonomy.state.zoneThreats.erase(it);
					zoneThreatsChanged = true;
				}
				else
				{
					++it;
				}
			}

			if (zoneThreatsChanged)
			{
				++m_autonomy.state.zonesSnapshotVersion;
				m_autonomy.state.zonesSnapshotTick = threatNowTick;
			}

			// Detect zone count changes (new zones created or destroyed)
			if (m_autonomy.state.telemetryZones.is_array() && m_autonomy.state.telemetryZones.size() != zones.size())
			{
				m_autonomy.state.telemetryZonesDirty = true;
			}

			// Only rebuild zone telemetry when zones have changed (dirty flag set)
			// This reduces overhead from ~2-10ms per macro tick to ~0.1ms when zones unchanged
			if (m_autonomy.state.telemetryZonesDirty)
			{
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

					// Phase 5.8: Zone development logic must handle non-supply zones
					// Different anchor types have different development requirements
					bool zoneNeedsFollowup = false;
					bool zoneDeveloped = false;

					if (zones[i].anchorType == ZoneAnchorType::SupplyStash || zones[i].anchorType == ZoneAnchorType::MainBase)
					{
						// Supply zones: require Supply Stash + infrastructure
						zoneNeedsFollowup =
							zoneSupply > 0
							&& (zoneTunnels < 1 || zoneBarracks < 1 || zoneArms < 1 || zoneStingers < 1);
						zoneDeveloped =
							zoneSupply > 0
							&& (zoneBarracks + zoneArms + zoneTunnels + zoneStingers) >= 3;
					}
					else if (zones[i].anchorType == ZoneAnchorType::CapturedStructure)
					{
						// Captured structure zones: require Tunnel + defense, Supply Stash optional
						zoneNeedsFollowup =
							zoneTunnels < 1 || zoneStingers < 1;
						zoneDeveloped =
							zoneTunnels >= 1 && zoneStingers >= 1;
					}
					else if (zones[i].anchorType == ZoneAnchorType::StrategicFoothold || zones[i].anchorType == ZoneAnchorType::MarketFoothold)
					{
						// Strategic/Market foothold zones: require Tunnel + Stinger minimum
						// Black Markets are follow-up goals only when safe
						zoneNeedsFollowup =
							zoneTunnels < 1 || zoneStingers < 1;
						zoneDeveloped =
							zoneTunnels >= 1 && zoneStingers >= 1;
					}
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
					const AIControlAdapterZoneTerrainResult terrainZone = AIControlAdapterApplyZoneTerrainFacts(
						terrainFacts,
						{ zones[i].center.x, zones[i].center.y, zoneRadius, zoneFrontDx, zoneFrontDy, zones[i].isMainBase });
					Real frontPointX = zonePoints.frontPoint.x;
					Real frontPointY = zonePoints.frontPoint.y;
					Real rearPointX = zonePoints.rearPoint.x;
					Real rearPointY = zonePoints.rearPoint.y;
					std::string frontSourceValue = frontSource;
					if (terrainZone.terrainLimited || terrainZone.hasEntrance)
					{
						frontPointX = terrainZone.frontPoint.x;
						frontPointY = terrainZone.frontPoint.y;
						rearPointX = terrainZone.rearPoint.x;
						rearPointY = terrainZone.rearPoint.y;
						if (!terrainZone.frontSource.empty() && terrainZone.frontSource != "fallback")
						{
							frontSourceValue = std::string("terrain_") + terrainZone.frontSource;
						}
					}
					if (terrainZone.hasEntrance)
					{
						adapterLog(
							"zone_entrance_selected zone=%u entrance=%s x=%.1f y=%.1f reason=main_base",
							static_cast<unsigned int>(zones[i].anchorId),
							terrainZone.entranceId.c_str(),
							frontPointX,
							frontPointY);
					}
					adapterLog(
						"zone_terrain_shape zone=%u anchor=%u naive_radius=%.1f effective_radius=%.1f limited=%d reason=%s",
						static_cast<unsigned int>(zones[i].anchorId),
						static_cast<unsigned int>(zones[i].anchorId),
						zoneRadius,
						terrainZone.effectiveRadius,
						terrainZone.terrainLimited ? 1 : 0,
						terrainZone.reason);
					adapterLog(
						"zone_terrain_points zone=%u front_x=%.1f front_y=%.1f rear_x=%.1f rear_y=%.1f source=%s reason=%s",
						static_cast<unsigned int>(zones[i].anchorId),
						frontPointX,
						frontPointY,
						rearPointX,
						rearPointY,
						frontSourceValue.c_str(),
						terrainZone.reason);
					m_autonomy.state.telemetryZones.push_back(nlohmann::json::object({
						{"anchor_id", static_cast<UnsignedInt>(zones[i].anchorId)},
						{"anchor_type", zoneAnchorTypeToString(zones[i].anchorType)},
						{"is_main_base", zones[i].isMainBase},
						{"active", isActiveZone},
						{"center_x", zones[i].center.x},
						{"center_y", zones[i].center.y},
						{"front_point_x", frontPointX},
						{"front_point_y", frontPointY},
						{"rear_point_x", rearPointX},
						{"rear_point_y", rearPointY},
						{"front_source", frontSourceValue},
						{"effective_radius", terrainZone.effectiveRadius},
						{"terrain_limited", terrainZone.terrainLimited},
						{"terrain_reason", terrainZone.reason},
						{"terrain_entrance_id", terrainZone.entranceId},
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
				++m_autonomy.state.zonesSnapshotVersion;
				m_autonomy.state.zonesSnapshotTick = ::GetTickCount();
				m_autonomy.state.telemetryZonesDirty = false;
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
			const Int totalZoneBlackMarkets = activeZoneCounts.blackMarkets + activeZoneCounts.blackMarketsInProgress;
			const Int totalZoneTunnels = activeZoneCounts.tunnels + activeZoneCounts.tunnelsInProgress;
			const Int totalZoneStingers = activeZoneCounts.stingers + activeZoneCounts.stingersInProgress;
			const bool hasCompletedPalace = counts.palaces > 0;
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
				else if (cmdString == "Game.QueueRocketBuggiesAllWarFactories")
				{
					ok = executeGameQueueRocketBuggiesAllWarFactories(message, outReason);
				}
				else if (cmdString == "Game.QueueScudLauncher")
				{
					nlohmann::json queuedMessage = message;
					if (!queuedMessage["args"].is_object())
					{
						queuedMessage["args"] = nlohmann::json::object();
					}
					const std::string scudTemplate = AIControlAdapterTemplateInferenceService::InferScudLauncherTemplate(player);
						adapterLog(
							"autonomy_scud_attempt player=%d side=%s base_side=%s template=%s",
							player != nullptr ? player->getPlayerIndex() : -1,
							player != nullptr ? player->getSide().str() : "",
							player != nullptr ? player->getBaseSide().str() : "",
							scudTemplate.c_str());
					queuedMessage["cmd"] = "Game.QueueUnit";
					queuedMessage["args"]["producer_kind"] = "arms_dealer";
					queuedMessage["args"]["unit_template"] = scudTemplate;
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
				else if (cmdString == "Game.AttackMove.DefendZoneSmart")
				{
					ok = executeGameAttackMoveDefendZoneSmart(message, outReason);
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
					const AIControlAdapterZoneTerrainResult terrainZone = AIControlAdapterApplyZoneTerrainFacts(
						terrainFacts,
						{ activeZone.center.x, activeZone.center.y, static_cast<float>(zoneRadius), static_cast<float>(zoneFrontDx), static_cast<float>(zoneFrontDy), activeZone.isMainBase });
					if (terrainZone.terrainLimited || terrainZone.hasEntrance)
					{
						zoneCenter.x = terrainZone.frontPoint.x;
						zoneCenter.y = terrainZone.frontPoint.y;
						zoneRadius = std::max<Real>(96.0f, terrainZone.effectiveRadius * 0.28f);
						adapterLog(
							"terrain_placement_choice template=%s zone=%u role=%s x=%.1f y=%.1f reason=%s",
							cmdString == "Game.BuildTunnelNetwork" ? "GLATunnelNetwork" : "GLAStingerSite",
							static_cast<unsigned int>(activeZone.anchorId),
							terrainZone.hasEntrance ? "chokepoint" : "front",
							zoneCenter.x,
							zoneCenter.y,
							terrainZone.reason);
					}
					else
					{
						const Real frontOffset = activeZone.isMainBase ? 0.45f : 0.72f;
						zoneCenter.x += zoneFrontDx * zoneRadius * frontOffset;
						zoneCenter.y += zoneFrontDy * zoneRadius * frontOffset;
						zoneRadius = std::max<Real>(96.0f, zoneRadius * 0.32f);
						adapterLog(
							"terrain_placement_choice template=%s zone=%u role=fallback x=%.1f y=%.1f reason=terrain_fallback",
							cmdString == "Game.BuildTunnelNetwork" ? "GLATunnelNetwork" : "GLAStingerSite",
							static_cast<unsigned int>(activeZone.anchorId),
							zoneCenter.x,
							zoneCenter.y);
					}
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
					const AIControlAdapterZoneTerrainResult terrainZone = AIControlAdapterApplyZoneTerrainFacts(
						terrainFacts,
						{ zone.center.x, zone.center.y, static_cast<float>(zoneRadius), static_cast<float>(zoneFrontDx), static_cast<float>(zoneFrontDy), zone.isMainBase });
					if (terrainZone.terrainLimited || terrainZone.hasEntrance)
					{
						zoneCenter.x = terrainZone.frontPoint.x;
						zoneCenter.y = terrainZone.frontPoint.y;
						zoneRadius = std::max<Real>(96.0f, terrainZone.effectiveRadius * 0.28f);
						adapterLog(
							"terrain_placement_choice template=%s zone=%u role=%s x=%.1f y=%.1f reason=%s",
							cmdString == "Game.BuildTunnelNetwork" ? "GLATunnelNetwork" : "GLAStingerSite",
							static_cast<unsigned int>(zone.anchorId),
							terrainZone.hasEntrance ? "chokepoint" : "front",
							zoneCenter.x,
							zoneCenter.y,
							terrainZone.reason);
					}
					else
					{
						const Real frontOffset = zone.isMainBase ? 0.45f : 0.72f;
						zoneCenter.x += zoneFrontDx * zoneRadius * frontOffset;
						zoneCenter.y += zoneFrontDy * zoneRadius * frontOffset;
						zoneRadius = std::max<Real>(96.0f, zoneRadius * 0.32f);
						adapterLog(
							"terrain_placement_choice template=%s zone=%u role=fallback x=%.1f y=%.1f reason=terrain_fallback",
							cmdString == "Game.BuildTunnelNetwork" ? "GLATunnelNetwork" : "GLAStingerSite",
							static_cast<unsigned int>(zone.anchorId),
							zoneCenter.x,
							zoneCenter.y);
					}
				}
				else if (hasAxis && (cmdString == "Game.BuildBarracksSmart" || cmdString == "Game.BuildArmsDealerSmart"))
				{
					const Real lineOffset = zone.isMainBase ? 0.18f : 0.38f;
					zoneCenter.x += zoneFrontDx * zoneRadius * lineOffset;
					zoneCenter.y += zoneFrontDy * zoneRadius * lineOffset;
					zoneRadius = std::max<Real>(120.0f, zoneRadius * 0.42f);
				}
				else if (hasAxis && cmdString == "Game.BuildPalaceSmart")
				{
					const AIControlAdapterZoneTerrainResult terrainZone = AIControlAdapterApplyZoneTerrainFacts(
						terrainFacts,
						{ zone.center.x, zone.center.y, static_cast<float>(zoneRadius), static_cast<float>(zoneFrontDx), static_cast<float>(zoneFrontDy), zone.isMainBase });
					if (terrainZone.terrainLimited || terrainZone.hasEntrance)
					{
						zoneCenter.x = terrainZone.rearPoint.x;
						zoneCenter.y = terrainZone.rearPoint.y;
						zoneRadius = std::max<Real>(160.0f, terrainZone.effectiveRadius * 0.42f);
						adapterLog(
							"terrain_placement_choice template=GLAPalace zone=%u role=rear x=%.1f y=%.1f reason=%s",
							static_cast<unsigned int>(zone.anchorId),
							zoneCenter.x,
							zoneCenter.y,
							terrainZone.reason);
					}
					else
					{
						const Real rearOffset = zone.isMainBase ? -0.12f : -0.28f;
						zoneCenter.x += zoneFrontDx * zoneRadius * rearOffset;
						zoneCenter.y += zoneFrontDy * zoneRadius * rearOffset;
						zoneRadius = std::max<Real>(160.0f, zoneRadius * 0.45f);
						adapterLog(
							"terrain_placement_choice template=GLAPalace zone=%u role=fallback x=%.1f y=%.1f reason=terrain_fallback",
							static_cast<unsigned int>(zone.anchorId),
							zoneCenter.x,
							zoneCenter.y);
					}
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

			auto isBuildCooldownReady = [&](const char* cmd) -> bool
			{
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

			unsigned int completedBlackMarkets = 0;
			unsigned int inProgressBlackMarkets = 0;
			unsigned int staleBlackMarketFoundations = 0;
			unsigned int blackMarketFoundationsNoBuilder = 0;
			int staleStrategicFoundations = 0;
			for (std::size_t i = 0; i < ownedObjects.size(); ++i)
			{
				const AutonomyOwnedObjectSnapshot& owned = ownedObjects[i];
				if (!owned.isStructure || owned.object == nullptr)
				{
					continue;
				}
				const ThingTemplate* templ = owned.object->getTemplate();
				if (templ == nullptr)
				{
					continue;
				}
				const std::string templName = templ->getName().str();
				if (templName != "GLABlackMarket")
				{
					continue;
				}
				if (owned.underConstruction)
				{
					const UnsignedInt foundationId = static_cast<UnsignedInt>(owned.object->getID());
					const auto stateIt = m_autonomy.state.strategicFoundationHealth.find(foundationId);
					const bool hasBuilder = static_cast<Int>(owned.object->getBuilderID()) > 0;
					const bool stopped = stateIt != m_autonomy.state.strategicFoundationHealth.end() && stateIt->second.stopIssued;
					const bool staleByProgress = stateIt != m_autonomy.state.strategicFoundationHealth.end()
						&& stateIt->second.lastProgressTick != 0u
						&& (now - stateIt->second.lastProgressTick) >= 60000u;
					const bool staleByReason = stateIt != m_autonomy.state.strategicFoundationHealth.end()
						&& (stateIt->second.reason == "stalled_no_builder"
							|| stateIt->second.reason == "stopped_stale_no_progress"
							|| stateIt->second.reason == "stopped_no_builder_timeout"
							|| stateIt->second.reason == "stopped_worker_dead");
					const bool stale = stopped || staleByProgress || staleByReason || !hasBuilder;
					if (!hasBuilder)
					{
						++blackMarketFoundationsNoBuilder;
					}
					if (stale)
					{
						++staleBlackMarketFoundations;
					}
					else
					{
						++inProgressBlackMarkets;
					}
					adapterLog(
						"income_foundation_health template=GLABlackMarket foundation=%u progress=%d healthy=%d stale=%d no_builder=%d reason=%s",
						foundationId,
						staleByProgress ? 0 : 1,
						stale ? 0 : 1,
						stale ? 1 : 0,
						hasBuilder ? 0 : 1,
						stale ? (!hasBuilder ? "no_active_builder" : "stale_no_progress") : "healthy_in_progress");
				}
				else
				{
					++completedBlackMarkets;
				}
			}
			for (const auto& foundationPair : m_autonomy.state.strategicFoundationHealth)
			{
				const AutonomyStrategicFoundationState& state = foundationPair.second;
				if (state.stopIssued ||
					state.reason == "stalled_no_builder" ||
					state.reason == "stopped_stale_no_progress" ||
					(state.lastProgressTick != 0u && (now - state.lastProgressTick) >= 60000u))
				{
					++staleStrategicFoundations;
				}
			}
			const AIControlAdapterStrategicSpendTelemetrySerializer strategicSpendTelemetrySerializer;
			strategicSpendTelemetrySerializer.RecordMarketFoundationCounts(
				m_autonomy.state.strategicSpendTelemetry,
				static_cast<int>(inProgressBlackMarkets),
				static_cast<int>(staleBlackMarketFoundations),
				staleStrategicFoundations);
			m_autonomy.state.strategicSpendTelemetry["market_foundations_no_builder"] = blackMarketFoundationsNoBuilder;
			const AIControlAdapterStrategicSpendSnapshotBuilder strategicSpendSnapshotBuilder;

			auto tryOpeningBarracksFallbackAfterSupplyFailure = [&](std::string& outReason) -> bool
			{
				if (counts.barracks > 0 || counts.barracksInProgress > 0)
				{
					return false;
				}
				if (!isBuildAttemptReady("Game.BuildBarracksSmart", counts.barracksInProgress))
				{
					return false;
				}
				if (money < 600u)
				{
					return false;
				}

				std::string fallbackReason;
				const bool fallbackIssued = tryMacroBuildWithFallback("Game.BuildBarracksSmart", false, fallbackReason);
				recordBuildAttempt("Game.BuildBarracksSmart", fallbackIssued, fallbackReason);
				if (fallbackIssued || !fallbackReason.empty())
				{
					outReason = fallbackReason;
				}
				return fallbackIssued;
			};

			if (macroDue)
			{
				std::string reason;
				bool issued = false;
				const std::string profile = AIControlAdapterUiUtils::NormalizeAsciiLower(m_autonomy.state.profile);
				const AIControlAdapterProfilePolicyConfig policyConfig = resolveAutonomyProfilePolicyConfig();
				const bool isBalancedSprawl = policyConfig.isBalancedSprawl;
				const bool isSprawlStyle = policyConfig.isSprawlStyle;
				const Int sprawlSupplyCap = policyConfig.sprawlSupplyCap;
				const Int sprawlTunnelCap = policyConfig.sprawlTunnelCap;
				const UnsignedInt reserveCash = policyConfig.reserveCash;
				const UnsignedInt blackMarketCost = 2500u;
				const bool openingInfrastructureReady = counts.supplyStashes >= 1 && counts.barracks >= 1 && counts.armsDealers >= 1;
				const Int effectiveTotalBlackMarkets = static_cast<Int>(completedBlackMarkets + inProgressBlackMarkets);
				Int stashZoneCount = 0;
				Int developedZoneCount = 0;
				Int remoteSupplyZoneCount = 0;
				Real supplyFootprintRadius = 0.0f;
				const bool hasMainZoneForFootprint = mainZoneIndex >= 0 && mainZoneIndex < static_cast<Int>(zones.size());
				Coord3D mainZoneCenter = {};
				if (hasMainZoneForFootprint)
				{
					mainZoneCenter = zones[static_cast<std::size_t>(mainZoneIndex)].center;
				}
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
						if (hasMainZoneForFootprint && zoneIdx < zones.size())
						{
							const Real dx = zones[zoneIdx].center.x - mainZoneCenter.x;
							const Real dy = zones[zoneIdx].center.y - mainZoneCenter.y;
							const Real dist = std::sqrt((dx * dx) + (dy * dy));
							supplyFootprintRadius = std::max<Real>(supplyFootprintRadius, dist);
							if (dist >= 1200.0f)
							{
								++remoteSupplyZoneCount;
							}
						}
					}
					if (zoneSupply > 0 && (zoneBarracks + zoneArms + zoneTunnels + zoneStingers) >= 3)
					{
						++developedZoneCount;
					}
				}
				const Int desiredZoneCount = std::max<Int>(1, sprawlSupplyCap);
				const auto activeThreatIt = hasActiveZone
					? m_autonomy.state.zoneThreats.find(static_cast<UnsignedInt>(activeZone.anchorId))
					: m_autonomy.state.zoneThreats.end();
				const bool activeZoneThreatened = activeThreatIt != m_autonomy.state.zoneThreats.end()
					&& (now - activeThreatIt->second.lastSeenTick) <= 45000u
					&& (activeThreatIt->second.response == "defend" || activeThreatIt->second.sourceType == "unit_attack");
				AIControlAdapterMacroBuildSnapshotBuilderInput macroSnapshotInput;
				macroSnapshotInput.policyConfig = policyConfig;
				macroSnapshotInput.money = money;
				macroSnapshotInput.blackMarketCost = blackMarketCost;
				macroSnapshotInput.hasCompletedPalace = hasCompletedPalace;
				macroSnapshotInput.hasActiveZone = hasActiveZone;
				macroSnapshotInput.activeZoneIsMainBase = hasActiveZone && activeZone.isMainBase;
				macroSnapshotInput.activeZoneThreatened = activeZoneThreatened;
				macroSnapshotInput.stashZoneCount = stashZoneCount;
				macroSnapshotInput.developedZoneCount = developedZoneCount;
				macroSnapshotInput.supplyFootprintRadius = static_cast<float>(supplyFootprintRadius);
				macroSnapshotInput.remoteSupplyZoneCount = remoteSupplyZoneCount;
				macroSnapshotInput.totalSupplyStashes = totalSupplyStashes;
				macroSnapshotInput.totalBarracks = totalBarracks;
				macroSnapshotInput.totalArmsDealers = totalArmsDealers;
				macroSnapshotInput.totalPalaces = counts.palaces;
				macroSnapshotInput.totalTunnels = totalTunnels;
				macroSnapshotInput.totalStingers = totalStingers;
				macroSnapshotInput.completedBlackMarkets = counts.blackMarkets;
				macroSnapshotInput.inProgressBlackMarkets = static_cast<int>(inProgressBlackMarkets);
				macroSnapshotInput.effectiveTotalBlackMarkets = effectiveTotalBlackMarkets;
				macroSnapshotInput.supplyStashesInProgress = counts.supplyStashesInProgress;
				macroSnapshotInput.barracksInProgress = counts.barracksInProgress;
				macroSnapshotInput.armsDealersInProgress = counts.armsDealersInProgress;
				macroSnapshotInput.palacesInProgress = counts.palacesInProgress;
				macroSnapshotInput.tunnelsInProgress = counts.tunnelsInProgress;
				macroSnapshotInput.stingersInProgress = counts.stingersInProgress;
				macroSnapshotInput.totalZoneSupplyStashes = totalZoneSupplyStashes;
				macroSnapshotInput.totalZoneBarracks = totalZoneBarracks;
				macroSnapshotInput.totalZoneArmsDealers = totalZoneArmsDealers;
				macroSnapshotInput.totalZoneTunnels = totalZoneTunnels;
				macroSnapshotInput.totalZoneStingers = totalZoneStingers;
				macroSnapshotInput.activeZoneTunnelsInProgress = activeZoneCounts.tunnelsInProgress;
				macroSnapshotInput.activeZoneStingersInProgress = activeZoneCounts.stingersInProgress;
				macroSnapshotInput.activeZoneBarracksInProgress = activeZoneCounts.barracksInProgress;
				macroSnapshotInput.activeZoneArmsDealersInProgress = activeZoneCounts.armsDealersInProgress;
				macroSnapshotInput.supplyBuildCooldownReady = isBuildCooldownReady("Game.BuildSupplyStashSmart");
				macroSnapshotInput.blackMarketBuildCooldownReady = isBuildCooldownReady("Game.BuildBlackMarketSmart");
				macroSnapshotInput.barracksBuildCooldownReady = isBuildCooldownReady("Game.BuildBarracksSmart");
				macroSnapshotInput.armsDealerBuildCooldownReady = isBuildCooldownReady("Game.BuildArmsDealerSmart");
				macroSnapshotInput.tunnelBuildCooldownReady = isBuildCooldownReady("Game.BuildTunnelNetwork");
				macroSnapshotInput.stingerBuildCooldownReady = isBuildCooldownReady("Game.BuildStingerSite");
				macroSnapshotInput.palaceBuildCooldownReady = isBuildCooldownReady("Game.BuildPalaceSmart");
				macroSnapshotInput.wantsBaselineTwoMarkets =
					AIControlAdapterProfilePolicyManager().WantsBaselineTwoMarkets(policyConfig);
				AIControlAdapterMacroBuildSnapshot macroBuildSnapshotBase =
					AIControlAdapterMacroBuildSnapshotBuilder().Build(macroSnapshotInput);
				const bool remoteZoneHasStash = macroBuildSnapshotBase.remoteZoneHasStash;
				const bool shouldThrottleExtraStashGrowth = macroBuildSnapshotBase.shouldThrottleExtraStashGrowth;
				const bool shouldPreserveReserve = macroBuildSnapshotBase.shouldPreserveReserve;
				const bool balancedZoneCanAddPalace = !isBalancedSprawl || !hasActiveZone || totalZonePalaces < 1;
				const AIControlAdapterMacroBuildManager macroBuildManager;
				const AIControlAdapterMacroBuildPolicyDecisions macroBuildPolicyDecisions = macroBuildManager.ResolvePolicyDecisions(macroBuildSnapshotBase);
				const AIControlAdapterMacroExpansionDecision& macroExpansion = macroBuildPolicyDecisions.macroExpansion;
				const AIControlAdapterZoneSeedPackageDecision& zoneSeedDecision = macroBuildPolicyDecisions.zoneSeedDecision;
				const bool remoteZoneNeedsFollowup = macroBuildPolicyDecisions.remoteZoneNeedsFollowup;
				const Int sprawlDesiredMarketCount = macroBuildPolicyDecisions.sprawlDesiredMarketCount;
				const bool canAttemptBlackMarketNow = macroBuildPolicyDecisions.canAttemptBlackMarketNow;
				const bool shouldForceEcoRecovery = macroBuildPolicyDecisions.shouldForceEcoRecovery;
				const Int zoneGap = macroExpansion.zoneGap;
				const bool reserveProtected = macroExpansion.reserveProtected;
				const unsigned int cashAboveReserve = macroExpansion.cashAboveReserve;
				const bool allowUrgentExpansionDespiteReserve = macroExpansion.allowUrgentExpansionDespiteReserve;
				const bool zoneExpansionIsUrgent = macroExpansion.zoneExpansionUrgent;
				AIControlAdapterStrategicSpendEconomyFacts macroSpendEconomy;
				macroSpendEconomy.money = money;
				macroSpendEconomy.reserveCash = reserveCash;
				macroSpendEconomy.completedMarkets = static_cast<int>(completedBlackMarkets);
				macroSpendEconomy.healthyMarketsInProgress = static_cast<int>(inProgressBlackMarkets);
				macroSpendEconomy.staleMarketFoundations = static_cast<int>(staleBlackMarketFoundations);
				macroSpendEconomy.staleStrategicFoundations = staleStrategicFoundations;
				macroSpendEconomy.activeWmdThreats = m_autonomy.wmdTargetTracker.hasActiveWMDThreat() ? 1 : 0;
				macroSpendEconomy.mainBaseCritical =
					m_autonomy.state.mainBaseCriticalOverrideTelemetry.is_object()
					&& m_autonomy.state.mainBaseCriticalOverrideTelemetry.value("active", false);
				AIControlAdapterStrategicSpendZoneFacts macroSpendZones;
				macroSpendZones.currentZones = stashZoneCount;
				macroSpendZones.developedZones = developedZoneCount;
				macroSpendZones.desiredZones = desiredZoneCount;
				AIControlAdapterStrategicSpendArmyFacts macroSpendArmy;
				macroSpendArmy.armySize = counts.mobileUnits;
				macroSpendArmy.armyCap = std::max(1, counts.mobileUnits);
				macroSpendArmy.quads = counts.quads;
				macroSpendArmy.buggies = counts.rocketBuggies;
				macroSpendArmy.scorpions = counts.scorpions;
				const AIControlAdapterStrategicSpendSnapshotBase macroSpendBase =
					strategicSpendSnapshotBuilder.BuildBase(macroSpendEconomy, macroSpendZones, macroSpendArmy);
				auto trySupplyExpansionBuild = [&](std::string& outReason) -> bool
				{
					nlohmann::json args = nlohmann::json::object();
					const AIControlAdapterRemoteSupplyStageDecision& remoteSupplyStage = macroBuildPolicyDecisions.remoteSupplyStage;
					if (remoteSupplyStage.allowRemote && hasMainZoneForFootprint)
					{
						args["prefer_remote"] = true;
						args["remote_stage"] = remoteSupplyStage.stage;
						args["remote_min_distance"] = remoteSupplyStage.minDistance;
						args["remote_max_distance"] = remoteSupplyStage.maxDistance;
						args["remote_origin"] = nlohmann::json::object({
							{"x", mainZoneCenter.x},
							{"y", mainZoneCenter.y}
						});
					}
					return tryCommand("auto_macro", "Game.BuildSupplyStashSmart", args, outReason);
				};
				AIControlAdapterStrategicSpendSnapshotOverrides macroSpendOverrides;
				macroSpendOverrides.expansionUrgent = zoneExpansionIsUrgent;
				macroSpendOverrides.incomeCritical = completedBlackMarkets == 0u;
				macroSpendOverrides.reserveDepleted = money < reserveCash;
				const AIControlAdapterStrategicSpendSnapshot macroSpendSnapshot =
					strategicSpendSnapshotBuilder.Build(macroSpendBase, macroSpendOverrides);
				const AIControlAdapterStrategicSpendManager strategicSpendManager;
				const AIControlAdapterStrategicSpendPlan macroSpendPlan =
					strategicSpendManager.EvaluateMacroPlan(macroSpendSnapshot, 1800u, blackMarketCost, blackMarketCost, 5000u, 1200u);
				for (const AIControlAdapterStrategicSpendRecord& spendRecord : macroSpendPlan.records)
				{
					const AIControlAdapterStrategicSpendDecision& decision = spendRecord.decision;
					adapterLog(
						"%s",
						strategicSpendTelemetrySerializer.BuildPolicyLogLine({
							spendRecord.category,
							money,
							reserveCash,
							&decision
						}).c_str());
					strategicSpendTelemetrySerializer.RecordCategory(
						m_autonomy.state.strategicSpendTelemetry,
						spendRecord.category,
						spendRecord.requestCost,
						decision);
				}

				// Evaluate zone expansion arbitration
				AIControlAdapterZoneExpansionArbitrationResult expansionDecision = macroBuildPolicyDecisions.expansionDecision;
				const AIControlAdapterStrategicSpendDecision expansionSpend = macroSpendPlan.expansion;
				if (expansionDecision.shouldAttemptExpansion && !expansionSpend.allowed)
				{
					expansionDecision.shouldAttemptExpansion = false;
					expansionDecision.command = nullptr;
					expansionDecision.reason = expansionSpend.reason;
					adapterLog(
						"strategic_spend_competition winner=none blocked=expansion money=%lu reserve=%lu reason=%s",
						static_cast<unsigned long>(money),
						static_cast<unsigned long>(reserveCash),
						expansionSpend.reason);
				}

				const char* requiredOpeningBuild = AIControlAdapterGetRequiredOpeningBuild({
					counts.supplyStashes,
					counts.barracks,
					counts.armsDealers
				});
				const AIControlAdapterStrategicSpendDecision marketRecoverySpend = macroSpendPlan.economyRecovery;
				const AIControlAdapterStrategicSpendDecision marketGrowthSpend = macroSpendPlan.economyGrowth;
				const AIControlAdapterStrategicSpendDecision palaceSpend = macroSpendPlan.techPrerequisite;
				const AIControlAdapterStrategicSpendDecision staticDefenseSpend = macroSpendPlan.staticDefense;
				AIControlAdapterMacroBuildSnapshotRuntimeInput macroRuntimeInput;
				macroRuntimeInput.expansionSpend = { expansionSpend.allowed, expansionSpend.reason };
				macroRuntimeInput.marketRecoverySpend = { marketRecoverySpend.allowed, marketRecoverySpend.reason };
				macroRuntimeInput.marketGrowthSpend = { marketGrowthSpend.allowed, marketGrowthSpend.reason };
				macroRuntimeInput.palaceSpend = { palaceSpend.allowed, palaceSpend.reason };
				macroRuntimeInput.staticDefenseSpend = { staticDefenseSpend.allowed, staticDefenseSpend.reason };
				macroRuntimeInput.zoneSeedCooldownReady = zoneSeedDecision.command != nullptr ? isBuildCooldownReady(zoneSeedDecision.command) : false;
				const AIControlAdapterMacroBuildSnapshotBuilder macroSnapshotBuilder;
				AIControlAdapterMacroBuildSnapshot macroBuildSnapshot =
					macroSnapshotBuilder.ApplyRuntimeInput(macroBuildSnapshotBase, macroRuntimeInput);
				AIControlAdapterMacroBuildPlan macroBuildPlan = macroBuildManager.BuildPlan(macroBuildSnapshot);
				const AIControlAdapterMacroBuildTelemetry& macroBuildTelemetry = macroBuildPlan.telemetry;
				const AIControlAdapterMacroBuildTelemetrySerializer macroTelemetrySerializer;
				for (const std::string& line : macroTelemetrySerializer.BuildExpansionLogLines({
					&macroBuildTelemetry,
					reserveCash,
					hasActiveZone ? static_cast<unsigned int>(activeZone.anchorId) : 0u,
					remoteZoneHasStash
				}))
				{
					adapterLog("%s", line.c_str());
				}
				m_autonomy.state.staticDefenseTelemetry = nlohmann::json::array();
				m_autonomy.state.palaceRedundancyTelemetry = nlohmann::json::array();
				Int staticDefenseZoneIndex = -1;
				std::string staticDefenseCommand;
				std::string staticDefenseReason = "no_candidate";
				Int palaceRedundancyZoneIndex = -1;
				std::string palaceRedundancyReason = "no_candidate";
				int staticDefenseGap = 0;
				int localWorkerGap = 0;
				auto countIdleWorkersNearZone = [&](const AutonomyZone& zone) -> int
				{
						const Real radius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
					const Real radiusSq = radius * radius;
					int localIdle = 0;
					for (const AutonomyOwnedObjectSnapshot& owned : ownedObjects)
					{
						if (!owned.isDozer || owned.object == nullptr || owned.underConstruction)
						{
							continue;
						}
						const Coord3D* pos = owned.object->getPosition();
						if (pos == nullptr || !isWorkerAvailableForNewBuild(owned.object))
						{
							continue;
						}
						const Real dx = pos->x - zone.center.x;
						const Real dy = pos->y - zone.center.y;
						if (dx * dx + dy * dy <= radiusSq)
						{
							++localIdle;
						}
					}
					return localIdle;
				};
				for (std::size_t zoneIdx = 0; zoneIdx < zones.size(); ++zoneIdx)
				{
					const AutonomyZone& zone = zones[zoneIdx];
					const AutonomyZoneCounts& zc = zoneCounts[zoneIdx];
					const UnsignedInt zoneAnchor = static_cast<UnsignedInt>(zone.anchorId);
					const bool zoneIsActive = hasActiveZone && zoneAnchor == static_cast<UnsignedInt>(activeZone.anchorId);
					const auto threatIt = m_autonomy.state.zoneThreats.find(zoneAnchor);
					const bool repeatedAttack = threatIt != m_autonomy.state.zoneThreats.end()
						&& (now - threatIt->second.lastSeenTick) <= 45000u
						&& (threatIt->second.damagedStructures >= 2 || threatIt->second.destroyedStructures > 0);
					const std::string zoneRole = classifyZoneRole(zone, zc, zoneIsActive, repeatedAttack);
					const bool isAnchorZone = zoneRole == "anchor";
					const bool isFrontier = zoneRole == "frontier";
					const bool isDeveloped = zoneRole == "developed_rear" || zoneRole == "active" || zoneRole == "frontier" || zoneRole == "anchor";
					const int reservedTunnels = countBuildTasksNearZone("TunnelNetwork", zone, std::max<Real>(220.0f, m_autonomy.state.zoneRadius * 1.25f));
					const int reservedStingers = countBuildTasksNearZone("StingerSite", zone, std::max<Real>(220.0f, m_autonomy.state.zoneRadius * 1.25f));
					const AIControlAdapterStaticDefensePolicyResult staticPolicy = AIControlAdapterChooseStaticDefensePolicy({
						zoneRole,
						zone.isMainBase,
						isDeveloped,
						isFrontier,
						zoneIsActive,
						isAnchorZone,
						repeatedAttack,
						zc.tunnels,
						zc.stingers,
						zc.tunnelsInProgress,
						zc.stingersInProgress,
						reservedTunnels,
						reservedStingers
					});
					const int staticInProgress = zc.tunnelsInProgress + zc.stingersInProgress + reservedTunnels + reservedStingers;
					staticDefenseGap += std::max<int>(0, staticPolicy.desiredTunnels - staticPolicy.effectiveTunnels);
					staticDefenseGap += std::max<int>(0, staticPolicy.desiredStingers - staticPolicy.effectiveStingers);
					const bool hasLocalStrategicTask = counts.palacesInProgress > 0 &&
						(zc.palaces > 0 || zc.supplyStashes > 0 || zoneIsActive);
					const int desiredLocalWorkers = hasLocalStrategicTask ? 3 : (zoneIsActive ? 2 : (isDeveloped ? 1 : 0));
					if (desiredLocalWorkers > 0)
					{
						localWorkerGap += std::max<int>(0, desiredLocalWorkers - countIdleWorkersNearZone(zone));
					}
					const AIControlAdapterStaticDefensePolicyTelemetryInput staticTelemetryInput{
						zoneAnchor,
						&staticPolicy,
						staticInProgress
					};
					adapterLog("%s", macroTelemetrySerializer.BuildStaticDefensePolicyLogLine(staticTelemetryInput).c_str());
					m_autonomy.state.staticDefenseTelemetry.push_back(
						macroTelemetrySerializer.BuildStaticDefensePolicyTelemetry(staticTelemetryInput));
					if (staticDefenseZoneIndex < 0)
					{
						const AIControlAdapterStaticDefenseCandidateDecision staticCandidate =
							macroBuildManager.EvaluateStaticDefenseCandidate({
								&staticPolicy,
								zoneExpansionIsUrgent,
								shouldPreserveReserve,
								isBalancedSprawl,
								money,
								isBuildAttemptReady("Game.BuildStingerSite", counts.stingersInProgress),
								isBuildAttemptReady("Game.BuildTunnelNetwork", counts.tunnelsInProgress)
							});
						if (staticCandidate.shouldBuild)
						{
							staticDefenseZoneIndex = static_cast<Int>(zoneIdx);
							staticDefenseCommand = staticCandidate.command != nullptr ? staticCandidate.command : "";
							staticDefenseReason = staticCandidate.reason != nullptr ? staticCandidate.reason : "no_candidate";
						}
					}

					const AIControlAdapterPalaceRedundancyResult palacePolicy = AIControlAdapterEvaluatePalaceRedundancy({
						zoneRole,
						hasCompletedPalace,
						isDeveloped,
						isFrontier,
						isAnchorZone,
						reserveProtected,
						zoneExpansionIsUrgent,
						cashAboveReserve,
						counts.palaces,
						counts.palacesInProgress,
						zc.palaces,
						zc.palacesInProgress
					});
					const AIControlAdapterPalaceRedundancyTelemetryInput palaceTelemetryInput{
						zoneAnchor,
						&palacePolicy,
						zc.palaces,
						zc.palacesInProgress + counts.palacesInProgress
					};
					adapterLog("%s", macroTelemetrySerializer.BuildPalaceRedundancyLogLine(palaceTelemetryInput).c_str());
					m_autonomy.state.palaceRedundancyTelemetry.push_back(
						macroTelemetrySerializer.BuildPalaceRedundancyTelemetry(palaceTelemetryInput));
					if (palaceRedundancyZoneIndex < 0)
					{
						const AIControlAdapterPalaceRedundancyCandidateDecision palaceCandidate =
							macroBuildManager.EvaluatePalaceRedundancyCandidate({
								&palacePolicy,
								isBuildAttemptReady("Game.BuildPalaceSmart", counts.palacesInProgress)
							});
						if (palaceCandidate.shouldBuild)
						{
							palaceRedundancyZoneIndex = static_cast<Int>(zoneIdx);
							palaceRedundancyReason = palaceCandidate.reason != nullptr ? palaceCandidate.reason : "no_candidate";
						}
					}
				}
				macroRuntimeInput.staticDefenseZoneIndex = staticDefenseZoneIndex;
				macroRuntimeInput.staticDefenseCommand = !staticDefenseCommand.empty() ? staticDefenseCommand.c_str() : nullptr;
				macroRuntimeInput.staticDefenseReason = staticDefenseReason.c_str();
				macroRuntimeInput.palaceRedundancyZoneIndex = palaceRedundancyZoneIndex;
				macroRuntimeInput.palaceRedundancyReason = palaceRedundancyReason.c_str();
				macroBuildSnapshot = macroSnapshotBuilder.ApplyRuntimeInput(macroBuildSnapshotBase, macroRuntimeInput);
				macroBuildPlan = macroBuildManager.BuildPlan(macroBuildSnapshot);
				AIControlAdapterMacroBuildTelemetryInput macroTelemetryInput;
				macroTelemetryInput.profile = profile;
				macroTelemetryInput.money = money;
				macroTelemetryInput.reserveCash = reserveCash;
				macroTelemetryInput.cashAboveReserve = cashAboveReserve;
				macroTelemetryInput.currentZones = stashZoneCount;
				macroTelemetryInput.developedZones = developedZoneCount;
				macroTelemetryInput.desiredZones = desiredZoneCount;
				macroTelemetryInput.zoneGap = zoneGap;
				macroTelemetryInput.activeZoneAnchor = hasActiveZone ? static_cast<unsigned int>(activeZone.anchorId) : 0u;
				macroTelemetryInput.activeZoneThreatened = activeZoneThreatened;
				macroTelemetryInput.remoteZoneNeedsFollowup = remoteZoneNeedsFollowup;
				macroTelemetryInput.macroBuildPlan = &macroBuildPlan;
				macroTelemetryInput.spendPlan = &macroSpendPlan;
				m_autonomy.state.macroBuildTelemetry =
					macroTelemetrySerializer.BuildTelemetry(macroTelemetryInput);
				int criticalZoneCount = 0;
				for (const auto& threatPair : m_autonomy.state.zoneThreats)
				{
					const AutonomyZoneThreatState& threat = threatPair.second;
					if ((now - threat.lastSeenTick) <= 45000u && threat.level == "critical")
					{
						++criticalZoneCount;
					}
				}
				int defenseReserveDeficits = 0;
				for (const auto& reservePair : m_autonomy.state.zoneDefenseReserves)
				{
					if (reservePair.second.deficit > 0)
					{
						++defenseReserveDeficits;
					}
					}
					const int activeCombatTaskCount = m_autonomy.combatTaskManager.getActiveTaskCount();
					const int enemyWmdTargetCount = static_cast<int>(m_autonomy.wmdTargetTracker.getAllTargets().size());
					const int readyScudStormCount = countReadyScudStorms(player);
					AIControlAdapterSurvivalPolicyInput survivalInput;
					survivalInput.criticalZoneCount = criticalZoneCount;
					survivalInput.defenseReserveDeficits = defenseReserveDeficits;
					survivalInput.activeCombatTasks = activeCombatTaskCount;
					survivalInput.readyScudStorms = readyScudStormCount;
					survivalInput.enemyWmdTargets = enemyWmdTargetCount;
					survivalInput.readyBarracks = counts.barracks;
					survivalInput.readyArmsDealers = counts.armsDealers;
					survivalInput.money = static_cast<unsigned int>(money);
					survivalInput.reserveCash = static_cast<unsigned int>(reserveCash);
					const AIControlAdapterSurvivalPolicyResult survivalPolicy =
						AIControlAdapterSurvivalPolicyManager().Evaluate(survivalInput);
					m_autonomy.state.survivalPolicyTelemetry = nlohmann::json::object({
						{"state", survivalPolicy.state},
						{"priority", survivalPolicy.priority},
						{"reason", survivalPolicy.reason},
						{"critical_zones", criticalZoneCount},
						{"defense_reserve_deficits", defenseReserveDeficits},
						{"active_combat_tasks", activeCombatTaskCount},
						{"ready_scuds", readyScudStormCount},
						{"enemy_wmd_targets", enemyWmdTargetCount},
						{"ready_barracks", counts.barracks},
						{"ready_arms_dealers", counts.armsDealers},
					{"money", money},
					{"reserve", reserveCash},
					{"cash_float", cashAboveReserve},
					{"block_exposed_wmd_foundations", survivalPolicy.blockExposedWmdFoundations}
				});
				adapterLog(
					"survival_policy state=%s priority=%s reason=%s critical_zones=%d defense_deficits=%d active_tasks=%d ready_scuds=%d enemy_wmd=%d producers=%d/%d money=%lu reserve=%lu",
					survivalPolicy.state,
					survivalPolicy.priority,
					survivalPolicy.reason,
						criticalZoneCount,
						defenseReserveDeficits,
						activeCombatTaskCount,
						readyScudStormCount,
						enemyWmdTargetCount,
					counts.barracks,
					counts.armsDealers,
					static_cast<unsigned long>(money),
					static_cast<unsigned long>(reserveCash));
				int garrisonGap = 0;
				if (m_autonomy.state.garrisonTelemetry.is_array())
				{
					for (const auto& entry : m_autonomy.state.garrisonTelemetry)
					{
						if (entry.is_object() && entry.value("state", std::string()) != "assigned")
						{
							++garrisonGap;
						}
					}
				}
				int staleFoundations = 0;
				for (const auto& pair : m_autonomy.state.strategicFoundationHealth)
				{
					if (isStoppedStrategicFoundation(pair.first))
					{
						++staleFoundations;
					}
				}
				bool mainUnderPressure = false;
				bool emergencyUnitAttack = false;
				for (const AutonomyZone& zone : zones)
				{
					if (!zone.isMainBase)
					{
						continue;
					}
					const auto threatIt = m_autonomy.state.zoneThreats.find(static_cast<UnsignedInt>(zone.anchorId));
					if (threatIt == m_autonomy.state.zoneThreats.end() || (now - threatIt->second.lastSeenTick) > 45000u)
					{
						continue;
					}
					if (threatIt->second.response == "defend" || threatIt->second.sourceType == "unit_attack")
					{
						mainUnderPressure = true;
						emergencyUnitAttack = true;
					}
					break;
				}
				int mobileSiegeThreats = 0;
				if (m_autonomy.state.counterbatteryTelemetry.is_object())
				{
					const auto threatsIt = m_autonomy.state.counterbatteryTelemetry.find("mobile_siege_threats");
					if (threatsIt != m_autonomy.state.counterbatteryTelemetry.end() && threatsIt->is_array())
					{
						mobileSiegeThreats = static_cast<int>(threatsIt->size());
					}
				}
				const int expansionGap = std::max<Int>(0, zoneGap);
				const int normalAttackCount = counts.soldiers + counts.rpg + counts.quads + counts.scorpions;
				const bool normalAttackReady = normalAttackCount >= m_automation.attackRule.minUnits;
				const AIControlAdapterBrutalPressureResult brutalPressure = AIControlAdapterChooseBrutalPressurePriority({
					expansionGap,
					mainUnderPressure,
					staticDefenseGap,
					garrisonGap,
					localWorkerGap,
					staleFoundations,
					mobileSiegeThreats,
					reserveProtected,
					cashAboveReserve,
					normalAttackReady,
					emergencyUnitAttack
				});
				const AIControlAdapterBrutalPressureTelemetryInput brutalPressureTelemetryInput{
					expansionGap,
					mainUnderPressure,
					staticDefenseGap,
					garrisonGap,
					localWorkerGap,
					staleFoundations,
					mobileSiegeThreats,
					reserveProtected,
					cashAboveReserve,
					&brutalPressure
				};
				m_autonomy.state.brutalPressureTelemetry =
					macroTelemetrySerializer.BuildBrutalPressureTelemetry(brutalPressureTelemetryInput);
				adapterLog("%s", macroTelemetrySerializer.BuildBrutalPressureLogLine(brutalPressureTelemetryInput).c_str());
				std::string chosenCommand = "none";
				const bool enemyWmdThreat = m_autonomy.wmdTargetTracker.hasActiveWMDThreat();
				const AIControlAdapterPalaceRecoveryDecision palaceRecovery = AIControlAdapterChoosePalaceRecovery({
					counts.palaces,
					counts.palacesInProgress,
					enemyWmdThreat,
					mobileSiegeThreats > 0,
					money,
					2500u,
					isBuildAttemptReady("Game.BuildPalaceSmart", counts.palacesInProgress)
				});
				const AIControlAdapterPalaceRecoveryCandidateDecision palaceRecoveryCandidate =
					macroBuildManager.EvaluatePalaceRecoveryCandidate({
						&palaceRecovery,
						openingInfrastructureReady
					});
				if (palaceRecoveryCandidate.shouldAttempt && palaceRecoveryCandidate.command != nullptr)
				{
					chosenCommand = palaceRecoveryCandidate.command;
					issued = tryMacroBuildWithFallback(palaceRecoveryCandidate.command, false, reason);
					recordBuildAttempt(palaceRecoveryCandidate.command, issued, reason);
				}
				const AIControlAdapterPalaceRecoveryResultDecision palaceRecoveryResult =
					macroBuildManager.EvaluatePalaceRecoveryResult({
						&palaceRecovery,
						openingInfrastructureReady,
						chosenCommand == "Game.BuildPalaceSmart",
						issued
					});
				adapterLog(
					"palace_recovery_policy live=%d in_progress=%d issued=%d reason=%s",
					counts.palaces,
					counts.palacesInProgress,
					palaceRecoveryResult.issued ? 1 : 0,
					palaceRecoveryResult.reason);
				auto tryMacroBuildIntentScheduler = [&]() -> bool
				{
					for (const AIControlAdapterMacroBuildIntent& intent : macroBuildPlan.intents)
					{
						adapterLog("%s", macroTelemetrySerializer.BuildIntentLogLine(intent, money).c_str());
					}
					const AIControlAdapterMacroBuildDispatcher dispatcher;
					const AIControlAdapterMacroBuildDispatchRequest dispatch = dispatcher.BuildDispatch(macroBuildPlan);
					if (!dispatch.shouldDispatch)
					{
						return false;
					}

					chosenCommand = dispatch.command;
					if (dispatch.kind == AIControlAdapterMacroBuildDispatchKind::SpecificZone
						&& dispatch.zoneIndex >= 0
						&& dispatch.zoneIndex < static_cast<int>(zones.size()))
					{
						issued = trySpecificZoneCommand(
							dispatch.taskName.c_str(),
							dispatch.command.c_str(),
							zones[static_cast<std::size_t>(dispatch.zoneIndex)],
							nlohmann::json::object(),
							reason);
					}
					else if (dispatch.kind == AIControlAdapterMacroBuildDispatchKind::SupplyExpansion)
					{
						issued = trySupplyExpansionBuild(reason);
					}
					else
					{
						issued = tryMacroBuildWithFallback(dispatch.command.c_str(), dispatch.preferZone, reason);
					}
					const AIControlAdapterMacroBuildDispatchResult dispatchResult =
						dispatcher.CompleteDispatch(dispatch, issued, reason);
					reason = dispatchResult.reason;
					recordBuildAttempt(dispatchResult.command.c_str(), issued, reason);
					adapterLog("%s", macroTelemetrySerializer.BuildDispatchResultLogLine(dispatchResult).c_str());
					if (issued
						&& hasActiveZone
						&& (dispatchResult.command == "Game.BuildTunnelNetwork"
							|| dispatchResult.command == "Game.BuildStingerSite")
						&& (activeZone.anchorType == ZoneAnchorType::CapturedStructure
							|| activeZone.anchorType == ZoneAnchorType::StrategicFoothold
							|| activeZone.anchorType == ZoneAnchorType::MarketFoothold))
					{
						adapterLog(
							"non_supply_zone_development anchor=%u type=%s command=%s issued=1 reason=%s",
							static_cast<unsigned int>(activeZone.anchorId),
							zoneAnchorTypeToString(activeZone.anchorType),
							dispatchResult.command.c_str(),
							dispatchResult.command == "Game.BuildTunnelNetwork" ? "zone_infrastructure" : "zone_defense");
					}
					return true;
				};
				if (chosenCommand == "Game.BuildPalaceSmart")
				{
					// Emergency Palace recovery intentionally leaves unit production to the production tick.
				}
				else
				{
					const AIControlAdapterOpeningBuildDecision openingDecision =
						macroBuildManager.EvaluateOpeningBuild({
							requiredOpeningBuild,
							totalSupplyStashes < 2
								&& AIControlAdapterProfilePolicyManager().WantsAcceleratedOpeningSupply(
									policyConfig,
									static_cast<float>(m_autonomy.state.expansionBias)),
							money,
							counts.barracks,
							counts.supplyStashesInProgress,
							counts.barracksInProgress,
							counts.armsDealersInProgress,
							totalSupplyStashes,
							isBuildAttemptReady("Game.BuildSupplyStashSmart", counts.supplyStashesInProgress),
							isBuildAttemptReady("Game.BuildBarracksSmart", counts.barracksInProgress),
							isBuildAttemptReady("Game.BuildArmsDealerSmart", counts.armsDealersInProgress)
						});
					if (openingDecision.handled)
					{
						chosenCommand = openingDecision.displayCommand != nullptr ? openingDecision.displayCommand : "";
						if (openingDecision.command != nullptr)
						{
							if (openingDecision.executor == AIControlAdapterMacroBuildExecutor::SupplyExpansion)
							{
								issued = trySupplyExpansionBuild(reason);
							}
							else
							{
								issued = tryMacroBuildWithFallback(openingDecision.command, openingDecision.preferZone, reason);
							}
							recordBuildAttempt(openingDecision.command, issued, reason);
							if (!issued
								&& openingDecision.allowBarracksFallbackAfterSupplyFailure
								&& reason != "no_money"
								&& reason != "opening_wait_money"
								&& tryOpeningBarracksFallbackAfterSupplyFailure(reason))
							{
								chosenCommand = "Game.BuildBarracksSmart";
								issued = true;
							}
						}
						else
						{
							reason = openingDecision.reason != nullptr ? openingDecision.reason : "";
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
							&& palaceSpend.allowed
							&& money >= 5000u)
						{
							chosenCommand = "Game.BuildPalaceSmart";
							issued = tryMacroBuildWithFallback("Game.BuildPalaceSmart", false, reason);
							recordBuildAttempt("Game.BuildPalaceSmart", issued, reason);
						}
					}
					else if (tryMacroBuildIntentScheduler())
					{
						// Intent scheduler handled this macro build tick.
					}
				}

				// Phase 5.8: Non-supply expansion path for strategic/market footholds
				// When supply expansion is blocked and economy is strong, allow Tunnel/Stinger foothold zones
				const AIControlAdapterNonSupplyFootholdDecision footholdDecision =
					macroBuildManager.EvaluateNonSupplyFoothold({
						issued,
						isSprawlStyle,
						stashZoneCount,
						desiredZoneCount,
						allowUrgentExpansionDespiteReserve,
						reason.c_str(),
						hasCompletedPalace,
						static_cast<int>(completedBlackMarkets),
						money,
						totalTunnels,
						sprawlTunnelCap,
						counts.tunnelsInProgress,
						isBuildAttemptReady("Game.BuildTunnelNetwork", counts.tunnelsInProgress)
					});
				if (footholdDecision.shouldEvaluatePlacement)
				{
					const ZoneAnchorType footholdType = footholdDecision.anchorType;

					// Find candidate foothold location
					std::string footholdReason;
					const Coord3D footholdCandidate = findFootholdPlacement(footholdReason);

					if (footholdReason == "valid_foothold_location")
					{
						// Check if we already have a pending foothold at this location
						bool duplicateFoothold = false;
						for (std::size_t i = 0; i < m_autonomy.state.pendingFootholdAnchors.size(); ++i)
						{
							const AutonomyState::PendingFootholdAnchor& existing = m_autonomy.state.pendingFootholdAnchors[i];
							const float dx = existing.targetX - footholdCandidate.x;
							const float dy = existing.targetY - footholdCandidate.y;
							const float distSq = (dx * dx) + (dy * dy);
							if (distSq < (100.0f * 100.0f))
							{
								duplicateFoothold = true;
								break;
							}
						}

						if (!duplicateFoothold)
						{
							// Prefer Tunnel Network as first foothold structure
							if (footholdDecision.shouldAttemptTunnel)
							{
								chosenCommand = "Game.BuildTunnelNetwork";
								issued = tryMacroBuildWithFallback("Game.BuildTunnelNetwork", true, reason);
								recordBuildAttempt("Game.BuildTunnelNetwork", issued, reason);

								if (issued)
								{
									// Create pending foothold anchor
									AutonomyState::PendingFootholdAnchor pending;
									pending.syntheticAnchorId = m_autonomy.state.nextSyntheticAnchorId++;
									pending.anchorType = footholdType;
									pending.targetX = footholdCandidate.x;
									pending.targetY = footholdCandidate.y;
									pending.createdTick = GetTickCount();
									pending.buildIssued = true;
									pending.buildCommand = "Game.BuildTunnelNetwork";
									pending.reason = "non_supply_expansion";
									m_autonomy.state.pendingFootholdAnchors.push_back(pending);

									adapterLog(
										"zone_anchor_candidate type=%s anchor_id=%u x=%.1f y=%.1f reason=non_supply_expansion tunnel_issued=1",
										zoneAnchorTypeToString(footholdType),
										static_cast<unsigned int>(pending.syntheticAnchorId),
										footholdCandidate.x,
										footholdCandidate.y);
								}
								else
								{
									adapterLog(
										"zone_anchor_rejected type=%s reason=tunnel_build_failed detail=%s",
										zoneAnchorTypeToString(footholdType),
										reason.c_str());
								}
							}
							else if (!issued)
							{
								adapterLog(
									"zone_anchor_rejected type=%s reason=tunnel_unavailable tunnels=%d cap=%d in_progress=%d money=%lu",
									zoneAnchorTypeToString(footholdType),
									totalTunnels,
									sprawlTunnelCap,
									counts.tunnelsInProgress,
									static_cast<unsigned long>(money));
							}
						}
						else
						{
							adapterLog(
								"zone_anchor_rejected type=%s reason=duplicate x=%.1f y=%.1f",
								zoneAnchorTypeToString(footholdType),
								footholdCandidate.x,
								footholdCandidate.y);
						}
					}
					else
					{
						// Candidate selection failed
						adapterLog(
							"zone_anchor_rejected type=%s reason=%s",
							zoneAnchorTypeToString(footholdType),
							footholdReason.c_str());
					}
				}
				else if (footholdDecision.reason == std::string("economy_not_ready"))
				{
					adapterLog(
						"zone_anchor_rejected type=market_foothold reason=economy_not_ready palace=%d markets=%d money=%lu",
						hasCompletedPalace ? 1 : 0,
						static_cast<int>(completedBlackMarkets),
						static_cast<unsigned long>(money));
				}

				if (!issued && reason.empty())
				{
					reason = macroBuildManager.EvaluateMacroHoldReason({
						shouldPreserveReserve,
						allowUrgentExpansionDespiteReserve,
						stashZoneCount,
						desiredZoneCount,
						counts.supplyStashesInProgress,
						counts.tunnelsInProgress,
						shouldThrottleExtraStashGrowth,
						hasCompletedPalace,
						static_cast<int>(completedBlackMarkets),
						totalTunnels,
						sprawlTunnelCap,
						shouldForceEcoRecovery,
						effectiveTotalBlackMarkets,
						sprawlDesiredMarketCount,
						canAttemptBlackMarketNow,
						remoteZoneNeedsFollowup
					});
				}

				m_autonomy.state.lastDecisionCategory = "macro";
				m_autonomy.state.lastDecisionCommand = chosenCommand;
				m_autonomy.state.lastDecisionReason = issued ? "ok" : reason;
				if (!m_autonomy.state.macroBuildTelemetry.is_object())
				{
					m_autonomy.state.macroBuildTelemetry = nlohmann::json::object();
				}
				m_autonomy.state.macroBuildTelemetry["result"] = nlohmann::json::object({
					{"command", chosenCommand},
					{"issued", issued},
					{"reason", issued ? "ok" : reason},
					{"tick", static_cast<unsigned int>(now)}
				});
				// Mark zone telemetry as dirty when build command issued (will complete later)
				if (issued && !chosenCommand.empty())
				{
					m_autonomy.state.telemetryZonesDirty = true;
				}
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
					totalSupplyStashes,
					totalBarracks,
					totalArmsDealers,
					totalPalaces,
					totalBlackMarkets,
					totalTunnels,
					totalStingers,
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
					totalZoneSupplyStashes,
					totalZoneBarracks,
					totalZoneArmsDealers,
					totalZoneBlackMarkets,
					totalZoneTunnels,
					totalZoneStingers,
					chosenCommand.c_str(),
					issued ? 1 : 0,
					(issued ? "ok" : reason.c_str()));

				if (isSprawlStyle && hasActiveZone && !zones.empty())
				{
					m_autonomy.state.nextZoneIndex = (m_autonomy.state.nextZoneIndex + 1u) % zones.size();
				}
				adapterLog(
					"autonomy_macro_decision player=%d command=%s issued=%d reason=%s money=%u",
					player->getPlayerIndex(),
					chosenCommand.c_str(),
					issued ? 1 : 0,
					reason.c_str(),
					money);
				m_autonomy.state.nextMacroTick = now + (issued ? 3000u : 2000u);
			}

			// Economy policy assessment: determines income health, reserve pressure, and spending mode
			const std::string profile = AIControlAdapterUiUtils::NormalizeAsciiLower(m_autonomy.state.profile);
			const AIControlAdapterProfilePolicyManager profilePolicyManager;
			const AIControlAdapterProfilePolicyConfig profilePolicyConfig = resolveAutonomyProfilePolicyConfig();
			const bool isBalancedSprawl = profilePolicyConfig.isBalancedSprawl;
			const UnsignedInt reserveCash = profilePolicyConfig.reserveCash;

			EconomyManagerInput economyInput;
			economyInput.currentMoney = money;
			economyInput.smoothedNetCashPerMinute = static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute));
			economyInput.reserveCash = reserveCash;
			economyInput.completedBlackMarkets = completedBlackMarkets;
			economyInput.inProgressBlackMarkets = inProgressBlackMarkets;
			economyInput.hasPalace = counts.palaces > 0;
			economyInput.profile = profile;

			EconomyPolicy economyPolicy = m_autonomy.economyManager.AssessEconomyPolicy(economyInput);

			// Calculate durable income capacity for telemetry
			const unsigned int incomePerMarket = 225u;
			const unsigned int durableIncome = completedBlackMarkets * incomePerMarket;
			adapterLog(
				"economy_recovery_budget protected_cash=%lu recovery_action=%s reason=%s",
				static_cast<unsigned long>(std::max<UnsignedInt>(reserveCash, completedBlackMarkets == 0u || staleBlackMarketFoundations > 0u ? 2500u : 0u)),
				staleBlackMarketFoundations > 0u ? "market" : (completedBlackMarkets == 0u ? "market" : "none"),
				staleBlackMarketFoundations > 0u ? "stale_income_foundation" : "current_state");

			// Determine recovery hold reason (if income is sufficient but state is not Healthy)
			const char* recoveryHold = "none";
			if (economyInput.smoothedNetCashPerMinute >= static_cast<int>(economyPolicy.sufficientIncomePerMinute) &&
			    economyPolicy.incomeState != EconomyIncomeState::Healthy)
			{
				if (economyPolicy.reserveState == EconomyReserveState::Depleted)
				{
					recoveryHold = "reserve_depleted";
				}
				else if (economyPolicy.reserveState == EconomyReserveState::Pressured && completedBlackMarkets < 2u)
				{
					recoveryHold = "possible_burst";
				}
				else if (inProgressBlackMarkets > 0)
				{
					recoveryHold = "in_recovery";
				}
				else
				{
					recoveryHold = "hysteresis";
				}
			}

			// Log economy policy for telemetry
			adapterLog(
				"economy_policy income_state=%s reserve_state=%s spending_mode=%s "
				"prioritize_income=%d allow_emergency=%d target_income=%u sufficient_income=%u "
				"current_income=%d durable_income=%u market_count=%u recovery_hold=%s "
				"markets[completed=%u in_progress=%u desired=%u max=%u] reason=%s tick=%u",
				economyPolicy.incomeState == EconomyIncomeState::Healthy ? "healthy" :
					economyPolicy.incomeState == EconomyIncomeState::Weak ? "weak" :
					economyPolicy.incomeState == EconomyIncomeState::Critical ? "critical" : "recovering",
				economyPolicy.reserveState == EconomyReserveState::Protected ? "protected" :
					economyPolicy.reserveState == EconomyReserveState::Pressured ? "pressured" : "depleted",
				economyPolicy.combatSpendingMode == CombatSpendingMode::Normal ? "normal" :
					economyPolicy.combatSpendingMode == CombatSpendingMode::Conservative ? "conservative" : "blocked_except_defense",
				economyPolicy.prioritizeIncomeBuild ? 1 : 0,
				economyPolicy.allowEmergencyIncomeBuild ? 1 : 0,
				economyPolicy.targetIncomePerMinute,
				economyPolicy.sufficientIncomePerMinute,
				economyInput.smoothedNetCashPerMinute,
				durableIncome,
				completedBlackMarkets,
				recoveryHold,
				completedBlackMarkets,
				inProgressBlackMarkets,
				economyPolicy.desiredBlackMarkets,
				economyPolicy.maxBlackMarkets,
				economyPolicy.reason.c_str(),
				now);

			// Log economy scaling telemetry (Phase 5.6)
			adapterLog(
				"economy_scaling mode=%s pressure=%d soft_markets=%u baseline_markets=%u hard_cap=%u "
				"in_progress_limit=%u reason=%s tick=%u",
				economyPolicy.scalingMode == EconomyScalingMode::Recovery ? "recovery" :
					economyPolicy.scalingMode == EconomyScalingMode::Growth ? "growth" : "saturated",
				economyPolicy.economyPressure,
				economyPolicy.softRecoveryMarkets,
				economyPolicy.baselineScalingMarkets,
				economyPolicy.maxBlackMarkets,
				economyPolicy.maxInProgressMarkets,
				economyPolicy.scalingReason.c_str(),
				now);

			// Scheduler integration: collect intents from economy, production, and tech, then process together
			bool schedulerHasIntents = false;

			// Economy recovery request (Black Market construction during reserve pressure)
			EconomyRecoveryRequest recoveryRequest = m_autonomy.economyManager.ChooseRecoveryAction(economyInput, economyPolicy);
			if (recoveryRequest.shouldBuildIncome)
			{
				AIControlAdapterStrategicSpendEconomyFacts recoverySpendEconomy;
				recoverySpendEconomy.money = money;
				recoverySpendEconomy.reserveCash = reserveCash;
				recoverySpendEconomy.completedMarkets = static_cast<int>(completedBlackMarkets);
				recoverySpendEconomy.healthyMarketsInProgress = static_cast<int>(inProgressBlackMarkets);
				recoverySpendEconomy.staleMarketFoundations = static_cast<int>(staleBlackMarketFoundations);
				recoverySpendEconomy.staleStrategicFoundations = staleStrategicFoundations;
				recoverySpendEconomy.activeWmdThreats = m_autonomy.wmdTargetTracker.hasActiveWMDThreat() ? 1 : 0;
				recoverySpendEconomy.mainBaseCritical =
					m_autonomy.state.mainBaseCriticalOverrideTelemetry.is_object()
					&& m_autonomy.state.mainBaseCriticalOverrideTelemetry.value("active", false);
				AIControlAdapterStrategicSpendZoneFacts recoverySpendZones;
				recoverySpendZones.currentZones =
					static_cast<int>(m_autonomy.state.telemetryZones.is_array() ? m_autonomy.state.telemetryZones.size() : 0u);
				recoverySpendZones.developedZones = 0;
				recoverySpendZones.desiredZones =
					profilePolicyManager.ResolveStrategicSpendDesiredZoneCount(
						profilePolicyConfig,
						profilePolicyConfig.sprawlSupplyCap);
				AIControlAdapterStrategicSpendArmyFacts recoverySpendArmy;
				recoverySpendArmy.armySize = counts.mobileUnits;
				recoverySpendArmy.armyCap = std::max(1, counts.mobileUnits);
				recoverySpendArmy.quads = counts.quads;
				recoverySpendArmy.buggies = counts.rocketBuggies;
				recoverySpendArmy.scorpions = counts.scorpions;
				const AIControlAdapterStrategicSpendSnapshotBase recoverySpendBase =
					strategicSpendSnapshotBuilder.BuildBase(recoverySpendEconomy, recoverySpendZones, recoverySpendArmy);
				AIControlAdapterStrategicSpendSnapshotOverrides recoverySpendOverrides;
				recoverySpendOverrides.incomeCritical = economyPolicy.incomeState == EconomyIncomeState::Critical || completedBlackMarkets == 0u;
				recoverySpendOverrides.reserveDepleted = economyPolicy.reserveState == EconomyReserveState::Depleted || money < reserveCash;
				const AIControlAdapterStrategicSpendSnapshot recoverySpendSnapshot =
					strategicSpendSnapshotBuilder.Build(recoverySpendBase, recoverySpendOverrides);
				const AIControlAdapterStrategicSpendManager strategicSpendManager;
				const AIControlAdapterStrategicSpendDecision recoverySpend =
					strategicSpendManager.Evaluate(recoverySpendSnapshot, StrategicSpendCategory::EconomyRecovery, 2500u);
				adapterLog(
					"%s",
					strategicSpendTelemetrySerializer.BuildPolicyLogLine({
						StrategicSpendCategory::EconomyRecovery,
						money,
						reserveCash,
						&recoverySpend
					}).c_str());
				strategicSpendTelemetrySerializer.RecordMarketFoundationCounts(
					m_autonomy.state.strategicSpendTelemetry,
					static_cast<int>(inProgressBlackMarkets),
					static_cast<int>(staleBlackMarketFoundations),
					staleStrategicFoundations);
				strategicSpendTelemetrySerializer.RecordCategory(
					m_autonomy.state.strategicSpendTelemetry,
					StrategicSpendCategory::EconomyRecovery,
					2500u,
					recoverySpend);
				m_autonomy.state.strategicSpendTelemetry["economy_recovery_action"] =
					recoverySpend.allowed ? "market" : "blocked";
				if (!recoverySpend.allowed)
				{
					adapterLog(
						"strategic_spend_competition winner=none blocked=economy_recovery money=%lu reserve=%lu reason=%s",
						static_cast<unsigned long>(money),
						static_cast<unsigned long>(reserveCash),
						recoverySpend.reason);
					recoveryRequest.shouldBuildIncome = false;
					recoveryRequest.reason = recoverySpend.reason;
				}
			}

			if (recoveryRequest.shouldBuildIncome)
			{
				// Phase 5.7: Choose distributed safe strategic placement for recovery Black Markets
				StrategicPlacementChoice placement = chooseStrategicPlacement(
					zones,
					zoneCounts,
					activeZoneIndex,
					StrategicStructureRole::Income,
					m_autonomy.state.zoneRadius,
					completedBlackMarkets);

				// Log strategic placement decision (Phase 5.7: added score and zone_markets)
				adapterLog(
					"strategic_placement command=%s template=%s role=income source=%s zone_anchor=%u "
					"zone_center=(%.1f,%.1f) zone_radius=%.1f strict_zone=%d has_placement=%d "
					"score=%d zone_markets=%u total_markets=%u reason=%s tick=%u",
					recoveryRequest.buildingCommand.c_str(),
					recoveryRequest.buildingTemplate.c_str(),
					placement.source.c_str(),
					placement.zoneAnchorId,
					placement.zoneCenterX,
					placement.zoneCenterY,
					placement.zoneRadius,
					0, // Always use strict_zone=false for recovery (Phase 5.5)
					placement.hasPlacement ? 1 : 0,
					placement.score,
					placement.zoneMarketCount,
					placement.totalMarkets,
					placement.reason.c_str(),
					now);

				Intent recoveryIntent;
				recoveryIntent.category = IntentCategory::ECONOMY;
				recoveryIntent.priority = economyPolicy.prioritizeIncomeBuild ? IntentPriority::HIGH : IntentPriority::NORMAL;
				recoveryIntent.commandName = recoveryRequest.buildingCommand;
				recoveryIntent.targetName = recoveryRequest.buildingTemplate;
				recoveryIntent.reason = recoveryRequest.reason;

				// Capture recovery execution in callback with strategic placement args
				recoveryIntent.executeFunc = [&, recoveryRequest, placement](std::string& resultReason) -> bool {
					nlohmann::json args = nlohmann::json::object();

					// Pass explicit zone placement if available (Phase 5.5)
					if (placement.hasPlacement)
					{
						// Use correct format: {"x": ..., "y": ...} not array
						args["zone_center"] = nlohmann::json::object({
							{"x", placement.zoneCenterX},
							{"y", placement.zoneCenterY}
						});
						args["zone_radius"] = placement.zoneRadius;
						// Use strict_zone=false to allow worker movement if placement fails
						args["strict_zone"] = false;
					}

					return tryCommand("auto_recovery", recoveryRequest.buildingCommand.c_str(), args, resultReason);
				};

				m_autonomy.scheduler.SubmitIntent(recoveryIntent);
				schedulerHasIntents = true;

				adapterLog(
					"economy_recovery_request command=%s template=%s markets[completed=%u in_progress=%u desired=%u max=%u] reason=%s tick=%u",
					recoveryRequest.buildingCommand.c_str(),
					recoveryRequest.buildingTemplate.c_str(),
					recoveryRequest.completedMarkets,
					recoveryRequest.inProgressMarkets,
					recoveryRequest.desiredMarkets,
					recoveryRequest.maxMarkets,
					recoveryRequest.reason.c_str(),
					now);
			}

			// Phase 7.8: Multi-front zone defense allocation.
			// Maintain capped per-zone defense groups instead of repeatedly moving one global army blob.
			const DWORD defenseResponseCooldownMs = 5000;  // 5 seconds command throttle per allocation

			if (!m_autonomy.state.zoneThreats.empty())
			{
				const DWORD threatFreshnessMs = 10000;  // 10 seconds
				auto isFreshThreat = [&](const AutonomyZoneThreatState& threat) -> bool
				{
					return AIControlAdapterDefenseManager::IsZoneThreatFresh(
						now,
						threat.lastSeenTick,
						threatFreshnessMs);
				};
				auto findZoneByAnchor = [&](UnsignedInt anchor) -> const AutonomyZone*
				{
					for (std::size_t i = 0; i < zones.size(); ++i)
					{
						if (static_cast<UnsignedInt>(zones[i].anchorId) == anchor)
						{
							return &zones[i];
						}
					}
					return nullptr;
				};
				auto countAliveAssigned = [&](const AutonomyZoneDefenseAllocation& allocation) -> int
				{
					int alive = 0;
					for (std::size_t i = 0; i < allocation.assignedUnitIds.size(); ++i)
					{
						Object* unit = TheGameLogic != nullptr ? TheGameLogic->findObjectByID(static_cast<ObjectID>(allocation.assignedUnitIds[i])) : nullptr;
						if (unit != nullptr && !unit->isEffectivelyDead())
						{
							++alive;
						}
					}
					return alive;
				};
				auto countLocalCombat = [&](const AutonomyZone* zone) -> int
				{
					if (zone == nullptr)
					{
						return 0;
					}
					const Real radius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
					const Real radiusSq = radius * radius;
					int count = 0;
					for (std::size_t i = 0; i < ownedObjects.size(); ++i)
					{
						const AutonomyOwnedObjectSnapshot& owned = ownedObjects[i];
						if (owned.object == nullptr || owned.isStructure || owned.isDozer || owned.isRadarVan || owned.underConstruction)
						{
							continue;
						}
						if (!(owned.isSoldier || owned.isRpg || owned.isQuad || owned.isScorpion || owned.isScudLauncher))
						{
							continue;
						}
						const Coord3D* pos = owned.object->getPosition();
						if (pos == nullptr)
						{
							continue;
						}
						const Real dx = pos->x - zone->center.x;
						const Real dy = pos->y - zone->center.y;
						if (dx * dx + dy * dy <= radiusSq)
						{
							++count;
						}
					}
					return count;
				};
				auto donorIdsForLog = [&](UnsignedInt targetZone, int severity) -> std::string
				{
					std::string donors;
					for (std::size_t i = 0; i < zones.size(); ++i)
					{
						const AutonomyZone& zone = zones[i];
						const UnsignedInt zoneAnchor = static_cast<UnsignedInt>(zone.anchorId);
						if (zoneAnchor == targetZone)
						{
							continue;
						}
						const bool donorThreatened = m_autonomy.state.zoneThreats.find(zoneAnchor) != m_autonomy.state.zoneThreats.end();
						if (donorThreatened && severity < 4)
						{
							continue;
						}
						if (!donors.empty())
						{
							donors += ",";
						}
						donors += std::to_string(zoneAnchor);
					}
					return donors.empty() ? std::string("global_pool") : donors;
				};

				for (auto it = m_autonomy.state.zoneDefenseAllocations.begin(); it != m_autonomy.state.zoneDefenseAllocations.end(); )
				{
					AutonomyZoneDefenseAllocation& allocation = it->second;
					const auto threatIt = m_autonomy.state.zoneThreats.find(allocation.zoneAnchorId);
					const bool threatStillFresh = threatIt != m_autonomy.state.zoneThreats.end() && isFreshThreat(threatIt->second);
					if (threatStillFresh)
					{
						const AutonomyZoneThreatState& threat = threatIt->second;
						const char* releaseReason = AIControlAdapterDefenseManager::TransientStrikeReleaseReason(
							threat.sourceType,
							threat.localEnemyCount,
							threat.enemyArtilleryCount);
						if (releaseReason != nullptr)
						{
							for (std::size_t taskIdx = 0; taskIdx < allocation.taskIds.size(); ++taskIdx)
							{
								m_autonomy.combatTaskManager.completeTask(allocation.taskIds[taskIdx], releaseReason);
							}
							adapterLog(
								"zone_defense_release zone=%u assigned=%d reason=%s",
								allocation.zoneAnchorId,
								countAliveAssigned(allocation),
								releaseReason);
							it = m_autonomy.state.zoneDefenseAllocations.erase(it);
							continue;
						}
					}
					if (allocation.expiryTick > 0 && now >= allocation.expiryTick)
					{
						for (std::size_t taskIdx = 0; taskIdx < allocation.taskIds.size(); ++taskIdx)
						{
							m_autonomy.combatTaskManager.expireTask(allocation.taskIds[taskIdx], "zone_defense_expired");
						}
						adapterLog(
							"zone_defense_release zone=%u assigned=%d reason=expired",
							allocation.zoneAnchorId,
							countAliveAssigned(allocation));
						it = m_autonomy.state.zoneDefenseAllocations.erase(it);
						continue;
					}
					if (!threatStillFresh && now >= allocation.holdUntilTick)
					{
						for (std::size_t taskIdx = 0; taskIdx < allocation.taskIds.size(); ++taskIdx)
						{
							m_autonomy.combatTaskManager.completeTask(allocation.taskIds[taskIdx], "zone_threat_cleared");
						}
						adapterLog(
							"zone_defense_release zone=%u assigned=%d reason=threat_cleared",
							allocation.zoneAnchorId,
							countAliveAssigned(allocation));
						it = m_autonomy.state.zoneDefenseAllocations.erase(it);
						continue;
					}
					++it;
				}

				struct DefenseThreatCandidate
				{
					UnsignedInt zoneAnchor;
					AutonomyZoneThreatState threat;
					int severity;
				};
				std::vector<DefenseThreatCandidate> defenseThreats;
				for (auto it = m_autonomy.state.zoneThreats.begin(); it != m_autonomy.state.zoneThreats.end(); ++it)
				{
					const UnsignedInt zoneAnchor = it->first;
					const AutonomyZoneThreatState& threat = it->second;

					if (!isFreshThreat(threat))
					{
						continue;
					}

					const int severity = AIControlAdapterDefenseManager::ThreatSeverityForLevel(threat.level);
					if (severity >= 1)
					{
						DefenseThreatCandidate candidate;
						candidate.zoneAnchor = zoneAnchor;
						candidate.threat = threat;
						candidate.severity = severity;
						defenseThreats.push_back(candidate);
					}
				}
				std::sort(defenseThreats.begin(), defenseThreats.end(), [](const DefenseThreatCandidate& lhs, const DefenseThreatCandidate& rhs) -> bool
				{
					if (lhs.severity != rhs.severity)
					{
						return lhs.severity > rhs.severity;
					}
					return lhs.threat.lastSeenTick > rhs.threat.lastSeenTick;
				});

				const DefenseThreatCandidate* selectedThreat = nullptr;
				int selectedMaxUnits = 0;
				int selectedExistingAssigned = 0;
				AIControlAdapterZoneDefenseBudgetResult selectedBudget;
				AIControlAdapterZoneDefenseAllocationResult selectedAllocationDecision;
				std::string selectedDonors;
				std::string selectedReason;
				bool selectedMainBaseCriticalOverride = false;
				std::vector<UnsignedInt> selectedAllowedUnitIds;

				std::vector<Object*> availableCombat;
				collectCombatUnitsForRaid(player, availableCombat, true);
				const int availableIdleCombat = static_cast<int>(availableCombat.size());
				int activeCriticalAllocations = 0;
				for (auto allocIt = m_autonomy.state.zoneDefenseAllocations.begin(); allocIt != m_autonomy.state.zoneDefenseAllocations.end(); ++allocIt)
				{
					if (allocIt->second.threatSeverity >= 4)
					{
						++activeCriticalAllocations;
					}
				}

				auto nearestZoneAnchorForUnit = [&](Object* unit) -> UnsignedInt
				{
					if (unit == nullptr || unit->getPosition() == nullptr || zones.empty())
					{
						return 0u;
					}
					const Coord3D* pos = unit->getPosition();
					const Real radius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius) * 1.25f;
					const Real radiusSq = radius * radius;
					Real bestDistSq = radiusSq;
					UnsignedInt bestAnchor = 0u;
					for (std::size_t zoneIdx = 0; zoneIdx < zones.size(); ++zoneIdx)
					{
						const AutonomyZone& zone = zones[zoneIdx];
						const Real dx = pos->x - zone.center.x;
						const Real dy = pos->y - zone.center.y;
						const Real distSq = dx * dx + dy * dy;
						if (distSq <= bestDistSq)
						{
							bestDistSq = distSq;
							bestAnchor = static_cast<UnsignedInt>(zone.anchorId);
						}
					}
					return bestAnchor;
				};
				std::unordered_map<UnsignedInt, std::vector<Object*> > availableCombatByZone;
				std::vector<Object*> unzonedAvailableCombat;
				for (std::size_t unitIdx = 0; unitIdx < availableCombat.size(); ++unitIdx)
				{
					Object* unit = availableCombat[unitIdx];
					const UnsignedInt zoneAnchor = nearestZoneAnchorForUnit(unit);
					if (zoneAnchor > 0)
					{
						availableCombatByZone[zoneAnchor].push_back(unit);
					}
					else
					{
						unzonedAvailableCombat.push_back(unit);
					}
				}
				nlohmann::json reserveTelemetry = nlohmann::json::array();
				for (std::size_t zoneIdx = 0; zoneIdx < zones.size(); ++zoneIdx)
				{
					const AutonomyZone& zone = zones[zoneIdx];
					const UnsignedInt zoneAnchor = static_cast<UnsignedInt>(zone.anchorId);
					const auto threatIt = m_autonomy.state.zoneThreats.find(zoneAnchor);
					const bool activeThreat = threatIt != m_autonomy.state.zoneThreats.end() && isFreshThreat(threatIt->second);
					const AutonomyZoneThreatState* threat = activeThreat ? &threatIt->second : nullptr;
					AutonomyZoneDefenseReserveState& reserveState = m_autonomy.state.zoneDefenseReserves[zoneAnchor];
					const int severity = threat != nullptr ? AIControlAdapterDefenseManager::ThreatSeverityForLevel(threat->level) : 0;
					const bool pressureEvidence =
						activeThreat &&
						(severity >= 3 ||
						 (threat != nullptr && (threat->localEnemyCount > 0 || threat->enemyArtilleryCount > 0 ||
							threat->damagedStructures > 0 || threat->destroyedStructures > 0)));
					if (pressureEvidence)
					{
						reserveState.recentAttackCount = std::min(5, reserveState.recentAttackCount + 1);
						reserveState.lastThreatTick = now;
					}
					else if (reserveState.lastThreatTick > 0u && now - reserveState.lastThreatTick >= 120000u)
					{
						reserveState.recentAttackCount = std::max(0, reserveState.recentAttackCount - 1);
						if (reserveState.recentAttackCount == 0)
						{
							reserveState.lastThreatTick = 0u;
						}
					}
					const auto allocationIt = m_autonomy.state.zoneDefenseAllocations.find(zoneAnchor);
					const bool hasAllocation = allocationIt != m_autonomy.state.zoneDefenseAllocations.end();
					const int assignedResidents = hasAllocation ? countAliveAssigned(allocationIt->second) : 0;
					const int availableResidents = static_cast<int>(availableCombatByZone[zoneAnchor].size());
					const int residentCount = availableResidents + assignedResidents;
					const bool isActiveZone = m_autonomy.state.hasLastZone && m_autonomy.state.lastZoneAnchorId == zoneAnchor;
					const bool isFrontierZone = !isActiveZone && !zone.isMainBase;
					const unsigned int quietMs = reserveState.lastThreatTick > 0u ? now - reserveState.lastThreatTick : 0u;
					const AIControlAdapterZoneDefenseReserveResult reserve =
						AIControlAdapterChooseZoneDefenseReserve({
							zone.isMainBase,
							isActiveZone,
							isFrontierZone,
							activeThreat,
							hasAllocation,
							severity,
							reserveState.recentAttackCount,
							threat != nullptr ? threat->localEnemyCount : 0,
							threat != nullptr ? threat->enemyArtilleryCount : 0,
							threat != nullptr ? threat->damagedStructures : 0,
							threat != nullptr ? threat->destroyedStructures : 0,
							quietMs
						});
					reserveState.posture = reserve.posture;
					reserveState.reason = reserve.reason;
					reserveState.resident = residentCount;
					reserveState.floor = reserve.floor;
					reserveState.surplus = std::max(0, availableResidents - reserve.floor);
					reserveState.deficit = std::max(0, reserve.floor - residentCount);
					reserveState.activeThreat = activeThreat;
					reserveState.productionNeeded = reserve.productionNeeded && reserveState.deficit > 0;
					reserveState.donorAllowed = reserveState.surplus > 0;
					adapterLog(
						"zone_defense_posture zone=%u posture=%s reason=%s recent_attacks=%d local_enemies=%d",
						zoneAnchor,
						reserveState.posture.c_str(),
						reserveState.reason.c_str(),
						reserveState.recentAttackCount,
						threat != nullptr ? threat->localEnemyCount : 0);
					adapterLog(
						"zone_defense_reserve zone=%u posture=%s resident=%d floor=%d surplus=%d deficit=%d reason=%s",
						zoneAnchor,
						reserveState.posture.c_str(),
						reserveState.resident,
						reserveState.floor,
						reserveState.surplus,
						reserveState.deficit,
						reserveState.reason.c_str());
					if (reserveState.productionNeeded)
					{
						adapterLog(
							"zone_reserve_production_needed zone=%u desired=%d resident=%d deficit=%d preferred_units=quad,scorpion,rocket_buggy reason=%s",
							zoneAnchor,
							reserveState.floor,
							reserveState.resident,
							reserveState.deficit,
							reserveState.reason.c_str());
					}
					reserveTelemetry.push_back(nlohmann::json::object({
						{"zone", zoneAnchor},
						{"posture", reserveState.posture},
						{"resident", reserveState.resident},
						{"floor", reserveState.floor},
						{"surplus", reserveState.surplus},
						{"deficit", reserveState.deficit},
						{"active_threat", reserveState.activeThreat},
						{"production_needed", reserveState.productionNeeded},
						{"donor_allowed", reserveState.donorAllowed},
						{"reason", reserveState.reason}
					}));
				}
				m_autonomy.state.zoneDefenseReserveTelemetry = reserveTelemetry;
				auto collectAllowedDefenseUnitIds = [&](UnsignedInt targetZone, int requested, bool mainBaseOverride, std::string& donorsOut) -> std::vector<UnsignedInt>
				{
					std::vector<UnsignedInt> ids;
					donorsOut.clear();
					if (requested <= 0)
					{
						donorsOut = "none";
						return ids;
					}
					auto appendUnits = [&](const std::vector<Object*>& units, int allowed, bool allowScoutPoolBorrow) -> int
					{
						int added = 0;
						for (std::size_t idx = 0; idx < units.size() && added < allowed; ++idx)
						{
							Object* unit = units[idx];
							if (unit == nullptr)
							{
								continue;
							}
							const ThingTemplate* tt = unit->getTemplate();
							AutomationOwnedObjectSnapshot snapshot = {};
							snapshot.object = unit;
							snapshot.name = tt != nullptr ? tt->getName().str() : "";
							snapshot.isTechnical = containsIgnoreCase(snapshot.name, "technical");
							snapshot.isStructure = unit->isKindOf(KINDOF_STRUCTURE);
							snapshot.underConstruction = unit->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
							if (!allowScoutPoolBorrow && isWorkerShuttleTechnicalProtected(player, snapshot))
							{
								adapterLog(
									"zone_defense_retask_blocked unit=%u from_zone=0 to_zone=%u reason=worker_shuttle_reserved",
									static_cast<unsigned int>(unit->getID()),
									targetZone);
								continue;
							}
							if (allowScoutPoolBorrow && isWorkerShuttleTechnicalProtected(player, snapshot))
							{
								adapterLog(
									"worker_shuttle_borrow unit=%u reason=main_base_critical_override",
									static_cast<unsigned int>(unit->getID()));
							}
							if (!allowScoutPoolBorrow && isScoutPoolTechnicalProtected(player, snapshot))
							{
								adapterLog(
									"zone_defense_retask_blocked unit=%u from_zone=0 to_zone=%u reason=scout_pool_reserved",
									static_cast<unsigned int>(unit->getID()),
									targetZone);
								continue;
							}
							if (allowScoutPoolBorrow && isScoutPoolTechnicalProtected(player, snapshot))
							{
								adapterLog(
									"scout_pool_borrow unit=%u reason=main_base_critical_override",
									static_cast<unsigned int>(unit->getID()));
							}
							ids.push_back(static_cast<UnsignedInt>(unit->getID()));
							++added;
						}
						return added;
					};
					auto appendDonor = [&](UnsignedInt donorZone) -> void
					{
						if (!donorsOut.empty())
						{
							donorsOut += ",";
						}
						donorsOut += std::to_string(donorZone);
					};
					const auto targetUnitsIt = availableCombatByZone.find(targetZone);
					if (targetUnitsIt != availableCombatByZone.end())
					{
						const int added = appendUnits(targetUnitsIt->second, std::min(requested, static_cast<int>(targetUnitsIt->second.size())), mainBaseOverride);
						if (added > 0)
						{
							appendDonor(targetZone);
						}
					}
					for (std::size_t zoneIdx = 0; zoneIdx < zones.size() && static_cast<int>(ids.size()) < requested; ++zoneIdx)
					{
						const UnsignedInt sourceZone = static_cast<UnsignedInt>(zones[zoneIdx].anchorId);
						if (sourceZone == targetZone)
						{
							continue;
						}
						const auto unitsIt = availableCombatByZone.find(sourceZone);
						const int sourceAvailable = unitsIt != availableCombatByZone.end() ? static_cast<int>(unitsIt->second.size()) : 0;
						const int remaining = requested - static_cast<int>(ids.size());
						const auto reserveIt = m_autonomy.state.zoneDefenseReserves.find(sourceZone);
						const AutonomyZoneDefenseReserveState emptyReserve;
						const AutonomyZoneDefenseReserveState& reserveState = reserveIt != m_autonomy.state.zoneDefenseReserves.end() ? reserveIt->second : emptyReserve;
						const AIControlAdapterZoneDefenseDonorFloorResult donor =
							AIControlAdapterApplyZoneDefenseDonorFloor({
								sourceAvailable,
								reserveState.floor,
								remaining,
								reserveState.activeThreat,
								m_autonomy.state.zoneDefenseAllocations.find(sourceZone) != m_autonomy.state.zoneDefenseAllocations.end(),
								reserveState.posture == "frontline",
								reserveState.posture == "contested_front",
								mainBaseOverride
							});
						adapterLog(
							"zone_defense_donor_floor source_zone=%u target_zone=%u available=%d floor=%d requested=%d allowed=%d blocked=%d reason=%s",
							sourceZone,
							targetZone,
							sourceAvailable,
							reserveState.floor,
							remaining,
							donor.allowed,
							donor.blocked,
							donor.reason);
						if (donor.allowed > 0 && unitsIt != availableCombatByZone.end())
						{
							const int added = appendUnits(unitsIt->second, donor.allowed, mainBaseOverride);
							if (added > 0)
							{
								appendDonor(sourceZone);
							}
						}
					}
					if (static_cast<int>(ids.size()) < requested && !unzonedAvailableCombat.empty())
					{
						const int added = appendUnits(unzonedAvailableCombat, requested - static_cast<int>(ids.size()), mainBaseOverride);
						if (added > 0)
						{
							if (!donorsOut.empty())
							{
								donorsOut += ",";
							}
							donorsOut += "global_pool";
						}
					}
					if (donorsOut.empty())
					{
						donorsOut = "none";
					}
					return ids;
				};

				adapterLog(
					"zone_defense_eval candidates=%d active_allocations=%d available_idle=%d threats_scanned=%zu tick=%u",
					static_cast<int>(defenseThreats.size()),
					static_cast<int>(m_autonomy.state.zoneDefenseAllocations.size()),
					availableIdleCombat,
					m_autonomy.state.zoneThreats.size(),
					now);

				m_autonomy.state.mainBaseCriticalOverrideTelemetry = nlohmann::json::object({
					{"active", false},
					{"zone", 0},
					{"reason", defenseThreats.empty() ? "no_active_threat" : "not_evaluated"},
					{"local_enemies", 0},
					{"recent_wmd", false},
					{"damaged_structures", 0},
					{"destroyed_structures", 0},
					{"assigned_units", 0},
					{"requested_units", 0}
				});

				for (std::size_t i = 0; i < defenseThreats.size(); ++i)
				{
					const DefenseThreatCandidate& candidate = defenseThreats[i];
					const AutonomyZone* zone = findZoneByAnchor(candidate.zoneAnchor);
					const bool isMainBaseThreat = zone != nullptr && zone->isMainBase;
					const std::string sourceType = candidate.threat.sourceType.empty() ? "unknown" : candidate.threat.sourceType;
					const std::string sourceResponse = candidate.threat.response.empty() ? "limited_scout" : candidate.threat.response;
					const std::string sourceReason = candidate.threat.reason.empty() ? "damage_source_unknown" : candidate.threat.reason;
					adapterLog(
						"zone_threat_source zone=%u type=%s severity=%s response=%s reason=%s",
						candidate.zoneAnchor,
						sourceType.c_str(),
						candidate.threat.level.c_str(),
						sourceResponse.c_str(),
						sourceReason.c_str());
					adapterLog(
						"zone_threat_evidence zone=%u local_enemies=%d enemy_artillery=%d recent_wmd=%d damaged_structures=%d destroyed_structures=%d reason=%s",
						candidate.zoneAnchor,
						candidate.threat.localEnemyCount,
						candidate.threat.enemyArtilleryCount,
						candidate.threat.recentWmd ? 1 : 0,
						candidate.threat.damagedStructures,
						candidate.threat.destroyedStructures,
						sourceReason.c_str());
					const AIControlAdapterMainBaseCriticalOverrideResult mainBaseOverride =
						AIControlAdapterEvaluateMainBaseCriticalOverride({
							isMainBaseThreat,
							candidate.threat.level,
							candidate.threat.localEnemyCount,
							candidate.threat.recentWmd,
							candidate.threat.damagedStructures,
							candidate.threat.destroyedStructures,
							candidate.threat.damagedStructures >= 3 ? 1 : 0
						});
					if (isMainBaseThreat || candidate.severity >= 4)
					{
						adapterLog(
							"main_base_critical_override active=%d zone=%u reason=%s local_enemies=%d recent_wmd=%d damaged_structures=%d destroyed_structures=%d",
							mainBaseOverride.active ? 1 : 0,
							candidate.zoneAnchor,
							mainBaseOverride.reason,
							candidate.threat.localEnemyCount,
							candidate.threat.recentWmd ? 1 : 0,
							candidate.threat.damagedStructures,
							candidate.threat.destroyedStructures);
					}
					if (isMainBaseThreat)
					{
						m_autonomy.state.mainBaseCriticalOverrideTelemetry = nlohmann::json::object({
							{"active", mainBaseOverride.active},
							{"zone", candidate.zoneAnchor},
							{"reason", mainBaseOverride.reason},
							{"local_enemies", candidate.threat.localEnemyCount},
							{"recent_wmd", candidate.threat.recentWmd},
							{"damaged_structures", candidate.threat.damagedStructures},
							{"destroyed_structures", candidate.threat.destroyedStructures},
							{"assigned_units", 0},
							{"requested_units", 0}
						});
					}
					if ((sourceType == "wmd_strike" || sourceType == "special_power_strike" || sourceType == "unknown_damage")
						&& candidate.threat.localEnemyCount <= 0
						&& candidate.threat.enemyArtilleryCount <= 0)
					{
						adapterLog(
							"zone_defense_skip zone=%u source_type=%s reason=no_local_enemy_units",
							candidate.zoneAnchor,
							sourceType.c_str());
						adapterLog(
							"zone_defense_response zone=%u issued=0 reason=no_local_enemy_units",
							candidate.zoneAnchor);
						continue;
					}
					const auto allocationIt = m_autonomy.state.zoneDefenseAllocations.find(candidate.zoneAnchor);
					const bool hasAllocation = allocationIt != m_autonomy.state.zoneDefenseAllocations.end();
					const AutonomyZoneDefenseAllocation* allocation = hasAllocation ? &allocationIt->second : nullptr;
					const int existingAssigned = allocation != nullptr ? countAliveAssigned(*allocation) : 0;
					const int localCombat = countLocalCombat(zone);
					const bool isActiveZone = m_autonomy.state.hasLastZone && m_autonomy.state.lastZoneAnchorId == candidate.zoneAnchor;
					const bool isDevelopedZone = zone != nullptr && !zone->isMainBase && localCombat >= 3;
					const bool isFrontierZone = !isActiveZone && zone != nullptr && !zone->isMainBase;

					AIControlAdapterZoneDefenseBudgetResult budget = AIControlAdapterChooseZoneDefenseBudget({
						candidate.threat.level,
						availableIdleCombat,
						static_cast<int>(m_autonomy.state.zoneDefenseAllocations.size()),
						localCombat,
						isMainBaseThreat,
						isDevelopedZone,
						isFrontierZone,
						isActiveZone,
						activeCriticalAllocations > 0,
						mainBaseOverride.active
					});

					AIControlAdapterZoneDefenseAllocationResult allocationDecision = AIControlAdapterEvaluateZoneDefenseAllocation({
						hasAllocation,
						allocation != nullptr ? allocation->threatSeverity : 0,
						candidate.severity,
						now,
						allocation != nullptr ? allocation->holdUntilTick : 0u,
						allocation != nullptr ? allocation->expiryTick : 0u,
						existingAssigned,
						budget.desiredDefenders,
						allocation != nullptr ? allocation->targetX : candidate.threat.positionX,
						allocation != nullptr ? allocation->targetY : candidate.threat.positionY,
						candidate.threat.positionX,
						candidate.threat.positionY,
						300.0f
					});

					adapterLog(
						"zone_defense_budget zone=%u threat=%s local=%d reserve=%d max_new=%d active_allocations=%d reason=%s",
						candidate.zoneAnchor,
						candidate.threat.level.c_str(),
						localCombat,
						budget.localReserve,
						budget.maxNewAssignments,
						static_cast<int>(m_autonomy.state.zoneDefenseAllocations.size()),
						budget.reason);

					if (!allocationDecision.shouldIssueCommand)
					{
						const char* blockReason = std::strcmp(allocationDecision.reason, "min_hold") == 0 ? "min_hold" : "same_priority";
						if (allocation != nullptr && !allocation->assignedUnitIds.empty())
						{
							adapterLog(
								"zone_defense_retask_blocked unit=%u from_zone=%u to_zone=%u reason=%s",
								allocation->assignedUnitIds[0],
								candidate.zoneAnchor,
								candidate.zoneAnchor,
								blockReason);
						}
						adapterLog(
							"zone_defense_allocation zone=%u state=%s threat=%s assigned=%d requested=%d donors=%s reason=%s",
							candidate.zoneAnchor,
							allocation != nullptr ? allocation->state.c_str() : "none",
							candidate.threat.level.c_str(),
							existingAssigned,
							0,
							"none",
							allocationDecision.reason);
						continue;
					}

					selectedMaxUnits = std::min(budget.maxNewAssignments, allocationDecision.requestedNewAssignments);
					if (sourceType == "artillery_attack")
					{
						selectedMaxUnits = std::min(selectedMaxUnits, 2);
					}
					else if (sourceType == "unknown_damage" || sourceType == "unknown")
					{
						selectedMaxUnits = std::min(selectedMaxUnits, 1);
					}
					std::string reserveAwareDonors;
					std::vector<UnsignedInt> reserveAllowedUnitIds;
					if (selectedMaxUnits > 0)
					{
						reserveAllowedUnitIds = collectAllowedDefenseUnitIds(
							candidate.zoneAnchor,
							selectedMaxUnits,
							mainBaseOverride.active,
							reserveAwareDonors);
						selectedMaxUnits = std::min(selectedMaxUnits, static_cast<int>(reserveAllowedUnitIds.size()));
					}
					if (selectedMaxUnits <= 0)
					{
						if (isMainBaseThreat)
						{
							m_autonomy.state.mainBaseCriticalOverrideTelemetry["requested_units"] = allocationDecision.requestedNewAssignments;
						}
						adapterLog(
							"zone_defense_allocation zone=%u state=%s threat=%s assigned=%d requested=%d donors=%s reason=%s",
							candidate.zoneAnchor,
							allocation != nullptr ? allocation->state.c_str() : "none",
							candidate.threat.level.c_str(),
							existingAssigned,
							allocationDecision.requestedNewAssignments,
							reserveAwareDonors.empty() ? "none" : reserveAwareDonors.c_str(),
							budget.reason);
						continue;
					}

					selectedThreat = &defenseThreats[i];
					selectedExistingAssigned = existingAssigned;
					selectedBudget = budget;
					selectedAllocationDecision = allocationDecision;
					selectedDonors = reserveAwareDonors.empty() ? donorIdsForLog(candidate.zoneAnchor, candidate.severity) : reserveAwareDonors;
					selectedReason = mainBaseOverride.active
						? "main_base_critical_override"
						: (sourceType == "artillery_attack"
						? "artillery_counterbattery"
						: ((sourceType == "unknown_damage" || sourceType == "unknown") ? "unknown_limited" : (sourceType == "unit_attack" ? "unit_attack" : allocationDecision.reason)));
					selectedMainBaseCriticalOverride = mainBaseOverride.active;
					selectedAllowedUnitIds = reserveAllowedUnitIds;
					if (isMainBaseThreat)
					{
						m_autonomy.state.mainBaseCriticalOverrideTelemetry["requested_units"] = selectedMaxUnits;
					}
					if (mainBaseOverride.active)
					{
						for (auto allocIt = m_autonomy.state.zoneDefenseAllocations.begin(); allocIt != m_autonomy.state.zoneDefenseAllocations.end(); ++allocIt)
						{
							if (allocIt->first == candidate.zoneAnchor)
							{
								continue;
							}
							adapterLog(
								"zone_defense_retask zone=%u from_zone=%u units=%d reason=main_base_critical_override",
								candidate.zoneAnchor,
								allocIt->first,
								countAliveAssigned(allocIt->second));
						}
					}
					break;
				}

				if (selectedThreat != nullptr && selectedMaxUnits > 0)
				{
					const UnsignedInt threatenedZoneAnchor = selectedThreat->zoneAnchor;
					const Real threatPositionX = selectedThreat->threat.positionX;
					const Real threatPositionY = selectedThreat->threat.positionY;
					const std::string threatLevel = selectedThreat->threat.level;
					const int highestSeverity = selectedThreat->severity;
					const int maxUnits = selectedMaxUnits;
					const int existingAssigned = selectedExistingAssigned;
					const AIControlAdapterZoneDefenseBudgetResult budget = selectedBudget;
					const AIControlAdapterZoneDefenseAllocationResult allocationDecision = selectedAllocationDecision;
					const std::string donors = selectedDonors;
					const std::string allocationReason = selectedReason;
					const bool mainBaseCriticalOverride = selectedMainBaseCriticalOverride;
					const std::vector<UnsignedInt> allowedUnitIds = selectedAllowedUnitIds;

					Intent defenseIntent;
					defenseIntent.category = IntentCategory::DEFENSE_RESPONSE;
					defenseIntent.priority = highestSeverity >= 4 ? IntentPriority::CRITICAL : IntentPriority::HIGH;
					defenseIntent.commandName = "Game.AttackMove.DefendZoneSmart";
					defenseIntent.targetName = "threatened_zone";
					defenseIntent.reason = allocationDecision.shouldReinforce ? "zone_defense_reinforce" : "zone_under_attack";

					defenseIntent.executeFunc = [&, threatenedZoneAnchor, threatPositionX, threatPositionY, threatLevel, highestSeverity, defenseResponseCooldownMs, maxUnits, existingAssigned, budget, allocationDecision, donors, allocationReason, mainBaseCriticalOverride, allowedUnitIds](std::string& resultReason) -> bool {
						nlohmann::json message = nlohmann::json::object();
						message["type"] = "SessionCommand";
						message["cmd"] = "Game.AttackMove.DefendZoneSmart";
						message["args"] = nlohmann::json::object();
						message["args"]["target_x"] = threatPositionX;
						message["args"]["target_y"] = threatPositionY;
						message["args"]["zone_anchor"] = threatenedZoneAnchor;
						message["args"]["max_units"] = maxUnits;
						message["args"]["allowed_unit_ids"] = nlohmann::json::array();
						for (std::size_t allowedIdx = 0; allowedIdx < allowedUnitIds.size(); ++allowedIdx)
						{
							message["args"]["allowed_unit_ids"].push_back(allowedUnitIds[allowedIdx]);
						}

						AttackCommandResult defenseResult;
						const bool issued = executeGameAttackMoveDefendZoneSmart(message, resultReason, &defenseResult);

						if (issued && !defenseResult.assignedUnitIds.empty())
						{
							m_autonomy.state.lastDefenseResponseTick = now + defenseResponseCooldownMs;
							m_autonomy.state.lastDefendedZoneAnchor = threatenedZoneAnchor;
							m_autonomy.state.lastDefendedThreatSeverity = highestSeverity;

							const unsigned int defenseTaskId = m_autonomy.combatTaskManager.createTask(
								CombatTaskType::Defense,
								defenseResult.assignedUnitIds,
								defenseResult.targetPosition,
								"zone_defense",
								"defend_zone_" + std::to_string(threatenedZoneAnchor),
								budget.timeoutMs);

							AutonomyZoneDefenseAllocation& allocation = m_autonomy.state.zoneDefenseAllocations[threatenedZoneAnchor];
							if (allocation.zoneAnchorId == 0)
							{
								allocation.zoneAnchorId = threatenedZoneAnchor;
								allocation.createdTick = now;
							}
							allocation.threatLevel = threatLevel;
							allocation.threatSeverity = highestSeverity;
							allocation.targetX = defenseResult.targetPosition.x;
							allocation.targetY = defenseResult.targetPosition.y;
							allocation.taskId = defenseTaskId;
							allocation.taskIds.push_back(defenseTaskId);
							allocation.assignedUnitIds.insert(allocation.assignedUnitIds.end(), defenseResult.assignedUnitIds.begin(), defenseResult.assignedUnitIds.end());
							allocation.lastCommandTick = now;
							allocation.holdUntilTick = now + budget.minHoldMs;
							allocation.expiryTick = now + budget.timeoutMs;
							allocation.state = allocationDecision.shouldReinforce ? "reinforcing" : "assigned";
							if (mainBaseCriticalOverride && m_autonomy.state.mainBaseCriticalOverrideTelemetry.is_object())
							{
								m_autonomy.state.mainBaseCriticalOverrideTelemetry["assigned_units"] = existingAssigned + static_cast<int>(defenseResult.assignedUnitIds.size());
								m_autonomy.state.mainBaseCriticalOverrideTelemetry["requested_units"] = maxUnits;
							}

							adapterLog(
								"zone_defense_allocation zone=%u state=%s threat=%s assigned=%d requested=%d donors=%s reason=%s",
								threatenedZoneAnchor,
								allocation.state.c_str(),
								threatLevel.c_str(),
								existingAssigned + static_cast<int>(defenseResult.assignedUnitIds.size()),
								maxUnits,
								donors.c_str(),
								allocationReason.c_str());

							if (allocationDecision.shouldReinforce)
							{
								adapterLog(
									"zone_defense_reinforce zone=%u added=%d existing=%d reason=%s",
									threatenedZoneAnchor,
									static_cast<int>(defenseResult.assignedUnitIds.size()),
									existingAssigned,
									allocationReason.c_str());
							}

							adapterLog(
								"zone_defense_response zone_anchor=%u threat_level=%s severity=%d target_x=%.1f target_y=%.1f result=issued units=%d tick=%u",
								threatenedZoneAnchor,
								threatLevel.c_str(),
								highestSeverity,
								defenseResult.targetPosition.x,
								defenseResult.targetPosition.y,
								static_cast<int>(defenseResult.assignedUnitIds.size()),
								now);
							adapterLog(
								"zone_defense_response zone=%u issued=1 reason=%s",
								threatenedZoneAnchor,
								allocationReason.c_str());

							adapterLog(
								"combat_task_assigned task=%u type=defense units=%d target=(%.1f,%.1f) zone=%u reason=zone_under_attack",
								defenseTaskId,
								static_cast<int>(defenseResult.assignedUnitIds.size()),
								defenseResult.targetPosition.x,
								defenseResult.targetPosition.y,
								threatenedZoneAnchor);

							adapterLog(
								"combat_task_command task=%u type=defense command=Game.AttackMove.DefendZoneSmart issued=1 units=%d",
								defenseTaskId,
								static_cast<int>(defenseResult.assignedUnitIds.size()));
						}
						else if (!issued)
						{
							adapterLog(
								"zone_defense_skip zone_anchor=%u threat_level=%s reason=%s tick=%u",
								threatenedZoneAnchor,
								threatLevel.c_str(),
								resultReason.c_str(),
								now);
							adapterLog(
								"zone_defense_response zone=%u issued=0 reason=%s",
								threatenedZoneAnchor,
								resultReason.c_str());
							adapterLog(
								"combat_task_skip type=defense reason=%s",
								resultReason.c_str());
						}
						else if (defenseResult.assignedUnitIds.empty())
						{
							adapterLog(
								"zone_defense_skip zone_anchor=%u threat_level=%s reason=no_eligible_units tick=%u",
								threatenedZoneAnchor,
								threatLevel.c_str(),
								now);
							adapterLog(
								"zone_defense_response zone=%u issued=0 reason=no_eligible_units",
								threatenedZoneAnchor);
							adapterLog(
								"combat_task_skip type=defense reason=no_eligible_units");
						}

						return issued && !defenseResult.assignedUnitIds.empty();
					};

					m_autonomy.scheduler.SubmitIntent(defenseIntent);
					schedulerHasIntents = true;
				}
			}

			AIControlAdapterGlaUsaStrategyResult glaUsaStrategy;
			if (productionDue)
			{
				const Int armyCount = std::max<Int>(0, counts.mobileUnits - counts.workers);
				const bool surplusProductionPressure = isBalancedSprawl && money >= 100000u;

				// Prepare owned producers for zone selection
				std::vector<ProductionProducerSnapshot> producerSnapshots;
				producerSnapshots.reserve(ownedObjects.size());
				for (std::size_t i = 0; i < ownedObjects.size(); ++i)
				{
					const AutonomyOwnedObjectSnapshot& owned = ownedObjects[i];
					if (!owned.isStructure || owned.object == nullptr)
					{
						continue;
					}

					ProductionProducerSnapshot snapshot;
					snapshot.object = owned.object;
					snapshot.objectId = static_cast<int>(owned.object->getID());
					snapshot.isStructure = owned.isStructure;
					snapshot.underConstruction = owned.underConstruction;
					snapshot.isBarracks = owned.isBarracks;
					snapshot.isWarFactoryLike = owned.isWarFactoryLike;
					if (owned.object->getPosition() != nullptr)
					{
						snapshot.positionX = owned.object->getPosition()->x;
						snapshot.positionY = owned.object->getPosition()->y;
					}
					producerSnapshots.push_back(snapshot);
				}

				// Gather capability state
				const ScienceType scudLauncherScience = TheScienceStore != nullptr
					? TheScienceStore->getScienceFromInternalName("SCIENCE_ScudLauncher")
					: SCIENCE_INVALID;
				const bool hasScudLauncherScience = scudLauncherScience != SCIENCE_INVALID
					&& player->hasScience(scudLauncherScience);
				const UpgradeTemplate* captureUpgrade = TheUpgradeCenter != nullptr
					? TheUpgradeCenter->findUpgrade("Upgrade_InfantryCaptureBuilding")
					: nullptr;
				const bool hasCaptureUpgrade = captureUpgrade != nullptr && player->hasUpgradeComplete(captureUpgrade);
				Int captureSources = 0;
				for (std::size_t i = 0; i < ownedObjects.size(); ++i)
				{
					const auto& owned = ownedObjects[i];
					if (owned.object == nullptr || owned.underConstruction || !owned.hasCapturePower)
					{
						continue;
					}
					++captureSources;
				}

				// Phase 6.3: Capture source capacity tracking
				int captureSourcesLive = captureSources;
				int captureSourcesReserved = 0;
				int capturableTargetsRemaining = 0;
				int maxCaptureConcurrent = m_automation.captureRule.maxConcurrent;

				// Count reserved capture sources (units in active capture tasks)
				std::vector<SpecialTaskReservation*> captureTasks = m_autonomy.taskReservationManager.findCaptureTasks();
				for (const SpecialTaskReservation* task : captureTasks)
				{
					if (task == nullptr)
					{
						continue;
					}
					// Only count active (non-terminal) tasks
					if (task->state != SpecialTaskState::Complete &&
						task->state != SpecialTaskState::Failed &&
						task->state != SpecialTaskState::Expired)
					{
						++captureSourcesReserved;
					}
				}

				// Count capturable targets remaining (not friendly, not reserved by active task)
				std::vector<Object*> capturableTargets;
				collectCapturableTargetsForPlayer(player, capturableTargets);
				capturableTargetsRemaining = static_cast<int>(capturableTargets.size());

				// Compute available and desired capture sources
				int captureSourcesAvailable = captureSourcesLive - captureSourcesReserved;
				if (captureSourcesAvailable < 0)
				{
					captureSourcesAvailable = 0;
				}
				int desiredCaptureSources = 0;
				const char* capacityReason = "no_upgrade";
				if (hasCaptureUpgrade && m_automation.captureRule.enabled && capturableTargetsRemaining > 0)
				{
					// desired = min(maxConcurrent + 2, capturable_targets_remaining)
					const int targetReserve = maxCaptureConcurrent + 2;
					desiredCaptureSources = targetReserve < capturableTargetsRemaining ? targetReserve : capturableTargetsRemaining;
					capacityReason = captureSourcesAvailable < desiredCaptureSources ? "production_needed" : "capacity_met";
				}
				else if (!hasCaptureUpgrade)
				{
					capacityReason = "no_upgrade";
				}
				else if (!m_automation.captureRule.enabled)
				{
					capacityReason = "automation_disabled";
				}
				else
				{
					capacityReason = "no_targets";
				}

				bool brutalEmergencyPriority = false;
				bool mainUnderPressure = false;
				if (m_autonomy.state.brutalPressureTelemetry.is_object())
				{
					brutalEmergencyPriority =
						m_autonomy.state.brutalPressureTelemetry.value("chosen_priority", std::string()) == "emergency_survival";
					mainUnderPressure = m_autonomy.state.brutalPressureTelemetry.value("main_under_pressure", false);
				}
				bool activeUnitAttack = false;
				int criticalThreatZones = 0;
				int localEnemyCount = 0;
				int enemyArtilleryCount = 0;
				for (const auto& threatPair : m_autonomy.state.zoneThreats)
				{
					const AutonomyZoneThreatState& threat = threatPair.second;
					if ((now - threat.lastSeenTick) > 45000u)
					{
						continue;
					}
					localEnemyCount += threat.localEnemyCount;
					enemyArtilleryCount += threat.enemyArtilleryCount;
					if (threat.sourceType == "unit_attack")
					{
						activeUnitAttack = true;
					}
					if ((threat.level == "high" || threat.level == "critical") && threat.localEnemyCount > 0)
					{
						++criticalThreatZones;
					}
				}
				bool enemyUsaPlayerDetected = false;
				if (ThePlayerList != nullptr)
				{
					const Int playerCount = ThePlayerList->getPlayerCount();
					for (Int i = 0; i < playerCount; ++i)
					{
						Player* candidate = ThePlayerList->getNthPlayer(i);
						if (candidate == nullptr || candidate == player || candidate == ThePlayerList->getNeutralPlayer())
						{
							continue;
						}
						if (candidate->getDefaultTeam() == nullptr || player->getRelationship(candidate->getDefaultTeam()) != ENEMIES)
						{
							continue;
						}
						const PlayerTemplate* candidateTemplate = candidate->getPlayerTemplate();
						if (AIControlAdapterLooksLikeUsaIdentity(candidate->getSide().str())
							|| AIControlAdapterLooksLikeUsaIdentity(candidate->getBaseSide().str())
							|| (candidateTemplate != nullptr
								&& (AIControlAdapterLooksLikeUsaIdentity(candidateTemplate->getName().str())
									|| AIControlAdapterLooksLikeUsaIdentity(candidateTemplate->getSide().str())
									|| AIControlAdapterLooksLikeUsaIdentity(candidateTemplate->getBaseSide().str()))))
						{
							enemyUsaPlayerDetected = true;
							break;
						}
					}
				}
				int enemyUsaMemoryItems = 0;
				for (const EnemyMemoryItem& item : m_autonomy.enemyMemory.getItems())
				{
					if (AIControlAdapterLooksLikeUsaIdentity(item.templateName))
					{
						++enemyUsaMemoryItems;
					}
				}
				int enemyUsaWmdTargets = 0;
				for (const WMDTarget& target : m_autonomy.wmdTargetTracker.getAllTargets())
				{
					if (target.alive
						&& (containsIgnoreCase(target.templateName, "america")
							|| containsIgnoreCase(target.templateName, "particlecannon")))
					{
						++enemyUsaWmdTargets;
					}
				}
				const bool enemyUsaDetected = enemyUsaPlayerDetected || enemyUsaMemoryItems > 0 || enemyUsaWmdTargets > 0;
				const int activeDefenseTaskCount = m_autonomy.combatTaskManager.getDefenseTaskCount();
				const int activeCombatTaskCount = m_autonomy.combatTaskManager.getActiveTaskCount();
				const int activeAttackTaskEstimate = std::max(0, activeCombatTaskCount - activeDefenseTaskCount);
				const int currentZoneCount =
					static_cast<int>(m_autonomy.state.telemetryZones.is_array() ? m_autonomy.state.telemetryZones.size() : 0u);
				const int desiredZoneCountForStrategy =
					profilePolicyManager.ResolveStrategicSpendDesiredZoneCount(
						profilePolicyConfig,
						profilePolicyConfig.sprawlSupplyCap);
				int exposedExpansionZones = 0;
				float farthestRemoteBuildDistance = 0.0f;
				if (m_autonomy.state.telemetryZones.is_array())
				{
					for (const auto& zoneJson : m_autonomy.state.telemetryZones)
					{
						if (!zoneJson.is_object())
						{
							continue;
						}
						const int zoneTunnels = zoneJson.value("tunnels", 0);
						const int zoneStingers = zoneJson.value("stingers", 0);
						const bool isMainBaseZone = zoneJson.value("is_main_base", false);
						if (!isMainBaseZone && (zoneTunnels + zoneStingers) <= 0)
						{
							++exposedExpansionZones;
						}
						if (hasActiveZone && zoneJson.contains("center_x") && zoneJson.contains("center_y"))
						{
							const float dx = zoneJson.value("center_x", activeZone.center.x) - activeZone.center.x;
							const float dy = zoneJson.value("center_y", activeZone.center.y) - activeZone.center.y;
							farthestRemoteBuildDistance = std::max(farthestRemoteBuildDistance, std::sqrt((dx * dx) + (dy * dy)));
						}
					}
				}
				AIControlAdapterGlaUsaStrategyInput glaUsaInput;
				glaUsaInput.enemyUsaDetected = enemyUsaDetected;
				glaUsaInput.enemyUsaWmdTargets = enemyUsaWmdTargets;
				glaUsaInput.enemyArmorThreats = criticalThreatZones > 0 ? localEnemyCount : 0;
				glaUsaInput.enemyAirThreats = 0;
				glaUsaInput.enemyMixedThreats = enemyArtilleryCount > 0 && localEnemyCount > 0 ? localEnemyCount : 0;
				glaUsaInput.criticalZoneCount = criticalThreatZones;
				glaUsaInput.mainBaseCritical = mainUnderPressure;
				glaUsaInput.producerSpineBroken =
					m_autonomy.state.survivalPolicyTelemetry.is_object()
					&& m_autonomy.state.survivalPolicyTelemetry.value("state", std::string()) == "producer_spine_broken";
				glaUsaInput.collapseImminent =
					m_autonomy.state.survivalPolicyTelemetry.is_object()
					&& m_autonomy.state.survivalPolicyTelemetry.value("state", std::string()) == "collapse_imminent";
				glaUsaInput.money = money;
				glaUsaInput.reserveCash = reserveCash;
				glaUsaInput.completedPalaces = hasCompletedPalace ? counts.palaces : 0;
				glaUsaInput.readyBarracks = counts.barracks;
				glaUsaInput.readyArmsDealers = counts.armsDealers;
				glaUsaInput.tunnels = counts.tunnels;
				glaUsaInput.stingers = counts.stingers;
				glaUsaInput.exposedExpansionZones = exposedExpansionZones;
				glaUsaInput.quads = counts.quads;
				glaUsaInput.queuedQuads = counts.queuedQuads;
				glaUsaInput.scorpions = counts.scorpions;
				glaUsaInput.queuedScorpions = counts.queuedScorpions;
				glaUsaInput.rocketBuggies = counts.rocketBuggies;
				glaUsaInput.armyCount = armyCount;
				glaUsaInput.activeDefenseTasks = activeDefenseTaskCount;
				glaUsaInput.activeAttackTasks = activeAttackTaskEstimate;
				glaUsaInput.workers = counts.workers;
				glaUsaInput.technicals = counts.technicals;
				glaUsaInput.queuedTechnicals = counts.queuedTechnicals;
				glaUsaInput.remoteBuildGap = std::max(0, desiredZoneCountForStrategy - currentZoneCount);
				glaUsaInput.farthestRemoteBuildDistance = farthestRemoteBuildDistance;
				glaUsaStrategy = AIControlAdapterGlaUsaStrategyManager().Evaluate(glaUsaInput);
				const unsigned int glaUsaCashFloat = money > reserveCash ? money - reserveCash : 0u;
				m_autonomy.state.glaUsaStrategyTelemetry = nlohmann::json::object({
					{"active", glaUsaStrategy.active},
					{"usa_enemy", enemyUsaDetected},
					{"pressure", glaUsaStrategy.pressure},
					{"reason", glaUsaStrategy.reason},
					{"tunnel_role", glaUsaStrategy.tunnelRole},
					{"tunnel_missiles_satisfy_armor", glaUsaStrategy.tunnelMissilesSatisfyArmor},
					{"scorpion_floor", glaUsaStrategy.scorpionFloor},
					{"quad_floor", glaUsaStrategy.quadFloor},
					{"prefer_scorpion_production", glaUsaStrategy.preferScorpionProduction},
					{"prefer_quad_production", glaUsaStrategy.preferQuadProduction},
					{"preserve_attack_group", glaUsaStrategy.preserveAttackGroup},
					{"reserved_strike_group", glaUsaStrategy.reservedStrikeGroup},
					{"prioritize_producer_recovery", glaUsaStrategy.prioritizeProducerRecovery},
					{"enemy_usa_wmd_targets", enemyUsaWmdTargets},
					{"enemy_usa_player_detected", enemyUsaPlayerDetected},
					{"enemy_usa_memory_items", enemyUsaMemoryItems},
					{"armor_threats", glaUsaInput.enemyArmorThreats},
					{"air_threats", glaUsaInput.enemyAirThreats},
					{"mixed_threats", glaUsaInput.enemyMixedThreats},
					{"exposed_expansion_zones", exposedExpansionZones},
					{"camouflage", nlohmann::json::object({
						{"desired", glaUsaStrategy.camouflageDesired},
						{"spend_allowed", glaUsaStrategy.camouflageSpendAllowed},
						{"reason", glaUsaStrategy.camouflageReason},
						{"cash_float", glaUsaCashFloat},
						{"palace", hasCompletedPalace ? 1 : 0},
						{"tunnels", counts.tunnels},
						{"stingers", counts.stingers},
						{"usa_enemy", enemyUsaDetected}
					})},
					{"worker_mobility", nlohmann::json::object({
						{"desired", glaUsaStrategy.workerMobilityDesired},
						{"mode", glaUsaStrategy.workerMobilityMode},
						{"workers", counts.workers},
						{"technicals", counts.technicals},
						{"queued_technicals", counts.queuedTechnicals},
						{"desired_shuttle_technicals", glaUsaStrategy.desiredShuttleTechnicals},
						{"protected_technicals", glaUsaStrategy.protectedShuttleTechnicals},
						{"production_needed", glaUsaStrategy.shuttleTechnicalProductionNeeded},
						{"remote_gap", glaUsaInput.remoteBuildGap},
						{"farthest_distance", farthestRemoteBuildDistance},
						{"reservation_reason", glaUsaStrategy.shuttleReservationReason},
						{"reason", glaUsaStrategy.workerMobilityReason}
					})},
					{"wmd_construction_diagnostic_needed", glaUsaStrategy.wmdConstructionDiagnosticNeeded},
					{"wmd_construction_reason", glaUsaStrategy.wmdConstructionReason}
				});
				adapterLog(
					"gla_usa_strategy usa_enemy=%d pressure=%s scorpion_floor=%d quad_floor=%d tunnel_role=%s preserve_attack=%d reason=%s",
					enemyUsaDetected ? 1 : 0,
					glaUsaStrategy.pressure,
					glaUsaStrategy.scorpionFloor,
					glaUsaStrategy.quadFloor,
					glaUsaStrategy.tunnelRole,
					glaUsaStrategy.preserveAttackGroup ? 1 : 0,
					glaUsaStrategy.reason);
				adapterLog(
					"camouflage_policy desired=%d spend_allowed=%d reason=%s cash_float=%lu palace=%d tunnels=%d stingers=%d usa_enemy=%d",
					glaUsaStrategy.camouflageDesired ? 1 : 0,
					glaUsaStrategy.camouflageSpendAllowed ? 1 : 0,
					glaUsaStrategy.camouflageReason,
					static_cast<unsigned long>(glaUsaCashFloat),
					hasCompletedPalace ? 1 : 0,
					counts.tunnels,
					counts.stingers,
					enemyUsaDetected ? 1 : 0);
				adapterLog(
					"worker_mobility_policy desired=%d mode=%s workers=%d technicals=%d queued=%d remote_gap=%d protected=%d production_needed=%d reason=%s reservation_reason=%s",
					glaUsaStrategy.workerMobilityDesired ? 1 : 0,
					glaUsaStrategy.workerMobilityMode,
					counts.workers,
					counts.technicals,
					counts.queuedTechnicals,
					glaUsaInput.remoteBuildGap,
					glaUsaStrategy.protectedShuttleTechnicals,
					glaUsaStrategy.shuttleTechnicalProductionNeeded ? 1 : 0,
					glaUsaStrategy.workerMobilityReason,
					glaUsaStrategy.shuttleReservationReason);

				const Int armyCapForLog = AIControlAdapterGetEffectiveArmyCap({
					isBalancedSprawl,
					money,
					static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute)),
					counts.barracks,
					counts.armsDealers,
					counts.blackMarkets,
					profilePolicyManager.ResolveCombatArmyCapBase(profilePolicyConfig)
				});
				{
					const int baseArmyCap = profilePolicyManager.ResolveCombatArmyCapBase(profilePolicyConfig);
					const int productionCapacity = std::max(0, counts.barracks) + std::max(0, counts.armsDealers);
					const int producerSupportedCap = 100 + (productionCapacity * 3);
					const int incomePerMinuteForCap = static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute));
					const int incomeSupportedCap = 100 + (std::max(0, incomePerMinuteForCap) / 500);
					int cashMarketFloor = 0;
					if (money >= 100000u && counts.blackMarkets >= 8)
					{
						cashMarketFloor = 150;
					}
					if ((money >= 250000u || counts.blackMarkets >= 20) && counts.blackMarkets >= 8)
					{
						cashMarketFloor = std::max(cashMarketFloor, 220);
					}
					if (money >= 250000u && counts.blackMarkets >= 20)
					{
						cashMarketFloor = std::max(cashMarketFloor, 260);
					}
					if (money >= 300000u && counts.blackMarkets >= 20)
					{
						cashMarketFloor = std::max(cashMarketFloor, 300);
					}
					const char* capReason =
						!isBalancedSprawl ? "profile_static" :
						money < 100000u ? "cash_below_surplus" :
						cashMarketFloor > incomeSupportedCap ? "cash_market_floor" :
						"income_supported";
					adapterLog(
						"army_cap_policy profile=%s base=%d effective=%d money=%lu income_per_min=%d producers=%d markets=%d income_supported=%d producer_supported=%d cash_floor=%d reason=%s",
						profile.c_str(),
						baseArmyCap,
						armyCapForLog,
						static_cast<unsigned long>(money),
						incomePerMinuteForCap,
						productionCapacity,
						counts.blackMarkets,
						incomeSupportedCap,
						producerSupportedCap,
						cashMarketFloor,
						capReason);
				}
				const AIControlAdapterEmergencySurvivalProductionDecision emergencyDecision =
					AIControlAdapterChooseEmergencySurvivalProduction({
						brutalEmergencyPriority,
						mainUnderPressure,
						activeUnitAttack,
						criticalThreatZones,
						localEnemyCount,
						enemyArtilleryCount,
						money,
						reserveCash,
						counts.armsDealers,
						counts.barracks,
						counts.palaces,
						counts.quads,
						counts.scorpions,
						counts.rpg,
						counts.soldiers,
						armyCount,
						armyCapForLog
					});
				AIControlAdapterStrategicSpendEconomyFacts emergencySpendEconomy;
				emergencySpendEconomy.money = money;
				emergencySpendEconomy.reserveCash = reserveCash;
				emergencySpendEconomy.completedMarkets = counts.blackMarkets;
				emergencySpendEconomy.healthyMarketsInProgress = static_cast<int>(inProgressBlackMarkets);
				emergencySpendEconomy.staleMarketFoundations = static_cast<int>(staleBlackMarketFoundations);
				emergencySpendEconomy.staleStrategicFoundations = staleStrategicFoundations;
				emergencySpendEconomy.activeWmdThreats = m_autonomy.wmdTargetTracker.hasActiveWMDThreat() ? 1 : 0;
				emergencySpendEconomy.mainBaseCritical = mainUnderPressure;
				AIControlAdapterStrategicSpendZoneFacts emergencySpendZones;
				emergencySpendZones.currentZones =
					static_cast<int>(m_autonomy.state.telemetryZones.is_array() ? m_autonomy.state.telemetryZones.size() : 0u);
				emergencySpendZones.developedZones = 0;
				emergencySpendZones.desiredZones =
					profilePolicyManager.ResolveStrategicSpendDesiredZoneCount(
						profilePolicyConfig,
						profilePolicyConfig.sprawlSupplyCap);
				AIControlAdapterStrategicSpendArmyFacts emergencySpendArmy;
				emergencySpendArmy.armySize = armyCount;
				emergencySpendArmy.armyCap = armyCapForLog;
				emergencySpendArmy.quads = counts.quads;
				emergencySpendArmy.buggies = counts.rocketBuggies;
				emergencySpendArmy.scorpions = counts.scorpions;
				const AIControlAdapterStrategicSpendSnapshotBase emergencySpendBase =
					strategicSpendSnapshotBuilder.BuildBase(emergencySpendEconomy, emergencySpendZones, emergencySpendArmy);
				AIControlAdapterStrategicSpendSnapshotOverrides emergencySpendOverrides;
				emergencySpendOverrides.activeLocalEnemies = localEnemyCount;
				emergencySpendOverrides.emergencySurvivalActive = emergencyDecision.active;
				emergencySpendOverrides.incomeCritical = counts.blackMarkets <= 0;
				emergencySpendOverrides.reserveDepleted = money < reserveCash;
				const AIControlAdapterStrategicSpendSnapshot emergencySpendSnapshot =
					strategicSpendSnapshotBuilder.Build(emergencySpendBase, emergencySpendOverrides);
				const AIControlAdapterStrategicSpendManager strategicSpendManager;
				const AIControlAdapterStrategicSpendDecision emergencySpend =
					strategicSpendManager.Evaluate(emergencySpendSnapshot, StrategicSpendCategory::EmergencyDefenseUnits, 700u);
				adapterLog(
					"emergency_spend_gate money=%lu reserve=%lu allow=%d batch_limit=%d protected_cash=%lu reason=%s",
					static_cast<unsigned long>(money),
					static_cast<unsigned long>(reserveCash),
					emergencySpend.allowed ? 1 : 0,
					emergencySpend.batchLimit,
					static_cast<unsigned long>(emergencySpend.protectedCash),
					emergencySpend.reason);
				adapterLog(
					"%s",
					strategicSpendTelemetrySerializer.BuildPolicyLogLine({
						StrategicSpendCategory::EmergencyDefenseUnits,
						money,
						reserveCash,
						&emergencySpend
					}).c_str());
				m_autonomy.state.emergencySurvivalTelemetry = nlohmann::json::object({
					{"active", emergencyDecision.active},
					{"allow_reserve_spend", emergencyDecision.allowReserveSpend},
					{"suppress_capture_source_production", emergencyDecision.suppressCaptureSourceProduction},
					{"bypass_army_cap_buffer", emergencyDecision.bypassArmyCapBuffer},
					{"emergency_army_cap", emergencyDecision.emergencyArmyCap},
					{"spend_allowed", emergencySpend.allowed},
					{"batch_limit", emergencySpend.batchLimit},
					{"reason", emergencyDecision.reason}
				});
				m_autonomy.state.strategicSpendTelemetry["emergency_batch_limit"] = emergencySpend.batchLimit;
				strategicSpendTelemetrySerializer.RecordCategory(
					m_autonomy.state.strategicSpendTelemetry,
					StrategicSpendCategory::EmergencyDefenseUnits,
					700u,
					emergencySpend);
				adapterLog(
					"emergency_survival_policy active=%d main_under_pressure=%d local_enemies=%d artillery=%d money=%lu reserve=%lu army=%d/%d reason=%s",
					emergencyDecision.active ? 1 : 0,
					mainUnderPressure ? 1 : 0,
					localEnemyCount,
					enemyArtilleryCount,
					static_cast<unsigned long>(money),
					static_cast<unsigned long>(reserveCash),
					armyCount,
					armyCapForLog,
					emergencyDecision.reason);

				if (emergencyDecision.suppressCaptureSourceProduction && captureSourcesLive > 0)
				{
					capacityReason = "suppressed_emergency_survival";
					desiredCaptureSources = captureSourcesAvailable;
				}

				// Phase 6.3: Log capture source policy
				const bool productionNeeded = captureSourcesAvailable < desiredCaptureSources && desiredCaptureSources > 0;
				adapterLog(
					"capture_source_policy live=%d reserved=%d available=%d desired=%d targets=%d max_concurrent=%d production_needed=%d reason=%s",
					captureSourcesLive,
					captureSourcesReserved,
					captureSourcesAvailable,
					desiredCaptureSources,
					capturableTargetsRemaining,
					maxCaptureConcurrent,
					productionNeeded ? 1 : 0,
					capacityReason);

				// Build ProductionManager inputs
				ProductionManagerInputs inputs;
				inputs.money = money;
				inputs.incomePerMinute = static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute));
				inputs.reserveCash = reserveCash;
				inputs.armyCount = armyCount;
				inputs.soldiers = counts.soldiers;
				inputs.rpg = counts.rpg;
				inputs.quads = counts.quads;
				inputs.scorpions = counts.scorpions;
				inputs.scudLaunchers = counts.scudLaunchers;
				inputs.radarVans = counts.radarVans;
				inputs.barracks = counts.barracks;
				inputs.armsDealers = counts.armsDealers;
				inputs.blackMarkets = counts.blackMarkets;
				inputs.palaces = counts.palaces;
				inputs.hasCompletedPalace = hasCompletedPalace;
				inputs.queuedProductionEntries = counts.queuedProductionEntries;
				inputs.queuedQuads = counts.queuedQuads;
				inputs.queuedScorpions = counts.queuedScorpions;
				inputs.queuedScudLaunchers = counts.queuedScudLaunchers;
				inputs.hasScudLauncherScience = hasScudLauncherScience;
				inputs.hasCaptureUpgrade = hasCaptureUpgrade;
				inputs.captureSources = captureSources;
				inputs.captureSourcesLive = captureSourcesLive;
				inputs.captureSourcesReserved = captureSourcesReserved;
				inputs.captureSourcesAvailable = captureSourcesAvailable;
				inputs.capturableTargetsRemaining = capturableTargetsRemaining;
				inputs.desiredCaptureSources = desiredCaptureSources;
				inputs.maxCaptureConcurrent = maxCaptureConcurrent;
				inputs.hasActiveZone = hasActiveZone;
				if (hasActiveZone)
				{
					inputs.activeZoneCenterX = activeZone.center.x;
					inputs.activeZoneCenterY = activeZone.center.y;
				}
				inputs.zoneRadius = m_autonomy.state.zoneRadius;
				inputs.openingInfrastructureReady = totalSupplyStashes >= 1 && totalBarracks >= 1 && totalArmsDealers >= 1;
				inputs.openingEconomyReady = totalSupplyStashes >= 2 || totalBlackMarkets >= 1;
				inputs.wasRecoveringFromReserve =
					(m_autonomy.state.lastDecisionCategory == "production" && m_autonomy.state.lastDecisionReason == "reserve_cash_recovery");
				inputs.wasArmyCapReached =
					(m_autonomy.state.lastDecisionCategory == "production" && m_autonomy.state.lastDecisionReason == "army_cap_reached");
				inputs.profile = profile.c_str();
				inputs.isBalancedSprawl = isBalancedSprawl;
				inputs.surplusProductionPressure = surplusProductionPressure;
				inputs.ownedProducers = &producerSnapshots;

				// Get production decision from ProductionManager
				ProductionIntent prodIntent = m_autonomy.productionManager.ChooseProduction(inputs, now);
				bool emergencyProductionCommand = false;
				if (emergencyDecision.active)
				{
					if (emergencySpend.allowed && emergencySpend.batchLimit > 0 && !emergencyDecision.commands.empty())
					{
						const char* emergencyCommand = AIControlAdapterChooseEmergencySurvivalProductionCommand(
							emergencyDecision,
							counts.quads,
							counts.queuedQuads,
							counts.scorpions,
							counts.queuedScorpions);
						prodIntent.shouldProduce = true;
						prodIntent.commandName = emergencyCommand != nullptr ? emergencyCommand : emergencyDecision.commands[0];
						prodIntent.producerKind = "any";
						prodIntent.producerObjectId = -1;
						prodIntent.unitTemplate = "";
						prodIntent.reason = emergencyDecision.reason;
						if (emergencySpend.batchLimit <= 1 && prodIntent.commandName == "Game.QueueQuadsAllWarFactories")
						{
							for (const ProductionProducerSnapshot& producer : producerSnapshots)
							{
								if (producer.isWarFactoryLike && !producer.underConstruction && producer.objectId > 0)
								{
									prodIntent.producerKind = "arms_dealer";
									prodIntent.producerObjectId = producer.objectId;
									break;
								}
							}
						}
						emergencyProductionCommand = true;
					}
					else
					{
						adapterLog(
							"emergency_survival_production command=none unit=none producers=0 issued_count=0 reserve_spend=%d army_cap_override=%d batch_limit=%d reason=%s",
							emergencyDecision.allowReserveSpend ? 1 : 0,
							emergencyDecision.bypassArmyCapBuffer ? 1 : 0,
							emergencySpend.batchLimit,
							emergencySpend.allowed ? emergencyDecision.reason : emergencySpend.reason);
						}
					}

					if (glaUsaStrategy.active && counts.armsDealers > 0)
					{
						if (glaUsaStrategy.preferScorpionProduction)
						{
							prodIntent.shouldProduce = true;
							prodIntent.commandName = "Game.QueueScorpionsAllWarFactories";
							prodIntent.producerKind = "any";
							prodIntent.producerObjectId = -1;
							prodIntent.unitTemplate = "";
							prodIntent.reason = "gla_usa_armor_scorpion_floor";
						}
						else if (glaUsaStrategy.preferQuadProduction)
						{
							prodIntent.shouldProduce = true;
							prodIntent.commandName = "Game.QueueQuadsAllWarFactories";
							prodIntent.producerKind = "any";
							prodIntent.producerObjectId = -1;
							prodIntent.unitTemplate = "";
							prodIntent.reason = "gla_usa_quad_floor";
						}
					}

					// Phase 6.3: Log capture source production decisions
					if (prodIntent.shouldProduce && prodIntent.reason == "capture_utility_reserve")
				{
					adapterLog(
						"capture_source_army_cap_override army_count=%d army_cap=%d desired=%d reason=capture_utility_reserve",
						armyCount,
						armyCapForLog,
						desiredCaptureSources);
					adapterLog(
						"capture_source_production command=%s reason=%s available=%d desired=%d",
						prodIntent.commandName.c_str(),
						prodIntent.reason.c_str(),
						captureSourcesAvailable,
						desiredCaptureSources);
				}
				else if (prodIntent.shouldProduce && productionNeeded && prodIntent.commandName == "Game.QueueSoldiersAllBarracks")
				{
					// Production for capture capacity (not at army cap)
					adapterLog(
						"capture_source_production command=%s reason=capture_capacity available=%d desired=%d",
						prodIntent.commandName.c_str(),
						captureSourcesAvailable,
						desiredCaptureSources);
				}

				const int activeScoutAssignments = m_autonomy.combatTaskManager.getScoutAssignedUnitCount();
				const int readyScudsForScouting = countReadyScudStorms(player);
				const int freshTargetsForScouting = countFreshScudStrategicTargets(now);
				const bool scudTargetStarvedForProduction = readyScudsForScouting > 0 && freshTargetsForScouting <= 0;
				const bool freshScoutCoverage = freshTargetsForScouting > 0 && countStaleScudStrategicTargets(now) <= 0;
				const CombatTaskScoutPoolDecision scoutPoolDecision = evaluateCombatTaskScoutPool({
					counts.armsDealers > 0,
					scudTargetStarvedForProduction,
					freshScoutCoverage,
					static_cast<unsigned int>(money),
					static_cast<unsigned int>(reserveCash),
					counts.technicals,
					counts.queuedTechnicals,
					activeScoutAssignments
				});
				int scoutPoolProducerId = -1;
				for (const ProductionProducerSnapshot& producer : producerSnapshots)
				{
					if (producer.isWarFactoryLike && !producer.underConstruction && producer.objectId > 0)
					{
						scoutPoolProducerId = producer.objectId;
						break;
					}
				}
				adapterLog(
					"scout_pool_policy desired=%d live=%d queued=%d assigned=%d production_needed=%d money=%lu reserve=%lu reason=%s",
					scoutPoolDecision.desiredTechnicals,
					counts.technicals,
					counts.queuedTechnicals,
					activeScoutAssignments,
					scoutPoolDecision.productionNeeded ? 1 : 0,
					static_cast<unsigned long>(money),
					static_cast<unsigned long>(reserveCash),
					scoutPoolDecision.reason);
				if (m_autonomy.state.scoutingTelemetry.is_object())
				{
					m_autonomy.state.scoutingTelemetry["scout_pool_desired"] = scoutPoolDecision.desiredTechnicals;
					m_autonomy.state.scoutingTelemetry["scout_pool_live"] = counts.technicals;
					m_autonomy.state.scoutingTelemetry["scout_pool_queued"] = counts.queuedTechnicals;
					m_autonomy.state.scoutingTelemetry["scout_pool_assigned"] = activeScoutAssignments;
				}
				if (!emergencyDecision.active
					&& scoutPoolDecision.productionNeeded
					&& scoutPoolProducerId > 0
					&& (!prodIntent.shouldProduce
						|| prodIntent.commandName == "Game.QueueQuadsAllWarFactories"
						|| prodIntent.commandName == "Game.QueueScorpionsAllWarFactories"))
				{
					prodIntent.shouldProduce = true;
					prodIntent.commandName = "Game.QueueUnit";
					prodIntent.producerKind = "arms_dealer";
					prodIntent.producerObjectId = scoutPoolProducerId;
					prodIntent.unitTemplate = scoutPoolDecision.unitTemplate;
					prodIntent.reason = scoutPoolDecision.reason;
				}
				if (glaUsaStrategy.shuttleTechnicalProductionNeeded
					&& scoutPoolProducerId > 0
					&& (!prodIntent.shouldProduce
						|| prodIntent.commandName == "Game.QueueQuadsAllWarFactories"
						|| prodIntent.commandName == "Game.QueueScorpionsAllWarFactories"))
				{
					std::string shuttleTechnicalTemplate = "GLAVehicleTechnical";
					for (const ProductionProducerSnapshot& producer : producerSnapshots)
					{
						if (producer.objectId == scoutPoolProducerId)
						{
							const std::string inferredTemplate = AIControlAdapterTemplateInferenceService::InferTechnicalTemplateForProducer(producer.object);
							if (!inferredTemplate.empty())
							{
								shuttleTechnicalTemplate = inferredTemplate;
							}
							break;
						}
					}
					prodIntent.shouldProduce = true;
					prodIntent.commandName = "Game.QueueUnit";
					prodIntent.producerKind = "arms_dealer";
					prodIntent.producerObjectId = scoutPoolProducerId;
					prodIntent.unitTemplate = shuttleTechnicalTemplate;
					prodIntent.reason = "worker_shuttle_technical";
					adapterLog(
						"worker_shuttle_production command=Game.QueueUnit unit=%s selected=1 desired=%d live=%d queued=%d reason=%s",
						shuttleTechnicalTemplate.c_str(),
						glaUsaStrategy.desiredShuttleTechnicals,
						counts.technicals,
						counts.queuedTechnicals,
						glaUsaStrategy.shuttleReservationReason);
				}
				else if (glaUsaStrategy.workerMobilityDesired)
				{
					adapterLog(
						"worker_shuttle_production command=Game.QueueUnit unit=auto selected=0 desired=%d live=%d queued=%d reason=%s",
						glaUsaStrategy.desiredShuttleTechnicals,
						counts.technicals,
						counts.queuedTechnicals,
						glaUsaStrategy.shuttleTechnicalProductionNeeded ? "producer_unavailable_or_higher_priority" : glaUsaStrategy.shuttleReservationReason);
				}

				bool mobileSiegeThreatVisible = false;
				if (m_autonomy.state.counterbatteryTelemetry.is_object())
				{
					const auto threatsIt = m_autonomy.state.counterbatteryTelemetry.find("mobile_siege_threats");
					mobileSiegeThreatVisible = threatsIt != m_autonomy.state.counterbatteryTelemetry.end()
						&& threatsIt->is_array()
						&& !threatsIt->empty();
				}
				const bool buggyPrereqReady = counts.armsDealers > 0 && hasCompletedPalace;
				const bool buggyBaselineSurplus =
					hasCompletedPalace
					&& counts.armsDealers > 0
					&& money >= (reserveCash + 3000u);
				const bool buggySpendAllowed =
					economyPolicy.combatSpendingMode != CombatSpendingMode::BlockedExceptDefense
					&& !(economyPolicy.prioritizeIncomeBuild && !mobileSiegeThreatVisible && !buggyBaselineSurplus);
				const AIControlAdapterRocketBuggyMixResult buggyMix = AIControlAdapterChooseRocketBuggyMix({
					buggyPrereqReady,
					buggySpendAllowed,
					mobileSiegeThreatVisible,
					counts.rocketBuggies,
					counts.queuedRocketBuggies,
					counts.quads,
					counts.scorpions,
					counts.scudLaunchers
				});
				adapterLog(
					"vehicle_mix_policy scorpions=%d quads=%d buggies=%d queued_buggies=%d desired_buggies=%d reason=%s",
					counts.scorpions,
					counts.quads,
					counts.rocketBuggies,
					counts.queuedRocketBuggies,
					buggyMix.desiredBuggies,
					buggyMix.reason);
				if (!emergencyDecision.active
					&& buggyMix.productionNeeded
					&& counts.warFactoryLikeProducers > 0
					&& (!prodIntent.shouldProduce
						|| prodIntent.commandName == "Game.QueueQuadsAllWarFactories"
						|| prodIntent.commandName == "Game.QueueScorpionsAllWarFactories"
						|| mobileSiegeThreatVisible))
				{
					prodIntent.shouldProduce = true;
					prodIntent.commandName = "Game.QueueRocketBuggiesAllWarFactories";
					prodIntent.producerKind = "arms_dealer";
					prodIntent.producerObjectId = -1;
					prodIntent.unitTemplate = "GLAVehicleRocketBuggy";
					prodIntent.reason = buggyMix.reason;
				}

				// Phase 7.5: Mobile SCUD Launcher Reserve - DISABLED
				// Mobile SCUD Launcher trucks are too slow/vulnerable for reliable counter-WMD
				// Use stationary SCUD Storm structures instead (see evaluateDefensiveScudStorm)
				// Mobile SCUD Launchers return to normal production mix
				bool scudCounterbatteryOverride = false;

				const int availableScuds = counts.scudLaunchers;
				const int queuedScuds = counts.queuedScudLaunchers;

				// Log for telemetry tracking
				adapterLog(
					"mobile_scud_launcher_policy baseline_reserve_enabled=0 live=%d queued=%d reason=phase_7_5_uses_scud_storm_structures",
					availableScuds,
					queuedScuds);

				// Phase 7.5: Mobile SCUD override code removed
				// Normal production manager handles mobile SCUD Launchers as part of vehicle mix
				// (scudCounterbatteryOverride remains false)

				// Economy policy enforcement: block or suppress production based on spending mode
				if (!emergencyDecision.allowReserveSpend && !scudCounterbatteryOverride && prodIntent.shouldProduce && economyPolicy.combatSpendingMode == CombatSpendingMode::BlockedExceptDefense)
				{
					// Block normal combat production during critical income/reserve pressure
					// (Defense production would still be allowed via DefenseManager intents)
					prodIntent.shouldProduce = false;
					prodIntent.reason = "economy_policy_blocked";
				}
				else if (!scudCounterbatteryOverride && prodIntent.shouldProduce && economyPolicy.combatSpendingMode == CombatSpendingMode::Conservative)
				{
					// Conservative mode: reduce production pressure (could skip some production ticks)
					// For now, allow production but note it in reason if it was reserve-related
					if (prodIntent.reason == "reserve_cash_recovery")
					{
						// Already conservative, keep it
					}
				}

				// Always submit production intent to scheduler (even for hold states)
				Intent schedulerIntent;
				schedulerIntent.category = IntentCategory::PRODUCTION;
				schedulerIntent.priority = IntentPriority::NORMAL;
				schedulerIntent.commandName = prodIntent.shouldProduce ? prodIntent.commandName : "none";
				schedulerIntent.targetName = prodIntent.shouldProduce ? prodIntent.unitTemplate : "";
				schedulerIntent.reason = prodIntent.reason;
				schedulerIntent.producerObjectId = prodIntent.producerObjectId;
				schedulerIntent.producerKind = prodIntent.producerKind;

				if (prodIntent.shouldProduce)
				{
					const bool emergencyProductionLog = emergencyProductionCommand;
					const bool emergencyReserveSpend = emergencyDecision.allowReserveSpend;
					const bool emergencyArmyCapOverride = emergencyDecision.bypassArmyCapBuffer;
					const std::string emergencyReason = emergencyDecision.reason != nullptr ? emergencyDecision.reason : "";
					const int emergencyBatchLimitForLog = emergencySpend.batchLimit;
					// Capture production execution logic in callback
					schedulerIntent.executeFunc = [&, prodIntent, producerSnapshots, emergencyProductionLog, emergencyReserveSpend, emergencyArmyCapOverride, emergencyReason, emergencyBatchLimitForLog](std::string& resultReason) -> bool {
						if (prodIntent.producerObjectId == -1)
						{
							// Use "all" command (all eligible producers)
							const bool issued = tryCommand("auto_prod", prodIntent.commandName.c_str(),
								nlohmann::json::object({ {"count", 1} }), resultReason);
							if (emergencyProductionLog)
							{
								int producerCount = 0;
								if (prodIntent.commandName == "Game.QueueQuadsAllWarFactories")
								{
									for (const ProductionProducerSnapshot& producer : producerSnapshots)
									{
										if (producer.isWarFactoryLike && !producer.underConstruction)
										{
											++producerCount;
										}
									}
								}
								adapterLog(
									"emergency_survival_production command=%s unit=%s producers=%d issued_count=%d reserve_spend=%d army_cap_override=%d batch_limit=%d reason=%s",
									prodIntent.commandName.c_str(),
									prodIntent.unitTemplate.empty() ? "auto" : prodIntent.unitTemplate.c_str(),
									producerCount,
									issued ? 1 : 0,
									emergencyReserveSpend ? 1 : 0,
									emergencyArmyCapOverride ? 1 : 0,
									emergencyBatchLimitForLog,
									issued ? emergencyReason.c_str() : resultReason.c_str());
							}
							return issued;
						}
						else
						{
							// Use specific producer via Game.QueueUnit
							std::string unitTemplate;
							Object* referenceProducer = nullptr;
							for (std::size_t i = 0; i < producerSnapshots.size(); ++i)
							{
								if (producerSnapshots[i].objectId == prodIntent.producerObjectId)
								{
									referenceProducer = producerSnapshots[i].object;
									break;
								}
							}

							if (!prodIntent.unitTemplate.empty())
							{
								unitTemplate = prodIntent.unitTemplate;
							}
							else if (std::strcmp(prodIntent.commandName.c_str(), "Game.QueueQuadsAllWarFactories") == 0)
							{
								unitTemplate = inferQuadTemplateForProducer(referenceProducer);
							}
							else if (std::strcmp(prodIntent.commandName.c_str(), "Game.QueueScorpionsAllWarFactories") == 0)
							{
								unitTemplate = inferScorpionTemplateForProducer(referenceProducer);
							}
							else if (std::strcmp(prodIntent.commandName.c_str(), "Game.QueueRocketBuggiesAllWarFactories") == 0)
							{
								unitTemplate = inferRocketBuggyTemplateForProducer(referenceProducer);
							}
							else if (std::strcmp(prodIntent.commandName.c_str(), "Game.QueueRpgTroopersAllBarracks") == 0)
							{
								unitTemplate = inferRpgTemplateForProducer(referenceProducer);
							}
							else if (std::strcmp(prodIntent.commandName.c_str(), "Game.QueueSoldiersAllBarracks") == 0)
							{
								unitTemplate = inferSoldierTemplateForPlayer(player, referenceProducer);
							}

							if (!unitTemplate.empty())
							{
								nlohmann::json queuedMessage = {
									{"type", "SessionCommand"},
									{"request_id", "auto_prod_zone"},
									{"cmd", "Game.QueueUnit"},
									{"args", nlohmann::json::object({
										{"producer_kind", prodIntent.producerKind},
										{"unit_template", unitTemplate},
										{"producer_object_id", prodIntent.producerObjectId}
									})}
								};
								if (m_autonomy.state.hasExplicitPlayerIndex)
								{
									queuedMessage["args"]["player_index"] = m_autonomy.state.playerIndex;
								}

								bool issued = executeGameQueueUnit(queuedMessage, resultReason);

								// Fallback to "all" command if zone producer failed
								if (!issued && resultReason != "no_money" && prodIntent.commandName != "Game.QueueUnit")
								{
									const std::string zoneFailureReason = resultReason;
									issued = tryCommand("auto_prod", prodIntent.commandName.c_str(),
										nlohmann::json::object({ {"count", 1} }), resultReason);

									// Phase 4 cleanup: If fallback succeeded, clear stale zone-specific failure reason
									if (issued)
									{
										resultReason = "fallback_ok";
									}
								}
								if (emergencyProductionLog)
								{
									adapterLog(
										"emergency_survival_production command=%s unit=%s producers=1 issued_count=%d reserve_spend=%d army_cap_override=%d batch_limit=%d reason=%s",
										prodIntent.commandName.c_str(),
										unitTemplate.c_str(),
										issued ? 1 : 0,
										emergencyReserveSpend ? 1 : 0,
										emergencyArmyCapOverride ? 1 : 0,
										emergencyBatchLimitForLog,
										issued ? emergencyReason.c_str() : resultReason.c_str());
								}
								return issued;
							}

							resultReason = "no_unit_template";
							return false;
						}
					};
				}
				else
				{
					// Hold state (no production): create no-op intent
					schedulerIntent.executeFunc = [reason = prodIntent.reason](std::string& resultReason) -> bool {
						resultReason = reason;
						return false;
					};
				}

				// Always submit production intent (executable or no-op)
				m_autonomy.scheduler.SubmitIntent(schedulerIntent);
				schedulerHasIntents = true;
			}

			if (techDue)
			{
				const AIControlAdapterProfilePolicyManager profilePolicyManager;
				if (profilePolicyManager.UsesTechRetryPolicy(resolveAutonomyProfilePolicyConfig()))
				{
					const bool hasScienceEconomy = totalSupplyStashes >= 2;
					const bool hasScienceAdvanced = counts.palaces > 0;

					// TechManager wrapper functions
					auto tryScienceCommand = [&](const char* category, const char* command, const char* scienceName, std::string& outReason) -> bool
					{
						nlohmann::json args = nlohmann::json::object({
							{"science_name", std::string(scienceName)}
						});
						return tryCommand(category, command, args, outReason);
					};

					auto tryUpgradeCommand = [&](const char* category, const char* command, const char* producerKind, const char* upgradeName, std::string& outReason) -> bool
					{
						nlohmann::json args = nlohmann::json::object({
							{"producer_kind", std::string(producerKind)},
							{"upgrade_name", std::string(upgradeName)}
						});
						return tryCommand(category, command, args, outReason);
					};

						auto playerHasUpgrade = [&](const char* upgradeName) -> bool
						{
							const UpgradeTemplate* upgradeT = TheUpgradeCenter != nullptr
								? TheUpgradeCenter->findUpgrade(upgradeName)
								: nullptr;
							return upgradeT != nullptr && player != nullptr && player->hasUpgradeComplete(upgradeT);
						};

						if (glaUsaStrategy.camouflageSpendAllowed
							&& money >= 1500u
							&& counts.blackMarkets > 0
							&& !playerHasUpgrade("Upgrade_GLACamoNetting"))
						{
							Intent camouflageIntent;
							camouflageIntent.category = IntentCategory::TECH_UPGRADE;
							camouflageIntent.priority = IntentPriority::HIGH;
							camouflageIntent.commandName = "Game.QueueUpgrade";
							camouflageIntent.targetName = "Upgrade_GLACamoNetting";
							camouflageIntent.reason = glaUsaStrategy.camouflageReason;
							camouflageIntent.executeFunc = [&, tryUpgradeCommand](std::string& resultReason) -> bool {
								const bool issued = tryUpgradeCommand(
									"auto_tech",
									"Game.QueueUpgrade",
									"black_market",
									"Upgrade_GLACamoNetting",
									resultReason);
								adapterLog(
									"camouflage_upgrade command=Game.QueueUpgrade issued=%d reason=%s",
									issued ? 1 : 0,
									issued ? "usa_anchor_multiplier" : resultReason.c_str());
								return issued;
							};
							m_autonomy.scheduler.SubmitIntent(camouflageIntent);
							schedulerHasIntents = true;
						}

						// Submit science intent to scheduler
						if (money >= 1000u)
						{
						Intent scienceIntent;
						scienceIntent.category = IntentCategory::TECH_SCIENCE;
						scienceIntent.priority = IntentPriority::HIGH;
						scienceIntent.commandName = "Game.PurchaseScience";
						scienceIntent.targetName = "science_candidate";
						scienceIntent.reason = "tech_science";

						// Capture science execution in callback
						scienceIntent.executeFunc = [&, tryScienceCommand, hasScienceEconomy, hasScienceAdvanced](std::string& resultReason) -> bool {
							ScienceCandidateResult scienceResult = m_autonomy.techManager.ChooseAndAttemptScience(
								kAutonomySciencePlan,
								static_cast<int>(sizeof(kAutonomySciencePlan) / sizeof(kAutonomySciencePlan[0])),
								hasScienceEconomy,
								hasScienceAdvanced,
								money,
								tryScienceCommand,
								now);

							if (scienceResult.attempted)
							{
								resultReason = scienceResult.resultReason;

								// Enhanced logging: show which candidate was attempted
								adapterLog(
									"autonomy_tech_science candidate=%s result=%s",
									scienceResult.candidateName.c_str(),
									scienceResult.resultReason.c_str());

								Coord3D zoneCenter = {};
								zoneCenter.x = m_autonomy.state.lastZoneCenterX;
								zoneCenter.y = m_autonomy.state.lastZoneCenterY;
								zoneCenter.z = 0.0f;
								recordAutonomyTelemetryEvent(
									"tech",
									"Game.PurchaseScience",
									std::string("ok:") + scienceResult.candidateName,
									m_autonomy.state.hasLastZone ? &zoneCenter : nullptr);

								return scienceResult.resultReason.empty() || scienceResult.resultReason == "ok" || scienceResult.resultReason.find("ok:") == 0;
							}
							else if (!scienceResult.candidateName.empty())
							{
								adapterLog(
									"autonomy_tech_science_failed candidate=%s reason=%s",
									scienceResult.candidateName.c_str(),
									scienceResult.resultReason.c_str());
								resultReason = scienceResult.resultReason;
							}
							else
							{
								resultReason = "no_science_candidate";
							}
							return false;
						};

						m_autonomy.scheduler.SubmitIntent(scienceIntent);
						schedulerHasIntents = true;
					}

					// Submit upgrade intent to scheduler
					if (money >= 1500u)
					{
						Intent upgradeIntent;
						upgradeIntent.category = IntentCategory::TECH_UPGRADE;
						upgradeIntent.priority = IntentPriority::HIGH;
						upgradeIntent.commandName = "Game.QueueUpgrade";
						upgradeIntent.targetName = "upgrade_candidate";
						upgradeIntent.reason = "tech_upgrade";

						// Capture upgrade execution in callback
						upgradeIntent.executeFunc = [&, tryUpgradeCommand, playerHasUpgrade](std::string& resultReason) -> bool {
							std::map<std::string, int> producerCounts;
							producerCounts["black_market"] = counts.blackMarkets;
							producerCounts["palace"] = counts.palaces;
							producerCounts["arms_dealer"] = counts.armsDealers;
							producerCounts["barracks"] = counts.barracks;
							producerCounts["any"] = (counts.barracks > 0 || counts.armsDealers > 0 || counts.palaces > 0) ? 1 : 0;

							UpgradeCandidateResult upgradeResult = m_autonomy.techManager.ChooseAndAttemptUpgrade(
								kAutonomyUpgradePlan,
								static_cast<int>(sizeof(kAutonomyUpgradePlan) / sizeof(kAutonomyUpgradePlan[0])),
								playerHasUpgrade,
								producerCounts,
								money,
								tryUpgradeCommand,
								now);

							if (upgradeResult.attempted)
							{
								resultReason = upgradeResult.resultReason;

								// Enhanced logging: show which upgrade and producer
								adapterLog(
									"autonomy_tech_upgrade candidate=%s producer=%s result=%s",
									upgradeResult.candidateName.c_str(),
									upgradeResult.producerKind.c_str(),
									upgradeResult.resultReason.c_str());

								Coord3D zoneCenter = {};
								zoneCenter.x = m_autonomy.state.lastZoneCenterX;
								zoneCenter.y = m_autonomy.state.lastZoneCenterY;
								zoneCenter.z = 0.0f;
								recordAutonomyTelemetryEvent(
									"tech",
									"Game.QueueUpgrade",
									std::string("ok:") + upgradeResult.candidateName,
									m_autonomy.state.hasLastZone ? &zoneCenter : nullptr);

								return upgradeResult.resultReason.empty() || upgradeResult.resultReason == "ok" || upgradeResult.resultReason.find("ok:") == 0;
							}
							else if (!upgradeResult.candidateName.empty())
							{
								adapterLog(
									"autonomy_tech_upgrade_failed candidate=%s producer=%s reason=%s",
									upgradeResult.candidateName.c_str(),
									upgradeResult.producerKind.c_str(),
									upgradeResult.resultReason.c_str());
								resultReason = upgradeResult.resultReason;
							}
							else
							{
								resultReason = "no_upgrade_candidate";
							}
							return false;
						};

						m_autonomy.scheduler.SubmitIntent(upgradeIntent);
						schedulerHasIntents = true;
					}
				}
			}

			// Process all scheduler intents (production + tech)
			bool productionIssued = false;
			bool techIssued = false;
			std::string productionReason;
			std::string techReason;

			if (schedulerHasIntents)
			{
				std::vector<IntentResult> results = m_autonomy.scheduler.ProcessIntents(now);

				// Log and update state from results
				for (const IntentResult& result : results)
				{
					// Log intent result for telemetry
					adapterLog(
						"autonomy_scheduler_result category=%s priority=%s command=%s target=%s "
						"state=%s attempted=%d issued=%d reason=%s tick=%u",
						IntentCategoryToString(result.category),
						IntentPriorityToString(result.priority),
						result.commandName.c_str(),
						result.targetName.c_str(),
						result.state == IntentState::PENDING ? "pending" :
							result.state == IntentState::ATTEMPTED ? "attempted" : "skipped",
						result.attempted ? 1 : 0,
						result.issued ? 1 : 0,
						result.resultReason.c_str(),
						result.tick);

					// Update decision state based on scheduler results
					if (result.attempted)
					{
						if (result.category == IntentCategory::PRODUCTION)
						{
							m_autonomy.state.lastDecisionCategory = "production";
							m_autonomy.state.lastDecisionCommand = result.issued ? result.commandName : "none";
							m_autonomy.state.lastDecisionReason = result.resultReason;
						}
						else if (result.category == IntentCategory::TECH_SCIENCE && result.issued)
						{
							m_autonomy.state.lastDecisionCategory = "tech";
							m_autonomy.state.lastDecisionCommand = result.commandName;
							m_autonomy.state.lastDecisionReason = std::string("ok:") + result.targetName;
						}
						else if (result.category == IntentCategory::TECH_UPGRADE && result.issued)
						{
							m_autonomy.state.lastDecisionCategory = "tech";
							m_autonomy.state.lastDecisionCommand = result.commandName;
							m_autonomy.state.lastDecisionReason = std::string("ok:") + result.targetName;
						}
					}
				}

				// Extract result data for timer advancement
				for (const IntentResult& result : results)
				{
					if (result.category == IntentCategory::PRODUCTION && result.attempted)
					{
						productionIssued = result.issued;
						productionReason = result.resultReason;

						// Update production telemetry (for all production decisions, including holds)
						const AIControlAdapterProfilePolicyConfig profilePolicyConfig = resolveAutonomyProfilePolicyConfig();
						const bool isBalancedSprawl = profilePolicyConfig.isBalancedSprawl;
						const Int armyCap = AIControlAdapterGetEffectiveArmyCap({
							isBalancedSprawl,
							money,
							static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute)),
							counts.barracks,
							counts.armsDealers,
							counts.blackMarkets,
							AIControlAdapterProfilePolicyManager().ResolveCombatArmyCapBase(profilePolicyConfig)
						});

						const std::string actionName = result.commandName.empty() ? "none" : result.commandName;

						adapterLog(
							"autonomy_tick category=production profile=%s money=%lu action=%s issued=%d reason=%s "
							"income_per_min=%d army_cap=%d "
							"units[soldiers=%d rpg=%d quads=%d scorpions=%d scuds=%d radar=%d] "
							"queued[entries=%d producers=%d quads=%d scorpions=%d scuds=%d]",
							profile.c_str(),
							static_cast<unsigned long>(money),
							actionName.c_str(),
							result.issued ? 1 : 0,
							result.resultReason.c_str(),
							static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute)),
							armyCap,
							counts.soldiers,
							counts.rpg,
							counts.quads,
							counts.scorpions,
							counts.scudLaunchers,
							counts.radarVans,
							counts.queuedProductionEntries,
							counts.warFactoryLikeProducers,
							counts.queuedQuads,
							counts.queuedScorpions,
							counts.queuedScudLaunchers);
					}
					else if ((result.category == IntentCategory::TECH_SCIENCE ||
							  result.category == IntentCategory::TECH_UPGRADE) && result.attempted)
					{
						techIssued = result.issued;
						techReason = result.resultReason;
					}
				}
			}

			// Update next production tick (always advance when productionDue)
			if (productionDue)
			{
				m_autonomy.state.nextProductionTick = now + AIControlAdapterGetProductionRetryDelayMs(productionIssued, productionReason.c_str());
			}

			// Update next tech tick (always advance when techDue)
			if (techDue)
			{
				const AIControlAdapterProfilePolicyManager profilePolicyManager;
				if (profilePolicyManager.UsesTechRetryPolicy(resolveAutonomyProfilePolicyConfig()))
				{
					m_autonomy.state.nextTechTick = now + AIControlAdapterGetTechRetryDelayMs(techIssued, techReason.c_str());
				}
				else
				{
					m_autonomy.state.nextTechTick = now + 5000u;
				}
			}

			if (AIControlAdapterHasTickElapsed(m_autonomy.state.nextGuardTick, now))
			{
				const Int combatCount = counts.soldiers + counts.rpg + counts.quads + counts.scorpions;
				const AIControlAdapterProfilePolicyManager profilePolicyManager;
				const DWORD cadenceMs = profilePolicyManager.ResolveGuardCadenceMs(resolveAutonomyProfilePolicyConfig());
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
			const std::string mode = AIControlAdapterUiUtils::NormalizeAsciiLower(modeIt->get<std::string>());
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
				const std::string profile = AIControlAdapterUiUtils::NormalizeAsciiLower(profileIt->get<std::string>());
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

			const auto urgentZoneGapIt = argsIt->find("urgent_zone_gap_threshold");
			if (urgentZoneGapIt != argsIt->end())
			{
				if (!urgentZoneGapIt->is_number_integer())
				{
					reason = "invalid_urgent_zone_gap_threshold";
					return false;
				}
				m_autonomy.state.hasUrgentZoneGapThresholdOverride = true;
				m_autonomy.state.urgentZoneGapThresholdOverride = std::max<Int>(1, urgentZoneGapIt->get<Int>());
			}

			const auto maxExpansionStashesIt = argsIt->find("max_concurrent_expansion_stashes");
			if (maxExpansionStashesIt != argsIt->end())
			{
				if (!maxExpansionStashesIt->is_number_integer())
				{
					reason = "invalid_max_concurrent_expansion_stashes";
					return false;
				}
				m_autonomy.state.hasMaxConcurrentExpansionStashesOverride = true;
				m_autonomy.state.maxConcurrentExpansionStashesOverride = std::max<Int>(1, maxExpansionStashesIt->get<Int>());
			}

			const auto allowSeededFollowupIt = argsIt->find("allow_expansion_before_full_remote_followup");
			if (allowSeededFollowupIt != argsIt->end())
			{
				if (!allowSeededFollowupIt->is_boolean())
				{
					reason = "invalid_allow_expansion_before_full_remote_followup";
					return false;
				}
				m_autonomy.state.hasAllowExpansionBeforeFullRemoteFollowupOverride = true;
				m_autonomy.state.allowExpansionBeforeFullRemoteFollowupOverride = allowSeededFollowupIt->get<bool>();
			}

			const auto expansionCashFloatIt = argsIt->find("expansion_high_cash_float_threshold");
			if (expansionCashFloatIt != argsIt->end())
			{
				if (!expansionCashFloatIt->is_number_integer() || expansionCashFloatIt->get<Int>() < 0)
				{
					reason = "invalid_expansion_high_cash_float_threshold";
					return false;
				}
				m_autonomy.state.hasExpansionHighCashFloatThresholdOverride = true;
				m_autonomy.state.expansionHighCashFloatThresholdOverride = static_cast<UnsignedInt>(expansionCashFloatIt->get<Int>());
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

			const auto debugDrawIt = argsIt->find("debug_draw");
			if (debugDrawIt != argsIt->end())
			{
				if (!debugDrawIt->is_boolean())
				{
					reason = "invalid_debug_draw";
					return false;
				}
				m_autonomy.state.debugDrawEnabled = debugDrawIt->get<bool>();
				adapterLog("config_autonomy debug_draw=%d", m_autonomy.state.debugDrawEnabled ? 1 : 0);
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

		nlohmann::json buildMatchOutcome(Player* localPlayer, DWORD now)
		{
			if (m_autonomy.state.matchOutcomeStartTick == 0u)
			{
				m_autonomy.state.matchOutcomeStartTick = now;
			}

			const GameSlot* localSlot = findSlotForPlayer(localPlayer);
			const Int localTeam = localSlot != nullptr ? localSlot->getTeamNumber() : -1;
			const Int localPlayerIndex = localPlayer != nullptr ? localPlayer->getPlayerIndex() : -1;
			const bool victoryConditionsAvailable = TheVictoryConditions != nullptr && localPlayer != nullptr;
			const bool alliedVictory = victoryConditionsAvailable ? TheVictoryConditions->isLocalAlliedVictory() : false;
			const bool alliedDefeat = victoryConditionsAvailable ? TheVictoryConditions->isLocalAlliedDefeat() : false;
			const bool localDefeat = victoryConditionsAvailable ? TheVictoryConditions->isLocalDefeat() : false;
			const UnsignedInt endFrame = victoryConditionsAvailable ? TheVictoryConditions->getEndFrame() : 0u;
			const AIControlAdapterMatchOutcomePolicyResult policy = AIControlAdapterClassifyMatchOutcome({
				victoryConditionsAvailable,
				alliedVictory,
				alliedDefeat,
				localDefeat,
				endFrame
			});

			Int winnerTeam = -1;
			nlohmann::json players = nlohmann::json::array();
			int enemyPlayerCount = 0;
			std::map<std::string, int> enemyDifficultyCounts;
			int includedParticipants = 0;
			int includedLocalParticipants = 0;
			int localUnits = 0;
			int localBuildings = 0;
			int localScudStorms = 0;
			int localScudStormsInProgress = 0;
			int localScudStormsReady = 0;
			int localBlackMarkets = 0;
			int localWorkers = 0;
			int localBarracks = 0;
			int localArmsDealers = 0;
			int localPalaces = 0;
			int localCommandCenters = 0;
			int localSupplyStashes = 0;
			auto slotStateName = [](SlotState state) -> const char*
			{
				switch (state)
				{
					case SLOT_EASY_AI: return "easy";
					case SLOT_MED_AI: return "medium";
					case SLOT_BRUTAL_AI: return "brutal";
					case SLOT_PLAYER: return "human";
					case SLOT_OPEN: return "open";
					case SLOT_CLOSED: return "closed";
					default: return "unknown";
				}
			};
			struct MatchAssetCounts
			{
				int units = 0;
				int buildings = 0;
				int workers = 0;
				int scudStorms = 0;
				int scudStormsInProgress = 0;
				int scudStormsReady = 0;
				int blackMarkets = 0;
				int barracks = 0;
				int armsDealers = 0;
				int palaces = 0;
				int commandCenters = 0;
				int supplyStashes = 0;
			};
			auto countPlayerAssets = [](Player* player, MatchAssetCounts& counts) -> int
			{
				counts = MatchAssetCounts();
				if (player == nullptr)
				{
					return 0;
				}
				player->iterateObjects([](Object* obj, void* userData)
				{
					if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
					{
						return;
					}
					MatchAssetCounts* p = static_cast<MatchAssetCounts*>(userData);
					const bool underConstruction = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
					const ThingTemplate* tt = obj->getTemplate();
					const std::string name = tt != nullptr ? tt->getName().str() : "";
					if (obj->isKindOf(KINDOF_STRUCTURE))
					{
						if (!underConstruction)
						{
							++p->buildings;
						}
						if (containsIgnoreCase(name, "scudstorm"))
						{
							if (underConstruction)
							{
								++p->scudStormsInProgress;
							}
							else
							{
								++p->scudStorms;
								++p->scudStormsReady;
							}
						}
						if (!underConstruction && containsIgnoreCase(name, "blackmarket"))
						{
							++p->blackMarkets;
						}
						if (!underConstruction && containsIgnoreCase(name, "barracks")) ++p->barracks;
						if (!underConstruction && containsIgnoreCase(name, "armsdealer")) ++p->armsDealers;
						if (!underConstruction && isPalaceTemplateName(name)) ++p->palaces;
						if (!underConstruction && containsIgnoreCase(name, "commandcenter")) ++p->commandCenters;
						if (!underConstruction && (containsIgnoreCase(name, "supplystash") || containsIgnoreCase(name, "supplycenter"))) ++p->supplyStashes;
					}
					else if (!underConstruction)
					{
						++p->units;
						if (obj->isKindOf(KINDOF_DOZER))
						{
							++p->workers;
						}
					}
				}, &counts);
				return counts.units + counts.buildings;
			};

			if (ThePlayerList != nullptr)
			{
				const Player* neutralPlayer = ThePlayerList->getNeutralPlayer();
				const Int count = ThePlayerList->getPlayerCount();
				for (Int i = 0; i < count; ++i)
				{
					Player* player = ThePlayerList->getNthPlayer(i);
					if (player == nullptr || player == neutralPlayer)
					{
						continue;
					}

					const GameSlot* slot = findSlotForPlayer(player);
					const Int team = slot != nullptr ? slot->getTeamNumber() : -1;
					const bool local = player == localPlayer;
					MatchAssetCounts playerCounts;
					const int playerAssets = countPlayerAssets(player, playerCounts);
					if (local)
					{
						localUnits = playerCounts.units;
						localBuildings = playerCounts.buildings;
						localScudStorms = playerCounts.scudStorms;
						localScudStormsInProgress = playerCounts.scudStormsInProgress;
						localScudStormsReady = playerCounts.scudStormsReady;
						localBlackMarkets = playerCounts.blackMarkets;
						localWorkers = playerCounts.workers;
						localBarracks = playerCounts.barracks;
						localArmsDealers = playerCounts.armsDealers;
						localPalaces = playerCounts.palaces;
						localCommandCenters = playerCounts.commandCenters;
						localSupplyStashes = playerCounts.supplyStashes;
					}
					const bool slotOccupied = slot != nullptr && slot->isOccupied();
					const bool validStartPosition = slot != nullptr && slot->getStartPos() >= 0;
					const bool validTemplate = slot != nullptr && slot->getPlayerTemplate() > PLAYERTEMPLATE_MIN;
					const AIControlAdapterMatchParticipantResult participant =
						AIControlAdapterClassifyMatchParticipant({
							local,
							slot != nullptr,
							slotOccupied,
							validStartPosition,
							validTemplate,
							playerAssets > 0
						});
					const bool victorious = victoryConditionsAvailable ? TheVictoryConditions->hasAchievedVictory(player) : false;
					const bool defeated = victoryConditionsAvailable ? TheVictoryConditions->hasSinglePlayerBeenDefeated(player) : player->isPlayerDead();
					if (participant.includedInOutcome)
					{
						++includedParticipants;
						if (local)
						{
							++includedLocalParticipants;
						}
					}
					if (participant.includedInOutcome && victorious && winnerTeam < 0)
					{
						winnerTeam = team;
					}
					if (participant.includedInOutcome && !local && localTeam >= 0 && team != localTeam)
					{
						++enemyPlayerCount;
						if (slot != nullptr && slot->isAI())
						{
							++enemyDifficultyCounts[slotStateName(slot->getState())];
						}
					}

					players.push_back(nlohmann::json::object({
						{"player_index", player->getPlayerIndex()},
						{"team", team},
						{"local", local},
						{"defeated", defeated},
						{"victorious", victorious},
						{"active_participant", participant.activeParticipant},
						{"included_in_outcome", participant.includedInOutcome},
						{"non_participant_reason", participant.nonParticipantReason}
					}));
				}
			}
			if (winnerTeam < 0 && policy.state == "victory")
			{
				winnerTeam = localTeam;
			}

			const UnsignedInt elapsedMs = now >= m_autonomy.state.matchOutcomeStartTick
				? static_cast<UnsignedInt>(now - m_autonomy.state.matchOutcomeStartTick)
				: 0u;
			int zoneCount = 0;
			int developedZoneCount = 0;
			int frontlineZoneCount = 0;
			int contestedZoneCount = 0;
			int underDefendedZoneCount = 0;
			if (m_autonomy.state.telemetryZones.is_array())
			{
				for (const auto& zone : m_autonomy.state.telemetryZones)
				{
					if (!zone.is_object())
					{
						continue;
					}
					++zoneCount;
					if (zone.value("developed", false))
					{
						++developedZoneCount;
					}
				}
			}
			if (m_autonomy.state.zoneDefenseReserveTelemetry.is_array())
			{
				for (const auto& reserve : m_autonomy.state.zoneDefenseReserveTelemetry)
				{
					if (!reserve.is_object())
					{
						continue;
					}
					const std::string posture = reserve.value("posture", std::string());
					if (posture == "frontline")
					{
						++frontlineZoneCount;
					}
					if (posture == "contested_front")
					{
						++contestedZoneCount;
					}
					if (reserve.value("deficit", 0) > 0)
					{
						++underDefendedZoneCount;
					}
				}
			}
			UnsignedInt money = 0u;
			if (localPlayer != nullptr && localPlayer->getMoney() != nullptr)
			{
				money = localPlayer->getMoney()->countMoney();
			}
			std::string mapName;
			if (TheGameInfo != nullptr)
			{
				mapName = TheGameInfo->getMap().str();
			}
			if (mapName.empty() && TheGlobalData != nullptr)
			{
				mapName = TheGlobalData->m_mapName.str();
			}
			const int producerCount = localBarracks + localArmsDealers + localPalaces + localCommandCenters;
			const int activeBuildTasks = m_autonomy.taskReservationManager.getBuildTaskCount();
			const int activeCaptureTasks = m_autonomy.taskReservationManager.getCaptureTaskCount();
			const int activeSpecialTasks = m_autonomy.taskReservationManager.getActiveTaskCount();
			const int activeCombatTasks = m_autonomy.combatTaskManager.getActiveTaskCount();
			const int activeAttackWaves = m_autonomy.combatTaskManager.getAttackTaskCount();
			const int activeDefenseAllocations = static_cast<int>(m_autonomy.state.zoneDefenseAllocations.size());
			int criticalThreatZones = 0;
			for (const auto& threatPair : m_autonomy.state.zoneThreats)
			{
				if (threatPair.second.level == "critical")
				{
					++criticalThreatZones;
				}
			}
			int knownEnemyPlayers = 0;
			int knownEnemyStructures = 0;
			int knownEnemyProducers = 0;
			int knownEnemyWmd = 0;
			int staleWmdTargets = 0;
			std::set<int> enemyPlayersSeen;
			for (const EnemyMemoryItem& item : m_autonomy.enemyMemory.getItems())
			{
				enemyPlayersSeen.insert(item.playerIndex);
				if (item.kind == EnemyMemoryKind::Production) ++knownEnemyProducers;
				if (item.kind == EnemyMemoryKind::Wmd)
				{
					++knownEnemyWmd;
					if (!item.visible) ++staleWmdTargets;
				}
				if (item.isStructure || item.kind != EnemyMemoryKind::Army)
				{
					++knownEnemyStructures;
				}
			}
			knownEnemyPlayers = static_cast<int>(enemyPlayersSeen.size());
			std::string reserveState = "unknown";
			std::string spendingMode = "unknown";
			if (m_autonomy.state.emergencySurvivalTelemetry.is_object())
			{
				reserveState = m_autonomy.state.emergencySurvivalTelemetry.value("reserve_state", reserveState);
				spendingMode = m_autonomy.state.emergencySurvivalTelemetry.value("spending_mode", spendingMode);
			}
			if (reserveState == "unknown")
			{
				reserveState = money < 3000u ? "depleted" : "protected";
			}
			std::vector<SpecialTaskReservation*> activeBuildReservations = m_autonomy.taskReservationManager.findBuildTasks();
			int stalledConstructionTasks = 0;
			for (const SpecialTaskReservation* task : activeBuildReservations)
			{
				if (task != nullptr && now >= task->createdTick && (now - task->createdTick) > 120000u)
				{
					++stalledConstructionTasks;
				}
			}
			const std::vector<std::string> diagnosisHints = AIControlAdapterChooseRunDiagnosisHints({
				reserveState,
				localCommandCenters,
				localWorkers,
				producerCount,
				underDefendedZoneCount,
				stalledConstructionTasks,
				knownEnemyWmd,
				activeAttackWaves
			});
			nlohmann::json diagnosisHintsJson = nlohmann::json::array();
			std::string diagnosisHintsCsv;
			for (std::size_t hintIdx = 0; hintIdx < diagnosisHints.size(); ++hintIdx)
			{
				diagnosisHintsJson.push_back(diagnosisHints[hintIdx]);
				if (!diagnosisHintsCsv.empty())
				{
					diagnosisHintsCsv += ",";
				}
				diagnosisHintsCsv += diagnosisHints[hintIdx];
			}
			std::vector<Object*> availableCombatForSnapshot;
			collectCombatUnitsForRaid(localPlayer, availableCombatForSnapshot);
			const int availableIdleCombat = static_cast<int>(availableCombatForSnapshot.size());
			const std::string currentIdentity = mapName + "|" + std::to_string(localPlayerIndex) + "|" + std::to_string(localTeam);
			const bool meaningfulContext =
				includedLocalParticipants > 0 &&
				includedParticipants >= 2 &&
				(localPlayerIndex >= 0) &&
				((localUnits + localBuildings) > 0 || zoneCount > 0 || !players.empty());

			const bool hasDurableFinishedOutcome =
				m_autonomy.state.lastTerminalMatchOutcome.is_object() ||
				m_autonomy.state.durableUnknownMatchOutcome.is_object() ||
				m_autonomy.state.finalMatchOutcome.is_object() ||
				m_autonomy.state.finalDiagnosticSnapshot.is_object();
			const bool newMatchDetected = meaningfulContext
				&& policy.state == "running"
				&& ((!m_autonomy.state.activeMatchIdentity.empty() && currentIdentity != m_autonomy.state.activeMatchIdentity)
					|| hasDurableFinishedOutcome);
			const AIControlAdapterDurableMatchOutcomeDecision initialDurableDecision =
				AIControlAdapterChooseDurableMatchOutcomeAction({
					newMatchDetected,
					meaningfulContext,
					m_autonomy.state.lastTerminalMatchOutcome.is_object(),
					m_autonomy.state.durableUnknownMatchOutcome.is_object(),
					m_autonomy.state.lastActiveMatchSnapshot.is_object(),
					policy.state
				});
			if (std::strcmp(initialDurableDecision.action, "reset_for_new_match") == 0)
			{
				m_autonomy.state.lastActiveMatchOutcome = nlohmann::json();
				m_autonomy.state.lastTerminalMatchOutcome = nlohmann::json();
				m_autonomy.state.durableUnknownMatchOutcome = nlohmann::json();
				m_autonomy.state.lastActiveMatchSnapshot = nlohmann::json();
				m_autonomy.state.finalDiagnosticSnapshot = nlohmann::json();
				m_autonomy.state.finalRunSummary = nlohmann::json();
				m_autonomy.state.finalMatchOutcome = nlohmann::json();
				m_autonomy.state.loggedTerminalMatchOutcomeState.clear();
				m_autonomy.state.loggedFinalSnapshotIdentity.clear();
				m_autonomy.state.matchOutcomeStartTick = now;
			}
			if (meaningfulContext)
			{
				m_autonomy.state.activeMatchIdentity = currentIdentity;
			}
			if (m_autonomy.state.finalMatchOutcome.is_object())
			{
				return m_autonomy.state.finalMatchOutcome;
			}

			nlohmann::json outcome = nlohmann::json::object({
				{"state", policy.state},
				{"local_player_index", localPlayerIndex},
				{"local_team", localTeam},
				{"allied_victory", alliedVictory},
				{"allied_defeat", alliedDefeat},
				{"local_defeat", localDefeat},
				{"end_frame", endFrame},
				{"elapsed_ms", elapsedMs},
				{"winner_team", winnerTeam >= 0 ? nlohmann::json(winnerTeam) : nlohmann::json(nullptr)},
				{"reason", policy.reason},
				{"players", players}
			});

			nlohmann::json activeSnapshot = nlohmann::json::object({
				{"tick", static_cast<UnsignedInt>(now)},
				{"frame", endFrame},
				{"map", mapName},
				{"local_player_index", localPlayerIndex},
				{"local_team", localTeam},
				{"active", meaningfulContext},
				{"elapsed_ms", elapsedMs},
				{"end_frame", endFrame},
				{"economy", nlohmann::json::object({
					{"money", money},
					{"income_rate", static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute))},
					{"reserve_state", reserveState},
					{"spending_mode", spendingMode},
					{"markets_completed", localBlackMarkets},
					{"markets_in_progress", 0},
					{"supply_stashes", localSupplyStashes},
					{"workers", localWorkers},
					{"idle_workers", localWorkers}
				})},
				{"assets", nlohmann::json::object({
					{"units", localUnits},
					{"buildings", localBuildings},
					{"workers", localWorkers},
					{"producers", nlohmann::json::object({
						{"barracks", localBarracks},
						{"arms_dealers", localArmsDealers},
						{"palaces", localPalaces},
						{"command_centers", localCommandCenters}
					})},
					{"scud_storms", nlohmann::json::object({
						{"live", localScudStorms},
						{"in_progress", localScudStormsInProgress},
						{"ready", localScudStormsReady},
						{"desired", 10}
					})}
				})},
				{"zones", nlohmann::json::object({
					{"total", zoneCount},
					{"developed", developedZoneCount},
					{"frontline", frontlineZoneCount},
					{"contested", contestedZoneCount},
					{"under_defended", underDefendedZoneCount}
				})},
				{"defense", nlohmann::json::object({
					{"active_allocations", activeDefenseAllocations},
					{"available_idle", availableIdleCombat},
					{"reserve_deficits", underDefendedZoneCount},
					{"critical_zones", criticalThreatZones}
				})},
				{"tasks", nlohmann::json::object({
					{"active_total", activeSpecialTasks + activeCombatTasks},
					{"active_build", activeBuildTasks},
					{"active_capture", activeCaptureTasks},
					{"stalled_construction", stalledConstructionTasks}
				})},
				{"combat", nlohmann::json::object({
					{"attack_waves_active", activeAttackWaves},
					{"raid_tasks_active", activeAttackWaves},
					{"known_artillery_threats", m_autonomy.state.counterbatteryTelemetry.is_object()
						? static_cast<int>(m_autonomy.state.counterbatteryTelemetry.value("artillery_threats", nlohmann::json::array()).size())
						: 0}
				})},
				{"enemy_memory", nlohmann::json::object({
					{"known_enemy_players", knownEnemyPlayers},
					{"known_structures", knownEnemyStructures},
					{"known_producers", knownEnemyProducers},
					{"known_wmd", knownEnemyWmd},
					{"stale_wmd_targets", staleWmdTargets}
				})},
				{"diagnosis_hints", diagnosisHintsJson},
				{"players", players}
			});
			activeSnapshot["legacy"] = nlohmann::json::object({
				{"zones", zoneCount},
				{"developed_zones", developedZoneCount},
				{"units", localUnits},
				{"buildings", localBuildings},
				{"money", money}
			});

			auto buildRunSummary = [&]() -> nlohmann::json
			{
				std::string enemyDifficulty = "unknown";
				if (!enemyDifficultyCounts.empty())
				{
					enemyDifficulty = enemyDifficultyCounts.size() == 1u
						? enemyDifficultyCounts.begin()->first
						: "mixed";
				}

				return nlohmann::json::object({
					{"outcome_state", policy.state},
					{"outcome_reason", policy.reason},
					{"map", mapName},
					{"local_player_index", localPlayerIndex},
					{"local_team", localTeam},
					{"enemy_player_count", enemyPlayerCount},
					{"active_enemy_participant_count", enemyPlayerCount},
					{"enemy_difficulty", enemyDifficulty},
					{"elapsed_ms", elapsedMs},
					{"end_frame", endFrame},
					{"final_money", money},
					{"final_income_rate", static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute))},
					{"final_reserve_state", reserveState},
					{"final_zone_count", zoneCount},
					{"final_developed_zone_count", developedZoneCount},
					{"final_contested_zone_count", contestedZoneCount},
					{"final_unit_count", localUnits},
					{"final_building_count", localBuildings},
					{"final_worker_count", localWorkers},
					{"final_producer_count", producerCount},
					{"final_scud_storm_count", localScudStorms},
					{"final_scud_storm_ready_count", localScudStormsReady},
					{"final_black_market_count", localBlackMarkets}
					,{"final_known_enemy_structure_count", knownEnemyStructures}
					,{"final_known_enemy_wmd_count", knownEnemyWmd}
					,{"final_active_task_count", activeSpecialTasks + activeCombatTasks}
					,{"final_defense_reserve_deficit_count", underDefendedZoneCount}
					,{"final_active_attack_wave_count", activeAttackWaves}
					,{"diagnosis_hints", diagnosisHintsJson}
				});
			};
			auto freezeFinalDiagnostics = [&](const nlohmann::json& finalOutcome, const nlohmann::json& snapshot, const char* source) -> void
			{
				if (m_autonomy.state.finalDiagnosticSnapshot.is_object())
				{
					return;
				}
				m_autonomy.state.finalMatchOutcome = finalOutcome;
				m_autonomy.state.finalDiagnosticSnapshot = snapshot;
				m_autonomy.state.finalDiagnosticSnapshot["final_snapshot_source"] = source != nullptr ? source : "terminal_outcome";
				m_autonomy.state.finalRunSummary = finalOutcome.contains("run_summary")
					? finalOutcome["run_summary"]
					: buildRunSummary();
				m_autonomy.state.finalRunSummary["final_snapshot_source"] = source != nullptr ? source : "terminal_outcome";
				const std::string finalState = finalOutcome.value("state", std::string("unknown"));
				const std::string finalReason = finalOutcome.value("reason", std::string("unknown"));
				const std::string logIdentity = currentIdentity + "|" + finalState + "|" + finalReason;
				if (m_autonomy.state.loggedFinalSnapshotIdentity != logIdentity)
				{
					adapterLog(
						"run_final_snapshot outcome=%s reason=%s tick=%u elapsed_ms=%u map=\"%s\" money=%u zones=%d units=%d buildings=%d workers=%d markets=%d producers=%d scud_storms=%d/%d known_enemy_structures=%d defense_deficits=%d active_tasks=%d hints=%s",
						finalState.c_str(),
						finalReason.c_str(),
						static_cast<unsigned int>(now),
						static_cast<unsigned int>(elapsedMs),
						mapName.c_str(),
						static_cast<unsigned int>(money),
						zoneCount,
						localUnits,
						localBuildings,
						localWorkers,
						localBlackMarkets,
						producerCount,
						localScudStorms,
						10,
						knownEnemyStructures,
						underDefendedZoneCount,
						activeSpecialTasks + activeCombatTasks,
						diagnosisHintsCsv.empty() ? "none" : diagnosisHintsCsv.c_str());
					m_autonomy.state.loggedFinalSnapshotIdentity = logIdentity;
				}
			};

			if (policy.state != "running" && policy.state != "unknown")
			{
				outcome["run_summary"] = buildRunSummary();
			}

			if (AIControlAdapterShouldLogTerminalMatchOutcome(
				m_autonomy.state.loggedTerminalMatchOutcomeState,
				policy.state))
			{
				adapterLog(
					"match_outcome state=%s local_player=%d team=%d end_frame=%u elapsed_ms=%u reason=%s",
					policy.state.c_str(),
					localPlayerIndex,
					localTeam,
					static_cast<unsigned int>(endFrame),
					static_cast<unsigned int>(elapsedMs),
					policy.reason);
				m_autonomy.state.loggedTerminalMatchOutcomeState = policy.state;
			}

			const AIControlAdapterDurableMatchOutcomeDecision durableDecision =
				AIControlAdapterChooseDurableMatchOutcomeAction({
					false,
					meaningfulContext,
					m_autonomy.state.lastTerminalMatchOutcome.is_object(),
					m_autonomy.state.durableUnknownMatchOutcome.is_object(),
					m_autonomy.state.lastActiveMatchSnapshot.is_object(),
					policy.state
				});
			if (std::strcmp(durableDecision.action, "persist_current_terminal") == 0)
			{
				m_autonomy.state.lastTerminalMatchOutcome = outcome;
				freezeFinalDiagnostics(m_autonomy.state.lastTerminalMatchOutcome, activeSnapshot, "terminal_outcome");
				return m_autonomy.state.lastTerminalMatchOutcome;
			}
			if (std::strcmp(durableDecision.action, "cache_current_active") == 0)
			{
				m_autonomy.state.lastActiveMatchOutcome = outcome;
				m_autonomy.state.lastActiveMatchSnapshot = activeSnapshot;
				return outcome;
			}
			if (std::strcmp(durableDecision.action, "return_cached_terminal") == 0)
			{
				return m_autonomy.state.lastTerminalMatchOutcome;
			}
			if (std::strcmp(durableDecision.action, "return_cached_unknown") == 0)
			{
				return m_autonomy.state.durableUnknownMatchOutcome;
			}
			if (std::strcmp(durableDecision.action, "create_lost_context_unknown") == 0)
			{
				nlohmann::json unknown = nlohmann::json::object({
					{"state", "unknown"},
					{"local_player_index", m_autonomy.state.lastActiveMatchSnapshot.value("local_player_index", localPlayerIndex)},
					{"local_team", m_autonomy.state.lastActiveMatchSnapshot.value("local_team", localTeam)},
					{"allied_victory", false},
					{"allied_defeat", false},
					{"local_defeat", false},
					{"end_frame", m_autonomy.state.lastActiveMatchSnapshot.value("end_frame", 0u)},
					{"elapsed_ms", m_autonomy.state.lastActiveMatchSnapshot.value("elapsed_ms", elapsedMs)},
					{"winner_team", nullptr},
					{"reason", "lost_match_context_before_terminal_outcome"},
					{"players", m_autonomy.state.lastActiveMatchSnapshot.value("players", nlohmann::json::array())},
					{"last_active", m_autonomy.state.lastActiveMatchSnapshot}
				});
				unknown["run_summary"] = buildRunSummary();
				unknown["run_summary"]["outcome_state"] = "unknown";
				unknown["run_summary"]["outcome_reason"] = "lost_match_context_before_terminal_outcome";
				if (m_autonomy.state.lastActiveMatchSnapshot.is_object())
				{
					const nlohmann::json& snapshot = m_autonomy.state.lastActiveMatchSnapshot;
					unknown["run_summary"]["map"] = snapshot.value("map", mapName);
					unknown["run_summary"]["local_player_index"] = snapshot.value("local_player_index", localPlayerIndex);
					unknown["run_summary"]["local_team"] = snapshot.value("local_team", localTeam);
					unknown["run_summary"]["elapsed_ms"] = snapshot.value("elapsed_ms", elapsedMs);
					if (snapshot.contains("economy") && snapshot["economy"].is_object())
					{
						unknown["run_summary"]["final_money"] = snapshot["economy"].value("money", money);
						unknown["run_summary"]["final_income_rate"] = snapshot["economy"].value("income_rate", static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute)));
						unknown["run_summary"]["final_reserve_state"] = snapshot["economy"].value("reserve_state", reserveState);
					}
					if (snapshot.contains("assets") && snapshot["assets"].is_object())
					{
						unknown["run_summary"]["final_unit_count"] = snapshot["assets"].value("units", localUnits);
						unknown["run_summary"]["final_building_count"] = snapshot["assets"].value("buildings", localBuildings);
						unknown["run_summary"]["final_worker_count"] = snapshot["assets"].value("workers", localWorkers);
						if (snapshot["assets"].contains("scud_storms") && snapshot["assets"]["scud_storms"].is_object())
						{
							unknown["run_summary"]["final_scud_storm_count"] = snapshot["assets"]["scud_storms"].value("live", localScudStorms);
							unknown["run_summary"]["final_scud_storm_ready_count"] = snapshot["assets"]["scud_storms"].value("ready", localScudStormsReady);
						}
					}
					if (snapshot.contains("zones") && snapshot["zones"].is_object())
					{
						unknown["run_summary"]["final_zone_count"] = snapshot["zones"].value("total", zoneCount);
						unknown["run_summary"]["final_developed_zone_count"] = snapshot["zones"].value("developed", developedZoneCount);
						unknown["run_summary"]["final_contested_zone_count"] = snapshot["zones"].value("contested", contestedZoneCount);
						unknown["run_summary"]["final_defense_reserve_deficit_count"] = snapshot["zones"].value("under_defended", underDefendedZoneCount);
					}
					if (snapshot.contains("enemy_memory") && snapshot["enemy_memory"].is_object())
					{
						unknown["run_summary"]["final_known_enemy_structure_count"] = snapshot["enemy_memory"].value("known_structures", knownEnemyStructures);
						unknown["run_summary"]["final_known_enemy_wmd_count"] = snapshot["enemy_memory"].value("known_wmd", knownEnemyWmd);
					}
				}
				m_autonomy.state.durableUnknownMatchOutcome = unknown;
				freezeFinalDiagnostics(
					m_autonomy.state.durableUnknownMatchOutcome,
					m_autonomy.state.lastActiveMatchSnapshot.is_object() ? m_autonomy.state.lastActiveMatchSnapshot : activeSnapshot,
					"last_active_before_context_loss");
				if (m_autonomy.state.loggedTerminalMatchOutcomeState != "unknown_lost_match_context")
				{
					adapterLog(
						"match_outcome state=unknown local_player=%d team=%d elapsed_ms=%u reason=lost_match_context_before_terminal_outcome",
						unknown.value("local_player_index", -1),
						unknown.value("local_team", -1),
						unknown.value("elapsed_ms", 0u));
					m_autonomy.state.loggedTerminalMatchOutcomeState = "unknown_lost_match_context";
				}
				return m_autonomy.state.durableUnknownMatchOutcome;
			}

			return outcome;
		}

		nlohmann::json buildAutonomyStatus()
		{
			nlohmann::json result = nlohmann::json::object();
			const DWORD statusNow = ::GetTickCount();
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
			result["debug_draw"] = m_autonomy.state.debugDrawEnabled;
			result["target_player_index"] = m_autonomy.state.hasExplicitTargetPlayerIndex ? m_autonomy.state.targetPlayerIndex : -1;
			const AIControlAdapterProfilePolicyConfig policyConfig = resolveAutonomyProfilePolicyConfig();
			const AIControlAdapterProfilePolicyManager profilePolicyManager;
			result["profile_policy"] = nlohmann::json::object({
				{"profile", policyConfig.profile},
				{"is_aggressive", policyConfig.isAggressive},
				{"is_economic", policyConfig.isEconomic},
				{"is_defensive", policyConfig.isDefensive},
				{"is_tech", policyConfig.isTech},
				{"is_balanced_sprawl", policyConfig.isBalancedSprawl},
				{"is_sprawl_style", policyConfig.isSprawlStyle},
				{"reserve_cash", policyConfig.reserveCash},
				{"reserve_cash_with_garrison_floor", profilePolicyManager.ResolveReserveCashWithFloor(policyConfig, 5000u)},
				{"guard_cadence_ms", profilePolicyManager.ResolveGuardCadenceMs(policyConfig)},
				{"combat_army_cap_base", profilePolicyManager.ResolveCombatArmyCapBase(policyConfig)},
				{"uses_tech_retry_policy", profilePolicyManager.UsesTechRetryPolicy(policyConfig)},
				{"worker_min_idle", policyConfig.workerMinIdle},
				{"worker_queue_count", policyConfig.workerQueueCount},
				{"worker_cooldown_ms", policyConfig.workerCooldownMs},
				{"stash_workers_per_stash", policyConfig.stashWorkersPerStash},
				{"stash_worker_cooldown_ms", policyConfig.stashWorkerCooldownMs},
				{"attack_min_units", policyConfig.attackMinUnits},
				{"attack_group_size", policyConfig.attackGroupSize},
				{"attack_cooldown_ms", policyConfig.attackCooldownMs},
				{"desired_supply_zones", policyConfig.sprawlSupplyCap},
				{"max_barracks", policyConfig.sprawlBarracksCap},
				{"max_arms_dealers", policyConfig.sprawlArmsCap},
				{"max_black_markets", policyConfig.sprawlMarketCap},
				{"max_tunnels", policyConfig.sprawlTunnelCap},
				{"max_stingers", policyConfig.sprawlStingerCap},
				{"urgent_zone_gap_threshold", policyConfig.urgentZoneGapThreshold},
				{"normal_max_concurrent_expansion_stashes", policyConfig.normalMaxConcurrentExpansionStashes},
				{"allow_expansion_before_full_remote_followup", policyConfig.allowExpansionBeforeFullRemoteFollowup},
				{"expansion_high_cash_float_threshold", policyConfig.expansionHighCashFloatThreshold},
				{"scud_storm_high_cash_float_threshold", policyConfig.scudStormHighCashFloatThreshold},
				{"overrides", nlohmann::json::object({
					{"urgent_zone_gap_threshold", m_autonomy.state.hasUrgentZoneGapThresholdOverride},
					{"max_concurrent_expansion_stashes", m_autonomy.state.hasMaxConcurrentExpansionStashesOverride},
					{"allow_expansion_before_full_remote_followup", m_autonomy.state.hasAllowExpansionBeforeFullRemoteFollowupOverride},
					{"expansion_high_cash_float_threshold", m_autonomy.state.hasExpansionHighCashFloatThresholdOverride}
				})}
			});
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
				std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
				collectOwnedAutomationObjects(player, ownedObjects);
				for (std::size_t i = 0; i < ownedObjects.size(); ++i)
				{
					const AutomationOwnedObjectSnapshot& owned = ownedObjects[i];
					if (owned.isStructure)
					{
						if (owned.underConstruction)
						{
							continue;
						}
						++counts.buildings;
						if (owned.isSupplyStructure)
						{
							++counts.supplyStashes;
						}
						if (owned.isBarracks)
						{
							++counts.barracks;
						}
						if (owned.isArmsDealer)
						{
							++counts.armsDealers;
						}
						if (owned.isPalace)
						{
							++counts.palaces;
						}
						if (owned.isBlackMarket)
						{
							++counts.blackMarkets;
						}
						if (owned.isScudStorm)
						{
							++counts.scudStorms;
						}
						continue;
					}

					++counts.units;
					if (owned.isDozer)
					{
						++counts.workers;
					}
					if (owned.isRadarVan)
					{
						++counts.radarVans;
					}
				}

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
			result["match_outcome"] = buildMatchOutcome(player, statusNow);

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
			result["final_snapshot_available"] = m_autonomy.state.finalDiagnosticSnapshot.is_object();
			result["final_snapshot_tick"] = m_autonomy.state.finalDiagnosticSnapshot.is_object()
				? m_autonomy.state.finalDiagnosticSnapshot.value("tick", 0u)
				: 0u;

			return result;
		}

		nlohmann::json buildAutonomyTelemetry()
		{
			nlohmann::json result = buildAutonomyStatus();
			const DWORD telemetryNow = ::GetTickCount();
			result["telemetry_tick"] = static_cast<UnsignedInt>(telemetryNow);
			result["zones"] = m_autonomy.state.telemetryZones.is_array() ? m_autonomy.state.telemetryZones : nlohmann::json::array();
			result["recent_events"] = m_autonomy.state.telemetryEvents.is_array() ? m_autonomy.state.telemetryEvents : nlohmann::json::array();
			result["enemy_memory"] = m_autonomy.enemyMemory.buildTelemetry(telemetryNow);

			// Phase 6.1: Task reservation counts
			result["task_reservations"] = nlohmann::json::object({
				{"active_total", m_autonomy.taskReservationManager.getActiveTaskCount()},
				{"active_build", m_autonomy.taskReservationManager.getBuildTaskCount()},
				{"active_capture", m_autonomy.taskReservationManager.getCaptureTaskCount()}
			});
			result["static_defense_policy"] = m_autonomy.state.staticDefenseTelemetry.is_array()
				? m_autonomy.state.staticDefenseTelemetry
				: nlohmann::json::array();
			result["palace_redundancy"] = m_autonomy.state.palaceRedundancyTelemetry.is_array()
				? m_autonomy.state.palaceRedundancyTelemetry
				: nlohmann::json::array();
			result["garrisons"] = m_autonomy.state.garrisonTelemetry.is_array()
				? m_autonomy.state.garrisonTelemetry
				: nlohmann::json::array();
			result["counterbattery"] = m_autonomy.state.counterbatteryTelemetry.is_object()
				? m_autonomy.state.counterbatteryTelemetry
				: nlohmann::json::object({
					{"artillery_threats", nlohmann::json::array()},
					{"mobile_siege_threats", nlohmann::json::array()},
					{"active_tasks", 0},
					{"assigned_units", 0},
					{"production_needed", false},
					{"reason", "not_evaluated"}
				});
			result["mobile_siege_counterbattery"] = result["counterbattery"];
				result["survival_policy"] = m_autonomy.state.survivalPolicyTelemetry.is_object()
					? m_autonomy.state.survivalPolicyTelemetry
					: nlohmann::json::object({
					{"state", "stable"},
					{"priority", "normal_macro"},
					{"reason", "not_evaluated"},
					{"critical_zones", 0},
					{"defense_reserve_deficits", 0},
					{"active_combat_tasks", 0},
					{"ready_scuds", 0},
					{"enemy_wmd_targets", 0},
					{"ready_barracks", 0},
					{"ready_arms_dealers", 0},
					{"money", 0},
					{"reserve", 0},
						{"cash_float", 0},
						{"block_exposed_wmd_foundations", false}
					});
				result["gla_usa_strategy"] = m_autonomy.state.glaUsaStrategyTelemetry.is_object()
					? m_autonomy.state.glaUsaStrategyTelemetry
					: nlohmann::json::object({
						{"active", false},
						{"usa_enemy", false},
						{"pressure", "none"},
						{"reason", "not_evaluated"},
						{"tunnel_role", "standard_defense"},
						{"tunnel_missiles_satisfy_armor", true},
						{"scorpion_floor", 0},
						{"quad_floor", 0},
						{"preserve_attack_group", false},
						{"reserved_strike_group", 0},
						{"camouflage", nlohmann::json::object({
							{"desired", false},
							{"spend_allowed", false},
							{"reason", "not_evaluated"}
						})},
						{"worker_mobility", nlohmann::json::object({
							{"desired", false},
							{"mode", "walk"},
							{"desired_shuttle_technicals", 0},
							{"protected_technicals", 0},
							{"production_needed", false},
							{"reservation_reason", "not_evaluated"},
							{"reason", "not_evaluated"}
						})},
						{"worker_shuttle_tasks", nlohmann::json::array()}
					});
				result["brutal_pressure"] = m_autonomy.state.brutalPressureTelemetry.is_object()
				? m_autonomy.state.brutalPressureTelemetry
				: nlohmann::json::object({
					{"expansion_gap", 0},
					{"main_under_pressure", false},
					{"static_defense_gap", 0},
					{"garrison_gap", 0},
					{"local_worker_gap", 0},
					{"stale_foundations", 0},
					{"mobile_siege_threats", 0},
					{"reserve_protected", false},
					{"cash_float", 0},
					{"chosen_priority", "not_evaluated"},
					{"reason", "not_evaluated"}
				});
			result["emergency_survival"] = m_autonomy.state.emergencySurvivalTelemetry.is_object()
				? m_autonomy.state.emergencySurvivalTelemetry
				: nlohmann::json::object({
					{"active", false},
					{"allow_reserve_spend", false},
					{"suppress_capture_source_production", false},
					{"bypass_army_cap_buffer", false},
					{"emergency_army_cap", 0},
					{"reason", "not_evaluated"}
				});
			result["strategic_spend"] = m_autonomy.state.strategicSpendTelemetry.is_object()
				? m_autonomy.state.strategicSpendTelemetry
				: AIControlAdapterStrategicSpendTelemetrySerializer().BuildDefaultTelemetry();
			result["macro_build"] = m_autonomy.state.macroBuildTelemetry.is_object()
				? m_autonomy.state.macroBuildTelemetry
				: AIControlAdapterMacroBuildTelemetrySerializer().BuildDefaultTelemetry();
			result["main_base_critical_override"] = m_autonomy.state.mainBaseCriticalOverrideTelemetry.is_object()
				? m_autonomy.state.mainBaseCriticalOverrideTelemetry
				: nlohmann::json::object({
					{"active", false},
					{"zone", 0},
					{"reason", "not_evaluated"},
					{"local_enemies", 0},
					{"recent_wmd", false},
					{"damaged_structures", 0},
					{"destroyed_structures", 0},
					{"assigned_units", 0},
					{"requested_units", 0}
				});
			result["zone_defense_reserves"] = m_autonomy.state.zoneDefenseReserveTelemetry.is_array()
				? m_autonomy.state.zoneDefenseReserveTelemetry
				: nlohmann::json::array();
			result["pathing"] = m_autonomy.state.pathingTelemetry.is_object()
				? m_autonomy.state.pathingTelemetry
				: nlohmann::json::object({
					{"source", "unavailable"},
					{"terrain_features", nlohmann::json::array()}
				});

			nlohmann::json zoneThreats = nlohmann::json::array();
			for (const auto& pair : m_autonomy.state.zoneThreats)
			{
				const AutonomyZoneThreatState& threat = pair.second;
				zoneThreats.push_back(nlohmann::json::object({
					{"zone_id", std::string("zone-") + std::to_string(pair.first)},
					{"anchor_id", pair.first},
					{"type", threat.sourceType.empty() ? "unknown" : threat.sourceType},
					{"severity", threat.level.empty() ? "low" : threat.level},
					{"response", threat.response.empty() ? "limited_scout" : threat.response},
					{"local_enemy_count", threat.localEnemyCount},
					{"enemy_artillery_count", threat.enemyArtilleryCount},
					{"recent_wmd", threat.recentWmd},
					{"damaged_structures", threat.damagedStructures},
					{"destroyed_structures", threat.destroyedStructures},
					{"last_seen_tick", static_cast<UnsignedInt>(threat.lastSeenTick)},
					{"reason", threat.reason.empty() ? "damage_source_unknown" : threat.reason}
				}));
			}
			result["zone_threats"] = zoneThreats;

			// Phase 6.2: Active capture task details
			nlohmann::json captureTasks = nlohmann::json::array();
			const DWORD now = ::GetTickCount();
			std::vector<SpecialTaskReservation*> activeCaptureTasks = m_autonomy.taskReservationManager.findCaptureTasks();
			for (const SpecialTaskReservation* task : activeCaptureTasks)
			{
				if (task == nullptr)
				{
					continue;
				}
				// Skip terminal states
				if (task->state == SpecialTaskState::Complete ||
					task->state == SpecialTaskState::Failed ||
					task->state == SpecialTaskState::Expired)
				{
					continue;
				}

				nlohmann::json taskDetail = nlohmann::json::object();
				taskDetail["task_id"] = task->taskId;
				taskDetail["source_id"] = task->sourceObjectId;
				taskDetail["target_id"] = task->targetObjectId;
				// Phase 6.2: Expose capture-specific state name "capturing" instead of generic "executing"
				taskDetail["state"] = task->state == SpecialTaskState::Assigned ? "assigned" :
					task->state == SpecialTaskState::Moving ? "moving" :
					task->state == SpecialTaskState::Executing ? "capturing" : "unknown";
				taskDetail["reason"] = task->reason;
				taskDetail["owner"] = task->owner;
				taskDetail["age_ms"] = now - task->createdTick;
				taskDetail["last_update_age_ms"] = now - task->lastUpdateTick;

				// Look up source object
				Object* source = TheGameLogic != nullptr ? TheGameLogic->findObjectByID((ObjectID)task->sourceObjectId) : nullptr;
				if (source != nullptr && !source->isEffectivelyDead())
				{
					const Coord3D* sourcePos = source->getPosition();
					if (sourcePos != nullptr)
					{
						taskDetail["source_x"] = sourcePos->x;
						taskDetail["source_y"] = sourcePos->y;
					}
					const ThingTemplate* sourceTemplate = source->getTemplate();
					if (sourceTemplate != nullptr)
					{
						taskDetail["source_template"] = sourceTemplate->getName().str();
					}
				}

				// Look up target object
				Object* target = TheGameLogic != nullptr ? TheGameLogic->findObjectByID((ObjectID)task->targetObjectId) : nullptr;
				if (target != nullptr && !target->isEffectivelyDead())
				{
					const Coord3D* targetPos = target->getPosition();
					if (targetPos != nullptr)
					{
						taskDetail["target_x"] = targetPos->x;
						taskDetail["target_y"] = targetPos->y;
					}
					const ThingTemplate* targetTemplate = target->getTemplate();
					if (targetTemplate != nullptr)
					{
						taskDetail["target_template"] = targetTemplate->getName().str();
					}
					Player* targetOwner = target->getControllingPlayer();
					if (targetOwner != nullptr)
					{
						taskDetail["target_owner_index"] = targetOwner->getPlayerIndex();
					}

					// Calculate distance
					if (source != nullptr && !source->isEffectivelyDead())
					{
						const Coord3D* sourcePos = source->getPosition();
						const Coord3D* targetPos = target->getPosition();
						if (sourcePos != nullptr && targetPos != nullptr)
						{
							const Real dx = targetPos->x - sourcePos->x;
							const Real dy = targetPos->y - sourcePos->y;
							const Real distance = std::sqrt(dx * dx + dy * dy);
							taskDetail["distance_to_target"] = distance;
						}
					}
				}

				captureTasks.push_back(taskDetail);
			}
			result["capture_tasks"] = captureTasks;

			// Phase 6.3: Capture source capacity tracking
			Player* player = ThePlayerList != nullptr ? ThePlayerList->getLocalPlayer() : nullptr;
			if (player != nullptr)
			{
				// Count live capture sources
				int captureSourcesLive = 0;
				std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
				collectOwnedAutomationObjects(player, ownedObjects);
				for (std::size_t i = 0; i < ownedObjects.size(); ++i)
				{
					const auto& owned = ownedObjects[i];
					if (owned.object != nullptr && !owned.underConstruction && owned.hasCapturePower)
					{
						++captureSourcesLive;
					}
				}

				// Count reserved capture sources
				int captureSourcesReserved = 0;
				for (const SpecialTaskReservation* task : activeCaptureTasks)
				{
					if (task != nullptr &&
						task->state != SpecialTaskState::Complete &&
						task->state != SpecialTaskState::Failed &&
						task->state != SpecialTaskState::Expired)
					{
						++captureSourcesReserved;
					}
				}

				// Count capturable targets remaining
				int capturableTargetsRemaining = 0;
				std::vector<Object*> capturableTargets;
				collectCapturableTargetsForPlayer(player, capturableTargets);
				capturableTargetsRemaining = static_cast<int>(capturableTargets.size());

				// Compute available and desired
				int captureSourcesAvailable = captureSourcesLive - captureSourcesReserved;
				if (captureSourcesAvailable < 0)
				{
					captureSourcesAvailable = 0;
				}

				const UpgradeTemplate* captureUpgrade = TheUpgradeCenter != nullptr
					? TheUpgradeCenter->findUpgrade("Upgrade_InfantryCaptureBuilding")
					: nullptr;
				const bool hasCaptureUpgrade = captureUpgrade != nullptr && player->hasUpgradeComplete(captureUpgrade);
				const int maxCaptureConcurrent = m_automation.captureRule.maxConcurrent;

				int desiredCaptureSources = 0;
				bool productionNeeded = false;
				if (hasCaptureUpgrade && m_automation.captureRule.enabled && capturableTargetsRemaining > 0)
				{
					const int targetReserve = maxCaptureConcurrent + 2;
					desiredCaptureSources = targetReserve < capturableTargetsRemaining ? targetReserve : capturableTargetsRemaining;
					productionNeeded = captureSourcesAvailable < desiredCaptureSources;
				}

				result["capture_sources"] = nlohmann::json::object({
					{"live", captureSourcesLive},
					{"reserved", captureSourcesReserved},
					{"available", captureSourcesAvailable},
					{"desired", desiredCaptureSources},
					{"capturable_targets_remaining", capturableTargetsRemaining},
					{"max_concurrent", maxCaptureConcurrent},
					{"production_needed", productionNeeded}
				});
			}

			// Phase 9: Combat task counts
			nlohmann::json combatTaskDetails = nlohmann::json::array();
			std::vector<CombatTask*> combatTaskTelemetryTasks = m_autonomy.combatTaskManager.findActiveTasks();
			for (CombatTask* task : combatTaskTelemetryTasks)
			{
				if (task == nullptr)
				{
					continue;
				}
				nlohmann::json waypoints = nlohmann::json::array();
				for (std::size_t waypointIndex = 0; waypointIndex < task->waypoints.size(); ++waypointIndex)
				{
					const CombatTaskWaypoint& waypoint = task->waypoints[waypointIndex];
					waypoints.push_back(nlohmann::json::object({
						{"index", static_cast<int>(waypointIndex)},
						{"x", waypoint.position.x},
						{"y", waypoint.position.y},
						{"z", waypoint.position.z},
						{"radius", waypoint.radius}
					}));
				}
				nlohmann::json detail = nlohmann::json::object({
					{"task_id", task->taskId},
					{"type", combatTaskTypeName(task->type)},
					{"state", combatTaskStateName(task->state)},
					{"owner", task->owner},
					{"reason", task->cohesionReason.empty() ? task->reason : task->cohesionReason},
					{"assigned_count", static_cast<int>(task->assignedUnitIds.size())},
					{"arrived_count", task->arrivedCount},
					{"missing_count", task->missingCount},
					{"confirmed_dead_count", task->confirmedDeadCount},
					{"required_quorum_count", task->requiredQuorumCount},
					{"infantry_count", task->infantryCount},
					{"infantry_arrived_count", task->infantryArrivedCount},
					{"infantry_required_quorum_count", task->infantryRequiredQuorumCount},
					{"vehicle_count", task->vehicleCount},
					{"raid_mode", task->raidMode.empty() ? "unknown" : task->raidMode},
					{"quorum_type", task->quorumType.empty() ? "group" : task->quorumType},
					{"degraded_from", task->degradedFrom},
					{"degrade_reason", task->degradeReason},
					{"group_spread", task->groupSpread},
					{"current_waypoint_index", task->currentWaypointIndex},
					{"waypoints", waypoints},
					{"probe_state", combatTaskProbeStateName(task->probeState)},
					{"probe_unit_count", static_cast<int>(task->probeUnitIds.size())},
					{"probe_started_tick", static_cast<UnsignedInt>(task->probeStartedTick)},
					{"probe_reason", task->probeReason},
					{"fresh_strategic_targets", task->freshStrategicTargets},
					{"cohesion_wait_ms", task->cohesionWaitStartTick > 0u ? static_cast<UnsignedInt>(telemetryNow - task->cohesionWaitStartTick) : 0u},
					{"target_position", nlohmann::json::object({
						{"x", task->targetPosition.x},
						{"y", task->targetPosition.y},
						{"z", task->targetPosition.z}
					})},
					{"age_ms", static_cast<UnsignedInt>(telemetryNow - task->createdTick)},
					{"last_command_age_ms", static_cast<UnsignedInt>(telemetryNow - task->lastCommandTick)}
				});
				if (task->hasOriginPosition)
				{
					detail["origin_position"] = nlohmann::json::object({
						{"x", task->originPosition.x},
						{"y", task->originPosition.y},
						{"z", task->originPosition.z}
					});
				}
				combatTaskDetails.push_back(detail);
			}
			result["combat_tasks"] = nlohmann::json::object({
				{"active_total", m_autonomy.combatTaskManager.getActiveTaskCount()},
				{"active_attack", m_autonomy.combatTaskManager.getAttackTaskCount()},
				{"active_defense", m_autonomy.combatTaskManager.getDefenseTaskCount()},
				{"active_guard", m_autonomy.combatTaskManager.getGuardTaskCount()},
				{"active_scout", m_autonomy.combatTaskManager.getScoutTaskCount()},
				{"total_assigned_units", m_autonomy.combatTaskManager.getTotalAssignedUnitCount()},
				{"details", combatTaskDetails}
			});
			result["scouting"] = m_autonomy.state.scoutingTelemetry.is_object()
				? m_autonomy.state.scoutingTelemetry
				: nlohmann::json::object();

			// Phase 7.4: WMD threat and counterbattery status
			nlohmann::json wmdTargetsArray = nlohmann::json::array();
			const std::vector<WMDTarget>& wmdTargets = m_autonomy.wmdTargetTracker.getAllTargets();
			for (std::size_t i = 0; i < wmdTargets.size(); ++i)
			{
				const WMDTarget& target = wmdTargets[i];
				wmdTargetsArray.push_back(nlohmann::json::object({
					{"object_id", target.objectId},
					{"template", target.templateName},
					{"owner_index", target.ownerIndex},
					{"x", target.position.x},
					{"y", target.position.y},
					{"priority", target.priority == WMDTargetPriority::Critical ? "critical" :
								 target.priority == WMDTargetPriority::High ? "high" : "medium"},
					{"visible", target.visible},
					{"alive", target.alive}
				}));
			}

			// Phase 7.4: SCUD reserve telemetry
			// Count live and queued SCUDs
			int liveScuds = 0;
			int queuedScuds = 0;
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || obj->isEffectivelyDead() || userData == nullptr)
				{
					return;
				}

				int* scudCount = static_cast<int*>(userData);
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";

				if (containsIgnoreCase(name, "scudlauncher"))
				{
					++(*scudCount);
				}
			}, &liveScuds);

			// For queued SCUDs, use the count already computed in macro counts
			// (avoids API issues with getProductionUpdate/getPlayer in this context)
			queuedScuds = 0; // Will be approximated from production telemetry if needed

			// Count SCUD counterbattery tasks
			int scudCounterbatteryTasks = 0;
			int scudCounterbatteryUnits = 0;
			const std::vector<CombatTask*> activeCombatTasks = m_autonomy.combatTaskManager.findActiveTasks();
			for (std::size_t i = 0; i < activeCombatTasks.size(); ++i)
			{
				const CombatTask* task = activeCombatTasks[i];
				if (task != nullptr && task->owner == "scud_counterbattery")
				{
					++scudCounterbatteryTasks;
					scudCounterbatteryUnits += static_cast<int>(task->assignedUnitIds.size());
				}
			}

			// Phase 7.5: Mobile SCUD Launcher trucks are normal vehicle-mix units,
			// not a standing counter-WMD reserve.
			const bool hasWMDThreat = m_autonomy.wmdTargetTracker.hasActiveWMDThreat();
			const int baselineDesired = 0;
			const int wmdDesired = 0;
			const int totalDesired = 0;
			const bool productionNeeded = false;

			// Phase 7.5: Count stationary SCUD Storms separately
			struct ScudStormTelemetryContext {
				int liveCount = 0;
				int readyCount = 0;
				int healthyUnderConstructionCount = 0;
				int stoppedFoundationCount = 0;
				AIControlAdapterState* self = nullptr;
				std::set<UnsignedInt> countedFoundationIds;
			};
			ScudStormTelemetryContext scudStormTelCtx;
			scudStormTelCtx.self = this;

			player->iterateObjects([](Object* obj, void* userData) -> void {
				if (obj == nullptr)
				{
					return;
				}

				ScudStormTelemetryContext* ctx = static_cast<ScudStormTelemetryContext*>(userData);

				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";

				if (containsIgnoreCase(name, "scudstorm"))
				{
					++(ctx->liveCount);
					const bool underConstruction = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
					if (!underConstruction)
					{
						++(ctx->readyCount);
					}
					else
					{
						const UnsignedInt objectId = static_cast<UnsignedInt>(obj->getID());
						if (ctx->self != nullptr && ctx->self->isStoppedStrategicFoundation(objectId))
						{
							++(ctx->stoppedFoundationCount);
						}
						else
						{
							++(ctx->healthyUnderConstructionCount);
							ctx->countedFoundationIds.insert(objectId);
						}
					}
				}
			}, &scudStormTelCtx);

			const int scudStormBuildTasks = countHealthyScudStormBuildTasks(scudStormTelCtx.countedFoundationIds);
			const int liveScudStorms = scudStormTelCtx.liveCount;
			const int readyScudStorms = scudStormTelCtx.readyCount;
			const int inProgressScudStorms = scudStormTelCtx.healthyUnderConstructionCount + scudStormBuildTasks;
			const int desiredScudStorms = 10;
			const int effectiveScudStorms = readyScudStorms + inProgressScudStorms;
			std::vector<AIControlAdapterScudStormStrategicTargetCandidate> scudStormStrategicCandidates;
			for (const EnemyMemoryItem& item : m_autonomy.enemyMemory.getItems())
			{
				AIControlAdapterScudStormStrategicTargetCandidate candidate;
				candidate.objectId = item.objectId;
				candidate.playerIndex = item.playerIndex;
				candidate.team = item.team;
				candidate.targetKind = AIControlAdapterEnemyMemory::kindToString(item.kind);
				candidate.templateName = item.templateName;
				candidate.visible = item.visible;
				candidate.stale = item.stale;
				candidate.enemyOwned = true;
				candidate.alive = true;
				candidate.ageMs = item.lastSeenTick == 0u ? 0u : static_cast<unsigned int>(telemetryNow - item.lastSeenTick);
				candidate.x = item.position.x;
				candidate.y = item.position.y;
				candidate.z = item.position.z;
				scudStormStrategicCandidates.push_back(candidate);
			}
			const AIControlAdapterScudStormStrategicTargetResult scudStormStrategicTarget =
				AIControlAdapterSelectScudStormStrategicTarget({
					readyScudStorms > 0,
					hasWMDThreat,
					false,
					120000u,
					scudStormStrategicCandidates
				});
			nlohmann::json scudStormStrategicTargetTelemetry = nlohmann::json::object({
				{"reason", scudStormStrategicTarget.reason}
			});
			if (scudStormStrategicTarget.hasTarget)
			{
				scudStormStrategicTargetTelemetry = nlohmann::json::object({
					{"object_id", scudStormStrategicTarget.objectId},
					{"player_index", scudStormStrategicTarget.playerIndex},
					{"team", scudStormStrategicTarget.team},
					{"template", scudStormStrategicTarget.templateName},
					{"target_kind", scudStormStrategicTarget.targetKind},
					{"visible", scudStormStrategicTarget.visible},
					{"stale", scudStormStrategicTarget.stale},
					{"age_ms", scudStormStrategicTarget.ageMs},
					{"x", scudStormStrategicTarget.x},
					{"y", scudStormStrategicTarget.y},
					{"z", scudStormStrategicTarget.z},
					{"reason", scudStormStrategicTarget.reason}
				});
			}

			// Phase 7.5: wmd_counter telemetry structure
			result["wmd_targets"] = wmdTargetsArray;
			result["wmd_counter"] = nlohmann::json::object({
				{"enemy_wmd_targets", wmdTargetsArray},
				{"scud_storms", nlohmann::json::object({
					{"desired", desiredScudStorms},
					{"live", liveScudStorms},
					{"complete", readyScudStorms},
					{"in_progress", inProgressScudStorms},
					{"ready", readyScudStorms},
					{"stale_stopped", scudStormTelCtx.stoppedFoundationCount},
					{"destroyed", 0},
					{"production_needed", effectiveScudStorms < desiredScudStorms},
					{"strategic_target", scudStormStrategicTargetTelemetry},
					{"reason", hasWMDThreat ? "enemy_wmd_detected" : "defensive_baseline"}
				})},
				{"mobile_scud_launchers", nlohmann::json::object({
					{"live", liveScuds},
					{"queued", queuedScuds},
					{"baseline_reserve_enabled", false}
				})}
			});
			nlohmann::json strategicFoundations = nlohmann::json::array();
			const AIControlAdapterStrategicFoundationSurvivalManager foundationSurvivalManager;
			for (const auto& pair : m_autonomy.state.strategicFoundationHealth)
			{
				const AutonomyStrategicFoundationState& state = pair.second;
				AIControlAdapterStrategicFoundationFact fact;
				fact.foundationId = pair.first;
				fact.templateName = state.templateName;
				fact.x = state.x;
				fact.y = state.y;
				fact.lastHealth = state.lastHealth;
				fact.nowTick = telemetryNow;
				fact.firstSeenTick = state.firstSeenTick;
				fact.lastSeenTick = state.lastSeenTick;
				fact.lastProgressTick = state.lastProgressTick;
				fact.recoveryAttempts = state.recoveryAttempts;
				fact.stopIssued = state.stopIssued;
				fact.reason = state.reason;
				const AIControlAdapterStrategicFoundationClassification classification =
					foundationSurvivalManager.Classify(fact);
				adapterLog(
					"strategic_foundation_survival template=%s foundation=%u state=%s reason=%s",
					state.templateName.c_str(),
					static_cast<unsigned int>(pair.first),
					classification.state,
					classification.reason);
				strategicFoundations.push_back(nlohmann::json::object({
					{"foundation_id", pair.first},
					{"template", state.templateName},
					{"last_health", state.lastHealth},
					{"position", nlohmann::json::object({
						{"x", state.x},
						{"y", state.y},
						{"z", state.z}
					})},
					{"last_seen_tick", state.lastSeenTick},
					{"last_progress_tick", state.lastProgressTick},
					{"no_progress_ms", state.lastProgressTick != 0u ? telemetryNow - state.lastProgressTick : 0u},
					{"recovery_attempts", state.recoveryAttempts},
					{"stop_issued", state.stopIssued},
					{"state", classification.state},
					{"failed", classification.failed},
					{"rebuild_blocked", classification.rebuildBlocked},
					{"reason", state.reason}
				}));
			}
			result["strategic_foundation_health"] = strategicFoundations;

			// Keep old scud_reserve for backward compatibility (deprecated)
			result["scud_reserve"] = nlohmann::json::object({
				{"baseline_desired", baselineDesired},
				{"wmd_desired", wmdDesired},
				{"desired", totalDesired},
				{"live", liveScuds},
				{"queued", queuedScuds},
				{"assigned_counterbattery", scudCounterbatteryUnits},
				{"production_needed", productionNeeded},
				{"reason", hasWMDThreat ? "wmd_threat" : "baseline_reserve"}
			});
			result["scud_counterbattery"] = nlohmann::json::object({
				{"active_tasks", scudCounterbatteryTasks},
				{"assigned_units", scudCounterbatteryUnits},
				{"has_active_threat", hasWMDThreat}
			});

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
			const AIControlAdapterProfilePolicyConfig zoneTargetPolicyConfig = resolveAutonomyProfilePolicyConfig();
			if (zoneTargetPolicyConfig.isSprawlStyle)
			{
				result["desired_zone_count"] = std::max<Int>(1, zoneTargetPolicyConfig.sprawlSupplyCap);
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

		AIControlAdapterTerrainFacts loadMapFileCacheFactsForMap(const std::string& mapName)
		{
			return m_terrainMapCacheService.loadFactsForMap(mapName, [this](const std::string& line) {
				adapterLog("%s", line.c_str());
			});
		}

		AIControlAdapterTerrainFacts buildEngineTerrainFacts(
			const std::string& mapName,
			bool hasMainBase,
			float mainBaseX,
			float mainBaseY) const
		{
			AIControlAdapterTerrainFacts facts;
			facts.mapName = mapName;
			facts.source = "unavailable";
			facts.extraction.mapName = mapName;
			facts.extraction.selectedSource = "unavailable";
			facts.extraction.fallbackSource = "none";
			facts.extraction.reason = "terrain_logic_unavailable";
			facts.extraction.sampleStep = 256;

			if (TheTerrainLogic == nullptr || !hasMainBase)
			{
				return facts;
			}

			const float sampleHalfSize = 2304.0f;
			facts.extraction.extent.minX = mainBaseX - sampleHalfSize;
			facts.extraction.extent.minY = mainBaseY - sampleHalfSize;
			facts.extraction.extent.maxX = mainBaseX + sampleHalfSize;
			facts.extraction.extent.maxY = mainBaseY + sampleHalfSize;

			int laneFeatureCount = 0;
			for (Waypoint* waypoint = TheTerrainLogic->getFirstWaypoint(); waypoint != nullptr; waypoint = waypoint->getNext())
			{
				++facts.extraction.waypointCount;
				if (laneFeatureCount >= 48)
				{
					continue;
				}
				const Coord3D* from = waypoint->getLocation();
				if (from == nullptr)
				{
					continue;
				}
				for (Int linkIdx = 0; linkIdx < waypoint->getNumLinks() && laneFeatureCount < 48; ++linkIdx)
				{
					Waypoint* linked = waypoint->getLink(linkIdx);
					const Coord3D* to = linked != nullptr ? linked->getLocation() : nullptr;
					if (to == nullptr)
					{
						continue;
					}
					AIControlAdapterTerrainFeature lane;
					lane.id = std::string("waypoint-lane-") + std::to_string(waypoint->getID()) + "-" + std::to_string(linked->getID());
					lane.kind = "lane";
					lane.source = "engine_query";
					lane.points.push_back({ from->x, from->y });
					lane.points.push_back({ to->x, to->y });
					const std::string label = waypoint->getPathLabel1().str();
					if (!label.empty())
					{
						lane.connects.push_back(label);
					}
					facts.features.push_back(lane);
					++laneFeatureCount;
				}
			}

			int bridgeFeatureCount = 0;
			for (Bridge* bridge = TheTerrainLogic->getFirstBridge(); bridge != nullptr; bridge = bridge->getNext())
			{
				++facts.extraction.bridgeCount;
				if (bridgeFeatureCount >= 16)
				{
					continue;
				}
				BridgeInfo info;
				bridge->getBridgeInfo(&info);
				AIControlAdapterTerrainFeature bridgeFeature;
				bridgeFeature.id = std::string("bridge-") + std::to_string(bridgeFeatureCount);
				bridgeFeature.kind = "chokepoint";
				bridgeFeature.source = "engine_query";
				bridgeFeature.hasPosition = true;
				bridgeFeature.position.x = (info.from.x + info.to.x) * 0.5f;
				bridgeFeature.position.y = (info.from.y + info.to.y) * 0.5f;
				bridgeFeature.width = info.bridgeWidth;
				bridgeFeature.points.push_back({ info.from.x, info.from.y });
				bridgeFeature.points.push_back({ info.to.x, info.to.y });
				facts.features.push_back(bridgeFeature);
				++bridgeFeatureCount;
			}

			int cliffFeatureCount = 0;
			const int step = std::max(64, facts.extraction.sampleStep);
			for (float y = facts.extraction.extent.minY; y <= facts.extraction.extent.maxY; y += static_cast<float>(step))
			{
				for (float x = facts.extraction.extent.minX; x <= facts.extraction.extent.maxX; x += static_cast<float>(step))
				{
					const Bool cliff = TheTerrainLogic->isCliffCell(x, y);
					if (cliff)
					{
						++facts.extraction.cliffSamples;
						if (cliffFeatureCount < 96)
						{
							AIControlAdapterTerrainFeature sample;
							sample.id = std::string("cliff-sample-") + std::to_string(cliffFeatureCount);
							sample.kind = "cliff_sample";
							sample.source = "engine_sample";
							sample.hasPosition = true;
							sample.position.x = x;
							sample.position.y = y;
							sample.width = static_cast<float>(step);
							facts.features.push_back(sample);
							++cliffFeatureCount;
						}
					}
					else
					{
						++facts.extraction.passableSamples;
					}
				}
			}

			if (facts.extraction.bridgeCount > 0 || facts.extraction.waypointCount > 0)
			{
				facts.source = "engine_query";
				facts.extraction.selectedSource = "engine_query";
				facts.extraction.reason = "engine_waypoints_or_bridges_available";
			}
			else if (facts.extraction.cliffSamples > 0)
			{
				facts.source = "engine_sample";
				facts.extraction.selectedSource = "engine_sample";
				facts.extraction.reason = "engine_sampling_available";
			}
			else
			{
				facts.source = "unavailable";
				facts.extraction.selectedSource = "unavailable";
				facts.extraction.reason = "engine_probe_no_features";
			}

			return facts;
		}

		nlohmann::json buildAutonomyZonesSnapshot()
		{
			Player* player = resolveAutonomyPlayer();
			updateEnemyMemory(player);

			nlohmann::json zones = nlohmann::json::array();
			const Real defaultRadius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
			const DWORD now = ::GetTickCount();

			if (m_autonomy.state.telemetryZones.is_array())
			{
				for (const auto& zone : m_autonomy.state.telemetryZones)
				{
					if (!zone.is_object())
					{
						continue;
					}

					const UnsignedInt anchorId = zone.value("anchor_id", 0u);
					const bool isMainBase = zone.value("is_main_base", false);
					const Int supplyStashes = zone.value("supply_stashes", 0);
					const Int barracks = zone.value("barracks", 0);
					const Int armsDealers = zone.value("arms_dealers", 0);
					const Int palaces = zone.value("palaces", 0);
					const Int blackMarkets = zone.value("black_markets", 0);
					const Int tunnels = zone.value("tunnels", 0);
					const Int stingers = zone.value("stingers", 0);
					const Real radius = std::max<Real>(160.0f, zone.value("effective_radius", defaultRadius));
					const bool active =
						m_autonomy.state.hasLastZone
						&& anchorId == m_autonomy.state.lastZoneAnchorId
						&& isMainBase == m_autonomy.state.lastZoneIsMainBase;
					nlohmann::json threatJson = nlohmann::json::object({
						{"level", "none"},
						{"under_attack", false},
						{"last_seen_tick", 0u},
						{"position", nullptr}
					});
					const auto threatIt = m_autonomy.state.zoneThreats.find(anchorId);
					if (threatIt != m_autonomy.state.zoneThreats.end())
					{
						const AutonomyZoneThreatState& threat = threatIt->second;
						threatJson = nlohmann::json::object({
							{"level", threat.level.empty() ? "low" : threat.level},
							{"under_attack", true},
							{"last_seen_tick", static_cast<UnsignedInt>(threat.lastSeenTick)},
							{"damaged_object_id", threat.damagedObjectId},
							{"damage_delta", threat.damageDelta},
							{"source_type", threat.sourceType.empty() ? "unknown" : threat.sourceType},
							{"response", threat.response.empty() ? "limited_scout" : threat.response},
							{"reason", threat.reason.empty() ? "damage_source_unknown" : threat.reason},
							{"local_enemy_count", threat.localEnemyCount},
							{"enemy_artillery_count", threat.enemyArtilleryCount},
							{"recent_wmd", threat.recentWmd},
							{"position", nlohmann::json::object({
								{"x", threat.positionX},
								{"y", threat.positionY}
							})}
						});
					}

					zones.push_back(nlohmann::json::object({
						{"id", std::string(isMainBase ? "main-" : "expansion-") + std::to_string(anchorId)},
						{"anchor_id", anchorId},
						{"kind", isMainBase ? "main_base" : "expansion"},
						{"active", active},
						{"developed", zone.value("developed", false)},
						{"needs_followup", zone.value("needs_followup", false)},
						{"center", nlohmann::json::object({
							{"x", zone.value("center_x", 0.0f)},
							{"y", zone.value("center_y", 0.0f)}
						})},
						{"radius", radius},
						{"base_radius", defaultRadius},
						{"terrain_limited", zone.value("terrain_limited", false)},
						{"terrain_reason", zone.value("terrain_reason", "")},
						{"terrain_entrance_id", zone.value("terrain_entrance_id", "")},
						{"front_point", nlohmann::json::object({
							{"x", zone.value("front_point_x", 0.0f)},
							{"y", zone.value("front_point_y", 0.0f)}
						})},
						{"rear_point", nlohmann::json::object({
							{"x", zone.value("rear_point_x", 0.0f)},
							{"y", zone.value("rear_point_y", 0.0f)}
						})},
						{"front_source", zone.value("front_source", "")},
						{"friendly", nlohmann::json::object({
							{"structures", supplyStashes + barracks + armsDealers + palaces + blackMarkets + tunnels + stingers},
							{"supply_stashes", supplyStashes},
							{"barracks", barracks},
							{"arms_dealers", armsDealers},
							{"palaces", palaces},
							{"black_markets", blackMarkets},
							{"tunnels", tunnels},
							{"stingers", stingers}
						})},
						{"threat", threatJson}
					}));
				}
			}

			return nlohmann::json::object({
				{"version", m_autonomy.state.zonesSnapshotVersion},
				{"tick", static_cast<UnsignedInt>(m_autonomy.state.zonesSnapshotTick)},
				{"zone_radius", m_autonomy.state.zoneRadius},
				{"selected_zone", nlohmann::json::object({
					{"active", m_autonomy.state.hasLastZone},
					{"anchor_id", m_autonomy.state.lastZoneAnchorId},
					{"kind", m_autonomy.state.lastZoneIsMainBase ? "main_base" : "expansion"},
					{"center", nlohmann::json::object({
						{"x", m_autonomy.state.lastZoneCenterX},
						{"y", m_autonomy.state.lastZoneCenterY}
					})}
				})},
				{"decision", nlohmann::json::object({
					{"category", m_autonomy.state.lastDecisionCategory},
					{"command", m_autonomy.state.lastDecisionCommand},
					{"reason", m_autonomy.state.lastDecisionReason}
				})},
				{"pathing", m_autonomy.state.pathingTelemetry.is_object()
					? m_autonomy.state.pathingTelemetry
					: nlohmann::json::object({
						{"source", "unavailable"},
						{"terrain_features", nlohmann::json::array()}
					})},
				{"enemy_memory", m_autonomy.enemyMemory.buildTelemetry(now)},
				{"zones", zones}
			});
		}

		static bool isArtilleryThreatTemplate(const std::string& templateName)
		{
			return AIControlAdapterIsBattlefieldArtilleryTemplate(templateName, false);
		}

		struct ZoneThreatEvidence
		{
			int localEnemyCount = 0;
			int enemyArtilleryCount = 0;
			bool recentWmd = false;
		};

		ZoneThreatEvidence collectZoneThreatEvidence(Player* player, Real x, Real y, Real localRadius, Real artilleryRadius) const
		{
			ZoneThreatEvidence evidence;
			evidence.recentWmd = m_autonomy.wmdTargetTracker.hasActiveWMDThreat();
			if (player == nullptr || TheGameLogic == nullptr || ThePlayerList == nullptr)
			{
				return evidence;
			}

			const Real localRadiusSq = localRadius * localRadius;
			const Real artilleryRadiusSq = artilleryRadius * artilleryRadius;
			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (obj == nullptr || obj->isEffectivelyDead())
				{
					continue;
				}
				Player* owner = obj->getControllingPlayer();
				if (owner == nullptr || owner == player || owner == ThePlayerList->getNeutralPlayer())
				{
					continue;
				}
				if (owner->getDefaultTeam() == nullptr || player->getRelationship(owner->getDefaultTeam()) != ENEMIES)
				{
					continue;
				}
				if (obj->isKindOf(KINDOF_STRUCTURE))
				{
					continue;
				}
				const ObjectShroudStatus shroudStatus = obj->getShroudedStatus(player->getPlayerIndex());
				if (shroudStatus != OBJECTSHROUD_CLEAR && shroudStatus != OBJECTSHROUD_PARTIAL_CLEAR)
				{
					continue;
				}
				const Coord3D* pos = obj->getPosition();
				if (pos == nullptr)
				{
					continue;
				}
				const Real dx = pos->x - x;
				const Real dy = pos->y - y;
				const Real distSq = dx * dx + dy * dy;
				if (distSq <= localRadiusSq)
				{
					++evidence.localEnemyCount;
				}
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				if (distSq <= artilleryRadiusSq && isArtilleryThreatTemplate(name))
				{
					++evidence.enemyArtilleryCount;
				}
			}
			return evidence;
		}

		template <typename ZoneT, typename CountsT>
		std::string classifyZoneRole(const ZoneT& zone, const CountsT& counts, bool isActiveZone, bool repeatedAttack) const
		{
			if (zone.isMainBase)
			{
				return "main_base";
			}
			const bool anchorZone =
				(counts.palaces + counts.palacesInProgress) > 0 ||
				(counts.blackMarkets + counts.blackMarketsInProgress) > 0 ||
				zone.anchorType == ZoneAnchorType::StrategicFoothold ||
				zone.anchorType == ZoneAnchorType::MarketFoothold;
			if (anchorZone)
			{
				return "anchor";
			}
			if (isActiveZone)
			{
				return "active";
			}
			if (repeatedAttack)
			{
				return "frontier";
			}
			const bool developed =
				(counts.supplyStashes + counts.supplyStashesInProgress) > 0 &&
				(counts.barracks + counts.armsDealers + counts.tunnels + counts.stingers) >= 3;
			return developed ? "developed_rear" : "rear";
		}

		template <typename ZoneT>
		int countBuildTasksNearZone(const std::string& templateFilter, const ZoneT& zone, Real radius) const
		{
			int count = 0;
			const Real radiusSq = radius * radius;
			std::vector<SpecialTaskReservation*> tasks =
				const_cast<AIControlAdapterTaskReservationManager&>(m_autonomy.taskReservationManager).findBuildTasks(templateFilter);
			for (const SpecialTaskReservation* task : tasks)
			{
				if (task == nullptr)
				{
					continue;
				}
				const Real dx = task->targetPosition.x - zone.center.x;
				const Real dy = task->targetPosition.y - zone.center.y;
				if (dx * dx + dy * dy <= radiusSq)
				{
					++count;
				}
			}
			return count;
		}

		bool isGarrisonReservedUnit(UnsignedInt unitId) const
		{
			for (const auto& pair : m_autonomy.state.garrisonAssignments)
			{
				const AutonomyGarrisonAssignment& assignment = pair.second;
				if (assignment.state == "released" || assignment.state == "failed")
				{
					continue;
				}
				for (std::size_t i = 0; i < assignment.infantryIds.size(); ++i)
				{
					if (assignment.infantryIds[i] == unitId)
					{
						return true;
					}
				}
			}
			return false;
		}

		using GarrisonDiscoveryResult = AIControlAdapterGarrisonDiscoveryResult;

		bool isNearMapCacheGarrison(const std::string& templateName, Real x, Real y, Real radius) const
		{
			return AIControlAdapterGarrisonManager::isNearMapCacheGarrison(
				m_autonomy.state.pathingTelemetry,
				templateName,
				x,
				y,
				radius);
		}

		GarrisonDiscoveryResult discoverGarrisonStructure(Player* player, Object* obj) const
		{
			if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_STRUCTURE))
			{
				GarrisonDiscoveryResult result;
				result.reason = "not_structure";
				return result;
			}
			const ThingTemplate* tt = obj->getTemplate();
			const std::string name = tt != nullptr ? tt->getName().str() : "";
			AIControlAdapterGarrisonDiscoveryFacts facts;
			facts.templateName = name;
			facts.palace = isPalaceTemplateName(name);
			facts.kindFlag = obj->isKindOf(KINDOF_GARRISONABLE_UNTIL_DESTROYED);
			ContainModuleInterface* contain = obj->getContain();
			if (contain != nullptr)
			{
				facts.hasContain = true;
				facts.containGarrison = contain->isGarrisonable();
				facts.capacity = contain->getContainMax();
			}
			const Coord3D* pos = obj->getPosition();
			if (pos != nullptr)
			{
				facts.mapCacheGarrison = isNearMapCacheGarrison(name, pos->x, pos->y, 90.0f);
			}
			facts.uiEnterable = TheActionManager != nullptr && player != nullptr
				&& TheActionManager->canPlayerGarrison(player, obj, CMD_FROM_PLAYER);
			return AIControlAdapterGarrisonManager::evaluateDiscovery(facts);
		}

		bool isUsefulGarrisonStructure(Player* player, Object* obj) const
		{
			return discoverGarrisonStructure(player, obj).accepted;
		}

		bool shouldLogGarrisonDiscovery(Player* player, Object* obj, const GarrisonDiscoveryResult& discovery, DWORD now)
		{
			if (obj == nullptr)
			{
				return false;
			}
			const ThingTemplate* tt = obj->getTemplate();
			const std::string name = tt != nullptr ? tt->getName().str() : "";
			if (!AIControlAdapterGarrisonManager::shouldLogDiscovery(name, discovery))
			{
				return false;
			}
			char key[192];
			std::snprintf(
				key,
				sizeof(key),
				"garrison_discovery:%u:%s:%d",
				static_cast<unsigned int>(obj->getID()),
				discovery.reason.c_str(),
				discovery.accepted ? 1 : 0);
			(void)player;
			return shouldLogScoutReservationSkip(key, now, 15000u);
		}

		bool isEnemyControlledObject(Player* player, Object* obj) const
		{
			if (player == nullptr || obj == nullptr)
			{
				return false;
			}
			Player* owner = obj->getControllingPlayer();
			if (owner == nullptr || owner == player || owner->getDefaultTeam() == nullptr)
			{
				return false;
			}
			return player->getRelationship(owner->getDefaultTeam()) == ENEMIES;
		}

		bool isNearGarrisonFeature(Real x, Real y, const char* wantedKind, Real radius) const
		{
			return AIControlAdapterGarrisonManager::isNearGarrisonFeature(
				m_autonomy.state.pathingTelemetry,
				x,
				y,
				wantedKind,
				radius);
		}

		bool isNearArtilleryPlatform(Real x, Real y, Real radius) const
		{
			const Real radiusSq = radius * radius;
			for (Object* obj = TheGameLogic != nullptr ? TheGameLogic->getFirstObject() : nullptr; obj != nullptr; obj = obj->getNextObject())
			{
				if (obj == nullptr || obj->isEffectivelyDead())
				{
					continue;
				}
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				if (!containsIgnoreCase(name, "artilleryplatform"))
				{
					continue;
				}
				const Coord3D* pos = obj->getPosition();
				if (pos == nullptr)
				{
					continue;
				}
				const Real dx = pos->x - x;
				const Real dy = pos->y - y;
				if ((dx * dx) + (dy * dy) <= radiusSq)
				{
					return true;
				}
			}
			return false;
		}

		bool issueGarrisonCommand(Player* player, Object* structure, const std::vector<Object*>& infantry, std::string& reason)
		{
			if (player == nullptr || structure == nullptr || infantry.empty())
			{
				reason = "invalid_garrison_request";
				return false;
			}
			std::vector<ObjectID> ids;
			for (Object* unit : infantry)
			{
				if (unit != nullptr)
				{
					ids.push_back(unit->getID());
				}
			}
			if (ids.empty())
			{
				reason = "no_infantry";
				return false;
			}
			const ObjectID structureId = structure->getID();
			return executeScopedSelectionCommand(player, ids, reason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_ENTER);
				if (msg == nullptr)
				{
					reason = "message_stream_not_ready";
					return false;
				}
				msg->appendObjectIDArgument(INVALID_ID);
				msg->appendObjectIDArgument(structureId);
				return true;
			});
		}

		std::string templateNameForObject(const Object* obj) const
		{
			const ThingTemplate* tt = obj != nullptr ? obj->getTemplate() : nullptr;
			return tt != nullptr ? tt->getName().str() : std::string();
		}

		int countEnteredGarrisonInfantry(const AutonomyGarrisonAssignment& assignment) const
		{
			int entered = 0;
			for (const AutonomyGarrisonInfantryAssignment& unit : assignment.infantry)
			{
				if (unit.entered)
				{
					++entered;
				}
			}
			return entered;
		}

		nlohmann::json garrisonAssignmentTelemetry(const AutonomyGarrisonAssignment& assignment, DWORD now) const
		{
			AIControlAdapterGarrisonAssignmentTelemetry telemetry;
			telemetry.structureId = assignment.structureId;
			telemetry.zoneAnchorId = assignment.zoneAnchorId;
			telemetry.templateName = assignment.templateName;
			telemetry.x = assignment.x;
			telemetry.y = assignment.y;
			telemetry.desiredInfantry = assignment.desiredInfantry;
			telemetry.estimatedCapacity = assignment.estimatedCapacity;
			telemetry.infantryIds = assignment.infantryIds;
			telemetry.lastCommandTick = assignment.lastCommandTick;
			telemetry.lastProgressTick = assignment.lastProgressTick;
			telemetry.state = assignment.state;
			telemetry.reason = assignment.reason;
			for (const AutonomyGarrisonInfantryAssignment& unit : assignment.infantry)
			{
				AIControlAdapterGarrisonInfantryTelemetry infantry;
				infantry.unitId = unit.unitId;
				infantry.templateName = unit.templateName;
				infantry.lastX = unit.lastX;
				infantry.lastY = unit.lastY;
				infantry.lastDistance = unit.lastDistance;
				infantry.entered = unit.entered;
				infantry.enteredPendingVerification = unit.enteredPendingVerification;
				infantry.outside = unit.outside;
				infantry.nearby = unit.nearby;
				infantry.lastCommandTick = unit.lastCommandTick;
				infantry.lastProgressTick = unit.lastProgressTick;
				infantry.state = unit.state;
				infantry.reason = unit.reason;
				telemetry.infantry.push_back(infantry);
			}
			return AIControlAdapterGarrisonManager::buildAssignmentTelemetry(telemetry, now);
		}

		void evaluateGarrisonManagement(Player* player)
		{
			if (!isAutonomyModeActive() || player == nullptr || TheGameLogic == nullptr)
			{
				return;
			}
			const DWORD now = ::GetTickCount();
			if (AIControlAdapterIsTickInFuture(m_autonomy.state.nextGarrisonTick, now))
			{
				return;
			}
			m_autonomy.state.nextGarrisonTick = now + 6000u;
			m_autonomy.state.garrisonTelemetry = nlohmann::json::array();

			for (auto it = m_autonomy.state.garrisonAssignments.begin(); it != m_autonomy.state.garrisonAssignments.end(); )
			{
				AutonomyGarrisonAssignment& assignment = it->second;
				Object* structure = TheGameLogic->findObjectByID(static_cast<ObjectID>(assignment.structureId));
				std::string releaseReason;
				if (structure == nullptr || structure->isEffectivelyDead())
				{
					releaseReason = "destroyed";
				}
				else if (isEnemyControlledObject(player, structure))
				{
					releaseReason = "enemy_owned";
				}
				else if (!isUsefulGarrisonStructure(player, structure))
				{
					releaseReason = "no_longer_useful";
				}
				else
				{
					ContainModuleInterface* contain = structure->getContain();
					const ContainedItemsList* containedItems = contain != nullptr ? contain->getContainedItemsList() : nullptr;
					std::vector<AutonomyGarrisonInfantryAssignment> retainedInfantry;
					std::vector<Object*> reissueInfantry;
					bool progressObserved = false;
					for (AutonomyGarrisonInfantryAssignment unit : assignment.infantry)
					{
						Object* infantry = TheGameLogic->findObjectByID(static_cast<ObjectID>(unit.unitId));
						bool listedContained = false;
						if (containedItems != nullptr)
						{
							for (ContainedItemsList::const_iterator containedIt = containedItems->begin(); containedIt != containedItems->end(); ++containedIt)
							{
								const Object* contained = *containedIt;
								if (contained != nullptr && contained->getID() == static_cast<ObjectID>(unit.unitId))
								{
									listedContained = true;
									break;
								}
							}
						}
						unit.entered = false;
						unit.enteredPendingVerification = false;
						unit.outside = false;
						unit.nearby = false;
						if (infantry != nullptr && !infantry->isEffectivelyDead())
						{
							if (unit.templateName.empty())
							{
								unit.templateName = templateNameForObject(infantry);
							}
							const Coord3D* unitPos = infantry->getPosition();
							if (unitPos != nullptr)
							{
								unit.lastX = unitPos->x;
								unit.lastY = unitPos->y;
								const Real dx = unitPos->x - assignment.x;
								const Real dy = unitPos->y - assignment.y;
								unit.lastDistance = std::sqrt((dx * dx) + (dy * dy));
								unit.nearby = unit.lastDistance >= 0.0f && unit.lastDistance <= 120.0f;
							}

							bool containedByStructure = infantry->getContainedBy() == structure;
							containedByStructure = containedByStructure || listedContained;
							if (containedByStructure)
							{
								unit.entered = true;
								unit.outside = false;
								unit.nearby = false;
								unit.state = "entered";
								unit.reason = "contained";
								unit.lastProgressTick = now;
								progressObserved = true;
							}
							else
							{
								unit.outside = true;
								if (unit.nearby)
								{
									unit.reason = "near_target_not_entered";
								}
								else
								{
									unit.reason = "moving_to_garrison";
								}
								unit.state = "entering";
								if (unit.lastDistance >= 0.0f && (unit.lastProgressTick == 0u || unit.lastDistance < 140.0f))
								{
									unit.lastProgressTick = unit.lastProgressTick == 0u ? now : unit.lastProgressTick;
								}
								if (unit.lastCommandTick == 0u || now - unit.lastCommandTick >= 12000u)
								{
									reissueInfantry.push_back(infantry);
								}
							}
							retainedInfantry.push_back(unit);
						}
						else
						{
							if (listedContained)
							{
								unit.entered = true;
								unit.state = "entered";
								unit.reason = "contained_list";
								unit.lastProgressTick = now;
								progressObserved = true;
								retainedInfantry.push_back(unit);
							}
							else if (unit.lastCommandTick != 0u && now - unit.lastCommandTick <= 20000u && unit.lastDistance >= 0.0f && unit.lastDistance <= 140.0f)
							{
								unit.enteredPendingVerification = true;
								unit.state = "entered_pending_verification";
								unit.reason = "missing_near_target_after_enter";
								progressObserved = true;
								retainedInfantry.push_back(unit);
							}
							else
							{
								adapterLog(
									"garrison_release structure=%u infantry=%u reason=missing_infantry",
									assignment.structureId,
									unit.unitId);
							}
						}
					}
					assignment.infantry = retainedInfantry;
					assignment.infantryIds.clear();
					for (const AutonomyGarrisonInfantryAssignment& unit : assignment.infantry)
					{
						assignment.infantryIds.push_back(unit.unitId);
					}
					if (progressObserved)
					{
						assignment.lastProgressTick = now;
					}
					const int enteredCount = countEnteredGarrisonInfantry(assignment);
					if (enteredCount >= assignment.desiredInfantry && assignment.desiredInfantry > 0)
					{
						assignment.state = "entered";
						assignment.reason = "desired_entered";
						if (assignment.taskId != 0u)
						{
							m_autonomy.combatTaskManager.updateTaskState(assignment.taskId, CombatTaskState::Engaging, "garrison_entered");
						}
					}
					else if (enteredCount > 0)
					{
						assignment.state = "partial";
						assignment.reason = "partial_entry";
					}
					else if (assignment.infantry.empty() && assignment.assignedTick != 0u && now - assignment.assignedTick > 45000u)
					{
						releaseReason = "task_failed";
					}
					else
					{
						assignment.state = "entering";
						assignment.reason = "awaiting_entry";
					}

					const DWORD noProgressAge = assignment.lastProgressTick != 0u ? now - assignment.lastProgressTick : now - assignment.assignedTick;
					if (releaseReason.empty() && assignment.assignedTick != 0u && noProgressAge >= 60000u && enteredCount == 0)
					{
						releaseReason = "entry_timeout";
					}
					else if (releaseReason.empty() && !reissueInfantry.empty() && enteredCount < assignment.desiredInfantry)
					{
						std::string reissueReason;
						const bool reissued = issueGarrisonCommand(player, structure, reissueInfantry, reissueReason);
						if (reissued)
						{
							assignment.lastCommandTick = now;
							for (AutonomyGarrisonInfantryAssignment& unit : assignment.infantry)
							{
								for (Object* reissuedUnit : reissueInfantry)
								{
									if (reissuedUnit != nullptr && reissuedUnit->getID() == static_cast<ObjectID>(unit.unitId))
									{
										unit.lastCommandTick = now;
										unit.reason = "enter_reissued";
										break;
									}
								}
							}
						}
						adapterLog(
							"garrison_reissue structure=%u infantry=%d issued=%d reason=%s",
							assignment.structureId,
							static_cast<int>(reissueInfantry.size()),
							reissued ? 1 : 0,
							reissued ? "no_entry_progress" : reissueReason.c_str());
					}
				}

				if (!releaseReason.empty())
				{
					if (assignment.taskId != 0u)
					{
						m_autonomy.combatTaskManager.expireTask(assignment.taskId, releaseReason);
					}
					for (unsigned int infantryId : assignment.infantryIds)
					{
						adapterLog(
							"garrison_release structure=%u infantry=%u reason=%s",
							assignment.structureId,
							infantryId,
							releaseReason.c_str());
					}
					adapterLog(
						"garrison_assignment structure=%u template=%s zone=%u infantry=%d state=released reason=%s",
						assignment.structureId,
						assignment.templateName.c_str(),
						assignment.zoneAnchorId,
						static_cast<int>(assignment.infantryIds.size()),
						releaseReason.c_str());
					it = m_autonomy.state.garrisonAssignments.erase(it);
					continue;
				}

				m_autonomy.state.garrisonTelemetry.push_back(garrisonAssignmentTelemetry(assignment, now));
				++it;
			}

			if (!m_autonomy.state.telemetryZones.is_array())
			{
				return;
			}

			struct GarrisonZone
			{
				UnsignedInt anchorId = 0;
				Real x = 0.0f;
				Real y = 0.0f;
				bool active = false;
				bool developed = false;
				bool useful = false;
			};
			std::vector<GarrisonZone> usefulZones;
			for (const auto& zoneJson : m_autonomy.state.telemetryZones)
			{
				if (!zoneJson.is_object())
				{
					continue;
				}
				GarrisonZone zone;
				zone.anchorId = zoneJson.value("anchor_id", 0u);
				zone.x = zoneJson.value("center_x", 0.0f);
				zone.y = zoneJson.value("center_y", 0.0f);
				zone.active = zoneJson.value("active", false);
				zone.developed = zoneJson.value("developed", false);
				const bool anchor =
					zoneJson.value("palaces", 0) > 0 ||
					zoneJson.value("black_markets", 0) > 0 ||
					zoneJson.value("anchor_type", std::string("")) == "strategic_foothold" ||
					zoneJson.value("anchor_type", std::string("")) == "market_foothold";
				const auto threatIt = m_autonomy.state.zoneThreats.find(zone.anchorId);
				const bool repeatedAttack = threatIt != m_autonomy.state.zoneThreats.end()
					&& (now - threatIt->second.lastSeenTick) <= 60000u;
				zone.useful = zone.active || zone.developed || anchor || repeatedAttack;
				if (zone.useful)
				{
					usefulZones.push_back(zone);
				}
			}

			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			std::vector<Object*> availableInfantry;
			std::vector<Object*> availableRpgInfantry;
			std::vector<Object*> availableRebelFallback;
			int totalInfantry = 0;
			int barracksReady = 0;
			int queuedGarrisonInfantry = 0;
			Object* firstBarracks = nullptr;
			std::vector<Object*> readyBarracks;
			for (const AutomationOwnedObjectSnapshot& owned : ownedObjects)
			{
				if (owned.isBarracks && !owned.underConstruction && owned.object != nullptr)
				{
					++barracksReady;
					readyBarracks.push_back(owned.object);
					if (firstBarracks == nullptr)
					{
						firstBarracks = owned.object;
					}
					ProductionUpdateInterface* production = owned.object->getProductionUpdateInterface();
					if (production != nullptr && TheThingFactory != nullptr)
					{
						const ThingTemplate* rpgTemplate = TheThingFactory->findTemplate(AsciiString("GLAInfantryTunnelDefender"), false);
						if (rpgTemplate != nullptr)
						{
							queuedGarrisonInfantry += static_cast<int>(production->countUnitTypeInQueue(rpgTemplate));
						}
					}
				}
				if (owned.object == nullptr || !owned.isInfantry || owned.isStructure || owned.underConstruction)
				{
					continue;
				}
				++totalInfantry;
				const UnsignedInt id = static_cast<UnsignedInt>(owned.object->getID());
				const bool isRpg = containsIgnoreCase(owned.name, "tunneldefender") || containsIgnoreCase(owned.name, "rpg");
				const bool isRebel = containsIgnoreCase(owned.name, "rebel");
				if (!owned.hasAI
					|| owned.isDozer
					|| owned.isHarvester
					|| m_autonomy.taskReservationManager.isObjectReserved(id)
					|| m_autonomy.combatTaskManager.isUnitReserved(id)
					|| isGarrisonReservedUnit(id))
				{
					continue;
				}
				if (isRpg)
				{
					availableRpgInfantry.push_back(owned.object);
					availableInfantry.push_back(owned.object);
				}
				else if (isRebel && owned.hasCapturePower)
				{
					availableRebelFallback.push_back(owned.object);
				}
			}
			std::stable_sort(availableRpgInfantry.begin(), availableRpgInfantry.end(), [](Object* a, Object* b) -> bool
			{
				const ThingTemplate* ta = a != nullptr ? a->getTemplate() : nullptr;
				const ThingTemplate* tb = b != nullptr ? b->getTemplate() : nullptr;
				const std::string an = ta != nullptr ? ta->getName().str() : "";
				const std::string bn = tb != nullptr ? tb->getName().str() : "";
				const int ap = containsIgnoreCase(an, "tunneldefender") ? 0 : (containsIgnoreCase(an, "rpg") ? 1 : 2);
				const int bp = containsIgnoreCase(bn, "tunneldefender") ? 0 : (containsIgnoreCase(bn, "rpg") ? 1 : 2);
				return ap < bp;
			});

			const int infantryReserve = 2;
			int structuresSeen = 0;
			int structuresSelected = 0;
			int structuresFilled = 0;
			int desiredInfantryTotal = 0;
			int assignedThisTick = 0;
			std::string policyReason = usefulZones.empty() ? "no_useful_zones" : "no_candidate";
			const Real zoneRadiusSq = std::max<Real>(220.0f, m_autonomy.state.zoneRadius * 1.35f) *
				std::max<Real>(220.0f, m_autonomy.state.zoneRadius * 1.35f);

			struct GarrisonCandidate
			{
				Object* structure = nullptr;
				UnsignedInt structureId = 0;
				std::string templateName;
				GarrisonZone zone;
				Real x = 0.0f;
				Real y = 0.0f;
				AIControlAdapterGarrisonCandidateDecision decision;
			};
			std::vector<GarrisonCandidate> candidates;
			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_STRUCTURE))
				{
					continue;
				}
				const Coord3D* pos = obj->getPosition();
				if (pos == nullptr)
				{
					continue;
				}
				const GarrisonDiscoveryResult discovery = discoverGarrisonStructure(player, obj);
				const ThingTemplate* tt = obj->getTemplate();
				const std::string templateName = tt != nullptr ? tt->getName().str() : "";
				if (shouldLogGarrisonDiscovery(player, obj, discovery, now))
				{
					adapterLog(
						"garrison_discovery object=%u template=%s x=%.1f y=%.1f kind_flag=%d contain=%s ui_enterable=%d map_cache=%d accepted=%d reason=%s",
						static_cast<unsigned int>(obj->getID()),
						templateName.c_str(),
						pos->x,
						pos->y,
						discovery.kindFlag ? 1 : 0,
						discovery.containName.c_str(),
						discovery.uiEnterable ? 1 : 0,
						discovery.mapCacheGarrison ? 1 : 0,
						discovery.accepted ? 1 : 0,
						discovery.reason.c_str());
				}
				if (!discovery.accepted)
				{
					continue;
				}

				const GarrisonZone* nearestZone = nullptr;
				Real nearestDistSq = zoneRadiusSq;
				for (const GarrisonZone& zone : usefulZones)
				{
					const Real dx = pos->x - zone.x;
					const Real dy = pos->y - zone.y;
					const Real distSq = dx * dx + dy * dy;
					if (distSq <= nearestDistSq)
					{
						nearestDistSq = distSq;
						nearestZone = &zone;
					}
				}
				if (nearestZone == nullptr)
				{
					adapterLog(
						"garrison_candidate structure=%u template=%s zone=0 x=%.1f y=%.1f capacity=unknown score=0 reason=too_far",
						static_cast<unsigned int>(obj->getID()),
						templateName.c_str(),
						pos->x,
						pos->y);
					m_autonomy.state.garrisonTelemetry.push_back(nlohmann::json::object({
						{"structure_id", static_cast<unsigned int>(obj->getID())},
						{"template", templateName},
						{"zone_id", 0u},
						{"position", nlohmann::json::object({ {"x", pos->x}, {"y", pos->y}, {"z", pos->z} })},
						{"desired_infantry", 0},
						{"capacity", discovery.capacity > 0 ? discovery.capacity : 0},
						{"engine_capacity", discovery.capacity},
						{"assigned_infantry", 0},
						{"entered", 0},
						{"entered_infantry", 0},
						{"selected", false},
						{"discovered", true},
						{"source", discovery.reason},
						{"discovery_reason", discovery.reason},
						{"kind_flag", discovery.kindFlag},
						{"contain", discovery.containName},
						{"ui_enterable", discovery.uiEnterable},
						{"map_cache", discovery.mapCacheGarrison},
						{"score", 0},
						{"state", "available"},
						{"reason", "too_far"}
					}));
					continue;
				}

				++structuresSeen;
				const bool palace = isPalaceTemplateName(templateName);
				Player* owner = obj->getControllingPlayer();
				const bool friendlyOwned = owner == player;
				const bool enemyOwned = isEnemyControlledObject(player, obj);
				const bool neutralOwned = !friendlyOwned && !enemyOwned && !palace;
				const UnsignedInt structureId = static_cast<UnsignedInt>(obj->getID());
				const auto existing = m_autonomy.state.garrisonAssignments.find(structureId);
				const int existingEntered = existing != m_autonomy.state.garrisonAssignments.end()
					? countEnteredGarrisonInfantry(existing->second)
					: 0;
				const int existingAssigned = existing != m_autonomy.state.garrisonAssignments.end()
					? static_cast<int>(existing->second.infantry.size())
					: 0;
				const auto threatIt = m_autonomy.state.zoneThreats.find(nearestZone->anchorId);
				const bool repeatedAttack = threatIt != m_autonomy.state.zoneThreats.end()
					&& (now - threatIt->second.lastSeenTick) <= 60000u;
				AIControlAdapterGarrisonCandidateDecision decision = AIControlAdapterEvaluateGarrisonCandidate({
					palace,
					!palace,
					friendlyOwned,
					neutralOwned,
					enemyOwned,
					nearestZone->useful,
					nearestZone->active,
					nearestZone->developed,
					repeatedAttack,
					isNearArtilleryPlatform(pos->x, pos->y, 650.0f),
					isNearGarrisonFeature(pos->x, pos->y, "entrance", 700.0f) || isNearGarrisonFeature(pos->x, pos->y, "lane", 420.0f),
					std::sqrt(nearestDistSq),
					std::sqrt(zoneRadiusSq),
					existingEntered,
					discovery.containGarrison || discovery.uiEnterable,
					discovery.mapCacheGarrison
				});
				if (decision.desiredInfantry > 0)
				{
					desiredInfantryTotal += decision.desiredInfantry;
				}
				adapterLog(
					"garrison_candidate structure=%u template=%s zone=%u x=%.1f y=%.1f capacity=%d score=%d reason=%s",
					structureId,
					templateName.c_str(),
					nearestZone->anchorId,
					pos->x,
					pos->y,
					decision.capacity,
					decision.score,
					decision.reason);
				m_autonomy.state.garrisonTelemetry.push_back(nlohmann::json::object({
					{"structure_id", structureId},
					{"template", templateName},
					{"zone_id", nearestZone->anchorId},
					{"position", nlohmann::json::object({ {"x", pos->x}, {"y", pos->y}, {"z", pos->z} })},
					{"desired_infantry", decision.desiredInfantry},
					{"capacity", decision.capacity},
					{"assigned_infantry", existingAssigned},
					{"entered", existingEntered},
					{"entered_infantry", existingEntered},
					{"selected", decision.selected},
					{"discovered", true},
					{"source", discovery.reason},
					{"discovery_reason", discovery.reason},
					{"kind_flag", discovery.kindFlag},
					{"contain", discovery.containName},
					{"ui_enterable", discovery.uiEnterable},
					{"map_cache", discovery.mapCacheGarrison},
					{"engine_capacity", discovery.capacity},
					{"score", decision.score},
					{"state", existingEntered >= decision.desiredInfantry && decision.desiredInfantry > 0 ? "filled" : "candidate"},
					{"reason", decision.reason}
				}));
				if (!decision.selected)
				{
					policyReason = decision.reason;
					continue;
				}
				++structuresSelected;
				if (existingEntered >= decision.desiredInfantry)
				{
					++structuresFilled;
					continue;
				}
				GarrisonCandidate candidate;
				candidate.structure = obj;
				candidate.structureId = structureId;
				candidate.templateName = templateName;
				candidate.zone = *nearestZone;
				candidate.x = pos->x;
				candidate.y = pos->y;
				candidate.decision = decision;
				candidates.push_back(candidate);
			}

			std::stable_sort(candidates.begin(), candidates.end(), [](const GarrisonCandidate& a, const GarrisonCandidate& b) -> bool
			{
				return a.decision.score > b.decision.score;
			});

			const Money* wallet = player->getMoney();
			const UnsignedInt money = wallet != nullptr ? wallet->countMoney() : 0u;
			const AIControlAdapterProfilePolicyManager profilePolicyManager;
			const UnsignedInt reserveCash =
				profilePolicyManager.ResolveReserveCashWithFloor(resolveAutonomyProfilePolicyConfig(), 5000u);
			int assignedGarrisonInfantry = 0;
			int enteredGarrisonInfantry = 0;
			for (const auto& pair : m_autonomy.state.garrisonAssignments)
			{
				assignedGarrisonInfantry += static_cast<int>(pair.second.infantry.size());
				enteredGarrisonInfantry += countEnteredGarrisonInfantry(pair.second);
			}
			int activeScoutAssignments = m_autonomy.combatTaskManager.getScoutAssignedUnitCount();
			int activeRaidAssignments = 0;
			int activeDefenseAssignments = 0;
			const std::vector<CombatTask*> activeTasks = m_autonomy.combatTaskManager.findActiveTasks();
			for (const CombatTask* task : activeTasks)
			{
				if (task == nullptr)
				{
					continue;
				}
				if (task->type == CombatTaskType::Attack)
				{
					activeRaidAssignments += static_cast<int>(task->assignedUnitIds.size());
				}
				else if (task->type == CombatTaskType::Defense)
				{
					activeDefenseAssignments += static_cast<int>(task->assignedUnitIds.size());
				}
			}
			const bool criticalEmergency =
				(m_autonomy.state.emergencySurvivalTelemetry.is_object() && m_autonomy.state.emergencySurvivalTelemetry.value("active", false)) ||
				(m_autonomy.state.mainBaseCriticalOverrideTelemetry.is_object() && m_autonomy.state.mainBaseCriticalOverrideTelemetry.value("active", false));
			const AIControlAdapterGarrisonThroughputDecision throughputDecision = AIControlAdapterEvaluateGarrisonThroughput({
				money,
				reserveCash,
				barracksReady,
				structuresSelected,
				structuresFilled,
				desiredInfantryTotal,
				assignedGarrisonInfantry,
				enteredGarrisonInfantry,
				static_cast<int>(availableRpgInfantry.size()),
				infantryReserve,
				activeScoutAssignments,
				activeRaidAssignments,
				activeDefenseAssignments,
				criticalEmergency
			});
			int structuresAssignedThisCycle = 0;

			for (const GarrisonCandidate& candidate : candidates)
			{
				if (structuresAssignedThisCycle >= throughputDecision.maxAssignmentsThisCycle)
				{
					policyReason = throughputDecision.maxAssignmentsThisCycle <= 1 ? "normal_assignment_budget" : "assignment_budget_reached";
					continue;
				}
				const auto existing = m_autonomy.state.garrisonAssignments.find(candidate.structureId);
				const int existingAssigned = existing != m_autonomy.state.garrisonAssignments.end()
					? static_cast<int>(existing->second.infantry.size())
					: 0;
				const int assignable = std::max<int>(0, static_cast<int>(availableRpgInfantry.size()) - throughputDecision.infantryReserveHeld);
				const int missing = candidate.decision.desiredInfantry - existingAssigned;
				int toAssign = std::min<int>(missing, std::min<int>(4, assignable));
				bool usingRebelFallback = false;
				if (toAssign <= 0 && !availableRebelFallback.empty())
				{
					const bool highPressure = candidate.zone.active ||
						m_autonomy.state.zoneThreats.find(candidate.zone.anchorId) != m_autonomy.state.zoneThreats.end();
					const int rebelCaptureReserve = 2;
					const int rebelAssignable = std::max<int>(0, static_cast<int>(availableRebelFallback.size()) - rebelCaptureReserve);
					if (highPressure && rebelAssignable > 0)
					{
						toAssign = std::min<int>(missing, std::min<int>(1, rebelAssignable));
						usingRebelFallback = true;
					}
				}
				if (toAssign <= 0)
				{
					policyReason = "infantry_reserved";
					continue;
				}

				std::vector<Object*> selectedInfantry;
				std::vector<unsigned int> selectedIds;
				for (int i = 0; i < toAssign; ++i)
				{
					Object* unit = usingRebelFallback
						? availableRebelFallback[static_cast<std::size_t>(i)]
						: availableRpgInfantry[static_cast<std::size_t>(i)];
					selectedInfantry.push_back(unit);
					selectedIds.push_back(static_cast<unsigned int>(unit->getID()));
				}

				std::string commandReason;
				const bool issued = issueGarrisonCommand(player, candidate.structure, selectedInfantry, commandReason);
				std::vector<unsigned int> assignmentIds = selectedIds;
				AutonomyGarrisonAssignment assignment;
				if (existing != m_autonomy.state.garrisonAssignments.end())
				{
					assignment = existing->second;
					assignmentIds = existing->second.infantryIds;
					assignmentIds.insert(assignmentIds.end(), selectedIds.begin(), selectedIds.end());
					if (assignment.taskId != 0u)
					{
						m_autonomy.combatTaskManager.expireTask(assignment.taskId, "reinforced");
					}
				}
				assignment.structureId = candidate.structureId;
				assignment.zoneAnchorId = candidate.zone.anchorId;
				assignment.templateName = candidate.templateName;
				assignment.x = candidate.x;
				assignment.y = candidate.y;
				assignment.desiredInfantry = candidate.decision.desiredInfantry;
				assignment.estimatedCapacity = candidate.decision.capacity;
				assignment.infantryIds = assignmentIds;
				if (assignment.assignedTick == 0u)
				{
					assignment.assignedTick = now;
				}
				assignment.lastCommandTick = now;
				if (assignment.lastProgressTick == 0u)
				{
					assignment.lastProgressTick = now;
				}
				for (Object* selectedUnit : selectedInfantry)
				{
					if (selectedUnit == nullptr)
					{
						continue;
					}
					AutonomyGarrisonInfantryAssignment unit;
					unit.unitId = static_cast<UnsignedInt>(selectedUnit->getID());
					unit.templateName = templateNameForObject(selectedUnit);
					const Coord3D* unitPos = selectedUnit->getPosition();
					if (unitPos != nullptr)
					{
						unit.lastX = unitPos->x;
						unit.lastY = unitPos->y;
						const Real dx = unitPos->x - candidate.x;
						const Real dy = unitPos->y - candidate.y;
						unit.lastDistance = std::sqrt((dx * dx) + (dy * dy));
					}
					unit.assignedTick = now;
					unit.lastCommandTick = now;
					unit.lastProgressTick = now;
					unit.state = "assigned";
					unit.reason = "enter_command_issued";
					assignment.infantry.push_back(unit);
				}
				assignment.state = issued ? "assigned" : "failed";
				assignment.reason = issued ? (usingRebelFallback ? "high_pressure_rebel_fallback" : "enter_command_issued") : commandReason;
				if (issued)
				{
					assignment.taskId = m_autonomy.combatTaskManager.createTask(
						CombatTaskType::Guard,
						assignmentIds,
						Coord3D{ candidate.x, candidate.y, 0.0f },
						"garrison",
						"garrison_" + std::to_string(candidate.structureId),
						300000);
					m_autonomy.combatTaskManager.updateTaskState(assignment.taskId, CombatTaskState::Moving, "enter_command_issued");
					m_autonomy.combatTaskManager.updateTaskCommand(assignment.taskId, now);
					m_autonomy.state.garrisonAssignments[candidate.structureId] = assignment;
					assignedThisTick += toAssign;
					++structuresAssignedThisCycle;
					policyReason = "assigned";
					if (usingRebelFallback)
					{
						availableRebelFallback.erase(availableRebelFallback.begin(), availableRebelFallback.begin() + toAssign);
					}
					else
					{
						availableRpgInfantry.erase(availableRpgInfantry.begin(), availableRpgInfantry.begin() + toAssign);
					}
				}
				else
				{
					policyReason = commandReason;
				}
				adapterLog(
					"garrison_assignment structure=%u template=%s zone=%u infantry=%d state=%s reason=%s",
					candidate.structureId,
					candidate.templateName.c_str(),
					candidate.zone.anchorId,
					toAssign,
					assignment.state.c_str(),
					assignment.reason.c_str());
				m_autonomy.state.garrisonTelemetry.push_back(nlohmann::json::object({
					{"structure_id", candidate.structureId},
					{"template", candidate.templateName},
					{"zone_id", candidate.zone.anchorId},
					{"position", nlohmann::json::object({ {"x", candidate.x}, {"y", candidate.y}, {"z", 0.0f} })},
					{"desired_infantry", candidate.decision.desiredInfantry},
					{"capacity", candidate.decision.capacity},
					{"assigned_infantry", toAssign},
					{"assigned_infantry_ids", selectedIds},
					{"entered", 0},
					{"state", assignment.state},
					{"reason", assignment.reason}
				}));
			}

			assignedGarrisonInfantry += assignedThisTick;
			const AIControlAdapterGarrisonProductionDecision productionDecision = AIControlAdapterChooseGarrisonProduction({
				desiredInfantryTotal,
				assignedGarrisonInfantry,
				static_cast<int>(availableRpgInfantry.size()),
				queuedGarrisonInfantry,
				barracksReady > 0,
				money,
				reserveCash
			});
			int producerId = firstBarracks != nullptr ? static_cast<int>(firstBarracks->getID()) : -1;
			bool productionIssued = false;
			std::string productionReason = productionDecision.reason;
			int productionIssuedCount = 0;
			const int productionFanout = productionDecision.productionNeeded
				? std::max(1, throughputDecision.productionFanout)
				: 0;
			if (productionDecision.productionNeeded && firstBarracks != nullptr)
			{
				const int producerLimit = std::min<int>(productionFanout, static_cast<int>(readyBarracks.size()));
				for (int producerIdx = 0; producerIdx < producerLimit; ++producerIdx)
				{
					Object* producer = readyBarracks[static_cast<std::size_t>(producerIdx)];
					if (producer == nullptr)
					{
						continue;
					}
					const int currentProducerId = static_cast<int>(producer->getID());
					const std::string unitTemplate = inferRpgTemplateForProducer(producer);
					nlohmann::json message = {
						{"type", "SessionCommand"},
						{"request_id", "garrison_tunnel_defender"},
						{"cmd", "Game.QueueUnit"},
						{"args", nlohmann::json::object({
							{"producer_kind", "barracks"},
							{"unit_template", unitTemplate.empty() ? std::string(productionDecision.unitTemplate) : unitTemplate},
							{"producer_object_id", currentProducerId}
						})}
					};
					if (m_autonomy.state.hasExplicitPlayerIndex)
					{
						message["args"]["player_index"] = m_autonomy.state.playerIndex;
					}
					std::string issueReason = productionDecision.reason;
					const bool issued = executeGameQueueUnit(message, issueReason);
					if (issued)
					{
						++productionIssuedCount;
						productionIssued = true;
						productionReason = productionDecision.reason;
					}
					else if (!productionIssued)
					{
						productionReason = issueReason;
					}
				}
			}
			adapterLog(
				"garrison_production_policy zone=%u desired=%d available=%d queued=%d producer=%d issued=%d reason=%s",
				usefulZones.empty() ? 0u : usefulZones.front().anchorId,
				desiredInfantryTotal,
				static_cast<int>(availableRpgInfantry.size()),
				queuedGarrisonInfantry,
				producerId,
				productionIssued ? 1 : 0,
				productionIssued ? productionDecision.reason : productionReason.c_str());
			adapterLog(
				"garrison_production_fanout barracks_ready=%d issued=%d desired_gap=%d reason=%s",
				barracksReady,
				productionIssuedCount,
				std::max(0, desiredInfantryTotal - assignedGarrisonInfantry - static_cast<int>(availableRpgInfantry.size()) - queuedGarrisonInfantry),
				productionIssued ? throughputDecision.reason : productionReason.c_str());
			adapterLog(
				"garrison_throughput_policy mode=%s selected=%d filled=%d desired=%d assigned=%d max_assign=%d reserve=%d reason=%s",
				throughputDecision.mode,
				structuresSelected,
				structuresFilled,
				desiredInfantryTotal,
				assignedThisTick,
				throughputDecision.maxAssignmentsThisCycle,
				throughputDecision.infantryReserveHeld,
				throughputDecision.reason);

			adapterLog(
				"garrison_policy zone=%u structures=%d selected=%d filled=%d desired_infantry=%d assigned=%d available_infantry=%d reason=%s",
				usefulZones.empty() ? 0u : usefulZones.front().anchorId,
				structuresSeen,
				structuresSelected,
				structuresFilled,
				desiredInfantryTotal,
				assignedThisTick,
				static_cast<int>(availableRpgInfantry.size()),
				policyReason.c_str());
			m_autonomy.state.garrisonTelemetry.push_back(nlohmann::json::object({
				{"type", "garrison_throughput"},
				{"selected_structures", structuresSelected},
				{"filled_structures", structuresFilled},
				{"desired_infantry", desiredInfantryTotal},
				{"entered_infantry", enteredGarrisonInfantry},
				{"assigned_this_cycle", assignedThisTick},
				{"max_assignments_this_cycle", throughputDecision.maxAssignmentsThisCycle},
				{"assignment_budget_reason", throughputDecision.reason},
				{"mode", throughputDecision.mode},
				{"infantry_reserve_held", throughputDecision.infantryReserveHeld},
				{"production_requested_per_barracks", productionFanout},
				{"production_issued", productionIssuedCount}
			}));
		}

		#include "AIControlAdapterProtocol.inl"

		void drawDebugZones()
		{
			static int logCount = 0;
			if (!m_autonomy.state.debugDrawEnabled)
			{
				if (logCount++ % 300 == 0) // Log every ~5 seconds at 60fps
				{
					adapterLog("debug_draw_disabled count=%d", logCount);
				}
				return;
			}
			adapterLog("debug_draw_active zones_check");
			if (TheWindowManager == nullptr)
			{
				adapterLog("debug_draw_skip reason=no_window_manager");
				return;
			}
			if (TheTacticalView == nullptr)
			{
				adapterLog("debug_draw_skip reason=no_tactical_view");
				return;
			}

			Player* player = nullptr;
			if (m_autonomy.state.hasExplicitPlayerIndex)
			{
				player = getPlayerByIndex(m_autonomy.state.playerIndex);
			}
			else if (ThePlayerList != nullptr)
			{
				player = ThePlayerList->getLocalPlayer();
			}
			if (player == nullptr)
			{
				return;
			}

			// Rebuild compact debug overlay anchors from current owned structures.
			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			std::vector<DebugZoneAnchorCandidate> zoneCandidates;
			for (std::size_t ownedIdx = 0; ownedIdx < ownedObjects.size(); ++ownedIdx)
			{
				const AutomationOwnedObjectSnapshot& owned = ownedObjects[ownedIdx];
				const Coord3D* anchorPos = owned.object != nullptr ? owned.object->getPosition() : nullptr;
				if (anchorPos == nullptr)
				{
					continue;
				}

				DebugZoneAnchorCandidate candidate;
				candidate.anchorId = owned.object->getID();
				candidate.x = anchorPos->x;
				candidate.y = anchorPos->y;
				candidate.name = owned.name;
				candidate.isStructure = owned.isStructure;
				candidate.underConstruction = owned.underConstruction;
				candidate.isSupplyStructure = owned.isSupplyStructure;
				zoneCandidates.push_back(candidate);
			}

			const std::vector<DebugZoneAnchor> zones =
				AIControlAdapterZoneManager::BuildDebugOverlayZones(zoneCandidates, 200.0f);

			if (zones.empty())
			{
				adapterLog("debug_draw_skip reason=no_zones");
				return;
			}

			adapterLog("debug_draw zones=%d radius=%.1f", static_cast<int>(zones.size()), m_autonomy.state.zoneRadius);

			const Real zoneRadius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
			const Int numSegments = 32; // Circle segments for smoothness
			const Real angleStep = (2.0f * 3.14159265f) / static_cast<Real>(numSegments);

			// Draw each zone
			for (std::size_t i = 0; i < zones.size(); ++i)
			{
				const DebugZoneAnchor& zone = zones[i];
				Coord3D zoneCenter;
				zoneCenter.x = zone.x;
				zoneCenter.y = zone.y;
				zoneCenter.z = 0.0f;

				// Color: green for main base, cyan for expansion zones
				Color zoneColor = zone.isMainBase
					? TheWindowManager->winMakeColor(0, 255, 0, 200)    // Green
					: TheWindowManager->winMakeColor(0, 200, 255, 180); // Cyan

				// Draw circle as line segments
				for (Int seg = 0; seg < numSegments; ++seg)
				{
					const Real angle1 = static_cast<Real>(seg) * angleStep;
					const Real angle2 = static_cast<Real>(seg + 1) * angleStep;

					Coord3D worldPos1 = zoneCenter;
					worldPos1.x += zoneRadius * std::cos(angle1);
					worldPos1.y += zoneRadius * std::sin(angle1);

					Coord3D worldPos2 = zoneCenter;
					worldPos2.x += zoneRadius * std::cos(angle2);
					worldPos2.y += zoneRadius * std::sin(angle2);

					ICoord2D screenPos1;
					ICoord2D screenPos2;

					if (TheTacticalView->worldToScreen(&worldPos1, &screenPos1) &&
						TheTacticalView->worldToScreen(&worldPos2, &screenPos2))
					{
						TheWindowManager->winDrawLine(
							zoneColor,
							2.0f,
							screenPos1.x,
							screenPos1.y,
							screenPos2.x,
							screenPos2.y);
					}
				}

				// Draw center marker (crosshair)
				Coord3D centerPos = zoneCenter;
				const Real markerSize = zoneRadius * 0.15f;

				Coord3D markerLeft = centerPos;
				markerLeft.x -= markerSize;
				Coord3D markerRight = centerPos;
				markerRight.x += markerSize;
				Coord3D markerTop = centerPos;
				markerTop.y -= markerSize;
				Coord3D markerBottom = centerPos;
				markerBottom.y += markerSize;

				ICoord2D screenLeft, screenRight, screenTop, screenBottom;
				if (TheTacticalView->worldToScreen(&markerLeft, &screenLeft) &&
					TheTacticalView->worldToScreen(&markerRight, &screenRight))
				{
					TheWindowManager->winDrawLine(zoneColor, 3.0f, screenLeft.x, screenLeft.y, screenRight.x, screenRight.y);
				}
				if (TheTacticalView->worldToScreen(&markerTop, &screenTop) &&
					TheTacticalView->worldToScreen(&markerBottom, &screenBottom))
				{
					TheWindowManager->winDrawLine(zoneColor, 3.0f, screenTop.x, screenTop.y, screenBottom.x, screenBottom.y);
				}
			}
		}

		void evaluateAutomationRules()
		{
			evaluateAutonomyMacro();
			evaluateStashWorkerAutomationRule();
			evaluateLocalWorkerLiquidity();
			evaluateWorkerAutomationRule();
			evaluateRadarVanAutomationRule();
			evaluateAttackAutomationRule();
			evaluateCaptureAutomationRule();

			// Phase 6.1: Update special task reservations and construction lifecycle
			Player* player = resolveAutonomyPlayer();
			updateSpecialTaskReservations(player);

			// Phase 9.0: Update combat task lifecycle
			updateCombatTaskLifecycle(player);

			// Phase 7.4: Update WMD target tracking
			updateWMDTargets(player);

			// Phase 8.1: Update structured enemy map memory for telemetry/UI.
			updateEnemyMemory(player);

			// Phase 9.2: Dedicated fast scouting refreshes map memory while raids wait.
			evaluateDedicatedScouting(player);

			// Phase 7.9: Bounded battlefield counterbattery for visible long-range artillery.
			evaluateBattlefieldCounterbattery(player);

			// Phase 7.9: Keep durable defensive garrisons reserved from unrelated automation.
			evaluateGarrisonManagement(player);

			// Phase 7.5: Mobile SCUD Launcher counterbattery is deprecated.
			// Trucks remain in the normal vehicle mix; stationary SCUD Storms handle WMD counterplay.

			// Phase 7.5: Evaluate defensive SCUD Storm construction and firing
			evaluateDefensiveScudStorm(player);

			drawDebugZones();
		}

		// Phase 9.0: Update combat task lifecycle - prune dead units, fail/complete/expire tasks
		int countFreshScudStrategicTargets(DWORD now) const
		{
			int freshTargets = 0;
			for (const EnemyMemoryItem& item : m_autonomy.enemyMemory.getItems())
			{
				if (!item.isStructure || item.stale || item.lastSeenTick == 0u)
				{
					continue;
				}
				const DWORD ageMs = now - item.lastSeenTick;
				if (ageMs > 120000u)
				{
					continue;
				}
				if (item.kind == EnemyMemoryKind::Wmd ||
					item.kind == EnemyMemoryKind::Production ||
					item.kind == EnemyMemoryKind::Economy ||
					item.kind == EnemyMemoryKind::Defense ||
					item.kind == EnemyMemoryKind::BaseCommand)
				{
					++freshTargets;
				}
			}
			return freshTargets;
		}

		bool isUnitProtectedByZoneDefenseFloor(Object* unit) const
		{
			if (unit == nullptr || unit->getPosition() == nullptr ||
				!m_autonomy.state.telemetryZones.is_array() ||
				m_autonomy.state.zoneDefenseReserves.empty())
			{
				return false;
			}

			const Coord3D* pos = unit->getPosition();
			std::vector<AIControlAdapterDefenseZoneSnapshot> zones;
			for (const auto& zone : m_autonomy.state.telemetryZones)
			{
				if (!zone.is_object())
				{
					continue;
				}
				AIControlAdapterDefenseZoneSnapshot snapshot;
				snapshot.anchorId = zone.value("anchor_id", 0u);
				snapshot.centerX = zone.value("center_x", 0.0f);
				snapshot.centerY = zone.value("center_y", 0.0f);
				snapshot.isMainBase = zone.value("is_main_base", false) || zone.value("main_base", false);
				zones.push_back(snapshot);
			}
			std::vector<AIControlAdapterZoneDefenseReserveSnapshot> reserves;
			for (const auto& reservePair : m_autonomy.state.zoneDefenseReserves)
			{
				AIControlAdapterZoneDefenseReserveSnapshot snapshot;
				snapshot.zoneId = reservePair.first;
				snapshot.surplus = reservePair.second.surplus;
				snapshot.deficit = reservePair.second.deficit;
				snapshot.activeThreat = reservePair.second.activeThreat;
				reserves.push_back(snapshot);
			}
			return AIControlAdapterDefenseManager::ResolveZoneDefenseFloor(
				pos->x,
				pos->y,
				m_autonomy.state.zoneRadius,
				zones,
				reserves).protectedByFloor;
		}

		bool canRelaxScoutDefenseFloorForUnit(Object* unit) const
		{
			if (unit == nullptr || unit->getPosition() == nullptr ||
				!m_autonomy.state.telemetryZones.is_array() ||
				m_autonomy.state.zoneDefenseReserves.empty())
			{
				return false;
			}

			const Coord3D* pos = unit->getPosition();
			std::vector<AIControlAdapterDefenseZoneSnapshot> zones;
			for (const auto& zone : m_autonomy.state.telemetryZones)
			{
				if (!zone.is_object())
				{
					continue;
				}
				AIControlAdapterDefenseZoneSnapshot snapshot;
				snapshot.anchorId = zone.value("anchor_id", 0u);
				snapshot.centerX = zone.value("center_x", 0.0f);
				snapshot.centerY = zone.value("center_y", 0.0f);
				snapshot.isMainBase = zone.value("is_main_base", false) || zone.value("main_base", false);
				zones.push_back(snapshot);
			}
			std::vector<AIControlAdapterZoneDefenseReserveSnapshot> reserves;
			for (const auto& reservePair : m_autonomy.state.zoneDefenseReserves)
			{
				AIControlAdapterZoneDefenseReserveSnapshot snapshot;
				snapshot.zoneId = reservePair.first;
				snapshot.surplus = reservePair.second.surplus;
				snapshot.deficit = reservePair.second.deficit;
				snapshot.activeThreat = reservePair.second.activeThreat;
				reserves.push_back(snapshot);
			}
			return AIControlAdapterDefenseManager::ResolveZoneDefenseFloor(
				pos->x,
				pos->y,
				m_autonomy.state.zoneRadius,
				zones,
				reserves).canRelaxForScout;
		}

		std::vector<unsigned int> selectRaidProbeUnitIds(const CombatTask& task, const CombatTaskWaypoint& waypoint)
		{
			std::vector<AIControlAdapterRaidProbeUnitSnapshot> units;
			for (unsigned int unitId : task.assignedUnitIds)
			{
				Object* unit = TheGameLogic != nullptr ? TheGameLogic->findObjectByID(static_cast<ObjectID>(unitId)) : nullptr;
				if (unit == nullptr)
				{
					continue;
				}
				const Coord3D* pos = unit->getPosition();
				const ThingTemplate* tt = unit->getTemplate();
				AIControlAdapterRaidProbeUnitSnapshot snapshot;
				snapshot.unitId = unitId;
				snapshot.templateName = tt != nullptr ? tt->getName().str() : "";
				snapshot.alive = !unit->isEffectivelyDead();
				snapshot.vehicle = unit->isKindOf(KINDOF_VEHICLE);
				snapshot.aircraft = unit->isKindOf(KINDOF_AIRCRAFT);
				snapshot.ableToAttack = unit->isAbleToAttack();
				snapshot.underConstruction = unit->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
				snapshot.worker = unit->isKindOf(KINDOF_DOZER);
				snapshot.harvester = unit->isKindOf(KINDOF_HARVESTER);
				snapshot.taskReserved = m_autonomy.taskReservationManager.isObjectReserved(unitId);
				snapshot.zoneDefenseFloorReserved = isUnitProtectedByZoneDefenseFloor(unit);
				if (pos != nullptr)
				{
					snapshot.x = pos->x;
					snapshot.y = pos->y;
					snapshot.hasPosition = true;
				}
				units.push_back(snapshot);
			}
			return AIControlAdapterRaidManager::selectProbeUnitIds(units, waypoint, 3, 1200.0f);
		}

		Coord3D computeRaidProbeTarget(const CombatTask& task, int waypointIndex)
		{
			return AIControlAdapterRaidManager::computeProbeTarget(
				task.waypoints,
				task.targetPosition,
				waypointIndex,
				1200.0f);
		}

		bool issueRaidProbeCommand(Player* player, CombatTask& task, const std::vector<unsigned int>& probeUnits, const Coord3D& target, const std::string& reason)
		{
			if (player == nullptr || probeUnits.empty())
			{
				return false;
			}
			nlohmann::json objectIds = nlohmann::json::array();
			for (unsigned int unitId : probeUnits)
			{
				objectIds.push_back(static_cast<Int>(unitId));
			}
			nlohmann::json args = nlohmann::json::object({
				{"x", target.x},
				{"y", target.y},
				{"object_ids", objectIds}
			});
			if (m_autonomy.state.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_autonomy.state.playerIndex;
			}
			nlohmann::json message = {
				{"type", "SessionCommand"},
				{"request_id", std::string("raid_probe")},
				{"cmd", "Game.AttackMove"},
				{"args", args}
			};
			std::string commandReason;
			const bool ok = executeGameAttackMove(message, commandReason);
			const DWORD now = ::GetTickCount();
			adapterLog(
				"raid_probe_launch task=%u units=%d waypoint=%d x=%.1f y=%.1f reason=%s",
				task.taskId,
				static_cast<int>(probeUnits.size()),
				task.currentWaypointIndex,
				target.x,
				target.y,
				ok ? reason.c_str() : commandReason.c_str());
			if (!ok)
			{
				return false;
			}
			task.probeState = CombatTaskProbeState::Moving;
			task.probeUnitIds = probeUnits;
			task.probeStartedTick = now;
			task.lastProbeCommandTick = now;
			task.probeReason = reason;
			return true;
		}

		Real raidProbeMaxDistanceFromPoint(const CombatTask& task, const Coord3D& point) const
		{
			Real maxDistance = 0.0f;
			for (unsigned int unitId : task.probeUnitIds)
			{
				Object* unit = TheGameLogic != nullptr ? TheGameLogic->findObjectByID(static_cast<ObjectID>(unitId)) : nullptr;
				const Coord3D* pos = unit != nullptr ? unit->getPosition() : nullptr;
				if (unit == nullptr || unit->isEffectivelyDead() || pos == nullptr)
				{
					continue;
				}
				const Real dx = pos->x - point.x;
				const Real dy = pos->y - point.y;
				maxDistance = std::max<Real>(maxDistance, std::sqrt(dx * dx + dy * dy));
			}
			return maxDistance;
		}

		void finishRaidProbe(CombatTask& task, CombatTaskProbeState state, const std::string& reason, DWORD now, Real distance)
		{
			if (task.probeState == CombatTaskProbeState::Inactive && task.probeUnitIds.empty())
			{
				return;
			}
			task.probeState = state;
			task.probeReason = reason;
			task.lastProbeEndTick = now;
			adapterLog(
				"raid_probe_state task=%u state=%s units=%d distance=%.1f fresh_targets=%d reason=%s",
				task.taskId,
				combatTaskProbeStateName(state),
				static_cast<int>(task.probeUnitIds.size()),
				distance,
				task.freshStrategicTargets,
				reason.c_str());
			task.probeUnitIds.clear();
			task.probeStartedTick = 0u;
		}

		bool issueRaidWaypointCommand(Player* player, CombatTask& task, const std::string& reason)
		{
			if (player == nullptr || task.assignedUnitIds.empty() || task.waypoints.empty())
			{
				return false;
			}
			const int waypointIndex = std::max(0, std::min(task.currentWaypointIndex, static_cast<int>(task.waypoints.size()) - 1));
			const CombatTaskWaypoint& waypoint = task.waypoints[waypointIndex];
			nlohmann::json objectIds = nlohmann::json::array();
			for (unsigned int unitId : task.assignedUnitIds)
			{
				objectIds.push_back(static_cast<Int>(unitId));
			}
			nlohmann::json args = nlohmann::json::object({
				{"x", waypoint.position.x},
				{"y", waypoint.position.y},
				{"object_ids", objectIds}
			});
			if (m_autonomy.state.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_autonomy.state.playerIndex;
			}
			nlohmann::json message = {
				{"type", "SessionCommand"},
				{"request_id", std::string("raid_waypoint")},
				{"cmd", "Game.AttackMove"},
				{"args", args}
			};
			std::string commandReason;
			const bool ok = executeGameAttackMove(message, commandReason);
			const DWORD now = ::GetTickCount();
			if (ok)
			{
				m_autonomy.combatTaskManager.updateTaskCommand(task.taskId, now);
				m_autonomy.combatTaskManager.updateTaskState(task.taskId, CombatTaskState::MovingToStage, reason);
				task.currentWaypointStartTick = now;
				task.cohesionReason = reason;
			}
			adapterLog(
				"raid_waypoint task=%u action=command waypoint=%d x=%.1f y=%.1f radius=%.1f issued=%d reason=%s",
				task.taskId,
				waypointIndex,
				waypoint.position.x,
				waypoint.position.y,
				waypoint.radius,
				ok ? 1 : 0,
				ok ? reason.c_str() : commandReason.c_str());
			adapterLog(
				"combat_task_command task=%u type=attack command=Game.AttackMove.Waypoint issued=%d units=%d reason=%s",
				task.taskId,
				ok ? 1 : 0,
				static_cast<int>(task.assignedUnitIds.size()),
				ok ? reason.c_str() : commandReason.c_str());
			return ok;
		}

		bool isStrategicEnemyMemoryKind(EnemyMemoryKind kind) const
		{
			return kind == EnemyMemoryKind::Wmd ||
				kind == EnemyMemoryKind::Production ||
				kind == EnemyMemoryKind::Economy ||
				kind == EnemyMemoryKind::Defense ||
				kind == EnemyMemoryKind::BaseCommand;
		}

		int countStaleScudStrategicTargets(DWORD now) const
		{
			int staleTargets = 0;
			for (const EnemyMemoryItem& item : m_autonomy.enemyMemory.getItems())
			{
				if (!item.isStructure || item.lastSeenTick == 0u || !isStrategicEnemyMemoryKind(item.kind))
				{
					continue;
				}
				const DWORD ageMs = now - item.lastSeenTick;
				if (item.stale || ageMs > 120000u)
				{
					++staleTargets;
				}
			}
			return staleTargets;
		}

		int countReadyScudStorms(Player* player) const
		{
			if (player == nullptr)
			{
				return 0;
			}
			struct ReadyScudCountContext
			{
				int ready = 0;
			};
			ReadyScudCountContext ctx;
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				if (containsIgnoreCase(name, "scudstorm") && !obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					ReadyScudCountContext* ctx = static_cast<ReadyScudCountContext*>(userData);
					++ctx->ready;
				}
			}, &ctx);
			return ctx.ready;
		}

		struct ScoutObjectiveBuildStats
		{
			int randomObjectiveCount = 0;
			int rejectedRandomObjectives = 0;
			nlohmann::json lastRandomObjective = nlohmann::json::object();
			std::string randomReason = "not_evaluated";
		};

		unsigned int buildStableScoutSeed(Player* player, DWORD now) const
		{
			std::string mapName;
			if (TheGameInfo != nullptr)
			{
				mapName = TheGameInfo->getMap().str();
			}
			if (mapName.empty() && TheTerrainLogic != nullptr)
			{
				mapName = TheTerrainLogic->getSourceFilename().str();
			}
			return AIControlAdapterScoutingManager::buildStableScoutSeed(
				mapName,
				player != nullptr ? player->getPlayerIndex() : 0,
				now,
				m_autonomy.combatTaskManager.getScoutTaskCount());
		}

		void resolveScoutMapBounds(float& outMinX, float& outMinY, float& outMaxX, float& outMaxY) const
		{
			const AIControlAdapterScoutMapBounds bounds = AIControlAdapterScoutingManager::resolveScoutMapBounds(
				m_autonomy.state.pathingTelemetry,
				m_autonomy.state.telemetryZones);
			outMinX = bounds.minX;
			outMinY = bounds.minY;
			outMaxX = bounds.maxX;
			outMaxY = bounds.maxY;
		}

		std::vector<CombatTaskScoutRandomOrigin> buildScoutRandomOrigins() const
		{
			AIControlAdapterScoutLastZoneSnapshot lastZone;
			lastZone.hasLastZone = m_autonomy.state.hasLastZone;
			lastZone.anchorId = m_autonomy.state.lastZoneAnchorId;
			lastZone.isMainBase = m_autonomy.state.lastZoneIsMainBase;
			lastZone.centerX = m_autonomy.state.lastZoneCenterX;
			lastZone.centerY = m_autonomy.state.lastZoneCenterY;
			return AIControlAdapterScoutingManager::buildScoutRandomOrigins(m_autonomy.state.telemetryZones, lastZone);
		}

		std::vector<CombatTaskScoutRandomBarrier> buildScoutRandomBarriers() const
		{
			return AIControlAdapterScoutingManager::buildScoutRandomBarriers(m_autonomy.state.pathingTelemetry);
		}

		std::vector<CombatTaskScoutObjective> buildScoutObjectives(Player* player, DWORD now, bool randomCoverageThin, ScoutObjectiveBuildStats* outStats)
		{
			std::vector<CombatTaskScoutObjective> objectives;
			for (const EnemyMemoryItem& item : m_autonomy.enemyMemory.getItems())
			{
				if (!item.isStructure || item.lastSeenTick == 0u || !isStrategicEnemyMemoryKind(item.kind))
				{
					continue;
				}
				const DWORD ageMs = now - item.lastSeenTick;
				if (!item.stale && ageMs <= 60000u)
				{
					continue;
				}
				CombatTaskScoutObjective objective;
				objective.objectiveId = item.objectId;
				objective.position.x = item.position.x;
				objective.position.y = item.position.y;
				objective.position.z = item.position.z;
				objective.staleStructure = true;
				objective.reason = "stale_enemy_structure";
				objective.priority =
					item.kind == EnemyMemoryKind::Wmd ? 140 :
					item.kind == EnemyMemoryKind::BaseCommand ? 120 :
					item.kind == EnemyMemoryKind::Production ? 110 :
					item.kind == EnemyMemoryKind::Economy ? 100 :
					item.kind == EnemyMemoryKind::Defense ? 80 : 60;
				objectives.push_back(objective);
			}

			for (const EnemyMemoryCluster& cluster : m_autonomy.enemyMemory.getClusters())
			{
				if (cluster.lastSeenTick == 0u || now - cluster.lastSeenTick <= 60000u)
				{
					continue;
				}
				CombatTaskScoutObjective objective;
				objective.objectiveId = static_cast<unsigned int>(800000u + objectives.size());
				objective.position.x = cluster.position.x;
				objective.position.y = cluster.position.y;
				objective.position.z = cluster.position.z;
				objective.cluster = true;
				objective.reason = "stale_enemy_cluster";
				objective.priority = 50;
				objectives.push_back(objective);
			}

			if (player != nullptr && ThePlayerList != nullptr)
			{
				const Player* neutralPlayer = ThePlayerList->getNeutralPlayer();
				for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
				{
					Player* candidate = ThePlayerList->getNthPlayer(i);
					if (candidate == nullptr || candidate == player || candidate == neutralPlayer)
					{
						continue;
					}
					if (candidate->getDefaultTeam() == nullptr || player->getRelationship(candidate->getDefaultTeam()) != ENEMIES)
					{
						continue;
					}
					AIControlAdapterMapPoint basePoint = { 0.0f, 0.0f };
					if (!AIControlAdapterTryReadMapPosition(buildPlayerMapPositionSummary(candidate), basePoint))
					{
						continue;
					}
					bool hasNearbyFreshMemory = false;
					for (const EnemyMemoryItem& item : m_autonomy.enemyMemory.getItems())
					{
						if (item.lastSeenTick == 0u || now - item.lastSeenTick > 60000u)
						{
							continue;
						}
						const Real dx = item.position.x - basePoint.x;
						const Real dy = item.position.y - basePoint.y;
						if (dx * dx + dy * dy <= 900.0f * 900.0f)
						{
							hasNearbyFreshMemory = true;
							break;
						}
					}
					if (hasNearbyFreshMemory)
					{
						continue;
					}
					CombatTaskScoutObjective objective;
					objective.objectiveId = static_cast<unsigned int>(900000 + candidate->getPlayerIndex());
					objective.position.x = basePoint.x;
					objective.position.y = basePoint.y;
					objective.position.z = 0.0f;
					objective.likelyBase = true;
					objective.reason = "likely_enemy_base";
					objective.priority = objectives.empty() ? 90 : 70;
					objectives.push_back(objective);
				}
			}
			if (randomCoverageThin)
			{
				float minX = 0.0f;
				float minY = 0.0f;
				float maxX = 5000.0f;
				float maxY = 5000.0f;
				resolveScoutMapBounds(minX, minY, maxX, maxY);
				const CombatTaskScoutRandomDecision randomDecision = selectCombatTaskRandomRevealObjective({
					buildScoutRandomOrigins(),
					buildScoutRandomBarriers(),
					buildStableScoutSeed(player, now),
					m_autonomy.state.lastScoutRandomObjectiveId,
					minX,
					minY,
					maxX,
					maxY,
					std::max<Real>(1200.0f, m_autonomy.state.zoneRadius * 3.0f),
					true
				});
				if (outStats != nullptr)
				{
					outStats->randomObjectiveCount = randomDecision.selected ? 1 : 0;
					outStats->rejectedRandomObjectives = randomDecision.rejectedCount;
					outStats->randomReason = randomDecision.reason;
					if (randomDecision.selected)
					{
						outStats->lastRandomObjective = nlohmann::json::object({
							{"id", randomDecision.objective.objectiveId},
							{"origin_zone", randomDecision.objective.originZoneId},
							{"x", randomDecision.objective.position.x},
							{"y", randomDecision.objective.position.y},
							{"direction_x", randomDecision.objective.directionX},
							{"direction_y", randomDecision.objective.directionY},
							{"reason", randomDecision.reason}
						});
					}
				}
				adapterLog(
					"scout_random_objective mode=%s objective=%u origin_zone=%u x=%.1f y=%.1f direction=%.2f,%.2f reason=%s",
					randomDecision.selected ? "selected" : (randomDecision.rejectedCount > 0 ? "rejected" : "hold"),
					randomDecision.selected ? randomDecision.objective.objectiveId : 0u,
					randomDecision.selected ? randomDecision.objective.originZoneId : 0u,
					randomDecision.selected ? randomDecision.objective.position.x : 0.0f,
					randomDecision.selected ? randomDecision.objective.position.y : 0.0f,
					randomDecision.selected ? randomDecision.objective.directionX : 0.0f,
					randomDecision.selected ? randomDecision.objective.directionY : 0.0f,
					randomDecision.reason);
				if (randomDecision.selected)
				{
					objectives.push_back(randomDecision.objective);
				}
			}
			return objectives;
		}

		void collectScoutPoolCounts(Player* player, int& outLiveTechnicals, int& outQueuedTechnicals, bool& outArmsDealerReady) const
		{
			outLiveTechnicals = 0;
			outQueuedTechnicals = 0;
			outArmsDealerReady = false;
			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			for (const AutomationOwnedObjectSnapshot& owned : ownedObjects)
			{
				if (owned.object == nullptr)
				{
					continue;
				}
				if (!owned.isStructure && !owned.underConstruction && owned.isTechnical)
				{
					++outLiveTechnicals;
					continue;
				}
				if (!owned.isStructure || !owned.isArmsDealer || owned.underConstruction)
				{
					continue;
				}
				outArmsDealerReady = true;
				ProductionUpdateInterface* production = owned.object->getProductionUpdateInterface();
				if (production == nullptr || TheThingFactory == nullptr)
				{
					continue;
				}
				const std::string technicalTemplateName = AIControlAdapterTemplateInferenceService::InferTechnicalTemplateForProducer(owned.object);
				if (technicalTemplateName.empty())
				{
					continue;
				}
				const ThingTemplate* technicalTemplate = TheThingFactory->findTemplate(AsciiString(technicalTemplateName.c_str()), false);
				if (technicalTemplate != nullptr)
				{
					outQueuedTechnicals += static_cast<int>(production->countUnitTypeInQueue(technicalTemplate));
				}
			}
		}

		bool isScoutPoolTechnicalProtected(Player* player, const AutomationOwnedObjectSnapshot& owned) const
		{
			if (player == nullptr || owned.object == nullptr || !owned.isTechnical || owned.isStructure || owned.underConstruction)
			{
				return false;
			}
			int liveTechnicals = 0;
			int queuedTechnicals = 0;
			bool armsDealerReady = false;
			collectScoutPoolCounts(player, liveTechnicals, queuedTechnicals, armsDealerReady);
			if (!armsDealerReady)
			{
				return false;
			}
			unsigned int money = 0u;
			const Money* wallet = player->getMoney();
			if (wallet != nullptr && wallet->countMoney() > 0)
			{
				money = static_cast<unsigned int>(wallet->countMoney());
			}
			const unsigned int reserveCash = resolveAutonomyProfilePolicyConfig().reserveCash;
			const CombatTaskScoutPoolDecision pool = evaluateCombatTaskScoutPool({
				armsDealerReady,
				true,
				false,
				money,
				reserveCash,
				liveTechnicals,
				queuedTechnicals,
				m_autonomy.combatTaskManager.getScoutAssignedUnitCount()
			});
			const int effectivePool = std::max(0, liveTechnicals) + std::max(0, queuedTechnicals);
			return effectivePool <= std::max(2, pool.desiredTechnicals);
		}

		int resolveWorkerShuttleProtectedTechnicalCount() const
		{
			return m_workerShuttleManager.ResolveProtectedTechnicalCount(m_autonomy.state.glaUsaStrategyTelemetry);
		}

		AIControlAdapterWorkerShuttleTechnicalSnapshot buildWorkerShuttleTechnicalSnapshot(
			const AutomationOwnedObjectSnapshot& owned) const
		{
			AIControlAdapterWorkerShuttleTechnicalSnapshot snapshot;
			if (owned.object != nullptr)
			{
				snapshot.id = static_cast<unsigned int>(owned.object->getID());
				snapshot.dead = owned.object->isEffectivelyDead();
			}
			snapshot.isStructure = owned.isStructure;
			snapshot.underConstruction = owned.underConstruction;
			snapshot.isTechnical = owned.isTechnical;
			return snapshot;
		}

		AIControlAdapterWorkerShuttleAssignmentSnapshot buildWorkerShuttleAssignmentSnapshot(
			const AutonomyWorkerShuttleAssignment& assignment) const
		{
			AIControlAdapterWorkerShuttleAssignmentSnapshot snapshot;
			snapshot.taskId = assignment.taskId;
			snapshot.workerId = assignment.workerId;
			snapshot.technicalId = assignment.technicalId;
			snapshot.templateName = assignment.templateName;
			snapshot.state = assignment.state;
			snapshot.reason = assignment.reason;
			snapshot.createdTick = assignment.createdTick;
			snapshot.lastCommandTick = assignment.lastCommandTick;
			snapshot.targetX = assignment.targetPosition.x;
			snapshot.targetY = assignment.targetPosition.y;
			snapshot.targetZ = assignment.targetPosition.z;
			return snapshot;
		}

		std::vector<AIControlAdapterWorkerShuttleAssignmentSnapshot> collectWorkerShuttleAssignmentSnapshots() const
		{
			std::vector<AIControlAdapterWorkerShuttleAssignmentSnapshot> assignments;
			assignments.reserve(m_autonomy.state.workerShuttleAssignments.size());
			for (const auto& pair : m_autonomy.state.workerShuttleAssignments)
			{
				assignments.push_back(buildWorkerShuttleAssignmentSnapshot(pair.second));
			}
			return assignments;
		}

		std::set<UnsignedInt> collectWorkerShuttleProtectedTechnicalIds(Player* player) const
		{
			std::set<UnsignedInt> protectedIds;
			const int desiredProtected = resolveWorkerShuttleProtectedTechnicalCount();
			if (player == nullptr || desiredProtected <= 0)
			{
				return protectedIds;
			}
			std::vector<AIControlAdapterWorkerShuttleTechnicalSnapshot> technicals;
			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			for (const AutomationOwnedObjectSnapshot& owned : ownedObjects)
			{
				technicals.push_back(buildWorkerShuttleTechnicalSnapshot(owned));
			}
			const std::set<unsigned int> selectedIds =
				m_workerShuttleManager.CollectProtectedTechnicalIds(technicals, desiredProtected);
			for (unsigned int id : selectedIds)
			{
				protectedIds.insert(static_cast<UnsignedInt>(id));
			}
			return protectedIds;
		}

		bool isWorkerShuttleTechnicalProtected(Player* player, const AutomationOwnedObjectSnapshot& owned) const
		{
			if (player == nullptr || owned.object == nullptr || !owned.isTechnical || owned.isStructure || owned.underConstruction)
			{
				return false;
			}
			const std::set<UnsignedInt> protectedIds = collectWorkerShuttleProtectedTechnicalIds(player);
			return m_workerShuttleManager.IsTechnicalProtected(
				buildWorkerShuttleTechnicalSnapshot(owned),
				std::set<unsigned int>(protectedIds.begin(), protectedIds.end()));
		}

		bool isWorkerInsideTechnical(Object* worker, Object* technical) const
		{
			if (worker == nullptr || technical == nullptr)
			{
				return false;
			}
			if (worker->getContainedBy() == technical)
			{
				return true;
			}
			ContainModuleInterface* contain = technical->getContain();
			const ContainedItemsList* items = contain != nullptr ? contain->getContainedItemsList() : nullptr;
			if (items == nullptr)
			{
				return false;
			}
			for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it)
			{
				if (*it == worker)
				{
					return true;
				}
			}
			return false;
		}

		bool issueWorkerEnterTechnical(Player* player, Object* worker, Object* technical, std::string& reason)
		{
			if (player == nullptr || worker == nullptr || technical == nullptr)
			{
				reason = "object_missing";
				return false;
			}
			if (TheActionManager != nullptr
				&& !TheActionManager->canEnterObject(worker, technical, CMD_FROM_PLAYER, CHECK_CAPACITY))
			{
				reason = "cannot_enter_transport";
				return false;
			}
			const ObjectID workerId = worker->getID();
			const ObjectID technicalId = technical->getID();
			return executeScopedSelectionCommand(player, std::vector<ObjectID>(1, workerId), reason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_ENTER);
				if (msg == nullptr)
				{
					reason = "message_stream_not_ready";
					return false;
				}
				msg->appendObjectIDArgument(INVALID_ID);
				msg->appendObjectIDArgument(technicalId);
				return true;
			});
		}

		bool issueTechnicalMoveAndEvacuate(Object* technical, const Coord3D& target, std::string& reason)
		{
			if (technical == nullptr || technical->isEffectivelyDead())
			{
				reason = "technical_dead";
				return false;
			}
			AIUpdateInterface* ai = technical->getAIUpdateInterface();
			if (ai == nullptr)
			{
				reason = "technical_no_ai";
				return false;
			}
			Coord3D moveTarget = target;
			moveTarget.z = 0.0f;
			ai->aiMoveToAndEvacuate(&moveTarget, CMD_FROM_PLAYER);
			reason = "move_and_evacuate_issued";
			return true;
		}

		bool issueWorkerConstructNoReservation(Player* player, Object* worker, const std::string& templateName, const Coord3D& location, std::string& reason)
		{
			if (player == nullptr || worker == nullptr || templateName.empty() || TheThingFactory == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}
			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(templateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "template_not_found";
				return false;
			}
			const ObjectID workerId = worker->getID();
			const Int templateId = buildingTemplate->getTemplateID();
			Coord3D target = location;
			target.z = 0.0f;
			return executeScopedSelectionCommand(player, std::vector<ObjectID>(1, workerId), reason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DOZER_CONSTRUCT);
				if (msg == nullptr)
				{
					reason = "message_stream_not_ready";
					return false;
				}
				msg->appendIntegerArgument(templateId);
				msg->appendLocationArgument(target);
				msg->appendRealArgument(0.0f);
				return true;
			});
		}

		bool isTechnicalAssignedToWorkerShuttle(UnsignedInt technicalId) const
		{
			return m_workerShuttleManager.IsTechnicalAssigned(
				static_cast<unsigned int>(technicalId),
				collectWorkerShuttleAssignmentSnapshots());
		}

		Object* chooseWorkerShuttleTechnical(Player* player, Object* worker)
		{
			if (player == nullptr || worker == nullptr || TheGameLogic == nullptr)
			{
				return nullptr;
			}
			const std::set<UnsignedInt> protectedIds = collectWorkerShuttleProtectedTechnicalIds(player);
			if (protectedIds.empty())
			{
				return nullptr;
			}
			const Coord3D* workerPos = worker->getPosition();
			Object* best = nullptr;
			Real bestDistSq = 999999999.0f;
			for (UnsignedInt id : protectedIds)
			{
				if (isTechnicalAssignedToWorkerShuttle(id))
				{
					continue;
				}
				Object* technical = TheGameLogic->findObjectByID(static_cast<ObjectID>(id));
				if (technical == nullptr || technical->isEffectivelyDead())
				{
					continue;
				}
				if (m_autonomy.combatTaskManager.isUnitReserved(id))
				{
					continue;
				}
				if (TheActionManager != nullptr
					&& !TheActionManager->canEnterObject(worker, technical, CMD_FROM_PLAYER, CHECK_CAPACITY))
				{
					continue;
				}
				const Coord3D* techPos = technical->getPosition();
				if (workerPos == nullptr || techPos == nullptr)
				{
					return technical;
				}
				const Real dx = techPos->x - workerPos->x;
				const Real dy = techPos->y - workerPos->y;
				const Real distSq = (dx * dx) + (dy * dy);
				if (best == nullptr || distSq < bestDistSq)
				{
					best = technical;
					bestDistSq = distSq;
				}
			}
			return best;
		}

		void releaseWorkerShuttleAssignment(std::unordered_map<UnsignedInt, AutonomyWorkerShuttleAssignment>::iterator& it, const std::string& reason)
		{
			adapterLog(
				"worker_shuttle_release task=%u worker=%u technical=%u state=%s reason=%s",
				it->second.taskId,
				it->second.workerId,
				it->second.technicalId,
				it->second.state.c_str(),
				reason.c_str());
			it = m_autonomy.state.workerShuttleAssignments.erase(it);
		}

		void updateWorkerShuttleAssignments(Player* player)
		{
			if (player == nullptr || TheGameLogic == nullptr)
			{
				return;
			}
			const DWORD now = ::GetTickCount();
			nlohmann::json telemetry = nlohmann::json::array();
			const int protectedTechnicals = resolveWorkerShuttleProtectedTechnicalCount();

			for (auto it = m_autonomy.state.workerShuttleAssignments.begin(); it != m_autonomy.state.workerShuttleAssignments.end();)
			{
				AutonomyWorkerShuttleAssignment& assignment = it->second;
				SpecialTaskReservation* task = m_autonomy.taskReservationManager.findReservation(assignment.taskId);
				Object* worker = TheGameLogic->findObjectByID(static_cast<ObjectID>(assignment.workerId));
				Object* technical = TheGameLogic->findObjectByID(static_cast<ObjectID>(assignment.technicalId));
				if (protectedTechnicals <= 0)
				{
					releaseWorkerShuttleAssignment(it, "policy_disabled");
					continue;
				}
				if (task == nullptr
					|| task->state == SpecialTaskState::Complete
					|| task->state == SpecialTaskState::Failed
					|| task->state == SpecialTaskState::Expired
					|| task->targetObjectId != 0u)
				{
					releaseWorkerShuttleAssignment(it, "build_task_resolved");
					continue;
				}
				if (worker == nullptr || worker->isEffectivelyDead())
				{
					releaseWorkerShuttleAssignment(it, "worker_dead");
					continue;
				}
				if (technical == nullptr || technical->isEffectivelyDead())
				{
					releaseWorkerShuttleAssignment(it, "technical_dead");
					continue;
				}
				if (now - assignment.createdTick > 90000u)
				{
					releaseWorkerShuttleAssignment(it, "timeout");
					continue;
				}

				const Coord3D* workerPos = worker->getPosition();
				const Coord3D* technicalPos = technical->getPosition();
				const bool workerInside = isWorkerInsideTechnical(worker, technical);
				Real workerTargetDist = 999999.0f;
				if (workerPos != nullptr)
				{
					const Real dx = workerPos->x - assignment.targetPosition.x;
					const Real dy = workerPos->y - assignment.targetPosition.y;
					workerTargetDist = std::sqrt((dx * dx) + (dy * dy));
				}
				Real technicalTargetDist = 999999.0f;
				if (technicalPos != nullptr)
				{
					const Real dx = technicalPos->x - assignment.targetPosition.x;
					const Real dy = technicalPos->y - assignment.targetPosition.y;
					technicalTargetDist = std::sqrt((dx * dx) + (dy * dy));
				}

				if (workerInside)
				{
					if (assignment.state != "transporting" || now - assignment.lastCommandTick >= 9000u)
					{
						std::string reason;
						const bool issued = issueTechnicalMoveAndEvacuate(technical, assignment.targetPosition, reason);
						assignment.state = issued ? "transporting" : "failed";
						assignment.reason = reason;
						assignment.lastCommandTick = now;
						adapterLog(
							"worker_shuttle_transport task=%u worker=%u technical=%u issued=%d target=(%.1f,%.1f) distance=%.1f reason=%s",
							assignment.taskId,
							assignment.workerId,
							assignment.technicalId,
							issued ? 1 : 0,
							assignment.targetPosition.x,
							assignment.targetPosition.y,
							technicalTargetDist,
							reason.c_str());
					}
				}
				else if (workerTargetDist <= 420.0f && !assignment.constructReissued)
				{
					std::string reason;
					const bool issued = issueWorkerConstructNoReservation(player, worker, assignment.templateName, assignment.targetPosition, reason);
					assignment.constructReissued = issued;
					assignment.state = issued ? "reissued_construction" : "failed";
					assignment.reason = reason;
					assignment.lastCommandTick = now;
					adapterLog(
						"worker_shuttle_construct_reissue task=%u worker=%u technical=%u template=%s issued=%d distance=%.1f reason=%s",
						assignment.taskId,
						assignment.workerId,
						assignment.technicalId,
						assignment.templateName.c_str(),
						issued ? 1 : 0,
						workerTargetDist,
						reason.c_str());
					if (issued)
					{
						releaseWorkerShuttleAssignment(it, "construction_reissued");
						continue;
					}
				}
				else if (assignment.state == "entering" && now - assignment.lastCommandTick >= 10000u && assignment.commandReissueCount < 2)
				{
					std::string reason;
					const bool issued = issueWorkerEnterTechnical(player, worker, technical, reason);
					++assignment.commandReissueCount;
					assignment.reason = reason;
					assignment.lastCommandTick = now;
					adapterLog(
						"worker_shuttle_enter task=%u worker=%u technical=%u issued=%d reissue=%d reason=%s",
						assignment.taskId,
						assignment.workerId,
						assignment.technicalId,
						issued ? 1 : 0,
						assignment.commandReissueCount,
						reason.c_str());
				}

				AIControlAdapterWorkerShuttleAssignmentStatus assignmentStatus;
				assignmentStatus.workerInside = workerInside;
				assignmentStatus.workerDistance = workerTargetDist;
				assignmentStatus.technicalDistance = technicalTargetDist;
				telemetry.push_back(m_workerShuttleManager.BuildAssignmentTelemetry(
					buildWorkerShuttleAssignmentSnapshot(assignment),
					assignmentStatus,
					static_cast<unsigned int>(now)));
				++it;
			}

			if (protectedTechnicals > 0)
			{
				std::vector<SpecialTaskReservation*> buildTasks = m_autonomy.taskReservationManager.findBuildTasks();
				for (SpecialTaskReservation* task : buildTasks)
				{
					if (static_cast<int>(m_autonomy.state.workerShuttleAssignments.size()) >= protectedTechnicals)
					{
						break;
					}
					if (task == nullptr || task->targetObjectId != 0u)
					{
						continue;
					}
					if (task->state != SpecialTaskState::Assigned && task->state != SpecialTaskState::Moving)
					{
						continue;
					}
					if (m_autonomy.state.workerShuttleAssignments.find(task->taskId) != m_autonomy.state.workerShuttleAssignments.end())
					{
						continue;
					}
					Object* worker = TheGameLogic->findObjectByID(static_cast<ObjectID>(task->sourceObjectId));
					if (worker == nullptr || worker->isEffectivelyDead() || !worker->isKindOf(KINDOF_DOZER))
					{
						continue;
					}
					if (worker->getContainedBy() != nullptr)
					{
						continue;
					}
					const Coord3D* workerPos = worker->getPosition();
					if (workerPos == nullptr)
					{
						continue;
					}
					const Real dx = workerPos->x - task->targetPosition.x;
					const Real dy = workerPos->y - task->targetPosition.y;
					const Real distance = std::sqrt((dx * dx) + (dy * dy));
					if (distance < 1200.0f)
					{
						continue;
					}
					Object* technical = chooseWorkerShuttleTechnical(player, worker);
					if (technical == nullptr)
					{
						continue;
					}
					std::string reason;
					const bool issued = issueWorkerEnterTechnical(player, worker, technical, reason);
					adapterLog(
						"worker_shuttle_assignment task=%u worker=%u technical=%u template=%s issued=%d distance=%.1f reason=%s",
						task->taskId,
						task->sourceObjectId,
						static_cast<unsigned int>(technical->getID()),
						task->expectedTemplate.c_str(),
						issued ? 1 : 0,
						distance,
						reason.c_str());
					if (!issued)
					{
						continue;
					}
					AutonomyWorkerShuttleAssignment assignment;
					assignment.taskId = task->taskId;
					assignment.workerId = task->sourceObjectId;
					assignment.technicalId = static_cast<UnsignedInt>(technical->getID());
					assignment.templateName = task->expectedTemplate;
					assignment.targetPosition = task->targetPosition;
					assignment.createdTick = now;
					assignment.lastCommandTick = now;
					assignment.lastProgressTick = now;
					assignment.state = "entering";
					assignment.reason = reason;
					m_autonomy.state.workerShuttleAssignments[task->taskId] = assignment;
					m_autonomy.taskReservationManager.updateTaskState(task->taskId, SpecialTaskState::Moving, "worker_shuttle_entering");
				}
			}

			m_autonomy.state.glaUsaStrategyTelemetry["worker_shuttle_tasks"] = telemetry;
			adapterLog(
				"worker_shuttle_policy protected=%d active=%d reason=%s",
				protectedTechnicals,
				static_cast<int>(m_autonomy.state.workerShuttleAssignments.size()),
				protectedTechnicals > 0 ? "worker_mobility" : "disabled");
		}

		bool shouldLogScoutReservationSkip(const std::string& key, DWORD now, DWORD heartbeatMs = 5000u)
		{
			std::unordered_map<std::string, DWORD>& ticks = m_autonomy.state.scoutReservationLogTickByKey;
			const auto it = ticks.find(key);
			if (it == ticks.end() || it->second == 0u || now - it->second >= heartbeatMs)
			{
				ticks[key] = now;
				return true;
			}
			return false;
		}

		struct ScoutAvailabilityStats
		{
			int available = 0;
			int unavailable = 0;
			std::string reason = "none";
			std::map<std::string, int> reasons;
		};

		void recordScoutUnavailable(ScoutAvailabilityStats* stats, const std::string& reason) const
		{
			if (stats == nullptr)
			{
				return;
			}
			++stats->unavailable;
			++stats->reasons[reason];
			if (stats->reason == "none" || stats->reasons[reason] > stats->reasons[stats->reason])
			{
				stats->reason = reason;
			}
		}

		std::vector<unsigned int> selectDedicatedScoutUnitIds(Player* player, const Coord3D& objective, bool scudTargetStarved, bool allowLateFallback, ScoutAvailabilityStats* outStats, bool relaxDefenseFloor = false)
		{
			std::vector<CombatTaskScoutCandidate> candidates;
			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			bool hasAvailableTechnical = false;
			for (const AutomationOwnedObjectSnapshot& owned : ownedObjects)
			{
				if (owned.object == nullptr || owned.isStructure || owned.underConstruction || !owned.isTechnical)
				{
					continue;
				}
				const UnsignedInt unitId = static_cast<UnsignedInt>(owned.object->getID());
				if (!owned.object->isEffectivelyDead() &&
					!owned.isDozer &&
					!owned.isHarvester &&
					!m_autonomy.taskReservationManager.isObjectReserved(unitId) &&
					!isGarrisonReservedUnit(unitId) &&
					(!isUnitProtectedByZoneDefenseFloor(owned.object) || (relaxDefenseFloor && canRelaxScoutDefenseFloorForUnit(owned.object))) &&
					!m_autonomy.combatTaskManager.isUnitReserved(unitId) &&
					!isWorkerShuttleTechnicalProtected(player, owned))
				{
					hasAvailableTechnical = true;
					break;
				}
			}
			bool hasAvailableQuad = false;
			if (!hasAvailableTechnical && scudTargetStarved)
			{
				for (const AutomationOwnedObjectSnapshot& owned : ownedObjects)
				{
					if (owned.object == nullptr || owned.isStructure || owned.underConstruction || !owned.isQuad)
					{
						continue;
					}
					const UnsignedInt unitId = static_cast<UnsignedInt>(owned.object->getID());
					if (!owned.object->isEffectivelyDead() &&
						!owned.isDozer &&
						!owned.isHarvester &&
						!m_autonomy.taskReservationManager.isObjectReserved(unitId) &&
						!isGarrisonReservedUnit(unitId) &&
						(!isUnitProtectedByZoneDefenseFloor(owned.object) || (relaxDefenseFloor && canRelaxScoutDefenseFloorForUnit(owned.object))) &&
						!m_autonomy.combatTaskManager.isUnitReserved(unitId))
					{
						hasAvailableQuad = true;
						break;
					}
				}
			}
			for (const AutomationOwnedObjectSnapshot& owned : ownedObjects)
			{
				if (owned.object == nullptr || owned.isStructure || owned.underConstruction)
				{
					continue;
				}
				const UnsignedInt unitId = static_cast<UnsignedInt>(owned.object->getID());
				const bool vehicle = owned.isVehicle || owned.isAircraft;
				const bool isBuggy = containsIgnoreCase(owned.name, "rocketbuggy") || containsIgnoreCase(owned.name, "buggy");
				const bool isAllowedFallback =
					owned.isTechnical ||
					(scudTargetStarved && !hasAvailableTechnical && owned.isQuad) ||
					(allowLateFallback && !hasAvailableTechnical && !hasAvailableQuad && isBuggy) ||
					(allowLateFallback && !hasAvailableTechnical && !hasAvailableQuad && owned.isScorpion);
				const bool fast = owned.isTechnical || owned.isQuad || isBuggy || owned.isScorpion;
				CombatTaskScoutCandidate candidate;
				candidate.unitId = unitId;
				candidate.alive = !owned.object->isEffectivelyDead();
				candidate.fast = fast;
				candidate.combatCapable = vehicle && isAllowedFallback && (owned.isTechnical || owned.object->isAbleToAttack());
				candidate.worker = owned.isDozer || owned.isHarvester;
				candidate.captureTaskReserved = m_autonomy.taskReservationManager.isObjectReserved(unitId);
				candidate.constructionTaskReserved = candidate.captureTaskReserved;
				candidate.garrisonReserved = isGarrisonReservedUnit(unitId);
				candidate.zoneDefenseFloorReserved =
					isUnitProtectedByZoneDefenseFloor(owned.object) &&
					!(relaxDefenseFloor && canRelaxScoutDefenseFloorForUnit(owned.object));
				candidate.combatTaskReserved = m_autonomy.combatTaskManager.isUnitReserved(unitId);
				candidate.artilleryCounterReserved =
					owned.isScudLauncher ||
					containsIgnoreCase(owned.name, "scudlauncher") ||
					containsIgnoreCase(owned.name, "tomahawk") ||
					containsIgnoreCase(owned.name, "nuke") ||
					containsIgnoreCase(owned.name, "inferno");
				candidate.workerShuttleReserved = owned.isTechnical && isWorkerShuttleTechnicalProtected(player, owned);
				if (owned.isTechnical)
				{
					candidate.preference = 100;
				}
				else if (owned.isQuad)
				{
					candidate.preference = 60;
				}
				else if (owned.isScorpion)
				{
					candidate.preference = 35;
				}
				else if (isBuggy)
				{
					candidate.preference = 20;
				}
				const Coord3D* pos = owned.object->getPosition();
				if (pos != nullptr)
				{
					const Real dx = pos->x - objective.x;
					const Real dy = pos->y - objective.y;
					candidate.distanceFromOrigin = std::sqrt(dx * dx + dy * dy);
				}
				if (owned.isTechnical || owned.isQuad || isBuggy || owned.isScorpion)
				{
					if (candidate.unitId == 0u || !candidate.alive)
					{
						recordScoutUnavailable(outStats, "dead_or_invalid");
					}
					else if (!candidate.combatCapable)
					{
						recordScoutUnavailable(outStats, isAllowedFallback ? "not_scout_capable" : "fallback_not_allowed");
					}
					else if (candidate.worker)
					{
						recordScoutUnavailable(outStats, "worker");
					}
					else if (candidate.captureTaskReserved || candidate.constructionTaskReserved)
					{
						recordScoutUnavailable(outStats, "reserved_by_special_task");
					}
					else if (candidate.garrisonReserved)
					{
						recordScoutUnavailable(outStats, "reserved_by_garrison");
					}
					else if (candidate.zoneDefenseFloorReserved)
					{
						recordScoutUnavailable(outStats, "defense_floor");
					}
					else if (candidate.combatTaskReserved)
					{
						recordScoutUnavailable(outStats, "task_owned");
					}
					else if (candidate.artilleryCounterReserved)
					{
						recordScoutUnavailable(outStats, "artillery_counter_reserved");
					}
					else if (candidate.workerShuttleReserved)
					{
						recordScoutUnavailable(outStats, "worker_shuttle_reserved");
					}
				}
				candidates.push_back(candidate);
			}
			std::vector<unsigned int> selected = selectCombatTaskScoutUnits(candidates, relaxDefenseFloor ? 1 : 2, relaxDefenseFloor);
			if (outStats != nullptr)
			{
				outStats->available = static_cast<int>(selected.size());
				if (outStats->unavailable == 0 && selected.empty())
				{
					outStats->reason = "no_idle_scouts";
				}
			}
			return selected;
		}

		bool issueScoutWaypointCommand(Player* player, CombatTask& task, const std::string& reason)
		{
			if (player == nullptr || task.assignedUnitIds.empty() || task.waypoints.empty())
			{
				return false;
			}
			const int waypointIndex = std::max(0, std::min(task.currentWaypointIndex, static_cast<int>(task.waypoints.size()) - 1));
			const CombatTaskWaypoint& waypoint = task.waypoints[static_cast<std::size_t>(waypointIndex)];
			nlohmann::json objectIds = nlohmann::json::array();
			for (unsigned int unitId : task.assignedUnitIds)
			{
				objectIds.push_back(static_cast<Int>(unitId));
			}
			nlohmann::json args = nlohmann::json::object({
				{"x", waypoint.position.x},
				{"y", waypoint.position.y},
				{"object_ids", objectIds}
			});
			if (m_autonomy.state.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_autonomy.state.playerIndex;
			}
			nlohmann::json message = {
				{"type", "SessionCommand"},
				{"request_id", std::string("dedicated_scout")},
				{"cmd", "Game.Move"},
				{"args", args}
			};
			std::string commandReason;
			const bool ok = executeGameMove(message, commandReason);
			const DWORD now = ::GetTickCount();
			if (ok)
			{
				m_autonomy.combatTaskManager.updateTaskCommand(task.taskId, now);
				m_autonomy.combatTaskManager.updateTaskState(task.taskId, CombatTaskState::MovingToStage, reason);
				task.currentWaypointStartTick = now;
			}
			adapterLog(
				"scout_command task=%u action=move waypoint=%d units=%d x=%.1f y=%.1f reason=%s",
				task.taskId,
				waypointIndex,
				static_cast<int>(task.assignedUnitIds.size()),
				waypoint.position.x,
				waypoint.position.y,
				ok ? reason.c_str() : commandReason.c_str());
			adapterLog(
				"scout_task_state task=%u state=moving units=%d waypoint=%d fresh_targets=%d reason=%s",
				task.taskId,
				static_cast<int>(task.assignedUnitIds.size()),
				waypointIndex,
				countFreshScudStrategicTargets(now),
				ok ? reason.c_str() : commandReason.c_str());
			return ok;
		}

		bool updateScoutTaskLifecycle(Player* player, CombatTask& task)
		{
			if (player == nullptr || TheGameLogic == nullptr || task.type != CombatTaskType::Scout)
			{
				return false;
			}
			const DWORD now = ::GetTickCount();
			const int freshTargets = countFreshScudStrategicTargets(now);
			if (freshTargets > 0)
			{
				for (const EnemyMemoryItem& item : m_autonomy.enemyMemory.getItems())
				{
					if (!item.isStructure || item.lastSeenTick == 0u || !isStrategicEnemyMemoryKind(item.kind) || now - item.lastSeenTick > 120000u)
					{
						continue;
					}
					adapterLog(
						"scout_target_revealed task=%u target=%u template=%s kind=%s x=%.1f y=%.1f",
						task.taskId,
						item.objectId,
						item.templateName.c_str(),
						AIControlAdapterEnemyMemory::kindToString(item.kind),
						item.position.x,
						item.position.y);
					break;
				}
				m_autonomy.state.lastScoutRevealTick = now;
				m_autonomy.combatTaskManager.completeTask(task.taskId, "fresh_target_revealed");
				task.scoutCompletedTick = now;
				adapterLog(
					"scout_task_state task=%u state=complete units=%d waypoint=%d fresh_targets=%d reason=fresh_target_revealed",
					task.taskId,
					static_cast<int>(task.assignedUnitIds.size()),
					task.currentWaypointIndex,
					freshTargets);
				return true;
			}
			if (task.timeoutTick > 0 && now >= task.timeoutTick)
			{
				m_autonomy.combatTaskManager.expireTask(task.taskId, "scout_timeout");
				task.scoutCompletedTick = now;
				adapterLog(
					"scout_task_state task=%u state=timeout units=%d waypoint=%d fresh_targets=%d reason=scout_timeout",
					task.taskId,
					static_cast<int>(task.assignedUnitIds.size()),
					task.currentWaypointIndex,
					freshTargets);
				return true;
			}
			if (task.assignedUnitIds.empty())
			{
				m_autonomy.combatTaskManager.failTask(task.taskId, "no_scout_units");
				task.scoutCompletedTick = now;
				adapterLog(
					"scout_task_state task=%u state=failed units=0 waypoint=%d fresh_targets=%d reason=no_scout_units",
					task.taskId,
					task.currentWaypointIndex,
					freshTargets);
				return true;
			}
			if (task.waypoints.empty())
			{
				task.waypoints = buildDirectRaidWaypoints(task.hasOriginPosition ? task.originPosition : task.targetPosition, task.targetPosition, 420.0f);
				task.currentWaypointIndex = 0;
			}

			const int waypointIndex = std::max(0, std::min(task.currentWaypointIndex, static_cast<int>(task.waypoints.size()) - 1));
			const CombatTaskWaypoint& waypoint = task.waypoints[static_cast<std::size_t>(waypointIndex)];
			const Real radiusSq = waypoint.radius * waypoint.radius;
			int arrived = 0;
			for (unsigned int unitId : task.assignedUnitIds)
			{
				Object* unit = TheGameLogic->findObjectByID(static_cast<ObjectID>(unitId));
				const Coord3D* pos = unit != nullptr ? unit->getPosition() : nullptr;
				if (unit == nullptr || unit->isEffectivelyDead() || pos == nullptr)
				{
					continue;
				}
				const Real dx = pos->x - waypoint.position.x;
				const Real dy = pos->y - waypoint.position.y;
				if (dx * dx + dy * dy <= radiusSq)
				{
					++arrived;
				}
			}
			task.arrivedCount = arrived;
			task.missingCount = std::max(0, static_cast<int>(task.assignedUnitIds.size()) - arrived);
			if (arrived > 0)
			{
				if (waypointIndex + 1 >= static_cast<int>(task.waypoints.size()))
				{
					m_autonomy.state.lastScoutRevealTick = now;
					m_autonomy.combatTaskManager.completeTask(task.taskId, "objective_scanned");
					task.scoutCompletedTick = now;
					adapterLog(
						"scout_task_state task=%u state=complete units=%d waypoint=%d fresh_targets=%d reason=objective_scanned",
						task.taskId,
						static_cast<int>(task.assignedUnitIds.size()),
						waypointIndex,
						freshTargets);
					return true;
				}
				++task.currentWaypointIndex;
				issueScoutWaypointCommand(player, task, "advance_scout_waypoint");
				return true;
			}
			if (task.state == CombatTaskState::Assembling || task.state == CombatTaskState::Forming || now - task.lastCommandTick >= 10000u)
			{
				issueScoutWaypointCommand(player, task, task.state == CombatTaskState::Assembling ? "scout_start" : "refresh_scout_waypoint");
				return true;
			}
			if (shouldLogCombatTaskScoutState(task, now, "scouting", "enroute", waypointIndex, freshTargets, 4000u))
			{
				adapterLog(
					"scout_task_state task=%u state=scouting units=%d waypoint=%d fresh_targets=%d reason=enroute",
					task.taskId,
					static_cast<int>(task.assignedUnitIds.size()),
					waypointIndex,
					freshTargets);
			}
			return true;
		}

		void evaluateDedicatedScouting(Player* player)
		{
			if (!isAutonomyModeActive() || player == nullptr || TheGameLogic == nullptr)
			{
				return;
			}
			const DWORD now = ::GetTickCount();
			if (AIControlAdapterIsTickInFuture(m_autonomy.state.nextScoutTick, now))
			{
				return;
			}
			m_autonomy.state.nextScoutTick = now + 5000u;

			const int readyScuds = countReadyScudStorms(player);
			const int freshTargets = countFreshScudStrategicTargets(now);
			const int staleTargets = countStaleScudStrategicTargets(now);
			const bool scudTargetStarved = readyScuds > 0 && freshTargets <= 0;
			int scoutPoolLive = 0;
			int scoutPoolQueued = 0;
			bool scoutPoolArmsDealerReady = false;
			collectScoutPoolCounts(player, scoutPoolLive, scoutPoolQueued, scoutPoolArmsDealerReady);
			unsigned int scoutPoolMoney = 0u;
			const Money* scoutWallet = player->getMoney();
			if (scoutWallet != nullptr)
			{
				const int walletMoney = scoutWallet->countMoney();
				scoutPoolMoney = walletMoney > 0 ? static_cast<unsigned int>(walletMoney) : 0u;
			}
			const unsigned int scoutReserveCash = resolveAutonomyProfilePolicyConfig().reserveCash;
			const int activeScoutAssignments = m_autonomy.combatTaskManager.getScoutAssignedUnitCount();
			const CombatTaskScoutPoolDecision scoutPoolDecision = evaluateCombatTaskScoutPool({
				scoutPoolArmsDealerReady,
				scudTargetStarved,
				freshTargets > 0 && staleTargets <= 0,
				scoutPoolMoney,
				scoutReserveCash,
				scoutPoolLive,
				scoutPoolQueued,
				activeScoutAssignments
			});
			ScoutObjectiveBuildStats objectiveStats;
			const bool randomCoverageThin = freshTargets <= 0;
			const std::vector<CombatTaskScoutObjective> objectives = buildScoutObjectives(player, now, randomCoverageThin, &objectiveStats);
			const CombatTaskScoutObjective objective = selectCombatTaskScoutObjective(objectives);
			int likelyRegionsRemaining = 0;
			for (const CombatTaskScoutObjective& candidateObjective : objectives)
			{
				if (candidateObjective.likelyBase || candidateObjective.staleStructure || candidateObjective.randomReveal)
				{
					++likelyRegionsRemaining;
				}
			}
			const DWORD lastScoutRevealAge = m_autonomy.state.lastScoutRevealTick != 0u
				? now - m_autonomy.state.lastScoutRevealTick
				: 999999999u;
			const bool scudTargetRefreshNeeded =
				readyScuds > 0 &&
				freshTargets <= 0 &&
				(staleTargets > 0 || likelyRegionsRemaining > 0);
			ScoutAvailabilityStats scoutAvailability;
			std::vector<unsigned int> scoutUnits =
				objective.objectiveId != 0u ? selectDedicatedScoutUnitIds(player, objective.position, scudTargetStarved, readyScuds >= 4, &scoutAvailability) : std::vector<unsigned int>();
			const int activeScouts = m_autonomy.combatTaskManager.getScoutTaskCount();
			const int maxActiveScouts = scudTargetRefreshNeeded && readyScuds >= 4 ? 2 : 1;
			CombatTaskScoutPolicyDecision decision = evaluateCombatTaskScoutPolicy({
				readyScuds,
				freshTargets,
				staleTargets,
				static_cast<int>(objectives.size()),
				static_cast<int>(scoutUnits.size()),
				activeScouts,
				maxActiveScouts,
				false,
				scudTargetRefreshNeeded,
				likelyRegionsRemaining,
				static_cast<unsigned int>(lastScoutRevealAge)
			});
			bool criticalZoneDefense = false;
			for (const auto& threatPair : m_autonomy.state.zoneThreats)
			{
				if (threatPair.second.level == "critical")
				{
					criticalZoneDefense = true;
					break;
				}
			}
			const bool mainBaseCritical =
				(m_autonomy.state.emergencySurvivalTelemetry.is_object() && m_autonomy.state.emergencySurvivalTelemetry.value("active", false)) ||
				(m_autonomy.state.mainBaseCriticalOverrideTelemetry.is_object() && m_autonomy.state.mainBaseCriticalOverrideTelemetry.value("active", false));
			const CombatTaskScudTargetRefreshOverrideDecision refreshOverride = evaluateCombatTaskScudTargetRefreshOverride({
				scudTargetRefreshNeeded,
				scoutUnits.empty(),
				scoutAvailability.reason == "defense_floor",
				mainBaseCritical,
				criticalZoneDefense,
				scoutPoolArmsDealerReady,
				scoutPoolMoney,
				scoutReserveCash,
				activeScouts,
				maxActiveScouts,
				scoutPoolLive,
				scoutPoolQueued,
				activeScoutAssignments
			});
			int borrowedScoutCount = 0;
			if (refreshOverride.allowBorrow && objective.objectiveId != 0u)
			{
				ScoutAvailabilityStats relaxedAvailability;
				std::vector<unsigned int> relaxedScoutUnits =
					selectDedicatedScoutUnitIds(player, objective.position, scudTargetStarved, readyScuds >= 4, &relaxedAvailability, true);
				if (!relaxedScoutUnits.empty())
				{
					scoutUnits = relaxedScoutUnits;
					scoutAvailability = relaxedAvailability;
					borrowedScoutCount = static_cast<int>(scoutUnits.size());
					decision = evaluateCombatTaskScoutPolicy({
						readyScuds,
						freshTargets,
						staleTargets,
						static_cast<int>(objectives.size()),
						static_cast<int>(scoutUnits.size()),
						activeScouts,
						maxActiveScouts,
						false,
						scudTargetRefreshNeeded,
						likelyRegionsRemaining,
						static_cast<unsigned int>(lastScoutRevealAge)
					});
				}
			}

			m_autonomy.state.scoutingTelemetry = nlohmann::json::object({
				{"active_scout_tasks", activeScouts},
				{"scout_units_assigned", 0},
				{"available_scout_candidates", static_cast<int>(scoutUnits.size())},
				{"scout_objective_count", static_cast<int>(objectives.size())},
				{"scout_pool_desired", scoutPoolDecision.desiredTechnicals},
				{"scout_pool_live", scoutPoolLive},
				{"scout_pool_queued", scoutPoolQueued},
				{"scout_pool_assigned", activeScoutAssignments},
				{"unavailable_scout_candidates", scoutAvailability.unavailable},
				{"unavailable_reason", scoutAvailability.reason},
				{"random_objective_count", objectiveStats.randomObjectiveCount},
				{"last_random_objective", objectiveStats.lastRandomObjective},
				{"rejected_random_objectives", objectiveStats.rejectedRandomObjectives},
				{"stale_enemy_memory_count", staleTargets},
				{"fresh_strategic_target_count", freshTargets},
				{"scud_target_starved", scudTargetStarved},
				{"scud_target_refresh", nlohmann::json::object({
					{"ready_scuds", readyScuds},
					{"fresh_strategic_targets", freshTargets},
					{"stale_strategic_targets", staleTargets},
					{"last_scout_reveal_age_ms", lastScoutRevealAge},
					{"likely_regions_remaining", likelyRegionsRemaining},
					{"mode", decision.mode},
					{"reason", decision.reason},
					{"defense_floor_relaxed", refreshOverride.relaxDefenseFloor && borrowedScoutCount > 0},
					{"borrowed_scout_count", borrowedScoutCount},
					{"queued_scout_count", scoutPoolQueued},
					{"override_mode", refreshOverride.mode},
					{"override_reason", refreshOverride.reason}
				})},
				{"mode", decision.mode},
				{"reason", decision.reason}
			});

			adapterLog(
				"scout_policy mode=%s reason=%s ready_scuds=%d fresh_targets=%d stale_targets=%d available_scouts=%d",
				decision.mode,
				decision.reason,
				readyScuds,
				freshTargets,
				staleTargets,
				static_cast<int>(scoutUnits.size()));
			adapterLog(
				"scud_target_refresh mode=%s ready_scuds=%d fresh_targets=%d stale_targets=%d likely_regions=%d reason=%s",
				decision.mode,
				readyScuds,
				freshTargets,
				staleTargets,
				likelyRegionsRemaining,
				decision.reason);
			if (scudTargetRefreshNeeded)
			{
				adapterLog(
					"scud_target_starvation ready_scuds=%d scout_pool_live=%d available_scouts=%d last_reveal_age_ms=%u reason=%s",
					readyScuds,
					scoutPoolLive,
					static_cast<int>(scoutUnits.size()),
					static_cast<unsigned int>(lastScoutRevealAge),
					decision.reason);
				adapterLog(
					"scud_target_refresh_override mode=%s reason=%s ready_scuds=%d active_scouts=%d borrowed=%d queued=%d",
					refreshOverride.mode,
					refreshOverride.reason,
					readyScuds,
					activeScouts,
					borrowedScoutCount,
					scoutPoolQueued);
			}
			adapterLog(
				"scout_pool_availability live=%d queued=%d assigned=%d available=%d unavailable=%d reason=%s",
				scoutPoolLive,
				scoutPoolQueued,
				activeScoutAssignments,
				static_cast<int>(scoutUnits.size()),
				scoutAvailability.unavailable,
				scoutAvailability.reason.c_str());

			if (!decision.shouldLaunch || objective.objectiveId == 0u || scoutUnits.empty())
			{
				return;
			}

			Coord3D origin = objective.position;
			int originCount = 0;
			for (unsigned int unitId : scoutUnits)
			{
				Object* unit = TheGameLogic->findObjectByID(static_cast<ObjectID>(unitId));
				const Coord3D* pos = unit != nullptr ? unit->getPosition() : nullptr;
				if (pos == nullptr)
				{
					continue;
				}
				if (originCount == 0)
				{
					origin.x = 0.0f;
					origin.y = 0.0f;
					origin.z = 0.0f;
				}
				origin.x += pos->x;
				origin.y += pos->y;
				origin.z += pos->z;
				++originCount;
			}
			if (originCount > 0)
			{
				const Real inv = 1.0f / static_cast<Real>(originCount);
				origin.x *= inv;
				origin.y *= inv;
				origin.z *= inv;
			}

			const unsigned int taskId = m_autonomy.combatTaskManager.createTask(
				CombatTaskType::Scout,
				scoutUnits,
				objective.position,
				"dedicated_scouting",
				decision.reason,
				45000u);
			CombatTask* task = m_autonomy.combatTaskManager.findTask(taskId);
			if (task == nullptr)
			{
				return;
			}
			task->targetObjectId = objective.objectiveId;
			task->originPosition = origin;
			task->hasOriginPosition = true;
			task->waypoints = buildDirectRaidWaypoints(origin, objective.position, 420.0f);
			task->currentWaypointIndex = 0;
			task->minimumViableCount = 1;
			task->state = CombatTaskState::Forming;
			if (objective.randomReveal)
			{
				m_autonomy.state.lastScoutRandomObjectiveId = objective.objectiveId;
				m_autonomy.state.lastScoutRandomObjectiveX = objective.position.x;
				m_autonomy.state.lastScoutRandomObjectiveY = objective.position.y;
				m_autonomy.state.lastScoutRandomDirection =
					std::to_string(static_cast<double>(objective.directionX)) + "," + std::to_string(static_cast<double>(objective.directionY));
			}
			m_autonomy.state.scoutingTelemetry["active_scout_tasks"] = activeScouts + 1;
			m_autonomy.state.scoutingTelemetry["scout_units_assigned"] = static_cast<int>(scoutUnits.size());
			m_autonomy.state.scoutingTelemetry["last_objective"] = nlohmann::json::object({
				{"id", objective.objectiveId},
				{"x", objective.position.x},
				{"y", objective.position.y},
				{"reason", objective.reason},
				{"random_reveal", objective.randomReveal}
			});
			adapterLog(
				"scout_task_created task=%u units=%d objective=%u x=%.1f y=%.1f reason=%s",
				taskId,
				static_cast<int>(scoutUnits.size()),
				objective.objectiveId,
				objective.position.x,
				objective.position.y,
				decision.reason);
			issueScoutWaypointCommand(player, *task, "scout_start");
		}

		bool updateRaidCohesionTask(Player* player, CombatTask& task)
		{
			if (player == nullptr || TheGameLogic == nullptr || task.type != CombatTaskType::Attack || task.owner != "autonomous_attack")
			{
				return false;
			}
			if (task.assignedUnitIds.empty())
			{
				m_autonomy.combatTaskManager.failTask(task.taskId, "no_assigned_units");
				adapterLog("combat_task_failed task=%u type=attack reason=no_assigned_units", task.taskId);
				return true;
			}
			if (task.waypoints.empty())
			{
				const Coord3D origin = task.hasOriginPosition ? task.originPosition : task.targetPosition;
				task.waypoints = buildDirectRaidWaypoints(origin, task.targetPosition, 300.0f);
				task.currentWaypointIndex = 0;
				task.currentWaypointStartTick = ::GetTickCount();
				task.cohesionReason = "waypoints_rebuilt";
			}

			const DWORD now = ::GetTickCount();
			if (task.timeoutTick > 0 && now >= task.timeoutTick)
			{
				const DWORD taskAge = now - task.createdTick;
				m_autonomy.combatTaskManager.expireTask(task.taskId, "timeout_exceeded");
				adapterLog(
					"combat_task_failed task=%u type=attack reason=timeout_exceeded age_ms=%u",
					task.taskId,
					static_cast<unsigned int>(taskAge));
				adapterLog(
					"combat_task_release task=%u type=attack units=%d reason=task_expired",
					task.taskId,
					static_cast<int>(task.assignedUnitIds.size()));
				return true;
			}
			if (task.state == CombatTaskState::Assembling || task.state == CombatTaskState::Forming)
			{
				issueRaidWaypointCommand(player, task, "forming");
			}

			const int waypointCount = static_cast<int>(task.waypoints.size());
			const int waypointIndex = std::max(0, std::min(task.currentWaypointIndex, waypointCount - 1));
			const CombatTaskWaypoint& waypoint = task.waypoints[waypointIndex];
			const Real radiusSq = waypoint.radius * waypoint.radius;

			int liveAssigned = 0;
			int arrived = 0;
			int infantryLive = 0;
			int infantryArrived = 0;
			int vehicleLive = 0;
			int vehicleArrived = 0;
			Real sumX = 0.0f;
			Real sumY = 0.0f;
			std::vector<Coord3D> livePositions;
			std::vector<unsigned int> retainedAssignedUnitIds;
			std::vector<unsigned int> vehicleUnitIds;
			std::vector<unsigned int> infantryUnitIds;
			for (unsigned int unitId : task.assignedUnitIds)
			{
				Object* unit = TheGameLogic->findObjectByID(static_cast<ObjectID>(unitId));
				if (unit == nullptr || unit->isEffectivelyDead())
				{
					continue;
				}
				const Coord3D* pos = unit->getPosition();
				if (pos == nullptr)
				{
					continue;
				}
				++liveAssigned;
				retainedAssignedUnitIds.push_back(unitId);
				sumX += pos->x;
				sumY += pos->y;
				livePositions.push_back(*pos);
				const bool infantry = unit->isKindOf(KINDOF_INFANTRY);
				const bool vehicle = unit->isKindOf(KINDOF_VEHICLE) || unit->isKindOf(KINDOF_AIRCRAFT);
				if (infantry)
				{
					++infantryLive;
					infantryUnitIds.push_back(unitId);
				}
				if (vehicle)
				{
					++vehicleLive;
					vehicleUnitIds.push_back(unitId);
				}
				const Real dx = pos->x - waypoint.position.x;
				const Real dy = pos->y - waypoint.position.y;
				const bool unitArrived = (dx * dx + dy * dy) <= radiusSq;
				if (unitArrived)
				{
					++arrived;
					if (infantry)
					{
						++infantryArrived;
					}
					if (vehicle)
					{
						++vehicleArrived;
					}
				}
			}
			if (retainedAssignedUnitIds.size() != task.assignedUnitIds.size())
			{
				task.assignedUnitIds = retainedAssignedUnitIds;
			}

			Real spread = 0.0f;
			if (liveAssigned > 0)
			{
				const Real cx = sumX / static_cast<Real>(liveAssigned);
				const Real cy = sumY / static_cast<Real>(liveAssigned);
				for (const Coord3D& pos : livePositions)
				{
					const Real dx = pos.x - cx;
					const Real dy = pos.y - cy;
					spread = std::max<Real>(spread, std::sqrt(dx * dx + dy * dy));
				}
			}

			const bool timeout = task.currentWaypointStartTick > 0 &&
				now - task.currentWaypointStartTick >= task.stageTimeoutMs;
			const DWORD cohesionWaitMsForMode = task.cohesionWaitStartTick > 0u ? now - task.cohesionWaitStartTick : 0u;
			const AIControlAdapterRaidModeDecision raidModeDecision =
				AIControlAdapterRaidManager::evaluateRaidModePolicy(
					task.raidMode,
					infantryLive,
					vehicleLive,
					task.initialUnitCount,
					task.minimumViableCount,
					task.cohesionWaitStartTick > 0u,
					cohesionWaitMsForMode,
					45000u);
			task.raidMode = raidModeDecision.raidMode;
			if (raidModeDecision.degradeToVehicle)
			{
				const int oldInfantryLive = infantryLive;
				if (task.degradedFrom.empty() || std::string(raidModeDecision.reason) == "infantry_timeout")
				{
					task.degradedFrom = "mixed_local";
					task.degradeReason = raidModeDecision.reason;
				}
				task.raidMode = "vehicle";
				task.assignedUnitIds = vehicleUnitIds;
				task.initialUnitCount = static_cast<int>(vehicleUnitIds.size());
				liveAssigned = vehicleLive;
				arrived = vehicleArrived;
				infantryLive = 0;
				infantryArrived = 0;
				if (std::string(raidModeDecision.reason) == "infantry_timeout")
				{
					task.cohesionWaitStartTick = 0u;
				}
				adapterLog(
					"raid_cohesion_degrade task=%u from=mixed to=vehicle reason=%s vehicles=%d infantry=%d missing=%d dead=%d",
					task.taskId,
					raidModeDecision.reason,
					vehicleLive,
					oldInfantryLive,
					std::max(0, liveAssigned - arrived),
					std::max(0, task.initialUnitCount - static_cast<int>(retainedAssignedUnitIds.size())));
			}
			if (raidModeDecision.fail && std::string(raidModeDecision.reason) == "infantry_timeout_no_survivors")
			{
				m_autonomy.combatTaskManager.failTask(task.taskId, raidModeDecision.reason);
				adapterLog(
					"raid_task_release task=%u reason=%s",
					task.taskId,
					raidModeDecision.reason);
				return true;
			}
			if (raidModeDecision.fail && std::string(raidModeDecision.reason) == "no_viable_vehicle_group")
			{
				m_autonomy.combatTaskManager.failTask(task.taskId, raidModeDecision.reason);
				adapterLog(
					"raid_task_release task=%u reason=%s",
					task.taskId,
					raidModeDecision.reason);
				return true;
			}
			const bool requireInfantryQuorum = task.raidMode != "vehicle";
			const CombatTaskCohesionDecision decision = evaluateCombatTaskCohesion({
				liveAssigned,
				arrived,
				infantryLive,
				infantryArrived,
				task.minimumViableCount,
				timeout,
				requireInfantryQuorum
			});
			task.arrivedCount = arrived;
			task.missingCount = std::max(0, liveAssigned - arrived);
			task.confirmedDeadCount = std::max(0, task.initialUnitCount - static_cast<int>(task.assignedUnitIds.size()));
			task.requiredQuorumCount = decision.requiredQuorum;
			task.infantryCount = infantryLive;
			task.infantryArrivedCount = infantryArrived;
			task.infantryRequiredQuorumCount = requireInfantryQuorum ? decision.infantryRequiredQuorum : 0;
			task.vehicleCount = vehicleLive;
			task.quorumType = task.raidMode == "vehicle" ? "vehicle_group" : (task.raidMode == "infantry" ? "infantry_group" : "mixed_group");
			task.groupSpread = spread;
			task.cohesionReason = decision.reason;
			task.freshStrategicTargets = countFreshScudStrategicTargets(now);

			if (task.state == CombatTaskState::Attacking)
			{
				task.cohesionReason = "attacking";
				if (shouldLogCombatTaskCohesion(task, now))
				{
					adapterLog(
						"raid_cohesion task=%u state=%s waypoint=%d/%d assigned=%d arrived=%d missing=%d dead=%d spread=%.1f reason=%s",
						task.taskId,
						combatTaskStateName(task.state),
						task.currentWaypointIndex,
						waypointCount,
						liveAssigned,
						arrived,
						task.missingCount,
						task.confirmedDeadCount,
						spread,
						task.cohesionReason.c_str());
					adapterLog(
						"combat_task_state task=%u type=attack state=%s reason=%s",
						task.taskId,
						combatTaskStateName(task.state),
						task.cohesionReason.c_str());
				}
				return true;
			}

			if (decision.shouldFail)
			{
				m_autonomy.combatTaskManager.failTask(task.taskId, decision.reason);
				adapterLog(
					"raid_cohesion task=%u state=failed waypoint=%d/%d assigned=%d arrived=%d missing=%d dead=%d spread=%.1f reason=%s",
					task.taskId,
					waypointIndex,
					waypointCount,
					liveAssigned,
					arrived,
					task.missingCount,
					task.confirmedDeadCount,
					spread,
					decision.reason);
				adapterLog("combat_task_state task=%u type=attack state=failed reason=%s", task.taskId, decision.reason);
				return true;
			}

			bool holdingForCohesion = false;
			if (decision.shouldAdvance)
			{
				if (task.probeState == CombatTaskProbeState::Moving || task.probeState == CombatTaskProbeState::Scouting)
				{
					finishRaidProbe(task, CombatTaskProbeState::Cancelled, "main_quorum_reached", now, raidProbeMaxDistanceFromPoint(task, waypoint.position));
				}
				task.cohesionWaitStartTick = 0u;
				if (waypointIndex + 1 >= waypointCount)
				{
					m_autonomy.combatTaskManager.updateTaskState(task.taskId, CombatTaskState::Attacking, decision.reason);
					issueRaidWaypointCommand(player, task, decision.reason);
					m_autonomy.combatTaskManager.updateTaskState(task.taskId, CombatTaskState::Attacking, "attacking");
					task.cohesionReason = "attacking";
					adapterLog(
						"raid_waypoint task=%u action=complete waypoint=%d x=%.1f y=%.1f radius=%.1f reason=%s",
						task.taskId,
						waypointIndex,
						waypoint.position.x,
						waypoint.position.y,
						waypoint.radius,
						decision.reason);
				}
				else
				{
					++task.currentWaypointIndex;
					m_autonomy.combatTaskManager.updateTaskState(task.taskId, CombatTaskState::Advancing, decision.reason);
					adapterLog(
						"raid_waypoint task=%u action=advance waypoint=%d x=%.1f y=%.1f radius=%.1f reason=%s",
						task.taskId,
						task.currentWaypointIndex,
						task.waypoints[task.currentWaypointIndex].position.x,
						task.waypoints[task.currentWaypointIndex].position.y,
						task.waypoints[task.currentWaypointIndex].radius,
						decision.reason);
					issueRaidWaypointCommand(player, task, decision.reason);
				}
			}
			else
			{
				m_autonomy.combatTaskManager.updateTaskState(task.taskId, CombatTaskState::WaitingForCohesion, decision.reason);
				if (task.cohesionWaitStartTick == 0u)
				{
					task.cohesionWaitStartTick = now;
				}
				if (now - task.lastCommandTick >= 8000u)
				{
					issueRaidWaypointCommand(player, task, "refresh_stage_command");
				}
				holdingForCohesion = true;
			}

			const bool shouldLogCohesion = shouldLogCombatTaskCohesion(task, now);
			if (holdingForCohesion)
			{
				if (task.probeState == CombatTaskProbeState::Moving)
				{
					task.probeState = CombatTaskProbeState::Scouting;
				}
				std::vector<unsigned int> probeUnits;
				if (task.probeState != CombatTaskProbeState::Moving && task.probeState != CombatTaskProbeState::Scouting)
				{
					probeUnits = selectRaidProbeUnitIds(task, waypoint);
				}
				const DWORD cohesionWaitMs = task.cohesionWaitStartTick > 0u ? now - task.cohesionWaitStartTick : 0u;
				const CombatTaskProbeDecision probeDecision = evaluateCombatTaskProbePolicy({
					holdingForCohesion,
					false,
					static_cast<unsigned int>(cohesionWaitMs),
					30000u,
					task.freshStrategicTargets,
					task.probeState,
					task.probeStartedTick,
					20000u,
					task.lastProbeEndTick,
					now,
					15000u,
					static_cast<int>(probeUnits.size())
				});
				const Real probeDistance = raidProbeMaxDistanceFromPoint(task, waypoint.position);
				const bool logProbePolicy = shouldLogCohesion || probeDecision.shouldLaunch || probeDecision.shouldTimeout || probeDecision.shouldCancel || probeDecision.mode != std::string("hold");
				if (logProbePolicy)
				{
					adapterLog(
						"raid_probe_policy task=%u mode=%s reason=%s assigned=%d arrived=%d infantry_arrived=%d required_infantry=%d fresh_targets=%d",
						task.taskId,
						probeDecision.mode,
						probeDecision.reason,
						liveAssigned,
						arrived,
						infantryArrived,
						decision.infantryRequiredQuorum,
						task.freshStrategicTargets);
				}
				if (probeDecision.shouldTimeout)
				{
					finishRaidProbe(task, CombatTaskProbeState::Timeout, probeDecision.reason, now, probeDistance);
				}
				else if (probeDecision.shouldCancel)
				{
					finishRaidProbe(task, probeDecision.mode == std::string("complete") ? CombatTaskProbeState::Complete : CombatTaskProbeState::Cancelled, probeDecision.reason, now, probeDistance);
				}
				else if (probeDecision.shouldLaunch)
				{
					const Coord3D probeTarget = computeRaidProbeTarget(task, waypointIndex);
					if (issueRaidProbeCommand(player, task, probeUnits, probeTarget, probeDecision.reason))
					{
						adapterLog(
							"raid_probe_state task=%u state=%s units=%d distance=%.1f fresh_targets=%d reason=%s",
							task.taskId,
							combatTaskProbeStateName(task.probeState),
							static_cast<int>(task.probeUnitIds.size()),
							raidProbeMaxDistanceFromPoint(task, waypoint.position),
							task.freshStrategicTargets,
							probeDecision.reason);
					}
				}
				else if (task.probeState == CombatTaskProbeState::Moving || task.probeState == CombatTaskProbeState::Scouting)
				{
					adapterLog(
						"raid_probe_state task=%u state=%s units=%d distance=%.1f fresh_targets=%d reason=%s",
						task.taskId,
						combatTaskProbeStateName(task.probeState),
						static_cast<int>(task.probeUnitIds.size()),
						probeDistance,
						task.freshStrategicTargets,
						probeDecision.reason);
				}
			}
			if (shouldLogCohesion && holdingForCohesion)
			{
				adapterLog(
					"raid_waypoint task=%u action=hold waypoint=%d x=%.1f y=%.1f radius=%.1f reason=%s",
					task.taskId,
					waypointIndex,
					waypoint.position.x,
					waypoint.position.y,
					waypoint.radius,
					decision.reason);
			}
			if (shouldLogCohesion)
			{
				adapterLog(
					"raid_cohesion task=%u state=%s waypoint=%d/%d assigned=%d arrived=%d missing=%d dead=%d spread=%.1f reason=%s",
					task.taskId,
					combatTaskStateName(task.state),
					task.currentWaypointIndex,
					waypointCount,
					liveAssigned,
					arrived,
					task.missingCount,
					task.confirmedDeadCount,
					spread,
					task.cohesionReason.c_str());
				adapterLog(
					"combat_task_state task=%u type=attack state=%s reason=%s",
					task.taskId,
					combatTaskStateName(task.state),
					task.cohesionReason.c_str());
			}
			return true;
		}

		void updateCombatTaskLifecycle(Player* player)
		{
			if (player == nullptr || TheGameLogic == nullptr)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			std::vector<CombatTask*> activeTasks = m_autonomy.combatTaskManager.findActiveTasks();

			for (std::size_t i = 0; i < activeTasks.size(); ++i)
			{
				CombatTask* task = activeTasks[i];
				if (task == nullptr)
				{
					continue;
				}

				// Skip terminal tasks - they'll be pruned after grace period
				if (task->state == CombatTaskState::Complete ||
					task->state == CombatTaskState::Failed ||
					task->state == CombatTaskState::Expired)
				{
					continue;
				}

				// 1. Remove dead units from task
				std::vector<unsigned int> deadUnits;
				for (std::size_t j = 0; j < task->assignedUnitIds.size(); ++j)
				{
					const unsigned int unitId = task->assignedUnitIds[j];
					Object* unit = TheGameLogic->findObjectByID(static_cast<ObjectID>(unitId));
					if (unit == nullptr || unit->isEffectivelyDead())
					{
						deadUnits.push_back(unitId);
					}
				}

				if (!deadUnits.empty())
				{
					m_autonomy.combatTaskManager.removeDeadUnits(task->taskId, deadUnits);

					adapterLog(
						"combat_task_state task=%u type=%s state=%s units_dead=%d remaining=%d reason=dead_unit_pruning",
						task->taskId,
						combatTaskTypeName(task->type),
						combatTaskStateName(task->state),
						static_cast<int>(deadUnits.size()),
						static_cast<int>(task->assignedUnitIds.size()));
				}

				// 2. Fail task if below minimum viable count
				const int remaining = static_cast<int>(task->assignedUnitIds.size());
				if (remaining < task->minimumViableCount)
				{
					m_autonomy.combatTaskManager.failTask(task->taskId, "insufficient_units_remaining");

					adapterLog(
						"combat_task_failed task=%u type=%s reason=insufficient_units_remaining remaining=%d minimum=%d",
						task->taskId,
						combatTaskTypeName(task->type),
						remaining,
						task->minimumViableCount);

					adapterLog(
						"combat_task_release task=%u type=%s units=%d reason=task_failed",
						task->taskId,
						combatTaskTypeName(task->type),
						remaining);
					if (task->owner == "artillery_counterbattery")
					{
						adapterLog(
							"mobile_siege_release task=%u target=%u reason=assigned_units_dead",
							task->taskId,
							task->targetObjectId);
						adapterLog(
							"counterbattery_release task=%u target=%u reason=assigned_units_dead",
							task->taskId,
							task->targetObjectId);
						adapterLog(
							"mobile_siege_task task=%u state=failed target=%u target_type=unknown assigned=%d reason=assigned_units_dead",
							task->taskId,
							task->targetObjectId,
							remaining);
						adapterLog(
							"counterbattery_task task=%u state=failed target=%u target_type=unknown assigned=%d reason=assigned_units_dead",
							task->taskId,
							task->targetObjectId,
							remaining);
					}
					continue;
				}

				if (task->type == CombatTaskType::Attack && task->owner == "autonomous_attack")
				{
					updateRaidCohesionTask(player, *task);
					continue;
				}
				if (task->type == CombatTaskType::Scout && task->owner == "dedicated_scouting")
				{
					updateScoutTaskLifecycle(player, *task);
					continue;
				}

				// 3. Expire task if timeout exceeded
				if (task->timeoutTick > 0 && now >= task->timeoutTick)
				{
					const DWORD taskAge = now - task->createdTick;
					m_autonomy.combatTaskManager.expireTask(task->taskId, "timeout_exceeded");

					adapterLog(
						"combat_task_failed task=%u type=%s reason=timeout_exceeded age_ms=%u",
						task->taskId,
						combatTaskTypeName(task->type),
						static_cast<unsigned int>(taskAge));

					adapterLog(
						"combat_task_release task=%u type=%s units=%d reason=task_expired",
						task->taskId,
						combatTaskTypeName(task->type),
						remaining);
					if (task->owner == "artillery_counterbattery")
					{
						adapterLog(
							"mobile_siege_release task=%u target=%u reason=timeout",
							task->taskId,
							task->targetObjectId);
						adapterLog(
							"counterbattery_release task=%u target=%u reason=timeout",
							task->taskId,
							task->targetObjectId);
						adapterLog(
							"mobile_siege_task task=%u state=expired target=%u target_type=unknown assigned=%d reason=timeout",
							task->taskId,
							task->targetObjectId,
							remaining);
						adapterLog(
							"counterbattery_task task=%u state=expired target=%u target_type=unknown assigned=%d reason=timeout",
							task->taskId,
							task->targetObjectId,
							remaining);
					}
					continue;
				}

				// 4. Phase 7.4: Complete counterbattery tasks if WMD target destroyed
				if (task->owner == "scud_counterbattery" && task->targetObjectId > 0)
				{
					Object* targetObj = TheGameLogic->findObjectByID(static_cast<ObjectID>(task->targetObjectId));

					if (targetObj == nullptr || targetObj->isEffectivelyDead())
					{
						m_autonomy.combatTaskManager.completeTask(task->taskId, "target_destroyed");

						adapterLog(
							"scud_counterbattery_complete task=%u target=%u reason=target_destroyed",
							task->taskId,
							task->targetObjectId);

						adapterLog(
							"combat_task_release task=%u type=scud_counterbattery units=%d reason=target_destroyed",
							task->taskId,
							remaining);
						continue;
					}
				}
				if (task->owner == "artillery_counterbattery" && task->targetObjectId > 0)
				{
					Object* targetObj = TheGameLogic->findObjectByID(static_cast<ObjectID>(task->targetObjectId));
					if (targetObj == nullptr || targetObj->isEffectivelyDead())
					{
						m_autonomy.combatTaskManager.completeTask(task->taskId, "target_destroyed");
						adapterLog(
							"mobile_siege_release task=%u target=%u reason=destroyed",
							task->taskId,
							task->targetObjectId);
						adapterLog(
							"counterbattery_release task=%u target=%u reason=destroyed",
							task->taskId,
							task->targetObjectId);
						adapterLog(
							"mobile_siege_task task=%u state=complete target=%u target_type=unknown assigned=%d reason=destroyed",
							task->taskId,
							task->targetObjectId,
							remaining);
						adapterLog(
							"counterbattery_task task=%u state=complete target=%u target_type=unknown assigned=%d reason=destroyed",
							task->taskId,
							task->targetObjectId,
							remaining);
						continue;
					}
				}
			}

			// 5. Prune terminal tasks after grace period (5 seconds)
			const DWORD terminalGracePeriodMs = 5000;
			std::vector<unsigned int> tasksToRemove;

			for (std::size_t i = 0; i < activeTasks.size(); ++i)
			{
				CombatTask* task = activeTasks[i];
				if (task == nullptr)
				{
					continue;
				}

				if (task->state == CombatTaskState::Complete ||
					task->state == CombatTaskState::Failed ||
					task->state == CombatTaskState::Expired)
				{
					const DWORD stateDuration = now - task->lastCommandTick;
					if (stateDuration > terminalGracePeriodMs)
					{
						tasksToRemove.push_back(task->taskId);
					}
				}
			}

			// Remove terminal tasks
			for (std::size_t i = 0; i < tasksToRemove.size(); ++i)
			{
				m_autonomy.combatTaskManager.removeTask(tasksToRemove[i]);
			}
		}

		// Phase 7.4: Update WMD target tracking
		void updateWMDTargets(Player* player)
		{
			if (player == nullptr)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			m_autonomy.wmdTargetTracker.updateWMDTargets(player, now);
		}

		void updateEnemyMemory(Player* player)
		{
			if (player == nullptr || TheGameLogic == nullptr || ThePlayerList == nullptr)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			m_autonomy.enemyMemory.beginUpdate(now);

			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (obj == nullptr || obj->isEffectivelyDead())
				{
					continue;
				}
				if (obj->isKindOf(KINDOF_ALWAYS_VISIBLE) || obj->isKindOf(KINDOF_REBUILD_HOLE))
				{
					continue;
				}

				Player* owner = obj->getControllingPlayer();
				if (owner == nullptr || owner == player || owner == ThePlayerList->getNeutralPlayer())
				{
					continue;
				}
				if (owner->getDefaultTeam() == nullptr || player->getRelationship(owner->getDefaultTeam()) != ENEMIES)
				{
					continue;
				}

				const ObjectShroudStatus shroudStatus = obj->getShroudedStatus(player->getPlayerIndex());
				const bool visible = shroudStatus == OBJECTSHROUD_CLEAR || shroudStatus == OBJECTSHROUD_PARTIAL_CLEAR;
				if (!visible)
				{
					continue;
				}

				const bool isStructure = obj->isKindOf(KINDOF_STRUCTURE);
				if (isStructure && obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					continue;
				}
				if (isStructure && obj->testStatus(OBJECT_STATUS_SOLD))
				{
					continue;
				}

				const Coord3D* position = obj->getPosition();
				const ThingTemplate* objectTemplate = obj->getTemplate();
				if (position == nullptr || objectTemplate == nullptr)
				{
					continue;
				}

				const GameSlot* ownerSlot = findSlotForPlayer(owner);
				EnemyMemoryObservation observation;
				observation.objectId = obj->getID();
				observation.playerIndex = owner->getPlayerIndex();
				observation.team = ownerSlot != nullptr ? ownerSlot->getTeamNumber() : -1;
				observation.templateName = objectTemplate->getName().str();
				observation.isStructure = isStructure;
				observation.isUnit = !observation.isStructure;
				observation.position.x = position->x;
				observation.position.y = position->y;
				observation.position.z = position->z;
				observation.seenTick = now;
				m_autonomy.enemyMemory.observe(observation);
			}

			m_autonomy.enemyMemory.finishUpdate(now);
		}

		// Phase 7.5: Defensive SCUD Storm Construction and Firing
		bool isStoppedStrategicFoundation(UnsignedInt foundationId) const
		{
			const auto it = m_autonomy.state.strategicFoundationHealth.find(foundationId);
			if (it == m_autonomy.state.strategicFoundationHealth.end())
			{
				return false;
			}
			return it->second.stopIssued ||
				it->second.reason == "stopped_stale_no_progress" ||
				it->second.reason == "stopped_no_builder_timeout" ||
				it->second.reason == "stopped_worker_dead" ||
				((it->second.reason == "no_active_builder" || it->second.reason == "worker_dead") &&
				 it->second.recoveryAttempts >= 1 &&
				 it->second.lastProgressTick != 0u &&
				 (::GetTickCount() - it->second.lastProgressTick) >= 60000u);
		}

		int countHealthyScudStormBuildTasks(const std::set<UnsignedInt>& countedFoundationIds)
		{
			int count = 0;
			const std::vector<SpecialTaskReservation*> tasks =
				m_autonomy.taskReservationManager.findBuildTasks("ScudStorm");
			for (SpecialTaskReservation* task : tasks)
			{
				if (task == nullptr)
				{
					continue;
				}
				if (task->state == SpecialTaskState::Complete ||
					task->state == SpecialTaskState::Failed ||
					task->state == SpecialTaskState::Expired)
				{
					continue;
				}
				const UnsignedInt targetId = static_cast<UnsignedInt>(task->targetObjectId);
				if (targetId != 0u)
				{
					if (countedFoundationIds.find(targetId) != countedFoundationIds.end())
					{
						continue;
					}
					if (isStoppedStrategicFoundation(targetId))
					{
						continue;
					}
				}
				++count;
			}
			return count;
		}

		void evaluateDefensiveScudStorm(Player* player)
		{
			if (player == nullptr || TheGameLogic == nullptr)
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			static DWORD s_nextScudStormEvaluationTick = 0u;
			static DWORD s_nextScudStormBuildTick = 0u;
			static DWORD s_nextScudStormFireTick = 0u;
			if (AIControlAdapterIsTickInFuture(s_nextScudStormEvaluationTick, now))
			{
				return;
			}
			s_nextScudStormEvaluationTick = now + 2000u;

			// Check prerequisites
			const bool hasWMDThreat = m_autonomy.wmdTargetTracker.hasActiveWMDThreat();

			// Count existing SCUD Storms
			struct ScudStormContext {
				int liveCount;
				int readyCount;
				int underConstructionCount;
				Object* readyObj;
				AIControlAdapterState* self;
				std::set<UnsignedInt> countedFoundationIds;
			};

			ScudStormContext scudStormCtx = { 0, 0, 0, nullptr, this, std::set<UnsignedInt>() };

			player->iterateObjects([](Object* obj, void* userData) {
				if (obj == nullptr || obj->isEffectivelyDead())
				{
					return;
				}

				ScudStormContext* ctx = static_cast<ScudStormContext*>(userData);
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";

				if (containsIgnoreCase(name, "scudstorm"))
				{
					++(ctx->liveCount);

					// Check if ready to fire (not on cooldown, construction complete)
					const bool underConstruction = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
					if (!underConstruction)
					{
						++(ctx->readyCount);
						if (ctx->readyObj == nullptr)
						{
							ctx->readyObj = obj;
						}
					}
					else
					{
						const UnsignedInt objectId = static_cast<UnsignedInt>(obj->getID());
						if (ctx->self == nullptr || !ctx->self->isStoppedStrategicFoundation(objectId))
						{
							++(ctx->underConstructionCount);
							ctx->countedFoundationIds.insert(objectId);
						}
					}
				}
			}, &scudStormCtx);

			const int liveScudStorms = scudStormCtx.liveCount;
			const int readyScudStorms = scudStormCtx.readyCount;
			const int scudStormBuildTasks = countHealthyScudStormBuildTasks(scudStormCtx.countedFoundationIds);
			const int inProgressScudStorms = scudStormCtx.underConstructionCount + scudStormBuildTasks;
			const int effectiveScudStorms = readyScudStorms + inProgressScudStorms;
			Object* readyScudStorm = scudStormCtx.readyObj;

			// Maintain a standing defensive SCUD Storm baseline. Firing still
			// requires a known WMD target, but production should not wait for one.
			const int desiredScudStorms = 10;
			const std::string reason = hasWMDThreat ? "enemy_wmd_detected" : "defensive_baseline";

			const bool productionNeeded = effectiveScudStorms < desiredScudStorms;
			bool scudStormPrereqReady = false;
			if (productionNeeded)
			{
				for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
				{
					if (obj == nullptr || obj->isEffectivelyDead())
					{
						continue;
					}
					if (obj->getControllingPlayer() != player || !obj->isKindOf(KINDOF_DOZER))
					{
						continue;
					}
					if (!inferScudStormTemplateForPlayer(player, obj).empty())
					{
						scudStormPrereqReady = true;
						break;
					}
				}
			}

			const Money* wallet = player->getMoney();
			const UnsignedInt currentMoney = wallet != nullptr ? wallet->countMoney() : 0u;
			const AIControlAdapterProfilePolicyManager profilePolicyManager;
			const AIControlAdapterProfilePolicyConfig profilePolicyConfig = resolveAutonomyProfilePolicyConfig();
			const UnsignedInt reserveCash = profilePolicyConfig.reserveCash;
			const Int currentZoneCount =
				AIControlAdapterScudStormManager::countSupplyZones(m_autonomy.state.telemetryZones);
			const Int desiredZoneCount = profilePolicyManager.ResolveStrategicSpendDesiredZoneCount(
				profilePolicyConfig,
				currentZoneCount);
			const AIControlAdapterScudStormConstructionPolicyResult buildPolicy =
				AIControlAdapterEvaluateScudStormConstruction({
					scudStormPrereqReady,
					productionNeeded,
					currentMoney,
					reserveCash,
					5000u,
					currentZoneCount,
					desiredZoneCount,
					5,
					inProgressScudStorms,
					1,
					25000u
				});
			AIControlAdapterStrategicSpendEconomyFacts scudSpendEconomy;
			scudSpendEconomy.money = currentMoney;
			scudSpendEconomy.reserveCash = reserveCash;
			scudSpendEconomy.activeWmdThreats = hasWMDThreat ? 1 : 0;
			AIControlAdapterStrategicSpendZoneFacts scudSpendZones;
			scudSpendZones.currentZones = currentZoneCount;
			scudSpendZones.desiredZones = desiredZoneCount;
			const AIControlAdapterStrategicSpendArmyFacts scudSpendArmy;
			const AIControlAdapterStrategicSpendSnapshotBuilder strategicSpendSnapshotBuilder;
			const AIControlAdapterStrategicSpendSnapshotBase scudSpendBase =
				strategicSpendSnapshotBuilder.BuildBase(scudSpendEconomy, scudSpendZones, scudSpendArmy);
			AIControlAdapterStrategicSpendSnapshotOverrides scudSpendOverrides;
			scudSpendOverrides.expansionUrgent = buildPolicy.zoneExpansionUrgent;
			scudSpendOverrides.incomeCritical = currentZoneCount <= 1;
			scudSpendOverrides.reserveDepleted = currentMoney < reserveCash;
			const AIControlAdapterStrategicSpendSnapshot scudSpendSnapshot =
				strategicSpendSnapshotBuilder.Build(scudSpendBase, scudSpendOverrides);
			const AIControlAdapterStrategicSpendManager strategicSpendManager;
			const AIControlAdapterStrategicSpendDecision centralScudSpend =
				strategicSpendManager.Evaluate(
					scudSpendSnapshot,
					hasWMDThreat ? StrategicSpendCategory::DefensiveWmd : StrategicSpendCategory::LuxuryBaseline,
					5000u);
			const bool scudSpendAllowed = buildPolicy.spendAllowed && centralScudSpend.allowed;
			const std::string policyReason =
				scudSpendAllowed && !buildPolicy.highCashOverride ? reason :
					(buildPolicy.spendAllowed ? centralScudSpend.reason : buildPolicy.reason);

			adapterLog(
				"scud_storm_policy desired=%d live=%d in_progress=%d ready=%d production_needed=%d "
				"money=%u reserve=%u cash_float=%u zone_gap=%d max_in_progress=%d spend_allowed=%d reason=%s",
				desiredScudStorms,
				liveScudStorms,
				inProgressScudStorms,
				readyScudStorms,
				productionNeeded ? 1 : 0,
				static_cast<unsigned int>(currentMoney),
				static_cast<unsigned int>(reserveCash),
				static_cast<unsigned int>(buildPolicy.cashFloat),
				buildPolicy.zoneGap,
				buildPolicy.maxInProgress,
				scudSpendAllowed ? 1 : 0,
				policyReason.c_str());
			const StrategicSpendCategory scudSpendCategory =
				hasWMDThreat ? StrategicSpendCategory::DefensiveWmd : StrategicSpendCategory::LuxuryBaseline;
			adapterLog(
				"%s",
				AIControlAdapterStrategicSpendTelemetrySerializer().BuildPolicyLogLine({
					scudSpendCategory,
					static_cast<unsigned int>(currentMoney),
					static_cast<unsigned int>(reserveCash),
					&centralScudSpend
				}).c_str());
			AIControlAdapterStrategicSpendTelemetrySerializer().RecordCategory(
				m_autonomy.state.strategicSpendTelemetry,
				scudSpendCategory,
				5000u,
				centralScudSpend);

			std::vector<AIControlAdapterScudStormStrategicTargetCandidate> strategicCandidates;
			const std::vector<AIControlAdapterScudStormMemorySnapshot> scudStormMemorySnapshots =
				AIControlAdapterScudStormManager::buildMemorySnapshotsFromEnemyMemory(m_autonomy.enemyMemory.getItems());
			strategicCandidates =
				AIControlAdapterScudStormManager::buildStrategicTargetCandidates(scudStormMemorySnapshots, now);

			const bool scudStormFireCooldownActive = AIControlAdapterIsTickInFuture(s_nextScudStormFireTick, now);
			if (readyScudStorm == nullptr)
			{
				adapterLog("scud_storm_target_policy mode=hold reason=no_ready_scud_storm");
			}
			else if (hasWMDThreat)
			{
				const WMDTarget* wmdTarget = m_autonomy.wmdTargetTracker.getHighestPriorityTarget();
				if (wmdTarget != nullptr && wmdTarget->alive)
				{
					adapterLog(
						"scud_storm_target_policy mode=wmd target=%u template=%s reason=enemy_wmd_detected",
						wmdTarget->objectId,
						wmdTarget->templateName.c_str());
					if (!scudStormFireCooldownActive)
					{
						s_nextScudStormFireTick = now + 10000u;

						char requestIdBuffer[96];
						sprintf_s(requestIdBuffer, "scud_storm_wmd_%08X", static_cast<unsigned int>(wmdTarget->objectId));

						nlohmann::json message = {
							{"type", "SessionCommand"},
							{"request_id", std::string(requestIdBuffer)},
							{"cmd", "Game.ScudStormAtPosition"},
							{"args", nlohmann::json::object({
								{"x", wmdTarget->position.x},
								{"y", wmdTarget->position.y},
								{"z", wmdTarget->position.z}
							})}
						};

						if (m_autonomy.state.hasExplicitPlayerIndex)
						{
							message["args"]["player"] = player->getPlayerIndex();
						}

						std::string fireReason;
						const bool fired = executeGameScudStormAtPosition(message, fireReason);

						adapterLog(
							"scud_storm_fire mode=wmd target=%u template=%s issued=%d reason=%s x=%.1f y=%.1f",
							wmdTarget->objectId,
							wmdTarget->templateName.c_str(),
							fired ? 1 : 0,
							fired ? "fired_at_enemy_wmd" : fireReason.c_str(),
							wmdTarget->position.x,
							wmdTarget->position.y);
					}
					else
					{
						adapterLog("scud_storm_target_policy mode=hold reason=cooldown");
					}
				}
			}
			else
			{
				const AIControlAdapterScudStormStrategicTargetResult strategicTarget =
					AIControlAdapterSelectScudStormStrategicTarget({
						readyScudStorm != nullptr,
						false,
						scudStormFireCooldownActive,
						120000u,
						strategicCandidates
					});
				if (strategicTarget.hasTarget)
				{
					adapterLog(
						"scud_storm_target_policy mode=strategic_siege target=%u template=%s kind=%s visible=%d age_ms=%u reason=%s",
						strategicTarget.objectId,
						strategicTarget.templateName.c_str(),
						strategicTarget.targetKind.c_str(),
						strategicTarget.visible ? 1 : 0,
						strategicTarget.ageMs,
						strategicTarget.reason);

					s_nextScudStormFireTick = now + 10000u;
					char requestIdBuffer[96];
					sprintf_s(requestIdBuffer, "scud_storm_siege_%08X", static_cast<unsigned int>(strategicTarget.objectId));

					nlohmann::json message = {
						{"type", "SessionCommand"},
						{"request_id", std::string(requestIdBuffer)},
						{"cmd", "Game.ScudStormAtPosition"},
						{"args", nlohmann::json::object({
							{"x", strategicTarget.x},
							{"y", strategicTarget.y},
							{"z", strategicTarget.z},
							{"target_object_id", strategicTarget.objectId}
						})}
					};

					if (m_autonomy.state.hasExplicitPlayerIndex)
					{
						message["args"]["player"] = player->getPlayerIndex();
					}

					std::string fireReason;
					const bool fired = executeGameScudStormAtPosition(message, fireReason);
					adapterLog(
						"scud_storm_fire mode=strategic_siege target=%u template=%s issued=%d reason=%s x=%.1f y=%.1f",
						strategicTarget.objectId,
						strategicTarget.templateName.c_str(),
						fired ? 1 : 0,
						fired ? strategicTarget.reason : fireReason.c_str(),
						strategicTarget.x,
						strategicTarget.y);
				}
				else
				{
					adapterLog("scud_storm_target_policy mode=hold reason=%s", strategicTarget.reason);
				}
			}

				// Build defensive SCUD Storms up to the standing baseline.
				if (productionNeeded && AIControlAdapterIsTickInFuture(s_nextScudStormBuildTick, now))
				{
					if (hasWMDThreat)
					{
						adapterLog(
							"wmd_construction_blocked reason=build_cooldown prereq=%d money=%u reserve=%u in_progress=%d",
							scudStormPrereqReady ? 1 : 0,
							static_cast<unsigned int>(currentMoney),
							static_cast<unsigned int>(reserveCash),
							inProgressScudStorms);
					}
					return;
				}
				if (productionNeeded && !scudSpendAllowed)
				{
					s_nextScudStormBuildTick = now + 10000u;
					if (hasWMDThreat)
					{
						adapterLog(
							"wmd_construction_blocked reason=%s prereq=%d money=%u reserve=%u in_progress=%d",
							policyReason.c_str(),
							scudStormPrereqReady ? 1 : 0,
							static_cast<unsigned int>(currentMoney),
							static_cast<unsigned int>(reserveCash),
							inProgressScudStorms);
					}
					adapterLog(
						"scud_storm_build command=Game.BuildScudStormSmart issued=0 desired=%d live=%d in_progress=%d reason=%s",
						desiredScudStorms,
					liveScudStorms,
					inProgressScudStorms,
					policyReason.c_str());
				return;
			}

			if (productionNeeded)
			{
				std::vector<AIControlAdapterScudStormZoneThreatSnapshot> scudStormZoneThreats;
				for (const auto& threatPair : m_autonomy.state.zoneThreats)
				{
					AIControlAdapterScudStormZoneThreatSnapshot snapshot;
					snapshot.zoneId = threatPair.first;
					snapshot.lastSeenTick = threatPair.second.lastSeenTick;
					scudStormZoneThreats.push_back(snapshot);
				}
					const AIControlAdapterScudStormPlacementChoice placement =
						AIControlAdapterScudStormManager::choosePlacement(
							m_autonomy.state.telemetryZones,
							scudStormZoneThreats,
							now,
							m_autonomy.state.zoneRadius);
					if (!placement.hasPlacement)
					{
						s_nextScudStormBuildTick = now + 10000u;
						if (hasWMDThreat)
						{
							adapterLog(
								"wmd_construction_blocked reason=no_safe_placement prereq=%d money=%u reserve=%u in_progress=%d",
								scudStormPrereqReady ? 1 : 0,
								static_cast<unsigned int>(currentMoney),
								static_cast<unsigned int>(reserveCash),
								inProgressScudStorms);
						}
						adapterLog(
							"scud_storm_build command=Game.BuildScudStormSmart issued=0 desired=%d live=%d in_progress=%d reason=no_safe_placement",
							desiredScudStorms,
							liveScudStorms,
							inProgressScudStorms);
						return;
					}
					std::vector<AIControlAdapterStrategicFoundationFact> foundationFacts;
				foundationFacts.reserve(m_autonomy.state.strategicFoundationHealth.size());
				for (const auto& foundationPair : m_autonomy.state.strategicFoundationHealth)
				{
					const AutonomyStrategicFoundationState& state = foundationPair.second;
					AIControlAdapterStrategicFoundationFact fact;
					fact.foundationId = foundationPair.first;
					fact.templateName = state.templateName;
					fact.x = state.x;
					fact.y = state.y;
					fact.lastHealth = state.lastHealth;
					fact.nowTick = now;
					fact.firstSeenTick = state.firstSeenTick;
					fact.lastSeenTick = state.lastSeenTick;
					fact.lastProgressTick = state.lastProgressTick;
					fact.recoveryAttempts = state.recoveryAttempts;
					fact.stopIssued = state.stopIssued;
					fact.reason = state.reason;
					foundationFacts.push_back(fact);
				}
				const bool collapseImminent =
					m_autonomy.state.survivalPolicyTelemetry.is_object()
					&& m_autonomy.state.survivalPolicyTelemetry.value("state", std::string()) == "collapse_imminent";
				const AIControlAdapterScudStormRebuildBlockResult rebuildBlock =
					AIControlAdapterStrategicFoundationSurvivalManager().ShouldBlockScudStormRebuild({
						static_cast<float>(placement.x),
						static_cast<float>(placement.y),
						static_cast<unsigned int>(now),
						300000u,
						650.0f,
						collapseImminent,
						foundationFacts
					});
				adapterLog(
					"scud_storm_placement zone=%u role=%s x=%.1f y=%.1f score=%d reason=%s",
						static_cast<unsigned int>(placement.zoneId),
						placement.role.c_str(),
					placement.x,
					placement.y,
					placement.score,
					placement.reason.c_str());
				adapterLog(
					"terrain_placement_choice template=GLAScudStorm zone=%u role=%s x=%.1f y=%.1f reason=%s",
					static_cast<unsigned int>(placement.zoneId),
					placement.role.c_str(),
					placement.x,
					placement.y,
					placement.reason.c_str());
				adapterLog(
					"strategic_placement command=Game.BuildScudStormSmart template=GLAScudStorm role=superweapon source=%s zone_anchor=%u zone_center=(%.1f,%.1f) zone_radius=%.1f strict_zone=0 has_placement=%d score=%d reason=%s tick=%u",
					placement.role.c_str(),
					static_cast<unsigned int>(placement.zoneId),
					placement.x,
					placement.y,
					placement.radius,
					placement.hasPlacement ? 1 : 0,
					placement.score,
					placement.reason.c_str(),
					static_cast<unsigned int>(now));
					if (rebuildBlock.blocked)
					{
						s_nextScudStormBuildTick = now + 15000u;
						if (hasWMDThreat)
						{
							adapterLog(
								"wmd_construction_blocked reason=%s prereq=%d money=%u reserve=%u in_progress=%d",
								rebuildBlock.reason,
								scudStormPrereqReady ? 1 : 0,
								static_cast<unsigned int>(currentMoney),
								static_cast<unsigned int>(reserveCash),
								inProgressScudStorms);
						}
						adapterLog(
							"strategic_foundation_rebuild_blocked template=GLAScudStorm foundation=%u reason=%s",
						static_cast<unsigned int>(rebuildBlock.foundationId),
						rebuildBlock.reason);
					adapterLog(
						"scud_storm_build command=Game.BuildScudStormSmart issued=0 desired=%d live=%d in_progress=%d reason=%s",
						desiredScudStorms,
						liveScudStorms,
						inProgressScudStorms,
						rebuildBlock.reason);
					return;
				}
				nlohmann::json message = {
					{"type", "SessionCommand"},
					{"request_id", std::string("defensive_scud_storm_build")},
					{"cmd", "Game.BuildScudStormSmart"},
					{"args", nlohmann::json::object()}
				};
				if (placement.hasPlacement)
				{
					message["args"]["zone_center"] = nlohmann::json::object({
						{"x", placement.x},
						{"y", placement.y}
					});
					message["args"]["zone_radius"] = placement.radius;
					message["args"]["strict_zone"] = false;
				}
				if (m_autonomy.state.hasExplicitPlayerIndex)
				{
					message["args"]["player_index"] = m_autonomy.state.playerIndex;
				}

				std::string buildReason;
				const bool buildIssued = executeGameBuildScudStormSmart(message, buildReason);
				s_nextScudStormBuildTick = now + (buildIssued ? 15000u : 10000u);

					adapterLog(
						"scud_storm_build command=Game.BuildScudStormSmart issued=%d desired=%d live=%d in_progress=%d reason=%s",
						buildIssued ? 1 : 0,
						desiredScudStorms,
					liveScudStorms,
						inProgressScudStorms,
						buildIssued ? policyReason.c_str() : buildReason.c_str());
					if (hasWMDThreat && !buildIssued)
					{
						adapterLog(
							"wmd_construction_blocked reason=%s prereq=%d money=%u reserve=%u in_progress=%d",
							buildReason.c_str(),
							scudStormPrereqReady ? 1 : 0,
							static_cast<unsigned int>(currentMoney),
							static_cast<unsigned int>(reserveCash),
							inProgressScudStorms);
					}
				}
		}

		void evaluateBattlefieldCounterbattery(Player* player)
		{
			if (player == nullptr || TheGameLogic == nullptr || ThePlayerList == nullptr)
			{
				return;
			}
			const DWORD now = ::GetTickCount();
			if (AIControlAdapterIsTickInFuture(m_autonomy.state.nextCounterbatteryTick, now))
			{
				return;
			}
			m_autonomy.state.nextCounterbatteryTick = now + 3000u;

			struct ArtilleryThreat
			{
				UnsignedInt objectId = 0u;
				std::string templateName;
				Coord3D position;
				bool visible = true;
			};
			std::vector<ArtilleryThreat> threats;
			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (obj == nullptr || obj->isEffectivelyDead() || obj->isKindOf(KINDOF_STRUCTURE))
				{
					continue;
				}
				Player* owner = obj->getControllingPlayer();
				if (owner == nullptr || owner == player || owner == ThePlayerList->getNeutralPlayer())
				{
					continue;
				}
				if (owner->getDefaultTeam() == nullptr || player->getRelationship(owner->getDefaultTeam()) != ENEMIES)
				{
					continue;
				}
				const ObjectShroudStatus shroudStatus = obj->getShroudedStatus(player->getPlayerIndex());
				if (shroudStatus != OBJECTSHROUD_CLEAR && shroudStatus != OBJECTSHROUD_PARTIAL_CLEAR)
				{
					continue;
				}
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				const AIControlAdapterMobileSiegeTemplateResult siegeClassification =
					AIControlAdapterClassifyMobileSiegeTemplate(name, false, true);
				if (AIControlAdapterCounterbatteryManager::shouldLogMobileSiegeDetection(name, siegeClassification))
				{
					adapterLog(
						"mobile_siege_detected object=%u template=%s accepted=%d reason=%s",
						static_cast<unsigned int>(obj->getID()),
						name.c_str(),
						siegeClassification.accepted ? 1 : 0,
						siegeClassification.reason);
				}
				if (!siegeClassification.accepted)
				{
					continue;
				}
				const Coord3D* pos = obj->getPosition();
				if (pos == nullptr)
				{
					continue;
				}
				ArtilleryThreat threat;
				threat.objectId = static_cast<UnsignedInt>(obj->getID());
				threat.templateName = name;
				threat.position = *pos;
				threat.visible = true;
				threats.push_back(threat);
			}
			const DWORD staleSiegeWindowMs = 30000u;
			const std::vector<EnemyMemoryItem>& memoryItems = m_autonomy.enemyMemory.getItems();
			for (const EnemyMemoryItem& item : memoryItems)
			{
				if (item.visible || !item.stale || item.isStructure)
				{
					continue;
				}
				if (item.lastSeenTick == 0u || now - item.lastSeenTick > staleSiegeWindowMs)
				{
					continue;
				}
				const AIControlAdapterMobileSiegeTemplateResult siegeClassification =
					AIControlAdapterClassifyMobileSiegeTemplate(item.templateName, false, true);
				if (AIControlAdapterCounterbatteryManager::shouldLogMobileSiegeDetection(item.templateName, siegeClassification))
				{
					adapterLog(
						"mobile_siege_detected object=%u template=%s accepted=%d reason=%s",
						static_cast<unsigned int>(item.objectId),
						item.templateName.c_str(),
						siegeClassification.accepted ? 1 : 0,
						siegeClassification.reason);
				}
				if (!siegeClassification.accepted)
				{
					continue;
				}
				bool duplicate = false;
				for (const ArtilleryThreat& existing : threats)
				{
					if (existing.objectId == item.objectId)
					{
						duplicate = true;
						break;
					}
				}
				if (duplicate)
				{
					continue;
				}
				ArtilleryThreat threat;
				threat.objectId = item.objectId;
				threat.templateName = item.templateName;
				threat.position.x = item.position.x;
				threat.position.y = item.position.y;
				threat.position.z = item.position.z;
				threat.visible = false;
				threats.push_back(threat);
			}

			int activeCounterbatteryTasks = 0;
			int assignedCounterbatteryUnits = 0;
			const std::vector<CombatTask*> activeTasks = m_autonomy.combatTaskManager.findActiveTasks();
			for (const CombatTask* task : activeTasks)
			{
				if (task == nullptr || task->owner != "artillery_counterbattery")
				{
					continue;
				}
				++activeCounterbatteryTasks;
				assignedCounterbatteryUnits += static_cast<int>(task->assignedUnitIds.size());
			}

			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			std::vector<Object*> availableCounters;
			std::vector<AIControlAdapterCounterbatteryUnitSnapshot> availableCounterSnapshots;
			int liveMobileScuds = 0;
			int liveRocketBuggies = 0;
			int queuedRocketBuggies = 0;
			int armsDealers = 0;
			int palaces = 0;
			for (const AutomationOwnedObjectSnapshot& owned : ownedObjects)
			{
				if (owned.object == nullptr)
				{
					continue;
				}
				if (owned.isArmsDealer && !owned.underConstruction)
				{
					++armsDealers;
				}
				if (owned.isPalace && !owned.underConstruction)
				{
					++palaces;
				}
				if (owned.isScudLauncher)
				{
					++liveMobileScuds;
				}
				if (containsIgnoreCase(owned.name, "rocketbuggy"))
				{
					++liveRocketBuggies;
				}
				if (owned.isArmsDealer && owned.object != nullptr)
				{
					ProductionUpdateInterface* production = owned.object->getProductionUpdateInterface();
					if (production != nullptr && TheThingFactory != nullptr)
					{
						const std::string buggyTemplateName = AIControlAdapterTemplateInferenceService::InferRocketBuggyTemplateForProducer(owned.object);
						if (!buggyTemplateName.empty())
						{
							const ThingTemplate* buggyTemplate = TheThingFactory->findTemplate(AsciiString(buggyTemplateName.c_str()), false);
							if (buggyTemplate != nullptr)
							{
								queuedRocketBuggies += static_cast<int>(production->countUnitTypeInQueue(buggyTemplate));
							}
						}
					}
				}
				AIControlAdapterCounterbatteryUnitSnapshot unitSnapshot;
				unitSnapshot.unitId = static_cast<UnsignedInt>(owned.object->getID());
				unitSnapshot.templateName = owned.name;
				unitSnapshot.isScudLauncher = owned.isScudLauncher;
				const bool suitableCounter = AIControlAdapterCounterbatteryManager::isSuitableCounterUnit(unitSnapshot);
				if (!suitableCounter || owned.underConstruction || !owned.hasAI)
				{
					continue;
				}
				if (m_autonomy.taskReservationManager.isObjectReserved(unitSnapshot.unitId) ||
					m_autonomy.combatTaskManager.isUnitReserved(unitSnapshot.unitId) ||
					isGarrisonReservedUnit(unitSnapshot.unitId))
				{
					continue;
				}
				availableCounters.push_back(owned.object);
				availableCounterSnapshots.push_back(unitSnapshot);
			}
			std::vector<std::size_t> counterOrder;
			for (std::size_t i = 0; i < availableCounters.size(); ++i)
			{
				counterOrder.push_back(i);
			}
			std::stable_sort(counterOrder.begin(), counterOrder.end(), [&](std::size_t a, std::size_t b) -> bool
			{
				return AIControlAdapterCounterbatteryManager::counterUnitPriority(availableCounterSnapshots[a]) <
					AIControlAdapterCounterbatteryManager::counterUnitPriority(availableCounterSnapshots[b]);
			});
			std::vector<Object*> sortedCounters;
			std::vector<AIControlAdapterCounterbatteryUnitSnapshot> sortedCounterSnapshots;
			for (std::size_t idx : counterOrder)
			{
				sortedCounters.push_back(availableCounters[idx]);
				sortedCounterSnapshots.push_back(availableCounterSnapshots[idx]);
			}
			availableCounters.swap(sortedCounters);
			availableCounterSnapshots.swap(sortedCounterSnapshots);

			const ScienceType scudLauncherScience = TheScienceStore != nullptr
				? TheScienceStore->getScienceFromInternalName("SCIENCE_ScudLauncher")
				: SCIENCE_INVALID;
			const bool hasScudLauncherScience = scudLauncherScience != SCIENCE_INVALID
				&& player->hasScience(scudLauncherScience);
			const Money* wallet = player->getMoney();
			const UnsignedInt money = wallet != nullptr ? wallet->countMoney() : 0u;
			const int queuedMobileScuds = 0;
			const int maxCounterbatteryMobileScuds = 2;
			const bool hasBuggyPrerequisites = armsDealers > 0 && palaces > 0 && money >= 900u;
			const bool hasScudProductionPrerequisites =
				armsDealers > 0 && palaces > 0 && hasScudLauncherScience && money >= 1200u;
			const bool hasProductionPrerequisites = hasBuggyPrerequisites || hasScudProductionPrerequisites;
			const AIControlAdapterCounterbatteryPolicyResult policy = AIControlAdapterChooseCounterbatteryPolicy({
				static_cast<int>(threats.size()),
				static_cast<int>(availableCounters.size()),
				activeCounterbatteryTasks,
				hasProductionPrerequisites,
				liveMobileScuds,
				queuedMobileScuds,
				maxCounterbatteryMobileScuds
			});
			const AIControlAdapterRocketBuggyMixResult buggyMix = AIControlAdapterChooseRocketBuggyMix({
				armsDealers > 0 && palaces > 0,
				money >= 900u,
				!threats.empty(),
				liveRocketBuggies,
				queuedRocketBuggies,
				0,
				0,
				liveMobileScuds
			});

			AIControlAdapterCounterbatteryTelemetryInput telemetryInput;
			telemetryInput.activeTasks = activeCounterbatteryTasks;
			telemetryInput.assignedUnits = assignedCounterbatteryUnits;
			telemetryInput.productionNeeded = policy.productionNeeded || buggyMix.productionNeeded;
			telemetryInput.reason = policy.reason;
			for (const ArtilleryThreat& threat : threats)
			{
				AIControlAdapterCounterbatteryThreatSnapshot threatSnapshot;
				threatSnapshot.objectId = threat.objectId;
				threatSnapshot.templateName = threat.templateName;
				threatSnapshot.x = threat.position.x;
				threatSnapshot.y = threat.position.y;
				threatSnapshot.z = threat.position.z;
				threatSnapshot.visible = threat.visible;
				telemetryInput.threats.push_back(threatSnapshot);
			}
			m_autonomy.state.counterbatteryTelemetry =
				AIControlAdapterCounterbatteryManager::buildTelemetry(telemetryInput);

			const ArtilleryThreat* target = threats.empty() ? nullptr : &threats.front();
			adapterLog(
				"mobile_siege_policy target=%u target_type=%s visible=%d desired=%d available=%d assigned=%d reason=%s",
				target != nullptr ? target->objectId : 0u,
				target != nullptr ? target->templateName.c_str() : "none",
				target != nullptr && target->visible ? 1 : 0,
				policy.desiredGroups,
				static_cast<int>(availableCounters.size()),
				assignedCounterbatteryUnits,
				policy.reason);
			adapterLog(
				"counterbattery_policy target=%u target_type=%s desired=%d available=%d assigned=%d reason=%s",
				target != nullptr ? target->objectId : 0u,
				target != nullptr ? target->templateName.c_str() : "none",
				policy.desiredGroups,
				static_cast<int>(availableCounters.size()),
				assignedCounterbatteryUnits,
				policy.reason);

			if ((policy.productionNeeded || buggyMix.productionNeeded) && !AIControlAdapterIsTickInFuture(m_autonomy.state.nextCounterbatteryProductionTick, now))
			{
				const bool preferBuggy = buggyMix.productionNeeded;
				const std::string unitTemplateName = preferBuggy ? "GLAVehicleRocketBuggy" : AIControlAdapterTemplateInferenceService::InferScudLauncherTemplate(player);
				nlohmann::json message = {
					{"type", "SessionCommand"},
					{"request_id", preferBuggy ? std::string("mobile_siege_rocket_buggy") : std::string("counterbattery_scud_launcher")},
					{"cmd", "Game.QueueUnit"},
					{"args", nlohmann::json::object({
						{"producer_kind", "arms_dealer"},
						{"unit_template", unitTemplateName},
						{"count", 1}
					})}
				};
				if (m_autonomy.state.hasExplicitPlayerIndex)
				{
					message["args"]["player"] = player->getPlayerIndex();
				}
				std::string productionReason;
				const bool issued = executeGameQueueUnit(message, productionReason);
				m_autonomy.state.nextCounterbatteryProductionTick = now + (issued ? 20000u : 8000u);
				adapterLog(
					"vehicle_mix_policy scorpions=%d quads=%d buggies=%d queued_buggies=%d desired_buggies=%d reason=%s",
					0,
					0,
					liveRocketBuggies,
					queuedRocketBuggies,
					buggyMix.desiredBuggies,
					preferBuggy ? buggyMix.reason : policy.reason);
				adapterLog(
					"mobile_siege_production unit=%s desired=%d live=%d queued=%d issued=%d reason=%s",
					unitTemplateName.c_str(),
					preferBuggy ? buggyMix.desiredBuggies : maxCounterbatteryMobileScuds,
					preferBuggy ? liveRocketBuggies : liveMobileScuds,
					preferBuggy ? queuedRocketBuggies : queuedMobileScuds,
					issued ? 1 : 0,
					issued ? (preferBuggy ? buggyMix.reason : policy.reason) : productionReason.c_str());
				adapterLog(
					"counterbattery_production unit=%s desired=%d live=%d queued=%d issued=%d reason=%s",
					unitTemplateName.c_str(),
					preferBuggy ? buggyMix.desiredBuggies : maxCounterbatteryMobileScuds,
					preferBuggy ? liveRocketBuggies : liveMobileScuds,
					preferBuggy ? queuedRocketBuggies : queuedMobileScuds,
					issued ? 1 : 0,
					issued ? (preferBuggy ? buggyMix.reason : policy.reason) : productionReason.c_str());
			}

			if (!policy.shouldAssign || target == nullptr)
			{
				return;
			}

			std::vector<unsigned int> assignedIds =
				AIControlAdapterCounterbatteryManager::selectAssignmentIds(
					availableCounterSnapshots,
					policy.desiredAssignedUnits,
					4);
			if (assignedIds.empty())
			{
				return;
			}

			const unsigned int taskId = m_autonomy.combatTaskManager.createTask(
				CombatTaskType::Attack,
				assignedIds,
				target->position,
				"artillery_counterbattery",
				"artillery_" + std::to_string(target->objectId),
				120000);
			CombatTask* task = m_autonomy.combatTaskManager.findTask(taskId);
			if (task != nullptr)
			{
				task->targetObjectId = target->objectId;
			}
			adapterLog(
				"mobile_siege_task task=%u state=assigned target=%u target_type=%s assigned=%d reason=%s",
				taskId,
				target->objectId,
				target->templateName.c_str(),
				static_cast<int>(assignedIds.size()),
				policy.reason);
			adapterLog(
				"counterbattery_task task=%u state=assigned target=%u target_type=%s assigned=%d reason=%s",
				taskId,
				target->objectId,
				target->templateName.c_str(),
				static_cast<int>(assignedIds.size()),
				policy.reason);

			std::vector<ObjectID> objectIds;
			for (unsigned int unitId : assignedIds)
			{
				objectIds.push_back(static_cast<ObjectID>(unitId));
			}
			std::string commandReason;
			const bool commandIssued = executeScopedSelectionCommand(player, objectIds, commandReason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_ATTACKMOVETO);
				if (msg == nullptr)
				{
					commandReason = "message_stream_not_ready";
					return false;
				}
				msg->appendLocationArgument(target->position);
				return true;
			});
			if (commandIssued)
			{
				m_autonomy.combatTaskManager.updateTaskState(taskId, CombatTaskState::Moving, "attack_move_to_artillery");
				m_autonomy.combatTaskManager.updateTaskCommand(taskId, now);
			}
			else
			{
				m_autonomy.combatTaskManager.failTask(taskId, "command_failed");
			}
			adapterLog(
				"mobile_siege_command task=%u target=%u issued=%d command=Game.AttackMove.MobileSiegeCounterbattery reason=%s",
				taskId,
				target->objectId,
				commandIssued ? 1 : 0,
				commandIssued ? "attack_move_to_mobile_siege" : commandReason.c_str());
			adapterLog(
				"counterbattery_command task=%u target=%u issued=%d command=Game.AttackMove.ArtilleryCounterbattery reason=%s",
				taskId,
				target->objectId,
				commandIssued ? 1 : 0,
				commandIssued ? "attack_move_to_artillery" : commandReason.c_str());
			if (!commandIssued)
			{
				adapterLog(
					"mobile_siege_task task=%u state=failed target=%u target_type=%s assigned=%d reason=command_failed",
					taskId,
					target->objectId,
					target->templateName.c_str(),
					static_cast<int>(assignedIds.size()));
				adapterLog(
					"counterbattery_task task=%u state=failed target=%u target_type=%s assigned=%d reason=command_failed",
					taskId,
					target->objectId,
					target->templateName.c_str(),
					static_cast<int>(assignedIds.size()));
			}
		}

		// Phase 7.4: SCUD Counterbattery Assignment
		void evaluateScudCounterbattery(Player* player)
		{
			if (player == nullptr || TheGameLogic == nullptr)
			{
				return;
			}

			// Only evaluate if WMD threat exists
			if (!m_autonomy.wmdTargetTracker.hasActiveWMDThreat())
			{
				return;
			}

			const WMDTarget* wmdTarget = m_autonomy.wmdTargetTracker.getHighestPriorityTarget();
			if (wmdTarget == nullptr || !wmdTarget->alive)
			{
				return;
			}

			// Check if we already have an active counterbattery task for this target
			const std::vector<CombatTask*> activeTasks = m_autonomy.combatTaskManager.findActiveTasks();
			for (std::size_t i = 0; i < activeTasks.size(); ++i)
			{
				const CombatTask* task = activeTasks[i];
				if (task != nullptr &&
					task->owner == "scud_counterbattery" &&
					task->targetObjectId == wmdTarget->objectId)
				{
					// Already have active counterbattery task for this WMD
					return;
				}
			}

			// Collect available SCUD Launchers
			std::vector<Object*> availableScuds;
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || obj->isEffectivelyDead() || userData == nullptr)
				{
					return;
				}

				std::vector<Object*>* scuds = static_cast<std::vector<Object*>*>(userData);
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";

				if (containsIgnoreCase(name, "scudlauncher"))
				{
					scuds->push_back(obj);
				}
			}, &availableScuds);

			// Phase 7.4: Direct strike fallback if no SCUDs available
			if (availableScuds.empty())
			{
				adapterLog(
					"scud_counterbattery_skip target=%u reason=no_scuds_available",
					wmdTarget->objectId);

				// Check if we should send a bounded direct strike instead
				evaluateWMDDirectStrikeFallback(player, wmdTarget);
				return;
			}

			// Filter out SCUDs already reserved for other combat tasks
			std::vector<unsigned int> scudIds;
			for (std::size_t i = 0; i < availableScuds.size(); ++i)
			{
				const unsigned int scudId = availableScuds[i]->getID();
				if (!m_autonomy.combatTaskManager.isUnitReserved(scudId))
				{
					scudIds.push_back(scudId);
				}
			}

			if (scudIds.empty())
			{
				adapterLog(
					"scud_counterbattery_skip target=%u reason=all_scuds_reserved",
					wmdTarget->objectId);
				return;
			}

			// Create counterbattery combat task
			const unsigned int taskId = m_autonomy.combatTaskManager.createTask(
				CombatTaskType::Attack,
				scudIds,
				wmdTarget->position,
				"scud_counterbattery",
				"wmd_threat_" + std::to_string(wmdTarget->objectId),
				180000); // 3 minute timeout

			// Update task to track target object ID
			CombatTask* task = m_autonomy.combatTaskManager.findTask(taskId);
			if (task != nullptr)
			{
				task->targetObjectId = wmdTarget->objectId;
			}

			adapterLog(
				"scud_counterbattery_assigned task=%u target=%u scuds=%d x=%.1f y=%.1f",
				taskId,
				wmdTarget->objectId,
				static_cast<int>(scudIds.size()),
				wmdTarget->position.x,
				wmdTarget->position.y);

			// Issue attack-move command to WMD target
			std::vector<ObjectID> scudObjectIds;
			scudObjectIds.reserve(scudIds.size());
			for (std::size_t i = 0; i < scudIds.size(); ++i)
			{
				scudObjectIds.push_back(static_cast<ObjectID>(scudIds[i]));
			}

			std::string commandReason;
			const bool commandIssued = executeScopedSelectionCommand(player, scudObjectIds, commandReason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_ATTACKMOVETO);
				if (msg == nullptr)
				{
					commandReason = "message_stream_not_ready";
					return false;
				}
				msg->appendLocationArgument(wmdTarget->position);
				return true;
			});

			if (commandIssued)
			{
				m_autonomy.combatTaskManager.updateTaskState(taskId, CombatTaskState::Moving, "attack_command_issued");
				m_autonomy.combatTaskManager.updateTaskCommand(taskId, ::GetTickCount());

				adapterLog(
					"scud_counterbattery_command task=%u target=%u issued=1 scuds=%d reason=attack_move_to_wmd",
					taskId,
					wmdTarget->objectId,
					static_cast<int>(scudIds.size()));
			}
			else
			{
				m_autonomy.combatTaskManager.failTask(taskId, "command_failed");

				adapterLog(
					"scud_counterbattery_failed task=%u target=%u reason=command_failed detail=%s",
					taskId,
					wmdTarget->objectId,
					commandReason.c_str());
			}
		}

		// Phase 7.4: WMD Direct Strike Fallback
		void evaluateWMDDirectStrikeFallback(Player* player, const WMDTarget* wmdTarget)
		{
			if (player == nullptr || wmdTarget == nullptr)
			{
				return;
			}

			// Check if we already have an active direct strike task for this WMD
			const std::vector<CombatTask*> activeTasks = m_autonomy.combatTaskManager.findActiveTasks();
			for (std::size_t i = 0; i < activeTasks.size(); ++i)
			{
				const CombatTask* task = activeTasks[i];
				if (task != nullptr &&
					task->owner == "wmd_direct_strike" &&
					task->targetObjectId == wmdTarget->objectId)
				{
					adapterLog(
						"wmd_direct_strike_skip target=%u reason=active_task_exists",
						wmdTarget->objectId);
					return;
				}
			}

			// Collect available combat units (bounded strike group, not entire army)
			std::vector<Object*> combatUnits;
			collectCombatUnitsForRaid(player, combatUnits, false, "vehicle");

			if (combatUnits.empty())
			{
				adapterLog(
					"wmd_direct_strike_skip target=%u reason=no_combat_units",
					wmdTarget->objectId);
				return;
			}

			// Bounded strike: use up to 15 units (not the entire army)
			const std::size_t maxStrikeUnits = 15;
			const std::size_t strikeCount = std::min(maxStrikeUnits, combatUnits.size());

			if (strikeCount < 5)
			{
				adapterLog(
					"wmd_direct_strike_skip target=%u reason=insufficient_units available=%d min=5",
					wmdTarget->objectId,
					static_cast<int>(strikeCount));
				return;
			}

			// Select units for direct strike
			std::vector<unsigned int> strikeUnitIds;
			for (std::size_t i = 0; i < strikeCount; ++i)
			{
				if (!m_autonomy.combatTaskManager.isUnitReserved(combatUnits[i]->getID()))
				{
					strikeUnitIds.push_back(combatUnits[i]->getID());
				}
			}

			if (strikeUnitIds.size() < 5)
			{
				adapterLog(
					"wmd_direct_strike_skip target=%u reason=too_many_reserved available_unreserved=%d min=5",
					wmdTarget->objectId,
					static_cast<int>(strikeUnitIds.size()));
				return;
			}

			// Create direct strike combat task
			const unsigned int taskId = m_autonomy.combatTaskManager.createTask(
				CombatTaskType::Attack,
				strikeUnitIds,
				wmdTarget->position,
				"wmd_direct_strike",
				"wmd_direct_" + std::to_string(wmdTarget->objectId),
				180000); // 3 minute timeout

			CombatTask* task = m_autonomy.combatTaskManager.findTask(taskId);
			if (task != nullptr)
			{
				task->targetObjectId = wmdTarget->objectId;
			}

			adapterLog(
				"wmd_direct_strike_assigned task=%u target=%u units=%d x=%.1f y=%.1f reason=no_scuds_available",
				taskId,
				wmdTarget->objectId,
				static_cast<int>(strikeUnitIds.size()),
				wmdTarget->position.x,
				wmdTarget->position.y);

			// Issue attack-move command to WMD target
			std::vector<ObjectID> strikeObjectIds;
			strikeObjectIds.reserve(strikeUnitIds.size());
			for (std::size_t i = 0; i < strikeUnitIds.size(); ++i)
			{
				strikeObjectIds.push_back(static_cast<ObjectID>(strikeUnitIds[i]));
			}

			std::string commandReason;
			const bool commandIssued = executeScopedSelectionCommand(player, strikeObjectIds, commandReason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_ATTACKMOVETO);
				if (msg == nullptr)
				{
					commandReason = "message_stream_not_ready";
					return false;
				}
				msg->appendLocationArgument(wmdTarget->position);
				return true;
			});

			if (commandIssued)
			{
				m_autonomy.combatTaskManager.updateTaskState(taskId, CombatTaskState::Moving, "attack_command_issued");
				m_autonomy.combatTaskManager.updateTaskCommand(taskId, ::GetTickCount());

				adapterLog(
					"wmd_direct_strike_command task=%u target=%u issued=1 units=%d reason=attack_move_to_wmd",
					taskId,
					wmdTarget->objectId,
					static_cast<int>(strikeUnitIds.size()));
			}
			else
			{
				m_autonomy.combatTaskManager.failTask(taskId, "command_failed");

				adapterLog(
					"wmd_direct_strike_failed task=%u target=%u reason=command_failed detail=%s",
					taskId,
					wmdTarget->objectId,
					commandReason.c_str());
			}
		}

		void collectCombatUnitsForRaid(
			Player* player,
			std::vector<Object*>& outUnits,
			bool includeScoutPoolTechnicals = false,
			const std::string& raidMode = "mixed_local")
		{
			outUnits.clear();
			if (player == nullptr)
			{
				return;
			}
			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			for (std::size_t i = 0; i < ownedObjects.size(); ++i)
			{
				const AutomationOwnedObjectSnapshot& owned = ownedObjects[i];
				if (owned.isStructure || owned.isDozer || owned.isHarvester)
				{
					continue;
				}
				if (!owned.isInfantry && !owned.isVehicle && !owned.isAircraft)
				{
					continue;
				}
				if (raidMode == "vehicle" && owned.isInfantry)
				{
					const DWORD now = ::GetTickCount();
					const std::string logKey =
						std::string("raid_infantry_skip:") +
						std::to_string(static_cast<unsigned int>(owned.object != nullptr ? owned.object->getID() : 0u)) +
						":long_distance";
					if (owned.object != nullptr && shouldLogScoutReservationSkip(logKey, now, 15000u))
					{
						adapterLog(
							"raid_infantry_skip unit=%u reason=long_distance",
							static_cast<unsigned int>(owned.object->getID()));
					}
					continue;
				}
				if (raidMode == "infantry" && !owned.isInfantry)
				{
					continue;
				}
				if (containsIgnoreCase(owned.name, "radar"))
				{
					continue;
				}
				if (owned.underConstruction || !owned.hasAI || owned.object == nullptr)
				{
					continue;
				}
				if (raidMode == "vehicle")
				{
					const bool suitableVehicle =
						owned.isAircraft ||
						owned.isQuad ||
						owned.isScorpion ||
						containsIgnoreCase(owned.name, "rocketbuggy") ||
						owned.isTechnical ||
						(owned.isVehicle && owned.object->isAbleToAttack());
					if (!suitableVehicle)
					{
						continue;
					}
				}
				if (owned.object != nullptr && isGarrisonReservedUnit(static_cast<UnsignedInt>(owned.object->getID())))
				{
					const DWORD now = ::GetTickCount();
					const std::string logKey =
						std::string("garrison:combat:") +
						std::to_string(static_cast<unsigned int>(owned.object->getID())) +
						":garrison_assignment";
					if (shouldLogScoutReservationSkip(logKey, now))
					{
						adapterLog(
							"combat_skip_reserved_garrison unit=%u reason=garrison_assignment",
							static_cast<unsigned int>(owned.object->getID()));
					}
					continue;
				}
				// Phase 6.2: Exclude units reserved for capture tasks
				if (owned.object != nullptr && m_autonomy.taskReservationManager.isObjectReserved(owned.object->getID()))
				{
					const SpecialTaskReservation* reservation = findActiveCaptureReservationForObject(owned.object->getID());
					if (reservation != nullptr)
					{
						adapterLog(
							"combat_skip_reserved_capture unit=%u task=%u owner=%s target=%u",
							owned.object->getID(),
							reservation->taskId,
							reservation->owner.c_str(),
							reservation->targetObjectId);
					}
					continue;
				}
				// Phase 9.0: Exclude units reserved for combat tasks
				if (owned.object != nullptr && m_autonomy.combatTaskManager.isUnitReserved(owned.object->getID()))
				{
					// Find which task owns this unit for logging
					const DWORD now = ::GetTickCount();
					const std::vector<CombatTask*> activeTasks = m_autonomy.combatTaskManager.findActiveTasks();
					for (std::size_t j = 0; j < activeTasks.size(); ++j)
					{
						const CombatTask* task = activeTasks[j];
						if (task != nullptr)
						{
							for (std::size_t k = 0; k < task->assignedUnitIds.size(); ++k)
							{
								if (task->assignedUnitIds[k] == owned.object->getID())
								{
									const std::string taskType = combatTaskTypeName(task->type);
									const bool scoutReservation = task->type == CombatTaskType::Scout || task->owner == "dedicated_scouting";
									const std::string logKey =
										std::string("combat:") +
										std::to_string(static_cast<unsigned int>(owned.object->getID())) +
										":" + std::to_string(task->taskId) +
										":" + task->owner +
										":" + taskType;
									if (!scoutReservation || shouldLogScoutReservationSkip(logKey, now))
									{
										adapterLog(
											"combat_skip_reserved_combat unit=%u task=%u owner=%s type=%s",
											owned.object->getID(),
											task->taskId,
											task->owner.c_str(),
											taskType.c_str());
									}
									break;
								}
							}
						}
					}
					continue;
				}
				if (!includeScoutPoolTechnicals && isWorkerShuttleTechnicalProtected(player, owned))
				{
					const DWORD now = ::GetTickCount();
					const std::string logKey =
						std::string("worker_shuttle:") +
						std::to_string(static_cast<unsigned int>(owned.object->getID())) +
						":worker_shuttle_reserved";
					if (shouldLogScoutReservationSkip(logKey, now))
					{
						adapterLog(
							"combat_skip_reserved_worker_shuttle unit=%u reason=worker_mobility",
							static_cast<unsigned int>(owned.object->getID()));
					}
					continue;
				}
				if (!includeScoutPoolTechnicals && isScoutPoolTechnicalProtected(player, owned))
				{
					const DWORD now = ::GetTickCount();
					const std::string logKey =
						std::string("pool:") +
						std::to_string(static_cast<unsigned int>(owned.object->getID())) +
						":scout_pool_reserved";
					if (shouldLogScoutReservationSkip(logKey, now))
					{
						adapterLog(
							"combat_skip_reserved_scout_pool unit=%u reason=scout_pool_reserved",
							static_cast<unsigned int>(owned.object->getID()));
					}
					continue;
				}
				outUnits.push_back(owned.object);
			}
		}

		void collectCapturableTargetsForPlayer(Player* player, std::vector<Object*>& outTargets) const
		{
			outTargets.clear();
			if (player == nullptr || TheGameLogic == nullptr)
			{
				return;
			}

			std::vector<SpecialTaskReservation*> captureTasks =
				const_cast<AIControlAdapterTaskReservationManager&>(m_autonomy.taskReservationManager).findCaptureTasks();
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
				// Phase 6.2: Skip targets that already have active capture tasks
				if (AIControlAdapterCaptureManager::IsCaptureTargetReserved(
					captureTasks,
					static_cast<unsigned int>(obj->getID())))
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
			// Phase 6.2: Check durable task reservations instead of simple pending map
			return AIControlAdapterCaptureManager::IsCaptureSourceReserved(
				m_autonomy.taskReservationManager.isObjectReserved(static_cast<unsigned int>(sourceId)),
				isGarrisonReservedUnit(static_cast<unsigned int>(sourceId)));
		}

		/**
		 * Phase 6.2: Find active capture reservation for object (source or target).
		 * Returns task details if object is reserved, nullptr otherwise.
		 * Used for skip logging to show which task is protecting the unit.
		 */
		const SpecialTaskReservation* findActiveCaptureReservationForObject(unsigned int objectId) const
		{
			std::vector<SpecialTaskReservation*> captureTasks =
				const_cast<AIControlAdapterTaskReservationManager&>(m_autonomy.taskReservationManager).findCaptureTasks();
			return AIControlAdapterCaptureManager::FindActiveCaptureReservationForObject(captureTasks, objectId);
		}

		bool isCaptureTargetReserved(const Object* target) const
		{
			if (target == nullptr)
			{
				return false;
			}
			const Int targetId = static_cast<Int>(target->getID());
			if (targetId <= 0)
			{
				return false;
			}
			// Phase 6.2: Check if target is already reserved by an active capture task
			std::vector<SpecialTaskReservation*> captureTasks =
				const_cast<AIControlAdapterTaskReservationManager&>(m_autonomy.taskReservationManager).findCaptureTasks();
			return AIControlAdapterCaptureManager::IsCaptureTargetReserved(
				captureTasks,
				static_cast<unsigned int>(targetId));
		}

		void collectCaptureSourcesForPlayer(Player* player, bool preferIdle, std::vector<Object*>& outSources)
		{
			outSources.clear();
			static DWORD s_nextCaptureSourceSearchLogTick = 0u;
			const DWORD now = ::GetTickCount();
			const bool shouldLogSearch = AIControlAdapterHasTickElapsed(s_nextCaptureSourceSearchLogTick, now);
			if (shouldLogSearch)
			{
				s_nextCaptureSourceSearchLogTick = now + 5000u;
				adapterLog("collect_capture_sources_enter player=%d prefer_idle=%d",
					player != nullptr ? player->getPlayerIndex() : -1, preferIdle ? 1 : 0);
			}
			if (player == nullptr || TheActionManager == nullptr)
			{
				adapterLog("collect_capture_sources_early_return player_null=%d action_mgr_null=%d",
					player == nullptr ? 1 : 0, TheActionManager == nullptr ? 1 : 0);
				return;
			}

			struct CaptureSourceSearchContext
			{
				bool preferIdle;
				int unitCount;
				int withCapturePower;
				int reserved;
				int underConstruction;
				int notIdle;
			} ctx = { preferIdle, 0, 0, 0, 0, 0 };
			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			for (std::size_t i = 0; i < ownedObjects.size(); ++i)
			{
				const AutomationOwnedObjectSnapshot& owned = ownedObjects[i];
				++ctx.unitCount;
				if (!owned.hasCapturePower || owned.object == nullptr)
				{
					continue;
				}
				++ctx.withCapturePower;
				if (isCaptureSourceTemporarilyReserved(owned.object))
				{
					++ctx.reserved;
					continue;
				}
				if (owned.underConstruction)
				{
					++ctx.underConstruction;
					continue;
				}

				AIUpdateInterface* ai = owned.object->getAI();
				const bool isIdle = (ai != nullptr && ai->isIdle() && !ai->isBusy());
				if (ctx.preferIdle && !isIdle)
				{
					++ctx.notIdle;
					continue;
				}
				outSources.push_back(owned.object);
			}

			if (shouldLogSearch)
			{
				adapterLog("capture_source_search player=%d prefer_idle=%d units=%d with_power=%d reserved=%d under_construction=%d not_idle=%d found=%d",
					player->getPlayerIndex(), preferIdle ? 1 : 0, ctx.unitCount, ctx.withCapturePower,
					ctx.reserved, ctx.underConstruction, ctx.notIdle, (int)outSources.size());
			}

			if (!outSources.empty() || !preferIdle)
			{
				return;
			}

			// Retry without idle preference
			ctx.preferIdle = false;
			ctx.reserved = 0;
			ctx.underConstruction = 0;
			for (std::size_t i = 0; i < ownedObjects.size(); ++i)
			{
				const AutomationOwnedObjectSnapshot& owned = ownedObjects[i];
				if (!owned.hasCapturePower || owned.object == nullptr)
				{
					continue;
				}
				if (isCaptureSourceTemporarilyReserved(owned.object))
				{
					++ctx.reserved;
					continue;
				}
				if (owned.underConstruction)
				{
					++ctx.underConstruction;
					continue;
				}
				outSources.push_back(owned.object);
			}

			if (shouldLogSearch)
			{
				adapterLog("capture_source_search_retry player=%d reserved=%d under_construction=%d found=%d",
					player->getPlayerIndex(), ctx.reserved, ctx.underConstruction, (int)outSources.size());
			}
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

		void evaluateLocalWorkerLiquidity()
		{
			if (!isAutonomyModeActive())
			{
				return;
			}
			const DWORD now = ::GetTickCount();
			if (AIControlAdapterIsTickInFuture(m_autonomy.state.nextLocalWorkerLiquidityTick, now))
			{
				return;
			}

			Player* player = resolveAutonomyPlayer();
			if (player == nullptr || !m_autonomy.state.telemetryZones.is_array())
			{
				m_autonomy.state.nextLocalWorkerLiquidityTick = now + 10000u;
				return;
			}

			const Money* wallet = player->getMoney();
			const UnsignedInt money = wallet != nullptr ? wallet->countMoney() : 0u;
			const AIControlAdapterProfilePolicyManager profilePolicyManager;
			const UnsignedInt reserve =
				profilePolicyManager.ResolveReserveCashWithFloor(resolveAutonomyProfilePolicyConfig(), 5000u);
			const UnsignedInt cashFloat = money > reserve ? money - reserve : 0u;
			const int workerCap = 80;
			int globalWorkers = 0;
			std::vector<Object*> producers;
			std::vector<Object*> workers;
			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			for (const AutomationOwnedObjectSnapshot& owned : ownedObjects)
			{
				if (owned.object == nullptr)
				{
					continue;
				}
				if (owned.isDozer)
				{
					++globalWorkers;
					workers.push_back(owned.object);
				}
				if (owned.underConstruction)
				{
					continue;
				}
				if (owned.isSupplyStructure || containsIgnoreCase(owned.name, "commandcenter"))
				{
					producers.push_back(owned.object);
				}
			}

			int strategicTasks = 0;
			for (SpecialTaskReservation* task : m_autonomy.taskReservationManager.findBuildTasks())
			{
				if (task != nullptr &&
					(containsIgnoreCase(task->expectedTemplate, "scudstorm") ||
					 containsIgnoreCase(task->expectedTemplate, "palace")))
				{
					++strategicTasks;
				}
			}

			adapterLog(
				"worker_liquidity_policy global=%d/%d zones=%u cash_float=%u strategic_tasks=%d reason=%s",
				globalWorkers,
				workerCap,
				static_cast<unsigned int>(m_autonomy.state.telemetryZones.size()),
				static_cast<unsigned int>(cashFloat),
				strategicTasks,
				cashFloat >= 3000u ? "cash_healthy" : "cash_reserved");

			const AIControlAdapterLocalWorkerLiquidityResult globalWorkerPolicy = AIControlAdapterChooseLocalWorkerLiquidity({
				globalWorkers,
				workerCap,
				cashFloat,
				0,
				1,
				true
			});
			if (std::string(globalWorkerPolicy.reason) == "cash_reserved" ||
				std::string(globalWorkerPolicy.reason) == "worker_cap_reached")
			{
				m_autonomy.state.nextLocalWorkerLiquidityTick = now + 12000u;
				return;
			}

			const Real zoneRadiusSq = std::max<Real>(160.0f, m_autonomy.state.zoneRadius) * std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
			for (const auto& zone : m_autonomy.state.telemetryZones)
			{
				if (!zone.is_object())
				{
					continue;
				}
				const UnsignedInt zoneId = zone.value("anchor_id", 0u);
				const Real zx = zone.value("center_x", 0.0f);
				const Real zy = zone.value("center_y", 0.0f);
				const bool developed = zone.value("developed", false);
				const bool active = zone.value("active", false);
				const bool hasLocalStrategicTask = strategicTasks > 0 &&
					(zone.value("palaces", 0) > 0 || zone.value("supply_stashes", 0) > 0 || active);
				const int desired = hasLocalStrategicTask ? 3 : (active ? 2 : (developed ? 1 : 0));
				if (desired <= 0)
				{
					continue;
				}

				int localIdle = 0;
				for (Object* worker : workers)
				{
					const Coord3D* pos = worker != nullptr ? worker->getPosition() : nullptr;
					if (pos == nullptr)
					{
						continue;
					}
					const Real dx = pos->x - zx;
					const Real dy = pos->y - zy;
					if (dx * dx + dy * dy <= zoneRadiusSq && isWorkerAvailableForNewBuild(worker))
					{
						++localIdle;
					}
				}

				std::vector<Object*> localProducers;
				for (Object* producer : producers)
				{
					const Coord3D* pos = producer != nullptr ? producer->getPosition() : nullptr;
					if (pos == nullptr)
					{
						continue;
					}
					const Real dx = pos->x - zx;
					const Real dy = pos->y - zy;
					if (dx * dx + dy * dy <= zoneRadiusSq)
					{
						localProducers.push_back(producer);
					}
				}

				const AIControlAdapterLocalWorkerLiquidityResult workerPolicy = AIControlAdapterChooseLocalWorkerLiquidity({
					globalWorkers,
					workerCap,
					cashFloat,
					localIdle,
					desired,
					!localProducers.empty()
				});
				if (!workerPolicy.shouldQueue)
				{
					adapterLog(
						"local_worker_policy zone=%u idle=%d desired=%d producers=%u queued=0 reason=%s",
						zoneId,
						localIdle,
						desired,
						static_cast<unsigned int>(localProducers.size()),
						workerPolicy.reason);
					continue;
				}

				Object* producer = localProducers.front();
				nlohmann::json args = {
					{"count", 1},
					{"producer_kind", "any"},
					{"producer_object_id", static_cast<Int>(producer->getID())}
				};
				if (m_autonomy.state.hasExplicitPlayerIndex)
				{
					args["player_index"] = m_autonomy.state.playerIndex;
				}
				nlohmann::json message = {
					{"type", "SessionCommand"},
					{"request_id", std::string("local_worker_liquidity")},
					{"cmd", "Game.BuildWorker"},
					{"args", args}
				};
				std::string reason;
				const bool ok = executeGameBuildWorker(message, reason);
				adapterLog(
					"local_worker_policy zone=%u idle=%d desired=%d producers=%u queued=%d reason=%s",
					zoneId,
					localIdle,
					desired,
					static_cast<unsigned int>(localProducers.size()),
					ok ? 1 : 0,
					ok ? "queued_local_worker" : reason.c_str());
				m_autonomy.state.nextLocalWorkerLiquidityTick = now + (ok ? 6000u : 10000u);
				return;
			}

			m_autonomy.state.nextLocalWorkerLiquidityTick = now + 10000u;
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
			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			for (std::size_t i = 0; i < ownedObjects.size(); ++i)
			{
				const AutomationOwnedObjectSnapshot& owned = ownedObjects[i];
				if (!owned.underConstruction && owned.isSupplyStructure && owned.object != nullptr)
				{
					stashes.push_back(owned.object);
				}
			}

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
			collectCombatUnitsForRaid(player, combatUnits, false, "vehicle");
			const Int combatUnitCount = static_cast<Int>(combatUnits.size());
			if (combatUnitCount < m_automation.attackRule.minUnits)
			{
				return;
			}

			nlohmann::json args = nlohmann::json::object({
				{"min_units", m_automation.attackRule.minUnits},
				{"group_size", m_automation.attackRule.groupSize},
				{"distance", m_automation.attackRule.distance},
				{"raid_mode", "vehicle"},
				{"defer_command", true}
			});
			if (m_automation.attackRule.hasExplicitPlayerIndex)
			{
				args["player_index"] = m_automation.attackRule.playerIndex;
			}

			// Phase 9.0: Check for equivalent active attack task before issuing
			std::vector<Object*> tempCombatUnits;
			collectCombatUnitsForRaid(player, tempCombatUnits, false, "vehicle");
			if (static_cast<Int>(tempCombatUnits.size()) < m_automation.attackRule.minUnits)
			{
				// Not enough units after filtering combat-reserved
				adapterLog(
					"combat_task_skip type=attack reason=no_eligible_units available=%d min=%d",
					static_cast<int>(tempCombatUnits.size()),
					m_automation.attackRule.minUnits);
				return;
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

			// Phase 9.0: Get attack command result with unit IDs and target
			std::string reason;
			AttackCommandResult attackResult;
			const bool ok = executeGameAttackMoveRaidSmart(message, reason, &attackResult);
			m_automation.attackRule.nextAllowedTick = now + m_automation.attackRule.cooldownMs;

			// Phase 9.0: Create attack combat task with real unit IDs and target
			if (ok && !attackResult.assignedUnitIds.empty())
			{
				// Check for equivalent active attack near same target
				if (m_autonomy.combatTaskManager.hasEquivalentActiveTask(
					CombatTaskType::Attack,
					attackResult.targetPosition,
					500.0f))
				{
					adapterLog(
						"combat_task_skip type=attack reason=equivalent_active target=(%.1f,%.1f)",
						attackResult.targetPosition.x,
						attackResult.targetPosition.y);

					// Release units since we didn't create task for them
					// They'll be available for next cycle
					adapterLog(
						"automation_attack_rule request_id=%s player=%d combat_units=%d ok=0 reason=equivalent_active_task",
						requestIdBuffer,
						player->getPlayerIndex(),
						static_cast<int>(attackResult.assignedUnitIds.size()));
					return;
				}

				const unsigned int attackTaskId = m_autonomy.combatTaskManager.createTask(
					CombatTaskType::Attack,
					attackResult.assignedUnitIds,
					attackResult.targetPosition,
					"autonomous_attack",
					"auto_attack_rule",
					120000); // 2 minute timeout
				CombatTask* attackTask = m_autonomy.combatTaskManager.findTask(attackTaskId);
				if (attackTask != nullptr)
				{
					attackTask->originPosition = attackResult.originPosition;
					attackTask->hasOriginPosition = true;
					attackTask->waypoints = buildDirectRaidWaypoints(attackResult.originPosition, attackResult.targetPosition, 300.0f);
					attackTask->currentWaypointIndex = 0;
					attackTask->currentWaypointStartTick = now;
					attackTask->stageTimeoutMs = 45000u;
					attackTask->state = CombatTaskState::Forming;
					attackTask->raidMode = attackResult.raidMode.empty() ? "vehicle" : attackResult.raidMode;
					attackTask->quorumType = attackTask->raidMode == "vehicle" ? "vehicle_group" : (attackTask->raidMode == "infantry" ? "infantry_group" : "mixed_group");
					attackTask->cohesionReason = "staged_waypoints_created";
				}

				adapterLog(
					"combat_task_assigned task=%u type=attack raid_mode=%s units=%d target=(%.1f,%.1f) reason=attack_automation",
					attackTaskId,
					attackTask != nullptr ? attackTask->raidMode.c_str() : "vehicle",
					static_cast<int>(attackResult.assignedUnitIds.size()),
					attackResult.targetPosition.x,
					attackResult.targetPosition.y);

				if (attackTask != nullptr)
				{
					issueRaidWaypointCommand(player, *attackTask, "created");
				}
			}
			else if (!ok)
			{
				adapterLog(
					"combat_task_skip type=attack reason=%s",
					reason.c_str());
			}
			else if (attackResult.assignedUnitIds.empty())
			{
				adapterLog(
					"combat_task_skip type=attack reason=no_eligible_units");
			}

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
			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			for (std::size_t i = 0; i < ownedObjects.size(); ++i)
			{
				const AutomationOwnedObjectSnapshot& owned = ownedObjects[i];
				if (owned.isSupplyStructure)
				{
					++counts.supplyStashCount;
				}
				if (owned.isBarracks)
				{
					++counts.barracksCount;
				}
				if (owned.isBlackMarket)
				{
					++counts.blackMarketCount;
				}
				if (owned.isArmsDealer)
				{
					++counts.armsCount;
				}
				if (owned.isRadarVan)
				{
					++counts.radarVanCount;
				}
				if (owned.isQuad || owned.isScorpion || owned.isScudLauncher)
				{
					++counts.combatVehicleCount;
				}
			}
			const Int supplyStashCount = counts.supplyStashCount;
			const Int barracksCount = counts.barracksCount;
			const Int blackMarketCount = counts.blackMarketCount;
			const Int armsCount = counts.armsCount;
			const Int radarVanCount = counts.radarVanCount;
			const Int combatVehicleCount = counts.combatVehicleCount;
			const AIControlAdapterProfilePolicyConfig profilePolicyConfig = resolveAutonomyProfilePolicyConfig();
			const bool isBalancedSprawl = profilePolicyConfig.isBalancedSprawl;
			const UnsignedInt reserveCash = profilePolicyConfig.reserveCash;
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
				AIControlAdapterProfilePolicyManager().ResolveCombatArmyCapBase(profilePolicyConfig)
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

		/**
		 * Phase 6.2: Update capture task lifecycle.
		 *
		 * Checks active capture tasks and updates their state based on:
		 * - Source unit validity (dead/alive)
		 * - Target building validity (dead/capturable/friendly)
		 * - Task timeout/expiration
		 *
		 * Marks tasks as Complete when target becomes friendly.
		 * Marks tasks as Failed when source/target dies or becomes invalid.
		 * Marks tasks as Expired when timeout elapses without progress.
		 */
		void updateCaptureTaskLifecycle()
		{
			const DWORD now = ::GetTickCount();
			std::vector<SpecialTaskReservation*> captureTasks = m_autonomy.taskReservationManager.findCaptureTasks();

			for (SpecialTaskReservation* task : captureTasks)
			{
				if (task == nullptr)
				{
					continue;
				}

				// Skip terminal states
				if (task->state == SpecialTaskState::Complete ||
					task->state == SpecialTaskState::Failed ||
					task->state == SpecialTaskState::Expired)
				{
					continue;
				}

				// Find player to check ownership
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
					continue;
				}

				// Check source unit validity
				Object* source = TheGameLogic != nullptr ? TheGameLogic->findObjectByID((ObjectID)task->sourceObjectId) : nullptr;
				if (source == nullptr || source->isEffectivelyDead())
				{
					m_autonomy.taskReservationManager.failTask(task->taskId, "source_dead");
					adapterLog(
						"capture_task_failed task=%u reason=source_dead source=%u target=%u",
						task->taskId,
						task->sourceObjectId,
						task->targetObjectId);
					adapterLog(
						"capture_unit_released task=%u source=%u reason=source_dead",
						task->taskId,
						task->sourceObjectId);
					recordAutonomyTelemetryEvent("capture_task_failed", "source_dead", "source_unit_dead");
					continue;
				}

				// Check target building validity
				Object* target = nullptr;
				if (TheGameLogic != nullptr)
				{
					for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
					{
						if (obj->getID() == task->targetObjectId)
						{
							target = obj;
							break;
						}
					}
				}

				if (target == nullptr || target->isEffectivelyDead())
				{
					m_autonomy.taskReservationManager.failTask(task->taskId, "target_dead");
					adapterLog(
						"capture_task_failed task=%u reason=target_dead source=%u target=%u",
						task->taskId,
						task->sourceObjectId,
						task->targetObjectId);
					adapterLog(
						"capture_unit_released task=%u source=%u reason=target_dead",
						task->taskId,
						task->sourceObjectId);
					recordAutonomyTelemetryEvent("capture_task_failed", "target_dead", "target_building_dead");
					continue;
				}

				// Check if target has been captured (is now friendly)
				if (target->getControllingPlayer() == player)
				{
					m_autonomy.taskReservationManager.completeTask(task->taskId, "target_captured");
					adapterLog(
						"capture_task_complete task=%u target=%u owner=%d",
						task->taskId,
						task->targetObjectId,
						player->getPlayerIndex());
					adapterLog(
						"capture_unit_released task=%u source=%u reason=target_captured",
						task->taskId,
						task->sourceObjectId);
					const Coord3D* targetPos = target->getPosition();
					recordAutonomyTelemetryEvent("capture_task_complete", "target_captured", "building_captured", targetPos);
					continue;
				}

				// Check if target is no longer capturable
				if (!target->isKindOf(KINDOF_CAPTURABLE))
				{
					m_autonomy.taskReservationManager.failTask(task->taskId, "target_not_capturable");
					adapterLog(
						"capture_task_failed task=%u reason=target_not_capturable source=%u target=%u",
						task->taskId,
						task->sourceObjectId,
						task->targetObjectId);
					adapterLog(
						"capture_unit_released task=%u source=%u reason=target_not_capturable",
						task->taskId,
						task->sourceObjectId);
					recordAutonomyTelemetryEvent("capture_task_failed", "target_not_capturable", "building_not_capturable");
					continue;
				}

				// Phase 6.2: Track progress to avoid premature timeout
				const Coord3D* sourcePos = source->getPosition();
				const Coord3D* targetPos = target->getPosition();
				Real currentDistance = -1.0f;
				bool nearTarget = false;
				if (sourcePos != nullptr && targetPos != nullptr)
				{
					const Real dx = targetPos->x - sourcePos->x;
					const Real dy = targetPos->y - sourcePos->y;
					currentDistance = std::sqrt(dx * dx + dy * dy);
					const AIControlAdapterCaptureProgressResult progress =
						AIControlAdapterCaptureManager::ApplyProgressMeasurement(*task, now, currentDistance);
					nearTarget = progress.nearTarget;
					if (progress.loggedProgress)
					{
						adapterLog(
							"capture_task_progress task=%u source=%u target=%u distance=%.1f best_distance=%.1f reason=%s",
							task->taskId,
							task->sourceObjectId,
							task->targetObjectId,
							currentDistance,
							task->bestDistance,
							progress.progressReason);
					}
					if (progress.updateState)
					{
						m_autonomy.taskReservationManager.updateTaskState(
							task->taskId, progress.newState, progress.stateReason);
						adapterLog(
							"capture_task_state task=%u state=%s reason=%s source=%u target=%u distance=%.1f",
							task->taskId,
							progress.stateLogName,
							progress.stateReason,
							task->sourceObjectId,
							task->targetObjectId,
							currentDistance);
					}
				}

				// Phase 6.2: Reissue capture command for stalled but recoverable tasks
				const AIControlAdapterCaptureCommandDecision reissueDecision =
					AIControlAdapterCaptureManager::ChooseCommandReissue(*task, now, currentDistance, nearTarget);

				auto reissueCaptureCommand = [&](const char* recoveryReason, DWORD staleDuration) -> bool
				{
					nlohmann::json args = nlohmann::json::object({
						{"target_object_id", static_cast<Int>(task->targetObjectId)},
						{"source_object_id", static_cast<Int>(task->sourceObjectId)}
					});
					if (m_automation.captureRule.hasExplicitPlayerIndex)
					{
						args["player_index"] = m_automation.captureRule.playerIndex;
					}

					char requestIdBuffer[64];
					sprintf_s(
						requestIdBuffer,
						"reissue_capture_%08X_%08X",
						task->taskId,
						static_cast<unsigned int>(now));

					nlohmann::json message = {
						{"type", "SessionCommand"},
						{"request_id", std::string(requestIdBuffer)},
						{"cmd", "Game.CaptureBuilding"},
						{"args", args}
					};

					std::string reason;
					const bool ok = executeGameCaptureBuilding(message, reason);
					if (ok)
					{
						AIControlAdapterCaptureManager::ApplyCommandReissueSuccess(*task, now);
						adapterLog(
							"capture_task_command task=%u source=%u target=%u action=reissue reason=%s reissue_count=%d distance=%.1f stale_ms=%u",
							task->taskId,
							task->sourceObjectId,
							task->targetObjectId,
							recoveryReason,
							task->commandReissueCount,
							currentDistance,
							staleDuration);
						recordAutonomyTelemetryEvent("capture_task_command", "reissue", recoveryReason, targetPos);
					}
					else
					{
						adapterLog(
							"capture_task_command task=%u source=%u target=%u action=reissue_failed reason=%s recovery_reason=%s",
							task->taskId,
							task->sourceObjectId,
							task->targetObjectId,
							reason.c_str(),
							recoveryReason);
					}
					return ok;
				};

				// Try to reissue command if stalled but not yet expired
				if (reissueDecision.shouldReissue)
				{
					reissueCaptureCommand(reissueDecision.reason, reissueDecision.staleDurationMs);
				}

				// Check for timeout with progress awareness
				// Expire only if truly stalled:
				// - No progress for the stale threshold AND
				// - Overall timeout reached
				const AIControlAdapterCaptureExpirationDecision expirationDecision =
					AIControlAdapterCaptureManager::ChooseExpiration(*task, now, currentDistance, nearTarget);

				if (expirationDecision.shouldExpire)
				{
					m_autonomy.taskReservationManager.expireTask(task->taskId, expirationDecision.reason);
					adapterLog(
						"capture_task_expired task=%u reason=%s source=%u target=%u age_ms=%u stale_ms=%u distance=%.1f reissues=%d",
						task->taskId,
						expirationDecision.reason,
						task->sourceObjectId,
						task->targetObjectId,
						now - task->createdTick,
						expirationDecision.staleDurationMs,
						currentDistance,
						task->commandReissueCount);
					adapterLog(
						"capture_unit_released task=%u source=%u reason=%s",
						task->taskId,
						task->sourceObjectId,
						expirationDecision.reason);
					recordAutonomyTelemetryEvent("capture_task_expired", expirationDecision.reason, "no_movement");
					continue;
				}
			}

			// Prune expired tasks after a grace period
			m_autonomy.taskReservationManager.pruneExpiredTasks(now);
		}

		void evaluateCaptureAutomationRule()
		{
			if (!m_automation.captureRule.enabled)
			{
				if (!m_automation.captureRule.disabledLogEmitted)
				{
					m_automation.captureRule.disabledLogEmitted = true;
					m_automation.captureRule.nextAllowedTick = ::GetTickCount() + std::max<DWORD>(m_automation.captureRule.cooldownMs, 4000u);
					adapterLog("automation_capture_rule_disabled");
				}
				return;
			}
			m_automation.captureRule.disabledLogEmitted = false;

			const DWORD now = ::GetTickCount();
			// Phase 6.2: Update capture task lifecycle instead of pruning simple pending maps
			updateCaptureTaskLifecycle();
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

			{
				std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
				collectOwnedAutomationObjects(player, ownedObjects);
				Int completedBarracks = 0;
				for (std::size_t i = 0; i < ownedObjects.size(); ++i)
				{
					const AutomationOwnedObjectSnapshot& owned = ownedObjects[i];
					if (owned.isBarracks && !owned.underConstruction)
					{
						++completedBarracks;
					}
				}
				if (completedBarracks < 1)
				{
					m_automation.captureRule.nextAllowedTick = now + m_automation.captureRule.cooldownMs;
					adapterLog("automation_capture_rule_skip player=%d reason=capture_barracks_not_ready completed_barracks=%d",
						player->getPlayerIndex(), completedBarracks);
					return;
				}
			}

			const UpgradeTemplate* captureUpgrade = TheUpgradeCenter != nullptr
				? TheUpgradeCenter->findUpgrade("Upgrade_InfantryCaptureBuilding")
				: nullptr;
			if (captureUpgrade == nullptr)
			{
				m_automation.captureRule.nextAllowedTick = now + m_automation.captureRule.cooldownMs;
				adapterLog("automation_capture_rule_skip player=%d reason=capture_upgrade_not_found", player->getPlayerIndex());
				return;
			}
			if (!player->hasUpgradeComplete(captureUpgrade))
			{
				const bool upgradeInProgress = player->hasUpgradeInProduction(captureUpgrade) ? true : false;
				m_automation.captureRule.nextAllowedTick = now + m_automation.captureRule.cooldownMs;
				adapterLog("automation_capture_rule_skip player=%d reason=%s",
					player->getPlayerIndex(),
					upgradeInProgress ? "capture_upgrade_in_progress" : "capture_upgrade_not_ready");
				return;
			}

			// Phase 6.2: Check active capture task count instead of simple pending map
			const Int pendingCount = m_autonomy.taskReservationManager.getCaptureTaskCount();
			const AIControlAdapterCaptureAssignmentBudgetDecision assignmentBudget =
				AIControlAdapterCaptureManager::ChooseAssignmentBudget(
					pendingCount,
					m_automation.captureRule.maxConcurrent);
			if (!assignmentBudget.canAssign)
			{
				adapterLog(
					"automation_capture_rule_skip player=%d reason=%s pending=%d max_concurrent=%d",
					player->getPlayerIndex(),
					assignmentBudget.reason,
					pendingCount,
					m_automation.captureRule.maxConcurrent);
				return;
			}

			std::vector<Object*> targets;
			collectCapturableTargetsForPlayer(player, targets);
			if (targets.empty())
			{
				adapterLog("automation_capture_rule_skip player=%d reason=no_targets", player->getPlayerIndex());
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

			Int assignmentsRemaining = assignmentBudget.assignmentsRemaining;
			Int sentOk = 0;
			Int attemptsRemaining = assignmentBudget.attemptsRemaining;
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

				// Phase 6.2: Create durable capture task reservation
				const unsigned int sourceId = static_cast<unsigned int>(selectedSource->getID());
				const unsigned int targetIdUnsigned = static_cast<unsigned int>(targetId);
				const unsigned int taskId = m_autonomy.taskReservationManager.createCaptureReservation(
					sourceId,
					targetIdUnsigned,
					"capture_automation",
					60000); // 60 second timeout

				const Coord3D* targetPos = target->getPosition();
				adapterLog(
					"capture_task_assigned task=%u source=%u target=%u x=%.1f y=%.1f reason=capture_automation",
					taskId,
					sourceId,
					targetIdUnsigned,
					targetPos != nullptr ? targetPos->x : 0.0f,
					targetPos != nullptr ? targetPos->y : 0.0f);

				recordAutonomyTelemetryEvent("capture_task_assigned", "capture_automation", "capturable_building", targetPos);

				++sentOk;
				--assignmentsRemaining;
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
			rule.disabledLogEmitted = false;
			rule.playerIndex = -1;
			rule.maxConcurrent = 4;
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
			m_automation.captureRule.disabledLogEmitted = false;
			m_automation.captureRule.playerIndex = -1;
			m_automation.captureRule.maxConcurrent = 4;
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
			const std::string displayName = AIControlAdapterUiUtils::UnicodeToUtf8(const_cast<Player*>(player)->getPlayerDisplayName());
			nlohmann::json local = {
				{"player_index", player->getPlayerIndex()},
				{"name", displayName},
				{"player_name_key", KEYNAME(player->getPlayerNameKey()).str()},
				{"side", player->getSide().str()},
				{"base_side", player->getBaseSide().str()},
				{"color", AIControlAdapterUiUtils::FormatColorHex(static_cast<unsigned int>(playerColor))},
				{"color_argb", static_cast<Int>(playerColor)},
				{"color_hex", AIControlAdapterUiUtils::FormatColorHex(static_cast<unsigned int>(playerColor))},
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
					const std::string slotName = AIControlAdapterUiUtils::UnicodeToUtf8(slot->getName());
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

		#include "AIControlAdapterTaskLifecycle.inl"

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
