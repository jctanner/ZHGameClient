#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendTelemetrySerializer.h"

#include <cstdarg>
#include <cstdio>

namespace
{
	const char* safeString(const char* value, const char* fallback)
	{
		return value != nullptr && value[0] != '\0' ? value : fallback;
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

	void ensureObject(nlohmann::json& telemetry)
	{
		if (!telemetry.is_object())
		{
			telemetry = nlohmann::json::object();
		}
	}

	void ensureCategories(nlohmann::json& telemetry)
	{
		ensureObject(telemetry);
		if (!telemetry["categories"].is_object())
		{
			telemetry["categories"] = nlohmann::json::object();
		}
	}
}

nlohmann::json AIControlAdapterStrategicSpendTelemetrySerializer::BuildDefaultTelemetry() const
{
	return nlohmann::json::object({
		{"protected_cash", 0},
		{"last_allowed_category", "none"},
		{"last_blocked_reason", "not_evaluated"},
		{"categories", nlohmann::json::object()},
		{"emergency_batch_limit", 0},
		{"economy_recovery_action", "none"},
		{"healthy_market_foundations", 0},
		{"stale_market_foundations", 0},
		{"market_foundations_no_builder", 0},
		{"stale_strategic_foundations", 0}
	});
}

void AIControlAdapterStrategicSpendTelemetrySerializer::RecordMarketFoundationCounts(
	nlohmann::json& telemetry,
	int healthyMarketFoundations,
	int staleMarketFoundations,
	int staleStrategicFoundations) const
{
	ensureObject(telemetry);
	telemetry["healthy_market_foundations"] = healthyMarketFoundations;
	telemetry["stale_market_foundations"] = staleMarketFoundations;
	telemetry["stale_strategic_foundations"] = staleStrategicFoundations;
}

void AIControlAdapterStrategicSpendTelemetrySerializer::RecordCategory(
	nlohmann::json& telemetry,
	StrategicSpendCategory category,
	unsigned int requestCost,
	const AIControlAdapterStrategicSpendDecision& decision) const
{
	ensureCategories(telemetry);
	const char* categoryName = AIControlAdapterStrategicSpendCategoryName(category);
	telemetry["categories"][categoryName] = nlohmann::json::object({
		{"allowed", decision.allowed},
		{"request_cost", requestCost},
		{"protected_cash", decision.protectedCash},
		{"spend_budget", decision.spendBudget},
		{"batch_limit", decision.batchLimit},
		{"reason", decision.reason}
	});
	telemetry["protected_cash"] = decision.protectedCash;
	if (decision.allowed)
	{
		telemetry["last_allowed_category"] = categoryName;
	}
	else
	{
		telemetry["last_blocked_reason"] = decision.reason;
	}
}

void AIControlAdapterStrategicSpendTelemetrySerializer::RecordPlan(
	nlohmann::json& telemetry,
	const AIControlAdapterStrategicSpendPlan& plan) const
{
	for (const AIControlAdapterStrategicSpendRecord& record : plan.records)
	{
		RecordCategory(telemetry, record.category, record.requestCost, record.decision);
	}
}

std::string AIControlAdapterStrategicSpendTelemetrySerializer::BuildPolicyLogLine(
	const AIControlAdapterStrategicSpendPolicyLogInput& input) const
{
	if (input.decision == nullptr)
	{
		return formatLine(
			"strategic_spend_policy category=%s allowed=0 money=%lu reserve=%lu protected_cash=0 spend_budget=0 batch_limit=0 reason=not_evaluated",
			AIControlAdapterStrategicSpendCategoryName(input.category),
			static_cast<unsigned long>(input.money),
			static_cast<unsigned long>(input.reserveCash));
	}

	const AIControlAdapterStrategicSpendDecision& decision = *input.decision;
	return formatLine(
		"strategic_spend_policy category=%s allowed=%d money=%lu reserve=%lu protected_cash=%lu spend_budget=%lu batch_limit=%d reason=%s",
		AIControlAdapterStrategicSpendCategoryName(input.category),
		decision.allowed ? 1 : 0,
		static_cast<unsigned long>(input.money),
		static_cast<unsigned long>(input.reserveCash),
		static_cast<unsigned long>(decision.protectedCash),
		static_cast<unsigned long>(decision.spendBudget),
		decision.batchLimit,
		safeString(decision.reason, "unknown"));
}
