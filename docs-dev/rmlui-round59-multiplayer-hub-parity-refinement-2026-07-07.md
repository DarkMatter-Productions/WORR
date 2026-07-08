# RmlUi Round 59 Multiplayer Hub Parity Refinement

Date: 2026-07-07

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T08`, `FR-09-T09`,
`DV-03-T07`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 59 refines the RmlUi Multiplayer hub into a real shell menu instead of a
starter/session stub. The page now preserves the original pre-RmlUi
multiplayer intent: browse q2servers.com, browse saved/broadcast addresses,
browse demos, start a server with the legacy setup defaults, adjust player
setup, and enter Options.

The pass also removes the dead `multiplayer.connect_address` command, keeps
menu music/open-sound metadata active, and ensures the staged `pushmenu
multiplayer` path opens the RmlUi page without missing-model warnings.

## Implementation

- Switched `assets/ui/rml/multiplayer/multiplayer.rml` onto the shared shell
  layout contract while keeping the session status fallback.
- Replaced placeholder route-opening controls with legacy command strings:
  - `pushmenu servers "+https://q2servers.com/?raw=2" "https://q2servers.com/?raw=0"`
  - `pushmenu servers "favorites://" "file:///servers.lst" "broadcast://"`
  - `pushmenu demos`
  - legacy `match_setup_*` defaults followed by `pushmenu startserver`
  - `pushmenu players`
  - `pushmenu options`
- Added route-target metadata for navigation graph validation while preserving
  real command dispatch strings for runtime behavior.
- Added explicit shell-grid styling for `#multiplayer-menu-actions`, primary
  Start Server treatment, and a fixed-width status panel that fits the
  `604px` command contract.
- Removed stale session-theme multiplayer grid selectors so the shell grid owns
  placement consistently.
- Removed runtime-only placeholder `data-model` bindings from the hub until a
  live multiplayer controller exists, preventing RmlUi missing-model warnings.
- Updated multiplayer route metadata to describe the current static parity and
  deferred live-controller boundary.

## Runtime Evidence

Focused staged OpenGL probes used `.install/worr_x86_64.exe`,
`.install` as basedir, `game basew`, `r_renderer opengl`, and
`ui_rml_enable 1`.

- `.install/basew/logs/round59_pushmenu_multiplayer_clean.log`
  - `pushmenu multiplayer` routed through `ui_rml_runtime_open`.
  - `ui/rml/multiplayer/multiplayer.rml` opened successfully.
  - Active status reported `route='multiplayer'` and `availability='ready'`.
  - RmlUi loaded Quake II Rerelease TTF faces for display, UI, and mono text.
  - The route consumed `data-menu-sound-open="open"` and
    `data-menu-music="menu"`.
  - No missing-model warnings remained.
- `.install/basew/logs/round59_multiplayer_visual_final.log`
  - Captured `round59_multiplayer_final.tga`.
  - Confirmed the final shell grid opened through `pushmenu multiplayer`.
- `.tmp/rmlui/round59-screens/round59_multiplayer_final.png`
  - Visual evidence for the refined two-column Multiplayer hub at `960x720`.
- `.tmp/rmlui/runtime-capture/round59_main_capture.tga`
  - Main menu capture remains valid at `960x720`; button right edges are not
    clipped in the current staged shell layout.

## Validation

Accepted validation commands:

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\ui_smoke\check_rmlui_semantics.py`
- `python tools\ui_smoke\check_rmlui_command_inventory.py`
- `python tools\ui_smoke\check_rmlui_condition_inventory.py`
- `python tools\ui_smoke\check_rmlui_data_model_inventory.py`
- `python tools\ui_smoke\check_rmlui_navigation_graph.py`
- `python tools\ui_smoke\check_rmlui_metadata_sync.py`
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --route-id main --engine-exe .install/worr_x86_64.exe --install-dir .install --write-manifest .tmp/rmlui/round59-main-capture-manifest.json --evidence-id round59_main_capture --format json --timeout 120`
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --route-id main --engine-exe .install/worr_x86_64.exe --install-dir .install --write-manifest .tmp/rmlui/round59-main-capture-manifest.json --evidence-id round59_main_capture --format json`
- `python -m pytest tools\ui_smoke`

The final full UI smoke run reported `224 passed`.

The unsupported-RCSS grep was also run with explanatory comments filtered out;
no unsupported rule usage was found outside existing comments.

## Remaining Gaps

- The Multiplayer hub still uses static fallback copy for session status until
  live multiplayer/session controllers land.
- Native Vulkan/RTX-vkpt RmlUi rendering remains pending and must be completed
  natively.
- Full route-wide keyboard/controller navigation, live server/demo/player
  controller behavior, localization stress, and automated pixel-clipping
  assertions remain later parity gates.
