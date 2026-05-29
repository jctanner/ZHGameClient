#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapterEnemyMemory.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"
#include "GameClient/AIControlAdapter/AIControlAdapterWMDTarget.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>
#include <map>

namespace
{
	std::string AIControlAdapterLowerCopy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) -> unsigned char
		{
			return static_cast<unsigned char>(std::tolower(ch));
		});
		return value;
	}

	bool AIControlAdapterContainsToken(const std::string& lower, const char* token)
	{
		return lower.find(token) != std::string::npos;
	}

	bool AIControlAdapterIsExcludedEnemyMemoryTemplate(const std::string& templateName)
	{
		const std::string lower = AIControlAdapterLowerCopy(templateName);
		return AIControlAdapterContainsToken(lower, "sneakattack");
	}
}

bool AIControlAdapterShouldPauseCombatProduction(const AIControlAdapterProductionPolicyInputs& inputs)
{
	const bool ecoStructuresInProgress = (inputs.blackMarketsInProgress > 0 || inputs.supplyStashesInProgress > 0);

	if (!inputs.isBalancedSprawl)
	{
		return false;
	}

	if (!inputs.openingInfrastructureReady)
	{
		return true;
	}

	if (!inputs.openingEconomyReady)
	{
		return true;
	}

	if (inputs.money < inputs.reserveCash)
	{
		return true;
	}

	if (ecoStructuresInProgress && inputs.money < (inputs.reserveCash + 2000u))
	{
		return true;
	}

	if (inputs.wasRecoveringFromReserve && inputs.money < (inputs.reserveCash + 3000u))
	{
		return true;
	}

	return false;
}

bool AIControlAdapterShouldHoldArmyCap(const AIControlAdapterCombatProductionPolicyInputs& inputs)
{
	if (inputs.shouldPauseForEconomy)
	{
		return false;
	}

	if (!inputs.isBalancedSprawl || inputs.armyCap <= 0)
	{
		return false;
	}

	if (inputs.combatCount >= inputs.armyCap)
	{
		return true;
	}

	if (inputs.wasArmyCapReached && inputs.combatCount >= (inputs.armyCap - 10))
	{
		return true;
	}

	return false;
}

int AIControlAdapterGetEffectiveArmyCap(const AIControlAdapterEffectiveArmyCapPolicyInputs& inputs)
{
	const int baseArmyCap = inputs.baseArmyCap > 0 ? inputs.baseArmyCap : 0;
	if (!inputs.isBalancedSprawl || inputs.money < 100000u)
	{
		return baseArmyCap;
	}

	const int productionCapacity = std::max(0, inputs.barracks) + std::max(0, inputs.armsDealers);
	const int productionCapacityCap = 100 + (productionCapacity * 3);
	const int incomeSupportedCap = 100 + (std::max(0, inputs.incomePerMinute) / 500);
	const int surplusCap = std::min(productionCapacityCap, incomeSupportedCap);
	const int cappedSurplusCap = std::min(300, surplusCap);
	return std::max(baseArmyCap, cappedSurplusCap);
}

const char* AIControlAdapterGetRequiredOpeningBuild(const AIControlAdapterOpeningPolicyInputs& inputs)
{
	if (inputs.completedSupplyStashes < 1)
	{
		return "Game.BuildSupplyStashSmart";
	}

	if (inputs.completedBarracks < 1)
	{
		return "Game.BuildBarracksSmart";
	}

	if (inputs.completedArmsDealers < 1)
	{
		return "Game.BuildArmsDealerSmart";
	}

	return nullptr;
}

bool AIControlAdapterCanAttemptBlackMarket(const AIControlAdapterBlackMarketPolicyInputs& inputs)
{
	if (!inputs.hasCompletedPalace)
	{
		return false;
	}

	if (!inputs.isBalancedSprawl)
	{
		return true;
	}

	if (inputs.blackMarketsInProgress > 0)
	{
		return false;
	}

	if (inputs.completedBlackMarkets < 1)
	{
		return inputs.money >= inputs.reserveCash;
	}

	return inputs.money >= (inputs.reserveCash + 2500u);
}

const char* AIControlAdapterGetEcoRecoveryBuild(const AIControlAdapterEcoRecoveryPolicyInputs& inputs)
{
	if (!inputs.isBalancedSprawl)
	{
		return nullptr;
	}

	if (inputs.totalSupplyStashes < 2)
	{
		return "Game.BuildSupplyStashSmart";
	}

	if (inputs.totalSupplyStashes < 3 && !inputs.shouldThrottleExtraStashGrowth)
	{
		return "Game.BuildSupplyStashSmart";
	}

	if (inputs.canAttemptBlackMarket && inputs.totalBlackMarkets < inputs.desiredMarketCount)
	{
		return "Game.BuildBlackMarketSmart";
	}

	if (!inputs.shouldThrottleExtraStashGrowth)
	{
		return "Game.BuildSupplyStashSmart";
	}

	return nullptr;
}

bool AIControlAdapterIsSettlingSensitiveBuild(const char* commandName)
{
	if (commandName == nullptr)
	{
		return false;
	}

	return std::strcmp(commandName, "Game.BuildSupplyStashSmart") == 0
		|| std::strcmp(commandName, "Game.BuildBarracksSmart") == 0
		|| std::strcmp(commandName, "Game.BuildArmsDealerSmart") == 0
		|| std::strcmp(commandName, "Game.BuildPalaceSmart") == 0
		|| std::strcmp(commandName, "Game.BuildTunnelNetwork") == 0
		|| std::strcmp(commandName, "Game.BuildStingerSite") == 0;
}

unsigned int AIControlAdapterGetBuildRetryDelayMs(const char* commandName, bool success, const char* reason)
{
	const bool settlingSensitiveBuild = AIControlAdapterIsSettlingSensitiveBuild(commandName);
	unsigned int delayMs = success ? 9000u : 4000u;

	if (reason != nullptr && std::strcmp(reason, "idle_worker_not_found") == 0)
	{
		delayMs = settlingSensitiveBuild ? 2000u : 1500u;
	}
	else if (reason != nullptr && std::strcmp(reason, "construct_site_not_created") == 0)
	{
		delayMs = settlingSensitiveBuild ? 4500u : 5000u;
	}
	else if (!success && settlingSensitiveBuild)
	{
		delayMs = 4000u;
	}

	if (commandName == nullptr)
	{
		return delayMs;
	}

	if (std::strcmp(commandName, "Game.BuildSupplyStashSmart") == 0)
	{
		if (success)
		{
			return 7000u;
		}
		if (reason != nullptr && std::strcmp(reason, "construct_site_not_created") == 0)
		{
			return 2500u;
		}
		return delayMs < 2500u ? 2500u : delayMs;
	}

	if (std::strcmp(commandName, "Game.BuildBarracksSmart") == 0)
	{
		if (success)
		{
			return 9000u;
		}
		if (reason != nullptr && std::strcmp(reason, "construct_site_not_created") == 0)
		{
			return 3000u;
		}
		return delayMs < 3000u ? 3000u : delayMs;
	}

	if (std::strcmp(commandName, "Game.BuildArmsDealerSmart") == 0)
	{
		if (success)
		{
			return 9000u;
		}
		if (reason != nullptr && std::strcmp(reason, "construct_site_not_created") == 0)
		{
			return 3500u;
		}
		return delayMs < 3500u ? 3500u : delayMs;
	}

	if (std::strcmp(commandName, "Game.BuildPalaceSmart") == 0)
	{
		if (success)
		{
			return 12000u;
		}
		if (!success && delayMs < 6000u)
		{
			return 6000u;
		}
	}

	return delayMs;
}

bool AIControlAdapterShouldAbortSciencePlanForTick(const char* reason)
{
	return reason != nullptr && std::strcmp(reason, "science_not_purchasable") == 0;
}

bool AIControlAdapterShouldAbortUpgradePlanForTick(const char* reason)
{
	if (reason == nullptr)
	{
		return false;
	}

	return std::strcmp(reason, "queue_full") == 0
		|| std::strcmp(reason, "palace_not_found") == 0
		|| std::strcmp(reason, "black_market_not_found") == 0;
}

bool AIControlAdapterShouldAbortUpgradePlanForReason(const char* reason)
{
	if (AIControlAdapterShouldAbortUpgradePlanForTick(reason))
	{
		return true;
	}

	if (reason == nullptr)
	{
		return false;
	}

	return std::strcmp(reason, "upgrade_already_complete") == 0;
}

unsigned int AIControlAdapterGetTechRetryDelayMs(bool issued, const char* reason)
{
	if (issued)
	{
		return 6000u;
	}

	if (reason == nullptr || *reason == '\0')
	{
		return 8000u;
	}

	if (std::strcmp(reason, "tech_prereq_missing") == 0)
	{
		return 12000u;
	}

	if (std::strcmp(reason, "science_not_purchasable") == 0
		|| std::strcmp(reason, "producer_cannot_make_upgrade") == 0
		|| std::strcmp(reason, "palace_not_found") == 0
		|| std::strcmp(reason, "black_market_not_found") == 0
		|| std::strcmp(reason, "queue_full") == 0
		|| std::strcmp(reason, "upgrade_already_in_production") == 0
		|| std::strcmp(reason, "upgrade_already_complete") == 0)
	{
		return 15000u;
	}

	return 8000u;
}

unsigned int AIControlAdapterGetProductionRetryDelayMs(bool issued, const char* reason)
{
	if (issued)
	{
		return 2200u;
	}

	if (reason == nullptr || *reason == '\0')
	{
		return 1500u;
	}

	if (std::strcmp(reason, "queue_full") == 0)
	{
		return 3500u;
	}

	if (std::strcmp(reason, "producer_under_construction") == 0)
	{
		return 5000u;
	}

	if (std::strcmp(reason, "no_prereq") == 0)
	{
		return 6000u;
	}

	if (std::strcmp(reason, "no_money") == 0)
	{
		return 2500u;
	}

	return 1500u;
}

bool AIControlAdapterShouldQueueRadarVan(const AIControlAdapterRadarVanPolicyInputs& inputs)
{
	if (inputs.shouldPauseForEconomy || inputs.shouldHoldArmyCap)
	{
		return false;
	}

	if (inputs.armsDealers <= 0)
	{
		return false;
	}

	if (inputs.radarVans >= inputs.minRadarVans)
	{
		return false;
	}

	if (inputs.combatVehicles <= 0)
	{
		return false;
	}

	return inputs.radarVans < inputs.combatVehicles;
}

bool AIControlAdapterShouldPreferVehicleReplenishment(const AIControlAdapterVehicleSustainPolicyInputs& inputs)
{
	if (!inputs.isBalancedSprawl || inputs.armsDealers <= 0)
	{
		return false;
	}

	const int combatVehicles = inputs.quads + inputs.scorpions + inputs.scudLaunchers;
	const int vehicleSupport = inputs.radarVans;
	const int frontlineVehicles = combatVehicles - vehicleSupport;
	const int infantry = inputs.soldiers + inputs.rpg;
	const int desiredVehicleFloor = inputs.armsDealers <= 1 ? 4 : (inputs.armsDealers * 3);

	if (frontlineVehicles <= 0)
	{
		return true;
	}

	if (frontlineVehicles < desiredVehicleFloor)
	{
		return true;
	}

	if (inputs.barracks > 0 && frontlineVehicles * 2 < infantry)
	{
		return true;
	}

	return false;
}

bool AIControlAdapterShouldTreatMacroAsComplete(const AIControlAdapterMacroCompletionPolicyInputs& inputs)
{
	if (!inputs.isSprawlStyle)
	{
		return false;
	}

	if (inputs.shouldForceEcoRecovery || inputs.remoteZoneNeedsFollowup)
	{
		return false;
	}

	return inputs.totalSupplyStashes >= inputs.supplyCap
		&& inputs.totalBarracks >= inputs.barracksCap
		&& inputs.totalArmsDealers >= inputs.armsCap
		&& inputs.totalBlackMarkets >= inputs.marketCap
		&& inputs.totalTunnels >= inputs.tunnelCap
		&& inputs.totalStingers >= inputs.stingerCap;
}

bool AIControlAdapterShouldPrioritizeMarketsOverProductionBuildings(
	const AIControlAdapterMarketGrowthPolicyInputs& inputs)
{
	if (!inputs.isBalancedSprawl || !inputs.isSprawlStyle || !inputs.canAttemptBlackMarket)
	{
		return false;
	}

	if (inputs.shouldForceEcoRecovery)
	{
		return true;
	}

	if (inputs.shouldPrioritizeMarketGrowth && inputs.totalBlackMarkets < inputs.desiredMarketCount)
	{
		return true;
	}

	if (inputs.shouldPreserveReserve)
	{
		return true;
	}

	if (!inputs.canScaleMilitaryProduction)
	{
		return inputs.totalBlackMarkets < inputs.desiredMarketCount;
	}

	const bool hasRoomForMoreProduction = inputs.totalBarracks < inputs.barracksCap
		|| inputs.totalArmsDealers < inputs.armsCap;
	return inputs.totalBlackMarkets < inputs.desiredMarketCount && hasRoomForMoreProduction;
}

bool AIControlAdapterIsZoneExpansionUrgent(const AIControlAdapterZoneExpansionPolicyInputs& inputs)
{
	if (inputs.currentZoneCount >= inputs.desiredZoneCount)
	{
		return false;
	}

	const int zoneGap = inputs.desiredZoneCount - inputs.currentZoneCount;
	return zoneGap >= inputs.zoneGapThreshold;
}

AIControlAdapterZoneExpansionArbitrationResult AIControlAdapterChooseZoneExpansionAction(
	const AIControlAdapterZoneExpansionArbitrationInputs& inputs)
{
	AIControlAdapterZoneExpansionArbitrationResult result;
	result.shouldAttemptExpansion = false;
	result.command = nullptr;
	result.reason = "unknown";
	result.isUrgent = false;
	result.allowReserveSpend = false;

	// Check target reached
	if (inputs.stashZoneCount >= inputs.desiredZoneCount)
	{
		result.reason = "target_reached";
		return result;
	}

	// Check build in progress
	if (inputs.supplyStashesInProgress > 0)
	{
		result.reason = "build_in_progress";
		return result;
	}

	// Check throttle
	if (inputs.shouldThrottleExtraStashGrowth)
	{
		result.reason = "throttled";
		return result;
	}

	// Check build attempt ready (cooldown)
	if (!inputs.isBuildAttemptReady)
	{
		result.reason = "build_cooldown";
		return result;
	}

	// Urgent expansion: large zone gap + high cash float
	if (inputs.zoneExpansionIsUrgent && inputs.allowUrgentExpansionDespiteReserve)
	{
		// Urgent expansion can proceed even if remoteZoneNeedsFollowup
		// because the zone deficit is critical
		const unsigned int minCash = inputs.isBalancedSprawl ? 2200u : 1800u;
		if (inputs.money >= minCash)
		{
			result.shouldAttemptExpansion = true;
			result.command = "Game.BuildSupplyStashSmart";
			result.reason = "urgent_high_cash";
			result.isUrgent = true;
			result.allowReserveSpend = true;
			return result;
		}
		else
		{
			result.reason = "urgent_low_cash";
			return result;
		}
	}

	// Normal expansion: requires followup complete
	if (!inputs.remoteZoneNeedsFollowup)
	{
		const unsigned int minCash = inputs.isBalancedSprawl ? (inputs.reserveCash + 1800u) : 1800u;
		if (inputs.money >= minCash)
		{
			result.shouldAttemptExpansion = true;
			result.command = "Game.BuildSupplyStashSmart";
			result.reason = "normal";
			result.isUrgent = false;
			result.allowReserveSpend = false;
			return result;
		}
		else
		{
			result.reason = "normal_low_cash";
			return result;
		}
	}

	// Below target but followup needed
	result.reason = "followup_needed";
	return result;
}

AIControlAdapterScudStormConstructionPolicyResult AIControlAdapterEvaluateScudStormConstruction(
	const AIControlAdapterScudStormConstructionPolicyInputs& inputs)
{
	AIControlAdapterScudStormConstructionPolicyResult result;
	result.spendAllowed = false;
	result.zoneExpansionUrgent = AIControlAdapterIsZoneExpansionUrgent({
		inputs.currentZoneCount,
		inputs.desiredZoneCount,
		inputs.zoneGapThreshold
	});
	result.highCashOverride = false;
	result.cashFloat = inputs.money > inputs.reserveCash ? (inputs.money - inputs.reserveCash) : 0u;
	result.zoneGap = inputs.desiredZoneCount - inputs.currentZoneCount;
	result.maxInProgress = std::max(1, inputs.normalMaxInProgress);
	result.reason = "unknown";

	if (!inputs.productionNeeded)
	{
		result.reason = "target_reached";
		return result;
	}

	if (!inputs.prereqReady)
	{
		result.reason = "prereq_missing";
		return result;
	}

	if (inputs.money < inputs.scudStormCost)
	{
		result.reason = "cash_below_scud_storm_cost";
		return result;
	}

	if (inputs.money <= inputs.reserveCash + inputs.scudStormCost)
	{
		result.reason = "reserve_protected";
		return result;
	}

	const bool veryHighCashFloat = result.cashFloat >= inputs.highCashFloatThreshold;
	if (veryHighCashFloat)
	{
		result.maxInProgress = std::max(result.maxInProgress, 2);
	}

	if (result.zoneExpansionUrgent && !veryHighCashFloat)
	{
		result.reason = "urgent_expansion_priority";
		return result;
	}

	if (inputs.inProgressScudStorms >= result.maxInProgress)
	{
		result.reason = "in_progress_cap";
		return result;
	}

	result.spendAllowed = true;
	result.highCashOverride = result.zoneExpansionUrgent || result.maxInProgress > inputs.normalMaxInProgress;
	result.reason = result.highCashOverride ? "high_cash_override" : "spend_allowed";
	return result;
}

AIControlAdapterZoneDefenseBudgetResult AIControlAdapterChooseZoneDefenseBudget(
	const AIControlAdapterZoneDefenseBudgetInputs& inputs)
{
	AIControlAdapterZoneDefenseBudgetResult result;
	result.threatSeverity = 0;
	result.desiredDefenders = 0;
	result.maxNewAssignments = 0;
	result.localReserve = 0;
	result.minHoldMs = 30000u;
	result.timeoutMs = 90000u;
	result.allowFrontDonors = false;
	result.criticalOverride = false;
	result.reason = "no_active_threat";

	if (inputs.threatLevel == "critical")
	{
		result.threatSeverity = 4;
		result.desiredDefenders = 18;
		result.maxNewAssignments = 12;
		result.minHoldMs = 45000u;
		result.timeoutMs = 120000u;
		result.allowFrontDonors = true;
		result.criticalOverride = true;
		result.reason = inputs.hasActiveCriticalAllocation ? "critical_preserve_other_fronts" : "critical_override";
	}
	else if (inputs.threatLevel == "high")
	{
		result.threatSeverity = 3;
		result.desiredDefenders = 12;
		result.maxNewAssignments = 8;
		result.minHoldMs = 40000u;
		result.timeoutMs = 105000u;
		result.allowFrontDonors = false;
		result.reason = "high_threat_capped";
	}
	else if (inputs.threatLevel == "medium")
	{
		result.threatSeverity = 2;
		result.desiredDefenders = 8;
		result.maxNewAssignments = 5;
		result.minHoldMs = 35000u;
		result.timeoutMs = 90000u;
		result.reason = "medium_threat_capped";
	}
	else if (inputs.threatLevel == "low")
	{
		result.threatSeverity = 1;
		result.desiredDefenders = 4;
		result.maxNewAssignments = 3;
		result.minHoldMs = 30000u;
		result.timeoutMs = 75000u;
		result.reason = "low_threat_capped";
	}

	if (result.threatSeverity <= 0)
	{
		return result;
	}

	if (inputs.isMainBase)
	{
		result.localReserve = 8;
	}
	else if (inputs.isActiveZone || inputs.isFrontier)
	{
		result.localReserve = 6;
	}
	else if (inputs.isDeveloped)
	{
		result.localReserve = 4;
	}
	else
	{
		result.localReserve = 2;
	}

	if (result.threatSeverity <= 2 && (inputs.isActiveZone || inputs.isFrontier))
	{
		result.maxNewAssignments = 0;
		result.reason = "front_reserve";
	}

	if (inputs.localFriendlyCombat > 0)
	{
		result.desiredDefenders = std::max(1, result.desiredDefenders - std::max(0, inputs.localFriendlyCombat / 2));
	}

	const int availableAfterReserve = inputs.availableIdleCombat - result.localReserve;
	int allowedByReserve = std::max(0, availableAfterReserve);
	if (result.criticalOverride)
	{
		allowedByReserve = std::max(0, inputs.availableIdleCombat - (inputs.hasActiveCriticalAllocation ? 4 : 2));
	}
	result.maxNewAssignments = std::max(0, std::min(result.maxNewAssignments, allowedByReserve));

	if (inputs.activeDefenseAllocations >= 3 && !result.criticalOverride)
	{
		result.maxNewAssignments = std::min(result.maxNewAssignments, 3);
		result.reason = "allocation_pressure";
	}

	return result;
}

AIControlAdapterZoneDefenseAllocationResult AIControlAdapterEvaluateZoneDefenseAllocation(
	const AIControlAdapterZoneDefenseAllocationInputs& inputs)
{
	AIControlAdapterZoneDefenseAllocationResult result;
	result.shouldIssueCommand = false;
	result.shouldReinforce = false;
	result.shouldRelease = false;
	result.requestedNewAssignments = 0;
	result.reason = "no_action";

	if (inputs.newThreatSeverity <= 0)
	{
		result.shouldRelease = inputs.hasActiveAllocation;
		result.reason = "threat_cleared";
		return result;
	}

	if (!inputs.hasActiveAllocation)
	{
		result.shouldIssueCommand = inputs.desiredDefenders > 0;
		result.requestedNewAssignments = std::max(0, inputs.desiredDefenders);
		result.reason = result.shouldIssueCommand ? "new_allocation" : "no_budget";
		return result;
	}

	if (inputs.expiryTick > 0 && inputs.currentTick >= inputs.expiryTick)
	{
		result.shouldRelease = true;
		result.reason = "expired";
		return result;
	}

	const float dx = inputs.newTargetX - inputs.oldTargetX;
	const float dy = inputs.newTargetY - inputs.oldTargetY;
	const bool targetMoved = (dx * dx + dy * dy) >= (inputs.targetMoveThreshold * inputs.targetMoveThreshold);
	const bool threatEscalated = inputs.newThreatSeverity > inputs.activeSeverity;
	const bool defendersMissing = inputs.assignedCount < std::max(1, inputs.desiredDefenders / 2);

	if (threatEscalated || targetMoved || defendersMissing)
	{
		const int missing = std::max(0, inputs.desiredDefenders - inputs.assignedCount);
		result.shouldIssueCommand = missing > 0;
		result.shouldReinforce = result.shouldIssueCommand;
		result.requestedNewAssignments = missing;
		result.reason = threatEscalated ? "threat_escalated" : (defendersMissing ? "defenders_missing" : "target_moved");
		return result;
	}

	if (inputs.currentTick < inputs.holdUntilTick && inputs.newThreatSeverity <= inputs.activeSeverity)
	{
		result.reason = "min_hold";
		return result;
	}

	result.reason = "active_allocation";
	return result;
}

AIControlAdapterZoneThreatSourceResult AIControlAdapterClassifyZoneThreatSource(
	const AIControlAdapterZoneThreatSourceInputs& inputs)
{
	AIControlAdapterZoneThreatSourceResult result;
	result.type = "unknown";
	result.response = "limited_scout";
	result.reason = "damage_source_unknown";
	result.severity = "low";

	const bool severeDamage =
		inputs.damageFraction >= 0.30f ||
		inputs.damageDelta >= 500.0f ||
		inputs.damagedStructures >= 2 ||
		inputs.destroyedStructures > 0;
	if (severeDamage)
	{
		result.severity = "critical";
	}
	else if (inputs.damageFraction >= 0.15f)
	{
		result.severity = "high";
	}
	else if (inputs.damageFraction >= 0.05f)
	{
		result.severity = "medium";
	}

	if (inputs.localEnemyCount > 0)
	{
		result.type = "unit_attack";
		result.response = "defend";
		result.reason = severeDamage && inputs.recentWmd ? "local_enemy_after_wmd_damage" : "local_visible_enemy";
		return result;
	}

	if (inputs.enemyArtilleryCount > 0)
	{
		result.type = "artillery_attack";
		result.response = "counterbattery";
		result.reason = "visible_enemy_artillery_no_local_enemy";
		return result;
	}

	if (severeDamage && inputs.recentWmd)
	{
		result.type = "wmd_strike";
		result.response = "rebuild_only";
		result.reason = "severe_damage_no_local_enemy_recent_wmd";
		return result;
	}

	return result;
}

AIControlAdapterStaticDefensePolicyResult AIControlAdapterChooseStaticDefensePolicy(
	const AIControlAdapterStaticDefensePolicyInputs& inputs)
{
	AIControlAdapterStaticDefensePolicyResult result;
	result.desiredTunnels = 0;
	result.desiredStingers = 0;
	result.effectiveTunnels = inputs.liveTunnels + inputs.inProgressTunnels + inputs.reservedTunnels;
	result.effectiveStingers = inputs.liveStingers + inputs.inProgressStingers + inputs.reservedStingers;
	result.shouldBuildTunnel = false;
	result.shouldBuildStinger = false;
	result.role = "rear";
	result.reason = "target_met";

	if (inputs.isMainBase)
	{
		result.role = "main_base";
		result.desiredTunnels = 1;
		result.desiredStingers = 1;
	}
	else if (inputs.isAnchorZone)
	{
		result.role = "anchor";
		result.desiredTunnels = 2;
		result.desiredStingers = 2;
	}
	else if (inputs.isActiveZone)
	{
		result.role = "active";
		result.desiredTunnels = 2;
		result.desiredStingers = 2;
	}
	else if (inputs.isFrontier)
	{
		result.role = "frontier";
		result.desiredTunnels = 2;
		result.desiredStingers = 2;
	}
	else if (inputs.isDeveloped)
	{
		result.role = "developed_rear";
		result.desiredTunnels = 1;
		result.desiredStingers = 0;
	}
	else
	{
		result.role = "rear";
	}

	if (inputs.repeatedAttack)
	{
		result.desiredStingers = std::min(3, result.desiredStingers + 1);
		result.reason = "repeated_attack";
	}

	result.shouldBuildTunnel = result.effectiveTunnels < result.desiredTunnels;
	result.shouldBuildStinger = result.effectiveStingers < result.desiredStingers;
	if (!result.shouldBuildTunnel && !result.shouldBuildStinger && std::string(result.reason) != "repeated_attack")
	{
		result.reason = "target_met";
	}
	else if (result.shouldBuildTunnel || result.shouldBuildStinger)
	{
		result.reason = inputs.repeatedAttack ? "repeated_attack" : "below_desired";
	}
	return result;
}

AIControlAdapterPalaceRedundancyResult AIControlAdapterEvaluatePalaceRedundancy(
	const AIControlAdapterPalaceRedundancyInputs& inputs)
{
	AIControlAdapterPalaceRedundancyResult result;
	result.desiredZonePalaces = 0;
	result.spendAllowed = false;
	result.shouldBuild = false;
	result.role = inputs.zoneRole == "frontier" ? "frontier" :
		(inputs.zoneRole == "active" ? "active" :
			(inputs.zoneRole == "anchor" ? "anchor" :
				(inputs.zoneRole == "main_base" ? "main_base" : "rear")));
	result.reason = "not_needed";

	if (!inputs.hasTechUnlocked)
	{
		result.reason = "tech_locked";
		return result;
	}
	if (inputs.globalLivePalaces < 1)
	{
		result.desiredZonePalaces = 1;
		result.spendAllowed = inputs.reserveProtected;
		result.shouldBuild = result.spendAllowed && inputs.globalInProgressPalaces < 1;
		result.reason = result.shouldBuild ? "global_minimum" : (inputs.reserveProtected ? "palace_in_progress" : "reserve_protected");
		return result;
	}
	if (inputs.urgentExpansion)
	{
		result.reason = "urgent_expansion_priority";
		return result;
	}
	if (inputs.globalInProgressPalaces > 0)
	{
		result.reason = "palace_in_progress";
		return result;
	}
	if (!inputs.reserveProtected || inputs.cashFloat < 15000u)
	{
		result.reason = "reserve_protected";
		return result;
	}
	if (!(inputs.isDeveloped || inputs.isFrontier || inputs.isAnchorZone))
	{
		result.reason = "zone_not_mature";
		return result;
	}
	if (inputs.zoneLivePalaces + inputs.zoneInProgressPalaces > 0)
	{
		result.desiredZonePalaces = 1;
		result.reason = "target_met";
		return result;
	}

	result.desiredZonePalaces = 1;
	result.spendAllowed = true;
	result.shouldBuild = true;
	result.reason = "redundant_anchor";
	return result;
}

bool AIControlAdapterIsBattlefieldArtilleryTemplate(const std::string& templateName, bool isStructure)
{
	return AIControlAdapterClassifyMobileSiegeTemplate(templateName, isStructure, true).accepted;
}

AIControlAdapterMobileSiegeTemplateResult AIControlAdapterClassifyMobileSiegeTemplate(
	const std::string& templateName,
	bool isStructure,
	bool isEnemy)
{
	if (isStructure)
	{
		return { false, "effect_rejected" };
	}
	if (!isEnemy)
	{
		return { false, "not_enemy" };
	}
	const std::string lower = AIControlAdapterLowerCopy(templateName);
	if (lower.find("nuclearmissile") != std::string::npos ||
		lower.find("scudstorm") != std::string::npos ||
		lower.find("particlecannon") != std::string::npos)
	{
		return { false, "effect_rejected" };
	}
	if (lower.find("shell") != std::string::npos ||
		lower.find("projectile") != std::string::npos ||
		lower.find("weapon") != std::string::npos ||
		lower.find("explosion") != std::string::npos ||
		lower.find("debris") != std::string::npos)
	{
		return { false, "projectile_rejected" };
	}
	if (lower.find("artillerycannon") != std::string::npos)
	{
		return { false, "projectile_rejected" };
	}
	if (lower.find("nukecannon") != std::string::npos ||
		lower.find("infernocannon") != std::string::npos ||
		lower.find("scudlauncher") != std::string::npos ||
		lower.find("tomahawk") != std::string::npos ||
		lower.find("rocketbuggy") != std::string::npos)
	{
		return { true, "real_vehicle" };
	}
	return { false, "effect_rejected" };
}

AIControlAdapterCounterbatteryPolicyResult AIControlAdapterChooseCounterbatteryPolicy(
	const AIControlAdapterCounterbatteryPolicyInputs& inputs)
{
	AIControlAdapterCounterbatteryPolicyResult result;
	result.desiredGroups = inputs.visibleArtilleryThreats > 0 ? 1 : 0;
	result.desiredAssignedUnits = inputs.visibleArtilleryThreats > 0 ? 3 : 0;
	result.shouldAssign = false;
	result.productionNeeded = false;
	result.reason = "no_artillery_threat";

	if (inputs.visibleArtilleryThreats <= 0)
	{
		return result;
	}
	if (inputs.activeCounterbatteryTasks >= result.desiredGroups)
	{
		result.reason = "active_task_exists";
		return result;
	}
	if (inputs.availableCounterUnits > 0)
	{
		result.shouldAssign = true;
		result.reason = "mobile_siege_visible";
		return result;
	}

	const int effectiveMobileScuds = inputs.liveMobileScudLaunchers + inputs.queuedMobileScudLaunchers;
	if (inputs.hasProductionPrerequisites && effectiveMobileScuds < inputs.maxMobileScudLaunchers)
	{
		result.productionNeeded = true;
		result.reason = "production_needed";
		return result;
	}

	result.reason = inputs.hasProductionPrerequisites ? "no_counter_available" : "missing_prerequisites";
	return result;
}

AIControlAdapterRocketBuggyMixResult AIControlAdapterChooseRocketBuggyMix(
	const AIControlAdapterRocketBuggyMixInputs& inputs)
{
	AIControlAdapterRocketBuggyMixResult result;
	result.desiredBuggies = 0;
	result.productionNeeded = false;
	result.reason = "prereq_missing";

	if (!inputs.hasPrerequisites)
	{
		return result;
	}

	const int currentAndQueued = inputs.liveBuggies + inputs.queuedBuggies;
	const int vehicleCore = inputs.quads + inputs.scorpions + inputs.scudLaunchers + inputs.liveBuggies;
	const int baselineDesired = vehicleCore >= 6 ? std::max(2, (vehicleCore + 4) / 5) : 0;
	result.desiredBuggies = inputs.mobileSiegeThreatVisible
		? std::max(2, baselineDesired + 1)
		: baselineDesired;

	if (result.desiredBuggies <= currentAndQueued)
	{
		result.reason = inputs.mobileSiegeThreatVisible ? "mobile_siege_counter" : "late_game_mix";
		return result;
	}
	if (!inputs.spendAllowed)
	{
		result.reason = "spend_blocked";
		return result;
	}

	result.productionNeeded = true;
	result.reason = inputs.mobileSiegeThreatVisible ? "mobile_siege_counter" : "late_game_mix";
	return result;
}

AIControlAdapterLocalWorkerLiquidityResult AIControlAdapterChooseLocalWorkerLiquidity(
	const AIControlAdapterLocalWorkerLiquidityInputs& inputs)
{
	AIControlAdapterLocalWorkerLiquidityResult result;
	result.shouldQueue = false;
	result.reason = "target_met";

	if (inputs.workerCap > 0 && inputs.globalWorkers >= inputs.workerCap)
	{
		result.reason = "worker_cap_reached";
		return result;
	}
	if (inputs.cashFloat < 3000u)
	{
		result.reason = "cash_reserved";
		return result;
	}
	if (inputs.desiredLocalWorkers <= 0 || inputs.localIdleWorkers >= inputs.desiredLocalWorkers)
	{
		result.reason = "target_met";
		return result;
	}
	if (!inputs.hasLocalProducer)
	{
		result.reason = "producer_missing";
		return result;
	}

	result.shouldQueue = true;
	result.reason = "queued_local_worker";
	return result;
}

AIControlAdapterBrutalPressureResult AIControlAdapterChooseBrutalPressurePriority(
	const AIControlAdapterBrutalPressureInputs& inputs)
{
	AIControlAdapterBrutalPressureResult result;
	result.chosenPriority = "hold";
	result.reason = inputs.reserveProtected ? "no_pressure" : "reserve_protected";

	if (inputs.emergencyUnitAttack || inputs.mainUnderPressure)
	{
		result.chosenPriority = "emergency_survival";
		result.reason = inputs.emergencyUnitAttack ? "active_unit_attack" : "main_under_pressure";
		return result;
	}
	if (inputs.staleFoundations > 0)
	{
		result.chosenPriority = "foundation_recovery";
		result.reason = "stale_foundation_recovery";
		return result;
	}
	if (inputs.expansionGap >= 5)
	{
		result.chosenPriority = "urgent_expansion";
		result.reason = "distributed_survival_expansion";
		return result;
	}
	if (inputs.localWorkerGap > 0)
	{
		result.chosenPriority = "local_worker_liquidity";
		result.reason = "frontier_worker_gap";
		return result;
	}
	if (inputs.staticDefenseGap > 0 || inputs.garrisonGap > 0)
	{
		result.chosenPriority = "harden_frontier";
		result.reason = "defense_budget_before_attack";
		return result;
	}
	if (inputs.mobileSiegeThreats > 0)
	{
		result.chosenPriority = "mobile_siege_counterbattery";
		result.reason = "long_range_threat";
		return result;
	}
	if (inputs.normalAttackReady)
	{
		result.chosenPriority = "offensive_pressure";
		result.reason = "defense_budget_satisfied";
		return result;
	}

	return result;
}

void AIControlAdapterEnemyMemory::beginUpdate(unsigned int currentTick)
{
	m_updateTick = currentTick;
	m_visibleUnits.clear();
}

void AIControlAdapterEnemyMemory::observe(const EnemyMemoryObservation& observation)
{
	if (observation.objectId == 0 || observation.templateName.empty())
	{
		return;
	}
	if (AIControlAdapterIsExcludedEnemyMemoryTemplate(observation.templateName))
	{
		return;
	}

	const EnemyMemoryKind kind = classifyTemplate(observation.templateName, observation.isStructure);
	if (!shouldTrackIndividualItem(kind, observation.isStructure) &&
		!AIControlAdapterIsBattlefieldArtilleryTemplate(observation.templateName, observation.isStructure))
	{
		if (observation.isUnit)
		{
			UnitObservation unit;
			unit.playerIndex = observation.playerIndex;
			unit.team = observation.team;
			unit.position = observation.position;
			unit.seenTick = observation.seenTick;
			m_visibleUnits.push_back(unit);
		}
		return;
	}

	for (std::size_t i = 0; i < m_items.size(); ++i)
	{
		if (m_items[i].objectId != observation.objectId)
		{
			continue;
		}
		m_items[i].playerIndex = observation.playerIndex;
		m_items[i].team = observation.team;
		m_items[i].kind = kind;
		m_items[i].templateName = observation.templateName;
		m_items[i].visible = true;
		m_items[i].stale = false;
		m_items[i].isStructure = observation.isStructure;
		m_items[i].position = observation.position;
		m_items[i].lastSeenTick = observation.seenTick;
		return;
	}

	EnemyMemoryItem item;
	item.objectId = observation.objectId;
	item.playerIndex = observation.playerIndex;
	item.team = observation.team;
	item.kind = kind;
	item.templateName = observation.templateName;
	item.visible = true;
	item.stale = false;
	item.isStructure = observation.isStructure;
	item.position = observation.position;
	item.firstSeenTick = observation.seenTick;
	item.lastSeenTick = observation.seenTick;
	m_items.push_back(item);
}

void AIControlAdapterEnemyMemory::finishUpdate(unsigned int currentTick)
{
	const unsigned int staleTimeoutMs = 300000u;
	for (std::size_t i = 0; i < m_items.size(); )
	{
		EnemyMemoryItem& item = m_items[i];
		if (AIControlAdapterIsExcludedEnemyMemoryTemplate(item.templateName))
		{
			m_items.erase(m_items.begin() + i);
			continue;
		}
		if (item.lastSeenTick != currentTick)
		{
			item.visible = false;
			item.stale = true;
		}

		if (currentTick - item.lastSeenTick > staleTimeoutMs)
		{
			m_items.erase(m_items.begin() + i);
			continue;
		}
		++i;
	}

	rebuildClusters(currentTick);
}

void AIControlAdapterEnemyMemory::clear()
{
	m_items.clear();
	m_visibleUnits.clear();
	m_clusters.clear();
	m_updateTick = 0;
}

nlohmann::json AIControlAdapterEnemyMemory::buildTelemetry(unsigned int currentTick) const
{
	nlohmann::json items = nlohmann::json::array();
	for (std::size_t i = 0; i < m_items.size(); ++i)
	{
		const EnemyMemoryItem& item = m_items[i];
		const unsigned int age = currentTick - item.lastSeenTick;
		items.push_back(nlohmann::json::object({
			{"id", item.objectId},
			{"player_index", item.playerIndex},
			{"team", item.team},
			{"kind", kindToString(item.kind)},
			{"template", item.templateName},
			{"visible", item.visible},
			{"stale", item.stale},
			{"last_seen_tick", item.lastSeenTick},
			{"age_ms", age},
			{"position", nlohmann::json::object({
				{"x", item.position.x},
				{"y", item.position.y},
				{"z", item.position.z}
			})},
			{"threat", threatForKind(item.kind)}
		}));
	}

	nlohmann::json clusters = nlohmann::json::array();
	for (std::size_t i = 0; i < m_clusters.size(); ++i)
	{
		const EnemyMemoryCluster& cluster = m_clusters[i];
		clusters.push_back(nlohmann::json::object({
			{"player_index", cluster.playerIndex},
			{"team", cluster.team},
			{"kind", kindToString(cluster.kind)},
			{"visible_count", cluster.visibleCount},
			{"last_seen_tick", cluster.lastSeenTick},
			{"age_ms", currentTick - cluster.lastSeenTick},
			{"position", nlohmann::json::object({
				{"x", cluster.position.x},
				{"y", cluster.position.y},
				{"z", cluster.position.z}
			})}
		}));
	}

	return nlohmann::json::object({
		{"items", items},
		{"clusters", clusters}
	});
}

EnemyMemoryKind AIControlAdapterEnemyMemory::classifyTemplate(const std::string& templateName, bool isStructure)
{
	if (AIControlAdapterWMDTargetTracker::isWMDTemplate(templateName))
	{
		return EnemyMemoryKind::Wmd;
	}

	const std::string lower = AIControlAdapterLowerCopy(templateName);
	if (isStructure)
	{
		if (AIControlAdapterContainsToken(lower, "commandcenter") ||
			AIControlAdapterContainsToken(lower, "command_center") ||
			AIControlAdapterContainsToken(lower, "command"))
		{
			return EnemyMemoryKind::BaseCommand;
		}
		if (AIControlAdapterContainsToken(lower, "barracks") ||
			AIControlAdapterContainsToken(lower, "armsdealer") ||
			AIControlAdapterContainsToken(lower, "warf") ||
			AIControlAdapterContainsToken(lower, "warfactory") ||
			AIControlAdapterContainsToken(lower, "airfield") ||
			AIControlAdapterContainsToken(lower, "strategycenter") ||
			AIControlAdapterContainsToken(lower, "propagandacenter") ||
			AIControlAdapterContainsToken(lower, "palace"))
		{
			return EnemyMemoryKind::Production;
		}
		if (AIControlAdapterContainsToken(lower, "supply") ||
			AIControlAdapterContainsToken(lower, "stash") ||
			AIControlAdapterContainsToken(lower, "blackmarket") ||
			AIControlAdapterContainsToken(lower, "market") ||
			AIControlAdapterContainsToken(lower, "dropzone") ||
			AIControlAdapterContainsToken(lower, "hack"))
		{
			return EnemyMemoryKind::Economy;
		}
		if (AIControlAdapterContainsToken(lower, "stinger") ||
			AIControlAdapterContainsToken(lower, "tunnel") ||
			AIControlAdapterContainsToken(lower, "patriot") ||
			AIControlAdapterContainsToken(lower, "firebase") ||
			AIControlAdapterContainsToken(lower, "bunker") ||
			AIControlAdapterContainsToken(lower, "gatling") ||
			AIControlAdapterContainsToken(lower, "defense") ||
			AIControlAdapterContainsToken(lower, "tower"))
		{
			return EnemyMemoryKind::Defense;
		}
	}

	return isStructure ? EnemyMemoryKind::Unknown : EnemyMemoryKind::Army;
}

const char* AIControlAdapterEnemyMemory::kindToString(EnemyMemoryKind kind)
{
	switch (kind)
	{
		case EnemyMemoryKind::BaseCommand: return "base/command";
		case EnemyMemoryKind::Production: return "production";
		case EnemyMemoryKind::Economy: return "economy";
		case EnemyMemoryKind::Defense: return "defense";
		case EnemyMemoryKind::Wmd: return "wmd";
		case EnemyMemoryKind::Army: return "army";
		default: return "unknown/other";
	}
}

const char* AIControlAdapterEnemyMemory::threatForKind(EnemyMemoryKind kind)
{
	switch (kind)
	{
		case EnemyMemoryKind::Wmd: return "critical";
		case EnemyMemoryKind::Defense:
		case EnemyMemoryKind::Production:
		case EnemyMemoryKind::BaseCommand: return "high";
		case EnemyMemoryKind::Economy: return "medium";
		default: return "low";
	}
}

bool AIControlAdapterEnemyMemory::shouldTrackIndividualItem(EnemyMemoryKind kind, bool isStructure)
{
	return isStructure || kind == EnemyMemoryKind::Wmd;
}

void AIControlAdapterEnemyMemory::rebuildClusters(unsigned int currentTick)
{
	m_clusters.clear();
	const float clusterSize = 700.0f;

	struct Accum
	{
		int playerIndex = -1;
		int team = -1;
		int count = 0;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		unsigned int lastSeenTick = 0;
	};

	std::map<std::string, Accum> accumByCell;
	for (std::size_t i = 0; i < m_visibleUnits.size(); ++i)
	{
		const UnitObservation& unit = m_visibleUnits[i];
		const int cellX = static_cast<int>(std::floor(unit.position.x / clusterSize));
		const int cellY = static_cast<int>(std::floor(unit.position.y / clusterSize));
		const std::string key = std::to_string(unit.playerIndex) + ":" + std::to_string(unit.team) + ":" +
			std::to_string(cellX) + ":" + std::to_string(cellY);

		Accum& accum = accumByCell[key];
		accum.playerIndex = unit.playerIndex;
		accum.team = unit.team;
		++accum.count;
		accum.x += unit.position.x;
		accum.y += unit.position.y;
		accum.z += unit.position.z;
		accum.lastSeenTick = std::max(accum.lastSeenTick, unit.seenTick);
	}

	for (std::map<std::string, Accum>::const_iterator it = accumByCell.begin(); it != accumByCell.end(); ++it)
	{
		const Accum& accum = it->second;
		if (accum.count <= 0)
		{
			continue;
		}

		EnemyMemoryCluster cluster;
		cluster.playerIndex = accum.playerIndex;
		cluster.team = accum.team;
		cluster.kind = EnemyMemoryKind::Army;
		cluster.visibleCount = accum.count;
		cluster.position.x = accum.x / accum.count;
		cluster.position.y = accum.y / accum.count;
		cluster.position.z = accum.z / accum.count;
		cluster.lastSeenTick = accum.lastSeenTick != 0 ? accum.lastSeenTick : currentTick;
		m_clusters.push_back(cluster);
	}
}

AIControlAdapterProductionChoiceResult AIControlAdapterChoosePreferredProductionCommand(
	const AIControlAdapterProductionChoiceInputs& inputs)
{
	const char* profile = inputs.profile != nullptr ? inputs.profile : "";
	const bool aggressive = std::strcmp(profile, "aggressive") == 0;
	const bool techy = std::strcmp(profile, "tech") == 0 || std::strcmp(profile, "sprawl") == 0 || inputs.isBalancedSprawl;
	const int infantry = inputs.soldiers + inputs.rpg;
	const int frontlineVehicles = inputs.quads + inputs.scorpions + inputs.scudLaunchers;
	const bool preferVehicleReplenishment = AIControlAdapterShouldPreferVehicleReplenishment({
		inputs.isBalancedSprawl,
		inputs.armsDealers,
		inputs.barracks,
		inputs.quads,
		inputs.scorpions,
		inputs.scudLaunchers,
		0,
		inputs.soldiers,
		inputs.rpg
	});

	// Phase 6.3: Capture source capacity check
	// Only force production when real capture demand exists:
	// - Capture upgrade complete
	// - Desired capacity > 0 (automation enabled, targets remaining)
	// - Available < desired
	const bool needsCaptureReserve = inputs.hasCaptureUpgrade
		&& inputs.desiredCaptureSources > 0
		&& inputs.captureSourcesAvailable < inputs.desiredCaptureSources;

	if (inputs.shouldPauseForEconomy)
	{
		return { nullptr, inputs.pauseReason != nullptr ? inputs.pauseReason : "reserve_cash_recovery" };
	}

	// Phase 6.3: Army cap override for bounded capture utility reserve
	// Allow capture source production even at army cap, but only up to desired reserve
	if (inputs.shouldHoldArmyCap)
	{
		if (needsCaptureReserve && inputs.barracks > 0 && inputs.money >= 300u)
		{
			// Allow bounded override: only produce if we haven't exceeded desired reserve
			// This prevents infinite infantry production while still ensuring capture capacity
			const int captureUtilityOverhead = inputs.desiredCaptureSources;
			if (inputs.armyCount < (inputs.armyCap + captureUtilityOverhead))
			{
				return { "Game.QueueSoldiersAllBarracks", "capture_utility_reserve" };
			}
		}
		return { nullptr, "army_cap_reached" };
	}

	if (inputs.isBalancedSprawl && inputs.armyCap > 0)
	{
		const int supportReserve = inputs.armsDealers <= 1 ? 2 : 4;
		const int targetCombatCount = inputs.armyCap > supportReserve ? (inputs.armyCap - supportReserve) : inputs.armyCap;
		const int desiredVehicleCount = (targetCombatCount * 11) / 20;
		const int desiredInfantryCount = targetCombatCount - desiredVehicleCount;
		const int vehicleDeficit = desiredVehicleCount - frontlineVehicles;
		const int infantryDeficit = desiredInfantryCount - infantry;
		const bool canQueueVehicles = inputs.armsDealers > 0 && inputs.money >= 700u;
		const bool canQueueInfantry = inputs.barracks > 0 && inputs.money >= 300u;
		const bool nearArmyCap = inputs.armyCount >= (inputs.armyCap - 5);
		const bool compositionCloseEnough = std::abs(vehicleDeficit) <= 8 && std::abs(infantryDeficit) <= 8;

		if (inputs.armyCount >= inputs.armyCap)
		{
			return { nullptr, "army_cap_reached" };
		}

		if (nearArmyCap && compositionCloseEnough)
		{
			return { nullptr, "army_cap_buffer" };
		}

		// Phase 6.3: Check capture reserve need before composition priorities
		// Capture reserve overrides normal composition when below desired capacity
		if (needsCaptureReserve && canQueueInfantry)
		{
			return { "Game.QueueSoldiersAllBarracks", "" };
		}

		if (canQueueVehicles
			&& (preferVehicleReplenishment
				|| !canQueueInfantry
				|| vehicleDeficit > infantryDeficit))
		{
			if (techy
				&& inputs.hasCompletedPalace
				&& inputs.hasScudLauncherScience
				&& inputs.scudLaunchers < ((inputs.quads + inputs.scorpions) / 10 > 1 ? (inputs.quads + inputs.scorpions) / 10 : 1)
				&& inputs.money >= 1200u)
			{
				return { "Game.QueueScudLauncher", "" };
			}
			if (aggressive || inputs.quads <= inputs.scorpions)
			{
				return { "Game.QueueQuadsAllWarFactories", "" };
			}
			return { "Game.QueueScorpionsAllWarFactories", "" };
		}

		if (canQueueInfantry
			&& (!canQueueVehicles
				|| infantryDeficit > 0
				|| inputs.armyCount < (inputs.armyCap / 2)))
		{
			if (aggressive || inputs.rpg < inputs.soldiers)
			{
				return { "Game.QueueRpgTroopersAllBarracks", "" };
			}
			return { "Game.QueueSoldiersAllBarracks", "" };
		}

		if (canQueueVehicles)
		{
			if (techy
				&& inputs.hasCompletedPalace
				&& inputs.hasScudLauncherScience
				&& inputs.scudLaunchers < ((inputs.quads + inputs.scorpions) / 10 > 1 ? (inputs.quads + inputs.scorpions) / 10 : 1)
				&& inputs.money >= 1200u)
			{
				return { "Game.QueueScudLauncher", "" };
			}
			if (aggressive || inputs.quads <= inputs.scorpions)
			{
				return { "Game.QueueQuadsAllWarFactories", "" };
			}
			return { "Game.QueueScorpionsAllWarFactories", "" };
		}

		if (canQueueInfantry)
		{
			// Phase 6.3: In fallback infantry, capture reserve takes priority
			if (needsCaptureReserve)
			{
				return { "Game.QueueSoldiersAllBarracks", "" };
			}
			if (aggressive || inputs.rpg < inputs.soldiers)
			{
				return { "Game.QueueRpgTroopersAllBarracks", "" };
			}
			return { "Game.QueueSoldiersAllBarracks", "" };
		}

		return { nullptr, "" };
	}

	if (inputs.isBalancedSprawl
		&& inputs.armsDealers > 0
		&& inputs.money >= 700u
		&& preferVehicleReplenishment)
	{
		if (techy
			&& inputs.hasCompletedPalace
			&& inputs.hasScudLauncherScience
			&& inputs.scudLaunchers < ((inputs.quads + inputs.scorpions) / 10 > 1 ? (inputs.quads + inputs.scorpions) / 10 : 1)
			&& inputs.money >= 1200u)
		{
			return { "Game.QueueScudLauncher", "" };
		}
		if (aggressive || inputs.quads <= inputs.scorpions)
		{
			return { "Game.QueueQuadsAllWarFactories", "" };
		}
		return { "Game.QueueScorpionsAllWarFactories", "" };
	}

	if (inputs.barracks > 0 && inputs.money >= 300u)
	{
		// Phase 6.3: Non-balanced sprawl infantry fallback with capture priority
		if (needsCaptureReserve)
		{
			return { "Game.QueueSoldiersAllBarracks", "" };
		}
		if (aggressive || inputs.rpg < inputs.soldiers)
		{
			return { "Game.QueueRpgTroopersAllBarracks", "" };
		}
		return { "Game.QueueSoldiersAllBarracks", "" };
	}

	if (inputs.armsDealers > 0 && inputs.money >= 700u)
	{
		if (techy
			&& inputs.hasCompletedPalace
			&& inputs.hasScudLauncherScience
			&& inputs.scudLaunchers < ((inputs.quads + inputs.scorpions) / 10 > 1 ? (inputs.quads + inputs.scorpions) / 10 : 1)
			&& inputs.money >= 1200u)
		{
			return { "Game.QueueScudLauncher", "" };
		}
		if (aggressive || inputs.quads <= inputs.scorpions)
		{
			return { "Game.QueueQuadsAllWarFactories", "" };
		}
		return { "Game.QueueScorpionsAllWarFactories", "" };
	}

	return { nullptr, "" };
}

bool AIControlAdapterHasTickElapsed(unsigned int deadline, unsigned int now)
{
	if (deadline == 0u)
	{
		return true;
	}

	return static_cast<LONG>(now - deadline) >= 0;
}

bool AIControlAdapterIsTickInFuture(unsigned int deadline, unsigned int now)
{
	if (deadline == 0u)
	{
		return false;
	}

	return static_cast<LONG>(now - deadline) < 0;
}

bool AIControlAdapterTryNormalizeDirection(float dx, float dy, float& outDx, float& outDy)
{
	const float lenSq = (dx * dx) + (dy * dy);
	if (lenSq <= 1.0f)
	{
		return false;
	}

	const float invLen = 1.0f / std::sqrt(lenSq);
	outDx = dx * invLen;
	outDy = dy * invLen;
	return true;
}

bool AIControlAdapterTryReadMapPosition(const nlohmann::json& mapPos, AIControlAdapterMapPoint& outPos)
{
	if (!mapPos.is_object())
	{
		return false;
	}

	const auto xIt = mapPos.find("x");
	const auto yIt = mapPos.find("y");
	if (xIt == mapPos.end() || yIt == mapPos.end() || !xIt->is_number() || !yIt->is_number())
	{
		return false;
	}

	outPos.x = xIt->get<float>();
	outPos.y = yIt->get<float>();
	return true;
}

bool AIControlAdapterTryGetRecentAttackTarget(
	const nlohmann::json& events,
	unsigned int nowTick,
	unsigned int freshnessMs,
	AIControlAdapterMapPoint& outPos)
{
	if (!events.is_array())
	{
		return false;
	}

	for (auto it = events.rbegin(); it != events.rend(); ++it)
	{
		if (!it->is_object())
		{
			continue;
		}

		if (it->value("kind", "") != "attack")
		{
			continue;
		}

		const auto xIt = it->find("x");
		const auto yIt = it->find("y");
		if (xIt == it->end() || yIt == it->end() || !xIt->is_number() || !yIt->is_number())
		{
			continue;
		}

		const unsigned int eventTick = it->value("tick", 0u);
		if (eventTick != 0u && (nowTick - eventTick) > freshnessMs)
		{
			continue;
		}

		outPos.x = xIt->get<float>();
		outPos.y = yIt->get<float>();
		return true;
	}

	return false;
}

AIControlAdapterZoneFrontDirectionResult AIControlAdapterResolveZoneFrontDirection(
	const AIControlAdapterMapPoint& zoneCenter,
	bool hasRecentAttackTarget,
	const AIControlAdapterMapPoint& recentAttackTarget,
	bool hasPreferredEnemyBase,
	const AIControlAdapterMapPoint& preferredEnemyBase,
	const std::vector<AIControlAdapterMapPoint>& knownEnemyBases,
	float fallbackDx,
	float fallbackDy)
{
	AIControlAdapterZoneFrontDirectionResult result = {
		fallbackDx,
		fallbackDy,
		"sprawl_axis"
	};

	if (hasRecentAttackTarget
		&& AIControlAdapterTryNormalizeDirection(
			recentAttackTarget.x - zoneCenter.x,
			recentAttackTarget.y - zoneCenter.y,
			result.dx,
			result.dy))
	{
		result.source = "attack_target";
		return result;
	}

	if (hasPreferredEnemyBase
		&& AIControlAdapterTryNormalizeDirection(
			preferredEnemyBase.x - zoneCenter.x,
			preferredEnemyBase.y - zoneCenter.y,
			result.dx,
			result.dy))
	{
		result.source = "enemy_base";
		return result;
	}

	bool hasNearestEnemyBase = false;
	AIControlAdapterMapPoint nearestEnemyBase = {};
	float nearestEnemyBaseDistSq = 0.0f;
	for (const AIControlAdapterMapPoint& candidatePos : knownEnemyBases)
	{
		const float candidateDx = candidatePos.x - zoneCenter.x;
		const float candidateDy = candidatePos.y - zoneCenter.y;
		const float candidateDistSq = (candidateDx * candidateDx) + (candidateDy * candidateDy);
		if (!hasNearestEnemyBase || candidateDistSq < nearestEnemyBaseDistSq)
		{
			hasNearestEnemyBase = true;
			nearestEnemyBase = candidatePos;
			nearestEnemyBaseDistSq = candidateDistSq;
		}
	}

	if (hasNearestEnemyBase
		&& AIControlAdapterTryNormalizeDirection(
			nearestEnemyBase.x - zoneCenter.x,
			nearestEnemyBase.y - zoneCenter.y,
			result.dx,
			result.dy))
	{
		result.source = "enemy_base";
		return result;
	}

	return result;
}

AIControlAdapterZoneFrontRearPoints AIControlAdapterBuildZoneFrontRearPoints(
	const AIControlAdapterMapPoint& center,
	float radius,
	float dirDx,
	float dirDy)
{
	return {
		{ center.x + (dirDx * radius), center.y + (dirDy * radius) },
		{ center.x - (dirDx * radius), center.y - (dirDy * radius) }
	};
}
