# Q3A BotLib Route CPU Timing

Date: 2026-06-18

Related tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T16`

## Summary

This slice implements the route-side CPU timing fields from the bot performance source-counter plan without touching `bot_brain.cpp`. The work stays in `bot_nav.*`, `botlib_adapter.*`, and `q3a_botlib_import.*`, so the final status-print integration can be completed by the owner of the brain/status path.

## WORR Route Timing

`BotNavRouteStatus` now owns:

- `routeQueryCpuNs`
- `routeQueryCpuSamples`
- `routeQueryCpuMaxNs`
- `routeQueryCpuFailNs`
- `routeQueryCpuFailSamples`
- `routeReuseCpuNs`
- `routeReuseCpuSamples`

`BotNavRefreshRoute(...)` records one query sample around `BotNavBuildRouteWithFallback(...)`, immediately after `queries++`. Failed route builds add to the failure timing totals before `failures++` handling continues. The cache-reuse branch in `BotNav_GetRouteSteer(...)` records one reuse sample alongside the existing `reuses++` path.

## Q3A Route Timing

The import layer now owns 64-bit `q3aRouteCpu*` fields in both `Q3ABotLibImportSmokeStatus` and `Q3ABotLibImportSourceCounters`:

- `q3aRouteCpuNs`
- `q3aRouteCpuSamples`
- `q3aRouteCpuMaxNs`
- `q3aRouteCpuFailNs`
- `q3aRouteCpuFailSamples`

The timer is local to `q3a_botlib_import.c`: Windows uses `QueryPerformanceCounter`, and non-Windows builds use `CLOCK_MONOTONIC` when available. The three exported adapter-facing route entry points each record exactly one timing sample, including early failures.

`Q3A_BotLibImport_ResetAASRouteStatus(...)` resets the Q3A timing totals with the existing route build counters. `CopyImportStatus()` and `CopyImportSourceCounters()` mirror the new fields into `BotLibAdapterStatus` and `BotLibAdapterSourceCounters`.

## Follow-Up Boundary

This worker intentionally did not edit `bot_brain.cpp`, `tools/bot_perf`, the Q3A AAS plan, or the roadmap. Final log emission still needs the brain/status owner to print the `route_query_cpu_*`, `route_reuse_cpu_*`, and `q3a_route_cpu_*` snake_case fields.

## Validation

Completed local validation:

```powershell
git diff --check -- src/game/sgame/bots/bot_nav.hpp src/game/sgame/bots/bot_nav.cpp src/game/sgame/bots/q3a/q3a_botlib_import.h src/game/sgame/bots/q3a/q3a_botlib_import.c src/game/sgame/bots/botlib_adapter.hpp src/game/sgame/bots/botlib_adapter.cpp docs-dev/q3a-botlib-route-cpu-timing-2026-06-18.md
meson compile -C builddir-win sgame_x86_64
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas
```

Results:

- `git diff --check` passed with only Git's existing CRLF working-copy warnings for the edited source files.
- `meson compile -C builddir-win sgame_x86_64` passed and linked `sgame_x86_64.dll`. Ninja printed `premature end of file; recovering`, matching the active shared build-dir state, but the command exited 0.
- `refresh_install.py` passed, wrote `.install`, copied 16 root runtime files, packed 93 asset files into `.install/basew/pak0.pkz`, mirrored loose `botfiles`, injected `maps/mm-rage.aas`, and validated the `windows-x86_64` staged payload.
