# FR-10 keyed `svc_poi` native-shadow slice

Date: 2026-07-20

Project tasks: `FR-10-T04`, `FR-10-T05`, `FR-10-T07`

Status: bounded default-off slice complete; no roadmap task closes here

## Outcome

The exact keyed Rerelease `svc_poi` action now has one canonical value from
legacy server capture through the native event journal, client/cgame parity
audit, and the two-phase cgame presenter. The implementation preserves the
legacy service as presentation authority and does not change its bytes or
delivery. Native capture is observational and default-off: a failure can
discard or drain only the private native shadow after the authoritative legacy
write has succeeded.

The slice deliberately covers keyed POIs only. Legacy key zero is the unkeyed
transient form and remains on the legacy-only path.

## Canonical event contract

`WORR_EVENT_PAYLOAD_KEYED_POI_V1` is stable payload catalog ID `13`. Its fixed
20-byte little-endian value is:

| Offset | Field | Width |
|---:|---|---:|
| 0 | `key` | 2 bytes |
| 2 | `lifetime_ms` | 2 bytes |
| 4 | `position[3]` | 12 bytes |
| 16 | `image_index` | 2 bytes |
| 18 | `color_index` | 1 byte |
| 19 | `flags` | 1 byte |

The event ABI remains version 1, while `WORR_EVENT_MODEL_REVISION` advances
from 1 to 2 because payload catalog membership and semantic hashing changed.
The native codec encodes and decodes the fields explicitly rather than copying
host struct bytes. C and C++ layout assertions lock the size, offsets, payload
ID, and model revision.

The accepted canonical shape is an authoritative-only `STATE_CHANGE` action
with `REPLAY_SAFE | PRESENT_ONCE`. Key zero, non-finite positions, unknown POI
flags, noncanonical removal fields, persistent-state delivery, cosmetic
delivery, and unrelated record flags reject. `HIDE_ON_AIM` is currently the
only recognized POI flag.

Lifetime has the legacy meanings:

- `0` means an infinite keyed POI;
- `1..65534` means a finite lifetime in milliseconds;
- `65535` (`UINT16_MAX`) means remove that key, and position, image, colour,
  and flags are canonical zeroes.

The journal does not coalesce keyed values and does not turn them into
`PERSISTENT_STATE` records. They remain presentation actions. Repeated actions
for one key therefore retain separate event identities and FIFO order, then
the cgame sink applies each action to its keyed presentation state after the
present-once boundary.

## Exact legacy adapter and server capture

The shared adapter accepts exactly one 21-byte carrier: the `svc_poi` opcode
followed by the 20-byte value. Truncated input, trailing bytes, another opcode,
key zero, non-finite coordinates, or unknown flags reject transactionally.
The template uses stable world source `{0,1}`, absent subject, no event ID, and
no prediction key. The exact final-emission snapshot binder supplies the
controlled-player subject generation and sets `SNAPSHOT_FENCED`.

The server observes the two legacy delivery paths at their existing points of
no return:

- Reliable `svc_poi` is decoded only after the authoritative reliable append.
  It enters the same bounded discriminated FIFO as reliable temp/muzzle game-
  event sequences. The FIFO preserves game-event/POI cross-family append order,
  has 32 entries and the shared 512-record backlog bound, and is cleared at the
  declared map lifecycle boundary. Each pending entry binds once to the next
  exact committed per-client snapshot. Same-key POIs are not replaced in this
  FIFO.
- Unreliable `svc_poi` is observed only after the complete message fits the
  post-snapshot datagram. Lifetime zero is rejected because an unreliable
  infinite state could disappear permanently. Every other 16-bit lifetime,
  including the `UINT16_MAX` removal action, is legal. The bound record becomes
  `TRANSIENT` with `expiry_tick = source_tick + 1`; `source_tick == UINT32_MAX`
  rejects instead of wrapping.
- Reliable finite, zero-lifetime, and removal forms remain
  `RELIABLE_ORDERED` with `expiry_tick = 0`.

Both paths use world as source because `svc_poi` encodes no emitter. The exact
controlled entity is the subject because the service is addressed to that
client. Capture or binding failure never rolls back, replaces, or edits the
already authoritative legacy message.

The source-placement contract binds the production objective, team-pickup,
dropped-item, and point-ping writers to these seams. It also proves reliable
capture occurs after append, unreliable capture after successful fit, and
reliable FIFO flush after exact final-snapshot commit.

No file under `q2proto/` changed. That tree remains the read-only legacy wire
adapter.

## Client/cgame raw lane and adjacent snapshot fences

The client action range adds keyed POI carrier kind 7. Raw capture remains
keyed-only and occurs before the legacy presenter; the legacy call still owns
the visible result. The native presenter/probe also uses kind 7.

Reliable delivery creates an intentional adjacent-fence case. A raw reliable
service is parsed against the retained frame current at that action, while its
server native copy waits in the reliable FIFO and binds to the following exact
committed frame. The raw and native records can therefore have adjacent source
ticks, different snapshot sequences and hashes, and a different controlled-
entity generation without representing different POI presentation.

Parity handles that ordering explicitly instead of weakening snapshot
validation:

1. The raw lane independently resolves `{active epoch, raw source tick + 1}`
   and requires that retained snapshot's tick and controlled identity.
2. The native lane independently resolves its exact event fence, tick, time,
   and controlled identity.
3. Each record must still use world source `{0,1}` and the subject must equal
   the controlled identity in its own retained snapshot.
4. Only after both lane-local fences validate does POI presentation parity
   compare a validator-valid neutral record. The neutral record retains the
   exact payload and event model/schema/type, but clears authority identity,
   routing, prediction, tick, ordinal, time, expiry, and subject details and
   uses reliable ordered delivery.
5. The parity domain hashes the shared map epoch and neutral semantic value.
   It deliberately does not claim equality of the adjacent snapshot sequence,
   snapshot hash, or generation.

Payload changes and map-epoch changes still produce different parity hashes.
Two consecutive same-key actions remain two ordered raw tokens and two ordered
native presentations.

## Two-phase cgame presentation

The real cgame keyed-POI sink follows the existing synchronous two-phase event
contract:

- `CanPresent` is side-effect-free. It validates the copied payload and exact
  fence, chooses an existing/free slot, calculates finite or infinite expiry,
  and copies any already-precached image name and dimensions into a prepared
  value. It neither registers a resource nor mutates POI state.
- The event journal makes its present-once decision only after preflight
  succeeds.
- `Present` consumes only the prepared value and commits the selected update or
  exact-key deletion. Runtime callbacks cannot reenter or replace either phase.

The sink reproduces the legacy state behavior, including less obvious no-op
and resource cases:

- a disabled POI cvar does not mutate state;
- an all-keyed full table cannot evict an unrelated keyed entry;
- deleting an absent key is a no-op, and deleting a present key clears only
  that slot;
- lifetime zero records explicit infinite state rather than relying on an
  already-expired timestamp;
- an unavailable image lookup is not a failed POI action. Legacy `CG_AddPOI`
  still stores the key, lifetime, position, image index, colour, and flags but
  leaves name/dimensions empty so the entry is invisible. Native preflight now
  returns a successful upsert plan with that same empty image metadata, and
  commit retains the state. A later removal therefore still finds the key.

The cgame import API advances to `CGAME_API_VERSION 2029` to expose a read-only
precache image-information query; it does not add presentation-time resource
registration.

## Probe and presenter schemas

- `WORR_CGAME_NATIVE_EVENT_PROBE_STATUS_VERSION` is 3.
- Probe status is fixed at 336 bytes with eight kind slots (`NONE` plus kinds
  1 through 7); keyed POI is kind 7.
- The real presenter status is version 2 with eight kind slots, and keyed POI
  is presenter kind 7.
- The client V2 carrier catalog has keyed POI kind 7 and seven nonzero carrier
  kinds.

The separate event-effect authority gate remains dormant/default-off and is
not armed merely because native transport becomes ACTIVE. The map-latched
preflight probe can exercise the exact value path without dispatching a native
effect. Raw legacy POI presentation and ordinary rendering remain authoritative
unless a later ownership slice explicitly cuts them over.

## Verification recorded for this slice

Focused headless revalidation passed for the affected contracts, including:

- `network-event-journal`, `network-native-codec`, and both native/event C/C++
  layout rows;
- `network-legacy-keyed-poi-event-candidate` and
  `network-keyed-poi-source-contract`;
- `network-server-snapshot-event-candidates` and
  `network-native-server-shadow-pilot`;
- the server map-quiesce, challenge-barrier, and reliable-game-event source
  contracts;
- `network-cgame-event-shadow`, `network-cgame-event-presentation`, both probe
  layout rows, `network-cgame-native-event-presenter`, and
  `network-native-event-presenter-source-contract`;
- `network-native-event-virtual-link` and the canonical native runtime parser.

The post-closure `FR-10-T05` parent now includes `legacy-keyed-poi` and passes
18 compiled gates twice with identical results. Evidence is
`.tmp/networking/fr10_t05_acceptance.json`; its deterministic digest is
`5f300c5f7f925cf5105812cf49ce108305c357790989978738e34e88058e347c`
and the evidence SHA-256 is
`1041f4a279c916009f3efa25c23c5cf4649dd4875fd6a80bae5bc7102dc8b635`.

The T04 source-bound parent was hardened so a passing artifact cannot ignore
the new POI path. Its focused decision binds 123 stable source artifacts,
including all three sgame writers, the shared adapter, server capture/binder,
client raw lane, cgame parity/sink, layouts, tests, and ownership contracts.
It passes 11/11 with `source_inputs_stable=true` and
`q2proto_unchanged=true`, while correctly retaining `status=partial` and
`task_complete=false`. Evidence is
`.tmp/networking/fr10_t04_poi_inventory_focused.json`, with semantic SHA-256
`7d391f8df2f7b6aad6cb64668fba09cdcaba08213273cbef4fe1d39855806171`
and file SHA-256
`1f8e4a56b784ae314211bf3b839e04487198043dbfb064c580052dbe03866fde`.
The parent parser units pass 12/12.

The Windows production engine, dedicated engine, cgame, and sgame targets
build with the slice. Event-model revision 2 intentionally refreshes the
snapshot-store event, snapshot, and 100,000-publish soak goldens to
`906b42c00fab2edf`, `b2b60ae5a21f47ce`, and `6f3a2ad4178c5259`.
The final no-rebuild headless networking suite passes **201/201** in 1,050.7
seconds. That single run includes the focused POI rows, the 18-gate T05
parent, the T09 million-command parent, both 100,000-snapshot corpora, and the
T06 parent.

Current dependent corpus/parent evidence is:

- offline final-projection parity: two identical 100,000-snapshot runs,
  digest `6451c75bdb523477`, evidence SHA-256
  `ad52959b88c5ccbd17d776e2d78b7d33aea526d49b98e56b25bff98108dca5c6`;
- serialized production path: two identical 100,000-frame runs plus three
  negative probes, unchanged golden `ba519ae7bdd1db74`, evidence SHA-256
  `9302fd5cb40b1cfbf774e0f7115ec03618dde763c056b67ff71cb0f5d7f5c945`;
  this fixture has no serialized event references, so it is rebuilt-path
  regression evidence rather than keyed-POI coverage;
- `FR-10-T06`: 12 compiled gates twice plus both corpora and maximum-capacity
  budgets, evidence SHA-256
  `781b1f1edd147b2218423ac192c5047cd77baef8feaf5233d8c1bae933963782`;
- `FR-10-T09`: two one-million-command probes and nine compiled gates,
  unchanged digest `a9685d1ac8f40ef6`, evidence SHA-256
  `8117a68e2a89be704ad8c9c213af286bedc687eb6adf897752148c9e01ed92b9`.

The required final `.install/` refresh stages 16 root runtime files and one
runtime dependency, packages 601 repository assets, reinjects and audits 11
Q2AAS maps, and validates the Windows x86-64 stage. Independent artifact
checks pass 16/16 package tests, 12/12 release-unit tests, and the 1/1
headless bootstrap contract.

## Scope boundary and roadmap accounting

This is not a public native-authority or presentation cutover. It adds one
legacy service family to the private canonical shadow and proves its focused
value/parity/presenter contracts. It does not add:

- unkeyed POI native capture;
- a live T07 keyed-POI loss/retry/map-reuse presentation row;
- compound loss/reorder/duplication/corruption/rate/pause/demo-seek parity for
  POIs;
- remaining direct service-family adapters or raw direct-game sound;
- public negotiation/default-on authority, multi-client load, soak,
  cross-platform, or release evidence;
- the remaining `DV-04-T02` ownership inventory and cutover proof.

`FR-10-T05` was already complete; the 18-gate rerun is post-closure extension
evidence. `FR-10-T04` and `FR-10-T07` remain In Progress. No task changes state
and FR-10 remains **8/16 complete (50.0%)**.
