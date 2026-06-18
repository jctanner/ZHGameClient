/**
 * AIControlAdapterDefenseManager.cpp
 *
 * Implementation of defense request management system.
 *
 * See AIControlAdapterDefenseManager.h for design principles and architecture.
 */

#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterDefenseManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

#include <algorithm>
#include <cstring>

AIControlAdapterDefenseManager::AIControlAdapterDefenseManager()
{
}

void AIControlAdapterDefenseManager::Reset()
{
	// DefenseManager is stateless - nothing to reset
}

bool AIControlAdapterDefenseManager::ShouldRespondToThreat(
	const MostThreatenedZoneResult& threat,
	const DefenseManagerInputs& inputs) const
{
	// No defense if defense disabled by profile/bias
	if (!inputs.defenseEnabled)
	{
		return false;
	}

	// No defense if no active threat
	if (!threat.hasThreat)
	{
		return false;
	}

	// No defense if threat is too old (should have been expired by ZoneManager)
	// This is a safety check - ZoneManager should have already filtered old threats
	if (threat.threatAge > 45000)
	{
		return false;
	}

	// Respond to all fresh threats
	return true;
}

void AIControlAdapterDefenseManager::CalculateDefenderMix(
	const std::string& severityLevel,
	int currentSoldiers,
	int currentRpg,
	int currentQuads,
	int currentScorpions,
	DefenseRequest& outRequest) const
{
	// Start with basic defender goals based on severity
	// These are not hard caps - just desired minimums for defense

	if (severityLevel == "high")
	{
		// High severity: need strong defense mix
		outRequest.desiredSoldiers = 8;
		outRequest.desiredRpg = 4;
		outRequest.desiredQuads = 3;
		outRequest.desiredScorpions = 2;
	}
	else if (severityLevel == "medium")
	{
		// Medium severity: moderate defense
		outRequest.desiredSoldiers = 6;
		outRequest.desiredRpg = 3;
		outRequest.desiredQuads = 2;
		outRequest.desiredScorpions = 1;
	}
	else
	{
		// Low severity: minimal defense
		outRequest.desiredSoldiers = 4;
		outRequest.desiredRpg = 2;
		outRequest.desiredQuads = 1;
		outRequest.desiredScorpions = 0;
	}

	// Adjust based on what we already have
	// Don't request more if we're already at or above desired levels
	// (This is a simple heuristic - ProductionManager still makes final production decisions)
	if (currentSoldiers >= outRequest.desiredSoldiers)
	{
		outRequest.desiredSoldiers = 0;
	}
	if (currentRpg >= outRequest.desiredRpg)
	{
		outRequest.desiredRpg = 0;
	}
	if (currentQuads >= outRequest.desiredQuads)
	{
		outRequest.desiredQuads = 0;
	}
	if (currentScorpions >= outRequest.desiredScorpions)
	{
		outRequest.desiredScorpions = 0;
	}
}

const ZoneSnapshot* AIControlAdapterDefenseManager::FindZoneByAnchorId(
	unsigned int anchorId,
	const std::vector<ZoneSnapshot>& zones) const
{
	for (std::size_t i = 0; i < zones.size(); ++i)
	{
		if (zones[i].anchorId == anchorId)
		{
			return &zones[i];
		}
	}
	return nullptr;
}

DefenseRequest AIControlAdapterDefenseManager::ChooseDefenseProduction(
	const DefenseManagerInputs& inputs,
	const AIControlAdapterZoneManager& zoneManager)
{
	DefenseRequest request;

	// Step 1: Check if we have a threatened zone
	if (inputs.mostThreatenedZone == nullptr || !inputs.mostThreatenedZone->hasThreat)
	{
		return request;
	}

	const MostThreatenedZoneResult& threat = *inputs.mostThreatenedZone;

	// Step 2: Decide if threat warrants defense response
	if (!ShouldRespondToThreat(threat, inputs))
	{
		return request;
	}

	// Step 3: Find threatened zone
	if (inputs.zones == nullptr)
	{
		return request;
	}

	const std::vector<ZoneSnapshot>& zones = *inputs.zones;
	const ZoneSnapshot* threatenedZone = FindZoneByAnchorId(threat.zoneAnchorId, zones);
	if (threatenedZone == nullptr)
	{
		return request;
	}

	// Step 4: Calculate desired defender mix
	CalculateDefenderMix(
		threat.severityLevel,
		inputs.currentSoldiers,
		inputs.currentRpg,
		inputs.currentQuads,
		inputs.currentScorpions,
		request);

	// Step 5: Determine where to produce defenders
	// Check if threatened zone has local producers
	if (zoneManager.ZoneHasEligibleProducers(*threatenedZone))
	{
		// Local production in threatened zone
		request.active = true;
		request.threatenedZoneId = threat.zoneAnchorId;
		request.producerZoneId = threat.zoneAnchorId;
		request.isLocalProduction = true;
		request.fallbackReason = "";
		request.severityLevel = threat.severityLevel;
		request.threatAge = threat.threatAge;
	}
	else
	{
		// No local producers - find nearest eligible producer zone
		const NearestProducerZoneResult nearestProducer = zoneManager.FindNearestProducerZone(
			threat.zoneAnchorId,
			zones);

		if (nearestProducer.found)
		{
			request.active = true;
			request.threatenedZoneId = threat.zoneAnchorId;
			request.producerZoneId = nearestProducer.zoneAnchorId;
			request.isLocalProduction = false;
			request.fallbackReason = nearestProducer.fallbackReason;
			request.severityLevel = threat.severityLevel;
			request.threatAge = threat.threatAge;
		}
		else
		{
			// No eligible producer zones found anywhere
			// Defense request not active - can't produce defenders
			return request;
		}
	}

	return request;
}

AIControlAdapterZoneDefenseFloorDecision AIControlAdapterDefenseManager::ResolveZoneDefenseFloor(
	float unitX,
	float unitY,
	float zoneRadius,
	const std::vector<AIControlAdapterDefenseZoneSnapshot>& zones,
	const std::vector<AIControlAdapterZoneDefenseReserveSnapshot>& reserves)
{
	AIControlAdapterZoneDefenseFloorDecision decision;
	if (zones.empty() || reserves.empty())
	{
		return decision;
	}
	const float radius = std::max<float>(160.0f, zoneRadius) * 1.25f;
	const float radiusSq = radius * radius;
	float bestDistSq = radiusSq;
	for (const AIControlAdapterDefenseZoneSnapshot& zone : zones)
	{
		const float dx = unitX - zone.centerX;
		const float dy = unitY - zone.centerY;
		const float distSq = dx * dx + dy * dy;
		if (distSq <= bestDistSq)
		{
			bestDistSq = distSq;
			decision.hasZone = true;
			decision.zoneId = zone.anchorId;
			decision.isMainBase = zone.isMainBase;
		}
	}
	if (!decision.hasZone || decision.zoneId == 0u)
	{
		decision.hasZone = false;
		return decision;
	}
	const AIControlAdapterZoneDefenseReserveSnapshot* reserve = nullptr;
	for (const AIControlAdapterZoneDefenseReserveSnapshot& candidate : reserves)
	{
		if (candidate.zoneId == decision.zoneId)
		{
			reserve = &candidate;
			break;
		}
	}
	if (reserve == nullptr)
	{
		decision.hasZone = false;
		decision.zoneId = 0u;
		decision.isMainBase = false;
		return decision;
	}
	decision.protectedByFloor =
		reserve->surplus <= 0 ||
		reserve->deficit > 0 ||
		reserve->activeThreat ||
		decision.isMainBase;
	decision.canRelaxForScout =
		!decision.isMainBase &&
		!reserve->activeThreat &&
		reserve->deficit <= 0;
	return decision;
}

int AIControlAdapterDefenseManager::ThreatSeverityForLevel(const std::string& level)
{
	if (level == "critical")
	{
		return 4;
	}
	if (level == "high")
	{
		return 3;
	}
	if (level == "medium")
	{
		return 2;
	}
	if (level == "low")
	{
		return 1;
	}
	return 0;
}

bool AIControlAdapterDefenseManager::IsZoneThreatFresh(
	unsigned int now,
	unsigned int lastSeenTick,
	unsigned int freshnessMs)
{
	const unsigned int ageCutoff = now - freshnessMs;
	return AIControlAdapterHasTickElapsed(ageCutoff, lastSeenTick);
}

const char* AIControlAdapterDefenseManager::TransientStrikeReleaseReason(
	const std::string& sourceType,
	int localEnemyCount,
	int enemyArtilleryCount)
{
	if (localEnemyCount > 0 || enemyArtilleryCount > 0)
	{
		return nullptr;
	}
	if (sourceType == "wmd_strike")
	{
		return "wmd_strike_no_local_enemy";
	}
	if (sourceType == "special_power_strike")
	{
		return "special_power_no_local_enemy";
	}
	if (sourceType == "unknown_damage")
	{
		return "unknown_damage_no_local_enemy";
	}
	return nullptr;
}
