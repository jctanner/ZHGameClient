# Bot UI (Tkinter)

Desktop control panel for Zero Hour AI adapter.

## Run

```powershell
python .\ZHGameClient\bot-ui\src\main.py
```

## Current Scope

1. Connect/disconnect to `\\.\pipe\zh_ai_control`.
2. Send key session/game commands from UI buttons.
3. Poll `Game.Query` for map/entity/player data.
4. Render simplified map objects on a Tkinter canvas.
5. Show player metadata and live logs.
