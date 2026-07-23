# FR-10-T04/T05 Positional Off-Frame Spatial-Audio Native Shadow

Task IDs: `FR-10-T04`, `FR-10-T05`
Date: 2026-07-15
Status: implemented production-shadow extension; parent tasks remain in progress.

## Outcome

The default-off native event shadow now observes an unreliable entity sound
whose source is absent from the final per-client snapshot only when the real
server writer explicitly supplies its world position. This extends the earlier
visible-source spatial-audio producer without inventing lineage for an entity
that the recipient did not receive.

## Exact binding contract

`emit_snd` still first lets the real per-client writer decide the final sound
shape. When its source is not in the emitted frame, it sets the legacy
position bit before q2proto writes the sound. Only after that write succeeds
does the native observer decode the final structured sound.

- A source present in the exact sent snapshot remains bound to its immutable
  snapshot entity generation.
- An absent entity source is admitted only when the final sound has an explicit
  finite position. The canonical record is bound to world identity `{0, 1}`;
  the audio payload retains the raw entity, entity channel, final origin, and
  `WORR_EVENT_SPATIAL_AUDIO_POSITION_FORCED` flag. The world identity is a
  deliberate routing anchor, not a claim that entity zero emitted the sound.
- An off-frame sound without an explicit position, an invalid source/asset or
  floating field, a missing exact snapshot reference, disabled native mode,
  old netchan, reliable write, or any failed queue operation remains legacy
  delivery only. No legacy bytes, q2proto structures, demo data, or presenter
  authority change.

The snapshot binder stages its candidate locally and writes caller output only
after a visible-source bind or the explicit positional world bind validates.
Consequently a rejected source cannot partially alter a native batch.

## Regression coverage

`network-server-snapshot-event-candidates` now proves all three boundaries:
a visible entity sound preserves its exact generation; an unseen entity without
a position rejects without changing output; and the same unseen entity with a
finite final position produces a valid world-anchored candidate carrying the
original raw entity, origin, entity-channel, position, and forced-position
semantics. The existing virtual-link event delivery proof retains digest
`be9724b38fb5f682`.

## Validation

```text
meson test -C builddir-win --suite networking --print-errorlogs \
  network-server-snapshot-event-candidates \
  network-legacy-spatial-audio-event-candidate \
  network-native-event-virtual-link
# 3/3 passed

ninja -C builddir-win worr_engine_x86_64.dll worr_ded_engine_x86_64.dll \
  cgame_x86_64.dll sgame_x86_64.dll
# passed

meson test -C builddir-win --suite networking --print-errorlogs
# 125/125 passed

meson test -C builddir-win --suite networking --print-errorlogs --repeat 3
# 375/375 passed
```

The required `windows-x86_64` `.install/` refresh follows this validation and
validates 16 root runtime files, one dependency, the `basew` runtime, one q2aas
reference map, 342 packaged assets, 31 botfile payloads, and 215 RmlUi assets.

## Remaining scope

The 2026-07-18 continuation in
`docs-dev/fr-10-t04-t05-reliable-local-spatial-audio-native-shadow-2026-07-18.md`
now covers structured reliable engine sounds and positionless client-local
sound paths. Raw direct-sgame `svc_sound` messages outside the structured
boundaries, arbitrary non-local positionless sources, native presenter
authority, cgame/sgame prediction integration, and presenter cutover remain
open `FR-10-T04/T05/T07/T08/T09` work.
