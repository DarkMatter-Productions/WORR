# Q3A BotLib Interaction Mover Ride Observation

Date: 2026-07-01

Task IDs: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round extends the train key-carry bridge proof with a bounded
post-activation observation window for the key-side `func_train` bridge. The
previous wait/board/ride/leave telemetry proved the lifecycle calls, but the
scenario could still advance immediately from bridge activation into the
arrival projection without sampling the mover after activation. The new window
keeps the proof bot on the bridge phase for 200 ms, records ride-state samples,
then releases the bot back to the normal post-mover arrival and `trigger_key`
lock route.

The train bridge selected by the current proof is parked at `MoveState::Top`
for this short scenario. Because of that, moving and grounded-on-mover samples
remain reported as diagnostics rather than hard gates for this map. The
scenario now gates the observation request, sample frame count, completion, and
elapsed time, while still requiring the regular post-arrival route and final
key-lock progression evidence.

## Implementation

- `bot_brain` now tracks key-carry bridge ride observation requests, sample
  frames, moving/grounded one-shot observations, completion, timeout, last move
  state, last ground entity, and elapsed milliseconds.
- `Bot_CommandObserveKeyCarrySmokeBridgeRide` samples the recorded bridge
  entity through `BotNav_RecordMoverRideState(..., Ride)` after activation,
  closes after the first moving sample beyond the minimum window or after the
  200 ms parked-mover timeout, and resets only the proof bot route state before
  the arrival route is resolved.
- `bot_nav` now preserves a completed leave phase for the same mover/client so
  later generic recovery samples cannot overwrite final lifecycle evidence back
  to board/wait.
- The generic recovery sampler continues to report mover endpoint wait/board
  samples for other mover-like interactions, which keeps broader diagnostics
  available while the campaign row gates the deterministic train bridge window.
- `coop_campaign_key_carry_train` now requires
  `key_carry_bridge_ride_observation_requests`,
  `key_carry_bridge_ride_observation_frames`,
  `key_carry_bridge_ride_observation_completed`, and
  `last_key_carry_bridge_ride_observation_elapsed_ms >= 200`.

## Validation

Focused validation passed from
`.tmp\bot_scenarios\mover_ride_observation_final.json`:

- `interaction_mover_ride_checks=20`
- `interaction_mover_ride_wait_states=1`
- `interaction_mover_ride_board_states=7`
- `interaction_mover_ride_ride_states=11`
- `interaction_mover_ride_leave_states=1`
- `last_interaction_mover_ride_phase=4`
- `last_interaction_mover_ride_entity=60`
- `key_carry_bridge_ride_observation_requests=1`
- `key_carry_bridge_ride_observation_frames=9`
- `key_carry_bridge_ride_observation_completed=1`
- `key_carry_bridge_ride_observation_timeouts=1`
- `last_key_carry_bridge_ride_observation_move_state=0`
- `last_key_carry_bridge_ride_observation_elapsed_ms=200`
- `nav_interaction_progression_key_path_candidates=1`
- `nav_interaction_progression_key_path_selections=1`
- `last_nav_interaction_progression_key_path_key_lock=1`
- `last_nav_interaction_progression_key_path_required_item=70`
- `key_carry_bridge_arrival_route_requests=2`
- `key_carry_bridge_arrival_reached=1`
- `key_carry_lock_route_requests=34`
- `key_carry_lock_warps=0`
- `route_failures=0`

Targeted `sgame_x86_64` builds passed before the focused run, with `.install/`
refreshed through `tools/refresh_install.py`.

## Follow-Up

The next mover-oriented work should promote moving and grounded-on-mover gates
on a map/interaction where the selected mover actually travels while the proof
bot is aboard. The current `train` bridge remains valuable as a parked-mover
regression row because it proves the bot can observe, time out cleanly, preserve
the leave lifecycle, and continue to the key lock without proof warps.
