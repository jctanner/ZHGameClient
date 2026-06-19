#include "GameClient/AIControlAdapter/AIControlAdapterWorkerShuttleManager.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
	void expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << "\n";
			std::exit(1);
		}
	}
}

int main()
{
	AIControlAdapterWorkerShuttleManager manager;

	{
		const nlohmann::json telemetry = nlohmann::json::object({
			{"active", true},
			{"worker_mobility", nlohmann::json::object({
				{"desired", true},
				{"protected_technicals", 2}
			})}
		});
		expect(manager.ResolveProtectedTechnicalCount(telemetry) == 2, "Active worker mobility should expose protected Technical count");
		expect(manager.ResolveProtectedTechnicalCount(nlohmann::json::object()) == 0, "Inactive telemetry should disable worker shuttle protection");
	}

	{
		std::vector<AIControlAdapterWorkerShuttleTechnicalSnapshot> technicals;
		AIControlAdapterWorkerShuttleTechnicalSnapshot highId;
		highId.id = 33u;
		highId.isTechnical = true;
		technicals.push_back(highId);

		AIControlAdapterWorkerShuttleTechnicalSnapshot lowId;
		lowId.id = 11u;
		lowId.isTechnical = true;
		technicals.push_back(lowId);

		AIControlAdapterWorkerShuttleTechnicalSnapshot deadLowId;
		deadLowId.id = 7u;
		deadLowId.isTechnical = true;
		deadLowId.dead = true;
		technicals.push_back(deadLowId);

		AIControlAdapterWorkerShuttleTechnicalSnapshot structure;
		structure.id = 5u;
		structure.isTechnical = true;
		structure.isStructure = true;
		technicals.push_back(structure);

		const std::set<unsigned int> protectedIds = manager.CollectProtectedTechnicalIds(technicals, 1);
		expect(protectedIds.size() == 1u, "Protected Technical selection should honor desired count");
		expect(protectedIds.find(11u) != protectedIds.end(), "Protected Technical selection should be deterministic by lowest live id");
		expect(manager.IsTechnicalProtected(lowId, protectedIds), "Selected Technical should be protected");
		expect(!manager.IsTechnicalProtected(highId, protectedIds), "Unselected Technical should not be protected");
		expect(!manager.IsTechnicalProtected(structure, protectedIds), "Structures should not be protected as shuttle cabs");
	}

	{
		std::vector<AIControlAdapterWorkerShuttleAssignmentSnapshot> assignments;
		AIControlAdapterWorkerShuttleAssignmentSnapshot active;
		active.technicalId = 42u;
		active.state = "entering";
		assignments.push_back(active);

		AIControlAdapterWorkerShuttleAssignmentSnapshot released;
		released.technicalId = 7u;
		released.state = "released";
		assignments.push_back(released);

		expect(manager.IsTechnicalAssigned(42u, assignments), "Active assignment should reserve its Technical");
		expect(!manager.IsTechnicalAssigned(7u, assignments), "Released assignment should not reserve its Technical");
		expect(!manager.IsTechnicalAssigned(99u, assignments), "Unknown Technical should not be assigned");
	}

	{
		expect(
			!manager.ShouldReleaseAssignmentForReservation(false, false),
			"Active build reservations should keep worker shuttle assignments alive");
		expect(
			manager.ShouldReleaseAssignmentForReservation(true, false),
			"Missing build reservations should release worker shuttle assignments");
		expect(
			manager.ShouldReleaseAssignmentForReservation(false, true),
			"Terminal build reservations should release worker shuttle assignments");
	}

	{
		expect(
			manager.IsBuildTaskEligibleForAssignment("assigned", false, 1500.0f),
			"Assigned remote build tasks should be eligible for worker shuttle");
		expect(
			manager.IsBuildTaskEligibleForAssignment("moving", false, 1500.0f),
			"Moving remote build tasks should be eligible for worker shuttle");
		expect(
			manager.IsBuildTaskEligibleForAssignment("executing", false, 1500.0f),
			"Executing remote foundation tasks should remain eligible for worker shuttle");
		expect(
			!manager.IsBuildTaskEligibleForAssignment("complete", false, 1500.0f),
			"Complete build tasks should not be eligible for worker shuttle");
		expect(
			!manager.IsBuildTaskEligibleForAssignment("executing", false, 800.0f),
			"Nearby foundation tasks should not consume worker shuttle capacity");
		expect(
			!manager.IsBuildTaskEligibleForAssignment("executing", true, 1500.0f),
			"Already contained workers should not start another worker shuttle assignment");
	}

	{
		AIControlAdapterWorkerShuttleAssignmentSnapshot assignment;
		assignment.taskId = 3u;
		assignment.workerId = 10u;
		assignment.technicalId = 20u;
		assignment.templateName = "GLAStingerSite";
		assignment.state = "transporting";
		assignment.reason = "move_and_evacuate_issued";
		assignment.createdTick = 1000u;
		assignment.lastCommandTick = 1800u;
		assignment.targetX = 100.0f;
		assignment.targetY = 200.0f;
		assignment.targetZ = 0.0f;

		AIControlAdapterWorkerShuttleAssignmentStatus status;
		status.workerInside = true;
		status.workerDistance = 1250.0f;
		status.technicalDistance = 300.0f;

		const nlohmann::json telemetry = manager.BuildAssignmentTelemetry(assignment, status, 2500u);
		expect(telemetry.value("task_id", 0u) == 3u, "Telemetry should preserve task id");
		expect(telemetry.value("worker_inside", false), "Telemetry should preserve containment status");
		expect(telemetry.value("age_ms", 0u) == 1500u, "Telemetry should compute assignment age");
		expect(telemetry.value("last_command_age_ms", 0u) == 700u, "Telemetry should compute command age");
		expect(telemetry["target"].value("x", 0.0f) == 100.0f, "Telemetry should preserve target x");
	}

	{
		AIControlAdapterTechnicalScoutShuttleDecision decision =
			manager.EvaluateTechnicalScoutShuttle({
				0,
				false,
				1,
				1,
				3,
				4,
				2,
				1,
				0
			});
		expect(decision.desired, "Early map enumeration with cargo should desire scout-shuttle mode");
		expect(decision.mode == std::string("scout_shuttle"), "Early Technical scouting should become scout-shuttle");
		expect(decision.workerPassengers == 1, "Remote build gap should request one worker passenger");
		expect(decision.rpgPassengers == 2, "Scout-shuttle should cap RPG passengers at two");
		expect(decision.rebelPassengers == 0, "Worker/RPG loadout should not add rebel by default");
		expect(decision.allowProtectedTechnicalScout, "Protected Technicals may be used by scout-shuttle mode");
	}

	{
		AIControlAdapterTechnicalScoutShuttleDecision decision =
			manager.EvaluateTechnicalScoutShuttle({
				2,
				true,
				1,
				1,
				3,
				4,
				2,
				1,
				0
			});
		expect(!decision.desired, "Ready SCUD target refresh should not wait for shuttle loadout");
		expect(decision.mode == std::string("wmd_refresh"), "Ready SCUD target refresh should prefer pure scout mode");
	}

	{
		AIControlAdapterTechnicalScoutShuttleDecision decision =
			manager.EvaluateTechnicalScoutShuttle({
				0,
				false,
				1,
				0,
				0,
				0,
				1,
				0,
				0
			});
		expect(decision.desired, "Partial infantry-only loadout should still be useful");
		expect(decision.workerPassengers == 0, "No available workers should produce partial loadout");
		expect(decision.rpgPassengers == 1, "Available RPG passenger should be selected");
	}

	return 0;
}
