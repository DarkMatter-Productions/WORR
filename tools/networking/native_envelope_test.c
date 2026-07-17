/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/native_envelope.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "native envelope check failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition);                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

static uint8_t payload[WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES];
static uint8_t restored[WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES];
static uint8_t frames[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS]
                     [WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
static size_t frame_bytes[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS];

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
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

static uint32_t frame_crc(const uint8_t *frame, size_t bytes)
{
    static const uint8_t zeros[4] = { 0, 0, 0, 0 };
    uint32_t crc = UINT32_MAX;

    crc = crc32_update(crc, frame, 52);
    crc = crc32_update(crc, zeros, sizeof(zeros));
    crc = crc32_update(crc, frame + 56, bytes - 56);
    return ~crc;
}

static void repair_frame_crc(uint8_t *frame, size_t bytes)
{
    memset(frame + 52, 0, 4);
    write_u32_le(frame + 52, frame_crc(frame, bytes));
}

static worr_native_record_ref_v1 make_ref(uint8_t record_class,
                                          uint32_t sequence)
{
    worr_native_record_ref_v1 record;

    memset(&record, 0, sizeof(record));
    record.record_class = record_class;
    record.record_schema_version = 1;
    record.object_epoch = 7;
    record.object_sequence = sequence;
    return record;
}

static void fill_payload(uint32_t bytes, uint32_t salt)
{
    uint32_t i;

    for (i = 0; i < bytes; ++i) {
        payload[i] = (uint8_t)((i * 131u + i / 7u + salt) & 0xffu);
    }
}

static bool encode_payload(uint32_t bytes,
                           uint16_t mtu,
                           uint32_t transport_epoch,
                           uint32_t message_sequence,
                           worr_native_record_ref_v1 record,
                           uint8_t priority,
                           uint16_t *count_out)
{
    worr_native_envelope_fragmenter_v1 fragmenter;
    uint16_t count = 0;
    size_t exhausted_bytes = 99;

    CHECK(Worr_NativeEnvelopeFragmenterInitV1(&fragmenter,
                                              transport_epoch,
                                              message_sequence,
                                              record,
                                              priority,
                                              payload,
                                              bytes,
                                              mtu));
    while ((fragmenter.state_flags & WORR_NATIVE_FRAGMENTER_EXHAUSTED) == 0) {
        CHECK(count < WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS);
        CHECK(Worr_NativeEnvelopeFragmentNextV1(
                  &fragmenter,
                  payload,
                  bytes,
                  frames[count],
                  sizeof(frames[count]),
                  &frame_bytes[count]) == WORR_NATIVE_ENVELOPE_EMIT_OK);
        CHECK(frame_bytes[count] <= mtu);
        CHECK(frame_bytes[count] <=
              WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES);
        ++count;
    }
    CHECK(fragmenter.next_fragment == fragmenter.fragment_count);
    CHECK(count == fragmenter.fragment_count);
    CHECK(Worr_NativeEnvelopeFragmentNextV1(
              &fragmenter,
              payload,
              bytes,
              frames[0],
              sizeof(frames[0]),
              &exhausted_bytes) == WORR_NATIVE_ENVELOPE_EMIT_EXHAUSTED);
    CHECK(exhausted_bytes == 0);
    *count_out = count;
    return true;
}

static bool test_fragmentation_and_reorder(void)
{
    static const uint32_t sizes[] = { 1, 1144, 1145, 5000, 65536 };
    size_t case_index;

    for (case_index = 0; case_index < sizeof(sizes) / sizeof(sizes[0]);
         ++case_index) {
        worr_native_envelope_reassembly_v1 reassembly;
        worr_native_envelope_frame_info_v1 info;
        const uint32_t bytes = sizes[case_index];
        uint16_t count;
        uint16_t index;
        bool completed = false;

        fill_payload(bytes, (uint32_t)case_index);
        CHECK(encode_payload(bytes,
                             1200,
                             3,
                             (uint32_t)case_index + 1u,
                             make_ref(WORR_NATIVE_RECORD_SNAPSHOT_V1,
                                      (uint32_t)case_index + 20u),
                             4,
                             &count));
        CHECK(count == (uint16_t)(bytes / 1144u +
                                  (bytes % 1144u != 0 ? 1u : 0u)));

        for (index = 0; index < count; ++index) {
            const worr_native_envelope_decode_result_v1 decode_result =
                Worr_NativeEnvelopeDecodeV1(frames[index],
                                            frame_bytes[index],
                                            &info);
            if (decode_result != WORR_NATIVE_ENVELOPE_DECODE_OK) {
                fprintf(stderr,
                        "decode failed for payload %u fragment %u/%u: %d "
                        "wire={class=%u pri=%u flags=%u schema=%u epoch=%u "
                        "message=%u object=%u:%u total=%u offset=%u bytes=%u "
                        "index=%u count=%u stride=%u datagram=%zu}\n",
                        bytes,
                        index,
                        count,
                        (int)decode_result,
                        frames[index][8],
                        frames[index][9],
                        read_u16_le(frames[index] + 10),
                        read_u16_le(frames[index] + 12),
                        (unsigned)(frames[index][16] |
                                   (frames[index][17] << 8)),
                        (unsigned)(frames[index][20] |
                                   (frames[index][21] << 8)),
                        (unsigned)(frames[index][24] |
                                   (frames[index][25] << 8)),
                        (unsigned)(frames[index][28] |
                                   (frames[index][29] << 8)),
                        (unsigned)(frames[index][32] |
                                   (frames[index][33] << 8)),
                        (unsigned)(frames[index][40] |
                                   (frames[index][41] << 8)),
                        read_u16_le(frames[index] + 44),
                        read_u16_le(frames[index] + 46),
                        read_u16_le(frames[index] + 48),
                        read_u16_le(frames[index] + 50),
                        frame_bytes[index]);
                return false;
            }
            CHECK(info.fragment_index == index);
            CHECK(info.fragment_count == count);
            CHECK(info.fragment_stride == 1144);
            CHECK(info.total_payload_bytes == bytes);
            CHECK(info.payload_offset == 56);
            if (index == 0) {
                CHECK((info.fragment_flags &
                       WORR_NATIVE_ENVELOPE_FRAGMENT_FIRST) != 0);
            }
            if ((uint16_t)(index + 1u) == count) {
                CHECK((info.fragment_flags &
                       WORR_NATIVE_ENVELOPE_FRAGMENT_LAST) != 0);
            }
        }

        memset(restored, 0xa5, bytes);
        Worr_NativeEnvelopeReassemblyResetV1(&reassembly);
        for (index = count; index-- > 0;) {
            const worr_native_envelope_accept_result_v1 result =
                Worr_NativeEnvelopeReassemblyAcceptV1(
                    &reassembly,
                    restored,
                    sizeof(restored),
                    frames[index],
                    frame_bytes[index],
                    NULL);

            if (index == 0) {
                CHECK(result == WORR_NATIVE_ENVELOPE_ACCEPTED_COMPLETE);
                completed = true;
            } else {
                CHECK(result == WORR_NATIVE_ENVELOPE_ACCEPTED);
            }
            if (index == (uint16_t)(count - 1u)) {
                CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
                          &reassembly,
                          restored,
                          sizeof(restored),
                          frames[index],
                          frame_bytes[index],
                          NULL) == WORR_NATIVE_ENVELOPE_ACCEPTED_DUPLICATE);
            }
        }
        CHECK(completed);
        CHECK((reassembly.state_flags & WORR_NATIVE_REASSEMBLY_COMPLETE) != 0);
        CHECK(memcmp(restored, payload, bytes) == 0);
        CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
                  &reassembly,
                  restored,
                  sizeof(restored),
                  frames[0],
                  frame_bytes[0],
                  NULL) == WORR_NATIVE_ENVELOPE_ACCEPTED_DUPLICATE);
    }

    return true;
}

static bool test_maximum_payload_and_fragment_count(void)
{
    worr_native_envelope_reassembly_v1 reassembly;
    worr_native_envelope_frame_info_v1 info;
    uint16_t count;
    uint16_t index;

    fill_payload(WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES, 91);
    CHECK(encode_payload(WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES,
                         WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES,
                         12,
                         43,
                         make_ref(WORR_NATIVE_RECORD_SNAPSHOT_V1, 87),
                         3,
                         &count));
    CHECK(count == 115);
    CHECK(encode_payload(WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES,
                         1080,
                         12,
                         44,
                         make_ref(WORR_NATIVE_RECORD_SNAPSHOT_V1, 88),
                         3,
                         &count));
    CHECK(count == WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS);
    CHECK(Worr_NativeEnvelopeDecodeV1(
              frames[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS - 1u],
              frame_bytes[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS - 1u],
              &info) == WORR_NATIVE_ENVELOPE_DECODE_OK);
    CHECK(info.fragment_index ==
              WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS - 1u &&
          info.fragment_count == WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS &&
          info.fragment_stride == 1024 &&
          info.total_payload_bytes ==
              WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES);

    memset(restored, 0xa5, sizeof(restored));
    Worr_NativeEnvelopeReassemblyResetV1(&reassembly);
    for (index = count; index-- > 0;) {
        const worr_native_envelope_accept_result_v1 result =
            Worr_NativeEnvelopeReassemblyAcceptV1(
                &reassembly,
                restored,
                sizeof(restored),
                frames[index],
                frame_bytes[index],
                NULL);

        CHECK(result ==
              (index == 0
                   ? WORR_NATIVE_ENVELOPE_ACCEPTED_COMPLETE
                   : WORR_NATIVE_ENVELOPE_ACCEPTED));
        if (index == (uint16_t)(count - 1u)) {
            CHECK(reassembly.received_bitmap[0] == 0);
            CHECK(reassembly.received_bitmap[1] ==
                  (UINT64_C(1) << 63));
            CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
                      &reassembly,
                      restored,
                      sizeof(restored),
                      frames[index],
                      frame_bytes[index],
                      NULL) ==
                  WORR_NATIVE_ENVELOPE_ACCEPTED_DUPLICATE);
        }
    }
    CHECK(reassembly.received_bitmap[0] == UINT64_MAX &&
          reassembly.received_bitmap[1] == UINT64_MAX);
    CHECK(reassembly.received_fragment_count ==
          WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS);
    CHECK(memcmp(restored, payload, sizeof(payload)) == 0);
    return true;
}

static bool test_fragmenter_limits_and_transactionality(void)
{
    worr_native_envelope_fragmenter_v1 fragmenter;
    worr_native_record_ref_v1 record =
        make_ref(WORR_NATIVE_RECORD_COMMAND_V1, 1);
    uint8_t output[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t output_bytes = 99;

    fill_payload(sizeof(payload), 81);
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, 1, 1, record, 0, payload, 0, 1200));
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, 1, 1, record, 0, NULL, 1, 1200));
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, 1, 1, record, 0, payload, 1, 56));
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, 1, 1, record, 0, payload, 1, 1201));
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, 0, 1, record, 0, payload, 1, 1200));
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, 1, 0, record, 0, payload, 1, 1200));
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, 1, 1, record, 8, payload, 1, 1200));
    record.object_sequence = 0;
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, 1, 1, record, 0, payload, 1, 1200));
    record.object_sequence = 1;
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter,
        1,
        1,
        record,
        0,
        payload,
        WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES + 1u,
        1200));
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter,
        1,
        1,
        record,
        0,
        payload,
        WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES,
        1079));
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter,
        1,
        1,
        record,
        0,
        payload,
        WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES,
        1080));
    CHECK(fragmenter.fragment_stride == 1024 &&
          fragmenter.fragment_count ==
              WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS);
    CHECK(!Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter,
        1,
        1,
        record,
        0,
        payload,
        sizeof(payload),
        64));

    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, 1, 1, record, 0, payload, 2000, 1200));
    output_bytes = 99;
    CHECK(Worr_NativeEnvelopeFragmentNextV1(&fragmenter,
                                            payload,
                                            2000,
                                            payload,
                                            1200,
                                            &output_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_INVALID_ARGUMENT);
    CHECK(output_bytes == 0);
    CHECK(fragmenter.next_fragment == 0);
    CHECK(Worr_NativeEnvelopeFragmentNextV1(&fragmenter,
                                            payload,
                                            2000,
                                            output,
                                            1199,
                                            &output_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_OUTPUT_TOO_SMALL);
    CHECK(output_bytes == 0);
    CHECK(fragmenter.next_fragment == 0);
    CHECK(Worr_NativeEnvelopeFragmentNextV1(&fragmenter,
                                            payload,
                                            1999,
                                            output,
                                            sizeof(output),
                                            &output_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_INVALID_STATE);
    CHECK(fragmenter.next_fragment == 0);
    fragmenter.fragment_count = 1;
    CHECK(Worr_NativeEnvelopeFragmentNextV1(&fragmenter,
                                            payload,
                                            2000,
                                            output,
                                            sizeof(output),
                                            &output_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_INVALID_STATE);
    return true;
}

static bool expect_decode(uint8_t *frame,
                          size_t bytes,
                          worr_native_envelope_decode_result_v1 expected)
{
    worr_native_envelope_frame_info_v1 info;

    CHECK(Worr_NativeEnvelopeDecodeV1(frame, bytes, &info) == expected);
    return true;
}

static bool test_malformed_and_integrity(void)
{
    uint8_t bad[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    worr_native_envelope_frame_info_v1 info;
    uint16_t count;

    fill_payload(2000, 17);
    CHECK(encode_payload(2000,
                         1200,
                         9,
                         11,
                         make_ref(WORR_NATIVE_RECORD_EVENT_V1, 31),
                         2,
                         &count));
    CHECK(count == 2);

    memcpy(bad, frames[0], frame_bytes[0]);
    bad[56] ^= 0x40;
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_CORRUPT));

    memcpy(bad, frames[0], frame_bytes[0]);
    bad[0] = 'X';
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u16_le(bad + 4, 2);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u16_le(bad + 6, 60);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED));

    memcpy(bad, frames[0], frame_bytes[0]);
    bad[8] = 99;
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED));

    memcpy(bad, frames[0], frame_bytes[0]);
    bad[9] = 8;
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u16_le(bad + 10, 4);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u16_le(bad + 12, 0);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    bad[14] = 1;
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u32_le(bad + 16, 0);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u32_le(bad + 24, 0);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u32_le(bad + 32, 0);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u32_le(bad + 40, 1);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u16_le(bad + 44, 1143);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u16_le(bad + 46, 2);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u16_le(bad + 48, 3);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    memcpy(bad, frames[0], frame_bytes[0]);
    write_u16_le(bad + 50, 0);
    repair_frame_crc(bad, frame_bytes[0]);
    CHECK(expect_decode(bad,
                        frame_bytes[0],
                        WORR_NATIVE_ENVELOPE_DECODE_MALFORMED));

    CHECK(read_u16_le(frames[0] + 44) == 1144);
    CHECK(Worr_NativeEnvelopeDecodeV1(frames[0], 55, NULL) ==
          WORR_NATIVE_ENVELOPE_DECODE_INVALID_ARGUMENT);
    CHECK(Worr_NativeEnvelopeDecodeV1(frames[0], 55, &info) ==
          WORR_NATIVE_ENVELOPE_DECODE_MALFORMED);
    return true;
}

static bool test_reassembly_rejections(void)
{
    worr_native_envelope_reassembly_v1 reassembly;
    worr_native_envelope_reassembly_v1 malformed_reassembly;
    uint8_t altered[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint8_t other_frame[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t other_bytes;
    worr_native_envelope_fragmenter_v1 other_fragmenter;
    uint16_t count;

    fill_payload(2000, 42);
    CHECK(encode_payload(2000,
                         1200,
                         2,
                         5,
                         make_ref(WORR_NATIVE_RECORD_COMMAND_V1, 70),
                         1,
                         &count));
    CHECK(count == 2);
    memset(restored, 0, 2000);
    Worr_NativeEnvelopeReassemblyResetV1(&reassembly);
    CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
              &reassembly,
              restored,
              1999,
              frames[0],
              frame_bytes[0],
              NULL) == WORR_NATIVE_ENVELOPE_REJECT_STORAGE_CAPACITY);
    CHECK(reassembly.state_flags == 0);

    memcpy(altered, frames[0], frame_bytes[0]);
    altered[56] ^= 1;
    CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
              &reassembly,
              restored,
              sizeof(restored),
              altered,
              frame_bytes[0],
              NULL) == WORR_NATIVE_ENVELOPE_REJECT_DATAGRAM_CHECKSUM);
    CHECK(reassembly.state_flags == 0);

    CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
              &reassembly,
              restored,
              sizeof(restored),
              frames[0],
              frame_bytes[0],
              NULL) == WORR_NATIVE_ENVELOPE_ACCEPTED);
    malformed_reassembly = reassembly;
    malformed_reassembly.received_bitmap[1] = UINT64_C(1);
    CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
              &malformed_reassembly,
              restored,
              sizeof(restored),
              frames[1],
              frame_bytes[1],
              NULL) == WORR_NATIVE_ENVELOPE_REJECT_INVALID_STATE);

    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &other_fragmenter,
        2,
        6,
        make_ref(WORR_NATIVE_RECORD_COMMAND_V1, 71),
        1,
        payload,
        2000,
        1200));
    CHECK(Worr_NativeEnvelopeFragmentNextV1(&other_fragmenter,
                                            payload,
                                            2000,
                                            other_frame,
                                            sizeof(other_frame),
                                            &other_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_OK);
    CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
              &reassembly,
              restored,
              sizeof(restored),
              other_frame,
              other_bytes,
              NULL) == WORR_NATIVE_ENVELOPE_REJECT_MESSAGE_CONFLICT);

    memcpy(altered, frames[0], frame_bytes[0]);
    altered[56] ^= 0x22;
    repair_frame_crc(altered, frame_bytes[0]);
    CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
              &reassembly,
              restored,
              sizeof(restored),
              altered,
              frame_bytes[0],
              NULL) == WORR_NATIVE_ENVELOPE_REJECT_DUPLICATE_CONFLICT);

    Worr_NativeEnvelopeReassemblyResetV1(&reassembly);
    memcpy(altered, frames[0], frame_bytes[0]);
    altered[56] ^= 0x80;
    repair_frame_crc(altered, frame_bytes[0]);
    CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
              &reassembly,
              restored,
              sizeof(restored),
              altered,
              frame_bytes[0],
              NULL) == WORR_NATIVE_ENVELOPE_ACCEPTED);
    CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
              &reassembly,
              restored,
              sizeof(restored),
              frames[1],
              frame_bytes[1],
              NULL) == WORR_NATIVE_ENVELOPE_REJECT_MESSAGE_CHECKSUM);
    CHECK(reassembly.state_flags == 0);

    reassembly.struct_size = 0;
    CHECK(Worr_NativeEnvelopeReassemblyAcceptV1(
              &reassembly,
              restored,
              sizeof(restored),
              frames[0],
              frame_bytes[0],
              NULL) == WORR_NATIVE_ENVELOPE_REJECT_INVALID_STATE);
    return true;
}

static bool test_priority_queue(void)
{
    worr_native_envelope_tx_queue_v1 queue;
    worr_native_envelope_tx_queue_v1 before;
    worr_native_envelope_tx_item_v1 item;
    uint32_t i;

    Worr_NativeEnvelopeTxQueueResetV1(&queue);
    CHECK(Worr_NativeEnvelopeTxQueuePopV1(&queue, &item) ==
          WORR_NATIVE_ENVELOPE_QUEUE_EMPTY);
    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_SNAPSHOT_V1, 1),
              5,
              1,
              100) == WORR_NATIVE_ENVELOPE_QUEUE_OK);
    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_COMMAND_V1, 2),
              1,
              2,
              100) == WORR_NATIVE_ENVELOPE_QUEUE_OK);
    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_EVENT_V1, 3),
              1,
              3,
              100) == WORR_NATIVE_ENVELOPE_QUEUE_OK);
    before = queue;
    CHECK(Worr_NativeEnvelopeTxQueuePopV1(
              &queue, &queue.items[0]) ==
          WORR_NATIVE_ENVELOPE_QUEUE_INVALID_ARGUMENT);
    CHECK(memcmp(&queue, &before, sizeof(queue)) == 0);
    CHECK(Worr_NativeEnvelopeTxQueuePopV1(&queue, &item) ==
          WORR_NATIVE_ENVELOPE_QUEUE_OK);
    CHECK(item.payload_handle == 2);
    CHECK(Worr_NativeEnvelopeTxQueuePopV1(&queue, &item) ==
          WORR_NATIVE_ENVELOPE_QUEUE_OK);
    CHECK(item.payload_handle == 3);
    CHECK(Worr_NativeEnvelopeTxQueuePopV1(&queue, &item) ==
          WORR_NATIVE_ENVELOPE_QUEUE_OK);
    CHECK(item.payload_handle == 1);
    CHECK(queue.count == 0 && queue.dispatch_count == 0 &&
          queue.next_enqueue_serial == 1);

    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_EVENT_V1, 8),
              2,
              8,
              20) == WORR_NATIVE_ENVELOPE_QUEUE_OK);
    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_EVENT_V1, 8),
              2,
              9,
              20) == WORR_NATIVE_ENVELOPE_QUEUE_DUPLICATE);
    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_EVENT_V1, 9),
              2,
              8,
              20) == WORR_NATIVE_ENVELOPE_QUEUE_DUPLICATE);
    Worr_NativeEnvelopeTxQueueResetV1(&queue);

    for (i = 0; i < WORR_NATIVE_ENVELOPE_TX_QUEUE_CAPACITY; ++i) {
        CHECK(Worr_NativeEnvelopeTxQueuePushV1(
                  &queue,
                  make_ref(WORR_NATIVE_RECORD_SNAPSHOT_V1, i + 1u),
                  4,
                  i + 1u,
                  50) == WORR_NATIVE_ENVELOPE_QUEUE_OK);
    }
    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_SNAPSHOT_V1, 1000),
              4,
              1000,
              50) == WORR_NATIVE_ENVELOPE_QUEUE_FULL);

    Worr_NativeEnvelopeTxQueueResetV1(&queue);
    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_SNAPSHOT_V1, 1),
              7,
              1,
              100) == WORR_NATIVE_ENVELOPE_QUEUE_OK);
    for (i = 0; i < 56; ++i) {
        CHECK(Worr_NativeEnvelopeTxQueuePushV1(
                  &queue,
                  make_ref(WORR_NATIVE_RECORD_COMMAND_V1, 100 + i),
                  0,
                  100 + i,
                  100) == WORR_NATIVE_ENVELOPE_QUEUE_OK);
        CHECK(Worr_NativeEnvelopeTxQueuePopV1(&queue, &item) ==
              WORR_NATIVE_ENVELOPE_QUEUE_OK);
        CHECK(item.payload_handle == 100 + i);
    }
    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_COMMAND_V1, 1000),
              0,
              1000,
              100) == WORR_NATIVE_ENVELOPE_QUEUE_OK);
    CHECK(Worr_NativeEnvelopeTxQueuePopV1(&queue, &item) ==
          WORR_NATIVE_ENVELOPE_QUEUE_OK);
    CHECK(item.payload_handle == 1);
    CHECK(Worr_NativeEnvelopeTxQueuePopV1(&queue, &item) ==
          WORR_NATIVE_ENVELOPE_QUEUE_OK);
    CHECK(item.payload_handle == 1000);

    Worr_NativeEnvelopeTxQueueResetV1(&queue);
    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_COMMAND_V1, 1),
              8,
              1,
              1) == WORR_NATIVE_ENVELOPE_QUEUE_INVALID_ARGUMENT);
    queue.struct_size = 0;
    CHECK(Worr_NativeEnvelopeTxQueuePushV1(
              &queue,
              make_ref(WORR_NATIVE_RECORD_COMMAND_V1, 1),
              0,
              1,
              1) == WORR_NATIVE_ENVELOPE_QUEUE_INVALID_STATE);
    return true;
}

int main(void)
{
    if (!test_fragmentation_and_reorder() ||
        !test_maximum_payload_and_fragment_count() ||
        !test_fragmenter_limits_and_transactionality() ||
        !test_malformed_and_integrity() ||
        !test_reassembly_rejections() ||
        !test_priority_queue()) {
        return 1;
    }

    puts("native envelope tests passed");
    return 0;
}
