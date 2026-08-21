# FR-10 outstanding-work inventory and delivery plan

Date: 2026-07-25

Task IDs: `FR-10-T04`, `FR-10-T07`, `FR-10-T08`, `FR-10-T12`, `FR-10-T13`,
`FR-10-T14`, `FR-10-T15`, `FR-10-T16`.

Parent: `docs-dev/plans/progressive-networking-events-snapshots-roadmap.md`.

Status: **inventory and plan**, plus the two defect fixes recorded at the end.
This document does not promote any task or close any acceptance gate.

## Scope and non-goals

The parent roadmap records eight in-progress tasks in dense prose. This
inventory resolves that prose into discrete work items, each verified against
real source rather than against the roadmap's own claims, and classifies each
item by what kind of evidence can close it.

Non-goals: this is not a re-plan of the accepted architecture, does not change
any acceptance criterion, and does not edit `q2proto/`.

## Method

Each in-progress task section, its cited `docs-dev/` evidence logs, and the
roadmap's Immediate Next Actions were read, then every claimed-open item was
checked against source and the `meson` `networking` suite wiring. Items were
then independently re-checked with the opposite objective -- to prove the item
was already implemented, or that no suite test could prove it. Items that
survived both passes are the buildable frontier.

One methodological correction is worth recording, because it is a trap for the
next reader: the `networking` suite is **not** limited to pointer-free cores.
`native_event_virtual_link_test` and `native_snapshot_production_virtual_link_test`
compile real production sources -- `src/client/native_readiness_pilot.cpp`,
`src/game/cgame/cg_predict.cpp`, `src/game/cgame/cg_event_runtime.cpp`,
`src/server/native_shadow.c` -- directly into suite executables. Several rows
the roadmap frames as needing a live client are therefore provable in the
suite today.

## Classification

| Mark | Class | Closable by |
|---|---|---|
| `U` | in-repo, unit-testable | a test in the `meson` `networking` suite |
| `G` | in-repo, needs a running game | a live client/server session (the verification runbook) |
| `C` | needs CI or multiple platforms | soak/load/multi-platform runs |
| `X` | needs external evidence | calendar-time operator exposure |

Size marks are `S`/`M`/`L`/`XL`.

## Totals

139 outstanding items across the eight in-progress tasks:

| Task | Items | `U` | `G` | `C` | `X` |
|---|---|---|---|---|---|
| `FR-10-T04` | 16 | 11 | 2 | 3 | 0 |
| `FR-10-T07` | 15 | 10 | 4 | 1 | 0 |
| `FR-10-T08` | 17 | 14 | 2 | 1 | 0 |
| `FR-10-T12` | 17 | 7 | 9 | 1 | 0 |
| `FR-10-T13` | 15 | 9 | 6 | 0 | 0 |
| `FR-10-T14` | 23 | 18 | 3 | 2 | 0 |
| `FR-10-T15` | 16 | 11 | 1 | 3 | 1 |
| `FR-10-T16` | 20 | 16 | 2 | 2 | 0 |
| **Total** | **139** | **96** | **29** | **13** | **1** |

96 items are unit-testable in-repo; 83 of those survived the adversarial
re-check as both genuinely unimplemented and genuinely provable in the suite.

The `C` and `X` rows cannot be closed from a development session by
construction, and the runbook already says so: the T14/T15 load, soak,
multi-platform and security floors "require multi-day and multi-platform runs".

## Priority

Ordinary roadmap items are ranked below two defects found during the audit.
Both are in work already committed as done, and both make the committed
feature non-functional rather than merely incomplete. They take precedence
over any new scope.

1. `t13-timeline-accepts-bootstrap-record` (**fixed, see below**)
2. `t08-event-abi-predict-seed-payload-kind` (**fixed, see below**)

After those, the highest-value ordering is: the remaining `FR-10-T08`
carriage-correctness items (the seed path is load-bearing for the runbook's
P2 gate and is currently unprovable), then `FR-10-T14`'s acceptance-runner and
evidence-envelope foundations (many other tasks' gates depend on them), then
per-task breadth.

## Inventory

### FR-10-T04
- [U][M] **t04-snapshot-combined-ack-exhaustion-rotation** — Extend ACK-credit exhaustion and map-rotation breadth from the event lane to the snapshot and combined lanes
- [U][M] **t04-native-wire-byte-telemetry** — Add cumulative native wire-byte accounting to the peer/pilot status so bandwidth budgets are measurable
- [U][M] **t04-nonvisible-game-event-sources** — Decide and implement a safe binding for non-visible game-event sources instead of dropping them to legacy-only
- [U][L] **t04-netimpair-matrix-over-native-lanes** — Drive the full NetImpair profile matrix through the native event/snapshot/combined lanes with a checked-in golden
- [U][XL] **t04-remaining-sgame-service-families** — Add candidate adapters for the remaining direct sgame service families (layout, inventory, achievement, stufftext, fog)
- [U][L] **t04-combined-0x77-sustained-corpus** — Add a sustained combined event+snapshot (0x77) serialized corpus with its own golden digest
- [U][XL] **t04-wnc1-true-delta-codec-revision** — Define a WNC1 wire revision 2 true delta codec with an explicit retained-base reconstruction contract
- [U][L] **t04-multi-peer-server-fairness** — Prove server-side fairness and a shared budget across N concurrent native peers
- [U][L] **t04-corpus-scope-broadening** — Replace the corpus's fixed 8-entity/4-area-byte view with broad entity, event-ref, keyframe, visibility and expectation-invalidation cases
- [U][XL] **t04-structured-sound-carrier-boundary** — Introduce the structured game-import sound carrier required to lift raw svc_sound spans and positionless non-local sounds out of legacy-only
- [U][L] **t04-dual-adapter-adaptive-correction-budgets** — Prove adaptive input age, recovery and correction budgets identically on the legacy and native adapters
- [G][L] **t04-live-two-process-event-snapshot-impairment** — Extend live two-process impairment evidence from the command lane to the event, snapshot and combined lanes
- [G][L] **t04-legacy-demo-mvd-relay-matrix** — Prove the supported legacy demo / MVD / GTV relay matrix stays byte-identical while a native endpoint is armed
- [C][L] **t04-client-count-budget-matrix** — Record 1/8/16/32-client bandwidth and correction budget evidence on both adapters
- [C][XL] **t04-load-soak-cross-platform-release** — Produce sustained load, soak, cross-platform and release-matrix evidence for the negotiated transport
- [C][M] **t04-authority-promotion-and-advertisement** — Promote the native endpoint beyond default-off shadow: advertisement, default-on, and rollback drill

### FR-10-T07
- [U][M] **t07-acceptance-parent-runner** — Add the missing FR-10-T07 acceptance parent runner and meson gate
- [U][S] **t07-adaptive-delay-loss-burst-coverage** — Cover the adaptive interpolation-delay loss-burst and cadence-change paths
- [U][M] **t07-demo-clock-timeline-planner** — Build a pointer-free demo-transport to canonical-clock planner so demo playback stops disabling the timeline
- [U][S] **t07-classic-cgame-policy-contract** — Decide and encode the classic-cgame timeline/event policy
- [U][M] **t07-presentation-budget-probe** — Add the FR-10-T07 presentation/timeline CPU and allocation budget probe
- [U][M] **t07-generic-audio-effect-asset-mapping** — Resolve or formally retain the fail-closed generic AUDIO/EFFECT payload rejection
- [U][L] **t07-predicted-presenter-lane** — Give the native event presenter a predicted-provenance lane instead of degrading the epoch
- [U][L] **t07-per-family-ownership-negotiation-core** — Implement the exhaustive per-carrier/per-family event ownership negotiation core
- [U][M] **t07-timeline-indexed-lookup-debt** — Replace the linear snapshot/entity lookups named as performance debt in the cgame timeline
- [U][L] **t07-production-corpus-entity-event-breadth** — Broaden the serialized production snapshot corpus past its fixed 8-entity scope
- [G][L] **t07-live-fault-matrix-gate-modes** — Add loss/duplication/reordering/corruption/rate-limit rows to the live presenter gates
- [G][L] **t07-live-pause-timescale-demo-seek-reuse-rows** — Prove pause/resume, timescale, reconnect, entity-reuse and demo record/play/seek rows in a live presenter parent
- [G][XL] **t07-live-visual-audio-parity-all-families** — Prove visible/audible exactly-once parity for every projected event family in real processes
- [G][L] **t07-raw-family-cutover-live-proof** — Prove one negotiated raw-family cutover end to end and reclassify the inventory
- [C][L] **t07-presenter-load-soak-platform-matrix** — Carry presenter/timeline load, soak, malformed-input and supported-platform evidence

### FR-10-T08
- [U][M] **t08-event-abi-predict-seed-payload-kind** — Make native event payload kind 14 (predict seed) actually admissible
- [U][M] **t08-replay-range-cursor-idempotence** — Make CG_WeaponPredictReplayRange cursor-idempotent instead of re-replaying the whole unacked range
- [U][M] **t08-seed-ready-phase-gate-and-change-detection** — Gate seed publication on the server weapon actually being READY, and only publish on change
- [U][S] **t08-seed-publish-cursor-ordering-contract** — Define and pin the seed's exact command boundary (publish/consume ordering)
- [U][L] **t08-predicted-transaction-retention** — Retain predicted transactions so reconciliation is possible at all
- [U][M] **t08-predictor-correction-telemetry** — Surface the weapon predictor's replay/correction/resync counters and correction classes
- [U][S] **t08-local-player-passthrough-and-lifecycle-reset** — Deactivate the predictor when the presented player is not the locally predicted client
- [U][M] **t08-fire-cue-dedup-core-extraction** — Extract the predicted fire-cue debt/high-water dedup into a testable pointer-free core
- [U][M] **t08-predicted-event-asset-identity** — Resolve predicted fire audio/effect events to real asset identities or explicitly exclude them
- [U][M] **t08-seed-sample-time-consumed-boundary** — Stamp the seed with the consumed command's exact sample time, not the received one
- [U][L] **t08-weapon-switch-intent-source** — Give the client predictor a weapon-switch intent source (the tested switch path is unreachable)
- [U][S] **t08-seed-decoder-malformed-fuzz** — Add the 100,000-case malformed-input fuzz for the new predict-seed and kind-14 decoders
- [U][L] **t08-end-to-end-seed-virtual-link-gate** — Add a full server->client weapon-seed/predict/present gate to the production virtual-link test
- [U][L] **t08-authoritative-transaction-receipt** — Produce and carry the authoritative local-action transaction for the predictable surface
- [G][L] **t08-running-game-behavioral-gate** — Execute the runbook P1/P2 running-game gate and file the evidence
- [G][S] **t08-frame-ms-cadence-tuning** — Tune and pin the single animation frame-cadence constant against live weapon timing
- [C][XL] **t08-impairment-load-correction-budget** — Prove the correction budget under sustained impairment and the 1/8/16/32-client load profiles

### FR-10-T12
- [U][M] **t12-interaction-family-register** — Machine-checkable interaction-family policy register
- [U][M] **t12-fixture-bsp-mover-and-trigger-classes** — Extend the generated fixture BSP beyond func_rotating/func_water
- [U][M] **t12-abuse-manifest-and-source-contract** — T12 abuse case matrix in the manifest plus fail-closed source contracts
- [U][S] **t12-t11-manifest-schema-pin-drift** — T11 aggregate manifest pins canonical schema v37 while the child emits v42
- [U][S] **t12-projectile-forward-cvar-documentation** — Document the two T12-introduced lag-compensation controls
- [U][M] **t12-multi-client-harness** — N-client roster support in the canonical headless runner and T12 manifest
- [U][L] **t12-trigger-volume-pose-history-core** — Pointer-free history support for non-solid trigger volumes
- [G][XL] **t12-nonweapon-interaction-modes** — Live evidence modes for the non-weapon interaction catalog
- [G][XL] **t12-projectile-family-lifecycle-observers** — Ownership/lifetime/retirement observation for the non-Rocket projectile families
- [G][XL] **t12-deployable-and-attachment-lifecycles** — Deployable, trigger and attachment lifecycle policies for the register's seven uncompensated families
- [G][L] **t12-multi-target-and-friendly-fire-modes** — Multi-target, friendly-fire and pierce-ordering fairness modes
- [G][L] **t12-moving-shooter-rider-and-mover-class-modes** — Moving shooter/target/rider modes across multiple mover classes
- [G][L] **t12-coop-monster-and-horde-policy** — Coop/monster interaction policy and evidence
- [G][M] **t12-other-melee-policy** — Explicit policy for melee families other than Chainfist
- [G][M] **t12-projectile-forward-and-melee-budget** — Performance/load budget for the projectile-forward and melee-selection hot paths
- [G][S] **t12-full-aggregate-117-repetition-run** — Execute the full 39-mode x 3 aggregate gate and publish the artifact
- [C][M] **t12-platform-and-load-promotion** — Cross-platform and sustained-load evidence for the T12 gate

### FR-10-T13
- [U][M] **t13-timeline-accepts-bootstrap-record** — Timeline planner rejects BOOTSTRAP records, so every real client recording fails to load
- [U][M] **t13-timeline-cursor-seek-to-step** — No primitive to reposition the present-once frontier to an arbitrary plan step/time
- [U][S] **t13-player-source-contract-test** — No source/lifecycle contract test for src/client/native_demo_player.cpp
- [U][M] **t13-acceptance-gate-parser-contract** — No deterministic parser/contract test for an FR-10-T13 acceptance-report producer
- [U][S] **t13-bootstrap-configstring-stream-roundtrip** — Bootstrap configstring table is never exercised through the full record→scan→index→decode path
- [U][L] **t13-spectator-view-switch-record** — No record class or recorder support for a spectator / controlled-view switch
- [U][M] **t13-timeline-discontinuity-flag** — Plan carries no discontinuity marker for dropped snapshots (packet loss in a recorded stream)
- [U][S] **t13-demo-clock-pause-timescale** — Native demo clock has no pause or timescale; disabling the cvar is the only pause
- [U][L] **t13-epoch-continuation-sequence-reset** — A recording cannot span a sequence reset / epoch change (map change ends the capture)
- [G][M] **t13-demo-epoch-publication-and-reset** — Player never resets/publishes the cgame canonical timeline or event authority epoch, so presentation is a guaranteed no-op
- [G][L] **t13-mapload-reaches-active-and-full-fidelity** — native_mapload stalls at ca_precached and drops captured serverdata fields
- [G][M] **t13-native-seek-command-wiring** — File-backed seek is unwired: no native_seek command and zero client call sites for the seek cores
- [G][M] **t13-client-event-command-capture** — Client recorder captures only snapshots — no event or command records are ever written
- [G][XL] **t13-mvd-gtv-native-support** — Zero native (WDM1) integration for MVD/GTV or spectator relay
- [G][L] **t13-live-record-play-seek-relay-matrix** — The FR-10-T13 live record/play/seek/relay acceptance matrix does not exist

### FR-10-T14
- [U][L] **t14-acceptance-runner-and-manifest** — Create the FR-10-T14 acceptance runner, scenario manifest, and meson test
- [U][M] **t14-shared-evidence-envelope-module** — Extract a shared worr.networking.acceptance-evidence.v1 writer + validator
- [U][M] **t14-baseline-store-and-regression-comparator** — Implement the same-machine performance baseline store and 5%/10% regression comparator
- [U][L] **t14-decoder-fuzz-coverage-expansion** — Extend deterministic malformed-input fuzzing to every changed decoder
- [U][M] **t14-canonical-range-constructor-fuzz** — Add malformed-range corpora for the canonical range constructors and validators
- [U][M] **t14-event-telemetry-counters** — Implement the Events section of the telemetry catalog (no counters exist today)
- [U][M] **t14-transport-class-and-impairment-telemetry** — Add the missing transport telemetry rows: burst loss, reorder, duplicate, bytes/packets by class, rate-limit deferrals, reassembly
- [U][M] **t14-command-rate-flood-limiter** — Implement the command rate / batch / duration limiter required by the abuse model
- [U][S] **t14-declared-budget-table** — Check in the declared CPU/memory/bandwidth/correction budget table the acceptance runner enforces
- [U][M] **t14-corpus-cross-snapshot-netchan-reorder** — Drive real netchan sequencing and cross-snapshot reorder through the production corpus
- [U][M] **t14-memory-byte-highwater-publication** — Publish current and high-water BYTE counts for every per-client ring and pose history
- [U][M] **t14-prediction-correction-telemetry** — Categorize prediction correction telemetry beyond the local-action audit ring
- [U][M] **t14-security-scenario-gate-binding** — Bind timestamp-abuse and rewind-cheat scenarios into an asserted security gate
- [U][M] **t14-amplification-and-downgrade-resistance** — Bound and test connectionless amplification factor and capability downgrade ambiguity
- [U][L] **t14-corpus-impairment-matrix-sweep** — Sweep the serialized production snapshot corpus across all 15 ratified impairment profiles
- [U][L] **t14-corpus-semantic-breadth-to-max-projection** — Widen the production corpus from 8 entities/4 area bytes to the ratified maximum WNC1 view
- [U][M] **t14-steady-state-allocation-proof** — Replace the hard-coded allocation-free claim with a measured zero-allocation harness
- [U][M] **t14-machine-readable-status-report-writer** — Add a schema-versioned machine-readable telemetry report writer under .tmp/networking/
- [G][L] **t14-multi-client-load-harness** — Build the 1/8/16/32-client load harness (current gates cap at 3 clients)
- [G][L] **t14-real-socket-loopback-evidence** — Produce real-socket loopback evidence for the native snapshot/event pipeline
- [G][L] **t14-networking-frame-budget-attribution** — Instrument server-frame time attribution for combined networking work
- [C][XL] **t14-weapon-stress-and-soak-profiles** — Run the 32-client super-shotgun and chaingun stress and soak profiles
- [C][XL] **t14-cross-platform-acceptance-matrix** — Extend the acceptance matrix to the non-Windows release targets

### FR-10-T15
- [U][M] **t15-user-control-surface-audit** — Audit public networking control surface against docs-user and document the 15 missing cvars
- [U][S] **t15-platform-matrix-from-targets** — Derive the networking release platform matrix from tools/release/targets.py TARGETS
- [U][M] **t15-compatibility-matrix-manifest** — Check in the compatibility-matrix scenario manifest with stable scenario IDs
- [U][M] **t15-validation-matrix-scenario-registry** — Enforce the mandatory validation-matrix row to scenario-ID mapping
- [U][L] **t15-rollback-decision-core** — Add the pointer-free kill-switch / rollback decision core
- [U][M] **t15-defect-register-and-release-block** — Add the FR-10 P0/P1 defect register and make it release-blocking
- [U][M] **t15-optin-exposure-ledger** — Build the R4/R5 opt-in exposure ledger accumulator
- [U][M] **t15-release-ci-networking-gate** — Make the release workflow fail on a networking-suite or acceptance-gate failure
- [U][M] **t15-rollout-rollback-operator-docs** — Document rollout stages, kill switches and rollback behavior in docs-user
- [U][M] **t15-roadmap-truthfulness-and-evidence-log** — Write the missing FR-10-T15 evidence log and correct the stale T15 progress claims
- [U][L] **t15-parent-release-acceptance-runner** — Build the FR-10-T15 parent release-acceptance runner and evidence envelope
- [G][L] **t15-killswitch-drills-live** — Exercise the kill-switch and transport-rollback drills against a live session
- [C][XL] **t15-three-consecutive-platform-runs** — Run three consecutive full compatibility and impairment acceptance runs on each supported platform
- [C][XL] **t15-two-hour-32client-soak** — Run the two-hour 32-client soak on each supported dedicated-server platform
- [C][M] **t15-install-validate-linux-macos** — Refresh and validate the .install/ payload on linux-x86_64 and macos-x86_64
- [X][L] **t15-optin-exposure-evidence** — Accumulate the seven-day / 100 server-hour opt-in exposure evidence

### FR-10-T16
- [U][M] **t16-received-cursor-sideband** — Authenticated server received-cursor feedback sideband
- [U][L] **t16-adaptive-impairment-acceptance-probe** — Adaptive-input impairment acceptance probe and machine-readable networking-suite gate
- [U][M] **t16-input-age-telemetry-core** — Common input-age (freshness) accounting core for both adapters
- [U][M] **t16-bandwidth-accounting-core** — Upstream command bandwidth accounting and budget core
- [U][M] **t16-retry-exhaustion-recovery** — WNB1 retry-exhaustion recovery policy and telemetry
- [U][M] **t16-map-rotation-liveness-breadth** — Multi-rotation WNB1 map-rotation liveness coverage
- [U][L] **t16-cadence-decoupling-core** — Explicit sampling/simulation/render/send cadence contract core
- [U][M] **t16-command-flood-policy-core** — Bounded per-client command-intake flood policy
- [U][L] **t16-netimpair-golden-matrix-native** — Lift native command/input fault proofs from scheduled wrappers into the NetImpair golden matrix
- [U][M] **t16-telemetry-correlation-record** — Machine-readable dual-adapter telemetry record correlating pacing with correction and cursors
- [U][L] **t16-input-batch-virtual-link** — Full-duplex WNB1 input-batch virtual link with impairment and convergence digest
- [U][M] **t16-native-intake-duplicate-idempotence** — Deterministic duplicate/replay idempotence proof for promoted native intake
- [U][M] **t16-default-threshold-tuning** — Tune adaptive default thresholds from measured impairment evidence
- [U][M] **t16-server-rate-feedback** — Server-side effective-rate and intake-pressure feedback to the adaptive controller
- [U][M] **t16-planner-authenticated-feedback** — Feed authenticated cursors into the delivery planner and reset it on every lifecycle
- [U][L] **t16-production-selective-redundancy** — Enable selective redundancy in the WNB1 production hook
- [G][M] **t16-cadence-client-wiring** — Wire the cadence contract into CL_Frame and the send path
- [G][XL] **t16-dual-adapter-live-parent-gate** — Live dual-adapter input-age, recovery, bandwidth, correction, and flood parent gate
- [C][XL] **t16-multiclient-budget-matrix** — 1/8/16/32-client adaptive delivery budget matrix on both adapters
- [C][XL] **t16-load-soak-platform-gates** — Load, soak, and supported-platform gates for the adaptive input path

## Defects fixed in this pass

Both were regressions in already-committed work, found by the audit and
confirmed against source before any edit. Each fix is guarded by a test that
was verified to fail with the fix reverted.

### `t13-timeline-accepts-bootstrap-record`

`worr_timeline_record_kind_known` (`src/common/net/native_demo_timeline.c`)
accepted only COMMAND/SNAPSHOT/EVENT. The recorder writes a
`WORR_NATIVE_DEMO_RECORD_BOOTSTRAP_V1` record as the **first** record of every
client recording, so `Worr_NativeDemoTimelineBuildV1` returned `WRONG_CLASS`
for every real recording and `native_play` could not build a plan at all --
the bootstrap increment silently broke the player it was meant to enable.

Fix: the planner accepts a BOOTSTRAP prologue, mirrors the container's
bootstrap-precedes-everything rule (legal only as the first record, so a
mid-stream prologue fails closed with `ORDER`), plans it as a step so the 1:1
step/entry mapping is preserved, and marks it
`WORR_NATIVE_DEMO_TIMELINE_STEP_BOOTSTRAP`. A prologue is not counted as a
command, never becomes an event anchor, and does not cost the stream its
`LEADING_SNAPSHOT` classification -- that flag now means "the first
*presentable* record is a snapshot", which is byte-identical behaviour for
streams without a prologue. No struct changed: both new flags occupy existing
free bits, so every layout assertion and the C/C++ parity test are untouched.

Coverage: `test_bootstrap_prologue_plan` and `test_mid_stream_bootstrap_fails`
in `tools/networking/native_demo_timeline_test.c`.

### `t08-event-abi-predict-seed-payload-kind`

`WORR_EVENT_PAYLOAD_LOCAL_ACTION_PREDICT_SEED_V1 = 14` was declared in
`inc/shared/event_abi.h` with no case in `payload_size_for_kind`,
`payload_matches_event_type`, `validate_payload`, `hash_payload` or
`payload_semantically_equal` (`src/common/net/event_abi.c`), nor in
`event_payload_wire_bytes`, `write_event_payload` or `read_event_payload`
(`src/common/net/native_codec.c`). `Worr_EventRecordCandidateValidateV1`
therefore rejected every seed the server built, so the peer drained and the
entire FR-10-T08 weapon-prediction carriage was dead on arrival -- the
predictor, producer, mailbox and reconciliation were all reachable code with
no possible input.

Fix: the payload is wired through all eight sites. The seed record moved from
`inc/common/net/local_action_predictor.h` to a new
`inc/shared/local_action_predict_seed.h` because it is an event payload and
`shared/event_abi.h` must be able to size, validate, hash and serialize it,
while the predictor header already depends on `event_abi.h` (defining it there
would be circular). Validation delegates to the predictor core's own
`Worr_LocalActionPredictSeedValidateV1` rather than restating the contract.
The struct is unchanged and now carries its own layout assertions.

Coverage: `test_predict_seed_event_codec` in
`tools/networking/native_codec_test.c` proves the exact candidate the server
builds validates, the ID-assigned record round-trips byte-for-byte with a
matching semantic hash, and that a non-predictable weapon, a wrong schema
version, a non-zero reserved field and a mismatched payload size each fail
closed.

### `t08-seed-sample-time-consumed-boundary` (UNVERIFIED)

With kind 14 admissible, the seed's own contents matter. The publisher
(`SV_WorrQueueWeaponPredictSeed`, `src/server/user.c`) paired the **consumed**
cursor with `last_received_sample_time_us`, which is the **received**
frontier. Reception normally runs ahead of consumption, so the seed carried a
time belonging to a later command than the boundary its own cursor names --
contradicting the function's own documented intent ("the exact consumed cursor
and its sample time").

Fix: the sample time is read from the retained record the cursor names, via
`Worr_CommandStreamCopyRecordV1`. A cursor at sequence 0 is the legitimate
`{epoch,0}` bootstrap and carries no command time. If the named command has
already been reclaimed the seed is dropped rather than published with a wrong
time.

**This fix is not covered by a suite test.** `src/server/user.c` is linked
only into the engine binaries (`server_src`), not into any `networking` suite
executable, so no test in the suite can reach it. It is a small fail-closed
change whose correctness is argued from the surrounding contract, and it
belongs to the runbook's P2 running-game gate. Closing
`t08-end-to-end-seed-virtual-link-gate` -- which would drive the seed through
the production virtual link -- is the item that would make this and the rest
of the seed path provable in-suite, and it remains open.

## Remaining work

The 136 items above other than these three remain open. Nothing here promotes
a feature, changes a default, or closes an acceptance gate; all three fixes
are confined to code that is still default-off.

The single highest-leverage next item is
`t08-end-to-end-seed-virtual-link-gate`: the seed carriage is now admissible
but still has no end-to-end proof, and the existing
`native_snapshot_production_virtual_link_test` already links the real client,
cgame and server sources needed to build one.
