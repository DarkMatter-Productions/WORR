# FR-10 Serialized Production Snapshot Corpus

Date: 2026-07-17

Tasks: `FR-10-T04`, `FR-10-T06`, `FR-10-T07`, `FR-10-T08`,
`FR-10-T09`, `FR-10-T14`

Status: the deterministic 100,000-frame serialized production-path corpus is
accepted. All six parent tasks remain Incomplete because their broader
Definitions of Done and release gates are not satisfied.

## Purpose

This milestone scales the default-off private native snapshot path from four
focused production-hook admissions to a fixed-seed corpus. It proves that one
canonical snapshot model can travel through the existing production ownership
boundaries, survive deterministic transport impairment, become immutable cgame
prediction authority, and release retained server payload only after a semantic
acknowledgement.

It does not enable public native snapshot authority or alter `q2proto/`.

## Exercised path

Every requested positive frame traverses this in-process production path:

1. The server snapshot shadow captures and commits an exact final-emission
   projection.
2. The bounded native snapshot sender serializes the complete reconstructed
   view as WNC1, fragments it through WNE1, and carries it in WTC1 DATA.
3. The production `NetImpair` model decides latency, jitter, loss, burst loss,
   reorder, duplication, and upstream acknowledgement stall behavior.
4. The real client native snapshot receiver reassembles DATA and defers
   semantic admission until its exact independently reconstructed legacy
   expectation exists.
5. Admission verifies snapshot identity and the endpoint, legacy-parity,
   player, entity, area, and event proof domains before the private native
   receiver publishes to the sole canonical cgame timeline owner for that
   epoch.
6. A generation-checked timeline copy-out supplies the retained snapshot and
   player value. The prediction-input resolver validates a cursor-bound
   canonical command range, and the pure prediction-authority selector binds
   the two transactions.
7. Only the fresh cgame semantic receipt authorizes the reverse WTC1 ACK. The
   server releases the retained payload only after that exact acknowledgement.

The corpus rotates through four connection, transport, snapshot, and command
epochs. Each epoch includes a valid `{epoch, 0}` prediction bootstrap range;
99,996 later ranges use a nonzero server-consumed cursor.

## Defects exposed and fixed

The larger corpus exposed two prediction-boundary defects:

- the first command after a valid `{epoch, 0}` consumed cursor must be derived
  with `Worr_CommandCursorNextIdV1`; treating the cursor as an already-existing
  command ID rejects the legal bootstrap;
- prediction discontinuities must be consumed before the
  `PMF_NO_PREDICTION` early return so reset-required snapshots clear retained
  prediction history even while movement prediction is disabled.

Focused source and runtime contracts retain both fixes.

## Deterministic corpus contract

The checked-in scenario is
`tools/networking/scenarios/native_snapshot_production_corpus.json`. It fixes:

- 100,000 requested positive frames;
- seed `1369948382` (`0x51a7c0de`);
- four activated epochs;
- two complete executions;
- golden corpus digest `c6aee48df85341ab`;
- three additional corruption probes, separate from the positive-frame count;
- headless execution through `tools/networking/headless_process.py`, with no
  visible window, no stdin, no client input initialization, and no mouse
  capture.

The runner rejects a changed schema, count, seed, normalized result, digest,
coverage field, or exact accounting invariant. It requires a real lowercase
golden, removes stale evidence before an attempt, confines output below
`.tmp/`, and publishes the final document atomically. Eleven focused runner
tests cover strict JSON, mutated counters, path/link safety, stale evidence,
atomic publication, and the recorded headless policy without launching the
corpus. Evidence is written to
`.tmp/networking/native_snapshot_production_corpus/evidence.json` under schema
`worr.native_snapshot_production_corpus.evidence.v1`.

Both executions produced normalized JSON SHA-256
`a35973b39947387d7454a45650d5f9489e0ed136158e55177d9770c904444d38` and
corpus digest `c6aee48df85341ab`. The manifest SHA-256 is
`b3307f9c8fdce1fbee9bf4250f7dcebf1431c2d3353880d809e21926831ab385`;
the qualified executable SHA-256 is
`a5222206e72783027b6ebed50ea77441a3a86e8acb0995cdae0a4a1da650a3a3`, and
the qualifying runner SHA-256 is
`6f5b5708a77c7030fb5b5c2dc2673daf37c35892e7443cdc19f48e9a93ea3a7f`.

## Accepted coverage

The positive path records exact terminal equality:

- requested, accepted, acknowledged, released, and selected prediction
  authorities: `100000` each;
- serialized views: `100003`, comprising the 100,000 requested positives and
  three corruption probes;
- accepted positive abandonment: `0`;
- resolved prediction commands: `250500`;
- pending ranges: `33334`; non-pending ranges: `66666`;
- bootstrap ranges: `4`; nonzero-cursor ranges: `99996`;
- 127-command limit ranges: `4`; maximum replay length: `127`;
- deliberate 128-command exhaustion rejections: `1`;
- authority history resets: `43`;
- exact-once checks: `115602`;
- premature-ACK checks: `25805`; premature-release checks: `141407`.

Transport and scheduling coverage records:

- server transmissions: `139750`; server packets: `279500` and
  `224857750` bytes;
- server-to-client deliveries: `254295` and `204635610` bytes;
- client-to-server deliveries: `104943` and `5037264` bytes;
- server-to-client impairment decisions: `279498`; client-to-server
  decisions: `115602`;
- server-to-client losses: `37355`; burst losses: `34650`;
- within-burst fragment release inversions: `55448`;
- duplicate deliveries: `17094`;
- acknowledgement losses and exact repeat revalidations: `15601` each;
- upstream acknowledgement stalls: `100000`;
- late expectations: `2697`; rate suppressions: `370`; fragment stalls: `29`;
  sequence gaps: `10`.

Two corrupt server-to-client carriers and one corrupt client-to-server ACK are
rejected exactly once each. The corrupt-ACK probe first reaches legitimate
snapshot admission and prediction-authority selection, then proves that the
corrupt reverse carrier cannot release its retained payload. All three probe
payloads remain retained until the deliberate epoch reset. They are not
counted as abandoned accepted positive frames.

## Validation

The final production build passed. The complete headless networking suite
passed `149/149`; its corpus row passed in `382.30 s`. That row invokes the
hardened runner, executes the fixed 100,000-frame corpus twice, compares
normalized evidence byte-for-byte, verifies the checked-in golden digest, and
publishes evidence bound to the executable, manifest, and runner hashes. The
focused runner contract passed `11/11`, packaged-asset tests passed `16/16`,
and the release bootstrap headless contract passed `1/1`.

The final `windows-x86_64` `.install/` refresh and staged validation passed
with 16 root runtime files, one root dependency, a 500-file `basew/pak0.pkz`,
31 botfile payloads, 215 RmlUi assets, and one q2aas reference map. None of
these focused single-platform validations substitutes for the broader parent
task release matrices, load/soak, or cross-platform gates.

## Exact limitations

This evidence is intentionally narrower than the parent tasks:

- Prediction stops at the canonical input resolver and pure authority
  selector. It does not execute `cg_predict`, PMove, weapon prediction, or
  predicted audiovisual presentation.
- The harness fabricates resolver history locally. It does not exercise the
  engine's V2 prediction-input request construction/import boundary.
- It uses one deterministic `NetImpair` profile, not the full ratified
  15-profile matrix.
- It is an in-process production-hook fixture without real sockets,
  multi-client load, soak, or cross-platform execution.
- The harness uses a dynamic vector per transmission, so it provides no
  allocation-free or steady-state allocation proof.
- Reordering is scheduled only among fragments in one transmission burst. The
  harness assigns a synthetic incoming packet sequence at delivery, so this is
  not evidence of netchan sequencing or cross-snapshot packet reorder.
- Snapshot semantic breadth is fixed at eight entities and four area bytes. It
  does not cover the complete entity/event/lifecycle/visibility matrix or the
  legal maximum projection.
- The three corruption probes are focused checks, not the 100,000 malformed
  cases per changed decoder/range required by `FR-10-T14`.
- Public advertisement, combined `0x77`, legacy/native dual-adapter promotion,
  presenter cutover, packet/CPU/memory/correction budgets, release-platform
  matrices, and rollout remain open.

## Roadmap effect

This closes the previously named 100,000-frame serialized production-corpus
milestone only. It does not close a project task:

- overall roadmap: **74/190 complete (38.9%)**, **116 open**;
- FR-10: **3/16 complete (18.75%)**, **13 open**;
- task closures in this milestone: **0**.

`FR-10-T04`, `FR-10-T06`, `FR-10-T07`, `FR-10-T08`, `FR-10-T09`, and
`FR-10-T14` remain Incomplete.
