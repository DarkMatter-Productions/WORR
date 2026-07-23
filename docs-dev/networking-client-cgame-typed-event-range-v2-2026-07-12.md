# Client-to-Cgame Typed Event Range V2

Task: `FR-10-T05`

Date: 2026-07-12

Status: typed client decode-order range implemented and tested; `FR-10-T05`
closed on 2026-07-19. Production presentation ownership remains with
`FR-10-T07`/`DV-04-T02`.

## Outcome

The client now projects five legacy action-message families and accepted-frame
entity events into one allocation-free, decode-ordered canonical event range:

- typed temporary entities;
- player muzzle flashes;
- monster muzzle flashes;
- normalized spatial sounds;
- per-client damage indicators; and
- typed legacy entity events from accepted frames.

This range began as an audit-only migration stage. Existing temp-entity,
muzzle, sound, damage, and entity-event presenters remain authoritative and
execute at their original sites. Action ranges are delivered immediately before
their legacy presenter; entity-frame ranges are delivered after the existing
cgame entity-event pass. Failure or absence of the optional consumer cannot
suppress, duplicate, or reorder raw presentation.

The client-range extension itself changes no `q2proto/` source, legacy packet or
snapshot byte, reliable/unreliable ordering, demo/MVD byte, or legacy server
producer. Later private native schema-2 carriage is a separate T04/T07 boundary.

## Frozen V1 and Named V2 Extension

`WORR_CGAME_CANONICAL_EVENT_SHADOW_EXPORT_V1` remains frozen with its original
entity-frame-only contract. Broadening V1 would make older consumers accept
payloads and callback phases that they never validated.

`inc/shared/cgame_event_shadow.h` therefore adds the macro
`WORR_CGAME_EVENT_RANGE_EXPORT_V2`, whose named-extension string value is
`WORR_CGAME_CANONICAL_EVENT_RANGE_EXPORT_V2`. The client validates the
export's exact structure size, API version, and three required callbacks before
attachment. Classic cgame and older external modules continue without a V2
consumer.

The current external cgame implementation synchronously validates and hashes
each range. It retains no range pointer, record pointer, or record copy and has
no presentation hook.

## Phase and Carrier Contract

V2 has two explicit phases:

1. `ACTION_PRE_PRESENT` is an immediate range for a successfully adapted action
   message. Temp, muzzle, and sound carriers contain one record; damage contains
   one to four ordered records. Adaptation failure produces one immediate empty
   rejected range.
2. `ENTITY_FRAME_POST_PRESENT` is an accepted-frame carrier split into chunks
   of at most 512 records. Every chunk shares the same epoch, batch, carrier
   sequence, tick, time, kind, status, and chunk count.

Every range carries a bounded `carrier_kind`:

- entity frame;
- temp entity;
- player muzzle;
- monster muzzle;
- spatial sound; and
- damage.

Rejected carriers contain no records and carry one bounded `adapter_status`:
unsupported ID, invalid shape, entity out of range, or invalid payload. The
cgame audit hashes kind and status and maintains accepted/rejected carrier
counters for each kind.

## Decode Order and Source Order

`first_arrival_ordinal` is engine-owned decode/acceptance order. It is distinct
from `record.source_ordinal`, which describes deterministic order inside the
legacy source:

- immediate legacy actions retain source ordinal zero;
- accepted entity events use parsed-entity scan order;
- chunk continuation advances arrival order while preserving source scan
  ordinals;
- an empty accepted or rejected carrier consumes one arrival ordinal, so two
  observed carriers never become indistinguishable merely because both have
  zero records.

Arrival, batch, and carrier cursors fail closed before wrap. Reset establishes
a new non-zero epoch and restarts all three cursors.

## Typed Adapters

### Temporary entities

The adapter uses `Worr_EventLegacyTempFieldMaskV1` as the single subtype field
catalog. Only fields actually defined for a subtype are copied; all remaining
payload and padding bytes stay zero. Historical integer slots remain
subtype-aware: entity references, steam/widow sustain IDs, and particle
magnitude are not conflated.

True entity-bearing beam, cable, flashlight, heatbeam, power-splash, and
lightning forms receive client-observed entity references. Lightning carries
its second entity as the canonical subject. Non-entity forms use world as the
source. Unknown or malformed forms produce an empty rejected temp carrier
without changing legacy presentation.

`DAMAGE_DEALT` maps to a gameplay cue; the remaining supported temp forms map
to visual effects.

### Muzzle flashes

Player and monster muzzle records use `MUZZLE_V1`. Player IDs accept the
assigned ranges 0..20 and 30..39; the reserved 21..29 gap fails closed.
Monster IDs accept the active WORR cgame catalog 1..293, including the Vore and
Chthon additions.

The packet parser now validates the source entity and muzzle ID before copying
them into `mz` or invoking cgame. Invalid entity indices, player gaps, monster
zero/294, and the invalid silenced-monster shape cause an explicit
`ERR_DROP`. `src/game/cgame/cg_main.cpp` pins the active bgame muzzle enum and
offset table to canonical ID 293 at compile time. The shorter dormant engine C
table is not used as the WORR decode bound.

### Spatial sounds

The sound adapter consumes the existing normalized `q2proto_sound_t` after
decode validation. It preserves asset ID, entity/channel presence, explicit
position, origin, volume, attenuation, and time offset, and assigns pitch 1.
Only routing facts present in the decoded client message are claimed. Reliable,
PHS, local-only, and forced-position routing cannot be reconstructed at this
boundary, so action ranges explicitly carry `ROUTING_UNKNOWN`.

### Damage indicators

The damage adapter accepts the protocol-bounded one-to-four indicator array and
maps every indicator to one `DAMAGE_INDICATOR_V1` record in wire order. The
records share one carrier identity, source tick/time, and callback; validation
and delivery are all-or-nothing. Zero, excess, malformed, truncated, or
unrepresentable input produces an empty rejected damage carrier.

Damage is implicitly addressed to the controlled player. First-person frames
may omit that entity, so the client synchronizes only its observed slot from
the latest exact canonical snapshot/player identity before binding the range.
Current tick, entity index, generation, and provenance must agree. Unavailable,
stale, mismatched, or rewinding lineage rejects instead of fabricating a new
generation; other observed slots are never changed by this synchronization.

### Legacy entity events

V2 replaces V1's generic `U32X4` migration payload with the typed
`LEGACY_ENTITY_V1` catalog. The source record preserves accepted frame tick,
time, parsed-entity scan order, observed entity generation, exact raw event,
presentation flag, and teleport discontinuity flag. Unexpected raw values
reject the complete frame carrier as an empty audited carrier; they do not
alter accepted-frame handling.

## Provenance and Demo Behavior

Action message ticks and times are inferred from the client's current accepted
frame context because legacy action messages do not carry an independent
source tick. The builder adds `SOURCE_TIME_INFERRED` and `ROUTING_UNKNOWN`
itself, so a successful producer cannot omit required provenance.

Entity-frame ranges instead carry `ENTITY_GENERATION_OBSERVED`. All ranges
state that the legacy presenter remains authoritative. Demo playback and demo
seek are explicit flags; `DEMO_SEEK` is valid only together with
`DEMO_PLAYBACK`. The same capture sites run during ordinary parsing and seek
replay, preserving message order without changing demo bytes.

Legacy-derived records remain ID-less, authoritative-only candidates:

- event ID `{0, 0}`;
- no `HAS_AUTHORITY_ID` flag;
- zero prediction key;
- transient, present-once, replay-safe delivery metadata.

Prediction-key derivation and authoritative reconciliation remain later
`FR-10-T05` work.

## Provisional Entity Generations

Actions may reference an entity before it appears in an accepted frame. V2
assigns a provisional observed generation without allocating or inventing a
server lifecycle ID. A later accepted frame adopts that generation when the
entity is visible. If an accepted frame omits the entity, provisional/present
state clears; the next action or reappearance increments the generation.

World is permanently `{0, 1}`. Absent subject is `{NO_ENTITY, 0}`. Generation
preview for both source and subject completes before any lifecycle mutation,
so exhaustion of either reference leaves the entire builder unchanged.

This generation is explicitly an observer lineage, not a true authoritative
entity lifecycle generation. PVS/visibility absence can split one still-live
entity into multiple observed generations, and the counter alone cannot prove
slot reuse. `FR-10-T06` authoritative lineage/tombstone identity must replace
or reconcile this provisional lineage before authority IDs or prediction keys
depend on it.

## Failure Atomicity and Storage Safety

The V2 builder owns no allocation. The client supplies fixed observed, marker,
scratch-record, and carrier arrays. Initialization and every subsequent shape
check validate:

- non-null pointers, alignment, capacities, and checked byte ranges;
- no exact or partial overlap among the builder envelope and all owned arrays;
- no overlap between mutable owned storage and action/frame input;
- non-zero epoch and live order cursors.

Full frame validation precedes lifecycle, cursor, or callback mutation.
Duplicate entities, invalid scan order, invalid flags, tick rewind, generation
exhaustion, order exhaustion, malformed candidates, buffer aliasing, and
reentrant delivery fail without partial changes. Scratch records are zeroed
immediately after every callback.

## Deterministic Evidence

`tools/networking/cgame_event_shadow_test.c` now covers V1 preservation plus:

- all V2 carrier kinds and adapter statuses;
- typed temp, muzzle, sound, and legacy-entity records;
- bounded one-to-four damage batches, ordered per-indicator ordinals, and
  exact controlled-player canonical lineage across first-person omission;
- immediate action provenance and post-present frame provenance;
- source ordinal versus arrival ordinal;
- unique arrival slots for empty accepted and rejected carriers;
- provisional generation creation, adoption, disappearance, and reuse;
- 513 entity events split into 512/1 chunks with shared carrier identity;
- callback reentrancy rejection and immediate scratch destruction;
- demo seek implication and exact kind/status/phase/chunk flag validation;
- bytewise failure atomicity for malformed input and exhausted cursors;
- exact and partial owned-buffer aliases plus aliased external input;
- player muzzle boundaries and gap rejection;
- monster muzzle boundaries 0, 1, 288, 289, 293, and 294;
- per-kind audit counters, record counts, and deterministic hashes.

The C11/C++20 layout checks pin the 184-byte action candidate, 12-byte carrier,
12-byte observed state, and 208-byte audit status. The event-journal
harness directly covers candidate-only validation in addition to authoritative
record validation.

## Subsequent Ownership Work

The accepted T05 contract stops before production presentation or transport
cutover. Direct native carriage, present-once effects, raw/native ownership,
prediction correlation, and impairment parity continue under T04, T07, T08,
and `DV-04-T02`; they are not retroactive T05 closure criteria.

No `.install/` refresh is performed by this focused slice; the integrating
build owns distributable staging after concurrent networking work settles. The
subsequent combined networking pass refreshed and validated `.install/`,
including the current engine, dedicated-server, cgame, sgame, and rebuilt
`basew/pak0.pkz` payload.
