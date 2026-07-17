# FR-10-T12 Proximity Launcher Ballistic Deploy-Forward Acceptance

Date: 2026-07-16  
Project task: `FR-10-T12`

## Scope

This acceptance adds policy `17`,
`WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD`, for a normal
Proximity Launcher command. It is deliberately the deployable's bounded
launch-latency seam, not an attempt to rewind mine collision, placement,
arming, trigger selection, visibility, explosion, or damage.

## Authority boundary

After the real `Weapon_ProxLauncher_Fire` callback, the newly linked mine may
consume only the accepted command age, capped by
`sg_lag_compensation_projectile_forward_ms` (100 ms in acceptance). The
resolver mirrors gravity-before-move in short present-world hull steps. Any
contact rejects the entire advance and leaves the ordinary Bounce mine at its
unadvanced spawn state; it cannot manufacture a partial bounce, placement,
trigger, or hit.

For a clear path, the production mine receives only the resolved position and
velocity. Its normal `timeStamp` lifetime is reduced by the elapsed accepted
time, so delayed command delivery cannot grant it extra life. The existing
current-world `prox_land`, arm delay, `Prox_TriggerThink`, visibility
check, `Prox_Explode`, and `RadiusDamage` paths remain unmodified.

## Headless runtime evidence

The staged gate used a dedicated server and two hidden clients with isolated
runtime paths. Both clients used `win_headless=1`, `in_enable=0`,
`in_grab=0`, disabled sound, `stdin=DEVNULL`, and
`CREATE_NO_WINDOW`. The only client action was the normal server-stuffed
`+attack`; no OS input or mouse path was initialized or captured.

```powershell
python tools/networking/run_canonical_rail_damage_runtime_gate.py --client-exe E:/Repositories/WORR/.install/worr_x86_64.exe --dedicated-exe E:/Repositories/WORR/.install/worr_ded_x86_64.exe --working-dir E:/Repositories/WORR/.install --output E:/Repositories/WORR/.tmp/networking/canonical-prox-launcher-install-runtime.json --weapon prox-launcher --repeat 3 --timeout 50 --lag-debug 2
```

All three report-schema-v20 repeats passed with policy 17, normal command
scope/attack/callback, no historical projectile impact, no fallback, unchanged
live target geometry, authenticated clear present-world advance, and no
current-world block. Advanced ages were 56 ms, 40 ms, and 56 ms.

The fixture deliberately creates no landing or trigger situation, so
`damage_applied=0` is expected. It is not a mine explosion or radius-damage
claim. Mine landing on world/movers, arm delay, trigger visibility and target
selection, chained mines, water/slime/lava, destruction, explosions,
multi-target radius effects, cooperative rules, abuse, load, and fairness
matrices remain open.

## Verification and compatibility

```powershell
python -m unittest tools.networking.test_lag_compensation_canonical_rail_contract tools.networking.test_run_canonical_rail_damage_runtime_gate
meson test -C builddir-win --print-errorlogs
```

The focused source/runner suite and the full configured suite pass after the
change. `.install/` was refreshed and validated for `windows-x86_64` after
the sgame build. The change does not edit `q2proto/`, does not alter legacy
wire compatibility, and fails closed to the normal unadvanced mine when its
authorization or current-world trace is not valid.
