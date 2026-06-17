#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildSnapshotBuilder.h"

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

	AIControlAdapterProfilePolicyConfig makeBalancedPolicy()
	{
		AIControlAdapterProfilePolicyConfig policy;
		policy.profile = "sprawl_balanced";
		policy.isBalancedSprawl = true;
		policy.isSprawlStyle = true;
		policy.reserveCash = 10000u;
		policy.sprawlSupplyCap = 30;
		policy.sprawlBarracksCap = 15;
		policy.sprawlArmsCap = 20;
		policy.sprawlMarketCap = 60;
		policy.sprawlTunnelCap = 50;
		policy.sprawlStingerCap = 40;
		policy.urgentZoneGapThreshold = 5;
		policy.normalMaxConcurrentExpansionStashes = 3;
		policy.allowExpansionBeforeFullRemoteFollowup = true;
		policy.expansionHighCashFloatThreshold = 10000u;
		return policy;
	}
}

int main()
{
	const AIControlAdapterMacroBuildSnapshotBuilder builder;

	{
		AIControlAdapterMacroBuildSnapshotBuilderInput input;
		input.policyConfig = makeBalancedPolicy();
		input.money = 50000u;
		input.hasActiveZone = true;
		input.activeZoneIsMainBase = false;
		input.totalSupplyStashes = 1;
		input.totalBarracks = 1;
		input.totalArmsDealers = 1;
		input.completedBlackMarkets = 0;
		input.effectiveTotalBlackMarkets = 0;
		input.totalZoneSupplyStashes = 1;
		input.totalZoneBarracks = 1;
		const AIControlAdapterMacroBuildSnapshot snapshot = builder.Build(input);
		expect(snapshot.desiredZoneCount == 30, "Builder should derive desired zones from policy supply cap");
		expect(snapshot.maxConcurrentExpansionStashes == 3, "Builder should copy expansion concurrency from policy");
		expect(snapshot.remoteZoneHasStash, "Builder should derive remote zone stash state");
		expect(!snapshot.canScaleMilitaryProduction, "Balanced sprawl should wait for opening economy before scaling producers");
		expect(snapshot.effectiveBarracksCap == 1, "Balanced sprawl should cap early barracks growth");
		expect(snapshot.effectiveArmsCap == 1, "Balanced sprawl should cap early arms growth");
		expect(!snapshot.balancedZoneCanAddBarracks, "Builder should derive per-zone barracks cap");
	}

	{
		AIControlAdapterMacroBuildSnapshotBuilderInput input;
		input.policyConfig = makeBalancedPolicy();
		input.money = 9000u;
		input.hasCompletedPalace = true;
		input.hasActiveZone = true;
		input.activeZoneIsMainBase = false;
		input.totalSupplyStashes = 8;
		input.totalBarracks = 3;
		input.totalArmsDealers = 2;
		input.completedBlackMarkets = 1;
		input.inProgressBlackMarkets = 1;
		input.effectiveTotalBlackMarkets = 2;
		input.totalZoneSupplyStashes = 1;
		input.totalZoneBarracks = 1;
		input.totalZoneArmsDealers = 1;
		input.totalZoneTunnels = 1;
		const AIControlAdapterMacroBuildSnapshot snapshot = builder.Build(input);
		expect(snapshot.canScaleMilitaryProduction, "Balanced sprawl should scale producers after economy is open");
		expect(snapshot.effectiveBarracksCap == 15, "Builder should restore balanced barracks cap after economy is open");
		expect(snapshot.shouldPreserveReserve, "Builder should derive balanced reserve preservation");
		expect(snapshot.shouldThrottleExtraStashGrowth, "Builder should derive developed-zone stash throttling");
	}

	{
		AIControlAdapterMacroBuildSnapshotBuilderInput input;
		input.policyConfig = makeBalancedPolicy();
		AIControlAdapterMacroBuildSnapshot base = builder.Build(input);

		AIControlAdapterMacroBuildSnapshotRuntimeInput runtime;
		runtime.expansionSpend = { false, "reserve_protected" };
		runtime.marketGrowthSpend = { true, "growth_allowed" };
		runtime.zoneSeedCooldownReady = true;
		runtime.staticDefenseZoneIndex = 4;
		runtime.staticDefenseCommand = "Game.BuildStingerSite";
		runtime.staticDefenseReason = "frontier_floor";
		runtime.palaceRedundancyZoneIndex = 2;
		runtime.palaceRedundancyReason = "anchor_redundancy";
		const AIControlAdapterMacroBuildSnapshot snapshot = builder.ApplyRuntimeInput(base, runtime);
		expect(!snapshot.expansionSpend.allowed, "Runtime overlay should preserve expansion spend gate");
		expect(snapshot.expansionSpend.reason == std::string("reserve_protected"), "Runtime overlay should preserve expansion spend reason");
		expect(snapshot.marketGrowthSpend.allowed, "Runtime overlay should preserve market growth spend gate");
		expect(snapshot.zoneSeedCooldownReady, "Runtime overlay should preserve zone seed cooldown");
		expect(snapshot.staticDefenseZoneIndex == 4, "Runtime overlay should preserve static defense zone");
		expect(snapshot.staticDefenseCommand == std::string("Game.BuildStingerSite"), "Runtime overlay should preserve static defense command");
		expect(snapshot.palaceRedundancyZoneIndex == 2, "Runtime overlay should preserve Palace redundancy zone");
		expect(snapshot.palaceRedundancyReason == std::string("anchor_redundancy"), "Runtime overlay should preserve Palace redundancy reason");
	}

	std::cout << "AIControlAdapterMacroBuildSnapshotBuilderTests passed\n";
	return 0;
}
