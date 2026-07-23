/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_input_batch.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
    WNB_MAGIC_OFFSET = 0,
    WNB_VERSION_OFFSET = 4,
    WNB_HEADER_BYTES_OFFSET = 6,
    WNB_COMMAND_COUNT_OFFSET = 8,
    WNB_FLAGS_OFFSET = 10,
    WNB_COMMAND_EPOCH_OFFSET = 12,
    WNB_FIRST_SEQUENCE_OFFSET = 16,
    WNB_LAST_SEQUENCE_OFFSET = 20,
    WNB_COMMAND_BYTES_OFFSET = 24,
    WNB_RESERVED_OFFSET = 26,
    WNB_CRC_OFFSET = 28,
};

static const uint8_t wnb_magic[4] = {'W', 'N', 'B', '1'};

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
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                                (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc;
}

static uint32_t payload_crc32(const uint8_t *bytes, size_t count)
{
    static const uint8_t zero_crc[4] = {0, 0, 0, 0};
    uint32_t crc = UINT32_MAX;
    crc = crc32_update(crc, bytes, WNB_CRC_OFFSET);
    crc = crc32_update(crc, zero_crc, sizeof(zero_crc));
    crc = crc32_update(crc, bytes + WNB_CRC_OFFSET + sizeof(uint32_t),
                       count - WNB_CRC_OFFSET - sizeof(uint32_t));
    return ~crc;
}

static bool ranges_overlap(const void *a, size_t a_size,
                           const void *b, size_t b_size)
{
    uintptr_t a_begin;
    uintptr_t b_begin;
    uintptr_t a_end;
    uintptr_t b_end;
    if (!a || !b || a_size == 0 || b_size == 0)
        return false;
    a_begin = (uintptr_t)a;
    b_begin = (uintptr_t)b;
    if (a_size > UINTPTR_MAX - a_begin || b_size > UINTPTR_MAX - b_begin)
        return true;
    a_end = a_begin + a_size;
    b_end = b_begin + b_size;
    return a_begin < b_end && b_begin < a_end;
}

uint32_t Worr_NativeInputBatchWireBytesV1(uint32_t command_count)
{
    if (command_count < WORR_NATIVE_INPUT_BATCH_MIN_COMMANDS ||
        command_count > WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS)
        return 0;
    return WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES +
           command_count * WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES;
}

worr_native_input_batch_result_v1 Worr_NativeInputBatchInspectV1(
    const void *encoded, size_t encoded_bytes,
    worr_native_input_batch_info_v1 *info_out)
{
    const uint8_t *bytes = (const uint8_t *)encoded;
    worr_native_input_batch_info_v1 info;
    uint32_t expected_bytes;
    uint32_t stored_crc;

    if (!encoded || !info_out ||
        ranges_overlap(encoded, encoded_bytes, info_out, sizeof(*info_out)))
        return WORR_NATIVE_INPUT_BATCH_INVALID_ARGUMENT;
    if (encoded_bytes < WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES)
        return WORR_NATIVE_INPUT_BATCH_MALFORMED;
    if (memcmp(bytes + WNB_MAGIC_OFFSET, wnb_magic, sizeof(wnb_magic)) != 0)
        return WORR_NATIVE_INPUT_BATCH_MALFORMED;
    if (read_u16_le(bytes + WNB_VERSION_OFFSET) !=
            WORR_NATIVE_INPUT_BATCH_WIRE_VERSION ||
        read_u16_le(bytes + WNB_HEADER_BYTES_OFFSET) !=
            WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES)
        return WORR_NATIVE_INPUT_BATCH_UNSUPPORTED;

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.schema_version = WORR_NATIVE_INPUT_BATCH_ABI_VERSION;
    info.wire_header_bytes =
        read_u16_le(bytes + WNB_HEADER_BYTES_OFFSET);
    info.command_count = read_u16_le(bytes + WNB_COMMAND_COUNT_OFFSET);
    info.flags = read_u16_le(bytes + WNB_FLAGS_OFFSET);
    info.command_epoch = read_u32_le(bytes + WNB_COMMAND_EPOCH_OFFSET);
    info.first_sequence = read_u32_le(bytes + WNB_FIRST_SEQUENCE_OFFSET);
    info.last_sequence = read_u32_le(bytes + WNB_LAST_SEQUENCE_OFFSET);
    info.command_record_bytes = read_u16_le(bytes + WNB_COMMAND_BYTES_OFFSET);
    info.reserved0 = read_u16_le(bytes + WNB_RESERVED_OFFSET);
    info.payload_crc32 = read_u32_le(bytes + WNB_CRC_OFFSET);
    if (info.command_count < WORR_NATIVE_INPUT_BATCH_MIN_COMMANDS ||
        info.command_count > WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS)
        return WORR_NATIVE_INPUT_BATCH_LIMIT;
    expected_bytes = Worr_NativeInputBatchWireBytesV1(info.command_count);
    if (expected_bytes == 0 || encoded_bytes != expected_bytes)
        return WORR_NATIVE_INPUT_BATCH_MALFORMED;
    if (info.flags != 0 || info.reserved0 != 0 ||
        info.command_record_bytes != WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES ||
        info.command_epoch == 0 || info.first_sequence == 0 ||
        info.last_sequence < info.first_sequence ||
        info.command_count - 1u > UINT32_MAX - info.first_sequence ||
        info.last_sequence !=
            info.first_sequence + info.command_count - 1u)
        return WORR_NATIVE_INPUT_BATCH_MALFORMED;
    stored_crc = info.payload_crc32;
    if (stored_crc != payload_crc32(bytes, encoded_bytes))
        return WORR_NATIVE_INPUT_BATCH_CORRUPT;
    info.encoded_bytes = expected_bytes;
    *info_out = info;
    return WORR_NATIVE_INPUT_BATCH_OK;
}

worr_native_input_batch_result_v1 Worr_NativeInputBatchEncodeV1(
    const worr_command_record_v1 *records, uint32_t command_count,
    uint16_t max_duration_ms, void *encoded_out, size_t encoded_capacity,
    size_t *encoded_bytes_out, worr_native_input_batch_info_v1 *info_out)
{
    uint8_t staged[WORR_NATIVE_INPUT_BATCH_MAX_PAYLOAD_BYTES];
    worr_native_input_batch_info_v1 info;
    uint32_t encoded_bytes;
    uint32_t index;

    if (!records || !encoded_out || !encoded_bytes_out || !info_out)
        return WORR_NATIVE_INPUT_BATCH_INVALID_ARGUMENT;
    encoded_bytes = Worr_NativeInputBatchWireBytesV1(command_count);
    if (encoded_bytes == 0)
        return WORR_NATIVE_INPUT_BATCH_LIMIT;
    if (encoded_capacity < encoded_bytes)
        return WORR_NATIVE_INPUT_BATCH_OUTPUT_TOO_SMALL;
    if (ranges_overlap(records, command_count * sizeof(*records),
                       encoded_out, encoded_bytes) ||
        ranges_overlap(records, command_count * sizeof(*records),
                       encoded_bytes_out, sizeof(*encoded_bytes_out)) ||
        ranges_overlap(records, command_count * sizeof(*records),
                       info_out, sizeof(*info_out)) ||
        ranges_overlap(encoded_out, encoded_bytes, encoded_bytes_out,
                       sizeof(*encoded_bytes_out)) ||
        ranges_overlap(encoded_out, encoded_bytes, info_out,
                       sizeof(*info_out)) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       info_out, sizeof(*info_out)))
        return WORR_NATIVE_INPUT_BATCH_OUTPUT_OVERLAP;

    if (records[0].command_id.epoch == 0 ||
        records[0].command_id.sequence == 0)
        return WORR_NATIVE_INPUT_BATCH_INVALID_RECORD;
    memset(staged, 0, encoded_bytes);
    memcpy(staged + WNB_MAGIC_OFFSET, wnb_magic, sizeof(wnb_magic));
    write_u16_le(staged + WNB_VERSION_OFFSET,
                 WORR_NATIVE_INPUT_BATCH_WIRE_VERSION);
    write_u16_le(staged + WNB_HEADER_BYTES_OFFSET,
                 WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES);
    write_u16_le(staged + WNB_COMMAND_COUNT_OFFSET,
                 (uint16_t)command_count);
    write_u32_le(staged + WNB_COMMAND_EPOCH_OFFSET,
                 records[0].command_id.epoch);
    write_u32_le(staged + WNB_FIRST_SEQUENCE_OFFSET,
                 records[0].command_id.sequence);
    if (command_count - 1u > UINT32_MAX - records[0].command_id.sequence)
        return WORR_NATIVE_INPUT_BATCH_INVALID_RECORD;
    write_u32_le(staged + WNB_LAST_SEQUENCE_OFFSET,
                 records[0].command_id.sequence + command_count - 1u);
    write_u16_le(staged + WNB_COMMAND_BYTES_OFFSET,
                 WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES);

    for (index = 0; index < command_count; ++index) {
        size_t nested_bytes = 0;
        if (records[index].command_id.epoch != records[0].command_id.epoch ||
            records[index].command_id.sequence !=
                records[0].command_id.sequence + index ||
            (index != 0 && records[index].sample_time_us <
                               records[index - 1u].sample_time_us) ||
            Worr_NativeCodecCommandEncodeV1(
                &records[index], max_duration_ms,
                staged + WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES +
                    index * WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES,
                WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES, &nested_bytes) !=
                WORR_NATIVE_CODEC_OK ||
            nested_bytes != WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES)
            return WORR_NATIVE_INPUT_BATCH_INVALID_RECORD;
    }
    write_u32_le(staged + WNB_CRC_OFFSET,
                 payload_crc32(staged, encoded_bytes));
    if (Worr_NativeInputBatchInspectV1(staged, encoded_bytes, &info) !=
        WORR_NATIVE_INPUT_BATCH_OK)
        return WORR_NATIVE_INPUT_BATCH_INVALID_RECORD;
    memcpy(encoded_out, staged, encoded_bytes);
    *encoded_bytes_out = encoded_bytes;
    *info_out = info;
    return WORR_NATIVE_INPUT_BATCH_OK;
}

worr_native_input_batch_result_v1 Worr_NativeInputBatchDecodeV1(
    const void *encoded, size_t encoded_bytes, uint16_t max_duration_ms,
    worr_command_record_v1 *records_out, uint32_t record_capacity,
    worr_native_input_batch_info_v1 *info_out)
{
    worr_command_record_v1 staged[WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS];
    worr_native_input_batch_info_v1 info;
    const uint8_t *bytes = (const uint8_t *)encoded;
    size_t records_bytes;
    uint32_t index;
    worr_native_input_batch_result_v1 result;

    if (!encoded || !records_out || !info_out)
        return WORR_NATIVE_INPUT_BATCH_INVALID_ARGUMENT;
    if (ranges_overlap(encoded, encoded_bytes, info_out, sizeof(*info_out)))
        return WORR_NATIVE_INPUT_BATCH_OUTPUT_OVERLAP;
    result = Worr_NativeInputBatchInspectV1(encoded, encoded_bytes, &info);
    if (result != WORR_NATIVE_INPUT_BATCH_OK)
        return result;
    if (record_capacity < info.command_count)
        return WORR_NATIVE_INPUT_BATCH_OUTPUT_TOO_SMALL;
    /* Inspect bounds command_count to 2..8 before this multiplication.  Never
     * derive an address span from the caller's untrusted uint32_t capacity:
     * that can wrap size_t on a 32-bit build and weaken the overlap gate. */
    records_bytes = (size_t)info.command_count * sizeof(*records_out);
    if (ranges_overlap(encoded, encoded_bytes, records_out, records_bytes) ||
        ranges_overlap(records_out, records_bytes,
                       info_out, sizeof(*info_out)))
        return WORR_NATIVE_INPUT_BATCH_OUTPUT_OVERLAP;

    memset(staged, 0, sizeof(staged));
    for (index = 0; index < info.command_count; ++index) {
        if (Worr_NativeCodecCommandDecodeV1(
                bytes + WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES +
                    index * WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES,
                WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES, max_duration_ms,
                &staged[index]) != WORR_NATIVE_CODEC_OK)
            return WORR_NATIVE_INPUT_BATCH_CORRUPT;
        if (staged[index].command_id.epoch != info.command_epoch ||
            staged[index].command_id.sequence != info.first_sequence + index ||
            (index != 0 && staged[index].sample_time_us <
                               staged[index - 1u].sample_time_us))
            return WORR_NATIVE_INPUT_BATCH_IDENTITY_MISMATCH;
    }
    memcpy(records_out, staged, records_bytes);
    *info_out = info;
    return WORR_NATIVE_INPUT_BATCH_OK;
}
