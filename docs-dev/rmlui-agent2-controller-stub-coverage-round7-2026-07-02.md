# RmlUi Agent 2 Controller Stub Coverage Round 7

Date: 2026-07-02

Worker lane: Worker 2, controller-stub eligibility validation

Task IDs: `FR-09-T05`, `FR-09-T09`, `DV-03-T07`, `DV-04-T02`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

Round 7 adds a dependency-free eligibility checker that connects
`controller_stub` phase claims in `tools/ui_smoke/rmlui_manifest.json` to the
route-level `controller_contracts` metadata in
`assets/ui/rml/shell/routes.json`.

The checker stays intentionally conservative. It parses only the authored RML
documents referenced by manifest routes that already claim
`migration_phase: "controller_stub"`, infers controller contract categories
from static attributes present in those documents, and requires the matching
shell route metadata to cover the inferred categories.

## Changed Files

- `tools/ui_smoke/check_rmlui_controller_stub_coverage.py`
  - Adds a standalone CLI with `--manifest`, `--shell-routes`, and
    `--repo-root` arguments.
  - Checks every manifest route in the `controller_stub` phase for matching
    shell route metadata, the same shell `migration_phase`, and a non-empty
    `controller_contracts` list.
  - Infers static controller categories from RML attributes:
    `navigation`, `command_action`, `cvar_binding`, and `condition_state`.
  - Reports counts for checked `controller_stub` routes plus inferred,
    covered, and missing category coverage.
  - Exits non-zero when shell metadata, contract declarations, or inferred
    category coverage is missing.
- `tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py`
  - Adds focused pytest coverage for valid coverage, missing shell route
    metadata, missing controller contracts, and missing inferred category
    coverage.
- `docs-dev/rmlui-agent2-controller-stub-coverage-round7-2026-07-02.md`
  - Records this validation pass and task mapping.

## Task Mapping

`FR-09-T05`: Adds an automated guard that verifies mock controller/data-model
contract categories line up with static controller-facing RML hooks.

`FR-09-T09`: Strengthens migration phase evidence by making
`controller_stub` eligibility depend on both manifest and shell route metadata.

`DV-03-T07`: Extends the UI smoke toolchain with a focused regression check for
controller-stub route progression.

`DV-04-T02`: Keeps ownership boundaries narrow by validating metadata and
static RML evidence without touching runtime, renderer, or protocol code.

`DV-07-T04`: Improves the parity trail by catching routes whose static
navigation, command, cvar, or conditional hooks are not represented in
controller contract metadata.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py`
  - Result: passed, `4 passed`.
- `python tools/ui_smoke/check_rmlui_controller_stub_coverage.py`
  - Result: passed.
  - Current workspace counts: `15` `controller_stub` routes checked;
    inferred and covered categories were `navigation=5`,
    `command_action=15`, `cvar_binding=12`, and `condition_state=3`.
  - Missing categories: `none`.
- `git diff --check -- tools/ui_smoke/check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py docs-dev/rmlui-agent2-controller-stub-coverage-round7-2026-07-02.md`
  - Result: passed with no output.
  - Because these owned files are new and untracked in the current RmlUi
    worktree, an additional `git diff --check --no-index` pass compared each
    file against an empty `.tmp/` scratch file.
  - Result: no whitespace errors; Git reported the expected LF/CRLF warning
    for the new docs file.

## Notes

- No `q2proto/` files were touched.
- No renderer files or Vulkan/OpenGL paths were touched.
- The checker does not inspect dynamic template expressions beyond the static
  attributes present in the parsed document tree.
