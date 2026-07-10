# RmlUi Parallel Round 8 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `FR-09-T05`,
`FR-09-T06`, `FR-09-T09`, `FR-03-T08`, `DV-03-T07`, `DV-04-T02`,
`DV-07-T04`

Status: coordinator accepted as a scaffold and validation round.

## Scope

Round 8 advanced the RmlUi migration from `controller_stub` metadata into a
small, guarded `runtime_stub` tier for menu-entrypoint document probing. The
accepted runtime stubs are `main`, `game`, and `download_status`. They are
eligible because the current client scaffold can map normal menu entry points
to those route IDs, probe their runtime document paths, and return `false` from
`UI_Rml_OpenMenu` so the legacy UI remains authoritative.

This round does not claim a first-class RmlUi dependency, native renderer
integration, live controller behavior, native runtime navigation, screenshots,
parity, or legacy JSON removal.

## Subagent Results

- Agent 1 added `tools/ui_smoke/check_rmlui_menu_entrypoints.py` and focused
  tests. The checker validates `UI_Rml_RouteForMenu` mappings, fallthrough
  behavior, manifest matches, runtime registry matches, and runtime path
  matches for menu-entrypoint routes.
- Agent 2 added `tools/ui_smoke/check_rmlui_runtime_stub_eligibility.py` and
  focused tests. The checker requires each `runtime_stub` route to have shell
  route metadata, controller contracts, a menu-entrypoint mapping, a runtime
  registry entry, and the guarded legacy-fallback `UI_Rml_OpenMenu` behavior.
- Agent 3 promoted exactly `main`, `game`, and `download_status` from
  `controller_stub` to `runtime_stub` in `tools/ui_smoke/rmlui_manifest.json`
  and `assets/ui/rml/shell/routes.json`.
- Agent 4 extended `tools/ui_smoke/check_rmlui_runtime_assets.py` with
  `--format text|json`, preserving the existing text output while adding
  structured success and error payloads.
- Agent 5 extended `tools/ui_smoke/report_rmlui_progress.py` with phase
  progression and `routes_by_phase` facts in text, markdown, and JSON output.

## Accepted Baseline

- Smoke routes: `57` total, `57` required, `57` present, `0` pending.
- Waves: `A=21`, `B=11`, `C=25`.
- Migration phases: `starter=42`, `controller_stub=12`, `runtime_stub=3`.
- Runtime registry: `57` manifest routes, `58` registered routes including
  `core.runtime_smoke`, `0` missing, `0` unexpected, `0` duplicates.
- Runtime asset paths: `57` route documents plus `16` imported assets, for
  `73` derived runtime paths.
- Staged loose runtime paths under `.tmp/rmlui/round8-package-validation`:
  `73` present, `0` missing, including all `16` imported assets.
- Controller-contract references: `44` references across `15` advanced shell
  routes.
- Phase progression: `15` advanced routes, `26.3%`.

## Coordinator Validation

- `python tools\ui_smoke\check_rmlui_manifest.py`: passed.
- `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed.
- `python tools\ui_smoke\check_rmlui_semantics.py`: passed.
- `python tools\ui_smoke\check_rmlui_menu_entrypoints.py`: passed with `5`
  menu cases, `4` mapped route cases, and `3` unique mapped route IDs.
- `python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py`: passed with
  `3` `runtime_stub` routes checked.
- `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed with
  `12` `controller_stub` routes checked.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports`:
  passed with `73` derived runtime paths.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --format json`:
  passed with `ok=true` and no errors.
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`: passed.
- `python tools\ui_smoke\report_rmlui_progress.py`: passed.
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`: passed.
- `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed.
- `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_report_rmlui_progress.py`:
  passed with `73` tests.
- `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round8-package-validation --base-game basew --archive-name pak0.pkz`:
  passed, packed `194` files, validated `31` botfile package/loose files, and
  validated `100` RmlUi package/loose files.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round8-package-validation --base-game basew`:
  passed with `73` staged loose runtime paths present.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round8-package-validation --base-game basew --format json`:
  passed with `ok=true`, `staged_loose_files.present=73`, and
  `staged_loose_imported_assets.present=16`.
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
  passed. The broader Windows Meson build remains subject to the existing
  `llvm-ar` regular-archive to thin-archive blocker recorded in earlier RmlUi
  rounds.

## Progression Notes

- `runtime_stub` currently means guarded route probing through the existing
  client scaffold, not real RmlUi presentation ownership.
- `controller_stub` still marks source-route/controller metadata readiness and
  static contract coverage, not live C++ controller execution.
- The next safe implementation frontier is first-class RmlUi dependency wiring
  or an actual native-rendered sample document, with OpenGL, Vulkan, and
  RTX/vkpt handled natively.
