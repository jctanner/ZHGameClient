# AI Hooks Architecture and Plan

## Goal

Build a custom AI system that:

1. Behaves like a normal player (same legal command surface).
2. Can be controlled/debugged from an external console/client.
3. Is safe for multiplayer/desync-sensitive simulation.

## Non-Negotiable Constraints

1. Do not mutate sim state directly from custom bot code.
2. Do not spawn/delete/edit objects through ad-hoc internals.
3. Issue actions through existing game command paths only.
4. Keep bot logic out-of-process when possible (stability + iteration speed).

## Recommended Architecture

Use a two-process model:

1. In-process `Adapter` (inside GameClient build)
2. Out-of-process `Brain` (CLI/service)

Also split control responsibilities into two domains:

1. `Session Control Plane` (shell/menus/lobby/game setup)
2. `In-Game Command Plane` (actions during match)

### 1) In-Process Adapter

Responsibilities:

1. Collect per-frame game state for one controlled player.
2. Publish state snapshots/events via IPC.
3. Receive high-level intents and map them to legal engine commands.
4. Enforce action throttles and safety checks.

For session control, adapter should also expose:

1. Menu navigation primitives (open menu, click control, set text).
2. Lobby/game-room operations (join/leave room, ready state, map/mode options).
3. Query APIs for lobby roster and game settings.

Suggested hook points:

1. Frame tick / logic update loop:
   - `GeneralsMD/Code/GameEngine/Source/GameLogic/AI/AI.cpp` (`AI::update`)
   - `GeneralsMD/Code/GameEngine/Source/Common/RTS/Player.cpp` (`Player::update`)
2. Strategic AI integration points:
   - `.../GameLogic/AI/AIPlayer.cpp`
   - `.../GameLogic/AI/AISkirmishPlayer.cpp`
3. Command-capable surfaces:
   - `.../GameLogic/Object/Update/AIUpdate.cpp` (`aiMove`, `aiAttack`, `aiGuard`, waypoint follow)
   - `.../GameLogic/ScriptEngine/ScriptActions.cpp` (existing high-level scripted actions)

### 2) Out-of-Process Brain

Responsibilities:

1. Consume snapshots and maintain world model.
2. Run strategy/tactics modules.
3. Emit intents (not low-level engine calls).
4. Host control interfaces (terminal + optional websocket).

## Data Flow

1. Engine frame N -> Adapter captures state.
2. Adapter publishes `StateFrame(N)` to Brain.
3. Brain computes intents for frame >= N.
4. Adapter validates + executes intents via legal command API.
5. Adapter publishes `ActionAck` + `ExecutionResult`.

## IPC Contract (v1)

Transport:

1. Windows Named Pipe (recommended for local).
2. Optional TCP loopback for multi-tool integration.

Messages:

1. `Hello`
2. `StateFrame`
3. `EventBatch`
4. `IntentBatch`
5. `ActionAck`
6. `Health/Ping`
7. `SessionState` (shell/menu/lobby/game-setup state)
8. `SessionCommand`

`Intent` examples:

1. `BuildStructure { template, near?, at?, priority }`
2. `TrainTeam { teamProto }`
3. `MoveGroup { groupId, position|waypointPath }`
4. `Attack { groupId, targetId|position }`
5. `UsePower { power, target }`
6. `SetMode { human_only|bot_only|hybrid }`

`SessionCommand` examples:

1. `Menu.Open { screen }`
2. `Menu.Click { controlId }`
3. `Menu.SetText { controlId, text }`
4. `Lobby.ListPlayers {}`
5. `Lobby.Join { roomId|roomName }`
6. `Lobby.Leave {}`
7. `Lobby.SetReady { ready }`
8. `GameSetup.GetSettings {}`
9. `GameSetup.SetSetting { key, value }`
10. `GameSetup.GetPlayers {}`

## Command Execution Strategy

Priority order:

1. Prefer command paths that mirror player-issued actions.
2. Fall back to existing AI/script action entry points only when equivalent.
3. Never call deep object mutation paths directly.

Practical mapping layer:

1. `Intent -> EngineCommand` in Adapter.
2. Validate prerequisites (ownership, buildability, cooldown, range, visibility).
3. Emit rejection reason if invalid.

## Human/Bot Arbitration

Modes:

1. `human_only`
2. `bot_only`
3. `hybrid` (human command preempts bot for configurable window)

Rules:

1. Single writer per unit/group per frame.
2. Cooldown on command flip-flopping.
3. Optional APM cap to look natural.

## Observability

Add structured logs:

1. Snapshot rate, queue depth, latency.
2. Intent acceptance/rejection reasons.
3. Final command issued to engine.
4. Per-frame checksum/sequence markers for replay/debug.

Artifacts:

1. `bot_events.log` (adapter)
2. `brain_decisions.log` (brain)
3. Optional JSONL trace for offline analysis.

## Implementation Plan

## Phase 0: Skeleton

1. Add adapter module stub compiled into GameClient build.
2. Add named pipe server/client handshake.
3. Add no-op brain CLI that prints frames and sends no intents.

Deliverable:

1. Stable streaming of `StateFrame` at fixed cadence.

## Phase 1: Read-Only State

1. Export minimal state for controlled player:
   - money/power/supply
   - owned units/buildings (id, template, hp, pos, status)
   - visible enemies
2. Add frame numbering and ack protocol.
3. Export `SessionState` for shell/lobby/game-setup:
   - current screen/menu
   - lobby/room identity
   - players + slots + ready flags
   - key game setup settings (map, mode, teams, options)

Deliverable:

1. External console `status` command shows live game state.
2. External console `session.status` shows lobby/menu state and players.

## Phase 2: Safe Action Ingress

1. Implement small in-game intent set:
   - move/attack/guard
   - build structure
2. Route through legal command surfaces.
3. Add rejection diagnostics.
4. Implement session commands:
   - join/leave lobby
   - set/query lobby and game setup options
   - set ready/unready

Deliverable:

1. Console can issue commands to bot player and see deterministic acknowledgements.
2. Console can drive menus/lobby similarly to headless workflow.

## Phase 3: Strategy Layer

1. Add planner modules:
   - economy manager
   - production manager
   - combat manager
2. Add blacklist/cooldown system to prevent command spam.
3. Add behavior profiles per faction.

Deliverable:

1. Bot can play full skirmish loop without external command spam.

## Phase 4: Hybrid Control + Tooling

1. Add human override arbitration.
2. Add scenario scripts and regression harness.
3. Add replay-friendly trace export.
4. Add macro workflows:
   - `find_and_join_lobby`
   - `configure_match`
   - `ready_and_launch`

Deliverable:

1. Human+bot cooperative control with clear ownership semantics.

## Risks and Mitigations

1. Desync risk:
   - Mitigate by strict legal command path and no direct sim mutations.
2. Command race conditions:
   - Mitigate with arbiter + single writer rules.
3. Performance overhead:
   - Mitigate with delta snapshots and capped tick export.
4. Debug complexity:
   - Mitigate with frame-stamped acks and JSONL traces.

## Suggested Next Step

Implement Phase 0 and Phase 1 first, then validate:

1. no crashes for full skirmish duration
2. stable IPC under load
3. frame-ordered action acknowledgements

## First Hello World (Recommended)

Use this as the first end-to-end vertical slice:

1. Human launches game manually.
2. Human joins multiplayer game manually.
3. External control process sends: `Chat.Send("hello from bot")`.
4. In-game adapter forwards to normal in-game chat path.
5. Other players receive the chat normally.

Why this first:

1. Proves IPC ingress works.
2. Proves adapter can trigger real networked game action.
3. Uses low-risk command with visible verification.
4. Avoids unit-control/desync complexity at start.

### Concrete Engine Path for Chat

Current in-game chat send call is:

1. `TheNetwork->sendChat(msg, playerMask)`
   - `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/InGameChat.cpp`

Network implementation path:

1. `Network::sendChat(UnicodeString, Int playerMask)`
   - `Core/GameEngine/Source/GameNetwork/Network.cpp`
2. `ConnectionManager::sendChat(...)`
   - `Core/GameEngine/Source/GameNetwork/ConnectionManager.cpp`

Public interface:

1. `NetworkInterface::sendChat(UnicodeString, Int playerMask)`
   - `Core/GameEngine/Include/GameNetwork/NetworkInterface.h`

### Minimal Adapter API for This Slice

Add one inbound command only:

1. `Chat.Send`
   - fields:
     - `text` (string)
     - `scope` (`players|allies|everyone`)

Adapter behavior:

1. Resolve `scope -> playerMask` using the same logic as `InGameChat.cpp`.
2. Apply language filter if required (`TheLanguageFilter->filterLine(...)` parity).
3. Call `TheNetwork->sendChat(...)`.
4. Emit `ActionAck` with success/failure and reason.

### Acceptance Criteria

1. Chat message appears in local chat UI.
2. Chat message is visible on at least one remote client.
3. Adapter returns deterministic ack for each request id.
4. No crash or assert during repeated sends (100+ messages test with rate limit).
