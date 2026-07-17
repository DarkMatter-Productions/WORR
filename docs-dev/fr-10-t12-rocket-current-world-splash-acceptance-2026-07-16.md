# FR-10-T12 Rocket Current-World Splash Acceptance

Date: 2026-07-16  
Project tasks: `FR-10-T12` (primary), `FR-10-T09` (authenticated canonical
command timing prerequisite)

## Scope

This acceptance slice extends the existing Rocket Launcher spawn-forward policy
with one explicit splash/radius scenario. It does not rewind a Rocket's flight,
contact, impact, splash, radius occlusion, target, or mover state. It does not
accept client collision or damage claims, change legacy protocol/demo payloads,
or promote native transport authority.

## Accepted policy

Policy `WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD` (9) remains a bounded,
server-authenticated spawn advance only:

- The active canonical command provides the mapped server time; its 56 ms age
  is bounded by `sg_lag_compensation_projectile_forward_ms=100`.
- The new Rocket sweeps its normal hull through the present world. Historical
  player poses and historical movers are never consulted for this query.
- The production `rocket_touch` callback retains sole ownership of impact,
  direct damage, `RadiusDamage`, effects, and cleanup. The fixture observer
  records only that the accepted spawned Rocket hit its expected present-world
  blocker.

The test creates a non-damageable `SOLID_BBOX` blocker only after the required
six target-history captures and immediately before the real received attack.
The target moves 64 units left and 48 units off the firing ray. Consequently
the Rocket must hit the current blocker, and ordinary `RadiusDamage` must
determine the off-axis target's visibility and reduced damage. The first
production impact deals exact 58 splash damage; a direct 100-damage impact is
explicitly rejected by the runner.

## Acceptance evidence

The schema-v10 staged runtime report is
`.tmp/networking/canonical-rocket-splash-install-runtime.json`.

Each of the three deterministic repeats requires:

- two admitted real UDP clients and a real received `+attack` reaching the
  ordinary Rocket Launcher fire path;
- an accepted policy-9 command and the same spawned Rocket entity at the
  observer;
- `canonical_historical_hit=0`;
- authenticated and applied 56,000 microsecond clear current-world advance,
  without clamping or a forward-sweep block;
- the normal current-world blocker impact and exact reduced 58 damage.

All launches are dedicated/headless/input-free: the server is
`worr_ded_x86_64.exe`; both clients use `win_headless=1`, `in_enable=0`,
`in_grab=0`, `s_enable=0`, and `cl_async=1`, with `stdin=DEVNULL`,
`CREATE_NO_WINDOW`, and isolated runtime homes. No test initializes client
input or captures the mouse.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py tools/networking/test_lag_compensation_rail_damage_contract.py tools/networking/test_run_canonical_rail_damage_runtime_gate.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon rocket-splash --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-rocket-splash-install-runtime.json --repeat 3 --port 28085 --timeout 35
```

This is a narrow current-world Rocket splash seam only. Other projectile
classes, continued flight, moving/multi-target radius behavior, underwater
radius behavior, mover-relative collision, and the remaining `FR-10-T12`
interaction policies remain open.
