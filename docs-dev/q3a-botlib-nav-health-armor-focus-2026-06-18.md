# Q3A BotLib Nav Health/Armor Focus - 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Summary

This slice wires the reserved `sg_bot_frame_command_smoke_item_focus=health_armor`
contract into `bot_nav.cpp` without taking over the server smoke setup, action
dispatcher, or scenario harness ownership.

The pickup-goal scanner now consumes the existing `bot_items` utility surface:

- `BotItems_BuildContextForEntity()` builds candidate facts from active pickup
  entities.
- `BotItems_Evaluate()` scores candidates and records item classifier/scoring
  counters.
- `BotItems_RecordGoalAssignment()` records health/armor goal assignment
  telemetry when nav creates a new item reservation.
- `BotItems_RecordPickup()` records health/armor pickup deltas when a reserved
  item goal is consumed near the bot's route goal and the matching resource
  value increased.

Normal item routing is preserved when no smoke focus is active: weapons, ammo,
powerups, and other useful pickups continue to compete through the shared item
utility score. When `health_armor` is active, nav filters the candidate pool to
health and armor utility kinds and applies the candidate-specific focus boost,
so the smoke mode materially drives bots toward health/armor goals instead of
high-value weapon/ammo goals.

## Implementation Notes

- Added a local nav focus parser for `health`, `armor`, and `health_armor`.
- Kept existing route-area lookup, reservation skipping, blacklist cooldowns,
  distance penalty, and deterministic entity-order tie behavior.
- Stored bot health and armor snapshots on item-goal assignment.
- Recorded a pickup only when the bot is still near the recorded route goal and
  the relevant health or armor value increased, avoiding false positives from
  unrelated item churn elsewhere on the map.

No `bot_nav.hpp` status ABI changes were needed for this lane.

## Validation

No local unit tests exist for this nav/item slice; the only nearby tests are the
scenario harness/tool parser tests, so validation used compile plus the reserved
dedicated smoke mode.

Commands run:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_nav.cpp.obj
meson compile -C builddir-win sgame_x86_64
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27978 +set logfile 1 +set logfile_name q3a_bot_nav_health_armor_focus_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_frame_command_smoke 22 +map mm-rage
```

Results:

- Targeted `bot_nav.cpp` object compile passed.
- Full `sgame_x86_64` compile/link passed.
- `.install` refresh passed; `pak0.pkz` was rebuilt from `87` asset files,
  `botfiles` were mirrored loose, `maps/mm-rage.aas` was injected, the q2aas
  archive audit passed, and staged install validation passed for
  `windows-x86_64`.
- Reserved smoke mode `22` completed with `pass=1`, `route_failures=0`,
  `item_focus=health_armor`, `item_goal_scans=15`,
  `item_goal_candidates=329`, `item_goal_assignments=15`, and final
  `last_item_goal_item=4` (`IT_ARMOR_SHARD`). The same run blacklisted another
  armor goal with `last_failed_goal_item=2` (`IT_ARMOR_COMBAT`), confirming the
  focus lane stayed on armor-class goals.

The smoke did not change server-side low-health/low-armor seeding in this lane;
the pickup-delta hooks are ready for that deterministic setup to drive both
health and armor pickup completion counters.
