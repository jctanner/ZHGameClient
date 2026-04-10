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
			resetAutonomyState();
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
			resetAutonomyState();
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
		AutonomyState m_autonomyState;

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

		void resetAutonomyState()
		{
			m_autonomyState.mode = "manual";
			m_autonomyState.profile = "sprawl_balanced";
			m_autonomyState.paused = false;
			m_autonomyState.captureTech = true;
			m_autonomyState.allowSuperweapons = false;
			m_autonomyState.hasExplicitPlayerIndex = false;
			m_autonomyState.playerIndex = -1;
			m_autonomyState.hasExplicitTargetPlayerIndex = false;
			m_autonomyState.targetPlayerIndex = -1;
			m_autonomyState.economyBias = 0.65f;
			m_autonomyState.aggressionBias = 0.35f;
			m_autonomyState.defenseBias = 0.45f;
			m_autonomyState.expansionBias = 0.55f;
			m_autonomyState.sprawlMultiplier = 1.5f;
			m_autonomyState.zoneRadius = 450.0f;
			m_autonomyState.lastAppliedTick = 0u;
			m_autonomyState.nextMacroTick = 0u;
			m_autonomyState.nextProductionTick = 0u;
			m_autonomyState.nextTechTick = 0u;
			m_autonomyState.nextGuardTick = 0u;
			m_autonomyState.nextSupplyBuildTick = 0u;
			m_autonomyState.nextBarracksBuildTick = 0u;
			m_autonomyState.nextArmsBuildTick = 0u;
			m_autonomyState.nextPalaceBuildTick = 0u;
			m_autonomyState.nextMarketBuildTick = 0u;
			m_autonomyState.nextTunnelBuildTick = 0u;
			m_autonomyState.nextStingerBuildTick = 0u;
			m_autonomyState.nextZoneIndex = 0u;
			m_autonomyState.hasLastZone = false;
			m_autonomyState.lastZoneAnchorId = 0u;
			m_autonomyState.lastZoneIsMainBase = false;
			m_autonomyState.lastZoneCenterX = 0.0f;
			m_autonomyState.lastZoneCenterY = 0.0f;
			m_autonomyState.lastDecisionCategory.clear();
			m_autonomyState.lastDecisionCommand.clear();
			m_autonomyState.lastDecisionReason.clear();
		}

		bool isAutonomyModeActive() const
		{
			if (m_autonomyState.paused)
			{
				return false;
			}
			return m_autonomyState.mode == "autonomous" || m_autonomyState.mode == "hybrid";
		}

		Player* resolveAutonomyPlayer() const
		{
			if (m_autonomyState.hasExplicitPlayerIndex)
			{
				return getPlayerByIndex(m_autonomyState.playerIndex);
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

		void applyAutonomyRules()
		{
			clearAutonomyManagedRules();
			if (!isAutonomyModeActive())
			{
				m_autonomyState.lastAppliedTick = ::GetTickCount();
				adapterLog("autonomy_rules_inactive mode=%s paused=%d", m_autonomyState.mode.c_str(), m_autonomyState.paused ? 1 : 0);
				return;
			}

			const std::string profile = normalizeAsciiLower(m_autonomyState.profile);
			const bool isBalancedSprawl = (profile == "sprawl_balanced");
			const bool isSprawlStyle = (profile == "sprawl" || isBalancedSprawl);
			const Real econ = clampUnitFloat(m_autonomyState.economyBias, 0.0f, 1.0f);
			const Real aggro = clampUnitFloat(m_autonomyState.aggressionBias, 0.0f, 1.0f);
			const Real defense = clampUnitFloat(m_autonomyState.defenseBias, 0.0f, 1.0f);
			const Real expansion = clampUnitFloat(m_autonomyState.expansionBias, 0.0f, 1.0f);

			m_workerAutomationRule.enabled = true;
			m_workerAutomationRule.hasExplicitPlayerIndex = m_autonomyState.hasExplicitPlayerIndex;
			m_workerAutomationRule.playerIndex = m_autonomyState.playerIndex;
			m_workerAutomationRule.hasExplicitProducerKind = true;
			m_workerAutomationRule.producerKind = "command_center";
			m_workerAutomationRule.minIdleWorkers = (econ >= 0.70f || profile == "economic" || isSprawlStyle) ? 2 : 1;
			m_workerAutomationRule.queueCount = (econ >= 0.75f || profile == "economic" || isSprawlStyle) ? 2 : 1;
			m_workerAutomationRule.cooldownMs = (profile == "aggressive") ? 1500u : 2500u;
			m_workerAutomationRule.nextAllowedTick = 0u;
			if (profile == "sprawl")
			{
				m_workerAutomationRule.minIdleWorkers = 3;
				m_workerAutomationRule.queueCount = 1;
				m_workerAutomationRule.cooldownMs = 2000u;
			}
			else if (isBalancedSprawl)
			{
				m_workerAutomationRule.minIdleWorkers = 2;
				m_workerAutomationRule.queueCount = 1;
				m_workerAutomationRule.cooldownMs = 2500u;
			}

			m_stashWorkerAutomationRule.enabled = profile != "builtin_passthrough";
			m_stashWorkerAutomationRule.hasExplicitPlayerIndex = m_autonomyState.hasExplicitPlayerIndex;
			m_stashWorkerAutomationRule.playerIndex = m_autonomyState.playerIndex;
			m_stashWorkerAutomationRule.targetWorkersPerStash = 8;
			if (profile == "economic" || isSprawlStyle || econ >= 0.70f)
			{
				m_stashWorkerAutomationRule.targetWorkersPerStash = 10;
			}
			else if (profile == "aggressive")
			{
				m_stashWorkerAutomationRule.targetWorkersPerStash = 7;
			}
			m_stashWorkerAutomationRule.cooldownMs = 5000u;
			m_stashWorkerAutomationRule.nextAllowedTick = 0u;
			m_stashWorkerAutomationRule.servicedStashIds.clear();
			if (profile == "sprawl")
			{
				m_stashWorkerAutomationRule.targetWorkersPerStash = 3;
				m_stashWorkerAutomationRule.cooldownMs = 12000u;
			}
			else if (isBalancedSprawl)
			{
				m_stashWorkerAutomationRule.targetWorkersPerStash = 6;
				m_stashWorkerAutomationRule.cooldownMs = 8000u;
			}

			m_radarVanAutomationRule.enabled = (profile != "defensive" && profile != "builtin_passthrough");
			m_radarVanAutomationRule.hasExplicitPlayerIndex = m_autonomyState.hasExplicitPlayerIndex;
			m_radarVanAutomationRule.playerIndex = m_autonomyState.playerIndex;
			m_radarVanAutomationRule.minCount = (profile == "tech") ? 2 : 1;
			m_radarVanAutomationRule.cooldownMs = 12000u;
			m_radarVanAutomationRule.nextAllowedTick = 0u;

			m_attackAutomationRule.enabled = profile != "builtin_passthrough";
			m_attackAutomationRule.hasExplicitPlayerIndex = m_autonomyState.hasExplicitPlayerIndex;
			m_attackAutomationRule.playerIndex = m_autonomyState.playerIndex;
			m_attackAutomationRule.minUnits = 38;
			m_attackAutomationRule.groupSize = 28;
			m_attackAutomationRule.distance = 3000.0f + (expansion * 400.0f);
			m_attackAutomationRule.cooldownMs = 18000u;
			m_attackAutomationRule.nextAllowedTick = 0u;
			if (profile == "aggressive" || aggro >= 0.70f)
			{
				m_attackAutomationRule.minUnits = 24;
				m_attackAutomationRule.groupSize = 20;
				m_attackAutomationRule.cooldownMs = 12000u;
			}
			else if (profile == "economic")
			{
				m_attackAutomationRule.minUnits = 50;
				m_attackAutomationRule.groupSize = 34;
				m_attackAutomationRule.cooldownMs = 22000u;
			}
			else if (profile == "defensive" || defense >= 0.70f)
			{
				m_attackAutomationRule.minUnits = 60;
				m_attackAutomationRule.groupSize = 40;
				m_attackAutomationRule.cooldownMs = 26000u;
			}
			else if (profile == "tech")
			{
				m_attackAutomationRule.minUnits = 44;
				m_attackAutomationRule.groupSize = 30;
				m_attackAutomationRule.cooldownMs = 20000u;
			}
			else if (profile == "sprawl")
			{
				m_attackAutomationRule.minUnits = 70;
				m_attackAutomationRule.groupSize = 45;
				m_attackAutomationRule.cooldownMs = 26000u;
			}
			else if (isBalancedSprawl)
			{
				m_attackAutomationRule.minUnits = 55;
				m_attackAutomationRule.groupSize = 28;
				m_attackAutomationRule.cooldownMs = 22000u;
			}

			m_captureAutomationRule.enabled = m_autonomyState.captureTech && profile != "builtin_passthrough";
			m_captureAutomationRule.hasExplicitPlayerIndex = m_autonomyState.hasExplicitPlayerIndex;
			m_captureAutomationRule.playerIndex = m_autonomyState.playerIndex;
			m_captureAutomationRule.preferIdle = true;
			m_captureAutomationRule.maxConcurrent = (profile == "tech") ? 3 : 2;
			m_captureAutomationRule.cooldownMs = 5000u;
			m_captureAutomationRule.nextAllowedTick = 0u;
			m_captureAutomationRule.pendingTargetsUntilTick.clear();
			m_captureAutomationRule.pendingSourcesUntilTick.clear();

			m_autonomyState.lastAppliedTick = ::GetTickCount();
			m_autonomyState.nextMacroTick = 0u;
			m_autonomyState.nextProductionTick = 0u;
			m_autonomyState.nextTechTick = 0u;
			m_autonomyState.nextGuardTick = 0u;
			adapterLog(
				"autonomy_apply mode=%s profile=%s paused=%d economy_bias=%.2f aggression_bias=%.2f defense_bias=%.2f expansion_bias=%.2f capture_tech=%d target_player=%d",
				m_autonomyState.mode.c_str(),
				m_autonomyState.profile.c_str(),
				m_autonomyState.paused ? 1 : 0,
				static_cast<double>(m_autonomyState.economyBias),
				static_cast<double>(m_autonomyState.aggressionBias),
				static_cast<double>(m_autonomyState.defenseBias),
				static_cast<double>(m_autonomyState.expansionBias),
				m_autonomyState.captureTech ? 1 : 0,
				m_autonomyState.hasExplicitTargetPlayerIndex ? m_autonomyState.targetPlayerIndex : -1);
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
				return;
			}

			const DWORD now = ::GetTickCount();
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
				Int workers;
				Int radarVans;
				Int soldiers;
				Int rpg;
				Int quads;
				Int scorpions;
				Int scudLaunchers;
			} counts = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

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
			if (!zones.empty())
			{
				m_autonomyState.nextZoneIndex = zones.empty() ? 0u : (m_autonomyState.nextZoneIndex % zones.size());
				activeZone = zones[m_autonomyState.nextZoneIndex];
				hasActiveZone = true;
			}
			m_autonomyState.hasLastZone = hasActiveZone;
			m_autonomyState.lastZoneAnchorId = hasActiveZone ? static_cast<UnsignedInt>(activeZone.anchorId) : 0u;
			m_autonomyState.lastZoneIsMainBase = hasActiveZone && activeZone.isMainBase;
			m_autonomyState.lastZoneCenterX = hasActiveZone ? activeZone.center.x : 0.0f;
			m_autonomyState.lastZoneCenterY = hasActiveZone ? activeZone.center.y : 0.0f;

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
			} activeZoneCounts = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			if (hasActiveZone)
			{
				const Real zoneRadiusSq = std::max<Real>(160.0f, m_autonomyState.zoneRadius) * std::max<Real>(160.0f, m_autonomyState.zoneRadius);
				for (Object* obj = TheGameLogic != nullptr ? TheGameLogic->getFirstObject() : nullptr; obj != nullptr; obj = obj->getNextObject())
				{
					if (obj->isEffectivelyDead() || obj->getControllingPlayer() != player || !obj->isKindOf(KINDOF_STRUCTURE) || obj->getPosition() == nullptr)
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
					const bool underConstruction = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
					const bool countAsInProgress = underConstruction && static_cast<Int>(obj->getBuilderID()) > 0;
					auto incrementZoneStructureCount = [&](Int& completeCount, Int& inProgressCount) -> void
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
						incrementZoneStructureCount(activeZoneCounts.supplyStashes, activeZoneCounts.supplyStashesInProgress);
					}
					else if (containsIgnoreCase(name, "barracks"))
					{
						incrementZoneStructureCount(activeZoneCounts.barracks, activeZoneCounts.barracksInProgress);
					}
					else if (containsIgnoreCase(name, "armsdealer"))
					{
						incrementZoneStructureCount(activeZoneCounts.armsDealers, activeZoneCounts.armsDealersInProgress);
					}
					else if (containsIgnoreCase(name, "palace"))
					{
						incrementZoneStructureCount(activeZoneCounts.palaces, activeZoneCounts.palacesInProgress);
					}
					else if (containsIgnoreCase(name, "black") && containsIgnoreCase(name, "market"))
					{
						incrementZoneStructureCount(activeZoneCounts.blackMarkets, activeZoneCounts.blackMarketsInProgress);
					}
					else if (containsIgnoreCase(name, "tunnelnetwork"))
					{
						incrementZoneStructureCount(activeZoneCounts.tunnels, activeZoneCounts.tunnelsInProgress);
					}
					else if (containsIgnoreCase(name, "stingersite"))
					{
						incrementZoneStructureCount(activeZoneCounts.stingers, activeZoneCounts.stingersInProgress);
					}
				}
			}

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
				if (m_autonomyState.hasExplicitPlayerIndex)
				{
					message["args"]["player_index"] = m_autonomyState.playerIndex;
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
					m_ownedCacheValid = false;
				}
				return ok;
			};

			auto tryZoneCommand = [&](const char* requestIdPrefix, const char* cmd, nlohmann::json args, std::string& outReason) -> bool
			{
				if (!hasActiveZone)
				{
					return tryCommand(requestIdPrefix, cmd, args, outReason);
				}
				args["zone_center"] = nlohmann::json::object({
					{"x", activeZone.center.x},
					{"y", activeZone.center.y}
				});
				args["zone_radius"] = std::max<Real>(160.0f, m_autonomyState.zoneRadius);
				args["strict_zone"] = false;
				return tryCommand(requestIdPrefix, cmd, args, outReason);
			};

			auto trySpecificZoneCommand = [&](const char* requestIdPrefix, const char* cmd, const AutonomyZone& zone, nlohmann::json args, std::string& outReason) -> bool
			{
				args["zone_center"] = nlohmann::json::object({
					{"x", zone.center.x},
					{"y", zone.center.y}
				});
				args["zone_radius"] = std::max<Real>(220.0f, m_autonomyState.zoneRadius * 1.35f);
				args["strict_zone"] = false;
				return tryCommand(requestIdPrefix, cmd, args, outReason);
			};

			auto isSettlingSensitiveBuild = [&](const char* cmd) -> bool
			{
				const std::string cmdString = cmd != nullptr ? cmd : "";
				return cmdString == "Game.BuildSupplyStashSmart"
					|| cmdString == "Game.BuildBarracksSmart"
					|| cmdString == "Game.BuildArmsDealerSmart"
					|| cmdString == "Game.BuildPalaceSmart";
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
					return &m_autonomyState.nextSupplyBuildTick;
				}
				if (cmdString == "Game.BuildBarracksSmart")
				{
					return &m_autonomyState.nextBarracksBuildTick;
				}
				if (cmdString == "Game.BuildArmsDealerSmart")
				{
					return &m_autonomyState.nextArmsBuildTick;
				}
				if (cmdString == "Game.BuildPalaceSmart")
				{
					return &m_autonomyState.nextPalaceBuildTick;
				}
				if (cmdString == "Game.BuildBlackMarketSmart")
				{
					return &m_autonomyState.nextMarketBuildTick;
				}
				if (cmdString == "Game.BuildTunnelNetwork")
				{
					return &m_autonomyState.nextTunnelBuildTick;
				}
				if (cmdString == "Game.BuildStingerSite")
				{
					return &m_autonomyState.nextStingerBuildTick;
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
				return nextAllowedTick == nullptr || static_cast<LONG>(*nextAllowedTick - now) <= 0;
			};

			auto recordBuildAttempt = [&](const char* cmd, bool success, const std::string& outReason)
			{
				DWORD* nextAllowedTick = getBuildCooldownTick(cmd);
				if (nextAllowedTick == nullptr)
				{
					return;
				}

				DWORD delayMs = success ? 9000u : 4000u;
				const bool settlingSensitiveBuild = isSettlingSensitiveBuild(cmd);
				if (outReason == "idle_worker_not_found")
				{
					delayMs = settlingSensitiveBuild ? 2500u : 1500u;
				}
				else if (outReason == "construct_site_not_created")
				{
					delayMs = settlingSensitiveBuild ? 7000u : 5000u;
				}
				else if (!success && settlingSensitiveBuild)
				{
					delayMs = std::max<DWORD>(delayMs, 5000u);
				}
				if (std::strcmp(cmd, "Game.BuildSupplyStashSmart") == 0)
				{
					delayMs = success ? 10000u : std::max<DWORD>(delayMs, 3500u);
					if (outReason == "construct_site_not_created")
					{
						delayMs = 4500u;
					}
				}
				else if (std::strcmp(cmd, "Game.BuildBarracksSmart") == 0)
				{
					delayMs = success ? 12000u : std::max<DWORD>(delayMs, 6000u);
				}
				else if (std::strcmp(cmd, "Game.BuildArmsDealerSmart") == 0)
				{
					delayMs = success ? 12000u : std::max<DWORD>(delayMs, 6000u);
				}
				if (std::strcmp(cmd, "Game.BuildPalaceSmart") == 0)
				{
					delayMs = success ? 15000u : std::max<DWORD>(delayMs, 8000u);
				}
				*nextAllowedTick = now + delayMs;
			};

			if (static_cast<LONG>(m_autonomyState.nextMacroTick - now) <= 0)
			{
				std::string reason;
				bool issued = false;
				const std::string profile = normalizeAsciiLower(m_autonomyState.profile);
				const bool isBalancedSprawl = (profile == "sprawl_balanced");
				const bool isSprawlStyle = (profile == "sprawl" || isBalancedSprawl);
				const Real sprawlMultiplier = std::max<Real>(0.5f, std::min<Real>(10.0f, m_autonomyState.sprawlMultiplier));
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
				const bool openingNeedsSupplyStash = counts.supplyStashes < 1;
				const bool openingNeedsBarracks = !openingNeedsSupplyStash && counts.barracks < 1;
				const bool openingNeedsArmsDealer = !openingNeedsSupplyStash && !openingNeedsBarracks && counts.armsDealers < 1;
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
				const bool balancedZoneCanAddBarracks = !isBalancedSprawl || !hasActiveZone || totalZoneBarracks < 1;
				const bool balancedZoneCanAddArmsDealer = !isBalancedSprawl || !hasActiveZone || totalZoneArmsDealers < 1;
				const bool balancedZoneCanAddPalace = !isBalancedSprawl || !hasActiveZone || totalZonePalaces < 1;
				std::string chosenCommand = "none";
				if (openingNeedsSupplyStash)
				{
					if (counts.supplyStashes < 1
						&& counts.supplyStashesInProgress < 1
						&& isBuildAttemptReady("Game.BuildSupplyStashSmart", counts.supplyStashesInProgress)
						&& money >= 1200u)
					{
						chosenCommand = "Game.BuildSupplyStashSmart";
						issued = tryMacroBuildWithFallback("Game.BuildSupplyStashSmart", false, reason);
						recordBuildAttempt("Game.BuildSupplyStashSmart", issued, reason);
					}
				}
				else if (openingNeedsBarracks)
				{
					if (counts.supplyStashes >= 1
						&& counts.barracks < 1
						&& counts.barracksInProgress < 1
						&& isBuildAttemptReady("Game.BuildBarracksSmart", counts.barracksInProgress)
						&& money >= 600u)
					{
						chosenCommand = "Game.BuildBarracksSmart";
						issued = tryMacroBuildWithFallback("Game.BuildBarracksSmart", false, reason);
						recordBuildAttempt("Game.BuildBarracksSmart", issued, reason);
					}
				}
				else if (openingNeedsArmsDealer)
				{
					if (counts.supplyStashes >= 1
						&& counts.barracks >= 1
						&& counts.armsDealers < 1
						&& counts.armsDealersInProgress < 1
						&& isBuildAttemptReady("Game.BuildArmsDealerSmart", counts.armsDealersInProgress)
						&& money >= 2500u)
					{
						chosenCommand = "Game.BuildArmsDealerSmart";
						issued = tryMacroBuildWithFallback("Game.BuildArmsDealerSmart", false, reason);
						recordBuildAttempt("Game.BuildArmsDealerSmart", issued, reason);
					}
				}
				else if (totalSupplyStashes < 2 && (profile == "economic" || isSprawlStyle || m_autonomyState.expansionBias >= 0.70f))
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
				else if (shouldForceEcoRecovery
					&& totalPalaces > 0
					&& totalBlackMarkets < sprawlDesiredMarketCount
					&& counts.blackMarketsInProgress < 1
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress)
					&& money >= blackMarketCost)
				{
					chosenCommand = "Game.BuildBlackMarketSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBlackMarketSmart", hasActiveZone, reason);
					recordBuildAttempt("Game.BuildBlackMarketSmart", issued, reason);
				}
				else if (shouldForceEcoRecovery
					&& !shouldThrottleExtraStashGrowth
					&& totalSupplyStashes < sprawlSupplyCap
					&& counts.supplyStashesInProgress < 1
					&& isBuildAttemptReady("Game.BuildSupplyStashSmart", counts.supplyStashesInProgress)
					&& money >= 1800u)
				{
					chosenCommand = "Game.BuildSupplyStashSmart";
					issued = tryMacroBuildWithFallback("Game.BuildSupplyStashSmart", false, reason);
					recordBuildAttempt("Game.BuildSupplyStashSmart", issued, reason);
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
					&& totalPalaces > 0
					&& totalBlackMarkets < 1
					&& money >= (isBalancedSprawl ? (reserveCash + blackMarketCost) : blackMarketCost)
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress)
					&& (!isBalancedSprawl || counts.blackMarketsInProgress < 1))
				{
					chosenCommand = "Game.BuildBlackMarketSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBlackMarketSmart", false, reason);
					recordBuildAttempt("Game.BuildBlackMarketSmart", issued, reason);
				}
				else if (shouldPrioritizeMarketGrowth
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress))
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
					&& money >= (isBalancedSprawl ? (reserveCash + blackMarketCost) : blackMarketCost)
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress)
					&& (!isBalancedSprawl || counts.blackMarketsInProgress < 1))
				{
					chosenCommand = "Game.BuildBlackMarketSmart";
					issued = tryMacroBuildWithFallback("Game.BuildBlackMarketSmart", false, reason);
					recordBuildAttempt("Game.BuildBlackMarketSmart", issued, reason);
				}
				else if (isSprawlStyle
					&& totalPalaces > 0
					&& totalBlackMarkets < sprawlMarketCap
					&& money >= (isBalancedSprawl ? (reserveCash + blackMarketCost) : blackMarketCost)
					&& isBuildAttemptReady("Game.BuildBlackMarketSmart", counts.blackMarketsInProgress)
					&& (!isBalancedSprawl || counts.blackMarketsInProgress < 1))
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

				m_autonomyState.lastDecisionCategory = "macro";
				m_autonomyState.lastDecisionCommand = chosenCommand;
				m_autonomyState.lastDecisionReason = issued ? "ok" : reason;
				adapterLog(
					"autonomy_tick category=macro profile=%s money=%lu selected_zone=%lu zone_main=%d zone_center=(%.1f,%.1f) "
					"global[supply=%d barracks=%d arms=%d palace=%d markets=%d tunnels=%d stingers=%d workers=%d radar=%d soldiers=%d rpg=%d quads=%d scorpions=%d scuds=%d] "
					"zone[supply=%d barracks=%d arms=%d markets=%d tunnels=%d stingers=%d] action=%s issued=%d reason=%s",
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
					m_autonomyState.nextZoneIndex = (m_autonomyState.nextZoneIndex + 1u) % zones.size();
				}
				m_autonomyState.nextMacroTick = now + (issued ? 3000u : 2000u);
			}

			if (static_cast<LONG>(m_autonomyState.nextProductionTick - now) <= 0)
			{
				std::string reason;
				bool issued = false;
				const std::string profile = normalizeAsciiLower(m_autonomyState.profile);
				const bool isBalancedSprawl = (profile == "sprawl_balanced");
				const Int armyCap = isBalancedSprawl ? 100 : 9999;
				const Int combatCount = counts.soldiers + counts.rpg + counts.quads + counts.scorpions + counts.scudLaunchers;
				const UnsignedInt reserveCash = isBalancedSprawl ? 10000u : 0u;
				const bool openingInfrastructureReady = totalSupplyStashes >= 1 && totalBarracks >= 1 && totalArmsDealers >= 1;
				const bool shouldPauseCombatProduction =
					isBalancedSprawl
					&& (!openingInfrastructureReady
						|| money < reserveCash
						|| ((counts.blackMarketsInProgress > 0 || counts.supplyStashesInProgress > 0)
							&& money < (reserveCash + 2000u)));
				const Real zoneRadiusSq = std::max<Real>(160.0f, m_autonomyState.zoneRadius) * std::max<Real>(160.0f, m_autonomyState.zoneRadius);
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
					if (m_autonomyState.hasExplicitPlayerIndex)
					{
						queuedMessage["args"]["player_index"] = m_autonomyState.playerIndex;
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

				if (shouldPauseCombatProduction)
				{
					reason = openingInfrastructureReady ? "reserve_cash_recovery" : "opening_not_ready";
				}
				else if (isBalancedSprawl && combatCount >= armyCap)
				{
					reason = "army_cap_reached";
				}
				else if (counts.barracks > 0 && money >= 300u)
				{
					if (profile == "aggressive" || counts.rpg < counts.soldiers)
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
					}
					else
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
					}
				}
				else if (!issued && counts.armsDealers > 0 && money >= 700u)
				{
					if ((profile == "tech" || profile == "sprawl" || isBalancedSprawl)
						&& counts.palaces > 0
						&& counts.scudLaunchers < std::max<Int>(1, (counts.quads + counts.scorpions) / 10)
						&& money >= 1200u)
					{
						chosenCommand = "Game.QueueScudLauncher";
						issued = tryCommand("auto_prod", "Game.QueueScudLauncher", nlohmann::json::object({ {"count", 1} }), reason);
					}
					if (!issued && (profile == "aggressive" || counts.quads <= counts.scorpions))
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
					}
					else if (!issued)
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
					}
				}
				m_autonomyState.lastDecisionCategory = "production";
				m_autonomyState.lastDecisionCommand = chosenCommand;
				m_autonomyState.lastDecisionReason = issued ? "ok" : reason;
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
				m_autonomyState.nextProductionTick = now + (issued ? 2200u : 1500u);
			}

			if (static_cast<LONG>(m_autonomyState.nextTechTick - now) <= 0)
			{
				const std::string profile = normalizeAsciiLower(m_autonomyState.profile);
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
								m_autonomyState.lastDecisionCategory = "tech";
								m_autonomyState.lastDecisionCommand = "Game.PurchaseScience";
								m_autonomyState.lastDecisionReason = std::string("ok:") + scienceName;
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
								m_autonomyState.lastDecisionCategory = "tech";
								m_autonomyState.lastDecisionCommand = "Game.QueueUpgrade";
								m_autonomyState.lastDecisionReason = std::string("ok:") + kAutonomyUpgradePlan[i].upgradeName;
								break;
							}
						}
					}

					if (!issued)
					{
						m_autonomyState.lastDecisionCategory = "tech";
						m_autonomyState.lastDecisionCommand = "none";
						m_autonomyState.lastDecisionReason = reason.empty() ? "tech_prereq_missing" : reason;
					}

					m_autonomyState.nextTechTick = now + (issued ? 6000u : 8000u);
				}
				else
				{
					m_autonomyState.nextTechTick = now + 5000u;
				}
			}

			if (static_cast<LONG>(m_autonomyState.nextGuardTick - now) <= 0)
			{
				const std::string profile = normalizeAsciiLower(m_autonomyState.profile);
				const Int combatCount = counts.soldiers + counts.rpg + counts.quads + counts.scorpions;
				const DWORD cadenceMs = (profile == "aggressive") ? 5000u : (((profile == "sprawl" || profile == "sprawl_balanced" || profile == "defensive")) ? 7000u : 6000u);
				if (combatCount > 0)
				{
					std::string reason;
					const bool guarded = tryCommand("auto_guard", "Game.GuardAllIdleGroundCombat", nlohmann::json::object(), reason);
					m_autonomyState.nextGuardTick = now + (guarded ? cadenceMs : 4000u);
				}
				else
				{
					m_autonomyState.nextGuardTick = now + 3000u;
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
				m_autonomyState.profile = profile;
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
			if (!parseBias("economy_bias", m_autonomyState.economyBias)
				|| !parseBias("aggression_bias", m_autonomyState.aggressionBias)
				|| !parseBias("defense_bias", m_autonomyState.defenseBias)
				|| !parseBias("expansion_bias", m_autonomyState.expansionBias))
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
				m_autonomyState.sprawlMultiplier = std::max<Real>(0.5f, std::min<Real>(10.0f, sprawlMultiplierIt->get<Real>()));
			}

			const auto zoneRadiusIt = argsIt->find("zone_radius");
			if (zoneRadiusIt != argsIt->end())
			{
				if (!zoneRadiusIt->is_number())
				{
					reason = "invalid_zone_radius";
					return false;
				}
				m_autonomyState.zoneRadius = std::max<Real>(120.0f, std::min<Real>(4000.0f, zoneRadiusIt->get<Real>()));
			}

			const auto captureIt = argsIt->find("capture_tech");
			if (captureIt != argsIt->end())
			{
				if (!captureIt->is_boolean())
				{
					reason = "invalid_capture_tech";
					return false;
				}
				m_autonomyState.captureTech = captureIt->get<bool>();
			}

			const auto superIt = argsIt->find("allow_superweapons");
			if (superIt != argsIt->end())
			{
				if (!superIt->is_boolean())
				{
					reason = "invalid_allow_superweapons";
					return false;
				}
				m_autonomyState.allowSuperweapons = superIt->get<bool>();
			}

			const auto playerIndexIt = argsIt->find("player_index");
			if (playerIndexIt != argsIt->end())
			{
				if (!playerIndexIt->is_number_integer())
				{
					reason = "invalid_player_index";
					return false;
				}
				m_autonomyState.hasExplicitPlayerIndex = true;
				m_autonomyState.playerIndex = playerIndexIt->get<Int>();
			}

			const auto targetPlayerIt = argsIt->find("target_player_index");
			if (targetPlayerIt != argsIt->end())
			{
				if (!targetPlayerIt->is_number_integer())
				{
					reason = "invalid_target_player_index";
					return false;
				}
				m_autonomyState.hasExplicitTargetPlayerIndex = true;
				m_autonomyState.targetPlayerIndex = targetPlayerIt->get<Int>();
			}

			if (isAutonomyModeActive())
			{
				applyAutonomyRules();
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

			m_autonomyState.mode = mode;
			m_autonomyState.paused = false;
			applyAutonomyRules();
			return true;
		}

		void pauseAutonomy()
		{
			m_autonomyState.paused = true;
			applyAutonomyRules();
		}

		void resumeAutonomy()
		{
			m_autonomyState.paused = false;
			applyAutonomyRules();
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
			result["mode"] = m_autonomyState.mode;
			result["profile"] = m_autonomyState.profile;
			result["paused"] = m_autonomyState.paused;
			result["active"] = isAutonomyModeActive();
			result["capture_tech"] = m_autonomyState.captureTech;
			result["allow_superweapons"] = m_autonomyState.allowSuperweapons;
			result["economy_bias"] = m_autonomyState.economyBias;
			result["aggression_bias"] = m_autonomyState.aggressionBias;
			result["defense_bias"] = m_autonomyState.defenseBias;
			result["expansion_bias"] = m_autonomyState.expansionBias;
			result["sprawl_multiplier"] = m_autonomyState.sprawlMultiplier;
			result["zone_radius"] = m_autonomyState.zoneRadius;
			result["target_player_index"] = m_autonomyState.hasExplicitTargetPlayerIndex ? m_autonomyState.targetPlayerIndex : -1;
			result["last_applied_tick"] = static_cast<UnsignedInt>(m_autonomyState.lastAppliedTick);
			result["selected_zone"] = nlohmann::json::object({
				{"active", m_autonomyState.hasLastZone},
				{"anchor_id", m_autonomyState.hasLastZone ? m_autonomyState.lastZoneAnchorId : 0u},
				{"is_main_base", m_autonomyState.hasLastZone ? m_autonomyState.lastZoneIsMainBase : false},
				{"center_x", m_autonomyState.hasLastZone ? m_autonomyState.lastZoneCenterX : 0.0f},
				{"center_y", m_autonomyState.hasLastZone ? m_autonomyState.lastZoneCenterY : 0.0f}
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
					{"enabled", m_workerAutomationRule.enabled},
					{"min_idle_workers", m_workerAutomationRule.minIdleWorkers},
					{"queue_count", m_workerAutomationRule.queueCount},
					{"producer_kind", m_workerAutomationRule.hasExplicitProducerKind ? m_workerAutomationRule.producerKind : "default"}
				})},
				{"stash_worker", nlohmann::json::object({
					{"enabled", m_stashWorkerAutomationRule.enabled},
					{"target_workers_per_stash", m_stashWorkerAutomationRule.targetWorkersPerStash}
				})},
				{"attack", nlohmann::json::object({
					{"enabled", m_attackAutomationRule.enabled},
					{"min_units", m_attackAutomationRule.minUnits},
					{"group_size", m_attackAutomationRule.groupSize},
					{"distance", m_attackAutomationRule.distance}
				})},
				{"capture", nlohmann::json::object({
					{"enabled", m_captureAutomationRule.enabled},
					{"max_concurrent", m_captureAutomationRule.maxConcurrent},
					{"prefer_idle", m_captureAutomationRule.preferIdle}
				})},
				{"radar_van", nlohmann::json::object({
					{"enabled", m_radarVanAutomationRule.enabled},
					{"min_count", m_radarVanAutomationRule.minCount}
				})}
			});

			result["last_decision"] = nlohmann::json::object({
				{"category", m_autonomyState.lastDecisionCategory},
				{"command", m_autonomyState.lastDecisionCommand},
				{"reason", m_autonomyState.lastDecisionReason}
			});

			return result;
		}

		#include "AIControlAdapterTransport.inl"

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



