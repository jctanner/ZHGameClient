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

AIControlAdapterRaidLaunchSelection AIControlAdapterRaidManager::selectLaunchUnitIds(
	const std::vector<AIControlAdapterRaidLaunchUnitSnapshot>& units,
	float targetX,
	float targetY,
	int minimumUnits,
	int groupSize,
	bool includeScudLaunchers,
	float clusterRadius)
{
	AIControlAdapterRaidLaunchSelection result;
	if (groupSize <= 0 || clusterRadius <= 0.0f) return result;
	// Reserve roughly ten percent of a wave for siege, with at least one
	// escort and no more than four launchers. Early waves need not wait for tech.
	result.desiredScudLaunchers = includeScudLaunchers && groupSize > 1
		? std::min(4, std::min(groupSize - 1, groupSize / 10 + (groupSize % 10 != 0 ? 1 : 0)))
		: 0;
	const int minimumLocalGroup = std::min(std::max(1, minimumUnits), groupSize);
	const float radiusSq = clusterRadius * clusterRadius;
	const AIControlAdapterRaidLaunchUnitSnapshot* bestSeed = nullptr;
	int bestCount = 0;
	int bestScudCount = 0;
	float bestTargetDistanceSq = 0.0f;
	for (const auto& seed : units)
	{
		int escortCount = 0;
		int scudCount = 0;
		for (const auto& unit : units)
		{
			const float dx = unit.x - seed.x;
			const float dy = unit.y - seed.y;
			if (dx * dx + dy * dy > radiusSq) continue;
			if (unit.scudLauncher) ++scudCount;
			else ++escortCount;
		}
		// Excess artillery must not make a launcher-only cluster look viable.
		scudCount = escortCount > 0 ? std::min(scudCount, result.desiredScudLaunchers) : 0;
		const int count = std::min(groupSize, escortCount + scudCount);
		if (count < minimumLocalGroup) continue;
		const float dx = targetX - seed.x;
		const float dy = targetY - seed.y;
		const float targetDistanceSq = dx * dx + dy * dy;
		if (bestSeed == nullptr || count > bestCount ||
			(count == bestCount && (scudCount > bestScudCount ||
			 (scudCount == bestScudCount && (targetDistanceSq < bestTargetDistanceSq ||
			  (targetDistanceSq == bestTargetDistanceSq && seed.unitId < bestSeed->unitId))))))
		{
			bestSeed = &seed;
			bestCount = count;
			bestScudCount = scudCount;
			bestTargetDistanceSq = targetDistanceSq;
		}
	}
	if (bestSeed == nullptr) return result;
	result.clusterX = bestSeed->x;
	result.clusterY = bestSeed->y;
	std::vector<const AIControlAdapterRaidLaunchUnitSnapshot*> local;
	for (const auto& unit : units)
	{
		const float dx = unit.x - bestSeed->x;
		const float dy = unit.y - bestSeed->y;
		if (dx * dx + dy * dy <= radiusSq) local.push_back(&unit);
	}
	std::sort(local.begin(), local.end(), [&](const AIControlAdapterRaidLaunchUnitSnapshot* a,
		const AIControlAdapterRaidLaunchUnitSnapshot* b)
	{
		const float ax = a->x - bestSeed->x;
		const float ay = a->y - bestSeed->y;
		const float bx = b->x - bestSeed->x;
		const float by = b->y - bestSeed->y;
		const float aDist = ax * ax + ay * ay;
		const float bDist = bx * bx + by * by;
		return aDist != bDist ? aDist < bDist : a->unitId < b->unitId;
	});
	// Pick launchers before filling escort slots so distance ordering cannot
	// consume all seats with Quads/Buggies. Both roles share the same task.
	for (const auto* unit : local)
	{
		if (!unit->scudLauncher || static_cast<int>(result.scudLauncherUnitIds.size()) >= bestScudCount) continue;
		result.scudLauncherUnitIds.push_back(unit->unitId);
		result.unitIds.push_back(unit->unitId);
	}
	for (const auto* unit : local)
	{
		if (static_cast<int>(result.unitIds.size()) >= groupSize) break;
		if (!unit->scudLauncher) result.unitIds.push_back(unit->unitId);
	}
	result.reason = bestScudCount > 0 ? "scud_launchers_included" :
		(result.desiredScudLaunchers > 0 ? "no_local_scud_launchers" : "scud_slots_disabled");
	return result;
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

AIControlAdapterAttackAutomationDecision AIControlAdapterRaidManager::evaluateAttackAutomationGate(
	int eligibleUnits,
	int minimumUnits)
{
	AIControlAdapterAttackAutomationDecision decision;
	if (minimumUnits <= 0)
	{
		decision.shouldIssue = true;
		decision.reason = "minimum_disabled";
		return decision;
	}
	if (eligibleUnits < minimumUnits)
	{
		decision.shouldIssue = false;
		decision.reason = "not_enough_units";
		return decision;
	}
	decision.shouldIssue = true;
	decision.reason = "enough_units";
	return decision;
}

AIControlAdapterAttackTaskCreationDecision AIControlAdapterRaidManager::evaluateAttackTaskCreation(
	bool commandIssued,
	int assignedUnits,
	bool hasEquivalentActiveTask)
{
	AIControlAdapterAttackTaskCreationDecision decision;
	if (!commandIssued)
	{
		decision.reason = "command_failed";
		return decision;
	}
	if (assignedUnits <= 0)
	{
		decision.reason = "no_eligible_units";
		return decision;
	}
	if (hasEquivalentActiveTask)
	{
		decision.shouldReleaseUnits = true;
		decision.reason = "equivalent_active_task";
		return decision;
	}

	decision.shouldCreateTask = true;
	decision.reason = "attack_automation";
	return decision;
}
