/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/command_abi.h"
#include "shared/snapshot_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_CGAME_PREDICTION_INPUT_IMPORT_V1 \
    "WORR_CGAME_PREDICTION_INPUT_IMPORT_V1"
#define WORR_CGAME_PREDICTION_INPUT_API_VERSION 1u

/* Keep the module ABI independent of the engine's CMD_BACKUP macro while
 * retaining every command the current engine can replay. */
#define WORR_CGAME_PREDICTION_INPUT_CAPACITY 128u

typedef enum worr_cgame_prediction_input_source_v1_e {
    WORR_CGAME_PREDICTION_INPUT_SOURCE_NONE = 0,
    WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR = 1,
    WORR_CGAME_PREDICTION_INPUT_SOURCE_LEGACY_PACKET_ACK = 2,
    /* Negotiated carrier is live but the server has not consumed command 1. */
    WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_BOOTSTRAP = 3,
} worr_cgame_prediction_input_source_v1;

typedef enum worr_cgame_prediction_input_result_v1_e {
    WORR_CGAME_PREDICTION_INPUT_OK = 0,
    WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT = 1,
    WORR_CGAME_PREDICTION_INPUT_INVALID_METADATA = 2,
    WORR_CGAME_PREDICTION_INPUT_CANONICAL_METADATA_REQUIRED = 3,
    WORR_CGAME_PREDICTION_INPUT_IDENTITY_EPOCH_MISMATCH = 4,
    WORR_CGAME_PREDICTION_INPUT_HISTORY_MISSING = 5,
    WORR_CGAME_PREDICTION_INPUT_HISTORY_AMBIGUOUS = 6,
    WORR_CGAME_PREDICTION_INPUT_IDENTITY_DISCONTINUITY = 7,
    WORR_CGAME_PREDICTION_INPUT_RANGE_EXHAUSTED = 8,
    WORR_CGAME_PREDICTION_INPUT_COMMAND_INVALID = 9,
    /* The cgame rejected a locally resolved command while replaying it. */
    WORR_CGAME_PREDICTION_INPUT_REPLAY_REJECTED = 10,
    /* No matching retained movement prediction exists for authority. */
    WORR_CGAME_PREDICTION_INPUT_RETAINED_STATE_MISSING = 11,
    /* The movement configuration changed across the retained prediction. */
    WORR_CGAME_PREDICTION_INPUT_CONFIG_DISCONTINUITY = 12,
} worr_cgame_prediction_input_result_v1;

enum {
    WORR_CGAME_PREDICTION_INPUT_CANONICAL = UINT32_C(1) << 0,
    WORR_CGAME_PREDICTION_INPUT_LEGACY_FALLBACK = UINT32_C(1) << 1,
    WORR_CGAME_PREDICTION_INPUT_HAS_PENDING = UINT32_C(1) << 2,
    WORR_CGAME_PREDICTION_INPUT_HARD_RESYNC_REQUIRED = UINT32_C(1) << 3,
    WORR_CGAME_PREDICTION_INPUT_CANONICAL_BOOTSTRAP = UINT32_C(1) << 4,
};

/* A command is copied by value.  The legacy sequence remains the bounded
 * client-history address; command_id is authoritative only in canonical
 * mode and is the absent ID in legacy fallback mode. */
typedef struct worr_cgame_prediction_input_command_v1_s {
    uint32_t legacy_sequence;
    uint32_t reserved0;
    worr_command_id_v1 command_id;
    worr_prediction_command_v1 command;
} worr_cgame_prediction_input_command_v1;

/*
 * Immutable result for one prediction invocation.  commands contains only
 * finalized successors of authoritative_legacy_sequence, in execution order.
 * pending_command is the not-yet-finalized local sample and never has a
 * canonical command ID.
 */
typedef struct worr_cgame_prediction_input_range_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t result;
    uint32_t source;
    uint32_t flags;
    uint32_t reserved0;
    worr_snapshot_consumed_command_v2 consumed_command;
    uint32_t authoritative_legacy_sequence;
    uint32_t current_legacy_sequence;
    uint32_t command_count;
    uint32_t reserved1;
    worr_cgame_prediction_input_command_v1 pending_command;
    worr_cgame_prediction_input_command_v1
        commands[WORR_CGAME_PREDICTION_INPUT_CAPACITY];
} worr_cgame_prediction_input_range_v1;

typedef struct worr_cgame_prediction_input_import_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t (*ResolveInputRange)(
        worr_cgame_prediction_input_range_v1 *range_out);
} worr_cgame_prediction_input_import_v1;

/*
 * V2 lets cgame resolve replay from the exact consumed-command cursor carried
 * by an immutable canonical snapshot.  V1 remains the legacy/current-frame
 * convenience import; V2 never falls back to packet acknowledgement.
 */
#define WORR_CGAME_PREDICTION_INPUT_IMPORT_V2 \
    "WORR_CGAME_PREDICTION_INPUT_IMPORT_V2"
#define WORR_CGAME_PREDICTION_INPUT_API_VERSION_V2 2u

enum {
    WORR_CGAME_PREDICTION_INPUT_REQUEST_CANONICAL_REQUIRED =
        UINT32_C(1) << 0,
};

typedef struct worr_cgame_prediction_input_request_v2_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t flags;
    uint32_t reserved0;
    worr_snapshot_consumed_command_v2 consumed_command;
} worr_cgame_prediction_input_request_v2;

typedef struct worr_cgame_prediction_input_import_v2_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t (*ResolveInputRangeForCursor)(
        const worr_cgame_prediction_input_request_v2 *request,
        worr_cgame_prediction_input_range_v1 *range_out);
} worr_cgame_prediction_input_import_v2;

/*
 * Full canonical command records are exposed through a separate extension so
 * movement prediction V1 remains stable.  Only records finalized by the
 * engine's command-identity owner are returned; packet acknowledgement and
 * render timing are never reconstructed by cgame.
 */
#define WORR_CGAME_COMMAND_RECORD_IMPORT_V1 \
    "WORR_CGAME_COMMAND_RECORD_IMPORT_V1"
#define WORR_CGAME_COMMAND_RECORD_API_VERSION 1u

typedef enum worr_cgame_command_record_result_v1_e {
    WORR_CGAME_COMMAND_RECORD_OK = 0,
    WORR_CGAME_COMMAND_RECORD_INVALID_ARGUMENT = 1,
    WORR_CGAME_COMMAND_RECORD_HISTORY_MISSING = 2,
    WORR_CGAME_COMMAND_RECORD_RANGE_EXHAUSTED = 3,
} worr_cgame_command_record_result_v1;

enum {
    WORR_CGAME_COMMAND_RECORD_CANONICAL = UINT32_C(1) << 0,
};

typedef struct worr_cgame_command_record_entry_v1_s {
    uint32_t legacy_sequence;
    uint32_t reserved0;
    worr_command_record_v1 command;
} worr_cgame_command_record_entry_v1;

typedef struct worr_cgame_command_record_range_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t result;
    uint32_t flags;
    uint32_t first_legacy_sequence;
    uint32_t command_count;
    worr_cgame_command_record_entry_v1
        commands[WORR_CGAME_PREDICTION_INPUT_CAPACITY];
} worr_cgame_command_record_range_v1;

typedef struct worr_cgame_command_record_import_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t (*ResolveCanonicalCommandRange)(
        uint32_t first_legacy_sequence, uint32_t command_count,
        worr_cgame_command_record_range_v1 *range_out);
} worr_cgame_command_record_import_v1;

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_CGAME_PREDICTION_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_CGAME_PREDICTION_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_CGAME_PREDICTION_STATIC_ASSERT(
    sizeof(worr_cgame_prediction_input_command_v1) == 48,
    "cgame prediction input command v1 layout changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_prediction_input_command_v1, command_id) == 8,
    "cgame prediction input command ID offset changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_prediction_input_command_v1, command) == 16,
    "cgame prediction input payload offset changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    sizeof(worr_cgame_prediction_input_range_v1) == 6248,
    "cgame prediction input range v1 layout changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_prediction_input_range_v1, consumed_command) == 24,
    "cgame prediction input consumed cursor offset changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_prediction_input_range_v1, pending_command) == 56,
    "cgame prediction pending input offset changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_prediction_input_range_v1, commands) == 104,
    "cgame prediction finalized input offset changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_prediction_input_import_v1, ResolveInputRange) == 8,
    "cgame prediction input import header changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    sizeof(worr_cgame_prediction_input_request_v2) == 32,
    "cgame prediction input request v2 layout changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_prediction_input_request_v2, consumed_command) == 16,
    "cgame prediction input request cursor offset changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_prediction_input_import_v2,
             ResolveInputRangeForCursor) == 8,
    "cgame prediction input import v2 header changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    sizeof(worr_cgame_command_record_entry_v1) == 112,
    "cgame command record entry v1 layout changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_command_record_entry_v1, command) == 8,
    "cgame command record entry command offset changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    sizeof(worr_cgame_command_record_range_v1) == 14360,
    "cgame command record range v1 layout changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_command_record_range_v1, commands) == 24,
    "cgame command record range commands offset changed");
WORR_CGAME_PREDICTION_STATIC_ASSERT(
    offsetof(worr_cgame_command_record_import_v1,
             ResolveCanonicalCommandRange) == 8,
    "cgame command record import header changed");

#undef WORR_CGAME_PREDICTION_STATIC_ASSERT
