# RmlUi Agent 1 Navigation Graph Round 9

Date: 2026-07-02

Owner lane: Agent 1, static RmlUi navigation graph validation

Task IDs: `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Summary

This round adds `tools/ui_smoke/check_rmlui_navigation_graph.py`, a
dependency-free source checker for the route graph declared by authored RmlUi
documents.

The checker reads route IDs and document paths from
`tools/ui_smoke/rmlui_manifest.json`, scans present `.rml` documents for
`data-route-target` attributes, validates that every target resolves to a
manifest route ID, and reports graph progression facts for migration tracking.

The current guarded roots are `main`, `game`, and `download_status`, matching
the menu-entrypoint runtime stubs accepted in Round 8.

## Changed Files

- `tools/ui_smoke/check_rmlui_navigation_graph.py`
  - Adds a CLI source checker for `data-route-target` graph coverage.
  - Reports route count, present/missing document count, raw route-target
    references, unique directed edge count, unknown target count, dead-end
    routes, and routes unreachable from the guarded roots.
  - Treats unknown, empty, or non-static `data-route-target` values as
    validation failures because the static migration graph must resolve to the
    manifest.
  - Keeps dead-end and unreachable routes as reported progression facts instead
    of hard failures, since many RmlUi documents are still starter/controller
    stubs.
  - Supports `--format json` for machine-readable roadmap/status consumers.
- `tools/ui_smoke/test_check_rmlui_navigation_graph.py`
  - Covers known target edge construction.
  - Covers unknown target failure.
  - Covers dead-end route reporting.
  - Covers unreachable route reporting from explicit guarded roots.
  - Covers JSON output, including unique-edge counting when multiple elements
    point to the same route.
- `docs-dev/rmlui-agent1-navigation-graph-round9-2026-07-02.md`
  - Records implementation scope, task mapping, validation results, and
    caveats.

## Live Graph Facts

`python tools/ui_smoke/check_rmlui_navigation_graph.py` currently reports:

- Routes known: `57`.
- Documents checked: `57` present, `0` missing.
- Route-target references: `52`.
- Unique directed edges: `50`.
- Unknown targets: `0`.
- Dead-end routes: `44`.
- Routes unreachable from `main`, `game`, and `download_status`: `27`.

## Task Mapping

`FR-09-T09`: Adds a smoke-level source validator for route-to-route navigation
coverage, keeping route IDs and static document navigation aligned with the
manifest.

`DV-03-T07`: Provides a machine-readable graph summary that can be consumed by
progression tooling and roadmap status updates.

`DV-07-T04`: Adds focused pytest coverage around known targets, unknown
targets, dead-end routes, unreachable routes, and JSON output.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_navigation_graph.py`
  - Result: passed, `5 passed`.
- `python tools/ui_smoke/check_rmlui_navigation_graph.py`
  - Result: passed.
  - Routes known: `57`.
  - Route-target references: `52`.
  - Unique directed edges: `50`.
  - Unknown targets: `0`.
  - Dead-end routes: `44`.
  - Unreachable routes from guarded roots: `27`.
- `python tools/ui_smoke/check_rmlui_navigation_graph.py --format json`
  - Result: passed.
  - JSON payload reported `ok: true`, `edge_count: 50`, and no errors.

## Caveats

- This is static source graph validation only.
- This does not prove runtime navigation dispatch, RmlUi controller execution,
  rendered UI parity, screenshot parity, input focus behavior, or menu authority.
- This does not modify RmlUi documents, runtime code, renderer code, packaging,
  or `q2proto/`.
