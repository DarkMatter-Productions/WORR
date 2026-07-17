# FR-10-T08 independent off-hand interaction model

Date: 2026-07-16  
Project task: `FR-10-T08`  
Status: integrated shadow foundation; authoritative lifecycle delivery remains
intentionally open.

## Decision

Off-hand Hook is not a selected-weapon attack. It now has a small independent
transaction model rather than being forced through the selected-weapon
`local_action_v2` path. This keeps the future catalogue predictor from
conflating weapon cadence, ammo, and presentation with a mapped off-hand
input.

The client retains the exact canonical `worr_command_record_v1` at command
finalization in the command-identity owner. That retention is unconditional
once canonical identity is live; it does not depend on the optional
native-readiness pilot. The stored record uses the same cumulative
sample-clock builder as the native shadow and therefore includes the command
ID, sample time, model revision, canonical payload, and explicitly absent
render watermark. It is immutable after retention.

The existing movement input import is unchanged. A separate versioned
`WORR_CGAME_COMMAND_RECORD_IMPORT_V1` exposes a bounded, value-only range of
these exact records to cgame. This prevents cgame from inventing sample time,
model revision, or watermark provenance from a packet acknowledgement.

## Shadow behavior

`local_interaction_abi.h` defines the Hook-only state, intent, transaction,
and correction contract.

- A predicted rising edge creates a request-pending transaction only.
- Pending state survives later held and release samples until authority
  reconciles it.
- A predicted transaction never declares Hook active.
- An authoritative transaction alone may declare active, persisted, or
  rejected lifecycle state.
- Transactions are keyed by the exact canonical command record and validate
  atomically.

`cg_local_interaction.cpp` invokes this core from normal movement replay only
when canonical records are available. It validates record range metadata,
canonical IDs, and the full movement payload against the already accepted
movement prediction range. A missing, malformed, or bounded-history-missing
record makes this shadow dormant; it does not fall back to packet ACK and does
not affect movement correction.

The first retained record is treated carefully. Command sequence one has a
known zero-state predecessor. For a later range, cgame asks for the immediate
predecessor to carry the held bit across the range boundary. If that record has
aged out, a held first sample is treated as pre-held, suppressing the
unprovable request edge. This is fail-closed and only affects the
diagnostic/shadow request, never server authority.

There is deliberately no collision, damage, attachment, pull, sound, visual,
or event presentation call in this adapter. The existing sgame Hook action and
current-world policy remain authoritative.

## Authority promotion prerequisites

This increment does not claim live client/server Hook reconciliation. Promotion
requires all of the following:

1. Sgame must construct the matching authoritative interaction transaction
   from the command-scoped Hook observation before/after state.
2. A reliable, ordered authority payload must carry that transaction to cgame
   under the existing canonical event/snapshot fences.
3. Cgame must match the authoritative transaction by command ID, classify the
   correction, retire pending state, and record correction telemetry.
4. Presentation can be considered only after transaction parity and
   no-duplicate side-effect evidence pass under loss, reordering, map reset,
   and command-ring-overflow cases.

## Validation

The focused Meson networking tests pass:

```text
network-local-interaction
network-cgame-local-interaction
network-prediction-input-layout-c
network-prediction-input-layout-cpp
```

The focused build also compiles `worr_x86_64.exe`, `cgame_x86_64.dll`, and
`sgame_x86_64.dll`. The full Meson suite passes 141/141. The refreshed
`.install` stage validates 16 root runtime files, one root dependency, 449
packaged assets, 31 botfiles, and 215 RmlUi assets.

These are in-process test executables and build/staging steps only; this
increment launches no game process and therefore does not initialize input or
capture the mouse. The stage check also confirmed that no WORR/Q2 process was
running before refresh.
