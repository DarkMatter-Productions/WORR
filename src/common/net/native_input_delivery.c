/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_input_delivery.h"

#include "common/net/native_carrier.h"
#include "shared/native_envelope.h"

#include <string.h>

_Static_assert(WORR_NATIVE_INPUT_DELIVERY_MAX_SELECTIONS <=
                   WORR_NATIVE_CARRIER_MAX_ENTRIES,
               "native input selections must fit the carrier entry bound");

#define NATIVE_INPUT_DELIVERY_ADAPTIVE_FLAGS                         \
    ((uint32_t)(WORR_ADAPTIVE_INPUT_OUTPUT_VALID |                   \
                WORR_ADAPTIVE_INPUT_OUTPUT_WINDOW_EVALUATED |       \
                WORR_ADAPTIVE_INPUT_OUTPUT_PROTECTIVE |             \
                WORR_ADAPTIVE_INPUT_OUTPUT_RECOVERY_HELD |          \
                WORR_ADAPTIVE_INPUT_OUTPUT_RATE_CAPPED |            \
                WORR_ADAPTIVE_INPUT_OUTPUT_REDUNDANCY_AVAILABLE))

#define NATIVE_INPUT_DELIVERY_ADAPTIVE_REASONS                       \
    ((uint32_t)((UINT32_C(1) << 19) - UINT32_C(1)))

static uint32_t min_u32(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

static void saturating_increment(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static void saturating_add(uint64_t *value, uint64_t amount)
{
    if (amount > UINT64_MAX - *value)
        *value = UINT64_MAX;
    else
        *value += amount;
}

static bool regions_overlap(const void *left, size_t left_bytes,
                            const void *right, size_t right_bytes)
{
    const uintptr_t left_begin = (uintptr_t)left;
    const uintptr_t right_begin = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left_bytes == 0 || right_bytes == 0)
        return false;
    if (left_begin > UINTPTR_MAX - left_bytes ||
        right_begin > UINTPTR_MAX - right_bytes)
        return true;
    left_end = left_begin + left_bytes;
    right_end = right_begin + right_bytes;
    return left_begin < right_end && right_begin < left_end;
}

static bool config_valid(
    const worr_native_input_delivery_config_v1 *config)
{
    uint32_t command_bytes;

    if (!config || config->struct_size != sizeof(*config) ||
        config->schema_version != WORR_NATIVE_INPUT_DELIVERY_VERSION ||
        config->maximum_batch_commands < 2 ||
        config->maximum_batch_commands >
            WORR_NATIVE_INPUT_DELIVERY_MAX_SELECTIONS ||
        config->maximum_redundant_commands >=
            config->maximum_batch_commands ||
        config->datagram_budget_bytes == 0 ||
        config->datagram_budget_bytes >
            WORR_NATIVE_INPUT_DELIVERY_MAX_DATAGRAM_BYTES ||
        config->batch_overhead_bytes >= config->datagram_budget_bytes ||
        config->per_command_overhead_bytes >=
            config->datagram_budget_bytes ||
        config->retry_interval_ms == 0 ||
        config->retry_interval_ms > 60000 ||
        config->maximum_transmissions_per_command == 0 ||
        config->maximum_transmissions_per_command >
            WORR_NATIVE_INPUT_DELIVERY_MAX_TRANSMISSIONS ||
        config->reserved[0] != 0 || config->reserved[1] != 0 ||
        config->reserved[2] != 0) {
        return false;
    }

    command_bytes = WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES +
                    config->per_command_overhead_bytes;
    if (command_bytes < WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES)
        return false;
    if (command_bytes >
        (config->datagram_budget_bytes - config->batch_overhead_bytes) / 2u) {
        return false;
    }
    if (config->maximum_redundant_commands != 0 &&
        config->maximum_transmissions_per_command < 2) {
        return false;
    }
    return true;
}

static bool adaptive_output_valid(
    const worr_adaptive_input_output_v1 *output, uint64_t now_ms)
{
    return output && output->struct_size == sizeof(*output) &&
           output->schema_version == WORR_ADAPTIVE_INPUT_VERSION &&
           output->result <= WORR_ADAPTIVE_INPUT_CLOCK_RESET &&
           (output->reason_mask &
            ~NATIVE_INPUT_DELIVERY_ADAPTIVE_REASONS) == 0 &&
           (output->flags & ~NATIVE_INPUT_DELIVERY_ADAPTIVE_FLAGS) == 0 &&
           (output->flags & WORR_ADAPTIVE_INPUT_OUTPUT_VALID) != 0 &&
           output->evaluated_at_ms <= now_ms &&
           output->packets_per_second <= 1000 &&
           output->send_interval_ms <= 1000 &&
           output->window_loss_basis_points <= 10000 &&
           output->smoothed_loss_basis_points <= 10000 &&
           output->smoothed_rtt_ms <= 60000 &&
           output->smoothed_jitter_ms <= 60000 &&
           output->queued_commands <= 1048576 &&
           output->unacknowledged_packets <= 1048576 &&
           output->rate_bytes_per_second <= 1073741824 &&
           output->redundancy_frames <= 32 &&
           (output->redundancy_frames == 0 ||
            (output->flags &
             WORR_ADAPTIVE_INPUT_OUTPUT_REDUNDANCY_AVAILABLE) != 0) &&
           output->reserved0 == 0;
}

static bool feedback_valid(
    const worr_native_input_delivery_state_v1 *state,
    const worr_native_input_delivery_feedback_v1 *feedback,
    uint64_t now_ms)
{
    return feedback && feedback->struct_size == sizeof(*feedback) &&
           feedback->schema_version ==
               WORR_NATIVE_INPUT_DELIVERY_VERSION &&
           Worr_CommandCursorValidV1(feedback->received_cursor) &&
           Worr_CommandCursorValidV1(feedback->consumed_cursor) &&
           feedback->received_cursor.epoch == state->command_epoch &&
           feedback->consumed_cursor.epoch == state->command_epoch &&
           feedback->consumed_cursor.contiguous_sequence <=
               feedback->received_cursor.contiguous_sequence &&
           feedback->observed_at_ms <= now_ms &&
           feedback->reserved[0] == 0 && feedback->reserved[1] == 0;
}

static bool feedback_rolled_back(
    const worr_native_input_delivery_state_v1 *state,
    const worr_native_input_delivery_feedback_v1 *feedback)
{
    return feedback->received_cursor.contiguous_sequence <
               state->last_received_sequence ||
           feedback->consumed_cursor.contiguous_sequence <
               state->last_consumed_sequence ||
           feedback->observed_at_ms < state->last_feedback_time_ms;
}

static bool candidates_valid(
    const worr_native_input_delivery_config_v1 *config,
    const worr_native_input_delivery_feedback_v1 *feedback,
    const worr_native_input_delivery_candidate_v1 *candidates,
    size_t candidate_count, uint64_t now_ms)
{
    uint32_t expected_sequence;
    uint64_t prior_sample_time = 0;
    size_t index;

    if (candidate_count > WORR_NATIVE_INPUT_DELIVERY_MAX_CANDIDATES)
        return false;
    if (candidate_count == 0)
        return true;
    if (!candidates ||
        feedback->received_cursor.contiguous_sequence == UINT32_MAX) {
        return false;
    }

    expected_sequence =
        feedback->received_cursor.contiguous_sequence + UINT32_C(1);
    for (index = 0; index < candidate_count; ++index) {
        const worr_native_input_delivery_candidate_v1 *candidate =
            &candidates[index];

        if (candidate->command_id.epoch !=
                feedback->received_cursor.epoch ||
            candidate->command_id.sequence != expected_sequence ||
            candidate->reserved0 != 0 ||
            candidate->transmit_count >
                config->maximum_transmissions_per_command ||
            candidate->last_transmit_time_ms > now_ms ||
            (candidate->transmit_count == 0 &&
             candidate->last_transmit_time_ms != 0) ||
            (index != 0 && candidate->sample_time_us < prior_sample_time)) {
            return false;
        }

        prior_sample_time = candidate->sample_time_us;
        if (index + 1u < candidate_count) {
            if (expected_sequence == UINT32_MAX)
                return false;
            ++expected_sequence;
        }
    }
    return true;
}

static bool output_overlaps(
    const worr_native_input_delivery_state_v1 *state,
    const worr_native_input_delivery_config_v1 *config,
    const worr_native_input_delivery_feedback_v1 *feedback,
    const worr_adaptive_input_output_v1 *adaptive_output,
    const worr_native_input_delivery_candidate_v1 *candidates,
    size_t candidate_count,
    const worr_native_input_delivery_plan_v1 *plan_out)
{
    const size_t output_bytes = sizeof(*plan_out);

    return regions_overlap(plan_out, output_bytes, state, sizeof(*state)) ||
           regions_overlap(plan_out, output_bytes, config, sizeof(*config)) ||
           regions_overlap(plan_out, output_bytes, feedback,
                           sizeof(*feedback)) ||
           regions_overlap(plan_out, output_bytes, adaptive_output,
                           sizeof(*adaptive_output)) ||
           regions_overlap(plan_out, output_bytes, candidates,
                           candidate_count * sizeof(*candidates));
}

static bool retry_due(
    const worr_native_input_delivery_candidate_v1 *candidate,
    const worr_native_input_delivery_config_v1 *config, uint64_t now_ms)
{
    return candidate->transmit_count != 0 &&
           now_ms - candidate->last_transmit_time_ms >=
               config->retry_interval_ms;
}

static bool retry_exhausted(
    const worr_native_input_delivery_candidate_v1 *candidate,
    const worr_native_input_delivery_config_v1 *config)
{
    return candidate->transmit_count >=
           config->maximum_transmissions_per_command;
}

static void select_candidate(
    size_t index, uint32_t flags, bool selected[], uint32_t selection_flags[],
    uint32_t *selection_count, uint32_t *fresh_count,
    uint32_t *redundant_count)
{
    if (selected[index]) {
        selection_flags[index] |= flags;
        return;
    }

    selected[index] = true;
    selection_flags[index] = flags;
    ++*selection_count;
    if ((flags & WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRESH) != 0)
        ++*fresh_count;
    if ((flags & WORR_NATIVE_INPUT_DELIVERY_SELECTION_REDUNDANT) != 0)
        ++*redundant_count;
}

void Worr_NativeInputDeliveryDefaultConfigV1(
    worr_native_input_delivery_config_v1 *config_out)
{
    if (!config_out)
        return;

    memset(config_out, 0, sizeof(*config_out));
    config_out->struct_size = sizeof(*config_out);
    config_out->schema_version = WORR_NATIVE_INPUT_DELIVERY_VERSION;
    config_out->maximum_batch_commands = 8;
    config_out->maximum_redundant_commands = 3;
    config_out->datagram_budget_bytes =
        WORR_NATIVE_INPUT_DELIVERY_MAX_DATAGRAM_BYTES;
    /* Account each selected command as an independently framed current WNE
     * DATA entry plus the one shared WTC footer.  This conservative default
     * fits six exact WNC1 commands under 1,200 bytes.  A later negotiated
     * batch codec may explicitly configure a smaller amortized overhead. */
    config_out->batch_overhead_bytes =
        WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES;
    config_out->per_command_overhead_bytes =
        WORR_NATIVE_CARRIER_WIRE_ENTRY_HEADER_BYTES +
        WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES;
    config_out->retry_interval_ms = 100;
    config_out->maximum_transmissions_per_command = 8;
}

bool Worr_NativeInputDeliveryResetV1(
    worr_native_input_delivery_state_v1 *state_out,
    uint32_t command_epoch)
{
    worr_native_input_delivery_state_v1 staged;

    if (!state_out || command_epoch == 0)
        return false;

    memset(&staged, 0, sizeof(staged));
    staged.struct_size = sizeof(staged);
    staged.schema_version = WORR_NATIVE_INPUT_DELIVERY_VERSION;
    staged.command_epoch = command_epoch;
    staged.initialized = 1;
    memcpy(state_out, &staged, sizeof(staged));
    return true;
}

bool Worr_NativeInputDeliveryValidateV1(
    const worr_native_input_delivery_state_v1 *state)
{
    return state && state->struct_size == sizeof(*state) &&
           state->schema_version == WORR_NATIVE_INPUT_DELIVERY_VERSION &&
           state->command_epoch != 0 && state->initialized == 1 &&
           state->last_consumed_sequence <= state->last_received_sequence &&
           state->reserved[0] == 0 && state->reserved[1] == 0;
}

uint32_t Worr_NativeInputDeliveryPlanV1(
    worr_native_input_delivery_state_v1 *state,
    const worr_native_input_delivery_config_v1 *config,
    const worr_native_input_delivery_feedback_v1 *feedback,
    const worr_adaptive_input_output_v1 *adaptive_output,
    const worr_native_input_delivery_candidate_v1 *candidates,
    size_t candidate_count, uint64_t now_ms,
    worr_native_input_delivery_plan_v1 *plan_out)
{
    worr_native_input_delivery_plan_v1 staged;
    bool selected[WORR_NATIVE_INPUT_DELIVERY_MAX_CANDIDATES] = {false};
    uint32_t selection_flags[WORR_NATIVE_INPUT_DELIVERY_MAX_CANDIDATES] = {0};
    uint32_t command_wire_bytes;
    uint32_t wire_selection_limit;
    uint32_t selection_limit;
    uint32_t adaptive_redundancy_limit;
    uint32_t selection_count = 0;
    uint32_t fresh_count = 0;
    uint32_t redundant_count = 0;
    uint32_t fresh_candidate_count = 0;
    uint32_t retry_exhausted_count = 0;
    uint32_t due_redundant_count = 0;
    size_t newest_fresh = candidate_count;
    size_t index;

    if (!state || !Worr_NativeInputDeliveryValidateV1(state))
        return WORR_NATIVE_INPUT_DELIVERY_INVALID_STATE;

    saturating_increment(&state->telemetry.plan_attempts);
    if (!config || !feedback || !adaptive_output || !plan_out ||
        (candidate_count != 0 && !candidates)) {
        saturating_increment(&state->telemetry.invalid_arguments);
        return WORR_NATIVE_INPUT_DELIVERY_INVALID_ARGUMENT;
    }
    if (!config_valid(config)) {
        saturating_increment(&state->telemetry.invalid_configs);
        return WORR_NATIVE_INPUT_DELIVERY_INVALID_CONFIG;
    }
    if (!feedback_valid(state, feedback, now_ms)) {
        saturating_increment(&state->telemetry.invalid_feedback);
        return WORR_NATIVE_INPUT_DELIVERY_INVALID_FEEDBACK;
    }
    if (feedback_rolled_back(state, feedback)) {
        saturating_increment(&state->telemetry.feedback_rollbacks);
        return WORR_NATIVE_INPUT_DELIVERY_FEEDBACK_ROLLBACK;
    }
    if (!adaptive_output_valid(adaptive_output, now_ms)) {
        saturating_increment(&state->telemetry.invalid_adaptive_outputs);
        return WORR_NATIVE_INPUT_DELIVERY_INVALID_ADAPTIVE_OUTPUT;
    }
    if (!candidates_valid(config, feedback, candidates, candidate_count,
                          now_ms)) {
        saturating_increment(&state->telemetry.invalid_candidates);
        return WORR_NATIVE_INPUT_DELIVERY_INVALID_CANDIDATES;
    }
    if (output_overlaps(state, config, feedback, adaptive_output, candidates,
                        candidate_count, plan_out)) {
        saturating_increment(&state->telemetry.output_overlaps);
        return WORR_NATIVE_INPUT_DELIVERY_OUTPUT_OVERLAP;
    }

    command_wire_bytes = WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES +
                         config->per_command_overhead_bytes;
    wire_selection_limit =
        (config->datagram_budget_bytes - config->batch_overhead_bytes) /
        command_wire_bytes;
    selection_limit = min_u32(config->maximum_batch_commands,
                              wire_selection_limit);
    adaptive_redundancy_limit = min_u32(
        adaptive_output->redundancy_frames,
        config->maximum_redundant_commands);

    memset(&staged, 0, sizeof(staged));
    staged.struct_size = sizeof(staged);
    staged.schema_version = WORR_NATIVE_INPUT_DELIVERY_VERSION;
    staged.flags = WORR_NATIVE_INPUT_DELIVERY_PLAN_VALID;
    staged.planned_at_ms = now_ms;
    staged.received_cursor = feedback->received_cursor;
    staged.consumed_cursor = feedback->consumed_cursor;
    staged.adaptive_redundancy_limit = adaptive_redundancy_limit;
    if (wire_selection_limit < config->maximum_batch_commands) {
        staged.flags |=
            WORR_NATIVE_INPUT_DELIVERY_PLAN_WIRE_BUDGET_CAPPED;
    }
    if (adaptive_output->redundancy_frames >
        config->maximum_redundant_commands) {
        staged.flags |=
            WORR_NATIVE_INPUT_DELIVERY_PLAN_REDUNDANCY_CAPPED;
    }

    for (index = 0; index < candidate_count; ++index) {
        const worr_native_input_delivery_candidate_v1 *candidate =
            &candidates[index];

        if (candidate->transmit_count == 0) {
            newest_fresh = index;
            ++fresh_candidate_count;
        } else if (retry_exhausted(candidate, config)) {
            ++retry_exhausted_count;
            staged.flags |=
                WORR_NATIVE_INPUT_DELIVERY_PLAN_RETRY_EXHAUSTED;
        } else if (retry_due(candidate, config, now_ms)) {
            ++due_redundant_count;
        } else {
            staged.flags |=
                WORR_NATIVE_INPUT_DELIVERY_PLAN_RETRY_NOT_DUE;
        }
    }

    /* Reserve the first slot for the newest never-sent input.  This is the
     * freshness half of the two-slot minimum contract. */
    if (newest_fresh != candidate_count) {
        uint32_t flags =
            WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRESH |
            WORR_NATIVE_INPUT_DELIVERY_SELECTION_NEWEST_FRESH;
        if (newest_fresh == 0)
            flags |= WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRONTIER;
        select_candidate(newest_fresh, flags, selected, selection_flags,
                         &selection_count, &fresh_count,
                         &redundant_count);
        staged.flags |=
            WORR_NATIVE_INPUT_DELIVERY_PLAN_NEWEST_FRESH_INCLUDED;
        if (newest_fresh == 0) {
            staged.flags |=
                WORR_NATIVE_INPUT_DELIVERY_PLAN_FRONTIER_INCLUDED;
        }
    }

    /* Reserve the second slot for the exact receive frontier whenever it is
     * fresh or an adaptive retry is due.  This prevents newest-first delivery
     * from permanently leaving a contiguous acknowledgement gap. */
    if (candidate_count != 0 && !selected[0] &&
        selection_count < selection_limit) {
        const worr_native_input_delivery_candidate_v1 *frontier =
            &candidates[0];
        if (frontier->transmit_count == 0) {
            select_candidate(
                0, WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRESH |
                       WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRONTIER,
                selected, selection_flags, &selection_count, &fresh_count,
                &redundant_count);
            staged.flags |=
                WORR_NATIVE_INPUT_DELIVERY_PLAN_FRONTIER_INCLUDED;
        } else if (!retry_exhausted(frontier, config) &&
                   retry_due(frontier, config, now_ms) &&
                   redundant_count < adaptive_redundancy_limit) {
            select_candidate(
                0, WORR_NATIVE_INPUT_DELIVERY_SELECTION_REDUNDANT |
                       WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRONTIER,
                selected, selection_flags, &selection_count, &fresh_count,
                &redundant_count);
            staged.flags |=
                WORR_NATIVE_INPUT_DELIVERY_PLAN_FRONTIER_INCLUDED;
        }
    }

    /* Fill remaining batch space with never-sent commands, oldest first. */
    for (index = 0;
         index < candidate_count && selection_count < selection_limit;
         ++index) {
        uint32_t flags;
        if (selected[index] || candidates[index].transmit_count != 0)
            continue;
        flags = WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRESH;
        if (index == 0)
            flags |= WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRONTIER;
        select_candidate(index, flags, selected, selection_flags,
                         &selection_count, &fresh_count,
                         &redundant_count);
        if (index == 0) {
            staged.flags |=
                WORR_NATIVE_INPUT_DELIVERY_PLAN_FRONTIER_INCLUDED;
        }
    }

    /* Adaptive redundancy is selective: only due, non-exhausted identities
     * are repeated, oldest first, and never beyond the evaluated depth. */
    for (index = 0;
         index < candidate_count && selection_count < selection_limit &&
         redundant_count < adaptive_redundancy_limit;
         ++index) {
        uint32_t flags;
        if (selected[index] || candidates[index].transmit_count == 0 ||
            retry_exhausted(&candidates[index], config) ||
            !retry_due(&candidates[index], config, now_ms)) {
            continue;
        }
        flags = WORR_NATIVE_INPUT_DELIVERY_SELECTION_REDUNDANT;
        if (index == 0)
            flags |= WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRONTIER;
        select_candidate(index, flags, selected, selection_flags,
                         &selection_count, &fresh_count,
                         &redundant_count);
        if (index == 0) {
            staged.flags |=
                WORR_NATIVE_INPUT_DELIVERY_PLAN_FRONTIER_INCLUDED;
        }
    }

    if (due_redundant_count > redundant_count) {
        staged.flags |=
            WORR_NATIVE_INPUT_DELIVERY_PLAN_REDUNDANCY_CAPPED;
    }
    if (selection_limit <
        fresh_candidate_count +
            min_u32(due_redundant_count, adaptive_redundancy_limit)) {
        staged.flags |= WORR_NATIVE_INPUT_DELIVERY_PLAN_BATCH_CAPPED;
    }
    if (fresh_count != 0)
        staged.flags |= WORR_NATIVE_INPUT_DELIVERY_PLAN_HAS_FRESH;
    if (redundant_count != 0) {
        staged.flags |=
            WORR_NATIVE_INPUT_DELIVERY_PLAN_HAS_REDUNDANCY;
    }

    staged.selection_count = selection_count;
    staged.fresh_count = fresh_count;
    staged.redundant_count = redundant_count;
    staged.deferred_count = (uint32_t)candidate_count - selection_count;
    staged.retry_exhausted_count = retry_exhausted_count;
    if (selection_count != 0) {
        staged.wire_bytes = config->batch_overhead_bytes +
                            selection_count * command_wire_bytes;
    }

    /* Emit selected identities in canonical ascending order independent of
     * the priority order used above. */
    {
        uint32_t output_index = 0;
        for (index = 0; index < candidate_count; ++index) {
            worr_native_input_delivery_selection_v1 *selection;
            if (!selected[index])
                continue;
            selection = &staged.selections[output_index++];
            selection->command_id = candidates[index].command_id;
            selection->sample_time_us = candidates[index].sample_time_us;
            selection->candidate_index = (uint32_t)index;
            selection->attempt_ordinal =
                candidates[index].transmit_count + UINT32_C(1);
            selection->flags = selection_flags[index];
        }
    }

    if (state->last_received_sequence !=
            feedback->received_cursor.contiguous_sequence ||
        state->last_consumed_sequence !=
            feedback->consumed_cursor.contiguous_sequence ||
        state->last_feedback_time_ms != feedback->observed_at_ms) {
        saturating_increment(&state->telemetry.feedback_advances);
    }
    state->last_received_sequence =
        feedback->received_cursor.contiguous_sequence;
    state->last_consumed_sequence =
        feedback->consumed_cursor.contiguous_sequence;
    state->last_feedback_time_ms = feedback->observed_at_ms;
    saturating_increment(&state->decision_serial);
    staged.decision_serial = state->decision_serial;

    if (selection_count == 0) {
        staged.result = WORR_NATIVE_INPUT_DELIVERY_NOTHING_DUE;
        saturating_increment(&state->telemetry.nothing_due);
    } else {
        staged.result = WORR_NATIVE_INPUT_DELIVERY_PLANNED;
        saturating_increment(&state->telemetry.plans);
    }
    saturating_add(&state->telemetry.selected_fresh, fresh_count);
    saturating_add(&state->telemetry.selected_redundant, redundant_count);
    saturating_add(&state->telemetry.deferred, staged.deferred_count);
    saturating_add(&state->telemetry.retry_exhausted,
                   retry_exhausted_count);

    memcpy(plan_out, &staged, sizeof(staged));
    return staged.result;
}
