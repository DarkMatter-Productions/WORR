# Grenade Launcher ballistic spawn-forward and current-world splash acceptance

Date: 2026-07-16  
Project task: FR-10-T12

## Scope

Grenade Launcher now has policy 15,
WORR_REWIND_WEAPON_GRENADE_LAUNCHER_BALLISTIC_SPAWN_FORWARD. It is a
deliberately narrow, current-world-only projectile-spawn policy. An accepted,
mapped canonical command may consume at most 100 ms of authenticated age after
the ordinary grenade is created. It does not rewind, select, or damage a
historical projectile impact.

The resolver reproduces the normal Toss ordering for this bounded interval:
it advances gravity before each swept hull move, using the current world's
collision state. The production grenade is moved to the clear result and
receives the matching forward velocity; its ordinary fuse timestamp is reduced
by the consumed age. The resolution phase has no gameplay side effects.

Any swept current-world contact fails closed: no age is consumed and the grenade
starts at its normal production location. The resolver deliberately does not
invent a bounce, touch, explosion, placement, or historical impact. After
creation, normal Toss physics, grenade touch/explosion, fuse, and radius-damage
code retain authority for all collision, bounce, splash, damage, effects, and
lifecycle decisions.

This is conceptually informed by the separation of trajectory advance and
authoritative impact used by idTech3-era games, while retaining WORR's existing
Quake II Toss implementation and semantics. No Quake III code is copied.

## Authorization and observability

The forward decision accepts only the existing canonical server authorization:
the same live shooter, life generation, map epoch, weapon, policy, expiry, and
bounded launch count are required. It is capped at 100 ms. The generic
current-world splash observer records only the already-created grenade's normal
impact; it cannot choose a target, alter collision, or apply damage.

The runtime fixture captures target history, then creates a present-world
damageable BBOX blocker and places the live target 96 units laterally from the
grenade ray. The normal grenade touch explodes on that current-world blocker;
the target receives ordinary production RadiusDamage from the clear side. This
proves a first current-world ballistic impact/splash seam, not generic bounce,
occlusion, fuse, deployable, mover, trigger, cooperative, or multi-target
compensation.

The acceptance range is intentionally exact and closed: 57 through 60 damage.
The original weapon's small random right/up launch adjustments, together with
RadiusDamage closest-point falloff against the target BBOX, make the normal
production value vary inside that four-value interval. Values below 57 or above
60 fail. The range is therefore not an open tolerance or a substitute for
ownership checks.

## Evidence

The grenade-launcher, three-repeat canonical weapon runtime gate passed against
the refreshed .install stage. All three dedicated-server/two-client runs were
headless and input-free:

| Run | Authenticated / advanced age | Observed normal splash damage |
| --- | ---: | ---: |
| 1 | 56 ms | 58 |
| 2 | 56 ms | 58 |
| 3 | 40 ms | 58 |

Every report recorded policy 15, a current-world fixture impact, no historical
projectile impact, and gate-owned termination of the server, shooter, and
target. The machine-readable artifact is
.tmp/networking/canonical-grenade-launcher-install-runtime.json.

Focused contract and runtime-runner tests passed 62/62. The Windows stage was
refreshed before the runtime gate. The complete Windows Meson suite also passed
137/137.

Automated launch policy remains mandatory: dedicated server, win_headless 1 for
the two hidden clients, in_enable 0, in_grab 0, DEVNULL stdin,
CREATE_NO_WINDOW, and an isolated runtime directory. No test initializes
physical input or captures the mouse.

## Exclusions

This evidence does not promote arbitrary bounce/contact/placement behavior,
fuse timing beyond the consumed spawn age, moving or multiple targets, movers,
water, deployables, triggers, cooperative interactions, fairness/load
matrices, or client-side prediction of every grenade state. Those remain
separate FR-10-T12 work.

## Implementation locations

- inc/common/net/rewind_observation.h: policy identity.
- src/game/sgame/network/lag_compensation.hpp and
  src/game/sgame/network/lag_compensation.cpp: ballistic resolver and runtime
  observation.
- src/game/sgame/gameplay/g_weapon.cpp and
  src/game/sgame/player/p_weapon.cpp: normal grenade lifecycle integration.
- tools/networking/run_canonical_rail_damage_runtime_gate.py: schema v18
  fixture and closed damage-range verification.
