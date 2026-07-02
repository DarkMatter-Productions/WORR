# Q3A BotLib Interaction-Arrival Mover Endpoint Round - 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round extends the train campaign key-carry proof from reusable
interaction-arrival route state into endpoint-aware mover arrival selection.
`bot_nav` now inspects candidate endpoint positions from mover metadata and
target links before falling back to the local destination-side sweep.

The live `train` case is intentionally mixed: the map exposes mover endpoint
candidates, and the scorer selects endpoint candidates while evaluating the
goal, but the final bridge-arrival goal remains a destination-offset source
because that point is much closer to the lock-side route target. The scenario
therefore requires both endpoint discovery telemetry and an accepted final
arrival source of either destination offset or mover endpoint.

## Implementation

- Added `BotNavInteractionArrivalSource` tracking for none, destination
  offset, direct destination, and mover endpoint arrival goals.
- Added endpoint collection for mover-like interaction kinds, drawing from
  `moveInfo.endOrigin`, `moveInfo.dest`, `pos2`, `pos1`, the target entity
  origin, and entities reachable through `target` / `targetname` links.
- Added endpoint candidate scoring before the destination-offset sweep in
  `BotNavFindInteractionArrivalGoal`. Endpoint points are recorded and can win,
  while destination-side points still win when they are materially closer to the
  requested post-mover route destination.
- Expanded `BotNavRouteStatus` and `q3a_bot_frame_command_status` with
  endpoint check/candidate/selection counters plus the last endpoint entity,
  kind, action, area, position, direct distance, and destination distance.
- Hardened `coop_campaign_key_carry_train` so the train key-carry proof now
  requires endpoint checks, endpoint candidates, endpoint selections, endpoint
  entity/area telemetry, interaction-arrival route reach evidence, and no
  bridge-arrival or lock warps.

## Validation

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win sgame_x86_64`
- Focused `coop_campaign_key_carry_train` validation:
  `.tmp\bot_scenarios\interaction_arrival_mover_endpoint.json`
- Full implemented suite:
  `.tmp\bot_scenarios\implemented_after_interaction_arrival_mover_endpoint.json`
- Release acceptance:
  `tools/bot_release/run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_after_interaction_arrival_mover_endpoint.json`
- Staging refresh:
  `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`

The focused run passed with endpoint telemetry active:
`interaction_arrival_mover_endpoint_checks=3`,
`interaction_arrival_mover_endpoint_candidates=3`,
`interaction_arrival_mover_endpoint_selections=2`,
`last_interaction_arrival_goal_source=1`,
`last_interaction_arrival_mover_endpoint_entity=60`,
`last_interaction_arrival_mover_endpoint_kind=4`,
`last_interaction_arrival_mover_endpoint_area=3131`, and
`last_interaction_arrival_mover_endpoint_destination_distance_sq=2567345`.

The full implemented suite passed `123/123` automated rows from
`.tmp\bot_scenarios\implemented_after_interaction_arrival_mover_endpoint.json`.
Release acceptance passed `15/15` checks and consumed that scenario report as
the release gate evidence.

## Follow-Up

The next M4/M6 movement slice should use this endpoint-aware arrival data as
the base for broader off-mesh mover ride-state support: explicitly tracking
when a bot is waiting for, boarding, riding, and leaving moving geometry across
additional maps instead of only proving the train key-carry bridge case.
