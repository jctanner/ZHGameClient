# Bot UI (Tkinter)

Desktop control panel for Zero Hour AI adapter.

## Run

```powershell
python .\ZHGameClient\bot-ui\src\main.py
```

## Current Scope

1. Connect/disconnect to `\\.\pipe\zh_ai_control`.
2. Provide separate `Manual`, `Autonomy`, and `Menu` tabs instead of one crowded operator surface.
3. Use the `Autonomy` tab as the default supervisory workflow with `sprawl_balanced` selected by default.
4. Poll `Game.Query` for map/entity/player data and reduce polling when autonomy is active.
5. Render simplified map objects on a Tkinter canvas.
6. Show player metadata and live logs.
