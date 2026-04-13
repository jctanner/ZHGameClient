# Zone Expansion Defense

## Goal

Make autonomous sprawl zones directional instead of purely radial, so the adapter can:

- place static defenses toward the likely forward edge of an expansion
- keep economy/core structures closer to the interior of the zone
- bias production posture toward the same expansion-facing direction
- expose the geometry through telemetry so the bot UI can visualize it cheaply

This is intended to improve both:

- gameplay behavior
- observability of why the adapter is building/defending where it is


## Current Behavior

Today the adapter mainly treats zones as circles centered on:

- the main command center
- completed supply stashes / supply centers

The zone model is useful for local counts and build throttling, but it lacks a notion of:

- front edge
- rear edge
- expansion-facing orientation
- enemy-facing orientation

As a result:

- defenses can cluster too centrally
- barracks and arms dealers can be placed without a coherent defensive line
- the UI can show zone circles, but not their intended posture


## Design Direction

### 1. Introduce a sprawl axis

Use the expansion pattern itself to derive a stable fallback direction:

1. identify the main / starting zone
2. identify the furthest non-main zone from the main zone
3. compute a normalized axis from main zone center to furthest zone center

This produces a stable "sprawl direction" even before enemy threat information is reliable.

This axis is not a perfect threat model. It is a low-cost, high-coherence fallback that matches the adapter's own expansion pattern.


### 2. Derive a per-zone front point

For each zone:

- `front_point = zone.center + (zone_axis * zone.radius)`
- `rear_point = zone.center - (zone_axis * zone.radius)`

Where `zone_axis` comes from the best available source in the following fallback order:

1. recent local threat direction
2. current attack / raid target direction
3. nearest known enemy base / player position
4. sprawl axis from main zone to furthest expansion

The initial implementation can start with step 4 only.


### 3. Use the front point to shape base layout

#### Static defenses

Tunnels and stingers should bias toward:

- `front_point`
- or a small set of nearby forward-edge candidate points

This creates an actual forward defensive arc instead of central clustering.

#### Production buildings

Barracks and arms dealers should not usually sit at the extreme front edge.

They should bias toward:

- mid-to-front zone placement
- behind the defensive line
- still oriented toward the same front direction

This creates:

- defense line first
- production line behind it

#### Economy / core structures

Supply stashes, markets, and similar economy structures should remain:

- center-to-mid zone
- not front-edge biased


### 4. Use the same geometry for rally posture

Each zone should expose:

- `front_point`
- optional `rear_point`

Production and reinforcement logic can use these for:

- unit rally bias
- local guard concentration
- future line-holding / edge-staging behaviors

This should make unit movement more coherent without requiring heavy path analysis.


## Telemetry Plan

Telemetry should stay compact and cached.

Do not recompute geometry on telemetry requests.

The adapter should compute and cache the fields during the existing macro / production evaluation cadence.

### Per-zone telemetry fields

Each zone payload should eventually include:

- `anchor_id`
- `center_x`
- `center_y`
- `radius`
- `is_main_base`
- `active`
- `supply_stashes`
- `barracks`
- `arms_dealers`
- `palaces`
- `black_markets`
- `tunnels`
- `stingers`
- `developed`
- `needs_followup`
- `front_point_x`
- `front_point_y`
- `rear_point_x`
- `rear_point_y`
- `front_source`

Where `front_source` is one of:

- `recent_threat`
- `attack_target`
- `enemy_base`
- `sprawl_axis`

### Global telemetry fields

Global telemetry should also include:

- `main_zone_anchor_id`
- `sprawl_axis_dx`
- `sprawl_axis_dy`
- `furthest_zone_anchor_id`

This lets the UI explain why front points are oriented the way they are.


## Bot UI Mapping Plan

The bot UI should render this as lightweight overlay geometry only.

Avoid dependence on heavy map polling.

### Visual elements

1. zone circle
2. zone center marker
3. front point marker
4. optional line from zone center to front point
5. optional rear point marker

### Labeling

Per-zone map labels should show:

- zone number
- active / main / expansion
- state: `early`, `followup`, `developed`
- compact counts: `S`, `B`, `A`, `T`, `G`
- eventually `front_source`

Example:

`Z3 ACTIVE EXPANSION`

`followup  S1 B1 A0 T1 G0`

`front: sprawl_axis`


## Rollout Plan

### Phase 1

Telemetry only:

- compute global sprawl axis from main zone to furthest zone
- compute `front_point` and `rear_point` for each zone from that axis
- expose fields through `Autonomy.Telemetry`
- draw them in the bot UI

This phase is low risk and lets us validate the geometry visually before behavior changes.

### Phase 2

Behavior bias:

- bias tunnel and stinger placement toward `front_point`
- keep barracks / arms dealer behind that edge
- leave economy/core buildings interior-biased

### Phase 3

Adaptive direction overrides:

- allow threat or raid-target direction to override sprawl-axis direction
- surface `front_source` in telemetry so the UI stays interpretable


## Unit Test Plan

The new Phase 3 behavior is only partially testable in its current inline form inside the adapter macro evaluation path.

To make it testable without mocking the whole game, the decision logic should be extracted into small pure helpers and covered directly.

### Priority order

1. extract and test direction normalization
2. extract and test map-position extraction from JSON
3. extract and test recent-attack target selection
4. extract and test zone front-direction fallback resolution

This keeps the first test pass cheap and focused while covering the highest-risk logic added in Phase 3.

### 1. Direction normalization helper

Extract a helper equivalent to:

- `tryNormalizeDirection(dx, dy, outDx, outDy)`

Expected test coverage:

- valid vectors normalize correctly
- zero-length vectors are rejected
- near-zero vectors are rejected
- successful outputs are approximately unit length

### 2. Map-position extraction helper

Extract a helper equivalent to:

- `tryReadMapPosition(json, outPos)`

Expected test coverage:

- accepts a valid object with numeric `x` and `y`
- rejects non-object payloads
- rejects missing `x`
- rejects missing `y`
- rejects wrong types for `x` or `y`

### 3. Recent attack target helper

Extract a helper equivalent to:

- `tryGetRecentAttackTarget(events, nowTick, freshnessMs, outPos)`

Expected test coverage:

- accepts a fresh `attack` event with `x` and `y`
- ignores stale attack events
- ignores non-attack events
- ignores malformed attack events without usable coordinates
- prefers the newest valid attack event when multiple exist

### 4. Zone front-direction resolver

Extract a helper equivalent to:

- `resolveZoneFrontDirection(...)`

Inputs should include:

- zone center
- optional recent attack target
- optional preferred enemy base
- optional list of known enemy bases
- fallback sprawl-axis direction

Outputs should include:

- normalized `dx`
- normalized `dy`
- `front_source`

Expected test coverage:

- uses `attack_target` when a valid recent attack target exists
- falls back to preferred enemy base when no attack target exists
- falls back to nearest enemy base when only a list of enemy bases exists
- falls back to `sprawl_axis` when no higher-priority source is usable
- ignores degenerate vectors where the candidate source is effectively on top of the zone center

### 5. Front/rear point geometry helper

If the geometry calculation is split out, add a helper equivalent to:

- `buildZoneFrontRearPoints(center, radius, dirDx, dirDy)`

Expected test coverage:

- `front_point = center + dir * radius`
- `rear_point = center - dir * radius`
- non-cardinal directions are handled correctly

### Why this structure

The goal is to test the Phase 3 logic without introducing heavy world-state fixtures.

The adapter’s game-facing orchestration can remain integration-tested through live runs, while the mathematically deterministic parts are validated with fast unit tests.


## Performance Constraints

This feature must remain cheap.

### Acceptable

- vector math
- comparing zone distances
- choosing furthest zone
- caching a small telemetry payload
- UI drawing a handful of circles / lines / labels

### Not acceptable

- rescanning the world only to answer telemetry
- pathfinding to determine front direction
- full threat heatmaps on UI cadence
- large unit/building payloads for visualization

The intended model is:

- compute once during autonomy logic
- cache small results
- poll slowly from bot UI


## Notes

- Sprawl-axis direction is a structural fallback, not a true threat model.
- It is still useful because it aligns the defensive posture with the adapter's own expansion shape.
- If threat-aware direction is added later, it should override the sprawl axis only when the evidence is strong enough to justify the change.
