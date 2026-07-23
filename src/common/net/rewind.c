/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/rewind.h"

#include <math.h>
#include <string.h>

#define WORR_FNV_OFFSET UINT64_C(14695981039346656037)
#define WORR_FNV_PRIME UINT64_C(1099511628211)

typedef struct worr_range_s {
  uintptr_t begin;
  uintptr_t end;
} worr_range;

static bool checked_mul_size(size_t a, size_t b, size_t *out) {
  if (!out || (a != 0 && b > SIZE_MAX / a))
    return false;
  *out = a * b;
  return true;
}

static bool make_range(const void *pointer, size_t bytes, worr_range *out) {
  uintptr_t begin;
  if (!pointer || !out || bytes == 0)
    return false;
  begin = (uintptr_t)pointer;
  if (bytes > UINTPTR_MAX - begin)
    return false;
  out->begin = begin;
  out->end = begin + bytes;
  return true;
}

static bool ranges_overlap(worr_range a, worr_range b) {
  return a.begin < b.end && b.begin < a.end;
}

static bool range_aligned(const void *pointer, size_t alignment) {
  return pointer && alignment != 0 &&
         ((uintptr_t)pointer % (uintptr_t)alignment) == 0;
}

static void saturating_increment(uint64_t *value) {
  if (*value != UINT64_MAX)
    ++*value;
}

static uint64_t absolute_difference_u64(uint64_t a, uint64_t b) {
  return a >= b ? a - b : b - a;
}

static bool entity_ref_absent(worr_event_entity_ref_v1 ref) {
  return ref.index == 0 && ref.generation == 0;
}

static bool entity_ref_valid(worr_event_entity_ref_v1 ref, bool absent_ok) {
  return (ref.index != 0 && ref.generation != 0) ||
         (absent_ok && entity_ref_absent(ref));
}

static bool entity_ref_equal(worr_event_entity_ref_v1 a,
                             worr_event_entity_ref_v1 b) {
  return a.index == b.index && a.generation == b.generation;
}

static bool snapshot_id_valid(worr_snapshot_id_v2 id) {
  return id.epoch != 0 && id.sequence != 0;
}

static bool float_valid(float value) { return isfinite(value) != 0; }

static float canonical_float(float value) {
  return value == 0.0f ? 0.0f : value;
}

static bool vector_valid(const float value[3]) {
  return float_valid(value[0]) && float_valid(value[1]) &&
         float_valid(value[2]);
}

static bool vector_zero(const float value[3]) {
  return value[0] == 0.0f && value[1] == 0.0f && value[2] == 0.0f;
}

static void canonicalize_vector(float value[3]) {
  value[0] = canonical_float(value[0]);
  value[1] = canonical_float(value[1]);
  value[2] = canonical_float(value[2]);
}

static uint64_t hash_byte(uint64_t hash, uint8_t value) {
  return (hash ^ value) * WORR_FNV_PRIME;
}

static uint64_t hash_u32(uint64_t hash, uint32_t value) {
  unsigned int i;
  for (i = 0; i < 4; ++i)
    hash = hash_byte(hash, (uint8_t)(value >> (i * 8)));
  return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value) {
  unsigned int i;
  for (i = 0; i < 8; ++i)
    hash = hash_byte(hash, (uint8_t)(value >> (i * 8)));
  return hash;
}

static uint64_t hash_float(uint64_t hash, float value) {
  uint32_t bits;
  value = canonical_float(value);
  memcpy(&bits, &value, sizeof(bits));
  return hash_u32(hash, bits);
}

static uint64_t hash_vector(uint64_t hash, const float value[3]) {
  hash = hash_float(hash, value[0]);
  hash = hash_float(hash, value[1]);
  return hash_float(hash, value[2]);
}

static bool policy_config_valid(const worr_rewind_policy_config_v1 *config) {
  const uint32_t flags = WORR_REWIND_POLICY_ALLOW_LEGACY_PACKET_SHARED |
                         WORR_REWIND_POLICY_REQUIRE_CONSUMED_COMMAND;
  return config &&
         range_aligned(config, _Alignof(worr_rewind_policy_config_v1)) &&
         config->struct_size == sizeof(*config) &&
         config->schema_version == WORR_REWIND_ABI_VERSION &&
         (config->flags & ~flags) == 0 && config->reserved0 == 0 &&
         config->target_window_us != 0 &&
         config->target_window_us <= config->hard_window_us &&
         config->hard_window_us <= WORR_REWIND_HARD_LIMIT_US &&
         config->future_tolerance_us <= config->hard_window_us &&
         config->max_clock_skew_us <= UINT64_C(1000000) &&
         config->max_legacy_error_us <= config->hard_window_us;
}

void Worr_RewindPolicyConfigDefaultsV1(worr_rewind_policy_config_v1 *config) {
  worr_rewind_policy_config_v1 output;
  if (!range_aligned(config, _Alignof(worr_rewind_policy_config_v1)))
    return;
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_REWIND_ABI_VERSION;
  output.flags = WORR_REWIND_POLICY_ALLOW_LEGACY_PACKET_SHARED |
                 WORR_REWIND_POLICY_REQUIRE_CONSUMED_COMMAND;
  output.target_window_us = WORR_REWIND_DEFAULT_TARGET_US;
  output.hard_window_us = WORR_REWIND_HARD_LIMIT_US;
  output.future_tolerance_us = WORR_REWIND_DEFAULT_FUTURE_TOLERANCE_US;
  output.max_clock_skew_us = WORR_REWIND_DEFAULT_CLOCK_SKEW_US;
  output.max_legacy_error_us = WORR_REWIND_DEFAULT_LEGACY_ERROR_US;
  *config = output;
}

bool Worr_RewindPolicyConfigValidateV1(
    const worr_rewind_policy_config_v1 *config) {
  return policy_config_valid(config);
}

bool Worr_RewindPolicyStateInitV1(worr_rewind_policy_state_v1 *state) {
  worr_rewind_policy_state_v1 output;
  if (!state || !range_aligned(state, _Alignof(worr_rewind_policy_state_v1)))
    return false;
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_REWIND_ABI_VERSION;
  *state = output;
  return true;
}

static bool policy_state_valid(const worr_rewind_policy_state_v1 *state) {
  if (!state || state->struct_size != sizeof(*state) ||
      state->schema_version != WORR_REWIND_ABI_VERSION ||
      state->initialized > 1 || state->reserved0 != 0) {
    return false;
  }
  if (!state->initialized) {
    return state->map_epoch == 0 && state->last_command_id.epoch == 0 &&
           state->last_command_id.sequence == 0 &&
           state->last_snapshot_id.epoch == 0 &&
           state->last_snapshot_id.sequence == 0 &&
           state->last_server_tick == 0 && state->last_server_time_us == 0 &&
           state->last_snapshot_flags == 0 &&
           state->last_tick_interval_us == 0 &&
           state->last_consumed_command.cursor.epoch == 0 &&
           state->last_consumed_command.cursor.contiguous_sequence == 0 &&
           state->last_consumed_command.provenance ==
               WORR_SNAPSHOT_CONSUMED_COMMAND_NONE &&
           state->last_consumed_command.reserved0 == 0;
  }
  return state->map_epoch != 0 &&
         Worr_CommandIdValidV1(state->last_command_id, false) &&
         snapshot_id_valid(state->last_snapshot_id) &&
         state->last_snapshot_id.epoch == state->map_epoch &&
         state->last_tick_interval_us != 0 &&
         state->last_tick_interval_us <= WORR_COMMAND_MAX_TICK_INTERVAL_US &&
         (state->last_snapshot_flags &
          ~((uint32_t)WORR_REWIND_SNAPSHOT_PAUSED |
            (uint32_t)WORR_REWIND_SNAPSHOT_MAP_RESET |
            (uint32_t)WORR_REWIND_SNAPSHOT_HARD_RESYNC)) == 0 &&
         state->last_consumed_command.reserved0 == 0 &&
         state->last_consumed_command.provenance <=
             WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED &&
         ((state->last_consumed_command.provenance ==
               WORR_SNAPSHOT_CONSUMED_COMMAND_NONE &&
           state->last_consumed_command.cursor.epoch == 0 &&
           state->last_consumed_command.cursor.contiguous_sequence == 0) ||
          (state->last_consumed_command.provenance ==
               WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED &&
           state->last_consumed_command.cursor.epoch != 0));
}

static bool snapshot_time_valid(const worr_rewind_snapshot_time_v1 *snapshot) {
  const uint32_t allowed_flags = WORR_REWIND_SNAPSHOT_PAUSED |
                                 WORR_REWIND_SNAPSHOT_MAP_RESET |
                                 WORR_REWIND_SNAPSHOT_HARD_RESYNC;
  if (!snapshot || snapshot->struct_size != sizeof(*snapshot) ||
      snapshot->schema_version != WORR_REWIND_ABI_VERSION ||
      (snapshot->flags & ~allowed_flags) != 0 ||
      snapshot->tick_interval_us == 0 ||
      snapshot->tick_interval_us > WORR_COMMAND_MAX_TICK_INTERVAL_US ||
      !snapshot_id_valid(snapshot->snapshot_id) || snapshot->reserved0 != 0 ||
      snapshot->consumed_command.reserved0 != 0 ||
      snapshot->consumed_command.provenance >
          WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED) {
    return false;
  }
  if (snapshot->consumed_command.provenance ==
      WORR_SNAPSHOT_CONSUMED_COMMAND_NONE) {
    return snapshot->consumed_command.cursor.epoch == 0 &&
           snapshot->consumed_command.cursor.contiguous_sequence == 0;
  }
  return snapshot->consumed_command.cursor.epoch != 0;
}

bool Worr_RewindSnapshotTimeFromCanonicalV2(
    const worr_snapshot_v2 *snapshot, uint32_t tick_interval_us, uint32_t flags,
    worr_rewind_snapshot_time_v1 *time_out) {
  worr_range source_range;
  worr_range output_range;
  worr_rewind_snapshot_time_v1 output;
  const uint32_t caller_flags = WORR_REWIND_SNAPSHOT_PAUSED;
  if (!snapshot || !time_out || (flags & ~caller_flags) != 0 ||
      tick_interval_us == 0 ||
      tick_interval_us > WORR_COMMAND_MAX_TICK_INTERVAL_US ||
      !range_aligned(snapshot, _Alignof(worr_snapshot_v2)) ||
      !range_aligned(time_out, _Alignof(worr_rewind_snapshot_time_v1)) ||
      !make_range(snapshot, sizeof(*snapshot), &source_range) ||
      !make_range(time_out, sizeof(*time_out), &output_range) ||
      ranges_overlap(source_range, output_range)) {
    return false;
  }
  if (!Worr_SnapshotValidateV2(snapshot, UINT32_MAX)) {
    return false;
  }
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_REWIND_ABI_VERSION;
  output.flags = flags;
  if ((snapshot->discontinuity.flags & WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET) !=
      0) {
    output.flags |= WORR_REWIND_SNAPSHOT_MAP_RESET;
  }
  if ((snapshot->discontinuity.flags &
       WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC) != 0) {
    output.flags |= WORR_REWIND_SNAPSHOT_HARD_RESYNC;
  }
  output.tick_interval_us = tick_interval_us;
  output.snapshot_id = snapshot->snapshot_id;
  output.server_tick = snapshot->server_tick;
  output.server_time_us = snapshot->server_time_us;
  output.consumed_command = snapshot->consumed_command;
  if (!snapshot_time_valid(&output))
    return false;
  *time_out = output;
  return true;
}

static bool mapping_proof_valid(const worr_rewind_mapping_proof_v1 *proof) {
  worr_command_render_watermark_v1 watermark;
  if (!proof || !range_aligned(proof, _Alignof(worr_rewind_mapping_proof_v1)) ||
      proof->struct_size != sizeof(*proof) ||
      proof->schema_version != WORR_REWIND_ABI_VERSION ||
      proof->flags != WORR_REWIND_MAPPING_AUTHENTICATED_TIMELINE ||
      proof->reserved0 != 0 ||
      !Worr_CommandIdValidV1(proof->command_id, false) ||
      !snapshot_id_valid(proof->source_snapshot_id)) {
    return false;
  }
  memset(&watermark, 0, sizeof(watermark));
  watermark.struct_size = sizeof(watermark);
  watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  watermark.provenance = proof->watermark_provenance;
  watermark.flags = proof->watermark_flags;
  watermark.source_server_tick = proof->source_server_tick;
  watermark.tick_interval_us = proof->tick_interval_us;
  watermark.source_server_time_us = proof->source_server_time_us;
  watermark.rendered_server_time_us = proof->rendered_server_time_us;
  if (!Worr_CommandRenderWatermarkValidateV1(&watermark))
    return false;
  if (proof->watermark_provenance ==
      WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED) {
    return proof->mapped_server_time_us <= proof->rendered_server_time_us &&
           proof->mapping_error_bound_us != 0;
  }
  return proof->mapped_server_time_us == proof->rendered_server_time_us &&
         proof->mapping_error_bound_us == 0;
}

bool Worr_RewindMappingProofValidateV1(
    const worr_rewind_mapping_proof_v1 *proof) {
  return mapping_proof_valid(proof);
}

static bool
mapping_proof_matches(const worr_rewind_mapping_proof_v1 *proof,
                      const worr_command_record_v1 *command,
                      const worr_rewind_snapshot_time_v1 *snapshot) {
  const worr_command_render_watermark_v1 *watermark =
      &command->render_watermark;
  if (!mapping_proof_valid(proof) ||
      proof->command_id.epoch != command->command_id.epoch ||
      proof->command_id.sequence != command->command_id.sequence ||
      proof->watermark_provenance != watermark->provenance ||
      proof->watermark_flags != watermark->flags ||
      proof->source_server_tick != watermark->source_server_tick ||
      proof->tick_interval_us != watermark->tick_interval_us ||
      proof->source_server_time_us != watermark->source_server_time_us ||
      proof->rendered_server_time_us != watermark->rendered_server_time_us ||
      proof->source_snapshot_id.epoch != snapshot->snapshot_id.epoch ||
      proof->source_snapshot_id.sequence > snapshot->snapshot_id.sequence) {
    return false;
  }
  if (proof->source_snapshot_id.sequence == snapshot->snapshot_id.sequence &&
      (proof->source_server_tick != snapshot->server_tick ||
       proof->source_server_time_us != snapshot->server_time_us)) {
    return false;
  }
  return true;
}

static void policy_rejected(worr_rewind_policy_state_v1 *state,
                            worr_rewind_policy_decision_v1 *decision,
                            uint32_t reason) {
  decision->reason = reason;
  switch (reason) {
  case WORR_REWIND_POLICY_REJECT_INVALID_COMMAND:
  case WORR_REWIND_POLICY_REJECT_INVALID_SNAPSHOT:
    saturating_increment(&state->telemetry.rejected_invalid);
    break;
  case WORR_REWIND_POLICY_REJECT_NO_WATERMARK:
  case WORR_REWIND_POLICY_REJECT_LEGACY_UNBOUNDED:
  case WORR_REWIND_POLICY_REJECT_UNCONSUMED_COMMAND:
  case WORR_REWIND_POLICY_REJECT_MAPPING_PROOF:
    saturating_increment(&state->telemetry.rejected_untrusted);
    break;
  case WORR_REWIND_POLICY_REJECT_COMMAND_REPLAY:
  case WORR_REWIND_POLICY_REJECT_COMMAND_EPOCH:
    saturating_increment(&state->telemetry.rejected_replay);
    break;
  case WORR_REWIND_POLICY_REJECT_COMMAND_SEQUENCE_EXHAUSTED:
  case WORR_REWIND_POLICY_REJECT_SNAPSHOT_SEQUENCE_EXHAUSTED:
  case WORR_REWIND_POLICY_REJECT_TIME_EXHAUSTED:
    saturating_increment(&state->telemetry.rejected_exhausted);
    break;
  case WORR_REWIND_POLICY_REJECT_FUTURE:
    saturating_increment(&state->telemetry.rejected_future);
    break;
  case WORR_REWIND_POLICY_REJECT_TOO_OLD:
    saturating_increment(&state->telemetry.rejected_too_old);
    break;
  case WORR_REWIND_POLICY_REJECT_CLOCK_ABUSE:
  case WORR_REWIND_POLICY_REJECT_SNAPSHOT_REGRESSION:
    saturating_increment(&state->telemetry.rejected_clock);
    break;
  case WORR_REWIND_POLICY_REJECT_DISCONTINUITY:
    saturating_increment(&state->telemetry.rejected_discontinuity);
    break;
  default:
    saturating_increment(&state->telemetry.rejected_invalid);
    break;
  }
}

static bool policy_ranges_valid(worr_rewind_policy_state_v1 *state,
                                const worr_rewind_policy_config_v1 *config,
                                const worr_command_record_v1 *command,
                                const worr_rewind_snapshot_time_v1 *snapshot,
                                const worr_rewind_mapping_proof_v1 *proof,
                                worr_rewind_policy_decision_v1 *decision) {
  worr_range ranges[6];
  size_t i;
  size_t j;
  if (!range_aligned(state, _Alignof(worr_rewind_policy_state_v1)) ||
      !range_aligned(config, _Alignof(worr_rewind_policy_config_v1)) ||
      !range_aligned(command, _Alignof(worr_command_record_v1)) ||
      !range_aligned(snapshot, _Alignof(worr_rewind_snapshot_time_v1)) ||
      !range_aligned(proof, _Alignof(worr_rewind_mapping_proof_v1)) ||
      !range_aligned(decision, _Alignof(worr_rewind_policy_decision_v1)) ||
      !make_range(state, sizeof(*state), &ranges[0]) ||
      !make_range(config, sizeof(*config), &ranges[1]) ||
      !make_range(command, sizeof(*command), &ranges[2]) ||
      !make_range(snapshot, sizeof(*snapshot), &ranges[3]) ||
      !make_range(proof, sizeof(*proof), &ranges[4]) ||
      !make_range(decision, sizeof(*decision), &ranges[5])) {
    return false;
  }
  for (i = 0; i < 6; ++i) {
    for (j = i + 1; j < 6; ++j) {
      if (ranges_overlap(ranges[i], ranges[j]))
        return false;
    }
  }
  return true;
}

bool Worr_RewindPolicyResolveV1(worr_rewind_policy_state_v1 *state,
                                const worr_rewind_policy_config_v1 *config,
                                const worr_command_record_v1 *command,
                                const worr_rewind_snapshot_time_v1 *snapshot,
                                const worr_rewind_mapping_proof_v1 *proof,
                                worr_rewind_policy_decision_v1 *decision_out) {
  worr_rewind_policy_state_v1 next;
  worr_rewind_policy_decision_v1 decision;
  const worr_command_render_watermark_v1 *watermark;
  uint64_t expected_elapsed;
  uint64_t actual_elapsed;
  uint64_t age;
  bool accepted = false;

  if (!state || !config || !command || !snapshot || !proof || !decision_out ||
      !policy_ranges_valid(state, config, command, snapshot, proof,
                           decision_out) ||
      !policy_state_valid(state) || !policy_config_valid(config)) {
    return false;
  }

  next = *state;
  memset(&decision, 0, sizeof(decision));
  decision.struct_size = sizeof(decision);
  decision.schema_version = WORR_REWIND_ABI_VERSION;
  decision.command_id = command->command_id;
  decision.snapshot_id = snapshot->snapshot_id;
  saturating_increment(&next.telemetry.requests);

  if (!Worr_CommandRecordValidateV1(command,
                                    WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS)) {
    policy_rejected(&next, &decision,
                    WORR_REWIND_POLICY_REJECT_INVALID_COMMAND);
    goto finish;
  }
  if (!snapshot_time_valid(snapshot)) {
    policy_rejected(&next, &decision,
                    WORR_REWIND_POLICY_REJECT_INVALID_SNAPSHOT);
    goto finish;
  }
  watermark = &command->render_watermark;
  decision.watermark_provenance = watermark->provenance;
  if (watermark->provenance == WORR_COMMAND_RENDER_PROVENANCE_NONE) {
    policy_rejected(&next, &decision, WORR_REWIND_POLICY_REJECT_NO_WATERMARK);
    goto finish;
  }
  if (!mapping_proof_matches(proof, command, snapshot)) {
    policy_rejected(&next, &decision, WORR_REWIND_POLICY_REJECT_MAPPING_PROOF);
    goto finish;
  }
  decision.source_snapshot_id = proof->source_snapshot_id;
  decision.mapping_error_bound_us = proof->mapping_error_bound_us;

  if (next.initialized) {
    if (snapshot->snapshot_id.epoch < next.map_epoch) {
      policy_rejected(&next, &decision,
                      WORR_REWIND_POLICY_REJECT_SNAPSHOT_REGRESSION);
      goto finish;
    }
    if (snapshot->snapshot_id.epoch > next.map_epoch) {
      if ((snapshot->flags & (WORR_REWIND_SNAPSHOT_MAP_RESET |
                              WORR_REWIND_SNAPSHOT_HARD_RESYNC)) == 0 ||
          next.map_epoch == UINT32_MAX ||
          snapshot->snapshot_id.epoch != next.map_epoch + 1u ||
          snapshot->snapshot_id.sequence != 1 ||
          next.last_command_id.epoch == UINT32_MAX ||
          command->command_id.epoch != next.last_command_id.epoch + 1u ||
          command->command_id.sequence != 1) {
        policy_rejected(&next, &decision,
                        WORR_REWIND_POLICY_REJECT_DISCONTINUITY);
        goto finish;
      }
    } else {
      if (command->command_id.epoch < next.last_command_id.epoch) {
        policy_rejected(&next, &decision,
                        WORR_REWIND_POLICY_REJECT_COMMAND_EPOCH);
        goto finish;
      }
      if (command->command_id.epoch > next.last_command_id.epoch) {
        if (next.last_command_id.sequence != UINT32_MAX ||
            next.last_command_id.epoch == UINT32_MAX ||
            command->command_id.epoch != next.last_command_id.epoch + 1u ||
            command->command_id.sequence != 1) {
          policy_rejected(
              &next, &decision,
              next.last_command_id.epoch == UINT32_MAX
                  ? WORR_REWIND_POLICY_REJECT_COMMAND_SEQUENCE_EXHAUSTED
                  : WORR_REWIND_POLICY_REJECT_COMMAND_EPOCH);
          goto finish;
        }
      } else if (command->command_id.sequence <=
                 next.last_command_id.sequence) {
        policy_rejected(
            &next, &decision,
            next.last_command_id.sequence == UINT32_MAX
                ? WORR_REWIND_POLICY_REJECT_COMMAND_SEQUENCE_EXHAUSTED
                : WORR_REWIND_POLICY_REJECT_COMMAND_REPLAY);
        goto finish;
      }
      if (next.last_snapshot_id.sequence == UINT32_MAX &&
          snapshot->snapshot_id.sequence != UINT32_MAX) {
        policy_rejected(&next, &decision,
                        WORR_REWIND_POLICY_REJECT_SNAPSHOT_SEQUENCE_EXHAUSTED);
        goto finish;
      }
      if (snapshot->snapshot_id.sequence < next.last_snapshot_id.sequence) {
        policy_rejected(&next, &decision,
                        WORR_REWIND_POLICY_REJECT_SNAPSHOT_REGRESSION);
        goto finish;
      }
      if (snapshot->snapshot_id.sequence == next.last_snapshot_id.sequence &&
          (snapshot->server_time_us != next.last_server_time_us ||
           snapshot->server_tick != next.last_server_tick ||
           snapshot->flags != next.last_snapshot_flags ||
           snapshot->tick_interval_us != next.last_tick_interval_us ||
           snapshot->consumed_command.cursor.epoch !=
               next.last_consumed_command.cursor.epoch ||
           snapshot->consumed_command.cursor.contiguous_sequence !=
               next.last_consumed_command.cursor.contiguous_sequence ||
           snapshot->consumed_command.provenance !=
               next.last_consumed_command.provenance)) {
        policy_rejected(&next, &decision,
                        WORR_REWIND_POLICY_REJECT_SNAPSHOT_REGRESSION);
        goto finish;
      }
      if (snapshot->server_time_us < next.last_server_time_us ||
          snapshot->server_tick < next.last_server_tick) {
        policy_rejected(&next, &decision,
                        WORR_REWIND_POLICY_REJECT_SNAPSHOT_REGRESSION);
        goto finish;
      }
      if (next.last_server_time_us == UINT64_MAX &&
          snapshot->snapshot_id.sequence != next.last_snapshot_id.sequence) {
        policy_rejected(&next, &decision,
                        WORR_REWIND_POLICY_REJECT_TIME_EXHAUSTED);
        goto finish;
      }
      if (next.last_server_tick == UINT32_MAX &&
          snapshot->snapshot_id.sequence != next.last_snapshot_id.sequence) {
        policy_rejected(&next, &decision,
                        WORR_REWIND_POLICY_REJECT_TIME_EXHAUSTED);
        goto finish;
      }
      if (snapshot->snapshot_id.sequence != next.last_snapshot_id.sequence) {
        if (snapshot->server_tick == next.last_server_tick &&
            snapshot->server_time_us != next.last_server_time_us) {
          policy_rejected(&next, &decision,
                          WORR_REWIND_POLICY_REJECT_CLOCK_ABUSE);
          goto finish;
        }
        if (snapshot->server_time_us == next.last_server_time_us &&
            (snapshot->flags & WORR_REWIND_SNAPSHOT_PAUSED) == 0) {
          policy_rejected(&next, &decision,
                          WORR_REWIND_POLICY_REJECT_CLOCK_ABUSE);
          goto finish;
        }
      }
    }
  }

  if ((config->flags & WORR_REWIND_POLICY_REQUIRE_CONSUMED_COMMAND) != 0) {
    if (snapshot->consumed_command.provenance !=
            WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED ||
        snapshot->consumed_command.cursor.epoch != command->command_id.epoch ||
        snapshot->consumed_command.cursor.contiguous_sequence <
            command->command_id.sequence) {
      policy_rejected(&next, &decision,
                      WORR_REWIND_POLICY_REJECT_UNCONSUMED_COMMAND);
      goto finish;
    }
  }

  decision.requested_time_us = watermark->rendered_server_time_us;
  decision.mapped_time_us = proof->mapped_server_time_us;
  decision.applied_time_us = proof->mapped_server_time_us;
  if (watermark->provenance ==
      WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED) {
    if ((config->flags & WORR_REWIND_POLICY_ALLOW_LEGACY_PACKET_SHARED) == 0) {
      policy_rejected(&next, &decision,
                      WORR_REWIND_POLICY_REJECT_LEGACY_UNBOUNDED);
      goto finish;
    }
    decision.flags |= WORR_REWIND_DECISION_LEGACY_FALLBACK;
    if (proof->mapping_error_bound_us > config->max_legacy_error_us) {
      policy_rejected(&next, &decision,
                      WORR_REWIND_POLICY_REJECT_LEGACY_UNBOUNDED);
      goto finish;
    }
  }

  if (watermark->source_server_tick > snapshot->server_tick ||
      watermark->source_server_time_us > snapshot->server_time_us ||
      watermark->tick_interval_us != snapshot->tick_interval_us) {
    policy_rejected(&next, &decision, WORR_REWIND_POLICY_REJECT_CLOCK_ABUSE);
    goto finish;
  }
  actual_elapsed = snapshot->server_time_us - watermark->source_server_time_us;
  if ((uint64_t)(snapshot->server_tick - watermark->source_server_tick) >
      UINT64_MAX / snapshot->tick_interval_us) {
    policy_rejected(&next, &decision, WORR_REWIND_POLICY_REJECT_TIME_EXHAUSTED);
    goto finish;
  }
  expected_elapsed =
      (uint64_t)(snapshot->server_tick - watermark->source_server_tick) *
      snapshot->tick_interval_us;
  if ((snapshot->flags & WORR_REWIND_SNAPSHOT_PAUSED) == 0 &&
      absolute_difference_u64(actual_elapsed, expected_elapsed) >
          config->max_clock_skew_us) {
    policy_rejected(&next, &decision, WORR_REWIND_POLICY_REJECT_CLOCK_ABUSE);
    goto finish;
  }

  if (decision.mapped_time_us > snapshot->server_time_us) {
    const uint64_t lead = decision.mapped_time_us - snapshot->server_time_us;
    if (lead > config->future_tolerance_us) {
      policy_rejected(&next, &decision, WORR_REWIND_POLICY_REJECT_FUTURE);
      goto finish;
    }
    decision.applied_time_us = snapshot->server_time_us;
    decision.flags |=
        WORR_REWIND_DECISION_ACCEPTED | WORR_REWIND_DECISION_CLAMPED;
    decision.reason = WORR_REWIND_POLICY_CLAMPED_FUTURE;
    saturating_increment(&next.telemetry.clamped_future);
    accepted = true;
    goto accepted_finish;
  }

  age = snapshot->server_time_us - decision.mapped_time_us;
  if (age > config->hard_window_us) {
    policy_rejected(&next, &decision, WORR_REWIND_POLICY_REJECT_TOO_OLD);
    goto finish;
  }
  if (age > config->target_window_us) {
    decision.applied_time_us =
        snapshot->server_time_us >= config->target_window_us
            ? snapshot->server_time_us - config->target_window_us
            : 0;
    decision.flags |=
        WORR_REWIND_DECISION_ACCEPTED | WORR_REWIND_DECISION_CLAMPED;
    decision.reason = WORR_REWIND_POLICY_CLAMPED_TARGET_WINDOW;
    saturating_increment(&next.telemetry.clamped_target);
    accepted = true;
    goto accepted_finish;
  }

  decision.flags |= WORR_REWIND_DECISION_ACCEPTED;
  if ((decision.flags & WORR_REWIND_DECISION_LEGACY_FALLBACK) != 0) {
    decision.reason = WORR_REWIND_POLICY_LEGACY_BOUNDED;
    saturating_increment(&next.telemetry.accepted_legacy);
  } else {
    decision.reason = WORR_REWIND_POLICY_EXACT;
    saturating_increment(&next.telemetry.accepted_exact);
  }
  accepted = true;

accepted_finish:
  if (accepted) {
    next.initialized = 1;
    next.map_epoch = snapshot->snapshot_id.epoch;
    next.last_command_id = command->command_id;
    next.last_snapshot_id = snapshot->snapshot_id;
    next.last_server_tick = snapshot->server_tick;
    next.last_server_time_us = snapshot->server_time_us;
    next.last_snapshot_flags = snapshot->flags;
    next.last_tick_interval_us = snapshot->tick_interval_us;
    next.last_consumed_command = snapshot->consumed_command;
  }
finish:
  *state = next;
  *decision_out = decision;
  return true;
}

void Worr_RewindHistoryConfigDefaultsV1(worr_rewind_history_config_v1 *config) {
  worr_rewind_history_config_v1 output;
  if (!range_aligned(config, _Alignof(worr_rewind_history_config_v1)))
    return;
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_REWIND_ABI_VERSION;
  output.max_interpolation_span_us = WORR_REWIND_DEFAULT_INTERPOLATION_SPAN_US;
  output.teleport_distance = WORR_REWIND_DEFAULT_TELEPORT_DISTANCE;
  *config = output;
}

bool Worr_RewindHistoryConfigValidateV1(
    const worr_rewind_history_config_v1 *config) {
  return config &&
         range_aligned(config, _Alignof(worr_rewind_history_config_v1)) &&
         config->struct_size == sizeof(*config) &&
         config->schema_version == WORR_REWIND_ABI_VERSION &&
         config->max_interpolation_span_us != 0 &&
         config->max_interpolation_span_us <= WORR_REWIND_HARD_LIMIT_US &&
         float_valid(config->teleport_distance) &&
         config->teleport_distance > 0.0f && config->reserved0 == 0;
}

#if defined(_MSC_VER)
#define WORR_REWIND_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define WORR_REWIND_FORCE_INLINE inline __attribute__((always_inline))
#else
#define WORR_REWIND_FORCE_INLINE inline
#endif

static WORR_REWIND_FORCE_INLINE bool
rewind_pose_valid(const worr_rewind_pose_v1 *pose) {
  if (!pose || !range_aligned(pose, _Alignof(worr_rewind_pose_v1)) ||
      pose->struct_size != sizeof(*pose) ||
      pose->schema_version != WORR_REWIND_ABI_VERSION ||
      pose->model_revision != WORR_REWIND_POSE_MODEL_REVISION ||
      (pose->flags & ~WORR_REWIND_POSE_FLAGS_V1) != 0 ||
      !entity_ref_valid(pose->entity, false) || pose->map_epoch == 0 ||
      pose->lifecycle > WORR_REWIND_LIFECYCLE_DORMANT ||
      !vector_valid(pose->origin) || !vector_valid(pose->angles) ||
      !vector_valid(pose->velocity) || !vector_valid(pose->mins) ||
      !vector_valid(pose->maxs) || !vector_valid(pose->mover_relative_origin) ||
      !vector_valid(pose->mover_relative_angles) || pose->reserved0 != 0 ||
      pose->reserved1 != 0 || pose->mins[0] > pose->maxs[0] ||
      pose->mins[1] > pose->maxs[1] || pose->mins[2] > pose->maxs[2]) {
    return false;
  }
  if ((pose->flags & WORR_REWIND_POSE_HAS_MOVER) != 0) {
    if (!entity_ref_valid(pose->mover, false) ||
        entity_ref_equal(pose->entity, pose->mover))
      return false;
  } else if (!entity_ref_absent(pose->mover) ||
             !vector_zero(pose->mover_relative_origin) ||
             !vector_zero(pose->mover_relative_angles)) {
    return false;
  }
  if ((pose->flags & WORR_REWIND_POSE_LINKED) == 0) {
    if (pose->collision_shape != WORR_REWIND_COLLISION_NONE ||
        pose->collision_asset_id != 0) {
      return false;
    }
  } else if (pose->collision_shape == WORR_REWIND_COLLISION_BOUNDS) {
    if (pose->collision_asset_id != 0)
      return false;
  } else if (pose->collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL) {
    if (pose->collision_asset_id == 0)
      return false;
  } else {
    return false;
  }
  if ((pose->flags & WORR_REWIND_POSE_DAMAGEABLE) != 0 &&
      (pose->flags & WORR_REWIND_POSE_LINKED) == 0) {
    return false;
  }
  if ((pose->flags & WORR_REWIND_POSE_DAMAGEABLE) != 0 &&
      pose->lifecycle != WORR_REWIND_LIFECYCLE_ALIVE) {
    return false;
  }
  return true;
}

bool Worr_RewindPoseValidateV1(const worr_rewind_pose_v1 *pose) {
  return rewind_pose_valid(pose);
}

static void canonicalize_pose(worr_rewind_pose_v1 *pose) {
  canonicalize_vector(pose->origin);
  canonicalize_vector(pose->angles);
  canonicalize_vector(pose->velocity);
  canonicalize_vector(pose->mins);
  canonicalize_vector(pose->maxs);
  canonicalize_vector(pose->mover_relative_origin);
  canonicalize_vector(pose->mover_relative_angles);
}

static uint64_t pose_hash_unchecked(const worr_rewind_pose_v1 *pose) {
  uint64_t hash = WORR_FNV_OFFSET;
  hash = hash_u32(hash, UINT32_C(0x45534f50)); /* POSE */
  hash = hash_u32(hash, pose->model_revision);
  hash = hash_u32(hash, pose->flags);
  hash = hash_u32(hash, pose->entity.index);
  hash = hash_u32(hash, pose->entity.generation);
  hash = hash_u32(hash, pose->mover.index);
  hash = hash_u32(hash, pose->mover.generation);
  hash = hash_u32(hash, pose->map_epoch);
  hash = hash_u32(hash, pose->server_tick);
  hash = hash_u32(hash, pose->lifecycle);
  hash = hash_u32(hash, pose->solid);
  hash = hash_u32(hash, pose->clip_flags);
  hash = hash_u32(hash, pose->collision_shape);
  hash = hash_u32(hash, pose->collision_asset_id);
  hash = hash_u64(hash, pose->server_time_us);
  hash = hash_vector(hash, pose->origin);
  hash = hash_vector(hash, pose->angles);
  hash = hash_vector(hash, pose->velocity);
  hash = hash_vector(hash, pose->mins);
  hash = hash_vector(hash, pose->maxs);
  hash = hash_vector(hash, pose->mover_relative_origin);
  return hash_vector(hash, pose->mover_relative_angles);
}

bool Worr_RewindPoseHashV1(const worr_rewind_pose_v1 *pose,
                           uint64_t *hash_out) {
  worr_range pose_range;
  worr_range output_range;
  if (!pose || !hash_out ||
      !range_aligned(pose, _Alignof(worr_rewind_pose_v1)) ||
      !range_aligned(hash_out, _Alignof(uint64_t)) ||
      !make_range(pose, sizeof(*pose), &pose_range) ||
      !make_range(hash_out, sizeof(*hash_out), &output_range) ||
      ranges_overlap(pose_range, output_range) ||
      !rewind_pose_valid(pose)) {
    return false;
  }
  *hash_out = pose_hash_unchecked(pose);
  return true;
}

static bool history_storage_range(const worr_rewind_history_v1 *history,
                                  worr_range *range_out) {
  size_t bytes;
  return history && history->slots && history->capacity != 0 &&
         checked_mul_size(history->capacity, sizeof(*history->slots), &bytes) &&
         make_range(history->slots, bytes, range_out);
}

static const worr_rewind_pose_v1 *
history_at(const worr_rewind_history_v1 *history,
           uint32_t chronological_index) {
  const uint32_t tail = history->capacity - history->head;
  const uint32_t index = chronological_index < tail
                             ? history->head + chronological_index
                             : chronological_index - tail;
  return &history->slots[index];
}

static const worr_rewind_pose_v1 *
history_newest(const worr_rewind_history_v1 *history) {
  return history_at(history, history->count - 1);
}

static double distance_squared(const float a[3], const float b[3]) {
  const double x = (double)a[0] - (double)b[0];
  const double y = (double)a[1] - (double)b[1];
  const double z = (double)a[2] - (double)b[2];
  return x * x + y * y + z * z;
}

static bool mover_changed(const worr_rewind_pose_v1 *a,
                          const worr_rewind_pose_v1 *b) {
  const bool a_has = (a->flags & WORR_REWIND_POSE_HAS_MOVER) != 0;
  const bool b_has = (b->flags & WORR_REWIND_POSE_HAS_MOVER) != 0;
  return a_has != b_has || (a_has && !entity_ref_equal(a->mover, b->mover));
}

static bool collision_identity_changed(const worr_rewind_pose_v1 *a,
                                       const worr_rewind_pose_v1 *b) {
  const uint32_t semantic_flags =
      WORR_REWIND_POSE_LINKED | WORR_REWIND_POSE_DAMAGEABLE;
  return ((a->flags ^ b->flags) & semantic_flags) != 0 ||
         a->solid != b->solid || a->clip_flags != b->clip_flags ||
         a->collision_shape != b->collision_shape ||
         a->collision_asset_id != b->collision_asset_id;
}

/*
 * Keep this definition ahead of the full-history validator.  It is the sole
 * caller and executes once per adjacent pose, so making the body visible at
 * the call site lets optimized builds fold it into that traversal instead of
 * paying a large out-of-line helper call for every retained sample.
 */
static WORR_REWIND_FORCE_INLINE bool
history_boundary_valid(const worr_rewind_history_v1 *history,
                       const worr_rewind_pose_v1 *older,
                       const worr_rewind_pose_v1 *newer,
                       double teleport_limit_squared) {
  const bool same_entity = entity_ref_equal(newer->entity, older->entity);
  const bool same_mover = !mover_changed(newer, older);
  uint32_t required = 0;
  if (newer->map_epoch != older->map_epoch) {
    return (newer->flags & WORR_REWIND_POSE_DISCONTINUITY_MAP) != 0;
  }
  if (newer->server_time_us - older->server_time_us >
      history->config.max_interpolation_span_us) {
    required |= WORR_REWIND_POSE_DISCONTINUITY_TIME;
  }
  if (!same_entity)
    required |= WORR_REWIND_POSE_DISCONTINUITY_GENERATION;
  if (!same_mover)
    required |= WORR_REWIND_POSE_DISCONTINUITY_MOVER;
  if (collision_identity_changed(newer, older))
    required |= WORR_REWIND_POSE_DISCONTINUITY_COLLISION;
  if (older->lifecycle == WORR_REWIND_LIFECYCLE_ALIVE &&
      newer->lifecycle != WORR_REWIND_LIFECYCLE_ALIVE) {
    required |= WORR_REWIND_POSE_DISCONTINUITY_DEATH;
  }
  if (older->lifecycle != WORR_REWIND_LIFECYCLE_ALIVE &&
      newer->lifecycle == WORR_REWIND_LIFECYCLE_ALIVE) {
    required |= WORR_REWIND_POSE_DISCONTINUITY_RESPAWN;
  }
  if (same_entity && same_mover) {
    const bool relative = (newer->flags & WORR_REWIND_POSE_HAS_MOVER) != 0;
    const float *newer_position =
        relative ? newer->mover_relative_origin : newer->origin;
    const float *older_position =
        relative ? older->mover_relative_origin : older->origin;
    if (distance_squared(newer_position, older_position) >
        teleport_limit_squared) {
      required |= WORR_REWIND_POSE_DISCONTINUITY_TELEPORT;
    }
  }
  return (newer->flags & required) == required;
}

#undef WORR_REWIND_FORCE_INLINE

bool Worr_RewindHistoryInitV1(worr_rewind_history_v1 *history,
                              worr_rewind_pose_v1 *storage, uint32_t capacity,
                              uint32_t entity_index,
                              const worr_rewind_history_config_v1 *config) {
  worr_range envelope_range;
  worr_range storage_range;
  worr_range config_range;
  worr_rewind_history_v1 output;
  size_t storage_bytes;
  if (!history || !storage || !config || capacity < 2 || entity_index == 0 ||
      !range_aligned(history, _Alignof(worr_rewind_history_v1)) ||
      !range_aligned(storage, _Alignof(worr_rewind_pose_v1)) ||
      !range_aligned(config, _Alignof(worr_rewind_history_config_v1)) ||
      !Worr_RewindHistoryConfigValidateV1(config) ||
      !checked_mul_size(capacity, sizeof(*storage), &storage_bytes) ||
      !make_range(history, sizeof(*history), &envelope_range) ||
      !make_range(storage, storage_bytes, &storage_range) ||
      !make_range(config, sizeof(*config), &config_range) ||
      ranges_overlap(envelope_range, storage_range) ||
      ranges_overlap(envelope_range, config_range) ||
      ranges_overlap(storage_range, config_range)) {
    return false;
  }
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_REWIND_ABI_VERSION;
  output.slots = storage;
  output.capacity = capacity;
  output.entity_index = entity_index;
  output.config = *config;
  memset(storage, 0, storage_bytes);
  *history = output;
  return true;
}

bool Worr_RewindHistoryValidateV1(const worr_rewind_history_v1 *history) {
  worr_range envelope_range;
  worr_range storage_range;
  const worr_rewind_pose_v1 *previous = NULL;
  double teleport_limit_squared;
  uint32_t physical_index;
  uint32_t i;
  if (!history || !range_aligned(history, _Alignof(worr_rewind_history_v1)) ||
      history->struct_size != sizeof(*history) ||
      history->schema_version != WORR_REWIND_ABI_VERSION ||
      history->capacity < 2 || history->entity_index == 0 ||
      history->head >= history->capacity ||
      history->count > history->capacity ||
      (history->count < history->capacity && history->head != 0) ||
      !range_aligned(history->slots, _Alignof(worr_rewind_pose_v1)) ||
      !Worr_RewindHistoryConfigValidateV1(&history->config) ||
      !make_range(history, sizeof(*history), &envelope_range) ||
      !history_storage_range(history, &storage_range) ||
      ranges_overlap(envelope_range, storage_range)) {
    return false;
  }
  teleport_limit_squared = (double)history->config.teleport_distance *
                           (double)history->config.teleport_distance;
  physical_index = history->head;
  for (i = 0; i < history->count; ++i) {
    const worr_rewind_pose_v1 *pose = &history->slots[physical_index];
    if (!rewind_pose_valid(pose) ||
        pose->entity.index != history->entity_index) {
      return false;
    }
    if (previous) {
      if (pose->map_epoch < previous->map_epoch)
        return false;
      if (pose->map_epoch == previous->map_epoch &&
          (pose->server_time_us < previous->server_time_us ||
           pose->server_tick < previous->server_tick)) {
        return false;
      }
      if (pose->map_epoch == previous->map_epoch) {
        if (pose->server_tick == previous->server_tick &&
            pose->server_time_us != previous->server_time_us) {
          return false;
        }
        if (pose->server_time_us == previous->server_time_us &&
            (pose->flags & WORR_REWIND_POSE_DISCONTINUITY_PAUSE) == 0) {
          return false;
        }
      }
      if (!history_boundary_valid(history, previous, pose,
                                  teleport_limit_squared))
        return false;
    }
    previous = pose;
    if (++physical_index == history->capacity)
      physical_index = 0;
  }
  return true;
}

static bool append_ranges_valid(worr_rewind_history_v1 *history,
                                const worr_rewind_pose_v1 *pose,
                                uint32_t *reason_out) {
  worr_range ranges[4];
  size_t i;
  size_t j;
  if (!make_range(history, sizeof(*history), &ranges[0]) ||
      !history_storage_range(history, &ranges[1]) ||
      !make_range(pose, sizeof(*pose), &ranges[2]) ||
      !make_range(reason_out, sizeof(*reason_out), &ranges[3])) {
    return false;
  }
  for (i = 0; i < 4; ++i) {
    for (j = i + 1; j < 4; ++j) {
      if (ranges_overlap(ranges[i], ranges[j]))
        return false;
    }
  }
  return true;
}

bool Worr_RewindHistoryAppendV1(worr_rewind_history_v1 *history,
                                const worr_rewind_pose_v1 *pose,
                                uint32_t *reason_out) {
  worr_rewind_history_v1 next;
  worr_rewind_pose_v1 candidate;
  const worr_rewind_pose_v1 *previous;
  uint32_t reason = WORR_REWIND_APPEND_ACCEPTED;
  uint32_t index;
  double teleport_limit_squared;

  if (!history || !pose || !reason_out ||
      !range_aligned(pose, _Alignof(worr_rewind_pose_v1)) ||
      !range_aligned(reason_out, _Alignof(uint32_t)) ||
      !Worr_RewindHistoryValidateV1(history) ||
      !append_ranges_valid(history, pose, reason_out)) {
    return false;
  }
  next = *history;
  candidate = *pose;
  canonicalize_pose(&candidate);
  saturating_increment(&next.telemetry.append_attempts);
  if (!rewind_pose_valid(&candidate)) {
    reason = WORR_REWIND_APPEND_REJECT_INVALID;
    saturating_increment(&next.telemetry.rejected_invalid);
    goto rejected;
  }
  if (candidate.entity.index != history->entity_index) {
    reason = WORR_REWIND_APPEND_REJECT_ENTITY;
    saturating_increment(&next.telemetry.rejected_invalid);
    goto rejected;
  }
  if (history->count != 0) {
    previous = history_newest(history);
    if (candidate.map_epoch < previous->map_epoch) {
      reason = WORR_REWIND_APPEND_REJECT_MAP_REGRESSION;
      saturating_increment(&next.telemetry.rejected_order);
      goto rejected;
    }
    if (candidate.map_epoch > previous->map_epoch) {
      candidate.flags |= WORR_REWIND_POSE_DISCONTINUITY_MAP;
      saturating_increment(&next.telemetry.auto_map_resets);
    } else {
      if (candidate.server_time_us < previous->server_time_us) {
        reason = WORR_REWIND_APPEND_REJECT_TIME_REGRESSION;
        saturating_increment(&next.telemetry.rejected_order);
        goto rejected;
      }
      if (candidate.server_tick < previous->server_tick) {
        reason = WORR_REWIND_APPEND_REJECT_TICK_REGRESSION;
        saturating_increment(&next.telemetry.rejected_order);
        goto rejected;
      }
      if (previous->server_time_us == UINT64_MAX &&
          (candidate.server_tick != previous->server_tick ||
           candidate.server_time_us != previous->server_time_us)) {
        reason = WORR_REWIND_APPEND_REJECT_TIME_EXHAUSTED;
        saturating_increment(&next.telemetry.rejected_order);
        goto rejected;
      }
      if (candidate.server_tick == previous->server_tick &&
          candidate.server_time_us != previous->server_time_us) {
        reason = WORR_REWIND_APPEND_REJECT_TICK_STALL;
        saturating_increment(&next.telemetry.rejected_order);
        goto rejected;
      }
      if (previous->server_tick == UINT32_MAX &&
          candidate.server_time_us != previous->server_time_us) {
        reason = WORR_REWIND_APPEND_REJECT_TICK_EXHAUSTED;
        saturating_increment(&next.telemetry.rejected_order);
        goto rejected;
      }
      if (candidate.server_time_us == previous->server_time_us &&
          (candidate.flags & WORR_REWIND_POSE_DISCONTINUITY_PAUSE) == 0) {
        reason = WORR_REWIND_APPEND_REJECT_PAUSE_INVARIANT;
        saturating_increment(&next.telemetry.rejected_order);
        goto rejected;
      }
      if (candidate.server_time_us - previous->server_time_us >
          history->config.max_interpolation_span_us) {
        candidate.flags |= WORR_REWIND_POSE_DISCONTINUITY_TIME;
        saturating_increment(&next.telemetry.auto_time_gaps);
      }
      if (!entity_ref_equal(candidate.entity, previous->entity)) {
        candidate.flags |= WORR_REWIND_POSE_DISCONTINUITY_GENERATION;
        saturating_increment(&next.telemetry.auto_generation_changes);
      }
      if (mover_changed(&candidate, previous)) {
        candidate.flags |= WORR_REWIND_POSE_DISCONTINUITY_MOVER;
        saturating_increment(&next.telemetry.auto_mover_changes);
      }
      if (collision_identity_changed(&candidate, previous)) {
        candidate.flags |= WORR_REWIND_POSE_DISCONTINUITY_COLLISION;
        saturating_increment(&next.telemetry.auto_collision_changes);
      }
      if (previous->lifecycle == WORR_REWIND_LIFECYCLE_ALIVE &&
          candidate.lifecycle != WORR_REWIND_LIFECYCLE_ALIVE) {
        candidate.flags |= WORR_REWIND_POSE_DISCONTINUITY_DEATH;
        saturating_increment(&next.telemetry.auto_deaths);
      }
      if (previous->lifecycle != WORR_REWIND_LIFECYCLE_ALIVE &&
          candidate.lifecycle == WORR_REWIND_LIFECYCLE_ALIVE) {
        candidate.flags |= WORR_REWIND_POSE_DISCONTINUITY_RESPAWN;
        saturating_increment(&next.telemetry.auto_respawns);
      }
      if (entity_ref_equal(candidate.entity, previous->entity) &&
          !mover_changed(&candidate, previous)) {
        const bool relative =
            (candidate.flags & WORR_REWIND_POSE_HAS_MOVER) != 0;
        const float *candidate_position =
            relative ? candidate.mover_relative_origin : candidate.origin;
        const float *previous_position =
            relative ? previous->mover_relative_origin : previous->origin;
        teleport_limit_squared = (double)history->config.teleport_distance *
                                 (double)history->config.teleport_distance;
        if (distance_squared(candidate_position, previous_position) >
            teleport_limit_squared) {
          candidate.flags |= WORR_REWIND_POSE_DISCONTINUITY_TELEPORT;
          saturating_increment(&next.telemetry.auto_teleports);
        }
      }
    }
  }

  if (next.count < next.capacity) {
    /* A non-full validated history has head == 0. */
    index = next.count;
    ++next.count;
  } else {
    index = next.head;
    next.head = next.head + 1u == next.capacity ? 0 : next.head + 1u;
    saturating_increment(&next.telemetry.overwritten);
  }
  next.slots[index] = candidate;
  saturating_increment(&next.telemetry.appended);
  *history = next;
  *reason_out = reason;
  return true;

rejected:
  *history = next;
  *reason_out = reason;
  return true;
}

static bool query_ranges_valid(worr_rewind_history_v1 *history,
                               const worr_rewind_pose_query_v1 *query,
                               worr_rewind_pose_result_v1 *result) {
  worr_range ranges[4];
  size_t i;
  size_t j;
  if (!make_range(history, sizeof(*history), &ranges[0]) ||
      !history_storage_range(history, &ranges[1]) ||
      !make_range(query, sizeof(*query), &ranges[2]) ||
      !make_range(result, sizeof(*result), &ranges[3])) {
    return false;
  }
  for (i = 0; i < 4; ++i) {
    for (j = i + 1; j < 4; ++j) {
      if (ranges_overlap(ranges[i], ranges[j]))
        return false;
    }
  }
  return true;
}

static float interpolate_linear(float older, float newer, double fraction) {
  const double value =
      (double)older + ((double)newer - (double)older) * fraction;
  return canonical_float((float)value);
}

static float interpolate_angle(float older, float newer, double fraction) {
  double delta = fmod((double)newer - (double)older, 360.0);
  if (delta > 180.0)
    delta -= 360.0;
  else if (delta < -180.0)
    delta += 360.0;
  return canonical_float((float)((double)older + delta * fraction));
}

static void interpolate_vector(float output[3], const float older[3],
                               const float newer[3], double fraction) {
  output[0] = interpolate_linear(older[0], newer[0], fraction);
  output[1] = interpolate_linear(older[1], newer[1], fraction);
  output[2] = interpolate_linear(older[2], newer[2], fraction);
}

static void interpolate_angles(float output[3], const float older[3],
                               const float newer[3], double fraction) {
  output[0] = interpolate_angle(older[0], newer[0], fraction);
  output[1] = interpolate_angle(older[1], newer[1], fraction);
  output[2] = interpolate_angle(older[2], newer[2], fraction);
}

static bool pose_matches_lifecycle(const worr_rewind_pose_v1 *pose,
                                   uint32_t lifecycle) {
  return lifecycle == WORR_REWIND_LIFECYCLE_UNAVAILABLE ||
         pose->lifecycle == lifecycle;
}

static void query_miss(worr_rewind_history_v1 *next,
                       worr_rewind_pose_result_v1 *result, uint32_t reason) {
  result->reason = reason;
  saturating_increment(&next->telemetry.history_misses);
}

bool Worr_RewindHistoryQueryV1(worr_rewind_history_v1 *history,
                               const worr_rewind_pose_query_v1 *query,
                               worr_rewind_pose_result_v1 *result_out) {
  worr_rewind_history_v1 next;
  worr_rewind_pose_result_v1 result;
  const worr_rewind_pose_v1 *exact = NULL;
  const worr_rewind_pose_v1 *older = NULL;
  const worr_rewind_pose_v1 *newer = NULL;
  const worr_rewind_pose_v1 *discrete;
  bool map_found = false;
  bool generation_found = false;
  uint64_t oldest_time = UINT64_MAX;
  uint64_t newest_time = 0;
  uint64_t span;
  uint64_t offset;
  uint32_t fraction_q32;
  uint32_t physical_index;
  uint32_t i;
  double fraction;

  if (!history || !query || !result_out ||
      !range_aligned(query, _Alignof(worr_rewind_pose_query_v1)) ||
      !range_aligned(result_out, _Alignof(worr_rewind_pose_result_v1)) ||
      !Worr_RewindHistoryValidateV1(history) ||
      !query_ranges_valid(history, query, result_out)) {
    return false;
  }
  next = *history;
  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.schema_version = WORR_REWIND_ABI_VERSION;
  result.reason = WORR_REWIND_QUERY_INVALID;
  result.requested_time_us = query->target_time_us;
  saturating_increment(&next.telemetry.queries);

  if (query->struct_size != sizeof(*query) ||
      query->schema_version != WORR_REWIND_ABI_VERSION ||
      !entity_ref_valid(query->entity, false) || query->map_epoch == 0 ||
      query->entity.index != history->entity_index ||
      query->required_lifecycle > WORR_REWIND_LIFECYCLE_DORMANT) {
    query_miss(&next, &result, WORR_REWIND_QUERY_INVALID);
    goto finish;
  }
  if (history->count == 0) {
    query_miss(&next, &result, WORR_REWIND_QUERY_MISS_EMPTY);
    goto finish;
  }

  /*
   * Rewind targets normally sit near the newest capture.  Walk backward so
   * the common exact/interpolation path examines only the requested tail of a
   * full ring.  Reset the newer bracket across map/identity boundaries just
   * as the former oldest-to-newest walk reset its older bracket.  The first
   * exact sample encountered is the newest duplicate pause sample.
   */
  {
    const uint32_t tail = history->capacity - history->head;
    physical_index = history->count < tail
                         ? history->head + history->count
                         : history->count - tail;
  }
  for (i = history->count; i-- > 0;) {
    physical_index =
        physical_index == 0 ? history->capacity - 1u : physical_index - 1u;
    const worr_rewind_pose_v1 *pose = &history->slots[physical_index];
    if (pose->map_epoch != query->map_epoch) {
      newer = NULL;
      continue;
    }
    map_found = true;
    if (!entity_ref_equal(pose->entity, query->entity)) {
      newer = NULL;
      continue;
    }
    generation_found = true;
    if (pose->server_time_us < oldest_time)
      oldest_time = pose->server_time_us;
    if (pose->server_time_us > newest_time)
      newest_time = pose->server_time_us;
    if (pose->server_time_us == query->target_time_us) {
      exact = pose;
      break;
    }
    if (pose->server_time_us > query->target_time_us) {
      newer = pose;
      continue;
    }
    older = pose;
    break;
  }

  if (!map_found) {
    query_miss(&next, &result, WORR_REWIND_QUERY_MISS_MAP);
    goto finish;
  }
  if (!generation_found) {
    query_miss(&next, &result, WORR_REWIND_QUERY_MISS_GENERATION);
    goto finish;
  }
  if (exact) {
    if (!pose_matches_lifecycle(exact, query->required_lifecycle)) {
      query_miss(&next, &result, WORR_REWIND_QUERY_MISS_LIFECYCLE);
      goto finish;
    }
    result.found = 1;
    result.reason = WORR_REWIND_QUERY_EXACT;
    result.discrete_source = WORR_REWIND_DISCRETE_EXACT;
    result.applied_time_us = exact->server_time_us;
    result.pose = *exact;
    saturating_increment(&next.telemetry.exact);
    goto finish;
  }
  if (query->target_time_us < oldest_time) {
    query_miss(&next, &result, WORR_REWIND_QUERY_MISS_TOO_OLD);
    goto finish;
  }
  if (query->target_time_us > newest_time) {
    query_miss(&next, &result, WORR_REWIND_QUERY_MISS_FUTURE);
    goto finish;
  }
  if (!older || !newer || newer->server_time_us <= older->server_time_us) {
    query_miss(&next, &result, WORR_REWIND_QUERY_MISS_GAP);
    goto finish;
  }

  span = newer->server_time_us - older->server_time_us;
  offset = query->target_time_us - older->server_time_us;
  if (!entity_ref_equal(older->entity, newer->entity)) {
    query_miss(&next, &result, WORR_REWIND_QUERY_MISS_GENERATION);
    goto finish;
  }
  if (newer->map_epoch != older->map_epoch ||
      newer->lifecycle != older->lifecycle || mover_changed(older, newer) ||
      collision_identity_changed(older, newer) ||
      (newer->flags & WORR_REWIND_POSE_DISCONTINUITIES_V1) != 0 ||
      span > history->config.max_interpolation_span_us) {
    if (!pose_matches_lifecycle(older, query->required_lifecycle)) {
      query_miss(&next, &result, WORR_REWIND_QUERY_MISS_LIFECYCLE);
      goto finish;
    }
    result.found = 1;
    result.reason = WORR_REWIND_QUERY_DISCONTINUITY_FLOOR;
    result.discrete_source = WORR_REWIND_DISCRETE_DISCONTINUITY_FLOOR;
    result.applied_time_us = older->server_time_us;
    result.pose = *older;
    saturating_increment(&next.telemetry.discontinuity_floors);
    goto finish;
  }
  if (!pose_matches_lifecycle(older, query->required_lifecycle)) {
    query_miss(&next, &result, WORR_REWIND_QUERY_MISS_LIFECYCLE);
    goto finish;
  }

  fraction_q32 = (uint32_t)((offset * UINT64_C(0xffffffff)) / span);
  fraction = (double)fraction_q32 / 4294967295.0;
  discrete = offset <= span - offset ? older : newer; /* Ties use older. */
  result.found = 1;
  result.reason = WORR_REWIND_QUERY_INTERPOLATED;
  result.discrete_source = discrete == older ? WORR_REWIND_DISCRETE_OLDER
                                             : WORR_REWIND_DISCRETE_NEWER;
  result.interpolation_fraction_q32 = fraction_q32;
  result.applied_time_us = query->target_time_us;
  result.pose = *discrete;
  result.pose.server_time_us = query->target_time_us;
  interpolate_vector(result.pose.origin, older->origin, newer->origin,
                     fraction);
  interpolate_angles(result.pose.angles, older->angles, newer->angles,
                     fraction);
  interpolate_vector(result.pose.velocity, older->velocity, newer->velocity,
                     fraction);
  interpolate_vector(result.pose.mover_relative_origin,
                     older->mover_relative_origin, newer->mover_relative_origin,
                     fraction);
  interpolate_angles(result.pose.mover_relative_angles,
                     older->mover_relative_angles, newer->mover_relative_angles,
                     fraction);
  saturating_increment(&next.telemetry.interpolated);

finish:
  *history = next;
  *result_out = result;
  return true;
}

bool Worr_RewindHistoryHashV1(const worr_rewind_history_v1 *history,
                              uint64_t *hash_out) {
  worr_range history_range;
  worr_range storage_range;
  worr_range output_range;
  uint64_t hash;
  uint32_t i;
  if (!history || !hash_out || !range_aligned(hash_out, _Alignof(uint64_t)) ||
      !Worr_RewindHistoryValidateV1(history) ||
      !make_range(history, sizeof(*history), &history_range) ||
      !history_storage_range(history, &storage_range) ||
      !make_range(hash_out, sizeof(*hash_out), &output_range) ||
      ranges_overlap(history_range, output_range) ||
      ranges_overlap(storage_range, output_range)) {
    return false;
  }
  hash = WORR_FNV_OFFSET;
  hash = hash_u32(hash, UINT32_C(0x54534948)); /* HIST */
  hash = hash_u32(hash, history->entity_index);
  hash = hash_u64(hash, history->config.max_interpolation_span_us);
  hash = hash_float(hash, history->config.teleport_distance);
  hash = hash_u32(hash, history->count);
  for (i = 0; i < history->count; ++i)
    hash = hash_u64(hash, pose_hash_unchecked(history_at(history, i)));
  *hash_out = hash;
  return true;
}

static bool
policy_decision_valid(const worr_rewind_policy_decision_v1 *decision,
                      bool require_accepted) {
  const uint32_t known_flags = WORR_REWIND_DECISION_ACCEPTED |
                               WORR_REWIND_DECISION_CLAMPED |
                               WORR_REWIND_DECISION_LEGACY_FALLBACK;
  const bool accepted =
      decision && (decision->flags & WORR_REWIND_DECISION_ACCEPTED) != 0;
  const bool clamped =
      decision && (decision->flags & WORR_REWIND_DECISION_CLAMPED) != 0;
  const bool legacy =
      decision && (decision->flags & WORR_REWIND_DECISION_LEGACY_FALLBACK) != 0;

  if (!decision ||
      !range_aligned(decision, _Alignof(worr_rewind_policy_decision_v1)) ||
      decision->struct_size != sizeof(*decision) ||
      decision->schema_version != WORR_REWIND_ABI_VERSION ||
      (decision->flags & ~known_flags) != 0 || decision->reserved0 != 0 ||
      decision->reason > WORR_REWIND_POLICY_REJECT_MAPPING_PROOF ||
      (require_accepted && !accepted)) {
    return false;
  }
  if (!accepted) {
    return !clamped && !require_accepted &&
           decision->reason >= WORR_REWIND_POLICY_REJECT_INVALID_COMMAND;
  }
  if (!Worr_CommandIdValidV1(decision->command_id, false) ||
      !snapshot_id_valid(decision->snapshot_id) ||
      !snapshot_id_valid(decision->source_snapshot_id) ||
      decision->source_snapshot_id.epoch != decision->snapshot_id.epoch ||
      decision->source_snapshot_id.sequence > decision->snapshot_id.sequence ||
      decision->watermark_provenance <
          WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND ||
      decision->watermark_provenance >
          WORR_COMMAND_RENDER_PROVENANCE_SERVER_SYNTHESIZED ||
      legacy != (decision->mapping_error_bound_us != 0) ||
      legacy != (decision->watermark_provenance ==
                 WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED)) {
    return false;
  }
  if ((legacy && decision->mapped_time_us > decision->requested_time_us) ||
      (!legacy && decision->mapped_time_us != decision->requested_time_us)) {
    return false;
  }
  switch (decision->reason) {
  case WORR_REWIND_POLICY_EXACT:
    return !clamped && !legacy &&
           decision->applied_time_us == decision->mapped_time_us;
  case WORR_REWIND_POLICY_LEGACY_BOUNDED:
    return !clamped && legacy &&
           decision->applied_time_us == decision->mapped_time_us;
  case WORR_REWIND_POLICY_CLAMPED_TARGET_WINDOW:
    return clamped && decision->applied_time_us >= decision->mapped_time_us;
  case WORR_REWIND_POLICY_CLAMPED_FUTURE:
    return clamped && decision->applied_time_us <= decision->mapped_time_us;
  default:
    return false;
  }
}

bool Worr_RewindPolicyDecisionValidateV1(
    const worr_rewind_policy_decision_v1 *decision, bool require_accepted) {
  return policy_decision_valid(decision, require_accepted);
}

static bool scene_storage_range(const worr_rewind_scene_v1 *scene,
                                worr_range *range_out) {
  size_t bytes;
  return scene && scene->slots && scene->capacity != 0 &&
         checked_mul_size(scene->capacity, sizeof(*scene->slots), &bytes) &&
         make_range(scene->slots, bytes, range_out);
}

static int entity_ref_compare(worr_event_entity_ref_v1 a,
                              worr_event_entity_ref_v1 b) {
  if (a.index != b.index)
    return a.index < b.index ? -1 : 1;
  if (a.generation != b.generation)
    return a.generation < b.generation ? -1 : 1;
  return 0;
}

static bool
scene_candidate_timing_valid(
    const worr_rewind_scene_v1 *scene,
    const worr_rewind_scene_candidate_v1 *candidate) {
  switch (candidate->query_reason) {
  case WORR_REWIND_QUERY_EXACT:
    return candidate->discrete_source == WORR_REWIND_DISCRETE_EXACT &&
           candidate->interpolation_fraction_q32 == 0 &&
           candidate->pose.server_time_us == scene->decision.applied_time_us;
  case WORR_REWIND_QUERY_INTERPOLATED:
    return (candidate->discrete_source == WORR_REWIND_DISCRETE_OLDER ||
            candidate->discrete_source == WORR_REWIND_DISCRETE_NEWER) &&
           candidate->interpolation_fraction_q32 != 0 &&
           candidate->interpolation_fraction_q32 != UINT32_MAX &&
           candidate->pose.server_time_us == scene->decision.applied_time_us;
  case WORR_REWIND_QUERY_DISCONTINUITY_FLOOR:
    return candidate->discrete_source ==
               WORR_REWIND_DISCRETE_DISCONTINUITY_FLOOR &&
           candidate->interpolation_fraction_q32 == 0 &&
           candidate->pose.server_time_us <= scene->decision.applied_time_us;
  default:
    return false;
  }
}

static bool
scene_candidate_valid(const worr_rewind_scene_v1 *scene,
                      const worr_rewind_scene_candidate_v1 *candidate) {
  uint64_t pose_hash;
  return scene && candidate && candidate->struct_size == sizeof(*candidate) &&
         candidate->schema_version == WORR_REWIND_ABI_VERSION &&
         candidate->reserved0 == 0 &&
         (candidate->pose.flags & WORR_REWIND_POSE_LINKED) != 0 &&
         candidate->pose.map_epoch == scene->map_epoch &&
         Worr_RewindPoseHashV1(&candidate->pose, &pose_hash) &&
         pose_hash == candidate->pose_hash &&
         scene_candidate_timing_valid(scene, candidate);
}

static const worr_rewind_scene_candidate_v1 *
scene_find_index(const worr_rewind_scene_v1 *scene, uint32_t entity_index) {
  uint32_t low = 0;
  uint32_t high = scene->count;
  while (low < high) {
    const uint32_t middle = low + (high - low) / 2u;
    const uint32_t candidate_index = scene->slots[middle].pose.entity.index;
    if (candidate_index < entity_index)
      low = middle + 1u;
    else
      high = middle;
  }
  if (low < scene->count &&
      scene->slots[low].pose.entity.index == entity_index) {
    return &scene->slots[low];
  }
  return NULL;
}

static bool scene_movers_valid(const worr_rewind_scene_v1 *scene) {
  uint32_t i;
  for (i = 0; i < scene->count; ++i) {
    const worr_rewind_scene_candidate_v1 *candidate = &scene->slots[i];
    const worr_rewind_scene_candidate_v1 *cursor = candidate;
    uint32_t depth = 0;
    while ((cursor->pose.flags & WORR_REWIND_POSE_HAS_MOVER) != 0) {
      const worr_rewind_scene_candidate_v1 *mover =
          scene_find_index(scene, cursor->pose.mover.index);
      if (!mover || !entity_ref_equal(mover->pose.entity, cursor->pose.mover) ||
          (mover->pose.flags & WORR_REWIND_POSE_LINKED) == 0) {
        return false;
      }
      cursor = mover;
      ++depth;
      if (depth > WORR_REWIND_MAX_MOVER_DEPTH)
        return false;
    }
  }
  return true;
}

static uint64_t scene_hash_unchecked(const worr_rewind_scene_v1 *scene) {
  const worr_rewind_policy_decision_v1 *decision = &scene->decision;
  uint64_t hash = WORR_FNV_OFFSET;
  uint32_t i;
  hash = hash_u32(hash, UINT32_C(0x4e435352)); /* RSCN */
  hash = hash_u32(hash, WORR_REWIND_ABI_VERSION);
  hash = hash_u32(hash, scene->map_epoch);
  hash = hash_u32(hash, decision->flags);
  hash = hash_u32(hash, decision->reason);
  hash = hash_u32(hash, decision->command_id.epoch);
  hash = hash_u32(hash, decision->command_id.sequence);
  hash = hash_u32(hash, decision->snapshot_id.epoch);
  hash = hash_u32(hash, decision->snapshot_id.sequence);
  hash = hash_u32(hash, decision->source_snapshot_id.epoch);
  hash = hash_u32(hash, decision->source_snapshot_id.sequence);
  hash = hash_u32(hash, decision->watermark_provenance);
  hash = hash_u64(hash, decision->requested_time_us);
  hash = hash_u64(hash, decision->mapped_time_us);
  hash = hash_u64(hash, decision->applied_time_us);
  hash = hash_u64(hash, decision->mapping_error_bound_us);
  hash = hash_u32(hash, scene->count);
  for (i = 0; i < scene->count; ++i) {
    const worr_rewind_scene_candidate_v1 *candidate = &scene->slots[i];
    hash = hash_u32(hash, candidate->query_reason);
    hash = hash_u32(hash, candidate->discrete_source);
    hash = hash_u32(hash, candidate->interpolation_fraction_q32);
    hash = hash_u64(hash, candidate->pose_hash);
  }
  return hash;
}

static bool scene_envelope_valid(const worr_rewind_scene_v1 *scene) {
  worr_range envelope_range;
  worr_range storage_range;
  if (!scene || !range_aligned(scene, _Alignof(worr_rewind_scene_v1)) ||
      scene->struct_size != sizeof(*scene) ||
      scene->schema_version != WORR_REWIND_ABI_VERSION ||
      (scene->flags & ~(uint32_t)WORR_REWIND_SCENE_SEALED) != 0 ||
      scene->reserved0 != 0 || scene->reserved1 != 0 || scene->capacity == 0 ||
      scene->count > scene->capacity || scene->map_epoch == 0 ||
      !range_aligned(scene->slots, _Alignof(worr_rewind_scene_candidate_v1)) ||
      !policy_decision_valid(&scene->decision, true) ||
      scene->decision.snapshot_id.epoch != scene->map_epoch ||
      !make_range(scene, sizeof(*scene), &envelope_range) ||
      !scene_storage_range(scene, &storage_range) ||
      ranges_overlap(envelope_range, storage_range)) {
    return false;
  }
  return true;
}

static bool scene_core_valid(const worr_rewind_scene_v1 *scene,
                             bool verify_seal) {
  uint32_t i;
  if (!scene_envelope_valid(scene))
    return false;
  for (i = 0; i < scene->count; ++i) {
    if (!scene_candidate_valid(scene, &scene->slots[i]) ||
        (i != 0 && scene->slots[i - 1].pose.entity.index >=
                       scene->slots[i].pose.entity.index)) {
      return false;
    }
  }
  if ((scene->flags & WORR_REWIND_SCENE_SEALED) == 0)
    return scene->scene_hash == 0;
  if (!scene_movers_valid(scene))
    return false;
  return !verify_seal || scene->scene_hash == scene_hash_unchecked(scene);
}

bool Worr_RewindSceneInitV1(worr_rewind_scene_v1 *scene,
                            worr_rewind_scene_candidate_v1 *storage,
                            uint32_t capacity,
                            const worr_rewind_policy_decision_v1 *decision) {
  worr_range ranges[3];
  worr_rewind_scene_v1 output;
  size_t storage_bytes;
  size_t i;
  size_t j;
  if (!scene || !storage || !decision || capacity == 0 ||
      !range_aligned(scene, _Alignof(worr_rewind_scene_v1)) ||
      !range_aligned(storage, _Alignof(worr_rewind_scene_candidate_v1)) ||
      !range_aligned(decision, _Alignof(worr_rewind_policy_decision_v1)) ||
      !policy_decision_valid(decision, true) ||
      !checked_mul_size(capacity, sizeof(*storage), &storage_bytes) ||
      !make_range(scene, sizeof(*scene), &ranges[0]) ||
      !make_range(storage, storage_bytes, &ranges[1]) ||
      !make_range(decision, sizeof(*decision), &ranges[2])) {
    return false;
  }
  for (i = 0; i < 3; ++i) {
    for (j = i + 1; j < 3; ++j) {
      if (ranges_overlap(ranges[i], ranges[j]))
        return false;
    }
  }
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_REWIND_ABI_VERSION;
  output.slots = storage;
  output.capacity = capacity;
  output.map_epoch = decision->snapshot_id.epoch;
  output.decision = *decision;
  memset(storage, 0, storage_bytes);
  *scene = output;
  return true;
}

bool Worr_RewindSceneValidateV1(const worr_rewind_scene_v1 *scene) {
  return scene_core_valid(scene, true);
}

static bool scene_add_ranges_valid(worr_rewind_scene_v1 *scene,
                                   const worr_rewind_pose_result_v1 *result) {
  worr_range ranges[3];
  size_t i;
  size_t j;
  if (!make_range(scene, sizeof(*scene), &ranges[0]) ||
      !scene_storage_range(scene, &ranges[1]) ||
      !make_range(result, sizeof(*result), &ranges[2])) {
    return false;
  }
  for (i = 0; i < 3; ++i) {
    for (j = i + 1; j < 3; ++j) {
      if (ranges_overlap(ranges[i], ranges[j]))
        return false;
    }
  }
  return true;
}

static bool scene_add_result(worr_rewind_scene_v1 *scene,
                             const worr_rewind_pose_result_v1 *result,
                             bool validate_existing) {
  worr_rewind_scene_candidate_v1 candidate;
  uint64_t pose_hash;
  if (!scene || !result ||
      !range_aligned(result, _Alignof(worr_rewind_pose_result_v1)) ||
      (validate_existing ? !scene_core_valid(scene, true)
                         : (!scene_envelope_valid(scene) ||
                            scene->scene_hash != 0)) ||
      (scene->flags & WORR_REWIND_SCENE_SEALED) != 0 ||
      scene->count == scene->capacity ||
      !scene_add_ranges_valid(scene, result) ||
      result->struct_size != sizeof(*result) ||
      result->schema_version != WORR_REWIND_ABI_VERSION || result->found != 1 ||
      result->requested_time_us != scene->decision.applied_time_us ||
      result->applied_time_us != result->pose.server_time_us ||
      (result->pose.flags & WORR_REWIND_POSE_LINKED) == 0 ||
      result->pose.map_epoch != scene->map_epoch ||
      !Worr_RewindPoseHashV1(&result->pose, &pose_hash)) {
    return false;
  }
  memset(&candidate, 0, sizeof(candidate));
  candidate.struct_size = sizeof(candidate);
  candidate.schema_version = WORR_REWIND_ABI_VERSION;
  candidate.query_reason = (uint16_t)result->reason;
  candidate.discrete_source = (uint16_t)result->discrete_source;
  candidate.interpolation_fraction_q32 = result->interpolation_fraction_q32;
  candidate.pose = result->pose;
  candidate.pose_hash = pose_hash;
  /*
   * The result pose was just validated and hashed above, and every envelope
   * field on this local candidate is assigned here.  Rehashing it through
   * scene_candidate_valid() duplicated the production scene-add boundary.
   * Existing caller-owned slots still receive the full validation in
   * scene_core_valid().
   */
  if (!scene_candidate_timing_valid(scene, &candidate))
    return false;
  if (scene->count != 0 && scene->slots[scene->count - 1u].pose.entity.index >=
                               candidate.pose.entity.index) {
    return false;
  }
  scene->slots[scene->count] = candidate;
  ++scene->count;
  return true;
}

bool Worr_RewindSceneAddResultV1(worr_rewind_scene_v1 *scene,
                                 const worr_rewind_pose_result_v1 *result) {
  return scene_add_result(scene, result, true);
}

bool Worr_RewindSceneAddOwnedResultV1(
    worr_rewind_scene_v1 *scene,
    const worr_rewind_pose_result_v1 *result) {
  return scene_add_result(scene, result, false);
}

bool Worr_RewindSceneSealV1(worr_rewind_scene_v1 *scene) {
  worr_rewind_scene_v1 next;
  if (!scene || !scene_core_valid(scene, true) ||
      (scene->flags & WORR_REWIND_SCENE_SEALED) != 0 ||
      !scene_movers_valid(scene)) {
    return false;
  }
  next = *scene;
  next.scene_hash = scene_hash_unchecked(scene);
  next.flags |= WORR_REWIND_SCENE_SEALED;
  *scene = next;
  return true;
}

static bool ignore_storage_range(const worr_rewind_ignore_set_v1 *ignore_set,
                                 worr_range *range_out) {
  size_t bytes;
  return ignore_set && ignore_set->slots && ignore_set->capacity != 0 &&
         checked_mul_size(ignore_set->capacity, sizeof(*ignore_set->slots),
                          &bytes) &&
         make_range(ignore_set->slots, bytes, range_out);
}

bool Worr_RewindIgnoreSetInitV1(worr_rewind_ignore_set_v1 *ignore_set,
                                worr_event_entity_ref_v1 *storage,
                                uint32_t capacity) {
  worr_range envelope_range;
  worr_range storage_range;
  worr_rewind_ignore_set_v1 output;
  size_t storage_bytes;
  if (!ignore_set || !storage || capacity == 0 ||
      !range_aligned(ignore_set, _Alignof(worr_rewind_ignore_set_v1)) ||
      !range_aligned(storage, _Alignof(worr_event_entity_ref_v1)) ||
      !checked_mul_size(capacity, sizeof(*storage), &storage_bytes) ||
      !make_range(ignore_set, sizeof(*ignore_set), &envelope_range) ||
      !make_range(storage, storage_bytes, &storage_range) ||
      ranges_overlap(envelope_range, storage_range)) {
    return false;
  }
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_REWIND_ABI_VERSION;
  output.slots = storage;
  output.capacity = capacity;
  memset(storage, 0, storage_bytes);
  *ignore_set = output;
  return true;
}

bool Worr_RewindIgnoreSetValidateV1(
    const worr_rewind_ignore_set_v1 *ignore_set) {
  worr_range envelope_range;
  worr_range storage_range;
  uint32_t i;
  if (!ignore_set ||
      !range_aligned(ignore_set, _Alignof(worr_rewind_ignore_set_v1)) ||
      ignore_set->struct_size != sizeof(*ignore_set) ||
      ignore_set->schema_version != WORR_REWIND_ABI_VERSION ||
      ignore_set->capacity == 0 || ignore_set->count > ignore_set->capacity ||
      !range_aligned(ignore_set->slots, _Alignof(worr_event_entity_ref_v1)) ||
      !make_range(ignore_set, sizeof(*ignore_set), &envelope_range) ||
      !ignore_storage_range(ignore_set, &storage_range) ||
      ranges_overlap(envelope_range, storage_range)) {
    return false;
  }
  for (i = 0; i < ignore_set->count; ++i) {
    if (!entity_ref_valid(ignore_set->slots[i], false) ||
        (i != 0 && entity_ref_compare(ignore_set->slots[i - 1],
                                      ignore_set->slots[i]) >= 0)) {
      return false;
    }
  }
  return true;
}

bool Worr_RewindIgnoreSetAddV1(worr_rewind_ignore_set_v1 *ignore_set,
                               worr_event_entity_ref_v1 entity) {
  uint32_t insertion = 0;
  if (!Worr_RewindIgnoreSetValidateV1(ignore_set) ||
      !entity_ref_valid(entity, false)) {
    return false;
  }
  while (insertion < ignore_set->count &&
         entity_ref_compare(ignore_set->slots[insertion], entity) < 0) {
    ++insertion;
  }
  if (insertion < ignore_set->count &&
      entity_ref_equal(ignore_set->slots[insertion], entity)) {
    return true;
  }
  if (ignore_set->count == ignore_set->capacity)
    return false;
  if (insertion != ignore_set->count) {
    memmove(&ignore_set->slots[insertion + 1u], &ignore_set->slots[insertion],
            (size_t)(ignore_set->count - insertion) *
                sizeof(*ignore_set->slots));
  }
  ignore_set->slots[insertion] = entity;
  ++ignore_set->count;
  return true;
}

bool Worr_RewindIgnoreSetContainsV1(const worr_rewind_ignore_set_v1 *ignore_set,
                                    worr_event_entity_ref_v1 entity,
                                    bool *contains_out) {
  worr_range ranges[3];
  bool found = false;
  uint32_t low = 0;
  uint32_t high;
  size_t i;
  size_t j;
  if (!ignore_set || !contains_out ||
      !range_aligned(contains_out, _Alignof(bool)) ||
      !Worr_RewindIgnoreSetValidateV1(ignore_set) ||
      !entity_ref_valid(entity, false) ||
      !make_range(ignore_set, sizeof(*ignore_set), &ranges[0]) ||
      !ignore_storage_range(ignore_set, &ranges[1]) ||
      !make_range(contains_out, sizeof(*contains_out), &ranges[2])) {
    return false;
  }
  for (i = 0; i < 3; ++i) {
    for (j = i + 1; j < 3; ++j) {
      if (ranges_overlap(ranges[i], ranges[j]))
        return false;
    }
  }
  high = ignore_set->count;
  while (low < high) {
    const uint32_t middle = low + (high - low) / 2u;
    const int comparison =
        entity_ref_compare(ignore_set->slots[middle], entity);
    if (comparison < 0)
      low = middle + 1u;
    else
      high = middle;
  }
  if (low < ignore_set->count)
    found = entity_ref_equal(ignore_set->slots[low], entity);
  *contains_out = found;
  return true;
}

bool Worr_RewindTraceViewV1(const worr_rewind_scene_v1 *scene,
                            const worr_rewind_ignore_set_v1 *ignore_set,
                            worr_rewind_trace_view_v1 *view_out) {
  worr_range ranges[5];
  worr_rewind_trace_view_v1 output;
  size_t i;
  size_t j;
  if (!scene || !ignore_set || !view_out ||
      !range_aligned(view_out, _Alignof(worr_rewind_trace_view_v1)) ||
      !Worr_RewindSceneValidateV1(scene) ||
      (scene->flags & WORR_REWIND_SCENE_SEALED) == 0 ||
      !Worr_RewindIgnoreSetValidateV1(ignore_set) ||
      !make_range(scene, sizeof(*scene), &ranges[0]) ||
      !scene_storage_range(scene, &ranges[1]) ||
      !make_range(ignore_set, sizeof(*ignore_set), &ranges[2]) ||
      !ignore_storage_range(ignore_set, &ranges[3]) ||
      !make_range(view_out, sizeof(*view_out), &ranges[4])) {
    return false;
  }
  for (i = 0; i < 5; ++i) {
    for (j = i + 1; j < 5; ++j) {
      if (ranges_overlap(ranges[i], ranges[j]))
        return false;
    }
  }
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_REWIND_ABI_VERSION;
  output.candidates = scene->slots;
  output.ignored_entities = ignore_set->slots;
  output.candidate_count = scene->count;
  output.ignore_count = ignore_set->count;
  output.map_epoch = scene->map_epoch;
  output.target_time_us = scene->decision.applied_time_us;
  output.scene_hash = scene->scene_hash;
  output.command_id = scene->decision.command_id;
  output.snapshot_id = scene->decision.snapshot_id;
  *view_out = output;
  return true;
}
