# FR-10-T12 Proximity Launcher lifecycle current-world acceptance

Date: 2026-07-16

Project task: FR-10-T12

## Scope

Policy 17 now covers one real Proximity Launcher deployable lifecycle. A real,
server-authenticated command creates the normal bouncing mine. The bounded
gravity-before-move current-world advance is still the only rewind-owned
operation. Once that clear advance has completed, a non-damageable current-world
MoveType::Push landing surface is staged under the mine. It provides no trace,
touch, plane, attachment, target-selection, or damage result.

The normal physics frame must call prox_land and create the production
arm-delay trigger. Only after that normal landing does the fixture stage its
isolated real target 64 units right and 64 units above the mine. Production
radial candidate/visibility checks must select the target, then the normal
delayed Prox_Explode must invoke RadiusDamage.

The packaged FR-10 firing lane is intentionally open and has no floor. The
post-advance landing surface is therefore test geometry, like the existing
current-world splash blockers: it makes a normal collision observable without
bypassing collision or deployable ownership.

## Authority boundary

LagCompensation_ResolveProjectileSpawnForward still fails closed on every
contact during its initial bounded ballistic sweep. It neither queries a
historical collision world nor synthesizes a bounce, landing, trigger,
explosion, or damage result.

The lifecycle hooks are passive:

- LagCompensation_ObserveProxMineLanded accepts only the authenticated mine
  after prox_land has linked its actual trigger.
- LagCompensation_ObserveProxMineTriggered runs only after production radial
  candidate and visibility checks accept the staged live target.
- LagCompensation_ObserveProxMineExploded runs after ordinary RadiusDamage.

The gate requires all three observations for the same mine identity, an
authenticated and advanced clear-forward proof, unchanged staged live geometry,
and no historical impact. Status schema
worr.networking.canonical-weapon-damage-runtime.v21 appends
prox_lifecycle_required, prox_mine_landed, prox_mine_triggered, and
prox_mine_exploded.

## Deterministic evidence

The staged three-repeat runner used the dedicated server with two hidden
clients. Every run passed with policy 17, no historical hit, a clear
authenticated 56 ms advance, lifecycle flags 1/1/1 for
landing/trigger/explosion, and exact 61 normal RadiusDamage damage. The fixed
post-landing target position makes 61 the closed expected value.

All launches used a dedicated host or win_headless=1, in_enable=0, and
in_grab=0, with disabled audio, DEVNULL stdin, CREATE_NO_WINDOW, and isolated
runtime homes. No test initialized or captured a mouse.

## Verification

- Focused source and runner tests: 69/69 passed.
- ninja -C builddir-win sgame_x86_64.dll passed.
- refresh_install.py refreshed and validated .install.
- The three-repeat staged lifecycle runtime gate passed:
  .tmp/networking/canonical-prox-launcher-lifecycle-install-runtime.json.
- The runner's Windows process-tree cleanup regression plus the headless-input
  contract passed 34/34. A fresh one-repeat staged lifecycle gate also passed
  and, immediately afterward, no WORR client, dedicated server, or engine
  process remained.

## Remaining scope

This does not prove arbitrary mine placement, other surface types, water/lava,
destruction, chain reactions, multiple targets, mover-relative placement,
cooperative rules, fairness abuse cases, load, or release readiness.
FR-10-T12 remains incomplete.
