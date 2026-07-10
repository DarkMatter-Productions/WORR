# RmlUi Parallel Round 15 Integration

Date: 2026-07-02

Tasks: `FR-09-T02`, `FR-09-T03`, `FR-09-T09`, `DV-03-T07`,
`DV-06-T01`, and `DV-07-T04`.

## Summary

Round 15 accepts the first dependency/build integration slice without claiming
runtime ownership. It pins an upstream RmlUi source wrap, adds a
default-disabled Meson feature option, prepares the client scaffold for future
runtime hooks, adds a dependency-integration smoke checker, and revalidates the
package/install gate.

Accepted dependency/build state:

- Source acquisition: `subprojects/rmlui.wrap` pins upstream RmlUi `6.2`.
- Archive SHA-256:
  `814c3ff7b9666280338d8f0dda85979f5daf028d01c85fc8975431d1e2fd8e8b`.
- Meson option: `rmlui`, type `feature`, default `disabled`.
- Integration checker state: `optional`.
- Integration components present: `4/4`.
- Wrap files: `1`; source dirs: `0`.
- Meson dependency declarations: `2` optional probes.
- Compile defines: `1` optional `UI_RML_HAS_RUNTIME` define.
- Runtime compiled: `false`; scaffold status: `compiled-stub`.

This round does not add a live RmlUi runtime, native renderer bridge, live
controller execution, runtime navigation evidence, screenshot/layout evidence,
`parity_ready` routes, or legacy JSON removal.

## Worker Results

- Agent 1 added the RmlUi `6.2` wrap and dependency-source audit. The wrap is
  downloadable and CMake-method based, but it intentionally has no `[provide]`
  section or Meson patch overlay yet.
- Agent 2 added the default-disabled `rmlui` feature option and guarded Meson
  dependency probes. `UI_RML_HAS_RUNTIME=1` is only emitted when a real RmlUi
  dependency resolves.
- Agent 3 refactored the dependency-free client scaffold to expose runtime
  availability, a file-interface boundary, and a future runtime hook interface
  while preserving legacy fallback behavior.
- Agent 4 added `tools/ui_smoke/check_rmlui_dependency_integration.py` and
  focused tests for the dependency/source/build/scaffold state.
- Agent 5 confirmed the package/install guardrails already cover the current
  dependency-era asset facts and produced fresh staged evidence.

Worker logs:

- `docs-dev/rmlui-agent1-dependency-source-round15-2026-07-02.md`
- `docs-dev/rmlui-agent2-build-option-round15-2026-07-02.md`
- `docs-dev/rmlui-agent3-runtime-interface-round15-2026-07-02.md`
- `docs-dev/rmlui-agent4-dependency-integration-check-round15-2026-07-02.md`
- `docs-dev/rmlui-agent5-install-gate-round15-2026-07-02.md`

## Coordinator Validation

Accepted checks:

```powershell
python tools\ui_smoke\check_rmlui_dependency_integration.py
python tools\ui_smoke\check_rmlui_dependency_integration.py --format json
python tools\ui_smoke\check_rmlui_manifest.py
python tools\ui_smoke\check_rmlui_phase_consistency.py
python tools\ui_smoke\check_rmlui_dependency_decision.py
python tools\ui_smoke\report_rmlui_progress.py --format json
meson setup builddir-win --reconfigure
meson setup builddir-win --reconfigure -Drmlui=auto
meson setup builddir-win --reconfigure -Drmlui=disabled
ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v
meson compile -C builddir-win
ninja -C builddir-win -n
```

Focused pytest coverage includes the new dependency-integration tests and the
existing RmlUi smoke/package suite: `191 passed`.

Package/staged asset validation passed with `197` packaged assets, `103` RmlUi
package/loose assets, `73` staged runtime paths, and `16` staged imported
assets under `.tmp/rmlui/round15-package-validation`.

The Windows builddir remained on `rmlui=disabled` after the validation pass.
`meson compile -C builddir-win` completed cleanly, and the final
`ninja -C builddir-win -n` reported no work to do.

## Remaining Gates

- RmlUi must still compile/link through the selected dependency path.
- Runtime file/system/font/input/controller bridges remain pending.
- OpenGL, Vulkan, and RTX/vkpt native renderer proof remains pending.
- Runtime navigation, screenshots, parity evidence, and legacy JSON removal
  remain blocked by later gates.
