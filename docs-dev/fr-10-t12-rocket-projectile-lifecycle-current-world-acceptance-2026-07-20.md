# FR-10-T12 Rocket projectile lifecycle current-world acceptance

Date: 2026-07-20

Project task: `FR-10-T12`

Status: implemented and accepted for the two bounded Rocket lifecycle modes;
`FR-10-T12` remains partial because the remaining projectile-family ownership,
lifetime, and collision matrix is still open.

## Scope

This slice adds production-owned lifecycle evidence for two Rocket Launcher
paths after the already accepted authenticated current-world spawn advance:

- `rocket-lifecycle-touch`: a real Rocket retains its exact owner identity,
  touches the live target exactly once in the current world, applies exactly
  100 direct damage, retires through production `FreeEntity`, and remains free
  of a second touch or damage application for a 250 ms post-touch hold.
- `rocket-lifetime-expiry`: the real target remains outside the flight lane,
  the Rocket retains its exact owner identity, receives no touch and applies no
  damage, and retires its exact entity generation through its ordinary
  `8000 / speed` scheduled lifetime. The fixture Rocket speed is the normal
  800 units/second, so the unadjusted schedule is exactly 10,000 ms.

The fixture does not create a projectile, synthesize a contact, call a weapon
function, supply damage, shorten a lifetime, or call `FreeEntity`. It only
stages isolated player geometry and passively observes production boundaries.
`q2proto/` and the wire protocol are unchanged.

## Production implementation

`src/game/sgame/gameplay/g_weapon.cpp` keeps the normal Rocket path in control:

1. `fire_rocket` links the new Rocket with its normal owner and 10,000 ms
   `nextThink`.
2. A passive spawn observer captures the exact non-client entity reference,
   owner, spawn time, and base schedule before authenticated spawn-forward
   processing can subtract already-consumed flight age.
3. `rocket_touch` calls a passive observer only after owner and sky rejection,
   then runs its unchanged direct `Damage`, `RadiusDamage`, effects, and
   production `FreeEntity` path.
4. The scheduled think callback is a behavior-equivalent `rocket_expire`
   wrapper around `FreeEntity`. Passive pre/post observers bracket that normal
   retirement without choosing or advancing it.

For a non-client entity, the captured reference generation is
`spawn_count + 1`. `FreeEntity` increments the prior `spawn_count` and writes
that same value into the freed slot. The post-retirement proof therefore
requires the same entity index, `inUse == false`, class name `freed`, and
`post_free spawn_count == captured generation`. This proves that the exact
previously-live generation was invalidated rather than accepting a reused
slot.

The touch mode cannot pass until authoritative server time is at least 250 ms
after retirement and the target health remains unchanged. The expiry mode
requires:

```text
10,000 ms <= observed elapsed ms + authenticated advanced age ms <= 10,032 ms
```

The upper bound is two fixed 62 Hz server frames. This preserves the exact
production schedule while acknowledging ordinary frame-quantized think
dispatch.

## Runtime schema v36

The child runtime schema is now
`worr.networking.canonical-weapon-damage-runtime.v36`. It appends exactly these
12 fields, in order, after the v35 splash suffix:

1. `rocket_lifecycle_required`
2. `rocket_lifecycle_policy`
3. `rocket_owner_identity_retained`
4. `rocket_touch_count`
5. `rocket_touch_current_world`
6. `rocket_retired`
7. `rocket_retired_by_touch`
8. `rocket_retired_by_expiry`
9. `rocket_post_touch_hold_verified`
10. `rocket_no_double_damage`
11. `rocket_lifetime_scheduled_ms`
12. `rocket_lifetime_elapsed_ms`

Lifecycle policy `1` is touch retirement and policy `2` is lifetime expiry.
The determinism signature includes the first 11 fields and deliberately
excludes only the measured elapsed value. Timing validity is still enforced on
every repetition by the mode validator.

The child now has 113 status fields and a 90-field semantic determinism
signature. The partial T12 parent binds 39 literal modes across 19 weapon
policies and requires 117 live repetitions. Its six-item open list remains
present; only the already-proven Rocket subset was removed from the wording of
the projectile lifecycle class.

## Verification

The final module compiled successfully:

```text
meson compile -C builddir-win sgame_x86_64
```

Focused automated contracts all pass:

```text
54/54  test_run_canonical_rail_damage_runtime_gate.py
51/51  test_lag_compensation_canonical_rail_contract.py
12/12  test_run_fr10_t12_acceptance.py
117/117 combined
```

`git diff --check` passes for the focused source, runner, and test files.

Both live gates used a copied `.install` runtime under
`.tmp/networking/fr10_t12_v36_stage`, replaced only its staged
`basew/sgame_x86_64.dll`, launched a dedicated server plus two hidden,
input-disabled real clients, and required three deterministic repetitions:

| Mode | Exact repeated lifecycle result | Evidence SHA-256 |
| --- | --- | --- |
| `rocket-lifecycle-touch` | policy 1; owner 1; touches 1; current target 1; retired 1/1/0; hold 1; no double damage 1; schedule 10000; elapsed 96; damage 100; forward age 56000 us | `7ec08475e0611f607efaef82f476ad86de19535a765a7d8328a76abd7a059836` |
| `rocket-lifetime-expiry` | policy 2; owner 1; touches 0; current target 0; retired 1/0/1; hold 0; no double damage 1; schedule 10000; elapsed 9952; damage 0; forward age 56000 us | `af88efa4cdd7e9e8a500f758f8e366afa6f44d107efed82dbe94467addc03d00` |

Evidence files:

- `.tmp/networking/fr10_t12_rocket_lifecycle_touch_v36.json`
- `.tmp/networking/fr10_t12_rocket_lifetime_expiry_v36.json`

The final built and isolated-stage module SHA-256 is
`dc6ee0046b46afd05abba20373f09fb8fa2bf5d4ec6032024e929875cc2653f3`.
The canonical child runner SHA-256 is
`5b342057131f029d6868d39f9f3fda6f075c4b0d818347b39c67f75863f1b97a`.

Per this slice's isolation requirement, `.install` was not refreshed. Its
existing `basew/sgame_x86_64.dll` remained
`c62d4e67e68a1b10337a7a48cbb10cb0378f273b60f4b23e6e29d7e26f0b3090`.

## Remaining work and schema consumers

This is not exhaustive projectile lifecycle closure. Ownership transfer,
collision/retirement variants, and lifetime behavior across the remaining
projectile and deployable families still require separate production evidence.
Accordingly, `FR-10-T12` remains incomplete.

Two earlier aggregate consumers still explicitly pin v35 and require an
intentional compatibility decision plus their own full live reruns before they
can consume v36:

- `tools/networking/run_fr10_t04_acceptance.py`
- `tools/networking/scenarios/fr10_t11_acceptance_manifest.json`

Historical v35 T04/T11 evidence is not relabeled or rewritten by this slice.
