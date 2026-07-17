# FR-10-T07 deterministic snapshot timeline core

Date: 2026-07-12

Project task: `FR-10-T07`

Dependencies: `FR-10-T05`, `FR-10-T06`

## Scope and status

This change completed the transport-neutral timeline core behind cgame
migration. Subsequent integration now feeds parity-qualified accepted canonical
V2 snapshot views into the external cgame consumer. It still does not switch
live remote-entity rendering or event presentation to the new core. Promotion
requires impairment parity, live render/event consumers, and budget evidence.

### Subsequent integration status

`WORR_CGAME_SNAPSHOT_TIMELINE_EXPORT_V1` now transfers immutable value/range
views into a bounded cgame-owned timeline. The live consumer exposes stateful
clock, pair selection, entity sampling, snapshot/player copy-out, ordered event
iteration, reset, and diagnostics helpers. The client supplies a stateful
canonical server clock across FPS changes and preserves projection lineage
through precache and demo seeks. Meson now registers the core and layout tests.
Legacy rendering and event presentation remain authoritative; this is a live
audit/consumer seam, not a promotion claim.

The core is deliberately pointer-free at its public value boundaries. The
runtime owner has caller-provided fixed arenas; published slots contain copied
V2 records and generation-checked process-local references. No q2proto, demo or
wire format changed.

Update, 2026-07-16: the export/status ABI is now V2 so the added exact snapshot
admission receipt cannot be mistaken for the smaller V1 record. The Stage D
semantic ACK and event-health fence are documented in
`docs-dev/fr-10-t04-t06-native-snapshot-semantic-admission-2026-07-16.md`.
The paragraph above remains the historical V1 integration record.

Implementation:

- `inc/common/net/snapshot_timeline.h`
- `src/common/net/snapshot_timeline.c`
- `tools/networking/snapshot_timeline_test.c`
- `tools/networking/snapshot_timeline_schema_layout_c.c`
- `tools/networking/snapshot_timeline_schema_layout_cpp.cpp`

## Durable publication and corruption contract

Publication accepts a validated `worr_snapshot_projection_view_v2`, rejects
all overlap between the view, its payloads, the timeline arenas and the output
reference, then copies payload arenas before committing the slot metadata.
Failures leave the timeline, arenas and output untouched. Publication serials
never wrap; slot generations increment before reuse and reset preflights every
generation before changing any slot.

The timeline validates retained projection hashes before any operation that
reads snapshot content. It also checks:

- slot counts against fixed per-slot arenas;
- snapshot range counts against committed slot counts;
- controlled-player and entity-generation provenance;
- contiguous retained publication serials;
- snapshot ID, time and segment ordering;
- authority-event ID conflicts across all retained segments; and
- exact canonical player/entity/area/event hashes and the final snapshot hash.

Detected content damage returns `WORR_SNAPSHOT_TIMELINE_CORRUPT`; malformed
runtime-owner metadata returns `WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE`.
Copy functions resolve and validate a reference before deriving any arena
pointer, including zero-payload timelines, to avoid null-pointer arithmetic or
prevalidation out-of-bounds access.

## Clock semantics

The render clock uses integer microseconds plus an explicit unsigned Q16
fractional accumulator. This makes rate integration independent of update
chunking: for example, two one-microsecond advances at 0.5x produce the same
state as one two-microsecond advance. Multiplication is decomposed into high
and low 16-bit terms, so all overflow checks are portable uint64 checks rather
than compiler-specific 128-bit arithmetic.

`ANCHOR` initializes the clock. `ADVANCE`, `PAUSE`, `RESUME` and `SET_RATE`
consume monotonic host time; pause advances to the pause instant, and resume
moves the host anchor without accumulating paused time. `DEMO_SEEK` and
`RESET` clear the fractional accumulator and increment the clock epoch. Every
ignored request field must be zero, and every operation is transactional on
regression or overflow.

## Pair selection and entity sampling

Selection subtracts a bounded interpolation delay from render time and searches
only the active discontinuity segment. The immutable pair records timeline
instance, clock epoch, segment and a canonical policy fingerprint. Modes are:

- clamp to the earliest retained snapshot;
- hold the previous snapshot across a discontinuity;
- interpolate between a strict previous/current pair;
- select an exact snapshot;
- extrapolate from the last strict pair within the policy budget; or
- clamp to the latest snapshot when history, interval, discontinuity or policy
  forbids extrapolation.

Sampling rejects a pair after timeline reset, clock seek, segment change,
reference overwrite or policy mismatch. Entity add/remove and generation
replacement are step transitions: a newly added or replacement generation is
not shown early, and a removed entity remains visible only before the current
snapshot time. Snapshot discontinuities hold the previous entity.

For compatible generations, transform interpolation uses linear origins and
shortest-arc angles. Velocity is derived from the single immutable pair.
Teleport distance, three-axis linear-speed magnitude and angular-speed
magnitude independently block interpolation/extrapolation. Extrapolation is
bounded both globally and by policy. Discrete components are carried from the
previous record during interpolation and reported through the discrete
transition block bit; they are never numerically blended.

The timeline source must be compiled with the project's strict floating-point
arguments, matching the deterministic prediction/snapshot cores.

## Event cursor and deduplication semantics

An event cursor begins at the oldest snapshot retained in the active segment,
or at the next publication serial when the segment is empty. Iteration is in
publication serial then carrier order. A reset or segment boundary makes a
cursor stale. Overwriting an unread publication produces `CURSOR_OVERRUN`
instead of silently skipping events.

Authority events deduplicate by exact T05 authority ID. Legacy inferred events
deduplicate by semantic schema version and semantic hash; no authority ID is
fabricated. Match counts cover prior retained events in the active segment.
`EVENT_HISTORY_COMPLETE` is present only while the segment's first publication
is still retained. Once the ring has overwritten that publication, absence of
a retained match is explicitly not proof that an inferred event is globally
new.

## Hash and telemetry domains

`retained_content_hash` visits slots in publication order and includes receiver
publication/receive chronology, segment and the validated projection endpoint
hash. It excludes pointers, ring indices and slot generations.

`clock_hash` covers the exact clock epoch, host/render times, rate, pause state,
reset reason and Q16 fraction. `telemetry_hash` covers instance/segment/cursor
state, capacities/high-water marks and saturating counters. These domains are
diagnostic timeline hashes, not wire or cross-endpoint parity hashes.

## Verification performed

The standalone test was built with Clang C17, warnings-as-errors and
`-ffp-model=strict`, at both `-O0` and `-O2`. It covers:

- chunk-invariant fractional rates, pause/resume, seeks, regression and
  overflow rollback;
- interpolation, shortest-arc angles, bounded extrapolation, exact/earliest/
  latest selection and discontinuity hold;
- generation replacement and entity removal visibility;
- authority conflict detection and authority/legacy retained deduplication;
- cursor overrun and segment/reset staleness;
- ring overwrite, stale references and generation-exhaustion reset rollback;
- malformed envelopes, hostile aliases, zero-payload arenas and damaged source
  hashes; and
- deterministic hashes across independently allocated timelines.

The test passed 20 repeated runs. C17 and C++20 layout probes passed. Clang's
static analyzer reported no findings for the core or behavioral test.

The local Windows sanitizer runtime was not usable: the installed Clang ASan
binary could not load its runtime DLL, and standalone UBSan could not link the
required Windows sanitizer/DbgHelp imports. This is an environment limitation,
not sanitizer evidence. The same tests are now registered in Meson; supported
sanitizer-environment coverage remains a promotion gate.

## Integration requirements still open

Before marking `FR-10-T07` done:

1. Compare selected pairs, sampled transforms and event observations against
   legacy behavior under the deterministic impairment matrix.
2. Move remote-entity rendering and event playback to immutable value/range
   APIs, retaining an explicit rollback switch through promotion.
3. Complete classic-cgame ownership migration and verify pause, rate-change,
   discontinuity, demo, and long overwrite behavior in live scenarios.
4. Record CPU/history memory budgets and long wrap/overwrite/demo-seek soak
   evidence.
