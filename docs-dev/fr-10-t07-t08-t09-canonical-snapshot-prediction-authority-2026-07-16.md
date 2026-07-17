# FR-10 T07/T08/T09 canonical snapshot prediction authority

Date: 2026-07-16

Primary project tasks: `FR-10-T07`, `FR-10-T08`, and `FR-10-T09`.

Validation and safety task: `FR-10-T14`.

Status: implemented as a default-off prediction-authority bridge with audit
and explicit promotion modes. This increment advances all four tasks but
closes none of them.

## Outcome

WORR cgame prediction can now resolve one exact, immutable canonical snapshot
and the exact server-consumed command cursor carried by that snapshot as a
single prediction-authority transaction.

The previous prediction path seeded correction and replay from mutable
`cl.frame.ps` state while independently resolving pending commands from the
current client frame. That remains the default compatibility path. The new
bridge adds a stronger option: cgame copies the exact retained canonical
snapshot/player pair by value, asks the engine for a value-only input range
starting at that snapshot's consumed cursor, and admits the pair through a
pure fail-closed selector before either state can drive replay.

This is a progressive hybrid architecture:

- the established legacy path remains the default and compatibility
  authority;
- audit mode evaluates canonical authority alongside the legacy result
  without changing gameplay;
- promotion mode uses canonical player state and cursor-bound input only
  after every identity, provenance, schema, hash, and alignment check passes;
  and
- cgame receives immutable value records through versioned extension ABIs
  rather than borrowing engine-owned timeline pointers.

The design preserves the useful idTech3-style separation between engine
transport/state ownership and cgame prediction policy while using explicit
modern value ABIs, generations, epochs, hashes, and classified recovery
instead of shared mutable snapshot structures.

## Authority flow

The prediction transaction is:

```text
admitted canonical snapshot
    -> cgame canonical timeline publication
    -> exact sequence lookup and generation-checked value copy
    -> immutable snapshot + player bundle
    -> V2 input request using that snapshot's consumed cursor
    -> canonical-only value-copy command range
    -> pure prediction-authority selector
    -> audit comparison or promoted correction/replay
```

The implementation is divided deliberately:

- `inc/shared/cgame_prediction.h` defines
  `WORR_CGAME_PREDICTION_INPUT_IMPORT_V2`. Its request contains the exact
  `worr_snapshot_consumed_command_v2` copied from the selected snapshot and a
  canonical-required flag.
- `src/client/cgame.cpp` implements the engine-side V2 resolver. It validates
  the requested cursor and canonical metadata, copies the bounded command
  range by value, and never substitutes packet acknowledgement when canonical
  authority was requested.
- `src/game/cgame/cg_canonical_snapshot_timeline.hpp` and `.cpp` provide
  `CG_CanonicalSnapshotTimelineCopyPredictionSnapshot`. The helper finds one
  exact sequence in the active epoch and copies the snapshot and player
  records under a generation-checked timeline reference.
- `src/game/cgame/cg_prediction_authority.hpp` and `.cpp` implement the pure
  selector. The selector has no client globals and returns a classified result
  plus the accepted value copies.
- `src/game/cgame/cg_predict.cpp` integrates the selector into angle
  prediction, correction, movement replay, hard resynchronization, and
  telemetry.
- `src/game/cgame/cg_snapshot_timeline.hpp` and `.cpp` expose machine-readable
  correction reasons for unavailable authority, mismatched authority, and
  snapshot discontinuity in addition to the existing input/replay failures.

## Default-off audit and promotion control

`cg_prediction_snapshot_authority` is a non-archived cgame cvar. It defaults
to `0` and is intentionally not a public promotion decision.

| Value | Mode | Behavior |
|---:|---|---|
| `0` | Legacy | Retains the established legacy frame and V1 input-resolution path. |
| `1` | Audit | Resolves canonical authority and compares it with the legacy movement, view, blend, cursor, and replay range, but legacy remains authoritative. |
| `2` | Promote | Uses canonical snapshot/player state and its exact V2 cursor-bound input range. Any unavailable or mismatched authority blocks replay and hard-resynchronizes. |

Audit telemetry records attempts, canonical selections, unavailable results,
matches, mismatches, blocked promotions, and the last classified selector
result. Developer output is rate limited.

The cvar is `CVAR_NOARCHIVE` so an experimental promotion cannot silently
become a durable user configuration. Any future default change requires the
remaining corpus, impairment, compatibility, and operational promotion gates.

## Exact immutable snapshot rules

A candidate can become prediction authority only when all of these conditions
hold:

1. The timeline is active, its clock reset has resolved, and exactly one
   retained slot matches the requested snapshot sequence.
2. The copied timeline reference is non-absent and generation-valid before
   and after copying.
3. The wrapper schema is current, its active epoch is nonzero, and the copied
   snapshot belongs to that exact epoch.
4. The snapshot is complete and promotion eligible. A transport-truncated
   snapshot is rejected explicitly.
5. Snapshot and player schemas, component masks, generations, ranges, and
   hashes validate.
6. Snapshot sequence, authoritative server tick, canonical server time, and
   controlled entity match the exact legacy frame being reconciled.
7. Snapshot and player controlled-entity identity, generation, and provenance
   are identical.
8. The consumed cursor has `SERVER_CONSUMED` provenance, a valid nonzero
   command epoch, and no reserved metadata.
9. The V2 input result names `CANONICAL_CURSOR` as its source, carries the
   canonical flag, contains no legacy-fallback flag, and repeats the selected
   snapshot's consumed cursor exactly.
10. Finalized replay commands are continuous in both legacy ring sequence and
    canonical command identity. A pending command, when present, is explicitly
    noncanonical and follows the current finalized sequence.

The current cgame alignment uses:

- snapshot sequence `cl.frame.number + 1`;
- authoritative server tick `cl.frame.number`;
- the frame's separately retained canonical server time; and
- controlled entity `cl.frame.clientNum + 1`.

These fields are checked rather than inferred from render time or packet
acknowledgement. A stale, future, wrong-epoch, wrong-tick, wrong-time, or
wrong-entity copy cannot seed prediction.

## Canonical state used by promotion

After selection, promotion seeds prediction from the copied canonical player
record:

- movement origin, velocity, type, flags, delta angles, gravity, and view
  height;
- view angles and view offset;
- screen blend and render flags; and
- controlled entity identity.

Replay starts from the selected snapshot's movement state and executes only
the accepted cursor-bound command range. Correction comparison also uses that
same canonical movement state. This removes the earlier split where correction
could use one mutable frame state while replay range ownership came from
another observation.

## Fail-closed behavior

The selector returns classified failures for:

- invalid arguments;
- unavailable timeline state;
- invalid, incomplete, misaligned, or truncated snapshots;
- controlled-entity mismatch;
- consumed-cursor mismatch; and
- invalid input ranges.

In audit mode, any canonical failure or mismatch is observable but cannot
change gameplay because legacy authority remains active.

In promotion mode, failure never falls back to legacy command replay. Cgame
marks the canonical request blocked, clears every retained prediction ring,
and applies the currently visible authoritative frame as a safe presentation
seed. It records either `canonical_authority_unavailable` or
`canonical_authority_mismatch`.

The hard-resync path clears:

- retained command sequences and prediction states;
- predicted origins;
- state, collision, configuration, and replay-chain hashes;
- visual prediction error;
- step state; and
- cached ground entity and plane state.

Missing retained state, movement-configuration discontinuity, and replay-step
rejection continue to use the same complete recovery path.

Accepted canonical discontinuities that represent an initial snapshot,
sequence gap, base jump, fragment stall, map reset, demo rewind, hard resync,
or observer attach reset prediction history once for that exact snapshot.
Transport truncation is present in the defensive reset mask but is rejected
before promotion. A full-snapshot marker or rate suppression alone does not
force a prediction reset.

## Production-path evidence

`native_snapshot_production_virtual_link_test` now propagates a deterministic
server-consumed cursor through the real production-shaped fixture:

```text
frame carrier
    -> server snapshot-shadow input
    -> encoded native snapshot transaction
    -> client receiver and semantic admission
    -> client legacy-shadow expectation
    -> cgame canonical timeline
    -> exact prediction snapshot copy
    -> canonical prediction-authority selector
```

After each of four successful cgame admissions, the test:

- copies the exact retained snapshot/player bundle;
- creates a canonical V2 input range from the copied consumed cursor;
- selects it as canonical prediction authority;
- verifies snapshot ID, server tick, server time, consumed cursor, player
  value, and all snapshot component/final hashes; and
- increments `prediction_ready` exactly once.

Zero pending replay commands are sufficient for this production bridge test:
the purpose is to prove that a real admitted snapshot becomes exact
prediction authority without reconstructing identity from packet ACK. The
standalone selector test separately covers nonempty continuous replay ranges.

The deterministic production result is:

```text
native_snapshot_production_virtual_link_test: ok
fragmented=8 s2c_loss=1 reordered=13 duplicates=1
ack_loss=1 repeat_revalidate=1 cgame_once=4
hash_quarantine=1 wrong_epoch=1 real_domains=4
expectation_rollovers=1 timeout_recoveries=1
prediction_ready=4 digest=7176afa3d4eb62b2
```

The existing corruption, incomplete-fragment, duplicate, hash-mismatch,
wrong-epoch, expectation-window, complete-timeout, ACK-loss, and retained
payload-release scenarios remain active around the new authority assertions.

## Focused validation

The focused prediction contract passed 4/4:

```text
network-prediction-input-layout-c
network-prediction-input-layout-cpp
network-cgame-prediction-snapshot-authority
network-cgame-prediction-fail-closed-contract

4/4 passed; 0 failed
```

The standalone authority test covers:

- exact successful selection and value preservation;
- null arguments and unavailable timeline references;
- invalid hashes and incomplete/non-promotable snapshots;
- stale/future sequence, wrong tick/time, and wrong active epoch;
- transport truncation;
- controlled-entity index and generation mismatch;
- absent, non-server, or mismatched consumed cursors;
- malformed canonical input ranges; and
- discontinuities that require prediction-history reset.

The production virtual-link target passed separately:

```text
network-native-snapshot-production-virtual-link

1/1 passed; 0 failed
```

The post-integration repository validation also passed:

```text
complete networking suite:       147/147 passed
full production compile:         passed
packaging/stager policy:         16/16 passed
release headless policy:         1/1 passed
Windows x86-64 .install refresh: validated
```

The refreshed stage contains 16 root runtime files, one root dependency, a
483-member `basew/pak0.pkz`, one q2aas reference map, 31 botfile
package/loose assets, and 215 RmlUi package/loose assets.

All validation was headless and input-free under `FR-10-T14`. The tests are
in-process executables or source-contract checks. They create no renderer,
window, client input backend, or mouse capture, and no interactive game client
was launched.

## Limitations and next gate

This increment does not complete `FR-10-T07`, `FR-10-T08`, or `FR-10-T09`.
In particular:

- canonical snapshot prediction remains default-off;
- the production bridge proves exact authority availability with a
  zero-command range, not sustained full replay under long impairment;
- the public native snapshot capability and compatibility authority remain
  unchanged;
- legacy Q2 server/demo compatibility still requires the established legacy
  path;
- weapon/action prediction, presentation deduplication, and full correction
  budgets remain open under `FR-10-T08`;
- command-identity promotion and long-horizon exhaustion/reset evidence remain
  open under `FR-10-T09`; and
- no interactive visual validation is claimed by these headless tests.

The next promotion gate is a deterministic 100,000-frame serialized
production corpus through the actual path:

```text
final server projection
    -> native snapshot codec
    -> WNE1 fragmentation
    -> WTC1 carrier
    -> deterministic loss/reorder/duplicate impairment
    -> native receiver and semantic admission
    -> cgame timeline
    -> canonical prediction authority and replay
    -> semantic ACK
    -> server retained-payload release
```

That corpus must prove reproducible digests, bounded storage, exact-once
admission/authority, correct quarantine, no ACK before semantic authority, no
incorrect retained-payload release, cursor continuity, deterministic replay,
and classified recovery across epoch, map, discontinuity, and command-history
boundaries. Audit-mode parity evidence from that corpus is required before
considering promotion mode as a default.

## Roadmap accounting

No project task closes from this increment.

- Overall roadmap: **74/190 complete (38.9%)**, **116 open**.
- FR-10 roadmap: **3/16 complete (18.75%)**, **13 open**.
- Round closure delta: **0**.

`FR-10-T07`, `FR-10-T08`, `FR-10-T09`, and `FR-10-T14` all gain
implementation or validation evidence, but remain open.
