# Q3A BotLib Runtime AAS Shell

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice starts Phase 2 with a WORR-native BotLib/AAS runtime shell. It does
not import the Quake III Arena BotLib runtime yet and does not make bots move.
Instead, it establishes the server-game lifecycle, public `sg_bot_*` control
cvars, and a packaged-AAS loading probe that future BotLib adapter work can
build on.

The important behavior is deliberately small: when `sg_bot_enable` is set, the
server game tries to load `maps/<current-map>.aas` through WORR's filesystem
extension, which means loose files and packaged `pak0.pkz` members follow the
same search path as other game assets. The loader decodes the BSPC/Q3A AAS v5
header transform, validates the `EAAS` version 5 lump bounds, then records area,
area-settings, reachability, and cluster counts for runtime status/debug output.

## Implementation

Added:

- `src/game/sgame/bots/bot_runtime.hpp`
- `src/game/sgame/bots/bot_runtime.cpp`

Runtime shell responsibilities:

- Register initial public bot cvars:
  - `sg_bot_enable`
  - `sg_bot_debug`
  - `sg_bot_debug_aas`
  - `sg_bot_debug_route`
  - `sg_bot_debug_goal`
  - `sg_bot_cpu_budget_ms`
- Track a small `BotAasRuntimeStatus` for the active map.
- Use the `FILESYSTEM_API_V1` server extension to load `maps/<map>.aas`.
- Decode the AAS v5 header transform used by Q3A/BSPC.
- Validate the AAS header ident, version, file size, and lump bounds.
- Derive initial structural metrics from fixed-size AAS lumps:
  - area count
  - area-settings count
  - reachability count
  - cluster count
- Keep bot frame stubs gated on `sg_bot_enable` and loaded AAS state.

Integration points:

- `InitGame` registers the new `sg_bot_*` cvars.
- `SpawnEntities` and `G_ResetWorldEntitiesFromSavedString` end any previous
  runtime state before level memory is cleared, then begin the runtime after
  level entity setup finishes.
- `G_RunFrame_` ticks runtime debug/status output once per frame.
- `ShutdownGame` clears runtime state before game/level tags are freed.
- `meson.build` includes the new runtime source in `sgame_x86_64`.

## Current Limits

- No Q3A BotLib runtime files are imported yet.
- No `botlib_import_t` callbacks are implemented yet.
- AAS data is header-validated and summarized, not converted into live BotLib
  routing queries.
- Bots still do not generate movement or combat input from this runtime shell.
- `sg_bot_debug_route` and `sg_bot_debug_goal` are registered for the planned
  debug surface, but route/goal overlays remain future work.

## Validation

Build validation:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result:

- `sgame_x86_64.dll` compiled and linked successfully.
- `tools/refresh_install.py --package-q2aas-aas --platform-id windows-x86_64`
  refreshed `.install`, re-injected packaged AAS, audited `maps/mm-rage.aas`,
  and passed staged Windows payload validation.

Runtime smoke:

```powershell
$env:WORR_LOG_LEVEL='debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name bot_aas_runtime_debug_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 30 +quit; Remove-Item Env:WORR_LOG_LEVEL
```

Result:

- The dedicated server loaded `.install\basew\sgame_x86_64.dll`.
- The runtime loaded packaged `maps/mm-rage.aas`.
- Reported structural counts: `areas=428`, `reachability=562`, `clusters=4`,
  `bytes=277484`.

## Credits and Provenance

This is a WORR-native runtime shell. It references the credited Quake III
Arena/BSPC AAS file format constants needed to recognize `EAAS` version 5 and
the standard AAS lump table, plus the Q3A/BSPC AAS v5 header transform, but it
does not copy Q3A BotLib runtime code.

The imported-source ledger records this as native WORR integration around the
credited Q3A/BSPC AAS format.

## Next Work

- Add the quarantined imported-code boundary for Q3A BotLib runtime files.
- Implement the first `botlib_import_t` callback table against WORR services.
- Replace the header probe with real BotLib AAS load/unload/query calls.
- Extend the packaged-map runtime smoke from structural header/count validation
  to successful BotLib area/reachability queries.
