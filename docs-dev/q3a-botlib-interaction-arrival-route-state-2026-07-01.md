# Q3A BotLib Interaction Arrival Route State - 2026-07-01

Tasks: FR-04-T04, FR-04-T05, FR-04-T15, DV-03-T05, DV-07-T06

## Summary

The `coop_campaign_key_carry_train` bridge-arrival proof now uses reusable
`bot_nav` interaction-arrival route state instead of keeping all route/reach
bookkeeping in the key-carry smoke path. A route request can identify its
position goal as the arrival side of a specific interaction entity, and the nav
layer records assignment, cache reuse, reached, and last-route metadata for
that arrival leg.

This keeps the focused train proof warp-free while making the mechanism
available to later button, platform, train, teleporter, and mover transition
work.

## Implementation Notes

- Extended `BotNavRouteRequest` with interaction-arrival metadata:
  source entity, interaction kind, and interaction action.
- Added reusable helpers:
  `BotNav_SetInteractionArrivalRouteRequest()`,
  `BotNav_BuildInteractionArrivalRouteRequest()`, and
  `BotNav_InteractionArrivalRouteReached()`.
- Added route-status counters:
  `interaction_arrival_route_requests`,
  `interaction_arrival_route_assignments`,
  `interaction_arrival_route_cache_reuses`,
  `interaction_arrival_route_reached`,
  `interaction_arrival_route_invalid_skips`, and the
  `last_interaction_arrival_route_*` fields.
- Made interaction-arrival route assignment independent from exact persistent
  position-goal assignment. This matters on `train`, where the AAS route can
  return a successful fallback end area while still delivering a one-point route
  target close enough to prove arrival.
- Updated `coop_campaign_key_carry_train` to save the full
  `BotNavInteractionGoal`, populate its route request through `bot_nav`, and
  advance to the lock leg only after the generic nav reached check fires.
- Updated the scenario gate to require generic interaction-arrival route
  request, assignment, reached, source entity, and AAS area telemetry in
  addition to the existing key-carry-specific counters.

## Validation

Focused scenario:

```text
python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --game basew --scenario coop_campaign_key_carry_train --timeout 120 --artifact-dir .tmp\bot_scenarios\20260701T162117Z --format both --json-out .tmp\bot_scenarios\20260701T162117Z\coop_campaign_key_carry_train.json
```

Artifact: `.tmp\bot_scenarios\20260701T162117Z`

Key result:

```text
pass=1
frames=121
commands=121
route_commands=61
route_failures=0
interaction_arrival_goal_requests=1
interaction_arrival_goal_resolved=1
last_interaction_arrival_goal_area=1058
interaction_arrival_route_requests=2
interaction_arrival_route_assignments=2
interaction_arrival_route_reached=1
last_interaction_arrival_route_entity=60
last_interaction_arrival_route_area=1058
key_carry_bridge_arrival_route_requests=2
key_carry_bridge_arrival_reached=1
key_carry_bridge_arrival_warps=0
key_carry_lock_route_requests=42
key_carry_lock_warps=0
last_key_carry_bridge_arrival_distance_sq=119
last_key_carry_lock_required_item=70
```

Additional checks run before the focused scenario:

```text
python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
meson compile -C builddir-win sgame_x86_64
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

Broader validation:

```text
python tools/bot_release/run_bot_acceptance.py --install-dir .install --base-game basew --format text --output .tmp\bot_scenarios\bot_acceptance_after_interaction_arrival_route_state.txt
```

Release acceptance passed `15/15` with `0` warnings. The `scenario_evidence`
gate still points at the previously green
`.tmp\bot_scenarios\implemented_after_bridge_arrival_route.json` report.

A fresh full implemented scenario rerun was attempted at
`.tmp\bot_scenarios\implemented_after_interaction_arrival_route_state_rerun.json`
and reached `119/123`. The four failures were confined to existing chat-policy
count/outcome rows (`bot_chat_team_policy`, `bot_chat_reply_policy`,
`bot_chat_event_policy`, and `bot_chat_live_match_result`) and did not involve
the interaction-arrival route or key-carry train proof.
