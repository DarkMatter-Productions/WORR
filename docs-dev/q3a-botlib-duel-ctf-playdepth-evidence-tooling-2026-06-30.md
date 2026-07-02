# Q3A BotLib Duel/CTF Play-Depth Evidence Tooling

Date: 2026-06-30

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice implements the release-evidence layer for the roadmap's Duel and CTF live-server play-depth passes. It does not claim that a manual playtest has passed; instead, it adds the tooling that turns filled operator notes for the required `duel_rotation` and `ctf_objectives` cases into a compact JSON/Markdown attachment and makes the release acceptance runner prove that attachment path stays functional.

## Implementation

- Added `tools/bot_playtest/build_bot_playdepth_evidence.py`.
- The new tool reads `bot_multiplayer_playtest.json` plus operator notes, reuses `triage_bot_playtest.py` for scenario-candidate classification, and writes `bot_duel_ctf_playdepth_evidence.json` plus `bot_duel_ctf_playdepth_evidence.md`.
- The default required play-depth cases are `duel_rotation` and `ctf_objectives`; pending, skipped, failed, and blocked outcomes remain visible in the attachment instead of being flattened into a release-ready claim.
- Added `--required-case`, `--repeat-threshold`, `--strict`, text, and JSON CLI support so release candidates can either collect pending evidence or require a fully passing Duel/CTF pass.
- Added `playdepth_evidence_tooling` to `tools/bot_release/run_bot_acceptance.py`. The check builds a synthetic passing Duel/CTF note set in memory and verifies the release attachment schema, required cases, markdown coverage, and botlist accounting.
- Updated `docs-user/bot-playtest.md`, `tools/bot_playtest/README.md`, and `tools/bot_release/README.md` with the new release-evidence workflow.

## Validation

- `python -m unittest discover -s tools\bot_playtest -p "test_*.py"`
- `python -m unittest tools.bot_release.test_run_bot_acceptance`
- `python tools\bot_playtest\generate_bot_playtest.py --output-dir .tmp\bot_playtest`
- `python tools\bot_playtest\build_bot_playdepth_evidence.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --notes .tmp\bot_playtest\bot_multiplayer_playtest_notes_template.json --format text`
- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_playdepth_evidence.json`
- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format text`

The focused playtest suite reports 15 passing tests. The release acceptance test module reports 13 passing tests. The generated default evidence attachment is pending until real Duel/CTF notes are filled, as expected. The release acceptance dry run reports `checks=13`, `passed=13`, `failed=0`, and `warnings=0`.

## Notes

No imported source changed in this slice. The work is WORR-native release tooling and documentation that prepares the next actual live-server Duel/CTF play-depth run.
