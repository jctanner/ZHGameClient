# Control Protocol (v1)

## Purpose

Define how a secondary controller app talks to the in-game adapter for:

1. Session control (menus/lobbies/game setup)
2. In-game commands (chat/actions)
3. Queries, acknowledgements, and events

## Transport

Use local Windows named pipe:

1. `\\.\pipe\zh_ai_control`

Rules:

1. Local machine only.
2. UTF-8 JSON Lines (one JSON object per line).
3. Full-duplex stream.
4. Adapter must create the pipe with explicit ACLs (same user SID by default).
5. Reject connections from non-authorized local principals.

## Envelope

Every message should include:

1. `type` (string)
2. `request_id` (string, required for requests)
3. `ts` (optional unix ms)
4. `source` (optional: `controller|adapter`)
5. `exec` (optional: `serial|parallel`, default `serial`)

Naming convention:

1. Top-level envelope fields and most `args` keys use `snake_case`.
2. Existing adapter command payloads may require legacy keys where implemented (for example `controlId` for `Menu.Click` / `Menu.SetText`).

## Core Message Types

Requests from controller:

1. `Hello`
2. `SessionCommand`
3. `IntentBatch`
4. `Ping`
5. `Subscribe`
6. `Unsubscribe`

Responses/events from adapter:

1. `HelloAck`
2. `ActionAck`
3. `QueryResult`
4. `SessionState`
5. `StateFrame`
6. `EventBatch`
7. `Pong`
8. `Error`
9. `AdapterLog`

## Hello Handshake

Controller -> Adapter:

```json
{"type":"Hello","request_id":"h1","protocol":"zh-ai-control-v1","client_name":"zhctl","client_version":"0.1.0","auth":{"kind":"none|shared_secret","token":"<optional>"}}
```

Adapter -> Controller:

```json
{"type":"HelloAck","request_id":"h1","ok":true,"protocol":"zh-ai-control-v1","adapter_version":"0.1.0","session_id":"a8b83a71","capabilities":["session","chat","query"]}
```

## Session Commands

Request shape:

```json
{"type":"SessionCommand","request_id":"s42","cmd":"Lobby.Join","args":{"room_name":"My Lobby"}}
```

Supported `cmd` values (v1):

1. `Session.Status`
2. `Menu.Open`
3. `Menu.Click`
4. `Menu.SetText`
5. `Menu.ListControls`
6. `Lobby.ListPlayers`
7. `Lobby.Join`
8. `Lobby.Leave`
9. `Lobby.SetReady`
10. `GameSetup.GetSettings`
11. `GameSetup.SetSetting`
12. `GameSetup.GetPlayers`
13. `Chat.Send`
14. `Game.Query`
15. `Game.QueueUnit`
16. `Game.BuildWorker`
17. `Game.QueueSoldiersAllBarracks`
18. `Game.QueueRpgTroopersAllBarracks`
19. `Game.QueueQuadsAllWarFactories`
20. `Game.QueueScorpionsAllWarFactories`
21. `Game.QueueRadarVansAllWarFactories`
22. `Game.QueueRadarVan`
23. `Game.FindSupplySources`
24. `Game.FindBuildLocationNearSupply`
25. `Game.DozerConstruct`
26. `Game.BuildSupplyStashAuto`
27. `Game.BuildSupplyStashSmart`
28. `Game.BuildBarracksSmart`
29. `Game.BuildCommandCenterSmart`
30. `Game.AttackMove`
31. `Game.AttackMove.RaidSmart`
32. `Game.Camera.Set`
33. `Game.Camera.LookAt`
34. `Game.Camera.SetZoomLimited`
35. `Game.Camera.Get`

`Lobby.Join` args:

1. `room_name` (optional string)
2. `room_id` (optional string/int)

`Menu.Click` args:

1. `controlId` (string; legacy key required by current adapter implementation)

`Menu.SetText` args:

1. `controlId` (string; legacy key required by current adapter implementation)
2. `text` (string)

`Menu.ListControls` args:

1. `kind` (`all|button|text_entry`, optional; default `button`)
2. `include_hidden` (optional bool, default `false`)

`Chat.Send` args:

1. `text` (string)
2. `scope` (`players|allies|everyone`)

`Game.FindSupplySources` args:

1. `player_index` (optional int, default local player)
2. `minimum_cash` (optional int, default `1`)

`Game.FindBuildLocationNearSupply` args:

1. `player_index` (optional int, default local player)
2. `worker_object_id` (optional int, default first dozer/worker)
3. `supply_source_id` (optional int, default closest supply source to worker)
4. `building_template` (string, default `GLASupplyStash`)
5. `minimum_cash` (optional int, default `1`)

`Game.DozerConstruct` args:

1. Same args as `Game.FindBuildLocationNearSupply`

`Game.BuildSupplyStashAuto` args:

1. `player_index` (optional int, default local player)
2. `worker_object_id` (optional int, default first idle dozer/worker)
3. `building_template` (optional override; otherwise inferred from faction)
4. `minimum_cash` (optional int, default `1`)

`Game.BuildSupplyStashSmart` args:

1. Same args as `Game.BuildSupplyStashAuto`
2. Optional `supply_source_id` (int) to force a specific supply dock/source

`Game.BuildSupplyStashSmart` behavior:

1. Try immediate construct near closest supply source.
2. If blocked by unrevealed shroud, issue a move order for the selected worker/dozer toward that supply area.
3. Return success for the move order; call again after reveal to complete build.

`Game.BuildBarracksSmart` args:

1. `player_index` (optional int, default local player)
2. `worker_object_id` (optional int, default first idle dozer/worker)
3. `building_template` (optional override; otherwise inferred by faction/buildability)
4. `anchor_object_id` (optional int; owned object used as placement anchor)
5. `zone_center` (optional object `{x,y}`; preferred zone center for placement)
6. `zone_radius` (optional number; search radius around `zone_center`, default adapter-defined)
7. `strict_zone` (optional bool; if `true`, do not fallback to anchor-based placement when zone has no legal spot)

`Game.BuildBarracksSmart` behavior:

1. Resolve an idle worker/dozer.
2. Infer barracks template if not explicitly provided.
3. Place near anchor object (default: player command center).
4. If no legal location currently exists (often shroud/path), issue a move order toward anchor and return success.

`Game.BuildCommandCenterSmart` args:

1. `player_index` (optional int, default local player)
2. `worker_object_id` (optional int, default first idle dozer/worker)
3. `building_template` (optional override; otherwise inferred by faction/buildability)
4. `anchor_object_id` (optional int; owned object used as placement anchor, defaults to command center)
5. `zone_center` (optional object `{x,y}`; preferred zone center for placement)
6. `zone_radius` (optional number; search radius around `zone_center`, default adapter-defined)
7. `strict_zone` (optional bool; if `true`, do not fallback to anchor-based placement when zone has no legal spot)

`Game.BuildArmsDealerSmart` args:

1. `player_index` (optional int, default local player)
2. `worker_object_id` (optional int, default first idle dozer/worker)
3. `building_template` (optional override; otherwise inferred by faction/buildability)
4. `anchor_object_id` (optional int; owned object used as placement anchor)
5. `zone_center` (optional object `{x,y}`; preferred zone center for placement)
6. `zone_radius` (optional number; search radius around `zone_center`, default adapter-defined)
7. `strict_zone` (optional bool; if `true`, do not fallback to anchor-based placement when zone has no legal spot)

`Game.BuildPalaceSmart` args:

1. `player_index` (optional int, default local player)
2. `worker_object_id` (optional int, default first idle dozer/worker)
3. `building_template` (optional override; otherwise inferred by faction/buildability)
4. `anchor_object_id` (optional int; owned object used as placement anchor)
5. `zone_center` (optional object `{x,y}`; preferred zone center for placement)
6. `zone_radius` (optional number; search radius around `zone_center`, default adapter-defined)
7. `strict_zone` (optional bool; if `true`, do not fallback to anchor-based placement when zone has no legal spot)

`Game.BuildBlackMarketSmart` args:

1. `player_index` (optional int, default local player)
2. `worker_object_id` (optional int, default first idle dozer/worker)
3. `building_template` (optional override; otherwise inferred by faction/buildability)
4. `anchor_object_id` (optional int; owned object used as placement anchor)
5. `zone_center` (optional object `{x,y}`; preferred zone center for placement)
6. `zone_radius` (optional number; search radius around `zone_center`, default adapter-defined)
7. `strict_zone` (optional bool; if `true`, do not fallback to anchor-based placement when zone has no legal spot)

`Game.BuildCommandCenterSmart` behavior:

1. Resolve an idle worker/dozer.
2. Infer command center template if not explicitly provided.
3. Place near anchor object (default: player command center).
4. If no legal location currently exists (often shroud/path), issue a move order toward anchor and return success.

`Game.QueueUnit` args:

1. `player_index` (optional int, default local player)
2. `producer_object_id` (optional int; if omitted, adapter finds matching producer)
3. `producer_kind` (`command_center|any`, optional; default `command_center`)
4. `unit_template` (required string)

`Game.BuildWorker` args:

1. Same args as `Game.QueueUnit`
2. If `unit_template` is omitted, adapter infers faction worker/dozer template.

`Game.QueueSoldiersAllBarracks` args:

1. `player_index` (optional int, default local player)
2. `count` (optional int, default `1`, clamped `1..9`)

`Game.QueueRpgTroopersAllBarracks` args:

1. `player_index` (optional int, default local player)
2. `count` (optional int, default `1`, clamped `1..9`)

`Game.QueueQuadsAllWarFactories` args:

1. `player_index` (optional int, default local player)
2. `count` (optional int, default `1`, clamped `1..9`)

`Game.QueueScorpionsAllWarFactories` args:

1. `player_index` (optional int, default local player)
2. `count` (optional int, default `1`, clamped `1..9`)

`Game.QueueRadarVansAllWarFactories` args:

1. `player_index` (optional int, default local player)
2. `count` (optional int, default `1`, clamped `1..9`)

`Game.QueueRadarVan` args:

1. `player_index` (optional int, default local player)

`Game.AttackMove` args:

1. `player_index` (optional int, default local player)
2. `x` (required number)
3. `y` (required number)
4. `object_id` (optional int, single controlled object)
5. `object_ids` (optional int[], multiple controlled objects)

`Game.AttackMove.RaidSmart` args:

1. `player_index` (optional int, default local player)
2. `min_units` (optional int, default `20`)
3. `group_size` (optional int, default `20`)
4. `distance` (optional number, default `3000`)
5. `direction_index` (optional int `0..3`; right/left/down/up cycle if omitted)

`Game.Camera.Set` args:

1. `angle` (optional number, radians)
2. `pitch` (optional number, radians)
3. `zoom` (optional number)
4. `zoom_multiplier` (optional number, multiplies current zoom)
5. `top_down` (optional bool, convenience pitch preset near -90 degrees)
6. `height` (optional number, camera height-above-ground)
7. `height_multiplier` (optional number, multiplies current camera height-above-ground)

`Game.Camera.LookAt` args:

1. `x` (required number) and `y` (required number)
2. alternatively `zone_center` object with `{x,y}`

`Game.Camera.SetZoomLimited` args:

1. `enabled` (required bool; `true` clamps camera height to engine min/max, `false` disables that clamp)
2. alias `zoom_limited` (optional bool, same behavior as `enabled`)

`Game.Camera.Get`:

1. Returns `QueryResult` with current camera `x,y,z,angle,pitch,zoom,height_above_ground,zoom_limited`.
2. Also includes defaults/limits when available: `default_height`, `min_height`, `max_height`.

## In-Game Intent Batch

Use for match actions:

```json
{
  "type":"IntentBatch",
  "request_id":"i7",
  "frame_hint":12345,
  "intents":[
    {"intent_id":"i7-1","kind":"BuildStructure","args":{"template":"GLAScudStorm","near":"base_center"}},
    {"intent_id":"i7-2","kind":"MoveGroup","args":{"group_id":"alpha","x":120.0,"y":345.0}}
  ]
}
```

## Game.Query (via SessionCommand)

Request:

```json
{"type":"SessionCommand","request_id":"q1","cmd":"Game.Query","args":{"path":"game.status"}}
```

Common `path` values:

1. `game.status`
2. `game.summary`
3. `game.all`
4. `game.local_player`
5. `game.player`
6. `game.players`
7. `game.faction`
8. `game.resources`
9. `game.units`
10. `game.objects`
11. `game.objects_map`
12. `game.objects_units_map`
13. `game.objects_buildings_map`
14. `game.idle_workers`
15. `game.objects_cache_status`
16. `game.objects_cache_refresh`
17. `game.zone_counts`
18. `game.visible_enemies`

`game.objects_units_map`, `game.objects_buildings_map`, and `game.idle_workers` are served from a short-lived adapter cache (owned objects for the selected player). Responses include:

1. `cache_version` (monotonic per refresh)
2. `cache_age_ms` (age of the cached snapshot)

`game.zone_counts` args:

1. `player_index` (optional int, default local player)
2. `zone_center` (optional object `{x,y}`; omitted means whole owned map state)
3. `zone_radius` (optional number; default adapter-defined)

## Acknowledgements

Any request requiring execution returns `ActionAck`:

```json
{"type":"ActionAck","request_id":"s42","ok":true}
```

Failure example:

```json
{"type":"ActionAck","request_id":"s42","ok":false,"code":"invalid_state","reason":"not in lobby"}
```

## Streaming State

Adapter may push periodic state:

```json
{"type":"SessionState","state":{"screen":"Lobby","room_name":"My Lobby","players":[{"slot":0,"name":"You","ready":true}]}}
```

```json
{"type":"StateFrame","frame":12345,"player":{"money":1000,"power":{"prod":200,"used":150}}}
```

## Error Contract

Protocol-level errors use:

```json
{"type":"Error","request_id":"x1","code":"bad_request","reason":"missing cmd"}
```

Suggested codes:

1. `bad_request`
2. `unsupported_cmd`
3. `invalid_state`
4. `rate_limited`
5. `internal_error`

## Idempotency and Ordering

1. Controller must make `request_id` globally unique per process lifetime (UUID recommended).
2. Adapter must echo `request_id` in all direct replies.
3. Adapter must keep a dedupe cache per `session_id` (recommended 60s window).
4. If a duplicate `request_id` is received within the dedupe window, adapter must return the original reply and must not re-execute side effects.
5. Adapter should execute requests in receive order when `exec` is omitted or `serial`.
6. Adapter may execute requests concurrently only when `exec` is explicitly `parallel`.

## Safety Controls

Adapter should enforce:

1. Max command rate (example: 10 req/s session, 30 req/s intents).
2. Command validation by current game/session state.
3. Optional allowlist by mode (`human_only|bot_only|hybrid`).

See also:

1. `AI_DOCS/SIMULATION.md` for lockstep/desync behavior and why direct money mutation commands should be single-player only.

## Minimal CLI Mapping (suggested)

Current Python CLI examples:

1. `python .\ZHGameClient\scripts\ai\zhctl.py hello`
2. `python .\ZHGameClient\scripts\ai\zhctl.py status --pretty`
3. `python .\ZHGameClient\scripts\ai\zhctl.py list-controls --kind button`
4. `python .\ZHGameClient\scripts\ai\zhctl.py menu-click --control-id "MainMenu.wnd:ButtonMultiplayer"`
5. `python .\ZHGameClient\scripts\ai\zhctl.py chat-send --scope everyone --text "hello from controller"`
6. `python .\ZHGameClient\scripts\ai\zhctl.py query --path game.resources --pretty`

## CLI Modes (required)

Support two user-facing modes:

1. Direct command mode (one-shot)
2. Interactive shell mode (REPL + live stream)

### 1) Direct Command Mode

Examples:

1. `python .\ZHGameClient\scripts\ai\zhctl.py menu-click --control-id "MainMenu.wnd:ButtonMultiplayer"`
2. `python .\ZHGameClient\scripts\ai\zhctl.py query --path game.status --pretty`
3. `python .\ZHGameClient\scripts\ai\zhctl.py chat-send --scope everyone --text "hello"`

Behavior:

1. Connect -> send one request -> print ack/result -> exit with code.
2. Exit code `0` on success, non-zero on error.

### 2) Shell Mode

Entry:

1. Not implemented in current `zhctl.py` (planned).

Shell capabilities:

1. Execute same commands as direct mode.
2. Subscribe to live events/logs.
3. Show request/ack correlation.
4. Keep command history and aliases.

Suggested shell built-ins:

1. `help`
2. `connect`
3. `status`
4. `watch session`
5. `watch game`
6. `watch logs`
7. `unwatch <stream>`
8. `send <raw-json>`
9. `last`
10. `quit`

## Streaming and Log View

To support shell live output, adapter should emit:

1. `SessionState` (menu/lobby/setup state updates)
2. `StateFrame` (periodic in-game snapshots)
3. `EventBatch` (structured events)
4. `AdapterLog` (optional textual log lines)

`EventBatch` example:

```json
{
  "type":"EventBatch",
  "events":[
    {"kind":"session.transition","from":"MainMenu","to":"Lobby"},
    {"kind":"lobby.player_joined","name":"Alice","slot":2},
    {"kind":"action.executed","request_id":"s42","cmd":"Chat.Send"}
  ]
}
```

`AdapterLog` example:

```json
{"type":"AdapterLog","level":"info","channel":"chat","message":"sendChat scope=everyone bytes=22"}
```

## Subscribe / Unsubscribe (v1)

Add explicit stream control requests:

1. `Subscribe`
2. `Unsubscribe`

Examples:

```json
{"type":"Subscribe","request_id":"sub1","streams":["session","events","logs"]}
```

```json
{"type":"Unsubscribe","request_id":"sub2","streams":["logs"]}
```

Ack:

```json
{"type":"ActionAck","request_id":"sub1","ok":true}
```

## Shell Output Format (recommended)

Each async line should include:

1. clock time
2. stream/type
3. compact payload

Example:

```text
[14:32:10.224] event lobby.player_joined name=Alice slot=2
```

```text
[14:32:11.901] ack s42 ok=true cmd=Chat.Send
```
