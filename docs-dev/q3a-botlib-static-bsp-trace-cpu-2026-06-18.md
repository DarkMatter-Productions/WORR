# Q3A BotLib Static BSP Trace CPU Timing

Date: 2026-06-18

Related tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T16`

## Summary

This slice closes the static BSP trace CPU gap in the Q3A BotLib source-counter path. `AAS_Trace(...)` and `AAS_PointContents(...)` now record cumulative nanoseconds, sample count, and max single-sample nanoseconds for active-map Q2 BSP collision and point-contents work.

The counters are emitted on the split `q3a_bot_source_counter_status` marker:

- `bsp_trace_cpu_ns`
- `bsp_trace_cpu_samples`
- `bsp_trace_cpu_max_ns`

The existing count and pressure counters remain unchanged: `aas_trace_calls`, `bsp_trace_calls`, point/box/zero-length trace counts, hit/miss/startsolid/allsolid counts, hull-node visits, and brush tests.

## Implementation

- `src/game/sgame/bots/q3a/q3a_botlib_import.h` / `.c`
  - Added `bspTraceCpuNs`, `bspTraceCpuSamples`, and `bspTraceCpuMaxNs` to `Q3ABotLibImportSmokeStatus` and `Q3ABotLibImportSourceCounters`.
  - Reused the existing monotonic nanosecond helper already used for Q3A route timing.
  - Wrapped `AAS_Trace(...)` and `AAS_PointContents(...)` so both static BSP collision and point-contents calls contribute to the same CPU sample stream.
  - Reset the timing counters with `Q3A_BotLibImport_ResetBspTraceCounters(...)`, alongside the existing static BSP trace counters.

- `src/game/sgame/bots/botlib_adapter.hpp` / `.cpp`
  - Mirrored the three new fields through `BotLibAdapterStatus` and `BotLibAdapterSourceCounters`.

- `src/game/sgame/bots/bot_brain.cpp`
  - Appended the three snake_case fields to the existing `q3a_bot_source_counter_status` line. This is the only edit outside the initially preferred import/adapter/analyzer files, and it is required for scenario/perf logs to expose the counters.

- `tools/bot_perf/analyze_bot_perf.py`
  - Added `bsp_trace_cpu_samples` to the static BSP trace source-counter group.
  - Updated CPU metric derivation to prefer `bsp_trace_cpu_samples` while falling back to older logs that only have `bsp_trace_calls`.

- `tools/bot_perf/test_analyze_bot_perf.py`
  - Added coverage for split-marker parsing of static BSP CPU timing fields.

## Validation

Completed commands:

```powershell
python -m py_compile .\tools\bot_perf\analyze_bot_perf.py .\tools\bot_perf\test_analyze_bot_perf.py
python -m unittest .\tools\bot_perf\test_analyze_bot_perf.py
ninja -C builddir-win libq3a_botlib_utility.a.p/src_game_sgame_bots_q3a_q3a_botlib_import.c.obj sgame_x86_64.dll.p/src_game_sgame_bots_botlib_adapter.cpp.obj sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj
meson compile -C builddir-win sgame_x86_64
```

Results:

- Python compile passed.
- Bot perf analyzer tests passed: 12 tests.
- Focused Ninja object build passed for `q3a_botlib_import.c`, `botlib_adapter.cpp`, and `bot_brain.cpp`. Ninja printed the existing shared-build-dir warning `premature end of file; recovering`.
- `meson compile -C builddir-win sgame_x86_64` passed and linked `sgame_x86_64.dll`. Ninja printed the same shared-build-dir warning.

## Integration Notes

- The analyzer average for `bsp_trace_cpu_avg_us` now uses `bsp_trace_cpu_samples` when present. This matters because point-contents calls are timed even though they do not increment `bsp_trace_calls`.
- Older logs that have `bsp_trace_cpu_ns` and `bsp_trace_calls` but not `bsp_trace_cpu_samples` still derive a static BSP trace CPU average using the old denominator.
- Runtime dynamic entity clip timing remains separate and is not changed by this slice.
