# Q3A BotLib CTF Objective Route Precedence

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round makes the default-off CTF objective route policy compose cleanly with the older default-off CTF role-route owner. When `sg_bot_ctf_role_route` and `sg_bot_ctf_objective_route` are enabled together, the generic CTF role-route policy now records a deliberate objective-route deferral instead of activating a timed role route that would consume the route request first.

The behavior keeps focused `ctf_role_route` ownership unchanged when only `sg_bot_ctf_role_route` is enabled, while letting the more specific objective-route policy own flag routes when both route owners are available.

## Runtime Changes

- Added `ctf_role_route_objective_deferrals` frame-command status to record role-route deferrals caused by the active objective-route policy.
- Updated `Bot_CommandActivateCtfRoleRoute()` so valid CTF role policy is still counted, then the role-route owner exits before activating a timed route when `sg_bot_ctf_objective_route` is active.
- Added server smoke mode `41` for the precedence proof. Mode `41` runs as four-bot CTF, enables both `sg_bot_ctf_role_route` and `sg_bot_ctf_objective_route`, and emits `ctf_objective_route_precedence=1` in the scenario begin marker.
- Added `ctf_role_route_objective_deferrals` to optional scenario status discovery under the existing `ctf_role_route_counters` family.
- Hardened the adjacent four-bot CTF carrier-support and base-return smoke setup so it seeds carrier flag inventory in place after team readiness instead of teleporting live players.
- Hardened the TDM team-fire smoke setup so mode `34` supplies smoke-only friendly-line proof to the existing friendly-fire policy instead of teleporting live actors into the firing lane.

## Scenario Coverage

The promoted `ctf_objective_route_precedence` scenario verifies:

- CTF readiness and the reserved mode-41 begin marker.
- Both `ctf_role_route` and `ctf_objective_route` are enabled for the same smoke run.
- CTF role-route requests and policy selections are observed.
- CTF role-route activations and timed route requests remain at zero while `ctf_role_route_objective_deferrals` increases.
- CTF objective-route requests, assignments, route requests, route commands, latest selected-objective metadata, and invalid-skip safety still pass.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario ctf_objective_route_precedence --timeout 120 --base-port 28910 --format text --json-out .tmp\bot_scenarios\ctf_objective_route_precedence.json`
- `ctf_carrier_support_route` focused stress loop, 5 consecutive runs after no-teleport CTF smoke hardening.
- `ctf_base_return_route` focused stress loop, 5 consecutive runs after no-teleport CTF smoke hardening.
- `team_fire_avoidance` focused stress loop, 5 consecutive runs after no-teleport TDM smoke hardening.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 29780 --format text --json-out .tmp\bot_scenarios\latest_report.json`

The focused precedence scenario reported 1 passed, 0 failed, 0 timed out, 0 errored, and 0 pending. The carrier-support, base-return, and team-fire stress loops each passed five consecutive focused runs after the no-teleport smoke hardening. The full implemented scenario suite reported 33 passed, 0 failed, 0 timed out, 0 errored, and 0 pending from `.tmp\bot_scenarios\20260621T091013Z`, with the summary copied to `.tmp\bot_scenarios\latest_report.json`.

## Provenance

No new upstream files were imported in this round. The implementation is WORR-native and uses the existing CTF objective and role-policy helper APIs.
