# Q3A BotLib Status Harness Expansion

Date: 2026-06-18

Tasks: `DV-03-T05`, `DV-05-T02`, `FR-04-T16`, `DV-07-T06`

## Summary

Worker H expanded the `tools/bot_scenarios` status harness so parallel runtime
lanes can add richer BotLib telemetry without immediately turning those fields
into scenario promotion gates.

The harness now discovers optional status fields for:

- action dispatch counters for weapon/inventory command request build,
  validation, dispatch, submit, defer, and failure outcomes;
- aim-policy counters and last-policy metadata;
- special item utility candidate, seek-decision, boost, and last-kind buckets;
- route-target stabilization counters and last sampled target metadata.

## Implementation

- Added an optional field family catalog to
  `tools/bot_scenarios/run_bot_scenarios.py`.
- Normal scenario runs now parse optional marker streams, currently
  `q3a_bot_action_status` and `q3a_bot_blackboard_status`, even when a scenario
  does not require those markers for pass/fail checks.
- The selected `q3a_bot_frame_command_status` row is scanned for optional
  frame-status fields, which avoids cleanup status rows changing the reported
  route/command proof.
- Raw reserved-mode diagnostics also attach optional field discoveries so
  pending-gap reports can show newly landed counters before scenarios promote
  them to hard checks.
- Text, JSON, Markdown, and pending-gap reports now surface `optional_fields`
  only when matching fields appear.

## Gate Policy

No new hard runtime requirements were added in this slice. Existing scenario
`checks`, `marker_checks`, promotion checks, and degradation-policy checks remain
the only pass/fail criteria.

Optional counters are evidence only until a future task promotes specific fields
into scenario checks after implemented scenario logs prove they are stable.

## Tests

Added synthetic parser coverage for:

- discovery and report text for all four optional families;
- raw reserved-mode diagnostics carrying optional fields into a pending-gap
  report without changing readiness or failure counts.

Validation commands:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
```

Results:

- `python tools\bot_scenarios\test_run_bot_scenarios.py`: passed, 25 tests.
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`: passed.
