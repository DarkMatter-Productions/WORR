# FR-10-T16 Map-Bootstrap Input-Batch ACK Cancellation

Date: 2026-07-20  
Project task: `FR-10-T16`  
Status: bounded lifecycle defect fixed and focused regressions passing; task-level
acceptance remains open

## Outcome

The private, default-off WNB1 client no longer rejects an authoritative legacy
packet when an old server receipt arrives between `SERVERDATA` reset and the
fresh native-epoch cancellation barrier.

Before this fix, `CL_NativeReadinessPilotServerDataReset` cleared the WNB1 TX
bank and its message-sequence high-water immediately. The server can still
proactively hand off a receipt for the old transport during this bootstrap
window. With the WNB1 bank gone, the client routed that receipt to the clean
schema-1 command bank. The common ACK validator correctly classified the old
sequence as a future ACK for that empty bank, but the production hook then
rejected the complete carrier, including its otherwise valid legacy prefix.

## Lifecycle contract

`input_batch_prepare_map_bootstrap` now preserves the old WNB1 transport
tombstone until the fresh challenge is durably accepted:

- the confirmed/drained state, TX session, slots, sequence high-water, retained
  batch, candidates, and counters remain intact across `SERVERDATA` reset;
- WNB1 same-packet parsing is re-armed from the persistent user request, so a
  clean decline on the prior map cannot disable confirmation on the next map;
- the sideband parser and per-packet `SERVER_ACTIVE` flag are reset for the new
  bootstrap transaction;
- an old receipt before the barrier is parsed against the exact old WNB1 bank
  and always preserves the legacy prefix;
- the existing fresh-challenge transaction stages cancellation of any retained
  WNB1 batch, publishes the monotonic cancelled-through transport epoch only
  after `CLIENT_READY` is queued, and then clears the old batch state; and
- after the barrier, receipts from the old epoch take the stale-cancelled path
  before ACK-bank selection, so they cannot alias a numerically identical
  message in the new WNB1 bank.

Connection close and demo teardown still clear all private state immediately.
The preservation applies only to the bounded same-connection map-bootstrap
window and does not add a public capability or change native authority.

## Regression coverage

`native_client_readiness_pilot_test` now covers both lifecycle edges:

1. Map one ACKs WNB1 batch 1 and retains batch 2. After map quiesce and
   `SERVERDATA` reset, a duplicate old receipt with seven legacy bytes is
   exposed as legacy without mutation or drain. The fresh challenge cancels
   the retained two-command batch and commits the old transport floor. A new
   epoch then activates message 1; a delayed receipt for old message 2 with
   nine legacy bytes is consumed by the floor and leaves the new bank intact.
2. Map one sends a valid `SERVER_ACTIVE` without private WNB1 confirmation,
   producing a clean per-map decline. After rotation, the parser is re-armed;
   the next fresh challenge and same-packet confirmation activate WNB1, and a
   real two-command batch completes its receipt normally.

The current Windows Clang build passes:

```text
native_input_batch_test.exe: ok
native_input_batch_layout_cpp_test.exe: pass
native_server_shadow_pilot_test.exe: ok
native_client_readiness_pilot_test.exe: ok
```

The integrated client/dedicated engine and cgame/sgame build is rerun before
staging and live evidence. No interactive client is used by this verification.

## Scope and remaining work

This closes the identified pre-barrier ACK-routing race and the adjacent
decline-to-next-map parser liveness edge. It does not complete `FR-10-T16`:
production-model rotation/ACK-exhaustion breadth, authenticated server cursor
feedback, native command authority, dual-adapter impairment, load/soak, and
supported-platform gates remain open. No file under `q2proto/` changed.
