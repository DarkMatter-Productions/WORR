# Q3A BotLib TDM Role Spawn Stability Round

Date: 2026-06-22

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a focused TDM stability proof for the multiplayer mode
intelligence roadmap. The new `tdm_role_spawn_stability` scenario reserves bot
frame-command smoke mode `73`, stages a four-bot TDM role route/combat run,
forces a same-map restart through the existing map-repeat lifecycle, and proves
that the TDM route and combat owners remain active after the reload before the
server performs final bot cleanup.

The scenario is intentionally a live integration gate rather than a new policy
feature. It reuses the existing TDM role-route and role-combat bridges, the
existing map-repeat restart path, and the existing cleanup markers. The added
contract is that all of those surfaces stay coherent together across a spawn
and map lifecycle transition.

## Implementation

- `src/server/main.c` now treats mode `73` as both a map-repeat run and a TDM
  role route/combat run. The scenario begin marker includes
  `tdm_role_spawn_stability=1` so logs can distinguish this integration proof
  from the older individual route/combat rows.
- `tools/bot_scenarios/run_bot_scenarios.py` adds the implemented
  `tdm_role_spawn_stability` row, reserves mode `73`, sets two map-repeat
  cycles with forced restart, and hard-gates begin, queued-restart,
  observed-reload, post-reload, cleanup, role-route, role-combat, TDM
  readiness, objective-policy, and attack-button evidence.
- `tools/bot_scenarios/test_run_bot_scenarios.py` adds reserved-mode synthetic
  parser coverage, catalog/marker contract coverage, restart tag selection
  coverage, and command construction checks for the forced restart cvars.
- `tools/bot_scenarios/README.md` documents the new scenario row.

No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
imported or modified for this round.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  - Passed 48 tests.
- `meson compile -C builddir-win worr_ded_x86_64`
  - Passed.
- `meson compile -C builddir-win worr_ded_engine_x86_64`
  - Passed and rebuilt `src_server_main.c.obj`.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Passed and refreshed the local `.install/` staging payload.
- Focused scenario:
  - Command: `python tools\bot_scenarios\run_bot_scenarios.py --scenario tdm_role_spawn_stability --timeout 120 --base-port 28620 --format text --json-out .tmp\bot_scenarios\tdm_role_spawn_stability_report.json --markdown-out .tmp\bot_scenarios\tdm_role_spawn_stability_report.md`
  - Artifact: `.tmp\bot_scenarios\20260622T212431Z`
  - Result: `1` passed, `0` failed, `0` timeouts, `0` errors, `0` pending.
  - Key evidence: `expected_min_commands=4`, `frames=246`, `commands=245`,
    `route_commands=245`, `route_failures=0`, `cycles=2`, `map_changes=1`,
    `final_count=0`, `team_role_route_activations=245`,
    `team_role_route_route_requests=245`,
    `team_role_combat_target_selections=245`,
    `team_role_combat_attack_decisions=245`, and `action_attack_buttons=245`.
- Full implemented suite:
  - Command: `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28680 --format text --json-out .tmp\bot_scenarios\implemented_after_tdm_role_spawn_stability_report.json --markdown-out .tmp\bot_scenarios\implemented_after_tdm_role_spawn_stability_report.md`
  - Artifact: `.tmp\bot_scenarios\20260622T212440Z`
  - Result: `81` passed, `0` failed, `0` timeouts, `0` errors, `0` pending.

## Follow-Up

The next practical M3 slices remain FFA/Duel live pacing, less-staged CTF
pickup/drop transitions, and then a fresh source-counter soak once the live
mode rows have enough breadth to justify a new performance baseline.
