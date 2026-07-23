/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/legacy_poi_event_candidate.h"

#include "common/intreadwrite.h"
#include "shared/shared.h"
#include "common/protocol.h"

#include <math.h>
#include <string.h>

static float raw_float(const uint8_t *data)
{
    const uint32_t bits = RL32(data);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool poi_shape_valid(const q2proto_svc_poi_t *poi)
{
    uint32_t index;

    if (poi == NULL || poi->key == 0 ||
        (poi->flags & ~WORR_EVENT_KEYED_POI_KNOWN_FLAGS) != 0) {
        return false;
    }
    for (index = 0; index < 3; ++index) {
        if (!isfinite(poi->pos[index]))
            return false;
    }
    return true;
}

worr_legacy_keyed_poi_event_candidate_result_v1
Worr_LegacyKeyedPOIEventDecodeRawV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_poi_t *poi_out)
{
    q2proto_svc_poi_t poi;
    uint32_t index;

    if (raw_message == NULL || poi_out == NULL) {
        return WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }
    if (raw_message_size != WORR_LEGACY_KEYED_POI_RAW_SIZE ||
        raw_message[0] != svc_poi) {
        return WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_SHAPE;
    }

    memset(&poi, 0, sizeof(poi));
    poi.key = RL16(&raw_message[1]);
    poi.time = RL16(&raw_message[3]);
    for (index = 0; index < 3; ++index)
        poi.pos[index] = raw_float(&raw_message[5u + index * 4u]);
    poi.image = RL16(&raw_message[17]);
    poi.color = raw_message[19];
    poi.flags = raw_message[20];
    if (!poi_shape_valid(&poi))
        return WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_SHAPE;

    *poi_out = poi;
    return WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_OK;
}

worr_legacy_keyed_poi_event_candidate_result_v1
Worr_LegacyKeyedPOIEventCandidateBuildV1(
    const q2proto_svc_poi_t *poi, uint32_t source_tick,
    uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidate_out)
{
    worr_event_record_v1 candidate;
    worr_event_payload_keyed_poi_v1 payload;

    if (poi == NULL || candidate_out == NULL || max_entities == 0) {
        return WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }
    if (!poi_shape_valid(poi))
        return WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_SHAPE;
    memset(&candidate, 0, sizeof(candidate));
    memset(&payload, 0, sizeof(payload));
    payload.key = poi->key;
    payload.lifetime_ms = poi->time;
    if (poi->time != WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS) {
        memcpy(payload.position, poi->pos, sizeof(payload.position));
        payload.image_index = poi->image;
        payload.color_index = poi->color;
        payload.flags = poi->flags;
    }

    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_EVENT_ABI_VERSION;
    candidate.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate.flags =
        WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE;
    candidate.source_tick = source_tick;
    candidate.source_time_us = source_time_us;
    candidate.source_entity.index = 0;
    candidate.source_entity.generation = 1;
    candidate.subject_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.event_type = WORR_EVENT_TYPE_STATE_CHANGE;
    candidate.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    candidate.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate.expiry_tick = 0;
    candidate.payload_kind = WORR_EVENT_PAYLOAD_KEYED_POI_V1;
    candidate.payload_size = sizeof(payload);
    memcpy(candidate.payload, &payload, sizeof(payload));

    if (!Worr_EventRecordCandidateValidateV1(&candidate, max_entities)) {
        return WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_RECORD;
    }

    *candidate_out = candidate;
    return WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_OK;
}
