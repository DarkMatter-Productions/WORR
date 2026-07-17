# FR-10-T12 Blaster Bolt Current-World Spawn-Forward Acceptance

Date: 2026-07-16

Project task: `FR-10-T12`

## Scope

The shared `fire_blaster` path now has an explicit policy for the straight
player-fired Blaster bolt family:

- policy `11` is `WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD`;
- ordinary current-world muzzle clearance is evaluated before compensation;
- only the active server-authenticated command mapping may advance the newly
  spawned bolt, bounded by `sg_lag_compensation_projectile_forward_ms`;
- that advance uses the projectile hull in the current world;
- normal bolt flight, contact, direct damage, optional HyperBlaster radius
  effects, visual effects, and lifetime remain production current authority.

The policy covers the production spawn function used by both standard Blaster
and HyperBlaster. It never selects historical projectile contact, a historical
radius target, or a historical splash/occlusion result. A present-world blocker
therefore wins. Consumed forward time shortens the normal two-second lifetime,
so accepted latency cannot grant extra server time.

The real runtime seam below proves standard Blaster direct damage only.
HyperBlaster's cadence and optional Quake III radius branch, plus all broader
projectile, splash, mover, deployable, trigger, and fairness matrices remain
open `FR-10-T12` work.

## Real-command fixture

The new `worr_rewind_canonical_blaster_damage_arm` fixture uses the normal
Blaster item callback (idle frame 9) after a real client `+attack`. The
ammo-less base Blaster is now handled explicitly by the generic fixture: an
`IT_NULL` ammo item is not used as an inventory array index.

History is retained before the real attack. The live target then moves 32
units farther along the aim ray and stays 224 units from the fixture origin.
At speed 1500, even the 100 ms maximum permitted advance is only 150 units,
so normal remaining current-world bolt flight must deliver the direct hit. The
terminal contract requires the same authenticated command identity, an
unblocked positive advance no greater than the mapped command age, no
historical impact, unchanged current target geometry, and exact 15 damage.

## Headless validation

The acceptance command is:

```powershell
python tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon blaster --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-blaster-projectile-forward-install-runtime.json --repeat 3 --port 28088 --timeout 35
```

The dedicated server uses `worr_ded_x86_64.exe`. Both clients use isolated
runtime roots, `stdin=DEVNULL`, `CREATE_NO_WINDOW`, `win_headless=1`,
`in_enable=0`, `in_grab=0`, `s_enable=0`, and `cl_async=1`. The sole input
action is a server-stuffed normal `+attack` command. No run opens a visible
client, initializes physical input, or captures the mouse.

Focused static/runtime contracts passed `54/54`, the native sgame target built,
and the staged payload refreshed successfully. Report
`.tmp/networking/canonical-blaster-projectile-forward-install-runtime.json`
uses schema `worr.networking.canonical-weapon-damage-runtime.v12`. All three
runs passed with policy `11`, `canonical_historical_hit=0`, authenticated and
advanced forward age `56000` microseconds, no forward block, and exact
expected/observed direct damage `15`.

## Outcome

Standard Blaster and the shared HyperBlaster spawn path now consume only
bounded server-authenticated delay through current-world geometry. The
implementation adds a clear modern projectile policy without falsely treating
direct or radius damage as rewind results.
