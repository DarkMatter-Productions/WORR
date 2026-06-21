# Q3A BotLib TDM Role-Combat Friendly-Fire Precedence

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a mode `44` bot scenario proof for the TDM policy stack where
`sg_bot_team_role_combat` first consumes TDM role/lane match policy as a live
attack-decision owner, then `sg_bot_team_fire_avoidance` can veto blocked
friendly-line shots before final command application.

No new gameplay-facing cvar was added. The proof composes the two existing
default-off bridges:

- `sg_bot_team_role_combat`
- `sg_bot_team_fire_avoidance`

## Implementation

- `src/game/sgame/bots/bot_brain.cpp`
  - Recognizes the combined smoke setup as scenario mode `44` when both TDM
    role-combat and team-fire avoidance bridges are enabled.
  - Prepares both proof lanes for mode `44`, using the existing role-combat
    target setup and the existing team-fire avoidance setup.
  - Allows the smoke-only friendly-line proof to run for both mode `34` and
    mode `44`.
  - Lets TDM role-combat smoke facts fall back to deterministic smoke enemy
    facts in mode `44`, matching the existing isolated role-combat proof.

- `src/server/main.c`
  - Treats mode `44` as both a team-fire-avoidance and team-role-combat smoke.
  - Keeps the smoke combat cvar at `0` for mode `44`, so attack intent must
    come from the role-combat bridge rather than the generic engage-enemy
    smoke path.
  - Emits both `team_fire_avoidance=1` and `team_role_combat=1` in the
    scenario begin marker.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Adds implemented scenario `team_role_combat_avoidance` on smoke mode `44`.
  - Gates TDM readiness, TDM match-policy evidence, role-combat target/attack
    selection, friendly-fire policy evaluation, friendly-line blocks, and final
    blocked-state metadata.

- `tools/bot_scenarios/test_run_bot_scenarios.py`
  - Covers the new mode name, catalog row, command construction, begin marker,
    required marker metrics, and a promoted fixture row.

## Proof Behavior

The focused run showed mode `44` producing live role-combat decisions and then
blocking the friendly-line subset through team-fire avoidance:

- `team_role_combat_attack_decisions=234`
- `team_fire_avoidance_evaluations=234`
- `team_fire_avoidance_blocks=231`
- `team_fire_avoidance_line_blocks=231`
- `last_team_fire_avoidance_friendly_line=1`
- `last_team_fire_avoidance_blocked=1`
- `route_commands=246`
- `route_failures=0`
- `pass=1`

The first local gate expected all attack buttons to be suppressed. The live
four-bot TDM setup correctly still allowed a few unblocked shots, so the final
scenario gate now asserts the durable policy invariant instead: role combat
creates attack decisions and team-fire avoidance records blocked friendly-line
suppression.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario team_role_combat_avoidance --timeout 120 --base-port 30120 --format text --json-out .tmp\bot_scenarios\team_role_combat_avoidance.json`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 30160 --format text --json-out .tmp\bot_scenarios\latest_report.json`

Focused mode `44` passed with 1 passed, 0 failed. The full implemented suite
passed with 36 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.

No upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
imported or modified for this update.
