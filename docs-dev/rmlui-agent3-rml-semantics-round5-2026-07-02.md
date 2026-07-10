# RmlUi Agent 3 RML Semantics Round 5

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Scope

This round adds a static RML semantics checker for the manifest-authored RmlUi
documents. The checker complements the manifest and route-contract validators by
reading `tools/ui_smoke/rmlui_manifest.json` as the route registry, parsing each
present route document with `ElementTree`, and validating controller-facing
attributes that can be checked without running the UI.

The roadmap document was not edited in this worker slice because Round 5 Worker
2 is concurrently updating migration phase state. This log records the task
coverage and implementation details for the owned Agent 3 semantics work.

## Changed Files

- `tools/ui_smoke/check_rmlui_semantics.py`
- `tools/ui_smoke/test_check_rmlui_semantics.py`
- `docs-dev/rmlui-agent3-rml-semantics-round5-2026-07-02.md`

## Implementation Notes

- Added `check_rmlui_semantics.py` with the same dependency-free CLI style as
  the existing UI smoke validators.
- Route IDs and document paths are collected from the smoke manifest; only
  present document files are parsed.
- `data-route-target` values are checked against known manifest route IDs when
  they are static route tokens. Dynamic template values and external/hash
  targets are intentionally skipped through documented patterns in the checker.
- Elements with `data-command` must have non-empty `id` attributes, and empty
  command values are reported.
- Direct cvar attributes are checked for lowercase snake_case-ish tokens:
  `data-cvar`, `data-bind-cvar`, `data-label-cvar`, and the existing authored
  `data-command-cvar`.
- Conditional cvar expressions are conservatively sampled from
  `data-enable-if`, `data-show-if`, and the existing authored
  `data-visible-if`, validating direct bare/comparison tokens while skipping
  dynamic template expressions.
- The CLI prints counts for documents checked, route targets checked, command
  elements checked, and cvar references checked.

## Test Coverage

Focused pytest coverage uses temporary manifests and RML fixtures for:

- Valid static route target resolution.
- Unknown static route target failure.
- Missing `id` on an element with `data-command`.
- Bad direct cvar token failure.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_semantics.py` passed:
  `4 passed`.
- `python tools/ui_smoke/check_rmlui_semantics.py` passed against the current
  repository manifest:
  - Routes known: 57
  - Documents checked: 57
  - Route targets checked: 52
  - Dynamic/external route targets skipped: 0
  - Command elements checked: 289
  - Cvar references checked: 452

## Handoff Notes

- Future controller work can add additional static attributes to the checker as
  the bridge contract settles.
- If dynamic or external route targets are introduced, keep their skip behavior
  explicit in `check_rmlui_semantics.py` so accidental misspellings continue to
  fail loudly.
