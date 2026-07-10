# RmlUi Agent 3 Runtime Stub Progression Round 8 - 2026-07-02

## Task IDs
- FR-09-T03
- FR-09-T05
- FR-09-T06
- FR-09-T09
- DV-03-T07
- DV-04-T02
- DV-07-T04

## Scope
Round 8 promotes only the guarded menu-open probe routes currently returned by
`UI_Rml_RouteForMenu`:

- `main`
- `game`
- `download_status`

The central smoke manifest at `tools/ui_smoke/rmlui_manifest.json` and the shell
route metadata at `assets/ui/rml/shell/routes.json` now mark those exact route
IDs as `runtime_stub`.

## Rationale
`UI_Rml_RouteForMenu` maps `main`, `game`, and `download_status` to guarded
`UI_Rml_OpenMenu` probing. `UI_Rml_OpenMenu` still falls back to the legacy UI,
so these route promotions record only the guarded engine menu-open document
probe. They do not claim real RmlUi rendering, parity, or cutover readiness.

`options`, settings routes, and all other routes remain at their prior phases
because this round is limited to the route IDs returned by
`UI_Rml_RouteForMenu`.

## Route Metadata Notes
- `main`: promoted from `controller_stub` to `runtime_stub`; existing navigation
  and command controller contracts are preserved.
- `game`: promoted from `controller_stub` to `runtime_stub`; existing
  navigation, command, and condition controller contracts are preserved.
- `download_status`: promoted from `controller_stub` to `runtime_stub`;
  existing cvar, condition, and command controller contracts are preserved.

Expected phase counts after this round, before future work:

- `starter`: 42
- `controller_stub`: 12
- `runtime_stub`: 3
