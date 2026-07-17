# FR-10-T12 Hand Grenade Release-Ballistic Current-World Forward Acceptance

Date: 2026-07-16  
Project task: `FR-10-T12`

## Scope

This acceptance adds policy `16`,
`WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD`, for the
normal hand-grenade release path. It is deliberately a narrow projectile-spawn
latency seam. It does not rewind a grenade collision, bounce, touch, fuse,
trigger, splash, or damage result.

The policy applies only when `Weapon_HandGrenade_Fire` emits the ordinary
non-held throw. The held self-detonation path remains ordinary gameplay and
cannot authorize spawn advance.

## Release authority

The server records the first authentic attack-to-no-attack command edge while
the prior accepted client button state still contains attack. That pending
authorization is bound to the shooter, life, map epoch, weapon, policy,
canonical command identity, and its short expiry. A new attack clears any
unconsumed hand-grenade release authorization, and exactly one normal
non-held fire callback may consume it.

This is intentionally different from normal held-fire projectile policies:

- a held attack cannot later be relabelled as a release;
- a later zero-input command cannot replace the original release command;
- release-only authority cannot be used by another policy, and ordinary
  deferred authority cannot be used by policy 16;
- failure to validate any binding, expiry, command context, or snapshot age
  fails closed to the unadvanced normal grenade.

The client now asks for an immediate normal packet after `-attack`, just as
it already does for `+attack`. This affects only command delivery:
`IN_AttackUp` still builds the normal client command and neither initializes
physical input nor changes server weapon authority.

## Current-world ballistic boundary

After the normal grenade is spawned, an accepted command age may advance only
the initial clear portion of flight. The existing projectile-forward cap is
`sg_lag_compensation_projectile_forward_ms` (100 ms in acceptance), and the
resolver steps present-world gravity before each present-world hull movement.
Any contact rejects the whole forward result; it does not fabricate a bounce,
touch, trigger, or historical collision.

On an accepted clear result the production hand-grenade path applies only the
returned position, velocity, and known elapsed fuse lifetime, relinks the
entity, and retains normal Toss/touch/think/splash authority. The dynamic
`Grenade_Touch` observer labels a later hand-grenade contact as policy 16,
but it does not alter that normal current-world branch.

## Headless runtime evidence

The staged installed gate used a dedicated server plus two hidden clients with
isolated runtime directories. Both clients used `win_headless=1`,
`in_enable=0`, `in_grab=0`, disabled sound, `stdin=DEVNULL`, and
`CREATE_NO_WINDOW`; no test initialized input, captured the mouse, or
launched an interactive client. The only release action was an ordinary
client-side `stuff <id> "-attack"` command after the server observed the
normal priming attack.

Command:

```powershell
python tools/networking/run_canonical_rail_damage_runtime_gate.py --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-hand-grenade-install-runtime.json --weapon hand-grenade --repeat 3 --timeout 50 --lag-debug 2
```

All three repeats passed under report schema
`worr.networking.canonical-weapon-damage-runtime.v19`:

| Requirement | Result |
|---|---|
| normal command scope, attack, and weapon callback | present in all runs |
| policy | 16 in all runs |
| historical projectile impact | 0 in all runs |
| fallback / failure | 0 in all runs |
| current geometry unchanged | 1 in all runs |
| forward authorization / advance | 1 / 1 in all runs |
| clear current-world forward age | 56 ms, 56 ms, 40 ms |
| current-world forward block | 0 in all runs |

The mode intentionally does not create a contact fixture, so
`damage_applied=0` is expected and is not a damage acceptance claim. The
existing Grenade Launcher policy 15 gate separately proves normal
current-world grenade touch and splash authority. Hand-grenade bounce, fuse,
water, mover, deployable, trigger, cooperative, occlusion, abuse, load, and
broader radius-damage matrices remain open under `FR-10-T12`.

## Verification and compatibility

```powershell
python -m unittest tools.networking.test_lag_compensation_canonical_rail_contract tools.networking.test_run_canonical_rail_damage_runtime_gate
```

The focused source and runner contracts pass 65/65. The change is server-game
policy state plus client command urgency; it leaves `q2proto/` untouched and
does not change legacy server or demo wire compatibility. The normal,
unadvanced hand-grenade path remains the fail-closed rollback behavior.
