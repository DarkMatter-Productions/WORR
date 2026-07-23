# FR-10-T12 Mover-Relative Projectile Current-World Acceptance

Date: 2026-07-20  
Project task: `FR-10-T12`  
Dependencies: `FR-10-T10`, `FR-10-T11`

## Result

One previously open T12 interaction cell now has explicit production policy and
three fresh-process headless repetitions: a Rocket fired at a real client rider
on a real rotating mover uses paired historical player/mover samples for proof,
but retains current-world authority for projectile spawn advance, contact, and
damage. All three repetitions passed.

This is a bounded T12 continuation, not task closure. The T12 partial manifest
is now 35 modes across 19 weapon policies and requires 105 live child runs when
executed in full. This continuation ran only the newly added
`rocket-mover-relative` mode three times; it does not claim a new full
35-mode aggregate artifact or close the seven remaining coverage classes.

## Explicit policy

`WORR_LAG_COMPENSATION_MOVER_RELATIVE_CURRENT_WORLD` is policy value `1`.
For a projectile launched by an authenticated command:

- the projectile begins at its production current-world muzzle pose;
- the bounded launch-delay estimate performs only a current-world projectile
  hull sweep;
- current player and mover colliders own that sweep and all later flight;
- the normal production touch owns target selection and contact;
- current sgame damage and knockback own the outcome; and
- historical mover-relative poses are never installed into live edicts.

This is deliberately not hitscan-style rewind. Historical target/mover pairs
prove that the interaction really involved a moving rider and moving mover;
they do not supply the Rocket's collision result.

## Production and fixture path

The isolated fixture is armed by
`worr_rewind_canonical_rocket_mover_relative_arm` and publishes
`sg_worr_rewind_canonical_rocket_mover_relative_status`. Arming selects two real
clients plus the fixture map's real `func_rotating`; it does not fabricate an
attack command, projectile, trace, contact, or damage result.

Before the real received attack:

1. normal end-frame capture retains generation-qualified target and mover poses
   at the same server tick/time;
2. each target pose must identify the mover and retain matching
   `mover_relative_origin` and `mover_relative_angles` data;
3. at least two paired samples must show both target world-origin motion and
   mover origin-or-angle motion;
4. the live mover is translated 32 units in X and the rider is restaged at the
   same mover-relative offset, with a small squared-distance epsilon used only
   to tolerate floating-point cancellation after translation; and
5. the real client command proceeds through the ordinary Rocket weapon path.

`LagCompensation_ResolveProjectileSpawnForward()` fingerprints all live
player/mover collision authority before and after its current-world sweep. The
fingerprint must be unchanged, and the resolver contains no
`TraceHistoricalScene` call. The exact spawned projectile identity is then
latched as evidence only. The ordinary `rocket_touch` passively records an
exact-projectile/exact-target, non-start-solid production contact before
`Damage()` can legitimately change the rider's ground and velocity state.
Terminal proof also requires exact 100 damage and
`canonical_historical_hit=0`.

No status flag feeds a weapon, collision, or damage decision.

## Runtime integration

The canonical headless runner schema is
`worr.networking.canonical-weapon-damage-runtime.v34`. Its new mode is:

| Field | Value |
|---|---|
| Mode | `rocket-mover-relative` |
| Weapon policy | `9` (`WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD`) |
| Expected damage | `100` |
| Client count | `2` |
| Mover-relative policy | `1` (current world) |
| Required repeats | `3` |
| T12 coverage label | `current-projectile-mover-relative-direct` |

The T12 parent retains a literal manifest rather than discovering child modes.
The added row is therefore reviewable and cannot silently broaden task claims.
The raw number of matching history pairs is required to be at least two but is
excluded from the repeat determinism signature because normal client admission
and attack timing can retain a different number of otherwise valid pairs.

## Fresh headless evidence

Command:

```text
python tools/networking/run_canonical_rail_damage_runtime_gate.py \
  --client-exe .install/worr_x86_64.exe \
  --dedicated-exe .install/worr_ded_x86_64.exe \
  --working-dir .install \
  --output .tmp/networking/fr10_t12_rocket_mover_relative_acceptance.json \
  --port 28030 --repeat 3 --timeout 35 \
  --weapon rocket-mover-relative
```

Result:

- run ID: `20260720T022311.683821Z-26448`;
- started: `2026-07-20T02:23:11.683821+00:00`;
- completed: `2026-07-20T02:23:40.164478+00:00`;
- artifact SHA-256:
  `254fb5e8a1ab3e112a506839c4e514046ad66bdfc7c1ba35c1a725aabd6e0d7a`;
- all three rows: `status=pass`, `failure_code=0`,
  `attack_received=1`, `weapon_callback=1`,
  `canonical_historical_hit=0`, `damage_applied=1`,
  `expected_damage=100`, and `observed_damage=100`;
- all three rows: authenticated and advanced 56,000 microseconds through the
  current world, with `projectile_forward_blocked=0`;
- all three rows: `mover_relative_policy=1`, target history moved, mover history
  moved, translated pair preserved, current-world impact observed, and live
  collision authority unchanged;
- matching paired-history counts were `36`, `37`, and `37`; and
- all three rows retained exact scoped/leased/joined local-action continuity.

Per-run log hashes:

| Run | Server stdout | Shooter stdout | Target stdout |
|---|---|---|---|
| 1 | `f15f95bbd08ccf0787cc44dc81706504e72a6cd6d8060f641b65fa447f27d696` | `fefd6b93d55c42032084c3e5965db14a87c0a11aab3ec7e19a77116e6aab383e` | `d164ecc962340e5d5507f7f8e2b00ceea91ce247dd2b0c1de0db567f0efb6284` |
| 2 | `ded3ae72a08eca449afe0ab668f14cea6cec316c7828a3b3e8d6622321ee0148` | `d73097216cb8b768ab469559b4ef634c198f76f6d07b8b43bdef55a45a9ad9da` | `354e56d0f057c190f5a0763260fc6d5cdbd3fa792559c7f474f9710d1e64a567` |
| 3 | `887c024a358004671f3a550081716a00fbba9770e5203d84cdf625ac6703dda4` | `18e0a33aa7a73f62e2cf8375ac0d097812c26bc041ba5de6be327dc06f56cbe6` | `c41eb68171c3d463edc281b4127cdb7cacfcb775f52fc126fe01f2f29fabf161` |

## Build and focused validation

```text
meson compile -C builddir-win sgame_x86_64
  pass

python tools/refresh_install.py --build-dir builddir-win \
  --assets-dir assets --install-dir .install
  pass: 16 root runtime files, 1 dependency, basew runtime,
        601 packaged assets, and 1 q2aas reference map

python tools/networking/test_run_canonical_rail_damage_runtime_gate.py
  50/50: pass

python tools/networking/test_lag_compensation_canonical_rail_contract.py
  49/49: pass

python tools/networking/test_run_fr10_t12_acceptance.py
  12/12: pass
```

Staged runtime hashes used by the successful live gate:

| Target | SHA-256 |
|---|---|
| `.install/worr_x86_64.exe` | `2143bb6d95981b275e408369d016dfa7ceb03c5bbc9f9bf522f166215a00abae` |
| `.install/worr_ded_x86_64.exe` | `2cb54523fd2ec1a1533fe787ac6d9a23580d0bf01354a6fbef61e86a239fb54f` |
| `.install/worr_engine_x86_64.dll` | `697c405af1bfad333221cdd3157a22c2b7f97377d8da22b39d4e4188046922bb` |
| `.install/worr_ded_engine_x86_64.dll` | `61f4d162ef0e7f7497cd72ca465cec31f8e2aa443362236a5aec7732060edb2e` |
| `.install/basew/sgame_x86_64.dll` | `bb9b445ca945d50e58d9b0060596d7e4fda52558da62d5608969af8cb6cd1de1` |
| `.install/basew/cgame_x86_64.dll` | `d94c4f01f992c9a831368d7b9fd15550912769754978aeeea01a3fdbf18811a6` |
| `.install/worr_opengl_x86_64.dll` | `8a0f85cb67cc22ba59e02a22b8237bfdcaea6b58d5e72980129dd32107aff28c` |
| `.install/worr_vulkan_x86_64.dll` | `f092a777c180c3dd8112e81ee647dcf6d0bbb1e4cc83a19c0810c33d025bdd02` |
| `.install/worr_rtx_x86_64.dll` | `3b16afe31c94c532e245e4eb5cba2cb60f1f15b2f1c045cac9520720c23c238e` |
| `.install/basew/pak0.pkz` | `2dc3e6c016e196c6a171854b0d7210c8f7002da3c33e0c97c4491f5d0e166217` |
| `.install/basew/maps/worr_fr10_rewind_mover.bsp` | `42642d702c39f5d7a4d788964c4a9e9e5e7d92c5cc3cf45b953cef987fe6fe3d` |

The staged cgame and renderer hashes identify the shared-tree staging snapshot;
this T12 slice did not modify cgame or renderer source.

## Source and runner provenance

- `src/game/sgame/network/lag_compensation.cpp`:
  `03b8c78817f0f30dc5f051b75fb324a821564dd5aadf1ce609926f12a422e514`;
- `src/game/sgame/network/lag_compensation.hpp`:
  `49d77fec5c919bc32e17dd6848da40f7b301a818231f7a36dc1b51dc5d2e919e`;
- `src/game/sgame/gameplay/g_svcmds.cpp`:
  `1054bbdeeaee3f9f694106675cc6cb19524846bdb978631629f7474e869ab767`;
- canonical runtime runner:
  `ff668662ee9554825e349722c135d9b44b52ce92a4d3659860135f879bb6b366`;
- T12 parent runner:
  `8859039d729600ef278271a2c7d7d16df5ebf09cd8c0b52c5e2c238ffe664788`;
- canonical runner tests:
  `a54ae5f323335885e89e43ac12199656e6418c26aa479389d4e61a6a7e45f6ac`;
- lag-compensation source-contract tests:
  `350d4d12321bf8f04fb7cf5f1f5a8830829fa0e9c7d3d74e479f9adb01710384`;
- T12 parent tests:
  `4347e8bec9802ea354cbb511a50ed49645a6eb404ef597b8cacf72ca63c21179`.

## Remaining T12 coverage

The mover-relative projectile/contact row is no longer an open class. The
partial parent still publishes these seven open classes and keeps
`task_complete=false`:

1. exhaustive non-weapon interaction catalog;
2. projectile ownership, lifetime, and collision matrix;
3. splash occlusion across player, BSP, and water boundaries;
4. moving- and multi-target fairness;
5. trigger and deployable lifecycle breadth;
6. coop, monster, and other melee interactions; and
7. abuse, load, and release-sequence breadth.

Broader mover classes and multiple simultaneous riders/targets remain part of
the moving/multi-target fairness work. `FR-10-T12` therefore remains unchecked.
