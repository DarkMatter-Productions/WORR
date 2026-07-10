# RmlUi Parallel Round 14 Integration

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, `FR-09-T10`, `FR-07-T01`,
`FR-07-T02`, `DV-03-T07`, `DV-04-T02`, and `DV-07-T04`.

## Summary

Round 14 accepted another static migration/progression slice. It promoted the
multiplayer/lobby/info session batch to `controller_stub`, added document/body
identity and route entrypoint inventories, added stricter route metadata shape
validation, and wired legacy-removal gate status into the RmlUi progress
reporter.

Accepted baseline:
- Route documents: `57/57` present.
- Central migration phases: `starter=12`, `controller_stub=42`,
  `runtime_stub=3`.
- Advanced routes: `45/57` (`78.9%`).
- Controller contracts: `117` refs across `45` routes.
- Document identity: `57` body IDs, `57` unique body IDs, `57` matched
  metadata/body IDs, `0` mismatches.
- Entrypoints: `72` total refs, `72` unique refs, `0` malformed refs.
- Route metadata shape: `58` metadata routes, `45` routes with controller
  contracts, `117` controller-contract refs, `0` malformed routes.
- Legacy-removal progress: `6` items, `blocked=4`, `pending=2`, `ready=0`,
  `complete=0`, parity gate closed.
- Parity state: `0` `parity_ready` routes.

This remains static source, metadata, and guardrail work only. No first-class
RmlUi dependency, native renderer bridge, live controller behavior, runtime
navigation, screenshot/layout evidence, parity proof, or legacy JSON removal is
claimed.

## Worker Results

- Agent 1 promoted exactly `multiplayer`, `dm_welcome`, `dm_join`, `join`,
  `dm_hostinfo`, and `dm_matchinfo` to `controller_stub` in the central smoke
  manifest plus multiplayer/session metadata. The six promoted routes add
  `16` static controller-contract refs and keep legacy fallback authoritative.
- Agent 2 added `tools/ui_smoke/check_rmlui_document_id_inventory.py` and
  tests. The checker verifies central route body IDs, body `data-route-id`
  values, and feature metadata `document_id` values.
- Agent 3 added `tools/ui_smoke/check_rmlui_entrypoint_inventory.py` and
  tests. The checker inventories static route metadata entry points without
  claiming live menu routing.
- Agent 4 extended `tools/ui_smoke/report_rmlui_progress.py` and tests so
  text, markdown, and JSON progress output include optional legacy-removal
  summaries and parity-gate open/closed state.
- Agent 5 added `tools/ui_smoke/check_rmlui_route_metadata_shape.py` and
  tests. The checker validates route metadata shape, path safety, task IDs,
  entry/data-model lists, phases, and advanced-route controller-contract refs.

Worker logs:
- `docs-dev/rmlui-agent1-session-lobby-controller-stubs-round14-2026-07-02.md`
- `docs-dev/rmlui-agent2-document-id-inventory-round14-2026-07-02.md`
- `docs-dev/rmlui-agent3-entrypoint-inventory-round14-2026-07-02.md`
- `docs-dev/rmlui-agent4-progress-legacy-removal-round14-2026-07-02.md`
- `docs-dev/rmlui-agent5-route-metadata-shape-round14-2026-07-02.md`

## Coordinator Validation

Accepted checks:
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
- `python tools\ui_smoke\check_rmlui_event_inventory.py`
- `python tools\ui_smoke\check_rmlui_a11y_inventory.py`
- `python tools\ui_smoke\check_rmlui_document_id_inventory.py`
- `python tools\ui_smoke\check_rmlui_entrypoint_inventory.py`
- `python tools\ui_smoke\check_rmlui_metadata_sync.py`
- `python tools\ui_smoke\check_rmlui_route_metadata_shape.py`
- `python tools\ui_smoke\check_rmlui_phase_consistency.py`
- `python tools\ui_smoke\check_rmlui_dependency_decision.py`
- `python tools\ui_smoke\check_rmlui_legacy_removal.py`
- `python tools\ui_smoke\report_rmlui_progress.py`
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`
- `python tools\ui_smoke\report_rmlui_progress.py --format json`

Focused pytest passed with the Round 14 tests included: `183 passed`.

```powershell
python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_data_model_inventory.py tools/ui_smoke/test_check_rmlui_condition_inventory.py tools/ui_smoke/test_check_rmlui_event_inventory.py tools/ui_smoke/test_check_rmlui_a11y_inventory.py tools/ui_smoke/test_check_rmlui_document_id_inventory.py tools/ui_smoke/test_check_rmlui_entrypoint_inventory.py tools/ui_smoke/test_check_rmlui_metadata_sync.py tools/ui_smoke/test_check_rmlui_route_metadata_shape.py tools/ui_smoke/test_check_rmlui_phase_consistency.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_legacy_removal.py tools/ui_smoke/test_report_rmlui_progress.py
```

Package/staged asset validation passed with `197` packaged assets, `103`
RmlUi package/loose assets, `73` staged runtime paths, and `16` staged imported
assets:

```powershell
python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round14-package-validation --base-game basew --archive-name pak0.pkz
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round14-package-validation --base-game basew
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round14-package-validation --base-game basew --format json
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round14-package-validation --base-game basew --write-manifest .tmp\rmlui\round14-runtime-assets-staged.json
```

The touched RmlUi client object still compiles:

```powershell
ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v
```

Ninja still emits the known `premature end of file; recovering` warning. The
broader Windows Meson target remains subject to the existing `llvm-ar`
regular-archive to thin-archive blocker recorded in Round 4.

## Remaining Gates

- Gate G1: real RmlUi dependency/build integration and native renderer proof.
- Gate G2: live cvar/command/data-model/condition/event/localization/a11y
  controllers and services.
- Gate G3: runtime activation and session/menu parity for migrated routes.
- Gate G4: screenshot/layout/input/renderer evidence, `parity_ready` proof, and
  documented legacy JSON removal.
