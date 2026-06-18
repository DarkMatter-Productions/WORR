# Q3A BotLib Profile Scenario Smoke

Date: 2026-06-18

Tasks: `FR-04-T13`, `DV-03-T05`

## Summary

This slice adds scenario-harness coverage for the existing dedicated-server
profile smoke path without changing profile assets, profile validation tooling,
or game/source code.

The new `profile_backed_spawn` scenario runs from `.install` by setting
`sv_bot_profile_smoke 2` on the dedicated server. It verifies stdout markers
from the server-owned profile smoke instead of `q3a_bot_frame_command_status`,
because profile loading and userinfo bridging are not frame-command scenarios.

## Harness Changes

- Added per-scenario smoke cvar selection so existing frame-command scenarios
  keep using `sv_bot_frame_command_smoke`, while `profile_backed_spawn` uses
  `sv_bot_profile_smoke`.
- Extended marker parsing to retain string and decimal values from marker rows.
  This lets the harness validate fields such as `profile=smoke`,
  `skin=male/grunt`, `aggression=0.65`, and
  `preferred_weapon=rocketlauncher`.
- Tightened marker matching so `q3a_bot_profile_smoke_after_add` does not also
  consume `q3a_bot_profile_smoke_after_add_request`.
- Allowed marker-only scenarios to pass or fail on marker checks without
  requiring a `q3a_bot_frame_command_status` line.
- Added catalog and command tests for the profile scenario, plus parser coverage
  for the profile stdout marker shape.

## Profile Contract

When the staged install contains a resolvable `smoke` profile, normally loaded
from `botfiles/bots/smoke_c.c`, the scenario expects these marker proofs:

- `q3a_bot_profile_smoke=begin profiles>=1 found=1`
- `q3a_bot_profile_smoke_after_add_request added=1 count=1`
- `q3a_bot_profile_smoke_after_add count=1 name=B|Smoke profile=smoke skin=male/grunt skill=4 reaction=250 aggression=0.65 aim_error=2.5 preferred_weapon=rocketlauncher chat=quiet role=attacker movement=strafe`
- `q3a_bot_profile_smoke_after_remove_all count=0`
- `q3a_bot_profile_smoke=end final_count=0`

## Validation

Command:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed.

Command:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed, `11` tests run.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --catalog --scenario profile_backed_spawn --format text --json-out .tmp\bot_scenarios\profile_catalog_report.json
```

Result: passed. The catalog reports one implemented scenario using
`sv_bot_profile_smoke=2` with marker checks for profile load, add request,
bridged userinfo fields, and cleanup.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario profile_backed_spawn --timeout 60 --base-port 28200 --format text --json-out .tmp\bot_scenarios\profile_backed_spawn_report.json --markdown-out .tmp\bot_scenarios\profile_backed_spawn_report.md
```

Initial result: failed as expected in the then-current staged install because no
smoke profile asset was present. The dedicated server exited cleanly with return
code `0`, and the harness captured `profiles=0 found=0`.

Follow-up result after native profile assets and loose `botfiles` staging:
passed. The refreshed install loaded 5 profiles, resolved `smoke`, spawned
`B|Smoke`, verified all profile/userinfo fields, and removed all bots.

Follow-up result after reshaping the profile pack to Q3-style
`*_c/_w/_i/_t.c` botfiles: passed. The loader strips `_c` from profile entry
points, skips `_w/_i/_t` companions, resolved `smoke` from `smoke_c.c`, and
preserved the same marker contract.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario spawn_route_to_item,recover_from_stall,multi_bot_reservation,map_change_repeat --timeout 120 --base-port 28210 --format text --json-out .tmp\bot_scenarios\implemented_navigation_after_profile_report.json --markdown-out .tmp\bot_scenarios\implemented_navigation_after_profile_report.md
```

Result: passed. The four pre-existing implemented scenarios still pass:
`spawn_route_to_item`, `recover_from_stall`, `multi_bot_reservation`, and
`map_change_repeat`.

## Remaining Risks

- The harness verifies the server marker contract and userinfo bridge fields,
  but it does not create or validate profile assets.
- Running `--scenario implemented` now includes the profile scenario, so stale
  installs that were not refreshed after botfile staging will report the profile
  asset gap.
