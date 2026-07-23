# Progressive Networking, Events, Snapshots, Prediction, and Lag Compensation Roadmap

Date: 2026-07-11

Status: Living roadmap. The N0 architecture contract is accepted and its first
wire-compatible foundation is implemented. `FR-10-T01`, deterministic shared
prediction `FR-10-T02`, the virtual-link harness `FR-10-T03`, typed event
journal `FR-10-T05`, acknowledged-baseline snapshot system `FR-10-T06`, and
canonical command identity `FR-10-T09`, bounded timestamped rewind
`FR-10-T10`, and authoritative hitscan policy `FR-10-T11` are complete.
This is 8/16 FR-10 tasks complete; the remaining eight tasks retain the
transport, timeline, broader-gameplay, ecosystem, stress, release, and adaptive
delivery scope described below.
Canonical transport and timeline work continues in shadow-first stages.

Primary tasks: `FR-10-T01`, `FR-10-T02`, `FR-10-T03`, `FR-10-T04`,
`FR-10-T05`, `FR-10-T06`, `FR-10-T07`, `FR-10-T08`, `FR-10-T09`,
`FR-10-T10`, `FR-10-T11`, `FR-10-T12`, `FR-10-T13`, `FR-10-T14`, and
`FR-10-T15`, plus the post-prediction delivery task `FR-10-T16`.

Strategic parent:
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

Historical predecessor:
`docs-dev/proposals/event-system-migration.md`.

## Purpose

This is the go-forward engineering plan for a progressive WORR networking
architecture that supports deterministic client-side prediction, resilient
snapshot delivery, typed sequenced events, and extensive but fair server-side
lag compensation.

The intended result is not a direct Quake 3 port and not a wholesale protocol
rewrite. WORR will use a hybrid design:

- Preserve the proven Quake II and Quake II Rerelease compatibility paths.
- Adopt the useful idTech3 separation between shared simulation, snapshots,
  commands, events, and client presentation.
- Introduce a modern canonical state model above the wire protocol layer.
- Progressively replace cgame's direct access to mutable engine state with
  validated, immutable snapshot, input, and event ranges.
- Leave room for a future WORR-native transport without making it a prerequisite
  for the prediction, event, timeline, or lag-compensation improvements.

This document records intended work and acceptance gates. The bounded legacy
metadata/event seams remain, and the live progressive pipeline now includes
client canonical projection, server final-emission snapshot refs, exact
simulation time, a cgame-owned immutable timeline and durable event journal,
default-off per-entity transform promotion, negotiated command identity and
consumed-cursor transport, authoritative movement replay, a shared local-action
  convergence model, callback-scoped authenticated command context, common frozen
  scenes with bounded player and mover pose capture, and bounded per-query
  rewind observations for supported hitscan. These slices do not imply that
  canonical effect/audio presentation, live gameplay-action prediction, the
  native WORR envelope, extended historical-brush coverage, extended compensation
  policies, or release gates are complete.

## Mandatory Constraints

- `q2proto/` is read-only for this initiative unless a later explicit request
  authorizes changes there.
- A future WORR-native wire transport must live outside `q2proto/` and negotiate
  independently.
- WORR clients must retain compatibility with legacy Quake II servers and legacy
  demos throughout the migration.
- Existing server protocol paths must remain available for legacy clients where
  they are supported today.
- Temporary reports, packet captures, fuzz corpora, and benchmark evidence belong
  under `.tmp/networking/`.
- Every build that changes binaries or packaged assets must end by refreshing
  `.install/`.
- New public controls must follow subsystem ownership: `net_` for shared network
  behavior, `cl_` for client-engine policy, `sv_` for server-engine policy,
  `cg_` for cgame presentation/prediction controls, and `sg_` for sgame lag-
  compensation or gameplay fairness controls. New `g_` names are reserved for
  legacy compatibility aliases.

## Corrected Current Baseline

The older event migration proposal describes snapshot deltas as if WORR always
deltas from the immediately preceding server frame. That is stale. The current
engine already selects snapshot delta bases from frames acknowledged by each
client and falls back to a non-delta frame when the requested base is invalid or
too old. This roadmap does not propose reimplementing that existing behavior.

`FR-10-T01` completed the source audit and accepted the above-wire ownership,
identity, time, compatibility, budget, security, rollout, and rollback
contracts. Versioned pointer-free command, event, and snapshot models,
immutable stores/ranges, and a deterministic impairment harness now implement
those foundations. The remaining work is to finish live producer/consumer
integration, carry the accepted models over an exact-receipt native adapter,
promote the cgame/sgame paths progressively, and collect the mandatory live,
load, soak, malformed-input, and platform evidence.

Other established foundations include:

- Movement prediction and prediction-error handling have already migrated into
  `src/game/cgame/cg_predict.cpp` behind the cgame entity API.
- Entity interpolation, effects, and temp-entity handling are already primarily
  cgame responsibilities. The completed `FR-10-T01` audit inventories the
  remaining live engine fallbacks and direct-pointer dependencies; later tasks
  still own their progressive migration.
- `src/game/bgame/` is compiled into cgame and sgame and is the correct home for
  deterministic shared simulation rules.
- Legacy protocol decoding and encoding are centralized through the existing
  engine and `q2proto` integration.
- Canonical command, event, and snapshot ABIs plus caller-owned immutable
  storage/range APIs are established without changing the legacy wire format.
- Typed event journaling and deterministic latency/loss/jitter/reorder/
  duplication fault injection are established at common-core and staged
  default-off integration scope.

The following remaining capabilities are not established and must not be
assumed complete:

- Complete authoritative and predicted multi-event production, matching, and
  live at-most-once presentation through cgame.
- General deterministic replay of all locally controlled gameplay state.
- Complete player-and-mover historical scenes and weapon/mechanic integration.
- An advertised WORR-native exact-receipt carrier with live client/server
  adapters and dual-adapter parity.
- Remaining mandatory malicious-input, multi-client load, soak, bandwidth,
  and supported-platform acceptance gates.

## Architecture Decision

### Hybrid Above-Wire Canonical Model

The stable architectural seam is above the wire protocol, not inside cgame and
not inside `q2proto`.

```text
legacy Q2/Q2R wire                 future WORR wire
        |                                |
 existing q2proto adapter        separate WORR adapter
        |                                |
        +------ engine validation -------+
                       |
           canonical immutable model
        snapshots | inputs | events | time
                       |
        +--------------+---------------+
        |                              |
 deterministic bgame              engine/server
 simulation rules                 history/QoS
        |
 cgame transition, interpolation,
 prediction, reconciliation, and event playback
```

Both wire families must decode into the same canonical model. Cgame must not
branch on protocol field layouts. Sgame and shared simulation must not write
wire messages directly for new functionality.

### Ownership Boundaries

| Layer | Owner | Responsibilities | Must not own |
|---|---|---|---|
| Wire adapters | Engine protocol layer | Decode/encode negotiated wire formats and normalize protocol-specific representations | Prediction, presentation, or weapon fairness policy |
| Validation and canonicalization | Engine common/client/server | Bounds, counts, sequence windows, timestamps, entity identity, immutable storage, lifetime and generation checks | Cgame effects or sgame damage decisions |
| Shared deterministic simulation | `bgame` | Command interpretation, deterministic movement/state transitions, shared event derivation rules and state comparison | Transport negotiation or rendering |
| Server authority | Server engine plus `sgame` | Command acceptance, authoritative simulation, pose history, rewind queries, damage validation and fairness policy | Client visual smoothing |
| Client game | `cgame` | Snapshot transitions, time interpolation, bounded extrapolation, local prediction/replay, reconciliation, event matching and playback | Raw packet parsing or mutable server storage |
| Presentation | Cgame/render/audio/UI | Visual smoothing, effects, sound and diagnostics derived from canonical state/events | Authority or hit acceptance |

### Immutable Module API

The cgame bridge will progressively replace direct pointers to mutable engine
containers with immutable, generation-checked views. Final names and layouts are
an output of `FR-10-T01`; the required semantic contracts are:

- **Snapshot view:** server tick/time, acknowledged input sequence, validity and
  discontinuity flags, player state, sorted entity-state range, area/PVS data,
  and the event range associated with the transition.
- **Snapshot pair/timeline view:** explicit previous/current generations and
  interpolation fraction inputs. Missing or invalid prior state is represented
  explicitly rather than through a null pointer into engine storage.
- **Input range:** ordered immutable user commands with sequence numbers,
  simulation duration, client sample time, and acknowledgement state.
- **Event range:** ordered typed events with stable identity, source tick,
  subject, parameters, delivery class, and prediction-matching metadata.
- **Lifetime:** a view is valid only for the documented callback or acquired
  generation. Cgame may copy durable state but may not retain raw engine pointers
  across generation changes.
- **Validation:** counts, indexes, ranges, sequence arithmetic, timestamps, and
  all protocol-derived values are validated before a view becomes observable to
  a module.

The migration is progressive. Existing bridge fields remain until their
consumers have parity coverage; removal is a later task gate, not a first-slice
goal.

### Time and Sequence Model

- Internal canonical time uses monotonic server ticks plus a sufficiently wide
  monotonic duration representation. Wire wraparound is normalized at the
  adapter boundary.
- Input, snapshot, and event sequences use explicit wrap-safe comparison
  helpers. Raw signed subtraction is not an accepted ordering test.
- The client maintains a filtered server-time estimate separately from render
  time and local input sample time.
- Teleports, respawns, map changes, large mover discontinuities, and authoritative
  corrections carry explicit discontinuity markers. They never interpolate or
  rewind through an invalid state boundary.

### Typed Sequenced Events

Events are data, not transient writes into presentation code. The canonical
event model must distinguish at least:

- **Predictable events:** derivable from local deterministic input and matched
  against later authority without replaying audiovisual side effects.
- **Authoritative transient events:** snapshot-associated and sequenced for
  at-most-once client playback.
- **Durable/reliable control events:** important state transitions that require
  acknowledgement or representation in durable replicated state.
- **Cosmetic events:** explicitly eligible for coalescing or loss under a defined
  budget; they must never be silently confused with gameplay authority.

Every event needs a stable type, source tick, source/subject identity, sequence
or unique key, bounded typed payload, and declared delivery/prediction class.
Cgame owns matching, deduplication, transition dispatch, and presentation. Wire
adapters only serialize or normalize it.

### Snapshot Model

The canonical snapshot is a complete logical view even when the wire message is
a delta. The engine owns reconstruction and validation before cgame sees it.

Required properties include:

- Client-acknowledged delta bases remain the normal legacy behavior.
- Invalid, stale, missing, or overwritten bases trigger a safe full/key frame;
  they never expose a partially reconstructed snapshot.
- Entity creation, update, removal, visibility changes, and identity reuse have
  explicit semantics.
- Snapshot history is bounded by count, age, entity storage, and per-client rate.
- Oversized snapshots use application-level prioritization/fragment policy and
  do not rely on IP fragmentation for correctness.
- Cgame receives immutable entity ranges and explicit transition metadata rather
  than the engine's mutable packet-entity ring.

### Prediction and Reconciliation

Full client-side prediction means replaying all locally controlled state that is
both gameplay-relevant and deterministic, not merely extrapolating an origin.
The target includes movement, view state, stance, predictable weapon state,
selected predictable interactions, and their event identities where shared
simulation rules exist.

The client:

1. Starts from the last authoritative state and acknowledged input sequence.
2. Replays unacknowledged immutable input commands through shared `bgame` rules.
3. Matches predictable events by identity instead of replaying their effects.
4. Classifies reconciliation differences by cause and magnitude.
5. Applies authority immediately to collision/gameplay state while optionally
   smoothing only the rendered presentation offset.
6. Hard-resynchronizes when command history, map generation, entity identity, or
   deterministic invariants are no longer valid.

### Bounded Full-Pose Server Rewind

Lag compensation is an authoritative historical query, not client-authorized
damage and not a general rollback of the live world.

The history records timestamped full poses needed for a fair collision query:

- Origin, orientation, bounds, stance, solidity and relevant collision flags.
- Ground and mover attachment plus the mover transform needed to reconstruct the
  player's pose.
- Teleport, respawn, death, map-generation and other discontinuity markers.
- A stable entity identity/generation so a reused slot cannot be rewound as the
  wrong actor.

The server maps a validated command timestamp into the bounded history window,
interpolates only between compatible samples, clamps or rejects invalid/future/
excessively old requests, performs the historical trace, restores no mutable
live state, and applies damage only through current authoritative sgame rules.

Hitscan is the first integration target. Projectile, melee, radius damage,
movers, deployables, triggers, and cooperative interactions follow only after
their fairness semantics and validation scenarios are explicit.

## Current Progressive Implementation State

The architecture, deterministic shared-movement, virtual-link, typed-event,
acknowledged-baseline snapshot, canonical command-identity, bounded-rewind, and
authoritative-hitscan tasks (`FR-10-T01` through `FR-10-T03`, `FR-10-T05`,
`FR-10-T06`, and `FR-10-T09` through `FR-10-T11`) are complete (8/16).
The remaining tasks are advancing through bounded shadow-first, audit, and
default-off slices; the implemented surfaces below do
not by themselves satisfy their parent tasks' acceptance or release gates.

- Inventoried current event, snapshot, prediction, command, and module-pointer
  ownership and recorded the accepted contracts (`FR-10-T01`).
- Replaced the mirrored cross-DLL PMove layout with a validated, exactly typed,
  pointer-free prediction ABI; cgame and sgame link one strict-FP movement core,
  and replay/wire/state-hash parity closes `FR-10-T02`.
- Added a deterministic virtual-link model, checked-in impairment matrix and
  golden, production staging control, repeatability runner, and ordinary Meson
  entrypoints for latency/loss/jitter/reorder/duplication and boundary coverage
  (`FR-10-T03`).
- Added typed event and snapshot stores without changing the live wire format,
  including client decode-order V2 event ranges, a durable cgame presentation
  journal, complete legacy snapshot projection, exact server final-emission
  refs, final per-peer legacy-entity event candidate extraction, and a cgame-
  owned immutable timeline. A separate default-off native event shadow now
  carries those exact per-peer candidates through the cgame authority runtime;
  legacy snapshots and effect/audio presenters remain authoritative. Remote
  transform promotion and canonical-failure-driven full-snapshot requests
  remain default-off
  (`FR-10-T05`/`FR-10-T06`/`FR-10-T07`).
- Added an explicit wrap-safe command ID, independent received and consumed
  cursors, validated command/sample/render timing, fieldwise hashes, and a
  bounded caller-owned command stream. Negotiated packet-scoped command and
  consumed-cursor sidebands are now live over compatible legacy carriers;
  authoritative simulation advances the cursor, snapshots and demos retain it,
  and cgame prediction plus rewind consume it. Authenticated transport gaps no
  longer inherit the retention-ring ceiling: an O(1) distance proof, 4,096-
  command policy cap, bounded synthesis, and transactional fast-forward now
  have unit/core coverage for the reproduced 161/401-command failures;
  a 50,001-frame live stress reached its terminal marker after 24 fast-
  forwards/10,229 skips, and the independent 100,000-target snapshot gate
  remained connected. A dedicated headless machine-readable acceptance gate
  now invokes the production large-loss `SV_WorrFillCommandGap` branch for the
  reproduced 161- and 401-command ranges, requiring exact consumed/received
  cursors, one successful bounded fast-forward per range, and zero policy/core
  rejections (`FR-10-T09`/`FR-10-T16`). Packet-path, client-age, native, load,
  and cross-platform evidence remain open.
- Added a stateful client canonical server clock plus validated,
  client-generated exact-time anchors for every backup frame in an in-memory
  seek snapshot. Stored-snapshot selection replaces clock/projection lineage
  even for a user-visible forward seek, while sequential forward skipping
  preserves continuity (`FR-10-T06`/`FR-10-T07`/`FR-10-T13`).
- Added server-validated snapshot-to-simulation clock mapping, timestamped
  common full-pose player and mover history, discontinuity handling, tri-state
  canonical command scope, and a non-mutating sealed historical player/brush
  collision scene (`FR-10-T10` live default-off scope).
- Routed supported hitscan convergence and collision queries through the
  sealed historical scene with per-ray ignore identities while applying damage
  only to current authoritative state. Bounded observations and a repeatable
  rewind evidence runner cover the current common-core policy surface
  (`FR-10-T10`/`FR-10-T11` live default-off scope).
- Added an unadvertised native-envelope core that fragments opaque canonical
  references within a 1,200-byte datagram ceiling and a bounded session core
  with exact acknowledgements, retained reliable messages, supersedable
  snapshots, frozen retry fragmentation, replay identity, and lost-ACK
  recovery. Transactional allocation-free byte codecs now encode/decode the
  accepted command V1, event V1, and snapshot V2 models under the envelope's
  131,072-byte payload ceiling. A default-off adaptive input controller drives
  only the existing batched legacy move path. Exact-receipt carrier cores,
  post-assembly TX and admitted-RX netchan seams, a time-aware endpoint barrier
  with an adjacent-setting carrier, and a default-off canonical command shadow
  now exist. Default-off production client/server adapters bind those pieces to
  eligible NEW-channel connections behind `cl_worr_native_shadow=0` and
  `sv_worr_native_shadow=0`. Negotiated private epoch-cancellation bit `0x40`
  is now mandatory: command mode binds exact `0x53`, while separate
  `cl_worr_native_event_shadow=0` and `sv_worr_native_event_shadow=0` controls
  opt eligible base-shadow peers into exact private `0x73` with a client-active
  confirmation. Separate `cl_worr_native_snapshot_shadow=0` and
  `sv_worr_native_snapshot_shadow=0` controls select exact private `0x57`,
  bind the canonical snapshot epoch, and use the same fourth confirmation.
  Enabling both event and snapshot controls now selects exact private `0x77`.
  Its fair DATA scheduler alternates eligible event and snapshot work, while
  disjoint low/high message-sequence lanes keep semantic ACK authority isolated
  by record class. The public capability offer/confirmation mask remains
  exactly `0x03`. Legacy `MOVE`/`BATCH_MOVE` remains the sole authority. The pilot now carries a
  repeated bounded stop-and-wait stream: it retains one exact-range
  `WTC1(DATA(WNE1(WNC1)))` observation, waits for an exact ACK and release, then
  may select a newer command. Exact matched slots retire immediately, a
  canonical high-water rejects fresh-identity replay, and old-bank drain occurs
  before current-bank replay checks. Three two-process latency runs match,
  acknowledge, and release 152, 150, and 151 commands respectively (453 total)
  with zero final retention, mismatch, rejection, drain, or failure. An
  additive common core can transactionally compose one DATA fragment with up
  to seven ACK ranges. The `0x73` event mode now uses that core at both live
  adapters: exact final-emission legacy-entity candidates remain ID-less until
  the per-peer descriptor is acknowledged, then a reliable sender assigns
  semantic IDs and emits server DATA alongside command ACKs. Client command
  DATA can carry semantic event ACKs. A fresh negotiated challenge now
  transactionally cancels and counts every lower activated or pending-readiness
  epoch; fresh current state replaces the former retired-bank rollover.
  A fresh cgame status/receipt is required before any semantic ACK becomes
  authoritative. A bounded Stage D snapshot transaction now also treats
  snapshot DATA as semantic: it decodes one complete reconstructed WNC1 V2
  view, proves the exact snapshot ID plus five independently reconstructed
  cross-endpoint hashes (legacy parity and player/entity/area/event semantics),
  stages RX/ACK state on private copies, and publishes only after the cgame V2
  timeline export returns a fresh exact timeline, decoded native endpoint
  hash, and healthy event-fence receipt. Exact committed repeats revalidate
  that receipt
  before ACK rearm, while uncertainty retires semantic receipts, quarantines
  the epoch, and requires a keyframe. An actual server final-emission view
  first passes a reversed multi-fragment virtual link through the real
  default-off cgame timeline and event runtime with exact five-proof parity
  plus a locally exact native endpoint receipt.
  The separate default-off private `0x57` production path now binds the
  canonical snapshot epoch independently through 15-pair readiness, copies
  the exact final per-peer projection into a bounded two-bank sender, uses the
  real server and client post-assembly/admission netchan hooks, defers client
  commit until the legacy projector supplies the exact expectation, gives the
  native receiver sole canonical-timeline publication ownership for that
  epoch, and returns the semantic ACK through the reverse mixed carrier.
  Its registered production-hook virtual link deterministically covers
  four-fragment loss/reorder/duplication, deferred expectation, one lost ACK,
  exact repeat revalidation, exactly-once cgame publication, retention
  release, hash quarantine, and wrong-epoch rejection. Legacy-inferred event
  references earn the fence independently of the optional comparison audit.
  A fixed-seed serialized production corpus now repeats that path for 100,000
  positive snapshots across four epochs through WNC1/WNE1/WTC1, production
  `NetImpair`, receiver/admission, sole cgame timeline publication, locally
  constructed cursor-bound prediction resolution/selection, semantic ACK, and
  exact server release. Two normalized runs match golden digest
  `ba519ae7bdd1db74`; three additional corrupt-carrier probes reject. The
  controlled-generation and exact-`0x57` fixture rebaseline is forensically
  recorded in
  `docs-dev/fr-10-networking-suite-rebaseline-and-resource-isolation-2026-07-20.md`.
  A production-linked focused continuation now carries one admitted native
  snapshot through the extracted shipped engine V2 request constructor, an
  immutable slot-generation/event-fence admission receipt, canonical authority
  selection, real cgame `CL_PredictMovement`, shared PMove, prediction-ring
  hashes, and reconciliation. It proves pending-to-finalized parity, bounded
  and threshold corrections, the legal 127-command range, fail-closed
  128-command exhaustion, and one timeline-only publication that remains
  diagnostic but cannot seed prediction. Its deterministic result includes
  `receipt_fence_blocks=1`, `v2_replays=4`, `oracle_matches=3`, and digest
  `abf2723d5f03cbe9`.
  Client keepalive and server async scheduling wake for native DATA/retries
  and current receipts. Live native gameplay/render authority, public
  advertisement, additional event families, the production-model/golden
  impairment matrix, broader ACK-exhaustion/rotation evidence, and
  release-scale dual-adapter acceptance remain open
  (`FR-10-T04`/`FR-10-T06`/`FR-10-T16`).
- Added a transport-neutral local-action convergence core with stable gameplay,
  audio, and effect keys, plus a bounded prediction/authority audit ring for
  exact-command pairing and correction evidence. Allocation-free O(n)
  operational validation is now separate from explicit whole-ring deep audit,
  and maximum-capacity benchmarks enforce 500 microsecond hot-path and 10 ms
  destructive-lifecycle budgets. It remains diagnostic-only until connected to
  cgame/sgame weapon state (`FR-10-T08`/`FR-10-T14`).
- Added a versioned engine-to-game immutable inline-BSP collision provider that
  traces at caller-supplied historical transforms without editing or relinking
  live edicts. A generated real IBSP38 collision fixture now matches `SV_Clip`
  for translated/rotated ray and box cases while guarding live edict/link bytes.
  Sgame now captures bounded mover poses and player mover-relative provenance
  into sealed scenes, excludes matching live movers from the current baseline,
  and traces their sealed historical BSP transforms without mutating an edict.
  A dedicated-server gate now captures an actual fixture mover, seals its
  scene, relocates only its live collider, proves the baseline is clear and
  the immutable historical provider blocks, and guards/restores authoritative
  collision state across three identical fresh-process runs. Its V5 row arms a
  real engine bot on a start-on rotating `SOLID_BSP` pusher, lets twelve normal
  server frames move it, requires paired player/mover capture continuity, then
  freezes the newest exact pair as a sealed canonical scene with mover
  provenance intact. Broader player-on-mover physics, rotating/complex-map
  fairness, load budgets, and authoritative promotion remain open (`FR-10-T10`).

It does **not** authorize edits under `q2proto/`, a new wire protocol, removal of
legacy parsing, or broad direct-pointer removal. It preserves the existing
`g_lag_compensation` compatibility switch, but keeps the new historical path
default-off through rollout stage R4. The configured rewind target defaults to
200 ms under a 250 ms hard public-policy ceiling. All open parent tasks remain
open until their full Definitions of Done are met.

## Delivery Phases

| Phase | Tasks | Outcome | Exit gate |
|---|---|---|---|
| N0: Baseline and safe foundations | `FR-10-T01`, `FR-10-T02`, foundations of `FR-10-T05`, `FR-10-T07`, `FR-10-T10`, `FR-10-T11` | Accepted architecture, deterministic movement prediction, inventories, wire-compatible APIs and historical query foundation | Existing server/demo behavior passes; no wire change; foundation logs and evidence exist |
| N1: Deterministic model and test harness | `FR-10-T02`, `FR-10-T03`, `FR-10-T05` | Shared deterministic state/event rules and repeatable impairment tests | State/event replay is deterministic and event loss/duplication cases are covered |
| N2: Canonical snapshots, command identity, and cgame timeline | `FR-10-T06`, `FR-10-T07`, `FR-10-T09` | Validated immutable snapshots, explicit consumed-command watermarks, transitions, interpolation and event playback over legacy transport | Shadow parity, packet-loss/base-invalidation/visibility/discontinuity, and command-wrap tests pass |
| N3: Full local prediction and adaptive delivery | `FR-10-T08`, `FR-10-T04`, `FR-10-T16` | Complete command replay first, then negotiated WORR transport and adaptive pacing/batching/redundancy | Bounded corrections and command recovery pass the impairment matrix on both adapters |
| N4: Extensive authoritative lag compensation | `FR-10-T10`, `FR-10-T11`, `FR-10-T12` | Full-pose rewind, hitscan then extended gameplay coverage with fairness controls | Weapon-specific deterministic scenarios, abuse cases and performance budgets pass |
| N5: Dual-stack ecosystem and release hardening | `FR-10-T13`, `FR-10-T14`, `FR-10-T15` | Demos/spectators, telemetry, staged rollout and operator documentation | Compatibility matrix, soak/load, rollback and staged release gates pass |

The phases overlap only where interfaces are stable enough to keep work
independent. The WORR transport is downstream of the canonical event, snapshot,
and command models: it serializes those accepted models and must not introduce a
second transport-owned gameplay schema. Adaptive delivery follows working
prediction so pacing policy can be evaluated against authoritative replay and
correction evidence.

## Task Register and Definitions of Done

### `FR-10-T01` Current-state audit and architecture contract

- Area: `inc/client`, `inc/common`, `inc/server`, `src/client`, `src/server`,
  `src/game/bgame`, `src/game/cgame`, `src/game/sgame`.
- Priority: P0.
- Dependencies: none.
- Current state: Done on 2026-07-11.
- Evidence: `docs-dev/progressive-networking-foundation-2026-07-11.md`.
- Definition of Done:
  - Live data flows and mutable-pointer dependencies are inventoried from source.
  - The above-wire canonical model, ownership matrix, time/sequence rules,
    compatibility promises, budgets, security boundary, rollout gates, and
    rollback conditions are accepted and documented.
  - Stale statements in predecessor documents are identified without rewriting
    history as if the new system already exists.

### `FR-10-T02` Deterministic shared `bgame` simulation and state schema

- Area: `src/game/bgame`, cgame and sgame integration.
- Priority: P0.
- Dependencies: `FR-10-T01`.
- Current state: Done on 2026-07-12.
- Evidence:
  `docs-dev/networking-deterministic-prediction-core-2026-07-12.md`.
- Progress: cgame and sgame link one strict-FP C++20 movement core behind a
  versioned pointer-free ABI. Canonical command adapters cover Vanilla, Q2PRO,
  and Q2REPRO MOVE/BATCH_MOVE. Exact state, configuration, collision-transcript,
  and replay-chain hashes match through correction and sequence-wrap cases.
  Public game/cgame APIs are `2025`/`2028`, so stale mixed binaries fail closed.
- Definition of Done:
  - Shared state and command rules use deterministic time and explicit inputs.
  - Client and server replay produce matching state hashes for the supported
    prediction surface across sequence wrap and correction cases.
  - Nondeterministic presentation, allocation, wall-clock, and renderer data are
    excluded from the simulation contract.

### `FR-10-T03` Deterministic impairment, fault-injection, and baseline harness

- Area: `tools/networking`, engine test hooks, `.tmp/networking` evidence.
- Priority: P0.
- Dependencies: `FR-10-T01`.
- Current state: Done on 2026-07-12.
- Evidence:
  `docs-dev/networking-deterministic-impairment-harness-2026-07-12.md`.
- Definition of Done:
  - Tests can deterministically inject latency, jitter, loss, burst loss,
    reordering, duplication, corruption, throttling, upstream stalls, stale and
    future acknowledgements, and legacy sequence-exhaustion boundaries.
  - Checked-in golden artifacts detect deterministic model drift; a staged
    default-off control proves existing raw routing remains active.
  - Production model, queue, clock, and sequence primitives have ordinary unit
    tests, a non-interactive CI entrypoint, and machine-readable evidence.

### `FR-10-T04` Negotiated WORR transport envelope and canonical adapters

- Area: engine common/client/server networking outside `q2proto/`.
- Priority: P0.
- Dependencies: `FR-10-T03`, `FR-10-T05`, `FR-10-T06`, `FR-10-T09`.
- Current state: In Progress at capability negotiation plus native envelope,
  session/retention, canonical byte-codec, WTC1 packet-carrier, transport-
  confirmed dispatch, retained-receipt cores, post-assembly TX and post-
  admission RX netchan seams, endpoint-readiness proof/carrier, default-off
  production adapters, a repeated stop-and-wait canonical command shadow, a
  production-linked mixed DATA/ACK coordinator, and a canonical event-stream
  descriptor/transactional admission core with an observation-bound semantic
  repeat and ACK-authority fence. A separate default-off `0x73` mode now adds
  exact per-peer legacy-entity event production, descriptor-before-ID reliable
  retention, live full-duplex mixed packing, transactional epoch cancellation,
  and scheduler liveness. An unarmed endpoint still offers exact legacy
  `0x03`. An armed command endpoint offers and readiness-binds exact `0x53`,
  event mode exact `0x73`, snapshot mode exact `0x57`, and the implemented
  combined mode exact `0x77`; an endpoint mismatch, partial intersection, or
  unavailable native endpoint falls back to exact `0x03`. A deterministic
  in-process virtual link now drives the real
  client/server production hook callbacks through directional DATA loss,
  reorder, duplication, one-way ACK loss, corruption, and lifecycle traffic.
  Its multi-event schedule now also proves that an out-of-order semantic event
  produces a selective client receipt which releases only that later event;
  the earlier lost event stays retained until its retry closes the gap.
  The first additional producer now observes accepted structured per-client
  spatial-audio writes only after the exact snapshot emission and successful
  legacy append. It covers the final unreliable writer, reliable
  `SV_StartSound`, and transient/reliable positionless `PF_LocalSound`: a
  visible source binds to that snapshot's entity generation, an explicitly
  positioned off-frame source binds to world with `POSITION_FORCED`, and a
  client-local cue binds to world with `LOCAL_ONLY` while retaining its raw
  override key. Reliable events are non-expiring `RELIABLE_ORDERED` records.
  A second producer observes one bounded, ordered direct-game sequence of up
  to 16 supported temporary-entity and player/monster/rerelease-muzzle
  carriers. Unreliable messages bind only after fitting the post-snapshot
  datagram. Reliable messages are decoded only after the authoritative
  netchan append, retained in a native-only 32-batch/512-record FIFO with map
  generation, and bound once to the next exact committed snapshot before
  frame-derived and unreliable candidates. Reliable members are non-expiring
  `RELIABLE_ORDERED` records. Every batch rebuilds through the shared mappers
  and publishes atomically only when all visible source/subject generations
  bind. Exact per-client damage indicators now also enter the native shadow
  only after their bounded unreliable message fits after the final snapshot;
  world remains the source and the exact controlled-player generation is the
  subject. Per-client `svc_help_path` markers now follow the same accepted-
  write boundary: one exact finite float-position/packed-direction carrier
  becomes a world-sourced canonical help-marker effect whose subject is the
  exact controlled-player generation. Exact keyed `svc_poi` carriers now use
  a shared 20-byte canonical value, world source, and the exact controlled-
  player subject. Reliable carriers share one bounded FIFO with reliable game-
  event sequences and remain distinct even when keys repeat. Unreliable
  carriers reject only the zero-lifetime infinite form; every other lifetime,
  including the keyed-removal sentinel, enters as a one-tick transient.
  Reliable finite, zero-lifetime, and removal forms remain ordered reliable
  actions. Slice closeout passes the 11/11 T04 focused parent with 123 stable
  source bindings, the final 201/201 headless networking suite, 16/16 package
  tests, 12/12 release-unit tests, and the 1/1 headless bootstrap contract;
  T04 correctly remains partial. Arbitrary
  non-local positionless sounds,
  remaining unsupported service families, raw direct-game `svc_sound` byte
  spans, and non-visible game-event sources remain legacy-only until a safe
  structured final-emission carrier exists.
  Map-quiesced client `DRAIN` still services already-authorized semantic ACKs
  while native DATA stays frozen before the fresh challenge barrier. A
  bounded native snapshot semantic-admission core, exact private `0x57`
  readiness binding, and production client/server snapshot TX/RX adapters now
  connect the final server projection to sole native publication in the cgame
  canonical timeline. The registered real-hook virtual link covers
  directional loss, reorder, duplication, deferred expectation, one-way ACK
  loss, repeat revalidation, retention release, hash quarantine, and
  wrong-epoch rejection. The combined `0x77` continuation now exercises event
  and snapshot DATA simultaneously with fair lane selection and class-isolated
  ACKs; three two-process runs match 99/99 local-action receipts and show both
  snapshot peers ACKing and releasing retained frames with zero endpoint
  failure. The task-level exact-bundle parent now binds the literal `0x03`/
  `0x53`/`0x73`/`0x57`/`0x77` policy, fixed focused children, real-process live
  lanes, the staged runtime closure, relevant source inputs, and the clean
  `q2proto/` tree. Its first full run correctly failed on four mode-3
  frames observed before the engine's first snapshot-ownership bind. Production
  presentation now remains legacy-authoritative during that initial wait,
  latches on the first positive bind, fails closed on same-stream ownership
  loss, and reopens the pre-bind wait only from the explicit `InitCGame` stream
  reset. Fresh full run `20260720T035616.104289Z-41896` passes all `10/10`
  focused and `4/4` live children with semantic digest
  `a73979dfe68b99388159583d8b4eebfe91ed29c0c0f54ee3a4332b3dc4184706`;
  exact snapshot `0x57` reports 1,024 native samples and zero authority block,
  while combined `0x77` completes its real reconnect and both snapshot peers
  ACK/release with zero queue/rejection/retirement fault. This remains partial
  task evidence (`task_complete=false`). The schema-v36 continuation makes the
  event-only child capture both live client status rows and both server-peer
  rows after reconnect/receipt parity, and the T04 parent independently requires
  exact public/private `0x73` before it can label that lane
  `direct-client-and-server-status`. It also adds a direct legacy `0x03` row and
  a combined preflight over both clients, both server slots, and both snapshot
  peers before arming gameplay. Pre-capacity artifact
  `.tmp/networking/fr10_t04_ordered_frontier_full.json` passes its then-current
  10/10 focused and 5/5 live manifest with semantic digest
  `521cdaabad088cb6e89b0ad56d89cf2dcf2bcb8194ec69d3ed078b61a84a32a7`.
  A subsequent manifest hardening adds cgame local-action correlation as an
  eleventh focused child and binds the prediction-ring, event-sender, event-ABI,
  and mailbox constants used by the derived capacity; the recorded 10-child
  artifact is not relabeled as evidence for that change.
  The private runtime now owns an ordered reconciliation cursor independent of
  visual fencing, fails closed on callback rejection/mutation reentry, handles
  `UINT32_MAX` terminally, rolls receipt-absent command history safely, prunes
  already-matched terminal pairs behind a monotonic receipt frontier, and
  explicitly drains typed server mailbox failure as native failure `18`.
  Controlled snapshot-generation provenance is retained across delta,
  omission, and generation-only keyframes. A fixed 1,024-entry cgame evidence
  table is derived from the complete 798-entry in-flight composition. Shared-
  constant assertions, both 798-entry arrival orders, more-than-two-table
  rolling streams, six focused Meson rows, and the production cgame build pass.
  The corrected command oracle excludes process teardown from its evidence
  clock while preserving the live-grace deadline. Final post-stage parent
  `.tmp/networking/fr10_t04_bounded_lifecycle_full_retry.json` passes 11/11
  focused plus all five live masks. Its combined child records 15/15 damage,
  30/30 receipt matches, zero reconciliation fault, and preflight plus post-fire
  proof; semantic digest is
  `f4a161bf0876e05db21499ab5c21399cc0ef87ae4881ab78ae764bc67b6789ad`.
  The six production targets and refreshed 16-root-file/one-dependency/601-
  asset stage pass. The parent remains deliberately `task_complete=false`.
  Evidence:
  `docs-dev/fr-10-t04-live-event-0x73-status-evidence-gate-2026-07-20.md` and
  `docs-dev/fr-10-t04-t08-private-receipt-bounded-lifecycle-2026-07-20.md`.
  Other remaining event producers,
  default-on/release promotion,
  sustained combined load, and release matrices are not implemented. The negotiated monotonic barrier now
  proves finite ACK-credit
  exhaustion and two rotations end in counted disposition with no stale cgame
  mutation, retired bank, or silent overwrite.
- Progress: a pointer-free capability core, userinfo offer, server-owned session
  epoch, adjacent confirmation tuple, packet-boundary validation,
  downgrade/failure handling, reconnect lifecycle, and exact default-off
  public bundles are live. Disabled endpoints advertise only legacy `0x03`;
  armed endpoints freeze one exact `0x53`, `0x73`, `0x57`, or `0x77` offer for
  the connection. Cancellation bit `0x40` is mandatory in every exact native
  readiness/public mode; the event bit participates in `0x73`, canonical
  snapshot participates in `0x57`, and both participate in exact `0x77`.
  An allocation-free
  transport-only V1 core now frames opaque canonical
  command/snapshot/event references, enforces a 1,200-byte datagram ceiling,
  fragments and reassembles up to 131,072 bytes/128 fragments with datagram and
  message CRCs, accepts reordered delivery and identical duplicates
  idempotently, rejects conflicting duplicates and malformed input, and
  schedules bounded caller-owned handles with deterministic priority aging.
  The pointer-free session core binds that envelope to a negotiated epoch plus
  a non-reusable process-local connection-incarnation owner, retains commands/
  events until an exact receipt, supersedes only snapshots,
  freezes each transport sequence's fragmentation plan, reproduces ACKs after
  loss, and fail-closes replay or canonical-identity conflicts. Its 64-sequence
  receipt bound, 16-entry snapshot retry identity cache, timeout/discard/
  checksum retry paths, sequence exhaustion, and cache-churn survival are
  covered in C and C++ without advertising the capability. A `WNC1` common
  header and fieldwise little-endian codecs transactionally encode/decode
  command V1, all fourteen event payloads in event V1, and snapshot V2 (up to 512
  entity records, 1,024 area bytes, and 512 event refs). The record count is
  separate from the exclusive entity-index identity domain: production
  adapters accept 1,024 legacy or 8,192 Rerelease identities. The 65,509-byte
  no-event maximum-512-record snapshot, the formerly rejected 65,539-byte
  one-event case, and the full legal 80,869-byte 512-event projection now fit
  below the 131,072-byte ceiling. Hostile lengths/counts, aliasing, truncation,
  hashes,
  signed zero, C/C++ ABI, sanitizers, static analysis, and i686 layouts pass
  without advertising the capability. The exact-receipt wire
  bridge now has a bounded WTC1 core: up to eight atomic WNE1 DATA or inclusive
  exact-range ACK entries follow an unchanged legacy prefix, and a strict
  32-byte terminal footer binds version, sizes, confirmed epoch, count, and
  carrier-only CRC within the complete 1,200-byte packet budget. Missing magic
  is legacy; matching malformed candidates fail closed. The pointer-free API is
  transactional, validates every nested WNE1 datagram and epoch, supports
  overlapping byte inputs, and remains unadvertised. The isolated session-to-
  carrier bridge now prepares scheduler selections without mutating retained
  state, gates one whole-message dispatch per connection, advances fragments
  only after exact transport confirmation, records a send attempt only with
  the final confirmed fragment, and rejects copied cursors, changed payloads,
  superseded snapshots, stale tickets, and unsent receipts transactionally.
  A separate 80-identity ledger imports ACK authority only through retained RX
  commit or a combined admitted `ALREADY_COMMITTED` path. One owner-bound active
  token serializes receipt mutation with prepare/bind/transport/terminal
  accounting; full packet length/CRC and ordered ACK identity are confirmed
  before retry credits change. It coalesces adjacent due identities without
  bridging gaps, emits up to eight ACK-only ranges, and uses bounded proactive
  handoffs rearmed by committed-DATA retry. A cross-component property test now
  exhausts every proactive copy, proves the sender stalls at the 64-sequence
  receipt window while RX authority survives, then resumes after repeat rearm
  and reverse-path recovery. `NetchanNew_Transmit` now exposes a
  transactional NEW-channel hook after complete reliable/unreliable assembly
  and before physical packet duplication. It excludes the sequence/ack/qport
  header and legacy-fragment paths, stages bounded replacement bytes, preserves
  byte-identical fallback, completes every token-bearing prepare exactly once,
  and reports every copy's synchronous acceptance plus the exact final
  application slice. `Netchan_ProcessEx` now provides the symmetric NEW-channel
  RX boundary only after sequence/ACK admission, reliable assembly, final
  fragment reassembly, liveness, and accounting. It exposes the exact unread
  application slice, restores byte-identical legacy behavior on bypass, and
  makes malformed native-candidate rejection a terminal call-site decision.
  Production client and dedicated call sites handle that result. A
  pointer-free role-specific readiness state machine now binds the negotiated
  capability mask, globally non-reused transport epoch,
  caller-supplied monotonic nonce, deadlines, and exact
  `CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE` echoes. Its RX/TX gates perform
  sticky live clock/deadline checks. A packet-scoped 15-pair signed-setting
  carrier serializes those records identically in both legacy directions with
  record CRC32 plus ordered `WRS1` CRC16 commit validation. The added pair
  words bind an independent canonical snapshot epoch; records occupy 75 bytes
  client-to-server and 135 bytes server-to-client. The default-off
  production pilot gives eligible NEW client and server connections process-
  stable readiness owners and snapshots `cl_worr_native_shadow=0` /
  `sv_worr_native_shadow=0` at connection start. The public capability offer
  and confirmation remain exactly `0x03`; the private readiness exchange binds
  command `0x53`, event `0x73`, or snapshot `0x57`. After the packet-scoped
  `CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE` proof, the client command shadow
  observes every finalized command and, while no native command is retained,
  selects the newest command in the exact successfully encoded legacy
  `MOVE`/`BATCH_MOVE` range. It retains one immutable 110-byte `WNC1` command;
  the post-assembly hook appends one 206-byte
  `WTC1(DATA(WNE1(WNC1)))` transaction and stop-and-wait selection resumes only
  after an exact ACK releases it. The server stamps the admitted packet clock
  before `Netchan_ProcessEx`, validates one complete command fragment,
  retains/commits it, joins either arrival order against the authoritative
  legacy stream, and exposes only the unchanged legacy prefix. Exact matches
  retire their bounded join slots, clear the pending identity, and advance a
  per-peer canonical command high-water. Exact transport repeats refresh the
  committed receipt before fresh admission; a fresh message identity replaying
  the same or an older command drains without committing staged RX state.
  Structurally valid DATA at or below the negotiated cancellation floor exposes
  only its legacy prefix before the active-epoch high-water check, so an old map
  epoch cannot poison the current stream. Command/sample mismatch still
  drains the pilot and never changes simulation authority. The reverse path
  emits exact-range ACK-only WTC1 records with 100 ms retry and three proactive
  handoffs; an exact committed duplicate rearms delivery. The client now
  accepts multiple ACK entries only when all are ACK ranges and applies the
  complete set transactionally. ACK due wakes the async server send only after
  rate and fragment gates. Map-quiesced ACK service can drain exact receipts
  opportunistically until a fresh challenge gives every lower epoch its
  terminal cancellation disposition.
  Reliable-queue point-of-no-return latches retain the hooks once either peer
  may legitimately send WTC1; an exact server committed-epoch fallback strips
  the trailer to legacy even when later local initialization fails. An invalid
  post-`CLIENT_READY` same-epoch ACTIVE stays in DRAIN and a later valid ACTIVE
  cannot resurrect DATA; only a validated fresh map challenge after quiesce may
  rearm it. Malformed, wrong-direction, unknown/ambiguous epoch, mismatched-
  reference, and unsafe fresh-identity replay traffic rejects or drains under
  the declared fail-closed policy. The client uses a shared alias-safe usercmd
  conversion, and each bank stores its official command epoch separately. At
  the production 1,024-byte ceiling, client legacy prefixes of 818 bytes fit
  and 819 bypass; one
  server ACK fits after 976 bytes and 977 bypasses. A pointer-free 168-byte
  mixed transaction token now coordinates one DATA fragment with zero to seven
  exact ACK ranges, reserves the full mixed budget before dispatch, permits a
  DATA-only fallback when no ACK is due, and binds confirm/reject/abort to the
  exact packet, owner, epoch, dispatch, fragment, and ordered ACK ranges. The
  existing strict one-DATA APIs are unchanged. The mixed core links into both
  production engines and is called only by the separately opted-in private
  event-shadow hooks. A new
  transport-neutral 24-byte canonical event-stream descriptor separates
  semantic event epoch/first-sequence identity from transport and snapshot
  lifetimes. Native record class 4 carries its exact 56-byte WNC1 image. A
  56-byte process-local connection owner enforces exact duplicates, conflicts,
  stale epochs, resync high-water, generation exhaustion, and reconnect-only
  epoch reuse. The production-linked completed-message admission core takes the
  exact immutable session binding, requires reserved capability bit 5, stages
  RX commit and ACK-ledger mutation on private copies, then invokes only the
  engine-owned cgame endpoint. Descriptor/event transport state is published
  only after a fresh exact cgame status/receipt proof. Generic retained commit
  and repeat bridges now reject semantic classes; the event admission path
  nonmutatingly binds a newly observed WTC1/WNE1 fragment to exact committed
  whole-message history, requires `ALREADY_COMMITTED`, refreshes only that
  identity, and supports any fragment of a committed multi-fragment message.
  Exact-only ledger capture cannot import an unrelated semantic receipt.
  Post-callback or repeat-status uncertainty burns the attempted epoch, leaves
  staged RX state uncommitted, and retires every semantic snapshot/event/
  descriptor ACK while preserving command receipts. Map/serverdata quiesce
  retains engine
  high-water while full disconnect alone clears it. A separate pointer-free
  snapshot admission owner now requires the private snapshot binding, decodes
  one complete reconstructed WNC1 V2 projection into caller-owned scratch,
  verifies its exact ID and five cross-endpoint parity/semantic hashes against
  the independently built legacy/server-shadow expectation, and stages
  session, slot, and receipt mutation on private copies. The endpoint hash is
  endpoint-local chronology/provenance and is checked against the fresh native
  cgame receipt after consumption. Generic retained commit and repeat paths
  refuse
  snapshot DATA. The cgame timeline export is now V2 and reports an exact
  admission generation, snapshot ID/hash, receive time, endpoint/parity hashes,
  and timeline/event-fence flags. Transport state and ACK authority publish
  only after both immutable timeline copy and a healthy event-runtime fence
  prove that exact snapshot. Legacy-inferred event references are always
  structure/order/hash validated for this fence with
  `cg_event_runtime_audit=0`; audit-off operation retains no diagnostic join
  entries, so legal 512-reference snapshots cannot exhaust the 2,048-entry
  table. Consumer uncertainty hard-resets cgame, retires
  all semantic receipts, quarantines the epoch, and arms keyframe recovery.
  Exact committed packet repeats do not consume twice; they require a fresh
  exact V2 receipt before rearming only that ACK. WNC1 still serializes a
  complete reconstructed view even when `base_id` is nonzero, so `base_id`
  remains lineage metadata rather than a decode dependency. An actual
  `SV_SnapshotShadowViewV1` keyframe and base-referenced update pass reversed
  multi-fragment delivery with exact five-proof cross-endpoint parity and an
  exact local native endpoint receipt. Endpoint-only expectation divergence is
  legal; an identity, legacy-parity, or semantic mismatch leaves session,
  slots, ledger, and cgame byte-for-byte uncommitted.
  The command-only readiness path now binds exactly `0x53`. When
  both event controls and both base-shadow controls are enabled, a separate
  private `0x73` path adds `CLIENT_ACTIVE_CONFIRM`; a receive-only binding lets
  the server admit client DATA while waiting for that record but cannot grant
  transmit authority early. Separate snapshot controls select private `0x57`,
  carry an independent nonzero snapshot epoch in the 15-pair readiness record,
  and require the same fourth confirmation. Enabling both controls activates
  exact private `0x77`; a fair scheduler services both DATA lanes and the
  disjoint message-sequence ranges preserve semantic ACK class identity.
  After confirmation, the server copies each exact final projection into a
  supersedable two-bank sender and the client accepts it through a bounded
  receiver. Native completion may wait for the independently reconstructed
  legacy expectation; only semantic cgame admission publishes the ACK. The
  rolling 16-entry expectation cache preserves active-RX references, evicts
  only safe older observations, and returns retry-later instead of draining
  when no safe victim exists. Complete core-RX expiry is reconciled with the
  wrapper's pending descriptors before later DATA is interpreted. During that
  private epoch the native receiver is the sole canonical timeline publisher,
  including through diagnostic DRAIN and transport-hook corruption/loss.
  Default modes retain legacy rendering authority; source-gated
  `cg_snapshot_timeline_render=3` can instead promote safe remote transforms
  from the engine-owned native timeline. Only map/serverdata or matching
  connection boundaries release ownership, and failure handling never
  overwrites a foreign replacement hook. The registered production-hook link
  proves fragmented loss, reorder, duplicate, deferred expectation, 24
  expectations before DATA, complete-timeout slot reuse, lost ACK/repeat,
  exactly-once cgame consumption, exact server release, five-proof hash
  quarantine, wrong epoch, and real 1,024/8,192 identity domains.
  After each successful final per-peer legacy
  snapshot projection, compact candidate metadata reconstructs only the
  legacy entity events that survived that peer's visibility and emission
  rules. The sender queues its descriptor first, buffers ID-less candidates,
  and allocates per-peer semantic IDs only after exact descriptor ACK. Live
  server packets may mix current descriptor/event DATA with command ACKs, and
  live client packets may mix command DATA with current event ACKs.
  Descriptor/event ACK authority is published only after a fresh
  cgame status/receipt proof, and native-output due queries wake client
  keepalives plus server asynchronous sends for DATA, retries, fragments, and
   current receipts. Legacy commands, snapshots, demos, and presentation
   remain authoritative. The staged writer now additionally observes only a
   bounded ordered sequence of up to 16 supported temporary-entity and
   player/monster/rerelease-muzzle carriers. Its shared raw decoder accepts
   each legacy shape exactly and preserves cross-family order. Unreliable
   batches bind only after fitting the post-snapshot datagram; reliable batches
   are captured after the legacy append, held in a fixed native-only FIFO, and
   rebound once to the next exact committed snapshot as non-expiring reliable-
   ordered records. The final-emission adapter binds an entityless effect to
   world or raw source/subject indices to generations from that same exact sent
   view. Malformed, unsupported-service, over-capacity, old-netchan, map-stale,
   and non-visible paths remain legacy-only. A dedicated exact `svc_damage`
   adapter additionally decodes at most four ordered indicators only after the
   complete legacy message fits, preserves compressed magnitude/direction and
   health/armor/shield flags, and binds the exact final snapshot controlled
   entity as subject without inventing an attacker identity. A deterministic in-process production-wrapper virtual
  link now drives the real client/server pilot hooks through scheduled S2C
  accepted-but-lost descriptor DATA, C2S mixed command-DATA loss, delayed/
  reordered descriptor retry, duplicate event retry, one-way event-ACK loss,
  bidirectional mixed DATA/ACK, corruption in each direction, and consecutive
  cancellation barriers. The run converges with `s2c_loss=1`, `c2s_loss=2`,
  `ack_loss=1`, `reordered=2`, `duplicates=3`, `corrupt_s2c=1`,
  `corrupt_c2s=1`, `cancelled_ack_strip=1`, `cancelled_data_strip=1`,
  `cancelled_corrupt_reject=2`, `presented=1`, and digest
  `be9724b38fb5f682`; valid canceled carriers expose legacy only, corrupt old
  carriers reject, and no delayed DATA reaches cgame. The same explicit
  in-process wrapper gate now also has a three-event assertion-only schedule:
  lost event 2 remains retained while delivered event 3 is selectively
  receipted and retired. It is not yet the production `NetImpair` model/golden
  matrix, live two-process impairment, load/soak, or cross-platform evidence.
  The negotiated
  bit-6 barrier now transactionally cancels command TX, event sender/RX, and
  exact receipts; its monotonic floor covers activated banks and advertised-
  but-unactivated readiness epochs. The client publishes its staged disposition
  only after `CLIENT_READY` is durably appended. The server preflights all
  terminal calls before issuing the next challenge. Fully validated carriers at
  or below the floor expose only their byte-identical legacy prefix, while
  corrupt old carriers still reject. Valid old readiness controls are consumed
  without state mutation and counted separately after full direction/capability
  validation; malformed, wrong-direction, or downgraded controls still reject.
  The server performs that classification before reserving `SERVER_ACTIVE`, so
  stale control stays response-free even with a full reliable queue, while a
  current response that cannot be appended fails closed. Server status reports
  `wire_committed_transport_epoch` separately from the current
  `transport_epoch`, preserving an unambiguous prior wire commitment across a
  replacement challenge.
  No retired bank or epoch ring remains. The
  lifecycle diagnostic records `ack_handoffs=3`, no fourth due ACK, one canceled
  client receipt, two canceled server event records, zero retired receipt/
  retention, a second rotation without a retired bank, both delayed old
  directions stripped, and two corrupt-old rejections. Three
  consecutive repeated two-process runs under 25 ms latency retain, match,
  acknowledge, and release 152, 150, and 151 observational commands (453 total)
  with exact client/server counter equality and zero final retention, mismatch,
  rejection, drain, or failure. The original fragment-pressure/async gate also
  passes post-change. Broader event-family production, production/live native
  snapshot authority, full production impairment-matrix breadth,
  broader
  ACK-exhaustion/map-rotation
  evidence, release-scale dual-adapter parity, load/soak evidence, and
  advertisement
  remain open.
- Evidence: `docs-dev/fr-10-t04-native-envelope-foundation-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-transport-session-retention-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-canonical-byte-codecs-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-packet-carrier-trailer-core-2026-07-13.md`,
  `docs-dev/fr-10-t04-transport-confirmed-session-carrier-and-receipts-2026-07-13.md`,
  `docs-dev/fr-10-t04-post-assembly-netchan-hook-2026-07-13.md`,
  `docs-dev/fr-10-t04-post-admission-netchan-rx-seam-2026-07-14.md`,
  `docs-dev/fr-10-t04-native-endpoint-readiness-core-2026-07-14.md`,
  `docs-dev/fr-10-t04-native-readiness-setting-sideband-2026-07-14.md`,
  `docs-dev/fr-10-t04-native-command-shadow-core-2026-07-14.md`, with current
  integration validation in
  `docs-dev/fr-10-t04-rx-readiness-command-shadow-integration-validation-2026-07-14.md`,
  readiness-only pilot evidence in
  `docs-dev/fr-10-t04-default-off-native-readiness-production-pilot-2026-07-14.md`,
  historical initial production DATA/ACK evidence in
  `docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`,
  and the stable two-process gate in
  `docs-dev/fr-10-t04-native-two-process-async-ack-impairment-gate-2026-07-14.md`,
  with repeated-stream and mixed-core evidence in
  `docs-dev/fr-10-t04-repeated-command-shadow-and-mixed-carrier-core-2026-07-14.md`,
  and canonical descriptor/admission evidence in
  `docs-dev/fr-10-t04-t05-canonical-event-stream-admission-core-2026-07-14.md`,
  with semantic repeat/ACK fencing in
  `docs-dev/fr-10-t04-t05-semantic-repeat-ack-fence-2026-07-14.md`, and the
  live full-duplex per-peer milestone in
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  with deterministic production-wrapper fault evidence in
  `docs-dev/fr-10-t04-t05-directional-event-shadow-virtual-link-2026-07-14.md`,
  the quiesce fix and cancellation decision in
  `docs-dev/fr-10-t04-t05-map-quiesce-ack-service-and-epoch-cancel-decision-2026-07-14.md`,
  and the implemented negotiated barrier in
  `docs-dev/fr-10-t04-t05-negotiated-epoch-cancellation-barrier-2026-07-14.md`,
  plus multi-event selective-receipt evidence in
  `docs-dev/fr-10-t04-t05-multi-event-selective-receipt-virtual-link-2026-07-15.md`,
   visible-muzzle native-shadow evidence in
   `docs-dev/fr-10-t04-t05-visible-muzzle-native-shadow-2026-07-15.md`, and
   visible-temporary-entity native-shadow evidence in
   `docs-dev/fr-10-t04-t05-visible-temp-native-shadow-2026-07-15.md`, with
   bounded direct-sgame sequence evidence in
   `docs-dev/fr-10-t04-t05-bounded-temp-sequence-native-shadow-2026-07-15.md`,
   and bounded muzzle-sequence evidence in
   `docs-dev/fr-10-t04-t05-bounded-muzzle-sequence-native-shadow-2026-07-15.md`,
   extended by ordered mixed-game-event evidence in
   `docs-dev/fr-10-t04-t05-mixed-game-event-native-shadow-2026-07-15.md`, and
   explicit-positional off-frame audio evidence in
   `docs-dev/fr-10-t04-t05-positional-offframe-spatial-audio-native-shadow-2026-07-15.md`,
   followed by reliable and client-local positionless audio evidence in
   `docs-dev/fr-10-t04-t05-reliable-local-spatial-audio-native-shadow-2026-07-18.md`,
   and reliable mixed/multi-muzzle snapshot-fence evidence in
   `docs-dev/fr-10-t04-t05-reliable-mixed-game-event-native-shadow-2026-07-18.md`,
   plus per-client damage-indicator native-shadow evidence in
   `docs-dev/fr-10-t04-t05-damage-indicator-native-shadow-2026-07-18.md`,
   followed by exact per-client help-path marker evidence in
   `docs-dev/fr-10-t04-t05-help-path-native-shadow-2026-07-19.md`,
   and keyed-POI adapter, FIFO, fence-parity, and presenter evidence in
   `docs-dev/fr-10-t04-t05-t07-keyed-poi-native-shadow-2026-07-20.md`,
   with the raw direct-sound boundary recorded in
   `docs-dev/fr-10-t04-t05-raw-direct-sound-adapter-decision-2026-07-15.md`,
   and the Stage D snapshot semantic-admission foundation in
   `docs-dev/fr-10-t04-t06-native-snapshot-semantic-admission-2026-07-16.md`,
   with the production adapter continuation in
   `docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`,
   and the accepted serialized production corpus in
   `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`,
   plus the production replay boundary in
   `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-production-v2-pmove-replay-boundary-2026-07-17.md`,
   and combined-lane evidence in
   `docs-dev/fr-10-t04-t06-t14-combined-native-event-snapshot-shadow-2026-07-18.md`,
   followed by the exact public-bundle acceptance parent and explicit open-row
   audit in
   `docs-dev/fr-10-t04-exact-bundle-acceptance-parent-2026-07-20.md`.
- Definition of Done:
  - Capability negotiation cannot reinterpret a legacy stream as WORR traffic.
  - The envelope serializes, fragments, and prioritizes the canonical command,
    snapshot, and event records already established by `FR-10-T05`,
    `FR-10-T06`, and `FR-10-T09`; it does not define parallel domain schemas.
  - Legacy and WORR adapters feed the same validators, canonical storage, and
    cgame/sgame consumers.
  - Negotiation downgrade, unknown versions, malformed messages, reconnect, and
    MTU-boundary behavior pass the deterministic fault matrix.
  - `q2proto/` is unchanged and legacy server/demo paths remain operational.

### `FR-10-T05` Typed sequenced event journal

- Area: shared event schema, engine canonicalization, `bgame`, cgame and sgame.
- Priority: P0.
- Dependencies: `FR-10-T02`, `FR-10-T03`.
- Current state: Done on 2026-07-19.
  cgame authority/prediction runtime, validated engine-export scope, explicit
  event-stream descriptor, transactional native admission, and semantic
  repeat/ACK-authority fencing, plus a default-off live per-peer stream with
  descriptor-first reliable retention for legacy-entity events, visible,
  explicitly positioned, reliable, or client-local positionless spatial
  audio, bounded ordered mixed temporary-entity/player-muzzle/monster-muzzle/
  Rerelease-muzzle sequences, bounded per-client damage indicators, and exact
  per-client help-path markers.
- Progress: the pointer-free event ABI and caller-owned bounded journal now
  validate typed payloads, authoritative and prediction identities, delivery
  classes, receipt windows, sequence wrap, matching, coalescing, expiry, and
  at-most-once presentation state. Stable payload records cover legacy entity
  events, all supported temporary-entity shapes, player/monster muzzle flashes,
  spatial audio, per-client damage indicators, and help-path marker effects;
  fieldwise `ESM1` semantic comparison is independent of authority allocation
  and object padding. A
  named optional extension shadows
  final authoritative legacy entity events from sgame into an engine-owned
  4096-record journal. A separate engine-to-cgame extension delivers
  callback-scoped immutable audit ranges. V2 now captures typed temporary
  entities, player/monster muzzle flashes, normalized spatial sounds, and
  accepted-frame entity events in legacy decode order, with explicit carrier
  phase/status, inferred provenance, chunk identity, strict bounds, and
  failure atomicity. The decoded temporary-entity mapper is now a shared C
  constructor used by cgame: it creates a fully validated payload template and
  returns raw source/subject indices, while cgame continues to bind those
  indices to its observed entity generations at range delivery. That removes a
  second type-specific mapping without treating raw indices as authoritative
   lineage. The same common-template boundary now covers spatial audio and
   player/monster muzzle flashes. Their shared mappers now feed one bounded,
   ordered mixed raw-carrier sequence decoder for final server emission: it
   accepts up to 16 complete supported legacy temporary-entity and
   player/monster/rerelease-muzzle shapes, preserves their source/subject
   indices, family, silence state, and wire order, then stages the full output
   until it can bind world or exact visible snapshot generations atomically. The spatial
  server-side final-emission adapter runs only after a structured per-client
  sound write succeeds. It covers final unreliable, reliable engine, and
  transient/reliable client-local paths: a visible source binds to its exact
  snapshot generation, an explicit off-frame position binds to world with
  `POSITION_FORCED`, and a local cue binds to world with `LOCAL_ONLY`. The
  direct-game adapter accepts an exact supported sequence after it either fits
  the unreliable post-snapshot datagram or is appended to the reliable
  netchan. Reliable decoded batches wait in a fixed native-only FIFO and bind
  once to the next exact snapshot before frame-derived and unreliable events;
  their members become non-expiring reliable-ordered records. It rejects
  malformed, unsupported-service, over-capacity, old-netchan, map-stale, and
  non-visible source/subject cases atomically. Arbitrary non-local positionless
  sounds and other excluded paths deliberately remain legacy-only. The
  per-client `svc_damage` adapter accepts exactly one bounded carrier only
  after its complete unreliable message fits and is appended after the exact
  final snapshot. It decodes at most four indicators, preserves their wire
  order and compressed magnitude/direction/type bits, uses world as the honest
  source because no attacker identity is carried, and binds the exact snapshot
  controlled-entity generation as subject before atomic native queueing. The
  per-client `svc_help_path` adapter likewise runs only after a complete
  unreliable legacy append. It exact-decodes a canonical start flag, three
  finite float coordinates, and one shared packed normal into a stable help-
  marker effect, retains world source, and binds the same exact controlled-
  entity generation as subject. Cgame
  now validates each V2 range before copying it into a
  2,048-record value-only journal with reset/overwrite-safe cursors, semantic
  hashes, ordered future blocking, at-most-once audit advancement, and explicit
  overrun recovery. A new allocation-free cgame runtime now owns separate
  authority, prediction-tombstone, snapshot-reference, and legacy-audit
  lifetimes. It transactionally admits authoritative and predicted batches,
  maintains selective receipt state, joins authority records to immutable
  snapshot fences in either arrival order, reconciles prediction keys, retains
  presented/cancelled evidence, retires safe tombstones from consumed-command
  watermarks, and advances ordered terminal records without permanent
  head-of-line stalls. The `cg_event_runtime_audit` cvar controls legacy
  body comparison only. Snapshot-reference correctness ingestion continues
  for both authoritative and legacy-inferred provenance when it is off;
  audit-off legacy ranges validate and fence without retaining diagnostic join
  entries. Authority expiry and ordering likewise continue. A compact
  C-compatible cgame export exposes reset,
  authoritative submit, and a 48-byte receipt/health summary. Its engine owner
  validates every reset/status handshake, quarantines incoherent callbacks,
  clears pointers before DLL unload, enforces strictly increasing
  connection-lifetime event epochs, and requires a fresh epoch after active
  DLL replacement or unresolved snapshot-fence loss without returning a stale
  receipt. The shared local-action v2 core also derives stable gameplay/audio/
  effect prediction keys directly from canonical command IDs and adapts them
  into the same event ABI. Legacy presenters remain authoritative and neither
  path changes packets, demos, or rendered behavior. The canonical 24-byte
  event-stream descriptor now establishes an independent semantic epoch and
  first sequence. Its connection owner retains high-water across map quiesce,
  and a binding/capability-gated completed-message adapter transactionally
  precommits RX/ACK state, submits through the engine-owned export, and requires
  fresh exact status/receipt proof before publication. Repeats now require a
  newly observed exact committed packet plus fresh descriptor/event receipt;
  semantic uncertainty retires all pending snapshot/event/descriptor ACK
  authority while preserving command receipts.
  The server now extracts typed ID-less legacy-entity candidates from each
  peer's exact final snapshot emission, queues them behind a per-peer stream
  descriptor, and assigns semantic IDs only after that descriptor is exactly
  acknowledged. Server-originated descriptor/event DATA is retained reliably
  and reaches the engine-owned cgame authority runtime through the fresh-
  status semantic ACK fence. A negotiated fresh challenge now terminally
  counts and removes lower-epoch sender, RX, and receipt state without allowing
  canceled DATA to reactivate authority. The production-wrapper virtual-link
  gate proves that one
  admitted canonical event is presented exactly once across DATA loss, retry,
  reordering, duplication, one-way ACK loss, corruption, and two cancellation
  barriers. It now additionally proves three distinct event IDs across a real
  semantic gap: event 3 is selectively receipted and retired while lost event 2
  stays retained until retry restores contiguous receipt state. Its deterministic
  digest is `be9724b38fb5f682`. Map-quiesced `DRAIN`
  now permits only already-authorized event ACKs before the barrier, preserving
  the cgame fence while command DATA remains frozen. An exact follow-up proves
  that three exhausted ACK handoffs plus two rotations terminate as counted
  cancellation with zero retired state, no stale cgame mutation, and no bank
  overwrite. Production support now includes narrow visible-source,
   explicit-positional off-frame, reliable, and client-local positionless
   spatial-audio,
   bounded ordered mixed temporary-entity and player/monster/rerelease-muzzle
   sequence family, per-client damage indicators, and exact help-path markers
   alongside legacy entity events.
   Arbitrary non-local positionless audio, raw direct sound, and other direct
  sgame service families,
  live cgame/
  sgame local-action submission, prediction cutover, and effect/audio
  presenter cutover remain. Stage D snapshot admission now refuses snapshot
  ACK authority unless the cgame event runtime reports a healthy exact
  snapshot fence. The real-cgame virtual link proves event-carrying
  legacy-inferred snapshots pass that fence with
  `cg_event_runtime_audit=0`; this is a cross-stream safety gate, not event
  presenter cutover. The final task-level acceptance gate executes 18 compiled
  journal, stream, cgame-runtime, native-link, prediction-correlation, and
  legacy-family adapter gates twice with identical output. It records digest
  `5f300c5f7f925cf5105812cf49ce108305c357790989978738e34e88058e347c`
  and directly covers all four published Definition-of-Done criteria. The
  additional event-family, presenter-cutover, public-authority, and load work
  remains with its owning tasks and does not expand T05's accepted contract.
- Evidence:
  `docs-dev/networking-canonical-event-journal-core-2026-07-12.md`,
  `docs-dev/networking-legacy-entity-event-shadow-2026-07-12.md`,
  `docs-dev/networking-client-cgame-legacy-event-shadow-2026-07-12.md`,
  `docs-dev/networking-event-payload-catalog-2026-07-12.md`,
  `docs-dev/networking-client-cgame-typed-event-range-v2-2026-07-12.md`,
  `docs-dev/networking-cgame-canonical-event-presentation-journal-2026-07-13.md`,
  `docs-dev/networking-canonical-local-action-transaction-v2-2026-07-13.md`,
  `docs-dev/fr-10-t05-cgame-event-runtime-and-direct-authority-export-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-t05-canonical-event-stream-admission-core-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-semantic-repeat-ack-fence-2026-07-14.md` and
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-directional-event-shadow-virtual-link-2026-07-14.md`
  and
  `docs-dev/fr-10-t04-t05-map-quiesce-ack-service-and-epoch-cancel-decision-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-negotiated-epoch-cancellation-barrier-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-t05-multi-event-selective-receipt-virtual-link-2026-07-15.md`,
  plus
  `docs-dev/fr-10-t05-shared-legacy-temp-event-candidate-2026-07-15.md`,
  and
   `docs-dev/fr-10-t04-t05-visible-spatial-audio-native-shadow-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-positional-offframe-spatial-audio-native-shadow-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-reliable-local-spatial-audio-native-shadow-2026-07-18.md`,
   `docs-dev/fr-10-t04-t05-raw-direct-sound-adapter-decision-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-visible-muzzle-native-shadow-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-visible-temp-native-shadow-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-bounded-temp-sequence-native-shadow-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-bounded-muzzle-sequence-native-shadow-2026-07-15.md`,
  `docs-dev/fr-10-t04-t05-mixed-game-event-native-shadow-2026-07-15.md`,
  `docs-dev/fr-10-t04-t05-reliable-mixed-game-event-native-shadow-2026-07-18.md`,
  `docs-dev/fr-10-t04-t05-damage-indicator-native-shadow-2026-07-18.md`,
  `docs-dev/fr-10-t04-t05-help-path-native-shadow-2026-07-19.md`,
  `docs-dev/fr-10-t04-t05-t07-keyed-poi-native-shadow-2026-07-20.md`,
  `docs-dev/fr-10-t04-t06-native-snapshot-semantic-admission-2026-07-16.md`,
   and
   `docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`,
   with final closure evidence in
   `docs-dev/fr-10-t05-typed-event-journal-acceptance-closure-2026-07-19.md`.
- Definition of Done:
  - Event types, payloads, source time, identity, ordering, delivery class and
    prediction class are explicit and bounds-checked.
  - Multiple events per source/tick are retained within declared capacity.
  - Loss, replay, duplicate, late arrival, predicted matching and sequence-wrap
    tests prove at-most-once presentation where required.
  - Legacy temp/entity events map into the canonical journal without breaking
    legacy traffic or demos.

### `FR-10-T06` Acknowledged-baseline canonical snapshot system and legacy shadow

- Area: server snapshot construction, client reconstruction, canonical storage.
- Priority: P0.
- Dependencies: `FR-10-T02`, `FR-10-T03`, `FR-10-T05`.
- Current state: Done on 2026-07-19.
- Progress: Stage A defines a pointer-free, component-aware canonical snapshot
  ABI with explicit snapshot/base/previous identities, discontinuities,
  authoritative or legacy-inferred entity generations, T02 player movement,
  and ordered T05 event references. A caller-owned fixed-capacity transactional
  store publishes immutable generation-safe snapshots, hashes semantic fields
  without C padding or store-local serials, and exposes validating copy-out
  APIs. Its deterministic 100,000-publication and hostile-failure suite is part
  of the Meson networking suite. Stage B now reconstructs complete snapshots
  from public q2proto frame/entity-delta records against the exact retained
  `deltaframe`, stores per-base inferred lifecycle lineage, validates
  baseline/add/remove/branch/full-frame behavior, and separates an exact
  endpoint hash from a narrow legacy-reconstructible parity hash. Public
  q2proto server-to-wire-to-client tests produce equal semantic parity for
  Vanilla, R1Q2, Q2PRO, and Q2REPRO, while focused faults cover base jumps,
  fragment stalls, controlled first-person omission, explicit truncation, and
  hostile alias rejection. The live client now captures baselines, frame
  headers and entity deltas, attaches the negotiated consumed-command cursor,
  resolves a stateful canonical server clock across FPS changes, retains
  lineage before precache and during demo seek, compares accepted legacy state
  independently, and delivers only parity-qualified promotion-eligible
  immutable views to the external cgame timeline. Client-generated stored seek
  snapshots additionally carry a checksum/commit-validated exact frame/time
  tuple for every backup frame, preserving accumulated clock time across rate
  changes instead of recomputing it from the latest FPS. Legacy
  parsing/rendering remains authoritative. Stage C now observes each peer's
  final accepted q2proto frame/entity services at the real packet-emission
  boundary, retains generation-safe refs to the exact sent base and endpoint,
  records explicit truncation, and commits only after the entity terminator.
  Its tick/time metadata comes from an exact map-scoped simulation clock that
  advances once per executed game frame and is independent of peer-local wire
  frame numbering. Projection failure cannot reject a valid legacy packet.
  In the normal server loop, that clock advances immediately after the game
  frame executes and before snapshot emission; `sv.framenum` advances only
  after emission, so a retained frame's time describes the authoritative state
  actually sent. A validated delta acknowledgement retains the acknowledged
  `client_frame_t.server_time_us`, and canonical command/rewind context uses
  that exact value instead of reconstructing time as `tick * interval`. A
  fixed-seed offline corpus now compares 100,000 exact server final-emission
  refs with a separately allocated receiver projection across acknowledged
  branches, keyframes, invalid-base recovery, entity lifecycle/visibility,
  truncation, fragment-stall/rate causes, chronology, authoritative tick wrap,
  and the signed wire-frame boundary. Two executions produced identical
  evidence and 100,000/100,000 endpoint, legacy, component, and chronology
  matches. The corpus also caught and permanently covers the legacy rule that
  advances `old_origin` for an unchanged non-beam entity; the projector applies
  that rule only to scratch state and preserves retained-base transactionality.
  The current deterministic corpus digest is `6451c75bdb523477`. A staged loopback gate
  now also proves that the final live client projection reaches the external
  cgame timeline: the latest staged schema-v3 evidence
  recorded 388/388 clean
  and 386/386 impaired attempts as projected, published, legacy-compared,
  promotion-
  eligible, and consumer-accepted with zero mismatch, rejection, capture
  failure, or recovery activity. That stricter gate exposed and now covers the
  first-keyframe controlled-entity epoch-reset provenance ordering defect. The
  offline 100,000-case corpus remains public-API evidence. A separate accepted
  corpus now supplies 100,000-frame serialized production-hook evidence without
  replacing the broader live/load gates. Stage D serializes a complete reconstructed
  WNC1 snapshot view—including valid nonzero-`base_id` lineage—through WNE1
  fragmentation, WTC1 DATA, retained RX, exact projection decode, and semantic
  admission. WNC1 does not encode a delta: `base_id` is metadata and missing
  base storage is not a decode dependency. The admission owner requires an
  exact independently observed server/legacy-shadow snapshot ID plus five
  cross-endpoint parity/semantic hashes, stages session/slot/receipt mutation
  on private copies, and submits to cgame only after parity succeeds. The
  decoded native endpoint hash is then matched locally against cgame's V2
  receipt. Cgame's V2 snapshot timeline export returns an exact admission
  generation, ID/hash/time, endpoint/parity
  hashes, and separate timeline/event-fence flags; the event runtime must also
  be healthy and not require resync. Legacy-inferred references are validated
  and receipted with the optional comparison audit disabled, without
  materializing audit-only join entries. Only that fresh exact receipt authorizes
  the snapshot ACK. Stale/wrong-epoch/parity failures cannot ACK, semantic
  uncertainty retires every semantic receipt and quarantines the epoch behind
  an explicit keyframe barrier, and a lost-ACK repeat revalidates the current
  receipt without consuming the snapshot twice. A real
  `SV_SnapshotShadowViewV1` event-carrying keyframe and base-referenced full
  update pass reversed multi-fragment delivery through the real canonical
  cgame timeline/event runtime with exact cross-endpoint parity and a locally
  exact decoded-native endpoint receipt. Endpoint-only expectation divergence
  remains admissible, while an identity/parity/semantic mismatch leaves
  transport and cgame uncommitted. The production continuation now binds the
  snapshot epoch
  independently in readiness, queues exact final-emission projections through
  a supersedable two-bank server sender, receives through the actual client
  hook, defers to the exact legacy expectation, publishes only through the
  native cgame consumer, and returns the semantic ACK through the reverse
  carrier. Retention now keeps an attempted active frame until semantic ACK,
  coalesces newer projections into a pending slot, promotes that pending frame
  after ACK, and uses a bounded 1,000-tick abandonment rule to recover when a
  receiver has expired incomplete work. Epoch rebinding preserves admitted
  snapshot history and q2proto bases, while server projection canonicalizes
  direction-specific q2proto unions before hashing/serialization. The
  deterministic real-hook gate covers loss, reorder, duplicate,
  lost ACK/repeat, exact release, five-proof hash quarantine, wrong epoch,
  1,024/8,192 identity domains, 24 expectations before DATA, and
  complete-timeout slot reuse. This is
  still not a public native authority cutover. The separate retained target-count run now
  records 115,914/115,914 production attempts, projections, publications,
  comparisons, promotion-eligible frames, and cgame consumer accepts, with
  zero mismatch, rejection, capture overflow, queue overflow, or throttle
  under deterministic loss/jitter/reorder/duplicate/upstream-stall conditions.
  It closes the single-client live-count item, not native serialization,
  bandwidth, load, soak, or platform acceptance. A pointer-free
  recovery policy observes legacy reconstruction and canonical
  projection/parity failures. The serialized corpus records 100,000 requested,
  accepted, acknowledged, released, and prediction-selected positive frames,
  250,500 resolved commands, pending/non-pending ranges through the 127-command
  limit, deterministic loss/burst/reorder/duplicate/stall decisions, and three
  rejected corruption probes across four epochs. It is an in-process,
  single-profile, fixed eight-entity/four-area-byte resolver/selector fixture;
  it neither executes `cg_predict`/PMove nor constructs the engine V2 request.
  The live legacy adapter can request bounded
  three-opportunity full-snapshot bursts separated by a two-opportunity
  cooldown through the existing `lastframe = -1` field. This optional behavior
  defaults off through `cl_snapshot_recovery 0`; the established invalid-frame
  recovery remains unconditional. Each successfully committed final-emission
  snapshot now also retains compact per-peer source metadata for its visible
  legacy entity events. Transactional copy-out rebuilds typed ID-less
  candidates and rechecks their semantic hashes before the opted-in event
  sender can queue them; failure affects only the native shadow after the
  legacy frame is committed. Other event families, broader snapshot semantic
  breadth, recovery impairment/promotion evidence,
  load/budget gates, and broad rendering cutover remain open. A separate
  three-repeat, two-process presentation gate proves target-only private `0x57`
  admission and actual mode-3 remote-transform authority: every run records
  positive snapshot queue/ACK/release, clock/pair/sample/native/promotion
  counters, exact `promoted == native`, and zero clock, pair, alignment, parity,
  event-fence, endpoint, or transform-error failures.
  The final parent-level gate runs 12 compiled snapshot gates twice, two
  identical 100,000-snapshot offline parity corpora, two identical 100,000-
  snapshot serialized production corpora, and a maximum-capacity budget probe.
  It verifies digests `6451c75bdb523477` and `ba519ae7bdd1db74`, zero
  unexplained mismatch/accepted abandonment, and exact ACK/release/authority
  counts. At 512 entities, 1,024 area bytes, and 512 event references, the
  80,869-byte WNC1 image remains under 131,072 bytes; the final aggregate
  build-plus-encode p95 is 486,900 ns and decode p95 is 214,700 ns against a
  1,666,600 ns limit.
  Maximum canonical history is 5,869,696 bytes under 16 MiB, and fixed native
  snapshot owners are 624,152 bytes under 2 MiB. Additional presenter,
  public-carrier, multi-client load/soak, platform, and release work remains
  with `FR-10-T04/T07/T14/T15` and does not expand T06's accepted contract.
- Evidence:
  `docs-dev/networking-canonical-snapshot-stage-a-2026-07-12.md` and
  `docs-dev/networking-canonical-snapshot-stage-b-q2proto-projection-2026-07-12.md`,
  the live client/demo integration record
  `docs-dev/networking-live-client-snapshot-prediction-and-demo-clock-2026-07-12.md`,
  `docs-dev/fr-10-t06-stage-c-server-final-emission-shadow-2026-07-13.md`,
  `docs-dev/fr-10-t06-final-emission-projector-parity-corpus-2026-07-13.md`,
  `docs-dev/networking-snapshot-keyframe-recovery-policy-2026-07-13.md`, and
  `docs-dev/networking-live-snapshot-parity-runtime-gate-2026-07-13.md`, and
  `docs-dev/fr-10-t06-live-100k-snapshot-acceptance-gate-2026-07-13.md`, plus
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-t06-native-snapshot-semantic-admission-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`,
  and
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-production-v2-pmove-replay-boundary-2026-07-17.md`,
  plus `docs-dev/fr-10-t04-t06-t14-combined-native-event-snapshot-shadow-2026-07-18.md`,
  `docs-dev/fr-10-t06-t07-t14-native-snapshot-presentation-authority-2026-07-18.md`,
  and final closure evidence in
  `docs-dev/fr-10-t06-canonical-snapshot-acceptance-closure-2026-07-19.md`.
- Definition of Done:
  - Existing acknowledged-base behavior is retained and tested rather than
    redundantly reimplemented.
  - R1 shadow mode constructs complete canonical snapshots beside the active
    legacy path and compares every legacy-representable player, entity, event,
    identity, and discontinuity field without granting shadow data authority.
  - Shadow promotion requires zero unexplained semantic mismatches across the
    required fault matrix and at least 100,000 accepted snapshots.
  - Reconstructed snapshot views are complete, immutable and generation-safe.
  - Invalid base, keyframe, entity add/remove/reuse, visibility, overflow,
    fragmentation and rate-budget cases pass deterministic tests.
  - The later WORR adapter in `FR-10-T04` uses the same canonical constructors
    and validation rules; it does not create a second snapshot model.
  - History memory and encode/decode work remain within the declared budgets.

### `FR-10-T07` Cgame snapshot timeline, interpolation, and event playback

- Area: cgame entity API, `cg_entities`, `cg_predict`, effects and temp events.
- Priority: P0.
- Dependencies: `FR-10-T05`, `FR-10-T06`; aligned with `DV-04-T02` ownership
  cleanup.
- Current state: In Progress at live canonical snapshot-consumer, source-gated
  remote-transform authority, and a default-off native present-once event
  cutover for the currently projected legacy effect families, plus bounded
  native snapshot semantic admission and production snapshot/event ownership.
- Progress: the client accepted-frame shadow now feeds parity-qualified
  immutable V2 views to `WORR_CGAME_SNAPSHOT_TIMELINE_EXPORT_V2`. External
  cgame owns a bounded copied canonical timeline and exposes clock, pair
  selection, entity sampling, snapshot/player copy-out, event iteration, reset,
  and diagnostics helpers. Selecting a stored seek snapshot starts a new
  projection/timeline epoch and feeds monotonic per-frame exact times, while a
  sequential forward seek keeps the existing clock lineage. Each render frame
  now advances the canonical clock with explicit pause/resume, selects an
  immutable pair at the legacy-equivalent target time, samples remote entity
  transforms, and either audits parity or promotes only a parity-proven entity
  behind `cg_snapshot_timeline_render`; local prediction and every rejected
  entity fall back independently. The durable event journal advances an
  ordered present-once audit cursor at the selected pair time. A live default-
  off native lane now submits final-emission legacy-entity events through the
  engine-owned cgame authority endpoint. Descriptor and event ACKs require a
  fresh exact cgame status/receipt, while semantic resync retires ACK authority
  and canceled-epoch DATA cannot invoke the consumer. The production-wrapper
  virtual-link gate reaches this real authority endpoint, records exactly one
  presentation, and proves that corrupt current traffic plus delayed canceled
  DATA cannot invoke cgame. That historical gate remains audit-only evidence;
  a later production presenter continuation described below supplies the real
  effect boundary but still needs its live fault/parity matrix.
  Stage D promotes the snapshot timeline export to ABI V2 and adds an exact
  admission generation, snapshot ID/hash/time, endpoint/parity hashes, and
  separate timeline/event-fence receipt flags. Snapshot DATA may authorize an
  ACK only after cgame publishes a fresh exact receipt and the event runtime
  reports the same snapshot epoch healthy without a resync requirement.
  Legacy-inferred references satisfy this correctness fence with
  `cg_event_runtime_audit=0`; the cvar gates only optional body/join
  comparison retention.
  Lost-ACK repeats revalidate that current receipt without consuming the
  snapshot twice; uncertainty clears the consumer, retires semantic receipt
  authority, quarantines the epoch, and requires a keyframe. In private
  `0x57` mode the real client frame boundary now compares legacy reconstruction
  without publishing it, while the native receiver is the sole canonical
  timeline publisher for the readiness epoch. Ownership remains latched
  through diagnostic DRAIN and transport-hook corruption/loss to prevent a
  mid-epoch double producer. Only map/serverdata or matching connection
  boundaries release it; failure handling detaches only exact owned hooks and
  preserves foreign replacements. The production-hook virtual link proves
  this real client RX path. A continuation adds
  `cg_snapshot_timeline_render=3`, which is unavailable unless the engine's
  read-only native-timeline ownership latch is active. It samples no later than
  the newest admitted native server time and promotes only safe remote
  transforms, retaining per-entity fallback and controlled-player prediction.
  A three-repeat hidden-client gate records 303/303/261 samples,
  84/287/189 native-authority samples, the same promotion counts, and zero
  clock/pair/alignment/parity/event-fence/endpoint/transform-error failures.
  That same production
  path now carries a deterministic server-consumed cursor into each admitted
  snapshot, copies the exact retained snapshot/player bundle by value, and
  proves four cgame publications become exact canonical prediction-authority
  candidates once each. The production assertion reports
  `prediction_ready=4` with digest `7176afa3d4eb62b2`; authority remains
  default-off and this does not promote rendering or presenters.
  The serialized corpus extends immutable timeline admission and pure
  prediction-authority selection to 100,000 positives across four epochs. It
  covers four `{epoch,0}` bootstraps, 99,996 nonzero cursors, pending and
  non-pending ranges, 43 history resets, and four 127-command ranges. The
  locally fabricated resolver history stops before `cg_predict`, PMove,
  presenters, and engine V2 request construction.
  A 2026-07-20 continuation adds a synchronous two-phase cgame presenter.
  Side-effect-free `CanPresent` resolves every authority entity from the exact
  retained snapshot ID/generation and checks only resources cached during map
  precache before the journal mark; the post-mark `Present` consumes copied
  values and cached handles. Runtime mutation and presenter replacement are
  rejected/ignored while either callback runs. The bounded families cover
  legacy entity/temp events, player/monster muzzle flashes, projected spatial
  audio, damage indicators, help-path markers, and intentionally nonvisual
  receipt/state payloads. Unknown effects and generic local-action audio remain
  fail-closed T08 work. A separate cgame effect-authority gate defaults false
  and is not connected to native ACTIVE: exact fence/generation validation,
  ordering, audit, and terminal consumption continue, but no value-effect
  callback runs and the raw legacy paths remain the production owner. Explicit
  test enablement proves dispatch plus missing-resource/busy-lifecycle
  rejection; callback reentry, default-off audit-only behavior, and prepared-
  plan scrubbing are covered independently. Entity-channel audio, muzzle
  effects, and delayed impulse sounds use exact copied origins, while generation
  participates in native dynamic-light identity.
  A second 2026-07-20 continuation adds the default-off
  `cg_native_event_preflight_probe`. Cgame samples it once before map resource
  registration, so mid-map changes cannot create a partially prepared
  presenter. An active latch prepares the same cached sound resources and runs
  the same pure per-family readiness/lifecycle checks as explicit effect
  authority. Successful records still cross the preflight, present-once mark,
  audit accounting, and post-mark callback boundary, but the retained dispatch
  kind is `none`; raw legacy effects remain solely authoritative. Fixed,
  saturating per-map status records requested/latched/active state, resource
  state, total commits, suppressed/nonvisual totals, and seven family counts.
  Focused tests cover map latching, missing resources, zero effect calls,
  single-use accounting, and effect-authority precedence. This safe probe does
  not satisfy the outstanding real-process fault/parity or ownership gates.
  A third 2026-07-20 continuation makes previous-only presentation evidence
  explicit at the renderer boundary. The value-owned render view carries a
  scrubbed per-source classification, and cumulative
  `previous_only=observed/selected/submitted` telemetry advances `submitted`
  only after `V_AddEntity`, with the shared invariant
  `submitted <= selected <= observed`. The version-38 headless presentation
  parent also validates the existing adaptive-delay diagnostics against
  explicit 50/150 ms baseline/ceiling controls. Three fresh live repeats each
  captured `0/0/0` before fire and a positive ordered `2/2/2`, `3/3/3`, or
  `2/2/2` renderer delta afterward. They reported 4--6 phase-local adaptive
  rises, 57.8--67.1 ms selected delay, and zero controller failures. This
  proves one ordinary live cadence; it does not replace the broader fault,
  demo, parity, load, or platform parents.
  A fourth bounded 2026-07-20 continuation adds per-client damage as the fifth
  legacy action-message family and submits its protocol-bounded one-to-four
  indicators in one transactional V2 range. Controlled-player lineage is
  synchronized from the exact current canonical snapshot when first-person
  packet frames omit that entity; stale, mismatched, or rewinding lineage
  rejects. Same-coordinate native EVENT records can now form a schema-2 two-
  to-eight-record ACK/retry unit. A strict three-repeat same-process Blaster
  parent passes both phases around one `gamemap` transition: all six phases
  record the exact five-action profile `(0,0,2,1,1,1,0)`, one four-record
  schema-2 impact batch plus one muzzle singleton, five delayed-ACK retries,
  five raw effects, five present-once/probe commits, zero native effects, exact
  chain parity, and zero failures/duplicates/mismatches. Map generation advances
  `1 -> 2`, map-end count `0 -> 1`, and event stream epoch `2 -> 4`. Server map
  quiesce cancels prior native state before map-local command reset; retained
  cgame receives one `Shutdown()` before the next `Init()`; and an exact sealed
  end-of-Begin reliable prefix gives the isolated CHALLENGE precedence over
  later reliable appends. Focused rows pass 9/9, runner units pass 96/96, and
  the v100 parent artifact hash is
  `545C6A46BD2E6A952958945CCE2F7DF2175551AE9423D5FFAF150DD31DA841D3`.
  This proves one clean/delayed-ACK same-map profile, not the outstanding
  compound fault/demo/visual-audio parity or ownership matrices.
  A fifth bounded 2026-07-20 continuation adds keyed POI as carrier/probe kind
  7 and advances the fixed probe status to schema 3/336 bytes/eight kinds.
  Raw capture remains keyed-only and legacy authoritative. Each lane validates
  its own exact retained snapshot and controlled identity; the POI parity
  projection then hashes a validator-valid neutral value plus the common map
  epoch because reliable raw/native copies are necessarily fenced to adjacent
  snapshots. Same-key actions remain distinct FIFO entries. The two-phase
  sink reproduces legacy infinite lifetime, removal, disabled, capacity, and
  missing-image state behavior without preflight mutation or registration.
  Native effect authority remains behind the existing dormant cutover gate.
  Legacy rendering/effect/audio presentation remains authoritative by default;
  a future per-carrier/exhaustive ownership negotiation must select the bounded
  event owner above. Source-gated
  transforms, previous-only renderer submission, one live adaptive cadence,
  and current presenter families are implemented, but the live loss/rate/
  pause/demo presenter and adaptive-policy matrix, generic predicted
  audiovisual reconciliation, performance budgets, demos, and classic-cgame
  migration remain open.
- Evidence:
  `docs-dev/networking-snapshot-timeline-core-t07-2026-07-12.md`,
  `docs-dev/networking-live-client-snapshot-prediction-and-demo-clock-2026-07-12.md`,
  `docs-dev/networking-live-cgame-canonical-render-promotion-2026-07-13.md`,
  `docs-dev/networking-cgame-canonical-event-presentation-journal-2026-07-13.md`,
  and
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-directional-event-shadow-virtual-link-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-t06-native-snapshot-semantic-admission-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`,
  and
  `docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`,
  and
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-production-v2-pmove-replay-boundary-2026-07-17.md`,
  plus `docs-dev/fr-10-t06-t07-t14-native-snapshot-presentation-authority-2026-07-18.md`
  and
  `docs-dev/fr-10-t07-native-present-once-event-effects-2026-07-20.md`
  and
  `docs-dev/fr-10-t07-native-event-preflight-probe-2026-07-20.md`, plus
  `docs-dev/fr-10-t07-previous-only-render-and-adaptive-live-evidence-2026-07-20.md`,
  and
  `docs-dev/fr-10-t07-schema2-damage-map-reuse-present-once-evidence-2026-07-20.md`,
  plus
  `docs-dev/fr-10-t04-t05-t07-keyed-poi-native-shadow-2026-07-20.md`.
- Definition of Done:
  - Cgame consumes immutable snapshot and event ranges without retaining mutable
    engine pointers across generations.
  - Remote entities interpolate on an explicit timeline and use bounded,
    policy-driven extrapolation only when valid.
  - Discontinuities, missing snapshots, pauses, rate changes and demo playback do
    not blend invalid states.
  - Event dispatch is ordered, deduplicated and separated from raw parsing.

### `FR-10-T08` Full client prediction and reconciliation

- Area: `bgame`, `cg_predict`, cgame player/weapon state and client command history.
- Priority: P0.
- Dependencies: `FR-10-T02`, `FR-10-T05`, `FR-10-T07`, `FR-10-T09`.
- Current state: In Progress at authoritative movement replay and local
  fail-closed reconciliation scope.
- Progress: the live snapshot consumed-command cursor now enters cgame through
  a versioned, immutable value-copy input-range import. Once canonical cursor
  authority is established, cgame maps the exact consumed ID to retained local
  history, verifies every successor ID, ignores packet ACK, and replays only
  those successors plus a copied pending command. Negotiated `{0,0}` bootstrap
  and truly legacy peers retain an explicitly tagged packet-ACK fallback;
  missing, ambiguous, discontinuous, invalid, or over-capacity canonical
  history triggers a local authoritative-state hard resync. In-engine retained
  state loss, movement-configuration discontinuity, and deterministic replay
  rejection now use that same full-ring hard-resync path rather than leaving a
  partial local prediction; correction telemetry names each recovery or
  divergence cause. A V2 canonical-required import now resolves replay from
  the exact consumed cursor copied out of one retained immutable snapshot and
  never substitutes packet acknowledgement. A pure fail-closed selector
  validates the snapshot/player hashes, active epoch, exact sequence/tick/time,
  controlled-entity identity and generation, cursor provenance, and continuous
  command range before admitting the value copies together. The non-archived
  `cg_prediction_snapshot_authority` control defaults to legacy mode, can audit
  canonical parity without changing gameplay, and can explicitly promote only
  an admitted pair. Promotion seeds correction and movement replay from the
  canonical player state and exact cursor-bound range; unavailable or
  mismatched authority blocks legacy replay, clears every prediction-history
  ring, reapplies a safe visible authority seed, and emits classified
  canonical-authority telemetry. Accepted snapshot discontinuities clear
  retained prediction history once for that exact snapshot. Focused selector
  and source-contract coverage exercises successful selection, stale/future/
  wrong-epoch/tick/time/entity cases, truncation, invalid hashes and cursors,
  malformed ranges, hard-resync clearing, and the absence of a direct legacy
  replay seed in promotion. The serialized corpus now resolves 250,500 locally
  fabricated commands and selects 100,000 exact authorities from nonempty
  ranges, covering bootstrap, pending/non-pending, discontinuity, 127-command
  limit, and one deliberate 128-command rejection. A separate production-linked
  focused gate now crosses the boundary the corpus intentionally omitted: the
  shipped engine V2 request constructor resolves the admitted receipt's exact
  cursor, and real cgame `CL_PredictMovement` executes shared PMove. The gate
  proves pending/finalized state/collision/replay-hash parity, a legal
  127-command replay, fail-closed 128-command ring clearing, bounded correction,
  threshold hard reset, and event-fence receipt withholding. This is four
  deterministic replay calls, not sustained impairment/load evidence; weapon
  replay, side-effect suppression, presenter de-duplication, and correction
  budgets remain open. A transport-neutral
  local-action v2 convergence core now proves exact-command weapon/ammo/phase
  transitions, deterministic gameplay/audio/effect keys, event-ABI adaptation,
  correction classification, byte-atomic failure, and 4,096-command
  prediction/authority parity. A separate fixed-capacity audit ring now pairs
  the exact predicted and authoritative transactions in either arrival order,
  retains immutable correction evidence, blocks unsafe pruning, and rejects
  connection/command identity conflicts transactionally. It is diagnostic-
  only for live gameplay, but its operational-cost promotion blocker is
  closed: allocation-free O(n) operational validation is separate from the
  explicit whole-ring deep audit, and maximum-capacity benchmarks enforce a
  500 microsecond hot-path gate plus a 10 ms destructive-lifecycle gate.
  The new command-scoped authoritative action-observation ledger now captures
  the actual sgame pre/post weapon state for every validated canonical callback
  and separately counts `Think_Weapon` advances outside that scope. It is
  deliberately observational: no action state, event, packet, snapshot, or
  presentation authority moved. This establishes the required oracle and
  exposes the current frame-driven timing boundary before a full catalogue
  adapter can be allowed to predict. The oracle now also separates the
  independently mapped `+hook` command intent from selected-weapon attack and
  records whether the server has an active hook after each canonical callback.
  It still provides evidence only. The command-identity owner now retains the
  exact immutable command record at finalization, independently of the
  optional native pilot, and exposes it through a separate versioned cgame
  record-range import without changing the movement-input ABI. A small
  independent Hook interaction core and cgame shadow consume only those
  records, validate their IDs and payloads against the accepted replay range,
  and model a rising edge as a request-pending transaction. Missing
  predecessor/history evidence suppresses the unprovable edge; the shadow
  never derives timing from packet ACK and cannot create collision, damage,
  attachment, sound, visuals, or an active hook. Sgame now builds the matching
  authoritative Hook transaction inside its existing authenticated pre/post
  callback scope and retains it in a bounded value-only, exact-command ledger.
  Bootstrap, epoch, order, or observed-state discontinuities rebase from the
  canonical record plus observed pre-state rather than inventing a transition.
  The ledger now also projects a compact receipt only for authoritative Hook
  request edges: exact command/hash, action sequence, state hash, and either
  active or rejected outcome. Cgame pairs that receipt with its bounded
  request cache in either arrival order; duplicates are inert while an
  ID/hash/action-sequence conflict requires local shadow resync. The exact
  receipt now crosses a map-scoped engine mailbox and the existing negotiated,
  default-off private per-peer event stream after that authenticated command
  completes. A dedicated critical/reliable/authoritative-only receipt record
  has absent entity references, never receives a snapshot fence or generic
  presentation, and resets its cgame pairing cache on event epoch change. No
  legacy packet, snapshot field, demo record, public cvar, Hook activation,
  collision, damage, attachment, audiovisual output, or other presentation is
  emitted. The deterministic native virtual-link harness now installs the
  production cgame runtime and proves that one private receipt crosses the
  live descriptor/DATA/ACK path, pairs it with a real cgame-predicted Hook
  request for both confirmed and rejected authority outcomes and either
  prediction/receipt arrival order, terminally skips generic presentation,
  survives a lost ACK plus two retry deliveries without duplicate correction,
  and retires only after its exact final ACK. A distinct valid receipt for the
  same command now proves the live native admission path scrubs cgame authority,
  rejects the carrier without ACK, retains the server record, and requires a
  fresh event epoch. That recovery is also proven: map rotation cancels the
  retained conflict, and only the fresh descriptor followed by its ACK clears
  resync and opens promotion for a new exact receipt. Receipt-first authority
  evidence is now also bounded by canonical history: if its exact command has
  fallen behind the prediction frontier, cgame latches a hard resync through
  the event-runtime health bit before any queued ACK can reach the server.
  The harness is in-process and input-free: it creates no
  socket/window and cannot capture the mouse. Command-time ownership/lease,
  audiovisual suppression, live shadow parity, and correction-budget promotion
  remain open.
  The first complete catalog layer is now present: a pointer-free shared ABI
  freezes 22 stable ordinary-weapon identities, their exact current
  `Weapon_Generic`/`Weapon_Repeating`/`Throw_Generic` driver class, inventory
  ownership shape, and a mandatory shadow-only promotion state. An sgame
  adapter validates unique `Weapon`/`item_id_t`/ammo round trips against the
  live item list, production weapon-preference and chat mapping use that
  adapter, and the authenticated observation ledger counts scoped records plus
  scoped/unscoped weapon thinks by catalog ID. Grapple remains explicitly in
  the separate Hook interaction domain. Exact digest `e857eec08cfa9c00` and
  hostile adapter tests pass. A second shared profile now freezes every
  ordinary weapon's exact legacy frame boundaries/lists, throw hold/release
  frames, BFG alternate list, and Ion Ripper refire gate with digest
  `4f723f6fddf5bf52`; the 22 production wrappers consume those profiles while
  the legacy drivers/callbacks remain authoritative. A third pointer-free
  descriptor freezes each callback's trigger/emission family, direct
  dependencies, special behavior, per-callback ammo/emission bounds, and exact
  local-action V2 blocker mask. Its digest is `a5d823b554b31ee8`, and the
  hostile audit proves zero of 22 entries are representable by V2. Production
  wrappers validate the descriptor. A bounded observation-only lease now
  assigns the latest exact authenticated command to at most one later
  `ClientBeginServerFrame` weapon advance, expires on every frame path, retains
  separate scoped/leased records, and joins them only across exact non-timer
  state plus equal bounded relative-timer decay. It does not extend the engine
  command context. Three hidden-client Blaster repeats prove the production
  callback lease, including claims, expiries, supersedes, joined records, and
  zero rejection. A descriptor-complete 528-byte shared shadow record now
  binds each joined observation to the exact catalog/profile/semantics facts,
  three frozen digests, observed comparable ammo delta, V2 blockers, and
  stable hashes. The ordered 22-record golden is `1270244792631122`; schema
  v30 proves live Blaster catalog `1`, flags `127`, blocker mask `4367`, and a
  nonzero record hash in all three repeats. A compact 64-byte authority
  receipt now carries those immutable hashes and blockers through a
  default-off sgame-to-engine mailbox and native event payload kind `12`.
  Queue backpressure retains the oldest receipt, reconnect/client-slot reuse
  clears stale mail, and the cgame audit owner correlates command-first or
  receipt-first arrival against the exact canonical command semantic hash.
  A production-wrapper virtual link proves descriptor admission, fieldwise
  WNE1 delivery, exact command match, zero presentation, ACK, and server
  release. A three-repeat schema-v32 two-process gate now disconnects and
  reconnects the admitted shooter in-session, observes command epoch 3, then
  proves 45 exact matches for 45 live authority receipts with zero unmatched,
  outstanding, mismatch, conflict, or resync. Each run also retains the exact
  scoped/leased/joined Blaster shadow (catalog `1`, flags `127`, blockers
  `4367`) and normal 15 damage. The fixture latches only immutable diagnostic
  value copies after the exact join so bounded-ledger rotation and slow RCON
  polling cannot erase evidence; it does not alter weapon or receipt
  authority. Live reconnect rebase and cross-process receipt parity are now
  closed at this narrow default-off shadow scope, but no prediction or
  presentation authority moved. The 2026-07-20 lifecycle continuation gives
  private reconciliation an ordered cursor independent of visually fenced
  presentation, permits that cursor to cross admitted visual gaps without
  consuming them, rejects callback mutation/reentry, and explicitly consumes a
  valid final event sequence `UINT32_MAX` once. Command-only entries can roll
  when no receipt exists; a real receipt behind that coverage fails closed only
  when the exact V2 command record is also unavailable. A monotonic full-
  receipt frontier keeps latest duplicate identity and prunes lower command-
  only or exact matched-terminal rows while preserving receipt-only evidence.
  Server publication is likewise monotonic and any typed invalid/regression/
  conflict/capacity/order failure poisons the mailbox until reset and drains
  the peer as native failure `18`. Standalone schema-v36 combined evidence
  passes 29/29 exact matches. The final post-stage T04 parent passes 11/11
  focused plus 5/5 live while staying partial; its combined child passes 30/30
  exact matches with zero reconciliation fault. The hardened manifest includes
  local-action correlation as its eleventh focused child. A fixed 1,024-entry correlation
  table is derived from the 798-entry total of client pending commands, event
  TX, event backlog, selective-ACK successors, and server mailbox. Both full-
  bound arrival orders, more-than-two-table rolling streams, six focused Meson
  rows, production cgame build, refreshed stage, and the final integrated parent
  pass. No prediction or presentation authority moves.
- Evidence:
  `docs-dev/networking-authoritative-prediction-input-range-2026-07-12.md`,
  `docs-dev/networking-canonical-local-action-transaction-v2-2026-07-13.md`, and
  `docs-dev/networking-local-action-prediction-authority-audit-ring-2026-07-13.md`,
  plus
  `docs-dev/fr-10-t08-t09-command-scoped-authoritative-action-observation-2026-07-15.md`,
  and
  `docs-dev/fr-10-t08-cgame-prediction-fail-closed-recovery-2026-07-15.md`,
  plus
  `docs-dev/fr-10-t08-offhand-hook-authority-observation-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t08-independent-offhand-interaction-model-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t08-offhand-hook-authoritative-transaction-ledger-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t08-offhand-hook-authority-receipt-reconciliation-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t08-private-hook-authority-receipt-carrier-2026-07-16.md`,
  and
  `docs-dev/fr-10-t08-private-hook-authority-receipt-native-virtual-link-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t08-unmatched-authority-receipt-history-loss-2026-07-16.md`,
  and
  `docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`,
  and
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-production-v2-pmove-replay-boundary-2026-07-17.md`,
  plus
  `docs-dev/fr-10-t08-t09-rerelease-weapon-catalog-boundary-2026-07-17.md`,
  and
  `docs-dev/fr-10-t08-t09-weapon-semantics-v2-blocker-audit-2026-07-17.md`,
  plus
  `docs-dev/fr-10-t08-t09-post-command-weapon-observation-lease-2026-07-17.md`,
  `docs-dev/fr-10-t08-t09-post-command-weapon-lease-runtime-acceptance-2026-07-17.md`,
  and
  `docs-dev/fr-10-t08-t09-descriptor-complete-local-action-shadow-2026-07-17.md`,
  plus
  `docs-dev/fr-10-t04-t05-t08-t09-local-action-shadow-authority-receipt-2026-07-17.md`,
  and
  `docs-dev/fr-10-t04-t05-t08-t09-t14-live-reconnect-local-action-authority-2026-07-18.md`,
  plus
  `docs-dev/fr-10-t04-t08-private-receipt-bounded-lifecycle-2026-07-20.md`.
- Definition of Done:
  - All declared predictable local state replays from the authoritative
    consumed-command watermark established by `FR-10-T09` through
    unacknowledged input.
  - Predictable audiovisual events match authority without duplicate playback.
  - Corrections are classified and instrumented; collision state is immediately
    authoritative while visual smoothing remains presentation-only.
  - History overflow and invariant failure trigger a safe hard resync.

### `FR-10-T09` Canonical command identity and consumed-input acknowledgement

- Area: client command production, server command intake, canonical snapshot
  acknowledgement, and prediction history.
- Priority: P0.
- Dependencies: `FR-10-T02`, `FR-10-T03`, `FR-10-T05`, `FR-10-T06`.
- Current state: Done on 2026-07-19.
- Progress: Phase 1 defines a pointer-free canonical command record that embeds
  the completed T02 input payload, uses explicit `{epoch, sequence}` identity,
  validates cumulative sample time and bounded render-time provenance, and
  excludes packet sequence/acknowledgement from semantic identity. A
  caller-owned fixed-capacity stream independently tracks contiguous receive
  and simulation-consume cursors, rejects gaps/conflicts/overflow, retains the
  first legacy retry context, and reclaims only consumed head records. Hostile
  wrap/reset/duration/provenance/alias tests and C/C++ layout checks are in the
  Meson networking suite. Phase 2 adds a packet-scoped, nine-pair signed-setting
  sideband with CRC/commit validation and a transactional MOVE/BATCH adapter.
  It proves duplicate/stale backup handling, the phantom-backup bootstrap,
  124-command batches, wrap/exhaustion, reordered retry, checked aliases, and
  byte-identical failure. The client now assigns contiguous IDs and atomically
  stages the identity tuple with MOVE/BATCH_MOVE. At that same
  command-finalization boundary, command identity retains immutable canonical
  records through the shared cumulative sample-clock builder so cgame can
  consume exact bounded records without a packet-ACK reconstruction or
  optional-native-pilot dependency. The server validates the
  negotiated adjacent range, advances `consumed_cursor` only around
  authoritative simulation, and publishes the post-callback cursor before
  snapshots. The client requires the cursor tuple, binds its established epoch
  to the negotiated session, rejects regression, attaches it to snapshots and
  demos, and drives cgame replay from the exact consumed ID. Cgame can now ask
  the engine's V2 import for a canonical-only value range using the cursor
  copied from one exact retained snapshot; the resolver rejects absent,
  non-server, mismatched, or discontinuous identity and cannot fall back to a
  packet ACK. The prediction-authority selector then proves that snapshot
  cursor and returned range are the same transaction before replay can be
  promoted. The production virtual-link path carries that cursor through
  server projection, native serialization, receiver admission, legacy-shadow
  expectation, and cgame timeline publication, and records four exact
  prediction-ready admissions. The same identity enters the callback-scoped
  rewind context. In client-generated stored seek
  snapshots, the validated exact-time tuple is staged before the existing
  cursor tuple so cursor-to-frame adjacency and consumed-input authority are
  preserved. Event correlation now has a shared command-derived local-action
  key model and a diagnostic prediction/authority pairing ring. The ring now
  exposes allocation-free O(n) operational V2 validation under a measured
  maximum-capacity hot-path budget while preserving explicit deep corruption
  audit. The default-off native production pilot additionally maps a repeated
  sampled sequence from exact encoded legacy ranges into `WNC1` with the shared
  `usercmd_t`-to-prediction converter. Each fresh transport bank carries its
  official command epoch, and the server joins either native or
  legacy arrival order while preserving the legacy consumed cursor and
  simulation authority. Successful comparisons retire their bounded join slots
  and advance a canonical command high-water so a fresh transport identity
  cannot replay an already matched command. Three staged two-process runs prove
  453 mapped commands cross actual client/server processes, match their legacy
  joins, receive exact ACKs, and release client retention with no final residue
  or mismatch. The command stream also computes forward identity
  distance in O(1),
  separates its 128-slot retention capacity from a packet-history-derived
  4,096-command policy ceiling, preflights sample/epoch failure, and advances
  over-retention loss transactionally with distinct server telemetry. The
  serialized corpus additionally binds 100,000 admitted snapshots to exact
  server-consumed cursors over four epochs and resolves continuous nonempty
  canonical ranges through the 127-command boundary. The local-action
  continuation provides one real event-correlation
  slice across an in-session reconnect: the exact finalized command-record V2
  import and input-only semantic hash pair epoch-3 client commands with 45/45
  cross-process receipts over three repeats, with zero mismatch, conflict, or
  resync. The final task-level acceptance gate then runs two identical
  million-command probes across natural sequence rollover, maps 100,002
  commands through the legacy sideband/adapter and the explicit native codec,
  records 2,100,002 idempotent acknowledgements and 139,998 hostile rejections,
  and obtains deterministic digest `a9685d1ac8f40ef6`. Nine compiled gates bind
  that core to the production consumed-cursor prediction resolver,
  authoritative rewind context, local-action correlation owner, and native
  event/snapshot links. This directly satisfies every published Definition of
  Done item without making native transport or presentation authoritative.
  With `FR-10-T06` complete, every dependency is satisfied and the separate
  review transition closes T09.
- Evidence:
  `docs-dev/networking-canonical-command-stream-core-2026-07-12.md`,
  `docs-dev/networking-legacy-command-adapter-core-2026-07-12.md`,
  `docs-dev/networking-consumed-command-cursor-svc-setting-sideband-2026-07-12.md`,
  `docs-dev/networking-consumed-command-cursor-server-live-carrier-2026-07-12.md`,
  `docs-dev/networking-authoritative-prediction-input-range-2026-07-12.md`,
  `docs-dev/networking-authenticated-command-context-2026-07-12.md`,
  `docs-dev/networking-live-client-snapshot-prediction-and-demo-clock-2026-07-12.md`,
  `docs-dev/networking-canonical-local-action-transaction-v2-2026-07-13.md`,
  `docs-dev/networking-local-action-prediction-authority-audit-ring-2026-07-13.md`,
  `docs-dev/fr-10-t09-bounded-command-gap-fast-forward-2026-07-13.md`,
  `docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-native-two-process-async-ack-impairment-gate-2026-07-14.md`,
  with current repeated-stream evidence in
  `docs-dev/fr-10-t04-repeated-command-shadow-and-mixed-carrier-core-2026-07-14.md`,
  plus dedicated large-gap acceptance evidence in
  `docs-dev/fr-10-t09-t16-headless-command-gap-acceptance-gate-2026-07-15.md`,
  and exact snapshot/cursor authority evidence in
  `docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`,
  plus serialized production-corpus evidence in
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`,
  and focused production replay evidence in
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-production-v2-pmove-replay-boundary-2026-07-17.md`,
  plus live reconnect correlation evidence in
  `docs-dev/fr-10-t04-t05-t08-t09-t14-live-reconnect-local-action-authority-2026-07-18.md`,
  and final direct-acceptance evidence in
  `docs-dev/fr-10-t09-canonical-command-acceptance-gate-2026-07-19.md`.
- Definition of Done:
  - Every command has a wrap-safe canonical identity, validated duration, sample
    time, and render-time watermark.
  - The server publishes the last command actually consumed by authoritative
    simulation; packet acknowledgement is never substituted for that watermark.
  - Duplicate commands are acknowledged idempotently and never simulated twice;
    future, stale, malformed-duration, flood, and sequence-wrap cases fail
    closed with bounded telemetry.
  - The legacy adapter maps the canonical identity without changing `q2proto/`;
    `FR-10-T04` later gives the WORR adapter an explicit wire representation.
  - Prediction, rewind, and event correlation consume the same command identity.

### `FR-10-T10` Bounded timestamped full-pose server rewind

- Area: server history, sgame collision query bridge, time synchronization.
- Priority: P0.
- Dependencies: `FR-10-T02`, `FR-10-T06`, `FR-10-T09`.
- Current state: Done on 2026-07-19.
- Progress: server-validated acknowledgements map to authoritative simulation
  frames and validated contiguous-snapshot intervals; first/suppressed gaps use
  an explicit no-interpolation sentinel. The live canonical command path now
  exports an API-version-2 callback scope with inactive-legacy, active-valid,
  and active-rejected states. Canonical proof rejection and synthesized-gap
  commands cannot fall back to packet-ack rewind. Sgame uses the common
  512-pose history, lifecycle/discontinuity policy, per-map/client resets, one
  sealed player-bounds scene per command, immutable trace views,
  generation-checked live revalidation, and per-ray ignore sets. The source
  snapshot is still authenticated through a server-owned projection of the
  legacy frame ring rather than a materialized canonical server snapshot
  store. A pointer-free 256-entry per-query observation journal now records
  path/reason/times/candidate/scene/hit/duration fields and a before/after live
  collision-state guard. A versioned 40-case, three-repeat acceptance runner
  covers eight weapon policy tags at 0/50/100/200 ms plus cap, stale/future,
  history, teleport, respawn, slot-reuse, and disable boundaries in the common
  production core. A versioned engine extension now resolves immutable BSP
  inline models into epoch/hash-bound handles and performs transformed ray/box
  traces without exposing, editing, unlinking, or relinking an edict. The
  provider and process-local import layouts pass native plus i686 C/C++ checks.
  A real generated IBSP38 fixture now drives the production map loader and
  compares the public extension directly with `SV_Clip` across ten translated/
  rotated ray and box cases plus four rejection classes, with byte guards over
  live edict/link state. Sgame now captures bounded live `SOLID_BSP`
  push/stop mover history after final player end-frame state, records
  generation-checked player-to-ground-mover relative provenance, and seals
  matching immutable brush poses into the canonical scene. Capture uses 64
  tracks of 64 poses, resolves each current map asset through a C++-safe mirror
  of the engine import ABI, and fails scene construction closed if an eligible
  live mover lacks a history. Canonical traces now validate each sealed brush
  identity/asset against the current map epoch, omit only those matching live
  movers from the current-entity baseline, and dispatch immutable transformed
  brush traces at the sealed origin/angles before merging player bounds. Any
  validation or trace failure returns the complete uncompensated current-world
  query rather than a partial replacement. The authority fingerprint now covers
  live movers as well as players. A headless dedicated-server runtime gate uses
  a packaged collision-capable map, captures an actual live mover, seals its
  historical scene, relocates the live collider, confirms the excluded current
  baseline is clear, dispatches a blocking historical BSP trace, and restores
  the authoritative state. The asymmetric fixture now also captures the mover
  at 90 degrees, requires an unrotated immutable-asset negative control to be
  clear, and proves the sealed rotated pose blocks. Its three fresh-process V5
  rows are identical: a start-on rotating pusher carries a real bot through
  twelve normal server frames, and paired end-frame player/mover captures must
  show changed mover angle and rider world origin while preserving exact
  mover-relative data. The newest pair must also enter an exact sealed
  canonical scene. This fixture blocks at fraction 0.374756. A separate
  schema-v28 real-command Railgun mode now relocates that live mover only after
  authenticated command mapping, requires a clear current baseline, and lets
  the concrete production Railgun pierce trace terminate on the
  generation-matched sealed historical mover. Three headless two-client
  repeats apply 56 ms of rewind from 64 retained mover samples, preserve
  collision authority, and leave the target behind the mover at exactly zero
  damage. The final production-bound load fixture fills all 32 player and 64
  mover histories, appends and queries all 96 owners at an exact 200 ms age,
  seals the maximum 96-candidate scene, and repeats 256 individually timed
  one-frame workloads after 32 warmups. Five fresh dedicated processes each
  record exactly 27,648 bounded queries/overwrites, zero overflow, full rings,
  sealed scenes, allocation-free fixed storage, unchanged live authority, and
  identical workload hash `4973674365086484083`. Production fixed owners use
  4,370,192 bytes; the deliberately stronger production-plus-fixture bound is
  7,950,576/8,388,608 bytes. Worst p95 is 1,075,300 ns under the 1,666,600 ns
  limit, leaving 591,300 ns (35.5%) headroom. The common query now uses
  division-free one-wrap indexing and a
  newest-first ordered search; duplicate, wrap, generation, discontinuity,
  map-epoch, 40-case/120-invocation, and 16,384-query differential coverage
  preserves exact semantics. Broader gameplay interactions, concurrent-client
  stress/soak, and release-platform evidence remain with T12, T14, and T15.
- Evidence: `docs-dev/networking-authenticated-command-context-2026-07-12.md`,
  `docs-dev/networking-live-canonical-rewind-scene-and-hitscan-2026-07-12.md`,
  `docs-dev/networking-rewind-observability-acceptance-evidence-2026-07-13.md`,
  `docs-dev/fr-10-t10-immutable-brush-collision-extension-2026-07-13.md`,
  `docs-dev/fr-10-t10-live-mover-pose-history-2026-07-15.md`,
  `docs-dev/fr-10-t10-sealed-historical-brush-dispatch-2026-07-15.md`, and
  `docs-dev/fr-10-t10-headless-moving-brush-runtime-gate-2026-07-15.md`,
  `docs-dev/fr-10-t10-rotated-moving-brush-runtime-gate-2026-07-15.md`,
  `docs-dev/fr-10-t10-live-player-on-mover-provenance-gate-2026-07-15.md`,
  `docs-dev/fr-10-t10-normal-frame-rider-continuity-gate-2026-07-15.md`, and
  `docs-dev/fr-10-t10-normal-frame-canonical-scene-gate-2026-07-15.md`, and
  `docs-dev/fr-10-t10-t11-canonical-rail-historical-mover-occlusion-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t10-bounded-rewind-acceptance-closure-2026-07-19.md`.
- Definition of Done:
  - Bounded pose history includes collision-relevant player and mover state,
    stable identity and discontinuity markers.
  - Timestamp mapping, interpolation, clamp/reject reasons and history misses are
    deterministic and observable.
  - Queries do not mutate or restore the live world and cannot cross invalid
    identity, teleport, respawn or map boundaries.
  - Memory, query count and server-frame CPU budgets pass load tests.

### `FR-10-T11` Authoritative hitscan lag compensation

- Area: sgame weapon traces, server rewind query policy and damage validation.
- Priority: P0.
- Dependencies: `FR-10-T10`.
- Current state: Done on 2026-07-19; historical collision remains default-off.
- Progress: machinegun, chaingun, shotgun, super shotgun, railgun, disruptor,
  plasma beam, and thunderbolt convergence/trace queries use one cached
  canonical decision and sealed historical scene per command. Piercing uses
  generation-checked per-ray ignore identities rather than changing solidity
  or relinking live entities. A canonical rejection produces an uncompensated
  authoritative trace and cannot use the legacy estimate. Damage, knockback,
  death, effects, and radius damage execute against current authoritative
  state. Every integrated trace now supplies an explicit weapon-policy tag to
  the bounded observation seam, and the deterministic common-core matrix
  proves the currently declared timing/discontinuity boundaries. A dedicated
  `fire_rail` acceptance gate now proves an invalid current-frame
  acknowledgement falls back to a live miss with no damage, while near,
  in-budget, and over-budget recorded acknowledgements each hit the historical
  target and apply real current-authority damage without query-time collision
  mutation. The over-budget row verifies the applied time is the rewind cap;
  all three valid rows require exactly 30 total damage. The legacy master switch
  remains default-off. A shared real-command fixture now also proves the normal
  machinegun bullet callback selects policy `1`, hits the canonical historical
  target at positive age, preserves current geometry, and applies exactly 8
  damage. Its standard Chaingun callback selects policy `2` and applies the
  full three-round 18 damage. Its ordinary two-barrel Super Shotgun callback
  selects policy `4` and applies exactly 120 damage (twenty six-damage
  pellets), while its shotgun pellet callback selects policy `3` and applies
  exactly 48 damage (twelve four-damage pellets). Its Disruptor convergence
  policy `6` selects a historical target but lets normal live projectile flight
  and the 500 ms damage daemon apply exactly 45 damage; its Railgun regression
  proves policy `5` and exactly 80 damage. A distinct policy-`5` Railgun mode
  relocates a live rotating mover after authenticated command mapping, proves
  the substituted current baseline is clear, then requires the concrete
  `fire_rail` pierce hit to stop on the sealed historical brush rather than
  accepting the earlier muzzle-projection query. Three headless two-client
  repeats retain 64 mover samples, apply 56 ms of rewind, preserve collision
  authority, and leave the target at exactly zero damage. The final parent gate
  runs the 40-case common matrix three times, all eight production weapon
  policies plus historical-mover occlusion, three-client spectator exclusion,
  and current-authority spawn protection three times each, then the invalid-
  authority and near/in-budget/capped fallback fixture three times. All 120
  matrix invocations and 36 fresh-process live runs pass with identical
  repeated semantics, unchanged authority, exact damage/fallback outcomes, and
  parent digest
  `aa8aba98e1bc4e514badf7aed8a04e7e026afa2bc4f26d333d022cb9b5364bc1`.
  User guidance documents all eight policies, server-owned hit/time authority,
  the 200 ms default/250 ms hard cap, current-state damage and lifecycle rules,
  spectators, disable/opt-out controls, and fail-closed fallback. Broader
  gameplay interactions, demo recording, concurrent stress/soak, platform
  breadth, and release promotion remain with T12 through T15.
- Evidence:
  `docs-dev/networking-live-canonical-rewind-scene-and-hitscan-2026-07-12.md`,
  `docs-dev/networking-rewind-observability-acceptance-evidence-2026-07-13.md`,
  `docs-dev/fr-10-t11-headless-railgun-damage-fairness-gate-2026-07-15.md`,
  `docs-dev/fr-10-t11-latency-class-rail-damage-gate-2026-07-15.md`,
  `docs-dev/fr-10-t11-canonical-command-rail-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-machinegun-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-shotgun-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-chaingun-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-super-shotgun-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-disruptor-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t10-t11-canonical-rail-historical-mover-occlusion-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t11-authoritative-hitscan-acceptance-closure-2026-07-19.md`.
- Definition of Done:
  - Each supported hitscan weapon uses one authoritative rewind policy and
    records requested/applied time plus clamp/reject reason.
  - The client never supplies a hit result or arbitrary rewind target.
  - Shooter, target, spectators, movers, spawn protection, death/respawn and
    discontinuity semantics have deterministic scenarios.
  - Fairness caps, opt-out/disable behavior and uncompensated fallback are
    documented and tested before default enablement.

#### 2026-07-15 canonical command-to-Railgun acceptance update

- Added the production-owned canonical rail-damage acceptance seam and its
  dedicated-server/two-real-client headless gate; details and task references
  are in `docs-dev/fr-10-t11-canonical-command-rail-damage-acceptance-2026-07-15.md`.
- The gate proves command-scope admission, real attack delivery, normal
  Railgun dispatch, retained target history, canonical historical target hit
  at a positive applied age, and live 80-damage application without synthetic
  context creation or direct `fire_rail` invocation.
- The production capture timeline now uses the engine authoritative simulation
  clock rather than the game-local pre-admission clock, and the staged
  three-repeat headless UDP gate passes. This closes the canonical-command
  Railgun acceptance seam, but does not complete `FR-10-T11`.

#### 2026-07-16 canonical Railgun historical-mover occlusion update

- Added a real-command two-client Railgun mode that relocates only the live
  rotating `SOLID_BSP` fixture mover after authenticated command mapping.
- The production query must see a clear substituted current baseline, and the
  concrete `fire_rail` pierce-hit callback must then stop on the
  generation-matched sealed historical mover. The preliminary
  `P_ProjectSource` trace cannot satisfy this proof. The target behind the
  mover must remain at exactly zero damage, and collision authority must remain
  unchanged.
- Three hidden, input-disabled repeats pass at 56 ms applied age with 64
  retained mover samples. This closes one bounded weapon/mover occlusion seam
  only; broader mover classes, moving-target/multi-target fairness, lifecycle,
  abuse/load, platform, and release gates remain open. Evidence:
  `docs-dev/fr-10-t10-t11-canonical-rail-historical-mover-occlusion-acceptance-2026-07-16.md`.

#### 2026-07-15 canonical command-to-machinegun acceptance update

- Generalized the real-command fixture by explicit policy, item, damage, and
  normal idle-frame configuration; it continues to use only the received attack
  and ordinary `Item::weaponThink` dispatch.
- Three headless two-client machinegun runs prove policy `1`, a positive-age
  canonical historical target hit, exact 8 current-authority damage, and
  unchanged current geometry. The same version-2 fixture reran Railgun three
  times with policy `5` and exact 80 damage.
- This closes the canonical-command machinegun seam only. `FR-10-T11` remains
  incomplete pending the remaining weapons, mover/lifecycle matrices,
  fairness/abuse/load gates, and release promotion.

#### 2026-07-15 canonical command-to-shotgun acceptance update

- Added the ordinary shotgun path to the shared fixture as policy `3`,
  `IT_WEAPON_SHOTGUN`, and an exact 48-damage (twelve-pellet) acceptance
  contract. It continues to receive only the real attack command and relies on
  normal `Item::weaponThink` and `fire_shotgun` dispatch.
- Three headless, input-disabled two-client repeats prove a positive-age
  historical target hit, no fallback, unchanged current geometry, and exact
  48 current-authority damage. The updated staged Railgun regression also
  passed three times at policy `5` and exact 80 damage.
- This closes the canonical-command shotgun seam only. `FR-10-T11` remains
  incomplete pending super-shotgun, disruptor, plasma-beam, and
  thunderbolt command-to-damage coverage; mover/lifecycle matrices;
  fairness/abuse/load gates; platform evidence; and release promotion.

#### 2026-07-15 canonical command-to-Chaingun acceptance update

- Added policy `2` Chaingun coverage to the shared fixture at its normal
  three-round burst frame. The acceptance contract requires exactly 18 damage,
  rejecting a one- or two-round result.
- The fixture clears only synthetic spawn-health bonus state so the normal
  target end-frame timer cannot add a post-burst point of damage to the
  measurement. Three headless, input-disabled two-client repeats now prove
  valid canonical scope, positive-age historical hits, no fallback, unchanged
  geometry, and exact 18 current-authority damage. The current staged fixture
  also reran Railgun, machinegun, and shotgun three times each, retaining their
  exact 80, 8, and 48 damage seams respectively.
- This closes the canonical-command Chaingun seam only. `FR-10-T11` remains
  incomplete pending disruptor, plasma-beam, and thunderbolt
  command-to-damage coverage; mover/lifecycle matrices; fairness/abuse/load
  gates; platform evidence; and release promotion.

#### 2026-07-15 canonical command-to-Super-Shotgun acceptance update

- Added policy `4` Super Shotgun coverage to the shared real-command fixture.
  It uses the normal two-barrel `fire_shotgun` path and requires the full 120
  damage from twenty six-damage pellets, rejecting a single 60-damage barrel
  or any partial pellet result.
- The Super Shotgun fixture uses only a closer ordinary player placement at
  64 units, preserving the normal player hull, canonical history, and current
  geometry split while making both normal ±5 degree barrels deterministic.
  Three headless, input-disabled repeats prove positive-age historical hits,
  no fallback, unchanged current geometry, and exact 120 damage.
- This closes the canonical-command Super Shotgun seam only. `FR-10-T11`
  remains incomplete pending plasma-beam and thunderbolt
  command-to-damage coverage; mover/lifecycle matrices; fairness/abuse/load
  gates; platform evidence; and release promotion.

#### 2026-07-15 canonical command-to-Disruptor acceptance update

- Extended the shared gate from instantaneous hitscan proof to explicit
  canonical weapon-damage v3 evidence. Disruptor policy `6` verifies canonical
  historical convergence target selection, then preserves live-world authority
  for normal projectile flight and its delayed damage daemon.
- The terminal state waits up to 1.5 seconds only for this weapon's normal
  delayed lifecycle, then requires the intended exact 45 current-authority
  damage. Three headless, input-disabled two-client repeats prove a
  positive-age historical query, no fallback, unchanged query geometry, and
  complete delayed damage; all report schema v3.
- This closes the canonical-command Disruptor convergence-and-damage seam
  only. `FR-10-T11` and `FR-10-T12` remain incomplete pending plasma-beam and
  thunderbolt command-to-damage coverage; mover/lifecycle matrices;
  fairness/abuse/load gates; platform evidence; and release promotion.

#### 2026-07-15 canonical command-to-Plasma-Beam first-tick acceptance update

- Added policy `7`, `IT_WEAPON_PLASMABEAM`, and exact 8-damage first-tick
  coverage to the shared real-command fixture. The received held attack enters
  the ordinary repeating weapon state; `Weapon_PlasmaBeam_Fire` and
  `fire_beams` retain exclusive ownership of the canonical trace and
  current-authority damage.
- Three headless, input-disabled two-client repeats report positive 56 ms
  applied age, canonical historical target hit, no fallback, unchanged current
  geometry, and exact 8 damage under the v3 report schema. The narrow proof
  intentionally excludes sustained command cadence, release, and water-retrace
  beam lifecycle claims.
- This closes the first command-to-Plasma-Beam tick seam only. `FR-10-T11` and
  `FR-10-T12` remain incomplete pending thunderbolt command-to-damage coverage,
  sustained beam/mover/lifecycle matrices, fairness/abuse/load gates, platform
  evidence, and release promotion.

#### 2026-07-15 canonical command-to-Thunderbolt first-tick acceptance update

- Added policy `8`, `IT_WEAPON_THUNDERBOLT`, and exact 8-damage dry-world
  first-tick coverage to the shared real-command fixture. The received held
  attack reaches normal `Weapon_Thunderbolt` and `fire_thunderbolt`, which
  retains ownership of the complete main/side-ray footprint and target
  de-duplication.
- Three headless, input-disabled two-client repeats report positive 56 ms
  applied age, canonical historical target hit, no fallback, unchanged current
  geometry, and exact 8 damage under the v3 report schema. The exact-damage
  contract rejects multi-ray accumulation and does not claim underwater
  discharge, water-retrace, or sustained/release lifecycle coverage.
- This closes the dry-world first command-to-Thunderbolt tick seam only.
  `FR-10-T11` and `FR-10-T12` remain incomplete pending broader beam and
  mover/lifecycle matrices, fairness/abuse/load gates, platform evidence, and
  release promotion.

#### 2026-07-15 held continuous-beam cadence acceptance update

- Extended the shared real-command fixture with bounded held-command modes for
  Plasma Beam policy `7` and dry-world Thunderbolt policy `8`. Each requires
  exact 24 current-authority damage from three ordinary 8-damage ticks and
  fails closed after 1.5 seconds; the status remains pending after one or two
  ticks.
- The runner refreshes a release/press edge only through the admitted hidden
  client while the cadence is pending, producing fresh normal attack commands
  without server-side input or direct weapon calls. A distinct current target
  remains on the aim ray for later current-history commands while the first
  retained-pose command still proves canonical historical selection.
- Three staged headless repeats per weapon pass at positive 56 ms age with no
  fallback, unchanged current geometry, exact 24 damage, and (for Thunderbolt)
  normal per-tick target de-duplication. This is bounded cadence evidence only:
  long holds, release, water/retrace/discharge, and the wider interaction
  matrices remain open under `FR-10-T12`.

#### 2026-07-15 continuous-beam release acceptance update

- Added bounded release modes for Plasma Beam policy `7` and dry-world
  Thunderbolt policy `8`. After three normal ticks reach exact 24 damage, a
  valid real no-attack command must arrive; target damage must remain exactly
  24 throughout a 250 ms authoritative grace interval.
- The release is delivered only through the admitted headless client. A paired
  zero-net-movement edge requests immediate delivery of the final no-attack
  user command; no server-side input or direct weapon callback exists in the
  fixture. Missing release and post-release damage fail closed.
- Three staged headless repeats per weapon prove the canonical initial query,
  exact 24 damage, normal release, and stable post-release damage. This closes
  a bounded release seam only: long holds, water/retrace, Thunderbolt discharge,
  movers, broader interaction policies, and hardening matrices remain open.

#### 2026-07-15 continuous-beam water-retrace acceptance update

- Added real-command water-crossing modes for Plasma Beam policy `7` and
  dry-shooter Thunderbolt policy `8`. The packaged fixture contains a real
  `func_water`; it places the historical target beyond that volume and displaces
  only the live target before the received attack command.
- Each mode requires exact four current-authority damage: the normal
  eight-damage tick must be halved by water, then reach the historical target
  through the production water-excluding retrace. The terminal proof also
  requires positive-age canonical selection, no fallback, unchanged live
  geometry, and the water-retrace status flag. Thunderbolt retains normal
  main/side-ray target de-duplication.
- Three staged headless repeats per weapon pass at positive 56 ms age under
  runtime schema v4. This closes the bounded water-crossing seam only; the
  separate current-authority underwater-discharge seam is documented next.
  Indefinite holds, movers, broader interaction policies, and hardening
  matrices remain open.

#### 2026-07-15 Thunderbolt underwater-discharge acceptance update

- Added a bounded current-authority underwater-discharge mode for Thunderbolt
  policy `8`. The generated deterministic collision BSP now includes a bounded
  world-water leaf for the production `gi.pointContents(start)` check, while
  retaining the inline `func_water` used by water-retrace coverage.
- The real admitted command supplies eight Cells. Production
  `fire_thunderbolt` drains them, excludes the shooter from its radius pass,
  and routes its 140 direct self value through the shared self-damage reduction
  for exactly 70 lost health. An explicit no-op-outside-the-fixture observer is
  invoked after that production authority damage; the acceptance gate does not
  substitute a historical trace for a branch that intentionally returns before
  beam tracing.
- Three staged headless, input-disabled repeats pass under runtime schema v6:
  canonical command scope and attack are present, all Cells drain, the
  production discharge observer fires, and exact 70 self damage is measured.
  This closes the bounded underwater self-discharge seam only; general radius
  fairness, underwater multi-target effects, indefinite-hold/mover lifecycle, and
  the broader interaction matrix remain open.

#### 2026-07-15 sustained continuous-beam acceptance update

- Added bounded real-command sustained modes for Plasma Beam policy `7` and
  dry-world Thunderbolt policy `8`: one ordinary held `+attack` must produce
  exactly 32 normal ticks and 256 current-authority damage without a release
  edge. The v7 terminal contract rejects a received no-attack command during
  the hold.
- Hidden, input-disabled clients use the independent physics cadence
  (`cl_async=1`), so fresh canonical commands continue without visible
  rendering, device input, or mouse capture. A six-second fixture aim lock
  covers the five-second deadline, and fixture target pinning clears only
  knockback displacement while preserving health and combat outcomes.
- Three staged repeats per weapon pass at positive 56 ms age with historical
  target selection, unchanged current query geometry, exact 256 damage, and
  `sustained_hold_interrupted=0`. This closes the bounded 32-tick seam only;
  indefinite, mover, multi-target, underwater-radius, abuse/load, and release
  promotion matrices remain open. Evidence:
  `docs-dev/fr-10-t12-sustained-continuous-beam-acceptance-2026-07-15.md`.

### `FR-10-T12` Extended gameplay lag compensation

- Area: projectile, melee, radius damage, movers, deployables, triggers and coop
  interactions.
- Priority: P1.
- Dependencies: `FR-10-T10`, `FR-10-T11`.
- Current state: In Progress at bounded continuous-beam lifecycle,
  current-authority underwater-discharge, narrow projectile convergence, and
  bounded current-world spawn-forward policies for Disruptor, Rocket, and
  Plasma Gun plus the shared Blaster/HyperBlaster bolt path, including first
  current-world Rocket and Plasma Gun radius-execution seams and bounded Chainfist hybrid
  melee eligibility plus ETF flechette, Phalanx spawn-forward, and Grenade
 Launcher ballistic spawn-forward/current-world first splash, plus
  Hand Grenade release-ballistic current-world forward, Proximity Launcher
  ballistic deploy-forward, BFG current-world launch-forward, Ion Ripper
  fifteen-bolt current-world burst-forward, and Tesla Mine release-bound
  ballistic deploy-forward, plus Trap release-bound ballistic deploy-forward
  and Grapple fresh-hook current-world spawn-forward, plus ProBall
  Chainfist-held release current-world ballistic spawn-forward, plus native
  off-hand Hook fresh current-world spawn-forward, one bounded mover-relative
  Rocket interaction, and independent Rocket splash visibility across a clear
  player path, exact real BSP blocker, and exact real water boundary, plus
  exact Rocket owner/single-touch retirement and target-free scheduled-expiry
  lifecycle cells.
- Progress: plasma/heat-beam and thunderbolt main, water-retrace, and side-ray
  queries use the historical scene, and the complete thunderbolt footprint is
  resolved before damage. Three real-command, water-crossing repeats per beam
  prove the production water-excluding retrace and exact halved four-damage
  historical-target result. Three real-command Thunderbolt underwater-discharge
  repeats separately prove current-authority eight-Cell drain, explicit
  production-branch observation, and exact 70 self damage; that branch does
  not claim historical beam tracing. Three further real-command, continuously
  held repeats per beam prove 32 exact normal ticks / 256 damage without an
  input release. Disruptor target acquisition uses historical point/expanded
  convergence, then may advance its new projectile by a server-authenticated,
  100 ms-capped mapped-command age through a present-world hull trace. The
  normal touch callback, homing/lifetime state, and damage daemon remain
  current-authority; a present-world blocker wins and no projectile collision
  is historically rewound. Three staged real-command repeats prove exact
  45-damage normal lifecycle plus positive 56 ms authenticated clear advance.
  Rocket Launcher now uses the same authenticated 100 ms cap with a distinct
  policy tag, but deliberately never performs a historical contact or splash
  query: a current-world hull sweep advances spawn only, then the unchanged
  touch/RadiusDamage path owns impact and damage. Three staged real-command
  repeats prove the displaced current target receives exact 100 damage after
  an authenticated/advanced 56 ms clear sweep, while every report explicitly
  records no historical impact. A separate three-repeat Rocket splash scenario
  creates a non-damageable current-world BBOX blocker only after history
  capture and moves the player off the firing ray. The normal `rocket_touch`
  then hits that blocker and normal `RadiusDamage` deals exact reduced
  58-damage splash through the clear side; the fixture requires the same
  spawned projectile identity, records no historical hit, and retains the
  same clear authenticated 56 ms spawn advance. Plasma Gun now has distinct
  policy `10`: after ordinary current-world muzzle clearance it may consume the
  same authenticated 100 ms cap through a current-world projectile hull only.
  Three headless real-command repeats move the live target 32 units farther
  along the aim ray, reject historical contact, require a clear authenticated
  56 ms advance, and receive exact 20 current-authority direct damage through
  normal production flight. A separate three-repeat Plasma Gun radius mode
  now creates and identifies a present-world blocker after history capture,
  requires the exact normal Plasma projectile touch, and stages a small
  clear-side current-world damage target before the unchanged production
  `RadiusDamage` call. Each run records no historical impact, authenticated
  unblocked 40–56 ms forward progress, and exact seven-damage falloff. This
  narrow seam does not claim player-hull splash, map/BSP occlusion, movers,
  water, multi-target, or fairness behavior. Evidence:
  `docs-dev/fr-10-t12-plasma-gun-current-world-splash-acceptance-2026-07-16.md`.
  Three additional current-world Rocket modes now independently observe the
  exact production `RadiusDamage`/`CanDamage` decision for a clear player
  (`true`, 58 damage), a real linked `func_rotating` BSP blocker (`false`, zero
  damage), and a real linked `func_water` boundary (`true`, 58 damage). Each
  mode passes three fresh-process headless repetitions with no historical hit
  and authenticated 56 ms current-world advance. The observer is passive and
  both real brush entities restore their exact origin/link state.
  Two schema-v36 Rocket lifecycle modes now pass three final-binary repeats
  each. The direct mode retains the exact owner, observes one current-world
  target touch and exact 100 damage, proves the exact projectile generation
  retired through production `FreeEntity`, then holds target health and touch
  count stable for 250 ms. The target-free mode observes zero touch/damage and
  proves production scheduled expiry: all rows retain a 10,000 ms base
  schedule and 9,952 ms elapsed plus the authenticated 56 ms spawn advance.
  These rows close only the bounded Rocket cells; lifecycle coverage across
  the remaining projectile/deployable families stays open.
  The shared `fire_blaster` path now has
  policy `11` for standard Blaster and HyperBlaster bolt spawn: muzzle
  clearance then bounded authenticated current-world advance only, while
  flight/contact/direct/radius authority remains production-owned. Three
  headless standard-Blaster direct-hit repeats prove no historical impact,
  clear authenticated 56 ms advance, and exact 15 current-authority damage.
  Three headless HyperBlaster held-cadence repeats now also prove its first
  ordinary 6–11 gun-frame bolt under policy `11`: no historical impact, clear
  authenticated/advanced 56 ms current-world progress, unchanged staged
  geometry, and exact 15 normal direct damage. The observation hook only
  restores the staged fixture pose before the normal shared bolt callback;
  cadence and combat authority remain production-owned. Sustained cadence and
  optional ruleset radius behavior are not claimed. Chainfist now has policy
  `12`: the accepted canonical
  command may establish one historical player reach/FOV candidate only, then
  a 64-unit live displacement guard and ordinary live `CanDamage`/`Damage`
  retain collision and damage authority. Three headless real-command repeats
  prove positive 56 ms authenticated age, historical eligibility, exact
  64-unit accepted live displacement, unchanged current geometry, and exact
  15 normal damage. This bounded single-player seam does not claim moving or
  multi-target melee, other melee weapons, mover contacts, or general
  collision/radius/trigger/co-op policy. ETF flechettes now have policy `13`:
  normal muzzle clearance first, then a 100 ms-capped authenticated
  current-world hull advance only; normal contact and exact direct damage stay
  production-owned. Three headless real-command repeats prove no historical
  impact, clear 40–56 ms advances, and exact 10 direct damage. ETF projectile
  variants remain separately open. Phalanx now has policy `14`: only an
  accepted, 100 ms-capped command age may advance the newly-spawned shell
  through a current-world hull sweep. Its Generic callback may follow the
  command scope, so the server retains a short-lived accepted authorization
  bound to the same shooter life, map epoch, weapon, policy, expiry, and
  bounded launch count; it conveys no historical collision or damage result.
  Normal `phalanx_touch`, direct damage, `RadiusDamage` splash, effects, and
  lifetime remain current-world authority, including when the forward sweep
  blocks. Three headless real-command repeats retain six target-history
  captures, claim no historical impact, prove a clear authenticated/advanced
  56–72 ms current-world forward sweep, and apply exact 80 normal direct
  damage. A shared current-world splash fixture now creates a non-damageable
  blocker only after history capture, moves the target off the shell ray, and
  verifies the accepted normal shell identity strikes that blocker before
  normal `phalanx_touch`/`RadiusDamage` applies splash. Three headless repeats
  prove no historical impact, clear authenticated/advanced 56–72 ms forward
  progress, and the exact two-normal-barrel 93-damage splash total (48 then
  45). Grenade Launcher policy 15 now consumes only accepted, 100 ms-capped
  command age through stepped gravity-before-move current-world hull sweeps. A
  clear result repositions the new grenade and forward velocity while
  shortening its ordinary fuse by the same age; any current-world contact fails
  closed to an unadvanced ordinary Toss spawn, without inventing a historical
  contact or bounce. A post-history present-world damageable BBOX blocker and
  laterally displaced target require the accepted normal grenade to use its
  unchanged touch/explosion/RadiusDamage path. Three headless repeats prove no
  historical projectile impact, a clear authenticated 40–56 ms advance, and
  normal current-world splash within the deliberately closed 57–60 window (all
  observed 58). Hand Grenade policy 16 accepts only the authentic
  attack-to-release edge: its short-lived release-only authorization is
  bound to the original command and may advance the normal non-held throw
  through a clear current-world gravity-stepped hull path only. Three
  headless, input-disabled repeats prove no historical impact or fallback,
 unchanged current geometry, and authenticated/advanced 40–56 ms forward
 progress. A separate arc-aware release/splash mode derives its present-world
 blocker and lateral target only after the accepted real grenade exists, then
 requires normal Toss contact and RadiusDamage. Three headless repeats prove
 no historical impact, 56 ms clear forward progress, and exact 45
 current-world splash damage. Bounce, fuse, occlusion, moving/multi-target,
 water, mover, chained, cooperative, load, and release coverage remain open.
 Proximity Launcher policy 17
  consumes only accepted, 100 ms-capped command age through a clear
  gravity-before-move current-world hull path. A contact fails closed to the
  ordinary Bounce mine; clear forward updates only its origin, velocity, and
  normal lifetime. Three headless, input-disabled repeats prove no historical
  impact/fallback, unchanged geometry, and clear authenticated/advanced
  40–56 ms initial progress. A separate lifecycle mode stages a
  non-damageable current-world Push landing surface only after that accepted
  clear advance, then stages the isolated live target only after normal
  `prox_land` has created its real trigger. Three further dedicated/headless
  repeats prove 56 ms initial forward progress, no historical impact, normal
  landing, arm-delay target selection, delayed explosion, and exact 61
  current-world `RadiusDamage`. The fixture supplies no collision plane,
  attach result, target selection, trigger, explosion, or damage. Arbitrary
  splash/occlusion, moving/multi-target, mover, water, destruction,
  chained-mine, cooperative, fairness, load, and release coverage remain open
  alongside broader projectile interaction work. BFG policy 18 now authorizes
  only its fresh projectile's bounded current-world launch. The normal
  frame-9-to-17 BFG wind-up receives a shooter/life/weapon/policy/map-epoch/
  command-bound single-launch authorization for at most 1.25 seconds, while
  the projectile itself may still consume only the shared 100 ms forward cap.
  Three headless repeats record 664, 744, and 680 ms authenticated age,
  unblocked clamped 100 ms advances, no historical impact, and unchanged
  current geometry. The fixture deliberately accepts no BFG laser, contact,
  explosion, radius, or damage result; all later lifecycle authority remains
  current-world production code. Ion Ripper policy 19 now permits only the
  normal callback's fifteen fresh randomized bolts to consume the accepted
  current-world spawn-forward age. The fixture retains a per-bolt count and
  cannot pass unless all 15/15 bolts authenticate, advance, and remain
  unblocked. Three headless repeats record no historical impact, unchanged
  current geometry, and exactly 15/15 clear 56 ms advances. Spread, contact,
  damage, effects, lifetime, moving/multiple-target, mover, cooperative,
  fairness, abuse, and load behavior remain production-owned and unproven.
  Tesla Mine policy 20 now permits only a fresh mine's initial clear
  current-world gravity advance after a real held-throw release command. The
  one-launch authorization is tied to the live shooter, weapon, policy, map
  epoch, accepted release command, and expiry; a new prime invalidates it.
  Three input-disabled headless repeats record no historical impact, unchanged
  current geometry, no block, and 56, 56, and 40 ms advances. Landing,
  activation, target scans, chains, damage, effects, destruction, expiration,
  moving/multiple-target, mover, cooperative, fairness, abuse, and load
  behavior remain production-owned and unproven.
  Trap policy 21 now permits only a fresh Trap's initial clear current-world
  gravity advance after a real held-throw release command. The one-launch
  authorization is tied to the live shooter, weapon, policy, map epoch,
  accepted release command, and expiry; a new prime invalidates it. Three
  input-disabled headless repeats record no historical impact, unchanged
  current geometry, no block, and 56 ms advances. Landing, capture/release,
  destruction, expiration, moving/multiple-target, mover, cooperative,
  fairness, abuse, and load behavior remain production-owned and unproven.
  Grapple policy 22 now permits only a fresh normal hook's clear
  current-world advance after its normal frame-six callback. One accepted
  shooter/weapon/policy/map-epoch/command-bound authorization may survive the
  normal wind-up for at most 750 ms, while the hook movement itself remains
  capped at 100 ms. Three input-disabled headless repeats record no historical
  impact, unchanged current geometry, no block, and 40, 56, and 56 ms
  advances. Attachment, pull, cable, damage, reset/detach, movers,
  moving/multiple targets, cooperative, fairness, abuse, and load behavior
  remain production-owned and unproven; the legacy off-hand `hook` string
  command is unchanged.
  ProBall policy 23 now permits only a fresh ball's clear current-world
  gravity advance after the ordinary Chainfist-held attack-to-release edge.
  The one-launch authorization is bound to the live ProBall carrier,
  Chainfist-plus-ball state, policy, map epoch, and accepted release command;
  a later prime invalidates it, and direct item-use passes are unchanged.
  Three input-disabled headless ProBall repeats record no historical impact,
  unchanged geometry, no block, and 72, 88, and 56 ms advances, with explicit
  dedicated-server and client termination. Possession, pickup, touch,
  re-grab, bounce, goals, scoring, teams, resets, and lifecycle behavior
  remain production-owned and unproven.
  Native off-hand Hook policy 24 now maps a distinct `+hook` user-command
  edge, rather than deriving a decision from the legacy `hook` string command.
  The edge is accepted only for a live playing client with both off-hand Hook
  rules enabled, and only when no hook is already active. It can advance the
  fresh hook through one 100 ms-capped current-world hull sweep. The normal
  production hook callback still owns contact, attachment, pull, damage,
  reset, detach, and lifecycle authority; neither historical collision nor
  historical damage is produced. A debug gate and three headless,
  input-disabled dedicated-server/two-client repeats record policy 24,
  a clear authenticated/advanced 56 ms sweep, no historical hit or damage,
  and explicit termination of all launched WORR processes. Legacy `hook`
  string-command behavior remains current-world and uncompensated.
- Evidence:
  `docs-dev/fr-10-t12-disruptor-projectile-spawn-forward-acceptance-2026-07-16.md`
  and
  `docs-dev/fr-10-t12-rocket-projectile-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-rocket-current-world-splash-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-plasma-gun-projectile-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-blaster-bolt-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-hyperblaster-cadence-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-chainfist-hybrid-melee-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-etf-flechette-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-phalanx-current-world-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-phalanx-current-world-splash-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-grenade-launcher-ballistic-current-world-splash-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-hand-grenade-release-ballistic-current-world-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-hand-grenade-release-current-world-splash-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-prox-launcher-ballistic-deploy-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-prox-launcher-lifecycle-current-world-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-bfg-current-world-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-ion-ripper-burst-current-world-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-tesla-mine-release-ballistic-deploy-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-trap-release-ballistic-deploy-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-grapple-hook-current-world-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-proball-held-throw-current-world-ballistic-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-offhand-hook-authenticated-current-world-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-mover-relative-projectile-current-world-acceptance-2026-07-20.md`,
  and
  `docs-dev/fr-10-t12-splash-occlusion-player-bsp-water-acceptance-2026-07-20.md`,
  and
  `docs-dev/fr-10-t12-rocket-projectile-lifecycle-current-world-acceptance-2026-07-20.md`.
- Definition of Done:
  - Each mechanic has an explicit policy: rewind, forward estimate, hybrid, or
    deliberately uncompensated.
  - Projectile ownership, lifetime, collision, splash occlusion, mover-relative
    poses and multi-target fairness are covered independently.
  - No mechanic reuses hitscan rewind blindly where its time semantics differ.
  BFG's later laser/explosion/radius lifecycle, Ion Ripper's later
  contact/damage/effect/lifetime behavior, Tesla's and Trap's later deployable
  lifecycles, Grapple's later attachment/pull/reset lifecycle, the off-hand
  Hook, and ball families are explicitly deliberately uncompensated/current-
  world pending their own policies; see
  `docs-dev/fr-10-t12-uncompensated-interaction-policy-register-2026-07-16.md`.

#### 2026-07-20 completion audit and aggregate-gate status

- The fixed aggregate manifest contains 39 production modes, spans weapon
  policies `6` through `24`, and requires three semantically identical
  headless repetitions per mode. Its 12 parent tests, 54 canonical-runner
  tests, and 51 lag-compensation source-contract tests pass.
- This is explicitly partial evidence, not task acceptance. The manifest now
  includes one bounded Rocket mover-relative current-world direct-impact mode:
  a real moving rider/rotating mover produces paired history, retains its
  translated relative pose, receives the exact production current-world touch
  and 100 damage, and leaves the live collision fingerprint unchanged. The new
  mode passes three fresh-process repetitions. Three additional Rocket splash
  modes now pass three repetitions each and independently bind production
  clear-player, BSP-blocked, and water-boundary visibility/damage outcomes.
  Two further Rocket modes pass three repetitions each for exact owner/
  single-touch retirement with a 250 ms no-double-damage hold and target-free
  production lifetime expiry.
  The manifest still has no coop/monster mode or broader moving/multi-target
  fairness matrix.
  Initial projectile/deployable launch seams also do not cover their complete
  ownership, lifetime, collision, trigger, or destruction lifecycles.
- `FR-10-T12` therefore remains unchecked. The parent publishes six open
  classes; a live 117-repetition execution of
  the current runner would prove only the bounded subset and cannot change
  that result. Audit and focused evidence:
  `docs-dev/fr-10-t12-completion-audit-and-partial-gate-evidence-2026-07-20.md`
  and
  `docs-dev/fr-10-t12-mover-relative-projectile-current-world-acceptance-2026-07-20.md`,
  and
  `docs-dev/fr-10-t12-splash-occlusion-player-bsp-water-acceptance-2026-07-20.md`,
  and
  `docs-dev/fr-10-t12-rocket-projectile-lifecycle-current-world-acceptance-2026-07-20.md`.

### `FR-10-T13` Demo, MVD, spectator, and replay compatibility

- Area: client demos, server streaming/MVD, spectator timelines.
- Priority: P1.
- Dependencies: `FR-10-T04`, `FR-10-T05`, `FR-10-T06`, `FR-10-T07`,
  `FR-10-T09`.
- Current state: In Progress at client-demo capability/cursor preservation,
  canonical seek lineage, a versioned native WDM1/WDR1 stream-index/seek core,
  an allocation-free native snapshot playback cursor/decoder, and an opt-in
  bounded production client snapshot recorder.
- Progress: new client recordings rebuild the confirmed capability tuple, emit
  the consumed-cursor sideband atomically with synthetic frame/entity data, and
  replay it through the same strict packet parser. Client-generated in-memory
  seek snapshots also emit a private six-setting, checksum/commit-validated
  exact clock tuple for every stored backup frame. The tuple is accepted only
  for an explicitly armed synthetic packet, must match the following frame,
  and precedes the cursor tuple so cursor adjacency is unchanged. Selecting any
  stored snapshot replaces clock/projection lineage, including during a
  forward seek whose backup window begins behind the current frame; sequential
  forward skipping retains continuity. Focused codec corruption/order,
  frame-match, C/C++ layout, arming, legacy-fallback, and serverdata-lifecycle
  checks pass. Legacy demos and protocols without the private tuple remain on
  the stateful fallback. A filesystem-agnostic WDM1 container records exact
  WNC1 command/snapshot/event images in checksummed WDR1 records. Its bounded
  scanner validates complete bodies and cross-record canonical order before
  committing a caller-owned index, and its seek selector revalidates that
  index before selecting the latest exact snapshot at or before time and/or
  ordinal bounds. An opt-in `native_record` path now writes only independently
  parity-qualified immutable snapshots through a fixed-buffer transactional
  recorder. It publishes a separate `.wdm` only after flush, close, and a full
  bounded scanner validation of its exclusive `.wdm.tmp`; short writes and
  semantic/order failures quarantine the temporary artifact. Canonical ID/hash
  deduplication covers delayed promotion even when a process-local projection
  ref changes, while conflicts, regressions, epoch changes, and controlled-view
  changes fail closed. The 64 MiB hard cap keeps publication validation below
  `MAX_LOADFILE`. A pointer-free playback cursor now rebinds and revalidates the
  complete caller-owned stream/index on every operation, decodes a selected
  WNC1 snapshot transactionally into fixed caller buffers, and exposes explicit
  reset generations for initial position and seek. Forward stepping starts at
  the entry immediately following the emitted snapshot, skips command/event
  records, and never duplicates a snapshot. Header-only streams remain valid;
  stream/index mutation, ordinal gaps, epoch/time mismatch, overflow, capacity,
  and alias faults fail unchanged. DM2, MVD, GTV, and q2proto are unchanged.
  Standalone bootstrap data and file-backed/client playback/seek, canonical
  timeline publication/reset, command/event recording, MVD/GTV,
  spectator-view switching,
  event-present-once playback, and full native/legacy record/play/seek/relay
  matrices remain open.
- Evidence:
  `docs-dev/networking-live-client-snapshot-prediction-and-demo-clock-2026-07-12.md`
  and
  `docs-dev/fr-10-t13-native-demo-stream-index-seek-core-2026-07-20.md`
  and
  `docs-dev/fr-10-t13-native-demo-snapshot-playback-cursor-2026-07-20.md`
  and
  `docs-dev/fr-10-t13-client-native-snapshot-recording-2026-07-20.md`.
- Definition of Done:
  - Legacy demos still parse and play with the existing adapter.
  - New recordings identify their schema/transport and reproduce canonical
    snapshot/event order deterministically.
  - Seek, pause, timescale, packet loss in recorded streams, spectator switches
    and sequence reset do not duplicate events or interpolate discontinuities.

### `FR-10-T14` Networking telemetry, performance, load, and security gates

- Area: client/server/cgame diagnostics, tooling and CI evidence.
- Priority: P1.
- Dependencies: `FR-10-T03` through `FR-10-T13`, plus `FR-10-T16`.
- Current state: In Progress at rewind observability, bounded client-policy
  telemetry, and versioned integration/acceptance-evidence foundations; full
  telemetry/load/security gates remain open.
- Automated client evidence now uses a hidden Windows surface with physical
  input disabled (`win_headless=1`, `in_enable=0`, `in_grab=0`), no stdin, and
  no-window process creation; dedicated-only gates retain the dedicated binary.
  Every networking runtime launcher now uses the shared input-free process
  policy: Windows launches enter a private kill-on-close job, use hidden
  `taskkill /T` while their root is live, wait, and close the job before
  reporting success. This preserves automated runtime coverage without desktop
  focus, mouse interference, or a lingering staged process even when a launcher
  exits before its descendant. See
  `docs-dev/fr-10-t14-headless-isolated-process-tree-enforcement-2026-07-16.md`
  and `docs-dev/headless-input-free-automated-launch-policy-2026-07-15.md`.
- Progress: the first `worr.networking.acceptance-evidence.v1` producer hashes
  its source/matrix/binaries and records platform/build/workload, deterministic
  outcomes, timings, thresholds, privacy declarations, child artifacts, and
  explicit limitations. The rewind observation journal publishes saturating
  counters and opt-in detail without steady-state allocation. Snapshot recovery
  and adaptive input expose pointer-free status records with bounded,
  saturating counters rather than requiring console-text scraping. The staged
  schema-v3 runtime report now directly gates snapshot projector/publication/
  parity and cgame-consumer counters for both clean and impaired live loopback
  profiles. Focused native-envelope/session/codec/carrier, recovery-policy,
  adaptive-policy, canonical-range, local-action-audit, collision-provider,
  and ABI tests exercise transactional rejection and deterministic replay. The
  carrier adds byte-exact golden framing, strict 1,200-byte packet boundaries,
  nested-envelope/epoch validation, corruption/truncation/alias rejection, and
  an explicit carrier-only CRC-domain proof. The preceding one-shot production-
  pilot milestone passed 14/14 focused checks for exact command-range selection,
  native/legacy arrival order, committed-duplicate ACK rearm, current/retired
  epoch fairness, reliable-queue point-of-no-return handling, admission-clock
  ordering, its then-current one-shot drain contract, async-wake eligibility,
  and 818/819 plus 976/977 production application boundaries. Repeated command
  tests add exact
  match retirement, same/older canonical replay rejection, old-bank drain
  ordering, a 256-command bounded client loop, and transactional multi-range
  ACK application. Mixed-core tests cover one DATA plus up to seven ACK ranges,
  DATA-only fallback, exact packet/token binding, transactional confirm/reject/
  abort, and C/C++ layout. The staged repeated gate adds three self-validating,
  hash-bound V1 reports: 152, 150, and 151 commands (453 total) match,
  acknowledge, and release with exact client/server counters and zero terminal
  retention, mismatch, rejection, drain, or failure under 25 ms latency. The
  post-change one-command regression still proves exact-once 12,800-byte low-
  rate reliable delivery with 206 rate and 25 fragment deferrals, plus exact-
  once 6,400-byte high-rate delivery with three deterministic async wakes and
  ACK handoffs. The cgame runtime/export/owner tests additionally cover
  transactional authoritative admission, receipt gaps, prediction correction,
  audit-off authority lifecycle, audit-off legacy-inferred snapshot fencing
  beyond the fixed diagnostic capacity, ABI layout, reload quarantine,
  snapshot-fence resync, fresh-status receipt suppression, and
  connection-lifetime epoch monotonicity.
  Descriptor/codec/owner/admission tests add exact class-4 wire bytes,
  capability isolation, private RX/ACK precommit, baseline and epoch fences,
  fresh receipt proof, callback-failure rollback, generation exhaustion, and
  map-quiesce versus reconnect behavior. Semantic repeat tests additionally
  prove public commit/repeat gating, exact-only receipt isolation, exact
  descriptor/event revalidation, multi-fragment repeats, new-fragment arena
  rollback, active-emission serialization, and fail-closed semantic receipt
  retirement. The live per-peer shadow adds focused readiness-confirmation,
  receive-only binding, exact final-emission candidate/hash, descriptor-before-
  ID sender, duplicate/partial/multi-range ACK, sequence-exhaustion, mixed full-
  duplex, and idle-scheduler-liveness coverage. Exact mixed
  packet boundaries are 760/761 legacy-prefix bytes for a descriptor and
  696/697 for a legacy entity event. Command-only peers avoid the event heap;
  opted-in server peers use a bounded current sender and snapshot
  candidate scratch, and all queue/admission failures are isolated from the
  already committed legacy snapshot. The production-wrapper virtual-link gate
  now has deterministic digest `be9724b38fb5f682` and explicitly records
  directional loss, retry, reorder, duplication, corruption, one-way ACK loss,
  canceled ACK/DATA stripping, corrupt-old rejection, convergence, and
  presentation count. Its lifecycle follow-up records exactly three exhausted
  ACK handoffs followed by one canceled client receipt, two canceled server
  event records, zero retired receipt/retention, a second rotation without a
  retired bank, both valid old directions stripped, and two corrupt-old
  rejections. The negotiated bit-6 contract uses exact public/readiness
  `0x53`/`0x73`/`0x57` plus combined `0x77` when the corresponding default-off
  endpoint is armed. An unarmed endpoint, mismatched bundle, partial
  intersection, or missing readiness falls back to exact legacy `0x03`;
  client and server
  status expose the monotonic floor,
  saturating disposition counters, and separate stale-canceled carrier/
  readiness counts. Delayed valid old controls are consumed without mutation
  only after full validation; malformed or downgraded controls reject. The
  production parser now delegates to a shared decision helper that classifies a
  complete record before testing fixed `SERVER_ACTIVE` capacity. Its regression
  drives a physically full reliable buffer through both response-free stale and
  response-required current COMMIT paths. Server status separates current from
  wire-committed transport epoch across a
  replacement challenge. The same link now queues three event IDs and proves
  that out-of-order event 3 is selectively acknowledged and retired while
  lost event 2 remains retained through its retry. The
  floor includes pending readiness
  epochs that never activated. It remains an explicitly scheduled in-process
  Windows gate, with a focused three-event selective-receipt assertion rather
  than the production impairment-model golden, live multi-process/
  load, malformed-corpus, soak, or cross-platform evidence required for
  completion. The final cancellation-focused gate passes 8/8; the registered
  production snapshot-hook link passes with `prediction_ready=4` and digest
  `7176afa3d4eb62b2`. The registered serialized corpus row passes inside the
  final 149/149 headless suite in 209.08 seconds, with two normalized
  100,000-frame results matching golden `ba519ae7bdd1db74`. Its fail-closed
  runner requires the real golden, removes stale evidence before each attempt,
  publishes atomically, records executable/manifest/runner hashes and explicit
  no-mouse policy, and has an 11/11 focused contract. The corpus stops at a
  locally fabricated resolver/selector, while a focused production continuation
  now runs four shipped-engine V2/cgame PMove calls with pending/finalized,
  127/128, correction, and event-fence receipt coverage. The corpus uses one
  impairment profile, dynamic per-transmission vectors, within-burst fragment
  scheduling with synthetic receive sequencing, and a fixed eight-entity/
  four-area-byte snapshot. It proves neither allocation-free operation nor real
  sockets, cross-snapshot netchan reorder, multi-client load, soak, or platform
  breadth. The three 125-test repetitions remain historical pre-Stage-D
  evidence. The final production build, package tests 16/16, release headless
  contract 1/1, and Windows x86-64 stage refresh/validation pass. The stage has
  16 root files, one dependency, a 507-file pak, one q2aas reference map,
  31 botfile payloads, and 215 RmlUi assets. A hardened
  target-count live runner now requires exact pipeline totals, attached cgame
  consumer flags, exercised impairment, a nonce completion marker, unchanged
  component hashes, and retained log hashes. Its impaired retained run passed
  115,914 exact consumer accepts against a 100,000 target with zero pipeline,
  parity, queue, or throttle failure. Bounded over-retention command-gap
  recovery is unit/core-complete, survived scoped 50,001-frame live stress,
  and now has a staged dedicated-server report that proves exact 161/401
  production-policy fast-forward outcomes. This is still focused
  common-core and single-platform integration evidence, not a substitute for
  the mandatory 100,000-case malformed-input, live 1/8/16/32-client, stress,
  soak, or cross-platform gates. The schema-v32 local-action continuation adds
  three machine-readable two-process reconnect rows with exact epoch-3 joined
  shadows, 45/45 aggregate receipt matches, zero mismatch/conflict/resync, and
  normal 15 damage. It closes only the narrow reconnect lifecycle row. Schema
  v33 adds combined `0x77` and target-only native-presentation modes. Three
  combined repeats match 99/99 exact local-action receipts while both snapshot
  peers ACK and release retained frames with zero client/server failure. Three
  presentation repeats prove positive native snapshot traffic and exact
  mode-3 remote-transform promotion with zero clock, pair, alignment, parity,
  event-fence, endpoint, or transform-error failures. These are focused live
  gates, not the mandatory load/soak/malformed/platform matrix.
- Evidence:
  `docs-dev/networking-rewind-observability-acceptance-evidence-2026-07-13.md`,
  `docs-dev/networking-snapshot-keyframe-recovery-policy-2026-07-13.md`,
  `docs-dev/networking-adaptive-input-pacing-and-redundancy-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-transport-session-retention-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-canonical-byte-codecs-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-packet-carrier-trailer-core-2026-07-13.md`,
  `docs-dev/fr-10-t04-transport-confirmed-session-carrier-and-receipts-2026-07-13.md`,
  `docs-dev/fr-10-t04-post-assembly-netchan-hook-2026-07-13.md`,
  `docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`,
  `docs-dev/networking-live-snapshot-parity-runtime-gate-2026-07-13.md`,
  `docs-dev/fr-10-t06-live-100k-snapshot-acceptance-gate-2026-07-13.md`,
  `docs-dev/networking-local-action-prediction-authority-audit-ring-2026-07-13.md`,
  `docs-dev/fr-10-t09-bounded-command-gap-fast-forward-2026-07-13.md`,
  `docs-dev/fr-10-t09-t16-headless-command-gap-acceptance-gate-2026-07-15.md`,
  `docs-dev/fr-10-t10-immutable-brush-collision-extension-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-two-process-async-ack-impairment-gate-2026-07-14.md`,
  `docs-dev/fr-10-t04-repeated-command-shadow-and-mixed-carrier-core-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-t05-canonical-event-stream-admission-core-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-semantic-repeat-ack-fence-2026-07-14.md` and
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-directional-event-shadow-virtual-link-2026-07-14.md`
  and
  `docs-dev/fr-10-t04-t05-map-quiesce-ack-service-and-epoch-cancel-decision-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-negotiated-epoch-cancellation-barrier-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-parser-shared-stale-readiness-capacity-gate-2026-07-15.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`,
  and
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-production-v2-pmove-replay-boundary-2026-07-17.md`,
  plus
  `docs-dev/fr-10-t04-t05-t08-t09-t14-live-reconnect-local-action-authority-2026-07-18.md`,
  plus `docs-dev/fr-10-t04-t06-t14-combined-native-event-snapshot-shadow-2026-07-18.md`
  and `docs-dev/fr-10-t06-t07-t14-native-snapshot-presentation-authority-2026-07-18.md`.
- Definition of Done:
  - The telemetry catalog below is implemented with low-overhead counters and
    focused opt-in detail.
  - Representative player/entity/rate/load matrices meet declared CPU, memory,
    bandwidth and correction budgets.
  - Decoder fuzzing, malformed-range tests, timestamp abuse, amplification,
    command flood and rewind-cheat scenarios pass.
  - Every ratified `FR-10-T14` acceptance floor below passes without waiver.
  - Reports are machine-readable and written under `.tmp/networking/`.

### `FR-10-T15` Progressive rollout, documentation, and release acceptance

- Area: capability defaults, server policy, `.install`, CI/release and docs.
- Priority: P1.
- Dependencies: `FR-10-T04` through `FR-10-T14`, plus `FR-10-T16`.
- Current state: In Progress at user/operator controls documentation and current
  local Windows staging only; rollout exercises and release acceptance remain
  open.
- Progress: `docs-user/progressive-networking-controls.md` documents the current
  default-off snapshot timeline, snapshot recovery, adaptive input, and lag-
  compensation controls, compatibility fallbacks, diagnostics, and safe
  compensation controls, plus the default-off client/server native-shadow
  observation controls, compatibility fallbacks, diagnostics, and safe
  evaluation workflow. It is linked from the user README and client, server,
  and server-quickstart guides. The Windows `.install/` refresh and stage
  validation pass with 16 root runtime files, one dependency, a 483-member
  `basew/pak0.pkz`, 31 botfiles, 215 RmlUi assets, one loose q2aas reference
  BSP, and five release notice sidecars. No feature is promoted
  by this documentation or staging;
  rollout, soak, rollback, cross-platform, and release-default gates remain
  open.
- Definition of Done:
  - Rollout gates and rollback triggers below are exercised, not merely
    documented.
  - Legacy and WORR-native compatibility matrices pass on supported platforms.
  - Server operators and users have approachable `docs-user/` guidance for
    public controls, fairness behavior and diagnostics.
  - `.install/` is refreshed and validated, release payloads contain the current
    modules/assets, and the canonical roadmap is updated with truthful status.
  - Every ratified `FR-10-T15` release floor below passes without waiver.

### `FR-10-T16` Adaptive input pacing, batching, and selective redundancy

- Area: client command transmission, server intake queues, and network pacing.
- Priority: P1.
- Dependencies: `FR-10-T03`, `FR-10-T04`, `FR-10-T08`, `FR-10-T09`.
- Current state: In Progress at deterministic controller, hardened default-off
  live batched-client integration, a bounded native batch-selection policy, a
  private default-off two-to-eight-command WNB1 production-hook shadow, a
  repeated stop-and-wait observational native command shadow, and default-off
  live full-duplex mixed command/event carrier use with explicit scheduler
  wakeups; native authority, feedback, and parent acceptance gates remain open.
- Progress: a pointer-free, allocation-free integer V1 controller now consumes
  normalized successful/lost packet counters, RTT variation, queued-command and
  acknowledgement pressure, rate, `cl_maxpackets`, and `cl_packetdup`. The
  adapter separates inferred loss from successfully received packets,
  accumulates low-rate samples, rebases counter/clock resets, and resets across
  client connection lifecycles. The default-off `cl_adaptive_input` path applies
  bounded pacing and selective redundancy only to existing batch moves. It
  preserves `cl_maxpackets 0` as unlimited, retains the configured
  `cl_packetdup` baseline unless transport/rate constraints require less, raises
  protection under measured pressure, and leaves the non-batched fake-drop
  compatibility path untouched. Invalid input fails locally to legacy policy;
  copyable reason/counter telemetry is exposed through
  `cl_adaptive_input_status`. A short staged impaired loopback produces live
  evaluation windows without fallback or queue overflow. The default-off native
  production pilot now repeatedly sends one retained command DATA at a time
  beside the authoritative legacy prefix and waits for an exact ACK/release
  before selecting a newer command. It uses the post-assembly TX and admitted-
  RX seams, exact synchronous handoff outcomes, actual per-direction application
  ceilings, one fresh current epoch, a 100 ms receipt retry,
  rate/fragment-gated asynchronous ACK wake, immediate matched-slot retirement,
  and a canonical replay high-water. Three staged latency runs match and release
  453 commands with zero final retention or failure. The `0x73` event mode now
  uses the mixed core at both production hooks: client command DATA can carry
  descriptor/event ACKs and server descriptor/event DATA can carry command
  ACKs. Event DATA stays current-epoch only, and nonmutating output-due queries
  wake empty client keepalives and
  asynchronous server sends for retry, fragment continuation, and receipts.
  The wrapper-level virtual link now proves bounded recovery for scheduled
  loss in both directions, delayed/reordered and duplicate event traffic, one-
  way event-ACK loss, mixed command/event DATA/ACK packing, and a three-event
  semantic gap whose selective receipt retires only the later event, with
  convergence digest `be9724b38fb5f682`; corruption fails closed. Map-quiesced `DRAIN` now
  services only already-authorized event ACKs while freezing command DATA until
  the next challenge. The negotiated bit-6 barrier then terminally counts every
  lower epoch, and the three-credit/two-rotation diagnostic proves zero retired
  state and no overwrite. A pointer-free native-input delivery core now combines
  exact received/consumed server cursors with the adaptive decision and up to
  64 retained command identities. It plans at most eight ordered selections
  under a 1,200-byte ceiling, reserves both newest fresh input and the receive
  frontier, and adds only due, non-exhausted selective retries. It never commits
  send history or transport authority; feedback rollback, identity gaps,
  malformed policy, and output overlap fail transactionally. A separate private,
  default-off WNB1 production-hook slice now binds that policy to exact
  contiguous groups of two through eight retained WNC1 commands. The client
  sends one immutable batch at a time beside authoritative legacy input, with
  100 ms retry and eight-handoff ceilings. Same-packet private confirmation is
  capacity-atomic and does not alter any public capability mask. The server
  transactionally admits the complete batch, retro-joins older authoritative
  commands from the retained canonical stream before parsing the current
  packet, blocks its receipt until every nested command exactly joins, and
  leaves malformed pre-confirm/declined schema-2 traffic legacy-only. Duplicate
  and delayed cross-batch receipts are idempotent at the client. Map bootstrap
  preserves the drained old WNB1 TX high-water until the fresh-challenge
  cancellation floor commits, so proactive old receipts cannot be misrouted to
  the clean schema-1 bank or reject their legacy prefix. The same reset re-arms
  a cleanly declined prior map, and focused regressions prove both next-map
  confirmation and delayed-old-ACK isolation. This closes the private
  production batch codec/handoff and identified bootstrap race, but does not yet prove
  authenticated live received/consumed cursor feedback, native authority,
  statistical/model-backed impairment, or complete map-rotation liveness.
  Legacy canonical intake now admits server-observed packet-loss gaps
  independently of its 128-slot retention ring, caps identity
  advancement at 4,096 commands, simulates only the declared budget, and fast-
  forwards the already-lost remainder without unbounded work. Deterministic
  functional/layout and repeatability tests pass, and a staged dedicated-server
  gate now proves the production large-loss branch for the retained 161/401
  cases with exact cursors and zero rejection counters. Complete-native-adapter
  impairment, bandwidth, command-age, server-feedback, correction, and
  release-promotion gates remain open.
- Evidence:
  `docs-dev/networking-adaptive-input-pacing-and-redundancy-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-transport-session-retention-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-packet-carrier-trailer-core-2026-07-13.md`,
  `docs-dev/fr-10-t04-post-assembly-netchan-hook-2026-07-13.md`,
  `docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`,
  `docs-dev/networking-live-snapshot-parity-runtime-gate-2026-07-13.md`,
  `docs-dev/fr-10-t09-bounded-command-gap-fast-forward-2026-07-13.md`,
  `docs-dev/fr-10-t09-t16-headless-command-gap-acceptance-gate-2026-07-15.md`, and
  `docs-dev/fr-10-t04-native-two-process-async-ack-impairment-gate-2026-07-14.md`,
  with current repeated-stream and mixed-core evidence in
  `docs-dev/fr-10-t04-repeated-command-shadow-and-mixed-carrier-core-2026-07-14.md`,
  and live full-duplex event-shadow evidence in
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  plus deterministic fault/lifecycle evidence in
  `docs-dev/fr-10-t04-t05-directional-event-shadow-virtual-link-2026-07-14.md`
  the cancellation-decision evidence in
  `docs-dev/fr-10-t04-t05-map-quiesce-ack-service-and-epoch-cancel-decision-2026-07-14.md`,
  and the implemented barrier evidence in
  `docs-dev/fr-10-t04-t05-negotiated-epoch-cancellation-barrier-2026-07-14.md`,
  plus the bounded selection-policy evidence in
  `docs-dev/fr-10-t16-native-input-delivery-policy-core-2026-07-20.md`, and the
  private production-hook batch evidence in
  `docs-dev/fr-10-t16-private-native-input-batch-shadow-2026-07-20.md`, and the
  map-bootstrap receipt/cancellation evidence in
  `docs-dev/fr-10-t16-map-bootstrap-input-batch-ack-cancellation-2026-07-20.md`.
- Definition of Done:
  - Batching and selective redundancy recover bounded loss without duplicate
    simulation or ambiguous acknowledgement.
  - Input sampling, simulation, rendering, and packet-send cadence are
    explicitly decoupled.
  - Pacing reacts within declared bounds to RTT, jitter, loss, queue pressure,
    and configured rate without starving fresh input.
  - Legacy and WORR adapters meet the same input-age, recovery, bandwidth, and
    command-flood gates with machine-readable telemetry.

## Initial Engineering Budgets

These are ratified minimum guardrails as of 2026-07-12. Measured baselines may
tighten them; relaxation requires explicit task-linked evidence and roadmap
approval before any affected behavior becomes default.

| Area | Initial budget or invariant |
|---|---|
| Datagram sizing | Target application datagrams at or below 1200 bytes. Larger logical snapshots use explicit application fragmentation/prioritization; correctness must not depend on IP fragmentation. |
| One-command native pilot | At the production 1,024-byte application ceiling, one client command costs exactly 206 bytes (`WNC1` 110, complete `WNE1` 166, WTC entry/footer included): an 818-byte legacy prefix fits and 819 bypasses. One server ACK range costs 48 bytes: 976 fits and 977 bypasses. Budget failure leaves legacy bytes authoritative and retries native state later. |
| Snapshot rate | Stay within each client's configured rate. When constrained, prioritize authority and explicitly defer/coalesce eligible state rather than emitting an invalid partial snapshot. |
| Snapshot history | Bound by count, age and entity storage. An overwritten/invalid base produces a full/key frame, never a partial reconstruction. |
| T06 snapshot acceptance profile | A maximum WNC1 view contains 512 entities, 1,024 area bytes and 512 event references: 80,869 encoded bytes under the 131,072-byte payload cap. A 64-slot maximum-capacity canonical history stays below 16 MiB; fixed native sender-plus-receiver owners stay below 2 MiB. Build plus encode and decode each stay at or below 10% of the minimum 60 Hz authoritative-frame interval at p95 (1,666,600 ns). |
| Event retention | Gameplay-critical and reliable events cannot be dropped by cosmetic pressure. Cosmetic coalescing requires an explicit type policy and counter. |
| Prediction history | Keep a bounded command ring sized for the supported impairment window; overflow causes an instrumented hard resync rather than replaying ambiguous commands. |
| Rewind window | Competitive default target: 200 ms; hard public-policy ceiling target: 250 ms. Requests outside policy are clamped or rejected and counted. Coop policy may differ only through an explicit bounded server rule. |
| Rewind sampling | At least one full collision pose per authoritative simulation step, with compatible-sample interpolation and discontinuity rejection. |
| CPU | At supported player/entity counts, snapshot build/encode and rewind queries each target no more than 10% of the authoritative server-frame budget at p95; combined networking must leave the simulation within its frame deadline. |
| Memory | All per-client input/snapshot/event rings and server pose history have configured hard caps and publish current/high-water byte counts. No unbounded growth from an unacknowledging client. |
| Allocation | No steady-state heap allocation is permitted in per-entity snapshot iteration, command replay, event playback or individual rewind trace hot paths after initialization/warmup. |
| Correction | Deterministic replay should produce zero gameplay-state error for the declared prediction surface. Nonzero corrections require categorized telemetry; visual smoothing must never delay authoritative collision state. |
| Time validation | Future, stale and non-monotonic client times are bounded by server-derived clock policy. Client timestamps never directly select arbitrary world history. |
| Failure behavior | Invalid canonical ranges, missing bases, generation mismatch or sequence ambiguity fail closed through drop, keyframe, hard resync or disconnect policy; never unchecked memory access. |

### Ratified `FR-10-T14` acceptance floors

These floors are expressed as workload counts and fractions of the configured
authoritative frame budget so they remain meaningful across supported hardware.

- Mandatory load profiles use 1, 8, 16, and 32 active clients. Separate
  32-client super-shotgun and chaingun stress profiles run for at least 10
  minutes after a 60-second warmup and retain at least 5,000 measured
  authoritative frames each.
- Snapshot build/encode and historical-query work each remain at or below 10%
  of the authoritative frame budget at p95. Combined networking work remains at
  or below 25% at p99, with zero networking-attributable frame-deadline misses.
- Steady-state command, snapshot, event, and rewind hot paths perform zero heap
  allocations after warmup and never exceed their declared storage caps.
- A release candidate may regress no more than 5% at p95 or 10% at p99 against
  the latest accepted same-machine modern baseline while still satisfying the
  absolute frame-budget limits.
- Each changed network decoder and canonical range constructor processes at
  least 100,000 deterministic malformed/adversarial cases per acceptance run
  with zero crash, hang, out-of-bounds access, or unchecked-size acceptance.
- Deterministic acceptance permits zero state-hash divergence, double-simulated
  command, gameplay-critical event loss/duplicate, or invalid snapshot
  promotion. All results are machine-readable under `.tmp/networking/`.

### Ratified `FR-10-T15` release floors

- R1 shadow promotion requires at least 100,000 accepted snapshots with zero
  unexplained semantic mismatches.
- Every mandatory compatibility and impairment row passes in three consecutive
  full acceptance runs on each supported release platform.
- Each supported dedicated-server platform completes a continuous two-hour,
  32-client mixed-weapon/map-cycle/reconnect soak with zero crash, assertion,
  persistent hash divergence, critical-event loss/duplicate, history/ring
  overflow, or unexpected disconnect.
- R4/R5 features remain opt-in for at least seven calendar days and accumulate
  at least 100 aggregate server-hours before default review.
- Every kill-switch drill passes. Internal features return to the validated
  fallback within one snapshot boundary; a transport rollback completes within
  one explicit reconnect, without a process restart or in-flight
  reinterpretation.
- One correctness, security, or mandatory-compatibility failure blocks
  promotion immediately. A performance breach reproduced in two of three
  identical runs also blocks promotion.
- Release acceptance requires zero unresolved FR-10 P0/P1 defects, current
  operator/end-user documentation, and a freshly validated `.install/` payload.

Performance gates must record the machine, build type, map, player/entity count,
tick/snapshot rates and impairment profile. Relative regressions are evaluated
against a stored same-machine baseline, not an undocumented absolute number.

## Compatibility Matrix

| Client / producer | Server / consumer | Required behavior | Modern features |
|---|---|---|---|
| WORR client | Legacy Quake II-compatible server | Connect and play through the existing negotiated `q2proto` path | Canonical model and local cgame improvements may run; WORR-only wire features remain off |
| WORR client | Current WORR server using a legacy protocol | Preserve current connection, acknowledged-delta snapshots and demo behavior | Enable only features representable safely above the existing wire |
| Legacy-compatible client | WORR server | Preserve currently supported legacy protocol paths | WORR-only fields/events/transport remain off |
| WORR client | Future WORR-native server | Explicit capability/version negotiation selects the separate WORR adapter | Full canonical event/snapshot/input feature set when mutually supported |
| Legacy demo | WORR client | Parse and play through the existing legacy adapter | No reinterpretation as a WORR-native stream |
| Future WORR demo | WORR client | Versioned recording feeds the canonical timeline deterministically | Typed events, modern snapshots and seek metadata supported by recorded version |
| MVD/spectator stream | WORR client/tool | Version and capability are explicit; player-view switches reset timeline/prediction correctly | Modern stream optional; legacy stream remains independently parseable |

No mid-stream packet may be guessed as another protocol. A transport change
requires a negotiated connection boundary or reconnect.

## Security and Abuse Model

The network is untrusted; cgame and sgame should receive validated canonical
data rather than parser-owned buffers.

Required controls include:

- Hard bounds on message length, range count, entity number, payload size,
  fragment count, decompressed size, sequence window and history references.
- Checked arithmetic for sizes, offsets, time conversion, interpolation and
  sequence wrap.
- Capability/version negotiation resistant to downgrade ambiguity and reflection
  or amplification before address validation.
- Command rate, batch size and duration limits; duplicate commands may be
  acknowledged but never simulated twice.
- Server-derived timestamp mapping. A client cannot request an arbitrary rewind
  age, future shot, target identity or hit result.
- Rewind policy caps by mode/server, with counters for clamp, reject, history
  miss and discontinuity rejection.
- Stable entity generations in snapshot and history lookups to prevent slot-
  reuse confusion.
- Fuzz/property tests for wire adapters and canonical range constructors, plus
  deterministic malformed-input corpora.
- Low-cost default telemetry and rate-limited detailed logging so diagnostics do
  not create a denial-of-service path.
- No raw input history, authentication data, or personally identifying network
  address data in ordinary telemetry artifacts.

## Telemetry Catalog

### Transport and input

- RTT, smoothed RTT, jitter, packet loss, burst loss, reorder and duplicate
  counts.
- Bytes/packets sent and received by class, rate-limit deferrals, fragment and
  reassembly counts.
- Latest input sequence, acknowledgement lag, command batch/redundancy depth,
  duplicate/stale/future/rejected command counts and input-buffer high water.
- Native-shadow lifecycle/current-retired epochs, readiness/wire-commit state,
  TX/RX bypasses, carrier commits/rejections/drains, exact-duplicate ACK rearms,
  repeated proof enqueues/releases, ACK prepares/handoffs/rejections, join
  matches/retirements/mismatches/expiry, canonical replay-high-water drains,
  and command/sample-offset comparison outcomes.

### Snapshots

- Full versus delta snapshots, selected base age, invalid/missing/overwritten
  base fallbacks and keyframe requests.
- Snapshot bytes, entity create/update/remove/defer counts, visible entity count,
  canonical storage bytes and history high water.
- Encode, decode, validate, reconstruct and cgame transition timings.

### Events

- Events emitted/received/played/matched/deduplicated/late/dropped/coalesced by
  class.
- Sequence gaps, queue occupancy/high water, prediction mismatch and reliable
  acknowledgement age.

### Prediction and timeline

- Commands replayed per frame, authoritative error by state field, correction
  magnitude/cause, visual smoothing duration and hard-resync reason.
- Render interpolation delay, extrapolated frames/duration, missing transition,
  discontinuity reset and server-time adjustment.

### Lag compensation

- Requested, mapped and applied rewind age; clamp/reject/history-miss reason.
- Pose samples/interpolations/discontinuity rejects, candidate counts and query
  time.
- Hits accepted/rejected with compensated versus uncompensated result metadata,
  without trusting or logging client-supplied hit conclusions.
- History bytes, samples and high water by entity class.

Diagnostics should expose compact operator status plus machine-readable reports.
Presentation controls belong to `cg_`; engine transport/status controls belong
to `net_`, `cl_`, or `sv_`; gameplay fairness controls belong to `sg_`.

## Validation Matrix

Each implementation task selects the relevant rows and records exact commands
and artifacts in its `docs-dev/` implementation log.

| Class | Required coverage |
|---|---|
| Determinism | Shared client/server state hashes; command/event sequence wrap; repeated identical seed/run |
| Impairment | 0/50/100/200 ms latency baselines; jitter; 1/5/10/20% loss; burst loss; reorder; duplicate; throttled rate; ack stall |
| Snapshot | Full/key/delta; invalid and overwritten base; add/remove/reuse; PVS transitions; oversize/fragment; reconnect/map change |
| Events | Multiple same-tick events; loss/replay/late/duplicate; predicted match/mismatch; reliable control; cosmetic pressure |
| Prediction | Movement, stance, view and supported weapon state; collision correction; history overflow; pause; tick/rate change |
| Rewind | Exact sample/interpolation; cap; future/stale time; teleport/respawn/death; mover attachment; slot reuse; history miss |
| Gameplay | Weapon-by-weapon hitscan, projectile, melee, radius and coop/mover scenarios as their tasks mature |
| Compatibility | Legacy servers, current WORR legacy protocol, legacy clients where supported, legacy demos, future demo/MVD versions |
| Security | Fuzzed adapters/ranges, malformed lengths/counts, decompression/fragment limits, command flood, timestamp abuse, amplification |
| Performance | Player/entity/rate matrix, CPU p50/p95/p99, allocation count, bandwidth, history/ring memory and long soak |

## Supported Release Platform, Corpus, and Evidence Contract

The authoritative release-platform list is the checked-in `TARGETS` collection
in `tools/release/targets.py`, not an independently copied list in networking
tooling. As of 2026-07-12 it contains `windows-x86_64`, `linux-x86_64`, and
`macos-x86_64`, with client and dedicated-server artifacts for each. Adding or
removing a release target changes the next `FR-10-T15` platform matrix
automatically. Current build/package support does not by itself claim that the
networking acceptance corpus has passed on that target.

The checked-in corpus contract is:

- Versioned manifests live under `tools/networking/scenarios/`; release evidence
  records the repository revision and SHA-256 of every manifest and fixed input
  corpus used.
- `tools/networking/scenarios/impairment_matrix.json`, schema
  `worr.networking.impairment-matrix.v1`, is the required seed impairment matrix.
  Together with its checked-in golden, ordinary model tests, and staged runtime
  control it is the virtual-link evidence for `FR-10-T03`. Snapshot, event,
  prediction, rewind, compatibility, performance, and malformed-input corpora
  remain the responsibility of their owning downstream tasks.
- Generated cases use a recorded deterministic seed, generator/schema version,
  requested case count, and generated-corpus digest. Demo, packet, map, or other
  fixed fixtures are named and hashed; an unversioned local fixture cannot prove
  a release gate.
- Each mandatory validation-matrix row maps to at least one stable scenario ID.
  Removing or weakening a mandatory scenario requires a task-linked roadmap
  decision rather than silently updating a baseline.

The release evidence envelope will use schema
`worr.networking.acceptance-evidence.v1` and be written beneath
`.tmp/networking/<run-id>/`. It must contain, at minimum: run/scenario/schema
identity; source revision and dirty-state flag; platform ID and client/server
role; compiler, build type, executable/module hashes, and capability/transport
versions; map/mode, deterministic seed, tick/snapshot/rate settings,
player/entity counts, warmup/duration/sample counts, and impairment profile;
baseline ID and machine fingerprint suitable for same-machine comparison;
measured counters and p50/p95/p99 values with units; the exact threshold set;
per-gate pass/fail/reason; child artifact paths and SHA-256 values; and overall
result. Existing `worr.networking.baseline-evidence.v1` and
`worr.networking.impairment-runtime.v3` reports may be referenced as child
artifacts but are not, alone, the full acceptance envelope. Ordinary evidence
must not contain raw input history, authentication data, or personally
identifying network addresses.

## Rollout and Rollback

### Rollout stages

1. **R0: Inventory only.** Accept architecture, baselines and budgets. No runtime
   behavior or wire changes.
2. **R1: Shadow canonicalization.** Build immutable canonical views beside the
   current path, compare state/hashes, and discard them from authority.
3. **R2: Cgame range cutover.** Move consumers to immutable ranges one subsystem
   at a time behind a developer gate, retaining the current bridge until parity
   evidence is complete.
4. **R3: Prediction/event cutover.** Enable canonical event playback and expanded
   prediction over legacy transport for controlled testing, then opt-in servers.
5. **R4: Lag compensation opt-in.** Enable bounded hitscan rewind per mode/server
   after fairness, abuse and performance gates; extend mechanics independently.
6. **R5: WORR transport experimental.** Negotiate the separate transport only
   between explicitly capable peers; keep dual-stack legacy paths.
7. **R6: Release candidate/default review.** Promote individual features only
   when compatibility, impairment, soak, telemetry and user-doc gates pass.

### Rollback rules

- Every promoted subsystem has a server/client capability or policy kill switch
  until its release gate is complete.
- A disabled modern feature falls back to the last validated legacy/canonical
  behavior at a connection or snapshot boundary; it does not reinterpret an
  in-flight stream.
- Protocol rollback requires reconnect using explicit negotiation.
- Lag-compensation disablement returns to authoritative current-world traces; it
  never accepts cached client hit results.
- Canonical range or prediction invariant failures request a full snapshot/hard
  resync or disconnect according to severity.
- Rollback triggers include crash, memory corruption, persistent state hash
  divergence, unbounded queue/history growth, missed critical events, fairness
  bypass, compatibility regression, or failure of the declared frame budget.
- Removal of the previous internal module bridge waits until the replacement has
  parity evidence across normal play, demos and impairment scenarios.

## Documentation and Evidence Rules

Each significant slice must add a focused implementation log under
`docs-dev/`, preferably under a networking subsystem directory, and put its
task IDs near the top. The log records:

- Scope and non-goals.
- Ownership/API changes.
- Compatibility and security effects.
- Exact build/test/runtime commands.
- Budget measurements and impairment profile.
- Evidence paths under `.tmp/networking/`.
- Remaining task work, without marking a parent task complete for a foundation
  slice.

Public cvars, operator policy, visible diagnostics, or fairness changes also
require approachable updates under `docs-user/` before release promotion.

## Build, Stage, and Current Test Commands

### Windows Clang/LLVM (`builddir-win`)

The configured local workflow used by the repository's VS Code tasks is:

```powershell
tools\meson_setup.cmd setup --native-file meson.native.ini --reconfigure builddir-win -Dbase-game=basew -Ddefault-game=basew -Drmlui=enabled
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

For a fresh build directory, omit `--reconfigure` on the setup command.

### Windows MSVC

Do not use `meson.native.ini` with MSVC:

```powershell
tools\meson_setup.cmd setup -Dwrap_mode=forcefallback builddir
meson compile -C builddir
python tools\refresh_install.py --build-dir builddir --install-dir .install --base-game basew --platform-id windows-x86_64
```

### Linux/macOS

```bash
meson setup builddir -Dbase-game=basew -Ddefault-game=basew
meson compile -C builddir
python3 tools/refresh_install.py --build-dir builddir --install-dir .install --base-game basew
```

The refresh step replaces generated WORR runtime files, game modules,
`shader_vkpt`, release notices, and `basew/pak0.pkz`, while preserving
user-managed local data such as licensed `pak*.pak`, configs, saves, demos and
screenshots. It repackages current repository assets and validates the staged
Windows payload when `--platform-id windows-x86_64` is supplied.

The top-level build now registers the safe networking suite independently of
the dangerous legacy `-Dtests=true` option:

```powershell
meson test -C builddir-win --suite networking --print-errorlogs
meson compile -C builddir-win networking-baseline
meson compile -C builddir-win networking-runtime-smoke
```

The first two commands exercise the portable model, production scheduler/clock/
sequence primitives, repeatability, and checked-in golden on ordinary builds.
The Windows runtime target consumes a refreshed `.install/` and validates the
default-off and impaired client/server profiles. The existing top-level
`-Dtests=true` option still enables dangerous in-engine test code and must not be
treated as this unit suite or enabled in release builds.

Current Windows Clang integration inventory (2026-07-20): after the repeated
command shadow, mixed carrier, cgame event authority, narrow live event-family,
epoch-cancellation, local-interaction, lag-compensation, Stage D native
snapshot semantic admission, default-off production snapshot adapters, and the
private local-action shadow receipt, the production build links cgame, sgame,
client and dedicated engines, both launchers, all renderers, and the updater.
Meson registers 170 networking tests, including the T06 parent-level snapshot
acceptance gate, the T10 rewind-budget parser and contract, the T11 parent-gate
parser and acceptance contract, the exact canonical snapshot
prediction-authority selector, the fail-closed corpus-runner contract, the
two-run serialized production corpus, the direct T09 command gate, the final
task-level T05 event-journal gate, and the receipt mailbox, codec, cgame owner,
virtual-link, per-client damage/help-path adapters, exact-snapshot binder, and
production-placement coverage. The final serial Meson networking suite passes
170/170. Focused T11 unit contracts pass 76/76. The T10 parent gate passes five
fresh-process repeats; its final worst p95 is 1,075,300 ns against 1,666,600 ns,
with 591,300 ns (35.5%) headroom. The T11 parent gate passes all 40 cases and
120 matrix invocations, then 11 modes/33 canonical live repeats plus three
fallback repeats for 36 total live runs, with zero orchestration retries. Its
semantic SHA-256 is
`aa8aba98e1bc4e514badf7aed8a04e7e026afa2bc4f26d333d022cb9b5364bc1`.
Package tests pass 16/16, release unit tests pass 12/12, and the headless
bootstrap contract passes 1/1. The full bounded production build and final
`.install/` refresh/validation also pass. The retained serialized corpus still
records 100,000 accepted/acknowledged/released/selected positive frames and
golden `ba519ae7bdd1db74`.
The focused Stage D codec,
snapshot admission, server-view virtual link, cgame event-health, carrier ACK,
and envelope gate passes 6/6. Its server-view row now terminates at the real
default-off canonical cgame timeline/event runtime, and the runtime row proves
2,560 audit-off legacy references without diagnostic-table growth. The
production-hook row covers fragmented loss/reorder/duplicate, deferred
expectation, lost ACK/repeat, exactly-once cgame publication, exact release,
five-proof hash quarantine, wrong epoch, 1,024/8,192 identity domains, a
24-expectation delayed-DATA window, and complete-timeout slot reuse with
`prediction_ready=4` and digest `7176afa3d4eb62b2`. The focused production
replay continuation additionally reports
`receipt_fence_blocks=1`, `v2_replays=4`, `oracle_matches=3`, legal 127-command
replay, fail-closed 128-command clearing, and digest `abf2723d5f03cbe9`. The earlier
cancellation-focused 8/8 gate and three pre-Stage-D 125-test repetitions remain
valid historical evidence rather than a repetition of the current suite. The
event virtual-link digest remains `be9724b38fb5f682`. The final refreshed
Windows x86-64 stage validates 16 root runtime files, one dependency, and a
601-member pak, plus one loose q2aas reference map, 11 packaged q2aas AAS maps, 31
botfile payloads, and 215 RmlUi assets.
Focused command,
client, server, mixed-carrier, journal, cgame runtime/export/owner, and C/C++ ABI
layout tests pass, and the original plus repeated runtime-validator Python
suites pass 33/33 and 8/8. The preceding one-command slice also retains its
x86-64/i686
strict syntax, Clang analysis, and ASan/UBSan evidence. The post-hook schema-v3
runtime smoke accepts 388/388 clean and 386/386 impaired canonical frames in
cgame with zero snapshot/entity mismatch, frame failure, or rejection. Its
impaired profile exercised 7 drops, 7 duplicates, 6 reorders, and 1 throttle
event with queue checks passing; all 120 rewind-evidence invocations also
passed. The preceding pre-corpus `.install/` contained 16 root runtime files,
one dependency, a 483-member `pak0.pkz`, one q2aas reference map, 31 botfiles,
215 RmlUi assets, and five release notice sidecars; its
principal binaries match the production build. An immutable verified copy at
`.tmp/networking/native-shadow-isolated-20260714T063249514/.install` was used
for two-process evidence so concurrent staging work could not change a runtime
mid-gate; all 12 copied artifacts matched canonical staging at clone creation.
Three repeated latency reports pass with 152, 150, and 151 exact command
match/ACK/release cycles (453 total), zero final retention or failure, and
report hashes recorded in
`docs-dev/fr-10-t04-repeated-command-shadow-and-mixed-carrier-core-2026-07-14.md`.
The original one-command fragment/async gate also passes post-change.
`q2proto/` remained unchanged.
This is local integration and staging evidence only; it does not satisfy the
open load, soak, malformed-input, multi-platform, rollout, or release gates.

## Immediate Next Actions

1. Extend the visibility-safe per-peer descriptor/reliable stream beyond
   final-emission legacy entities, visible/explicit-positional/reliable/client-
   local spatial audio, bounded ordered transient/reliable mixed temporary-
   entity/player/monster/rerelease-muzzle sequences, exact per-client
   damage/help-path indicators, and the now-covered keyed POI family to other
   direct sgame service families and predicted local-action keys. Do not
   parse raw direct-game `svc_sound` spans
   until a structured final-emission or exact q2proto decode boundary exists
   (`docs-dev/fr-10-t04-t05-raw-direct-sound-adapter-decision-2026-07-15.md`).
   Replace individual legacy effect/audio presenters only after native
   snapshot fences and present-once parity evidence are exact
   (`FR-10-T05/T07/T08`).
2. Build on the accepted two-run 100,000-frame serialized production corpus by
   replacing its fixed eight-entity/four-area-byte scope with broader entity,
   event, lifecycle, visibility, keyframe, and expectation-invalidation cases.
   Exercise the engine V2 request boundary and real `cg_predict`/PMove replay,
   then record allocation, memory/CPU, load, bandwidth, real-socket,
   multi-client, cross-platform, and long-session evidence
   (`FR-10-T04/T06/T07/T08/T14`).
3. Build on the complete 22-identity/item/ammo/frame/callback-capability
   foundation, zero-of-22 V2 representability audit, bounded post-command
   observation lease, descriptor-complete shadow, and compact default-off
   authority receipt. The in-process native link now proves exact cgame
   command-hash correlation and zero presentation. Next add an in-session live
   disconnect/reconnect rebase fixture and live cross-process receipt parity,
   then extend observation descriptors only where a catalog blocker can be
   removed honestly. Do not construct or promote a V2 weapon transaction until
   a catalog entry's blocker mask is zero and state/event divergence plus
   correction budgets are proven (`FR-10-T08`).
4. The snapshot corpus now uses one reusable production `NetImpair` scenario
   and golden. Lift the implemented event ACK-exhaustion/cancellation and
   distinct-event selective-receipt proof from its explicit production-wrapper
   schedule into the full `NetImpair` golden matrix with sustained/live
   two-process faults and machine-readable evidence. Production-connect the
   Stage D complete-view snapshot transaction, then prove adaptive input age,
   recovery, bandwidth, correction, and 1/8/16/32-client budgets on both
   adapters before any authority promotion. WNC1 currently carries a complete
   reconstructed view; a future true delta codec requires an explicit codec
   revision plus a retained-base reconstruction contract rather than treating
   `base_id` as an implicit decode dependency (`FR-10-T04/T05/T06/T16`).
5. Extend the completed rewind-budget and authoritative-hitscan acceptance
   baseline with moving shooter/target/rider cases, multiple mover classes,
   piercing ordering, and broader projectile, melee, radius, deployable,
   trigger, and coop promotion under `FR-10-T12`; carry concurrent-client
   stress, platform load/soak, and release promotion through `FR-10-T14/T15`.
6. Complete demo/MVD/GTV/spectator matrices, malformed-input and sustained-load
   gates, cross-platform soaks, rollback drills, documentation review, and
   staged release acceptance (`FR-10-T13/T14/T15`).
