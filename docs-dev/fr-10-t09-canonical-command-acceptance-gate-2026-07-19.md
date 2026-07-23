# FR-10-T09 Canonical Command Acceptance Gate

Date: 2026-07-19

Project task: `FR-10-T09`

## Outcome

The direct `FR-10-T09` acceptance criteria pass. WORR has one wrap-safe
canonical command identity, validated duration/sample/render timing, an
authoritative consumed-command watermark distinct from packet acknowledgement,
idempotent bounded intake, and the same command identity at the prediction,
rewind, and event-correlation consumers.

The strategic roadmap requires dependencies to complete first. `FR-10-T05`
closed with the original companion gate and `FR-10-T06` is now complete under
its parent-level acceptance closure. T09 therefore has complete dependencies
and direct acceptance evidence and is closed on 2026-07-19. This transition
increments FR-10 from 5/16 to 6/16 without transferring native-authority,
adaptive-delivery, load, or release obligations from their owning tasks.

## Definition-of-Done evidence

1. **Identity and timing.** The pointer-free command ABI uses explicit
   `{epoch, sequence}` identity, cumulative sample time, bounded duration, and
   validated render-watermark provenance. The new probe covers every duration
   from 0 through 250 ms repeatedly across one million commands and a natural
   sequence-to-epoch rollover.
2. **Authoritative consumed watermark.** The server advances the consumed
   cursor only around authoritative simulation and snapshots publish that
   post-callback cursor. The canonical-required prediction resolver rejects
   packet-ACK substitution. The acceptance gate executes the compiled cursor,
   resolver, production snapshot-link, and replay-boundary contracts.
3. **Idempotence and hostile input.** The probe records 2,100,002 duplicate or
   already-consumed acknowledgements without duplicate simulation. It records
   139,998 bounded conflict, future-gap, wrong-epoch, malformed-duration, stale,
   and rollover rejections while retained cursors/sample time remain valid.
   Existing capacity, alias, flood-distance, and 161/401/4,096-command gap gates
   remain part of the composite evidence.
4. **Legacy and native adapters.** The unchanged legacy signed-setting
   sideband and MOVE adapter map 100,002 commands, including rollover, through
   the canonical stream. The same commands complete 100,002 exact native codec
   round trips and constant-offset semantic comparisons. No `q2proto/` source
   changed.
5. **Shared consumers.** Nine compiled gates cover the command stream, legacy
   adapter, consumed-cursor sideband, canonical prediction resolver,
   authoritative rewind context, local-action prediction/authority correlation,
   native command adapter, and the production native event and snapshot links.
   Prediction, rewind, and event correlation therefore consume the same command
   ID rather than reconstructing transport-local identities.

## Task-level acceptance gate

`network-fr10-t09-acceptance` runs the large probe twice and requires byte-
equivalent structured results before running the nine compiled component and
production-link gates. It writes
`.tmp/networking/fr10_t09_acceptance.json` using schema
`worr.networking.fr10-t09-acceptance-evidence.v1`.

The current focused Windows x86-64 result passes in 457.47 seconds with:

- 1,000,000 canonical commands;
- 100,002 legacy mappings;
- 100,002 native codec round trips;
- 2,100,002 idempotent acknowledgements;
- 139,998 hostile rejections;
- two natural sequence wraps; and
- deterministic digest `a9685d1ac8f40ef6` in both repetitions.

Commands used:

```powershell
meson compile -C builddir-win fr10_t09_command_acceptance_probe command_stream_test legacy_command_adapter_test consumed_cursor_sideband_test prediction_input_range_test command_context_test local_action_audit_test native_command_shadow_test native_event_virtual_link_test native_snapshot_production_virtual_link_test
meson test -C builddir-win --no-rebuild --print-errorlogs network-fr10-t09-acceptance
```

The T09 runner now gives each million-command probe a 420-second child guard
inside the unchanged 900-second Meson gate envelope. This prevents a loaded
Windows host from tripping the former 240-second child cutoff without weakening
the command-count, equality, digest, or compiled-gate assertions. T09 passes in
362.58 seconds inside the earlier serial 166/166 networking suite. The keyed-
POI/event-model revision-2 closeout reruns the unchanged one-million-command
contract inside the final **201/201** suite: digest `a9685d1ac8f40ef6`
remains stable and `.tmp/networking/fr10_t09_acceptance.json` has SHA-256
`8117a68e2a89be704ad8c9c213af286bedc687eb6adf897752148c9e01ed92b9`.

## Scope boundary

This gate does not promote the experimental native transport, change public
capability advertisement, make native snapshots/events authoritative, or close
the load/release work owned by `FR-10-T04`, `FR-10-T08`, `FR-10-T14`, or
`FR-10-T16`. `FR-10-T06` is complete and the separately reviewed T09 evidence
now closes T09, bringing FR-10 to exactly 6/16 at this transition.
