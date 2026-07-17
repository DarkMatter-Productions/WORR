/* Standalone FR-10-T04 native-readiness signed-setting carrier tests. */

#include "common/net/native_readiness_sideband.h"

#include "common/net/consumed_cursor_sideband.h"
#include "common/net/demo_clock_sideband.h"
#include "common/net/legacy_command_adapter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(stderr,                                                 \
                    "native_readiness_sideband_test:%d: %s\n",           \
                    __LINE__, #expression);                                 \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    } while (0)

#define NATIVE_CAPABILITIES WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK
#define EVENT_NATIVE_CAPABILITIES WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK
#define SNAPSHOT_NATIVE_CAPABILITIES WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK
#define EVENT_SNAPSHOT_NATIVE_CAPABILITIES                                \
    ((uint32_t)(WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK |                  \
                WORR_NET_CAP_CANONICAL_SNAPSHOT_V2))

static void fill_bytes(void *object, size_t bytes, unsigned char value)
{
    memset(object, value, bytes);
}

static uint16_t pair_word(int16_t value)
{
    uint16_t word;
    memcpy(&word, &value, sizeof(word));
    return word;
}

static void pair_set_word(int16_t *value, uint16_t word)
{
    memcpy(value, &word, sizeof(word));
}

static void make_record(uint16_t kind,
                        worr_native_readiness_record_v1 *record)
{
    CHECK(Worr_NativeReadinessRecordInitV1(
        record, kind, UINT32_C(0x89abcdef),
        kind == WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM
            ? EVENT_NATIVE_CAPABILITIES : NATIVE_CAPABILITIES,
        UINT64_C(0xfedcba9876543210)));
}

static void make_pairs(
    uint16_t kind,
    worr_native_readiness_record_v1 *record,
    worr_native_readiness_setting_pair_v1 *pairs)
{
    make_record(kind, record);
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        record, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
}

static worr_native_readiness_sideband_result_v1 observe_pair(
    worr_native_readiness_sideband_parser_v1 *parser,
    const worr_native_readiness_setting_pair_v1 *pair,
    bool svc)
{
    if (svc) {
        return Worr_NativeReadinessSidebandObserveSvcSettingV1(
            parser, (int32_t)pair->index, (int32_t)pair->value);
    }
    return Worr_NativeReadinessSidebandObservePairV1(
        parser, pair->index, pair->value);
}

static void begin_parser(
    worr_native_readiness_sideband_parser_v1 *parser)
{
    CHECK(Worr_NativeReadinessSidebandParserInitV1(parser));
    CHECK(Worr_NativeReadinessSidebandParserValidateV1(parser));
    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED);
}

static void feed_complete(
    worr_native_readiness_sideband_parser_v1 *parser,
    const worr_native_readiness_setting_pair_v1 *pairs,
    bool svc)
{
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        const worr_native_readiness_sideband_result_v1 result =
            observe_pair(parser, &pairs[index], svc);
        CHECK(result ==
              (index + 1 == WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT
                   ? WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED
                   : WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED));
        CHECK(Worr_NativeReadinessSidebandParserValidateV1(parser));
    }
}

static void carrier_round_trip(
    const worr_native_readiness_record_v1 *record,
    bool svc,
    worr_native_readiness_record_v1 *record_out)
{
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_sideband_parser_v1 parser;

    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        record, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    begin_parser(&parser);
    feed_complete(&parser, pairs, svc);
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(
              &parser, record_out) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
}

static void test_reserved_range(void)
{
    static const int expected[WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT] = {
        WORR_NATIVE_READINESS_SETTING_BEGIN,
        WORR_NATIVE_READINESS_SETTING_KIND,
        WORR_NATIVE_READINESS_SETTING_EPOCH_LOW,
        WORR_NATIVE_READINESS_SETTING_EPOCH_HIGH,
        WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_LOW,
        WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_HIGH,
        WORR_NATIVE_READINESS_SETTING_CAPABILITIES_LOW,
        WORR_NATIVE_READINESS_SETTING_CAPABILITIES_HIGH,
        WORR_NATIVE_READINESS_SETTING_NONCE_WORD0,
        WORR_NATIVE_READINESS_SETTING_NONCE_WORD1,
        WORR_NATIVE_READINESS_SETTING_NONCE_WORD2,
        WORR_NATIVE_READINESS_SETTING_NONCE_WORD3,
        WORR_NATIVE_READINESS_SETTING_CHECKSUM_LOW,
        WORR_NATIVE_READINESS_SETTING_CHECKSUM_HIGH,
        WORR_NATIVE_READINESS_SETTING_COMMIT,
    };
    uint32_t index;

    CHECK(WORR_NATIVE_READINESS_SETTING_BEGIN == -31980);
    CHECK(WORR_NATIVE_READINESS_SETTING_COMMIT == -31966);
    for (index = 1;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        CHECK(expected[index] == expected[index - 1] + 1);
    }

    CHECK(WORR_LEGACY_COMMAND_SETTING_BEGIN == -32000);
    CHECK(WORR_LEGACY_COMMAND_SETTING_COMMIT == -31992);
    CHECK(WORR_LEGACY_COMMAND_SETTING_COMMIT <
          WORR_NATIVE_READINESS_SETTING_BEGIN);
    CHECK(WORR_NATIVE_READINESS_SETTING_COMMIT <
          WORR_NET_CAPABILITY_CONFIRM_EPOCH_SETTING);
    CHECK(WORR_NET_CAPABILITY_CONFIRM_EPOCH_SETTING == -31799);
    CHECK(WORR_NET_CAPABILITY_CONFIRM_NEGOTIATED_SETTING == -31797);
    CHECK(WORR_NATIVE_READINESS_SETTING_COMMIT <
          WORR_CONSUMED_CURSOR_SETTING_BEGIN);
    CHECK(WORR_CONSUMED_CURSOR_SETTING_BEGIN == -31790);
    CHECK(WORR_CONSUMED_CURSOR_SETTING_COMMIT == -31786);
    CHECK(WORR_NATIVE_READINESS_SETTING_COMMIT <
          WORR_DEMO_CLOCK_SETTING_BEGIN);
    CHECK(WORR_DEMO_CLOCK_SETTING_BEGIN == -31780);
    CHECK(WORR_DEMO_CLOCK_SETTING_COMMIT == -31775);
}

static void test_encode_and_transactionality(void)
{
    static const uint16_t golden_words[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT] = {
        UINT16_C(0x0001), UINT16_C(0x0001), UINT16_C(0xcdef),
        UINT16_C(0x89ab), UINT16_C(0x0000), UINT16_C(0x0000),
        UINT16_C(0x0053), UINT16_C(0x0000), UINT16_C(0x3210),
        UINT16_C(0x7654), UINT16_C(0xba98), UINT16_C(0xfedc),
        UINT16_C(0x0c0a), UINT16_C(0x5d6c), UINT16_C(0xe770),
    };
    worr_native_readiness_record_v1 record;
    worr_native_readiness_record_v1 bad;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_setting_pair_v1 before[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    union alias_storage_u {
        worr_native_readiness_record_v1 record;
        worr_native_readiness_setting_pair_v1 pairs[
            WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    } alias_storage;
    unsigned char alias_before[sizeof(alias_storage)];
    uint32_t index;

    make_pairs(WORR_NATIVE_READINESS_RECORD_CHALLENGE, &record, pairs);
    CHECK(record.record_checksum == UINT32_C(0x5d6c0c0a));
    CHECK(pairs[0].index == WORR_NATIVE_READINESS_SETTING_BEGIN);
    CHECK(pairs[0].value ==
          (int16_t)WORR_NATIVE_READINESS_SIDEBAND_VERSION);
    CHECK(pairs[1].index == WORR_NATIVE_READINESS_SETTING_KIND);
    CHECK(pairs[1].value == WORR_NATIVE_READINESS_RECORD_CHALLENGE);
    CHECK(pairs[14].index == WORR_NATIVE_READINESS_SETTING_COMMIT);
    for (index = 1;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        CHECK(pairs[index].index == pairs[index - 1].index + 1);
    }
    for (index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        CHECK(pair_word(pairs[index].value) == golden_words[index]);
    }
    CHECK(pair_word(pairs[2].value) ==
          (uint16_t)record.transport_epoch);
    CHECK(pair_word(pairs[3].value) ==
          (uint16_t)(record.transport_epoch >> 16));
    CHECK(pair_word(pairs[4].value) ==
          (uint16_t)record.snapshot_epoch);
    CHECK(pair_word(pairs[5].value) ==
          (uint16_t)(record.snapshot_epoch >> 16));
    CHECK(pair_word(pairs[8].value) ==
          (uint16_t)record.readiness_nonce);
    CHECK(pair_word(pairs[11].value) ==
          (uint16_t)(record.readiness_nonce >> 48));
    CHECK(pair_word(pairs[12].value) ==
          (uint16_t)record.record_checksum);
    CHECK(pair_word(pairs[13].value) ==
          (uint16_t)(record.record_checksum >> 16));

    memcpy(before, pairs, sizeof(before));
    bad = record;
    bad.record_checksum ^= UINT32_C(1);
    CHECK(!Worr_NativeReadinessSidebandEncodeV1(
        &bad, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    CHECK(!memcmp(pairs, before, sizeof(before)));
    CHECK(!Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT - 1));
    CHECK(!memcmp(pairs, before, sizeof(before)));
    CHECK(!Worr_NativeReadinessSidebandEncodeV1(
        NULL, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    CHECK(!memcmp(pairs, before, sizeof(before)));

    fill_bytes(&alias_storage, sizeof(alias_storage), 0xa5);
    alias_storage.record = record;
    memcpy(alias_before, &alias_storage, sizeof(alias_storage));
    CHECK(!Worr_NativeReadinessSidebandEncodeV1(
        &alias_storage.record, alias_storage.pairs,
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    CHECK(!memcmp(&alias_storage, alias_before, sizeof(alias_storage)));
}

static void test_clc_and_svc_round_trip(void)
{
    uint16_t kind;

    for (kind = WORR_NATIVE_READINESS_RECORD_CHALLENGE;
         kind <= WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM;
         ++kind) {
        worr_native_readiness_record_v1 record;
        worr_native_readiness_record_v1 output;
        worr_native_readiness_setting_pair_v1 pairs[
            WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
        worr_native_readiness_sideband_parser_v1 parser;
        const bool svc = (kind & 1u) == 0;

        make_pairs(kind, &record, pairs);
        begin_parser(&parser);
        feed_complete(&parser, pairs, svc);
        CHECK(parser.phase ==
              WORR_NATIVE_READINESS_SIDEBAND_PHASE_RECORD);
        CHECK(parser.telemetry.fields_accepted ==
              WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT);
        CHECK(parser.telemetry.records_committed == 1);
        fill_bytes(&output, sizeof(output), 0xa5);
        CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, &output) ==
              WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
        CHECK(!memcmp(&output, &record, sizeof(output)));
        CHECK(Worr_NativeReadinessRecordValidateV1(&output));
        CHECK(parser.phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_IDLE);
        CHECK(parser.telemetry.records_taken == 1);
        CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
              WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
        CHECK(Worr_NativeReadinessSidebandParserValidateV1(&parser));
    }
}

static void test_snapshot_epoch_round_trip_and_binding_validation(void)
{
    static const uint32_t capabilities[] = {
        SNAPSHOT_NATIVE_CAPABILITIES,
        EVENT_SNAPSHOT_NATIVE_CAPABILITIES,
    };
    worr_native_readiness_record_v1 record;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_sideband_parser_v1 parser;
    uint32_t mode;
    uint32_t index;

    CHECK(SNAPSHOT_NATIVE_CAPABILITIES == UINT32_C(0x57));
    CHECK(EVENT_SNAPSHOT_NATIVE_CAPABILITIES == UINT32_C(0x77));
    for (mode = 0; mode < 2; ++mode) {
        CHECK(Worr_NativeReadinessRecordInitBoundV1(
            &record, WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM,
            UINT32_C(0x89abcdef), UINT32_C(0x2468ace0),
            capabilities[mode], UINT64_C(0xfedcba9876543210)));
        CHECK(Worr_NativeReadinessSidebandEncodeV1(
            &record, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
        CHECK(pair_word(pairs[4].value) == UINT16_C(0xace0));
        CHECK(pair_word(pairs[5].value) == UINT16_C(0x2468));
        carrier_round_trip(&record, mode != 0, &output);
        CHECK(!memcmp(&record, &output, sizeof(record)));
        CHECK(output.snapshot_epoch == UINT32_C(0x2468ace0));
    }

    make_pairs(WORR_NATIVE_READINESS_RECORD_CHALLENGE, &record, pairs);
    pair_set_word(&pairs[4].value, UINT16_C(1));
    begin_parser(&parser);
    for (index = 0; index < 7; ++index) {
        CHECK(observe_pair(&parser, &pairs[index], false) ==
              WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    }
    CHECK(observe_pair(&parser, &pairs[7], false) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_INVALID);
    CHECK(parser.phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED);
    CHECK(parser.telemetry.record_validation_failures == 1);

    CHECK(Worr_NativeReadinessRecordInitBoundV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE,
        UINT32_C(0x89abcdef), UINT32_C(0x2468ace0),
        SNAPSHOT_NATIVE_CAPABILITIES, UINT64_C(0xfedcba9876543210)));
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    pair_set_word(&pairs[4].value, UINT16_C(0));
    pair_set_word(&pairs[5].value, UINT16_C(0));
    begin_parser(&parser);
    for (index = 0; index < 7; ++index) {
        CHECK(observe_pair(&parser, &pairs[index], true) ==
              WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    }
    CHECK(observe_pair(&parser, &pairs[7], true) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_INVALID);
    CHECK(parser.phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED);
}

static void test_multiple_records_per_packet(void)
{
    worr_native_readiness_record_v1 first;
    worr_native_readiness_record_v1 second;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_setting_pair_v1 first_pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_setting_pair_v1 second_pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_sideband_parser_v1 parser;

    make_pairs(WORR_NATIVE_READINESS_RECORD_CHALLENGE,
               &first, first_pairs);
    make_pairs(WORR_NATIVE_READINESS_RECORD_CLIENT_READY,
               &second, second_pairs);
    begin_parser(&parser);
    feed_complete(&parser, first_pairs, false);
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, &output) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(!memcmp(&output, &first, sizeof(output)));
    feed_complete(&parser, second_pairs, true);
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, &output) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(!memcmp(&output, &second, sizeof(output)));
    CHECK(parser.telemetry.records_committed == 2);
    CHECK(parser.telemetry.records_taken == 2);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
}

static void test_readiness_handshake_through_sideband(void)
{
    const uint32_t epoch = UINT32_C(0x12345678);
    const uint64_t nonce = UINT64_C(0x1020304050607080);
    worr_native_readiness_state_v1 server;
    worr_native_readiness_state_v1 client;
    worr_native_readiness_state_v1 wrong_direction_client;
    worr_native_readiness_state_v1 stale_client;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 carried;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 duplicate_ready;
    worr_native_readiness_record_v1 server_active;

    CHECK(Worr_NativeReadinessServerInitV1(
              &server, epoch, NATIVE_CAPABILITIES, nonce, 0, 100,
              &challenge) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientInitV1(
              &client, epoch, NATIVE_CAPABILITIES, 0, 100) ==
          WORR_NATIVE_READINESS_OK);

    carrier_round_trip(&challenge, true, &carried);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &carried, 1, &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    carrier_round_trip(&challenge, true, &carried);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &carried, 2, &duplicate_ready) ==
          WORR_NATIVE_READINESS_EXACT_DUPLICATE);
    CHECK(memcmp(&duplicate_ready, &client_ready,
                 sizeof(client_ready)) == 0);

    carrier_round_trip(&client_ready, false, &carried);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &server, &carried, 3, &server_active) ==
          WORR_NATIVE_READINESS_OK);
    carrier_round_trip(&server_active, true, &carried);
    CHECK(Worr_NativeReadinessClientObserveServerActiveV1(
              &client, &carried, 4) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(&server, 5));
    CHECK(Worr_NativeReadinessCanTransmitNativeV1(&server, 5));
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(&client, 5));
    CHECK(Worr_NativeReadinessCanTransmitNativeV1(&client, 5));

    CHECK(Worr_NativeReadinessClientInitV1(
              &wrong_direction_client, epoch, NATIVE_CAPABILITIES, 0, 100) ==
          WORR_NATIVE_READINESS_OK);
    carrier_round_trip(&client_ready, false, &carried);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &wrong_direction_client, &carried, 1, &duplicate_ready) ==
          WORR_NATIVE_READINESS_WRONG_ORDER);
    CHECK(wrong_direction_client.phase ==
          WORR_NATIVE_READINESS_PHASE_FAILED);

    CHECK(Worr_NativeReadinessClientInitV1(
              &stale_client, epoch + 1u, NATIVE_CAPABILITIES, 0, 100) ==
          WORR_NATIVE_READINESS_OK);
    carrier_round_trip(&challenge, true, &carried);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &stale_client, &carried, 1, &duplicate_ready) ==
          WORR_NATIVE_READINESS_BINDING_MISMATCH);
    CHECK(stale_client.phase == WORR_NATIVE_READINESS_PHASE_FAILED);
}

static void test_packet_boundaries(void)
{
    worr_native_readiness_record_v1 record;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_sideband_parser_v1 parser;

    make_pairs(WORR_NATIVE_READINESS_RECORD_CHALLENGE, &record, pairs);
    CHECK(Worr_NativeReadinessSidebandParserInitV1(&parser));
    CHECK(Worr_NativeReadinessSidebandObservePairV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_NATIVE_READINESS_SIDEBAND_INVALID_STATE);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_INVALID_STATE);
    CHECK(parser.telemetry.invalid_state == 2);

    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED);
    CHECK(observe_pair(&parser, &pairs[0], false) ==
          WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    CHECK(observe_pair(&parser, &pairs[1], false) ==
          WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY);
    CHECK(parser.telemetry.dangling_sequences == 1);
    CHECK(parser.telemetry.packet_boundary_resets == 1);
    CHECK(!parser.packet_active);

    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED);
    CHECK(observe_pair(&parser, &pairs[0], false) ==
          WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY);
    CHECK(parser.packet_active);
    CHECK(parser.phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_IDLE);

    feed_complete(&parser, pairs, false);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY);
    CHECK(parser.telemetry.discarded_records == 1);
    CHECK(Worr_NativeReadinessSidebandParserValidateV1(&parser));
}

static void test_order_and_intervening_resets(void)
{
    worr_native_readiness_record_v1 record;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_sideband_parser_v1 parser;
    uint32_t skip;

    make_pairs(WORR_NATIVE_READINESS_RECORD_CLIENT_READY, &record, pairs);
    for (skip = 1;
         skip + 1 < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++skip) {
        uint32_t index;
        begin_parser(&parser);
        for (index = 0; index < skip; ++index) {
            CHECK(observe_pair(&parser, &pairs[index], false) ==
                  WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
        }
        CHECK(observe_pair(&parser, &pairs[skip + 1], false) ==
              WORR_NATIVE_READINESS_SIDEBAND_UNEXPECTED_FIELD);
        CHECK(parser.phase ==
              WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED);
        CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
              WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY);
    }

    begin_parser(&parser);
    CHECK(observe_pair(&parser, &pairs[1], false) ==
          WORR_NATIVE_READINESS_SIDEBAND_UNEXPECTED_FIELD);
    CHECK(Worr_NativeReadinessSidebandObservePairV1(
              &parser, 42, 17) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_INTERVENING_SERVICE);
    CHECK(parser.phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_IDLE);
    CHECK(Worr_NativeReadinessSidebandObservePairV1(&parser, 42, 17) ==
          WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND);

    CHECK(observe_pair(&parser, &pairs[0], false) ==
          WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    CHECK(observe_pair(&parser, &pairs[1], false) ==
          WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_NativeReadinessSidebandObserveInterveningServiceV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_INTERVENING_SERVICE);
    CHECK(parser.telemetry.dangling_sequences == 1);
    CHECK(Worr_NativeReadinessSidebandObserveInterveningServiceV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND);

    CHECK(observe_pair(&parser, &pairs[0], false) ==
          WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_NativeReadinessSidebandObservePairV1(
              &parser, WORR_LEGACY_COMMAND_SETTING_BEGIN, 1) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_INTERVENING_SERVICE);
    feed_complete(&parser, pairs, false);
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, &output) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(!memcmp(&output, &record, sizeof(output)));
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
}

static void test_svc_range_validation(void)
{
    worr_native_readiness_record_v1 record;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_sideband_parser_v1 parser;

    make_pairs(WORR_NATIVE_READINESS_RECORD_CHALLENGE, &record, pairs);
    begin_parser(&parser);
    CHECK(Worr_NativeReadinessSidebandObserveSvcSettingV1(
              &parser, 123456, INT32_MAX) ==
          WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND);
    CHECK(Worr_NativeReadinessSidebandObserveSvcSettingV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_NativeReadinessSidebandObserveSvcSettingV1(
              &parser, pairs[1].index, INT16_MAX + INT32_C(1)) ==
          WORR_NATIVE_READINESS_SIDEBAND_VALUE_OUT_OF_RANGE);
    CHECK(parser.phase ==
          WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED);
    CHECK(parser.telemetry.value_range_failures == 1);
    CHECK(Worr_NativeReadinessSidebandObserveSvcSettingV1(
              &parser, 123456, INT32_MIN) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_INTERVENING_SERVICE);

    CHECK(Worr_NativeReadinessSidebandObserveSvcSettingV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_NativeReadinessSidebandObserveSvcSettingV1(
              &parser, pairs[1].index, INT16_MIN - INT32_C(1)) ==
          WORR_NATIVE_READINESS_SIDEBAND_VALUE_OUT_OF_RANGE);
    CHECK(parser.telemetry.value_range_failures == 2);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY);
}

static void test_corrupt_records_and_commit(void)
{
    worr_native_readiness_record_v1 record;
    worr_native_readiness_setting_pair_v1 clean[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_setting_pair_v1 corrupt[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_sideband_parser_v1 parser;
    uint32_t index;

    make_pairs(WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE, &record, clean);

    memcpy(corrupt, clean, sizeof(corrupt));
    pair_set_word(&corrupt[12].value,
                  pair_word(corrupt[12].value) ^ UINT16_C(1));
    begin_parser(&parser);
    for (index = 0; index <= 12; ++index) {
        CHECK(observe_pair(&parser, &corrupt[index], false) ==
              WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    }
    CHECK(observe_pair(&parser, &corrupt[13], false) ==
          WORR_NATIVE_READINESS_SIDEBAND_CHECKSUM_MISMATCH);
    CHECK(parser.telemetry.checksum_failures == 1);
    CHECK(parser.phase ==
          WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED);

    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY);
    memcpy(corrupt, clean, sizeof(corrupt));
    pair_set_word(&corrupt[14].value,
                  pair_word(corrupt[14].value) ^ UINT16_C(1));
    for (index = 0; index < 14; ++index) {
        CHECK(observe_pair(&parser, &corrupt[index], true) ==
              WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    }
    CHECK(observe_pair(&parser, &corrupt[14], true) ==
          WORR_NATIVE_READINESS_SIDEBAND_COMMIT_MISMATCH);
    CHECK(parser.telemetry.commit_failures == 1);

    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY);
    CHECK(Worr_NativeReadinessSidebandObservePairV1(
              &parser, clean[0].index, 2) ==
          WORR_NATIVE_READINESS_SIDEBAND_UNSUPPORTED_VERSION);
    CHECK(parser.telemetry.unsupported_versions == 1);

    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY);
    CHECK(observe_pair(&parser, &clean[0], false) ==
          WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_NativeReadinessSidebandObservePairV1(
              &parser, clean[1].index, 0) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_INVALID);
    CHECK(parser.telemetry.record_validation_failures == 1);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY);
}

static void test_every_single_value_bit_is_rejected(void)
{
    worr_native_readiness_record_v1 record;
    worr_native_readiness_setting_pair_v1 clean[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_setting_pair_v1 corrupt[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    uint32_t pair_index;
    unsigned bit;

    make_pairs(WORR_NATIVE_READINESS_RECORD_CHALLENGE, &record, clean);
    for (pair_index = 0;
         pair_index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++pair_index) {
        for (bit = 0; bit < 16; ++bit) {
            worr_native_readiness_sideband_parser_v1 parser;
            bool rejected = false;
            uint32_t observed;

            memcpy(corrupt, clean, sizeof(corrupt));
            pair_set_word(&corrupt[pair_index].value,
                          pair_word(corrupt[pair_index].value) ^
                              (uint16_t)(UINT16_C(1) << bit));
            begin_parser(&parser);
            for (observed = 0;
                 observed < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
                 ++observed) {
                const worr_native_readiness_sideband_result_v1 result =
                    observe_pair(&parser, &corrupt[observed], false);
                if (result ==
                    WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED) {
                    continue;
                }
                CHECK(result !=
                      WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED);
                rejected = true;
                break;
            }
            CHECK(rejected);
            if (parser.phase !=
                WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED) {
                fprintf(stderr,
                        "single-bit rejection pair=%u bit=%u phase=%u "
                        "observed=%u\n",
                        pair_index, bit, parser.phase, observed);
            }
            CHECK(parser.phase ==
                  WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED);
            CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
                  WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY);
        }
    }
}

static void test_take_transactionality_and_alias(void)
{
    worr_native_readiness_record_v1 record;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_record_v1 output_before;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_sideband_parser_v1 parser;
    worr_native_readiness_sideband_parser_v1 parser_before;
    union parser_alias_u {
        worr_native_readiness_sideband_parser_v1 parser;
        unsigned char bytes[sizeof(worr_native_readiness_sideband_parser_v1)];
    } alias;
    unsigned char alias_before[sizeof(alias)];
    worr_native_readiness_record_v1 *overlap;

    make_pairs(WORR_NATIVE_READINESS_RECORD_CHALLENGE, &record, pairs);
    begin_parser(&parser);
    fill_bytes(&output, sizeof(output), 0x4d);
    output_before = output;
    parser_before = parser;
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, &output) ==
          WORR_NATIVE_READINESS_SIDEBAND_NO_RECORD);
    CHECK(!memcmp(&output, &output_before, sizeof(output)));
    CHECK(!memcmp(&parser, &parser_before, sizeof(parser)));

    feed_complete(&parser, pairs, false);
    alias.parser = parser;
    memcpy(alias_before, &alias, sizeof(alias));
    overlap = (worr_native_readiness_record_v1 *)(void *)(alias.bytes + 16);
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(
              &alias.parser, overlap) ==
          WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT);
    CHECK(!memcmp(&alias, alias_before, sizeof(alias)));

    parser_before = parser;
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, NULL) ==
          WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT);
    CHECK(parser.telemetry.invalid_arguments ==
          parser_before.telemetry.invalid_arguments + 1);
    CHECK(parser.phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_RECORD);
    CHECK(!memcmp(&output, &output_before, sizeof(output)));

    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, &output) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(!memcmp(&output, &record, sizeof(output)));
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
}

static void test_validation_and_saturation(void)
{
    worr_native_readiness_sideband_parser_v1 parser;
    worr_native_readiness_sideband_parser_v1 invalid;

    begin_parser(&parser);
    parser.telemetry.settings_seen = UINT64_MAX;
    parser.telemetry.non_sideband_settings = UINT64_MAX;
    CHECK(Worr_NativeReadinessSidebandObservePairV1(&parser, 1, 2) ==
          WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND);
    CHECK(parser.telemetry.settings_seen == UINT64_MAX);
    CHECK(parser.telemetry.non_sideband_settings == UINT64_MAX);
    parser.telemetry.packet_ends = UINT64_MAX;
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
    CHECK(parser.telemetry.packet_ends == UINT64_MAX);

    invalid = parser;
    invalid.struct_size = 0;
    CHECK(!Worr_NativeReadinessSidebandParserValidateV1(&invalid));
    invalid = parser;
    invalid.phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED;
    CHECK(!Worr_NativeReadinessSidebandParserValidateV1(&invalid));
    invalid = parser;
    invalid.reserved1 = 1;
    CHECK(!Worr_NativeReadinessSidebandParserValidateV1(&invalid));
    CHECK(!Worr_NativeReadinessSidebandParserValidateV1(NULL));
    CHECK(!Worr_NativeReadinessSidebandParserInitV1(NULL));
    CHECK(Worr_NativeReadinessSidebandObservePairV1(NULL, 0, 0) ==
          WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT);
    CHECK(Worr_NativeReadinessSidebandObserveSvcSettingV1(NULL, 0, 0) ==
          WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT);
    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(NULL) ==
          WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT);
}

int main(void)
{
    test_reserved_range();
    test_encode_and_transactionality();
    test_clc_and_svc_round_trip();
    test_snapshot_epoch_round_trip_and_binding_validation();
    test_multiple_records_per_packet();
    test_readiness_handshake_through_sideband();
    test_packet_boundaries();
    test_order_and_intervening_resets();
    test_svc_range_validation();
    test_corrupt_records_and_commit();
    test_every_single_value_bit_is_rejected();
    test_take_transactionality_and_alias();
    test_validation_and_saturation();
    puts("native_readiness_sideband_test: ok");
    return EXIT_SUCCESS;
}
