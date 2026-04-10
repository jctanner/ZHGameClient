# Known Bugs

## Autonomous `sprawl_balanced`

- `B001`: Opening build recovery is too passive after `construct_site_not_created`. The bot often idles for many macro ticks instead of retrying the missing opening structure promptly.
- `B002`: Opening state can still become inconsistent after failed site creation. Counts sometimes advance enough to unlock later stages even when the intended structure is not reliably established.
- `B003`: Combat production starts too early. Infantry production can begin while the opening is still incomplete, which competes with core infrastructure spending.
- `B004`: Macro priorities after the opening are weak. The bot often falls into `action=none` instead of choosing the next economic or expansion structure.

## Smart Build / Placement

- `B005`: Supply stash placement is still too loose. Stashes are often built several building lengths away from the supply source instead of tightly adjacent.
- `B006`: Supply stash placement is still terrain-naive. The bot can prefer legal sites that are on the wrong side of cliffs or mountains relative to the supply and worker approach.
- `B007`: Additional supply expansion is still under-prioritized. Nearby unclaimed supplies are not always claimed as early as they should be.
- `B008`: Non-core structure retries are still too aggressive. Tunnels and stingers can still perform same-tick fallback attempts after `construct_site_not_created`.
- `B009`: Same-type spacing is improved for core production buildings, but not fully validated across all maps and retries.

## Economy / Production Balance

- `B010`: Low-cash recovery is better than before, but the bot can still stall near the reserve floor instead of committing cleanly to eco recovery or expansion.
- `B011`: Unit production mix is poor. The bot can over-focus on infantry, and the arms-dealer logic can collapse into producing mostly or only radar vans instead of a healthier vehicle mix such as quads and scorpions.
- `B012`: The intended army cap behavior is still soft and not yet proven to sustain a steady target force cleanly.
- `B013`: Reserve logic, opening gates, and special-case production rules are still not fully aligned, so some production paths can crowd out better macro choices.
- `B020`: Reserve mode has poor hysteresis. The bot can sit idle in `reserve_cash_recovery` for long stretches while cash slowly climbs, then resume spending immediately after barely crossing the reserve threshold.

## Tech / Upgrades

- `B014`: Tech logic is still noisy. The bot repeatedly attempts science or upgrades in states where they are not currently actionable.
- `B015`: Black market attempts are still triggered before palace prerequisites are met.
- `B016`: Upgrade retries are still noisy around `producer_cannot_make_upgrade`, `queue_full`, `upgrade_already_in_production`, and `palace_not_found`.

## Observability / Validation

- `B017`: The codebase does not appear to have a real unit-test harness for the autonomy logic; verification is still mostly rebuild, run, and inspect logs.
- `B018`: Several fixes have only been validated by partial log slices or visual inspection and still need clean end-to-end confirmation in stable matches.
- `B019`: The UI-adapter transport is not fully robust. Sessions have shown request timeouts and pipe write errors during reconnect/startup.
