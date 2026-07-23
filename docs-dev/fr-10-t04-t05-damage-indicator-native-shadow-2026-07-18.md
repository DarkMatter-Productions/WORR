# FR-10-T04/T05 Per-Client Damage-Indicator Native Shadow

Date: 2026-07-18

Project tasks: `FR-10-T04`, `FR-10-T05`

## Outcome

The default-off native event path now observes the Rerelease per-client
`svc_damage` presentation carrier at the exact accepted post-snapshot write
boundary. One legacy message becomes an ordered batch of canonical damage
events tied to the same final-emission snapshot as the packet that carries the
legacy bytes. Legacy parsing and the existing damage-display presenter remain
authoritative.

This closes one direct sgame service-family gap. It does not complete either
parent task and does not change public capability advertisement, q2proto,
demos, or rendered behavior.

## Wire and canonical contract

`Worr_LegacyDamageEventDecodeRawV1()` accepts exactly one complete engine-game
carrier:

- `svc_damage` opcode;
- a count from one through `Q2PROTO_MAX_DAMAGE_INDICATORS` (four); and
- one encoded magnitude/type byte plus one packed-direction byte per entry.

Zero/excess counts, truncation, trailing bytes, wrong services, and packed
direction indices outside the shared 162-normal table reject without changing
caller output. The adapter is outside the read-only `q2proto/` subtree.

Each indicator maps to `WORR_EVENT_TYPE_DAMAGE` with
`WORR_EVENT_PAYLOAD_DAMAGE`. The canonical amount preserves the decoded 5-bit
wire/display magnitude rather than pretending the unavailable pre-compression
damage total is known. The three high wire bits map to stable health, armor,
and shield damage flags. Direction is the exact shared packed-normal value;
impulse, impact point, and means of death remain zero because this carrier does
not encode them.

The wire service also carries no attacker identity. Every candidate therefore
uses the stable world reference as source. The exact final snapshot's
controlled-player generation becomes the subject. This distinguishes an
honest missing attacker from the player who received the client-local cue.
Indicator order is retained through `source_ordinal`, and the complete batch is
failure-atomic.

## Production placement

`write_msg()` first confirms the whole unreliable message fits and appends the
legacy bytes with `MSG_WriteData()`. Only then does
`queue_native_damage_indicators()`:

1. decode the exact bounded raw carrier;
2. resolve `client->framenum` to the exact retained final snapshot;
3. build and bind every candidate to that snapshot's controlled identity; and
4. queue the complete batch through the existing descriptor-gated native event
   sender.

Old netchans, disabled or unready native peers, absent/stale snapshots,
malformed messages, capacity failures, and invalid candidate batches remain
legacy-only. Native failure cannot reject or remove an accepted legacy
message.

## Compatibility and security

- Public capability offer/confirmation remains unchanged.
- Legacy server, client, demo, and presentation behavior is unchanged.
- No `q2proto/` source was modified.
- All parsing is exact-length and fixed-capacity; no input-controlled
  allocation was added.
- Unknown canonical damage flag bits now fail event validation.
- No attacker, impact point, impulse, or means-of-death value is fabricated.

## Verification

Focused headless coverage passes 3/3:

- exact multi-indicator decode plus zero/excess/truncated/trailing/wrong-service/
  invalid-direction atomic rejection;
- canonical order, payload mapping, known-flag validation, capacity atomicity,
  exact controlled-subject generation, and stale-snapshot rejection; and
- production source placement after accepted legacy append and before the
  existing generic direct-game observer.

Both production engine DLLs link successfully, followed by the aggregate
production build. The full headless networking suite passes 161/161. The cold
isolated build-plus-test command completes in 599.9 seconds; its serialized
production snapshot corpus row completes in 217.78 seconds. Independent
artifact gates pass 16/16 package tests, 12/12 release-unit tests, and the 1/1
headless bootstrap contract. The final canonical Windows refresh stages 16
root runtime files plus one dependency and validates a 544-member
`basew/pak0.pkz`: 533 repository assets plus 11 hash-audited Q2AAS maps. The
archive contains 31 botfile payloads and 215 RmlUi assets.

Commands used:

```powershell
meson setup .tmp/build-fr10-damage -Dbase-game=basew -Ddefault-game=basew -Dopenal-soft:utils=false -Drmlui=enabled
meson compile -C .tmp/build-fr10-damage legacy_damage_event_candidate_test server_snapshot_event_candidates_test
meson test -C .tmp/build-fr10-damage --no-rebuild --print-errorlogs network-legacy-damage-event-candidate network-server-snapshot-event-candidates network-damage-indicator-source-contract
meson compile -C .tmp/build-fr10-damage worr_engine_x86_64 worr_ded_engine_x86_64
meson test -C .tmp/build-fr10-damage --suite networking --print-errorlogs
meson compile -C .tmp/build-fr10-damage
python -m unittest tools.test_package_assets
python -m unittest discover -s tools/release/tests
meson test -C .tmp/build-fr10-damage --no-rebuild --print-errorlogs release-bootstrap-headless-contract
meson compile -C builddir-win worr_engine_x86_64 worr_ded_engine_x86_64 cgame_x86_64 sgame_x86_64 legacy_damage_event_candidate_test server_snapshot_event_candidates_test
meson test -C builddir-win --no-rebuild --print-errorlogs network-legacy-damage-event-candidate network-server-snapshot-event-candidates network-damage-indicator-source-contract
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
python tools/release/validate_stage.py --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64
```

## Remaining work and accounting

Other direct service families, raw direct-game `svc_sound`, arbitrary non-local
positionless audio, predicted local-action presentation, native presenter
cutover, public advertisement, broad malformed-corpus coverage, sustained load,
real-socket/multi-client profiles, and supported-platform release evidence
remain open.

Roadmap accounting remains **74/190 Definition-of-Done checks = 38.9%** and
**3/16 FR-10 parent tasks = 18.75%**. This slice narrows `FR-10-T04/T05`; it
does not satisfy another complete parent-task acceptance gate.
