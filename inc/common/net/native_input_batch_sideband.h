/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_input_batch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_NATIVE_INPUT_BATCH_SIDEBAND_VERSION 1u
#define WORR_NATIVE_INPUT_BATCH_USERINFO_KEY "worr_input_batch"
#define WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT 7u
#define WORR_NATIVE_INPUT_BATCH_SIDEBAND_WIRE_BYTES \
    (WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT * 9u)

/* Private server-to-client confirmation.  This range is disjoint from public
 * confirmation (-31799..-31797), consumed cursor (-31790..-31786), demo clock
 * (-31780..-31775), readiness (-31980..-31966), and CLC command settings. */
enum {
    WORR_NATIVE_INPUT_BATCH_SETTING_BEGIN = -31770,
    WORR_NATIVE_INPUT_BATCH_SETTING_OFFICIAL_EPOCH = -31769,
    WORR_NATIVE_INPUT_BATCH_SETTING_TRANSPORT_EPOCH = -31768,
    WORR_NATIVE_INPUT_BATCH_SETTING_SCHEMA = -31767,
    WORR_NATIVE_INPUT_BATCH_SETTING_LIMITS = -31766,
    WORR_NATIVE_INPUT_BATCH_SETTING_CHECKSUM = -31765,
    WORR_NATIVE_INPUT_BATCH_SETTING_COMMIT = -31764,
};

typedef struct worr_native_input_batch_setting_pair_v1_s {
    int32_t index;
    int32_t value;
} worr_native_input_batch_setting_pair_v1;

typedef struct worr_native_input_batch_confirm_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t record_schema_version;
    uint16_t wire_version;
    uint16_t maximum_commands;
    uint32_t official_connection_epoch;
    uint32_t transport_epoch;
    uint32_t header_checksum;
    uint32_t reserved[2];
} worr_native_input_batch_confirm_v1;

typedef enum worr_native_input_batch_sideband_result_v1_e {
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_STARTED = 0,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_ENDED = 1,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_NOT_SIDEBAND = 2,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_FIELD_ACCEPTED = 3,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_RECORD_COMMITTED = 4,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_RECORD_TAKEN = 5,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_BOUNDARY = 6,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_INTERVENING_SERVICE = 7,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_UNSUPPORTED = 8,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_UNEXPECTED_FIELD = 9,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_CHECKSUM_MISMATCH = 10,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_COMMIT_MISMATCH = 11,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_ARGUMENT = 12,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_STATE = 13,
} worr_native_input_batch_sideband_result_v1;

enum {
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_IDLE = 0,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_OFFICIAL_EPOCH = 1,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_TRANSPORT_EPOCH = 2,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_SCHEMA = 3,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_LIMITS = 4,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_CHECKSUM = 5,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_COMMIT = 6,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_READY = 7,
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_POISONED = 8,
};

typedef struct worr_native_input_batch_sideband_parser_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t phase;
    uint32_t packet_active;
    uint32_t observed_checksum;
    worr_native_input_batch_confirm_v1 pending;
    uint32_t reserved[2];
} worr_native_input_batch_sideband_parser_v1;

bool Worr_NativeInputBatchSettingRecognizedV1(int32_t index);
bool Worr_NativeInputBatchConfirmInitV1(
    worr_native_input_batch_confirm_v1 *confirm_out,
    uint32_t official_connection_epoch,
    uint32_t transport_epoch);
bool Worr_NativeInputBatchConfirmValidateV1(
    const worr_native_input_batch_confirm_v1 *confirm);
bool Worr_NativeInputBatchConfirmEqualV1(
    const worr_native_input_batch_confirm_v1 *left,
    const worr_native_input_batch_confirm_v1 *right);
bool Worr_NativeInputBatchConfirmEncodeV1(
    const worr_native_input_batch_confirm_v1 *confirm,
    worr_native_input_batch_setting_pair_v1 *pairs,
    uint32_t pair_capacity);

bool Worr_NativeInputBatchSidebandParserInitV1(
    worr_native_input_batch_sideband_parser_v1 *parser_out);
bool Worr_NativeInputBatchSidebandParserValidateV1(
    const worr_native_input_batch_sideband_parser_v1 *parser);
worr_native_input_batch_sideband_result_v1
Worr_NativeInputBatchSidebandPacketBeginV1(
    worr_native_input_batch_sideband_parser_v1 *parser);
worr_native_input_batch_sideband_result_v1
Worr_NativeInputBatchSidebandPacketEndV1(
    worr_native_input_batch_sideband_parser_v1 *parser);
worr_native_input_batch_sideband_result_v1
Worr_NativeInputBatchSidebandObserveSettingV1(
    worr_native_input_batch_sideband_parser_v1 *parser,
    int32_t index,
    int32_t value);
worr_native_input_batch_sideband_result_v1
Worr_NativeInputBatchSidebandObserveInterveningServiceV1(
    worr_native_input_batch_sideband_parser_v1 *parser);
worr_native_input_batch_sideband_result_v1
Worr_NativeInputBatchSidebandTakeRecordV1(
    worr_native_input_batch_sideband_parser_v1 *parser,
    worr_native_input_batch_confirm_v1 *confirm_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_INPUT_BATCH_SIDEBAND_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_INPUT_BATCH_SIDEBAND_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_INPUT_BATCH_SIDEBAND_STATIC_ASSERT(
    WORR_NATIVE_INPUT_BATCH_SIDEBAND_WIRE_BYTES == 63u,
    "WNB1 private confirmation wire size changed");
WORR_NATIVE_INPUT_BATCH_SIDEBAND_STATIC_ASSERT(
    sizeof(worr_native_input_batch_setting_pair_v1) == 8,
    "WNB1 setting pair layout changed");
WORR_NATIVE_INPUT_BATCH_SIDEBAND_STATIC_ASSERT(
    sizeof(worr_native_input_batch_confirm_v1) == 32,
    "WNB1 confirmation layout changed");
WORR_NATIVE_INPUT_BATCH_SIDEBAND_STATIC_ASSERT(
    sizeof(worr_native_input_batch_sideband_parser_v1) == 56,
    "WNB1 confirmation parser layout changed");

#undef WORR_NATIVE_INPUT_BATCH_SIDEBAND_STATIC_ASSERT
