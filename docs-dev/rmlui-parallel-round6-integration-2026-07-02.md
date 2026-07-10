# RmlUi Parallel Round 6 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `FR-09-T04`,
`FR-09-T05`, `FR-09-T06`, `FR-09-T09`, `FR-03-T08`, `DV-03-T07`,
`DV-04-T02`, and `DV-07-T04`.

## Purpose

Integrate the sixth five-agent RmlUi migration pass. This round expands the
dependency-free route/document probe to the full smoke-manifest route set,
adds static validation for controller contract references, adds runtime asset
path validation, promotes a second conservative shell/settings batch to
`controller_stub`, and makes progress reporting machine-readable.

This is still scaffold and validation work. It does not add the real RmlUi
dependency, a native renderer bridge, live C++ data controllers, runtime
document rendering, screenshot/layout capture, parity evidence, or legacy
JSON/menu removal.

## Integrated Work

- Worker 1 expanded the static client runtime probe registry to `58` entries:
  all `57` route IDs from `tools/ui_smoke/rmlui_manifest.json` plus
  `core.runtime_smoke`. The registry stores paths relative to
  `ui_rml_asset_root`, and `ui_rml_probe` now covers the full route set.
- Worker 2 extended `tools/ui_smoke/check_rmlui_route_contracts.py` so optional
  route-level `controller_contracts` metadata is validated for safe token
  fields, fixture paths, fixture existence, and JSON object fixture shape. The
  checker now reports controller contract reference counts.
- Worker 3 added `tools/ui_smoke/check_rmlui_runtime_assets.py` to map authored
  route documents under `assets/ui/rml/` to runtime paths under `ui/rml/`, with
  optional staged loose-file checks under `<install-dir>/<base-game>/ui/rml/`.
- Worker 4 promoted exactly five additional shell/settings routes to
  `controller_stub`: `performance`, `accessibility`, `sound`, `screen`, and
  `input`. These join the Round 5 routes `main`, `game`, `options`, `video`,
  and `download_status`, giving `controller_stub=10`.
- Worker 5 added `--format json` to
  `tools/ui_smoke/report_rmlui_progress.py`, preserving the existing text and
  markdown outputs while exposing stable machine-readable progress facts.

## Coordinator Adjustments

- `docs-dev/rmlui-agent1-runtime-route-coverage-round6-2026-07-02.md` was
  updated with the actual coordinator validation results instead of planned
  validation placeholders.
- `docs-dev/rmlui-agent5-progress-json-round6-2026-07-02.md` was updated to
  report the final integrated `starter=47` and `controller_stub=10` counts
  after Worker 4's route progression landed.
- `docs-dev/plans/rmlui-ui-migration-roadmap.md` and the canonical strategic
  roadmap now record Round 6 as the accepted full-route probe, controller
  contract validation, runtime asset, second route-progression, and JSON
  progress-report baseline.

## Validation

- Manifest/registry comparison
  - Passed.
  - Manifest routes: `57`.
  - Registered routes: `58`.
  - Missing manifest route IDs: `0`.
  - Unexpected extra route IDs: `0`.
  - `core.runtime_smoke`: present.
- `python tools\ui_smoke\check_rmlui_manifest.py`
  - Passed.
  - `57` total routes, `57` required, `57` present, `0` pending.
  - Waves: `A=21`, `B=11`, `C=25`.
  - Migration phases: `starter=47`, `controller_stub=10`.
  - Parsed `151` RML/import files and checked `213` local `href` imports.
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
  - Passed.
  - Core: `1` route, `0` controller contract references,
    `Migration phases: starter=1`.
  - Shell: `23` routes, `28` controller contract references,
    `Migration phases: controller_stub=10, starter=13`.
  - Smoke: `57` routes, `0` controller contract references,
    `Migration phases: controller_stub=10, starter=47`.
- `python tools\ui_smoke\check_rmlui_semantics.py`
  - Passed.
  - `57` routes known, `57` documents checked, `52` route targets checked,
    `289` command elements checked, and `452` cvar references checked.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py`
  - Passed.
  - `57` routes checked, `57` source documents present, `57` runtime paths
    derived.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --install-dir .tmp\rmlui\round6-package-validation --base-game basew`
  - Passed.
  - `57` staged loose route document files present.
- `python tools\ui_smoke\report_rmlui_progress.py`
  - Passed with migration phases `starter=47`, `controller_stub=10`.
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`
  - Passed with the same counts.
- `python tools\ui_smoke\report_rmlui_progress.py --format json`
  - Passed with `57` total routes, `57/57` documents present, and migration
    phases `starter=47`, `controller_stub=10`.
- `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_report_rmlui_progress.py`
  - Passed: `43` tests.
- `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round6-package-validation --base-game basew --archive-name pak0.pkz`
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
