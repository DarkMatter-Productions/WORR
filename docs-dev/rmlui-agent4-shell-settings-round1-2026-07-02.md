# RmlUi Agent 4 Shell/Settings Round 1

Date: 2026-07-02

Tasks: `FR-09-T06`, `FR-09-T07`, `FR-09-T04`, `FR-09-T09`

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Scope

This pass adds first-round RmlUi content scaffolding for the Agent 4 lane:
shell, settings, and single-player content. It is intentionally not full menu
parity. The goal is to seed loadable documents, preserve obvious legacy command
targets, and provide route/status metadata for the later smoke harness and
translation passes.

The starter documents import the planned shared theme/component paths from
`assets/ui/rml/common/`. At validation time, `common/theme/base.rcss` existed;
the other imports remained planned Agent 2/3 handoff paths.

## Changed Files

- `assets/ui/rml/shell/main.rml`
- `assets/ui/rml/shell/game.rml`
- `assets/ui/rml/shell/options.rml`
- `assets/ui/rml/settings/video.rml`
- `assets/ui/rml/singleplayer/singleplayer.rml`
- `assets/ui/rml/shell/routes.json`
- `docs-dev/rmlui-agent4-shell-settings-round1-2026-07-02.md`

## Route Metadata

`assets/ui/rml/shell/routes.json` lists all 23 Agent 4-owned routes from the
roadmap:

- Wave A shell/settings: `main`, `game`, `options`, `video`, `multimonitor`,
  `performance`, `accessibility`, `sound`, `railtrail`, `effects`,
  `crosshair`, `screen`, `language`, `downloads`, `download_status`, `input`,
  and `quit_confirm`.
- Wave B single-player/local-session: `gameflags`, `startserver`,
  `singleplayer`, `skill_select`, `loadgame`, and `savegame`.

Initial statuses:

- `scaffolded_first_round`: `main`, `game`, `options`, `video`,
  `singleplayer`.
- `planned_route_metadata_only`: the other 18 Agent 4-owned routes.

## Validation

Performed cheap file-level checks only; runtime load, build, renderer, and
packaging validation remain blocked on the shared RmlUi runtime/build work.

Commands run:

```powershell
$files = @(
  'assets/ui/rml/shell/main.rml',
  'assets/ui/rml/shell/game.rml',
  'assets/ui/rml/shell/options.rml',
  'assets/ui/rml/settings/video.rml',
  'assets/ui/rml/singleplayer/singleplayer.rml'
)
foreach ($file in $files) {
  [xml](Get-Content -Path $file -Raw) | Out-Null
  Write-Output "XML OK $file"
}
```

Result: all 5 starter RML documents parsed as XML-ish markup.

```powershell
$manifest = Get-Content -Path assets/ui/rml/shell/routes.json -Raw | ConvertFrom-Json
```

Result: JSON parsed successfully, route count was 23, and route coverage matched
the full Agent 4-owned list from the roadmap. Status counts were 5
`scaffolded_first_round` and 18 `planned_route_metadata_only`.

Additional checks:

- Duplicate `id` scan passed for all 5 starter RML documents.
- Every `scaffolded_first_round` manifest entry pointed at an existing document:
  `main`, `game`, `options`, `video`, and `singleplayer`.

Shared import presence check:

- `assets/ui/rml/common/theme/base.rcss`: present.
- `assets/ui/rml/common/theme/shell.rcss`: not present yet.
- `assets/ui/rml/common/theme/settings.rcss`: not present yet.
- `assets/ui/rml/common/theme/singleplayer.rcss`: not present yet.
- `assets/ui/rml/common/components/menu_frame.rml`: not present yet.
- `assets/ui/rml/common/components/command_button.rml`: not present yet.
- `assets/ui/rml/common/components/cvar_controls.rml`: not present yet.
- `assets/ui/rml/common/components/selector_list.rml`: not present yet.
- `assets/ui/rml/common/components/save_slot.rml`: not present yet.

## Handoff Notes

- Agent 1/runtime should decide whether per-lane route manifests like
  `assets/ui/rml/shell/routes.json` are loaded directly or aggregated into a
  generated runtime manifest.
- Agent 2/theme should either provide the planned `shell.rcss`,
  `settings.rcss`, and `singleplayer.rcss` files or redirect content agents to
  the final shared stylesheet names before broad translation begins.
- Agent 3/components should bind the placeholder attributes used here:
  `data-command`, `data-route-target`, `data-cvar`, `data-bind-cvar`,
  `data-control`, `data-model`, `data-show-if`, and `data-enable-if`.
- Agent 5 routes referenced from Agent 4 pages are marked with `data-owner` on
  starter buttons where the target is outside this lane.
- Later Agent 4 passes should expand the 18 metadata-only routes and audit the
  content against `src/game/cgame/ui/worr.json` plus the legacy
  `src/client/ui/worr.menu` reference before any JSON route cutover.
