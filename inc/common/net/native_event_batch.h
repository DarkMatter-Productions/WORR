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
 * Bounded schema-2 EVENT payload.  The body concatenates independently valid
 * variable-length schema-1 WNC1 EVENT images.  The WNE object identity
 * for the batch is {stream_epoch, last_sequence}; this makes a committed
 * repeat prove the contiguous batch through the canonical event receipt.
 */
#define WORR_NATIVE_EVENT_BATCH_ABI_VERSION 1u
#define WORR_NATIVE_EVENT_BATCH_WIRE_VERSION 1u
#define WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA 2u
#define WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES 32u
#define WORR_NATIVE_EVENT_BATCH_MIN_EVENTS 2u
#define WORR_NATIVE_EVENT_BATCH_MAX_EVENTS 8u
#define WORR_NATIVE_EVENT_BATCH_MAX_EVENT_BYTES \
    WORR_NATIVE_CODEC_MAX_EVENT_ENCODED_BYTES
#define WORR_NATIVE_EVENT_BATCH_MAX_PAYLOAD_BYTES                       \
    (WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES +                       \
     WORR_NATIVE_EVENT_BATCH_MAX_EVENTS *                              \
         WORR_NATIVE_EVENT_BATCH_MAX_EVENT_BYTES)

typedef enum worr_native_event_batch_result_v1_e {
    WORR_NATIVE_EVENT_BATCH_OK = 0,
    WORR_NATIVE_EVENT_BATCH_INVALID_ARGUMENT = 1,
    WORR_NATIVE_EVENT_BATCH_INVALID_RECORD = 2,
    WORR_NATIVE_EVENT_BATCH_OUTPUT_TOO_SMALL = 3,
    WORR_NATIVE_EVENT_BATCH_MALFORMED = 4,
    WORR_NATIVE_EVENT_BATCH_UNSUPPORTED = 5,
    WORR_NATIVE_EVENT_BATCH_LIMIT = 6,
    WORR_NATIVE_EVENT_BATCH_CORRUPT = 7,
    WORR_NATIVE_EVENT_BATCH_IDENTITY_MISMATCH = 8,
    WORR_NATIVE_EVENT_BATCH_OUTPUT_OVERLAP = 9,
} worr_native_event_batch_result_v1;

typedef struct worr_native_event_batch_info_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t wire_header_bytes;
    uint16_t event_count;
    uint16_t flags;
    uint32_t stream_epoch;
    uint32_t first_sequence;
    uint32_t last_sequence;
    uint16_t nested_event_bytes;
    uint16_t reserved0;
    uint32_t payload_crc32;
    uint32_t encoded_bytes;
} worr_native_event_batch_info_v1;

uint32_t Worr_NativeEventBatchMaxWireBytesV1(uint32_t event_count);

/* Inspect validates framing, exact length, range arithmetic and whole-image
 * CRC.  Decode additionally validates every nested canonical event. */
worr_native_event_batch_result_v1 Worr_NativeEventBatchInspectV1(
    const void *encoded,
    size_t encoded_bytes,
    worr_native_event_batch_info_v1 *info_out);

/* Encode/decode are all-or-nothing.  Records must be authoritative,
 * contiguous, ordered, and share exact source_tick and source_time_us
 * producer coordinates.  WORR_EVENT_FLAG_SNAPSHOT_FENCED remains optional
 * per-record delivery metadata; batching neither requires nor normalizes it. */
worr_native_event_batch_result_v1 Worr_NativeEventBatchEncodeV1(
    const worr_event_record_v1 *records,
    uint32_t event_count,
    uint32_t max_entities,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out,
    worr_native_event_batch_info_v1 *info_out);

worr_native_event_batch_result_v1 Worr_NativeEventBatchDecodeV1(
    const void *encoded,
    size_t encoded_bytes,
    uint32_t max_entities,
    worr_event_record_v1 *records_out,
    uint32_t record_capacity,
    worr_native_event_batch_info_v1 *info_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_EVENT_BATCH_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_EVENT_BATCH_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_EVENT_BATCH_STATIC_ASSERT(
    WORR_NATIVE_EVENT_BATCH_MAX_EVENT_BYTES == 192u,
    "schema-1 nested EVENT maximum changed");
WORR_NATIVE_EVENT_BATCH_STATIC_ASSERT(
    WORR_NATIVE_EVENT_BATCH_MAX_PAYLOAD_BYTES == 1568u,
    "native event batch maximum changed");
WORR_NATIVE_EVENT_BATCH_STATIC_ASSERT(
    sizeof(worr_native_event_batch_info_v1) == 36,
    "native event batch info layout changed");
WORR_NATIVE_EVENT_BATCH_STATIC_ASSERT(
    offsetof(worr_native_event_batch_info_v1, payload_crc32) == 28,
    "native event batch CRC offset changed");

#undef WORR_NATIVE_EVENT_BATCH_STATIC_ASSERT
