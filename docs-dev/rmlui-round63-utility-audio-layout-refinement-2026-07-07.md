# RmlUi Round 63 Utility Audio And Layout Refinement

Date: 2026-07-07

Tasks: `FR-09-T04`, `FR-09-T05`, `FR-09-T07`, `FR-09-T09`,
`DV-03-T07`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 63 brings the utility route family into the same menu music/open-sound
contract as the refined shell, settings, and single-player pages.

The pass also tightens utility layout containment at `960x720`: Address Book
now shows all sixteen legacy address slots in a four-column field grid, Key
Bindings uses a bounded three-column capture grid, and Weapon Bindings uses a
two-column arsenal layout so the last weapon row no longer collides with the
footer.

## Implementation

- Added `data-menu-music="menu"` and `data-menu-sound-open="open"` to:
  - `addressbook`
  - `demos`
  - `keys`
  - `legacykeys`
  - `players`
  - `servers`
  - `ui_list`
  - `weapons`
- Added explicit action sounds across utility controls:
  - `open` for route/browser navigation.
  - `confirm` for connect, play, apply, and session-list item actions.
  - `change` for refresh, paging, key-capture, and editable form controls.
  - `cancel` for Back, Return, and Reset actions.
- Added `data-action-type="capture"` to the keybind capture surfaces.
- Renamed the movement bind id from `keys-back` to `keys-backpedal` to avoid
  colliding with the footer Back control id.
- Reworked Address Book into a four-column typed field grid with all
  `adr0` through `adr15` cvar fields visible in the static fallback view.
- Reworked Key Bindings into a stable three-column layout for Combat,
  Inventory, Movement, and Interface controls.
- Reworked Weapon Bindings into two bounded groups: Main Arsenal and Heavy
  Weapons.
- Added route-specific utility RCSS sizing for browser tables, player setup,
  address fields, keybind grids, and weapon bind groups.

## Runtime Evidence

Focused staged OpenGL probes used `.install/worr_x86_64.exe`, `.install` as
basedir, `game basew`, `r_renderer opengl`, `ui_rml_enable 1`, and
`pushmenu <route>` for every utility route.

- `.install/basew/logs/round63_utility_addressbook_pushmenu_layout_final.log`
- `.install/basew/logs/round63_utility_demos_pushmenu_layout_final.log`
- `.install/basew/logs/round63_utility_keys_pushmenu_layout_final.log`
- `.install/basew/logs/round63_utility_legacykeys_pushmenu_layout_final.log`
- `.install/basew/logs/round63_utility_players_pushmenu_layout_final.log`
- `.install/basew/logs/round63_utility_servers_pushmenu_layout_final.log`
- `.install/basew/logs/round63_utility_ui_list_pushmenu_layout_final.log`
- `.install/basew/logs/round63_utility_weapons_pushmenu_layout_final2.log`

Those logs confirm that each utility route:

- Routed through the `pushmenu` bridge into `ui_rml_runtime_open`.
- Opened its RmlUi document in the guarded OpenGL context.
- Requested menu open sound `open`.
- Requested menu music cue `menu`.
- Loaded the Quake II Rerelease TTF faces for display, UI, and mono text.
- Reported active RmlUi runtime status at `960x720`.

Visual evidence:

- `.tmp/rmlui/round63-screens/round63_utility_addressbook_pushmenu_layout_final.png`
- `.tmp/rmlui/round63-screens/round63_utility_demos_pushmenu_layout_final.png`
- `.tmp/rmlui/round63-screens/round63_utility_keys_pushmenu_layout_final.png`
- `.tmp/rmlui/round63-screens/round63_utility_legacykeys_pushmenu_layout_final.png`
- `.tmp/rmlui/round63-screens/round63_utility_players_pushmenu_layout_final.png`
- `.tmp/rmlui/round63-screens/round63_utility_servers_pushmenu_layout_final.png`
- `.tmp/rmlui/round63-screens/round63_utility_ui_list_pushmenu_layout_final.png`
- `.tmp/rmlui/round63-screens/round63_utility_weapons_pushmenu_layout_final2.png`

## Validation

Accepted validation commands:

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python -m pytest tools\ui_smoke`

The final full UI smoke run reported `224 passed`.

## Remaining Gaps

- This pass does not claim live browser/list/keybind/player preview
  controllers; utility routes still use the accepted static controller-stub
  contracts.
- Full keyboard/controller navigation parity, live key-capture behavior,
  localization stress coverage, automated route-wide pixel clipping
  assertions, and native Vulkan/RTX-vkpt RmlUi rendering remain later gates.
