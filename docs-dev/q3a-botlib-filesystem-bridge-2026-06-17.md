# Q3A BotLib Filesystem Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

This round replaces the Q3A BotLib import table's single active-memory file shim with a read-only WORR filesystem callback bridge.

Q3A still sees its native `botimport.FS_FOpenFile`, `FS_Read`, `FS_Seek`, and `FS_FCloseFile` callbacks. Behind that boundary, WORR now loads files through the existing server-game filesystem extension, tracks Q3A file handles in the import layer, and releases callback-owned buffers through the runtime free callback when Q3A closes the file.

The old active in-memory AAS buffer remains as a fallback path, but the normal dedicated-server load now exercises WORR FS directly for `maps/<map>.aas`.

## Implementation Notes

- `q3a_botlib_import.*`
  - Adds C-compatible filesystem load/free callbacks.
  - Replaces the singleton memory-file handle with a small tracked file-handle table.
  - Counts open attempts, WORR-FS opens, fallback memory opens, read bytes, seeks, closes, write rejections, and open misses.
  - Keeps `FS_WRITE` rejected because the runtime bridge is read-only for now.
- `botlib_adapter.*`
  - Adds filesystem callback wiring and adapter status fields.
- `bot_runtime.cpp`
  - Registers a callback backed by `FILESYSTEM_API_V1::LoadFile`.
  - Frees Q3A-owned buffers through `gi.TagFree`.
  - Prints a compact `BotLib adapter filesystem` status line under `sg_bot_debug_aas 2`.

The single open miss observed in the smoke is expected: imported Q3A route-cache initialization probes `maps/<map>.rcd` if present. WORR does not currently package route-cache dumps, so the bridge reports the miss while still passing the AAS load.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated server smoke:
  `.\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_bot_fs_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 3 +map mm-rage +wait 100 +quit`
- Smoke log evidence:
  `BotLib adapter filesystem: q3a_fs=Q3A BotLib filesystem bridge passed, q3a_fs_callback=yes, q3a_fs_attempted=yes, q3a_fs_passed=yes, q3a_fs_open_attempts=2, q3a_fs_files=1, q3a_fs_memory_files=0, q3a_fs_open_failures=1, q3a_fs_read_bytes=277484, q3a_fs_seeks=0, q3a_fs_closes=1, q3a_fs_writes_rejected=0`
- The same smoke still reported `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`, `q3a_memory_failures=0`, and `q3a_bot_client_command=Q3A BotClientCommand bridge passed: callback=yes client=0 accepted=0 rejected=1 failures=0`.

## Outstanding Work

- Add a deliberate write policy before enabling Q3A route-cache dump writes.
- Decide whether future BotLib profile/config reads should stream through engine file handles or continue to use load-whole-file buffers at this boundary.
