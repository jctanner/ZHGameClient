#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildSnapshotBuilder.h"

#include <algorithm>

AIControlAdapterMacroBuildSnapshot AIControlAdapterMacroBuildSnapshotBuilder::Build(
	const AIControlAdapterMacroBuildSnapshotBuilderInput& input) const
{
	const AIControlAdapterProfilePolicyConfig& policy = input.policyConfig;
	const bool openingInfrastructureReady =
		input.totalSupplyStashes >= 1
		&& input.totalBarracks >= 1
		&& input.totalArmsDealers >= 1;
	const bool openingEconomyReady = input.totalSupplyStashes >= 2 || input.completedBlackMarkets >= 1;
	const bool canScaleMilitaryProduction = !policy.isBalancedSprawl || (openingInfrastructureReady && openingEconomyReady);
	const int effectiveBarracksCap = policy.isBalancedSprawl
		? (canScaleMilitaryProduction ? policy.sprawlBarracksCap : 1)
		: policy.sprawlBarracksCap;
	const int effectiveArmsCap = policy.isBalancedSprawl
		? (canScaleMilitaryProduction ? policy.sprawlArmsCap : 1)
		: policy.sprawlArmsCap;
	const bool remoteZoneHasStash =
		input.hasActiveZone
		&& !input.activeZoneIsMainBase
		&& input.totalZoneSupplyStashes > 0;
	const bool activeZoneIsDeveloped =
		input.hasActiveZone
		&& input.totalZoneSupplyStashes > 0
		&& (input.totalZoneBarracks + input.totalZoneArmsDealers + input.totalZoneTunnels + input.totalZoneStingers) >= 3;
	const bool shouldThrottleExtraStashGrowth =
		policy.isSprawlStyle
		&& input.hasCompletedPalace
		&& input.effectiveTotalBlackMarkets < std::max<int>(
			policy.isBalancedSprawl ? 2 : 1,
			policy.isBalancedSprawl ? input.totalSupplyStashes : (input.totalSupplyStashes / 2))
		&& activeZoneIsDeveloped;
	const bool shouldPreserveReserve =
		policy.isBalancedSprawl
		&& (input.money < policy.reserveCash || input.inProgressBlackMarkets > 0)
		&& input.hasCompletedPalace
		&& input.effectiveTotalBlackMarkets > 0;

	AIControlAdapterMacroBuildSnapshot snapshot;
	snapshot.isSprawlStyle = policy.isSprawlStyle;
	snapshot.isBalancedSprawl = policy.isBalancedSprawl;
	snapshot.hasActiveZone = input.hasActiveZone;
	snapshot.money = input.money;
	snapshot.reserveCash = policy.reserveCash;
	snapshot.blackMarketCost = input.blackMarketCost;
	snapshot.stashZoneCount = input.stashZoneCount;
	snapshot.developedZoneCount = input.developedZoneCount;
	snapshot.desiredZoneCount = std::max<int>(1, policy.sprawlSupplyCap);
	snapshot.urgentZoneGapThreshold = policy.urgentZoneGapThreshold;
	snapshot.expansionHighCashFloatThreshold = policy.expansionHighCashFloatThreshold;
	snapshot.supplyFootprintRadius = input.supplyFootprintRadius;
	snapshot.remoteSupplyZoneCount = input.remoteSupplyZoneCount;
	snapshot.localSupplyEstablished = input.totalSupplyStashes >= 2;
	snapshot.remoteZoneHasStash = remoteZoneHasStash;
	snapshot.allowExpansionBeforeFullRemoteFollowup = policy.allowExpansionBeforeFullRemoteFollowup;
	snapshot.shouldThrottleExtraStashGrowth = shouldThrottleExtraStashGrowth;
	snapshot.activeZoneThreatened = input.activeZoneThreatened;
	snapshot.canScaleMilitaryProduction = canScaleMilitaryProduction;
	snapshot.supplyStashesInProgress = input.supplyStashesInProgress;
	snapshot.maxConcurrentExpansionStashes = policy.normalMaxConcurrentExpansionStashes;
	snapshot.supplyBuildCooldownReady = input.supplyBuildCooldownReady;
	snapshot.totalZoneTunnels = input.totalZoneTunnels;
	snapshot.totalZoneStingers = input.totalZoneStingers;
	snapshot.totalZoneBarracks = input.totalZoneBarracks;
	snapshot.totalZoneArmsDealers = input.totalZoneArmsDealers;
	snapshot.activeZoneTunnelsInProgress = input.activeZoneTunnelsInProgress;
	snapshot.activeZoneStingersInProgress = input.activeZoneStingersInProgress;
	snapshot.activeZoneBarracksInProgress = input.activeZoneBarracksInProgress;
	snapshot.activeZoneArmsDealersInProgress = input.activeZoneArmsDealersInProgress;
	snapshot.hasCompletedPalace = input.hasCompletedPalace;
	snapshot.totalSupplyStashes = input.totalSupplyStashes;
	snapshot.totalBarracks = input.totalBarracks;
	snapshot.totalArmsDealers = input.totalArmsDealers;
	snapshot.totalPalaces = input.totalPalaces;
	snapshot.completedBlackMarkets = input.completedBlackMarkets;
	snapshot.shouldPreserveReserve = shouldPreserveReserve;
	snapshot.wantsBaselineTwoMarkets = input.wantsBaselineTwoMarkets;
	snapshot.sprawlSupplyCap = policy.sprawlSupplyCap;
	snapshot.sprawlBarracksCap = policy.sprawlBarracksCap;
	snapshot.sprawlArmsCap = policy.sprawlArmsCap;
	snapshot.sprawlMarketCap = policy.sprawlMarketCap;
	snapshot.sprawlTunnelCap = policy.sprawlTunnelCap;
	snapshot.sprawlStingerCap = policy.sprawlStingerCap;
	snapshot.effectiveBarracksCap = effectiveBarracksCap;
	snapshot.effectiveArmsCap = effectiveArmsCap;
	snapshot.balancedZoneCanAddBarracks = !policy.isBalancedSprawl || !input.hasActiveZone || input.totalZoneBarracks < 1;
	snapshot.balancedZoneCanAddArmsDealer = !policy.isBalancedSprawl || !input.hasActiveZone || input.totalZoneArmsDealers < 1;
	snapshot.effectiveTotalBlackMarkets = input.effectiveTotalBlackMarkets;
	snapshot.inProgressBlackMarkets = input.inProgressBlackMarkets;
	snapshot.blackMarketBuildCooldownReady = input.blackMarketBuildCooldownReady;
	snapshot.barracksInProgress = input.barracksInProgress;
	snapshot.armsDealersInProgress = input.armsDealersInProgress;
	snapshot.tunnelsInProgress = input.tunnelsInProgress;
	snapshot.stingersInProgress = input.stingersInProgress;
	snapshot.barracksBuildCooldownReady = input.barracksBuildCooldownReady;
	snapshot.armsDealerBuildCooldownReady = input.armsDealerBuildCooldownReady;
	snapshot.tunnelBuildCooldownReady = input.tunnelBuildCooldownReady;
	snapshot.stingerBuildCooldownReady = input.stingerBuildCooldownReady;
	snapshot.totalTunnels = input.totalTunnels;
	snapshot.totalStingers = input.totalStingers;
	snapshot.palaceBuildCooldownReady = input.palaceBuildCooldownReady;
	snapshot.palacesInProgress = input.palacesInProgress;
	snapshot.expansionSpend = input.expansionSpend;
	snapshot.marketRecoverySpend = input.marketRecoverySpend;
	snapshot.marketGrowthSpend = input.marketGrowthSpend;
	snapshot.palaceSpend = input.palaceSpend;
	snapshot.staticDefenseSpend = input.staticDefenseSpend;
	snapshot.zoneSeedCooldownReady = input.zoneSeedCooldownReady;
	return snapshot;
}

AIControlAdapterMacroBuildSnapshot AIControlAdapterMacroBuildSnapshotBuilder::ApplyRuntimeInput(
	const AIControlAdapterMacroBuildSnapshot& base,
	const AIControlAdapterMacroBuildSnapshotRuntimeInput& input) const
{
	AIControlAdapterMacroBuildSnapshot snapshot = base;
	snapshot.expansionSpend = input.expansionSpend;
	snapshot.marketRecoverySpend = input.marketRecoverySpend;
	snapshot.marketGrowthSpend = input.marketGrowthSpend;
	snapshot.palaceSpend = input.palaceSpend;
	snapshot.staticDefenseSpend = input.staticDefenseSpend;
	snapshot.zoneSeedCooldownReady = input.zoneSeedCooldownReady;
	snapshot.staticDefenseZoneIndex = input.staticDefenseZoneIndex;
	snapshot.staticDefenseCommand = input.staticDefenseCommand;
	snapshot.staticDefenseReason = input.staticDefenseReason != nullptr ? input.staticDefenseReason : "no_candidate";
	snapshot.palaceRedundancyZoneIndex = input.palaceRedundancyZoneIndex;
	snapshot.palaceRedundancyReason = input.palaceRedundancyReason != nullptr ? input.palaceRedundancyReason : "no_candidate";
	return snapshot;
}
