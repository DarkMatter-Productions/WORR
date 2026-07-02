# Q3A BotLib Campaign Post-Interaction Progression Base2

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round completed the next coop/campaign roadmap slice after the base2
progression-consumer proof. Bots were already selecting scored target-chain
interactions, but the interaction lifecycle did not reliably record what
happened after wait/use command ownership. In live mode `91`, coop route goals
can refresh before a retry window naturally expires, so active progression
interactions were cleared without a completion phase.

The new post-interaction path records command-backed progression interaction
completion, forces a route refresh, keeps a short post-interaction telemetry
window, and suppresses immediate reselection of the same entity. This makes the
campaign behavior less prone to re-fixating on the same trigger/door/mover after
using it.

## Implementation

- `src/game/sgame/bots/bot_nav.cpp` now tracks active interaction command
  frames, progression entity spawn counts, post-interaction state, and
  short-lived repeat-suppression state per client route slot.
- Progression completion is recorded when a scored interaction with at least one
  wait/use command frame ends by natural expiry or by a goal reset/clear.
- Completion invalidates the route slot, increments post-refresh telemetry, and
  records the completed entity and progression score.
- The route interaction chooser skips the recently completed entity while the
  suppression window is active and records repeat-suppression telemetry.
- `src/game/sgame/bots/bot_nav.hpp` and `bot_brain.cpp` expose aggregate
  progression selection counters, completion counters, post-refresh/post-frame
  counters, and last completed/post/suppressed entity metadata through
  `q3a_bot_nav_policy_status`.
- `tools/bot_scenarios/run_bot_scenarios.py` adds
  `coop_campaign_post_interaction_base2`, requiring target-linked progression
  selection, progression completion, post-refresh, post-window frames, and at
  least one suppressed immediate repeat.
- `tools/bot_release/run_bot_acceptance.py` now requires
  `coop_campaign_post_interaction_base2` as part of the release scenario
  evidence set.

## Validation

- `meson compile -C builddir-win sgame_x86_64 copy_sgame_dll`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios tools.bot_release.test_run_bot_acceptance`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario coop_campaign_post_interaction_base2 --binary .install\worr_ded_x86_64.exe --install-dir .install --format both --json-out .tmp\bot_scenarios\coop_campaign_post_interaction_base2.json --markdown-out .tmp\bot_scenarios\coop_campaign_post_interaction_base2.md --artifact-dir .tmp\bot_scenarios\coop_campaign_post_interaction_base2_artifacts --timeout 90`
- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_post_interaction.json`

Focused validation passed with `frames=121`, `commands=121`,
`route_commands=61`, `route_failures=0`,
`nav_interaction_progression_target_link_selections=3`,
`nav_interaction_progression_completions=3`,
`nav_interaction_progression_post_refreshes=3`,
`nav_interaction_progression_post_frames=241`, and
`nav_interaction_progression_repeat_suppressions=1`.

Release acceptance passed 15/15 checks with `required_scenarios=15` and
`supplemental_reports=2`. The scenario catalog reports 121 implemented rows and
zero pending rows.
