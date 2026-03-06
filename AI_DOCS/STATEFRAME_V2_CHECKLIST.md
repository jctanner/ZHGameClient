# StateFrame v2 Implementation Checklist

## Scope

Implement `StateFrame v2` in the in-process adapter with full + delta snapshots, event streaming, and optional heavy streams.

Target file:

1. `GeneralsMD/Code/GameEngine/Source/GameClient/AIControlAdapter.cpp`

Reference specs:

1. `AI_DOCS/STATEFRAME_V2.md`
2. `AI_DOCS/SIMULATION.md`

## Phase 0: Protocol and Config

1. [ ] Add `schema_version` constant (`2`) to adapter responses.
2. [ ] Add adapter config constants:
3. [ ] `kStateFrameHz` (default 10)
4. [ ] `kFullSnapshotIntervalFrames` (default 10-20 ticks depending on rate)
5. [ ] `kMaxPayloadBytes` safety cap
6. [ ] Extend `HelloAck.capabilities` with:
7. [ ] `stateframe_v2`
8. [ ] `event_batch_v2`
9. [ ] `subscribe_streams`

## Phase 1: Internal State Containers

1. [ ] Add per-client stream subscription state in `AIControlAdapterState`:
2. [ ] `stream_stateframe`
3. [ ] `stream_events`
4. [ ] `stream_logs`
5. [ ] `stream_fog` (optional heavy)
6. [ ] `stream_threat` (optional heavy)
7. [ ] Add previous-frame caches for delta:
8. [ ] `prevSelfUnitsById`
9. [ ] `prevSelfBuildingsById`
10. [ ] `prevVisibleEnemiesById`
11. [ ] `prevEconomy`
12. [ ] `prevTech`
13. [ ] Add `lastSeenEnemyById` cache for enemy intel.
14. [ ] Add frame bookkeeping:
15. [ ] `lastStateFrameSentAtFrame`
16. [ ] `lastFullStateFrameSentAtFrame`

## Phase 2: Reader Primitives

1. [ ] Add `collectSelfObjects(Player*, ...)` helper:
2. [ ] Emit object id/template/class/pos/hp/status.
3. [ ] Split into `units` and `buildings`.
4. [ ] Add `collectVisibleEnemies(Player*, ...)` helper:
5. [ ] Emit id/template/pos/first_seen/last_seen.
6. [ ] Update `lastSeenEnemyById`.
7. [ ] Add `collectEconomy(Player*, ...)` helper:
8. [ ] Money, income rate, power state.
9. [ ] Add `collectTech(Player*, ...)` helper:
10. [ ] Upgrades completed/in-progress, special powers if available.
11. [ ] Add map metadata helper:
12. [ ] Map name/width/height if available; otherwise omit gracefully.

## Phase 3: Event Detection

1. [ ] Add diff helpers:
2. [ ] `emitCreateDestroyEvents(prev, curr, object_kind)`
3. [ ] `emitStateChangeEvents(prev, curr)`
4. [ ] `emitEconomyChangeEvents(prev, curr)`
5. [ ] `emitTechChangeEvents(prev, curr)`
6. [ ] Build `EventBatch` when changes exist.
7. [ ] Include frame and compact payload per event.

## Phase 4: Delta Encoding

1. [ ] Add `buildStateFrameFull(...)` serializer.
2. [ ] Add `buildStateFrameDelta(...)` serializer:
3. [ ] Only include changed fields for existing objects.
4. [ ] Include add/remove lists for object lifecycle.
5. [ ] Add full snapshot fallback if delta cache invalid or desynced.

## Phase 5: Stream Scheduling

1. [ ] Hook publisher in adapter `update()` loop after message handling.
2. [ ] Gate by:
3. [ ] Client connected
4. [ ] In-game/player state available
5. [ ] Subscription enabled
6. [ ] Tick cadence reached
7. [ ] Send full frame on interval, delta otherwise.
8. [ ] Send `EventBatch` immediately when non-empty and subscribed.

## Phase 6: Subscribe / Unsubscribe Commands

1. [ ] Add `Subscribe` and `Unsubscribe` message type handling.
2. [ ] Parse `streams[]` and update per-client stream flags.
3. [ ] Return `ActionAck` success/failure with reason codes.
4. [ ] Supported stream names:
5. [ ] `stateframe`
6. [ ] `events`
7. [ ] `logs`
8. [ ] `fog`
9. [ ] `threat`

## Phase 7: Safety and Performance

1. [ ] Add payload-size guard before `sendJsonLine`.
2. [ ] Add optional max object count clamps per frame.
3. [ ] Avoid heavy scans when stream unsubscribed.
4. [ ] Ensure null-safe object/template/player dereferences.
5. [ ] Ensure deterministic ordering:
6. [ ] Sort arrays by `id` before serialization.

## Phase 8: CLI and Docs Alignment

1. [ ] Add `zhctl` shell/watch support for:
2. [ ] `watch game` -> `Subscribe stateframe/events`
3. [ ] `unwatch` -> `Unsubscribe`
4. [ ] Update docs:
5. [ ] `AI_DOCS/CONTROL_PROTOCOL.md` (`StateFrame v2`, stream names)
6. [ ] `AI_DOCS/AI_HOOKS.md` (implementation plan note)
7. [ ] `AI_DOCS/MVP_LOBBY_AUTOMATION.md` (if CLI examples added)

## Phase 9: Validation Checklist

1. [ ] Single-player skirmish 30+ minute soak with streaming enabled.
2. [ ] Verify no crash on rapid connect/disconnect.
3. [ ] Verify deterministic event ordering for same scenario replay.
4. [ ] Verify full snapshot recovers state after dropped deltas.
5. [ ] Verify payload caps and graceful truncation behavior.
6. [ ] Verify no direct simulation mutation added in telemetry path.

## Minimal Vertical Slice (Recommended First)

1. [ ] `StateFrame` full only (no delta) at 2 Hz with:
2. [ ] frame, player_index
3. [ ] resources.money
4. [ ] self unit/building `id/template/x/y/hp`
5. [ ] visible enemy `id/template/x/y`
6. [ ] Add `Subscribe`/`Unsubscribe` for `stateframe`.
7. [ ] Add one `EventBatch` kind: `object.created` and `object.destroyed`.
