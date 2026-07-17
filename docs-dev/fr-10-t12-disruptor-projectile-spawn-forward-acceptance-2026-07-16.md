# FR-10-T12 Disruptor Projectile Spawn-Forward Acceptance

Date: 2026-07-16  
Project tasks: `FR-10-T12` (primary), `FR-10-T11` (canonical command
context and Disruptor convergence prerequisite)

## Scope

This milestone adds the first explicit projectile timing policy. It applies
only to a newly spawned player Disruptor projectile while
`g_lag_compensation` is enabled. It does not make projectile collision a
historical rewind query, does not accept client hit claims, and does not
change legacy protocol or demo payloads.

The wider `FR-10-T12` work remains open: other projectile families,
continuous flight under changing world state, splash/radius, melee, movers,
deployables, triggers, cooperative interaction, abuse/load, and platform
matrices are outside this acceptance seam.

## Accepted policy

`Weapon_Disruptor_Fire` keeps its existing hybrid target-selection policy:
the point/expanded Disruptor convergence query selects an optional target from
the sealed historical collision scene. `fire_disruptor` still creates the
projectile at the present server muzzle, and its normal `disruptor_fly`,
`disruptor_touch`, damage daemon, link, and lifetime state remain
current-authority gameplay.

Immediately after the usual close-muzzle obstruction trace succeeds,
`LagCompensation_ResolveProjectileSpawnForward` may advance that new
projectile through the current world:

- It obtains the active canonical policy decision and derives elapsed time from
  the server-authenticated `mapped_time_us`, never from a client-owned
  timestamp or packet-ack estimate.
- `sg_lag_compensation_projectile_forward_ms` defaults to 100 ms and is
  clamped to the configured historical-rewind ceiling. Zero disables this
  additional forward policy.
- The advance is a current-world projectile-hull `gi.trace` from the new
  entity's actual origin using its own bounds and clip mask. It never calls a
  historical trace helper or relinks historical player/mover state.
- A present-world blocker stops the advance. The existing weapon touch callback
  receives the current trace and retains sole ownership of effects and damage.
  A clear advance relinks the projectile at its swept end.
- The remaining normal think/lifetime schedule is reduced by the accepted
  elapsed interval, so delayed spawn compensation does not grant extra
  projectile lifetime.

The result reports authenticated age, applied age, cap state, and
current-world block state. Debug diagnostics also count requests,
authenticated decisions, advances, clamps, blocks, and rejections.

## Acceptance seam

The existing canonical Disruptor fixture remains production-owned:

1. Two real UDP clients connect to a dedicated server under deterministic
   50 ms upstream impairment for the shooter.
2. A normal received `+attack` reaches `ClientThink`,
   `Item::weaponThink`, `Weapon_Disruptor_Fire`, and `fire_disruptor`.
3. The fixture requires the historical convergence proof and the normal
   current-authority 45-damage daemon result, plus one authenticated,
   non-zero, unblocked projectile forward result whose applied age does not
   exceed the authenticated age.

The runner starts only a dedicated server and hidden clients. Its clients use
`win_headless=1`, `in_enable=0`, `in_grab=0`, `s_enable=0`,
`cl_async=1`, `stdin=DEVNULL`, `CREATE_NO_WINDOW`, and isolated runtime
homes. It neither initializes client input nor captures the mouse.

## Evidence

The final staged run used runtime schema
`worr.networking.canonical-weapon-damage-runtime.v8`:

- `.tmp/networking/canonical-disruptor-projectile-forward-install-runtime.json`
  records three passing dedicated/two-client repeats.
- Every repeat records policy `6`, exact current-authority 45 damage,
  positive 56,000 microsecond historical selection age, and unchanged live
  query geometry.
- Every repeat also records
  `projectile_forward_required=1`,
  `projectile_forward_authenticated=1`,
  `projectile_forward_advanced=1`,
  `projectile_forward_blocked=0`, and equal authenticated/applied
  56,000 microsecond forward ages. The 100 ms cap was therefore not reached.
- The gate terminated both hidden clients and the dedicated server after every
  repeat. The final `.install` refresh validated the Windows staged payload.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon disruptor --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-disruptor-projectile-forward-install-runtime.json `
  --repeat 3 --port 28087 --timeout 35
```
