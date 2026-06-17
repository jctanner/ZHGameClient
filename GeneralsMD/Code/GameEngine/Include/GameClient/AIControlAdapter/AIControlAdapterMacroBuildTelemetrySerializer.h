/**
 * AIControlAdapterMacroBuildTelemetrySerializer.h
 *
 * Stable JSON contract for macro build telemetry.
 */

#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildDispatcher.h"
#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendManager.h"
#include "GameNetwork/GeneralsOnline/json.hpp"

#include <string>
#include <vector>

struct AIControlAdapterMacroBuildTelemetryInput
{
	std::string profile = "none";
	unsigned int money = 0u;
	unsigned int reserveCash = 0u;
	unsigned int cashAboveReserve = 0u;
	int currentZones = 0;
	int developedZones = 0;
	int desiredZones = 0;
	int zoneGap = 0;
	unsigned int activeZoneAnchor = 0u;
	bool activeZoneThreatened = false;
	bool remoteZoneNeedsFollowup = false;
	const AIControlAdapterMacroBuildPlan* macroBuildPlan = nullptr;
	const AIControlAdapterStrategicSpendPlan* spendPlan = nullptr;
};

struct AIControlAdapterMacroBuildExpansionLogInput
{
	const AIControlAdapterMacroBuildTelemetry* telemetry = nullptr;
	unsigned int reserveCash = 0u;
	unsigned int activeZoneAnchor = 0u;
	bool logZoneSeed = false;
};

struct AIControlAdapterStaticDefensePolicyTelemetryInput
{
	unsigned int zone = 0u;
	const AIControlAdapterStaticDefensePolicyResult* policy = nullptr;
	int inProgress = 0;
};

struct AIControlAdapterPalaceRedundancyTelemetryInput
{
	unsigned int zone = 0u;
	const AIControlAdapterPalaceRedundancyResult* policy = nullptr;
	int live = 0;
	int inProgress = 0;
};

struct AIControlAdapterBrutalPressureTelemetryInput
{
	int expansionGap = 0;
	bool mainUnderPressure = false;
	int staticDefenseGap = 0;
	int garrisonGap = 0;
	int localWorkerGap = 0;
	int staleFoundations = 0;
	int mobileSiegeThreats = 0;
	bool reserveProtected = false;
	unsigned int cashAboveReserve = 0u;
	const AIControlAdapterBrutalPressureResult* policy = nullptr;
};

class AIControlAdapterMacroBuildTelemetrySerializer
{
public:
	nlohmann::json BuildDefaultTelemetry() const;
	nlohmann::json BuildTelemetry(const AIControlAdapterMacroBuildTelemetryInput& input) const;
	std::vector<std::string> BuildExpansionLogLines(const AIControlAdapterMacroBuildExpansionLogInput& input) const;
	nlohmann::json BuildStaticDefensePolicyTelemetry(const AIControlAdapterStaticDefensePolicyTelemetryInput& input) const;
	std::string BuildStaticDefensePolicyLogLine(const AIControlAdapterStaticDefensePolicyTelemetryInput& input) const;
	nlohmann::json BuildPalaceRedundancyTelemetry(const AIControlAdapterPalaceRedundancyTelemetryInput& input) const;
	std::string BuildPalaceRedundancyLogLine(const AIControlAdapterPalaceRedundancyTelemetryInput& input) const;
	nlohmann::json BuildBrutalPressureTelemetry(const AIControlAdapterBrutalPressureTelemetryInput& input) const;
	std::string BuildBrutalPressureLogLine(const AIControlAdapterBrutalPressureTelemetryInput& input) const;
	std::string BuildIntentLogLine(const AIControlAdapterMacroBuildIntent& intent, unsigned int money) const;
	std::string BuildDispatchResultLogLine(const AIControlAdapterMacroBuildDispatchResult& result) const;
};
