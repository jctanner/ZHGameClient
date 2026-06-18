#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterScoutingManager.h"

#include <algorithm>

unsigned int AIControlAdapterScoutingManager::buildStableScoutSeed(
	const std::string& mapName,
	int playerIndex,
	unsigned int currentTick,
	int scoutTaskCount)
{
	unsigned int hash = 2166136261u;
	for (char c : mapName)
	{
		hash ^= static_cast<unsigned char>(c);
		hash *= 16777619u;
	}
	hash ^= static_cast<unsigned int>(playerIndex);
	hash *= 16777619u;
	hash ^= static_cast<unsigned int>((currentTick / 30000u) & 0xFFFFu);
	hash *= 16777619u;
	hash ^= static_cast<unsigned int>(scoutTaskCount + 1);
	return hash;
}

AIControlAdapterScoutMapBounds AIControlAdapterScoutingManager::resolveScoutMapBounds(
	const nlohmann::json& pathingTelemetry,
	const nlohmann::json& telemetryZones)
{
	AIControlAdapterScoutMapBounds bounds;
	if (pathingTelemetry.is_object())
	{
		const nlohmann::json& extraction = pathingTelemetry.value("terrain_extraction", nlohmann::json::object());
		if (extraction.is_object() && extraction.contains("extent"))
		{
			const nlohmann::json& extent = extraction["extent"];
			if (extent.is_object())
			{
				bounds.minX = extent.value("min_x", bounds.minX);
				bounds.minY = extent.value("min_y", bounds.minY);
				bounds.maxX = extent.value("max_x", bounds.maxX);
				bounds.maxY = extent.value("max_y", bounds.maxY);
			}
		}
	}
	if (telemetryZones.is_array())
	{
		for (const nlohmann::json& zone : telemetryZones)
		{
			const float x = zone.value("center_x", 0.0f);
			const float y = zone.value("center_y", 0.0f);
			if (x > 0.0f || y > 0.0f)
			{
				bounds.minX = std::min(bounds.minX, x - 2500.0f);
				bounds.minY = std::min(bounds.minY, y - 2500.0f);
				bounds.maxX = std::max(bounds.maxX, x + 2500.0f);
				bounds.maxY = std::max(bounds.maxY, y + 2500.0f);
			}
		}
	}
	return bounds;
}

std::vector<CombatTaskScoutRandomOrigin> AIControlAdapterScoutingManager::buildScoutRandomOrigins(
	const nlohmann::json& telemetryZones,
	const AIControlAdapterScoutLastZoneSnapshot& lastZone)
{
	std::vector<CombatTaskScoutRandomOrigin> origins;
	if (telemetryZones.is_array())
	{
		for (const nlohmann::json& zone : telemetryZones)
		{
			CombatTaskScoutRandomOrigin origin;
			origin.zoneId = zone.value("anchor_id", 0u);
			origin.mainBase = zone.value("is_main_base", false);
			origin.position.x = zone.value("front_point_x", zone.value("center_x", 0.0f));
			origin.position.y = zone.value("front_point_y", zone.value("center_y", 0.0f));
			origin.position.z = 0.0f;
			origin.priority = zone.value("active", false) ? 10 : (zone.value("developed", false) ? 5 : 0);
			if (origin.zoneId > 0u && (origin.position.x != 0.0f || origin.position.y != 0.0f))
			{
				origins.push_back(origin);
			}
		}
	}
	bool hasNonMain = false;
	for (const CombatTaskScoutRandomOrigin& origin : origins)
	{
		if (!origin.mainBase)
		{
			hasNonMain = true;
			break;
		}
	}
	if (hasNonMain)
	{
		std::vector<CombatTaskScoutRandomOrigin> filtered;
		for (const CombatTaskScoutRandomOrigin& origin : origins)
		{
			if (!origin.mainBase)
			{
				filtered.push_back(origin);
			}
		}
		origins.swap(filtered);
	}
	if (origins.empty() && lastZone.hasLastZone)
	{
		CombatTaskScoutRandomOrigin origin;
		origin.zoneId = lastZone.anchorId;
		origin.mainBase = lastZone.isMainBase;
		origin.position.x = lastZone.centerX;
		origin.position.y = lastZone.centerY;
		origin.position.z = 0.0f;
		origins.push_back(origin);
	}
	return origins;
}

std::vector<CombatTaskScoutRandomBarrier> AIControlAdapterScoutingManager::buildScoutRandomBarriers(
	const nlohmann::json& pathingTelemetry)
{
	std::vector<CombatTaskScoutRandomBarrier> barriers;
	if (!pathingTelemetry.is_object())
	{
		return barriers;
	}
	const nlohmann::json& features = pathingTelemetry.value("terrain_features", nlohmann::json::array());
	if (!features.is_array())
	{
		return barriers;
	}
	for (const nlohmann::json& feature : features)
	{
		const std::string kind = feature.value("kind", "");
		if (kind != "impassable_barrier" && kind != "blocked_area")
		{
			continue;
		}
		const nlohmann::json& points = feature.value("points", nlohmann::json::array());
		if (!points.is_array() || points.size() < 2u)
		{
			continue;
		}
		for (std::size_t i = 1; i < points.size(); ++i)
		{
			const nlohmann::json& a = points[i - 1];
			const nlohmann::json& b = points[i];
			if (!a.is_object() || !b.is_object())
			{
				continue;
			}
			CombatTaskScoutRandomBarrier barrier;
			barrier.ax = a.value("x", 0.0f);
			barrier.ay = a.value("y", 0.0f);
			barrier.bx = b.value("x", 0.0f);
			barrier.by = b.value("y", 0.0f);
			barriers.push_back(barrier);
		}
	}
	return barriers;
}
