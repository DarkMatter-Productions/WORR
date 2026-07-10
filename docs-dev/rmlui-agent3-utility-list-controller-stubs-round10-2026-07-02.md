# RmlUi Agent 3 Utility List Controller Stubs Round 10 - 2026-07-02

## Task IDs
- FR-09-T05
- FR-09-T07
- FR-09-T09
- FR-03-T08
- DV-04-T02

## Scope
Round 10 Worker 3 promotes exactly four additional Agent 5-owned utility/list
routes from `starter` to `controller_stub` in
`tools/ui_smoke/rmlui_manifest.json`:

- `servers`
- `demos`
- `players`
- `ui_list`

The new `assets/ui/rml/utility/routes.json` entries record
controller-stub readiness for these routes using existing mock fixtures under
`assets/ui/rml/contracts`.

## Route Metadata
- `servers`: declares `command_action` and `list_provider` contracts for
  refresh, connect, favorites, back, table data, sorting, selection, and status
  wiring.
- `demos`: declares `command_action` and `list_provider` contracts for parent
  directory, refresh, playback, back, table data, sorting, directory entries,
  and status wiring.
- `players`: declares `cvar_binding`, `command_action`, and `preview`
  contracts for name/model/skin/hand bindings, apply/reset/back actions, and
  future player preview surface ownership.
- `ui_list`: declares `cvar_binding`, `command_action`, `condition_state`, and
  `list_provider` contracts for the cvar-published title, labels, item rows,
  visibility flags, paging commands, and close command.

## Progression
The central manifest phase counts after this worker are:

- `starter`: 34
- `controller_stub`: 20
- `runtime_stub`: 3

The progress reporter now shows `23` advanced routes (`40.4%`).

## Validation
Commands run:

```powershell
python tools/ui_smoke/check_rmlui_manifest.py
python tools/ui_smoke/check_rmlui_route_contracts.py
python tools/ui_smoke/check_rmlui_controller_stub_coverage.py
python tools/ui_smoke/check_rmlui_controller_fixtures.py
python tools/ui_smoke/report_rmlui_progress.py --format json
```

Results:

- Manifest check passed with `57` routes present and phase counts
  `starter=34`, `controller_stub=20`, `runtime_stub=3`.
- Route contract audit passed and discovered
  `assets/ui/rml/utility/routes.json` with `8` utility routes and `21`
  utility controller contract references.
- Controller-stub coverage passed across `3` route metadata files with `20`
  controller-stub routes checked and no missing categories.
- Controller fixture audit passed across `3` route metadata files, `32` routes,
  `23` routes with controller contracts, `65` contract references, and `7`
  referenced fixtures.
- Progress JSON reported `57` total routes, `34` starter routes, `20`
  controller-stub routes, `3` runtime-stub routes, `23` advanced routes, and
  `40.4` advanced percent.

## Caveat
This is metadata/controller-stub readiness only. It does not add live server,
demo, player, or `ui_list` controllers; live list providers; live preview
controllers; runtime open paths; screenshots; parity evidence; or legacy UI
removal.
