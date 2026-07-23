/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/event_abi.h"
#include "q2proto/q2proto.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_LEGACY_KEYED_POI_RAW_SIZE 21u

typedef enum worr_legacy_keyed_poi_event_candidate_result_v1_e {
    WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_OK = 0,
    WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_ARGUMENT = 1,
    WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_SHAPE = 2,
    WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_RECORD = 3,
} worr_legacy_keyed_poi_event_candidate_result_v1;

/* Accepts exactly one complete rerelease engine-game svc_poi carrier. A
 * canonical POI is always keyed; unkeyed, truncated, trailing, non-finite,
 * and unknown-flag forms reject without changing caller output. The carrier's
 * lifetime is preserved verbatim, including zero and the keyed-remove value;
 * delivery-context policy is applied by the server/client binder. */
worr_legacy_keyed_poi_event_candidate_result_v1
Worr_LegacyKeyedPOIEventDecodeRawV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_poi_t *poi_out);

/* Creates one ID-less RELIABLE_ORDERED keyed-POI action template. Raw/client
 * capture has no transport-reliability provenance, so an accepted unreliable
 * server binder explicitly rewrites delivery to TRANSIENT. The service has no
 * emitter identity: source is the stable world and subject remains absent
 * until exact-context binding. Removal fields are canonicalized. */
worr_legacy_keyed_poi_event_candidate_result_v1
Worr_LegacyKeyedPOIEventCandidateBuildV1(
    const q2proto_svc_poi_t *poi, uint32_t source_tick,
    uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidate_out);

#ifdef __cplusplus
}
#endif
