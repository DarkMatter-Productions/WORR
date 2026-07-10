# RmlUi Round 12 Worker 1 Local-Session Controller Stubs

Date: 2026-07-02

Task IDs: FR-09-T01, FR-09-T06, FR-09-T07, FR-09-T08, FR-03-T08

## Scope

Worker 1 promoted exactly four remaining shell/local-session routes from `starter` to `controller_stub`:

- `downloads`
- `quit_confirm`
- `gameflags`
- `startserver`

This is static controller-stub metadata only. It does not implement or claim live download behavior, quit confirmation behavior, gameflags bitfield binding, start-server behavior, RmlUi runtime open paths, screenshots, parity, or legacy menu replacement.

## Metadata Changes

Updated `assets/ui/rml/shell/routes.json`:

- `downloads`
  - Phase: `controller_stub`
  - Contracts: `cvar_binding`, `command_action`, `navigation`
  - Static evidence: download permission `data-cvar` toggles plus back/close `data-command` hooks.
- `quit_confirm`
  - Phase: `controller_stub`
  - Contracts: `command_action`, `navigation`
  - Static evidence: `quit` and `popmenu` confirmation `data-command` hooks.
- `gameflags`
  - Phase: `controller_stub`
  - Contracts: `cvar_binding`, `command_action`, `navigation`
  - Static evidence: `dmflags` `data-cvar`, `data-bit`, and `data-negate` toggle metadata plus back/close `data-command` hooks.
- `startserver`
  - Phase: `controller_stub`
  - Contracts: `cvar_binding`, `condition_state`, `command_action`, `navigation`
  - Static evidence: local-session cvars, `data-show-if` condition expressions, gameflags route target, begin-game command, and back/close hooks.

Updated `tools/ui_smoke/rmlui_manifest.json` to mirror the same four route phase changes.

## Validation

Required smoke checks passed:

- `python tools\ui_smoke\check_rmlui_manifest.py`
  - Routes: `57` total, `57` required, `57` present
  - Phases: `starter=26`, `controller_stub=28`, `runtime_stub=3`
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
  - Shell routes: `23`
  - Shell controller contracts: `66`
  - Manifest phases: `starter=26`, `controller_stub=28`, `runtime_stub=3`
- `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`
  - Controller-stub routes checked: `28`
  - Missing categories: `none`
- `python tools\ui_smoke\check_rmlui_controller_fixtures.py`
  - Route contract refs: `87`
  - Fixtures referenced/present: `8/8`
  - Missing fixtures: `0`
  - Malformed contract refs: `0`
- `python tools\ui_smoke\check_rmlui_parity_manifest.py`
  - Routes checked: `57`
  - Parity-ready routes: `0`
  - Phases: `starter=26`, `controller_stub=28`, `runtime_stub=3`
- `python tools\ui_smoke\check_rmlui_phase_consistency.py`
  - Metadata-backed advanced routes: `31`
  - Missing evidence: `none`
- `python tools\ui_smoke\report_rmlui_progress.py --format json`
  - Advanced routes: `31`
  - Advanced percent: `54.4`
  - Controller contract refs: `87`
  - Data-model malformed tokens: `0`

Focused pytest passed:

- `python -m pytest tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_phase_consistency.py tools/ui_smoke/test_report_rmlui_progress.py`
  - Result: `52 passed`

`git diff --check -- owned files` is expected to be run after this log is written.

## Follow-Up Boundaries

Remaining work for later rounds:

- Wire live controllers for download toggles, quit confirmation, gameflags bitfields, and start-server launch behavior.
- Add runtime route opening evidence before any runtime-stub promotion.
- Capture renderer and screenshot parity evidence before any parity-ready claim.
- Keep legacy JSON menu fallback until runtime and parity evidence are complete.
