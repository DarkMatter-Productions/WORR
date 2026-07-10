# RmlUi Agent 1 Runtime Registry Check Round 7

Date: 2026-07-02

Owner lane: Agent 1, client runtime scaffold

Task IDs: `FR-09-T02`, `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-04-T02`

## Summary

This round promotes the ad hoc static route-registry comparison into a
first-class smoke tool:
`tools/ui_smoke/check_rmlui_runtime_registry.py`.

The checker reads `tools/ui_smoke/rmlui_manifest.json`, parses the static
`ui_rml_routes` table in `src/client/ui_rml/ui_rml.cpp`, and reports drift
between the smoke manifest and the dependency-free runtime probe registry.

## Changed Files

- `tools/ui_smoke/check_rmlui_runtime_registry.py`
  - Adds a dependency-free CLI for validating the static runtime route table.
  - Confirms every smoke-manifest route ID is registered.
  - Allows the single default non-manifest runtime probe route
    `core.runtime_smoke`.
  - Rejects duplicate registered route IDs and unexpected non-manifest route
    IDs.
  - Validates registered document paths are runtime-relative `.rml` paths that
    use `/`, avoid absolute or parent-directory segments, and remain under
    `ui/rml` once prefixed.
  - Derives expected runtime paths from manifest source paths under
    `assets/ui/rml` and compares them against registered route documents.
- `tools/ui_smoke/test_check_rmlui_runtime_registry.py`
  - Adds focused pytest coverage for valid registry alignment, missing
    manifest routes, unexpected registered routes, duplicate route IDs, and
    mismatched registered document paths.
- `docs-dev/rmlui-agent1-runtime-registry-check-round7-2026-07-02.md`
  - Records the implementation scope, task mapping, validation plan, and
    caveats for Round 7.

## Task Mapping

`FR-09-T02`: Keeps the dependency-free client runtime scaffold covered by a
repeatable registry drift check instead of coordinator-only scripts.

`FR-09-T03`: Protects runtime bootstrap probing by ensuring every smoke
manifest route remains present in `ui_rml_routes`.

`FR-09-T09`: Verifies the registry uses runtime document paths that correspond
to packaged/staged RmlUi asset locations under `ui/rml`.

`DV-03-T07`: Aligns manifest tracking, runtime probing, and smoke tooling
around the same canonical route IDs and document mappings.

`DV-04-T02`: Keeps route registration centralized in the RmlUi runtime scaffold
while making drift visible through a reusable smoke command.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_registry.py`
  - Result: passed, `5 passed`.
- `python tools/ui_smoke/check_rmlui_runtime_registry.py`
  - Result: passed.
  - Manifest routes: `57`.
  - Registered routes: `58`.
  - Missing: `0`.
  - Unexpected: `0`.
  - Duplicates: `0`.
  - Matched runtime paths: `57`.
- `git diff --check -- tools/ui_smoke/check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_runtime_registry.py docs-dev/rmlui-agent1-runtime-registry-check-round7-2026-07-02.md`
  - Result: passed with no output.
  - Because the owned files are currently untracked in this shared worktree,
    additional no-index checks against `NUL` were run for each new file. They
    reported no whitespace errors; Git returned the expected diff-exists exit
    code and emitted an LF/CRLF normalization warning for this Markdown file.

## Caveats

- This round does not modify `src/client/ui_rml/ui_rml.cpp`; it validates the
  existing static registry.
- This round does not add RmlUi as a dependency, rendering, controller
  bindings, input dispatch, or menu authority changes.
- This round does not touch `q2proto/` or renderer code.
- The strategic roadmap document was not edited in this worker because the
  requested write scope was limited to the checker, its tests, and this log.
