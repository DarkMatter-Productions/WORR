# Progressive Networking Controls

WORR is introducing its newer snapshot, prediction, event, and lag-compensation
systems in stages. The normal compatibility path remains in charge by default.
You do not need to change any of the controls on this page to join or host a
server.

This guide is for server operators evaluating the new lag-compensation path and
for testers comparing the newer snapshot timeline with the established client
renderer. Features labelled **test only** should not be put into a public
server config yet.

## Safe Defaults and Compatibility

| Control | Default | Normal behavior |
| --- | ---: | --- |
| `sv_snapshot_shadow` | `1` | Observe outgoing snapshots without changing the packet sent to a client. |
| `cg_snapshot_timeline_render` | `0` | Keep the established client renderer authoritative. |
| `cl_snapshot_recovery` | `0` | Keep full-snapshot requests on the established legacy invalid-frame path only. |
| `cl_adaptive_input` | `0` | Keep the established `cl_maxpackets` and `cl_packetdup` input-delivery policy. |
| `cl_worr_native_shadow` | `0` | Do not send the client half of the native command-shadow diagnostic. |
| `sv_worr_native_shadow` | `0` | Do not enable the server half of the native command-shadow diagnostic. |
| `cl_worr_native_input_batch` | `0` | Do not request the private multi-command native input-shadow test. |
| `sv_worr_native_input_batch` | `0` | Do not allow the private multi-command native input-shadow test. |
| `cl_worr_native_event_shadow` | `0` | Do not receive the client half of the native event-stream diagnostic. |
| `sv_worr_native_event_shadow` | `0` | Do not emit the server half of the native event-stream diagnostic. |
| `cl_worr_native_snapshot_shadow` | `0` | Do not receive the client half of the native canonical-snapshot diagnostic. |
| `sv_worr_native_snapshot_shadow` | `0` | Do not emit the server half of the native canonical-snapshot diagnostic. |
| `g_lag_compensation` | `0` | Keep server-authoritative weapon traces in the current world. |
| `sg_lag_compensation_debug` | `0` | Do not collect or print rewind diagnostics. |
| `net_impair_enable` | `0` | Do not deliberately damage or delay network traffic. |

Legacy Quake II clients, servers, and demos continue to use their established
packet and presentation paths. If a progressive snapshot, command, or rewind
proof is missing or rejected, WORR falls back to a safe compatible path; it
does not trust a guessed timestamp. Client render promotion also falls back one
entity at a time instead of dropping the whole scene.

The following work is still in progress:

- the native progressive wire envelope is limited to the default-off command,
  event, snapshot, and combined diagnostic bundles described below; these do
  not constitute full native-netcode or release promotion;
- native canonical events can reach the cgame audit runtime, but live effects
  and audio are still presented by the established event path;
- client gameplay and weapon prediction are not fully promoted;
- adaptive input pacing is available as an opt-in evaluation path for the
  established batched-command transport, and a separate private native batch
  shadow is available for developers; neither is release-promoted;
- rewind covers the eight historical query policies listed below and bounded
  canonical brush-mover scenes, but broader mover, lifecycle, projectile,
  melee, splash, and multi-target fairness coverage is not complete; and
- multiplayer load, fairness, demo/spectator, and cross-platform release gates
  are not complete.

For those reasons, snapshot render promotion and historical hit validation
remain explicit opt-ins.

## Native Command, Event, and Snapshot Shadows (Developer Diagnostic Only)

`cl_worr_native_shadow` and `sv_worr_native_shadow` enable the client and
server halves of a small native-wire diagnostic. Both default to `0`, and both
endpoints must opt in before connecting. The diagnostic runs only on compatible
NEW-netchan connections.

When enabled successfully, WORR sends a bounded, repeated sample of canonical
input commands beside the normal legacy movement stream and compares both at
the server. The legacy command is still sent, received, and used for gameplay.
This pilot does not improve ping, bandwidth, prediction, or hit registration,
and it does not enable the planned full native netcode.

The connection advertises one exact capability bundle based on the controls
that were set before connecting:

| Controls enabled at an endpoint | Exact capability mask |
| --- | ---: |
| Base native shadow disabled | `0x03` (established compatibility path) |
| Base native shadow only | `0x53` (command diagnostic) |
| Base + event shadows | `0x73` (command and event diagnostics) |
| Base + snapshot shadows | `0x57` (command and snapshot diagnostics) |
| Base + event + snapshot shadows | `0x77` (combined diagnostics) |

Both endpoints must select the same armed bundle. A mode mismatch, partial
intersection, unavailable native endpoint, or failed readiness check falls
back to exact `0x03`; WORR does not guess a partial native format. The choice
is frozen for that connection, so reconnect after changing any of these
controls.

`cl_worr_native_event_shadow` and `sv_worr_native_event_shadow` add a separate
server-to-client event-stream test on top of that command shadow. Both event
controls and both base native-shadow controls must be enabled before connecting.
The server must also retain its normal snapshot observation
(`sv_snapshot_shadow 1`). The server copies exact per-client events from final
snapshot output, retains them until acknowledged, and submits them to the
client's cgame event audit runtime. Established snapshot events remain in the
packet and remain responsible for visible effects and audio.

`cl_worr_native_snapshot_shadow` and `sv_worr_native_snapshot_shadow` add the
canonical full-snapshot diagnostic instead. Both snapshot controls and both
base native-shadow controls must be enabled before connecting, and the server
must keep `sv_snapshot_shadow 1`. Established snapshots continue to be sent.
These controls alone do not promote the native timeline, effects, audio, or
gameplay authority; the narrow presentation modes used by developers have
separate controls and are not release-ready.

Enabling both the event and snapshot pairs selects the combined `0x77` test
bundle. Its event and snapshot lanes share one bounded carrier, but it remains
default-off diagnostic traffic rather than completed `FR-10-T04` transport or
`FR-10-T15` release acceptance.

For a controlled local or private test, set the server side before accepting the
connection:

```text
set sv_worr_native_shadow 1
set sv_worr_native_event_shadow 1
```

Set the client side before connecting:

```text
set cl_worr_native_shadow 1
set cl_worr_native_event_shadow 1
```

That example selects event bundle `0x73`. For snapshot-only bundle `0x57`,
leave both event controls at `0` and set
`sv_worr_native_snapshot_shadow 1` plus
`cl_worr_native_snapshot_shadow 1`. For combined bundle `0x77`, set both event
controls and both snapshot controls to `1`. The base native-shadow pair is
required in every native mode.

These controls apply when a connection is created. Reconnect after changing
any native-shadow control; changing a value during an existing connection does
not reconfigure that connection. If the peer is incompatible or the diagnostic
encounters an error, it silently drains and falls back while the established
command path continues. With either base native-shadow control left at its
default `0`, normal legacy networking and demo behavior are unchanged.

Leave both event controls and both snapshot controls at `0` when testing only
the command shadow. A client and server that select different native modes
fall back safely to established networking; they do not guess a shared format.

Use `cl_worr_native_shadow_status` on the client or
`sv_worr_native_shadow_status` on the server when diagnosing a map transition.
The `cancelled_through_epoch` value is the newest old test epoch that has been
closed. The `cancellation_barriers`, `cancelled_*`, and
`stale_cancelled_carriers` counters show unfinished shadow work that was
discarded safely or delayed old shadow packets that were ignored. The
`stale_cancelled_readiness_records` counter separately records valid but late
shadow handshake messages. These are diagnostic dispositions, not lost legacy
gameplay commands, snapshots, or visible events. Malformed or corrupt old
messages still fail the shadow test rather than being accepted as stale.
On the server, `transport_epoch` is the newest handshake being evaluated, while
`wire_committed_transport_epoch` is the epoch whose native test-wire state has
actually crossed its point of no return. They can differ briefly during a map
change; that is expected and makes the transition explicit.

### Private Native Input Batch Shadow

`cl_worr_native_input_batch` and `sv_worr_native_input_batch` add a developer-
only multi-command test to the command shadow. Both default to `0`. It works
only with the base command-only native shadow: leave both event and snapshot
shadow pairs disabled, enable both base native-shadow controls, and enable the
matching input-batch control at each endpoint before connecting.

For a controlled test server:

```text
set sv_worr_native_shadow 1
set sv_worr_native_input_batch 1
```

On the client, before connecting:

```text
set cl_worr_native_shadow 1
set cl_worr_native_input_batch 1
```

This still selects the existing command diagnostic bundle `0x53`; it does not
advertise a new public capability. When the private confirmation succeeds, the
client can send two through eight fresh canonical commands together beside the
normal movement stream. The normal commands stay in the packet and remain in
charge of gameplay. The server acknowledges the test batch only after every
native command exactly matches its normal counterpart.

Reconnect after changing either input-batch control. If confirmation space is
unavailable, a peer does not support the test, or the private batch is malformed
or expires, WORR declines or drains only this diagnostic and keeps established
input delivery available. Do not use this test as a ping, bandwidth, prediction,
or hit-registration improvement.

Use `cl_worr_native_input_delivery_status` to inspect the client request,
confirmation, candidate range, retained batch, retry, acknowledgement, fallback,
and failure counters. `sv_worr_native_shadow_status [slot]` prints the matching
per-client server input-delivery row, including pending legacy joins and whether
its test acknowledgement is blocked. Both commands are diagnostic only.

## Server Snapshot Observation

`sv_snapshot_shadow` controls a server-side copy of the final snapshot endpoint
prepared for each client.

- `1` (default) creates the observation copy.
- `0` disables creation of that copy.

The shadow is not a second packet and does not replace the existing protocol.
Failure to create or update it cannot reject an otherwise valid outgoing
snapshot. Disabling it removes diagnostic and future progressive state only;
it is not a bandwidth or gameplay switch.

Set this cvar before clients connect when you want a clean test. An existing
client's shadow is initialized with its gamestate and can remain allocated
until that peer is reinitialized or disconnects.

## Client Snapshot Timeline

`cg_snapshot_timeline_render` is a non-persistent testing control. It returns to
`0` when the client is restarted.

- `0` — established rendering remains authoritative (default).
- `1` — **audit mode**. Sample the canonical snapshot timeline, compare it with
  established interpolation, and keep the established transform.
- `2` — **promotion mode, test only**. Use a canonical remote-entity transform
  only when it passes the timeline safety checks and matches established
  interpolation within the configured tolerance. Every rejected entity falls
  back independently.
- `3` — **native timeline authority, test only**. Once the engine establishes
  native timeline ownership, enumerate remote entities from immutable
  canonical snapshots and keep previous-only entities visible until their
  removal boundary. Losing ownership after that bind produces an empty native
  frame instead of silently mixing in established packet entities.

When the canonical snapshot consumer is active, modes `1` and `2` print one
aggregate `cg_snapshot_timeline_render: ...` line per second. They do not print
a line for every entity. Mode `3` also prints a compact
`cg_snapshot_timeline_adaptive: ...` row. The predicted local player is never
switched by this control. Modes `1` and `2` do not extrapolate; mode `3` uses
the bounded controls below.

`cg_snapshot_timeline_render_epsilon` sets the largest accepted per-axis origin,
beam-end, or angular difference at the promotion gate. Its default is `0.125`;
values are limited to `0.0001` through `8.0`. Raising it makes the comparison
less strict and is not recommended for ordinary parity testing.

Mode `3` uses these non-persistent timing controls:

- `cg_snapshot_timeline_interpolation_delay_ms` sets the baseline delay and
  defaults to `50` ms (range `0`–`1000`).
- `cg_snapshot_timeline_adaptive_interpolation` defaults to `1`. It raises the
  delay from accepted arrival jitter and recent extrapolation/clamp pressure,
  then recovers toward the baseline during stable delivery.
- `cg_snapshot_timeline_max_interpolation_delay_ms` caps that adaptive delay
  and defaults to `150` ms (range `0`–`1000`, never below the baseline).
- `cg_snapshot_timeline_max_extrapolation_ms` defaults to `50` ms (range
  `0`–`250`). Set it to `0` to disable extrapolation.

Generation replacement, missing components, snapshot discontinuities,
teleports, and excessive linear or angular speed always block blending even
when extrapolation is enabled. Pause and rate changes advance through the
canonical clock; legacy demos keep established whole-stream rendering.

To run a short client-side audit:

```text
cg_snapshot_timeline_render 1
```

Watch the console for parity mismatches, discontinuities, pair failures, and
maximum errors. Return to the normal path with:

```text
cg_snapshot_timeline_render 0
```

Modes `2` and `3` are intended only for controlled playtests after mode `1` is
clean. Neither enables canonical event effects or full gameplay prediction.

## Client Full-Snapshot Recovery

`cl_snapshot_recovery` is an archived client opt-in and defaults to `0`.

- `0` — use the established recovery path. An invalid or unavailable delta
  base still makes the client ask for a full snapshot automatically.
- `1` — also allow three consecutive canonical snapshot projection or parity
  failures to request a full snapshot through the existing protocol. Requests
  use bounded bursts and a short cooldown so one fault cannot create an
  unrestricted request loop.

This control does not enable a new wire protocol, and it cannot weaken normal
invalid-frame recovery. It is intended for controlled snapshot-shadow testing
while the wider impairment and long-session release gates remain open.

Use `cl_snapshot_recovery_status` to print the current reason flags, failure
streaks, request generation, burst/cooldown state, applied requests, successful
recoveries, and fallback counters. `cl_snapshot_recovery_debug 1` additionally
prints each arm and applied request; leave it at its default `0` outside a
short investigation.

Use `cl_snapshot_shadow_status` to check the underlying canonical snapshot
audit directly. A healthy connected session reports an active shadow, live
projection and comparison counts, and zero parity mismatches. The command is
diagnostic only: it does not enable rendering, recovery, or a new protocol.

## Adaptive Input Delivery

`cl_adaptive_input` is an archived client opt-in and defaults to `0`.

- `0` — keep the established `cl_maxpackets` pacing and `cl_packetdup` backup
  policy.
- `1` — on the existing batched-command path, evaluate recent loss, RTT change,
  queued input, acknowledgement backlog, and the configured rate to select a
  bounded send interval and preserve or raise the configured number of previous
  command batches within the existing transport limit.

The controller does not change the non-batched three-command compatibility
path, does not enable a new protocol, and never changes which command identity
the server acknowledges. A `cl_maxpackets` value of `0` remains unlimited.
Invalid observations fall back locally to the established policy.

Use `cl_adaptive_input_status` to print the current interval, redundancy, loss,
RTT/jitter estimate, queue pressure, reason flags, and fallback counters. This
control remains for evaluation while impairment, bandwidth, correction, and
native-adapter gates are completed.

## Historical Player Hit Validation

`g_lag_compensation` is the compatibility master switch in the server game
module:

- `0` — use current-world collision (default and current release-safe choice);
- `1` — allow the historical player-bounds path for eligible deathmatch weapon
  traces.

This is a server-authoritative feature. A client sends its ordinary input and
acknowledgement data, but it cannot supply a target, a hit result, or an
arbitrary rewind time. The server validates the command identity and timing,
chooses the eligible historical instant, constructs the collision scene, and
runs the trace. If a canonical command is rejected, the server does not retry
it using the weaker legacy acknowledgement estimate.

### Supported Historical Query Policies

These are the only weapon policies that currently use historical player
collision:

| Policy | Historical part | Current-authority part |
| --- | --- | --- |
| Machinegun | Muzzle-convergence and bullet trace. | Normal bullet damage, knockback, effects, and target state. |
| Chaingun | Muzzle-convergence and each bullet trace. | Normal burst cadence and each bullet's damage and effects. |
| Shotgun | Muzzle-convergence and pellet traces. | Normal pellet damage, knockback, and effects. |
| Super Shotgun | Muzzle-convergence and both barrels' pellet traces. | Normal damage, knockback, and effects from both barrels. |
| Railgun | Muzzle-convergence and the piercing ray, including generation-checked per-ray ignores. | Normal damage and effects after each trace result. |
| Disruptor convergence | Point target query followed, when needed, by the expanded convergence query at the same historical instant. | Projectile movement, contact, delayed damage, and lifetime. |
| Plasma Beam | Main beam query and its water-excluding retrace. | Beam cadence, damage, water effects, and target state. |
| Thunderbolt | Main ray, water-excluding retrace, and paired side rays as one de-duplicated footprint. | Damage, cadence, effects, and the separate underwater-discharge branch. |

Static map collision remains authoritative. Canonical scenes can include
authenticated history for live `SOLID_BSP` brush movers; legacy rewind keeps
non-player collision in the current world. Broader mover and multi-target
fairness coverage is still in progress. Bots are not treated as rewound
shooters.

### Fairness, Lifecycle, and Fallback

The configured rewind window defaults to 200 ms. The server clamps it to a
hard range of `0..250` ms, so neither interpolation allowance nor client data
can extend a query beyond 250 ms. Setting the window to `0` prevents historical
queries even if the master switch is `1`.

Historical geometry is used only to answer the collision query. It is not a
historical damage simulation. Once the trace returns, the normal server weapon
code evaluates the current, generation-matched entity. Current health, armor,
team rules, spawn protection, damageability, knockback, death, effects, and
respawn state remain authoritative. A respawn begins a new identity generation
and clears the previous life's history, so an old pose cannot stand in for the
new life. Spectators are excluded from eligible shooter and target collision
history.

WORR does not interpolate across a teleport, map change/reset, identity or
life-generation change, death/respawn boundary, or another marked history
discontinuity. If the server cannot prove a safe pose on the requested side of
that boundary, or if required history, mover data, scene sealing, or command
authority is missing, the affected query uses an uncompensated current-world
trace. It does not guess a time, keep a partial rewound scene, or accept a
client-declared hit.

The supporting policy controls are:

| Control | Default | Meaning |
| --- | ---: | --- |
| `sg_lag_compensation_max_ms` | `200` | Maximum historical window. The server enforces a `250` ms hard cap; `0` prevents historical rewind even when the master switch is on. |
| `sg_lag_compensation_interp_ms` | `-1` | Additional interpolation allowance. `-1` derives it from the estimated snapshot interval; a non-negative value supplies an explicit allowance limited by `sg_lag_compensation_max_ms`. |
| `sg_lag_compensation_legacy_error_ms` | `50` | Largest accepted error bound for a legacy packet-shared time mapping, limited to `0..250` ms. Lower values fail closed more readily. |
| `sg_lag_compensation_debug` | `0` | Diagnostic level described below. |

Debug levels are cumulative:

- `0` — no aggregate diagnostics, trace timing, authority fingerprinting, or
  observation journal writes.
- `1` — print one aggregate `lagcomp: ...` status line per second while the
  trace path is active.
- `2` — also retain a bounded 256-record detailed query journal for diagnostic
  tooling, without printing every query to the console. New records overwrite
  the oldest retained records when the journal is full.
- `3` or greater — also print one detailed `lagcomp trace ...` line for each
  observed query. Use this only for short investigations; busy weapon traces
  can produce substantial console output.

The aggregate line counts accepted sessions, canonical acceptance/rejection,
missing history, discontinuities, caps, scene work, and mover/brush activity.
At level `2`, the retained journal and its telemetry distinguish canonical,
bounded legacy, and current-world paths and their fallback outcomes for
diagnostic tooling. A detailed line includes the weapon policy, selected path,
outcome, fallback and policy reason, requested and applied times, candidate
count, and the collision-authority guard. These values are diagnostic evidence;
they cannot enable compensation or change a hit result.

A controlled evaluation config can begin with:

```text
set g_lag_compensation 1
set sg_lag_compensation_max_ms 200
set sg_lag_compensation_interp_ms -1
set sg_lag_compensation_legacy_error_ms 50
set sg_lag_compensation_debug 1
```

Test every enabled ruleset and weapon mix before considering broader use. To
return immediately to current-world collision:

```text
g_lag_compensation 0
sg_lag_compensation_debug 0
```

`g_lag_compensation 0` is the server-wide opt-out and takes the historical path
out of consideration for every client. `sg_lag_compensation_max_ms 0` is an
additional server-side disable while retaining the master setting. No client
setting can override either control.

Historical hit validation is still default-off because its full fairness,
abuse, moving-geometry, sustained-load, and multi-platform release evidence is
not complete.

## Deliberate Network Impairment (Developer Test Only)

The `net_impair_*` controls deliberately delay, drop, duplicate, reorder, or
corrupt traffic for repeatable lab tests. They are not connection-tuning
options. Never enable them on an active public server or on a client whose
connection you need to preserve.

All impairment cvars are non-persistent. Changing a profile parameter clears
queued test packets and restarts the deterministic streams. The same seed and
traffic pattern are intended to reproduce the same impairment decisions.

| Control | Default | Range / purpose |
| --- | ---: | --- |
| `net_impair_enable` | `0` | `0` or `1`; master test switch. |
| `net_impair_seed` | `1` | Deterministic 32-bit seed. |
| `net_impair_latency_ms` | `0` | `0..2000` ms one-way base delay. |
| `net_impair_jitter_ms` | `0` | `0..2000` ms signed jitter range. |
| `net_impair_loss_pct` | `0` | `0..100` percent independent loss. |
| `net_impair_burst_loss_pct` | `0` | `0..100` percent burst-start chance. |
| `net_impair_burst_length` | `3` | `1..64` packets in a burst. |
| `net_impair_reorder_pct` | `0` | `0..100` percent extra-hold/reorder chance. |
| `net_impair_duplicate_pct` | `0` | `0..100` percent duplicate chance. |
| `net_impair_corrupt_pct` | `0` | `0..100` percent deterministic one-bit corruption chance. |
| `net_impair_upstream_stall_ms` | `0` | `0..2000` ms extra delay for sequenced client-to-server datagrams. |
| `net_impair_rate_kbps` | `0` | `0..1000000` kbit/s; `0` disables test throttling. |
| `net_impair_queue_limit` | `1024` | `1..1024` active delayed-packet slots. |

A small local latency test looks like this:

```text
net_impair_seed 12345
net_impair_latency_ms 50
net_impair_jitter_ms 10
net_impair_enable 1
net_impair_status
```

Disable the link model and clear its queued packets and counters when finished:

```text
net_impair_enable 0
net_impair_reset
```

`net_impair_status` prints the active profile, queue occupancy, high-water
mark, resets, overflow, and feature counters. `net_impair_reset` clears queued
packets and restarts the current deterministic profile. The
`net_impair_queue_selftest` command is an engine-development invariant test,
not a playtest command; it intentionally fills the configured synthetic queue
and raises a fatal error if the invariant fails.

## What to Include in a Test Report

When reporting a progressive networking issue, include:

- client and server build identifiers and operating systems;
- protocol selected, map, ruleset, player count, and whether a demo was being
  recorded or played;
- the exact values of the controls changed from their defaults;
- relevant `cg_snapshot_timeline_render:`, `cl_snapshot_shadow_status`,
  `cl_snapshot_recovery_status`,
  `lagcomp:`, `lagcomp trace`, or `net_impair_status` output; and
- a short reproduction sequence, including the weapon and whether a mover,
  teleport, death/respawn, or reconnect was involved.

Do not include passwords, private addresses, authentication tokens, or other
secrets in a public report.
