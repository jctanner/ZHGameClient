# StateFrame v2 Proposal

## Goal

Define a richer adapter -> brain data contract for AI orchestration while staying deterministic and multiplayer-safe.

## Message Types

1. `StateFrame` (periodic snapshot, full or delta)
2. `EventBatch` (asynchronous discrete changes)
3. `QueryResult` (on-demand expensive views, e.g. pathing/travel time)

## Cadence

1. `StateFrame` baseline: 5-10 Hz
2. `EventBatch`: immediate on change
3. `StateFrame` full snapshot: every 1-2 seconds
4. `StateFrame` delta snapshot: all other ticks

## StateFrame Envelope

```json
{
  "type": "StateFrame",
  "session_id": "abc123",
  "frame": 123456,
  "tick_ms": 100,
  "full": false,
  "player_index": 0,
  "data": {}
}
```

## `data` Schema

### 1) World

1. `map`: `{ name, width, height }`
2. `regions`: optional region/chokepoint IDs
3. `fog`: optional compressed fog/shroud mask
4. `threat_map`: optional compressed enemy influence grid

### 2) Economy

1. `resources`: `{ money, income_per_minute, spend_per_minute }`
2. `power`: `{ produced, used, low_power }`
3. `supply_nodes`: list of `{ id, x, y, cash_value, depleted, owner_player_index? }`
4. `workers`: assignment summary by source `{ source_id, assigned, ideal, efficiency }`

### 3) Tech and Powers

1. `upgrades_completed`: string[]
2. `upgrades_in_progress`: `{ name, progress_0_1 }[]`
3. `special_powers`: `{ name, ready, cooldown_frames }[]`
4. `faction`: `{ side, base_side, rank_level, science_points }`

### 4) Self Objects

1. `units`: list of:
   `{
      id, template, class, x, y, hp_cur, hp_max,
      status, idle, target_id?, command?, command_age_frames,
      weapon_cooldown_frames?, veterancy?, stealth_state?
    }`
2. `buildings`: list of:
   `{
      id, template, x, y, hp_cur, hp_max,
      under_construction, build_progress_0_1?,
      queue?, rally_point?
    }`
3. `transports`: occupancy summary

### 5) Enemy and Intel

1. `visible_enemies`: list with `{ id, template, x, y, hp_est?, first_seen_frame, last_seen_frame }`
2. `enemy_summary`: counts by type/class
3. `last_known_enemy_sites`: inferred positions with confidence score

### 6) Control and Arbitration

1. `writer_locks`: `{ object_id, owner: human|bot, until_frame? }[]`
2. `human_override`: `{ active, window_frames_remaining }`
3. `command_budget`: `{ max_per_second, used_last_second, queue_depth }`

## EventBatch Kinds

1. `object.created`
2. `object.destroyed`
3. `object.visible`
4. `object.lost_vision`
5. `build.started`
6. `build.completed`
7. `build.canceled`
8. `queue.changed`
9. `upgrade.completed`
10. `power.changed`
11. `engagement.started`
12. `engagement.ended`
13. `resource.changed`

## Query APIs (On-Demand)

1. `Game.Query path=game.pathing.travel_time` args: `{ object_id|unit_class, from:{x,y}, to:{x,y} }`
2. `Game.Query path=game.affordances` args: `{ object_id }`
3. `Game.Query path=game.region_control`
4. `Game.Query path=game.enemy_inference`

## Performance Rules

1. Use stable object IDs and send only changed fields in delta frames.
2. Omit heavy grids unless subscribed (`fog`, `threat_map`).
3. Keep strings interned/enumerated where possible (`status`, `class`, `command`).
4. Include `schema_version` for compatibility.

## Example Minimal Delta Frame

```json
{
  "type": "StateFrame",
  "schema_version": 2,
  "frame": 123460,
  "full": false,
  "player_index": 0,
  "data": {
    "economy": { "resources": { "money": 2350 } },
    "self": {
      "units": [
        { "id": 10021, "x": 412.5, "y": 198.0, "status": "moving", "command": "MoveTo" }
      ]
    },
    "enemy": {
      "visible_enemies": [
        { "id": 5007, "template": "ChinaTankBattleMaster", "x": 602.1, "y": 221.4, "last_seen_frame": 123460 }
      ]
    }
  }
}
```

## Notes

1. Keep direct cash mutation commands out of multiplayer command sets unless they are synchronized by a deterministic network command path.
2. Prefer legal player-equivalent actions for all control commands.
3. See `AI_DOCS/SIMULATION.md` and `AI_DOCS/AI_COMMANDS.md`.
