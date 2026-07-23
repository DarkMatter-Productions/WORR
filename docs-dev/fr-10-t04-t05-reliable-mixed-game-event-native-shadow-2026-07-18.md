# FR-10-T04/T05 Reliable Mixed Game-Event Native Shadow

Task IDs: `FR-10-T04`, `FR-10-T05`

Date: 2026-07-18

Status: implemented production-shadow extension; parent tasks remain in
progress.

## Outcome

The default-off native event shadow now covers reliable direct-game messages
made entirely from the established `svc_temp_entity`, `svc_muzzleflash`,
`svc_muzzleflash2`, and `svc_muzzleflash3` carriers. One reliable message may
contain one or many muzzle records or an ordered mixture of temporary and
muzzle effects, up to the existing 16-carrier raw-message bound.

Legacy reliable bytes remain authoritative and byte-identical. The observer
runs only after `SV_ClientAddMessage` has appended those bytes to the real
client netchan. Unsupported service messages, old netchans, disabled native
mode, malformed sequences, and native capacity pressure remain legacy-only.
No public capability bit or `q2proto/` file changed.

## Snapshot-fenced reliable queue

A reliable message is written before the per-client frame that follows it, so
binding it to the last emitted snapshot would claim stale entity generations.
The event peer therefore owns a fixed native-only pending queue:

- at most 32 complete reliable messages and 512 decoded records;
- decoded carrier values only, with no borrowed game-DLL or message-buffer
  pointer;
- FIFO message and carrier order;
- the server `spawncount` captured with each message; and
- no allocation or legacy message-pool consumption after peer creation.

`SV_NativeShadowCaptureReliableGameEventsV1` decodes and publishes a batch only
after the legacy append. Capacity exhaustion returns an observational capacity
result and cannot reject, clear, or drain the legacy reliable queue.

`SV_NativeShadowFlushReliableGameEventsV1` runs immediately after the next
exact final per-client snapshot commits. It attempts every pending batch once,
rejects a mismatched map generation, binds valid batches to that one snapshot,
and clears the pending queue before native sender admission. A failed or
invisible batch cannot borrow a later snapshot's lineage. Epoch cancellation,
native drain/disable, detach, and destruction also discard pending batches.

The production ordering is now explicit:

1. reliable direct-game candidates bound to the just-committed snapshot;
2. event candidates derived from that frame; and
3. unreliable direct-game candidates that fit after the frame.

This mirrors the legacy reliable-before-frame-before-unreliable ordering while
leaving the legacy frame and reliable stream authoritative.

## Delivery and identity contract

`SV_SnapshotShadowBuildGameEventCandidatesWithDeliveryV1` is an additive
delivery-aware binder. The original V1 entry point remains a zero-flag wrapper,
so the existing unreliable path is unchanged.

For a reliable batch, every member becomes
`WORR_EVENT_DELIVERY_RELIABLE_ORDERED` with expiry zero. The complete batch is
failure-atomic: all temporary and muzzle carriers must bind their world,
source, and subject identities from the same exact final-emission snapshot
before caller output changes. Unknown delivery flags and any invisible member
reject the whole batch. Semantic IDs are still allocated only by the existing
descriptor-gated retained native sender.

## Regression coverage

The focused 6/6 headless gate covers:

- mixed and multi-muzzle raw sequence decoding;
- reliable delivery conversion, zero expiry, exact source/subject generations,
  and output-atomic invalid-flag/invisible-member rejection;
- capture after active native readiness, exact snapshot-fenced flush, one-shot
  consumption, and map-generation rejection;
- the 32-batch bound, unchanged pending state on the rejected 33rd batch, and
  exact queueing of 99 ordered candidates in the bounded pilot fixture;
- production source placement after the legacy append and before frame-derived
  and post-frame unreliable candidates; and
- unchanged native event virtual-link delivery.

Both production engine DLLs link successfully. The aggregate production build
passes, followed by the complete 159/159 headless networking suite in 359.5
seconds, including the 263.1-second native production corpus. Independent
package/release/bootstrap validation passes 29/29: 16 package tests, 12 release
unit tests, and one headless bootstrap contract.

The final Windows x86-64 `.install/` refresh validates 16 root runtime files,
one runtime dependency, the `basew` runtime, one loose q2aas reference map, a
532-file `pak0.pkz`, 31 botfile payloads, 215 RmlUi assets, and 11 packaged
q2aas AAS maps.

## Scope left open

This closes the roadmap's combined/reliable muzzle producer gap, not either
parent task. Other direct `svc_*` families, raw direct-game `svc_sound`,
arbitrary non-local positionless audio without a safe identity anchor,
predicted local-action presentation, legacy presenter cutover, public
advertisement, load/soak, malformed-corpus breadth, and supported-platform
release evidence remain open under `FR-10-T04/T05/T07/T08/T09/T14`.
