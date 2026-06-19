#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapterWorkerShuttleManager.h"

#include <algorithm>

int AIControlAdapterWorkerShuttleManager::ResolveProtectedTechnicalCount(
	const nlohmann::json& glaUsaStrategyTelemetry) const
{
	if (!glaUsaStrategyTelemetry.is_object()
		|| !glaUsaStrategyTelemetry.value("active", false))
	{
		return 0;
	}
	const auto workerIt = glaUsaStrategyTelemetry.find("worker_mobility");
	if (workerIt == glaUsaStrategyTelemetry.end() || !workerIt->is_object())
	{
		return 0;
	}
	if (!workerIt->value("desired", false))
	{
		return 0;
	}
	return std::max(0, workerIt->value("protected_technicals", 0));
}

AIControlAdapterTechnicalScoutShuttleDecision AIControlAdapterWorkerShuttleManager::EvaluateTechnicalScoutShuttle(
	const AIControlAdapterTechnicalScoutShuttleInput& input) const
{
	AIControlAdapterTechnicalScoutShuttleDecision decision;
	if (input.scudTargetRefreshNeeded && input.readyScudStorms > 0)
	{
		decision.mode = "wmd_refresh";
		decision.reason = "ready_scud_target_refresh";
		return decision;
	}
	if (input.availableTechnicals <= 0)
	{
		decision.reason = "no_available_technicals";
		return decision;
	}
	if (input.activeScoutTasks > 0)
	{
		decision.reason = "scout_task_active";
		return decision;
	}

	const bool mapEnumeration = input.readyScudStorms <= 0;
	const bool usefulCargo =
		input.availableWorkers > 0 ||
		input.availableRpg > 0 ||
		input.availableRebels > 0;
	if (!mapEnumeration || !usefulCargo)
	{
		decision.reason = mapEnumeration ? "no_useful_passengers" : "scuds_ready";
		return decision;
	}

	decision.desired = true;
	decision.mode = "scout_shuttle";
	decision.reason = input.remoteBuildGap > 0 ? "early_map_enum_remote_gap" : "early_map_enum_passenger_utility";
	decision.allowProtectedTechnicalScout = input.protectedTechnicals > 0;
	decision.workerPassengers = (input.remoteBuildGap > 0 && input.availableWorkers > 0) ? 1 : 0;
	decision.rpgPassengers = std::min(2, std::max(0, input.availableRpg));
	decision.rebelPassengers = (decision.workerPassengers == 0 && input.availableRebels > 0) ? 1 : 0;
	return decision;
}

std::set<unsigned int> AIControlAdapterWorkerShuttleManager::CollectProtectedTechnicalIds(
	const std::vector<AIControlAdapterWorkerShuttleTechnicalSnapshot>& technicals,
	int desiredProtected) const
{
	std::set<unsigned int> protectedIds;
	if (desiredProtected <= 0)
	{
		return protectedIds;
	}

	std::vector<unsigned int> technicalIds;
	for (const AIControlAdapterWorkerShuttleTechnicalSnapshot& technical : technicals)
	{
		if (technical.id == 0u
			|| technical.isStructure
			|| technical.underConstruction
			|| !technical.isTechnical
			|| technical.dead)
		{
			continue;
		}
		technicalIds.push_back(technical.id);
	}

	std::sort(technicalIds.begin(), technicalIds.end());
	const int count = std::min(desiredProtected, static_cast<int>(technicalIds.size()));
	for (int i = 0; i < count; ++i)
	{
		protectedIds.insert(technicalIds[static_cast<std::size_t>(i)]);
	}
	return protectedIds;
}

bool AIControlAdapterWorkerShuttleManager::IsTechnicalProtected(
	const AIControlAdapterWorkerShuttleTechnicalSnapshot& technical,
	const std::set<unsigned int>& protectedIds) const
{
	if (technical.id == 0u
		|| technical.isStructure
		|| technical.underConstruction
		|| !technical.isTechnical)
	{
		return false;
	}
	return protectedIds.find(technical.id) != protectedIds.end();
}

bool AIControlAdapterWorkerShuttleManager::IsTechnicalAssigned(
	unsigned int technicalId,
	const std::vector<AIControlAdapterWorkerShuttleAssignmentSnapshot>& assignments) const
{
	for (const AIControlAdapterWorkerShuttleAssignmentSnapshot& assignment : assignments)
	{
		if (assignment.technicalId == technicalId
			&& assignment.state != "released"
			&& assignment.state != "failed")
		{
			return true;
		}
	}
	return false;
}

bool AIControlAdapterWorkerShuttleManager::ShouldReleaseAssignmentForReservation(
	bool reservationMissing,
	bool reservationTerminal) const
{
	return reservationMissing || reservationTerminal;
}

bool AIControlAdapterWorkerShuttleManager::IsBuildTaskEligibleForAssignment(
	const std::string& taskState,
	bool workerAlreadyContained,
	float workerDistance) const
{
	if (workerAlreadyContained || workerDistance < 1200.0f)
	{
		return false;
	}
	return taskState == "assigned" || taskState == "moving" || taskState == "executing";
}

nlohmann::json AIControlAdapterWorkerShuttleManager::BuildAssignmentTelemetry(
	const AIControlAdapterWorkerShuttleAssignmentSnapshot& assignment,
	const AIControlAdapterWorkerShuttleAssignmentStatus& status,
	unsigned int now) const
{
	return nlohmann::json::object({
		{"task_id", assignment.taskId},
		{"worker_id", assignment.workerId},
		{"technical_id", assignment.technicalId},
		{"template", assignment.templateName},
		{"state", assignment.state},
		{"reason", assignment.reason},
		{"age_ms", now - assignment.createdTick},
		{"last_command_age_ms", now - assignment.lastCommandTick},
		{"worker_inside", status.workerInside},
		{"worker_distance", status.workerDistance},
		{"technical_distance", status.technicalDistance},
		{"target", nlohmann::json::object({
			{"x", assignment.targetX},
			{"y", assignment.targetY},
			{"z", assignment.targetZ}
		})}
	});
}
