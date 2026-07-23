# FR-10-T07 Native Event Preflight Probe

Date: 2026-07-20  
Project tasks: `FR-10-T07`, `DV-04-T02`; related gates `FR-10-T08`,
`FR-10-T14`, and `FR-10-T15`

## Decision

The cgame native event presenter now has a default-off, map-latched
full-resource preflight probe. Setting `cg_native_event_preflight_probe 1`
before the next map makes the native lane run the same exact-fence,
entity-generation, cached-resource, and effect-lifecycle readiness checks used
by the explicit test-only effect-authority path. A successful probe is marked
presented and accounted by the existing present-once journal before the
presenter callback runs, but that callback dispatches no audiovisual or UI
effect. Raw legacy service and frame presenters remain the sole effect owner.

This is a bounded `FR-10-T07` advancement, not task closure. The probe provides
a safe production-shaped way to exercise readiness without a family-wide
cutover. It does not complete the `DV-04-T02` ownership inventory, promote
native effect authority, implement T08 prediction reconciliation, or replace
the required live visual/audio fault and parity parent.

## Map and resource lifecycle

`CG_NativeEventPresenterBeginMap()` samples the non-archived cvar before the
normal map sound/model registration phases. The sampled decision is immutable
until `CG_NativeEventPresenterEndMap()`:

- changing the cvar during a map changes only the requested status;
- the current map keeps its original probe decision;
- the next map samples the new value; and
- a reconnect/map shutdown scrubs any prepared presentation plan.

When the latch is active, `CL_RegisterTEntSounds()` prepares the same native
impulse, temp-event, and muzzle sound handles that explicit effect authority
requires. Models, footsteps, server-indexed spatial audio, and other existing
map resources continue through their normal registration owners. No resource
registration or arbitrary path lookup occurs from `CanPresent`.

Default-off maps retain the prior resource profile. The independent
`CG_NativeEventPresenterSetEffectAuthority(bool)` test gate is unchanged; if a
test enables it while the probe latch is present, effect authority takes
precedence and its commits are not counted as suppressed probe effects.

## Shared preflight and no-effect commit

The presenter has one `full_preflight_enabled()` decision for both probe and
effect-authority operation. Each projected effect family follows the same
sequence:

1. copy and validate the typed record;
2. resolve exact immutable snapshot/entity identity where required;
3. if neither full mode is active, retain the original audit-only no-effect
   plan;
4. otherwise call the existing pure `CL_CanPresent*` or cached-handle lookup;
5. retain the checked family separately from its dispatch family; and
6. let the event runtime preflight, journal mark, audit accounting, and
   post-mark callback order remain unchanged.

In probe mode, the checked family records the resource/lifecycle proof while
the dispatch family is always `none`. A rejected preflight therefore remains
uncommitted and degrades/requires resync through the existing runtime contract;
a successful one is terminally consumed once without calling any legacy
entity, temp-event, muzzle, sound, damage, or help-path effect function.

Generic `WORR_EVENT_PAYLOAD_AUDIO` and unknown effects still fail closed. The
probe does not make an unproved asset mapping acceptable.

## Bounded status

`CG_NativeEventPresenterGetStatus()` returns a fixed 120-byte version-1 value
containing:

- map generation and active state;
- requested, latched, and currently active probe state;
- explicit effect-authority and resource-required state;
- saturating per-map probe commits;
- saturating suppressed-effect and nonvisual commit totals; and
- seven fixed family counters (`none`, legacy entity, legacy temp, muzzle,
  spatial audio, damage, and help path).

Accounting occurs in the post-mark callback. A duplicate or mismatched callback
cannot inflate it because the prepared plan is single-use. Counters reset at
the next map latch and remain readable after map end until that reset.

## Focused evidence

Commands run from `E:\Repositories\WORR` without launching a visible client:

```text
meson compile -C builddir-win \
  cgame_native_event_presenter_test cgame_x86_64
PASS

.\builddir-win\cgame_native_event_presenter_test.exe
native event probe schema=1 map=2 commits=2 suppressed=1 nonvisual=1 \
  muzzle=1 none=1 effect_calls=0
PASS

meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-cgame-native-event-presenter \
  network-cgame-event-runtime \
  network-native-event-presenter-source-contract \
  network-cgame-canonical-snapshot-copy-entities \
  network-native-client-readiness-pilot
PASS: 5/5
```

The focused presenter test covers default-off behavior, mid-map enable and
disable deferral, next-map activation, missing lifecycle/resource rejection,
zero effect calls on successful muzzle and nonvisual probe commits,
single-use accounting, fixed status counters, map-end scrubbing, and explicit
effect-authority precedence. The source contract locks the shared preflight
branch, pre-map resource registration, post-journal-mark accounting, and the
absence of any probe-to-native-readiness or raw-ownership connection.

## Bounded live map-reuse evidence

The later v100 gate attaches this presenter status to a schema-2, 320-byte
combined raw/probe status without changing the presenter's 120-byte V1 ABI.
Three fresh headless process repetitions each retain the same server, shooter,
and target across one `gamemap` transition. Both phases pass in every repeat.

Each of the six phases establishes an explicit zero checkpoint, then records
five raw actions and five present-once probe commits with the family profile
`(none=0, entity=0, temp=2, muzzle=1, spatial=1, damage=1, help=0)`. Raw
presentation dispatches five effects, the probe suppresses five native effects,
native dispatch remains zero, and raw action, raw effect, and probe chain hashes
match exactly. The four impact records travel as one schema-2 batch while the
earlier launch muzzle remains a schema-1 singleton. A 105 ms target upstream
latency plus 20 ms stall forces five checkpoint-window retries with zero peer
failure, duplicate authority, mismatch, degradation, or resync.

Map lifecycle is explicit: generations advance `1 -> 2`, map-end counts
advance `0 -> 1`, and event stream epochs rotate `2 -> 4`. The server quiesces
the prior native map before clearing its command stream; the retained cgame DLL
receives `Shutdown()` before its next per-map `Init()`. A sealed end-of-Begin
reliable prefix also prevents later reliable output from starving the isolated
native CHALLENGE on the reused map.

Evidence:
`docs-dev/fr-10-t07-schema2-damage-map-reuse-present-once-evidence-2026-07-20.md`
and
`.tmp/networking/fr10_t07_native_event_probe_checkpoint_v100_barrier_clean_repeat3.json`
(SHA-256
`545C6A46BD2E6A952958945CCE2F7DF2175551AE9423D5FFAF150DD31DA841D3`).
Focused verification is 9/9 and runner-unit verification is 96/96.

## Remaining closure work

- Extend the now-proven clean/delayed-ACK same-process map-reuse row across
  compound loss/reorder/rate, pause/timescale/demo seek, entity reuse, and
  bounded cosmetic-resource pressure cases.
- Generalize the schema-versioned raw/presenter parity parent beyond this exact
  five-record Blaster profile and its two-map lifecycle.
- Complete the per-carrier `DV-04-T02` ownership inventory and prove a safe
  negotiated cutover before suppressing any raw family.
- Prove visible and audible parity, entity reuse behavior, performance,
  malformed-input, load, soak, and supported-platform floors under T14/T15.

`FR-10-T07` therefore remains **In Progress** and the snapshot/netcode project
remains **8 of 16 complete**.
