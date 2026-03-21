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

Important related subproject:

1. `bot-ui/AGENTS.md`

That file defines the desktop operator UI plan in `ZHGameClient/bot-ui`, including:

1. Tkinter app scope and architecture
2. Protocol integration expectations
3. Query polling and `StateFrame` usage
4. Player metadata and tactical map rendering requirements

## Guidance

1. If the task touches the desktop controller UI, canvas rendering, polling/stream consumption, or operator workflows, read `bot-ui/AGENTS.md` before making changes.
2. If the task touches the in-process adapter, command semantics, or protocol messages, read the relevant `AI_DOCS/*.md` files first and use them as the source of truth.
