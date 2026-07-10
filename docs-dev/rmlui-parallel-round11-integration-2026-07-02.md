# RmlUi Parallel Round 11 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T05`, `FR-09-T06`, `FR-09-T07`,
`FR-09-T09`, `FR-09-T10`, `FR-03-T08`, `DV-03-T07`, `DV-04-T02`,
`DV-06-T01`, and `DV-07-T04`.

## Summary

Round 11 accepted another static migration/progression slice for the RmlUi UI
migration. The round promoted exactly four single-player/save-load routes to
`controller_stub`: `singleplayer`, `skill_select`, `loadgame`, and `savegame`.
It also added static data-model inventory reporting, phase-consistency
guardrails, dependency-decision validation, and progress-report data-model
summaries.

Accepted progression baseline:

- Route documents: `57/57` present.
- Migration phases: `starter=30`, `controller_stub=24`, `runtime_stub=3`.
- Advanced routes: `27` (`47.4%`).
- Controller contracts: `75` references across `27` advanced routes.
- Data-model inventory: `190` static model/data-binding refs, `187` unique
  model tokens, `38` routes with hooks, and `0` malformed tokens.
- Parity checklist: `57` routes, `9` categories, and `0` `parity_ready`
  routes.

This is still scaffold and validation work. It does not add the first-class
RmlUi dependency, native renderer output, live controllers, runtime navigation,
screenshot/layout parity, user-visible RmlUi behavior, or legacy JSON removal.

## Worker Results

### Worker 1: Data-Model Inventory

Accepted files:

- `tools/ui_smoke/check_rmlui_data_model_inventory.py`
- `tools/ui_smoke/test_check_rmlui_data_model_inventory.py`
- `docs-dev/rmlui-agent1-data-model-inventory-round11-2026-07-02.md`

The checker inventories `data-model`, `data-bind`, `data-options`,
`data-component`, `data-controller`, `data-action-type`, `data-slot`,
`data-bind-command`, and `data-bind-group` hooks. Accepted output reports
`57` routes checked, `190` total model/data-binding refs, `187` unique model
tokens, and `0` malformed tokens.

### Worker 2: Single-Player Controller Stubs

Accepted files:

- `assets/ui/rml/shell/routes.json`
- `tools/ui_smoke/rmlui_manifest.json`
- `docs-dev/rmlui-agent2-singleplayer-controller-stubs-round11-2026-07-02.md`

The route metadata now marks `singleplayer`, `skill_select`, `loadgame`, and
`savegame` as `controller_stub`. The contracts cover static single-player
episode/unit selection, command actions, navigation, condition-state
availability, and save/load slot behavior. No live save/load or campaign
controller behavior is claimed.

### Worker 3: Phase Consistency

Accepted files:

- `tools/ui_smoke/check_rmlui_phase_consistency.py`
- `tools/ui_smoke/test_check_rmlui_phase_consistency.py`
- `docs-dev/rmlui-agent3-phase-consistency-round11-2026-07-02.md`

The checker validates that advanced route phases have metadata and
controller-contract evidence, `runtime_stub` routes remain backed by guarded
runtime menu mappings, and `parity_ready` is not overclaimed while parity
categories remain incomplete.

### Worker 4: Dependency Decision Validation

Accepted files:

- `tools/ui_smoke/check_rmlui_dependency_decision.py`
- `tools/ui_smoke/test_check_rmlui_dependency_decision.py`
- `docs-dev/rmlui-agent4-dependency-decision-check-round11-2026-07-02.md`

The checker validates
`docs-dev/rmlui-dependency-decision-record-2026-07-02.md` as a proposed,
not-implemented decision record. It requires task IDs, no-go wording for
Meson/build/runtime/source dependency changes, native OpenGL/Vulkan/RTX-vkpt
obligations, Gate G1 interface areas, and validation/staging evidence.

### Worker 5: Progress Report Data-Model Summary

Accepted files:

- `tools/ui_smoke/report_rmlui_progress.py`
- `tools/ui_smoke/test_report_rmlui_progress.py`
- `docs-dev/rmlui-agent5-progress-data-model-round11-2026-07-02.md`

Progress reports now include data-model inventory facts in text, markdown, and
JSON output when the new checker is available. Existing command, cvar, parity,
phase, and controller summaries remain intact.

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
- `python tools\ui_smoke\check_rmlui_data_model_inventory.py --format json`
- `python tools\ui_smoke\check_rmlui_phase_consistency.py`
- `python tools\ui_smoke\check_rmlui_phase_consistency.py --format json`
- `python tools\ui_smoke\check_rmlui_dependency_decision.py`
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`
- `python tools\ui_smoke\report_rmlui_progress.py`
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`
- `python tools\ui_smoke\report_rmlui_progress.py --format json`
- `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_data_model_inventory.py tools/ui_smoke/test_check_rmlui_phase_consistency.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_report_rmlui_progress.py`
  (`127` tests)
- `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round11-package-validation --base-game basew --archive-name pak0.pkz`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round11-package-validation --base-game basew`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round11-package-validation --base-game basew --format json`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round11-package-validation --base-game basew --write-manifest .tmp\rmlui\round11-runtime-assets-staged.json`
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`

Packaging packed `195` files from `assets`, validated `31` botfile
package/loose files, validated `101` RmlUi package/loose files, and staged
`73` loose RmlUi runtime paths including all `16` imported assets. The compile
passed with the existing Ninja `premature end of file; recovering` warning.

## Remaining Gates

- Gate G1 is still open for the real RmlUi dependency, runtime bootstrap,
  native renderer draw proof, input/file/font/system bridges, and `.install`
  refresh proof against the live runtime.
- Gate G2 is still open for live cvar, command, condition, list/table,
  keybind, preview, and save/load controllers.
- Gate G3 is still open for RmlUi runtime activation of the translated
  surfaces and functional parity of settings, browser, demos, player config,
  save/load, and session flows.
- Gate G4 is still open for renderer/screenshot/input/layout parity evidence
  and legacy JSON removal.
