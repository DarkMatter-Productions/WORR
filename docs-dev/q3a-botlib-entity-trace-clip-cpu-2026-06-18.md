# Q3A BotLib Entity Trace Clip CPU Counters

Date: 2026-06-18

Related tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice instruments the dynamic entity trace callback path used by imported Q3A BotLib AAS queries when they cross into WORR's `gi.clip(...)` entity collision bridge. The timing is measured around the registered entity-trace callback invocation in `AAS_EntityCollision(...)`, which keeps the work inside the adapter/import ownership boundary while still covering the runtime callback that performs the WORR clip.

The split `q3a_bot_source_counter_status` marker now emits these fields:

- `entity_trace_clip_calls`
- `entity_trace_clip_hits`
- `entity_trace_clip_misses`
- `entity_trace_clip_startsolid`
- `entity_trace_clip_allsolid`
- `entity_trace_clip_cpu_ns`
- `entity_trace_clip_cpu_max_ns`

## Implementation

- `src/game/sgame/bots/q3a/q3a_botlib_import.h` / `.c`
  - Added entity clip call/result/timing fields to `Q3ABotLibImportSmokeStatus` and `Q3ABotLibImportSourceCounters`.
  - Reused the existing monotonic nanosecond helper to time only the registered entity trace callback path.
  - Records a clip call and elapsed nanoseconds for every callback invocation, including failed callback returns; hit/miss/startsolid/allsolid classification is taken from the returned trace result.
  - Resets the clip counters with the existing entity trace reset path used during entity sync, AAS load/unload, shutdown, and startup.

- `src/game/sgame/bots/botlib_adapter.hpp` / `.cpp`
  - Mirrored the new import fields through `BotLibAdapterStatus` and `BotLibAdapterSourceCounters`.

- `src/game/sgame/bots/bot_brain.cpp`
  - Added the seven snake_case `entity_trace_clip_*` fields to `q3a_bot_source_counter_status`.
  - Kept Worker A's static BSP CPU fields and the main-thread AAS memory source-counter fields in place.

- `tools/bot_perf/analyze_bot_perf.py` / `tools/bot_perf/test_analyze_bot_perf.py`
  - The dirty working copy already contained analyzer derivation and synthetic-log coverage for the `entity_trace_clip_*` fields, so no additional analyzer edits were required in this slice.

## Integration Notes

- The measured CPU time includes the C-to-C++ adapter callback and the runtime callback body around `gi.clip(...)`; it intentionally does not include the higher-level Q3A entity trace bookkeeping before/after the callback.
- `entity_trace_clip_calls` is the analyzer denominator for `entity_trace_clip_cpu_avg_us`.
- High-level `entity_trace_failures` remains the failure counter for callback failure. Clip hit/miss counters are focused on callback path results.

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
- Focused object build passed for `q3a_botlib_import.c`, `botlib_adapter.cpp`, and `bot_brain.cpp`.
- `meson compile -C builddir-win sgame_x86_64` passed and linked `sgame_x86_64.dll`.
- Ninja printed the existing shared-build-dir warning `premature end of file; recovering` during the build commands.
