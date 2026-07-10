# RmlUi Parallel Round 2 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `FR-09-T04`,
`FR-09-T05`, `FR-09-T06`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`,
`FR-03-T08`, `DV-03-T07`, `DV-04-T02`, `DV-07-T02`, `DV-07-T04`

## Summary

This integration pass collected the second five-agent RmlUi implementation
round. The round advanced source assets, package staging, shared component
contracts, and starter document coverage. It still does not implement the
RmlUi dependency, C++ runtime, native renderer bridge, live data controllers,
or legacy JSON removal.

## Integrated Work

- Agent 1 updated `tools/package_assets.py` so authored `assets/ui/rml/`
  content is mirrored loose to `.install/<base-game>/ui/rml/` by default and
  validated against the generated archive. Focused package tests now cover the
  new loose staging path.
- Agent 2 added utility, session, and accessibility theme contracts plus theme
  import documentation.
- Agent 3 added keybind and image-grid component contracts, mock data
  contracts, and a minimal route-contract schema.
- Agent 4 added ten shell/settings documents and updated its route metadata.
- Agent 5 added ten utility/session documents, including address book,
  keybinds, weapon bindings, `ui_list`, callvote, MyMap, and leave-match
  starter routes.

## Coordinator Fixes

- Marked the twenty new starter documents as `required_now` with
  `starter_round2` status in `tools/ui_smoke/rmlui_manifest.json`.
- Updated `docs-dev/plans/rmlui-ui-migration-roadmap.md` task statuses and
  evidence for the second round.
- Updated the canonical strategic roadmap with the new package-staging and
  validation state.

## Validation

- `python tools\ui_smoke\check_rmlui_manifest.py`
  - Passed with `57` tracked routes, `30` `required_now` routes, `30/30`
    required documents present, `30` present documents, and `27` pending
    documents.
- JSON/RML/import validation
  - Passed for every JSON and `.rml` file under `assets/ui/rml/` plus the
    central smoke manifest.
- `python -m pytest tools/test_package_assets.py`
  - Passed: `13` tests.
- `python tools\package_assets.py --assets-dir assets --install-dir
  .tmp/rmlui/round2-package-validation --base-game basew --archive-name
  pak0.pkz`
  - Passed with `161` packaged asset files, `31` validated botfile
    package/loose files, and `67` validated RmlUi package/loose files.

## Remaining Gate Work

- Gate G0 still needs the complete active UI entry-point inventory and final
  runtime-owner sign-off.
- Gate G1 still needs RmlUi dependency integration, C++ runtime bootstrap,
  native renderer bridges, and normal build refresh validation.
- Gate G2 still needs live cvar, command, condition, keybind, list/table,
  image-grid, preview, and save/load controllers.
- Gate G3 still needs the remaining `27` manifest routes plus parity checks.
- Gate G4 remains blocked until runtime automation, manual parity validation,
  and legacy JSON removal/archive decisions are complete.
