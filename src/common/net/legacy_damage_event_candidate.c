/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
at your option) any later version.
*/

#include "common/net/legacy_damage_event_candidate.h"

#include "shared/shared.h"
#include "common/math.h"
#include "common/protocol.h"

#include <string.h>

worr_legacy_damage_event_candidate_result_v1
Worr_LegacyDamageEventDecodeRawV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_damage_t *damage_out)
{
    q2proto_svc_damage_t damage;
    uint32_t index;
    size_t expected_size;

    if (!raw_message || !damage_out)
        return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_ARGUMENT;
    if (raw_message_size < 2u || raw_message[0] != svc_damage)
        return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_SHAPE;

    memset(&damage, 0, sizeof(damage));
    damage.count = raw_message[1];
    if (damage.count == 0 || damage.count > Q2PROTO_MAX_DAMAGE_INDICATORS)
        return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_SHAPE;
    expected_size = 2u + (size_t)damage.count * 2u;
    if (raw_message_size != expected_size)
        return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_SHAPE;

    for (index = 0; index < damage.count; ++index) {
        const uint8_t encoded = raw_message[2u + index * 2u];
        const uint8_t direction_index = raw_message[3u + index * 2u];

        if (direction_index >= NUMVERTEXNORMALS)
            return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_SHAPE;
        damage.damage[index].damage = encoded & 0x1fu;
        damage.damage[index].health = (encoded & 0x20u) != 0;
        damage.damage[index].armor = (encoded & 0x40u) != 0;
        damage.damage[index].shield = (encoded & 0x80u) != 0;
        memcpy(damage.damage[index].direction, bytedirs[direction_index],
               sizeof(damage.damage[index].direction));
    }

    *damage_out = damage;
    return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_OK;
}

worr_legacy_damage_event_candidate_result_v1
Worr_LegacyDamageEventCandidatesBuildV1(
    const q2proto_svc_damage_t *damage, uint32_t source_tick,
    uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidates_out, uint32_t candidate_capacity,
    uint32_t *candidate_count_out)
{
    worr_event_record_v1 candidates[Q2PROTO_MAX_DAMAGE_INDICATORS];
    uint32_t index;

    if (!damage || !candidates_out || !candidate_count_out ||
        candidate_capacity == 0 || max_entities == 0) {
        return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }
    if (damage->count == 0 ||
        damage->count > Q2PROTO_MAX_DAMAGE_INDICATORS) {
        return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_SHAPE;
    }
    if (damage->count > candidate_capacity)
        return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_CAPACITY;

    for (index = 0; index < damage->count; ++index) {
        worr_event_payload_damage_v1 payload;
        uint32_t damage_flags = 0;

        memset(&candidates[index], 0, sizeof(candidates[index]));
        memset(&payload, 0, sizeof(payload));
        payload.amount = damage->damage[index].damage;
        memcpy(payload.direction, damage->damage[index].direction,
               sizeof(payload.direction));
        if (damage->damage[index].health)
            damage_flags |= WORR_EVENT_DAMAGE_FLAG_HEALTH;
        if (damage->damage[index].armor)
            damage_flags |= WORR_EVENT_DAMAGE_FLAG_ARMOR;
        if (damage->damage[index].shield)
            damage_flags |= WORR_EVENT_DAMAGE_FLAG_SHIELD;
        payload.damage_flags = damage_flags;

        candidates[index].struct_size = sizeof(candidates[index]);
        candidates[index].schema_version = WORR_EVENT_ABI_VERSION;
        candidates[index].model_revision = WORR_EVENT_MODEL_REVISION;
        candidates[index].flags =
            WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE;
        candidates[index].source_tick = source_tick;
        candidates[index].source_ordinal = index;
        candidates[index].source_time_us = source_time_us;
        candidates[index].source_entity.index = 0;
        candidates[index].source_entity.generation = 1;
        candidates[index].subject_entity.index = WORR_EVENT_NO_ENTITY;
        candidates[index].event_type = WORR_EVENT_TYPE_DAMAGE;
        candidates[index].delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
        candidates[index].prediction_class =
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
        candidates[index].expiry_tick = source_tick + 1u;
        candidates[index].payload_kind = WORR_EVENT_PAYLOAD_DAMAGE;
        candidates[index].payload_size = sizeof(payload);
        memcpy(candidates[index].payload, &payload, sizeof(payload));

        if (!Worr_EventRecordCandidateValidateV1(&candidates[index],
                                                 max_entities)) {
            return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_RECORD;
        }
    }

    memcpy(candidates_out, candidates,
           sizeof(candidates[0]) * damage->count);
    *candidate_count_out = damage->count;
    return WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_OK;
}
