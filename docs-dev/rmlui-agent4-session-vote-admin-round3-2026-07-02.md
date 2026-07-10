# RmlUi Agent 4 Session Vote Admin Round 3

Date: 2026-07-02

Tasks: `FR-09-T08`, `FR-09-T09`, `FR-07-T01`, `FR-07-T02`,
`DV-03-T07`

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Summary

This Agent 4 round-three lane adds scaffold-level RML documents for the
remaining standard multiplayer session, vote, and admin routes assigned in the
round-three work packet. The documents preserve the current cgame JSON menu
commands and the sgame-published `ui_*` cvar placeholders while marking session
ownership boundaries for the future RmlUi data bridge.

No central smoke manifest edits were made. `tools/ui_smoke/rmlui_manifest.json`
is intentionally left for the coordinator integration pass.

## Changed Files

- `assets/ui/rml/session/dm_welcome.rml`
- `assets/ui/rml/session/dm_join.rml`
- `assets/ui/rml/session/join.rml`
- `assets/ui/rml/session/dm_hostinfo.rml`
- `assets/ui/rml/session/dm_matchinfo.rml`
- `assets/ui/rml/session/callvote_timelimit.rml`
- `assets/ui/rml/session/callvote_scorelimit.rml`
- `assets/ui/rml/session/callvote_unlagged.rml`
- `assets/ui/rml/session/callvote_random.rml`
- `assets/ui/rml/session/callvote_map_flags.rml`
- `assets/ui/rml/session/forfeit_confirm.rml`
- `assets/ui/rml/session/admin_menu.rml`
- `assets/ui/rml/session/admin_commands.rml`
- `docs-dev/rmlui-agent4-session-vote-admin-round3-2026-07-02.md`

## Contract Notes

- `dm_welcome` keeps `ui_welcome_title`, `ui_welcome_hostname`,
  `ui_welcome_motd`, and `forcemenuoff; worr_welcome_continue`.
- `dm_join` and `join` keep the `ui_dm_*` cvars plus the current team,
  tournament, callvote, MyMap, host info, forfeit, match info, stats, admin,
  settings, and leave-match commands.
- `dm_hostinfo` keeps `ui_hostinfo_server`, `ui_hostinfo_host`, and
  `ui_hostinfo_motd`.
- `dm_matchinfo` keeps the `ui_matchinfo_*` cvar placeholders published by
  `OpenDmMatchInfoMenu`.
- `callvote_timelimit`, `callvote_scorelimit`, `callvote_unlagged`,
  `callvote_random`, and `callvote_map_flags` keep the current
  `worr_callvote_*` command arguments and related `ui_callvote_*` label cvars.
- `forfeit_confirm` keeps `worr_forfeit_yes` and `popmenu`.
- `admin_menu` keeps `ui_admin_show_replay`, `worr_tourney_replay_menu`, and
  `pushmenu admin_commands`.
- `admin_commands` keeps the static admin command reference text from the
  current JSON menu, with usage tokens escaped for XML-ish parsing.
- All new routes import the shared base, session, and accessibility themes and
  use `data-session-owner="sgame"` plus route-local owner boundary attributes.

## Validation

Ran XML-ish parsing on the 13 new RML documents with PowerShell:

```powershell
$files = @(
  'assets/ui/rml/session/dm_welcome.rml',
  'assets/ui/rml/session/dm_join.rml',
  'assets/ui/rml/session/join.rml',
  'assets/ui/rml/session/dm_hostinfo.rml',
  'assets/ui/rml/session/dm_matchinfo.rml',
  'assets/ui/rml/session/callvote_timelimit.rml',
  'assets/ui/rml/session/callvote_scorelimit.rml',
  'assets/ui/rml/session/callvote_unlagged.rml',
  'assets/ui/rml/session/callvote_random.rml',
  'assets/ui/rml/session/callvote_map_flags.rml',
  'assets/ui/rml/session/forfeit_confirm.rml',
  'assets/ui/rml/session/admin_menu.rml',
  'assets/ui/rml/session/admin_commands.rml'
)
foreach ($file in $files) {
  [xml](Get-Content -Path $file -Raw) | Out-Null
  Write-Output "XML OK $file"
}
Write-Output "XML-ish pass $($files.Count)/$($files.Count)"
```

Result: pass for all `13/13` new RML documents.

## Handoff Notes

- Coordinator should add these routes to the central smoke manifest/status pass.
- Runtime/controller work still needs to define final parsing for multi-cvar
  `data-visible-if` placeholders such as
  `ui_dm_show_join=1;ui_dm_teamplay=1`.
- These are static starter documents only; live command dispatch, cvar binding,
  conditional visibility, localization binding, and focus behavior remain in
  the shared RmlUi bridge follow-up tasks.
