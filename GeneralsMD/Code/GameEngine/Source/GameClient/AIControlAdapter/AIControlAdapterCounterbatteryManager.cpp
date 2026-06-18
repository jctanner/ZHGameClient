#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCounterbatteryManager.h"

#include <algorithm>
#include <cctype>

bool AIControlAdapterCounterbatteryManager::containsIgnoreCase(const std::string& haystack, const std::string& needle)
{
	if (needle.empty())
	{
		return true;
	}
	std::string hay = haystack;
	std::string ndl = needle;
	std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	std::transform(ndl.begin(), ndl.end(), ndl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return hay.find(ndl) != std::string::npos;
}

bool AIControlAdapterCounterbatteryManager::shouldLogMobileSiegeDetection(
	const std::string& templateName,
	const AIControlAdapterMobileSiegeTemplateResult& classification)
{
	if (classification.accepted)
	{
		return true;
	}
	return containsIgnoreCase(templateName, "shell") ||
		containsIgnoreCase(templateName, "artillery") ||
		containsIgnoreCase(templateName, "cannon") ||
		containsIgnoreCase(templateName, "inferno") ||
		containsIgnoreCase(templateName, "scudlauncher") ||
		containsIgnoreCase(templateName, "tomahawk") ||
		containsIgnoreCase(templateName, "rocketbuggy");
}

bool AIControlAdapterCounterbatteryManager::isSuitableCounterUnit(const AIControlAdapterCounterbatteryUnitSnapshot& unit)
{
	return containsIgnoreCase(unit.templateName, "rocketbuggy") ||
		containsIgnoreCase(unit.templateName, "jarmen") ||
		unit.isScudLauncher;
}

int AIControlAdapterCounterbatteryManager::counterUnitPriority(const AIControlAdapterCounterbatteryUnitSnapshot& unit)
{
	if (containsIgnoreCase(unit.templateName, "rocketbuggy"))
	{
		return 0;
	}
	if (containsIgnoreCase(unit.templateName, "jarmen"))
	{
		return 1;
	}
	if (unit.isScudLauncher || containsIgnoreCase(unit.templateName, "scudlauncher"))
	{
		return 2;
	}
	return 3;
}

std::vector<unsigned int> AIControlAdapterCounterbatteryManager::selectAssignmentIds(
	const std::vector<AIControlAdapterCounterbatteryUnitSnapshot>& units,
	int desiredAssignedUnits,
	int hardCap)
{
	std::vector<unsigned int> ids;
	const int count = std::min<int>(
		std::max<int>(0, desiredAssignedUnits),
		std::min<int>(std::max<int>(0, hardCap), static_cast<int>(units.size())));
	for (int i = 0; i < count; ++i)
	{
		if (units[static_cast<std::size_t>(i)].unitId != 0u)
		{
			ids.push_back(units[static_cast<std::size_t>(i)].unitId);
		}
	}
	return ids;
}

nlohmann::json AIControlAdapterCounterbatteryManager::buildTelemetry(
	const AIControlAdapterCounterbatteryTelemetryInput& input)
{
	nlohmann::json threatTelemetry = nlohmann::json::array();
	for (const AIControlAdapterCounterbatteryThreatSnapshot& threat : input.threats)
	{
		threatTelemetry.push_back(nlohmann::json::object({
			{"object_id", threat.objectId},
			{"template", threat.templateName},
			{"visible", threat.visible},
			{"stale", !threat.visible},
			{"x", threat.x},
			{"y", threat.y},
			{"priority", "high"}
		}));
	}
	return nlohmann::json::object({
		{"artillery_threats", threatTelemetry},
		{"mobile_siege_threats", threatTelemetry},
		{"active_tasks", input.activeTasks},
		{"assigned_units", input.assignedUnits},
		{"production_needed", input.productionNeeded},
		{"reason", input.reason}
	});
}
