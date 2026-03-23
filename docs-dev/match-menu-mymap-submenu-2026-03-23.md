# Match Menu MyMap Submenu (2026-03-23)

## Related Task IDs
- `FR-03-T08` - Complete split between engine-side and cgame-side UI ownership where still mixed.
- `FR-07-T01` - Add end-to-end validation scenarios for map vote, mymap queue, and nextmap transitions.

## Summary
- Converted the multiplayer `MyMap` entry from a direct list jump into a dedicated submenu flow.
- Preserved MyMap flag state across menu navigation instead of clearing it every time the player opens the menu.
- Closed the active UI after a successful queue request and reset the temporary MyMap flags so the flow behaves like a completed action instead of leaving stale state behind.

## Implementation Details

### Dedicated submenu
- `src/game/cgame/ui/worr.json`
  - Added `mymap_main`, a compact submenu that shows:
    - current MyMap availability/status
    - current flag summary
    - `Select Map`
    - `Flags...`
    - `Clear Flags`
  - Kept `mymap_flags` as the dedicated tri-state flag editor.

### Sgame menu/control flow
- `src/game/sgame/menu/menu_page_mymap.cpp`
  - Reworked `OpenMyMapMenu()` to populate MyMap submenu cvars and `pushmenu mymap_main` instead of immediately opening the generic `ui_list`.
  - Added `OpenMyMapSelectMenu()` for the actual map-picker list launch.
  - Added `RefreshMyMapMenu()` so the submenu summary/status stays in sync after flag changes and clears.
  - Added explicit availability/status messaging for:
    - tournaments
    - server-disabled MyMap
    - missing login/social ID

### Command behavior
- `src/game/sgame/commands/command_client.cpp`
  - Added `worr_mymap_select` to launch the existing MyMap list from the new submenu.
  - Updated `worr_mymap_flag` and `worr_mymap_clear` to refresh both:
    - the flag editor cvars
    - the MyMap submenu summary cvars
    - the active `ui_list` view when the map list is open
  - Updated `worr_mymap_queue` so:
    - no-arg use opens the map selection list
    - successful queueing clears temporary MyMap flags
    - successful queueing closes the active menu stack

### State behavior change
- Removed the old behavior where opening MyMap immediately reset all pending MyMap flags.
- Flag state now persists while the player navigates between:
  - the multiplayer menu
  - the MyMap submenu
  - the MyMap flag editor
  - the map selection list
- The temporary state is cleared after a successful queue request or an explicit `Clear Flags` action.

## Validation
- Parsed `src/game/cgame/ui/worr.json` successfully with PowerShell `ConvertFrom-Json`.
- Built successfully with:

```powershell
meson compile -C builddir-client-cpp20
```

- Refreshed and validated `.install/` with:

```powershell
python tools/refresh_install.py --build-dir builddir-client-cpp20 --install-dir .install --base-game basew --platform-id windows-x86_64
```
