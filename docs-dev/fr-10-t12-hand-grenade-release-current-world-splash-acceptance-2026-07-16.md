# FR-10-T12 Hand Grenade release current-world splash acceptance

Date: 2026-07-16  
Project task: `FR-10-T12`

## Scope

Hand Grenade policy 16 accepts only the authenticated no-attack release edge
after the real prime/hold sequence. That command may advance the newly spawned
grenade through a bounded clear current-world gravity path. It never rewinds
contact, bounce, fuse, target selection, occlusion, or damage.

The splash fixture is derived only after the accepted production grenade exists.
It uses the projectile's post-forward origin and velocity to place a
damageable current-world blocker 96 units ahead, and places the isolated live
target 128 units laterally from that blocker. The fixture supplies geometry
and player placement only. Normal Toss physics selects the collision;
`Grenade_Touch`, `Grenade_Explode`, and `RadiusDamage` remain the only
contact, explosion, and damage authorities.

## Acceptance proof

The `hand-grenade-splash` mode of
`tools/networking/run_canonical_rail_damage_runtime_gate.py` runs a dedicated
server and two hidden, input-disabled clients. It requires:

- the real release command and accepted policy-16 mapping;
- a clear authenticated 100 ms-capped current-world forward result;
- identity match between that released grenade and the normal blocker contact;
- no historical impact claim; and
- normal current-world splash damage in the closed 45–60 window.

Three headless repeats at
`.tmp/networking/canonical-hand-grenade-splash-runtime.json` pass with an
identical 56 ms advance, no historical impact, exact 45 damage, and explicit
harness termination of server, shooter, and target for every repeat.
Focused source/runner/headless-policy contracts pass 78/78 and the full
registered Windows Meson suite passes 139/139.

This closes only the bounded release/current-world splash seam. Moving,
multi-target, water, mover, chained interaction, long-hold, occlusion, load,
and cooperative cases remain open.
