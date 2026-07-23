# Canonical Event Payload Catalog ABI Slice

Task: `FR-10-T05`

Date: 2026-07-12

Status: transport-neutral payload catalog and validation slice implemented;
runtime producer, transport, cgame presentation, prediction-correlation, and
demo/MVD cutovers remain open.

## Outcome

The canonical event ABI now has stable, pointer-free payload definitions for
the retained legacy entity-event, temporary-entity, muzzle-flash, and spatial
sound carriers. This slice deliberately does not change live legacy traffic,
q2proto, runtime producers, cgame presentation, Meson wiring, or the strategic
roadmaps.

The original payload IDs remain unchanged. The appended IDs are:

| Stable ID | Payload |
|---:|---|
| 7 | `WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1` |
| 8 | `WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1` |
| 9 | `WORR_EVENT_PAYLOAD_MUZZLE_V1` |
| 10 | `WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1` |

`worr_event_record_v1` remains 168 bytes with its 80-byte payload beginning at
offset 88.

## Fixed Layouts

`inc/shared/event_abi.h` defines and statically checks:

- an 8-byte legacy entity payload containing the raw event ID, its exact
  semantic flags, and a zero reserved field;
- a 72-byte legacy temp payload containing the numeric subtype, exact
  valid-field mask, signed raw entity carrier fields, time/count/color values,
  four vectors, and a zero reserved field;
- an 8-byte muzzle payload containing semantic family, raw flash ID, and flags;
- a 40-byte spatial-audio payload containing asset, entity/channel presence,
  routing flags, raw entity, optional origin, volume, attenuation, offset, and
  pitch.

C11 and C++20 layout probes additionally check standard-layout and trivially
copyable properties. Strict `-Wpadded -Werror` builds confirm that the current
supported layouts contain no implicit padding.

## Stable Numeric Hazards

The catalog uses numeric carrier identities instead of relying on legacy symbol
spelling. In particular:

- temp ID 22 remains boss teleport;
- temp ID 27 is explicitly the historical/broken blue hyperblaster carrier;
- temp ID 32 is explicitly reserved as unsupported flame;
- temp ID 56 is explicitly the fixed rerelease blue hyperblaster carrier;
- temp ID 128 remains damage-dealt feedback;
- player muzzle IDs 21 through 29 remain unassigned and invalid;
- the monster muzzle range is 1 through the rerelease terminal ID 293.

This avoids the known C/q2proto versus C++ naming disagreement around temp IDs
27 and 56 while leaving `q2proto/` read-only.

## Validation Semantics

`src/common/net/event_abi.c` now validates every new payload through typed local
copies and rejects unknown enum bits, mismatched event types, non-finite values,
out-of-range values, non-zero unused fields, non-zero reserved fields, and
non-zero payload tails.

### Legacy Entity Events

- Item respawn is a presentation `VISUAL_EFFECT`.
- Footstep, fall, other-footstep, and ladder events are presentation
  `MOVEMENT_CUE` records.
- Player teleport is a presentation plus discontinuity `STATE_CHANGE`.
- Other teleport is a discontinuity-only `STATE_CHANGE`.

Raw IDs outside 1 through 9 and any non-canonical flag/type combination fail.
The existing `LEGACY_BRIDGE/U32X4` shadow format remains valid and unchanged;
there is no runtime cutover in this slice.

### Temporary Entities

`Worr_EventLegacyTempFieldMaskV1()` is the single authoritative mapping from
subtype to exact field mask. It covers all q2proto-decoded rerelease shapes:
position/direction impacts, counted/color effects, two-point trails, point
explosions, entity beams, offset beams, lightning, flashlight, forcewall,
immediate and sustained steam, widow sustain, power splash, and damage-dealt.

Rules include:

- all defined vector fields must be finite;
- every undefined vector and scalar field must be zero;
- the historical `entity1`/`entity2` wire slots are interpreted by subtype,
  rather than being treated uniformly as entity references;
- genuine entity references are in `[0, max_entities)`: beam, grapple,
  flashlight, and power-splash sources use `raw_entity1`, while lightning also
  uses `raw_entity2` as its destination entity;
- immediate steam uses exactly `raw_entity1 == -1` and omits time; sustained
  steam uses a strictly positive signed-short sustain ID and positive time;
- steam `raw_entity2` is a signed-short particle magnitude, not an entity, and
  preserves the complete `int16` representation domain;
- widow beam-out uses a strictly positive signed-short sustain ID, not an
  entity reference; the live rerelease producer values 20001 and 20002 are
  explicitly covered and remain valid when `max_entities` is 8192;
- ordinary counts and colors are in the legacy byte range;
- damage-dealt amount is in `[0, INT16_MAX]`;
- all decoded temp types are `VISUAL_EFFECT` except damage-dealt, which is a
  `GAMEPLAY_CUE`;
- reserved flame and all numeric gaps fail validation.

The catalog preserves a full-width damage amount even though the current
q2proto special decoder narrows its `i16` value into a byte field. Correcting
that decoder is intentionally outside this slice.

The follow-up runtime integration audit corrected an initial over-broad rule
that bounded both historical integer slots as entity indices. Live
`TE_WIDOWBEAMOUT` producers write sustain IDs 20001 and 20002 in
`src/game/sgame/monsters/m_widow.cpp`; `TE_STEAM` stores a sustain ID or the
immediate sentinel in `entity1` and particle magnitude in `entity2`, as consumed
by `CL_ParseSteam` in `src/game/cgame/cg_tent.cpp`. Validation now assigns
roles explicitly for every integer-bearing subtype. This correction changes no
payload ID, structure size, offset, hash field, q2proto source, or wire/demo
byte.

### Muzzle Flashes

Player and monster families are explicit. Player lifecycle flashes map to
`STATE_CHANGE`, item respawn and nuke flashes map to `VISUAL_EFFECT`, and other
assigned player flashes map to `WEAPON_FIRE`. Player silencing is the only v1
muzzle flag. Monster IDs 1 through 293 map to `WEAPON_FIRE` and reject player
silencing or unknown flags.

### Spatial Audio

Entity/channel and fixed-position presence are explicit rather than inferred
from zero values. Forced-position requires a position. Entity channels are
0 through 7 and their raw entities are bounded by `max_entities`; absent entity
channels require channel zero and `WORR_EVENT_NO_ENTITY`.

Volume and attenuation are finite in `[0, 4]`, legacy time offset is finite in
`[0, 0.255]`, and pitch is finite in `(0, 4]`. The current legacy sound wire has
no pitch field; the defensive upper bound is a canonical v1 policy for future
native producers, while legacy adapters use pitch 1.

## Semantic Hashing

`Worr_EventRecordSemanticHashV1()` hashes the same validated semantic fields as
the full event hash while ignoring exactly:

- `event_id.stream_epoch` and `event_id.sequence`;
- `WORR_EVENT_FLAG_HAS_AUTHORITY_ID`; and
- `WORR_EVENT_FLAG_SNAPSHOT_FENCED`.

It uses the unique `ESM1` domain tag (`0x45534d31`); full event hashes retain
the existing `EVT1` domain. Event-model revision 2, introduced with the
keyed-POI payload, intentionally changes the pinned fixture hashes. The
semantic golden is `459024801986651691` for the pinned teleport fixture.

The semantic API accepts an otherwise valid authority candidate with zero ID
and no authority flag, allowing pre-allocation and post-allocation shadows to
compare. It rejects partial or contradictory authority state.

`Worr_EventRecordSemanticallyEqualV1()` is the collision-safe companion. It
compares every ABI field explicitly, uses normalized float bits, dispatches to
per-kind payload comparison, and ignores only the same authority fields. It
does not compare structure or payload object representations. A hostile test
assigns identical semantic temp fields over different `0xa5`/`0x5a` object
fills so any compiler-introduced internal padding cannot affect hashing or
equality.

## Standalone Evidence

All artifacts were built under `.tmp/event-payload-catalog/`; no build-system
file was changed.

Strict C11 ABI/journal test, repeated three times:

```text
clang -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
  -Werror -ffp-contract=off -fno-fast-math -Iinc \
  src/common/net/event_abi.c src/common/net/event_journal.c \
  tools/networking/event_journal_test.c \
  -o .tmp/event-payload-catalog/event_journal_test.exe
```

Result: 3/3 passed.

Strict C11/C++20 layout tests, including `-Wpadded -Werror`: passed.

Compatibility tests built directly against the expanded header and validator:

- legacy server event shadow mapper/allocator: passed;
- immutable client-to-cgame event shadow range: passed.

AddressSanitizer plus UndefinedBehaviorSanitizer build and execution passed
after adding the installed Clang runtime directory to `PATH`.

Coverage includes every supported temp subtype and valid field-mask shape,
immediate/sustained steam, the live widow sustain IDs 20001/20002, full-width
steam magnitude boundaries, every genuine temp-entity reference subtype, all
assigned player muzzle values, monster range boundaries, entity namespace
boundaries, finite/range failures, unknown flag bits, reserved numeric gaps,
payload-size/tail failures, semantic type mismatches, authority-candidate
hashing, authority-ID independence, and padding-independent semantic equality.

## Remaining `FR-10-T05` Work

This does not complete `FR-10-T05`. Remaining work includes typed runtime
producer APIs and global source ordinals, migration of the existing
`LEGACY_BRIDGE/U32X4` client/cgame audit range to typed records,
prediction-key integration, reliable retention and negotiated receipts,
presentation migration, legacy and WORR transport adapters, demo/MVD versioning,
impairment-matrix coverage, and load profiling.
