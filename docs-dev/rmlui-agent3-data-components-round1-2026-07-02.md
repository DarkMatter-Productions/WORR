# RmlUi Agent 3 Data Components Round 1

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T07`, `FR-03-T08`, `DV-04-T02`

## Scope

This round adds first-pass RmlUi asset contracts for Agent 3's data-model,
controller, and shared-component lane. It intentionally does not add C++
controllers yet. The files are mock-first handoff assets that let content
agents reference stable component names, command event names, cvar bindings,
and ownership boundaries while runtime/bootstrap work continues.

## Changed Files

- `assets/ui/rml/common/components/controls.rml`
- `assets/ui/rml/common/components/controls.rcss`
- `assets/ui/rml/common/components/list-table.rml`
- `assets/ui/rml/common/components/list-table.rcss`
- `assets/ui/rml/common/components/preview.rml`
- `assets/ui/rml/common/components/preview.rcss`
- `assets/ui/rml/common/components/save-load.rml`
- `assets/ui/rml/common/components/save-load.rcss`
- `assets/ui/rml/contracts/settings.mock.json`
- `assets/ui/rml/contracts/utility-list.mock.json`
- `assets/ui/rml/contracts/preview.mock.json`
- `assets/ui/rml/contracts/save-load.mock.json`
- `assets/ui/rml/contracts/session-multiplayer.mock.json`
- `docs-dev/rmlui-agent3-data-components-round1-2026-07-02.md`

## Implementation Notes

- Added reusable component stubs for cvar-backed controls, command buttons,
  conditionals, list/table views, preview panels, and save/load slot lists.
- Added small JSON mocks for settings, utility lists, previews, save/load, and
  multiplayer/session state. These define expected model names, component
  references, source boundaries, command events, cvar fields, and conditions.
- Kept contracts presentation-focused and bridge-neutral so they support the
  `FR-03-T08` and `DV-04-T02` ownership split without forcing a C++ shape.

## Validation

- JSON validation: every `assets/ui/rml/contracts/*.mock.json` file parsed with
  PowerShell `ConvertFrom-Json`.
- File presence check: component and contract files were listed after creation.
- Whitespace validation: `git diff --check` passed for the owned files.
- Build validation was not run because this slice adds mock assets only and no
  Meson/runtime integration exists for these paths yet.

## Handoff Notes

- Agent 4 can reference `settings.mock.json`, `save-load.mock.json`, and the
  `controls`/`save-load` component templates while authoring settings and
  single-player documents.
- Agent 5 can reference `utility-list.mock.json`,
  `session-multiplayer.mock.json`, and `preview.mock.json` for browser,
  utility, player-config, and session document drafts.
- Future Agent 3 controller work should bind `cvar.set`, `command.dispatch`,
  `list.select`, and the named command events to live client/cgame/sgame data
  providers.
