# Q3A BotLib Behavior Policy Umbrella

Date: 2026-06-22

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round added a WORR-owned `sg_bot_behavior_enable` integration switch for
the current default-off bot behavior proof lanes. The switch lets test servers
enable the active behavior family without setting each individual proof cvar,
and it adds a dedicated frame-command smoke mode (`52`) to prove the umbrella
activates real TDM behavior ownership without masking the individual cvars in
the smoke begin marker.

The new cvar remains default-off. Existing narrow proof cvars such as
`sg_bot_team_role_route`, `sg_bot_team_role_combat`,
`sg_bot_team_fire_avoidance`, and `sg_bot_match_item_policy` still work
independently.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` now registers
  `sg_bot_behavior_enable` and treats it as an umbrella gate for the existing
  command-side behavior helpers: coop progress/lead/resource/target/anti-block
  and door/elevator policies, FFA roam/spawn-camp/role-combat policies, TDM
  role-route/role-combat/friendly-fire policies, CTF route/combat/objective
  policies, and the brain-side match item-policy status helper.
- `src/game/sgame/bots/bot_nav.cpp` now lets `sg_bot_behavior_enable` activate
  the nav-side match item-policy family, including team item-role and
  deny-enemy resource scoring through the existing `sg_bot_match_item_policy`
  path.
- `src/game/sgame/bots/bot_brain.cpp` emits
  `q3a_bot_behavior_policy_status` so scenario tooling can verify the umbrella
  cvar and the composed policy gates without inferring behavior from unrelated
  counters.
- `src/server/main.c` reserves `sv_bot_frame_command_smoke 52` for the behavior
  umbrella scenario, stages a four-bot TDM match, sets only
  `sg_bot_behavior_enable 1`, reports `behavior_enable` in the begin marker,
  and resets the cvar with the other scenario controls.
- `tools/bot_scenarios/run_bot_scenarios.py` adds the
  `behavior_policy_umbrella` implemented scenario row and reserves mode `52`.
  The scenario proves route ownership, team role-combat, friendly-fire
  suppression, TDM readiness, and match item-policy activation. Live item-goal
  selection remains proven by the dedicated `match_item_policy` mode `51`
  because role-route ownership can legitimately preempt pickup-goal routing in
  the combined mode.
- `tools/bot_scenarios/test_run_bot_scenarios.py` covers the new catalog row,
  command construction, reserved-mode mapping, behavior status marker parsing,
  and synthetic marker validation.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  - Passed: 45 tests.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
  - Passed, linking `sgame_x86_64.dll` and `worr_ded_engine_x86_64.dll`.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json`
  - Passed; all eight staged AAS maps were represented in `.install/basew/pak0.pkz`.
- `python tools\bot_scenarios\run_bot_scenarios.py --binary .install\worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --scenario behavior_policy_umbrella --artifact-dir .tmp\bot_scenarios\behavior_policy_umbrella --format both --json-out .tmp\bot_scenarios\behavior_policy_umbrella\report.json --markdown-out .tmp\bot_scenarios\behavior_policy_umbrella\report.md`
  - Passed from `.tmp\bot_scenarios\behavior_policy_umbrella\20260622T050833Z`.
- `python tools\bot_scenarios\run_bot_scenarios.py --binary .install\worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --scenario match_item_policy --artifact-dir .tmp\bot_scenarios\match_item_policy_check --format text --json-out .tmp\bot_scenarios\match_item_policy_check\report.json`
  - Passed from `.tmp\bot_scenarios\match_item_policy_check\20260622T050722Z`,
    revalidating the live item-role/resource-denial pickup-goal proof that mode
    `52` intentionally does not duplicate.

## Provenance

No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source files
were imported or modified. This slice is WORR-native cvar plumbing, behavior
gate composition, scenario tooling, and documentation over the existing bot
brain/nav/server smoke infrastructure.
