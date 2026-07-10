# RmlUi Parallel Round 7 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `FR-09-T04`,
`FR-09-T05`, `FR-09-T06`, `FR-09-T09`, `FR-03-T08`, `DV-03-T07`,
`DV-04-T02`, and `DV-07-T04`.

## Purpose

Integrate the seventh five-agent RmlUi migration pass. This round turns two
coordinator-only checks into reusable smoke tools, extends runtime asset
validation to include imported RML/RCSS assets, promotes the remaining
low-risk settings routes to `controller_stub`, and adds controller-contract
facts to progress reporting.

This remains scaffold and validation work. It does not add the real RmlUi
dependency, a native renderer bridge, live C++ data controllers, runtime
document rendering, screenshot/layout capture, parity evidence, or legacy
JSON/menu removal.

## Integrated Work

- Worker 1 added `tools/ui_smoke/check_rmlui_runtime_registry.py`, which parses
  `src/client/ui_rml/ui_rml.cpp` and verifies that `ui_rml_routes` covers all
  smoke-manifest route IDs, allows only `core.runtime_smoke` as the extra
  route, rejects duplicate/unexpected IDs, and checks runtime document path
  drift.
- Worker 2 added `tools/ui_smoke/check_rmlui_controller_stub_coverage.py`,
  which connects `controller_stub` phase claims to shell route
  `controller_contracts` and static RML attributes for navigation, command,
  cvar, and conditional coverage.
- Worker 3 promoted exactly five additional settings routes to
  `controller_stub`: `multimonitor`, `railtrail`, `effects`, `crosshair`, and
  `language`. These join the Round 5 and Round 6 batches, giving
  `controller_stub=15`.
- Worker 4 extended `tools/ui_smoke/check_rmlui_runtime_assets.py` with
  `--include-imports`, which follows local RML `<link href>` imports for
  `.rml` and `.rcss` assets and validates their runtime/staged loose paths.
- Worker 5 extended `tools/ui_smoke/report_rmlui_progress.py` with
  controller-contract facts in text, markdown, and JSON output.

## Validation

- `python tools\ui_smoke\check_rmlui_manifest.py`
  - Passed.
  - `57` total routes, `57` required, `57` present, `0` pending.
  - Waves: `A=21`, `B=11`, `C=25`.
  - Migration phases: `starter=42`, `controller_stub=15`.
  - Parsed `151` RML/import files and checked `213` local `href` imports.
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
  - Passed.
  - Core: `1` route, `0` controller contract references,
    `Migration phases: starter=1`.
  - Shell: `23` routes, `44` controller contract references,
    `Migration phases: controller_stub=15, starter=8`.
  - Smoke: `57` routes, `0` controller contract references,
    `Migration phases: controller_stub=15, starter=42`.
- `python tools\ui_smoke\check_rmlui_semantics.py`
  - Passed.
  - `57` routes known, `57` documents checked, `52` route targets checked,
    `289` command elements checked, and `452` cvar references checked.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports`
  - Passed.
  - `57` routes checked.
  - `57` source route documents present.
  - `16` imported assets discovered and present.
  - `73` runtime paths derived: `57` route documents and `16` imported assets.
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - Passed.
  - `57` manifest routes, `58` registered routes, `0` missing, `0`
    unexpected, `0` duplicates, and `57` matched runtime paths.
- `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`
  - Passed.
  - `15` `controller_stub` routes checked.
  - Inferred and covered categories:
    `navigation=5`, `command_action=15`, `cvar_binding=12`,
    `condition_state=3`.
  - Missing categories: none.
- `python tools\ui_smoke\report_rmlui_progress.py`
  - Passed with migration phases `starter=42`, `controller_stub=15`.
  - Controller contracts: `44` references across `15` shell routes.
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`
  - Passed with the same route and controller-contract counts.
- `python tools\ui_smoke\report_rmlui_progress.py --format json`
  - Passed with `57/57` documents present, migration phases `starter=42` and
    `controller_stub=15`, plus controller-contract facts:
    `44` total references, `15` routes with contracts,
    `command_action=15`, `condition_state=3`, `cvar_binding=12`,
    `navigation=14`, and route phase coverage `controller_stub=15`.
- `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_report_rmlui_progress.py`
  - Passed: `58` tests.
- `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round7-package-validation --base-game basew --archive-name pak0.pkz`
  - Passed.
  - Packed `194` files from `assets`.
  - Validated `31` botfile package/loose files and `100` RmlUi package/loose
    files.
  - Mirrored loose asset paths: `botfiles`, `ui/rml`.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round7-package-validation --base-game basew`
  - Passed with `73` staged loose route/import assets present and `0` missing.
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
