#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildManager.h"

#include <algorithm>
#include <cstring>

namespace
{
	int GetZoneSeedInProgress(const AIControlAdapterMacroBuildSnapshot& snapshot, const char* command)
	{
		if (command == nullptr)
		{
			return 0;
		}
		if (std::strcmp(command, "Game.BuildTunnelNetwork") == 0)
		{
			return snapshot.activeZoneTunnelsInProgress;
		}
		if (std::strcmp(command, "Game.BuildStingerSite") == 0)
		{
			return snapshot.activeZoneStingersInProgress;
		}
		if (std::strcmp(command, "Game.BuildBarracksSmart") == 0)
		{
			return snapshot.activeZoneBarracksInProgress;
		}
		if (std::strcmp(command, "Game.BuildArmsDealerSmart") == 0)
		{
			return snapshot.activeZoneArmsDealersInProgress;
		}
		return 0;
	}

	unsigned int GetZoneSeedMinCash(const AIControlAdapterMacroBuildSnapshot& snapshot, const char* command)
	{
		if (command == nullptr)
		{
			return 0u;
		}
		if (std::strcmp(command, "Game.BuildTunnelNetwork") == 0)
		{
			return snapshot.isBalancedSprawl ? 1400u : 900u;
		}
		if (std::strcmp(command, "Game.BuildStingerSite") == 0)
		{
			return snapshot.isBalancedSprawl ? 1800u : 1200u;
		}
		if (std::strcmp(command, "Game.BuildBarracksSmart") == 0)
		{
			return snapshot.isBalancedSprawl ? snapshot.reserveCash : 800u;
		}
		if (std::strcmp(command, "Game.BuildArmsDealerSmart") == 0)
		{
			return snapshot.isBalancedSprawl ? snapshot.reserveCash : 2600u;
		}
		return 0u;
	}
}

void AIControlAdapterMacroBuildManager::AddIntent(
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
	int zoneIndex,
	const char* taskName) const
{
	const char* blockedReason = policyReason != nullptr ? policyReason : "unavailable";
	bool valid = policyAllows && command != nullptr;
	if (valid && minCash > 0u && money < minCash)
	{
		valid = false;
		blockedReason = "wait_money";
	}
	if (valid && maxInProgress > 0 && inProgress >= maxInProgress)
	{
		valid = false;
		blockedReason = "in_progress";
	}
	if (valid && !cooldownReady)
	{
		valid = false;
		blockedReason = "cooldown";
	}
	if (valid && !spend.allowed)
	{
		valid = false;
		blockedReason = spend.reason != nullptr ? spend.reason : "spend_blocked";
	}
	if (valid)
	{
		blockedReason = policyReason != nullptr ? policyReason : "selected";
	}

	AIControlAdapterMacroBuildIntent intent;
	intent.option.category = category != nullptr ? category : "unknown";
	intent.option.command = command;
	intent.option.priority = priority;
	intent.option.valid = valid;
	intent.option.reason = blockedReason;
	intent.preferZone = preferZone;
	intent.executor = executor;
	intent.minCash = minCash;
	intent.inProgress = inProgress;
	intent.maxInProgress = maxInProgress;
	intent.zoneIndex = zoneIndex;
	intent.taskName = taskName;
	plan.intents.push_back(intent);
}

AIControlAdapterMacroBuildPolicyDecisions AIControlAdapterMacroBuildManager::ResolvePolicyDecisions(
	const AIControlAdapterMacroBuildSnapshot& snapshot) const
{
	AIControlAdapterMacroBuildPolicyDecisions decisions;
	const AIControlAdapterRemoteZoneFollowupResult remoteFollowup = AIControlAdapterChooseRemoteZoneFollowup({
		snapshot.remoteZoneHasStash,
		snapshot.totalZoneTunnels,
		snapshot.totalZoneBarracks,
		snapshot.totalZoneArmsDealers,
		snapshot.totalZoneStingers,
		snapshot.allowExpansionBeforeFullRemoteFollowup
	});
	decisions.remoteZoneNeedsFollowup = remoteFollowup.needsFollowup;
	decisions.macroExpansion = AIControlAdapterResolveMacroExpansionDecision({
		snapshot.isSprawlStyle,
		snapshot.isBalancedSprawl,
		snapshot.stashZoneCount,
		snapshot.developedZoneCount,
		snapshot.remoteSupplyZoneCount,
		snapshot.supplyFootprintRadius,
		snapshot.desiredZoneCount,
		snapshot.urgentZoneGapThreshold,
		snapshot.supplyStashesInProgress,
		snapshot.maxConcurrentExpansionStashes,
		remoteFollowup.needsFollowup,
		snapshot.shouldThrottleExtraStashGrowth,
		snapshot.supplyBuildCooldownReady,
		snapshot.money,
		snapshot.reserveCash,
		snapshot.expansionHighCashFloatThreshold
	});
	decisions.expansionDecision = decisions.macroExpansion.arbitration;
	decisions.zoneSeedDecision = AIControlAdapterChooseZoneSeedPackage({
		snapshot.isSprawlStyle,
		snapshot.canScaleMilitaryProduction,
		snapshot.remoteZoneHasStash,
		decisions.macroExpansion.coverageExpansionUrgent,
		snapshot.activeZoneThreatened,
		snapshot.allowExpansionBeforeFullRemoteFollowup,
		snapshot.totalZoneTunnels,
		snapshot.activeZoneTunnelsInProgress,
		snapshot.totalZoneStingers,
		snapshot.activeZoneStingersInProgress,
		snapshot.totalZoneBarracks,
		snapshot.activeZoneBarracksInProgress,
		snapshot.totalZoneArmsDealers,
		snapshot.activeZoneArmsDealersInProgress
	});
	decisions.sprawlDesiredMarketCount =
		std::max<int>(
			snapshot.isBalancedSprawl ? 2 : 1,
			std::min<int>(
				snapshot.sprawlMarketCap,
				std::max<int>(
					snapshot.isBalancedSprawl ? snapshot.totalSupplyStashes : (snapshot.totalSupplyStashes / 2),
					snapshot.isBalancedSprawl
						? ((snapshot.totalBarracks + snapshot.totalArmsDealers + 1) / 2)
						: ((snapshot.totalBarracks + snapshot.totalArmsDealers) / 4))));
	decisions.canAttemptBlackMarketNow = AIControlAdapterCanAttemptBlackMarket({
		snapshot.hasCompletedPalace,
		snapshot.isBalancedSprawl,
		snapshot.money,
		snapshot.reserveCash,
		snapshot.completedBlackMarkets,
		snapshot.inProgressBlackMarkets
	});
	decisions.shouldForceEcoRecovery =
		snapshot.isBalancedSprawl
		&& snapshot.money < snapshot.reserveCash
		&& snapshot.totalSupplyStashes > 0;
	decisions.ecoRecoveryBuild = AIControlAdapterGetEcoRecoveryBuild({
		snapshot.isBalancedSprawl,
		snapshot.totalSupplyStashes,
		snapshot.totalPalaces,
		snapshot.effectiveTotalBlackMarkets,
		decisions.sprawlDesiredMarketCount,
		snapshot.shouldThrottleExtraStashGrowth,
		decisions.canAttemptBlackMarketNow
	});
	decisions.shouldBuildFirstMarket =
		snapshot.isSprawlStyle
		&& snapshot.hasCompletedPalace
		&& snapshot.effectiveTotalBlackMarkets < 1
		&& snapshot.inProgressBlackMarkets < 1
		&& snapshot.blackMarketBuildCooldownReady
		&& decisions.canAttemptBlackMarketNow;
	decisions.shouldPrioritizeMarketGrowth =
		snapshot.isSprawlStyle
		&& snapshot.hasCompletedPalace
		&& snapshot.money >= (snapshot.isBalancedSprawl ? (snapshot.reserveCash + snapshot.blackMarketCost) : snapshot.blackMarketCost)
		&& (!snapshot.isBalancedSprawl || snapshot.inProgressBlackMarkets < 1)
		&& snapshot.effectiveTotalBlackMarkets < decisions.sprawlDesiredMarketCount;
	return decisions;
}

AIControlAdapterMacroBuildPlan AIControlAdapterMacroBuildManager::BuildPlan(
	const AIControlAdapterMacroBuildSnapshot& snapshot) const
{
	const AIControlAdapterMacroBuildPolicyDecisions decisions = ResolvePolicyDecisions(snapshot);
	AIControlAdapterZoneExpansionArbitrationResult expansionDecision = decisions.expansionDecision;
	if (expansionDecision.shouldAttemptExpansion && !snapshot.expansionSpend.allowed)
	{
		expansionDecision.shouldAttemptExpansion = false;
		expansionDecision.command = nullptr;
		expansionDecision.reason = snapshot.expansionSpend.reason;
	}
	const AIControlAdapterZoneSeedPackageDecision& zoneSeedDecision = decisions.zoneSeedDecision;
	const char* ecoRecoveryBuild = decisions.ecoRecoveryBuild;
	const int effectiveExpansionCap = std::max(1, decisions.macroExpansion.maxConcurrentExpansionStashes);

	AIControlAdapterMacroBuildPlan plan;
	plan.telemetry.stashZoneCount = snapshot.stashZoneCount;
	plan.telemetry.developedZoneCount = snapshot.developedZoneCount;
	plan.telemetry.desiredZoneCount = snapshot.desiredZoneCount;
	plan.telemetry.zoneGap = decisions.macroExpansion.zoneGap;
	plan.telemetry.reserveProtected = decisions.macroExpansion.reserveProtected;
	plan.telemetry.cashAboveReserve = decisions.macroExpansion.cashAboveReserve;
	plan.telemetry.supplyFootprintRadius = snapshot.supplyFootprintRadius;
	plan.telemetry.remoteSupplyZoneCount = snapshot.remoteSupplyZoneCount;
	plan.telemetry.desiredRemoteSupplyZones = decisions.macroExpansion.desiredRemoteSupplyZones;
	plan.telemetry.coverageExpansionUrgent = decisions.macroExpansion.coverageExpansionUrgent;
	plan.telemetry.preferRemoteSupplyExpansion = decisions.macroExpansion.preferRemoteSupplyExpansion;
	plan.telemetry.expansionMode = decisions.macroExpansion.expansionMode != nullptr ? decisions.macroExpansion.expansionMode : "hold";
	plan.telemetry.expansionReason = decisions.macroExpansion.expansionReason != nullptr ? decisions.macroExpansion.expansionReason : "target_reached";
	plan.telemetry.expansionCommand = expansionDecision.command;
	plan.telemetry.expansionShouldAttempt = expansionDecision.shouldAttemptExpansion;
	plan.telemetry.expansionDecisionReason = expansionDecision.reason != nullptr ? expansionDecision.reason : "target_reached";
	plan.telemetry.expansionIsUrgent = expansionDecision.isUrgent;
	plan.telemetry.supplyStashesInProgress = snapshot.supplyStashesInProgress;
	plan.telemetry.maxConcurrentExpansionStashes = effectiveExpansionCap;
	plan.telemetry.expansionConcurrencyReason = decisions.macroExpansion.concurrencyReason;
	plan.telemetry.shouldThrottleExtraStashGrowth = snapshot.shouldThrottleExtraStashGrowth;
	plan.telemetry.zoneSeedCommand = zoneSeedDecision.command;
	plan.telemetry.zoneSeedPackageStage = zoneSeedDecision.packageStage != nullptr ? zoneSeedDecision.packageStage : "none";
	plan.telemetry.zoneSeedReason = zoneSeedDecision.reason != nullptr ? zoneSeedDecision.reason : "no_remote_stash";
	plan.telemetry.zoneSeedPriority = zoneSeedDecision.priority;
	plan.telemetry.activeZoneThreatened = snapshot.activeZoneThreatened;

	if (snapshot.isSprawlStyle && snapshot.stashZoneCount < snapshot.desiredZoneCount)
	{
		AddIntent(
			plan,
			"expansion",
			"Game.BuildSupplyStashSmart",
			expansionDecision.isUrgent ? 100 : 60,
			expansionDecision.shouldAttemptExpansion,
			expansionDecision.reason,
			0u,
			snapshot.supplyStashesInProgress,
			effectiveExpansionCap,
			snapshot.supplyBuildCooldownReady,
			{ true, "allowed" },
			false,
			AIControlAdapterMacroBuildExecutor::SupplyExpansion,
			snapshot.money);
	}

	if (snapshot.isSprawlStyle && zoneSeedDecision.command != nullptr)
	{
		const char* seedCommand = zoneSeedDecision.command;
		const unsigned int seedMinCash = GetZoneSeedMinCash(snapshot, seedCommand);
		AddIntent(
			plan,
			"zone_seed",
			seedCommand,
			zoneSeedDecision.priority,
			seedMinCash > 0u,
			zoneSeedDecision.reason,
			seedMinCash,
			GetZoneSeedInProgress(snapshot, seedCommand),
			1,
			snapshot.zoneSeedCooldownReady,
			{ true, "allowed" },
			zoneSeedDecision.preferZone,
			AIControlAdapterMacroBuildExecutor::MacroBuild,
			snapshot.money);
	}

	if (decisions.shouldForceEcoRecovery && ecoRecoveryBuild != nullptr)
	{
		if (std::strcmp(ecoRecoveryBuild, "Game.BuildBlackMarketSmart") == 0)
		{
			AddIntent(
				plan,
				"eco_recovery",
				ecoRecoveryBuild,
				85,
				decisions.canAttemptBlackMarketNow,
				"eco_recovery_market",
				snapshot.blackMarketCost,
				snapshot.inProgressBlackMarkets,
				1,
				snapshot.blackMarketBuildCooldownReady,
				snapshot.marketRecoverySpend,
				snapshot.hasActiveZone,
				AIControlAdapterMacroBuildExecutor::MacroBuild,
				snapshot.money);
		}
		else if (std::strcmp(ecoRecoveryBuild, "Game.BuildSupplyStashSmart") == 0)
		{
			AddIntent(
				plan,
				"eco_recovery",
				ecoRecoveryBuild,
				85,
				true,
				"eco_recovery_supply",
				1800u,
				snapshot.supplyStashesInProgress,
				effectiveExpansionCap,
				snapshot.supplyBuildCooldownReady,
				snapshot.expansionSpend,
				false,
				AIControlAdapterMacroBuildExecutor::SupplyExpansion,
				snapshot.money);
		}
	}

	if (decisions.shouldBuildFirstMarket)
	{
		AddIntent(
			plan,
			"market_recovery",
			"Game.BuildBlackMarketSmart",
			55,
			true,
			"first_market",
			snapshot.blackMarketCost,
			snapshot.inProgressBlackMarkets,
			1,
			snapshot.blackMarketBuildCooldownReady,
			snapshot.marketRecoverySpend,
			false,
			AIControlAdapterMacroBuildExecutor::MacroBuild,
			snapshot.money);
	}

	if (snapshot.isSprawlStyle && snapshot.effectiveTotalBlackMarkets < 1)
	{
		AddIntent(
			plan,
			"market_recovery",
			"Game.BuildBlackMarketSmart",
			55,
			decisions.canAttemptBlackMarketNow,
			"first_sprawl_market",
			snapshot.blackMarketCost,
			snapshot.inProgressBlackMarkets,
			1,
			snapshot.blackMarketBuildCooldownReady,
			snapshot.marketRecoverySpend,
			false,
			AIControlAdapterMacroBuildExecutor::MacroBuild,
			snapshot.money);
	}

	if (decisions.shouldPrioritizeMarketGrowth && !decisions.macroExpansion.zoneExpansionUrgent)
	{
		AddIntent(
			plan,
			"market_growth",
			"Game.BuildBlackMarketSmart",
			40,
			true,
			"market_growth",
			snapshot.blackMarketCost,
			snapshot.inProgressBlackMarkets,
			1,
			snapshot.blackMarketBuildCooldownReady,
			snapshot.marketGrowthSpend,
			false,
			AIControlAdapterMacroBuildExecutor::MacroBuild,
			snapshot.money);
	}

	if (snapshot.staticDefenseZoneIndex >= 0 && snapshot.staticDefenseCommand != nullptr)
	{
		AddIntent(
			plan,
			"static_defense",
			snapshot.staticDefenseCommand,
			35,
			true,
			snapshot.staticDefenseReason,
			0u,
			0,
			0,
			true,
			snapshot.staticDefenseSpend,
			true,
			AIControlAdapterMacroBuildExecutor::SpecificZone,
			snapshot.money,
			snapshot.staticDefenseZoneIndex,
			"auto_static_defense");
	}

	if (snapshot.palaceRedundancyZoneIndex >= 0)
	{
		AddIntent(
			plan,
			"palace_redundancy",
			"Game.BuildPalaceSmart",
			34,
			true,
			snapshot.palaceRedundancyReason,
			5000u,
			snapshot.palacesInProgress,
			1,
			snapshot.palaceBuildCooldownReady,
			snapshot.palaceSpend,
			true,
			AIControlAdapterMacroBuildExecutor::SpecificZone,
			snapshot.money,
			snapshot.palaceRedundancyZoneIndex,
			"auto_palace_redundancy");
	}

	if (!snapshot.shouldPreserveReserve && snapshot.canScaleMilitaryProduction && snapshot.isSprawlStyle)
	{
		AddIntent(
			plan,
			"producer_growth",
			"Game.BuildBarracksSmart",
			30,
			snapshot.totalBarracks < snapshot.effectiveBarracksCap && snapshot.balancedZoneCanAddBarracks,
			"producer_barracks_growth",
			snapshot.isBalancedSprawl ? snapshot.reserveCash : 800u,
			snapshot.barracksInProgress,
			1,
			snapshot.barracksBuildCooldownReady,
			{ true, "allowed" },
			true,
			AIControlAdapterMacroBuildExecutor::MacroBuild,
			snapshot.money);
		AddIntent(
			plan,
			"producer_growth",
			"Game.BuildArmsDealerSmart",
			29,
			snapshot.totalArmsDealers < snapshot.effectiveArmsCap && snapshot.balancedZoneCanAddArmsDealer,
			"producer_arms_growth",
			snapshot.isBalancedSprawl ? snapshot.reserveCash : 2600u,
			snapshot.armsDealersInProgress,
			1,
			snapshot.armsDealerBuildCooldownReady,
			{ true, "allowed" },
			true,
			AIControlAdapterMacroBuildExecutor::MacroBuild,
			snapshot.money);
	}

	if (snapshot.hasCompletedPalace && snapshot.effectiveTotalBlackMarkets < (snapshot.wantsBaselineTwoMarkets ? 2 : 1))
	{
		AddIntent(
			plan,
			"market_recovery",
			"Game.BuildBlackMarketSmart",
			28,
			decisions.canAttemptBlackMarketNow,
			"baseline_market",
			snapshot.blackMarketCost,
			snapshot.inProgressBlackMarkets,
			1,
			snapshot.blackMarketBuildCooldownReady,
			snapshot.marketRecoverySpend,
			false,
			AIControlAdapterMacroBuildExecutor::MacroBuild,
			snapshot.money);
	}

	if (snapshot.isSprawlStyle && snapshot.effectiveTotalBlackMarkets < snapshot.sprawlMarketCap)
	{
		AddIntent(
			plan,
			"market_growth",
			"Game.BuildBlackMarketSmart",
			27,
			decisions.canAttemptBlackMarketNow,
			"sprawl_market_cap_growth",
			snapshot.blackMarketCost,
			snapshot.inProgressBlackMarkets,
			1,
			snapshot.blackMarketBuildCooldownReady,
			snapshot.marketGrowthSpend,
			true,
			AIControlAdapterMacroBuildExecutor::MacroBuild,
			snapshot.money);
	}

	if (!snapshot.shouldPreserveReserve && snapshot.isSprawlStyle)
	{
		AddIntent(
			plan,
			"static_growth",
			"Game.BuildTunnelNetwork",
			25,
			snapshot.totalTunnels < snapshot.sprawlTunnelCap,
			"global_tunnel_growth",
			snapshot.isBalancedSprawl ? 1400u : 900u,
			snapshot.tunnelsInProgress,
			1,
			snapshot.tunnelBuildCooldownReady,
			{ true, "allowed" },
			true,
			AIControlAdapterMacroBuildExecutor::MacroBuild,
			snapshot.money);
		AddIntent(
			plan,
			"static_growth",
			"Game.BuildStingerSite",
			24,
			snapshot.totalStingers < snapshot.sprawlStingerCap,
			"global_stinger_growth",
			snapshot.isBalancedSprawl ? 1800u : 1200u,
			snapshot.stingersInProgress,
			1,
			snapshot.stingerBuildCooldownReady,
			{ true, "allowed" },
			true,
			AIControlAdapterMacroBuildExecutor::MacroBuild,
			snapshot.money);
	}

	if ((!snapshot.shouldPreserveReserve || decisions.macroExpansion.allowUrgentExpansionDespiteReserve)
		&& snapshot.isSprawlStyle
		&& !snapshot.shouldThrottleExtraStashGrowth)
	{
		AddIntent(
			plan,
			"supply_growth",
			"Game.BuildSupplyStashSmart",
			20,
			snapshot.totalSupplyStashes < snapshot.sprawlSupplyCap,
			"sprawl_supply_cap_growth",
			snapshot.isBalancedSprawl ? 2200u : 1800u,
			snapshot.supplyStashesInProgress,
			effectiveExpansionCap,
			snapshot.supplyBuildCooldownReady,
			{ true, "allowed" },
			false,
			AIControlAdapterMacroBuildExecutor::SupplyExpansion,
			snapshot.money);
	}

	std::vector<AIControlAdapterMacroBuildIntentOption> options;
	options.reserve(plan.intents.size());
	for (const AIControlAdapterMacroBuildIntent& intent : plan.intents)
	{
		options.push_back(intent.option);
	}
	plan.choice = AIControlAdapterChooseMacroBuildIntent(options);
	return plan;
}

AIControlAdapterNonSupplyFootholdDecision AIControlAdapterMacroBuildManager::EvaluateNonSupplyFoothold(
	const AIControlAdapterNonSupplyFootholdInput& input) const
{
	AIControlAdapterNonSupplyFootholdDecision decision;
	decision.supplyExpansionBlocked =
		input.previousReason != nullptr
		&& (std::strcmp(input.previousReason, "no_legal_build_location") == 0
			|| std::strcmp(input.previousReason, "placement_failed") == 0
			|| std::strcmp(input.previousReason, "no_worker") == 0);
	decision.economyIsStrong =
		input.hasCompletedPalace
		&& input.completedBlackMarkets >= 4
		&& input.money >= 3000u;
	decision.durableIncome = input.completedBlackMarkets * 200;
	decision.hasStrongIncome = decision.durableIncome >= 800;
	decision.anchorType = decision.hasStrongIncome
		? ZoneAnchorType::MarketFoothold
		: ZoneAnchorType::StrategicFoothold;

	if (input.alreadyIssued)
	{
		decision.reason = "already_issued";
		return decision;
	}
	if (!input.isSprawlStyle || input.currentZones >= input.desiredZones)
	{
		decision.reason = "target_reached";
		return decision;
	}
	if (!input.allowUrgentExpansionDespiteReserve)
	{
		decision.reason = "reserve_protected";
		return decision;
	}
	if (!decision.economyIsStrong)
	{
		decision.reason = "economy_not_ready";
		return decision;
	}
	if (!decision.supplyExpansionBlocked && !decision.hasStrongIncome)
	{
		decision.reason = "supply_expansion_not_blocked";
		return decision;
	}

	decision.shouldEvaluatePlacement = true;
	if (input.totalTunnels >= input.sprawlTunnelCap)
	{
		decision.reason = "tunnel_cap_reached";
		return decision;
	}
	if (input.tunnelsInProgress >= 1)
	{
		decision.reason = "tunnel_in_progress";
		return decision;
	}
	if (!input.tunnelBuildReady)
	{
		decision.reason = "tunnel_cooldown";
		return decision;
	}
	if (input.money < 900u)
	{
		decision.reason = "wait_money";
		return decision;
	}

	decision.shouldAttemptTunnel = true;
	decision.reason = "non_supply_expansion";
	return decision;
}

AIControlAdapterOpeningBuildDecision AIControlAdapterMacroBuildManager::EvaluateOpeningBuild(
	const AIControlAdapterOpeningBuildInput& input) const
{
	AIControlAdapterOpeningBuildDecision decision;
	const char* required = input.requiredOpeningBuild;
	if (required == nullptr)
	{
		if (!input.wantsSecondSupply || input.totalSupplyStashes >= 2)
		{
			return decision;
		}
		decision.handled = true;
		decision.displayCommand = "Game.BuildSupplyStashSmart";
		decision.executor = AIControlAdapterMacroBuildExecutor::SupplyExpansion;
		if (input.supplyStashesInProgress < 1 && input.supplyBuildReady && input.money >= 1600u)
		{
			decision.command = "Game.BuildSupplyStashSmart";
		}
		return decision;
	}

	decision.handled = true;
	decision.displayCommand = required;
	if (std::strcmp(required, "Game.BuildSupplyStashSmart") == 0)
	{
		decision.executor = AIControlAdapterMacroBuildExecutor::SupplyExpansion;
		if (input.supplyStashesInProgress > 0)
		{
			if (input.completedBarracks < 1
				&& input.barracksInProgress < 1
				&& input.barracksBuildReady
				&& input.money >= 600u)
			{
				decision.displayCommand = "Game.BuildBarracksSmart";
				decision.command = "Game.BuildBarracksSmart";
				decision.executor = AIControlAdapterMacroBuildExecutor::MacroBuild;
			}
			else
			{
				decision.reason = "opening_wait_supply_stash";
			}
			return decision;
		}
		if (input.supplyBuildReady && input.money >= 1200u)
		{
			decision.command = "Game.BuildSupplyStashSmart";
			decision.allowBarracksFallbackAfterSupplyFailure = true;
		}
		else
		{
			decision.reason = input.money < 1200u ? "opening_wait_money" : "opening_wait_supply_stash";
		}
		return decision;
	}

	if (std::strcmp(required, "Game.BuildBarracksSmart") == 0)
	{
		if (input.barracksInProgress > 0)
		{
			decision.reason = "opening_wait_barracks";
		}
		else if (input.barracksBuildReady && input.money >= 600u)
		{
			decision.command = "Game.BuildBarracksSmart";
		}
		else
		{
			decision.reason = input.money < 600u ? "opening_wait_money" : "opening_wait_barracks";
		}
		return decision;
	}

	if (std::strcmp(required, "Game.BuildArmsDealerSmart") == 0)
	{
		if (input.armsDealersInProgress > 0)
		{
			decision.reason = "opening_wait_arms_dealer";
		}
		else if (input.armsDealerBuildReady && input.money >= 2500u)
		{
			decision.command = "Game.BuildArmsDealerSmart";
		}
		else
		{
			decision.reason = input.money < 2500u ? "opening_wait_money" : "opening_wait_arms_dealer";
		}
	}
	return decision;
}

const char* AIControlAdapterMacroBuildManager::EvaluateMacroHoldReason(
	const AIControlAdapterMacroHoldReasonInput& input) const
{
	if (input.shouldPreserveReserve && !input.allowUrgentExpansionDespiteReserve)
	{
		return "macro_hold_reserve";
	}
	if (input.allowUrgentExpansionDespiteReserve && input.stashZoneCount < input.desiredZoneCount)
	{
		if (input.supplyStashesInProgress > 0)
		{
			return "macro_wait_expansion_in_progress";
		}
		if (input.tunnelsInProgress > 0)
		{
			return "macro_wait_non_supply_anchor_build_in_progress";
		}
		if (input.shouldThrottleExtraStashGrowth)
		{
			return "macro_wait_expansion_throttle";
		}
		if (!input.hasCompletedPalace || input.completedBlackMarkets < 4)
		{
			return "macro_wait_non_supply_anchor_economy";
		}
		if (input.totalTunnels >= input.sprawlTunnelCap)
		{
			return "macro_wait_non_supply_anchor_unavailable";
		}
		return "macro_wait_expansion_placement";
	}
	if (input.shouldForceEcoRecovery)
	{
		return "macro_wait_eco_recovery";
	}
	if (input.effectiveTotalBlackMarkets < input.sprawlDesiredMarketCount && !input.canAttemptBlackMarketNow)
	{
		return "macro_wait_market_cash";
	}
	if (input.remoteZoneNeedsFollowup)
	{
		return "macro_wait_zone_followup";
	}
	if (input.stashZoneCount < input.desiredZoneCount)
	{
		return "macro_wait_zone_expansion";
	}
	return "macro_no_priority";
}

AIControlAdapterStaticDefenseCandidateDecision AIControlAdapterMacroBuildManager::EvaluateStaticDefenseCandidate(
	const AIControlAdapterStaticDefenseCandidateInput& input) const
{
	AIControlAdapterStaticDefenseCandidateDecision decision;
	if (input.policy == nullptr)
	{
		decision.reason = "not_evaluated";
		return decision;
	}
	if (input.zoneExpansionUrgent)
	{
		decision.reason = "urgent_expansion_priority";
		return decision;
	}
	if (input.shouldPreserveReserve)
	{
		decision.reason = "reserve_protected";
		return decision;
	}

	const unsigned int stingerMinCash = input.isBalancedSprawl ? 1800u : 1200u;
	const unsigned int tunnelMinCash = input.isBalancedSprawl ? 1400u : 900u;
	if (input.policy->shouldBuildStinger)
	{
		if (input.money < stingerMinCash)
		{
			decision.reason = "wait_money";
			return decision;
		}
		if (!input.stingerBuildReady)
		{
			decision.reason = "cooldown";
			return decision;
		}
		decision.shouldBuild = true;
		decision.command = "Game.BuildStingerSite";
		decision.reason = input.policy->reason;
		return decision;
	}
	if (input.policy->shouldBuildTunnel)
	{
		if (input.money < tunnelMinCash)
		{
			decision.reason = "wait_money";
			return decision;
		}
		if (!input.tunnelBuildReady)
		{
			decision.reason = "cooldown";
			return decision;
		}
		decision.shouldBuild = true;
		decision.command = "Game.BuildTunnelNetwork";
		decision.reason = input.policy->reason;
		return decision;
	}
	decision.reason = input.policy->reason != nullptr ? input.policy->reason : "target_met";
	return decision;
}

AIControlAdapterPalaceRedundancyCandidateDecision AIControlAdapterMacroBuildManager::EvaluatePalaceRedundancyCandidate(
	const AIControlAdapterPalaceRedundancyCandidateInput& input) const
{
	AIControlAdapterPalaceRedundancyCandidateDecision decision;
	if (input.policy == nullptr)
	{
		decision.reason = "not_evaluated";
		return decision;
	}
	if (!input.policy->shouldBuild)
	{
		decision.reason = input.policy->reason != nullptr ? input.policy->reason : "not_needed";
		return decision;
	}
	if (!input.palaceBuildReady)
	{
		decision.reason = "cooldown";
		return decision;
	}
	decision.shouldBuild = true;
	decision.reason = input.policy->reason;
	return decision;
}

AIControlAdapterPalaceRecoveryCandidateDecision AIControlAdapterMacroBuildManager::EvaluatePalaceRecoveryCandidate(
	const AIControlAdapterPalaceRecoveryCandidateInput& input) const
{
	AIControlAdapterPalaceRecoveryCandidateDecision decision;
	if (input.policy == nullptr)
	{
		decision.reason = "not_evaluated";
		return decision;
	}
	if (!input.policy->shouldBuild)
	{
		decision.reason = input.policy->reason != nullptr ? input.policy->reason : "not_needed";
		return decision;
	}
	if (!input.openingInfrastructureReady)
	{
		decision.reason = "placement_unavailable";
		return decision;
	}
	decision.shouldAttempt = true;
	decision.command = "Game.BuildPalaceSmart";
	decision.reason = input.policy->reason;
	return decision;
}

AIControlAdapterPalaceRecoveryResultDecision AIControlAdapterMacroBuildManager::EvaluatePalaceRecoveryResult(
	const AIControlAdapterPalaceRecoveryResultInput& input) const
{
	AIControlAdapterPalaceRecoveryResultDecision decision;
	if (input.policy == nullptr)
	{
		decision.reason = "not_evaluated";
		return decision;
	}
	decision.issued = input.commandSelected && input.issued;
	decision.reason = input.policy->reason != nullptr ? input.policy->reason : "not_needed";
	if (input.policy->shouldBuild && !input.openingInfrastructureReady)
	{
		decision.reason = "placement_unavailable";
	}
	else if (input.policy->shouldBuild && input.commandSelected && !input.issued)
	{
		decision.reason = "placement_unavailable";
	}
	return decision;
}
