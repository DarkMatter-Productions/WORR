# RmlUi Parallel Round 16 Integration

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, `FR-09-T10`,
`FR-07-T01`, `FR-07-T02`, `DV-03-T07`, `DV-04-T02`, and `DV-07-T04`.

## Summary

Round 16 accepts the controller-stub completion slice for the remaining
non-runtime RmlUi migration routes. It promotes the final `12` central
`starter` routes to static `controller_stub`, adds missing static RML hooks for
admin, MyMap, map selector, match stats, and tournament flows, reconciles
feature metadata/controller-contract declarations, and adds a strict
controller-stub completion checker.

Accepted baseline:

- Route documents: `57/57` present.
- Central migration phases: `starter=0`, `controller_stub=54`,
  `runtime_stub=3`.
- Advanced routes: `57/57` (`100.0%`).
- Controller contracts: `149` refs across `57` routes.
- Controller contract categories: `command_action=57`, `condition_state=22`,
  `cvar_binding=30`, `keybind=3`, `list_provider=8`, `navigation=26`,
  `preview=1`, and `save_load=2`.
- Parity checklist: `controller_bindings=57` complete and `0` pending.
- Route metadata shape: `58` metadata routes, including the support-only
  `core.runtime_smoke` starter metadata route.
- Legacy-removal progress: `6` items, `blocked=4`, `pending=2`, `ready=0`,
  `complete=0`, parity gate closed.
- Parity state: `0` `parity_ready` routes.

This remains static source, metadata, and guardrail work only. No compiled
RmlUi runtime, native renderer bridge, live controller behavior, runtime
navigation, screenshot/layout evidence, parity proof, or legacy JSON removal is
claimed.

## Worker Results

- Agent 1 hardened admin/confirmation routes and added the missing static
  replay-confirm route target from `admin_menu`.
- Agent 2 hardened MyMap, map-selector, and match-stats RML hooks with direct
  cvar bindings, condition gates, and fixed-list providers.
- Agent 3 hardened tournament info/map-choice/veto/replay-confirm flows with
  explicit command, cvar-label, visibility, and list-provider hooks.
- Agent 4 added `tools/ui_smoke/check_rmlui_controller_stub_completion.py`
  plus focused tests for strict/non-strict text and JSON completion reports.
- Agent 5 audited install/package/parity gates and confirmed the expected
  post-round target remained static controller-stub completion, not live
  runtime activation.

Worker logs:

- `docs-dev/rmlui-agent1-admin-confirm-controller-stubs-round16-2026-07-02.md`
- `docs-dev/rmlui-agent2-mymap-mapstats-controller-stubs-round16-2026-07-02.md`
- `docs-dev/rmlui-agent3-tournament-controller-stubs-round16-2026-07-02.md`
- `docs-dev/rmlui-agent4-controller-stub-completion-round16-2026-07-02.md`
- `docs-dev/rmlui-agent5-round16-install-docs-gate-2026-07-02.md`

## Coordinator Validation

Accepted checks:

```powershell
python tools\ui_smoke\check_rmlui_manifest.py
python tools\ui_smoke\check_rmlui_route_contracts.py
python tools\ui_smoke\check_rmlui_semantics.py
python tools\ui_smoke\check_rmlui_controller_stub_coverage.py
python tools\ui_smoke\check_rmlui_controller_stub_completion.py --require-complete-controller-stubs --format json
python tools\ui_smoke\report_rmlui_progress.py --format json
python tools\ui_smoke\check_rmlui_parity_manifest.py --format json
python tools\ui_smoke\check_rmlui_legacy_removal.py --format json
python tools\ui_smoke\check_rmlui_metadata_sync.py --format json
python tools\ui_smoke\check_rmlui_route_metadata_shape.py --format json
python tools\ui_smoke\check_rmlui_dependency_integration.py --format json
python tools\ui_smoke\check_rmlui_dependency_decision.py
python tools\ui_smoke\check_rmlui_command_inventory.py
python tools\ui_smoke\check_rmlui_cvar_inventory.py
python tools\ui_smoke\check_rmlui_condition_inventory.py
python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_controller_stub_completion.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_data_model_inventory.py tools/ui_smoke/test_check_rmlui_condition_inventory.py tools/ui_smoke/test_check_rmlui_event_inventory.py tools/ui_smoke/test_check_rmlui_a11y_inventory.py tools/ui_smoke/test_check_rmlui_document_id_inventory.py tools/ui_smoke/test_check_rmlui_entrypoint_inventory.py tools/ui_smoke/test_check_rmlui_metadata_sync.py tools/ui_smoke/test_check_rmlui_route_metadata_shape.py tools/ui_smoke/test_check_rmlui_phase_consistency.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_legacy_removal.py tools/ui_smoke/test_report_rmlui_progress.py
meson setup builddir-win --reconfigure
meson setup builddir-win --reconfigure -Drmlui=auto
meson setup builddir-win --reconfigure -Drmlui=disabled
meson compile -C builddir-win
ninja -C builddir-win -n
meson introspect builddir-win --buildoptions
```

Focused pytest coverage includes the new controller-stub completion tests and
the existing RmlUi smoke/package suite: `196 passed`.

Package/staged asset validation passed with `197` packaged assets, `103` RmlUi
package/loose assets, `73` staged runtime paths, and `16` staged imported
assets under `.tmp/rmlui/round16-package-validation`.

The Windows builddir remained on `rmlui=disabled` after validation. Default
and `-Drmlui=auto` reconfiguration stayed non-fatal with the optional
dependency absent, the active builddir was restored to the disabled RmlUi
option, `meson compile -C builddir-win` reported no work to do, and the final
`ninja -C builddir-win -n` also reported no work to do. Build option
introspection reported `rmlui disabled`.

## Remaining Gates

- RmlUi must still compile/link through the selected dependency path.
- Live C++ controllers and data-provider bridges remain pending.
- OpenGL, Vulkan, and RTX/vkpt native renderer proof remains pending.
- Runtime navigation, screenshots, input/back behavior, parity evidence, and
  legacy JSON removal remain blocked by later gates.
