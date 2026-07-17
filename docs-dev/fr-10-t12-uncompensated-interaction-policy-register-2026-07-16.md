# FR-10-T12 uncompensated interaction policy register

Date: 2026-07-16  
Project task: `FR-10-T12`

The following production interaction families are explicitly current-world and
uncompensated until a family-specific policy and acceptance proof exists:

- BFG laser, delayed explosion, and radius effects (policy 18 covers only the
  separately accepted initial bounded current-world launch);
- Ion Ripper contact, damage, effects, lifetime, and interaction behavior
  (policy 19 covers only the separately accepted initial fifteen-bolt
  current-world burst advance);
- Grapple attachment, pull, ownership, and detach lifecycle (policy 22 covers
  only the separately accepted fresh-hook current-world launch); the off-hand
  `hook` string command remains entirely current-world;
- Native `+hook` input (policy 24 covers only its separately accepted fresh
  clear current-world spawn-forward; attachment, pull, contact, damage, reset,
  detach, and lifecycle remain current-world); and
- Tesla landing, activation, target acquisition, chain behavior, destruction,
  and expiration (policy 20 covers only the separately accepted initial
  release-bound current-world ballistic deployment advance);
- Trap landing, capture, release, destruction, and expiration (policy 21
  covers only the separately accepted initial release-bound current-world
  ballistic deployment advance); and
- ProBall possession, pickup, direct item-use pass, touch, re-grab,
  attraction, bounce, out-of-bounds, reset, goal, scoring, team, and lifecycle
  interactions (policy 23 covers only the separately accepted fresh
  Chainfist-held release's current-world ballistic launch).

They must not inherit hitscan rewind or a generic projectile-forward result.
Existing unspecified-policy routing therefore fails closed to normal
current-world production behavior. The remaining families need their own
bounded authority boundaries and runtime proofs; they must not inherit the BFG
launch policy.
