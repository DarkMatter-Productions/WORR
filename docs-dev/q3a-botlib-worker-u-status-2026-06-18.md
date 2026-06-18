# Q3A BotLib Worker U Status Reconciliation

Date: 2026-06-18

Worker lane: Worker U, docs/status only

Tasks: `FR-04-T13`, `FR-04-T16`, `DV-03-T05`, `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `DV-07-T06`, `DV-08-T05`

## Scope

This pass reconciles status after the latest worker wave. It edits only the
canonical Q3A BotLib/AAS plan, the SWOT roadmap, and this status note.

No `src/**`, `tools/**`, or `assets/**` files were changed in this lane.

## Evidence Reviewed

- `docs-dev/q3a-botlib-botfile-script-parity-2026-06-18.md` records the
  Worker R script-style pass. The current script companions remain original
  WORR assets, pass the profile validator, and now follow the available
  idTech3-style script/header shape more closely.
- `docs-dev/q3a-botlib-botfiles-scripts-support-2026-06-18.md` and
  `docs-dev/q3a-botlib-botfiles-scripts-package-coverage-2026-06-18.md`
  establish the validator and packaging evidence for `botfiles/scripts/*_s.c`.
- `docs-dev/q3a-botlib-source-counter-plumbing-2026-06-18.md` plus the latest
  implemented scenario outputs show split `q3a_bot_source_counter_status`
  emission for route-build, PVS/PHS, visibility, entity-trace, and static BSP
  trace counters.
- `docs-dev/q3a-botlib-route-cpu-timing-2026-06-18.md` records route query,
  route reuse, and Q3A route import timing fields in source-owned structs. The
  note also says final status-print integration and local validation remain
  pending, so this pass does not claim completed CPU timing validation.
- `.tmp/bot_scenarios/final_validation2_report.json` reports the five
  implemented scenario rows passing: spawn route to item, recover from stall,
  multi-bot reservation, map-change repeat, and profile-backed spawn.
- `.tmp/bot_scenarios/final_pending_gap.md` reports
  `total=4, ready=0, blocked=4, overall=blocked` for reserved modes `20`
  through `23`.

## Status Result

The plan and roadmap now reflect:

- Botfile script parity is complete as documentation/asset validation evidence.
- Split source-counter status emission is present for non-timing counters.
- Route-side CPU timing exists in source-owned structures, but final status
  emission, analyzer-visible validation, bot-frame CPU timing, and CPU budgets
  remain pending.
- Modes `20` through `23` remain pending. Mode `22` has a passing route-focus
  proof, but not the health/armor-specific pickup counters required for
  promotion.

Completion stats after this reconciliation:

- Total checklist completion: 519 of 696 items complete, or 74.6%.
- Phase checklist completion: 519 of 684 phase items complete, or 75.9%.
