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
