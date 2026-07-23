# FR-10-T04/T05 Per-Client Help-Path Native Shadow

Date: 2026-07-19

Project tasks: `FR-10-T04`, `FR-10-T05`

## Outcome

The default-off native event path now observes the Rerelease per-client
`svc_help_path` presentation carrier at the exact accepted post-snapshot write
boundary. Each accepted legacy marker becomes one canonical visual-effect
candidate tied to the same final-emission snapshot as the packet carrying the
legacy bytes. The existing `CL_AddHelpPath` presenter remains authoritative.

This closes one narrow direct-sgame service-family gap. It does not complete
either parent task or change public capability advertisement, q2proto, demos,
or rendered behavior.

## Wire and canonical contract

`Worr_LegacyHelpPathEventDecodeRawV1()` accepts exactly one 15-byte
engine-game carrier:

- the `svc_help_path` opcode;
- a canonical zero/one `start` byte;
- three little-endian float coordinates written by the game import's
  multicast-float position path; and
- one shared packed-normal direction index.

Wrong services, non-canonical booleans, truncation, trailing bytes, non-finite
coordinates, and direction indices outside the 162-entry shared normal table
reject without changing caller output. The adapter lives outside the read-only
`q2proto/` subtree.

The marker maps to `WORR_EVENT_TYPE_VISUAL_EFFECT` and the existing generic
effect payload. Stable schema effect ID
`WORR_EVENT_EFFECT_HELP_PATH_MARKER` distinguishes it from renderer asset
handles; the variant preserves start versus continuation, and origin/direction
preserve the exact decoded carrier values. The carrier has no emitter
identity, so source remains world. Its implicit per-client recipient is left
absent until the exact-snapshot binder supplies the controlled-player
generation. Delivery is transient, authoritative-only, replay-safe, and
present-once.

## Production placement

The shipped sgame `Compass_Update()` emits help-path markers with unreliable
per-client delivery. `write_msg()` first confirms the complete message fits and
appends the authoritative legacy bytes with `MSG_WriteData()`. Only then does
`queue_native_help_path_marker()`:

1. decode the exact raw carrier;
2. resolve `client->framenum` to its retained final-emission snapshot;
3. build the world-positioned marker and bind the snapshot's controlled
   identity as subject; and
4. queue the candidate through the existing descriptor-gated native sender.

Old netchans, disabled or unready native peers, absent/stale snapshots,
malformed carriers, or native queue failure remain legacy-only. Native
observation cannot reject or remove an already accepted legacy marker.

## Compatibility and security

- Public capability offer/confirmation remains unchanged.
- Legacy server, client, demo, and marker presentation behavior is unchanged.
- No `q2proto/` source was modified.
- Parsing is exact-size and allocation-free.
- Non-finite positions and invalid packed normals fail closed to legacy-only.
- No emitter identity or predicted command key is fabricated.

## Verification

Focused headless coverage passes 3/3:

- exact start/continuation, float-position, and packed-direction decode plus
  wrong-service/non-canonical/truncated/trailing/non-finite/invalid-direction
  atomic rejection;
- canonical effect ID/variant/payload mapping, exact controlled-subject
  generation, and stale-snapshot output atomicity; and
- production source placement after accepted legacy append and before the
  generic direct-game observer.

Both production engine DLLs link, followed by the complete aggregate build.
The full isolated headless networking suite passes 163/163 in 450.7 seconds;
the serialized production snapshot corpus is the longest row at 427.31
seconds. Independent artifact gates pass 16/16 package tests, 12/12
release-unit tests, and the 1/1 headless bootstrap contract.

The final canonical Windows relink passes the same three focused contracts.
The required `.install/` refresh stages 16 root runtime files plus one runtime
dependency and independently validates a 544-member `basew/pak0.pkz`: 533
repository assets plus 11 hash-audited Q2AAS maps, including 31 botfile and
215 RmlUi members.

Commands used:

```powershell
meson setup .tmp/build-fr10-help-path -Dbase-game=basew -Ddefault-game=basew -Dopenal-soft:utils=false -Drmlui=enabled
meson compile -C .tmp/build-fr10-help-path legacy_help_path_event_candidate_test server_snapshot_event_candidates_test
meson test -C .tmp/build-fr10-help-path --no-rebuild --print-errorlogs network-legacy-help-path-event-candidate network-server-snapshot-event-candidates network-help-path-source-contract
meson compile -C .tmp/build-fr10-help-path worr_engine_x86_64 worr_ded_engine_x86_64
meson compile -C .tmp/build-fr10-help-path
meson test -C .tmp/build-fr10-help-path --no-rebuild --suite networking --print-errorlogs
python -m unittest tools.test_package_assets
python -m unittest discover -s tools/release/tests
meson test -C .tmp/build-fr10-help-path --no-rebuild --print-errorlogs release-bootstrap-headless-contract
meson setup --reconfigure builddir-win
meson compile -C builddir-win worr_engine_x86_64 worr_ded_engine_x86_64 cgame_x86_64 sgame_x86_64 legacy_help_path_event_candidate_test server_snapshot_event_candidates_test
meson test -C builddir-win --no-rebuild --print-errorlogs network-legacy-help-path-event-candidate network-server-snapshot-event-candidates network-help-path-source-contract
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
python tools/release/validate_stage.py --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64
```

## Remaining work and accounting

POI keyed state, fog/layout/inventory state, raw direct-game `svc_sound`,
arbitrary non-local positionless audio, predicted local-action presentation,
native presenter cutover, public advertisement, broad malformed corpora,
sustained load, real-socket/multi-client profiles, and supported-platform
release evidence remain open.

Roadmap accounting remains **74/190 Definition-of-Done checks = 38.9%** and
**3/16 FR-10 parent tasks = 18.75%**. This slice narrows `FR-10-T04/T05`; it
does not satisfy another complete parent-task acceptance gate.
