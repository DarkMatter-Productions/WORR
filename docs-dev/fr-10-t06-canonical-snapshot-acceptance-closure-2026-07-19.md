# FR-10-T06 Canonical Snapshot Acceptance Closure

Date: 2026-07-19

Project task: `FR-10-T06`

## Outcome

`FR-10-T06` is complete. Its dependencies `FR-10-T02`, `FR-10-T03`, and
`FR-10-T05` are complete, and one parent-level gate now proves every published
acknowledged-baseline, immutable-view, shadow-parity, recovery, fault,
single-model, and budget criterion at the task boundary.

The closure does not make the private native carrier public, replace legacy
snapshot authority, finish cgame interpolation/presentation, or satisfy the
multi-client load, soak, platform, and release matrices owned by
`FR-10-T04`, `FR-10-T07`, `FR-10-T14`, and `FR-10-T15`.

## Definition-of-Done evidence

1. **Acknowledged bases are retained.** The canonical projector consumes the
   exact q2proto `deltaframe`/server acknowledged base and never introduces a
   previous-frame-only algorithm. Q2proto projection/wire and server-shadow
   gates cover acknowledged branches, base jumps, invalid bases, full frames,
   and keyframe recovery while legacy packet output remains authoritative.
2. **Complete R1 shadow parity.** Server final-emission capture constructs a
   complete canonical player/entity/event/identity/discontinuity view beside
   the active legacy path. The offline corpus compares separately allocated
   server and receiver projections for 100,000 snapshots with exactly 100,000
   endpoint, legacy, component, and chronology matches and zero unexplained
   mismatch.
3. **Fault and promotion floor.** Two identical offline runs cover invalid-base
   recovery, acknowledged branches, keyframes, entity add/update/remove/reuse,
   visibility gaps/re-entry, truncation, fragment stalls, rate suppression,
   events, stale references, tick wrap, and the signed wire-frame boundary.
   Two identical serialized production runs carry 100,000 positive frames
   through WNC1/WNE1/WTC1, deterministic loss/burst/reorder/duplicate/stall,
   client admission, cgame selection, semantic ACK, and exact release. All
   three corrupt probes reject and accepted abandonment is zero.
4. **Complete immutable generation-safe views.** The caller-owned fixed store
   recomputes semantic hashes and serial ranges transactionally, publishes only
   complete value views, rejects stale generation-tagged references, and
   preserves retained bases on failure. Store, schema, q2proto, and admission
   gates run twice with identical results.
5. **No silent limit or recovery failure.** Maximum counts, buffer sizes,
   malformed/truncated input, output atomicity, fragmentation, expectation
   capacity, sender coalescing, lost ACK/revalidation, receiver timeout,
   overflow, wrong epoch, and keyframe quarantine are covered by the component
   gates and the two corpora. The complete legal WNC1 snapshot is 80,869 bytes
   and remains below the explicit 131,072-byte payload cap.
6. **One canonical model.** The legacy q2proto projector and private WORR
   snapshot adapter both use the same Snapshot V2 store, projection hashes,
   WNC1 codec, and admission validators. The production virtual link proves
   that path through the real server/client wrappers. No `q2proto/` source
   changed.
7. **Declared budgets.** The current debug-optimized Windows x86-64 probe uses
   the maximum 512 entity records, 1,024 area bytes, and 512 event references.
   Across 256 measured iterations after warmup, final aggregate build plus
   encode p95 is 504,800 ns and decode p95 is 211,300 ns; each is below the 1,666,600 ns
   limit (10% of WORR's minimum 60 Hz authoritative-frame interval).
   A 64-slot maximum-capacity canonical history requires 5,869,696 bytes under
   its 16 MiB cap. Fixed native sender-plus-receiver owners require 624,152
   bytes under their 2 MiB cap. Store high-water and server-shadow allocation
   telemetry remain covered by the compiled gates.

## Parent-level acceptance gate

`network-fr10-t06-acceptance` performs the following work in one decision:

- runs 12 compiled store, recovery, q2proto, server-shadow, native-codec,
  admission, sender, receiver, virtual-link, production-link, and cgame
  authority gates twice and requires byte-identical output per gate;
- runs the 100,000-snapshot offline final-emission/projector corpus twice and
  verifies digest `7b185107eeb0f6e7`;
- runs the 100,000-snapshot serialized production corpus twice and verifies
  golden digest `c6aee48df85341ab`, 100,000 ACKs/releases/authority selections,
  three corrupt rejections, and zero accepted abandonment; and
- runs the maximum-capacity timing/storage probe and rejects any declared
  CPU, history, native-owner, or encoded-size budget overrun.

The focused gate passes in 503.26 seconds and writes
`.tmp/networking/fr10_t06_acceptance.json` with schema
`worr.networking.fr10-t06-acceptance-evidence.v1`. Its component evidence is
under `.tmp/networking/fr10_t06_components/`.

Focused command:

```powershell
meson test -C builddir-win --no-rebuild --print-errorlogs network-fr10-t06-acceptance
```

The final production build and serial 166/166 networking suite pass. In that
suite the T05, T09, serialized-production-corpus, and T06 rows take 1.81,
362.58, 302.71, and 380.00 seconds. Package tests pass 16/16, release-unit tests
pass 12/12, and the headless bootstrap contract passes 1/1. The refreshed and
independently validated Windows stage contains 16 root runtime files, one
dependency, and a 593-member `pak0.pkz` (582 repository assets plus 11 audited
Q2AAS maps).

## Accounting

Closing exactly `FR-10-T06` moves the strategic roadmap from 75/190 to
**76/190 complete (40.0%)**, with 114 tasks open. FR-10 moves from 4/16 to
**5/16 complete (31.25%)**, with 11 tasks open. `FR-10-T09` now has completed
dependencies and direct acceptance evidence, but remains a separate unchecked
task in review so this requested increment remains exactly one task.

## 2026-07-20 post-closure revalidation

The task remains closed after the controlled-entity generation-provenance fix
and exact private `0x57` production-link fixture correction. Event model
revision 2 and keyed-POI catalog coverage refresh the offline corpus to
`6451c75bdb523477`; its two byte-identical 100,000-snapshot runs have evidence
SHA-256
`ad52959b88c5ccbd17d776e2d78b7d33aea526d49b98e56b25bff98108dca5c6`.
The serialized production corpus still reports golden `ba519ae7bdd1db74`
because that fixture contains no event references; its rebuilt executable-
bound evidence SHA-256 is
`9302fd5cb40b1cfbf774e0f7115ec03618dde763c056b67ff71cb0f5d7f5c945`.
The maximum-capacity build-plus-encode/decode p95 values are 486,900/214,700
ns under the unchanged 1,666,600 ns limits. The 12-gate parent passes twice
with evidence SHA-256
`781b1f1edd147b2218423ac192c5047cd77baef8feaf5233d8c1bae933963782`
inside a clean **201/201** networking suite. The forensic rebaseline and
scheduling repair are documented in
`docs-dev/fr-10-networking-suite-rebaseline-and-resource-isolation-2026-07-20.md`;
no acceptance floor or workload was reduced.
