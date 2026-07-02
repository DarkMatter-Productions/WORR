# Q3A BotLib Min-Player Profile Coverage Scenario Gate - 2026-07-01

Related tasks: `FR-04-T13`, `FR-04-T16`, `DV-07-T06`

## Summary

Promoted the min-player first-party profile rotation fix into a first-class
scenario and release-acceptance gate. The previous runtime fix and headless
play-depth coverage proved the issue in a higher-level CTF run; this slice adds
a narrow, fast server smoke that fails if `bot_min_players` no longer autofills
the full first-party roster through public `bot_*` names.

## Implementation

- Added an autofill profile coverage collector in `src/server/main.c`.
- Extended `bot_min_players_smoke` mode `2` to target five autofill bots and
  emit `q3a_bot_min_players_smoke_profile_coverage` with `bulwark`, `relay`,
  `smoke`, `vanguard`, `vector`, `covered`, and `pass` fields.
- Added the implemented `min_players_profile_coverage` scenario row in
  `tools/bot_scenarios/run_bot_scenarios.py`.
- Added scenario harness tests that require the public
  `bot_min_players_smoke` cvar and reject the old `sv_` spelling.
- Added `min_players_profile_coverage` to release acceptance required scenario
  evidence; focused supplemental reports now satisfy the gate without forcing a
  full scenario-suite rerun.

## Evidence

Build and staged install refresh:

```text
meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

Focused scenario:

```text
python tools\bot_scenarios\run_bot_scenarios.py --scenario min_players_profile_coverage --binary .install\worr_ded_x86_64.exe --install-dir .install --format both --json-out .tmp\bot_scenarios\min_players_profile_coverage.json --markdown-out .tmp\bot_scenarios\min_players_profile_coverage.md --artifact-dir .tmp\bot_scenarios\min_players_profile_coverage_artifacts --timeout 60
```

Result:

- `min_players_profile_coverage` passed in `1.765s`.
- `q3a_bot_min_players_smoke_after_fill` reported `count=5`, `auto=5`,
  `profiled=5`, `target=5`, and `first_profile=bulwark`.
- `q3a_bot_min_players_smoke_profile_coverage` reported `covered=5`,
  `bulwark=1`, `relay=1`, `smoke=1`, `vanguard=1`, `vector=1`, and `pass=1`.
- Trim and disable cleanup reached `count=1` and then `final_count=0`.

Release acceptance:

```text
python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_min_player_profile_scenario.json
python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format text
```

Result: `15/15` checks passed, `0` failed, `0` warnings. The
`scenario_evidence` gate saw `required_scenarios=10` and discovered the new
focused supplemental scenario report.

Unit coverage:

```text
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
python -m unittest tools.bot_release.test_run_bot_acceptance tools.bot_scenarios.test_audit_movement_reference_gaps
python -m unittest discover -s tools\bot_playtest -p "test_*.py"
```

Results: `59`, `21`, and `27` tests passed respectively.

## Notes

This does not mark M3 complete. The actual M3 gate artifact remains pending on
visual review of the generated Duel/CTF play-depth notes. This slice closes the
min-player/profile regression surface and strengthens M8 release readiness.
