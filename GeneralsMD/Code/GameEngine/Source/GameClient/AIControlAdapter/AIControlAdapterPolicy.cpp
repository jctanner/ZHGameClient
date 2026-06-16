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
#include <limits>
#include <set>

namespace
{
	float AIControlAdapterClampUnitFloat(float value)
	{
		return std::max(0.0f, std::min(1.0f, value));
	}

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

	bool AIControlAdapterReadTerrainPoint(const nlohmann::json& value, AIControlAdapterTerrainPoint& outPoint)
	{
		if (!value.is_object() || !value.contains("x") || !value.contains("y"))
		{
			return false;
		}
		outPoint.x = value.value("x", 0.0f);
		outPoint.y = value.value("y", 0.0f);
		return true;
	}

	bool AIControlAdapterIsTrustedMapFileCacheFeature(const nlohmann::json& featureJson)
	{
		const std::string kind = featureJson.value("kind", "");
		const std::string id = featureJson.value("id", "");
		if (id == "death-valley-main-base-lower-entry")
		{
			return false;
		}
		if (kind == "waypoint"
			|| kind == "lane"
			|| kind == "impassable_barrier"
			|| kind == "blocked_area")
		{
			return true;
		}
		if (kind == "base_entrance" || kind == "chokepoint")
		{
			const std::string source = featureJson.value("source", "");
			return featureJson.value("trusted", false)
				&& (source == "manual_annotation" || source == "map_file_annotation");
		}
		return false;
	}

	float AIControlAdapterDistancePointToSegmentSq(
		float px,
		float py,
		const AIControlAdapterTerrainPoint& a,
		const AIControlAdapterTerrainPoint& b)
	{
		const float vx = b.x - a.x;
		const float vy = b.y - a.y;
		const float wx = px - a.x;
		const float wy = py - a.y;
		const float lenSq = (vx * vx) + (vy * vy);
		float t = 0.0f;
		if (lenSq > 0.0001f)
		{
			t = ((wx * vx) + (wy * vy)) / lenSq;
			t = std::max(0.0f, std::min(1.0f, t));
		}
		const float cx = a.x + (t * vx);
		const float cy = a.y + (t * vy);
		const float dx = px - cx;
		const float dy = py - cy;
		return (dx * dx) + (dy * dy);
	}

	float AIControlAdapterDistancePointToFeatureSq(
		float px,
		float py,
		const AIControlAdapterTerrainFeature& feature)
	{
		if (feature.hasPosition)
		{
			const float dx = px - feature.position.x;
			const float dy = py - feature.position.y;
			return (dx * dx) + (dy * dy);
		}
		if (feature.points.empty())
		{
			return std::numeric_limits<float>::max();
		}
		if (feature.points.size() == 1u)
		{
			const float dx = px - feature.points[0].x;
			const float dy = py - feature.points[0].y;
			return (dx * dx) + (dy * dy);
		}
		float best = std::numeric_limits<float>::max();
		for (std::size_t i = 1; i < feature.points.size(); ++i)
		{
			best = std::min(best, AIControlAdapterDistancePointToSegmentSq(px, py, feature.points[i - 1], feature.points[i]));
		}
		return best;
	}

	float AIControlAdapterCross2D(
		const AIControlAdapterTerrainPoint& a,
		const AIControlAdapterTerrainPoint& b,
		const AIControlAdapterTerrainPoint& c)
	{
		return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
	}

	bool AIControlAdapterSegmentsIntersect(
		const AIControlAdapterTerrainPoint& a,
		const AIControlAdapterTerrainPoint& b,
		const AIControlAdapterTerrainPoint& c,
		const AIControlAdapterTerrainPoint& d)
	{
		const float c1 = AIControlAdapterCross2D(a, b, c);
		const float c2 = AIControlAdapterCross2D(a, b, d);
		const float c3 = AIControlAdapterCross2D(c, d, a);
		const float c4 = AIControlAdapterCross2D(c, d, b);
		return ((c1 > 0.0f && c2 < 0.0f) || (c1 < 0.0f && c2 > 0.0f))
			&& ((c3 > 0.0f && c4 < 0.0f) || (c3 < 0.0f && c4 > 0.0f));
	}

	bool AIControlAdapterSegmentCrossesBarrier(
		const AIControlAdapterTerrainFacts& facts,
		const AIControlAdapterTerrainPoint& from,
		const AIControlAdapterTerrainPoint& to)
	{
		for (std::size_t featureIdx = 0; featureIdx < facts.features.size(); ++featureIdx)
		{
			const AIControlAdapterTerrainFeature& feature = facts.features[featureIdx];
			if (feature.kind != "impassable_barrier" && feature.kind != "blocked_area")
			{
				continue;
			}
			if (feature.points.size() < 2u)
			{
				continue;
			}
			for (std::size_t pointIdx = 1; pointIdx < feature.points.size(); ++pointIdx)
			{
				if (AIControlAdapterSegmentsIntersect(from, to, feature.points[pointIdx - 1], feature.points[pointIdx]))
				{
					return true;
				}
			}
		}
		return false;
	}
}

AIControlAdapterProfilePolicyConfig AIControlAdapterResolveProfilePolicyConfig(
	const std::string& profile,
	float economyBias,
	float aggressionBias,
	float defenseBias,
	float /*expansionBias*/,
	float sprawlMultiplier)
{
	const std::string normalizedProfile = AIControlAdapterLowerCopy(profile);
	const float econ = AIControlAdapterClampUnitFloat(economyBias);
	const float aggro = AIControlAdapterClampUnitFloat(aggressionBias);
	const float defense = AIControlAdapterClampUnitFloat(defenseBias);
	const float multiplier = std::max(0.5f, std::min(10.0f, sprawlMultiplier));

	AIControlAdapterProfilePolicyConfig config;
	config.profile = normalizedProfile;
	config.isBalancedSprawl = normalizedProfile == "sprawl_balanced";
	config.isSprawlStyle = normalizedProfile == "sprawl" || config.isBalancedSprawl;
	config.reserveCash = config.isBalancedSprawl ? 10000u : (normalizedProfile == "sprawl" ? 5000u : 0u);

	config.workerMinIdle = (econ >= 0.70f || normalizedProfile == "economic" || config.isSprawlStyle) ? 2 : 1;
	config.workerQueueCount = (econ >= 0.75f || normalizedProfile == "economic" || config.isSprawlStyle) ? 2 : 1;
	config.workerCooldownMs = (normalizedProfile == "aggressive") ? 1500u : 2500u;
	if (normalizedProfile == "sprawl")
	{
		config.workerMinIdle = 3;
		config.workerQueueCount = 1;
		config.workerCooldownMs = 2000u;
	}
	else if (config.isBalancedSprawl)
	{
		config.workerMinIdle = 2;
		config.workerQueueCount = 1;
		config.workerCooldownMs = 2500u;
	}

	config.stashWorkersPerStash = 8;
	if (normalizedProfile == "economic" || config.isSprawlStyle || econ >= 0.70f)
	{
		config.stashWorkersPerStash = 10;
	}
	else if (normalizedProfile == "aggressive")
	{
		config.stashWorkersPerStash = 7;
	}
	config.stashWorkerCooldownMs = 5000u;
	if (normalizedProfile == "sprawl")
	{
		config.stashWorkersPerStash = 3;
		config.stashWorkerCooldownMs = 12000u;
	}
	else if (config.isBalancedSprawl)
	{
		config.stashWorkersPerStash = 6;
		config.stashWorkerCooldownMs = 8000u;
	}

	config.attackMinUnits = 38;
	config.attackGroupSize = 28;
	config.attackCooldownMs = 18000u;
	if (normalizedProfile == "aggressive" || aggro >= 0.70f)
	{
		config.attackMinUnits = 24;
		config.attackGroupSize = 20;
		config.attackCooldownMs = 12000u;
	}
	else if (normalizedProfile == "economic")
	{
		config.attackMinUnits = 50;
		config.attackGroupSize = 34;
		config.attackCooldownMs = 22000u;
	}
	else if (normalizedProfile == "defensive" || defense >= 0.70f)
	{
		config.attackMinUnits = 60;
		config.attackGroupSize = 40;
		config.attackCooldownMs = 26000u;
	}
	else if (normalizedProfile == "tech")
	{
		config.attackMinUnits = 44;
		config.attackGroupSize = 30;
		config.attackCooldownMs = 20000u;
	}
	else if (normalizedProfile == "sprawl")
	{
		config.attackMinUnits = 70;
		config.attackGroupSize = 45;
		config.attackCooldownMs = 26000u;
	}
	else if (config.isBalancedSprawl)
	{
		config.attackMinUnits = 55;
		config.attackGroupSize = 28;
		config.attackCooldownMs = 22000u;
	}

	config.sprawlSupplyCap = std::max(1, static_cast<int>(std::floor((config.isBalancedSprawl ? 3.0f : 4.0f) * multiplier)));
	config.sprawlBarracksCap = std::max(1, static_cast<int>(std::floor((config.isBalancedSprawl ? 1.5f : 2.0f) * multiplier)));
	config.sprawlArmsCap = std::max(1, static_cast<int>(std::floor((config.isBalancedSprawl ? 2.0f : 3.0f) * multiplier)));
	config.sprawlMarketCap = std::max(1, static_cast<int>(std::floor((config.isBalancedSprawl ? 6.0f : 8.0f) * multiplier)));
	config.sprawlTunnelCap = std::max(1, static_cast<int>(std::floor((config.isBalancedSprawl ? 5.0f : 8.0f) * multiplier)));
	config.sprawlStingerCap = std::max(1, static_cast<int>(std::floor((config.isBalancedSprawl ? 4.0f : 6.0f) * multiplier)));

	config.urgentZoneGapThreshold = 5;
	config.normalMaxConcurrentExpansionStashes = 1;
	if (config.isSprawlStyle && multiplier >= 5.0f)
	{
		config.normalMaxConcurrentExpansionStashes = config.isBalancedSprawl ? 2 : 3;
	}
	if (config.isSprawlStyle && multiplier >= 8.0f)
	{
		config.normalMaxConcurrentExpansionStashes = config.isBalancedSprawl ? 3 : 4;
	}
	config.allowExpansionBeforeFullRemoteFollowup = config.isSprawlStyle && multiplier >= 8.0f;
	config.expansionHighCashFloatThreshold = 10000u;
	config.scudStormHighCashFloatThreshold = 25000u;
	return config;
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
	int cashMarketFloor = 0;
	if (inputs.money >= 100000u && inputs.blackMarkets >= 8)
	{
		cashMarketFloor = 150;
	}
	if ((inputs.money >= 250000u || inputs.blackMarkets >= 20) && inputs.blackMarkets >= 8)
	{
		cashMarketFloor = std::max(cashMarketFloor, 220);
	}
	if (inputs.money >= 250000u && inputs.blackMarkets >= 20)
	{
		cashMarketFloor = std::max(cashMarketFloor, 260);
	}
	if (inputs.money >= 300000u && inputs.blackMarkets >= 20)
	{
		cashMarketFloor = std::max(cashMarketFloor, 300);
	}
	const int surplusCap = std::min(productionCapacityCap, std::max(incomeSupportedCap, cashMarketFloor));
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
	const int maxConcurrentSupplyStashes = std::max(1, inputs.maxConcurrentSupplyStashes);
	if (inputs.supplyStashesInProgress >= maxConcurrentSupplyStashes)
	{
		result.reason = maxConcurrentSupplyStashes > 1 ? "in_progress_cap" : "build_in_progress";
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

AIControlAdapterRemoteZoneFollowupResult AIControlAdapterChooseRemoteZoneFollowup(
	const AIControlAdapterRemoteZoneFollowupInputs& inputs)
{
	AIControlAdapterRemoteZoneFollowupResult result;
	if (!inputs.remoteZoneHasStash)
	{
		result.needsFollowup = false;
		result.packageStage = "none";
		result.reason = "no_remote_stash";
		return result;
	}

	if (inputs.tunnels < 1)
	{
		result.needsFollowup = true;
		result.packageStage = "tunnel";
		result.reason = "needs_tunnel";
		return result;
	}

	if (inputs.stingers < 1)
	{
		result.needsFollowup = true;
		result.packageStage = "stinger";
		result.reason = "needs_stinger";
		return result;
	}

	if (inputs.allowExpansionBeforeFullRemoteFollowup)
	{
		result.needsFollowup = false;
		result.packageStage = "seeded";
		result.reason = "seeded_defense_sufficient";
		return result;
	}

	if (inputs.barracks < 1)
	{
		result.needsFollowup = true;
		result.packageStage = "barracks";
		result.reason = "needs_barracks";
		return result;
	}

	if (inputs.armsDealers < 1)
	{
		result.needsFollowup = true;
		result.packageStage = "arms_dealer";
		result.reason = "needs_arms_dealer";
		return result;
	}

	result.needsFollowup = false;
	result.packageStage = "complete";
	result.reason = "full_followup_complete";
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

const char* AIControlAdapterStrategicSpendCategoryName(StrategicSpendCategory category)
{
	switch (category)
	{
	case StrategicSpendCategory::EconomyRecovery: return "economy_recovery";
	case StrategicSpendCategory::EconomyGrowth: return "economy_growth";
	case StrategicSpendCategory::Expansion: return "expansion";
	case StrategicSpendCategory::LocalWorkerRecovery: return "local_worker_recovery";
	case StrategicSpendCategory::TechPrerequisite: return "tech_prerequisite";
	case StrategicSpendCategory::StaticDefense: return "static_defense";
	case StrategicSpendCategory::DefensiveWmd: return "defensive_wmd";
	case StrategicSpendCategory::EmergencyDefenseUnits: return "emergency_defense_units";
	case StrategicSpendCategory::CounterbatteryUnits: return "counterbattery_units";
	case StrategicSpendCategory::LuxuryBaseline: return "luxury_baseline";
	default: return "unknown";
	}
}

AIControlAdapterStrategicSpendDecision AIControlAdapterEvaluateStrategicSpend(
	StrategicSpendCategory category,
	const AIControlAdapterStrategicSpendInput& inputs)
{
	AIControlAdapterStrategicSpendDecision result;
	const unsigned int recoveryStepCost = inputs.expansionUrgent ? 1800u : 2500u;
	const bool recoveryPathNeeded =
		inputs.incomeCritical
		|| inputs.reserveDepleted
		|| inputs.completedMarkets <= 0
		|| inputs.staleMarketFoundations > 0
		|| inputs.expansionUrgent;
	result.protectedCash = inputs.reserveCash;
	if (recoveryPathNeeded)
	{
		result.protectedCash = std::max(result.protectedCash, recoveryStepCost);
	}
	if (inputs.mainBaseCritical)
	{
		result.protectedCash = std::min(result.protectedCash, inputs.reserveCash / 2u);
	}
	result.spendBudget = inputs.money > result.protectedCash ? (inputs.money - result.protectedCash) : 0u;
	result.batchLimit = 1;
	result.reason = "reserve_protected";

	const bool affordable = inputs.money >= inputs.requestCost;
	const bool protectedAffordable = result.spendBudget >= inputs.requestCost;
	const bool immediateCollapse = inputs.mainBaseCritical && inputs.activeLocalEnemies > 0;
	const bool healthyRecoveryInProgress = inputs.healthyMarketsInProgress > 0 && inputs.staleMarketFoundations <= 0;

	switch (category)
	{
	case StrategicSpendCategory::EconomyRecovery:
		if (!affordable)
		{
			result.reason = "cash_below_recovery_cost";
			return result;
		}
		if (healthyRecoveryInProgress)
		{
			result.reason = "healthy_income_build_in_progress";
			return result;
		}
		result.allowed = true;
		result.reason = inputs.staleMarketFoundations > 0 ? "replace_stale_income_foundation" : "economy_recovery_priority";
		return result;
	case StrategicSpendCategory::Expansion:
		if (!affordable)
		{
			result.reason = "cash_below_expansion_cost";
			return result;
		}
		if (inputs.expansionUrgent || !recoveryPathNeeded || protectedAffordable)
		{
			result.allowed = true;
			result.reason = inputs.expansionUrgent ? "urgent_expansion_priority" : "expansion_allowed";
			return result;
		}
		result.reason = "recovery_cash_protected";
		return result;
	case StrategicSpendCategory::LocalWorkerRecovery:
		if (affordable && (protectedAffordable || inputs.staleStrategicFoundations > 0))
		{
			result.allowed = true;
			result.reason = "worker_recovery";
		}
		else
		{
			result.reason = affordable ? "recovery_cash_protected" : "cash_below_worker_cost";
		}
		return result;
	case StrategicSpendCategory::EmergencyDefenseUnits:
		if (!inputs.emergencySurvivalActive && inputs.activeLocalEnemies <= 0 && !inputs.mainBaseCritical)
		{
			result.reason = "no_emergency";
			result.batchLimit = 0;
			return result;
		}
		if (inputs.quads >= std::max(12, inputs.scorpions + inputs.buggies + 8) && !immediateCollapse)
		{
			result.reason = "quad_saturation_recovery_protected";
			result.batchLimit = 0;
			return result;
		}
		if (immediateCollapse)
		{
			result.allowed = affordable;
			result.batchLimit = 3;
			result.reason = affordable ? "main_base_critical_override" : "cash_below_unit_cost";
			return result;
		}
		if (inputs.reserveDepleted || inputs.incomeCritical || inputs.money < inputs.reserveCash)
		{
			result.allowed = affordable && inputs.money >= inputs.requestCost + 300u;
			result.batchLimit = result.allowed ? 1 : 0;
			result.reason = result.allowed ? "bounded_emergency_pulse" : "recovery_cash_protected";
			return result;
		}
		result.allowed = affordable;
		result.batchLimit = 2;
		result.reason = affordable ? "emergency_defense" : "cash_below_unit_cost";
		return result;
	case StrategicSpendCategory::DefensiveWmd:
		if (inputs.activeWmdThreats > 0 && affordable && (!recoveryPathNeeded || protectedAffordable || inputs.completedMarkets > 0))
		{
			result.allowed = true;
			result.reason = "defensive_wmd";
			return result;
		}
		if (!recoveryPathNeeded && protectedAffordable)
		{
			result.allowed = true;
			result.reason = "defensive_baseline";
			return result;
		}
		result.reason = inputs.expansionUrgent ? "urgent_expansion_priority" : "recovery_cash_protected";
		return result;
	case StrategicSpendCategory::TechPrerequisite:
	case StrategicSpendCategory::StaticDefense:
	case StrategicSpendCategory::CounterbatteryUnits:
		if (affordable && (protectedAffordable || immediateCollapse))
		{
			result.allowed = true;
			result.reason = immediateCollapse ? "main_base_critical_override" : "spend_allowed";
		}
		else
		{
			result.reason = affordable ? "recovery_cash_protected" : "cash_below_cost";
		}
		return result;
	case StrategicSpendCategory::EconomyGrowth:
		if (affordable && protectedAffordable && !inputs.expansionUrgent)
		{
			result.allowed = true;
			result.reason = "growth_allowed";
		}
		else
		{
			result.reason = inputs.expansionUrgent ? "urgent_expansion_priority" : (affordable ? "reserve_protected" : "cash_below_cost");
		}
		return result;
	case StrategicSpendCategory::LuxuryBaseline:
	default:
		if (recoveryPathNeeded)
		{
			result.reason = inputs.expansionUrgent ? "urgent_expansion_priority" : "recovery_cash_protected";
			return result;
		}
		if (affordable && protectedAffordable)
		{
			result.allowed = true;
			result.reason = "luxury_allowed";
		}
		else
		{
			result.reason = affordable ? "reserve_protected" : "cash_below_cost";
		}
		return result;
	}
}

namespace
{
	int AIControlAdapterScudStormStrategicKindPriority(
		const AIControlAdapterScudStormStrategicTargetCandidate& candidate,
		std::string& normalizedKind)
	{
		const std::string kindLower = AIControlAdapterLowerCopy(candidate.targetKind);
		const std::string templateLower = AIControlAdapterLowerCopy(candidate.templateName);
		if (AIControlAdapterContainsToken(kindLower, "base") ||
			AIControlAdapterContainsToken(kindLower, "command") ||
			AIControlAdapterContainsToken(templateLower, "commandcenter") ||
			AIControlAdapterContainsToken(templateLower, "command_center") ||
			AIControlAdapterContainsToken(templateLower, "command"))
		{
			normalizedKind = "base/command";
			return 4000;
		}
		if (AIControlAdapterContainsToken(kindLower, "production") ||
			AIControlAdapterContainsToken(templateLower, "barracks") ||
			AIControlAdapterContainsToken(templateLower, "armsdealer") ||
			AIControlAdapterContainsToken(templateLower, "warf") ||
			AIControlAdapterContainsToken(templateLower, "warfactory") ||
			AIControlAdapterContainsToken(templateLower, "airfield") ||
			AIControlAdapterContainsToken(templateLower, "strategycenter") ||
			AIControlAdapterContainsToken(templateLower, "propagandacenter") ||
			AIControlAdapterContainsToken(templateLower, "palace"))
		{
			normalizedKind = "production";
			return 3000;
		}
		if (AIControlAdapterContainsToken(kindLower, "economy") ||
			AIControlAdapterContainsToken(templateLower, "supply") ||
			AIControlAdapterContainsToken(templateLower, "stash") ||
			AIControlAdapterContainsToken(templateLower, "blackmarket") ||
			AIControlAdapterContainsToken(templateLower, "market") ||
			AIControlAdapterContainsToken(templateLower, "dropzone") ||
			AIControlAdapterContainsToken(templateLower, "internetcenter") ||
			AIControlAdapterContainsToken(templateLower, "hack"))
		{
			normalizedKind = "economy";
			return 2000;
		}
		if (AIControlAdapterContainsToken(kindLower, "defense") ||
			AIControlAdapterContainsToken(templateLower, "stinger") ||
			AIControlAdapterContainsToken(templateLower, "tunnel") ||
			AIControlAdapterContainsToken(templateLower, "patriot") ||
			AIControlAdapterContainsToken(templateLower, "firebase") ||
			AIControlAdapterContainsToken(templateLower, "bunker") ||
			AIControlAdapterContainsToken(templateLower, "gatling") ||
			AIControlAdapterContainsToken(templateLower, "defense") ||
			AIControlAdapterContainsToken(templateLower, "tower"))
		{
			normalizedKind = "defense";
			return 1000;
		}
		return 0;
	}
}

AIControlAdapterScudStormStrategicTargetResult AIControlAdapterSelectScudStormStrategicTarget(
	const AIControlAdapterScudStormStrategicTargetInputs& inputs)
{
	AIControlAdapterScudStormStrategicTargetResult result;
	result.reason = "no_known_enemy_structures";

	if (!inputs.hasReadyScudStorm)
	{
		result.reason = "no_ready_scud_storm";
		return result;
	}
	if (inputs.hasActiveWmdTarget)
	{
		result.reason = "enemy_wmd_preempts";
		return result;
	}
	if (inputs.fireCooldownActive)
	{
		result.reason = "cooldown";
		return result;
	}

	const AIControlAdapterScudStormStrategicTargetCandidate* best = nullptr;
	std::string bestKind;
	int bestScore = std::numeric_limits<int>::min();
	bool hasKnownCandidate = false;
	bool hasFreshCandidate = false;
	bool hasVisibleHighValue = false;

	for (const AIControlAdapterScudStormStrategicTargetCandidate& candidate : inputs.candidates)
	{
		std::string normalizedKind;
		const int kindPriority = AIControlAdapterScudStormStrategicKindPriority(candidate, normalizedKind);
		if (kindPriority <= 0)
		{
			continue;
		}
		hasKnownCandidate = true;
		if (!candidate.visible && (!candidate.stale || candidate.ageMs > inputs.staleMaxAgeMs))
		{
			continue;
		}
		if (!candidate.enemyOwned || !candidate.alive)
		{
			continue;
		}
		hasFreshCandidate = true;
		if (candidate.visible && kindPriority >= 2000)
		{
			hasVisibleHighValue = true;
		}
	}

	for (const AIControlAdapterScudStormStrategicTargetCandidate& candidate : inputs.candidates)
	{
		std::string normalizedKind;
		const int kindPriority = AIControlAdapterScudStormStrategicKindPriority(candidate, normalizedKind);
		if (kindPriority <= 0 || !candidate.enemyOwned || !candidate.alive)
		{
			continue;
		}
		if (!candidate.visible)
		{
			if (!candidate.stale || candidate.ageMs > inputs.staleMaxAgeMs)
			{
				continue;
			}
			if (hasVisibleHighValue)
			{
				continue;
			}
		}
		const int visibilityScore = candidate.visible ? 600 : 0;
		const int freshnessPenalty = candidate.visible ? 0 : static_cast<int>(std::min<unsigned int>(candidate.ageMs / 1000u, 300u));
		const int score = kindPriority + visibilityScore - freshnessPenalty;
		if (best == nullptr || score > bestScore ||
			(score == bestScore && candidate.objectId < best->objectId))
		{
			best = &candidate;
			bestKind = normalizedKind;
			bestScore = score;
		}
	}

	if (best == nullptr)
	{
		result.reason = hasKnownCandidate && !hasFreshCandidate ? "no_valid_fresh_targets" : "no_known_enemy_structures";
		return result;
	}

	result.hasTarget = true;
	result.objectId = best->objectId;
	result.playerIndex = best->playerIndex;
	result.team = best->team;
	result.targetKind = bestKind;
	result.templateName = best->templateName;
	result.visible = best->visible;
	result.stale = !best->visible;
	result.ageMs = best->ageMs;
	result.x = best->x;
	result.y = best->y;
	result.z = best->z;
	result.score = bestScore;
	result.maxFireCount = 1;
	if (bestKind == "base/command")
	{
		result.reason = best->visible ? "no_wmd_targets_visible_command" : "no_wmd_targets_stale_command";
	}
	else if (bestKind == "production")
	{
		result.reason = best->visible ? "no_wmd_targets_visible_production" : "no_wmd_targets_stale_production";
	}
	else if (bestKind == "economy")
	{
		result.reason = best->visible ? "no_wmd_targets_visible_economy" : "no_wmd_targets_stale_economy";
	}
	else
	{
		result.reason = best->visible ? "no_wmd_targets_visible_defense" : "no_wmd_targets_stale_defense";
	}
	return result;
}

AIControlAdapterMatchOutcomePolicyResult AIControlAdapterClassifyMatchOutcome(
	const AIControlAdapterMatchOutcomePolicyInputs& inputs)
{
	AIControlAdapterMatchOutcomePolicyResult result;
	if (!inputs.victoryConditionsAvailable)
	{
		result.state = "unknown";
		result.reason = "victory_conditions_unavailable";
		return result;
	}
	if (inputs.alliedVictory)
	{
		result.state = "victory";
		result.reason = "allied_victory";
		return result;
	}
	if (inputs.alliedDefeat)
	{
		result.state = "defeat";
		result.reason = "allied_defeat";
		return result;
	}
	if (inputs.localDefeat)
	{
		result.state = "defeat";
		result.reason = "local_defeat";
		return result;
	}
	if (inputs.endFrame > 0u)
	{
		result.state = "draw_or_unknown";
		result.reason = "end_frame_without_local_result";
		return result;
	}
	result.state = "running";
	result.reason = "in_progress";
	return result;
}

bool AIControlAdapterShouldLogTerminalMatchOutcome(
	const std::string& previousLoggedState,
	const std::string& currentState)
{
	const bool terminal =
		currentState == "victory" ||
		currentState == "defeat" ||
		currentState == "draw_or_unknown";
	return terminal && previousLoggedState != currentState;
}

AIControlAdapterDurableMatchOutcomeDecision AIControlAdapterChooseDurableMatchOutcomeAction(
	const AIControlAdapterDurableMatchOutcomeInputs& inputs)
{
	AIControlAdapterDurableMatchOutcomeDecision result;
	if (inputs.newMatchDetected)
	{
		result.action = "reset_for_new_match";
		return result;
	}
	if (inputs.currentState == "victory" || inputs.currentState == "defeat" || inputs.currentState == "draw_or_unknown")
	{
		result.action = "persist_current_terminal";
		return result;
	}
	if (inputs.hasCachedTerminal)
	{
		result.action = "return_cached_terminal";
		return result;
	}
	if (inputs.hasDurableUnknown)
	{
		result.action = "return_cached_unknown";
		return result;
	}
	if (!inputs.meaningfulContext)
	{
		if (inputs.hasLastActiveSnapshot)
		{
			result.action = "create_lost_context_unknown";
			return result;
		}
	}
	if (inputs.meaningfulContext && (inputs.currentState == "running" || inputs.currentState == "unknown"))
	{
		result.action = "cache_current_active";
		return result;
	}
	result.action = "return_current";
	return result;
}

std::vector<std::string> AIControlAdapterChooseRunDiagnosisHints(
	const AIControlAdapterRunDiagnosisInputs& inputs)
{
	std::vector<std::string> hints;
	if (inputs.reserveState == "depleted" || inputs.reserveState == "reserve_depleted")
	{
		hints.push_back("economy_reserve_depleted");
	}
	if (inputs.commandCenters <= 0)
	{
		hints.push_back("no_command_center");
	}
	if (inputs.workers <= 0)
	{
		hints.push_back("no_workers");
	}
	if (inputs.producers <= 0)
	{
		hints.push_back("no_producers");
	}
	if (inputs.defenseReserveDeficits > 0)
	{
		hints.push_back("zone_reserve_deficits");
	}
	if (inputs.stalledConstructionTasks > 0)
	{
		hints.push_back("stalled_construction_tasks");
	}
	if (inputs.knownEnemyWmd > 0)
	{
		hints.push_back("enemy_wmd_still_known");
	}
	if (inputs.activeAttackWaves <= 0)
	{
		hints.push_back("attack_waves_inactive");
	}
	return hints;
}

AIControlAdapterMatchParticipantResult AIControlAdapterClassifyMatchParticipant(
	const AIControlAdapterMatchParticipantInputs& inputs)
{
	AIControlAdapterMatchParticipantResult result;
	result.activeParticipant =
		inputs.hasAssets ||
		(inputs.local && inputs.slotOccupied && inputs.validStartPosition) ||
		(inputs.slotOccupied && inputs.validStartPosition && inputs.validTemplate);
	result.includedInOutcome = result.activeParticipant;
	if (result.includedInOutcome)
	{
		result.nonParticipantReason = "";
	}
	else if (!inputs.slotPresent)
	{
		result.nonParticipantReason = "slot_missing";
	}
	else if (!inputs.slotOccupied)
	{
		result.nonParticipantReason = "closed_slot_or_no_assets";
	}
	else
	{
		result.nonParticipantReason = "no_assets_or_start_position";
	}
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
		result.reason = inputs.mainBaseCriticalOverride
			? "main_base_critical_override"
			: (inputs.hasActiveCriticalAllocation ? "critical_preserve_other_fronts" : "critical_override");
		if (inputs.mainBaseCriticalOverride)
		{
			result.desiredDefenders = 20;
			result.maxNewAssignments = 14;
			result.minHoldMs = 60000u;
			result.timeoutMs = 135000u;
		}
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
		const int criticalReserve = inputs.mainBaseCriticalOverride ? 2 : (inputs.hasActiveCriticalAllocation ? 4 : 2);
		allowedByReserve = std::max(0, inputs.availableIdleCombat - criticalReserve);
	}
	result.maxNewAssignments = std::max(0, std::min(result.maxNewAssignments, allowedByReserve));

	if (inputs.activeDefenseAllocations >= 3 && !result.criticalOverride)
	{
		result.maxNewAssignments = std::min(result.maxNewAssignments, 3);
		result.reason = "allocation_pressure";
	}

	return result;
}

AIControlAdapterMainBaseCriticalOverrideResult AIControlAdapterEvaluateMainBaseCriticalOverride(
	const AIControlAdapterMainBaseCriticalOverrideInputs& inputs)
{
	AIControlAdapterMainBaseCriticalOverrideResult result;
	result.active = false;
	result.reason = "no_core_pressure";

	if (!inputs.isMainBase)
	{
		result.reason = "not_main_base";
		return result;
	}
	if (inputs.localEnemyCount <= 0)
	{
		result.reason = "no_local_enemies";
		return result;
	}
	if (inputs.threatLevel != "critical")
	{
		result.reason = "severity_below_threshold";
		return result;
	}
	if (inputs.recentWmd)
	{
		result.active = true;
		result.reason = "wmd_plus_local_enemies";
		return result;
	}
	if (inputs.damagedStructures > 0 || inputs.destroyedStructures > 0)
	{
		result.active = true;
		result.reason = "core_structure_under_attack";
		return result;
	}
	if (inputs.repeatedCriticalDamageCount > 0)
	{
		result.active = true;
		result.reason = "repeated_critical_damage";
		return result;
	}

	return result;
}

AIControlAdapterZoneDefenseReserveResult AIControlAdapterChooseZoneDefenseReserve(
	const AIControlAdapterZoneDefenseReserveInputs& inputs)
{
	AIControlAdapterZoneDefenseReserveResult result;
	result.posture = "interior";
	result.floor = 1;
	result.productionNeeded = false;
	result.reason = "interior";

	if (inputs.isMainBase)
	{
		result.posture = "main_base";
		result.floor = inputs.hasActiveThreat ? 10 : 8;
		result.reason = inputs.hasActiveThreat ? "main_base_under_pressure" : "main_base";
		result.productionNeeded = inputs.hasActiveThreat;
		return result;
	}

	const bool activePressure =
		inputs.hasActiveThreat &&
		(inputs.threatSeverity >= 3 ||
		 inputs.localEnemyCount > 0 ||
		 inputs.enemyArtilleryCount > 0 ||
		 inputs.damagedStructures > 0 ||
		 inputs.destroyedStructures > 0);
	const bool repeatedPressure =
		inputs.recentAttackCount >= 2 ||
		inputs.damagedStructures >= 2 ||
		inputs.destroyedStructures > 0;
	const bool quietDecayed = inputs.quietMs >= 120000u;

	if ((inputs.isActiveZone || inputs.isFrontier || inputs.hasActiveAllocation) &&
		!quietDecayed &&
		(activePressure || repeatedPressure))
	{
		result.posture = "contested_front";
		result.floor = 10;
		result.reason = repeatedPressure ? "repeated_attack" : "sustained_pressure";
		result.productionNeeded = true;
		return result;
	}

	if (inputs.isActiveZone)
	{
		result.posture = "frontline";
		result.floor = 6;
		result.reason = quietDecayed ? "quiet_decay" : "active_front";
		result.productionNeeded = inputs.hasActiveThreat;
		return result;
	}

	if (inputs.isFrontier)
	{
		result.posture = "outer";
		result.floor = 4;
		result.reason = quietDecayed ? "quiet_decay" : "frontier";
		result.productionNeeded = inputs.hasActiveThreat;
		return result;
	}

	if (inputs.hasActiveThreat)
	{
		result.posture = "outer";
		result.floor = 3;
		result.reason = "threatened_interior_edge";
		result.productionNeeded = true;
		return result;
	}

	result.floor = 1;
	return result;
}

AIControlAdapterZoneDefenseDonorFloorResult AIControlAdapterApplyZoneDefenseDonorFloor(
	const AIControlAdapterZoneDefenseDonorFloorInputs& inputs)
{
	AIControlAdapterZoneDefenseDonorFloorResult result;
	result.allowed = 0;
	result.blocked = std::max(0, inputs.requested);
	result.reason = "reserve_floor";

	if (inputs.requested <= 0)
	{
		result.blocked = 0;
		result.reason = "no_request";
		return result;
	}

	if (inputs.mainBaseCriticalOverride)
	{
		result.allowed = std::min(inputs.requested, std::max(0, inputs.residentCount));
		result.blocked = std::max(0, inputs.requested - result.allowed);
		result.reason = "main_base_critical_override";
		return result;
	}

	const int surplus = std::max(0, inputs.residentCount - inputs.floor);
	result.allowed = std::min(inputs.requested, surplus);
	result.blocked = std::max(0, inputs.requested - result.allowed);
	if (result.allowed <= 0)
	{
		if (inputs.sourceHasThreat)
		{
			result.reason = "source_threatened";
		}
		else if (inputs.sourceIsContestedFront)
		{
			result.reason = "contested_front";
		}
		else if (inputs.sourceIsActiveFront)
		{
			result.reason = "active_front";
		}
		else if (inputs.sourceHasAllocation)
		{
			result.reason = "active_allocation";
		}
		else
		{
			result.reason = "reserve_floor";
		}
	}
	else
	{
		result.reason = "surplus";
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
		result.reason = inputs.recentWmd ? "local_enemy_units_recent_wmd" : "local_enemy_units";
		return result;
	}

	if (inputs.enemyArtilleryCount > 0)
	{
		result.type = "artillery_attack";
		result.response = "counterbattery";
		result.reason = "enemy_artillery_detected";
		return result;
	}

	if (inputs.recentWmd)
	{
		result.type = "wmd_strike";
		result.response = "hold_rebuild_recover";
		result.reason = "recent_wmd_no_local_enemy";
		return result;
	}

	result.type = "unknown_damage";
	result.response = "hold_rebuild_recover";
	result.reason = "damage_no_local_enemy";

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

AIControlAdapterEmergencySurvivalProductionDecision AIControlAdapterChooseEmergencySurvivalProduction(
	const AIControlAdapterEmergencySurvivalProductionInputs& inputs)
{
	AIControlAdapterEmergencySurvivalProductionDecision result;
	result.active = false;
	result.allowReserveSpend = false;
	result.suppressCaptureSourceProduction = false;
	result.bypassArmyCapBuffer = false;
	result.emergencyArmyCap = std::max(inputs.armyCap + 30, (inputs.armyCap * 3) / 2);
	result.reason = "not_emergency";

	if (result.emergencyArmyCap < inputs.armyCap)
	{
		result.emergencyArmyCap = inputs.armyCap;
	}

	if (inputs.brutalEmergencyPriority)
	{
		result.active = true;
		result.reason = "brutal_pressure_emergency_survival";
	}
	else if (inputs.mainUnderPressure)
	{
		result.active = true;
		result.reason = "main_under_pressure";
	}
	else if (inputs.activeUnitAttack)
	{
		result.active = true;
		result.reason = "active_unit_attack";
	}
	else if (inputs.criticalThreatZones > 0 && inputs.localEnemyCount > 0)
	{
		result.active = true;
		result.reason = "critical_zone_threat";
	}

	if (!result.active)
	{
		return result;
	}

	result.allowReserveSpend = true;
	result.suppressCaptureSourceProduction = true;
	result.bypassArmyCapBuffer = true;

	if (inputs.armyCount >= result.emergencyArmyCap)
	{
		result.reason = "emergency_cap_reached";
		return result;
	}

	if (inputs.armsDealers > 0)
	{
		if (inputs.quads <= 2 || inputs.enemyArtilleryCount > 0 || inputs.localEnemyCount > 0)
		{
			result.commands.push_back("Game.QueueQuadsAllWarFactories");
		}
		if (inputs.scorpions <= 3 || inputs.localEnemyCount > inputs.scorpions)
		{
			result.commands.push_back("Game.QueueScorpionsAllWarFactories");
		}
	}
	if (inputs.barracks > 0 && (inputs.rpg <= 4 || inputs.localEnemyCount > 0 || inputs.enemyArtilleryCount > 0))
	{
		result.commands.push_back("Game.QueueRpgTroopersAllBarracks");
	}
	if (result.commands.empty() && inputs.barracks > 0 && inputs.soldiers <= 2)
	{
		result.commands.push_back("Game.QueueSoldiersAllBarracks");
	}
	if (result.commands.empty())
	{
		result.reason = "producer_missing";
	}

	return result;
}

AIControlAdapterPalaceRecoveryDecision AIControlAdapterChoosePalaceRecovery(
	const AIControlAdapterPalaceRecoveryInputs& inputs)
{
	AIControlAdapterPalaceRecoveryDecision result;
	result.shouldBuild = false;
	result.reason = "no_threat";

	if (inputs.palaces > 0)
	{
		result.reason = "palace_present";
		return result;
	}
	if (inputs.palacesInProgress > 0)
	{
		result.reason = "healthy_in_progress";
		return result;
	}
	if (!inputs.enemyWmdThreat && !inputs.mobileSiegeThreat)
	{
		result.reason = "no_threat";
		return result;
	}
	if (inputs.money < inputs.palaceCost)
	{
		result.reason = "reserve_blocked";
		return result;
	}
	if (!inputs.buildAttemptReady)
	{
		result.reason = "healthy_in_progress";
		return result;
	}

	result.shouldBuild = true;
	result.reason = "missing_palace_under_wmd_threat";
	return result;
}

AIControlAdapterTerrainFacts AIControlAdapterBuildTerrainFacts(
	const std::string& mapName,
	bool hasMainBase,
	float mainBaseX,
	float mainBaseY)
{
	AIControlAdapterTerrainFacts facts;
	facts.mapName = mapName;
	facts.source = "unavailable";
	facts.extraction.mapName = mapName;
	facts.extraction.selectedSource = "unavailable";
	facts.extraction.fallbackSource = "none";
	facts.extraction.reason = "no_fixture";

	const std::string lowerMap = AIControlAdapterLowerCopy(mapName);
	const bool isDeathValley =
		AIControlAdapterContainsToken(lowerMap, "deathvalley")
		|| (AIControlAdapterContainsToken(lowerMap, "death") && AIControlAdapterContainsToken(lowerMap, "valley"));
	if (!isDeathValley || !hasMainBase)
	{
		return facts;
	}

	facts.source = "manual_fixture";
	facts.extraction.selectedSource = "manual_fixture";
	facts.extraction.reason = "death_valley_fixture";

	const float dirX = mainBaseX < 3000.0f ? 1.0f : -1.0f;
	const float dirY = mainBaseY < 3000.0f ? 0.35f : -0.35f;
	const float len = std::max(0.001f, std::sqrt((dirX * dirX) + (dirY * dirY)));
	const float nx = dirX / len;
	const float ny = dirY / len;
	const float px = -ny;
	const float py = nx;

	auto point = [](float x, float y) -> AIControlAdapterTerrainPoint
	{
		AIControlAdapterTerrainPoint p;
		p.x = x;
		p.y = y;
		return p;
	};
	auto offset = [&](float forward, float side) -> AIControlAdapterTerrainPoint
	{
		return point(mainBaseX + (nx * forward) + (px * side), mainBaseY + (ny * forward) + (py * side));
	};

	AIControlAdapterTerrainFeature northBarrier;
	northBarrier.id = "death-valley-main-ridge-north";
	northBarrier.kind = "impassable_barrier";
	northBarrier.source = "manual_fixture";
	northBarrier.points.push_back(offset(220.0f, 760.0f));
	northBarrier.points.push_back(offset(720.0f, 900.0f));
	northBarrier.points.push_back(offset(1420.0f, 820.0f));
	facts.features.push_back(northBarrier);

	AIControlAdapterTerrainFeature southBarrier;
	southBarrier.id = "death-valley-main-ridge-south";
	southBarrier.kind = "impassable_barrier";
	southBarrier.source = "manual_fixture";
	southBarrier.points.push_back(offset(220.0f, -760.0f));
	southBarrier.points.push_back(offset(720.0f, -900.0f));
	southBarrier.points.push_back(offset(1420.0f, -820.0f));
	facts.features.push_back(southBarrier);

	AIControlAdapterTerrainFeature upperEntry;
	upperEntry.id = "death-valley-main-base-upper-entry";
	upperEntry.kind = "base_entrance";
	upperEntry.source = "manual_fixture";
	upperEntry.hasPosition = true;
	upperEntry.position = offset(820.0f, 390.0f);
	upperEntry.width = 450.0f;
	upperEntry.connects.push_back("main_base");
	upperEntry.connects.push_back("upper_approach");
	facts.features.push_back(upperEntry);

	AIControlAdapterTerrainFeature lane;
	lane.id = "death-valley-main-approach-lane";
	lane.kind = "lane";
	lane.source = "manual_fixture";
	lane.points.push_back(point(mainBaseX, mainBaseY));
	lane.points.push_back(offset(900.0f, 0.0f));
	lane.points.push_back(offset(1800.0f, 0.0f));
	facts.features.push_back(lane);

	return facts;
}

std::string AIControlAdapterNormalizeMapFileCacheKey(const std::string& mapName)
{
	std::string leaf = mapName;
	const std::size_t slash = leaf.find_last_of("\\/");
	if (slash != std::string::npos)
	{
		leaf = leaf.substr(slash + 1);
	}
	const std::size_t dot = leaf.find_last_of('.');
	if (dot != std::string::npos)
	{
		leaf = leaf.substr(0, dot);
	}

	std::string key;
	bool pendingDash = false;
	for (std::size_t i = 0; i < leaf.size(); ++i)
	{
		const unsigned char ch = static_cast<unsigned char>(leaf[i]);
		if (std::isalnum(ch))
		{
			if (!key.empty()
				&& std::isupper(ch)
				&& i > 0
				&& std::islower(static_cast<unsigned char>(leaf[i - 1])))
			{
				pendingDash = true;
			}
			if (pendingDash && !key.empty())
			{
				key.push_back('-');
			}
			key.push_back(static_cast<char>(std::tolower(ch)));
			pendingDash = false;
		}
		else
		{
			pendingDash = true;
		}
	}
	return key;
}

bool AIControlAdapterParseMapFileCacheJson(
	const nlohmann::json& cacheJson,
	const std::string& mapName,
	const std::string& sourcePath,
	AIControlAdapterTerrainFacts& outFacts,
	std::string& outReason)
{
	outFacts = AIControlAdapterTerrainFacts();
	outFacts.mapName = mapName;
	outFacts.source = "unavailable";
	outFacts.extraction.mapName = mapName;
	outFacts.extraction.selectedSource = "unavailable";
	outFacts.extraction.fallbackSource = "none";
	outFacts.extraction.reason = "not_loaded";
	outFacts.mapFileCacheTelemetry = nlohmann::json::object({
		{"loaded", false},
		{"source", sourcePath},
		{"reason", "not_loaded"}
	});

	if (!cacheJson.is_object())
	{
		outReason = "parse_error";
		outFacts.mapFileCacheTelemetry["reason"] = outReason;
		return false;
	}
	if (cacheJson.value("schema_version", 0) != 1)
	{
		outReason = "unsupported_schema";
		outFacts.mapFileCacheTelemetry["reason"] = outReason;
		return false;
	}

	const nlohmann::json mapInfo = cacheJson.value("map", nlohmann::json::object());
	const nlohmann::json metadata = cacheJson.value("metadata", nlohmann::json::object());
	const std::string mapHash = mapInfo.value("hash", "");
	int ignoredSemanticFeatures = 0;
	int annotationFeatures = 0;
	int trustedEntrances = 0;
	int semanticFeaturesLoaded = 0;
	int waypointCount = 0;
	int laneCount = 0;

	const nlohmann::json features = cacheJson.value("features", nlohmann::json::array());
	if (features.is_array())
	{
		for (nlohmann::json::const_iterator it = features.begin(); it != features.end(); ++it)
		{
			if (!it->is_object())
			{
				continue;
			}
			const std::string kind = it->value("kind", "");
			const std::string originalSource = it->value("source", "");
			const bool annotationFeature = originalSource == "manual_annotation" || originalSource == "map_file_annotation";
			if (annotationFeature)
			{
				++annotationFeatures;
			}
			if (!AIControlAdapterIsTrustedMapFileCacheFeature(*it))
			{
				if (kind == "base_entrance" || kind == "chokepoint")
				{
					++ignoredSemanticFeatures;
				}
				continue;
			}

			AIControlAdapterTerrainFeature feature;
			feature.id = it->value("id", "");
			if (feature.id.empty())
			{
				feature.id = std::string("map-file-cache-feature-") + std::to_string(outFacts.features.size());
			}
			feature.kind = kind;
			feature.source = "map_file_cache";
			AIControlAdapterTerrainPoint position;
			if (AIControlAdapterReadTerrainPoint(it->value("position", nlohmann::json::object()), position))
			{
				feature.hasPosition = true;
				feature.position = position;
			}
			const nlohmann::json points = it->value("points", nlohmann::json::array());
			if (points.is_array())
			{
				for (nlohmann::json::const_iterator pointIt = points.begin(); pointIt != points.end(); ++pointIt)
				{
					AIControlAdapterTerrainPoint point;
					if (AIControlAdapterReadTerrainPoint(*pointIt, point))
					{
						feature.points.push_back(point);
					}
				}
			}
			const nlohmann::json labels = it->contains("connects")
				? it->value("connects", nlohmann::json::array())
				: it->value("labels", nlohmann::json::array());
			if (labels.is_array())
			{
				for (nlohmann::json::const_iterator labelIt = labels.begin(); labelIt != labels.end(); ++labelIt)
				{
					if (labelIt->is_string())
					{
						feature.connects.push_back(labelIt->get<std::string>());
					}
				}
			}
			feature.width = it->value("width", 0.0f);
			feature.trusted = it->value("trusted", false);
			if (kind == "waypoint")
			{
				++waypointCount;
			}
			else if (kind == "lane")
			{
				++laneCount;
			}
			else if (kind == "base_entrance" || kind == "chokepoint")
			{
				++semanticFeaturesLoaded;
				if (feature.trusted && kind == "base_entrance")
				{
					++trustedEntrances;
				}
			}
			outFacts.features.push_back(feature);
		}
	}

	const nlohmann::json objects = cacheJson.value("objects", nlohmann::json::array());
	if (objects.is_array())
	{
		for (nlohmann::json::const_iterator it = objects.begin(); it != objects.end(); ++it)
		{
			if (!it->is_object())
			{
				continue;
			}
			nlohmann::json object = *it;
			object["source"] = "map_file_cache";
			outFacts.strategicObjects.push_back(object);
		}
	}

	outFacts.source = outFacts.features.empty() ? "unavailable" : "map_file_cache";
	outFacts.extraction.selectedSource = outFacts.source;
	outFacts.extraction.reason = outFacts.features.empty() ? "cache_empty" : "matched_normalized_map_name";
	outFacts.extraction.waypointCount = waypointCount;
	outFacts.mapFileCacheTelemetry = nlohmann::json::object({
		{"loaded", !outFacts.features.empty()},
		{"source", sourcePath},
		{"map_hash", mapHash},
		{"features_loaded", static_cast<int>(outFacts.features.size())},
		{"waypoints", waypointCount},
		{"lanes", laneCount},
		{"objects", static_cast<int>(outFacts.strategicObjects.size())},
		{"warnings", cacheJson.value("warnings", nlohmann::json::array()).is_array() ? static_cast<int>(cacheJson.value("warnings", nlohmann::json::array()).size()) : 0},
		{"ignored_semantic_features", ignoredSemanticFeatures},
		{"annotation_features", annotationFeatures},
		{"trusted_entrances", trustedEntrances},
		{"semantic_features_loaded", semanticFeaturesLoaded},
		{"reason", outFacts.features.empty() ? "cache_empty" : "matched_normalized_map_name"}
	});
	if (metadata.is_object())
	{
		outFacts.mapFileCacheTelemetry["decoded_waypoints"] = metadata.value("decoded_waypoint_count", waypointCount);
		outFacts.mapFileCacheTelemetry["decoded_lanes"] = metadata.value("decoded_waypoint_link_count", laneCount);
		outFacts.mapFileCacheTelemetry["decoded_cliff_cells"] = metadata.value("decoded_cliff_cell_count", 0);
		outFacts.mapFileCacheTelemetry["derived_barriers"] = metadata.value("derived_barrier_feature_count", 0);
	}

	if (outFacts.features.empty())
	{
		outReason = "cache_empty";
		return false;
	}
	outReason = "matched_normalized_map_name";
	return true;
}

AIControlAdapterTerrainFacts AIControlAdapterMergeMapFileCacheFacts(
	const AIControlAdapterTerrainFacts& extractedFacts,
	const AIControlAdapterTerrainFacts& fixtureFacts,
	const AIControlAdapterTerrainFacts& mapFileCacheFacts)
{
	AIControlAdapterTerrainFacts selected = AIControlAdapterSelectTerrainFacts(extractedFacts, fixtureFacts);
	selected.mapFileCacheTelemetry = mapFileCacheFacts.mapFileCacheTelemetry.is_object()
		? mapFileCacheFacts.mapFileCacheTelemetry
		: nlohmann::json::object({ {"loaded", false}, {"reason", "not_evaluated"} });

	if (mapFileCacheFacts.source == "map_file_cache" && !mapFileCacheFacts.features.empty())
	{
		if (selected.source == "unavailable" || selected.features.empty())
		{
			selected = mapFileCacheFacts;
			selected.mapFileCacheTelemetry = mapFileCacheFacts.mapFileCacheTelemetry;
		}
		else
		{
			bool hasTrustedCacheEntrance = false;
			for (std::size_t i = 0; i < mapFileCacheFacts.features.size(); ++i)
			{
				const AIControlAdapterTerrainFeature& feature = mapFileCacheFacts.features[i];
				if (feature.id != "death-valley-main-base-lower-entry"
					&& feature.trusted
					&& (feature.kind == "base_entrance" || feature.kind == "chokepoint"))
				{
					hasTrustedCacheEntrance = true;
					break;
				}
			}
			if (hasTrustedCacheEntrance)
			{
				std::vector<AIControlAdapterTerrainFeature> retained;
				for (std::size_t i = 0; i < selected.features.size(); ++i)
				{
					const AIControlAdapterTerrainFeature& feature = selected.features[i];
					if (feature.source == "manual_fixture"
						&& (feature.kind == "base_entrance" || feature.kind == "chokepoint"))
					{
						continue;
					}
					retained.push_back(feature);
				}
				selected.features.swap(retained);
				selected.mapFileCacheTelemetry["fixture_skipped"] = true;
				selected.mapFileCacheTelemetry["fixture_skip_reason"] = "trusted_cache_annotation";
			}
			std::set<std::string> existingIds;
			for (std::size_t i = 0; i < selected.features.size(); ++i)
			{
				existingIds.insert(selected.features[i].id);
			}
			for (std::size_t i = 0; i < mapFileCacheFacts.features.size(); ++i)
			{
				const AIControlAdapterTerrainFeature& feature = mapFileCacheFacts.features[i];
				if (feature.id == "death-valley-main-base-lower-entry")
				{
					selected.mapFileCacheTelemetry["ignored_lower_entry"] = true;
					continue;
				}
				if (existingIds.insert(feature.id).second)
				{
					selected.features.push_back(feature);
				}
			}
			selected.strategicObjects = mapFileCacheFacts.strategicObjects;
		}

		if (!fixtureFacts.features.empty())
		{
			bool hasEntrance = false;
			bool hasBarrier = false;
			std::set<std::string> existingIds;
			for (std::size_t i = 0; i < selected.features.size(); ++i)
			{
				existingIds.insert(selected.features[i].id);
				if (selected.features[i].kind == "base_entrance" || selected.features[i].kind == "chokepoint")
				{
					hasEntrance = true;
				}
				if (selected.features[i].kind == "impassable_barrier")
				{
					hasBarrier = true;
				}
			}
			for (std::size_t i = 0; i < fixtureFacts.features.size(); ++i)
			{
				const AIControlAdapterTerrainFeature& fixture = fixtureFacts.features[i];
				const bool needed =
					((fixture.kind == "base_entrance" && !hasEntrance)
						|| (fixture.kind == "impassable_barrier" && !hasBarrier));
				if (needed && existingIds.insert(fixture.id).second)
				{
					selected.features.push_back(fixture);
				}
			}
		}
	}
	else if (mapFileCacheFacts.mapFileCacheTelemetry.is_object())
	{
		selected.mapFileCacheTelemetry = mapFileCacheFacts.mapFileCacheTelemetry;
	}

	return selected;
}

AIControlAdapterTerrainFacts AIControlAdapterSelectTerrainFacts(
	const AIControlAdapterTerrainFacts& extractedFacts,
	const AIControlAdapterTerrainFacts& fixtureFacts)
{
	if ((extractedFacts.source == "engine_query" || extractedFacts.source == "engine_sample" || extractedFacts.source == "map_file")
		&& !extractedFacts.features.empty())
	{
		AIControlAdapterTerrainFacts selected = extractedFacts;
		if (!fixtureFacts.features.empty())
		{
			bool hasEntrance = false;
			for (std::size_t i = 0; i < selected.features.size(); ++i)
			{
				if (selected.features[i].kind == "base_entrance" || selected.features[i].kind == "chokepoint")
				{
					hasEntrance = true;
					break;
				}
			}
			if (!hasEntrance)
			{
				for (std::size_t i = 0; i < fixtureFacts.features.size(); ++i)
				{
					if (fixtureFacts.features[i].kind == "base_entrance" || fixtureFacts.features[i].kind == "impassable_barrier")
					{
						selected.features.push_back(fixtureFacts.features[i]);
					}
				}
			}
		}
		selected.extraction.selectedSource = extractedFacts.source;
		selected.extraction.fallbackSource = fixtureFacts.features.empty() ? "none" : fixtureFacts.source;
		selected.extraction.fallbackUsed = false;
		if (selected.extraction.reason.empty())
		{
			selected.extraction.reason = "extraction_available";
		}
		return selected;
	}

	if (!fixtureFacts.features.empty())
	{
		AIControlAdapterTerrainFacts selected = fixtureFacts;
		selected.extraction = extractedFacts.extraction;
		selected.extraction.mapName = !fixtureFacts.mapName.empty() ? fixtureFacts.mapName : extractedFacts.mapName;
		selected.extraction.selectedSource = fixtureFacts.source;
		selected.extraction.fallbackSource = extractedFacts.source.empty() ? "unavailable" : extractedFacts.source;
		selected.extraction.fallbackUsed = true;
		selected.extraction.reason = extractedFacts.features.empty() ? "fixture_after_extraction_unavailable" : "fixture_after_extraction_insufficient";
		return selected;
	}

	AIControlAdapterTerrainFacts selected = extractedFacts;
	selected.source = "unavailable";
	selected.extraction.selectedSource = "unavailable";
	selected.extraction.fallbackSource = "none";
	selected.extraction.fallbackUsed = false;
	selected.extraction.reason = "no_extraction_or_fixture";
	return selected;
}

nlohmann::json AIControlAdapterSerializeTerrainFacts(const AIControlAdapterTerrainFacts& facts)
{
	nlohmann::json features = nlohmann::json::array();
	for (std::size_t i = 0; i < facts.features.size(); ++i)
	{
		const AIControlAdapterTerrainFeature& feature = facts.features[i];
		nlohmann::json item = nlohmann::json::object({
			{"id", feature.id},
			{"kind", feature.kind},
			{"source", feature.source}
		});
		if (feature.hasPosition)
		{
			item["position"] = nlohmann::json::object({
				{"x", feature.position.x},
				{"y", feature.position.y}
			});
			if (feature.width > 0.0f)
			{
				item["width"] = feature.width;
			}
		}
		if (feature.trusted)
		{
			item["trusted"] = true;
		}
		if (!feature.points.empty())
		{
			nlohmann::json points = nlohmann::json::array();
			for (std::size_t pointIdx = 0; pointIdx < feature.points.size(); ++pointIdx)
			{
				points.push_back(nlohmann::json::object({
					{"x", feature.points[pointIdx].x},
					{"y", feature.points[pointIdx].y}
				}));
			}
			item["points"] = points;
		}
		if (!feature.connects.empty())
		{
			item["connects"] = feature.connects;
		}
		features.push_back(item);
	}

	return nlohmann::json::object({
		{"source", facts.source},
		{"terrain_source", facts.source},
		{"map", facts.mapName},
		{"map_file_cache", facts.mapFileCacheTelemetry.is_object()
			? facts.mapFileCacheTelemetry
			: nlohmann::json::object({ {"loaded", false}, {"reason", "not_evaluated"} })},
		{"terrain_extraction", nlohmann::json::object({
			{"map", facts.extraction.mapName.empty() ? facts.mapName : facts.extraction.mapName},
			{"extent", nlohmann::json::object({
				{"min_x", facts.extraction.extent.minX},
				{"min_y", facts.extraction.extent.minY},
				{"max_x", facts.extraction.extent.maxX},
				{"max_y", facts.extraction.extent.maxY}
			})},
			{"sample_step", facts.extraction.sampleStep},
			{"blocked_samples", facts.extraction.blockedSamples},
			{"passable_samples", facts.extraction.passableSamples},
			{"cliff_samples", facts.extraction.cliffSamples},
			{"unknown_samples", facts.extraction.unknownSamples},
			{"bridge_count", facts.extraction.bridgeCount},
			{"waypoint_count", facts.extraction.waypointCount},
			{"fallback_used", facts.extraction.fallbackUsed},
			{"selected_source", facts.extraction.selectedSource.empty() ? facts.source : facts.extraction.selectedSource},
			{"fallback_source", facts.extraction.fallbackSource.empty() ? "none" : facts.extraction.fallbackSource},
			{"reason", facts.extraction.reason.empty() ? "not_evaluated" : facts.extraction.reason}
		})},
		{"strategic_objects", facts.strategicObjects},
		{"terrain_features", features}
	});
}

AIControlAdapterZoneTerrainResult AIControlAdapterApplyZoneTerrainFacts(
	const AIControlAdapterTerrainFacts& facts,
	const AIControlAdapterZoneTerrainInputs& inputs)
{
	AIControlAdapterZoneTerrainResult result;
	result.effectiveRadius = inputs.radius;
	result.terrainLimited = false;
	result.hasEntrance = false;
	result.frontSource = "fallback";
	result.reason = facts.features.empty() ? "unavailable" : "none";

	float dirX = inputs.frontDirX;
	float dirY = inputs.frontDirY;
	const float dirLen = std::sqrt((dirX * dirX) + (dirY * dirY));
	if (dirLen > 0.001f)
	{
		dirX /= dirLen;
		dirY /= dirLen;
	}
	else
	{
		dirX = 1.0f;
		dirY = 0.0f;
	}
	result.frontPoint = { inputs.centerX + (dirX * result.effectiveRadius), inputs.centerY + (dirY * result.effectiveRadius) };
	result.rearPoint = { inputs.centerX - (dirX * result.effectiveRadius), inputs.centerY - (dirY * result.effectiveRadius) };

	float nearestBarrierSq = std::numeric_limits<float>::max();
	const AIControlAdapterTerrainFeature* nearestEntrance = nullptr;
	float nearestEntranceSq = std::numeric_limits<float>::max();

	for (std::size_t i = 0; i < facts.features.size(); ++i)
	{
		const AIControlAdapterTerrainFeature& feature = facts.features[i];
		const float distSq = AIControlAdapterDistancePointToFeatureSq(inputs.centerX, inputs.centerY, feature);
		if (feature.kind == "impassable_barrier" || feature.kind == "blocked_area")
		{
			nearestBarrierSq = std::min(nearestBarrierSq, distSq);
		}
		else if ((feature.kind == "base_entrance" || feature.kind == "chokepoint") && feature.hasPosition)
		{
			const AIControlAdapterTerrainPoint center = { inputs.centerX, inputs.centerY };
			const float approachDx = feature.position.x - inputs.centerX;
			const float approachDy = feature.position.y - inputs.centerY;
			const bool towardFront = ((approachDx * dirX) + (approachDy * dirY)) >= -80.0f;
			const bool crossesBarrier = AIControlAdapterSegmentCrossesBarrier(facts, center, feature.position);
			if (towardFront && !crossesBarrier && distSq < nearestEntranceSq)
			{
				nearestEntranceSq = distSq;
				nearestEntrance = &feature;
			}
		}
	}

	if (nearestBarrierSq < std::numeric_limits<float>::max())
	{
		const float nearestBarrier = std::sqrt(nearestBarrierSq);
		const float limitedRadius = std::max(160.0f, nearestBarrier * 0.85f);
		if (limitedRadius + 1.0f < result.effectiveRadius)
		{
			result.effectiveRadius = limitedRadius;
			result.terrainLimited = true;
			result.reason = "barrier";
			result.frontPoint = { inputs.centerX + (dirX * result.effectiveRadius), inputs.centerY + (dirY * result.effectiveRadius) };
			result.rearPoint = { inputs.centerX - (dirX * result.effectiveRadius), inputs.centerY - (dirY * result.effectiveRadius) };
		}
	}

	if (inputs.isMainBase && nearestEntrance != nullptr)
	{
		result.hasEntrance = true;
		result.entrancePosition = nearestEntrance->position;
		result.entranceId = nearestEntrance->id;
		result.frontPoint = nearestEntrance->position;
		result.frontSource = nearestEntrance->kind == "chokepoint" ? "chokepoint" : "entrance";
		const float rearDx = inputs.centerX - nearestEntrance->position.x;
		const float rearDy = inputs.centerY - nearestEntrance->position.y;
		const float rearLen = std::sqrt((rearDx * rearDx) + (rearDy * rearDy));
		if (rearLen > 0.001f)
		{
			const float rearDistance = std::max(160.0f, result.effectiveRadius * 0.55f);
			result.rearPoint = {
				inputs.centerX + ((rearDx / rearLen) * rearDistance),
				inputs.centerY + ((rearDy / rearLen) * rearDistance)
			};
		}
		if (!result.terrainLimited)
		{
			result.reason = "entrance";
		}
	}
	else if (result.terrainLimited)
	{
		result.frontSource = "terrain_limited";
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

AIControlAdapterGarrisonCandidateDecision AIControlAdapterEvaluateGarrisonCandidate(
	const AIControlAdapterGarrisonCandidateInput& input)
{
	AIControlAdapterGarrisonCandidateDecision decision;
	if (!input.palace && !input.mapGarrison && !input.runtimeGarrisonable && !input.mapCacheGarrison)
	{
		decision.reason = "not_garrisonable";
		return decision;
	}
	if (input.enemyOwned)
	{
		decision.reason = "enemy_owned";
		return decision;
	}
	if (!input.friendlyOwned && !input.neutralOwned)
	{
		decision.reason = "unsafe";
		return decision;
	}

	const bool mapStyleGarrison = input.mapGarrison || input.runtimeGarrisonable || input.mapCacheGarrison;
	decision.capacity = input.palace ? 5 : 10;
	decision.desiredInfantry = input.palace ? 4 : 8;
	if (mapStyleGarrison && !input.nearArtilleryPlatform && !input.nearChokepoint && !input.zoneActive && !input.repeatedAttack)
	{
		decision.desiredInfantry = 4;
		decision.capacity = 6;
	}
	if (decision.desiredInfantry > decision.capacity)
	{
		decision.desiredInfantry = decision.capacity;
	}
	if (input.existingAssigned >= decision.desiredInfantry)
	{
		decision.selected = true;
		decision.reason = "full";
		decision.score = 1;
		return decision;
	}
	if (!input.zoneUseful)
	{
		decision.reason = "not_useful";
		return decision;
	}
	if (input.distanceToZone > input.usefulRadius)
	{
		decision.reason = "too_far";
		return decision;
	}

	decision.score = input.palace ? 120 : 70;
	if (input.zoneActive)
	{
		decision.score += 45;
	}
	if (input.zoneDeveloped)
	{
		decision.score += 25;
	}
	if (input.repeatedAttack)
	{
		decision.score += 35;
	}
	if (input.nearArtilleryPlatform)
	{
		decision.score += 45;
	}
	if (input.nearChokepoint)
	{
		decision.score += 25;
	}
	if (input.neutralOwned)
	{
		decision.score -= 10;
	}
	decision.score -= static_cast<int>(input.distanceToZone / 120.0f);
	if (decision.score < 1)
	{
		decision.score = 1;
	}
	decision.selected = true;
	decision.reason = "selected";
	return decision;
}

AIControlAdapterGarrisonProductionDecision AIControlAdapterChooseGarrisonProduction(
	const AIControlAdapterGarrisonProductionInput& input)
{
	AIControlAdapterGarrisonProductionDecision decision;
	if (input.desiredInfantry <= input.assignedInfantry)
	{
		decision.reason = "filled";
		return decision;
	}
	if (!input.barracksReady)
	{
		decision.reason = "producer_missing";
		return decision;
	}
	if (input.money < 300u)
	{
		decision.reason = "cash_below_unit_cost";
		return decision;
	}
	if (input.money < input.reserveCash + 300u)
	{
		decision.reason = "reserve_protected";
		return decision;
	}

	const int gap = input.desiredInfantry - input.assignedInfantry - input.availableInfantry - input.queuedInfantry;
	if (gap <= 0)
	{
		decision.reason = "available_infantry";
		return decision;
	}
	decision.productionNeeded = true;
	decision.desiredQueued = gap > 2 ? 2 : gap;
	decision.reason = "garrison_gap";
	return decision;
}

AIControlAdapterGarrisonThroughputDecision AIControlAdapterEvaluateGarrisonThroughput(
	const AIControlAdapterGarrisonThroughputInput& input)
{
	AIControlAdapterGarrisonThroughputDecision decision;
	const int selected = std::max(0, input.selectedStructures);
	const int filled = std::max(0, input.filledStructures);
	const int structureGap = std::max(0, selected - filled);
	const int infantryGap = std::max(0, input.desiredInfantry - std::max(input.assignedInfantry, input.enteredInfantry));
	const unsigned int cashFloat = input.money > input.reserveCash ? input.money - input.reserveCash : 0u;
	const int protectedReserve =
		std::max(2, input.infantryReserve) +
		std::max(0, input.activeScoutAssignments) +
		std::max(0, input.activeRaidAssignments / 4) +
		std::max(0, input.zoneDefenseReserved / 4);
	decision.infantryReserveHeld = protectedReserve;

	if (input.criticalEmergency)
	{
		decision.maxAssignmentsThisCycle = 0;
		decision.mode = "hold";
		decision.reason = "critical_emergency_reserve";
		return decision;
	}
	if (selected <= 0 || infantryGap <= 0 || structureGap <= 0)
	{
		decision.maxAssignmentsThisCycle = 0;
		decision.mode = "hold";
		decision.reason = selected <= 0 ? "no_selected_garrisons" : "garrisons_filled";
		return decision;
	}

	const int assignableInfantry = std::max(0, input.availableRpgInfantry - protectedReserve);
	if (assignableInfantry <= 0)
	{
		decision.maxAssignmentsThisCycle = 0;
		decision.mode = "hold";
		decision.reason = "infantry_reserve_held";
		if (input.barracksReady >= 4 && infantryGap >= 12 && cashFloat >= 15000u)
		{
			decision.productionFanout = std::min(4, std::max(1, input.barracksReady / 4));
		}
		return decision;
	}

	const bool accelerated =
		cashFloat >= 25000u &&
		input.barracksReady >= 6 &&
		infantryGap >= 24 &&
		assignableInfantry >= 8 &&
		selected >= 4;
	if (!accelerated)
	{
		decision.maxAssignmentsThisCycle = 1;
		decision.mode = "normal";
		decision.reason = "normal_pacing";
		if (input.barracksReady >= 4 && infantryGap >= 12 && cashFloat >= 15000u)
		{
			decision.productionFanout = 1;
		}
		return decision;
	}

	decision.mode = "accelerated";
	decision.reason = "late_game_high_gap";
	decision.maxAssignmentsThisCycle = std::min(6, std::max(2, std::min(structureGap, assignableInfantry / 4)));
	decision.productionFanout = std::min(4, std::max(2, std::min(input.barracksReady / 4, infantryGap / 8)));
	return decision;
}
