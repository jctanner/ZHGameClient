# AI Command Backlog

## Purpose

Catalog candidate adapter commands based on existing in-engine command surfaces and AI capabilities.

## Current Implemented Commands

1. `Game.Query`
2. `Game.QueueUnit`
3. `Game.QueueUpgrade`
4. `Game.PurchaseScience`
5. `Game.BuildWorker`
6. `Game.FindSupplySources`
7. `Game.FindBuildLocationNearSupply`
8. `Game.DozerConstruct`
9. `Game.BuildSupplyStashAuto`
10. `Game.BuildSupplyStashSmart`
11. `Game.BuildBarracksSmart`
12. `Game.BuildCommandCenterSmart`
13. `Game.AttackMove`
14. `Chat.Send`
15. `Menu.Click`
16. `Menu.SetText`
17. `Menu.ListControls`
18. `Game.SetMoney` (single-player/skirmish debug only)
19. `Game.BuildArmsDealerSmart`
20. `Game.BuildPalaceSmart`
21. `Game.BuildBlackMarketSmart`
22. `Game.BuildScudStormSmart`
23. `Game.BuildBuildingMix`
24. `Game.ScudStormAtPosition`
25. `Game.ScudStormAtPlayer`

## Implemented Query Primitives (Orchestration-Critical)

1. `Game.Query path=game.objects` -> owned unit/building inventory with object IDs, template names, class, coordinates, construction state, and idle flag
2. `Game.Query path=game.visible_enemies` -> visible enemy objects with IDs, template names, class, coordinates, and owner player index

## Minimum Primitive Set For A Basic RTS Loop

1. Read owned state: `Game.Query path=game.objects`
2. Read enemy state: `Game.Query path=game.visible_enemies`
3. Build economy structure: `Game.BuildSupplyStashSmart`
4. Build workers at stash/factory: `Game.BuildWorker { producer_kind:any, producer_object_id, unit_template }`
5. Build production building: `Game.BuildBarracksSmart`
6. Queue soldiers: `Game.QueueUnit { producer_kind:any, producer_object_id, unit_template }`
7. Apply combat pressure: `Game.AttackMove { object_ids, x, y }`

## Priority 1: Core Combat and Movement (Low-Medium Effort)

1. `Game.MoveObject { object_id, x, y }`
2. `Game.AttackObject { object_id, target_object_id }`
3. `Game.AttackGround { object_id, x, y }`
4. `Game.GuardObject { object_id, target_object_id }`
5. `Game.GuardPosition { object_id, x, y }`
6. `Game.Stop { object_id }`

Suggested engine surfaces:

1. `AICommandInterface` wrappers (`aiMoveToPosition`, `aiAttackObject`, `aiAttackPosition`, `aiAttackMoveToPosition`, `aiGuardObject`, `aiGuardPosition`)
2. Existing game message equivalents in `MessageStream.h`

## Priority 2: Support and Utility Unit Actions (Medium Effort)

1. `Game.RepairObject { object_id, target_object_id }`
2. `Game.GetHealed { object_id, depot_object_id }`
3. `Game.GetRepaired { object_id, depot_object_id }`
4. `Game.Enter { object_id, target_object_id }`
5. `Game.Dock { object_id, target_object_id }`
6. `Game.Evacuate { object_id, expose_stealth_units? }`

Suggested validation:

1. Use `ActionManager` checks (`canRepairObject`, `canGetHealedAt`, `canGetRepairedAt`, `canEnterObject`, `canDockAt`).
2. Enforce ownership and liveness checks before command dispatch.

## Priority 3: Production and Structure Control (Medium Effort)

1. `Game.QueueUpgrade { producer_object_id, upgrade_name }`
2. `Game.CancelUpgrade { producer_object_id, upgrade_name }`
3. `Game.CancelUnitCreate { producer_object_id, unit_template? }`
4. `Game.SetRallyPoint { producer_object_id, x, y }`
5. `Game.SellObject { object_id }`
6. `Game.DozerCancelConstruct { structure_object_id }`
7. `Game.ResumeConstruction { object_id, structure_object_id }`

Notes:

1. Prefer existing game message semantics where available (`MSG_QUEUE_UPGRADE`, `MSG_CANCEL_UPGRADE`, `MSG_SET_RALLY_POINT`, `MSG_SELL`, `MSG_DOZER_CANCEL_CONSTRUCT`, `MSG_RESUME_CONSTRUCTION`).

## Priority 4: High-Level Macro AI Commands (Medium-High Effort)

1. `Game.BuildBySupplies { thing_name, minimum_cash }`
2. `Game.BuildSpecificBuilding { thing_name }`
3. `Game.BuildBaseDefense { flank }`
4. `Game.BuildBaseDefenseStructure { thing_name, flank }`
5. `Game.BuildUpgradeSmart { upgrade_name }`
6. `Game.QueueDozerSmart {}`

Suggested surfaces:

1. Player/AI helper methods exposed in `Player` and `AIPlayer`.
2. Existing BuildAssistant legality and prerequisite checks.

## Priority 5: Advanced / Faction-Specific Actions (Higher Risk)

1. `Game.DoSpecialPowerAtLocation { object_id, power, x, y }`
2. `Game.DoSpecialPowerAtObject { object_id, power, target_object_id }`
3. `Game.CaptureBuilding { object_id, target_object_id }`
4. `Game.DisableVehicleHack { object_id, target_object_id }`
5. `Game.DisableBuildingHack { object_id, target_object_id }`
6. `Game.StealCashHack { object_id, target_object_id }`
7. `Game.SnipeVehicle { object_id, target_object_id }`

Notes:

1. Requires strict capability checks through `ActionManager`.
2. Some commands are faction/unit specific and need robust rejection reasons.

## Command Design Rules

1. Always route through legal command/action surfaces.
2. Never mutate deep sim state directly in adapter command handlers, except explicitly labeled single-player/skirmish debug commands such as `Game.SetMoney`.
3. Keep deterministic behavior for multiplayer safety.
4. Return explicit rejection reasons (`invalid_state`, `unsupported_cmd`, `no_prereq`, `no_money`, etc.).
5. Include object ownership and readiness validation in every command path.

## Multiplayer Safety Notes

1. Commands that mirror existing player/network command paths are generally safer for multiplayer.
2. Direct local state mutation commands (for example direct money mutation) should remain single-player only unless backed by synchronized networked command handling.
3. See `AI_DOCS/SIMULATION.md` for desync and lockstep details.

## Debug-Only Commands

1. `Game.SetMoney { player_index?, money }`
2. This command intentionally mutates `Player::Money` directly and must remain disabled for real multiplayer sessions.
