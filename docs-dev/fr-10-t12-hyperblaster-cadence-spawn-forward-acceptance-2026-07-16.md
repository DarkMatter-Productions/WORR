# FR-10-T12 HyperBlaster cadence spawn-forward acceptance

Date: 2026-07-16  
Project task: `FR-10-T12`

## Policy

HyperBlaster uses the shared policy `11`,
`WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD`. The normal muzzle-clearance
probe happens first. A bolt may then consume only bounded,
server-authenticated mapped-command age through a current-world hull sweep.
There is no historical target, contact, direct-damage, or radius query.

The normal `Weapon_HyperBlaster_Fire` cadence and the shared `fire_blaster`
path retain authority over firing cadence, projectile flight, contact, direct
damage, lifetime, effects, and any ruleset-specific radius behavior. An
observation-only fixture hook immediately before the real shared bolt callback
may restore the already staged fixture target pose for a delayed held command;
it does not create input, collision, damage, or a projectile result.

## Acceptance fixture

The two-client fixture retains six ordinary target-history samples and submits
an ordinary client `BUTTON_ATTACK`. The HyperBlaster must receive a later real
held command to enter its production 6–11 gun-frame fire loop. The live target
is 32 units farther along the aim ray and beyond the 100-unit maximum advance
for the 1000 units/second deathmatch bolt under the shared 100 ms cap.

Passing requires policy `11`, a no-historical-impact result, a clear positive
authenticated current-world advance, unchanged staged current geometry, and
exact 15 normal direct damage from the first production HyperBlaster bolt.
This does not claim a full sustained cadence, Q3 radius behavior, moving or
multiple targets, movers, water, or broader splash/fairness/load coverage.

## Validation

```powershell
python -m unittest tools/networking/test_run_canonical_rail_damage_runtime_gate.py tools/networking/test_lag_compensation_canonical_rail_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon hyperblaster --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-hyperblaster-install-runtime.json --repeat 3 --port 28112 --timeout 55
meson test -C builddir-win --print-errorlogs
```

Focused contracts passed `60/60`. The native `sgame_x86_64.dll` build and
`.install` refresh passed. The three-repeat runtime report
`20260716T082807.239204Z-32280` uses schema
`worr.networking.canonical-weapon-damage-runtime.v17`. Every run passed policy
`11` with exact `15` normal direct damage, no historical impact, clear
authenticated/advanced current-world forward proof, no forward sweep block,
and a 56 ms advance. The full Meson suite passed `137/137`.

The test host was `worr_ded_x86_64.exe`. Both clients used isolated runtime
directories, `stdin=DEVNULL`, no-window process creation, `win_headless=1`,
`in_enable=0`, and `in_grab=0`. Physical input was not initialized and the
mouse was not captured; the gate terminated every process after each repeat.
