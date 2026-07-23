# FR-10 Snapshot/Netcode 16-of-16 Completion Audit

Date: 2026-07-20  
Project tasks: `FR-10-T04`, `FR-10-T07`, `FR-10-T08`, `FR-10-T12`,
`FR-10-T13`, `FR-10-T14`, `FR-10-T15`, `FR-10-T16`; dependency task
`DV-04-T02`

## Decision

The snapshot/netcode project remains at **8 of 16 complete**. The completed
set is `FR-10-T01`, `T02`, `T03`, `T05`, `T06`, `T09`, `T10`, and `T11`.
This audit does not promote any remaining task on the strength of unit-level,
private-shadow, stale, single-platform, or parser-only evidence.

The shortest dependency-correct closure order is:

1. `FR-10-T04` public, default-off, exact native negotiation plus unified
   adapter/consumer and fault coverage.
2. `DV-04-T02` and `FR-10-T07` presentation ownership, immutable enumeration,
   bounded render timing, and real present-once event effects.
3. `FR-10-T08` declared predictable gameplay state, command-keyed replay,
   audiovisual reconciliation, and correction budgets.
4. `FR-10-T12` exhaustive interaction-policy and fairness matrices.
5. `FR-10-T13` native/legacy demo, MVD, GTV, seek, and spectator continuity.
6. `FR-10-T16` adaptive native batching/redundancy and common input-delivery
   gates.
7. `FR-10-T14` full telemetry, hostile-input, load, soak, and platform parent
   acceptance.
8. `FR-10-T15` rollout/rollback, supported-platform compatibility, elapsed
   opt-in exposure, release staging, and release floors.

## Criterion audit

| Task | Proven foundation | Direct unmet completion criteria | Dependency impact |
|---|---|---|---|
| `FR-10-T04` | Canonical command/event/snapshot codecs, WTC1 carrier, fragmentation, receipts, readiness, private live pilots, MTU and malformed-core tests | Public exact native negotiation is not yet a released path; remaining event/service families do not all reach one real presenter; legacy/native consumer equivalence, real-socket fault matrix, demo/spectator compatibility, and modern-to-legacy matrices are absent | Blocks `T13`, `T14`, `T15`, and `T16` |
| `FR-10-T07` | Immutable copied timeline/entity enumeration; generation-checked exact snapshot references; explicit render clock and bounded interpolation/extrapolation; ordered/deduplicated runtime; source-gated transform authority; exact previous-only renderer-submission evidence; one three-repeat live adaptive-delay cadence; two-phase value-only presenter behind a separate default-off effect-authority gate; default-off map-latched full-resource preflight probe; a strict three-repeat/two-map delayed-ACK Blaster row with five raw/present-once records, schema-2 four-record impact batching, exact chain parity, map generation `1 -> 2`, and stream epoch `2 -> 4` | The applicable `DV-04-T02` ownership inventory and exhaustive/per-carrier cutover are incomplete; raw legacy effects remain authoritative; adaptive/fault breadth remains open; the proven same-map row does not cover visual/audio parity across live loss, duplication, reordering, corruption, rate, pause/timescale, reconnect, demo seek, entity reuse, or all event families | Blocks `T08`, `T13`, `T14`, and `T15` |
| `FR-10-T08` | Canonical consumed-command movement replay, classified hard resync, immediate authoritative collision base, view-only correction offset | All 22 declared Rerelease weapon/action families remain shadow-only; no real predicted audiovisual presenter/suppression; no sustained impairment/load correction budget; predictable-state catalog is not ratified | Blocks `T14`, `T15`, and `T16` |
| `FR-10-T12` | Explicit policies for the ordinary weapon catalog and several projectile/melee/deployable seams; current-world contact ownership; immutable historical queries | No exhaustive interaction-family register; lifecycle, splash occlusion, BSP/player/mover/water collision, mover-relative, multi-target, coop/friendly-fire, abuse, and fairness cells are incomplete; no aggregate parent artifact | Blocks `T14` and `T15` |
| `FR-10-T13` | Legacy `.dm2` parsing, legacy/MVD detection, clock/cursor seek primitives, timeline reset primitives, a bounded WDM1/WDR1 canonical stream scan/index/seek core, an opt-in fixed-buffer snapshot-only client recorder with temp/validate/rename publication, and a pointer-free transactional WDM1 snapshot playback cursor with explicit reset generations and duplicate-free forward stepping | No standalone WDM bootstrap or file-backed/client timeline playback path; command/event recording and present-once replay are absent; decoded canonical bodies do not yet publish into the client playback timeline; MVD/GTV are legacy-only; spectator switches lack canonical discontinuities; no legacy-corpus and native relay parent matrix | Blocks `T14` and `T15` |
| `FR-10-T16` | Allocation-free adaptive controller, default-off legacy batch integration, one-command native observation, large-gap server intake, an exact-cursor bounded native fresh/retry policy, and a private default-off two-to-eight-command WNB1 production-hook shadow with authoritative-history joins, receipt blocking, and a tested map-bootstrap ACK/cancellation tombstone | The batch path remains private and observational; no authenticated received/consumed cursor feedback or native authority, no common native/legacy fresh-input-age, recovery, bandwidth, correction, and flood parent gate, and no complete tested cadence-decoupling contract | Blocks `T14` and `T15` |
| `FR-10-T14` | Strong component-level snapshot and rewind budgets plus 170 networking rows at the 8/16 baseline | Missing 1/8/16/32-client profiles; two separate 10-minute 32-client weapon loads; combined-network p99/deadline/allocation floors; 100,000 malformed cases for every changed decoder/range; concurrent stress, soak, security inventory, and supported-platform parent report | Blocks `T15` |
| `FR-10-T15` | Windows staging and historical private live evidence | Missing three complete compatibility/impairment runs per Windows/Linux/macOS; two-hour 32-client soak per dedicated platform; seven calendar days and at least 100 aggregate server-hours of opt-in exposure; complete rollback drills and native/demo/MVD matrix; current release-block/default audit | Final release gate |

## Work started from this audit

### Bounded production render timing (`FR-10-T07`, `FR-10-T14`)

The default-off native-authority render path now has explicit 50 ms
interpolation-delay and 50 ms maximum-extrapolation controls. The common
timeline still owns the absolute 1,000 ms interpolation and 250 ms
extrapolation ceilings, while entity generation, missing/component, teleport,
linear/angular velocity, and policy blockers remain fail-closed. Cumulative
interpolation, extrapolation, clamp, and extrapolated-microsecond telemetry is
included in the existing headless presentation row.

This is an advancement, not `T07` closure. Canonical entity enumeration and
real event effects were implemented by the subsequent slice below;
`DV-04-T02`, adaptive policy breadth, and the full fault/demo matrix remain
open.

Focused verification:

```text
python tools/networking/test_canonical_snapshot_render_policy_contract.py
  4 tests: pass
python tools/networking/test_run_canonical_rail_damage_runtime_gate.py
  49 tests: pass
meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-canonical-snapshot-render-policy-contract \
  network-canonical-rail-damage-runtime-gate-parser
  2/2: pass
meson compile -C builddir-win cgame_x86_64
  pass
```

### Immutable present-once event cutover (`FR-10-T07`, `DV-04-T02`)

The cgame now resolves event entities from the exact retained canonical
snapshot ID/generation into copied values. A side-effect-free preflight checks
only handles prepared during map precache and runs before the journal mark; the
value callback runs only after the present-once commit. Both callbacks reject
runtime mutation/replacement reentry. A separate cgame effect-authority gate
defaults false and is not connected to native ACTIVE. In that production
default, exact identity validation, ordering, audit, and terminal consumption
continue without a value-effect callback, while the raw legacy service and
frame-embedded one-shot presenters remain authoritative. Explicit test-only
enablement covers projected legacy entity/temp/muzzle/spatial-audio, damage,
and help-path families and rejects missing resources or busy lifecycles before
commit. Unknown effects and generic predicted local-action audio remain fail-
closed T08 work.

Focused verification:

```text
meson compile -C builddir-win cgame_x86_64 worr_engine_x86_64
  pass
.\builddir-win\cgame_canonical_snapshot_copy_entities_test.exe
  pass
.\builddir-win\cgame_event_runtime_test.exe
  pass
.\builddir-win\cgame_native_event_presenter_test.exe
  pass: direct production dispatch, exact identity, and fail-closed cases
python tools/networking/test_native_event_presenter_source_contract.py \
  --repo-root .
  pass
meson test -C builddir-win --print-errorlogs \
  network-cgame-native-event-presenter \
  network-cgame-event-runtime \
  network-native-event-presenter-source-contract \
  network-cgame-canonical-snapshot-copy-entities \
  network-native-client-readiness-pilot
  5/5: pass
```

This hardens and narrows the audit-only presenter slice; it does not remove the
production cutover blocker. Exhaustive/per-carrier ownership negotiation and
the real-process visual/audio fault and parity parent remain required.

### Map-latched full-resource preflight probe (`FR-10-T07`)

The default-off `cg_native_event_preflight_probe` is now sampled exactly once
at cgame map startup, before normal resource registration. A live cvar change
cannot activate or deactivate it mid-map. When latched, the probe prepares and
checks the exact cached resources and lifecycle conditions used by explicit
effect authority. Successful records are marked/accounted present-once first,
then reach a no-effect callback plan; raw legacy ownership is unchanged.
Missing resources reject before commit. Fixed version-1 status uses saturating
per-map totals for commits, suppressed effects, nonvisual records, and seven
families.

Focused verification remains 5/5 for the presenter, runtime callback/reentry,
source ownership contract, immutable snapshot copy, and readiness pilot rows.
The presenter test additionally proves default `0`/non-archived startup
sampling, mid-map change deferral, zero effect dispatch, missing-resource
failure, single-use accounting, and explicit effect-authority precedence.
Evidence:
`docs-dev/fr-10-t07-native-event-preflight-probe-2026-07-20.md`.

This is production-shaped preflight evidence only. The live process
loss/rate/pause/demo/resource-pressure matrix, visual/audio parity, ownership
inventory, load, platform, and release gates remain open, so T07 stays
**In Progress** and FR-10 stays **8/16 complete**.

### Schema-2 damage and same-process map reuse (`FR-10-T07`, `DV-04-T02`)

Damage now enters the client-to-cgame V2 range as the fifth legacy action
family. One to four protocol indicators are delivered transactionally, and the
controlled-player observed lineage is synchronized from the exact current
canonical snapshot when a first-person frame omits it. Same-coordinate native
events can form a schema-2 two-to-eight-record ACK/retry unit; the fixed
six-carrier audit status is 208 bytes.

The v100 parent retains the same server and two client processes across one
`gamemap`. Three repetitions pass both phases. Every phase records 15/15 damage,
five raw actions/effects, five present-once/probe commits, the exact family
profile `(0,0,2,1,1,1,0)`, one four-record schema-2 impact batch plus one muzzle
singleton, five delayed-ACK retries, identical raw/probe/effect chains, and zero
native effect dispatch, peer failure, duplicate/conflict, mismatch,
degradation, or resync. Map generation advances `1 -> 2`, map-end count
`0 -> 1`, and event stream epoch `2 -> 4`.

Server map quiesce now precedes map-local command reset, retained cgame shutdown
precedes its next initialization, and a sealed end-of-Begin reliable prefix
prevents later appends from starving the isolated native CHALLENGE. The runner
proves queue-neutral readiness before post-map client commands and drains
lifecycle EVENT traffic before opening each checkpoint window.

Focused verification passes 9/9 and runner units pass 96/96. The artifact
`.tmp/networking/fr10_t07_native_event_probe_checkpoint_v100_barrier_clean_repeat3.json`
has SHA-256
`545C6A46BD2E6A952958945CCE2F7DF2175551AE9423D5FFAF150DD31DA841D3`.
Detailed evidence:
`docs-dev/fr-10-t07-schema2-damage-map-reuse-present-once-evidence-2026-07-20.md`.

This proves one clean/delayed-ACK map-reuse profile only. Raw legacy effects
remain authoritative, and the ownership, family breadth, compound fault/demo,
visual/audio parity, load, platform, and release criteria remain open. T07 is
still **In Progress** and the project remains **8/16 complete**.

### In-flight bounded slices

- `FR-10-T04`: exact, all-or-nothing public native masks (`0x53`, `0x73`,
  `0x57`, `0x77`) with exact legacy `0x03` fallback, still behind both
  endpoints' opt-in and the existing readiness barrier.
- `FR-10-T07`: the value-copy entity range, exact snapshot lookup, canonical
  enumeration, default-off audit-only sink, guarded bounded presenter, and
  map-latched full-resource no-effect preflight probe are focused-green; one
  strict schema-2 five-record delayed-ACK profile also passes across retained-
  process map reuse. Exhaustive/per-carrier cutover plus the broad live
  fault/demo/effect-parity parent remain open.
- `FR-10-T12`: a manifest-bound aggregate runner for the already implemented
  modes. Its artifact must say `partial`; it cannot stand in for the missing
  interaction catalog or matrix cells.
- `FR-10-T13`: a separate opt-in `.wdm` snapshot recorder now captures only
  parity-qualified immutable admissions with bounded transactional writes and
  publishes only after complete validation. A separate pointer-free core can
  now bind, seek, and transactionally decode snapshots from caller-owned WDM1
  bytes/indexes with explicit reset generations, but it is not connected to a
  file reader or client timeline and does not provide event-once replay,
  spectator switching, or MVD/GTV support. `T13` remains partial.

Each in-flight slice requires focused build/test review before it is included
in evidence or roadmap progress.

### Private receipt/event/snapshot lifecycle hardening (`FR-10-T04/T08`)

The private local-action receipt no longer shares visual presentation's
progress boundary. A distinct ordered reconciliation cursor can cross admitted
visual gaps without consuming them, pins on missing IDs or callback rejection,
blocks callback-time mutation/replacement, and consumes terminal event sequence
`UINT32_MAX` once through explicit exhausted bits. Command-only cache pressure
without a receipt rolls forward without false resync. A real late receipt still
fails closed if it is behind recorded coverage loss and exact V2 command
history is unavailable. A monotonic latest-receipt frontier retains exact
duplicate bytes, prunes lower command-only and matched-terminal rows, and never
evicts unresolved receipt-only authority.

The server mailbox now latches typed invalid/epoch/regression/conflict/capacity/
order failure, rejects further publication until reset, and makes the post-
sgame `SV_ClientThink` boundary disable/drain the peer as native failure `18`.
Snapshot projection separately retains exact controlled-entity generation
provenance across delta, omission, first-carrier, and generation-only keyframe
paths, rejecting contradictory base entity/player evidence before hashing.

Fresh schema-v36 combined evidence at
`.tmp/networking/fr10_t04_combined_terminal_prune_current.json` passes 15/15
damage, exact-`0x77` preflight over both clients/server slots/snapshot peers,
and 29/29 private receipt matches with zero reconciliation fault. The recorded
full parent `.tmp/networking/fr10_t04_ordered_frontier_full.json` passes its
historical pre-capacity 10 focused and all five direct-numeric live masks with status
`partial` and `task_complete=false`.

The complete in-flight correlation composition is 798 entries (127 client
pending + 64 event TX + 512 event backlog + 63 selective successors + 32
mailbox), so the chosen fixed table is 1,024 entries. The hardened parent now
adds local-action correlation as an eleventh focused child and hashes the
shared bound inputs. Shared-constant assertions, both 798-entry arrival orders,
more-than-two-table rolling streams, six focused Meson rows, and the production
cgame build pass. Final post-stage parent
`.tmp/networking/fr10_t04_bounded_lifecycle_full_retry.json` passes 11/11
focused plus 5/5 live; combined records 30/30 matches, zero fault, preflight
plus post-fire proof, and deliberate `task_complete=false`. Detailed
engineering record:
`docs-dev/fr-10-t04-t08-private-receipt-bounded-lifecycle-2026-07-20.md`.

This is bounded default-off progress only. It does not close the public
adapter/presenter, weapon-prediction/audiovisual, impairment/load, demo,
platform, soak, or release criteria. The decision remains **8/16 complete**.

## Ratified evidence floors that must not be weakened

`FR-10-T14` requires 1/8/16/32 active-client profiles, two independent
10-minute 32-client profiles after a 60-second warm-up with at least 5,000
frames, snapshot/rewind work at or below 10% of the authoritative-frame p95,
combined networking at or below 25% p99, zero deadline misses, zero
steady-state allocations for fixed-capacity owners, same-machine regression no
worse than 5% p95 and 10% p99, at least 100,000 malformed cases per changed
decoder/range, and zero deterministic correctness errors.

`FR-10-T15` requires three full compatibility/impairment runs on every
supported platform, a two-hour 32-client soak on each dedicated platform,
seven calendar days and at least 100 aggregate server-hours of opt-in exposure,
rollback within one snapshot/reconnect boundary, failure-blocking release
automation, no open FR-10 P0/P1 issues, current user/developer documentation,
and a freshly validated `.install/` payload.

The supported release platform list is Windows, Linux, and macOS, as defined
by `tools/release/targets.py`. Elapsed-time and platform evidence must be
collected rather than synthesized.

## Required final proof shape

Every remaining task needs a schema-versioned parent JSON under
`.tmp/networking/`, fresh binary/config/manifest hashes, explicit child-gate
results, normalized semantic digests, platform/build/compiler identity, and a
closure document tied to the task ID. The final 16/16 transition additionally
requires the full networking suite, package/release/bootstrap gates, q2proto
read-only guard, refreshed `.install/`, and a roadmap accounting audit with no
dependency or denominator mismatch.
