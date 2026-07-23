# FR-10-T07 Schema-2 Damage and Map-Reuse Present-Once Evidence

Date: 2026-07-20  
Project tasks: `FR-10-T07`, `DV-04-T02`; supporting boundaries
`FR-10-T04`, `FR-10-T05`, `FR-10-T14`, and `FR-10-T15`

## Decision

The default-off native event preflight now has a strict, same-process,
two-map runtime gate for one real Blaster action. The target receives five
canonical action records in source order: two temporary-event records, one
muzzle record, one spatial-audio record, and one damage record. The four
same-impact records share exact producer tick/time coordinates and travel as
one schema-2 EVENT batch; the earlier launch muzzle has a distinct snapshot
identity and remains one schema-1 singleton. Both representations retain one
logical ACK/retry identity per sender unit.

The v100 parent passed three fresh process repetitions, with both map phases
passing in every repetition. All six phases recorded five raw legacy actions,
five authoritative present-once commits, five probe commits, five raw effect
dispatches, five suppressed native effects, and identical raw/probe/effect
chain hashes. Native effect dispatch remained zero because production effect
authority is still disabled. Raw legacy presentation therefore remains the
sole audiovisual owner.

This is bounded `FR-10-T07` and `DV-04-T02` evidence, not task closure. It does
not complete the per-carrier ownership inventory, enable native effects,
cover every event family, or supply the loss/reorder/rate/pause/demo/load/
platform matrices required by T07, T14, and T15. Snapshot/netcode completion
therefore remains **8 of 16**.

## Five-family client range and damage batch

The client-to-cgame V2 action range now covers five legacy action-message
families in addition to accepted-frame entity events:

- typed temporary entities;
- player and monster muzzle flashes;
- normalized spatial sounds; and
- per-client damage indicators.

`svc_damage` is bounded by the Rerelease protocol limit of one to four ordered
indicators. The shared damage adapter creates one typed record per indicator,
preserves its source ordinal and payload, and submits all records in one
allocation-free V2 action range. The delivery is transactional: invalid count,
shape, identity, capacity, or callback state rejects the whole carrier before
partial audit or lifecycle mutation.

First-person packet frames may omit the controlled player entity. Before
binding a damage range, the client now synchronizes only that controlled slot
from the latest exact canonical snapshot/player identity. It requires the
current source tick, matching snapshot/player index, generation, and
provenance, and rejects stale or rewinding lineage. This prevents packet-frame
absence from fabricating a new observed generation for a damage record while
leaving every other observed entity lifecycle unchanged.

Adding the sixth V2 carrier kind (`damage`) expands the fixed per-kind audit
arrays. The current ABI is therefore a 184-byte action candidate, 12-byte
carrier, 12-byte observed state, and **208-byte** V2 audit status. The earlier
192-byte status described only the five-carrier layout and is obsolete.

## Native schema-2 EVENT unit

The canonical schema-2 payload concatenates two to eight independently valid
schema-1 EVENT images. Records must be authoritative, ordered, contiguous in
event identity, and share exact `source_tick` and `source_time_us` producer
coordinates. `SNAPSHOT_FENCED` remains optional per-record delivery metadata;
batching does not invent or normalize it.

The batch object is identified by `{stream_epoch, last_sequence}`. Admission,
retention, retransmission, and receipt therefore treat the batch as one
transport unit while preserving every logical record for ordered cgame
presentation. Sender telemetry separately counts promoted logical events and
schema-2 batches so a carrier cannot conceal a missing or extra record.

For the v100 Blaster profile, the impact produced four same-coordinate records
and the launch produced one earlier singleton. Every phase consequently
reported:

```text
candidates_queued=5
candidates_promoted=5
event_acks=5
schema2_batches_promoted=1
schema2_events_promoted=4
retries=5
queue_failures=0
```

The retry figure is the checkpoint-window delta. A second-map status may also
contain one pre-window readiness retry; the runner compares deltas and does not
misattribute that lifecycle traffic to the shot.

## Present-once and raw-owner parity

The target's combined probe status is schema 2 and 320 bytes. It joins the
existing 120-byte presenter status with raw action/effect evidence, exact
checkpoint scope, and fixed family counters. The successful per-phase family
profile is:

```text
(none, legacy entity, legacy temp, muzzle, spatial, damage, help)
(0,    0,             2,           1,      1,       1,      0)
```

Each phase first proves a stable zero baseline. After the shot it requires:

- `raw_action_records=5`, `raw_effect_dispatches=5`, and no pending raw pair;
- `authoritative_presentations=5` and `probe_action_commits=5`;
- `probe_effects_suppressed=5` and `native_effect_dispatches=0`;
- exact equality of raw action, raw effect, and probe action chain hashes;
- zero duplicate/conflicting authority, legacy body/reference mismatch,
  presenter-commit mismatch, raw-pair failure, degradation, or resync; and
- an applied checkpoint receipt plus an exact duplicate receipt that cannot
  inflate any counter.

The target uses OpenAL Soft's null output backend, with engine initialization
and no fallback backend markers. This proves the real audio lifecycle remains
available without opening a visible client or an input device; it is not a
subjective audible-parity claim.

## Same-process map lifecycle

The gate executes `gamemap worr_canonical_rail_gate` once between phases while
retaining the dedicated server, shooter, and target processes and client slots.
Two lifecycle defects exposed by this gate are now closed at their production
boundaries:

1. `SV_ClientReset()` calls `SV_NativeShadowPeerQuiesceMapV1()` before clearing
   the map-local command parser/stream. A healthy peer publishes a cancellation
   floor for prior native transport, sender, and receipt state; clears pending
   input/match state; retains its exact hooks; and returns to
   `WAIT_READINESS`. Repetition is idempotent. A busy, failed, or structurally
   inconsistent peer fails closed with a session failure instead of carrying
   an in-flight half into the next map.
2. A non-fresh `svc_serverdata` calls the retained cgame DLL's `Shutdown()`
   before `CL_ClearState()`, reload, and the next per-map `Init()`. This gives
   every retained-DLL map initialization exactly one preceding shutdown, so
   present-once counters and prepared plans end generation 1 before generation
   2 begins.

Across all three repetitions, the first and second phases respectively record
map generations `1` and `2`, map-end counts `0` and `1`, and native event stream
epochs `2` and `4`. Stale event, receipt, or presentation state does not cross
the transition.

## Sealed-prefix CHALLENGE liveness

Map reuse also exposed an intermittent readiness starvation: `SV_Begin_f()`
requested the native CHALLENGE before game/bootstrap reliable output had
finished, while later reliable messages could keep the ordinary queue nonempty
until its 60-second queue bound expired.

The server now establishes an exact precedence barrier after `stuff_cmds`,
`ClientBegin`, and client announcement have completed. It records the reliable
byte count at that boundary. Once the current generation is idle, the send
scheduler:

1. hands off exactly that sealed bootstrap prefix;
2. preserves all later reliable bytes as an untouched queued tail;
3. waits for the handed-off generation to clear;
4. preflights and transmits CHALLENGE as its own isolated reliable generation;
5. starts the separate 10-second readiness deadline only for that CHALLENGE
   transaction; and
6. allows the preserved tail to follow normally.

Both synchronous and asynchronous server schedulers apply rate and fragment
admission before servicing this barrier, then service it before frame/native
output. The asynchronous path remains available while the game is paused. The
60-second pre-start queue bound remains fail-closed; the change removes
unbounded postponement by later reliable appends rather than relaxing either
deadline.

Focused netchan tests cover exact prefix handoff, byte-identical tail
preservation, hook-appended tail bytes, retry, fragments, output bounds,
invalid/overlapping storage, and isolated CHALLENGE handoff. A source contract
pins the end-of-Begin barrier and scheduler ordering.

## Strict runner ordering

Each map phase is closed in the following order:

1. prove both client and server native peers active through queue-neutral RCON
   status;
2. arm the ordinary gameplay fixture and wait for both existing players;
3. drain all lifecycle EVENT sender work;
4. capture pre-checkpoint cgame status, issue one checkpoint, prove applied and
   duplicate receipts, then prove a stable zero row;
5. require the server sender signature to remain unchanged across that client
   checkpoint;
6. send one matched `+attack 255; -attack 255` command to the shooter;
7. close the authoritative 15-damage gameplay result;
8. close server sender counts/schema/retries first; and
9. close target present-once/raw parity and compare the two five-record totals.

The map transition is deliberately non-idempotent. The runner sends one RCON
`gamemap`, requires exactly one `SpawnServer` marker in that direct reply, then
requires one additional `Serverdata packet received` marker from both retained
clients. It does not stuff team/status commands into either client reliable
queue before the new native epochs are proven. Only then does phase 2 repeat
the complete checkpoint sequence.

The target impairment is armed only after initial native readiness. It applies
105 ms upstream latency plus a 20 ms upstream stall, forcing five retransmits
per phase without packet loss, duplication, corruption, reordering, overflow,
or rate throttling. This is a delayed-ACK role split, not the broader compound
fault matrix.

## Verification

Focused headless verification from `E:\Repositories\WORR`:

```text
meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-cgame-event-shadow \
  network-cgame-event-presentation \
  network-native-codec \
  network-native-event-sender \
  network-native-server-shadow-pilot \
  network-damage-indicator-source-contract \
  network-native-event-presenter-source-contract \
  network-server-native-map-quiesce-source-contract \
  network-server-native-challenge-barrier-source-contract
PASS: 9/9

python tools/networking/test_run_canonical_rail_damage_runtime_gate.py
PASS: 96/96

python tools/networking/run_canonical_rail_damage_runtime_gate.py \
  --client-exe .tmp/networking/t07_probe_stage_v45/worr_x86_64.exe \
  --dedicated-exe .tmp/networking/t07_probe_stage_v45/worr_ded_x86_64.exe \
  --working-dir .tmp/networking/t07_probe_stage_v45 \
  --output .tmp/networking/fr10_t07_native_event_probe_checkpoint_v100_barrier_clean_repeat3.json \
  --weapon native-event-probe-map-reuse --repeat 3 --timeout 120 --lag-debug 3
PASS: 3/3 repetitions; 6/6 map phases
```

The schema `worr.networking.canonical-weapon-damage-runtime.v42` parent artifact
records 15/15 gameplay damage in every phase, the exact
five-record profile, one four-record schema-2 batch plus one singleton, five
checkpoint-window retries, zero terminal peer failures, zero duplicates, zero
chain/body/commit mismatches, clean process teardown, and stream rotation
`2 -> 4`.

Evidence and binary hashes:

```text
artifact SHA-256
545C6A46BD2E6A952958945CCE2F7DF2175551AE9423D5FFAF150DD31DA841D3

worr_engine_x86_64.dll SHA-256
99D1F56062988FE2BAD780DB896AF3AB3892D2650202D00B353EBD01D3F957A9

worr_ded_engine_x86_64.dll SHA-256
52771A69D486E2A51E0F71BEFDC1A08F9BC295C55C572EE46014B601EA18B7B4

basew/cgame_x86_64.dll SHA-256
F95743C6A92DA0286D2BB779C1C7E7E89E05976CC3C0320B68A1FB1C3B750750
```

No `q2proto/` file changed. The integrating build refreshed and validated
`.install/` with 16 root runtime files, one dependency, and 601 packaged
assets.

## Remaining closure work

- Complete the applicable `DV-04-T02` raw/native ownership inventory and
  negotiate an exhaustive or per-carrier cutover before suppressing raw
  presentation.
- Extend real-process presenter parity across all projected families and
  loss, duplication, reordering, corruption, rate, pause/timescale, entity
  reuse, reconnect, demo record/play/seek, and resource-pressure cases.
- Finish T08 predicted audiovisual authority and reconciliation before native
  predicted effects can be enabled.
- Satisfy T14's malformed-input, allocation, 1/8/16/32-client, sustained-load,
  and performance floors, then T15's supported-platform, soak, opt-in, and
  rollback requirements.

`FR-10-T07` remains **In Progress**, `DV-04-T02` remains open, and the project
remains **8 of 16 complete**.
