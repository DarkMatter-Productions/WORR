# FR-10-T05 Typed Event Journal Acceptance Closure

Date: 2026-07-19

Project task: `FR-10-T05`

## Outcome

`FR-10-T05` is complete. Its dependencies, `FR-10-T02` and `FR-10-T03`, are
complete, and one task-level gate now executes the production-linked evidence
for every published Definition-of-Done criterion twice with identical results.

The closure is deliberately scoped to the published typed-journal contract.
Additional event-family adapters, predicted gameplay presentation, presenter
cutover, public native authority, and release/load matrices remain assigned to
`FR-10-T04`, `FR-10-T07`, `FR-10-T08`, `FR-10-T14`, and `FR-10-T15`; they do
not expand or reopen T05's accepted criteria.

## Definition-of-Done evidence

1. **Explicit, bounded schema.** The pointer-free canonical event record
   validates type, payload, source time, source/subject generation, semantic
   identity, ordering, delivery class, prediction class, and typed payload
   bounds. The payload catalog and legacy-family gates exercise valid and
   malformed variants transactionally.
2. **Multiple events per source/tick.** The journal and mixed temporary,
   muzzle, spatial-audio, damage, and help-path adapters retain bounded ordered
   batches without collapsing distinct same-tick semantic IDs. Selective
   receipts preserve gaps and ordering across multiple retained events.
3. **At-most-once presentation.** Journal, cgame runtime, native stream, native
   virtual-link, snapshot-fence, and local-action prediction gates cover loss,
   retry/replay, duplication, late and reordered arrival, selective receipt,
   prediction matching/cancellation, epoch reset, and sequence wrap. Required
   records present at most once and reliable records remain retained until an
   exact semantic receipt or terminal cancellation.
4. **Legacy compatibility.** Entity, temporary-entity, player/monster/Rerelease
   muzzle, spatial-audio, damage, help-path, and keyed-POI adapters construct
   canonical records only around successful legacy handling. Legacy packets,
   demos, and presenters remain authoritative and unchanged; `q2proto/` is
   untouched.

## Task-level acceptance gate

`network-fr10-t05-acceptance` runs 18 compiled gates twice and requires every
gate's stdout, stderr, and exit status to be identical between repetitions:

- canonical journal, event stream, live shadow, cgame presentation/runtime,
  native admission/sender/virtual-link, and snapshot event-fence gates;
- server candidate plus legacy temporary, muzzle, spatial-audio, game-event,
  damage, help-path, and keyed-POI adapter gates; and
- local-action prediction/authority correlation.

The focused result passes all 18 gates in both repetitions and writes
`.tmp/networking/fr10_t05_acceptance.json` with schema
`worr.networking.fr10-t05-acceptance-evidence.v1` and digest
`5f300c5f7f925cf5105812cf49ce108305c357790989978738e34e88058e347c`.

Focused command:

```powershell
meson test -C builddir-win --no-rebuild --print-errorlogs network-fr10-t05-acceptance
```

The final production build passes. The serial headless networking suite passes
165/165: T05 takes 1.01 seconds, T09 takes 219.20 seconds, and the serialized
100,000-snapshot corpus takes 188.51 seconds. Package tests pass 16/16,
release-unit tests pass 12/12, and the headless bootstrap contract passes 1/1.
The refreshed and independently validated Windows stage contains 16 root
runtime files, one dependency, and a 546-member `basew/pak0.pkz`: 535 current
repository assets plus 11 hash-audited Q2AAS maps, including 31 botfile and 215
RmlUi members.

## Accounting

Closing exactly `FR-10-T05` moves the strategic roadmap from 74/190 to
**75/190 complete (39.5%)**, with 115 tasks open. FR-10 moves from 3/16 to
**4/16 complete (25.0%)**, with 12 tasks open. The T09 acceptance gate does
not add a second completion because `FR-10-T06` remains incomplete.
