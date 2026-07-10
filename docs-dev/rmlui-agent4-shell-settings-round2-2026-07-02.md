# RmlUi Agent 4 Shell/Settings Round 2

Date: 2026-07-02

Tasks: `FR-09-T06`, `FR-09-T07`, `FR-09-T04`, `FR-09-T09`

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Scope

This pass adds the second Agent 4-owned shell/settings batch as scaffold-level
RmlUi documents. The goal is not full menu parity yet; the documents preserve
representative legacy command and cvar names from
`src/game/cgame/ui/worr.json` while keeping the markup simple enough for the
future shared controller bridge.

The central smoke manifest under `tools/ui_smoke/` was intentionally not edited
in this lane. `assets/ui/rml/shell/routes.json` remains the Agent 4 route
status source for this pass.

## Changed Files

- `assets/ui/rml/settings/multimonitor.rml`
- `assets/ui/rml/settings/performance.rml`
- `assets/ui/rml/settings/accessibility.rml`
- `assets/ui/rml/settings/sound.rml`
- `assets/ui/rml/settings/screen.rml`
- `assets/ui/rml/settings/language.rml`
- `assets/ui/rml/settings/input.rml`
- `assets/ui/rml/shell/downloads.rml`
- `assets/ui/rml/shell/download_status.rml`
- `assets/ui/rml/shell/quit_confirm.rml`
- `assets/ui/rml/shell/routes.json`
- `docs-dev/rmlui-agent4-shell-settings-round2-2026-07-02.md`

## Route Status

`assets/ui/rml/shell/routes.json` now declares `scaffolded_round2` and marks
these ten routes with that status:

- `multimonitor`
- `performance`
- `accessibility`
- `sound`
- `screen`
- `language`
- `downloads`
- `download_status`
- `input`
- `quit_confirm`

Current Agent 4 route status counts after this pass:

- `scaffolded_first_round`: 5
- `scaffolded_round2`: 10
- `planned_route_metadata_only`: 8

## Preserved Starter Bindings

- `multimonitor`: `r_monitor_mode`, `r_borderless`, `r_display`
- `performance`: `cl_maxfps`, `r_maxfps`, `cl_predict`, `cl_footsteps`,
  `cl_warn_on_fps_rounding`, `cl_async`
- `accessibility`: `ui_high_visibility_text`, `ui_text_typeface`
- `sound`: `s_enable`, `s_mixahead`, `s_volume`, `ogg_volume`, `ogg_enable`,
  `ogg_shuffle`, `s_underwater`, `s_ambient`, `cl_chat_sound`
- `screen`: `viewsize`, `cg_lagometer`, `cl_demobar`, `cl_alpha`,
  `con_alpha`, `cl_scale`, `con_scale`, `ui_scale`, `pushmenu crosshair`
- `language`: `loc_language`
- `input`: `sensitivity`, `m_autosens`, `m_filter`, `freelook`, `cl_run`
- `downloads`: `allow_download`, `allow_download_maps`,
  `allow_download_players`, `allow_download_models`, `allow_download_sounds`,
  `allow_download_textures`, `allow_download_pics`, `cl_http_downloads`
- `download_status`: `ui_download_file`, `ui_download_status`,
  `ui_download_percent`, `ui_download_queue`, `download_cancel`
- `quit_confirm`: `quit`, `popmenu`

## Validation

Performed cheap file-level checks only. Runtime loading, renderer behavior,
input handling, cvar persistence, and screenshot/layout validation still depend
on the shared runtime/component work.

Command run:

```powershell
$files = @(
  'assets/ui/rml/settings/multimonitor.rml',
  'assets/ui/rml/settings/performance.rml',
  'assets/ui/rml/settings/accessibility.rml',
  'assets/ui/rml/settings/sound.rml',
  'assets/ui/rml/settings/screen.rml',
  'assets/ui/rml/settings/language.rml',
  'assets/ui/rml/settings/input.rml',
  'assets/ui/rml/shell/downloads.rml',
  'assets/ui/rml/shell/download_status.rml',
  'assets/ui/rml/shell/quit_confirm.rml'
)
foreach ($file in $files) {
  [xml](Get-Content -Path $file -Raw) | Out-Null
  Write-Output "XML OK $file"
}
$routes = Get-Content -Path assets/ui/rml/shell/routes.json -Raw | ConvertFrom-Json
Write-Output "JSON OK assets/ui/rml/shell/routes.json"
Write-Output "Route count $($routes.routes.Count)"
$routes.routes | Group-Object status | Sort-Object Name | ForEach-Object { Write-Output "Status $($_.Name) $($_.Count)" }
$expected = @('multimonitor','performance','accessibility','sound','screen','language','downloads','download_status','input','quit_confirm')
$bad = @($routes.routes | Where-Object { $expected -contains $_.id } | Where-Object { $_.status -ne 'scaffolded_round2' })
if ($bad.Count -gt 0) {
  throw "Round2 status mismatch: $($bad.id -join ', ')"
}
foreach ($id in $expected) {
  $route = $routes.routes | Where-Object { $_.id -eq $id }
  $path = Join-Path 'assets/ui/rml' $route.document
  if (-not (Test-Path $path)) {
    throw "Missing RML for route ${id}: $path"
  }
}
Write-Output "Round2 route status/document check OK $($expected.Count)"
```

Result:

- XML-ish parse passed for all 10 new RML documents.
- `assets/ui/rml/shell/routes.json` parsed successfully.
- Route count remained 23.
- Status counts were 5 `scaffolded_first_round`, 10 `scaffolded_round2`, and
  8 `planned_route_metadata_only`.
- All 10 round 2 routes pointed at existing RML documents and had
  `scaffolded_round2` status.

## Known Gaps

- These are static starter documents. Live cvar binding, command dispatch,
  condition evaluation, focus navigation, and localization binding are still
  Agent 1/2/3 runtime/component follow-ups.
- The `screen` page links to `crosshair`, which is still metadata-only in this
  round.
- `download_status` preserves the current dynamic `ui_download_*` cvars and
  `download_cancel` close command, but the exact auto-open/auto-close runtime
  route contract still needs the S0/runtime decision.
