# FR-10-T08: Sparse Hook Authority Receipt and Cgame Reconciliation

Task: `FR-10-T08` — full client prediction and reconciliation.

Status: the sparse authoritative-receipt and cgame reconciliation foundation
is complete. Its private per-peer ordered native-event carrier is now
implemented; see
`docs-dev/fr-10-t08-private-hook-authority-receipt-carrier-2026-07-16.md`.

## Why this boundary exists

The authoritative sgame interaction ledger observes every canonical command,
but publishing a reliable record for every held/released sample would be both
needlessly expensive and an incorrect fit for the general event presenter.
Hook prediction needs an authority answer only for a locally predicted request
edge: active (confirmed) or rejected. Continued held state and release do not
need an independent cgame side effect while the interaction remains
shadow-only.

The receipt therefore stays distinct from a presentation event and carries no
target, trace, attachment, collision, damage, audio, or visual data.

## Immutable receipt contract

`worr_local_interaction_authority_receipt_v1` is a 56-byte value object built
only from a validated authoritative request transaction. It contains:

- the exact canonical command ID and semantic command hash;
- authoritative state and transaction hashes;
- action sequence and final state flags; and
- the request outcome, which must contain `REQUESTED` and exactly one of
  `ACTIVE` or `REJECTED`.

The builder rejects persisted/release samples, pending-authority state,
contradictory outcomes, invalid transactions, or nonzero output storage. The
receipt validator fails closed on malformed fields. Cgame recomputes the
canonical command hash from its retained predicted transaction before it can
classify the result.

## Sgame projection and cgame pairing

`SG_LocalInteractionObservationCopyAuthorityReceiptForCommand` is a read-only
projection of the existing authenticated, exact-command sgame ledger. It does
not choose a recipient, allocate an event, write a packet, or mutate game
state.

`CG_LocalInteractionSubmitAuthorityReceipt` pairs the receipt with cgame's
bounded 128-entry request cache. Prediction and receipt may arrive in either
order. Exact duplicate receipts are inert; a distinct receipt for the same
command or any command/hash/action-sequence mismatch sets the local shadow's
resync requirement. Successfully paired requests are classified only as Hook
confirmed or Hook rejected. The cache is pruned after the relevant canonical
legacy command is consumed.

This is still reconciliation evidence only. It does not activate a local
hook, move authority, apply collision/damage, or present sound/effects.

## Deliberate non-promotion boundary

The subsequent carrier reuses the existing private per-peer event stream only
because it already supplies reliable ordering and epoch cancellation. Its
dedicated authority-receipt event type is non-presentable and snapshot-fence
free, so control-state reconciliation is not conflated with a world event.
There is still no legacy packet, snapshot field, demo field, public cvar, or
gameplay/presentation promotion.

## Validation

Focused in-process checks passed:

- `network-local-interaction` verifies receipt construction, validation,
  confirmation/rejection classification, divergence, and atomic failure.
- `network-cgame-local-interaction` verifies prediction-first and
  receipt-first pairing, duplicate suppression, confirmed/rejected
  classification, and conflicting authority fail-closed resync.
- `sgame_x86_64.dll` links the sparse receipt projection into the real
  authenticated observation ledger.

The complete Meson suite passed 141/141 tests after the change. `.install/`
was then refreshed and validated from the current build output: 16 root
runtime files, one root dependency, 456 packaged assets, 31 botfile payloads,
and 215 RmlUi payloads.

These checks do not launch a game process; input initialization and mouse
capture do not occur.
