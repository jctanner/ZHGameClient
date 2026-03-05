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

## Core Message Types

Requests from controller:

1. `Hello`
2. `SessionCommand`
3. `IntentBatch`
4. `Query`
5. `Ping`
6. `Subscribe`
7. `Unsubscribe`

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
5. `Lobby.ListPlayers`
6. `Lobby.Join`
7. `Lobby.Leave`
8. `Lobby.SetReady`
9. `GameSetup.GetSettings`
10. `GameSetup.SetSetting`
11. `GameSetup.GetPlayers`
12. `Chat.Send`
13. `Game.Query`
14. `Game.QueueUnit`
15. `Game.BuildWorker`
16. `Game.FindSupplySources`
17. `Game.FindBuildLocationNearSupply`
18. `Game.DozerConstruct`
19. `Game.BuildSupplyStashAuto`
20. `Game.BuildSupplyStashSmart`
21. `Game.BuildBarracksSmart`

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

`Game.BuildBarracksSmart` behavior:

1. Resolve an idle worker/dozer.
2. Infer barracks template if not explicitly provided.
3. Place near anchor object (default: player command center).
4. If no legal location currently exists (often shroud/path), issue a move order toward anchor and return success.

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

## Query

Request:

```json
{"type":"Query","request_id":"q1","path":"session.players"}
```

Common `path` values:

1. `session.state`
2. `session.players`
3. `session.settings`
4. `game.local_player`
5. `game.resources`

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

## Minimal CLI Mapping (suggested)

1. `zhctl hello`
2. `zhctl session status`
3. `zhctl lobby players`
4. `zhctl lobby join --room "X"`
5. `zhctl lobby ready --on`
6. `zhctl setup set --key map --value "Tournament Desert"`
7. `zhctl chat send --scope everyone --text "hello from controller"`

## CLI Modes (required)

Support two user-facing modes:

1. Direct command mode (one-shot)
2. Interactive shell mode (REPL + live stream)

### 1) Direct Command Mode

Examples:

1. `zhctl lobby join --room "My Lobby"`
2. `zhctl setup set --key map --value "Tournament Desert"`
3. `zhctl chat send --scope everyone --text "hello"`

Behavior:

1. Connect -> send one request -> print ack/result -> exit with code.
2. Exit code `0` on success, non-zero on error.

### 2) Shell Mode

Entry:

1. `zhctl shell`

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
