# Q3A BotLib Combat Damage Event Hook

Date: 2026-06-18

Project tasks: `FR-04-T15`, `DV-03-T05`

## Summary

This slice wires the existing `BotCombatStatus` damage fields to the real
server-game damage path. The hook is intentionally narrow: it runs from
`Damage()` after friendly-fire, protection, armor, power armor, spawn
protection, battle suit, freeze-tag, and other damage adjustments have resolved.

`BotCombat_RecordDamageEvent()` records only attributed bot damage against a
client enemy. It rejects non-bot attackers, self-damage, non-client targets, bad
client indexes, teammates, and non-positive post-protection damage. This keeps
`combat_damage_events` honest for scenario promotion and avoids synthesizing
combat success from action telemetry alone.

## Runtime Contract

The status fields now update as follows:

- `combat_damage_events`: increments once for each qualifying bot-inflicted
  damage event observed by `Damage()`.
- `last_combat_damage`: receives the most recent real hit amount, counting
  health damage plus real armor and power-armor absorption.
- `last_combat_enemy_client`: receives the damaged enemy client index.
- `combat_last_enemy_distance_sq`: receives the current attacker-to-target
  distance squared when the damage event is recorded.

Protection-only saves such as god mode, combat-disabled windows, and other
damage-prevention paths do not count as real combat damage for this hook.

## Validation Notes

Commands run:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_combat.cpp.obj
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_gameplay_g_combat.cpp.obj
meson compile -C builddir-win sgame_x86_64
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp/q2aas/stage-report.json --q2aas-package-report .tmp/q2aas/refresh-package-archive-report.json --q2aas-package-audit-report .tmp/q2aas/refresh-package-archive-audit-report.json
& .\.install\worr_ded_x86_64.exe +set basedir E:\Repositories\WORR\.install +set game basew +set net_port 27971 +set logfile 1 +set logfile_name q3a_bot_combat_damage_hook_mode20 +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_frame_command_smoke 20 +map mm-rage
```

Results:

- Both touched objects compiled.
- `meson compile -C builddir-win sgame_x86_64` linked `sgame_x86_64.dll`.
- The refresh workflow completed, packaged 87 asset files, injected
  `maps/mm-rage.aas` into `.install/basew/pak0.pkz`, audited the archive member,
  and validated the staged Windows payload.
- The optional mode `20` raw smoke emitted
  `q3a_bot_frame_command_smoke_scenario=begin mode=20 combat=engage_enemy` and a
  frame-command status with `frames=121`, `commands=121`, `route_failures=0`,
  and `pass=1`.
- Mode `20` did not prove the damage hook in this shared setup: the frame status
  reported `blackboard_current_enemies=0`, `combat_enemy_acquisitions=0`, and
  `last_combat_enemy_client=-1`, so no real enemy damage was expected.
