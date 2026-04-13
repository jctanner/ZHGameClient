# Known Bugs

This file is append-only.

- Never delete a bug from this file.
- When code changes are made against a bug, keep the bug entry and add status text such as `status: code changed, needs validation`.
- Only mark a bug as confirmed fixed after explicit validation in logs, tests, or live gameplay.

## Autonomous `sprawl_balanced`

- `B001`: Opening build recovery is too passive after `construct_site_not_created`. The bot often idles for many macro ticks instead of retrying the missing opening structure promptly.
  status: code changed, needs gameplay validation and log confirmation.
- `B002`: Opening state can still become inconsistent after failed site creation. Counts sometimes advance enough to unlock later stages even when the intended structure is not reliably established.
  status: code changed, needs gameplay validation and log confirmation.
- `B003`: Combat production starts too early. Infantry production can begin while the opening is still incomplete, which competes with core infrastructure spending.
  status: partially improved. In the latest live run, production stayed gated behind `opening_not_ready` through the early opening and did not immediately fall into infantry spam. This still needs broader gameplay validation before being treated as fixed.
- `B004`: Macro priorities after the opening are weak. The bot often falls into `action=none` instead of choosing the next economic or expansion structure.
  status: reproduced in the latest live run. Midgame logs still show `action=none` with reasons such as `macro_wait_zone_followup` and `macro_wait_zone_expansion` while the bot remains below the desired zone count.

## Smart Build / Placement

- `B005`: Supply stash placement is still too loose. Stashes are often built several building lengths away from the supply source instead of tightly adjacent.
  status: code changed, needs gameplay validation and log confirmation.
- `B006`: Supply stash placement is still terrain-naive. The bot can prefer legal sites that are on the wrong side of cliffs or mountains relative to the supply and worker approach.
  status: code changed, needs gameplay validation and log confirmation.
- `B007`: Additional supply expansion is still under-prioritized. Nearby unclaimed supplies are not always claimed as early as they should be.
  status: code changed, needs gameplay validation and log confirmation.
- `B008`: Non-core structure retries are still too aggressive. Tunnels and stingers can still perform same-tick fallback attempts after `construct_site_not_created`.
  status: code changed, needs gameplay validation and log confirmation.
- `B009`: Same-type spacing is improved for core production buildings, but not fully validated across all maps and retries.

## Economy / Production Balance

- `B010`: Low-cash recovery is better than before, but the bot can still stall near the reserve floor instead of committing cleanly to eco recovery or expansion.
  status: reproduced in the latest live run. The bot reached a healthier midgame, but production remained pinned in `reserve_cash_recovery` for long stretches while cash climbed only gradually from roughly `$6k` to `$10k`.
- `B011`: Unit production mix is poor. The bot can over-focus on infantry, and the arms-dealer logic can collapse into producing mostly or only radar vans instead of a healthier vehicle mix such as quads and scorpions.
  status: partially improved, still open. Earlier runs showed radar-van-heavy or barracks-heavy drift, but the latest live run is now favoring war-factory production with `Game.QueueQuadsAllWarFactories` and `Game.QueueScorpionsAllWarFactories`. This still needs later-game validation to confirm it does not regress once the force grows larger.
- `B012`: The intended army cap behavior is still soft and not yet proven to sustain a steady target force cleanly.
  status: partially improved, still open. In the latest live run the army reached and held at `100` units with `reason=army_cap_reached`, which suggests the hard cap is now engaging. This still needs gameplay validation to confirm the cap remains stable under combat losses and does not regress above the threshold in longer matches.
- `B013`: Reserve logic, opening gates, and special-case production rules are still not fully aligned, so some production paths can crowd out better macro choices.
  status: code changed, needs gameplay validation and log confirmation.
- `B020`: Reserve mode has poor hysteresis. The bot can sit idle in `reserve_cash_recovery` for long stretches while cash slowly climbs, then resume spending immediately after barely crossing the reserve threshold.
  status: reproduced in the latest live run. `reserve_cash_recovery` remained active across long windows even as cash crawled upward from roughly `$6k` through `$10k`, producing visible idle time.

## Tech / Upgrades

- `B014`: Tech logic is still noisy. The bot repeatedly attempts science or upgrades in states where they are not currently actionable.
  status: code changed, needs gameplay validation and log confirmation.
- `B015`: Black market attempts are still triggered before palace prerequisites are met.
  status: reproduced in the latest live run. Macro still attempts `Game.BuildBlackMarketSmart` before palace tech is ready and reports `black_market_prereq_missing`; later in the same run, after palace progression, black-market placement still failed with `construct_site_not_created`, so the full econ transition remains unreliable.
- `B016`: Upgrade retries are still noisy around `producer_cannot_make_upgrade`, `queue_full`, `upgrade_already_in_production`, and `palace_not_found`.
  status: partially reproduced in the latest live run. Non-actionable retry noise remains visible through repeated `no_prereq` and `producer_under_construction` outcomes during production/tech progression.

## Observability / Validation

- `B017`: The codebase does not appear to have a real unit-test harness for the autonomy logic; verification is still mostly rebuild, run, and inspect logs.
  status: partially improved. The existing adapter test harness now covers extracted Phase 3 zone-direction helpers such as direction normalization, map-position parsing, recent attack target selection, front-direction fallback resolution, and front/rear geometry. Broader autonomy orchestration is still primarily validated through gameplay and logs.
- `B018`: Several fixes have only been validated by partial log slices or visual inspection and still need clean end-to-end confirmation in stable matches.
- `B019`: The UI-adapter transport is not fully robust. Sessions have shown request timeouts and pipe write errors during reconnect/startup.
- `B021`: Autonomous startup can become a silent no-op in early game. The adapter accepts `Autonomy.Configure`, `Autonomy.SetMode`, and `Autonomy.Resume`, and now logs `autonomy_kickoff`, but no follow-up `autonomy_tick` or worker automation action is emitted.
  status: code changed. Root cause was timer/sentinel handling around `GetTickCount()` and `0`-deadline startup ticks. Autonomy is acting again in the latest live run, but still needs broader gameplay validation before marking fixed.

## Latest Session Findings

- `B022`: Phase 3 front-direction telemetry is not yet easy to validate from logs. `Autonomy.Telemetry` responses in `adapter.log` are truncated before the per-zone `front_source` fields, so we cannot confirm from logs alone when zones are using `attack_target`, `enemy_base`, or `sprawl_axis`.
  status: partially improved. The bot-ui telemetry dump now captures full `Autonomy.Telemetry` payloads, including per-zone `front_source`, so this is no longer blocked on `adapter.log`. Live telemetry currently shows `enemy_base` dominating front selection in the latest run.
- `B023`: Reserve recovery currently suppresses too much of the production system. The bot can stay in `reserve_cash_recovery` while unit counts continue to drift upward from other queued sources, leaving the reported decision state disconnected from the actual force growth.
  status: newly observed in the latest live run.
- `B024`: Macro intent during stable midgame is still under-explained. Repeated `autonomy_tick category=macro ... action=none issued=0 reason=` entries appear even when zones are active and the base is still expanding, which makes both behavior and debugging ambiguous.
  status: newly observed in the latest live run.
- `B025`: Midgame expansion can stall after reaching a small baseline footprint. The bot can settle at a few supply stashes and production buildings, stop choosing meaningful expansion actions, and continue growing the army instead of pushing additional eco or zone development.
  status: partially improved, still open. The latest run did reach `current_zone_count=4` and `developed_zone_count=4`, so the immediate `3 -> 4` expansion stall is no longer the dominant failure. However, the adapter then stopped expanding entirely because `desired_zone_count` also remained capped at `4`, even while the user still observed viable room to sprawl further.
- `B026`: Late-game balanced-sprawl production drifts back into barracks-first spam after the base is fully developed. Even with `current_zone_count == desired_zone_count`, the adapter repeatedly selects `Game.QueueRpgTroopersAllBarracks`, while arms-dealer output only appears opportunistically when the barracks queue is full. This causes the intended army-cap and mixed-composition behavior to fail in practice.
  status: reproduced in the latest live run. Midgame production did favor war-factory units initially, but later ticks again showed repeated `Game.QueueRpgTroopersAllBarracks` attempts after the base was established, so the barracks-heavy fallback is still present.
- `B027`: Production retries are still too eager against unavailable war-factory producers. The bot can repeatedly issue unit production choices into `queue_full` or `producer_under_construction` states instead of backing off cleanly and letting the producer become available first.
  status: partially improved, still open. The new retry delays reduced some of the earlier thrash, but the latest live run still shows repeated war-factory production failures such as `Game.QueueScudLauncher ... reason=queue_full`, so producer-aware backoff is not yet clean enough.
- `B028`: `sprawl_balanced` stops territorial growth too early because its desired expansion target is too conservative. Once `current_zone_count == desired_zone_count == 4`, macro behavior treats sprawl as complete and shifts into reserve/production behavior even when additional nearby supplies or safe expansion lanes still exist.
  status: newly observed in the latest live run.
- `B029`: Bot-ui autonomy telemetry can silently stop updating during an active session. The adapter can continue expanding and logging fresh zone counts while the autonomy tab remains frozen on the last telemetry snapshot, with no visible stale-state indicator.
  status: reproduced in the latest live run. `adapter.log` advanced to `zones=15` while `bot-ui-autonomy-telemetry.ndjson` stopped receiving fresh `Autonomy.Telemetry` payloads much earlier.
