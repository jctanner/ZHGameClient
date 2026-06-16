#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCombatTask.h"
#include <algorithm>
#include <cmath>

AIControlAdapterCombatTaskManager::AIControlAdapterCombatTaskManager()
{
}

const char* combatTaskTypeName(CombatTaskType type)
{
	switch (type)
	{
	case CombatTaskType::Attack:
		return "attack";
	case CombatTaskType::Defense:
		return "defense";
	case CombatTaskType::Guard:
		return "guard";
	case CombatTaskType::Scout:
		return "scout";
	default:
		return "unknown";
	}
}

const char* combatTaskStateName(CombatTaskState state)
{
	switch (state)
	{
	case CombatTaskState::Assembling:
		return "assembling";
	case CombatTaskState::Forming:
		return "forming";
	case CombatTaskState::Moving:
		return "moving";
	case CombatTaskState::MovingToStage:
		return "moving_to_stage";
	case CombatTaskState::WaitingForCohesion:
		return "waiting_for_cohesion";
	case CombatTaskState::Advancing:
		return "advancing";
	case CombatTaskState::Engaging:
		return "engaging";
	case CombatTaskState::Attacking:
		return "attacking";
	case CombatTaskState::Reassessing:
		return "reassessing";
	case CombatTaskState::Complete:
		return "complete";
	case CombatTaskState::Failed:
		return "failed";
	case CombatTaskState::Expired:
		return "expired";
	default:
		return "unknown";
	}
}

const char* combatTaskProbeStateName(CombatTaskProbeState state)
{
	switch (state)
	{
	case CombatTaskProbeState::Inactive:
		return "inactive";
	case CombatTaskProbeState::Moving:
		return "moving";
	case CombatTaskProbeState::Scouting:
		return "scouting";
	case CombatTaskProbeState::Complete:
		return "complete";
	case CombatTaskProbeState::Timeout:
		return "timeout";
	case CombatTaskProbeState::Cancelled:
		return "cancelled";
	default:
		return "unknown";
	}
}

std::vector<CombatTaskWaypoint> buildDirectRaidWaypoints(
	const Coord3D& origin,
	const Coord3D& target,
	Real stageRadius)
{
	std::vector<CombatTaskWaypoint> result;
	const Real dx = target.x - origin.x;
	const Real dy = target.y - origin.y;
	const Real distance = std::sqrt(dx * dx + dy * dy);
	const Real radius = std::max<Real>(250.0f, std::min<Real>(350.0f, stageRadius));

	if (distance <= 1.0f)
	{
		CombatTaskWaypoint finalWaypoint;
		finalWaypoint.position = target;
		finalWaypoint.radius = radius;
		result.push_back(finalWaypoint);
		return result;
	}

	const Real fractions[] = {0.35f, 0.76f, 1.0f};
	for (int i = 0; i < 3; ++i)
	{
		CombatTaskWaypoint waypoint;
		waypoint.position.x = origin.x + (dx * fractions[i]);
		waypoint.position.y = origin.y + (dy * fractions[i]);
		waypoint.position.z = 0.0f;
		waypoint.radius = i == 2 ? std::max<Real>(320.0f, radius) : radius;
		result.push_back(waypoint);
	}
	return result;
}

CombatTaskCohesionDecision evaluateCombatTaskCohesion(
	const CombatTaskCohesionInput& input)
{
	CombatTaskCohesionDecision decision;
	if (input.liveAssigned < input.minimumViable)
	{
		decision.shouldFail = true;
		decision.reason = "below_minimum_viable";
		return decision;
	}

	decision.requiredQuorum = std::max(1, (input.liveAssigned * 70 + 99) / 100);
	if (input.infantryLive > 0)
	{
		decision.infantryRequiredQuorum = std::max(1, (input.infantryLive * 50 + 99) / 100);
	}

	const bool groupQuorum = input.arrived >= decision.requiredQuorum;
	const bool infantryQuorum =
		!input.requireInfantryQuorum ||
		input.infantryLive <= 0 ||
		input.infantryArrived >= decision.infantryRequiredQuorum;
	if (groupQuorum && infantryQuorum)
	{
		decision.shouldAdvance = true;
		decision.reason = "quorum_reached";
		return decision;
	}

	if (input.timeout)
	{
		decision.shouldAdvance = true;
		decision.reason = "timeout_advancing";
		return decision;
	}

	decision.reason = input.infantryLive > 0 && !infantryQuorum
		? "waiting_for_infantry_quorum"
		: "waiting_for_cohesion";
	return decision;
}

CombatTaskRaidMixDecision evaluateCombatTaskRaidMixPolicy(
	int vehicleCount,
	int infantryCount,
	Real targetDistance,
	Real localDistance)
{
	CombatTaskRaidMixDecision decision;
	const int vehicles = std::max(0, vehicleCount);
	const int infantry = std::max(0, infantryCount);
	const bool longDistance = targetDistance > std::max<Real>(256.0f, localDistance);
	if (vehicles > 0 && longDistance)
	{
		decision.mode = "vehicle";
		decision.allowVehicles = true;
		decision.shouldLaunch = true;
		decision.reason = infantry > 0 ? "long_distance_vehicle_only" : "vehicle_raid";
		return decision;
	}
	if (vehicles > 0 && infantry > 0)
	{
		decision.mode = "mixed_local";
		decision.allowVehicles = true;
		decision.allowInfantry = true;
		decision.shouldLaunch = true;
		decision.reason = "local_mixed_push";
		return decision;
	}
	if (vehicles > 0)
	{
		decision.mode = "vehicle";
		decision.allowVehicles = true;
		decision.shouldLaunch = true;
		decision.reason = "vehicle_raid";
		return decision;
	}
	if (infantry > 0 && !longDistance)
	{
		decision.mode = "infantry";
		decision.allowInfantry = true;
		decision.shouldLaunch = true;
		decision.reason = "local_infantry_push";
		return decision;
	}
	if (infantry > 0)
	{
		decision.mode = "hold";
		decision.reason = "long_distance_infantry_blocked";
		return decision;
	}
	decision.reason = "no_viable_units";
	return decision;
}

bool shouldLogCombatTaskCohesion(
	CombatTask& task,
	DWORD currentTick,
	DWORD heartbeatMs,
	int countDeltaThreshold)
{
	const int threshold = std::max(1, countDeltaThreshold);
	const bool firstLog = !task.cohesionLogSnapshotValid;
	const bool stateChanged = task.lastLoggedState != task.state;
	const bool reasonChanged = task.lastLoggedReason != task.cohesionReason;
	const bool waypointChanged = task.lastLoggedWaypointIndex != task.currentWaypointIndex;
	const bool deadChanged = task.lastLoggedDeadCount != task.confirmedDeadCount;
	const bool arrivedThreshold =
		std::abs(task.arrivedCount - task.lastLoggedArrivedCount) >= threshold;
	const bool missingThreshold =
		std::abs(task.missingCount - task.lastLoggedMissingCount) >= threshold;
	const bool heartbeat =
		task.lastCohesionLogTick == 0 || currentTick - task.lastCohesionLogTick >= heartbeatMs;

	if (!(firstLog || stateChanged || reasonChanged || waypointChanged || deadChanged ||
		arrivedThreshold || missingThreshold || heartbeat))
	{
		return false;
	}

	task.cohesionLogSnapshotValid = true;
	task.lastCohesionLogTick = currentTick;
	task.lastLoggedState = task.state;
	task.lastLoggedReason = task.cohesionReason;
	task.lastLoggedWaypointIndex = task.currentWaypointIndex;
	task.lastLoggedArrivedCount = task.arrivedCount;
	task.lastLoggedMissingCount = task.missingCount;
	task.lastLoggedDeadCount = task.confirmedDeadCount;
	return true;
}

bool shouldLogCombatTaskScoutState(
	CombatTask& task,
	DWORD currentTick,
	const std::string& stateName,
	const std::string& reason,
	int waypointIndex,
	int freshTargets,
	DWORD heartbeatMs)
{
	const bool firstLog = !task.scoutStateLogSnapshotValid;
	const bool stateChanged = task.lastScoutLoggedState != stateName;
	const bool reasonChanged = task.lastScoutLoggedReason != reason;
	const bool waypointChanged = task.lastScoutLoggedWaypointIndex != waypointIndex;
	const bool freshTargetsChanged = task.lastScoutLoggedFreshTargets != freshTargets;
	const bool heartbeat =
		task.lastScoutStateLogTick == 0 || currentTick - task.lastScoutStateLogTick >= heartbeatMs;

	if (!(firstLog || stateChanged || reasonChanged || waypointChanged || freshTargetsChanged || heartbeat))
	{
		return false;
	}

	task.scoutStateLogSnapshotValid = true;
	task.lastScoutStateLogTick = currentTick;
	task.lastScoutLoggedState = stateName;
	task.lastScoutLoggedReason = reason;
	task.lastScoutLoggedWaypointIndex = waypointIndex;
	task.lastScoutLoggedFreshTargets = freshTargets;
	return true;
}

CombatTaskProbeDecision evaluateCombatTaskProbePolicy(
	const CombatTaskProbePolicyInput& input)
{
	CombatTaskProbeDecision decision;
	const bool activeProbe =
		input.probeState == CombatTaskProbeState::Moving ||
		input.probeState == CombatTaskProbeState::Scouting;

	if (input.quorumReached)
	{
		if (activeProbe)
		{
			decision.shouldCancel = true;
			decision.mode = "cancel";
			decision.reason = "main_quorum_reached";
			return decision;
		}
		decision.mode = "complete";
		decision.reason = "main_quorum_reached";
		return decision;
	}

	if (activeProbe)
	{
		if (input.freshStrategicTargets > 0)
		{
			decision.shouldCancel = true;
			decision.mode = "complete";
			decision.reason = "fresh_targets_found";
			return decision;
		}
		if (input.probeStartedTick > 0 &&
			input.currentTick >= input.probeStartedTick + input.probeTimeoutMs)
		{
			decision.shouldTimeout = true;
			decision.mode = "timeout";
			decision.reason = "probe_timeout";
			return decision;
		}
		decision.mode = "active";
		decision.reason = "probe_active";
		return decision;
	}

	if (!input.holdingForCohesion)
	{
		decision.reason = "not_waiting_for_cohesion";
		return decision;
	}
	if (input.freshStrategicTargets > 0)
	{
		decision.reason = "fresh_targets_available";
		return decision;
	}
	if (input.cohesionWaitMs < input.minWaitMs)
	{
		decision.reason = "cohesion_wait_below_threshold";
		return decision;
	}
	if (input.lastProbeEndTick > 0 &&
		input.currentTick < input.lastProbeEndTick + input.cooldownMs)
	{
		decision.reason = "probe_cooldown";
		return decision;
	}
	if (input.eligibleProbeUnits <= 0)
	{
		decision.reason = "no_eligible_probe_units";
		return decision;
	}

	decision.shouldLaunch = true;
	decision.mode = "launch";
	decision.reason = "stale_enemy_memory_probe";
	return decision;
}

std::vector<unsigned int> selectCombatTaskProbeUnits(
	const std::vector<CombatTaskProbeCandidate>& candidates,
	int maxProbeUnits,
	Real maxDistanceFromAnchor)
{
	std::vector<CombatTaskProbeCandidate> eligible;
	for (const CombatTaskProbeCandidate& candidate : candidates)
	{
		if (candidate.unitId == 0u ||
			!candidate.alive ||
			!candidate.combatCapable ||
			candidate.worker ||
			candidate.captureTaskReserved ||
			candidate.constructionTaskReserved ||
			candidate.zoneDefenseFloorReserved ||
			candidate.criticalBaseDefenseReserved ||
			candidate.distanceFromAnchor > maxDistanceFromAnchor)
		{
			continue;
		}
		eligible.push_back(candidate);
	}

	std::stable_sort(eligible.begin(), eligible.end(), [](const CombatTaskProbeCandidate& lhs, const CombatTaskProbeCandidate& rhs) -> bool
	{
		if (lhs.fast != rhs.fast)
		{
			return lhs.fast && !rhs.fast;
		}
		if (lhs.distanceFromAnchor != rhs.distanceFromAnchor)
		{
			return lhs.distanceFromAnchor < rhs.distanceFromAnchor;
		}
		return lhs.unitId < rhs.unitId;
	});

	std::vector<unsigned int> result;
	const int limit = std::max(0, maxProbeUnits);
	for (int i = 0; i < limit && i < static_cast<int>(eligible.size()); ++i)
	{
		result.push_back(eligible[static_cast<std::size_t>(i)].unitId);
	}
	return result;
}

CombatTaskScoutPolicyDecision evaluateCombatTaskScoutPolicy(
	const CombatTaskScoutPolicyInput& input)
{
	CombatTaskScoutPolicyDecision decision;
	if (input.activeScoutTasks >= std::max(1, input.maxActiveScoutTasks))
	{
		decision.mode = "active";
		decision.reason = input.scudTargetRefreshNeeded ? "scud_refresh_active" : "active_scout_cap";
		return decision;
	}
	if (input.availableScouts <= 0)
	{
		decision.reason = "no_available_scouts";
		return decision;
	}
	if (input.objectiveCount <= 0)
	{
		decision.reason = "no_scout_objectives";
		return decision;
	}
	if (input.scudTargetRefreshNeeded && input.likelyRegionsRemaining > 0)
	{
		decision.shouldLaunch = true;
		decision.mode = "launch";
		decision.reason = input.lastScoutRevealAgeMs > 120000u ? "scud_target_refresh_stale" : "scud_target_refresh";
		return decision;
	}
	if (input.scudTargetRefreshNeeded && input.likelyRegionsRemaining <= 0 && input.lastScoutRevealAgeMs <= 120000u)
	{
		decision.reason = "likely_regions_recently_swept";
		return decision;
	}
	if (input.freshStrategicTargets > 0 && input.combatPressureHigh)
	{
		decision.reason = "fresh_targets_combat_pressure";
		return decision;
	}
	if (input.freshStrategicTargets > 0 && input.staleStrategicTargets <= 0 && input.readyScudStorms <= 0)
	{
		decision.reason = "fresh_targets_available";
		return decision;
	}
	if (input.readyScudStorms > 0 && input.freshStrategicTargets <= 0)
	{
		decision.shouldLaunch = true;
		decision.mode = "launch";
		decision.reason = "scud_target_starved";
		return decision;
	}
	if (input.staleStrategicTargets > 0 && input.freshStrategicTargets <= 0)
	{
		decision.shouldLaunch = true;
		decision.mode = "launch";
		decision.reason = "stale_enemy_memory";
		return decision;
	}
	if (input.freshStrategicTargets <= 0)
	{
		decision.shouldLaunch = true;
		decision.mode = "launch";
		decision.reason = "uncovered_enemy_area";
		return decision;
	}
	return decision;
}

CombatTaskScoutObjective selectCombatTaskScoutObjective(
	const std::vector<CombatTaskScoutObjective>& objectives)
{
	CombatTaskScoutObjective best;
	bool hasBest = false;
	for (const CombatTaskScoutObjective& objective : objectives)
	{
		if (!hasBest ||
			objective.priority > best.priority ||
			(objective.priority == best.priority && objective.objectiveId < best.objectiveId))
		{
			best = objective;
			hasBest = true;
		}
	}
	return best;
}

namespace
{
	float scoutCross2D(float ax, float ay, float bx, float by, float cx, float cy)
	{
		return ((bx - ax) * (cy - ay)) - ((by - ay) * (cx - ax));
	}

	bool scoutSegmentsIntersect(
		float ax,
		float ay,
		float bx,
		float by,
		float cx,
		float cy,
		float dx,
		float dy)
	{
		const float c1 = scoutCross2D(ax, ay, bx, by, cx, cy);
		const float c2 = scoutCross2D(ax, ay, bx, by, dx, dy);
		const float c3 = scoutCross2D(cx, cy, dx, dy, ax, ay);
		const float c4 = scoutCross2D(cx, cy, dx, dy, bx, by);
		return ((c1 > 0.0f && c2 < 0.0f) || (c1 < 0.0f && c2 > 0.0f))
			&& ((c3 > 0.0f && c4 < 0.0f) || (c3 < 0.0f && c4 > 0.0f));
	}

	bool scoutCrossesBarrier(
		const CombatTaskScoutRandomInput& input,
		float fromX,
		float fromY,
		float toX,
		float toY)
	{
		for (const CombatTaskScoutRandomBarrier& barrier : input.barriers)
		{
			if (scoutSegmentsIntersect(fromX, fromY, toX, toY, barrier.ax, barrier.ay, barrier.bx, barrier.by))
			{
				return true;
			}
		}
		return false;
	}
}

CombatTaskScoutRandomDecision selectCombatTaskRandomRevealObjective(
	const CombatTaskScoutRandomInput& input)
{
	CombatTaskScoutRandomDecision decision;
	if (!input.coverageThin)
	{
		decision.reason = "coverage_fresh";
		return decision;
	}
	if (input.origins.empty())
	{
		decision.reason = "no_random_origin";
		return decision;
	}

	struct Direction
	{
		float x;
		float y;
		const char* name;
	};
	const Direction directions[] = {
		{ 1.0f, 0.0f, "east" },
		{ 0.70710678f, 0.70710678f, "northeast" },
		{ 0.0f, 1.0f, "north" },
		{ -0.70710678f, 0.70710678f, "northwest" },
		{ -1.0f, 0.0f, "west" },
		{ -0.70710678f, -0.70710678f, "southwest" },
		{ 0.0f, -1.0f, "south" },
		{ 0.70710678f, -0.70710678f, "southeast" }
	};
	const unsigned int directionCount = static_cast<unsigned int>(sizeof(directions) / sizeof(directions[0]));
	const unsigned int originOffset = input.seed % static_cast<unsigned int>(input.origins.size());
	const unsigned int directionOffset = (input.seed / 7u) % directionCount;
	const float minX = std::min(input.minX, input.maxX);
	const float maxX = std::max(input.minX, input.maxX);
	const float minY = std::min(input.minY, input.maxY);
	const float maxY = std::max(input.minY, input.maxY);
	const float distance = std::max(300.0f, input.revealDistance);

	for (unsigned int originScan = 0; originScan < input.origins.size(); ++originScan)
	{
		const CombatTaskScoutRandomOrigin& origin = input.origins[(originOffset + originScan) % input.origins.size()];
		for (unsigned int directionScan = 0; directionScan < directionCount; ++directionScan)
		{
			const Direction& direction = directions[(directionOffset + directionScan) % directionCount];
			++decision.candidateCount;
			float x = origin.position.x + (direction.x * distance);
			float y = origin.position.y + (direction.y * distance);
			x = std::max(minX, std::min(maxX, x));
			y = std::max(minY, std::min(maxY, y));
			const unsigned int objectiveId =
				static_cast<unsigned int>(700000u + ((origin.zoneId % 997u) * 8u) + ((directionOffset + directionScan) % directionCount));
			if (objectiveId == input.previousObjectiveId)
			{
				++decision.rejectedCount;
				decision.reason = "repeat_objective";
				continue;
			}
			const float dx = x - origin.position.x;
			const float dy = y - origin.position.y;
			if ((dx * dx) + (dy * dy) < 250.0f * 250.0f)
			{
				++decision.rejectedCount;
				decision.reason = "clamped_too_close";
				continue;
			}
			if (scoutCrossesBarrier(input, origin.position.x, origin.position.y, x, y))
			{
				++decision.rejectedCount;
				decision.reason = "impassable_barrier";
				continue;
			}

			decision.selected = true;
			decision.reason = direction.name;
			decision.objective.objectiveId = objectiveId;
			decision.objective.position.x = x;
			decision.objective.position.y = y;
			decision.objective.position.z = origin.position.z;
			decision.objective.priority = origin.mainBase ? 35 : 45 + origin.priority;
			decision.objective.randomReveal = true;
			decision.objective.originZoneId = origin.zoneId;
			decision.objective.directionX = direction.x;
			decision.objective.directionY = direction.y;
			decision.objective.reason = "random_reveal";
			return decision;
		}
	}

	return decision;
}

std::vector<unsigned int> selectCombatTaskScoutUnits(
	const std::vector<CombatTaskScoutCandidate>& candidates,
	int maxScoutUnits,
	bool relaxDefenseFloor)
{
	std::vector<CombatTaskScoutCandidate> eligible;
	for (const CombatTaskScoutCandidate& candidate : candidates)
	{
		if (candidate.unitId == 0u ||
			!candidate.alive ||
			!candidate.combatCapable ||
			candidate.worker ||
			candidate.captureTaskReserved ||
			candidate.constructionTaskReserved ||
			candidate.garrisonReserved ||
			candidate.combatTaskReserved ||
			candidate.artilleryCounterReserved)
		{
			continue;
		}
		if (candidate.zoneDefenseFloorReserved && !relaxDefenseFloor)
		{
			continue;
		}
		eligible.push_back(candidate);
	}
	std::stable_sort(eligible.begin(), eligible.end(), [](const CombatTaskScoutCandidate& lhs, const CombatTaskScoutCandidate& rhs) -> bool
	{
		if (lhs.preference != rhs.preference)
		{
			return lhs.preference > rhs.preference;
		}
		if (lhs.fast != rhs.fast)
		{
			return lhs.fast && !rhs.fast;
		}
		if (lhs.distanceFromOrigin != rhs.distanceFromOrigin)
		{
			return lhs.distanceFromOrigin < rhs.distanceFromOrigin;
		}
		return lhs.unitId < rhs.unitId;
	});

	std::vector<unsigned int> result;
	const int limit = std::max(0, maxScoutUnits);
	for (int i = 0; i < limit && i < static_cast<int>(eligible.size()); ++i)
	{
		result.push_back(eligible[static_cast<std::size_t>(i)].unitId);
	}
	return result;
}

CombatTaskScudTargetRefreshOverrideDecision evaluateCombatTaskScudTargetRefreshOverride(
	const CombatTaskScudTargetRefreshOverrideInput& input)
{
	CombatTaskScudTargetRefreshOverrideDecision decision;
	if (!input.scudTargetRefreshNeeded)
	{
		decision.reason = "not_needed";
		return decision;
	}
	if (input.activeScoutTasks >= std::max(1, input.maxActiveScoutTasks))
	{
		decision.reason = "active_scout_cap";
		return decision;
	}
	if (!input.noAvailableScouts || !input.defenseFloorBlocked)
	{
		decision.reason = input.noAvailableScouts ? "no_units" : "scouts_available";
		return decision;
	}
	if (input.mainBaseCritical)
	{
		decision.reason = "critical_defense";
		return decision;
	}
	if (input.criticalZoneDefense)
	{
		decision.reason = "critical_zone_defense";
		return decision;
	}

	const bool highCash = input.money >= input.reserveCash + 10000u;
	if (!highCash)
	{
		decision.reason = "cash_below_override";
		return decision;
	}

	const bool scoutPoolSaturated =
		input.scoutPoolAssigned >= std::max(1, input.scoutPoolLive) ||
		input.scoutPoolQueued > 0;
	if (input.armsDealerReady && scoutPoolSaturated && input.scoutPoolQueued <= 0)
	{
		decision.allowProduction = true;
		decision.mode = "produce";
		decision.reason = "defense_floor_relaxed";
		return decision;
	}

	decision.allowBorrow = true;
	decision.relaxDefenseFloor = true;
	decision.mode = "borrow";
	decision.reason = input.scoutPoolQueued > 0 ? "production_queued" : "defense_floor_relaxed";
	return decision;
}

CombatTaskScoutPoolDecision evaluateCombatTaskScoutPool(
	const CombatTaskScoutPoolInput& input)
{
	CombatTaskScoutPoolDecision decision;
	if (!input.armsDealerReady)
	{
		decision.reason = "arms_dealer_missing";
		return decision;
	}
	if (input.freshScoutCoverage && !input.scudTargetStarved)
	{
		decision.reason = "coverage_fresh";
		return decision;
	}

	const unsigned int protectedReserve = input.reserveCash;
	const unsigned int highCashThreshold = protectedReserve + 10000u;
	decision.desiredTechnicals = input.money >= highCashThreshold ? 4 : 2;
	if (input.scudTargetStarved)
	{
		decision.reason = decision.desiredTechnicals >= 4 ? "scud_target_starved_high_cash" : "scud_target_starved";
	}
	else
	{
		decision.reason = decision.desiredTechnicals >= 4 ? "stale_coverage_high_cash" : "stale_coverage";
	}

	const int effectiveTechnicals = std::max(0, input.liveTechnicals) + std::max(0, input.queuedTechnicals);
	decision.productionNeeded = effectiveTechnicals < decision.desiredTechnicals;
	if (!decision.productionNeeded)
	{
		decision.reason = "scout_pool_satisfied";
	}
	return decision;
}

unsigned int AIControlAdapterCombatTaskManager::createTask(
	CombatTaskType type,
	const std::vector<unsigned int>& unitIds,
	const Coord3D& targetPosition,
	const std::string& owner,
	const std::string& reason,
	unsigned int timeoutMs)
{
	const unsigned int taskId = nextTaskId++;
	const DWORD now = ::GetTickCount();

	CombatTask task;
	task.taskId = taskId;
	task.type = type;
	task.state = CombatTaskState::Assembling;
	task.assignedUnitIds = unitIds;
	task.targetPosition = targetPosition;
	task.targetObjectId = 0;
	task.owner = owner;
	task.reason = reason;
	task.createdTick = now;
	task.lastCommandTick = now;
	task.lastProgressTick = now;
	task.timeoutTick = now + timeoutMs;
	task.initialUnitCount = static_cast<int>(unitIds.size());
	// Set minimum viable count to 30% of initial, at least 1
	task.minimumViableCount = std::max(1, task.initialUnitCount * 30 / 100);

	tasks[taskId] = task;
	return taskId;
}

bool AIControlAdapterCombatTaskManager::isUnitReserved(unsigned int unitId) const
{
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			auto it = std::find(task.assignedUnitIds.begin(), task.assignedUnitIds.end(), unitId);
			if (it != task.assignedUnitIds.end())
			{
				return true;
			}
		}
	}
	return false;
}

bool AIControlAdapterCombatTaskManager::canUseUnitForTask(
	unsigned int unitId,
	const std::string& requestingOwner) const
{
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			auto it = std::find(task.assignedUnitIds.begin(), task.assignedUnitIds.end(), unitId);
			if (it != task.assignedUnitIds.end())
			{
				// Allow same owner to reuse
				if (task.owner == requestingOwner)
				{
					return true;
				}
				return false;
			}
		}
	}
	return true; // Not reserved
}

CombatTask* AIControlAdapterCombatTaskManager::findTask(unsigned int taskId)
{
	auto it = tasks.find(taskId);
	return it != tasks.end() ? &it->second : nullptr;
}

const CombatTask* AIControlAdapterCombatTaskManager::findTask(unsigned int taskId) const
{
	auto it = tasks.find(taskId);
	return it != tasks.end() ? &it->second : nullptr;
}

CombatTask* AIControlAdapterCombatTaskManager::findTaskByUnit(unsigned int unitId)
{
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			auto it = std::find(task.assignedUnitIds.begin(), task.assignedUnitIds.end(), unitId);
			if (it != task.assignedUnitIds.end())
			{
				return &task;
			}
		}
	}
	return nullptr;
}

const CombatTask* AIControlAdapterCombatTaskManager::findTaskByUnit(unsigned int unitId) const
{
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			auto it = std::find(task.assignedUnitIds.begin(), task.assignedUnitIds.end(), unitId);
			if (it != task.assignedUnitIds.end())
			{
				return &task;
			}
		}
	}
	return nullptr;
}

bool AIControlAdapterCombatTaskManager::updateTaskState(
	unsigned int taskId,
	CombatTaskState newState,
	const std::string& reason)
{
	CombatTask* task = findTask(taskId);
	if (task == nullptr)
	{
		return false;
	}

	task->state = newState;
	task->reason = reason;
	return true;
}

bool AIControlAdapterCombatTaskManager::updateTaskCommand(
	unsigned int taskId,
	DWORD currentTick)
{
	CombatTask* task = findTask(taskId);
	if (task == nullptr)
	{
		return false;
	}

	task->lastCommandTick = currentTick;
	return true;
}

bool AIControlAdapterCombatTaskManager::updateTaskProgress(
	unsigned int taskId,
	DWORD currentTick)
{
	CombatTask* task = findTask(taskId);
	if (task == nullptr)
	{
		return false;
	}

	task->lastProgressTick = currentTick;
	return true;
}

bool AIControlAdapterCombatTaskManager::completeTask(unsigned int taskId, const std::string& reason)
{
	return updateTaskState(taskId, CombatTaskState::Complete, reason);
}

bool AIControlAdapterCombatTaskManager::failTask(unsigned int taskId, const std::string& reason)
{
	return updateTaskState(taskId, CombatTaskState::Failed, reason);
}

bool AIControlAdapterCombatTaskManager::expireTask(unsigned int taskId, const std::string& reason)
{
	return updateTaskState(taskId, CombatTaskState::Expired, reason);
}

bool AIControlAdapterCombatTaskManager::removeDeadUnits(
	unsigned int taskId,
	const std::vector<unsigned int>& deadUnitIds)
{
	CombatTask* task = findTask(taskId);
	if (task == nullptr)
	{
		return false;
	}

	for (unsigned int deadId : deadUnitIds)
	{
		auto it = std::find(task->assignedUnitIds.begin(), task->assignedUnitIds.end(), deadId);
		if (it != task->assignedUnitIds.end())
		{
			task->assignedUnitIds.erase(it);
		}
	}

	return true;
}

bool AIControlAdapterCombatTaskManager::releaseUnits(unsigned int taskId, const std::string& reason)
{
	CombatTask* task = findTask(taskId);
	if (task == nullptr)
	{
		return false;
	}

	task->assignedUnitIds.clear();
	task->reason = reason;
	return true;
}

void AIControlAdapterCombatTaskManager::updateTasks(DWORD currentTick)
{
	// Update last seen tick for active tasks
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			// Check for timeout
			if (currentTick >= task.timeoutTick)
			{
				task.state = CombatTaskState::Expired;
				task.reason = "timeout";
			}
		}
	}
}

void AIControlAdapterCombatTaskManager::pruneExpiredTasks(DWORD currentTick)
{
	std::vector<unsigned int> toRemove;

	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;

		// Remove terminal tasks after a grace period
		if (task.state == CombatTaskState::Complete ||
			task.state == CombatTaskState::Failed ||
			task.state == CombatTaskState::Expired)
		{
			// Keep terminal tasks for 5 seconds for telemetry
			const DWORD terminalAge = currentTick - task.lastCommandTick;
			if (terminalAge > 5000)
			{
				toRemove.push_back(task.taskId);
			}
		}
	}

	for (unsigned int taskId : toRemove)
	{
		tasks.erase(taskId);
	}
}

void AIControlAdapterCombatTaskManager::pruneDeadUnits(DWORD currentTick)
{
	// Dead unit pruning should be done by the caller using removeDeadUnits
	// This is a placeholder for future automatic dead unit detection
}

void AIControlAdapterCombatTaskManager::removeTask(unsigned int taskId)
{
	tasks.erase(taskId);
}

std::vector<CombatTask*> AIControlAdapterCombatTaskManager::findAttackTasks()
{
	std::vector<CombatTask*> result;
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Attack &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			result.push_back(&task);
		}
	}
	return result;
}

std::vector<CombatTask*> AIControlAdapterCombatTaskManager::findDefenseTasks()
{
	std::vector<CombatTask*> result;
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Defense &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			result.push_back(&task);
		}
	}
	return result;
}

std::vector<CombatTask*> AIControlAdapterCombatTaskManager::findActiveTasks()
{
	std::vector<CombatTask*> result;
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			result.push_back(&task);
		}
	}
	return result;
}

CombatTask* AIControlAdapterCombatTaskManager::findActiveTaskByType(
	CombatTaskType type,
	const std::string& owner)
{
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.type == type &&
			task.owner == owner &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			return &task;
		}
	}
	return nullptr;
}

bool AIControlAdapterCombatTaskManager::hasEquivalentActiveTask(
	CombatTaskType type,
	const Coord3D& targetPosition,
	Real toleranceRadius) const
{
	const Real toleranceRadiusSq = toleranceRadius * toleranceRadius;

	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.type == type &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			const Real dx = task.targetPosition.x - targetPosition.x;
			const Real dy = task.targetPosition.y - targetPosition.y;
			const Real distSq = dx * dx + dy * dy;
			if (distSq <= toleranceRadiusSq)
			{
				return true;
			}
		}
	}
	return false;
}

int AIControlAdapterCombatTaskManager::getActiveTaskCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterCombatTaskManager::getAttackTaskCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Attack &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterCombatTaskManager::getDefenseTaskCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Defense &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterCombatTaskManager::getGuardTaskCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Guard &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterCombatTaskManager::getScoutTaskCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Scout &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterCombatTaskManager::getScoutAssignedUnitCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Scout &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			count += static_cast<int>(task.assignedUnitIds.size());
		}
	}
	return count;
}

int AIControlAdapterCombatTaskManager::getTotalAssignedUnitCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			count += static_cast<int>(task.assignedUnitIds.size());
		}
	}
	return count;
}
