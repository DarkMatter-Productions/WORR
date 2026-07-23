# FR-10-T12 Completion Audit and Partial-Gate Evidence

Date: 2026-07-20  
Project task: `FR-10-T12`  
Dependencies reviewed: `FR-10-T10`, `FR-10-T11`

## Decision

`FR-10-T12` remains **incomplete**. The 39-mode aggregate manifest is a useful
bounded regression gate for the production weapon policies numbered `6`
through `24`, but it does not cover every interaction class declared by the
task. The runner correctly labels both its manifest and evidence as `partial`,
publishes six open coverage classes, and hard-codes `task_complete=false`.

Because the completion precondition failed, this audit did not relabel the
runner or check the roadmap task. The mover-relative continuation ran only its
new mode three times; the subsequent splash-occlusion continuation ran only
the clear-player, BSP-blocked, and water-boundary Rocket modes three times
each. The subsequent Rocket lifecycle continuation ran its single-touch and
scheduled-expiry modes three times each. A future full 117-repetition
execution of the current runner can prove
only its declared partial scope; it cannot satisfy the missing Definition of
Done cells.

## Declared-class audit

| Declared interaction class | Evidence in the 39-mode manifest | Completion result |
|---|---|---|
| Continuous beams | First tick, three-tick hold, 32-tick hold, release, water retrace, and Thunderbolt discharge modes | Bounded weapon seams covered; indefinite and wider lifecycle/fairness cases remain open |
| Projectile spawn/forward estimate | Direct or launch modes for Disruptor and policies `9` through `24`, including a mover-relative Rocket current-world row plus exact Rocket owner/single-touch retirement and target-free scheduled-expiry rows | Initial forward seams and two Rocket lifecycle cells covered; the remaining projectile-family ownership, lifetime, and collision matrix is open |
| Splash/radius damage | Narrow Rocket, Plasma Gun, Phalanx, grenade, hand-grenade, and Proximity Mine cases; Rocket now independently proves clear-player, exact real-BSP blocked, and exact real-water-boundary outcomes through production `RadiusDamage`/`CanDamage` | The published player/BSP/water class is covered; broader mover-relative, multi-target, other-weapon, team/friendly-fire, and load matrices remain open elsewhere |
| Melee | One single-player Chainfist hybrid mode | Other melee, moving targets, multiple targets, monsters, and coop are absent |
| Movers and mover-relative poses | One Rocket direct mode uses a real moving rider/rotating mover, paired target/mover history, preserved relative staging, current-world impact authority, and an unchanged live collision fingerprint | One bounded mover-relative policy cell covered; broader moving/multi-target fairness remains open |
| Deployables and triggers | One Proximity Mine lifecycle mode; Tesla and Trap launch-only modes | Targeting, trigger, destruction, chained, and broader lifecycle coverage is open |
| Coop and monster interactions | None | Open |
| Multi-target/friendly-fire/fairness and abuse | One bounded moving-rider/current-mover case; no multi-target, friendly-fire, or abuse matrix | Open |

The aggregate parent evidence records the same missing scope under
`coverage.open` as six explicit classes:

- exhaustive non-weapon interaction catalog;
- remaining projectile-family ownership, lifetime, and collision matrix;
- moving- and multi-target fairness;
- trigger and deployable lifecycle breadth;
- coop, monster, and other melee interactions; and
- abuse, load, and release-sequence breadth.

The existing uncompensated-policy register is also intentionally bounded. It
classifies later behavior for BFG, Ion Ripper, Grapple/off-hand Hook, Tesla,
Trap, and ProBall, but is not an exhaustive register for non-weapon, broader
mover, collision, occlusion, coop, and fairness classes above.

## Evidence executed

```text
python tools/networking/test_run_fr10_t12_acceptance.py
  12/12: pass

python tools/networking/test_run_canonical_rail_damage_runtime_gate.py
  54/54: pass

python tools/networking/test_lag_compensation_canonical_rail_contract.py
  51/51: pass

python -m py_compile \
  tools/networking/run_fr10_t12_acceptance.py \
  tools/networking/test_run_fr10_t12_acceptance.py
  pass

meson compile -C builddir-win sgame_x86_64
  pass

python tools/networking/run_canonical_rail_damage_runtime_gate.py \
  --client-exe <isolated-stage>/worr_x86_64.exe \
  --dedicated-exe <isolated-stage>/worr_ded_x86_64.exe \
  --working-dir <isolated-stage> --repeat 3 --timeout 35 \
  --weapon <rocket-splash mode>
  clear-player 3/3: pass
  BSP-blocked 3/3: pass
  water-boundary 3/3: pass

python tools/networking/run_canonical_rail_damage_runtime_gate.py \
  --client-exe <isolated-stage>/worr_x86_64.exe \
  --dedicated-exe <isolated-stage>/worr_ded_x86_64.exe \
  --working-dir <isolated-stage> --repeat 3 --timeout 35 \
  --weapon <rocket lifecycle mode>
  single-touch retirement 3/3: pass
  scheduled lifetime expiry 3/3: pass
```

The focused tests prove that:

- the literal manifest contains exactly 39 modes and policies `6` through
  `24`;
- three repeats would produce exactly 117 live child executions;
- policy, damage, mode, repeat, launch, isolation, termination, provenance,
  and semantic-repeat drift fail closed;
- previous-task and local-action modes cannot leak into this artifact; and
- the resulting parent artifact cannot claim completion and retains its
  explicit open-coverage list.

The preceding mover-focused artifact proves its added mode only. Run
`20260720T022311.683821Z-26448` records three passing rows with exact 100 damage,
no historical hit, current-world policy `1`, authenticated 56,000-microsecond
forward advance, paired moving target/mover history, preserved relative
staging, exact production target contact, and an unchanged live collision
fingerprint. Matching pair counts `36`, `37`, and `37` meet the minimum while
remaining correctly outside the stable semantic signature.

The new v35 splash artifacts prove the three policy cells only. Clear-player
run `20260720T031552.349131Z-6056` passes three times with production
`CanDamage=1` and exact 58 damage. BSP-blocked run
`20260720T031622.003021Z-44580` passes three times with an exact linked
`func_rotating` blocker, production `CanDamage=0`, and exact zero damage.
Water-boundary run `20260720T031519.090134Z-15764` passes three times with an
exact linked `func_water` boundary, production `CanDamage=1`, and exact 58
damage. Every row retains no historical projectile hit and an authenticated,
advanced 56,000-microsecond current-world sweep. Schema v35 has 101 status
fields and a 79-field stable semantic signature.

The v36 Rocket lifecycle artifacts add two exact policy cells. Touch run
`20260720T042938.172809Z-18656` passes three times with owner identity retained,
one current-world target touch, exact 100 damage, the exact projectile
generation retired by production `FreeEntity`, and no second touch or damage
during the 250 ms hold. Every row records base lifetime 10,000 ms and touch
retirement at 96 ms. Expiry run `20260720T042822.736959Z-63280` passes three
times with owner identity retained, zero touches, zero damage, and exact
expiry retirement. Every row records 9,952 ms elapsed plus the authenticated
56 ms spawn advance, or 10,008 ms against the production 10,000 ms schedule.
Schema v36 has 113 status fields and a 90-field stable semantic signature;
only measured lifecycle elapsed time is excluded from that signature and is
validated independently on every row.

Audited source hashes:

- `tools/networking/run_fr10_t12_acceptance.py`:
  `b3b6dda71b74402def76d03dabd6c5307585ea90f2999baec049fc844b365c64`;
- `tools/networking/test_run_fr10_t12_acceptance.py`:
  `717976be0123a5506f4c0b2c40e416e90d5857d9097ac5450d42c1809ee6fc75`;
- `tools/networking/run_canonical_rail_damage_runtime_gate.py`:
  `5b342057131f029d6868d39f9f3fda6f075c4b0d818347b39c67f75863f1b97a`;
- `tools/networking/test_run_canonical_rail_damage_runtime_gate.py`:
  `e91b37b6399d4aaefac638c1ea6edbb97344b5a7fd243567beb71d0a8bb62bfb`;
- `tools/networking/test_lag_compensation_canonical_rail_contract.py`:
  `d157cd24deaaf9aca5888171f5c24ad494178d476872ea4571fccab956cd7f4b`.

No new full `.tmp/networking/fr10_t12_partial_acceptance.json` was produced.
The focused mover artifact remains
`.tmp/networking/fr10_t12_rocket_mover_relative_acceptance.json`, SHA-256
`254fb5e8a1ab3e112a506839c4e514046ad66bdfc7c1ba35c1a725aabd6e0d7a`.
The new focused artifacts are
`.tmp/networking/fr10_t12_rocket_splash_clear_v35.json`, SHA-256
`3c8e685dbeccef54eecb5dd38032375dbf14b5638524d63cbd0517235028c202`;
`.tmp/networking/fr10_t12_rocket_splash_bsp_occlusion_v35.json`, SHA-256
`99368c5a49db1264fa57bb3331983521a1758801151a1ad2eb4421e5123c0e39`;
and `.tmp/networking/fr10_t12_rocket_splash_water_boundary_v35.json`, SHA-256
`a2e1a307a1603d94c228eca111f58a79fd72983f782e9fec2b7087942b906cb9`.
The v36 lifecycle artifacts are
`.tmp/networking/fr10_t12_rocket_lifecycle_touch_v36.json`, SHA-256
`7ec08475e0611f607efaef82f476ad86de19535a765a7d8328a76abd7a059836`,
and `.tmp/networking/fr10_t12_rocket_lifetime_expiry_v36.json`, SHA-256
`af88efa4cdd7e9e8a500f758f8e366afa6f44d107efed82dbe94467addc03d00`.
The current build and isolated v36 staged sgame share SHA-256
`dc6ee0046b46afd05abba20373f09fb8fa2bf5d4ec6032024e929875cc2653f3`.
At the time this focused v35 evidence was captured, the continuation had not
refreshed `.install/`; its then-staged sgame was
`bb9b445ca945d50e58d9b0060596d7e4fda52558da62d5608969af8cb6cd1de1`.
The later stable post-`FR-10-T07` refresh now stages
`c62d4e67e68a1b10337a7a48cbb10cb0378f273b60f4b23e6e29d7e26f0b3090`,
which matched `builddir-win/sgame_x86_64.dll` and the isolated v35 stage when
that evidence was captured.
The v36 continuation intentionally did not refresh `.install/`; its existing
sgame remains `c62d4e67e68a1b10337a7a48cbb10cb0378f273b60f4b23e6e29d7e26f0b3090`.
Full policy and fixture provenance is recorded in
`docs-dev/fr-10-t12-mover-relative-projectile-current-world-acceptance-2026-07-20.md`
and
`docs-dev/fr-10-t12-splash-occlusion-player-bsp-water-acceptance-2026-07-20.md`.
The focused lifecycle implementation and evidence are recorded in
`docs-dev/fr-10-t12-rocket-projectile-lifecycle-current-world-acceptance-2026-07-20.md`.

## Remaining acceptance needed for closure

Closure requires a reviewed interaction-family register plus deterministic
production evidence for every missing class, including independent
ownership/lifetime/contact outcomes, broader mover-relative splash, moving and
multiple targets, friendly-fire/coop/monster/melee semantics,
deployable/trigger lifecycles, and fairness/abuse cases. Only then
may the aggregate manifest be expanded, its completion semantics changed, the
full repeated headless gate run, and `FR-10-T12` checked complete.
