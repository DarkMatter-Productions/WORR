# FR-10 T04/T06/T07 Production Native Snapshot Adapters

Date: 2026-07-16

Primary project tasks: `FR-10-T04`, `FR-10-T06`, and `FR-10-T07`.

Supporting and safety tasks: `FR-10-T05`, `FR-10-T14`, and `FR-10-T16`.

## Outcome and authority

WORR now has default-off production server-transmit and client-receive
adapters for the canonical native snapshot transaction developed by the
preceding Stage D semantic-admission work.

The live path begins at the exact final per-peer server projection retained
after successful legacy frame encoding. It copies that projection into a
bounded server-owned native sender, emits WNC1 snapshot bytes through the real
post-assembly WNE1/WTC1 netchan hook, accepts them through the real client
netchan receive hook, defers semantic admission until the independently
reconstructed legacy frame supplies an exact parity-qualified expectation,
publishes the admitted immutable view to the real cgame V2 snapshot timeline,
and returns the semantic ACK through the reverse native carrier. The server
releases its retained snapshot only after that exact ACK is admitted.

This is a production adapter milestone, not the public native-snapshot
authority cutover:

- legacy q2proto frames remain gameplay, rendering, effects, audio, demo, and
  compatibility authority;
- the native snapshot lane is private, default-off, and does not alter the
  public `WORR_NET_CAP_LEGACY_STAGE_MASK == 0x03` offer;
- while private snapshot mode owns a readiness epoch, native admission is the
  sole publisher to the canonical cgame snapshot timeline;
- the client still reconstructs the legacy frame independently, but uses that
  reconstruction only to qualify the native expectation instead of publishing
  a second copy to the timeline; and
- a native failure drains or quarantines only the private lane. It never
  rewrites or invalidates the already authoritative legacy packet.

The work advances `FR-10-T04` by connecting the retained native carrier to
both production netchan directions, `FR-10-T06` by sourcing native DATA from
the exact final-emission server view, and `FR-10-T07` by assigning one
unambiguous producer to the cgame canonical timeline during snapshot mode.
It also depends on `FR-10-T05`'s event-fence semantics and advances
`FR-10-T14` bounded observability/failure coverage and `FR-10-T16` mixed
DATA/ACK scheduling. None of those parent tasks is complete from this slice
alone.

## End-to-end data flow

The production path is:

```text
final per-peer legacy frame emission
    -> SV_SnapshotShadowCommitFrameV1
    -> SV_NativeShadowQueueSnapshotV1
    -> bounded native snapshot sender
    -> real server post-assembly netchan TX hook
    -> admitted client netchan RX hook
    -> bounded native snapshot receiver
    -> deferred exact legacy-shadow expectation
    -> semantic snapshot admission
    -> real cgame V2 snapshot timeline and event fence
    -> client semantic ACK ledger
    -> real client post-assembly netchan TX hook
    -> server ACK admission
    -> exact retained payload release
```

The server queues the native projection only after the complete legacy frame
header and entity stream have encoded successfully and
`SV_SnapshotShadowCommitFrameV1` has produced a valid final-emission
reference. Consequently the native copy includes the same per-client
visibility, truncation, entity lineage, player state, area data, and event
references observed by the legacy packet writer.

The two directions remain complementary:

- server to client carries canonical snapshot DATA and may carry ACKs for
  client command DATA in the same WTC1 transaction; and
- client to server carries the existing command DATA and may carry semantic
  snapshot ACKs in the same WTC1 transaction.

There is one server-originated semantic DATA owner per private epoch. Event
mode and full-snapshot mode are therefore mutually exclusive until a separate
combined scheduling and authority contract is designed.

## Private modes

The current exact private readiness bindings are:

| Mode | Exact mask | Added private capabilities | Readiness chain |
|---|---:|---|---|
| Command | `0x53` | native envelope and epoch cancellation over public `0x03` | `CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE` |
| Event | `0x73` | command binding plus native event stream | `CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE -> CLIENT_ACTIVE_CONFIRM` |
| Snapshot | `0x57` | command binding plus canonical snapshot V2 | `CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE -> CLIENT_ACTIVE_CONFIRM` |
| Event + snapshot | `0x77` | event and canonical snapshot capabilities together | Defined for validation/future design, but unsupported by the live adapters |

The command, event, and snapshot constants include
`WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1`; this is why the current values are
`0x53`, `0x73`, and `0x57`, respectively.

The client controls are `cl_worr_native_shadow`,
`cl_worr_native_event_shadow`, and `cl_worr_native_snapshot_shadow`. The
server controls are `sv_worr_native_shadow`,
`sv_worr_native_event_shadow`, and `sv_worr_native_snapshot_shadow`. All
default to disabled. Enabling both event and snapshot controls is ambiguous
and therefore creates no private pilot on either endpoint. The implementation
does not silently select one mode or reinterpret `0x77`.

## Bound readiness and snapshot epoch

The readiness record now carries a canonical `snapshot_epoch` independently
of the native `transport_epoch`. Bound readiness constructors and validators
enforce this exact relationship:

- a binding containing `WORR_NET_CAP_CANONICAL_SNAPSHOT_V2` requires a
  nonzero snapshot epoch; and
- every non-snapshot binding requires a zero snapshot epoch.

Snapshot DATA cannot teach, replace, or advance that epoch. The server binds
the current canonical snapshot epoch into `CHALLENGE`, and the client binds
its legacy projector and native receiver from that independently negotiated
control record before DATA can gain semantic authority.

The signed-setting readiness carrier now contains 15 index/value pairs. The
two new pairs carry the low and high 16-bit words of `snapshot_epoch`; the
remaining ordering, checksum, commit marker, strict sign extension, and
packet-boundary transaction rules remain intact.

The production wire sizes are:

- client-to-server readiness record: 15 pairs at 5 bytes per CLC setting,
  for 75 bytes; and
- server-to-client readiness record: 15 pairs at 9 bytes per SVC setting,
  for 135 bytes.

Event and snapshot modes both require the fourth
`CLIENT_ACTIVE_CONFIRM`. After `SERVER_ACTIVE`, the client can initialize its
receive owner and enqueue the confirmation. The server may receive native
application bytes while waiting for that record, matching real netchan parse
order, but server-originated DATA remains closed until the exact confirmation
is accepted. Command mode retains the original three-record chain.

## Server sender and coalescing

`worr_native_snapshot_sender_v1` is a transport-neutral, bounded owner for one
server-to-client canonical snapshot stream. The production server adapter
allocates it only for a peer that completed the exact `0x57` private binding.

The sender has:

- one supersedable retained TX slot;
- two generation-tagged payload banks;
- immutable encoded bytes for the duration of a multi-fragment dispatch;
- a single latest-pending projection while a dispatch is active;
- exact duplicate, stale, conflict, capacity, and sequence-limit handling;
- transactional mixed DATA/ACK prepare, confirm, and reject operations;
- a 100 ms snapshot resend interval in the current server adapter; and
- pointer-free scalar status for retained bytes, active and pending snapshot
  IDs, supersession, coalescing, ACK release, sends, retries, and failures.

If a newer final-emission projection arrives during an active multi-fragment
dispatch, it cannot mutate the in-flight payload. It occupies the other bank.
Still newer projections coalesce atomically to the newest legal identity in
that pending bank. Once the current dispatch reaches a definite terminal
outcome, the pending bank is promoted and supersedes the older retained
snapshot where legal.

An exact semantic ACK releases the corresponding retained TX entry and
payload bank. If the server enters DRAIN, the sender stops queueing and
emitting new DATA but retains already sent payload ownership so a late exact
ACK can still release memory. Terminal cancellation aborts any known
dispatch, cancels retained transport state, releases both banks, and reports
the removed retained and pending ownership.

`SV_NativeShadowQueueSnapshotV1` accepts only a valid final-emission view with
the expected snapshot epoch and projection-hash schema. Queue or projection
failure drains the native pilot and increments its snapshot-specific status;
the successfully encoded legacy frame continues normally.

## Client receiver and deferred expectations

`worr_native_snapshot_receiver_v1` is the bounded client owner for
server-originated snapshot DATA. It contains:

- two native RX/reassembly slots;
- a 16-entry rolling exact snapshot-expectation cache;
- owned bounded payload, decode, and canonical projection storage;
- the semantic-admission state;
- the semantic ACK ledger;
- the real cgame consumer adapter; and
- saturated telemetry for fragments, duplicates, deferred completions,
  expectations, admissions, repeats, capacity, rejection, quarantine, and
  cancellation.

The live receive adapter supports both legal arrival orders:

1. Native DATA may reassemble first. The complete message remains retained,
   but it receives no ACK while its exact independently qualified legacy
   expectation is absent.
2. The legacy frame may project first. Its exact expectation remains cached,
   and later native completion immediately pumps semantic admission.

The cache retains the newest safe expectation window rather than failing the
epoch on the seventeenth observation. Expectations referenced by active RX or
complete pending descriptors are protected; unprotected older entries may be
evicted for newer identities. An older out-of-window observation, or an
observation for which no safe victim exists, returns retry-later and does not
quarantine or drain the connection. The production gate projects 24
consecutive legacy expectations before the server is allowed to send DATA,
then admits the newest fragmented snapshot exactly once.

`max_entities` is the exclusive entity-index identity domain, not the number
of decoded records. Legacy mode accepts indices below 1,024 and Rerelease mode
accepts indices below 8,192, while codec/decode storage remains bounded to 512
entity records in any one projection. Production coverage exercises high
identities 1023 and 8191 with record counts below that fixed capacity.

Complete RX slots expire lazily when later DATA advances the receiver clock.
The wrapper now reconciles its pending semantic descriptor against the exact
core RX slot after every accepted carrier and after admission. If the core has
expired or reused a complete slot, the stale wrapper descriptor is removed
before the new message is interpreted. A production-shaped regression leaves
snapshot A complete without an expectation, advances beyond the 1,000-tick
complete timeout, reuses the same occupancy with snapshot B, and proves B
admits and receives exactly one ACK without conflict or quarantine.

`CL_SnapshotShadowGetNativeExpectation` classifies an exact lookup as
available, pending, unqualified, stale, wrong-epoch, or invalid. Only
`AVAILABLE` supplies the snapshot ID, five cross-endpoint proofs, and the
diagnostic endpoint-local hash to semantic admission. The cross-endpoint
contract is exact snapshot identity plus legacy parity and the player, entity,
area, and event semantic hashes. `endpoint_hash` includes local
chronology/provenance and may legitimately differ between the legacy
expectation and native final-emission view; after native consumption it must
match the fresh local cgame receipt exactly. A pending classification defers
without inventing failure; an unqualified, wrong-epoch, conflicting, or
otherwise uncertain observation cannot authorize an ACK.

The native consumer adapter wraps the real attached cgame snapshot timeline.
Its opaque lifetime cookie changes when cgame unloads or replaces the export,
so a receiver cannot retain a stale callback identity across module lifetime
changes.

Current and immediately retired receiver owners preserve already authorized
ACK service across a transport-epoch transition. Retired epochs cannot admit
new snapshot DATA. Fresh `CHALLENGE` processing cancels older incomplete
reassembly and receipts under the negotiated epoch-cancellation barrier.

## Cgame timeline ownership

Snapshot mode must not let the same logical frame enter the canonical cgame
timeline twice.

For an ordinary legacy-only connection,
`CL_SnapshotShadowAcceptFrame` can compare the reconstructed q2proto frame and
deliver the canonical projection to the cgame consumer.

For an active native snapshot epoch, `CL_DeltaFrame` instead calls
`CL_SnapshotShadowAcceptFrameEx` with legacy comparison enabled and consumer
delivery disabled. This preserves the independently reconstructed parity
expectation while preventing the legacy projector from publishing. The native
receiver becomes the sole timeline publisher after semantic admission.

Legacy rendering still processes the accepted q2proto frame before this
comparison. Timeline ownership therefore does not make the native lane the
rendering or gameplay authority in this slice.

Ownership remains latched when a diagnostic failure enters DRAIN inside the
same active map epoch. The receiver is quarantined and further native DATA is
rejected, but the engine does not resume legacy timeline publication midway
through that epoch. This avoids mixing two producers, rolling chronology
backward, or consuming the same snapshot twice. An explicit map quiesce,
serverdata reset, disconnect, or new readiness epoch ends the old ownership
scope.

Hook ownership loss follows the same rule. If another subsystem replaces or
removes a netchan application hook after the snapshot epoch binds, the pilot
enters terminal DRAIN, quarantines the receiver, detaches only hooks still
owned by the pilot, and reports transport hooks unavailable. It deliberately
retains canonical timeline ownership until an explicit map/serverdata or
matching connection-close boundary. Foreign replacement hooks are never
overwritten during failure handling or release.

## Lost ACK, exact repeat, and quarantine

A transport-complete native message still has no ACK authority. ACK
publication occurs only after semantic admission has:

- decoded and validated the complete canonical projection;
- matched the exact snapshot identity and five cross-endpoint parity/semantic
  proofs;
- obtained a successful fresh cgame V2 timeline receipt;
- matched that receipt to the decoded native endpoint hash locally; and
- passed the cgame event-fence health checks inherited from `FR-10-T05`.

The receiver grants three proactive ACK handoffs for a committed receipt. If
those handoffs are lost, the server retains the snapshot and retries it. An
exact repeat of an already committed snapshot revalidates the live cgame
receipt and rearms the ACK; it does not decode into a second timeline
publication or call the consumer twice.

The sender applies only exact admitted ACK ranges. An ACK received during an
active fragment dispatch retires the now-unnecessary gate before releasing
the payload, preventing a dispatch from remaining attached to an empty TX
slot. A definite transport rejection restores retry eligibility. A prepared
ACK whose handoff outcome is still unknown is a strict cancellation barrier,
because treating it as either delivered or lost without evidence could break
receipt authority.

Wrong snapshot epoch, identity/hash parity mismatch, malformed canonical
shape, conflicting completion, stale consumer receipt, or other semantic
uncertainty quarantines the snapshot receiver and publishes no ACK. A newer
successfully admitted snapshot supersedes and clears deferred older
reassembly and expectations without authorizing a stale receipt.

## Compatibility and security properties

The production adapter preserves the existing compatibility boundary:

- the public capability offer remains `0x03`;
- private snapshot mode is exact-mask `0x57`, default-off, and restricted to
  the existing supported NEW-netchan protocols;
- legacy servers and clients that never enter the private readiness exchange
  see no native DATA;
- demos and demo seeking do not install the client pilot;
- readiness carrier settings remain excluded from demo/GTV interpretation;
- legacy q2proto packet parsing and rendering continue as before; and
- no file under `q2proto/` was modified.

The wire path is fail-closed rather than trusted. It validates exact
capability, connection owner, transport epoch, snapshot epoch, record class,
schema, fragment provenance, CRCs, payload bounds, expectation hashes,
consumer generation, and ACK ranges before committing authority. Bounded
slots and arenas prevent peer-controlled unbounded allocation.

Readiness and carrier CRCs detect corruption; they are not cryptographic
authentication. The current private pilot therefore remains an opt-in
protocol experiment and must not be described as providing identity or
tamper-proof security.

## Headless validation

All validation for this slice was console-only, headless, input-free, and
mouse-safe. No interactive client, visible window, renderer initialization,
client input initialization, or mouse capture was used.

The focused coverage includes:

- the native snapshot sender's multi-fragment immutable dispatch,
  supersession, pending coalescing, retry, exact ACK release, retirement,
  cancellation, and invalid-binding cases;
- the native snapshot receiver's two arrival orders, reverse fragments,
  duplicates, deferred completion, no-ACK-before-expectation rule, semantic
  admission, three lost ACK handoffs, exact-repeat revalidation, mismatch and
  wrong-epoch quarantine, rolling 24-expectation survival, protected
  in-flight expectations, expired-complete descriptor reconciliation,
  superseded cleanup, and cancellation barrier;
- server pilot snapshot readiness, fourth-confirm gating, final projection
  queueing, multi-fragment retry, ACK release, status, and misuse failures;
- client pilot snapshot readiness, server-bound epoch setup, real receiver
  ownership, expectation pumping, semantic ACK service, and timeline
  ownership behavior, including hook corruption, direct serverdata reset,
  unrelated/matching connection close, and foreign-hook preservation; and
- the readiness core, readiness layout, 15-pair sideband, q2proto carrier
  boundary, native session, event virtual link, and prior snapshot semantic
  virtual-link regressions.

Current validation result:

- dedicated production-hook virtual link: deterministic pass with
  `fragmented=8`, `s2c_loss=1`, `reordered=13`, `duplicates=1`,
  `ack_loss=1`, `repeat_revalidate=1`, `cgame_once=4`,
  `hash_quarantine=1`, `wrong_epoch=1`, `real_domains=4`,
  `expectation_rollovers=1`, `timeout_recoveries=1`,
  `prediction_ready=4`, and digest `7176afa3d4eb62b2`;
- complete headless Meson networking suite: 147/147 passed;
- full Windows production build: passed;
- refreshed Windows x86-64 `.install/`: validated with 16 root runtime
  files, one runtime dependency, one loose q2aas reference BSP, a
  483-member `basew/pak0.pkz`, 31 botfiles, 215 RmlUi assets, and five
  release notice sidecars;
- packaging/stager policy tests: 16/16 passed;
- release headless-contract suite: 1/1 passed.

`native_snapshot_production_virtual_link_test` joins the actual server and
client production hook callbacks. It proves four-fragment server DATA with
one directional loss, reverse-order delivery, an identical duplicate,
deferred legacy expectation, exactly-once cgame publication, one lost
reverse ACK followed by exact repeat revalidation and retention release,
projection-hash quarantine, wrong-snapshot-epoch rejection, real 1,024/8,192
identity domains, a 24-expectation delayed-confirm window, and complete-RX
timeout recovery with exact slot reuse. Its later prediction-authority
extension also proves that all four exact cgame admissions can be copied from
the retained canonical timeline and selected once each with the snapshot's
server-consumed cursor; detailed evidence is in
`docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`.

## Remaining work

This slice does not complete `FR-10-T04`, `FR-10-T05`, `FR-10-T06`,
`FR-10-T07`, `FR-10-T14`, or `FR-10-T16`. Remaining work includes:

- retain the public `0x03` boundary until the full native authority,
  compatibility, and release gates justify advertisement;
- run the required serialized-datagram parity population and malformed-input
  matrix;
- complete bandwidth, CPU, memory, load, soak, and 1/8/16/32-client budgets;
- preserve demo, MVD, GTV, spectator, seek, and reconnect behavior across the
  eventual authority cutover;
- complete predicted local-action reconciliation, event presentation, and
  rendering/presentation ownership work;
- decide and prove any future combined `0x77` event-plus-snapshot scheduling
  contract; and
- introduce a separately versioned retained-base codec if WORR later adopts
  true snapshot deltas rather than the current complete-view WNC1 encoding.

## Roadmap status

Roadmap completion: 74/190 (38.9%), 116 open.

FR-10 completion: 3/16 (18.75%), 13 open.
