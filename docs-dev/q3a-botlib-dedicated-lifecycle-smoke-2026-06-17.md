# Q3A BotLib Dedicated Lifecycle Smoke

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

## Summary

This round adds a deterministic dedicated-server lifecycle smoke for the imported Q3A BotLib runtime. The smoke starts a real dedicated server on `mm-rage`, loads packaged `maps/mm-rage.aas`, reloads the map once, prints shutdown-time lifecycle counters before each game-DLL unload, and exits without relying on command-line `+wait` / `+sv` timing.

The dedicated `map` path reloads `sgame_x86_64.dll`, so map-reload lifecycle proof must be captured before the old game module is destroyed. The smoke therefore prints status after `Bot_RuntimeEndLevel()` inside `ShutdownGame()` when `sg_bot_lifecycle_smoke` is active.

## Implementation Notes

- Moved Bot runtime cvar registration into `PreInitGame()` so command-line `+set sg_bot_*` values are available before first-map startup.
- Added `sv botlib_lifecycle_status` as a server command for explicit lifecycle status probes.
- Added `sg_bot_lifecycle_smoke`:
  - `1`: print lifecycle status after active-map BotLib begin.
  - `2`: print, reload the same map once, then print again.
  - `3`: print, reload once, print again, and quit.
  - `4`: internal post-reload print-and-quit mode used by mode `3` after the dedicated `map` reload.
- Active smoke mode raises the game logger to `Info` so status lines are emitted even when the default logger threshold is `Warn`.
- `ShutdownGame()` now prints lifecycle status after `Bot_RuntimeEndLevel()` while the smoke cvar is active, capturing clean unload counters before the game DLL is released.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated self-smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_lifecycle_self_smoke_shutdown +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_lifecycle_smoke 3 +map mm-rage`

Key log evidence:

- First load:
  `q3a_lifecycle=Q3A BotLib lifecycle load passed, q3a_lifecycle_load_attempts=1, q3a_lifecycle_load_successes=1`
- First shutdown before map reload:
  `q3a_lifecycle=Q3A BotLib lifecycle unload clean, q3a_lifecycle_active_unloads=1, q3a_lifecycle_clean_unloads=1, q3a_lifecycle_unload_failures=0, q3a_lifecycle_last_unload_zone_active=0, q3a_lifecycle_last_unload_hunk_active=0, q3a_lifecycle_last_unload_open_files=0, q3a_lifecycle_persistent_zone=5876`
- Second shutdown before quit:
  `q3a_lifecycle=Q3A BotLib lifecycle unload clean, q3a_lifecycle_active_unloads=1, q3a_lifecycle_clean_unloads=1, q3a_lifecycle_unload_failures=0, q3a_lifecycle_last_unload_zone_active=0, q3a_lifecycle_last_unload_hunk_active=0, q3a_lifecycle_last_unload_open_files=0, q3a_lifecycle_persistent_zone=5876`

## Outstanding Work

- Add real bot creation on top of this initialized BotLib path.
- Add long-running dedicated bot-count smokes once fake-client commands and bot movement exist.
