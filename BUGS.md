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
  status: code changed, needs gameplay validation and log confirmation.
- `B004`: Macro priorities after the opening are weak. The bot often falls into `action=none` instead of choosing the next economic or expansion structure.
  status: code changed, needs gameplay validation and log confirmation.

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
  status: code changed, needs gameplay validation and log confirmation.
- `B011`: Unit production mix is poor. The bot can over-focus on infantry, and the arms-dealer logic can collapse into producing mostly or only radar vans instead of a healthier vehicle mix such as quads and scorpions.
  status: code changed. Latest live logs now show successful `Game.QueueQuadsAllWarFactories` actions after arms dealers are established, so the "no vehicles from arms dealer" path is improved. Broader gameplay validation is still needed for overall unit-mix balance.
- `B012`: The intended army cap behavior is still soft and not yet proven to sustain a steady target force cleanly.
  status: code changed, needs gameplay validation and log confirmation.
- `B013`: Reserve logic, opening gates, and special-case production rules are still not fully aligned, so some production paths can crowd out better macro choices.
  status: code changed, needs gameplay validation and log confirmation.
- `B020`: Reserve mode has poor hysteresis. The bot can sit idle in `reserve_cash_recovery` for long stretches while cash slowly climbs, then resume spending immediately after barely crossing the reserve threshold.
  status: code changed, needs gameplay validation and log confirmation.

## Tech / Upgrades

- `B014`: Tech logic is still noisy. The bot repeatedly attempts science or upgrades in states where they are not currently actionable.
  status: code changed, needs gameplay validation and log confirmation.
- `B015`: Black market attempts are still triggered before palace prerequisites are met.
  status: code changed, needs gameplay validation and log confirmation.
- `B016`: Upgrade retries are still noisy around `producer_cannot_make_upgrade`, `queue_full`, `upgrade_already_in_production`, and `palace_not_found`.
  status: code changed, needs gameplay validation and log confirmation.

## Observability / Validation

- `B017`: The codebase does not appear to have a real unit-test harness for the autonomy logic; verification is still mostly rebuild, run, and inspect logs.
- `B018`: Several fixes have only been validated by partial log slices or visual inspection and still need clean end-to-end confirmation in stable matches.
- `B019`: The UI-adapter transport is not fully robust. Sessions have shown request timeouts and pipe write errors during reconnect/startup.
- `B021`: Autonomous startup can become a silent no-op in early game. The adapter accepts `Autonomy.Configure`, `Autonomy.SetMode`, and `Autonomy.Resume`, and now logs `autonomy_kickoff`, but no follow-up `autonomy_tick` or worker automation action is emitted.
  status: code changed. Root cause was timer/sentinel handling around `GetTickCount()` and `0`-deadline startup ticks. Autonomy is acting again in the latest live run, but still needs broader gameplay validation before marking fixed.
