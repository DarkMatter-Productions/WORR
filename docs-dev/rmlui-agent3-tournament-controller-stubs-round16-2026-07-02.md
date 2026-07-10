# RmlUi Agent 3 Tournament Controller-Stub Hardening Round 16

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, `FR-07-T02`,
`DV-03-T07`, and `DV-04-T02`.

## Scope

Round 16 Worker 3 hardened the remaining tournament starter RML documents for
static `controller_stub` metadata integration:

- `assets/ui/rml/session/tourney_info.rml`
- `assets/ui/rml/session/tourney_mapchoices.rml`
- `assets/ui/rml/session/tourney_veto.rml`
- `assets/ui/rml/session/tourney_replay_confirm.rml`

No shared route metadata was edited in this worker. In particular,
`assets/ui/rml/session/routes.json` and `tools/ui_smoke/rmlui_manifest.json`
were left for coordinator integration.

## Static Hook Coverage

- `tourney_info`: keeps the static tournament guidance, `popmenu` back
  command, and now exposes the `tourney_status` console reference through an
  explicit static `data-command` hook. It should not need a `cvar_binding` or
  `condition_state` claim unless a future tournament-info status cvar is added.
- `tourney_mapchoices`: now declares each
  `ui_tourney_mapchoice_line_0` through `ui_tourney_mapchoice_line_9` line as
  an explicit `data-label-cvar`, preserves each `data-visible-if` gate, keeps
  the `popmenu` command, and marks the fixed line stack with
  `data-list-provider="session.tournament.mapchoices.lines"`.
- `tourney_veto`: now declares the inactive, turn, wait, picks-needed, and
  maps-remaining labels as explicit `data-label-cvar` hooks, preserves the
  existing visibility gates for inactive, turn, waiting, pick, ban, and locked
  states, keeps `worr_tourney_pick`, `worr_tourney_ban`, and `popmenu`
  command hooks, and marks the repeated veto-state rows with
  `data-list-provider="session.tournament.veto.maps"`.
- `tourney_replay_confirm`: now declares `ui_tourney_replay_prompt` as an
  explicit `data-label-cvar`, preserves its visibility gate, keeps
  `ui_tourney_replay_yes_cmd` as the command-cvar confirmation slot, and keeps
  `popmenu` for cancel.

This prepares the coordinator metadata to claim `command_action`,
`cvar_binding`, `condition_state`, and list-provider categories only where the
RML now has matching static hooks.

## No-Live-Runtime Caveat

This is no-live-runtime controller-stub preparation only. It does not add live
RmlUi tournament controllers, live cvar propagation, live command dispatch,
runtime route opening, renderer integration, screenshot/layout evidence,
parity proof, legacy JSON removal, q2proto changes, or any Vulkan-to-OpenGL
renderer redirection.

## Validation

Commands run:

```powershell
$files = @('assets/ui/rml/session/tourney_info.rml','assets/ui/rml/session/tourney_mapchoices.rml','assets/ui/rml/session/tourney_veto.rml','assets/ui/rml/session/tourney_replay_confirm.rml'); foreach ($file in $files) { [xml](Get-Content -Path $file -Raw) | Out-Null }; "XML validation passed for $($files.Count) RML files."
python tools\ui_smoke\check_rmlui_semantics.py
python tools\ui_smoke\check_rmlui_command_inventory.py
python tools\ui_smoke\check_rmlui_cvar_inventory.py
python tools\ui_smoke\check_rmlui_condition_inventory.py
python tools\ui_smoke\report_rmlui_progress.py --format json
python tools\ui_smoke\check_rmlui_controller_stub_coverage.py
python tools\ui_smoke\check_rmlui_controller_fixtures.py
python tools\ui_smoke\check_rmlui_metadata_sync.py
```

Results:

- XML validation passed for all `4` touched RML files.
- Static semantics passed with `57` routes known, `57` documents checked,
  `290` command elements checked, and `494` cvar references checked.
- Command inventory passed with `290` direct command refs, `15` cvar-command
  refs, `71` unique command tokens, `15` unique cvar-command refs, and `0`
  malformed command attributes.
- Cvar inventory passed with `255` direct cvar refs, `71` label cvar refs,
  `15` command cvar refs, `153` condition cvar refs, `494` total refs, and
  `0` bad tokens.
- Condition inventory passed with `144` total condition refs, `22` routes with
  condition hooks, `111` unique condition tokens/cvars, `0` unsupported
  non-static conditions, and `0` malformed condition attributes.
- Progress JSON in this checkout reported `57` total routes, `57` present
  documents, `0` missing documents, phase counts `controller_stub=54` and
  `runtime_stub=3`, and `149` controller-contract refs. That reflects the
  current shared metadata state already present in the workspace; this worker
  did not edit the shared metadata files.
- Controller-stub coverage passed with `5` route metadata files checked, `54`
  controller-stub routes checked, inferred categories
  `navigation=11`, `command_action=54`, `cvar_binding=28`,
  `condition_state=20`, `list_provider=4`, and `keybind=3`, with missing
  categories `none`.
- Controller fixture audit passed with `5` route metadata files, `58` routes
  checked, `57` routes with controller contracts, `149` contract refs, `9`
  referenced fixtures, and `0` missing or malformed fixtures.
- Metadata sync passed with `57` central routes, `5` metadata files, `58`
  metadata routes, `57` matched routes, `0` central routes without feature
  metadata, `0` phase mismatches, `0` document mismatches, and `0` duplicate
  route IDs.
