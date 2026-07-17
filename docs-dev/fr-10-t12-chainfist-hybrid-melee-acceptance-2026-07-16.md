# FR-10-T12 Chainfist hybrid melee acceptance

Date: 2026-07-16  
Project task: `FR-10-T12`

## Scope

Policy `12`, `WORR_REWIND_WEAPON_CHAINFIST_MELEE_HYBRID`, makes Chainfist a
bounded hybrid interaction rather than a hitscan trace or a rewound-world
melee attack.

History establishes only whether one living player was within the original
Chainfist reach/FOV footprint at the accepted canonical command time. The
selection:

- accepts only a server-authenticated canonical command; it has no legacy
  acknowledgement or client-timestamp fallback;
- queries every current live player at the same bounded historical instant and
  fails closed to ordinary current melee if that roster cannot be queried;
- chooses at most one nearest historical player candidate;
- requires that candidate's live origin be no farther than
  `sg_lag_compensation_melee_max_displacement` (default and gate value:
  `64` units) from its historical origin.

The normal `fire_player_melee` broadphase still owns current non-player
targets. When the historical player roster is coherent, it excludes only
present-time player candidates and re-adds the one eligible, displacement-
accepted player. The existing live `CanDamage` check remains the final world
occlusion decision, and the existing `Damage` call owns health, armour,
knockback, effects, and death. No historical world collision, entity relink,
or historical direct damage occurs.

This intentionally does not claim moving/multi-target melee fairness, a
larger melee catalogue, complex mover contacts, underwater effects, or
general radius/trigger/co-op compensation.

## Acceptance fixture

The canonical two-client fixture arms only server-owned setup and consumes one
ordinary `BUTTON_ATTACK` command through normal `Item::weaponThink` and
`Weapon_ChainFist_Fire`.

- The retained historical player bounds are 48 units apart: the 16-unit box
  gap is inside Chainfist's 24-unit reach.
- The live target moves 64 units off-axis. It is outside ordinary present-time
  Chainfist reach but exactly meets the live displacement guard.
- The terminal proof requires canonical scope, command identity agreement,
  historical eligibility for the selected target, live displacement acceptance,
  current geometry, and exactly 15 normal deathmatch damage.

The report schema is
`worr.networking.canonical-weapon-damage-runtime.v13`. It adds explicit melee
selection/authentication/eligibility/displacement fields so a melee result
cannot be misreported as a generic hitscan historical trace.

## Validation

```powershell
python -m unittest tools/networking/test_run_canonical_rail_damage_runtime_gate.py tools/networking/test_lag_compensation_canonical_rail_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon chainfist --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-chainfist-hybrid-melee-install-runtime-repeat3.json --repeat 3 --port 28089 --timeout 35
```

Focused contracts passed `52/52`; the native `sgame_x86_64.dll` build and
`.install` refresh passed. All three staged runtime repeats passed with policy
`12`, exact `15` damage, positive authenticated age `56 ms`, and
`melee_selection_authenticated=1`, `melee_historical_eligible=1`,
`melee_current_displacement_accepted=1`, and
`melee_current_displacement_units=64`.

The run remained compliant with the workspace launch rule: the host was
`worr_ded_x86_64.exe`; both clients used `win_headless=1`, `in_enable=0`, and
`in_grab=0`, isolated runtime directories, `stdin=DEVNULL`, and
no-window process creation. The only client action was the ordinary
server-stuffed `+attack`; no test initialized physical input or captured a
mouse.
