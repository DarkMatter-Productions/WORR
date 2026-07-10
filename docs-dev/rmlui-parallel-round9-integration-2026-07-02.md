# RmlUi Parallel Round 9 Integration

Date: 2026-07-02

Tasks: `FR-09-T02`, `FR-09-T05`, `FR-09-T07`, `FR-09-T09`,
`FR-09-T10`, `FR-03-T08`, `DV-03-T07`, `DV-04-T02`, `DV-07-T04`

Status: coordinator accepted as a scaffold and validation round.

## Scope

Round 9 expanded migration validation and promoted four utility routes to the
`controller_stub` phase: `addressbook`, `keys`, `legacykeys`, and `weapons`.
This round also added static navigation graph validation, controller fixture
validation, detailed runtime asset manifests, and a parity checklist manifest.

This round does not claim first-class RmlUi dependency integration, native
renderer output, live controller behavior, native runtime navigation,
screenshots, parity, or legacy JSON removal.

## Subagent Results

- Agent 1 added `tools/ui_smoke/check_rmlui_navigation_graph.py` and tests.
  The checker validates static `data-route-target` edges and reports graph
  facts from guarded roots `main`, `game`, and `download_status`.
- Agent 2 added `tools/ui_smoke/check_rmlui_controller_fixtures.py` and tests.
  The checker validates route `controller_contracts` references against mock
  fixture JSON files under `assets/ui/rml/contracts/`.
- Agent 3 added `assets/ui/rml/utility/routes.json`, promoted
  `addressbook`, `keys`, `legacykeys`, and `weapons` to `controller_stub`, and
  extended route-contract/controller-stub validation to discovered
  `assets/ui/rml/*/routes.json` metadata.
- Agent 4 added `--write-manifest <path>` to
  `tools/ui_smoke/check_rmlui_runtime_assets.py`, producing deterministic JSON
  for route/import source paths, runtime paths, source presence, and staged
  loose presence.
- Agent 5 added `tools/ui_smoke/rmlui_parity_manifest.json` plus
  `tools/ui_smoke/check_rmlui_parity_manifest.py`, giving Gate G3/G4 work an
  explicit checklist for document load, navigation, controller bindings,
  renderer coverage, screenshots/layout, input escape/back behavior, and
  guarded legacy fallback.
- The coordinator updated `tools/ui_smoke/report_rmlui_progress.py` so its
  controller-contract summary aggregates all discovered route metadata files,
  not only shell metadata.

## Accepted Baseline

- Smoke routes: `57` total, `57` required, `57` present, `0` pending.
- Waves: `A=21`, `B=11`, `C=25`.
- Migration phases: `starter=38`, `controller_stub=16`, `runtime_stub=3`.
- Phase progression: `19` advanced routes, `33.3%`.
- Route metadata files checked: `3` (`core`, `shell`, and `utility`).
- Controller-contract references: `54` references across `19` routes.
- Controller fixture coverage: `5` unique fixtures referenced and present.
- Navigation graph: `52` route-target references, `50` unique directed edges,
  `0` unknown targets, `44` dead-end routes, and `27` routes unreachable from
  guarded roots.
- Parity checklist: `57` routes, `9` categories, `0` `parity_ready` routes.
  Document-load evidence is complete for `57` routes; controller-binding
  evidence is complete for `19` metadata-advanced routes; guarded legacy
  fallback evidence is complete for the `3` `runtime_stub` routes; renderer,
  screenshot/layout, runtime navigation, and input parity remain pending.
- Runtime asset paths: `57` route documents plus `16` imported assets, for
  `73` derived runtime paths.

## Coordinator Validation

- `python tools\ui_smoke\check_rmlui_manifest.py`: passed.
- `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed.
- `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed.
- `python tools\ui_smoke\check_rmlui_controller_fixtures.py`: passed.
- `python tools\ui_smoke\check_rmlui_navigation_graph.py`: passed.
- `python tools\ui_smoke\check_rmlui_parity_manifest.py`: passed.
- `python tools\ui_smoke\check_rmlui_semantics.py`: passed.
- `python tools\ui_smoke\check_rmlui_menu_entrypoints.py`: passed.
- `python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py`: passed.
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`: passed.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --write-manifest .tmp\rmlui\round9-runtime-assets.json`:
  passed.
- `python tools\ui_smoke\report_rmlui_progress.py`: passed.
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`: passed.
- `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed.
- `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_report_rmlui_progress.py`:
  passed with `94` tests.
- `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round9-package-validation --base-game basew --archive-name pak0.pkz`:
  passed, packed `195` files, validated `31` botfile package/loose files, and
  validated `101` RmlUi package/loose files.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round9-package-validation --base-game basew`:
  passed with `73` staged loose runtime paths present.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round9-package-validation --base-game basew --format json`:
  passed with `ok=true`, `staged_loose_files.present=73`, and
  `staged_loose_imported_assets.present=16`.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round9-package-validation --base-game basew --write-manifest .tmp\rmlui\round9-runtime-assets-staged.json`:
  passed and wrote staged manifest entries against
  `.tmp/rmlui/round9-package-validation`.
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
  passed. The broader Windows Meson build remains subject to the existing
  `llvm-ar` regular-archive to thin-archive blocker recorded in earlier RmlUi
  rounds.

## Progression Notes

- `controller_stub` still means static controller/data-model metadata
  readiness and mock-fixture coverage, not live C++ controller execution.
- `runtime_stub` still means guarded route probing through the existing client
  scaffold with legacy fallback, not real RmlUi presentation ownership.
- The parity checklist intentionally records renderer, screenshot/layout,
  runtime navigation, and input evidence as pending until those checks exist.
