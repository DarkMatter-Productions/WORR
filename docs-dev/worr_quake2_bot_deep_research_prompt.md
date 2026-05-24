# Deep Research Prompt: WORR Quake II Bot System Implementation Plan

You are a world-class game AI engineer and systems programmer with deep experience in FPS bot architecture, idTech-derived engines, Quake II / idTech2 internals, multiplayer netcode, real-time navigation, and performance-constrained C/C++ game DLL development.

Your task is to study **WORR**, an advanced Quake II Rerelease / idTech2-style server mod, and produce a detailed technical implementation plan for a best-in-class bot system suitable for both multiplayer and cooperative play.

Treat WORR as the target integration environment. Study its codebase, architecture, build system, game-loop structure, entity system, player/client handling, monster/NPC handling, physics, collision queries, map loading, save/load behaviour, multiplayer modes, and any existing AI or scripting facilities before proposing implementation details.

The final output must be an implementation plan, not a generic design essay.

---

## Primary Goal

Design a modern, efficient, extensible bot system for WORR / Quake II Rerelease that supports:

1. Multiplayer bots.
2. Cooperative campaign bots.
3. Teamplay and objective-aware behaviour.
4. Real-time automatic route generation.
5. No manually authored route, waypoint, or bot-path files.
6. Modern voxel-based navigation.
7. Modern but practical game AI.
8. Personalized bot character files.
9. Efficient runtime performance suitable for idTech2-style constraints.

The system should feel like it belongs in Quake II: fast, aggressive, readable, fair, deterministic where appropriate, and compatible with the limitations and design philosophy of the engine.

---

## Critical Constraints

The proposed bot system must obey the following constraints.

### Engine and Integration Constraints

- Target WORR / Quake II Rerelease-style game code.
- Assume C or C++ integration into the game/server module.
- Respect idTech2-style frame simulation, entity thinking, server authority, physics, collision, and networking.
- Do not require an external AI service at runtime.
- Do not require GPU acceleration at runtime.
- Do not require heavyweight middleware.
- Do not require client-side cheats, memory inspection, or proxy clients.
- Bots must operate through legitimate server-side input/action simulation.
- The system must be compatible with dedicated servers.
- The design must not assume full engine rewrites unless explicitly separated as optional future work.

### Navigation Constraints

- Use a **modern voxel navigation stack**.
- Generate navigation automatically from the current map at runtime or level load.
- Do **not** rely on manually-authored route files, waypoint files, AAS files, or per-map bot path files.
- The system may use temporary in-memory caches during a session.
- If persistent caching is proposed, clearly separate it from the core requirement and explain whether it violates the “no route files” constraint.
- Navigation must work on existing Quake II maps without requiring mapper intervention.
- Must support common Quake II traversal features:
  - Stairs.
  - Ramps.
  - Lifts.
  - Doors.
  - Platforms.
  - Water.
  - Lava/slime avoidance.
  - Ladders if present in WORR.
  - Teleporters.
  - Drop-downs.
  - Jump gaps.
  - Crouch paths if supported.
  - Elevators and moving brush models.
  - Trigger-controlled progression.
  - Player-blocked corridors.
  - Coop bottlenecks.
  - Dynamic obstructions.
  - Potential rocket-jump or grenade-jump links as optional advanced traversal.

### AI Constraints

- Use a modern AI architecture, but keep it suitable for an old-school shooter.
- Avoid vague “use machine learning” recommendations unless they are practical, bounded, and clearly separated into offline tooling versus runtime AI.
- Runtime AI must be deterministic, debuggable, performant, and server-safe.
- Bots must not be omniscient.
- Bots must not use unfair wallhack-style knowledge.
- Difficulty must be tunable without simply making bots cheat.
- Behaviour must be fun, not merely optimal.

### Multiplayer and Coop Constraints

The bot system must support both:

#### Multiplayer

- Deathmatch.
- Team Deathmatch.
- CTF or flag-style modes if present in WORR.
- Arena-style or custom WORR modes.
- Item control.
- Weapon selection.
- Spawn awareness.
- Powerup timing.
- Position control.
- Team coordination.
- Combat movement.
- Fair perception.

#### Cooperative Play

- Campaign progression.
- Following human players.
- Waiting for humans at doors/lifts/transitions.
- Not blocking narrow corridors.
- Not stealing all health/ammo.
- Helping with fights.
- Handling scripted triggers.
- Pressing buttons or using objects where appropriate.
- Understanding objective flow.
- Avoiding sequence breaks.
- Supporting mixed teams of humans and bots.
- Recovering from stuck or separated states.
- Teleporting/catching up only as a last-resort configurable failsafe.

---

## Required Research Areas

Before proposing the implementation plan, investigate and summarize the relevant technical facts from:

1. WORR source code and project structure.
2. Quake II Rerelease game DLL architecture.
3. idTech2 entity, movement, collision, and tracing model.
4. Existing Quake II bot systems where relevant:
   - Gladiator Bot.
   - Eraser Bot.
   - ACE Bot.
   - Quake II 3ZB / other historical bots if applicable.
5. Quake III Arena BotLib concepts where relevant:
   - Area awareness.
   - Reachability.
   - Item goals.
   - Bot character files.
   - Long-term / short-term goals.
6. Modern navigation approaches:
   - Voxelization.
   - Recast-style navmesh generation.
   - Compact heightfields.
   - Navigation graphs.
   - Hierarchical pathfinding.
   - Dynamic link generation.
   - Local steering.
7. Modern game AI approaches:
   - Behaviour trees.
   - Utility AI.
   - GOAP or planner-style objective reasoning.
   - Blackboard systems.
   - Sensory memory.
   - Influence maps.
   - Tactical position scoring.
   - Offline imitation learning or reinforcement learning, only if practical.
8. Performance constraints for real-time server-side AI.
9. Debugging and development tooling for bot systems.
10. Mod customization and character/profile authoring.

---

## Required Output Structure

Produce the final research result using the following structure.

---

# 1. Executive Summary

Provide a concise technical summary of the recommended bot architecture.

Include:

- The overall architecture.
- Why this approach fits WORR / Quake II.
- What makes it better than traditional waypoint bots.
- What is realistic for an idTech2-style codebase.
- What should be implemented first.
- What should be deferred.

---

# 2. Assumptions About WORR

List all assumptions made about WORR after studying it.

Include assumptions or confirmed facts about:

- Language and build system.
- Game DLL / server module structure.
- Entity representation.
- Player/client representation.
- Monster/NPC AI structure.
- Physics and movement functions.
- Collision tracing functions.
- Map/BSP access.
- Save/load support.
- Multiplayer mode structure.
- Console variable / command system.
- Config file support.
- Existing AI hooks.
- Existing bot support, if any.
- Any constraints caused by Quake II Rerelease / KEX integration.

For each assumption, state whether it is:

- Confirmed from code.
- Inferred from Quake II architecture.
- Unknown and requiring inspection.

---

# 3. Design Goals and Non-Goals

Define precise goals.

## Goals

Must include:

- Automatic navigation generation.
- Voxel navigation.
- Real-time routing.
- No manually-authored route files.
- Deathmatch support.
- Teamplay support.
- Coop support.
- Character customization.
- Fair perception.
- Good combat.
- Efficient CPU use.
- Debuggability.
- Mod extensibility.

## Non-Goals

Explicitly identify what should not be attempted in the first implementation.

Examples:

- Full neural network runtime control.
- Human-level natural language planning.
- Perfect scripted map understanding.
- Full client-side prediction.
- Unbounded machine learning at runtime.
- Requiring map recompilation.
- Requiring mapper-authored nav data.
- Requiring engine-level rewrites unless optional.

---

# 4. System Architecture

Design the bot system as a set of modules.

At minimum include:

```text
BotManager
BotClient / BotEntityAdapter
PerceptionSystem
SensoryMemory
NavigationSystem
VoxelNavBuilder
ReachabilityAnalyzer
PathPlanner
LocalMovementController
CombatController
TacticalEvaluator
GoalManager
BehaviourTreeRuntime
UtilityScorer
TeamCoordinator
CoopObjectiveManager
CharacterProfileSystem
DebugOverlay / BotDiagnostics
Config / CVAR Interface
PerformanceScheduler
```

For each module, describe:

- Responsibility.
- Main data structures.
- Update frequency.
- Dependencies.
- Public API.
- Failure modes.
- Performance cost.
- How it integrates with WORR.

---

# 5. Bot Execution Pipeline

Describe the per-frame or per-think bot update pipeline.

Include a proposed tick schedule similar to:

```text
Every server frame:
  1. Collect bot-relevant world state.
  2. Update perception for a subset of bots.
  3. Update sensory memory.
  4. Refresh tactical blackboard.
  5. Re-score goals when needed.
  6. Re-plan path if required.
  7. Execute local movement.
  8. Execute combat aiming and firing.
  9. Emit usercmd-style movement/buttons.
  10. Run diagnostics and stuck detection.
```

Specify which tasks should run:

- Every frame.
- Every 100 ms.
- Every 250 ms.
- Every 500 ms.
- On map load.
- On entity spawn/despawn.
- On major event, such as item pickup, death, door activation, or objective trigger.

Explain how to stagger expensive work across bots.

---

# 6. Navigation System: Voxel-Based Runtime Auto-Routing

Design the navigation stack in detail.

## 6.1 Source Geometry

Explain how the bot system should obtain geometry from WORR / Quake II:

- BSP collision data.
- Traces and hull tests.
- Brush models.
- Static entities.
- Doors/platforms.
- Water/lava/slime volumes.
- Trigger volumes.
- Item/entity positions.
- Spawn points.
- Monster/player dimensions.

If direct BSP access is difficult, provide fallback strategies using collision sampling and traces.

## 6.2 Voxelization

Specify a practical voxel representation.

Include recommended parameters:

- Cell size.
- Cell height.
- Agent radius.
- Agent height.
- Step height.
- Max slope.
- Max fall height.
- Jump height.
- Crouch height if applicable.
- Water handling.
- Hazard cost multipliers.

Explain how parameters should differ for:

- Standard player bot.
- Crouching bot.
- Swimming bot.
- Large monster/NPC if shared with NPC AI later.

## 6.3 Walkable Surface Extraction

Describe how to identify walkable voxels or spans.

Include:

- Floor detection.
- Ceiling clearance.
- Slope filtering.
- Step connectivity.
- Water connectivity.
- Hazard tagging.
- Dynamic entity exclusion.
- Door and lift tagging.

## 6.4 Region Building

Explain how to build connected regions.

Include:

- Flood fill.
- Region IDs.
- Portal edges.
- Area boundaries.
- Vertical connectivity.
- Narrow passage detection.
- Choke point tagging.

## 6.5 Navmesh or Area Graph

Recommend whether the runtime representation should be:

- Pure voxel grid.
- Extracted polygon navmesh.
- Area graph.
- Hybrid voxel + graph.

Provide a reasoned recommendation for WORR.

The plan should likely prefer a hybrid:

```text
Voxel field → compact spans → connected regions → area graph → optional simplified convex walkable polygons → runtime path graph.
```

## 6.6 Reachability Links

Describe automatic generation of links for:

- Walking.
- Stepping.
- Jumping.
- Falling.
- Swimming.
- Ladders.
- Doors.
- Platforms.
- Elevators.
- Teleporters.
- Triggered doors.
- Jump pads if present.
- Optional rocket jumps.
- Optional grenade jumps.

For each link type, specify:

- Detection method.
- Required movement controller.
- Cost model.
- Risk score.
- Validation method.
- Debug visualization.

## 6.7 Dynamic Navigation

Explain how to handle:

- Moving platforms.
- Doors opening/closing.
- Temporary blockers.
- Players blocking doors.
- Monsters blocking corridors.
- Disabled paths.
- Broken assumptions.
- Scripted changes.
- Coop progression state.

## 6.8 Real-Time Routing

Explain how bots route without route files.

Include:

- A* over area graph.
- Hierarchical A* for large maps.
- Flow fields for team pushes or common goals.
- Path cache keyed by area pairs.
- Incremental path repair.
- Time-sliced pathfinding.
- Emergency local steering fallback.
- Route invalidation rules.

## 6.9 Stuck Detection and Recovery

Specify robust stuck recovery logic.

Include:

- Position delta tracking.
- Velocity failure.
- Path progress failure.
- Door/lift wait states.
- Human blocking detection.
- Jump failure detection.
- Re-pathing.
- Micro-unstuck movement.
- Temporary local obstacle avoidance.
- Last-resort teleport rules for coop only, configurable.

---

# 7. Movement Controller

Design the low-level movement system.

It must support:

- Human-like acceleration.
- Strafe running.
- Circle strafing.
- Dodging.
- Bunnyhop-like behaviour only if compatible with WORR physics.
- Jump timing.
- Stair traversal.
- Ramp traversal.
- Swimming.
- Falling.
- Elevator riding.
- Door handling.
- Ledge avoidance.
- Crouch movement if present.
- Rocket-jump traversal as optional expert behaviour.

Explain how movement should convert desired movement into Quake II-style command input:

```text
forwardmove
sidemove
upmove
viewangles
buttons
impulse / weapon select
```

Describe how to avoid robotic movement while remaining effective.

---

# 8. Perception and Sensory Memory

Design a fair sensory model.

Include:

- Field of view.
- Line-of-sight tracing.
- Hearing model.
- Weapon sound events.
- Footstep or movement sound if available.
- Damage source awareness.
- Teammate callouts.
- Last known enemy positions.
- Memory decay.
- Confidence scores.
- Alertness states.
- Difficulty-dependent reaction delays.

Bots must not know:

- Enemy position through walls.
- Item pickups they did not observe or infer.
- Hidden enemies without sensory evidence.
- Exact player health unless visible/inferred by damage model.

Explain how to implement this efficiently without tracing every bot to every entity every frame.

---

# 9. Goal and Decision Architecture

Design the high-level AI.

Use a layered architecture.

Recommended structure:

```text
Reflex Layer:
  immediate dodge, fire, avoid lava, avoid rocket splash

Tactical Layer:
  choose fight/flee/reposition/item pickup

Strategic Layer:
  map control, powerups, objectives, team roles

Social/Team Layer:
  coordination, escort, defend, attack, follow, regroup

Coop Layer:
  campaign progression, wait for humans, trigger handling
```

Use a combination of:

- Utility AI for goal selection.
- Behaviour trees for execution.
- Blackboard for shared state.
- Lightweight planning for multi-step objectives.
- Finite state machines only for simple movement/action substates.

For each layer, define:

- Inputs.
- Outputs.
- Update rate.
- Example decisions.
- Failure handling.

---

# 10. Multiplayer Bot Behaviour

Design behaviour for competitive modes.

Include:

## Deathmatch

- Item prioritization.
- Weapon acquisition.
- Armor control.
- Mega health control.
- Powerup timing.
- Spawn movement.
- Enemy engagement.
- Ambush behaviour.
- Disengagement.
- Route variation.
- Anti-camping logic.

## Team Deathmatch

- Role assignment.
- Grouping.
- Crossfire.
- Covering teammates.
- Area control.
- Resource sharing.
- Avoiding overstacking one route.
- Responding to teammate deaths.

## CTF / Objective Modes

If WORR supports these modes, include:

- Attacker.
- Defender.
- Midfielder.
- Escort.
- Flag carrier.
- Return specialist.
- Base defence.
- Enemy flag prediction.
- Team communication.
- Dynamic role switching.

---

# 11. Cooperative Bot Behaviour

Design coop-specific behaviour in depth.

The plan must handle:

- Following human players.
- Leading only when commanded or when safe.
- Waiting at progression points.
- Not blocking doors, lifts, ladders, corridors, or teleporters.
- Sharing resources.
- Prioritizing humans for health/ammo/armor unless configured otherwise.
- Avoiding premature trigger activation.
- Helping humans survive encounters.
- Finishing off enemies.
- Covering retreats.
- Handling boss fights.
- Pressing buttons.
- Recognizing required keys or objectives if present.
- Riding elevators with players.
- Regrouping after map transitions.
- Recovering when separated.
- Coop-friendly teleport catch-up rules.

Include a proposed `CoopObjectiveManager`.

It should infer objective flow using:

- Trigger_use events.
- Target/targetname relationships.
- Doors and buttons.
- Monster death triggers.
- Level exit triggers.
- Key/item dependencies.
- Human player position clustering.
- Previously observed progression events.
- Optional manual scripting hooks for exceptional maps, while still not requiring route files.

---

# 12. Combat System

Design a Quake II-specific combat controller.

Include:

- Weapon selection.
- Weapon desirability by range.
- Ammo awareness.
- Projectile prediction.
- Hitscan aiming.
- Railgun leading.
- Rocket splash aiming.
- Grenade arcs.
- BFG use.
- Chaingun tracking.
- Blaster finishing.
- Shotgun range behaviour.
- Hyperblaster prediction if relevant.
- Target prioritization.
- Threat scoring.
- Cover seeking.
- Dodge behaviour.
- Retreat logic.
- Friendly fire avoidance.
- Coop monster targeting.
- Boss behaviour.
- Difficulty scaling.

Difficulty scaling must affect:

- Reaction time.
- Aim noise.
- Prediction accuracy.
- Tactical foresight.
- Movement skill.
- Item timing precision.
- Team coordination.
- Risk tolerance.
- Memory duration.

Avoid simply giving higher skill bots unfair knowledge.

---

# 13. Character Profile System

Design customizable personalized bot character files.

Use a human-editable format such as:

```text
.cfg
.json
.toml
.ini
```

or a format idiomatic to WORR.

Character files should support:

```text
name
skin/model
chat style
skill level
aim profile
reaction time
aggression
bravery
teamplay
cooperation
resource greed
preferred weapons
weapon avoidance
movement skill
jump skill
rocket-jump permission
item timing skill
map-control preference
objective preference
follow/lead tendency
risk tolerance
camping tolerance
personality tags
voice/chat lines
coop helpfulness
human-protection bias
friendly-fire caution
role preferences
```

Provide an example character file.

Example:

```ini
[identity]
name = "Rivet"
model = "female/athena"
skin = "worr_blue"

[skill]
overall = 0.72
reaction_ms_min = 180
reaction_ms_max = 340
aim_error_degrees = 2.8
prediction = 0.65
movement = 0.78

[personality]
aggression = 0.82
bravery = 0.70
teamplay = 0.55
resource_greed = 0.45
humor = 0.20

[weapons]
preferred = ["rocket_launcher", "railgun", "super_shotgun"]
avoid = ["bfg"]
rocket_jump = true

[coop]
follow_human_bias = 0.70
share_resources = 0.80
press_buttons = "ask_or_when_safe"
block_corridors = false
protect_low_health_players = 0.85
```

Explain how character profiles should be parsed, validated, clamped, hot-reloaded, and exposed to server admins.

---

# 14. Team Coordination

Design the team AI layer.

Include:

- Shared blackboard.
- Role assignment.
- Requests and claims.
- Item reservation.
- Route deconfliction.
- Area defence.
- Attack grouping.
- Escort behaviour.
- Coop follow groups.
- Human player priority.
- Communication messages.
- Avoiding all bots choosing the same item or route.

Example shared facts:

```text
enemy_last_seen[entity_id]
powerup_spawn_time[item_id]
claimed_item[item_id]
claimed_route_segment
team_role[bot_id]
human_needs_help[player_id]
objective_state
danger_area_score
```

---

# 15. Fairness and Anti-Cheat Principles

Define explicit fairness rules.

Bots may know:

- Their own state.
- Teammate shared observations.
- Visible enemies.
- Audible events.
- Items they have seen.
- Timers for items they observed.
- Map topology after navigation build.

Bots may not know unfairly:

- Enemy positions through walls.
- Exact hidden player health.
- Unseen item pickups.
- Future spawn choices.
- Player inputs.
- Server internals that a player could not infer.

Define server cvars for fairness:

```text
bot_skill
bot_reaction_min
bot_reaction_max
bot_aim_error
bot_perception_fov
bot_hearing_range
bot_memory_time
bot_allow_item_timers
bot_allow_team_callouts
bot_allow_rocketjump
bot_coop_resource_sharing
bot_debug
```

---

# 16. Performance Design

Provide a detailed performance plan.

Include:

- CPU budget per bot.
- Expected cost of voxel nav generation.
- Pathfinding cost.
- Perception trace budget.
- Combat trace budget.
- Think scheduling.
- LOD for far-away or inactive bots.
- Shared team computations.
- Cache invalidation.
- Memory budget.
- Data-oriented layout opportunities.
- Avoiding heap churn.
- Avoiding per-frame full-world scans.
- Avoiding expensive traces for every entity pair.
- Staggering updates across frames.

Include estimated budgets for:

```text
4 bots
8 bots
16 bots
32 bots
```

State which features should degrade gracefully at high bot counts.

---

# 17. Data Structures

Specify concrete data structures.

Include likely structs/classes for:

```c
bot_t
bot_character_t
bot_perception_t
bot_memory_t
bot_goal_t
bot_path_t
bot_area_t
bot_navlink_t
bot_blackboard_t
bot_team_state_t
bot_coop_state_t
bot_debug_state_t
```

For each, list important fields.

Example:

```c
typedef struct bot_navlink_s {
    int from_area;
    int to_area;
    int type;
    float cost;
    float risk;
    vec3_t start;
    vec3_t end;
    int required_flags;
    int dynamic_entity_id;
    float last_valid_time;
} bot_navlink_t;
```

---

# 18. APIs and Integration Points

Identify where the bot system should hook into WORR.

Include likely integration points:

- Game initialization.
- Map load.
- Entity spawn.
- Entity removal.
- Per-frame server update.
- Client connect/disconnect.
- Bot spawn.
- Bot remove.
- User command generation.
- Damage events.
- Death events.
- Item pickup events.
- Weapon fire events.
- Sound events.
- Trigger use.
- Door/platform movement.
- Level transition.
- Save/load if relevant.
- Console commands.
- CVAR registration.

Provide proposed function names and signatures where practical.

Example:

```c
void Bot_InitGame(void);
void Bot_ShutdownGame(void);
void Bot_LevelInit(void);
void Bot_LevelShutdown(void);
void Bot_RunFrame(float dt);
qboolean Bot_Spawn(const char *character_file, int team);
void Bot_Remove(edict_t *bot);
void Bot_OnEntityEvent(edict_t *ent, bot_event_type_t type);
void Bot_BuildUserCmd(bot_t *bot, usercmd_t *cmd);
```

---

# 19. Debugging and Tooling

Design developer tools.

Must include:

- Nav voxel visualization.
- Region visualization.
- Path visualization.
- Reachability link visualization.
- Bot current goal display.
- Perception cone display.
- Last-known enemy marker.
- Item desirability overlay.
- Danger heatmap.
- Coop objective state display.
- Stuck detector logs.
- Performance counters.
- Bot decision trace.
- Character profile inspector.
- Console commands for spawning and controlling bots.

Example commands:

```text
bot_add <character> [team]
bot_remove <name|all>
bot_debug <botname>
bot_nav_debug 0/1/2/3
bot_nav_rebuild
bot_goal_dump <botname>
bot_profile_reload
bot_followme
bot_hold
bot_attack
bot_defend
bot_coop_wait
bot_coop_lead
```

---

# 20. Testing Plan

Provide a robust test plan.

Include:

## Unit Tests

- Character profile parser.
- Utility scoring.
- A* pathfinding.
- Region connectivity.
- Reachability validation.
- Stuck detection.
- Perception visibility rules.
- Weapon selection.

## Integration Tests

- Spawn bots.
- Run bots on stock maps.
- Navigate to all major items.
- Complete simple coop sections.
- Use doors and elevators.
- Fight monsters.
- Fight players.
- Recover from blocked paths.
- Join/leave dedicated server.
- Load map transitions.

## Regression Tests

- Every stock Quake II map.
- WORR-specific maps.
- Deathmatch maps.
- CTF maps if present.
- Coop campaign maps.
- Stress tests with 16+ bots.
- Long-running server stability.

## Metrics

Track:

```text
average frame cost
max frame spike
path success rate
stuck incidents per hour
coop completion rate
item pickup effectiveness
combat win rate by skill
friendly fire incidents
human-blocking incidents
resource-stealing incidents
bot death causes
player satisfaction notes
```

---

# 21. Implementation Phases

Break the work into practical phases.

The plan should include at least:

## Phase 0: Codebase Reconnaissance

- Study WORR architecture.
- Identify hooks.
- Build minimal test branch.
- Add bot CVARs and commands.
- Spawn inert fake client/bot.

## Phase 1: Minimal Bot Skeleton

- Bot spawn/remove.
- Usercmd generation.
- Basic movement.
- Basic view control.
- Basic combat target selection.
- Basic debug output.

## Phase 2: Runtime Voxel Navigation Prototype

- Generate simple walkable grid.
- Build connected regions.
- Pathfind between points.
- Visualize nav data.
- Navigate to items.

## Phase 3: Reachability and Movement

- Doors.
- Lifts.
- Stairs.
- Water.
- Jumps.
- Drop-downs.
- Dynamic path validation.
- Stuck recovery.

## Phase 4: Deathmatch Bot

- Item goals.
- Weapon selection.
- Combat movement.
- Enemy perception.
- Difficulty scaling.
- Character profiles.

## Phase 5: Teamplay

- Shared blackboard.
- Role assignment.
- Team item claims.
- Tactical grouping.
- CTF/objective support if applicable.

## Phase 6: Coop Bot

- Follow/wait/lead commands.
- Resource sharing.
- Trigger/objective inference.
- Coop progression.
- Human-friendly behaviour.
- Boss/monster tactics.

## Phase 7: Polish and Optimization

- Performance scheduler.
- Debug tooling.
- Config polish.
- Regression tests.
- Documentation.
- Server admin controls.
- Modder extension hooks.

For each phase, provide:

- Deliverables.
- Required code areas.
- Risks.
- Test criteria.
- Estimated complexity.
- Dependencies.

---

# 22. Risk Analysis

Identify major technical risks and mitigations.

Must include:

- Runtime nav generation too slow.
- BSP geometry access difficult.
- Lifts and moving brush models unreliable.
- Coop scripts hard to infer.
- Bots blocking humans.
- Multiplayer bots feeling unfair.
- Performance degradation with many bots.
- Character files becoming too complex.
- Save/load incompatibility.
- Dedicated server edge cases.
- WORR-specific architectural constraints.

For each risk, provide a mitigation strategy.

---

# 23. Recommended Algorithms

List recommended algorithms and where they should be used.

Examples:

```text
Voxelization:
  compact heightfield / span grid

Region generation:
  flood fill + contour/portal extraction

Pathfinding:
  A* over area graph, hierarchical A* for large maps

Local movement:
  steering + waypoint corridor following + Quake-style movement controller

Goal selection:
  utility scoring

Task execution:
  behaviour trees

Coop progression:
  lightweight symbolic planner / dependency graph

Combat:
  weapon-specific heuristics + projectile prediction

Perception:
  event-driven sensing + time-sliced LOS traces

Team coordination:
  shared blackboard + claim tokens

Difficulty:
  parameterized skill model, not cheating
```

Explain why each algorithm is appropriate for idTech2/WORR.

---

# 24. Concrete Pseudocode

Include pseudocode for the most important systems.

At minimum include:

## Bot Frame Update

```c
void Bot_RunFrame(float dt);
```

## Per-Bot Think

```c
void Bot_Think(bot_t *bot, float dt);
```

## Goal Selection

```c
bot_goal_t Bot_SelectGoal(bot_t *bot);
```

## Path Request

```c
qboolean Bot_RequestPath(bot_t *bot, vec3_t start, vec3_t goal);
```

## Movement Command Generation

```c
void Bot_BuildUserCmd(bot_t *bot, usercmd_t *cmd);
```

## Voxel Nav Build

```c
void BotNav_BuildForLevel(void);
```

## Reachability Validation

```c
qboolean BotNav_TestReachability(nav_area_t *a, nav_area_t *b, int link_type);
```

## Coop Wait/Follow Logic

```c
void BotCoop_Update(bot_t *bot);
```

---

# 25. Example Configuration

Provide examples for:

## Server CVARs

```text
bot_enable 1
bot_max_clients 8
bot_skill 0.65
bot_nav_runtime_build 1
bot_nav_allow_persistent_cache 0
bot_perception_fair 1
bot_allow_rocketjump 0
bot_coop_enabled 1
bot_coop_share_resources 1
bot_coop_teleport_unstuck 1
bot_debug 0
```

## Character Profile

Provide a full example character profile.

## Team Role Profile

Provide an example role configuration for:

- Attacker.
- Defender.
- Support.
- Coop follower.
- Coop leader.

---

# 26. Compatibility With Existing Quake II Bot Ideas

Compare the proposed system against classic Quake II / Quake III bot approaches.

Discuss:

- What to borrow from classic Quake II bots.
- What to avoid.
- What Quake III BotLib concepts remain useful.
- Why route-file or waypoint-only systems are insufficient.
- How voxel navigation improves maintainability.
- How character files can modernize classic bot personalities.

---

# 27. Final Recommended Architecture

End with a clear final recommendation.

Include:

- The exact architecture to build.
- The minimal viable bot.
- The best long-term architecture.
- What to implement first.
- What to avoid.
- What design choices are most important for success.

Conclude with a prioritized checklist.

Example:

```text
Priority 1:
  - Bot spawn/usercmd skeleton
  - Runtime voxel nav prototype
  - Debug visualization
  - Basic deathmatch item routing

Priority 2:
  - Perception and sensory memory
  - Combat controller
  - Character profiles
  - Stuck recovery

Priority 3:
  - Team blackboard
  - Coop follow/wait/resource sharing
  - Trigger/objective inference

Priority 4:
  - Advanced reachability
  - Rocket/grenade jumps
  - Offline tuning tools
  - Regression automation
```

---

## Style Requirements

- Be technical and specific.
- Avoid generic AI language.
- Do not hand-wave integration details.
- State uncertainty explicitly.
- Distinguish confirmed code facts from assumptions.
- Prefer practical C/C++ engine design over theoretical AI.
- Prioritize maintainability, determinism, debugging, and performance.
- Include concrete data structures, algorithms, APIs, pseudocode, CVARs, file formats, and implementation phases.
- Treat this as a plan that an experienced Quake II mod programmer could begin implementing from.
