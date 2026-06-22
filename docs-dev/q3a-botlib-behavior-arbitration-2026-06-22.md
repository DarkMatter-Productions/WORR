# Q3A BotLib Behavior Arbitration

Date: 2026-06-22

Task IDs: `FR-04-T02`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

Implemented the M1 live behavior arbitration slice from
`docs-dev/plans/bot-implementation-completion-roadmap.md`.

The bot brain now records a central behavior arbitration result for accepted
command frames. The current priority order is:

1. Recovery
2. Interaction
3. Objective
4. Combat
5. Item
6. Route
7. Idle

Candidates are recorded separately from the winning owner, so a frame can prove
that route, item, and combat systems all contributed without pretending all of
them owned the final command.

## Implementation

- Added `BotBehaviorArbitrationOwner` and `BotBehaviorArbitrationCandidates` in
  `bot_brain.cpp`.
- Added per-client last-owner memory so owner handoffs are counted when the
  winning behavior owner changes across frames for the same live bot.
- Extended `q3a_bot_behavior_policy_status` with:
  - `behavior_arbitration`
  - `behavior_policy_cvar_audit`
  - live/smoke/debug/deprecated cvar classification counts
  - route/item/combat/objective/interaction/recovery candidate counters
  - owner counters
  - handoff count
  - latest owner id, name, priority, previous owner, and reason
- Classified the current behavior policy cvar surface so
  `sg_bot_behavior_enable` activates the live behavior family while smoke-only
  gates remain visible and countable.
- Added server smoke mode `63` for `behavior_arbitration`, reusing the
  four-bot TDM behavior setup while stamping `behavior_arbitration=1` in the
  begin marker.
- Added the `behavior_arbitration` scenario row and parser optional field
  family to `tools/bot_scenarios/run_bot_scenarios.py`.
- Updated scenario harness tests for mode reservation, catalog stats, command
  construction, marker requirements, and raw-log parsing.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario behavior_arbitration --install-dir .install --timeout 120 --format text`

Focused runtime artifact:
`.tmp\bot_scenarios\20260622T112202Z`.

The focused run passed with:

- `behavior_arbitration_evaluations=246`
- `behavior_arbitration_route_candidates=246`
- `behavior_arbitration_item_candidates=246`
- `behavior_arbitration_combat_candidates=245`
- `behavior_arbitration_combat_owners=239`
- `behavior_arbitration_handoffs=3`
- `behavior_live_policy_cvars=8`
- `behavior_smoke_policy_cvars=0`
- `behavior_debug_policy_cvars=0`
- `behavior_deprecated_policy_cvars=0`

Catalog stats after this slice:

- `69` implemented rows total.
- `68` automated short-run rows.
- `1` manual high-bot degradation row.
- Highest frame-command smoke mode: `63`.
