# Q3A BotLib AAS Debug Draw Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice replaces the temporary Q3A debug-line no-op path with a WORR-owned debug draw callback bridge. Imported Q3A AAS code can now emit debug lines, permanent lines, crosses, arrows, and clear requests through the `botlib_adapter.*` boundary, and `bot_runtime.cpp` maps those primitives to WORR `gi.Draw_*` imports.

The bridge is gated by developer cvars. It draws only when `sg_bot_debug_aas >= 3`, `sg_bot_debug_route`, or `sg_bot_debug_goal` is enabled.

## Implementation

- Added a C-compatible debug draw callback ABI to `q3a_botlib_import.h`.
- Replaced the no-op `AAS_DebugLine`, `AAS_PermanentLine`, `AAS_ClearShownDebugLines`, `AAS_DrawPermanentCross`, and `AAS_DrawArrow` bridge functions with callback-backed implementations.
- Added `Q3A_BotLibImport_RunDebugDrawSmoke()`, which draws a line/arrow between the imported route-smoke start and goal areas and records callback/counter status.
- Added adapter-side callback registration and status copying for debug draw attempts, primitive counts, and failures.
- Added a runtime callback that maps Q3A line color IDs to WORR `rgba_t` values and draws with `gi.Draw_Line` / `gi.Draw_Arrow`.
- Triggered the generic debug draw smoke from the bot runtime frame loop when `sg_bot_debug_aas >= 3` is active. A later route-overlay slice now uses the same primitive bridge under `sg_bot_debug_route` / `sg_bot_debug_goal`.

This is a debug primitive bridge, not final bot navigation visualization. Future `bot_nav.*` work still needs to draw selected bot routes, current areas, reachability types, and stuck reasons.

## Validation

Build:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Install refresh and package preservation:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas
```

Runtime smoke:

```powershell
$env:WORR_LOG_LEVEL = 'debug'
try {
  .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_debug_draw_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 3 +map mm-rage +wait 90 +quit *> .tmp\q3a_aas_debug_draw_smoke_stdout.log
  $code = $LASTEXITCODE
} finally {
  Remove-Item Env:WORR_LOG_LEVEL -ErrorAction SilentlyContinue
}
exit $code
```

Observed:

```text
q3a_debug_draw=Q3A debug draw bridge passed: callback=yes lines=2 crosses=1 arrows=1 clears=1 failures=0
q3a_debug_draw_callback=yes
q3a_debug_draw_lines=2
q3a_debug_draw_crosses=1
q3a_debug_draw_arrows=1
q3a_debug_draw_clears=1
q3a_debug_draw_failures=0
q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes
q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024
q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18
```

## Remaining Work

- Feed real route/goal overlays from `bot_nav.*` once bot steering exists; the imported route-query smoke now has a dedicated overlay bridge in `docs-dev/q3a-botlib-aas-route-overlay-2026-06-17.md`.
- Q3A debug polygon create/delete bridging is covered by `docs-dev/q3a-botlib-aas-debug-polygon-bridge-2026-06-17.md`; imported AAS area helper coverage is covered by `docs-dev/q3a-botlib-aas-debug-area-helpers-2026-06-17.md`.
- Keep the debug draw bridge behind developer cvars so normal bot runtime smoke stays quiet.
