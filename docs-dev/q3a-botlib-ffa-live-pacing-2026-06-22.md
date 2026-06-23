# Q3A BotLib FFA Live Pacing Round

Date: 2026-06-22

Task IDs: FR-04-T04, FR-04-T06, FR-04-T15, DV-03-T05, DV-07-T06

## Summary

This round promotes a combined FFA live-pacing proof as frame-command smoke
mode `74` through the `ffa_live_pacing` scenario. The run stages a four-bot FFA
match with `deathmatch 1`, `g_gametype 1`, `sg_bot_ffa_roam_route 1`,
`sg_bot_ffa_spawn_camp_avoidance 1`, `sg_bot_ffa_item_roles 1`,
`sg_bot_ffa_role_combat 1`, and
`sg_bot_ffa_spawn_camp_combat_avoidance 1`.

The server begin marker now publishes `ffa_live_pacing=1` when this combined
proof is active. The mode participates in the existing FFA helper predicates so
one scenario can verify route ownership, spawn-camp route-source pressure,
item-role scoring, role-combat decisions, and spawn-camp combat veto behavior.

The game-side smoke detector now recognizes the exact live-pacing cvar
combination through `Bot_CommandFfaLivePacingProofEnabled()`. Without that
dedicated detector, the same cvar set presented as the narrower mode `49`
spawn-camp combat proof and skipped the additional item-role telemetry path.

`BotNav_ProbePickupGoal()` was added as a validation-only item scoring probe.
The probe reuses the normal pickup-goal scoring and updates nav policy telemetry,
but it does not assign route ownership. That keeps the FFA timed route owner in
control while still proving that `sg_bot_ffa_item_roles` is shaping pickup
candidate scores during the same live pacing run.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed with
  49 tests.
- `meson compile -C builddir-win sgame_x86_64 copy_sgame_dll
  worr_ded_engine_x86_64 worr_ded_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64` refreshed the staged
  install successfully.
- Focused `ffa_live_pacing` validation passed from
  `.tmp\bot_scenarios\20260622T214927Z`.
- The full `implemented` scenario suite passed from
  `.tmp\bot_scenarios\20260622T215343Z` with 82 passed rows, 0 failed rows, 0
  timeouts, 0 errors, and 0 pending rows.

## Source Boundaries

No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source was imported or
modified in this round. The changes are WORR-native scenario, server marker,
bot-brain detector, nav telemetry, and documentation work.

## Follow-Up

Duel-specific pacing remains the next multiplayer gap because this proof is
explicitly FFA-only through `g_gametype 1`; Duel needs separate `g_gametype 2`
coverage for duel item denial, spawn pressure, queue flow, and pacing behavior.
