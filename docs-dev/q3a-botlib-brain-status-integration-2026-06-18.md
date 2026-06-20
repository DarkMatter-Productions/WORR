# Q3A BotLib Brain Status Integration - 2026-06-18

Task IDs: FR-04-T03, FR-04-T04, FR-04-T14, FR-04-T15, FR-04-T16, DV-07-T06

## Summary

This round connected the new bot subsystem helper work to the frame-command status surface used by the scenario harness.

Implemented in `src/game/sgame/bots/bot_brain.cpp`:

- Exposed item timer fairness aliases on `q3a_bot_action_status` and `q3a_bot_action_detail_status` (`item_timer_*`, `last_item_timer_*`) while preserving the source-owned `item_timing_policy_*` counters.
- Exposed live aim and projectile lead counters on compact and detailed action status rows.
- Exposed FFA/TDM/CTF objective match-policy, item-role, and friendly-fire counters on `q3a_bot_objective_status`.
- Evaluated frame-level match/item/friendly-fire objective policies after action selection so status rows reflect live bot frame facts without changing route ownership yet.
- Added trace-checked corner-cut counters to the existing `q3a_bot_nav_policy_status` row.
- Added lightweight `q3a_bot_match_readiness_status` and `q3a_bot_coop_readiness_status` rows for future readiness scenarios.
- Kept `q3a_bot_frame_command_status` as the first emitted status row in each dump so `sv_bot_frame_command_smoke_map_repeat` can continue to validate the immediately requested frame status while later rows carry expanded detail telemetry.

The scenario harness was also updated to discover the new match/coop readiness markers as optional team-mode telemetry, including raw reserved-mode diagnostics that scan those markers for optional fields without flattening their generic `pass` values into promotion metrics.

## Validation

- Touched bot objects compiled with `ninja -C builddir-win ...bot_brain.cpp.obj`, `...bot_nav.cpp.obj`, `...bot_combat.cpp.obj`, and `...bot_objectives.cpp.obj`.
- `meson compile -C builddir-win sgame_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --package-q2aas-aas` refreshed `.install`, packed 93 assets, validated 30 botfile package/loose members, and packaged the staged `mm-rage` AAS.
- `python tools\bot_scenarios\test_run_bot_scenarios.py` passed 27 tests.
- `python -m pytest tools\bot_profiles\test_validate_bot_profiles.py tools\test_package_assets.py tools\release\tests\test_target_contract.py` passed 34 tests.
- `python -B -m unittest tools.aas_inventory.test_inventory_aas_assets tools.q2aas.test_validate_worr_q2aas` passed 8 tests.
- `python -B tools\bot_profiles\validate_bot_profiles.py assets\botfiles\bots --format json` passed for 5 profiles.
- Implemented scenario run against `.install` passed 10/10, with the five future promotion rows still pending by design.

## Remaining Work

- Promote the pending item-timer, aim-fairness, FFA/TDM, and coop scenarios once dedicated source-owned smokes exercise nonzero readiness/fairness behavior.
- Expand item timer consumers beyond the current helper/counter path.
- Wire autonomous FFA/TDM/CTF tactical behavior from helper policy into route/action selection.
- Stage broader BSP/reference-map data for strict Q2AAS reference gates beyond `mm-rage`.
