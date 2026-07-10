# RmlUi Parallel Round 10 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `FR-09-T05`,
`FR-09-T07`, `FR-09-T09`, `FR-09-T10`, `FR-03-T08`, `DV-03-T07`,
`DV-04-T02`, `DV-06-T01`, and `DV-07-T04`

## Summary

Round 10 is accepted as a static validation/progression round. It adds command
inventory reporting, cvar inventory reporting, utility/list controller-stub
metadata, parity checklist summaries in progress reporting, and a proposed
RmlUi dependency decision record.

Accepted migration baseline:

- `starter=34`
- `controller_stub=20`
- `runtime_stub=3`
- Advanced routes: `23` (`40.4%`)
- Controller-contract refs: `65` across `23` metadata-advanced routes
- Parity-ready routes: `0`

This round does not add a first-class RmlUi dependency, does not change Meson
dependency wiring, does not implement native renderer output, does not add live
C++ controllers, does not prove native runtime navigation, does not add
screenshots or player-visible parity evidence, and does not remove legacy JSON.

## Worker Results

- Worker 1:
  `docs-dev/rmlui-agent1-command-inventory-round10-2026-07-02.md`
  added `tools/ui_smoke/check_rmlui_command_inventory.py` and focused tests.
  Live baseline: `289` direct command refs, `15` cvar-command refs, `70`
  unique command tokens, all `57` routes with command hooks, and `0`
  malformed command attributes.
- Worker 2:
  `docs-dev/rmlui-agent2-cvar-inventory-round10-2026-07-02.md` added
  `tools/ui_smoke/check_rmlui_cvar_inventory.py` and focused tests. Live
  baseline: `233` direct cvar refs, `54` label refs, `15` command refs,
  `150` condition refs, `452` total refs, `272` unique cvars, `37` routes
  with cvar hooks, and `0` unknown/bad tokens.
- Worker 3:
  `docs-dev/rmlui-agent3-utility-list-controller-stubs-round10-2026-07-02.md`
  promoted exactly `servers`, `demos`, `players`, and `ui_list` to
  `controller_stub` and expanded utility route metadata to `8` routes and
  `21` utility controller-contract references.
- Worker 4:
  `docs-dev/rmlui-agent4-progress-inventory-round10-2026-07-02.md` and
  `docs-dev/rmlui-agent4-progress-parity-round10-2026-07-02.md` extended
  `tools/ui_smoke/report_rmlui_progress.py` so text, markdown, and JSON
  progress reports include command inventory, cvar inventory, and parity
  checklist category summaries.
- Worker 5:
  `docs-dev/rmlui-agent5-dependency-decision-round10-2026-07-02.md` added
  `docs-dev/rmlui-dependency-decision-record-2026-07-02.md`, a planning-only
  dependency decision record for future first-class RmlUi integration.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_command_inventory.py`
  - Passed: `5` tests
- `python -m pytest tools/ui_smoke/test_check_rmlui_cvar_inventory.py`
  - Passed: `6` tests
- `python -m pytest tools/ui_smoke/test_report_rmlui_progress.py`
  - Passed: `11` tests
- `python tools\ui_smoke\check_rmlui_command_inventory.py`
  - Passed with `289` direct command refs and `0` malformed command attributes
- `python tools\ui_smoke\check_rmlui_command_inventory.py --format json`
  - Passed with `ok=true`
- `python tools\ui_smoke\check_rmlui_cvar_inventory.py`
  - Passed with `452` total cvar refs and `0` unknown/bad tokens
- `python tools\ui_smoke\check_rmlui_cvar_inventory.py --format json`
  - Passed with `ok=true`
- `python tools\ui_smoke\check_rmlui_manifest.py`
  - Passed with `57` routes and phases `starter=34`,
    `controller_stub=20`, `runtime_stub=3`
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
  - Passed with core, shell, smoke, and utility metadata checked
- `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`
  - Passed with `20` controller-stub routes and no missing categories
- `python tools\ui_smoke\check_rmlui_controller_fixtures.py`
  - Passed with `65` refs across `23` routes and `7` fixtures present
- `python tools\ui_smoke\check_rmlui_navigation_graph.py`
  - Passed with `0` unknown targets
- `python tools\ui_smoke\check_rmlui_parity_manifest.py`
  - Passed with `57` routes, `9` categories, and `0` parity-ready routes
- `python tools\ui_smoke\check_rmlui_semantics.py`
  - Passed with `57` documents, `52` route targets, `289` command elements,
    and `452` cvar refs checked
- `python tools\ui_smoke\check_rmlui_menu_entrypoints.py`
  - Passed with `3` unique mapped route IDs
- `python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py`
  - Passed with `3` runtime-stub routes checked
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - Passed with `57` manifest routes matched to runtime paths
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports`
  - Passed with `57` source documents, `16` imports, and `73` runtime paths
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --format json`
  - Passed with `ok=true`
- `python tools\ui_smoke\report_rmlui_progress.py`
  - Passed and emitted command inventory, cvar inventory, and parity checklist
    summaries
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`
  - Passed and emitted command inventory, cvar inventory, and parity checklist
    table rows
- `python tools\ui_smoke\report_rmlui_progress.py --format json`
  - Passed and emitted `command_inventory`, `cvar_inventory`, and
    `parity_checklist` objects
- `python tools\ui_smoke\report_rmlui_progress.py --no-parity-summary --format json`
  - Passed and omitted the optional parity block
- `python tools\ui_smoke\report_rmlui_progress.py --no-inventory-summary --format json`
  - Passed and omitted the optional command/cvar inventory blocks
- `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_report_rmlui_progress.py`
  - Passed: `108` tests
- `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round10-package-validation --base-game basew --archive-name pak0.pkz`
  - Passed: packed `195` files, validated `31` botfile package/loose files,
    and validated `101` RmlUi package/loose files
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round10-package-validation --base-game basew`
  - Passed with `73` staged loose route/import assets present
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round10-package-validation --base-game basew --format json`
  - Passed with `ok=true`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round10-package-validation --base-game basew --write-manifest .tmp\rmlui\round10-runtime-assets.json`
  - Passed and wrote staged runtime asset details
- `rg -n "FR-09-T02|FR-09-T03|DV-06-T01|DV-03-T07|DV-07-T04|Non-Goals|No RmlUi dependency is added|No Meson|No runtime switch|No screenshot|No legacy JSON" docs-dev\rmlui-dependency-decision-record-2026-07-02.md docs-dev\rmlui-agent5-dependency-decision-round10-2026-07-02.md`
  - Passed with expected task/boundary matches
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`
  - Passed, with the existing `ninja: warning: premature end of file;
    recovering` warning

The broader Windows Meson target remains subject to the existing `llvm-ar`
regular-archive to thin-archive blocker recorded in earlier RmlUi rounds.
