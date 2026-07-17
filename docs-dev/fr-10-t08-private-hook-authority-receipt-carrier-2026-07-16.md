# FR-10-T08: Private Ordered Hook Authority-Receipt Carrier

Task: `FR-10-T08` — full client prediction and reconciliation.

Status: implemented as a default-off, native-event-mode reconciliation carrier.
It is private to the authenticated peer and remains deliberately separate from
world events and presentation.

## Objective

The prior Hook interaction work produced two value-only halves of the same
exact-command fact: sgame's authenticated Hook request-edge receipt and
cgame's bounded receipt/prediction pairing cache. This round connects those
halves without promoting a gameplay action or adding a public protocol field.

The selected carrier is the existing private `0x73` native event stream. That
stream is already per-peer, reliable ordered, receipt-acknowledged, and
epoch-cancelable. Reusing only its lifecycle—not its world-event semantics—is
smaller and safer than adding a second transport queue.

## Ownership and lifecycle

1. During the authenticated server command callback, sgame builds a receipt
   only for a Hook request edge and publishes it through the new
   `WORR_LOCAL_INTERACTION_AUTHORITY_IMPORT_V1` engine import.
2. The server owns a map-scoped, 32-entry-per-client mailbox. It accepts only
   a valid immutable receipt, treats an exact re-publication as idempotent,
   and rejects a distinct receipt for the same canonical command ID.
3. Immediately after `SV_ClientThink()` returns and the command context is
   closed, `SV_WorrConsumeCommand()` removes the exact command's receipt from
   that authenticated client's mailbox. If that client has an active private
   native event stream, it queues one candidate there. The event sender owns
   retransmission, ordering, acknowledgement retirement, and map-epoch
   cancellation after this one-shot transfer.
4. The candidate is the dedicated `WORR_EVENT_TYPE_AUTHORITY_RECEIPT` with
   `WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1`. It must be critical,
   reliable ordered, authoritative-only, non-expiring, and carry absent source
   and subject entity references. Its payload is the validated 56-byte receipt.
5. Cgame validates and journals the record in normal ordered event admission,
   then marks it `AUTHORITY_SKIP`. It cannot wait for a snapshot fence or reach
   a generic event presenter. Only after the event batch commits does cgame
   submit the receipt to `CG_LocalInteractionSubmitAuthorityReceipt()`.
6. A conflicting, invalid, divergent, capacity-exhausted, or already-resyncing
   receipt receiver result marks the event authority domain resync-required.
   An authority epoch reset clears the local receipt-pair cache before any new
   epoch can admit a receipt.

This provides exact-command reconciliation in either arrival order while
preserving command ownership and avoiding any client-authored authority.

## Explicit non-goals

The carrier does not publish a legacy packet, snapshot field, demo record,
global event, public cvar, Hook activation, target/attachment, trace,
collision, damage, audio, or visual effect. The legacy protocol and demos
remain unchanged. The whole native event mode remains negotiated and
default-off; clients without it discard the one-shot mailbox transfer rather
than changing legacy behavior.

The remaining `FR-10-T08` work includes the full predictable weapon catalog,
command-time ownership/lease policy, side-effect suppression/presentation
proof, live shadow parity, impairment/load coverage, and correction budgets.

## Validation

Console-only, input-free tests passed:

- `network-local-interaction-authority` verifies mailbox isolation, exact
  duplicate/conflict handling, bounded capacity, transfer, and map reset.
- `network-native-codec` verifies authority-receipt validation plus strict
  native event encode/decode round-trip.
- `network-cgame-event-runtime` verifies the private receipt is admitted,
  delivered to cgame reconciliation, skipped by presentation, and idempotent
  on retransmission.
- `network-native-event-virtual-link` now installs the production cgame event
  runtime into the live private `0x73` virtual netchan path and verifies
  descriptor/DATA/ACK delivery, confirmed/rejected Hook correction in both
  prediction/authority arrival orders, terminal skipping, lost-ACK retry
  idempotence, and final server retirement. Its conflict row proves that a
  retained no-ACK contradictory receipt is cancelled by map-epoch rotation;
  only the fresh descriptor clears cgame resync, and its ACK gates new receipt
  promotion. It also proves receipt-first history loss does not silently evict
  authority evidence: cgame marks resync before the queued ACK reaches the
  server. Details:
  `docs-dev/fr-10-t08-private-hook-authority-receipt-native-virtual-link-2026-07-16.md`.

The current Windows production targets build successfully:
`worr_engine_x86_64`, `worr_ded_engine_x86_64`, `worr_x86_64`,
`worr_ded_x86_64`, `sgame_x86_64`, and `cgame_x86_64`.

The complete registered Windows Meson suite also passes **142/142**, including
the headless-input and isolated-process-policy contracts.

No game process was launched by these checks, so no client input initialization
or mouse capture occurred. After the pre-existing interactive client released
its staged-DLL lock, `refresh_install.py` refreshed and validated `.install/`:
16 root runtime files, one root runtime dependency, 458 packaged assets, 31
botfile payloads, and 215 RmlUi payloads.
