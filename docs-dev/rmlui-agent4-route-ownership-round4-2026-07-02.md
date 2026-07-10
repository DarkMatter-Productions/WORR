# RmlUi Agent 4 Route Ownership Round 4

Date: 2026-07-02

Worker: 4

Tasks: `FR-09-T01`, `FR-09-T06`, `FR-09-T07`, `FR-09-T08`, `FR-03-T08`

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Scope

This pass adds route ownership/current-system metadata to the Agent 4-owned
RmlUi route manifests without changing any route IDs or document paths. The
central smoke manifest under `tools/ui_smoke/` was intentionally not edited;
Worker 2 owns the round-4 progression pass there.

The metadata maps each route to:

- the current legacy/source menu or surface name,
- the current RmlUi surface name,
- the source behavior owner,
- the shared migration phase,
- the relevant project task IDs,
- the intended controller/data bridge scope.

## Phase Vocabulary

The route metadata uses the Worker 2-aligned phase names:

- `starter`
- `controller_stub`
- `runtime_stub`
- `parity_pending`
- `parity_ready`

All core/shell routes in this pass are marked `starter`. That is intentional:
all source RML documents exist, but the live RmlUi runtime, native controller
bridge, route-opening evidence, and parity evidence are still pending.

## Changed Files

- `assets/ui/rml/contracts/route-ownership.schema.json`
- `assets/ui/rml/core/routes.json`
- `assets/ui/rml/shell/routes.json`
- `docs-dev/rmlui-agent4-route-ownership-round4-2026-07-02.md`

## Manifest Changes

`assets/ui/rml/contracts/route-ownership.schema.json` records the additive
ownership metadata contract and phase vocabulary. It is supplemental to the
existing route-contract checker, so current smoke/contract validation remains
compatible.

`assets/ui/rml/core/routes.json` now references the ownership contract and adds
the ownership fields to `core.runtime_smoke`.

`assets/ui/rml/shell/routes.json` now references the ownership contract and adds
consistent ownership fields to all 23 Agent 4 routes:

- `legacy_surface`
- `current_surface`
- `source_owner`
- `migration_phase`
- `task_ids`
- `controller_scope`

Task mapping notes:

- Shell/settings/single-player routes carry `FR-09-T01`, `FR-09-T06`, and
  `FR-03-T08`.
- Download and save/load routes also carry `FR-09-T07`.
- `gameflags` and `startserver` also carry `FR-09-T08` because they feed local
  session/match setup behavior that must stay aligned with the session lane.

## Validation

Commands run:

```powershell
$paths = @(
  'assets/ui/rml/contracts/route-ownership.schema.json',
  'assets/ui/rml/core/routes.json',
  'assets/ui/rml/shell/routes.json'
)
foreach ($path in $paths) {
  $json = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
  Write-Output "JSON OK $path"
}
$core = Get-Content -LiteralPath assets/ui/rml/core/routes.json -Raw | ConvertFrom-Json
$shell = Get-Content -LiteralPath assets/ui/rml/shell/routes.json -Raw | ConvertFrom-Json
$required = @('legacy_surface','current_surface','source_owner','migration_phase','task_ids','controller_scope')
foreach ($manifest in @($core,$shell)) {
  foreach ($route in $manifest.routes) {
    foreach ($field in $required) {
      if (-not $route.PSObject.Properties.Name.Contains($field)) {
        throw "Missing $field on route $($route.id)"
      }
    }
  }
}
Write-Output "Ownership field coverage OK core=$($core.routes.Count) shell=$($shell.routes.Count)"
$shell.routes | Group-Object migration_phase | Sort-Object Name | ForEach-Object {
  Write-Output "Shell migration_phase $($_.Name) $($_.Count)"
}
```

Result:

- JSON parsed for the ownership schema, core route manifest, and shell route
  manifest.
- Ownership field coverage passed for `1` core route and `23` shell routes.
- Shell phase count: `starter=23`.

Additional validation:

```powershell
python tools\ui_smoke\check_rmlui_route_contracts.py
python tools\ui_smoke\check_rmlui_manifest.py
```

Results:

- Route contract audit passed for core, shell, and smoke manifests.
- Smoke manifest check passed with `57` routes, `57` required documents, `57`
  present documents, `151` parsed RML/import files, and `213` local imports
  checked.

## Known Gaps

- The new ownership schema is descriptive; no dedicated validator consumes it
  yet.
- Phase remains `starter` until runtime route opening, live controller stubs,
  and parity evidence land in later lanes.
- No document paths, route IDs, or central smoke manifest entries were changed
  in this pass.
