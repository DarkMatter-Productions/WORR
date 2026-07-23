# FR-10-T12 Splash Occlusion: Player, BSP, and Water Acceptance

Date: 2026-07-20  
Project task: `FR-10-T12`  
Dependencies: `FR-10-T10`, `FR-10-T11`

## Result

The previously open player/BSP/water splash-occlusion class now has three
explicit current-world Rocket policies and three fresh-process headless
repetitions per policy. The ordinary Rocket touch, production
`RadiusDamage`, production `CanDamage`, and normal `Damage` path remain the
only authorities for impact, visibility, falloff, and health changes.

All nine executions passed under canonical runner schema
`worr.networking.canonical-weapon-damage-runtime.v35`:

| Mode | Splash policy | Production `CanDamage` | Exact damage | Required geometry proof |
|---|---:|---:|---:|---|
| `rocket-splash` | `1` clear player | `1` | `58` | Clear current-world player side |
| `rocket-splash-bsp-occlusion` | `2` BSP blocked | `0` | `0` | Exact real linked `func_rotating`, `SOLID_BSP`, between impact and player |
| `rocket-splash-water-boundary` | `3` water boundary | `1` | `58` | Exact real linked `func_water`; impact starts in its water contents and the player endpoint is beyond its brush |

This closes one T12 interaction class only. The partial parent now contains
37 modes across 19 weapon policies, would require 111 child executions when
run in full, publishes six remaining coverage classes, and continues to set
`task_complete=false`. `FR-10-T12` remains incomplete.

## Production authority and fixture boundary

`RadiusDamage` now passively reports the boolean returned by its existing
`CanDamage(ent, inflictor)` call to
`LagCompensation_ObserveCurrentWorldSplashCanDamage`. The observer is inert
unless one of the isolated Rocket splash modes is armed and both the inflictor
and player identities match exactly. The observed value is never returned to
gameplay and never changes candidate discovery, trace masks, damage falloff,
knockback, or health.

The common fixture first requires an authenticated real client attack, the
normal bounded current-world Rocket spawn advance, and contact by the exact
spawned Rocket with the exact existing impact blocker. Only after that
production contact does it stage the selected boundary:

- clear-player policy stages no additional occluder;
- BSP-blocked policy moves the fixture map's real `func_rotating` to the
  midpoint between the linked Rocket impact centre and linked player centre;
  it requires the exact generation-qualified mover to remain linked and
  `SOLID_BSP`; and
- water-boundary policy moves the fixture map's real `func_water` around the
  impact. An exact `MASK_WATER` trace from impact to player must begin solid in
  that precise brush identity but must not remain all-solid, proving that the
  query crosses the brush boundary. Production `CanDamage` retains its normal
  `MASK_SOLID` behavior across the non-solid water contents.

Both real brush entities retain their original origin and link state and are
restored on pass, failure, reset, and shutdown. The fixture does not create a
projectile, attack, contact result, visibility result, or damage result.

The v35 status suffix is deliberately fail-closed and participates in the
repeat semantic signature:

```text
splash_occlusion_required
splash_occlusion_policy
splash_radius_evaluated
splash_can_damage_observed
splash_can_damage_result
splash_bsp_blocker_verified
splash_water_boundary_verified
splash_target_undamaged
```

The complete canonical status has 101 fields and its stable semantic
signature has 79 fields. The T12 parent binds each literal manifest row to the
child's expected splash policy so a mode alias or policy drift cannot broaden
the acceptance claim.

## Fresh headless evidence

All runs used the isolated runtime root
`.tmp/networking/fr10_t12_splash_stage_v35_20260720T040155319`, dedicated
server mode, hidden/input-disabled clients, separate runtime homes, and the
fixture map `worr_fr10_rewind_mover`.

| Mode | Run ID | Result tuple for all three rows | Artifact SHA-256 |
|---|---|---|---|
| Clear player | `20260720T031552.349131Z-6056` | `pass/58/1/1/1/1/1/0/0/0` | `3c8e685dbeccef54eecb5dd38032375dbf14b5638524d63cbd0517235028c202` |
| BSP blocked | `20260720T031622.003021Z-44580` | `pass/0/1/2/1/1/0/1/0/1` | `99368c5a49db1264fa57bb3331983521a1758801151a1ad2eb4421e5123c0e39` |
| Water boundary | `20260720T031519.090134Z-15764` | `pass/58/1/3/1/1/1/0/1/0` | `a2e1a307a1603d94c228eca111f58a79fd72983f782e9fec2b7087942b906cb9` |

The tuple order is
`status/damage/required/policy/radius-evaluated/can-damage-observed/`
`can-damage-result/BSP-verified/water-verified/target-undamaged`.
Every row also records `canonical_historical_hit=0`, authenticated and
advanced 56,000-microsecond current-world spawn-forward, and
`failure_code=0`.

Artifacts:

- `.tmp/networking/fr10_t12_rocket_splash_clear_v35.json`;
- `.tmp/networking/fr10_t12_rocket_splash_bsp_occlusion_v35.json`; and
- `.tmp/networking/fr10_t12_rocket_splash_water_boundary_v35.json`.

The current built and isolated-staged sgame binaries are byte-identical at
SHA-256
`c62d4e67e68a1b10337a7a48cbb10cb0378f273b60f4b23e6e29d7e26f0b3090`.
The stable launcher, engine, cgame, pak, and map inputs retain the hashes
recorded by the preceding mover-relative slice. At the time this focused v35
evidence was captured, the shared `.install/` tree had intentionally not been
refreshed; its then-staged sgame was
`bb9b445ca945d50e58d9b0060596d7e4fda52558da62d5608969af8cb6cd1de1`.
The later stable post-`FR-10-T07` refresh now stages
`c62d4e67e68a1b10337a7a48cbb10cb0378f273b60f4b23e6e29d7e26f0b3090`,
matching `builddir-win/sgame_x86_64.dll` and the isolated v35 stage.

## Build and focused validation

```text
meson compile -C builddir-win sgame_x86_64
  pass

python tools/networking/test_run_canonical_rail_damage_runtime_gate.py
  51/51: pass

python tools/networking/test_run_fr10_t12_acceptance.py
  12/12: pass

python tools/networking/test_lag_compensation_canonical_rail_contract.py
  49/49: pass

python -m py_compile <five changed T12 runner/test modules>
  pass

git diff --check -- <T12 implementation and focused-test files>
  pass; line-ending notices only
```

No full `.tmp/networking/fr10_t12_partial_acceptance.json` was produced and no
claim is made for the 37-mode aggregate in this continuation.

## Source provenance

- `src/game/sgame/network/lag_compensation.cpp`:
  `d4a59d03b1ebca8bb90ee2bad97e8b0cd4114d87bb15bf3d39bb2255af9ee969`;
- `src/game/sgame/network/lag_compensation.hpp`:
  `cf800efa18a87ec189f370afe2ddad451cac3d1d95b4bfc4a93c04f53dbafd33`;
- `src/game/sgame/gameplay/g_combat.cpp`:
  `fe6798f95ee50549d2751c52bfa92f1bc721efb461639ca690ccdc61d188a394`;
- `src/game/sgame/gameplay/g_svcmds.cpp`:
  `6cb0fe275e78e79930b8bf0789df5e8db6377fdbc219204b42e6b638447741fd`;
- canonical runtime runner:
  `8ee786d0fd454b37a5d803c8309b918384cea0ffe350769d92af557cec3a3a01`;
- T12 parent runner:
  `93c9c05c57c93a9d29351ef8f3850eb4afe8137d60c8633c07343dcfed761127`;
- canonical runner tests:
  `0dc5d62b246158fec0405a5920b1238e7a53d9bcdf8a3d28a3d1c4f9dc109e96`;
- T12 parent tests:
  `ce0eb2f962e537d3785541c4b952b4da56fd0db30ebab92d16a70738392735de`;
- lag-compensation source-contract tests:
  `e7802bf2dd4aa37d7b60fd89d52e964b4a75259ca628bd1ceacfe3fba34367af`.

## Remaining T12 coverage

The parent remains explicitly partial with these six open classes:

1. exhaustive non-weapon interaction catalog;
2. projectile ownership, lifetime, and collision matrix;
3. moving- and multi-target fairness;
4. trigger and deployable lifecycle breadth;
5. coop, monster, and other melee interactions; and
6. abuse, load, and release-sequence breadth.

Broader mover-relative splash, multiple simultaneous victims, team/friendly-
fire behavior, and load/fairness variants remain within those open classes.
