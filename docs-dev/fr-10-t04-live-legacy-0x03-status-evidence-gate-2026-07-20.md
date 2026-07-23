# FR-10-T04 Live Legacy 0x03 Status Evidence Gate

Date: 2026-07-20  
Project task: `FR-10-T04`

## Purpose

The exact-bundle acceptance parent previously declared the default legacy
bundle (`0x03`) but had no direct live client/server numeric observation for
it. Launch flags and the absence of a native readiness endpoint are not proof
of the tuple that both peers actually confirmed.

This slice adds read-only status surfaces and a fifth real-UDP, headless live
row. It does not add a capability, alter negotiation, enable native authority,
or change `q2proto/`.

## Implementation

- `cl_worr_capability_status` prints the validated client capability state:
  phase, connection epoch, protocol, offered, locally supported, peer
  supported, and negotiated masks.
- `sv_worr_capability_status [slot]` prints the corresponding server-owned
  tuple plus confirmation/failure state, legacy command-parser readiness, and
  explicit absence of the optional native-shadow/input-batch endpoints.
- Canonical runtime mode `blaster-legacy-capability-status` keeps all optional
  native endpoint cvars disabled. After an ordinary production Blaster action,
  it requires both hidden input-free clients and both server peers to publish
  exact `0x03` tuples with two distinct nonzero epochs that match across the
  process boundary.
- The `FR-10-T04` parent now has literal live rows for `0x03`, `0x53`, `0x73`,
  `0x57`, and `0x77`. Full evidence rejects either missing direct legacy status
  or missing direct event status.

The live row remains input-safe: both clients use `win_headless=1`,
`in_enable=0`, `in_grab=0`, and `s_enable=0`; the dedicated process never
launches a client.

## Focused verification

The parser/contract layer passes before the combined production rebuild:

```text
python tools/networking/test_run_canonical_rail_damage_runtime_gate.py
  56/56 pass
python tools/networking/test_run_fr10_t04_acceptance.py
  11/11 pass
```

The production binary build, `.install` refresh, exact live `0x03` child, and
full five-row parent are intentionally pending the shared schema-v36/T13/T16
integration build. No live result is claimed by this document until those
commands pass from the refreshed staged binaries.

## Remaining FR-10-T04 scope

This closes only the direct live numeric legacy-mask gap after final runtime
verification. The supported legacy demo/MVD/relay compatibility matrix,
complete service-family adapters, authority promotion, full impairment and
multi-client breadth, ACK/map-rotation breadth, load/soak, supported-platform,
rollout, and release evidence remain open. `FR-10-T04` therefore remains
partial and the project count remains 8/16.
