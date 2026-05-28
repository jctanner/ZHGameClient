#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

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
