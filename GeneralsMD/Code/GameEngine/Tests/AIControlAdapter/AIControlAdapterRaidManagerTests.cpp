#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterRaidManager.h"

#include <cstdlib>
#include <iostream>

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

	void expectNear(float actual, float expected, float epsilon, const char* message)
	{
		const float delta = actual > expected ? actual - expected : expected - actual;
		if (delta > epsilon)
		{
			std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << "\n";
			std::exit(1);
		}
	}
}

int main()
{
	{
		std::vector<CombatTaskWaypoint> waypoints;
		CombatTaskWaypoint start;
		start.position.x = 0.0f;
		start.position.y = 0.0f;
		waypoints.push_back(start);
		CombatTaskWaypoint next;
		next.position.x = 2400.0f;
		next.position.y = 0.0f;
		waypoints.push_back(next);
		Coord3D finalTarget;
		finalTarget.x = 4000.0f;
		finalTarget.y = 0.0f;
		const Coord3D target = AIControlAdapterRaidManager::computeProbeTarget(waypoints, finalTarget, 0, 1200.0f);
		expectNear(target.x, 1200.0f, 0.001f, "probe target should clamp long advance");
		expectNear(target.y, 0.0f, 0.001f, "probe target should preserve direction y");
	}

	{
		std::vector<CombatTaskWaypoint> waypoints;
		CombatTaskWaypoint start;
		start.position.x = 0.0f;
		start.position.y = 0.0f;
		waypoints.push_back(start);
		Coord3D finalTarget;
		finalTarget.x = 300.0f;
		finalTarget.y = 400.0f;
		const Coord3D target = AIControlAdapterRaidManager::computeProbeTarget(waypoints, finalTarget, 0, 1200.0f);
		expectNear(target.x, 300.0f, 0.001f, "probe target should use final target when close");
		expectNear(target.y, 400.0f, 0.001f, "probe target should keep final target y");
	}

	{
		CombatTaskWaypoint waypoint;
		waypoint.position.x = 100.0f;
		waypoint.position.y = 100.0f;
		std::vector<AIControlAdapterRaidProbeUnitSnapshot> units;
		AIControlAdapterRaidProbeUnitSnapshot slow;
		slow.unitId = 1u;
		slow.templateName = "GLAVehicleBattleBus";
		slow.vehicle = true;
		slow.ableToAttack = true;
		slow.hasPosition = true;
		slow.x = 110.0f;
		slow.y = 100.0f;
		units.push_back(slow);
		AIControlAdapterRaidProbeUnitSnapshot buggy;
		buggy.unitId = 2u;
		buggy.templateName = "GLAVehicleRocketBuggy";
		buggy.vehicle = true;
		buggy.ableToAttack = true;
		buggy.hasPosition = true;
		buggy.x = 300.0f;
		buggy.y = 100.0f;
		units.push_back(buggy);
		AIControlAdapterRaidProbeUnitSnapshot reserved;
		reserved.unitId = 3u;
		reserved.templateName = "GLAVehicleTechnical";
		reserved.vehicle = true;
		reserved.ableToAttack = true;
		reserved.taskReserved = true;
		reserved.hasPosition = true;
		reserved.x = 120.0f;
		reserved.y = 100.0f;
		units.push_back(reserved);
		AIControlAdapterRaidProbeUnitSnapshot worker;
		worker.unitId = 4u;
		worker.templateName = "GLAWorker";
		worker.vehicle = false;
		worker.worker = true;
		worker.hasPosition = true;
		worker.x = 100.0f;
		worker.y = 100.0f;
		units.push_back(worker);

		const std::vector<unsigned int> selected =
			AIControlAdapterRaidManager::selectProbeUnitIds(units, waypoint, 2, 1200.0f);
		expect(selected.size() == 2u, "probe selection should choose two eligible units");
		expect(selected[0] == 2u, "probe selection should prefer fast combat units");
		expect(selected[1] == 1u, "probe selection should fall back to slower combat units");
	}

	{
		const AIControlAdapterRaidModeDecision decision =
			AIControlAdapterRaidManager::evaluateRaidModePolicy("", 2, 3, 5, 2, false, 0u);
		expect(decision.raidMode == "mixed_local", "raid mode should infer mixed local when infantry and vehicles are live");
		expect(!decision.degradeToVehicle, "fresh mixed local group should not degrade immediately");
		expect(!decision.fail, "fresh mixed local group should not fail");
	}

	{
		const AIControlAdapterRaidModeDecision decision =
			AIControlAdapterRaidManager::evaluateRaidModePolicy("vehicle", 2, 3, 5, 2, false, 0u);
		expect(decision.degradeToVehicle, "vehicle mode with infantry attached should degrade to vehicle-only group");
		expect(std::string(decision.reason) == "long_distance", "vehicle degrade should report long distance");
	}

	{
		const AIControlAdapterRaidModeDecision decision =
			AIControlAdapterRaidManager::evaluateRaidModePolicy("mixed_local", 3, 2, 5, 2, true, 45000u);
		expect(decision.degradeToVehicle, "mixed local should degrade after infantry timeout when vehicles survive");
		expect(decision.raidMode == "vehicle", "infantry timeout should switch mode to vehicle");
		expect(std::string(decision.reason) == "infantry_timeout", "mixed degrade should report infantry timeout");
	}

	{
		const AIControlAdapterRaidModeDecision decision =
			AIControlAdapterRaidManager::evaluateRaidModePolicy("mixed_local", 3, 0, 5, 2, true, 45000u);
		expect(decision.fail, "mixed local should fail when timeout expires and no vehicle group survives");
		expect(std::string(decision.reason) == "infantry_timeout_no_survivors", "mixed fail should report infantry timeout no survivors");
	}

	{
		const AIControlAdapterRaidModeDecision decision =
			AIControlAdapterRaidManager::evaluateRaidModePolicy("vehicle", 0, 0, 3, 1, false, 0u);
		expect(decision.fail, "vehicle raid should fail with no viable vehicles");
		expect(std::string(decision.reason) == "no_viable_vehicle_group", "vehicle fail should report no viable vehicle group");
	}

	{
		const AIControlAdapterAttackAutomationDecision decision =
			AIControlAdapterRaidManager::evaluateAttackAutomationGate(4, 8);
		expect(!decision.shouldIssue, "attack automation should block when eligible units are below minimum");
		expect(std::string(decision.reason) == "not_enough_units", "attack gate should report not enough units");
	}

	{
		const AIControlAdapterAttackAutomationDecision decision =
			AIControlAdapterRaidManager::evaluateAttackAutomationGate(8, 8);
		expect(decision.shouldIssue, "attack automation should allow when eligible units meet minimum");
		expect(std::string(decision.reason) == "enough_units", "attack gate should report enough units");
	}

	{
		const AIControlAdapterAttackAutomationDecision decision =
			AIControlAdapterRaidManager::evaluateAttackAutomationGate(0, 0);
		expect(decision.shouldIssue, "attack automation should allow when minimum is disabled");
		expect(std::string(decision.reason) == "minimum_disabled", "attack gate should report disabled minimum");
	}

	{
		const AIControlAdapterAttackTaskCreationDecision decision =
			AIControlAdapterRaidManager::evaluateAttackTaskCreation(false, 3, false);
		expect(!decision.shouldCreateTask, "attack task creation should block after failed command");
		expect(!decision.shouldReleaseUnits, "failed command should not release assigned units");
		expect(std::string(decision.reason) == "command_failed", "failed command should report command failed");
	}

	{
		const AIControlAdapterAttackTaskCreationDecision decision =
			AIControlAdapterRaidManager::evaluateAttackTaskCreation(true, 0, false);
		expect(!decision.shouldCreateTask, "attack task creation should block without assigned units");
		expect(std::string(decision.reason) == "no_eligible_units", "no units should report no eligible units");
	}

	{
		const AIControlAdapterAttackTaskCreationDecision decision =
			AIControlAdapterRaidManager::evaluateAttackTaskCreation(true, 3, true);
		expect(!decision.shouldCreateTask, "attack task creation should block equivalent active task");
		expect(decision.shouldReleaseUnits, "equivalent active task should release command-selected units");
		expect(std::string(decision.reason) == "equivalent_active_task", "equivalent active should report equivalent task");
	}

	{
		const AIControlAdapterAttackTaskCreationDecision decision =
			AIControlAdapterRaidManager::evaluateAttackTaskCreation(true, 3, false);
		expect(decision.shouldCreateTask, "attack task creation should allow assigned units without equivalent task");
		expect(!decision.shouldReleaseUnits, "new attack task should keep assigned units");
		expect(std::string(decision.reason) == "attack_automation", "new attack task should report attack automation");
	}

	return 0;
}
