# Q3A BotLib CTF Role Combat Bridge

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off live CTF role-combat bridge behind
`sg_bot_ctf_role_combat`. The existing objective match-policy helpers already
classify Capture the Flag roles, lanes, and engage intent; the new command-side
bridge consumes that policy as a live attack-decision owner when it has
visible, shootable enemy facts.

No upstream Q3A, Gladiator, BSPC, idTech3, or `q2proto/` source files were
imported or modified. The implementation is WORR-native and uses the existing
`bot_objectives.*`, `bot_combat.*`, `bot_brain.*`, and scenario harness
surfaces.

## Implementation

- Added `sg_bot_ctf_role_combat`, defaulting to `0`.
- Added `Bot_CommandApplyCtfRoleCombat()` between action-decision sampling and
  team friendly-fire suppression. This lets CTF role policy own attack input
  while preserving the existing later friendly-fire veto point.
- Restricted the bridge to valid `BotObjectiveMatchMode::CaptureTheFlag`
  policies that participate in scoring, have a non-empty role/lane, and request
  engage behavior.
- Reused the existing combat enemy-fact builders. A target must be valid,
  visible, shootable, alive, and spawn-count matched before the role bridge can
  own an attack decision.
- Adopted the selected target into `bot->enemy` and the per-bot blackboard so
  the final command angles face the same enemy that the role-combat bridge
  selected.
- Changed frame-command aiming to use the final command decision after CTF
  role-combat ownership and team-fire suppression, rather than the original
  sampled action decision.
- Added deterministic CTF smoke setup for mode `36`: four bots are assigned to
  red/blue teams, combat weapons are granted, and each bot gets a one-time
  visible enemy placement for proof stability.
- Added frame-command counters:
  - `ctf_role_combat_requests`
  - `ctf_role_combat_policy_selections`
  - `ctf_role_combat_target_selections`
  - `ctf_role_combat_attack_decisions`
  - `ctf_role_combat_decision_overrides`
  - `ctf_role_combat_target_deferrals`
  - `ctf_role_combat_invalid_skips`
  - `last_ctf_role_combat_*`
- Added the CTF role-combat counters to the compact
  `q3a_bot_frame_command_status` row. The same compact row also keeps
  team-fire counters available after the verbose route/status row was split to
  avoid compiler nesting pressure and keep proof counters ahead of the older
  oversized diagnostic row.

## Scenario Promotion

`ctf_role_combat` is now an implemented scenario using smoke mode `36`. The
promoted checks validate:

- Four active bots in a CTF team-mode match.
- Objective match-policy selection for Capture the Flag.
- Default-off role-combat request, policy-selection, target-selection, and
  attack-decision counters.
- Last selected role/lane metadata and visible/shootable client target facts.
- Applied `BUTTON_ATTACK` evidence through `q3a_bot_action_status`.

The scenario proves policy consumption at the command attack boundary. It does
not yet claim full autonomous flag-carrier support, dropped-flag response,
base-return priorities, or cross-role tactical coordination in arbitrary live
CTF matches.

## Validation

Validation completed:

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
  passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed with
  32 tests.
- `meson compile -C builddir-win` passed after splitting the oversized
  frame-command status emission.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
  passed and repackaged/audited the staged `mm-rage.aas` payload.
- Focused runtime proof passed:
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario ctf_role_combat --timeout 90 --base-port 28200 --format text --json-out .tmp\bot_scenarios\ctf_role_combat_report.json`.
  The run reported 246 frames, 246 commands, 0 route failures, 245 applied
  attack buttons, 246 role-combat requests, 245 role-combat attack decisions,
  and visible/shootable target metadata.
- Focused regression proof passed:
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario team_fire_avoidance --timeout 90 --base-port 28220 --format text --json-out .tmp\bot_scenarios\team_fire_avoidance_report.json`.
- Full implemented suite passed:
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md`
  reported 28 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.
