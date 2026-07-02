# Q3A BotLib Campaign Key Carry Train Proof

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round extends the `train` campaign keyed-path proof into a concrete
pickup-to-lock carry proof. The new `coop_campaign_key_carry_train` scenario
runs mode `91` on `train`, enables the `bot_campaign_key_carry_smoke` overlay,
routes the proof bot to the real red-key item, records a normal `Touch_Item`
pickup, sends the bot through the key-side `func_train` bridge proof, then
carries that inventory into the existing `trigger_key` lock-side interaction
proof.

The bridge-start and lock-side warps remain explicit. Current generated AAS
metadata records the red key as reachable from the spawn side, while the bridge
mover start and `trigger_key` lock island are not connected cleanly enough for a
true end-to-end route. This slice proves the inventory carry, real train bridge
activation, and keyed lock behavior without pretending the map graph is already
solved end-to-end.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` adds the smoke-only
  `bot_campaign_key_carry_smoke` cvar. The public name uses the `bot_` prefix
  and is intentionally not an `sv_` cvar.
- The mode `91` train keyed-path overlay now opts out while key-carry is
  active, so `coop_campaign_keyed_path_train` continues to validate the
  lock-side keyed-path regression independently.
- The key-carry overlay stages client 0 at the red-key leg, clears stale
  red-key inventory, builds a position route to the red key, invokes the normal
  item touch path, and records positive red-key inventory.
- After pickup, the overlay resets nav state, records an explicit bridge-start
  warp, activates the nearby `func_train` through the normal nav interaction
  retry path, and only then builds the lock-side route request. The existing
  nav interaction chooser then selects the `trigger_key` lock and reports the
  required key item.
- `q3a_bot_nav_policy_status` now prints `key_carry_*` counters so scenario
  evidence can distinguish key route, real pickup, bridge route/interaction,
  lock route, warp bridge, and required-item telemetry.
- `tools/bot_scenarios/run_bot_scenarios.py` adds
  `coop_campaign_key_carry_train` and marker gates for both the new key-carry
  counters and the existing keyed-path lock selection.
- `tools/bot_release/run_bot_acceptance.py` now requires
  `coop_campaign_key_carry_train` as release scenario evidence.

## Validation

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py tools/bot_release/run_bot_acceptance.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_coop_campaign_key_carry_train_catalog_and_marker_checks tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_policy_trace_and_readiness_promotions_use_expected_smoke_rows tools.bot_release.test_run_bot_acceptance`
- `meson compile -C builddir-win sgame_x86_64`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_campaign_key_carry_train --timeout 90 --artifact-dir .tmp/bot_scenarios --json-out .tmp/bot_scenarios/coop_campaign_key_carry_train.json`
- `python tools/bot_release/run_bot_acceptance.py --scenario-report .tmp/bot_scenarios/implemented_hazard_context.json --allow-missing-scenario-report`

The focused scenario passed with:

- `pass=1`
- `route_commands=61`
- `route_failures=0`
- `key_carry_key_route_requests=1`
- `key_carry_key_touch_attempts=1`
- `key_carry_key_pickups=1`
- `key_carry_bridge_route_requests=1`
- `key_carry_bridge_warps=1`
- `key_carry_bridge_interactions=1`
- `key_carry_bridge_interaction_commands=4`
- `last_key_carry_bridge_kind=4`
- `last_key_carry_bridge_travel_type=11`
- `last_nav_interaction_client=0`
- `key_carry_lock_route_requests=59`
- `key_carry_lock_warps=1`
- `last_key_carry_pickup_inventory=1`
- `last_key_carry_lock_required_item=70`
- `last_nav_interaction_progression_key_path_key_lock=1`
- `last_nav_interaction_progression_key_path_required_item=70`

The catalog now reports `124` implemented rows and `0` pending rows. Release
acceptance passes `15/15` checks and requires `18` scenario evidence rows.

## Notes

No upstream Q3A, Quake3e, baseq3a, BSPC, Gladiator, or `q2proto/` source files
were imported or modified for this slice.

Next campaign work should remove or reduce the bridge-start and lock-side proof
warps by improving the campaign route graph, train mover ride state, and
arrival-side continuation enough for a bot to select the key, cross the bridge,
travel to the lock side, and complete the transition without smoke-only
staging.
