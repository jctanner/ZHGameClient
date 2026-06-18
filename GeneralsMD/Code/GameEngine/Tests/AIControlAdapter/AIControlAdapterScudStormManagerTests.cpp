#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterScudStormManager.h"

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
		nlohmann::json zones = nlohmann::json::array({
			nlohmann::json::object({{"anchor_id", 1u}, {"supply_stashes", 1}}),
			nlohmann::json::object({{"anchor_id", 2u}, {"supply_stashes", 0}}),
			nlohmann::json::object({{"anchor_id", 3u}, {"supply_stashes", 2}})
		});
		expect(AIControlAdapterScudStormManager::countSupplyZones(zones) == 2,
			"supply zone counter should count zones with at least one stash");
	}

	{
		std::vector<AIControlAdapterScudStormMemorySnapshot> memory;
		AIControlAdapterScudStormMemorySnapshot item;
		item.objectId = 42u;
		item.playerIndex = 2;
		item.team = 1;
		item.targetKind = "production";
		item.templateName = "ChinaWarFactory";
		item.visible = false;
		item.stale = true;
		item.lastSeenTick = 1000u;
		item.x = 300.0f;
		item.y = 400.0f;
		memory.push_back(item);

		const std::vector<AIControlAdapterScudStormStrategicTargetCandidate> candidates =
			AIControlAdapterScudStormManager::buildStrategicTargetCandidates(memory, 2500u);
		expect(candidates.size() == 1u, "strategic candidates should mirror memory items");
		expect(candidates[0].objectId == 42u, "strategic candidate should keep object id");
		expect(candidates[0].ageMs == 1500u, "strategic candidate should compute age");
		expect(candidates[0].stale, "strategic candidate should keep stale state");
	}

	{
		std::vector<EnemyMemoryItem> memoryItems;
		EnemyMemoryItem item;
		item.objectId = 99u;
		item.playerIndex = 3;
		item.team = 2;
		item.kind = EnemyMemoryKind::Wmd;
		item.templateName = "ChinaNuclearMissile";
		item.visible = true;
		item.stale = false;
		item.lastSeenTick = 12345u;
		item.position.x = 111.0f;
		item.position.y = 222.0f;
		item.position.z = 3.0f;
		memoryItems.push_back(item);

		const std::vector<AIControlAdapterScudStormMemorySnapshot> snapshots =
			AIControlAdapterScudStormManager::buildMemorySnapshotsFromEnemyMemory(memoryItems);
		expect(snapshots.size() == 1u, "SCUD memory snapshots should mirror enemy memory items");
		expect(snapshots[0].objectId == 99u, "SCUD memory snapshot should keep object id");
		expect(snapshots[0].targetKind == "wmd", "SCUD memory snapshot should classify WMD kind");
		expect(snapshots[0].templateName == "ChinaNuclearMissile", "SCUD memory snapshot should keep template name");
		expect(snapshots[0].visible, "SCUD memory snapshot should keep visible state");
		expect(!snapshots[0].stale, "SCUD memory snapshot should keep stale state");
		expect(snapshots[0].enemyOwned, "SCUD memory snapshot should be marked enemy owned");
		expect(snapshots[0].alive, "SCUD memory snapshot should be marked alive for known memory");
		expect(snapshots[0].x == 111.0f && snapshots[0].y == 222.0f && snapshots[0].z == 3.0f,
			"SCUD memory snapshot should keep position");
	}

	{
		nlohmann::json zones = nlohmann::json::array({
			nlohmann::json::object({
				{"anchor_id", 1u},
				{"is_main_base", true},
				{"developed", true},
				{"center_x", 100.0f},
				{"center_y", 100.0f},
				{"rear_point_x", 120.0f},
				{"rear_point_y", 130.0f},
				{"effective_radius", 600.0f},
				{"black_markets", 1}
			}),
			nlohmann::json::object({
				{"anchor_id", 2u},
				{"developed", true},
				{"active", true},
				{"center_x", 900.0f},
				{"center_y", 900.0f},
				{"effective_radius", 600.0f},
				{"black_markets", 2}
			})
		});
		std::vector<AIControlAdapterScudStormZoneThreatSnapshot> threats;
		AIControlAdapterScudStormPlacementChoice choice =
			AIControlAdapterScudStormManager::choosePlacement(zones, threats, 100000u, 500.0f);
		expect(choice.hasPlacement, "placement should be available when zones exist");
		expect(choice.zoneId == 1u, "placement should prefer safe developed main base over active front");
		expect(choice.role == "interior", "main-base placement should use interior role");
		expect(choice.reason == "fallback_interior", "main-base placement should keep reason");
	}

	{
		nlohmann::json zones = nlohmann::json::array({
			nlohmann::json::object({
				{"anchor_id", 1u},
				{"is_main_base", true},
				{"developed", true},
				{"center_x", 100.0f},
				{"center_y", 100.0f},
				{"effective_radius", 600.0f}
			}),
			nlohmann::json::object({
				{"anchor_id", 2u},
				{"developed", true},
				{"center_x", 900.0f},
				{"center_y", 900.0f},
				{"effective_radius", 600.0f},
				{"black_markets", 2}
			})
		});
		std::vector<AIControlAdapterScudStormZoneThreatSnapshot> threats;
		AIControlAdapterScudStormZoneThreatSnapshot threat;
		threat.zoneId = 1u;
		threat.lastSeenTick = 99000u;
		threats.push_back(threat);
		AIControlAdapterScudStormPlacementChoice choice =
			AIControlAdapterScudStormManager::choosePlacement(zones, threats, 100000u, 500.0f);
		expect(choice.zoneId == 2u, "recently attacked main base should lose to safer developed rear zone");
		expect(choice.reason == "safe_rear", "safe rear placement should report safe_rear");
	}

	return 0;
}
