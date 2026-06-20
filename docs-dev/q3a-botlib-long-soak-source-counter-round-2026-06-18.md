# Q3A BotLib Long-Soak Source-Counter Report Round

Date: 2026-06-18

Task IDs: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T16`

## Summary

This tooling-only slice makes `tools/bot_perf/analyze_bot_perf.py` clearer for validating current and future source-counter long-soak reports against numeric budgets.

No game source, scenario definitions, release tooling, roadmap, plan, or credits files were changed. The work stays inside `tools/bot_perf/**` plus this implementation note.

## Analyzer Changes

Per-run output now includes explicit source-counter readiness fields:

- `source_counter_status`
- `source_counter_pass`
- `source_counter_pass_int`
- `source_counter_groups_expected`
- `source_counter_groups_present_count`
- `source_counter_groups_missing_count`
- `source_counter_group_status`
- `missing_current_counters`

These fields separate smoke pass/fail from source-counter completeness. That matters for the current ten-minute fixture, which still passes the long-soak budget but predates split source-counter emission.

Budget output now includes flat pass/fail fields on each report row when `--budget` is used:

- `budget_status`
- `budget_pass`
- `budget_pass_int`
- `budget_checks`
- `budget_failures`
- `budget_warnings`
- `budget_required_failed`
- `budget_required_passed`
- `budget_optional_missing`
- `budget_missing_current_counters`

The nested `budget.missing_current_counters` list maps missing budget metrics back to the raw source-counter inputs that would satisfy them. For example, a missing `route_query_cpu_ms_per_bot_sec` check now points at `route_query_cpu_ns` or `bot_route_cpu_ms` instead of only saying the derived metric is absent.

Text, CSV, JSON, and Markdown outputs all expose the new fields. Future strict source-counter soak budgets can gate `source_counter_pass_int` with `min=1` and `max=1` once legacy pre-counter fixtures are no longer accepted for that validation lane.

## Current Fixture Read

The legacy ten-minute fixture still passes `tools/bot_perf/default_soak_budget.json`:

- `budget_status=pass`
- `budget_pass_int=1`
- `budget_required_failed=0`
- `budget_optional_missing=4`
- `budget_missing_current_counters=4`

Its source-counter readiness is intentionally separate:

- `source_counter_status=fail`
- `source_counter_pass_int=0`
- `source_counter_groups_present_count=0`
- `source_counter_groups_missing_count=7`

The short source-counter smoke log at `.install/basew/logs/logs/source_counter_soak.log` parses with partial readiness:

- `source_counter_status=fail`
- `source_counter_groups_present_count=3`
- `source_counter_groups_missing_count=4`
- Present groups: `visibility`, `static_bsp_trace`, `entity_trace`
- Missing groups: `bot_frame_cpu`, `route_query_cpu`, `q3a_route_cpu`, `q3a_memory`

That short log is not a default-budget pass candidate because it is about one second long and has no ten-minute progress cadence. It is useful for confirming source-counter group diagnostics.

## Validation

Commands run:

```powershell
python -m py_compile .\tools\bot_perf\analyze_bot_perf.py .\tools\bot_perf\test_analyze_bot_perf.py
python .\tools\bot_perf\test_analyze_bot_perf.py
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --format json --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py .install\basew\logs\logs\source_counter_soak.log
python .\tools\bot_perf\analyze_bot_perf.py --format csv --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json --markdown-out .tmp\bot_perf\soak_compare_source_counter_fields.md .tmp\q3a_bot_nav_soak_10min_final.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

Results:

- Python compile check passed.
- Bot perf unit tests passed: `Ran 13 tests`, `OK`.
- The legacy ten-minute fixture passed the default budget while reporting the expected four optional CPU source-counter warnings and four budget missing-current-counter diagnostics.
- JSON output includes both nested budget diagnostics and flat `budget_*` fields.
- CSV output includes `source_counter_*`, `missing_current_counters`, and flat `budget_*` columns.
- Markdown generation wrote `.tmp/bot_perf/soak_compare_source_counter_fields.md` with source-counter and budget missing-counter columns.
- The short source-counter smoke log parsed successfully and reported three present source-counter groups plus four missing groups.

## Residual Risk

The current checked ten-minute fixture is still pre-source-counter. A fresh ten-minute source-counter run is needed before CPU, memory, visibility, static-BSP trace, and entity-trace budgets can be promoted from diagnostics to strict long-soak gates.
