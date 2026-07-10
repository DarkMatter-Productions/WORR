# RmlUi Agent 1 Menu Entrypoints Round 8

Date: 2026-07-02

Owner lane: Agent 1, client runtime scaffold

Task IDs: `FR-09-T02`, `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-04-T02`

## Summary

This round adds a reusable static checker for the guarded RmlUi menu-open
surface: `tools/ui_smoke/check_rmlui_menu_entrypoints.py`.

The checker parses `UI_Rml_RouteForMenu(uiMenu_t menu)` in
`src/client/ui_rml/ui_rml.cpp`, extracts each `UIMENU_*` case label and its
literal returned route ID, and validates those menu entrypoint routes against
the smoke manifest and static `ui_rml_routes` registry.

## Changed Files

- `tools/ui_smoke/check_rmlui_menu_entrypoints.py`
  - Adds a dependency-free CLI for validating menu-open route entrypoints.
  - Handles fallthrough `case UIMENU_*:` labels before a shared `return`.
  - Allows null menu mappings such as `UIMENU_NONE` while requiring every
    non-null returned route ID to exist in `tools/ui_smoke/rmlui_manifest.json`
    and in `ui_rml_routes`.
  - Reuses the runtime registry path rules so manifest source documents under
    `assets/ui/rml` must match registered runtime documents under `ui/rml`.
  - Reports menu cases checked, mapped route count, unique mapped route count,
    manifest matches, and registry matches.
  - Fails on missing manifest routes, missing registry routes, contradictory
    duplicate menu mappings, and mismatched runtime document paths.
- `tools/ui_smoke/test_check_rmlui_menu_entrypoints.py`
  - Covers a valid menu-entrypoint surface.
  - Covers missing manifest route and missing registry route failures.
  - Covers stacked fallthrough case labels before a shared return.
  - Covers runtime path mismatch failures.
  - Covers duplicate contradictory case mapping failures.
- `docs-dev/rmlui-agent1-menu-entrypoints-round8-2026-07-02.md`
  - Records the Round 8 implementation scope, task mapping, validation plan,
    and caveats.

## Task Mapping

`FR-09-T02`: Keeps the menu-open bridge aligned with the dependency-free RmlUi
runtime scaffold while the guarded probe remains opt-in.

`FR-09-T03`: Protects runtime bootstrap probing by proving each menu-open route
is statically registered before the menu bridge can probe it.

`FR-09-T09`: Confirms menu-open routes resolve to the same packaged runtime
asset paths derived from the smoke manifest under `ui/rml`.

`DV-03-T07`: Connects manifest tracking, static registry coverage, and menu
entrypoint coverage around the same canonical route IDs.

`DV-04-T02`: Keeps route registration centralized in the RmlUi runtime scaffold
while making the menu bridge's entrypoint surface auditable by smoke tooling.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_menu_entrypoints.py`
  - Result: passed, `6 passed`.
- `python tools/ui_smoke/check_rmlui_menu_entrypoints.py`
  - Result: passed.
  - Menu cases checked: `5`.
  - Mapped routes: `4`.
  - Unique mapped routes: `3`.
  - Manifest matches: `3`.
  - Registry matches: `3`.
- `git diff --check -- tools/ui_smoke/check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py docs-dev/rmlui-agent1-menu-entrypoints-round8-2026-07-02.md`
  - Result: passed with no output.
  - Because the owned files are currently untracked in this shared worktree,
    additional no-index checks against `NUL` were run for each new file. They
    reported no whitespace errors; Git returned the expected diff-exists exit
    code. Git also emitted an LF/CRLF normalization warning for this Markdown
    file.

## Caveats

- This round does not modify `src/client/ui_rml/ui_rml.cpp`; it validates the
  current static menu mapping.
- This round does not change RmlUi runtime behavior, renderer code, asset
  packaging, controller bindings, or menu authority.
- This round does not touch `q2proto/`.
- The strategic roadmap document was not edited in this worker because the
  requested write scope was limited to the checker, tests, and this log.
