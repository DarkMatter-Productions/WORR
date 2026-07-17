# FR-10-T08 off-hand Hook authoritative transaction ledger

Date: 2026-07-16  
Project task: `FR-10-T08`  
Status: authoritative transaction capture complete; delivery and presentation
remain intentionally open.

## Purpose

The independent cgame Hook shadow requires a matching authority fact before it
can reconcile a request. Sgame now constructs that fact at the authenticated
command boundary, using the same `worr_local_interaction_transaction_v1` model
as cgame. This closes the producer-side modelling prerequisite without
promoting a wire path.

## Lifecycle

`SG_LocalActionObservationScope` already begins before `ClientThink` applies a
new `usercmd_t` and ends after normal movement, weapon, and off-hand Hook
processing. Its existing immutable command context and pre/post live-state
captures supply:

- the exact canonical command record and client index;
- pre-command Hook-held and Hook-active state;
- the exact command Hook intent; and
- post-callback Hook-active state.

The scope builds an authoritative transaction only after the ordinary callback
has completed. Each client has a bounded 32-entry value-only ledger, keyed by
exact command ID. `SG_LocalInteractionObservationCopyForCommand` copies a
validated authoritative transaction out of that ledger; it does not expose
mutable storage.

## Rebase and fail-closed rules

The shared core now provides
`Worr_LocalInteractionRebaseBeforeCommandV1`. On bootstrap, command epoch
change, command-order break, or an observed pre-state mismatch, sgame derives
the state immediately before the canonical command from the record's sample
time and duration plus the observed Hook held/active facts. It then builds the
transaction from that exact baseline.

This avoids manufacturing a transition from stale bounded state. The rebase
does not infer an active hook, client collision, damage, attachment, pull,
audio, or visuals. A transaction cannot be created if the canonical record or
derived pre-state is invalid.

## Non-promotion boundary

The ledger is observational/private to sgame:

- no game state is modified beyond the existing production Hook path;
- no event, packet, snapshot, receipt, or demo data is emitted;
- no cgame presentation or suppression is enabled; and
- current-world Hook authority remains unchanged.

The next required integration is a reliable, ordered authority payload fenced
by canonical event/snapshot ordering. Only then may cgame match and retire its
pending request, classify a correction, and eventually consider presentation.

## Validation

`network-local-interaction` now exercises authoritative pre-command rebasing,
active/held state transfer, request creation, and atomic failure. The
`sgame_x86_64.dll` target links the ledger into the real command scope. The
complete Meson suite passed 141/141 tests after the change, including the
cgame and sgame targets. `.install/` was then refreshed and validated from the
current build output: 16 root runtime files, one root dependency, 451 packaged
assets, 31 botfile payloads, and 215 RmlUi payloads.

These are build and in-process test checks only. No game process was launched,
so client input initialization and mouse capture did not occur.
