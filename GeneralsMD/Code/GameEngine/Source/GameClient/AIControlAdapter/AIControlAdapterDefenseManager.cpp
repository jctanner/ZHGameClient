/**
 * AIControlAdapterDefenseManager.cpp
 *
 * Implementation of defense request management system.
 *
 * See AIControlAdapterDefenseManager.h for design principles and architecture.
 */

#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterDefenseManager.h"

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
