# FR-10-T05 Cgame Event Runtime and Direct Authority Export

Date: 2026-07-14
Last updated: 2026-07-16

Tasks: `FR-10-T05`, `FR-10-T07`, `FR-10-T08`, `FR-10-T14`; supporting
boundaries from `FR-10-T04` and `FR-10-T09`

Status: implemented and focused-test validated as a cgame-owned, fixed-capacity
event lifecycle and a direct engine-to-cgame authority seam. The seam is not
yet connected to server-originated native DATA, direct sgame multi-event
producers, live local-action producers, or a side-effect presenter. Legacy
decode and legacy presenters therefore remain presentation authority. None of
the parent tasks is complete.

## Outcome and authority boundary

This milestone replaces the earlier collection of event audit observations
with one coherent cgame lifecycle for authoritative and predicted canonical
events. It adds:

1. a transactional authoritative/predicted journal with exact event receipt
   state;
2. ordered authority release joined to immutable snapshot event references;
3. command-keyed prediction reconciliation, cancellation, tombstones, and
   consumed-command retirement;
4. independent legacy, authority, and snapshot-fence lifetimes;
5. a compact C ABI through which the client engine can reset an authority
   stream, synchronously submit copied canonical records, and read validated
   receipt/health state; and
6. an engine-side owner that treats cgame loss, malformed status, callback
   uncertainty, and unresolved snapshot-fence loss as hard-resynchronization
   barriers.

Snapshot-reference and authority lifecycles are correctness state, not an
optional diagnostic. Authoritative references require the active authority
stream; legacy-inferred references are validated and fence snapshot chronology
even when `cg_event_runtime_audit=0`. Receipt progression, prediction
reconciliation, expiry, and ordered authority advancement likewise remain
independent of that cvar. The default-off, `CVAR_NOARCHIVE` control gates only
retention and presentation of legacy body-comparison metadata.

The presentation sink is still deliberately no-effects. It records ordered,
present-once evidence and a deterministic presentation-chain hash, but does
not spawn particles, play audio, mutate gameplay, or suppress an existing
legacy side effect. This preserves the current production behavior while the
new lifecycle is proven.

No file under `q2proto/` changed.

## Cgame-owned architecture

### Fixed storage and transaction model

The runtime lives entirely inside cgame and owns process-static value storage.
It performs no steady-state heap allocation and does not retain caller
pointers. Authoritative and predicted batch calls copy the complete runtime to
a second fixed staging state, validate and apply every record there, and
publish the staged state only if the complete batch succeeds. A record array
passed through the export is borrowed for the synchronous call only.

| Storage | Capacity | Purpose |
|---|---:|---|
| Shared journal slots | 512 | Canonical authoritative/predicted record state, generation-safe slot refs, expiry, presentation, and exact receipt tracking |
| Authority bindings | 1,024 | Ordered authoritative identities, semantic hashes, journal bindings, snapshot fences, and presented/terminal evidence |
| Prediction tombstones | 1,024 | Command-keyed speculative body and reconciliation/cancellation/presentation evidence that survives journal-slot reuse |
| Snapshot event references | 2,048 | Authority-ID presentation dependencies and audit-enabled legacy-carrier join metadata with snapshot tick/time fences |
| Legacy body metadata | 2,048 | Join and fence metadata for bodies owned by the existing cgame presentation history |
| One submitted batch | 512 | Exact public export and canonical V2 event-range ceiling |

The authority binding table may reclaim only presented entries. Reference
storage may reclaim consumed joins. Audit-off legacy-inferred ranges are
validated, counted, and bound to snapshot chronology without allocating a
reference entry, so sustained legal snapshots cannot exhaust diagnostic join
storage. Prediction tombstones are reclaimed only when the consumed-command
retirement watermark proves their keys stale and their
reconciliation/terminal state makes reclamation safe. Capacity pressure that
cannot satisfy the authority rules fails closed instead of silently
discarding a reliable lifecycle dependency.

The runtime keeps detailed, saturating private telemetry for resets, batches,
records, duplicate/conflict/capacity results, receipt state, reconciliation,
retirement, joins, presentations, stalls, expiry, high-water marks, overrun,
and the presentation-chain hash. The transport-facing export exposes only the
small health and receipt subset needed at the ownership boundary.

### Three independent lifetimes

The legacy event range, authoritative event stream, and snapshot timeline do
not share an epoch and cannot reset each other by inference:

- `CG_EventRuntimeResetLegacy(stream_epoch)` clears only legacy body metadata
  and retained legacy-inferred audit joins. The already accepted snapshot
  chronology and exact receipt remain owned by the snapshot lifetime. The
  reset is driven by the existing V2 cgame event-range reset.
- `CG_EventRuntimeResetAuthority(event_stream_epoch, first_sequence)` clears
  authority bindings, prediction tombstones, authority references, the shared
  journal, retirement cursor, receipt, and authority-local health. A non-zero
  reset seeds ordered presentation at `first_sequence` and seeds the receipt
  at `first_sequence - 1`, so joining an already-running stream does not invent
  a gap from sequence one. `{0, 0}` deactivates and scrubs the complete
  authority/prediction domain.
- `CG_EventRuntimeResetSnapshot(snapshot_epoch)` clears snapshot-derived joins
  and time/tick fences while leaving legacy and prediction identities in their
  own domains. If any authoritative record is still unresolved/unpresented,
  losing those fences sets `REQUIRES_RESYNC`; the old event epoch is no longer
  safe to continue.

Toggling `cg_event_runtime_audit` starts a clean legacy comparison window. It
clears only legacy bodies and retained legacy audit joins. Authority
references, snapshot chronology, and the exact receipt remain intact.

### Legacy body ownership remains singular

The existing 2,048-entry canonical cgame presentation history remains the
sole owner of each copied legacy event body. After that history validates and
stores a V2 range, it passes one immutable presentation entry to the runtime.
The runtime copies only the serial, semantic hash, source tick/time/ordinal,
carrier kind, derived dense entity-event ordinal, and join/fence state. It does
not create a second legacy `worr_event_record_v1` body store.

This distinction matters during overwrite and reset: presentation history
continues to own body retention, while the runtime can independently diagnose
a missing join, overrun, or semantic mismatch without creating two competing
presentation authorities. Temporary actions can advance using their source
fence; accepted-frame entity events require their matching snapshot event
reference.

## Authority receipt, ordering, and snapshot joins

### Transactional receipt progression

Every authoritative body is validated against the stable event ABI and hashed
fieldwise before commit. Exact duplicate IDs with equal bodies are idempotent;
the same ID with a different body is a conflict. A batch containing a wrong
epoch, invalid body, conflict, unrecoverable capacity condition, or other
terminal failure leaves all bodies and receipt bits from that batch
uncommitted.

The journal owns a 24-byte selective receipt consisting of the event stream
epoch, `highest_contiguous`, and a 64-bit selective mask. Out-of-order bodies
set selective bits; receiving the missing gap advances `highest_contiguous`
and collapses the covered bits. The receipt is copied into the 48-byte public
status only after the cgame transaction commits. This provides the exact
acceptance proof required by the later native receiver without coupling
receipt identity to packet sequence.

### Ordered release and dual fences

Authority presentation is keyed by the exact next event sequence, never by
arrival or resident order. A later record cannot pass a missing earlier
record. Before a non-terminal authority record can advance, the runtime
requires:

- a snapshot event reference with the same authoritative event ID;
- matching semantic model revision and semantic hash;
- the record's source tick and source time to have been reached; and
- the joining snapshot's fence tick and fence time to have been reached.

Snapshot references and bodies may arrive in either order. References are
stored by authoritative event ID and joined transactionally when the other
half arrives. Legacy-inferred references use snapshot tick plus the dense
event ordinal, while preserving the sparse source ordinal in semantic
evidence. A mismatched semantic version/hash is retained as degradation
evidence and cannot be presented as a valid join.

Expired or canceled transient authority records advance the ordered sequence
as terminal skips even when their snapshot reference never arrived. Once such
a record is terminal it cannot emit a side effect; keeping it at the head of
the stream would permanently block every later event behind an impossible
join. Reliable and persistent records do not gain that lossy escape route.

Snapshot observation occurs only after the immutable cgame snapshot timeline
has accepted and copied the snapshot. Event-runtime failure therefore cannot
roll back a valid timeline publication. It can, however, degrade the active
authority stream and require its own fresh event epoch.

## Prediction, correction, and retirement

Predicted events use the `FR-10-T09` canonical command identity plus emitter
ordinal and prediction lane. That key is deliberately independent of an
authoritative event ID. The runtime supports prediction-before-authority and
authority-before-prediction arrival:

- a semantic match binds the authoritative ID and suppresses duplicate
  authority presentation when the speculative side effect was already
  presented;
- a mismatch before speculative presentation is `CORRECTED`;
- a mismatch after speculative presentation is
  `CORRECTED_AFTER_PRESENTATION`, preserving evidence for the later corrective
  presenter; and
- a prediction key already reconciled to one authoritative ID cannot be
  rebound to a different ID.

Command-immediate predictions are source tick/time fenced and ordered by their
resident insertion order. Deferred predictions do not bypass the authority
gate. Cancellation first resolves a still-resident predicted slot, even when
the same key is at or below the retirement cursor, so a pending event cannot
be resurrected by a stale-key shortcut.

The tombstone side table retains the speculative body, slot generation,
semantic hash, presented/terminal/reconciled state, and the bound authority ID.
That evidence survives journal-slot displacement. Presented-but-unmatched
history is retained for late correction; reconciled history and safely
terminal unpresented history become reclaimable only after a monotonic
`worr_command_cursor_v1` proves the key consumed. Repeating the same retirement
cursor is idempotent and still sweeps entries that became terminal since the
previous call. A regressing cursor is a conflict. A newly submitted prediction
at or below the established cursor is terminal and cannot resurrect a retired
side effect.

Every accepted canonical snapshot with a non-zero consumed-command cursor
automatically retires eligible prediction history through that cursor. This is
the direct `FR-10-T08`/`FR-10-T09` bridge between authoritative input
consumption and bounded predicted-event retention.

## Compact cgame export and engine ownership

### Separate, stable C ABI

The direct seam is the named optional extension
`WORR_CGAME_EVENT_RUNTIME_EXPORT_V1`. It is separate from the legacy event
shadow/range extensions and from the snapshot timeline; those APIs were not
broadened to hide a new lifecycle inside an old contract.

The V1 function table contains:

- `ResetAuthority(event_stream_epoch, first_sequence)`;
- `SubmitAuthoritativeBatch(records, count)`; and
- `GetStatus(status_out)`.

The API is synchronous, process-local, main-thread-only, and version/size
checked. The public status is exactly 48 bytes: ABI metadata, active authority
epoch, next presentation sequence, retained authority count, state flags, and
the 24-byte exact receipt. Detailed join/prediction/presentation telemetry
stays cgame-private. C and C++ layout assertions pin the status offsets,
function-table ordering, result values, flags, and 512-record batch limit.

### Client-engine owner

The client engine discovers the extension through `CG_GetExtension`, validates
the complete table, and installs it in a small owner independent of the DLL's
private state. This owner can remember an inactive or active authority
descriptor before cgame attaches and synchronously replay it at attach time.
An attach/reset is accepted only after `GetStatus` proves the expected clean
inactive state or exact fresh active state.

Every status read is validated before caller output is touched. Validation
requires:

- exact status size and API version;
- no unknown state flags;
- a completely zero inactive authority/receipt state;
- a non-zero active epoch and next sequence;
- exact event receipt size/schema and receipt epoch equality;
- presentation and contiguous-receipt positions consistent with the stream's
  first sequence; and
- agreement with the engine owner's active descriptor.

An unknown callback result, failed status callback, malformed or incoherent
status, failed reset handshake, or cgame-declared `REQUIRES_RESYNC` quarantines
the function table. Quarantine forgets the active descriptor, establishes a
hard-resync barrier, clears the callable pointer, and makes a best-effort
`{0, 0}` scrub through the old table. Subsequent submit/status calls cannot
invoke that table.

### DLL loss, epoch high-water, and stale-receipt exclusion

`CG_Unload()` disconnects the event-runtime consumer before nulling cgame
exports and before `Sys_FreeLibrary`. An active detach or replacement is state
loss, even if the next DLL implements the same ABI. The new DLL is attached in
an inactive state and cannot resume the lost authority epoch.

The engine maintains a strict connection-lifetime event-epoch high-water mark:

- reasserting the exact active `{epoch, first_sequence}` is an owner-side
  no-op;
- changing `first_sequence` within the same epoch is rejected;
- after active cgame loss, every epoch at or below the high-water mark is
  rejected; and
- only a strictly newer event epoch clears the hard-resync barrier.

`CL_ClearState()` performs explicit `{0, 0}` deactivation before clearing the
rest of client state. That full disconnect is the only operation that clears
the descriptor, barrier, and epoch high-water, allowing a later connection to
reuse numerical epoch values safely.

Unresolved snapshot-fence loss follows the same rule. The cgame runtime marks
the active stream `REQUIRES_RESYNC`; the owner refuses to copy that status to a
transport caller, scrubs the runtime to inactive, retains the epoch high-water,
and waits for a strictly newer event epoch. The old status may still contain
the receipt for bodies accepted before fence loss, but it is never returned as
an ACK source after the resync condition is observed. Detach, quarantine, and
disconnect also prevent cached callbacks or a freed DLL table from producing a
receipt. A future receiver must query validated status after each successful
submit and must not emit an ACK from a cached or pre-submit status.

This is the core no-stale-ACK invariant: native RX commit and ACK authority may
advance only after the cgame has transactionally accepted the exact batch and
the owner has validated an active, non-resync status for the same event epoch.

## Implementation surfaces

The milestone is concentrated in these ownership seams:

- `inc/shared/cgame_event_runtime.h`: stable result values, state flags,
  compact status, and V1 export table;
- `src/game/cgame/cg_event_runtime.hpp` and
  `src/game/cgame/cg_event_runtime.cpp`: fixed-capacity runtime, detailed
  diagnostics, authority/prediction/reference joins, ordering, and export;
- `src/game/cgame/cg_event_shadow.cpp`: legacy metadata observation after the
  sole body owner commits a V2 range;
- `src/game/cgame/cg_canonical_snapshot_timeline.cpp`: snapshot-reference
  observation only after immutable timeline publication;
- `src/game/cgame/cg_entities.cpp`: default-off legacy audit control and
  authority advancement on the canonical render clock;
- `inc/client/cgame_event_runtime.h` and
  `src/client/cgame_event_runtime.cpp`: engine-side descriptor, status
  validation, epoch high-water, quarantine, and hard-resync ownership;
- `src/game/cgame/cg_main.cpp` and `src/client/cgame.cpp`: named extension
  publication/discovery and unload ordering; and
- `src/client/main.cpp`: explicit disconnect scrub.

## Focused validation

All focused launches are headless unit executables; no interactive client was
started. The current state-focused selection passes 7/7:

| Test | Principal contract |
|---|---|
| `network-event-journal` | Journal receipt, prediction matching/correction, slot lifecycle, persistent/cosmetic replacement, expiry, and failure atomicity |
| `network-cgame-event-presentation` | Existing legacy body owner, ordered present-once history, reset and overwrite behavior |
| `network-cgame-event-runtime` | Transactional authority, reordered gaps, join arrival orders, prediction matrix, tombstones, cancellation, retirement, source/snapshot fences, terminal skip, strict mismatch, zero scrub, 2,560 audit-off legacy-reference admissions without diagnostic-table growth, event-hash rejection, and inactive-authority fail-closed behavior |
| `network-cgame-event-runtime-export` | Compact table/status, borrowed-input copy, selective receipt, rollback, authority operation with audit off, snapshot-loss resync, and deactivation |
| `network-cgame-event-runtime-owner` | Pre-attach reset replay, detach/reload barriers, strict epoch high-water, table/status quarantine, callback result validation, and no callback after unload |
| `network-cgame-event-runtime-layout-c` | C ABI sizes, offsets, enum/flag values, function-table ordering, and export name |
| `network-cgame-event-runtime-layout-cpp` | Equivalent C++ ABI and standard-layout/trivial-copyability proof |

Repository-wide validation also passes:

- complete networking suite, once: **113/113**;
- complete networking suite, three repetitions: **339/339**;
- complete Windows Clang production build, including client/dedicated engine
  DLLs, cgame, sgame, launchers, updater, and renderer modules; and
- refreshed and validated `windows-x86_64` `.install/`: 16 root runtime files,
  one dependency, a 323-file `basew/pak0.pkz`, 31 botfiles, and 215 RmlUi
  assets.

Focused coverage additionally proves authority-first and prediction-first
reconciliation; correction before and after speculative presentation;
presented prediction and authority evidence surviving slot displacement;
reference-before-body and body-before-reference joins; sparse legacy entity
scan versus dense snapshot ordinal mapping; a mid-stream first sequence;
receipt gap closure; transaction rollback; duplicate retirement sweeps; no
prediction resurrection; active DLL replacement; malformed status fields;
runtime-requested resync; and disconnect reuse of an otherwise stale numerical
epoch.

## Explicit limits

This milestone does not complete `FR-10-T05`, `FR-10-T07`, `FR-10-T08`, or
`FR-10-T14`. Its current limits are:

- no server-originated native DATA adapter resets or submits this export;
- no direct sgame multi-event producer supplies authoritative runtime batches;
- no live cgame local-action producer submits predicted gameplay, audio, or
  effect events;
- the no-effects sink does not replace legacy particles, audio, temporary
  entities, muzzle flashes, or entity-event presentation;
- legacy decode remains the source of production presentation bodies;
- exact receipt state is not yet connected to native retention/retry/ACK;
- the active connection has no negotiated canonical event-stream descriptor;
- demo, spectator, seek, and reconnect behavior has focused lifecycle coverage
  but no native event-stream integration gate;
- no WAN, multi-client load, long soak, bandwidth-pressure, or supported-
  platform matrix has exercised the direct export; and
- the mandatory `FR-10-T14` malformed-input, performance, memory, load, and
  security floors remain open.

## Next gates

The next production slice must define and propagate an explicit canonical
event-stream descriptor containing at least its own event epoch and first
sequence. The event epoch is a separate semantic lifetime and **must never be
aliased to either the native transport epoch or the canonical snapshot
epoch**. Transport rotation must not reset event identity, and snapshot
timeline rotation must not silently mint or resume an authority stream.

With that descriptor established, server-to-client native event delivery must
perform this exact order:

1. reassemble and validate a complete canonical event DATA record/range in
   bounded scratch storage;
2. establish or validate the explicit event-stream descriptor;
3. synchronously submit the complete batch through the engine owner;
4. read and validate the post-submit compact status;
5. commit native RX/retention state only after cgame commit is proven; and
6. derive an ACK only from that same-epoch post-commit receipt.

Any submit failure, quarantine, DLL loss, status mismatch, snapshot-fence
resync request, or epoch disagreement must leave native acceptance and ACK
state uncommitted. Recovery requires a hard resync with a strictly newer event
epoch, not replay of the lost descriptor.

After transport delivery is exact, the remaining promotion sequence is:

1. add direct multi-event authoritative producers without passing through the
   legacy body history;
2. connect the existing `FR-10-T09` local-action transaction to predicted
   runtime submission and consumed-command retirement;
3. implement typed live side-effect presenters with match suppression and
   correction policy;
4. prove present-once parity and bounded correction behavior before disabling
   the corresponding legacy presenter; and
5. run deterministic loss/reorder/duplication/corruption, bandwidth, demo,
   multi-client load, soak, malformed-input, performance, and cross-platform
   gates required by `FR-10-T14`.

`FR-10-T04` transport work must reuse this cgame authority seam and the shared
event ABI; it must not create a second transport-owned event journal.
