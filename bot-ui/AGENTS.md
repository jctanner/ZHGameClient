# Bot UI Subproject Plan (Tkinter)

## Scope
Build a Python Tkinter desktop UI in `ZHGameClient/bot-ui` that:
1. Connects to the Zero Hour AI adapter over named pipe (`\\.\\pipe\\zh_ai_control`).
2. Exposes practical controls for session and in-game bot commands.
3. Renders a simplified tactical map on a Tkinter `Canvas` using basic shapes.

Primary outcome:
1. A stable operator console for quick bot control and visual situational awareness.

## Ground Rules
1. Follow `AI_DOCS/CONTROL_PROTOCOL.md` as the source of truth for transport/message formats.
2. Use only legal command paths already exposed by adapter commands.
3. No direct simulation mutation assumptions in multiplayer.
4. Keep first iterations robust and debuggable over feature-rich.

## Non-Goals (Initial)
1. No advanced pathfinding overlays.
2. No heavy custom graphics engine.
3. No replay parser integration.
4. No web frontend; desktop Tkinter only.

## Proposed Project Layout
1. `ZHGameClient/bot-ui/AGENTS.md` (this plan)
2. `ZHGameClient/bot-ui/README.md`
3. `ZHGameClient/bot-ui/requirements.txt` (minimal; prefer stdlib first)
4. `ZHGameClient/bot-ui/src/main.py`
5. `ZHGameClient/bot-ui/src/app.py` (UI shell + tabs/panels)
6. `ZHGameClient/bot-ui/src/protocol/client.py` (pipe client, send/recv, request IDs)
7. `ZHGameClient/bot-ui/src/protocol/messages.py` (message builders/parsers)
8. `ZHGameClient/bot-ui/src/state/store.py` (latest session/game state cache)
9. `ZHGameClient/bot-ui/src/render/map_canvas.py` (world->canvas transform + drawing)
10. `ZHGameClient/bot-ui/src/controllers/commands.py` (button handlers -> protocol calls)
11. `ZHGameClient/bot-ui/src/logging/ui_log.py` (UI log sink)
12. `ZHGameClient/bot-ui/tests/` (protocol/state/render smoke tests)

## UI Plan
Top-level window split into functional regions:
1. Top-right: map canvas region, ~50% of window width and ~50% of window height.
2. Bottom-right: bot controls region under map (~50% width, ~50% height) with:
3. action buttons for bot/game commands
4. single-line text input for user command text
5. output/log box occupying a substantial lower-area section
6. Left side: connection/session status + player metadata panel/table.

Window defaults:
1. Default startup size: `1280x1024`.
2. Minimum size: `1024x768`.

Layout behavior:
1. Use resizable panes/grid weights so the map keeps priority space in the top-right quadrant.
2. Keep control buttons and command input directly below the map.
3. Keep logs visible without tab switching in the lower region.
4. Preserve readable layout at minimum window size.

### Left Panel Controls (MVP)
1. Connection: connect/disconnect, pipe path, hello/status indicators.
2. Menu navigation/start-game quick actions:
3. Main menu navigation (for example multiplayer/skirmish entry buttons)
4. LAN lobby actions (create/join/direct connect/back)
5. Ready/start workflow buttons where exposed by adapter commands
6. Session actions:
   - `Session.Status`
   - `Menu.ListControls`
   - `Menu.Click` (manual `controlId` input)
7. Chat:
   - text input
   - scope dropdown (`players|allies|everyone`)
   - send button (`Chat.Send`)
8. Game query/actions:
   - `Game.Query` presets (`game.status`, `game.objects`, `game.visible_enemies`)
   - `Game.BuildSupplyStashSmart`
   - `Game.BuildBarracksSmart`
   - `Game.QueueUnit`
   - `Game.AttackMove`

### Map Canvas Rendering (MVP)
Use very basic primitives:
1. Friendly units: green circles.
2. Friendly buildings: green rectangles.
3. Visible enemies: red circles/rectangles.
4. Supply/interesting objects: yellow outlined circles.
5. Optional labels: small text for object ID or short template.

### Player Metadata Panel (Required)
Display one row/card per player with live values:
1. Name
2. Player index / slot
3. Team and color
4. Approx base/map position (centroid or anchor object position)
5. Cash
6. Unit count
7. Building count
8. Promotions/rank/science points (when available)
9. Ready/alive/defeated state (when available)

Data source strategy:
1. Prefer `StateFrame` fields when present (`faction`, `resources`, self/enemy summaries).
2. Query fallbacks via `Game.Query` paths (`game.players`, `game.player`, `game.resources`, `game.objects`).
3. Derive counts/centroids from object lists if adapter does not provide direct aggregates.

Coordinate strategy:
1. Read map width/height from best available state (`StateFrame` world map if present; fallback to inferred extents).
2. Compute world->canvas transform with aspect ratio preservation and padding.
3. Support pan/zoom later; fixed fit-to-canvas first.

## Protocol Integration Plan
1. Implement `Hello` handshake and capability display.
2. Support request/ack correlation by `request_id` UUID.
3. Handle these inbound message types first:
   - `HelloAck`
   - `ActionAck`
   - `QueryResult`
   - `SessionState`
   - `StateFrame`
   - `EventBatch`
   - `AdapterLog`
   - `Error`
4. Implement `Subscribe`/`Unsubscribe` for `stateframe`, `events`, `logs`.
5. Keep serial execution default unless explicitly requested.

## Data Model (UI Store)
Maintain one in-memory store with timestamps:
1. `connection`: connected, session_id, capabilities.
2. `session_state`: screen/lobby/settings summary.
3. `game_objects`: owned units/buildings by ID.
4. `visible_enemies`: by ID.
5. `interesting_objects`: supply nodes or inferred points (if available).
6. `acks`: recent action acknowledgements.
7. `logs`: bounded ring buffer.
8. `players`: metadata by player index:
9. `{ name, color, team, cash, unit_count, building_count, promotions, science_points, map_position, status }`

Update rules:
1. Full frame replaces relevant slices.
2. Delta frame patches changed fields (when v2 enabled).
3. Always render from normalized store model, not directly from raw message payload.

## Canvas Update Loop (Required)
Use a regular query-driven refresh loop so the canvas updates even without streaming:
1. Poll `Game.Query` on a fixed interval (default 500 ms, configurable).
2. Primary query set per tick:
3. `game.objects`
4. `game.visible_enemies`
5. Optional lower-frequency queries (every 2-5 s): `game.status`, `game.resources`.
6. Player metadata queries every 1-2 s: `game.players` (plus fallbacks as needed).
7. After each successful query batch, patch store then trigger one canvas redraw.
8. If a poll request fails, log error and continue next interval (no UI stall).
9. When `StateFrame` subscription is active, reduce or pause polling to avoid duplicate load.

## Concurrency Approach
Tkinter must remain responsive:
1. Network/pipe IO runs in a background thread.
2. Incoming messages are pushed into a thread-safe queue.
3. Tk main loop polls queue on interval (e.g. 30-50 ms).
4. UI updates happen only on Tk thread.

## Implementation Phases
### Phase 0: Bootstrap
1. Create project skeleton and entrypoint.
2. Basic window with placeholder controls + canvas.
3. Logging panel.

Exit criteria:
1. App launches cleanly from CLI.

### Phase 1: Connectivity + Manual Commands
1. Implement named pipe client + Hello.
2. Add raw command send and structured `SessionCommand` helpers.
3. Show `ActionAck`/`Error` in log panel.

Exit criteria:
1. Operator can connect and send `Session.Status` + `Chat.Send`.

### Phase 2: Query-Driven Map (Polling)
1. Add periodic `Game.Query game.objects` and `game.visible_enemies` polling.
2. Render objects on canvas with shape/color by class/team.
3. Add simple selection on canvas click.
4. Add configurable polling controls in UI (`interval_ms`, start/stop polling).
5. Add player metadata table populated from `game.players` + derived aggregates.

Exit criteria:
1. Canvas updates and tracks moving units.

### Phase 3: Streaming State (Preferred)
1. Add `Subscribe stateframe/events/logs` controls.
2. Consume `StateFrame`/`EventBatch` into store.
3. Reduce polling load when streaming active.

Exit criteria:
1. Real-time rendering driven by stream updates.

### Phase 4: Action Workflows
1. Add quick actions for economy/production/combat commands.
2. Provide input validation and user-friendly rejection reasons.
3. Add command presets and last-used values.

Exit criteria:
1. Operator can run a basic economy->army->attack loop from UI.

### Phase 5: Hardening
1. Reconnect behavior and graceful adapter disconnect handling.
2. Bounded queues/log buffers.
3. Smoke tests for protocol encode/decode + state patching.

Exit criteria:
1. Stable 30+ minute session without UI freeze or memory growth.

## Testing Strategy
1. Unit tests:
   - message envelope creation/parsing
   - request_id correlation
   - delta patch logic
   - world->canvas transform
2. Integration smoke:
   - connect/hello
   - send `query --path game.objects` equivalent
   - receive and render at least one update
3. Manual acceptance:
   - click action buttons and verify `ActionAck` behavior
   - visible map updates during active match

## Key Risks and Mitigations
1. Pipe IO blocking UI:
   - isolate IO thread + queue polling model.
2. Message schema drift:
   - strict parsing with tolerant unknown-field handling.
3. Over-rendering/perf issues:
   - cap redraw rate (e.g. 10-20 FPS) and diff renderable objects.
4. Missing map metadata:
   - fallback to inferred extents and display warning.

## MVP Definition
MVP is complete when:
1. App connects and handshakes successfully.
2. User can send `Chat.Send` and at least one `Game.*` command from buttons.
3. Canvas renders friendly and enemy positions from live data.
4. Player panel shows live metadata (name, color/team, cash, counts, promotions when available).
5. Logs show correlated request/ack outcomes.

## Immediate Next Steps
1. Scaffold files for Phase 0.
2. Implement protocol client + Hello + `Session.Status` button.
3. Add query polling + first-pass canvas rendering.
