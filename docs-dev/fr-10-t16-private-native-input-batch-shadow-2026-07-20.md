# FR-10-T16 Private Native Input Batch Shadow

Date: 2026-07-20  
Project task: `FR-10-T16`  
Status: default-off production-hook slice implemented and focused tests passing;
task-level acceptance remains open

## Outcome

WORR can now carry a fresh contiguous group of two through eight canonical
input commands in one native command envelope while the established legacy
movement bytes remain present and authoritative. The client uses the bounded
native-input delivery policy to choose the group, retains one immutable batch
until an exact receipt arrives, and retries it without changing its bytes. The
server admits every nested command transactionally and withholds the batch
receipt until every command has joined its exact authoritative legacy command.

This is a private, default-off diagnostic slice. It does not add a q2proto bit,
does not change the public `0x03`, `0x53`, `0x73`, `0x57`, or `0x77` capability
masks, and does not promote native command authority. No file under `q2proto/`
changed.

## Private connection contract

The client derives the read-only `worr_input_batch=1` userinfo request only when
`cl_worr_native_input_batch 1` is selected with the command-only native shadow.
The server must have both `sv_worr_native_shadow 1` and
`sv_worr_native_input_batch 1` before accepting the connection.

After the ordinary readiness `SERVER_ACTIVE` record, the server appends seven
ordered private setting records (`-31770` through `-31764`) in the same packet.
The 63-byte record commits the official connection epoch, private transport
epoch, WNB1 schema/version, maximum command count, and checksum. The client
accepts it only at the exact packet boundary and only when the existing
readiness session, command payload registry, retained-message banks, and TX
gate are clean. Commands finalized or encoded before that commit cannot enter
the batch candidate set.

Readiness needs 135 bytes and the combined readiness/confirmation append needs
198 bytes. If the latter cannot be appended atomically, the server still sends
readiness and declines only WNB1 for that connection. Missing confirmation,
an incompatible peer, a mode mismatch, or a later request mutation therefore
leaves the authoritative legacy path usable.

## WNB1 codec and delivery

`inc/common/net/native_input_batch.h` and
`src/common/net/native_input_batch.c` define the pointer-free WNB1 application
schema carried as schema 2 of a COMMAND WNE record:

- a fixed 32-byte little-endian header;
- two through eight independently valid, exact-contiguous 110-byte WNC1
  command records in one epoch;
- exact payload sizes from 252 through 912 bytes; and
- a CRC32 over the complete payload with the CRC field treated as zero.

Inspect, encode, and decode validate framing, reserved fields, range arithmetic,
outer identity, nested identity, duration bounds, capacity, and overlap. Encode
and decode are all-or-nothing. Decode first obtains the bounded command count
from strict inspection, so a hostile caller capacity (including `UINT32_MAX`)
cannot enlarge its input-derived copy range.

The live client collects only post-confirmation finalized WNC1 records. It asks
the common `native_input_delivery` policy for an exact contiguous selection
that fits the bytes left beside the legacy prefix. One batch may be retained at
a time. A successful transport handoff starts its 100 ms retry clock; retries
reuse the exact retained bytes and stop after eight successful handoffs.
Connection, demo, and pilot teardown clear private batch state. At a map
boundary, the client preserves the drained WNB1 TX tombstone through the short
bootstrap window so proactive old receipts still parse against their real
sequence high-water. The fresh challenge transaction then cancels retained
work, publishes the monotonic transport floor, and clears the old bank before
new confirmation. A prior clean decline is separately re-armed for the next
map. Broader production-model map-rotation telemetry remains open.

The client applies receipts to a separate batch TX bank. A repeated receipt
after release is an idempotent zero-release operation. A delayed receipt for
batch N arriving while batch N+1 is active cannot release or mutate N+1, even
though both use the same private transport epoch.

## Server admission and authority safety

The server accepts a confirmed WNB1 only after strict carrier, envelope, batch,
outer-record identity, and nested-record validation. It then admits the whole
contiguous group into the bounded command-join state as one transaction. No
prefix of a malformed, noncontiguous, replayed, or capacity-rejected batch is
committed.

Legacy command parsing remains the authority boundary. The batch receipt is
blocked while any nested native record is waiting for its matching legacy
record. Exact joins retire the group in order; the final join releases the
receipt. If a client formed the batch from commands sent in earlier legacy
packets, the production parser first reconciles the newly staged WNB1 identities
against the server's bounded authoritative command-stream history, then parses
the current packet. This covers both a minimum batch accumulated across two
packets and newer candidates accumulated while an earlier batch awaits its
receipt. A duplicate native group may refresh only the already-proven pending
receipt. Identity mismatch, sample mismatch, expiry, or a conflicting fresh
group fails/drains the private shadow and never releases an ambiguous receipt.

Before private confirmation, or after a clean capacity decline, schema-2 DATA
is optional unknown traffic: the server exposes only its legacy prefix and
does not apply mixed ACK entries or mutate native transport state. This fallback
also covers a corrupt or too-short WNB body whose surrounding carrier/envelope
framing is valid. Once confirmed, the same malformed body is rejected
transactionally. Schema-1 WNC1 keeps its exact 110-byte requirement in every
state.

## Controls and telemetry

All new controls default to `0`:

- client: `cl_worr_native_input_batch`;
- server: `sv_worr_native_input_batch`;
- client status: `cl_worr_native_input_delivery_status`; and
- per-client server status: `sv_worr_native_shadow_status [slot]`, which now
  includes the `WORR_NATIVE_SERVER_INPUT_DELIVERY_STATUS_V1` row.

The status rows expose request/confirmation/decline state, active and pending
ranges, handoffs, retries, acknowledgements, legacy joins, fallbacks, drains,
and failures without changing connection state.

## Focused verification

The following Windows Clang targets build and pass in the T16 test build:

```text
ninja -C .tmp/build-fr10-t16 \
  native_input_batch_test.exe \
  native_input_batch_layout_cpp_test.exe \
  native_server_shadow_pilot_test.exe \
  native_client_readiness_pilot_test.exe

native_input_batch_test.exe: ok
native_input_batch_layout_cpp_test.exe: pass
native_server_shadow_pilot_test.exe: ok
native_client_readiness_pilot_test.exe: ok
```

The common tests cover minimum/maximum sizes, C and C++ layout, transactionality,
CRC corruption, nested identity corruption, noncontiguous input, boundary and
capacity behavior, and hostile decode capacity. The production-pilot tests add
same-packet confirmation, confirmation-capacity decline, pre-confirm and
declined malformed/short fallback, confirmed malformed rejection, multi-command
legacy join and ACK blocking, disjoint-packet and delayed-ACK authoritative-
history reconciliation, mismatch/expiry quarantine, immutable retry, duplicate
ACK idempotence, delayed cross-batch ACK isolation, old-ACK preservation before
the fresh-challenge cancellation barrier, stale old-ACK isolation after new
activation, and prior-decline parser re-arming on the next map.

After the integrated Windows reconfigure, the client and dedicated engines,
client and dedicated executables, cgame, and sgame all compile and link. No
interactive client or input-initializing runtime was launched for validation.

## Remaining `FR-10-T16` work

This slice advances but does not complete `FR-10-T16`. It does not yet provide
authenticated live received/consumed cursor feedback to the adaptive policy,
native simulation authority, a complete sampling/simulation/render/send cadence
contract, or the common legacy/native fresh-input-age, bounded-loss, bandwidth,
correction, command-flood, load, soak, and supported-platform parent gates.
Production-model ACK-exhaustion and map-rotation telemetry breadth also remains
open. The bounded bootstrap race evidence is detailed in
`docs-dev/fr-10-t16-map-bootstrap-input-batch-ack-cancellation-2026-07-20.md`.
Those remaining gates are prerequisites for task completion and any default-on
or release promotion.
