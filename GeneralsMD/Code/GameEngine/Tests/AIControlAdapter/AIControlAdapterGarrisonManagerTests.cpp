#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterGarrisonManager.h"

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
		AIControlAdapterGarrisonDiscoveryFacts facts;
		facts.templateName = "CivilianBuilding";
		facts.hasContain = false;
		AIControlAdapterGarrisonDiscoveryResult missing = AIControlAdapterGarrisonManager::evaluateDiscovery(facts);
		expect(!missing.accepted, "missing contain should not be accepted");
		expect(missing.reason == "missing_contain", "missing contain should keep explicit reason");

		facts.hasContain = true;
		facts.containGarrison = true;
		facts.capacity = 5;
		AIControlAdapterGarrisonDiscoveryResult contain = AIControlAdapterGarrisonManager::evaluateDiscovery(facts);
		expect(contain.accepted, "garrison contain should be accepted");
		expect(contain.reason == "contain_garrison", "contain garrison should choose contain reason");
		expect(contain.capacity == 5, "discovery should keep contain capacity");
		expect(contain.containName == "garrison", "discovery should label garrison contain");
	}

	{
		nlohmann::json pathing = nlohmann::json::object({
			{"strategic_objects", nlohmann::json::array({
				nlohmann::json::object({
					{"kind", "garrison"},
					{"template", "CivilianBunker"},
					{"position", nlohmann::json::object({{"x", 100.0f}, {"y", 120.0f}})}
				})
			})}
		});
		expect(AIControlAdapterGarrisonManager::isNearMapCacheGarrison(pathing, "CivilianBunker", 105.0f, 125.0f, 20.0f),
			"map cache garrison should match nearby template");
		expect(!AIControlAdapterGarrisonManager::isNearMapCacheGarrison(pathing, "CivilianTower", 105.0f, 125.0f, 20.0f),
			"map cache garrison should not match different non-empty template");
		expect(!AIControlAdapterGarrisonManager::isNearMapCacheGarrison(pathing, "CivilianBunker", 200.0f, 220.0f, 20.0f),
			"map cache garrison should respect radius");
	}

	{
		nlohmann::json pathing = nlohmann::json::object({
			{"features", nlohmann::json::array({
				nlohmann::json::object({
					{"kind", "cliff_entrance"},
					{"position", nlohmann::json::object({{"x", 500.0f}, {"y", 500.0f}})}
				}),
				nlohmann::json::object({
					{"id", "main_lane_0"},
					{"points", nlohmann::json::array({
						nlohmann::json::object({{"x", 900.0f}, {"y", 900.0f}})
					})}
				})
			})}
		});
		expect(AIControlAdapterGarrisonManager::isNearGarrisonFeature(pathing, 510.0f, 510.0f, "entrance", 30.0f),
			"feature proximity should match kind position");
		expect(AIControlAdapterGarrisonManager::isNearGarrisonFeature(pathing, 910.0f, 900.0f, "lane", 30.0f),
			"feature proximity should match id point fallback");
		expect(!AIControlAdapterGarrisonManager::isNearGarrisonFeature(pathing, 700.0f, 700.0f, "entrance", 30.0f),
			"feature proximity should respect radius");
	}

	{
		AIControlAdapterGarrisonAssignmentTelemetry assignment;
		assignment.structureId = 42u;
		assignment.zoneAnchorId = 7u;
		assignment.templateName = "CivilianBunker";
		assignment.x = 100.0f;
		assignment.y = 200.0f;
		assignment.desiredInfantry = 2;
		assignment.estimatedCapacity = 5;
		assignment.lastCommandTick = 1000u;
		assignment.lastProgressTick = 1200u;
		assignment.state = "partial";
		assignment.reason = "partial_entry";
		AIControlAdapterGarrisonInfantryTelemetry entered;
		entered.unitId = 11u;
		entered.templateName = "GLAInfantryTunnelDefender";
		entered.entered = true;
		entered.lastCommandTick = 1000u;
		entered.lastProgressTick = 1400u;
		AIControlAdapterGarrisonInfantryTelemetry outside;
		outside.unitId = 12u;
		outside.outside = true;
		outside.nearby = true;
		outside.enteredPendingVerification = true;
		assignment.infantry.push_back(entered);
		assignment.infantry.push_back(outside);

		const nlohmann::json telemetry = AIControlAdapterGarrisonManager::buildAssignmentTelemetry(assignment, 2000u);
		expect(telemetry.value("structure_id", 0u) == 42u, "assignment telemetry should keep structure id");
		expect(telemetry.value("entered_infantry", 0) == 1, "assignment telemetry should count entered infantry");
		expect(telemetry.value("nearby_infantry", 0) == 1, "assignment telemetry should count nearby infantry");
		expect(telemetry.value("entered_pending_verification", 0) == 1, "assignment telemetry should count pending verification");
		expect(telemetry["units"].is_array() && telemetry["units"].size() == 2u, "assignment telemetry should include unit details");
	}

	return 0;
}
