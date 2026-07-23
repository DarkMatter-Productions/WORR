# FR-10-T04/T05 Mixed Direct Game-Event Native Shadow

Task IDs: `FR-10-T04`, `FR-10-T05`
Date: 2026-07-15
Status: implemented production-shadow extension; the parent tasks remain in progress.

## Outcome

The default-off native event shadow now observes one bounded, ordered mixture
of direct game-DLL `svc_temp_entity`, `svc_muzzleflash`, `svc_muzzleflash2`,
and `svc_muzzleflash3` records in a single already-fitted post-snapshot
unreliable message. This supersedes the previous two homogeneous dispatch
branches without changing the legacy wire protocol, q2proto, or the
authoritative legacy presentation path.

The new common carrier preserves the decoded temporary-entity payload or the
muzzle payload plus its player/monster/rerelease family. It is intentionally a
server-side observation format, not a new network protocol. The sequence is
bounded to 16 carriers, preserves wire order, and never changes caller output
until the complete raw message has decoded successfully.

## Design and safety contract

- The established temporary-entity and muzzle decoders now expose a prefix
  operation. It validates exactly one carrier and reports only its consumed
  bytes; it does not add framing or amend q2proto.
- `Worr_LegacyGameEventDecodeRawSequenceV1` dispatches only the four supported
  service opcodes into a local 16-carrier buffer, then commits the complete
  ordered result. Empty input, malformed/truncated data, an unknown service
  opcode, an invalid subordinate shape, and capacity exhaustion leave both the
  caller buffer and count untouched.
- `SV_SnapshotShadowBuildGameEventCandidatesV1` binds the resulting order to
  one exact final snapshot reference. It reuses the established entity/world
  temporary-event and muzzle mappers, so all source/subject visibility and
  generation checks retain their existing semantics. One failed member rejects
  the entire batch and preserves the output buffer.
- The final `SV_Multicast` writer observes only unreliable `NETCHAN_NEW`
  messages after the corresponding snapshot has fitted. It queues exactly one
  native batch after complete decode and complete snapshot binding.

At this milestone reliable writes remained legacy-only. The continuation in
`fr-10-t04-t05-reliable-mixed-game-event-native-shadow-2026-07-18.md` now
defers complete reliable batches until the next exact committed snapshot and
emits them as reliable-ordered native candidates. Old netchan, no snapshot
reference, disabled native mode, audio packets, malformed input, over-capacity
sequences, unsupported `svc_*` families, and non-visible sources/subjects
remain legacy-only. Existing legacy delivery is never suppressed.

## Regression coverage

`network-legacy-game-event-candidate` proves a mixed temporary/muzzle/
temporary sequence preserves payloads, family, silence state, and order; it
also proves capacity and unknown-opcode rejection are output-atomic.
`network-server-snapshot-event-candidates` proves an ordered mixed batch binds
to one final view, rejects an invisible member without a partial batch, and
rejects more than 16 records. The existing virtual-link loss/reorder proof
continues to cover native event semantic delivery and retains digest
`be9724b38fb5f682`.

## Validation

```text
meson test -C builddir-win --suite networking --print-errorlogs \
  network-legacy-game-event-candidate network-legacy-temp-event-candidate \
  network-legacy-muzzle-event-candidate network-server-snapshot-event-candidates \
  network-native-event-virtual-link
# 5/5 passed

ninja -C builddir-win worr_engine_x86_64.dll worr_ded_engine_x86_64.dll \
  cgame_x86_64.dll sgame_x86_64.dll
# passed

meson test -C builddir-win --suite networking --print-errorlogs
# 125/125 passed

meson test -C builddir-win --suite networking --print-errorlogs --repeat 3
# 375/375 passed
```

The required `windows-x86_64` `.install/` refresh is performed after the
validation gate. It validates 16 root runtime files, one dependency, the
`basew` runtime, one q2aas reference map, 342 packaged assets, 31 botfile
payloads, and 215 RmlUi asset payloads.

## Remaining scope

This is intentionally a narrow production shadow extension, not a cutover.
Other direct game service families, off-frame effects without a safe identity
anchor, native authority, live cgame/sgame local-action integration, prediction
cutover, and presenter replacement remain under the open
`FR-10-T04/T05/T07/T08/T09` work. Reliable mixed and multi-muzzle messages are
covered by the 2026-07-18 continuation above.
