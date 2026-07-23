/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_native_event_presenter.hpp"

#include "cg_entity_local.h"
#include "cg_canonical_entity_adapter.hpp"
#include "cg_canonical_snapshot_timeline.hpp"
#include "cg_event_shadow.hpp"
#include "cg_event_runtime.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

enum class presentation_kind_t : std::uint32_t {
    none,
    legacy_entity,
    legacy_temp,
    muzzle,
    spatial_audio,
    damage,
    help_path,
    keyed_poi,
};

static_assert(static_cast<std::uint32_t>(presentation_kind_t::none) ==
              CG_NATIVE_EVENT_PRESENTER_KIND_NONE);
static_assert(static_cast<std::uint32_t>(presentation_kind_t::legacy_entity) ==
              CG_NATIVE_EVENT_PRESENTER_KIND_LEGACY_ENTITY);
static_assert(static_cast<std::uint32_t>(presentation_kind_t::legacy_temp) ==
              CG_NATIVE_EVENT_PRESENTER_KIND_LEGACY_TEMP);
static_assert(static_cast<std::uint32_t>(presentation_kind_t::muzzle) ==
              CG_NATIVE_EVENT_PRESENTER_KIND_MUZZLE);
static_assert(static_cast<std::uint32_t>(presentation_kind_t::spatial_audio) ==
              CG_NATIVE_EVENT_PRESENTER_KIND_SPATIAL_AUDIO);
static_assert(static_cast<std::uint32_t>(presentation_kind_t::damage) ==
              CG_NATIVE_EVENT_PRESENTER_KIND_DAMAGE);
static_assert(static_cast<std::uint32_t>(presentation_kind_t::help_path) ==
              CG_NATIVE_EVENT_PRESENTER_KIND_HELP_PATH);
static_assert(static_cast<std::uint32_t>(presentation_kind_t::keyed_poi) ==
              CG_NATIVE_EVENT_PRESENTER_KIND_KEYED_POI);
static_assert(CG_NATIVE_EVENT_PRESENTER_KIND_KEYED_POI + 1u ==
              CG_NATIVE_EVENT_PRESENTER_KIND_COUNT);

struct prepared_presentation_t {
    bool ready;
    bool probe_commit;
    presentation_kind_t kind;
    presentation_kind_t preflight_kind;
    std::uint64_t semantic_hash;
    std::uint64_t presentation_hash;
    worr_event_record_v1 record;
    cg_event_runtime_presentation_context_v1 context;
    entity_state_t source_state;
    std::uint32_t source_generation;
    tent_params_t temp;
    mz_params_t muzzle;
    qhandle_t sound;
    int sound_entity;
    int sound_channel;
    vec3_t sound_origin;
    float sound_volume;
    float sound_attenuation;
    float sound_time_offset;
    worr_event_payload_damage_v1 damage_payload;
    worr_event_payload_effect_v1 effect_payload;
    cg_prepared_keyed_poi_v1 keyed_poi;
};

struct native_event_probe_checkpoint_state_t {
    bool applied;
    std::uint32_t map_generation;
    std::uint32_t authority_epoch;
    std::uint64_t checkpoint_id;
    std::uint64_t authoritative_presentations;
    std::uint64_t authoritative_duplicates;
    std::uint64_t authoritative_conflicts;
    std::uint64_t authority_ref_body_joins;
    std::uint64_t legacy_ref_body_mismatches;
    std::uint64_t authoritative_expirations;
    std::uint64_t authoritative_terminal_skips;
    std::uint64_t future_time_stalls;
};

struct native_event_probe_busy_log_key_t {
    std::uint32_t map_generation;
    std::uint32_t authority_epoch;
    std::uint64_t checkpoint_id;
    std::uint32_t phase;
    std::uint32_t reason;
    std::uint32_t next_authority_sequence;
    std::uint32_t pending_sequence;
    std::uint32_t authority_state;
    std::uint32_t slot_state;
};

prepared_presentation_t prepared;
bool effect_authority_enabled;
cvar_t *native_event_preflight_probe;
bool preflight_probe_latched;
bool map_active;
std::uint32_t map_generation;
std::uint32_t map_end_count;
std::uint64_t probe_commits;
std::uint64_t probe_effects_suppressed;
std::uint64_t probe_nonvisual_commits;
std::uint64_t probe_action_chain_hash;
std::uint64_t native_effect_dispatches;
std::uint64_t native_effect_chain_hash;
std::uint64_t presenter_commit_mismatches;
std::array<std::uint64_t, CG_NATIVE_EVENT_PRESENTER_KIND_COUNT>
    probe_commits_by_kind;
std::array<worr_snapshot_entity_v2,
           CG_CANONICAL_SNAPSHOT_TIMELINE_ENTITY_CAPACITY>
    fence_entities;
std::uint32_t fence_entity_count;
worr_snapshot_player_v2 fence_player;
worr_snapshot_id_v2 loaded_fence_id;
std::uint32_t loaded_fence_tick;
std::uint64_t loaded_fence_time_us;
native_event_probe_checkpoint_state_t probe_checkpoint;
native_event_probe_busy_log_key_t probe_busy_log_key;

void increment_saturated(std::uint64_t &value)
{
    if (value != std::numeric_limits<std::uint64_t>::max())
        ++value;
}

void increment_saturated(std::uint32_t &value)
{
    if (value != std::numeric_limits<std::uint32_t>::max())
        ++value;
}

void scrub_prepared()
{
    prepared = {};
    loaded_fence_id = {};
    loaded_fence_tick = 0;
    loaded_fence_time_us = 0;
    fence_entity_count = 0;
    fence_player = {};
}

std::uint64_t checkpoint_delta(std::uint64_t current,
                               std::uint64_t baseline)
{
    return current >= baseline
               ? current - baseline
               : std::numeric_limits<std::uint64_t>::max();
}

void reset_probe_window_counters()
{
    probe_commits = 0;
    probe_effects_suppressed = 0;
    probe_nonvisual_commits = 0;
    probe_action_chain_hash = 0;
    probe_commits_by_kind = {};
}

void log_probe_checkpoint_busy(
    std::uint32_t phase, std::uint32_t expected_authority_epoch,
    std::uint64_t checkpoint_id,
    const cg_event_runtime_checkpoint_block_v1 *block = nullptr)
{
    native_event_probe_busy_log_key_t key{};
    key.map_generation = map_generation;
    key.authority_epoch = expected_authority_epoch;
    key.checkpoint_id = checkpoint_id;
    key.phase = phase;
    if (block) {
        key.reason = block->reason;
        key.next_authority_sequence = block->next_authority_sequence;
        key.pending_sequence = block->pending_sequence;
        key.authority_state = block->authority_state;
        key.slot_state = block->slot_state;
    }
    if (std::memcmp(&key, &probe_busy_log_key, sizeof(key)) == 0)
        return;
    probe_busy_log_key = key;
    if (!cgei)
        return;

    if (!block) {
        Com_DPrintf(
            "native event probe checkpoint busy: map=%u epoch=%u "
            "checkpoint=%llu phase=%u\n",
            map_generation, expected_authority_epoch,
            static_cast<unsigned long long>(checkpoint_id), phase);
        return;
    }
    Com_DPrintf(
        "native event probe checkpoint busy: map=%u epoch=%u "
        "checkpoint=%llu phase=%u reason=%u next=%u pending=%u "
        "authority_state=0x%x slot_state=0x%x flags=0x%x payload=%u "
        "delivery=%u source=%u expiry=%u fence=%u/%u@%u/%llu "
        "last_snapshot=%u/%u/%llu render=%u/%llu\n",
        map_generation, expected_authority_epoch,
        static_cast<unsigned long long>(checkpoint_id), phase,
        block->reason, block->next_authority_sequence,
        block->pending_sequence, block->authority_state,
        block->slot_state, block->record_flags, block->payload_kind,
        block->delivery_class, block->source_tick, block->expiry_tick,
        block->fence_snapshot_id.epoch,
        block->fence_snapshot_id.sequence, block->fence_tick,
        static_cast<unsigned long long>(block->fence_time_us),
        block->last_snapshot_id.epoch, block->last_snapshot_id.sequence,
        static_cast<unsigned long long>(block->last_snapshot_time_us),
        block->last_now_tick,
        static_cast<unsigned long long>(block->last_render_time_us));
}

bool preflight_probe_active()
{
    return map_active && preflight_probe_latched &&
           !effect_authority_enabled;
}

bool full_preflight_enabled()
{
    return effect_authority_enabled || preflight_probe_active();
}

void select_presentation_kind(presentation_kind_t kind)
{
    prepared.preflight_kind = kind;
    prepared.kind = effect_authority_enabled
                        ? kind
                        : presentation_kind_t::none;
}

bool id_equal(worr_snapshot_id_v2 left, worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch && left.sequence == right.sequence;
}

bool ref_absent(worr_event_entity_ref_v1 ref)
{
    return ref.index == WORR_EVENT_NO_ENTITY && ref.generation == 0;
}

bool load_fence(
    const cg_event_runtime_presentation_context_v1 &context)
{
    if (context.provenance != CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY ||
        context.fence_snapshot_id.epoch == 0 ||
        context.fence_snapshot_id.sequence == 0) {
        return false;
    }
    if (id_equal(loaded_fence_id, context.fence_snapshot_id)) {
        return loaded_fence_tick == context.fence_tick &&
               loaded_fence_time_us == context.fence_time_us;
    }

    worr_snapshot_timeline_ref_v1 ref{};
    if (CG_CanonicalSnapshotTimelineFindSnapshot(
            context.fence_snapshot_id, &ref) !=
        WORR_SNAPSHOT_TIMELINE_OK) {
        return false;
    }
    worr_snapshot_v2 snapshot{};
    if (CG_CanonicalSnapshotTimelineCopySnapshot(ref, &snapshot) !=
            WORR_SNAPSHOT_TIMELINE_OK ||
        !id_equal(snapshot.snapshot_id, context.fence_snapshot_id) ||
        snapshot.server_tick != context.fence_tick ||
        snapshot.server_time_us != context.fence_time_us) {
        return false;
    }
    worr_snapshot_player_v2 player{};
    if (CG_CanonicalSnapshotTimelineCopyPlayer(ref, &player) !=
        WORR_SNAPSHOT_TIMELINE_OK) {
        return false;
    }
    std::uint32_t count = 0;
    if (CG_CanonicalSnapshotTimelineCopyEntities(
            ref, fence_entities.data(),
            static_cast<std::uint32_t>(fence_entities.size()), &count) !=
        WORR_SNAPSHOT_TIMELINE_OK) {
        return false;
    }
    fence_player = player;
    fence_entity_count = count;
    loaded_fence_id = context.fence_snapshot_id;
    loaded_fence_tick = context.fence_tick;
    loaded_fence_time_us = context.fence_time_us;
    return true;
}

bool resolve_entity(
    worr_event_entity_ref_v1 identity,
    const cg_event_runtime_presentation_context_v1 &context,
    entity_state_t *state_out, std::uint32_t *generation_out)
{
    if (!state_out || !generation_out || ref_absent(identity) ||
        !load_fence(context)) {
        return false;
    }
    const worr_snapshot_entity_v2 *found = nullptr;
    for (std::uint32_t index = 0; index < fence_entity_count; ++index) {
        const auto &entity = fence_entities[index];
        if (entity.generation.identity.index != identity.index)
            continue;
        if (found || entity.generation.identity.generation !=
                         identity.generation) {
            return false;
        }
        found = &entity;
    }
    if (!cgei || cl.csr.max_edicts <= 1 ||
        cl.csr.max_models <= 0 || cl.csr.max_sounds <= 0)
        return false;

    const cg_canonical_entity_adapter_limits_v1 limits{
        static_cast<std::uint32_t>(cl.csr.max_edicts),
        static_cast<std::uint32_t>(cl.csr.max_models),
        static_cast<std::uint32_t>(cl.csr.max_sounds),
    };
    entity_state_t state{};
    if (found) {
        if (CG_CanonicalEntityToRenderStateV1(found, limits, &state) !=
            CG_CANONICAL_ENTITY_ADAPTER_OK) {
            return false;
        }
    } else {
        const auto &controlled = fence_player.controlled_entity.identity;
        constexpr std::uint64_t required_components =
            WORR_SNAPSHOT_PLAYER_MOVEMENT | WORR_SNAPSHOT_PLAYER_VIEW;
        if (controlled.index != identity.index ||
            controlled.generation != identity.generation ||
            !Worr_SnapshotPlayerValidateV2(
                &fence_player,
                static_cast<std::uint32_t>(cl.csr.max_edicts)) ||
            (fence_player.component_mask & required_components) !=
                required_components) {
            return false;
        }

        /* Legacy frame installation reconstructs an invisible controlled
         * player from playerstate before dispatching muzzle/effect carriers.
         * Mirror Com_PlayerToEntityState from this exact immutable fence so
         * native presentation never reaches into mutable cl_entities. */
        state.number = static_cast<int>(identity.index);
        std::memcpy(state.origin, fence_player.movement.origin,
                    sizeof(state.origin));
        float pitch = fence_player.view_angles[PITCH];
        if (pitch > 180.0f)
            pitch -= 360.0f;
        state.angles[PITCH] = pitch / 3.0f;
        state.angles[YAW] = fence_player.view_angles[YAW];
        state.angles[ROLL] = 0.0f;
    }
    *state_out = state;
    *generation_out = identity.generation;
    return true;
}

bool resolve_entity_exists(
    worr_event_entity_ref_v1 identity,
    const cg_event_runtime_presentation_context_v1 &context)
{
    entity_state_t ignored_state{};
    std::uint32_t ignored_generation = 0;
    return ref_absent(identity) ||
           resolve_entity(identity, context, &ignored_state,
                          &ignored_generation);
}

template <typename Payload>
Payload payload_copy(const worr_event_record_v1 &record)
{
    Payload payload{};
    std::memcpy(&payload, record.payload, sizeof(payload));
    return payload;
}

bool prepare_legacy_entity(
    const worr_event_record_v1 &record,
    const cg_event_runtime_presentation_context_v1 &context)
{
    const auto payload =
        payload_copy<worr_event_payload_legacy_entity_v1>(record);
    if (payload.raw_event <= EV_NONE || payload.raw_event > EV_LADDER_STEP ||
        !resolve_entity(record.source_entity, context,
                        &prepared.source_state,
                        &prepared.source_generation)) {
        return false;
    }
    if (!full_preflight_enabled()) {
        select_presentation_kind(presentation_kind_t::none);
        return true;
    }
    if (!CL_CanPresentLegacyEntityEventValue(
            static_cast<int>(record.source_entity.index),
            payload.raw_event, &prepared.source_state)) {
        return false;
    }
    select_presentation_kind(presentation_kind_t::legacy_entity);
    return true;
}

bool prepare_legacy_temp(
    const worr_event_record_v1 &record,
    const cg_event_runtime_presentation_context_v1 &context)
{
    const auto payload =
        payload_copy<worr_event_payload_legacy_temp_v1>(record);
    entity_state_t source{};
    std::uint32_t generation = 0;
    bool has_source = false;
    if (record.source_entity.index != 0 &&
        !ref_absent(record.source_entity)) {
        if (!resolve_entity(record.source_entity, context, &source,
                            &generation)) {
            return false;
        }
        has_source = true;
    }
    if (!resolve_entity_exists(record.subject_entity, context))
        return false;

    prepared.temp.type = payload.subtype;
    prepared.temp.entity1 = payload.raw_entity1;
    prepared.temp.entity2 = payload.raw_entity2;
    prepared.temp.time = payload.time_ms;
    prepared.temp.count = payload.count_or_amount;
    prepared.temp.color = payload.color;
    VectorCopy(payload.position1, prepared.temp.pos1);
    VectorCopy(payload.position2, prepared.temp.pos2);
    VectorCopy(payload.offset, prepared.temp.offset);
    VectorCopy(payload.direction, prepared.temp.dir);
    if (has_source) {
        prepared.source_state = source;
        prepared.source_generation = generation;
        VectorCopy(source.origin, prepared.sound_origin);
    }
    prepared.sound_entity = payload.raw_entity1;
    if (!full_preflight_enabled()) {
        select_presentation_kind(presentation_kind_t::none);
        return true;
    }
    if (!CL_CanPresentTEntValue(
            &prepared.temp,
            prepared.source_generation ? prepared.sound_origin : nullptr,
            prepared.sound_entity)) {
        return false;
    }
    select_presentation_kind(presentation_kind_t::legacy_temp);
    return true;
}

bool prepare_muzzle(
    const worr_event_record_v1 &record,
    const cg_event_runtime_presentation_context_v1 &context)
{
    const auto payload =
        payload_copy<worr_event_payload_muzzle_v1>(record);
    if (!resolve_entity(record.source_entity, context,
                        &prepared.source_state,
                        &prepared.source_generation)) {
        return false;
    }
    prepared.muzzle.entity =
        static_cast<int>(record.source_entity.index);
    prepared.muzzle.weapon = payload.flash_id;
    prepared.muzzle.silenced =
        (payload.flags & WORR_EVENT_MUZZLE_FLAG_SILENCED) != 0;
    VectorCopy(prepared.source_state.origin, prepared.sound_origin);
    if (!full_preflight_enabled()) {
        select_presentation_kind(presentation_kind_t::none);
        return true;
    }
    if (!CL_CanPresentMuzzleFlashValue(
            &prepared.muzzle, &prepared.source_state,
            prepared.sound_origin, prepared.source_generation,
            payload.family == WORR_EVENT_MUZZLE_FAMILY_MONSTER)) {
        return false;
    }
    select_presentation_kind(presentation_kind_t::muzzle);
    return true;
}

bool prepare_spatial_audio(
    const worr_event_record_v1 &record,
    const cg_event_runtime_presentation_context_v1 &context)
{
    const auto payload =
        payload_copy<worr_event_payload_spatial_audio_v1>(record);
    if (payload.pitch != 1.0f || !cgei)
        return false;

    const bool has_entity_channel =
        (payload.flags &
         WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL) != 0;
    const bool has_position =
        (payload.flags & WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION) != 0;
    const bool position_forced =
        (payload.flags &
         WORR_EVENT_SPATIAL_AUDIO_POSITION_FORCED) != 0;
    entity_state_t source{};
    std::uint32_t generation = 0;
    if (position_forced &&
        (!has_position || record.source_entity.index != 0 ||
         record.source_entity.generation != 1)) {
        return false;
    }
    if (has_entity_channel && !position_forced) {
        if (record.source_entity.index != payload.raw_entity)
            return false;
        if (!resolve_entity(record.source_entity, context, &source,
                            &generation))
            return false;
    }

    if (has_position) {
        VectorCopy(payload.origin, prepared.sound_origin);
    } else {
        if (!has_entity_channel &&
            !resolve_entity(record.source_entity, context, &source,
                            &generation))
            return false;
        VectorCopy(source.origin, prepared.sound_origin);
    }
    if (generation != 0) {
        prepared.source_state = source;
        prepared.source_generation = generation;
    }
    if (!full_preflight_enabled()) {
        select_presentation_kind(presentation_kind_t::none);
        return true;
    }
    const qhandle_t sound = S_GetPrecachedSound(payload.asset_id);
    if (!sound)
        return false;
    prepared.sound = sound;
    prepared.sound_entity =
        has_entity_channel
            ? static_cast<int>(payload.raw_entity)
            : -1;
    prepared.sound_channel = payload.channel;
    prepared.sound_volume = payload.volume;
    prepared.sound_attenuation = payload.attenuation;
    prepared.sound_time_offset = payload.time_offset;
    select_presentation_kind(presentation_kind_t::spatial_audio);
    return true;
}

bool prepare_damage(
    const worr_event_record_v1 &record,
    const cg_event_runtime_presentation_context_v1 &context)
{
    const auto payload =
        payload_copy<worr_event_payload_damage_v1>(record);
    if (payload.amount >
            static_cast<float>(std::numeric_limits<int>::max()) ||
        std::floor(payload.amount) != payload.amount ||
        !resolve_entity_exists(record.subject_entity, context)) {
        return false;
    }
    prepared.damage_payload = payload;
    if (!full_preflight_enabled()) {
        select_presentation_kind(presentation_kind_t::none);
        return true;
    }
    select_presentation_kind(presentation_kind_t::damage);
    return true;
}

bool prepare_effect(
    const worr_event_record_v1 &record,
    const cg_event_runtime_presentation_context_v1 &context)
{
    const auto payload =
        payload_copy<worr_event_payload_effect_v1>(record);
    if (payload.effect_id != WORR_EVENT_EFFECT_HELP_PATH_MARKER ||
        payload.variant > WORR_EVENT_HELP_PATH_VARIANT_START ||
        !resolve_entity_exists(record.subject_entity, context)) {
        return false;
    }
    prepared.effect_payload = payload;
    if (!full_preflight_enabled()) {
        select_presentation_kind(presentation_kind_t::none);
        return true;
    }
    if (!CL_CanPresentHelpPathValue(
            payload.origin, payload.direction,
            payload.variant == WORR_EVENT_HELP_PATH_VARIANT_START)) {
        return false;
    }
    select_presentation_kind(presentation_kind_t::help_path);
    return true;
}

bool prepare_keyed_poi(
    const worr_event_record_v1 &record,
    const cg_event_runtime_presentation_context_v1 &context)
{
    const auto payload =
        payload_copy<worr_event_payload_keyed_poi_v1>(record);
    if (ref_absent(record.subject_entity) ||
        !resolve_entity_exists(record.subject_entity, context)) {
        return false;
    }
    if (!full_preflight_enabled()) {
        select_presentation_kind(presentation_kind_t::none);
        return true;
    }
    if (!CG_CanPresentKeyedPOIValue(&payload, &prepared.keyed_poi))
        return false;
    select_presentation_kind(presentation_kind_t::keyed_poi);
    return true;
}

bool can_present(
    const worr_event_record_v1 *record,
    const cg_event_runtime_presentation_context_v1 *context)
{
    if (prepared.ready)
        increment_saturated(presenter_commit_mismatches);
    prepared = {};
    loaded_fence_id = {};
    loaded_fence_tick = 0;
    loaded_fence_time_us = 0;
    fence_entity_count = 0;
    if (!record || !context || !cgei ||
        context->struct_size != sizeof(*context) ||
        context->schema_version != CG_EVENT_RUNTIME_PRESENTER_VERSION ||
        context->reserved0 != 0 || context->reserved1 != 0 ||
        (context->provenance != CG_EVENT_RUNTIME_PRESENTATION_PREDICTED &&
         context->provenance != CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY)) {
        return false;
    }

    bool supported = false;
    switch (record->payload_kind) {
    case WORR_EVENT_PAYLOAD_NONE:
    case WORR_EVENT_PAYLOAD_VEC3:
    case WORR_EVENT_PAYLOAD_U32X4:
    case WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1:
    case WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1:
        select_presentation_kind(presentation_kind_t::none);
        supported = true;
        break;
    case WORR_EVENT_PAYLOAD_ENTITY_REF: {
        const auto payload =
            payload_copy<worr_event_payload_entity_ref_v1>(*record);
        supported = context->provenance ==
                        CG_EVENT_RUNTIME_PRESENTATION_PREDICTED ||
                    resolve_entity_exists(payload.entity, *context);
        select_presentation_kind(presentation_kind_t::none);
        break;
    }
    case WORR_EVENT_PAYLOAD_DAMAGE:
        supported = prepare_damage(*record, *context);
        break;
    case WORR_EVENT_PAYLOAD_EFFECT:
        supported = prepare_effect(*record, *context);
        break;
    case WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1:
        supported = prepare_legacy_entity(*record, *context);
        break;
    case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1:
        supported = prepare_legacy_temp(*record, *context);
        break;
    case WORR_EVENT_PAYLOAD_MUZZLE_V1:
        supported = prepare_muzzle(*record, *context);
        break;
    case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1:
        supported = prepare_spatial_audio(*record, *context);
        break;
    case WORR_EVENT_PAYLOAD_KEYED_POI_V1:
        supported = prepare_keyed_poi(*record, *context);
        break;
    case WORR_EVENT_PAYLOAD_AUDIO:
    default:
        supported = false;
        break;
    }
    if (!supported) {
        prepared = {};
        return false;
    }
    if (!Worr_EventRecordSemanticHashV1(
            record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
            &prepared.semantic_hash)) {
        prepared = {};
        return false;
    }
    prepared.presentation_hash = prepared.semantic_hash;
    const bool probe_commit = preflight_probe_active();
    if (probe_commit && !CG_NativeEventProbeAuthorityHash(
                            record, context,
                            &prepared.presentation_hash)) {
        prepared = {};
        return false;
    }
    prepared.record = *record;
    prepared.context = *context;
    prepared.probe_commit = probe_commit;
    prepared.ready = true;
    return true;
}

void account_probe_commit()
{
    if (!prepared.probe_commit)
        return;
    const auto kind = static_cast<std::uint32_t>(prepared.preflight_kind);
    if (kind >= probe_commits_by_kind.size())
        return;
    increment_saturated(probe_commits);
    increment_saturated(probe_commits_by_kind[kind]);
    probe_action_chain_hash =
        Worr_CGameNativeEventProbeChainAppendV1(
            probe_action_chain_hash, kind, prepared.presentation_hash);
    if (prepared.preflight_kind == presentation_kind_t::none)
        increment_saturated(probe_nonvisual_commits);
    else
        increment_saturated(probe_effects_suppressed);
}

void present(const worr_event_record_v1 *record,
             const cg_event_runtime_presentation_context_v1 *context)
{
    if (!prepared.ready || !record || !context ||
        std::memcmp(record, &prepared.record, sizeof(*record)) != 0 ||
        std::memcmp(context, &prepared.context, sizeof(*context)) != 0) {
        increment_saturated(presenter_commit_mismatches);
        scrub_prepared();
        return;
    }

    const bool effect_dispatched =
        prepared.kind != presentation_kind_t::none;
    switch (prepared.kind) {
    case presentation_kind_t::none:
        break;
    case presentation_kind_t::legacy_entity: {
        const auto payload =
            payload_copy<worr_event_payload_legacy_entity_v1>(*record);
        CL_PresentLegacyEntityEventValue(
            static_cast<int>(record->source_entity.index),
            payload.raw_event, &prepared.source_state);
        break;
    }
    case presentation_kind_t::legacy_temp:
        CL_PresentTEntValue(
            &prepared.temp,
            prepared.source_generation ? prepared.sound_origin : nullptr,
            prepared.sound_entity);
        break;
    case presentation_kind_t::muzzle: {
        const auto payload =
            payload_copy<worr_event_payload_muzzle_v1>(*record);
        CL_PresentMuzzleFlashValue(
            &prepared.muzzle, &prepared.source_state,
            prepared.sound_origin, prepared.source_generation,
            payload.family == WORR_EVENT_MUZZLE_FAMILY_MONSTER);
        break;
    }
    case presentation_kind_t::spatial_audio:
        S_StartSound(prepared.sound_origin, prepared.sound_entity,
                     prepared.sound_channel, prepared.sound,
                     prepared.sound_volume, prepared.sound_attenuation,
                     prepared.sound_time_offset);
        break;
    case presentation_kind_t::damage: {
        vec3_t color{};
        if ((prepared.damage_payload.damage_flags &
             WORR_EVENT_DAMAGE_FLAG_HEALTH) != 0)
            color[0] += 1.0f;
        if ((prepared.damage_payload.damage_flags &
             WORR_EVENT_DAMAGE_FLAG_SHIELD) != 0)
            color[1] += 1.0f;
        if ((prepared.damage_payload.damage_flags &
             WORR_EVENT_DAMAGE_FLAG_ARMOR) != 0) {
            color[0] += 1.0f;
            color[1] += 1.0f;
            color[2] += 1.0f;
        }
        VectorNormalize(color);
        CL_PresentDamageDisplayValue(
            static_cast<int>(prepared.damage_payload.amount), color,
            prepared.damage_payload.direction);
        break;
    }
    case presentation_kind_t::help_path:
        CL_AddHelpPath(
            prepared.effect_payload.origin,
            prepared.effect_payload.direction,
            prepared.effect_payload.variant ==
                WORR_EVENT_HELP_PATH_VARIANT_START);
        break;
    case presentation_kind_t::keyed_poi:
        CG_PresentKeyedPOIValue(&prepared.keyed_poi);
        break;
    }
    if (effect_dispatched) {
        const auto kind = static_cast<std::uint32_t>(prepared.kind);
        increment_saturated(native_effect_dispatches);
        native_effect_chain_hash =
            Worr_CGameNativeEventProbeChainAppendV1(
                native_effect_chain_hash, kind,
                prepared.presentation_hash);
    }
    account_probe_commit();
    scrub_prepared();
}

} // namespace

void CG_NativeEventPresenterInitCvars()
{
    native_event_preflight_probe =
        cgei
            ? Cvar_Get("cg_native_event_preflight_probe", "0",
                       CVAR_NOARCHIVE)
            : nullptr;
}

void CG_NativeEventPresenterInstall()
{
    CG_EventRuntimeSetPresenter(&can_present, &present);
}

void CG_NativeEventPresenterUninstall()
{
    effect_authority_enabled = false;
    native_event_preflight_probe = nullptr;
    preflight_probe_latched = false;
    map_active = false;
    map_generation = 0;
    map_end_count = 0;
    probe_commits = 0;
    probe_effects_suppressed = 0;
    probe_nonvisual_commits = 0;
    probe_action_chain_hash = 0;
    native_effect_dispatches = 0;
    native_effect_chain_hash = 0;
    presenter_commit_mismatches = 0;
    probe_commits_by_kind = {};
    probe_checkpoint = {};
    probe_busy_log_key = {};
    scrub_prepared();
    CG_NativeEventProbeRawUninstall();
    CG_EventRuntimeSetPresenter(nullptr, nullptr);
}

void CG_NativeEventPresenterBeginMap()
{
    scrub_prepared();
    preflight_probe_latched =
        native_event_preflight_probe &&
        native_event_preflight_probe->integer != 0;
    map_active = true;
    increment_saturated(map_generation);
    reset_probe_window_counters();
    native_effect_dispatches = 0;
    native_effect_chain_hash = 0;
    presenter_commit_mismatches = 0;
    probe_checkpoint = {};
    probe_busy_log_key = {};
    CG_NativeEventProbeRawBeginMap();
}

void CG_NativeEventPresenterEndMap()
{
    if (map_active)
        increment_saturated(map_end_count);
    CG_NativeEventProbeRawEndMap();
    map_active = false;
    preflight_probe_latched = false;
    probe_busy_log_key = {};
    scrub_prepared();
}

bool CG_NativeEventPresenterPreflightProbeEnabled()
{
    return preflight_probe_active();
}

bool CG_NativeEventPresenterResourcesRequired()
{
    return effect_authority_enabled || preflight_probe_active();
}

bool CG_NativeEventPresenterGetStatus(
    cg_native_event_presenter_status_v1 *status_out)
{
    if (!status_out)
        return false;
    *status_out = {};
    status_out->struct_size = sizeof(*status_out);
    status_out->schema_version =
        CG_NATIVE_EVENT_PRESENTER_STATUS_VERSION;
    status_out->map_generation = map_generation;
    status_out->map_active = map_active ? 1u : 0u;
    status_out->preflight_probe_requested =
        native_event_preflight_probe &&
                native_event_preflight_probe->integer != 0
            ? 1u
            : 0u;
    status_out->preflight_probe_latched =
        preflight_probe_latched ? 1u : 0u;
    status_out->preflight_probe_active =
        preflight_probe_active() ? 1u : 0u;
    status_out->effect_authority_enabled =
        effect_authority_enabled ? 1u : 0u;
    status_out->resources_required =
        CG_NativeEventPresenterResourcesRequired() ? 1u : 0u;
    status_out->probe_commits = probe_commits;
    status_out->probe_effects_suppressed =
        probe_effects_suppressed;
    status_out->probe_nonvisual_commits = probe_nonvisual_commits;
    for (std::uint32_t index = 0;
         index < CG_NATIVE_EVENT_PRESENTER_KIND_COUNT; ++index) {
        status_out->probe_commits_by_kind[index] =
            probe_commits_by_kind[index];
    }
    return true;
}

namespace {

bool get_probe_status(
    worr_cgame_native_event_probe_status_v1 *status_out)
{
    if (!status_out)
        return false;

    worr_cgame_native_event_probe_status_v1 status{};
    status.struct_size = sizeof(status);
    status.schema_version =
        WORR_CGAME_NATIVE_EVENT_PROBE_STATUS_VERSION;
    status.kind_count = WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT;
    status.map_generation = map_generation;
    status.map_end_count = map_end_count;
    status.map_active = map_active ? 1u : 0u;
    status.probe_requested =
        native_event_preflight_probe &&
                native_event_preflight_probe->integer != 0
            ? 1u
            : 0u;
    status.probe_latched = preflight_probe_latched ? 1u : 0u;
    status.probe_active = preflight_probe_active() ? 1u : 0u;
    status.effect_authority_enabled =
        effect_authority_enabled ? 1u : 0u;
    status.resources_required =
        CG_NativeEventPresenterResourcesRequired() ? 1u : 0u;
    status.legacy_owner_active =
        map_active && !effect_authority_enabled ? 1u : 0u;
    status.probe_action_commits = probe_commits;
    status.probe_action_chain_hash = probe_action_chain_hash;
    status.probe_effects_suppressed = probe_effects_suppressed;
    status.probe_nonvisual_commits = probe_nonvisual_commits;
    status.native_effect_dispatches = native_effect_dispatches;
    status.native_effect_chain_hash = native_effect_chain_hash;
    status.presenter_commit_mismatches = presenter_commit_mismatches;
    for (std::uint32_t index = 0;
         index < CG_NATIVE_EVENT_PRESENTER_KIND_COUNT; ++index) {
        status.probe_action_by_kind[index] =
            probe_commits_by_kind[index];
    }
    if (!CG_NativeEventProbeFillRawStatus(&status))
        return false;

    cg_event_runtime_status_v1 runtime_status{};
    if (!CG_EventRuntimeGetStatus(&runtime_status))
        return false;
    status.authority_epoch = runtime_status.authority_epoch;
    status.authority_requires_resync =
        runtime_status.authority_requires_resync;
    status.authority_degraded = runtime_status.authority_degraded;
    if (probe_checkpoint.applied) {
        status.authoritative_presentations = checkpoint_delta(
            runtime_status.authoritative_presentations,
            probe_checkpoint.authoritative_presentations);
        status.authoritative_duplicates = checkpoint_delta(
            runtime_status.authoritative_duplicates,
            probe_checkpoint.authoritative_duplicates);
        status.authoritative_conflicts = checkpoint_delta(
            runtime_status.authoritative_conflicts,
            probe_checkpoint.authoritative_conflicts);
        status.authority_ref_body_joins = checkpoint_delta(
            runtime_status.authority_ref_body_joins,
            probe_checkpoint.authority_ref_body_joins);
        status.legacy_ref_body_mismatches = checkpoint_delta(
            runtime_status.legacy_ref_body_mismatches,
            probe_checkpoint.legacy_ref_body_mismatches);

    } else {
        status.authoritative_presentations =
            runtime_status.authoritative_presentations;
        status.authoritative_duplicates =
            runtime_status.authoritative_duplicates;
        status.authoritative_conflicts =
            runtime_status.authoritative_conflicts;
        status.authority_ref_body_joins =
            runtime_status.authority_ref_body_joins;
        status.legacy_ref_body_mismatches =
            runtime_status.legacy_ref_body_mismatches;
    }

    *status_out = status;
    return true;
}

bool checkpoint_probe_window(
    std::uint32_t expected_map_generation,
    std::uint32_t expected_authority_epoch,
    std::uint64_t checkpoint_id,
    worr_cgame_native_event_probe_checkpoint_receipt_v1 *receipt_out)
{
    if (!receipt_out)
        return false;

    worr_cgame_native_event_probe_checkpoint_receipt_v1 receipt{};
    receipt.struct_size = sizeof(receipt);
    receipt.schema_version =
        WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_VERSION;
    receipt.result =
        WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_INVALID_ARGUMENT;
    receipt.observed_map_generation = map_generation;
    receipt.checkpoint_id = checkpoint_id;

    cg_event_runtime_status_v1 runtime_status{};
    if (!CG_EventRuntimeGetStatus(&runtime_status)) {
        receipt.result = WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_NOT_READY;
        *receipt_out = receipt;
        return true;
    }
    receipt.observed_authority_epoch = runtime_status.authority_epoch;

    if (expected_map_generation == 0 || expected_authority_epoch == 0 ||
        checkpoint_id == 0) {
        *receipt_out = receipt;
        return true;
    }
    if (expected_map_generation != map_generation) {
        receipt.result =
            WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_MAP;
        *receipt_out = receipt;
        return true;
    }
    if (expected_authority_epoch != runtime_status.authority_epoch) {
        receipt.result =
            WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_AUTHORITY;
        *receipt_out = receipt;
        return true;
    }

    if (probe_checkpoint.applied) {
        receipt.result =
            probe_checkpoint.map_generation == expected_map_generation &&
                    probe_checkpoint.authority_epoch ==
                        expected_authority_epoch &&
                    probe_checkpoint.checkpoint_id == checkpoint_id
                ? WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED
                : WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_CONFLICT;
        *receipt_out = receipt;
        return true;
    }

    if (!map_active || !preflight_probe_active() ||
        effect_authority_enabled) {
        receipt.result = WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_NOT_READY;
        *receipt_out = receipt;
        return true;
    }
    if (runtime_status.authority_requires_resync != 0 ||
        runtime_status.authority_degraded != 0 ||
        native_effect_dispatches != 0 || native_effect_chain_hash != 0 ||
        presenter_commit_mismatches != 0) {
        receipt.result = WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_UNHEALTHY;
        *receipt_out = receipt;
        return true;
    }
    if (prepared.ready) {
        log_probe_checkpoint_busy(
            1u, expected_authority_epoch, checkpoint_id);
        receipt.result = WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_BUSY;
        *receipt_out = receipt;
        return true;
    }

    worr_cgame_native_event_probe_status_v1 raw_status{};
    if (!CG_NativeEventProbeFillRawStatus(&raw_status)) {
        receipt.result = WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_NOT_READY;
        *receipt_out = receipt;
        return true;
    }
    if (raw_status.raw_pending_count != 0) {
        log_probe_checkpoint_busy(
            2u, expected_authority_epoch, checkpoint_id);
        receipt.result = WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_BUSY;
        *receipt_out = receipt;
        return true;
    }
    if (raw_status.raw_pair_failures != 0 ||
        raw_status.raw_effect_suppressions != 0 ||
        !CG_NativeEventProbeRawCheckpointReady()) {
        receipt.result = WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_UNHEALTHY;
        *receipt_out = receipt;
        return true;
    }

    cg_event_runtime_status_v1 checkpoint_status{};
    if (!CG_EventRuntimeCheckpointReady(
            expected_authority_epoch, &checkpoint_status)) {
        /* The runtime helper deliberately publishes its counter snapshot
         * only on success.  Map/epoch and current health were classified
         * from runtime_status above; a remaining refusal is an in-flight or
         * otherwise unpresented authority record, so do not reinterpret the
         * untouched output buffer as a stale epoch. */
        cg_event_runtime_checkpoint_block_v1 block{};
        const auto *block_ptr = CG_EventRuntimeGetCheckpointBlock(
                                    expected_authority_epoch, &block)
                                    ? &block
                                    : nullptr;
        log_probe_checkpoint_busy(
            3u, expected_authority_epoch, checkpoint_id, block_ptr);
        receipt.result = WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_BUSY;
        *receipt_out = receipt;
        return true;
    }

    /* Everything above is read-only. From here to receipt publication the
     * main-thread synchronous extension has no fallible operation. */
    CG_NativeEventProbeRawApplyCheckpoint();
    reset_probe_window_counters();
    probe_checkpoint.applied = true;
    probe_checkpoint.map_generation = expected_map_generation;
    probe_checkpoint.authority_epoch = expected_authority_epoch;
    probe_checkpoint.checkpoint_id = checkpoint_id;
    probe_checkpoint.authoritative_presentations =
        checkpoint_status.authoritative_presentations;
    probe_checkpoint.authoritative_duplicates =
        checkpoint_status.authoritative_duplicates;
    probe_checkpoint.authoritative_conflicts =
        checkpoint_status.authoritative_conflicts;
    probe_checkpoint.authority_ref_body_joins =
        checkpoint_status.authority_ref_body_joins;
    probe_checkpoint.legacy_ref_body_mismatches =
        checkpoint_status.legacy_ref_body_mismatches;
    probe_checkpoint.authoritative_expirations =
        checkpoint_status.authoritative_expirations;
    probe_checkpoint.authoritative_terminal_skips =
        checkpoint_status.authoritative_terminal_skips;
    probe_checkpoint.future_time_stalls =
        checkpoint_status.future_time_stalls;

    receipt.result = WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED;
    *receipt_out = receipt;
    return true;
}

const worr_cgame_native_event_probe_export_v1 native_event_probe_api = {
    sizeof(native_event_probe_api),
    WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION,
    CG_NativeEventProbeCompleteLegacyDispatch,
    get_probe_status,
};

const worr_cgame_native_event_probe_export_v2 native_event_probe_api_v2 = {
    sizeof(native_event_probe_api_v2),
    WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION_V2,
    CG_NativeEventProbeCompleteLegacyDispatch,
    get_probe_status,
    checkpoint_probe_window,
};

} // namespace

const worr_cgame_native_event_probe_export_v1 *
CG_GetNativeEventProbeAPI()
{
    return &native_event_probe_api;
}

const worr_cgame_native_event_probe_export_v2 *
CG_GetNativeEventProbeAPIv2()
{
    return &native_event_probe_api_v2;
}

void CG_NativeEventPresenterSetEffectAuthority(bool enabled)
{
    if (effect_authority_enabled == enabled)
        return;
    effect_authority_enabled = enabled;
    scrub_prepared();
}

bool CG_NativeEventPresenterEffectAuthorityEnabled()
{
    return effect_authority_enabled;
}
