# Q3A BotLib AAS Memory Source Counters

Date: 2026-06-18

Tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T12`

## Summary

The Q3A BotLib adapter already tracked zone/hunk allocator active and peak
bytes through the verbose adapter status path. This slice exposes those values
on the split `q3a_bot_source_counter_status` marker so scenario captures can
carry memory-used data beside route, visibility, trace, and CPU counters.

## Implementation

- `bot_brain.*` now reads `BotLibAdapter_GetStatus()` while printing frame
  command status.
- The source-counter marker now includes:
  - `q3a_memory_zone_active`
  - `q3a_memory_zone_peak`
  - `q3a_memory_hunk_active`
  - `q3a_memory_hunk_peak`
  - `q3a_memory_total_active`
  - `q3a_memory_total_peak`
  - `q3a_memory_failures`
  - `q3a_memory_available`
- `tools/bot_perf/analyze_bot_perf.py` now classifies these fields as the
  `q3a_memory` source-counter group and exposes the byte totals in JSON output.

## Validation

This is a status-surface change over existing adapter counters. The final
integration pass rebuilt `sgame_x86_64` and `worr_ded_engine_x86_64`, refreshed
`.install`, reran the implemented bot scenario suite at 9/9 passing rows, and
ran the bot perf analyzer tests.

The latest `engage_enemy` perf parse reports the `q3a_memory` source-counter
group alongside bot-frame, route, visibility, static-BSP, and entity-trace
groups, with no missing source-counter groups.

## Remaining Work

Memory budget thresholds should wait until there are stable baselines across the
reference map set.
