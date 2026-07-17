# FR-10-T10/T11 Canonical Railgun Historical-Mover Occlusion Acceptance

Date: 2026-07-16

Project tasks: `FR-10-T10`, `FR-10-T11`

Status: bounded acceptance increment complete; both parent tasks remain open.

## Outcome

WORR now has a real-command, two-client Railgun acceptance mode that proves a
sealed historical `SOLID_BSP` mover can authoritatively occlude a target after
the corresponding live mover has moved out of the shot lane.

The accepted client attack follows the normal production path:

`Item::weaponThink` -> `Weapon_Railgun_Fire` -> `fire_rail` ->
`pierce_trace` -> `LagCompensation_TraceLine`.

The fixture does not call a weapon callback, provide a trace result, choose a
damage result, or mutate an edict while the historical query runs. The normal
Railgun pierce callback observes the concrete damage-bearing trace, which
terminates on the generation-matched sealed mover. The target behind it loses
exactly zero health.

## Authority and lifecycle contract

The `railgun-mover-occlusion` mode adds these bounded requirements:

- select one eligible, linked, live `SOLID_BSP` mover with a resolved immutable
  inline-brush asset and at least six retained history samples;
- retain the mover's generation-qualified identity, original origin, linked
  state, and immutable historical pose;
- after the real command is mapped, move only the live fixture mover 96 units
  laterally and relink it;
- require the canonical current-world baseline, with the selected live mover
  omitted for sealed-scene substitution, to remain completely clear;
- require the production Railgun policy `5` query to report the sealed
  historical mover as its canonical historical hit;
- separately require the concrete `fire_rail` pierce-hit callback to observe
  that mover, preventing `P_ProjectSource`'s earlier convergence query from
  satisfying the terminal proof;
- require the live target to remain undamaged with both reported damage fields
  equal to zero;
- require the collision-authority fingerprint to remain unchanged across the
  query; and
- restore the fixture-owned live mover on success, failure, participant
  disconnect/respawn, rearm, shutdown, and map reset.

Mover restoration is fixture lifecycle handling, not rewind-query handling.
The historical trace itself continues to use the immutable collision extension
and never unlinks, moves, relinks, or restores an authoritative edict.

The runtime status schema is
`worr.networking.canonical-weapon-damage-runtime.v28`. Its mover proof fields
are:

- `historical_mover_occlusion_required`
- `historical_mover_relocated`
- `historical_mover_baseline_clear`
- `historical_mover_occlusion_observed`
- `historical_mover_target_undamaged`
- `historical_mover_history_count`

`historical_mover_occlusion_observed` is set only by the production Railgun
pierce-hit observer, not by the preliminary projection trace.

## Implementation

The bounded production acceptance seam is implemented in:

- `src/game/sgame/network/lag_compensation.cpp`
- `src/game/sgame/network/lag_compensation.hpp`
- `src/game/sgame/gameplay/g_weapon.cpp`
- `src/game/sgame/gameplay/g_svcmds.cpp`
- `tools/networking/run_canonical_rail_damage_runtime_gate.py`
- `tools/networking/test_lag_compensation_canonical_rail_contract.py`
- `tools/networking/test_run_canonical_rail_damage_runtime_gate.py`

The server command is
`worr_rewind_canonical_rail_mover_occlusion_arm`. It can only stage the
isolated fixture and cannot synthesize client input or directly invoke the
Railgun.

No wire format, legacy Q2 compatibility path, demo format, `q2proto/` source,
client prediction rule, or cgame presentation path changed in this increment.

## Headless runtime evidence

Report:
`.tmp/networking/canonical-rail-mover-occlusion-runtime-v2.json`

Run ID: `20260716T161549.903598Z-38848`

| Repeat | Applied age | Mover history | Current baseline | Rail pierce mover hit | Target damage |
|---|---:|---:|---|---|---:|
| 1 | 56 ms | 64 | clear | yes | 0 |
| 2 | 56 ms | 64 | clear | yes | 0 |
| 3 | 56 ms | 64 | clear | yes | 0 |

All three runs also report canonical command scope, genuine attack receipt,
normal weapon callback, policy `5`, no fallback, and unchanged current
collision authority. The server, shooter, and target process trees were all
terminated by the gate.

Every client launch used an isolated runtime plus `win_headless=1`,
`in_enable=0`, `in_grab=0`, disabled audio, hidden/no-window process creation,
detached stdin, and kill-on-close process-tree cleanup. No visible client,
input initialization, or mouse capture was used.

## Validation

- `python tools/networking/test_run_canonical_rail_damage_runtime_gate.py`:
  41 tests passed.
- `python tools/networking/test_lag_compensation_canonical_rail_contract.py`:
  47 tests passed.
- Windows production compile for client engine, dedicated engine, client,
  dedicated server, sgame, and cgame: passed.
- `meson test -C builddir-win --no-rebuild --print-errorlogs`: 142/142 passed.
- `python tools/test_package_assets.py`: 16/16 passed.
- `.install` refresh and Windows x86-64 payload validation: passed with 16 root
  runtime files, one runtime dependency, 461 packed assets, 31 botfiles, and
  215 RmlUi assets.

During staging, an external image lock denied deletion of
`.install/worr_engine_x86_64.dll` even though no WORR process owned it. The
stager now retains a deletion-denied runtime only when its SHA-256 exactly
matches the requested build artifact; a mismatched locked destination still
fails closed. This allowed the canonical `.install` workflow to complete
without terminating an unknown process or accepting a stale binary.

## Remaining scope

This increment does not complete either parent task. Remaining acceptance work
includes:

- live shots with moving shooters, moving targets, and riders;
- continuous translation/rotation interpolation and multiple simultaneous
  movers;
- doors, trains, platforms, broader BSP/BSPX geometry, and complex maps;
- piercing multi-target ordering and mover/target lifecycle transitions;
- zero/low/high-latency fairness, abuse, CPU, memory, and query-count budgets;
  and
- cross-platform, soak, rollout, rollback, and release-promotion evidence.
