# Simulation and Desync Notes

## Why This Matters

The game runs a deterministic lockstep-style simulation in multiplayer. Clients do not rely on a dedicated "cash sync" message each frame. Instead, they run the same simulation commands/events and arrive at the same money totals.

## How Money Stays Consistent Across Players

Money is part of per-player simulation state:

1. `Player` owns `m_money`.
2. `Player::xfer` serializes `m_money` as snapshot state.

References:

1. `GeneralsMD/Code/GameEngine/Include/Common/Player.h` (`m_money`, `getMoney`)
2. `GeneralsMD/Code/GameEngine/Source/Common/RTS/Player.cpp` (`Player::xfer`, `xferSnapshot(&m_money)`)

Money changes occur through normal simulation systems on all peers:

1. Build queue spends money via `withdraw`.
2. Income systems (for example supply docking) add money via `deposit`.

References:

1. `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/ProductionUpdate.cpp`
2. `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/DockUpdate/SupplyCenterDockUpdate.cpp`
3. `GeneralsMD/Code/GameEngine/Source/Common/RTS/Money.cpp`

## Why Local Cash Mutation Is Risky In Multiplayer

If one client changes money directly (for example via adapter calling `deposit/withdraw` locally) without an equivalent deterministic command processed by all peers, simulation diverges and may desync.

Existing debug/cheat cash paths are already guarded against multiplayer:

1. `MSG_CHEAT_ADD_CASH` only applies when `!isInMultiplayerGame()`.
2. `MSG_META_DEMO_ADD_CASH` has the same single-player guard.

Reference:

1. `GeneralsMD/Code/GameEngine/Source/GameClient/MessageStream/CommandXlat.cpp`

## Adapter Policy (Recommended)

For AI control adapter commands that modify cash:

1. Allow in single-player/skirmish only.
2. Reject in multiplayer with deterministic error code, such as `invalid_state` + `cash_mutation_not_allowed_in_multiplayer`.
3. Prefer legal command surfaces that naturally cause money changes (build, sell, gather, powers) for multiplayer-safe behavior.

## Practical Guidance

Safe to add:

1. `Game.GiveMoney` (single-player only)
2. `Game.SetMoney` (single-player/skirmish debug only)

Recommended debug behavior for `Game.SetMoney`:

1. Allow in single-player and skirmish debug workflows.
2. Reject network multiplayer with `cash_mutation_not_allowed_in_multiplayer`.
3. Implement by directly replacing the player's current `Money` total inside the local simulation.

Not safe without additional networked design:

1. Any direct adapter command that mutates `Money` during multiplayer sessions.
