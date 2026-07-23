/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/command_abi.h"
#include "shared/snapshot_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_REWIND_ABI_VERSION 1u
#define WORR_REWIND_POSE_MODEL_REVISION 1u
#define WORR_REWIND_DEFAULT_TARGET_US UINT64_C(200000)
#define WORR_REWIND_HARD_LIMIT_US UINT64_C(250000)
#define WORR_REWIND_DEFAULT_FUTURE_TOLERANCE_US UINT64_C(25000)
#define WORR_REWIND_DEFAULT_CLOCK_SKEW_US UINT64_C(100000)
#define WORR_REWIND_DEFAULT_LEGACY_ERROR_US UINT64_C(50000)
#define WORR_REWIND_DEFAULT_INTERPOLATION_SPAN_US UINT64_C(100000)
#define WORR_REWIND_DEFAULT_TELEPORT_DISTANCE 256.0f
#define WORR_REWIND_MAX_MOVER_DEPTH 8u

enum {
  WORR_REWIND_POLICY_ALLOW_LEGACY_PACKET_SHARED = 1u << 0,
  WORR_REWIND_POLICY_REQUIRE_CONSUMED_COMMAND = 1u << 1,
};

typedef struct worr_rewind_policy_config_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t flags;
  uint32_t reserved0;
  uint64_t target_window_us;
  uint64_t hard_window_us;
  uint64_t future_tolerance_us;
  uint64_t max_clock_skew_us;
  uint64_t max_legacy_error_us;
} worr_rewind_policy_config_v1;

enum {
  WORR_REWIND_SNAPSHOT_PAUSED = 1u << 0,
  WORR_REWIND_SNAPSHOT_MAP_RESET = 1u << 1,
  WORR_REWIND_SNAPSHOT_HARD_RESYNC = 1u << 2,
};

/*
 * Trusted, server-built view of the canonical snapshot timing fields used by
 * policy resolution.  It contains no client-provided hit position or target.
 */
typedef struct worr_rewind_snapshot_time_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t flags;
  uint32_t tick_interval_us;
  worr_snapshot_id_v2 snapshot_id;
  uint32_t server_tick;
  uint32_t reserved0;
  uint64_t server_time_us;
  worr_snapshot_consumed_command_v2 consumed_command;
} worr_rewind_snapshot_time_v1;

enum {
  WORR_REWIND_MAPPING_AUTHENTICATED_TIMELINE = 1u << 0,
};

/*
 * Trusted result of looking the command watermark up in the server-owned
 * canonical snapshot timeline.  Legacy adapters must calculate the actual
 * per-command packet-sharing uncertainty; the policy never guesses it.
 */
typedef struct worr_rewind_mapping_proof_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t flags;
  uint32_t reserved0;
  worr_command_id_v1 command_id;
  worr_snapshot_id_v2 source_snapshot_id;
  uint32_t source_server_tick;
  uint32_t tick_interval_us;
  uint32_t watermark_provenance;
  uint32_t watermark_flags;
  uint64_t source_server_time_us;
  uint64_t rendered_server_time_us;
  uint64_t mapped_server_time_us;
  uint64_t mapping_error_bound_us;
} worr_rewind_mapping_proof_v1;

typedef enum worr_rewind_policy_reason_v1_e {
  WORR_REWIND_POLICY_EXACT = 0,
  WORR_REWIND_POLICY_LEGACY_BOUNDED = 1,
  WORR_REWIND_POLICY_CLAMPED_TARGET_WINDOW = 2,
  WORR_REWIND_POLICY_CLAMPED_FUTURE = 3,
  WORR_REWIND_POLICY_REJECT_INVALID_COMMAND = 4,
  WORR_REWIND_POLICY_REJECT_INVALID_SNAPSHOT = 5,
  WORR_REWIND_POLICY_REJECT_NO_WATERMARK = 6,
  WORR_REWIND_POLICY_REJECT_LEGACY_UNBOUNDED = 7,
  WORR_REWIND_POLICY_REJECT_UNCONSUMED_COMMAND = 8,
  WORR_REWIND_POLICY_REJECT_COMMAND_REPLAY = 9,
  WORR_REWIND_POLICY_REJECT_COMMAND_EPOCH = 10,
  WORR_REWIND_POLICY_REJECT_COMMAND_SEQUENCE_EXHAUSTED = 11,
  WORR_REWIND_POLICY_REJECT_SNAPSHOT_REGRESSION = 12,
  WORR_REWIND_POLICY_REJECT_SNAPSHOT_SEQUENCE_EXHAUSTED = 13,
  WORR_REWIND_POLICY_REJECT_TIME_EXHAUSTED = 14,
  WORR_REWIND_POLICY_REJECT_FUTURE = 15,
  WORR_REWIND_POLICY_REJECT_TOO_OLD = 16,
  WORR_REWIND_POLICY_REJECT_CLOCK_ABUSE = 17,
  WORR_REWIND_POLICY_REJECT_DISCONTINUITY = 18,
  WORR_REWIND_POLICY_REJECT_MAPPING_PROOF = 19,
} worr_rewind_policy_reason_v1;

enum {
  WORR_REWIND_DECISION_ACCEPTED = 1u << 0,
  WORR_REWIND_DECISION_CLAMPED = 1u << 1,
  WORR_REWIND_DECISION_LEGACY_FALLBACK = 1u << 2,
};

typedef struct worr_rewind_policy_decision_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t flags;
  uint32_t reason;
  worr_command_id_v1 command_id;
  worr_snapshot_id_v2 snapshot_id;
  worr_snapshot_id_v2 source_snapshot_id;
  uint32_t watermark_provenance;
  uint32_t reserved0;
  uint64_t requested_time_us;
  uint64_t mapped_time_us;
  uint64_t applied_time_us;
  uint64_t mapping_error_bound_us;
} worr_rewind_policy_decision_v1;

/* Every counter saturates at UINT64_MAX. */
typedef struct worr_rewind_policy_telemetry_v1_s {
  uint64_t requests;
  uint64_t accepted_exact;
  uint64_t accepted_legacy;
  uint64_t clamped_target;
  uint64_t clamped_future;
  uint64_t rejected_invalid;
  uint64_t rejected_untrusted;
  uint64_t rejected_replay;
  uint64_t rejected_exhausted;
  uint64_t rejected_future;
  uint64_t rejected_too_old;
  uint64_t rejected_clock;
  uint64_t rejected_discontinuity;
} worr_rewind_policy_telemetry_v1;

typedef struct worr_rewind_policy_state_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t initialized;
  uint32_t map_epoch;
  worr_command_id_v1 last_command_id;
  worr_snapshot_id_v2 last_snapshot_id;
  uint32_t last_server_tick;
  uint32_t reserved0;
  uint64_t last_server_time_us;
  uint32_t last_snapshot_flags;
  uint32_t last_tick_interval_us;
  worr_snapshot_consumed_command_v2 last_consumed_command;
  worr_rewind_policy_telemetry_v1 telemetry;
} worr_rewind_policy_state_v1;

void Worr_RewindPolicyConfigDefaultsV1(worr_rewind_policy_config_v1 *config);
bool Worr_RewindPolicyConfigValidateV1(
    const worr_rewind_policy_config_v1 *config);
bool Worr_RewindPolicyStateInitV1(worr_rewind_policy_state_v1 *state);
bool Worr_RewindSnapshotTimeFromCanonicalV2(
    const worr_snapshot_v2 *snapshot, uint32_t tick_interval_us, uint32_t flags,
    worr_rewind_snapshot_time_v1 *time_out);
bool Worr_RewindMappingProofValidateV1(
    const worr_rewind_mapping_proof_v1 *proof);

/*
 * Returns false only for an invalid/overlapping call; state and decision_out
 * are then byte-identical.  A policy rejection is a successful evaluation:
 * it returns true with ACCEPTED clear and an explicit reason, and advances
 * saturating telemetry.  Accepted commands alone advance progression state.
 */
bool Worr_RewindPolicyResolveV1(worr_rewind_policy_state_v1 *state,
                                const worr_rewind_policy_config_v1 *config,
                                const worr_command_record_v1 *command,
                                const worr_rewind_snapshot_time_v1 *snapshot,
                                const worr_rewind_mapping_proof_v1 *proof,
                                worr_rewind_policy_decision_v1 *decision_out);

typedef enum worr_rewind_lifecycle_v1_e {
  WORR_REWIND_LIFECYCLE_UNAVAILABLE = 0,
  WORR_REWIND_LIFECYCLE_ALIVE = 1,
  WORR_REWIND_LIFECYCLE_DEAD = 2,
  WORR_REWIND_LIFECYCLE_DORMANT = 3,
} worr_rewind_lifecycle_v1;

typedef enum worr_rewind_collision_shape_v1_e {
  WORR_REWIND_COLLISION_NONE = 0,
  WORR_REWIND_COLLISION_BOUNDS = 1,
  WORR_REWIND_COLLISION_BRUSH_MODEL = 2,
} worr_rewind_collision_shape_v1;

enum {
  WORR_REWIND_POSE_LINKED = 1u << 0,
  WORR_REWIND_POSE_DAMAGEABLE = 1u << 1,
  WORR_REWIND_POSE_HAS_MOVER = 1u << 2,
  WORR_REWIND_POSE_DISCONTINUITY_TELEPORT = 1u << 3,
  WORR_REWIND_POSE_DISCONTINUITY_MAP = 1u << 4,
  WORR_REWIND_POSE_DISCONTINUITY_TIME = 1u << 5,
  WORR_REWIND_POSE_DISCONTINUITY_PAUSE = 1u << 6,
  WORR_REWIND_POSE_DISCONTINUITY_RESPAWN = 1u << 7,
  WORR_REWIND_POSE_DISCONTINUITY_DEATH = 1u << 8,
  WORR_REWIND_POSE_DISCONTINUITY_GENERATION = 1u << 9,
  WORR_REWIND_POSE_DISCONTINUITY_MOVER = 1u << 10,
  WORR_REWIND_POSE_DISCONTINUITY_MANUAL = 1u << 11,
  WORR_REWIND_POSE_DISCONTINUITY_COLLISION = 1u << 12,
};

#define WORR_REWIND_POSE_FLAGS_V1                                              \
  ((uint32_t)WORR_REWIND_POSE_LINKED | (uint32_t)WORR_REWIND_POSE_DAMAGEABLE | \
   (uint32_t)WORR_REWIND_POSE_HAS_MOVER |                                      \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_TELEPORT |                         \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_MAP |                              \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_TIME |                             \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_PAUSE |                            \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_RESPAWN |                          \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_DEATH |                            \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_GENERATION |                       \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_MOVER |                            \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_MANUAL |                           \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_COLLISION)

#define WORR_REWIND_POSE_DISCONTINUITIES_V1                                    \
  ((uint32_t)WORR_REWIND_POSE_DISCONTINUITY_TELEPORT |                         \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_MAP |                              \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_TIME |                             \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_PAUSE |                            \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_RESPAWN |                          \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_DEATH |                            \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_GENERATION |                       \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_MOVER |                            \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_MANUAL |                           \
   (uint32_t)WORR_REWIND_POSE_DISCONTINUITY_COLLISION)

/* Pointer-free historical collision pose. */
typedef struct worr_rewind_pose_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t model_revision;
  uint32_t flags;
  worr_event_entity_ref_v1 entity;
  worr_event_entity_ref_v1 mover;
  uint32_t map_epoch;
  uint32_t server_tick;
  uint32_t lifecycle;
  uint32_t solid;
  uint32_t clip_flags;
  uint32_t collision_shape;
  /* Map-local immutable collision asset; nonzero only for brush models. */
  uint32_t collision_asset_id;
  /* Makes the 64-bit time alignment byte-defined across C and C++. */
  uint32_t reserved0;
  uint64_t server_time_us;
  float origin[3];
  float angles[3];
  float velocity[3];
  float mins[3];
  float maxs[3];
  float mover_relative_origin[3];
  float mover_relative_angles[3];
  /* Makes the 8-byte aggregate alignment tail byte-defined. */
  uint32_t reserved1;
} worr_rewind_pose_v1;

typedef struct worr_rewind_history_config_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint64_t max_interpolation_span_us;
  float teleport_distance;
  uint32_t reserved0;
} worr_rewind_history_config_v1;

typedef enum worr_rewind_append_reason_v1_e {
  WORR_REWIND_APPEND_ACCEPTED = 0,
  WORR_REWIND_APPEND_REJECT_INVALID = 1,
  WORR_REWIND_APPEND_REJECT_ENTITY = 2,
  WORR_REWIND_APPEND_REJECT_MAP_REGRESSION = 3,
  WORR_REWIND_APPEND_REJECT_TIME_REGRESSION = 4,
  WORR_REWIND_APPEND_REJECT_TICK_REGRESSION = 5,
  WORR_REWIND_APPEND_REJECT_TIME_EXHAUSTED = 6,
  WORR_REWIND_APPEND_REJECT_TICK_EXHAUSTED = 7,
  WORR_REWIND_APPEND_REJECT_PAUSE_INVARIANT = 8,
  WORR_REWIND_APPEND_REJECT_TICK_STALL = 9,
} worr_rewind_append_reason_v1;

typedef enum worr_rewind_query_reason_v1_e {
  WORR_REWIND_QUERY_EXACT = 0,
  WORR_REWIND_QUERY_INTERPOLATED = 1,
  WORR_REWIND_QUERY_DISCONTINUITY_FLOOR = 2,
  WORR_REWIND_QUERY_MISS_EMPTY = 3,
  WORR_REWIND_QUERY_MISS_TOO_OLD = 4,
  WORR_REWIND_QUERY_MISS_FUTURE = 5,
  WORR_REWIND_QUERY_MISS_MAP = 6,
  WORR_REWIND_QUERY_MISS_GENERATION = 7,
  WORR_REWIND_QUERY_MISS_LIFECYCLE = 8,
  WORR_REWIND_QUERY_MISS_GAP = 9,
  WORR_REWIND_QUERY_INVALID = 10,
} worr_rewind_query_reason_v1;

typedef enum worr_rewind_discrete_source_v1_e {
  WORR_REWIND_DISCRETE_NONE = 0,
  WORR_REWIND_DISCRETE_EXACT = 1,
  WORR_REWIND_DISCRETE_OLDER = 2,
  WORR_REWIND_DISCRETE_NEWER = 3,
  WORR_REWIND_DISCRETE_DISCONTINUITY_FLOOR = 4,
} worr_rewind_discrete_source_v1;

typedef struct worr_rewind_pose_query_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  worr_event_entity_ref_v1 entity;
  uint32_t map_epoch;
  uint32_t required_lifecycle;
  uint64_t target_time_us;
} worr_rewind_pose_query_v1;

typedef struct worr_rewind_pose_result_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t found;
  uint32_t reason;
  uint32_t discrete_source;
  uint32_t interpolation_fraction_q32;
  uint64_t requested_time_us;
  uint64_t applied_time_us;
  worr_rewind_pose_v1 pose;
} worr_rewind_pose_result_v1;

/* Every counter saturates at UINT64_MAX. */
typedef struct worr_rewind_history_telemetry_v1_s {
  uint64_t append_attempts;
  uint64_t appended;
  uint64_t overwritten;
  uint64_t rejected_invalid;
  uint64_t rejected_order;
  uint64_t auto_teleports;
  uint64_t auto_map_resets;
  uint64_t auto_time_gaps;
  uint64_t auto_generation_changes;
  uint64_t auto_mover_changes;
  uint64_t auto_collision_changes;
  uint64_t auto_respawns;
  uint64_t auto_deaths;
  uint64_t queries;
  uint64_t exact;
  uint64_t interpolated;
  uint64_t discontinuity_floors;
  uint64_t history_misses;
} worr_rewind_history_telemetry_v1;

/* Caller-owned envelope; slots and envelope must not overlap. */
typedef struct worr_rewind_history_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  worr_rewind_pose_v1 *slots;
  uint32_t capacity;
  uint32_t head;
  uint32_t count;
  uint32_t entity_index;
  worr_rewind_history_config_v1 config;
  worr_rewind_history_telemetry_v1 telemetry;
} worr_rewind_history_v1;

void Worr_RewindHistoryConfigDefaultsV1(worr_rewind_history_config_v1 *config);
bool Worr_RewindHistoryConfigValidateV1(
    const worr_rewind_history_config_v1 *config);
bool Worr_RewindPoseValidateV1(const worr_rewind_pose_v1 *pose);
bool Worr_RewindPoseHashV1(const worr_rewind_pose_v1 *pose, uint64_t *hash_out);
bool Worr_RewindHistoryInitV1(worr_rewind_history_v1 *history,
                              worr_rewind_pose_v1 *storage, uint32_t capacity,
                              uint32_t entity_index,
                              const worr_rewind_history_config_v1 *config);
bool Worr_RewindHistoryValidateV1(const worr_rewind_history_v1 *history);

/*
 * Returns false only for invalid/overlapping call storage and leaves every
 * object unchanged.  A rejected pose is a successful evaluated append: its
 * explicit reason and saturating telemetry are committed, while ring records
 * and cursors remain unchanged.
 */
bool Worr_RewindHistoryAppendV1(worr_rewind_history_v1 *history,
                                const worr_rewind_pose_v1 *pose,
                                uint32_t *reason_out);

bool Worr_RewindHistoryQueryV1(worr_rewind_history_v1 *history,
                               const worr_rewind_pose_query_v1 *query,
                               worr_rewind_pose_result_v1 *result_out);
bool Worr_RewindHistoryHashV1(const worr_rewind_history_v1 *history,
                              uint64_t *hash_out);

/*
 * A scene freezes the accepted policy target and the collision-relevant pose
 * selected for each entity.  Candidate storage is caller-owned and sorted by
 * entity index when the scene is sealed.  The scene never contains or mutates
 * a live game-entity pointer.
 */
typedef struct worr_rewind_scene_candidate_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t query_reason;
  uint16_t discrete_source;
  uint16_t reserved0;
  uint32_t interpolation_fraction_q32;
  worr_rewind_pose_v1 pose;
  uint64_t pose_hash;
} worr_rewind_scene_candidate_v1;

enum {
  WORR_REWIND_SCENE_SEALED = 1u << 0,
};

typedef struct worr_rewind_scene_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t flags;
  uint32_t reserved0;
  worr_rewind_scene_candidate_v1 *slots;
  uint32_t capacity;
  uint32_t count;
  uint32_t map_epoch;
  uint32_t reserved1;
  worr_rewind_policy_decision_v1 decision;
  uint64_t scene_hash;
} worr_rewind_scene_v1;

bool Worr_RewindPolicyDecisionValidateV1(
    const worr_rewind_policy_decision_v1 *decision, bool require_accepted);
bool Worr_RewindSceneInitV1(worr_rewind_scene_v1 *scene,
                            worr_rewind_scene_candidate_v1 *storage,
                            uint32_t capacity,
                            const worr_rewind_policy_decision_v1 *decision);
bool Worr_RewindSceneValidateV1(const worr_rewind_scene_v1 *scene);

/*
 * Adds one found, linked history result in strictly increasing entity-index
 * order.  This keeps scene construction O(candidate count), canonicalizes the
 * hash order, and matches the server's normal ascending entity scan.  Missing
 * results, duplicate/out-of-order indices, capacity exhaustion, aliasing, and
 * post-seal writes fail without changing the scene or candidate storage.
 */
bool Worr_RewindSceneAddResultV1(worr_rewind_scene_v1 *scene,
                                 const worr_rewind_pose_result_v1 *result);

/*
 * Server-owned construction path.  The caller must retain exclusive ownership
 * of the scene and storage returned by Worr_RewindSceneInitV1 between calls.
 * Header, decision, result, alias, capacity, and ordering checks remain on
 * every add, while previously accepted slots are not rescanned.  Seal is the
 * mandatory publication boundary: it deeply validates every candidate, mover
 * relationship, and hash before setting WORR_REWIND_SCENE_SEALED.  Unsealed
 * scenes remain invalid for Worr_RewindTraceViewV1.
 */
bool Worr_RewindSceneAddOwnedResultV1(
    worr_rewind_scene_v1 *scene,
    const worr_rewind_pose_result_v1 *result);
bool Worr_RewindSceneSealV1(worr_rewind_scene_v1 *scene);

/*
 * A trace-local ignore set is separate from the immutable scene.  Piercing
 * traces add identities to their own set; pellets/rays may start from distinct
 * sets while sharing one sealed scene and one selected target time.
 */
typedef struct worr_rewind_ignore_set_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  worr_event_entity_ref_v1 *slots;
  uint32_t capacity;
  uint32_t count;
} worr_rewind_ignore_set_v1;

bool Worr_RewindIgnoreSetInitV1(worr_rewind_ignore_set_v1 *ignore_set,
                                worr_event_entity_ref_v1 *storage,
                                uint32_t capacity);
bool Worr_RewindIgnoreSetValidateV1(
    const worr_rewind_ignore_set_v1 *ignore_set);
bool Worr_RewindIgnoreSetAddV1(worr_rewind_ignore_set_v1 *ignore_set,
                               worr_event_entity_ref_v1 entity);
bool Worr_RewindIgnoreSetContainsV1(const worr_rewind_ignore_set_v1 *ignore_set,
                                    worr_event_entity_ref_v1 entity,
                                    bool *contains_out);

/*
 * Immutable bridge input for one collision query.  The returned pointers are
 * valid until the scene or ignore storage is destroyed; sealed scene storage
 * cannot be modified through this API.  Rebuild the view after adding an
 * ignored identity.
 */
typedef struct worr_rewind_trace_view_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  const worr_rewind_scene_candidate_v1 *candidates;
  const worr_event_entity_ref_v1 *ignored_entities;
  uint32_t candidate_count;
  uint32_t ignore_count;
  uint32_t map_epoch;
  uint32_t reserved0;
  uint64_t target_time_us;
  uint64_t scene_hash;
  worr_command_id_v1 command_id;
  worr_snapshot_id_v2 snapshot_id;
} worr_rewind_trace_view_v1;

bool Worr_RewindTraceViewV1(const worr_rewind_scene_v1 *scene,
                            const worr_rewind_ignore_set_v1 *ignore_set,
                            worr_rewind_trace_view_v1 *view_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_REWIND_STATIC_ASSERT(condition, message)                          \
  static_assert((condition), message)
#else
#define WORR_REWIND_STATIC_ASSERT(condition, message)                          \
  _Static_assert((condition), message)
#endif

WORR_REWIND_STATIC_ASSERT(sizeof(float) == 4,
                          "rewind ABI requires 32-bit floats");
WORR_REWIND_STATIC_ASSERT(FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
                              FLT_MAX_EXP == 128,
                          "rewind ABI requires IEEE-754 binary32 floats");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_policy_config_v1) == 56,
                          "rewind policy config layout changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_snapshot_time_v1) == 56,
                          "rewind snapshot time layout changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_mapping_proof_v1) == 80,
                          "rewind mapping proof layout changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_policy_decision_v1) == 80,
                          "rewind policy decision layout changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_policy_telemetry_v1) == 104,
                          "rewind policy telemetry layout changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_policy_state_v1) == 176,
                          "rewind policy state layout changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_pose_v1) == 160,
                          "rewind pose layout changed");
WORR_REWIND_STATIC_ASSERT(offsetof(worr_rewind_pose_v1, collision_shape) == 52,
                          "rewind pose collision-shape offset changed");
WORR_REWIND_STATIC_ASSERT(offsetof(worr_rewind_pose_v1, reserved0) == 60,
                          "rewind pose reserved offset changed");
WORR_REWIND_STATIC_ASSERT(offsetof(worr_rewind_pose_v1, server_time_us) == 64,
                          "rewind pose time offset changed");
WORR_REWIND_STATIC_ASSERT(offsetof(worr_rewind_pose_v1, origin) == 72,
                          "rewind pose origin offset changed");
WORR_REWIND_STATIC_ASSERT(offsetof(worr_rewind_pose_v1,
                                   mover_relative_origin) == 132,
                          "rewind pose mover-relative offset changed");
WORR_REWIND_STATIC_ASSERT(offsetof(worr_rewind_pose_v1, reserved1) == 156,
                          "rewind pose tail-reserved offset changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_history_config_v1) == 24,
                          "rewind history config layout changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_pose_query_v1) == 32,
                          "rewind pose query layout changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_pose_result_v1) == 200,
                          "rewind pose result layout changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_history_telemetry_v1) == 144,
                          "rewind history telemetry layout changed");
WORR_REWIND_STATIC_ASSERT(sizeof(worr_rewind_scene_candidate_v1) == 184,
                          "rewind scene candidate layout changed");

#undef WORR_REWIND_STATIC_ASSERT
