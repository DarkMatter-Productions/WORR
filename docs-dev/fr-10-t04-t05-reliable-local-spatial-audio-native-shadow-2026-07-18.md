# FR-10-T04/T05 Reliable and Local Spatial-Audio Native Shadow

Task IDs: `FR-10-T04`, `FR-10-T05`

Date: 2026-07-18

Status: implemented production-shadow extension; parent tasks remain in
progress.

## Outcome

The default-off native event shadow now observes the two structured audio
families that were still bypassing the earlier final-emission producer:

- reliable `SV_StartSound` output, including PHS-filtered, no-PHS/global, and
  explicit-position multicast delivery; and
- positionless `PF_LocalSound` output directed to one client, for both
  transient and reliable cues.

The legacy/q2proto path remains authoritative. Native observation happens only
after the exact legacy write has succeeded, uses the recipient's latest exact
final-emission snapshot as its fence, and changes no packet bytes, demo data,
sound playback, or public capability advertisement.

## Delivery and identity contract

`SV_SnapshotShadowBuildSpatialAudioCandidateWithDeliveryV1` is an additive
delivery-aware binder. The original V1 entry point remains a zero-flag wrapper,
so existing unreliable visible/explicit-position behavior is unchanged.

- `RELIABLE` changes the canonical event to
  `WORR_EVENT_DELIVERY_RELIABLE_ORDERED`, clears expiry, and sets the typed
  payload's reliable flag. The existing native descriptor/DATA/semantic-ACK
  sender retains and releases the event through its established exact-receipt
  path.
- `NO_PHS` records the server's actual global/no-PHS routing decision without
  changing the q2proto channel field.
- `LOCAL_ONLY` binds the canonical source to stable world identity `{0, 1}`.
  The payload retains the raw client entity and channel override key, but does
  not claim that this non-spatial local cue came from a snapshot entity. A
  positionless cue therefore has neither `HAS_POSITION` nor
  `POSITION_FORCED`.
- Unknown delivery flags, invalid payloads, missing/stale snapshot fences, and
  failed source binds reject atomically without altering caller output.

Reliable entity sounds retain exact visible snapshot lineage when possible.
The engine already includes a final position for reliable `SV_StartSound`
traffic; an off-frame source therefore follows the established explicit-
position world bind and `POSITION_FORCED` contract instead of inventing entity
lineage.

## Production placement

`SV_StartSound` records native candidates only after the shared structured
q2proto multicast write succeeds:

- the normal reliable branch first appends the legacy reliable message, then
  submits the typed candidate for that recipient;
- the forced-position multicast branch first performs normal legacy multicast,
  then repeats its exact PHS/area recipient filter solely for default-off
  native observation; and
- attenuation-none routing is preserved as typed `NO_PHS` metadata.

`PF_LocalSound` first performs the recipient-specific q2proto write and normal
unicast append. Only then does it submit a `LOCAL_ONLY` candidate, adding
`RELIABLE` only when the original channel requested reliable delivery.

`SV_QueueNativeSpatialAudio` is the shared post-write boundary. It requires an
active event-shadow mode, finds the exact snapshot by the client's emitted wire
frame, decodes the already-final structured service, binds it transactionally,
and queues one ID-less candidate. Any unmet precondition leaves the native
shadow empty while legacy delivery continues normally.

## Regression coverage

`network-server-snapshot-event-candidates` now proves:

- reliable plus no-PHS visible audio preserves the exact entity generation,
  becomes reliable ordered, and never expires;
- reliable positionless local audio is world-bound while retaining its raw
  entity/channel and local/reliable flags;
- transient positionless local audio remains transient with its original
  one-tick expiry; and
- unknown delivery flags reject without changing output.

`network-reliable-spatial-audio-source-contract` freezes the production
placement: successful q2proto/legacy append precedes every native queue, the
shared helper uses the exact sent snapshot, and the final unreliable writer
continues to observe only after successful per-client encoding.

Focused headless validation passes 7/7 across the new binder/source contract,
the shared spatial-audio mapper, event sender, server pilot, event virtual link,
and combined snapshot-production virtual link. Both production engine DLLs
also link successfully. The final aggregate production build passes, followed
by the complete 158/158 headless networking suite and 30/30 package/release/
bootstrap contracts. The refreshed Windows x86-64 `.install/` validates 16
root runtime files, one runtime dependency, the `basew` runtime, one q2aas
reference map, a 528-file `pak0.pkz`, 31 botfile payloads, and 215 RmlUi
assets.

## Scope left open

This extension does not decode raw direct-game `svc_sound` byte spans, admit an
arbitrary non-local positionless source with no safe spatial/lineage anchor,
cover other service families, submit predicted local-action presentation, or
cut over the legacy audio presenter. Combined/reliable muzzle traffic is now
covered by
`fr-10-t04-t05-reliable-mixed-game-event-native-shadow-2026-07-18.md`. The
remaining gaps stay open `FR-10-T04/T05/T07/T08/T09` work. `q2proto/` is
unchanged.
