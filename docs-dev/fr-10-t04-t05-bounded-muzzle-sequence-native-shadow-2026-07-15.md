# FR-10-T04 / FR-10-T05: Bounded sgame Muzzle Sequences

Date: 2026-07-15

Tasks: `FR-10-T04`, `FR-10-T05` (both remain In Progress)

## Outcome

The native event shadow first accepted a second direct sgame multi-event
family: a bounded homogeneous sequence of player, monster, and
rerelease-monster muzzle carriers in a single game-DLL multicast message. It
is now subsumed by the ordered mixed-carrier adapter documented in
`fr-10-t04-t05-mixed-game-event-native-shadow-2026-07-15.md`. The existing
single muzzle behavior is retained as a sequence of one.

This follows the same final-emission contract as the temporary-entity batch:
the legacy multicast is emitted first, then the native observer may queue an
ordered batch only when every carrier has exact per-peer snapshot lineage.
Packets, demos, client presentation, and authority remain legacy-owned. No
`q2proto/` file changed.

## Bounded raw sequence

`Worr_LegacyMuzzleEventDecodeRawSequenceV1` recognizes the three known legacy
layouts without adding a new wire format:

- player `svc_muzzleflash` (four bytes, including the silent flag);
- monster `svc_muzzleflash2` (four bytes); and
- rerelease monster `svc_muzzleflash3` (five bytes).

It stages both decoded carrier values and their player/monster families before
publishing. A sequence can contain at most 16 consecutive muzzle carriers. An
empty, malformed, truncated, mixed-opcode, or over-capacity sequence leaves
all output arrays and the count unchanged. The original single-carrier decoder
delegates through this implementation and continues to reject any second
carrier.

## Exact batch identity

`SV_SnapshotShadowBuildMuzzleCandidatesV1` builds the ordered native candidate
array against one exact sent snapshot, then publishes the batch only if all
source entities occur in that snapshot view. This keeps each family and
silence flag attached to its own source identity and prevents a partially
admitted native burst.

The `write_msg` adapter requires:

1. an unreliable `NETCHAN_NEW` post-snapshot message that has already fitted;
2. native event-shadow mode and an exact snapshot reference;
3. one supported bounded sequence (now also permitted to mix temp and muzzle
   carriers); and
4. visible source generations for every carrier.

At this milestone reliable messages remained legacy-only. The continuation in
`fr-10-t04-t05-reliable-mixed-game-event-native-shadow-2026-07-18.md` now
captures complete reliable mixed/multi-muzzle messages after legacy append and
binds them to the next exact final snapshot. Old-netchan, malformed,
unsupported-service, over-capacity, and non-visible paths remain byte-identical
legacy delivery.

## Verification

Focused tests cover ordered player-plus-rerelease-monster decoding,
family/silence preservation, capacity and malformed-tail rejection without
output mutation, exact batch source binding, and atomic rejection when one
source is not visible.

The latest full headless networking suite passed 125/125 and three repetitions
passed 375/375. The production client engine, dedicated engine, cgame, and sgame
targets linked successfully. The required refreshed `windows-x86_64` stage is
validated: 16 root runtime files, one root dependency, one q2aas reference map,
342 packaged assets, 31 botfile payloads, and 215 RmlUi asset payloads.

## Remaining scope

The transport remains observational and legacy authoritative. Unsupported
`svc_*` game messages, off-frame event classes without a safe identity anchor,
direct predicted local submission, native presentation, and authority cutover
remain open roadmap work. Reliable mixed/multi-muzzle snapshot fencing is now
implemented by the 2026-07-18 continuation above.
