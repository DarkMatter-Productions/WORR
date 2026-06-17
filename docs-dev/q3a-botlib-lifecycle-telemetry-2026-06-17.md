# Q3A BotLib Lifecycle Telemetry

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

This round adds explicit lifecycle telemetry around the Q3A BotLib import bridge. The bridge now counts import initialization, shutdown, AAS load attempts/successes, active AAS unloads, clean unloads, unload failures, transient unload residue, open file handles, and persistent LibVar zone bytes.

The first repeated-load harness exposed `zone_active=5876` after unload. That storage is Q3A LibVar state, which is module-lifetime state rather than active AAS state, so the telemetry now reports it separately as `q3a_lifecycle_persistent_zone`. Clean unload requires transient AAS zone bytes, hunk bytes, and file handles to return to zero.

## Implementation Notes

- `q3a_botlib_import.*`
  - Adds lifecycle fields to `Q3ABotLibImportSmokeStatus`.
  - Counts lifecycle init/shutdown/load/unload events.
  - Verifies active unloads leave no loaded AAS state, hunk allocations, transient zone allocations, active memory-file fallback, or open Q3A file handles.
  - Tracks persistent LibVar zone bytes separately from transient unload residue.
- `botlib_adapter.*`
  - Copies lifecycle counters into adapter status.
- `bot_runtime.cpp`
  - Prints a compact `BotLib adapter lifecycle` line under verbose bot AAS debug output.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Scratch lifecycle harness:
  `clang++ -std=c++17 -fuse-ld=lld .tmp\q3a_lifecycle_harness.cpp builddir-win\libq3a_botlib_utility.a -Isrc\game\sgame\bots -Isrc\game\sgame\bots\q3a -Isrc\game\sgame\bots\q3a\game -Isrc\game\sgame\bots\q3a\botlib -o .tmp\q3a_lifecycle_harness.exe`
- Harness evidence:
  `cycle=1 clean=1 loads=1/1 active_unloads=1 clean_unloads=1 unload_failures=0 zone_active=5876 persistent_zone=5876 hunk_active=0 last_unload_files=0 lifecycle=Q3A BotLib lifecycle unload clean`
- Final harness evidence:
  `shutdown inits=1 shutdowns=1 loads=3/3 active_unloads=3 clean_unloads=3 unload_failures=0 lifecycle=Q3A BotLib lifecycle shut down`

## Follow-Up

- Dedicated map-change coverage is now handled by `docs-dev/q3a-botlib-dedicated-lifecycle-smoke-2026-06-17.md`.
- Decide which Q3A LibVars remain module-lifetime internal state and document the mapping before importing broader bot behavior code.
