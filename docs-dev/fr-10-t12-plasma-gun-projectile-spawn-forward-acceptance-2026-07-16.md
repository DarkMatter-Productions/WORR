# FR-10-T12 Plasma Gun Current-World Spawn-Forward Acceptance

Date: 2026-07-16

Project task: `FR-10-T12`

## Scope

This slice gives the fast, non-homing Plasma Gun an explicit independent
policy rather than applying hitscan rewind to a projectile:

- policy `10` is `WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD`;
- normal current-world muzzle clearance runs first;
- only the active, server-authenticated command mapping may contribute a spawn
  advance, capped by `sg_lag_compensation_projectile_forward_ms` (100 ms in
  the acceptance gate);
- the advance is a normal current-world projectile-hull trace;
- normal Plasma Gun flight, contact, direct damage, small-radius splash,
  lifetime, and effects remain production-owned and current-authoritative.

The policy does not query a historical player/mover scene for projectile
flight, contact, direct impact, or radius damage. A live blocker therefore
wins the spawn advance. Consumed forward time also shortens the ordinary
projectile lifetime so a delayed command cannot gain extra server-time.

This acceptance slice proves only the normal direct-hit path. Plasma Gun
small-radius splash, splash occlusion, water behavior, moving/multi-target
fairness, mover/deployable/trigger interactions, and load/fairness matrices
remain separate open `FR-10-T12` work.

## Real-command fixture

The console arm `worr_rewind_canonical_plasma_gun_damage_arm` stages two
already admitted real clients and preserves target history before a real client
`+attack` command. It sets Plasma Gun frame 43 (the ordinary idle frame) and
does not call a weapon, trace, or damage function itself.

After history capture, the live target moves 32 units farther along the aim
ray and is 224 units from the fixture origin. That exceeds the maximum
100 ms / 200-unit Plasma Gun forward distance at speed 2000, so the policy
must first record an unblocked current-world spawn advance and then rely on
normal subsequent projectile flight for the direct impact. The terminal
contract requires:

- an authenticated command identity equal to the firing command;
- a positive, unblocked current-world forward advance no greater than the
  mapped command age;
- `canonical_historical_hit=0`;
- normal weapon callback and unchanged live target geometry;
- exact current-authority direct damage of 20.

The fixture does not accept a historical contact, fabricated damage, or a
radius result as a direct hit.

## Headless validation

The two-client gate uses `worr_ded_x86_64.exe` for the server. Both client
processes set `win_headless=1`, `in_enable=0`, `in_grab=0`,
`s_enable=0`, and `cl_async=1`, receive `stdin=DEVNULL`, use isolated
`fs_homepath` directories, and start with `CREATE_NO_WINDOW` on Windows.
The only action is a server-stuffed normal `+attack` command; no test opens a
visible client, initializes physical input, or captures the mouse.

Focused static/runtime-gate contracts:

```powershell
python -m unittest tools.networking.test_lag_compensation_canonical_rail_contract tools.networking.test_lag_compensation_rail_damage_contract tools.networking.test_run_canonical_rail_damage_runtime_gate -v
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

The focused contracts passed `52/52`, the native sgame target built, and the
staged payload validated. The fresh staged runtime evidence is:

```powershell
python tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon plasma-gun --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-plasma-gun-projectile-forward-install-runtime.json --repeat 3 --port 28087 --timeout 35
```

Report
`.tmp/networking/canonical-plasma-gun-projectile-forward-install-runtime.json`
uses schema
`worr.networking.canonical-weapon-damage-runtime.v11`. All three runs passed
with policy `10`, `canonical_historical_hit=0`, authenticated and advanced
forward age `56000` microseconds, no forward block, and exact expected and
observed direct damage `20`.

## Outcome

Plasma Gun now has a bounded modern projectile policy that consumes only
server-authenticated delay in the current world. The work strengthens
`FR-10-T12` without claiming that its broader projectile or radius matrix is
complete.
