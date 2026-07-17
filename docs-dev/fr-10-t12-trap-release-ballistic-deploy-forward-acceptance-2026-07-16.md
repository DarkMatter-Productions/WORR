# FR-10-T12 Trap release ballistic deployment acceptance

Date: 2026-07-16  
Project task: FR-10-T12

## Scope

Trap policy 21 permits only a fresh Trap's initial bounded current-world
gravity advance after the normal held-throw release callback. The authorization
is created only from the real attack-to-no-attack command edge and is bound to
the live shooter identity, weapon, policy, map epoch, accepted command
identity, expiry, and one launch. A later prime invalidates an unused
authorization.

The advance is capped at 100 ms and follows gravity-before-move current-world
hull sweeps. Any contact rejects the advance entirely, leaving normal Bounce
physics as the only owner of collision response, placement, and later Trap
behavior. Consumed flight time is subtracted from the existing first-think and
30-second expiry schedules, so the policy cannot extend either.

The policy supplies no historical impact, collision, capture, target,
destruction, effect, damage, or lifetime result. Landing, capture and release,
destruction, and expiration remain normal current-world production authority.

## Acceptance proof

The trap mode of tools/networking/run_canonical_rail_damage_runtime_gate.py
runs a dedicated server and two hidden, input-disabled clients. It accepts
only a normal client-side no-attack release after the fixture has observed the
real attack command. It requires:

- the normal Trap callback;
- no historical projectile-impact claim;
- an authenticated, unblocked current-world ballistic advance; and
- unchanged staged current geometry.

Three headless repeats at
.tmp/networking/canonical-trap-install-runtime.json pass. Every repeat records
a 56 ms authenticated clear current-world advance, no historical impact, no
block, unchanged current geometry, and explicit termination of the dedicated
server and both clients. The post-run audit finds no WORR client, dedicated,
or engine process.

Focused source and runner contracts pass 78/78. The server game module builds,
the .install staging root is refreshed, and the registered Windows Meson suite
passes 139/139.

This closes only the fresh release-to-deployment advance seam. Trap landing,
capture/release, destruction, expiration, moving/multiple targets, movers,
cooperative rules, fairness, abuse, and load coverage remain open.
