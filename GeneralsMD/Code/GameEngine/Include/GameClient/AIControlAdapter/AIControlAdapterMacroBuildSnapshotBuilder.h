/**
 * AIControlAdapterMacroBuildSnapshotBuilder.h
 *
 * Converts raw autonomy counts and resolved profile policy into the canonical
 * macro build snapshot consumed by AIControlAdapterMacroBuildManager.
 */

#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

struct AIControlAdapterMacroBuildSnapshotBuilderInput
{
	AIControlAdapterProfilePolicyConfig policyConfig;
	unsigned int money = 0u;
	unsigned int blackMarketCost = 2500u;
	bool hasCompletedPalace = false;
	bool hasActiveZone = false;
	bool activeZoneIsMainBase = false;
	bool activeZoneThreatened = false;

	int stashZoneCount = 0;
	int developedZoneCount = 0;
	float supplyFootprintRadius = 0.0f;
	int remoteSupplyZoneCount = 0;

	int totalSupplyStashes = 0;
	int totalBarracks = 0;
	int totalArmsDealers = 0;
	int totalPalaces = 0;
	int totalTunnels = 0;
	int totalStingers = 0;
	int completedBlackMarkets = 0;
	int inProgressBlackMarkets = 0;
	int effectiveTotalBlackMarkets = 0;

	int supplyStashesInProgress = 0;
	int barracksInProgress = 0;
	int armsDealersInProgress = 0;
	int palacesInProgress = 0;
	int tunnelsInProgress = 0;
	int stingersInProgress = 0;

	int totalZoneSupplyStashes = 0;
	int totalZoneBarracks = 0;
	int totalZoneArmsDealers = 0;
	int totalZoneTunnels = 0;
	int totalZoneStingers = 0;
	int activeZoneTunnelsInProgress = 0;
	int activeZoneStingersInProgress = 0;
	int activeZoneBarracksInProgress = 0;
	int activeZoneArmsDealersInProgress = 0;

	bool supplyBuildCooldownReady = false;
	bool blackMarketBuildCooldownReady = false;
	bool barracksBuildCooldownReady = false;
	bool armsDealerBuildCooldownReady = false;
	bool tunnelBuildCooldownReady = false;
	bool stingerBuildCooldownReady = false;
	bool palaceBuildCooldownReady = false;

	bool wantsBaselineTwoMarkets = false;
	AIControlAdapterMacroBuildSpendGate expansionSpend;
	AIControlAdapterMacroBuildSpendGate marketRecoverySpend;
	AIControlAdapterMacroBuildSpendGate marketGrowthSpend;
	AIControlAdapterMacroBuildSpendGate palaceSpend;
	AIControlAdapterMacroBuildSpendGate staticDefenseSpend;
	bool zoneSeedCooldownReady = false;
};

struct AIControlAdapterMacroBuildSnapshotRuntimeInput
{
	AIControlAdapterMacroBuildSpendGate expansionSpend;
	AIControlAdapterMacroBuildSpendGate marketRecoverySpend;
	AIControlAdapterMacroBuildSpendGate marketGrowthSpend;
	AIControlAdapterMacroBuildSpendGate palaceSpend;
	AIControlAdapterMacroBuildSpendGate staticDefenseSpend;
	bool zoneSeedCooldownReady = false;
	int staticDefenseZoneIndex = -1;
	const char* staticDefenseCommand = nullptr;
	const char* staticDefenseReason = "no_candidate";
	int palaceRedundancyZoneIndex = -1;
	const char* palaceRedundancyReason = "no_candidate";
};

class AIControlAdapterMacroBuildSnapshotBuilder
{
public:
	AIControlAdapterMacroBuildSnapshot Build(const AIControlAdapterMacroBuildSnapshotBuilderInput& input) const;
	AIControlAdapterMacroBuildSnapshot ApplyRuntimeInput(
		const AIControlAdapterMacroBuildSnapshot& base,
		const AIControlAdapterMacroBuildSnapshotRuntimeInput& input) const;
};
