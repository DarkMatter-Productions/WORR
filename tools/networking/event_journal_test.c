/*
Copyright (C) 2026 WORR contributors

Deterministic canonical event ABI and bounded journal tests.
*/

#include "common/net/event_journal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_MAX_ENTITIES 1024u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "event journal check failed at %s:%d: %s\n",   \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static worr_event_record_v1 make_event(bool authoritative,
                                       uint32_t epoch,
                                       uint32_t sequence,
                                       uint32_t ordinal,
                                       uint8_t delivery_class,
                                       uint8_t prediction_class,
                                       uint32_t entity_generation,
                                       uint32_t marker)
{
    worr_event_record_v1 record;
    worr_event_payload_u32x4_v1 payload = {
        {marker, marker ^ UINT32_C(0x55aa55aa), ordinal, 0},
    };
    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE;
    if (authoritative) {
        record.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
        record.event_id.stream_epoch = epoch;
        record.event_id.sequence = sequence;
    }
    record.source_tick = UINT32_C(1000) + ordinal;
    record.source_ordinal = ordinal;
    record.source_time_us = UINT64_C(5000000) + ordinal * UINT64_C(16667);
    record.source_entity.index = 4;
    record.source_entity.generation = entity_generation;
    record.subject_entity.index = WORR_EVENT_NO_ENTITY;
    record.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
    record.delivery_class = delivery_class;
    record.prediction_class = prediction_class;
    if (prediction_class != WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY) {
        record.prediction_key.command_epoch = 9;
        record.prediction_key.command_sequence = 77;
        record.prediction_key.emitter_ordinal = ordinal;
        record.prediction_key.lane = WORR_EVENT_PREDICTION_LANE_GAMEPLAY;
    }
    if (delivery_class <= WORR_EVENT_DELIVERY_TRANSIENT)
        record.expiry_tick = record.source_tick + 16;
    record.payload_kind = WORR_EVENT_PAYLOAD_U32X4;
    record.payload_size = sizeof(payload);
    memcpy(record.payload, &payload, sizeof(payload));
    return record;
}

static worr_event_record_v1 authorize(worr_event_record_v1 predicted,
                                      uint32_t epoch,
                                      uint32_t sequence)
{
    predicted.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
    predicted.event_id.stream_epoch = epoch;
    predicted.event_id.sequence = sequence;
    return predicted;
}

static worr_event_record_v1 make_payload_event(uint32_t sequence,
                                               uint16_t event_type,
                                               uint16_t payload_kind,
                                               const void *payload,
                                               uint16_t payload_size)
{
    worr_event_record_v1 record =
        make_event(true, 50, sequence, sequence,
                   WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                   WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, sequence);
    record.event_type = event_type;
    record.payload_kind = payload_kind;
    record.payload_size = payload_size;
    memset(record.payload, 0, sizeof(record.payload));
    if (payload_size)
        memcpy(record.payload, payload, payload_size);
    return record;
}

static void set_test_vec3(float value[3], float base)
{
    value[0] = base;
    value[1] = base + 1.0f;
    value[2] = base + 2.0f;
}

static worr_event_record_v1 make_keyed_poi_event(uint32_t epoch,
                                                  uint32_t sequence,
                                                  uint16_t key,
                                                  uint16_t lifetime_ms)
{
    worr_event_payload_keyed_poi_v1 payload;
    worr_event_record_v1 record;

    memset(&payload, 0, sizeof(payload));
    payload.key = key;
    payload.lifetime_ms = lifetime_ms;
    if (lifetime_ms != WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS) {
        payload.position[0] = 1.0f;
        payload.position[1] = 2.0f;
        payload.position[2] = 3.0f;
        payload.image_index = 77;
        payload.color_index = 208;
        payload.flags = WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM;
    }
    record = make_payload_event(sequence, WORR_EVENT_TYPE_STATE_CHANGE,
                                WORR_EVENT_PAYLOAD_KEYED_POI_V1, &payload,
                                sizeof(payload));
    record.event_id.stream_epoch = epoch;
    record.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    record.expiry_tick = 0;
    return record;
}

static void keyed_poi_payload_get(
    const worr_event_record_v1 *record,
    worr_event_payload_keyed_poi_v1 *payload_out)
{
    memcpy(payload_out, record->payload, sizeof(*payload_out));
}

static void keyed_poi_payload_set(
    worr_event_record_v1 *record,
    const worr_event_payload_keyed_poi_v1 *payload)
{
    memcpy(record->payload, payload, sizeof(*payload));
}

static worr_event_payload_legacy_temp_v1 make_temp_payload(
    uint16_t subtype, int16_t raw_entity1)
{
    worr_event_payload_legacy_temp_v1 payload;
    uint16_t fields = 0;
    memset(&payload, 0, sizeof(payload));
    payload.subtype = subtype;
    payload.raw_entity1 = raw_entity1;
    if (!Worr_EventLegacyTempFieldMaskV1(subtype, raw_entity1, &fields))
        return payload;
    payload.valid_fields = fields;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1) == 0)
        payload.raw_entity1 = 0;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2) != 0)
        payload.raw_entity2 = 2;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_TIME) != 0)
        payload.time_ms = 250;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_COUNT) != 0)
        payload.count_or_amount = 12;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_COLOR) != 0)
        payload.color = 7;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1) != 0)
        set_test_vec3(payload.position1, 1.0f);
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2) != 0)
        set_test_vec3(payload.position2, 4.0f);
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_OFFSET) != 0)
        set_test_vec3(payload.offset, 7.0f);
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION) != 0)
        set_test_vec3(payload.direction, 10.0f);
    return payload;
}

static void assign_temp_payload_fields(
    worr_event_payload_legacy_temp_v1 *destination,
    const worr_event_payload_legacy_temp_v1 *source)
{
    unsigned int i;
    destination->subtype = source->subtype;
    destination->valid_fields = source->valid_fields;
    destination->raw_entity1 = source->raw_entity1;
    destination->raw_entity2 = source->raw_entity2;
    destination->time_ms = source->time_ms;
    destination->count_or_amount = source->count_or_amount;
    destination->color = source->color;
    for (i = 0; i < 3; ++i) {
        destination->position1[i] = source->position1[i];
        destination->position2[i] = source->position2[i];
        destination->offset[i] = source->offset[i];
        destination->direction[i] = source->direction[i];
    }
    destination->reserved0 = source->reserved0;
}

static uint16_t temp_event_type(uint16_t subtype)
{
    return subtype == WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT
               ? WORR_EVENT_TYPE_GAMEPLAY_CUE
               : WORR_EVENT_TYPE_VISUAL_EFFECT;
}

static int test_validation_and_hash(void)
{
    worr_event_record_v1 valid =
        make_event(true, 5, 1, 3, WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                   WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 2,
                   UINT32_C(0x12345678));
    worr_event_record_v1 invalid;
    worr_event_record_v1 movement;
    worr_event_payload_vec3_v1 vector_payload = {{1.0f, 2.0f, 3.0f}};
    uint64_t hash_a = 0;
    uint64_t hash_b = 0;
    uint64_t key_hash;
    worr_event_record_v1 key_event;

    CHECK(Worr_EventRecordValidateV1(&valid, TEST_MAX_ENTITIES));
    CHECK(Worr_EventRecordHashV1(&valid, TEST_MAX_ENTITIES, &hash_a));
    CHECK(Worr_EventRecordHashV1(&valid, TEST_MAX_ENTITIES, &hash_b));
    CHECK(hash_a == hash_b);
    CHECK(hash_a == UINT64_C(17999893636270564749));

    invalid = valid;
    invalid.struct_size--;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.schema_version++;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.model_revision++;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.flags |= UINT32_C(0x80000000);
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.reserved0 = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.event_id.stream_epoch = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.event_id.sequence = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.event_type = UINT16_MAX;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.delivery_class = UINT8_MAX;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.prediction_class = UINT8_MAX;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.payload_kind = UINT16_MAX;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.event_type = WORR_EVENT_TYPE_DAMAGE;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.source_entity.generation = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.source_entity.index = TEST_MAX_ENTITIES;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.subject_entity.generation = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.payload_size--;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.payload[invalid.payload_size] = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = valid;
    invalid.expiry_tick = valid.source_tick + 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    invalid = make_event(true, 5, 2, 4, WORR_EVENT_DELIVERY_COSMETIC,
                         WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 2, 9);
    invalid.expiry_tick = invalid.source_tick;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = make_event(true, 5, 2, 4, WORR_EVENT_DELIVERY_COSMETIC,
                         WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 2, 9);
    invalid.flags |= WORR_EVENT_FLAG_CRITICAL;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = make_event(false, 0, 0, 4, WORR_EVENT_DELIVERY_COSMETIC,
                         WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 2, 9);
    invalid.prediction_key.command_epoch = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = make_event(false, 0, 0, 4, WORR_EVENT_DELIVERY_COSMETIC,
                         WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 2, 9);
    invalid.prediction_key.command_sequence = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = make_event(false, 0, 0, 4, WORR_EVENT_DELIVERY_COSMETIC,
                         WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 2, 9);
    invalid.prediction_key.emitter_ordinal++;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = make_event(false, 0, 0, 4, WORR_EVENT_DELIVERY_COSMETIC,
                         WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 2, 9);
    invalid.event_id.stream_epoch = 5;
    invalid.event_id.sequence = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = make_event(false, 0, 0, 4, WORR_EVENT_DELIVERY_COSMETIC,
                         WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 2, 9);
    invalid.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    movement = valid;
    movement.event_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
    movement.payload_kind = WORR_EVENT_PAYLOAD_VEC3;
    movement.payload_size = sizeof(vector_payload);
    memset(movement.payload, 0, sizeof(movement.payload));
    vector_payload.value[1] = NAN;
    memcpy(movement.payload, &vector_payload, sizeof(vector_payload));
    CHECK(!Worr_EventRecordValidateV1(&movement, TEST_MAX_ENTITIES));

    invalid = valid;
    invalid.event_id.sequence++;
    CHECK(Worr_EventRecordHashV1(&invalid, TEST_MAX_ENTITIES, &hash_b));
    CHECK(hash_a != hash_b);
    key_event = make_event(false, 0, 0, 7,
                           WORR_EVENT_DELIVERY_COSMETIC,
                           WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 1);
    key_hash = Worr_EventPredictionKeyHashV1(&key_event.prediction_key);
    CHECK(key_hash == UINT64_C(11232163421548043143));
    return 0;
}

static int test_legacy_entity_catalog_and_semantic_hash(void)
{
    static const struct {
        uint16_t raw_event;
        uint16_t flags;
        uint16_t event_type;
    } cases[] = {
        {WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN,
         WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION,
         WORR_EVENT_TYPE_VISUAL_EFFECT},
        {WORR_EVENT_LEGACY_ENTITY_FOOTSTEP,
         WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION,
         WORR_EVENT_TYPE_MOVEMENT_CUE},
        {WORR_EVENT_LEGACY_ENTITY_FALL_SHORT,
         WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION,
         WORR_EVENT_TYPE_MOVEMENT_CUE},
        {WORR_EVENT_LEGACY_ENTITY_FALL_MEDIUM,
         WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION,
         WORR_EVENT_TYPE_MOVEMENT_CUE},
        {WORR_EVENT_LEGACY_ENTITY_FALL_FAR,
         WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION,
         WORR_EVENT_TYPE_MOVEMENT_CUE},
        {WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT,
         WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION |
             WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY,
         WORR_EVENT_TYPE_STATE_CHANGE},
        {WORR_EVENT_LEGACY_ENTITY_OTHER_TELEPORT,
         WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY,
         WORR_EVENT_TYPE_STATE_CHANGE},
        {WORR_EVENT_LEGACY_ENTITY_OTHER_FOOTSTEP,
         WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION,
         WORR_EVENT_TYPE_MOVEMENT_CUE},
        {WORR_EVENT_LEGACY_ENTITY_LADDER_STEP,
         WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION,
         WORR_EVENT_TYPE_MOVEMENT_CUE},
    };
    size_t i;
    worr_event_payload_legacy_entity_v1 payload;
    worr_event_record_v1 record;
    worr_event_record_v1 invalid;
    worr_event_record_v1 candidate;
    worr_event_record_v1 other_authority;
    uint64_t full_hash = 0;
    uint64_t other_full_hash = 0;
    uint64_t semantic_hash = 0;
    uint64_t candidate_hash = 0;

    CHECK(WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1 == 7);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        memset(&payload, 0, sizeof(payload));
        payload.raw_event = cases[i].raw_event;
        payload.flags = cases[i].flags;
        record = make_payload_event((uint32_t)i + 1u, cases[i].event_type,
                                    WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1,
                                    &payload, sizeof(payload));
        CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
    }

    memset(&payload, 0, sizeof(payload));
    payload.raw_event = WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT;
    payload.flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION |
                    WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY;
    record = make_payload_event(20, WORR_EVENT_TYPE_STATE_CHANGE,
                                WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1,
                                &payload, sizeof(payload));
    CHECK(Worr_EventRecordHashV1(&record, TEST_MAX_ENTITIES, &full_hash));
    CHECK(Worr_EventRecordSemanticHashV1(&record, TEST_MAX_ENTITIES,
                                         &semantic_hash));
    CHECK(semantic_hash == UINT64_C(459024801986651691));

    candidate = record;
    candidate.flags &= ~(uint32_t)WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
    memset(&candidate.event_id, 0, sizeof(candidate.event_id));
    CHECK(!Worr_EventRecordValidateV1(&candidate, TEST_MAX_ENTITIES));
    CHECK(Worr_EventRecordCandidateValidateV1(
        &candidate, TEST_MAX_ENTITIES));
    invalid = candidate;
    invalid.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
    CHECK(!Worr_EventRecordCandidateValidateV1(
        &invalid, TEST_MAX_ENTITIES));
    invalid = candidate;
    invalid.event_id.sequence = 1;
    CHECK(!Worr_EventRecordCandidateValidateV1(
        &invalid, TEST_MAX_ENTITIES));
    CHECK(Worr_EventRecordSemanticHashV1(&candidate, TEST_MAX_ENTITIES,
                                         &candidate_hash));
    CHECK(candidate_hash == semantic_hash);
    CHECK(Worr_EventRecordSemanticallyEqualV1(&record, &candidate,
                                               TEST_MAX_ENTITIES));

    other_authority = record;
    other_authority.event_id.stream_epoch = 99;
    other_authority.event_id.sequence = 1234;
    CHECK(Worr_EventRecordHashV1(&other_authority, TEST_MAX_ENTITIES,
                                 &other_full_hash));
    CHECK(full_hash != other_full_hash);
    CHECK(Worr_EventRecordSemanticHashV1(&other_authority,
                                         TEST_MAX_ENTITIES,
                                         &candidate_hash));
    CHECK(candidate_hash == semantic_hash);
    CHECK(Worr_EventRecordSemanticallyEqualV1(&record, &other_authority,
                                               TEST_MAX_ENTITIES));

    invalid = candidate;
    invalid.flags |= WORR_EVENT_FLAG_CRITICAL;
    CHECK(Worr_EventRecordSemanticHashV1(&invalid, TEST_MAX_ENTITIES,
                                         &candidate_hash));
    CHECK(candidate_hash != semantic_hash);
    CHECK(!Worr_EventRecordSemanticallyEqualV1(&record, &invalid,
                                                TEST_MAX_ENTITIES));
    invalid = candidate;
    invalid.event_id.sequence = 1;
    CHECK(!Worr_EventRecordSemanticHashV1(&invalid, TEST_MAX_ENTITIES,
                                          &candidate_hash));
    invalid = record;
    invalid.event_id.sequence = 0;
    CHECK(!Worr_EventRecordSemanticHashV1(&invalid, TEST_MAX_ENTITIES,
                                          &candidate_hash));
    CHECK(!Worr_EventRecordSemanticHashV1(&record, TEST_MAX_ENTITIES, NULL));
    CHECK(!Worr_EventRecordSemanticallyEqualV1(NULL, &record,
                                                TEST_MAX_ENTITIES));

    invalid = record;
    ((worr_event_payload_legacy_entity_v1 *)invalid.payload)->raw_event = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_entity_v1 *)invalid.payload)->raw_event = 10;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_entity_v1 *)invalid.payload)->flags = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_entity_v1 *)invalid.payload)->flags |=
        UINT16_C(0x8000);
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_entity_v1 *)invalid.payload)->reserved0 = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    invalid.event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    invalid.payload[sizeof(payload)] = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    return 0;
}

static int test_legacy_temp_catalog(void)
{
    static const uint16_t entity1_ref_subtypes[] = {
        WORR_EVENT_LEGACY_TEMP_PARASITE_ATTACK,
        WORR_EVENT_LEGACY_TEMP_MEDIC_CABLE_ATTACK,
        WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE,
        WORR_EVENT_LEGACY_TEMP_LIGHTNING,
        WORR_EVENT_LEGACY_TEMP_FLASHLIGHT,
        WORR_EVENT_LEGACY_TEMP_HEATBEAM,
        WORR_EVENT_LEGACY_TEMP_MONSTER_HEATBEAM,
        WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE2,
        WORR_EVENT_LEGACY_TEMP_POWER_SPLASH,
        WORR_EVENT_LEGACY_TEMP_LIGHTNING_BEAM,
    };
    static const struct {
        uint16_t subtype;
        int16_t raw_entity1;
        uint16_t expected_fields;
    } mask_cases[] = {
        {WORR_EVENT_LEGACY_TEMP_GUNSHOT, 0,
         WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION},
        {WORR_EVENT_LEGACY_TEMP_SPLASH, 0,
         WORR_EVENT_LEGACY_TEMP_FIELD_COUNT |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION |
             WORR_EVENT_LEGACY_TEMP_FIELD_COLOR},
        {WORR_EVENT_LEGACY_TEMP_RAILTRAIL, 0,
         WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2},
        {WORR_EVENT_LEGACY_TEMP_EXPLOSION1, 0,
         WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1},
        {WORR_EVENT_LEGACY_TEMP_PARASITE_ATTACK, 1,
         WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2},
        {WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE, 1,
         WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2 |
             WORR_EVENT_LEGACY_TEMP_FIELD_OFFSET},
        {WORR_EVENT_LEGACY_TEMP_LIGHTNING, 1,
         WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2 |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2},
        {WORR_EVENT_LEGACY_TEMP_FLASHLIGHT, 1,
         WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1},
        {WORR_EVENT_LEGACY_TEMP_FORCEWALL, 0,
         WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2 |
             WORR_EVENT_LEGACY_TEMP_FIELD_COLOR},
        {WORR_EVENT_LEGACY_TEMP_STEAM, -1,
         WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2 |
             WORR_EVENT_LEGACY_TEMP_FIELD_COUNT |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION |
             WORR_EVENT_LEGACY_TEMP_FIELD_COLOR},
        {WORR_EVENT_LEGACY_TEMP_STEAM, 20001,
         WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2 |
             WORR_EVENT_LEGACY_TEMP_FIELD_COUNT |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION |
             WORR_EVENT_LEGACY_TEMP_FIELD_COLOR |
             WORR_EVENT_LEGACY_TEMP_FIELD_TIME},
        {WORR_EVENT_LEGACY_TEMP_WIDOWBEAMOUT, 20001,
         WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1},
        {WORR_EVENT_LEGACY_TEMP_POWER_SPLASH, 1,
         WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
             WORR_EVENT_LEGACY_TEMP_FIELD_COUNT},
        {WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT, 0,
         WORR_EVENT_LEGACY_TEMP_FIELD_COUNT},
    };
    size_t i;
    uint16_t subtype;
    uint16_t fields;
    worr_event_payload_legacy_temp_v1 payload;
    worr_event_payload_legacy_temp_v1 padding_a;
    worr_event_payload_legacy_temp_v1 padding_b;
    worr_event_record_v1 record;
    worr_event_record_v1 invalid;
    worr_event_record_v1 padding_record_a;
    worr_event_record_v1 padding_record_b;
    uint64_t padding_hash_a;
    uint64_t padding_hash_b;

    CHECK(WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1 == 8);
    CHECK(WORR_EVENT_LEGACY_TEMP_BOSSTPORT == 22);
    CHECK(WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_BROKEN == 27);
    CHECK(WORR_EVENT_LEGACY_TEMP_FLAME_RESERVED == 32);
    CHECK(WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_FIXED == 56);
    CHECK(WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT == 128);
    for (i = 0; i < sizeof(mask_cases) / sizeof(mask_cases[0]); ++i) {
        fields = 0;
        CHECK(Worr_EventLegacyTempFieldMaskV1(mask_cases[i].subtype,
                                               mask_cases[i].raw_entity1,
                                               &fields));
        CHECK(fields == mask_cases[i].expected_fields);
    }
    CHECK(!Worr_EventLegacyTempFieldMaskV1(
        WORR_EVENT_LEGACY_TEMP_GUNSHOT, 0, NULL));

    for (subtype = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
         subtype <= WORR_EVENT_LEGACY_TEMP_EXPLOSION2_NL; ++subtype) {
        int16_t raw_entity1;
        if (subtype == WORR_EVENT_LEGACY_TEMP_FLAME_RESERVED)
            continue;
        raw_entity1 = subtype == WORR_EVENT_LEGACY_TEMP_STEAM ? -1 : 1;
        payload = make_temp_payload(subtype, raw_entity1);
        record = make_payload_event((uint32_t)subtype + 1u,
                                    temp_event_type(subtype),
                                    WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                    &payload, sizeof(payload));
        CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
    }
    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_STEAM, 1);
    record = make_payload_event(100, WORR_EVENT_TYPE_VISUAL_EFFECT,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT, 0);
    record = make_payload_event(101, WORR_EVENT_TYPE_GAMEPLAY_CUE,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));

    CHECK(!Worr_EventLegacyTempFieldMaskV1(
        WORR_EVENT_LEGACY_TEMP_FLAME_RESERVED, 0, &fields));
    CHECK(!Worr_EventLegacyTempFieldMaskV1(64, 0, &fields));
    CHECK(!Worr_EventLegacyTempFieldMaskV1(127, 0, &fields));
    CHECK(!Worr_EventLegacyTempFieldMaskV1(129, 0, &fields));
    CHECK(!Worr_EventLegacyTempFieldMaskV1(
        WORR_EVENT_LEGACY_TEMP_STEAM, -2, &fields));
    CHECK(!Worr_EventLegacyTempFieldMaskV1(
        WORR_EVENT_LEGACY_TEMP_STEAM, 0, &fields));

    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_GUNSHOT, 0);
    record = make_payload_event(102, WORR_EVENT_TYPE_VISUAL_EFFECT,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->valid_fields ^=
        WORR_EVENT_LEGACY_TEMP_FIELD_COUNT;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->valid_fields |=
        UINT16_C(0x8000);
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->position1[0] = NAN;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->direction[0] =
        INFINITY;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->position2[0] = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->offset[0] = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity2 = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->time_ms = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->count_or_amount = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->color = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->reserved0 = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    invalid.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    invalid.payload[sizeof(payload)] = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_RAILTRAIL, 0);
    record = make_payload_event(103, WORR_EVENT_TYPE_VISUAL_EFFECT,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->position2[1] = NAN;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE, 1);
    record = make_payload_event(104, WORR_EVENT_TYPE_VISUAL_EFFECT,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->offset[2] = NAN;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 = -1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 =
        (int16_t)TEST_MAX_ENTITIES;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 =
        (int16_t)(TEST_MAX_ENTITIES - 1);
    CHECK(Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    /* Every subtype whose historical entity1 slot is a genuine entity
     * reference uses entity namespace bounds. */
    for (i = 0;
         i < sizeof(entity1_ref_subtypes) / sizeof(entity1_ref_subtypes[0]);
         ++i) {
        payload = make_temp_payload(entity1_ref_subtypes[i], 1);
        record = make_payload_event(
            (uint32_t)(110u + i),
            temp_event_type(entity1_ref_subtypes[i]),
            WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
            &payload, sizeof(payload));
        CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
        invalid = record;
        ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 =
            -1;
        CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
        invalid = record;
        ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 =
            (int16_t)TEST_MAX_ENTITIES;
        CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
        invalid = record;
        ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 =
            (int16_t)(TEST_MAX_ENTITIES - 1);
        CHECK(Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    }

    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_LIGHTNING, 1);
    record = make_payload_event(105, WORR_EVENT_TYPE_VISUAL_EFFECT,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity2 = -1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity2 =
        (int16_t)TEST_MAX_ENTITIES;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity2 =
        (int16_t)(TEST_MAX_ENTITIES - 1);
    CHECK(Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_SPLASH, 0);
    record = make_payload_event(106, WORR_EVENT_TYPE_VISUAL_EFFECT,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->count_or_amount =
        -1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->count_or_amount =
        256;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->color = -1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->color = 256;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    /* Live rerelease widow producers use signed-short sustain IDs 20001 and
     * 20002.  They are not entity references and therefore remain valid even
     * though TEST_MAX_ENTITIES is only 1024. */
    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_WIDOWBEAMOUT, 20001);
    record = make_payload_event(121, WORR_EVENT_TYPE_VISUAL_EFFECT,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 =
        20002;
    CHECK(Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 =
        INT16_MAX;
    CHECK(Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 = -1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 =
        INT16_MIN;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    /* Steam uses -1 for the immediate shape and a positive sustain ID for the
     * timed shape.  Its second signed-short slot is particle magnitude, not
     * an entity, so the complete int16 domain is representation-valid. */
    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_STEAM, -1);
    payload.raw_entity2 = 75;
    record = make_payload_event(122, WORR_EVENT_TYPE_VISUAL_EFFECT,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity2 =
        INT16_MIN;
    CHECK(Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity2 =
        INT16_MAX;
    CHECK(Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->valid_fields |=
        WORR_EVENT_LEGACY_TEMP_FIELD_TIME;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->time_ms = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_STEAM, 20001);
    payload.raw_entity2 = 75;
    record = make_payload_event(107, WORR_EVENT_TYPE_VISUAL_EFFECT,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 =
        INT16_MAX;
    CHECK(Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity2 =
        INT16_MIN;
    CHECK(Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity2 =
        INT16_MAX;
    CHECK(Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->time_ms = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->time_ms = -1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->raw_entity1 = -2;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT, 0);
    record = make_payload_event(108, WORR_EVENT_TYPE_GAMEPLAY_CUE,
                                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                &payload, sizeof(payload));
    invalid = record;
    ((worr_event_payload_legacy_temp_v1 *)invalid.payload)->count_or_amount =
        INT16_MAX + 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    /* Assign every semantic field over hostile object-representation fills.
     * If a supported compiler introduces internal payload padding, the bytes
     * retain different fill values while validation, hashing, and equality
     * remain field-defined and padding-independent. */
    payload = make_temp_payload(WORR_EVENT_LEGACY_TEMP_STEAM, 1);
    memset(&padding_a, 0xa5, sizeof(padding_a));
    memset(&padding_b, 0x5a, sizeof(padding_b));
    assign_temp_payload_fields(&padding_a, &payload);
    assign_temp_payload_fields(&padding_b, &payload);
    padding_record_a = make_payload_event(
        109, WORR_EVENT_TYPE_VISUAL_EFFECT,
        WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1, &padding_a, sizeof(padding_a));
    padding_record_b = make_payload_event(
        109, WORR_EVENT_TYPE_VISUAL_EFFECT,
        WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1, &padding_b, sizeof(padding_b));
    padding_record_b.event_id.sequence = 110;
    CHECK(Worr_EventRecordValidateV1(&padding_record_a, TEST_MAX_ENTITIES));
    CHECK(Worr_EventRecordValidateV1(&padding_record_b, TEST_MAX_ENTITIES));
    CHECK(Worr_EventRecordSemanticHashV1(
        &padding_record_a, TEST_MAX_ENTITIES, &padding_hash_a));
    CHECK(Worr_EventRecordSemanticHashV1(
        &padding_record_b, TEST_MAX_ENTITIES, &padding_hash_b));
    CHECK(padding_hash_a == padding_hash_b);
    CHECK(Worr_EventRecordSemanticallyEqualV1(
        &padding_record_a, &padding_record_b, TEST_MAX_ENTITIES));
    return 0;
}

static uint16_t player_muzzle_event_type(uint16_t flash_id)
{
    if (flash_id >= WORR_EVENT_PLAYER_MUZZLE_LOGIN &&
        flash_id <= WORR_EVENT_PLAYER_MUZZLE_RESPAWN) {
        return WORR_EVENT_TYPE_STATE_CHANGE;
    }
    if (flash_id == WORR_EVENT_PLAYER_MUZZLE_ITEM_RESPAWN ||
        (flash_id >= WORR_EVENT_PLAYER_MUZZLE_NUKE1 &&
         flash_id <= WORR_EVENT_PLAYER_MUZZLE_NUKE8)) {
        return WORR_EVENT_TYPE_VISUAL_EFFECT;
    }
    return WORR_EVENT_TYPE_WEAPON_FIRE;
}

static int test_muzzle_and_spatial_audio_catalog(void)
{
    uint16_t id;
    worr_event_payload_muzzle_v1 muzzle;
    worr_event_payload_spatial_audio_v1 audio;
    worr_event_record_v1 record;
    worr_event_record_v1 invalid;

    CHECK(WORR_EVENT_PAYLOAD_MUZZLE_V1 == 9);
    CHECK(WORR_EVENT_PLAYER_MUZZLE_RESERVED_FIRST == 21);
    CHECK(WORR_EVENT_PLAYER_MUZZLE_RESERVED_LAST == 29);
    CHECK(WORR_EVENT_MONSTER_MUZZLE_LAST == 293);
    for (id = 0; id <= WORR_EVENT_PLAYER_MUZZLE_NUKE8; ++id) {
        const bool assigned =
            id <= WORR_EVENT_PLAYER_MUZZLE_PHALANX2 ||
            id >= WORR_EVENT_PLAYER_MUZZLE_ETF_RIFLE;
        memset(&muzzle, 0, sizeof(muzzle));
        muzzle.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
        muzzle.flash_id = id;
        record = make_payload_event((uint32_t)id + 200u,
                                    player_muzzle_event_type(id),
                                    WORR_EVENT_PAYLOAD_MUZZLE_V1,
                                    &muzzle, sizeof(muzzle));
        CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES) ==
              assigned);
    }

    memset(&muzzle, 0, sizeof(muzzle));
    muzzle.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
    muzzle.flash_id = WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN;
    muzzle.flags = WORR_EVENT_MUZZLE_FLAG_SILENCED;
    record = make_payload_event(250, WORR_EVENT_TYPE_WEAPON_FIRE,
                                WORR_EVENT_PAYLOAD_MUZZLE_V1,
                                &muzzle, sizeof(muzzle));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_muzzle_v1 *)invalid.payload)->flags |= 2;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_muzzle_v1 *)invalid.payload)->family = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    invalid.event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    invalid.payload[sizeof(muzzle)] = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    memset(&muzzle, 0, sizeof(muzzle));
    muzzle.family = WORR_EVENT_MUZZLE_FAMILY_MONSTER;
    muzzle.flash_id = WORR_EVENT_MONSTER_MUZZLE_FIRST;
    record = make_payload_event(251, WORR_EVENT_TYPE_WEAPON_FIRE,
                                WORR_EVENT_PAYLOAD_MUZZLE_V1,
                                &muzzle, sizeof(muzzle));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
    muzzle.flash_id = WORR_EVENT_MONSTER_MUZZLE_LAST;
    record = make_payload_event(252, WORR_EVENT_TYPE_WEAPON_FIRE,
                                WORR_EVENT_PAYLOAD_MUZZLE_V1,
                                &muzzle, sizeof(muzzle));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_muzzle_v1 *)invalid.payload)->flash_id = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_muzzle_v1 *)invalid.payload)->flash_id =
        WORR_EVENT_MONSTER_MUZZLE_LAST + 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_muzzle_v1 *)invalid.payload)->flags =
        WORR_EVENT_MUZZLE_FLAG_SILENCED;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    CHECK(WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1 == 10);
    memset(&audio, 0, sizeof(audio));
    audio.asset_id = 7;
    audio.channel = 3;
    audio.flags = WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL |
                  WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION |
                  WORR_EVENT_SPATIAL_AUDIO_RELIABLE |
                  WORR_EVENT_SPATIAL_AUDIO_POSITION_FORCED;
    audio.raw_entity = 4;
    set_test_vec3(audio.origin, 20.0f);
    audio.volume = 1.0f;
    audio.attenuation = 1.0f;
    audio.time_offset = 0.1f;
    audio.pitch = 1.0f;
    record = make_payload_event(260, WORR_EVENT_TYPE_AUDIO_CUE,
                                WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1,
                                &audio, sizeof(audio));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));

    memset(&audio, 0, sizeof(audio));
    audio.asset_id = 8;
    audio.raw_entity = WORR_EVENT_NO_ENTITY;
    audio.volume = 0.0f;
    audio.attenuation = 4.0f;
    audio.time_offset = 0.255f;
    audio.pitch = 0.5f;
    record = make_payload_event(261, WORR_EVENT_TYPE_AUDIO_CUE,
                                WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1,
                                &audio, sizeof(audio));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));

    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->asset_id = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->flags =
        UINT16_C(0x8000);
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->flags =
        WORR_EVENT_SPATIAL_AUDIO_POSITION_FORCED;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->channel = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->raw_entity = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->origin[0] = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->volume = -1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->volume = 4.01f;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->attenuation = -1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->attenuation =
        4.01f;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->time_offset = -1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->time_offset =
        0.256f;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->pitch = 0;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->pitch = NAN;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->pitch =
        WORR_EVENT_SPATIAL_AUDIO_MAX_PITCH + 0.01f;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    invalid.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    invalid.payload[sizeof(audio)] = 1;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));

    memset(&audio, 0, sizeof(audio));
    audio.asset_id = 9;
    audio.channel = 7;
    audio.flags = WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL |
                  WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION;
    audio.raw_entity = TEST_MAX_ENTITIES - 1;
    set_test_vec3(audio.origin, 1.0f);
    audio.volume = 4.0f;
    audio.attenuation = 0.0f;
    audio.time_offset = 0.0f;
    audio.pitch = WORR_EVENT_SPATIAL_AUDIO_MAX_PITCH;
    record = make_payload_event(262, WORR_EVENT_TYPE_AUDIO_CUE,
                                WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1,
                                &audio, sizeof(audio));
    CHECK(Worr_EventRecordValidateV1(&record, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->channel = 8;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->raw_entity =
        TEST_MAX_ENTITIES;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    invalid = record;
    ((worr_event_payload_spatial_audio_v1 *)invalid.payload)->origin[1] =
        INFINITY;
    CHECK(!Worr_EventRecordValidateV1(&invalid, TEST_MAX_ENTITIES));
    return 0;
}

static int test_keyed_poi_catalog_and_semantics(void)
{
    worr_event_record_v1 base = make_keyed_poi_event(60, 1, 1024, 5000);
    worr_event_record_v1 other;
    worr_event_payload_keyed_poi_v1 payload;
    uint64_t base_hash;
    uint64_t other_hash;

    CHECK(WORR_EVENT_MODEL_REVISION == 2);
    CHECK(WORR_EVENT_PAYLOAD_KEYED_POI_V1 == 13);
    CHECK(sizeof(worr_event_payload_keyed_poi_v1) == 20);
    CHECK(Worr_EventRecordValidateV1(&base, TEST_MAX_ENTITIES));
    CHECK(Worr_EventRecordHashV1(&base, TEST_MAX_ENTITIES, &base_hash));

    other = make_keyed_poi_event(60, 1, 1024, 0);
    CHECK(Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = make_keyed_poi_event(60, 1, UINT16_MAX, UINT16_MAX - 1u);
    CHECK(Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = make_keyed_poi_event(
        60, 1, 1024, WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS);
    CHECK(Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));

    other = base;
    keyed_poi_payload_get(&other, &payload);
    payload.key = 0;
    keyed_poi_payload_set(&other, &payload);
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));

    other = base;
    keyed_poi_payload_get(&other, &payload);
    payload.flags |= 1u << 1;
    keyed_poi_payload_set(&other, &payload);
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    keyed_poi_payload_get(&other, &payload);
    payload.position[1] = NAN;
    keyed_poi_payload_set(&other, &payload);
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    payload.position[1] = INFINITY;
    keyed_poi_payload_set(&other, &payload);
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));

    other = base;
    other.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    other.expiry_tick = other.source_tick + 1u;
    CHECK(Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other.expiry_tick++;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    other.source_tick = UINT32_MAX;
    other.expiry_tick = 0;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.delivery_class = WORR_EVENT_DELIVERY_PERSISTENT_STATE;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.delivery_class = WORR_EVENT_DELIVERY_COSMETIC;
    other.expiry_tick = other.source_tick + 1u;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.expiry_tick = other.source_tick + 1u;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.prediction_class = WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.flags &= ~WORR_EVENT_FLAG_REPLAY_SAFE;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.flags &= ~WORR_EVENT_FLAG_PRESENT_ONCE;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.flags |= WORR_EVENT_FLAG_CRITICAL;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.payload_size--;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other = base;
    other.payload[sizeof(payload)] = 1;
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));

    other = base;
    other.source_time_us = UINT64_MAX - UINT64_C(4999999);
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    other.source_time_us = UINT64_MAX - UINT64_C(5000000);
    CHECK(Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));

    other = make_keyed_poi_event(
        60, 1, 1024, WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS);
    keyed_poi_payload_get(&other, &payload);
    payload.position[0] = 1.0f;
    keyed_poi_payload_set(&other, &payload);
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    payload.position[0] = 0.0f;
    payload.image_index = 1;
    keyed_poi_payload_set(&other, &payload);
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    payload.image_index = 0;
    payload.color_index = 1;
    keyed_poi_payload_set(&other, &payload);
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));
    payload.color_index = 0;
    payload.flags = WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM;
    keyed_poi_payload_set(&other, &payload);
    CHECK(!Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));

    other = base;
    keyed_poi_payload_get(&other, &payload);
    payload.position[1] = -0.0f;
    keyed_poi_payload_set(&other, &payload);
    keyed_poi_payload_get(&base, &payload);
    payload.position[1] = 0.0f;
    keyed_poi_payload_set(&base, &payload);
    CHECK(Worr_EventRecordSemanticallyEqualV1(
        &base, &other, TEST_MAX_ENTITIES));
    CHECK(Worr_EventRecordHashV1(&base, TEST_MAX_ENTITIES, &base_hash));
    CHECK(Worr_EventRecordHashV1(&other, TEST_MAX_ENTITIES, &other_hash));
    CHECK(base_hash == other_hash);

#define CHECK_POI_PAYLOAD_CHANGE(statement)                                 \
    do {                                                                    \
        other = base;                                                       \
        keyed_poi_payload_get(&other, &payload);                            \
        statement;                                                          \
        keyed_poi_payload_set(&other, &payload);                            \
        CHECK(Worr_EventRecordValidateV1(&other, TEST_MAX_ENTITIES));       \
        CHECK(!Worr_EventRecordSemanticallyEqualV1(                         \
            &base, &other, TEST_MAX_ENTITIES));                             \
        CHECK(Worr_EventRecordHashV1(                                       \
            &other, TEST_MAX_ENTITIES, &other_hash));                       \
        CHECK(base_hash != other_hash);                                     \
    } while (0)
    CHECK_POI_PAYLOAD_CHANGE(payload.key++);
    CHECK_POI_PAYLOAD_CHANGE(payload.lifetime_ms++);
    CHECK_POI_PAYLOAD_CHANGE(payload.position[0] += 1.0f);
    CHECK_POI_PAYLOAD_CHANGE(payload.image_index++);
    CHECK_POI_PAYLOAD_CHANGE(payload.color_index++);
    CHECK_POI_PAYLOAD_CHANGE(payload.flags = 0);
#undef CHECK_POI_PAYLOAD_CHANGE
    return 0;
}

static int test_keyed_poi_journal_fifo_and_expiry(void)
{
    worr_event_journal_v1 journal;
    worr_event_journal_slot_v1 storage[2];
    worr_event_journal_slot_v1 transient_storage[1];
    worr_event_slot_ref_v1 first;
    worr_event_slot_ref_v1 second;
    worr_event_slot_ref_v1 third;
    worr_event_record_v1 event;

    CHECK(Worr_EventJournalInitV1(&journal, storage, 2,
                                  TEST_MAX_ENTITIES, 70));
    event = make_keyed_poi_event(70, 1, 100, 5000);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event, &first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    event = make_keyed_poi_event(70, 2, 100, 4000);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event, &second) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(first.index != second.index && journal.occupied == 2);
    CHECK(Worr_EventJournalResolveV1(&journal, first)->record.event_id.sequence ==
          1);
    CHECK(Worr_EventJournalResolveV1(&journal, second)->record.event_id.sequence ==
          2);

    event = make_keyed_poi_event(70, 3, 100, 3000);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event, &third) ==
          WORR_EVENT_JOURNAL_CAPACITY_FATAL);
    CHECK(!Worr_EventReceiptContainsV1(&journal.receipt, event.event_id));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, second) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event, &third) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(!Worr_EventJournalResolveV1(&journal, first));
    CHECK(Worr_EventJournalResolveV1(&journal, second));
    CHECK(Worr_EventJournalResolveV1(&journal, third));

    CHECK(Worr_EventJournalInitV1(&journal, transient_storage, 1,
                                  TEST_MAX_ENTITIES, 71));
    event = make_keyed_poi_event(71, 1, 200, 25);
    event.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    event.expiry_tick = event.source_tick + 1u;
    CHECK(Worr_EventRecordValidateV1(&event, TEST_MAX_ENTITIES));
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event, &first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalNeedsPresentationV1(
        &journal, first, event.source_tick));
    CHECK(Worr_EventJournalExpireV1(&journal, event.source_tick) == 0);
    CHECK(Worr_EventJournalExpireV1(&journal, event.expiry_tick) == 1);
    CHECK(!Worr_EventJournalNeedsPresentationV1(
        &journal, first, event.expiry_tick));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, first) ==
          WORR_EVENT_JOURNAL_TERMINAL);
    return 0;
}

static int test_receipts_order_loss_and_wrap(void)
{
    worr_event_receipt_ack_v1 receipt;
    worr_event_journal_v1 journal;
    worr_event_journal_slot_v1 storage[8];
    worr_event_slot_ref_v1 slot;
    const worr_event_journal_slot_v1 *resolved;
    worr_event_record_v1 event1 =
        make_event(true, 7, 1, 1, WORR_EVENT_DELIVERY_TRANSIENT,
                   WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 11);
    worr_event_record_v1 event2 =
        make_event(true, 7, 2, 2, WORR_EVENT_DELIVERY_TRANSIENT,
                   WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 22);
    worr_event_record_v1 event3 =
        make_event(true, 7, 3, 3, WORR_EVENT_DELIVERY_TRANSIENT,
                   WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 33);
    worr_event_record_v1 conflict;
    worr_event_id_v1 next;

    CHECK(!Worr_EventReceiptInitV1(&receipt, 0));
    CHECK(Worr_EventReceiptInitV1(&receipt, 7));
    CHECK(Worr_EventReceiptMarkV1(&receipt, event3.event_id) ==
          WORR_EVENT_RECEIPT_ACCEPTED);
    CHECK(receipt.highest_contiguous == 0 && receipt.selective_mask == 4);
    CHECK(Worr_EventReceiptContainsV1(&receipt, event3.event_id));
    CHECK(!Worr_EventReceiptContainsV1(&receipt, event2.event_id));
    CHECK(Worr_EventReceiptMarkV1(&receipt, event1.event_id) ==
          WORR_EVENT_RECEIPT_ACCEPTED);
    CHECK(receipt.highest_contiguous == 1 && receipt.selective_mask == 2);
    CHECK(Worr_EventReceiptMarkV1(&receipt, event2.event_id) ==
          WORR_EVENT_RECEIPT_ACCEPTED);
    CHECK(receipt.highest_contiguous == 3 && receipt.selective_mask == 0);
    CHECK(Worr_EventReceiptMarkV1(&receipt, event2.event_id) ==
          WORR_EVENT_RECEIPT_DUPLICATE);
    CHECK(Worr_EventReceiptMarkV1(
              &receipt, (worr_event_id_v1){7, 68}) ==
          WORR_EVENT_RECEIPT_OUTSIDE_WINDOW);
    CHECK(Worr_EventReceiptMarkV1(
              &receipt, (worr_event_id_v1){8, 4}) ==
          WORR_EVENT_RECEIPT_WRONG_EPOCH);
    CHECK(!Worr_EventReceiptAdvanceEpochV1(&receipt, 6));
    CHECK(!Worr_EventReceiptAdvanceEpochV1(&receipt, 7));
    CHECK(Worr_EventReceiptAdvanceEpochV1(&receipt, 8));
    CHECK(receipt.highest_contiguous == 0 && receipt.selective_mask == 0);

    CHECK(Worr_EventIdNextV1((worr_event_id_v1){0, 0}, &next));
    CHECK(next.stream_epoch == 1 && next.sequence == 1);
    CHECK(Worr_EventIdNextV1((worr_event_id_v1){9, UINT32_MAX}, &next));
    CHECK(next.stream_epoch == 10 && next.sequence == 1);
    CHECK(!Worr_EventIdNextV1(
        (worr_event_id_v1){UINT32_MAX, UINT32_MAX}, &next));
    CHECK(!Worr_EventIdNextV1((worr_event_id_v1){0, 4}, &next));

    CHECK(!Worr_EventJournalInitV1(&journal, storage, 8,
                                   TEST_MAX_ENTITIES, 0));
    CHECK(Worr_EventJournalInitV1(&journal, storage, 8,
                                  TEST_MAX_ENTITIES, 7));
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event3, &slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(journal.receipt.highest_contiguous == 0 &&
          journal.receipt.selective_mask == 4);
    CHECK(!Worr_EventJournalFindAuthoritativeV1(
        &journal, event2.event_id, &slot));
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event1, &slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event2, &slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(journal.receipt.highest_contiguous == 3);
    CHECK(Worr_EventJournalFindAuthoritativeAtOrAfterV1(&journal, 1, &slot));
    resolved = Worr_EventJournalResolveV1(&journal, slot);
    CHECK(resolved && resolved->record.event_id.sequence == 1);
    CHECK(Worr_EventJournalFindAuthoritativeAtOrAfterV1(&journal, 2, &slot));
    resolved = Worr_EventJournalResolveV1(&journal, slot);
    CHECK(resolved && resolved->record.event_id.sequence == 2);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event2, &slot) ==
          WORR_EVENT_JOURNAL_DUPLICATE);
    conflict = event2;
    conflict.payload[0] ^= 1;
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &conflict, &slot) ==
          WORR_EVENT_JOURNAL_CONFLICT);
    CHECK(!Worr_EventJournalAdvanceEpochV1(&journal, 6));
    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 8));
    CHECK(journal.occupied == 0 && journal.receipt.stream_epoch == 8);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event1, &slot) ==
          WORR_EVENT_JOURNAL_WRONG_EPOCH);

    event1 = make_event(true, 8, 1, 1,
                        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 101);
    event2 = make_event(true, 8, 2, 2,
                        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 102);
    event3 = make_event(true, 8, 3, 3,
                        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 103);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event1, &slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event3, &slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(journal.receipt.highest_contiguous == 1 &&
          journal.receipt.selective_mask == 2);
    CHECK(!Worr_EventJournalNeedsPresentationV1(
        &journal, slot, event3.source_tick));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, slot) ==
          WORR_EVENT_JOURNAL_NOT_READY);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event2, &slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(journal.receipt.highest_contiguous == 3);
    CHECK(Worr_EventJournalFindAuthoritativeAtOrAfterV1(&journal, 2, &slot));
    resolved = Worr_EventJournalResolveV1(&journal, slot);
    CHECK(resolved && resolved->record.event_id.sequence == 2);
    CHECK(Worr_EventJournalNeedsPresentationV1(
        &journal, slot, event2.source_tick));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalFindAuthoritativeAtOrAfterV1(&journal, 3, &slot));
    resolved = Worr_EventJournalResolveV1(&journal, slot);
    CHECK(resolved && resolved->record.event_id.sequence == 3);
    CHECK(Worr_EventJournalNeedsPresentationV1(
        &journal, slot, event3.source_tick));

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 9));
    journal.receipt.highest_contiguous = UINT32_MAX - 1;
    event1 = make_event(true, 9, UINT32_MAX, 1,
                        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 201);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event1, &slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(journal.receipt.highest_contiguous == UINT32_MAX &&
          journal.receipt.selective_mask == 0);
    CHECK(Worr_EventJournalFindAuthoritativeAtOrAfterV1(
        &journal, UINT32_MAX, &slot));
    resolved = Worr_EventJournalResolveV1(&journal, slot);
    CHECK(resolved && resolved->record.event_id.sequence == UINT32_MAX);
    return 0;
}

static int test_prediction_matching(void)
{
    worr_event_journal_v1 journal;
    worr_event_journal_slot_v1 storage[8];
    worr_event_slot_ref_v1 predicted_slot;
    worr_event_slot_ref_v1 authority_slot;
    const worr_event_journal_slot_v1 *resolved;
    worr_event_record_v1 predicted =
        make_event(false, 0, 0, 1, WORR_EVENT_DELIVERY_COSMETIC,
                   WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 101);
    worr_event_record_v1 authority = authorize(predicted, 11, 1);
    worr_event_record_v1 mismatch;
    worr_event_record_v1 repeated;

    CHECK(Worr_EventJournalInitV1(&journal, storage, 8,
                                  TEST_MAX_ENTITIES, 11));
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalNeedsPresentationV1(
        &journal, predicted_slot, predicted.source_tick));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &authority, &authority_slot) ==
          WORR_EVENT_JOURNAL_MATCHED);
    CHECK(authority_slot.index == predicted_slot.index &&
          authority_slot.generation == predicted_slot.generation);
    resolved = Worr_EventJournalResolveV1(&journal, authority_slot);
    CHECK(resolved &&
          (resolved->state & (WORR_EVENT_SLOT_RECEIVED |
                              WORR_EVENT_SLOT_PREDICTED |
                              WORR_EVENT_SLOT_MATCHED |
                              WORR_EVENT_SLOT_PRESENTED)) ==
              (WORR_EVENT_SLOT_RECEIVED | WORR_EVENT_SLOT_PREDICTED |
               WORR_EVENT_SLOT_MATCHED | WORR_EVENT_SLOT_PRESENTED));
    CHECK(!Worr_EventJournalNeedsPresentationV1(
        &journal, authority_slot, authority.source_tick));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, authority_slot) ==
          WORR_EVENT_JOURNAL_ALREADY_PRESENTED);
    mismatch = authority;
    mismatch.event_id.sequence = 2;
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &mismatch, &authority_slot) ==
          WORR_EVENT_JOURNAL_CONFLICT);
    CHECK(!Worr_EventReceiptContainsV1(
        &journal.receipt, mismatch.event_id));

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 12));
    authority = authorize(predicted, 12, 1);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &authority, &authority_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, authority_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_MATCHED);
    CHECK(predicted_slot.index == authority_slot.index);
    resolved = Worr_EventJournalResolveV1(&journal, predicted_slot);
    CHECK(resolved &&
          (resolved->state & (WORR_EVENT_SLOT_RECEIVED |
                              WORR_EVENT_SLOT_PREDICTED |
                              WORR_EVENT_SLOT_MATCHED |
                              WORR_EVENT_SLOT_PRESENTED)) ==
              (WORR_EVENT_SLOT_RECEIVED | WORR_EVENT_SLOT_PREDICTED |
               WORR_EVENT_SLOT_MATCHED | WORR_EVENT_SLOT_PRESENTED));
    CHECK(!Worr_EventJournalNeedsPresentationV1(
        &journal, predicted_slot, predicted.source_tick));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, predicted_slot) ==
          WORR_EVENT_JOURNAL_ALREADY_PRESENTED);

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 13));
    mismatch = predicted;
    mismatch.payload[0] ^= 1;
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    mismatch = authorize(mismatch, 13, 1);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &mismatch, &authority_slot) ==
          WORR_EVENT_JOURNAL_CORRECTED);
    CHECK(authority_slot.index == predicted_slot.index &&
          authority_slot.generation == predicted_slot.generation);
    resolved = Worr_EventJournalResolveV1(&journal, predicted_slot);
    CHECK(resolved &&
          (resolved->state & (WORR_EVENT_SLOT_RECEIVED |
                              WORR_EVENT_SLOT_PREDICTED |
                              WORR_EVENT_SLOT_CORRECTED)) ==
              (WORR_EVENT_SLOT_RECEIVED | WORR_EVENT_SLOT_PREDICTED |
               WORR_EVENT_SLOT_CORRECTED));
    CHECK((resolved->state & (WORR_EVENT_SLOT_MATCHED |
                              WORR_EVENT_SLOT_PRESENTED |
                              WORR_EVENT_SLOT_CANCELED |
                              WORR_EVENT_SLOT_EXPIRED)) == 0);
    CHECK(resolved->record.payload[0] == mismatch.payload[0]);
    CHECK(Worr_EventReceiptContainsV1(&journal.receipt, mismatch.event_id));
    CHECK(Worr_EventJournalNeedsPresentationV1(
        &journal, authority_slot, mismatch.source_tick));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, authority_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 14));
    predicted = make_event(false, 0, 0, 1,
                           WORR_EVENT_DELIVERY_COSMETIC,
                           WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 500);
    mismatch = predicted;
    mismatch.payload[0] ^= 1;
    mismatch = authorize(mismatch, 14, 1);
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &mismatch, &authority_slot) ==
          WORR_EVENT_JOURNAL_CORRECTED_AFTER_PRESENTATION);
    resolved = Worr_EventJournalResolveV1(&journal, authority_slot);
    CHECK(resolved &&
          (resolved->state & (WORR_EVENT_SLOT_RECEIVED |
                              WORR_EVENT_SLOT_PREDICTED |
                              WORR_EVENT_SLOT_PRESENTED |
                              WORR_EVENT_SLOT_CORRECTED)) ==
              (WORR_EVENT_SLOT_RECEIVED | WORR_EVENT_SLOT_PREDICTED |
               WORR_EVENT_SLOT_PRESENTED | WORR_EVENT_SLOT_CORRECTED));
    CHECK(!Worr_EventJournalNeedsPresentationV1(
        &journal, authority_slot, mismatch.source_tick));
    CHECK(Worr_EventReceiptContainsV1(&journal.receipt, mismatch.event_id));

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 15));
    predicted = make_event(false, 0, 0, 5, WORR_EVENT_DELIVERY_COSMETIC,
                           WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 999);
    repeated = predicted;
    repeated.source_ordinal = 6;
    repeated.prediction_key.emitter_ordinal = 6;
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &repeated, &authority_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(predicted_slot.index != authority_slot.index);

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 16));
    predicted = make_event(false, 0, 0, 1, WORR_EVENT_DELIVERY_TRANSIENT,
                           WORR_EVENT_PREDICTION_COMMAND_DEFERRED, 1, 4);
    authority = authorize(predicted, 16, 1);
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(!Worr_EventJournalNeedsPresentationV1(
        &journal, predicted_slot, predicted.source_tick));
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &authority, &authority_slot) ==
          WORR_EVENT_JOURNAL_MATCHED);
    CHECK(Worr_EventJournalNeedsPresentationV1(
        &journal, authority_slot, authority.source_tick));

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 17));
    predicted = make_event(false, 0, 0, 1,
                           WORR_EVENT_DELIVERY_COSMETIC,
                           WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 700);
    authority = predicted;
    authority.payload[0] ^= 1;
    authority = authorize(authority, 17, 1);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &authority, &authority_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_CORRECTED);
    CHECK(predicted_slot.index == authority_slot.index);
    resolved = Worr_EventJournalResolveV1(&journal, predicted_slot);
    CHECK(resolved && resolved->record.payload[0] == authority.payload[0]);
    CHECK((resolved->state & WORR_EVENT_SLOT_CORRECTED) != 0);
    CHECK(Worr_EventJournalNeedsPresentationV1(
        &journal, predicted_slot, authority.source_tick));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_CORRECTED_AFTER_PRESENTATION);
    CHECK(!Worr_EventJournalNeedsPresentationV1(
        &journal, predicted_slot, authority.source_tick));

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 18));
    predicted = make_event(false, 0, 0, 1,
                           WORR_EVENT_DELIVERY_COSMETIC,
                           WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 800);
    authority = authorize(predicted, 18, 1);
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalCancelV1(&journal, predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &authority, &authority_slot) ==
          WORR_EVENT_JOURNAL_MATCHED);
    resolved = Worr_EventJournalResolveV1(&journal, authority_slot);
    CHECK(resolved &&
          (resolved->state & (WORR_EVENT_SLOT_CANCELED |
                              WORR_EVENT_SLOT_EXPIRED)) == 0);
    CHECK(Worr_EventJournalNeedsPresentationV1(
        &journal, authority_slot, authority.source_tick));

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 19));
    predicted = make_event(false, 0, 0, 1,
                           WORR_EVENT_DELIVERY_TRANSIENT,
                           WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 900);
    authority = authorize(predicted, 19, 1);
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalExpireV1(&journal, predicted.expiry_tick) == 1);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &authority, &authority_slot) ==
          WORR_EVENT_JOURNAL_MATCHED);
    resolved = Worr_EventJournalResolveV1(&journal, authority_slot);
    CHECK(resolved && (resolved->state & WORR_EVENT_SLOT_EXPIRED) == 0);
    CHECK(Worr_EventJournalNeedsPresentationV1(
        &journal, authority_slot, authority.source_tick));

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 20));
    predicted = make_event(false, 0, 0, 1,
                           WORR_EVENT_DELIVERY_COSMETIC,
                           WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 1000);
    predicted.event_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
    predicted.payload_kind = WORR_EVENT_PAYLOAD_VEC3;
    predicted.payload_size = sizeof(worr_event_payload_vec3_v1);
    memset(predicted.payload, 0, sizeof(predicted.payload));
    authority = authorize(predicted, 20, 1);
    {
        worr_event_payload_vec3_v1 signed_zero = {{-0.0f, 0.0f, 0.0f}};
        memcpy(authority.payload, &signed_zero, sizeof(signed_zero));
    }
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &authority, &authority_slot) ==
          WORR_EVENT_JOURNAL_MATCHED);
    resolved = Worr_EventJournalResolveV1(&journal, authority_slot);
    CHECK(resolved && (resolved->state & WORR_EVENT_SLOT_MATCHED) != 0);
    CHECK((resolved->state & WORR_EVENT_SLOT_CORRECTED) == 0);
    return 0;
}

static int test_prediction_correction_receipt_edges(void)
{
    worr_event_journal_v1 journal;
    worr_event_journal_slot_v1 storage[4];
    worr_event_journal_slot_v1 single_storage[1];
    worr_event_slot_ref_v1 predicted_slot;
    worr_event_slot_ref_v1 corrected_slot;
    worr_event_slot_ref_v1 retransmit_slot;
    worr_event_slot_ref_v1 sequence2_slot;
    worr_event_slot_ref_v1 sequence3_slot;
    worr_event_record_v1 predicted;
    worr_event_record_v1 corrected;
    worr_event_record_v1 changed;
    worr_event_record_v1 sequence2;
    worr_event_record_v1 sequence3;
    const worr_event_journal_slot_v1 *resolved;

    CHECK(Worr_EventJournalInitV1(&journal, storage, 4,
                                  TEST_MAX_ENTITIES, 31));
    predicted = make_event(false, 0, 0, 1,
                           WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                           WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 1101);
    corrected = predicted;
    corrected.payload[0] ^= 1;
    corrected = authorize(corrected, 31, 1);
    sequence2 = make_event(true, 31, 2, 2,
                           WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                           WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 1102);
    sequence3 = make_event(true, 31, 3, 3,
                           WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                           WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 1103);

    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &sequence2, &sequence2_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &sequence3, &sequence3_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(journal.receipt.highest_contiguous == 0 &&
          journal.receipt.selective_mask == 6);
    CHECK(!Worr_EventJournalNeedsPresentationV1(
        &journal, sequence2_slot, sequence2.source_tick));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, sequence2_slot) ==
          WORR_EVENT_JOURNAL_NOT_READY);

    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &corrected, &corrected_slot) ==
          WORR_EVENT_JOURNAL_CORRECTED);
    CHECK(corrected_slot.index == predicted_slot.index &&
          corrected_slot.generation == predicted_slot.generation);
    CHECK(journal.receipt.highest_contiguous == 3 &&
          journal.receipt.selective_mask == 0);
    CHECK(Worr_EventReceiptContainsV1(&journal.receipt, corrected.event_id));
    CHECK(Worr_EventJournalNeedsPresentationV1(
        &journal, sequence2_slot, sequence2.source_tick));

    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &corrected, &retransmit_slot) ==
          WORR_EVENT_JOURNAL_DUPLICATE);
    CHECK(retransmit_slot.index == corrected_slot.index &&
          retransmit_slot.generation == corrected_slot.generation);

    changed = corrected;
    changed.payload[1] ^= 1;
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &changed, &retransmit_slot) ==
          WORR_EVENT_JOURNAL_CONFLICT);
    CHECK(retransmit_slot.index == corrected_slot.index &&
          retransmit_slot.generation == corrected_slot.generation);
    resolved = Worr_EventJournalResolveV1(&journal, corrected_slot);
    CHECK(resolved && resolved->record.payload[0] == corrected.payload[0] &&
          resolved->record.payload[1] == corrected.payload[1]);
    CHECK(journal.receipt.highest_contiguous == 3 &&
          journal.receipt.selective_mask == 0);

    CHECK(Worr_EventJournalInitV1(&journal, single_storage, 1,
                                  TEST_MAX_ENTITIES, 32));
    predicted = make_event(false, 0, 0, 1,
                           WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                           WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 1201);
    corrected = predicted;
    corrected.payload[0] ^= 1;
    corrected = authorize(corrected, 32, 1);
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &corrected, &corrected_slot) ==
          WORR_EVENT_JOURNAL_CORRECTED);
    CHECK(corrected_slot.index == predicted_slot.index &&
          corrected_slot.generation == predicted_slot.generation);
    CHECK(journal.occupied == 1 &&
          journal.receipt.highest_contiguous == 1);
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, corrected_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);

    sequence2 = make_event(true, 32, 2, 2,
                           WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
                           WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 1202);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &sequence2, &sequence2_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(!Worr_EventJournalResolveV1(&journal, corrected_slot));
    CHECK(sequence2_slot.index == corrected_slot.index &&
          sequence2_slot.generation != corrected_slot.generation);
    CHECK(journal.occupied == 1 &&
          journal.receipt.highest_contiguous == 2);
    return 0;
}

static int test_predicted_cosmetic_cannot_coalesce_authority(void)
{
    worr_event_journal_v1 journal;
    worr_event_journal_slot_v1 storage[1];
    worr_event_slot_ref_v1 authority_slot;
    worr_event_slot_ref_v1 predicted_slot;
    worr_event_record_v1 authority_candidate;
    worr_event_record_v1 authority;
    worr_event_record_v1 predicted;
    const worr_event_journal_slot_v1 *resolved;

    CHECK(Worr_EventJournalInitV1(&journal, storage, 1,
                                  TEST_MAX_ENTITIES, 33));
    authority_candidate =
        make_event(false, 0, 0, 1, WORR_EVENT_DELIVERY_COSMETIC,
                   WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 1301);
    authority = authorize(authority_candidate, 33, 1);
    predicted = make_event(false, 0, 0, 2,
                           WORR_EVENT_DELIVERY_COSMETIC,
                           WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 1302);

    CHECK(Worr_EventJournalInsertAuthoritativeV1(
              &journal, &authority, &authority_slot) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertPredictedV1(
              &journal, &predicted, &predicted_slot) ==
          WORR_EVENT_JOURNAL_DROPPED_OVERFLOW);
    CHECK(predicted_slot.index == WORR_EVENT_SLOT_INVALID &&
          predicted_slot.generation == 0);
    resolved = Worr_EventJournalResolveV1(&journal, authority_slot);
    CHECK(resolved &&
          (resolved->state & WORR_EVENT_SLOT_RECEIVED) != 0 &&
          (resolved->state & WORR_EVENT_SLOT_PREDICTED) == 0);
    CHECK(resolved->record.event_id.stream_epoch == 33 &&
          resolved->record.event_id.sequence == 1 &&
          resolved->record.source_ordinal == authority.source_ordinal &&
          memcmp(resolved->record.payload, authority.payload,
                 authority.payload_size) == 0);
    CHECK(journal.occupied == 1 &&
          journal.receipt.highest_contiguous == 1);
    return 0;
}

static int test_generation_capacity_and_terminal_states(void)
{
    worr_event_journal_v1 journal;
    worr_event_journal_slot_v1 storage[2];
    worr_event_slot_ref_v1 first;
    worr_event_slot_ref_v1 second;
    worr_event_slot_ref_v1 third;
    worr_event_record_v1 event1;
    worr_event_record_v1 event2;
    worr_event_record_v1 event3;

    CHECK(Worr_EventJournalInitV1(&journal, storage, 2,
                                  TEST_MAX_ENTITIES, 20));
    event1 = make_event(true, 20, 1, 1, WORR_EVENT_DELIVERY_TRANSIENT,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 1);
    event2 = make_event(true, 20, 2, 2, WORR_EVENT_DELIVERY_TRANSIENT,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 2, 1);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event1, &first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event2, &second) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(first.index != second.index);
    CHECK(Worr_EventJournalResolveV1(&journal, first)->record.source_entity
              .generation == 1);
    CHECK(Worr_EventJournalResolveV1(&journal, second)->record.source_entity
              .generation == 2);

    event3 = make_event(true, 20, 3, 3, WORR_EVENT_DELIVERY_TRANSIENT,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 3, 3);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event3, &third) ==
          WORR_EVENT_JOURNAL_DROPPED_OVERFLOW);
    CHECK(!Worr_EventReceiptContainsV1(&journal.receipt, event3.event_id));
    event3.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    event3.expiry_tick = 0;
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event3, &third) ==
          WORR_EVENT_JOURNAL_CAPACITY_FATAL);
    CHECK(!Worr_EventReceiptContainsV1(&journal.receipt, event3.event_id));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event3, &third) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(!Worr_EventJournalResolveV1(&journal, first));
    CHECK(Worr_EventJournalResolveV1(&journal, third));

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 21));
    event1 = make_event(true, 21, 1, 7, WORR_EVENT_DELIVERY_COSMETIC,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 1);
    event2 = make_event(true, 21, 2, 7, WORR_EVENT_DELIVERY_COSMETIC,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 2);
    journal.capacity = 1;
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event1, &first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event2, &second) ==
          WORR_EVENT_JOURNAL_COALESCED);
    CHECK(!Worr_EventJournalResolveV1(&journal, first));
    CHECK(Worr_EventJournalResolveV1(&journal, second)->record.event_id.sequence ==
          2);
    journal.capacity = 2;

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 22));
    event1 = make_event(true, 22, 2, 1,
                        WORR_EVENT_DELIVERY_PERSISTENT_STATE,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 2);
    event2 = make_event(true, 22, 1, 1,
                        WORR_EVENT_DELIVERY_PERSISTENT_STATE,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 1);
    event3 = make_event(true, 22, 3, 1,
                        WORR_EVENT_DELIVERY_PERSISTENT_STATE,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 3);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event1, &first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event2, &second) ==
          WORR_EVENT_JOURNAL_DROPPED_STALE);
    CHECK(journal.receipt.highest_contiguous == 2);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event3, &third) ==
          WORR_EVENT_JOURNAL_SUPERSEDED);
    CHECK(!Worr_EventJournalResolveV1(&journal, first));
    CHECK(Worr_EventJournalResolveV1(&journal, third)->record.event_id.sequence ==
          3);

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 23));
    event1 = make_event(true, 23, 1, 1, WORR_EVENT_DELIVERY_COSMETIC,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 8);
    event1.source_tick = UINT32_MAX - 3;
    event1.expiry_tick = event1.source_tick + 8;
    CHECK(Worr_EventRecordValidateV1(&event1, TEST_MAX_ENTITIES));
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event1, &first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalNeedsPresentationV1(&journal, first, UINT32_MAX));
    CHECK(Worr_EventJournalExpireV1(&journal, 3) == 0);
    CHECK(Worr_EventJournalExpireV1(&journal, 4) == 1);
    CHECK(!Worr_EventJournalNeedsPresentationV1(&journal, first, 4));
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, first) ==
          WORR_EVENT_JOURNAL_TERMINAL);

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 24));
    event1 = make_event(false, 0, 0, 1, WORR_EVENT_DELIVERY_COSMETIC,
                        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 9);
    CHECK(Worr_EventJournalInsertPredictedV1(&journal, &event1, &first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalCancelV1(&journal, first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalCancelV1(&journal, first) ==
          WORR_EVENT_JOURNAL_TERMINAL);
    CHECK(!Worr_EventJournalNeedsPresentationV1(
        &journal, first, event1.source_tick));

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 25));
    journal.capacity = 1;
    event1 = make_event(false, 0, 0, 1, WORR_EVENT_DELIVERY_COSMETIC,
                        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE, 1, 10);
    event2 = make_event(true, 25, 1, 2, WORR_EVENT_DELIVERY_TRANSIENT,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 11);
    event3 = authorize(event1, 25, 1);
    CHECK(Worr_EventJournalInsertPredictedV1(&journal, &event1, &first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event2, &second) ==
          WORR_EVENT_JOURNAL_DROPPED_OVERFLOW);
    CHECK(Worr_EventJournalResolveV1(&journal, first));
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event3, &third) ==
          WORR_EVENT_JOURNAL_MATCHED);
    CHECK(!Worr_EventJournalNeedsPresentationV1(
        &journal, third, event3.source_tick));
    journal.capacity = 2;

    CHECK(Worr_EventJournalAdvanceEpochV1(&journal, 26));
    journal.capacity = 1;
    event1 = make_event(true, 26, 1, 1,
                        WORR_EVENT_DELIVERY_PERSISTENT_STATE,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 31);
    event2 = make_event(true, 26, 2, 2,
                        WORR_EVENT_DELIVERY_PERSISTENT_STATE,
                        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY, 1, 32);
    event2.source_entity.index = 5;
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event1, &first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalMarkPresentedV1(&journal, first) ==
          WORR_EVENT_JOURNAL_INSERTED);
    CHECK(Worr_EventJournalInsertAuthoritativeV1(&journal, &event2, &second) ==
          WORR_EVENT_JOURNAL_CAPACITY_FATAL);
    CHECK(!Worr_EventReceiptContainsV1(&journal.receipt, event2.event_id));
    journal.capacity = 2;
    return 0;
}

int main(void)
{
    CHECK(test_validation_and_hash() == 0);
    CHECK(test_legacy_entity_catalog_and_semantic_hash() == 0);
    CHECK(test_legacy_temp_catalog() == 0);
    CHECK(test_muzzle_and_spatial_audio_catalog() == 0);
    CHECK(test_keyed_poi_catalog_and_semantics() == 0);
    CHECK(test_keyed_poi_journal_fifo_and_expiry() == 0);
    CHECK(test_receipts_order_loss_and_wrap() == 0);
    CHECK(test_prediction_matching() == 0);
    CHECK(test_prediction_correction_receipt_edges() == 0);
    CHECK(test_predicted_cosmetic_cannot_coalesce_authority() == 0);
    CHECK(test_generation_capacity_and_terminal_states() == 0);
    printf("event journal: all deterministic ABI/journal checks passed\n");
    return 0;
}
