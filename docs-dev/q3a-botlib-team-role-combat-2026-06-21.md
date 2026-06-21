# Q3A BotLib Team Role Combat Proof

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off TDM combat-owner proof for WORR-native bot role policy. The new `sg_bot_team_role_combat` bridge lets a bot with selected Team Deathmatch match-role and lane metadata own a live attack decision when visible, shootable enemy facts are available.

The feature is intentionally still a smoke-level proof. It proves that the existing objective-side TDM role/lane policy can drive the action layer without making broader autonomous TDM strategy claims.

## Runtime Behavior

- `src/game/sgame/bots/bot_brain.cpp` registers the disabled-by-default `sg_bot_team_role_combat` cvar.
- The bridge runs after the base action decision and before CTF role combat and friendly-fire suppression.
- Eligible bots must have a valid TDM match policy, scoring participation, role/lane metadata, and an engage intent.
- The bridge reuses the same live enemy fact checks as the CTF role-combat proof: visible target, shootable target, recorded target client/entity, and attack-button application.
- Frame-command status now exposes `team_role_combat_*` and `last_team_role_combat_*` counters for requests, policy selections, target selections, attack decisions, invalid skips, last role/lane, last priority, last target, and visibility/shootability facts.
- Compact action proof rows now print before oversized verbose diagnostics so scenario gates can reliably parse split action/status proof markers.

## Scenario Coverage

- Server smoke mode `43` runs a four-bot TDM setup with `deathmatch 1`, `g_gametype 3`, and `sg_bot_team_role_combat 1`.
- The promoted `team_role_combat` scenario validates:
  - TDM readiness and scoring-participant match-policy evidence.
  - Team-role combat requests, policy selections, target selections, and attack decisions.
  - Valid role/lane metadata and zero invalid skips.
  - Visible and shootable target facts.
  - Live attack-button application through `q3a_bot_action_status`.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario team_role_combat --timeout 120 --base-port 29920 --format text --json-out .tmp\bot_scenarios\team_role_combat.json`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 30080 --format text --json-out .tmp\bot_scenarios\latest_report.json`

The latest implemented scenario run reports 35 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.

## Provenance

No new upstream Q3A or BSPC source files were imported. The work is WORR-native behavior, status, server-smoke, scenario-catalog, and documentation code built on top of the existing BotLib/AAS bridge.
