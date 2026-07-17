# FR-10-T12 Phalanx current-world spawn-forward acceptance

Date: 2026-07-16  
Project task: `FR-10-T12`

## Policy

Policy `14`, `WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD`, permits a
newly-spawned Phalanx shell to consume only the accepted, server-authenticated
mapped-command age through one bounded hull sweep of the current world. The
advance is capped by `sg_lag_compensation_projectile_forward_ms` (100 ms).
It is never a historical collision, target, contact, direct-damage, or splash
query.

Phalanx fires through `Weapon_Generic`, so its normal barrel callback can
occur after the initial command-context scope has ended. At the real
`ClientThink` command boundary the server retains a short-lived authorization
containing only the accepted policy decision, shooter life-generation identity,
matching weapon, expiry, and bounded launch count. The later normal
`fire_phalanx` call may consume that authorization only when all of those
checks still pass. It carries no result from a past world and cannot authorize
a different shooter, life, map epoch, policy, weapon, or an expired command.

After the optional advance, the ordinary current-world `phalanx_touch` path
still owns collision effects, direct damage, `RadiusDamage` splash, and the
projectile lifetime. A blocking current-world sweep invokes that same normal
touch callback. The forward time also shortens the normal lifetime so advance
cannot extend the shell's time in flight.

This acceptance slice proves direct contact only. It deliberately does not
claim a Phalanx splash/occlusion matrix, two-barrel cadence, moving or multiple
targets, mover contacts, water, deployables, triggers, cooperative
interactions, fairness/load, or release-platform coverage.

## Acceptance fixture

The canonical two-client fixture uses the production `Weapon_Phalanx`
callback. It retains six normal target-history samples, submits a real
client-side `BUTTON_ATTACK`, and uses an ordinary client release/press edge
only when the Generic weapon needs a later held command to reach fire frame 7
or 8. The target stays 64 units farther along the live aim ray. A 725
units/second shell can consume at most 72 units at the 100 ms cap, so the
remaining normal current-world flight must cause the exact direct hit.

Passing requires all of the following:

- policy `14` and exactly `80` normal direct damage;
- no claimed historical impact;
- positive authenticated and advanced current-world spawn-forward age;
- no blocking current-world forward sweep; and
- unchanged staged current geometry at the production callback.

## Validation

```powershell
python -m unittest tools/networking/test_run_canonical_rail_damage_runtime_gate.py tools/networking/test_lag_compensation_canonical_rail_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon phalanx --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-phalanx-install-runtime.json --repeat 3 --port 28106 --timeout 55
meson test -C builddir-win --print-errorlogs
```

The focused contracts passed `56/56`. The native `sgame_x86_64.dll` build and
`.install` refresh passed. The three-repeat gate reported runtime schema
`worr.networking.canonical-weapon-damage-runtime.v15` in run
`20260716T074812.602250Z-25420`. All three runs passed policy `14` with exact
`80` damage, no historical impact, authenticated/advanced current-world forward
proof, and no blocked sweep. The applied advance was 72 ms, 56 ms, and 72 ms
respectively; the report compares only stable semantics for determinism. The
full Meson suite passed `137/137`.

The runtime host was `worr_ded_x86_64.exe`. Both client launches used isolated
runtime directories, `stdin=DEVNULL`, no-window process creation,
`win_headless=1`, `in_enable=0`, and `in_grab=0`. The gate terminates all server
and client processes after each repeat. It does not initialize physical input
or capture a mouse.
