# RmlUi Agent 5 Utility and Session Round 2

Date: 2026-07-02

Tasks: `FR-09-T07`, `FR-09-T08`, `FR-09-T09`, `FR-07-T01`,
`FR-07-T02`, `DV-03-T07`

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Summary

This second Agent 5 slice adds scaffold-level RML documents for the next owned
utility and multiplayer-session routes. The documents preserve current cvar,
bind, and command names from the cgame JSON menus and sgame menu publishers,
while naming the future RmlUi model/controller placeholders needed by the
runtime and shared-component lanes.

No central smoke manifest or checker edits were made in this lane. The
coordinator can integrate these routes into `tools/ui_smoke/rmlui_manifest.json`
once round-2 ownership/status is reconciled.

## Changed Files

- `assets/ui/rml/utility/addressbook.rml`
- `assets/ui/rml/utility/keys.rml`
- `assets/ui/rml/utility/legacykeys.rml`
- `assets/ui/rml/utility/weapons.rml`
- `assets/ui/rml/utility/ui_list.rml`
- `assets/ui/rml/session/callvote_main.rml`
- `assets/ui/rml/session/callvote_ruleset.rml`
- `assets/ui/rml/session/mymap_main.rml`
- `assets/ui/rml/session/mymap_flags.rml`
- `assets/ui/rml/session/leave_match_confirm.rml`
- `docs-dev/rmlui-agent5-utility-session-round2-2026-07-02.md`

## Contract Notes

- `addressbook` keeps the `adr0` through `adr15` cvar field contract and the
  existing address-book server browser command:
  `pushmenu servers "favorites://" "file:///servers.lst" "broadcast://"`.
- `keys`, `legacykeys`, and `weapons` preserve the legacy bind command strings
  as `data-bind-command` values for the future key-capture controller.
- `ui_list` keeps the sgame-published `ui_list_*` cvars, the two extra
  `commandCvar` slots, twelve fixed item slots, pagination commands, and the
  close command `popmenu; worr_ui_list_close`.
- `callvote_main` keeps the `ui_callvote_show_*` visibility cvars and
  `worr_callvote_*` commands published/opened by `menu_page_callvote.cpp`.
- `callvote_ruleset` keeps `worr_callvote_ruleset q1`, `q2`, and `q3`.
- `mymap_main` and `mymap_flags` keep the `ui_mymap_*` cvars and
  `worr_mymap_*` command family from `menu_page_mymap.cpp`.
- `leave_match_confirm` keeps localization-key placeholders and the current
  `forcemenuoff; disconnect` / `popmenu` commands.

## Validation

Ran XML-ish parsing on the new RML files with PowerShell:

```powershell
$files = @(
  'assets/ui/rml/utility/addressbook.rml',
  'assets/ui/rml/utility/keys.rml',
  'assets/ui/rml/utility/legacykeys.rml',
  'assets/ui/rml/utility/weapons.rml',
  'assets/ui/rml/utility/ui_list.rml',
  'assets/ui/rml/session/callvote_main.rml',
  'assets/ui/rml/session/callvote_ruleset.rml',
  'assets/ui/rml/session/mymap_main.rml',
  'assets/ui/rml/session/mymap_flags.rml',
  'assets/ui/rml/session/leave_match_confirm.rml'
)
foreach ($file in $files) { [xml](Get-Content -Path $file -Raw) | Out-Null }
```

Result: pass for all `10/10` new RML documents.

## Handoff Notes

- Agent 3 should map `data-bind-command`, `data-label-cvar`, and
  `data-command-cvar` to the shared keybind and command/cvar controllers.
- Runtime/session bridge work should treat `data-owner-boundary` and
  `data-session-owner="sgame"` as explicit placeholders: these pages still
  depend on sgame-published cvars and commands.
- Route status was intentionally not integrated into the central smoke manifest
  in this lane.
