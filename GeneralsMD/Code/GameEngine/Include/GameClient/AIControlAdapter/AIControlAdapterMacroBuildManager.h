/**
 * AIControlAdapterMacroBuildManager.h
 *
 * Macro build intent generation for AIControlAdapter autonomy.
 *
 * This manager owns candidate construction and priority selection for the
 * extracted Phase 12 macro hot path. It does not issue game commands or reserve
 * workers; the adapter executor remains responsible for command dispatch.
 */

#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

#include <vector>

enum class AIControlAdapterMacroBuildExecutor
{
	MacroBuild,
	SupplyExpansion,
	SpecificZone
};

struct AIControlAdapterMacroBuildSpendGate
{
	bool allowed = true;
	const char* reason = "allowed";
};

struct AIControlAdapterMacroBuildIntent
{
	AIControlAdapterMacroBuildIntentOption option;
	bool preferZone = false;
	AIControlAdapterMacroBuildExecutor executor = AIControlAdapterMacroBuildExecutor::MacroBuild;
	unsigned int minCash = 0u;
	int inProgress = 0;
	int maxInProgress = 0;
	int zoneIndex = -1;
	const char* taskName = nullptr;
};

struct AIControlAdapterMacroBuildSnapshot
{
	bool isSprawlStyle = false;
	bool isBalancedSprawl = false;
	bool hasActiveZone = false;

	unsigned int money = 0u;
	unsigned int reserveCash = 0u;
	unsigned int blackMarketCost = 2500u;
	bool hasCompletedPalace = false;

	int stashZoneCount = 0;
	int developedZoneCount = 0;
	int desiredZoneCount = 1;
	int urgentZoneGapThreshold = 5;
	unsigned int expansionHighCashFloatThreshold = 10000u;
	float supplyFootprintRadius = 0.0f;
	int remoteSupplyZoneCount = 0;
	bool remoteZoneHasStash = false;
	bool remoteZoneNeedsFollowup = false;
	bool allowExpansionBeforeFullRemoteFollowup = false;
	bool shouldThrottleExtraStashGrowth = false;
	bool activeZoneThreatened = false;
	bool canScaleMilitaryProduction = false;
	int totalSupplyStashes = 0;
	int totalBarracks = 0;
	int totalArmsDealers = 0;
	int totalPalaces = 0;
	int completedBlackMarkets = 0;
	bool shouldPreserveReserve = false;
	bool wantsBaselineTwoMarkets = false;
	int sprawlSupplyCap = 1;
	int sprawlBarracksCap = 1;
	int sprawlArmsCap = 1;
	int sprawlMarketCap = 1;
	int sprawlTunnelCap = 1;
	int sprawlStingerCap = 1;
	int effectiveBarracksCap = 1;
	int effectiveArmsCap = 1;
	bool balancedZoneCanAddBarracks = true;
	bool balancedZoneCanAddArmsDealer = true;

	int supplyStashesInProgress = 0;
	int maxConcurrentExpansionStashes = 1;
	bool supplyBuildCooldownReady = false;
	int barracksInProgress = 0;
	int armsDealersInProgress = 0;
	int tunnelsInProgress = 0;
	int stingersInProgress = 0;
	bool barracksBuildCooldownReady = false;
	bool armsDealerBuildCooldownReady = false;
	bool tunnelBuildCooldownReady = false;
	bool stingerBuildCooldownReady = false;

	AIControlAdapterMacroBuildSpendGate expansionSpend;
	AIControlAdapterMacroBuildSpendGate marketRecoverySpend;
	AIControlAdapterMacroBuildSpendGate marketGrowthSpend;
	AIControlAdapterMacroBuildSpendGate palaceSpend;
	AIControlAdapterMacroBuildSpendGate staticDefenseSpend;

	int totalZoneTunnels = 0;
	int totalZoneStingers = 0;
	int totalZoneBarracks = 0;
	int totalZoneArmsDealers = 0;
	int activeZoneTunnelsInProgress = 0;
	int activeZoneStingersInProgress = 0;
	int activeZoneBarracksInProgress = 0;
	int activeZoneArmsDealersInProgress = 0;
	bool zoneSeedCooldownReady = false;

	int effectiveTotalBlackMarkets = 0;
	int inProgressBlackMarkets = 0;
	bool blackMarketBuildCooldownReady = false;
	int totalTunnels = 0;
	int totalStingers = 0;
	int staticDefenseZoneIndex = -1;
	const char* staticDefenseCommand = nullptr;
	const char* staticDefenseReason = "no_candidate";
	int palaceRedundancyZoneIndex = -1;
	const char* palaceRedundancyReason = "no_candidate";
	bool palaceBuildCooldownReady = false;
	int palacesInProgress = 0;
};

using AIControlAdapterMacroBuildManagerInput = AIControlAdapterMacroBuildSnapshot;

struct AIControlAdapterMacroBuildPolicyDecisions
{
	AIControlAdapterMacroExpansionDecision macroExpansion;
	AIControlAdapterZoneExpansionArbitrationResult expansionDecision;
	AIControlAdapterZoneSeedPackageDecision zoneSeedDecision;
	bool remoteZoneNeedsFollowup = false;
	int sprawlDesiredMarketCount = 1;
	bool canAttemptBlackMarketNow = false;
	bool shouldForceEcoRecovery = false;
	const char* ecoRecoveryBuild = nullptr;
	bool shouldBuildFirstMarket = false;
	bool shouldPrioritizeMarketGrowth = false;
};

struct AIControlAdapterMacroBuildTelemetry
{
	int stashZoneCount = 0;
	int developedZoneCount = 0;
	int desiredZoneCount = 1;
	int zoneGap = 0;
	bool reserveProtected = false;
	unsigned int cashAboveReserve = 0u;
	float supplyFootprintRadius = 0.0f;
	int remoteSupplyZoneCount = 0;
	int desiredRemoteSupplyZones = 0;
	bool coverageExpansionUrgent = false;
	bool preferRemoteSupplyExpansion = false;
	const char* expansionMode = "hold";
	const char* expansionReason = "target_reached";
	const char* expansionCommand = nullptr;
	bool expansionShouldAttempt = false;
	const char* expansionDecisionReason = "target_reached";
	bool expansionIsUrgent = false;
	int supplyStashesInProgress = 0;
	int maxConcurrentExpansionStashes = 1;
	bool shouldThrottleExtraStashGrowth = false;
	const char* zoneSeedCommand = nullptr;
	const char* zoneSeedPackageStage = "none";
	const char* zoneSeedReason = "no_remote_stash";
	int zoneSeedPriority = 0;
	bool activeZoneThreatened = false;
};

struct AIControlAdapterNonSupplyFootholdInput
{
	bool alreadyIssued = false;
	bool isSprawlStyle = false;
	int currentZones = 0;
	int desiredZones = 1;
	bool allowUrgentExpansionDespiteReserve = false;
	const char* previousReason = nullptr;
	bool hasCompletedPalace = false;
	int completedBlackMarkets = 0;
	unsigned int money = 0u;
	int totalTunnels = 0;
	int sprawlTunnelCap = 0;
	int tunnelsInProgress = 0;
	bool tunnelBuildReady = false;
};

struct AIControlAdapterNonSupplyFootholdDecision
{
	bool shouldEvaluatePlacement = false;
	bool shouldAttemptTunnel = false;
	bool supplyExpansionBlocked = false;
	bool economyIsStrong = false;
	bool hasStrongIncome = false;
	int durableIncome = 0;
	ZoneAnchorType anchorType = ZoneAnchorType::StrategicFoothold;
	const char* reason = "not_needed";
};

struct AIControlAdapterOpeningBuildInput
{
	const char* requiredOpeningBuild = nullptr;
	bool wantsSecondSupply = false;
	unsigned int money = 0u;
	int completedBarracks = 0;
	int supplyStashesInProgress = 0;
	int barracksInProgress = 0;
	int armsDealersInProgress = 0;
	int totalSupplyStashes = 0;
	bool supplyBuildReady = false;
	bool barracksBuildReady = false;
	bool armsDealerBuildReady = false;
};

struct AIControlAdapterOpeningBuildDecision
{
	bool handled = false;
	const char* displayCommand = nullptr;
	const char* command = nullptr;
	const char* reason = "";
	AIControlAdapterMacroBuildExecutor executor = AIControlAdapterMacroBuildExecutor::MacroBuild;
	bool preferZone = false;
	bool allowBarracksFallbackAfterSupplyFailure = false;
};

struct AIControlAdapterMacroHoldReasonInput
{
	bool shouldPreserveReserve = false;
	bool allowUrgentExpansionDespiteReserve = false;
	int stashZoneCount = 0;
	int desiredZoneCount = 1;
	int supplyStashesInProgress = 0;
	int tunnelsInProgress = 0;
	bool shouldThrottleExtraStashGrowth = false;
	bool hasCompletedPalace = false;
	int completedBlackMarkets = 0;
	int totalTunnels = 0;
	int sprawlTunnelCap = 0;
	bool shouldForceEcoRecovery = false;
	int effectiveTotalBlackMarkets = 0;
	int sprawlDesiredMarketCount = 0;
	bool canAttemptBlackMarketNow = false;
	bool remoteZoneNeedsFollowup = false;
};

struct AIControlAdapterStaticDefenseCandidateInput
{
	const AIControlAdapterStaticDefensePolicyResult* policy = nullptr;
	bool zoneExpansionUrgent = false;
	bool shouldPreserveReserve = false;
	bool isBalancedSprawl = false;
	unsigned int money = 0u;
	bool stingerBuildReady = false;
	bool tunnelBuildReady = false;
};

struct AIControlAdapterStaticDefenseCandidateDecision
{
	bool shouldBuild = false;
	const char* command = nullptr;
	const char* reason = "no_candidate";
};

struct AIControlAdapterPalaceRedundancyCandidateInput
{
	const AIControlAdapterPalaceRedundancyResult* policy = nullptr;
	bool palaceBuildReady = false;
};

struct AIControlAdapterPalaceRedundancyCandidateDecision
{
	bool shouldBuild = false;
	const char* reason = "no_candidate";
};

struct AIControlAdapterPalaceRecoveryCandidateInput
{
	const AIControlAdapterPalaceRecoveryDecision* policy = nullptr;
	bool openingInfrastructureReady = false;
};

struct AIControlAdapterPalaceRecoveryCandidateDecision
{
	bool shouldAttempt = false;
	const char* command = nullptr;
	const char* reason = "not_needed";
};

struct AIControlAdapterPalaceRecoveryResultInput
{
	const AIControlAdapterPalaceRecoveryDecision* policy = nullptr;
	bool openingInfrastructureReady = false;
	bool commandSelected = false;
	bool issued = false;
};

struct AIControlAdapterPalaceRecoveryResultDecision
{
	bool issued = false;
	const char* reason = "not_needed";
};

struct AIControlAdapterMacroBuildPlan
{
	std::vector<AIControlAdapterMacroBuildIntent> intents;
	AIControlAdapterMacroBuildIntentChoice choice;
	AIControlAdapterMacroBuildTelemetry telemetry;
};

class AIControlAdapterMacroBuildManager
{
public:
	AIControlAdapterMacroBuildPolicyDecisions ResolvePolicyDecisions(const AIControlAdapterMacroBuildSnapshot& snapshot) const;
	AIControlAdapterMacroBuildPlan BuildPlan(const AIControlAdapterMacroBuildSnapshot& snapshot) const;
	AIControlAdapterNonSupplyFootholdDecision EvaluateNonSupplyFoothold(
		const AIControlAdapterNonSupplyFootholdInput& input) const;
	AIControlAdapterOpeningBuildDecision EvaluateOpeningBuild(
		const AIControlAdapterOpeningBuildInput& input) const;
	const char* EvaluateMacroHoldReason(
		const AIControlAdapterMacroHoldReasonInput& input) const;
	AIControlAdapterStaticDefenseCandidateDecision EvaluateStaticDefenseCandidate(
		const AIControlAdapterStaticDefenseCandidateInput& input) const;
	AIControlAdapterPalaceRedundancyCandidateDecision EvaluatePalaceRedundancyCandidate(
		const AIControlAdapterPalaceRedundancyCandidateInput& input) const;
	AIControlAdapterPalaceRecoveryCandidateDecision EvaluatePalaceRecoveryCandidate(
		const AIControlAdapterPalaceRecoveryCandidateInput& input) const;
	AIControlAdapterPalaceRecoveryResultDecision EvaluatePalaceRecoveryResult(
		const AIControlAdapterPalaceRecoveryResultInput& input) const;

private:
	void AddIntent(
		AIControlAdapterMacroBuildPlan& plan,
		const char* category,
		const char* command,
		int priority,
		bool policyAllows,
		const char* policyReason,
		unsigned int minCash,
		int inProgress,
		int maxInProgress,
		bool cooldownReady,
		const AIControlAdapterMacroBuildSpendGate& spend,
		bool preferZone,
		AIControlAdapterMacroBuildExecutor executor,
		unsigned int money,
		int zoneIndex = -1,
		const char* taskName = nullptr) const;
};
