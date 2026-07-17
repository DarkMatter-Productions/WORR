# FR-10-T12 Grapple fresh-hook current-world spawn-forward acceptance

Date: 2026-07-16  
Project task: FR-10-T12

## Scope

Grapple policy 22 permits only a fresh normal Grapple hook's bounded
current-world forward flight. The authorization is bound to the live shooter
identity, Grapple weapon, policy, map epoch, accepted command identity, expiry,
and one launch. The normal Generic frame-six hook callback can consume that
single authorization for at most 750 ms; the hook's actual movement remains
capped at 100 ms.

Weapon_Grapple_FireHook keeps its normal immediate muzzle-clearance trace. Only
after that trace is clear may the fresh hook use the current-world swept hull
advance. A blocked advance is not converted into a synthetic touch: normal
FlyMissile/touch processing remains the owner of contact and all later state.

The policy supplies no historical impact, collision, target, attachment, pull,
cable, damage, reset, detach, or lifetime decision. Grapple touch, attachment,
pull, damage, reset, and detach behavior remain normal current-world production
authority. The off-hand Hook remains unspecified/current-world.

## Acceptance proof

The grapple mode of
tools/networking/run_canonical_rail_damage_runtime_gate.py runs a dedicated
server and two hidden, input-disabled clients. It requires:

- the normal frame-six Grapple callback;
- no historical projectile-impact claim;
- an authenticated, unblocked current-world hook advance; and
- unchanged staged current geometry.

Three headless repeats at
.tmp/networking/canonical-grapple-install-runtime.json pass. They record
authenticated clear current-world advances of 40, 56, and 56 ms, no historical
impact, no block, unchanged current geometry, and explicit termination of the
dedicated server and both clients. The post-run audit finds no WORR client,
dedicated, or engine process.

Focused source and runner contracts pass 80/80. The server game module builds,
the .install staging root is refreshed, and the registered Windows Meson suite
passes 139/139.

This closes only the fresh-hook clear-flight seam. Grapple attachment, pull,
cable, direct damage, reset/detach, movers, moving/multiple targets,
cooperative rules, fairness, abuse, and load coverage remain open. The off-hand
Hook remains entirely current-world and unproven.
