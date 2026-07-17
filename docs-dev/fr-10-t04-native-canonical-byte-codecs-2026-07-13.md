# FR-10-T04 Native Canonical Byte Codecs

Date: 2026-07-13  
Project task: `FR-10-T04`  
Status: command, snapshot, and authoritative-event codecs complete; command V1
now serves the default-off one-command production observation linked below,
while general adapters, negotiation, compression, and rollout remain open

## Outcome

WORR now has a versioned, allocation-free native byte codec for the existing
canonical command v1, snapshot v2, and authoritative event v1 models. The
codec is a payload producer/consumer for the already isolated native envelope;
it is not a second gameplay schema and it is not active on a client or server
connection.

The implementation is deliberately staged:

- `WORR_NET_CAP_NATIVE_ENVELOPE_V1` remains unadvertised;
- no live client, server, demo, or cgame path calls the codec;
- legacy Q2/q2proto traffic remains authoritative;
- `q2proto/` is unchanged; and
- decoding cannot create store-local snapshot serials. Successful snapshot
  decode returns the existing `worr_snapshot_store_publish_v2` view, and the
  destination snapshot store atomically assigns serials and recomputes all
  five hashes.

The public contract is
`inc/common/net/native_codec.h`; the strict C11 implementation is
`src/common/net/native_codec.c`.

## Design constraints

The codec follows these hard rules:

1. Every integer and IEEE-754 binary32 bit pattern is encoded little-endian.
2. C padding, pointers, process-local handles, snapshot refs, and arena serials
   are never copied to the wire.
3. The encoded fields reconstruct the accepted canonical structs and are fed
   back through their existing validators and hash functions.
4. Every encode has a validating preflight. No destination changes on invalid
   input or insufficient capacity.
5. Every decode first validates the complete immutable byte range, including
   exact lengths, nested item frames, canonical semantics, ordering, and
   hashes. It commits caller outputs only after that pass succeeds.
6. All storage is caller-owned. There is no heap allocation, implicit growth,
   or retained pointer.
7. Unknown codec, schema, or model versions fail explicitly. Reserved fields,
   alternate lengths, unknown counts, trailing bytes, and zero canonical IDs
   fail closed.

Caller-owned source records and encoded input must remain byte-identical for
the duration of a call; live adapters will provide their normal external
synchronization around store and session access.

Signed zero is encoded as positive zero because the command, snapshot, and
event hash contracts already define those values as semantically identical;
decode rejects the alternate negative-zero wire image. All other accepted
float bits are preserved exactly.

## Shared payload frame v1

Every codec payload starts with a 48-byte frame. This frame is inside the
native envelope payload; it does not replace the envelope's fragmentation,
CRC, session, or retry identity.

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | `WNC1` magic |
| 4 | 2 | codec wire version, exactly 1 |
| 6 | 2 | header bytes, exactly 48 |
| 8 | 1 | native canonical record class |
| 9 | 1 | flags, required to be zero |
| 10 | 2 | canonical record schema version |
| 12 | 4 | canonical model revision |
| 16 | 4 | exact complete encoded byte count |
| 20 | 4 | class-fixed body byte count |
| 24 | 4 | range 0 count |
| 28 | 4 | range 1 count |
| 32 | 4 | range 2 count |
| 36 | 4 | canonical object epoch |
| 40 | 4 | canonical object sequence |
| 44 | 4 | reserved, required to be zero |

`Worr_NativeCodecInspectV1` validates this common frame transactionally and
returns a 48-byte pointer-free `worr_native_codec_info_v1`. The information can
be converted to the existing `worr_native_record_ref_v1` with
`Worr_NativeCodecInfoRecordRefV1`, allowing a future adapter to compare the
payload identity with the outer envelope identity before committing it.

The original Stage C codec payload was bounded by the envelope's 65,536-byte
payload limit; the later Stage D envelope revision raises the complete opaque
message ceiling to 131,072 bytes. Counts remain independently bounded to 512
snapshot entity records, 1,024 area bytes, and 512 snapshot event refs. The
512-record count is separate from the exclusive entity-index identity domain:
live adapters support 1,024 legacy identities and 8,192 Rerelease identities.
The encoded-length limit is checked in preflight and during inspection, before
range parsing.

## Command encoding

Command v1 has a 62-byte body and an exact 110-byte total frame. The header
owns the canonical command ID and movement-model revision. The body encodes:

- cumulative `sample_time_us`;
- the existing T02 `worr_prediction_command_v1` duration, buttons, three view
  angles, and two movement axes; and
- the existing render-watermark provenance, flags, tick, interval, source
  time, and rendered time.

Nested `struct_size`, schema tags, and reserved zeros are reconstructed, then
`Worr_CommandRecordValidateV1` applies the negotiated duration limit and exact
canonical short-angle/movement rules. Tests compare both semantic and content
hashes after round-trip; this covers the legacy packet-shared watermark's
intentional semantic projection as well as exact provenance content.

## Event encoding

Event v1 has a 64-byte fixed body followed by a payload-kind-specific fieldwise
body. Only canonical authoritative records with a non-zero T05 event ID are
transport-addressable in this stage. Predicted ID-less records remain local to
prediction and presentation journals.

The fixed body is the existing event record's flags, source tick/ordinal/time,
source and subject generation-aware entity refs, type, delivery and prediction
classes, prediction key, expiry tick, payload kind, and canonical payload
size. All eleven T05 payload kinds are encoded fieldwise:

| Canonical payload kind | Wire bytes |
|---|---:|
| none | 0 |
| vec3 | 12 |
| entity ref | 8 |
| damage | 40 |
| audio | 20 |
| effect | 32 |
| u32x4 | 16 |
| legacy entity v1 | 4 |
| legacy temp v1 | 68 |
| muzzle v1 | 8 |
| spatial audio v1 | 40 |

The legacy-entity canonical payload is eight C bytes but has only four wire
bytes; its required-zero reserved word and any C padding are reconstructed.
Likewise, the 72-byte legacy-temp canonical payload has a 68-byte wire image;
its reserved tail is not serialized. This proves the codec is field-defined,
not a raw-structure transport.

Decode verifies the declared wire-payload length against the payload kind and
the canonical payload size against the T05 catalog before calling
`Worr_EventRecordValidateV1`. Round-trip proof covers every payload switch and
compares the authoritative event hash.

## Snapshot encoding

Snapshot v2 uses the existing immutable projection as encoder input and the
existing store-publication view as decoder output.

The 437-byte fixed body contains:

- snapshot flags, base ID, authoritative tick/time, controlled generation,
  exact consumed-command cursor, and discontinuity metadata;
- semantic event-range metadata, excluding its store-local serial;
- the five source component/final hashes; and
- the complete existing `worr_snapshot_player_v2` semantic record, with nested
  ABI headers and reserved zeros reconstructed on decode.

The header's ranges mean entity count, area-byte count, and event-ref count.
Entities are component-aware. Each begins with an exact 16-bit item byte count
and a required-zero 16-bit reserved word. The smallest legal entity is 52
bytes (the 28-byte framed identity/component prefix plus mandatory transform),
and a record with every v2 component is 125 bytes. Absent components consume no
wire bytes and reconstruct to their canonical absent representation.

Each snapshot event ref is a fixed 30-byte framed item. Area visibility bytes
are copied exactly. Decode rejects unknown component bits, alternate entity or
event-ref item sizes, unordered entity identities, mixed/unsorted event refs,
generation-source disagreement, controlled-entity disagreement, and an event
range that does not exactly describe the decoded refs.

Before commit, decode recomputes and compares:

- player hash;
- ordered entity-list hash;
- area hash;
- event-ref-list hash; and
- final snapshot hash.

The internal validation image uses a non-zero sentinel only to satisfy the
canonical range validator; that value never escapes and is never serialized.
The successful caller-visible snapshot metadata has zeroed entity, area, and
event ranges plus zeroed hashes, exactly as required by
`Worr_SnapshotStorePublishV2`. Store publication then assigns real monotonic
serials, rebuilds event-range metadata, recomputes all hashes, and commits or
fails without exposing a partial slot.

The uncompressed component-aware representation can carry 512 all-component
entities with no other ranges inside the 65,536-byte envelope bound. Some
valid combinations of maximum entities, event refs, and area bytes exceed that
bound and return `WORR_NATIVE_CODEC_LIMIT`; a live native path must add a
canonical baseline/delta or separately framed range strategy before
advertisement. Silently truncating a canonical snapshot is not permitted.

## Transactional and hostile-input behavior

Encode validates sources, calculates the exact byte count, verifies capacity
and non-aliasing, then performs an infallible fieldwise commit. Decode builds
fixed records locally and walks snapshot variable ranges once without writing
caller storage. Only after exact end-of-input, semantic, ordering, and hash
checks succeed does a second walk commit to disjoint caller-owned ranges.

The result enum distinguishes invalid API use, invalid canonical records,
output capacity, malformed framing, unsupported versions, declared-limit
failures, and semantic corruption. Tests cover every truncation of a valid
command frame, malformed magic/header/class/schema/model/fixed-size/count/ID/
reserved/total-length fields, mismatched event payload lengths, snapshot nested
reserved and hash corruption, over-limit counts, insufficient destinations,
and output non-mutation on every tested failure.

## Build and proof evidence

Meson declares these networking-suite targets:

- `network-native-codec` / `native_codec_test.exe`;
- `network-native-codec-layout-c` /
  `native_codec_layout_c_test.exe`; and
- `network-native-codec-layout-cpp` /
  `native_codec_layout_cpp_test.exe`.

Current focused evidence on Windows x86_64 Clang 20.1.7:

- all three targets built and linked;
- the focused matrix passed 3/3;
- three consecutive focused repetitions passed 9/9;
- the complete networking suite, including the new tests, passed 83/83;
- the behavioral test passed under AddressSanitizer plus UndefinedBehaviorSanitizer;
- strict standalone `-Wall -Wextra -Wpedantic` syntax checks passed; and
- C11 implementation plus C/C++ layout checks passed an independent
  `i686-pc-windows-msvc` syntax/layout compile.

The behavior proof includes exact little-endian header/identity/time bytes,
command semantic/content hash parity, every event payload kind and event-hash
parity, component-aware snapshot ranges, decode-to-store publication, and
equality of all five source and republished snapshot hashes. It also encodes,
decodes, and republishes the declared maximum of 512 all-component entities as
a 64,485-byte payload, and then combines those entities with the declared
maximum 1,024 area bytes for a 65,509-byte payload. Adding one valid 30-byte
event ref crosses the 65,536-byte bound and is proven to return `LIMIT` without
output, exercising both sides of the envelope boundary without truncation.

## Exact remaining FR-10-T04 work at codec validation

1. Add bounded live adapters from the canonical command stream, snapshot
   store, and authoritative event journal into the codec and native session.
2. On receive, compare the codec info identity/class/schema with the outer
   envelope record ref before inserting into canonical stores/journals.
3. Define canonical snapshot baseline/delta or separately framed range
   records so every accepted maximum-range combination has a bounded native
   representation below the envelope limit.
4. Add shadow-only client/server transport adapters and prove native-decoded
   versus legacy-projected semantic/hash parity under loss, duplication,
   reordering, timeout, supersession, reconnect, and downgrade.
5. Add long-session/flood/fuzz and Linux/macOS build/runtime evidence.
6. Advertise the native capability only after the FR-10-T14/T15 rollback,
   compatibility, soak, and release gates pass.

## Production integration update (2026-07-14)

Command V1 is now exercised through one default-off production DATA
observation. The server validates the inner command identity against the WNE1
record reference and the official command epoch stored with the selected
transport bank before joining it to legacy authority. Event and snapshot codecs
remain unwired, and no capability is advertised. See
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
