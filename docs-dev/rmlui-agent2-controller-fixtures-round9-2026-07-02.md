# RmlUi Agent 2 Controller Fixtures Round 9

Date: 2026-07-02

Worker lane: Worker 2, controller fixture validation

Task IDs: `FR-09-T05`, `FR-09-T09`, `DV-03-T07`, `DV-04-T02`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

Round 9 adds a dependency-free checker for mock controller fixture references
declared by route `controller_contracts` metadata. The checker discovers route
metadata files under `assets/ui/rml/*/routes.json`, validates the shape of each
controller contract entry, resolves each referenced `*.mock.json` file under
`assets/ui/rml/contracts`, and validates the referenced fixture JSON payloads.

This validation is static metadata coverage only. It verifies mock/controller
fixture files and route metadata references; it does not claim live C++
controller behavior, live data-model wiring, RmlUi rendering, runtime parity, or
legacy UI cutover.

## Changed Files

- `tools/ui_smoke/check_rmlui_controller_fixtures.py`
  - Adds a standalone CLI for route controller fixture validation.
  - Discovers `assets/ui/rml/*/routes.json` metadata, including optional future
    route groups when present.
  - Requires route controller contract entries to provide `category`,
    `contract`, `fixture`, `model`, and `status`.
  - Requires fixture references to stay under `assets/ui/rml/contracts` and to
    point at `*.mock.json` files.
  - Validates referenced fixtures as JSON objects with an id-ish field such as
    `contract`, `$id`, `id`, `schema`, or `$schema`.
  - Reports metadata file count, route contract references, fixture count,
    missing fixtures, malformed fixtures, and malformed contract refs in text or
    JSON format.
- `tools/ui_smoke/test_check_rmlui_controller_fixtures.py`
  - Adds pytest coverage for valid fixture references, missing fixture files,
    malformed JSON fixture payloads, malformed controller contract entries, and
    optional route metadata discovery.
- `docs-dev/rmlui-agent2-controller-fixtures-round9-2026-07-02.md`
  - Records the Worker 2 scope, validation intent, and task mapping.

## Task Mapping

`FR-09-T05`: Adds an explicit audit for controller contract fixture metadata so
mock controller references remain resolvable and well-formed while live
controller work is pending.

`FR-09-T09`: Extends route migration traceability by tying route metadata
contract claims to concrete fixture files under the RmlUi asset tree.

`DV-03-T07`: Adds another UI smoke tool for repeatable static validation in the
RmlUi migration lane.

`DV-04-T02`: Keeps the validation lane bounded to metadata and fixture assets;
no protocol, renderer, or live runtime files are touched.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_controller_fixtures.py`
  - Result: passed, `5 passed`.
- `python tools/ui_smoke/check_rmlui_controller_fixtures.py`
  - Result: passed.
  - Current workspace counts: `3` route metadata files, `28` routes checked,
    `19` routes with controller contracts, `54` route contract refs, `5`
    unique fixtures referenced, `5` fixtures present, `0` missing fixtures,
    `0` malformed fixtures, and `0` malformed contract refs.
- `python tools/ui_smoke/check_rmlui_controller_fixtures.py --format json`
  - Result: passed.
  - The checker discovered `assets/ui/rml/core/routes.json`,
    `assets/ui/rml/shell/routes.json`, and
    `assets/ui/rml/utility/routes.json`.
- `git diff --check -- tools/ui_smoke/check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py docs-dev/rmlui-agent2-controller-fixtures-round9-2026-07-02.md`
  - Result: passed with no output.
  - Because these owned files are new and untracked in the current shared
    RmlUi worktree, an additional `git diff --check --no-index` pass compared
    each file against an empty `.tmp/` scratch file.
  - Result: no whitespace errors; Git reported the expected LF/CRLF warning for
    this Markdown file.

## Caveats

- This checker validates static mock fixture references only.
- It does not execute or verify live C++ controller behavior.
- It does not add a native RmlUi renderer path or prove visual parity.
- No `q2proto/` files were touched.
