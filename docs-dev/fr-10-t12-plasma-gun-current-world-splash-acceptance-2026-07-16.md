# FR-10-T12 Plasma Gun Current-World Splash Acceptance

Date: 2026-07-16  
Task: `FR-10-T12`  
Status: Narrow current-world radius-execution seam accepted; parent task remains in progress.

## Objective

Extend the existing Plasma Gun policy `10` acceptance coverage beyond its
direct-hit spawn-forward slice without allowing historical collision, touch,
radius target selection, line-of-sight, or damage authority.

## Accepted contract

The server accepts only the normal real-command Plasma Gun callback after
ordinary muzzle clearance, a same-command server-authenticated 100 ms-capped
current-world spawn forward, and normal current-world projectile touch on a
fixture blocker placed after player-history capture.

The fixture observes that normal touch only after it identifies the exact
accepted Plasma projectile and present-world blocker. It then stages a small,
damageable current-world entity on the clear side of the observed contact. The
entity is not a player hull, has no historical representation, and exists
before `plasmagun_touch` invokes unchanged production `RadiusDamage`. The
blocker is unlinked only after production touch has selected it, so a plasma
entity relinked at its impact point cannot self-occlude the following
production line-of-sight query.

No fixture code calls a weapon function, chooses collision, applies damage,
supplies a radius victim, rewinds a projectile, or bypasses `RadiusDamage`.
The game retains falloff, visibility, candidate scan, and `Damage` authority.
The fixture records the small target's health only after that path completes.

## Implementation

- `plasmagun_touch` now has a passive current-world splash observation seam.
- The canonical fixture retains and cleans up a separately identified small
  radius target on success, failure, shutdown, and map reset.
- `worr_rewind_canonical_plasma_gun_splash_damage_arm` and
  `sg_worr_rewind_canonical_plasma_gun_splash_damage_status` provide a distinct
  runtime channel.
- Gate mode `plasma-gun-splash` requires policy `10`, no historical hit,
  authenticated unblocked spawn-forward, production blocker touch, and exact
  seven-damage radius falloff.

The initial smoke measured seven damage; the accepted contract pins that exact
result rather than accepting an open falloff range.

## Runtime evidence

Three deterministic repetitions completed with mode `plasma-gun-splash`,
policy `10`, `canonical_historical_hit=0`, authenticated unblocked 40–56 ms
current-world spawn-forward, and exact `expected_damage=7` /
`observed_damage=7`. The dedicated server and both isolated clients were
terminated by the harness after each repetition.

Machine-readable report:
`.tmp/networking/canonical-plasma-gun-splash-runtime.json`
(`run_id=20260716T154059.210371Z-8892`).

Every runtime launch used the required no-window/detached-stdin process policy
with `win_headless=1`, `in_enable=0`, and `in_grab=0`; no visible client,
physical input initialization, or mouse capture was used.

## Validation

- `python tools/networking/test_lag_compensation_canonical_rail_contract.py`
  — 46 passing checks.
- `python tools/networking/test_run_canonical_rail_damage_runtime_gate.py`
  — 40 passing checks.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_x86_64`.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64`.
- The three-repeat headless runtime gate above.

## Remaining scope

This does not prove player-hull Plasma splash, map/BSP occlusion, movers,
water, multi-target behavior, self-damage, damage modifiers, impairment,
fairness/load, or release matrices. Those remain open under `FR-10-T12`.
