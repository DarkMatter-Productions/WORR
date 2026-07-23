# FR-10-T04 exact-bundle acceptance parent (2026-07-20)

Task: `FR-10-T04` — negotiated WORR transport envelope and canonical adapters.

Status: **In Progress**. This slice adds a bounded task-level acceptance parent
and corrects the public capability policy documentation. It does not close the
full task.

## Outcome

`tools/networking/run_fr10_t04_acceptance.py` is a schema-versioned, headless
parent over the exact default-off transport bundles. Its manifest is literal:
new child tests or canonical weapon-runner modes cannot silently widen the
claimed scope.

| Lane | Exact offer and readiness binding | Live child used by the full scope |
|---|---:|---|
| Native endpoint disabled | `0x03` | canonical legacy-status child |
| Command | `0x53` | `run_native_shadow_runtime_smoke.py` |
| Event | `0x73` | canonical `blaster-local-action-lease` mode |
| Snapshot | `0x57` | canonical `blaster-native-snapshot-presentation` mode |
| Event + snapshot | `0x77` | canonical `blaster-local-action-lease-combined` mode |

The public offer now uses the same exact bundle as readiness when a default-off
endpoint is armed. A server exposes a native bundle only when the peer offer,
configured bundle, and endpoint readiness match exactly. Every mismatch and
partial intersection returns exact legacy `0x03`. The bundle is frozen for the
connection. Disabled endpoints therefore remain byte-for-byte on the prior
legacy offer.

`tools/networking/capability_test.c` now pins all five exact numeric values for
both the public and existing private/readiness names. It also retains the
cross-product proof that a missing endpoint or differently configured peer
downgrades to `0x03`, and exhausts all 128 currently valid capability-bit
combinations so no partial native offer can be admitted accidentally.

## Parent contract

The focused lane reruns eleven fixed build products and binds every
child to its executable hash, argv hash, stdout hash, and stderr hash:

1. capability policy, downgrade, unknown versions, malformed capability text,
   and connection-epoch failure;
2. envelope fragmentation, MTU boundaries, malformed/unknown envelopes, and
   priority;
3. mixed carrier admission and capacity boundaries;
4. session retention, ACKs, exhaustion, and transactionality;
5. readiness ordering, reconnect, captured-old challenge rejection, deadline,
   and sticky failure;
6. client and server production netchan pilots, including malformed input,
   cancellation, reconnect, combined fairness, and append capacity;
7. canonical command adapter behavior;
8. event production virtual-link loss/reorder/duplicate/corruption behavior;
9. snapshot production virtual-link fragmentation, semantic ACK, quarantine,
   and exactly-once cgame publication; and
10. cgame local-action reconciliation through the complete shared 798-entry
    bound, command/receipt arrival orders, rolling pressure, and exact frontier
    duplicates.

The full lane first hashes the caller-supplied staged runtime closure, copies
the distributable install into a fresh
`.tmp/networking/.../runtime/.install` root, excludes nested `.install`, `.tmp`,
and `crashdump_*.dmp` transient artifacts from that clone, verifies the bound
closure, and runs every real-process child only from the isolated copy. The
parent then verifies that the isolated launchers, engines, renderer, game
modules, package, configuration, and fixture map did not change. All clients
are hidden/input-disabled; the dedicated server never creates a client or
renderer.

The full live validators require:

- legacy mode to keep both clients and both server peers on exact numeric
  public/private `0x03`, with no native endpoint and a valid consumed-command
  cursor;
- command client and server status to report public/private `0x53`, exact-once
  bounded reliable payload delivery, fragment pressure, async ACK wake, and
  harness termination;
- event mode to use only the event cvars, complete a real in-session reconnect,
  and produce positive native authority receipts with zero mismatch, conflict,
  resync, unmatched, or outstanding records;
- snapshot client and server status to report public/private `0x57`, positive
  semantic ACK/release traffic, and positive native cgame presentation with
  zero clock/pair/alignment/sample/event/parity failures;
- combined client and server status to report public/private `0x77`, complete
  the real reconnect, produce exact event receipts, and show snapshot ACK and
  release traffic on the simultaneous lane.

The recorded full-run artifact below is historical canonical child schema v35.
At that point the event-only child did not publish its numeric mask in JSON, so
that artifact correctly labels the row
`fixed-config-plus-native-receipt`; it is not relabeled or rewritten. Current
parent code is pinned to schema v36 and now requires the child to capture both
live client rows and both server-peer rows with exact public/private `0x73`
before it can report `direct-client-and-server-status`. The contract test reads
the child runner's declared schema directly, and full-scope evidence
construction rejects an event row without the direct observation. A later
fresh v36 run now passes that requirement, adds direct numeric legacy `0x03`,
and requires a complete combined-native preflight before gameplay is armed.
The historical v35 artifact remains unchanged. See the ordered-frontier
continuation below and
`docs-dev/fr-10-t04-t08-private-receipt-bounded-lifecycle-2026-07-20.md`.

The user-facing progressive networking guide and client/server cvar references
now list both snapshot-shadow controls, the exact `0x03`/`0x53`/`0x73`/`0x57`/
`0x77` selection policy, connection-frozen fallback behavior, and the explicit
diagnostic-only status. They do not present this slice as transport or release
promotion.

## Evidence

Parser and semantic contract:

```text
meson test -C builddir-win --suite networking \
  network-fr10-t04-partial-acceptance-parser --print-errorlogs
1/1 passed
```

Direct Python parent-contract coverage is 12/12:

```text
python -m unittest tools.networking.test_run_fr10_t04_acceptance
Ran 12 tests ... OK
```

The historical pre-capacity focused parent was exercised successfully against
its ten build products. The durable report is written under
`.tmp/networking/fr10_t04_partial_acceptance.json` (or a caller-selected JSON
path under `.tmp/networking/`). Each run gets a unique sibling `.runs/<run-id>`
tree, so logs and child reports cannot be confused with prior evidence.

The report also hashes the fixed manifest, parent/child source inputs, every
focused binary, every child command and output, and the clean `q2proto/` tree.
It rejects any source, binary, runtime, or `q2proto/` change observed while the
gate is running. Duplicate JSON keys, non-finite values, non-object reports,
unexpected schemas, mask drift, mode drift, visible/input-enabled clients,
missing process termination, and output paths outside `.tmp/networking/` fail
closed.

### Full-stage result and native pre-bind correction

The first full-stage attempt (`20260720T033734.194561Z-31424`) failed closed in
the snapshot lane. Its final presentation row was otherwise healthy, but
reported `native_authority_blocks=4`. Investigation showed that mode 3 had
classified four frames between canonical timeline activation and the engine's
first `cl_worr_native_snapshot_timeline_owned=1` publication as native-
authority failures. Those frames were a startup ownership wait, not a loss of
authority that had already been acquired; the zero-block validator was kept
unchanged.

Production presentation now resolves requested mode 3 through an explicit
ownership phase:

- before the first ownership bind it remains legacy-authoritative and records
  no native-authority block;
- the first positive ownership edge resets the presentation view and telemetry
  and latches native authority;
- a same-stream ownership loss remains latched, fails closed, and increments
  `native_authority_blocks` on every affected frame;
- `InitCGame` explicitly resets the latch at the next connection/map stream, so
  a legitimate new readiness handshake starts in the legacy pre-bind wait.

The pure transition unit covers repeated pre-bind wait, first bind, steady
ownership, post-bind loss, recovery without relatching, and explicit stream
reset. The source contract pins the `InitCGame` reset before canonical timeline
initialization, and loop-audio preparation reuses the resolved per-frame
authority rather than recomputing a mixed frame. Focused validation passed the
compiled render-entity test, source contract, and canonical runtime parser
(`3/3`); the direct source contract is `11/11`.

A fresh full run from child 1 then passed against a newly staged production
runtime:

```text
run_id: 20260720T035616.104289Z-41896
schema: worr.networking.fr10-t04-partial-acceptance-evidence.v1
child schema: worr.networking.canonical-weapon-damage-runtime.v35
result: pass
focused children: 10/10
live children: 4/4
semantic_sha256: a73979dfe68b99388159583d8b4eebfe91ed29c0c0f54ee3a4332b3dc4184706
task_complete: false
```

The exact snapshot `0x57` lane recorded 1,024 native samples/promotions and
1,024 renderer submissions with zero native-authority, clock, pair, alignment,
sample, parity, event-audit, or enumeration failures. Its snapshot peer
recorded 60 semantic ACKs and 90 releases with zero queue failure, rejection,
or retirement fault. The combined `0x77` lane completed the real reconnect
with three admissions and two shooter serverdata packets; its two snapshot
peers recorded 419 ACKs and 442 releases with the same zero-fault conditions.
The isolated runtime and all bound source inputs remained byte-identical for
the run. The `q2proto/` digest remained
`389e70575f3e2e97f468075a20be137feaa3e735b24ac4f155c8228ec125d7ac`.

### Schema-v36 ordered-frontier continuation

The subsequent private-receipt lifecycle work split ordered private
reconciliation from snapshot-fenced presentation, made callback rejection and
mutation reentry fail closed, preserved `UINT32_MAX` as a valid terminal event
ID, added monotonic server mailbox failure/drain, retained exact controlled-
entity generation provenance across delta/omission/keyframe cases, and pruned
healthy matched command/receipt pairs behind a newer receipt frontier. The
combined runner now proves both real clients, both server peers, and both
snapshot peers healthy on exact `0x77` before it arms gameplay.

The fresh combined child
`.tmp/networking/fr10_t04_combined_terminal_prune_current.json` passes schema
v36 with normal 15/15 Blaster damage, 29/29 exact local-action receipt matches,
zero unmatched/outstanding/mismatch/conflict/resync, and a complete zero-fault
combined preflight.

The pre-capacity full parent
`.tmp/networking/fr10_t04_ordered_frontier_full.json` records:

```text
run_id: 20260720T080507.527435Z-22372
result/status: pass / partial
focused children: 10/10
live children: 5/5
live masks: 0x03, 0x53, 0x73, 0x57, 0x77
semantic_sha256: 521cdaabad088cb6e89b0ad56d89cf2dcf2bcb8194ec69d3ed078b61a84a32a7
task_complete: false
```

All five live rows have direct client/server numeric-mask observation. The
runtime, source inputs, and read-only `q2proto/` tree remained stable.

A follow-on capacity correction derives 798 simultaneous legitimate
command/receipt states from 127 client-pending commands, 64 retained event TX
slots, 512 event-backlog rows, 63 selectively acknowledged successors, and the
32-entry server mailbox, and selects a fixed 1,024-entry cgame table. The two
artifacts above predate that capacity and are not relabeled as its evidence.
The derived assertions, command-first and receipt-first 798-entry tests,
more-than-two-table rolling streams, six focused Meson rows, and production
cgame build pass. The manifest now carries 11 focused children and hashes the
shared bound inputs.

The first post-stage attempt failed only because the command smoke included
about 4.8 seconds of process-tree teardown in its evidence elapsed time after
both trials had already completed semantically. The corrected runner excludes
teardown from the bounded evidence clock, retains the deadline check after live
grace, and joins the affected runner/parser contracts in a 117/117 Python pass.

The final post-capacity parent
`.tmp/networking/fr10_t04_bounded_lifecycle_full_retry.json` records:

```text
run_id: 20260720T083347.156487Z-45280
result/status: pass / partial
focused children: 11/11
live children: 5/5
live masks: 0x03, 0x53, 0x73, 0x57, 0x77
combined: 15/15 damage, 30/30 receipts, zero reconciliation faults,
          preflight and post-fire proof present
command evidence seconds: 42.703 / 42.719 (87.890 overall)
semantic_sha256: f4a161bf0876e05db21499ab5c21399cc0ef87ae4881ab78ae764bc67b6789ad
task_complete: false
```

The six production targets build and the refreshed/validated stage contains 16
root runtime files, one dependency, and 601 packaged assets. Every final parent
gate is true except the deliberate task-completion gate. Runtime/source hashes
remain stable and the read-only `q2proto/` digest remains
`389e70575f3e2e97f468075a20be137feaa3e735b24ac4f155c8228ec125d7ac`.
The full derivation and evidence boundary are recorded in
`docs-dev/fr-10-t04-t08-private-receipt-bounded-lifecycle-2026-07-20.md`.

## Why FR-10-T04 remains open

Passing this parent proves the bounded exact-bundle slice; it is not the whole
Definition of Done. The following remain open:

- the supported legacy demo/MVD/relay matrix beyond the direct live `0x03`
  status row;
- complete game-service event-family adapters;
- native authority promotion beyond default-off shadow operation;
- multi-client fairness and the full deterministic impairment matrix;
- ACK-exhaustion and map-rotation breadth for every live bundle;
- sustained load, soak, cross-platform, rollout, and release evidence.

Accordingly, both machine evidence and this document set `task_complete=false`.
No file under `q2proto/` changed.
