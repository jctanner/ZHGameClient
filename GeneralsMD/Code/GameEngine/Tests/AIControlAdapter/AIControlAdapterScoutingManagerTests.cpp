#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterScoutingManager.h"

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
		const unsigned int seed = AIControlAdapterScoutingManager::buildStableScoutSeed("maps/test.map", 1, 65000u, 2);
		expect(seed == AIControlAdapterScoutingManager::buildStableScoutSeed("maps/test.map", 1, 65000u, 2),
			"stable scout seed should be deterministic for identical inputs");
		expect(seed != AIControlAdapterScoutingManager::buildStableScoutSeed("maps/test.map", 2, 65000u, 2),
			"stable scout seed should vary by player index");
		expect(seed != AIControlAdapterScoutingManager::buildStableScoutSeed("maps/test.map", 1, 95000u, 2),
			"stable scout seed should vary by 30 second time bucket");
	}

	{
		nlohmann::json pathing = nlohmann::json::object({
			{"terrain_extraction", nlohmann::json::object({
				{"extent", nlohmann::json::object({
					{"min_x", 100.0f},
					{"min_y", 200.0f},
					{"max_x", 3000.0f},
					{"max_y", 3500.0f}
				})}
			})}
		});
		nlohmann::json zones = nlohmann::json::array({
			nlohmann::json::object({{"center_x", 4200.0f}, {"center_y", 4300.0f}}),
			nlohmann::json::object({{"center_x", -100.0f}, {"center_y", 1000.0f}})
		});
		const AIControlAdapterScoutMapBounds bounds = AIControlAdapterScoutingManager::resolveScoutMapBounds(pathing, zones);
		expectNear(bounds.minX, -2600.0f, 0.001f, "bounds should expand around telemetry zone min x");
		expectNear(bounds.minY, -1500.0f, 0.001f, "bounds should expand around telemetry zone min y");
		expectNear(bounds.maxX, 6700.0f, 0.001f, "bounds should expand around telemetry zone max x");
		expectNear(bounds.maxY, 6800.0f, 0.001f, "bounds should expand around telemetry zone max y");
	}

	{
		nlohmann::json zones = nlohmann::json::array({
			nlohmann::json::object({
				{"anchor_id", 1u},
				{"is_main_base", true},
				{"center_x", 1000.0f},
				{"center_y", 1000.0f},
				{"active", true}
			}),
			nlohmann::json::object({
				{"anchor_id", 2u},
				{"is_main_base", false},
				{"front_point_x", 2000.0f},
				{"front_point_y", 2500.0f},
				{"developed", true}
			})
		});
		AIControlAdapterScoutLastZoneSnapshot lastZone;
		lastZone.hasLastZone = true;
		lastZone.anchorId = 7u;
		lastZone.centerX = 7000.0f;
		lastZone.centerY = 7100.0f;
		const std::vector<CombatTaskScoutRandomOrigin> origins =
			AIControlAdapterScoutingManager::buildScoutRandomOrigins(zones, lastZone);
		expect(origins.size() == 1u, "random origins should prefer non-main zones when present");
		expect(origins[0].zoneId == 2u, "random origin should keep non-main zone id");
		expectNear(origins[0].position.x, 2000.0f, 0.001f, "random origin should use front point x");
		expectNear(origins[0].position.y, 2500.0f, 0.001f, "random origin should use front point y");
		expect(origins[0].priority == 5, "developed zone should keep origin priority");
	}

	{
		AIControlAdapterScoutLastZoneSnapshot lastZone;
		lastZone.hasLastZone = true;
		lastZone.anchorId = 7u;
		lastZone.isMainBase = false;
		lastZone.centerX = 7000.0f;
		lastZone.centerY = 7100.0f;
		const std::vector<CombatTaskScoutRandomOrigin> origins =
			AIControlAdapterScoutingManager::buildScoutRandomOrigins(nlohmann::json::array(), lastZone);
		expect(origins.size() == 1u, "random origins should fall back to last zone");
		expect(origins[0].zoneId == 7u, "last zone fallback should keep anchor id");
		expectNear(origins[0].position.x, 7000.0f, 0.001f, "last zone fallback should keep center x");
		expectNear(origins[0].position.y, 7100.0f, 0.001f, "last zone fallback should keep center y");
	}

	{
		nlohmann::json pathing = nlohmann::json::object({
			{"terrain_features", nlohmann::json::array({
				nlohmann::json::object({
					{"kind", "impassable_barrier"},
					{"points", nlohmann::json::array({
						nlohmann::json::object({{"x", 1.0f}, {"y", 2.0f}}),
						nlohmann::json::object({{"x", 3.0f}, {"y", 4.0f}}),
						nlohmann::json::object({{"x", 5.0f}, {"y", 6.0f}})
					})}
				}),
				nlohmann::json::object({
					{"kind", "road"},
					{"points", nlohmann::json::array({
						nlohmann::json::object({{"x", 10.0f}, {"y", 20.0f}}),
						nlohmann::json::object({{"x", 30.0f}, {"y", 40.0f}})
					})}
				})
			})}
		});
		const std::vector<CombatTaskScoutRandomBarrier> barriers =
			AIControlAdapterScoutingManager::buildScoutRandomBarriers(pathing);
		expect(barriers.size() == 2u, "barrier extraction should create one segment per adjacent barrier point");
		expectNear(barriers[0].ax, 1.0f, 0.001f, "first barrier should keep start x");
		expectNear(barriers[0].by, 4.0f, 0.001f, "first barrier should keep end y");
		expectNear(barriers[1].ax, 3.0f, 0.001f, "second barrier should chain from previous point");
		expectNear(barriers[1].by, 6.0f, 0.001f, "second barrier should keep final y");
	}

	return 0;
}
