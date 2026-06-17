# Q3A BotLib Print Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

This round replaces the quiet-only Q3A `botimport.Print` bridge with a
WORR-owned print callback path. Imported Q3A BotLib/AAS code still formats
messages inside the C boundary, but the final dispatch now crosses into
`botlib_adapter.*` and `bot_runtime.cpp`.

Warnings, errors, and fatals are always forwarded to `gi.Com_PrintFmt`.
Message-level Q3A chatter is forwarded only when `sg_bot_debug_aas >= 3` so
the normal `sg_bot_debug_aas 2` AAS status smoke remains readable.

## Implementation Notes

- Added `Q3ABotLibImportPrintCallback` and `Q3A_BotLibImport_SetPrintCallback`
  to the Q3A import boundary.
- Added adapter-level print callback registration and import-status counters:
  callback presence, message count, warning count, error count, fatal count,
  and last Q3A print type.
- Registered `BotRuntimeQ3APrint` during bot runtime cvar setup before adapter
  initialization.
- Extended verbose `sg_bot_debug_aas 2` adapter status with `q3a_print_*`
  counters so the callback is visible in smoke logs.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated smoke with `sg_bot_debug_aas 3` verifies Q3A message-level print
  forwarding and loaded-AAS behavior:
  - `Q3A BotLib message: trying to load maps/mm-rage.aas`
  - `q3a_print_callback=yes`
  - `q3a_print_messages=2`
  - `q3a_print_warnings=0`
  - `q3a_print_errors=0`
  - `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`
  - `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`
