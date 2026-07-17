# FR-10-T12 Rocket Projectile Spawn-Forward Acceptance

Date: 2026-07-16  
Project tasks: `FR-10-T12` (primary), `FR-10-T09` (authenticated canonical
command timing prerequisite)

## Scope

This is the second explicit player-projectile policy. It covers a newly
spawned Rocket Launcher rocket while `g_lag_compensation` is enabled. It does
not rewind rocket contact, impact, splash, radius occlusion, moving targets,
or movers. It also does not accept client collision or damage claims, alter
legacy protocol/demo payloads, or promote native transport authority.

The broader `FR-10-T12` task remains open for remaining projectile classes,
ongoing flight under changing world state, radius/occlusion policy breadth,
melee, movers, deployables, triggers, cooperative interactions, abuse/load,
and platform matrices.

## Accepted policy

`fire_rocket` now calls
`LagCompensation_ResolveProjectileSpawnForward` after the normal current-world
rocket entity is initialized and linked:

- The resolver derives elapsed age from the active server-authenticated
  `mapped_time_us`. It never consumes a client timestamp or packet-ack
  estimate.
- `sg_lag_compensation_projectile_forward_ms` defaults to 100 ms and is
  constrained to the server rewind ceiling. Zero disables this additional
  spawn-forward policy.
- The sweep uses the rocket's own hull, bounds, clip mask, and velocity in a
  current-world `gi.trace`. It does not use historical player/mover proxies or
  any historical trace helper.
- A present-world blocker wins. The unchanged `rocket_touch` callback receives
  that trace and remains exclusively responsible for contact, splash,
  `RadiusDamage`, effects, and entity cleanup.
- A clear sweep relinks the rocket at its current-world end point. Its normal
  expiry schedule is reduced by the accepted elapsed time, so compensation
  cannot add flight lifetime.

The resolver result has an explicit rocket policy tag
`WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD` (9). This prevents a fixture result
for another projectile family from being admitted as Rocket evidence. Its
accepted command ID is also retained and must equal the fixture's received
attack command before forward evidence can complete.

## Acceptance seam

The canonical two-client fixture adds a `rocket` mode:

1. Two real UDP clients connect to a dedicated server. Only the shooter has
   deterministic 50 ms upstream latency.
2. After normal pre-fire history capture, a real received `+attack` reaches
   `ClientThink`, `Item::weaponThink`, `Weapon_RocketLauncher_Fire`, and
   `fire_rocket`.
3. The fixture intentionally moves the target 32 units farther on the current
   aim ray after history capture. It requires an authenticated, non-zero,
   unblocked current-world spawn advance and exact 100 direct Rocket damage.
4. It requires `canonical_historical_hit=0`. That is an invariant: Rocket
   impact/splash is deliberately current authority rather than a disguised
   hitscan rewind query.

All fixture launches are dedicated/headless/input-free. The clients set
`win_headless=1`, `in_enable=0`, `in_grab=0`, `s_enable=0`, and `cl_async=1`;
they use `stdin=DEVNULL`, `CREATE_NO_WINDOW`, and isolated runtime homes. No
test initializes input or captures the mouse.

## Evidence

The final staged gate writes runtime schema
`worr.networking.canonical-weapon-damage-runtime.v9` to
`.tmp/networking/canonical-rocket-projectile-forward-install-runtime.json`.

Three dedicated/two-client repeats pass:

- Every repeat records Rocket policy 9, canonical command scope, a real attack,
  the normal weapon callback, unchanged current target geometry, and exact
  100 current-authority damage.
- Every repeat reports `canonical_historical_hit=0`,
  `projectile_forward_required=1`,
  `projectile_forward_authenticated=1`,
  `projectile_forward_advanced=1`, and
  `projectile_forward_blocked=0`.
- The authenticated and applied forward ages are each 56,000 microseconds,
  below the 100 ms cap. The staged gate terminates both hidden clients and the
  dedicated server after every repeat.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py tools/networking/test_run_canonical_rail_damage_runtime_gate.py tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon rocket --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-rocket-projectile-forward-install-runtime.json --repeat 3 --port 28086 --timeout 35
```
