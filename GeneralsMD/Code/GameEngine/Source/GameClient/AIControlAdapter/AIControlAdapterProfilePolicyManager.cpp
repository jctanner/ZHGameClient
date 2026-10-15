#include "GameClient/AIControlAdapter/AIControlAdapterProfilePolicyManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
	struct ProfilePolicyPreset
	{
		bool isBalancedSprawl = false;
		bool isSprawlStyle = false;
		unsigned int reserveCash = 0u;
		int workerMinIdle = 1;
		int workerQueueCount = 1;
		unsigned int workerCooldownMs = 2500u;
		int stashWorkersPerStash = 8;
		unsigned int stashWorkerCooldownMs = 5000u;
		int attackMinUnits = 38;
		int attackGroupSize = 28;
		unsigned int attackCooldownMs = 18000u;
		float supplyCapScale = 4.0f;
		float barracksCapScale = 2.0f;
		float armsCapScale = 3.0f;
		float marketCapScale = 8.0f;
		float tunnelCapScale = 8.0f;
		float stingerCapScale = 6.0f;
	};

	float clampUnitFloat(float value)
	{
		return std::max(0.0f, std::min(1.0f, value));
	}

	std::string lowerCopy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) -> unsigned char
		{
			return static_cast<unsigned char>(std::tolower(ch));
		});
		return value;
	}

	ProfilePolicyPreset resolveProfilePolicyPreset(
		const std::string& normalizedProfile,
		float econ,
		float aggro,
		float defense)
	{
		ProfilePolicyPreset preset;
		// Both presets reuse the adaptive sprawl execution/policy family.
		preset.isBalancedSprawl = normalizedProfile == "sprawl_balanced" || normalizedProfile == "sprawl_economic";
		preset.isSprawlStyle = normalizedProfile == "sprawl" || preset.isBalancedSprawl;
		preset.reserveCash = preset.isBalancedSprawl ? 10000u : (normalizedProfile == "sprawl" ? 5000u : 0u);

		preset.workerMinIdle = (econ >= 0.70f || normalizedProfile == "economic" || preset.isSprawlStyle) ? 2 : 1;
		preset.workerQueueCount = (econ >= 0.75f || normalizedProfile == "economic" || preset.isSprawlStyle) ? 2 : 1;
		preset.workerCooldownMs = (normalizedProfile == "aggressive") ? 1500u : 2500u;

		if (normalizedProfile == "sprawl")
		{
			preset.workerMinIdle = 3;
			preset.workerQueueCount = 1;
			preset.workerCooldownMs = 2000u;
		}
		else if (preset.isBalancedSprawl)
		{
			preset.workerMinIdle = 2;
			preset.workerQueueCount = 1;
			preset.workerCooldownMs = 2500u;
		}

		if (normalizedProfile == "economic" || preset.isSprawlStyle || econ >= 0.70f)
		{
			preset.stashWorkersPerStash = 10;
		}
		else if (normalizedProfile == "aggressive")
		{
			preset.stashWorkersPerStash = 7;
		}

		if (normalizedProfile == "sprawl")
		{
			preset.stashWorkersPerStash = 3;
			preset.stashWorkerCooldownMs = 12000u;
		}
		else if (preset.isBalancedSprawl)
		{
			preset.stashWorkersPerStash = 6;
			preset.stashWorkerCooldownMs = 8000u;
		}

		if (normalizedProfile == "aggressive" || aggro >= 0.70f)
		{
			preset.attackMinUnits = 24;
			preset.attackGroupSize = 20;
			preset.attackCooldownMs = 12000u;
		}
		else if (normalizedProfile == "economic")
		{
			preset.attackMinUnits = 50;
			preset.attackGroupSize = 34;
			preset.attackCooldownMs = 22000u;
		}
		else if (normalizedProfile == "defensive" || defense >= 0.70f)
		{
			preset.attackMinUnits = 60;
			preset.attackGroupSize = 40;
			preset.attackCooldownMs = 26000u;
		}
		else if (normalizedProfile == "tech")
		{
			preset.attackMinUnits = 44;
			preset.attackGroupSize = 30;
			preset.attackCooldownMs = 20000u;
		}
		else if (normalizedProfile == "sprawl")
		{
			preset.attackMinUnits = 70;
			preset.attackGroupSize = 45;
			preset.attackCooldownMs = 26000u;
		}
		else if (preset.isBalancedSprawl)
		{
			preset.attackMinUnits = 55;
			preset.attackGroupSize = 28;
			preset.attackCooldownMs = 22000u;
		}

		if (preset.isBalancedSprawl)
		{
			preset.supplyCapScale = 3.0f;
			preset.barracksCapScale = 1.5f;
			preset.armsCapScale = 2.0f;
			preset.marketCapScale = 6.0f;
			preset.tunnelCapScale = 5.0f;
			preset.stingerCapScale = 4.0f;
		}

		return preset;
	}

	int scaledCap(float scale, float multiplier)
	{
		return std::max(1, static_cast<int>(std::floor(scale * multiplier)));
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
	const std::string normalizedProfile = lowerCopy(profile);
	const float econ = clampUnitFloat(economyBias);
	const float aggro = clampUnitFloat(aggressionBias);
	const float defense = clampUnitFloat(defenseBias);
	const float multiplier = std::max(0.5f, std::min(10.0f, sprawlMultiplier));
	const ProfilePolicyPreset preset = resolveProfilePolicyPreset(normalizedProfile, econ, aggro, defense);

	AIControlAdapterProfilePolicyConfig config;
	config.profile = normalizedProfile;
	config.isAggressive = normalizedProfile == "aggressive";
	config.isEconomic = normalizedProfile == "economic";
	config.isDefensive = normalizedProfile == "defensive";
	config.isTech = normalizedProfile == "tech";
	config.isBalancedSprawl = preset.isBalancedSprawl;
	config.isEconomyFirstSprawl = normalizedProfile == "sprawl_economic";
	config.isSprawlStyle = preset.isSprawlStyle;
	config.reserveCash = preset.reserveCash;
	config.workerMinIdle = preset.workerMinIdle;
	config.workerQueueCount = preset.workerQueueCount;
	config.workerCooldownMs = preset.workerCooldownMs;
	config.stashWorkersPerStash = preset.stashWorkersPerStash;
	config.stashWorkerCooldownMs = preset.stashWorkerCooldownMs;
	config.attackMinUnits = preset.attackMinUnits;
	config.attackGroupSize = preset.attackGroupSize;
	config.attackCooldownMs = preset.attackCooldownMs;
	config.sprawlSupplyCap = scaledCap(preset.supplyCapScale, multiplier);
	config.sprawlBarracksCap = scaledCap(preset.barracksCapScale, multiplier);
	config.sprawlArmsCap = scaledCap(preset.armsCapScale, multiplier);
	config.sprawlMarketCap = scaledCap(preset.marketCapScale, multiplier);
	config.sprawlTunnelCap = scaledCap(preset.tunnelCapScale, multiplier);
	config.sprawlStingerCap = scaledCap(preset.stingerCapScale, multiplier);

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
	if (config.isEconomyFirstSprawl)
	{
		config.reserveCash = 6000u;
		config.stashWorkersPerStash = 2;
		config.workerQueueCount = 1;
		config.workerMinIdle = 2;
		config.normalMaxConcurrentExpansionStashes = 1;
		config.sprawlMarketCap = std::max(config.sprawlMarketCap, 24);
	}
	return config;
}

AIControlAdapterProfileSpendPlan AIControlAdapterProfilePolicyManager::ResolveSpendPlan(
	const AIControlAdapterProfilePolicyConfig& config,
	unsigned int availableCash, int completedMarkets,
	int localEnemies, bool threatResponse, bool sustainedOffense) const
{
	AIControlAdapterProfileSpendPlan plan;
	plan.active = config.isEconomyFirstSprawl;
	if (!plan.active) return plan;
	plan.threatResponse = threatResponse;
	plan.sustainedOffense = sustainedOffense;
	plan.surplusUnderThreat = threatResponse && availableCash >= config.reserveCash + config.threatSurplusCashFloat;
	plan.workerLimit = config.workerLimit;
	plan.armyTarget = sustainedOffense ? 1000 : threatResponse
		? std::min(150, std::max(config.responseArmyFloor, 32 + std::min(29, std::max(0, localEnemies)) * 4))
		: config.quietArmyTarget;
	// Once the income asset threshold is met, preserve the small phase-entry
	// balance across purchases so our own growth cannot reset qualification.
	plan.growthProtectedCash = !threatResponse && !sustainedOffense
		&& completedMarkets >= config.offensiveMarketThreshold
		? config.reserveCash + config.offensiveCashFloat : config.reserveCash;
	// During response, discretionary construction/worker purchases yield to
	// combat. Essential income and builder recovery retain a bounded allowance.
	plan.incomeProtectedCash = threatResponse ? 3000u : plan.growthProtectedCash;
	plan.workerProtectedCash = threatResponse ? config.reserveCash : std::max(config.reserveCash + 2500u, plan.growthProtectedCash);
	plan.combatProtectedCash = threatResponse ? 1000u : std::max(config.reserveCash + 2500u, plan.growthProtectedCash);
	plan.maxConcurrentMarkets = !threatResponse && completedMarkets >= 3
		&& availableCash >= plan.incomeProtectedCash + 5000u ? 2 : 1;
	plan.reason = plan.surplusUnderThreat ? "threat_response_surplus" : threatResponse ? "threat_response" : sustainedOffense ? "income_funded_offense" : "quiet_economic_growth";
	return plan;
}

AIControlAdapterProfilePolicyConfig AIControlAdapterProfilePolicyManager::Resolve(
	const AIControlAdapterProfilePolicyRequest& request) const
{
	AIControlAdapterProfilePolicyConfig config = AIControlAdapterResolveProfilePolicyConfig(
		request.profile,
		request.economyBias,
		request.aggressionBias,
		request.defenseBias,
		request.expansionBias,
		request.sprawlMultiplier);

	if (request.overrides.hasUrgentZoneGapThreshold)
	{
		config.urgentZoneGapThreshold = std::max(1, request.overrides.urgentZoneGapThreshold);
	}
	if (request.overrides.hasMaxConcurrentExpansionStashes)
	{
		config.normalMaxConcurrentExpansionStashes = std::max(1, request.overrides.maxConcurrentExpansionStashes);
	}
	if (request.overrides.hasAllowExpansionBeforeFullRemoteFollowup)
	{
		config.allowExpansionBeforeFullRemoteFollowup = request.overrides.allowExpansionBeforeFullRemoteFollowup;
	}
	if (request.overrides.hasExpansionHighCashFloatThreshold)
	{
		config.expansionHighCashFloatThreshold = request.overrides.expansionHighCashFloatThreshold;
	}
	return config;
}

int AIControlAdapterProfilePolicyManager::ResolveStrategicSpendDesiredZoneCount(
	const AIControlAdapterProfilePolicyConfig& config,
	int nonSprawlFallback) const
{
	return std::max(1, config.isSprawlStyle ? config.sprawlSupplyCap : nonSprawlFallback);
}

unsigned int AIControlAdapterProfilePolicyManager::ResolveGuardCadenceMs(
	const AIControlAdapterProfilePolicyConfig& config) const
{
	if (config.isAggressive)
	{
		return 5000u;
	}
	if (config.isSprawlStyle || config.isDefensive)
	{
		return 7000u;
	}
	return 6000u;
}

unsigned int AIControlAdapterProfilePolicyManager::ResolveReserveCashWithFloor(
	const AIControlAdapterProfilePolicyConfig& config,
	unsigned int minimumReserveCash) const
{
	return std::max(config.reserveCash, minimumReserveCash);
}

int AIControlAdapterProfilePolicyManager::ResolveCombatArmyCapBase(
	const AIControlAdapterProfilePolicyConfig& config) const
{
	return config.isBalancedSprawl ? 100 : 9999;
}

bool AIControlAdapterProfilePolicyManager::UsesTechRetryPolicy(
	const AIControlAdapterProfilePolicyConfig& config) const
{
	return config.isSprawlStyle || config.isTech;
}

bool AIControlAdapterProfilePolicyManager::AllowsRadarVanAutomation(
	const AIControlAdapterProfilePolicyConfig& config) const
{
	return !config.isDefensive && config.profile != "builtin_passthrough";
}

int AIControlAdapterProfilePolicyManager::ResolveRadarVanMinCount(
	const AIControlAdapterProfilePolicyConfig& config) const
{
	return config.isTech ? 2 : 1;
}

bool AIControlAdapterProfilePolicyManager::WantsBaselineTwoMarkets(
	const AIControlAdapterProfilePolicyConfig& config) const
{
	return config.isEconomic || config.isTech;
}

bool AIControlAdapterProfilePolicyManager::WantsAcceleratedOpeningSupply(
	const AIControlAdapterProfilePolicyConfig& config,
	float expansionBias) const
{
	return config.isEconomic || config.isSprawlStyle || expansionBias >= 0.70f;
}
