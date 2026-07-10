# RmlUi Round 15 Agent 4 Dependency Integration Check

Date: 2026-07-02

Task IDs: `FR-09-T02`, `FR-09-T03`, `DV-03-T07`, `DV-06-T01`

## Scope

Agent 4 added a static smoke checker for the RmlUi dependency/build integration
surface. The checker validates the current Meson/source wiring without
downloading or building the real RmlUi source.

Owned changes:

- `tools/ui_smoke/check_rmlui_dependency_integration.py`
- `tools/ui_smoke/test_check_rmlui_dependency_integration.py`
- `docs-dev/rmlui-agent4-dependency-integration-check-round15-2026-07-02.md`

## Checker Coverage

The checker scans:

- RmlUi source/wrap presence under `subprojects/`;
- RmlUi `dependency()` or `subproject()` declarations in `meson.build`;
- RmlUi Meson option declarations in `meson_options.txt`;
- RmlUi runtime compile defines such as `UI_RML_HAS_RUNTIME`;
- the WORR scaffold source at `src/client/ui_rml/ui_rml.cpp`.

It reports `absent`, `declared`, `optional`, `enabled`, and
`runtime-compiled` states where they apply. Disabled-by-default or optional
integration is accepted. Malformed half-wiring is nonzero, including a Meson
option with no dependency declaration, an enabled runtime define with no RmlUi
dependency, a required dependency that ignores an optional option, or a missing
scaffold source that is still listed in Meson.

The tool supports text output and `--format json`. JSON exposes per-surface
status, paths, source/wrap counts, Meson declaration counts, option details,
compile define snippets, scaffold facts, warnings, and errors.

## Current Live Result

Accepted live result from `python tools\ui_smoke\check_rmlui_dependency_integration.py`:

- state: `optional`
- components present: `4/4`
- malformed findings: `0`
- warnings: `0`
- dependency source/wrap: `optional`
- wrap files: `1` (`subprojects/rmlui.wrap`)
- source dirs: `0`
- Meson declarations: `2` (`dependency('RmlUi')`, `dependency('rmlui')`)
- Meson option: `rmlui`, type `feature`, default `disabled`, status `optional`
- compile defines: `1` (`UI_RML_HAS_RUNTIME`), status `optional`
- runtime compiled: `no`
- scaffold source: `compiled-stub`, source exists, listed in Meson, runtime
  guard present

This means the current build integration is declared and optional, but the
default configuration does not compile the RmlUi runtime. The checker does not
require the `subprojects/RmlUi-6.2` source tree or a configured build directory
to exist.

## Validation

Run before handoff:

```text
python -m pytest tools/ui_smoke/test_check_rmlui_dependency_integration.py
python tools/ui_smoke/check_rmlui_dependency_integration.py
python tools/ui_smoke/check_rmlui_dependency_integration.py --format json
git diff --check -- tools/ui_smoke/check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_integration.py docs-dev/rmlui-agent4-dependency-integration-check-round15-2026-07-02.md
```

Focused pytest coverage: `8` tests.

## Non-Goals

This checker does not build RmlUi, download wrap sources, prove renderer output,
or validate runtime UI parity. Vulkan renderer work remains native-only; this
tool only reports static dependency/build integration facts.
