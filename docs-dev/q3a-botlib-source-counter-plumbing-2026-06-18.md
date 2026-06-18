# Q3A BotLib Source Counter Plumbing

Date: 2026-06-18

Related tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T16`

## Summary

This source-side slice adds low-risk Q3A BotLib/AAS performance counters to the import status, mirrors them through `BotLibAdapterStatus`, and exposes stable grouped source-counter getters for scenario/status printing. The final integration emits these counters on a dedicated `q3a_bot_source_counter_status` marker so the primary `q3a_bot_frame_command_status` row stays below the engine print-buffer limit.

The counters are intended to feed the Phase 9 analyzer contract defined in `docs-dev/q3a-botlib-bot-perf-source-counters-2026-06-18.md`. Timing fields are still pending because this import layer does not currently expose an obvious monotonic nanosecond primitive.

## Stable Read API

The source counters can now be read without depending on the broad smoke/status object:

- C import layer: `Q3ABotLibImportSourceCounters` plus `Q3A_BotLibImport_GetSourceCounters(...)`.
- C++ adapter layer: `BotLibAdapterSourceCounters` plus `BotLibAdapter_GetSourceCounters()`.

The adapter getter refreshes its process-local snapshot from the import layer before returning it. It does not reset counters, allocate memory, or touch route/visibility/trace behavior, so scenario status printing can read it cheaply after frame-command work.

Final status-print field names should stay lowercase `snake_case`. The adapter names map directly to the dedicated source-counter marker, and `tools/bot_perf/analyze_bot_perf.py` merges that marker with the frame-command status payload before deriving rates:

| Adapter field | Final status field |
| --- | --- |
| `q3aRouteBuildAttempts` | `q3a_route_build_attempts` |
| `q3aRouteBuildSuccesses` | `q3a_route_build_successes` |
| `q3aRouteBuildFailures` | `q3a_route_build_failures` |
| `q3aAasInpvsChecks` | `aas_inpvs_checks` |
| `q3aAasInpvsVisible` | `aas_inpvs_visible` |
| `q3aAasInpvsMisses` | `aas_inpvs_misses` |
| `q3aAasInphsChecks` | `aas_inphs_checks` |
| `q3aAasInphsVisible` | `aas_inphs_visible` |
| `q3aAasInphsMisses` | `aas_inphs_misses` |
| `q3aVisibilityClusterChecks` | `visibility_cluster_checks` |
| `q3aVisibilityClusterSame` | `visibility_cluster_same` |
| `q3aVisibilityClusterInvalid` | `visibility_cluster_invalid` |
| `q3aVisibilityDecompressCalls` | `visibility_decompress_calls` |
| `q3aVisibilityDecompressBytes` | `visibility_decompress_bytes` |
| `q3aVisibilityDecompressRuns` | `visibility_decompress_runs` |
| `q3aVisibilityDecompressFailures` | `visibility_decompress_failures` |
| `q3aEntityTraceAttempts` | `entity_trace_attempts` |
| `q3aEntityTraceHits` | `entity_trace_hits` |
| `q3aEntityTraceMisses` | `entity_trace_misses` |
| `q3aEntityTraceFailures` | `entity_trace_failures` |
| `q3aAasTraceCalls` | `aas_trace_calls` |
| `q3aBspTraceCalls` | `bsp_trace_calls` |
| `q3aBspTracePointCalls` | `bsp_trace_point_calls` |
| `q3aBspTraceBoxCalls` | `bsp_trace_box_calls` |
| `q3aBspTraceZeroLengthCalls` | `bsp_trace_zero_length_calls` |
| `q3aBspTraceHits` | `bsp_trace_hits` |
| `q3aBspTraceMisses` | `bsp_trace_misses` |
| `q3aBspTraceStartSolid` | `bsp_trace_startsolid` |
| `q3aBspTraceAllSolid` | `bsp_trace_allsolid` |
| `q3aBspTraceHullNodes` | `bsp_trace_hull_nodes` |
| `q3aBspTraceBrushTests` | `bsp_trace_brush_tests` |

## Implemented Counters

Route build counters:

- Import fields: `routeBuildAttempts`, `routeBuildSuccesses`, `routeBuildFailures`.
- Adapter fields ready for final print integration: `q3aRouteBuildAttempts`, `q3aRouteBuildSuccesses`, `q3aRouteBuildFailures`.
- Stable grouped getters: `Q3A_BotLibImport_GetSourceCounters(...)` and `BotLibAdapter_GetSourceCounters()`.
- Counted at exported adapter-facing route build entry points: `Q3A_BotLibImport_BuildRouteSteer`, `Q3A_BotLibImport_BuildRouteSteerToGoal`, and `Q3A_BotLibImport_BuildRouteSteerForTravelType`.
- Internal route probes used by route-start discovery are not counted, keeping these fields aligned to source-side route requests from the adapter.

Visibility and PVS/PHS counters:

- Adapter fields ready for final print integration: `q3aAasInpvsChecks`, `q3aAasInpvsVisible`, `q3aAasInpvsMisses`, `q3aAasInphsChecks`, `q3aAasInphsVisible`, `q3aAasInphsMisses`.
- Additional visibility work counters ready: `q3aVisibilityClusterChecks`, `q3aVisibilityClusterSame`, `q3aVisibilityClusterInvalid`, `q3aVisibilityDecompressCalls`, `q3aVisibilityDecompressBytes`, `q3aVisibilityDecompressRuns`, `q3aVisibilityDecompressFailures`.
- Counted through `AAS_inPVS`, `AAS_inPHS`, `Q3A_BotLibImport_ClusterVisible`, and `Q3A_BotLibImport_DecompressVisByte`.

Static BSP trace counters:

- Adapter fields ready for final print integration: `q3aAasTraceCalls`, `q3aBspTraceCalls`, `q3aBspTracePointCalls`, `q3aBspTraceBoxCalls`, `q3aBspTraceZeroLengthCalls`, `q3aBspTraceHits`, `q3aBspTraceMisses`, `q3aBspTraceStartSolid`, `q3aBspTraceAllSolid`, `q3aBspTraceHullNodes`, `q3aBspTraceBrushTests`.
- Counted through `AAS_Trace` and the single Q2 BSP trace entry, including early miss returns, zero-length tests, point/box swept traces, recursive hull nodes, and brush tests.

Existing dynamic entity trace counters:

- Existing adapter fields remain ready for final print integration: `q3aEntityTraceAttempted`, `q3aEntityTraceHits`, `q3aEntityTraceMisses`, `q3aEntityTraceFailures`, plus `q3aEntityTraceCallbackSet`.
- This slice did not add `entity_trace_clip_*` counters because those belong to the WORR runtime `gi.clip(...)` bridge, outside this ownership lane.

## Reset and Copy Behavior

- Route build counters reset with `Q3A_BotLibImport_ResetAASRouteStatus`, which is already called during import init, AAS load reset, unload, and shutdown paths.
- Static BSP trace counters reset with BSP collision data clear.
- Visibility counters reset with BSP visibility data clear.
- Adapter route-build wrappers now call `CopyImportStatus()` after Q3A route calls so final integration can read fresh source counters immediately after route command work.

## Final Print Integration

- `BotBrain_PrintFrameCommandStatus(...)` now reads `BotLibAdapter_GetSourceCounters()` once per status print.
- The counters are emitted on `q3a_bot_source_counter_status`, not on the primary `q3a_bot_frame_command_status` line. A temporary inline attempt pushed the primary status row past the engine print-buffer limit and truncated the trailing `pass=` field, so the split marker is now the stable contract.
- `tools/bot_perf/analyze_bot_perf.py` accepts both old inline source counters and the split marker, merging `q3a_bot_source_counter_status` into its parsed status dictionary.

## Remaining Gaps

- Q3A route CPU nanosecond counters are still absent; add them only after a monotonic timing primitive is agreed for this layer.
- Static BSP trace CPU counters are absent for the same timing reason.
- Runtime `entity_trace_clip_*` counters around `gi.clip(...)` remain owned by runtime-side work, not this import/adapter slice.

## Validation

Worker D completion commands:

```powershell
git diff --check -- src/game/sgame/bots/botlib_adapter.cpp src/game/sgame/bots/botlib_adapter.hpp src/game/sgame/bots/q3a/q3a_botlib_import.c src/game/sgame/bots/q3a/q3a_botlib_import.h docs-dev/q3a-botlib-source-counter-plumbing-2026-06-18.md
meson compile -C builddir-win sgame_x86_64
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas
```

Results:

- `git diff --check` passed with only Git's existing CRLF working-copy warnings for the edited source files.
- `meson compile -C builddir-win sgame_x86_64` passed and linked `sgame_x86_64.dll`. Two earlier 120-second tool-wrapper timeouts left live Ninja jobs in the build directory; after waiting for those jobs to finish, the explicit 300-second run completed. Ninja printed `premature end of file; recovering` for the interrupted log, but the final build exited 0.
- `refresh_install.py` passed, wrote `.install`, copied 16 root runtime files, rebuilt `.install/basew/pak0.pkz` from 87 asset files, mirrored loose `botfiles`, injected `maps/mm-rage.aas` (`277484` bytes, SHA-256 `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`), and validated the `windows-x86_64` staged payload.
