# Q3A BotLib Scenario Smoke Modes

Task IDs: DV-03-T05, FR-04-T14, FR-04-T16

## Summary

This implementation round reserves dedicated `sv_bot_frame_command_smoke`
modes for the remaining bot scenario promotions:

- `20`: engage enemy
- `21`: switch weapons
- `22`: health/armor pickup
- `23`: team objective

The server side now owns deterministic setup for these modes while the sgame
bot modules continue to own the actual perception, combat, item, and objective
facts. The dedicated server does not synthesize pass metrics; it only enables
mode-specific cvars and prints a begin marker so the scenario harness can
diagnose which contract was active.

## Server-To-Sgame Contract

At smoke start, `src/server/main.c` sets the following cvars:

- `sg_bot_frame_command_smoke_combat`: `engage_enemy`, `switch_weapons`, or `0`
- `sg_bot_frame_command_smoke_weapon_switch`: `1` for mode 21, otherwise `0`
- `sg_bot_frame_command_smoke_item_focus`: `health_armor` for mode 22, otherwise
  `0`
- `sg_bot_frame_command_smoke_team_objective`: `1` for mode 23, otherwise `0`

These cvars are reset at smoke cleanup alongside the existing frame-command
smoke cvars. The server target counts are:

- combat modes: two bots
- health/armor pickup mode: one bot
- team-objective mode: four bots

## Validation State

The new orchestration compiles in `worr_ded_engine_x86_64`. Scenario promotion
still depends on landed sgame status metrics for enemy acquisition, weapon
switch completion, health/armor pickup completion, and team objective progress.
The pending scenario definitions should remain pending until those metrics are
produced by real bot module code.
