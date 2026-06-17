#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildTelemetrySerializer.h"

#include <cstdarg>
#include <cstdio>

namespace
{
	const char* safeString(const char* value, const char* fallback)
	{
		return value != nullptr && value[0] != '\0' ? value : fallback;
	}

	const char* executorName(AIControlAdapterMacroBuildExecutor executor)
	{
		switch (executor)
		{
		case AIControlAdapterMacroBuildExecutor::SpecificZone:
			return "specific_zone";
		case AIControlAdapterMacroBuildExecutor::SupplyExpansion:
			return "supply_expansion";
		case AIControlAdapterMacroBuildExecutor::MacroBuild:
		default:
			return "macro_build";
		}
	}

	nlohmann::json buildResultDefault(const char* reason)
	{
		return nlohmann::json::object({
			{"command", "none"},
			{"issued", false},
			{"reason", reason}
		});
	}

	std::string formatLine(const char* format, ...)
	{
		char buffer[1024] = {};
		va_list args;
		va_start(args, format);
		std::vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		return std::string(buffer);
	}
}

nlohmann::json AIControlAdapterMacroBuildTelemetrySerializer::BuildDefaultTelemetry() const
{
	return nlohmann::json::object({
		{"profile", "none"},
		{"money", 0},
		{"reserve", 0},
		{"cash_float", 0},
		{"current_zones", 0},
		{"developed_zones", 0},
		{"desired_zones", 0},
		{"zone_gap", 0},
		{"active_zone_anchor", 0},
		{"active_zone_threatened", false},
		{"remote_zone_needs_followup", false},
		{"expansion", nlohmann::json::object({
			{"mode", "hold"},
			{"reason", "target_reached"},
			{"command", "none"},
			{"should_attempt", false},
			{"decision_reason", "target_reached"},
			{"urgent", false},
			{"coverage_urgent", false},
			{"prefer_remote", false},
			{"remote_stage", "local_bootstrap"},
			{"remote_min_distance", 0.0f},
			{"remote_max_distance", 0.0f},
			{"remote_allowed", false},
			{"remote_reason", "local_bootstrap"},
			{"remote_supply", 0},
			{"desired_remote_supply", 0},
			{"footprint", 0.0f}
		})},
		{"zone_seed", nlohmann::json::object({
			{"command", "none"},
			{"package_stage", "none"},
			{"reason", "no_remote_stash"},
			{"priority", 0}
		})},
		{"spend", nlohmann::json::object()},
		{"intents", nlohmann::json::array()},
		{"selected", nlohmann::json::object({
			{"index", -1},
			{"category", "none"},
			{"command", "none"},
			{"priority", 0},
			{"reason", "not_evaluated"}
		})},
		{"result", buildResultDefault("not_evaluated")}
	});
}

nlohmann::json AIControlAdapterMacroBuildTelemetrySerializer::BuildTelemetry(
	const AIControlAdapterMacroBuildTelemetryInput& input) const
{
	nlohmann::json telemetry = BuildDefaultTelemetry();
	telemetry["profile"] = input.profile;
	telemetry["money"] = input.money;
	telemetry["reserve"] = input.reserveCash;
	telemetry["cash_float"] = input.cashAboveReserve;
	telemetry["current_zones"] = input.currentZones;
	telemetry["developed_zones"] = input.developedZones;
	telemetry["desired_zones"] = input.desiredZones;
	telemetry["zone_gap"] = input.zoneGap;
	telemetry["active_zone_anchor"] = input.activeZoneAnchor;
	telemetry["active_zone_threatened"] = input.activeZoneThreatened;
	telemetry["remote_zone_needs_followup"] = input.remoteZoneNeedsFollowup;
	telemetry["result"] = buildResultDefault("not_attempted");

	if (input.macroBuildPlan != nullptr)
	{
		const AIControlAdapterMacroBuildTelemetry& macroTelemetry = input.macroBuildPlan->telemetry;
		telemetry["expansion"] = nlohmann::json::object({
			{"mode", safeString(macroTelemetry.expansionMode, "hold")},
			{"reason", safeString(macroTelemetry.expansionReason, "target_reached")},
			{"command", safeString(macroTelemetry.expansionCommand, "none")},
			{"should_attempt", macroTelemetry.expansionShouldAttempt},
			{"decision_reason", safeString(macroTelemetry.expansionDecisionReason, "target_reached")},
			{"urgent", macroTelemetry.expansionIsUrgent},
			{"max_in_progress", macroTelemetry.maxConcurrentExpansionStashes},
			{"concurrency_reason", safeString(macroTelemetry.expansionConcurrencyReason, "profile_cap")},
			{"coverage_urgent", macroTelemetry.coverageExpansionUrgent},
			{"prefer_remote", macroTelemetry.preferRemoteSupplyExpansion},
			{"remote_stage", safeString(macroTelemetry.remoteSupplyStage, "local_bootstrap")},
			{"remote_min_distance", macroTelemetry.remoteSupplyMinDistance},
			{"remote_max_distance", macroTelemetry.remoteSupplyMaxDistance},
			{"remote_allowed", macroTelemetry.remoteSupplyAllowed},
			{"remote_reason", safeString(macroTelemetry.remoteSupplyReason, "local_bootstrap")},
			{"remote_supply", macroTelemetry.remoteSupplyZoneCount},
			{"desired_remote_supply", macroTelemetry.desiredRemoteSupplyZones},
			{"footprint", macroTelemetry.supplyFootprintRadius}
		});
		telemetry["zone_seed"] = nlohmann::json::object({
			{"command", safeString(macroTelemetry.zoneSeedCommand, "none")},
			{"package_stage", safeString(macroTelemetry.zoneSeedPackageStage, "none")},
			{"reason", safeString(macroTelemetry.zoneSeedReason, "no_remote_stash")},
			{"priority", macroTelemetry.zoneSeedPriority}
		});

		nlohmann::json intents = nlohmann::json::array();
		for (const AIControlAdapterMacroBuildIntent& intent : input.macroBuildPlan->intents)
		{
			intents.push_back(nlohmann::json::object({
				{"category", safeString(intent.option.category, "unknown")},
				{"command", safeString(intent.option.command, "none")},
				{"priority", intent.option.priority},
				{"valid", intent.option.valid},
				{"reason", safeString(intent.option.reason, "unknown")},
				{"prefer_zone", intent.preferZone},
				{"min_cash", intent.minCash},
				{"in_progress", intent.inProgress},
				{"max_in_progress", intent.maxInProgress},
				{"zone_index", intent.zoneIndex},
				{"executor", executorName(intent.executor)}
			}));
		}
		telemetry["intents"] = intents;
		telemetry["selected"] = nlohmann::json::object({
			{"index", input.macroBuildPlan->choice.index},
			{"category", safeString(input.macroBuildPlan->choice.category, "none")},
			{"command", safeString(input.macroBuildPlan->choice.command, "none")},
			{"priority", input.macroBuildPlan->choice.priority},
			{"reason", safeString(input.macroBuildPlan->choice.reason, "no_valid_intent")}
		});
	}

	if (input.spendPlan != nullptr)
	{
		nlohmann::json spendTelemetry = nlohmann::json::object();
		for (const AIControlAdapterStrategicSpendRecord& spendRecord : input.spendPlan->records)
		{
			const AIControlAdapterStrategicSpendDecision& decision = spendRecord.decision;
			spendTelemetry[AIControlAdapterStrategicSpendCategoryName(spendRecord.category)] = nlohmann::json::object({
				{"allowed", decision.allowed},
				{"request_cost", spendRecord.requestCost},
				{"protected_cash", decision.protectedCash},
				{"spend_budget", decision.spendBudget},
				{"batch_limit", decision.batchLimit},
				{"reason", decision.reason}
			});
		}
		telemetry["spend"] = spendTelemetry;
	}

	return telemetry;
}

std::vector<std::string> AIControlAdapterMacroBuildTelemetrySerializer::BuildExpansionLogLines(
	const AIControlAdapterMacroBuildExpansionLogInput& input) const
{
	std::vector<std::string> lines;
	if (input.telemetry == nullptr)
	{
		return lines;
	}

	const AIControlAdapterMacroBuildTelemetry& telemetry = *input.telemetry;
	lines.push_back(formatLine(
		"zone_expansion_policy mode=%s current=%d developed=%d desired=%d gap=%d reserve_protected=%d cash_float=%lu footprint=%.1f remote_supply=%d desired_remote=%d coverage_urgent=%d reason=%s",
		safeString(telemetry.expansionMode, "hold"),
		telemetry.stashZoneCount,
		telemetry.developedZoneCount,
		telemetry.desiredZoneCount,
		telemetry.zoneGap,
		telemetry.reserveProtected ? 1 : 0,
		static_cast<unsigned long>(telemetry.cashAboveReserve),
		telemetry.supplyFootprintRadius,
		telemetry.remoteSupplyZoneCount,
		telemetry.desiredRemoteSupplyZones,
		telemetry.coverageExpansionUrgent ? 1 : 0,
		safeString(telemetry.expansionReason, "target_reached")));
	lines.push_back(formatLine(
		"zone_expansion_request command=%s issued=%d reason=%s current=%d developed=%d desired=%d gap=%d cash_above_reserve=%lu in_progress=%d max_in_progress=%d concurrency_reason=%s throttled=%d is_urgent=%d",
		safeString(telemetry.expansionCommand, "none"),
		telemetry.expansionShouldAttempt ? 1 : 0,
		safeString(telemetry.expansionDecisionReason, "target_reached"),
		telemetry.stashZoneCount,
		telemetry.developedZoneCount,
		telemetry.desiredZoneCount,
		telemetry.zoneGap,
		static_cast<unsigned long>(telemetry.cashAboveReserve),
		telemetry.supplyStashesInProgress,
		telemetry.maxConcurrentExpansionStashes,
		safeString(telemetry.expansionConcurrencyReason, "profile_cap"),
		telemetry.shouldThrottleExtraStashGrowth ? 1 : 0,
		telemetry.expansionIsUrgent ? 1 : 0));
	lines.push_back(formatLine(
		"sprawl_expansion_throughput current=%d desired=%d gap=%d in_progress=%d max_in_progress=%d concurrency_reason=%s reserve=%lu cash_float=%lu footprint=%.1f remote_supply=%d desired_remote=%d prefer_remote=%d remote_stage=%s remote_allowed=%d remote_min=%.1f remote_max=%.1f action=%s reason=%s",
		telemetry.stashZoneCount,
		telemetry.desiredZoneCount,
		telemetry.zoneGap,
		telemetry.supplyStashesInProgress,
		telemetry.maxConcurrentExpansionStashes,
		safeString(telemetry.expansionConcurrencyReason, "profile_cap"),
		static_cast<unsigned long>(input.reserveCash),
		static_cast<unsigned long>(telemetry.cashAboveReserve),
		telemetry.supplyFootprintRadius,
		telemetry.remoteSupplyZoneCount,
		telemetry.desiredRemoteSupplyZones,
		telemetry.preferRemoteSupplyExpansion ? 1 : 0,
		safeString(telemetry.remoteSupplyStage, "local_bootstrap"),
		telemetry.remoteSupplyAllowed ? 1 : 0,
		telemetry.remoteSupplyMinDistance,
		telemetry.remoteSupplyMaxDistance,
		safeString(telemetry.expansionCommand, "none"),
		safeString(telemetry.expansionDecisionReason, "target_reached")));

	if (input.logZoneSeed)
	{
		lines.push_back(formatLine(
			"sprawl_zone_seed zone=%u command=%s issued=0 package_stage=%s reason=%s coverage_urgent=%d threatened=%d priority=%d",
			input.activeZoneAnchor,
			safeString(telemetry.zoneSeedCommand, "none"),
			safeString(telemetry.zoneSeedPackageStage, "none"),
			safeString(telemetry.zoneSeedReason, "no_remote_stash"),
			telemetry.coverageExpansionUrgent ? 1 : 0,
			telemetry.activeZoneThreatened ? 1 : 0,
			telemetry.zoneSeedPriority));
	}

	return lines;
}

nlohmann::json AIControlAdapterMacroBuildTelemetrySerializer::BuildStaticDefensePolicyTelemetry(
	const AIControlAdapterStaticDefensePolicyTelemetryInput& input) const
{
	if (input.policy == nullptr)
	{
		return nlohmann::json::object({
			{"zone", input.zone},
			{"role", "unknown"},
			{"tunnels", 0},
			{"desired_tunnels", 0},
			{"stingers", 0},
			{"desired_stingers", 0},
			{"in_progress", input.inProgress},
			{"reason", "not_evaluated"}
		});
	}

	const AIControlAdapterStaticDefensePolicyResult& policy = *input.policy;
	return nlohmann::json::object({
		{"zone", input.zone},
		{"role", safeString(policy.role, "unknown")},
		{"tunnels", policy.effectiveTunnels},
		{"desired_tunnels", policy.desiredTunnels},
		{"stingers", policy.effectiveStingers},
		{"desired_stingers", policy.desiredStingers},
		{"in_progress", input.inProgress},
		{"reason", safeString(policy.reason, "unknown")}
	});
}

std::string AIControlAdapterMacroBuildTelemetrySerializer::BuildStaticDefensePolicyLogLine(
	const AIControlAdapterStaticDefensePolicyTelemetryInput& input) const
{
	if (input.policy == nullptr)
	{
		return formatLine(
			"static_defense_policy zone=%u role=unknown tunnels=0/0 stingers=0/0 in_progress=%d reason=not_evaluated",
			input.zone,
			input.inProgress);
	}

	const AIControlAdapterStaticDefensePolicyResult& policy = *input.policy;
	return formatLine(
		"static_defense_policy zone=%u role=%s tunnels=%d/%d stingers=%d/%d in_progress=%d reason=%s",
		input.zone,
		safeString(policy.role, "unknown"),
		policy.effectiveTunnels,
		policy.desiredTunnels,
		policy.effectiveStingers,
		policy.desiredStingers,
		input.inProgress,
		safeString(policy.reason, "unknown"));
}

nlohmann::json AIControlAdapterMacroBuildTelemetrySerializer::BuildPalaceRedundancyTelemetry(
	const AIControlAdapterPalaceRedundancyTelemetryInput& input) const
{
	if (input.policy == nullptr)
	{
		return nlohmann::json::object({
			{"zone", input.zone},
			{"role", "unknown"},
			{"live", input.live},
			{"desired", 0},
			{"in_progress", input.inProgress},
			{"spend_allowed", false},
			{"reason", "not_evaluated"}
		});
	}

	const AIControlAdapterPalaceRedundancyResult& policy = *input.policy;
	return nlohmann::json::object({
		{"zone", input.zone},
		{"role", safeString(policy.role, "unknown")},
		{"live", input.live},
		{"desired", policy.desiredZonePalaces},
		{"in_progress", input.inProgress},
		{"spend_allowed", policy.spendAllowed},
		{"reason", safeString(policy.reason, "unknown")}
	});
}

std::string AIControlAdapterMacroBuildTelemetrySerializer::BuildPalaceRedundancyLogLine(
	const AIControlAdapterPalaceRedundancyTelemetryInput& input) const
{
	if (input.policy == nullptr)
	{
		return formatLine(
			"palace_redundancy_policy zone=%u role=unknown live=%d desired=0 in_progress=%d spend_allowed=0 reason=not_evaluated",
			input.zone,
			input.live,
			input.inProgress);
	}

	const AIControlAdapterPalaceRedundancyResult& policy = *input.policy;
	return formatLine(
		"palace_redundancy_policy zone=%u role=%s live=%d desired=%d in_progress=%d spend_allowed=%d reason=%s",
		input.zone,
		safeString(policy.role, "unknown"),
		input.live,
		policy.desiredZonePalaces,
		input.inProgress,
		policy.spendAllowed ? 1 : 0,
		safeString(policy.reason, "unknown"));
}

nlohmann::json AIControlAdapterMacroBuildTelemetrySerializer::BuildBrutalPressureTelemetry(
	const AIControlAdapterBrutalPressureTelemetryInput& input) const
{
	return nlohmann::json::object({
		{"expansion_gap", input.expansionGap},
		{"main_under_pressure", input.mainUnderPressure},
		{"static_defense_gap", input.staticDefenseGap},
		{"garrison_gap", input.garrisonGap},
		{"local_worker_gap", input.localWorkerGap},
		{"stale_foundations", input.staleFoundations},
		{"mobile_siege_threats", input.mobileSiegeThreats},
		{"reserve_protected", input.reserveProtected},
		{"cash_float", input.cashAboveReserve},
		{"chosen_priority", input.policy != nullptr ? safeString(input.policy->chosenPriority, "none") : "not_evaluated"},
		{"reason", input.policy != nullptr ? safeString(input.policy->reason, "not_evaluated") : "not_evaluated"}
	});
}

std::string AIControlAdapterMacroBuildTelemetrySerializer::BuildBrutalPressureLogLine(
	const AIControlAdapterBrutalPressureTelemetryInput& input) const
{
	return formatLine(
		"brutal_pressure_policy expansion_gap=%d main_under_pressure=%d static_defense_gap=%d garrison_gap=%d local_worker_gap=%d stale_foundations=%d mobile_siege_threats=%d reserve_protected=%d cash_float=%lu chosen_priority=%s reason=%s",
		input.expansionGap,
		input.mainUnderPressure ? 1 : 0,
		input.staticDefenseGap,
		input.garrisonGap,
		input.localWorkerGap,
		input.staleFoundations,
		input.mobileSiegeThreats,
		input.reserveProtected ? 1 : 0,
		static_cast<unsigned long>(input.cashAboveReserve),
		input.policy != nullptr ? safeString(input.policy->chosenPriority, "none") : "not_evaluated",
		input.policy != nullptr ? safeString(input.policy->reason, "not_evaluated") : "not_evaluated");
}

std::string AIControlAdapterMacroBuildTelemetrySerializer::BuildIntentLogLine(
	const AIControlAdapterMacroBuildIntent& intent,
	unsigned int money) const
{
	return formatLine(
		"macro_build_intent category=%s command=%s priority=%d valid=%d money=%lu min_cash=%lu in_progress=%d max_in_progress=%d reason=%s",
		safeString(intent.option.category, "unknown"),
		safeString(intent.option.command, "none"),
		intent.option.priority,
		intent.option.valid ? 1 : 0,
		static_cast<unsigned long>(money),
		static_cast<unsigned long>(intent.minCash),
		intent.inProgress,
		intent.maxInProgress,
		safeString(intent.option.reason, "unknown"));
}

std::string AIControlAdapterMacroBuildTelemetrySerializer::BuildDispatchResultLogLine(
	const AIControlAdapterMacroBuildDispatchResult& result) const
{
	return formatLine(
		"macro_build_intent_result category=%s command=%s priority=%d issued=%d reason=%s",
		result.category.c_str(),
		result.command.c_str(),
		result.priority,
		result.issued ? 1 : 0,
		result.reason.c_str());
}
