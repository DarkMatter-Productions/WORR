/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "cg_canonical_entity_adapter.hpp"
#include "common/net/snapshot_timeline.h"

#include <cstdint>

constexpr std::uint32_t CG_CANONICAL_RENDER_SNAPSHOT_ENTITY_CAPACITY = 512u;
constexpr std::uint32_t CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY =
    CG_CANONICAL_RENDER_SNAPSHOT_ENTITY_CAPACITY * 2u;

enum cg_canonical_render_entity_flags_v1 : std::uint32_t {
    CG_CANONICAL_RENDER_ENTITY_CURRENT = 1u << 0,
    CG_CANONICAL_RENDER_ENTITY_PREVIOUS = 1u << 1,
    CG_CANONICAL_RENDER_ENTITY_GENERATION_CONTINUOUS = 1u << 2,
};

enum cg_canonical_render_entities_result_v1 : std::uint32_t {
    CG_CANONICAL_RENDER_ENTITIES_OK = 0,
    CG_CANONICAL_RENDER_ENTITIES_INVALID_ARGUMENT = 1,
    CG_CANONICAL_RENDER_ENTITIES_INVALID_LIMITS = 2,
    CG_CANONICAL_RENDER_ENTITIES_COUNT_LIMIT = 3,
    CG_CANONICAL_RENDER_ENTITIES_OUT_OF_ORDER = 4,
    CG_CANONICAL_RENDER_ENTITIES_INVALID_ENTITY = 5,
    CG_CANONICAL_RENDER_ENTITIES_OUTPUT_TOO_SMALL = 6,
    CG_CANONICAL_RENDER_ENTITIES_OVERLAP = 7,
};

enum cg_canonical_render_sample_result_v1 : std::uint32_t {
    CG_CANONICAL_RENDER_SAMPLE_OK = 0,
    CG_CANONICAL_RENDER_SAMPLE_INVALID_ARGUMENT = 1,
    CG_CANONICAL_RENDER_SAMPLE_INVALID_RECORD = 2,
    CG_CANONICAL_RENDER_SAMPLE_INVALID_SAMPLE = 3,
    CG_CANONICAL_RENDER_SAMPLE_UNSAFE = 4,
    CG_CANONICAL_RENDER_SAMPLE_ADAPTER_REJECTED = 5,
};

enum cg_canonical_render_lifecycle_result_v1 : std::uint32_t {
    CG_CANONICAL_RENDER_LIFECYCLE_OK = 0,
    CG_CANONICAL_RENDER_LIFECYCLE_INVALID_ARGUMENT = 1,
    CG_CANONICAL_RENDER_LIFECYCLE_INVALID_INPUT = 2,
};

enum cg_canonical_render_clock_plan_result_v1 : std::uint32_t {
    CG_CANONICAL_RENDER_CLOCK_PLAN_OK = 0,
    CG_CANONICAL_RENDER_CLOCK_PLAN_INVALID_ARGUMENT = 1,
    CG_CANONICAL_RENDER_CLOCK_PLAN_INVALID_STATE = 2,
    CG_CANONICAL_RENDER_CLOCK_PLAN_INVALID_RATE = 3,
};

enum cg_canonical_legacy_alignment_result_v1 : std::uint32_t {
    CG_CANONICAL_LEGACY_ALIGNMENT_OK = 0,
    CG_CANONICAL_LEGACY_ALIGNMENT_CLAMPED_FUTURE = 1,
    CG_CANONICAL_LEGACY_ALIGNMENT_PAST_LIMIT = 2,
    CG_CANONICAL_LEGACY_ALIGNMENT_INVALID_ARGUMENT = 3,
};

enum cg_canonical_host_time_result_v1 : std::uint32_t {
    CG_CANONICAL_HOST_TIME_OK = 0,
    CG_CANONICAL_HOST_TIME_INVALID_ARGUMENT = 1,
    CG_CANONICAL_HOST_TIME_OVERFLOW = 2,
};

enum cg_canonical_interpolation_delay_result_v1 : std::uint32_t {
    CG_CANONICAL_INTERPOLATION_DELAY_OK = 0,
    CG_CANONICAL_INTERPOLATION_DELAY_INVALID_ARGUMENT = 1,
    CG_CANONICAL_INTERPOLATION_DELAY_INVALID_CONFIG = 2,
    CG_CANONICAL_INTERPOLATION_DELAY_INVALID_OBSERVATION = 3,
};

enum cg_canonical_interpolation_delay_adjustment_v1 : std::uint32_t {
    CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_NONE = 0,
    CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_RESET = 1,
    CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_FIXED = 2,
    CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_JITTER_RISE = 3,
    CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_PRESSURE_RISE = 4,
    CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_RECOVERY = 5,
};

/*
 * Resolves the one-way ownership edge for native snapshot presentation.
 * Requesting native authority before the engine publishes ownership remains
 * legacy-authoritative.  Once ownership is observed, later loss is a native
 * authority fault and cannot silently reopen the legacy pre-bind path.
 */
enum cg_canonical_native_authority_phase_v1 : std::uint32_t {
    CG_CANONICAL_NATIVE_AUTHORITY_INVALID = 0,
    CG_CANONICAL_NATIVE_AUTHORITY_INACTIVE = 1,
    CG_CANONICAL_NATIVE_AUTHORITY_PREBIND_LEGACY = 2,
    CG_CANONICAL_NATIVE_AUTHORITY_FIRST_BIND = 3,
    CG_CANONICAL_NATIVE_AUTHORITY_OWNED = 4,
    CG_CANONICAL_NATIVE_AUTHORITY_POST_BIND_LOSS = 5,
};

struct cg_canonical_native_authority_state_v1 {
    std::uint32_t ownership_latched;
};

/*
 * One value-owned member of the previous/current identity union.  A missing
 * current state identifies a removal.  A missing or discontinuous previous
 * state leaves previous as a safe copy of current for callers that need a
 * no-blend fallback; GENERATION_CONTINUOUS is the only permission to consume
 * the copied previous lifecycle as the same entity.
 */
struct cg_canonical_render_entity_v1 {
    std::uint32_t identity;
    std::uint32_t current_generation;
    std::uint32_t previous_generation;
    std::uint32_t current_provenance;
    std::uint32_t previous_provenance;
    std::uint32_t flags;
    entity_state_t current;
    entity_state_t previous;
};

/*
 * One validated presentation choice for a visible timeline sample.  Timeline
 * interpolation deliberately reports non-spatial discrete/component changes
 * while retaining a useful hybrid sample: discrete fields stay on the proper
 * endpoint while transforms remain smooth.  Native presentation preserves
 * that hybrid, but model changes and the legacy exact per-axis 512-unit
 * teleport rule select a direct endpoint.  The selector is value-only and
 * transactional so production and tests share that authority decision.
 */
struct cg_canonical_render_sample_v1 {
    entity_state_t state;
    std::uint32_t generation;
    std::uint32_t provenance;
    std::uint32_t blocking_reasons;
    std::uint32_t direct_endpoint;
};

/*
 * Cumulative previous-only presentation evidence is ordered by construction:
 * a renderer submission must first have been selected as visible, and a
 * visible previous-only selection must first have been observed in the
 * previous/current identity union.  Keep the predicate shared by production
 * assertions and focused tests so counter saturation cannot weaken the
 * relationship silently.
 */
bool CG_CanonicalPreviousOnlyEvidenceOrderedV1(
    std::uint64_t observed_removed_identities,
    std::uint64_t selected_visible_sources,
    std::uint64_t renderer_submitted_sources);

/*
 * Resolves the mutable centity lifecycle edge around one immutable sampled
 * state.  Motion/snapshot discontinuities sampled on their previous endpoint
 * arm a one-shot fence; the first sample on the other side consumes that
 * fence and restarts trail/animation/audio identity even when both endpoint
 * transforms happen to be byte-equivalent.  A current-side discontinuity is
 * likewise restarted once per exact retained-endpoint key, so identical
 * repeated samples do not churn lifecycle identity.  Component, discrete-
 * field and extrapolation-policy notices remain presentation-continuous by
 * themselves.
 */
struct cg_canonical_render_lifecycle_decision_v1 {
    std::uint32_t reset;
    std::uint32_t pending_blocking_reasons;
    std::uint64_t pending_discontinuity_key;
    std::uint64_t handled_discontinuity_key;
};

struct cg_canonical_render_clock_plan_v1 {
    std::uint32_t request_count;
    std::uint32_t reserved0;
    worr_snapshot_timeline_clock_request_v1 requests[2];
};

struct cg_canonical_host_time_extender_v1 {
    std::uint64_t extended_ms;
    std::uint32_t last_sample_ms;
    std::uint32_t initialized;
};

/*
 * Pure integer interpolation-delay controller.  Snapshot observations are
 * admitted only when accepted_snapshot_count advances; render feedback is
 * limited to the preceding canonical pair's extrapolation/clamp result.
 * stream_reset_count is the timeline reset serial, so same-epoch demo seeks
 * reset the controller just as map/epoch transitions do.
 */
struct cg_canonical_interpolation_delay_config_v1 {
    std::uint64_t baseline_delay_us;
    std::uint64_t maximum_delay_us;
    std::uint32_t adaptive_enabled;
    std::uint32_t reserved0;
};

struct cg_canonical_interpolation_delay_observation_v1 {
    std::uint32_t epoch;
    std::uint32_t snapshot_valid;
    std::uint32_t pair_valid;
    std::uint32_t pair_mode;
    std::uint64_t stream_reset_count;
    std::uint64_t accepted_snapshot_count;
    std::uint64_t snapshot_server_time_us;
    std::uint64_t snapshot_receive_time_us;
    std::uint64_t pair_extrapolation_us;
};

struct cg_canonical_interpolation_delay_state_v1 {
    std::uint32_t initialized;
    std::uint32_t epoch;
    std::uint32_t adaptive_enabled;
    std::uint32_t snapshot_anchor_valid;
    std::uint64_t stream_reset_count;
    std::uint64_t baseline_delay_us;
    std::uint64_t maximum_delay_us;
    std::uint64_t current_delay_us;
    std::uint64_t last_accepted_snapshot_count;
    std::uint64_t last_snapshot_server_time_us;
    std::uint64_t last_snapshot_receive_time_us;
    std::uint64_t cadence_us;
    std::uint64_t jitter_us;
    std::uint64_t last_jitter_us;
    std::uint64_t interval_observations;
    std::uint64_t rise_adjustments;
    std::uint64_t recovery_adjustments;
    std::uint64_t pressure_observations;
    std::uint64_t reset_count;
    std::uint32_t last_adjustment;
    std::uint32_t reserved0;
};

struct cg_canonical_interpolation_delay_status_v1 {
    std::uint64_t delay_us;
    std::uint64_t baseline_delay_us;
    std::uint64_t maximum_delay_us;
    std::uint64_t cadence_us;
    std::uint64_t jitter_us;
    std::uint64_t last_jitter_us;
    std::uint64_t interval_observations;
    std::uint64_t rise_adjustments;
    std::uint64_t recovery_adjustments;
    std::uint64_t pressure_observations;
    std::uint64_t reset_count;
    std::uint32_t adaptive_enabled;
    std::uint32_t last_adjustment;
};

/*
 * Builds a sorted, pointer-free identity union from two immutable canonical
 * ranges.  Both inputs must be strictly ordered by entity identity.  The
 * function validates every entity through the canonical adapter before it
 * writes anything; all failures leave records_out and count_out byte-for-byte
 * unchanged.  Input and output ranges must be disjoint.
 */
cg_canonical_render_entities_result_v1 CG_BuildCanonicalRenderEntitiesV1(
    const worr_snapshot_entity_v2 *previous_entities,
    std::uint32_t previous_count,
    const worr_snapshot_entity_v2 *current_entities,
    std::uint32_t current_count,
    cg_canonical_entity_adapter_limits_v1 limits,
    cg_canonical_render_entity_v1 *records_out,
    std::uint32_t record_capacity,
    std::uint32_t *count_out);

cg_canonical_render_sample_result_v1 CG_SelectCanonicalRenderSampleV1(
    const cg_canonical_render_entity_v1 *record,
    const worr_snapshot_timeline_entity_sample_v1 *sample,
    cg_canonical_entity_adapter_limits_v1 limits,
    cg_canonical_render_sample_v1 *selection_out);

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
    cg_canonical_render_lifecycle_decision_v1 *decision_out);

std::uint64_t CG_CanonicalRenderEndpointKeyV1(
    worr_snapshot_timeline_ref_v1 endpoint);

/*
 * Plans the minimum ordered clock operations for one render frame.  SET_RATE
 * advances at the old rate before installing the new one, so a simultaneous
 * pause/resume transition is emitted second at the same monotonic host time.
 */
cg_canonical_render_clock_plan_result_v1
CG_BuildCanonicalRenderClockPlanV1(
    const worr_snapshot_timeline_clock_state_v1 *clock,
    std::uint64_t host_time_us, std::uint32_t rate_q16, bool should_pause,
    cg_canonical_render_clock_plan_v1 *plan_out);

/* Legacy presentation time remains authoritative, but an independently
 * advancing canonical host clock can briefly lag it after a process hitch.
 * Resolve future skew conservatively to the current canonical time so event
 * authority keeps moving; reject a legacy time older than the admitted delay
 * because advancing it would present ahead of the visible legacy frame. */
cg_canonical_legacy_alignment_result_v1
CG_ResolveCanonicalLegacyAlignmentV1(
    std::uint64_t clock_time_us, std::uint64_t desired_time_us,
    std::uint64_t maximum_delay_us, std::uint64_t *resolved_time_us);

cg_canonical_host_time_result_v1 CG_ExtendCanonicalHostTimeV1(
    cg_canonical_host_time_extender_v1 *state, std::uint32_t sample_ms,
    std::uint64_t *extended_ms_out);

cg_canonical_interpolation_delay_result_v1
CG_UpdateCanonicalInterpolationDelayV1(
    cg_canonical_interpolation_delay_state_v1 *state,
    const cg_canonical_interpolation_delay_config_v1 *config,
    const cg_canonical_interpolation_delay_observation_v1 *observation,
    cg_canonical_interpolation_delay_status_v1 *status_out);

void CG_ResetCanonicalInterpolationDelayV1(
    cg_canonical_interpolation_delay_state_v1 *state);

cg_canonical_native_authority_phase_v1
CG_ResolveCanonicalNativeAuthorityV1(
    cg_canonical_native_authority_state_v1 *state, bool native_requested,
    bool ownership_present);

void CG_ResetCanonicalNativeAuthorityV1(
    cg_canonical_native_authority_state_v1 *state);
