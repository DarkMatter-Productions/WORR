/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/legacy_help_path_event_candidate.h"

#include "common/intreadwrite.h"
#include "shared/shared.h"
#include "common/math.h"
#include "common/protocol.h"

#include <math.h>
#include <string.h>

#define WORR_LEGACY_HELP_PATH_RAW_SIZE 15u

static float raw_float(const uint8_t *data)
{
    const uint32_t bits = RL32(data);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

worr_legacy_help_path_event_candidate_result_v1
Worr_LegacyHelpPathEventDecodeRawV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_help_path_t *help_path_out)
{
    q2proto_svc_help_path_t help_path;
    uint8_t direction_index;
    uint32_t index;

    if (!raw_message || !help_path_out) {
        return WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }
    if (raw_message_size != WORR_LEGACY_HELP_PATH_RAW_SIZE ||
        raw_message[0] != svc_help_path || raw_message[1] > 1u) {
        return WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_SHAPE;
    }

    memset(&help_path, 0, sizeof(help_path));
    help_path.start = raw_message[1] != 0;
    for (index = 0; index < 3; ++index) {
        help_path.pos[index] = raw_float(&raw_message[2u + index * 4u]);
        if (!isfinite(help_path.pos[index])) {
            return WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_SHAPE;
        }
    }
    direction_index = raw_message[14];
    if (direction_index >= NUMVERTEXNORMALS) {
        return WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_SHAPE;
    }
    memcpy(help_path.dir, bytedirs[direction_index], sizeof(help_path.dir));

    *help_path_out = help_path;
    return WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_OK;
}

worr_legacy_help_path_event_candidate_result_v1
Worr_LegacyHelpPathEventCandidateBuildV1(
    const q2proto_svc_help_path_t *help_path, uint32_t source_tick,
    uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidate_out)
{
    worr_event_record_v1 candidate;
    worr_event_payload_effect_v1 payload;

    if (!help_path || !candidate_out || max_entities == 0) {
        return WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }

    memset(&candidate, 0, sizeof(candidate));
    memset(&payload, 0, sizeof(payload));
    payload.effect_id = WORR_EVENT_EFFECT_HELP_PATH_MARKER;
    payload.variant = help_path->start
                          ? WORR_EVENT_HELP_PATH_VARIANT_START
                          : WORR_EVENT_HELP_PATH_VARIANT_CONTINUE;
    memcpy(payload.origin, help_path->pos, sizeof(payload.origin));
    memcpy(payload.direction, help_path->dir, sizeof(payload.direction));

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
    candidate.event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
    candidate.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    candidate.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate.expiry_tick = source_tick + 1u;
    candidate.payload_kind = WORR_EVENT_PAYLOAD_EFFECT;
    candidate.payload_size = sizeof(payload);
    memcpy(candidate.payload, &payload, sizeof(payload));

    if (!Worr_EventRecordCandidateValidateV1(&candidate, max_entities)) {
        return WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_RECORD;
    }

    *candidate_out = candidate;
    return WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_OK;
}
