# Canonical Event ABI and Bounded Journal Core

Task: `FR-10-T05`

Date: 2026-07-12

Status: core slice implemented and tested. Subsequent legacy producer and
client/cgame audit adapters are live; canonical presentation and transport
retention remain before `FR-10-T05` is complete.

## Subsequent Integration Status

This document preserves the original core-slice design and evidence. Later
2026-07-12 work added a named optional sgame extension that shadows final
authoritative legacy entity events into an engine-owned 4096-record canonical
journal. A separate V2 engine-to-cgame audit range captures typed temporary
entities, player and monster muzzle flashes, normalized spatial sounds, and
accepted-frame entity events in legacy decode order with bounded provenance and
failure atomicity. Those ranges can accompany parity-qualified canonical
snapshot views, but legacy event presentation remains authoritative. No
`q2proto/` source was changed. Direct multi-event producers, unified lifecycle
propagation, prediction correlation, canonical presentation, negotiated
retention, and impairment/load promotion gates remain open.

## Outcome

WORR now has a transport-neutral canonical event ABI and a deterministic,
caller-owned journal core. The core is deliberately below cgame, sgame, the
legacy protocol adapters, and the future WORR transport. Those consumers can
therefore converge on one event identity, validation, matching, ordering, and
receipt model instead of defining parallel wire-specific schemas.

The implementation is in:

- `inc/shared/event_abi.h` and `src/common/net/event_abi.c` for pointer-free
  records, validation, hashing, stream IDs, and selective receipt state;
- `inc/common/net/event_journal.h` and
  `src/common/net/event_journal.c` for the bounded runtime journal;
- `tools/networking/event_journal_test.c` for behavioral and fault coverage;
- `tools/networking/event_schema_layout_c.c` and
  `tools/networking/event_schema_layout_cpp.cpp` for C11/C++20 schema checks.

No `q2proto/` source or live legacy wire format changes are part of this slice.

## Canonical Record

`worr_event_record_v1` is a 168-byte, pointer-free record with an 80-byte fixed
payload. Its schema and movement-independent event model revisions are
explicit. It records:

- authoritative identity as `(stream_epoch, sequence)`;
- deterministic prediction provenance as
  `(command_epoch, command_sequence, emitter_ordinal, lane)`;
- source simulation tick and microsecond simulation time;
- source ordinal, stable source entity, and optional subject entity;
- stable event type, delivery class, prediction class, flags, and expiry tick;
- a known payload kind, exact payload length, and bounded payload bytes.

The two identities are intentionally independent. An authoritative ID defines
global stream order and duplicate suppression. A prediction key correlates a
deterministically reproduced client event with later authority. It never acts
as an authoritative stream ID.

Epoch zero and sequence zero are reserved. The first authoritative identity is
`{1, 1}`. Advancing `UINT32_MAX` sequence produces sequence one in the next
epoch. Epoch exhaustion fails instead of reusing an identity. This avoids
ambiguous modular ordering inside an epoch.

Entity slot indices are not identities. Every present reference includes a
non-zero generation. Only `{UINT32_MAX, 0}` represents an absent optional
subject. This prevents a recycled entity slot from matching an event produced
by its previous occupant.

## Validation and Hashing

The validator fails closed before a record can enter the journal. It checks:

- exact structure size, ABI version, event model revision, and zero reserved
  bits/fields;
- known event, delivery, prediction, prediction-lane, and payload enums;
- authoritative-ID presence rules and reserved epoch/sequence values;
- non-zero deterministic command epoch/sequence for command-derived events;
- source ordinal agreement with the prediction emitter ordinal;
- source and subject generation/index bounds;
- delivery-class expiry rules using an unambiguous half-range tick lifetime;
- exact payload-kind size, event-type compatibility, and a zero payload tail;
- finite vectors and scalar values, plus payload-specific value bounds.

Unknown records are rejected in this first schema, including unknown cosmetic
records. A later negotiated transport may define a length-validated skip rule
for non-critical cosmetics, but it must not weaken canonical validation for
known records or allow unknown reliable/critical events through.

`Worr_EventRecordHashV1` uses the correct FNV-1a 64-bit offset basis
`14695981039346656037`, explicit domain tags, little-endian scalar appends, and
fieldwise payload hashing. It never hashes C padding or native pointers.
Finite negative zero is normalized to positive zero. Prediction keys have a
separate hash domain. The tests pin exact record and prediction-key goldens so
schema or hash drift is visible. After the keyed-POI catalog addition advanced
the event model to revision 2, the fixture goldens are
`17999893636270564749` for the full record and `11232163421548043143` for its
prediction key.

## Journal Semantics

`worr_event_journal_v1` owns no memory. The caller supplies a fixed array of
`worr_event_journal_slot_v1`; initialization and every operation are bounded
and allocation-free. There is no wall-clock access, random source, renderer
state, or other hidden input.

The first implementation uses full-tuple linear scans. This makes hash
collisions irrelevant to correctness and keeps the initial storage contract
simple. `FR-10-T14` must profile real event populations before replacing these
scans with a bounded index. If an index is added, retained tuples remain the
authority for collision-safe equality.

Authoritative insertion provides:

- exact-ID duplicate suppression and conflict rejection;
- receipt-window duplicate suppression after a terminal slot is reclaimed;
- arrival-order-independent, ascending authoritative enumeration;
- prediction-key matching with full semantic comparison;
- persistent-state supersession by newer authoritative sequence;
- explicit stale persistent revision handling;
- deterministic reclaimable-terminal slot reuse; the newest persistent state
  remains retained until superseded or canceled.

Predicted insertion provides:

- separate prediction-only records with no authority-ID flag or value;
- duplicate suppression by full prediction provenance;
- full semantic conflict detection when a key is reused incorrectly;
- matching whether prediction or authority arrives first;
- preservation of `presented`, `expired`, and `canceled` terminal state across
  a later authority match, preventing double presentation;
- immediate and deferred presentation eligibility.

Repeated events from the same entity at the same tick and simulation time are
distinct when their source/emitter ordinals differ, even when type and payload
are identical.

Client-facing slot metadata is explicit: `received`, `predicted`, `matched`,
`presented`, `expired`, and `canceled`. Slot references also have generations,
so reclamation invalidates old references.

## Delivery-Class Capacity Policy

Overflow behavior is deterministic and class-aware:

- cosmetic events may replace an unpresented cosmetic with the same coalescing
  key; otherwise the incoming event is explicitly dropped;
- transient events are explicitly dropped when no terminal slot is available;
- reliable ordered events fail closed on exhaustion;
- persistent state replaces an older retained revision of the same state key,
  but fails closed if unrelated reliable state exhausts capacity;
- reclaimable terminal slots are selected oldest-resident first; a presented
  persistent-state revision is retained until it is superseded or canceled.

A dropped event is not marked received. This matters because a receipt means
canonical storage/processing, not presentation. Capacity failure therefore
cannot accidentally acknowledge data the client did not retain.

The API is capacity-parametric. Proposed starting budgets remain 4096 global
server events, 1024 per-client references, 2048 authoritative client events,
and 256 predicted client events. They are not hard-coded here and require
`FR-10-T14` measurement before release defaults are selected.

## Receipt Acknowledgement

`worr_event_receipt_ack_v1` contains exactly the active epoch, highest
contiguous received sequence, and a 64-bit selective mask. Bit zero describes
the event immediately after the contiguous prefix. Out-of-order receipt sets a
bit; filling a gap collapses the prefix and shifts the mask. Events farther
than 64 positions beyond the prefix fail without mutation.

This is a canonical receipt primitive, not a resend scheduler or presentation
acknowledgement. The journal makes an authoritative reliable event ineligible
for presentation while its sequence is above the contiguous receipt prefix;
both the eligibility query and presentation transition enforce that gate.
Consumers still walk the ascending storage view from the exact next sequence.
The future transport adapter owns retransmission, pacing, and encoding of this
same receipt state.

While a record is retained, replaying its authoritative ID with different
content is a hard conflict. Once a terminal record has been reclaimed, the
receipt prefix/mask still suppresses that ID but intentionally carries no
content hash, so it cannot diagnose a changed payload on an ancient duplicate.
Transport authentication and replay-window policy remain adapter concerns.

## Deterministic Test Coverage

The ordinary networking tests cover:

- loss holes, duplicate packets, reorder, late gap fill, and global sequence
  enumeration;
- selective acknowledgements, 64-entry window failure, wrong epochs, forward
  epoch reset, sequence wrap, and epoch exhaustion;
- prediction before authority, authority before prediction, immediate and
  deferred presentation, and at-most-once presentation;
- same-tick/same-payload predicted events with distinct ordinals;
- prediction-key collision with a different payload as a hard conflict;
- entity slot reuse with different generations;
- capacity and capacity-plus-one behavior for transient and reliable classes,
  cosmetic coalescing, terminal reclamation, and persistent supersession;
- simulation-tick expiry across `uint32_t` wrap;
- invalid versions, revisions, flags, reserved bytes, identities, enums,
  entity references, prediction provenance, expiry, payload sizes/tails, and
  non-finite payload values;
- fixed fieldwise hash goldens and C11/C++20 layout parity.

The standalone strict-warning checks used while developing this slice were:

```text
clang -std=c11 -Wall -Wextra -Wpedantic -Iinc \
  src/common/net/event_abi.c src/common/net/event_journal.c \
  tools/networking/event_journal_test.c -o .tmp/event-core/event_journal_test.exe
.tmp/event-core/event_journal_test.exe
clang -std=c11 -Wall -Wextra -Wpedantic -Iinc \
  tools/networking/event_schema_layout_c.c \
  -o .tmp/event-core/event_schema_layout_c.exe
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Iinc \
  tools/networking/event_schema_layout_cpp.cpp \
  -o .tmp/event-core/event_schema_layout_cpp.exe
```

Meson exposes the same checks as ordinary `networking` suite tests.

The isolated Meson validation for this slice was:

```text
meson setup .tmp/build-event-core --buildtype=debug
meson compile -C .tmp/build-event-core event_journal_test \
  event_schema_layout_c_test event_schema_layout_cpp_test
meson test -C .tmp/build-event-core network-event-journal \
  network-event-schema-layout-c network-event-schema-layout-cpp \
  --print-errorlogs
```

All three tests passed for three consecutive repetitions (9/9) on the Windows
Clang C11/C++20 build. At this slice revision there was no live adapter;
subsequent integration is summarized above. Full canonical-presentation,
transport-retention, impairment, and release gates remain open.

## Remaining `FR-10-T05` Work

This core does not complete the task. Current remaining work is:

1. Expand the authoritative producer surface beyond the landed legacy-event
   shadow and assign exact source ordering for direct multi-event producers.
2. Propagate one authoritative lifecycle generation model through server
   snapshots and client event ranges.
3. Connect command-derived cgame/sgame events to deterministic prediction keys
   and prove weapon/effect reconciliation without duplicate presentation.
4. Promote immutable canonical event iteration into presentation while keeping
   a tested rollback path.
5. Add reliable resend/retention policy and carry receipt state in the native
   negotiated WORR envelope under `FR-10-T04`.
6. Complete legacy client/server/demo and impairment matrices, then profile
   journal scans, lifetimes, capacities, and steady-state allocation under
   `FR-10-T14`.

Until those items are done, the existing live cgame presenter remains the
authority; the canonical journal and V2 audit ranges are progressive shadow
infrastructure rather than a presentation cutover.
