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
16. `Game.QueueUpgrade`
17. `Game.PurchaseScience`
18. `Game.BuildWorker`
19. `Game.SetMoney`
20. `Game.DebugDeshroud`
21. `Game.QueueSoldiersAllBarracks`
22. `Game.QueueRpgTroopersAllBarracks`
23. `Game.QueueQuadsAllWarFactories`
24. `Game.QueueScorpionsAllWarFactories`
25. `Game.QueueRadarVansAllWarFactories`
26. `Game.QueueRadarVan`
27. `Game.FindSupplySources`
28. `Game.FindBuildLocationNearSupply`
29. `Game.DozerConstruct`
30. `Game.BuildSupplyStashAuto`
31. `Game.BuildSupplyStashSmart`
32. `Game.BuildBarracksSmart`
33. `Game.BuildCommandCenterSmart`
34. `Game.BuildArmsDealerSmart`
35. `Game.BuildPalaceSmart`
36. `Game.BuildBlackMarketSmart`
37. `Game.BuildScudStormSmart`
38. `Game.BuildBuildingMix`
39. `Game.ScudStormAtPosition`
40. `Game.ScudStormAtPlayer`
41. `Game.AttackMove`
42. `Game.CaptureBuilding`
43. `Game.AttackMove.RaidSmart`
44. `Game.Camera.Set`
45. `Game.Camera.LookAt`
46. `Game.Camera.SetZoomLimited`
47. `Game.Camera.Get`
48. `Game.Display.Get`
48. `Autonomy.SetMode`
49. `Autonomy.Configure`
50. `Autonomy.Status`
51. `Autonomy.Pause`
52. `Autonomy.Resume`
53. `Autonomy.Reset`

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

`Game.BuildScudStormSmart` args:

1. `player_index` (optional int, default local player)
2. `worker_object_id` (optional int, default first idle dozer/worker)
3. `building_template` (optional override; otherwise inferred by faction/buildability)
4. `anchor_object_id` (optional int; owned object used as placement anchor)
5. `zone_center` (optional object `{x,y}`; preferred zone center for placement)
6. `zone_radius` (optional number; search radius around `zone_center`, default adapter-defined)
7. `strict_zone` (optional bool; if `true`, do not fallback to anchor-based placement when zone has no legal spot)

`Game.BuildScudStormSmart` behavior:

1. Resolve an idle worker/dozer.
2. Infer a buildable Scud Storm template for the player's GLA variant if not explicitly provided.
3. Place near anchor object (default: player command center).
4. If no legal location currently exists, issue a move order toward the preferred area and return success.

`Game.ScudStormAtPosition` args:

1. `player_index` (optional int, default local player)
2. `source_object_id` (optional int; explicit Scud Storm structure to fire from)
3. `x` (required number)
4. `y` (required number)

`Game.ScudStormAtPosition` behavior:

1. Resolve a ready Scud Storm owned by the requested player.
2. Validate the target through the engine's normal special-power checks.
3. Issue the native `MSG_DO_SPECIAL_POWER_AT_LOCATION` command using that structure as the specific source.

`Game.ScudStormAtPlayer` args:

1. `player_index` (optional int, default local player)
2. `source_object_id` (optional int; explicit Scud Storm structure to fire from)
3. `target_player_index` (required int)

`Game.ScudStormAtPlayer` behavior:

1. Resolve the target player's primary command center position when possible.
2. Fallback to the target player's inferred map position if no command center is available.
3. Launch the Scud Storm via the same engine path as `Game.ScudStormAtPosition`.

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
5. `count` (optional int, default `1`, clamped `1..9`)

Queue requests share the same executor as autonomous production. Pending unit
commands reserve their cost and producer queue slots until the production ID is
observed in the engine queue, the producer is lost, or five seconds of simulated
gameplay elapse. A successful acknowledgement means at least one command was
submitted; engine execution can still reject a command. Construction and upgrade
spending are outside this pending unit budget.

`Game.BuildWorker` args:

1. Same args as `Game.QueueUnit`
2. If `unit_template` is omitted, adapter discovers the worker/dozer offered by each producer.
3. `count` is applied once per selected producer; supply producer fan-out queues up to that count at each eligible supply building.

`Game.QueueUpgrade` args:

1. `player_index` (optional int, default local player)
2. `producer_object_id` (optional int; explicit producer building)
3. `producer_kind` (optional string; adapter currently recognizes `palace`, `black_market`, `blackmarket`, `market`, or `any`)
4. `upgrade_name` (required string; internal upgrade identifier)

`Game.QueueUpgrade` behavior:

1. Resolve an owned producer building with a production queue.
2. Resolve the upgrade by its internal name via `TheUpgradeCenter`.
3. Validate ownership, affordability, duplicate/in-progress state, and producer compatibility.
4. Issue the native `MSG_QUEUE_UPGRADE` command against that producer.

`Game.PurchaseScience` args:

1. `player_index` (optional int, default local player)
2. `science_name` (required string; internal science/promotion identifier)

`Game.PurchaseScience` behavior:

1. Resolve the science by internal name via `TheScienceStore`.
2. Validate that the player can currently purchase it.
3. Issue the native `MSG_PURCHASE_SCIENCE` command for that player.

`Game.SetMoney` args:

1. `player_index` (optional int, default local player)
2. `money` (required integer, clamped to the engine `UnsignedInt` max)

`Game.SetMoney` behavior:

1. Directly mutates the selected player's current `Money` total.
2. Intended for debugging only.
3. Rejects multiplayer sessions with `invalid_state` + `cash_mutation_not_allowed_in_multiplayer`.
4. Safe target modes are single-player and skirmish.

`Game.DebugDeshroud` args:

1. `player_index` (optional int, default local player)

`Game.DebugDeshroud` behavior:

1. Permanently reveals the selected player's map for debugging.
2. Intended for debugging only.
3. Rejects multiplayer sessions with `invalid_state` + `deshroud_not_allowed_in_multiplayer`.
4. Safe target modes are single-player and skirmish.

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

`Game.CaptureBuilding` args:

1. `player_index` (optional int, default local player)
2. `target_object_id` (required int, capturable building object)
3. `source_object_id` (optional int, explicit rebel/Black Lotus source; defaults to first idle owned capture-capable unit)

`Game.CaptureBuilding` behavior:

1. Resolve a capturable target building.
2. Resolve an owned capture-capable source unit, preferring the explicit `source_object_id` when provided.
3. Validate the capture action through the engine's normal special-power checks.
4. Issue `MSG_DO_SPECIAL_POWER_AT_OBJECT` using the capture-building special power against the target.

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

`Game.Display.Get`:

1. Returns the live renderer `width`, `height`, and `windowed` state.
2. Also returns `configured_width`, `configured_height`, and `configured_windowed` from global game settings when available.

`Autonomy.SetMode` args:

1. `mode` (required string: `manual|hybrid|autonomous`)

`Autonomy.Configure` args:

1. `profile` (optional string: `sprawl_economic|sprawl_balanced|standard|aggressive|economic|defensive|tech|sprawl|builtin_passthrough`)
2. `player_index` (optional int; defaults to local player)
3. `target_player_index` (optional int)
4. `economy_bias` (optional number `0..1`)
5. `aggression_bias` (optional number `0..1`)
6. `defense_bias` (optional number `0..1`)
7. `expansion_bias` (optional number `0..1`)
8. `capture_tech` (optional bool)
9. `allow_superweapons` (optional bool, default false): permits autonomous new SCUD Storm construction and automatic firing, including the defensive baseline. Existing structures/foundations and manual commands remain available. Mobile SCUD Launchers are conventional units and are unaffected.
10. `sprawl_multiplier` (optional number, default `1.0`, adapter-clamped; scales `sprawl` profile expansion caps)
11. `rule_overrides` (optional object): rule IDs mapped to `true` (enable),
    `false` (disable), or `null` (inherit). Omitted IDs retain their overrides.
12. `rule_scope` (optional object): `session_id`, `match_generation`, `player_index`
    from live Status; a mismatch rejects Configure with `rule_scope_changed`.

### Live rule controls

Read Status/Telemetry `result.rule_controls`: `version`, `session_id`, `revision`,
`changed_tick` and a `rules` array. Rows contain `id`, `label`, `description`,
`kind`, `profile_default`, `override`, `effective`, `gate_reason`,
`last_block_reason` and `last_block_tick`. Effective means selected, not proof
of issued work. Last-block fields describe historical rejections.

| ID | Scope |
| --- | --- |
| `market_growth` | New Black Markets, including income recovery |
| `zone_expansion` | New supply zones and deliberate non-supply footholds |
| `capacity_growth` | Additional producers; first-producer recovery remains available |
| `combat_production` | Conventional combat recruitment, including garrison infantry; siege/capture/scout purchases are separate |
| `siege_production` | Mobile SCUD Launcher purchases |
| `worker_recruitment` | All automatic worker purchases |
| `worker_redistribution` | New idle-worker transfers; transfer cleanup/emergency evacuation continue |
| `static_defense` | New defensive tunnels/Stingers; footholds use expansion control |
| `raids` | New limited raids before the economic milestone |
| `sustained_assaults` | New persistent offensive groups after the milestone |
| `scouting` | New scout missions and scout/radar recruitment |
| `capture` | New capture missions, capture-source recruitment and capture upgrades |
| `garrisons` | New garrison assignments/reinforcements |
| `counterbattery` | New mobile artillery-response tasks |
| `storm_construction` / `storm_firing` | Independent automatic Storm build/fire controls |
| `tech_upgrades` | Palaces, science and automatic upgrades |
| `idle_guard` | Automatic guard orders to idle ground combat |
| `defer_income_under_threat` | Economic-profile income restriction during response |
| `defer_offense_under_threat` | Quiet-pressure requirement for economic qualification and new sustained assaults |

Example via `POST /api/adapter/command`:

```json
{"cmd":"Autonomy.Configure","args":{"rule_overrides":{"market_growth":true,"defer_income_under_threat":false,"zone_expansion":false,"scouting":true,"capture":false}}}
```

Read back with `{"cmd":"Autonomy.Status","args":{}}` on that same endpoint.
The existing backend forwards these fields; the new contract requires the new
game binary. Zones UI discovers the catalog and supplies the optional scope guard.

Overrides survive pause/resume, reconnection and profile changes within the same
match/controller. Reset, new match or changed configured player scope clears them.
Unknown IDs and invalid values reject before Configure mutation; legacy field
types are prevalidated too. Identical overrides are no-ops. Changes emit compact
`kind=rule` events with ID, simulation tick, request ID and old/new values.

Disable stops new automatic work; existing queues and construction/combat/capture/
garrison tasks retain their lifecycles. Manual commands stay explicit actions.
Funds, prerequisites, targets, legal placement and the global command gate remain
enforced. Both Storm controls additionally require `allow_superweapons=true`.
Scout Technicals also transport workers/garrisons; scouting-off does not forbid
transport purchases. Income deferral applies to the economic profile; disabling
it retains the income reserve and market concurrency. Offense deferral does not
remove the cash/asset/milestone requirements.

Expansion-off constrains growth/fallback placement to existing observed zone
circles. Markets can try other existing safe zones; `no_existing_zone_space`
reports no permitted placement. Captures, manual builds and previously issued
construction can still introduce observed zones. Legacy `Automation.*` commands
remain individual standalone rules; use rule overrides for durable control of
all autonomous paths. `attack_enabled`, `capture_tech`, `garrisons_enabled` supply
inherited defaults, with explicit rule overrides taking precedence. Numeric
targets/reserves/cooldowns retain their existing configuration in this change.

`sprawl_balanced` notes:

1. Intended for normal `$50K` starts rather than debug-max cash.
2. Applies lower structure caps than `sprawl`, favors earlier market growth, and preserves a larger cash reserve before extra perimeter production.
3. Sustains combat production up to a soft army cap of about `100` mobile combat units and then replaces losses. Contained garrison/transport troops and immobile defenders do not consume this mobile cap; queued combat infantry and Quad/Scorpion/Buggy/SCUD Launcher replacements do. Defense and bounded scout replenishment retain their own policy gates.
4. Uses one shared shallow allocator across eligible owned producers, before and after the $100K milestone. Actual and pending queue entries count toward a depth of two; pre-milestone passes issue at most 16 orders, and approved low-cash emergency passes at most three. Emergency counter floors, capture/scout utility, prerequisites, reserve and army-cap gates remain active.

`sprawl_economic` reuses adaptive sprawl managers with a shared profile spending
plan. Other presets keep their existing behavior.

- Quiet target:24 mobile combat units including counted queues, with initial
  composition floors of six Quads, four Scorpions and four infantry, plus legal
  mobile SCUD Launcher coverage. Targets do not prove unit completion.
- Fresh visible mobile enemy memory within max(900,2.5*zone radius) of an owned
  zone, or recent zone damage/artillery pressure, starts response. Damage is not
  required. Response persists30 simulation seconds after the last detection and
  targets60–150 units according to nearby threats. Optional expansion, tech and
  capacity growth yield to counter/siege production. Quiet periods suppress raids.
- Quiet construction protects$6K; optional workers/army protect$8.5K including a
  market allowance. Response army protects$1K. Missing producers/builders and
  the first income replacement retain bounded recovery. Recruitment shares an
  80-worker ceiling including queued/pending matching workers; stashes initially
  recruit two. All automated worker/combat queues stay shallow (depth two).
- Income has priority over additional sprawl. Allow two concurrent markets when
  at least three are completed and available cash >=$11K. Funded spare factories
  grow toward min(8,2+markets/3) without requiring saturated quiet queues.
- Twelve completed markets plus >=$11K available cash for30 quiet simulation
  seconds activates existing sustained assault/1000-unit production before$100K.
  This asset/funding heuristic is not measured gross income. The$100K trigger
  remains. `economic_victory.activation_reason` distinguishes `income_capacity`
  from `cash_milestone`; selecting a different profile clears an income-only latch.
  Once twelve markets are complete, quiet purchases preserve that$11K entry
  balance so growth cannot continually reset the qualification timer. After entry,
  normal growth floors resume. During response, the combat target remains1000 if
  this offensive milestone was already achieved;60–150 applies before it.
- Pending unit orders and unstarted reserved sites reduce available funds. Site
  claims release when a native foundation binds or task lifecycle ends. Actual
  template costs and legal command paths remain enforced. Operator commands
  outside automation keep normal semantics. Switching profiles does not cancel
  existing queues, foundations or already committed attacks.
- Storm permission remains mandatory. This profile uses its shared spend gate
  instead of the legacy urgent-expansion cash gate, retaining one in-progress
  Storm and existing prerequisite/placement/survival checks.

`profile_policy.spending_plan` reports active/reason, response/hold deadline,
offensive phase, army target, worker limit, cash floors and market concurrency.
`is_balanced_sprawl` denotes the shared adaptive family for both presets;
`economy_first_sprawl` identifies this specialization.

Idle worker redistribution runs before local worker production, including at the
80-worker liquidity cap or during low cash. It retains three available workers
at the main base and one or two at developed/active donor zones, moves at most
one worker per pass, and tracks incoming workers until arrival or a bounded
timeout. Active construction/task/transport reservations are respected.

Staged autonomous raids select a nearby group within 1000 map units of a seed,
exclude allied objectives, and stage from the selected group's center. Their
base origin remains the retreat destination. Existing raids in their attack or
retreat phase allow new ready reinforcement waves; assembling waves still
suppress duplicate launches to the same objective. The whole-raid two-minute deadline
is removed. A 45-second inactivity watchdog tracks forward movement of a
commit-sized cohort, recent shots fired and damage received; command refreshes
do not count as progress. Two local route retries precede a stalled-raid retreat,
and retries remain counted across waypoint advances. Viable engaged groups keep
fighting. Quiet final attacks pursue nearby visible enemies (1800 map units),
or complete at an objective after 30 seconds without activity when the arrival
quorum is present and no visible objective remains. Completion releases units
at the front. Retreat still applies below minimum viable strength, or after 35%
casualties before the final attack when the group is no longer engaged.
Retreat uses move orders and retains task ownership until 70% return or a
60-second retreat timeout. Successful partial commits retain the existing policy.

Vehicle/mixed raids reserve `ceil(group_size / 10)` mobile SCUD Launcher seats,
capped at four and leaving at least one escort. Among equally full local groups,
selection prefers the group with more launchers up to that budget before using
distance to the target as a tie-break. Launchers are selected before filling the
remaining seats with escorts; they remain assigned to the same raid through
waypoint commands, recovery and retreat. Existing task reservations and the
1000-unit launch radius apply. Infantry-only and one-unit waves have no launcher
seats. Waves can launch without siege when no free launchers are in a viable local
group. This is wave composition; it does not increase the production army cap.

Scout/shuttle Technical replenishment chooses a compatible producer with the
shortest queue and at most one existing queued item, including pending adapter
orders. Template inference follows that producer's command set.

`Autonomy.Status`:

1. Returns compact `QueryResult` with current autonomy mode, profile, bias values, active automation rules, local player summary, and asset counts.

`Autonomy.Telemetry` also exposes `worker_liquidity` (available idle workers,
transfers in progress, last target/reason), `production` (mobile/contained combat
counts, queued mobile replacements, effective cap, selected intent and scout
producer queue depth), and `combat_tasks.details[].retreating`. Raid task details
include `raid_progress_age_ms`, nullable `raid_combat_age_ms`,
`raid_quorum_distance`, `raid_recovery_attempts`, and `raid_engaged_unit_count`.
Sequenced events include `raid_recovery`, `raid_retarget`, and `raid_complete`.

Raid details also include `raid_desired_scud_launchers`,
`raid_initial_scud_launchers`, `raid_live_scud_launchers`, and
`raid_scud_launcher_unit_ids` (initial assigned IDs, retained after losses).
The live count is recomputed from surviving assigned units when queried, including
retreating tasks. A `raid_composition` event records the selection reason once per
created wave; `raid_siege_assigned` records each initial launcher object ID (at most
four per wave). Neither assignment nor a live count proves arrival or shots fired.

Match outcome/final snapshot/run summary queries share the configured autonomy
player's perspective (default local player), independent of the selected player
in an object/resource query. `match_outcome.local_player_index` identifies this
perspective. Victory/defeat is evaluated for that player. Changing a query view
does not create a new match or reset elapsed time; actual game resets do.

Gameplay ticks and task deadlines use simulated milliseconds derived from the
engine frame number (`clock: "simulation"` in autonomy status/events). They stop
while the game simulation is paused and advance with game speed. Transport,
session logging and object-query cache ages retain wall time. `Autonomy.Pause`
pauses controller decisions; it does not pause the game or task deadlines.
New-match adapter resets clear tasks, reservations and controller cooldowns.

`Autonomy.Pause` / `Autonomy.Resume`:

1. Pause or resume autonomous rule execution without discarding the configured profile/settings.

`Autonomy.Reset`:

1. Clear autonomy settings and return the adapter to `manual` mode defaults.

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
19. `game.capturable_buildings`
20. `game.grid`
21. `game.grid_objects`

`game.objects_units_map`, `game.objects_buildings_map`, and `game.idle_workers` are served from a short-lived adapter cache (owned objects for the selected player). Responses include:

1. `cache_version` (monotonic per refresh)
2. `cache_age_ms` (age of the cached snapshot)

`game.zone_counts` args:

1. `player_index` (optional int, default local player)
2. `zone_center` (optional object `{x,y}`; omitted means whole owned map state)
3. `zone_radius` (optional number; default adapter-defined)

`game.capturable_buildings` args:

1. `player_index` (optional int, default local player perspective for ally filtering)

Returns non-allied capturable tech/neutral buildings visible to the engine object list, including `id`, `template`, `x`, `y`, and `player_index`.

`game.grid` args:

1. `grid_cols` (optional int, default `32`, clamped to adapter limits)
2. `grid_rows` (optional int, default `32`, clamped to adapter limits)

Returns occupied cell aggregates for the map. Each cell row includes a cell label such as `C30`, unit/building totals, dominant player index, per-player counts, and world-space bounds.

`game.grid_objects` args:

1. `cell` (optional string grid label such as `C30`)
2. `cells` (optional array of string grid labels)
3. `grid_cols` (optional int, must match the caller's grid interpretation)
4. `grid_rows` (optional int, must match the caller's grid interpretation)
5. `max_objects_per_cell` (optional int, default `120`, capped by adapter)

Returns bounded object detail for one or more specific grid cells. Responses include per-cell bounds, total object counts, a `truncated` flag when the per-cell object cap is hit, and the serialized objects for the requested cells.

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

## Recorded match observation (v1)

Capability: `recording_snapshot_v1`. Read-only commands:

```json
{"id":"record-1","cmd":"Autonomy.RecordingSnapshot","args":{"after_sequence":0,"max_events":512,"match_generation":0}}
{"id":"record-2","cmd":"Autonomy.RecordingEvents","args":{"after_sequence":512,"max_events":512,"match_generation":1}}
```

Both return version, simulation clock, match generation, bounded `events` and
`recording_health`. A supplied generation that differs from the game journal
resets the requested cursor to zero. Limits clamp to 1–512; invalid/negative
arguments are rejected. These reads do not generate command receipt events.
Persist received events before advancing a consumer cursor. Sequence gaps indicate
retention loss; duplicate delivery after a recorder restart is possible.

`RecordingSnapshot` additionally returns telemetry, controlled-player outcome,
status, retained final snapshot/run summary, map, frame, match activity and
collection start/end simulation ticks. Its world observation is cached for one
wall second. `telemetry.worker_observation` gives actual observed idle/contained
workers and observation tick; it is separate from old diagnostic placeholder
fields. Views are collected on the simulation thread and are not an atomic engine
transaction. Snapshot collection/JSON cost remains gameplay-thread work.

Events carry sequence, generation, tick/frame, kind/label, owner, task/object/
production/request IDs, bounded reason/state fields and optional position. Queue/template names use the reason field.
Command acceptance and production queue issuance do not mean physical completion.
The bounded game journal separates ordinary events from priority terminal/task
outcomes; either ring can overwrite. It retains the existing twelve-event UI/policy
window separately. Journal reset follows actual adapter match reset, not autonomy
configuration reset.

Health exposes record size/capacity, retained/high-water/overwritten records,
next sequence, field truncations, sampled producer timing, collection time,
adapter update timing totals/max/100-us histogram and diagnostic writer counters.
The adapter update histogram is not whole-engine frame time. Exact deaths/damage
and comprehensive unit spawn attribution are not supplied by these hooks.

Diagnostic text now uses a bounded producer queue and background writer. Configure
acknowledges queueing; disk open/write failure appears in writer counters. Existing
throttles remain, flushes are batched and shutdown joins the writer. Abrupt process
termination can lose buffered text. The TCP output backlog is bounded; this does
not change the separate Windows named-pipe implementation's blocking behavior.

The external backend handles segment persistence, compression and report creation.
See the workspace [post match guide](../../../docs/guides/post-match-adapter-analysis.md)
for consumer commands and evidence limitations.

### Balanced-sprawl economic victory

After the controlled player's balance first reaches $100,000, `Autonomy.Telemetry`
(and status) expose `economic_victory` with achieved/tick/threshold, army target
1,000, market target 120, assault readiness and reason. This is a strategy milestone,
not a game outcome. It remains achieved as cash is spent; new match, controlled
player and explicit autonomy reset clear it.

With attack automation enabled and fresh zone pressure cleared, quiet defense
allocations join available ground forces in persistent `autonomous_assault`
tasks. Existing autonomous raids keep their IDs on conversion. Up to96 local
units join each new group every 2 simulation seconds, with ten local launcher seats
selected before escorts. Visible legal targets receive object attack messages;
fogged objectives receive attack-move orders to stored positions. Surviving
assaults remain at the front and wait for fresh intelligence if needed. Pause,
simulation command gating and attack disable suppress new assault orders.

Production refills eligible queues toward depth 2 every production tick in this phase,
up to 64 new orders per pass, honoring actual unit prices, pending orders, army
slots and the configured reserve. `production` adds `refill_issued`,
`refill_desired_scuds`, `refill_protected_cash`, `refill_order_limit`,
`refill_emergency_pulse` and `refill_utility_template`. Normal refill protects
reserve plus$3000 for a market/workers, or plus$5000 with ample cash. Approved
low-cash emergency pulses issue at most three orders and retain$3000, or$600
when cash is below$4000; rejected
emergency spending cannot fall through to broad factory queueing. Legal scout
utility production uses one selected producer under the same budget/queue gates.
SCUD prerequisites and per-producer capabilities still apply. Markets grow toward
120 with four healthy construction slots when funded; below reserve+$15000,
one viable recovery market may use reserve cash. Positive net cash samples do
not suppress that recovery. Safe local worker replacement can use bounded reserve
money while retaining market funding. Zone seeds obey spending/reserve gates.

Assault details export `target_object_id`, `assault_target_template`,
`assault_target_age_ms`, `assault_damage_age_ms`. Templates and health progress
are read only for currently visible objectives. Movement progress uses median
member distance, and incidental firing does not extend objective progress.
After45s without target damage/group progress, or90s ineffective mobile pursuit,
`assault_objective_deferred` records the old objective/reason; that objective is
avoided for120s across cohorts. Useful visible structures have stronger target
preference, visible targets require a representative group member to legally
attack them, and additional cohorts avoid a mobile target with24 other committed
units. Fog objectives retain stored positions and bounded movement recovery.
These controls do not establish that1000
units run efficiently; next-match performance measurement remains required.

## Zone front direction diagnostics

Zone fronts use recent attack targets, the configured enemy target or nearest
actual enemy base, then an outward-from-main expansion fallback. The main base
retains the shared sprawl fallback when no valid enemy/attack direction exists.
Enemy membership uses engine diplomacy; lobby metadata uses PlayerList slot
mapping, not engine player IDs. Noncombat/dead/observer players are excluded.

`front_direction_source` on telemetry zones and `Autonomy.ZonesSnapshot` preserves
`attack_target`, `enemy_base`, `outward_from_main` or `sprawl_axis` before terrain
adjustment. Existing `front_source` describes the final geometry source. Nearby
entrances must lie within60 degrees of the resolved front and2.5 zone radii;
terrain radius limits still apply. Front/rear geometry and active flags refresh
at existing macro cadence without requiring a new building; unchanged spatial
fields add no JSON writes or text logs. A direction is a planning point, not
proof that defenses or units have arrived.

## Balanced-sprawl survival recovery, 2026-10-10

Severe collapse or loss of the established production spine activates a
30-second recovery hold, refreshed while the trigger persists. The early opening
is exempt from the broken-spine trigger. Pressure and reserve deficits count
current zones; deficits count missing resident units rather than zone entries.
Recovery overrides expansion, supply seeds, perimeter static growth and Palace
redundancy. It retains the main base, the best viable production/income zone,
and at most one additional quiet income cluster. Exposed non-core defense tasks
release their units; available mobile defenders and idle guard units can rally to
core fronts. Existing scout/capture/garrison reservations retain ownership.

Factories can grow while a market is under construction when protected cash is
available. Established economies target two ready Arms Dealers/two barracks,
raise the factory target to four in recovery, and may target up to eight when
cash exceeds reserve+$8000 and queues are saturated, within profile caps.
Replacement of the first two dealers or missing barracks can use reserve cash,
retaining$600 for builders with existing market income, otherwise$3100 for a
market and builders. Funded growth retains the ordinary reserve. New capacity
uses the selected core's rear and strict placement; one pending building of each
type prevents duplicate requests. If that core has no worker producer, recovery
can rebuild the faction Command Center through the existing smart builder.

Recovery redirects at most one traveling builder per worker pass from a freshly
attacked outer supply/defense site with no bound foundation. Active foundations
retain their builder. Idle redistribution favors core zones; one-worker refill
uses legal core producers with shallow queues, targeting at least eight workers
and four available core builders. Legacy bulk worker rules defer to this recovery
allocator. Command Center/worker recovery still requires a surviving builder or
legal worker producer; it cannot recover an already eliminated player.

`survival_policy` adds `recovery_active`, `recovery_zone`, `core_zones`,
`desired_arms_dealers`, `desired_barracks`, `queues_saturated`,
`arms_spend_allowed` and `arms_spend_reason`. `macro_build` exposes
`producer_recovery`, `worker_producer_recovery`, and `safe_survival_income`
intent reasons and the inferred worker-producer template when dispatched.
`zone_defense_reserves` uses posture `consolidated` and floor zero for deferred
outer zones. `worker_liquidity` adds `recovery_worker_target`,
`recovery_safe_builders`, `recovery_queued_workers` and `recovery_issued`.
Worker redirection records the bounded event `worker_survival_return`.

`production` adds `refill_phase`, `refill_queue_limit` and `refill_siege_issued`.
Legal mobile SCUD coverage starts before the milestone: a two-launcher floor,
roughly one per ten heavy vehicles, capped at six before the milestone. After
it, the existing four-launcher floor and proportional scaling apply. Counter
production precedes siege for a missing escort floor; once escorts exist, siege
gets replenishment slots. Low-cash emergency pulses buy counters. The deprecated
`scud_reserve` now reports actual queued launchers and the shared mix target;
its reason is `launcher_mix_deficit` or `launcher_mix_satisfied`. This is mobile
army composition; `allow_superweapons` continues to govern SCUD Storms.

Counterbattery live scans require a vehicle and reject `KINDOF_PROJECTILE`.
Enemy memory excludes projectile/effect objects; template classification rejects
missiles, and live task revalidation repeats the kind/ownership checks. Real
Tomahawk, Inferno, Nuke Cannon and other supported siege vehicles remain eligible.
Counterbattery orders honor depth two and builder funding during recovery.
Autonomous garrison replenishment also respects depth two in balanced sprawl.
