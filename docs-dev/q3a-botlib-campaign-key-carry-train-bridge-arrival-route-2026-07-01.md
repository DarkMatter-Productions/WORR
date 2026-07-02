# Q3A BotLib Campaign Key Carry Train Bridge Arrival Route - 2026-07-01

Tasks: FR-04-T04, FR-04-T05, FR-04-T15, DV-03-T05, DV-07-T06

## Summary

The `coop_campaign_key_carry_train` proof no longer uses the temporary
post-mover bridge-arrival warp. The key-carry smoke slot now stores the
routeable bridge-arrival projection from the live `func_train` interaction and
holds that projected point as a normal position route goal until the bot reaches
it. Only then does the proof advance to the final `trigger_key` lock route.

## Implementation Notes

- Replaced the old `Bot_CommandMaybeWarpKeyCarrySmokeToBridgeArrival()` path
  with `Bot_CommandResolveKeyCarrySmokeBridgeArrival()` plus a route/reach
  latch.
- Added bridge-arrival route telemetry:
  `key_carry_bridge_arrival_route_requests`,
  `key_carry_bridge_arrival_reached`, and
  `last_key_carry_bridge_arrival_distance_sq`.
- Kept `key_carry_bridge_arrival_warps` as a regression counter and changed
  the scenario gate to require it to stay at `0`.
- Preserved the existing live train interaction and key-lock proof chain:
  the bot still resolves the live train entity, activates the mover, resolves a
  post-mover arrival candidate, then routes the lock leg without either bridge
  or lock-side proof warps.

## Validation

Focused scenario:

```text
python tools/bot_scenarios/run_bot_scenarios.py --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --scenario coop_campaign_key_carry_train --timeout 90 --artifact-dir .tmp/bot_scenarios --format both --json-out .tmp/bot_scenarios/coop_campaign_key_carry_train.json --markdown-out .tmp/bot_scenarios/coop_campaign_key_carry_train.md
```

Artifact: `.tmp\bot_scenarios\20260701T145703Z`

Key result:

```text
pass=1
frames=121
route_commands=61
route_failures=0
key_carry_bridge_approach_ready=1
key_carry_bridge_warps=0
key_carry_bridge_interactions=1
key_carry_bridge_arrival_requests=1
key_carry_bridge_arrival_resolved=1
key_carry_bridge_arrival_route_requests=2
key_carry_bridge_arrival_reached=1
key_carry_bridge_arrival_warps=0
last_key_carry_bridge_arrival_distance_sq=112
key_carry_lock_route_requests=42
key_carry_lock_warps=0
last_key_carry_lock_required_item=70
```

Additional checks:

```text
python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
meson compile -C builddir-win sgame_x86_64
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python -m unittest tools.bot_release.test_run_bot_acceptance
python tools/bot_scenarios/run_bot_scenarios.py --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --scenario implemented --timeout 90 --artifact-dir .tmp/bot_scenarios --format text --json-out .tmp/bot_scenarios/implemented_after_bridge_arrival_route.json --markdown-out .tmp/bot_scenarios/implemented_after_bridge_arrival_route.md
python tools/bot_release/run_bot_acceptance.py --install-dir .install --base-game basew --format text --output .tmp/bot_scenarios/bot_acceptance_after_bridge_arrival_route_current.txt
```

The full implemented scenario suite passed `123/123` from
`.tmp\bot_scenarios\20260701T145735Z`, and the final release acceptance audit
passed `15/15` with `scenario_evidence` using
`.tmp\bot_scenarios\implemented_after_bridge_arrival_route.json`.
