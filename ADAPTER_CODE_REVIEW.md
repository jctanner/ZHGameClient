# AIControlAdapter Code Review

Reviewed: 2026-04-12

## Scope

All adapter source files in `GeneralsMD/Code/GameEngine/Source/GameClient/` and
`GeneralsMD/Code/GameEngine/Include/GameClient/`, plus the test file in
`GeneralsMD/Code/GameEngine/Tests/`.

| File | Lines | Role |
|---|---|---|
| `AIControlAdapter.cpp` | 4568 | Core state class, autonomy orchestration, automation rules |
| `AIControlAdapterPolicy.cpp` | 776 | Pure decision functions (no engine deps) |
| `AIControlAdapterPolicy.h` | 216 | Policy structs and function declarations |
| `AIControlAdapter.h` | 4 | Public API (`Update`, `Reset`) |
| `AIControlAdapterProtocol.inl` | 988 | JSONL protocol dispatch (handleMessage, handleSessionCommand) |
| `AIControlAdapterTransport.inl` | 260 | Named pipe I/O, adapter log file management |
| `AIControlAdapterGameActions.inl` | 4331 | Unit production, building placement, worker management |
| `AIControlAdapterGameQuery.inl` | 1650 | 21+ query paths (resources, units, objects, grid, enemies) |
| `AIControlAdapterGameActionsSmartBuild.inl` | 1163 | Multi-phase smart building placement algorithms |
| `AIControlAdapterGameActionsCombat.inl` | 648 | ScudStorm, attack-move, capture, raid, guard |
| `AIControlAdapterGameActionsCamera.inl` | 235 | Camera set/reset/look-at/get |
| `AIControlAdapterGameActionsOrders.inl` | 139 | All-combat attack-move to player |
| `AIControlAdapterWorkers.inl` | 312 | Worker reservation and availability |
| `AIControlAdapterUI.inl` | 351 | Menu click, text entry, controls inventory |
| `AIControlAdapterPolicyTests.cpp` | 868 | Unit tests for all policy functions |
| **Total** | **~16,500** | |

---

## Architecture

The adapter injects itself into the game's main update loop via two free
functions declared in `AIControlAdapter.h`:

```cpp
void AIControlAdapterUpdate();  // called every frame
void AIControlAdapterReset();   // called on game exit
```

Internally, a single file-scope instance of `AIControlAdapterState` (defined
entirely inside `AIControlAdapter.cpp`) owns all state.  The `.inl` files are
`#include`-d directly inside the class body, making them syntactically part of
the same class.  The result is a single monolithic class spanning ~16K lines.

Communication with an external controller (the "bot-ui") happens over a
Windows named pipe (`\\.\pipe\zh_ai_control`) using a line-delimited JSON
protocol (`zh-ai-control-v1`).  The adapter processes incoming commands once
per frame on the game thread -- there is no multithreading.

Key subsystems:

- **Protocol layer** (`Protocol.inl`, `Transport.inl`): pipe lifecycle,
  message framing, JSONL parse/dispatch, request/response envelope.
- **Game actions** (`GameActions.inl`, `SmartBuild.inl`, `Combat.inl`,
  `Camera.inl`, `Orders.inl`): imperative commands that mutate game state
  (queue units, place buildings, issue orders).
- **Game queries** (`GameQuery.inl`): read-only inspection of game state
  (resources, unit counts, object lists, grid cells, visible enemies).
- **Automation rules** (`AIControlAdapter.cpp`): configurable per-tick rules
  that automatically issue commands when conditions are met (worker production,
  stash worker scaling, attack waves, capture, radar van).
- **Autonomy** (`AIControlAdapter.cpp`): higher-level autonomous decision
  making with profiles (`sprawl_balanced`, `aggressive`, `tech`, etc.),
  macro evaluation, production mix targeting, zone expansion.
- **Policy functions** (`AIControlAdapterPolicy.cpp`): pure functions
  extracted from the orchestration layer, each taking a flat input struct and
  returning a decision.  These are the only functions with unit tests.

---

## Strengths

### 1. Policy extraction pattern

The `AIControlAdapterPolicy.cpp` / `.h` pair is well-designed.  Each policy
function:

- Takes a plain struct with named fields (no engine types).
- Returns a simple value (`bool`, `const char*`, or a small result struct).
- Has no side effects and no game engine dependencies (the only system header
  is `<windows.h>` for the `LONG` type used in tick comparisons).

This makes the functions straightforward to test and reason about in isolation.
Examples: `ShouldPauseCombatProduction`, `ShouldHoldArmyCap`,
`GetRequiredOpeningBuild`, `ChoosePreferredProductionCommand`,
`ResolveZoneFrontDirection`.

### 2. Test coverage for policy functions

`AIControlAdapterPolicyTests.cpp` (868 lines) covers every exported policy
function with boundary-condition and hysteresis tests.  Notable coverage
includes:

- Reserve cash recovery hysteresis (entering vs. exiting recovery).
- Army cap with 10-unit dead-band.
- Tick-wrap safety across the `LONG` signed boundary
  (`0x7ffffff0` / `0x80000010`).
- Direction normalization rejection of zero and near-zero vectors.
- Map position parsing with missing/wrong-type fields.
- Recent attack target freshness filtering with reverse iteration.
- Zone front direction priority chain (attack target > preferred enemy base >
  nearest enemy base > sprawl axis fallback).
- Production choice composition targeting (55/45 vehicle/infantry split,
  vehicle replenishment priority, near-cap buffer).
- Macro completion evaluation against structure caps.
- Market growth vs. production building prioritization.

The test harness is custom (`expect()` / `expectNear()` with `std::exit(1)`
on failure) rather than using a framework, but it is adequate for the current
scope.

### 3. Defensive coding

The codebase consistently checks for null pointers, missing JSON keys, dead
objects, and invalid states before acting.  Examples:

- `getPlayerByIndex()` null-checks `ThePlayerList` and validates the index.
- `executeGameQueueUnit()` validates the template name against the
  `ThingFactory` before attempting to queue.
- `collectCombatUnitsForRaid()` filters out dead objects, non-combat kinds,
  radar vans, and objects under construction.
- Worker and capture automation rules gracefully back off when no valid
  source/target is found.
- Named pipe reads/writes check return values and reset the connection on
  failure.

### 4. Structured logging

The adapter log (`adapterLog()`) produces timestamped, key=value structured
entries that are machine-parseable.  Every automation rule evaluation, autonomy
tick, build attempt, and production decision is logged with full context
(player index, counts, reasons, success/failure).  Log truncation at 1200
characters prevents runaway payloads.

### 5. Tick-wrap-safe time comparisons

`HasTickElapsed()` and `IsTickInFuture()` use `static_cast<LONG>(now -
deadline)` which correctly handles `GetTickCount()` 32-bit wrap-around.  This
pattern is used consistently throughout the codebase for cooldown deadlines,
reservation expiry, and autonomy tick scheduling.

---

## Concerns

### C1. Monolithic single-class design

All `.inl` files are `#include`-d inside the `AIControlAdapterState` class
body.  This means the entire adapter is a single ~16K-line class with all
state, protocol handling, game actions, queries, automation, and autonomy in
one scope.  This makes it difficult to:

- Navigate and understand the code.
- Test subsystems in isolation.
- Reason about state dependencies between subsystems.
- Compile changes incrementally (any `.inl` change recompiles the entire
  4568-line `.cpp`).

The `.inl` approach was likely chosen to avoid exposing the class definition in
a header, but the same encapsulation could be achieved with a `pimpl` pattern
or internal linkage helper classes.

### C2. String-based command dispatch

`handleSessionCommand()` in `Protocol.inl` is a ~700-line chain of
`if (cmd == "Game.QueueUnit")` / `if (cmd == "Game.BuildBarracksSmart")` /
etc.  Each branch follows the same pattern (validate, execute, send ack), but
the dispatch is entirely string-based with no compile-time verification.

Risks:
- Typos in command strings are silent failures (fall through to
  `unsupported_session_command`).
- Adding a new command requires touching the dispatch chain, the capabilities
  list in `Hello`, and the execute function -- three separate locations with
  no compile-time link.
- The same command string appears in the dispatch, in autonomy/automation
  code that synthesizes internal messages, and in the policy layer as return
  values -- all as raw `const char*` literals.

### C3. evaluateAutonomyMacro() complexity

This is the central decision function for autonomous macro play.  It is a
~450-line `if/else-if` chain that interleaves:

- Object counting via `iterateObjects()` with `containsIgnoreCase()` name
  matching.
- Policy function calls.
- Direct game action dispatch.
- Telemetry event recording.
- Cooldown management.

The function is too large to hold in working memory and has no tests (the
policy functions it calls are tested, but the orchestration logic that wires
them together is not).  The object counting loop inside this function also
duplicates counting logic that appears separately in `buildAutonomyStatus()`,
`evaluateRadarVanAutomationRule()`, and the query layer.

### C4. Duplicated object counting

Building and unit counts are computed by iterating all owned objects with
`containsIgnoreCase()` name matching.  This same pattern appears in at least
four places:

1. `evaluateAutonomyMacro()` -- counts supply stashes, barracks, arms
   dealers, palaces, black markets, infantry types, vehicle types.
2. `evaluateRadarVanAutomationRule()` -- counts supply stashes, barracks,
   black markets, arms dealers, radar vans, combat vehicles.
3. `buildAutonomyStatus()` -- counts units, buildings, workers, supply
   stashes, barracks, arms dealers, palaces, black markets, scud storms,
   radar vans.
4. `executeGameQuery()` for `game.units` -- counts various unit categories.

Each instance re-iterates all player objects and re-applies string matching.
The counts are not cached or shared between call sites within the same frame.

### C5. Magic numbers throughout

Numeric constants are used directly in logic without named definitions.
Examples:

- `10000u` -- reserve cash threshold (appears in `evaluateAutonomyMacro()`).
- `100` -- army cap for balanced sprawl.
- `430.0f` -- supply "claimed" detection radius in smart build.
- `120.0f`, `150.0f`, `220.0f` -- same-type spacing for barracks, arms
  dealers, palaces.
- `48`, `72` -- angular division points per ring in concentric build search.
- `5000`, `45000` -- worker reservation durations (ms).
- `15000` -- build location reservation duration (ms).
- `3.0f`, `4.0f` -- base zone count for sprawl profiles before multiplier.
- `2500u`, `3000u`, `3500u`, `4500u` -- various build retry delays.
- `55/45` -- vehicle/infantry production ratio (expressed as `(targetCombatCount * 11) / 20`).

Most of these should be named constants or part of the autonomy profile
configuration.

### C6. No integration or orchestration tests

Only the pure policy functions in `AIControlAdapterPolicy.cpp` have tests.
The following critical subsystems have no test coverage:

- Protocol dispatch (`handleSessionCommand()`).
- Automation rule evaluation (worker, stash worker, attack, capture,
  radar van).
- Autonomy macro evaluation (`evaluateAutonomyMacro()`).
- Smart build placement algorithms.
- Game query result construction.
- Object counting and filtering.

This means the orchestration logic that ties policy decisions to game actions
is validated only through gameplay observation and log inspection, which is
noted in `BUGS.md` as `B017` and `B018`.

### C7. Production retry spam against unavailable producers

When a production command fails with `queue_full` or
`producer_under_construction`, the retry delay from
`GetProductionRetryDelayMs()` is 3.5s and 5.0s respectively.  These delays
are applied per-tick, but the autonomy macro can attempt the same production
command category on the next evaluation cycle if the cooldown has elapsed.

In practice this produces repeated failed attempts visible in logs (tracked as
`B027`), especially for war-factory production where build times are long.
The retry delay is context-free -- it does not consider how many ticks remain
until the producer finishes construction.

### C8. Conservative expansion ceiling

The desired zone count for `sprawl_balanced` is computed as:

```cpp
static_cast<Int>(std::floor(3.0f * sprawlMultiplier))
```

With the default multiplier of 1.0, this caps expansion at 3 zones.  The bot
reaches this cap and stops expanding even when viable supply sources and safe
expansion lanes remain (tracked as `B025`, `B028`).  The multiplier is
configurable via `Autonomy.Configure`, but the default is too conservative for
most maps.

### C9. Coarse reserve hysteresis

Reserve cash recovery uses a fixed $3000 hysteresis buffer above the $10000
reserve floor.  Once cash drops below $10000, production pauses until cash
reaches $13000.  This creates long idle windows where the bot sits in
`reserve_cash_recovery` (tracked as `B010`, `B020`, `B023`).

The hysteresis is not scaled to income rate, map stage, or current spending
pressure.  A bot with high income from multiple black markets should recover
through the buffer faster than a bot with a single supply stash, but the
threshold is identical for both.

### C10. Thread safety assumptions

The adapter runs entirely on the game's main thread and does not use locks.
This is safe as long as:

- `AIControlAdapterUpdate()` is only called from the main loop.
- No other thread accesses the adapter state or the named pipe.

These assumptions are reasonable for the current design but are not documented
or enforced.  If the game engine ever calls `AIControlAdapterUpdate()` from a
render thread or network callback, the shared mutable state would produce
data races.

---

## Relationship to Known Bugs

| Bug | Root Cause Area | Relevant Code |
|---|---|---|
| B001, B002 | Opening build recovery too passive | `evaluateAutonomyMacro()` opening phase, `GetRequiredOpeningBuild()` |
| B003 | Combat production starts too early | `ShouldPauseCombatProduction()`, opening gates |
| B004 | Macro falls into `action=none` | `evaluateAutonomyMacro()` else-chain gaps |
| B005, B006 | Supply stash placement quality | `SmartBuild.inl` supply-relative placement |
| B007 | Under-prioritized supply expansion | `GetEcoRecoveryBuild()`, zone expansion logic |
| B008 | Non-core structure retry aggression | `GetBuildRetryDelayMs()` for tunnels/stingers |
| B009 | Same-type spacing validation | `SmartBuild.inl` spacing constants (120/150/220) |
| B010, B020, B023 | Reserve recovery idle windows | `ShouldPauseCombatProduction()` hysteresis |
| B011 | Unit production mix drift | `ChoosePreferredProductionCommand()` |
| B012 | Army cap soft enforcement | `ShouldHoldArmyCap()`, 10-unit dead-band |
| B014, B016 | Tech/upgrade retry noise | `ShouldAbortUpgradePlanForTick()`, `GetTechRetryDelayMs()` |
| B015 | Black market before palace | `CanAttemptBlackMarket()` |
| B017 | No unit test harness for orchestration | Test coverage limited to `PolicyTests.cpp` |
| B021 | Autonomy startup silent no-op | Timer/sentinel handling around `GetTickCount()` |
| B025, B028 | Expansion stalls too early | `desired_zone_count` formula (`3.0f * multiplier`) |
| B026 | Late-game barracks-first spam | `ChoosePreferredProductionCommand()` fallback paths |
| B027 | Production retry spam | `GetProductionRetryDelayMs()`, context-free backoff |
| B029 | Telemetry stops updating | `buildAutonomyTelemetry()` payload delivery |

---

## Recommendations

### R1. Extract more policy functions from orchestration

The pattern established in `AIControlAdapterPolicy.cpp` works well.
Additional candidates for extraction:

- **Macro action selection**: the core `evaluateAutonomyMacro()` decision
  (which building/action to choose next) can be expressed as a pure function
  taking a snapshot of counts, profile, and configuration.
- **Eco recovery build selection with counts-in-progress**: currently
  `GetEcoRecoveryBuild()` receives `blackMarketsInProgress` and
  `supplyStashesInProgress` as zero from the macro evaluator -- passing real
  values would improve decision quality.
- **Desired zone count calculation**: currently inline in
  `buildAutonomyTelemetry()`, this should be a named policy function to
  enable testing and tuning.

Each extraction adds directly testable surface area and reduces the size of
the untested orchestration code.

### R2. Centralize object counting

Replace the duplicated `iterateObjects()` + `containsIgnoreCase()` counting
loops with a single per-frame cache.  Something like:

```
struct OwnedObjectSnapshot {
    int supplyStashes, barracks, armsDealers, palaces, blackMarkets;
    int soldiers, rpg, quads, scorpions, scudLaunchers, radarVans;
    int workers, idleWorkers;
    int totalUnits, totalBuildings;
    // ...
};
```

Compute once per frame (or on-demand with dirty flag), share across
`evaluateAutonomyMacro()`, `evaluateRadarVanAutomationRule()`,
`buildAutonomyStatus()`, and query handlers.  This eliminates redundant
iteration and ensures consistency within a single frame.

### R3. Replace command string dispatch with a table or enum

Convert the `handleSessionCommand()` if-chain into a dispatch table mapping
command strings to handler function pointers or method pointers.  This:

- Eliminates the risk of string typos.
- Makes it easy to auto-generate the capabilities list from the same table.
- Reduces the mechanical boilerplate (each handler currently has identical
  validate/execute/ack wrapping).

### R4. Parameterize magic numbers into autonomy profile configuration

Move hard-coded constants into the autonomy profile or a named-constant block:

- Reserve cash threshold.
- Army cap.
- Vehicle/infantry ratio target.
- Zone count base and multiplier.
- Same-type building spacing distances.
- Retry delay tables.
- Supply claimed radius.

This would make it possible to tune behavior per profile (e.g., `aggressive`
could have lower reserve cash and higher army cap) without code changes.

### R5. Add orchestration-level tests

The biggest gap in test coverage is the orchestration layer.  Even without
mocking the full game engine, some approaches:

- **Snapshot-based testing**: serialize an `OwnedObjectSnapshot` (see R2) and
  autonomy configuration as test inputs, call the macro evaluation logic, and
  assert on the resulting action/reason.  This requires extracting the macro
  logic into a testable function (see R1).
- **Protocol round-trip tests**: send JSONL commands through `handleMessage()`
  with a mock pipe and verify the JSON responses.  This would require
  abstracting the pipe I/O behind an interface.
- **Replay-based regression tests**: record adapter log sequences from live
  games and replay them through a test harness to detect behavioral
  regressions.
