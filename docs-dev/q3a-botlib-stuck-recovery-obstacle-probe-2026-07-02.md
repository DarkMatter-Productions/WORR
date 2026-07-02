# Q3A BotLib Stuck Recovery Obstacle Probe

Date: 2026-07-02

Tasks: `FR-04-T02`, `FR-04-T05`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round scrutinized the remaining live report that bots could still stick or
spin near route nodes even after route-target progress, route movement
projection, and trace-gated command look-ahead were fixed. The remaining cause
was local recovery: when the watchdog detected no route progress, the command
layer emitted a fixed back/strafe pair relative to the current view. If the view
was still aimed into the blocked route target, that deterministic command could
repeatedly push into the same nearby wall or corner.

The fix keeps route acquisition unchanged and makes the recovery window
obstacle-aware. `bot_nav` now selects an escape direction with short player-hull
probes when stuck recovery activates, stores the chosen world-space direction
for that recovery window, and lets `bot_brain` project the stored direction back
into the current usercmd view each frame. Interaction wait/use recovery remains
unchanged.

## Implementation

- `src/game/sgame/bots/bot_nav.hpp`
  - Extended `BotNavRecoveryMove` with optional movement override fields.
  - Added stuck-recovery probe counters and last-move metadata to
    `BotNavRouteStatus`.
- `src/game/sgame/bots/bot_nav.cpp`
  - Added local recovery probe candidates using the existing player hull and
    `MASK_PLAYERSOLID`.
  - Scores actual trace clearance first, with small bonuses for moving away
    from the obstructed route target and for preserving the alternating
    recovery side.
  - Stores the selected world-space escape direction on the route slot and
    reuses it for the short recovery window.
  - Falls back to the legacy back/strafe direction only when all probes fail.
- `src/game/sgame/bots/bot_brain.cpp`
  - Passes the current desired view angles into recovery lookup.
  - Applies the nav-selected movement override when present.
  - Emits `q3a_bot_nav_policy_status` recovery-probe telemetry.
- `tools/bot_scenarios/run_bot_scenarios.py`
  - Added optional-field/source-hint coverage for recovery probe metrics.
  - Hardened `recover_from_stall` so it proves at least one obstacle-aware
    recovery probe selection.

No new upstream Q3A files were imported in this round; this is WORR-native
bot-nav, bot-brain, harness, documentation, build, and release-staging work.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
  - Passed before and after moving probe selection to recovery activation.
- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py`
  - Passed: 66 passed.
- `recover_from_stall` passed from
  `.tmp\bot_scenarios\stuck_recovery_probe_focus2.json`.
  - Probe evidence: `stuck_recovery_probe_checks=1`,
    `stuck_recovery_probe_uses=1`, `stuck_recovery_probe_fallbacks=0`,
    `last_stuck_recovery_probe_fraction=1000`.
- Focused navigation slice passed 4/4 from
  `.tmp\bot_scenarios\stuck_recovery_probe_nav_focus.json`.
  - `movement_elevator_route`: one selected probe, no fallback.
  - `combat_survival_regression_q2dm2`: seven selected probes, no fallback.
- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py tools\bot_release\test_run_bot_acceptance.py`
  - Passed: 83 passed.
- Full implemented bot scenario sweep passed 123/123 from
  `.tmp\bot_scenarios\implemented_stuck_recovery_probe.json`.
  - Across the sweep: 70 probe checks, 69 selected probe moves,
    zero blocked probes, and one no-clear fallback.
- Release acceptance passed 15/15 with 0 warnings from
  `.tmp\bot_release\bot_release_acceptance_stuck_recovery_probe.json`.
- `meson compile -C builddir-win`
  - Passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Passed and refreshed `.install/`.
