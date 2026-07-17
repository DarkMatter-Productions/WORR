# FR-10-T08: Private Hook Receipt Native Virtual-Link Reconciliation

Task: `FR-10-T08` — full client prediction and reconciliation.

Status: the default-off private Hook authority-receipt carrier now has a
deterministic end-to-end transport proof through the live server native-event
sender, client admission owner, and production cgame reconciliation runtime.
Both authoritative outcomes—Hook confirmed and Hook rejected—are covered.
Both valid arrival orders—prediction first and authority receipt first—are also
covered.
This is an implementation increment only; the parent task remains open.

## Objective

The original receipt-carrier tests proved the server mailbox, event ABI/native
codec, and cgame receiver independently. This increment closes the missing
integration seam: it verifies that the actual event-stream descriptor, data,
receipt ACK, retry, and ordered client admission path reaches the real cgame
runtime and resolves a real locally predicted Hook request rather than a test
double or an unmatched receipt cache entry.

## Implementation

`network-native-event-virtual-link` now links the production cgame event
shadow/runtime, local-interaction shadow, and cgame event-shadow core. Its new
private-control scenario installs `CG_GetEventRuntimeAPI()` as the client
engine consumer, while the established scenarios continue to use their
isolated fake consumer.

The scenario queues one canonical
`WORR_EVENT_TYPE_AUTHORITY_RECEIPT` candidate with a validated 56-byte
`WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1` receipt. It then uses the
real private `0x73` server and client netchan callbacks to:

1. configure a real immutable cgame command-record import with a known
   non-Hook predecessor and a canonical `+hook` request edge, then predict its
   request-pending transaction after descriptor authority is established;
2. deliver and acknowledge the stream descriptor;
3. deliver the ordered control DATA record to the real cgame consumer and
   verify cgame admits one authority record, pairs it with that prediction,
   and records exactly one outcome-correct correction without requesting
   resync;
4. verify audit advancement terminally skips the control record and produces
   no generic authoritative presentation;
5. lose the first event ACK, deliver the retry twice, and prove the receipt is
   not reinserted, corrected again, or duplicated; and
6. deliver the final ACK and prove the server's retained record is retired.
7. after a valid reconciliation, deliver a second valid-but-distinct receipt
   for the same canonical command and prove native admission rejects it,
   scrubs cgame authority, emits no ACK, enters fail-closed drain, and leaves
   the server record retained pending a fresh descriptor epoch; and
8. quiesce the failed map epoch and negotiate a fresh one, then prove its
   new descriptor must be delivered and ACKed before any new event may be
   promoted. That descriptor clears the engine resync latch, after which a
   fresh prediction and receipt reconcile normally and retire through their
   final ACK; and
9. deliver a valid receipt before cgame prediction, then advance canonical
   prediction beyond its exact command. The cgame runtime must surface
   history-loss resync to the engine owner before the queued receipt ACK is
   delivered, leaving the server record retained.

The sequence runs once with the authoritative state rejected and again in a
fresh event epoch with the authoritative state active. The assertions require
exactly one `corrections_rejected` or `corrections_confirmed` increment as
appropriate, no duplicate or unresolved receipt, no resync, and no extra
correction from repeated DATA. It repeats each outcome with authority DATA
delivered before prediction: cgame first retains the receipt as unmatched
evidence, then the later prediction pairs and corrects it exactly once. The
unmatched telemetry intentionally remains as an arrival-order observation; it
is not a failure or an unpaired final state.

The conflict row changes only an otherwise valid immutable receipt hash. The
server therefore exercises normal reliable delivery, while the cgame receipt
cache detects the contradictory authority for its exact command. The client
engine's native admission owner then requires resync and rejects the packet;
the test asserts its retained server record cannot be silently acknowledged or
discarded. A later map epoch cancels that retained conflict rather than
reviving it. Only the new epoch's descriptor resets cgame authority; its ACK
opens the sender's descriptor gate, allowing a new receipt to be promoted and
reconciled exactly once.

Receipt-first delivery remains valid only while the matching canonical command
is retained. The virtual link also proves the opposite boundary: once that
command has fallen behind the prediction range, cgame marks the authority
runtime degraded/resync-required, the engine owner quiesces it, and the server
has not yet received the queued ACK. This preserves the authoritative evidence
for fresh-epoch recovery rather than silently losing it.

The harness remains fully in-process and deterministic. It creates neither a
socket nor a game window, does not initialize client input, and cannot capture
the mouse. The command-record import is a value-copy test source for the cgame
module API; it does not use input hardware, a packet ACK, or mutable engine
command storage. Because changing that import intentionally resets the
interaction cache, the harness installs it after descriptor authority but
before either prediction/receipt ordering begins.

## Scope and safety

No gameplay authority changed. The test does not activate a Hook, create a
target/attachment, issue a trace, change collision or damage, emit sound or
visual effects, add a legacy protocol field, or write a demo record. The
carrier remains negotiated and default-off; legacy packet and presentation
paths remain authoritative.

## Focused validation

The console-only virtual-link executable passed with its established fault
digest unchanged:

```text
native_event_virtual_link_test: ok ... digest=be9724b38fb5f682
native_event_virtual_link_test: lifecycle_diagnostic ...
```

The existing loss, reorder, duplication, directional-corruption, ACK-credit
exhaustion, and epoch-cancellation scenarios remain in the same run. Full
registered validation passed **142/142** tests, including
`network-native-event-virtual-link`, the headless-input contract, and the
headless process-policy contract. The current production target set
(`worr_engine_x86_64`, `worr_ded_engine_x86_64`, `worr_x86_64`,
`worr_ded_x86_64`, `sgame_x86_64`, and `cgame_x86_64`) is up to date.

The required `.install/` refresh initially encountered a pre-existing staged
client lock. The test/build work did not launch, modify, or terminate that
process. After it had released the DLL, the retry succeeded and validated the
Windows stage: 16 root runtime files, one root dependency, 458 packaged asset
files, 31 botfile payloads, and 215 RmlUi payloads.

## Roadmap completion after this round

- Overall strategic roadmap: **68/180 complete (37.8%)**; **112 open**.
- `FR-10`: **3/16 complete (18.75%)**; **13 open**.

This is a narrow validation increment for open `FR-10-T08`; no parent task
checkbox is closed.
