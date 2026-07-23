# FR-10-T13 Bounded Client Native Snapshot Recording

Date: 2026-07-20
Project task: `FR-10-T13`
Status: implemented production recording slice; task remains incomplete

## Outcome

The client now has an explicit, opt-in path that records promotion-qualified
canonical snapshots into a separate WDM1 file. The path is production-wired at
the immutable snapshot admission boundary, uses fixed staging storage, commits
recorder accounting only after exact writes, and publishes a final `.wdm` only
after a complete bounded rescan succeeds.

This is a snapshot capture for tooling and future replay work. It is not yet a
playable demo and does not satisfy the full `FR-10-T13` Definition of Done.

## Compatibility boundary

The new commands are separate from the legacy demo commands:

- `native_record <name[.wdm]>`
- `native_stop`
- `native_record_status`

The recorder writes `demos/<name>.wdm.tmp` and publishes
`demos/<name>.wdm`. It does not add records to `.dm2` or `.mvd2`, change
`CL_Record_f`, change `CL_WriteDemoMessage`, alter demo auto-detection, or
reinterpret a legacy demo as WDM1. MVD/GTV and server MVD framing are also
unchanged. No file under `q2proto/` is modified.

The command requires active live play, a confirmed exact native snapshot
capability bundle, and an independently parity-qualified immutable snapshot.
It rejects client demo playback/seeking and MVD/GTV broadcast playback.
Legacy clients, servers, demos, and the normal `record`/`stop` commands retain
their existing behavior.

## Recording subset

This slice emits snapshot WDR1 records only. It intentionally emits no command
or event records because WDM1 does not yet define a production mixed-class
timebase, and projected legacy events do not all have authoritative T05 IDs.

The WDM1 header records:

- WNC1 v1 transport;
- the confirmed negotiated capability mask;
- the connection capability epoch as `transport_epoch`;
- the first snapshot epoch as `timeline_epoch`;
- record ordinal 1 as the first ordinal; and
- `com_unscaledTimeUs` as diagnostic monotonic creation time, not wall time.

Each WDR1 record uses the snapshot's `server_time_us`. Snapshot IDs must
increase within the one bound timeline epoch. Forward sequence gaps remain
legal and retain their canonical discontinuity metadata. The controlled entity
identity must remain exact for the whole file.

## Admission lifecycle

`CL_SnapshotShadowAcceptFrameEx` calls the registered recorder observer only
after the immutable view has been produced, legacy parity has qualified it,
and `record_native_expectation` has committed the independent expectation. The
call occurs before a cgame consumer callback, so a later presentation rejection
cannot erase an already admitted canonical snapshot.

`CL_SnapshotShadowPromoteLatestFrame` has the same observer placement for a
delayed promotion. Deduplication uses canonical snapshot ID plus endpoint and
snapshot hashes, not the process-local projection ref, so the same promoted
snapshot cannot be written twice after store-slot reuse. A same-ID hash
conflict or regressive ID terminally fails. The observer encodes synchronously
and retains no immutable-view pointer.

Serverdata/map transitions finalize before `CL_ClearState` invalidates snapshot
storage. Disconnect cleanup finalizes before its later state clear. An observed
timeline-epoch change, POV change, order conflict, codec limit, byte limit, or
write failure is terminal and leaves the temporary file quarantined rather
than silently skipping an admitted snapshot.

Recorder failure is diagnostic and local: it never changes the result of
canonical snapshot admission.

## Bounds and transactions

`Worr_NativeDemoRecorderBeginV1` and
`Worr_NativeDemoRecorderObserveSnapshotV1` form the filesystem-neutral core.
The caller provides one fixed `WORR_NATIVE_CODEC_MAX_ENCODED_BYTES` buffer, one
fixed `WORR_NATIVE_DEMO_MAX_RECORD_BYTES` buffer, and an exact-write callback.
The buffers must not overlap. No per-snapshot allocation occurs.

The core stages WNC1 and WDR1 completely, decodes the staged WDR1, observes a
private next `worr_native_demo_order_state_v1`, checks byte and ordinal bounds,
then writes. Stream bytes, record count, ordering state, ordinal, and dedupe
identity commit only after the sink reports the exact frame length. A short
write makes the recorder terminal while leaving those fields at the last
complete record.

The production file cap is controlled by `cl_native_demo_max_mb`, clamped to
1..64 MiB with a 64 MiB default and hard ceiling. This stays below
`MAX_LOADFILE`, allowing a bounded complete-file validation before publication.
WNC1 additionally enforces at most 512 snapshot entities, 1024 area bytes, and
512 event references, with `cl.csr.max_edicts` as the exclusive entity-index
bound.

## Path and crash policy

Recording names are restricted to the game `demos/` directory. Empty names,
absolute/drive paths, `.` or `..` components, invalid platform characters,
trailing control/space bytes, overlong paths, existing final files, and
existing temporary files are rejected. Writes use `FS_FLAG_EXCL`; gzip and
append/resume are not supported.

On explicit stop, map boundary, or disconnect, the client:

1. marks the bounded recorder stopped;
2. flushes and closes the temporary file;
3. loads the bounded file and validates its complete WDM1 image with
   `Worr_NativeDemoStreamScanV1`;
4. verifies record/snapshot counts, byte length, capabilities, and epochs; and
5. renames the temporary file to `.wdm` only if the destination is still
   absent.

There is no WDM1 footer. A crash or short write may leave complete prefix bytes
plus a truncated tail, but only under `.wdm.tmp`. Complete-stream validation
rejects that artifact, and it is never advertised as a valid `.wdm`. Failed
temporary files are retained for diagnosis; the recorder never appends to or
resumes them.

## Verification

Focused evidence on 2026-07-20:

```text
python tools/networking/test_native_demo_recorder_source_contract.py \
  --repo-root .
  pass

meson compile -C builddir-win native_demo_recorder_test
  pass

builddir-win/native_demo_recorder_test.exe
  pass

meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-native-demo \
  network-native-demo-recorder \
  network-native-demo-recorder-source-contract
  3/3 pass
```

The fake-sink suite covers exact header/record output, complete scanner
acceptance, forward gaps, canonical deduplication across a changed projection
ref, conflicting duplicates, regressions, timeline/POV changes, byte limits,
header short writes, record short writes, and transactional counters.

The recorder client source and both admission/lifecycle hooks compile in the
`worr_engine_x86_64` target. The full shared-engine link in the concurrent
workspace was blocked after compilation by unresolved
`Worr_NativeInputBatchSideband*`/`Worr_NativeInputBatchConfirmEqualV1` symbols
from the separate in-flight `FR-10-T16` slice; no recorder symbol or compile
failure was reported.

No interactive client was launched. Future live validation must use the
documented isolated hidden-client policy with `win_headless=1`, `in_enable=0`,
and `in_grab=0`.

## Remaining `FR-10-T13` work

`FR-10-T13` remains unchecked. This slice still lacks:

- standalone bootstrap state for map, gamedir, configstrings, and baselines;
- WDM1 playback into the canonical cgame timeline;
- playback seek/reset, pause, and timescale behavior;
- authoritative event recording and present-once replay;
- command records and a defined mixed-class timebase;
- multi-map files and spectator/controlled-view switch records;
- MVD/GTV record, playback, and relay integration; and
- the full legacy/native record/play/seek/relay, malformed-input, load, and
  supported-platform matrices.
