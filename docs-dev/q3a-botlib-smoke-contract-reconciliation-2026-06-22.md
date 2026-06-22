# Q3A BotLib Smoke Contract Reconciliation

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

The expanded 76-row automated bot scenario catalog exposed ordering-sensitive
proof telemetry and stale marker expectations after the mode `71`
`combat_survival_regression` promotion. The behavior implementation was already
producing the intended live decisions in focused runs, but the aggregate suite
could lose positive proof values later in the run or fail older rows that now
had more precise route-specific status markers.

This round reconciles those smoke contracts and establishes a green full
automated `implemented` baseline:

`.tmp\bot_scenarios\implemented_after_next_round_stable_green\20260622T182201Z`

That run reports 76 passed rows, 0 failed rows, 0 timeouts, 0 errors, and
0 pending rows.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` now emits compact FFA roam-route and TDM
  role-route proof status lines so route-specific proof rows do not depend on
  the longer aggregate status line staying under print-buffer limits.
- The brain status path now reports aim-fairness proof failure as `none` once
  the policy allows fire, preventing later incidental blocked samples from
  overwriting the successful proof tail.
- The team-fire smoke path now lets mode `34` create immediate attack intent
  before friendly-line suppression vetoes it, which proves the avoidance gate
  without broadening all enemy-engagement smoke modes.
- The smoke combat classifier no longer treats weapon switch, weapon scoring,
  aim/fire policy, or survival regression helper modes as generic attack-only
  scenarios.
- `src/game/sgame/bots/bot_objectives.cpp` now preserves last-positive
  profile-derived bonus telemetry for match and item-role policy fields. Later
  aggregate samples can still update counters, but they no longer erase the
  positive profile evidence required by the profile policy proof rows.
- `tools/bot_scenarios/test_run_bot_scenarios.py` no longer requires stale CTF
  objective-detail markers where the route-specific status markers are now the
  authoritative proof surface.

## Validation

- `ninja -C builddir-win sgame_x86_64.dll` passed. Ninja continued to print
  its existing `premature end of file; recovering` warning.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed and refreshed the local staged payload.
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py` passed.
- `pytest tools\bot_scenarios\test_run_bot_scenarios.py` passed 45 tests.
- Focused scenario checks passed for `profile_item_policy`,
  `team_fire_avoidance`, `ffa_roam_route`, `team_role_route`, and
  `aim_fairness_policy_integration`.
- Full automated aggregate validation passed:
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --install-dir .install --game basew --artifact-dir .tmp\bot_scenarios\implemented_after_next_round_stable_green --format text --json-out .tmp\bot_scenarios\implemented_after_next_round_stable_green.json --timeout 45`

## Follow-Up

There are no default pending automated bot scenario rows after this round. The
next work should use the green 76/76 catalog as the regression baseline while
promoting deeper live behavior: second-map combat/item regression,
threat-retreat/avoidance, live CTF objective loops, TDM role stability, coop
live loops, movement/hazard coverage, and fresh source-counter soaks.
