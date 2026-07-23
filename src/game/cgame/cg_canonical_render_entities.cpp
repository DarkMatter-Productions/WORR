/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_canonical_render_entities.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

bool limits_valid(cg_canonical_entity_adapter_limits_v1 limits)
{
    return limits.max_entities > 1u && limits.max_models != 0u &&
           limits.max_sounds != 0u;
}

bool range_valid(const void *pointer, std::size_t count, std::size_t stride,
                 std::uintptr_t *begin_out, std::uintptr_t *end_out)
{
    if (!begin_out || !end_out || (count != 0u && !pointer) ||
        (stride != 0u && count >
                             std::numeric_limits<std::size_t>::max() /
                                 stride)) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
    const std::size_t bytes = count * stride;
    if (bytes > std::numeric_limits<std::uintptr_t>::max() - begin)
        return false;
    *begin_out = begin;
    *end_out = begin + bytes;
    return true;
}

bool ranges_overlap(const void *left, std::size_t left_count,
                    std::size_t left_stride, const void *right,
                    std::size_t right_count, std::size_t right_stride)
{
    std::uintptr_t left_begin;
    std::uintptr_t left_end;
    std::uintptr_t right_begin;
    std::uintptr_t right_end;
    if (!range_valid(left, left_count, left_stride, &left_begin, &left_end) ||
        !range_valid(right, right_count, right_stride, &right_begin,
                     &right_end)) {
        return true;
    }
    if (left_begin == left_end || right_begin == right_end)
        return false;
    return left_begin < right_end && right_begin < left_end;
}

bool generation_equal(const worr_snapshot_entity_generation_v2 &left,
                      const worr_snapshot_entity_generation_v2 &right)
{
    return left.identity.index == right.identity.index &&
           left.identity.generation == right.identity.generation &&
           left.provenance_flags == right.provenance_flags &&
           left.reserved0 == right.reserved0;
}

constexpr std::uint32_t kKnownRecordFlags =
    CG_CANONICAL_RENDER_ENTITY_CURRENT |
    CG_CANONICAL_RENDER_ENTITY_PREVIOUS |
    CG_CANONICAL_RENDER_ENTITY_GENERATION_CONTINUOUS;

constexpr std::uint32_t kKnownEntityBlocks =
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_GENERATION |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_MISSING |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_COMPONENT |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_ANGULAR_SPEED |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_DISCRETE_TRANSITION |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_POLICY;

constexpr std::uint32_t kLifecycleDiscontinuityBlocks =
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_GENERATION |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_MISSING |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED |
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_ANGULAR_SPEED;

bool state_discontinuous(const entity_state_t &previous,
                         const entity_state_t &current)
{
    if (previous.modelindex != current.modelindex ||
        previous.modelindex2 != current.modelindex2 ||
        previous.modelindex3 != current.modelindex3 ||
        previous.modelindex4 != current.modelindex4) {
        return true;
    }
    for (std::size_t axis = 0; axis < 3u; ++axis) {
        if (std::fabs(current.origin[axis] - previous.origin[axis]) >
            512.0f) {
            return true;
        }
    }
    return false;
}

bool presentation_transform_changed(const entity_state_t &previous,
                                    const entity_state_t &current)
{
    if (previous.modelindex != current.modelindex ||
        previous.modelindex2 != current.modelindex2 ||
        previous.modelindex3 != current.modelindex3 ||
        previous.modelindex4 != current.modelindex4) {
        return true;
    }
    for (std::size_t axis = 0; axis < 3u; ++axis) {
        if (previous.origin[axis] != current.origin[axis] ||
            previous.angles[axis] != current.angles[axis] ||
            previous.old_origin[axis] != current.old_origin[axis]) {
            return true;
        }
    }
    return false;
}

bool record_valid(const cg_canonical_render_entity_v1 &record,
                  cg_canonical_entity_adapter_limits_v1 limits)
{
    if (record.identity == 0u || record.identity >= limits.max_entities ||
        (record.flags & ~kKnownRecordFlags) != 0u ||
        (record.flags & (CG_CANONICAL_RENDER_ENTITY_CURRENT |
                         CG_CANONICAL_RENDER_ENTITY_PREVIOUS)) == 0u ||
        ((record.flags & CG_CANONICAL_RENDER_ENTITY_GENERATION_CONTINUOUS) !=
             0u &&
         (record.flags & (CG_CANONICAL_RENDER_ENTITY_CURRENT |
                          CG_CANONICAL_RENDER_ENTITY_PREVIOUS)) !=
             (CG_CANONICAL_RENDER_ENTITY_CURRENT |
              CG_CANONICAL_RENDER_ENTITY_PREVIOUS))) {
        return false;
    }
    if ((record.flags & CG_CANONICAL_RENDER_ENTITY_CURRENT) != 0u &&
        (record.current.number != static_cast<int>(record.identity) ||
         record.current_generation == 0u)) {
        return false;
    }
    if ((record.flags & CG_CANONICAL_RENDER_ENTITY_PREVIOUS) != 0u &&
        (record.previous.number != static_cast<int>(record.identity) ||
         record.previous_generation == 0u)) {
        return false;
    }
    if ((record.flags & CG_CANONICAL_RENDER_ENTITY_GENERATION_CONTINUOUS) !=
            0u &&
        (record.current_generation != record.previous_generation ||
         record.current_provenance != record.previous_provenance)) {
        return false;
    }
    return true;
}

cg_canonical_render_entities_result_v1 validate_range(
    const worr_snapshot_entity_v2 *entities, std::uint32_t count,
    cg_canonical_entity_adapter_limits_v1 limits)
{
    std::uint32_t previous_identity = 0u;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t identity =
            entities[index].generation.identity.index;
        if (identity <= previous_identity)
            return CG_CANONICAL_RENDER_ENTITIES_OUT_OF_ORDER;
        previous_identity = identity;

        entity_state_t scratch{};
        if (CG_CanonicalEntityToRenderStateV1(
                &entities[index], limits, &scratch) !=
            CG_CANONICAL_ENTITY_ADAPTER_OK) {
            return CG_CANONICAL_RENDER_ENTITIES_INVALID_ENTITY;
        }
    }
    return CG_CANONICAL_RENDER_ENTITIES_OK;
}

std::uint32_t union_count(const worr_snapshot_entity_v2 *previous_entities,
                          std::uint32_t previous_count,
                          const worr_snapshot_entity_v2 *current_entities,
                          std::uint32_t current_count)
{
    std::uint32_t previous_index = 0u;
    std::uint32_t current_index = 0u;
    std::uint32_t count = 0u;
    while (previous_index < previous_count || current_index < current_count) {
        const std::uint32_t previous_identity =
            previous_index < previous_count
                ? previous_entities[previous_index].generation.identity.index
                : UINT32_MAX;
        const std::uint32_t current_identity =
            current_index < current_count
                ? current_entities[current_index].generation.identity.index
                : UINT32_MAX;
        if (previous_identity <= current_identity)
            ++previous_index;
        if (current_identity <= previous_identity)
            ++current_index;
        ++count;
    }
    return count;
}

std::uint64_t add_saturated_u64(std::uint64_t left, std::uint64_t right)
{
    return left > UINT64_MAX - right ? UINT64_MAX : left + right;
}

void increment_saturated_u64(std::uint64_t &value)
{
    if (value != UINT64_MAX)
        ++value;
}

std::uint64_t absolute_difference_u64(std::uint64_t left,
                                      std::uint64_t right)
{
    return left >= right ? left - right : right - left;
}

std::uint64_t quarter_step_toward(std::uint64_t current,
                                  std::uint64_t sample)
{
    if (current == sample)
        return current;
    const std::uint64_t difference = absolute_difference_u64(current, sample);
    const std::uint64_t step =
        difference / 4u + (difference % 4u != 0u ? 1u : 0u);
    return sample > current ? add_saturated_u64(current, step)
                            : current - step;
}

void interpolation_delay_status(
    const cg_canonical_interpolation_delay_state_v1 &state,
    cg_canonical_interpolation_delay_status_v1 *status_out)
{
    cg_canonical_interpolation_delay_status_v1 status{};
    status.delay_us = state.current_delay_us;
    status.baseline_delay_us = state.baseline_delay_us;
    status.maximum_delay_us = state.maximum_delay_us;
    status.cadence_us = state.cadence_us;
    status.jitter_us = state.jitter_us;
    status.last_jitter_us = state.last_jitter_us;
    status.interval_observations = state.interval_observations;
    status.rise_adjustments = state.rise_adjustments;
    status.recovery_adjustments = state.recovery_adjustments;
    status.pressure_observations = state.pressure_observations;
    status.reset_count = state.reset_count;
    status.adaptive_enabled = state.adaptive_enabled;
    status.last_adjustment = state.last_adjustment;
    *status_out = status;
}

void initialize_interpolation_delay(
    cg_canonical_interpolation_delay_state_v1 &state,
    const cg_canonical_interpolation_delay_config_v1 &config,
    const cg_canonical_interpolation_delay_observation_v1 &observation)
{
    const std::uint64_t resets =
        state.reset_count == UINT64_MAX ? UINT64_MAX : state.reset_count + 1u;
    state = {};
    state.initialized = 1u;
    state.epoch = observation.epoch;
    state.adaptive_enabled = config.adaptive_enabled;
    state.stream_reset_count = observation.stream_reset_count;
    state.baseline_delay_us = config.baseline_delay_us;
    state.maximum_delay_us = config.maximum_delay_us;
    state.current_delay_us = config.baseline_delay_us;
    state.reset_count = resets;
    state.last_adjustment =
        CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_RESET;
    if (observation.snapshot_valid) {
        state.snapshot_anchor_valid = 1u;
        state.last_accepted_snapshot_count =
            observation.accepted_snapshot_count;
        state.last_snapshot_server_time_us =
            observation.snapshot_server_time_us;
        state.last_snapshot_receive_time_us =
            observation.snapshot_receive_time_us;
    }
}

} // namespace

cg_canonical_render_entities_result_v1 CG_BuildCanonicalRenderEntitiesV1(
    const worr_snapshot_entity_v2 *previous_entities,
    std::uint32_t previous_count,
    const worr_snapshot_entity_v2 *current_entities,
    std::uint32_t current_count,
    cg_canonical_entity_adapter_limits_v1 limits,
    cg_canonical_render_entity_v1 *records_out,
    std::uint32_t record_capacity,
    std::uint32_t *count_out)
{
    if (!records_out || !count_out ||
        (previous_count != 0u && !previous_entities) ||
        (current_count != 0u && !current_entities)) {
        return CG_CANONICAL_RENDER_ENTITIES_INVALID_ARGUMENT;
    }
    if (!limits_valid(limits))
        return CG_CANONICAL_RENDER_ENTITIES_INVALID_LIMITS;
    if (previous_count > CG_CANONICAL_RENDER_SNAPSHOT_ENTITY_CAPACITY ||
        current_count > CG_CANONICAL_RENDER_SNAPSHOT_ENTITY_CAPACITY ||
        record_capacity > CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY) {
        return CG_CANONICAL_RENDER_ENTITIES_COUNT_LIMIT;
    }

    auto result = validate_range(previous_entities, previous_count, limits);
    if (result != CG_CANONICAL_RENDER_ENTITIES_OK)
        return result;
    result = validate_range(current_entities, current_count, limits);
    if (result != CG_CANONICAL_RENDER_ENTITIES_OK)
        return result;

    const std::uint32_t required = union_count(
        previous_entities, previous_count, current_entities, current_count);
    if (record_capacity < required)
        return CG_CANONICAL_RENDER_ENTITIES_OUTPUT_TOO_SMALL;
    if (ranges_overlap(previous_entities, previous_count,
                       sizeof(*previous_entities), records_out,
                       record_capacity, sizeof(*records_out)) ||
        ranges_overlap(current_entities, current_count,
                       sizeof(*current_entities), records_out,
                       record_capacity, sizeof(*records_out))) {
        return CG_CANONICAL_RENDER_ENTITIES_OVERLAP;
    }

    std::uint32_t previous_index = 0u;
    std::uint32_t current_index = 0u;
    std::uint32_t output_index = 0u;
    while (previous_index < previous_count || current_index < current_count) {
        const worr_snapshot_entity_v2 *previous =
            previous_index < previous_count
                ? &previous_entities[previous_index]
                : nullptr;
        const worr_snapshot_entity_v2 *current =
            current_index < current_count
                ? &current_entities[current_index]
                : nullptr;
        const std::uint32_t previous_identity =
            previous ? previous->generation.identity.index : UINT32_MAX;
        const std::uint32_t current_identity =
            current ? current->generation.identity.index : UINT32_MAX;
        const bool take_previous = previous_identity <= current_identity;
        const bool take_current = current_identity <= previous_identity;

        cg_canonical_render_entity_v1 record{};
        record.identity = take_current ? current_identity : previous_identity;
        if (take_current) {
            (void)CG_CanonicalEntityToRenderStateV1(
                current, limits, &record.current);
            record.previous = record.current;
            record.current_generation =
                current->generation.identity.generation;
            record.current_provenance =
                current->generation.provenance_flags;
            record.flags |= CG_CANONICAL_RENDER_ENTITY_CURRENT;
        }
        if (take_previous) {
            entity_state_t previous_state{};
            (void)CG_CanonicalEntityToRenderStateV1(
                previous, limits, &previous_state);
            record.previous = previous_state;
            record.previous_generation =
                previous->generation.identity.generation;
            record.previous_provenance =
                previous->generation.provenance_flags;
            record.flags |= CG_CANONICAL_RENDER_ENTITY_PREVIOUS;
        }
        if (take_current && take_previous &&
            generation_equal(current->generation, previous->generation)) {
            record.flags |=
                CG_CANONICAL_RENDER_ENTITY_GENERATION_CONTINUOUS;
        } else if (take_current) {
            record.previous = record.current;
        }

        records_out[output_index++] = record;
        if (take_previous)
            ++previous_index;
        if (take_current)
            ++current_index;
    }
    *count_out = output_index;
    return CG_CANONICAL_RENDER_ENTITIES_OK;
}

cg_canonical_render_sample_result_v1 CG_SelectCanonicalRenderSampleV1(
    const cg_canonical_render_entity_v1 *record,
    const worr_snapshot_timeline_entity_sample_v1 *sample,
    cg_canonical_entity_adapter_limits_v1 limits,
    cg_canonical_render_sample_v1 *selection_out)
{
    if (!record || !sample || !selection_out)
        return CG_CANONICAL_RENDER_SAMPLE_INVALID_ARGUMENT;
    if (!limits_valid(limits) || !record_valid(*record, limits))
        return CG_CANONICAL_RENDER_SAMPLE_INVALID_RECORD;
    if (sample->struct_size != sizeof(*sample) ||
        sample->schema_version != WORR_SNAPSHOT_TIMELINE_VERSION ||
        sample->entity_index != record->identity || !sample->visible ||
        sample->entity.generation.identity.index != record->identity ||
        (sample->blocking_reasons & ~kKnownEntityBlocks) != 0u) {
        return CG_CANONICAL_RENDER_SAMPLE_INVALID_SAMPLE;
    }

    const bool has_previous =
        (record->flags & CG_CANONICAL_RENDER_ENTITY_PREVIOUS) != 0u;
    const bool has_current =
        (record->flags & CG_CANONICAL_RENDER_ENTITY_CURRENT) != 0u;
    const bool continuous =
        (record->flags &
         CG_CANONICAL_RENDER_ENTITY_GENERATION_CONTINUOUS) != 0u;
    const entity_state_t *direct = nullptr;
    std::uint32_t expected_generation = 0u;
    std::uint32_t expected_provenance = 0u;
    switch (sample->mode) {
    case WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS:
        if (!has_previous)
            return CG_CANONICAL_RENDER_SAMPLE_INVALID_SAMPLE;
        direct = &record->previous;
        expected_generation = record->previous_generation;
        expected_provenance = record->previous_provenance;
        break;
    case WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT:
        if (!has_current)
            return CG_CANONICAL_RENDER_SAMPLE_INVALID_SAMPLE;
        direct = &record->current;
        expected_generation = record->current_generation;
        expected_provenance = record->current_provenance;
        break;
    case WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED:
    case WORR_SNAPSHOT_TIMELINE_ENTITY_EXTRAPOLATED:
        if (!has_previous || !has_current || !continuous)
            return CG_CANONICAL_RENDER_SAMPLE_UNSAFE;
        expected_generation = record->current_generation;
        expected_provenance = record->current_provenance;
        break;
    default:
        return CG_CANONICAL_RENDER_SAMPLE_INVALID_SAMPLE;
    }
    if (sample->entity.generation.identity.generation !=
            expected_generation ||
        sample->entity.generation.provenance_flags != expected_provenance) {
        return CG_CANONICAL_RENDER_SAMPLE_INVALID_SAMPLE;
    }

    cg_canonical_render_sample_v1 selected{};
    if (CG_CanonicalEntityToRenderStateV1(&sample->entity, limits,
                                          &selected.state) !=
        CG_CANONICAL_ENTITY_ADAPTER_OK) {
        return CG_CANONICAL_RENDER_SAMPLE_ADAPTER_REJECTED;
    }
    selected.generation = expected_generation;
    selected.provenance = expected_provenance;
    selected.blocking_reasons = sample->blocking_reasons;

    const bool sampled_motion =
        sample->mode == WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED ||
        sample->mode == WORR_SNAPSHOT_TIMELINE_ENTITY_EXTRAPOLATED;
    const bool unsafe_transition =
        state_discontinuous(record->previous, record->current);
    if (sampled_motion && unsafe_transition) {
        direct = sample->mode == WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED
            ? &record->previous
            : &record->current;
        selected.state = *direct;
        selected.direct_endpoint = 1u;
    }

    *selection_out = selected;
    return CG_CANONICAL_RENDER_SAMPLE_OK;
}

bool CG_CanonicalPreviousOnlyEvidenceOrderedV1(
    std::uint64_t observed_removed_identities,
    std::uint64_t selected_visible_sources,
    std::uint64_t renderer_submitted_sources)
{
    return renderer_submitted_sources <= selected_visible_sources &&
           selected_visible_sources <= observed_removed_identities;
}

cg_canonical_render_lifecycle_result_v1
CG_ResolveCanonicalRenderLifecycleV1(
    const entity_state_t *cached_state,
    const entity_state_t *selected_state,
    std::uint32_t pending_blocking_reasons,
    std::uint64_t pending_discontinuity_key,
    std::uint64_t handled_discontinuity_key,
    std::uint32_t sample_mode,
    std::uint32_t sample_blocking_reasons,
    std::uint64_t sample_discontinuity_key,
    cg_canonical_render_lifecycle_decision_v1 *decision_out)
{
    if (!cached_state || !selected_state || !decision_out)
        return CG_CANONICAL_RENDER_LIFECYCLE_INVALID_ARGUMENT;
    if (cached_state->number <= 0 ||
        cached_state->number != selected_state->number ||
        sample_mode < WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS ||
        sample_mode > WORR_SNAPSHOT_TIMELINE_ENTITY_EXTRAPOLATED ||
        (sample_blocking_reasons & ~kKnownEntityBlocks) != 0u ||
        (pending_blocking_reasons & ~kLifecycleDiscontinuityBlocks) != 0u ||
        ((pending_blocking_reasons == 0u) !=
         (pending_discontinuity_key == 0u))) {
        return CG_CANONICAL_RENDER_LIFECYCLE_INVALID_INPUT;
    }

    const std::uint32_t lifecycle_blocks =
        sample_blocking_reasons & kLifecycleDiscontinuityBlocks;
    if (lifecycle_blocks != 0u && sample_discontinuity_key == 0u)
        return CG_CANONICAL_RENDER_LIFECYCLE_INVALID_INPUT;
    const bool holding_previous = lifecycle_blocks != 0u &&
        sample_mode == WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS;
    const bool transform_changed =
        presentation_transform_changed(*cached_state, *selected_state);
    const bool crossed_pending_discontinuity =
        pending_blocking_reasons != 0u && !holding_previous;
    const bool replaced_pending_discontinuity =
        pending_blocking_reasons != 0u && holding_previous &&
        pending_discontinuity_key != sample_discontinuity_key;
    const bool first_current_discontinuity =
        lifecycle_blocks != 0u && !holding_previous &&
        sample_discontinuity_key != handled_discontinuity_key;

    cg_canonical_render_lifecycle_decision_v1 decision{};
    decision.reset =
        state_discontinuous(*cached_state, *selected_state) ||
            (((pending_blocking_reasons | lifecycle_blocks) != 0u) &&
             transform_changed) ||
            crossed_pending_discontinuity ||
            replaced_pending_discontinuity ||
            first_current_discontinuity
        ? 1u
        : 0u;
    decision.pending_blocking_reasons =
        holding_previous ? lifecycle_blocks : 0u;
    decision.pending_discontinuity_key =
        holding_previous ? sample_discontinuity_key : 0u;
    decision.handled_discontinuity_key = handled_discontinuity_key;
    if (lifecycle_blocks != 0u && !holding_previous) {
        decision.handled_discontinuity_key = sample_discontinuity_key;
    } else if (crossed_pending_discontinuity ||
               replaced_pending_discontinuity) {
        decision.handled_discontinuity_key = pending_discontinuity_key;
    }
    *decision_out = decision;
    return CG_CANONICAL_RENDER_LIFECYCLE_OK;
}

std::uint64_t CG_CanonicalRenderEndpointKeyV1(
    worr_snapshot_timeline_ref_v1 endpoint)
{
    if (endpoint.slot == WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT ||
        endpoint.generation == 0u) {
        return 0u;
    }
    return (static_cast<std::uint64_t>(endpoint.generation) << 32u) |
        endpoint.slot;
}

cg_canonical_render_clock_plan_result_v1
CG_BuildCanonicalRenderClockPlanV1(
    const worr_snapshot_timeline_clock_state_v1 *clock,
    std::uint64_t host_time_us, std::uint32_t rate_q16, bool should_pause,
    cg_canonical_render_clock_plan_v1 *plan_out)
{
    if (!clock || !plan_out)
        return CG_CANONICAL_RENDER_CLOCK_PLAN_INVALID_ARGUMENT;
    if (clock->struct_size != sizeof(*clock) ||
        clock->schema_version != WORR_SNAPSHOT_TIMELINE_VERSION ||
        !clock->initialized || clock->epoch == 0u ||
        clock->rate_q16 == 0u ||
        clock->rate_q16 > WORR_SNAPSHOT_TIMELINE_RATE_MAX_Q16) {
        return CG_CANONICAL_RENDER_CLOCK_PLAN_INVALID_STATE;
    }
    if (rate_q16 == 0u ||
        rate_q16 > WORR_SNAPSHOT_TIMELINE_RATE_MAX_Q16) {
        return CG_CANONICAL_RENDER_CLOCK_PLAN_INVALID_RATE;
    }

    cg_canonical_render_clock_plan_v1 plan{};
    const std::uint64_t monotonic_host_time =
        host_time_us < clock->host_time_us ? clock->host_time_us
                                           : host_time_us;
    auto append = [&](std::uint16_t operation, std::uint32_t request_rate) {
        auto &request = plan.requests[plan.request_count++];
        request.struct_size = sizeof(request);
        request.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
        request.operation = operation;
        request.reset_reason = WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE;
        request.rate_q16 = request_rate;
        request.host_time_us = monotonic_host_time;
    };

    if (rate_q16 != clock->rate_q16) {
        append(WORR_SNAPSHOT_TIMELINE_CLOCK_SET_RATE, rate_q16);
    }
    if (should_pause != (clock->paused != 0u)) {
        append(should_pause ? WORR_SNAPSHOT_TIMELINE_CLOCK_PAUSE
                            : WORR_SNAPSHOT_TIMELINE_CLOCK_RESUME,
               0u);
    } else if (plan.request_count == 0u) {
        append(WORR_SNAPSHOT_TIMELINE_CLOCK_ADVANCE, 0u);
    }

    *plan_out = plan;
    return CG_CANONICAL_RENDER_CLOCK_PLAN_OK;
}

cg_canonical_legacy_alignment_result_v1
CG_ResolveCanonicalLegacyAlignmentV1(
    std::uint64_t clock_time_us, std::uint64_t desired_time_us,
    std::uint64_t maximum_delay_us, std::uint64_t *resolved_time_us)
{
    if (!resolved_time_us)
        return CG_CANONICAL_LEGACY_ALIGNMENT_INVALID_ARGUMENT;

    if (desired_time_us > clock_time_us) {
        *resolved_time_us = clock_time_us;
        return CG_CANONICAL_LEGACY_ALIGNMENT_CLAMPED_FUTURE;
    }

    const std::uint64_t earliest_time_us =
        clock_time_us > maximum_delay_us
            ? clock_time_us - maximum_delay_us
            : 0u;
    if (desired_time_us < earliest_time_us)
        return CG_CANONICAL_LEGACY_ALIGNMENT_PAST_LIMIT;

    *resolved_time_us = desired_time_us;
    return CG_CANONICAL_LEGACY_ALIGNMENT_OK;
}

cg_canonical_host_time_result_v1 CG_ExtendCanonicalHostTimeV1(
    cg_canonical_host_time_extender_v1 *state, std::uint32_t sample_ms,
    std::uint64_t *extended_ms_out)
{
    if (!state || !extended_ms_out)
        return CG_CANONICAL_HOST_TIME_INVALID_ARGUMENT;

    cg_canonical_host_time_extender_v1 next = *state;
    if (!next.initialized) {
        next.extended_ms = sample_ms;
        next.last_sample_ms = sample_ms;
        next.initialized = 1u;
    } else {
        const std::uint32_t delta = sample_ms - next.last_sample_ms;
        /* A forward modular delta below half the 32-bit domain includes the
         * normal UINT32_MAX -> 0 wrap.  A larger delta is a clock regression;
         * clamp without moving the accepted sample so recovery is monotonic. */
        if (delta <= UINT32_C(0x7fffffff)) {
            if (next.extended_ms > UINT64_MAX - delta)
                return CG_CANONICAL_HOST_TIME_OVERFLOW;
            next.extended_ms += delta;
            next.last_sample_ms = sample_ms;
        }
    }
    *state = next;
    *extended_ms_out = next.extended_ms;
    return CG_CANONICAL_HOST_TIME_OK;
}

cg_canonical_interpolation_delay_result_v1
CG_UpdateCanonicalInterpolationDelayV1(
    cg_canonical_interpolation_delay_state_v1 *state,
    const cg_canonical_interpolation_delay_config_v1 *config,
    const cg_canonical_interpolation_delay_observation_v1 *observation,
    cg_canonical_interpolation_delay_status_v1 *status_out)
{
    if (!state || !config || !observation || !status_out)
        return CG_CANONICAL_INTERPOLATION_DELAY_INVALID_ARGUMENT;
    if (config->adaptive_enabled > 1u || config->reserved0 != 0u ||
        config->baseline_delay_us >
            WORR_SNAPSHOT_TIMELINE_MAX_INTERPOLATION_DELAY_US ||
        config->maximum_delay_us < config->baseline_delay_us ||
        config->maximum_delay_us >
            WORR_SNAPSHOT_TIMELINE_MAX_INTERPOLATION_DELAY_US) {
        return CG_CANONICAL_INTERPOLATION_DELAY_INVALID_CONFIG;
    }
    if (observation->epoch == 0u || observation->snapshot_valid > 1u ||
        observation->pair_valid > 1u ||
        (!observation->snapshot_valid &&
         (observation->snapshot_server_time_us != 0u ||
          observation->snapshot_receive_time_us != 0u)) ||
        (observation->snapshot_valid &&
         observation->accepted_snapshot_count == 0u) ||
        (!observation->pair_valid &&
         (observation->pair_mode != 0u ||
          observation->pair_extrapolation_us != 0u)) ||
        (observation->pair_valid &&
         (observation->pair_mode <
              WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_EARLIEST ||
          observation->pair_mode >
              WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST))) {
        return CG_CANONICAL_INTERPOLATION_DELAY_INVALID_OBSERVATION;
    }

    cg_canonical_interpolation_delay_state_v1 next = *state;
    const bool reset = !next.initialized ||
        next.epoch != observation->epoch ||
        next.stream_reset_count != observation->stream_reset_count ||
        next.adaptive_enabled != config->adaptive_enabled ||
        next.baseline_delay_us != config->baseline_delay_us ||
        next.maximum_delay_us != config->maximum_delay_us;
    if (reset) {
        initialize_interpolation_delay(next, *config, *observation);
        *state = next;
        interpolation_delay_status(next, status_out);
        return CG_CANONICAL_INTERPOLATION_DELAY_OK;
    }

    next.last_adjustment = config->adaptive_enabled
        ? CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_NONE
        : CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_FIXED;
    if (!config->adaptive_enabled) {
        next.current_delay_us = config->baseline_delay_us;
        *state = next;
        interpolation_delay_status(next, status_out);
        return CG_CANONICAL_INTERPOLATION_DELAY_OK;
    }

    bool new_interval = false;
    std::uint64_t immediate_jitter_us = next.jitter_us;
    if (observation->snapshot_valid) {
        if (!next.snapshot_anchor_valid) {
            next.snapshot_anchor_valid = 1u;
            next.last_accepted_snapshot_count =
                observation->accepted_snapshot_count;
            next.last_snapshot_server_time_us =
                observation->snapshot_server_time_us;
            next.last_snapshot_receive_time_us =
                observation->snapshot_receive_time_us;
        } else if (observation->accepted_snapshot_count <
                       next.last_accepted_snapshot_count ||
                   (observation->accepted_snapshot_count ==
                        next.last_accepted_snapshot_count &&
                    (observation->snapshot_server_time_us !=
                         next.last_snapshot_server_time_us ||
                     observation->snapshot_receive_time_us !=
                         next.last_snapshot_receive_time_us))) {
            return CG_CANONICAL_INTERPOLATION_DELAY_INVALID_OBSERVATION;
        } else if (observation->accepted_snapshot_count >
                   next.last_accepted_snapshot_count) {
            if (observation->snapshot_server_time_us <=
                    next.last_snapshot_server_time_us ||
                observation->snapshot_receive_time_us <
                    next.last_snapshot_receive_time_us) {
                return CG_CANONICAL_INTERPOLATION_DELAY_INVALID_OBSERVATION;
            }
            const std::uint64_t accepted_delta =
                observation->accepted_snapshot_count -
                next.last_accepted_snapshot_count;
            const std::uint64_t server_interval =
                (observation->snapshot_server_time_us -
                 next.last_snapshot_server_time_us) /
                accepted_delta;
            const std::uint64_t receive_interval =
                (observation->snapshot_receive_time_us -
                 next.last_snapshot_receive_time_us) /
                accepted_delta;
            if (server_interval == 0u)
                return CG_CANONICAL_INTERPOLATION_DELAY_INVALID_OBSERVATION;

            immediate_jitter_us = absolute_difference_u64(
                server_interval, receive_interval);
            if (next.interval_observations == 0u) {
                next.cadence_us = server_interval;
                next.jitter_us = immediate_jitter_us;
            } else {
                next.cadence_us = quarter_step_toward(
                    next.cadence_us, server_interval);
                next.jitter_us = quarter_step_toward(
                    next.jitter_us, immediate_jitter_us);
            }
            next.last_jitter_us = immediate_jitter_us;
            increment_saturated_u64(next.interval_observations);
            next.last_accepted_snapshot_count =
                observation->accepted_snapshot_count;
            next.last_snapshot_server_time_us =
                observation->snapshot_server_time_us;
            next.last_snapshot_receive_time_us =
                observation->snapshot_receive_time_us;
            new_interval = true;
        }
    }

    const std::uint64_t jitter_target = std::max(
        next.jitter_us, new_interval ? immediate_jitter_us : 0u);
    std::uint64_t target_delay = std::min(
        config->maximum_delay_us,
        add_saturated_u64(config->baseline_delay_us, jitter_target));
    bool pressure_rise = false;
    bool recovery_signal = new_interval;
    if (observation->pair_valid) {
        if (observation->pair_mode ==
                WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE ||
            observation->pair_mode ==
                WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST) {
            increment_saturated_u64(next.pressure_observations);
            const std::uint64_t pressure_margin = std::max(
                UINT64_C(1000), next.cadence_us / UINT64_C(8));
            const std::uint64_t pressure_target = std::min(
                config->maximum_delay_us,
                add_saturated_u64(
                    add_saturated_u64(config->baseline_delay_us,
                                      next.jitter_us),
                    add_saturated_u64(
                        observation->pair_extrapolation_us,
                        pressure_margin)));
            if (pressure_target > target_delay) {
                target_delay = pressure_target;
                pressure_rise = true;
            }
        } else if (observation->pair_mode ==
                   WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_EARLIEST) {
            recovery_signal = true;
        }
    }

    if (target_delay > next.current_delay_us) {
        next.current_delay_us = target_delay;
        increment_saturated_u64(next.rise_adjustments);
        next.last_adjustment = pressure_rise
            ? CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_PRESSURE_RISE
            : CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_JITTER_RISE;
    } else if (target_delay < next.current_delay_us && recovery_signal) {
        const std::uint64_t recovery_step = std::max(
            UINT64_C(1000), next.cadence_us / UINT64_C(16));
        const std::uint64_t recoverable =
            next.current_delay_us - target_delay;
        next.current_delay_us -= std::min(recoverable, recovery_step);
        increment_saturated_u64(next.recovery_adjustments);
        next.last_adjustment =
            CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_RECOVERY;
    }

    *state = next;
    interpolation_delay_status(next, status_out);
    return CG_CANONICAL_INTERPOLATION_DELAY_OK;
}

void CG_ResetCanonicalInterpolationDelayV1(
    cg_canonical_interpolation_delay_state_v1 *state)
{
    if (state)
        *state = {};
}

cg_canonical_native_authority_phase_v1
CG_ResolveCanonicalNativeAuthorityV1(
    cg_canonical_native_authority_state_v1 *state, bool native_requested,
    bool ownership_present)
{
    if (!state)
        return CG_CANONICAL_NATIVE_AUTHORITY_INVALID;

    if (!native_requested) {
        state->ownership_latched = 0u;
        return CG_CANONICAL_NATIVE_AUTHORITY_INACTIVE;
    }

    if (!state->ownership_latched) {
        if (!ownership_present)
            return CG_CANONICAL_NATIVE_AUTHORITY_PREBIND_LEGACY;
        state->ownership_latched = 1u;
        return CG_CANONICAL_NATIVE_AUTHORITY_FIRST_BIND;
    }

    return ownership_present ? CG_CANONICAL_NATIVE_AUTHORITY_OWNED
                             : CG_CANONICAL_NATIVE_AUTHORITY_POST_BIND_LOSS;
}

void CG_ResetCanonicalNativeAuthorityV1(
    cg_canonical_native_authority_state_v1 *state)
{
    if (state)
        *state = {};
}
