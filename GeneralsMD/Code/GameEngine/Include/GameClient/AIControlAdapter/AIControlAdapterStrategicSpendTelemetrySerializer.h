/**
 * AIControlAdapterStrategicSpendTelemetrySerializer.h
 *
 * Stable JSON contract for strategic spend telemetry.
 */

#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendManager.h"
#include "GameNetwork/GeneralsOnline/json.hpp"

#include <string>

struct AIControlAdapterStrategicSpendPolicyLogInput
{
	StrategicSpendCategory category = StrategicSpendCategory::Expansion;
	unsigned int money = 0u;
	unsigned int reserveCash = 0u;
	const AIControlAdapterStrategicSpendDecision* decision = nullptr;
};

class AIControlAdapterStrategicSpendTelemetrySerializer
{
public:
	nlohmann::json BuildDefaultTelemetry() const;

	void RecordMarketFoundationCounts(
		nlohmann::json& telemetry,
		int healthyMarketFoundations,
		int staleMarketFoundations,
		int staleStrategicFoundations) const;

	void RecordCategory(
		nlohmann::json& telemetry,
		StrategicSpendCategory category,
		unsigned int requestCost,
		const AIControlAdapterStrategicSpendDecision& decision) const;

	void RecordPlan(
		nlohmann::json& telemetry,
		const AIControlAdapterStrategicSpendPlan& plan) const;

	std::string BuildPolicyLogLine(const AIControlAdapterStrategicSpendPolicyLogInput& input) const;
};
