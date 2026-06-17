#include "GameClient/AIControlAdapter/AIControlAdapterProfilePolicyManager.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << "\n";
			std::exit(1);
		}
	}
}

int main()
{
	const AIControlAdapterProfilePolicyManager manager;

	{
		AIControlAdapterProfilePolicyRequest request;
		request.profile = "sprawl_balanced";
		request.economyBias = 0.65f;
		request.aggressionBias = 0.35f;
		request.defenseBias = 0.45f;
		request.expansionBias = 0.55f;
		request.sprawlMultiplier = 10.0f;
		const AIControlAdapterProfilePolicyConfig config = manager.Resolve(request);
		expect(config.isBalancedSprawl, "Manager should preserve balanced-sprawl classification");
		expect(config.isSprawlStyle, "Manager should preserve sprawl-style classification");
		expect(config.reserveCash == 10000u, "Manager should preserve balanced reserve");
		expect(manager.ResolveReserveCashWithFloor(config, 5000u) == 10000u, "Manager should preserve balanced reserve above floor");
		expect(config.sprawlSupplyCap == 30, "Manager should preserve balanced supply cap");
		expect(config.normalMaxConcurrentExpansionStashes == 3, "Manager should preserve balanced normal expansion concurrency");
		expect(config.allowExpansionBeforeFullRemoteFollowup, "Manager should preserve balanced remote follow-up policy");
		expect(manager.ResolveGuardCadenceMs(config) == 7000u, "Manager should preserve balanced-sprawl guard cadence");
		expect(manager.ResolveCombatArmyCapBase(config) == 100, "Manager should preserve balanced-sprawl army cap base");
		expect(manager.UsesTechRetryPolicy(config), "Manager should use tech retry policy for balanced sprawl");
		expect(
			manager.ResolveStrategicSpendDesiredZoneCount(config, 4) == 30,
			"Manager should use balanced sprawl supply cap for strategic spend zone target");
	}

	{
		AIControlAdapterProfilePolicyRequest request;
		request.profile = "sprawl";
		request.sprawlMultiplier = 10.0f;
		const AIControlAdapterProfilePolicyConfig config = manager.Resolve(request);
		expect(!config.isBalancedSprawl, "Manager should preserve plain sprawl classification");
		expect(config.isSprawlStyle, "Manager should preserve plain sprawl style");
		expect(config.reserveCash == 5000u, "Manager should preserve plain sprawl reserve");
		expect(config.sprawlSupplyCap == 40, "Manager should preserve plain sprawl supply cap");
		expect(config.normalMaxConcurrentExpansionStashes == 4, "Manager should preserve plain sprawl expansion concurrency");
		expect(manager.ResolveGuardCadenceMs(config) == 7000u, "Manager should preserve plain sprawl guard cadence");
		expect(manager.UsesTechRetryPolicy(config), "Manager should use tech retry policy for plain sprawl");
		expect(
			manager.ResolveStrategicSpendDesiredZoneCount(config, 4) == 40,
			"Manager should use plain sprawl supply cap for strategic spend zone target");
	}

	{
		AIControlAdapterProfilePolicyRequest request;
		request.profile = "aggressive";
		request.sprawlMultiplier = 20.0f;
		const AIControlAdapterProfilePolicyConfig config = manager.Resolve(request);
		expect(config.isAggressive, "Manager should preserve aggressive classification");
		expect(!config.isSprawlStyle, "Manager should preserve non-sprawl classification");
		expect(config.reserveCash == 0u, "Manager should preserve non-sprawl reserve");
		expect(manager.ResolveReserveCashWithFloor(config, 5000u) == 5000u, "Manager should apply non-sprawl reserve floor");
		expect(config.workerCooldownMs == 1500u, "Manager should preserve aggressive worker cooldown");
		expect(manager.ResolveGuardCadenceMs(config) == 5000u, "Manager should preserve aggressive guard cadence");
		expect(manager.ResolveCombatArmyCapBase(config) == 9999, "Manager should preserve non-balanced army cap base");
		expect(!manager.UsesTechRetryPolicy(config), "Manager should not use tech retry policy for aggressive profile");
		expect(config.sprawlSupplyCap == 40, "Manager should preserve multiplier clamp");
		expect(
			manager.ResolveStrategicSpendDesiredZoneCount(config, 6) == 6,
			"Manager should preserve non-sprawl strategic spend fallback target");
	}

	{
		AIControlAdapterProfilePolicyRequest request;
		request.profile = "economic";
		const AIControlAdapterProfilePolicyConfig config = manager.Resolve(request);
		expect(config.isEconomic, "Manager should preserve economic classification");
		expect(manager.WantsBaselineTwoMarkets(config), "Manager should preserve economic baseline market policy");
		expect(manager.WantsAcceleratedOpeningSupply(config, 0.0f), "Manager should accelerate economic opening supply");
		expect(manager.ResolveRadarVanMinCount(config) == 1, "Manager should preserve economic radar van count");
		expect(manager.AllowsRadarVanAutomation(config), "Manager should allow economic radar van automation");
	}

	{
		AIControlAdapterProfilePolicyRequest request;
		request.economyBias = 0.80f;
		const AIControlAdapterProfilePolicyConfig config = manager.Resolve(request);
		expect(config.workerMinIdle == 2, "Manager should preserve economy-bias worker idle tuning");
		expect(config.workerQueueCount == 2, "Manager should preserve economy-bias worker queue tuning");
		expect(config.stashWorkersPerStash == 10, "Manager should preserve economy-bias stash worker tuning");
		expect(config.attackMinUnits == 38, "Manager should not change neutral attack tuning from economy bias alone");
	}

	{
		AIControlAdapterProfilePolicyRequest request;
		request.defenseBias = 0.80f;
		const AIControlAdapterProfilePolicyConfig config = manager.Resolve(request);
		expect(!config.isDefensive, "Manager should not classify defense bias as defensive profile");
		expect(config.attackMinUnits == 60, "Manager should preserve defense-bias attack threshold");
		expect(config.attackGroupSize == 40, "Manager should preserve defense-bias attack group size");
		expect(config.attackCooldownMs == 26000u, "Manager should preserve defense-bias attack cooldown");
		expect(manager.ResolveGuardCadenceMs(config) == 6000u, "Manager should preserve default guard cadence for defense bias");
	}

	{
		AIControlAdapterProfilePolicyRequest request;
		request.profile = "defensive";
		const AIControlAdapterProfilePolicyConfig config = manager.Resolve(request);
		expect(config.isDefensive, "Manager should preserve defensive profile classification");
		expect(manager.ResolveGuardCadenceMs(config) == 7000u, "Manager should preserve defensive guard cadence");
		expect(!manager.AllowsRadarVanAutomation(config), "Manager should block defensive radar van automation");
	}

	{
		AIControlAdapterProfilePolicyRequest request;
		request.profile = "tech";
		const AIControlAdapterProfilePolicyConfig config = manager.Resolve(request);
		expect(config.isTech, "Manager should preserve tech classification");
		expect(manager.ResolveGuardCadenceMs(config) == 6000u, "Manager should preserve default guard cadence for tech");
		expect(manager.UsesTechRetryPolicy(config), "Manager should use tech retry policy for tech profile");
		expect(manager.WantsBaselineTwoMarkets(config), "Manager should preserve tech baseline market policy");
		expect(manager.ResolveRadarVanMinCount(config) == 2, "Manager should preserve tech radar van count");
	}

	{
		AIControlAdapterProfilePolicyRequest request;
		request.profile = "sprawl_balanced";
		request.sprawlMultiplier = 10.0f;
		request.overrides.hasUrgentZoneGapThreshold = true;
		request.overrides.urgentZoneGapThreshold = 0;
		request.overrides.hasMaxConcurrentExpansionStashes = true;
		request.overrides.maxConcurrentExpansionStashes = 0;
		request.overrides.hasAllowExpansionBeforeFullRemoteFollowup = true;
		request.overrides.allowExpansionBeforeFullRemoteFollowup = false;
		request.overrides.hasExpansionHighCashFloatThreshold = true;
		request.overrides.expansionHighCashFloatThreshold = 25000u;
		const AIControlAdapterProfilePolicyConfig config = manager.Resolve(request);
		expect(config.urgentZoneGapThreshold == 1, "Manager should clamp urgent zone gap override");
		expect(config.normalMaxConcurrentExpansionStashes == 1, "Manager should clamp expansion concurrency override");
		expect(!config.allowExpansionBeforeFullRemoteFollowup, "Manager should apply remote follow-up override");
		expect(config.expansionHighCashFloatThreshold == 25000u, "Manager should apply high-cash override");
	}

	std::cout << "AIControlAdapterProfilePolicyManagerTests passed\n";
	return 0;
}
