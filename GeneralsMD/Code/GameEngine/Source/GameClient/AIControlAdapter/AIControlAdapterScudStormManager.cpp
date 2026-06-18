#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterScudStormManager.h"

#include <algorithm>

int AIControlAdapterScudStormManager::countSupplyZones(const nlohmann::json& telemetryZones)
{
	int count = 0;
	if (!telemetryZones.is_array())
	{
		return count;
	}
	for (const nlohmann::json& zone : telemetryZones)
	{
		if (!zone.is_object())
		{
			continue;
		}
		if (zone.value("supply_stashes", 0) > 0)
		{
			++count;
		}
	}
	return count;
}

std::vector<AIControlAdapterScudStormStrategicTargetCandidate> AIControlAdapterScudStormManager::buildStrategicTargetCandidates(
	const std::vector<AIControlAdapterScudStormMemorySnapshot>& memoryItems,
	unsigned int now)
{
	std::vector<AIControlAdapterScudStormStrategicTargetCandidate> candidates;
	candidates.reserve(memoryItems.size());
	for (const AIControlAdapterScudStormMemorySnapshot& item : memoryItems)
	{
		AIControlAdapterScudStormStrategicTargetCandidate candidate;
		candidate.objectId = item.objectId;
		candidate.playerIndex = item.playerIndex;
		candidate.team = item.team;
		candidate.targetKind = item.targetKind;
		candidate.templateName = item.templateName;
		candidate.visible = item.visible;
		candidate.stale = item.stale;
		candidate.enemyOwned = item.enemyOwned;
		candidate.alive = item.alive;
		candidate.ageMs = item.lastSeenTick == 0u ? 0u : now - item.lastSeenTick;
		candidate.x = item.x;
		candidate.y = item.y;
		candidate.z = item.z;
		candidates.push_back(candidate);
	}
	return candidates;
}

std::vector<AIControlAdapterScudStormMemorySnapshot> AIControlAdapterScudStormManager::buildMemorySnapshotsFromEnemyMemory(
	const std::vector<EnemyMemoryItem>& memoryItems)
{
	std::vector<AIControlAdapterScudStormMemorySnapshot> snapshots;
	snapshots.reserve(memoryItems.size());
	for (const EnemyMemoryItem& item : memoryItems)
	{
		AIControlAdapterScudStormMemorySnapshot snapshot;
		snapshot.objectId = item.objectId;
		snapshot.playerIndex = item.playerIndex;
		snapshot.team = item.team;
		snapshot.targetKind = AIControlAdapterEnemyMemory::kindToString(item.kind);
		snapshot.templateName = item.templateName;
		snapshot.visible = item.visible;
		snapshot.stale = item.stale;
		snapshot.enemyOwned = true;
		snapshot.alive = true;
		snapshot.lastSeenTick = item.lastSeenTick;
		snapshot.x = item.position.x;
		snapshot.y = item.position.y;
		snapshot.z = item.position.z;
		snapshots.push_back(snapshot);
	}
	return snapshots;
}

AIControlAdapterScudStormPlacementChoice AIControlAdapterScudStormManager::choosePlacement(
	const nlohmann::json& telemetryZones,
	const std::vector<AIControlAdapterScudStormZoneThreatSnapshot>& zoneThreats,
	unsigned int now,
	float defaultZoneRadius)
{
	AIControlAdapterScudStormPlacementChoice best;
	if (!telemetryZones.is_array() || telemetryZones.empty())
	{
		return best;
	}
	for (const nlohmann::json& zone : telemetryZones)
	{
		if (!zone.is_object())
		{
			continue;
		}
		const unsigned int zoneId = zone.value("anchor_id", 0u);
		const bool isMainBase = zone.value("is_main_base", false);
		const bool isActive = zone.value("active", false);
		const bool developed = zone.value("developed", false);
		const bool needsFollowup = zone.value("needs_followup", false);
		bool recentlyAttacked = false;
		for (const AIControlAdapterScudStormZoneThreatSnapshot& threat : zoneThreats)
		{
			if (threat.zoneId == zoneId && threat.lastSeenTick != 0u && now - threat.lastSeenTick <= 45000u)
			{
				recentlyAttacked = true;
				break;
			}
		}
		int score = 0;
		if (isMainBase)
		{
			score += 220;
		}
		if (developed)
		{
			score += 140;
		}
		if (zone.value("black_markets", 0) > 0 || zone.value("palaces", 0) > 0)
		{
			score += 80;
		}
		if (zone.value("tunnels", 0) > 0 || zone.value("stingers", 0) > 0)
		{
			score += 40;
		}
		if (needsFollowup)
		{
			score -= 120;
		}
		if (isActive)
		{
			score -= 500;
		}
		if (recentlyAttacked)
		{
			score -= 450;
		}
		const bool terrainLimited = zone.value("terrain_limited", false);
		const float effectiveRadius = zone.value("effective_radius", defaultZoneRadius);
		if (effectiveRadius < std::max<float>(220.0f, defaultZoneRadius * 0.45f))
		{
			score -= 180;
		}
		else if (terrainLimited)
		{
			score -= 40;
		}
		if (!developed && !isMainBase)
		{
			score -= 150;
		}
		if (!best.hasPlacement || score > best.score)
		{
			best.hasPlacement = true;
			best.zoneId = zoneId;
			best.score = score;
			best.x = zone.value("rear_point_x", zone.value("center_x", 0.0f));
			best.y = zone.value("rear_point_y", zone.value("center_y", 0.0f));
			best.z = 0.0f;
			best.radius = std::max<float>(180.0f, effectiveRadius * (isMainBase ? 0.35f : 0.30f));
			if (score < 0)
			{
				best.role = "fallback";
				best.reason = "emergency_override";
			}
			else if (terrainLimited)
			{
				best.role = "rear";
				best.reason = "terrain_rear";
			}
			else if (isMainBase)
			{
				best.role = "interior";
				best.reason = "fallback_interior";
			}
			else
			{
				best.role = "rear";
				best.reason = "safe_rear";
			}
		}
	}
	return best;
}
