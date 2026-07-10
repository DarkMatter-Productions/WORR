# RmlUi Parallel Round 1 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `FR-09-T04`,
`FR-09-T05`, `FR-09-T06`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`,
`FR-03-T08`, `DV-03-T07`, `DV-04-T02`, `DV-07-T02`, `DV-07-T04`

## Summary

This integration pass collected the five first-round RmlUi subagent slices and
aligned their metadata, shared imports, validation status, and roadmap tracking.
It remains source-asset scaffolding only. No RmlUi dependency, C++ runtime,
renderer bridge, build wiring, `.install/` staging, or legacy JSON removal was
implemented in this round.

## Integrated Work

- Agent 1 added the core runtime smoke route and route metadata under
  `assets/ui/rml/core/`.
- Agent 2 added the base shared theme and font staging contract under
  `assets/ui/rml/common/`.
- Agent 3 added component templates and mock data contracts under
  `assets/ui/rml/common/components/` and `assets/ui/rml/contracts/`.
- Agent 4 added first starter documents for `main`, `game`, `options`,
  `video`, and `singleplayer`, plus Agent 4 route metadata.
- Agent 5 added starter documents for `servers`, `demos`, `players`,
  `multiplayer`, and `vote_menu`, plus the first smoke manifest checker.

## Coordinator Fixes

- Marked the five Agent 4 starter documents as `required_now` and `starter` in
  `tools/ui_smoke/rmlui_manifest.json`, raising first-round manifest coverage
  from five to ten required documents.
- Aligned Agent 4 `downloads` and `download_status` route metadata with the
  central smoke manifest paths under `assets/ui/rml/shell/`.
- Added minimal shared import placeholder files for planned Agent 4 imports:
  `shell.rcss`, `settings.rcss`, `singleplayer.rcss`, `menu_frame.rml`,
  `command_button.rml`, `cvar_controls.rml`, `selector_list.rml`, and
  `save_slot.rml`.
- Added shared base theme/component imports to the Agent 5 starter documents.
- Updated the RmlUi roadmap and canonical strategic roadmap with round-one
  active/blocked status and validation evidence.

## Validation

- `python tools\ui_smoke\check_rmlui_manifest.py`
  - Passed with `57` tracked routes, `10` `required_now` routes, `10/10`
    required documents present, `10` present documents, and `47` pending
    documents.
- `python -m py_compile tools\ui_smoke\check_rmlui_manifest.py`
  - Passed.
- Parsed every JSON file under `assets/ui/rml/` plus
  `tools/ui_smoke/rmlui_manifest.json`.
  - Passed.
- Parsed every `.rml` file under `assets/ui/rml/` as XML-ish markup and
  verified local `href` imports resolve.
  - Passed.

## Remaining Gate Work

- Gate G0 still needs the full active UI entry-point inventory and final
  runtime-owner sign-off.
- Gate G1 still needs the real RmlUi dependency, C++ runtime bootstrap,
  renderer bridge, and `.install/` staging.
- Gate G2 still needs live cvar/command/condition/list/preview controllers.
- Gate G3 still needs full Wave A/B/C translation and parity checks.
- Gate G4 remains blocked until runtime validation, manual parity validation,
  and legacy JSON removal/archive decisions are complete.
