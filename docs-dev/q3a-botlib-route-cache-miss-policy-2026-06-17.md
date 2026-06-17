# Q3A BotLib Route Cache Miss Policy

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

Imported Q3A route-cache initialization probes `maps/<map>.rcd` through `botimport.FS_FOpenFile` after the active AAS load. WORR does not package route-cache dump files today, so that read miss is expected and should not look like a broken filesystem bridge.

This round keeps the filesystem bridge read-only, but classifies `.rcd` read misses as optional route-cache misses. True read failures for required files still increment `q3a_fs_open_failures`.

## Implementation Notes

- `q3a_botlib_import.*`
  - Adds `filesystemRouteCacheMisses` to the import smoke status.
  - Detects `.rcd` paths in `Q3A_BotLibFSOpenFile`.
  - Increments the route-cache miss counter instead of `filesystemOpenFailures` when the optional cache file is absent.
  - Leaves `FS_WRITE` rejected until WORR intentionally supports route-cache dump writes.
- `botlib_adapter.*`
  - Copies the route-cache miss counter into adapter status.
- `bot_runtime.cpp`
  - Prints `q3a_fs_route_cache_misses` on the verbose filesystem status line.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Scratch import harness:
  `clang++ -std=c++17 -fuse-ld=lld .tmp\q3a_route_cache_miss_harness.cpp builddir-win\libq3a_botlib_utility.a -Isrc\game\sgame\bots -Isrc\game\sgame\bots\q3a -Isrc\game\sgame\bots\q3a\game -Isrc\game\sgame\bots\q3a\botlib -o .tmp\q3a_route_cache_miss_harness.exe`
- Harness evidence:
  `loaded=1 q3a_fs=Q3A BotLib filesystem bridge passed q3a_fs_passed=1 q3a_fs_open_attempts=2 q3a_fs_files=1 q3a_fs_memory_files=0 q3a_fs_open_failures=0 q3a_fs_route_cache_misses=1 q3a_fs_read_bytes=277484 q3a_fs_closes=1`

## Outstanding Work

- Add a deliberate write policy before enabling Q3A route-cache dump writes.
- Decide whether WORR should ever package generated `.rcd` route-cache dumps or keep route cache data transient/import-owned.
