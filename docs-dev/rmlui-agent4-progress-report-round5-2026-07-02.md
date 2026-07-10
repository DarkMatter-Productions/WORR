# RmlUi Agent 4 Progress Report Round 5

Date: 2026-07-02

Worker: 4

Tasks: `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Scope

This pass adds a concise progress report for the central RmlUi smoke manifest.
The new tool reads `tools/ui_smoke/rmlui_manifest.json` by default and computes
its route counts at runtime, so later migration-phase changes in the manifest
are reflected without editing the report script.

The report is intentionally lighter than the existing validators. It confirms
that the manifest is readable JSON with a `routes` list of objects, that route
document paths are repo-relative, and that `required_now` is boolean when
present. It does not duplicate the full RML parsing, href import checks, or
phase vocabulary enforcement from `check_rmlui_manifest.py`.

## Tool Behavior

`tools/ui_smoke/report_rmlui_progress.py` reports:

- total route count,
- document presence count relative to the repository root,
- required document presence count,
- grouped route counts by `wave`,
- grouped route counts by `owner`,
- grouped route counts by `status`,
- grouped route counts by `migration_phase`.

The default text output is for quick terminal status checks. The
`--format markdown` mode emits a small two-column table for copying into
`docs-dev/` status notes or roadmap updates.

The tool exits non-zero when the manifest cannot be read, is not a JSON object,
does not contain a `routes` list, contains non-object routes, or contains
invalid lightweight fields needed by the report.

## Validation

Focused pytest coverage in `tools/ui_smoke/test_report_rmlui_progress.py`
creates temporary manifests and document files so the assertions cover behavior
rather than the current repo counts:

- text summaries for wave, owner, status, and migration phase,
- required and optional document presence accounting,
- markdown table output,
- non-zero handling for invalid manifest shape.

Commands run:

```powershell
python -m pytest tools/ui_smoke/test_report_rmlui_progress.py
python tools/ui_smoke/report_rmlui_progress.py
python tools/ui_smoke/report_rmlui_progress.py --format markdown
```

Results:

- pytest passed: `3` tests.
- The live text report read `tools/ui_smoke/rmlui_manifest.json` and reported
  `57` total routes, `57/57` present documents, `57/57` required documents
  present, waves `A=21, B=11, C=25`, owners
  `agent4-shell-settings-singleplayer=23` and
  `agent5-rich-tools-session-validation=34`, statuses
  `starter=10, starter_round2=20, starter_round3=27`, and migration phases
  `starter=52, controller_stub=5`.
- The live markdown report emitted the same counts as a compact copy/paste
  table.
