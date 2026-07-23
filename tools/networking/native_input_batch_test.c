/* Standalone FR-10-T16 private native input-batch codec tests. */

#include "common/net/native_input_batch.h"
#include "common/net/native_input_batch_sideband.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(stderr, "native_input_batch_test:%d: %s\n",          \
                    __LINE__, #expression);                                 \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    } while (0)

enum {
    TEST_BATCH_CRC_OFFSET = 28,
    TEST_NESTED_SEQUENCE_OFFSET = 40,
};

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
            crc = (crc >> 1) ^
                  (UINT32_C(0xedb88320) &
                   (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return crc;
}

static uint32_t batch_crc32(const uint8_t *bytes, size_t count)
{
    static const uint8_t zero_crc[4];
    uint32_t crc = UINT32_MAX;
    crc = crc32_update(crc, bytes, TEST_BATCH_CRC_OFFSET);
    crc = crc32_update(crc, zero_crc, sizeof(zero_crc));
    crc = crc32_update(
        crc, bytes + TEST_BATCH_CRC_OFFSET + sizeof(uint32_t),
        count - TEST_BATCH_CRC_OFFSET - sizeof(uint32_t));
    return ~crc;
}

static worr_command_record_v1 make_record(uint32_t epoch,
                                           uint32_t sequence,
                                           uint8_t marker)
{
    worr_command_record_v1 record;
    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id = (worr_command_id_v1){epoch, sequence};
    record.sample_time_us = (uint64_t)sequence * UINT64_C(10000);
    record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    record.command.struct_size = sizeof(record.command);
    record.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    record.command.duration_ms = 10;
    record.command.buttons = marker;
    record.command.view_angles[0] = (float)marker;
    record.command.forward_move = (float)(int8_t)marker;
    record.command.side_move = -(float)(int8_t)marker;
    record.render_watermark.struct_size = sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    record.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_NONE;
    CHECK(Worr_CommandRecordCanonicalizeV1(
        &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    return record;
}

static void make_records(worr_command_record_v1 *records,
                         uint32_t count, uint32_t epoch,
                         uint32_t first_sequence)
{
    uint32_t index;
    for (index = 0; index < count; ++index) {
        records[index] = make_record(
            epoch, first_sequence + index, (uint8_t)(index + 7u));
    }
}

static void test_sizes_round_trip_and_hostile_capacity(void)
{
    worr_command_record_v1 records[WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS];
    worr_command_record_v1 records_before[WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS];
    worr_command_record_v1 decoded[WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS];
    worr_native_input_batch_info_v1 info;
    worr_native_input_batch_info_v1 decoded_info;
    uint8_t encoded[WORR_NATIVE_INPUT_BATCH_MAX_PAYLOAD_BYTES];
    uint8_t encoded_before[sizeof(encoded)];
    size_t encoded_bytes;
    uint32_t index;

    CHECK(Worr_NativeInputBatchWireBytesV1(1) == 0);
    CHECK(Worr_NativeInputBatchWireBytesV1(2) == 252);
    CHECK(Worr_NativeInputBatchWireBytesV1(8) == 912);
    CHECK(Worr_NativeInputBatchWireBytesV1(9) == 0);

    make_records(records, 8, 91, 100);
    memcpy(records_before, records, sizeof(records));
    memset(encoded, 0xa5, sizeof(encoded));
    memset(&info, 0, sizeof(info));
    encoded_bytes = 0;
    CHECK(Worr_NativeInputBatchEncodeV1(
              records, 8, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
              encoded, sizeof(encoded), &encoded_bytes, &info) ==
          WORR_NATIVE_INPUT_BATCH_OK);
    CHECK(encoded_bytes == sizeof(encoded) && info.encoded_bytes == 912 &&
          info.command_count == 8 && info.command_epoch == 91 &&
          info.first_sequence == 100 && info.last_sequence == 107 &&
          info.command_record_bytes == 110);
    CHECK(memcmp(records, records_before, sizeof(records)) == 0);
    memcpy(encoded_before, encoded, sizeof(encoded));

    memset(decoded, 0x5a, sizeof(decoded));
    memset(&decoded_info, 0x5a, sizeof(decoded_info));
    /* UINT32_MAX is intentional.  Decode must derive its overlap span from
     * the inspected 2..8 record count, never from hostile caller capacity.
     * This is a regression proof for both native and i686 builds. */
    CHECK(Worr_NativeInputBatchDecodeV1(
              encoded, encoded_bytes,
              WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, decoded,
              UINT32_MAX, &decoded_info) == WORR_NATIVE_INPUT_BATCH_OK);
    CHECK(memcmp(decoded, records, sizeof(decoded)) == 0);
    CHECK(memcmp(&decoded_info, &info, sizeof(info)) == 0);
    CHECK(memcmp(encoded, encoded_before, sizeof(encoded)) == 0);

    make_records(records, 2, 92, 1);
    memset(encoded, 0, sizeof(encoded));
    encoded_bytes = 0;
    CHECK(Worr_NativeInputBatchEncodeV1(
              records, 2, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
              encoded, sizeof(encoded), &encoded_bytes, &info) ==
          WORR_NATIVE_INPUT_BATCH_OK);
    CHECK(encoded_bytes == 252 && info.command_count == 2);
    memset(decoded, 0, sizeof(decoded));
    CHECK(Worr_NativeInputBatchDecodeV1(
              encoded, encoded_bytes,
              WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, decoded, 2,
              &decoded_info) == WORR_NATIVE_INPUT_BATCH_OK);
    for (index = 0; index < 2; ++index)
        CHECK(memcmp(&decoded[index], &records[index], sizeof(records[index])) == 0);
}

static void test_transactional_failures(void)
{
    worr_command_record_v1 records[3];
    worr_command_record_v1 decoded[WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS];
    worr_command_record_v1 decoded_before[WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS];
    worr_native_input_batch_info_v1 info;
    worr_native_input_batch_info_v1 info_before;
    uint8_t encoded[WORR_NATIVE_INPUT_BATCH_MAX_PAYLOAD_BYTES];
    uint8_t output_before[sizeof(encoded)];
    size_t encoded_bytes;
    size_t encoded_bytes_before;
    size_t second_sequence_offset;

    make_records(records, 3, 101, 10);
    records[1].command_id.sequence = 12;
    CHECK(Worr_CommandRecordCanonicalizeV1(
        &records[1], WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    memset(encoded, 0xa5, sizeof(encoded));
    memcpy(output_before, encoded, sizeof(encoded));
    encoded_bytes = SIZE_MAX;
    encoded_bytes_before = encoded_bytes;
    memset(&info, 0x6b, sizeof(info));
    memcpy(&info_before, &info, sizeof(info));
    CHECK(Worr_NativeInputBatchEncodeV1(
              records, 3, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
              encoded, sizeof(encoded), &encoded_bytes, &info) ==
          WORR_NATIVE_INPUT_BATCH_INVALID_RECORD);
    CHECK(memcmp(encoded, output_before, sizeof(encoded)) == 0);
    CHECK(encoded_bytes == encoded_bytes_before);
    CHECK(memcmp(&info, &info_before, sizeof(info)) == 0);

    make_records(records, 3, 102, 20);
    CHECK(Worr_NativeInputBatchEncodeV1(
              records, 3, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
              encoded, sizeof(encoded), &encoded_bytes, &info) ==
          WORR_NATIVE_INPUT_BATCH_OK);
    second_sequence_offset =
        WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES +
        WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES +
        TEST_NESTED_SEQUENCE_OFFSET;
    write_u32_le(encoded + second_sequence_offset, 29);
    write_u32_le(encoded + TEST_BATCH_CRC_OFFSET,
                 batch_crc32(encoded, encoded_bytes));
    CHECK(Worr_NativeInputBatchInspectV1(
              encoded, encoded_bytes, &info) == WORR_NATIVE_INPUT_BATCH_OK);
    memset(decoded, 0xc3, sizeof(decoded));
    memcpy(decoded_before, decoded, sizeof(decoded));
    memset(&info, 0xd4, sizeof(info));
    memcpy(&info_before, &info, sizeof(info));
    CHECK(Worr_NativeInputBatchDecodeV1(
              encoded, encoded_bytes,
              WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, decoded,
              WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS, &info) ==
          WORR_NATIVE_INPUT_BATCH_IDENTITY_MISMATCH);
    CHECK(memcmp(decoded, decoded_before, sizeof(decoded)) == 0);
    CHECK(memcmp(&info, &info_before, sizeof(info)) == 0);

    /* CRC rejection is equally transactional for inspection output. */
    encoded[encoded_bytes - 1u] ^= 1u;
    memset(&info, 0xe5, sizeof(info));
    memcpy(&info_before, &info, sizeof(info));
    CHECK(Worr_NativeInputBatchInspectV1(
              encoded, encoded_bytes, &info) ==
          WORR_NATIVE_INPUT_BATCH_CORRUPT);
    CHECK(memcmp(&info, &info_before, sizeof(info)) == 0);
}

static void test_sideband_round_trip_and_boundaries(void)
{
    worr_native_input_batch_confirm_v1 confirm;
    worr_native_input_batch_confirm_v1 decoded;
    worr_native_input_batch_setting_pair_v1 pairs[
        WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT];
    worr_native_input_batch_setting_pair_v1 pairs_before[
        WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT];
    worr_native_input_batch_sideband_parser_v1 parser;
    uint32_t index;

    CHECK(Worr_NativeInputBatchConfirmInitV1(&confirm, 77, 901));
    CHECK(Worr_NativeInputBatchConfirmValidateV1(&confirm));
    CHECK(Worr_NativeInputBatchConfirmEncodeV1(
        &confirm, pairs, WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT));
    CHECK(pairs[0].index == WORR_NATIVE_INPUT_BATCH_SETTING_BEGIN);
    CHECK(pairs[WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT - 1u].index ==
          WORR_NATIVE_INPUT_BATCH_SETTING_COMMIT);
    CHECK(Worr_NativeInputBatchSettingRecognizedV1(-31770));
    CHECK(Worr_NativeInputBatchSettingRecognizedV1(-31764));
    CHECK(!Worr_NativeInputBatchSettingRecognizedV1(-31771));
    CHECK(!Worr_NativeInputBatchSettingRecognizedV1(-31763));

    CHECK(Worr_NativeInputBatchSidebandParserInitV1(&parser));
    CHECK(Worr_NativeInputBatchSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_STARTED);
    for (index = 0; index < WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT;
         ++index) {
        CHECK(Worr_NativeInputBatchSidebandObserveSettingV1(
                  &parser, pairs[index].index, pairs[index].value) ==
              (index + 1u == WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT
                   ? WORR_NATIVE_INPUT_BATCH_SIDEBAND_RECORD_COMMITTED
                   : WORR_NATIVE_INPUT_BATCH_SIDEBAND_FIELD_ACCEPTED));
    }
    memset(&decoded, 0, sizeof(decoded));
    CHECK(Worr_NativeInputBatchSidebandTakeRecordV1(&parser, &decoded) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_RECORD_TAKEN);
    CHECK(Worr_NativeInputBatchConfirmEqualV1(&confirm, &decoded));
    CHECK(Worr_NativeInputBatchSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_ENDED);

    /* Short-capacity encoding and parser boundary failures must not leak a
     * partial private declaration into a later service-message run. */
    memset(pairs, 0x8a, sizeof(pairs));
    memcpy(pairs_before, pairs, sizeof(pairs));
    CHECK(!Worr_NativeInputBatchConfirmEncodeV1(
        &confirm, pairs, WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT - 1u));
    CHECK(memcmp(pairs, pairs_before, sizeof(pairs)) == 0);
    CHECK(Worr_NativeInputBatchConfirmEncodeV1(
        &confirm, pairs, WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT));

    CHECK(Worr_NativeInputBatchSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_STARTED);
    CHECK(Worr_NativeInputBatchSidebandObserveSettingV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_NativeInputBatchSidebandObserveInterveningServiceV1(
              &parser) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_INTERVENING_SERVICE);
    CHECK(Worr_NativeInputBatchSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_BOUNDARY);

    CHECK(Worr_NativeInputBatchSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_STARTED);
    CHECK(Worr_NativeInputBatchSidebandObserveSettingV1(
              &parser, pairs[1].index, pairs[1].value) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_UNEXPECTED_FIELD);
    CHECK(Worr_NativeInputBatchSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_BOUNDARY);

    CHECK(Worr_NativeInputBatchSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_STARTED);
    CHECK(Worr_NativeInputBatchSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_BOUNDARY);
    CHECK(Worr_NativeInputBatchSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_ENDED);
}

int main(void)
{
    test_sizes_round_trip_and_hostile_capacity();
    test_transactional_failures();
    test_sideband_round_trip_and_boundaries();
    puts("native_input_batch_test: ok");
    return EXIT_SUCCESS;
}
