# RmlUi Agent 3 Utility Controller Stubs Round 9 - 2026-07-02

## Task IDs
- FR-09-T05
- FR-09-T07
- FR-09-T09
- FR-03-T08
- DV-04-T02

## Scope
Round 9 Worker 3 promotes exactly four Agent 5-owned utility routes from
`starter` to `controller_stub` in `tools/ui_smoke/rmlui_manifest.json`:

- `addressbook`
- `keys`
- `legacykeys`
- `weapons`

The new `assets/ui/rml/utility/routes.json` metadata records controller-stub
readiness for these routes using existing mock fixtures from
`assets/ui/rml/contracts`.

## Route Metadata
- `addressbook`: declares `cvar_binding`, `command_action`, and `navigation`
  contracts for static `adr0`-`adr15` fields plus browse/back controls.
- `keys`: declares `keybind`, `command_action`, and `navigation` contracts for
  static key-capture hooks plus the legacy-keys/back controls.
- `legacykeys`: declares `keybind` and `command_action` contracts for static
  legacy key-capture hooks plus the back control.
- `weapons`: declares `keybind` and `command_action` contracts for static
  weapon key-capture hooks plus the back control.

## Validator Changes
`tools/ui_smoke/check_rmlui_route_contracts.py` now discovers additional
feature route metadata files under `assets/ui/rml/*/routes.json`, so utility
route metadata is audited alongside the existing core, shell, and smoke
manifests.

`tools/ui_smoke/check_rmlui_controller_stub_coverage.py` now indexes route
metadata from multiple feature folders. It preserves the existing shell path
compatibility option while defaulting to all discovered route metadata files.
The static RML attribute inference now covers `keybind`, `list_provider`, and
`preview` categories where applicable.

## Progression
Expected central manifest phase counts after this worker:

- `starter`: 38
- `controller_stub`: 16
- `runtime_stub`: 3

These counts are metadata progression only. This round does not add live
utility controllers, key-capture behavior, address-book behavior, runtime open
paths, renderer evidence, screenshots, parity validation, or legacy UI removal.

## Validation
Commands run:

```powershell
python -m pytest tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py
python tools/ui_smoke/check_rmlui_manifest.py
python tools/ui_smoke/check_rmlui_route_contracts.py
python tools/ui_smoke/check_rmlui_controller_stub_coverage.py
python tools/ui_smoke/report_rmlui_progress.py --format json
```

Results:

- Focused pytest passed: `13` tests.
- Manifest check passed with `57` routes present and phase counts
  `starter=38`, `controller_stub=16`, `runtime_stub=3`.
- Route contract audit passed and discovered
  `assets/ui/rml/utility/routes.json` with `4` utility routes and `10`
  controller contract references.
- Controller-stub coverage passed across `3` route metadata files with `16`
  controller-stub routes checked and no missing categories.
- Progress JSON reported `57` total routes, `38` starter routes, `16`
  controller-stub routes, `3` runtime-stub routes, `19` advanced routes, and
  `33.3` advanced percent.

## Caveat
`report_rmlui_progress.py` still summarizes controller-contract references
from the shell route metadata path only. Its manifest phase progression is
correct for this worker, but a later reporting pass should make the controller
contract summary consume all discovered feature route metadata.
