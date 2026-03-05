# MVP: Launch Game and Click Into Lobby

## Goal

Run one client command that:

1. Launches the game executable.
2. Connects to the AI control pipe.
3. Sends `Menu.Click` commands in sequence to reach a lobby.

## Script

Use:

1. `scripts/ai/zhctl.py`

Named pipe default:

1. `\\.\pipe\zh_ai_control`

## Quick Start

Single handshake check:

```powershell
python .\scripts\ai\zhctl.py hello
```

Single click test:

```powershell
python .\scripts\ai\zhctl.py menu-click --control-id "MainMenu.wnd:ButtonMultiplayer"
```

End-to-end MVP flow (launch + click sequence):

```powershell
python .\scripts\ai\zhctl.py lobby-mvp `
  --exe-path "C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour\GeneralsOnlineZH_hacked.exe" `
  --lobby-clicks MainMenu.wnd:ButtonMultiplayer MainMenu.wnd:ButtonNetwork `
  --delay-ms 1500
```

## Notes

1. `controlId` values are decorated GUI IDs (for example `MainMenu.wnd:ButtonMultiplayer`).
2. If the adapter is not running, pipe connect will timeout.
3. You can skip launch and only run menu automation by omitting `--exe-path`.
4. The default `lobby-mvp` clicks navigate to the LAN lobby (`ButtonMultiplayer` -> `ButtonNetwork`).

## LAN Lobby Controls

Use these IDs with `menu-click`:

1. Main Menu: `LanLobbyMenu.wnd:ButtonBack`
2. Create Game: `LanLobbyMenu.wnd:ButtonHost`
3. Join Game: `LanLobbyMenu.wnd:ButtonJoin`
4. Direct Connect: `LanLobbyMenu.wnd:ButtonDirectConnect`

Quick action commands:

```powershell
python .\scripts\ai\zhctl.py lan-click --lan-action main-menu
python .\scripts\ai\zhctl.py lan-click --lan-action create-game
python .\scripts\ai\zhctl.py lan-click --lan-action join-game
python .\scripts\ai\zhctl.py lan-click --lan-action direct-connect
```

Set LAN lobby player name field:

```powershell
python .\scripts\ai\zhctl.py lan-name-set --text "YourNameHere"
```

Click Skirmish from Main Menu:

```powershell
python .\scripts\ai\zhctl.py main-click --main-action skirmish
```

List discoverable controls (button inventory):

```powershell
python .\scripts\ai\zhctl.py list-controls --kind button
```

Include hidden controls and text entries:

```powershell
python .\scripts\ai\zhctl.py list-controls --kind all --include-hidden
```

Send in-game chat:

```powershell
python .\scripts\ai\zhctl.py chat-send --scope everyone --text "hello from controller"
python .\scripts\ai\zhctl.py chat-send --scope allies --text "team chat"
python .\scripts\ai\zhctl.py chat-send --scope players --text "self chat"
```

## Gameplay Build Commands

Find supply sources discovered from map objects/modules:

```powershell
python .\scripts\ai\zhctl.py find-supply-sources --pretty
```

Find a legal build location near a supply source for a worker/dozer:

```powershell
python .\scripts\ai\zhctl.py find-build-location-near-supply `
  --building-template GLASupplyStash `
  --pretty
```

Issue construction using worker/dozer placement:

```powershell
python .\scripts\ai\zhctl.py dozer-construct `
  --building-template GLASupplyStash `
  --pretty
```

High-level abstraction (recommended):

1. Pick an idle worker/dozer for the local player.
2. Pick closest supply source.
3. Find legal location near that source.
4. Build supply stash/supply center.

```powershell
python .\scripts\ai\zhctl.py build-supply-stash --pretty
```

Shroud-aware abstraction (recommended for unrevealed supply areas):

1. Same flow as `build-supply-stash`.
2. By default, it prefers a supply source that does not already have one of your supply drop-off buildings nearby.
3. Repeated calls avoid reusing the same last auto-selected supply source when possible.
4. If no legal location is currently revealed, the command moves the worker/dozer toward the selected supply source.
5. Run the same command again once vision is revealed.

```powershell
python .\scripts\ai\zhctl.py build-supply-stash-smart --pretty
```

Optional targeting knobs:

```powershell
python .\scripts\ai\zhctl.py build-supply-stash --worker-object-id 12345 --pretty
python .\scripts\ai\zhctl.py build-supply-stash-smart --worker-object-id 12345 --pretty
python .\scripts\ai\zhctl.py build-supply-stash-smart --supply-source-id 67890 --pretty
python .\scripts\ai\zhctl.py dozer-construct --supply-source-id 67890 --pretty
python .\scripts\ai\zhctl.py dozer-construct --building-template AmericaSupplyCenter --pretty
```
