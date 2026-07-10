# RmlUi Parallel Round 5 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `FR-09-T05`,
`FR-09-T06`, `FR-09-T09`, `FR-03-T08`, `DV-03-T07`, `DV-04-T02`, and
`DV-07-T04`.

## Purpose

Integrate the fifth five-agent RmlUi migration pass. This round keeps the
migration in scaffold territory, but it advances the proof surface from route
existence to dependency-free runtime document probing, conservative
`controller_stub` route progression, static RML semantics validation, and
machine-readable progress reporting.

This round does not add the real RmlUi dependency, a native renderer bridge,
live C++ data controllers, runtime document rendering, screenshot/layout
capture, parity evidence, or legacy JSON/menu removal.

## Integrated Work

- Worker 1 added a runtime document probe to `src/client/ui_rml/`. The scaffold
  now registers `ui_rml_asset_root` and `ui_rml_probe [route_id]`, resolves a
  small route registry to runtime `ui/rml` document paths, probes documents
  through `FS_LoadFileEx`, and keeps legacy UI ownership by returning `false`
  from `UI_Rml_OpenMenu`.
- Worker 2 promoted exactly five shell/settings routes to `controller_stub`:
  `main`, `game`, `options`, `video`, and `download_status`. The same phase
  state is reflected in the central smoke manifest and
  `assets/ui/rml/shell/routes.json`; every other tracked route remains
  `starter`.
- Worker 3 added `tools/ui_smoke/check_rmlui_semantics.py` plus focused tests
  for static route-target resolution, command element IDs, and direct cvar
  token validation.
- Worker 4 added `tools/ui_smoke/report_rmlui_progress.py` plus focused tests
  for text and markdown summaries of route totals, document presence, required
  coverage, waves, owners, statuses, and migration phases.
- Worker 5 prepared the Round 5 docs/status shell. This coordinator pass
  replaces the placeholders with accepted worker evidence and validation
  results.

## Coordinator Adjustments

- `docs-dev/plans/rmlui-ui-migration-roadmap.md` now records Round 5 as the
  accepted probe/controller-stub validation baseline with migration phases
  `starter=52` and `controller_stub=5`.
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md` now
  carries the same Round 5 status for `FR-09`, `DV-03-T07`, `DV-04-T02`, and
  `DV-07-T04`.
- `docs-dev/rmlui-agent5-docs-status-round5-2026-07-02.md` now records that
  its coordinator placeholders were resolved here.

## Validation

- `python tools\ui_smoke\check_rmlui_manifest.py`
  - Passed.
  - `57` total routes, `57` required, `57` present, `0` pending.
  - Waves: `A=21`, `B=11`, `C=25`.
  - Migration phases: `starter=52`, `controller_stub=5`.
  - Parsed `151` RML/import files and checked `213` local `href` imports.
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
  - Passed.
  - Core: `1` route, `Migration phases: starter=1`.
  - Shell: `23` routes, `Migration phases: controller_stub=5, starter=18`.
  - Smoke: `57` routes, `Migration phases: controller_stub=5, starter=52`.
- `python tools\ui_smoke\check_rmlui_semantics.py`
  - Passed.
  - `57` routes known, `57` documents checked, `52` route targets checked,
    `289` command elements checked, and `452` cvar references checked.
- `python tools\ui_smoke\report_rmlui_progress.py`
  - Passed.
  - `57/57` documents present, `57/57` required documents present, waves
    `A=21`, `B=11`, `C=25`, and migration phases `starter=52`,
    `controller_stub=5`.
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`
  - Passed.
  - Emitted the same counts in a compact markdown table.
- `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_report_rmlui_progress.py`
  - Passed: `34` tests.
- `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round5-package-validation --base-game basew --archive-name pak0.pkz`
  - Passed.
  - Packed `194` files from `assets`.
  - Validated `31` botfile package/loose files and `100` RmlUi package/loose
    files.
  - Mirrored loose asset paths: `botfiles`, `ui/rml`.
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`
  - Passed.
  - Ninja also emitted `warning: premature end of file; recovering`, but the
    command exited successfully.

## Remaining Work

- Select and integrate the actual RmlUi dependency.
- Replace the guarded stub/probe with a real RmlUi document runtime and
  renderer-backed render interface.
- Implement native OpenGL, Vulkan, and RTX/vkpt renderer support without
  redirecting Vulkan paths to OpenGL.
- Add live cvar, command, condition, navigation, list, preview, and session
  controllers.
- Add runtime navigation, screenshot/layout, input, accessibility, renderer,
  and session parity validation.
- Remove or intentionally archive legacy JSON/menu paths only after Gate G3/G4
  evidence is accepted.
