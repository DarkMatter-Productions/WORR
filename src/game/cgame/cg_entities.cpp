/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
// cl_ents.c -- entity parsing and management

#include "cg_entity_local.h"
#include "cg_canonical_render_entities.hpp"
#include "cg_canonical_snapshot_timeline.hpp"
#include "cg_event_runtime.hpp"
#include "cg_event_shadow.hpp"
#include "cg_snapshot_timeline.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

extern qhandle_t cl_mod_powerscreen;
extern qhandle_t cl_mod_laser;
extern qhandle_t cl_mod_dmspot;
extern qhandle_t cl_img_flare;
float CG_Wheel_TimeScale(void);

static cvar_t *cl_brightskins_custom;
static cvar_t *cl_brightskins_enemy_color;
static cvar_t *cl_brightskins_team_color;
static cvar_t *cl_brightskins_dead;
static color_t brightskin_enemy_color = COLOR_RED;
static color_t brightskin_team_color = COLOR_GREEN;
static vec3_t listener_origin;
static vec3_t listener_forward;
static vec3_t listener_right;
static vec3_t listener_up;
static int listener_entnum;

namespace {

/*
 * FR-10-T07 progressive render cutover.
 *
 * Mode 0 keeps legacy rendering authoritative while still advancing the
 * canonical render clock.  Mode 1 performs a live transform parity audit.
 * Mode 2 promotes a sampled transform only after the same parity checks pass.
 * Mode 3 is a stricter source gate with an independent time policy: it
 * remains legacy-authoritative while waiting for the engine's first sole
 * native timeline ownership bind, then renders behind the canonical clock by
 * an explicit interpolation delay and permits only the configured bounded
 * extrapolation interval.  Once bound, native authority enumerates a
 * value-owned canonical identity union and fails closed on ownership loss
 * instead of falling back to mutable legacy frame storage.
 */
enum canonical_snapshot_render_mode_t {
    CANONICAL_SNAPSHOT_RENDER_LEGACY = 0,
    CANONICAL_SNAPSHOT_RENDER_AUDIT = 1,
    CANONICAL_SNAPSHOT_RENDER_PROMOTE = 2,
    CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY = 3,
};

struct canonical_snapshot_render_stats_t {
    std::uint32_t epoch;
    std::uint32_t last_clock_result;
    std::uint32_t last_pair_result;
    std::uint32_t last_pair_mode;
    std::uint32_t last_pair_blocks;
    std::uint64_t clock_frames;
    std::uint64_t clock_failures;
    std::uint64_t pair_frames;
    std::uint64_t pair_failures;
    std::uint64_t alignment_failures;
    std::uint64_t interpolation_frames;
    std::uint64_t extrapolation_frames;
    std::uint64_t extrapolation_time_us;
    std::uint64_t clamped_frames;
    std::uint64_t adaptive_delay_failures;
    std::uint64_t enumeration_frames;
    std::uint64_t enumeration_failures;
    std::uint64_t enumerated_current_entities;
    std::uint64_t enumerated_removed_entities;
    std::uint64_t selected_visible_previous_only_sources;
    std::uint64_t enumeration_generation_resets;
    std::uint64_t renderer_submission_calls;
    std::uint64_t renderer_submitted_sources;
    std::uint64_t renderer_previous_only_submitted_sources;
    std::uint64_t renderer_submission_hash;
    std::uint64_t sample_attempts;
    std::uint64_t sample_failures;
    std::uint64_t sample_invisible;
    std::uint64_t sample_discontinuities;
    std::uint64_t parity_matches;
    std::uint64_t parity_mismatches;
    std::uint64_t native_authority_samples;
    std::uint64_t native_authority_blocks;
    std::uint64_t promoted_transforms;
    std::uint64_t event_ready_records;
    std::uint64_t event_future_frames;
    std::uint64_t event_audit_failures;
    float max_origin_error;
    float max_old_origin_error;
    float max_angle_error;
    unsigned last_debug_time_ms;
};

struct canonical_snapshot_render_frame_t {
    worr_snapshot_timeline_policy_v1 policy;
    worr_snapshot_timeline_pair_v1 pair;
    std::uint32_t requested_mode;
    std::uint32_t mode;
    std::uint32_t prepared_realtime_ms;
    bool pair_valid;
    bool prepared;
};

struct canonical_interpolation_pair_feedback_t {
    std::uint32_t valid;
    std::uint32_t mode;
    std::uint64_t extrapolation_us;
};

static_assert(CG_CANONICAL_RENDER_SNAPSHOT_ENTITY_CAPACITY ==
                  CG_CANONICAL_SNAPSHOT_TIMELINE_ENTITY_CAPACITY,
              "native render copy capacity must cover one timeline slot");

struct canonical_native_render_cache_entry_t {
    centity_t entity;
    std::uint32_t generation;
    std::uint32_t provenance;
    std::uint32_t pending_discontinuity_blocks;
    std::uint64_t pending_discontinuity_key;
    std::uint64_t handled_discontinuity_key;
    bool valid;
};

enum canonical_native_render_result_t : std::uint32_t {
    CANONICAL_NATIVE_RENDER_UNINITIALIZED = 0,
    CANONICAL_NATIVE_RENDER_OK = 1,
    CANONICAL_NATIVE_RENDER_INVALID_LIMITS = 2,
    CANONICAL_NATIVE_RENDER_COPY_PREVIOUS = 3,
    CANONICAL_NATIVE_RENDER_COPY_CURRENT = 4,
    CANONICAL_NATIVE_RENDER_COPY_PLAYER = 5,
    CANONICAL_NATIVE_RENDER_BUILD_UNION = 6,
    CANONICAL_NATIVE_RENDER_SAMPLE_ENTITY = 7,
    CANONICAL_NATIVE_RENDER_ADAPT_ENTITY = 8,
    CANONICAL_NATIVE_RENDER_ID_EXHAUSTED = 9,
};

struct canonical_native_render_view_t {
    std::array<worr_snapshot_entity_v2,
               CG_CANONICAL_RENDER_SNAPSHOT_ENTITY_CAPACITY>
        previous_entities;
    std::array<worr_snapshot_entity_v2,
               CG_CANONICAL_RENDER_SNAPSHOT_ENTITY_CAPACITY>
        current_entities;
    std::array<cg_canonical_render_entity_v1,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        records;
    std::array<std::uint16_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_identities;
    std::array<std::uint32_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_generations;
    std::array<std::uint32_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_provenances;
    std::array<std::uint32_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_blocks;
    std::array<std::uint32_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_modes;
    std::array<std::uint8_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_previous_only;
    std::array<std::uint32_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_pending_discontinuity_blocks;
    std::array<std::uint64_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_pending_discontinuity_keys;
    std::array<std::uint64_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_handled_discontinuity_keys;
    std::array<std::uint8_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_lifecycle_resets;
    std::array<entity_state_t,
               CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        render_states;
    std::array<canonical_native_render_cache_entry_t,
               CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES>
        cache;
    std::uint32_t record_count;
    std::uint32_t current_count;
    std::uint32_t render_count;
    std::uint32_t removed_count;
    std::uint32_t selected_previous_only_count;
    std::uint32_t controlled_entity;
    std::uint8_t team_id;
    std::int32_t movement_type;
    std::uint32_t last_result;
    int next_entity_id;
    bool valid;
    bool authority_latched;
};

cvar_t *cg_snapshot_timeline_render;
cvar_t *cg_snapshot_timeline_render_epsilon;
cvar_t *cg_snapshot_timeline_interpolation_delay_ms;
cvar_t *cg_snapshot_timeline_adaptive_interpolation;
cvar_t *cg_snapshot_timeline_max_interpolation_delay_ms;
cvar_t *cg_snapshot_timeline_max_extrapolation_ms;
cvar_t *cg_event_runtime_audit;
cvar_t *cl_worr_native_event_presentation_owned;
cvar_t *cl_worr_native_snapshot_timeline_owned;
canonical_snapshot_render_stats_t canonical_render_stats;
canonical_snapshot_render_frame_t canonical_render_frame;
canonical_native_render_view_t canonical_native_render_view;
cg_canonical_host_time_extender_v1 canonical_host_time_extender;
cg_canonical_native_authority_state_v1 canonical_native_authority_state;
cg_canonical_interpolation_delay_state_v1 canonical_interpolation_delay_state;
cg_canonical_interpolation_delay_status_v1 canonical_interpolation_delay_status;
canonical_interpolation_pair_feedback_t canonical_interpolation_pair_feedback;

int effective_canonical_snapshot_render_mode()
{
    const int configured_mode = cg_snapshot_timeline_render
        ? Q_clip(cg_snapshot_timeline_render->integer,
                 static_cast<int>(CANONICAL_SNAPSHOT_RENDER_LEGACY),
                 static_cast<int>(
                     CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY))
        : CANONICAL_SNAPSHOT_RENDER_LEGACY;

    /* Legacy demos do not carry the canonical snapshot timeline.  Select one
     * authority for the entire playback/seek stream instead of entering mode
     * 3 and producing an entity-empty frame when live timeline ownership is
     * intentionally revoked. */
    if (configured_mode == CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY &&
        (cls.demo.playback || cls.demo.seeking)) {
        return CANONICAL_SNAPSHOT_RENDER_LEGACY;
    }
    return configured_mode;
}

void event_runtime_audit_changed(cvar_t *self)
{
    CG_EventRuntimeSetAuditEnabled(self && self->integer != 0);
}

void increment_saturated(std::uint64_t &value)
{
    if (value != UINT64_MAX)
        ++value;
}

void add_saturated(std::uint64_t &value, std::uint64_t amount)
{
    if (value > UINT64_MAX - amount)
        value = UINT64_MAX;
    else
        value += amount;
}

float maximum_component_error(const float left[3], const float right[3])
{
    float error = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
        error = max(error, fabsf(left[axis] - right[axis]));
    return error;
}

float maximum_angle_error(const float left[3], const float right[3])
{
    float error = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        float delta = fmodf(right[axis] - left[axis], 360.0f);
        if (delta < -180.0f)
            delta += 360.0f;
        else if (delta >= 180.0f)
            delta -= 360.0f;
        error = max(error, fabsf(delta));
    }
    return error;
}

void reset_canonical_render_stats(std::uint32_t epoch)
{
    canonical_render_stats = {};
    canonical_render_stats.epoch = epoch;
    canonical_render_stats.renderer_submission_hash =
        UINT64_C(1469598103934665603);
    canonical_render_stats.last_debug_time_ms = cls.realtime;
}

void canonical_submission_hash_u32(std::uint32_t value)
{
    for (unsigned byte = 0; byte < 4u; ++byte) {
        canonical_render_stats.renderer_submission_hash ^=
            static_cast<std::uint8_t>(value >> (byte * 8u));
        canonical_render_stats.renderer_submission_hash *=
            UINT64_C(1099511628211);
    }
}

void canonical_submission_hash_u64(std::uint64_t value)
{
    canonical_submission_hash_u32(static_cast<std::uint32_t>(value));
    canonical_submission_hash_u32(
        static_cast<std::uint32_t>(value >> 32u));
}

void record_canonical_renderer_submission(
    const canonical_native_render_cache_entry_t &entry,
    const entity_state_t &state, const entity_t &render_entity,
    bool previous_only_source, bool &source_already_submitted)
{
    increment_saturated(canonical_render_stats.renderer_submission_calls);
    if (!source_already_submitted) {
        increment_saturated(
            canonical_render_stats.renderer_submitted_sources);
        if (previous_only_source) {
            increment_saturated(
                canonical_render_stats
                    .renderer_previous_only_submitted_sources);
        }
        source_already_submitted = true;
        Q_assert(CG_CanonicalPreviousOnlyEvidenceOrderedV1(
            canonical_render_stats.enumerated_removed_entities,
            canonical_render_stats.selected_visible_previous_only_sources,
            canonical_render_stats
                .renderer_previous_only_submitted_sources));
    }
    canonical_submission_hash_u32(static_cast<std::uint32_t>(state.number));
    canonical_submission_hash_u32(entry.generation);
    canonical_submission_hash_u32(entry.provenance);
    canonical_submission_hash_u64(static_cast<std::uint64_t>(state.effects));
    canonical_submission_hash_u64(static_cast<std::uint64_t>(state.renderfx));
    canonical_submission_hash_u32(static_cast<std::uint32_t>(state.frame));
    canonical_submission_hash_u32(
        static_cast<std::uint32_t>(render_entity.id));
    canonical_submission_hash_u32(
        static_cast<std::uint32_t>(render_entity.owner_entity));
    for (unsigned axis = 0; axis < 3u; ++axis) {
        canonical_submission_hash_u32(
            std::bit_cast<std::uint32_t>(render_entity.origin[axis]));
        canonical_submission_hash_u32(
            std::bit_cast<std::uint32_t>(render_entity.angles[axis]));
    }
}

void reset_canonical_native_render_view()
{
    for (auto &entry : canonical_native_render_view.cache)
        entry = {};
    canonical_native_render_view.render_previous_only.fill(0u);
    canonical_native_render_view.record_count = 0;
    canonical_native_render_view.current_count = 0;
    canonical_native_render_view.render_count = 0;
    canonical_native_render_view.removed_count = 0;
    canonical_native_render_view.selected_previous_only_count = 0;
    canonical_native_render_view.controlled_entity = 0;
    canonical_native_render_view.team_id = 0;
    canonical_native_render_view.movement_type = 0;
    canonical_native_render_view.last_result =
        CANONICAL_NATIVE_RENDER_UNINITIALIZED;
    // Keep render lifecycle IDs process-monotonic across epoch, clock and
    // ownership resets. Persistent audio tokens include this ID, so resetting
    // it could make a recovered entity indistinguishable from an old channel.
    canonical_native_render_view.valid = false;
    canonical_native_render_view.authority_latched = false;
}

bool fail_canonical_native_render_view(canonical_native_render_result_t result)
{
    canonical_native_render_view.render_previous_only.fill(0u);
    canonical_native_render_view.record_count = 0;
    canonical_native_render_view.current_count = 0;
    canonical_native_render_view.render_count = 0;
    canonical_native_render_view.removed_count = 0;
    canonical_native_render_view.selected_previous_only_count = 0;
    canonical_native_render_view.controlled_entity = 0;
    canonical_native_render_view.team_id = 0;
    canonical_native_render_view.movement_type = 0;
    canonical_native_render_view.last_result = result;
    canonical_native_render_view.valid = false;
    increment_saturated(canonical_render_stats.enumeration_failures);
    return false;
}

void canonical_native_update_bounds(centity_t &entity)
{
    if (entity.current.solid && entity.current.solid != PACKED_BSP) {
        cgei->Q2Proto_UnpackSolid(entity.current.solid, entity.mins,
                                 entity.maxs);
        entity.radius = Distance(entity.maxs, entity.mins) * 0.5f;
    } else {
        VectorClear(entity.mins);
        VectorClear(entity.maxs);
        entity.radius = 0.0f;
    }
}

void canonical_native_initialize_cache_entry(
    canonical_native_render_cache_entry_t &entry,
    const entity_state_t &state, std::uint32_t generation,
    std::uint32_t provenance)
{
    entry = {};
    centity_t &entity = entry.entity;
    entity.id = ++canonical_native_render_view.next_entity_id;
    entity.current = state;
    entity.prev = state;
    entity.serverframe = cl.frame.number;
    entity.trailcount = 1024;
    entity.flashlightfrac = 1.0f;
    VectorCopy(state.origin, entity.lerp_origin);
#if USE_FPS
    entity.prev_frame = state.frame;
    entity.event_frame = cl.frame.number;
#endif
    entity.current_frame = state.frame;
    entity.last_frame = state.frame;
    entity.frame_servertime = cl.servertime;
    entity.stair_time = cls.realtime;
    canonical_native_update_bounds(entity);
    entry.generation = generation;
    entry.provenance = provenance;
    entry.valid = true;
    increment_saturated(canonical_render_stats.enumeration_generation_resets);
}

void canonical_native_update_cache_entry(
    canonical_native_render_cache_entry_t &entry,
    const entity_state_t &state)
{
    centity_t &entity = entry.entity;
    const entity_state_t previous = entity.current;
#if USE_FPS
    if (state.frame != previous.frame) {
        entity.prev_frame = previous.frame;
        entity.anim_start = cl.servertime - cl.frametime.time;
    }
#endif
    if (entity.current_frame != state.frame) {
        entity.last_frame = entity.current_frame;
        entity.current_frame = state.frame;
        entity.frame_servertime = cl.servertime;
    }
    entity.prev = previous;
    entity.current = state;
    entity.serverframe = cl.frame.number;
    canonical_native_update_bounds(entity);
}

bool prepare_canonical_native_render_view()
{
    auto &view = canonical_native_render_view;
    view.valid = false;
    view.record_count = 0;
    view.current_count = 0;
    view.render_count = 0;
    view.removed_count = 0;
    view.selected_previous_only_count = 0;
    view.render_previous_only.fill(0u);
    increment_saturated(canonical_render_stats.enumeration_frames);

    if (!cgei || !cgei->Q2Proto_UnpackSolid || cl.csr.max_edicts <= 1 ||
        cl.csr.max_edicts > CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES ||
        cl.csr.max_models <= 0 || cl.csr.max_models > MAX_MODELS ||
        cl.csr.max_sounds <= 0 || cl.csr.max_sounds > MAX_SOUNDS) {
        return fail_canonical_native_render_view(
            CANONICAL_NATIVE_RENDER_INVALID_LIMITS);
    }

    std::uint32_t previous_count = 0;
    if (canonical_render_frame.pair.previous.slot !=
        WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT) {
        const auto result = CG_CanonicalSnapshotTimelineCopyEntities(
            canonical_render_frame.pair.previous,
            view.previous_entities.data(),
            static_cast<std::uint32_t>(view.previous_entities.size()),
            &previous_count);
        if (result != WORR_SNAPSHOT_TIMELINE_OK) {
            return fail_canonical_native_render_view(
                CANONICAL_NATIVE_RENDER_COPY_PREVIOUS);
        }
    }

    std::uint32_t current_count = 0;
    if (CG_CanonicalSnapshotTimelineCopyEntities(
            canonical_render_frame.pair.current,
            view.current_entities.data(),
            static_cast<std::uint32_t>(view.current_entities.size()),
            &current_count) != WORR_SNAPSHOT_TIMELINE_OK) {
        return fail_canonical_native_render_view(
            CANONICAL_NATIVE_RENDER_COPY_CURRENT);
    }

    worr_snapshot_player_v2 player{};
    if (CG_CanonicalSnapshotTimelineCopyPlayer(
            canonical_render_frame.pair.current, &player) !=
        WORR_SNAPSHOT_TIMELINE_OK) {
        return fail_canonical_native_render_view(
            CANONICAL_NATIVE_RENDER_COPY_PLAYER);
    }

    const cg_canonical_entity_adapter_limits_v1 limits = {
        static_cast<std::uint32_t>(cl.csr.max_edicts),
        static_cast<std::uint32_t>(cl.csr.max_models),
        static_cast<std::uint32_t>(cl.csr.max_sounds),
    };
    std::uint32_t record_count = 0;
    if (CG_BuildCanonicalRenderEntitiesV1(
            view.previous_entities.data(), previous_count,
            view.current_entities.data(), current_count, limits,
            view.records.data(),
            static_cast<std::uint32_t>(view.records.size()),
            &record_count) != CG_CANONICAL_RENDER_ENTITIES_OK) {
        return fail_canonical_native_render_view(
            CANONICAL_NATIVE_RENDER_BUILD_UNION);
    }
    const std::uint64_t discontinuity_endpoint_key =
        CG_CanonicalRenderEndpointKeyV1(
            canonical_render_frame.pair.current);
    if (discontinuity_endpoint_key == 0u) {
        return fail_canonical_native_render_view(
            CANONICAL_NATIVE_RENDER_ADAPT_ENTITY);
    }

    std::uint32_t render_count = 0;
    std::uint32_t removed_count = 0;
    std::uint32_t selected_previous_only_count = 0;
    for (std::uint32_t record_index = 0; record_index < record_count;
         ++record_index) {
        const auto &record = view.records[record_index];
        const bool previous_only =
            (record.flags & CG_CANONICAL_RENDER_ENTITY_PREVIOUS) != 0u &&
            (record.flags & CG_CANONICAL_RENDER_ENTITY_CURRENT) == 0u;
        if (previous_only) {
            ++removed_count;
        }

        increment_saturated(canonical_render_stats.sample_attempts);
        worr_snapshot_timeline_entity_sample_v1 sample{};
        const auto sample_result = CG_CanonicalSnapshotTimelineSampleEntity(
            &canonical_render_frame.policy, &canonical_render_frame.pair,
            record.identity, &sample);
        if (sample_result != WORR_SNAPSHOT_TIMELINE_OK) {
            increment_saturated(canonical_render_stats.sample_failures);
            return fail_canonical_native_render_view(
                CANONICAL_NATIVE_RENDER_SAMPLE_ENTITY);
        }
        if (!sample.visible) {
            increment_saturated(canonical_render_stats.sample_invisible);
            continue;
        }
        if (sample.blocking_reasons != 0u)
            increment_saturated(canonical_render_stats.sample_discontinuities);

        cg_canonical_render_sample_v1 selected{};
        if (CG_SelectCanonicalRenderSampleV1(&record, &sample, limits,
                                             &selected) !=
            CG_CANONICAL_RENDER_SAMPLE_OK) {
            return fail_canonical_native_render_view(
                CANONICAL_NATIVE_RENDER_ADAPT_ENTITY);
        }
        view.render_identities[render_count] =
            static_cast<std::uint16_t>(record.identity);
        view.render_generations[render_count] =
            selected.generation;
        view.render_provenances[render_count] = selected.provenance;
        view.render_blocks[render_count] = sample.blocking_reasons;
        view.render_modes[render_count] = sample.mode;
        view.render_previous_only[render_count] =
            previous_only ? 1u : 0u;
        view.render_states[render_count] = selected.state;
        if (previous_only)
            ++selected_previous_only_count;
        ++render_count;
    }

    std::uint32_t resets = 0;
    for (std::uint32_t index = 0; index < render_count; ++index) {
        const std::uint32_t identity = view.render_identities[index];
        const auto &entry = view.cache[identity];
        const bool cache_identity_continuous = entry.valid &&
            entry.generation == view.render_generations[index] &&
            entry.provenance == view.render_provenances[index];
        const entity_state_t &cached_state = cache_identity_continuous
            ? entry.entity.current
            : view.render_states[index];
        cg_canonical_render_lifecycle_decision_v1 lifecycle{};
        if (CG_ResolveCanonicalRenderLifecycleV1(
                &cached_state, &view.render_states[index],
                cache_identity_continuous
                    ? entry.pending_discontinuity_blocks
                    : 0u,
                cache_identity_continuous
                    ? entry.pending_discontinuity_key
                    : 0u,
                cache_identity_continuous
                    ? entry.handled_discontinuity_key
                    : 0u,
                view.render_modes[index], view.render_blocks[index],
                discontinuity_endpoint_key,
                &lifecycle) != CG_CANONICAL_RENDER_LIFECYCLE_OK) {
            return fail_canonical_native_render_view(
                CANONICAL_NATIVE_RENDER_ADAPT_ENTITY);
        }
        const bool reset =
            !cache_identity_continuous || lifecycle.reset != 0u;
        view.render_pending_discontinuity_blocks[index] =
            lifecycle.pending_blocking_reasons;
        view.render_pending_discontinuity_keys[index] =
            lifecycle.pending_discontinuity_key;
        view.render_handled_discontinuity_keys[index] =
            lifecycle.handled_discontinuity_key;
        view.render_lifecycle_resets[index] = reset ? 1u : 0u;
        if (reset) {
            ++resets;
        }
    }
    if (resets > static_cast<std::uint32_t>(
                     std::numeric_limits<int>::max() -
                     view.next_entity_id)) {
        return fail_canonical_native_render_view(
            CANONICAL_NATIVE_RENDER_ID_EXHAUSTED);
    }

    std::uint32_t visible_cursor = 0;
    for (std::uint32_t identity = 0;
         identity < static_cast<std::uint32_t>(view.cache.size());
         ++identity) {
        while (visible_cursor < render_count &&
               view.render_identities[visible_cursor] < identity) {
            ++visible_cursor;
        }
        if (visible_cursor >= render_count ||
            view.render_identities[visible_cursor] != identity) {
            view.cache[identity].valid = false;
        }
    }

    for (std::uint32_t index = 0; index < render_count; ++index) {
        const std::uint32_t identity = view.render_identities[index];
        auto &entry = view.cache[identity];
        const bool reset = view.render_lifecycle_resets[index] != 0u;
        if (reset) {
            canonical_native_initialize_cache_entry(
                entry, view.render_states[index],
                view.render_generations[index],
                view.render_provenances[index]);
        } else {
            canonical_native_update_cache_entry(
                entry, view.render_states[index]);
        }
        entry.pending_discontinuity_blocks =
            view.render_pending_discontinuity_blocks[index];
        entry.pending_discontinuity_key =
            view.render_pending_discontinuity_keys[index];
        entry.handled_discontinuity_key =
            view.render_handled_discontinuity_keys[index];
    }

    view.record_count = record_count;
    view.current_count = current_count;
    view.render_count = render_count;
    view.removed_count = removed_count;
    view.selected_previous_only_count = selected_previous_only_count;
    view.controlled_entity = player.controlled_entity.identity.index;
    view.team_id = player.team_id;
    view.movement_type = player.movement.movement_type;
    view.last_result = CANONICAL_NATIVE_RENDER_OK;
    view.valid = true;
    add_saturated(canonical_render_stats.enumerated_current_entities,
                  render_count);
    add_saturated(canonical_render_stats.enumerated_removed_entities,
                  removed_count);
    add_saturated(
        canonical_render_stats.selected_visible_previous_only_sources,
        selected_previous_only_count);
    Q_assert(CG_CanonicalPreviousOnlyEvidenceOrderedV1(
        canonical_render_stats.enumerated_removed_entities,
        canonical_render_stats.selected_visible_previous_only_sources,
        canonical_render_stats.renderer_previous_only_submitted_sources));
    add_saturated(canonical_render_stats.native_authority_samples,
                  render_count);
    add_saturated(canonical_render_stats.promoted_transforms,
                  render_count);
    return true;
}

bool canonical_desired_render_time_us(std::uint64_t *time_out)
{
    if (!time_out)
        return false;

    if (cl.frame.canonical_server_time_valid) {
        const std::uint64_t current = cl.frame.canonical_server_time_us;
        if (cl.oldframe.valid && cl.oldframe.canonical_server_time_valid &&
            cl.oldframe.canonical_server_time_us <= current) {
            const std::uint64_t previous =
                cl.oldframe.canonical_server_time_us;
            const double fraction =
                static_cast<double>(Q_clipf(cl.lerpfrac, 0.0f, 1.0f));
            *time_out = previous + static_cast<std::uint64_t>(
                static_cast<double>(current - previous) * fraction);
            return true;
        }
        *time_out = current;
        return true;
    }

    if (cl.time < 0)
        return false;
    *time_out = static_cast<std::uint64_t>(cl.time) * UINT64_C(1000);
    return true;
}

worr_snapshot_timeline_result_v1 advance_canonical_render_clock(
    const cg_canonical_snapshot_timeline_diagnostics_v1 &diagnostics,
    worr_snapshot_timeline_clock_state_v1 *clock_out)
{
    std::uint64_t host_time_us = 0;
    if (cgei && cgei->host_realtime_us) {
        host_time_us = *cgei->host_realtime_us;
    } else {
        std::uint64_t host_time_ms = 0;
        if (CG_ExtendCanonicalHostTimeV1(
                &canonical_host_time_extender, cls.realtime,
                &host_time_ms) != CG_CANONICAL_HOST_TIME_OK ||
            host_time_ms > UINT64_MAX / UINT64_C(1000)) {
            return WORR_SNAPSHOT_TIMELINE_CLOCK_OVERFLOW;
        }
        host_time_us = host_time_ms * UINT64_C(1000);
    }
    const cvar_t *timescale_var = Cvar_FindVar("timescale");
    double effective_rate = timescale_var ? timescale_var->value : 1.0;
    effective_rate *= static_cast<double>(CG_Wheel_TimeScale());
    if (!std::isfinite(effective_rate) || effective_rate <= 0.0)
        effective_rate = 1.0;
    effective_rate = std::min(
        effective_rate,
        static_cast<double>(WORR_SNAPSHOT_TIMELINE_RATE_MAX_Q16) /
            WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16);
    std::uint32_t rate_q16 = static_cast<std::uint32_t>(
        effective_rate * WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16 + 0.5);
    if (rate_q16 == 0u)
        rate_q16 = 1u;

    const cvar_t *paused = CG_SvPausedVar();
    const bool should_pause = paused && paused->integer != 0;
    cg_canonical_render_clock_plan_v1 plan{};
    if (CG_BuildCanonicalRenderClockPlanV1(
            &diagnostics.clock, host_time_us, rate_q16, should_pause,
            &plan) != CG_CANONICAL_RENDER_CLOCK_PLAN_OK) {
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    }

    worr_snapshot_timeline_clock_state_v1 state{};
    for (std::uint32_t index = 0; index < plan.request_count; ++index) {
        const auto result = CG_CanonicalSnapshotTimelineClockApply(
            &plan.requests[index], &state);
        if (result != WORR_SNAPSHOT_TIMELINE_OK)
            return result;
    }
    *clock_out = state;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

std::uint64_t update_canonical_interpolation_delay(
    const cg_canonical_snapshot_timeline_diagnostics_v1 &diagnostics,
    std::uint64_t baseline_us)
{
    const float configured_maximum_ms =
        cg_snapshot_timeline_max_interpolation_delay_ms
            ? Cvar_ClampValue(
                  cg_snapshot_timeline_max_interpolation_delay_ms,
                  0.0f, 1000.0f)
            : 150.0f;
    const std::uint64_t maximum_us = std::max(
        baseline_us, static_cast<std::uint64_t>(
                         configured_maximum_ms * 1000.0f));
    const cg_canonical_interpolation_delay_config_v1 config{
        baseline_us,
        maximum_us,
        cg_snapshot_timeline_adaptive_interpolation &&
                cg_snapshot_timeline_adaptive_interpolation->integer != 0
            ? 1u
            : 0u,
        0u,
    };

    worr_cgame_snapshot_timeline_status_v2 timeline_status{};
    const auto *timeline_api = CG_GetCanonicalSnapshotTimelineAPI();
    if (!timeline_api || !timeline_api->GetStatus ||
        !timeline_api->GetStatus(&timeline_status) ||
        timeline_status.active_epoch != diagnostics.active_epoch) {
        increment_saturated(canonical_render_stats.adaptive_delay_failures);
        canonical_interpolation_pair_feedback = {};
        canonical_interpolation_delay_status = {};
        canonical_interpolation_delay_status.delay_us = baseline_us;
        canonical_interpolation_delay_status.baseline_delay_us = baseline_us;
        canonical_interpolation_delay_status.maximum_delay_us = maximum_us;
        canonical_interpolation_delay_status.adaptive_enabled =
            config.adaptive_enabled;
        return baseline_us;
    }

    cg_canonical_interpolation_delay_observation_v1 observation{};
    observation.epoch = diagnostics.active_epoch;
    observation.stream_reset_count = timeline_status.resets;
    observation.accepted_snapshot_count = timeline_status.accepted;
    if (diagnostics.latest_ref.slot !=
        WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT) {
        worr_snapshot_v2 latest{};
        if (CG_CanonicalSnapshotTimelineCopySnapshot(
                diagnostics.latest_ref, &latest) ==
            WORR_SNAPSHOT_TIMELINE_OK) {
            observation.snapshot_valid = 1u;
            observation.snapshot_server_time_us = latest.server_time_us;
            observation.snapshot_receive_time_us =
                timeline_status.last_receive_time_us;
        }
    }
    observation.pair_valid = canonical_interpolation_pair_feedback.valid;
    observation.pair_mode = canonical_interpolation_pair_feedback.mode;
    observation.pair_extrapolation_us =
        canonical_interpolation_pair_feedback.extrapolation_us;
    canonical_interpolation_pair_feedback = {};

    const auto result = CG_UpdateCanonicalInterpolationDelayV1(
        &canonical_interpolation_delay_state, &config, &observation,
        &canonical_interpolation_delay_status);
    if (result != CG_CANONICAL_INTERPOLATION_DELAY_OK) {
        increment_saturated(canonical_render_stats.adaptive_delay_failures);
        CG_ResetCanonicalInterpolationDelayV1(
            &canonical_interpolation_delay_state);
        canonical_interpolation_delay_status = {};
        canonical_interpolation_delay_status.delay_us = baseline_us;
        canonical_interpolation_delay_status.baseline_delay_us = baseline_us;
        canonical_interpolation_delay_status.maximum_delay_us = maximum_us;
        canonical_interpolation_delay_status.adaptive_enabled =
            config.adaptive_enabled;
        return baseline_us;
    }
    return canonical_interpolation_delay_status.delay_us;
}

void begin_canonical_snapshot_render_frame()
{
    canonical_render_frame = {};
    const int requested_mode = effective_canonical_snapshot_render_mode();
    const bool native_requested = requested_mode ==
        CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY;
    const bool native_ownership_present =
        cl_worr_native_snapshot_timeline_owned &&
        cl_worr_native_snapshot_timeline_owned->integer != 0;
    const auto native_authority_phase =
        CG_ResolveCanonicalNativeAuthorityV1(
            &canonical_native_authority_state, native_requested,
            native_ownership_present);
    const bool first_native_bind = native_authority_phase ==
        CG_CANONICAL_NATIVE_AUTHORITY_FIRST_BIND;
    const bool native_authority =
        first_native_bind ||
        native_authority_phase == CG_CANONICAL_NATIVE_AUTHORITY_OWNED ||
        native_authority_phase ==
            CG_CANONICAL_NATIVE_AUTHORITY_POST_BIND_LOSS;
    const int resolved_mode = native_authority_phase ==
            CG_CANONICAL_NATIVE_AUTHORITY_PREBIND_LEGACY
        ? CANONICAL_SNAPSHOT_RENDER_LEGACY
        : requested_mode;
    canonical_render_frame.requested_mode =
        static_cast<std::uint32_t>(requested_mode);
    canonical_render_frame.mode = static_cast<std::uint32_t>(resolved_mode);
    canonical_render_frame.prepared_realtime_ms = cls.realtime;
    canonical_render_frame.prepared = true;
    if (first_native_bind) {
        CG_ResetCanonicalInterpolationDelayV1(
            &canonical_interpolation_delay_state);
        canonical_interpolation_delay_status = {};
        canonical_interpolation_pair_feedback = {};
    }
    if (native_authority) {
        if (first_native_bind ||
            !canonical_native_render_view.authority_latched) {
            reset_canonical_native_render_view();
            canonical_native_render_view.authority_latched = true;
        }
        canonical_native_render_view.valid = false;
        canonical_native_render_view.render_count = 0;
    } else if (canonical_native_render_view.authority_latched) {
        reset_canonical_native_render_view();
    }

    CG_EventRuntimeSetAuditEnabled(
        cg_event_runtime_audit && cg_event_runtime_audit->integer != 0);

    /* Before the first ownership bind, requested mode 3 deliberately remains
     * legacy-authoritative.  After that one-way edge, ownership loss is an
     * observable native-authority fault on every affected frame. */
    if (native_authority_phase ==
        CG_CANONICAL_NATIVE_AUTHORITY_POST_BIND_LOSS) {
        increment_saturated(canonical_render_stats.native_authority_blocks);
        reset_canonical_native_render_view();
        canonical_native_render_view.authority_latched = true;
        return;
    }

    cg_canonical_snapshot_timeline_diagnostics_v1 diagnostics{};
    if (!CG_CanonicalSnapshotTimelineGetDiagnostics(&diagnostics) ||
        !diagnostics.active || diagnostics.pending_clock_reset) {
        if (first_native_bind)
            reset_canonical_render_stats(0u);
        if (native_authority) {
            reset_canonical_native_render_view();
            canonical_native_render_view.authority_latched = true;
        }
        return;
    }

    if (first_native_bind ||
        canonical_render_stats.epoch != diagnostics.active_epoch) {
        reset_canonical_render_stats(diagnostics.active_epoch);
        if (native_authority) {
            reset_canonical_native_render_view();
            canonical_native_render_view.authority_latched = true;
        }
    }

    worr_snapshot_timeline_clock_state_v1 clock{};
    const auto clock_result =
        advance_canonical_render_clock(diagnostics, &clock);
    canonical_render_stats.last_clock_result = clock_result;
    increment_saturated(canonical_render_stats.clock_frames);
    if (clock_result != WORR_SNAPSHOT_TIMELINE_OK) {
        increment_saturated(canonical_render_stats.clock_failures);
        if (native_authority) {
            reset_canonical_native_render_view();
            canonical_native_render_view.authority_latched = true;
        }
        return;
    }

    const bool event_runtime_enabled =
        cg_event_runtime_audit && cg_event_runtime_audit->integer != 0;
    cg_event_runtime_status_v1 event_status{};
    const bool authority_runtime_active =
        CG_EventRuntimeGetStatus(&event_status) &&
        event_status.authority_epoch != 0;
    if (resolved_mode == CANONICAL_SNAPSHOT_RENDER_LEGACY &&
        !event_runtime_enabled && !authority_runtime_active) {
        return;
    }

    std::uint64_t desired_time_us = 0;
    std::uint64_t max_extrapolation_us = 0;
    if (native_authority) {
        const float interpolation_delay_ms =
            cg_snapshot_timeline_interpolation_delay_ms
                ? Cvar_ClampValue(
                      cg_snapshot_timeline_interpolation_delay_ms,
                      0.0f, 1000.0f)
                : 50.0f;
        const float max_extrapolation_ms =
            cg_snapshot_timeline_max_extrapolation_ms
                ? Cvar_ClampValue(
                      cg_snapshot_timeline_max_extrapolation_ms,
                      0.0f, 250.0f)
                : 50.0f;
        const std::uint64_t baseline_interpolation_delay_us =
            static_cast<std::uint64_t>(
                interpolation_delay_ms * 1000.0f);
        const std::uint64_t interpolation_delay_us =
            update_canonical_interpolation_delay(
                diagnostics, baseline_interpolation_delay_us);
        max_extrapolation_us = static_cast<std::uint64_t>(
            max_extrapolation_ms * 1000.0f);

        /* The canonical clock is anchored to authoritative server time. A
         * baseline render delay is raised deterministically from accepted
         * arrival jitter and recent extrapolation/clamp pressure, then recovers
         * toward that baseline under stable cadence.  The timeline's
         * discontinuity, generation, teleport and velocity checks still have
         * to admit every bounded extrapolation. */
        desired_time_us =
            clock.render_time_us >= interpolation_delay_us
                ? clock.render_time_us - interpolation_delay_us
                : 0;
    } else {
        if (!canonical_desired_render_time_us(&desired_time_us)) {
            increment_saturated(canonical_render_stats.alignment_failures);
            return;
        }
        std::uint64_t aligned_time_us = 0;
        const auto alignment_result =
            CG_ResolveCanonicalLegacyAlignmentV1(
                clock.render_time_us, desired_time_us,
                WORR_SNAPSHOT_TIMELINE_MAX_INTERPOLATION_DELAY_US,
                &aligned_time_us);
        if (alignment_result ==
                CG_CANONICAL_LEGACY_ALIGNMENT_INVALID_ARGUMENT ||
            alignment_result ==
                CG_CANONICAL_LEGACY_ALIGNMENT_PAST_LIMIT) {
            increment_saturated(canonical_render_stats.alignment_failures);
            return;
        }
        if (alignment_result != CG_CANONICAL_LEGACY_ALIGNMENT_OK) {
            /* A process hitch may temporarily separate the legacy render
             * clock from the independently monotonic canonical clock. A
             * future legacy target resolves conservatively to canonical now,
             * allowing authority to keep advancing without presenting early. */
            increment_saturated(canonical_render_stats.alignment_failures);
        }
        desired_time_us = aligned_time_us;
    }

    auto &policy = canonical_render_frame.policy;
    policy.struct_size = sizeof(policy);
    policy.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    policy.interpolation_delay_us = clock.render_time_us - desired_time_us;
    policy.max_extrapolation_us = max_extrapolation_us;
    /* Legacy cgame rejects any individual-axis jump above 512 units.  The
     * Euclidean bound below admits exactly that axis-aligned cube; the parity
     * promotion gate remains the final authority for model/event discontinuities. */
    policy.teleport_distance = 887.0f;
    policy.max_linear_velocity = WORR_SNAPSHOT_TIMELINE_MAX_LINEAR_VELOCITY;
    policy.max_angular_velocity = WORR_SNAPSHOT_TIMELINE_MAX_ANGULAR_VELOCITY;
    policy.allow_extrapolation = max_extrapolation_us != 0 ? 1u : 0u;

    const auto pair_result = CG_CanonicalSnapshotTimelineSelectPair(
        &policy, &canonical_render_frame.pair);
    canonical_render_stats.last_pair_result = pair_result;
    increment_saturated(canonical_render_stats.pair_frames);
    if (pair_result != WORR_SNAPSHOT_TIMELINE_OK) {
        increment_saturated(canonical_render_stats.pair_failures);
        if (native_authority) {
            reset_canonical_native_render_view();
            canonical_native_render_view.authority_latched = true;
        }
        return;
    }

    canonical_render_stats.last_pair_mode =
        canonical_render_frame.pair.mode;
    canonical_render_stats.last_pair_blocks =
        canonical_render_frame.pair.blocking_reasons;
    switch (canonical_render_frame.pair.mode) {
    case WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE:
        increment_saturated(canonical_render_stats.interpolation_frames);
        break;
    case WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE:
        increment_saturated(canonical_render_stats.extrapolation_frames);
        add_saturated(canonical_render_stats.extrapolation_time_us,
                      canonical_render_frame.pair.extrapolation_us);
        break;
    case WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_EARLIEST:
    case WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST:
        increment_saturated(canonical_render_stats.clamped_frames);
        break;
    default:
        break;
    }
    if (native_authority) {
        canonical_interpolation_pair_feedback.valid = 1u;
        canonical_interpolation_pair_feedback.mode =
            canonical_render_frame.pair.mode;
        canonical_interpolation_pair_feedback.extrapolation_us =
            canonical_render_frame.pair.extrapolation_us;
    }
    canonical_render_frame.pair_valid =
        resolved_mode != CANONICAL_SNAPSHOT_RENDER_LEGACY;
    if (native_authority)
        (void)prepare_canonical_native_render_view();

    std::uint32_t ready_events = 0;
    cg_event_runtime_result_v1 runtime_result = CG_EVENT_RUNTIME_OK;
    cg_canonical_event_presentation_result_v1 legacy_result =
        CG_CANONICAL_EVENT_PRESENTATION_OK;
    const bool used_event_runtime =
        event_runtime_enabled || authority_runtime_active;
    if (used_event_runtime) {
        worr_snapshot_v2 current_snapshot{};
        if (CG_CanonicalSnapshotTimelineCopySnapshot(
                canonical_render_frame.pair.current,
                &current_snapshot) == WORR_SNAPSHOT_TIMELINE_OK) {
            runtime_result = CG_EventRuntimeAdvanceAudit(
                canonical_render_frame.pair.target_time_us,
                current_snapshot.server_tick,
                WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2, &ready_events);
        } else {
            runtime_result = CG_EVENT_RUNTIME_DEGRADED;
        }
    } else {
        legacy_result = CG_CanonicalEventPresentationAdvanceAudit(
            canonical_render_frame.pair.target_time_us,
            WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2, &ready_events);
    }
    add_saturated(canonical_render_stats.event_ready_records, ready_events);
    if ((used_event_runtime &&
         runtime_result == CG_EVENT_RUNTIME_NOT_READY) ||
        (!used_event_runtime &&
         legacy_result == CG_CANONICAL_EVENT_PRESENTATION_NOT_READY)) {
        increment_saturated(canonical_render_stats.event_future_frames);
    } else if ((used_event_runtime &&
                runtime_result != CG_EVENT_RUNTIME_OK &&
                runtime_result != CG_EVENT_RUNTIME_EMPTY &&
                runtime_result != CG_EVENT_RUNTIME_UNINITIALIZED) ||
               (!used_event_runtime &&
                legacy_result != CG_CANONICAL_EVENT_PRESENTATION_OK &&
                legacy_result !=
                    CG_CANONICAL_EVENT_PRESENTATION_UNINITIALIZED)) {
        increment_saturated(canonical_render_stats.event_audit_failures);
    }
}

bool sample_canonical_entity_transform(
    const entity_state_t *state, const centity_t *cent,
    worr_snapshot_timeline_entity_sample_v1 *sample_out)
{
    if (!state || !cent || !sample_out ||
        !canonical_render_frame.pair_valid || state->number <= 0 ||
        static_cast<std::uint32_t>(state->number) >=
            CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES) {
        return false;
    }

    increment_saturated(canonical_render_stats.sample_attempts);
    worr_snapshot_timeline_entity_sample_v1 sample{};
    const auto result = CG_CanonicalSnapshotTimelineSampleEntity(
        &canonical_render_frame.policy, &canonical_render_frame.pair,
        static_cast<std::uint32_t>(state->number), &sample);
    if (result != WORR_SNAPSHOT_TIMELINE_OK) {
        increment_saturated(canonical_render_stats.sample_failures);
        return false;
    }
    if (!sample.visible ||
        !(sample.entity.component_mask & WORR_SNAPSHOT_ENTITY_TRANSFORM)) {
        increment_saturated(canonical_render_stats.sample_invisible);
        return false;
    }

    constexpr std::uint32_t unsafe_blocks =
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT |
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_GENERATION |
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_MISSING |
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_COMPONENT |
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT |
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED |
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_ANGULAR_SPEED |
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_DISCRETE_TRANSITION |
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_POLICY;
    if (sample.blocking_reasons & unsafe_blocks) {
        increment_saturated(canonical_render_stats.sample_discontinuities);
        if (canonical_render_frame.mode ==
            CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY) {
            increment_saturated(
                canonical_render_stats.native_authority_blocks);
        }
        return false;
    }

    if (canonical_render_frame.mode ==
        CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY) {
        increment_saturated(canonical_render_stats.native_authority_samples);
        increment_saturated(canonical_render_stats.promoted_transforms);
        *sample_out = sample;
        return true;
    }

    vec3_t legacy_origin;
    vec3_t legacy_old_origin;
    vec3_t legacy_angles;
    LerpVector(cent->prev.origin, cent->current.origin,
               cl.lerpfrac, legacy_origin);
    LerpVector(cent->prev.old_origin, cent->current.old_origin,
               cl.lerpfrac, legacy_old_origin);
    LerpAngles(cent->prev.angles, cent->current.angles,
               cl.lerpfrac, legacy_angles);

    const float origin_error = maximum_component_error(
        legacy_origin, sample.entity.origin);
    const float old_origin_error = maximum_component_error(
        legacy_old_origin, sample.entity.old_origin);
    const float angle_error = maximum_angle_error(
        legacy_angles, sample.entity.angles);
    canonical_render_stats.max_origin_error = max(
        canonical_render_stats.max_origin_error, origin_error);
    canonical_render_stats.max_old_origin_error = max(
        canonical_render_stats.max_old_origin_error, old_origin_error);
    canonical_render_stats.max_angle_error = max(
        canonical_render_stats.max_angle_error, angle_error);

    const float epsilon = cg_snapshot_timeline_render_epsilon
        ? Cvar_ClampValue(cg_snapshot_timeline_render_epsilon,
                          0.0001f, 8.0f)
        : 0.125f;
    const bool old_origin_matters =
        (state->renderfx & RF_BEAM) != 0;
    if (origin_error > epsilon || angle_error > epsilon ||
        (old_origin_matters && old_origin_error > epsilon)) {
        increment_saturated(canonical_render_stats.parity_mismatches);
        return false;
    }

    increment_saturated(canonical_render_stats.parity_matches);
    *sample_out = sample;
    if (canonical_render_frame.mode == CANONICAL_SNAPSHOT_RENDER_PROMOTE) {
        increment_saturated(canonical_render_stats.promoted_transforms);
        return true;
    }
    return false;
}

void end_canonical_snapshot_render_frame()
{
    const bool event_runtime_enabled =
        cg_event_runtime_audit && cg_event_runtime_audit->integer != 0;
    if ((canonical_render_frame.mode ==
             CANONICAL_SNAPSHOT_RENDER_LEGACY &&
         !event_runtime_enabled) ||
        cls.realtime - canonical_render_stats.last_debug_time_ms < 1000) {
        return;
    }

    if (canonical_render_frame.mode !=
        CANONICAL_SNAPSHOT_RENDER_LEGACY) {
        Com_LPrintf(
            PRINT_ALL,
            "cg_snapshot_timeline_render: epoch=%u mode=%u clock=%llu/%llu "
            "pair=%llu/%llu align_fail=%llu pair_mode=%u pair_blocks=0x%x "
            "samples=%llu fail=%llu invisible=%llu discontinuity=%llu "
            "parity=%llu/%llu native=%llu/%llu promoted=%llu "
            "events=%llu/%llu/%llu "
            "max_error=%.4f/%.4f/%.4f "
            "timeline_modes=%llu/%llu/%llu extrap_us=%llu "
            "enumeration=%llu/%llu/%llu/%llu resets=%llu "
            "previous_only=%llu/%llu/%llu "
            "view=%u/%u/%u submission=%llu/%llu/%016llx\n",
            canonical_render_stats.epoch, canonical_render_frame.mode,
            static_cast<unsigned long long>(canonical_render_stats.clock_frames),
            static_cast<unsigned long long>(canonical_render_stats.clock_failures),
            static_cast<unsigned long long>(canonical_render_stats.pair_frames),
            static_cast<unsigned long long>(canonical_render_stats.pair_failures),
            static_cast<unsigned long long>(canonical_render_stats.alignment_failures),
            canonical_render_stats.last_pair_mode,
            canonical_render_stats.last_pair_blocks,
            static_cast<unsigned long long>(canonical_render_stats.sample_attempts),
            static_cast<unsigned long long>(canonical_render_stats.sample_failures),
            static_cast<unsigned long long>(canonical_render_stats.sample_invisible),
            static_cast<unsigned long long>(canonical_render_stats.sample_discontinuities),
            static_cast<unsigned long long>(canonical_render_stats.parity_matches),
            static_cast<unsigned long long>(canonical_render_stats.parity_mismatches),
            static_cast<unsigned long long>(
                canonical_render_stats.native_authority_samples),
            static_cast<unsigned long long>(
                canonical_render_stats.native_authority_blocks),
            static_cast<unsigned long long>(canonical_render_stats.promoted_transforms),
            static_cast<unsigned long long>(canonical_render_stats.event_ready_records),
            static_cast<unsigned long long>(canonical_render_stats.event_future_frames),
            static_cast<unsigned long long>(canonical_render_stats.event_audit_failures),
            canonical_render_stats.max_origin_error,
            canonical_render_stats.max_old_origin_error,
            canonical_render_stats.max_angle_error,
            static_cast<unsigned long long>(
                canonical_render_stats.interpolation_frames),
            static_cast<unsigned long long>(
                canonical_render_stats.extrapolation_frames),
            static_cast<unsigned long long>(
                canonical_render_stats.clamped_frames),
            static_cast<unsigned long long>(
                canonical_render_stats.extrapolation_time_us),
            static_cast<unsigned long long>(
                canonical_render_stats.enumeration_frames),
            static_cast<unsigned long long>(
                canonical_render_stats.enumeration_failures),
            static_cast<unsigned long long>(
                canonical_render_stats.enumerated_current_entities),
            static_cast<unsigned long long>(
                canonical_render_stats.enumerated_removed_entities),
            static_cast<unsigned long long>(
                canonical_render_stats.enumeration_generation_resets),
            static_cast<unsigned long long>(
                canonical_render_stats.enumerated_removed_entities),
            static_cast<unsigned long long>(
                canonical_render_stats
                    .selected_visible_previous_only_sources),
            static_cast<unsigned long long>(
                canonical_render_stats
                    .renderer_previous_only_submitted_sources),
            canonical_native_render_view.last_result,
            canonical_native_render_view.render_count,
            canonical_native_render_view.record_count,
            static_cast<unsigned long long>(
                canonical_render_stats.renderer_submission_calls),
            static_cast<unsigned long long>(
                canonical_render_stats.renderer_submitted_sources),
            static_cast<unsigned long long>(
                canonical_render_stats.renderer_submission_hash));
        Com_LPrintf(
            PRINT_ALL,
            "cg_snapshot_timeline_adaptive: enabled=%u adjustment=%u "
            "delay_us=%llu/%llu/%llu arrival_us=%llu/%llu/%llu "
            "counts=%llu/%llu/%llu/%llu failures=%llu\n",
            canonical_interpolation_delay_status.adaptive_enabled,
            canonical_interpolation_delay_status.last_adjustment,
            static_cast<unsigned long long>(
                canonical_interpolation_delay_status.delay_us),
            static_cast<unsigned long long>(
                canonical_interpolation_delay_status.baseline_delay_us),
            static_cast<unsigned long long>(
                canonical_interpolation_delay_status.maximum_delay_us),
            static_cast<unsigned long long>(
                canonical_interpolation_delay_status.cadence_us),
            static_cast<unsigned long long>(
                canonical_interpolation_delay_status.jitter_us),
            static_cast<unsigned long long>(
                canonical_interpolation_delay_status.last_jitter_us),
            static_cast<unsigned long long>(
                canonical_interpolation_delay_status.rise_adjustments),
            static_cast<unsigned long long>(
                canonical_interpolation_delay_status.recovery_adjustments),
            static_cast<unsigned long long>(
                canonical_interpolation_delay_status.pressure_observations),
            static_cast<unsigned long long>(
                canonical_interpolation_delay_status.reset_count),
            static_cast<unsigned long long>(
                canonical_render_stats.adaptive_delay_failures));
    }
    if (event_runtime_enabled) {
        cg_event_runtime_status_v1 status{};
        if (CG_EventRuntimeGetStatus(&status)) {
            Com_LPrintf(
                PRINT_ALL,
                "cg_event_runtime_audit: epochs=%u/%u/%u degraded=%u "
                "body/ref/auth=%u/%u/%u joins=%llu/%llu/%llu "
                "present=%llu/%llu/%llu/%llu correction=%llu/%llu "
                "stalls=%llu/%llu/%llu future=%llu hash=%016llx\n",
                status.legacy_epoch, status.authority_epoch,
                status.snapshot_epoch, status.degraded,
                status.legacy_body_count, status.reference_count,
                status.authority_count,
                static_cast<unsigned long long>(status.authority_ref_body_joins),
                static_cast<unsigned long long>(status.legacy_ref_before_body_joins),
                static_cast<unsigned long long>(status.legacy_body_before_ref_joins),
                static_cast<unsigned long long>(status.predicted_presentations),
                static_cast<unsigned long long>(status.authoritative_presentations),
                static_cast<unsigned long long>(status.legacy_entity_presentations),
                static_cast<unsigned long long>(status.legacy_action_presentations),
                static_cast<unsigned long long>(status.prediction_corrections),
                static_cast<unsigned long long>(status.prediction_late_corrections),
                static_cast<unsigned long long>(status.authority_sequence_stalls),
                static_cast<unsigned long long>(status.authority_reference_stalls),
                static_cast<unsigned long long>(status.legacy_reference_stalls),
                static_cast<unsigned long long>(status.future_time_stalls),
                static_cast<unsigned long long>(status.presentation_chain_hash));
        }
    }
    canonical_render_stats.last_debug_time_ms = cls.realtime;
}

} // namespace

void CG_CanonicalSnapshotRender_ResetStream(void)
{
    CG_ResetCanonicalNativeAuthorityV1(&canonical_native_authority_state);
    CG_ResetCanonicalInterpolationDelayV1(
        &canonical_interpolation_delay_state);
    canonical_interpolation_delay_status = {};
    canonical_interpolation_pair_feedback = {};
    canonical_render_frame = {};
    reset_canonical_native_render_view();
    reset_canonical_render_stats(0u);
}

void CG_CanonicalSnapshotRender_InitCvars(void)
{
    if (!cgei)
        return;
    cg_snapshot_timeline_render = Cvar_Get(
        "cg_snapshot_timeline_render", "0", CVAR_NOARCHIVE);
    cg_snapshot_timeline_render_epsilon = Cvar_Get(
        "cg_snapshot_timeline_render_epsilon", "0.125", CVAR_NOARCHIVE);
    cg_snapshot_timeline_interpolation_delay_ms = Cvar_Get(
        "cg_snapshot_timeline_interpolation_delay_ms", "50",
        CVAR_NOARCHIVE);
    cg_snapshot_timeline_adaptive_interpolation = Cvar_Get(
        "cg_snapshot_timeline_adaptive_interpolation", "1",
        CVAR_NOARCHIVE);
    cg_snapshot_timeline_max_interpolation_delay_ms = Cvar_Get(
        "cg_snapshot_timeline_max_interpolation_delay_ms", "150",
        CVAR_NOARCHIVE);
    cg_snapshot_timeline_max_extrapolation_ms = Cvar_Get(
        "cg_snapshot_timeline_max_extrapolation_ms", "50",
        CVAR_NOARCHIVE);
    cg_event_runtime_audit = Cvar_Get(
        "cg_event_runtime_audit", "0", CVAR_NOARCHIVE);
    cl_worr_native_event_presentation_owned = Cvar_Get(
        "cl_worr_native_event_presentation_owned", "0",
        CVAR_ROM | CVAR_NOARCHIVE);
    cl_worr_native_snapshot_timeline_owned = Cvar_Get(
        "cl_worr_native_snapshot_timeline_owned", "0",
        CVAR_ROM | CVAR_NOARCHIVE);
    if (cg_event_runtime_audit)
        cg_event_runtime_audit->changed = event_runtime_audit_changed;
    CG_EventRuntimeSetAuditEnabled(
        cg_event_runtime_audit && cg_event_runtime_audit->integer != 0);
}

static constexpr float FLASHLIGHT_CLASSIC_DISTANCE = 256.0f;
static constexpr float FLASHLIGHT_SHADOW_DISTANCE = 1024.0f;
static constexpr float FLASHLIGHT_SHADOW_RADIUS = 1024.0f;
static constexpr float FLASHLIGHT_SHADOW_INTENSITY = 6.0f;
static constexpr float FLASHLIGHT_SHADOW_RESOLUTION = 512.0f;
static constexpr float FLASHLIGHT_SHADOW_CONE_ANGLE = 24.0f;
static constexpr float FLASHLIGHT_TORSO_FORWARD_OFFSET = 4.0f;
static constexpr float FLASHLIGHT_TORSO_DOWN_OFFSET = 18.0f;
static constexpr float FLASHLIGHT_TORSO_IDLE_BREATH_SPEED = 0.0017f;
static constexpr float FLASHLIGHT_TORSO_IDLE_RIGHT = 0.10f;
static constexpr float FLASHLIGHT_TORSO_IDLE_UP = 0.30f;
static constexpr float FLASHLIGHT_TORSO_IDLE_FORWARD = 0.48f;
static constexpr float FLASHLIGHT_TORSO_IDLE_AIM_RIGHT = 0.0020f;
static constexpr float FLASHLIGHT_TORSO_IDLE_AIM_UP = 0.0060f;
static constexpr float FLASHLIGHT_TORSO_BOB_RIGHT = 1.45f;
static constexpr float FLASHLIGHT_TORSO_BOB_UP = 1.10f;
static constexpr float FLASHLIGHT_TORSO_BOB_FORWARD = 0.65f;
static constexpr float FLASHLIGHT_TORSO_BOB_SPEED = 0.0080f;
static constexpr float FLASHLIGHT_TORSO_BOB_SPEED_RUN = 0.0060f;
static constexpr float FLASHLIGHT_TORSO_DAMAGE_BOB_SPEED = 0.026f;
static constexpr float FLASHLIGHT_TORSO_DAMAGE_BOB_SCALE = 0.45f;
static constexpr float FLASHLIGHT_TORSO_DAMAGE_SHARPEN_SCALE = 0.60f;
static constexpr float FLASHLIGHT_TORSO_DAMAGE_AIM_RIGHT = 0.0060f;
static constexpr float FLASHLIGHT_TORSO_DAMAGE_AIM_UP = 0.0080f;
static constexpr float FLASHLIGHT_TORSO_AIM_RIGHT = 0.0100f;
static constexpr float FLASHLIGHT_TORSO_AIM_UP = 0.0080f;

static bool CL_ParseBrightskinColor(const char *s, color_t *color)
{
    if (SCR_ParseColor(s, color))
        return true;

    if (s[0] != '#') {
        size_t len = strlen(s);
        if (len == 6 || len == 8) {
            char buffer[10];

            buffer[0] = '#';
            Q_strlcpy(buffer + 1, s, sizeof(buffer) - 1);
            return SCR_ParseColor(buffer, color);
        }
    }

    return false;
}

static void cl_brightskins_enemy_color_changed(cvar_t *self)
{
    if (!CL_ParseBrightskinColor(self->string, &brightskin_enemy_color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
        Cvar_Reset(self);
        brightskin_enemy_color = COLOR_RED;
    }
}

static void cl_brightskins_team_color_changed(cvar_t *self)
{
    if (!CL_ParseBrightskinColor(self->string, &brightskin_team_color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
        Cvar_Reset(self);
        brightskin_team_color = COLOR_GREEN;
    }
}

void CL_InitBrightskins(void)
{
    cl_brightskins_custom = Cvar_Get("cl_brightskins_custom", "0", CVAR_ARCHIVE);
    cl_brightskins_enemy_color = Cvar_Get("cl_brightskins_enemy_color", "#ff0000", CVAR_ARCHIVE);
    cl_brightskins_enemy_color->changed = cl_brightskins_enemy_color_changed;
    cl_brightskins_enemy_color->generator = Com_Color_g;
    cl_brightskins_enemy_color_changed(cl_brightskins_enemy_color);
    cl_brightskins_team_color = Cvar_Get("cl_brightskins_team_color", "#00ff00", CVAR_ARCHIVE);
    cl_brightskins_team_color->changed = cl_brightskins_team_color_changed;
    cl_brightskins_team_color->generator = Com_Color_g;
    cl_brightskins_team_color_changed(cl_brightskins_team_color);
    cl_brightskins_dead = Cvar_Get("cl_brightskins_dead", "1", CVAR_ARCHIVE);
}

typedef struct {
    clientinfo_t info;
    bool active;
} forced_model_t;

static forced_model_t cl_forced_enemy_model;
static forced_model_t cl_forced_team_model;
static int32_t cl_forced_enemy_modcount = -1;
static int32_t cl_forced_team_modcount = -1;

static bool CL_IsSpectatorView(void)
{
    pmtype_t pm_type = cl.frame.ps.pmove.pm_type;

    return pm_type == PM_SPECTATOR || pm_type == PM_FREEZE;
}

static bool CL_FlashlightTorsoMotionAllowed(void)
{
    if (info_bobskip && info_bobskip->integer)
        return false;

    if (CL_IsSpectatorView())
        return false;

    return cl.frame.ps.pmove.pm_type < PM_DEAD;
}

static float CL_FlashlightTorsoSwayScale(void)
{
    if (!cl_flashlight_torso_sway)
        return 1.0f;

    return Q_clipf(cl_flashlight_torso_sway->value, 0.0f, 2.0f);
}

static float CL_FlashlightMovementScale(void)
{
    vec3_t velocity;

    if (!CL_FlashlightTorsoMotionAllowed())
        return 0.0f;

    if (!cls.demo.playback && cl_predict->integer &&
        !(cl.frame.ps.pmove.pm_flags & PMF_NO_PREDICTION)) {
        VectorCopy(cl.predicted_velocity, velocity);
    } else {
        VectorCopy(cl.frame.ps.pmove.velocity, velocity);
    }

    velocity[2] = 0.0f;
    float scale = Q_clipf(VectorLength(velocity) / 320.0f, 0.0f, 1.0f);
    if (!(cl.frame.ps.pmove.pm_flags & PMF_ON_GROUND))
        scale *= 0.35f;

    return scale;
}

static float CL_FlashlightDamageScale(void)
{
    if (!CL_FlashlightTorsoMotionAllowed())
        return 0.0f;

    float damage = Q_clipf(cl.refdef.damage_blend[3] * 1.75f, 0.0f, 1.0f);
    int health = cl.frame.ps.stats[STAT_HEALTH];

    if (health > 0 && health < 60) {
        float low_health = ((60.0f - (float)health) / 60.0f) * 0.35f;
        if (low_health > damage)
            damage = low_health;
    }

    return damage;
}

static void CL_ApplyFlashlightTorsoMotion(vec3_t origin, vec3_t direction)
{
    float sway_scale = CL_FlashlightTorsoSwayScale();
    float movement = CL_FlashlightMovementScale();
    float damage = CL_FlashlightDamageScale();

    if (sway_scale <= 0.0f || !CL_FlashlightTorsoMotionAllowed())
        return;

    float breath_phase = (float)cl.time * FLASHLIGHT_TORSO_IDLE_BREATH_SPEED;
    float breath = sinf(breath_phase);
    float breath_side = sinf(breath_phase * 0.50f + 0.85f);
    float bob_phase = (float)cl.time *
        (FLASHLIGHT_TORSO_BOB_SPEED + FLASHLIGHT_TORSO_BOB_SPEED_RUN * movement);
    float damage_phase = (float)cl.time * FLASHLIGHT_TORSO_DAMAGE_BOB_SPEED;
    float stride = movement * (1.0f + damage * FLASHLIGHT_TORSO_DAMAGE_BOB_SCALE);
    float sharp = damage * (0.40f + movement * FLASHLIGHT_TORSO_DAMAGE_SHARPEN_SCALE);
    float side_wave = sinf(bob_phase);
    float lift_wave = 0.5f - 0.5f * cosf(bob_phase * 2.0f);

    float right = breath_side * FLASHLIGHT_TORSO_IDLE_RIGHT +
        side_wave * FLASHLIGHT_TORSO_BOB_RIGHT * stride +
        sinf(damage_phase + 0.65f) * FLASHLIGHT_TORSO_BOB_RIGHT * 0.40f * sharp;
    float up = breath * FLASHLIGHT_TORSO_IDLE_UP +
        (lift_wave - 0.45f) * FLASHLIGHT_TORSO_BOB_UP * stride +
        sinf(damage_phase) * FLASHLIGHT_TORSO_BOB_UP * 0.65f * sharp;
    float forward = breath * FLASHLIGHT_TORSO_IDLE_FORWARD +
        cosf(bob_phase) * FLASHLIGHT_TORSO_BOB_FORWARD * stride +
        cosf(damage_phase * 0.70f) * FLASHLIGHT_TORSO_BOB_FORWARD * 0.50f * sharp -
        damage * 0.25f;
    float aim_right = breath_side * FLASHLIGHT_TORSO_IDLE_AIM_RIGHT +
        right * FLASHLIGHT_TORSO_AIM_RIGHT +
        sinf(damage_phase + 1.15f) * sharp * FLASHLIGHT_TORSO_DAMAGE_AIM_RIGHT;
    float aim_up = breath * FLASHLIGHT_TORSO_IDLE_AIM_UP +
        up * FLASHLIGHT_TORSO_AIM_UP +
        sinf(damage_phase) * sharp * FLASHLIGHT_TORSO_DAMAGE_AIM_UP;

    VectorMA(origin, right * sway_scale, cl.v_right, origin);
    VectorMA(origin, up * sway_scale, cl.v_up, origin);
    VectorMA(origin, forward * sway_scale, cl.v_forward, origin);

    VectorMA(direction, aim_right * sway_scale, cl.v_right, direction);
    VectorMA(direction, aim_up * sway_scale, cl.v_up, direction);
    if (VectorNormalize(direction) <= 0.0f)
        VectorCopy(cl.v_forward, direction);
}

void CL_MigratePlayerCvars(void)
{
    float old_enemy_outline = 0.0f;
    float old_self_outline = 0.0f;
    float old_enemy_rim = 0.0f;
    float old_self_rim = 0.0f;

    if (cl_enemy_outline)
        old_enemy_outline = Cvar_ClampValue(cl_enemy_outline, 0.0f, 1.0f);
    if (cl_enemy_outline_self)
        old_self_outline = Cvar_ClampValue(cl_enemy_outline_self, 0.0f, 1.0f);
    if (cl_enemy_rimlight)
        old_enemy_rim = Cvar_ClampValue(cl_enemy_rimlight, 0.0f, 1.0f);
    if (cl_enemy_rimlight_self)
        old_self_rim = Cvar_ClampValue(cl_enemy_rimlight_self, 0.0f, 1.0f);

    if (cl_player_outline_enemy && cl_player_outline_enemy->modified_count == 0 && old_enemy_outline > 0.0f)
        Cvar_SetValue(cl_player_outline_enemy, old_enemy_outline, FROM_CODE);

    if (cl_player_outline_team && cl_player_outline_team->modified_count == 0) {
        float team_outline = old_enemy_outline > old_self_outline ? old_enemy_outline : old_self_outline;
        if (team_outline > 0.0f)
            Cvar_SetValue(cl_player_outline_team, team_outline, FROM_CODE);
    }

    if (cl_player_rimlight_enemy && cl_player_rimlight_enemy->modified_count == 0 && old_enemy_rim > 0.0f)
        Cvar_SetValue(cl_player_rimlight_enemy, old_enemy_rim, FROM_CODE);

    if (cl_player_rimlight_team && cl_player_rimlight_team->modified_count == 0) {
        float team_rim = old_enemy_rim > old_self_rim ? old_enemy_rim : old_self_rim;
        if (team_rim > 0.0f)
            Cvar_SetValue(cl_player_rimlight_team, team_rim, FROM_CODE);
    }
}

static bool CL_ForceModelActive(const cvar_t *var)
{
    if (!var || !var->string[0])
        return false;

    return Q_stricmp(var->string, "0") != 0;
}

static void CL_LoadForcedModel(forced_model_t *forced, const char *label, const cvar_t *var)
{
    memset(forced, 0, sizeof(*forced));

    if (!CL_ForceModelActive(var))
        return;

    char buffer[MAX_QPATH * 2];
    if (Q_concat(buffer, sizeof(buffer), "forced\\", var->string) >= sizeof(buffer)) {
        Com_WPrintf("cl_force_%s_model too long: '%s'\n", label, var->string);
        return;
    }

    CL_LoadClientinfo(&forced->info, buffer);
    if (!forced->info.model || !forced->info.skin) {
        memset(&forced->info, 0, sizeof(forced->info));
        return;
    }

    forced->active = true;
}

static void CL_UpdateForcedModels(void)
{
    if (!cl_force_enemy_model || !cl_force_team_model)
        return;

    if (cl_forced_enemy_modcount != cl_force_enemy_model->modified_count) {
        cl_forced_enemy_modcount = cl_force_enemy_model->modified_count;
        CL_LoadForcedModel(&cl_forced_enemy_model, "enemy", cl_force_enemy_model);
    }

    if (cl_forced_team_modcount != cl_force_team_model->modified_count) {
        cl_forced_team_modcount = cl_force_team_model->modified_count;
        CL_LoadForcedModel(&cl_forced_team_model, "team", cl_force_team_model);
    }
}

void CL_RegisterForcedModels(void)
{
    cl_forced_enemy_modcount = -1;
    cl_forced_team_modcount = -1;
    CL_UpdateForcedModels();
}

/*
=========================================================================

FRAME PARSING

=========================================================================
*/

// returns true if origin/angles update has been optimized out
static inline bool entity_is_optimized(const entity_state_t *state)
{
    return (cls.serverProtocol == PROTOCOL_VERSION_Q2PRO || cls.serverProtocol == PROTOCOL_VERSION_RERELEASE)
        && state->number == cl.frame.clientNum + 1
        && cl.frame.ps.pmove.pm_type < PM_DEAD;
}

static inline void
entity_update_new(centity_t *ent, const entity_state_t *state, const vec_t *origin)
{
    static int entity_ctr;
    ent->id = ++entity_ctr;
    ent->trailcount = 1024;     // for diminishing rocket / grenade trails
    ent->flashlightfrac = 1.0f;

    // duplicate the current state so lerping doesn't hurt anything
    ent->prev = *state;
#if USE_FPS
    ent->prev_frame = state->frame;
    ent->event_frame = cl.frame.number;
#endif

// KEX
    ent->current_frame = ent->last_frame = state->frame;
    ent->frame_servertime = cl.servertime;
    ent->stair_time = cls.realtime;
// KEX

    if (state->event == EV_PLAYER_TELEPORT ||
        state->event == EV_OTHER_TELEPORT ||
        (state->renderfx & RF_BEAM)) {
        // no lerping if teleported
        VectorCopy(origin, ent->lerp_origin);
        return;
    }

    // old_origin is valid for new entities,
    // so use it as starting point for interpolating between
    VectorCopy(state->old_origin, ent->prev.origin);
    VectorCopy(state->old_origin, ent->lerp_origin);
}

#define	MAX_STEP_CHANGE 32

static inline void
entity_update_old(centity_t *ent, const entity_state_t *state, const vec_t *origin)
{
    int event = state->event;

#if USE_FPS
    // check for new event
    if (state->event != ent->current.event)
        ent->event_frame = cl.frame.number; // new
    else if (cl.frame.number - ent->event_frame >= cl.frametime.div)
        ent->event_frame = cl.frame.number; // refreshed
    else
        event = 0; // duplicated
#endif

// KEX
    if (ent->current_frame != state->frame)
    {
        ent->current_frame = state->frame;
        ent->last_frame = ent->current.frame;
        ent->frame_servertime = cl.servertime;
    }
// KEX

    if (state->modelindex != ent->current.modelindex
        || state->modelindex2 != ent->current.modelindex2
        || state->modelindex3 != ent->current.modelindex3
        || state->modelindex4 != ent->current.modelindex4
        || event == EV_PLAYER_TELEPORT
        || event == EV_OTHER_TELEPORT
        || fabsf(origin[0] - ent->current.origin[0]) > 512
        || fabsf(origin[1] - ent->current.origin[1]) > 512
        || fabsf(origin[2] - ent->current.origin[2]) > 512
        || cl_nolerp->integer == 1) {
        // some data changes will force no lerping
        ent->trailcount = 1024;     // for diminishing rocket / grenade trails
        ent->flashlightfrac = 1.0f;

        // duplicate the current state so lerping doesn't hurt anything
        ent->prev = *state;
#if USE_FPS
        ent->prev_frame = state->frame;
#endif
        // no lerping if teleported or morphed
        VectorCopy(origin, ent->lerp_origin);
        ent->stair_time = cls.realtime;
        return;
    }

#if USE_FPS
    // start alias model animation
    if (state->frame != ent->current.frame) {
        ent->prev_frame = ent->current.frame;
        ent->anim_start = cl.servertime - cl.frametime.time;
    }
#endif

    // stair interpolation support; this is only
    // necessary for > 10 fps
    if (cl.frametime.time != 100) {
        if (state->renderfx & RF_STAIR_STEP) {
            // Code below adapted from Q3A.
            // check for stepping up before a previous step is completed
            float step = origin[2] - ent->current.origin[2];
            float delta = cls.realtime - ent->stair_time;
            float old_step;
            if (delta < STEP_TIME) {
                old_step = ent->stair_height * (STEP_TIME - delta) / STEP_TIME;
            } else {
                old_step = 0;
            }

            // add this amount
            ent->stair_height = Q_clip(old_step + step, -MAX_STEP_CHANGE, MAX_STEP_CHANGE);
            ent->stair_time = cls.realtime;
        }
    }

    // shuffle the last state to previous
    ent->prev = ent->current;
}

static inline bool entity_is_new(const centity_t *ent)
{
    if (!cl.oldframe.valid)
        return true;    // last received frame was invalid

    if (ent->serverframe != cl.oldframe.number)
        return true;    // wasn't in last received frame

    if (cl_nolerp->integer == 2)
        return true;    // developer option, always new

    if (cl_nolerp->integer == 3)
        return false;   // developer option, lerp from last received frame

    if (cl.oldframe.number != cl.frame.number - 1)
        return true;    // previous server frame was dropped

    return false;
}

static void parse_entity_update(const entity_state_t *state)
{
    centity_t *ent = &cl_entities[state->number];
    const vec_t *origin;
    vec3_t origin_v;

    // if entity is solid, decode mins/maxs and add to the list
    if (state->solid && state->number != cl.frame.clientNum + 1
        && cl.numSolidEntities < MAX_PACKET_ENTITIES)
        cl.solidEntities[cl.numSolidEntities++] = ent;

    if (state->solid && state->solid != PACKED_BSP) {
        cgei->Q2Proto_UnpackSolid(state->solid, ent->mins, ent->maxs);
        ent->radius = Distance(ent->maxs, ent->mins) * 0.5f;
    } else {
        VectorClear(ent->mins);
        VectorClear(ent->maxs);
        ent->radius = 0;
    }

    // work around Q2PRO server bandwidth optimization
    if (entity_is_optimized(state)) {
        VectorCopy(cl.frame.ps.pmove.origin, origin_v);
        origin = origin_v;
    } else {
        origin = state->origin;
    }

    if (entity_is_new(ent)) {
        // wasn't in last update, so initialize some things
        entity_update_new(ent, state, origin);
    } else {
        entity_update_old(ent, state, origin);
    }

    ent->serverframe = cl.frame.number;
    ent->current = *state;

    // work around Q2PRO server bandwidth optimization
    if (entity_is_optimized(state)) {
        Com_PlayerToEntityState(&cl.frame.ps, &ent->current);
    }
}

static void present_entity_impulse_event(int number, int event,
                                         const vec3_t fixed_origin,
                                         qhandle_t prepared_sound = 0)
{
    const vec_t *origin = fixed_origin;
    if (!origin)
        origin = cl_entities[number].current.origin;

    switch (event) {
    case EV_ITEM_RESPAWN:
        S_StartSound(fixed_origin, number, CHAN_WEAPON,
                     prepared_sound ? prepared_sound
                                    : S_RegisterSound("items/respawn1.wav"),
                     1, ATTN_IDLE, 0);
        CL_ItemRespawnParticles(origin);
        break;
    case EV_PLAYER_TELEPORT:
        S_StartSound(fixed_origin, number, CHAN_WEAPON,
                     prepared_sound ? prepared_sound
                                    : S_RegisterSound("misc/tele1.wav"),
                     1, ATTN_IDLE, 0);
        CL_TeleportParticles(origin);
        break;
    case EV_FOOTSTEP:
        if (cl_footsteps->integer) {
            if (fixed_origin)
                CL_PlayFootstepSfxAt(-1, number, fixed_origin, 1.0f,
                                     ATTN_NORM);
            else
                CL_PlayFootstepSfx(-1, number, 1.0f, ATTN_NORM);
        }
        break;
    case EV_OTHER_FOOTSTEP:
        if (cl.csr.extended && cl_footsteps->integer) {
            if (fixed_origin)
                CL_PlayFootstepSfxAt(-1, number, fixed_origin, 0.5f,
                                     ATTN_IDLE);
            else
                CL_PlayFootstepSfx(-1, number, 0.5f, ATTN_IDLE);
        }
        break;
    case EV_LADDER_STEP:
        if (cl.csr.extended && cl_footsteps->integer) {
            if (fixed_origin)
                CL_PlayFootstepSfxAt(FOOTSTEP_ID_LADDER, number,
                                     fixed_origin, 0.5f, ATTN_IDLE);
            else
                CL_PlayFootstepSfx(FOOTSTEP_ID_LADDER, number, 0.5f,
                                   ATTN_IDLE);
        }
        break;
    case EV_FALLSHORT:
        S_StartSound(fixed_origin, number, CHAN_AUTO,
                     prepared_sound ? prepared_sound
                                    : S_RegisterSound("player/land1.wav"),
                     1, ATTN_NORM, 0);
        break;
    case EV_FALL:
        S_StartSound(fixed_origin, number, CHAN_AUTO,
                     prepared_sound ? prepared_sound
                                    : S_RegisterSound("*fall2.wav"),
                     1, ATTN_NORM, 0);
        break;
    case EV_FALLFAR:
        S_StartSound(fixed_origin, number, CHAN_AUTO,
                     prepared_sound ? prepared_sound
                                    : S_RegisterSound("*fall1.wav"),
                     1, ATTN_NORM, 0);
        break;
    }
}

bool CL_CanPresentLegacyEntityEventValue(
    int number, int raw_event, const entity_state_t *source_state)
{
    if (!source_state || !cgei || !cl_footsteps || number <= 0 ||
        number >= cl.csr.max_edicts || source_state->number != number ||
        raw_event <= EV_NONE || raw_event > EV_LADDER_STEP) {
        return false;
    }

    switch (raw_event) {
    case EV_ITEM_RESPAWN:
    case EV_PLAYER_TELEPORT:
    case EV_FALLSHORT:
    case EV_FALL:
    case EV_FALLFAR:
        return CL_NativeLegacyEntityEventSound(raw_event) != 0;
    case EV_FOOTSTEP:
        return !cl_footsteps->integer ||
               CL_CanPresentFootstepValue(FOOTSTEP_ID_DEFAULT);
    case EV_OTHER_FOOTSTEP:
        return !cl.csr.extended || !cl_footsteps->integer ||
               CL_CanPresentFootstepValue(FOOTSTEP_ID_DEFAULT);
    case EV_LADDER_STEP:
        return !cl.csr.extended || !cl_footsteps->integer ||
               CL_CanPresentFootstepValue(FOOTSTEP_ID_LADDER);
    default:
        return false;
    }
}

void CL_PresentLegacyEntityEventValue(int number, int raw_event,
                                      const entity_state_t *source_state)
{
    if (!source_state || number <= 0 || number >= cl.csr.max_edicts ||
        source_state->number != number || raw_event <= EV_NONE ||
        raw_event > EV_LADDER_STEP) {
        return;
    }
    present_entity_impulse_event(
        number, raw_event, source_state->origin,
        CL_NativeLegacyEntityEventSound(raw_event));
}

// an entity has just been parsed that has an event value
static void parse_entity_event(int number)
{
    const centity_t *cent = &cl_entities[number];

    if (CL_FRAMESYNC) {
        // EF_TELEPORTER acts like an event, but is not cleared each frame
        if (cent->current.effects & EF_TELEPORTER)
            CL_TeleporterParticles(cent->current.origin);

        if (cent->current.effects & EF_TELEPORTER2)
            CL_TeleporterParticles2(cent->current.origin);

        if (cent->current.effects & EF_BARREL_EXPLODING)
            CL_BarrelExplodingParticles(cent->current.origin);
    }

#if USE_FPS
    if (cent->event_frame != cl.frame.number)
        return;
#endif

    if (cent->current.event &&
        !CG_SnapshotTimeline_ShouldDispatchEvent(
            static_cast<std::uint32_t>(number),
            static_cast<std::uint32_t>(cent->current.event))) {
        return;
    }

    /* The raw frame remains decoded for capture/audit, but once the native
     * event epoch owns presentation its fenced journal is the only one-shot
     * effect source for this map.  The ownership ROM latch survives a
     * transport drain and is released only at a whole-stream boundary. */
    if (cent->current.event &&
        cl_worr_native_event_presentation_owned &&
        cl_worr_native_event_presentation_owned->integer) {
        return;
    }

    present_entity_impulse_event(number, cent->current.event, nullptr);
}

static void set_active_state(void)
{
    cls.state = ca_active;
    CG_SnapshotTimeline_Reset(cls.realtime);
    Cbuf_ExecuteDeferred(&cmd_buffer);

    cl.serverdelta = cl.frame.number ? Q_align_down(cl.frame.number, CL_FRAMEDIV) : 0;
    cl.time = cl.servertime = 0; // set time, needed for demos
#if USE_FPS
    cl.keytime = cl.keyservertime = 0;
    cl.keyframe = cl.frame; // initialize keyframe to make sure it's valid
#endif

    // initialize oldframe so lerping doesn't hurt anything
    cl.oldframe.valid = false;
    cl.oldframe.ps = cl.frame.ps;
#if USE_FPS
    cl.oldkeyframe.valid = false;
    cl.oldkeyframe.ps = cl.keyframe.ps;
#endif

    cl.frameflags = 0;
    cl.initialSeq = cls.netchan.outgoing_sequence;

    if (cls.demo.playback) {
        // init some demo things
        CL_FirstDemoFrame();
    } else {
        // set initial cl.predicted_origin and cl.predicted_angles
        VectorCopy(cl.frame.ps.pmove.origin, cl.predicted_origin);
        VectorCopy(cl.frame.ps.pmove.velocity, cl.predicted_velocity);
        if (cl.frame.ps.pmove.pm_type < PM_DEAD &&
            cls.serverProtocol > PROTOCOL_VERSION_DEFAULT) {
            // enhanced servers don't send viewangles
            CL_PredictAngles();
        } else {
            // just use what server provided
            VectorCopy(cl.frame.ps.viewangles, cl.predicted_angles);
        }
        Vector4Copy(cl.frame.ps.screen_blend, cl.predicted_screen_blend);
        cl.predicted_rdflags = cl.frame.ps.rdflags;
        cl.current_viewheight = cl.prev_viewheight = cl.frame.ps.pmove.viewheight;
    }

    cl.viewheight_change_time = 0;

    cl.last_groundentity = NULL;
    memset(&cl.last_groundplane, 0, sizeof(cl.last_groundplane));

    SCR_EndLoadingPlaque();     // get rid of loading plaque
    SCR_LagClear();
    Con_Close(false);           // get rid of connection screen

    CL_CheckForPause();

    CL_UpdateFrameTimes();

    IN_Activate();

    if (!cls.demo.playback) {
        EXEC_TRIGGER(cl_beginmapcmd);
        Cmd_ExecTrigger("#cl_enterlevel");
    }
}

static void
check_player_lerp(server_frame_t *oldframe, server_frame_t *frame, int framediv)
{
    player_state_t *ps, *ops;
    const centity_t *ent;
    int oldnum;

    // find states to interpolate between
    ps = &frame->ps;
    ops = &oldframe->ps;

    // no lerping if previous frame was dropped or invalid
    if (!oldframe->valid)
        goto dup;

    oldnum = frame->number - framediv;
    if (oldframe->number != oldnum)
        goto dup;

    // no lerping if player entity was teleported (origin check)
    if (fabsf(ops->pmove.origin[0] - ps->pmove.origin[0]) > 256 ||
        fabsf(ops->pmove.origin[1] - ps->pmove.origin[1]) > 256 ||
        fabsf(ops->pmove.origin[2] - ps->pmove.origin[2]) > 256) {
        goto dup;
    }

    // no lerping if player entity was teleported (event check)
    ent = &cl_entities[frame->clientNum + 1];
    if (ent->serverframe > oldnum &&
        ent->serverframe <= frame->number &&
#if USE_FPS
        ent->event_frame > oldnum &&
        ent->event_frame <= frame->number &&
#endif
        (ent->current.event == EV_PLAYER_TELEPORT
         || ent->current.event == EV_OTHER_TELEPORT)) {
        goto dup;
    }

    // no lerping if teleport bit was flipped
    if (!cl.csr.extended && (ops->pmove.pm_flags ^ ps->pmove.pm_flags) & PMF_TELEPORT_BIT)
        goto dup;

    if (cl.csr.extended && (ops->rdflags ^ ps->rdflags) & RDF_TELEPORT_BIT)
        goto dup;

    // no lerping if POV number changed
    if (oldframe->clientNum != frame->clientNum)
        goto dup;

    // developer option
    if (cl_nolerp->integer == 1)
        goto dup;

    return;

dup:
    // duplicate the current state so lerping doesn't hurt anything
    *ops = *ps;
}

/*
==================
CL_DeltaFrame

A valid frame has been parsed.
==================
*/
void CL_DeltaFrame(void)
{
    centity_t           *ent;
    int                 i, j;
    int                 framenum;
    int                 prevstate = cls.state;
    const unsigned      accepted_frame_flags = cl.frameflags;

    // getting a valid frame message ends the connection process
    if (cls.state == ca_precached)
        set_active_state();

    // set server time
    framenum = cl.frame.number - cl.serverdelta;

    if (framenum < 0)
        Com_Error(ERR_DROP, "%s: server time went backwards", __func__);

    if (CL_FRAMETIME && framenum > INT_MAX / CL_FRAMETIME)
        Com_Error(ERR_DROP, "%s: server time overflowed", __func__);

    cl.servertime = framenum * CL_FRAMETIME;
#if USE_FPS
    cl.keyservertime = (framenum / cl.frametime.div) * BASE_FRAMETIME;
#endif

    const unsigned inferred_acked_input_cmd = cls.demo.playback
        ? 0u
        : cl.history[cls.netchan.incoming_acknowledged & CMD_MASK].cmdNumber;
    const cg_accepted_snapshot_t accepted_snapshot = {
        .snapshot_id = cl.frame.number,
        .delta_baseline_id = cl.frame.delta,
        .server_time_ms = static_cast<std::uint32_t>(cl.servertime),
        .receive_time_ms = cls.realtime,
        .frame_flags = accepted_frame_flags,
        .entity_count = static_cast<std::uint32_t>(cl.frame.numEntities),
        .inferred_acked_input_cmd = inferred_acked_input_cmd,
    };
    CG_SnapshotTimeline_RecordAccepted(accepted_snapshot);

    // rebuild the list of solid entities for this frame
    cl.numSolidEntities = 0;

    // initialize position of the player's own entity from playerstate.
    // this is needed in situations when player entity is invisible, but
    // server sends an effect referencing it's origin (such as MZ_LOGIN, etc)
    ent = &cl_entities[cl.frame.clientNum + 1];
    Com_PlayerToEntityState(&cl.frame.ps, &ent->current);

    // set current and prev, unpack solid, etc
    for (i = 0; i < cl.frame.numEntities; i++) {
        j = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        parse_entity_update(&cl.entityStates[j]);
    }

    // fire events. due to footstep tracing this must be after updating entities.
    for (i = 0; i < cl.frame.numEntities; i++) {
        j = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        parse_entity_event(cl.entityStates[j].number);
    }

    if (cls.demo.recording && !cls.demo.paused && !cls.demo.seeking && CL_FRAMESYNC) {
        CL_EmitDemoFrame();
    }

    if (prevstate == ca_precached)
        CL_GTV_Resume();
    else
        CL_GTV_EmitFrame();

    if (cls.demo.playback) {
        // this delta has nothing to do with local viewangles,
        // clear it to avoid interfering with demo freelook hack
        VectorClear(cl.frame.ps.pmove.delta_angles);
    }

    if (cl.oldframe.ps.pmove.pm_type != cl.frame.ps.pmove.pm_type) {
        IN_Activate();
    }

    check_player_lerp(&cl.oldframe, &cl.frame, 1);

#if USE_FPS
    if (CL_FRAMESYNC)
        check_player_lerp(&cl.oldkeyframe, &cl.keyframe, cl.frametime.div);
#endif

    CL_CheckPredictionError();

    CG_SnapshotTimeline_DebugTick(cls.realtime);

    SCR_SetCrosshairColor();
}

#if USE_DEBUG
// for debugging problems when out-of-date entity origin is referenced
void CL_CheckEntityPresent(int entnum, const char *what)
{
    const centity_t *e;

    if (entnum == cl.frame.clientNum + 1) {
        return; // player entity = current
    }

    e = &cl_entities[entnum];
    if (e->serverframe == cl.frame.number) {
        return; // current
    }

    if (e->serverframe) {
        Com_LPrintf(PRINT_DEVELOPER,
                    "SERVER BUG: %s on entity %d last seen %d frames ago\n",
                    what, entnum, cl.frame.number - e->serverframe);
    } else {
        Com_LPrintf(PRINT_DEVELOPER,
                    "SERVER BUG: %s on entity %d never seen before\n",
                    what, entnum);
    }
}
#endif


/*
==========================================================================

INTERPOLATE BETWEEN FRAMES TO GET RENDERING PARAMS

==========================================================================
*/

static float lerp_entity_alpha(const centity_t *ent)
{
    float prev = ent->prev.alpha;
    float curr = ent->current.alpha;

    // no lerping from/to default alpha
    if (prev && curr)
        return prev + cl.lerpfrac * (curr - prev);

    return curr ? curr : 1.0f;
}

static bool CL_IsPlayerEntity(const entity_state_t *state)
{
    return state->modelindex == MODELINDEX_PLAYER && state->number <= cl.maxclients;
}

static bool CL_UsesClientAutoAnimation(effects_t effects)
{
    return (effects & (EF_ANIM01 | EF_ANIM23 | EF_ANIM_ALL | EF_ANIM_ALLFAST)) != 0;
}

static uint32_t CL_HashEntityLightKey(uint32_t hash, uint32_t value)
{
    hash ^= value;
    return hash * 16777619u;
}

static uint32_t CL_EntityLightKey(const entity_state_t *state, effects_t effects,
                                  uint32_t source)
{
    uint32_t hash = 2166136261u;
    hash = CL_HashEntityLightKey(hash, 0x45464c54u);
    hash = CL_HashEntityLightKey(hash, state ? (uint32_t)state->number : 0u);
    hash = CL_HashEntityLightKey(hash, source);
    hash = CL_HashEntityLightKey(hash, (uint32_t)effects);
    hash = CL_HashEntityLightKey(hash, (uint32_t)(effects >> 32));
    return hash ? hash : 1u;
}

static void CL_AddEntityLightWithSource(const entity_state_t *state,
                                        effects_t effects,
                                        uint32_t source,
                                        const vec3_t org,
                                        float intensity,
                                        float r,
                                        float g,
                                        float b)
{
    V_AddLightWithKey(org, intensity, r, g, b,
                      CL_EntityLightKey(state, effects, source));
}

#define CL_AddEntityLight(state, effects, org, intensity, r, g, b) \
    CL_AddEntityLightWithSource(state, effects, __LINE__, org, intensity, r, g, b)

static bool CL_UsesExtendedModelFrameLerp(const entity_state_t *state)
{
    if (!state || !state->modelindex || (state->renderfx & RF_BEAM) ||
        CL_UsesClientAutoAnimation(state->effects))
        return false;

    if (state->modelindex == MODELINDEX_PLAYER)
        return true;

    const char *model_name = cl.configstrings[cl.csr.models + state->modelindex];
    if (!model_name || !*model_name || model_name[0] == '*')
        return false;

    const char *ext = COM_FileExtension(model_name);
    return !Q_stricmp(ext, ".md2") ||
           !Q_stricmp(ext, ".md3") ||
           !Q_stricmp(ext, ".iqm") ||
           !Q_stricmp(ext, ".md5mesh");
}

static bool CL_GetPlayerTeamInfo(const entity_state_t *state, uint8_t *team_index, bool *is_dead)
{
    if (team_index)
        *team_index = 0;
    if (is_dead)
        *is_dead = false;

    if (cl.game_api != Q2PROTO_GAME_RERELEASE || !cl.csr.extended)
        return false;

    player_skinnum_t unpacked = {};
    unpacked.skinnum = state->skinnum;

    if (team_index)
        *team_index = unpacked.team_index;
    if (is_dead)
        *is_dead = unpacked.poi_icon != 0;

    return true;
}

static color_t CL_BrightskinTeamColor(uint8_t team_index)
{
    switch (team_index) {
    case 1:
        return COLOR_RED;
    case 2:
        return COLOR_BLUE;
    default:
        return COLOR_GREEN;
    }
}

static color_t CL_SelectBrightskinColor(bool use_custom, bool is_enemy, bool is_ally, bool is_self, uint8_t team_index)
{
    if (!use_custom)
        return CL_BrightskinTeamColor(team_index);

    if (is_enemy || (!is_self && !is_ally))
        return brightskin_enemy_color;

    return brightskin_team_color;
}

static bool CL_IsThirdPersonSelf(const entity_state_t *state,
                                 int controlled_entity)
{
    return cl.thirdPersonView && controlled_entity > 0 &&
           state->number == controlled_entity;
}

static uint8_t CL_ShellColorToByte(float value)
{
    int scaled = Q_rint(value * 255.0f);
    return (uint8_t)Q_clip(scaled, 0, 255);
}

static bool CL_GetItemColorizeSettings(int model_index, color_t *color, float *strength, float *rim_strength)
{
    if (!cl_colorize_items)
        return false;

    const int flags = Cvar_ClampInteger(cl_colorize_items, 0, 3);
    if (!flags)
        return false;

    if (model_index <= 0 || model_index >= cl.csr.max_models)
        return false;

    const color_t item_color = cl.item_color_by_model[model_index];
    if (!item_color.u32)
        return false;

    *color = item_color;
    if (strength)
        *strength = (flags & 1) ? 1.0f : 0.0f;
    if (rim_strength)
        *rim_strength = (flags & 2) ? 1.0f : 0.0f;
    return (flags & 3) != 0;
}

static bool CL_ShellRimlightColor(renderfx_t flags, color_t *color)
{
    flags &= RF_SHELL_MASK;
    if (!flags)
        return false;

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    if (flags & RF_SHELL_LITE_GREEN) {
        r = 0.56f;
        g = 0.93f;
        b = 0.56f;
    }
    if (flags & RF_SHELL_HALF_DAM) {
        r = 0.56f;
        g = 0.59f;
        b = 0.45f;
    }
    if (flags & RF_SHELL_DOUBLE) {
        r = 0.25f;
        g = 0.88f;
        b = 0.82f;
    }

    if (flags & RF_SHELL_RED)
        r = 1.0f;
    if (flags & RF_SHELL_GREEN)
        g = 1.0f;
    if (flags & RF_SHELL_BLUE)
        b = 1.0f;

    if (r == 0.0f && g == 0.0f && b == 0.0f)
        return false;

    *color = COLOR_RGB(CL_ShellColorToByte(r), CL_ShellColorToByte(g), CL_ShellColorToByte(b));
    return true;
}

/*
===============
CL_AddPacketEntities

===============
*/
static void CL_AddPacketEntities(void)
{
    entity_t                ent;
    const entity_state_t    *s1;
    float                   autorotate, autobob;
    int                     i;
    int                     pnum;
    centity_t               *cent;
    int                     autoanim;
    const clientinfo_t      *ci;
    effects_t               effects;
    renderfx_t              renderfx;
    bool                    has_alpha, has_trail;
    float                   custom_alpha;
    float                   entity_alpha;
    uint64_t                custom_flags;
    float                   enemy_outline_alpha;
    float                   team_outline_alpha;
    bool                    enemy_outline_enabled;
    float                   enemy_rim_alpha;
    bool                    enemy_rim_enabled;
    float                   team_rim_alpha;
    bool                    team_outline_enabled;
    bool                    team_rim_enabled;
    float                   shell_rim_scale;
    uint8_t                 my_team;
    bool                    teamplay;
    bool                    brightskins_custom;
    bool                    brightskins_hide_dead;
    bool                    viewer_is_spectator;
    bool                    native_authority;
    int                     controlled_entity_number;
    int                     packet_entity_count;

    // bonus items rotate at a fixed rate
    autorotate = anglemod(cl.time * 0.1f);

    // brush models can auto animate their frames
    autoanim = cl.time / 500;

    autobob = 5 * sinf(cl.time / 400.0f);

    native_authority = canonical_render_frame.mode ==
        CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY;
    if (native_authority && !canonical_native_render_view.valid)
        return;
    controlled_entity_number = native_authority
        ? static_cast<int>(canonical_native_render_view.controlled_entity)
        : cl.frame.clientNum + 1;
    packet_entity_count = native_authority
        ? static_cast<int>(canonical_native_render_view.render_count)
        : cl.frame.numEntities;
    viewer_is_spectator = native_authority
        ? canonical_native_render_view.movement_type == PM_SPECTATOR ||
              canonical_native_render_view.movement_type == PM_FREEZE
        : CL_IsSpectatorView();
    enemy_outline_alpha = Cvar_ClampValue(cl_player_outline_enemy, 0.0f, 1.0f);
    team_outline_alpha = Cvar_ClampValue(cl_player_outline_team, 0.0f, 1.0f);
    enemy_outline_enabled = enemy_outline_alpha > 0.0f;
    team_outline_enabled = team_outline_alpha > 0.0f;
    enemy_rim_alpha = Cvar_ClampValue(cl_player_rimlight_enemy, 0.0f, 1.0f);
    team_rim_alpha = Cvar_ClampValue(cl_player_rimlight_team, 0.0f, 1.0f);
    enemy_rim_enabled = enemy_rim_alpha > 0.0f;
    team_rim_enabled = team_rim_alpha > 0.0f;
    shell_rim_scale = cl_player_rimlight_shell ? Cvar_ClampValue(cl_player_rimlight_shell, 0.0f, 1.0f) : 0.0f;
    my_team = native_authority
        ? canonical_native_render_view.team_id
        : cl.frame.ps.team_id;
    teamplay = my_team != 0;
    brightskins_custom = cl_brightskins_custom->integer != 0 && !viewer_is_spectator;
    brightskins_hide_dead = cl_brightskins_dead->integer != 0;
    CL_UpdateForcedModels();

    memset(&ent, 0, sizeof(ent));

    for (pnum = 0; pnum < packet_entity_count; pnum++) {
        canonical_native_render_cache_entry_t *native_entry = nullptr;
        bool native_previous_only_source = false;
        if (native_authority) {
            const std::uint32_t identity =
                canonical_native_render_view.render_identities[pnum];
            auto &entry = canonical_native_render_view.cache[identity];
            if (!entry.valid)
                continue;
            cent = &entry.entity;
            s1 = &cent->current;
            native_entry = &entry;
            native_previous_only_source =
                canonical_native_render_view.render_previous_only[pnum] !=
                0u;
        } else {
            i = (cl.frame.firstEntity + pnum) & PARSE_ENTITIES_MASK;
            s1 = &cl.entityStates[i];
            cent = &cl_entities[s1->number];
        }
        bool native_source_submitted = false;
        const auto submit_packet_entity = [&](const entity_t *submitted) {
            V_AddEntity(submitted);
            if (native_authority && native_entry) {
                record_canonical_renderer_submission(
                    *native_entry, *s1, *submitted,
                    native_previous_only_source,
                    native_source_submitted);
            }
        };

        // handled elsewhere
        if (s1->renderfx & RF_CASTSHADOW) {
            continue;
        }

        ent.id = cent->id + RESERVED_ENTITY_COUNT;
        ent.owner_entity = s1->number;

        worr_snapshot_timeline_entity_sample_v1 canonical_sample{};
        bool canonical_transform = native_authority;
        if (native_authority) {
            VectorCopy(s1->origin, canonical_sample.entity.origin);
            VectorCopy(s1->old_origin,
                       canonical_sample.entity.old_origin);
            VectorCopy(s1->angles, canonical_sample.entity.angles);
        } else if (s1->number != controlled_entity_number) {
            canonical_transform = sample_canonical_entity_transform(
                s1, cent, &canonical_sample);
        }

        has_trail = false;
        bool is_player = CL_IsPlayerEntity(s1);
        const clientinfo_t *render_ci = NULL;
        qhandle_t brightskin = 0;
        bool is_self = false;
        uint8_t other_team = 0;
        bool is_dead = false;
        bool has_team_info = false;
        bool is_enemy = false;
        bool is_ally = false;
        bool valid_player = false;
        bool shell_rim_active = false;
        color_t shell_rim_color = COLOR_WHITE;
        float shell_rim_alpha = 0.0f;

        effects = s1->effects;
        renderfx = s1->renderfx;

        // set frame
        if (effects & EF_ANIM01)
            ent.frame = autoanim & 1;
        else if (effects & EF_ANIM23)
            ent.frame = 2 + (autoanim & 1);
        else if (effects & EF_ANIM_ALL)
            ent.frame = autoanim;
        else if (effects & EF_ANIM_ALLFAST)
            ent.frame = cl.time / 100;
        else
            ent.frame = s1->frame;

        // quad and pent can do different things on client
        if (effects & EF_PENT) {
            effects &= ~EF_PENT;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_RED;
        }

        if (effects & EF_QUAD) {
            effects &= ~EF_QUAD;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_BLUE;
        }

        if (effects & EF_DOUBLE) {
            effects &= ~EF_DOUBLE;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_DOUBLE;
        }

        if (effects & EF_HALF_DAMAGE) {
            effects &= ~EF_HALF_DAMAGE;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_HALF_DAM;
        }

        if (effects & EF_DUALFIRE) {
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_LITE_GREEN;
        }

        // optionally remove the glowing effect
        if (cl_noglow->integer && !(renderfx & RF_BEAM))
            renderfx &= ~RF_GLOW;

        if (native_authority) {
            ent.oldframe = s1->old_frame >= 0
                ? s1->old_frame
                : s1->frame;
            ent.backlerp = 0.0f;
        } else {
            ent.oldframe = cent->prev.frame;
            ent.backlerp = 1.0f - cl.lerpfrac;
        }

// KEX
        if (!native_authority && cl.csr.extended &&
            CL_UsesExtendedModelFrameLerp(s1)) {
            if (cent->last_frame != cent->current_frame) {
                ent.backlerp = Q_clipf(1.0f - ((cl.time - ((float) cent->frame_servertime - cl.frametime.time)) / 100.f), 0.0f, 1.0f);
                ent.frame = cent->current_frame;
                ent.oldframe = cent->last_frame;
            }
        }
// KEX

        if (renderfx & RF_BEAM) {
            // interpolate start and end points for beams
            if (canonical_transform) {
                VectorCopy(canonical_sample.entity.origin, ent.origin);
                VectorCopy(canonical_sample.entity.old_origin, ent.oldorigin);
            } else {
                LerpVector(cent->prev.origin, cent->current.origin,
                           cl.lerpfrac, ent.origin);
                LerpVector(cent->prev.old_origin, cent->current.old_origin,
                           cl.lerpfrac, ent.oldorigin);
            }
        } else {
            if (s1->number == controlled_entity_number) {
                // use predicted origin
                VectorCopy(cl.playerEntityOrigin, ent.origin);
                VectorCopy(cl.playerEntityOrigin, ent.oldorigin);
            } else if (canonical_transform) {
                VectorCopy(canonical_sample.entity.origin, ent.origin);
                VectorCopy(ent.origin, ent.oldorigin);
            } else {
                // interpolate origin
                LerpVector(cent->prev.origin, cent->current.origin,
                           cl.lerpfrac, ent.origin);
                VectorCopy(ent.origin, ent.oldorigin);
            }
#if USE_FPS
            // run alias model animation
            if (!native_authority && cent->prev_frame != s1->frame) {
                int delta = cl.time - cent->anim_start;
                float frac;

                if (delta > BASE_FRAMETIME) {
                    cent->prev_frame = s1->frame;
                    frac = 1;
                } else if (delta > 0) {
                    frac = delta * BASE_1_FRAMETIME;
                } else {
                    frac = 0;
                }

                ent.oldframe = cent->prev_frame;
                ent.backlerp = 1.0f - frac;
            }
#endif
        }

        if (effects & EF_BOB && !cl_nobob->integer) {
            ent.origin[2] += autobob;
            ent.oldorigin[2] += autobob;
        }

        // use predicted values
        unsigned step_delta = cls.realtime - cent->stair_time;

        // smooth out stair climbing
        if (step_delta < STEP_TIME && cent->stair_height) {
            float step_change = cent->stair_height * ((STEP_TIME - step_delta) * (1.f / STEP_TIME));
            ent.origin[2] = cent->current.origin[2] - step_change;
            ent.oldorigin[2] = ent.origin[2];
        }

        // bottom Z position, for shadow fading
        ent.bottom_z = ent.origin[2] + cent->mins[2];

        if (!cl_gibs->integer) {
            if (effects & EF_GIB && !(cl.csr.extended && effects & EF_ROCKET))
                goto skip;
            if (effects & EF_GREENGIB)
                goto skip;
        }

        // create a new entity

        if (cl.csr.extended) {
            if (renderfx & RF_FLARE) {
                if (!cl_flares->integer)
                    goto skip;
                float fade_start = s1->modelindex2;
                float fade_end = s1->modelindex3;
                float d = Distance(cl.refdef.vieworg, ent.origin);
                if (d < fade_start)
                    goto skip;
                if (d > fade_end)
                    ent.alpha = 1;
                else
                    ent.alpha = (d - fade_start) / (fade_end - fade_start);
                ent.skin = 0;
                if (renderfx & RF_CUSTOMSKIN && (unsigned)s1->frame < cl.csr.max_images)
                    ent.skin = cl.image_precache[s1->frame];
                if (!ent.skin)
                    ent.skin = cl_img_flare;
                float s = s1->scale ? s1->scale : 1;
                VectorSet(ent.scale, s, s, s);
                ent.flags = renderfx | RF_TRANSLUCENT;
                if (!s1->skinnum)
                    ent.rgba = COLOR_WHITE;
                else
                    ent.rgba.u32 = BigLong(s1->skinnum);
                ent.skinnum = s1->number;
                submit_packet_entity(&ent);
                goto skip;
            }

            if (renderfx & RF_CUSTOM_LIGHT) {
                color_t color;
                if (!s1->skinnum)
                    color = COLOR_WHITE;
                else
                    color.u32 = BigLong(s1->skinnum);
                CL_AddEntityLight(s1, effects, ent.origin, DLIGHT_CUTOFF + s1->frame,
                                  color.r / 255.0f,
                                  color.g / 255.0f,
                                  color.b / 255.0f);
                goto skip;
            }

            if ((renderfx & RF_BEAM) && s1->modelindex > 1) {
                CL_DrawBeam(ent.origin, ent.oldorigin, ent.frame, cl.model_draw[s1->modelindex]);
                goto skip;
            }
        }

        // tweak the color of beams
        if (renderfx & RF_BEAM) {
            // the four beam colors are encoded in 32 bits of skinnum (hack)
            ent.alpha = 0.30f;
            ent.skinnum = (s1->skinnum >> ((Com_SlowRand() % 4) * 8)) & 0xff;
            ent.model = 0;
        } else {
            // set skin
            if (s1->modelindex == MODELINDEX_PLAYER) {
                // use custom player skin
                ent.skinnum = 0;
                ci = &cl.clientinfo[s1->skinnum & 0xff];
                ent.skin = ci->skin;
                ent.model = ci->model;
                if (!ent.skin || !ent.model) {
                    ent.skin = cl.baseclientinfo.skin;
                    ent.model = cl.baseclientinfo.model;
                    ci = &cl.baseclientinfo;
                }
                render_ci = ci;
                brightskin = ci->brightskin;
                if (renderfx & RF_USE_DISGUISE) {
                    char buffer[MAX_QPATH];

                    Q_concat(buffer, sizeof(buffer), "players/", ci->model_name, "/disguise.pcx");
                    ent.skin = R_RegisterSkin(buffer);
                    Q_concat(buffer, sizeof(buffer), "players/", ci->model_name, "/disguise_brtskn.png");
                    brightskin = R_RegisterImage(buffer, IT_SKIN,
                                                 static_cast<imageflags_t>(IF_KEEP_EXTENSION | IF_OPTIONAL));
                }
            } else {
                ent.skinnum = s1->skinnum;
                ent.skin = 0;
                ent.model = cl.model_draw[s1->modelindex];
                if (ent.model == cl_mod_laser || ent.model == cl_mod_dmspot)
                    renderfx |= RF_NOSHADOW;
            }
        }

        // allow skin override for remaster
        if (cl.csr.extended && renderfx & RF_CUSTOMSKIN && (unsigned)s1->skinnum < cl.csr.max_images) {
            ent.skin = cl.image_precache[s1->skinnum];
            ent.skinnum = 0;
            brightskin = 0;
        }

        // only used for black hole model right now, FIXME: do better
        if ((renderfx & RF_TRANSLUCENT) && !(renderfx & RF_BEAM))
            ent.alpha = 0.70f;

        // render effects (fullbright, translucent, etc)
        if (effects & EF_COLOR_SHELL)
            ent.flags = 0;  // renderfx go on color shell entity
        else
            ent.flags = renderfx;
        {
            const effects_t shadow_skip_effects =
                EF_ROCKET | EF_BLASTER | EF_HYPERBLASTER | EF_GRENADE |
                EF_BFG | EF_TRAP | EF_TRACKERTRAIL | EF_TRACKER |
                EF_IONRIPPER | EF_PLASMA | EF_BLUEHYPERBLASTER |
                EF_GRENADE_LIGHT;
            if (effects & shadow_skip_effects)
                ent.flags |= RF_NOSHADOW;
        }

        // calculate angles
        if (effects & EF_ROTATE) {  // some bonus items auto-rotate
            ent.angles[0] = 0;
            ent.angles[1] = autorotate;
            ent.angles[2] = 0;
        } else if (effects & EF_SPINNINGLIGHTS) {
            vec3_t forward;
            vec3_t start;

            ent.angles[0] = 0;
            ent.angles[1] = anglemod(cl.time / 2) + s1->angles[1];
            ent.angles[2] = 180;

            AngleVectors(ent.angles, forward, NULL, NULL);
            VectorMA(ent.origin, 64, forward, start);
            CL_AddEntityLight(s1, effects, start, 100, 1, 0, 0);
        } else if (s1->number == controlled_entity_number) {
            VectorCopy(cl.playerEntityAngles, ent.angles);      // use predicted angles
        } else if (canonical_transform) {
            VectorCopy(canonical_sample.entity.angles, ent.angles);
            if (s1->modelindex == MODELINDEX_PLAYER && cl_rollhack->integer && !cl.csr.extended)
                ent.angles[ROLL] = -ent.angles[ROLL];
        } else { // interpolate angles
            LerpAngles(cent->prev.angles, cent->current.angles,
                       cl.lerpfrac, ent.angles);
            // mimic original ref_gl "leaning" bug (uuugly!)
            if (s1->modelindex == MODELINDEX_PLAYER && cl_rollhack->integer && !cl.csr.extended)
                ent.angles[ROLL] = -ent.angles[ROLL];
        }

        if (effects & EF_FLASHLIGHT) {
            vec3_t forward, start, end;
            trace_t trace;
            contents_t mask = CONTENTS_SOLID;
            bool is_per_pixel = cl_shadowlights->integer && R_SupportsPerPixelLighting();
            
            if (!is_per_pixel)
                mask |= CONTENTS_MONSTER | CONTENTS_PLAYER;

            if (s1->number == controlled_entity_number) {
                float dist = is_per_pixel ? FLASHLIGHT_SHADOW_DISTANCE : FLASHLIGHT_CLASSIC_DISTANCE;
                VectorCopy(cl.refdef.vieworg, start);
                VectorCopy(cl.v_forward, forward);
                if (is_per_pixel) {
                    VectorMA(start, FLASHLIGHT_TORSO_FORWARD_OFFSET, cl.v_forward, start);
                    VectorMA(start, -FLASHLIGHT_TORSO_DOWN_OFFSET, cl.v_up, start);
                    CL_ApplyFlashlightTorsoMotion(start, forward);
                }
                VectorMA(start, dist, forward, end);
            } else {
                AngleVectors(ent.angles, forward, NULL, NULL);
                float dist = is_per_pixel ? FLASHLIGHT_SHADOW_DISTANCE : FLASHLIGHT_CLASSIC_DISTANCE;
                VectorMA(ent.origin, dist, forward, end);
                VectorCopy(ent.origin, start);
            }

            CL_Trace(&trace, start, end, vec3_origin, vec3_origin, NULL, mask);

            if (is_per_pixel) {
                cl_shadow_light_t light = {};
                light.fade_end = light.fade_start = 0;
                light.lightstyle = -1;
                light.resolution = FLASHLIGHT_SHADOW_RESOLUTION;
                light.intensity = FLASHLIGHT_SHADOW_INTENSITY;
                light.radius = FLASHLIGHT_SHADOW_RADIUS;
                light.coneangle = FLASHLIGHT_SHADOW_CONE_ANGLE;
                VectorCopy(forward, light.conedirection);
                light.color = COLOR_WHITE;
                light.owner_entity = s1->number;
                light.source_index = -1;
                light.strict_pvs = true;
                light.ignore_owner_casters = true;
                light.dynamic_shadow = true;
                VectorCopy(start, light.origin);
                V_AddLightEx(&light);
            } else {
                // smooth out distance "jumps"
                LerpVector(start, end, cent->flashlightfrac, end);
                CL_AddEntityLight(s1, effects, end, 256, 1, 1, 1);
                CL_AdvanceValue(&cent->flashlightfrac, trace.fraction, 1);
            }
        }

        if (effects & EF_GRENADE_LIGHT)
            CL_AddEntityLight(s1, effects, ent.origin, 100, 1, 1, 0);

        if (s1->number == controlled_entity_number && !cl.thirdPersonView) {
            if (effects & EF_FLAG1)
                CL_AddEntityLight(s1, effects, ent.origin, 225, 1.0f, 0.1f, 0.1f);
            else if (effects & EF_FLAG2)
                CL_AddEntityLight(s1, effects, ent.origin, 225, 0.1f, 0.1f, 1.0f);
            else if (effects & EF_TAGTRAIL)
                CL_AddEntityLight(s1, effects, ent.origin, 225, 1.0f, 1.0f, 0.0f);
            else if (effects & EF_TRACKERTRAIL)
                CL_AddEntityLight(s1, effects, ent.origin, 225, -1.0f, -1.0f, -1.0f);
            // Add a shadow-only entity so first-person players still cast shadows.
            {
                const effects_t shadow_skip_effects =
                    EF_ROCKET | EF_BLASTER | EF_HYPERBLASTER | EF_GRENADE |
                    EF_BFG | EF_TRAP | EF_TRACKERTRAIL | EF_TRACKER |
                    EF_IONRIPPER | EF_PLASMA | EF_BLUEHYPERBLASTER |
                    EF_GRENADE_LIGHT;
                entity_t shadow_ent = ent;
                shadow_ent.flags = renderfx | RF_VIEWERMODEL | RF_CASTSHADOW;
                if (effects & shadow_skip_effects)
                    shadow_ent.flags |= RF_NOSHADOW;
                V_AddEntity(&shadow_ent);
            }
            goto skip;
        }

        // if set to invisible, skip
        if (!s1->modelindex)
            goto skip;

        if (effects & EF_BFG) {
            ent.flags |= RF_TRANSLUCENT;
            ent.alpha = 0.30f;
        }

        if (effects & EF_PLASMA) {
            ent.flags |= RF_TRANSLUCENT;
            ent.alpha = 0.6f;
        }

        if (effects & EF_SPHERETRANS) {
            ent.flags |= RF_TRANSLUCENT;
            if (effects & EF_TRACKERTRAIL)
                ent.alpha = 0.6f;
            else
                ent.alpha = 0.3f;
        }

        // custom alpha overrides any derived value
        custom_alpha = 1.0f;
        custom_flags = 0;
        has_alpha = false;

        if (s1->alpha) {
            custom_alpha = native_authority
                ? s1->alpha
                : lerp_entity_alpha(cent);
            has_alpha = true;
        }

        if (s1->number == controlled_entity_number && cl.thirdPersonView && cl.thirdPersonAlpha != 1.0f) {
            custom_alpha *= cl.thirdPersonAlpha;
            has_alpha = true;
        }

        if (has_alpha) {
            ent.alpha = custom_alpha;
            if (custom_alpha == 1.0f)
                ent.flags &= ~RF_TRANSLUCENT;
            else
                ent.flags |= RF_TRANSLUCENT;
            custom_flags = ent.flags & RF_TRANSLUCENT;
        }
        entity_alpha = (ent.flags & RF_TRANSLUCENT) ? ent.alpha : 1.0f;

        // tracker effect is duplicated for linked models
        if (IS_TRACKER(effects)) {
            ent.flags    |= RF_TRACKER;
            custom_flags |= RF_TRACKER;
        }

        VectorSet(ent.scale, s1->scale, s1->scale, s1->scale);

        is_self = is_player &&
            CL_IsThirdPersonSelf(s1, controlled_entity_number);
        other_team = 0;
        is_dead = false;
        has_team_info = is_player && CL_GetPlayerTeamInfo(s1, &other_team, &is_dead);
        is_enemy = false;
        is_ally = false;
        valid_player = is_player && !is_dead;

        if (is_player && !is_self) {
            if (teamplay && has_team_info) {
                if (other_team) {
                    if (other_team == my_team)
                        is_ally = true;
                    else
                        is_enemy = true;
                }
            } else {
                is_enemy = true;
            }
        }

        if (!viewer_is_spectator && is_player && !is_self &&
            !(renderfx & (RF_CUSTOMSKIN | RF_USE_DISGUISE))) {
            const forced_model_t *forced = NULL;

            if (is_enemy && cl_forced_enemy_model.active)
                forced = &cl_forced_enemy_model;
            else if (is_ally && cl_forced_team_model.active)
                forced = &cl_forced_team_model;

            if (forced && forced->info.model && forced->info.skin) {
                ent.skin = forced->info.skin;
                ent.model = forced->info.model;
                brightskin = forced->info.brightskin;
                render_ci = &forced->info;
            }
        }

        if (enemy_outline_enabled || team_outline_enabled || enemy_rim_enabled || team_rim_enabled) {
            bool self_effects = is_self && valid_player && cl.thirdPersonView;
            bool outline_enemy = enemy_outline_enabled && is_enemy && valid_player;
            bool outline_team = team_outline_enabled && is_ally && valid_player;
            bool outline_self = team_outline_enabled && self_effects;
            bool rim_enemy = enemy_rim_enabled && is_enemy && valid_player;
            bool rim_team = team_rim_enabled && is_ally && valid_player;
            bool rim_self = team_rim_enabled && self_effects;

            if (outline_enemy || outline_team || outline_self) {
                uint8_t team_index = is_self ? my_team : other_team;
                color_t outline_color = CL_SelectBrightskinColor(brightskins_custom, is_enemy, is_ally, is_self, team_index);
                float outline_scale = outline_enemy ? enemy_outline_alpha : team_outline_alpha;
                float outline_alpha = entity_alpha * outline_scale * (outline_color.a * (1.0f / 255.0f));

                if (outline_alpha > 0.0f) {
                    ent.rgba = COLOR_SETA_F(outline_color, outline_alpha);
                    ent.flags |= RF_OUTLINE;
                    if (outline_team)
                        ent.flags |= RF_OUTLINE_NODEPTH;
                }
            }

            if (rim_enemy || rim_team || rim_self) {
                float rim_scale = rim_enemy ? enemy_rim_alpha : team_rim_alpha;
                uint8_t team_index = is_self ? my_team : other_team;
                color_t rim_color = CL_SelectBrightskinColor(brightskins_custom, is_enemy, is_ally, is_self, team_index);
                float rim_alpha = entity_alpha * rim_scale * (rim_color.a * (1.0f / 255.0f));

                if (rim_alpha > 0.0f) {
                    entity_t rim = ent;
                    rim.flags = RF_RIMLIGHT | RF_TRANSLUCENT;
                    rim.alpha = rim_alpha;
                    rim.rgba = rim_color;
                    V_AddEntity(&rim);
                }
            }
        }

        if (!is_player) {
            color_t item_color;
            float item_strength = 0.0f;
            float item_rim_strength = 0.0f;
            if (CL_GetItemColorizeSettings(s1->modelindex, &item_color, &item_strength, &item_rim_strength)) {
                ent.rgba = item_color;
                if (item_strength > 0.0f) {
                    ent.flags |= RF_ITEM_COLORIZE;
                    ent.rgba.a = (uint8_t)Q_clip(Q_rint(item_strength * 255.0f), 0, 255);
                }

                if (item_rim_strength > 0.0f) {
                    entity_t rim = ent;
                    rim.flags = RF_RIMLIGHT | RF_TRANSLUCENT;
                    rim.alpha = entity_alpha * item_rim_strength;
                    rim.rgba = item_color;
                    V_AddEntity(&rim);
                }
            }
        }

        // add to renderer list
        submit_packet_entity(&ent);

        if (is_player && brightskin && (!brightskins_hide_dead || !is_dead)) {
            uint8_t team_index = is_self ? my_team : other_team;
            color_t bright_color = CL_SelectBrightskinColor(brightskins_custom, is_enemy, is_ally, is_self, team_index);
            float bright_alpha = entity_alpha * (bright_color.a * (1.0f / 255.0f));
            if (bright_alpha > 0.0f) {
                entity_t bright = ent;
                bright.skin = brightskin;
                bright.skinnum = 0;
                bright.flags = RF_FULLBRIGHT | RF_TRANSLUCENT | RF_BRIGHTSKIN | RF_NOSHADOW;
                bright.alpha = bright_alpha;
                bright.rgba = bright_color;
                V_AddEntity(&bright);
            }
        }

        // color shells generate a separate entity for the main model
        if (effects & EF_COLOR_SHELL) {
            // PMM - at this point, all of the shells have been handled
            // if we're in the rogue pack, set up the custom mixing, otherwise just
            // keep going
            if (!strcmp(fs_game->string, "rogue")) {
                // all of the solo colors are fine.  we need to catch any of the combinations that look bad
                // (double & half) and turn them into the appropriate color, and make double/quad something special
                if (renderfx & RF_SHELL_HALF_DAM) {
                    // ditch the half damage shell if any of red, blue, or double are on
                    if (renderfx & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE))
                        renderfx &= ~RF_SHELL_HALF_DAM;
                }

                if (renderfx & RF_SHELL_DOUBLE) {
                    // lose the yellow shell if we have a red, blue, or green shell
                    if (renderfx & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_GREEN))
                        renderfx &= ~RF_SHELL_DOUBLE;
                    // if we have a red shell, turn it to purple by adding blue
                    if (renderfx & RF_SHELL_RED)
                        renderfx |= RF_SHELL_BLUE;
                    // if we have a blue shell (and not a red shell), turn it to cyan by adding green
                    else if (renderfx & RF_SHELL_BLUE) {
                        // go to green if it's on already, otherwise do cyan (flash green)
                        if (renderfx & RF_SHELL_GREEN)
                            renderfx &= ~RF_SHELL_BLUE;
                        else
                            renderfx |= RF_SHELL_GREEN;
                    }
                }
            }

            if (shell_rim_scale > 0.0f && is_player) {
                color_t rim_color;
                if (CL_ShellRimlightColor(renderfx, &rim_color)) {
                    float rim_alpha = entity_alpha * (0.30f * shell_rim_scale);
                    if (rim_alpha > 0.0f) {
                        entity_t rim = ent;
                        rim.flags = RF_RIMLIGHT | RF_TRANSLUCENT;
                        rim.alpha = rim_alpha;
                        rim.rgba = rim_color;
                        V_AddEntity(&rim);
                        shell_rim_active = true;
                        shell_rim_color = rim_color;
                        shell_rim_alpha = rim_alpha;
                    }
                }
            }
            ent.flags = renderfx | RF_TRANSLUCENT;
            ent.alpha = custom_alpha * 0.30f;
            submit_packet_entity(&ent);
        }

        ent.skin = 0;       // never use a custom skin on others
        ent.skinnum = 0;
        ent.flags = custom_flags;
        ent.alpha = custom_alpha;

        // duplicate for linked models
        if (s1->modelindex2) {
            if (s1->modelindex2 == MODELINDEX_PLAYER) {
                // custom weapon
                const clientinfo_t *weapon_ci = render_ci;

                if (cl.game_api == Q2PROTO_GAME_RERELEASE) {
                    player_skinnum_t unpacked = {};
                    unpacked.skinnum = s1->skinnum;
                    ci = &cl.clientinfo[unpacked.client_num];
                    i = unpacked.vwep_index;
                } else {
                    ci = &cl.clientinfo[s1->skinnum & 0xff];
                    i = (s1->skinnum >> 8); // 0 is default weapon model
                }
                if (!weapon_ci)
                    weapon_ci = ci;
                if (i < 0 || i > cl.numWeaponModels - 1)
                    i = 0;
                ent.model = weapon_ci->weaponmodel[i];
                if (!ent.model) {
                    if (i != 0)
                        ent.model = weapon_ci->weaponmodel[0];
                    if (!ent.model)
                        ent.model = cl.baseclientinfo.weaponmodel[0];
                }
            } else
                ent.model = cl.model_draw[s1->modelindex2];

            // PMM - check for the defender sphere shell .. make it translucent
            if (!Q_strcasecmp(cl.configstrings[cl.csr.models + s1->modelindex2], "models/items/shell/tris.md2")) {
                ent.alpha = custom_alpha * 0.32f;
                ent.flags = RF_TRANSLUCENT;
            }

            submit_packet_entity(&ent);

            if (shell_rim_active) {
                entity_t rim = ent;
                rim.flags = RF_RIMLIGHT | RF_TRANSLUCENT;
                rim.alpha = shell_rim_alpha;
                rim.rgba = shell_rim_color;
                V_AddEntity(&rim);
            }

            //PGM - make sure these get reset.
            ent.flags = custom_flags;
            ent.alpha = custom_alpha;
        }

        if (s1->modelindex3) {
            ent.model = cl.model_draw[s1->modelindex3];
            submit_packet_entity(&ent);
        }

        if (s1->modelindex4) {
            ent.model = cl.model_draw[s1->modelindex4];
            submit_packet_entity(&ent);
        }

        if (effects & EF_POWERSCREEN) {
            ent.model = cl_mod_powerscreen;
            ent.oldframe = 0;
            ent.frame = 0;
            ent.flags = RF_TRANSLUCENT;
            ent.alpha = custom_alpha * 0.30f;

            // remaster powerscreen is tiny and needs scaling
            if (cl.need_powerscreen_scale) {
                vec3_t forward, mid, tmp;
                VectorCopy(ent.origin, tmp);
                VectorAvg(cent->mins, cent->maxs, mid);
                VectorAdd(ent.origin, mid, ent.origin);
                AngleVectors(ent.angles, forward, NULL, NULL);
                VectorMA(ent.origin, cent->maxs[0], forward, ent.origin);
                float s = cent->radius * 0.8f;
                VectorSet(ent.scale, s, s, s);
                ent.flags |= RF_FULLBRIGHT;
                submit_packet_entity(&ent);
                VectorCopy(tmp, ent.origin);
            } else {
                ent.flags |= RF_SHELL_GREEN;
                submit_packet_entity(&ent);
            }
        }

        if (effects & EF_HOLOGRAM)
            CL_HologramParticles(ent.origin);

        // add automatic particle trails
        if (!(effects & EF_TRAIL_MASK))
            goto skip;

        if (effects & EF_ROCKET) {
            if (cl.csr.extended && effects & EF_GIB) {
                CL_DiminishingTrail(cent, ent.origin, DT_FIREBALL);
                has_trail = true;
            } else if (!(cl_disable_particles->integer & NOPART_ROCKET_TRAIL)) {
                CL_DiminishingTrail(cent, ent.origin, DT_ROCKET);
                has_trail = true;
            }
            if (cl_dlight_hacks->integer & DLHACK_ROCKET_COLOR)
                CL_AddEntityLight(s1, effects, ent.origin, 200, 1, 0.23f, 0);
            else
                CL_AddEntityLight(s1, effects, ent.origin, 200, 1, 1, 0);
        } else if (effects & EF_BLASTER) {
            if (effects & EF_TRACKER) {
                CL_BlasterTrail2(cent, ent.origin);
                CL_AddEntityLight(s1, effects, ent.origin, 200, 0, 1, 0);
                has_trail = true;
            } else {
                if (!(cl_disable_particles->integer & NOPART_BLASTER_TRAIL)) {
                    CL_BlasterTrail(cent, ent.origin);
                    has_trail = true;
                }
                CL_AddEntityLight(s1, effects, ent.origin, 200, 1, 1, 0);
            }
        } else if (effects & EF_HYPERBLASTER) {
            if (effects & EF_TRACKER)
                CL_AddEntityLight(s1, effects, ent.origin, 200, 0, 1, 0);
            else
                CL_AddEntityLight(s1, effects, ent.origin, 200, 1, 1, 0);
        } else if (effects & EF_GIB) {
            CL_DiminishingTrail(cent, ent.origin, DT_GIB);
            has_trail = true;
        } else if (effects & EF_GRENADE) {
            if (!(cl_disable_particles->integer & NOPART_GRENADE_TRAIL)) {
                CL_DiminishingTrail(cent, ent.origin, DT_GRENADE);
                has_trail = true;
            }
        } else if (effects & EF_FLIES) {
            CL_FlyEffect(cent, ent.origin);
        } else if (effects & EF_BFG) {
            static const uint16_t bfg_lightramp[6] = {300, 400, 600, 300, 150, 75};
            if (effects & EF_ANIM_ALLFAST) {
                CL_BfgParticles(&ent);
                i = 200;
            } else if (cl.csr.extended || cl_smooth_explosions->integer) {
                i = bfg_lightramp[Q_clip(ent.oldframe, 0, 5)] * ent.backlerp +
                    bfg_lightramp[Q_clip(ent.frame,    0, 5)] * (1.0f - ent.backlerp);
            } else {
                i = bfg_lightramp[Q_clip(s1->frame, 0, 5)];
            }
            CL_AddEntityLight(s1, effects, ent.origin, i, 0, 1, 0);
        } else if (effects & EF_TRAP) {
            ent.origin[2] += 32;
            CL_TrapParticles(cent, ent.origin);
            i = (Com_SlowRand() % 100) + 100;
            CL_AddEntityLight(s1, effects, ent.origin, i, 1, 0.8f, 0.1f);
        } else if (effects & EF_FLAG1) {
            CL_FlagTrail(cent, ent.origin, 242);
            CL_AddEntityLight(s1, effects, ent.origin, 225, 1, 0.1f, 0.1f);
            has_trail = true;
        } else if (effects & EF_FLAG2) {
            CL_FlagTrail(cent, ent.origin, 115);
            CL_AddEntityLight(s1, effects, ent.origin, 225, 0.1f, 0.1f, 1);
            has_trail = true;
        } else if (effects & EF_TAGTRAIL) {
            CL_TagTrail(cent, ent.origin, 220);
            CL_AddEntityLight(s1, effects, ent.origin, 225, 1.0f, 1.0f, 0.0f);
            has_trail = true;
        } else if (effects & EF_TRACKERTRAIL) {
            if (effects & EF_TRACKER) {
                float intensity = 50 + (500 * (sinf(cl.time / 500.0f) + 1.0f));
                CL_AddEntityLight(s1, effects, ent.origin, intensity, -1.0f, -1.0f, -1.0f);
            } else {
                CL_Tracker_Shell(cent, ent.origin);
                CL_AddEntityLight(s1, effects, ent.origin, 155, -1.0f, -1.0f, -1.0f);
            }
        } else if (effects & EF_TRACKER) {
            CL_TrackerTrail(cent, ent.origin);
            CL_AddEntityLight(s1, effects, ent.origin, 200, -1, -1, -1);
            has_trail = true;
        } else if (effects & EF_GREENGIB) {
            CL_DiminishingTrail(cent, ent.origin, DT_GREENGIB);
            has_trail = true;
        } else if (effects & EF_IONRIPPER) {
            CL_IonripperTrail(cent, ent.origin);
            CL_AddEntityLight(s1, effects, ent.origin, 100, 1, 0.5f, 0.5f);
            has_trail = true;
        } else if (effects & EF_BLUEHYPERBLASTER) {
            CL_AddEntityLight(s1, effects, ent.origin, 200, 0, 0, 1);
        } else if (effects & EF_PLASMA) {
            if (effects & EF_ANIM_ALLFAST) {
                CL_BlasterTrail(cent, ent.origin);
                has_trail = true;
            }
            CL_AddEntityLight(s1, effects, ent.origin, 130, 1, 0.5f, 0.5f);
        }

skip:
        if (!has_trail)
            VectorCopy(ent.origin, cent->lerp_origin);
    }
}

static const centity_t *get_player_entity(void)
{
    if (canonical_render_frame.mode ==
        CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY) {
        if (!canonical_native_render_view.valid ||
            canonical_native_render_view.controlled_entity == 0u ||
            canonical_native_render_view.controlled_entity >=
                canonical_native_render_view.cache.size()) {
            return NULL;
        }
        const auto &entry = canonical_native_render_view.cache[
            canonical_native_render_view.controlled_entity];
        if (!entry.valid || !entry.entity.current.modelindex)
            return NULL;
        return &entry.entity;
    }

    const centity_t *ent = &cl_entities[cl.frame.clientNum + 1];

    if (ent->serverframe != cl.frame.number)
        return NULL;
    if (!ent->current.modelindex)
        return NULL;

    return ent;
}

static int shell_effect_hack(const centity_t *ent)
{
    int flags = 0;

    if (ent->current.effects & EF_PENT)
        flags |= RF_SHELL_RED;
    if (ent->current.effects & EF_QUAD)
        flags |= RF_SHELL_BLUE;
    if (ent->current.effects & EF_DOUBLE)
        flags |= RF_SHELL_DOUBLE;
    if (ent->current.effects & EF_HALF_DAMAGE)
        flags |= RF_SHELL_HALF_DAM;

    if (cl.csr.extended) {
        if (ent->current.effects & EF_DUALFIRE)
            flags |= RF_SHELL_LITE_GREEN;
        if (ent->current.effects & EF_COLOR_SHELL)
            flags |= ent->current.renderfx & RF_SHELL_MASK;
    }

    return flags;
}

/*
==============
CL_AddViewWeapon
==============
*/
static void CL_AddViewWeapon(void)
{
    const centity_t *ent;
    const player_state_t *ps, *ops;
    entity_t    gun;        // view model
    int         flags;
    bool        skip_bob;

    // allow the gun to be completely removed
    if (cl_gun->integer < 1) {
        return;
    }

    if (cl_gun->integer == 1) {
        // don't draw gun if in wide angle view
        if (cls.demo.playback && cls.demo.compat && cl.frame.ps.fov > 90) {
            return;
        }
        // don't draw gun if center handed
        if (info_hand->integer == 2) {
            return;
        }
    }

    // find states to interpolate between
    ps = CL_KEYPS;
    ops = CL_OLDKEYPS;

    memset(&gun, 0, sizeof(gun));
    gun.id = RESERVED_ENTITY_GUN;

    if (gun_model) {
        gun.model = gun_model;  // development tool
    } else {
        gun.model = cl.model_draw[ps->gunindex];
        gun.skinnum = ps->gunskin;
    }
    if (!gun.model) {
        return;
    }

    skip_bob = info_bobskip->integer != 0;

    CG_View_CalcWeaponPose(gun.origin, gun.angles, ps, ops, skip_bob);

    VectorMA(gun.origin, cl_gun_y->value, cl.v_forward, gun.origin);
    VectorMA(gun.origin, cl_gun_x->value, cl.v_right, gun.origin);
    VectorMA(gun.origin, cl_gun_z->value, cl.v_up, gun.origin);

    VectorCopy(gun.origin, gun.oldorigin);      // don't lerp at all

    if (gun_frame) {
        gun.frame = gun_frame;  // development tool
        gun.oldframe = gun_frame;   // development tool
    } else {
// KEX
        if (cl.game_api == Q2PROTO_GAME_RERELEASE) {
            if (ops->gunindex != ps->gunindex) { // just changed weapons, don't lerp from old
                cl.weapon.frame = cl.weapon.last_frame = ps->gunframe;
                cl.weapon.server_time = cl.servertime;
            } else if (cl.weapon.frame == -1 || cl.weapon.frame != ps->gunframe) {
                cl.weapon.frame = ps->gunframe;
                cl.weapon.last_frame = ops->gunframe;
                cl.weapon.server_time = cl.servertime;
            }

            const float gun_ms = 1.f / (!ps->gunrate ? 10 : ps->gunrate) * 1000.f;
            gun.backlerp = Q_clipf(1.f - ((cl.time - ((float) cl.weapon.server_time - cl.frametime.time)) / gun_ms), 0.0f, 1.f);
            gun.frame = cl.weapon.frame;
            gun.oldframe = cl.weapon.last_frame;
        } else {
// KEX
            gun.frame = ps->gunframe;
            if (gun.frame == 0) {
                gun.oldframe = 0;   // just changed weapons, don't lerp from old
            } else {
                gun.oldframe = ops->gunframe;
                gun.backlerp = 1.0f - CL_KEYLERPFRAC;
            }
        }
    }

    gun.flags = RF_MINLIGHT | RF_DEPTHHACK | RF_WEAPONMODEL;
    gun.owner_entity = canonical_render_frame.mode ==
            CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY
        ? (canonical_native_render_view.valid
               ? static_cast<int>(
                     canonical_native_render_view.controlled_entity)
               : 0)
        : cl.frame.clientNum + 1;
    gun.alpha = Cvar_ClampValue(cl_gunalpha, 0.1f, 1.0f);

    ent = get_player_entity();

    // add alpha from cvar or player entity
    if (ent && gun.alpha == 1.0f)
        gun.alpha = lerp_entity_alpha(ent);

    if (gun.alpha != 1.0f)
        gun.flags |= RF_TRANSLUCENT;

    V_AddEntity(&gun);

    // add shell effect from player entity
    if (ent && (flags = shell_effect_hack(ent))) {
        gun.alpha *= 0.30f;
        gun.flags |= flags | RF_TRANSLUCENT;
        V_AddEntity(&gun);
    }

    // add muzzle flash
    if (!cl.weapon.muzzle.model)
        return;

    if (cl.time - cl.weapon.muzzle.time > 50) {
        cl.weapon.muzzle.model = 0;
        return;
    }

    gun.flags = RF_FULLBRIGHT | RF_DEPTHHACK | RF_WEAPONMODEL | RF_TRANSLUCENT;
    gun.alpha = 1.0f;
    gun.model = cl.weapon.muzzle.model;
    gun.skinnum = 0;
    VectorSet(gun.scale, cl.weapon.muzzle.scale, cl.weapon.muzzle.scale, cl.weapon.muzzle.scale);
    gun.backlerp = 0.0f;
    gun.frame = gun.oldframe = 0;
    gun.backlerp = 0.f;
    gun.frame = gun.oldframe = 0;

    vec3_t forward, right, up;
    AngleVectors(gun.angles, forward, right, up);

    VectorMA(gun.origin, cl.weapon.muzzle.offset[0], forward, gun.origin);
    VectorMA(gun.origin, cl.weapon.muzzle.offset[1], right, gun.origin);
    VectorMA(gun.origin, cl.weapon.muzzle.offset[2], up, gun.origin);

    VectorCopy(cl.refdef.viewangles, gun.angles);
    gun.angles[2] += cl.weapon.muzzle.roll;
            
    V_AddEntity(&gun);
}

static void CL_SetupFirstPersonView(void)
{
    // add kick angles
    if (cl_kickangles->integer && !info_bobskip->integer) {
        vec3_t kickangles;
        LerpAngles(CL_OLDKEYPS->kick_angles, CL_KEYPS->kick_angles, CL_KEYLERPFRAC, kickangles);
        VectorAdd(cl.refdef.viewangles, kickangles, cl.refdef.viewangles);
    }

    // add the weapon
    CL_AddViewWeapon();

    cl.thirdPersonView = false;
}

// need to interpolate bmodel positions, or third person view would be very jerky
static void CL_LerpedTrace(trace_t *tr, const vec3_t start, const vec3_t end,
                           const vec3_t mins, const vec3_t maxs, int contentmask)
{
    trace_t trace;
    const centity_t *ent;
    const mmodel_t *cmodel;
    vec3_t org, ang;

    // check against world
    CM_BoxTrace(tr, start, end, mins, maxs, cl.bsp->nodes, contentmask, cl.csr.extended);
    tr->ent = (struct edict_s *)cl_entities;
    if (tr->fraction == 0)
        return;     // blocked by the world

    // check all other solid models
    for (int i = 0; i < cl.numSolidEntities; i++) {
        ent = cl.solidEntities[i];

        // special value for bmodel
        if (ent->current.solid != PACKED_BSP)
            continue;

        cmodel = cl.model_clip[ent->current.modelindex];
        if (!cmodel)
            continue;

        LerpVector(ent->prev.origin, ent->current.origin, cl.lerpfrac, org);
        LerpAngles(ent->prev.angles, ent->current.angles, cl.lerpfrac, ang);

        CM_TransformedBoxTrace(&trace, start, end, mins, maxs, cmodel->headnode,
                               contentmask, org, ang, cl.csr.extended);

        CM_ClipEntity(tr, &trace, (struct edict_s *)ent);
    }
}

/*
===============
CL_SetupThirdPersionView
===============
*/
static void CL_SetupThirdPersionView(void)
{
    static const vec3_t mins = { -4, -4, -4 };
    static const vec3_t maxs = {  4,  4,  4 };
    vec3_t focus;
    float fscale, rscale;
    float dist, angle, range;
    trace_t trace;

    // if dead, set a nice view angle
    if (cl.frame.ps.stats[STAT_HEALTH] <= 0) {
        cl.refdef.viewangles[ROLL] = 0;
        cl.refdef.viewangles[PITCH] = 10;
    }

    VectorMA(cl.refdef.vieworg, 512, cl.v_forward, focus);
    cl.refdef.vieworg[2] += 8;

    cl.refdef.viewangles[PITCH] *= 0.5f;
    AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

    angle = DEG2RAD(cl_thirdperson_angle->value);
    range = cl_thirdperson_range->value;
    fscale = cosf(angle);
    rscale = sinf(angle);
    VectorMA(cl.refdef.vieworg, -range * fscale, cl.v_forward, cl.refdef.vieworg);
    VectorMA(cl.refdef.vieworg, -range * rscale, cl.v_right, cl.refdef.vieworg);

    CL_LerpedTrace(&trace, cl.playerEntityOrigin, cl.refdef.vieworg, mins, maxs, CONTENTS_SOLID);
    VectorCopy(trace.endpos, cl.refdef.vieworg);
    cl.thirdPersonAlpha = trace.fraction;

    VectorSubtract(focus, cl.refdef.vieworg, focus);
    dist = sqrtf(focus[0] * focus[0] + focus[1] * focus[1]);

    cl.refdef.viewangles[PITCH] = -RAD2DEG(atan2f(focus[2], dist));
    cl.refdef.viewangles[YAW] -= cl_thirdperson_angle->value;

    cl.thirdPersonView = true;
}

static void CL_FinishViewValues(void)
{
    if (cl_thirdperson->integer && get_player_entity())
        CL_SetupThirdPersionView();
    else
        CL_SetupFirstPersonView();
}

static inline float lerp_client_fov(float ofov, float nfov, float lerp)
{
    if (cls.demo.playback && !cls.demo.compat) {
        int fov = info_fov->integer;

        if (fov < 1)
            fov = 90;
        else if (fov > 160)
            fov = 160;

        if (info_uf->integer & UF_LOCALFOV)
            return fov;

        if (!(info_uf->integer & UF_PLAYERFOV)) {
            if (ofov >= 90)
                ofov = fov;
            if (nfov >= 90)
                nfov = fov;
        }
    }

    return ofov + lerp * (nfov - ofov);
}

/*
===============
CL_CalcViewValues

Sets cl.refdef view values and sound spatialization params.
Usually called from CL_AddEntities, but may be directly called from the main
loop if rendering is disabled but sound is running.
===============
*/
void CL_CalcViewValues(void)
{
    const player_state_t *ps, *ops;
    vec3_t viewoffset;
    float lerp;

    if (!cl.frame.valid) {
        return;
    }

    // find states to interpolate between
    ps = &cl.frame.ps;
    ops = &cl.oldframe.ps;

    lerp = cl.lerpfrac;

    float viewheight;

    // calculate the origin
    if (!cls.demo.playback && cl_predict->integer && !(ps->pmove.pm_flags & PMF_NO_PREDICTION)) {
        // use predicted values
        unsigned delta = cls.realtime - cl.predicted_step_time;
        float backlerp = lerp - 1.0f;

        VectorMA(cl.predicted_origin, backlerp, cl.prediction_error, cl.refdef.vieworg);

        // smooth out stair climbing
        if (delta < STEP_TIME) {
            cl.refdef.vieworg[2] -= cl.predicted_step * (STEP_TIME - delta) * (1.f / STEP_TIME);
        }
    } else {
        // just use interpolated values
        for (int i = 0; i < 3; i++) {
            cl.refdef.vieworg[i] = ops->pmove.origin[i] +
                lerp * (ps->pmove.origin[i] - ops->pmove.origin[i]);
        }
    }
    
    // Record viewheight changes
    if (cl.current_viewheight != ps->pmove.viewheight) {
        cl.prev_viewheight = cl.current_viewheight;
        cl.current_viewheight = ps->pmove.viewheight;
        cl.viewheight_change_time = cl.time;
    }

    // if not running a demo or on a locked frame, add the local angle movement
    if (cls.demo.playback) {
        if (cls.key_dest == KEY_GAME && Key_IsDown(K_SHIFT)) {
            VectorCopy(cl.viewangles, cl.refdef.viewangles);
        } else {
            LerpAngles(ops->viewangles, ps->viewangles, lerp,
                       cl.refdef.viewangles);
        }
    } else if (ps->pmove.pm_type < PM_DEAD) {
        // use predicted values
        VectorCopy(cl.predicted_angles, cl.refdef.viewangles);
    } else if (ops->pmove.pm_type < PM_DEAD && cls.serverProtocol > PROTOCOL_VERSION_DEFAULT) {
        // lerp from predicted angles, since enhanced servers
        // do not send viewangles each frame
        LerpAngles(cl.predicted_angles, ps->viewangles, lerp, cl.refdef.viewangles);
    } else {
        // just use interpolated values
        LerpAngles(ops->viewangles, ps->viewangles, lerp, cl.refdef.viewangles);
    }

    if (cl.csr.extended) {
        // interpolate blend colors if the last frame wasn't clear
        float blendfrac = ops->screen_blend[3] ? cl.lerpfrac : 1;
        float damageblendfrac = ops->damage_blend[3] ? cl.lerpfrac : 1;
        
        Vector4Lerp(ops->screen_blend, ps->screen_blend, blendfrac, cl.refdef.screen_blend);
        Vector4Lerp(ops->damage_blend, ps->damage_blend, damageblendfrac, cl.refdef.damage_blend);
    } else {
        Vector4Copy(ps->screen_blend, cl.refdef.screen_blend);
        Vector4Copy(ps->damage_blend, cl.refdef.damage_blend);
    }
    // Mix in screen_blend from cgame pmove
    // FIXME: Should also be interpolated?...
    if(cl.predicted_screen_blend[3] > 0) {
        float a2 = cl.refdef.screen_blend[3] + (1 - cl.refdef.screen_blend[3]) * cl.predicted_screen_blend[3]; // new total alpha
        float a3 = cl.refdef.screen_blend[3] / a2;					// fraction of color from old

        LerpVector(cl.predicted_screen_blend, cl.refdef.screen_blend, a3, cl.refdef.screen_blend);
        cl.refdef.screen_blend[3] = a2;
    }
    if (info_bobskip->integer) {
        Vector4Clear(cl.refdef.damage_blend);
    }


#if USE_FPS
    ps = &cl.keyframe.ps;
    ops = &cl.oldkeyframe.ps;

    lerp = cl.keylerpfrac;
#endif

    // interpolate field of view
    cl.fov_x = lerp_client_fov(ops->fov, ps->fov, lerp);
    cl.fov_y = V_CalcFov(cl.fov_x, 4, 3);

    LerpVector(ops->viewoffset, ps->viewoffset, lerp, viewoffset);
    if (info_bobskip->integer) {
        VectorClear(viewoffset);
    }

    AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

    VectorCopy(cl.refdef.vieworg, cl.playerEntityOrigin);
    VectorCopy(cl.refdef.viewangles, cl.playerEntityAngles);

    if (cl.playerEntityAngles[PITCH] > 180) {
        cl.playerEntityAngles[PITCH] -= 360;
    }

    cl.playerEntityAngles[PITCH] = cl.playerEntityAngles[PITCH] / 3;

    VectorAdd(cl.refdef.vieworg, viewoffset, cl.refdef.vieworg);

    // Smooth out view height over 100ms
    float viewheight_lerp = (cl.time - cl.viewheight_change_time);
    viewheight_lerp = 100 - min(viewheight_lerp, 100.0f);
    viewheight = cl.current_viewheight + (float)(cl.prev_viewheight - cl.current_viewheight) * viewheight_lerp * 0.01f;

    cl.refdef.vieworg[2] += viewheight;

    VectorCopy(cl.refdef.vieworg, listener_origin);
    VectorCopy(cl.v_forward, listener_forward);
    VectorCopy(cl.v_right, listener_right);
    VectorCopy(cl.v_up, listener_up);
    listener_entnum = canonical_render_frame.mode ==
            CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY
        ? (canonical_native_render_view.valid
               ? static_cast<int>(
                     canonical_native_render_view.controlled_entity)
               : 0)
        : cl.frame.clientNum + 1;
}

/*
===============
CL_AddEntities

Emits all entities, particles, and lights to the renderer
===============
*/
void CL_AddEntities(void)
{
    begin_canonical_snapshot_render_frame();
    CL_CalcViewValues();
    CL_FinishViewValues();
    CL_AddPacketEntities();
    CL_AddTEnts();
    CL_AddParticles();
    CL_AddDLights();
    CL_AddLightStyles();
    CL_AddShadowLights();
    LOC_AddLocationsToScene();
    end_canonical_snapshot_render_frame();
}

/*
===============
CL_GetEntitySoundOrigin

Called to get the sound spatialization origin
===============
*/
void CL_GetEntitySoundOrigin(unsigned entnum, vec3_t org)
{
    const centity_t *ent;
    const mmodel_t  *mod;
    vec3_t          mid;
    const bool native_authority = canonical_render_frame.mode ==
        CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY;

    if (entnum >= cl.csr.max_edicts)
        Com_Error(ERR_DROP, "%s: bad entity", __func__);

    if (!entnum || entnum == listener_entnum) {
        // should this ever happen?
        VectorCopy(listener_origin, org);
        return;
    }

    if (native_authority) {
        if (!canonical_native_render_view.valid ||
            entnum >= canonical_native_render_view.cache.size() ||
            !canonical_native_render_view.cache[entnum].valid) {
            VectorClear(org);
            return;
        }
        ent = &canonical_native_render_view.cache[entnum].entity;
        VectorCopy(ent->current.origin, org);
    } else {
        // interpolate origin
        ent = &cl_entities[entnum];
        LerpVector(ent->prev.origin, ent->current.origin, cl.lerpfrac, org);
    }

    // use re-releases algorithm for bmodels & beams
    if (cl.csr.extended) {
        // for BSP models, we want the nearest point from
        // the bmodel to the listener; if we're "inside"
        // the bmodel we want it full strength.
        if (ent->current.solid == PACKED_BSP) {
            mod = cl.model_clip[ent->current.modelindex];
            if (mod) {
                vec3_t absmin, absmax;
                VectorAdd(org, mod->mins, absmin);
                VectorAdd(org, mod->maxs, absmax);

                for (int i = 0; i < 3; i++)
                    org[i] = (listener_origin[i] < absmin[i]) ? absmin[i] :
                             (listener_origin[i] > absmax[i]) ? absmax[i] :
                             listener_origin[i];
            }
        } else if (ent->current.renderfx & RF_BEAM) {
            // for beams, we use the nearest point on the line
            // between the two origins
            vec3_t old_origin;
            if (native_authority)
                VectorCopy(ent->current.old_origin, old_origin);
            else
                LerpVector(ent->prev.old_origin,
                           ent->current.old_origin, cl.lerpfrac,
                           old_origin);

            vec3_t vec, p;
            VectorSubtract(old_origin, org, vec);
            VectorSubtract(listener_origin, org, p);

            float frac = Q_clipf(DotProduct(p, vec) / DotProduct(vec, vec), 0.0f, 1.0f);
            VectorMA(org, frac, vec, org);
        }
    } else {
        // offset the origin for BSP models
        if (ent->current.solid == PACKED_BSP) {
            mod = cl.model_clip[ent->current.modelindex];
            if (mod) {
                VectorAvg(mod->mins, mod->maxs, mid);
                VectorAdd(org, mid, org);
            }
        }
    }
}

/*
 * Supplies the sound system with the exact value-owned entity phase used by
 * native rendering.  Returning -1 is an explicit non-native ownership signal;
 * once mode 3 is selected, an unavailable/invalid view returns an empty range
 * and cannot fall back to mutable legacy frame storage.
 */
int CL_CopyLoopSoundEntities(entity_state_t *states, int capacity)
{
    if (canonical_render_frame.mode !=
        CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY) {
        return -1;
    }
    if (!canonical_native_render_view.valid || capacity < 0)
        return 0;
    const int count = static_cast<int>(
        canonical_native_render_view.render_count);
    if (count > capacity || (count != 0 && !states))
        return 0;
    if (count != 0) {
        std::memcpy(states, canonical_native_render_view.render_states.data(),
                    static_cast<std::size_t>(count) * sizeof(*states));
    }
    return count;
}

void CL_PrepareLoopSoundEntities(void)
{
    const int configured_mode = effective_canonical_snapshot_render_mode();
    if (canonical_render_frame.prepared &&
        canonical_render_frame.prepared_realtime_ms == cls.realtime &&
        canonical_render_frame.requested_mode ==
            static_cast<std::uint32_t>(configured_mode)) {
        return;
    }
    begin_canonical_snapshot_render_frame();
}

int CL_GetEntitySoundBinding(unsigned entnum, uint64_t *binding_out)
{
    if (canonical_render_frame.mode !=
        CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY) {
        return -1;
    }
    if (!binding_out || !canonical_native_render_view.valid ||
        entnum >= canonical_native_render_view.cache.size()) {
        return 0;
    }
    const auto &entry = canonical_native_render_view.cache[entnum];
    if (!entry.valid || entry.entity.id <= 0)
        return 0;
    const uint64_t binding =
        (static_cast<uint64_t>(static_cast<uint32_t>(entry.entity.id)) << 32u) |
        entry.generation;
    if (binding == 0u)
        return 0;
    *binding_out = binding;
    return 1;
}

bool CL_GetEntitySoundOriginBound(unsigned entnum, uint64_t binding,
                                  vec3_t org)
{
    if (!org || binding == 0u ||
        canonical_render_frame.mode !=
            CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY ||
        !canonical_native_render_view.valid ||
        entnum >= canonical_native_render_view.cache.size()) {
        return false;
    }
    const auto &entry = canonical_native_render_view.cache[entnum];
    if (!entry.valid || entry.entity.id <= 0) {
        return false;
    }
    const uint64_t current =
        (static_cast<uint64_t>(static_cast<uint32_t>(entry.entity.id)) << 32u) |
        entry.generation;
    if (current != binding)
        return false;
    CL_GetEntitySoundOrigin(entnum, org);
    return true;
}
