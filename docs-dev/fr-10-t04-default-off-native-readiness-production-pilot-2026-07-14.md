# FR-10-T04 Default-Off Native Readiness Production Pilot

Date: 2026-07-14

Tasks: `FR-10-T04`, `FR-10-T14`

Status: historical readiness-only production slice, later extended by the
one-command DATA/ACK observation linked below. Simulation authority remains
legacy-only and `FR-10-T04` remains Incomplete.

> 2026-07-16 supersession: readiness now carries an independently checksummed
> `snapshot_epoch` in 15 setting pairs. Current atomic sizes are 75 client
> bytes and 135 server bytes; the 65/117-byte figures below describe this
> historical readiness-only revision.

## Delivered boundary

This slice moves the isolated readiness core and signed-setting carrier into
real client/server lifecycles. When explicitly enabled, an eligible connection
proves the exact:

```text
server CHALLENGE -> client CLIENT_READY -> server SERVER_ACTIVE
```

exchange before any future native application transport could be admitted.
Both netchan application hooks are installed so their ownership and teardown
paths are exercised, but every callback is a token-free `BYPASS`. The hooks do
not append, remove, or reinterpret application bytes, and the resulting
readiness gate has no production native DATA consumer.

The strict scope is:

- readiness records only;
- no WTC1 DATA or ACK emission;
- no WNC1 command record on the wire;
- no native command, event, or snapshot consumer;
- no native simulation or cgame authority;
- no `WORR_NET_CAP_NATIVE_ENVELOPE_V1` advertisement or promotion; and
- no changes under `q2proto/`.

Legacy CLC MOVE/BATCH_MOVE and SVC traffic remain the sole authoritative path.
A pilot failure disables only the private experiment and leaves that legacy
path running.

## Opt-in and capability binding

The two experimental controls are:

| Endpoint | Cvar | Default | Sampling point |
| --- | --- | ---: | --- |
| Client | `cl_worr_native_shadow` | `0` | Once, after a fresh `NETCHAN_NEW` and q2proto client context are initialized |
| Server | `sv_worr_native_shadow` | `0` | Once, while accepting an eligible fresh `NETCHAN_NEW` connection |

Changing either cvar during a connection does not reconfigure that connection.
With the default values, the client installs no hooks and the server allocates
no peer adapter, installs no hooks, and emits no readiness bytes.

The official public offer, server support, peer support, and negotiated values
remain exactly:

```text
WORR_NET_CAP_LEGACY_STAGE_MASK = 0x00000003
```

The readiness records are privately bound to:

```text
0x00000003 | WORR_NET_CAP_NATIVE_ENVELOPE_V1 = 0x00000013
```

`0x13` is not placed in the public capability offer or confirmation. During
`SV_New`, the server writes the exact public legacy-only confirmation into the
normal `SERVERDATA`/join bootstrap and flushes that bootstrap reliably. It does
not create a private epoch, emit `CHALLENGE`, or start the private deadline at
that point. The client will accept readiness only after the real capability
parser confirms the exact legacy-only tuple. The server waits for the later,
accepted post-bootstrap `SV_Begin` before recording a pending private challenge
request and its request time. `SV_Begin` never calls the readiness epoch core.
The request does not create an epoch or deadline until the send path reaches a
clean reliable boundary. This prevents the hidden pilot from widening the live
protocol contract or timing out behind bootstrap traffic the client has not yet
observed.

The server allocates its peer only for an exact legacy-stage client offer, a
supported setting-bearing protocol (Q2PRO, R1Q2, or Rerelease), and a valid new
netchan. The client refuses to arm for an old netchan, demo playback, or demo
seeking.

## Wire transaction

Each readiness record is encoded as 13 adjacent signed-setting pairs, including
its version, role-specific kind, private transport epoch, private capability
binding, nonce, record checksum, and commit word. Packet boundaries are part of
the record grammar: a partial tuple may not cross a packet or be interrupted by
another service.

| Direction | Record | Existing carrier | Exact bytes |
| --- | --- | --- | ---: |
| Server to client | `CHALLENGE` | 13 SVC settings, each `1 + 4 + 4` bytes | 117 |
| Client to server | `CLIENT_READY` | 13 CLC settings, each `1 + 2 + 2` bytes | 65 |
| Server to client | `SERVER_ACTIVE` | 13 SVC settings, each `1 + 4 + 4` bytes | 117 |

The complete proof therefore adds exactly 299 carrier bytes across its three
reliable carrier records. The server sign-extends each signed 16-bit carrier
word into the existing 32-bit SVC setting fields. The client uses the existing
q2proto CLC setting writer; production parsing in both directions continues to
use the existing q2proto readers.

The readiness timeout is 10,000 milliseconds. Accepted `SV_Begin` records only
a pending challenge request and its request time; it does not call
`SV_NativeShadowBeginEpochV1()`. The server-side readiness clock starts later,
immediately before the clean-boundary send path hands the newly created and
atomically queued `CHALLENGE` to netchan. The deadline therefore measures an
observable readiness exchange, not time spent delivering `SERVERDATA`, the
gamestate, bootstrap fragments, or an older in-flight reliable.

The distinct pre-start queue wait is bounded to 60,000 milliseconds from the
accepted begin request. If no clean handoff boundary appears in that interval,
the optional pilot fails closed with `SV_NATIVE_SHADOW_FAILURE_QUEUE`; the
legacy connection and simulation continue unaffected. Both adapters extend
their existing 32-bit engine clocks into checked 64-bit monotonic time. A normal
32-bit wrap is accepted under the half-range rule; a regression, ambiguous
half-range gap, or 64-bit exhaustion disables the pilot.

Exact duplicate readiness records do not repeat a state transition and may
reproduce the expected reply. A different role, epoch, nonce, capability
binding, checksum, commit value, or record payload fails closed with respect to
native readiness.

## Queue and transition atomicity

Readiness state may not advance if the complete response cannot be retained.
The production adapters enforce that rule at both endpoints.

On the client, all 13 `CLIENT_READY` settings are first encoded into isolated
scratch storage. The result must be exactly 65 bytes. The reliable netchan queue
is then checked for the full record, followed by one append. The exact boundary
is covered: 64 free bytes produces no partial output and disables the pilot;
65 free bytes commits the complete record.

On the server, each SVC record is staged into an exact 117-byte buffer and
appended once. Accepted `SV_Begin` only creates a pending challenge request and
records its request time. A queued, in-flight, or fragmented reliable is
expected backpressure: the request remains pending, the server does not create
an epoch or readiness deadline, and the private pilot is not disabled.
`SV_SendClientMessages` is the normal service point. It examines the request
only after the client passes rate admission and after any pending fragment has
been completed. A paused active client has no synchronous snapshot pass, so
`SV_SendAsyncPackets` services the same pending transaction while `SV_PAUSED`,
again only after its rate and fragment gates.

At the first clean boundary, `CHALLENGE` capacity is reserved. In the same send
transaction, the readiness core creates the epoch, nonce, record, and 10-second
deadline; the fully staged record is copied atomically into the empty reliable
queue; and the owning send path immediately calls `Netchan_Transmit` with zero
frame payload. The deadline therefore starts immediately before the netchan
handoff. This makes the 117-byte `CHALLENGE` a dedicated reliable generation.
The synchronous path suppresses that client's one snapshot frame; the paused
asynchronous path has no snapshot to suppress. No game frame payload or later
game-start output can precede the challenge in that generation.

The pending pre-start state may wait for a clean boundary for at most 60
seconds. Reaching that bound retires the optional pilot with the typed `QUEUE`
failure and leaves legacy traffic untouched.

An invalid official binding, insufficient capacity at that clean boundary, or
an invalid readiness-core state still fails closed for the private pilot.
Legacy traffic remains authoritative and continues independently; the
default-off configuration never creates the pending request.

`SERVER_ACTIVE` capacity is separately reserved before the final
`CLIENT_READY` commit can transition server readiness. The completed active
record is then queued reliably as one checked transaction. Capacity is checked
against both the current message and the peer's reliable queue. The exact
server boundary is covered independently: 116 free bytes leaves the source and
reliable buffers byte-for-byte unchanged, while 117 free bytes commits the
complete record. The generic append calculation includes a non-empty
current-message prefix, although production `CHALLENGE` creation requires the
clean boundary above. Busy reliable state before that boundary is a deferral,
not a capacity failure.

Encoding, capacity, or append failure detaches the application hooks and
retires the private readiness state. It does not leave a partial carrier record
in the reliable stream and does not affect the already-authoritative legacy
message handling.

## Connection, map, and parser lifecycle

The client pilot owner is process-static rather than part of `client_state_t`,
so `CL_ClearState` cannot invalidate a hook opaque or erase connection-level
readiness during `serverdata`. The server peer is separately heap allocated and
address stable; the disabled path adds only its null pointer in `client_t`.

Map transitions preserve the physical connection while advancing only the
map-local readiness epoch:

1. The client quiesces the current map before the transition. An exact late
   `SERVER_ACTIVE` for the old epoch can still complete the in-flight proof.
2. Parsing `SERVERDATA` clears the official capability-confirmed flag and
   restarts the packet-scoped readiness parser, while retaining the connection
   owner, accepted nonce floor, and old readiness binding.
3. `SV_New` writes the new official legacy capability confirmation into the
   `SERVERDATA`/join bootstrap and flushes it through the normal reliable path.
   The private epoch has not started yet.
4. After the client consumes the complete join/bootstrap stream and sends
   `begin`, the server accepts `SV_Begin`, transitions the client to spawned,
   and records a pending challenge request plus its request time. It does not
   call the readiness epoch core; no private epoch or readiness deadline exists
   yet.
5. If an older reliable is queued, in flight, or fragmented, the request is
   deferred without disabling the pilot. The normal send scheduler retries it
   only after rate admission and fragment completion; while the simulation is
   paused, the asynchronous pass provides the equivalent clean-boundary
   service. If this pre-start wait reaches 60 seconds, the optional pilot fails
   closed with `QUEUE` while the legacy connection continues.
6. At the first clean reliable boundary, the server allocates a process-unique
   private transport epoch and nonce, starts the 10-second readiness deadline,
   atomically queues `CHALLENGE`, and immediately calls `Netchan_Transmit` with
   zero frame payload in the same send transaction. The timer starts
   immediately before this handoff. This dedicated 117-byte reliable generation
   is sent immediately. The synchronous owner suppresses one snapshot frame;
   the paused asynchronous owner has no snapshot frame. Invalid binding,
   capacity, or core state still retires the private pilot.
7. `SERVERDATA` and the public confirmation therefore never share a reliable
   transaction with the next private `CHALLENGE`; bootstrap and older reliable
   delivery consume none of the readiness deadline.

Server owner IDs, private transport epochs, and readiness nonces come from
process-static monotonic counters outside `svs`, preventing reuse across map
changes, slot reuse, and server-state resets during one process lifetime.
Counter exhaustion is terminal for the pilot.

Every CLC and SVC packet supplies explicit begin/end boundaries to the carrier
parser. Reordered, repeated, partial, dangling, overlapping, or interleaved
fields disable the pilot. While a pilot packet parser is active, reserved
readiness settings are consumed as private carrier fields and are not forwarded
to ordinary setting handling; the client also keeps this private SVC range out
of recording and GTV paths when the pilot is disabled.

## Demo, GTV, and teardown rules

The client does not arm the pilot during demo playback or seeking, and the next
pilot lifecycle/parser touch after entering either state retires it. Private
readiness SVC settings are excluded from client demo recording and GTV
forwarding, so this experimental handshake does not create an undeclared demo
or spectator protocol.

Hook opaqueness is cleared before the associated owner or channel can become
invalid:

- the client detaches before final disconnect transmission, reconnect channel
  replacement, and every netchan close;
- the server detaches before per-client final disconnect traffic and before
  global final-message loops; and
- the server destroys and frees the peer before `Netchan_Close`, client cleanup,
  or slot reuse.

Detach and destroy are idempotent. An occupied hook set is never replaced, and
partial hook installation is rolled back before initialization returns.

## Failure model

The pilot is unadvertised, non-authoritative, and deliberately disposable.
Pilot-specific failures silently disable native readiness, clear both hooks,
and continue decoding the rest of the packet through normal legacy paths. This
includes:

- a non-exact official capability binding;
- the 60-second pre-start queue bound or 10-second readiness timeout;
- clock regression or identity exhaustion;
- malformed readiness order, checksum, commit, role, epoch, or nonce;
- an interrupted or dangling packet-scoped tuple;
- hook installation or lifecycle failure; and
- insufficient reliable-queue capacity.

Ordinary q2proto decoding errors and existing authoritative protocol failures
retain their normal connection-drop behavior. Silent pilot retirement is not a
native-to-legacy downgrade: this slice never grants native application
authority in the first place.

## Validation evidence

The current Windows Clang/Meson tree passes the seven focused readiness tests,
7/7:

- `network-native-readiness`;
- `network-native-readiness-layout`;
- `network-native-readiness-sideband`;
- `network-native-readiness-sideband-layout`;
- `network-native-readiness-q2proto-wire`;
- `network-native-server-shadow-pilot`; and
- `network-native-client-readiness-pilot`.

The q2proto oracle checks golden setting opcodes and widths, signed-word
behavior, the fixed challenge checksum, and the complete
`CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE` transition through the real
q2proto SVC/CLC readers and existing CLC writer while independently checking
the production SVC encoding. The adapter harnesses cover default-off behavior,
BYPASS hook ownership, the happy path, exact duplicates, map/serverdata epoch
advance, late old-epoch active handling, demo non-arming, 64/65-byte client
capacity, exact 116/117-byte server capacity, prefixed reliable-message
capacity, server queue-failure retirement, parser
interruption/dangling/double-begin cases, deadline, clock wrap/regression,
occupied hooks, owner non-reuse, and teardown clearing.

The full registered networking suite passes 105/105 and three consecutive
runs pass 315/315. The client and dedicated engine DLLs link with the production
adapters; the complete client engine, dedicated engine, cgame, sgame, client
launcher, and dedicated launcher production set also builds.

Those figures are the exact readiness-pilot baseline. The current integrated
baseline after repeated-carrier and cgame event-runtime follow-ups is 113/113
once and 339/339 across three complete repetitions.

The later production integration now also passes the stable two-process native
shadow gate three times. Each fresh staged `base1` connection completes the
private readiness proof under 25 ms deterministic latency before one
observational command is admitted. The low-rate profile uses 1,500 B/s and a
512-byte netchan ceiling while delivering 12,800 reliable bytes exactly once;
the separate 1,000,000 B/s zero-delay profile deterministically admits three
asynchronous ACK wakes and three handoffs while delivering 6,400 reliable bytes
exactly once. All three complete gates retain one command and release it after
one server match with no pilot failure. Detailed report hashes, counters,
scheduler-stability correction, and limitations are recorded in
`docs-dev/fr-10-t04-native-two-process-async-ack-impairment-gate-2026-07-14.md`.

Current focused validation also passes:

- strict warning-as-error x64 compilation of both adapters, with a separate
  strict i686 compile;
- ASan/UBSan adapter behavior runs;
- Clang static analysis of both adapters; and
- scoped diff checks and fixed-layout/wire checks.

The refreshed `.install/` passes staged validation with 16 root runtime files,
one root runtime dependency, the `basew` tree, a 308-file `pak0.pkz`, 31
botfile package/loose paths, and 214 RmlUi package/loose paths. The staged
runtime smoke passes with 387/387 default-off and 384/384 impaired projected,
published, legacy-compared, and cgame-consumed snapshots, with zero shadow
mismatch, capture/frame failure, or consumer rejection. One initial smoke
invocation ended before impaired live evidence; the clean rerun above is the
accepted result, not repeat or soak evidence. SHA-256 identity checks match all
six current build/stage pairs: both launchers, both engine DLLs, cgame, and
sgame.

`q2proto/` remains unchanged; the new wire oracle links and exercises the
existing library instead of modifying it.

## Historical next production frontier

The next `FR-10-T04` slice should replace `BYPASS` only for the narrowest useful
observational traffic:

1. build one immutable canonical client-command record from the production
   command path;
2. carry exactly one unfragmented 110-byte WNC1 command in one client-to-server
   WTC1 DATA entry after the readiness gate is active;
3. return server-to-client ACK-only WTC1 traffic independently of DATA;
4. feed the admitted native record only to the existing diagnostic comparator
   and join; and
5. keep legacy MOVE/BATCH_MOVE as the sole simulation authority.

That slice must remain default-off and publicly legacy-only. It must prove the
client packet ceiling at the exact 818/819-byte legacy-prefix boundary,
transactional RX session/join/receipt mutation, loss/reorder/duplication,
reconnect and map advance, bandwidth and load limits, demo/spectator exclusion,
and platform parity before DATA/ACK piggyback, events, snapshots, native demos,
capability advertisement, or authority promotion can be considered.

## Production integration update (2026-07-14)

The first five items above are now implemented by the default-off one-command
production observation. This document remains the readiness-only design and
validation record; its statements that production hooks are BYPASS-only or
carry no DATA/ACK are superseded by
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
Public masks remain `0x03`, private readiness remains `0x13`, and legacy
`MOVE`/`BATCH_MOVE` remains the sole authority. The stable two-process gate now
proves the narrow readiness, low-rate reliable/fragment, and high-rate async-ACK
paths described above. Directional readiness-packet loss/reorder/duplication,
load, multi-client, supported-platform, and promotion gates remain open.
