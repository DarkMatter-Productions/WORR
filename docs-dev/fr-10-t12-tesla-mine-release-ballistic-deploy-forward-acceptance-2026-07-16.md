# FR-10-T12 Tesla Mine release ballistic deployment acceptance

Date: 2026-07-16  
Project task: FR-10-T12

## Scope

Tesla Mine policy 20 permits only a fresh mine's initial bounded
current-world gravity advance after the normal held-throw release callback.
The authorization is created only from the real attack-to-no-attack command
edge and is bound to the live shooter identity, weapon, policy, map epoch,
accepted command identity, expiry, and one launch. A later prime invalidates
an unused authorization.

The advance is capped at 100 ms and is resolved as gravity-before-move
current-world hull sweeps. Any contact rejects the advance entirely, leaving
normal Bounce physics as the sole owner of collision response, placement, and
later deployable behavior. Consumed flight time is subtracted from the normal
activation and expiration schedule so the policy cannot extend either.

The policy supplies no historical impact, collision, target, chain, damage,
effect, destruction, or lifetime decision. Landing, activation, target scans,
Tesla damage, chain behavior, destruction, and expiration remain normal
current-world production authority.

## Acceptance proof

The tesla-mine mode of
tools/networking/run_canonical_rail_damage_runtime_gate.py runs a dedicated
server and two hidden, input-disabled clients. It accepts only a normal
client-side no-attack release after the fixture has observed the real attack
command. It requires:

- the normal Tesla Mine callback;
- no historical projectile-impact claim;
- an authenticated, unblocked current-world ballistic advance; and
- unchanged staged current geometry.

Three headless repeats at
.tmp/networking/canonical-tesla-mine-install-runtime.json pass. They record
authenticated clear current-world advances of 56, 56, and 40 ms, no historical
impact, no block, unchanged current geometry, and explicit termination of the
dedicated server and both clients. The post-run audit finds no WORR client,
dedicated, or engine process.

Focused source and runner contracts pass 76/76. The server game module builds,
the .install staging root is refreshed, and the registered Windows Meson suite
passes 139/139.

This closes only the fresh release-to-deployment advance seam. Tesla landing,
activation, target selection, chains, damage, destruction, expiration,
moving/multiple targets, movers, cooperative rules, fairness, abuse, and load
coverage remain open.
