/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"

#include "client/cgame_event_shadow_runtime.h"
#include "client/snapshot_shadow.h"
#include "common/net/legacy_damage_event_candidate.h"
#include "common/net/legacy_poi_event_candidate.h"
#include "common/net/legacy_muzzle_event_candidate.h"
#include "common/net/legacy_spatial_audio_event_candidate.h"
#include "common/net/legacy_temp_event_candidate.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace {

static_assert(static_cast<uint32_t>(EV_NONE) == 0);
static_assert(static_cast<uint32_t>(EV_LADDER_STEP) ==
              WORR_CGAME_EVENT_SHADOW_MAX_LEGACY_EVENT);
static_assert(WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2 ==
              Q2PROTO_MAX_DAMAGE_INDICATORS);

std::array<worr_cgame_event_shadow_observed_v1,
           WORR_CGAME_EVENT_SHADOW_MAX_ENTITIES>
    observed_entities_v1;
std::array<uint32_t, WORR_CGAME_EVENT_SHADOW_MAX_ENTITIES> seen_markers_v1;
std::array<worr_event_record_v1, WORR_CGAME_EVENT_SHADOW_MAX_RECORDS>
    scratch_records_v1;
std::array<worr_cgame_event_shadow_carrier_v1,
           WORR_CGAME_EVENT_SHADOW_MAX_ENTITIES>
    frame_carriers_v1;
worr_cgame_event_shadow_builder_v1 builder_v1;
const worr_cgame_event_shadow_export_v1 *event_consumer_v1;

std::array<worr_cgame_event_observed_v2,
           WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2>
    observed_entities_v2;
std::array<uint32_t, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2> seen_markers_v2;
std::array<worr_event_record_v1, WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2>
    scratch_records_v2;
std::array<worr_cgame_event_carrier_v2,
           WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2>
    frame_carriers_v2;
worr_cgame_event_range_builder_v2 builder_v2;
const worr_cgame_event_range_export_v2 *event_consumer_v2;
uint32_t epoch_seed;
bool builder_v1_initialized;
bool builder_v2_initialized;

void consume_bridge(void *, const worr_cgame_event_range_v1 *range)
{
    if (event_consumer_v1)
        event_consumer_v1->ConsumeCanonicalEventRange(range);
}

void consume_bridge_v2(void *, const worr_cgame_event_range_v2 *range)
{
    if (event_consumer_v2)
        event_consumer_v2->ConsumeCanonicalEventRange(range);
}

bool reset_builder(uint32_t reason)
{
    if (epoch_seed == UINT32_MAX) {
        builder_v1_initialized = false;
        builder_v2_initialized = false;
        return false;
    }
    ++epoch_seed;
    if (!builder_v1_initialized) {
        builder_v1_initialized = Worr_CGameEventShadowBuilderInitV1(
            &builder_v1, observed_entities_v1.data(), seen_markers_v1.data(),
            static_cast<uint32_t>(observed_entities_v1.size()),
            scratch_records_v1.data(),
            static_cast<uint32_t>(scratch_records_v1.size()), epoch_seed);
    } else {
        builder_v1_initialized =
            Worr_CGameEventShadowBuilderResetV1(&builder_v1, epoch_seed);
    }
    if (!builder_v2_initialized) {
        builder_v2_initialized = Worr_CGameEventRangeBuilderInitV2(
            &builder_v2, observed_entities_v2.data(), seen_markers_v2.data(),
            static_cast<uint32_t>(observed_entities_v2.size()),
            scratch_records_v2.data(),
            static_cast<uint32_t>(scratch_records_v2.size()), epoch_seed);
    } else {
        builder_v2_initialized =
            Worr_CGameEventRangeBuilderResetV2(&builder_v2, epoch_seed);
    }
    if (builder_v1_initialized && event_consumer_v1)
        event_consumer_v1->Reset(epoch_seed, reason);
    if (builder_v2_initialized && event_consumer_v2)
        event_consumer_v2->Reset(epoch_seed, reason);
    return builder_v1_initialized || builder_v2_initialized;
}

uint64_t frame_time_us()
{
    if (cl.servertime <= 0)
        return 0;
    return static_cast<uint64_t>(cl.servertime) * UINT64_C(1000);
}

uint32_t action_tick()
{
    return cl.frame.number < 0 ? 0u
                               : static_cast<uint32_t>(cl.frame.number);
}

uint32_t demo_range_flags_v2()
{
    uint32_t flags = 0;
    if (cls.demo.playback)
        flags |= WORR_CGAME_EVENT_RANGE_DEMO_PLAYBACK_V2;
    if (cls.demo.seeking)
        flags |= WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2;
    return flags;
}

uint32_t runtime_entity_capacity()
{
    const uint32_t configured = static_cast<uint32_t>(cl.csr.max_edicts);
    if (cl.csr.max_edicts <= 1)
        return 0;
    return configured < WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2
               ? configured
               : WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2;
}

bool synchronize_controlled_action_lineage(
    uint32_t controlled_entity_index, uint32_t capacity)
{
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    worr_snapshot_ref_v2 projection_ref{};

    if (controlled_entity_index == 0 ||
        controlled_entity_index >= capacity ||
        (!builder_v2_initialized &&
         !reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE)) ||
        !builder_v2_initialized || builder_v2.in_callback ||
        !CL_SnapshotShadowLatest(&view, &hashes, &projection_ref) ||
        !view.snapshot || !view.player ||
        view.snapshot->server_tick != action_tick() ||
        !Worr_SnapshotGenerationValidV2(
            view.snapshot->controlled_entity, capacity, false) ||
        !Worr_SnapshotGenerationValidV2(
            view.player->controlled_entity, capacity, false)) {
        return false;
    }

    const auto &snapshot_controlled =
        view.snapshot->controlled_entity;
    const auto &player_controlled = view.player->controlled_entity;
    if (snapshot_controlled.identity.index != controlled_entity_index ||
        player_controlled.identity.index != controlled_entity_index ||
        snapshot_controlled.identity.generation !=
            player_controlled.identity.generation ||
        snapshot_controlled.provenance_flags !=
            player_controlled.provenance_flags) {
        return false;
    }

    auto &observed = builder_v2.observed[controlled_entity_index];
    if (observed.generation >
        snapshot_controlled.identity.generation) {
        return false;
    }

    /* First-person packet frames may omit the controlled entity while the
     * canonical snapshot deliberately preserves its lifecycle.  Recipient-
     * local actions are implicitly addressed to that entity, so bind the
     * action builder to the retained canonical identity instead of previewing
     * a fabricated next generation from packet-frame absence.  No other
     * observed slot is changed; ordinary packet entities retain their
     * existing lifecycle. */
    observed.generation = snapshot_controlled.identity.generation;
    observed.present = 1;
    observed.provisional = 0;
    observed.last_seen_batch = builder_v2.batch_generation;
    return true;
}

bool runtime_entity_valid(int32_t entity, bool allow_world)
{
    const uint32_t capacity = runtime_entity_capacity();
    return capacity != 0 && entity >= (allow_world ? 0 : 1) &&
           static_cast<uint32_t>(entity) < capacity;
}

void initialize_action_candidate(
    worr_cgame_event_action_candidate_v2 *candidate,
    uint16_t event_type,
    uint16_t payload_kind,
    uint16_t payload_size)
{
    std::memset(candidate, 0, sizeof(*candidate));
    candidate->struct_size = sizeof(*candidate);
    candidate->source_entity_index = 0;
    candidate->subject_entity_index = WORR_EVENT_NO_ENTITY;
    candidate->record.struct_size = sizeof(candidate->record);
    candidate->record.schema_version = WORR_EVENT_ABI_VERSION;
    candidate->record.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate->record.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                              WORR_EVENT_FLAG_PRESENT_ONCE;
    candidate->record.source_tick = action_tick();
    candidate->record.source_time_us = frame_time_us();
    candidate->record.source_entity.index = WORR_EVENT_NO_ENTITY;
    candidate->record.subject_entity.index = WORR_EVENT_NO_ENTITY;
    candidate->record.event_type = event_type;
    candidate->record.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    candidate->record.prediction_class =
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate->record.expiry_tick = candidate->record.source_tick + 1u;
    candidate->record.payload_kind = payload_kind;
    candidate->record.payload_size = payload_size;
}

void deliver_rejected_action(uint32_t carrier_kind, uint32_t adapter_status)
{
    if (!builder_v2_initialized &&
        !reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE)) {
        return;
    }
    if (!builder_v2_initialized)
        return;
    (void)Worr_CGameEventRangeDeliverRejectedActionV2(
        &builder_v2, action_tick(), frame_time_us(), carrier_kind,
        adapter_status, demo_range_flags_v2(), consume_bridge_v2, nullptr);
}

void deliver_action_candidate(
    const worr_cgame_event_action_candidate_v2 *candidate,
    uint32_t carrier_kind)
{
    worr_cgame_event_range_build_result_v2 result;
    if (!builder_v2_initialized &&
        !reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE)) {
        return;
    }
    if (!builder_v2_initialized)
        return;
    result = Worr_CGameEventRangeDeliverActionV2(
        &builder_v2, candidate, carrier_kind, demo_range_flags_v2(),
        consume_bridge_v2, nullptr);
    if (result == WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2) {
        deliver_rejected_action(
            carrier_kind, WORR_CGAME_EVENT_ADAPTER_PAYLOAD_INVALID_V2);
    }
}

bool build_temp_candidate(
    const q2proto_svc_temp_entity_t *temp_entity,
    worr_cgame_event_action_candidate_v2 *candidate,
    uint32_t *adapter_status)
{
    worr_event_record_v1 record{};
    uint32_t source_entity_index = 0;
    uint32_t subject_entity_index = WORR_EVENT_NO_ENTITY;
    const auto result = Worr_LegacyTempEventCandidateBuildV1(
        temp_entity, action_tick(), frame_time_us(), runtime_entity_capacity(),
        &record, &source_entity_index, &subject_entity_index);

    switch (result) {
    case WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK:
        std::memset(candidate, 0, sizeof(*candidate));
        candidate->struct_size = sizeof(*candidate);
        candidate->source_entity_index = source_entity_index;
        candidate->subject_entity_index = subject_entity_index;
        candidate->record = record;
        *adapter_status = WORR_CGAME_EVENT_ADAPTER_OK_V2;
        return true;
    case WORR_LEGACY_TEMP_EVENT_CANDIDATE_UNSUPPORTED_SUBTYPE:
        *adapter_status = WORR_CGAME_EVENT_ADAPTER_UNSUPPORTED_ID_V2;
        return false;
    case WORR_LEGACY_TEMP_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE:
        *adapter_status = WORR_CGAME_EVENT_ADAPTER_ENTITY_OUT_OF_RANGE_V2;
        return false;
    case WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_ARGUMENT:
    case WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE:
    case WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_RECORD:
    default:
        *adapter_status = WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2;
        return false;
    }
}

bool build_spatial_audio_candidate(
    const q2proto_sound_t *sound,
    worr_cgame_event_action_candidate_v2 *candidate,
    uint32_t *adapter_status)
{
    worr_event_record_v1 record{};
    uint32_t source_entity_index = 0;
    const auto result = Worr_LegacySpatialAudioEventCandidateBuildV1(
        sound, action_tick(), frame_time_us(), runtime_entity_capacity(),
        &record, &source_entity_index);

    switch (result) {
    case WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_OK:
        std::memset(candidate, 0, sizeof(*candidate));
        candidate->struct_size = sizeof(*candidate);
        candidate->source_entity_index = source_entity_index;
        candidate->subject_entity_index = WORR_EVENT_NO_ENTITY;
        candidate->record = record;
        *adapter_status = WORR_CGAME_EVENT_ADAPTER_OK_V2;
        return true;
    case WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE:
        *adapter_status = WORR_CGAME_EVENT_ADAPTER_ENTITY_OUT_OF_RANGE_V2;
        return false;
    case WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_ARGUMENT:
    case WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_SHAPE:
    case WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_RECORD:
    default:
        *adapter_status = WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2;
        return false;
    }
}

bool build_muzzle_candidate(
    const q2proto_svc_muzzleflash_t *muzzleflash, uint32_t family,
    worr_cgame_event_action_candidate_v2 *candidate,
    uint32_t *adapter_status)
{
    worr_event_record_v1 record{};
    uint32_t source_entity_index = 0;
    const auto result = Worr_LegacyMuzzleEventCandidateBuildV1(
        muzzleflash, family, action_tick(), frame_time_us(),
        runtime_entity_capacity(), &record, &source_entity_index);

    switch (result) {
    case WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK:
        std::memset(candidate, 0, sizeof(*candidate));
        candidate->struct_size = sizeof(*candidate);
        candidate->source_entity_index = source_entity_index;
        candidate->subject_entity_index = WORR_EVENT_NO_ENTITY;
        candidate->record = record;
        *adapter_status = WORR_CGAME_EVENT_ADAPTER_OK_V2;
        return true;
    case WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE:
        *adapter_status = WORR_CGAME_EVENT_ADAPTER_ENTITY_OUT_OF_RANGE_V2;
        return false;
    case WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_ARGUMENT:
    case WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE:
    case WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_RECORD:
    default:
        *adapter_status = WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2;
        return false;
    }
}

} // namespace

extern "C" void CL_EventShadowSetConsumer(
    const worr_cgame_event_shadow_export_v1 *consumer)
{
    event_consumer_v1 = consumer;
    if (!event_consumer_v1)
        return;
    if (!builder_v1_initialized) {
        (void)reset_builder(
            WORR_CGAME_EVENT_SHADOW_RESET_CONSUMER_ATTACH);
        return;
    }
    event_consumer_v1->Reset(
        builder_v1.stream_epoch,
        WORR_CGAME_EVENT_SHADOW_RESET_CONSUMER_ATTACH);
}

extern "C" void CL_EventRangeSetConsumerV2(
    const worr_cgame_event_range_export_v2 *consumer)
{
    event_consumer_v2 = consumer;
    if (!event_consumer_v2)
        return;
    if (!builder_v2_initialized) {
        (void)reset_builder(
            WORR_CGAME_EVENT_SHADOW_RESET_CONSUMER_ATTACH);
        return;
    }
    event_consumer_v2->Reset(
        builder_v2.stream_epoch,
        WORR_CGAME_EVENT_SHADOW_RESET_CONSUMER_ATTACH);
}

extern "C" void CL_EventShadowReset(uint32_t reason)
{
    (void)reset_builder(reason);
}

extern "C" void CL_EventShadowDeliverAcceptedFrame(void)
{
    uint32_t index;
    uint32_t carrier_count;
    uint32_t flags_v1 = 0;
    uint32_t flags_v2;

    if ((!builder_v1_initialized || !builder_v2_initialized) &&
        !reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE)) {
        return;
    }
    if (!cl.frame.valid || cl.frame.number < 0 ||
        cl.frame.numEntities < 0 ||
        static_cast<uint32_t>(cl.frame.numEntities) >
            frame_carriers_v1.size()) {
        return;
    }

    carrier_count = static_cast<uint32_t>(cl.frame.numEntities);
    for (index = 0; index < carrier_count; ++index) {
        const uint32_t parse_index =
            (static_cast<uint32_t>(cl.frame.firstEntity) + index) &
            PARSE_ENTITIES_MASK;
        const entity_state_t &state = cl.entityStates[parse_index];
        frame_carriers_v1[index].entity_index =
            static_cast<uint32_t>(state.number);
        const uint32_t raw_event = static_cast<uint32_t>(state.event);
        frame_carriers_v1[index].raw_event =
            raw_event <= WORR_CGAME_EVENT_SHADOW_MAX_LEGACY_EVENT
                ? raw_event
                : 0u;
        frame_carriers_v1[index].scan_order = index;
        frame_carriers_v2[index].entity_index =
            static_cast<uint32_t>(state.number);
        frame_carriers_v2[index].raw_event = raw_event;
        frame_carriers_v2[index].scan_order = index;
    }
    if (cls.demo.playback)
        flags_v1 |= WORR_CGAME_EVENT_SHADOW_RANGE_DEMO_PLAYBACK;
    flags_v2 = demo_range_flags_v2();

    const uint32_t source_tick = static_cast<uint32_t>(cl.frame.number);
    if ((builder_v1_initialized && builder_v1.has_last_source_tick &&
         source_tick < builder_v1.last_source_tick) ||
        (builder_v2_initialized && builder_v2.has_last_frame_tick &&
         source_tick < builder_v2.last_frame_tick)) {
        if (!reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_FRAME_REWIND))
            return;
    }
    if (builder_v1_initialized) {
        (void)Worr_CGameEventShadowDeliverFrameV1(
            &builder_v1, source_tick, frame_time_us(),
            frame_carriers_v1.data(), carrier_count, flags_v1,
            consume_bridge, nullptr);
    }
    if (builder_v2_initialized) {
        (void)Worr_CGameEventRangeDeliverFrameV2(
            &builder_v2, source_tick, frame_time_us(),
            frame_carriers_v2.data(), carrier_count, flags_v2,
            consume_bridge_v2, nullptr);
    }
}

extern "C" void CL_EventRangeCaptureTempV2(
    const q2proto_svc_temp_entity_t *temp_entity)
{
    worr_cgame_event_action_candidate_v2 candidate;
    uint32_t adapter_status;
    if (!temp_entity)
        return;
    if (!build_temp_candidate(temp_entity, &candidate, &adapter_status)) {
        deliver_rejected_action(
            WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2, adapter_status);
        return;
    }
    deliver_action_candidate(
        &candidate, WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2);
}

extern "C" void CL_EventRangeCaptureMuzzleV2(
    const q2proto_svc_muzzleflash_t *muzzleflash,
    uint32_t family)
{
    worr_cgame_event_action_candidate_v2 candidate;
    uint32_t carrier_kind;
    uint32_t adapter_status;

    if (!muzzleflash)
        return;
    carrier_kind = family == WORR_EVENT_MUZZLE_FAMILY_PLAYER
                       ? WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2
                       : WORR_CGAME_EVENT_CARRIER_MONSTER_MUZZLE_V2;
    if (!build_muzzle_candidate(muzzleflash, family, &candidate,
                                &adapter_status)) {
        deliver_rejected_action(
            carrier_kind, adapter_status);
        return;
    }
    deliver_action_candidate(&candidate, carrier_kind);
}

extern "C" void CL_EventRangeCaptureSoundV2(const q2proto_sound_t *sound)
{
    worr_cgame_event_action_candidate_v2 candidate;
    uint32_t adapter_status;

    if (!sound)
        return;
    if (!build_spatial_audio_candidate(sound, &candidate, &adapter_status)) {
        deliver_rejected_action(
            WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2,
            adapter_status);
        return;
    }
    deliver_action_candidate(
        &candidate, WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2);
}

extern "C" void CL_EventRangeCaptureDamageV2(
    const q2proto_svc_damage_t *damage)
{
    std::array<worr_event_record_v1, Q2PROTO_MAX_DAMAGE_INDICATORS>
        records{};
    std::array<worr_cgame_event_action_candidate_v2,
               Q2PROTO_MAX_DAMAGE_INDICATORS>
        candidates{};
    std::uint32_t candidate_count = 0;
    const std::uint32_t capacity = runtime_entity_capacity();
    const std::uint32_t controlled_entity_index =
        cl.frame.clientNum >= 0
            ? static_cast<std::uint32_t>(cl.frame.clientNum) + 1u
            : WORR_EVENT_NO_ENTITY;
    const bool controlled_index_valid =
        controlled_entity_index != 0 &&
        controlled_entity_index != WORR_EVENT_NO_ENTITY &&
        controlled_entity_index < capacity;

    if (!damage || !controlled_index_valid ||
        Worr_LegacyDamageEventCandidatesBuildV1(
            damage, action_tick(), frame_time_us(), capacity,
            records.data(), static_cast<std::uint32_t>(records.size()),
            &candidate_count) !=
            WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_OK) {
        deliver_rejected_action(
            WORR_CGAME_EVENT_CARRIER_DAMAGE_V2,
            controlled_index_valid
                ? WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2
                : WORR_CGAME_EVENT_ADAPTER_ENTITY_OUT_OF_RANGE_V2);
        return;
    }
    if (!synchronize_controlled_action_lineage(
            controlled_entity_index, capacity)) {
        deliver_rejected_action(
            WORR_CGAME_EVENT_CARRIER_DAMAGE_V2,
            WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2);
        return;
    }

    for (std::uint32_t index = 0; index < candidate_count; ++index) {
        candidates[index].struct_size = sizeof(candidates[index]);
        candidates[index].source_entity_index = 0;
        candidates[index].subject_entity_index =
            controlled_entity_index;
        candidates[index].record = records[index];
        /* Action-range generation ownership is client-observed.  Convert the
         * already validated shared record into the required ID-less/ref-less
         * template without changing payload, tick, ordinal, or expiry. */
        candidates[index].record.source_entity = {
            WORR_EVENT_NO_ENTITY, 0};
        candidates[index].record.subject_entity = {
            WORR_EVENT_NO_ENTITY, 0};
    }
    if ((!builder_v2_initialized &&
         !reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE)) ||
        !builder_v2_initialized ||
        Worr_CGameEventRangeDeliverActionBatchV2(
            &builder_v2, candidates.data(), candidate_count,
            WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, demo_range_flags_v2(),
            consume_bridge_v2, nullptr) !=
        WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2) {
        deliver_rejected_action(
            WORR_CGAME_EVENT_CARRIER_DAMAGE_V2,
            WORR_CGAME_EVENT_ADAPTER_PAYLOAD_INVALID_V2);
    }
}

extern "C" bool CL_EventRangeCapturePOIV2(
    const q2proto_svc_poi_t *poi)
{
    worr_event_record_v1 record{};
    worr_cgame_event_action_candidate_v2 candidate{};
    const std::uint32_t capacity = runtime_entity_capacity();
    const std::uint32_t source_tick = action_tick();
    const std::uint32_t controlled_entity_index =
        cl.frame.clientNum >= 0
            ? static_cast<std::uint32_t>(cl.frame.clientNum) + 1u
            : WORR_EVENT_NO_ENTITY;
    const bool controlled_index_valid =
        controlled_entity_index != 0 &&
        controlled_entity_index != WORR_EVENT_NO_ENTITY &&
        controlled_entity_index < capacity;

    if (!poi || source_tick == UINT32_MAX || !controlled_index_valid ||
        Worr_LegacyKeyedPOIEventCandidateBuildV1(
            poi, source_tick, frame_time_us(), capacity, &record) !=
            WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_OK) {
        deliver_rejected_action(
            WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2,
            controlled_index_valid
                ? WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2
                : WORR_CGAME_EVENT_ADAPTER_ENTITY_OUT_OF_RANGE_V2);
        return false;
    }
    if (!synchronize_controlled_action_lineage(
            controlled_entity_index, capacity)) {
        deliver_rejected_action(
            WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2,
            WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2);
        return false;
    }

    candidate.struct_size = sizeof(candidate);
    candidate.source_entity_index = 0;
    candidate.subject_entity_index = controlled_entity_index;
    candidate.record = record;
    candidate.record.source_entity = {WORR_EVENT_NO_ENTITY, 0};
    candidate.record.subject_entity = {WORR_EVENT_NO_ENTITY, 0};

    if (!builder_v2_initialized &&
        !reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE)) {
        return false;
    }
    if (!builder_v2_initialized)
        return false;

    const auto result = Worr_CGameEventRangeDeliverActionV2(
        &builder_v2, &candidate,
        WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2,
        demo_range_flags_v2(), consume_bridge_v2, nullptr);
    if (result == WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2)
        return true;
    if (result == WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2) {
        deliver_rejected_action(
            WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2,
            WORR_CGAME_EVENT_ADAPTER_PAYLOAD_INVALID_V2);
    }
    return false;
}
