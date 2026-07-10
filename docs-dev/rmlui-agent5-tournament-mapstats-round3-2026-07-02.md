# RmlUi Agent 5 Tournament, Map Selector, and Match Stats Round 3

Date: 2026-07-02

Tasks: `FR-09-T08`, `FR-09-T09`, `FR-07-T02`, `DV-03-T07`

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Summary

This third Agent 5 slice adds scaffold-level RML documents for the remaining
tournament, replay, end-of-match map selector, and match stats session routes.
The documents preserve the cvar, command, command-cvar, close-command, and
future model/controller placeholders exposed by the current JSON menus and
sgame menu publishers.

No central smoke manifest edits were made. The coordinator can integrate route
status in `tools/ui_smoke/rmlui_manifest.json` after round-3 reconciliation.

## Changed Files

- `assets/ui/rml/session/tourney_info.rml`
- `assets/ui/rml/session/tourney_mapchoices.rml`
- `assets/ui/rml/session/tourney_veto.rml`
- `assets/ui/rml/session/tourney_replay_confirm.rml`
- `assets/ui/rml/session/map_selector.rml`
- `assets/ui/rml/session/match_stats.rml`
- `docs-dev/rmlui-agent5-tournament-mapstats-round3-2026-07-02.md`

## Contract Notes

- `tourney_info` keeps the `worr_tourney_info` open-command placeholder,
  static tournament guidance, the `tourney_status` console command reference,
  and `popmenu` back behavior.
- `tourney_mapchoices` keeps `worr_tourney_maps`, all ten
  `ui_tourney_mapchoice_line_0` through `ui_tourney_mapchoice_line_9` cvar
  bindings, and `popmenu`.
- `tourney_veto` keeps the `ui_tourney_veto_*` inactive, turn, wait, pick, ban,
  picks-needed, and maps-remaining gates plus the `worr_tourney_pick` and
  `worr_tourney_ban` commands.
- `tourney_replay_confirm` keeps `ui_tourney_replay_prompt`,
  `ui_tourney_replay_yes_cmd` as a command-cvar slot, and `popmenu` cancel
  behavior.
- `map_selector` keeps `ui_mapselector_title`, the three
  `ui_mapselector_option_*` labels and show cvars, `worr_mapselector_vote 0`
  through `worr_mapselector_vote 2`, acknowledgement cvars, countdown bar, and
  `popmenu; worr_mapselector_close`.
- `match_stats` keeps the sixteen `ui_matchstats_line_*` cvars,
  `worr_matchstats_menu` open-command placeholder, live-refresh model hook, and
  `popmenu; worr_matchstats_close`.

## Validation

XML-ish validation for the six new RML documents:

```powershell
$files = @(
  'assets/ui/rml/session/tourney_info.rml',
  'assets/ui/rml/session/tourney_mapchoices.rml',
  'assets/ui/rml/session/tourney_veto.rml',
  'assets/ui/rml/session/tourney_replay_confirm.rml',
  'assets/ui/rml/session/map_selector.rml',
  'assets/ui/rml/session/match_stats.rml'
)
foreach ($file in $files) { [xml](Get-Content -Path $file -Raw) | Out-Null }
```

Result: pass for all `6/6` new RML documents.

## Handoff Notes

- Agent 3/shared bridge work should map `data-label-cvar`,
  `data-command-cvar`, `data-visible-if`, `data-enabled-if`, and
  `data-close-command` consistently with the existing utility/session
  scaffolds.
- The session bridge should treat `data-session-owner="sgame"` and
  `data-owner-boundary="sgame-published-cvars"` as explicit ownership markers:
  these documents do not claim live RmlUi parity yet.
- Central manifest status remains intentionally untouched in this lane.
