/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
at your option) any later version.
*/

#pragma once

#include "shared/event_abi.h"
#include "q2proto/q2proto.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Converts the bounded rerelease svc_damage carrier into ID-less canonical
 * damage candidates. The legacy carrier identifies the receiving player only
 * implicitly, so the shared constructor uses a world source and leaves the
 * subject absent for an exact-snapshot binder to resolve. All output remains
 * untouched on failure. This adapter does not modify q2proto.
 */
typedef enum worr_legacy_damage_event_candidate_result_v1_e {
    WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_OK = 0,
    WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_ARGUMENT = 1,
    WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_SHAPE = 2,
    WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_RECORD = 3,
    WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_CAPACITY = 4,
} worr_legacy_damage_event_candidate_result_v1;

/* Accepts exactly one complete engine-game svc_damage message: opcode, count,
 * then one encoded-damage byte and one packed-direction byte per indicator.
 * Zero, excess, truncated, trailing, and invalid direction shapes reject. */
worr_legacy_damage_event_candidate_result_v1
Worr_LegacyDamageEventDecodeRawV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_damage_t *damage_out);

worr_legacy_damage_event_candidate_result_v1
Worr_LegacyDamageEventCandidatesBuildV1(
    const q2proto_svc_damage_t *damage, uint32_t source_tick,
    uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidates_out, uint32_t candidate_capacity,
    uint32_t *candidate_count_out);

#ifdef __cplusplus
}
#endif
