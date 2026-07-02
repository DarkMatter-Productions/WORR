# Q3A BotLib M3 Headless Play-Depth Runner

Date: 2026-06-30

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Purpose

This slice keeps M3 focused on one concrete blocker: the required Duel and CTF
play-depth notes were still manual-only. The new runner starts those two cases
on the dedicated server, captures stdout/stderr and `botlist` roster output,
and writes prefilled notes that can be reviewed instead of starting from a
blank template.

The runner does not mark a play-depth case as passing. A clean headless run
means the server launched, bots joined, and machine evidence was captured. The
notes remain `pending` until a visual review confirms rotation, objectives,
spacing, retreat, and combat behavior.

## Implementation

- Added `tools/bot_playtest/run_bot_playdepth_headless.py`.
- The runner reads `bot_multiplayer_playtest.json`, selects `duel_rotation` and
  `ctf_objectives` by default, builds dedicated-server commands directly from
  each case's cvars, and captures stdout/stderr under
  `.tmp\bot_playtest\headless\<stamp>\`.
- It writes `bot_playdepth_headless_runs.json`,
  `bot_playdepth_headless_runs.md`, and
  `bot_multiplayer_playtest_headless_notes.json`.
- Roster parsing now uses actual server output such as `Added bot B|...` and
  `num state name` rows, avoiding startup profile-scan chatter.
- Added `tools/bot_playtest/test_run_bot_playdepth_headless.py`.
- Added `playdepth_headless_tooling` to
  `tools/bot_release/run_bot_acceptance.py`. This is a synthetic tooling check
  and intentionally does not launch the server during release acceptance.
- Updated user, playtest, release, roadmap, and provenance documentation.

## Validation

Commands run:

```powershell
python -m unittest discover -s tools\bot_playtest -p "test_*.py"
python -m unittest tools.bot_release.test_run_bot_acceptance tools.bot_scenarios.test_audit_movement_reference_gaps
python tools\bot_playtest\run_bot_playdepth_headless.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --dry-run --format text
python tools\bot_playtest\run_bot_playdepth_headless.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --startup-wait 120 --run-wait 120 --timeout 90 --format text
python tools\bot_playtest\build_bot_playdepth_evidence.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --notes .tmp\bot_playtest\headless\20260630T195219Z\bot_multiplayer_playtest_headless_notes.json --format text
python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_m3_headless.json
python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format text
```

Results:

- Playtest unit suite: 24 tests passed.
- Release/audit unit suite: 21 tests passed.
- Real short headless run passed both required cases and wrote
  `.tmp\bot_playtest\headless\20260630T195219Z\bot_playdepth_headless_runs.json`.
- Evidence built from the headless notes correctly remains `pending` with both
  required play-depth cases awaiting visual review.
- Release acceptance passes with `checks=15`, `passed=15`, `failed=0`, and
  `warnings=0`.

## Follow-Up

Use the generated headless notes as the starting point for the live Duel and CTF
review. After the operator changes both required outcomes to `pass`, rebuild
`bot_duel_ctf_playdepth_evidence.*` and rerun `check_m3_multiplayer_gate.py`.
