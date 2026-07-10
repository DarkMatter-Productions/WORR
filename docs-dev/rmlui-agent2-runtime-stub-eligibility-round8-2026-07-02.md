# RmlUi Agent 2 Runtime Stub Eligibility Round 8

Date: 2026-07-02

Worker lane: Worker 2, runtime-stub eligibility validation

Task IDs: `FR-09-T03`, `FR-09-T05`, `FR-09-T09`, `DV-03-T07`, `DV-04-T02`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

Round 8 adds a dependency-free eligibility checker for routes promoted to
`migration_phase: "runtime_stub"` in `tools/ui_smoke/rmlui_manifest.json`.

The checker intentionally validates only static probe/runtime-stub evidence. It
does not claim real RmlUi runtime rendering, input/controller parity, or menu
cutover. For this round, only `main`, `game`, and `download_status` are eligible
because they are the routes currently returned by `UI_Rml_RouteForMenu` and
probed by `UI_Rml_OpenMenu` when `ui_rml_enable` is non-zero.

## Changed Files

- `tools/ui_smoke/check_rmlui_runtime_stub_eligibility.py`
  - Adds a standalone CLI that reads the RmlUi smoke manifest, shell route
    metadata, and `src/client/ui_rml/ui_rml.cpp`.
  - Selects manifest routes with `migration_phase: "runtime_stub"` and permits
    only `main`, `game`, and `download_status` in this round.
  - Requires matching shell route metadata with the same `runtime_stub` phase
    and non-empty `controller_contracts`.
  - Parses `UI_Rml_RouteForMenu` to ensure each runtime-stub route is returned
    for at least one `UIMENU_*` case.
  - Parses `ui_rml_routes` to ensure each runtime-stub route is registered with
    the expected runtime document path derived from the manifest document.
  - Statically verifies `UI_Rml_OpenMenu` still probes the route and returns
    `false` after the probe path.
- `tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py`
  - Adds focused pytest coverage for zero `runtime_stub` routes, a valid
    runtime-stub route, missing menu mapping, missing controller contracts, and
    registry path mismatch.
- `docs-dev/rmlui-agent2-runtime-stub-eligibility-round8-2026-07-02.md`
  - Records this validation pass and task mapping.

## Task Mapping

`FR-09-T03`: Guards runtime route probing by requiring promoted routes to remain
registered and menu-mapped in the dependency-free client scaffold.

`FR-09-T05`: Keeps controller contract metadata attached when a route advances
from controller-stub evidence to runtime-stub evidence.

`FR-09-T09`: Connects manifest phase claims, shell route metadata, and runtime
document paths so promotion evidence remains auditable.

`DV-03-T07`: Extends the UI smoke toolchain with a focused runtime-stub
eligibility regression check.

`DV-04-T02`: Preserves narrow ownership boundaries by validating metadata and
static client UI scaffolding without editing runtime, renderer, or protocol
code.

`DV-07-T04`: Improves the parity trail by preventing runtime-stub promotion
when controller contracts, menu routing, or registry paths drift.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py`
  - Result: passed, `5 passed`.
- `python tools/ui_smoke/check_rmlui_runtime_stub_eligibility.py`
  - Result: passed.
  - Current workspace counts: `3` `runtime_stub` routes checked; `3`
    menu-mapped routes; `3` registry matches; `3` controller contract matches.
- `git diff --check -- tools/ui_smoke/check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py docs-dev/rmlui-agent2-runtime-stub-eligibility-round8-2026-07-02.md`
  - Result: passed with no output.
  - Because these owned files are new and untracked in the current shared
    RmlUi worktree, an additional `git diff --check --no-index` pass compared
    each file against an empty `.tmp/` scratch file.
  - Result: no whitespace errors; Git reported the expected LF/CRLF warning
    for this Markdown file.

## Caveats

- This is static probe/runtime-stub validation only; it does not add or verify a
  real RmlUi runtime, rendering path, live controller binding, or parity.
- No `q2proto/` files were touched.
- No renderer files, Vulkan paths, or OpenGL paths were touched.
- The strategic roadmap document was not edited in this worker because the
  requested write scope was limited to the checker, its tests, and this log.
