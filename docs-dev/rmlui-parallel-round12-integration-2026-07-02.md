# RmlUi Parallel Round 12 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T05`, `FR-09-T06`, `FR-09-T07`, `FR-09-T08`,
`FR-09-T09`, `FR-09-T10`, `FR-03-T08`, `DV-03-T07`, `DV-04-T02`, and
`DV-07-T04`.

## Summary

Round 12 accepted another static migration/progression slice for the RmlUi UI
migration. The round promoted exactly four local-session routes to
`controller_stub`: `downloads`, `quit_confirm`, `gameflags`, and `startserver`.
It also added starter route metadata coverage for the multiplayer hub plus all
tracked session/match documents, condition-expression inventory validation,
route-metadata sync validation, and progress-report summaries for those new
guardrails.

Accepted progression baseline:

- Route documents: `57/57` present.
- Migration phases: `starter=26`, `controller_stub=28`, `runtime_stub=3`.
- Advanced routes: `31` (`54.4%`).
- Controller contracts: `87` references across `31` advanced routes.
- Condition inventory: `141` static condition refs, `114` unique expressions,
  `111` unique tokens/cvars, and `0` malformed attributes.
- Metadata sync: `5` metadata files, `58` metadata routes, `57` matched
  central migration routes, `1` support metadata route (`core.runtime_smoke`),
  and `0` sync errors.
- Parity checklist: `57` routes, `9` categories, and `0` `parity_ready`
  routes.

This is still scaffold and validation work. It does not add the first-class
RmlUi dependency, native renderer output, live controllers, runtime navigation,
screenshot/layout parity, user-visible RmlUi behavior, or legacy JSON removal.

## Worker Results

### Worker 1: Local-Session Controller Stubs

Accepted files:

- `assets/ui/rml/shell/routes.json`
- `tools/ui_smoke/rmlui_manifest.json`
- `docs-dev/rmlui-agent1-local-session-controller-stubs-round12-2026-07-02.md`

The route metadata now marks `downloads`, `quit_confirm`, `gameflags`, and
`startserver` as `controller_stub`. The contracts cover static download-option
cvars, quit confirmation commands, dmflags metadata, local-session cvars,
condition-state expressions, command actions, and navigation. No live
download, quit, dmflags, start-server, runtime, screenshot, or parity behavior
is claimed.

### Worker 2: Multiplayer/Session Route Metadata

Accepted files:

- `assets/ui/rml/multiplayer/routes.json`
- `assets/ui/rml/session/routes.json`
- `docs-dev/rmlui-agent2-session-route-metadata-round12-2026-07-02.md`

The new metadata files cover `1` multiplayer route and `25` session/match
routes at `starter`. They add route ownership, source/current surface,
entrypoint, task, and data-model metadata without adding `controller_contracts`
or changing central smoke-manifest phases.

### Worker 3: Condition Inventory

Accepted files:

- `tools/ui_smoke/check_rmlui_condition_inventory.py`
- `tools/ui_smoke/test_check_rmlui_condition_inventory.py`
- `docs-dev/rmlui-agent3-condition-inventory-round12-2026-07-02.md`

The checker inventories `data-show-if`, `data-enable-if`, `data-visible-if`,
`data-enabled-if`, and `data-condition` hooks. Accepted output reports `141`
total condition refs, `22` routes with condition hooks, `114` unique condition
expressions, `111` unique tokens/cvars, and `0` malformed attributes.

### Worker 4: Metadata Sync

Accepted files:

- `tools/ui_smoke/check_rmlui_metadata_sync.py`
- `tools/ui_smoke/test_check_rmlui_metadata_sync.py`
- `docs-dev/rmlui-agent4-metadata-sync-round12-2026-07-02.md`

The checker compares discovered feature route metadata with the central smoke
manifest. The coordinator updated it to treat `core.runtime_smoke` as an
explicit support metadata route rather than a central migration-route drift,
preserving the `57`-route migration baseline.

### Worker 5: Progress Guardrail Summaries

Accepted files:

- `tools/ui_smoke/report_rmlui_progress.py`
- `tools/ui_smoke/test_report_rmlui_progress.py`
- `docs-dev/rmlui-agent5-progress-guardrails-round12-2026-07-02.md`

Progress reports now include condition-inventory and metadata-sync facts in
text, markdown, and JSON output. Existing phase, controller, parity, command,
cvar, and data-model summaries remain intact.

## Coordinator Validation

Passed:

- `python tools\ui_smoke\check_rmlui_manifest.py`
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
- `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`
- `python tools\ui_smoke\check_rmlui_controller_fixtures.py`
- `python tools\ui_smoke\check_rmlui_parity_manifest.py`
- `python tools\ui_smoke\check_rmlui_semantics.py`
- `python tools\ui_smoke\check_rmlui_menu_entrypoints.py`
- `python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py`
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports`
- `python tools\ui_smoke\check_rmlui_command_inventory.py`
- `python tools\ui_smoke\check_rmlui_cvar_inventory.py`
- `python tools\ui_smoke\check_rmlui_data_model_inventory.py`
- `python tools\ui_smoke\check_rmlui_condition_inventory.py`
- `python tools\ui_smoke\check_rmlui_condition_inventory.py --format json`
- `python tools\ui_smoke\check_rmlui_metadata_sync.py`
- `python tools\ui_smoke\check_rmlui_metadata_sync.py --format json`
- `python tools\ui_smoke\check_rmlui_phase_consistency.py`
- `python tools\ui_smoke\check_rmlui_dependency_decision.py`
- `python tools\ui_smoke\report_rmlui_progress.py`
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`
- `python tools\ui_smoke\report_rmlui_progress.py --format json`
- `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_data_model_inventory.py tools/ui_smoke/test_check_rmlui_condition_inventory.py tools/ui_smoke/test_check_rmlui_metadata_sync.py tools/ui_smoke/test_check_rmlui_phase_consistency.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_report_rmlui_progress.py`
  (`144` tests)
- `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round12-package-validation --base-game basew --archive-name pak0.pkz`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round12-package-validation --base-game basew`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round12-package-validation --base-game basew --format json`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round12-package-validation --base-game basew --write-manifest .tmp\rmlui\round12-runtime-assets-staged.json`
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`

Packaging packed `197` files from `assets`, validated `31` botfile
package/loose files, validated `103` RmlUi package/loose files, and staged
`73` loose RmlUi runtime paths including all `16` imported assets. The compile
passed with the existing Ninja `premature end of file; recovering` warning.

## Remaining Gates

- Gate G1 is still open for the real RmlUi dependency, runtime bootstrap,
  native renderer draw proof, input/file/font/system bridges, and `.install`
  refresh proof against the live runtime.
- Gate G2 is still open for live cvar, command, condition, list/table,
  keybind, preview, save/load, download, dmflags, and local-session
  controllers.
- Gate G3 is still open for RmlUi runtime activation of the translated
  surfaces and functional parity of settings, browser, demos, player config,
  save/load, downloads, local-session, and session flows.
- Gate G4 is still open for renderer/screenshot/input/layout parity evidence
  and legacy JSON removal.
