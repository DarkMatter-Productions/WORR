# FR-10-T12 BFG current-world spawn-forward acceptance

Date: 2026-07-16  
Project task: FR-10-T12

## Scope

BFG policy 18 accepts only the bounded delayed launch of one newly-created BFG
projectile. The policy is entered through the ordinary Weapon_BFG_Fire
callback, which runs the normal frame-9 wind-up and creates the projectile at
frame 17. The accepted command remains tied to its shooter identity, weapon,
policy, map epoch, command identity, expiry, and one launch.

The normal Generic callback can fall outside the short authorization used by
other projectiles. BFG therefore has its own 1.25-second authorization window,
which covers its eight 100 ms production animation intervals without expanding
the 100 ms projectile-forward distance cap. The forward resolver traces only
the fresh projectile hull in the current world. It does not use a historical
impact, target, collision, laser, touch, explosion, splash, or damage result.

After the bounded advance, ordinary BFG logic remains unchanged:
bfg_think, laser damage, contact, staged explosion, radius damage, effects,
and lifetime all remain current-world production authority. This acceptance
seam intentionally does not claim any of those later lifecycle outcomes.

## Acceptance proof

The bfg mode of
tools/networking/run_canonical_rail_damage_runtime_gate.py uses a dedicated
server plus two hidden input-disabled clients. It requires:

- the real policy-18 BFG callback after the normal wind-up;
- an accepted command mapping and a one-launch deferred authorization;
- a positive current-world forward result, capped to 100 ms;
- no historical projectile-impact claim; and
- unchanged current geometry.

Three headless repeats at
.tmp/networking/canonical-bfg-install-runtime.json pass. Their authenticated
ages are 664, 744, and 680 ms respectively; every result is clamped to the
100 ms cap, advances successfully, and is unblocked. Every run records normal
server, shooter, and target process termination by the gate, and the post-run
process audit finds no WORR client, dedicated, or engine process.

Focused source and runner contracts pass 72/72. The server game module builds
and the .install staging refresh packages 429 assets. The registered Windows
Meson suite passes 139/139.

This closes only BFG's initial bounded current-world launch. BFG laser,
touch, explosion, radius, moving/multiple target, water, mover, cooperative,
fairness, abuse, and load scenarios remain open.
