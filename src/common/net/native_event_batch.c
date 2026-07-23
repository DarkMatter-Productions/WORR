/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_event_batch.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
    WEB_MAGIC_OFFSET = 0,
    WEB_VERSION_OFFSET = 4,
    WEB_HEADER_BYTES_OFFSET = 6,
    WEB_EVENT_COUNT_OFFSET = 8,
    WEB_FLAGS_OFFSET = 10,
    WEB_STREAM_EPOCH_OFFSET = 12,
    WEB_FIRST_SEQUENCE_OFFSET = 16,
    WEB_LAST_SEQUENCE_OFFSET = 20,
    WEB_EVENT_BYTES_OFFSET = 24,
    WEB_RESERVED_OFFSET = 26,
    WEB_CRC_OFFSET = 28,
    WNC_ENCODED_BYTES_OFFSET = 16,
};

static const uint8_t web_magic[4] = {'W', 'E', 'B', '1'};

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes,
                             size_t count)
{
    size_t index;
    for (index = 0; index < count; ++index) {
        unsigned bit;
        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                                (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return crc;
}

static uint32_t payload_crc32(const uint8_t *bytes, size_t count)
{
    static const uint8_t zero_crc[4] = {0, 0, 0, 0};
    uint32_t crc = UINT32_MAX;
    crc = crc32_update(crc, bytes, WEB_CRC_OFFSET);
    crc = crc32_update(crc, zero_crc, sizeof(zero_crc));
    crc = crc32_update(crc, bytes + WEB_CRC_OFFSET + sizeof(uint32_t),
                       count - WEB_CRC_OFFSET - sizeof(uint32_t));
    return ~crc;
}

static bool ranges_overlap(const void *a, size_t a_size,
                           const void *b, size_t b_size)
{
    uintptr_t a_begin;
    uintptr_t b_begin;
    uintptr_t a_end;
    uintptr_t b_end;
    if (a == NULL || b == NULL || a_size == 0 || b_size == 0)
        return false;
    a_begin = (uintptr_t)a;
    b_begin = (uintptr_t)b;
    if (a_size > UINTPTR_MAX - a_begin || b_size > UINTPTR_MAX - b_begin)
        return true;
    a_end = a_begin + a_size;
    b_end = b_begin + b_size;
    return a_begin < b_end && b_begin < a_end;
}

uint32_t Worr_NativeEventBatchMaxWireBytesV1(uint32_t event_count)
{
    if (event_count < WORR_NATIVE_EVENT_BATCH_MIN_EVENTS ||
        event_count > WORR_NATIVE_EVENT_BATCH_MAX_EVENTS)
        return 0;
    return WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES +
           event_count * WORR_NATIVE_EVENT_BATCH_MAX_EVENT_BYTES;
}

worr_native_event_batch_result_v1 Worr_NativeEventBatchInspectV1(
    const void *encoded, size_t encoded_bytes,
    worr_native_event_batch_info_v1 *info_out)
{
    const uint8_t *bytes = (const uint8_t *)encoded;
    worr_native_event_batch_info_v1 info;
    uint32_t maximum_bytes;

    if (encoded == NULL || info_out == NULL ||
        ranges_overlap(encoded, encoded_bytes, info_out, sizeof(*info_out)))
        return WORR_NATIVE_EVENT_BATCH_INVALID_ARGUMENT;
    if (encoded_bytes < WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES)
        return WORR_NATIVE_EVENT_BATCH_MALFORMED;
    if (memcmp(bytes + WEB_MAGIC_OFFSET, web_magic, sizeof(web_magic)) != 0)
        return WORR_NATIVE_EVENT_BATCH_MALFORMED;
    if (read_u16_le(bytes + WEB_VERSION_OFFSET) !=
            WORR_NATIVE_EVENT_BATCH_WIRE_VERSION ||
        read_u16_le(bytes + WEB_HEADER_BYTES_OFFSET) !=
            WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES)
        return WORR_NATIVE_EVENT_BATCH_UNSUPPORTED;

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.schema_version = WORR_NATIVE_EVENT_BATCH_ABI_VERSION;
    info.wire_header_bytes = read_u16_le(bytes + WEB_HEADER_BYTES_OFFSET);
    info.event_count = read_u16_le(bytes + WEB_EVENT_COUNT_OFFSET);
    info.flags = read_u16_le(bytes + WEB_FLAGS_OFFSET);
    info.stream_epoch = read_u32_le(bytes + WEB_STREAM_EPOCH_OFFSET);
    info.first_sequence = read_u32_le(bytes + WEB_FIRST_SEQUENCE_OFFSET);
    info.last_sequence = read_u32_le(bytes + WEB_LAST_SEQUENCE_OFFSET);
    info.nested_event_bytes = read_u16_le(bytes + WEB_EVENT_BYTES_OFFSET);
    info.reserved0 = read_u16_le(bytes + WEB_RESERVED_OFFSET);
    info.payload_crc32 = read_u32_le(bytes + WEB_CRC_OFFSET);
    if (info.event_count < WORR_NATIVE_EVENT_BATCH_MIN_EVENTS ||
        info.event_count > WORR_NATIVE_EVENT_BATCH_MAX_EVENTS)
        return WORR_NATIVE_EVENT_BATCH_LIMIT;
    maximum_bytes = Worr_NativeEventBatchMaxWireBytesV1(info.event_count);
    if (maximum_bytes == 0 ||
        info.nested_event_bytes <
            info.event_count *
                (WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                 WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES) ||
        info.nested_event_bytes >
            info.event_count * WORR_NATIVE_EVENT_BATCH_MAX_EVENT_BYTES ||
        encoded_bytes != WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES +
                             info.nested_event_bytes ||
        encoded_bytes > maximum_bytes)
        return WORR_NATIVE_EVENT_BATCH_MALFORMED;
    if (info.flags != 0 || info.reserved0 != 0 ||
        info.stream_epoch == 0 || info.first_sequence == 0 ||
        info.event_count - 1u > UINT32_MAX - info.first_sequence ||
        info.last_sequence != info.first_sequence + info.event_count - 1u)
        return WORR_NATIVE_EVENT_BATCH_MALFORMED;
    if (info.payload_crc32 != payload_crc32(bytes, encoded_bytes))
        return WORR_NATIVE_EVENT_BATCH_CORRUPT;
    info.encoded_bytes = (uint32_t)encoded_bytes;
    *info_out = info;
    return WORR_NATIVE_EVENT_BATCH_OK;
}

worr_native_event_batch_result_v1 Worr_NativeEventBatchEncodeV1(
    const worr_event_record_v1 *records, uint32_t event_count,
    uint32_t max_entities, void *encoded_out, size_t encoded_capacity,
    size_t *encoded_bytes_out, worr_native_event_batch_info_v1 *info_out)
{
    uint8_t staged[WORR_NATIVE_EVENT_BATCH_MAX_PAYLOAD_BYTES];
    worr_native_event_batch_info_v1 info;
    uint32_t encoded_bytes;
    uint32_t nested_event_bytes = 0;
    uint32_t index;
    uint8_t *cursor;

    if (records == NULL || encoded_out == NULL || encoded_bytes_out == NULL ||
        info_out == NULL || max_entities == 0)
        return WORR_NATIVE_EVENT_BATCH_INVALID_ARGUMENT;
    encoded_bytes = Worr_NativeEventBatchMaxWireBytesV1(event_count);
    if (encoded_bytes == 0)
        return WORR_NATIVE_EVENT_BATCH_LIMIT;
    if (records[0].event_id.stream_epoch == 0 ||
        records[0].event_id.sequence == 0 ||
        event_count - 1u > UINT32_MAX - records[0].event_id.sequence)
        return WORR_NATIVE_EVENT_BATCH_INVALID_RECORD;
    for (index = 0; index < event_count; ++index) {
        uint32_t nested_bytes = 0;
        if (records[index].event_id.stream_epoch !=
                records[0].event_id.stream_epoch ||
            records[index].event_id.sequence !=
                records[0].event_id.sequence + index ||
            records[index].source_tick != records[0].source_tick ||
            records[index].source_time_us != records[0].source_time_us ||
            Worr_NativeCodecEventPreflightV1(
                &records[index], max_entities, &nested_bytes) !=
                WORR_NATIVE_CODEC_OK ||
            nested_bytes > WORR_NATIVE_EVENT_BATCH_MAX_EVENT_BYTES ||
            nested_event_bytes > UINT16_MAX - nested_bytes)
            return WORR_NATIVE_EVENT_BATCH_INVALID_RECORD;
        nested_event_bytes += nested_bytes;
    }
    encoded_bytes = WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES +
                    nested_event_bytes;
    if (encoded_capacity < encoded_bytes)
        return WORR_NATIVE_EVENT_BATCH_OUTPUT_TOO_SMALL;
    if (ranges_overlap(records, (size_t)event_count * sizeof(*records),
                       encoded_out, encoded_bytes) ||
        ranges_overlap(records, (size_t)event_count * sizeof(*records),
                       encoded_bytes_out, sizeof(*encoded_bytes_out)) ||
        ranges_overlap(records, (size_t)event_count * sizeof(*records),
                       info_out, sizeof(*info_out)) ||
        ranges_overlap(encoded_out, encoded_bytes, encoded_bytes_out,
                       sizeof(*encoded_bytes_out)) ||
        ranges_overlap(encoded_out, encoded_bytes, info_out,
                       sizeof(*info_out)) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       info_out, sizeof(*info_out)))
        return WORR_NATIVE_EVENT_BATCH_OUTPUT_OVERLAP;
    memset(staged, 0, encoded_bytes);
    memcpy(staged + WEB_MAGIC_OFFSET, web_magic, sizeof(web_magic));
    write_u16_le(staged + WEB_VERSION_OFFSET,
                 WORR_NATIVE_EVENT_BATCH_WIRE_VERSION);
    write_u16_le(staged + WEB_HEADER_BYTES_OFFSET,
                 WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES);
    write_u16_le(staged + WEB_EVENT_COUNT_OFFSET, (uint16_t)event_count);
    write_u32_le(staged + WEB_STREAM_EPOCH_OFFSET,
                 records[0].event_id.stream_epoch);
    write_u32_le(staged + WEB_FIRST_SEQUENCE_OFFSET,
                 records[0].event_id.sequence);
    write_u32_le(staged + WEB_LAST_SEQUENCE_OFFSET,
                 records[0].event_id.sequence + event_count - 1u);
    write_u16_le(staged + WEB_EVENT_BYTES_OFFSET,
                 (uint16_t)nested_event_bytes);

    cursor = staged + WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES;
    for (index = 0; index < event_count; ++index) {
        size_t nested_bytes = 0;
        if (Worr_NativeCodecEventEncodeV1(
                &records[index], max_entities, cursor,
                WORR_NATIVE_EVENT_BATCH_MAX_EVENT_BYTES, &nested_bytes) !=
                WORR_NATIVE_CODEC_OK ||
            nested_bytes > WORR_NATIVE_EVENT_BATCH_MAX_EVENT_BYTES)
            return WORR_NATIVE_EVENT_BATCH_INVALID_RECORD;
        cursor += nested_bytes;
    }
    if ((size_t)(cursor - staged) != encoded_bytes)
        return WORR_NATIVE_EVENT_BATCH_INVALID_RECORD;
    write_u32_le(staged + WEB_CRC_OFFSET,
                 payload_crc32(staged, encoded_bytes));
    if (Worr_NativeEventBatchInspectV1(staged, encoded_bytes, &info) !=
        WORR_NATIVE_EVENT_BATCH_OK)
        return WORR_NATIVE_EVENT_BATCH_INVALID_RECORD;
    memcpy(encoded_out, staged, encoded_bytes);
    *encoded_bytes_out = encoded_bytes;
    *info_out = info;
    return WORR_NATIVE_EVENT_BATCH_OK;
}

worr_native_event_batch_result_v1 Worr_NativeEventBatchDecodeV1(
    const void *encoded, size_t encoded_bytes, uint32_t max_entities,
    worr_event_record_v1 *records_out, uint32_t record_capacity,
    worr_native_event_batch_info_v1 *info_out)
{
    worr_event_record_v1 staged[WORR_NATIVE_EVENT_BATCH_MAX_EVENTS];
    worr_native_event_batch_info_v1 info;
    const uint8_t *bytes = (const uint8_t *)encoded;
    size_t records_bytes;
    uint32_t index;
    const uint8_t *cursor;
    const uint8_t *end;
    worr_native_event_batch_result_v1 result;

    if (encoded == NULL || records_out == NULL || info_out == NULL ||
        max_entities == 0)
        return WORR_NATIVE_EVENT_BATCH_INVALID_ARGUMENT;
    if (ranges_overlap(encoded, encoded_bytes, info_out, sizeof(*info_out)))
        return WORR_NATIVE_EVENT_BATCH_OUTPUT_OVERLAP;
    result = Worr_NativeEventBatchInspectV1(encoded, encoded_bytes, &info);
    if (result != WORR_NATIVE_EVENT_BATCH_OK)
        return result;
    if (record_capacity < info.event_count)
        return WORR_NATIVE_EVENT_BATCH_OUTPUT_TOO_SMALL;
    records_bytes = (size_t)info.event_count * sizeof(*records_out);
    if (ranges_overlap(encoded, encoded_bytes, records_out, records_bytes) ||
        ranges_overlap(records_out, records_bytes, info_out,
                       sizeof(*info_out)))
        return WORR_NATIVE_EVENT_BATCH_OUTPUT_OVERLAP;

    memset(staged, 0, sizeof(staged));
    cursor = bytes + WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES;
    end = bytes + encoded_bytes;
    for (index = 0; index < info.event_count; ++index) {
        uint32_t nested_bytes;
        if ((size_t)(end - cursor) < WORR_NATIVE_CODEC_WIRE_HEADER_BYTES)
            return WORR_NATIVE_EVENT_BATCH_CORRUPT;
        nested_bytes = read_u32_le(cursor + WNC_ENCODED_BYTES_OFFSET);
        if (nested_bytes < WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                               WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES ||
            nested_bytes > WORR_NATIVE_EVENT_BATCH_MAX_EVENT_BYTES ||
            nested_bytes > (size_t)(end - cursor) ||
            Worr_NativeCodecEventDecodeV1(
                cursor, nested_bytes, max_entities, &staged[index]) !=
                WORR_NATIVE_CODEC_OK)
            return WORR_NATIVE_EVENT_BATCH_CORRUPT;
        if (staged[index].event_id.stream_epoch != info.stream_epoch ||
            staged[index].event_id.sequence != info.first_sequence + index ||
            staged[index].source_tick != staged[0].source_tick ||
            staged[index].source_time_us != staged[0].source_time_us)
            return WORR_NATIVE_EVENT_BATCH_IDENTITY_MISMATCH;
        cursor += nested_bytes;
    }
    if (cursor != end)
        return WORR_NATIVE_EVENT_BATCH_CORRUPT;
    memcpy(records_out, staged, records_bytes);
    *info_out = info;
    return WORR_NATIVE_EVENT_BATCH_OK;
}
