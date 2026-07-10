# RmlUi Round 11 Worker 2 - Single-Player Controller Stub Metadata

Date: 2026-07-02

Tasks: FR-09-T05, FR-09-T06, FR-09-T07, FR-09-T09, FR-03-T08, DV-04-T02

## Scope

Promoted exactly four single-player routes from `starter` to `controller_stub`:

- `singleplayer`
- `skill_select`
- `loadgame`
- `savegame`

The central smoke manifest now reports the expected Round 11 Worker 2 phase baseline, assuming no other concurrent phase changes:

- `starter=30`
- `controller_stub=24`
- `runtime_stub=3`

## Metadata Changes

Updated `assets/ui/rml/shell/routes.json` with static controller contract references for the promoted single-player routes:

- `singleplayer`: navigation, command action, cvar binding, condition state, and map database list-provider boundaries.
- `skill_select`: skill command dispatch boundary.
- `loadgame`: load-slot command dispatch and save/load slot-provider boundaries.
- `savegame`: save-slot command dispatch and save/load slot-provider boundaries.

Updated `tools/ui_smoke/rmlui_manifest.json` so the same four routes now report `migration_phase: controller_stub`.

No parity checklist route changes were required. `tools/ui_smoke/rmlui_parity_manifest.json` already tracks all 57 route IDs and derives per-route checklist defaults from the smoke manifest phase.

## Non-Goals

This is metadata/controller-stub readiness only. It does not claim:

- Live campaign, skill, save-game, or load-game controllers.
- Live save-slot discovery, validation, overwrite, or load execution.
- Runtime menu open path for these single-player routes.
- Screenshot, renderer, input, or layout parity.
- Legacy JSON UI removal.

## Validation

Passed:

- `python tools/ui_smoke/check_rmlui_manifest.py`
- `python tools/ui_smoke/check_rmlui_route_contracts.py`
- `python tools/ui_smoke/check_rmlui_controller_stub_coverage.py`
- `python tools/ui_smoke/check_rmlui_controller_fixtures.py`
- `python tools/ui_smoke/check_rmlui_parity_manifest.py`
- `python tools/ui_smoke/report_rmlui_progress.py --format json`
- `python -m pytest tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py`
- `git diff --check -- assets/ui/rml/shell/routes.json tools/ui_smoke/rmlui_manifest.json tools/ui_smoke/rmlui_parity_manifest.json tools/ui_smoke/check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py docs-dev/rmlui-agent2-singleplayer-controller-stubs-round11-2026-07-02.md`

Observed checker facts:

- Smoke manifest: `57` routes, `57` required documents present.
- Smoke manifest phases: `starter=30`, `controller_stub=24`, `runtime_stub=3`.
- Shell metadata: `23` routes, `54` controller contract references.
- Controller-stub coverage: `24` controller-stub routes checked, no missing inferred categories.
- Controller fixtures: `75` route contract references across `8` fixtures, no missing or malformed fixtures.
- Parity checklist: `57` routes, `9` categories, `0` parity-ready routes.
- Progress JSON: `advanced_routes=27`, `advanced_percent=47.4`.
- Focused pytest: `34` tests passed.
