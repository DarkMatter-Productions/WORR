# RmlUi Agent 3 Shell/Single-Player Round 3

Date: 2026-07-02

Tasks: `FR-09-T06`, `FR-09-T07`, `FR-09-T09`

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Scope

This pass adds scaffold-level RML documents for the remaining Agent 4-owned
settings and single-player routes that were still metadata-only after round 2.
The central smoke manifest under `tools/ui_smoke/` was intentionally not
edited; the coordinator owns that integration.

The new documents preserve the current command, cvar, bitfield, and slot names
from `src/game/cgame/ui/worr.json` where practical. Save/load pages also keep
the legacy menu command shape in `data-legacy-menu-command` while exposing the
runtime save/load command target on `data-command`.

## Changed Files

- `assets/ui/rml/settings/railtrail.rml`
- `assets/ui/rml/settings/effects.rml`
- `assets/ui/rml/settings/crosshair.rml`
- `assets/ui/rml/singleplayer/gameflags.rml`
- `assets/ui/rml/singleplayer/startserver.rml`
- `assets/ui/rml/singleplayer/skill_select.rml`
- `assets/ui/rml/singleplayer/loadgame.rml`
- `assets/ui/rml/singleplayer/savegame.rml`
- `assets/ui/rml/shell/routes.json`
- `docs-dev/rmlui-agent3-shell-singleplayer-round3-2026-07-02.md`

## Route Status

`assets/ui/rml/shell/routes.json` now declares `scaffolded_round3` and marks
these eight routes with that status:

- `railtrail`
- `effects`
- `crosshair`
- `gameflags`
- `startserver`
- `skill_select`
- `loadgame`
- `savegame`

Current Agent 4 route status counts after this pass:

- `scaffolded_first_round`: 5
- `scaffolded_round2`: 10
- `scaffolded_round3`: 8

## Preserved Starter Bindings

- `railtrail`: `cl_railtrail_type`, `cl_railtrail_time`,
  `cl_railcore_width`, `cl_railspiral_radius`, `cl_railcore_color`,
  `cl_railspiral_color`
- `effects`: `gl_dynamic`, `gl_celshading`, `cl_noglow`, `gl_shadows`,
  `gl_polyblend`, `gl_waterwarp`, `cg_weapon_bob`,
  `cl_disable_explosions` bits 0 and 1, `pushmenu railtrail`
- `crosshair`: `crosshair`, `cl_crosshair_size`,
  `cl_crosshair_brightness`, `cl_crosshair_color`,
  `cl_crosshair_health`, `cl_crosshair_hit_style`,
  `cl_crosshair_hit_color`, `cl_crosshair_hit_time`,
  `cl_crosshair_pulse`
- `gameflags`: `dmflags` bits 0, 1, 2, 3, 4, 5, 9, 10, 11, 13, 14, and 15,
  including negate metadata from the JSON source
- `startserver`: `_ui_nextserver`, `coop`, `hostname`,
  `match_setup_format`, `match_setup_gametype`, `match_setup_modifier`,
  `match_setup_maxplayers`, `match_setup_length`, `match_setup_type`,
  `match_setup_bestof`, `timelimit`, `fraglimit`, `maxclients`,
  `pushmenu gameflags`, and the full `begin game!` command
- `skill_select`: easy, medium, hard, and nightmare command strings
- `loadgame`: `save0` through `save15` load slots
- `savegame`: `save1` through `save15` save slots

## Validation

Performed file-level validation only. Runtime loading, focus navigation,
condition evaluation, live cvar persistence, and screenshot/layout parity still
depend on the shared RmlUi runtime and component bridge work.

Command run:

```powershell
$files = @(
  'assets/ui/rml/settings/railtrail.rml',
  'assets/ui/rml/settings/effects.rml',
  'assets/ui/rml/settings/crosshair.rml',
  'assets/ui/rml/singleplayer/gameflags.rml',
  'assets/ui/rml/singleplayer/startserver.rml',
  'assets/ui/rml/singleplayer/skill_select.rml',
  'assets/ui/rml/singleplayer/loadgame.rml',
  'assets/ui/rml/singleplayer/savegame.rml'
)
foreach ($file in $files) {
  [xml](Get-Content -LiteralPath $file -Raw) | Out-Null
  Write-Output "XML OK $file"
}
$routes = Get-Content -LiteralPath assets/ui/rml/shell/routes.json -Raw | ConvertFrom-Json
Write-Output "JSON OK assets/ui/rml/shell/routes.json"
Write-Output "Route count $($routes.routes.Count)"
$routes.routes | Group-Object status | Sort-Object Name | ForEach-Object { Write-Output "Status $($_.Name) $($_.Count)" }
$expected = @('railtrail','effects','crosshair','gameflags','startserver','skill_select','loadgame','savegame')
$bad = @($routes.routes | Where-Object { $expected -contains $_.id } | Where-Object { $_.status -ne 'scaffolded_round3' })
if ($bad.Count -gt 0) { throw "Round3 status mismatch: $($bad.id -join ', ')" }
foreach ($id in $expected) {
  $route = $routes.routes | Where-Object { $_.id -eq $id }
  $path = Join-Path 'assets/ui/rml' $route.document
  if (-not (Test-Path -LiteralPath $path)) { throw "Missing RML for route ${id}: $path" }
}
Write-Output "Round3 route status/document check OK $($expected.Count)"
```

Result:

- XML-ish parse passed for all eight new RML documents.
- `assets/ui/rml/shell/routes.json` parsed successfully.
- Route count remained 23.
- Status counts were 5 `scaffolded_first_round`, 10 `scaffolded_round2`, and
  8 `scaffolded_round3`.
- All eight round 3 routes pointed at existing RML documents and had
  `scaffolded_round3` status.

## Known Gaps

- These are static starter documents, not complete runtime menu ports.
- `showIf`, `enableIf`, bitfield toggle behavior, image grid population, and
  save/load slot metadata still need the shared component/controller bridge.
- The central smoke manifest still needs coordinator integration for these
  eight newly scaffolded routes.
