# RmlUi Parallel Round 4 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `FR-09-T05`,
`FR-09-T06`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`, `FR-03-T08`,
`DV-03-T07`, `DV-04-T02`, and `DV-07-T04`.

## Purpose

Integrate the fourth five-agent RmlUi migration pass. This round moved the
migration beyond source-document existence by adding a guarded client runtime
switch, explicit route progression metadata, first live-controller contract
fixtures, and route ownership/current-system mapping.

This is still a scaffold round. It does not add the real RmlUi dependency, a
native renderer bridge, live C++ data controllers, runtime document rendering,
parity evidence, or legacy JSON removal.

## Integrated Work

- Worker 1 added `src/client/ui_rml/ui_rml.cpp` and
  `src/client/ui_rml/ui_rml.h`, plus Meson source wiring. The scaffold
  registers `ui_rml_enable` and `ui_rml_debug`, maps `uiMenu_t` entry points to
  `main`, `game`, and `download_status`, and falls back to the current UI path
  until a real runtime returns ownership.
- Coordinator integration also wires the scaffold through
  `src/client/ui_bridge.cpp`, which is the active cgame UI bridge in the
  current build. The older `src/client/ui/ui.cpp` hook remains as a dormant
  legacy-path guard.
- Worker 2 added `migration_phase` smoke metadata and validation. The central
  smoke manifest now opts into required phase metadata and marks all 57 tracked
  routes as `starter`.
- Worker 3 added first controller contract fixtures for cvar binding, command
  actions, conditional state, route navigation/back behavior, and list/table
  data providers.
- Worker 4 added current-system route ownership metadata for core and shell
  manifests, including legacy/current surface names, source owners, task IDs,
  controller scope, and shared migration phases.
- Worker 5 prepared the docs/status shell. This coordinator pass replaces its
  placeholders with the accepted Round 4 evidence.

## Coordinator Adjustments

- `tools/ui_smoke/check_rmlui_route_contracts.py` now validates and reports the
  shared `migration_phase` values across route manifests.
- `tools/ui_smoke/test_check_rmlui_route_contracts.py` now covers valid and
  invalid migration-phase metadata.
- `assets/ui/rml/contracts/route-contract.schema.json` now documents
  `migration_phase` alongside the existing route fields.
- `docs-dev/plans/rmlui-ui-migration-roadmap.md` and the canonical strategic
  roadmap were updated to show Round 4 as validated scaffold progress while
  keeping the later runtime, renderer, controller, parity, and removal gates
  open.

## Validation

- `python tools\ui_smoke\check_rmlui_manifest.py`
  - Passed.
  - `57` total routes, `57` required, `57` present, `0` pending.
  - `Migration phases: starter=57`.
  - Parsed `151` RML/import files and checked `213` local `href` imports.
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
  - Passed.
  - Core: `1` route, `Migration phases: starter=1`.
  - Shell: `23` routes, `Migration phases: starter=23`.
  - Smoke: `57` routes, `Migration phases: starter=57`.
- `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py`
  - Passed: `27` tests.
- `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round4-package-validation --base-game basew --archive-name pak0.pkz`
  - Passed.
  - Packed `194` files from `assets`.
  - Validated `31` botfile package/loose files and `100` RmlUi package/loose
    files.
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`
  - Passed.
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_bridge.cpp.obj -v`
  - Passed.
- `meson compile -C builddir-win worr_engine_x86_64`
  - Blocked by an existing Windows archive-tool failure:
    `llvm-ar.exe: error: cannot convert a regular archive to a thin one` while
    linking static archives for curl, SDL3_ttf, and q2proto.
  - The touched RmlUi scaffold and bridge objects compile before this blocker.

## Remaining Work

- Select and integrate the actual RmlUi dependency.
- Replace the guarded stub with a real document runtime and renderer-backed
  render interface.
- Implement native OpenGL, Vulkan, and RTX/vkpt renderer support without
  redirecting Vulkan paths to OpenGL.
- Add live cvar, command, condition, navigation, list, preview, and session
  controllers.
- Add runtime navigation, screenshot/layout, input, accessibility, and session
  parity validation.
- Remove or intentionally archive legacy JSON/menu paths only after Gate G3/G4
  evidence is accepted.
