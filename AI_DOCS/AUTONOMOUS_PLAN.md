# Autonomous Mode Plan

## Purpose

Define a new adapter control mode that minimizes IPC traffic and external orchestration overhead by letting the in-process adapter drive a player with game-native AI behavior and only accepting coarse, infrequent controller directives.

This is a response to the current adapter architecture being too expensive when it is used as a high-frequency remote-control loop with:

1. frequent `Game.Query` polling
2. large object snapshots
3. high command churn
4. expensive state normalization on both sides
5. duplicated decision-making between external scripts and in-game logic

## Recommendation

Yes, this mode should exist.

The current external-brain architecture is still useful for:

1. debugging
2. targeted experimentation
3. deterministic command testing
4. operator-driven workflows

But it is a poor fit for "play a competent RTS match continuously" if that requires constant polling and a large remote decision loop.

For sustained autonomous play, the cheapest architecture is:

1. keep the decision loop in-process
2. reuse the game's native AI systems and scheduling where possible
3. expose only high-level policy inputs through the control API
4. reduce state export to sparse status and event reporting

## Core Idea

Add an adapter mode called `autonomous` where:

1. the adapter owns ongoing game decisions
2. the external controller sends occasional strategic directives only
3. the adapter internally maps those directives to native AI behavior, scripts, or existing legal command paths
4. the default control API becomes declarative rather than imperative

In this mode, the controller should not need to:

1. poll `game.objects` every second
2. issue repeated build/queue commands
3. maintain opening/macro phase state externally
4. manage per-unit actions
5. continuously consume `StateFrame`

## Design Goal

The external controller should be able to say things like:

1. "play normally"
2. "play aggressively"
3. "focus economy first"
4. "turtle until rank 3"
5. "capture tech buildings when safe"
6. "attack player 2 when ready"
7. "expand toward this zone"

Then the adapter should run for long periods without further assistance.

## Proposed Control Model

Support three control modes:

1. `manual`
2. `hybrid`
3. `autonomous`

### 1) `manual`

Current behavior:

1. external process issues explicit commands
2. polling and state queries remain available
3. useful for debugging and fine-grained tooling

### 2) `hybrid`

Mixed ownership:

1. adapter runs autonomous policy by default
2. external commands can temporarily override parts of behavior
3. ownership returns to autonomous policy after a cooldown or explicit release

### 3) `autonomous`

Primary new mode:

1. adapter runs long-lived AI logic in-process
2. external control is limited to high-level directives and status checks
3. detailed polling is disabled or strongly discouraged by default

## What "Act Like Built-In AI" Should Mean

This should not mean "literally expose no controls and hope skirmish AI does everything."

It should mean:

1. reuse native AI update cadence and internal world access
2. reuse existing AI player logic where practical
3. reuse existing legal command paths for actual actions
4. add a thin policy layer for coarse intent and configuration

There are two implementation levels here.

### Level A: AI Policy Wrapper

Recommended first step.

The adapter owns a lightweight autonomous policy layer that:

1. sets goals and modes
2. calls existing adapter helpers
3. optionally invokes existing AI/script helpers already present in the game
4. executes on a low, controlled cadence in-process

This gives most of the performance win without a risky full rewrite.

### Level B: Native AI Delegation

Longer-term option.

The adapter configures and delegates more behavior directly to the game's built-in AI systems:

1. production priorities
2. expansion priorities
3. attack timing
4. defense posture
5. target preferences

This is attractive if the relevant AI hooks are stable and controllable enough, but it should be treated as a later phase because the exact engine integration points may be messy.

## Why This Helps Performance

Moving to autonomous mode should reduce several current costs.

### 1) IPC Volume Drops Sharply

Instead of:

1. polling object state every 500-1000 ms
2. shipping large JSON payloads
3. correlating many request/response pairs

we can reduce control traffic to:

1. mode changes
2. strategic directive updates
3. sparse status snapshots
4. notable events

### 2) Serialization Cost Drops

The adapter already has direct access to engine state. It should not constantly serialize large slices of that state only so an external process can tell it what it already knows internally.

### 3) Decision Latency Drops

The current architecture inserts:

1. query latency
2. JSON encoding/decoding
3. controller scheduling delay
4. duplicate cache/staleness handling

Autonomous mode removes most of that loop.

### 4) Less Thrash

External polling plus external command scripts tends to create:

1. repeated "still trying" commands
2. unstable macro phase changes
3. command spam around transient invalid states
4. more pending/retry bookkeeping

An in-process policy can make decisions against current state with cheaper backoff and cleaner cooldown logic.

## Recommended API Shape

Add a separate command family for autonomous control instead of overloading low-level `Game.*` commands.

## New Commands

### `Autonomy.SetMode`

Example:

```json
{
  "type": "SessionCommand",
  "request_id": "a1",
  "cmd": "Autonomy.SetMode",
  "args": {
    "mode": "autonomous"
  }
}
```

Behavior:

1. enters `manual|hybrid|autonomous`
2. returns current mode and whether the transition succeeded
3. rejects invalid transitions for unsupported contexts

### `Autonomy.Configure`

Use for long-lived policy settings.

Example:

```json
{
  "type": "SessionCommand",
  "request_id": "a2",
  "cmd": "Autonomy.Configure",
  "args": {
    "profile": "gla_default",
    "economy_bias": 0.7,
    "aggression_bias": 0.4,
    "expansion_bias": 0.8,
    "capture_tech": true,
    "target_player_index": 2
  }
}
```

Suggested fields:

1. `profile`
2. `economy_bias`
3. `aggression_bias`
4. `defense_bias`
5. `expansion_bias`
6. `capture_tech`
7. `allow_superweapons`
8. `target_player_index`
9. `preferred_attack_direction`
10. `zone_focus`

### `Autonomy.SetDirective`

Use for one-off high-level intent changes.

Example directives:

1. `play_standard`
2. `rush`
3. `boom`
4. `turtle`
5. `tech_up`
6. `expand_here`
7. `attack_player`
8. `hold_position`
9. `capture_tech`
10. `resume_default`

Example:

```json
{
  "type": "SessionCommand",
  "request_id": "a3",
  "cmd": "Autonomy.SetDirective",
  "args": {
    "directive": "attack_player",
    "target_player_index": 1,
    "duration_ms": 60000
  }
}
```

### `Autonomy.Status`

Lightweight status query only.

This should return compact information such as:

1. current mode
2. active profile
3. current macro state
4. target player
5. last major action time
6. economy summary
7. army summary
8. current bottleneck/reason if stalled

Not a full object dump.

### `Autonomy.Pause`

Temporarily suspend autonomous issuing of commands while preserving configuration.

### `Autonomy.Resume`

Resume after pause.

### `Autonomy.Reset`

Clear learned/transient state and return to profile defaults.

Useful after lobby transitions, rematches, or debugging.

## Status/Event Model

Autonomous mode still needs observability, but not the current heavy telemetry model.

## Replace High-Frequency Polling With Sparse Reporting

Recommended default stream:

1. `AutonomyEvent`
2. `AutonomyStatus`

### `AutonomyEvent`

Emit only meaningful changes:

1. mode entered
2. profile changed
3. attack launched
4. expansion started
5. tech capture ordered
6. superweapon fired
7. stalled due to cash/prereq/no worker
8. target player changed

Example:

```json
{
  "type": "AutonomyEvent",
  "kind": "attack_launched",
  "frame": 123456,
  "data": {
    "target_player_index": 2,
    "group_size": 34
  }
}
```

### `AutonomyStatus`

Low-rate summary, for example every 2-5 seconds, or on demand.

Example fields:

1. `mode`
2. `profile`
3. `macro_phase`
4. `money`
5. `income_estimate`
6. `base_count`
7. `production_count`
8. `army_value_estimate`
9. `current_goal`
10. `last_failure_reason`

## What To Stop Doing In Autonomous Mode

To get the performance win, we need hard boundaries.

When `mode=autonomous`:

1. disable default UI poll loops
2. do not stream `StateFrame` unless explicitly requested for debugging
3. do not rely on `game.objects`, `game.visible_enemies`, or `game.grid` as the normal control loop
4. do not drive economy/production/combat from external Python scripts
5. avoid per-unit external commands except as temporary overrides in `hybrid`

If we keep all the old polling and just add one more autonomous loop, we will not actually fix the architecture.

## Recommended Adapter Internals

## 1) Add an `AutonomyManager`

Create a single in-process owner for autonomous behavior.

Responsibilities:

1. current control mode
2. profile/config storage
3. active directives
4. per-player autonomous state
5. update cadence
6. cooldowns and action budget
7. event emission

Suggested internal shape:

1. `AutonomyManager`
2. `AutonomyProfile`
3. `AutonomyDirective`
4. `AutonomyRuntimeState`
5. `AutonomyStatusSnapshot`

## 2) Run On Engine Tick, But Not Every Tick For Everything

Do not replace external polling with an expensive in-process full scan every frame.

Use staged cadences:

1. every tick: cheap bookkeeping only
2. every 250-500 ms: macro decision checks
3. every 500-1000 ms: strategic retarget/rebalance
4. event-driven updates where possible for destruction/completion/transitions

This should be much cheaper than repeated JSON query/export cycles.

## 3) Separate Macro Decisions From Tactical Decisions

Macro layer:

1. economy
2. build order
3. expansion
4. upgrades
5. attack readiness

Tactical layer:

1. attack wave launch
2. defend local threats
3. rally behavior
4. target choice

This is important because "act like built-in AI" should not mean one monolithic update function with no observability.

## 4) Reuse Existing Helpers Instead Of Porting External Scripts Directly

The current branch has a lot of logic in external Python scripts for:

1. opening phases
2. worker rules
3. radar van rules
4. capture rules
5. macro transitions

Do not re-create this exact architecture in-process line-for-line.

Instead:

1. extract the stable policy concepts
2. represent them as small internal rules
3. remove dependence on external query cadence
4. keep only the minimum state needed in-process

## Proposed Profiles

Start with a small number of explicit profiles.

### `standard`

Balanced economy/army behavior.

### `aggressive`

Earlier military production, lower eco reserve, earlier attacks.

### `economic`

Higher worker and expansion priority, delayed large attacks.

### `defensive`

More base defenses, larger attack threshold, stronger local threat response.

### `tech`

Prioritize palace/black market/upgrades/science and superweapon path when allowed.

### `sprawl`

Prioritize map-wide expansion with repeated production, markets, and defensive structures.
Also a good candidate for autonomous promotion and upgrade spending once palace/black market tech is online.

### `builtin_passthrough`

Minimal adapter steering; delegate as much as possible to native AI defaults.

This last one is useful as a baseline and as a risk-reduction step.

## Minimal Data Needed In Autonomous Mode

The in-process autonomy layer should consume engine state directly and internally, but the controller should only see compact summaries.

Recommended exported fields:

1. `money`
2. `power_state`
3. `worker_count`
4. `supply_count`
5. `production_count`
6. `army_count`
7. `attack_ready`
8. `macro_phase`
9. `target_player_index`
10. `last_failure_reason`

Avoid exporting:

1. full object lists by default
2. full enemy lists by default
3. full grid occupancy by default
4. high-rate positional deltas by default

## Hybrid Override Rules

Hybrid mode is important because full autonomy without override is hard to debug.

Recommended rules:

1. external command can acquire temporary ownership over a unit/group/system
2. autonomous control backs off for a configured window
3. ownership expires automatically
4. adapter reports when ownership is returned

Examples:

1. manual camera control should never affect autonomy
2. manual attack command can override combat group control for 20-30 seconds
3. manual building placement can temporarily suppress autonomous building in that zone

## Safety and Scope Boundaries

Autonomous mode should still obey the same multiplayer-safe principles:

1. no deep direct simulation mutation
2. issue legal player-equivalent commands
3. preserve command throttling
4. avoid command spam
5. keep deterministic behavior where networked behavior matters

The performance problem is not an excuse to take unsafe shortcuts.

## Migration Strategy

## Phase 1: Add Mode Skeleton

Implement:

1. `Autonomy.SetMode`
2. `Autonomy.Status`
3. `Autonomy.Configure`
4. in-process `AutonomyManager`
5. compact status struct

Behavior:

1. `autonomous` mode exists but initially only runs a very small macro loop
2. external polling can be turned off in the UI when this mode is active

Exit criteria:

1. controller can switch modes
2. adapter can report compact autonomous status
3. no `Game.Query` spam is required to keep the bot active

## Phase 2: Port Current External Macro Loop Concepts Inward

Bring in only the highest-value behaviors:

1. worker sustain
2. supply expansion
3. production expansion
4. simple attack readiness and launch
5. optional tech capture

Exit criteria:

1. bot can play a complete macro loop without external scripts
2. command count is lower and more stable than current polling-driven control

## Phase 3: Add Profiles and Directives

Implement:

1. `standard`
2. `aggressive`
3. `economic`
4. `defensive`
5. `builtin_passthrough`

Add one-off directives like:

1. `attack_player`
2. `expand_here`
3. `hold_position`

Exit criteria:

1. operator can steer behavior without micromanaging it
2. the adapter remains mostly self-running between directives

## Phase 4: Reduce Old UI/Script Dependence

Update `bot-ui` behavior when autonomy is active:

1. stop background query rotation by default
2. show `AutonomyStatus` instead of detailed object state
3. expose profile/directive controls instead of micro buttons as the primary surface
4. keep low-level buttons in a debug/advanced section only

Exit criteria:

1. normal autonomous operation does not depend on object/grid polling
2. UI remains useful as a supervisory console

## Phase 5: Optional Native AI Deeper Integration

Investigate whether parts of the built-in skirmish AI can be:

1. configured
2. redirected
3. selectively delegated to
4. used as baseline policy modules

This phase should happen only after Phase 1-4 prove the high-level mode is useful.

## Bot UI Changes

If this mode is adopted, `bot-ui` should gain a distinct autonomous workflow.

Recommended UI elements:

1. control mode selector: `manual|hybrid|autonomous`
2. profile dropdown
3. directive buttons: `Standard`, `Rush`, `Boom`, `Turtle`, `Attack Player`, `Expand`, `Resume Default`
4. compact status panel
5. autonomy event log
6. debug toggle to re-enable detailed polling only when needed

The current `bot-ui` layout is optimized for low-level action buttons and query-driven map inspection. That is useful for debug mode, but not ideal as the primary operator surface for autonomous play.

## Open Questions

These need investigation before implementation is finalized.

1. Which parts of the game's built-in AI are configurable enough to reuse safely?
2. Can we instantiate or redirect native AI behavior for a human-controlled slot without fighting existing ownership assumptions?
3. Which existing adapter helper functions are cheap enough to reuse in an in-process loop?
4. What is the minimum useful status payload for operators?
5. Do we need one autonomy profile per faction, or a shared model with faction-specific rule tables?
6. Should autonomous mode disable some low-level commands entirely, or just warn against them?

## Strong Opinion

The right move is not to abandon the current adapter protocol. The right move is to split it cleanly into:

1. a low-level debug/control protocol
2. a high-level autonomous protocol

The low-level protocol remains valuable for:

1. development
2. instrumentation
3. reproducing bugs
4. specialized experiments

But the default "play the game well for a long time" path should shift toward autonomous in-process control with sparse supervisory telemetry.

## Concrete First Step

Implement this smallest vertical slice first:

1. add `Autonomy.SetMode`
2. add `Autonomy.Configure`
3. add `Autonomy.Status`
4. create an in-process autonomous macro loop that only manages:
   - workers
   - stash expansion
   - barracks/arms dealer growth
   - basic attack timing
5. update `bot-ui` to stop polling when autonomy is active and show compact status instead

If that slice performs well, then continue moving the external Python macro logic into the adapter and reduce the external control surface to supervision and coarse steering.
