# FR-10-T12 Phalanx current-world splash acceptance

Date: 2026-07-16  
Project task: `FR-10-T12`

## Policy

Phalanx remains policy `14`, `WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD`. The
only compensated operation is its bounded, accepted command-age spawn advance
through the current world. No historical trace, target, contact, explosion, or
radius result participates in either direct or splash damage.

The splash acceptance fixture reuses a generic current-world projectile impact
observer. It verifies only that the production projectile that received the
accepted spawn-forward result struck the fixture's present-world, non-damageable
blocker. The observer does not return collision data, pick a damage target, or
modify damage. `phalanx_touch` then remains the exclusive owner of direct
damage, `RadiusDamage`, effects, and free/lifetime behavior.

## Acceptance fixture

The fixture preserves six normal target-history samples, moves the live target
off the shell's firing ray, then creates the blocker only after that history
is retained and just before a real client `BUTTON_ATTACK`. Normal Phalanx
flight must hit the blocker; normal `RadiusDamage` must reach the target around
the blocker’s clear side.

The Generic weapon’s two ordinary barrel frames both reach the blocker. The
normal production splash results are 48 and 45 damage, so acceptance requires
their exact `93`-damage total. The gate also requires policy `14`, no claimed
historical impact, positive authenticated/advanced current-world forward
progress, no blocked forward sweep, unchanged live target geometry, and the
verified production impact identity.

This proves one stationary, two-barrel, clear-side splash layout only. It does
not claim Phalanx splash through arbitrary occlusion, moving/multiple targets,
movers, water, deployables, triggers, cooperative interactions, general
cadence/fairness, or load/release coverage.

## Validation

```powershell
python -m unittest tools/networking/test_run_canonical_rail_damage_runtime_gate.py tools/networking/test_lag_compensation_canonical_rail_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon phalanx-splash --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-phalanx-splash-install-runtime.json --repeat 3 --port 28109 --timeout 55
meson test -C builddir-win --print-errorlogs
```

Focused contracts passed `58/58`. The native `sgame_x86_64.dll` build and
`.install` refresh passed. The three-repeat report
`20260716T080531.570368Z-3368` uses runtime schema
`worr.networking.canonical-weapon-damage-runtime.v16`. Every run passed policy
`14` with exact `93` normal production splash damage, no historical impact,
authenticated/advanced current-world forward proof, and no blocked forward
sweep; forward age was 72 ms, 56 ms, and 72 ms. The full Meson suite passed
`137/137`.

The runtime host was `worr_ded_x86_64.exe`. Both client launches used isolated
runtime directories, `stdin=DEVNULL`, no-window process creation,
`win_headless=1`, `in_enable=0`, and `in_grab=0`. No test initialized physical
input or captured a mouse; the gate terminated every server and client process
after each repeat.
