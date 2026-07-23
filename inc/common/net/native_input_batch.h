/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_codec.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Private, default-off FR-10-T16 command-batch payload.  This is an
 * application schema carried by a COMMAND WNE record; it is deliberately not
 * a public capability bit and does not change the nested WNC1 command ABI.
 */
#define WORR_NATIVE_INPUT_BATCH_ABI_VERSION 1u
#define WORR_NATIVE_INPUT_BATCH_WIRE_VERSION 1u
#define WORR_NATIVE_INPUT_BATCH_RECORD_SCHEMA 2u
#define WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES 32u
#define WORR_NATIVE_INPUT_BATCH_MIN_COMMANDS 2u
#define WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS 8u
#define WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES 110u
#define WORR_NATIVE_INPUT_BATCH_MAX_PAYLOAD_BYTES                         \
    (WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES +                         \
     WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS *                              \
         WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES)

typedef enum worr_native_input_batch_result_v1_e {
    WORR_NATIVE_INPUT_BATCH_OK = 0,
    WORR_NATIVE_INPUT_BATCH_INVALID_ARGUMENT = 1,
    WORR_NATIVE_INPUT_BATCH_INVALID_RECORD = 2,
    WORR_NATIVE_INPUT_BATCH_OUTPUT_TOO_SMALL = 3,
    WORR_NATIVE_INPUT_BATCH_MALFORMED = 4,
    WORR_NATIVE_INPUT_BATCH_UNSUPPORTED = 5,
    WORR_NATIVE_INPUT_BATCH_LIMIT = 6,
    WORR_NATIVE_INPUT_BATCH_CORRUPT = 7,
    WORR_NATIVE_INPUT_BATCH_IDENTITY_MISMATCH = 8,
    WORR_NATIVE_INPUT_BATCH_OUTPUT_OVERLAP = 9,
} worr_native_input_batch_result_v1;

/* Pointer-free inspection result for one complete WNB1 payload. */
typedef struct worr_native_input_batch_info_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t wire_header_bytes;
    uint16_t command_count;
    uint16_t flags;
    uint32_t command_epoch;
    uint32_t first_sequence;
    uint32_t last_sequence;
    uint16_t command_record_bytes;
    uint16_t reserved0;
    uint32_t payload_crc32;
    uint32_t encoded_bytes;
} worr_native_input_batch_info_v1;

/* Returns the exact payload size for a valid count, or zero. */
uint32_t Worr_NativeInputBatchWireBytesV1(uint32_t command_count);

/* Inspect validates framing, length, reserved fields, range arithmetic and
 * the complete-payload CRC, but does not decode nested WNC1 records. */
worr_native_input_batch_result_v1 Worr_NativeInputBatchInspectV1(
    const void *encoded,
    size_t encoded_bytes,
    worr_native_input_batch_info_v1 *info_out);

/* Encode/decode are all-or-nothing.  Every command is an independently valid
 * WNC1 record in one epoch and identities must be exactly contiguous. */
worr_native_input_batch_result_v1 Worr_NativeInputBatchEncodeV1(
    const worr_command_record_v1 *records,
    uint32_t command_count,
    uint16_t max_duration_ms,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out,
    worr_native_input_batch_info_v1 *info_out);

worr_native_input_batch_result_v1 Worr_NativeInputBatchDecodeV1(
    const void *encoded,
    size_t encoded_bytes,
    uint16_t max_duration_ms,
    worr_command_record_v1 *records_out,
    uint32_t record_capacity,
    worr_native_input_batch_info_v1 *info_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_INPUT_BATCH_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_INPUT_BATCH_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_INPUT_BATCH_STATIC_ASSERT(
    WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES ==
        WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
            WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES,
    "WNB1 nested WNC1 size changed");
WORR_NATIVE_INPUT_BATCH_STATIC_ASSERT(
    WORR_NATIVE_INPUT_BATCH_MAX_PAYLOAD_BYTES == 912u,
    "WNB1 maximum payload size changed");
WORR_NATIVE_INPUT_BATCH_STATIC_ASSERT(
    sizeof(worr_native_input_batch_info_v1) == 36,
    "native input batch info v1 layout changed");
WORR_NATIVE_INPUT_BATCH_STATIC_ASSERT(
    offsetof(worr_native_input_batch_info_v1, payload_crc32) == 28,
    "native input batch CRC offset changed");

#undef WORR_NATIVE_INPUT_BATCH_STATIC_ASSERT
