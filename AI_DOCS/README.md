# AI Docs Index

## Purpose

Use this directory as the primary architecture and protocol context for the Zero Hour AI adapter/controller work.

## Read First

1. `AI_HOOKS.md` for the overall adapter/brain architecture and implementation phases.
2. `CONTROL_PROTOCOL.md` for the named-pipe JSONL contract and supported commands.
3. `SIMULATION.md` for multiplayer lockstep/desync constraints.
4. `AI_COMMANDS.md` for the current command surface and planned additions.
5. `STATEFRAME_V2.md` and `STATEFRAME_V2_CHECKLIST.md` for richer streaming state work.
6. `MVP_LOBBY_AUTOMATION.md` for current CLI-driven launch/menu/lobby workflows.

## Repository Context Outside This Folder

Agents should not assume all AI-control context lives only under `AI_DOCS`.

The supported desktop operator console is maintained outside this checkout:

1. `../../projects/zones-ui/README.md`

That project provides the default Zones viewer and HTTP-backed operator controls, including:

1. Autonomy profile and lifecycle controls
2. Manual commands and diagnostic tools
3. Zones, telemetry, and player views

## Guidance

1. If the task touches the desktop controller UI, read `../../projects/zones-ui/README.md` and `../../projects/zones-ui/CAPABILITY_MATRIX.md`.
2. If the task touches the in-process adapter, command semantics, or protocol messages, read the relevant `AI_DOCS/*.md` files first and use them as the source of truth.
