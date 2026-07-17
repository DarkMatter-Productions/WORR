# FR-10-T12 Ion Ripper burst current-world spawn-forward acceptance

Date: 2026-07-16  
Project task: FR-10-T12

## Scope

Ion Ripper policy 19 permits only the initial bounded current-world advance of
the fifteen bolts created by one normal Weapon_IonRipper_Fire callback. The
callback still creates its own randomized directions and speeds, performs its
ordinary muzzle-clearance trace, and creates all fifteen projectile entities.

Each fresh bolt may consume only the accepted command's 100 ms-capped age in a
current-world hull sweep. A deferred Generic callback, when applicable, is
bound to the live shooter, weapon, policy, map epoch, command identity, expiry,
and exactly fifteen launches. That budget cannot authorize an additional bolt.

The policy does not perform historical impact, target, collision, damage,
effect, or lifetime selection. Normal bolt contact, current-world blockers,
damage, effects, and expiration remain production authority.

## Acceptance proof

The ion-ripper mode of
tools/networking/run_canonical_rail_damage_runtime_gate.py runs a dedicated
server and two hidden, input-disabled clients. It requires:

- the normal policy-19 Ion Ripper callback;
- no historical projectile-impact claim;
- an authenticated, unblocked current-world forward result;
- unchanged staged current geometry; and
- exactly fifteen authenticated/advanced current-world bolt launches.

The burst count is emitted in the runtime status and required by schema v22,
so the gate cannot pass after observing only one bolt.

Three headless repeats at
.tmp/networking/canonical-ion-ripper-install-runtime.json pass. Every repeat
records 15/15 launches, 56 ms authenticated/current-world advance per bolt,
no block, no historical impact, unchanged current geometry, and explicit
server/shooter/target termination by the gate. The post-run audit finds no
WORR client, dedicated, or engine process.

Focused source and runner contracts pass 74/74. The server game module builds
and the .install staging root is refreshed. The registered Windows Meson suite
passes 139/139.

This closes only the initial current-world burst-advance seam. Spread
fairness, contact/damage behavior, environment interaction, moving or multiple
targets, movers, cooperative rules, abuse, and load coverage remain open.
