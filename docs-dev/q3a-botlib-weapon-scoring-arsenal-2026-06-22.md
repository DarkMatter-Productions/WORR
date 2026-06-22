# Q3A BotLib Weapon Scoring Arsenal Round

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Summary

This round promotes the next M2 combat slice by making carried-weapon scoring
auditable and adding a deterministic two-bot FFA proof for arsenal selection.
The new mode `65` scenario starts a bot on a close-range rocket launcher,
grants a ready super shotgun, carries railgun and BFG options without enough
ammo, and places a low-health enemy inside rocket splash danger but outside the
melee range bucket.

The result is a compact proof that the action layer scans the carried arsenal,
rejects insufficient-ammo weapons, records splash-risk pressure, selects a
range-appropriate weapon, applies the enemy-estimate finisher bonus, and proves
the weapon switch request/completion path.

## Implementation Details

- `src/game/sgame/bots/bot_combat.*`
  - `BotWeaponSelectionResult` now preserves current/preferred usability,
    safety, and score reasons in addition to the selected result.
  - This keeps the existing scoring behavior intact while making action-layer
    audits and future scenario diagnostics less lossy.
- `src/game/sgame/bots/bot_actions.*`
  - The carried-weapon inventory scanner now records ammo skips, splash-unsafe
    candidates, range-backed selections, estimate-backed selections, selected
    ammo, score margin, priority/ammo metadata, selected range band, attack
    model, splash/self-damage flags, estimate adjustment, and estimate reason.
  - The scanner keeps using the existing `BotCombat_SelectPreferredWeapon`
    threshold and metadata scoring model.
- `src/game/sgame/bots/bot_brain.cpp`
  - Added smoke mode `65` setup via `weapon_scoring`.
  - The setup grants rocket launcher, super shotgun, railgun, and BFG state;
    forces rail/BFG ammo insufficiency; places a low-health peer at close range;
    and routes through the existing exact weapon-switch dispatch proof.
  - The smoke proof action status now emits the weapon-inventory audit fields
    required by the scenario harness.
- `src/server/main.c`
  - Added reserved smoke mode `65`, begin-marker `weapon_scoring=1`, two-bot
    target count, FFA gametype setup, `combat=weapon_scoring`, and weapon-switch
    cvar activation for completion proof.
- `tools/bot_scenarios/run_bot_scenarios.py`
  - Added `weapon_scoring_arsenal` as mode `65`.
  - The row hard-gates arsenal scanning, ready candidates, carried selection,
    switch recommendation, ammo skips, splash safety pressure, range selection,
    estimate selection, selected super shotgun metadata, and switch completion.
  - The action dispatch optional-field family now describes the new
    weapon-inventory telemetry.
- `tools/bot_scenarios/test_run_bot_scenarios.py`
  - Added mode `65` reservation, catalog-summary coverage, required-marker
    assertions, and synthetic marker evaluation.

## Validation

Commands run:

```text
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64
python tools\refresh_install.py --build-dir builddir-win
meson compile -C builddir-win sgame_x86_64
python tools\refresh_install.py --build-dir builddir-win
python tools\bot_scenarios\run_bot_scenarios.py --scenario weapon_scoring_arsenal --install-dir .install --binary .install\worr_ded_x86_64.exe --format both
```

Latest focused artifact:

```text
.tmp\bot_scenarios\20260622T123648Z
```

The focused scenario passed with:

- `action_weapon_inventory_scans=121`
- `action_weapon_inventory_candidates=960`
- `action_weapon_inventory_ready_candidates=720`
- `action_weapon_inventory_selections=2`
- `action_weapon_inventory_switch_recommendations=2`
- `action_weapon_inventory_ammo_skips=240`
- `action_weapon_inventory_splash_unsafe_skips=448`
- `action_weapon_inventory_range_selections=2`
- `action_weapon_inventory_estimate_selections=1`
- `last_action_weapon_inventory_selected_item=11`
- `last_action_weapon_inventory_selected_range_band_name=close`
- `last_action_weapon_inventory_selected_attack_model_name=hitscan`
- `last_action_weapon_inventory_selected_estimate_adjustment=7`
- `last_action_weapon_inventory_estimate_reason=enemy_estimate_finisher`
- `weapon_switch_requests=2`
- `weapon_switch_completions=2`
- `weapon_switch_failures=0`

## Completion Stats

- Scenario catalog: `71` implemented rows total.
- Automated short-run rows: `70`.
- Manual high-bot degradation-policy rows: `1`.
- Default pending rows: `0`.
- Highest reserved bot frame-command smoke mode: `65`.
- M2 status: enemy target memory/decay is complete; weapon scoring arsenal
  proof is now complete; aim/fire, ammo pickup pressure, live inventory use,
  survival, and broader combat regression coverage remain.

## Follow-Up

The next M2 slice should promote aim/fire policy depth: tune aim settle,
reaction timing, projectile lead, and line-of-fire gating against the new
weapon scoring evidence, then add a non-perfect live firing proof that does not
depend on staged target damage.
