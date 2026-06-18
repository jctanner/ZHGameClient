#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCounterbatteryManager.h"

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
}

int main()
{
	{
		AIControlAdapterMobileSiegeTemplateResult accepted;
		accepted.accepted = true;
		accepted.reason = "mobile_siege";
		expect(AIControlAdapterCounterbatteryManager::shouldLogMobileSiegeDetection("ChinaNukeCannon", accepted),
			"accepted mobile siege should always log detection");

		AIControlAdapterMobileSiegeTemplateResult rejected;
		rejected.accepted = false;
		rejected.reason = "projectile_rejected";
		expect(AIControlAdapterCounterbatteryManager::shouldLogMobileSiegeDetection("StrategyCenterArtilleryShell", rejected),
			"rejected artillery-like templates should still log detection diagnostics");
		expect(!AIControlAdapterCounterbatteryManager::shouldLogMobileSiegeDetection("AmericaVehicleHumvee", rejected),
			"ordinary rejected vehicles should not log mobile siege diagnostics");
	}

	{
		AIControlAdapterCounterbatteryUnitSnapshot buggy;
		buggy.unitId = 10u;
		buggy.templateName = "GLAVehicleRocketBuggy";
		AIControlAdapterCounterbatteryUnitSnapshot jarmen;
		jarmen.unitId = 11u;
		jarmen.templateName = "GLAInfantryJarmenKell";
		AIControlAdapterCounterbatteryUnitSnapshot scud;
		scud.unitId = 12u;
		scud.templateName = "GLAVehicleScudLauncher";
		scud.isScudLauncher = true;
		AIControlAdapterCounterbatteryUnitSnapshot quad;
		quad.unitId = 13u;
		quad.templateName = "GLAVehicleQuadCannon";

		expect(AIControlAdapterCounterbatteryManager::isSuitableCounterUnit(buggy), "Rocket Buggy should be a counterbattery unit");
		expect(AIControlAdapterCounterbatteryManager::isSuitableCounterUnit(jarmen), "Jarmen should be a counterbattery unit");
		expect(AIControlAdapterCounterbatteryManager::isSuitableCounterUnit(scud), "SCUD Launcher should be a counterbattery unit");
		expect(!AIControlAdapterCounterbatteryManager::isSuitableCounterUnit(quad), "Quad Cannon should not be selected for counterbattery");
		expect(AIControlAdapterCounterbatteryManager::counterUnitPriority(buggy) < AIControlAdapterCounterbatteryManager::counterUnitPriority(jarmen),
			"Rocket Buggy should sort before Jarmen");
		expect(AIControlAdapterCounterbatteryManager::counterUnitPriority(jarmen) < AIControlAdapterCounterbatteryManager::counterUnitPriority(scud),
			"Jarmen should sort before SCUD Launcher");
	}

	{
		std::vector<AIControlAdapterCounterbatteryUnitSnapshot> units;
		for (unsigned int i = 1u; i <= 6u; ++i)
		{
			AIControlAdapterCounterbatteryUnitSnapshot unit;
			unit.unitId = i;
			unit.templateName = "GLAVehicleRocketBuggy";
			units.push_back(unit);
		}
		const std::vector<unsigned int> ids = AIControlAdapterCounterbatteryManager::selectAssignmentIds(units, 5, 4);
		expect(ids.size() == 4u, "assignment selection should respect hard cap");
		expect(ids[0] == 1u && ids[3] == 4u, "assignment selection should preserve sorted unit order");
	}

	{
		AIControlAdapterCounterbatteryTelemetryInput input;
		input.activeTasks = 1;
		input.assignedUnits = 3;
		input.productionNeeded = true;
		input.reason = "mobile_siege_visible";
		AIControlAdapterCounterbatteryThreatSnapshot threat;
		threat.objectId = 42u;
		threat.templateName = "ChinaNukeCannon";
		threat.x = 100.0f;
		threat.y = 200.0f;
		threat.visible = false;
		input.threats.push_back(threat);

		const nlohmann::json telemetry = AIControlAdapterCounterbatteryManager::buildTelemetry(input);
		expect(telemetry.value("active_tasks", 0) == 1, "telemetry should keep active task count");
		expect(telemetry.value("assigned_units", 0) == 3, "telemetry should keep assigned unit count");
		expect(telemetry.value("production_needed", false), "telemetry should keep production flag");
		expect(telemetry["mobile_siege_threats"].is_array() && telemetry["mobile_siege_threats"].size() == 1u,
			"telemetry should include mobile siege threat list");
		expect(telemetry["mobile_siege_threats"][0].value("stale", false),
			"telemetry should mark non-visible threats stale");
	}

	return 0;
}
