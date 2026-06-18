# Q3A BotLib Bot Performance Source Counter Proposal and Analyzer Support

Date: 2026-06-18

Related tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T16`

## Summary

The bot performance analyzer derives useful route/debug/recovery pressure from `q3a_bot_frame_command_status`. This proposal defines the source counters needed to make CPU, visibility, and trace budgets direct instead of proxy-based.

No game source changes are made in this tooling slice. The names below remain the proposed final `q3a_bot_frame_command_status` fields.

Analyzer support now exists in `tools/bot_perf/analyze_bot_perf.py`: when these fields appear in logs, the tool keeps old route/debug/recovery metrics, records present/missing source-counter groups, emits source counters in JSON, adds derived CPU/visibility/trace metrics to text/CSV/Markdown/comparison output, and leaves older logs analyzable with `n/a` optional metrics.

## Analyzer Support Status

Implemented under `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, and `FR-04-T16`:

- Bot command CPU: derives `bot_frame_cpu_total_ms`, `bot_frame_cpu_ms_per_sec`, `bot_frame_cpu_ms_per_bot_sec`, `bot_frame_cpu_avg_us`, `bot_frame_cpu_max_us`, and `bot_frame_cpu_success_avg_us` from `bot_frame_cpu_*` fields. Early alias support accepts `bot_cpu_ns`, `bot_cpu_ms`, and `bot_frame_cpu_ms` for total CPU only.
- WORR route CPU: derives route-query and route-reuse CPU totals, per-second/per-bot rates, average microseconds, max microseconds, and failed-query average microseconds from `route_query_cpu_*` / `route_reuse_cpu_*` fields. Early alias support accepts `bot_route_cpu_ms` for total route-query CPU only.
- Q3A route import CPU: derives `q3a_route_cpu_*` totals, per-second/per-bot rates, average/max microseconds, and failed-route average microseconds.
- Visibility and PVS/PHS pressure: derives per-second and per-bot rates for `aas_inpvs_checks`, `aas_inphs_checks`, `visibility_cluster_checks`, `visibility_decompress_calls`, and `visibility_decompress_bytes`, plus visible ratios and decompression failure reporting.
- Static BSP trace pressure: derives per-second/per-bot rates for `aas_trace_calls`, `bsp_trace_calls`, `bsp_trace_hull_nodes`, and `bsp_trace_brush_tests`, plus static BSP trace CPU rates/averages/max and hit ratio.
- Dynamic entity trace pressure: derives per-second/per-bot rates for `entity_trace_attempts` and `entity_trace_clip_calls`, plus clip CPU rates/averages/max, clip hit ratio, and failure reporting.
- Comparison output now includes the main source metrics: `bot_frame_cpu_ms_per_bot_sec`, `route_query_cpu_ms_per_bot_sec`, `q3a_route_cpu_ms_per_bot_sec`, `aas_inpvs_checks_per_bot_sec`, `aas_inphs_checks_per_bot_sec`, `bsp_trace_calls_per_bot_sec`, and `entity_trace_clip_calls_per_bot_sec`.

Validation completed for this tooling slice:

```powershell
python -m py_compile .\tools\bot_perf\analyze_bot_perf.py .\tools\bot_perf\test_analyze_bot_perf.py
python -m unittest .\tools\bot_perf\test_analyze_bot_perf.py
```

The unit coverage includes a legacy log with no source counters and a synthetic source-counter log covering CPU, visibility/PVS/PHS, static BSP trace, and dynamic entity trace fields. Source-side emission in `sgame`/BotLib remains pending.

## Q3A Bot Port Validation Mapping

The analyzer treats source counters as optional until the game-side emitters land, but each group now maps directly to the Phase 9 validation plan in `docs-dev/plans/q3a-botlib-aas-port.md`:

| Source counter group | Analyzer outputs | Phase 9 validation use |
| --- | --- | --- |
| `bot_frame_cpu_*` | `bot_frame_cpu_ms_per_sec`, `bot_frame_cpu_ms_per_bot_sec`, `bot_frame_cpu_avg_us`, `bot_frame_cpu_max_us` | Measures `Performance / CPU cost per bot` without relying on command throughput as a proxy. |
| `route_query_cpu_*`, `route_reuse_cpu_*` | `route_query_cpu_ms_per_bot_sec`, `route_query_cpu_avg_us`, `route_query_cpu_fail_avg_us`, `route_reuse_cpu_avg_us` | Pairs existing `Route recomputation rate` checks with real route CPU cost and failed-route cost. |
| `q3a_route_cpu_*` | `q3a_route_cpu_ms_per_bot_sec`, `q3a_route_cpu_avg_us`, `q3a_route_cpu_fail_avg_us` | Splits imported Q3A route-builder cost from WORR-side route/cache policy cost. |
| `aas_inpvs_*`, `aas_inphs_*`, `visibility_*` | `aas_inpvs_checks_per_bot_sec`, `aas_inphs_checks_per_bot_sec`, visible ratios, decompression rates/failures | Supplies direct data for `Performance / Visibility trace count` and visibility-lump regression checks. |
| `aas_trace_calls`, `bsp_trace_*` | `bsp_trace_calls_per_bot_sec`, `bsp_trace_brush_tests_per_sec`, `bsp_trace_cpu_avg_us`, `bsp_trace_hit_ratio` | Tracks static collision pressure from imported AAS tracing and active-map BSP collision bridge validation. |
| `entity_trace_*` | `entity_trace_attempts_per_sec`, `entity_trace_clip_calls_per_bot_sec`, `entity_trace_clip_cpu_avg_us`, `entity_trace_failures` | Tracks dynamic entity collision pressure behind `AAS_EntityCollision` and WORR `gi.clip(...)` bridge validation. |
| Present/missing source groups | `source_counter_groups_present`, `source_counter_groups_missing`, `missing_instrumentation` | Lets old soak logs remain valid while new post-counter logs show which Phase 9 source data is still absent. |

## Existing Measurement Surface

The current emission path is:

- `src/server/main.c`: dedicated smoke harness calls `SV_BotFrameCommandStatus(...)`.
- `src/game/sgame/bots/bot_think.cpp`: `Bot_FrameCommandStatus(...)` forwards to `BotBrain_PrintFrameCommandStatus(...)`.
- `src/game/sgame/bots/bot_brain.cpp`: `BotBrain_PrintFrameCommandStatus(...)` prints the final `q3a_bot_frame_command_status` line.
- `src/game/sgame/bots/bot_nav.hpp` / `bot_nav.cpp`: `BotNavRouteStatus` owns route/cache/debug/stuck/item counters used by the final status line.
- `src/game/sgame/bots/q3a/q3a_botlib_import.h` / `.c`: `Q3ABotLibImportSmokeStatus` owns Q3A bridge counters copied into `BotLibAdapterStatus`.
- `src/game/sgame/bots/botlib_adapter.hpp` / `.cpp`: `BotLibAdapterStatus` mirrors Q3A import status into C++ runtime code.

The analyzer already budgets:

- `commands_per_bot_sec`
- `route_queries_per_bot_sec`
- `route_refresh_ratio`
- `route_reuse_ratio`
- `debug_work_units_per_bot_sec`
- `recovery_command_uses_per_bot_sec`
- raw `route_failures`, `route_invalid_slots`, `route_debug_missing_frames`, and `skipped_inactive`

The counters below should be added to that same status line so the analyzer can populate direct CPU, visibility, and trace budget fields from real source data.

## Counter Contract

### Bot Command CPU

Owner: `src/game/sgame/bots/bot_brain.cpp`

Likely functions:

- `BotBrain_BuildFrameCommand(...)`
- `BotBrain_PrintFrameCommandStatus(...)`

Proposed status fields:

- `bot_frame_cpu_ns`: cumulative nanoseconds spent in `BotBrain_BuildFrameCommand(...)`, including route request build, `BotNav_GetRouteSteer(...)`, command construction, movement-state intent, and recovery command application.
- `bot_frame_cpu_samples`: number of timed `BotBrain_BuildFrameCommand(...)` invocations. This should match attempted frames where timing began, not only successful commands.
- `bot_frame_cpu_max_ns`: largest single `BotBrain_BuildFrameCommand(...)` duration.
- `bot_frame_cpu_success_ns`: cumulative nanoseconds for invocations that return `true`.
- `bot_frame_cpu_success_samples`: number of successful timed command builds.

Rationale:

- These fields answer "CPU cost per bot" directly once the analyzer divides by duration and detected bot count.
- Separating attempts from successes keeps inactive/runtime-skipped conditions visible without hiding expensive failed command paths.

Analyzer follow-up metrics:

- `bot_frame_cpu_ms_per_sec = bot_frame_cpu_ns / 1_000_000 / duration_sec`
- `bot_frame_cpu_ms_per_bot_sec = bot_frame_cpu_ns / 1_000_000 / duration_sec / bot_count`
- `bot_frame_cpu_avg_us = bot_frame_cpu_ns / bot_frame_cpu_samples / 1000`
- `bot_frame_cpu_max_us = bot_frame_cpu_max_ns / 1000`

### Route Query CPU

Owner: `src/game/sgame/bots/bot_nav.hpp` / `bot_nav.cpp`

Likely functions:

- `BotNav_GetRouteSteer(...)`
- `BotNavRefreshRoute(...)`
- `BotNavBuildRouteWithFallback(...)`

Proposed `BotNavRouteStatus` fields and final status fields:

- `route_query_cpu_ns`: cumulative nanoseconds around route recomputation work. The timer should start immediately before `BotNavBuildRouteWithFallback(...)` and stop immediately after it returns.
- `route_query_cpu_samples`: number of timed route recomputation attempts. This should track the existing `route_queries` count.
- `route_query_cpu_max_ns`: largest single route recomputation duration.
- `route_query_cpu_fail_ns`: cumulative nanoseconds for recomputations that fail and increment `route_failures`.
- `route_query_cpu_fail_samples`: number of failed timed recomputations.
- `route_reuse_cpu_ns`: optional cumulative nanoseconds for the cache-reuse branch in `BotNav_GetRouteSteer(...)`.
- `route_reuse_cpu_samples`: optional number of timed route-reuse branches. This should track the existing `route_reuses` count if enabled.

Rationale:

- The current analyzer can report route recomputation rate and refresh ratio, but cannot tell whether a route query is cheap or expensive.
- Timing `BotNavBuildRouteWithFallback(...)` isolates the expensive imported Q3A route work while leaving item-goal scans and command construction in `bot_frame_cpu_ns`.
- Optional reuse timing provides a contrast baseline; it can be skipped in the first implementation if the team wants minimal overhead.

Analyzer follow-up metrics:

- `route_query_cpu_ms_per_sec`
- `route_query_cpu_ms_per_bot_sec`
- `route_query_cpu_avg_us`
- `route_query_cpu_max_us`
- `route_query_cpu_fail_avg_us`
- existing `route_queries_per_bot_sec` and `route_refresh_ratio` become rate/cost pairings instead of standalone proxies.

### Q3A Route Import CPU

Owner: `src/game/sgame/bots/q3a/q3a_botlib_import.h` / `.c`, copied by `src/game/sgame/bots/botlib_adapter.*`

Likely functions:

- `Q3A_BotLibImport_BuildRouteSteer(...)`
- `Q3A_BotLibImport_BuildRouteSteerToGoal(...)`
- `Q3A_BotLibImport_BuildRouteSteerForTravelType(...)`
- `Q3A_BotLibImport_BuildRouteSteerInternal(...)`

Proposed import status fields:

- `q3a_route_cpu_ns`
- `q3a_route_cpu_samples`
- `q3a_route_cpu_max_ns`
- `q3a_route_cpu_fail_ns`
- `q3a_route_cpu_fail_samples`

Proposed final status fields:

- `q3a_route_cpu_ns`
- `q3a_route_cpu_samples`
- `q3a_route_cpu_max_ns`
- `q3a_route_cpu_fail_ns`
- `q3a_route_cpu_fail_samples`

Rationale:

- `route_query_cpu_ns` measures WORR nav-side recomputation. `q3a_route_cpu_ns` measures the imported route builder itself.
- If these diverge, the difference points to WORR goal selection/fallback overhead outside the import.

### Visibility/PVS/PHS Pressure

Owner: `src/game/sgame/bots/q3a/q3a_botlib_import.h` / `.c`, copied by `src/game/sgame/bots/botlib_adapter.*`

Likely functions:

- `AAS_inPVS(...)`
- `AAS_inPHS(...)`
- `Q3A_BotLibImport_PointsVisibleQ2Bsp(...)`
- `Q3A_BotLibImport_ClusterVisible(...)`
- `Q3A_BotLibImport_DecompressVisByte(...)`

Proposed import status fields:

- `aas_inpvs_checks`: count of `AAS_inPVS(...)` calls.
- `aas_inpvs_visible`: count of `AAS_inPVS(...)` calls returning true.
- `aas_inpvs_misses`: count of `AAS_inPVS(...)` calls returning false.
- `aas_inphs_checks`: count of `AAS_inPHS(...)` calls.
- `aas_inphs_visible`: count of `AAS_inPHS(...)` calls returning true.
- `aas_inphs_misses`: count of `AAS_inPHS(...)` calls returning false.
- `visibility_cluster_checks`: count of `Q3A_BotLibImport_ClusterVisible(...)` calls.
- `visibility_cluster_same`: count of same-cluster short-circuits.
- `visibility_cluster_invalid`: count of invalid cluster inputs.
- `visibility_decompress_calls`: count of `Q3A_BotLibImport_DecompressVisByte(...)` calls.
- `visibility_decompress_bytes`: cumulative compressed visibility bytes read while seeking target bytes.
- `visibility_decompress_runs`: cumulative zero-run records consumed.
- `visibility_decompress_failures`: count of failed decompressions or invalid rows.

Proposed final status fields:

- Same as the import status field names above.

Rationale:

- `aas_inpvs_checks` and `aas_inphs_checks` directly answer PVS/PHS pressure.
- Visible/miss split tells whether checks are useful or mostly rejecting.
- Decompression counters identify maps or bot paths that repeatedly walk expensive compressed visibility rows.

Analyzer follow-up metrics:

- `aas_inpvs_checks_per_sec`
- `aas_inpvs_checks_per_bot_sec`
- `aas_inphs_checks_per_sec`
- `aas_inphs_checks_per_bot_sec`
- `visibility_decompress_calls_per_sec`
- `visibility_decompress_bytes_per_sec`
- `visibility_decompress_failures`

### Static BSP Trace Pressure

Owner: `src/game/sgame/bots/q3a/q3a_botlib_import.h` / `.c`, copied by `src/game/sgame/bots/botlib_adapter.*`

Likely functions:

- `AAS_Trace(...)`
- `Q3A_BotLibImport_TraceQ2Bsp(...)`
- `Q3A_BotLibImport_RecursiveHullCheck(...)`
- `Q3A_BotLibImport_ClipBoxToBrush(...)`

Proposed import status fields:

- `aas_trace_calls`: count of `AAS_Trace(...)` calls.
- `bsp_trace_calls`: count of `Q3A_BotLibImport_TraceQ2Bsp(...)` calls.
- `bsp_trace_point_calls`: count of point traces.
- `bsp_trace_box_calls`: count of swept box traces.
- `bsp_trace_zero_length_calls`: count of zero-length box/point content checks through the trace path.
- `bsp_trace_hits`: count of traces with `fraction < 1.0`, `startsolid`, or `allsolid`.
- `bsp_trace_misses`: count of traces with full fraction and no solid start.
- `bsp_trace_startsolid`: count of traces ending `startsolid`.
- `bsp_trace_allsolid`: count of traces ending `allsolid`.
- `bsp_trace_hull_nodes`: cumulative recursive hull nodes visited.
- `bsp_trace_brush_tests`: cumulative brush tests.
- `bsp_trace_cpu_ns`: cumulative nanoseconds around `Q3A_BotLibImport_TraceQ2Bsp(...)`.
- `bsp_trace_cpu_max_ns`: largest single static BSP trace duration.

Proposed final status fields:

- Same as the import status field names above.

Rationale:

- Static BSP trace pressure is separate from dynamic entity trace pressure.
- Hull node and brush test counts give a map-complexity proxy that CPU time alone does not explain.

Analyzer follow-up metrics:

- `bsp_trace_calls_per_sec`
- `bsp_trace_calls_per_bot_sec`
- `bsp_trace_brush_tests_per_sec`
- `bsp_trace_cpu_ms_per_sec`
- `bsp_trace_avg_us`
- `bsp_trace_max_us`

### Dynamic Entity Trace Pressure

Owner: `src/game/sgame/bots/bot_runtime.cpp`, `src/game/sgame/bots/q3a/q3a_botlib_import.h` / `.c`, copied by `src/game/sgame/bots/botlib_adapter.*`

Likely functions:

- `AAS_EntityCollision(...)`
- `BotRuntimeEntityTrace(...)`
- `gi.clip(...)` call inside `BotRuntimeEntityTrace(...)`

Existing fields:

- `q3a_entity_trace_attempts` appears in verbose adapter status.
- `entityTraceAttempted`, `entityTraceHits`, `entityTraceMisses`, and `entityTraceFailures` already exist in Q3A import status.

Proposed new final status fields:

- `entity_trace_attempts`: raw `AAS_EntityCollision(...)` attempts during the smoke window.
- `entity_trace_hits`: hit count.
- `entity_trace_misses`: miss count.
- `entity_trace_failures`: callback or bridge failures.
- `entity_trace_clip_calls`: number of actual `gi.clip(...)` calls. This can be less than attempts because `BotRuntimeEntityTrace(...)` exits early for invalid, unused, unlinked, or non-solid entities.
- `entity_trace_clip_hits`: `gi.clip(...)` results that hit.
- `entity_trace_clip_misses`: `gi.clip(...)` results that miss.
- `entity_trace_clip_startsolid`: `gi.clip(...)` results with `startSolid`.
- `entity_trace_clip_allsolid`: `gi.clip(...)` results with `allSolid`.
- `entity_trace_clip_cpu_ns`: cumulative nanoseconds around `gi.clip(...)`.
- `entity_trace_clip_cpu_max_ns`: largest single `gi.clip(...)` duration.

Rationale:

- Existing Q3A smoke counters prove the callback works, but the frame-command status does not include them.
- `entity_trace_clip_calls` distinguishes expensive real collision work from cheap rejected attempts.

Analyzer follow-up metrics:

- `entity_trace_attempts_per_sec`
- `entity_trace_clip_calls_per_sec`
- `entity_trace_clip_calls_per_bot_sec`
- `entity_trace_clip_cpu_ms_per_sec`
- `entity_trace_clip_avg_us`

## Implementation Sequencing

1. Add timer helper primitives.
   - Prefer a monotonic nanosecond helper exposed to sgame code, or a small local RAII/struct helper if the project already has one.
   - Store cumulative timings as 64-bit integers in source structs. If the final status must stay integer-only, print nanoseconds as decimal integers.

2. Add bot command CPU counters.
   - Extend `BotFrameCommandStatus` in `bot_brain.cpp`.
   - Time the full `BotBrain_BuildFrameCommand(...)` body after the entry counter starts.
   - Emit fields from `BotBrain_PrintFrameCommandStatus(...)`.
   - Update `tools/bot_perf/analyze_bot_perf.py` to derive CPU rates and averages.

3. Add route CPU counters.
   - Extend `BotNavRouteStatus` in `bot_nav.hpp`.
   - Time `BotNavBuildRouteWithFallback(...)` inside `BotNavRefreshRoute(...)`.
   - Optional: time route cache reuse branch in `BotNav_GetRouteSteer(...)`.
   - Emit fields through `BotBrain_PrintFrameCommandStatus(...)`.
   - Add analyzer budget examples for `route_query_cpu_ms_per_bot_sec` and `route_query_cpu_avg_us`.

4. Add Q3A route import counters.
   - Extend `Q3ABotLibImportSmokeStatus` and `BotLibAdapterStatus`.
   - Time route import entry points or the shared `Q3A_BotLibImport_BuildRouteSteerInternal(...)`.
   - Copy through `CopyImportStatus()`.
   - Emit fields in `q3a_bot_frame_command_status`.

5. Add visibility/PVS/PHS counters.
   - Extend `Q3ABotLibImportSmokeStatus` and `BotLibAdapterStatus`.
   - Count in `AAS_inPVS(...)`, `AAS_inPHS(...)`, `Q3A_BotLibImport_ClusterVisible(...)`, and `Q3A_BotLibImport_DecompressVisByte(...)`.
   - Emit fields in the final status line.
   - Update analyzer missing-instrumentation list and add derived visibility rates.

6. Add static BSP trace counters.
   - Count `AAS_Trace(...)` and `Q3A_BotLibImport_TraceQ2Bsp(...)`.
   - Count point/box/zero-length categories after mins/maxs normalization.
   - Count hull nodes in `Q3A_BotLibImport_RecursiveHullCheck(...)`.
   - Count brush tests in `Q3A_BotLibImport_ClipBoxToBrush(...)`.
   - Time `Q3A_BotLibImport_TraceQ2Bsp(...)`.

7. Add dynamic entity trace counters.
   - Surface existing import trace attempt/hit/miss/failure counters in `q3a_bot_frame_command_status`.
   - Add `gi.clip(...)` call/hit/miss/timing counters in `BotRuntimeEntityTrace(...)`.
   - Decide whether to keep clip counters in runtime-owned status, adapter status, or a small bot runtime perf struct consumed by `BotBrain_PrintFrameCommandStatus(...)`.

8. Expand budgets conservatively.
   - Keep current default budget thresholds unchanged until at least one real 10-minute soak baseline with source counters exists.
   - Add optional budget checks first with `required: false`, then promote to required after two or more stable local/CI baselines.

## Validation Plan

Initial source-counter validation:

- Build `sgame` and dedicated server.
- Refresh `.install` with packaged `mm-rage.aas`.
- Run the existing 10-minute mode `18` soak.
- Analyze with:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

Expected first-pass assertions:

- Existing checks still pass: `pass=1`, `route_failures=0`, `skipped_inactive=0`, and budget pass.
- `bot_frame_cpu_samples >= frames`.
- `bot_frame_cpu_ns > 0`.
- `route_query_cpu_samples == route_queries`.
- `route_query_cpu_ns > 0` when `route_queries > 0`.
- `q3a_route_cpu_samples > 0` and `q3a_route_cpu_ns > 0` when route queries run through Q3A import.
- `aas_inpvs_checks` and/or `aas_inphs_checks` are present even if zero on a given run.
- `bsp_trace_calls`, `entity_trace_attempts`, and `entity_trace_clip_calls` are present even if a specific map/behavior produces low counts.

Analyzer/budget validation:

- Parser tests now confirm new fields are parsed as raw status metrics while legacy logs keep optional source metrics absent.
- Derived analyzer metrics now cover CPU/visibility/trace rates, averages, max timings, ratios, and failure counters.
- Optional budget checks can now target new metric fields, for example:
  - `bot_frame_cpu_ms_per_bot_sec`
  - `route_query_cpu_ms_per_bot_sec`
  - `q3a_route_cpu_ms_per_bot_sec`
  - `aas_inpvs_checks_per_bot_sec`
  - `aas_inphs_checks_per_bot_sec`
  - `bsp_trace_calls_per_bot_sec`
  - `entity_trace_clip_calls_per_bot_sec`
- Generate a Markdown comparison report for pre-counter and post-counter logs. Missing fields should be reported as absent for older logs, not crash the analyzer.

## Proposed Analyzer Budget Evolution

Current default budget remains rate/proxy-based:

- command throughput
- route recomputation rate
- route refresh/reuse ratio
- debug work units
- recovery churn
- route failure/inactive guards

After source counters land, add a second example budget, for example `tools/bot_perf/default_soak_source_counter_budget.json`, with initially optional checks:

```json
{
  "checks": {
    "metrics": {
      "bot_frame_cpu_ms_per_bot_sec": { "max": 5.0, "required": false },
      "route_query_cpu_ms_per_bot_sec": { "max": 2.0, "required": false },
      "bsp_trace_calls_per_bot_sec": { "max": 250.0, "required": false },
      "entity_trace_clip_calls_per_bot_sec": { "max": 100.0, "required": false }
    },
    "status": {
      "visibility_decompress_failures": { "max": 0, "required": false },
      "entity_trace_failures": { "max": 0, "required": false }
    }
  }
}
```

The numeric values above are placeholders. Replace them with real thresholds after the first two source-counter soak baselines.

## Risks and Guardrails

- Timing overhead must stay small. Use monotonic nanosecond reads only around coarse functions first; avoid timing every inner brush or visibility byte operation unless a future profiling pass proves it necessary.
- Use 64-bit totals for nanoseconds and high-frequency counters. The ten-minute soak already produces hundreds of thousands of route/debug events.
- Keep final status field names lowercase `snake_case`.
- Keep old logs analyzable. The analyzer should treat missing new fields as absent/optional unless a budget explicitly requires them.
- Do not route Vulkan or renderer concerns through this telemetry path; these counters are server-game/BotLib/AAS performance counters only.
