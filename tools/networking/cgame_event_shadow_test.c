/*
Copyright (C) 2026 WORR contributors

Deterministic client-to-cgame immutable legacy event-range tests.
*/

#include "shared/cgame_event_shadow.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "cgame event shadow check failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct callback_state_s {
    worr_cgame_event_shadow_builder_v1 *builder;
    worr_cgame_event_shadow_audit_v1 *audit;
    const worr_event_record_v1 *retained_pointer;
    worr_event_record_v1 copied_record;
    uint32_t calls;
    uint32_t last_count;
    uint32_t last_epoch;
    uint32_t last_batch;
    uint64_t last_sequence;
    bool audit_result;
    bool try_reentrant;
    worr_cgame_event_shadow_build_result_v1 reentrant_result;
} callback_state_t;

#define V2_CAPTURED_RANGES 16u
#define V2_CAPTURED_RECORDS 520u

typedef struct callback_state_v2_s {
    worr_cgame_event_range_builder_v2 *builder;
    worr_cgame_event_range_audit_v2 *audit;
    const worr_cgame_event_action_candidate_v2 *reentrant_candidate;
    const worr_cgame_event_action_candidate_v2 *reentrant_batch_candidates;
    uint32_t reentrant_batch_count;
    worr_cgame_event_range_v2 ranges[V2_CAPTURED_RANGES];
    worr_event_record_v1 records[V2_CAPTURED_RECORDS];
    uint32_t calls;
    uint32_t record_count;
    bool all_audited;
    bool try_reentrant;
    worr_cgame_event_range_build_result_v2 reentrant_action_result;
    worr_cgame_event_range_build_result_v2 reentrant_batch_result;
    worr_cgame_event_range_build_result_v2 reentrant_frame_result;
} callback_state_v2;

static void initialize_candidate_v2(
    worr_cgame_event_action_candidate_v2 *candidate,
    uint32_t tick,
    uint64_t time_us,
    uint32_t source_entity,
    uint32_t subject_entity,
    uint16_t event_type,
    uint16_t payload_kind,
    uint16_t payload_size)
{
    memset(candidate, 0, sizeof(*candidate));
    candidate->struct_size = sizeof(*candidate);
    candidate->source_entity_index = source_entity;
    candidate->subject_entity_index = subject_entity;
    candidate->record.struct_size = sizeof(candidate->record);
    candidate->record.schema_version = WORR_EVENT_ABI_VERSION;
    candidate->record.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate->record.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                              WORR_EVENT_FLAG_PRESENT_ONCE;
    candidate->record.source_tick = tick;
    candidate->record.source_time_us = time_us;
    candidate->record.source_entity.index = WORR_EVENT_NO_ENTITY;
    candidate->record.subject_entity.index = WORR_EVENT_NO_ENTITY;
    candidate->record.event_type = event_type;
    candidate->record.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    candidate->record.prediction_class =
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate->record.expiry_tick = tick + 1u;
    candidate->record.payload_kind = payload_kind;
    candidate->record.payload_size = payload_size;
}

static worr_cgame_event_action_candidate_v2 make_temp_candidate_v2(
    uint32_t tick, uint64_t time_us, uint32_t source_entity)
{
    worr_cgame_event_action_candidate_v2 candidate;
    worr_event_payload_legacy_temp_v1 payload;
    uint16_t fields = 0;

    memset(&payload, 0, sizeof(payload));
    payload.subtype = WORR_EVENT_LEGACY_TEMP_PARASITE_ATTACK;
    (void)Worr_EventLegacyTempFieldMaskV1(
        payload.subtype, (int16_t)source_entity, &fields);
    payload.valid_fields = fields;
    payload.raw_entity1 = (int16_t)source_entity;
    initialize_candidate_v2(
        &candidate, tick, time_us, source_entity, WORR_EVENT_NO_ENTITY,
        WORR_EVENT_TYPE_VISUAL_EFFECT, WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
        sizeof(payload));
    memcpy(candidate.record.payload, &payload, sizeof(payload));
    return candidate;
}

static worr_cgame_event_action_candidate_v2 make_muzzle_candidate_v2(
    uint32_t tick,
    uint64_t time_us,
    uint32_t source_entity,
    uint16_t family,
    uint16_t flash_id)
{
    worr_cgame_event_action_candidate_v2 candidate;
    worr_event_payload_muzzle_v1 payload;
    uint16_t event_type = WORR_EVENT_TYPE_WEAPON_FIRE;

    memset(&payload, 0, sizeof(payload));
    payload.family = family;
    payload.flash_id = flash_id;
    if (family == WORR_EVENT_MUZZLE_FAMILY_PLAYER &&
        flash_id >= WORR_EVENT_PLAYER_MUZZLE_LOGIN &&
        flash_id <= WORR_EVENT_PLAYER_MUZZLE_RESPAWN) {
        event_type = WORR_EVENT_TYPE_STATE_CHANGE;
    } else if (family == WORR_EVENT_MUZZLE_FAMILY_PLAYER &&
               (flash_id == WORR_EVENT_PLAYER_MUZZLE_ITEM_RESPAWN ||
                (flash_id >= WORR_EVENT_PLAYER_MUZZLE_NUKE1 &&
                 flash_id <= WORR_EVENT_PLAYER_MUZZLE_NUKE8))) {
        event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
    }
    initialize_candidate_v2(
        &candidate, tick, time_us, source_entity, WORR_EVENT_NO_ENTITY,
        event_type, WORR_EVENT_PAYLOAD_MUZZLE_V1, sizeof(payload));
    memcpy(candidate.record.payload, &payload, sizeof(payload));
    return candidate;
}

static worr_cgame_event_action_candidate_v2 make_sound_candidate_v2(
    uint32_t tick, uint64_t time_us, uint32_t source_entity)
{
    worr_cgame_event_action_candidate_v2 candidate;
    worr_event_payload_spatial_audio_v1 payload;

    memset(&payload, 0, sizeof(payload));
    payload.asset_id = 1;
    payload.channel = 1;
    payload.flags = WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL;
    payload.raw_entity = source_entity;
    payload.volume = 1.0f;
    payload.attenuation = 1.0f;
    payload.pitch = 1.0f;
    initialize_candidate_v2(
        &candidate, tick, time_us, source_entity, WORR_EVENT_NO_ENTITY,
        WORR_EVENT_TYPE_AUDIO_CUE,
        WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1, sizeof(payload));
    memcpy(candidate.record.payload, &payload, sizeof(payload));
    return candidate;
}

static worr_cgame_event_action_candidate_v2 make_damage_candidate_v2(
    uint32_t tick, uint64_t time_us, uint32_t subject_entity,
    uint32_t source_ordinal, uint32_t amount)
{
    worr_cgame_event_action_candidate_v2 candidate;
    worr_event_payload_damage_v1 payload;

    memset(&payload, 0, sizeof(payload));
    payload.amount = (float)amount;
    payload.direction[source_ordinal % 3u] = 1.0f;
    payload.damage_flags =
        source_ordinal % 2u == 0
            ? WORR_EVENT_DAMAGE_FLAG_HEALTH
            : WORR_EVENT_DAMAGE_FLAG_ARMOR | WORR_EVENT_DAMAGE_FLAG_SHIELD;
    initialize_candidate_v2(
        &candidate, tick, time_us, 0, subject_entity,
        WORR_EVENT_TYPE_DAMAGE, WORR_EVENT_PAYLOAD_DAMAGE,
        sizeof(payload));
    candidate.record.source_ordinal = source_ordinal;
    memcpy(candidate.record.payload, &payload, sizeof(payload));
    return candidate;
}

static worr_cgame_event_action_candidate_v2 make_keyed_poi_candidate_v2(
    uint32_t tick, uint64_t time_us, uint32_t subject_entity,
    uint16_t key, uint16_t lifetime_ms)
{
    worr_cgame_event_action_candidate_v2 candidate;
    worr_event_payload_keyed_poi_v1 payload;

    memset(&payload, 0, sizeof(payload));
    payload.key = key;
    payload.lifetime_ms = lifetime_ms;
    if (lifetime_ms != WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS) {
        payload.position[0] = 11.25f;
        payload.position[1] = -22.5f;
        payload.position[2] = 33.75f;
        payload.image_index = 7;
        payload.color_index = 3;
        payload.flags = WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM;
    }
    initialize_candidate_v2(
        &candidate, tick, time_us, 0, subject_entity,
        WORR_EVENT_TYPE_STATE_CHANGE, WORR_EVENT_PAYLOAD_KEYED_POI_V1,
        sizeof(payload));
    candidate.record.delivery_class =
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    candidate.record.expiry_tick = 0;
    memcpy(candidate.record.payload, &payload, sizeof(payload));
    return candidate;
}

static void consume_v2(void *context,
                       const worr_cgame_event_range_v2 *range)
{
    callback_state_v2 *state = (callback_state_v2 *)context;
    uint32_t range_index = state->calls;
    uint32_t record_offset = state->record_count;

    if (range_index < V2_CAPTURED_RANGES &&
        record_offset + range->count <= V2_CAPTURED_RECORDS) {
        state->ranges[range_index] = *range;
        if (range->count != 0) {
            memcpy(&state->records[record_offset], range->records,
                   sizeof(range->records[0]) * range->count);
            state->ranges[range_index].records =
                &state->records[record_offset];
            state->record_count += range->count;
        }
    }
    ++state->calls;
    if (state->audit &&
        !Worr_CGameEventRangeAuditConsumeV2(state->audit, range)) {
        state->all_audited = false;
    }

    if (state->try_reentrant) {
        state->reentrant_action_result =
            Worr_CGameEventRangeDeliverActionV2(
                state->builder, state->reentrant_candidate,
                WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2, 0,
                consume_v2, state);
        if (state->reentrant_batch_candidates &&
            state->reentrant_batch_count != 0) {
            state->reentrant_batch_result =
                Worr_CGameEventRangeDeliverActionBatchV2(
                    state->builder, state->reentrant_batch_candidates,
                    state->reentrant_batch_count,
                    WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
                    consume_v2, state);
        }
        state->reentrant_frame_result =
            Worr_CGameEventRangeDeliverFrameV2(
                state->builder, 999, 0, NULL, 0, 0,
                consume_v2, state);
    }
}

static void initialize_callback_v2(
    callback_state_v2 *callback,
    worr_cgame_event_range_builder_v2 *builder,
    worr_cgame_event_range_audit_v2 *audit,
    const worr_cgame_event_action_candidate_v2 *reentrant_candidate)
{
    memset(callback, 0, sizeof(*callback));
    callback->builder = builder;
    callback->audit = audit;
    callback->reentrant_candidate = reentrant_candidate;
    callback->all_audited = true;
}

static uint32_t payload_value(const worr_event_record_v1 *record,
                              uint32_t index)
{
    worr_event_payload_u32x4_v1 payload;
    memcpy(&payload, record->payload, sizeof(payload));
    return payload.value[index];
}

static void consume(void *context, const worr_cgame_event_range_v1 *range)
{
    callback_state_t *state = (callback_state_t *)context;
    ++state->calls;
    state->last_count = range->count;
    state->last_epoch = range->stream_epoch;
    state->last_batch = range->batch_generation;
    state->last_sequence = range->carrier_sequence;
    state->retained_pointer = range->records;
    if (range->count != 0)
        state->copied_record = range->records[0];
    state->audit_result =
        Worr_CGameEventShadowAuditConsumeV1(state->audit, range);

    if (state->try_reentrant) {
        state->reentrant_result = Worr_CGameEventShadowDeliverFrameV1(
            state->builder, 999, 0, NULL, 0, 0, consume, state);
    }
}

static int test_lifecycle_ranges_and_audit(void)
{
    worr_cgame_event_shadow_observed_v1 observed[8];
    uint32_t markers[8];
    worr_event_record_v1 scratch[4];
    worr_cgame_event_shadow_builder_v1 builder;
    worr_cgame_event_shadow_audit_v1 audit;
    worr_cgame_event_shadow_audit_status_v1 status;
    callback_state_t callback;
    worr_cgame_event_shadow_carrier_v1 frame1[] = {
        {1, 2, 0},
        {2, 0, 1},
    };
    worr_cgame_event_shadow_carrier_v1 frame2[] = {
        {2, 3, 0},
    };
    worr_cgame_event_shadow_carrier_v1 frame3[] = {
        {1, 2, 0},
        {2, 0, 1},
    };
    worr_cgame_event_shadow_carrier_v1 duplicate_entities[] = {
        {1, 2, 0},
        {1, 3, 1},
    };
    uint64_t normalized;
    uint64_t equivalent;
    worr_event_record_v1 changed;
    worr_cgame_event_range_v1 invalid_range;

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    memset(&callback, 0, sizeof(callback));
    CHECK(Worr_CGameEventShadowBuilderInitV1(
        &builder, observed, markers, 8, scratch, 4, 1));
    Worr_CGameEventShadowAuditResetV1(&audit, 1);
    callback.builder = &builder;
    callback.audit = &audit;
    callback.try_reentrant = true;

    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 10, 10000, frame1, 2, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED);
    CHECK(callback.calls == 1 && callback.last_count == 1 &&
          callback.last_epoch == 1 && callback.last_batch == 1 &&
          callback.last_sequence == 1 && callback.audit_result);
    CHECK(callback.reentrant_result ==
          WORR_CGAME_EVENT_SHADOW_BUILD_REENTRANT);
    CHECK(callback.copied_record.event_id.stream_epoch == 0 &&
          callback.copied_record.event_id.sequence == 0 &&
          (callback.copied_record.flags &
           WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0);
    CHECK(callback.copied_record.source_tick == 10 &&
          callback.copied_record.source_ordinal == 1 &&
          callback.copied_record.source_entity.index == 1 &&
          callback.copied_record.source_entity.generation == 1);
    CHECK(payload_value(&callback.copied_record, 0) == 2 &&
          payload_value(&callback.copied_record, 1) == 1 &&
          payload_value(&callback.copied_record, 2) == 1 &&
          payload_value(&callback.copied_record, 3) == 0);
    CHECK(callback.retained_pointer == scratch);
    CHECK(scratch[0].struct_size == 0); /* callback storage was destroyed */

    normalized = Worr_CGameEventShadowNormalizedRecordHashV1(
        &callback.copied_record);
    CHECK(normalized != 0);
    changed = callback.copied_record;
    changed.source_entity.generation = 77;
    memcpy(&changed.payload[8], &changed.source_entity.generation,
           sizeof(changed.source_entity.generation));
    changed.source_ordinal = 99;
    equivalent = Worr_CGameEventShadowNormalizedRecordHashV1(&changed);
    CHECK(equivalent == normalized);
    changed = callback.copied_record;
    changed.payload[12] = 1;
    CHECK(Worr_CGameEventShadowNormalizedRecordHashV1(&changed) !=
          normalized);

    callback.try_reentrant = false;
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 10, 10000, frame1, 2, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DUPLICATE_FRAME);
    CHECK(callback.calls == 1 && builder.batch_generation == 1 &&
          builder.carrier_sequence == 1);

    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 11, 11000, duplicate_entities, 2, 0,
              consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_INVALID_FRAME);
    CHECK(builder.batch_generation == 1 && observed[1].present &&
          observed[2].present);

    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 11, 11000, frame2, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED);
    CHECK(callback.calls == 2 && callback.last_count == 1 &&
          callback.copied_record.source_ordinal == 2 &&
          callback.copied_record.source_entity.index == 2 &&
          callback.copied_record.source_entity.generation == 1);
    CHECK(!observed[1].present && observed[1].generation == 1);

    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 12, 12000, frame3, 2,
              WORR_CGAME_EVENT_SHADOW_RANGE_DEMO_PLAYBACK,
              consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED);
    CHECK(callback.calls == 3 && callback.last_count == 1 &&
          callback.copied_record.source_ordinal == 3 &&
          callback.copied_record.source_entity.index == 1 &&
          callback.copied_record.source_entity.generation == 2);
    CHECK(payload_value(&callback.copied_record, 3) == 0);

    CHECK(Worr_CGameEventShadowAuditStatusV1(&audit, &status));
    CHECK(status.stream_epoch == 1 && status.accepted_batches == 3 &&
          status.accepted_records == 3 && status.rejected_batches == 0 &&
          status.last_batch_generation == 3 &&
          status.last_carrier_sequence == 3 &&
          status.normalized_chain_hash != 0 && status.last_batch_hash != 0);

    memset(&invalid_range, 0, sizeof(invalid_range));
    invalid_range.struct_size = sizeof(invalid_range);
    invalid_range.api_version = WORR_CGAME_EVENT_SHADOW_API_VERSION;
    invalid_range.stream_epoch = 1;
    invalid_range.batch_generation = 4;
    invalid_range.carrier_sequence = 3; /* duplicate carrier sequence */
    invalid_range.first_carrier_ordinal = 3;
    invalid_range.count = 1;
    invalid_range.phase = WORR_CGAME_EVENT_SHADOW_PHASE_ENTITY_FRAME;
    invalid_range.records = &callback.copied_record;
    CHECK(!Worr_CGameEventShadowAuditConsumeV1(&audit, &invalid_range));
    CHECK(Worr_CGameEventShadowAuditStatusV1(&audit, &status));
    CHECK(status.rejected_batches == 1 && status.accepted_batches == 3);

    CHECK(Worr_CGameEventShadowBuilderResetV1(&builder, 2));
    Worr_CGameEventShadowAuditResetV1(&audit, 2);
    memset(&callback.copied_record, 0, sizeof(callback.copied_record));
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 1, 1000, frame3, 2, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED);
    CHECK(callback.copied_record.source_ordinal == 1 &&
          callback.copied_record.source_entity.generation == 1 &&
          callback.last_batch == 1 && callback.last_sequence == 1);
    CHECK(Worr_CGameEventShadowAuditStatusV1(&audit, &status));
    CHECK(status.stream_epoch == 2 && status.reset_count == 2 &&
          status.accepted_batches == 1 && status.accepted_records == 1);
    return 0;
}

static int test_empty_invalid_and_capacity_frames(void)
{
    worr_cgame_event_shadow_observed_v1 observed[8];
    uint32_t markers[8];
    worr_event_record_v1 scratch[2];
    worr_cgame_event_shadow_builder_v1 builder;
    worr_cgame_event_shadow_audit_v1 audit;
    callback_state_t callback;
    worr_cgame_event_shadow_carrier_v1 visible[] = {
        {1, 0, 0},
    };
    worr_cgame_event_shadow_carrier_v1 event[] = {
        {1, 2, 0},
    };
    worr_cgame_event_shadow_carrier_v1 too_many[] = {
        {1, 2, 0},
        {2, 3, 1},
        {3, 4, 2},
    };
    worr_cgame_event_shadow_carrier_v1 bad_order[] = {
        {1, 2, 1},
    };
    worr_cgame_event_shadow_carrier_v1 world_entity[] = {
        {0, 2, 0},
    };
    worr_cgame_event_shadow_carrier_v1 unknown_event[] = {
        {1, WORR_CGAME_EVENT_SHADOW_MAX_LEGACY_EVENT + 1, 0},
    };
    worr_cgame_event_shadow_builder_v1 builder_before;
    worr_cgame_event_shadow_observed_v1 observed_before[8];
    uint32_t markers_before[8];
    worr_event_record_v1 scratch_before[2];

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    memset(&callback, 0, sizeof(callback));
    CHECK(Worr_CGameEventShadowBuilderInitV1(
        &builder, observed, markers, 8, scratch, 2, 3));
    Worr_CGameEventShadowAuditResetV1(&audit, 3);
    callback.builder = &builder;
    callback.audit = &audit;

    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 1, 0, visible, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED);
    CHECK(callback.last_count == 0 && observed[1].generation == 1 &&
          observed[1].present);
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 2, 0, NULL, 0, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED);
    CHECK(callback.last_count == 0 && !observed[1].present);
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 3, 0, event, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED);
    CHECK(callback.copied_record.source_entity.generation == 2);

    builder_before = builder;
    memcpy(observed_before, observed, sizeof(observed));
    memcpy(markers_before, markers, sizeof(markers));
    memcpy(scratch_before, scratch, sizeof(scratch));
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 3, 0, event, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DUPLICATE_FRAME);
    CHECK(memcmp(&builder, &builder_before, sizeof(builder)) == 0 &&
          memcmp(observed, observed_before, sizeof(observed)) == 0 &&
          memcmp(markers, markers_before, sizeof(markers)) == 0 &&
          memcmp(scratch, scratch_before, sizeof(scratch)) == 0);
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 4, 0, too_many, 3, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_CAPACITY);
    CHECK(memcmp(&builder, &builder_before, sizeof(builder)) == 0 &&
          memcmp(observed, observed_before, sizeof(observed)) == 0 &&
          memcmp(markers, markers_before, sizeof(markers)) == 0 &&
          memcmp(scratch, scratch_before, sizeof(scratch)) == 0);
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 4, 0, bad_order, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_INVALID_FRAME);
    CHECK(memcmp(&builder, &builder_before, sizeof(builder)) == 0 &&
          memcmp(observed, observed_before, sizeof(observed)) == 0 &&
          memcmp(markers, markers_before, sizeof(markers)) == 0 &&
          memcmp(scratch, scratch_before, sizeof(scratch)) == 0);
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 4, 0, world_entity, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_INVALID_FRAME);
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 4, 0, unknown_event, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_INVALID_FRAME);
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 2, 0, event, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_INVALID_FRAME);

    observed[2].generation = UINT32_MAX;
    observed[2].present = 0;
    too_many[0] = (worr_cgame_event_shadow_carrier_v1){2, 2, 0};
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 4, 0, too_many, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_GENERATION_EXHAUSTED);

    observed[2].generation = 0;
    builder.next_carrier_ordinal = UINT32_MAX;
    too_many[0] = (worr_cgame_event_shadow_carrier_v1){2, 2, 0};
    too_many[1] = (worr_cgame_event_shadow_carrier_v1){3, 3, 1};
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 4, 0, too_many, 2, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_ORDINAL_EXHAUSTED);
    return 0;
}

static int test_source_tick_wrap_requires_epoch_reset(void)
{
    worr_cgame_event_shadow_observed_v1 observed[4];
    uint32_t markers[4];
    worr_event_record_v1 scratch[2];
    worr_cgame_event_shadow_builder_v1 builder;
    worr_cgame_event_shadow_audit_v1 audit;
    callback_state_t callback;
    worr_cgame_event_shadow_carrier_v1 event[] = {{1, 2, 0}};

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    memset(&callback, 0, sizeof(callback));
    CHECK(Worr_CGameEventShadowBuilderInitV1(
        &builder, observed, markers, 4, scratch, 2, 10));
    Worr_CGameEventShadowAuditResetV1(&audit, 10);
    callback.builder = &builder;
    callback.audit = &audit;
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, UINT32_MAX, 0, event, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED);
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 0, 0, event, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_INVALID_FRAME);
    CHECK(Worr_CGameEventShadowBuilderResetV1(&builder, 11));
    Worr_CGameEventShadowAuditResetV1(&audit, 11);
    CHECK(Worr_CGameEventShadowDeliverFrameV1(
              &builder, 0, 0, event, 1, 0, consume, &callback) ==
          WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED);
    CHECK(callback.copied_record.source_tick == 0 &&
          callback.copied_record.source_entity.generation == 1);
    return 0;
}

static int test_v2_actions_lifecycle_and_audit(void)
{
    worr_cgame_event_observed_v2 observed[16];
    uint32_t markers[16];
    worr_event_record_v1 scratch[8];
    worr_cgame_event_range_builder_v2 builder;
    worr_cgame_event_range_audit_v2 audit;
    worr_cgame_event_range_audit_status_v2 status;
    callback_state_v2 callback;
    worr_cgame_event_action_candidate_v2 temp;
    worr_cgame_event_action_candidate_v2 player;
    worr_cgame_event_action_candidate_v2 monster;
    worr_cgame_event_action_candidate_v2 sound;
    worr_cgame_event_carrier_v2 adopt[] = {{2, 0, 0}};
    worr_cgame_event_carrier_v2 entity_event[] = {
        {2, 0, 0}, {3, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, 1}};
    worr_cgame_event_carrier_v2 absent[] = {{2, 0, 0}};
    worr_event_payload_legacy_entity_v1 legacy_payload;

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &builder, observed, markers, 16, scratch, 8, 7));
    Worr_CGameEventRangeAuditResetV2(&audit, 7);
    temp = make_temp_candidate_v2(10, 10000, 2);
    player = make_muzzle_candidate_v2(
        12, 12000, 3, WORR_EVENT_MUZZLE_FAMILY_PLAYER,
        WORR_EVENT_PLAYER_MUZZLE_ROCKET);
    monster = make_muzzle_candidate_v2(
        13, 13000, 4, WORR_EVENT_MUZZLE_FAMILY_MONSTER, 293);
    sound = make_sound_candidate_v2(14, 14000, 5);
    initialize_callback_v2(&callback, &builder, &audit, &player);

    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &temp, WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2,
              0, consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(callback.calls == 1 && callback.all_audited);
    CHECK(callback.ranges[0].first_arrival_ordinal == 1 &&
          callback.ranges[0].count == 1 &&
          callback.ranges[0].phase ==
              WORR_CGAME_EVENT_RANGE_PHASE_ACTION_PRE_PRESENT_V2 &&
          callback.ranges[0].adapter_status ==
              WORR_CGAME_EVENT_ADAPTER_OK_V2);
    CHECK((callback.ranges[0].flags &
           (WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2 |
            WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2 |
            WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2 |
            WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |
            WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2 |
            WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2)) ==
          (WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2 |
           WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2 |
           WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2 |
           WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |
           WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2 |
           WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2));
    CHECK(callback.records[0].source_ordinal == 0 &&
          callback.records[0].source_entity.index == 2 &&
          callback.records[0].source_entity.generation == 1 &&
          callback.records[0].prediction_key.command_epoch == 0 &&
          callback.records[0].prediction_key.command_sequence == 0 &&
          callback.records[0].prediction_key.emitter_ordinal == 0 &&
          callback.records[0].prediction_key.lane == 0);
    CHECK(observed[2].provisional && !observed[2].present &&
          scratch[0].struct_size == 0);

    CHECK(Worr_CGameEventRangeDeliverRejectedActionV2(
              &builder, 11, 11000,
              WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2,
              WORR_CGAME_EVENT_ADAPTER_UNSUPPORTED_ID_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_REJECTED_V2);
    CHECK(callback.ranges[1].count == 0 &&
          callback.ranges[1].records == NULL &&
          callback.ranges[1].first_arrival_ordinal == 2 &&
          (callback.ranges[1].flags &
           WORR_CGAME_EVENT_RANGE_ADAPTER_REJECTED_V2) != 0);

    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 12, 12000, adopt, 1, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(callback.ranges[2].count == 0 &&
          callback.ranges[2].first_arrival_ordinal == 3 &&
          observed[2].present && !observed[2].provisional &&
          observed[2].generation == 1);

    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &player,
              WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2,
              WORR_CGAME_EVENT_RANGE_DEMO_PLAYBACK_V2 |
                  WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(callback.ranges[3].first_arrival_ordinal == 4 &&
          (callback.ranges[3].flags &
           (WORR_CGAME_EVENT_RANGE_DEMO_PLAYBACK_V2 |
            WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2)) ==
              (WORR_CGAME_EVENT_RANGE_DEMO_PLAYBACK_V2 |
               WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2));
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &monster,
              WORR_CGAME_EVENT_CARRIER_MONSTER_MUZZLE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &sound,
              WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);

    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 13, 13000, entity_event, 2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(callback.ranges[6].first_arrival_ordinal == 7 &&
          callback.ranges[6].count == 1 &&
          callback.records[4].source_ordinal == 1 &&
          callback.records[4].source_entity.index == 3 &&
          callback.records[4].source_entity.generation == 1 &&
          callback.records[4].payload_kind ==
              WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1);
    memcpy(&legacy_payload, callback.records[4].payload,
           sizeof(legacy_payload));
    CHECK(legacy_payload.raw_event ==
              WORR_EVENT_LEGACY_ENTITY_FOOTSTEP &&
          legacy_payload.flags ==
              WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION);

    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 14, 14000, absent, 1, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(!observed[3].present && observed[3].generation == 1);
    player.record.source_tick = 15;
    player.record.source_time_us = 15000;
    player.record.expiry_tick = 16;
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &player,
              WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(callback.ranges[8].first_arrival_ordinal == 9 &&
          callback.records[5].source_entity.generation == 2 &&
          observed[3].provisional);

    CHECK(builder.next_arrival_ordinal == 10 && callback.all_audited);
    CHECK(Worr_CGameEventRangeAuditStatusV2(&audit, &status));
    CHECK(status.accepted_carriers == 8 && status.rejected_carriers == 1 &&
          status.accepted_ranges == 9 && status.rejected_ranges == 0 &&
          status.accepted_records == 6 &&
          status.last_arrival_ordinal == 9 &&
          status.accepted_carriers_by_kind[
              WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2 - 1] == 1 &&
          status.accepted_carriers_by_kind[
              WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2 - 1] == 2 &&
          status.accepted_carriers_by_kind[
              WORR_CGAME_EVENT_CARRIER_MONSTER_MUZZLE_V2 - 1] == 1 &&
          status.accepted_carriers_by_kind[
              WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2 - 1] == 1 &&
          status.rejected_carriers_by_kind[
              WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2 - 1] == 1 &&
          status.accepted_carriers_by_kind[
              WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2 - 1] == 3);
    return 0;
}

static int test_v2_chunking_and_reentrancy(void)
{
    worr_cgame_event_observed_v2 observed[520];
    uint32_t markers[520];
    worr_event_record_v1 scratch[512];
    worr_cgame_event_carrier_v2 carriers[513];
    worr_cgame_event_range_builder_v2 builder;
    worr_cgame_event_range_audit_v2 audit;
    worr_cgame_event_range_audit_v2 tampered_audit;
    worr_cgame_event_range_audit_status_v2 status;
    worr_cgame_event_action_candidate_v2 reentrant;
    callback_state_v2 callback;
    uint32_t index;

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    for (index = 0; index < 513; ++index) {
        carriers[index].entity_index = index + 1u;
        carriers[index].raw_event = WORR_EVENT_LEGACY_ENTITY_FOOTSTEP;
        carriers[index].scan_order = index;
    }
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &builder, observed, markers, 520, scratch, 512, 20));
    Worr_CGameEventRangeAuditResetV2(&audit, 20);
    reentrant = make_muzzle_candidate_v2(
        2, 2000, 1, WORR_EVENT_MUZZLE_FAMILY_PLAYER,
        WORR_EVENT_PLAYER_MUZZLE_BLASTER);
    initialize_callback_v2(&callback, &builder, &audit, &reentrant);
    callback.try_reentrant = true;

    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 1, 1000, carriers, 513, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(callback.calls == 2 && callback.record_count == 513 &&
          callback.all_audited);
    CHECK(callback.reentrant_action_result ==
              WORR_CGAME_EVENT_RANGE_BUILD_REENTRANT_V2 &&
          callback.reentrant_frame_result ==
              WORR_CGAME_EVENT_RANGE_BUILD_REENTRANT_V2);
    CHECK(callback.ranges[0].count == 512 &&
          callback.ranges[0].chunk_index == 0 &&
          callback.ranges[0].chunk_count == 2 &&
          callback.ranges[0].first_arrival_ordinal == 1 &&
          (callback.ranges[0].flags &
           WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2) != 0 &&
          (callback.ranges[0].flags &
           WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2) == 0);
    CHECK(callback.ranges[1].count == 1 &&
          callback.ranges[1].chunk_index == 1 &&
          callback.ranges[1].chunk_count == 2 &&
          callback.ranges[1].first_arrival_ordinal == 513 &&
          (callback.ranges[1].flags &
           WORR_CGAME_EVENT_RANGE_CONTINUATION_V2) != 0 &&
          (callback.ranges[1].flags &
           WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2) != 0);
    CHECK(callback.ranges[0].carrier_sequence ==
              callback.ranges[1].carrier_sequence &&
          callback.ranges[0].batch_generation ==
              callback.ranges[1].batch_generation &&
          callback.ranges[0].carrier_tick ==
              callback.ranges[1].carrier_tick &&
          callback.records[0].source_ordinal == 0 &&
          callback.records[511].source_ordinal == 511 &&
          callback.records[512].source_ordinal == 512);
    CHECK(builder.next_arrival_ordinal == 514 &&
          scratch[0].struct_size == 0 &&
          scratch[511].struct_size == 0);
    CHECK(Worr_CGameEventRangeAuditStatusV2(&audit, &status));
    CHECK(status.accepted_carriers == 1 && status.accepted_ranges == 2 &&
          status.accepted_records == 513 &&
          status.last_arrival_ordinal == 513);

    memset(&tampered_audit, 0, sizeof(tampered_audit));
    Worr_CGameEventRangeAuditResetV2(&tampered_audit, 20);
    CHECK(Worr_CGameEventRangeAuditConsumeV2(
        &tampered_audit, &callback.ranges[0]));
    callback.records[512].source_ordinal = 511;
    CHECK(!Worr_CGameEventRangeAuditConsumeV2(
        &tampered_audit, &callback.ranges[1]));
    return 0;
}

static int test_v2_failure_atomicity_and_aliases(void)
{
    worr_cgame_event_observed_v2 observed[16];
    uint32_t markers[16];
    worr_event_record_v1 scratch[4];
    worr_cgame_event_range_builder_v2 builder;
    worr_cgame_event_range_builder_v2 builder_before;
    worr_cgame_event_observed_v2 observed_before[16];
    uint32_t markers_before[16];
    worr_event_record_v1 scratch_before[4];
    worr_cgame_event_range_audit_v2 audit;
    callback_state_v2 callback;
    worr_cgame_event_action_candidate_v2 candidate;
    worr_cgame_event_action_candidate_v2 *aliased_candidate;
    worr_cgame_event_carrier_v2 valid_frame[] = {{1, 0, 0}};
    worr_cgame_event_carrier_v2 duplicate_frame[] = {
        {1, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, 0},
        {1, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, 1},
    };
    worr_cgame_event_carrier_v2 unknown_frame[] = {{1, 10, 0}};
    worr_cgame_event_carrier_v2 *aliased_carrier;
    union backing_u {
        max_align_t alignment;
        unsigned char bytes[2048];
    } backing;
    worr_cgame_event_range_builder_v2 alias_builder;
    worr_cgame_event_observed_v2 *alias_observed;
    uint32_t *alias_markers;
    worr_event_record_v1 *alias_scratch;
    unsigned char backing_before[sizeof(backing.bytes)];

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &builder, observed, markers, 16, scratch, 4, 30));
    Worr_CGameEventRangeAuditResetV2(&audit, 30);
    candidate = make_muzzle_candidate_v2(
        1, 1000, 1, WORR_EVENT_MUZZLE_FAMILY_PLAYER,
        WORR_EVENT_PLAYER_MUZZLE_BLASTER);
    initialize_callback_v2(&callback, &builder, &audit, &candidate);

#define SNAPSHOT_V2()                                                        \
    do {                                                                     \
        builder_before = builder;                                            \
        memcpy(observed_before, observed, sizeof(observed));                 \
        memcpy(markers_before, markers, sizeof(markers));                    \
        memcpy(scratch_before, scratch, sizeof(scratch));                    \
    } while (0)
#define CHECK_SNAPSHOT_V2()                                                  \
    do {                                                                     \
        CHECK(memcmp(&builder, &builder_before, sizeof(builder)) == 0);       \
        CHECK(memcmp(observed, observed_before, sizeof(observed)) == 0);      \
        CHECK(memcmp(markers, markers_before, sizeof(markers)) == 0);         \
        CHECK(memcmp(scratch, scratch_before, sizeof(scratch)) == 0);         \
    } while (0)

    SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_SNAPSHOT_V2();

    SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2,
              WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_SNAPSHOT_V2();

    candidate.source_entity_index = 16;
    SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_SNAPSHOT_V2();
    candidate.source_entity_index = 1;
    candidate.record.source_ordinal = 1;
    SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_SNAPSHOT_V2();
    candidate.record.source_ordinal = 0;

    SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 1, 1000, duplicate_frame, 2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_SNAPSHOT_V2();
    CHECK(callback.calls == 0);

    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 1, 1000, valid_frame, 1, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 1, 1000, valid_frame, 1, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DUPLICATE_FRAME_V2);
    CHECK_SNAPSHOT_V2();

    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 2, 2000, unknown_frame, 1, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_REJECTED_V2);
    CHECK(callback.ranges[1].count == 0 &&
          callback.ranges[1].adapter_status ==
              WORR_CGAME_EVENT_ADAPTER_UNSUPPORTED_ID_V2 &&
          callback.ranges[1].first_arrival_ordinal == 2);

    observed[5].generation = UINT32_MAX;
    observed[5].present = 0;
    observed[5].provisional = 0;
    candidate.source_entity_index = 5;
    SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_GENERATION_EXHAUSTED_V2);
    CHECK_SNAPSHOT_V2();
    candidate.source_entity_index = 1;

    builder.next_arrival_ordinal = UINT64_MAX;
    SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeDeliverRejectedActionV2(
              &builder, 3, 3000,
              WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2,
              WORR_CGAME_EVENT_ADAPTER_UNSUPPORTED_ID_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_ORDER_EXHAUSTED_V2);
    CHECK_SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeBuilderResetV2(&builder, 31));
    Worr_CGameEventRangeAuditResetV2(&audit, 31);
    initialize_callback_v2(&callback, &builder, &audit, &candidate);

    aliased_candidate =
        (worr_cgame_event_action_candidate_v2 *)(void *)scratch;
    memcpy(scratch, &candidate, sizeof(candidate));
    SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, aliased_candidate,
              WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_SNAPSHOT_V2();

    aliased_carrier = (worr_cgame_event_carrier_v2 *)(void *)&observed[6];
    {
        const worr_cgame_event_carrier_v2 carrier = {6, 0, 0};
        memcpy(&observed[6], &carrier, sizeof(carrier));
    }
    SNAPSHOT_V2();
    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 1, 1000, aliased_carrier, 1, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_SNAPSHOT_V2();

    memset(&backing, 0x5a, sizeof(backing));
    memcpy(backing_before, backing.bytes, sizeof(backing.bytes));
    alias_observed =
        (worr_cgame_event_observed_v2 *)(void *)&backing.bytes[0];
    alias_markers = (uint32_t *)(void *)&backing.bytes[4];
    alias_scratch =
        (worr_event_record_v1 *)(void *)&backing.bytes[256];
    memset(&alias_builder, 0xa5, sizeof(alias_builder));
    CHECK(!Worr_CGameEventRangeBuilderInitV2(
        &alias_builder, alias_observed, alias_markers, 8,
        alias_scratch, 4, 1));
    CHECK(memcmp(backing.bytes, backing_before,
                 sizeof(backing.bytes)) == 0);

    memset(&backing, 0x5a, sizeof(backing));
    memcpy(backing_before, backing.bytes, sizeof(backing.bytes));
    CHECK(!Worr_CGameEventRangeBuilderInitV2(
        (worr_cgame_event_range_builder_v2 *)(void *)&backing.bytes[0],
        (worr_cgame_event_observed_v2 *)(void *)&backing.bytes[0],
        (uint32_t *)(void *)&backing.bytes[256], 8,
        (worr_event_record_v1 *)(void *)&backing.bytes[512], 4, 1));
    CHECK(memcmp(backing.bytes, backing_before,
                 sizeof(backing.bytes)) == 0);

    CHECK(Worr_CGameEventRangeBuilderResetV2(&builder, 32));
    builder.seen_markers = (uint32_t *)(void *)((unsigned char *)observed + 4);
    builder_before = builder;
    memcpy(observed_before, observed, sizeof(observed));
    CHECK(!Worr_CGameEventRangeBuilderResetV2(&builder, 33));
    CHECK(memcmp(&builder, &builder_before, sizeof(builder)) == 0 &&
          memcmp(observed, observed_before, sizeof(observed)) == 0);

#undef CHECK_SNAPSHOT_V2
#undef SNAPSHOT_V2
    return 0;
}

static int test_v2_damage_action_batches_and_audit(void)
{
    worr_cgame_event_observed_v2 observed[8];
    uint32_t markers[8];
    worr_event_record_v1 scratch[WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2];
    worr_cgame_event_range_builder_v2 builder;
    worr_cgame_event_range_audit_v2 audit;
    worr_cgame_event_range_audit_v2 exact_audit;
    worr_cgame_event_range_audit_status_v2 status;
    callback_state_v2 callback;
    worr_cgame_event_action_candidate_v2
        candidates[WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2];
    worr_cgame_event_range_v2 audit_range;
    worr_event_record_v1
        audit_records[WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2];
    uint32_t batch_count;
    uint32_t index;

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    memset(&exact_audit, 0, sizeof(exact_audit));
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &builder, observed, markers, 8, scratch,
        WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2, 50));
    Worr_CGameEventRangeAuditResetV2(&audit, 50);
    initialize_callback_v2(&callback, &builder, &audit, NULL);

    /* Every legal svc_damage cardinality is one atomic action range. Across
     * consecutive carriers, arrival ordinals advance by the complete batch
     * size while source ordinals restart at zero inside each carrier. */
    for (batch_count = 1;
         batch_count <= WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2;
         ++batch_count) {
        const uint32_t tick = 100u + batch_count;
        const uint64_t time_us = UINT64_C(100000) + batch_count * 1000u;
        const uint32_t record_offset =
            (batch_count - 1u) * batch_count / 2u;
        const uint64_t first_arrival = 1u + record_offset;
        const worr_cgame_event_range_v2 *range;

        for (index = 0; index < batch_count; ++index) {
            candidates[index] = make_damage_candidate_v2(
                tick, time_us, 2, index, batch_count * 10u + index);
        }
        CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
                  &builder, candidates, batch_count,
                  WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
                  consume_v2, &callback) ==
              WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
        CHECK(callback.calls == batch_count && callback.all_audited);

        range = &callback.ranges[batch_count - 1u];
        CHECK(range->stream_epoch == 50 &&
              range->batch_generation == batch_count &&
              range->carrier_sequence == batch_count &&
              range->first_arrival_ordinal == first_arrival &&
              range->carrier_tick == tick &&
              range->carrier_time_us == time_us &&
              range->count == batch_count &&
              range->phase ==
                  WORR_CGAME_EVENT_RANGE_PHASE_ACTION_PRE_PRESENT_V2 &&
              range->carrier_kind == WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 &&
              range->adapter_status == WORR_CGAME_EVENT_ADAPTER_OK_V2 &&
              range->chunk_index == 0 && range->chunk_count == 1);
        CHECK((range->flags &
               (WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2 |
                WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2 |
                WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2 |
                WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |
                WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2 |
                WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2)) ==
              (WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2 |
               WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2 |
               WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2 |
               WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |
               WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2 |
               WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2));

        for (index = 0; index < batch_count; ++index) {
            worr_event_payload_damage_v1 payload;
            const worr_event_record_v1 *record =
                &callback.records[record_offset + index];
            memcpy(&payload, record->payload, sizeof(payload));
            CHECK(record->source_tick == tick &&
                  record->source_time_us == time_us &&
                  record->source_ordinal == index &&
                  record->source_entity.index == 0 &&
                  record->source_entity.generation == 1 &&
                  record->subject_entity.index == 2 &&
                  record->subject_entity.generation == 1 &&
                  record->event_type == WORR_EVENT_TYPE_DAMAGE &&
                  record->payload_kind == WORR_EVENT_PAYLOAD_DAMAGE &&
                  record->payload_size == sizeof(payload) &&
                  record->expiry_tick == tick + 1u &&
                  payload.amount == (float)(batch_count * 10u + index));
        }
        for (index = 0; index < batch_count; ++index)
            CHECK(scratch[index].struct_size == 0);
    }

    CHECK(builder.batch_generation == 4 &&
          builder.carrier_sequence == 4 &&
          builder.next_arrival_ordinal == 11 &&
          observed[0].present && observed[0].generation == 1 &&
          observed[2].provisional && !observed[2].present &&
          observed[2].generation == 1);
    CHECK(Worr_CGameEventRangeAuditStatusV2(&audit, &status));
    CHECK(status.struct_size == sizeof(status) &&
          status.stream_epoch == 50 &&
          status.last_batch_generation == 4 &&
          status.last_carrier_sequence == 4 &&
          status.last_arrival_ordinal == 10 &&
          status.last_carrier_kind == WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 &&
          status.accepted_carriers == 4 &&
          status.accepted_ranges == 4 &&
          status.accepted_records == 10 &&
          status.rejected_carriers == 0 &&
          status.rejected_ranges == 0 &&
          status.accepted_carriers_by_kind[
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 - 1u] == 4);

    /* The public audit independently enforces the same in-carrier ordinal and
     * controlled-subject identity contract, not merely builder output. */
    audit_range = callback.ranges[3];
    memcpy(audit_records, &callback.records[6], sizeof(audit_records));
    audit_range.records = audit_records;
    audit_range.batch_generation = 1;
    audit_range.carrier_sequence = 1;
    audit_range.first_arrival_ordinal = 1;
    Worr_CGameEventRangeAuditResetV2(&exact_audit, 50);
    CHECK(Worr_CGameEventRangeAuditConsumeV2(&exact_audit, &audit_range));
    CHECK(Worr_CGameEventRangeAuditStatusV2(&exact_audit, &status));
    CHECK(status.accepted_carriers == 1 && status.accepted_records == 4 &&
          status.last_arrival_ordinal == 4);

    audit_records[3].source_ordinal = 2;
    Worr_CGameEventRangeAuditResetV2(&exact_audit, 50);
    CHECK(!Worr_CGameEventRangeAuditConsumeV2(&exact_audit, &audit_range));
    CHECK(Worr_CGameEventRangeAuditStatusV2(&exact_audit, &status));
    CHECK(status.accepted_ranges == 0 && status.rejected_ranges == 1);
    audit_records[3].source_ordinal = 3;

    audit_records[3].subject_entity.generation = 2;
    Worr_CGameEventRangeAuditResetV2(&exact_audit, 50);
    CHECK(!Worr_CGameEventRangeAuditConsumeV2(&exact_audit, &audit_range));
    CHECK(Worr_CGameEventRangeAuditStatusV2(&exact_audit, &status));
    CHECK(status.accepted_ranges == 0 && status.rejected_ranges == 1);
    return 0;
}

static int test_v2_damage_preserves_omitted_controlled_lineage(void)
{
    worr_cgame_event_observed_v2 observed[8];
    uint32_t markers[8];
    worr_event_record_v1 scratch[WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2];
    worr_cgame_event_range_builder_v2 builder;
    worr_cgame_event_range_audit_v2 audit;
    callback_state_v2 callback;
    const worr_cgame_event_carrier_v2 controlled_frame[] = {{1, 0, 0}};
    const worr_cgame_event_carrier_v2 omitted_player_frame[] = {{2, 0, 0}};
    worr_cgame_event_action_candidate_v2 damage;

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &builder, observed, markers, 8, scratch,
        WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2, 55));
    Worr_CGameEventRangeAuditResetV2(&audit, 55);
    initialize_callback_v2(&callback, &builder, &audit, NULL);

    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 1, 1000, controlled_frame, 1, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(observed[1].generation == 1 && observed[1].present &&
          !observed[1].provisional);

    CHECK(Worr_CGameEventRangeDeliverFrameV2(
              &builder, 2, 2000, omitted_player_frame, 1, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(observed[1].generation == 1 && !observed[1].present &&
          !observed[1].provisional);
    CHECK(observed[2].generation == 1 && observed[2].present);

    /* The validated canonical snapshot still carries controlled generation 1.
     * Mirror the client integration's targeted synchronization before binding
     * an implicitly addressed damage carrier.  The unrelated packet entity
     * remains under ordinary frame-lifecycle ownership. */
    observed[1].generation = 1;
    observed[1].present = 1;
    observed[1].provisional = 0;
    observed[1].last_seen_batch = builder.batch_generation;

    damage = make_damage_candidate_v2(2, 2000, 1, 0, 20);
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, &damage, 1,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(callback.calls == 3 && callback.record_count == 1 &&
          callback.all_audited);
    CHECK(callback.records[0].subject_entity.index == 1 &&
          callback.records[0].subject_entity.generation == 1);
    CHECK(observed[1].generation == 1 && observed[1].present &&
          !observed[1].provisional);
    CHECK(observed[2].generation == 1 && observed[2].present &&
          !observed[2].provisional);
    return 0;
}

static int test_v2_damage_batch_failure_atomicity(void)
{
    worr_cgame_event_observed_v2 observed[8];
    worr_cgame_event_observed_v2 observed_before[8];
    uint32_t markers[8];
    uint32_t markers_before[8];
    worr_event_record_v1 scratch[5];
    worr_event_record_v1 scratch_before[5];
    worr_cgame_event_range_builder_v2 builder;
    worr_cgame_event_range_builder_v2 builder_before;
    worr_cgame_event_range_audit_v2 audit;
    worr_cgame_event_range_audit_status_v2 status;
    callback_state_v2 callback;
    worr_cgame_event_action_candidate_v2 candidates[5];
    worr_cgame_event_action_candidate_v2 muzzle;
    uint32_t calls_before;
    uint32_t index;

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &builder, observed, markers, 8, scratch, 5, 60));
    Worr_CGameEventRangeAuditResetV2(&audit, 60);
    muzzle = make_muzzle_candidate_v2(
        81, 81000, 1, WORR_EVENT_MUZZLE_FAMILY_PLAYER,
        WORR_EVENT_PLAYER_MUZZLE_BLASTER);
    initialize_callback_v2(&callback, &builder, &audit, &muzzle);
    for (index = 0; index < 5; ++index) {
        candidates[index] = make_damage_candidate_v2(
            80, 80000, 2, index, 20u + index);
    }

#define SNAPSHOT_DAMAGE_BATCH_V2()                                          \
    do {                                                                     \
        builder_before = builder;                                            \
        memcpy(observed_before, observed, sizeof(observed));                 \
        memcpy(markers_before, markers, sizeof(markers));                    \
        memcpy(scratch_before, scratch, sizeof(scratch));                    \
        calls_before = callback.calls;                                       \
    } while (0)
#define CHECK_DAMAGE_BATCH_SNAPSHOT_V2()                                    \
    do {                                                                     \
        CHECK(memcmp(&builder, &builder_before, sizeof(builder)) == 0);       \
        CHECK(memcmp(observed, observed_before, sizeof(observed)) == 0);      \
        CHECK(memcmp(markers, markers_before, sizeof(markers)) == 0);         \
        CHECK(memcmp(scratch, scratch_before, sizeof(scratch)) == 0);         \
        CHECK(callback.calls == calls_before);                               \
    } while (0)

    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 0,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();

    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 5,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();

    /* Scratch insufficiency is rejected before any prefix can be retained. */
    builder.scratch_capacity = 3;
    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 4,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();
    builder.scratch_capacity = 5;

    candidates[3].record.payload_kind = WORR_EVENT_PAYLOAD_EFFECT;
    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 4,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();
    candidates[3] = make_damage_candidate_v2(80, 80000, 2, 3, 23);

    candidates[3].record.source_ordinal = 2;
    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 4,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();
    candidates[3].record.source_ordinal = 3;

    candidates[3].record.source_tick = 81;
    candidates[3].record.expiry_tick = 82;
    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 4,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();
    candidates[3].record.source_tick = 80;
    candidates[3].record.expiry_tick = 81;

    candidates[3].record.source_time_us = 81000;
    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 4,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();
    candidates[3].record.source_time_us = 80000;

    candidates[3].subject_entity_index = 3;
    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 4,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();
    candidates[3].subject_entity_index = 2;

    candidates[3].source_entity_index = 1;
    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 4,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();
    candidates[3].source_entity_index = 0;

    observed[2].generation = UINT32_MAX;
    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 4,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_GENERATION_EXHAUSTED_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();
    observed[2].generation = 0;

    builder.next_arrival_ordinal = UINT64_MAX - 2u;
    SNAPSHOT_DAMAGE_BATCH_V2();
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 4,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_ORDER_EXHAUSTED_V2);
    CHECK_DAMAGE_BATCH_SNAPSHOT_V2();
    builder.next_arrival_ordinal = 1;

    CHECK(Worr_CGameEventRangeAuditStatusV2(&audit, &status));
    CHECK(status.accepted_ranges == 0 && status.rejected_ranges == 0 &&
          callback.calls == 0);

    /* A callback cannot recursively commit either a single action, another
     * damage batch, or an entity-frame carrier. The outer batch remains one
     * complete audited transaction. */
    callback.try_reentrant = true;
    callback.reentrant_batch_candidates = candidates;
    callback.reentrant_batch_count = 4;
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &builder, candidates, 4,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(callback.calls == 1 && callback.record_count == 4 &&
          callback.all_audited &&
          callback.reentrant_action_result ==
              WORR_CGAME_EVENT_RANGE_BUILD_REENTRANT_V2 &&
          callback.reentrant_batch_result ==
              WORR_CGAME_EVENT_RANGE_BUILD_REENTRANT_V2 &&
          callback.reentrant_frame_result ==
              WORR_CGAME_EVENT_RANGE_BUILD_REENTRANT_V2);
    CHECK(builder.batch_generation == 1 && builder.carrier_sequence == 1 &&
          builder.next_arrival_ordinal == 5 &&
          observed[2].generation == 1 && observed[2].provisional);
    CHECK(Worr_CGameEventRangeAuditStatusV2(&audit, &status));
    CHECK(status.accepted_carriers == 1 && status.accepted_ranges == 1 &&
          status.accepted_records == 4 &&
          status.last_arrival_ordinal == 4 &&
          status.accepted_carriers_by_kind[
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 - 1u] == 1);

#undef CHECK_DAMAGE_BATCH_SNAPSHOT_V2
#undef SNAPSHOT_DAMAGE_BATCH_V2
    return 0;
}

static int test_v2_keyed_poi_shape_and_audit(void)
{
    worr_cgame_event_observed_v2 observed[8];
    uint32_t markers[8];
    worr_event_record_v1 scratch[2];
    worr_cgame_event_range_builder_v2 builder;
    worr_cgame_event_range_audit_v2 audit;
    worr_cgame_event_range_audit_status_v2 status;
    callback_state_v2 callback;
    worr_cgame_event_action_candidate_v2 candidate;
    worr_event_payload_keyed_poi_v1 payload;

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    CHECK(sizeof(status) == 224u);
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &builder, observed, markers, 8, scratch, 2, 65));
    Worr_CGameEventRangeAuditResetV2(&audit, 65);
    initialize_callback_v2(&callback, &builder, &audit, NULL);

    candidate = make_keyed_poi_candidate_v2(90, 90000, 2, 41, 2000);
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(callback.calls == 1 && callback.record_count == 1 &&
          callback.all_audited);
    CHECK(callback.ranges[0].carrier_kind ==
              WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2 &&
          callback.ranges[0].count == 1 &&
          callback.records[0].source_entity.index == 0 &&
          callback.records[0].source_entity.generation == 1 &&
          callback.records[0].subject_entity.index == 2 &&
          callback.records[0].subject_entity.generation == 1 &&
          callback.records[0].delivery_class ==
              WORR_EVENT_DELIVERY_RELIABLE_ORDERED &&
          callback.records[0].expiry_tick == 0);
    memcpy(&payload, callback.records[0].payload, sizeof(payload));
    CHECK(payload.key == 41 && payload.lifetime_ms == 2000 &&
          payload.image_index == 7 && payload.color_index == 3 &&
          payload.flags == WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM);

    candidate.source_entity_index = 1;
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    candidate.source_entity_index = 0;
    candidate.subject_entity_index = WORR_EVENT_NO_ENTITY;
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    candidate.subject_entity_index = 2;
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    memcpy(&payload, candidate.record.payload, sizeof(payload));
    payload.key = 0;
    memcpy(candidate.record.payload, &payload, sizeof(payload));
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);

    candidate = make_keyed_poi_candidate_v2(
        91, 91000, 2, 41,
        WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS);
    memcpy(&payload, candidate.record.payload, sizeof(payload));
    payload.image_index = 1;
    memcpy(candidate.record.payload, &payload, sizeof(payload));
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2);
    payload.image_index = 0;
    memcpy(candidate.record.payload, &payload, sizeof(payload));
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(callback.calls == 2 && callback.record_count == 2 &&
          callback.all_audited);
    CHECK(Worr_CGameEventRangeAuditStatusV2(&audit, &status));
    CHECK(status.accepted_carriers == 2 &&
          status.accepted_records == 2 &&
          status.accepted_carriers_by_kind[
              WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2 - 1u] == 2);
    return 0;
}

static int test_v2_audit_exactness_and_muzzle_boundaries(void)
{
    worr_cgame_event_observed_v2 observed[8];
    uint32_t markers[8];
    worr_event_record_v1 scratch[2];
    worr_cgame_event_range_builder_v2 builder;
    worr_cgame_event_range_audit_v2 audit;
    worr_cgame_event_range_audit_status_v2 status;
    callback_state_v2 callback;
    worr_cgame_event_action_candidate_v2 candidate;
    worr_cgame_event_range_v2 range;
    worr_event_record_v1 record;

    CHECK(!Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_PLAYER, 0, 0, 8, 294));
    CHECK(Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_PLAYER, 1, 0, 8, 294));
    CHECK(Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_PLAYER, 7, 20, 8, 294));
    CHECK(!Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_PLAYER, 8, 20, 8, 294));
    CHECK(!Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_PLAYER, 1, 21, 8, 294));
    CHECK(!Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_PLAYER, 1, 29, 8, 294));
    CHECK(Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_PLAYER, 1, 30, 8, 294));
    CHECK(Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_PLAYER, 1, 39, 8, 294));
    CHECK(!Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_PLAYER, 1, 40, 8, 294));
    CHECK(!Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_MONSTER, 1, 0, 8, 294));
    CHECK(Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_MONSTER, 1, 1, 8, 294));
    CHECK(Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_MONSTER, 1, 288, 8, 294));
    CHECK(Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_MONSTER, 1, 289, 8, 294));
    CHECK(Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_MONSTER, 1, 293, 8, 294));
    CHECK(!Worr_EventMuzzleCarrierValidV1(
        WORR_EVENT_MUZZLE_FAMILY_MONSTER, 1, 294, 8, 294));

    memset(&builder, 0, sizeof(builder));
    memset(&audit, 0, sizeof(audit));
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &builder, observed, markers, 8, scratch, 2, 40));
    Worr_CGameEventRangeAuditResetV2(&audit, 40);
    candidate = make_muzzle_candidate_v2(
        1, 1000, 1, WORR_EVENT_MUZZLE_FAMILY_PLAYER,
        WORR_EVENT_PLAYER_MUZZLE_BLASTER);
    initialize_callback_v2(&callback, &builder, NULL, &candidate);
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &builder, &candidate,
              WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2, 0,
              consume_v2, &callback) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    range = callback.ranges[0];
    record = callback.records[0];
    range.records = &record;

    range.flags |= WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2;
    CHECK(!Worr_CGameEventRangeAuditConsumeV2(&audit, &range));
    CHECK(Worr_CGameEventRangeAuditStatusV2(&audit, &status));
    CHECK(status.rejected_ranges == 1 && status.accepted_ranges == 0);

    Worr_CGameEventRangeAuditResetV2(&audit, 40);
    range = callback.ranges[0];
    range.records = &record;
    range.carrier_kind = WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2;
    CHECK(!Worr_CGameEventRangeAuditConsumeV2(&audit, &range));

    Worr_CGameEventRangeAuditResetV2(&audit, 40);
    range = callback.ranges[0];
    range.records = &record;
    range.adapter_status = WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2;
    CHECK(!Worr_CGameEventRangeAuditConsumeV2(&audit, &range));

    Worr_CGameEventRangeAuditResetV2(&audit, 40);
    range = callback.ranges[0];
    range.records = &record;
    range.chunk_count = 2;
    CHECK(!Worr_CGameEventRangeAuditConsumeV2(&audit, &range));

    Worr_CGameEventRangeAuditResetV2(&audit, 40);
    range = callback.ranges[0];
    range.count = 0;
    range.records = NULL;
    CHECK(!Worr_CGameEventRangeAuditConsumeV2(&audit, &range));

    Worr_CGameEventRangeAuditResetV2(&audit, 40);
    range = callback.ranges[0];
    range.records = &record;
    range.flags &=
        ~(uint32_t)WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2;
    CHECK(!Worr_CGameEventRangeAuditConsumeV2(&audit, &range));

    Worr_CGameEventRangeAuditResetV2(&audit, 40);
    range = callback.ranges[0];
    range.records = &record;
    CHECK(Worr_CGameEventRangeAuditConsumeV2(&audit, &range));
    CHECK(Worr_CGameEventRangeAuditStatusV2(&audit, &status));
    CHECK(status.accepted_carriers == 1 && status.accepted_ranges == 1 &&
          status.accepted_records == 1 &&
          status.accepted_carriers_by_kind[
              WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2 - 1] == 1);
    return 0;
}

int main(void)
{
    CHECK(test_lifecycle_ranges_and_audit() == 0);
    CHECK(test_empty_invalid_and_capacity_frames() == 0);
    CHECK(test_source_tick_wrap_requires_epoch_reset() == 0);
    CHECK(test_v2_actions_lifecycle_and_audit() == 0);
    CHECK(test_v2_chunking_and_reentrancy() == 0);
    CHECK(test_v2_failure_atomicity_and_aliases() == 0);
    CHECK(test_v2_damage_action_batches_and_audit() == 0);
    CHECK(test_v2_damage_preserves_omitted_controlled_lineage() == 0);
    CHECK(test_v2_damage_batch_failure_atomicity() == 0);
    CHECK(test_v2_keyed_poi_shape_and_audit() == 0);
    CHECK(test_v2_audit_exactness_and_muzzle_boundaries() == 0);
    printf("cgame event shadow: immutable range checks passed\n");
    return 0;
}
