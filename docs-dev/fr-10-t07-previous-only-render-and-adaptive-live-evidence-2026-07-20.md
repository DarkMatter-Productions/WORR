# FR-10-T07 Previous-Only Render and Adaptive Live Evidence

Date: 2026-07-20  
Project task: `FR-10-T07`  
Dependency context: `DV-04-T02`

## Decision

This slice closes two previously ambiguous evidence gaps in the default-off
native snapshot presenter:

- a source present only in the older member of an interpolation pair is now
  proven at the real renderer submission boundary, rather than inferred from
  identity-union enumeration; and
- the live gate now proves that the adaptive interpolation controller is
  enabled, receives real arrival observations, raises its delay above the
  configured baseline, remains within its configured ceiling, and reports no
  controller failures.

This is bounded `FR-10-T07` progress, not task closure. Raw legacy audiovisual
ownership, the `DV-04-T02` per-carrier cutover inventory, the broad fault/rate/
pause/demo/entity-reuse matrix, visual/audio parity, and performance budgets
remain open. The FR-10 project therefore remains **8/16 complete**.

## Implementation

The value-owned canonical render view retains a byte classification alongside
each selected source. The classification is cleared at frame start and on
every reset/failure path. A previous-only source advances three cumulative,
saturating counters in order:

1. `observed` when it exists in the previous/current identity union but not in
   the current snapshot;
2. `selected` only when timeline sampling still selects it as visible before
   the current endpoint; and
3. `submitted` only after `V_AddEntity` is called for that source.

Production assertions and focused tests share the invariant
`submitted <= selected <= observed`. The status line is:

```text
previous_only=<observed>/<selected>/<submitted>
```

The native snapshot presentation gate now parses and validates that triplet.
It captures a healthy row immediately before the fixture action, requires the
same presentation epoch afterward, requires a positive phase-local delta for
all three values, and applies the ordering invariant to that delta. This
prevents an unrelated startup/team transition from satisfying the Blaster
action's proof. The final `observed` value must also match the existing
removed-identity total.

The same gate now parses the existing adaptive status row. Its launch contract
explicitly seals:

```text
cg_snapshot_timeline_adaptive_interpolation=1
cg_snapshot_timeline_interpolation_delay_ms=50
cg_snapshot_timeline_max_interpolation_delay_ms=150
cg_snapshot_timeline_max_extrapolation_ms=50
```

Acceptance requires a nonzero observed cadence, at least one rise adjustment,
a current delay in `(50, 150]` milliseconds, at least one lifecycle reset, and
zero adaptive failures. The artifact schema advances to
`worr.networking.canonical-weapon-damage-runtime.v38` and records the render and
adaptive rows separately.

## Focused verification

```text
meson compile -C builddir-win \
  cgame_x86_64 cgame_canonical_render_entities_test snapshot_timeline_test
  pass

meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-cgame-canonical-render-entities \
  network-snapshot-timeline \
  network-canonical-snapshot-render-policy-contract
  3/3 pass

python tools/networking/test_run_canonical_rail_damage_runtime_gate.py
  90 tests pass
```

The focused cases include exact visibility immediately before the current
endpoint, disappearance at equality, classification scrubbing, evidence-order
bounds, and controlled-player prediction ordering ahead of canonical remote
transform selection.

## Headless live verification

The live parent used isolated runtime homes, `win_headless=1`, disabled input,
and a hidden presentation cadence. No visible client, device input, or mouse
capture was used.

```text
python tools/networking/run_canonical_rail_damage_runtime_gate.py \
  --client-exe .tmp/networking/t07_probe_stage_v45/worr_x86_64.exe \
  --dedicated-exe .tmp/networking/t07_probe_stage_v45/worr_ded_x86_64.exe \
  --working-dir .tmp/networking/t07_probe_stage_v45 \
  --output .tmp/networking/fr10_t07_snapshot_previous_adaptive_checkpoint_v04_phase_repeat3.json \
  --port 28786 --repeat 3 --timeout 60 \
  --weapon blaster-native-snapshot-presentation
  pass
```

| Repeat | Pre-action | Post-action | Phase delta | Adaptive delay | Rise delta | Failures |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | `0/0/0` | `2/2/2` | `2/2/2` | 59,242 us | 4 | 0 |
| 2 | `0/0/0` | `3/3/3` | `3/3/3` | 57,781 us | 5 | 0 |
| 3 | `0/0/0` | `2/2/2` | `2/2/2` | 67,071 us | 6 | 0 |

Every repeat also reported zero clock, pair, alignment, sampling, parity,
native-authority, event-audit, enumeration, and adaptive failures. Process
cleanup left no client or dedicated-server process running.

Evidence hashes:

- `cgame_x86_64.dll`:
  `D79881C6C34780AD53E2BF1B9454FADB7E4D0A7035147F7214EE99ABAC744F10`
- staged client used for this slice:
  `2143BB6D95981B275E408369D016DFA7CEB03C5BBC9F9BF522F166215A00ABAE`
- staged dedicated server used for this slice:
  `9E6D5BBE70581160FD40A343ABCF1ED778234935ACF2D1EE373FB993F41AA921`
- phase-local repeat-three JSON artifact:
  `13395CA8143670A3AAC8A79AA7B5B9C60E0F3CE31651D06B13AFABE52F7E03D5`

## Remaining acceptance work

This evidence proves one ordinary live cadence on Windows. It does not stand
in for the required loss, duplication, reordering, rate, pause/timescale, map,
demo-seek, entity-reuse, resource-pressure, visual/audio parity, load, or
supported-platform parents. Those rows remain mandatory before `FR-10-T07`
can move from **In Progress** to complete.
