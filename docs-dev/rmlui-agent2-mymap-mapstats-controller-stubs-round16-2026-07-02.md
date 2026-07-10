# RmlUi Agent 2 MyMap Mapstats Controller Stub Hardening Round 16

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, `FR-07-T01`,
`FR-07-T02`, `FR-03-T08`, `DV-03-T07`, and `DV-04-T02`.

## Summary

Prepared the remaining MyMap, map-selector, and match-stats starter RML
documents for later static `controller_stub` metadata. This slice only hardens
the authored RML hooks; it does not edit `assets/ui/rml/session/routes.json`,
`tools/ui_smoke/rmlui_manifest.json`, or shared route phase counts.

The task-assigned Round 15 baseline was:

- `starter=12`
- `controller_stub=42`
- `runtime_stub=3`

During validation, concurrent unowned edits had already advanced the local
shared route/manifest files to `controller_stub=54` and `runtime_stub=3`.
This worker did not edit those shared JSON files.

No live runtime, live C++ controller, runtime open path, screenshot/layout
evidence, parity evidence, or legacy menu replacement is claimed. No `q2proto`,
renderer, or Vulkan path files were touched.

## Static Hook Coverage

- `mymap_main`
  - `command_action`: `worr_mymap_select`, `worr_mymap_flags`,
    `worr_mymap_clear`, and `popmenu` remain explicit `data-command` hooks.
  - `navigation`: the Flags control keeps the static
    `data-route-target="mymap_flags"` route edge.
  - `cvar_binding`: status and flag summary text now expose
    `data-bind-cvar` hooks for `ui_mymap_status` and
    `ui_mymap_flags_summary`.
  - `condition_state`: status/summary visibility stays on `data-visible-if`;
    select, flags, and clear controls now also expose recognized
    `data-enable-if` gates while preserving the existing `data-enabled-if`
    wording.

- `mymap_flags`
  - `command_action`: all fixed flag toggles and the Back control already
    expose static `data-command` hooks.
  - `cvar_binding`: all fixed flag labels already expose static
    `data-label-cvar` hooks for the `ui_mymap_flag_*` cvars.
  - No navigation, condition-state, or list-provider category should be claimed
    for this fixed flag-command set.

- `map_selector`
  - `command_action`: the three vote buttons and Close control remain explicit
    `data-command` hooks.
  - `cvar_binding`: title, option labels, acknowledgement lines, and countdown
    bar now have direct cvar hooks through `data-bind-cvar` or
    `data-label-cvar`.
  - `condition_state`: title, option rows, acknowledgement panel, and countdown
    bar keep explicit `data-visible-if` gates.
  - `list_provider`: the fixed candidate map option group now declares
    `data-list-provider="session.map_selector.options"`.

- `match_stats`
  - `command_action`: Return remains an explicit close command hook.
  - `cvar_binding`: all sixteen stat lines now expose direct
    `data-bind-cvar="ui_matchstats_line_*"` hooks.
  - `condition_state`: all sixteen stat lines keep explicit `data-visible-if`
    gates.
  - `list_provider`: the fixed stat-line panel now declares
    `data-list-provider="session.match_stats.lines"`.

## Handoff Notes

The coordinator can later add route-local `controller_contracts` for these
routes without inventing categories that are not visible in the RML. Expected
future category claims are:

- `mymap_main`: `navigation`, `command_action`, `cvar_binding`, and
  `condition_state`.
- `mymap_flags`: `command_action` and `cvar_binding`.
- `map_selector`: `command_action`, `cvar_binding`, `condition_state`, and
  `list_provider`.
- `match_stats`: `command_action`, `cvar_binding`, `condition_state`, and
  `list_provider`.

## Validation

Passed:

```powershell
python tools\ui_smoke\check_rmlui_semantics.py
python tools\ui_smoke\check_rmlui_command_inventory.py
python tools\ui_smoke\check_rmlui_cvar_inventory.py
python tools\ui_smoke\check_rmlui_condition_inventory.py
python tools\ui_smoke\check_rmlui_data_model_inventory.py
python tools\ui_smoke\check_rmlui_route_contracts.py
python tools\ui_smoke\check_rmlui_navigation_graph.py
python -m pytest tools\ui_smoke\test_check_rmlui_semantics.py tools\ui_smoke\test_check_rmlui_command_inventory.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_condition_inventory.py tools\ui_smoke\test_check_rmlui_data_model_inventory.py tools\ui_smoke\test_check_rmlui_route_contracts.py
$files = @('assets/ui/rml/session/mymap_main.rml','assets/ui/rml/session/mymap_flags.rml','assets/ui/rml/session/map_selector.rml','assets/ui/rml/session/match_stats.rml','docs-dev/rmlui-agent2-mymap-mapstats-controller-stubs-round16-2026-07-02.md'); $bad = Select-String -LiteralPath $files -Pattern '[ \t]+$'; if ($bad) { $bad | ForEach-Object { "{0}:{1}: trailing whitespace" -f $_.Path, $_.LineNumber }; exit 1 } else { 'No trailing whitespace in touched RML/docs.' }
git diff --check -- assets/ui/rml/session/mymap_main.rml assets/ui/rml/session/mymap_flags.rml assets/ui/rml/session/map_selector.rml assets/ui/rml/session/match_stats.rml docs-dev/rmlui-agent2-mymap-mapstats-controller-stubs-round16-2026-07-02.md
```

Results:

- Static semantics passed with `57` documents checked, `53` route targets,
  `290` command elements, and `494` cvar references.
- Command inventory passed with `290` direct command refs, `15`
  cvar-command refs, and `57` routes with command hooks.
- Cvar inventory passed with `255` direct cvar refs, `71` label cvar refs,
  `153` condition cvar refs, `282` unique cvars, and no bad tokens.
- Condition inventory passed with `144` condition refs and no malformed
  conditions.
- Data-model inventory passed with `190` total model/data-binding refs and no
  malformed tokens.
- Route contracts passed across `57` smoke routes and all discovered feature
  route metadata files.
- Navigation graph passed with `53` route-target references and `0` unknown
  targets.
- Focused pytest passed with `34 passed`.
- The direct whitespace scan reported no trailing whitespace in touched
  RML/docs.
- `git diff --check --` returned clean.

Coordinator follow-up:

```powershell
python tools\ui_smoke\check_rmlui_controller_stub_coverage.py
```

Result: the coordinator reconciled the shared route metadata after this worker
finished. `assets/ui/rml/session/routes.json` now includes the missing
`cvar_binding` controller-contract categories for `mymap_main`, `match_stats`,
`tourney_mapchoices`, and `tourney_veto`; controller-stub coverage passes in
the integrated Round 16 workspace.
