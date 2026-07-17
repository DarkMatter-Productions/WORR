# FR-10-T04 Native Readiness Setting Sideband

Date: 2026-07-14

Task: `FR-10-T04`

Status: Readiness-record carrier implemented, tested, and now registered by the
default-off production adapters linked below. Native capability remains
excluded from public offer/support masks.

> 2026-07-16 supersession: the readiness record now independently binds
> `snapshot_epoch`. The current carrier uses 15 pairs over `-31980..-31966`;
> one client record is 75 bytes and one server record is 135 bytes. The
> 13-pair sizes below remain historical evidence for the earlier schema.

## Purpose

The endpoint-readiness state machine needs a reliable bootstrap path that both
legacy protocol directions can already carry. Native WTC1 data cannot safely
carry the proof that authorizes native WTC1 parsing, so the three readiness
records use adjacent legacy setting messages until both directions are armed.

This sideband serializes the existing 32-byte
`worr_native_readiness_record_v1`; it does not define another readiness model or
weaken any state-machine binding.

## Logical format

Both directions use the same 13 adjacent signed-16 index/value pairs in the
previously unused range `-31980` through `-31968`:

```text
BEGIN/version
record kind
transport epoch low/high
negotiated capabilities low/high
readiness nonce words 0..3
readiness-record CRC32 low/high
COMMIT/CRC16
```

The range sits after the legacy canonical-command setting tuple and before the
capability-confirmation and consumed-cursor ranges. CLC settings already expose
signed-16 values. SVC settings arrive as signed 32-bit values, so the SVC entry
point rejects a recognized readiness index whose value is not representable as
signed 16-bit instead of silently truncating it.

The carried CRC32 is the checksum from the readiness record itself. The final
CRC16 uses a distinct `WRS1` domain and binds the ordered index and value words
of the first 12 pairs. These checks detect corruption and framing mistakes;
neither is authentication.

## Parser contract

The pointer-free 216-byte parser is explicitly packet-scoped. A caller begins a
packet, feeds each setting or reports an intervening non-setting service, takes
a committed record, and ends the packet. The parser requires exact adjacency
and order:

- a missing, repeated, reordered, or unexpected readiness index poisons the
  candidate for the rest of that packet;
- an ordinary setting or other service resets a partial sequence;
- a packet boundary discards a partial or untaken committed record;
- a second record is allowed in the same packet only after the first committed
  record has been taken;
- reconstruction uses `Worr_NativeReadinessRecordInitV1`, compares the carried
  checksum, and passes the result through the shared record validator before
  commit.

`TakeRecordV1` is transactional. Output/parser overlap is rejected without
mutation, and a successful take copies the exact validated record before
returning the parser to idle. Telemetry is saturating and distinguishes
boundary resets, intervening services, malformed order, unsupported versions,
range failures, record/checksum/commit failures, committed records, and taken
records.

## Validation

The focused behavior and C++ layout tests cover:

- noncollision and exact adjacency of every setting index;
- CLC and SVC round trips for `CHALLENGE`, `CLIENT_READY`, and
  `SERVER_ACTIVE`;
- packet begin/end, multiple records, untaken-record discard, and missing,
  reordered, repeated, interleaved, and out-of-range fields;
- readiness checksum corruption, commit corruption, unsupported versions, and
  every single value-bit mutation;
- an independent fixed `WRS1` record/checksum/commit vector and a complete
  `CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE` exchange through the carrier,
  including exact duplicate replay, wrong-direction rejection, stale-epoch
  rejection, and final time-aware RX/TX gates;
- encode/take aliases, output preservation, invalid/deep parser states,
  saturating telemetry, every public telemetry offset, and fixed C/C++ layouts.

The two configured Meson networking tests pass. Strict x64 C/C++, i686
syntax/layout, Clang analysis, ASan/UBSan, 25 repeated executions, and scoped
diff checks also pass.

## Remaining integration at sideband validation

The production adapter must reserve all 13 legacy setting messages atomically,
feed packet boundaries from the real service parser, and hand each taken record
to the correct role-specific readiness transition. Lifecycle ownership must
reset the parser and readiness state together across connect, reconnect,
serverdata/map transitions, failure, and teardown.

No live mask may advertise `WORR_NET_CAP_NATIVE_ENVELOPE_V1` until both endpoint
adapters, local RX/TX resources, the post-admission receive seam, the dormant
post-assembly transmit seam, and the three-record readiness exchange are wired
and tested. A readiness failure after native confirmation must fail closed; it
must not reinterpret the current connection as legacy.

## Production integration update (2026-07-14)

The production client/server adapters now reserve and parse all 13 settings
atomically and bind the completed three-record proof to native sessions. Public
capability remains `0x03` while the private proof remains `0x13`. Once
`CLIENT_READY` or queued `SERVER_ACTIVE` makes peer transmission possible,
local failure keeps the hooks in DRAIN so WTC trailers cannot leak into the
legacy parser. A post-`CLIENT_READY` invalid same-epoch `SERVER_ACTIVE` is
monotonic fail-closed: a later valid ACTIVE cannot reactivate DATA; only a
successfully validated fresh map challenge after quiesce may arm a new epoch.
See `docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
