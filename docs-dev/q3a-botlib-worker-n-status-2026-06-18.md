# Q3A BotLib Worker N Status Refresh

Date: 2026-06-18

Worker lane: Worker N, docs/status only

Tasks: `FR-04-T13`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-05-T02`, `DV-05-T05`, `DV-07-T06`, `DV-08-T05`

## Scope

This pass refreshed project status after the current multi-worker wave. It
edited only planning/status documentation and did not modify `src/**`,
`tools/**`, or `assets/**`.

The refresh keeps modes `20` through `23` in the pending scenario bucket. This
thread did not generate runtime smoke evidence that proves enemy engagement,
weapon switching, health/armor pickup, or team-objective completion end to end.

## Current Wave Summary

- Botfile profile work now includes Q3/Gladiator-style companion families,
  compact `botfiles/scripts/*_s.c` companions, denser profile validation,
  script-package coverage, loose `botfiles` staging for no-zlib dedicated
  builds, and user-facing profile docs.
- Scenario tooling now preserves source-aware raw marker diagnostics for
  reserved modes `20` through `23`, including the latest source line for metrics
  and source hints for missing promotion counters.
- Combat, item, and CTF proof hooks moved closer to source truth:
  bot-attributed damage is recorded from the real damage path, health/armor
  observations are recorded from successful item touches, and CTF objective
  pickup/return/capture observations are recorded from authoritative gameplay
  branches. These are proof hooks, not scenario promotions.
- The health/armor promotion gate was explicitly evaluated and remains blocked:
  the raw mode `22` route proof can pass, but required health/armor-specific
  source counters are still absent from the promotion result.
- Performance tooling can consume proposed source-counter fields, and the
  adapter exposes several non-timing source counters. CPU timing and final
  frame-command source-counter emission remain pending.

## Status Notes

The plan and roadmap now distinguish helper readiness from runtime scenario
completion. The pending scenario checklist items for engage enemy, switch
weapons, pick up health/armor, and follow team objective remain unchecked until
a future thread captures runtime-backed pass evidence for each mode.
