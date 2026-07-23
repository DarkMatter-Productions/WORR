# FR-10-T04/T08 private receipt bounded lifecycle

Date: 2026-07-20

Project tasks: `FR-10-T04` and `FR-10-T08`, with supporting evidence for
`FR-10-T05`, `FR-10-T06`, `FR-10-T07`, and `FR-10-T14`.

Status: **In Progress**. This continuation hardens the private local-action
receipt, ordered event application, and combined snapshot/event evidence path.
It does not move gameplay or presentation authority and does not close either
task. FR-10 remains **8/16 complete**.

## Problem and failure model

The descriptor-complete local-action receipt is reliable and ordered at the
native event layer, but cgame still has to correlate it with a separately
retained canonical command. Those two records can reach the correlation owner
in either order. The former implementation treated its 128 slots as a static
set: even exact command/receipt pairs remained resident after successful
reconciliation. A sustained combined `0x77` session could therefore exhaust
the table on healthy, already-terminal history. The observed failure occurred
at private event 138 after 137 exact matches, caused native event resync, and
dropped the shooter before the fixture could fire.

The correction separates four concepts which must not share one implicit
cursor or eviction rule:

1. ordered native event admission;
2. ordered private reconciliation application;
3. snapshot-fenced visual presentation; and
4. bounded command/receipt evidence retention.

An absent receipt is not negative authority. A received receipt is authority
evidence and may be discarded only after exact reconciliation, an explicit
epoch reset, or a fail-closed error.

## Ordered private reconciliation cursor

The cgame event runtime now owns
`next_private_reconciliation_sequence` independently of the ordinary
`next_authority_sequence` presentation cursor. After an authority batch is
transactionally admitted, the private cursor walks contiguous admitted event
IDs in event order:

- a private local-interaction or descriptor-complete local-action receipt is
  applied exactly at its event-ID boundary;
- a visual/non-private record is a legal gap for this cursor, so the private
  cursor crosses it without marking its journal slot presented, consuming its
  snapshot fence, or advancing the independent presentation cursor; and
- a missing event ID pins the private cursor until the gap is admitted.

This prevents a visually blocked event from indefinitely delaying later
private command reconciliation while preserving the original presentation
order. A private receipt becomes terminal/reclaimable at the presentation
cursor only after the exact journal resident is still proven and the private
callback has committed.

Private resolver and reporting callbacks are irreversible boundaries. If a
receipt callback rejects, the runtime marks current authority degraded and
resync-required, leaves the private cursor pinned, does not advance visual
presentation, and requires a fresh authority epoch. During either a private
callback or presenter callback, all runtime-mutating entry points and callback
replacement are rejected as reentrant. Read-only status inspection remains
available so diagnostics can observe the exact boundary. This prevents a
callback from resetting an epoch, replacing itself, admitting another record,
or advancing a cursor underneath the transaction.

`UINT32_MAX` remains a valid final event sequence. Both cursors now have an
explicit exhausted bit. Consuming the final record pins the numeric cursor at
`UINT32_MAX` and sets its exhausted bit; the bit distinguishes a consumed
terminal sequence from an unconsumed record whose ID is also `UINT32_MAX`.
Only a fresh authority epoch clears the exhausted state. This avoids integer
wrap to event sequence zero and prevents double application of the terminal
record.

## Bounded local command/receipt evidence

### No-receipt command rolling

Cgame may observe many canonical commands for which sgame publishes no private
receipt. Those command-only rows are speculative correlation opportunities,
not authority evidence. When bounded storage is under command pressure, the
oldest command-only row can roll forward without requesting resync. Telemetry
records the evicted command coverage frontier.

If a receipt later arrives behind that lost coverage, cgame first asks the
exact V2 command-history resolver for the command. A still-retained exact
record can reconstruct the comparison. If the real receipt is behind the
recorded coverage-loss frontier *and* exact V2 history is no longer available,
cgame records a coverage-loss/expiration failure and requires resync. It never
turns the prior absence of a receipt into a false mismatch, and it never
silently accepts a real late receipt whose hash can no longer be proved.

### Monotonic receipt frontier and terminal pruning

A valid receipt command ID advances a same-epoch monotonic receipt frontier.
An exact duplicate of the latest frontier is idempotent because the complete
receipt bytes remain retained separately. A regression, epoch conflict, or
same-ID byte conflict fails closed.

Before inserting a strictly newer receipt, cgame prunes lower command-only
rows and lower exact command-plus-receipt rows which are already terminal.
Receipt-only rows are deliberately retained because their local command may
still arrive and must be reconciled. This rule eliminated the observed event
138 failure: healthy matched history no longer consumes one permanent table
slot per command. Telemetry distinguishes command-frontier prunes,
terminal-frontier prunes, receipt-frontier advances, command-cache evictions,
and actual coverage-loss failures.

## Server mailbox authority and explicit failure drain

The server-side per-client mailbox now retains a monotonic command frontier
after FIFO consumption. An exact duplicate remains successful and does not
enqueue a second record. Invalid input, epoch change, command regression,
same-ID conflict, mailbox capacity, and publish-order exhaustion each latch a
typed first failure. Once failed, that client's mailbox rejects further
publication until client or map reset; taking the notification does not
unpoison the mailbox.

`SV_ClientThink` consumes the failure notification immediately after the real
sgame `ClientThink` callback, which is the boundary at which a receipt can be
published. It disables and drains that client's native peer with
`SV_NATIVE_SHADOW_FAILURE_LOCAL_ACTION_AUTHORITY` (`18`). A source-contract
gate follows the whole chain from the sgame publish import, through the typed
mailbox latch, to the post-callback server consume and native failure/drain.
Overflow or contradictory authority can therefore no longer degrade into a
silent missing receipt.

The mailbox remains a fixed 32-entry per-client owner and resets on the
existing map/client/reconnect/slot-reuse boundaries. Legacy gameplay and the
already-accepted legacy message remain authoritative throughout.

## Controlled snapshot-generation provenance

The first combined rerun after the receipt/runtime changes exposed an
independent snapshot-sender rejection: the controlled entity's generation was
correct but its exact provenance record had been regenerated rather than
retained. Snapshot V2 publication now carries the previous exact generation
record for the same index and lifecycle across:

- unchanged deltas;
- explicit controlled-entity updates;
- a controlled entity omitted from the observer's entity range;
- omitted-to-first-carrier transitions; and
- generation-only keyframes whose parent carries the prior provenance.

The base entity and base player records must agree on index, generation, and
provenance. A contradiction rejects before publication. Scratch generation is
normalized before entity, player, endpoint, and snapshot hashes are computed,
so the final player and entity views bind the same lifecycle evidence. This
keeps the first-person controlled entity valid even when it is legitimately
omitted from an observer's entity list.

## Combined live preflight and runner oracle

The schema-v36 canonical runner now performs a full combined-native preflight
after reconnect/team setup and before arming gameplay. It uses the actual live
user IDs and requires:

- both clients to publish exact public/private `0x77` and ACTIVE readiness;
- both server slots to publish exact public/private `0x77`, ACTIVE lifecycle,
  zero failures, and committed command proof;
- both snapshot peers to show positive semantic acknowledgement/release; and
- no terminal client/server native failure row.

The post-fire combined proof remains separate, so startup health cannot stand
in for gameplay-time liveness. The runner also retains status RCON responses in
the artifact sink, schedules `-attack` only when the selected mode owns an
explicit release key, releases a held attack on terminal fixture state, emits
the core failure tuple, and performs a final liveness check.

The command-lane smoke records its evidence elapsed time after the live grace
condition but before process-tree teardown. Teardown duration is excluded from
that bounded evidence clock and cannot turn timely native evidence into a false
deadline failure; completing the live grace after the declared trial deadline
still fails. Deterministic runner timing coverage passes 35/35 directly plus
the primary parser and repeated-runtime Meson consumers.

## Derived correlation bound: 798 required, 1,024 selected

Terminal pruning fixes the observed healthy-history leak, but 128 entries are
not a valid compositional bound for all simultaneously legitimate in-flight
command/receipt states. The required bound is derived from the public fixed
capacities rather than from one normal run:

| Contributor | Bound |
|---|---:|
| Client canonical commands pending behind the consumed cursor (`WORR_CGAME_PREDICTION_INPUT_CAPACITY - 1`) | 127 |
| Native event sender retained TX slots | 64 |
| Native event sender backlog | 512 |
| Selectively acknowledged successors behind one missing event (`WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY - 1`) | 63 |
| Server per-client authority mailbox | 32 |
| **Required simultaneous evidence bound** | **798** |

The chosen process-local table capacity is the next power of two, **1,024**,
leaving 226 entries of slack while remaining fixed-capacity and allocation
free. Source assertions now calculate 798 from the shared prediction-input,
event-sender TX/backlog, selective-receipt, and mailbox constants, pin the
current result, and require the 1,024-entry table to cover it. A future bound
increase therefore fails the build instead of silently invalidating the table.
Reaching the 1,024-entry hard limit still fails closed; the larger bound is not
permission to evict unmatched receipt evidence.

`WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY` and
`WORR_LOCAL_ACTION_SHADOW_AUTHORITY_MAILBOX_CAPACITY` are shared public
constants, and the selective capacity is statically checked against the
receipt mask width. `CG_LOCAL_ACTION_SHADOW_EVIDENCE_CAPACITY` is the public
cgame local-action bound. The independent legacy Hook/local-interaction pair
table remains separately named and capped at one 128-command prediction ring;
the 1,024-entry change does not silently widen that different owner.

The capacity implementation and focused evidence have landed. Direct tests
cover command-first retention through all 798 entries, receipt-first retention
and later exact joins through all 798 entries with engine history unavailable,
more than two complete 1,024-entry command-only rolling windows, and a
more-than-two-table exact receipt stream with terminal pruning and duplicate
frontier checks. The production cgame target and six focused Meson rows pass.
The T04 parent manifest now adds the cgame correlation executable as its
eleventh focused child and hashes the shared prediction-ring, event-sender,
event-ABI, and mailbox inputs used by the derivation.

Focused verification:

```text
meson compile -C builddir-win cgame_local_interaction_test \
  local_action_shadow_authority_test native_event_sender_test cgame_x86_64
pass

.\builddir-win\cgame_local_interaction_test.exe
cgame_local_interaction_test: ok

meson test -C builddir-win --print-errorlogs \
  network-cgame-local-interaction \
  network-local-action-shadow-authority \
  network-server-local-action-authority-failure-source-contract \
  network-native-event-sender \
  network-cgame-event-runtime \
  network-native-event-virtual-link
6/6 passed
```

Integrated verification is now complete for this bounded slice. The six
production targets build, `.install/` is refreshed and validated with 16 root
runtime files, one dependency, and 601 packaged assets, and the hardened
11-child parent passes from the isolated post-change stage.

The first post-stage parent attempt is retained only as a failure diagnostic:
both command trials completed semantically before their deadlines, but the
runner incorrectly included about 4.8 seconds of Windows process-tree teardown
in its evidence clock. The corrected oracle records evidence elapsed after the
live grace and before teardown, still rejects grace completion beyond the trial
deadline, and passes the affected Python runner contracts 117/117. The final
command trials record 42.703 and 42.719 seconds of evidence time, with 87.890
seconds overall.

## Implementation and test surface

The bounded lifecycle is split across the following owners:

- `src/game/cgame/cg_event_runtime.cpp` and `.hpp`: ordered private cursor,
  exhausted bits, callback/mutation guard, fail-closed private application;
- `src/game/cgame/cg_local_interaction.cpp` and `.hpp`: no-receipt rolling,
  exact-history recovery, coverage-loss failure, monotonic full-receipt
  frontier, terminal pruning, 798 derivation, and 1,024-entry table;
- `inc/shared/event_abi.h`, `inc/shared/local_action_shadow.h`, and
  `src/common/net/event_abi.c`: shared selective-window/mailbox bounds and
  width assertions;
- `inc/server/local_action_shadow_authority.h`,
  `src/server/local_action_shadow_authority.c`, `inc/server/native_shadow.h`,
  and `src/server/user.c`: monotonic mailbox, typed failure latch, and native
  peer failure/drain;
- `src/common/net/snapshot_q2proto.cpp`: controlled-generation provenance;
- `tools/networking/run_canonical_rail_damage_runtime_gate.py` and
  `tools/networking/run_fr10_t04_acceptance.py`: combined preflight, terminal
  failure oracle, source/runtime closure, transient-artifact-free isolated
  runtime clone, and 11-child parent manifest; and
- the focused cgame runtime/local-action, server mailbox/source-contract,
  snapshot projection/wire, native sender/link, and Python parent/child tests.

No file under `q2proto/` is part of this surface.

## Current evidence

The fresh combined lifecycle artifact is
`.tmp/networking/fr10_t04_combined_terminal_prune_current.json`:

```text
run_id: 20260720T080350.397799Z-39652
schema: worr.networking.canonical-weapon-damage-runtime.v36
mode: blaster-local-action-lease-combined
result: pass
damage: 15/15
local action parity: 29 receipts, 29 matches, 0 unmatched,
                     0 outstanding, 0 mismatch, 0 conflict, 0 resync
combined preflight: both clients and both server peers exact 0x77;
                    both snapshot peers acknowledge/release; zero failures
```

The pre-capacity exact-bundle parent is
`.tmp/networking/fr10_t04_ordered_frontier_full.json`:

```text
run_id: 20260720T080507.527435Z-22372
schema: worr.networking.fr10-t04-partial-acceptance-evidence.v1
scope/result/status: full / pass / partial
focused children: 10/10
live children: 5/5 (0x03, 0x53, 0x73, 0x57, 0x77)
semantic_sha256: 521cdaabad088cb6e89b0ad56d89cf2dcf2bcb8194ec69d3ed078b61a84a32a7
task_complete: false
```

That parent directly observes numeric client/server masks for all five lanes,
holds the isolated staged runtime and source inputs stable, and confirms the
`q2proto/` tree is unchanged. Together with the standalone combined artifact,
it verifies the ordered runtime, terminal pruning, controlled-generation
provenance, combined preflight, and exact-bundle slice at its recorded binaries.
It predates the 1,024-entry capacity and 11-child manifest and is not relabeled
as their proof.

The final post-capacity integrated evidence is
`.tmp/networking/fr10_t04_bounded_lifecycle_full_retry.json`:

```text
run_id: 20260720T083347.156487Z-45280
schema: worr.networking.fr10-t04-partial-acceptance-evidence.v1
scope/result/status: full / pass / partial
focused children: 11/11
live children: 5/5 (0x03, 0x53, 0x73, 0x57, 0x77)
combined child: 15/15 damage; 30 receipts, 30 matches;
                0 unmatched/outstanding/mismatch/conflict/resync;
                preflight and post-fire proof present
semantic_sha256: f4a161bf0876e05db21499ab5c21399cc0ef87ae4881ab78ae764bc67b6789ad
task_complete: false
```

Every parent gate is true except the deliberate `task_complete=false`. The
final source closure includes the shared constants and local-action test behind
the derived bound; the isolated runtime/source hashes remain stable; and the
read-only `q2proto/` digest remains
`389e70575f3e2e97f468075a20be137feaa3e735b24ac4f155c8228ec125d7ac`.

Focused implementation coverage includes the cgame local-action owner, cgame
event runtime/export/owner, native event virtual link, server local-action
mailbox, server publish-to-drain source contract, snapshot projection/wire, and
server snapshot-shadow rows. All automated process evidence remains headless,
input-disabled, and isolated under `.tmp/networking/`.

## Scope and remaining work

This slice does not predict any of the 22 ordinary weapon families, suppress
or present audiovisual effects, move legacy gameplay authority, enable native
transport by default, add a demo format, or modify `q2proto/`. It also does not
provide the complete real-socket fault matrix, sustained multi-client load,
supported-platform coverage, soak, rollout, or release floors.

Consequently `FR-10-T04` and `FR-10-T08` remain **In Progress**, the T04 parent
remains deliberately `task_complete=false`, and the snapshot/netcode plan
remains **8/16 complete**.
