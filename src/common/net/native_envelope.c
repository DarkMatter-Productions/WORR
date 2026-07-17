/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/native_envelope.h"

#include <limits.h>
#include <string.h>

enum {
    WORR_NATIVE_WIRE_CRC_OFFSET = 52,
};

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes)
{
    const uintptr_t left_begin = (uintptr_t)left;
    const uintptr_t right_begin = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left == NULL || right == NULL || left_bytes == 0 || right_bytes == 0)
        return false;
    if (left_bytes > UINTPTR_MAX - left_begin ||
        right_bytes > UINTPTR_MAX - right_begin) {
        return true;
    }
    left_end = left_begin + left_bytes;
    right_end = right_begin + right_bytes;
    return left_begin < right_end && right_begin < left_end;
}

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
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

static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        unsigned bit;

        crc ^= bytes[i];
        for (bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }

    return crc;
}

static uint32_t crc32_bytes(const void *data, size_t count)
{
    uint32_t crc = UINT32_MAX;

    if (count != 0) {
        crc = crc32_update(crc, (const uint8_t *)data, count);
    }
    return ~crc;
}

static uint32_t datagram_crc32(const uint8_t *datagram, size_t count)
{
    static const uint8_t zero_crc[sizeof(uint32_t)] = { 0, 0, 0, 0 };
    uint32_t crc = UINT32_MAX;

    crc = crc32_update(crc, datagram, WORR_NATIVE_WIRE_CRC_OFFSET);
    crc = crc32_update(crc, zero_crc, sizeof(zero_crc));
    crc = crc32_update(
        crc,
        datagram + WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES,
        count - WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES);
    return ~crc;
}

static bool record_class_valid(uint8_t record_class)
{
    return record_class == WORR_NATIVE_RECORD_COMMAND_V1 ||
           record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
           record_class == WORR_NATIVE_RECORD_EVENT_V1 ||
           record_class ==
               WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1;
}

bool Worr_NativeEnvelopeRecordRefValidV1(
    worr_native_record_ref_v1 record)
{
    return record_class_valid(record.record_class) &&
           record.reserved0 == 0 &&
           record.record_schema_version != 0 &&
           record.object_epoch != 0 &&
           record.object_sequence != 0;
}

static bool record_ref_equal(worr_native_record_ref_v1 a,
                             worr_native_record_ref_v1 b)
{
    return a.record_class == b.record_class &&
           a.record_schema_version == b.record_schema_version &&
           a.object_epoch == b.object_epoch &&
           a.object_sequence == b.object_sequence;
}

static uint16_t fragment_count_for(uint32_t payload_bytes,
                                   uint16_t fragment_stride)
{
    const uint32_t quotient = payload_bytes / fragment_stride;
    const uint32_t remainder = payload_bytes % fragment_stride;
    const uint32_t count = quotient + (remainder != 0 ? 1u : 0u);

    if (count == 0 || count > UINT16_MAX) {
        return 0;
    }
    return (uint16_t)count;
}

bool Worr_NativeEnvelopeFragmenterInitV1(
    worr_native_envelope_fragmenter_v1 *fragmenter,
    uint32_t transport_epoch,
    uint32_t message_sequence,
    worr_native_record_ref_v1 record,
    uint8_t priority,
    const void *payload,
    uint32_t payload_bytes,
    uint16_t max_datagram_bytes)
{
    uint16_t fragment_stride;
    uint16_t fragment_count;

    if (fragmenter == NULL) {
        return false;
    }

    memset(fragmenter, 0, sizeof(*fragmenter));
    fragmenter->struct_size = sizeof(*fragmenter);
    fragmenter->schema_version = WORR_NATIVE_ENVELOPE_ABI_VERSION;

    if (transport_epoch == 0 || message_sequence == 0 ||
        !Worr_NativeEnvelopeRecordRefValidV1(record) ||
        priority > WORR_NATIVE_ENVELOPE_MAX_PRIORITY || payload == NULL ||
        payload_bytes == 0 ||
        payload_bytes > WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES ||
        max_datagram_bytes <= WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ||
        max_datagram_bytes > WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES) {
        return false;
    }

    fragment_stride = (uint16_t)(max_datagram_bytes -
                                 WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES);
    fragment_count = fragment_count_for(payload_bytes, fragment_stride);
    if (fragment_count == 0 ||
        fragment_count > WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS) {
        return false;
    }

    fragmenter->state_flags = WORR_NATIVE_FRAGMENTER_INITIALIZED;
    fragmenter->record = record;
    fragmenter->transport_epoch = transport_epoch;
    fragmenter->message_sequence = message_sequence;
    fragmenter->total_payload_bytes = payload_bytes;
    fragmenter->payload_crc32 = crc32_bytes(payload, payload_bytes);
    fragmenter->max_datagram_bytes = max_datagram_bytes;
    fragmenter->fragment_stride = fragment_stride;
    fragmenter->fragment_count = fragment_count;
    fragmenter->priority = priority;
    return true;
}

static bool fragmenter_state_valid(
    const worr_native_envelope_fragmenter_v1 *fragmenter)
{
    uint16_t expected_count;

    if (fragmenter->struct_size != sizeof(*fragmenter) ||
        fragmenter->schema_version != WORR_NATIVE_ENVELOPE_ABI_VERSION ||
        (fragmenter->state_flags &
         ~(WORR_NATIVE_FRAGMENTER_INITIALIZED |
           WORR_NATIVE_FRAGMENTER_EXHAUSTED)) != 0 ||
        (fragmenter->state_flags & WORR_NATIVE_FRAGMENTER_INITIALIZED) == 0 ||
        !Worr_NativeEnvelopeRecordRefValidV1(fragmenter->record) ||
        fragmenter->transport_epoch == 0 ||
        fragmenter->message_sequence == 0 ||
        fragmenter->total_payload_bytes == 0 ||
        fragmenter->total_payload_bytes >
            WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES ||
        fragmenter->max_datagram_bytes <=
            WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ||
        fragmenter->max_datagram_bytes >
            WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES ||
        fragmenter->fragment_stride !=
            fragmenter->max_datagram_bytes -
                WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ||
        fragmenter->priority > WORR_NATIVE_ENVELOPE_MAX_PRIORITY ||
        fragmenter->reserved0[0] != 0 || fragmenter->reserved0[1] != 0 ||
        fragmenter->reserved0[2] != 0) {
        return false;
    }

    expected_count = fragment_count_for(fragmenter->total_payload_bytes,
                                        fragmenter->fragment_stride);
    if (expected_count == 0 || expected_count != fragmenter->fragment_count ||
        expected_count > WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS ||
        fragmenter->next_fragment > fragmenter->fragment_count) {
        return false;
    }

    return ((fragmenter->state_flags & WORR_NATIVE_FRAGMENTER_EXHAUSTED) != 0) ==
           (fragmenter->next_fragment == fragmenter->fragment_count);
}

worr_native_envelope_emit_result_v1 Worr_NativeEnvelopeFragmentNextV1(
    worr_native_envelope_fragmenter_v1 *fragmenter,
    const void *payload,
    uint32_t payload_bytes,
    void *datagram_out,
    size_t datagram_capacity,
    size_t *datagram_bytes_out)
{
    uint8_t frame[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint32_t fragment_offset;
    uint32_t remaining;
    uint16_t fragment_bytes;
    uint16_t fragment_index;
    uint16_t flags = 0;
    size_t datagram_bytes;

    if (fragmenter == NULL || payload == NULL || datagram_out == NULL ||
        datagram_bytes_out == NULL || payload_bytes == 0) {
        return WORR_NATIVE_ENVELOPE_EMIT_INVALID_ARGUMENT;
    }
    if (ranges_overlap(datagram_bytes_out, sizeof(*datagram_bytes_out),
                       fragmenter, sizeof(*fragmenter)) ||
        ranges_overlap(datagram_bytes_out, sizeof(*datagram_bytes_out),
                       payload, payload_bytes) ||
        ranges_overlap(datagram_bytes_out, sizeof(*datagram_bytes_out),
                       datagram_out,
                       datagram_capacity <
                               WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES
                           ? datagram_capacity
                           : WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES) ||
        ranges_overlap(fragmenter, sizeof(*fragmenter),
                       payload, payload_bytes)) {
        return WORR_NATIVE_ENVELOPE_EMIT_INVALID_ARGUMENT;
    }
    *datagram_bytes_out = 0;
    if (!fragmenter_state_valid(fragmenter) ||
        payload_bytes != fragmenter->total_payload_bytes) {
        return WORR_NATIVE_ENVELOPE_EMIT_INVALID_STATE;
    }
    if ((fragmenter->state_flags & WORR_NATIVE_FRAGMENTER_EXHAUSTED) != 0) {
        return WORR_NATIVE_ENVELOPE_EMIT_EXHAUSTED;
    }

    fragment_index = fragmenter->next_fragment;
    fragment_offset = (uint32_t)fragment_index *
                      (uint32_t)fragmenter->fragment_stride;
    if (fragment_offset >= fragmenter->total_payload_bytes) {
        return WORR_NATIVE_ENVELOPE_EMIT_INVALID_STATE;
    }
    remaining = fragmenter->total_payload_bytes - fragment_offset;
    fragment_bytes = remaining < fragmenter->fragment_stride
                         ? (uint16_t)remaining
                         : fragmenter->fragment_stride;
    datagram_bytes = WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES + fragment_bytes;
    if (datagram_bytes > fragmenter->max_datagram_bytes ||
        datagram_capacity < datagram_bytes) {
        return WORR_NATIVE_ENVELOPE_EMIT_OUTPUT_TOO_SMALL;
    }
    /* The fragmenter intentionally does not retain or copy a complete payload.
     * Reject any emitted frame that could overwrite bytes needed by this or a
     * later iterator call. */
    if (ranges_overlap(payload, payload_bytes,
                       datagram_out, datagram_bytes) ||
        ranges_overlap(fragmenter, sizeof(*fragmenter),
                       datagram_out, datagram_bytes)) {
        return WORR_NATIVE_ENVELOPE_EMIT_INVALID_ARGUMENT;
    }

    if (fragment_index == 0) {
        flags |= WORR_NATIVE_ENVELOPE_FRAGMENT_FIRST;
    }
    if ((uint16_t)(fragment_index + 1u) == fragmenter->fragment_count) {
        flags |= WORR_NATIVE_ENVELOPE_FRAGMENT_LAST;
    }

    memset(frame, 0, datagram_bytes);
    frame[0] = 'W';
    frame[1] = 'N';
    frame[2] = 'E';
    frame[3] = '1';
    write_u16_le(frame + 4, WORR_NATIVE_ENVELOPE_WIRE_VERSION);
    write_u16_le(frame + 6, WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES);
    frame[8] = fragmenter->record.record_class;
    frame[9] = fragmenter->priority;
    write_u16_le(frame + 10, flags);
    write_u16_le(frame + 12, fragmenter->record.record_schema_version);
    write_u32_le(frame + 16, fragmenter->transport_epoch);
    write_u32_le(frame + 20, fragmenter->message_sequence);
    write_u32_le(frame + 24, fragmenter->record.object_epoch);
    write_u32_le(frame + 28, fragmenter->record.object_sequence);
    write_u32_le(frame + 32, fragmenter->total_payload_bytes);
    write_u32_le(frame + 36, fragmenter->payload_crc32);
    write_u32_le(frame + 40, fragment_offset);
    write_u16_le(frame + 44, fragment_bytes);
    write_u16_le(frame + 46, fragment_index);
    write_u16_le(frame + 48, fragmenter->fragment_count);
    write_u16_le(frame + 50, fragmenter->fragment_stride);
    memcpy(frame + WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES,
           (const uint8_t *)payload + fragment_offset,
           fragment_bytes);
    write_u32_le(frame + WORR_NATIVE_WIRE_CRC_OFFSET,
                 datagram_crc32(frame, datagram_bytes));

    memcpy(datagram_out, frame, datagram_bytes);
    fragmenter->next_fragment = (uint16_t)(fragment_index + 1u);
    if (fragmenter->next_fragment == fragmenter->fragment_count) {
        fragmenter->state_flags |= WORR_NATIVE_FRAGMENTER_EXHAUSTED;
    }
    *datagram_bytes_out = datagram_bytes;
    return WORR_NATIVE_ENVELOPE_EMIT_OK;
}

worr_native_envelope_decode_result_v1 Worr_NativeEnvelopeDecodeV1(
    const void *datagram,
    size_t datagram_bytes,
    worr_native_envelope_frame_info_v1 *info_out)
{
    const uint8_t *frame = (const uint8_t *)datagram;
    worr_native_envelope_frame_info_v1 info;
    uint16_t wire_version;
    uint16_t header_bytes;
    uint16_t wire_flags;
    uint16_t expected_flags = 0;
    uint16_t expected_count;
    uint32_t expected_offset;
    uint32_t remaining;
    uint16_t expected_fragment_bytes;
    uint32_t stored_crc;

    if (info_out != NULL) {
        memset(info_out, 0, sizeof(*info_out));
    }
    if (datagram == NULL || info_out == NULL) {
        return WORR_NATIVE_ENVELOPE_DECODE_INVALID_ARGUMENT;
    }
    if (datagram_bytes < WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ||
        datagram_bytes > WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES) {
        return WORR_NATIVE_ENVELOPE_DECODE_MALFORMED;
    }
    if (frame[0] != 'W' || frame[1] != 'N' || frame[2] != 'E' ||
        frame[3] != '1') {
        return WORR_NATIVE_ENVELOPE_DECODE_MALFORMED;
    }

    wire_version = read_u16_le(frame + 4);
    header_bytes = read_u16_le(frame + 6);
    if (wire_version != WORR_NATIVE_ENVELOPE_WIRE_VERSION ||
        header_bytes != WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES) {
        return WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED;
    }

    stored_crc = read_u32_le(frame + WORR_NATIVE_WIRE_CRC_OFFSET);
    if (stored_crc != datagram_crc32(frame, datagram_bytes)) {
        return WORR_NATIVE_ENVELOPE_DECODE_CORRUPT;
    }

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.schema_version = WORR_NATIVE_ENVELOPE_ABI_VERSION;
    info.wire_header_bytes = header_bytes;
    info.record.record_class = frame[8];
    info.priority = frame[9];
    wire_flags = read_u16_le(frame + 10);
    info.record.record_schema_version = read_u16_le(frame + 12);
    info.record.reserved0 = 0;
    info.transport_epoch = read_u32_le(frame + 16);
    info.message_sequence = read_u32_le(frame + 20);
    info.record.object_epoch = read_u32_le(frame + 24);
    info.record.object_sequence = read_u32_le(frame + 28);
    info.total_payload_bytes = read_u32_le(frame + 32);
    info.payload_crc32 = read_u32_le(frame + 36);
    info.fragment_offset = read_u32_le(frame + 40);
    info.fragment_payload_bytes = read_u16_le(frame + 44);
    info.fragment_index = read_u16_le(frame + 46);
    info.fragment_count = read_u16_le(frame + 48);
    info.fragment_stride = read_u16_le(frame + 50);
    info.fragment_flags = (uint8_t)wire_flags;
    info.payload_offset = WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES;

    if (!record_class_valid(info.record.record_class)) {
        return WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED;
    }
    if (!Worr_NativeEnvelopeRecordRefValidV1(info.record) ||
        info.priority > WORR_NATIVE_ENVELOPE_MAX_PRIORITY ||
        frame[14] != 0 || frame[15] != 0 ||
        (wire_flags & ~(WORR_NATIVE_ENVELOPE_FRAGMENT_FIRST |
                        WORR_NATIVE_ENVELOPE_FRAGMENT_LAST)) != 0 ||
        info.transport_epoch == 0 || info.message_sequence == 0 ||
        info.total_payload_bytes == 0 ||
        info.total_payload_bytes > WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES ||
        info.fragment_stride == 0 ||
        (uint32_t)info.fragment_stride +
                WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES >
            WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES ||
        info.fragment_count == 0 ||
        info.fragment_count > WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS ||
        info.fragment_index >= info.fragment_count) {
        return WORR_NATIVE_ENVELOPE_DECODE_MALFORMED;
    }

    expected_count = fragment_count_for(info.total_payload_bytes,
                                        info.fragment_stride);
    if (expected_count == 0 || expected_count != info.fragment_count) {
        return WORR_NATIVE_ENVELOPE_DECODE_MALFORMED;
    }

    expected_offset = (uint32_t)info.fragment_index *
                      (uint32_t)info.fragment_stride;
    if (expected_offset >= info.total_payload_bytes ||
        info.fragment_offset != expected_offset) {
        return WORR_NATIVE_ENVELOPE_DECODE_MALFORMED;
    }
    remaining = info.total_payload_bytes - expected_offset;
    expected_fragment_bytes = remaining < info.fragment_stride
                                  ? (uint16_t)remaining
                                  : info.fragment_stride;
    if (info.fragment_payload_bytes != expected_fragment_bytes ||
        datagram_bytes != WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES +
                              (size_t)expected_fragment_bytes) {
        return WORR_NATIVE_ENVELOPE_DECODE_MALFORMED;
    }

    if (info.fragment_index == 0) {
        expected_flags |= WORR_NATIVE_ENVELOPE_FRAGMENT_FIRST;
    }
    if ((uint16_t)(info.fragment_index + 1u) == info.fragment_count) {
        expected_flags |= WORR_NATIVE_ENVELOPE_FRAGMENT_LAST;
    }
    if (wire_flags != expected_flags) {
        return WORR_NATIVE_ENVELOPE_DECODE_MALFORMED;
    }

    *info_out = info;
    return WORR_NATIVE_ENVELOPE_DECODE_OK;
}

void Worr_NativeEnvelopeReassemblyResetV1(
    worr_native_envelope_reassembly_v1 *reassembly)
{
    if (reassembly == NULL) {
        return;
    }
    memset(reassembly, 0, sizeof(*reassembly));
    reassembly->struct_size = sizeof(*reassembly);
    reassembly->schema_version = WORR_NATIVE_ENVELOPE_ABI_VERSION;
}

static unsigned bitmap_word_count(uint64_t value)
{
    unsigned count = 0;

    while (value != 0) {
        value &= value - 1u;
        ++count;
    }
    return count;
}

static unsigned bitmap_count(const uint64_t bitmap[2])
{
    return bitmap_word_count(bitmap[0]) +
           bitmap_word_count(bitmap[1]);
}

static uint64_t bitmap_valid_bits(uint16_t fragment_count,
                                  unsigned word_index)
{
    const uint16_t word_first_fragment =
        (uint16_t)(word_index * 64u);
    uint16_t word_fragment_count;

    if (fragment_count <= word_first_fragment) {
        return 0;
    }
    word_fragment_count =
        (uint16_t)(fragment_count - word_first_fragment);
    if (word_fragment_count >= 64u) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << word_fragment_count) - 1u;
}

static bool bitmap_has_fragment(const uint64_t bitmap[2],
                                uint16_t fragment_index)
{
    const unsigned word_index = fragment_index / 64u;
    const unsigned bit_index = fragment_index % 64u;

    return (bitmap[word_index] & (UINT64_C(1) << bit_index)) != 0;
}

static bool reassembly_state_valid(
    const worr_native_envelope_reassembly_v1 *reassembly)
{
    uint32_t expected_received_bytes = 0;
    bool complete;

    if (reassembly->struct_size != sizeof(*reassembly) ||
        reassembly->schema_version != WORR_NATIVE_ENVELOPE_ABI_VERSION ||
        (reassembly->state_flags &
         ~(WORR_NATIVE_REASSEMBLY_INITIALIZED |
           WORR_NATIVE_REASSEMBLY_COMPLETE)) != 0) {
        return false;
    }
    if ((reassembly->state_flags & WORR_NATIVE_REASSEMBLY_INITIALIZED) == 0) {
        return reassembly->state_flags == 0 &&
               reassembly->record.record_class == 0 &&
               reassembly->record.reserved0 == 0 &&
               reassembly->record.record_schema_version == 0 &&
               reassembly->record.object_epoch == 0 &&
               reassembly->record.object_sequence == 0 &&
               reassembly->transport_epoch == 0 &&
               reassembly->message_sequence == 0 &&
               reassembly->total_payload_bytes == 0 &&
               reassembly->payload_crc32 == 0 &&
               reassembly->received_fragment_count == 0 &&
               reassembly->received_payload_bytes == 0 &&
               reassembly->fragment_stride == 0 &&
               reassembly->fragment_count == 0 &&
               reassembly->priority == 0 &&
               reassembly->reserved0 == 0 &&
               reassembly->received_bitmap[0] == 0 &&
               reassembly->received_bitmap[1] == 0;
    }

    if (!Worr_NativeEnvelopeRecordRefValidV1(reassembly->record) ||
        reassembly->transport_epoch == 0 ||
        reassembly->message_sequence == 0 ||
        reassembly->total_payload_bytes == 0 ||
        reassembly->total_payload_bytes >
            WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES ||
        reassembly->fragment_stride == 0 ||
        reassembly->fragment_count == 0 ||
        reassembly->fragment_count > WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS ||
        fragment_count_for(reassembly->total_payload_bytes,
                           reassembly->fragment_stride) !=
            reassembly->fragment_count ||
        reassembly->received_fragment_count > reassembly->fragment_count ||
        reassembly->received_payload_bytes > reassembly->total_payload_bytes ||
        reassembly->priority > WORR_NATIVE_ENVELOPE_MAX_PRIORITY ||
        reassembly->reserved0 != 0) {
        return false;
    }

    if ((reassembly->received_bitmap[0] &
         ~bitmap_valid_bits(reassembly->fragment_count, 0)) != 0 ||
        (reassembly->received_bitmap[1] &
         ~bitmap_valid_bits(reassembly->fragment_count, 1)) != 0 ||
        bitmap_count(reassembly->received_bitmap) !=
            reassembly->received_fragment_count) {
        return false;
    }
    for (uint16_t index = 0; index < reassembly->fragment_count; ++index) {
        if (bitmap_has_fragment(reassembly->received_bitmap, index)) {
            const uint32_t offset = (uint32_t)index *
                                    (uint32_t)reassembly->fragment_stride;
            const uint32_t remaining =
                reassembly->total_payload_bytes - offset;
            const uint32_t fragment_bytes =
                remaining < reassembly->fragment_stride
                    ? remaining
                    : reassembly->fragment_stride;

            if (expected_received_bytes >
                reassembly->total_payload_bytes - fragment_bytes) {
                return false;
            }
            expected_received_bytes += fragment_bytes;
        }
    }
    if (expected_received_bytes != reassembly->received_payload_bytes) {
        return false;
    }

    complete = reassembly->received_fragment_count ==
                   reassembly->fragment_count &&
               reassembly->received_payload_bytes ==
                   reassembly->total_payload_bytes;
    return ((reassembly->state_flags & WORR_NATIVE_REASSEMBLY_COMPLETE) != 0) ==
           complete;
}

static bool frame_matches_reassembly(
    const worr_native_envelope_reassembly_v1 *reassembly,
    const worr_native_envelope_frame_info_v1 *info)
{
    return record_ref_equal(reassembly->record, info->record) &&
           reassembly->transport_epoch == info->transport_epoch &&
           reassembly->message_sequence == info->message_sequence &&
           reassembly->total_payload_bytes == info->total_payload_bytes &&
           reassembly->payload_crc32 == info->payload_crc32 &&
           reassembly->fragment_stride == info->fragment_stride &&
           reassembly->fragment_count == info->fragment_count &&
           reassembly->priority == info->priority;
}

worr_native_envelope_accept_result_v1 Worr_NativeEnvelopeReassemblyAcceptV1(
    worr_native_envelope_reassembly_v1 *reassembly,
    void *payload_storage,
    size_t payload_capacity,
    const void *datagram,
    size_t datagram_bytes,
    worr_native_envelope_frame_info_v1 *info_out)
{
    worr_native_envelope_frame_info_v1 info;
    worr_native_envelope_decode_result_v1 decode_result;
    unsigned fragment_word;
    uint64_t fragment_bit;
    uint8_t *destination;
    const uint8_t *source;

    if (info_out != NULL) {
        memset(info_out, 0, sizeof(*info_out));
    }
    if (reassembly == NULL || datagram == NULL) {
        return WORR_NATIVE_ENVELOPE_REJECT_INVALID_ARGUMENT;
    }
    if (!reassembly_state_valid(reassembly)) {
        return WORR_NATIVE_ENVELOPE_REJECT_INVALID_STATE;
    }

    decode_result = Worr_NativeEnvelopeDecodeV1(datagram,
                                                datagram_bytes,
                                                &info);
    if (decode_result == WORR_NATIVE_ENVELOPE_DECODE_INVALID_ARGUMENT) {
        return WORR_NATIVE_ENVELOPE_REJECT_INVALID_ARGUMENT;
    }
    if (decode_result == WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED) {
        return WORR_NATIVE_ENVELOPE_REJECT_UNSUPPORTED;
    }
    if (decode_result == WORR_NATIVE_ENVELOPE_DECODE_CORRUPT) {
        return WORR_NATIVE_ENVELOPE_REJECT_DATAGRAM_CHECKSUM;
    }
    if (decode_result != WORR_NATIVE_ENVELOPE_DECODE_OK) {
        return WORR_NATIVE_ENVELOPE_REJECT_MALFORMED;
    }
    if (info_out != NULL) {
        *info_out = info;
    }

    if (payload_storage == NULL ||
        payload_capacity < info.total_payload_bytes) {
        return WORR_NATIVE_ENVELOPE_REJECT_STORAGE_CAPACITY;
    }
    if ((reassembly->state_flags & WORR_NATIVE_REASSEMBLY_INITIALIZED) != 0 &&
        !frame_matches_reassembly(reassembly, &info)) {
        return WORR_NATIVE_ENVELOPE_REJECT_MESSAGE_CONFLICT;
    }

    fragment_word = info.fragment_index / 64u;
    fragment_bit = UINT64_C(1) << (info.fragment_index % 64u);
    destination = (uint8_t *)payload_storage + info.fragment_offset;
    source = (const uint8_t *)datagram + info.payload_offset;
    if ((reassembly->received_bitmap[fragment_word] & fragment_bit) != 0) {
        if (memcmp(destination, source, info.fragment_payload_bytes) != 0) {
            return WORR_NATIVE_ENVELOPE_REJECT_DUPLICATE_CONFLICT;
        }
        return WORR_NATIVE_ENVELOPE_ACCEPTED_DUPLICATE;
    }

    if ((reassembly->state_flags & WORR_NATIVE_REASSEMBLY_INITIALIZED) == 0) {
        reassembly->state_flags = WORR_NATIVE_REASSEMBLY_INITIALIZED;
        reassembly->record = info.record;
        reassembly->transport_epoch = info.transport_epoch;
        reassembly->message_sequence = info.message_sequence;
        reassembly->total_payload_bytes = info.total_payload_bytes;
        reassembly->payload_crc32 = info.payload_crc32;
        reassembly->fragment_stride = info.fragment_stride;
        reassembly->fragment_count = info.fragment_count;
        reassembly->priority = info.priority;
    }

    if (reassembly->received_payload_bytes >
        reassembly->total_payload_bytes - info.fragment_payload_bytes) {
        return WORR_NATIVE_ENVELOPE_REJECT_INVALID_STATE;
    }

    memmove(destination, source, info.fragment_payload_bytes);
    reassembly->received_bitmap[fragment_word] |= fragment_bit;
    ++reassembly->received_fragment_count;
    reassembly->received_payload_bytes += info.fragment_payload_bytes;

    if (reassembly->received_fragment_count == reassembly->fragment_count) {
        if (reassembly->received_payload_bytes !=
                reassembly->total_payload_bytes ||
            crc32_bytes(payload_storage, reassembly->total_payload_bytes) !=
                reassembly->payload_crc32) {
            Worr_NativeEnvelopeReassemblyResetV1(reassembly);
            return WORR_NATIVE_ENVELOPE_REJECT_MESSAGE_CHECKSUM;
        }
        reassembly->state_flags |= WORR_NATIVE_REASSEMBLY_COMPLETE;
        return WORR_NATIVE_ENVELOPE_ACCEPTED_COMPLETE;
    }

    return WORR_NATIVE_ENVELOPE_ACCEPTED;
}

void Worr_NativeEnvelopeTxQueueResetV1(
    worr_native_envelope_tx_queue_v1 *queue)
{
    if (queue == NULL) {
        return;
    }
    memset(queue, 0, sizeof(*queue));
    queue->struct_size = sizeof(*queue);
    queue->schema_version = WORR_NATIVE_ENVELOPE_ABI_VERSION;
    queue->next_enqueue_serial = 1;
}

static bool tx_item_valid(const worr_native_envelope_tx_item_v1 *item,
                          const worr_native_envelope_tx_queue_v1 *queue)
{
    size_t i;

    if (!Worr_NativeEnvelopeRecordRefValidV1(item->record) ||
        item->payload_handle == 0 || item->payload_bytes == 0 ||
        item->payload_bytes > WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES ||
        item->enqueue_serial == 0 ||
        item->enqueue_serial >= queue->next_enqueue_serial ||
        item->enqueue_dispatch > queue->dispatch_count ||
        item->priority > WORR_NATIVE_ENVELOPE_MAX_PRIORITY) {
        return false;
    }
    for (i = 0; i < sizeof(item->reserved0); ++i) {
        if (item->reserved0[i] != 0) {
            return false;
        }
    }
    return true;
}

static bool tx_queue_state_valid(
    const worr_native_envelope_tx_queue_v1 *queue)
{
    uint16_t i;

    if (queue->struct_size != sizeof(*queue) ||
        queue->schema_version != WORR_NATIVE_ENVELOPE_ABI_VERSION ||
        queue->count > WORR_NATIVE_ENVELOPE_TX_QUEUE_CAPACITY ||
        queue->next_enqueue_serial == 0) {
        return false;
    }
    for (i = 0; i < queue->count; ++i) {
        uint16_t j;

        if (!tx_item_valid(&queue->items[i], queue)) {
            return false;
        }
        for (j = 0; j < i; ++j) {
            if (queue->items[i].payload_handle ==
                    queue->items[j].payload_handle ||
                record_ref_equal(queue->items[i].record,
                                 queue->items[j].record)) {
                return false;
            }
        }
    }
    return true;
}

worr_native_envelope_queue_result_v1 Worr_NativeEnvelopeTxQueuePushV1(
    worr_native_envelope_tx_queue_v1 *queue,
    worr_native_record_ref_v1 record,
    uint8_t priority,
    uint32_t payload_handle,
    uint32_t payload_bytes)
{
    worr_native_envelope_tx_item_v1 *item;
    uint16_t i;

    if (queue == NULL || !Worr_NativeEnvelopeRecordRefValidV1(record) ||
        priority > WORR_NATIVE_ENVELOPE_MAX_PRIORITY || payload_handle == 0 ||
        payload_bytes == 0 ||
        payload_bytes > WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES) {
        return WORR_NATIVE_ENVELOPE_QUEUE_INVALID_ARGUMENT;
    }
    if (!tx_queue_state_valid(queue)) {
        return WORR_NATIVE_ENVELOPE_QUEUE_INVALID_STATE;
    }
    for (i = 0; i < queue->count; ++i) {
        if (queue->items[i].payload_handle == payload_handle ||
            record_ref_equal(queue->items[i].record, record)) {
            return WORR_NATIVE_ENVELOPE_QUEUE_DUPLICATE;
        }
    }
    if (queue->count == WORR_NATIVE_ENVELOPE_TX_QUEUE_CAPACITY) {
        return WORR_NATIVE_ENVELOPE_QUEUE_FULL;
    }
    if (queue->next_enqueue_serial == UINT64_MAX) {
        return WORR_NATIVE_ENVELOPE_QUEUE_SERIAL_EXHAUSTED;
    }

    item = &queue->items[queue->count];
    memset(item, 0, sizeof(*item));
    item->record = record;
    item->payload_handle = payload_handle;
    item->payload_bytes = payload_bytes;
    item->enqueue_serial = queue->next_enqueue_serial++;
    item->enqueue_dispatch = queue->dispatch_count;
    item->priority = priority;
    ++queue->count;
    return WORR_NATIVE_ENVELOPE_QUEUE_OK;
}

static uint8_t effective_priority(
    const worr_native_envelope_tx_queue_v1 *queue,
    const worr_native_envelope_tx_item_v1 *item)
{
    const uint64_t waited = queue->dispatch_count - item->enqueue_dispatch;
    const uint64_t promotion = waited / WORR_NATIVE_ENVELOPE_AGING_QUANTUM;

    return promotion >= item->priority
               ? 0
               : (uint8_t)(item->priority - promotion);
}

worr_native_envelope_queue_result_v1 Worr_NativeEnvelopeTxQueuePopV1(
    worr_native_envelope_tx_queue_v1 *queue,
    worr_native_envelope_tx_item_v1 *item_out)
{
    uint16_t best = 0;
    uint8_t best_priority;
    uint16_t i;

    if (queue == NULL || item_out == NULL) {
        return WORR_NATIVE_ENVELOPE_QUEUE_INVALID_ARGUMENT;
    }
    if (!tx_queue_state_valid(queue)) {
        return WORR_NATIVE_ENVELOPE_QUEUE_INVALID_STATE;
    }
    if (queue->count == 0) {
        return WORR_NATIVE_ENVELOPE_QUEUE_EMPTY;
    }
    if (queue->dispatch_count == UINT64_MAX) {
        return WORR_NATIVE_ENVELOPE_QUEUE_SERIAL_EXHAUSTED;
    }
    if (ranges_overlap(queue, sizeof(*queue),
                       item_out, sizeof(*item_out))) {
        return WORR_NATIVE_ENVELOPE_QUEUE_INVALID_ARGUMENT;
    }

    best_priority = effective_priority(queue, &queue->items[0]);
    for (i = 1; i < queue->count; ++i) {
        const uint8_t candidate_priority =
            effective_priority(queue, &queue->items[i]);

        if (candidate_priority < best_priority ||
            (candidate_priority == best_priority &&
             queue->items[i].enqueue_serial <
                 queue->items[best].enqueue_serial)) {
            best = i;
            best_priority = candidate_priority;
        }
    }

    *item_out = queue->items[best];
    if ((uint16_t)(best + 1u) < queue->count) {
        memmove(&queue->items[best],
                &queue->items[best + 1u],
                (size_t)(queue->count - best - 1u) *
                    sizeof(queue->items[0]));
    }
    --queue->count;
    memset(&queue->items[queue->count], 0, sizeof(queue->items[0]));
    ++queue->dispatch_count;

    if (queue->count == 0) {
        Worr_NativeEnvelopeTxQueueResetV1(queue);
    }
    return WORR_NATIVE_ENVELOPE_QUEUE_OK;
}
