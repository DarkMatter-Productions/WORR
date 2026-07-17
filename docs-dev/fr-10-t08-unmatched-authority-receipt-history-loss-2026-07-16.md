# FR-10-T08: Unmatched Authority Receipt History-Loss Barrier

Task: `FR-10-T08` — full client prediction and reconciliation.

Status: implemented as a fail-closed refinement of the default-off private
Hook authority-receipt path. This is an implementation increment only; the
parent task remains open.

## Problem

An ordered authority receipt may correctly arrive before the cgame prediction
pass which owns its command. Cgame retains that receipt as unmatched evidence
until the matching immutable canonical command record is available. Previously,
if later prediction began after that exact command in the same command epoch,
the unmatched cache entry could be discarded. That would hide prediction
history loss rather than establish a trusted baseline.

## Contract and implementation

`CG_LocalInteractionPredict()` now treats an unmatched receipt whose command
is behind the first retained canonical command as a reconciliation failure.
It increments `authority_expirations`, sets `requires_resync`, and stops the
pass before it can derive further local interaction state. The record is not
silently evicted.

`CG_EventRuntimeSynchronizeLocalInteractionHealth()` latches that cgame-local
failure into the active authority runtime as both degraded and
resync-required. `CG_PredictMovement()` calls it immediately after the
interaction pass, while the runtime's admission, presentation, and status
boundaries defensively perform the same latch. Thus the existing engine/cgame
runtime export exposes the normal `REQUIRES_RESYNC` health bit; no second
transport status protocol was added.

The client engine owner observes that bit through its existing status query,
quiesces the cgame authority consumer, and requires a fresh negotiated
descriptor epoch. This is the same fail-closed recovery boundary used for a
conflicting authority receipt.

## Native virtual-link proof

`network-native-event-virtual-link` now covers this sequence with the actual
cgame runtime and client engine owner:

1. The server sends and receives ACK for a fresh event-stream descriptor.
2. A valid Hook-rejected authority receipt arrives first and is admitted as
   unmatched evidence.
3. Cgame's next canonical prediction range starts one command after that
   receipt, so the missing matching command can no longer be reconstructed.
4. Cgame records one authority expiration and requests resync; the event
   runtime becomes degraded/resync-required.
5. The engine owner's status query observes the bit and enters resync before
   transmitting the queued event ACK. The server still retains the receipt
   (`retained_count == 1`, `events_acknowledged == 0`).

Focused console-only checks passed:

```text
native_event_virtual_link_test: ok ... digest=be9724b38fb5f682
cgame event runtime tests passed
```

`network-cgame-local-interaction` separately proves the direct cache rule and
the `authority_expirations` counter. All tests are in-process or console-only;
they create no window or socket, initialize no client input, and cannot capture
the mouse.

The complete registered Windows Meson suite passed **142/142**, including the
cgame runtime, native virtual-link, headless-input contract, and headless
process-policy contract. The production target set (`worr_engine_x86_64`,
`worr_ded_engine_x86_64`, `worr_x86_64`, `worr_ded_x86_64`,
`sgame_x86_64`, and `cgame_x86_64`) is current.

## Scope

This change does not make Hook active, does not change server authority, and
does not add collision, attachment, damage, sound, visual effects, a legacy
packet field, snapshot field, demo data, or public cvar. It only strengthens
the private shadow's hard-resync behavior when its exact evidence can no longer
be proven.

## Roadmap completion after this round

- Overall strategic roadmap: **68/180 complete (37.8%)**; **112 open**.
- `FR-10`: **3/16 complete (18.75%)**; **13 open**.
