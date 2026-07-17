# FR-10 T04/T06 Native Snapshot Semantic Admission

Date: 2026-07-16

Project tasks: `FR-10-T04`, `FR-10-T05`, `FR-10-T06`, and `FR-10-T07`.
This foundation also constrains later `FR-10-T08`, `FR-10-T13`, and
`FR-10-T16` integration.

## Continuation

The production server-TX/client-RX adapter continuation is now implemented
and validated in
`docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`.
That later slice binds an independent snapshot epoch through readiness,
connects the exact final-emission server projection to the real server and
client netchan hooks, gives the native receiver sole cgame timeline
publication ownership for the private epoch, and proves the reverse semantic
ACK path under deterministic loss. The Stage D design and validation below
remain the semantic foundation and historical evidence for that continuation.

## Outcome

WORR now has a bounded Stage D native snapshot transaction that carries one
complete reconstructed canonical snapshot through the existing WNC1 codec,
WNE1 fragmentation, WTC1 DATA carrier, retained receive state, semantic
projection validation, and the cgame snapshot timeline.

Transport receipt alone cannot acknowledge a snapshot. The client may publish
the transport ACK only after all of the following are true:

1. The complete message is bound to the expected connection and snapshot
   epochs and the private canonical-snapshot capability.
2. WNC1 decodes transactionally into a fully valid transient canonical view.
3. The exact snapshot ID and five independently reconstructed cross-endpoint
   proofs match the server/legacy-shadow expectation: legacy parity plus the
   player, entity, area, and event semantic hashes.
4. Cgame consumes the immutable view and returns a fresh V2 receipt for the
   exact snapshot, including the decoded native endpoint hash.
5. The cgame event runtime confirms the same snapshot fence is healthy and
   does not require authority resynchronization. Legacy-inferred event
   references must pass this fence even with `cg_event_runtime_audit=0`.

The implementation is intentionally a foundation rather than a production
authority cutover. Legacy q2proto snapshot delivery, rendering, effects,
audio, demos, and presentation remain authoritative. The public capability
offer is unchanged and `q2proto/` was not modified.

## Architecture

The design keeps the useful idTech3 separation between server final-state
construction, transport, client reconstruction, and cgame-owned presentation,
while replacing implicit mutable coupling with versioned pointer-free records
and explicit semantic commit boundaries.

The server side still owns the final per-peer snapshot view. The native
transport owns only opaque canonical bytes, fragmentation, retention, retry,
and carrier receipts. The admission owner binds those bytes to independently
observed canonical semantics. Cgame owns the immutable timeline and supplies
the final consumer receipt. Local prediction and reconciliation remain
downstream of the same timeline instead of a parallel network-state model.

This yields a hybrid architecture:

- idTech3-style separation of server snapshot construction, client game state,
  prediction, and presentation;
- modern typed and versioned C ABIs at module boundaries;
- allocation-free, caller-owned transaction storage;
- per-datagram and whole-message CRC validation;
- explicit connection, transport, snapshot, and semantic epochs;
- semantic ACK authority rather than transport-only acknowledgement;
- fail-closed quarantine and keyframe recovery.

## Wire and codec bounds

The WNE1 wire header and 1,200-byte datagram ceiling are unchanged.

The opaque message ceiling is now 131,072 bytes across at most 128 fragments.
This accommodates every legal snapshot V2 projection, including:

- the 65,509-byte maximum 512-record snapshot with no event references;
- the formerly rejected 65,539-byte maximum 512-record snapshot with one event
  reference;
- the full legal 80,869-byte projection with 512 event references.

The 512-record bound is a projection-count and decode-storage limit, not an
entity-identity namespace. Live legacy protocol projections may name entity
indices in the exclusive range `[0, 1024)`, and Rerelease projections may use
`[0, 8192)`, while any one canonical snapshot still carries at most 512 entity
records. Focused coverage admits high identities 1023 and 8191 with small
record counts.

`Worr_NativeCodecSnapshotDecodeProjectionV1` validates the complete wire image,
preflights caller capacities and disjoint output regions, calculates semantic
hashes during the validation pass, and only then writes the transient
projection. A failure therefore leaves every caller output unchanged.

The projection exposes six hash domains, but they have two distinct roles:

- `endpoint_hash` is endpoint-local. It includes chronology/provenance details
  that can legitimately differ between the independently reconstructed legacy
  expectation and the decoded native final-emission view. Admission validates
  it locally against the fresh cgame receipt after native consumption.
- Five hashes are cross-endpoint admission proofs: legacy parity, semantic
  player, semantic entity, semantic area, and semantic event.

## Full-view baseline contract

WNC1 snapshot V2 encodes a complete reconstructed view. A nonzero `base_id`
records lineage but is not a decode dependency. The receiver does not need a
retained base snapshot to decode the current WNC1 message.

This distinction is deliberate. If WORR later adds a true delta snapshot
codec, it must use an explicit codec revision with a retained-base
reconstruction and invalidation contract. It must not silently reinterpret the
current `base_id` field as an implicit delta dependency.

## Semantic admission transaction

`worr_native_snapshot_admission_state_v1` owns the pointer-free connection and
snapshot-epoch state. `Worr_NativeSnapshotAdmissionCommitCompletedV1` performs the
following transaction:

1. Validate binding, private capability, complete-message provenance,
   capacities, and mutation budgets.
2. Decode and validate the complete WNC1 projection in caller-owned scratch.
3. Compare the exact snapshot ID and five cross-endpoint parity/semantic
   hashes with the independent expectation.
4. Stage native session, retained slots, and receipt-ledger mutations on local
   copies.
5. Submit the immutable projection to the cgame consumer.
6. Read a fresh `worr_cgame_snapshot_timeline_status_v2`.
7. Require exact generation, identity, snapshot hash, time, decoded native
   endpoint/parity hashes, timeline receipt, and healthy event-fence receipt.
8. Commit the staged transport state and publish the snapshot ACK.

The generic retained-message commit path refuses snapshot, event, and
event-stream descriptor semantic classes. These classes can only gain ACK
authority through their semantic admission paths.

## Cgame receipt ABI V2

The snapshot timeline export is now
`WORR_CGAME_SNAPSHOT_TIMELINE_EXPORT_V2` with API version 2. The status record
adds:

- `admission_generation`;
- `last_snapshot_id`;
- `last_snapshot_hash`;
- `last_event_fence_result`;
- `receipt_flags`.

The version change is required because the status structure grew. A V2 export
prevents a mixed old engine/new cgame or new engine/old cgame pairing from
accepting incompatible buffer sizes.

Cgame increments `admission_generation` only after the canonical timeline
publish and event-runtime observation both succeed. A zero-event snapshot does
not bypass the fence: the event runtime must still report the current snapshot
epoch healthy and free of an authority-resync requirement.

`cg_event_runtime_audit` gates only retention and presentation of the optional
legacy body-comparison journal. Snapshot observation always validates the
complete legacy-inferred reference range, including structure, dense ordering,
semantic revision, and the exact event-range hash. With audit disabled it
advances snapshot chronology and returns a successful fence without allocating
from the fixed 2,048-entry legacy join table. This keeps correctness
audit-independent and prevents sustained legal 512-reference snapshots from
exhausting diagnostic storage. Authority-provenance references still require
the matching active authority lifecycle and fail closed when it is unavailable.

## Retry, uncertainty, and recovery

An exact `ALREADY_COMMITTED` repeat revalidates the current cgame V2 receipt
before rearming its ACK. It does not submit or consume the snapshot again.

Any uncertainty that can invalidate semantic authority clears the consumer,
retires snapshot/event/descriptor receipts, quarantines the current snapshot
epoch, and arms a keyframe requirement. Examples include:

- missing or stale cgame receipt;
- wrong snapshot or connection epoch;
- projection identity or hash mismatch;
- unhealthy event fence;
- consumer reset or callback uncertainty;
- semantic mutation exhaustion.

Command receipt authority is not retired by this snapshot-specific semantic
reset.

## Production-shaped virtual link

`native_snapshot_virtual_link_test` starts from actual
`SV_SnapshotShadowViewV1` final-emission data and terminates at the real
`CG_GetCanonicalSnapshotTimelineAPI` plus `CG_EventRuntimeObserveSnapshot`
implementation rather than a hand-authored snapshot or fake cgame consumer.
The cgame audit cvar remains disabled. It proves:

- an event-carrying keyframe crosses reverse-order multi-fragment WNE1/WTC1
  delivery and commits with exact five-proof cross-endpoint parity plus an
  exact local native endpoint receipt;
- a base-referenced complete update also commits without requiring retained
  base decode state;
- a lost ACK causes exact repeat validation and ACK rearm without duplicate
  cgame consumption;
- an endpoint-only expectation difference remains admissible because endpoint
  chronology/provenance is local, while a legacy-parity or semantic mismatch
  cannot ACK and leaves session, retained slots, receipt ledger, and cgame
  state uncommitted.

This is production-shaped in-process evidence. It does not yet connect the
live server final-emission TX hook to the live client RX/timeline hook.

## Validation

All validation remained headless and input-free. No interactive client,
renderer, input initialization, or mouse capture was launched.

- Focused Stage D gate: 6/6 passed.
- Complete Meson networking suite: 147/147 passed.
- Full Windows production build: passed.
- Refreshed Windows x86-64 `.install/`: validated.
- Staged runtime: 16 root runtime files and one root dependency.
- Packaged assets: 483 files, 31 botfiles, and 215 RmlUi assets.
- Q2AAS reference maps: 1.

The focused coverage includes native codec, snapshot admission, server-view
virtual link through the real cgame consumer, cgame event-runtime health,
semantic carrier ACK, and envelope tests, plus C/C++ ABI layout tests in the
complete suite. The event-runtime regression accepts five consecutive legal
512-reference audit-off snapshots—2,560 references total—without consuming
the 2,048-entry diagnostic join table or recording a capacity failure. It also
proves a corrupted event hash and authority-provenance refs without an active
authority stream cannot advance snapshot chronology.

## Remaining work

The following work remains open and prevents `FR-10-T04`, `FR-10-T05`,
`FR-10-T06`, and `FR-10-T07` completion:

- native snapshot authority and public capability advertisement;
- at least 100,000 serialized-datagram parity comparisons;
- recovery, bandwidth, CPU, memory, load, soak, and 1/8/16/32-client budgets;
- malformed-input and supported-platform release matrices;
- demo/MVD/GTV/spectator preservation;
- remaining event service families and predicted local-action presentation;
- event presenter and rendering authority cutover;
- any future explicit delta snapshot codec and retained-base contract.

## Roadmap status

Roadmap completion: 74/190 (38.9%), 116 open.

FR-10 completion: 3/16 (18.75%), 13 open.
