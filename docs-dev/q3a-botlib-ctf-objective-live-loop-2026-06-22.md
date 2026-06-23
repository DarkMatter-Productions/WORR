# Q3A BotLib CTF Objective Live Loop

Date: 2026-06-22

Task IDs: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`

## Purpose

Promote the combined CTF objective-route proof from "the policy can see
multiple flag-state candidates" to "the policy selects multiple live objective
states in one CTF run." This closes the next roadmap slice for CTF objective
loop promotion without adding a new smoke-only behavior path.

## Implementation

- Hardened `ctf_objective_route` in
  `tools/bot_scenarios/run_bot_scenarios.py`.
  - The mode `40` row still runs four CTF bots with
    `sg_bot_ctf_objective_route 1`.
  - It now requires base-return, carrier-support, and dropped-flag selections
    in the same run.
  - It also requires behavior arbitration objective candidates and objective
    ownership, proving the route is owned by the live owner model rather than
    only by a raw status counter.
- Updated `tools/bot_scenarios/test_run_bot_scenarios.py` so catalog marker
  coverage and synthetic marker evaluation fail unless the stricter CTF
  objective-loop evidence is present.
- Updated `tools/bot_scenarios/README.md` so the scenario catalog describes the
  row as a live objective-loop gate.

No C/C++ gameplay, BotLib, renderer, `q2proto/`, or upstream Q3A/BSPC source
files were modified in this round.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  - Passed 47 tests.
- Focused `ctf_objective_route` validation passed from
  `.tmp\bot_scenarios\20260622T210329Z`.
  - Key metrics: `frames=246`, `commands=246`, `route_commands=246`,
    `route_failures=0`, `ctf_objective_route_base_return_selections=106`,
    `ctf_objective_route_carrier_support_selections=53`,
    `ctf_objective_route_dropped_flag_selections=53`,
    `ctf_objective_route_route_commands=212`,
    `behavior_arbitration_objective_owners=192`.
- Full implemented-suite validation passed from
  `.tmp\bot_scenarios\20260622T210348Z`.
  - Summary: 80 passed, 0 failed, 0 timeouts, 0 errors, 0 pending.

## Follow-Up

- Move the next implementation slice to TDM role/lane stability over spawn
  changes, because the CTF objective route now has a stricter live-loop gate.
- Keep future CTF work focused on less-staged flag transitions, combat/objective
  handoffs around actual pickups/drops/returns, and objective behavior on
  reference CTF maps.
