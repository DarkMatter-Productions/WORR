# FR-10-T16 Native Input Delivery Policy Core

Date: 2026-07-20  
Project task: `FR-10-T16`  
Status: bounded policy core implemented and tested; a separate private
production-hook WNB1 slice now consumes it, while server cursor feedback and
parent acceptance remain open

## Outcome

WORR now has an allocation-free common policy for selecting exact canonical
command identities for a native input batch. The policy closes the
previous gap between the existing adaptive controller and the one-command
native observation path: it can plan multiple never-sent commands plus a
bounded, adaptive set of due retransmissions without consulting engine globals
or redefining command identity.

This core is not itself a wire format or authority promotion. The subsequent
private WNB1 pilot binds it to a default-off two-to-eight-command production
hook, documented in
`docs-dev/fr-10-t16-private-native-input-batch-shadow-2026-07-20.md`. Legacy
`MOVE`/`BATCH_MOVE` remains authoritative. No file under `q2proto/` changed.

## Contract

`inc/common/net/native_input_delivery.h` defines fixed-width V1 records for:

- planner configuration and conservative datagram accounting;
- exact received and consumed server cursors;
- caller-owned per-command successful-transmit history;
- selected command identities and attempt ordinals;
- a bounded eight-command plan; and
- persistent monotonic-feedback state with saturating telemetry.

The implementation in `src/common/net/native_input_delivery.c` accepts at most
64 candidates and emits at most eight selections under a 1,200-byte ceiling.
Candidates must begin exactly at `received_cursor + 1`, remain contiguous and
ordered in one reset-local epoch, and carry nondecreasing sample times. Server
received/consumed cursors and feedback time cannot roll back. These rules make
acknowledgement provenance explicit and reject ranges that could silently
reinterpret an already acknowledged command.

The default budget charges every selected command for its current WTC DATA
entry and WNE envelope header, plus one shared WTC footer. This conservative
standalone-record accounting fits six exact WNC1 commands under 1,200 bytes.
The private WNB1 adapter configures its measured shared/nested overhead and may
use the eight-selection storage ceiling. This is planner accounting only; the
codec and its private confirmation remain separate contracts. A compatibility
carrier must pass the bytes remaining after its legacy prefix, not assume the
full 1,200-byte default is available to the native trailer.

## Selection policy

Every valid configuration must fit at least two commands. The planner uses
those slots deliberately:

1. It reserves one for the newest never-sent command, so recovery work cannot
   indefinitely age the newest sampled input.
2. It reserves one for the exact contiguous receive frontier when that command
   is never sent or its retry is due, so newest-first delivery cannot leave a
   permanent acknowledgement gap.
3. It fills remaining capacity with never-sent commands from oldest to newest.
4. It then selects only due, non-exhausted retransmissions from oldest to
   newest, capped by both the adaptive controller's redundancy decision and
   the local policy maximum.

The returned selections are sorted by canonical command identity even though
freshness and frontier reservations are decided first. Each selection records
its original candidate index and exact next attempt ordinal. Initial sends do
not consume the adaptive redundancy allowance. Retry interval, transmit count,
batch count, candidate count, and wire bytes all have hard limits.

The planner never marks a transmission successful. The caller retains command
payloads and updates transmit history only after a successful transport
handoff. Failed planning leaves the output byte-identical; only valid state's
saturating attempt/rejection telemetry can advance. Valid empty decisions
produce an explicit `NOTHING_DUE` plan rather than fabricating traffic.

## Deterministic verification

The following focused Windows Clang checks pass:

```text
meson compile -C builddir-win native_input_delivery_test \
  native_input_delivery_layout_cpp_test
meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-native-input-delivery \
  network-native-input-delivery-layout-cpp
```

The C test covers fresh batching, newest/frontier reservation, exact cursor
feedback, adaptive retry depth, retry-not-due and exhausted cases, exact retry
boundary behavior, wire and batch caps, empty plans, rollback/gap/malformed
rejection, output overlap, the 64-candidate maximum, sequence ceiling, and
byte-identical replay from a reset state. The C++20 layout test fixes every
record size and important nested offsets while proving standard-layout and
trivially-copyable records. Both Meson rows pass.

## Remaining `FR-10-T16` work

This core does not complete the task. Remaining work includes:

- produce authenticated server received/consumed feedback on the native path
  and reset the planner across every connection/epoch lifecycle;
- prove duplicate native records remain idempotent if native intake is ever
  promoted from observational shadow to authority;
- complete the explicit sampling/simulation/render/send cadence contract;
- run common legacy/native input-age, bounded-loss recovery, bandwidth,
  correction, command-flood, load, soak, and supported-platform parent gates;
  and
- keep all native authority and rollout controls default-off until their
  dependency and release gates pass.
