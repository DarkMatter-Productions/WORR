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

typedef enum worr_legacy_help_path_event_candidate_result_v1_e {
    WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_OK = 0,
    WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_ARGUMENT = 1,
    WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_SHAPE = 2,
    WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_RECORD = 3,
} worr_legacy_help_path_event_candidate_result_v1;

/* Accepts exactly one complete rerelease engine-game svc_help_path carrier:
 * opcode, canonical boolean, three float coordinates, and one packed normal.
 * Truncated, trailing, non-finite, and invalid packed-direction input rejects
 * without changing caller output. */
worr_legacy_help_path_event_candidate_result_v1
Worr_LegacyHelpPathEventDecodeRawV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_help_path_t *help_path_out);

/* Creates one ID-less world-sourced help-marker effect. The implicit receiving
 * player remains absent until an exact per-client snapshot binder resolves it.
 * Legacy presentation remains authoritative. */
worr_legacy_help_path_event_candidate_result_v1
Worr_LegacyHelpPathEventCandidateBuildV1(
    const q2proto_svc_help_path_t *help_path, uint32_t source_tick,
    uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidate_out);

#ifdef __cplusplus
}
#endif
