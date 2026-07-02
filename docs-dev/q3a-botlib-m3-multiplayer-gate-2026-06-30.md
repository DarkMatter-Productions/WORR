# Q3A BotLib M3 Multiplayer Gate

Date: 2026-06-30

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds a concrete completion gate for M3 Multiplayer Mode Intelligence. The gate combines the required automated multiplayer scenario baseline with the Duel/CTF play-depth evidence attachment, so M3 can be judged as `passed`, `pending`, or `failed` from one artifact instead of remaining vaguely "in progress."

## Implementation

- Added `tools/bot_playtest/check_m3_multiplayer_gate.py`.
- The gate requires these automated scenario rows to pass: `duel_queue_spectator`, `tdm_role_spawn_stability`, `ffa_live_pacing`, `duel_live_pacing`, `ctf_objective_route`, and `ctf_objective_transitions`.
- The gate also requires the play-depth evidence attachment to contain passing `duel_rotation` and `ctf_objectives` cases.
- Missing, failed, or duration-budget-failed automated rows make the gate fail.
- Failed or blocked play-depth cases make the gate fail; pending or skipped play-depth cases keep the gate pending.
- Added JSON/Markdown output under `.tmp/bot_playtest` as `bot_m3_multiplayer_gate.json` and `bot_m3_multiplayer_gate.md`.
- Added `m3_multiplayer_gate_tooling` to `tools/bot_release/run_bot_acceptance.py`. This check uses synthetic passing data to verify the gate machinery without claiming the real pending play-depth notes are complete.
- Updated user and tool docs with the M3 gate workflow.

## Validation

- `python -m unittest discover -s tools\bot_playtest -p "test_*.py"`
- `python -m unittest tools.bot_release.test_run_bot_acceptance tools.bot_scenarios.test_audit_movement_reference_gaps`
- `python tools\bot_playtest\check_m3_multiplayer_gate.py --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --playdepth-evidence .tmp\bot_playtest\bot_duel_ctf_playdepth_evidence.json --format text`
- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_m3_gate.json`
- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format text`

The focused playtest suite reports 19 passing tests after this slice. The combined release/audit unit run reports 20 passing tests. With current template play-depth notes, the real M3 gate reports `pending` with all six automated scenarios passing and both required play-depth cases pending. The release acceptance dry run reports `checks=14`, `passed=14`, `failed=0`, and `warnings=0`.

## Notes

No imported source changed in this slice. The work is WORR-native milestone tooling and documentation. M3 should only be marked complete after the generated Duel and CTF play-depth cases are actually run, notes are filled, and this gate reports `passed`.
