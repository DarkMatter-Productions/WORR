# Q3A BotLib BotClientCommand Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

This round adds the Q3A `botimport.BotClientCommand` bridge to the WORR BotLib adapter, but deliberately keeps command execution closed until WORR has a validated bot fake-client command dispatcher.

The imported Q3A callback now lands in `q3a_botlib_import.c`, crosses `botlib_adapter.*`, and reaches a WORR runtime callback in `bot_runtime.cpp`. The runtime callback validates the Q3A client index against WORR's client entity range, requires a real bot client (`SVF_BOT` or `sess.is_a_bot`), and currently returns a safe rejection rather than executing the command string. This gives the runtime a smokeable import contract without creating an accidental text-command execution path.

No new upstream Q3A source files were imported in this slice.

## Implementation Notes

- `Q3A_BotLibImport_SetBotClientCommandCallback` registers a WORR-owned callback for the Q3A import table.
- `Q3A_BotLibImport_RunBotClientCommandSmoke` calls the real `botimport.BotClientCommand` slot with a smoke command and expects the bridge to reject it cleanly.
- `BotLibAdapter_SetBotClientCommandCallback` and `BotLibAdapter_RunBotClientCommandSmoke` keep the C import boundary hidden behind the existing adapter.
- `BotRuntimeBotClientCommand` validates client range and bot identity before returning `false` until a future dispatcher owns the command whitelist and execution path.
- `sg_bot_debug_aas >= 3` runs the smoke once per second alongside the existing debug draw/polygon/area smokes.
- Verbose debug output now includes a compact `BotLib adapter BotClientCommand` line with callback, client, accepted, rejected, and failure counters.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated server smoke:
  `.\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_bot_client_command_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 3 +map mm-rage +wait 90 +quit`
- Smoke log evidence:
  `q3a_bot_client_command=Q3A BotClientCommand bridge passed: callback=yes client=0 accepted=0 rejected=1 failures=0`
- The same smoke still reported the existing route and debug-area checks:
  `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`,
  `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`,
  and `q3a_debug_area=Q3A AAS debug area helpers passed: area=3 lines=12 polygon_creates=6 polygon_deletes=6 failures=0`.
