#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapter.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCombatTask.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTechManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterProductionManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterZoneManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterDefenseManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterEconomyManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterEnemyMemory.h"
#include "GameClient/AIControlAdapter/AIControlAdapterScheduler.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTaskReservation.h"
#include "GameClient/AIControlAdapter/AIControlAdapterWMDTarget.h"

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
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
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

	static std::string inferQuadTemplateForProducerSnapshot(Object* producer)
	{
		if (producer == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
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
		auto isPotentiallyQueueable = [&](const ThingTemplate* tt) -> bool
		{
			if (tt == nullptr)
			{
				return false;
			}
			const CanMakeType canMake = TheBuildAssistant->canMakeUnit(producer, tt);
			return canMake == CANMAKE_OK ||
				canMake == CANMAKE_NO_MONEY ||
				canMake == CANMAKE_QUEUE_FULL ||
				canMake == CANMAKE_PARKING_PLACES_FULL;
		};

		std::vector<std::string> candidates;
		const Player* player = producer->getControllingPlayer();
		const std::string side = player != nullptr ? player->getSide().str() : std::string();
		const std::string baseSide = player != nullptr ? player->getBaseSide().str() : std::string();
		if (containsAsciiLower(side, "gla") || containsAsciiLower(baseSide, "gla"))
		{
			if (containsAsciiLower(side, "slth") || containsAsciiLower(side, "stealth"))
			{
				candidates.push_back("GC_Slth_GLAVehicleQuadCannon");
			}
			if (containsAsciiLower(side, "chem") || containsAsciiLower(side, "toxin"))
			{
				candidates.push_back("GC_Chem_GLAVehicleQuadCannon");
			}
			if (containsAsciiLower(side, "demo"))
			{
				candidates.push_back("Demo_GLAVehicleQuadCannon");
			}
		}
		candidates.push_back("GLAVehicleQuadCannon");
		candidates.push_back("GLAVehicleQuadcannon");
		candidates.push_back("GLAQuadCannon");
		candidates.push_back("GLAVehicleQuad");
		candidates.push_back("GLAQuad");

		for (const std::string& name : candidates)
		{
			const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name.c_str()), false);
			if (tt != nullptr && isPotentiallyQueueable(tt))
			{
				return name;
			}
		}

		for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
		{
			const std::string templateName = tt->getName().str();
			if (!containsAsciiLower(templateName, "quad"))
			{
				continue;
			}
			if (!containsAsciiLower(templateName, "vehicle") && !containsAsciiLower(templateName, "cannon"))
			{
				continue;
			}
			if (isPotentiallyQueueable(tt))
			{
				return templateName;
			}
		}
		return std::string();
	}

	static std::string inferScorpionTemplateForProducerSnapshot(Object* producer)
	{
		if (producer == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
		{
			return std::string();
		}
		const char* candidates[] = {
			"GLAVehicleScorpion",
			"GLAVehicleScorpionTank",
			"GLATankScorpion"
		};
		for (const char* name : candidates)
		{
			const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name), false);
			if (tt != nullptr && TheBuildAssistant->canMakeUnit(producer, tt) == CANMAKE_OK)
			{
				return name;
			}
		}
		return std::string();
	}

	static std::string inferRocketBuggyTemplateForProducerSnapshot(Object* producer)
	{
		if (producer == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
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
		auto isPotentiallyQueueable = [&](const ThingTemplate* tt) -> bool
		{
			if (tt == nullptr)
			{
				return false;
			}
			const CanMakeType canMake = TheBuildAssistant->canMakeUnit(producer, tt);
			return canMake == CANMAKE_OK ||
				canMake == CANMAKE_NO_MONEY ||
				canMake == CANMAKE_QUEUE_FULL ||
				canMake == CANMAKE_PARKING_PLACES_FULL;
		};
		const char* candidates[] = {
			"GLAVehicleRocketBuggy"
		};
		for (const char* name : candidates)
		{
			const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name), false);
			if (tt != nullptr && isPotentiallyQueueable(tt))
			{
				return name;
			}
		}
		for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
		{
			const std::string templateName = tt->getName().str();
			if (containsAsciiLower(templateName, "rocketbuggy") && isPotentiallyQueueable(tt))
			{
				return templateName;
			}
		}
		return std::string();
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

	struct AutonomyStrategicFoundationState
	{
		std::string templateName;
		Real lastHealth = -1.0f;
		DWORD firstSeenTick = 0u;
		DWORD lastSeenTick = 0u;
		DWORD lastProgressTick = 0u;
		DWORD lastRecoveryTick = 0u;
		int recoveryAttempts = 0;
		bool stopIssued = false;
		std::string reason;
	};

	struct AutonomyGarrisonAssignment
	{
		UnsignedInt structureId = 0;
		UnsignedInt zoneAnchorId = 0;
		std::string templateName;
		std::vector<unsigned int> infantryIds;
		UnsignedInt taskId = 0;
		DWORD assignedTick = 0u;
		std::string state;
		std::string reason;
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
		std::unordered_map<UnsignedInt, AutonomyStrategicFoundationState> strategicFoundationHealth;
		std::unordered_map<UnsignedInt, AutonomyGarrisonAssignment> garrisonAssignments;

		nlohmann::json zoneThreatTelemetry;
		nlohmann::json staticDefenseTelemetry;
		nlohmann::json palaceRedundancyTelemetry;
		nlohmann::json garrisonTelemetry;
		nlohmann::json counterbatteryTelemetry;
		nlohmann::json brutalPressureTelemetry;

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
			m_autonomy.state.strategicFoundationHealth.clear();
			m_autonomy.state.garrisonAssignments.clear();
			m_autonomy.state.zoneThreatTelemetry = nlohmann::json::array();
			m_autonomy.state.staticDefenseTelemetry = nlohmann::json::array();
			m_autonomy.state.palaceRedundancyTelemetry = nlohmann::json::array();
			m_autonomy.state.garrisonTelemetry = nlohmann::json::array();
			m_autonomy.state.counterbatteryTelemetry = nlohmann::json::object();
			m_autonomy.state.brutalPressureTelemetry = nlohmann::json::object();
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

		void applyAutonomyRules()
		{
			if (!isAutonomyModeActive())
			{
				clearAutonomyManagedRules();
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
			const bool macroDue = AIControlAdapterHasTickElapsed(m_autonomy.state.nextMacroTick, now);
			const bool productionDue = AIControlAdapterHasTickElapsed(m_autonomy.state.nextProductionTick, now);
			const bool techDue = AIControlAdapterHasTickElapsed(m_autonomy.state.nextTechTick, now);
			const bool guardDue = AIControlAdapterHasTickElapsed(m_autonomy.state.nextGuardTick, now);
			if (!macroDue && !productionDue && !techDue && !guardDue)
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
			if (guardDue && !macroDue && !productionDue && !techDue)
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

				const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
				const Int combatCount = guardCounts.soldiers + guardCounts.rpg + guardCounts.quads + guardCounts.scorpions;
				const DWORD cadenceMs = (profile == "aggressive") ? 5000u : (((profile == "sprawl" || profile == "sprawl_balanced" || profile == "defensive")) ? 7000u : 6000u);
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
				Int quads;
				Int scorpions;
				Int rocketBuggies;
				Int scudLaunchers;
				Int queuedProductionEntries;
				Int queuedQuads;
				Int queuedScorpions;
				Int queuedRocketBuggies;
				Int queuedScudLaunchers;
				Int warFactoryLikeProducers;
			} counts = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

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
							const std::string quadTemplateName = inferQuadTemplateForProducerSnapshot(obj);
							if (!quadTemplateName.empty())
							{
								const ThingTemplate* quadTemplate = TheThingFactory->findTemplate(AsciiString(quadTemplateName.c_str()), false);
								if (quadTemplate != nullptr)
								{
									counts->queuedQuads += static_cast<Int>(production->countUnitTypeInQueue(quadTemplate));
								}
							}

							const std::string scorpionTemplateName = inferScorpionTemplateForProducerSnapshot(obj);
							if (!scorpionTemplateName.empty())
							{
								const ThingTemplate* scorpionTemplate = TheThingFactory->findTemplate(AsciiString(scorpionTemplateName.c_str()), false);
								if (scorpionTemplate != nullptr)
								{
									counts->queuedScorpions += static_cast<Int>(production->countUnitTypeInQueue(scorpionTemplate));
								}
							}

							const std::string buggyTemplateName = inferRocketBuggyTemplateForProducerSnapshot(obj);
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
								const std::string scudLauncherTemplateName = inferAutonomyScudLauncherTemplate(owner);
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
				auto applyRearBias = [&](const AutonomyZone& zone, float& centerX, float& centerY, Real radius) -> void
				{
					// Income and tech structures should prefer rear placement
					if (role == StrategicStructureRole::Income || role == StrategicStructureRole::Tech)
					{
						Real zoneFrontDx = sprawlAxisDx;
						Real zoneFrontDy = sprawlAxisDy;
						const char* ignoredSource = "sprawl_axis";
						resolveZoneFrontDirection(zone, zoneFrontDx, zoneFrontDy, ignoredSource);

						const bool hasAxis = (std::fabs(zoneFrontDx) > 0.0001f) || (std::fabs(zoneFrontDy) > 0.0001f);
						if (hasAxis)
						{
							// Push toward rear (negative offset from front direction)
							const Real rearOffset = zone.isMainBase ? -0.25f : -0.35f;
							centerX += zoneFrontDx * radius * rearOffset;
							centerY += zoneFrontDy * radius * rearOffset;
						}
					}
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

					// Apply rear-side bias
					applyRearBias(zone, choice.zoneCenterX, choice.zoneCenterY, zoneRadius);

					// Determine source based on zone type
					if (zone.isMainBase)
					{
						choice.source = "main_base_scored";
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
						choice.source = "rear_zone_scored";
						choice.reason = "distributed_placement";
					}

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

					// Apply rear-side bias (even for fallback)
					applyRearBias(zone, choice.zoneCenterX, choice.zoneCenterY, zoneRadius);

					choice.source = "fallback_least_saturated";
					choice.reason = "all_zones_poor_score_using_fallback";
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
									threatZone.center.x,
									threatZone.center.y,
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
					m_autonomy.state.telemetryZones.push_back(nlohmann::json::object({
						{"anchor_id", static_cast<UnsignedInt>(zones[i].anchorId)},
						{"anchor_type", zoneAnchorTypeToString(zones[i].anchorType)},
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
					const std::string scudTemplate = inferAutonomyScudLauncherTemplate(player);
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
				else if (hasAxis && cmdString == "Game.BuildPalaceSmart")
				{
					const Real rearOffset = zone.isMainBase ? -0.12f : -0.28f;
					zoneCenter.x += zoneFrontDx * zoneRadius * rearOffset;
					zoneCenter.y += zoneFrontDy * zoneRadius * rearOffset;
					zoneRadius = std::max<Real>(160.0f, zoneRadius * 0.45f);
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
					&& hasCompletedPalace
					&& totalBlackMarkets < std::max<Int>(isBalancedSprawl ? 2 : 1, isBalancedSprawl ? totalSupplyStashes : (totalSupplyStashes / 2))
					&& activeZoneIsDeveloped;
				const bool shouldPrioritizeMarketGrowth =
					isSprawlStyle
					&& hasCompletedPalace
					&& money >= (isBalancedSprawl ? (reserveCash + blackMarketCost) : blackMarketCost)
					&& (!isBalancedSprawl || counts.blackMarketsInProgress < 1)
					&& totalBlackMarkets < sprawlDesiredMarketCount;
				const bool shouldPreserveReserve =
					isBalancedSprawl
					&& (money < reserveCash || counts.blackMarketsInProgress > 0)
					&& hasCompletedPalace
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
				const bool zoneExpansionIsUrgent = AIControlAdapterIsZoneExpansionUrgent({
					stashZoneCount,
					desiredZoneCount,
					5  // zoneGapThreshold
				});
				const Int zoneGap = desiredZoneCount - stashZoneCount;
				const bool reserveProtected = money >= reserveCash;
				const unsigned int cashAboveReserve = reserveProtected ? (money - reserveCash) : 0;
				const bool cashFloatHigh = cashAboveReserve >= 10000u;  // Significant cash float above reserve
				const bool allowUrgentExpansionDespiteReserve = zoneExpansionIsUrgent && cashFloatHigh;

				// Log zone expansion policy before macro decisions
				const char* expansionMode = zoneExpansionIsUrgent ? "urgent" : (stashZoneCount < desiredZoneCount ? "normal" : "hold");
				std::string expansionReason;
				if (stashZoneCount >= desiredZoneCount)
				{
					expansionReason = "target_reached";
				}
				else if (zoneExpansionIsUrgent && cashFloatHigh)
				{
					expansionReason = "large_gap_high_cash";
				}
				else if (zoneExpansionIsUrgent && !cashFloatHigh)
				{
					expansionReason = "large_gap_low_cash";
				}
				else if (shouldPreserveReserve)
				{
					expansionReason = "below_target_reserve_hold";
				}
				else
				{
					expansionReason = "below_target_normal";
				}
				adapterLog(
					"zone_expansion_policy mode=%s current=%d developed=%d desired=%d gap=%d reserve_protected=%d cash_float=%lu reason=%s",
					expansionMode,
					stashZoneCount,
					developedZoneCount,
					desiredZoneCount,
					zoneGap,
					reserveProtected ? 1 : 0,
					static_cast<unsigned long>(cashAboveReserve),
					expansionReason.c_str());

				// Evaluate zone expansion arbitration
				const auto expansionDecision = AIControlAdapterChooseZoneExpansionAction({
					zoneExpansionIsUrgent,
					allowUrgentExpansionDespiteReserve,
					remoteZoneNeedsFollowup,
					stashZoneCount,
					desiredZoneCount,
					counts.supplyStashesInProgress,
					shouldThrottleExtraStashGrowth,
					money,
					reserveCash,
					isBalancedSprawl,
					isBuildAttemptReady("Game.BuildSupplyStashSmart", counts.supplyStashesInProgress)
				});

				// Log zone expansion request decision
				adapterLog(
					"zone_expansion_request command=%s issued=%d reason=%s current=%d developed=%d desired=%d gap=%d "
					"cash_above_reserve=%lu in_progress=%d throttled=%d is_urgent=%d",
					expansionDecision.command != nullptr ? expansionDecision.command : "none",
					expansionDecision.shouldAttemptExpansion ? 1 : 0,
					expansionDecision.reason,
					stashZoneCount,
					developedZoneCount,
					desiredZoneCount,
					zoneGap,
					static_cast<unsigned long>(cashAboveReserve),
					counts.supplyStashesInProgress,
					shouldThrottleExtraStashGrowth ? 1 : 0,
					expansionDecision.isUrgent ? 1 : 0);

				const char* requiredOpeningBuild = AIControlAdapterGetRequiredOpeningBuild({
					counts.supplyStashes,
					counts.barracks,
					counts.armsDealers
				});
				const bool canAttemptBlackMarketNow = AIControlAdapterCanAttemptBlackMarket({
					hasCompletedPalace,
					isBalancedSprawl,
					money,
					reserveCash,
					counts.blackMarkets,
					counts.blackMarketsInProgress
				});
				const bool shouldBuildFirstMarket =
					isSprawlStyle
					&& hasCompletedPalace
					&& totalBlackMarkets < 1
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress)
					&& canAttemptBlackMarketNow;
				const char* ecoRecoveryBuild = AIControlAdapterGetEcoRecoveryBuild({
					isBalancedSprawl,
					totalSupplyStashes,
					counts.palaces,
					totalBlackMarkets,
					sprawlDesiredMarketCount,
					shouldThrottleExtraStashGrowth,
					canAttemptBlackMarketNow
				});
				const bool balancedZoneCanAddBarracks = !isBalancedSprawl || !hasActiveZone || totalZoneBarracks < 1;
				const bool balancedZoneCanAddArmsDealer = !isBalancedSprawl || !hasActiveZone || totalZoneArmsDealers < 1;
				const bool balancedZoneCanAddPalace = !isBalancedSprawl || !hasActiveZone || totalZonePalaces < 1;
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
					adapterLog(
						"static_defense_policy zone=%u role=%s tunnels=%d/%d stingers=%d/%d in_progress=%d reason=%s",
						zoneAnchor,
						staticPolicy.role,
						staticPolicy.effectiveTunnels,
						staticPolicy.desiredTunnels,
						staticPolicy.effectiveStingers,
						staticPolicy.desiredStingers,
						staticInProgress,
						staticPolicy.reason);
					m_autonomy.state.staticDefenseTelemetry.push_back(nlohmann::json::object({
						{"zone", zoneAnchor},
						{"role", staticPolicy.role},
						{"tunnels", staticPolicy.effectiveTunnels},
						{"desired_tunnels", staticPolicy.desiredTunnels},
						{"stingers", staticPolicy.effectiveStingers},
						{"desired_stingers", staticPolicy.desiredStingers},
						{"in_progress", staticInProgress},
						{"reason", staticPolicy.reason}
					}));
					if (staticDefenseZoneIndex < 0 && !zoneExpansionIsUrgent && !shouldPreserveReserve)
					{
						if (staticPolicy.shouldBuildStinger && money >= (isBalancedSprawl ? 1800u : 1200u) && isBuildAttemptReady("Game.BuildStingerSite", counts.stingersInProgress))
						{
							staticDefenseZoneIndex = static_cast<Int>(zoneIdx);
							staticDefenseCommand = "Game.BuildStingerSite";
							staticDefenseReason = staticPolicy.reason;
						}
						else if (staticPolicy.shouldBuildTunnel && money >= (isBalancedSprawl ? 1400u : 900u) && isBuildAttemptReady("Game.BuildTunnelNetwork", counts.tunnelsInProgress))
						{
							staticDefenseZoneIndex = static_cast<Int>(zoneIdx);
							staticDefenseCommand = "Game.BuildTunnelNetwork";
							staticDefenseReason = staticPolicy.reason;
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
					adapterLog(
						"palace_redundancy_policy zone=%u role=%s live=%d desired=%d in_progress=%d spend_allowed=%d reason=%s",
						zoneAnchor,
						palacePolicy.role,
						zc.palaces,
						palacePolicy.desiredZonePalaces,
						zc.palacesInProgress + counts.palacesInProgress,
						palacePolicy.spendAllowed ? 1 : 0,
						palacePolicy.reason);
					m_autonomy.state.palaceRedundancyTelemetry.push_back(nlohmann::json::object({
						{"zone", zoneAnchor},
						{"role", palacePolicy.role},
						{"live", zc.palaces},
						{"desired", palacePolicy.desiredZonePalaces},
						{"in_progress", zc.palacesInProgress + counts.palacesInProgress},
						{"spend_allowed", palacePolicy.spendAllowed},
						{"reason", palacePolicy.reason}
					}));
					if (palaceRedundancyZoneIndex < 0 && palacePolicy.shouldBuild && isBuildAttemptReady("Game.BuildPalaceSmart", counts.palacesInProgress))
					{
						palaceRedundancyZoneIndex = static_cast<Int>(zoneIdx);
						palaceRedundancyReason = palacePolicy.reason;
					}
				}
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
				m_autonomy.state.brutalPressureTelemetry = nlohmann::json::object({
					{"expansion_gap", expansionGap},
					{"main_under_pressure", mainUnderPressure},
					{"static_defense_gap", staticDefenseGap},
					{"garrison_gap", garrisonGap},
					{"local_worker_gap", localWorkerGap},
					{"stale_foundations", staleFoundations},
					{"mobile_siege_threats", mobileSiegeThreats},
					{"reserve_protected", reserveProtected},
					{"cash_float", cashAboveReserve},
					{"chosen_priority", brutalPressure.chosenPriority},
					{"reason", brutalPressure.reason}
				});
				adapterLog(
					"brutal_pressure_policy expansion_gap=%d main_under_pressure=%d static_defense_gap=%d garrison_gap=%d local_worker_gap=%d stale_foundations=%d mobile_siege_threats=%d reserve_protected=%d cash_float=%lu chosen_priority=%s reason=%s",
					expansionGap,
					mainUnderPressure ? 1 : 0,
					staticDefenseGap,
					garrisonGap,
					localWorkerGap,
					staleFoundations,
					mobileSiegeThreats,
					reserveProtected ? 1 : 0,
					static_cast<unsigned long>(cashAboveReserve),
					brutalPressure.chosenPriority,
					brutalPressure.reason);
				std::string chosenCommand = "none";
				if (requiredOpeningBuild != nullptr)
				{
					chosenCommand = requiredOpeningBuild;
						if (std::strcmp(requiredOpeningBuild, "Game.BuildSupplyStashSmart") == 0)
						{
							if (counts.supplyStashesInProgress > 0)
							{
								if (counts.barracks < 1
									&& counts.barracksInProgress < 1
									&& isBuildAttemptReady("Game.BuildBarracksSmart", counts.barracksInProgress)
									&& money >= 600u)
								{
									chosenCommand = "Game.BuildBarracksSmart";
									issued = tryMacroBuildWithFallback("Game.BuildBarracksSmart", false, reason);
									recordBuildAttempt("Game.BuildBarracksSmart", issued, reason);
								}
								else
								{
									reason = "opening_wait_supply_stash";
								}
							}
							else if (isBuildAttemptReady(requiredOpeningBuild, counts.supplyStashesInProgress) && money >= 1200u)
							{
							issued = tryMacroBuildWithFallback(requiredOpeningBuild, false, reason);
							recordBuildAttempt(requiredOpeningBuild, issued, reason);
							if (!issued
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
				else if (shouldBuildFirstMarket)
				{
					chosenCommand = "Game.BuildBlackMarketSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBlackMarketSmart", false, reason);
					recordBuildAttempt("Game.BuildBlackMarketSmart", issued, reason);
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
					&& zoneExpansionIsUrgent
					&& allowUrgentExpansionDespiteReserve
					&& stashZoneCount < desiredZoneCount
					&& counts.supplyStashesInProgress < 1
					&& isBuildAttemptReady("Game.BuildSupplyStashSmart", counts.supplyStashesInProgress)
					&& money >= (isBalancedSprawl ? 2200u : 1800u))
				{
					// Urgent expansion: large zone gap + high cash float
					// This can proceed even if remoteZoneNeedsFollowup because zone deficit is critical
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
					&& !zoneExpansionIsUrgent
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress)
					&& canAttemptBlackMarketNow)
				{
					chosenCommand = "Game.BuildBlackMarketSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBlackMarketSmart", false, reason);
					recordBuildAttempt("Game.BuildBlackMarketSmart", issued, reason);
				}
				else if (staticDefenseZoneIndex >= 0 && !staticDefenseCommand.empty())
				{
					chosenCommand = staticDefenseCommand;
					issued = trySpecificZoneCommand(
						"auto_static_defense",
						staticDefenseCommand.c_str(),
						zones[static_cast<std::size_t>(staticDefenseZoneIndex)],
						nlohmann::json::object(),
						reason);
					recordBuildAttempt(staticDefenseCommand.c_str(), issued, reason);
					if (!issued && reason.empty())
					{
						reason = staticDefenseReason;
					}
				}
				else if (palaceRedundancyZoneIndex >= 0)
				{
					chosenCommand = "Game.BuildPalaceSmart";
					issued = trySpecificZoneCommand(
						"auto_palace_redundancy",
						"Game.BuildPalaceSmart",
						zones[static_cast<std::size_t>(palaceRedundancyZoneIndex)],
						nlohmann::json::object(),
						reason);
					recordBuildAttempt("Game.BuildPalaceSmart", issued, reason);
					if (!issued && reason.empty())
					{
						reason = palaceRedundancyReason;
					}
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
				else if (hasCompletedPalace
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

					// Phase 5.8: Log non-supply zone development if active zone is non-supply
					if (issued && hasActiveZone
						&& (activeZone.anchorType == ZoneAnchorType::CapturedStructure
							|| activeZone.anchorType == ZoneAnchorType::StrategicFoothold
							|| activeZone.anchorType == ZoneAnchorType::MarketFoothold))
					{
						adapterLog(
							"non_supply_zone_development anchor=%u type=%s command=Game.BuildTunnelNetwork issued=1 reason=zone_infrastructure",
							static_cast<unsigned int>(activeZone.anchorId),
							zoneAnchorTypeToString(activeZone.anchorType));
					}
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

					// Phase 5.8: Log non-supply zone development if active zone is non-supply
					if (issued && hasActiveZone
						&& (activeZone.anchorType == ZoneAnchorType::CapturedStructure
							|| activeZone.anchorType == ZoneAnchorType::StrategicFoothold
							|| activeZone.anchorType == ZoneAnchorType::MarketFoothold))
					{
						adapterLog(
							"non_supply_zone_development anchor=%u type=%s command=Game.BuildStingerSite issued=1 reason=zone_defense",
							static_cast<unsigned int>(activeZone.anchorId),
							zoneAnchorTypeToString(activeZone.anchorType));
					}
				}
				else if ((!shouldPreserveReserve || allowUrgentExpansionDespiteReserve)
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

				// Phase 5.8: Non-supply expansion path for strategic/market footholds
				// When supply expansion is blocked and economy is strong, allow Tunnel/Stinger foothold zones
				if (!issued
					&& isSprawlStyle
					&& stashZoneCount < desiredZoneCount
					&& allowUrgentExpansionDespiteReserve)
				{
					// Check if supply expansion repeatedly failed or is blocked
					const bool supplyExpansionBlocked =
						(reason == "no_legal_build_location" || reason == "placement_failed" || reason == "no_worker");

					// Check if Palace and Black Market economy is online
					const bool economyIsStrong =
						hasCompletedPalace
						&& totalBlackMarkets >= 4
						&& money >= 3000u;

					// Check if durable income is high enough
					const int durableIncome = totalBlackMarkets * 200; // Rough estimate: 200/min per market
					const bool hasStrongIncome = durableIncome >= 800;

					if ((supplyExpansionBlocked || hasStrongIncome) && economyIsStrong)
					{
						// Determine foothold type: market_foothold if income is strong, strategic_foothold otherwise
						const ZoneAnchorType footholdType = hasStrongIncome
							? ZoneAnchorType::MarketFoothold
							: ZoneAnchorType::StrategicFoothold;

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
								if (totalTunnels < sprawlTunnelCap
									&& counts.tunnelsInProgress < 1
									&& isBuildAttemptReady("Game.BuildTunnelNetwork", counts.tunnelsInProgress)
									&& money >= 900u)
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
					else if (!economyIsStrong)
					{
						adapterLog(
							"zone_anchor_rejected type=market_foothold reason=economy_not_ready palace=%d markets=%d money=%lu",
							hasCompletedPalace ? 1 : 0,
							totalBlackMarkets,
							static_cast<unsigned long>(money));
					}
				}

				if (!issued && reason.empty())
				{
					if (shouldPreserveReserve && !allowUrgentExpansionDespiteReserve)
					{
						reason = "macro_hold_reserve";
					}
					else if (allowUrgentExpansionDespiteReserve && stashZoneCount < desiredZoneCount)
					{
						// Expansion was urgent and cash was high, but expansion was not issued
						// Report a concrete blocker instead of macro_hold_reserve
						if (counts.supplyStashesInProgress > 0)
						{
							reason = "macro_wait_expansion_in_progress";
						}
						else if (counts.tunnelsInProgress > 0)
						{
							reason = "macro_wait_non_supply_anchor_build_in_progress";
						}
						else if (shouldThrottleExtraStashGrowth)
						{
							reason = "macro_wait_expansion_throttle";
						}
						else if (!hasCompletedPalace || totalBlackMarkets < 4)
						{
							reason = "macro_wait_non_supply_anchor_economy";
						}
						else if (totalTunnels >= sprawlTunnelCap)
						{
							reason = "macro_wait_non_supply_anchor_unavailable";
						}
						else
						{
							reason = "macro_wait_expansion_placement";
						}
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
			const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
			const bool isBalancedSprawl = (profile == "sprawl_balanced");
			const UnsignedInt reserveCash = isBalancedSprawl ? 10000u : 0u;

			// Count Black Markets
			unsigned int completedBlackMarkets = 0;
			unsigned int inProgressBlackMarkets = 0;
			for (std::size_t i = 0; i < ownedObjects.size(); ++i)
			{
				const AutonomyOwnedObjectSnapshot& owned = ownedObjects[i];
				if (!owned.isStructure || owned.object == nullptr)
				{
					continue;
				}
				const ThingTemplate* templ = owned.object->getTemplate();
				if (templ != nullptr)
				{
					const std::string templName = templ->getName().str();
					if (templName == "GLABlackMarket")
					{
						if (owned.underConstruction)
						{
							inProgressBlackMarkets++;
						}
						else
						{
							completedBlackMarkets++;
						}
					}
				}
			}

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
				auto threatSeverity = [](const std::string& level) -> int
				{
					if (level == "critical") return 4;
					if (level == "high") return 3;
					if (level == "medium") return 2;
					if (level == "low") return 1;
					return 0;
				};
				auto isFreshThreat = [&](const AutonomyZoneThreatState& threat) -> bool
				{
					const DWORD ageCutoff = now - threatFreshnessMs;
					return AIControlAdapterHasTickElapsed(ageCutoff, threat.lastSeenTick);
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

					const int severity = threatSeverity(threat.level);
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

				std::vector<Object*> availableCombat;
				collectCombatUnitsForRaid(player, availableCombat);
				const int availableIdleCombat = static_cast<int>(availableCombat.size());
				int activeCriticalAllocations = 0;
				for (auto allocIt = m_autonomy.state.zoneDefenseAllocations.begin(); allocIt != m_autonomy.state.zoneDefenseAllocations.end(); ++allocIt)
				{
					if (allocIt->second.threatSeverity >= 4)
					{
						++activeCriticalAllocations;
					}
				}

				adapterLog(
					"zone_defense_eval candidates=%d active_allocations=%d available_idle=%d threats_scanned=%zu tick=%u",
					static_cast<int>(defenseThreats.size()),
					static_cast<int>(m_autonomy.state.zoneDefenseAllocations.size()),
					availableIdleCombat,
					m_autonomy.state.zoneThreats.size(),
					now);

				for (std::size_t i = 0; i < defenseThreats.size(); ++i)
				{
					const DefenseThreatCandidate& candidate = defenseThreats[i];
					const AutonomyZone* zone = findZoneByAnchor(candidate.zoneAnchor);
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
					if (sourceType == "wmd_strike" && candidate.threat.localEnemyCount <= 0)
					{
						adapterLog(
							"zone_defense_response zone=%u issued=0 reason=wmd_strike_no_local_enemy",
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
						zone != nullptr && zone->isMainBase,
						isDevelopedZone,
						isFrontierZone,
						isActiveZone,
						activeCriticalAllocations > 0
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
					else if (sourceType == "unknown")
					{
						selectedMaxUnits = std::min(selectedMaxUnits, 1);
					}
					if (selectedMaxUnits <= 0)
					{
						adapterLog(
							"zone_defense_allocation zone=%u state=%s threat=%s assigned=%d requested=%d donors=%s reason=%s",
							candidate.zoneAnchor,
							allocation != nullptr ? allocation->state.c_str() : "none",
							candidate.threat.level.c_str(),
							existingAssigned,
							allocationDecision.requestedNewAssignments,
							"none",
							budget.reason);
						continue;
					}

					selectedThreat = &defenseThreats[i];
					selectedExistingAssigned = existingAssigned;
					selectedBudget = budget;
					selectedAllocationDecision = allocationDecision;
					selectedDonors = donorIdsForLog(candidate.zoneAnchor, candidate.severity);
					selectedReason = sourceType == "artillery_attack"
						? "artillery_counterbattery"
						: (sourceType == "unknown" ? "unknown_limited" : (sourceType == "unit_attack" ? "unit_attack" : allocationDecision.reason));
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

					Intent defenseIntent;
					defenseIntent.category = IntentCategory::DEFENSE_RESPONSE;
					defenseIntent.priority = highestSeverity >= 4 ? IntentPriority::CRITICAL : IntentPriority::HIGH;
					defenseIntent.commandName = "Game.AttackMove.DefendZoneSmart";
					defenseIntent.targetName = "threatened_zone";
					defenseIntent.reason = allocationDecision.shouldReinforce ? "zone_defense_reinforce" : "zone_under_attack";

					defenseIntent.executeFunc = [&, threatenedZoneAnchor, threatPositionX, threatPositionY, threatLevel, highestSeverity, defenseResponseCooldownMs, maxUnits, existingAssigned, budget, allocationDecision, donors, allocationReason](std::string& resultReason) -> bool {
						nlohmann::json message = nlohmann::json::object();
						message["type"] = "SessionCommand";
						message["cmd"] = "Game.AttackMove.DefendZoneSmart";
						message["args"] = nlohmann::json::object();
						message["args"]["target_x"] = threatPositionX;
						message["args"]["target_y"] = threatPositionY;
						message["args"]["zone_anchor"] = threatenedZoneAnchor;
						message["args"]["max_units"] = maxUnits;

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

				// Phase 6.3: Compute armyCap for logging (same logic as later telemetry)
				const Int armyCapForLog = AIControlAdapterGetEffectiveArmyCap({
					isBalancedSprawl,
					money,
					static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute)),
					counts.barracks,
					counts.armsDealers,
					isBalancedSprawl ? 100 : 9999
				});

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
				if (buggyMix.productionNeeded
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
				if (!scudCounterbatteryOverride && prodIntent.shouldProduce && economyPolicy.combatSpendingMode == CombatSpendingMode::BlockedExceptDefense)
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
					// Capture production execution logic in callback
					schedulerIntent.executeFunc = [&, prodIntent, producerSnapshots](std::string& resultReason) -> bool {
						if (prodIntent.producerObjectId == -1)
						{
							// Use "all" command (all eligible producers)
							return tryCommand("auto_prod", prodIntent.commandName.c_str(),
								nlohmann::json::object({ {"count", 1} }), resultReason);
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

							if (std::strcmp(prodIntent.commandName.c_str(), "Game.QueueQuadsAllWarFactories") == 0)
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
								if (!issued && resultReason != "no_money")
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
				const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
				if (profile == "sprawl" || profile == "sprawl_balanced" || profile == "tech")
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
						const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
						const bool isBalancedSprawl = (profile == "sprawl_balanced");
						const Int armyCap = AIControlAdapterGetEffectiveArmyCap({
							isBalancedSprawl,
							money,
							static_cast<int>(std::floor(m_autonomy.state.smoothedNetCashPerMinute)),
							counts.barracks,
							counts.armsDealers,
							isBalancedSprawl ? 100 : 9999
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
				const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
				if (profile == "sprawl" || profile == "sprawl_balanced" || profile == "tech")
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
			result["debug_draw"] = m_autonomy.state.debugDrawEnabled;
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
			result["combat_tasks"] = nlohmann::json::object({
				{"active_total", m_autonomy.combatTaskManager.getActiveTaskCount()},
				{"active_attack", m_autonomy.combatTaskManager.getAttackTaskCount()},
				{"active_defense", m_autonomy.combatTaskManager.getDefenseTaskCount()},
				{"active_guard", m_autonomy.combatTaskManager.getGuardTaskCount()},
				{"total_assigned_units", m_autonomy.combatTaskManager.getTotalAssignedUnitCount()}
			});

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
					{"reason", hasWMDThreat ? "enemy_wmd_detected" : "defensive_baseline"}
				})},
				{"mobile_scud_launchers", nlohmann::json::object({
					{"live", liveScuds},
					{"queued", queuedScuds},
					{"baseline_reserve_enabled", false}
				})}
			});
			nlohmann::json strategicFoundations = nlohmann::json::array();
			for (const auto& pair : m_autonomy.state.strategicFoundationHealth)
			{
				const AutonomyStrategicFoundationState& state = pair.second;
				strategicFoundations.push_back(nlohmann::json::object({
					{"foundation_id", pair.first},
					{"template", state.templateName},
					{"last_health", state.lastHealth},
					{"last_seen_tick", state.lastSeenTick},
					{"last_progress_tick", state.lastProgressTick},
					{"no_progress_ms", state.lastProgressTick != 0u ? telemetryNow - state.lastProgressTick : 0u},
					{"recovery_attempts", state.recoveryAttempts},
					{"stop_issued", state.stopIssued},
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

		nlohmann::json buildAutonomyZonesSnapshot()
		{
			Player* player = resolveAutonomyPlayer();
			updateEnemyMemory(player);

			nlohmann::json zones = nlohmann::json::array();
			const Real radius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
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

		bool isUsefulGarrisonStructure(Object* obj) const
		{
			if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_STRUCTURE))
			{
				return false;
			}
			const ThingTemplate* tt = obj->getTemplate();
			const std::string name = tt != nullptr ? tt->getName().str() : "";
			return isPalaceTemplateName(name) || obj->isKindOf(KINDOF_GARRISONABLE_UNTIL_DESTROYED);
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
				msg->appendObjectIDArgument(structureId);
				return true;
			});
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
				else if (structure->getControllingPlayer() != player)
				{
					releaseReason = "enemy_owned";
				}
				else if (!isUsefulGarrisonStructure(structure))
				{
					releaseReason = "not_useful";
				}

				if (!releaseReason.empty())
				{
					if (assignment.taskId != 0u)
					{
						m_autonomy.combatTaskManager.expireTask(assignment.taskId, releaseReason);
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

				assignment.state = "assigned";
				m_autonomy.state.garrisonTelemetry.push_back(nlohmann::json::object({
					{"structure_id", assignment.structureId},
					{"template", assignment.templateName},
					{"zone_id", assignment.zoneAnchorId},
					{"assigned_infantry", static_cast<int>(assignment.infantryIds.size())},
					{"entered", 0},
					{"state", assignment.state},
					{"reason", assignment.reason}
				}));
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
				const bool developed = zoneJson.value("developed", false);
				const bool anchor =
					zoneJson.value("palaces", 0) > 0 ||
					zoneJson.value("black_markets", 0) > 0 ||
					zoneJson.value("anchor_type", std::string("")) == "strategic_foothold" ||
					zoneJson.value("anchor_type", std::string("")) == "market_foothold";
				const auto threatIt = m_autonomy.state.zoneThreats.find(zone.anchorId);
				const bool repeatedAttack = threatIt != m_autonomy.state.zoneThreats.end()
					&& (now - threatIt->second.lastSeenTick) <= 60000u;
				zone.useful = zone.active || developed || anchor || repeatedAttack;
				if (zone.useful)
				{
					usefulZones.push_back(zone);
				}
			}

			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			std::vector<Object*> availableInfantry;
			int totalInfantry = 0;
			for (const AutomationOwnedObjectSnapshot& owned : ownedObjects)
			{
				if (owned.object == nullptr || !owned.isInfantry || owned.isStructure || owned.underConstruction)
				{
					continue;
				}
				++totalInfantry;
				const UnsignedInt id = static_cast<UnsignedInt>(owned.object->getID());
				if (!owned.hasAI
					|| owned.isDozer
					|| owned.isHarvester
					|| owned.hasCapturePower
					|| m_autonomy.taskReservationManager.isObjectReserved(id)
					|| m_autonomy.combatTaskManager.isUnitReserved(id)
					|| isGarrisonReservedUnit(id))
				{
					continue;
				}
				availableInfantry.push_back(owned.object);
			}
			std::stable_sort(availableInfantry.begin(), availableInfantry.end(), [](Object* a, Object* b) -> bool
			{
				const ThingTemplate* ta = a != nullptr ? a->getTemplate() : nullptr;
				const ThingTemplate* tb = b != nullptr ? b->getTemplate() : nullptr;
				const std::string an = ta != nullptr ? ta->getName().str() : "";
				const std::string bn = tb != nullptr ? tb->getName().str() : "";
				const int ap = containsIgnoreCase(an, "rpg") ? 0 : (containsIgnoreCase(an, "rebel") ? 1 : 2);
				const int bp = containsIgnoreCase(bn, "rpg") ? 0 : (containsIgnoreCase(bn, "rebel") ? 1 : 2);
				return ap < bp;
			});

			const int infantryReserve = 2;
			int structuresSeen = 0;
			int structuresFilled = 0;
			int desiredInfantryTotal = 0;
			int assignedThisTick = 0;
			std::string policyReason = usefulZones.empty() ? "no_useful_zones" : "no_candidate";
			const Real zoneRadiusSq = std::max<Real>(220.0f, m_autonomy.state.zoneRadius * 1.35f) *
				std::max<Real>(220.0f, m_autonomy.state.zoneRadius * 1.35f);

			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (obj == nullptr || obj->getControllingPlayer() != player || !isUsefulGarrisonStructure(obj))
				{
					continue;
				}
				const Coord3D* pos = obj->getPosition();
				if (pos == nullptr)
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
					continue;
				}

				++structuresSeen;
				const ThingTemplate* tt = obj->getTemplate();
				const std::string templateName = tt != nullptr ? tt->getName().str() : "";
				const int desiredForStructure = isPalaceTemplateName(templateName) ? 3 : 2;
				desiredInfantryTotal += desiredForStructure;
				const UnsignedInt structureId = static_cast<UnsignedInt>(obj->getID());
				const auto existing = m_autonomy.state.garrisonAssignments.find(structureId);
				const int existingInfantry = existing != m_autonomy.state.garrisonAssignments.end()
					? static_cast<int>(existing->second.infantryIds.size())
					: 0;
				if (existingInfantry >= desiredForStructure)
				{
					++structuresFilled;
					continue;
				}
				if (assignedThisTick > 0)
				{
					policyReason = "cooldown_one_assignment";
					continue;
				}
				const int assignable = std::max<int>(0, static_cast<int>(availableInfantry.size()) - infantryReserve);
				const int missing = desiredForStructure - existingInfantry;
				const int toAssign = std::min<int>(missing, std::min<int>(3, assignable));
				if (toAssign <= 0)
				{
					policyReason = "infantry_reserved";
					continue;
				}

				std::vector<Object*> selectedInfantry;
				std::vector<unsigned int> selectedIds;
				for (int i = 0; i < toAssign; ++i)
				{
					Object* unit = availableInfantry[static_cast<std::size_t>(i)];
					selectedInfantry.push_back(unit);
					selectedIds.push_back(static_cast<unsigned int>(unit->getID()));
				}

				std::string commandReason;
				const bool issued = issueGarrisonCommand(player, obj, selectedInfantry, commandReason);
				AutonomyGarrisonAssignment assignment;
				assignment.structureId = structureId;
				assignment.zoneAnchorId = nearestZone->anchorId;
				assignment.templateName = templateName;
				assignment.infantryIds = selectedIds;
				assignment.assignedTick = now;
				assignment.state = issued ? "assigned" : "failed";
				assignment.reason = issued ? "enter_command_issued" : commandReason;
				if (issued)
				{
					assignment.taskId = m_autonomy.combatTaskManager.createTask(
						CombatTaskType::Guard,
						selectedIds,
						*pos,
						"garrison",
						"garrison_" + std::to_string(structureId),
						180000);
					m_autonomy.combatTaskManager.updateTaskState(assignment.taskId, CombatTaskState::Moving, "enter_command_issued");
					m_autonomy.combatTaskManager.updateTaskCommand(assignment.taskId, now);
					m_autonomy.state.garrisonAssignments[structureId] = assignment;
					assignedThisTick += toAssign;
					policyReason = "assigned";
					availableInfantry.erase(availableInfantry.begin(), availableInfantry.begin() + toAssign);
				}
				else
				{
					policyReason = commandReason;
				}
				adapterLog(
					"garrison_assignment structure=%u template=%s zone=%u infantry=%d state=%s reason=%s",
					structureId,
					templateName.c_str(),
					nearestZone->anchorId,
					toAssign,
					assignment.state.c_str(),
					assignment.reason.c_str());
				m_autonomy.state.garrisonTelemetry.push_back(nlohmann::json::object({
					{"structure_id", structureId},
					{"template", templateName},
					{"zone_id", nearestZone->anchorId},
					{"assigned_infantry", toAssign},
					{"entered", 0},
					{"state", assignment.state},
					{"reason", assignment.reason}
				}));
			}

			adapterLog(
				"garrison_policy zone=%u structures=%d filled=%d desired_infantry=%d assigned=%d available_infantry=%d reason=%s",
				usefulZones.empty() ? 0u : usefulZones.front().anchorId,
				structuresSeen,
				structuresFilled,
				desiredInfantryTotal,
				assignedThisTick,
				static_cast<int>(availableInfantry.size()),
				policyReason.c_str());
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

			// Rebuild zones (same logic as in evaluateAutonomyMacro)
			struct AutonomyZone
			{
				Coord3D center;
				ObjectID anchorId;
				bool isMainBase;
				ZoneAnchorType anchorType;
			};
			std::vector<AutonomyZone> zones;

			std::vector<AutomationOwnedObjectSnapshot> ownedObjects;
			collectOwnedAutomationObjects(player, ownedObjects);
			for (std::size_t ownedIdx = 0; ownedIdx < ownedObjects.size(); ++ownedIdx)
			{
				const AutomationOwnedObjectSnapshot& owned = ownedObjects[ownedIdx];
				if (!owned.isStructure || owned.underConstruction)
				{
					continue;
				}
				if (!(owned.name == "GLASupplyStash" || owned.name == "GLABarracks" || owned.name == "GLAArmsDealer"))
				{
					continue;
				}

				const Coord3D* anchorPos = owned.object != nullptr ? owned.object->getPosition() : nullptr;
				if (anchorPos == nullptr)
				{
					continue;
				}

				const Real minZoneDistSq = 200.0f * 200.0f;
				bool tooClose = false;
				for (std::size_t i = 0; i < zones.size(); ++i)
				{
					const Real dx = zones[i].center.x - anchorPos->x;
					const Real dy = zones[i].center.y - anchorPos->y;
					if ((dx * dx) + (dy * dy) < minZoneDistSq)
					{
						tooClose = true;
						break;
					}
				}
				if (tooClose)
				{
					continue;
				}

				AutonomyZone zone = {};
				zone.center = *anchorPos;
				zone.anchorId = owned.object->getID();
				zone.isMainBase = false;
				zone.anchorType = owned.isSupplyStructure ? ZoneAnchorType::SupplyStash : ZoneAnchorType::MainBase;
				zones.push_back(zone);
			}

			if (zones.empty())
			{
				adapterLog("debug_draw_skip reason=no_zones");
				return;
			}

			// Mark main base
			zones[0].isMainBase = true;

			adapterLog("debug_draw zones=%d radius=%.1f", static_cast<int>(zones.size()), m_autonomy.state.zoneRadius);

			const Real zoneRadius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
			const Int numSegments = 32; // Circle segments for smoothness
			const Real angleStep = (2.0f * 3.14159265f) / static_cast<Real>(numSegments);

			// Draw each zone
			for (std::size_t i = 0; i < zones.size(); ++i)
			{
				const AutonomyZone& zone = zones[i];

				// Color: green for main base, cyan for expansion zones
				Color zoneColor = zone.isMainBase
					? TheWindowManager->winMakeColor(0, 255, 0, 200)    // Green
					: TheWindowManager->winMakeColor(0, 200, 255, 180); // Cyan

				// Draw circle as line segments
				for (Int seg = 0; seg < numSegments; ++seg)
				{
					const Real angle1 = static_cast<Real>(seg) * angleStep;
					const Real angle2 = static_cast<Real>(seg + 1) * angleStep;

					Coord3D worldPos1 = zone.center;
					worldPos1.x += zoneRadius * std::cos(angle1);
					worldPos1.y += zoneRadius * std::sin(angle1);

					Coord3D worldPos2 = zone.center;
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
				Coord3D centerPos = zone.center;
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
						task->type == CombatTaskType::Attack ? "attack" :
						task->type == CombatTaskType::Defense ? "defense" : "guard",
						task->state == CombatTaskState::Assembling ? "assembling" :
						task->state == CombatTaskState::Moving ? "moving" : "engaging",
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
						task->type == CombatTaskType::Attack ? "attack" :
						task->type == CombatTaskType::Defense ? "defense" : "guard",
						remaining,
						task->minimumViableCount);

					adapterLog(
						"combat_task_release task=%u type=%s units=%d reason=task_failed",
						task->taskId,
						task->type == CombatTaskType::Attack ? "attack" :
						task->type == CombatTaskType::Defense ? "defense" : "guard",
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

				// 3. Expire task if timeout exceeded
				if (task->timeoutTick > 0 && now >= task->timeoutTick)
				{
					const DWORD taskAge = now - task->createdTick;
					m_autonomy.combatTaskManager.expireTask(task->taskId, "timeout_exceeded");

					adapterLog(
						"combat_task_failed task=%u type=%s reason=timeout_exceeded age_ms=%u",
						task->taskId,
						task->type == CombatTaskType::Attack ? "attack" :
						task->type == CombatTaskType::Defense ? "defense" : "guard",
						static_cast<unsigned int>(taskAge));

					adapterLog(
						"combat_task_release task=%u type=%s units=%d reason=task_expired",
						task->taskId,
						task->type == CombatTaskType::Attack ? "attack" :
						task->type == CombatTaskType::Defense ? "defense" : "guard",
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
			const std::string profile = normalizeAsciiLower(m_autonomy.state.profile);
			const bool isBalancedSprawl = (profile == "sprawl_balanced");
			const bool isSprawlStyle = (profile == "sprawl" || isBalancedSprawl);
			const UnsignedInt reserveCash = isBalancedSprawl ? 10000u : (profile == "sprawl" ? 5000u : 0u);
			const Real sprawlMultiplier = std::max<Real>(0.5f, std::min<Real>(10.0f, m_autonomy.state.sprawlMultiplier));
			Int currentZoneCount = 0;
			if (m_autonomy.state.telemetryZones.is_array())
			{
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
				}
			}
			const Int desiredZoneCount = isSprawlStyle
				? std::max<Int>(1, static_cast<Int>(std::floor((isBalancedSprawl ? 3.0f : 4.0f) * sprawlMultiplier)))
				: std::max<Int>(1, currentZoneCount);
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
			const std::string policyReason =
				buildPolicy.spendAllowed && !buildPolicy.highCashOverride ? reason : buildPolicy.reason;

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
				buildPolicy.spendAllowed ? 1 : 0,
				policyReason.c_str());

			// Fire at enemy WMD if ready
			if (readyScudStorm != nullptr && hasWMDThreat)
			{
				const WMDTarget* wmdTarget = m_autonomy.wmdTargetTracker.getHighestPriorityTarget();
				if (wmdTarget != nullptr && wmdTarget->alive && !AIControlAdapterIsTickInFuture(s_nextScudStormFireTick, now))
				{
					s_nextScudStormFireTick = now + 10000u;

					// Issue SCUD Storm fire command
					char requestIdBuffer[96];
					sprintf_s(requestIdBuffer, "scud_storm_fire_%08X", static_cast<unsigned int>(wmdTarget->objectId));

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
						"scud_storm_fire target=%u template=%s issued=%d reason=%s x=%.1f y=%.1f",
						wmdTarget->objectId,
						wmdTarget->templateName.c_str(),
						fired ? 1 : 0,
						fired ? "fired_at_enemy_wmd" : fireReason.c_str(),
						wmdTarget->position.x,
						wmdTarget->position.y);
				}
			}

			// Build defensive SCUD Storms up to the standing baseline.
			if (productionNeeded && AIControlAdapterIsTickInFuture(s_nextScudStormBuildTick, now))
			{
				return;
			}
			if (productionNeeded && !buildPolicy.spendAllowed)
			{
				s_nextScudStormBuildTick = now + 10000u;
				adapterLog(
					"scud_storm_build command=Game.BuildScudStormSmart issued=0 desired=%d live=%d in_progress=%d reason=%s",
					desiredScudStorms,
					liveScudStorms,
					inProgressScudStorms,
					buildPolicy.reason);
				return;
			}

			if (productionNeeded)
			{
				struct ScudStormPlacementChoice
				{
					bool hasPlacement = false;
					UnsignedInt zoneId = 0u;
					std::string role = "unavailable";
					Coord3D center;
					Real radius = 320.0f;
					int score = -999999;
					std::string reason = "unavailable";
				};
				auto chooseScudStormPlacement = [&]() -> ScudStormPlacementChoice
				{
					ScudStormPlacementChoice best;
					best.center.x = 0.0f;
					best.center.y = 0.0f;
					best.center.z = 0.0f;
					if (!m_autonomy.state.telemetryZones.is_array() || m_autonomy.state.telemetryZones.empty())
					{
						return best;
					}
					for (const auto& zone : m_autonomy.state.telemetryZones)
					{
						if (!zone.is_object())
						{
							continue;
						}
						const UnsignedInt zoneId = zone.value("anchor_id", 0u);
						const bool isMainBase = zone.value("is_main_base", false);
						const bool isActive = zone.value("active", false);
						const bool developed = zone.value("developed", false);
						const bool needsFollowup = zone.value("needs_followup", false);
						const auto threatIt = m_autonomy.state.zoneThreats.find(zoneId);
						const bool recentlyAttacked = threatIt != m_autonomy.state.zoneThreats.end()
							&& (now - threatIt->second.lastSeenTick) <= 45000u;
						int score = 0;
						if (isMainBase)
						{
							score += 220;
						}
						if (developed)
						{
							score += 140;
						}
						if (zone.value("black_markets", 0) > 0 || zone.value("palaces", 0) > 0)
						{
							score += 80;
						}
						if (zone.value("tunnels", 0) > 0 || zone.value("stingers", 0) > 0)
						{
							score += 40;
						}
						if (needsFollowup)
						{
							score -= 120;
						}
						if (isActive)
						{
							score -= 500;
						}
						if (recentlyAttacked)
						{
							score -= 450;
						}
						if (!developed && !isMainBase)
						{
							score -= 150;
						}
						if (!best.hasPlacement || score > best.score)
						{
							best.hasPlacement = true;
							best.zoneId = zoneId;
							best.score = score;
							best.center.x = zone.value("rear_point_x", zone.value("center_x", 0.0f));
							best.center.y = zone.value("rear_point_y", zone.value("center_y", 0.0f));
							best.center.z = 0.0f;
							best.radius = std::max<Real>(180.0f, m_autonomy.state.zoneRadius * (isMainBase ? 0.35f : 0.30f));
							if (score < 0)
							{
								best.role = "fallback";
								best.reason = "emergency_override";
							}
							else if (isMainBase)
							{
								best.role = "interior";
								best.reason = "fallback_interior";
							}
							else
							{
								best.role = "rear";
								best.reason = "safe_rear";
							}
						}
					}
					return best;
				};
				const ScudStormPlacementChoice placement = chooseScudStormPlacement();
				adapterLog(
					"scud_storm_placement zone=%u role=%s x=%.1f y=%.1f score=%d reason=%s",
					static_cast<unsigned int>(placement.zoneId),
					placement.role.c_str(),
					placement.center.x,
					placement.center.y,
					placement.score,
					placement.reason.c_str());
				adapterLog(
					"strategic_placement command=Game.BuildScudStormSmart template=GLAScudStorm role=superweapon source=%s zone_anchor=%u zone_center=(%.1f,%.1f) zone_radius=%.1f strict_zone=0 has_placement=%d score=%d reason=%s tick=%u",
					placement.role.c_str(),
					static_cast<unsigned int>(placement.zoneId),
					placement.center.x,
					placement.center.y,
					placement.radius,
					placement.hasPlacement ? 1 : 0,
					placement.score,
					placement.reason.c_str(),
					static_cast<unsigned int>(now));
				nlohmann::json message = {
					{"type", "SessionCommand"},
					{"request_id", std::string("defensive_scud_storm_build")},
					{"cmd", "Game.BuildScudStormSmart"},
					{"args", nlohmann::json::object()}
				};
				if (placement.hasPlacement)
				{
					message["args"]["zone_center"] = nlohmann::json::object({
						{"x", placement.center.x},
						{"y", placement.center.y}
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
			auto shouldLogMobileSiegeDetection = [](const std::string& templateName, const AIControlAdapterMobileSiegeTemplateResult& classification) -> bool
			{
				if (classification.accepted)
				{
					return true;
				}
				const std::string lower = normalizeAsciiLower(templateName);
				return lower.find("shell") != std::string::npos ||
					lower.find("artillery") != std::string::npos ||
					lower.find("cannon") != std::string::npos ||
					lower.find("inferno") != std::string::npos ||
					lower.find("scudlauncher") != std::string::npos ||
					lower.find("tomahawk") != std::string::npos ||
					lower.find("rocketbuggy") != std::string::npos;
			};
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
				if (shouldLogMobileSiegeDetection(name, siegeClassification))
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
				if (shouldLogMobileSiegeDetection(item.templateName, siegeClassification))
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
						const std::string buggyTemplateName = inferRocketBuggyTemplateForProducerSnapshot(owned.object);
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
				const bool suitableCounter =
					containsIgnoreCase(owned.name, "rocketbuggy") ||
					containsIgnoreCase(owned.name, "jarmen") ||
					owned.isScudLauncher;
				if (!suitableCounter || owned.underConstruction || !owned.hasAI)
				{
					continue;
				}
				const UnsignedInt unitId = static_cast<UnsignedInt>(owned.object->getID());
				if (m_autonomy.taskReservationManager.isObjectReserved(unitId) ||
					m_autonomy.combatTaskManager.isUnitReserved(unitId) ||
					isGarrisonReservedUnit(unitId))
				{
					continue;
				}
				availableCounters.push_back(owned.object);
			}
			std::stable_sort(availableCounters.begin(), availableCounters.end(), [](Object* a, Object* b) -> bool
			{
				const ThingTemplate* ta = a != nullptr ? a->getTemplate() : nullptr;
				const ThingTemplate* tb = b != nullptr ? b->getTemplate() : nullptr;
				const std::string an = ta != nullptr ? ta->getName().str() : "";
				const std::string bn = tb != nullptr ? tb->getName().str() : "";
				const int ap = containsIgnoreCase(an, "rocketbuggy") ? 0 :
					(containsIgnoreCase(an, "jarmen") ? 1 :
						(containsIgnoreCase(an, "scudlauncher") ? 2 : 3));
				const int bp = containsIgnoreCase(bn, "rocketbuggy") ? 0 :
					(containsIgnoreCase(bn, "jarmen") ? 1 :
						(containsIgnoreCase(bn, "scudlauncher") ? 2 : 3));
				return ap < bp;
			});

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

			nlohmann::json threatTelemetry = nlohmann::json::array();
			for (const ArtilleryThreat& threat : threats)
			{
				threatTelemetry.push_back(nlohmann::json::object({
					{"object_id", threat.objectId},
					{"template", threat.templateName},
					{"visible", threat.visible},
					{"stale", !threat.visible},
					{"x", threat.position.x},
					{"y", threat.position.y},
					{"priority", "high"}
				}));
			}
			m_autonomy.state.counterbatteryTelemetry = nlohmann::json::object({
				{"artillery_threats", threatTelemetry},
				{"mobile_siege_threats", threatTelemetry},
				{"active_tasks", activeCounterbatteryTasks},
				{"assigned_units", assignedCounterbatteryUnits},
				{"production_needed", policy.productionNeeded || buggyMix.productionNeeded},
				{"reason", policy.reason}
			});

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
				const std::string unitTemplateName = preferBuggy ? "GLAVehicleRocketBuggy" : inferAutonomyScudLauncherTemplate(player);
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

			std::vector<unsigned int> assignedIds;
			const int assignCount = std::min<int>(
				policy.desiredAssignedUnits,
				std::min<int>(4, static_cast<int>(availableCounters.size())));
			for (int i = 0; i < assignCount; ++i)
			{
				assignedIds.push_back(static_cast<unsigned int>(availableCounters[static_cast<std::size_t>(i)]->getID()));
			}
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
			collectCombatUnitsForRaid(player, combatUnits);

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

		void collectCombatUnitsForRaid(Player* player, std::vector<Object*>& outUnits)
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
				if (containsIgnoreCase(owned.name, "radar"))
				{
					continue;
				}
				if (owned.underConstruction || !owned.hasAI || owned.object == nullptr)
				{
					continue;
				}
				if (owned.object != nullptr && isGarrisonReservedUnit(static_cast<UnsignedInt>(owned.object->getID())))
				{
					adapterLog(
						"combat_skip_reserved_garrison unit=%u reason=garrison_assignment",
						static_cast<unsigned int>(owned.object->getID()));
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
									adapterLog(
										"combat_skip_reserved_combat unit=%u task=%u owner=%s type=%s",
										owned.object->getID(),
										task->taskId,
										task->owner.c_str(),
										task->type == CombatTaskType::Attack ? "attack" :
										task->type == CombatTaskType::Defense ? "defense" : "guard");
									break;
								}
							}
						}
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
				if (isCaptureTargetReserved(obj))
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
			return m_autonomy.taskReservationManager.isObjectReserved(static_cast<unsigned int>(sourceId));
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
			for (const SpecialTaskReservation* task : captureTasks)
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
				// Check if object is source or target
				if (task->sourceObjectId == objectId || task->targetObjectId == objectId)
				{
					return task;
				}
			}
			return nullptr;
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
			for (const SpecialTaskReservation* task : captureTasks)
			{
				if (task != nullptr &&
					task->state != SpecialTaskState::Complete &&
					task->state != SpecialTaskState::Failed &&
					task->state != SpecialTaskState::Expired &&
					task->targetObjectId == static_cast<unsigned int>(targetId))
				{
					return true;
				}
			}
			return false;
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
			const UnsignedInt reserve = normalizeAsciiLower(m_autonomy.state.profile) == "sprawl_balanced" ? 10000u : 5000u;
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

			// Phase 9.0: Check for equivalent active attack task before issuing
			std::vector<Object*> tempCombatUnits;
			collectCombatUnitsForRaid(player, tempCombatUnits);
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

				adapterLog(
					"combat_task_assigned task=%u type=attack units=%d target=(%.1f,%.1f) reason=attack_automation",
					attackTaskId,
					static_cast<int>(attackResult.assignedUnitIds.size()),
					attackResult.targetPosition.x,
					attackResult.targetPosition.y);

				adapterLog(
					"combat_task_command task=%u type=attack command=Game.AttackMove.RaidSmart issued=1 units=%d",
					attackTaskId,
					static_cast<int>(attackResult.assignedUnitIds.size()));
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
					const Real NEAR_TARGET_THRESHOLD = 200.0f;
					const Real PROGRESS_THRESHOLD = 10.0f; // Consider 10 units improvement as progress
					const DWORD PROGRESS_TIMEOUT_EXTENSION_MS = 60000u;
					nearTarget = (currentDistance <= NEAR_TARGET_THRESHOLD);

					// Update distance tracking
					if (task->lastDistance < 0.0f)
					{
						// First measurement
						task->lastDistance = currentDistance;
						task->bestDistance = currentDistance;
						task->lastUpdateTick = now;

						task->timeoutTick = now + PROGRESS_TIMEOUT_EXTENSION_MS;

						adapterLog(
							"capture_task_progress task=%u source=%u target=%u distance=%.1f best_distance=%.1f reason=first_measurement",
							task->taskId,
							task->sourceObjectId,
							task->targetObjectId,
							currentDistance,
							currentDistance);

						if (nearTarget && task->state != SpecialTaskState::Executing)
						{
							m_autonomy.taskReservationManager.updateTaskState(
								task->taskId, SpecialTaskState::Executing, "near_target");
							adapterLog(
								"capture_task_state task=%u state=Capturing reason=near_target source=%u target=%u distance=%.1f",
								task->taskId,
								task->sourceObjectId,
								task->targetObjectId,
								currentDistance);
						}
					}
					else
					{
						// Check for progress
						const bool madeProgress = (currentDistance < task->bestDistance - PROGRESS_THRESHOLD);
						const bool enteringNearTarget =
							nearTarget &&
							task->state != SpecialTaskState::Executing;

						if (madeProgress || enteringNearTarget)
						{
							// Update progress
							task->lastUpdateTick = now;
							if (currentDistance < task->bestDistance)
							{
								task->bestDistance = currentDistance;
							}

							// Phase 6.2 FIX: Extend timeout when progress is made
							task->timeoutTick = now + PROGRESS_TIMEOUT_EXTENSION_MS;

							const char* progressReason = madeProgress ? "distance_decreased" : "near_target";
							adapterLog(
								"capture_task_progress task=%u source=%u target=%u distance=%.1f best_distance=%.1f reason=%s",
								task->taskId,
								task->sourceObjectId,
								task->targetObjectId,
								currentDistance,
								task->bestDistance,
								progressReason);

							// Phase 6.2 FIX: State transitions based on distance
							if (nearTarget && task->state != SpecialTaskState::Executing)
							{
								m_autonomy.taskReservationManager.updateTaskState(
									task->taskId, SpecialTaskState::Executing, "near_target");
								adapterLog(
									"capture_task_state task=%u state=Capturing reason=near_target source=%u target=%u distance=%.1f",
									task->taskId,
									task->sourceObjectId,
									task->targetObjectId,
									currentDistance);
							}
							else if (madeProgress && task->state == SpecialTaskState::Assigned)
							{
								m_autonomy.taskReservationManager.updateTaskState(
									task->taskId, SpecialTaskState::Moving, "distance_progress");
								adapterLog(
									"capture_task_state task=%u state=Moving reason=distance_progress source=%u target=%u distance=%.1f",
									task->taskId,
									task->sourceObjectId,
									task->targetObjectId,
									currentDistance);
							}
						}

						task->lastDistance = currentDistance;
					}
				}

				// Phase 6.2: Reissue capture command for stalled but recoverable tasks
				const DWORD REISSUE_THRESHOLD_MS = 10000u; // 10 seconds without progress
				const DWORD NEAR_TARGET_REISSUE_MS = 8000u; // Re-click capture quickly once adjacent
				const DWORD STALE_THRESHOLD_MS = 30000u; // 30 seconds without progress while far
				const DWORD NEAR_TARGET_STALE_MS = 45000u; // 45 seconds adjacent without ownership flip
				const Real FAR_FROM_TARGET = 200.0f;
				const DWORD staleDuration = now - task->lastUpdateTick;
				const DWORD timeSinceLastCommand = now - task->lastCommandTick;
				const int MAX_REISSUES = 4;

				auto reissueCaptureCommand = [&](const char* recoveryReason) -> bool
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
						task->lastCommandTick = now;
						task->commandReissueCount++;
						task->timeoutTick = now + 60000u;
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
				const bool canReissue =
					task->commandReissueCount < MAX_REISSUES &&
					currentDistance > 0.0f;
				const bool shouldReissueNearTarget =
					nearTarget &&
					timeSinceLastCommand > NEAR_TARGET_REISSUE_MS &&
					staleDuration > NEAR_TARGET_REISSUE_MS;
				const bool shouldReissueFarTarget =
					!nearTarget &&
					staleDuration > REISSUE_THRESHOLD_MS &&
					staleDuration < STALE_THRESHOLD_MS &&
					timeSinceLastCommand > REISSUE_THRESHOLD_MS;
				if (canReissue && shouldReissueNearTarget)
				{
					reissueCaptureCommand("near_target_retry");
				}
				else if (canReissue && shouldReissueFarTarget)
				{
					reissueCaptureCommand("stalled_recovery");
				}

				// Check for timeout with progress awareness
				// Expire only if truly stalled:
				// - No progress for the stale threshold AND
				// - Overall timeout reached
				const bool isFarStalled = (staleDuration > STALE_THRESHOLD_MS) &&
					(currentDistance < 0.0f || currentDistance > FAR_FROM_TARGET);
				const bool isNearStalled = nearTarget &&
					(staleDuration > NEAR_TARGET_STALE_MS) &&
					(task->commandReissueCount >= MAX_REISSUES);
				const bool overallTimeout = (now >= task->timeoutTick);

				if ((isFarStalled || isNearStalled) && overallTimeout)
				{
					const char* expireReason = isNearStalled ? "near_target_no_capture" : "stalled_no_progress";
					m_autonomy.taskReservationManager.expireTask(task->taskId, expireReason);
					adapterLog(
						"capture_task_expired task=%u reason=%s source=%u target=%u age_ms=%u stale_ms=%u distance=%.1f reissues=%d",
						task->taskId,
						expireReason,
						task->sourceObjectId,
						task->targetObjectId,
						now - task->createdTick,
						staleDuration,
						currentDistance,
						task->commandReissueCount);
					adapterLog(
						"capture_unit_released task=%u source=%u reason=%s",
						task->taskId,
						task->sourceObjectId,
						expireReason);
					recordAutonomyTelemetryEvent("capture_task_expired", expireReason, "no_movement");
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
			if (pendingCount >= m_automation.captureRule.maxConcurrent)
			{
				adapterLog("automation_capture_rule_skip player=%d reason=pending_limit pending=%d max_concurrent=%d", player->getPlayerIndex(), pendingCount, m_automation.captureRule.maxConcurrent);
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
