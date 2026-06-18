#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterRaidManager.h"

#include <algorithm>
#include <cmath>
#include <cctype>

bool AIControlAdapterRaidManager::containsIgnoreCase(const std::string& haystack, const std::string& needle)
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

std::vector<unsigned int> AIControlAdapterRaidManager::selectProbeUnitIds(
	const std::vector<AIControlAdapterRaidProbeUnitSnapshot>& units,
	const CombatTaskWaypoint& waypoint,
	int maxProbeUnits,
	float maxDistanceFromAnchor)
{
	std::vector<CombatTaskProbeCandidate> candidates;
	candidates.reserve(units.size());
	for (const AIControlAdapterRaidProbeUnitSnapshot& unit : units)
	{
		CombatTaskProbeCandidate candidate;
		candidate.unitId = unit.unitId;
		candidate.alive = unit.alive;
		candidate.fast =
			containsIgnoreCase(unit.templateName, "quad") ||
			containsIgnoreCase(unit.templateName, "buggy") ||
			containsIgnoreCase(unit.templateName, "technical") ||
			containsIgnoreCase(unit.templateName, "scorpion");
		candidate.combatCapable =
			(unit.vehicle || unit.aircraft) &&
			unit.ableToAttack &&
			!unit.underConstruction;
		candidate.worker = unit.worker || unit.harvester;
		candidate.captureTaskReserved = unit.taskReserved;
		candidate.constructionTaskReserved = unit.taskReserved;
		candidate.zoneDefenseFloorReserved = unit.zoneDefenseFloorReserved;
		candidate.criticalBaseDefenseReserved = unit.zoneDefenseFloorReserved;
		if (unit.hasPosition)
		{
			const float dx = unit.x - waypoint.position.x;
			const float dy = unit.y - waypoint.position.y;
			candidate.distanceFromAnchor = std::sqrt(dx * dx + dy * dy);
		}
		else
		{
			candidate.distanceFromAnchor = 999999.0f;
		}
		candidates.push_back(candidate);
	}
	return selectCombatTaskProbeUnits(candidates, maxProbeUnits, maxDistanceFromAnchor);
}

Coord3D AIControlAdapterRaidManager::computeProbeTarget(
	const std::vector<CombatTaskWaypoint>& waypoints,
	const Coord3D& finalTarget,
	int waypointIndex,
	float maxProbeAdvance)
{
	Coord3D target = finalTarget;
	if (waypoints.empty())
	{
		return target;
	}
	const int clampedIndex = std::max(0, std::min(waypointIndex, static_cast<int>(waypoints.size()) - 1));
	const CombatTaskWaypoint& waypoint = waypoints[static_cast<std::size_t>(clampedIndex)];
	const Coord3D desired = clampedIndex + 1 < static_cast<int>(waypoints.size())
		? waypoints[static_cast<std::size_t>(clampedIndex + 1)].position
		: finalTarget;
	target = desired;
	const float dx = desired.x - waypoint.position.x;
	const float dy = desired.y - waypoint.position.y;
	const float distance = std::sqrt(dx * dx + dy * dy);
	if (distance > maxProbeAdvance && distance > 1.0f)
	{
		const float scale = maxProbeAdvance / distance;
		target.x = waypoint.position.x + dx * scale;
		target.y = waypoint.position.y + dy * scale;
		target.z = waypoint.position.z;
	}
	return target;
}

AIControlAdapterRaidModeDecision AIControlAdapterRaidManager::evaluateRaidModePolicy(
	const std::string& currentRaidMode,
	int infantryLive,
	int vehicleLive,
	int initialUnitCount,
	int minimumViableCount,
	bool cohesionWaitStarted,
	unsigned int cohesionWaitMs,
	unsigned int mixedInfantryTimeoutMs)
{
	AIControlAdapterRaidModeDecision decision;
	decision.raidMode = currentRaidMode;
	if (decision.raidMode.empty())
	{
		decision.raidMode = (infantryLive > 0 && (vehicleLive > 0 || initialUnitCount > infantryLive))
			? "mixed_local"
			: (vehicleLive > 0 ? "vehicle" : "infantry");
	}

	const int minimumVehicles = std::max(1, minimumViableCount);
	if (decision.raidMode == "vehicle" && infantryLive > 0 && vehicleLive > 0)
	{
		decision.degradeToVehicle = true;
		decision.reason = "long_distance";
		return decision;
	}

	const bool mixedInfantryTimeout =
		decision.raidMode == "mixed_local" &&
		infantryLive > 0 &&
		vehicleLive >= minimumVehicles &&
		cohesionWaitStarted &&
		cohesionWaitMs >= mixedInfantryTimeoutMs;
	if (mixedInfantryTimeout)
	{
		decision.raidMode = "vehicle";
		decision.degradeToVehicle = true;
		decision.reason = "infantry_timeout";
		return decision;
	}

	if (decision.raidMode == "mixed_local" &&
		vehicleLive < minimumVehicles &&
		cohesionWaitStarted &&
		cohesionWaitMs >= mixedInfantryTimeoutMs)
	{
		decision.fail = true;
		decision.reason = "infantry_timeout_no_survivors";
		return decision;
	}

	if (decision.raidMode == "vehicle" && vehicleLive < minimumVehicles)
	{
		decision.fail = true;
		decision.reason = "no_viable_vehicle_group";
		return decision;
	}

	return decision;
}
