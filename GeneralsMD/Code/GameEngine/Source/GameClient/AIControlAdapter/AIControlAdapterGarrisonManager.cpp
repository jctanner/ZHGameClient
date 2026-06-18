#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterGarrisonManager.h"

#include <algorithm>
#include <cctype>

bool AIControlAdapterGarrisonManager::containsIgnoreCase(const std::string& haystack, const std::string& needle)
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

bool AIControlAdapterGarrisonManager::isNearMapCacheGarrison(
	const nlohmann::json& pathingTelemetry,
	const std::string& templateName,
	float x,
	float y,
	float radius)
{
	if (!pathingTelemetry.is_object())
	{
		return false;
	}
	const auto objectsIt = pathingTelemetry.find("strategic_objects");
	if (objectsIt == pathingTelemetry.end() || !objectsIt->is_array())
	{
		return false;
	}
	const float radiusSq = radius * radius;
	for (const auto& item : *objectsIt)
	{
		if (!item.is_object() || item.value("kind", std::string("")) != "garrison")
		{
			continue;
		}
		const std::string cachedTemplate = item.value("template", std::string(""));
		if (!cachedTemplate.empty() && !templateName.empty() && cachedTemplate != templateName)
		{
			continue;
		}
		const auto positionIt = item.find("position");
		if (positionIt == item.end() || !positionIt->is_object())
		{
			continue;
		}
		const float fx = positionIt->value("x", 0.0f);
		const float fy = positionIt->value("y", 0.0f);
		const float dx = fx - x;
		const float dy = fy - y;
		if ((dx * dx) + (dy * dy) <= radiusSq)
		{
			return true;
		}
	}
	return false;
}

AIControlAdapterGarrisonDiscoveryResult AIControlAdapterGarrisonManager::evaluateDiscovery(
	const AIControlAdapterGarrisonDiscoveryFacts& facts)
{
	AIControlAdapterGarrisonDiscoveryResult result;
	result.palace = facts.palace;
	result.kindFlag = facts.kindFlag;
	result.containGarrison = facts.containGarrison;
	result.uiEnterable = facts.uiEnterable;
	result.mapCacheGarrison = facts.mapCacheGarrison;
	result.capacity = facts.capacity;
	if (facts.hasContain)
	{
		result.containName = facts.containGarrison ? "garrison" : "contain";
	}
	result.accepted = result.palace || result.kindFlag || result.containGarrison || result.uiEnterable || result.mapCacheGarrison;
	if (result.palace)
	{
		result.reason = "palace";
	}
	else if (result.kindFlag)
	{
		result.reason = "kind_flag";
	}
	else if (result.containGarrison)
	{
		result.reason = "contain_garrison";
	}
	else if (result.uiEnterable)
	{
		result.reason = "ui_enterable";
	}
	else if (result.mapCacheGarrison)
	{
		result.reason = "map_cache_garrison";
	}
	else if (!facts.hasContain)
	{
		result.reason = "missing_contain";
	}
	else if (containsIgnoreCase(facts.templateName, "civilian"))
	{
		result.reason = "unknown_civilian";
	}
	else
	{
		result.reason = "not_garrisonable";
	}
	return result;
}

bool AIControlAdapterGarrisonManager::shouldLogDiscovery(
	const std::string& templateName,
	const AIControlAdapterGarrisonDiscoveryResult& discovery)
{
	return discovery.accepted
		|| discovery.kindFlag
		|| discovery.containName != "none"
		|| discovery.mapCacheGarrison
		|| containsIgnoreCase(templateName, "civilian")
		|| containsIgnoreCase(templateName, "bunker")
		|| containsIgnoreCase(templateName, "garrison");
}

bool AIControlAdapterGarrisonManager::isNearGarrisonFeature(
	const nlohmann::json& pathingTelemetry,
	float x,
	float y,
	const char* wantedKind,
	float radius)
{
	if (wantedKind == nullptr || !pathingTelemetry.is_object())
	{
		return false;
	}
	const auto featuresIt = pathingTelemetry.find("features");
	if (featuresIt == pathingTelemetry.end() || !featuresIt->is_array())
	{
		return false;
	}
	const float radiusSq = radius * radius;
	for (const auto& feature : *featuresIt)
	{
		if (!feature.is_object())
		{
			continue;
		}
		const std::string kind = feature.value("kind", std::string(""));
		const std::string id = feature.value("id", std::string(""));
		if (!containsIgnoreCase(kind, wantedKind) && !containsIgnoreCase(id, wantedKind))
		{
			continue;
		}
		float fx = 0.0f;
		float fy = 0.0f;
		const auto positionIt = feature.find("position");
		if (positionIt != feature.end() && positionIt->is_object())
		{
			fx = positionIt->value("x", 0.0f);
			fy = positionIt->value("y", 0.0f);
		}
		else
		{
			const auto pointsIt = feature.find("points");
			if (pointsIt == feature.end() || !pointsIt->is_array() || pointsIt->empty() || !(*pointsIt)[0].is_object())
			{
				continue;
			}
			fx = (*pointsIt)[0].value("x", 0.0f);
			fy = (*pointsIt)[0].value("y", 0.0f);
		}
		const float dx = fx - x;
		const float dy = fy - y;
		if ((dx * dx) + (dy * dy) <= radiusSq)
		{
			return true;
		}
	}
	return false;
}

int AIControlAdapterGarrisonManager::countEnteredInfantry(const AIControlAdapterGarrisonAssignmentTelemetry& assignment)
{
	int entered = 0;
	for (const AIControlAdapterGarrisonInfantryTelemetry& unit : assignment.infantry)
	{
		if (unit.entered)
		{
			++entered;
		}
	}
	return entered;
}

nlohmann::json AIControlAdapterGarrisonManager::buildAssignmentTelemetry(
	const AIControlAdapterGarrisonAssignmentTelemetry& assignment,
	unsigned int now)
{
	nlohmann::json unitTelemetry = nlohmann::json::array();
	nlohmann::json ids = nlohmann::json::array();
	nlohmann::json templates = nlohmann::json::array();
	int entered = 0;
	int pending = 0;
	int outside = 0;
	int nearby = 0;
	for (const AIControlAdapterGarrisonInfantryTelemetry& unit : assignment.infantry)
	{
		ids.push_back(unit.unitId);
		templates.push_back(unit.templateName);
		if (unit.entered)
		{
			++entered;
		}
		if (unit.enteredPendingVerification)
		{
			++pending;
		}
		if (unit.outside)
		{
			++outside;
		}
		if (unit.nearby)
		{
			++nearby;
		}
		unitTelemetry.push_back(nlohmann::json::object({
			{"id", unit.unitId},
			{"template", unit.templateName},
			{"position", nlohmann::json::object({ {"x", unit.lastX}, {"y", unit.lastY}, {"z", 0.0f} })},
			{"distance", unit.lastDistance},
			{"entered", unit.entered},
			{"entered_pending_verification", unit.enteredPendingVerification},
			{"outside", unit.outside},
			{"nearby", unit.nearby},
			{"last_command_age_ms", unit.lastCommandTick != 0u ? now - unit.lastCommandTick : 0u},
			{"last_progress_age_ms", unit.lastProgressTick != 0u ? now - unit.lastProgressTick : 0u},
			{"state", unit.state},
			{"reason", unit.reason}
		}));
	}
	return nlohmann::json::object({
		{"structure_id", assignment.structureId},
		{"template", assignment.templateName},
		{"zone_id", assignment.zoneAnchorId},
		{"position", nlohmann::json::object({ {"x", assignment.x}, {"y", assignment.y}, {"z", 0.0f} })},
		{"desired_infantry", assignment.desiredInfantry},
		{"capacity", assignment.estimatedCapacity},
		{"assigned_infantry", static_cast<int>(assignment.infantry.size())},
		{"assigned_infantry_ids", ids},
		{"assigned_infantry_templates", templates},
		{"entered", entered},
		{"entered_infantry", entered},
		{"entered_pending_verification", pending},
		{"outside_infantry", outside},
		{"nearby_infantry", nearby},
		{"selected", true},
		{"discovered", true},
		{"source", "assignment"},
		{"discovery_reason", "assignment"},
		{"last_command_age_ms", assignment.lastCommandTick != 0u ? now - assignment.lastCommandTick : 0u},
		{"last_progress_age_ms", assignment.lastProgressTick != 0u ? now - assignment.lastProgressTick : 0u},
		{"units", unitTelemetry},
		{"state", assignment.state},
		{"reason", assignment.reason}
	});
}
