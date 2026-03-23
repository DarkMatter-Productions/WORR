# Match Menu Session Split (2026-03-23)

## Related Task
- `FR-03-T08` - Complete split between engine-side and cgame-side UI ownership where still mixed.

## Summary
- Restricted the multiplayer match menu path so it is only selected during an active multiplayer game session.
- Moved the multiplayer-session menu definitions out of the main UI script into a dedicated `worr-multiplayer.json` menu asset.
- Embedded and loaded the new multiplayer menu script alongside the main UI script so the cgame UI still resolves match-specific menus without relying on `worr.json`.

## Implementation Details

### Session-gated routing
- `src/client/keys.cpp`
  - Tightened Escape-key multiplayer-menu routing to require an active in-game multiplayer session:
    - `cls.state == ca_active`
    - `cl.serverstate == ss_game`
    - `cls.netchan.protocol > 0`
    - `!cls.demo.playback`
    - `cl.servercount > 0`
    - `cl.maxclients > 1`
  - When those conditions are not met, Escape falls back to the normal in-game menu instead of the match menu.
- `src/game/cgame/cg_entity_api.cpp`
  - Added `CG_IsActiveMultiplayerSession()` so cgame can answer the same session-state question without directly binding UI code to engine globals.
- `src/game/cgame/ui/ui_cgame_access.h`
- `src/game/cgame/ui/ui_cgame_access.cpp`
  - Exposed the new helper through the existing cgame/UI access boundary.
- `src/game/cgame/ui/ui_core.cpp`
  - Replaced the earlier broad `cl.maxclients > 1` menu-routing check with the new active-session helper before mapping `UIMENU_MAIN` to the multiplayer menu.

### Menu-script split
- Added `src/game/cgame/ui/worr-multiplayer.json`.
- Removed the following multiplayer-session menus from `src/game/cgame/ui/worr.json` and placed them in the new script:
  - `dm_welcome`
  - `dm_join`
  - `join`
  - `dm_hostinfo`
  - `dm_matchinfo`
- Left shared/supporting menus such as `leave_match_confirm`, vote/admin/tournament flows, and general menus in `worr.json`.

### Embedded asset packaging
- `src/game/cgame/ui/ui_internal.h`
  - Added `UI_MULTIPLAYER_FILE` for the dedicated multiplayer menu asset name.
- `meson.build`
  - Added a second embedded JSON generation target for `worr-multiplayer.json`.
- `src/common/files.c`
  - Registered `worr-multiplayer.json` as a builtin embedded file.
- `src/game/cgame/ui/ui_core.cpp`
  - Updated `MenuSystem::Init()` to load both embedded JSON menu scripts.

## Validation
- Parsed both UI JSON files successfully with PowerShell `ConvertFrom-Json`:
  - `src/game/cgame/ui/worr.json`
  - `src/game/cgame/ui/worr-multiplayer.json`
- Built successfully with:

```powershell
meson compile -C builddir-client-cpp20
```

## Staging Notes
- `meson install -C builddir-client-cpp20` is currently not usable in this workspace because the configured install prefix resolves to `C:\...` and fails with a Windows permission error.
- The canonical `builddir-win` refresh path is also currently blocked by an existing `llvm-ar` thin-archive failure in third-party subprojects.
- For local validation, `.install/` was refreshed directly from the successful `builddir-client-cpp20` runtime outputs so the staged client binaries match this change set.
