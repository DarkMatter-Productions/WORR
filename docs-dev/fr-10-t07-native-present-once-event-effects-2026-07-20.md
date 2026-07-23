# FR-10-T07 Native Present-Once Event Effects

Date: 2026-07-20  
Project tasks: `FR-10-T07`, `DV-04-T02`; related boundaries
`FR-10-T04`, `FR-10-T05`, `FR-10-T08`, and `FR-10-T14`

## Decision

The default-off native event epoch now has a bounded cgame value presenter for
the currently projected legacy event families. A validated authoritative event
is resolved against the exact immutable canonical snapshot named by its event
reference and committed to the present-once journal. Audiovisual dispatch is a
separate authority decision: `CG_NativeEventPresenterSetEffectAuthority(bool)`
defaults false and is not connected to family-wide native ACTIVE readiness.
While false, the native lane validates, orders, audits, and terminally consumes
recognized records without calling a value-effect callback; the legacy service
and frame paths remain the sole production effect owner.

This is a bounded `FR-10-T07` cutover slice. It does **not** complete T07, T08,
T14, or T15. In particular, the live loss/rate/pause/demo presenter fault matrix
and the wider `DV-04-T02` ownership audit remain open. Generic local-action V2
audio/effect asset presentation also remains a T08 promotion blocker and fails
closed instead of guessing a legacy resource.

## Ownership and commit protocol

The event runtime exposes a synchronous two-phase presenter contract:

1. `CanPresent(record, context)` receives a borrowed event value plus exact
   provenance/fence metadata and validates the immutable identity plus every
   already-prepared handle and lifecycle prerequisite needed by `Present`. It
   has no presentation or resource-registration side effects.
2. A rejected preflight leaves the journal entry unpresented, latches the
   current authority epoch as degraded/requiring resync, and stops advancement.
3. A successful preflight is followed by the existing journal present-once
   mark and audit accounting.
4. Only after that irreversible mark does the runtime invoke the total/no-fail
   `Present(record, context)` callback.

No installed presenter retains the borrowed record, context, engine entity
pointer, or canonical-timeline pointer. A deliberately half-installed callback
pair is observable as an invalid presenter and fails before the journal mark;
it is never silently converted to the audit-only configuration.

Both callback phases run under a runtime non-reentrancy guard. A callback that
attempts any result-returning mutation receives
`CG_EVENT_RUNTIME_REENTRANT`; void configuration setters are ignored until the
callback returns. Read-only status inspection remains available so preflight
and post-mark ordering can be audited without mutating health state.

The disabled effect-authority path still performs exact fence and
entity-generation validation before producing its no-effect plan. It skips
only fallible resource readiness and value dispatch; it cannot turn a stale or
semantically invalid authority record into an ACKable success. Unknown effect
IDs and generic audio remain rejected rather than audit-consumed.

## Immutable fence and generation handling

Each authoritative runtime entry retains the exact `worr_snapshot_id_v2` from
the joined event reference, in addition to its tick/time fence. The cgame
timeline provides a fail-closed exact-ID lookup that requires:

- the active epoch;
- exactly one retained committed snapshot with the requested identity; and
- a generation-valid timeline reference.

The presenter copies that snapshot's entity range into cgame-owned fixed
storage before resolving an entity. Source and subject identities must match
both entity index and generation. The canonical entity is then converted into a
copied legacy render/effect value under the existing adapter bounds. Missing,
duplicate, stale, evicted, mismatched, or unrepresentable entities reject the
event and require resync; mutable `cl_entities[]` is not substituted.

Muzzle flashes, legacy entity impulses, entity-attached temp-event sound, and
entity-channel spatial audio use the copied origin. Native muzzle dynamic-light
keys include the source generation. This prevents a delayed event from
retargeting a recycled legacy entity slot. Fixed-origin footsteps intentionally
use the default material because querying a current collision surface would
reintroduce mutable-slot identity.

## Presenter coverage

When effect authority is explicitly enabled, the presenter handles:

- legacy entity impulse events;
- legacy temporary-entity events;
- player and monster muzzle flashes;
- projected positional/entity-channel spatial audio whose precache handle
  exists and whose legacy pitch is exactly representable;
- per-client damage indicators;
- help-path start/update markers; and
- state/gameplay/authority-receipt payloads that intentionally have no generic
  audiovisual side effect.

Unknown effect IDs and generic `WORR_EVENT_PAYLOAD_AUDIO` are rejected. Their
asset IDs do not yet define a production cgame resource mapping, so presenting
them would be an unproven T08 behavior change.

Production does not enable that dispatch gate yet. Raw service sound, temp
entity, player/monster muzzle, damage, help-path, and snapshot-embedded legacy
entity impulse presentation therefore remain authoritative. Persistent entity
effects such as `EF_TELEPORTER` are not one-shot event-journal records and are
outside this slice.

## ABI and lifecycle changes

`CGAME_ENTITY_API_VERSION` is now 7. The accumulated version adds the explicit
64-bit host render clock, canonical loop-audio enumeration/binding operations,
and a bounded `S_GetPrecachedSound(index)` import. The sound import exposes only
an already precached handle and returns zero for an out-of-range or missing
entry; the presenter does not resolve arbitrary paths from event data.

The presenter is installed only while the cgame entity import is live and is
uninstalled before that import changes or unloads. Its prepared value plan is
single-use and is scrubbed after presentation, authority changes, or uninstall;
uninstall also restores the effect-authority default of false.

When effect authority was explicitly armed before map precache, known muzzle,
temporary-event, and legacy impulse sound handles are registered during the
existing `CL_RegisterTEntSounds` phase, never from `CanPresent`. Default-off
maps do not perform that extra registration. Native muzzle and temp/impulse
presentation consumes the cached handles, while pure readiness functions check
the retained handles, models, cvars, and value-presenter lifecycle. Enabling
after precache therefore remains fail-closed until a later prepared lifecycle;
a missing handle or busy lifecycle rejects before the journal mark. Bounded
particle/beam/explosion pools retain their existing cosmetic pressure behavior
and are not a retry channel.

## Focused evidence

Commands run from `E:\Repositories\WORR`:

```text
meson compile -C builddir-win cgame_x86_64 worr_engine_x86_64
PASS

meson compile -C builddir-win cgame_canonical_snapshot_copy_entities_test
.\builddir-win\cgame_canonical_snapshot_copy_entities_test.exe
PASS: immutable entity copy plus exact-ID lookup, missing ID, wrong epoch,
invalid ID, transactional output, and stale-reference behavior

meson compile -C builddir-win cgame_event_runtime_test
.\builddir-win\cgame_event_runtime_test.exe
PASS: preflight/commit callback order, exact authority fence, predicted
provenance, exactly-once repeats, rejected preflight, half-registration, and
blocked mutation/presenter replacement reentry from both callback phases

meson compile -C builddir-win cgame_native_event_presenter_test
.\builddir-win\cgame_native_event_presenter_test.exe
PASS: direct production presenter dispatch for exact-fenced legacy entity/temp,
muzzle, positioned and entity-channel spatial audio, damage, help-path, and
nonvisual state payloads, plus default-off audit-only consumption, explicit
authority enablement, missing-resource/busy-lifecycle rejection, prepared-plan
scrubbing, unsupported payload rejection, and stale-identity rejection

python tools/networking/test_native_event_presenter_source_contract.py \
  --repo-root .
PASS: runtime ordering and callback non-reentrancy, exact fence/generation
lookup, default-off effect authority, pure cached-resource readiness, value
presenter lifecycle, and legacy/raw ownership boundaries

meson test -C builddir-win --print-errorlogs \
  network-cgame-native-event-presenter \
  network-cgame-event-runtime \
  network-native-event-presenter-source-contract \
  network-cgame-canonical-snapshot-copy-entities
PASS: 4/4
```

Additional real-process effect parity and live fault-matrix evidence must be
recorded before this task can be promoted.

## Bounded v100 real-process continuation

The default-off preflight now has one strict same-process map-reuse parent for
a real Blaster action. Three fresh repetitions each pass before and after one
`gamemap` transition, for six successful phases total. Every phase records the
same five-action sequence: two temporary effects, one muzzle, one positional
sound, and one damage indicator. Four same-impact records form one schema-2
EVENT ACK/retry unit; the earlier launch muzzle remains an independent
schema-1 unit.

The target proves five raw legacy dispatches, five authoritative present-once
entries, five probe commits, and five suppressed native effects, with exact
raw/probe/effect chain equality and zero native effect dispatch. Checkpoint
application plus an exact duplicate receipt cannot inflate the counters. The
delayed target ACK path produces five sender retries per phase with no terminal
peer failure, authority duplicate/conflict, body/reference mismatch,
presenter-commit mismatch, degradation, or resync.

The retained cgame DLL now closes map generation 1 before initializing map
generation 2, and the server cancels prior native map state before clearing its
map-local command stream. The gate observes map generation `1 -> 2`, map-end
count `0 -> 1`, and event stream epoch `2 -> 4` without replacing any process.
A sealed reliable bootstrap prefix prevents later post-Begin messages from
starving the map-2 CHALLENGE.

Detailed evidence:
`docs-dev/fr-10-t07-schema2-damage-map-reuse-present-once-evidence-2026-07-20.md`.
The parent artifact is
`.tmp/networking/fr10_t07_native_event_probe_checkpoint_v100_barrier_clean_repeat3.json`
(SHA-256
`545C6A46BD2E6A952958945CCE2F7DF2175551AE9423D5FFAF150DD31DA841D3`).
This closes only the clean/delayed-ACK same-map row; raw legacy effects remain
the production owner.

## Remaining closure work

- Extend the proven same-map delayed-ACK row across packet loss, duplication,
  reordering, rate limits, pause/resume, timescale changes, entity reuse,
  reconnect, demo record/play/seek, and renderer/audio lifecycle boundaries.
- Prove visible/audio semantic parity and exactly-once counts for every
  projected family/profile in real client/server processes; the v100 null-audio
  five-record Blaster profile is necessary but not exhaustive.
- Finish the applicable `DV-04-T02` ownership inventory and remove or formally
  retain every duplicate client/cgame presenter path.
- Complete T08's predictable-state catalogue, command-keyed weapon/action
  replay, generic audiovisual mapping, suppression, reconciliation, and
  correction budgets before enabling predicted presentation authority.
- Carry presenter CPU/allocation, malformed-input, load, platform, soak, and
  release evidence through T14 and T15.
