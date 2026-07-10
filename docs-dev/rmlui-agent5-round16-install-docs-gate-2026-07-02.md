# RmlUi Agent 5 Round 16 Install Docs Gate Audit

Date: 2026-07-02

Worker lane: Worker 5, package/install and docs-status support

Task IDs: `FR-09-T02`, `FR-09-T05`, `FR-09-T09`, `FR-09-T10`,
`DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 16 should document a route-phase bookkeeping closeout, not a new package,
runtime, or build-gate expansion. The final twelve central starter routes are
expected to move to `controller_stub`, which should make every one of the 57
central RmlUi routes advanced while keeping `parity_ready=0`.

This audit did not edit route metadata, the roadmap/proposal documents,
`q2proto/`, renderer code, package scripts, or build files. The live repository
was still at the Round 15 accepted baseline when the read-only checks below
were run.

## Final Starter Routes To Retire

Round 16 should document promotion of these central manifest routes from
`starter` to `controller_stub`:

- `admin_commands`
- `admin_menu`
- `forfeit_confirm`
- `leave_match_confirm`
- `map_selector`
- `match_stats`
- `mymap_flags`
- `mymap_main`
- `tourney_info`
- `tourney_mapchoices`
- `tourney_replay_confirm`
- `tourney_veto`

## Expected Round 16 Phase Counts

After the route metadata and central smoke manifest updates land, the canonical
57-route reports should show:

- `starter=0`
- `controller_stub=54`
- `runtime_stub=3`
- `parity_pending=0`
- `parity_ready=0`
- `advanced_routes=57`
- `advanced_percent=100.0`
- Required documents: `57/57`
- Missing documents: `0`

The `routes_by_phase` section in `report_rmlui_progress.py --format json`
should have an empty `starter` route list, the twelve retired starter routes in
the `controller_stub` route list, and the existing `runtime_stub` routes
unchanged: `download_status`, `game`, and `main`.

The parity checklist should remain closed for cutover:

- `parity_ready_routes=0`
- `controller_bindings` should become `complete=57` and `pending=0`
- `navigation`, renderer, screenshot, and input evidence should remain pending
  until live runtime parity work lands
- `legacy_fallback` should remain pending for the 54 `controller_stub` routes
  and complete only for the 3 guarded `runtime_stub` routes

The final twelve starter routes currently have zero `controller_contracts` in
the Round 15 baseline. Their Round 16 promotion should therefore document both
the phase change and the controller-contract metadata that makes
`controller_stub` coverage meaningful. A phase-only edit would leave the
install gate unchanged, but it should not be accepted as a completed
controller-stub promotion.

## Metadata Shape Caveat

The central manifest has 57 routes, but route metadata currently contains one
support-only record, `core.runtime_smoke`, in
`assets/ui/rml/core/routes.json`. Because that support route is not part of the
central smoke manifest, `check_rmlui_route_metadata_shape.py --format json`
currently reports 58 metadata routes and `starter=13` while the central manifest
reports `starter=12`.

After Round 16, the expected metadata-shape phase count is therefore:

- central manifest and progress reports: `starter=0`, `controller_stub=54`,
  `runtime_stub=3`
- route metadata shape report: `starter=1`, `controller_stub=54`,
  `runtime_stub=3`, with the one remaining starter being support metadata
  route `core.runtime_smoke`

That `starter=1` metadata-shape result is not a Round 16 failure as long as
`check_rmlui_metadata_sync.py` still reports `support_metadata_routes=1` for
`core.runtime_smoke`, `phase_mismatch_count=0`, and
`central_routes_without_feature_metadata=0`.

## Package Runtime Build Gate Audit

No package, runtime-asset, dependency, or build command needs to change solely
because the final twelve routes advance from `starter` to `controller_stub`.

Package checks should remain unchanged:

- `tools/package_assets.py` should still stage `ui/rml` as a default loose
  mirror beside `botfiles`.
- The package step should still validate archive members and loose mirrors by
  SHA-256.
- If Round 16 only changes route metadata/controller contracts and adds no new
  files/imports, the expected package counts remain `197` packaged assets, `31`
  botfile package/loose assets, and `103` RmlUi package/loose assets.

Runtime asset checks should remain unchanged:

- Source validation should still check `57` route documents.
- Import-aware validation should still discover `16` imported assets.
- Runtime path coverage should remain `73` total paths: `57` route documents
  plus `16` imported assets.
- Staged validation should still require all `73` runtime paths under the
  package install root.
- The sidecar manifest path should advance to a Round 16 scratch output, for
  example `.tmp/rmlui/round16-runtime-assets-staged.json`.

Dependency/build checks should remain unchanged:

- `subprojects/rmlui.wrap` remains the selected source acquisition path.
- The Meson `rmlui` feature remains default-disabled unless the coordinator is
  intentionally probing `-Drmlui=auto`.
- `UI_RML_HAS_RUNTIME=1` should remain guarded behind a resolved optional RmlUi
  dependency.
- The accepted dependency-integration state remains `optional` with `4/4`
  components present and the scaffold at `compiled-stub` until the real runtime
  is implemented.
- Vulkan, RTX/vkpt, and OpenGL proof remains native-renderer work for later
  gates; no Vulkan route may be redirected to OpenGL.

## Read-Only Checks Run

These checks were run against the live Round 15 baseline to verify the audit
starting point.

```powershell
git status --short
```

Result: the worktree already contained concurrent RmlUi modifications and
untracked RmlUi docs/assets before this audit. This worker did not revert or
edit those files.

```powershell
python tools\ui_smoke\report_rmlui_progress.py --format json
```

Result: `total_routes=57`, required documents `57/57`, migration phases
`starter=12`, `controller_stub=42`, `runtime_stub=3`,
`parity_pending=0`, `parity_ready=0`, `advanced_routes=45`,
`advanced_percent=78.9`, and `parity_ready_routes=0`.

```powershell
python tools\ui_smoke\check_rmlui_phase_consistency.py --format json
```

Result: `ok=true`, `routes_checked=57`, `route_metadata_files_checked=5`,
phase counts `starter=12`, `controller_stub=42`, `runtime_stub=3`,
`parity_ready=0`, `metadata_backed_advanced_routes=45`,
`runtime_stub_routes=3`, `runtime_menu_mapped_routes=3`, and `errors=[]`.

```powershell
python tools\ui_smoke\check_rmlui_route_metadata_shape.py --format json
```

Result: `ok=true`, `metadata_files=5`, `metadata_routes=58`,
`controller_contract_refs=117`, `routes_with_controller_contracts=45`, and
metadata phase counts `starter=13`, `controller_stub=42`, `runtime_stub=3`.

```powershell
python tools\ui_smoke\check_rmlui_metadata_sync.py --format json
```

Result: `ok=true`, `central_route_count=57`, `matched_route_count=57`,
`metadata_route_count=58`, `support_metadata_routes=1` with
`core.runtime_smoke`, `phase_mismatch_count=0`, `document_mismatch_count=0`,
and `duplicate_count=0`.

```powershell
python tools\ui_smoke\check_rmlui_manifest.py
```

Result: passed with `Routes: 57 total, 57 required, 57 present, 0 pending`,
waves `A=21, B=11, C=25`, migration phases `starter=12`,
`controller_stub=42`, `runtime_stub=3`, required documents `57/57`,
and `RML parsed: 151 files, href imports checked: 213`.

```powershell
python tools\ui_smoke\check_rmlui_controller_stub_coverage.py
```

Result: passed with `route metadata files checked: 5`,
`controller_stub routes checked: 42`, inferred and covered categories
`navigation=9`, `command_action=42`, `cvar_binding=21`,
`condition_state=13`, `keybind=3`, and `missing categories: none`.

```powershell
python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py
```

Result: passed with `Runtime_stub routes checked: 3`, `Menu-mapped routes: 3`,
`Registry matches: 3`, and `Controller contract matches: 3`.

```powershell
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --format json
```

Result: `ok=true`, `routes_checked=57`, source documents `present=57`,
`missing=0`, imported assets `discovered=16`, `present=16`, `missing=0`,
runtime paths `route_documents=57`, `imported_assets=16`, `total=73`, and
`staging_requested=false`.

```powershell
python tools\ui_smoke\check_rmlui_parity_manifest.py --format json
```

Result: `ok=true`, `routes_checked=57`, `categories_checked=9`, phase counts
`starter=12`, `controller_stub=42`, `runtime_stub=3`, `parity_ready=0`,
pending evidence included `controller_bindings=12`, `navigation=57`, all
renderer categories `57`, `screenshot_layout=57`, `input_escape_back=57`, and
`legacy_fallback=54`.

```powershell
python tools\ui_smoke\check_rmlui_legacy_removal.py --format json
```

Result: `ok=true`, `items_checked=6`, status counts `blocked=4`,
`pending=2`, `ready=0`, `complete=0`, no missing task IDs, and the parity gate
remained closed because `parity_ready_routes=0` and required parity evidence is
still incomplete.

```powershell
python tools\ui_smoke\check_rmlui_dependency_integration.py --format json
```

Result: `ok=true`, `state=optional`, `components_present=4`,
`components_total=4`, `meson_options=1`, `wrap_files=1`, `source_dirs=0`,
`meson_declarations=2`, `compile_defines=1`, `runtime_compiled=false`, and
scaffold status `compiled-stub`.

```powershell
python tools\ui_smoke\check_rmlui_dependency_decision.py
```

Result: passed with required task IDs `5/5`, decision status
`proposed; Round 15 source acquisition and optional build gate landed; runtime
not implemented.`, no status overclaims, native renderer obligations `4/4`,
Gate G1 interfaces `5/5`, and validation evidence `3/3`.

## Coordinator Validation Commands

After the Round 16 route metadata/controller-stub work lands, the coordinator
should run the same validation surface as Round 15 plus the phase-sensitive
route checks below.

```powershell
python tools\ui_smoke\check_rmlui_dependency_integration.py
python tools\ui_smoke\check_rmlui_dependency_integration.py --format json
python tools\ui_smoke\check_rmlui_manifest.py
python tools\ui_smoke\check_rmlui_route_contracts.py
python tools\ui_smoke\check_rmlui_semantics.py
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports
python tools\ui_smoke\check_rmlui_runtime_registry.py
python tools\ui_smoke\check_rmlui_controller_stub_coverage.py
python tools\ui_smoke\check_rmlui_menu_entrypoints.py
python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py
python tools\ui_smoke\check_rmlui_navigation_graph.py
python tools\ui_smoke\check_rmlui_controller_fixtures.py
python tools\ui_smoke\check_rmlui_parity_manifest.py
python tools\ui_smoke\check_rmlui_command_inventory.py
python tools\ui_smoke\check_rmlui_cvar_inventory.py
python tools\ui_smoke\check_rmlui_data_model_inventory.py
python tools\ui_smoke\check_rmlui_condition_inventory.py
python tools\ui_smoke\check_rmlui_event_inventory.py
python tools\ui_smoke\check_rmlui_a11y_inventory.py
python tools\ui_smoke\check_rmlui_document_id_inventory.py
python tools\ui_smoke\check_rmlui_entrypoint_inventory.py
python tools\ui_smoke\check_rmlui_metadata_sync.py
python tools\ui_smoke\check_rmlui_route_metadata_shape.py
python tools\ui_smoke\check_rmlui_phase_consistency.py
python tools\ui_smoke\check_rmlui_dependency_decision.py
python tools\ui_smoke\check_rmlui_legacy_removal.py
python tools\ui_smoke\report_rmlui_progress.py
python tools\ui_smoke\report_rmlui_progress.py --format markdown
python tools\ui_smoke\report_rmlui_progress.py --format json
```

The key expected Round 16 results from these commands are:

- `check_rmlui_manifest.py`: migration phases `starter=0`,
  `controller_stub=54`, `runtime_stub=3`
- `check_rmlui_controller_stub_coverage.py`: `controller_stub routes checked:
  54` and no missing categories
- `check_rmlui_route_metadata_shape.py`: `routes_with_controller_contracts=57`
  for the central route set, while retaining support metadata route
  `core.runtime_smoke`
- `check_rmlui_runtime_stub_eligibility.py`: still `Runtime_stub routes
  checked: 3`
- `check_rmlui_parity_manifest.py`: `parity_ready_routes=0`, with
  `controller_bindings` no longer pending
- `check_rmlui_metadata_sync.py`: `phase_mismatch_count=0`,
  `central_routes_without_feature_metadata=0`, and support route
  `core.runtime_smoke` still isolated from the central route count
- `report_rmlui_progress.py --format json`: `advanced_routes=57`,
  `advanced_percent=100.0`, `routes_by_phase.starter=[]`

Focused test command:

```powershell
python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_data_model_inventory.py tools/ui_smoke/test_check_rmlui_condition_inventory.py tools/ui_smoke/test_check_rmlui_event_inventory.py tools/ui_smoke/test_check_rmlui_a11y_inventory.py tools/ui_smoke/test_check_rmlui_document_id_inventory.py tools/ui_smoke/test_check_rmlui_entrypoint_inventory.py tools/ui_smoke/test_check_rmlui_metadata_sync.py tools/ui_smoke/test_check_rmlui_route_metadata_shape.py tools/ui_smoke/test_check_rmlui_phase_consistency.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_legacy_removal.py tools/ui_smoke/test_report_rmlui_progress.py
```

Package and staged-runtime commands:

```powershell
python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round16-package-validation --base-game basew --archive-name pak0.pkz
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round16-package-validation --base-game basew
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round16-package-validation --base-game basew --format json
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round16-package-validation --base-game basew --write-manifest .tmp\rmlui\round16-runtime-assets-staged.json
```

If Round 16 changes only metadata/controller contracts, the expected staged
runtime counts remain `57` route documents, `16` imported assets, and `73`
staged loose runtime paths.

Build/dependency commands:

```powershell
meson setup builddir-win --reconfigure
meson setup builddir-win --reconfigure -Drmlui=auto
meson setup builddir-win --reconfigure -Drmlui=disabled
ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v
meson compile -C builddir-win
ninja -C builddir-win -n
```

The coordinator should leave the Windows builddir on `rmlui=disabled` after
the validation pass unless that build tree is being intentionally held for
dependency probing.

## Caveats

This audit confirms expected documentation and install-gate behavior only. It
does not prove live RmlUi runtime loading, controller execution, renderer
integration, input behavior, screenshot/layout parity, Vulkan or RTX/vkpt
parity, or legacy JSON removal.
