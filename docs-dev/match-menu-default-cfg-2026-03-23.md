# Match menu defaults and menu-entry updates

## Summary
- Updated multiplayer main-menu routing so the multiplayer flow uses `dm_join` instead of the generic `main` menu.
- Added a `settings` action to the `dm_join` menu, using `pushmenu options`.
- Added `assets/default.cfg` from local Quake II data as an initial `default.cfg` seed in the repo.

## Files changed
- `src/game/cgame/ui/ui_core.cpp`
- `src/game/cgame/ui/worr.json`
- `assets/default.cfg`

## Notes
- Multiplayer detection is implemented by checking `cls.state >= ca_active && cl.maxclients > 1` before selecting the menu for `UIMENU_DEFAULT` and `UIMENU_MAIN`.
- `assets/default.cfg` was sourced from `C:\Program Files (x86)\Quake2\baseq2\default.cfg` on this machine due the rerelease repository snapshot not containing a checked-in `baseq2/default.cfg`.
- Replace this file with a canonical rerelease-provided `default.cfg` if an official source is available.
