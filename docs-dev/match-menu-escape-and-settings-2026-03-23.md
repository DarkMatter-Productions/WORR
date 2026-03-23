# Match/welcome menu access from Escape

## Task
- Replace the in-game hardcoded `UIMENU_GAME` escape behavior with multiplayer match menu routing.
- Add a game settings option to the multiplayer join menu list.

## Changes made
- Updated `src/client/keys.cpp`:
  - In the `K_ESCAPE` input path, when `cls.state >= ca_active && cl.maxclients > 1`, `KEY_GAME` now opens `UIMENU_MAIN`.
  - This uses existing cgame routing so multiplayer `UIMENU_MAIN` resolves to `dm_join`.
  - Single-player behavior remains `UIMENU_GAME` for consistency.
- Updated `src/game/cgame/ui/ui_core.cpp`:
  - Replaced client-state dependent routing condition with cvar-based multiplayer check (`maxclients > 1`) so cgame module compiles.
- Updated `src/game/cgame/ui/worr.json`:
  - Reworked `dm_join` and `join` into a 7-section structure with short breaker lines:
    - hostname/gametype-map
    - match join options
    - mymap and voting
    - host and match info
    - admin
    - settings and leave match
    - footer
  - Normalized settings label casing to `"Settings"` and removed duplicate entry.
  - Added `Leave Match` action with confirmation popup:
    - menu: `leave_match_confirm`
    - confirm action: `forcemenuoff; disconnect`
- Updated localization for the match menu entry labels:
  - Replaced `worr.json` join/hosted match actions with `$m_settings` and `$m_leave_match` labels.
  - Added `m_settings` and `m_leave_match` entries to all 20 localization files under `assets/localization/`.
  - Wired `$...` menu labels through a small UI localization helper (`UI_Localize`) so JSON menu item labels can be localized from the existing localization engine.

## Notes
- `dm_join` now contains a single `Settings` action; fallback `join` contains one as well.
- No project task ID was provided for this change set; if you want this tied to the roadmap tracker, add the task ID here and in the feature proposal document.
