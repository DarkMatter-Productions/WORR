# Q3A BotLib Release Acceptance Post-Crouch Reference

Date: 2026-06-30

Tasks: `FR-04-T16`, `DV-07-T06`

## Summary

This slice hardens the bot release acceptance runner after the natural-crouch and hazard-context promotions. The release gate no longer accepts the old `movement_hazard_context_gap` row, now requires the promoted `movement_crouch_route` and `movement_hazard_context` evidence, verifies the expanded eleven-map staged AAS set, checks that the WORR-authored crouch reference BSP is staged for runtime map loading, and adds a dedicated movement-reference audit gate.

## Implementation

- Updated `tools/bot_release/run_bot_acceptance.py` so required AAS coverage includes `worr_crouch_ref`, `q2dm7`, and `fact2` beside the existing release maps.
- Added a staged reference-BSP check for `worr_crouch_ref.bsp` under `.install/<base_game>/maps`.
- Replaced the stale required scenario `movement_hazard_context_gap` with promoted `movement_crouch_route` and `movement_hazard_context`.
- Added supplemental scenario-report discovery for required rows that were promoted after the last full aggregate run. The gate keeps the aggregate report as the primary evidence while allowing focused, clean post-promotion reports to satisfy newly required scenario names.
- Added `movement_reference_audit` acceptance coverage for `.tmp/bot_scenarios/movement_reference_gap_audit.json`, requiring `status=accepted`, zero blocked checks, and accepted `natural_crouch` / `hazard_context` rows mapped to the promoted scenarios.
- Added the `--movement-reference-audit` CLI option and unit coverage for supplemental scenario evidence, failed supplemental rows, and audit blockers.

## Validation

- `python -m unittest tools.bot_release.test_run_bot_acceptance tools.bot_scenarios.test_audit_movement_reference_gaps`
- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_post_crouch_reference.json`
- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format text`

The focused unit suites report 18 passing tests. The acceptance dry run reports `checks=12`, `passed=12`, `failed=0`, and `warnings=0`. The staged AAS gate reports `required_maps=11`, `present_maps=11`, `required_reference_bsps=1`, and `present_reference_bsps=1`; the movement-reference audit gate reports `checks=2`, `blocked=0`, and `accepted=2`.

## Notes

No imported source changed in this slice. The work is WORR-native release tooling and documentation, aligned with the post-build acceptance dry-run item in the bot completion roadmap.
