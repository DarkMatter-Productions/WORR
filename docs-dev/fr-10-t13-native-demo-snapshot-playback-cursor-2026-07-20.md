# FR-10-T13 Native Demo Snapshot Playback Cursor

Date: 2026-07-20  
Project task: `FR-10-T13`

## Outcome

WDM1 now has an allocation-free, pointer-free snapshot playback cursor over
caller-owned immutable stream bytes and a caller-owned scan index. The cursor
can position at the first snapshot, seek to the latest snapshot at or before a
time and/or ordinal bound, and step monotonically to the next snapshot without
emitting a duplicate during forward traversal.

This is a core decoding slice. It does not open files, own index storage,
publish into the client snapshot store, control a playback clock, or replace
legacy DM2/MVD/GTV playback. `FR-10-T13` remains incomplete, and the networking
plan remains at 8/16 completed tasks.

## Pointer-free binding

`worr_native_demo_playback_cursor_v1` is a frozen 296-byte, pointer-free value.
It retains:

- the complete validated WDM1 scan result and the bounds used to reproduce it;
- independent canonical fingerprints of every stream byte and every index
  field;
- the current and immediately following index positions;
- the last emitted WDR ordinal, time, and canonical snapshot ID; and
- a nonzero reset generation once playback has been positioned.

The stream and index are never retained by address. Callers supply both again
on every operation. Before selection, the core checks their exact sizes and
fingerprints, reruns the complete WDM1/WDR1/WNC1 semantic scan, exact-compares
the resulting scan to the bound value, and revalidates the entire index. Index
validation does not depend on finding a seek match: a valid header-only stream
is accepted, and the existing seek validator's `NOT_FOUND` result still proves
the zero-snapshot index.

The playback contract is intentionally stricter than generic WDM1 scanning:
WDR ordinals must be contiguous from `first_record_ordinal`, and every indexed
snapshot epoch must equal the WDM1 timeline epoch. This detects missing index
positions and prevents cross-epoch playback binding while retaining legal
canonical snapshot sequence gaps represented by snapshot discontinuity data.

## Cursor transitions

The state transitions are explicit:

1. `Worr_NativeDemoPlaybackInitV1` creates an unpositioned binding with reset
   generation zero.
2. `Worr_NativeDemoPlaybackPositionFirstV1` selects the first snapshot even
   when command records precede it. It emits generation one with
   `INITIAL_POSITION | RESET_REQUIRED`.
3. `Worr_NativeDemoPlaybackSeekV1` uses the existing at-or-before selector,
   increments the reset generation, and emits
   `SEEK_POSITION | RESET_REQUIRED`. An intentional seek to the current
   snapshot may re-emit that snapshot only under this new reset generation.
4. `Worr_NativeDemoPlaybackStepV1` starts at the entry immediately after the
   emitted snapshot, skips intervening command/event records, and emits the
   next snapshot strictly after the current canonical ID. It does not change
   the reset generation.

Reset-generation exhaustion fails transactionally rather than wrapping.
Header-only streams initialize successfully; first-position, seek, and step
all return `NOT_FOUND` without changing cursor or decode outputs.

## Transactional canonical decode

The selected WDR frame is decoded again from its logical offset and exact
frame span. Every decoded WDR and WNC1 identity, class, offset, length, CRC,
ordinal, and time field is compared with the selected index entry. Snapshot
epoch and sequence must match both the WNC1 object identity and the WDM1
timeline epoch, and WDR `time_us` must equal canonical snapshot
`server_time_us`.

The supporting `Worr_NativeCodecSnapshotMetadataV1` API exposes one frozen
336-byte pointer-free metadata result. It reuses the existing complete WNC1
snapshot dry-run validator, including all entity/area/event ranges and
semantic hashes, without materializing capacity-dependent output. Playback
therefore validates ID, epoch, and time before writing any caller buffer. The
normal canonical projection decoder then commits the snapshot, player,
entities, area bytes, and event references directly into distinct
caller-provided fixed-capacity destinations.

All size multiplication and logical-offset arithmetic is checked. Stream,
index, cursor, query, frame metadata, and decoded output regions must not
overlap. Capacity, truncation, corruption, unsupported class, epoch, order,
gap, mutation, and alias failures leave every output byte unchanged. A
zero-count decoded range writes no bytes and safely permits a null destination
even when the unused capacity value is `UINT32_MAX`.

## Focused evidence

The deterministic playback fixture contains commands before the first
snapshot and commands/events between three snapshots. Coverage includes:

- initial position, same-snapshot seek reset, backward seek, and monotonic
  forward stepping;
- exact next-entry behavior and end-of-stream non-duplication;
- header-only streams and zero-range snapshots;
- reset-generation exhaustion and at-or-before not-found behavior;
- every single-byte stream mutation and every byte of the canonical index;
- bound-scan mutation, insufficient capacity, overlapping outputs, and
  overflowing logical index metadata;
- wrong timeline epochs and contiguous-WDR ordinal gaps;
- a fully checksummed WDR/index whose record time disagrees with its valid
  canonical snapshot body; and
- direct metadata-preflight success, corrupt-hash/body rejection, input/output
  alias rejection, and transactional sentinel preservation.

C and C++ layout gates freeze the playback config (32 bytes), cursor
(296 bytes), frame metadata (160 bytes), and codec snapshot metadata
(336 bytes).

Focused validation:

```text
meson compile -C builddir-win \
  native_codec_test native_codec_layout_c_test native_codec_layout_cpp_test \
  native_demo_playback_test native_demo_playback_layout_c_test \
  native_demo_playback_layout_cpp_test

meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-native-codec network-native-codec-layout-c \
  network-native-codec-layout-cpp network-native-demo-playback \
  network-native-demo-playback-layout-c \
  network-native-demo-playback-layout-cpp \
  network-native-demo-playback-source-contract
```

Result: 7/7 tests passed. The new playback and metadata sources and the new
playback/layout tests also pass Clang 20 syntax-only compilation under C11/C++20 with
`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`.

## Remaining FR-10-T13 work

The cursor is not yet integrated into the client or filesystem reader.
Remaining work includes standalone WDM bootstrap metadata, client
timeline/store reset and playback-clock ownership, pause/timescale behavior,
command and event recording, present-once event replay across seek, spectator
POV changes, legacy corpus acceptance, MVD/GTV record/play/relay, malformed
file parent evidence, and the full native/legacy compatibility matrix.
