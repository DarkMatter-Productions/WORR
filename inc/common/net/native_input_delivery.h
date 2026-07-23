/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/adaptive_input.h"
#include "common/net/native_command_shadow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure native-input delivery planning for FR-10-T16.
 *
 * This core owns no transport, clock, allocation, payload, or simulation
 * authority.  A caller supplies one exact contiguous unacknowledged command
 * range, monotonic server feedback, and the already evaluated adaptive-input
 * decision.  The result is a bounded list of exact command identities for a
 * future native batch.  Encoding and committing a send remain explicit caller
 * operations, so a failed transport handoff cannot mutate this planner.
 */
#define WORR_NATIVE_INPUT_DELIVERY_VERSION UINT32_C(1)
#define WORR_NATIVE_INPUT_DELIVERY_MAX_CANDIDATES UINT32_C(64)
#define WORR_NATIVE_INPUT_DELIVERY_MAX_SELECTIONS UINT32_C(8)
#define WORR_NATIVE_INPUT_DELIVERY_MAX_DATAGRAM_BYTES UINT32_C(1200)
#define WORR_NATIVE_INPUT_DELIVERY_MAX_TRANSMISSIONS UINT32_C(32)

enum worr_native_input_delivery_result_v1_e {
    WORR_NATIVE_INPUT_DELIVERY_PLANNED = 0,
    WORR_NATIVE_INPUT_DELIVERY_NOTHING_DUE = 1,
    WORR_NATIVE_INPUT_DELIVERY_INVALID_ARGUMENT = 2,
    WORR_NATIVE_INPUT_DELIVERY_INVALID_CONFIG = 3,
    WORR_NATIVE_INPUT_DELIVERY_INVALID_STATE = 4,
    WORR_NATIVE_INPUT_DELIVERY_INVALID_FEEDBACK = 5,
    WORR_NATIVE_INPUT_DELIVERY_FEEDBACK_ROLLBACK = 6,
    WORR_NATIVE_INPUT_DELIVERY_INVALID_ADAPTIVE_OUTPUT = 7,
    WORR_NATIVE_INPUT_DELIVERY_INVALID_CANDIDATES = 8,
    WORR_NATIVE_INPUT_DELIVERY_OUTPUT_OVERLAP = 9,
};

enum worr_native_input_delivery_plan_flags_v1_e {
    WORR_NATIVE_INPUT_DELIVERY_PLAN_VALID = UINT32_C(1) << 0,
    WORR_NATIVE_INPUT_DELIVERY_PLAN_HAS_FRESH = UINT32_C(1) << 1,
    WORR_NATIVE_INPUT_DELIVERY_PLAN_HAS_REDUNDANCY = UINT32_C(1) << 2,
    WORR_NATIVE_INPUT_DELIVERY_PLAN_FRONTIER_INCLUDED = UINT32_C(1) << 3,
    WORR_NATIVE_INPUT_DELIVERY_PLAN_NEWEST_FRESH_INCLUDED = UINT32_C(1) << 4,
    WORR_NATIVE_INPUT_DELIVERY_PLAN_BATCH_CAPPED = UINT32_C(1) << 5,
    WORR_NATIVE_INPUT_DELIVERY_PLAN_WIRE_BUDGET_CAPPED = UINT32_C(1) << 6,
    WORR_NATIVE_INPUT_DELIVERY_PLAN_REDUNDANCY_CAPPED = UINT32_C(1) << 7,
    WORR_NATIVE_INPUT_DELIVERY_PLAN_RETRY_NOT_DUE = UINT32_C(1) << 8,
    WORR_NATIVE_INPUT_DELIVERY_PLAN_RETRY_EXHAUSTED = UINT32_C(1) << 9,
};

enum worr_native_input_delivery_selection_flags_v1_e {
    WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRESH = UINT32_C(1) << 0,
    WORR_NATIVE_INPUT_DELIVERY_SELECTION_REDUNDANT = UINT32_C(1) << 1,
    WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRONTIER = UINT32_C(1) << 2,
    WORR_NATIVE_INPUT_DELIVERY_SELECTION_NEWEST_FRESH = UINT32_C(1) << 3,
};

typedef struct worr_native_input_delivery_config_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t maximum_batch_commands;
    uint32_t maximum_redundant_commands;
    /* Bytes available to this native application payload, not necessarily the
     * whole packet. A compatibility carrier must subtract its legacy prefix
     * before planning. */
    uint32_t datagram_budget_bytes;
    uint32_t batch_overhead_bytes;
    uint32_t per_command_overhead_bytes;
    uint32_t retry_interval_ms;
    uint32_t maximum_transmissions_per_command;
    uint32_t reserved[3];
} worr_native_input_delivery_config_v1;

/* Exact contiguous feedback for the current command epoch. */
typedef struct worr_native_input_delivery_feedback_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_command_cursor_v1 received_cursor;
    worr_command_cursor_v1 consumed_cursor;
    uint64_t observed_at_ms;
    uint32_t reserved[2];
} worr_native_input_delivery_feedback_v1;

/*
 * Caller-owned send history for one retained command.  transmit_count counts
 * only successful transport handoffs.  A fresh command therefore has count
 * zero and last_transmit_time_ms zero.
 */
typedef struct worr_native_input_delivery_candidate_v1_s {
    worr_command_id_v1 command_id;
    uint64_t sample_time_us;
    uint64_t last_transmit_time_ms;
    uint32_t transmit_count;
    uint32_t reserved0;
} worr_native_input_delivery_candidate_v1;

typedef struct worr_native_input_delivery_selection_v1_s {
    worr_command_id_v1 command_id;
    uint64_t sample_time_us;
    uint32_t candidate_index;
    uint32_t attempt_ordinal;
    uint32_t flags;
    uint32_t reserved0;
} worr_native_input_delivery_selection_v1;

typedef struct worr_native_input_delivery_plan_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t result;
    uint32_t flags;
    uint64_t decision_serial;
    uint64_t planned_at_ms;
    worr_command_cursor_v1 received_cursor;
    worr_command_cursor_v1 consumed_cursor;
    uint32_t selection_count;
    uint32_t fresh_count;
    uint32_t redundant_count;
    uint32_t deferred_count;
    uint32_t retry_exhausted_count;
    uint32_t wire_bytes;
    uint32_t adaptive_redundancy_limit;
    uint32_t reserved0;
    worr_native_input_delivery_selection_v1
        selections[WORR_NATIVE_INPUT_DELIVERY_MAX_SELECTIONS];
} worr_native_input_delivery_plan_v1;

/* All counters saturate at UINT64_MAX. */
typedef struct worr_native_input_delivery_telemetry_v1_s {
    uint64_t plan_attempts;
    uint64_t plans;
    uint64_t nothing_due;
    uint64_t feedback_advances;
    uint64_t selected_fresh;
    uint64_t selected_redundant;
    uint64_t deferred;
    uint64_t retry_exhausted;
    uint64_t invalid_arguments;
    uint64_t invalid_configs;
    uint64_t invalid_feedback;
    uint64_t feedback_rollbacks;
    uint64_t invalid_adaptive_outputs;
    uint64_t invalid_candidates;
    uint64_t output_overlaps;
} worr_native_input_delivery_telemetry_v1;

typedef struct worr_native_input_delivery_state_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t command_epoch;
    uint32_t initialized;
    uint32_t last_received_sequence;
    uint32_t last_consumed_sequence;
    uint64_t last_feedback_time_ms;
    uint64_t decision_serial;
    uint32_t reserved[2];
    worr_native_input_delivery_telemetry_v1 telemetry;
} worr_native_input_delivery_state_v1;

void Worr_NativeInputDeliveryDefaultConfigV1(
    worr_native_input_delivery_config_v1 *config_out);

/* Invalid initialization leaves the destination byte-identical. */
bool Worr_NativeInputDeliveryResetV1(
    worr_native_input_delivery_state_v1 *state_out,
    uint32_t command_epoch);

bool Worr_NativeInputDeliveryValidateV1(
    const worr_native_input_delivery_state_v1 *state);

/*
 * The candidate range must start at received_cursor + 1 and remain contiguous
 * in the reset-local epoch. PLANNED and NOTHING_DUE write a valid plan; every
 * validation/rejection result leaves plan_out untouched. Valid rejection
 * counters may still advance in state.
 */
uint32_t Worr_NativeInputDeliveryPlanV1(
    worr_native_input_delivery_state_v1 *state,
    const worr_native_input_delivery_config_v1 *config,
    const worr_native_input_delivery_feedback_v1 *feedback,
    const worr_adaptive_input_output_v1 *adaptive_output,
    const worr_native_input_delivery_candidate_v1 *candidates,
    size_t candidate_count,
    uint64_t now_ms,
    worr_native_input_delivery_plan_v1 *plan_out);

#ifdef __cplusplus
}
#endif
