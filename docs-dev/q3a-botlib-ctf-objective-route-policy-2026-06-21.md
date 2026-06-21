# Q3A BotLib CTF Objective Route Policy

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off CTF objective route policy above the focused CTF route owners. The new `sg_bot_ctf_objective_route` bridge evaluates base-return, carrier-support, and enemy-flag fallback candidates in one command-frame owner, then assigns a position route goal from the highest-priority valid candidate before the focused `sg_bot_ctf_base_return_route`, `sg_bot_ctf_carrier_support_route`, or `sg_bot_ctf_dropped_flag_route` bridges can claim the same route request.

The policy is intentionally still a proof bridge, not the final autonomous CTF brain. It demonstrates that the existing CTF role/lane helpers can be composed into a single route owner while preserving priority order and route-command ownership.

## Runtime Changes

- Added `sg_bot_ctf_objective_route`, with server smoke mode `40` reserved for the combined CTF objective route policy.
- Added `Bot_CommandBuildCtfObjectiveRoute()` to evaluate the focused CTF objective assignments in priority order:
  1. Own-flag/base return.
  2. Same-team enemy flag-carrier support.
  3. Enemy flag fallback, including dropped-flag response surfaces.
- Added `ctf_objective_route_*` and `last_ctf_objective_route_*` frame-command status fields for requests, assignments, candidate availability, selections, deferrals, route requests, route commands, invalid skips, latest selection, role/lane/type/source, carrier client, item, priority, and goal distance.
- Kept lower focused CTF route bridges from double-claiming a route request once the combined policy has already assigned one.
- Let mode `40` retain the synthetic dropped-flag smoke targets that mode `37` already uses, so the combined policy can see a lower-priority dropped objective candidate.
- Stabilized synthetic CTF route carriers during smoke setup by making seeded carriers non-damageable with high smoke-only health. This prevents default bot combat from killing the route target before status capture while leaving the route objective assignment unchanged.

## Scenario Coverage

The promoted `ctf_objective_route` scenario runs four bots in CTF with `sg_bot_ctf_objective_route 1`. It verifies:

- CTF readiness and the reserved mode-40 begin marker.
- Policy requests and assignments.
- Base-return, carrier-support, and dropped-flag candidate visibility.
- Base-return selections.
- Carrier-support fallback selections when no base-return route is available for that bot.
- Dropped-flag deferrals when a higher-priority route exists.
- Route requests, route commands, invalid-skip safety, and latest selected-objective metadata.

The focused `ctf_dropped_flag_route`, `ctf_carrier_support_route`, and `ctf_base_return_route` scenarios remain the narrower ownership proofs for the individual route surfaces.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario ctf_base_return_route --timeout 120 --base-port 28720 --format text --json-out .tmp\bot_scenarios\ctf_base_return_after_stabilize.json`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario ctf_objective_route --timeout 120 --base-port 28730 --format text --json-out .tmp\bot_scenarios\ctf_objective_after_stabilize.json`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28800 --format text --json-out .tmp\bot_scenarios\latest_report.json`

The final implemented scenario run reported 32 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.

## Provenance

No new upstream files were imported in this round. The implementation is WORR-native and composes the existing objective helper APIs documented in the Q3A BotLib AAS credit ledger.
