/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* Deterministic canonical event-stream descriptor and native-codec tests. */

#include "common/net/native_codec.h"
#include "shared/event_stream.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_STREAM_ENCODED_BYTES                                      \
    (WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +                              \
     WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES)

#define CHECK(condition)                                                 \
    do {                                                                 \
        if (!(condition)) {                                              \
            fprintf(stderr, "event_stream_test:%d: %s\n", __LINE__,  \
                    #condition);                                         \
            return false;                                                \
        }                                                                \
    } while (0)

static void store_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void store_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool memory_is_value(const void *memory, size_t bytes, uint8_t value)
{
    const uint8_t *cursor = (const uint8_t *)memory;
    size_t index;
    for (index = 0; index < bytes; ++index) {
        if (cursor[index] != value)
            return false;
    }
    return true;
}

static worr_event_stream_descriptor_v1 make_descriptor(
    uint32_t stream_epoch, uint32_t first_sequence)
{
    worr_event_stream_descriptor_v1 descriptor;
    memset(&descriptor, 0xa5, sizeof(descriptor));
    if (!Worr_EventStreamDescriptorInitV1(
            &descriptor, stream_epoch, first_sequence)) {
        fprintf(stderr, "event_stream_test:%d: descriptor fixture init\n",
                __LINE__);
        exit(1);
    }
    return descriptor;
}

static bool test_descriptor_initialization(void)
{
    const uint32_t stream_epoch = UINT32_C(0x01020304);
    const uint32_t first_sequence = UINT32_C(0x11223344);
    worr_event_stream_descriptor_v1 descriptor;
    worr_event_stream_descriptor_v1 expected;
    worr_event_stream_descriptor_v1 maximum;

    memset(&descriptor, 0xa5, sizeof(descriptor));
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, stream_epoch, first_sequence));
    memset(&expected, 0, sizeof(expected));
    expected.struct_size = sizeof(expected);
    expected.schema_version = WORR_EVENT_STREAM_ABI_VERSION;
    expected.stream_epoch = stream_epoch;
    expected.first_sequence = first_sequence;
    expected.event_schema_version = WORR_EVENT_ABI_VERSION;
    expected.model_revision = WORR_EVENT_MODEL_REVISION;
    CHECK(memcmp(&descriptor, &expected, sizeof(descriptor)) == 0);
    CHECK(Worr_EventStreamDescriptorValidateV1(&descriptor));

    maximum = make_descriptor(UINT32_MAX, UINT32_MAX);
    CHECK(Worr_EventStreamDescriptorValidateV1(&maximum));
    CHECK(maximum.stream_epoch == UINT32_MAX &&
          maximum.first_sequence == UINT32_MAX);

    /* A midstream first sequence and numerical equality to unrelated epoch
     * domains remain valid only because they were supplied explicitly. */
    descriptor = make_descriptor(77, 400);
    CHECK(descriptor.stream_epoch == 77 &&
          descriptor.first_sequence == 400);
    return true;
}

static bool test_invalid_initialization_is_transactional(void)
{
    worr_event_stream_descriptor_v1 descriptor;
    worr_event_stream_descriptor_v1 before;
    size_t index;
    static const uint32_t invalid_pairs[][2] = {
        {0, 0},
        {0, 1},
        {1, 0},
        {0, UINT32_MAX},
        {UINT32_MAX, 0},
    };

    CHECK(!Worr_EventStreamDescriptorInitV1(NULL, 1, 1));
    for (index = 0;
         index < sizeof(invalid_pairs) / sizeof(invalid_pairs[0]);
         ++index) {
        memset(&descriptor, 0x5a, sizeof(descriptor));
        before = descriptor;
        CHECK(!Worr_EventStreamDescriptorInitV1(
            &descriptor, invalid_pairs[index][0], invalid_pairs[index][1]));
        CHECK(memcmp(&descriptor, &before, sizeof(descriptor)) == 0);
    }
    return true;
}

static bool test_descriptor_validation_matrix(void)
{
    worr_event_stream_descriptor_v1 valid = make_descriptor(91, 17);
    worr_event_stream_descriptor_v1 before = valid;
    worr_event_stream_descriptor_v1 malformed;
    uint32_t index;

    CHECK(!Worr_EventStreamDescriptorValidateV1(NULL));
    valid.flags = WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2;
    CHECK(Worr_EventStreamDescriptorValidateV1(&valid));
    valid.flags = 0;
    for (index = 0; index < 12; ++index) {
        malformed = valid;
        switch (index) {
        case 0:
            malformed.struct_size = 0;
            break;
        case 1:
            malformed.struct_size++;
            break;
        case 2:
            malformed.schema_version = 0;
            break;
        case 3:
            malformed.schema_version++;
            break;
        case 4:
            malformed.flags = UINT16_C(0x8000);
            break;
        case 5:
            malformed.flags = UINT16_MAX;
            break;
        case 6:
            malformed.stream_epoch = 0;
            break;
        case 7:
            malformed.first_sequence = 0;
            break;
        case 8:
            malformed.event_schema_version = 0;
            break;
        case 9:
            malformed.event_schema_version++;
            break;
        case 10:
            malformed.model_revision = 0;
            break;
        case 11:
            malformed.model_revision++;
            break;
        default:
            CHECK(false);
        }
        CHECK(!Worr_EventStreamDescriptorValidateV1(&malformed));
        CHECK(memcmp(&valid, &before, sizeof(valid)) == 0);
    }
    CHECK(Worr_EventStreamDescriptorValidateV1(&valid));
    return true;
}

static bool test_descriptor_equality(void)
{
    worr_event_stream_descriptor_v1 left = make_descriptor(12, 34);
    worr_event_stream_descriptor_v1 right = make_descriptor(12, 34);
    worr_event_stream_descriptor_v1 left_before = left;
    worr_event_stream_descriptor_v1 right_before = right;
    worr_event_stream_descriptor_v1 malformed;

    CHECK(Worr_EventStreamDescriptorEqualV1(&left, &right));
    CHECK(Worr_EventStreamDescriptorEqualV1(&left, &left));
    CHECK(!Worr_EventStreamDescriptorEqualV1(NULL, &right));
    CHECK(!Worr_EventStreamDescriptorEqualV1(&left, NULL));
    CHECK(!Worr_EventStreamDescriptorEqualV1(NULL, NULL));

    right = make_descriptor(13, 34);
    CHECK(!Worr_EventStreamDescriptorEqualV1(&left, &right));
    right = make_descriptor(12, 35);
    CHECK(!Worr_EventStreamDescriptorEqualV1(&left, &right));

    right = left;
    right.flags = WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2;
    CHECK(!Worr_EventStreamDescriptorEqualV1(&left, &right));
    malformed = right;
    CHECK(Worr_EventStreamDescriptorEqualV1(&right, &malformed));

    malformed = left;
    malformed.flags = UINT16_C(0x8000);
    CHECK(!Worr_EventStreamDescriptorEqualV1(&malformed, &malformed));
    CHECK(!Worr_EventStreamDescriptorEqualV1(&left, &malformed));
    CHECK(memcmp(&left, &left_before, sizeof(left)) == 0);
    right = right_before;
    CHECK(memcmp(&right, &right_before, sizeof(right)) == 0);
    return true;
}

static bool check_decode_failure_untouched(
    const void *encoded, size_t encoded_bytes,
    worr_native_codec_result_v1 expected)
{
    worr_event_stream_descriptor_v1 decoded;
    worr_event_stream_descriptor_v1 before;
    memset(&decoded, 0x6b, sizeof(decoded));
    before = decoded;
    CHECK(Worr_NativeCodecEventStreamDecodeV1(
              encoded, encoded_bytes, &decoded) == expected);
    CHECK(memcmp(&decoded, &before, sizeof(decoded)) == 0);
    return true;
}

static bool test_native_codec_round_trip(void)
{
    const worr_event_stream_descriptor_v1 source =
        make_descriptor(UINT32_C(0x01020304), UINT32_C(0x11223344));
    const worr_event_stream_descriptor_v1 source_before = source;
    worr_event_stream_descriptor_v1 decoded;
    worr_event_stream_descriptor_v1 flagged;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 record_ref;
    uint8_t encoded[64];
    uint32_t preflight = 0;
    size_t encoded_bytes = 0;

    memset(encoded, 0xa5, sizeof(encoded));
    CHECK(Worr_NativeCodecEventStreamPreflightV1(
              &source, &preflight) == WORR_NATIVE_CODEC_OK);
    CHECK(preflight == EVENT_STREAM_ENCODED_BYTES);
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &source, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes == EVENT_STREAM_ENCODED_BYTES);
    CHECK(memory_is_value(encoded + encoded_bytes,
                          sizeof(encoded) - encoded_bytes, 0xa5));
    CHECK(memcmp(&source, &source_before, sizeof(source)) == 0);

    CHECK(memcmp(encoded, "WNC1", 4) == 0);
    CHECK(encoded[4] == WORR_NATIVE_CODEC_WIRE_VERSION && encoded[5] == 0);
    CHECK(encoded[6] == WORR_NATIVE_CODEC_WIRE_HEADER_BYTES &&
          encoded[7] == 0);
    CHECK(encoded[8] == WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
    CHECK(encoded[9] == 0);
    CHECK(encoded[10] == WORR_EVENT_STREAM_ABI_VERSION && encoded[11] == 0);
    CHECK(encoded[12] == WORR_EVENT_MODEL_REVISION && encoded[13] == 0 &&
          encoded[14] == 0 && encoded[15] == 0);
    CHECK(encoded[16] == EVENT_STREAM_ENCODED_BYTES &&
          encoded[17] == 0 && encoded[18] == 0 && encoded[19] == 0);
    CHECK(encoded[20] == WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES &&
          encoded[21] == 0 && encoded[22] == 0 && encoded[23] == 0);
    CHECK(memory_is_value(encoded + 24, 12, 0));
    CHECK(encoded[36] == 0x04 && encoded[37] == 0x03 &&
          encoded[38] == 0x02 && encoded[39] == 0x01);
    CHECK(encoded[40] == 0x44 && encoded[41] == 0x33 &&
          encoded[42] == 0x22 && encoded[43] == 0x11);
    CHECK(memory_is_value(encoded + 44, 4, 0));
    CHECK(encoded[48] == WORR_EVENT_ABI_VERSION && encoded[49] == 0 &&
          encoded[50] == 0 && encoded[51] == 0);
    CHECK(memory_is_value(encoded + 52, 4, 0));

    memset(&info, 0xcc, sizeof(info));
    CHECK(Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(info.struct_size == sizeof(info));
    CHECK(info.schema_version == WORR_NATIVE_CODEC_ABI_VERSION);
    CHECK(info.record_class ==
          WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
    CHECK(info.flags == 0);
    CHECK(info.record_schema_version == WORR_EVENT_STREAM_ABI_VERSION);
    CHECK(info.model_revision == WORR_EVENT_MODEL_REVISION);
    CHECK(info.encoded_bytes == EVENT_STREAM_ENCODED_BYTES);
    CHECK(info.fixed_body_bytes ==
          WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES);
    CHECK(info.range_counts[0] == 0 && info.range_counts[1] == 0 &&
          info.range_counts[2] == 0);
    CHECK(info.object_epoch == source.stream_epoch);
    CHECK(info.object_sequence == source.first_sequence);

    memset(&record_ref, 0xa5, sizeof(record_ref));
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &record_ref));
    CHECK(record_ref.record_class ==
          WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
    CHECK(record_ref.reserved0 == 0);
    CHECK(record_ref.record_schema_version ==
          WORR_EVENT_STREAM_ABI_VERSION);
    CHECK(record_ref.object_epoch == source.stream_epoch);
    CHECK(record_ref.object_sequence == source.first_sequence);

    memset(&decoded, 0x3c, sizeof(decoded));
    CHECK(Worr_NativeCodecEventStreamDecodeV1(
              encoded, encoded_bytes, &decoded) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_EventStreamDescriptorEqualV1(&source, &decoded));
    CHECK(memcmp(&source, &decoded, sizeof(source)) == 0);

    flagged = source;
    flagged.flags = WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2;
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &flagged, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(encoded[52] == WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2 &&
          encoded[53] == 0 && encoded[54] == 0 && encoded[55] == 0);
    CHECK(Worr_NativeCodecEventStreamDecodeV1(
              encoded, encoded_bytes, &decoded) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_EventStreamDescriptorEqualV1(&flagged, &decoded));
    return true;
}

static bool test_native_codec_arguments_and_aliasing(void)
{
    worr_event_stream_descriptor_v1 descriptor = make_descriptor(5, 9);
    worr_event_stream_descriptor_v1 malformed;
    worr_event_stream_descriptor_v1 descriptor_before;
    uint8_t output[64];
    uint8_t output_before[64];
    uint32_t preflight;
    size_t encoded_bytes;
    union descriptor_size_alias_u {
        worr_event_stream_descriptor_v1 descriptor;
        size_t encoded_bytes;
    } descriptor_size_alias;
    union descriptor_output_alias_u {
        worr_event_stream_descriptor_v1 descriptor;
        uint8_t bytes[64];
    } descriptor_output_alias;
    union decode_alias_u {
        max_align_t alignment;
        uint8_t bytes[64];
        worr_event_stream_descriptor_v1 descriptor;
    } decode_alias;
    uint8_t decode_alias_before[sizeof(decode_alias)];

    preflight = UINT32_C(0xdeadbeef);
    CHECK(Worr_NativeCodecEventStreamPreflightV1(NULL, &preflight) ==
          WORR_NATIVE_CODEC_INVALID_ARGUMENT);
    CHECK(preflight == UINT32_C(0xdeadbeef));
    CHECK(Worr_NativeCodecEventStreamPreflightV1(&descriptor, NULL) ==
          WORR_NATIVE_CODEC_INVALID_ARGUMENT);

    malformed = descriptor;
    malformed.flags = UINT16_C(0x8000);
    preflight = UINT32_C(0xdeadbeef);
    CHECK(Worr_NativeCodecEventStreamPreflightV1(
              &malformed, &preflight) == WORR_NATIVE_CODEC_INVALID_RECORD);
    CHECK(preflight == UINT32_C(0xdeadbeef));

    memset(output, 0x5a, sizeof(output));
    memcpy(output_before, output, sizeof(output));
    encoded_bytes = SIZE_MAX;
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &malformed, output, sizeof(output), &encoded_bytes) ==
          WORR_NATIVE_CODEC_INVALID_RECORD);
    CHECK(encoded_bytes == SIZE_MAX);
    CHECK(memcmp(output, output_before, sizeof(output)) == 0);

    memset(output, 0x5a, sizeof(output));
    memcpy(output_before, output, sizeof(output));
    encoded_bytes = SIZE_MAX;
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &descriptor, output, EVENT_STREAM_ENCODED_BYTES - 1u,
              &encoded_bytes) == WORR_NATIVE_CODEC_OUTPUT_TOO_SMALL);
    CHECK(encoded_bytes == SIZE_MAX);
    CHECK(memcmp(output, output_before, sizeof(output)) == 0);
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &descriptor, NULL, sizeof(output), &encoded_bytes) ==
          WORR_NATIVE_CODEC_INVALID_ARGUMENT);
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &descriptor, output, sizeof(output), NULL) ==
          WORR_NATIVE_CODEC_INVALID_ARGUMENT);

    descriptor_before = descriptor;
    CHECK(Worr_NativeCodecEventStreamPreflightV1(
              &descriptor, &descriptor.stream_epoch) ==
          WORR_NATIVE_CODEC_INVALID_ARGUMENT);
    CHECK(memcmp(&descriptor, &descriptor_before, sizeof(descriptor)) == 0);

    descriptor_size_alias.descriptor = make_descriptor(6, 10);
    descriptor_before = descriptor_size_alias.descriptor;
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &descriptor_size_alias.descriptor, output, sizeof(output),
              &descriptor_size_alias.encoded_bytes) ==
          WORR_NATIVE_CODEC_INVALID_ARGUMENT);
    CHECK(memcmp(&descriptor_size_alias.descriptor, &descriptor_before,
                 sizeof(descriptor_before)) == 0);

    descriptor_output_alias.descriptor = make_descriptor(7, 11);
    memcpy(output_before, descriptor_output_alias.bytes,
           sizeof(descriptor_output_alias.bytes));
    encoded_bytes = SIZE_MAX;
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &descriptor_output_alias.descriptor,
              descriptor_output_alias.bytes,
              sizeof(descriptor_output_alias.bytes), &encoded_bytes) ==
          WORR_NATIVE_CODEC_INVALID_ARGUMENT);
    CHECK(encoded_bytes == SIZE_MAX);
    CHECK(memcmp(descriptor_output_alias.bytes, output_before,
                 sizeof(descriptor_output_alias.bytes)) == 0);

    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &descriptor, decode_alias.bytes, sizeof(decode_alias.bytes),
              &encoded_bytes) == WORR_NATIVE_CODEC_OK);
    memcpy(decode_alias_before, &decode_alias, sizeof(decode_alias));
    CHECK(Worr_NativeCodecEventStreamDecodeV1(
              decode_alias.bytes, encoded_bytes,
              &decode_alias.descriptor) == WORR_NATIVE_CODEC_INVALID_ARGUMENT);
    CHECK(memcmp(&decode_alias, decode_alias_before,
                 sizeof(decode_alias)) == 0);

    CHECK(check_decode_failure_untouched(
        NULL, EVENT_STREAM_ENCODED_BYTES,
        WORR_NATIVE_CODEC_INVALID_ARGUMENT));
    CHECK(Worr_NativeCodecEventStreamDecodeV1(output, sizeof(output), NULL) ==
          WORR_NATIVE_CODEC_INVALID_ARGUMENT);
    return true;
}

static bool test_native_codec_malformed_matrix(void)
{
    const worr_event_stream_descriptor_v1 descriptor =
        make_descriptor(71, 400);
    uint8_t encoded[128];
    uint8_t malformed[128];
    uint8_t other_class[128];
    worr_native_codec_info_v1 info;
    worr_native_codec_info_v1 info_before;
    size_t encoded_bytes;
    size_t truncate;
    uint32_t index;

    memset(encoded, 0, sizeof(encoded));
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &descriptor, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);

    memset(&info_before, 0x3c, sizeof(info_before));
    for (truncate = 0; truncate < encoded_bytes; ++truncate) {
        info = info_before;
        CHECK(Worr_NativeCodecInspectV1(encoded, truncate, &info) !=
              WORR_NATIVE_CODEC_OK);
        CHECK(memcmp(&info, &info_before, sizeof(info)) == 0);
        CHECK(check_decode_failure_untouched(
            encoded, truncate, WORR_NATIVE_CODEC_MALFORMED));
    }
    info = info_before;
    CHECK(Worr_NativeCodecInspectV1(
              encoded, encoded_bytes + 1u, &info) ==
          WORR_NATIVE_CODEC_MALFORMED);
    CHECK(memcmp(&info, &info_before, sizeof(info)) == 0);
    CHECK(check_decode_failure_untouched(
        encoded, encoded_bytes + 1u, WORR_NATIVE_CODEC_MALFORMED));

    for (index = 0; index < 16; ++index) {
        worr_native_codec_result_v1 expected = WORR_NATIVE_CODEC_MALFORMED;
        memcpy(malformed, encoded, encoded_bytes);
        switch (index) {
        case 0:
            malformed[0] = 'X';
            break;
        case 1:
            store_u16(malformed + 4,
                      WORR_NATIVE_CODEC_WIRE_VERSION + 1u);
            expected = WORR_NATIVE_CODEC_UNSUPPORTED;
            break;
        case 2:
            store_u16(malformed + 6,
                      WORR_NATIVE_CODEC_WIRE_HEADER_BYTES + 1u);
            expected = WORR_NATIVE_CODEC_UNSUPPORTED;
            break;
        case 3:
            malformed[8] = 99;
            expected = WORR_NATIVE_CODEC_UNSUPPORTED;
            break;
        case 4:
            malformed[9] = 1;
            break;
        case 5:
            store_u16(malformed + 10,
                      WORR_EVENT_STREAM_ABI_VERSION + 1u);
            expected = WORR_NATIVE_CODEC_UNSUPPORTED;
            break;
        case 6:
            store_u32(malformed + 12, WORR_EVENT_MODEL_REVISION + 1u);
            expected = WORR_NATIVE_CODEC_UNSUPPORTED;
            break;
        case 7:
            store_u32(malformed + 16,
                      (uint32_t)EVENT_STREAM_ENCODED_BYTES - 1u);
            break;
        case 8:
            store_u32(malformed + 20,
                      WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES - 1u);
            break;
        case 9:
            store_u32(malformed + 24, 1);
            break;
        case 10:
            store_u32(malformed + 28, 1);
            break;
        case 11:
            store_u32(malformed + 32, 1);
            break;
        case 12:
            store_u32(malformed + 36, 0);
            break;
        case 13:
            store_u32(malformed + 40, 0);
            break;
        case 14:
            store_u32(malformed + 44, 1);
            break;
        case 15:
            store_u32(malformed + 52, UINT32_MAX);
            break;
        default:
            CHECK(false);
        }
        CHECK(check_decode_failure_untouched(
            malformed, encoded_bytes, expected));
    }

    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 48, WORR_EVENT_ABI_VERSION + 1u);
    CHECK(check_decode_failure_untouched(
        malformed, encoded_bytes, WORR_NATIVE_CODEC_INVALID_RECORD));
    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 52, UINT32_C(0x8000));
    CHECK(check_decode_failure_untouched(
        malformed, encoded_bytes, WORR_NATIVE_CODEC_INVALID_RECORD));

    /* A structurally inspectable record from another class is unsupported by
     * this class decoder and must not be reinterpreted as a descriptor. */
    memset(other_class, 0, sizeof(other_class));
    memcpy(other_class, encoded, encoded_bytes);
    other_class[8] = WORR_NATIVE_RECORD_EVENT_V1;
    store_u16(other_class + 10, WORR_EVENT_ABI_VERSION);
    store_u32(other_class + 12, WORR_EVENT_MODEL_REVISION);
    store_u32(other_class + 16,
              WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                  WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES);
    store_u32(other_class + 20, WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES);
    CHECK(Worr_NativeCodecInspectV1(
              other_class,
              WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                  WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES,
              &info) == WORR_NATIVE_CODEC_OK);
    CHECK(check_decode_failure_untouched(
        other_class,
        WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
            WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES,
        WORR_NATIVE_CODEC_UNSUPPORTED));
    return true;
}

int main(void)
{
    if (!test_descriptor_initialization() ||
        !test_invalid_initialization_is_transactional() ||
        !test_descriptor_validation_matrix() ||
        !test_descriptor_equality() ||
        !test_native_codec_round_trip() ||
        !test_native_codec_arguments_and_aliasing() ||
        !test_native_codec_malformed_matrix()) {
        return 1;
    }
    puts("event stream descriptor tests passed");
    return 0;
}
