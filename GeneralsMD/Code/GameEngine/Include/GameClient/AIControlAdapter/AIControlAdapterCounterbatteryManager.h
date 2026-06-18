#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"
#include "GameNetwork/GeneralsOnline/json.hpp"

#include <string>
#include <vector>

struct AIControlAdapterCounterbatteryThreatSnapshot
{
	unsigned int objectId = 0u;
	std::string templateName;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	bool visible = true;
};

struct AIControlAdapterCounterbatteryUnitSnapshot
{
	unsigned int unitId = 0u;
	std::string templateName;
	bool isScudLauncher = false;
};

struct AIControlAdapterCounterbatteryTelemetryInput
{
	std::vector<AIControlAdapterCounterbatteryThreatSnapshot> threats;
	int activeTasks = 0;
	int assignedUnits = 0;
	bool productionNeeded = false;
	std::string reason;
};

class AIControlAdapterCounterbatteryManager
{
public:
	static bool containsIgnoreCase(const std::string& haystack, const std::string& needle);

	static bool shouldLogMobileSiegeDetection(
		const std::string& templateName,
		const AIControlAdapterMobileSiegeTemplateResult& classification);

	static bool isSuitableCounterUnit(const AIControlAdapterCounterbatteryUnitSnapshot& unit);

	static int counterUnitPriority(const AIControlAdapterCounterbatteryUnitSnapshot& unit);

	static std::vector<unsigned int> selectAssignmentIds(
		const std::vector<AIControlAdapterCounterbatteryUnitSnapshot>& units,
		int desiredAssignedUnits,
		int hardCap);

	static nlohmann::json buildTelemetry(const AIControlAdapterCounterbatteryTelemetryInput& input);
};
