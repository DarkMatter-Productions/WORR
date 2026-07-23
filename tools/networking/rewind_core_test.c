#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common/net/rewind.h"

static int failures;

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,         \
              #expression);                                                    \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static worr_command_record_v1 make_command(uint32_t epoch, uint32_t sequence,
                                           uint32_t source_tick,
                                           uint64_t source_time_us) {
  worr_command_record_v1 command;
  memset(&command, 0, sizeof(command));
  command.struct_size = sizeof(command);
  command.schema_version = WORR_COMMAND_ABI_VERSION;
  command.command_id.epoch = epoch;
  command.command_id.sequence = sequence;
  command.sample_time_us = (uint64_t)sequence * UINT64_C(16000);
  command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  command.command.struct_size = sizeof(command.command);
  command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  command.command.duration_ms = 16;
  command.render_watermark.struct_size = sizeof(command.render_watermark);
  command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  command.render_watermark.provenance =
      WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  command.render_watermark.source_server_tick = source_tick;
  command.render_watermark.tick_interval_us = 10000;
  command.render_watermark.source_server_time_us = source_time_us;
  command.render_watermark.rendered_server_time_us = source_time_us;
  return command;
}

static worr_rewind_snapshot_time_v1
make_snapshot_time(uint32_t epoch, uint32_t sequence, uint32_t server_tick,
                   uint64_t server_time_us, worr_command_id_v1 consumed) {
  worr_rewind_snapshot_time_v1 snapshot;
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.struct_size = sizeof(snapshot);
  snapshot.schema_version = WORR_REWIND_ABI_VERSION;
  snapshot.tick_interval_us = 10000;
  snapshot.snapshot_id.epoch = epoch;
  snapshot.snapshot_id.sequence = sequence;
  snapshot.server_tick = server_tick;
  snapshot.server_time_us = server_time_us;
  snapshot.consumed_command.cursor.epoch = consumed.epoch;
  snapshot.consumed_command.cursor.contiguous_sequence = consumed.sequence;
  snapshot.consumed_command.provenance =
      WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
  return snapshot;
}

static worr_rewind_mapping_proof_v1
make_mapping_proof(const worr_command_record_v1 *command,
                   const worr_rewind_snapshot_time_v1 *snapshot) {
  const worr_command_render_watermark_v1 *watermark =
      &command->render_watermark;
  worr_rewind_mapping_proof_v1 proof;
  memset(&proof, 0, sizeof(proof));
  proof.struct_size = sizeof(proof);
  proof.schema_version = WORR_REWIND_ABI_VERSION;
  proof.flags = WORR_REWIND_MAPPING_AUTHENTICATED_TIMELINE;
  proof.command_id = command->command_id;
  proof.source_snapshot_id.epoch = snapshot->snapshot_id.epoch;
  if (watermark->source_server_tick == snapshot->server_tick &&
      watermark->source_server_time_us == snapshot->server_time_us) {
    proof.source_snapshot_id.sequence = snapshot->snapshot_id.sequence;
  } else {
    proof.source_snapshot_id.sequence =
        snapshot->snapshot_id.sequence > 1 ? snapshot->snapshot_id.sequence - 1u
                                           : snapshot->snapshot_id.sequence;
  }
  proof.source_server_tick = watermark->source_server_tick;
  proof.tick_interval_us = watermark->tick_interval_us;
  proof.watermark_provenance = watermark->provenance;
  proof.watermark_flags = watermark->flags;
  proof.source_server_time_us = watermark->source_server_time_us;
  proof.rendered_server_time_us = watermark->rendered_server_time_us;
  proof.mapped_server_time_us = watermark->rendered_server_time_us;
  if (watermark->provenance ==
      WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED) {
    proof.mapping_error_bound_us =
        (uint64_t)watermark->tick_interval_us * UINT64_C(2);
  }
  return proof;
}

static bool resolve_policy(worr_rewind_policy_state_v1 *state,
                           const worr_rewind_policy_config_v1 *config,
                           const worr_command_record_v1 *command,
                           const worr_rewind_snapshot_time_v1 *snapshot,
                           worr_rewind_policy_decision_v1 *decision_out) {
  const worr_rewind_mapping_proof_v1 proof =
      make_mapping_proof(command, snapshot);
  return Worr_RewindPolicyResolveV1(state, config, command, snapshot, &proof,
                                    decision_out);
}

static worr_rewind_policy_decision_v1 make_decision(uint32_t map_epoch,
                                                    uint64_t target_time_us) {
  worr_rewind_policy_decision_v1 decision;
  memset(&decision, 0, sizeof(decision));
  decision.struct_size = sizeof(decision);
  decision.schema_version = WORR_REWIND_ABI_VERSION;
  decision.flags = WORR_REWIND_DECISION_ACCEPTED;
  decision.reason = WORR_REWIND_POLICY_EXACT;
  decision.command_id.epoch = 1;
  decision.command_id.sequence = 1;
  decision.snapshot_id.epoch = map_epoch;
  decision.snapshot_id.sequence = 1;
  decision.source_snapshot_id = decision.snapshot_id;
  decision.watermark_provenance = WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  decision.requested_time_us = target_time_us;
  decision.mapped_time_us = target_time_us;
  decision.applied_time_us = target_time_us;
  return decision;
}

static worr_rewind_pose_v1 make_pose(uint32_t index, uint32_t generation,
                                     uint32_t map_epoch, uint32_t server_tick,
                                     uint64_t server_time_us, float x) {
  worr_rewind_pose_v1 pose;
  unsigned int i;
  memset(&pose, 0, sizeof(pose));
  pose.struct_size = sizeof(pose);
  pose.schema_version = WORR_REWIND_ABI_VERSION;
  pose.model_revision = WORR_REWIND_POSE_MODEL_REVISION;
  pose.flags = WORR_REWIND_POSE_LINKED | WORR_REWIND_POSE_DAMAGEABLE;
  pose.entity.index = index;
  pose.entity.generation = generation;
  pose.map_epoch = map_epoch;
  pose.server_tick = server_tick;
  pose.lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
  pose.solid = 1;
  pose.clip_flags = 1;
  pose.collision_shape = WORR_REWIND_COLLISION_BOUNDS;
  pose.server_time_us = server_time_us;
  pose.origin[0] = x;
  for (i = 0; i < 3; ++i) {
    pose.mins[i] = -16.0f;
    pose.maxs[i] = 16.0f;
  }
  return pose;
}

static void attach_mover(worr_rewind_pose_v1 *pose,
                         worr_event_entity_ref_v1 mover, float relative_x) {
  pose->flags |= WORR_REWIND_POSE_HAS_MOVER;
  pose->mover = mover;
  pose->mover_relative_origin[0] = relative_x;
}

static worr_rewind_pose_result_v1
make_result(const worr_rewind_pose_v1 *pose, uint64_t target_time_us,
            uint32_t reason, uint32_t discrete_source, uint32_t fraction_q32) {
  worr_rewind_pose_result_v1 result;
  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.schema_version = WORR_REWIND_ABI_VERSION;
  result.found = 1;
  result.reason = reason;
  result.discrete_source = discrete_source;
  result.interpolation_fraction_q32 = fraction_q32;
  result.requested_time_us = target_time_us;
  result.applied_time_us = pose->server_time_us;
  result.pose = *pose;
  return result;
}

static void test_snapshot_projection(void) {
  worr_snapshot_v2 snapshot;
  worr_rewind_snapshot_time_v1 time;
  worr_rewind_snapshot_time_v1 before;
  uint64_t hash;

  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.struct_size = sizeof(snapshot);
  snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  snapshot.flags = WORR_SNAPSHOT_FLAG_KEYFRAME | WORR_SNAPSHOT_FLAG_COMPLETE |
                   WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION;
  snapshot.snapshot_id.epoch = 1;
  snapshot.snapshot_id.sequence = 1;
  snapshot.server_tick = 100;
  snapshot.server_time_us = UINT64_C(1000000);
  snapshot.controlled_entity.identity.index = 1;
  snapshot.controlled_entity.identity.generation = 1;
  snapshot.controlled_entity.provenance_flags =
      WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
  snapshot.discontinuity.flags = WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
                                 WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
  snapshot.discontinuity.reason = WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
  CHECK(Worr_SnapshotCalculateHashV2(&snapshot, 1024, &hash));
  snapshot.snapshot_hash = hash;
  CHECK(Worr_SnapshotValidateV2(&snapshot, 1024));
  CHECK(Worr_RewindSnapshotTimeFromCanonicalV2(&snapshot, 10000, 0, &time));
  CHECK(time.snapshot_id.epoch == 1 && time.snapshot_id.sequence == 1);
  CHECK(time.server_time_us == snapshot.server_time_us);

  memset(&time, 0xa5, sizeof(time));
  before = time;
  snapshot.snapshot_hash ^= UINT64_C(1);
  CHECK(!Worr_RewindSnapshotTimeFromCanonicalV2(&snapshot, 10000, 0, &time));
  CHECK(memcmp(&time, &before, sizeof(time)) == 0);
}

static void test_policy(void) {
  worr_rewind_policy_config_v1 config;
  worr_rewind_policy_state_v1 state;
  worr_rewind_policy_state_v1 before_state;
  worr_rewind_policy_decision_v1 decision;
  worr_command_record_v1 command;
  worr_rewind_snapshot_time_v1 snapshot;
  alignas(worr_rewind_policy_config_v1) unsigned char
      misaligned_bytes[sizeof(config) + 1];

  Worr_RewindPolicyConfigDefaultsV1(&config);
  CHECK(Worr_RewindPolicyConfigValidateV1(&config));
  memset(misaligned_bytes, 0xa5, sizeof(misaligned_bytes));
  CHECK(!Worr_RewindPolicyConfigValidateV1(
      (const worr_rewind_policy_config_v1 *)(const void *)(misaligned_bytes +
                                                           1)));
  Worr_RewindPolicyConfigDefaultsV1(
      (worr_rewind_policy_config_v1 *)(void *)(misaligned_bytes + 1));
  CHECK(misaligned_bytes[1] == 0xa5);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 90, UINT64_C(900000));
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(Worr_RewindPolicyDecisionValidateV1(&decision, true));
  CHECK(decision.reason == WORR_REWIND_POLICY_EXACT);
  CHECK(decision.applied_time_us == UINT64_C(900000));
  CHECK(state.last_snapshot_flags == snapshot.flags);
  CHECK(state.last_tick_interval_us == snapshot.tick_interval_us);

  command = make_command(1, 2, 91, UINT64_C(910000));
  snapshot.consumed_command.cursor.contiguous_sequence = 2;
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK((decision.flags & WORR_REWIND_DECISION_ACCEPTED) == 0);
  CHECK(decision.reason == WORR_REWIND_POLICY_REJECT_SNAPSHOT_REGRESSION);
  CHECK(state.last_command_id.sequence == 1);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 90, UINT64_C(900000));
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  command = make_command(2, 1, 91, UINT64_C(910000));
  snapshot =
      make_snapshot_time(1, 3, 101, UINT64_C(1010000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_REJECT_COMMAND_EPOCH);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, UINT32_MAX, 90, UINT64_C(900000));
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK((decision.flags & WORR_REWIND_DECISION_ACCEPTED) != 0);
  command = make_command(2, 1, 91, UINT64_C(910000));
  snapshot =
      make_snapshot_time(1, 3, 101, UINT64_C(1010000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK((decision.flags & WORR_REWIND_DECISION_ACCEPTED) != 0);
  CHECK(state.last_command_id.epoch == 2 &&
        state.last_command_id.sequence == 1);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 90, UINT64_C(900000));
  snapshot = make_snapshot_time(1, UINT32_MAX, 100, UINT64_C(1000000),
                                command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  command = make_command(1, 2, 101, UINT64_C(1010000));
  snapshot =
      make_snapshot_time(1, 1, 101, UINT64_C(1010000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(decision.reason ==
        WORR_REWIND_POLICY_REJECT_SNAPSHOT_SEQUENCE_EXHAUSTED);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 90, UINT64_C(900000));
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  before_state = state;
  CHECK(!resolve_policy(&state, &config, &command, &snapshot,
                        (worr_rewind_policy_decision_v1 *)(void *)&state));
  CHECK(memcmp(&state, &before_state, sizeof(state)) == 0);
}

static void test_policy_bounds(void) {
  worr_rewind_policy_config_v1 config;
  worr_rewind_policy_state_v1 state;
  worr_rewind_policy_decision_v1 decision;
  worr_command_record_v1 command;
  worr_rewind_snapshot_time_v1 snapshot;

  Worr_RewindPolicyConfigDefaultsV1(&config);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 100, UINT64_C(1000000));
  command.render_watermark.flags = WORR_COMMAND_RENDER_EXTRAPOLATED;
  command.render_watermark.rendered_server_time_us = UINT64_C(1010000);
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_CLAMPED_FUTURE);
  CHECK(decision.applied_time_us == snapshot.server_time_us);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 79, UINT64_C(790000));
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_CLAMPED_TARGET_WINDOW);
  CHECK(decision.applied_time_us == UINT64_C(800000));

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 74, UINT64_C(740000));
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_REJECT_TOO_OLD);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 90, UINT64_C(700000));
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_REJECT_CLOCK_ABUSE);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 90, UINT64_C(900000));
  command.render_watermark.provenance =
      WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED;
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_LEGACY_BOUNDED);
  CHECK(decision.mapping_error_bound_us == UINT64_C(20000));

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 90, UINT64_C(700000));
  command.render_watermark.provenance =
      WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED;
  command.render_watermark.tick_interval_us = 30000;
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  snapshot.tick_interval_us = 30000;
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_REJECT_LEGACY_UNBOUNDED);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 100, UINT64_C(1000000));
  command.render_watermark.flags = WORR_COMMAND_RENDER_EXTRAPOLATED;
  command.render_watermark.rendered_server_time_us = UINT64_C(1030000);
  snapshot =
      make_snapshot_time(1, 1, 100, UINT64_C(1000000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_REJECT_FUTURE);

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  command = make_command(1, 1, 90, UINT64_C(900000));
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  command = make_command(2, 1, 5, UINT64_C(50000));
  snapshot = make_snapshot_time(2, 1, 5, UINT64_C(50000), command.command_id);
  snapshot.flags = WORR_REWIND_SNAPSHOT_MAP_RESET;
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK((decision.flags & WORR_REWIND_DECISION_ACCEPTED) != 0);
  command = make_command(4, 1, 6, UINT64_C(60000));
  snapshot = make_snapshot_time(4, 1, 6, UINT64_C(60000), command.command_id);
  snapshot.flags = WORR_REWIND_SNAPSHOT_MAP_RESET;
  CHECK(resolve_policy(&state, &config, &command, &snapshot, &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_REJECT_DISCONTINUITY);
}

static void test_policy_mapping_proof(void) {
  worr_rewind_policy_config_v1 config;
  worr_rewind_policy_state_v1 state;
  worr_rewind_policy_decision_v1 decision;
  worr_command_record_v1 command;
  worr_rewind_snapshot_time_v1 snapshot;
  worr_rewind_mapping_proof_v1 proof;
  worr_rewind_policy_state_v1 before_state;
  worr_rewind_policy_decision_v1 before_decision;
  alignas(worr_rewind_mapping_proof_v1) unsigned char
      misaligned_proof[sizeof(proof) + 1];

  Worr_RewindPolicyConfigDefaultsV1(&config);
  command = make_command(1, 1, 90, UINT64_C(900000));
  command.render_watermark.provenance =
      WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED;
  snapshot =
      make_snapshot_time(1, 2, 100, UINT64_C(1000000), command.command_id);
  proof = make_mapping_proof(&command, &snapshot);
  CHECK(Worr_RewindMappingProofValidateV1(&proof));
  memset(misaligned_proof, 0, sizeof(misaligned_proof));
  CHECK(!Worr_RewindMappingProofValidateV1(
      (const worr_rewind_mapping_proof_v1 *)(const void *)(misaligned_proof +
                                                           1)));

  /* A large packet-shared batch must use its actual bound, not two ticks. */
  proof.mapped_server_time_us = UINT64_C(880000);
  proof.mapping_error_bound_us = UINT64_C(60000);
  CHECK(Worr_RewindMappingProofValidateV1(&proof));
  CHECK(Worr_RewindPolicyStateInitV1(&state));
  CHECK(Worr_RewindPolicyResolveV1(&state, &config, &command, &snapshot, &proof,
                                   &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_REJECT_LEGACY_UNBOUNDED);

  proof.mapping_error_bound_us = UINT64_C(20000);
  CHECK(Worr_RewindPolicyStateInitV1(&state));
  CHECK(Worr_RewindPolicyResolveV1(&state, &config, &command, &snapshot, &proof,
                                   &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_LEGACY_BOUNDED);
  CHECK(Worr_RewindPolicyDecisionValidateV1(&decision, true));
  CHECK(decision.requested_time_us == UINT64_C(900000));
  CHECK(decision.mapped_time_us == UINT64_C(880000));

  proof.source_snapshot_id = snapshot.snapshot_id;
  CHECK(Worr_RewindPolicyStateInitV1(&state));
  CHECK(Worr_RewindPolicyResolveV1(&state, &config, &command, &snapshot, &proof,
                                   &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_REJECT_MAPPING_PROOF);

  proof = make_mapping_proof(&command, &snapshot);
  proof.mapping_error_bound_us = 0;
  CHECK(!Worr_RewindMappingProofValidateV1(&proof));
  CHECK(Worr_RewindPolicyStateInitV1(&state));
  CHECK(Worr_RewindPolicyResolveV1(&state, &config, &command, &snapshot, &proof,
                                   &decision));
  CHECK(decision.reason == WORR_REWIND_POLICY_REJECT_MAPPING_PROOF);

  command.render_watermark.provenance =
      WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  proof = make_mapping_proof(&command, &snapshot);
  proof.mapping_error_bound_us = 1;
  CHECK(!Worr_RewindMappingProofValidateV1(&proof));

  CHECK(Worr_RewindPolicyStateInitV1(&state));
  before_state = state;
  memset(&decision, 0xa5, sizeof(decision));
  before_decision = decision;
  CHECK(!Worr_RewindPolicyResolveV1(
      &state, &config, &command, &snapshot,
      (const worr_rewind_mapping_proof_v1 *)(const void *)&state, &decision));
  CHECK(memcmp(&state, &before_state, sizeof(state)) == 0);
  CHECK(memcmp(&decision, &before_decision, sizeof(decision)) == 0);
}

static void test_history(void) {
  worr_rewind_history_config_v1 config;
  worr_rewind_history_v1 history;
  worr_rewind_pose_v1 storage[8];
  worr_rewind_pose_v1 before_storage[8];
  worr_rewind_history_v1 before_history;
  worr_rewind_pose_v1 pose;
  worr_rewind_pose_query_v1 query;
  worr_rewind_pose_result_v1 result;
  uint32_t reason;
  uint64_t hash_a;
  uint64_t hash_b;

  Worr_RewindHistoryConfigDefaultsV1(&config);
  CHECK(Worr_RewindHistoryInitV1(&history, storage, 8, 7, &config));
  pose = make_pose(7, 1, 1, 10, 100, 0.0f);
  pose.angles[1] = 350.0f;
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK(reason == WORR_REWIND_APPEND_ACCEPTED);
  pose = make_pose(7, 1, 1, 11, 200, 10.0f);
  pose.angles[1] = 10.0f;
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK(Worr_RewindHistoryHashV1(&history, &hash_a));

  memset(&query, 0, sizeof(query));
  query.struct_size = sizeof(query);
  query.schema_version = WORR_REWIND_ABI_VERSION;
  query.entity.index = 7;
  query.entity.generation = 1;
  query.map_epoch = 1;
  query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
  query.target_time_us = 150;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.found == 1 && result.reason == WORR_REWIND_QUERY_INTERPOLATED);
  CHECK(fabsf(result.pose.origin[0] - 5.0f) < 0.001f);
  CHECK(fabsf(result.pose.angles[1] - 360.0f) < 0.001f);
  CHECK(Worr_RewindHistoryHashV1(&history, &hash_b));
  CHECK(hash_a == hash_b);

  pose = make_pose(7, 1, 1, 12, 300, 400.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK((storage[2].flags & WORR_REWIND_POSE_DISCONTINUITY_TELEPORT) != 0);
  query.target_time_us = 250;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.reason == WORR_REWIND_QUERY_DISCONTINUITY_FLOOR);
  CHECK(result.applied_time_us == 200);

  pose = make_pose(7, 2, 1, 13, 400, 401.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK((storage[3].flags & WORR_REWIND_POSE_DISCONTINUITY_GENERATION) != 0);
  query.target_time_us = 350;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.reason == WORR_REWIND_QUERY_MISS_FUTURE);
  query.entity.generation = 2;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.reason == WORR_REWIND_QUERY_MISS_TOO_OLD);

  before_history = history;
  memcpy(before_storage, storage, sizeof(storage));
  pose = make_pose(7, 2, 1, 14, 500, 402.0f);
  CHECK(!Worr_RewindHistoryAppendV1(&history, &pose, &history.count));
  CHECK(memcmp(&history, &before_history, sizeof(history)) == 0);
  CHECK(memcmp(storage, before_storage, sizeof(storage)) == 0);

  CHECK(Worr_RewindHistoryHashV1(&history, &hash_a));
  pose = storage[0];
  pose.origin[1] = -0.0f;
  CHECK(Worr_RewindPoseHashV1(&pose, &hash_b));
  pose.origin[1] = 0.0f;
  CHECK(Worr_RewindPoseHashV1(&pose, &hash_a));
  CHECK(hash_a == hash_b);
}

static void test_history_discontinuity_rules(void) {
  worr_rewind_history_config_v1 config;
  worr_rewind_history_v1 history;
  worr_rewind_pose_v1 storage[4];
  worr_rewind_pose_v1 pose;
  worr_rewind_pose_query_v1 query;
  worr_rewind_pose_result_v1 result;
  worr_event_entity_ref_v1 mover = {20, 1};
  uint32_t reason;

  Worr_RewindHistoryConfigDefaultsV1(&config);
  CHECK(Worr_RewindHistoryInitV1(&history, storage, 4, 8, &config));
  pose = make_pose(8, 1, 1, 10, 100, 0.0f);
  attach_mover(&pose, mover, 1.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  pose = make_pose(8, 1, 1, 11, 200, 1000.0f);
  attach_mover(&pose, mover, 2.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK((storage[1].flags & WORR_REWIND_POSE_DISCONTINUITY_TELEPORT) == 0);

  pose = make_pose(8, 1, 1, 12, 200, 1001.0f);
  attach_mover(&pose, mover, 3.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK(reason == WORR_REWIND_APPEND_REJECT_PAUSE_INVARIANT);
  CHECK(history.count == 2);
  pose.flags |= WORR_REWIND_POSE_DISCONTINUITY_PAUSE;
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK(reason == WORR_REWIND_APPEND_ACCEPTED);
  memset(&query, 0, sizeof(query));
  query.struct_size = sizeof(query);
  query.schema_version = WORR_REWIND_ABI_VERSION;
  query.entity.index = 8;
  query.entity.generation = 1;
  query.map_epoch = 1;
  query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
  query.target_time_us = 200;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.found == 1 && result.reason == WORR_REWIND_QUERY_EXACT);
  CHECK(result.pose.origin[0] == 1001.0f);

  pose = make_pose(8, 1, 1, 12, 300, 1002.0f);
  attach_mover(&pose, mover, 4.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK(reason == WORR_REWIND_APPEND_REJECT_TICK_STALL);

  pose = make_pose(8, 1, 1, 13, 300, 1002.0f);
  pose.collision_shape = WORR_REWIND_COLLISION_BRUSH_MODEL;
  pose.collision_asset_id = 9;
  attach_mover(&pose, mover, 4.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK(reason == WORR_REWIND_APPEND_ACCEPTED);
  CHECK((storage[3].flags & WORR_REWIND_POSE_DISCONTINUITY_COLLISION) != 0);
  storage[3].flags &= ~(uint32_t)WORR_REWIND_POSE_DISCONTINUITY_COLLISION;
  CHECK(!Worr_RewindHistoryValidateV1(&history));
  storage[3].flags |= WORR_REWIND_POSE_DISCONTINUITY_COLLISION;
  CHECK(Worr_RewindHistoryValidateV1(&history));
}

static void test_history_hostile_layout(void) {
  union {
    max_align_t alignment;
    unsigned char
        bytes[sizeof(worr_rewind_history_v1) + 2 * sizeof(worr_rewind_pose_v1)];
  } hostile;
  unsigned char before[sizeof(hostile.bytes)];
  worr_rewind_history_config_v1 config;
  worr_rewind_history_v1 valid;
  worr_rewind_pose_v1 storage[2];
  worr_rewind_pose_v1 pose;

  Worr_RewindHistoryConfigDefaultsV1(&config);
  memset(hostile.bytes, 0xa5, sizeof(hostile.bytes));
  memcpy(before, hostile.bytes, sizeof(before));
  CHECK(!Worr_RewindHistoryInitV1(
      (worr_rewind_history_v1 *)(void *)hostile.bytes,
      (worr_rewind_pose_v1 *)(void *)hostile.bytes, 2, 1, &config));
  CHECK(memcmp(hostile.bytes, before, sizeof(before)) == 0);

  CHECK(Worr_RewindHistoryInitV1(&valid, storage, 2, 1, &config));
  valid.head = 1;
  CHECK(!Worr_RewindHistoryValidateV1(&valid));

  pose = make_pose(1, 1, 1, 1, 1, 0.0f);
  pose.collision_shape = WORR_REWIND_COLLISION_BRUSH_MODEL;
  CHECK(!Worr_RewindPoseValidateV1(&pose));
  pose.collision_asset_id = 1;
  CHECK(Worr_RewindPoseValidateV1(&pose));
  pose.reserved1 = 1;
  CHECK(!Worr_RewindPoseValidateV1(&pose));
  pose.reserved1 = 0;
  pose.collision_shape = WORR_REWIND_COLLISION_BOUNDS;
  CHECK(!Worr_RewindPoseValidateV1(&pose));
}

static void test_history_map_epoch_query_boundaries(void) {
  worr_rewind_history_config_v1 config;
  worr_rewind_history_v1 history;
  worr_rewind_pose_v1 storage[4];
  worr_rewind_pose_v1 pose;
  worr_rewind_pose_query_v1 query;
  worr_rewind_pose_result_v1 result;
  uint32_t reason;

  Worr_RewindHistoryConfigDefaultsV1(&config);
  CHECK(Worr_RewindHistoryInitV1(&history, storage, 4, 10, &config));
  pose = make_pose(10, 1, 1, 1, 100, 1.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  pose = make_pose(10, 1, 1, 2, 200, 2.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  pose = make_pose(10, 1, 2, 3, 300, 3.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK((storage[2].flags & WORR_REWIND_POSE_DISCONTINUITY_MAP) != 0);
  pose = make_pose(10, 1, 2, 4, 400, 4.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));

  memset(&query, 0, sizeof(query));
  query.struct_size = sizeof(query);
  query.schema_version = WORR_REWIND_ABI_VERSION;
  query.entity.index = 10;
  query.entity.generation = 1;
  query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
  query.map_epoch = 1;
  query.target_time_us = 150;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.found == 1 &&
        result.reason == WORR_REWIND_QUERY_INTERPOLATED);
  query.target_time_us = 250;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.found == 0 &&
        result.reason == WORR_REWIND_QUERY_MISS_FUTURE);

  query.map_epoch = 2;
  query.target_time_us = 350;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.found == 1 &&
        result.reason == WORR_REWIND_QUERY_INTERPOLATED);
  query.target_time_us = 250;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.found == 0 &&
        result.reason == WORR_REWIND_QUERY_MISS_TOO_OLD);
}

static void test_history_ring_wrap(void) {
  worr_rewind_history_config_v1 config;
  worr_rewind_history_v1 history;
  worr_rewind_pose_v1 storage[2];
  worr_rewind_pose_v1 pose;
  worr_rewind_pose_query_v1 query;
  worr_rewind_pose_result_v1 result;
  uint32_t reason;

  Worr_RewindHistoryConfigDefaultsV1(&config);
  CHECK(Worr_RewindHistoryInitV1(&history, storage, 2, 9, &config));
  pose = make_pose(9, 1, 1, 1, 100, 1.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  pose = make_pose(9, 1, 1, 2, 200, 2.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  pose = make_pose(9, 1, 1, 3, 300, 3.0f);
  CHECK(Worr_RewindHistoryAppendV1(&history, &pose, &reason));
  CHECK(history.count == 2 && history.head == 1);
  CHECK(history.telemetry.overwritten == 1);
  CHECK(Worr_RewindHistoryValidateV1(&history));

  memset(&query, 0, sizeof(query));
  query.struct_size = sizeof(query);
  query.schema_version = WORR_REWIND_ABI_VERSION;
  query.entity.index = 9;
  query.entity.generation = 1;
  query.map_epoch = 1;
  query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
  query.target_time_us = 200;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.found == 1 && result.reason == WORR_REWIND_QUERY_EXACT);
  CHECK(result.pose.origin[0] == 2.0f);
  query.target_time_us = 250;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.found == 1 &&
        result.reason == WORR_REWIND_QUERY_INTERPOLATED);
  CHECK(fabsf(result.pose.origin[0] - 2.5f) < 0.001f);
  query.target_time_us = 150;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.found == 0 &&
        result.reason == WORR_REWIND_QUERY_MISS_TOO_OLD);
  query.target_time_us = 350;
  CHECK(Worr_RewindHistoryQueryV1(&history, &query, &result));
  CHECK(result.found == 0 &&
        result.reason == WORR_REWIND_QUERY_MISS_FUTURE);
}

static void test_scene_and_ignore_sets(void) {
  const uint64_t target = 500;
  worr_rewind_policy_decision_v1 decision = make_decision(1, target);
  worr_rewind_scene_v1 scene_a;
  worr_rewind_scene_v1 scene_b;
  worr_rewind_scene_v1 before_scene;
  worr_rewind_scene_candidate_v1 storage_a[4];
  worr_rewind_scene_candidate_v1 storage_b[4];
  worr_rewind_scene_candidate_v1 before_storage[4];
  worr_rewind_pose_v1 mover_pose = make_pose(10, 1, 1, 50, target, 50.0f);
  worr_rewind_pose_v1 player_pose = make_pose(20, 3, 1, 50, target, 60.0f);
  worr_rewind_pose_result_v1 mover_result;
  worr_rewind_pose_result_v1 player_result;
  worr_rewind_ignore_set_v1 ignore_a;
  worr_rewind_ignore_set_v1 ignore_b;
  worr_event_entity_ref_v1 ignore_storage_a[3];
  worr_event_entity_ref_v1 ignore_storage_b[3];
  worr_rewind_trace_view_v1 view_a;
  worr_rewind_trace_view_v1 view_b;
  worr_event_entity_ref_v1 ref;
  bool contains = false;

  mover_pose.collision_shape = WORR_REWIND_COLLISION_BRUSH_MODEL;
  mover_pose.collision_asset_id = 7;
  attach_mover(&player_pose, mover_pose.entity, 10.0f);
  mover_result = make_result(&mover_pose, target, WORR_REWIND_QUERY_EXACT,
                             WORR_REWIND_DISCRETE_EXACT, 0);
  player_result = make_result(&player_pose, target, WORR_REWIND_QUERY_EXACT,
                              WORR_REWIND_DISCRETE_EXACT, 0);

  CHECK(Worr_RewindPolicyDecisionValidateV1(&decision, true));
  CHECK(Worr_RewindSceneInitV1(&scene_a, storage_a, 4, &decision));
  CHECK(Worr_RewindSceneAddResultV1(&scene_a, &player_result));
  before_scene = scene_a;
  memcpy(before_storage, storage_a, sizeof(storage_a));
  CHECK(!Worr_RewindSceneAddResultV1(&scene_a, &mover_result));
  CHECK(memcmp(&scene_a, &before_scene, sizeof(scene_a)) == 0);
  CHECK(memcmp(storage_a, before_storage, sizeof(storage_a)) == 0);
  CHECK(Worr_RewindSceneInitV1(&scene_a, storage_a, 4, &decision));
  CHECK(Worr_RewindSceneAddResultV1(&scene_a, &mover_result));
  CHECK(Worr_RewindSceneAddResultV1(&scene_a, &player_result));
  CHECK(scene_a.slots[0].pose.entity.index == 10);
  CHECK(scene_a.slots[1].pose.entity.index == 20);
  CHECK(Worr_RewindSceneSealV1(&scene_a));
  CHECK(Worr_RewindSceneValidateV1(&scene_a));

  CHECK(Worr_RewindSceneInitV1(&scene_b, storage_b, 4, &decision));
  CHECK(Worr_RewindSceneAddOwnedResultV1(&scene_b, &mover_result));
  CHECK(Worr_RewindSceneAddOwnedResultV1(&scene_b, &player_result));
  CHECK(Worr_RewindSceneSealV1(&scene_b));
  CHECK(Worr_RewindSceneValidateV1(&scene_b));
  CHECK(scene_a.scene_hash == scene_b.scene_hash);
  CHECK(memcmp(scene_a.slots, scene_b.slots,
               scene_a.count * sizeof(scene_a.slots[0])) == 0);

  before_scene = scene_a;
  memcpy(before_storage, storage_a, sizeof(storage_a));
  CHECK(!Worr_RewindSceneAddResultV1(&scene_a, &mover_result));
  CHECK(memcmp(&scene_a, &before_scene, sizeof(scene_a)) == 0);
  CHECK(memcmp(storage_a, before_storage, sizeof(storage_a)) == 0);

  CHECK(Worr_RewindIgnoreSetInitV1(&ignore_a, ignore_storage_a, 3));
  ref.index = 20;
  ref.generation = 3;
  CHECK(Worr_RewindIgnoreSetAddV1(&ignore_a, ref));
  ref.index = 2;
  ref.generation = 1;
  CHECK(Worr_RewindIgnoreSetAddV1(&ignore_a, ref));
  CHECK(ignore_a.slots[0].index == 2 && ignore_a.slots[1].index == 20);
  CHECK(Worr_RewindIgnoreSetAddV1(&ignore_a, ref));
  CHECK(ignore_a.count == 2);
  CHECK(Worr_RewindIgnoreSetContainsV1(&ignore_a, ref, &contains));
  CHECK(contains);

  CHECK(Worr_RewindIgnoreSetInitV1(&ignore_b, ignore_storage_b, 3));
  ref.index = 99;
  ref.generation = 1;
  CHECK(Worr_RewindIgnoreSetAddV1(&ignore_b, ref));
  CHECK(Worr_RewindTraceViewV1(&scene_a, &ignore_a, &view_a));
  CHECK(Worr_RewindTraceViewV1(&scene_a, &ignore_b, &view_b));
  CHECK(view_a.scene_hash == view_b.scene_hash);
  CHECK(view_a.candidates == scene_a.slots && view_a.candidate_count == 2);
  CHECK(view_a.ignore_count == 2 && view_b.ignore_count == 1);
  CHECK(view_a.target_time_us == target);

  {
    worr_rewind_ignore_set_v1 before_ignore = ignore_a;
    worr_event_entity_ref_v1 before_ignored[3];
    memcpy(before_ignored, ignore_storage_a, sizeof(before_ignored));
    CHECK(!Worr_RewindTraceViewV1(
        &scene_a, &ignore_a,
        (worr_rewind_trace_view_v1 *)(void *)ignore_storage_a));
    CHECK(memcmp(&ignore_a, &before_ignore, sizeof(ignore_a)) == 0);
    CHECK(memcmp(ignore_storage_a, before_ignored, sizeof(before_ignored)) ==
          0);
  }

  ref.index = 30;
  ref.generation = 1;
  CHECK(Worr_RewindIgnoreSetAddV1(&ignore_a, ref));
  {
    worr_rewind_ignore_set_v1 before_ignore = ignore_a;
    worr_event_entity_ref_v1 before_ignored[3];
    memcpy(before_ignored, ignore_storage_a, sizeof(before_ignored));
    ref.index = 40;
    CHECK(!Worr_RewindIgnoreSetAddV1(&ignore_a, ref));
    CHECK(memcmp(&ignore_a, &before_ignore, sizeof(ignore_a)) == 0);
    CHECK(memcmp(ignore_storage_a, before_ignored, sizeof(before_ignored)) ==
          0);
  }

  storage_a[0].pose.origin[0] += 1.0f;
  CHECK(!Worr_RewindSceneValidateV1(&scene_a));
  memcpy(storage_a, before_storage, sizeof(storage_a));
  scene_a = before_scene;
  CHECK(Worr_RewindSceneValidateV1(&scene_a));
}

static void test_scene_failures(void) {
  const uint64_t target = 500;
  worr_rewind_policy_decision_v1 decision = make_decision(1, target);
  worr_rewind_scene_v1 scene;
  worr_rewind_scene_v1 before_scene;
  worr_rewind_scene_candidate_v1 storage[4];
  worr_rewind_scene_candidate_v1 before_storage[4];
  worr_rewind_pose_v1 pose_a = make_pose(30, 1, 1, 50, target, 1.0f);
  worr_rewind_pose_v1 pose_b = make_pose(31, 1, 1, 50, target, 2.0f);
  worr_rewind_pose_result_v1 result_a;
  worr_rewind_pose_result_v1 result_b;
  union {
    max_align_t alignment;
    unsigned char bytes[sizeof(worr_rewind_scene_v1) +
                        2 * sizeof(worr_rewind_scene_candidate_v1)];
  } hostile;
  unsigned char before_hostile[sizeof(hostile.bytes)];

  attach_mover(&pose_a, pose_b.entity, 0.0f);
  result_a = make_result(&pose_a, target, WORR_REWIND_QUERY_EXACT,
                         WORR_REWIND_DISCRETE_EXACT, 0);
  CHECK(Worr_RewindSceneInitV1(&scene, storage, 4, &decision));
  CHECK(Worr_RewindSceneAddResultV1(&scene, &result_a));
  before_scene = scene;
  memcpy(before_storage, storage, sizeof(storage));
  CHECK(!Worr_RewindSceneSealV1(&scene));
  CHECK(memcmp(&scene, &before_scene, sizeof(scene)) == 0);
  CHECK(memcmp(storage, before_storage, sizeof(storage)) == 0);

  /*
   * The owned builder deliberately avoids rescanning prior caller-owned slots,
   * but the mandatory seal still rejects any between-call corruption before
   * the scene can become traceable.
   */
  result_b = make_result(&pose_b, target, WORR_REWIND_QUERY_EXACT,
                         WORR_REWIND_DISCRETE_EXACT, 0);
  CHECK(Worr_RewindSceneInitV1(&scene, storage, 4, &decision));
  CHECK(Worr_RewindSceneAddOwnedResultV1(&scene, &result_a));
  CHECK(Worr_RewindSceneAddOwnedResultV1(&scene, &result_b));
  storage[0].pose.origin[0] += 1.0f;
  before_scene = scene;
  CHECK(!Worr_RewindSceneSealV1(&scene));
  CHECK(memcmp(&scene, &before_scene, sizeof(scene)) == 0);
  CHECK((scene.flags & WORR_REWIND_SCENE_SEALED) == 0);

  CHECK(Worr_RewindSceneInitV1(&scene, storage, 4, &decision));
  CHECK(Worr_RewindSceneAddResultV1(&scene, &result_a));
  attach_mover(&pose_b, pose_a.entity, 0.0f);
  result_b = make_result(&pose_b, target, WORR_REWIND_QUERY_EXACT,
                         WORR_REWIND_DISCRETE_EXACT, 0);
  CHECK(Worr_RewindSceneAddResultV1(&scene, &result_b));
  before_scene = scene;
  CHECK(!Worr_RewindSceneSealV1(&scene));
  CHECK(memcmp(&scene, &before_scene, sizeof(scene)) == 0);

  CHECK(Worr_RewindSceneInitV1(&scene, storage, 4, &decision));
  before_scene = scene;
  memcpy(before_storage, storage, sizeof(storage));
  CHECK(!Worr_RewindSceneAddResultV1(
      &scene, (const worr_rewind_pose_result_v1 *)(const void *)storage));
  CHECK(memcmp(&scene, &before_scene, sizeof(scene)) == 0);
  CHECK(memcmp(storage, before_storage, sizeof(storage)) == 0);

  memset(hostile.bytes, 0xa5, sizeof(hostile.bytes));
  memcpy(before_hostile, hostile.bytes, sizeof(before_hostile));
  CHECK(!Worr_RewindSceneInitV1(
      (worr_rewind_scene_v1 *)(void *)hostile.bytes,
      (worr_rewind_scene_candidate_v1 *)(void *)hostile.bytes, 2, &decision));
  CHECK(memcmp(hostile.bytes, before_hostile, sizeof(before_hostile)) == 0);
}

int main(void) {
  test_snapshot_projection();
  test_policy();
  test_policy_bounds();
  test_policy_mapping_proof();
  test_history();
  test_history_discontinuity_rules();
  test_history_hostile_layout();
  test_history_map_epoch_query_boundaries();
  test_history_ring_wrap();
  test_scene_and_ignore_sets();
  test_scene_failures();
  if (failures != 0) {
    fprintf(stderr, "rewind core: %d failure(s)\n", failures);
    return 1;
  }
  puts("rewind core: ok");
  return 0;
}
