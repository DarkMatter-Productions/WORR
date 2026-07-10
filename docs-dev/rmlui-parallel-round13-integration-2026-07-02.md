# RmlUi Parallel Round 13 Integration

Date: 2026-07-02

Tasks: `FR-09-T04`, `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, `FR-09-T10`,
`FR-07-T01`, `FR-07-T02`, `DV-03-T07`, `DV-04-T02`, and `DV-07-T04`.

## Summary

Round 13 accepted another static migration/progression slice. It promoted the
first vote/callvote session batch to `controller_stub`, added event/action and
accessibility/localization inventory checks, added a legacy-removal guardrail
manifest/checker, and wired event/a11y summaries into the RmlUi progress
reporter.

Accepted baseline:
- Route documents: `57/57` present.
- Migration phases: `starter=18`, `controller_stub=36`, `runtime_stub=3`.
- Advanced routes: `39/57` (`68.4%`).
- Controller contracts: `101` refs across `39` routes.
- Event/action inventory: `465` refs across `57` routes, `70` unique command
  tokens, and `0` malformed hooks.
- Accessibility/localization inventory: `8` refs across `3` routes, `6` unique
  localization keys, and `0` malformed hooks.
- Legacy-removal inventory: `6` items, `blocked=4`, `pending=2`, `ready=0`,
  and `complete=0`.
- Parity state: `0` `parity_ready` routes.

This remains static source, metadata, and guardrail work only. No first-class
RmlUi dependency, native renderer bridge, live controller behavior, runtime
navigation, screenshot/layout evidence, parity proof, or legacy JSON removal is
claimed.

## Worker Results

- Agent 1 promoted exactly `vote_menu`, `callvote_main`, `callvote_ruleset`,
  `callvote_timelimit`, `callvote_scorelimit`, `callvote_unlagged`,
  `callvote_random`, and `callvote_map_flags` to `controller_stub` in the
  central smoke manifest and session metadata. The eight promoted routes add
  `14` static controller-contract refs and keep legacy fallback authoritative.
- Agent 2 added `tools/ui_smoke/check_rmlui_event_inventory.py` and tests. The
  checker reports static event/action facts across all route documents without
  claiming a live event dispatcher.
- Agent 3 added `tools/ui_smoke/check_rmlui_a11y_inventory.py` and tests. The
  checker records current localization/a11y hooks so later live localization
  and accessibility services have a visible baseline.
- Agent 4 extended `tools/ui_smoke/report_rmlui_progress.py` and tests so
  text, markdown, and JSON progress output include optional event and a11y
  inventory summaries.
- Agent 5 added `tools/ui_smoke/rmlui_legacy_removal_manifest.json`,
  `tools/ui_smoke/check_rmlui_legacy_removal.py`, and tests. The manifest keeps
  all legacy removal items blocked or pending until parity evidence is accepted.

Worker logs:
- `docs-dev/rmlui-agent1-session-vote-controller-stubs-round13-2026-07-02.md`
- `docs-dev/rmlui-agent2-event-inventory-round13-2026-07-02.md`
- `docs-dev/rmlui-agent3-a11y-inventory-round13-2026-07-02.md`
- `docs-dev/rmlui-agent4-progress-events-a11y-round13-2026-07-02.md`
- `docs-dev/rmlui-agent5-legacy-removal-inventory-round13-2026-07-02.md`

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
- `python tools\ui_smoke\check_rmlui_metadata_sync.py`
- `python tools\ui_smoke\check_rmlui_phase_consistency.py`
- `python tools\ui_smoke\check_rmlui_dependency_decision.py`
- `python tools\ui_smoke\check_rmlui_legacy_removal.py`
- `python tools\ui_smoke\report_rmlui_progress.py`
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`
- `python tools\ui_smoke\report_rmlui_progress.py --format json`

Focused pytest passed with `163` tests:

```powershell
python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_data_model_inventory.py tools/ui_smoke/test_check_rmlui_condition_inventory.py tools/ui_smoke/test_check_rmlui_event_inventory.py tools/ui_smoke/test_check_rmlui_a11y_inventory.py tools/ui_smoke/test_check_rmlui_metadata_sync.py tools/ui_smoke/test_check_rmlui_phase_consistency.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_legacy_removal.py tools/ui_smoke/test_report_rmlui_progress.py
```

Package/staged asset validation passed:

```powershell
python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round13-package-validation --base-game basew --archive-name pak0.pkz
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round13-package-validation --base-game basew
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round13-package-validation --base-game basew --format json
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round13-package-validation --base-game basew --write-manifest .tmp\rmlui\round13-runtime-assets-staged.json
```

Packaging wrote `.tmp/rmlui/round13-package-validation/basew/pak0.pkz`, packed
`197` files from `assets`, validated `31` botfile package/loose files, and
validated `103` RmlUi package/loose files. Staged runtime asset checks found
`73` route/import paths and all `16` imported assets present.

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
