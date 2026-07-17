# FR-10-T12 ETF flechette spawn-forward acceptance

Date: 2026-07-16  
Project task: `FR-10-T12`

## Policy

Policy `13`, `WORR_REWIND_WEAPON_ETF_FLECHETTE_SPAWN_FORWARD`, permits an ETF
flechette to consume only bounded server-authenticated command age after its
normal current-world muzzle probe. The advance uses the existing projectile
hull in the live world and remains capped by
`sg_lag_compensation_projectile_forward_ms` (100 ms).

The policy does not perform a historical target, contact, or damage query.
Normal flechette collision, `flechette_touch`, direct damage, visual effects,
and lifetime remain production authority. The legacy muzzle probe may touch
the owner while the flechette origin is within the player bounds; that ignored
owner touch keeps the original projectile alive, while a real contact still
consumes it before any forward advance.

## Acceptance

The canonical two-client fixture uses the normal ETF Rifle callback and a
real held-command refresh edge after `Weapon_Repeating` enters its firing
state. The current target's muzzle-to-player-box gap is 129 units, beyond the
full 115-unit ETF advance at 1150 units/second and the 100 ms cap. Passing
therefore requires remaining ordinary current-world flight and exact 10
direct damage; it explicitly requires no historical impact.

```powershell
python -m unittest tools/networking/test_run_canonical_rail_damage_runtime_gate.py tools/networking/test_lag_compensation_canonical_rail_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon etf-rifle --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-etf-flechette-install-runtime-repeat3.json --repeat 3 --port 28090 --timeout 35
```

Focused contracts passed `54/54`, the native sgame build and staged refresh
passed, and all three runtime repeats passed under schema
`worr.networking.canonical-weapon-damage-runtime.v14`. Every run reported
policy `13`, no historical impact, clear authenticated current-world advance
(40–56 ms), and exact 10 normal direct damage.

The test used `worr_ded_x86_64.exe` with two hidden clients
(`win_headless=1`, `in_enable=0`, `in_grab=0`), isolated runtime directories,
no stdin, and no-window process creation. It never initialized physical input
or captured the mouse.
