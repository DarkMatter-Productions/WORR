# WORR Quake 3 Snapshot and Event Networking Migration Plan

Date: 2026-03-16

Task IDs: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `FR-09-T04`, `FR-09-T05`, `FR-09-T06`, `FR-09-T07`, `FR-09-T08`, `DV-03-T07`

Status: `FR-09-T01` complete. This document is the planning baseline for the remaining tasks.

Supersedes: `docs-dev/proposals/event-system-migration.md`

## Purpose
Define a repository-grounded plan for porting the useful parts of Quake 3 Arena's snapshot and event model into WORR without pretending that current WORR networking is still vanilla idTech2.

The goal is not a literal transplant. The goal is a WORR-native networking model that:

- keeps Quake 3's strongest ideas
- does not inherit Quake 3's PvP-only assumptions
- handles monsters and cooperative play as first-class cases
- preserves a legacy path for existing Q2-compatible servers, clients, and demos until the WORR path is proven

## Primary Findings

1. WORR already has acknowledged-frame snapshot deltas.
   Vanilla Quake 2 and current WORR both delta snapshots from a client-acknowledged prior frame, not strictly from the immediately previous server frame. Quake 3 refines this model, but it does not invent it.

2. The largest remaining idTech2-era weakness in WORR is not snapshot existence.
   The real gap is event semantics: single-frame `entity_state_t.event`, heavy use of `svc_temp_entity`, and monster-specific `svc_muzzleflash2` paths that do not scale cleanly into a modern snapshot/event architecture.

3. WORR is not a Quake 3 deathmatch fork.
   WORR has monster-heavy PvE, cooperative slot reuse, coop respawn logic, instanced-item choices, and entity prioritization that already treats monsters specially. A direct Quake 3 port would leave real gameplay needs uncovered.

4. Quake 3's best ideas are still worth adopting.
   The most valuable imports are:
   - schema-driven snapshot deltas
   - sequenced player events with event parms
   - clearer separation between reliable command traffic and snapshot traffic
   - optional motion descriptors for entities that benefit from extrapolation

5. Quake 3's weakest fit for WORR is its transient-event model outside the player.
   `MAX_PS_EVENTS == 2`, one `entityState.event`, and player-centric prediction are not enough for monster attacks, coop objectives, scripted PvE encounters, and the volume of effects currently emitted through temp entities.

## Research Basis

### WORR Code Paths Reviewed

- `src/server/entities.c`
- `src/server/user.c`
- `src/server/server.h`
- `src/common/net/chan.c`
- `src/client/parse.cpp`
- `src/client/entities.cpp`
- `inc/shared/shared.h`
- `inc/shared/game.h`
- `inc/common/q2proto_shared.h`
- `src/server/game3_proxy/game3_proxy.c`
- `src/server/mvd/game.c`
- `src/game/sgame/player/p_client.cpp`
- `src/game/sgame/player/p_weapon.cpp`
- `src/game/sgame/monsters/m_zombie.cpp`
- `src/game/sgame/g_local.hpp`
- `src/legacy/game/g_monster.c`
- `src/legacy/game/g_weapon.c`

### Upstream Primary Sources Reviewed

- Quake 2
  - <https://github.com/id-Software/Quake-2/blob/master/server/sv_ents.c>
  - <https://github.com/id-Software/Quake-2/blob/master/client/cl_ents.c>
- Quake 3 Arena
  - <https://github.com/id-Software/Quake-III-Arena/blob/master/code/server/sv_snapshot.c>
  - <https://github.com/id-Software/Quake-III-Arena/blob/master/code/server/sv_client.c>
  - <https://github.com/id-Software/Quake-III-Arena/blob/master/code/client/cl_parse.c>
  - <https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/msg.c>
  - <https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/bg_misc.c>
  - <https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/q_shared.h>

## Current State: What WORR Actually Does Today

### Snapshot Transport

WORR is already beyond vanilla Quake 2 in several important ways:

- Server-side snapshots are built per client in `SV_BuildClientFrame()` in `src/server/entities.c`.
- Snapshot deltas are written in `SV_WriteFrameToClient_Enhanced()` and validated through `get_last_frame()`.
- The server already keeps frame history in `client_t.frames[UPDATE_BACKUP]` in `src/server/server.h`.
- The client already reconstructs delta snapshots in `CL_ParseFrame()` and `CL_ParsePacketEntities()` in `src/client/parse.cpp`.
- WORR already uses q2proto packing helpers for playerstate and entity delta packing.
- WORR already batches multiple usercmds in `src/server/user.c`, which is closer to Quake 3 than to stock Quake 2.

This matters because the migration should extend the existing snapshot backbone, not replace it blindly.

### Event Transport

WORR still carries idTech2-style event semantics:

- `entity_state_t.event` is a single impulse field in `inc/shared/shared.h`.
- The field is documented and treated as single-frame only.
- Client delta application explicitly resets the event when no event bit is present.
- `src/server/game3_proxy/game3_proxy.c` and `src/server/mvd/game.c` explicitly clear events every frame.
- Client event deduplication in `src/client/entities.cpp` is based on frame tracking, not an event sequence.

### Temp Entities and Muzzle Flashes

Large parts of gameplay still bypass entity events entirely:

- `svc_temp_entity` is heavily used in weapon and impact code.
- `svc_muzzleflash2` and `MZ2_*` monster flash ids are used across legacy and current monster attack paths.
- This is not a cosmetic edge case. It is the current transport for a large amount of transient PvE feedback.

### Monsters and Cooperative Play

WORR has gameplay constraints Quake 3 did not target:

- `SVF_MONSTER` exists as an explicit server-side priority hint.
- Cooperative slot reuse is built into `ClientChooseSlot(...)` / `ClientChooseSlot_Coop(...)`.
- `STAT_COOP_RESPAWN` exists in shared state.
- Monster tuning already scales for coop in some content, for example `m_zombie.cpp`.
- Snapshot overflow handling in `src/server/entities.c` already has prioritization logic and must not regress.

## Upstream Comparison

| Aspect | Vanilla Quake 2 | Current WORR | Quake 3 Arena | Planning Consequence |
| --- | --- | --- | --- | --- |
| Snapshot baseline | Delta from client-acknowledged prior frame in `SV_WriteFrameToClient` | Same core model, with stronger validation and q2proto packing | Delta from client-acknowledged packet in `SV_WriteSnapshotToClient` | Do not spend time "adding snapshots" first; focus on semantics and codec design |
| Entity list delta | Ordered old/new merge | Same | Same | Existing WORR merge logic can remain a conceptual base |
| Playerstate codec | Bitflag fields | q2proto-packed bitflag fields | Schema-driven `netField_t` table + bitstream packing | Quake 3's field-table model is a real upgrade worth porting |
| Player events | Mostly implicit or temp-entity driven | Same | `events[]`, `eventParms[]`, `eventSequence`, `externalEvent` | WORR should adopt this for player-centric predictable events |
| Non-player transient events | Single `entity_state_t.event` plus temp entities | Same | Still mostly one `entityState.event`, with some server-driven event usage | Quake 3's actor-side event model is not enough for WORR monsters/co-op |
| Motion model | Raw origins/angles plus interpolation | Same | `trajectory_t` on entity state | Use selectively, not as a forced global rewrite |
| Reliable commands | Netchan-level reliable data | Same core idea | Separate reliable server/client command rings | Useful, but can be introduced incrementally |
| Local player render entity | Replicated like any other entity | Same | Derived from `playerState_t` for the owning client | Optional later optimization, not a first migration step |
| Overflow handling | Hard limits, simple visibility choices | Prioritization already exists | Silent discard when snapshot entity budget is full | WORR should keep and expand its own priority model, not copy Quake 3 here |

## What Quake 3 Gets Right

### 1. Player Events Are Sequenced

Quake 3's `playerState_t` event ring fixes a real problem in Quake 2-style networking:

- multiple player events can exist over time without relying on one-frame coincidence
- client prediction has a clear event sequence to compare against
- `eventParm` makes event dispatch less overfitted than a single enum alone

This is a direct fit for footsteps, landing, jump pads, item pickup feedback, weapon-change feedback, predictable fire events, and similar local-player actions in WORR.

### 2. Snapshot Encoding Is Schema-Driven

Quake 3's `netField_t` tables are better than Quake 2's hard-coded bitflag families because they:

- centralize the network schema
- make the codec easier to audit and extend
- support per-field bit widths and small-int float encoding
- make cross-version protocol evolution easier to reason about

WORR should copy this idea, not the exact file layout.

### 3. Reliable Command Traffic Is Separated from Snapshot Traffic

Quake 3 makes reliable commands explicit and sequenced. That is cleaner than allowing the entire networking model to blur snapshots, textual commands, and transient state together.

WORR does not need a one-for-one rewrite immediately, but the long-term model should separate:

- persistent world snapshots
- reliable control/config traffic
- transient gameplay effects

### 4. Motion Semantics Are Explicit

`trajectory_t` gives Quake 3 a principled way to say whether an entity is:

- static
- interpolated
- linearly moving
- ballistic
- sine-driven

That is useful. The mistake would be assuming every WORR entity needs that representation on day one.

## Where Quake 3 Is Weak for WORR

| Quake 3 Limitation | Why It Is Weak for WORR | WORR Improvement |
| --- | --- | --- |
| `MAX_PS_EVENTS == 2` | Too small for a general event backlog under packet loss and modern effect density | Use a larger power-of-two queue for player events |
| One `entityState.event` | Still only one actor impulse at a time | Keep actor event support, but add a separate transient effect stream |
| Player-centric prediction model | Great for PvP movement, weak for monster-heavy PvE | Split player-predictable events from server-authoritative monster/world effects |
| Silent snapshot overflow | Acceptable in Quake 3 deathmatch, risky in coop PvE | Keep explicit priority classes and overflow diagnostics |
| Global `trajectory_t` mindset | Too invasive for an engine with large existing shared structs and content assumptions | Introduce optional motion descriptors only where they materially help |
| Tight coupling of shared game/network state | Harder to graft onto WORR safely | Start with snapshot view structs and translation layers before rewriting core shared structs |

## Design Principles for WORR

1. Build a new WORR networking path beside the legacy path.
   Current Q2-compatible protocols remain supported while the WORR path matures.

2. Do not start by editing `q2proto/`.
   Repository policy treats `q2proto/` as read-only unless explicitly authorized. The first iteration should live in engine-owned WORR networking modules and only later decide whether any q2proto integration is warranted.

3. Separate four concepts that idTech2 currently mixes together.
   - persistent replicated state
   - predictable player events
   - transient server-authoritative effects
   - reliable gameplay/control messages

4. Treat monsters and cooperative play as primary design cases.
   If the new model only looks good for PvP players, it is the wrong model for WORR.

5. Prefer translation layers before global struct surgery.
   Shared headers such as `entity_state_t` and `player_state_t` are deeply wired into the engine, game, client, renderer, demos, and tools. The safe path is to introduce snapshot-view structs first.

## Proposed Target Architecture

### 1. Protocol Layers

The WORR path should expose three explicit channels inside one versioned packet protocol:

- Reliable command stream
  - configstring-like updates
  - server commands
  - client commands
  - handshake and feature negotiation
- Snapshot stream
  - persistent player and entity state
  - delta against any acknowledged WORR snapshot
- Transient effect stream
  - monster muzzle flashes
  - impact effects
  - beam endpoints
  - scripted bursts
  - world-space sounds and visuals currently transported as temp entities

Optionally add a fourth path for reliable gameplay events if a state change is mission-critical and should not depend on snapshot visibility or effect-budget survival.

### 2. Snapshot View Structs

Do not begin by rewriting the engine's shared runtime structs.

Introduce engine-owned snapshot view types instead, for example:

- `worr_snapshot_t`
- `worr_snapshot_player_t`
- `worr_snapshot_entity_t`
- `worr_effect_record_t`

These structs are produced from current WORR state during snapshot build, encoded on the wire, then reconstructed on the client into view state consumed by cgame/client code.

Benefits:

- isolates protocol evolution from savegame and renderer structs
- allows dual-path legacy and WORR networking to coexist
- makes versioning easier
- reduces first-pass risk

### 3. Event Classification

The migration should begin by classifying every current transient signal into the right channel.

| Current Mechanism | Target Channel | Notes |
| --- | --- | --- |
| Player footsteps, land, item pickup, predictable fire cues | Player event queue | Quake 3-style import |
| Teleport, pain, death, one-shot actor impulses | Actor event field or actor event queue | Only if the event semantically belongs to the actor state |
| `svc_temp_entity` impacts, sparks, beams, explosions | Transient effect stream | Do not cram these into actor event fields |
| `svc_muzzleflash` / `svc_muzzleflash2` | Transient effect stream | Keep source entity and flash parm, but stop treating them as protocol special cases |
| Coop objective triggers, script milestones, stateful mission events | Reliable gameplay event or persistent snapshot state | Must survive packet loss and visibility constraints |

This classification work is the architectural pivot point of the whole migration.

### 4. Player Event Model

Adopt the Quake 3 idea, but widen it for WORR:

- event queue in the replicated player snapshot view
- event sequence counter
- event parms
- external event injection path
- client-side comparison against the last processed event sequence

Required changes:

- shared event helper API usable by server game and client prediction
- conversion of current player-centric impulse sites away from one-frame entity events
- local prediction rules that avoid double-play

### 5. Transient Effect Model

Add a dedicated effect queue per snapshot.

Each effect record should carry enough information to replace today's temp entity and muzzle-flash traffic without inventing fake persistent entities:

- effect type
- stable effect sequence
- source entity number where relevant
- optional target entity number
- position and optional secondary position
- parm / subtype
- flags such as predictable, broadcast, local-only, no-dedupe

This is the main improvement over Quake 3 for WORR.

It solves the cases Quake 3 handles awkwardly:

- monster attack flashes
- multi-impact weapons
- effects that belong to a place in the world, not to an entity field
- multiple transient events from one actor in the same snapshot window

### 6. Motion Descriptors

Do not replace all origins and angles with Quake 3 `trajectory_t` immediately.

Instead, add optional motion descriptors for entity classes that benefit:

- player-owned projectiles
- monster projectiles
- movers and platforms
- high-speed physics objects if needed

Keep raw state fallback for everything else. This preserves compatibility during rollout and avoids rewriting every entity producer at once.

### 7. Snapshot Budgeting for Monsters and Coop

Retain and extend WORR's current overflow prioritization.

Introduce explicit budget classes, for example:

- local player and view-critical state
- other players and squadmates
- bosses and active monsters
- active projectiles
- interactables and objectives
- loop sounds and ambient-only entities
- decorative effects

Add diagnostics:

- per-class drop counts
- overflow reasons
- effect budget pressure
- packet size attribution by class

Quake 3's silent discard behavior is not good enough here.

## Phased Implementation Plan

### Phase 0: Proposal, Instrumentation, and Audit

Tasks: `FR-09-T01`

Deliverables:

- this proposal
- a classification spreadsheet or code-facing inventory of temp entities, muzzle flashes, and entity events
- baseline packet-size and overflow telemetry for current WORR networking

Exit criteria:

- all event producers are assigned to a future channel
- current packet pressure is measurable before refactor work starts

### Phase 1: Snapshot Abstraction and Dual-Stack WORR Protocol

Tasks: `FR-09-T02`

Goals:

- add engine-owned WORR snapshot structs
- add protocol negotiation and feature flags for a WORR client/server path
- keep legacy Q2/Q2PRO/KEX-compatible paths intact
- add send/parse paths for WORR snapshots without changing gameplay semantics yet

Likely touchpoints:

- new engine-owned networking modules under `src/common/net/` and `inc/common/`
- `src/server/entities.c`
- `src/server/user.c`
- `src/client/parse.cpp`
- client/server connect negotiation paths

Exit criteria:

- WORR client and server can negotiate a new protocol path
- snapshot contents on the WORR path are state-equivalent to the legacy path
- legacy protocols still function unchanged

### Phase 2: Q3-Style Player Event Queue

Tasks: `FR-09-T03`

Goals:

- add player event queue, parms, and sequencing
- add external-event support
- move player-centric transient cues out of single-frame `entity_state_t.event`
- add prediction-safe client processing

Migration targets:

- footsteps
- land/fall events
- jump and teleport cues
- predictable item-pickup feedback
- predictable weapon fire cues where appropriate

Exit criteria:

- local-player predictable events are sequence-driven
- dropped packets do not silently erase player event history
- client prediction does not double-play queued events

### Phase 3: Transient Effect Stream

Tasks: `FR-09-T04`

Goals:

- add effect records to WORR snapshots or packet sections
- replace protocol-special `svc_temp_entity` and `svc_muzzleflash*` paths on the WORR protocol
- preserve legacy paths for old protocols during rollout

Migration targets:

- weapon impacts
- explosion bursts
- beam and trail events that are not persistent actor state
- monster muzzle flashes and attack cues
- world-space one-shot sounds/effects

Critical monster/co-op requirement:

- retain source-entity context and subtype information currently encoded by `MZ2_*`
- never require monsters to own predictive player-style event queues just to fire weapons correctly

Exit criteria:

- temp-entity and monster-muzzleflash traffic on the WORR protocol is carried by the effect stream
- multiple effects from one actor in one snapshot window can be represented cleanly

### Phase 4: Schema-Driven Snapshot Codec

Tasks: `FR-09-T05`

Goals:

- replace ad hoc bitflag families on the WORR protocol path with descriptor-driven field tables
- support explicit per-field widths and small-int float encoding
- isolate codec schemas from runtime structs

Notes:

- this is the closest direct import from Quake 3
- Huffman compression is optional and should be treated as a later optimization, not part of the semantic migration gate

Exit criteria:

- WORR snapshot schemas are explicit and versioned
- entity and player snapshot codecs are testable in isolation
- packet size is at least competitive with the legacy WORR path for comparable content

### Phase 5: Monster/Coop Budgeting and Optional Motion Descriptors

Tasks: `FR-09-T06`, `FR-09-T07`

Goals:

- promote class-aware budgeting from a local heuristic into an explicit protocol-era design
- add optional motion descriptors for projectiles, movers, and other high-value entities

Monster/co-op focus:

- squadmates and bosses must outrank incidental effects
- objective and mission-critical interactables must not disappear under load
- packet pressure from monster-heavy encounters must be measurable and debuggable

Exit criteria:

- worst-case coop encounters degrade predictably instead of randomly
- motion descriptors are only used where they demonstrably improve quality or bandwidth

### Phase 6: Demo, MVD, Validation, and Rollout

Tasks: `FR-09-T08`, `DV-03-T07`

Goals:

- define the WORR demo format and compatibility story
- keep legacy demo playback for legacy protocols
- extend MVD and replay paths only after the WORR packet model is stable
- add regression harnesses for packet loss, reconnect, monster encounters, and coop flows

Required validation:

- deterministic codec tests
- packet-loss replay tests
- demo record/playback parity checks
- coop mission flow checks
- monster-heavy encounter stress tests

Exit criteria:

- WORR protocol demos are versioned and replayable
- legacy demo paths still work
- rollout toggle can safely default on only after the validation matrix passes

## Detailed Monster and Cooperative Impact

### Monsters

Monsters are the strongest argument against a literal Quake 3 port.

Problems with a direct Quake 3 copy:

- monster attacks are not locally predicted like player movement
- a single actor event field cannot cleanly represent repeated muzzle flashes, impacts, and side effects
- many existing monster attack paths are expressed as effect bursts, not as durable actor state

Required WORR behavior:

- attacks must still identify the firing monster and muzzle subtype
- impact and trail effects must remain spatially correct under packet loss
- boss encounters must not lose critical effect cues due to generic snapshot overflow

Recommended approach:

- keep monster state authoritative in snapshots
- transport monster attack flashes and impact bursts through the effect stream
- reserve actor event fields for actual actor impulses, not protocol convenience

### Cooperative Play

Coop adds requirements Quake 3 largely sidesteps:

- stateful objectives
- mission scripting
- squad respawn and player-slot reuse
- instanced-item behavior choices
- monster density that exceeds typical Quake 3 PvP assumptions

Required WORR behavior:

- objective progression must not depend on a best-effort transient packet
- all players must receive the right subset of coop-relevant effects and state
- visibility and overflow rules must favor mission-critical state over decorative noise

Recommended approach:

- keep objective state persistent in snapshots or reliable gameplay messages
- keep transient coop feedback in the effect stream
- add per-client filtering at the effect layer for instanced or personalized events when needed

## What Not To Do

1. Do not treat this as "replace old frame deltas with acknowledged snapshots".
   WORR already has acknowledged snapshot deltas.

2. Do not stuff every temp entity into `entity_state_t.event`.
   That recreates the same bottleneck with different enum names.

3. Do not rewrite `q2proto/` first.
   Build the WORR path in engine-owned code, then decide later whether shared library changes are justified.

4. Do not replace all entity motion with Quake 3 trajectories in one pass.
   Use optional motion descriptors only where they earn their cost.

5. Do not remove the legacy path until demos, MVD, admin flows, and packet-loss testing all pass.

## Recommended Acceptance Matrix

Each implementation phase should be measured against the following:

- legacy Q2-compatible protocol path still connects and plays
- WORR protocol path passes packet-loss recovery tests
- player predictable events survive packet loss without double-play
- monster attacks still show correct muzzle flash, impact, and audio cues
- coop objective and respawn flows remain correct
- demo recording and playback remain versioned and valid for both paths
- snapshot overflow produces diagnostics and degrades by class priority instead of random omission

## Recommended Next Step

Start with `FR-09-T02` and an event inventory, not with a mass shared-struct rewrite.

The first code milestone should create a dual-stack WORR snapshot/effect packet path that can mirror current gameplay behavior exactly. Once that transport exists, player event queues and temp-entity retirement can proceed without destabilizing the rest of the engine.
