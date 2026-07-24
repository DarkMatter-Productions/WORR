# FR-10-T13 native-demo bootstrap / map-load — implementation plan

Status: **scoped implementation plan**, not built code. This is the one FR-10-T13
seam still open after the planner, execution cursor, client player, snapshot
presentation, and event presentation were built and committed. It is documented
here at precise file:line scope because it is a **multi-subsystem feature**
(native codec ABI + demo format + recorder + client load path), its client half
is unverifiable without a running client, and it touches the legacy
serverdata/demo machinery — so it should be built deliberately with running-game
verification, not blind. The wired snapshot/event presentation already in the
demo player present nothing until this bootstrap makes a map + cgame live.

## What the client needs to boot a map (the bootstrap payload)

From `CL_ParseServerData` (`src/client/parse.cpp:645`) and the demo precache path
(`src/client/main.cpp:1860-1882`), the minimum to reach `ca_precached` and present
frames:

- Serverdata identity: `protocol`, `cl.servercount`, `cl.gamedir`,
  `cl.clientNum`, `cl.serverstate`, `cl.game_api`, and the protocol-selected
  `cl.csr` remap + `cl.max_stats`/`esFlags`/`psFlags` (`parse.cpp:674-884`).
- Configstrings: `cl.configstrings[MAX_CONFIGSTRINGS]` up to `cl.csr.end`
  (`client_state.h:292-294`), each `char[CS_MAX_STRING_LENGTH=96]`. The
  load-bearing ones are the world model `cl.csr.models+1` and `cl.csr.mapchecksum`
  (`precache.cpp:305-318`), plus model/sound/image/skin indices for correct
  rendering.
- Spawn baselines: `cl.baselines[MAX_EDICTS]` (`client_state.h:204`), applied via
  `CL_ParseBaseline` (`parse.cpp:615`).
- Load sequence the record must reproduce: apply serverdata → apply configstrings
  (`CL_ParseConfigstring`, `parse.cpp:581`) → apply baselines → `CG_Load` +
  `cgame->Init()` (`parse.cpp:893-894`) → `CL_RegisterBspModels`/`CL_PrepRenderer`
  (`precache.cpp:305/621`) → `ca_precached`.

**Envelope:** the configstring set is the bulk and is variable-length (populated
indices up to `cl.csr.end`, each ≤ `Com_ConfigstringSize`). This is the crux for
the format choice below.

## Format options (native_demo.c / native_codec)

A WDR1 record body must be one canonical WNC1 codec image: `Worr_NativeDemoRecordEncodeV1`
requires `codec.record_class == record_kind` (`native_demo.c:337-338`) and the
scanner re-validates per-class semantics (`record_info_valid` `native_demo.c:375-467`,
`Worr_NativeCodecValidateV1` in the scan loop `:929`). The WNC1 codec
(`inc/common/net/native_codec.h`) has no opaque class — only COMMAND/SNAPSHOT/
EVENT/EVENT_STREAM_DESCRIPTOR fixed canonical bodies.

- **Option 1 — new WNC1 codec class `WORR_NATIVE_RECORD_BOOTSTRAP_V1`**
  (`inc/shared/native_envelope.h:40-45` + `native_codec.h/.c`): preserves the
  one-codec-image-per-record invariant. Clean, but a fixed canonical body cannot
  hold the variable configstring set; a fixed map-identity-only body is
  insufficient for correct rendering. Would need a variable-length canonical
  codec body (a new codec capability), which is a real codec-ABI extension.
- **Option 2 — demo-level opaque bootstrap blob**: smaller payload path, but
  breaks the codec-image invariant and requires special-casing every record-kind
  site (below) with its own CRC-covered length + validation. Higher risk to the
  working, tested format.

Recommendation: Option 1 with a **variable-length canonical bootstrap body** in
the WNC1 codec (map identity header + serialized configstring/baseline table),
because it keeps the scanner/index/seek invariants intact. This is the largest
piece of work.

## Every record_kind site to thread `BOOTSTRAP` through (`src/common/net/native_demo.c`)

- `record_kind_valid()` `:136-140` (central allow-list)
- `record_metadata_valid()` `:305-310`; `Worr_NativeDemoRecordEncodeV1` `:312`, class check `:337-338`
- `record_info_valid()` `:375-467` (add a BOOTSTRAP case to the per-kind switch)
- `Worr_NativeDemoRecordDecodeV1` `:469`, `record_kind_valid` `:495`, class match `:536`
- Ordering: `order_state_valid()` `:580-627`, `order_observe_ref()` `:636-700`
  — add **bootstrap-precedes-everything**: bootstrap legal only as the first
  record (`ordinal == first_record_ordinal`, phase NONE), and every other class
  requires the bootstrap class-bit in `seen_class_mask` (mirroring the existing
  event-requires-snapshot rule at `:680-685`).
- `Worr_NativeDemoStreamScanV1` `:865` (record walk `:919-947`)
- `Worr_NativeDemoIndexEntryFromRecordV1` `:719`; `record_ref_schema_valid()` `:813-826`
- Playback: `locate_record_payload` (`native_demo_playback.c:923/939-941`) + a new
  `Worr_NativeDemoPlaybackDecodeBootstrapAtV1` in the decode-at-index family
  (`native_demo_playback.h:192-220`).

## Recorder capture (`native_demo_recorder.c` / `native_demo_recorder.cpp`)

- Write the bootstrap record immediately after `Worr_NativeDemoRecorderBeginV1`
  (`native_demo_recorder.c:131`), **claiming ordinal 1** (currently the first
  Observe* takes ordinal 1). Add `Worr_NativeDemoRecorderBootstrapV1` (or a Begin
  argument) that frames+writes it before any Observe*.
- Client side: capture live gamestate in `CL_NativeDemoRecord_f`
  (`native_demo_recorder.cpp:437`) from `cl` (protocol/gamedir/clientNum/csr,
  `cl.configstrings`, `cl.baselines`) and write the bootstrap **between** Begin
  (`:437`) and the first `ObserveSnapshotV1` (`:444`).

## Client load path (the unverifiable half — running-game gate)

In the native player (`src/client/native_demo_player.cpp`), on `native_play`:
decode the bootstrap record and reproduce the serverdata→configstrings→baselines→
`CG_Load`/`cgame->Init`→`CL_RegisterBspModels`/`CL_PrepRenderer`→`ca_precached`
sequence (mirroring `CL_ParseServerData` + demo `CL_Precache_f`,
`main.cpp:1874-1882`), so the already-wired snapshot/event presentation renders.
This touches the legacy serverdata/demo machinery and **cannot be unit-verified**;
it must be validated on a real client per the verification runbook (extend it with
a "native_play loads the recorded map and renders" criterion).

## Build status (incremental)

- **DONE — payload serialization core** (`inc/common/net/native_demo_bootstrap.h`,
  `src/common/net/native_demo_bootstrap.c`, test `network-native-demo-bootstrap`):
  the pointer-free serialization of the bootstrap gamestate — the fixed
  serverdata-identity header (protocol/gamedir/clientNum/csr-variant/map name +
  checksum) plus the variable configstring table — with a transactional,
  fail-closed encoder/decoder and a roundtrip + capacity + malformed/truncated/
  reorder test. This is the data every later layer carries. Baselines are
  intentionally omitted (the first snapshot supplies entity state).
- **DONE — codec-class wrapper** (`WORR_NATIVE_RECORD_BOOTSTRAP_V1` in
  native_envelope.h; `Worr_NativeCodecBootstrap{Preflight,Encode,Decode}V1` in
  native_codec, tested by `network-native-codec`): a WNC1 record class wrapping
  the bootstrap payload (fixed 184-byte header body + a variable configstring
  range), so a WDM1 record can carry it while preserving the one-codec-image-per-
  record invariant. Inspect/Validate recognize and fail-close the class; Encode/
  Decode delegate the body to the tested payload core and add the canonical WNC1
  framing. Additive: existing COMMAND/SNAPSHOT/EVENT classes and their tests are
  unchanged; the build has no -Werror and only native_codec.c/native_demo.c
  switch on record_class.
- **DONE — demo-format threading** (`native_demo.c`, `native_envelope.c`, tested
  by `network-native-demo-roundtrip`): `WORR_NATIVE_DEMO_RECORD_BOOTSTRAP_V1`
  threaded through `record_kind_valid`, `record_info_valid`, the envelope
  `record_class_valid`, and `record_ref_schema_valid`, plus the
  bootstrap-precedes-everything ordering rule in `order_state_valid` /
  `order_observe_ref` (bootstrap legal only as the first record; rejected
  anywhere else before the per-class identity logic).
- **DONE — recorder write core** (`Worr_NativeDemoRecorderBootstrapV1`, tested):
  frames+writes the bootstrap record first (claiming the first ordinal), failing
  closed if any record was already written. The roundtrip test proves a
  bootstrap-first stream scans with the bootstrap as index entry 0 and that a
  bootstrap after a snapshot is rejected.
- **DONE — client-side capture** (`write_bootstrap_record` in
  `native_demo_recorder.cpp`): fills the header/configstrings from live `cl`/`cls`
  and writes the bootstrap first in `CL_NativeDemoRecord_f`. Low risk (reads `cl`,
  writes a record); links; validated on a running client per the runbook.
- **DONE — playback bootstrap decode-at-index**
  (`Worr_NativeDemoPlaybackDecodeBootstrapAtV1`, tested): the client map-load
  reads the gamestate at index entry 0 with this.
- **DONE (best-effort, UNVERIFIED) — client map-load** (`CL_NativeDemoMapLoad_f`,
  the `native_mapload` command): applies the decoded bootstrap to `cl`/`cls` and
  reproduces the serverdata + demo-precache sequence (`CL_ClearState`, populate
  `cl` identity + `cl.csr` + `cl.configstrings`, `CG_Load`/`cgame->Init`, then
  `CL_RegisterVWepModels`/`CL_RegisterBspModels`/`CL_PrepRenderer`/
  `CL_RegisterSounds`/`CL_LoadState`, `cls.state = ca_precached`) **additively**
  via public entry points -- it modifies no legacy function, and it runs only on
  the explicit opt-in `native_mapload` command, so a fault is confined to this
  experimental path and cannot touch legacy `.dm2`/live play. Compiles and links.
  Its runtime correctness (the exact stateful order, cgame/renderer assumptions,
  cl.frame/q2proto-context expectations) is the running-game gate the plan's
  Definition-of-Done requires a live session to validate -- see the runbook.
  Historical note: the earlier blocker below assumed the precache trigger had to
  be reused; it was avoided by reproducing the demo-precache steps directly from
  their public entry points.
- (superseded) The player already decodes and stages the bootstrap
  (`player.bootstrap_header`);
  what remains is applying it to `cl` and triggering the map load. Concrete
  blocker found: the load/init trigger `CL_Precache_f` is **static**
  (`main.cpp:1860`) and its map-load branch is gated on `cls.demo.playback`
  (`main.cpp:1874-1882`), and the per-map register functions (`CL_RegisterBspModels`,
  `CL_PrepRenderer`, `CG_Load`) are internal. So driving the actual map load
  requires either entangling `native_play` with the legacy demo-playback machinery
  (`cls.demo.playback` + the `precache` command + the legacy precache path) or
  un-staticing/reproducing internal functions across `main.cpp`/`parse.cpp`/
  `precache.cpp`. Both are invasive changes to the legacy client bootstrap
  machinery, are not unit-testable (no test links the client), and can break
  legacy `.dm2`/live playback if wrong — the reproduction of `CL_ParseServerData`'s
  stateful, `Com_Error`-trapped sequence needs a running client to iterate
  against. This is the running-game gate the plan's Definition-of-Done requires a
  live session to close. Suggested live approach: set `cls.serverProtocol` +
  `cls.demo.playback`, `CL_ClearState`, populate `cl` (protocol/servercount/
  gamedir/clientNum/csr/max_stats/flags) and `cl.configstrings[]` from the
  bootstrap, then `Cbuf_AddText("precache\n")` — iterating on a live client.

## Verification

- Unit-testable (payload core done; codec + demo-format threading next): the
  encode/decode roundtrip, the record→scan→index→decode roundtrip with a
  bootstrap-first stream, and the bootstrap-precedes-everything ordering (a
  snapshot before bootstrap must fail closed).
- Running-game only: the recorder capturing a real map's gamestate and the client
  reproducing the map load. This is the evidence gate; no unit test can close it.
