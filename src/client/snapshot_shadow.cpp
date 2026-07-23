/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"
#include "client/snapshot_shadow.h"

#include <cstring>
#include <limits>

namespace {

constexpr uint32_t shadow_slot_capacity = UPDATE_BACKUP;
constexpr uint32_t accept_known_flags =
    CL_SNAPSHOT_SHADOW_ACCEPT_DELIVER_CONSUMER |
    CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY;
constexpr uint64_t canonical_entity_components =
    WORR_SNAPSHOT_ENTITY_TRANSFORM |
    WORR_SNAPSHOT_ENTITY_INTERPOLATION |
    WORR_SNAPSHOT_ENTITY_MODELS |
    WORR_SNAPSHOT_ENTITY_ANIMATION |
    WORR_SNAPSHOT_ENTITY_APPEARANCE |
    WORR_SNAPSHOT_ENTITY_EFFECTS |
    WORR_SNAPSHOT_ENTITY_COLLISION |
    WORR_SNAPSHOT_ENTITY_LOOP_SOUND;

static_assert(MAX_STATS >= WORR_SNAPSHOT_STATS_CAPACITY,
              "legacy player state cannot cover the canonical stats range");

enum class native_expectation_qualification_t : uint8_t {
    empty = 0,
    unqualified = 1,
    available = 2,
};

struct native_expectation_slot_t {
    worr_native_snapshot_expectation_v1 expectation{};
    worr_snapshot_ref_v2 projection_ref{};
    native_expectation_qualification_t qualification{};
    uint8_t reserved0[3]{};
};

struct snapshot_shadow_state_t {
    worr_snapshot_q2proto_context_v2 context{};
    worr_snapshot_q2proto_slot_v2 *slots{};
    worr_snapshot_entity_v2 *entities{};
    uint8_t *area_bytes{};
    worr_snapshot_event_ref_v2 *event_refs{};
    worr_snapshot_q2proto_lineage_v2 *lineages{};
    worr_snapshot_entity_v2 *baselines{};
    uint8_t *baseline_present{};
    worr_snapshot_entity_v2 *scratch_entities{};
    uint8_t *scratch_area_bytes{};
    worr_snapshot_event_ref_v2 *scratch_event_refs{};
    worr_snapshot_q2proto_lineage_v2 *scratch_lineage{};

    /* Independent legacy canonicalization scratch, never projector-owned. */
    worr_snapshot_entity_v2 *legacy_entities{};
    worr_snapshot_event_ref_v2 *legacy_event_refs{};

    q2proto_svc_frame_t pending_frame{};
    worr_snapshot_consumed_command_v2 pending_consumed_command{};
    uint8_t pending_area_bytes[MAX_MAP_AREA_BYTES]{};
    q2proto_svc_frame_entity_delta_t *pending_deltas{};
    uint32_t pending_delta_count{};
    uint32_t pending_delta_capacity{};
    uint32_t entities_per_slot{};

    int32_t clock_last_server_frame{};
    uint64_t clock_last_server_time_us{};
    bool clock_initialized{};

    worr_snapshot_ref_v2 latest_ref{};
    worr_snapshot_projection_hashes_v2 latest_hashes{};
    native_expectation_slot_t
        native_expectations[shadow_slot_capacity]{};
    worr_snapshot_id_v2 native_expectation_watermark{};
    cl_snapshot_shadow_status_v1 status{};
    bool active{};
    bool pending{};
    bool latest_promotion_attempted{};
    bool native_epoch_bound{};
    uint32_t capture_failure{};
};

snapshot_shadow_state_t shadow;
uint32_t shadow_epoch_seed;
uint64_t shadow_connection_resets;
cvar_t *cl_snapshot_shadow;
cvar_t *cl_snapshot_shadow_debug;
const worr_cgame_snapshot_timeline_export_v2 *snapshot_consumer;
cl_snapshot_shadow_record_observer_v1 snapshot_record_observer;
uintptr_t snapshot_consumer_lifetime_cookie = 1;
bool snapshot_consumer_lifetime_exhausted;

void increment_saturating(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

uint64_t host_time_us()
{
    return com_unscaledTimeUs;
}

void initialize_status()
{
    if (shadow.status.struct_size == sizeof(shadow.status) &&
        shadow.status.schema_version == CL_SNAPSHOT_SHADOW_STATUS_VERSION) {
        return;
    }
    std::memset(&shadow.status, 0, sizeof(shadow.status));
    shadow.status.struct_size = sizeof(shadow.status);
    shadow.status.schema_version = CL_SNAPSHOT_SHADOW_STATUS_VERSION;
    shadow.status.lifecycle = CL_SNAPSHOT_SHADOW_LIFECYCLE_NEVER_STARTED;
    shadow.status.consumer_attached = snapshot_consumer ? 1u : 0u;
}

template <typename T>
void free_storage(T **pointer)
{
    if (*pointer != nullptr) {
        Z_Free(*pointer);
        *pointer = nullptr;
    }
}

void free_runtime_storage()
{
    free_storage(&shadow.slots);
    free_storage(&shadow.entities);
    free_storage(&shadow.area_bytes);
    free_storage(&shadow.event_refs);
    free_storage(&shadow.lineages);
    free_storage(&shadow.baselines);
    free_storage(&shadow.baseline_present);
    free_storage(&shadow.scratch_entities);
    free_storage(&shadow.scratch_area_bytes);
    free_storage(&shadow.scratch_event_refs);
    free_storage(&shadow.scratch_lineage);
    free_storage(&shadow.legacy_entities);
    free_storage(&shadow.legacy_event_refs);
    free_storage(&shadow.pending_deltas);
}

template <typename T>
T *allocate_storage(uint32_t count, uint64_t *allocation_bytes)
{
    if (count == 0 || count > SIZE_MAX / sizeof(T))
        return nullptr;
    const size_t bytes = static_cast<size_t>(count) * sizeof(T);
    if (*allocation_bytes > UINT64_MAX - bytes)
        return nullptr;
    auto *result = static_cast<T *>(Z_Mallocz(bytes));
    if (result != nullptr)
        *allocation_bytes += bytes;
    return result;
}

void clear_runtime_preserving_status()
{
    initialize_status();
    const cl_snapshot_shadow_status_v1 status = shadow.status;
    free_runtime_storage();
    std::memset(&shadow, 0, sizeof(shadow));
    shadow.status = status;
}

bool consumer_export_valid(
    const worr_cgame_snapshot_timeline_export_v2 *consumer)
{
    return consumer != nullptr &&
           consumer->struct_size == sizeof(*consumer) &&
           consumer->api_version ==
               WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION &&
           consumer->Reset != nullptr &&
           consumer->ConsumeCanonicalSnapshot != nullptr &&
           consumer->GetStatus != nullptr;
}

void invalidate_native_consumer_lifetime()
{
    if (snapshot_consumer_lifetime_exhausted)
        return;
    if (snapshot_consumer_lifetime_cookie == UINTPTR_MAX) {
        snapshot_consumer_lifetime_cookie = 0;
        snapshot_consumer_lifetime_exhausted = true;
        return;
    }
    ++snapshot_consumer_lifetime_cookie;
}

bool native_consumer_cookie_valid(void *opaque)
{
    return !snapshot_consumer_lifetime_exhausted &&
           snapshot_consumer_lifetime_cookie != 0 &&
           reinterpret_cast<uintptr_t>(opaque) ==
               snapshot_consumer_lifetime_cookie &&
           consumer_export_valid(snapshot_consumer);
}

void reset_consumer(uint32_t reason, uint64_t reset_host_time_us)
{
    if (!snapshot_consumer)
        return;
    shadow.status.last_reset_reason = reason;
    snapshot_consumer->Reset(shadow.status.snapshot_epoch, reason,
                             reset_host_time_us);
    increment_saturating(&shadow.status.consumer_resets);
}

void note_consumer_rejection()
{
    increment_saturating(&shadow.status.consumer_rejections);
    shadow.status.last_consumer_rejection_result = UINT32_MAX;
    if (!snapshot_consumer || !snapshot_consumer->GetStatus)
        return;

    worr_cgame_snapshot_timeline_status_v2 consumer_status{};
    if (!snapshot_consumer->GetStatus(&consumer_status))
        return;
    shadow.status.last_consumer_rejection_result =
        consumer_status.last_result;
    if (cl_snapshot_shadow_debug && cl_snapshot_shadow_debug->integer) {
        Com_DPrintf(
            "snapshot shadow: consumer rejected result=%u epoch=%u "
            "attempts=%" PRIu64 " accepted=%" PRIu64
            " rejected=%" PRIu64 "\n",
            consumer_status.last_result, consumer_status.active_epoch,
            consumer_status.consume_attempts, consumer_status.accepted,
            consumer_status.rejected);
    }
}

void native_consumer_reset(void *opaque, uint32_t snapshot_epoch,
                           uint32_t reason, uint64_t reset_host_time_us)
{
    if (!native_consumer_cookie_valid(opaque))
        return;
    shadow.status.last_reset_reason = reason;
    snapshot_consumer->Reset(snapshot_epoch, reason, reset_host_time_us);
    increment_saturating(&shadow.status.consumer_resets);
}

bool native_consumer_consume(
    void *opaque, const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_projection_hashes_v2 *hashes,
    uint64_t receive_time_us)
{
    if (!native_consumer_cookie_valid(opaque))
        return false;
    increment_saturating(&shadow.status.consumer_attempts);
    if (!snapshot_consumer->ConsumeCanonicalSnapshot(
            view, hashes, receive_time_us)) {
        note_consumer_rejection();
        return false;
    }
    increment_saturating(&shadow.status.consumer_accepts);
    return true;
}

bool native_consumer_get_status(
    void *opaque, worr_cgame_snapshot_timeline_status_v2 *status_out)
{
    return native_consumer_cookie_valid(opaque) &&
           status_out != nullptr &&
           snapshot_consumer->GetStatus(status_out);
}

void note_result(worr_snapshot_q2proto_result_v2 result)
{
    shadow.status.last_result = static_cast<uint32_t>(result);
    if (result != WORR_SNAPSHOT_Q2PROTO_OK &&
        cl_snapshot_shadow_debug && cl_snapshot_shadow_debug->integer) {
        Com_DPrintf("snapshot shadow: projector result %u\n",
                    static_cast<unsigned>(result));
    }
}

void note_capture_failure(cl_snapshot_shadow_capture_failure_v1 reason)
{
    if (shadow.capture_failure == CL_SNAPSHOT_SHADOW_CAPTURE_NONE)
        shadow.capture_failure = static_cast<uint32_t>(reason);
    shadow.status.last_capture_failure = shadow.capture_failure;
}

void abort_pending(bool count_abort)
{
    if (count_abort && shadow.pending)
        increment_saturating(&shadow.status.pending_aborts);
    shadow.pending = false;
    shadow.pending_consumed_command = {};
    shadow.capture_failure = CL_SNAPSHOT_SHADOW_CAPTURE_NONE;
    shadow.pending_delta_count = 0;
    shadow.status.pending_frame = 0;
}

void reset_snapshot_clock()
{
    shadow.clock_last_server_frame = 0;
    shadow.clock_last_server_time_us = 0;
    shadow.clock_initialized = false;
}

bool allocate_runtime_storage(uint32_t max_entities)
{
    const uint32_t entities_per_slot = max_entities - 1u;
    const uint64_t entity_total64 =
        static_cast<uint64_t>(shadow_slot_capacity) * entities_per_slot;
    const uint64_t area_total64 =
        static_cast<uint64_t>(shadow_slot_capacity) * MAX_MAP_AREA_BYTES;
    const uint64_t lineage_total64 =
        static_cast<uint64_t>(shadow_slot_capacity) * max_entities;
    if (entity_total64 > UINT32_MAX || area_total64 > UINT32_MAX ||
        lineage_total64 > UINT32_MAX) {
        return false;
    }

    uint64_t bytes = 0;
    shadow.slots = allocate_storage<worr_snapshot_q2proto_slot_v2>(
        shadow_slot_capacity, &bytes);
    shadow.entities = allocate_storage<worr_snapshot_entity_v2>(
        static_cast<uint32_t>(entity_total64), &bytes);
    shadow.area_bytes = allocate_storage<uint8_t>(
        static_cast<uint32_t>(area_total64), &bytes);
    shadow.event_refs = allocate_storage<worr_snapshot_event_ref_v2>(
        static_cast<uint32_t>(entity_total64), &bytes);
    shadow.lineages = allocate_storage<worr_snapshot_q2proto_lineage_v2>(
        static_cast<uint32_t>(lineage_total64), &bytes);
    shadow.baselines = allocate_storage<worr_snapshot_entity_v2>(
        max_entities, &bytes);
    shadow.baseline_present = allocate_storage<uint8_t>(max_entities,
                                                         &bytes);
    shadow.scratch_entities = allocate_storage<worr_snapshot_entity_v2>(
        entities_per_slot, &bytes);
    shadow.scratch_area_bytes = allocate_storage<uint8_t>(
        MAX_MAP_AREA_BYTES, &bytes);
    shadow.scratch_event_refs =
        allocate_storage<worr_snapshot_event_ref_v2>(entities_per_slot,
                                                       &bytes);
    shadow.scratch_lineage =
        allocate_storage<worr_snapshot_q2proto_lineage_v2>(max_entities,
                                                            &bytes);
    shadow.legacy_entities = allocate_storage<worr_snapshot_entity_v2>(
        entities_per_slot, &bytes);
    shadow.legacy_event_refs =
        allocate_storage<worr_snapshot_event_ref_v2>(entities_per_slot,
                                                       &bytes);
    /* One sorted carrier per legal entity, plus the mandatory zero sentinel. */
    shadow.pending_deltas =
        allocate_storage<q2proto_svc_frame_entity_delta_t>(max_entities,
                                                            &bytes);

    const bool complete = shadow.slots && shadow.entities &&
        shadow.area_bytes && shadow.event_refs && shadow.lineages &&
        shadow.baselines && shadow.baseline_present &&
        shadow.scratch_entities && shadow.scratch_area_bytes &&
        shadow.scratch_event_refs && shadow.scratch_lineage &&
        shadow.legacy_entities && shadow.legacy_event_refs &&
        shadow.pending_deltas;
    if (!complete) {
        free_runtime_storage();
        return false;
    }
    shadow.entities_per_slot = entities_per_slot;
    shadow.pending_delta_capacity = max_entities;
    shadow.status.storage_entities_per_slot = entities_per_slot;
    shadow.status.allocation_bytes = bytes;
    return true;
}

worr_snapshot_player_v2 canonical_legacy_player(
    const player_state_t &source,
    worr_snapshot_entity_generation_v2 controlled_entity)
{
    worr_snapshot_player_v2 player{};
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.controlled_entity = controlled_entity;
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    player.movement.movement_type =
        static_cast<int32_t>(source.pmove.pm_type);
    std::memcpy(player.movement.origin, source.pmove.origin,
                sizeof(player.movement.origin));
    std::memcpy(player.movement.velocity, source.pmove.velocity,
                sizeof(player.movement.velocity));
    player.movement.movement_flags = source.pmove.pm_flags;
    player.movement.movement_time_ms = source.pmove.pm_time;
    player.movement.gravity = source.pmove.gravity;
    player.movement.view_height = source.pmove.viewheight;
    std::memcpy(player.movement.delta_angles, source.pmove.delta_angles,
                sizeof(player.movement.delta_angles));
    std::memcpy(player.view_angles, source.viewangles,
                sizeof(player.view_angles));
    std::memcpy(player.view_offset, source.viewoffset,
                sizeof(player.view_offset));
    std::memcpy(player.kick_angles, source.kick_angles,
                sizeof(player.kick_angles));
    std::memcpy(player.gun_angles, source.gunangles,
                sizeof(player.gun_angles));
    std::memcpy(player.gun_offset, source.gunoffset,
                sizeof(player.gun_offset));
    std::memcpy(player.screen_blend, source.screen_blend,
                sizeof(player.screen_blend));
    std::memcpy(player.damage_blend, source.damage_blend,
                sizeof(player.damage_blend));
    player.gun_index = static_cast<uint16_t>(source.gunindex);
    player.gun_frame = static_cast<uint16_t>(source.gunframe);
    player.gun_skin = static_cast<uint8_t>(source.gunskin);
    player.gun_rate = static_cast<uint8_t>(source.gunrate);
    player.rdflags = static_cast<uint8_t>(source.rdflags);
    player.team_id = source.team_id;
    player.fov = source.fov;
    for (uint32_t i = 0; i < WORR_SNAPSHOT_STATS_CAPACITY; ++i)
        player.stats[i] = source.stats[i];
    return player;
}

worr_snapshot_entity_v2 canonical_legacy_entity(
    const entity_state_t &source,
    worr_snapshot_entity_generation_v2 generation)
{
    worr_snapshot_entity_v2 entity{};
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.generation = generation;
    entity.component_mask = canonical_entity_components;
    std::memcpy(entity.origin, source.origin, sizeof(entity.origin));
    std::memcpy(entity.angles, source.angles, sizeof(entity.angles));
    std::memcpy(entity.old_origin, source.old_origin,
                sizeof(entity.old_origin));
    entity.model_index[0] = static_cast<uint16_t>(source.modelindex);
    entity.model_index[1] = static_cast<uint16_t>(source.modelindex2);
    entity.model_index[2] = static_cast<uint16_t>(source.modelindex3);
    entity.model_index[3] = static_cast<uint16_t>(source.modelindex4);
    entity.frame = static_cast<uint16_t>(source.frame);
    entity.sound = static_cast<uint16_t>(source.sound);
    entity.skin = static_cast<uint32_t>(source.skinnum);
    entity.solid = static_cast<uint32_t>(source.solid);
    entity.effects = static_cast<uint64_t>(source.effects);
    entity.renderfx = static_cast<uint32_t>(source.renderfx);
    entity.alpha = source.alpha;
    entity.scale = source.scale;
    entity.loop_volume = source.loop_volume;
    entity.loop_attenuation = source.loop_attenuation;
    entity.owner.index = WORR_EVENT_NO_ENTITY;
    return entity;
}

bool initialize_legacy_event_record(worr_event_record_v1 *record,
                                    uint32_t tick, uint64_t time_us,
                                    uint32_t source_ordinal,
                                    worr_event_entity_ref_v1 source,
                                    uint8_t raw_event)
{
    if (!record)
        return false;
    worr_event_payload_legacy_entity_v1 payload{};
    uint16_t event_type;
    uint16_t payload_flags;
    switch (raw_event) {
    case WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN:
        event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
        payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
        break;
    case WORR_EVENT_LEGACY_ENTITY_FOOTSTEP:
    case WORR_EVENT_LEGACY_ENTITY_FALL_SHORT:
    case WORR_EVENT_LEGACY_ENTITY_FALL_MEDIUM:
    case WORR_EVENT_LEGACY_ENTITY_FALL_FAR:
    case WORR_EVENT_LEGACY_ENTITY_OTHER_FOOTSTEP:
    case WORR_EVENT_LEGACY_ENTITY_LADDER_STEP:
        event_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
        payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
        break;
    case WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT:
        event_type = WORR_EVENT_TYPE_STATE_CHANGE;
        payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION |
                        WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY;
        break;
    case WORR_EVENT_LEGACY_ENTITY_OTHER_TELEPORT:
        event_type = WORR_EVENT_TYPE_STATE_CHANGE;
        payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY;
        break;
    default:
        return false;
    }
    payload.raw_event = raw_event;
    payload.flags = payload_flags;
    record->struct_size = sizeof(*record);
    record->schema_version = WORR_EVENT_ABI_VERSION;
    record->model_revision = WORR_EVENT_MODEL_REVISION;
    record->flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                    WORR_EVENT_FLAG_PRESENT_ONCE;
    record->source_tick = tick;
    record->source_ordinal = source_ordinal;
    record->source_time_us = time_us;
    record->source_entity = source;
    record->subject_entity.index = WORR_EVENT_NO_ENTITY;
    record->event_type = event_type;
    record->delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    record->prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record->expiry_tick = tick + 1u;
    record->payload_kind = WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1;
    record->payload_size = sizeof(payload);
    std::memcpy(record->payload, &payload, sizeof(payload));
    return true;
}

bool append_legacy_event(const worr_snapshot_entity_v2 &entity,
                         uint8_t raw_event, uint32_t source_ordinal,
                         uint32_t tick, uint64_t time_us,
                         uint32_t *event_count)
{
    if (raw_event == 0)
        return true;
    if (!event_count || *event_count >= shadow.entities_per_slot)
        return false;
    worr_event_record_v1 record{};
    uint64_t semantic_hash;
    if (!initialize_legacy_event_record(
            &record, tick, time_us, source_ordinal,
            entity.generation.identity, raw_event) ||
        !Worr_EventRecordCandidateValidateV1(
            &record, shadow.context.profile.max_entities) ||
        !Worr_EventRecordSemanticHashV1(
            &record, shadow.context.profile.max_entities,
            &semantic_hash)) {
        return false;
    }
    auto &event = shadow.legacy_event_refs[*event_count];
    event = {};
    event.struct_size = sizeof(event);
    event.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    event.provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;
    event.carrier_ordinal = *event_count;
    event.semantic_version = WORR_EVENT_MODEL_REVISION;
    event.semantic_hash = semantic_hash;
    ++*event_count;
    return true;
}

struct legacy_parity_result_t {
    uint32_t mismatch{};
    uint64_t observed_parity_hash{};
};

legacy_parity_result_t compare_accepted_legacy_frame(
    const worr_snapshot_projection_view_v2 &view,
    const worr_snapshot_projection_hashes_v2 &projection_hashes,
    uint32_t controlled_entity_index, uint64_t server_time_us)
{
    legacy_parity_result_t result{};
    const server_frame_t &legacy = cl.frame;
    if (!legacy.valid || legacy.number < 0 ||
        (legacy.delta > 0 && legacy.delta == INT32_MAX) ||
        legacy.numEntities < 0 || legacy.areabytes < 0 ||
        static_cast<uint32_t>(legacy.numEntities) >
            shadow.entities_per_slot ||
        static_cast<uint32_t>(legacy.areabytes) > MAX_MAP_AREA_BYTES ||
        legacy.number == INT32_MAX) {
        result.mismatch = CL_SNAPSHOT_SHADOW_PARITY_METADATA |
                          CL_SNAPSHOT_SHADOW_PARITY_HASH_BUILD;
        return result;
    }

    worr_snapshot_id_v2 expected_id{
        view.snapshot->snapshot_id.epoch,
        static_cast<uint32_t>(legacy.number) + 1u};
    worr_snapshot_id_v2 expected_base{};
    if (legacy.delta > 0) {
        expected_base.epoch = expected_id.epoch;
        expected_base.sequence = static_cast<uint32_t>(legacy.delta) + 1u;
    }
    const bool legacy_keyframe = legacy.delta <= 0;
    if (view.snapshot->snapshot_id.epoch == 0 ||
        view.snapshot->snapshot_id.sequence != expected_id.sequence ||
        view.snapshot->base_id.epoch != expected_base.epoch ||
        view.snapshot->base_id.sequence != expected_base.sequence ||
        view.snapshot->server_tick != static_cast<uint32_t>(legacy.number) ||
        ((view.snapshot->flags & WORR_SNAPSHOT_FLAG_KEYFRAME) != 0) !=
            legacy_keyframe ||
        legacy.clientNum < 0 ||
        static_cast<uint32_t>(legacy.clientNum + 1) !=
            controlled_entity_index ||
        view.snapshot->controlled_entity.identity.index !=
            controlled_entity_index) {
        result.mismatch |= CL_SNAPSHOT_SHADOW_PARITY_METADATA;
    }

    const worr_snapshot_player_v2 player = canonical_legacy_player(
        legacy.ps, view.player->controlled_entity);
    uint64_t player_hash = 0;
    if (!Worr_SnapshotPlayerHashV2(
            &player, shadow.context.profile.max_entities, &player_hash) ||
        player_hash != view.snapshot->player_hash) {
        result.mismatch |= CL_SNAPSHOT_SHADOW_PARITY_PLAYER;
    }

    const uint32_t legacy_entity_count =
        static_cast<uint32_t>(legacy.numEntities);
    if (legacy_entity_count != view.entity_count)
        result.mismatch |= CL_SNAPSHOT_SHADOW_PARITY_ENTITY_COUNT;
    uint32_t event_count = 0;
    bool entity_build_valid = legacy_entity_count == view.entity_count;
    bool event_build_valid = entity_build_valid;
    if (entity_build_valid) {
        for (uint32_t i = 0; i < legacy_entity_count; ++i) {
            const entity_state_t &source =
                cl.entityStates[(legacy.firstEntity + i) &
                                PARSE_ENTITIES_MASK];
            if (source.number <= 0 ||
                static_cast<uint32_t>(source.number) >=
                    shadow.context.profile.max_entities ||
                view.entities[i].generation.identity.index !=
                    static_cast<uint32_t>(source.number)) {
                entity_build_valid = false;
                event_build_valid = false;
                result.mismatch |=
                    CL_SNAPSHOT_SHADOW_PARITY_ENTITY_PAYLOAD;
                break;
            }
            shadow.legacy_entities[i] = canonical_legacy_entity(
                source, view.entities[i].generation);
            if (!append_legacy_event(
                    shadow.legacy_entities[i], source.event, i,
                    static_cast<uint32_t>(legacy.number), server_time_us,
                    &event_count)) {
                event_build_valid = false;
                result.mismatch |=
                    CL_SNAPSHOT_SHADOW_PARITY_EVENT_PAYLOAD;
            }
        }
    }

    uint64_t entity_hash = 0;
    if (!entity_build_valid ||
        !Worr_SnapshotEntityListHashV2(
            shadow.legacy_entities, legacy_entity_count,
            shadow.context.profile.max_entities, &entity_hash) ||
        entity_hash != view.snapshot->entity_hash) {
        result.mismatch |= CL_SNAPSHOT_SHADOW_PARITY_ENTITY_PAYLOAD;
    }

    const uint32_t legacy_area_count =
        static_cast<uint32_t>(legacy.areabytes);
    if (legacy_area_count != view.area_byte_count)
        result.mismatch |= CL_SNAPSHOT_SHADOW_PARITY_AREA_COUNT;
    uint64_t area_hash = 0;
    if (legacy_area_count != view.area_byte_count ||
        !Worr_SnapshotAreaHashV2(legacy.areabits, legacy_area_count,
                                 &area_hash) ||
        area_hash != view.snapshot->area_hash) {
        result.mismatch |= CL_SNAPSHOT_SHADOW_PARITY_AREA_PAYLOAD;
    }

    if (event_count != view.event_ref_count)
        result.mismatch |= CL_SNAPSHOT_SHADOW_PARITY_EVENT_COUNT;
    uint64_t event_hash = 0;
    if (!event_build_valid || event_count != view.event_ref_count ||
        !Worr_SnapshotEventRefsHashV2(shadow.legacy_event_refs,
                                      event_count, &event_hash) ||
        event_hash != view.snapshot->event_hash) {
        result.mismatch |= CL_SNAPSHOT_SHADOW_PARITY_EVENT_PAYLOAD;
    }

    const uint32_t payload_mismatch =
        CL_SNAPSHOT_SHADOW_PARITY_PLAYER |
        CL_SNAPSHOT_SHADOW_PARITY_ENTITY_COUNT |
        CL_SNAPSHOT_SHADOW_PARITY_ENTITY_PAYLOAD |
        CL_SNAPSHOT_SHADOW_PARITY_AREA_COUNT |
        CL_SNAPSHOT_SHADOW_PARITY_AREA_PAYLOAD |
        CL_SNAPSHOT_SHADOW_PARITY_EVENT_COUNT |
        CL_SNAPSHOT_SHADOW_PARITY_EVENT_PAYLOAD;
    if ((result.mismatch &
         (CL_SNAPSHOT_SHADOW_PARITY_METADATA | payload_mismatch)) == 0) {
        worr_snapshot_v2 snapshot = *view.snapshot;
        snapshot.snapshot_id = expected_id;
        snapshot.base_id = expected_base;
        snapshot.server_tick = static_cast<uint32_t>(legacy.number);
        snapshot.server_time_us = server_time_us;
        snapshot.flags &= ~(WORR_SNAPSHOT_FLAG_KEYFRAME |
                            WORR_SNAPSHOT_FLAG_COMPLETE |
                            WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION);
        snapshot.flags |= WORR_SNAPSHOT_FLAG_COMPLETE |
                          WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION;
        if (legacy_keyframe)
            snapshot.flags |= WORR_SNAPSHOT_FLAG_KEYFRAME;
        snapshot.player_hash = player_hash;
        snapshot.entity_hash = entity_hash;
        snapshot.area_hash = area_hash;
        snapshot.event_hash = event_hash;
        snapshot.snapshot_hash = 0;
        worr_snapshot_projection_view_v2 expected_view{};
        expected_view.struct_size = sizeof(expected_view);
        expected_view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
        expected_view.snapshot = &snapshot;
        expected_view.player = &player;
        expected_view.entities = shadow.legacy_entities;
        expected_view.area_bytes = legacy.areabits;
        expected_view.event_refs = shadow.legacy_event_refs;
        expected_view.entity_count = legacy_entity_count;
        expected_view.area_byte_count = legacy_area_count;
        expected_view.event_ref_count = event_count;
        worr_snapshot_projection_hashes_v2 expected_hashes{};
        if (!Worr_SnapshotCalculateHashV2(
                &snapshot, shadow.context.profile.max_entities,
                &snapshot.snapshot_hash) ||
            !Worr_SnapshotProjectionHashesV2(
                &expected_view, shadow.context.profile.max_entities,
                &expected_hashes)) {
            result.mismatch |= CL_SNAPSHOT_SHADOW_PARITY_HASH_BUILD;
        } else {
            result.observed_parity_hash = expected_hashes.legacy_parity_hash;
            if (expected_hashes.legacy_parity_hash !=
                    projection_hashes.legacy_parity_hash ||
                expected_hashes.semantic_player_hash !=
                    projection_hashes.semantic_player_hash ||
                expected_hashes.semantic_entity_hash !=
                    projection_hashes.semantic_entity_hash ||
                expected_hashes.semantic_area_hash !=
                    projection_hashes.semantic_area_hash ||
                expected_hashes.semantic_event_hash !=
                    projection_hashes.semantic_event_hash) {
                result.mismatch |=
                    CL_SNAPSHOT_SHADOW_PARITY_HASH_BUILD;
            }
        }
    } else {
        result.mismatch |= CL_SNAPSHOT_SHADOW_PARITY_HASH_BUILD;
    }
    return result;
}

void record_parity(const legacy_parity_result_t &parity)
{
    increment_saturating(&shadow.status.parity_comparisons);
    shadow.status.last_parity_mismatch = parity.mismatch;
    shadow.status.last_legacy_observed_parity_hash =
        parity.observed_parity_hash;
    if (parity.mismatch == 0)
        return;
    increment_saturating(&shadow.status.parity_mismatches);
    if ((parity.mismatch & CL_SNAPSHOT_SHADOW_PARITY_METADATA) != 0)
        increment_saturating(&shadow.status.parity_metadata_mismatches);
    if ((parity.mismatch & CL_SNAPSHOT_SHADOW_PARITY_PLAYER) != 0)
        increment_saturating(&shadow.status.parity_player_mismatches);
    if ((parity.mismatch &
         (CL_SNAPSHOT_SHADOW_PARITY_ENTITY_COUNT |
          CL_SNAPSHOT_SHADOW_PARITY_ENTITY_PAYLOAD)) != 0) {
        increment_saturating(&shadow.status.parity_entity_mismatches);
    }
    if ((parity.mismatch &
         (CL_SNAPSHOT_SHADOW_PARITY_AREA_COUNT |
          CL_SNAPSHOT_SHADOW_PARITY_AREA_PAYLOAD)) != 0) {
        increment_saturating(&shadow.status.parity_area_mismatches);
    }
    if ((parity.mismatch &
         (CL_SNAPSHOT_SHADOW_PARITY_EVENT_COUNT |
          CL_SNAPSHOT_SHADOW_PARITY_EVENT_PAYLOAD)) != 0) {
        increment_saturating(&shadow.status.parity_event_mismatches);
    }
    if (cl_snapshot_shadow_debug && cl_snapshot_shadow_debug->integer) {
        Com_DPrintf("snapshot shadow: legacy parity mismatch 0x%x\n",
                    parity.mismatch);
    }
}

bool snapshot_id_equal(worr_snapshot_id_v2 left,
                       worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch &&
           left.sequence == right.sequence;
}

bool projection_hashes_equal(
    const worr_snapshot_projection_hashes_v2 &left,
    const worr_snapshot_projection_hashes_v2 &right)
{
    return left.struct_size == right.struct_size &&
           left.schema_version == right.schema_version &&
           left.endpoint_hash == right.endpoint_hash &&
           left.legacy_parity_hash == right.legacy_parity_hash &&
           left.semantic_player_hash == right.semantic_player_hash &&
           left.semantic_entity_hash == right.semantic_entity_hash &&
           left.semantic_area_hash == right.semantic_area_hash &&
           left.semantic_event_hash == right.semantic_event_hash;
}

void clear_native_expectations(bool preserve_watermark)
{
    const worr_snapshot_id_v2 watermark =
        shadow.native_expectation_watermark;
    std::memset(shadow.native_expectations, 0,
                sizeof(shadow.native_expectations));
    shadow.native_expectation_watermark =
        preserve_watermark ? watermark : worr_snapshot_id_v2{};
}

uint32_t native_expectation_slot_index(worr_snapshot_id_v2 snapshot_id)
{
    static_assert(
        (shadow_slot_capacity & (shadow_slot_capacity - 1u)) == 0,
        "native expectation capacity must be a power of two");
    return (snapshot_id.sequence - 1u) &
           (shadow_slot_capacity - 1u);
}

void record_native_expectation(
    const worr_snapshot_projection_view_v2 &view,
    const worr_snapshot_projection_hashes_v2 &hashes,
    worr_snapshot_ref_v2 projection_ref, bool available)
{
    if (!shadow.native_epoch_bound || !view.snapshot ||
        !Worr_SnapshotIdValidV2(view.snapshot->snapshot_id, false) ||
        view.snapshot->snapshot_id.epoch !=
            shadow.context.profile.snapshot_epoch ||
        projection_ref.generation == 0) {
        return;
    }

    auto &slot = shadow.native_expectations[
        native_expectation_slot_index(view.snapshot->snapshot_id)];
    slot = {};
    slot.expectation.struct_size = sizeof(slot.expectation);
    slot.expectation.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    slot.expectation.snapshot_id = view.snapshot->snapshot_id;
    slot.expectation.hashes = hashes;
    slot.projection_ref = projection_ref;
    slot.qualification =
        available ? native_expectation_qualification_t::available
                  : native_expectation_qualification_t::unqualified;
    shadow.native_expectation_watermark =
        view.snapshot->snapshot_id;
}

bool native_expectation_slot_current(
    const native_expectation_slot_t &slot)
{
    if (slot.qualification ==
            native_expectation_qualification_t::empty ||
        slot.projection_ref.generation == 0) {
        return false;
    }
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    if (Worr_SnapshotQ2ProtoViewV2(
            &shadow.context, slot.projection_ref, &view, &hashes) !=
            WORR_SNAPSHOT_Q2PROTO_OK ||
        !view.snapshot ||
        !snapshot_id_equal(view.snapshot->snapshot_id,
                           slot.expectation.snapshot_id) ||
        !projection_hashes_equal(hashes, slot.expectation.hashes)) {
        return false;
    }
    return true;
}

bool reset_projection_for_demo_seek()
{
    if (!shadow.active)
        return true;
    clear_native_expectations(false);
    shadow.native_epoch_bound = false;
    if (shadow_epoch_seed == UINT32_MAX) {
        note_result(WORR_SNAPSHOT_Q2PROTO_GENERATION_EXHAUSTED);
        shadow.active = false;
        shadow.status.active = 0;
        shadow.status.lifecycle = CL_SNAPSHOT_SHADOW_LIFECYCLE_EXHAUSTED;
        return false;
    }

    /* q2proto Reset correctly invalidates map baselines.  A demo seek stays
     * within the same map, so retain only their presence bits; the immutable
     * canonical baseline values already remain in caller-owned storage. */
    for (uint32_t i = 0; i < shadow.context.profile.max_entities; ++i) {
        shadow.scratch_lineage[i].present =
            shadow.baseline_present[i] ? 1u : 0u;
    }
    ++shadow_epoch_seed;
    if (shadow_epoch_seed == 0)
        ++shadow_epoch_seed;
    const auto result = Worr_SnapshotQ2ProtoResetV2(
        &shadow.context, shadow_epoch_seed);
    note_result(result);
    if (result != WORR_SNAPSHOT_Q2PROTO_OK) {
        shadow.active = false;
        shadow.status.active = 0;
        shadow.status.lifecycle =
            result == WORR_SNAPSHOT_Q2PROTO_GENERATION_EXHAUSTED
                ? CL_SNAPSHOT_SHADOW_LIFECYCLE_EXHAUSTED
                : CL_SNAPSHOT_SHADOW_LIFECYCLE_SHUTDOWN;
        return false;
    }
    for (uint32_t i = 0; i < shadow.context.profile.max_entities; ++i) {
        shadow.baseline_present[i] =
            shadow.scratch_lineage[i].present ? 1u : 0u;
    }
    shadow.latest_ref = {};
    shadow.latest_hashes = {};
    shadow.latest_promotion_attempted = false;
    shadow.status.snapshot_epoch = shadow_epoch_seed;
    shadow.status.last_entity_count = 0;
    shadow.status.last_endpoint_hash = 0;
    shadow.status.last_legacy_parity_hash = 0;
    shadow.status.last_legacy_observed_parity_hash = 0;
    shadow.status.last_parity_mismatch = 0;
    return true;
}

} // namespace

extern "C" void CL_SnapshotShadowBeginConnection(
    uint32_t max_entities, uint32_t max_models, uint32_t max_sounds,
    bool extended_entity_state)
{
    cl_snapshot_shadow = Cvar_Get("cl_snapshot_shadow", "1", 0);
    cl_snapshot_shadow_debug =
        Cvar_Get("cl_snapshot_shadow_debug", "0", 0);

    increment_saturating(&shadow_connection_resets);
    clear_runtime_preserving_status();
    std::memset(&shadow.status, 0, sizeof(shadow.status));
    shadow.status.struct_size = sizeof(shadow.status);
    shadow.status.schema_version = CL_SNAPSHOT_SHADOW_STATUS_VERSION;
    shadow.status.connection_resets = shadow_connection_resets;
    shadow.status.consumer_attached = snapshot_consumer ? 1u : 0u;
    shadow.status.last_reset_reason =
        WORR_CGAME_SNAPSHOT_RESET_CONNECTION;

    if (shadow_epoch_seed == UINT32_MAX) {
        shadow.status.lifecycle = CL_SNAPSHOT_SHADOW_LIFECYCLE_EXHAUSTED;
        shadow.status.last_result =
            WORR_SNAPSHOT_Q2PROTO_GENERATION_EXHAUSTED;
        reset_consumer(WORR_CGAME_SNAPSHOT_RESET_HARD_RESYNC,
                       host_time_us());
        return;
    }
    ++shadow_epoch_seed;
    if (shadow_epoch_seed == 0)
        ++shadow_epoch_seed;
    shadow.status.snapshot_epoch = shadow_epoch_seed;

    if (!cl_snapshot_shadow || !cl_snapshot_shadow->integer) {
        shadow.status.lifecycle = CL_SNAPSHOT_SHADOW_LIFECYCLE_DISABLED;
        note_result(WORR_SNAPSHOT_Q2PROTO_OK);
        reset_consumer(WORR_CGAME_SNAPSHOT_RESET_CONNECTION,
                       host_time_us());
        return;
    }
    if (max_entities <= 1 || max_entities > MAX_EDICTS ||
        max_models == 0 || max_sounds == 0 ||
        !allocate_runtime_storage(max_entities)) {
        shadow.status.lifecycle = CL_SNAPSHOT_SHADOW_LIFECYCLE_SHUTDOWN;
        note_result(WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
        reset_consumer(WORR_CGAME_SNAPSHOT_RESET_HARD_RESYNC,
                       host_time_us());
        return;
    }

    worr_snapshot_q2proto_profile_v2 profile{};
    profile.struct_size = sizeof(profile);
    profile.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    profile.snapshot_epoch = shadow_epoch_seed;
    profile.max_entities = max_entities;
    profile.max_models = max_models;
    profile.max_sounds = max_sounds;
    profile.beam_renderfx_mask = RF_BEAM;
    profile.legacy_renderfx_allowed_mask = RF_SHELL_LITE_GREEN - 1u;
    profile.legacy_beam_clear_mask = RF_GLOW;
    profile.extended_entity_state = extended_entity_state ? 1u : 0u;

    worr_snapshot_q2proto_storage_v2 storage{};
    storage.struct_size = sizeof(storage);
    storage.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    storage.slots = shadow.slots;
    storage.entities = shadow.entities;
    storage.area_bytes = shadow.area_bytes;
    storage.event_refs = shadow.event_refs;
    storage.lineages = shadow.lineages;
    storage.baselines = shadow.baselines;
    storage.baseline_present = shadow.baseline_present;
    storage.scratch_entities = shadow.scratch_entities;
    storage.scratch_area_bytes = shadow.scratch_area_bytes;
    storage.scratch_event_refs = shadow.scratch_event_refs;
    storage.scratch_lineage = shadow.scratch_lineage;
    storage.slot_capacity = shadow_slot_capacity;
    storage.entities_per_slot = shadow.entities_per_slot;
    storage.area_bytes_per_slot = MAX_MAP_AREA_BYTES;
    storage.event_refs_per_slot = shadow.entities_per_slot;
    storage.entity_storage_capacity =
        shadow_slot_capacity * shadow.entities_per_slot;
    storage.area_storage_capacity =
        shadow_slot_capacity * MAX_MAP_AREA_BYTES;
    storage.event_storage_capacity =
        shadow_slot_capacity * shadow.entities_per_slot;
    storage.lineage_storage_capacity = shadow_slot_capacity * max_entities;
    storage.scratch_entity_capacity = shadow.entities_per_slot;
    storage.scratch_area_capacity = MAX_MAP_AREA_BYTES;
    storage.scratch_event_capacity = shadow.entities_per_slot;
    storage.scratch_lineage_capacity = max_entities;

    const auto result =
        Worr_SnapshotQ2ProtoInitV2(&shadow.context, &profile, &storage);
    note_result(result);
    shadow.active = result == WORR_SNAPSHOT_Q2PROTO_OK;
    shadow.status.active = shadow.active ? 1u : 0u;
    shadow.status.lifecycle = shadow.active
        ? CL_SNAPSHOT_SHADOW_LIFECYCLE_ACTIVE
        : CL_SNAPSHOT_SHADOW_LIFECYCLE_SHUTDOWN;
    if (!shadow.active)
        free_runtime_storage();
    reset_consumer(shadow.active
                       ? WORR_CGAME_SNAPSHOT_RESET_CONNECTION
                       : WORR_CGAME_SNAPSHOT_RESET_HARD_RESYNC,
                   host_time_us());
}

extern "C" bool CL_SnapshotShadowBindNativeEpoch(
    uint32_t snapshot_epoch)
{
    initialize_status();
    if (snapshot_epoch == 0 || !shadow.active)
        return false;
    if (shadow.native_epoch_bound &&
        shadow.context.profile.snapshot_epoch == snapshot_epoch) {
        return true;
    }

    abort_pending(true);
    clear_native_expectations(false);
    shadow.native_epoch_bound = false;

    const auto result = Worr_SnapshotQ2ProtoRebindEpochV2(
        &shadow.context, snapshot_epoch);
    note_result(result);
    if (result != WORR_SNAPSHOT_Q2PROTO_OK) {
        shadow.active = false;
        shadow.status.active = 0;
        shadow.status.lifecycle =
            result == WORR_SNAPSHOT_Q2PROTO_GENERATION_EXHAUSTED
                ? CL_SNAPSHOT_SHADOW_LIFECYCLE_EXHAUSTED
                : CL_SNAPSHOT_SHADOW_LIFECYCLE_SHUTDOWN;
        shadow.latest_ref = {};
        shadow.latest_hashes = {};
        shadow.latest_promotion_attempted = false;
        reset_consumer(WORR_CGAME_SNAPSHOT_RESET_HARD_RESYNC,
                       host_time_us());
        return false;
    }

    shadow_epoch_seed = snapshot_epoch;
    shadow.native_epoch_bound = true;
    shadow.latest_ref = {};
    shadow.latest_hashes = {};
    shadow.latest_promotion_attempted = false;
    shadow.status.snapshot_epoch = snapshot_epoch;
    shadow.status.last_entity_count = 0;
    shadow.status.last_endpoint_hash = 0;
    shadow.status.last_legacy_parity_hash = 0;
    shadow.status.last_legacy_observed_parity_hash = 0;
    shadow.status.last_parity_mismatch = 0;
    shadow.status.last_reset_reason =
        WORR_CGAME_SNAPSHOT_RESET_CONNECTION;
    increment_saturating(&shadow.status.lifecycle_resets);
    reset_consumer(WORR_CGAME_SNAPSHOT_RESET_CONNECTION,
                   host_time_us());
    return true;
}

extern "C" bool CL_SnapshotShadowNotifyReset(uint32_t reason,
                                               uint64_t reset_host_time_us)
{
    return CL_SnapshotShadowNotifyResetEx(reason, reset_host_time_us, 0);
}

extern "C" bool CL_SnapshotShadowNotifyResetEx(
    uint32_t reason, uint64_t reset_host_time_us, uint32_t reset_flags)
{
    initialize_status();
    if (reason < WORR_CGAME_SNAPSHOT_RESET_CONNECTION ||
        reason > WORR_CGAME_SNAPSHOT_RESET_UNLOAD ||
        (reset_flags & ~CL_SNAPSHOT_SHADOW_RESET_PROJECTION_EPOCH) != 0 ||
        ((reset_flags & CL_SNAPSHOT_SHADOW_RESET_PROJECTION_EPOCH) != 0 &&
         reason != WORR_CGAME_SNAPSHOT_RESET_DEMO_SEEK)) {
        return false;
    }
    abort_pending(true);
    if ((reset_flags &
         CL_SNAPSHOT_SHADOW_RESET_PROJECTION_EPOCH) == 0) {
        const bool preserve_watermark =
            reason == WORR_CGAME_SNAPSHOT_RESET_DEMO_SEEK;
        clear_native_expectations(preserve_watermark);
        if (!preserve_watermark)
            shadow.native_epoch_bound = false;
    }
    if (reason != WORR_CGAME_SNAPSHOT_RESET_DEMO_SEEK ||
        (reset_flags &
         CL_SNAPSHOT_SHADOW_RESET_PROJECTION_EPOCH) != 0) {
        reset_snapshot_clock();
    }
    shadow.status.last_reset_reason = reason;
    increment_saturating(&shadow.status.lifecycle_resets);
    const bool projection_ok =
        (reset_flags & CL_SNAPSHOT_SHADOW_RESET_PROJECTION_EPOCH) == 0 ||
        reset_projection_for_demo_seek();
    reset_consumer(reason, reset_host_time_us);
    return projection_ok;
}

extern "C" void CL_SnapshotShadowShutdown(void)
{
    initialize_status();
    (void)CL_SnapshotShadowNotifyReset(
        WORR_CGAME_SNAPSHOT_RESET_HARD_RESYNC, host_time_us());
    clear_runtime_preserving_status();
    shadow.status.active = 0;
    shadow.status.pending_frame = 0;
    shadow.status.lifecycle = CL_SNAPSHOT_SHADOW_LIFECYCLE_SHUTDOWN;
    shadow.status.storage_entities_per_slot = 0;
    shadow.status.allocation_bytes = 0;
}

extern "C" void CL_SnapshotShadowSetBaseline(
    uint32_t entity_index,
    const q2proto_entity_state_delta_t *baseline_delta)
{
    if (!shadow.active)
        return;
    increment_saturating(&shadow.status.baseline_attempts);
    const auto result = Worr_SnapshotQ2ProtoSetBaselineV2(
        &shadow.context, entity_index, baseline_delta);
    note_result(result);
    if (result != WORR_SNAPSHOT_Q2PROTO_OK)
        increment_saturating(&shadow.status.baseline_failures);
}

extern "C" void CL_SnapshotShadowBeginFrame(
    const q2proto_svc_frame_t *frame)
{
    if (!shadow.active)
        return;
    abort_pending(true);
    increment_saturating(&shadow.status.frame_attempts);
    shadow.status.last_capture_failure = CL_SNAPSHOT_SHADOW_CAPTURE_NONE;
    if (!frame) {
        note_capture_failure(CL_SNAPSHOT_SHADOW_CAPTURE_NULL_FRAME);
        increment_saturating(&shadow.status.frame_failures);
        note_result(WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
        return;
    }
    shadow.pending_frame = *frame;
    shadow.pending = true;
    shadow.status.pending_frame = 1;
    if (frame->areabits_len > MAX_MAP_AREA_BYTES) {
        note_capture_failure(CL_SNAPSHOT_SHADOW_CAPTURE_AREA_CAPACITY);
        increment_saturating(&shadow.status.capture_overflows);
    } else if (frame->areabits_len != 0 && !frame->areabits) {
        note_capture_failure(CL_SNAPSHOT_SHADOW_CAPTURE_AREA_POINTER);
    } else if (frame->areabits_len != 0) {
        std::memcpy(shadow.pending_area_bytes, frame->areabits,
                    frame->areabits_len);
        shadow.pending_frame.areabits = shadow.pending_area_bytes;
    } else {
        shadow.pending_frame.areabits = nullptr;
    }
}

extern "C" bool CL_SnapshotShadowSetConsumedCommand(
    const worr_snapshot_consumed_command_v2 *consumed_command)
{
    if (!shadow.active)
        return true;
    if (!shadow.pending || !consumed_command ||
        consumed_command->reserved0 != 0 ||
        (consumed_command->provenance ==
                 WORR_SNAPSHOT_CONSUMED_COMMAND_NONE
             ? (consumed_command->cursor.epoch != 0 ||
                consumed_command->cursor.contiguous_sequence != 0)
             : (consumed_command->provenance !=
                    WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED ||
                consumed_command->cursor.epoch == 0))) {
        note_capture_failure(
            CL_SNAPSHOT_SHADOW_CAPTURE_CONSUMED_COMMAND);
        return false;
    }
    shadow.pending_consumed_command = *consumed_command;
    return true;
}

extern "C" void CL_SnapshotShadowCaptureEntityDelta(
    const q2proto_svc_frame_entity_delta_t *delta)
{
    if (!shadow.active || !shadow.pending)
        return;
    if (!delta) {
        note_capture_failure(CL_SNAPSHOT_SHADOW_CAPTURE_NULL_DELTA);
        return;
    }
    if (shadow.capture_failure != CL_SNAPSHOT_SHADOW_CAPTURE_NONE)
        return;
    if (shadow.pending_delta_count == shadow.pending_delta_capacity) {
        note_capture_failure(CL_SNAPSHOT_SHADOW_CAPTURE_DELTA_CAPACITY);
        increment_saturating(&shadow.status.capture_overflows);
        return;
    }
    shadow.pending_deltas[shadow.pending_delta_count++] = *delta;
}

extern "C" void CL_SnapshotShadowAbortFrame(void)
{
    abort_pending(true);
}

extern "C" bool CL_SnapshotShadowAcceptFrame(
    uint64_t server_time_us, uint32_t controlled_entity_index,
    int32_t canonical_movement_type, uint16_t canonical_movement_flags,
    uint8_t team_id)
{
    if (shadow.active && !shadow.pending &&
        shadow.latest_ref.generation != 0) {
        return CL_SnapshotShadowPromoteLatestFrame(
            controlled_entity_index);
    }
    return CL_SnapshotShadowAcceptFrameEx(
        server_time_us, controlled_entity_index, canonical_movement_type,
        canonical_movement_flags, team_id,
        CL_SNAPSHOT_SHADOW_ACCEPT_DELIVER_CONSUMER |
            CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY);
}

extern "C" bool CL_SnapshotShadowAcceptFrameEx(
    uint64_t server_time_us, uint32_t controlled_entity_index,
    int32_t canonical_movement_type, uint16_t canonical_movement_flags,
    uint8_t team_id, uint32_t accept_flags)
{
    if (!shadow.active || !shadow.pending)
        return false;
    shadow.status.last_accept_flags = accept_flags;
    if ((accept_flags & ~accept_known_flags) != 0) {
        increment_saturating(&shadow.status.frame_failures);
        note_result(WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
        abort_pending(false);
        return false;
    }
    if (controlled_entity_index == CL_SNAPSHOT_SHADOW_NO_CONTROLLED_ENTITY) {
        note_capture_failure(
            CL_SNAPSHOT_SHADOW_CAPTURE_NO_CONTROLLED_ENTITY);
        increment_saturating(
            &shadow.status.frames_without_controlled_entity);
        increment_saturating(&shadow.status.frame_failures);
        note_result(WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
        abort_pending(false);
        return false;
    }
    if (controlled_entity_index == 0 ||
        controlled_entity_index >= shadow.context.profile.max_entities) {
        note_capture_failure(
            CL_SNAPSHOT_SHADOW_CAPTURE_CONTROLLED_ENTITY_RANGE);
        increment_saturating(&shadow.status.frame_failures);
        note_result(WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
        abort_pending(false);
        return false;
    }
    if (shadow.capture_failure != CL_SNAPSHOT_SHADOW_CAPTURE_NONE) {
        increment_saturating(&shadow.status.frame_failures);
        note_result(shadow.capture_failure ==
                            CL_SNAPSHOT_SHADOW_CAPTURE_AREA_CAPACITY ||
                        shadow.capture_failure ==
                            CL_SNAPSHOT_SHADOW_CAPTURE_DELTA_CAPACITY
                    ? WORR_SNAPSHOT_Q2PROTO_CAPACITY
                    : WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
        abort_pending(false);
        return false;
    }
    if (shadow.pending_delta_count == 0 ||
        shadow.pending_deltas[shadow.pending_delta_count - 1u].newnum != 0) {
        note_capture_failure(
            CL_SNAPSHOT_SHADOW_CAPTURE_MISSING_TERMINATOR);
        increment_saturating(&shadow.status.frame_failures);
        note_result(WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
        abort_pending(false);
        return false;
    }

    worr_snapshot_q2proto_frame_input_v2 input{};
    input.struct_size = sizeof(input);
    input.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    input.frame = &shadow.pending_frame;
    input.entity_deltas = shadow.pending_deltas;
    input.entity_delta_count = shadow.pending_delta_count;
    input.flags = WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_TYPE_VALID |
                  WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_FLAGS_VALID |
                  WORR_SNAPSHOT_Q2PROTO_FRAME_TEAM_ID_VALID;
    /* Frame zero is the genuine initial keyframe, not a late observer attach. */
    if (shadow.context.last_observed.epoch == 0 &&
        shadow.pending_frame.serverframe > 0) {
        input.flags |= WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH;
    } else if (shadow.context.last_observed.epoch != 0 &&
               shadow.pending_frame.deltaframe <= 0) {
        input.flags |= WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID;
        input.lineage_parent_serverframe = static_cast<int32_t>(
            shadow.context.last_observed.sequence - 1u);
    }
    input.controlled_entity_index = controlled_entity_index;
    input.canonical_movement_type = canonical_movement_type;
    input.canonical_movement_flags = canonical_movement_flags;
    input.team_id = team_id;
    input.server_time_us = server_time_us;
    input.consumed_command = shadow.pending_consumed_command;

    worr_snapshot_ref_v2 ref{};
    const auto result =
        Worr_SnapshotQ2ProtoPublishV2(&shadow.context, &input, &ref);
    note_result(result);
    abort_pending(false);
    if (result != WORR_SNAPSHOT_Q2PROTO_OK) {
        increment_saturating(&shadow.status.frame_failures);
        return false;
    }

    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    const auto view_result = Worr_SnapshotQ2ProtoViewV2(
        &shadow.context, ref, &view, &hashes);
    note_result(view_result);
    if (view_result != WORR_SNAPSHOT_Q2PROTO_OK) {
        increment_saturating(&shadow.status.frame_failures);
        return false;
    }

    shadow.latest_ref = ref;
    shadow.latest_hashes = hashes;
    shadow.latest_promotion_attempted = false;
    increment_saturating(&shadow.status.frames_projected);
    increment_saturating(&shadow.status.frames_published);
    shadow.status.last_entity_count = view.entity_count;
    if (view.entity_count > shadow.status.entity_high_water)
        shadow.status.entity_high_water = view.entity_count;
    shadow.status.last_endpoint_hash = hashes.endpoint_hash;
    shadow.status.last_legacy_parity_hash = hashes.legacy_parity_hash;
    shadow.status.last_parity_mismatch = 0;
    shadow.status.last_legacy_observed_parity_hash = 0;

    bool parity_qualified = false;
    if ((accept_flags & CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY) != 0) {
        const auto parity = compare_accepted_legacy_frame(
            view, hashes, controlled_entity_index, server_time_us);
        record_parity(parity);
        parity_qualified = parity.mismatch == 0;
    }
    const bool promotion_eligible =
        parity_qualified &&
        (view.snapshot->flags & WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE) != 0;
    if (promotion_eligible)
        increment_saturating(&shadow.status.frames_promotion_eligible);
    record_native_expectation(view, hashes, ref, promotion_eligible);
    if (promotion_eligible && snapshot_record_observer)
        snapshot_record_observer(&view, &hashes, ref);

    if ((accept_flags & CL_SNAPSHOT_SHADOW_ACCEPT_DELIVER_CONSUMER) == 0) {
        increment_saturating(&shadow.status.frames_lineage_only);
        return true;
    }
    if (!promotion_eligible) {
        shadow.latest_promotion_attempted = true;
        increment_saturating(&shadow.status.promotion_blocks);
        return false;
    }
    shadow.latest_promotion_attempted = true;
    if (!snapshot_consumer)
        return true;

    increment_saturating(&shadow.status.consumer_attempts);
    if (!snapshot_consumer->ConsumeCanonicalSnapshot(
            &view, &hashes, host_time_us())) {
        note_consumer_rejection();
        return false;
    }
    increment_saturating(&shadow.status.consumer_accepts);
    return true;
}

extern "C" bool CL_SnapshotShadowPromoteLatestFrame(
    uint32_t controlled_entity_index)
{
    if (!shadow.active || shadow.latest_ref.generation == 0 ||
        shadow.latest_promotion_attempted ||
        controlled_entity_index == CL_SNAPSHOT_SHADOW_NO_CONTROLLED_ENTITY ||
        controlled_entity_index == 0 ||
        controlled_entity_index >= shadow.context.profile.max_entities) {
        return false;
    }
    shadow.status.last_accept_flags =
        CL_SNAPSHOT_SHADOW_ACCEPT_DELIVER_CONSUMER |
        CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY;

    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    const auto view_result = Worr_SnapshotQ2ProtoViewV2(
        &shadow.context, shadow.latest_ref, &view, &hashes);
    note_result(view_result);
    if (view_result != WORR_SNAPSHOT_Q2PROTO_OK)
        return false;

    const auto parity = compare_accepted_legacy_frame(
        view, hashes, controlled_entity_index,
        view.snapshot->server_time_us);
    record_parity(parity);
    const bool promotion_eligible =
        parity.mismatch == 0 &&
        (view.snapshot->flags & WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE) != 0;
    record_native_expectation(
        view, hashes, shadow.latest_ref, promotion_eligible);
    if (promotion_eligible && snapshot_record_observer) {
        snapshot_record_observer(&view, &hashes, shadow.latest_ref);
    }
    shadow.latest_promotion_attempted = true;
    if (!promotion_eligible) {
        increment_saturating(&shadow.status.promotion_blocks);
        return false;
    }
    increment_saturating(&shadow.status.frames_promotion_eligible);
    if (!snapshot_consumer)
        return true;

    increment_saturating(&shadow.status.consumer_attempts);
    if (!snapshot_consumer->ConsumeCanonicalSnapshot(
            &view, &hashes, host_time_us())) {
        note_consumer_rejection();
        return false;
    }
    increment_saturating(&shadow.status.consumer_accepts);
    return true;
}

extern "C" bool CL_SnapshotShadowLatest(
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out,
    worr_snapshot_ref_v2 *ref_out)
{
    if (!shadow.active || !view_out || !hashes_out || !ref_out ||
        shadow.latest_ref.generation == 0) {
        return false;
    }
    const auto result = Worr_SnapshotQ2ProtoViewV2(
        &shadow.context, shadow.latest_ref, view_out, hashes_out);
    note_result(result);
    if (result != WORR_SNAPSHOT_Q2PROTO_OK)
        return false;
    *ref_out = shadow.latest_ref;
    return true;
}

extern "C" bool CL_SnapshotShadowSetRecordObserver(
    cl_snapshot_shadow_record_observer_v1 observer)
{
    snapshot_record_observer = observer;
    return true;
}

extern "C"
cl_snapshot_shadow_native_expectation_result_v1
CL_SnapshotShadowGetNativeExpectation(
    worr_snapshot_id_v2 snapshot_id,
    worr_native_snapshot_expectation_v1 *expectation_out)
{
    if (expectation_out)
        *expectation_out = {};
    if (!expectation_out ||
        !Worr_SnapshotIdValidV2(snapshot_id, false) ||
        !shadow.active || !shadow.native_epoch_bound) {
        return CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_INVALID;
    }
    if (snapshot_id.epoch != shadow.context.profile.snapshot_epoch) {
        return CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_WRONG_EPOCH;
    }

    auto &slot = shadow.native_expectations[
        native_expectation_slot_index(snapshot_id)];
    if (slot.qualification !=
            native_expectation_qualification_t::empty &&
        snapshot_id_equal(slot.expectation.snapshot_id, snapshot_id)) {
        if (slot.qualification ==
            native_expectation_qualification_t::unqualified) {
            return CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_UNQUALIFIED;
        }
        if (slot.qualification !=
                native_expectation_qualification_t::available ||
            !native_expectation_slot_current(slot)) {
            slot = {};
            return CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_STALE;
        }
        *expectation_out = slot.expectation;
        return CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE;
    }

    if (!Worr_SnapshotIdValidV2(
            shadow.native_expectation_watermark, true) ||
        shadow.native_expectation_watermark.epoch == 0 ||
        snapshot_id.sequence >
            shadow.native_expectation_watermark.sequence) {
        return CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_PENDING;
    }
    return CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_STALE;
}

extern "C" bool CL_SnapshotShadowGetStatus(
    cl_snapshot_shadow_status_v1 *status_out)
{
    if (!status_out)
        return false;
    initialize_status();
    *status_out = shadow.status;
    return true;
}

extern "C" void CL_SnapshotShadowStatus_f(void)
{
    cl_snapshot_shadow_status_v1 status{};

    if (!CL_SnapshotShadowGetStatus(&status))
        return;

    Com_Printf(
        "snapshot shadow: active=%u lifecycle=%u epoch=%u pending=%u "
        "last_result=%u capture_failure=%u parity_last=0x%x "
        "accept_flags=0x%x consumer=%u consumer_last_rejection=%u\n",
        status.active, status.lifecycle, status.snapshot_epoch,
        status.pending_frame, status.last_result,
        status.last_capture_failure, status.last_parity_mismatch,
        status.last_accept_flags, status.consumer_attached,
        status.last_consumer_rejection_result);
    Com_Printf(
        "snapshot shadow telemetry: attempts=%" PRIu64
        " projected=%" PRIu64 " published=%" PRIu64
        " lineage_only=%" PRIu64 " promotion_eligible=%" PRIu64
        " comparisons=%" PRIu64 " mismatches=%" PRIu64
        " entity_mismatches=%" PRIu64 " frame_failures=%" PRIu64
        " capture_overflows=%" PRIu64 " promotion_blocks=%" PRIu64
        " consumer_attempts=%" PRIu64 " consumer_accepts=%" PRIu64
        " consumer_rejections=%" PRIu64 "\n",
        status.frame_attempts, status.frames_projected,
        status.frames_published, status.frames_lineage_only,
        status.frames_promotion_eligible, status.parity_comparisons,
        status.parity_mismatches, status.parity_entity_mismatches,
        status.frame_failures, status.capture_overflows,
        status.promotion_blocks, status.consumer_attempts,
        status.consumer_accepts, status.consumer_rejections);
}

extern "C" bool CL_SnapshotShadowSetConsumer(
    const worr_cgame_snapshot_timeline_export_v2 *consumer)
{
    initialize_status();
    if (consumer && !consumer_export_valid(consumer))
        return false;
    if (consumer == snapshot_consumer)
        return true;
    if (snapshot_consumer)
        reset_consumer(WORR_CGAME_SNAPSHOT_RESET_UNLOAD, host_time_us());
    invalidate_native_consumer_lifetime();
    snapshot_consumer = consumer;
    shadow.status.consumer_attached = snapshot_consumer ? 1u : 0u;
    if (snapshot_consumer && shadow.active) {
        reset_consumer(WORR_CGAME_SNAPSHOT_RESET_CONNECTION,
                       host_time_us());
    }
    return true;
}

extern "C" bool CL_SnapshotShadowGetNativeConsumerV1(
    worr_native_snapshot_consumer_v1 *consumer_out)
{
    if (!consumer_out)
        return false;
    *consumer_out = {};
    if (snapshot_consumer_lifetime_exhausted ||
        snapshot_consumer_lifetime_cookie == 0 ||
        !consumer_export_valid(snapshot_consumer)) {
        return false;
    }
    consumer_out->struct_size = sizeof(*consumer_out);
    consumer_out->schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    consumer_out->opaque = reinterpret_cast<void *>(
        snapshot_consumer_lifetime_cookie);
    consumer_out->Reset = native_consumer_reset;
    consumer_out->ConsumeCanonicalSnapshot = native_consumer_consume;
    consumer_out->GetStatus = native_consumer_get_status;
    return true;
}

extern "C" bool CL_SnapshotShadowServerTimeUs(
    int32_t server_frame, uint32_t frame_time_ms,
    uint64_t *server_time_us_out)
{
    if (!server_time_us_out || server_frame < 0 || frame_time_ms == 0)
        return false;
    const uint64_t frame_us =
        static_cast<uint64_t>(frame_time_ms) * UINT64_C(1000);
    const uint64_t frame = static_cast<uint64_t>(server_frame);
    if (frame != 0 && frame_us > UINT64_MAX / frame)
        return false;
    *server_time_us_out = frame * frame_us;
    return true;
}

extern "C" bool CL_SnapshotShadowResolveServerTimeUs(
    int32_t server_frame, uint32_t frame_time_ms,
    uint64_t *server_time_us_out)
{
    if (!server_time_us_out || server_frame < 0 || frame_time_ms == 0)
        return false;

    if (!shadow.clock_initialized) {
        uint64_t initial_time_us = 0;
        if (!CL_SnapshotShadowServerTimeUs(
                server_frame, frame_time_ms, &initial_time_us)) {
            return false;
        }
        shadow.clock_last_server_frame = server_frame;
        shadow.clock_last_server_time_us = initial_time_us;
        shadow.clock_initialized = true;
        *server_time_us_out = initial_time_us;
        return true;
    }

    if (server_frame < shadow.clock_last_server_frame)
        return false;
    if (server_frame == shadow.clock_last_server_frame) {
        *server_time_us_out = shadow.clock_last_server_time_us;
        return true;
    }

    const uint64_t frame_delta = static_cast<uint64_t>(
        server_frame - shadow.clock_last_server_frame);
    const uint64_t interval_us =
        static_cast<uint64_t>(frame_time_ms) * UINT64_C(1000);
    if (frame_delta > UINT64_MAX / interval_us)
        return false;
    const uint64_t elapsed_us = frame_delta * interval_us;
    if (shadow.clock_last_server_time_us > UINT64_MAX - elapsed_us)
        return false;

    shadow.clock_last_server_frame = server_frame;
    shadow.clock_last_server_time_us += elapsed_us;
    *server_time_us_out = shadow.clock_last_server_time_us;
    return true;
}

extern "C" bool CL_SnapshotShadowObserveExactServerTimeUs(
    int32_t server_frame, uint64_t server_time_us)
{
    if (server_frame < 0)
        return false;
    if (!shadow.clock_initialized) {
        shadow.clock_last_server_frame = server_frame;
        shadow.clock_last_server_time_us = server_time_us;
        shadow.clock_initialized = true;
        return true;
    }
    if (server_frame < shadow.clock_last_server_frame)
        return false;
    if (server_frame == shadow.clock_last_server_frame)
        return server_time_us == shadow.clock_last_server_time_us;
    if (server_time_us <= shadow.clock_last_server_time_us)
        return false;
    shadow.clock_last_server_frame = server_frame;
    shadow.clock_last_server_time_us = server_time_us;
    return true;
}
