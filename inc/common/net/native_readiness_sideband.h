/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_readiness.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Isolated FR-10-T04 readiness-record carrier.  It does not advertise the
 * native capability or register a live SVC/CLC consumer.  Both directions use
 * the same signed-int16 index/value words.  SVC callers pass decoded int32
 * settings through ObserveSvcSettingV1, which rejects non-sign-extended
 * sideband values instead of truncating them.
 */
#define WORR_NATIVE_READINESS_SIDEBAND_VERSION 1u
#define WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT 15u

enum {
    WORR_NATIVE_READINESS_SETTING_BEGIN = -31980,
    WORR_NATIVE_READINESS_SETTING_KIND = -31979,
    WORR_NATIVE_READINESS_SETTING_EPOCH_LOW = -31978,
    WORR_NATIVE_READINESS_SETTING_EPOCH_HIGH = -31977,
    WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_LOW = -31976,
    WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_HIGH = -31975,
    WORR_NATIVE_READINESS_SETTING_CAPABILITIES_LOW = -31974,
    WORR_NATIVE_READINESS_SETTING_CAPABILITIES_HIGH = -31973,
    WORR_NATIVE_READINESS_SETTING_NONCE_WORD0 = -31972,
    WORR_NATIVE_READINESS_SETTING_NONCE_WORD1 = -31971,
    WORR_NATIVE_READINESS_SETTING_NONCE_WORD2 = -31970,
    WORR_NATIVE_READINESS_SETTING_NONCE_WORD3 = -31969,
    WORR_NATIVE_READINESS_SETTING_CHECKSUM_LOW = -31968,
    WORR_NATIVE_READINESS_SETTING_CHECKSUM_HIGH = -31967,
    WORR_NATIVE_READINESS_SETTING_COMMIT = -31966,
};

typedef struct worr_native_readiness_setting_pair_v1_s {
    int16_t index;
    int16_t value;
} worr_native_readiness_setting_pair_v1;

typedef enum worr_native_readiness_sideband_result_v1_e {
    WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED = 0,
    WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED = 1,
    WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND = 2,
    WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED = 3,
    WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED = 4,
    WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN = 5,
    WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY = 6,
    WORR_NATIVE_READINESS_SIDEBAND_RESET_INTERVENING_SERVICE = 7,
    WORR_NATIVE_READINESS_SIDEBAND_NO_RECORD = 8,
    WORR_NATIVE_READINESS_SIDEBAND_UNSUPPORTED_VERSION = 9,
    WORR_NATIVE_READINESS_SIDEBAND_UNEXPECTED_FIELD = 10,
    WORR_NATIVE_READINESS_SIDEBAND_VALUE_OUT_OF_RANGE = 11,
    WORR_NATIVE_READINESS_SIDEBAND_RECORD_INVALID = 12,
    WORR_NATIVE_READINESS_SIDEBAND_CHECKSUM_MISMATCH = 13,
    WORR_NATIVE_READINESS_SIDEBAND_COMMIT_MISMATCH = 14,
    WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT = 15,
    WORR_NATIVE_READINESS_SIDEBAND_INVALID_STATE = 16,
} worr_native_readiness_sideband_result_v1;

/* Every counter saturates at UINT64_MAX. */
typedef struct worr_native_readiness_sideband_telemetry_v1_s {
    uint64_t packet_begins;
    uint64_t packet_ends;
    uint64_t packet_boundary_resets;
    uint64_t settings_seen;
    uint64_t non_sideband_settings;
    uint64_t sideband_begins;
    uint64_t fields_accepted;
    uint64_t records_committed;
    uint64_t records_taken;
    uint64_t intervening_resets;
    uint64_t dangling_sequences;
    uint64_t discarded_records;
    uint64_t malformed_order;
    uint64_t unsupported_versions;
    uint64_t value_range_failures;
    uint64_t record_validation_failures;
    uint64_t checksum_failures;
    uint64_t commit_failures;
    uint64_t invalid_arguments;
    uint64_t invalid_state;
} worr_native_readiness_sideband_telemetry_v1;

enum {
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_IDLE = 0,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_KIND = 1,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_EPOCH_LOW = 2,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_EPOCH_HIGH = 3,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_SNAPSHOT_EPOCH_LOW = 4,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_SNAPSHOT_EPOCH_HIGH = 5,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_CAPABILITIES_LOW = 6,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_CAPABILITIES_HIGH = 7,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD0 = 8,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD1 = 9,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD2 = 10,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD3 = 11,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_CHECKSUM_LOW = 12,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_CHECKSUM_HIGH = 13,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_COMMIT = 14,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_RECORD = 15,
    WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED = 16,
};

/* Fixed-layout packet parser; it contains no pointers or external storage. */
typedef struct worr_native_readiness_sideband_parser_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t phase;
    uint16_t packet_active;
    uint16_t reserved0;
    uint32_t reserved1;
    worr_native_readiness_record_v1 pending_record;
    uint16_t checksum_low;
    uint16_t reserved2;
    uint32_t reserved3;
    worr_native_readiness_sideband_telemetry_v1 telemetry;
} worr_native_readiness_sideband_parser_v1;

/* Invalid input, insufficient capacity, and aliasing leave output untouched. */
bool Worr_NativeReadinessSidebandEncodeV1(
    const worr_native_readiness_record_v1 *record,
    worr_native_readiness_setting_pair_v1 *pairs_out,
    uint32_t pair_capacity);

bool Worr_NativeReadinessSidebandParserInitV1(
    worr_native_readiness_sideband_parser_v1 *parser_out);
bool Worr_NativeReadinessSidebandParserValidateV1(
    const worr_native_readiness_sideband_parser_v1 *parser);
worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandPacketBeginV1(
    worr_native_readiness_sideband_parser_v1 *parser);
worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandPacketEndV1(
    worr_native_readiness_sideband_parser_v1 *parser);

/* CLC settings already have this exact int16 representation. */
worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandObservePairV1(
    worr_native_readiness_sideband_parser_v1 *parser,
    int16_t index,
    int16_t value);

/*
 * SVC settings are decoded as int32.  Native indices require a value in the
 * signed-int16 range; non-native settings remain idle pass-through values.
 */
worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandObserveSvcSettingV1(
    worr_native_readiness_sideband_parser_v1 *parser,
    int32_t index,
    int32_t value);

/* Any non-setting service breaks a partial or unconsumed adjacent sequence. */
worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandObserveInterveningServiceV1(
    worr_native_readiness_sideband_parser_v1 *parser);

/*
 * A successful take copies the exact validated record and returns to IDLE so
 * another adjacent record may follow in the same packet.  Failure leaves the
 * output untouched.  Parser/output overlap is rejected without mutation.
 */
worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandTakeRecordV1(
    worr_native_readiness_sideband_parser_v1 *parser,
    worr_native_readiness_record_v1 *record_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_READINESS_SIDEBAND_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_READINESS_SIDEBAND_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_READINESS_SIDEBAND_STATIC_ASSERT(
    sizeof(worr_native_readiness_setting_pair_v1) == 4,
    "native readiness sideband pair V1 layout changed");
WORR_NATIVE_READINESS_SIDEBAND_STATIC_ASSERT(
    WORR_NATIVE_READINESS_SETTING_COMMIT -
            WORR_NATIVE_READINESS_SETTING_BEGIN + 1 ==
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT,
    "native readiness sideband setting range is not exactly adjacent");
WORR_NATIVE_READINESS_SIDEBAND_STATIC_ASSERT(
    sizeof(worr_native_readiness_sideband_telemetry_v1) == 160,
    "native readiness sideband telemetry V1 layout changed");
WORR_NATIVE_READINESS_SIDEBAND_STATIC_ASSERT(
    sizeof(worr_native_readiness_sideband_parser_v1) == 216,
    "native readiness sideband parser V1 layout changed");
WORR_NATIVE_READINESS_SIDEBAND_STATIC_ASSERT(
    offsetof(worr_native_readiness_sideband_parser_v1, pending_record) == 16,
    "native readiness sideband pending record offset changed");
WORR_NATIVE_READINESS_SIDEBAND_STATIC_ASSERT(
    offsetof(worr_native_readiness_sideband_parser_v1, telemetry) == 56,
    "native readiness sideband telemetry offset changed");

#undef WORR_NATIVE_READINESS_SIDEBAND_STATIC_ASSERT
