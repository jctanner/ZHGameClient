/**
 * AIControlAdapterZoneManager.cpp
 *
 * Implementation of zone threat tracking and management system.
 *
 * See AIControlAdapterZoneManager.h for design principles and architecture.
 */

#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterZoneManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

#include <cmath>
#include <algorithm>

// ZoneSnapshot constructor implementation
ZoneSnapshot::ZoneSnapshot()
	: anchorId(0)
	, centerX(0.0f)
	, centerY(0.0f)
	, isMainBase(false)
	, barracks(0)
	, armsDealers(0)
	, anchorType(ZoneAnchorType::SupplyStash)
{
}

DebugZoneAnchorCandidate::DebugZoneAnchorCandidate()
	: anchorId(0)
	, x(0.0f)
	, y(0.0f)
	, isStructure(false)
	, underConstruction(false)
	, isSupplyStructure(false)
{
}

DebugZoneAnchor::DebugZoneAnchor()
	: anchorId(0)
	, x(0.0f)
	, y(0.0f)
	, isMainBase(false)
	, anchorType(ZoneAnchorType::SupplyStash)
{
}

AIControlAdapterZoneManager::AIControlAdapterZoneManager()
{
}

void AIControlAdapterZoneManager::Reset()
{
	m_threats.clear();
}

float AIControlAdapterZoneManager::DistanceSquared(float x1, float y1, float x2, float y2) const
{
	const float dx = x2 - x1;
	const float dy = y2 - y1;
	return (dx * dx) + (dy * dy);
}

int AIControlAdapterZoneManager::AssociateDamageWithZone(
	const StructureDamageEvent& damage,
	const std::vector<ZoneSnapshot>& zones,
	float zoneRadius) const
{
	if (zones.empty())
	{
		return -1;
	}

	const float associationRadius = std::max(160.0f, zoneRadius) * 1.25f;
	const float associationRadiusSq = associationRadius * associationRadius;

	int nearestZoneIndex = -1;
	float nearestDistSq = associationRadiusSq;

	for (std::size_t i = 0; i < zones.size(); ++i)
	{
		const ZoneSnapshot& zone = zones[i];
		const float distSq = DistanceSquared(damage.positionX, damage.positionY, zone.centerX, zone.centerY);

		if (distSq < nearestDistSq)
		{
			nearestDistSq = distSq;
			nearestZoneIndex = static_cast<int>(i);
		}
	}

	return nearestZoneIndex;
}

void AIControlAdapterZoneManager::UpdateThreats(const ZoneManagerInputs& inputs)
{
	if (inputs.zones == nullptr || inputs.damageEvents == nullptr)
	{
		return;
	}

	const std::vector<ZoneSnapshot>& zones = *inputs.zones;
	const std::vector<StructureDamageEvent>& damageEvents = *inputs.damageEvents;

	// Process recent damage events and update threat memory
	for (std::size_t i = 0; i < damageEvents.size(); ++i)
	{
		const StructureDamageEvent& damage = damageEvents[i];

		// Associate damage with nearest zone
		const int zoneIndex = AssociateDamageWithZone(damage, zones, inputs.zoneRadius);
		if (zoneIndex < 0 || zoneIndex >= static_cast<int>(zones.size()))
		{
			continue;
		}

		const ZoneSnapshot& zone = zones[static_cast<std::size_t>(zoneIndex)];

		// Update or create threat memory for this zone
		ZoneThreatMemory& threat = m_threats[zone.anchorId];
		threat.zoneAnchorId = zone.anchorId;
		threat.lastAttackedTick = inputs.currentTick;
		threat.lastAttackX = damage.positionX;
		threat.lastAttackY = damage.positionY;
		threat.damageDelta = damage.damageDelta;
		threat.severityLevel = damage.severityLevel.empty() ? "low" : damage.severityLevel;
		threat.damagedObjectId = damage.objectId;
	}

	// Expire old threats
	auto it = m_threats.begin();
	while (it != m_threats.end())
	{
		const unsigned int threatAge = inputs.currentTick - it->second.lastAttackedTick;
		if (threatAge > inputs.threatMemoryWindowMs)
		{
			it = m_threats.erase(it);
		}
		else
		{
			++it;
		}
	}
}

MostThreatenedZoneResult AIControlAdapterZoneManager::GetMostThreatenedZone(unsigned int currentTick) const
{
	MostThreatenedZoneResult result;

	if (m_threats.empty())
	{
		return result;
	}

	// Find the most recently attacked zone
	unsigned int newestAttackTick = 0;
	const ZoneThreatMemory* mostThreatenedZone = nullptr;

	for (auto it = m_threats.begin(); it != m_threats.end(); ++it)
	{
		const ZoneThreatMemory& threat = it->second;

		// Newer threat wins (or first threat if tied)
		if (mostThreatenedZone == nullptr || threat.lastAttackedTick > newestAttackTick)
		{
			newestAttackTick = threat.lastAttackedTick;
			mostThreatenedZone = &threat;
		}
	}

	if (mostThreatenedZone != nullptr)
	{
		result.hasThreat = true;
		result.zoneAnchorId = mostThreatenedZone->zoneAnchorId;
		result.threatAge = currentTick - mostThreatenedZone->lastAttackedTick;
		result.severityLevel = mostThreatenedZone->severityLevel;
		result.threatenedX = mostThreatenedZone->lastAttackX;
		result.threatenedY = mostThreatenedZone->lastAttackY;
	}

	return result;
}

bool AIControlAdapterZoneManager::ZoneHasEligibleProducers(const ZoneSnapshot& zone) const
{
	return zone.barracks > 0 || zone.armsDealers > 0;
}

std::vector<DebugZoneAnchor> AIControlAdapterZoneManager::BuildDebugOverlayZones(
	const std::vector<DebugZoneAnchorCandidate>& candidates,
	float minimumSpacing)
{
	std::vector<DebugZoneAnchor> zones;
	const float minSpacing = std::max(0.0f, minimumSpacing);
	const float minZoneDistSq = minSpacing * minSpacing;

	for (std::size_t candidateIdx = 0; candidateIdx < candidates.size(); ++candidateIdx)
	{
		const DebugZoneAnchorCandidate& candidate = candidates[candidateIdx];
		if (!candidate.isStructure || candidate.underConstruction)
		{
			continue;
		}
		if (!(candidate.name == "GLASupplyStash" || candidate.name == "GLABarracks" || candidate.name == "GLAArmsDealer"))
		{
			continue;
		}

		bool tooClose = false;
		for (std::size_t zoneIdx = 0; zoneIdx < zones.size(); ++zoneIdx)
		{
			const float dx = zones[zoneIdx].x - candidate.x;
			const float dy = zones[zoneIdx].y - candidate.y;
			if ((dx * dx) + (dy * dy) < minZoneDistSq)
			{
				tooClose = true;
				break;
			}
		}
		if (tooClose)
		{
			continue;
		}

		DebugZoneAnchor zone;
		zone.anchorId = candidate.anchorId;
		zone.x = candidate.x;
		zone.y = candidate.y;
		zone.anchorType = candidate.isSupplyStructure ? ZoneAnchorType::SupplyStash : ZoneAnchorType::MainBase;
		zones.push_back(zone);
	}

	if (!zones.empty())
	{
		zones[0].isMainBase = true;
	}

	return zones;
}

NearestProducerZoneResult AIControlAdapterZoneManager::FindNearestProducerZone(
	unsigned int threatenedZoneAnchorId,
	const std::vector<ZoneSnapshot>& zones) const
{
	NearestProducerZoneResult result;

	// Find threatened zone position
	const ZoneSnapshot* threatenedZone = nullptr;
	for (std::size_t i = 0; i < zones.size(); ++i)
	{
		if (zones[i].anchorId == threatenedZoneAnchorId)
		{
			threatenedZone = &zones[i];
			break;
		}
	}

	if (threatenedZone == nullptr)
	{
		result.fallbackReason = "threatened_zone_not_found";
		return result;
	}

	// Find nearest zone with eligible producers
	float nearestDistSq = -1.0f;
	const ZoneSnapshot* nearestProducerZone = nullptr;

	for (std::size_t i = 0; i < zones.size(); ++i)
	{
		const ZoneSnapshot& zone = zones[i];

		// Skip threatened zone itself (caller should check local producers first)
		if (zone.anchorId == threatenedZoneAnchorId)
		{
			continue;
		}

		// Check if zone has eligible producers
		if (!ZoneHasEligibleProducers(zone))
		{
			continue;
		}

		// Calculate distance
		const float distSq = DistanceSquared(
			threatenedZone->centerX, threatenedZone->centerY,
			zone.centerX, zone.centerY);

		// Track nearest
		if (nearestProducerZone == nullptr || distSq < nearestDistSq)
		{
			nearestDistSq = distSq;
			nearestProducerZone = &zone;
		}
	}

	if (nearestProducerZone != nullptr)
	{
		result.found = true;
		result.zoneAnchorId = nearestProducerZone->anchorId;
		result.zoneCenterX = nearestProducerZone->centerX;
		result.zoneCenterY = nearestProducerZone->centerY;
		result.distance = std::sqrt(nearestDistSq);
		result.fallbackReason = "nearest_producer_zone";
	}
	else
	{
		// No eligible producer zones found
		// Fall back to first available zone (deterministic fallback)
		for (std::size_t i = 0; i < zones.size(); ++i)
		{
			const ZoneSnapshot& zone = zones[i];
			if (zone.anchorId != threatenedZoneAnchorId)
			{
				result.found = true;
				result.zoneAnchorId = zone.anchorId;
				result.zoneCenterX = zone.centerX;
				result.zoneCenterY = zone.centerY;
				result.distance = -1.0f; // Unknown distance
				result.fallbackReason = "deterministic_fallback_no_producers";
				break;
			}
		}

		if (!result.found)
		{
			result.fallbackReason = "no_other_zones_available";
		}
	}

	return result;
}
