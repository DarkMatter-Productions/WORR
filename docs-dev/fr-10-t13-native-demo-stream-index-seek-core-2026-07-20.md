# FR-10-T13 Native Demo Stream Index and Seek Core

Date: 2026-07-20  
Project task: `FR-10-T13`

## Outcome

The native WDM1/WDR1 framing foundation now has an allocation-free,
filesystem-agnostic full-stream scanner and a deterministic snapshot seek
selector. The API operates only on caller-owned byte spans and caller-owned
index entries. It does not retain pointers, open files, or alter the legacy
DM2/MVD/GTV paths.

This is a prerequisite slice, not completion of `FR-10-T13`.

## Stream scan contract

`Worr_NativeDemoStreamScanV1` accepts one exact WDM1 image, a caller-selected
maximum record count, and a nonzero exclusive entity-index bound. It performs
the following work before committing output:

- validates the WDM1 header, transport tuple, reserved fields, and CRC;
- walks every WDR1 record with checked logical-offset arithmetic;
- validates each WDR1 frame length and CRC plus its complete WNC1 framing;
- runs the class-specific no-output semantic validator over every command,
  event, and snapshot, including canonical snapshot hashes and the configured
  entity bound;
- enforces global ordinal/time order, per-class canonical identity order, prior
  snapshot ownership for events, and snapshot-before-event same-time order;
- rejects trailing partial data rather than silently treating it as padding;
- derives compact, pointer-free index entries; and
- binds all canonical index fields into `index_crc32` so later seek validation
  detects intermediate entry mutation, including changes that preserve the
  final order-state high-water marks.

A null index output with zero capacity provides a count-only validation pass.
When an index is requested, the scanner first validates the entire stream and
its capacity, then performs the deterministic fill pass. The encoded input is
required to remain byte-identical for the duration of the call. Capacity,
limit, malformed, truncation, corruption, order, arithmetic, and alias
failures leave every output byte unchanged.

## Seek contract

`Worr_NativeDemoSeekSnapshotAtOrBeforeV1` selects the latest snapshot satisfying
all enabled bounds:

- `WORR_NATIVE_DEMO_SEEK_BY_TIME` requires `time_us <= target_time_us`;
- `WORR_NATIVE_DEMO_SEEK_BY_ORDINAL` requires
  `ordinal <= target_ordinal`; and
- enabling both applies both predicates.

Before returning a selection, it revalidates the complete index: layouts,
schemas, contiguous checked offsets, record identities, ordering, snapshot
count, final order state, stream end, and the scan-time canonical index CRC.
No match returns `WORR_NATIVE_DEMO_NOT_FOUND`; malformed or reordered entries
fail closed. The result remains unchanged on every failure.

## ABI and test coverage

The new pointer-free host structures have frozen C and C++ layout checks:

- scan config: 32 bytes;
- scan info: 184 bytes;
- seek query: 32 bytes; and
- seek result: 88 bytes.

The deterministic fixture covers a five-record
snapshot/event/snapshot/command/event stream, count-only and populated scans,
large logical offsets, two-bound/time-only/ordinal-only seek selection,
not-found behavior, a header-only stream, caller limits, insufficient capacity,
truncated records, trailing bytes, corrupt header and payload CRCs, malformed
lengths, valid outer CRCs around invalid command/event/snapshot bodies,
same-time order violations, offset overflow, reordered indices,
intermediate-time mutation, offset gaps, CRC mutation, scan/index mismatch, and
transactional failure outputs. It also flips one bit at every byte position in
the complete valid image and requires every mutation to fail without changing
the scan output.

Focused validation passed:

```text
meson compile -C builddir-win native_codec_test native_demo_test native_demo_layout_c_test native_demo_layout_cpp_test
meson test -C builddir-win --print-errorlogs network-native-codec network-native-demo network-native-demo-layout-c network-native-demo-layout-cpp
```

Result: 4/4 tests passed.

The implementation and tests also pass Clang 20 syntax-only compilation with
`-std=c11`/`-std=c++20`, `-Wall`, `-Wextra`, `-Wpedantic`, `-Wconversion`,
`-Wsign-conversion`, and `-Werror`.

## Remaining FR-10-T13 work

The scanner/index core now supports the separate fixed-buffer snapshot recorder
and pointer-free transactional snapshot playback cursor documented in
`docs-dev/fr-10-t13-client-native-snapshot-recording-2026-07-20.md` and
`docs-dev/fr-10-t13-native-demo-snapshot-playback-cursor-2026-07-20.md`.
Neither path is yet a complete client playback system. `FR-10-T13` still
requires file-backed reader ownership, canonical publication into playback
timelines, playback-clock seek/reset integration, pause and timescale behavior,
command/event recording, event present-once reproduction, spectator switches,
legacy DM2 compatibility gates, MVD/GTV record/play/relay paths, malformed-file
parent evidence, and the complete native/legacy compatibility matrix.
