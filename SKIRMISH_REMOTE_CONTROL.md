# Skirmish Remote Control

This document describes the remote control API for setting up and launching skirmish games programmatically.

## Overview

The adapter now exposes commands to:
- Query current skirmish game setup state
- Configure all aspects of skirmish setup (slots, map, cash, restrictions, etc.)
- Start the game without manual GUI interaction

This enables fully automated game setup for testing, benchmarking, or AI training workflows.

## Query Skirmish State

**Command:** `Game.Query` with `path: "game.skirmish_setup"`

**Example:**
```json
{
  "type": "SessionCommand",
  "request_id": "req-1",
  "cmd": "Game.Query",
  "args": {
    "path": "game.skirmish_setup"
  }
}
```

**Response:**
```json
{
  "type": "QueryResult",
  "request_id": "req-1",
  "result": {
    "available": true,
    "map": "Tournament Desert.map",
    "seed": 12345,
    "starting_cash": 10000,
    "superweapon_restricted": false,
    "superweapon_restriction_mask": 0,
    "local_slot_num": 0,
    "is_host": true,
    "slots": [
      {
        "slot_index": 0,
        "state": "human",
        "color": 0,
        "template": 0,
        "team": -1,
        "start_position": -1,
        "name": "Player1"
      },
      {
        "slot_index": 1,
        "state": "brutal_ai",
        "color": 1,
        "template": 1,
        "team": -1,
        "start_position": -1,
        "name": ""
      },
      // ... up to 8 slots
    ],
    "path": "game.skirmish_setup"
  }
}
```

### Slot State Values
- `"open"` - Slot is available
- `"closed"` - Slot is disabled
- `"easy_ai"` - Easy AI player
- `"med_ai"` - Medium AI player
- `"brutal_ai"` - Brutal/Hard AI player
- `"human"` - Human player

### Special Values
- `color: -1` - Random color
- `template: -1` - Random faction
- `team: -1` - No team/alliance
- `start_position: -1` - Random starting position

## Configure Individual Slot

**Command:** `Skirmish.SetSlot`

**Args:**
- `slot_index` (or `slot`): Integer 0-7
- `state`: String (optional) - "open", "closed", "easy_ai", "med_ai", "brutal_ai", "human"
- `color`: Integer (optional) - Color index or -1 for random
- `template`: Integer (optional) - Faction/general template index or -1 for random
- `team`: Integer (optional) - Team number or -1 for no team
- `start_position`: Integer (optional) - Start position or -1 for random
- `name`: String (optional) - Player name (for human players)

**Example:**
```json
{
  "type": "SessionCommand",
  "request_id": "req-2",
  "cmd": "Skirmish.SetSlot",
  "args": {
    "slot": 1,
    "state": "brutal_ai",
    "color": 1,
    "template": 2,
    "team": -1,
    "start_position": -1
  }
}
```

## Set Map

**Command:** `Skirmish.SetMap`

**Args:**
- `map`: String - Map filename (e.g., "Tournament Desert.map")

**Example:**
```json
{
  "type": "SessionCommand",
  "request_id": "req-3",
  "cmd": "Skirmish.SetMap",
  "args": {
    "map": "Tournament Desert.map"
  }
}
```

## Set Starting Cash

**Command:** `Skirmish.SetStartingCash`

**Args:**
- `cash`: Integer - Starting money amount (must be >= 0)

**Example:**
```json
{
  "type": "SessionCommand",
  "request_id": "req-4",
  "cmd": "Skirmish.SetStartingCash",
  "args": {
    "cash": 10000
  }
}
```

## Set Superweapon Restriction

**Command:** `Skirmish.SetSuperweaponRestriction`

**Args:**
- `restricted`: Boolean - `true` to limit superweapons, `false` to allow

**Example:**
```json
{
  "type": "SessionCommand",
  "request_id": "req-5",
  "cmd": "Skirmish.SetSuperweaponRestriction",
  "args": {
    "restricted": true
  }
}
```

## Set Random Seed

**Command:** `Skirmish.SetSeed`

**Args:**
- `seed`: Integer - Random seed for game

**Example:**
```json
{
  "type": "SessionCommand",
  "request_id": "req-6",
  "cmd": "Skirmish.SetSeed",
  "args": {
    "seed": 42
  }
}
```

## Start Game

**Command:** `Skirmish.Start`

Launches the configured skirmish game.

**Args:**
- `game_id`: Integer (optional) - Game ID to assign

**Example:**
```json
{
  "type": "SessionCommand",
  "request_id": "req-7",
  "cmd": "Skirmish.Start",
  "args": {}
}
```

## Batch Configuration

**Command:** `Skirmish.Configure`

Configure multiple settings in a single atomic command.

**Args:**
- `map`: String (optional) - Map filename
- `starting_cash`: Integer (optional) - Starting money
- `superweapon_restricted`: Boolean (optional) - Superweapon limits
- `seed`: Integer (optional) - Random seed
- `slots`: Array (optional) - Array of slot configurations

**Example - Full Game Setup:**
```json
{
  "type": "SessionCommand",
  "request_id": "req-8",
  "cmd": "Skirmish.Configure",
  "args": {
    "map": "Tournament Desert.map",
    "starting_cash": 10000,
    "superweapon_restricted": true,
    "seed": 12345,
    "slots": [
      {
        "slot": 0,
        "state": "human",
        "color": 0,
        "template": 0,
        "team": 1,
        "start_position": 0
      },
      {
        "slot": 1,
        "state": "brutal_ai",
        "color": 1,
        "template": 1,
        "team": 2,
        "start_position": 1
      },
      {
        "slot": 2,
        "state": "brutal_ai",
        "color": 2,
        "template": 2,
        "team": 2,
        "start_position": 2
      },
      {
        "slot": 3,
        "state": "closed"
      },
      {
        "slot": 4,
        "state": "closed"
      },
      {
        "slot": 5,
        "state": "closed"
      },
      {
        "slot": 6,
        "state": "closed"
      },
      {
        "slot": 7,
        "state": "closed"
      }
    ]
  }
}
```

## Typical Workflow

1. Navigate to skirmish menu using `Menu.Click` commands
2. Query current setup: `Game.Query` with `path: "game.skirmish_setup"`
3. Configure game using `Skirmish.Configure` or individual commands
4. Start game: `Skirmish.Start`
5. Once in-game, engage autonomy mode if desired

## Error Responses

Commands return errors in these cases:

- `"skirmish_not_available"` - TheSkirmishGameInfo is null (not in skirmish setup)
- `"not_host"` - Only the host can modify skirmish settings
- `"missing_args"` - Required arguments not provided
- `"invalid_slot_index"` - Slot index out of range (must be 0-7)
- `"slot_not_found"` - Slot pointer is null
- `"missing_map"` / `"missing_cash"` / `"missing_seed"` - Required parameter missing

## Capabilities

New capabilities advertised in Hello message:
- `game_query_skirmish_setup`
- `skirmish_set_slot`
- `skirmish_set_map`
- `skirmish_set_starting_cash`
- `skirmish_set_superweapon_restriction`
- `skirmish_set_seed`
- `skirmish_start`
- `skirmish_configure`

## Implementation Notes

All skirmish commands:
- Directly modify `TheSkirmishGameInfo` global state
- Bypass the GUI layer for faster, more reliable setup
- Only work when `TheSkirmishGameInfo` is initialized (in skirmish menus)
- Require host privileges (single-player skirmish is always host)
- Are implemented in `AIControlAdapterSkirmish.inl`
