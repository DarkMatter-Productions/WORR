/* Standalone FR-10-T04 native endpoint-readiness state-machine tests. */

#include "common/net/native_readiness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(stderr, "native_readiness_test:%d: %s\n", __LINE__,  \
                    #expression);                                           \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    } while (0)

#define NATIVE_CAPABILITIES WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK
#define EVENT_NATIVE_CAPABILITIES WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK
#define SNAPSHOT_NATIVE_CAPABILITIES WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK
#define EVENT_SNAPSHOT_NATIVE_CAPABILITIES                                \
    ((uint32_t)(WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK |                  \
                WORR_NET_CAP_CANONICAL_SNAPSHOT_V2))
#define COMMAND_WITHOUT_EPOCH_CANCEL                                       \
    ((uint32_t)(WORR_NET_CAP_LEGACY_STAGE_MASK |                           \
                WORR_NET_CAP_NATIVE_ENVELOPE_V1))
#define EVENT_WITHOUT_EPOCH_CANCEL                                         \
    ((uint32_t)(COMMAND_WITHOUT_EPOCH_CANCEL |                            \
                WORR_NET_CAP_NATIVE_EVENT_STREAM_V1))
#define CANCEL_WITHOUT_NATIVE_ENVELOPE                                     \
    ((uint32_t)(WORR_NET_CAP_LEGACY_STAGE_MASK |                           \
                WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1))
#define UNKNOWN_CAPABILITY (UINT32_C(1) << 31)

typedef struct readiness_fixture_s {
    worr_native_readiness_state_v1 server;
    worr_native_readiness_state_v1 client;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
} readiness_fixture;

static void fill_bytes(void *object, size_t bytes, unsigned char value)
{
    memset(object, value, bytes);
}

static void fixture_begin(readiness_fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    CHECK(Worr_NativeReadinessServerInitV1(
              &fixture->server, 7, NATIVE_CAPABILITIES, UINT64_C(100),
              UINT64_C(1000), UINT64_C(100), &fixture->challenge) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientInitV1(
              &fixture->client, 7, NATIVE_CAPABILITIES, UINT64_C(1000),
              UINT64_C(100)) == WORR_NATIVE_READINESS_OK);
}

static void fixture_client_ready(readiness_fixture *fixture)
{
    fixture_begin(fixture);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture->client, &fixture->challenge, UINT64_C(1010),
              &fixture->client_ready) == WORR_NATIVE_READINESS_OK);
}

static void fixture_complete(readiness_fixture *fixture)
{
    fixture_client_ready(fixture);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &fixture->server, &fixture->client_ready, UINT64_C(1020),
              &fixture->server_active) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveServerActiveV1(
              &fixture->client, &fixture->server_active, UINT64_C(1030)) ==
          WORR_NATIVE_READINESS_OK);
}

static void test_record_validation(void)
{
    worr_native_readiness_record_v1 record;
    worr_native_readiness_record_v1 altered;
    worr_native_readiness_record_v1 before;

    fill_bytes(&record, sizeof(record), 0xa5);
    CHECK(Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        NATIVE_CAPABILITIES, UINT64_C(100)));
    CHECK(Worr_NativeReadinessRecordValidateV1(&record));
    CHECK(record.struct_size == sizeof(record));
    CHECK(record.schema_version == WORR_NATIVE_READINESS_ABI_VERSION);
    CHECK(record.snapshot_epoch == 0);
    CHECK(record.record_checksum != 0);

#define CHECK_CORRUPT(member, expression)                                  \
    do {                                                                    \
        altered = record;                                                   \
        altered.member = (expression);                                      \
        CHECK(!Worr_NativeReadinessRecordValidateV1(&altered));            \
    } while (0)
    CHECK_CORRUPT(struct_size, 0);
    CHECK_CORRUPT(schema_version, 0);
    CHECK_CORRUPT(record_kind, 0);
    CHECK_CORRUPT(transport_epoch, 0);
    CHECK_CORRUPT(negotiated_capabilities, UINT32_C(0));
    CHECK_CORRUPT(readiness_nonce, UINT64_C(0));
    CHECK_CORRUPT(record_checksum, record.record_checksum ^ UINT32_C(1));
    CHECK_CORRUPT(snapshot_epoch, UINT32_C(1));
#undef CHECK_CORRUPT

    CHECK(!Worr_NativeReadinessRecordValidateV1(NULL));
    before = record;
    CHECK(!Worr_NativeReadinessRecordInitV1(
        &record, 0, 7, NATIVE_CAPABILITIES, UINT64_C(100)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
    CHECK(!Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 0,
        NATIVE_CAPABILITIES, UINT64_C(100)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
    CHECK(!Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        WORR_NET_CAP_LEGACY_STAGE_MASK, UINT64_C(100)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
    CHECK(!Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        COMMAND_WITHOUT_EPOCH_CANCEL, UINT64_C(100)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
    CHECK(!Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        EVENT_WITHOUT_EPOCH_CANCEL, UINT64_C(100)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
    CHECK(!Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        CANCEL_WITHOUT_NATIVE_ENVELOPE, UINT64_C(100)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
    CHECK(!Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        NATIVE_CAPABILITIES | UNKNOWN_CAPABILITY, UINT64_C(100)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
    CHECK(!Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        NATIVE_CAPABILITIES, UINT64_C(0)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
    CHECK(!Worr_NativeReadinessRecordInitV1(NULL,
        WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        NATIVE_CAPABILITIES, UINT64_C(100)));

    CHECK(!Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        SNAPSHOT_NATIVE_CAPABILITIES, UINT64_C(100)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
    CHECK(Worr_NativeReadinessRecordInitBoundV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7, 19,
        SNAPSHOT_NATIVE_CAPABILITIES, UINT64_C(100)));
    CHECK(record.snapshot_epoch == 19);
    CHECK(Worr_NativeReadinessRecordValidateV1(&record));
    altered = record;
    altered.snapshot_epoch = 18;
    CHECK(!Worr_NativeReadinessRecordValidateV1(&altered));
    CHECK(Worr_NativeReadinessRecordInitBoundV1(
        &record, WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM,
        7, 20, EVENT_SNAPSHOT_NATIVE_CAPABILITIES, UINT64_C(100)));
    CHECK(record.snapshot_epoch == 20);
    CHECK(Worr_NativeReadinessRecordValidateV1(&record));
    before = record;
    CHECK(!Worr_NativeReadinessRecordInitBoundV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7, 0,
        SNAPSHOT_NATIVE_CAPABILITIES, UINT64_C(100)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
    CHECK(!Worr_NativeReadinessRecordInitBoundV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7, 19,
        NATIVE_CAPABILITIES, UINT64_C(100)));
    CHECK(!memcmp(&record, &before, sizeof(record)));
}

static void test_capability_gating(void)
{
    readiness_fixture fixture;
    worr_native_readiness_state_v1 state;
    worr_native_readiness_state_v1 state_before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 challenge_before;

    fill_bytes(&state, sizeof(state), 0x26);
    fill_bytes(&challenge, sizeof(challenge), 0x62);
    state_before = state;
    challenge_before = challenge;
    CHECK(Worr_NativeReadinessServerInitV1(
              &state, 7, NATIVE_CAPABILITIES | UNKNOWN_CAPABILITY,
              UINT64_C(100), UINT64_C(1000), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&state, &state_before, sizeof(state)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));

    CHECK(Worr_NativeReadinessServerInitV1(
              &state, 7, COMMAND_WITHOUT_EPOCH_CANCEL, UINT64_C(100),
              UINT64_C(1000), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&state, &state_before, sizeof(state)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));

    CHECK(Worr_NativeReadinessServerInitV1(
              &state, 7, EVENT_WITHOUT_EPOCH_CANCEL, UINT64_C(100),
              UINT64_C(1000), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&state, &state_before, sizeof(state)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));

    CHECK(Worr_NativeReadinessServerInitV1(
              &state, 7, CANCEL_WITHOUT_NATIVE_ENVELOPE, UINT64_C(100),
              UINT64_C(1000), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&state, &state_before, sizeof(state)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));

    CHECK(Worr_NativeReadinessServerInitV1(
              &state, 7,
              WORR_NET_CAP_LEGACY_STAGE_MASK |
                  WORR_NET_CAP_NATIVE_EVENT_STREAM_V1,
              UINT64_C(100), UINT64_C(1000), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&state, &state_before, sizeof(state)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));

    CHECK(Worr_NativeReadinessClientInitV1(
              &state, 7, COMMAND_WITHOUT_EPOCH_CANCEL, UINT64_C(1000),
              UINT64_C(100)) == WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&state, &state_before, sizeof(state)));

    CHECK(Worr_NativeReadinessClientInitV1(
              &state, 7, NATIVE_CAPABILITIES | UNKNOWN_CAPABILITY,
              UINT64_C(1000), UINT64_C(100)) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&state, &state_before, sizeof(state)));

    fixture_complete(&fixture);
    state_before = fixture.server;
    challenge_before = challenge;
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, 8,
              NATIVE_CAPABILITIES | UNKNOWN_CAPABILITY, UINT64_C(200),
              UINT64_C(1040), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&fixture.server, &state_before, sizeof(state_before)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));

    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, 8, COMMAND_WITHOUT_EPOCH_CANCEL,
              UINT64_C(200), UINT64_C(1040), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&fixture.server, &state_before, sizeof(state_before)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));

    state_before = fixture.client;
    CHECK(Worr_NativeReadinessClientAdvanceEpochV1(
              &fixture.client, 8,
              NATIVE_CAPABILITIES | UNKNOWN_CAPABILITY, UINT64_C(1040),
              UINT64_C(100)) == WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&fixture.client, &state_before, sizeof(state_before)));
    CHECK(Worr_NativeReadinessClientAdvanceEpochV1(
              &fixture.client, 8, COMMAND_WITHOUT_EPOCH_CANCEL,
              UINT64_C(1040), UINT64_C(100)) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&fixture.client, &state_before, sizeof(state_before)));
}

static void test_handshake_and_gates(void)
{
    readiness_fixture fixture;

    fixture_begin(&fixture);
    CHECK(Worr_NativeReadinessStateValidateV1(&fixture.server));
    CHECK(Worr_NativeReadinessStateValidateV1(&fixture.client));
    CHECK(fixture.server.phase ==
          WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY);
    CHECK(fixture.client.phase ==
          WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE);
    CHECK(fixture.server.telemetry.challenges_emitted == 1);
    CHECK(fixture.challenge.negotiated_capabilities == NATIVE_CAPABILITIES);
    CHECK((fixture.challenge.negotiated_capabilities &
           WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1) != 0);
    CHECK(!Worr_NativeReadinessCanReceiveNativeV1(
        &fixture.server, UINT64_C(1000)));
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(
        &fixture.server, UINT64_C(1000)));
    CHECK(!Worr_NativeReadinessCanReceiveNativeV1(
        &fixture.client, UINT64_C(1000)));
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(
        &fixture.client, UINT64_C(1000)));

    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &fixture.challenge, UINT64_C(1010),
              &fixture.client_ready) == WORR_NATIVE_READINESS_OK);
    CHECK(fixture.client.phase ==
          WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE);
    CHECK(fixture.client.readiness_nonce == UINT64_C(100));
    CHECK(fixture.client.phase_start_tick == UINT64_C(1010));
    CHECK(fixture.client.deadline_tick == UINT64_C(1110));
    CHECK(fixture.client.telemetry.challenges_accepted == 1);
    CHECK(fixture.client.telemetry.client_ready_emitted == 1);
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(
        &fixture.client, UINT64_C(1010)));
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(
        &fixture.client, UINT64_C(1010)));
    CHECK(Worr_NativeReadinessRecordValidateV1(&fixture.client_ready));
    CHECK(fixture.client_ready.negotiated_capabilities ==
          NATIVE_CAPABILITIES);
    CHECK((fixture.client_ready.negotiated_capabilities &
           WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1) != 0);

    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &fixture.server, &fixture.client_ready, UINT64_C(1020),
              &fixture.server_active) == WORR_NATIVE_READINESS_OK);
    CHECK(fixture.server.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    CHECK(fixture.server.deadline_tick == 0);
    CHECK(fixture.server.telemetry.client_ready_accepted == 1);
    CHECK(fixture.server.telemetry.server_active_emitted == 1);
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(
        &fixture.server, UINT64_C(1020)));
    CHECK(Worr_NativeReadinessCanTransmitNativeV1(
        &fixture.server, UINT64_C(1020)));
    CHECK(Worr_NativeReadinessRecordValidateV1(&fixture.server_active));
    CHECK(fixture.server_active.negotiated_capabilities ==
          NATIVE_CAPABILITIES);

    CHECK(Worr_NativeReadinessClientObserveServerActiveV1(
              &fixture.client, &fixture.server_active, UINT64_C(1030)) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(fixture.client.phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    CHECK(fixture.client.deadline_tick == 0);
    CHECK(fixture.client.telemetry.server_active_accepted == 1);
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(
        &fixture.client, UINT64_C(1030)));
    CHECK(Worr_NativeReadinessCanTransmitNativeV1(
        &fixture.client, UINT64_C(1030)));
    CHECK(Worr_NativeReadinessStateValidateV1(&fixture.server));
    CHECK(Worr_NativeReadinessStateValidateV1(&fixture.client));
}

static void test_exact_duplicates(void)
{
    readiness_fixture fixture;
    worr_native_readiness_record_v1 repeated;

    fixture_client_ready(&fixture);
    fill_bytes(&repeated, sizeof(repeated), 0x5a);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &fixture.challenge, UINT64_C(1011),
              &repeated) == WORR_NATIVE_READINESS_EXACT_DUPLICATE);
    CHECK(!memcmp(&repeated, &fixture.client_ready, sizeof(repeated)));
    CHECK(repeated.negotiated_capabilities == NATIVE_CAPABILITIES);
    CHECK((repeated.negotiated_capabilities &
           WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1) != 0);
    CHECK(fixture.client.phase ==
          WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE);
    CHECK(fixture.client.telemetry.exact_duplicates == 1);
    CHECK(fixture.client.telemetry.client_ready_emitted == 2);

    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &fixture.server, &fixture.client_ready, UINT64_C(1020),
              &fixture.server_active) == WORR_NATIVE_READINESS_OK);
    fill_bytes(&repeated, sizeof(repeated), 0x5a);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &fixture.server, &fixture.client_ready, UINT64_C(1021),
              &repeated) == WORR_NATIVE_READINESS_EXACT_DUPLICATE);
    CHECK(!memcmp(&repeated, &fixture.server_active, sizeof(repeated)));
    CHECK(fixture.server.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    CHECK(fixture.server.telemetry.exact_duplicates == 1);
    CHECK(fixture.server.telemetry.server_active_emitted == 2);

    CHECK(Worr_NativeReadinessClientObserveServerActiveV1(
              &fixture.client, &fixture.server_active, UINT64_C(1030)) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveServerActiveV1(
              &fixture.client, &fixture.server_active, UINT64_C(1031)) ==
          WORR_NATIVE_READINESS_EXACT_DUPLICATE);
    CHECK(fixture.client.phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    CHECK(fixture.client.telemetry.exact_duplicates == 2);

    fill_bytes(&repeated, sizeof(repeated), 0x5a);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &fixture.challenge, UINT64_C(1032),
              &repeated) == WORR_NATIVE_READINESS_EXACT_DUPLICATE);
    CHECK(!memcmp(&repeated, &fixture.client_ready, sizeof(repeated)));
    CHECK(fixture.client.phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    CHECK(Worr_NativeReadinessStateValidateV1(&fixture.client));
}

static void check_failed(worr_native_readiness_state_v1 *state);

static void test_event_stream_active_confirmation_barrier(void)
{
    worr_native_readiness_state_v1 server;
    worr_native_readiness_state_v1 client;
    worr_native_readiness_state_v1 before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_record_v1 repeated_active;
    worr_native_readiness_record_v1 client_active_confirm;
    worr_native_readiness_record_v1 repeated_confirm;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_record_v1 output_before;

    CHECK(Worr_NativeReadinessServerInitV1(
              &server, 17, EVENT_NATIVE_CAPABILITIES, UINT64_C(900),
              UINT64_C(2000), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientInitV1(
              &client, 17, EVENT_NATIVE_CAPABILITIES, UINT64_C(2000),
              UINT64_C(100)) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &challenge, UINT64_C(2010), &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &server, &client_ready, UINT64_C(2020), &server_active) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(server.phase ==
          WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM);
    CHECK(server.phase_start_tick == UINT64_C(2020));
    CHECK(server.deadline_tick == UINT64_C(2120));
    CHECK(Worr_NativeReadinessStateValidateV1(&server));
    /* RX must open before the legacy parser observes CLIENT_ACTIVE_CONFIRM:
     * the client application hook may append DATA to that same packet. */
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(
        &server, UINT64_C(2020)));
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(
        &server, UINT64_C(2020)));

    /* The legacy three-record API cannot silently activate an event binding. */
    before = client;
    CHECK(Worr_NativeReadinessClientObserveServerActiveV1(
              &client, &server_active, UINT64_C(2030)) ==
          WORR_NATIVE_READINESS_INVALID_STATE);
    CHECK(!memcmp(&client, &before, sizeof(before)));

    CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &client, &server_active, UINT64_C(2030),
              &client_active_confirm) == WORR_NATIVE_READINESS_OK);
    CHECK(client.phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    CHECK(client_active_confirm.record_kind ==
          WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM);
    CHECK(client_active_confirm.transport_epoch == 17);
    CHECK(client_active_confirm.negotiated_capabilities ==
          EVENT_NATIVE_CAPABILITIES);
    CHECK(client_active_confirm.readiness_nonce == UINT64_C(900));
    CHECK(Worr_NativeReadinessRecordValidateV1(&client_active_confirm));
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(
        &client, UINT64_C(2030)));
    CHECK(Worr_NativeReadinessCanTransmitNativeV1(
        &client, UINT64_C(2030)));

    /* Neither client activation nor a repeated CLIENT_READY opens server
     * native gates; a lost SERVER_ACTIVE can be regenerated exactly. */
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(
        &server, UINT64_C(2030)));
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(
        &server, UINT64_C(2030)));
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &server, &client_ready, UINT64_C(2031),
              &repeated_active) == WORR_NATIVE_READINESS_EXACT_DUPLICATE);
    CHECK(!memcmp(&server_active, &repeated_active,
                  sizeof(server_active)));
    CHECK(server.phase ==
          WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM);
    CHECK(server.deadline_tick == UINT64_C(2120));

    CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &client, &repeated_active, UINT64_C(2032),
              &repeated_confirm) ==
          WORR_NATIVE_READINESS_EXACT_DUPLICATE);
    CHECK(!memcmp(&client_active_confirm, &repeated_confirm,
                  sizeof(client_active_confirm)));
    CHECK(Worr_NativeReadinessServerObserveClientActiveConfirmV1(
              &server, &client_active_confirm, UINT64_C(2040)) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(server.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    CHECK(server.deadline_tick == 0);
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(
        &server, UINT64_C(2040)));
    CHECK(Worr_NativeReadinessCanTransmitNativeV1(
        &server, UINT64_C(2040)));
    CHECK(Worr_NativeReadinessServerObserveClientActiveConfirmV1(
              &server, &repeated_confirm, UINT64_C(2041)) ==
          WORR_NATIVE_READINESS_EXACT_DUPLICATE);

    /* CLIENT_ACTIVE_CONFIRM is not valid for the command-only 0x53 binding,
     * so merely extending the enum cannot broaden command-only pilots. */
    fill_bytes(&output, sizeof(output), 0x4a);
    output_before = output;
    CHECK(!Worr_NativeReadinessRecordInitV1(
        &output, WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM,
        17, NATIVE_CAPABILITIES, UINT64_C(900)));
    CHECK(!memcmp(&output, &output_before, sizeof(output)));
}

static void test_event_stream_active_confirmation_fail_closed(void)
{
    worr_native_readiness_state_v1 server;
    worr_native_readiness_state_v1 client;
    worr_native_readiness_state_v1 before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_record_v1 client_active_confirm;
    worr_native_readiness_record_v1 bad_confirm;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_record_v1 output_before;

    CHECK(Worr_NativeReadinessServerInitV1(
              &server, 18, EVENT_NATIVE_CAPABILITIES, UINT64_C(901),
              UINT64_C(3000), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientInitV1(
              &client, 18, EVENT_NATIVE_CAPABILITIES, UINT64_C(3000),
              UINT64_C(100)) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &challenge, UINT64_C(3010), &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &server, &client_ready, UINT64_C(3020), &server_active) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &client, &server_active, UINT64_C(3030),
              &client_active_confirm) == WORR_NATIVE_READINESS_OK);

    bad_confirm = client_active_confirm;
    CHECK(Worr_NativeReadinessRecordInitV1(
        &bad_confirm,
        WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM, 19,
        EVENT_NATIVE_CAPABILITIES, UINT64_C(901)));
    CHECK(Worr_NativeReadinessServerObserveClientActiveConfirmV1(
              &server, &bad_confirm, UINT64_C(3040)) ==
          WORR_NATIVE_READINESS_BINDING_MISMATCH);
    CHECK(server.telemetry.binding_mismatches == 1);
    check_failed(&server);

    /* A missing fourth record remains subject to the reset phase deadline. */
    CHECK(Worr_NativeReadinessServerInitV1(
              &server, 18, EVENT_NATIVE_CAPABILITIES, UINT64_C(902),
              UINT64_C(4000), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientInitV1(
              &client, 18, EVENT_NATIVE_CAPABILITIES, UINT64_C(4000),
              UINT64_C(100)) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &challenge, UINT64_C(4010), &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &server, &client_ready, UINT64_C(4020), &server_active) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessCheckDeadlineV1(
              &server, UINT64_C(4120)) ==
          WORR_NATIVE_READINESS_DEADLINE_EXPIRED);
    check_failed(&server);

    /* Output aliasing and a legacy binding passed to the confirmation entry
     * point are rejected without mutating state or output. */
    CHECK(Worr_NativeReadinessClientInitV1(
              &client, 20, EVENT_NATIVE_CAPABILITIES, UINT64_C(5000),
              UINT64_C(100)) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessRecordInitV1(
        &challenge, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 20,
        EVENT_NATIVE_CAPABILITIES, UINT64_C(903)));
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &challenge, UINT64_C(5010), &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessRecordInitV1(
        &server_active, WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE, 20,
        EVENT_NATIVE_CAPABILITIES, UINT64_C(903)));
    before = client;
    CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &client, &server_active, UINT64_C(5020),
              &server_active) == WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&client, &before, sizeof(before)));

    CHECK(Worr_NativeReadinessClientInitV1(
              &client, 20, NATIVE_CAPABILITIES, UINT64_C(5000),
              UINT64_C(100)) == WORR_NATIVE_READINESS_OK);
    fill_bytes(&output, sizeof(output), 0x73);
    output_before = output;
    before = client;
    CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &client, &server_active, UINT64_C(5020), &output) ==
          WORR_NATIVE_READINESS_INVALID_STATE);
    CHECK(!memcmp(&client, &before, sizeof(before)));
    CHECK(!memcmp(&output, &output_before, sizeof(output)));
}

static void run_snapshot_confirmation_binding(uint32_t capabilities,
                                              uint32_t snapshot_epoch)
{
    worr_native_readiness_state_v1 server;
    worr_native_readiness_state_v1 client;
    worr_native_readiness_state_v1 before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_record_v1 client_active_confirm;

    CHECK(capabilities == UINT32_C(0x57) ||
          capabilities == UINT32_C(0x77));
    CHECK(Worr_NativeReadinessServerInitBoundV1(
              &server, 27, snapshot_epoch, capabilities, UINT64_C(1900),
              UINT64_C(6000), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientInitBoundV1(
              &client, 27, snapshot_epoch, capabilities, UINT64_C(6000),
              UINT64_C(100)) == WORR_NATIVE_READINESS_OK);
    CHECK(server.snapshot_epoch == snapshot_epoch);
    CHECK(client.snapshot_epoch == snapshot_epoch);
    CHECK(challenge.snapshot_epoch == snapshot_epoch);

    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &challenge, UINT64_C(6010), &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(client_ready.snapshot_epoch == snapshot_epoch);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &server, &client_ready, UINT64_C(6020), &server_active) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(server_active.snapshot_epoch == snapshot_epoch);
    CHECK(server.phase ==
          WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM);
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(
        &server, UINT64_C(6020)));
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(
        &server, UINT64_C(6020)));

    before = client;
    CHECK(Worr_NativeReadinessClientObserveServerActiveV1(
              &client, &server_active, UINT64_C(6030)) ==
          WORR_NATIVE_READINESS_INVALID_STATE);
    CHECK(!memcmp(&client, &before, sizeof(before)));
    CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &client, &server_active, UINT64_C(6030),
              &client_active_confirm) == WORR_NATIVE_READINESS_OK);
    CHECK(client_active_confirm.snapshot_epoch == snapshot_epoch);
    CHECK(client_active_confirm.negotiated_capabilities == capabilities);
    CHECK(Worr_NativeReadinessServerObserveClientActiveConfirmV1(
              &server, &client_active_confirm, UINT64_C(6040)) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(server.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    CHECK(Worr_NativeReadinessCanTransmitNativeV1(
        &server, UINT64_C(6040)));
}

static void test_snapshot_epoch_binding_and_confirmation(void)
{
    worr_native_readiness_state_v1 server;
    worr_native_readiness_state_v1 client;
    worr_native_readiness_state_v1 before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 wrong_challenge;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_record_v1 output_before;

    CHECK(SNAPSHOT_NATIVE_CAPABILITIES == UINT32_C(0x57));
    CHECK(EVENT_SNAPSHOT_NATIVE_CAPABILITIES == UINT32_C(0x77));
    run_snapshot_confirmation_binding(
        SNAPSHOT_NATIVE_CAPABILITIES, UINT32_C(701));
    run_snapshot_confirmation_binding(
        EVENT_SNAPSHOT_NATIVE_CAPABILITIES, UINT32_C(702));

    fill_bytes(&server, sizeof(server), 0x41);
    fill_bytes(&challenge, sizeof(challenge), 0x14);
    before = server;
    output_before = challenge;
    CHECK(Worr_NativeReadinessServerInitV1(
              &server, 28, SNAPSHOT_NATIVE_CAPABILITIES, UINT64_C(2000),
              UINT64_C(7000), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&server, &before, sizeof(server)));
    CHECK(!memcmp(&challenge, &output_before, sizeof(challenge)));
    CHECK(Worr_NativeReadinessClientInitV1(
              &server, 28, SNAPSHOT_NATIVE_CAPABILITIES, UINT64_C(7000),
              UINT64_C(100)) == WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&server, &before, sizeof(server)));

    CHECK(Worr_NativeReadinessServerInitBoundV1(
              &server, 28, 703, SNAPSHOT_NATIVE_CAPABILITIES,
              UINT64_C(2000), UINT64_C(7000), UINT64_C(100),
              &challenge) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientInitBoundV1(
              &client, 28, 703, SNAPSHOT_NATIVE_CAPABILITIES,
              UINT64_C(7000), UINT64_C(100)) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessRecordInitBoundV1(
        &wrong_challenge, WORR_NATIVE_READINESS_RECORD_CHALLENGE,
        28, 704, SNAPSHOT_NATIVE_CAPABILITIES, UINT64_C(2000)));
    fill_bytes(&output, sizeof(output), 0xa6);
    output_before = output;
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &wrong_challenge, UINT64_C(7010), &output) ==
          WORR_NATIVE_READINESS_BINDING_MISMATCH);
    CHECK(!memcmp(&output, &output_before, sizeof(output)));
    check_failed(&client);

    CHECK(Worr_NativeReadinessClientInitBoundV1(
              &client, 28, 703, SNAPSHOT_NATIVE_CAPABILITIES,
              UINT64_C(7000), UINT64_C(100)) ==
          WORR_NATIVE_READINESS_OK);
    before = server;
    output_before = challenge;
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &server, 29, SNAPSHOT_NATIVE_CAPABILITIES,
              UINT64_C(2001), UINT64_C(7010), UINT64_C(100),
              &challenge) == WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&server, &before, sizeof(server)));
    CHECK(!memcmp(&challenge, &output_before, sizeof(challenge)));
    CHECK(Worr_NativeReadinessServerAdvanceEpochBoundV1(
              &server, 29, 705, SNAPSHOT_NATIVE_CAPABILITIES,
              UINT64_C(2001), UINT64_C(7010), UINT64_C(100),
              &challenge) == WORR_NATIVE_READINESS_OK);
    CHECK(server.snapshot_epoch == 705);
    CHECK(challenge.snapshot_epoch == 705);

    before = client;
    CHECK(Worr_NativeReadinessClientAdvanceEpochV1(
              &client, 29, SNAPSHOT_NATIVE_CAPABILITIES,
              UINT64_C(7010), UINT64_C(100)) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&client, &before, sizeof(client)));
    CHECK(Worr_NativeReadinessClientAdvanceEpochBoundV1(
              &client, 29, 705, SNAPSHOT_NATIVE_CAPABILITIES,
              UINT64_C(7010), UINT64_C(100)) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(client.snapshot_epoch == 705);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &challenge, UINT64_C(7020), &output) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(output.snapshot_epoch == 705);
}

static void check_failed(worr_native_readiness_state_v1 *state)
{
    CHECK(state->phase == WORR_NATIVE_READINESS_PHASE_FAILED);
    CHECK(state->deadline_tick == 0);
    CHECK(state->telemetry.failures == 1);
    CHECK(Worr_NativeReadinessStateValidateV1(state));
    CHECK(!Worr_NativeReadinessCanReceiveNativeV1(state, state->last_tick));
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(state, state->last_tick));
}

static void test_invalid_order_and_sticky_failure(void)
{
    readiness_fixture fixture;
    worr_native_readiness_record_v1 bad;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_record_v1 output_before;
    worr_native_readiness_state_v1 failed_before;

    fixture_begin(&fixture);
    bad = fixture.challenge;
    bad.record_checksum ^= UINT32_C(1);
    fill_bytes(&output, sizeof(output), 0x31);
    output_before = output;
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &bad, UINT64_C(1010), &output) ==
          WORR_NATIVE_READINESS_INVALID_RECORD);
    CHECK(!memcmp(&output, &output_before, sizeof(output)));
    CHECK(fixture.client.telemetry.invalid_records == 1);
    check_failed(&fixture.client);
    failed_before = fixture.client;
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &fixture.challenge, UINT64_C(1011), &output) ==
          WORR_NATIVE_READINESS_INVALID_STATE);
    CHECK(!memcmp(&fixture.client, &failed_before, sizeof(failed_before)));
    CHECK(!memcmp(&output, &output_before, sizeof(output)));

    fixture_begin(&fixture);
    CHECK(Worr_NativeReadinessRecordInitV1(
        &bad, WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE, 7,
        NATIVE_CAPABILITIES, UINT64_C(100)));
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &bad, UINT64_C(1010), &output) ==
          WORR_NATIVE_READINESS_WRONG_ORDER);
    CHECK(fixture.client.telemetry.order_failures == 1);
    check_failed(&fixture.client);

    fixture_begin(&fixture);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.server, &fixture.challenge, UINT64_C(1010), &output) ==
          WORR_NATIVE_READINESS_WRONG_ROLE);
    CHECK(fixture.server.telemetry.order_failures == 1);
    check_failed(&fixture.server);
}

static void expect_server_binding_failure(
    const worr_native_readiness_record_v1 *record)
{
    readiness_fixture fixture;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_record_v1 before;

    fixture_begin(&fixture);
    fill_bytes(&output, sizeof(output), 0x6b);
    before = output;
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &fixture.server, record, UINT64_C(1010), &output) ==
          WORR_NATIVE_READINESS_BINDING_MISMATCH);
    CHECK(!memcmp(&output, &before, sizeof(output)));
    CHECK(fixture.server.telemetry.binding_mismatches == 1);
    check_failed(&fixture.server);
}

static void test_binding_failures(void)
{
    readiness_fixture fixture;
    worr_native_readiness_record_v1 record;
    worr_native_readiness_record_v1 output;

    CHECK(Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CLIENT_READY, 8,
        NATIVE_CAPABILITIES, UINT64_C(100)));
    expect_server_binding_failure(&record);
    CHECK(Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CLIENT_READY, 7,
        EVENT_NATIVE_CAPABILITIES, UINT64_C(100)));
    expect_server_binding_failure(&record);
    CHECK(Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CLIENT_READY, 7,
        NATIVE_CAPABILITIES, UINT64_C(101)));
    expect_server_binding_failure(&record);

    fixture_client_ready(&fixture);
    CHECK(Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        NATIVE_CAPABILITIES, UINT64_C(101)));
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &record, UINT64_C(1011), &output) ==
          WORR_NATIVE_READINESS_BINDING_MISMATCH);
    CHECK(fixture.client.telemetry.binding_mismatches == 1);
    check_failed(&fixture.client);

    fixture_client_ready(&fixture);
    CHECK(Worr_NativeReadinessClientAdvanceEpochV1(
              &fixture.client, 8, NATIVE_CAPABILITIES, UINT64_C(1010),
              UINT64_C(100)) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 8,
        NATIVE_CAPABILITIES, UINT64_C(100)));
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &record, UINT64_C(1020), &output) ==
          WORR_NATIVE_READINESS_NONCE_NOT_NEWER);
    CHECK(fixture.client.telemetry.binding_mismatches == 1);
    check_failed(&fixture.client);
}

static void test_fresh_reconnect_epoch_binding(void)
{
    readiness_fixture old_connection;
    worr_native_readiness_state_v1 fresh_client;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_record_v1 output_before;

    fixture_begin(&old_connection);
    CHECK(Worr_NativeReadinessClientInitV1(
              &fresh_client, 8, NATIVE_CAPABILITIES, UINT64_C(1000),
              UINT64_C(100)) == WORR_NATIVE_READINESS_OK);
    fill_bytes(&output, sizeof(output), 0xa7);
    output_before = output;

    /* A globally non-reused fresh epoch rejects a captured old CHALLENGE.
     * Epoch reuse cannot be detected once the caller creates a fresh object. */
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fresh_client, &old_connection.challenge, UINT64_C(1010),
              &output) == WORR_NATIVE_READINESS_BINDING_MISMATCH);
    CHECK(!memcmp(&output, &output_before, sizeof(output)));
    CHECK(fresh_client.telemetry.binding_mismatches == 1);
    check_failed(&fresh_client);
}

static void test_deadlines_and_clock(void)
{
    readiness_fixture fixture;
    worr_native_readiness_state_v1 state;
    worr_native_readiness_state_v1 before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_record_v1 output_before;

    fixture_begin(&fixture);
    CHECK(Worr_NativeReadinessCheckDeadlineV1(
              &fixture.server, UINT64_C(1099)) == WORR_NATIVE_READINESS_OK);
    CHECK(fixture.server.last_tick == UINT64_C(1099));
    CHECK(fixture.server.telemetry.deadline_checks == 1);
    CHECK(Worr_NativeReadinessCheckDeadlineV1(
              &fixture.server, UINT64_C(1100)) ==
          WORR_NATIVE_READINESS_DEADLINE_EXPIRED);
    CHECK(fixture.server.telemetry.deadline_checks == 2);
    CHECK(fixture.server.telemetry.deadline_expirations == 1);
    check_failed(&fixture.server);

    fixture_begin(&fixture);
    CHECK(Worr_NativeReadinessCheckDeadlineV1(
              &fixture.client, UINT64_C(999)) ==
          WORR_NATIVE_READINESS_CLOCK_REGRESSION);
    CHECK(fixture.client.telemetry.clock_regressions == 1);
    check_failed(&fixture.client);

    fixture_client_ready(&fixture);
    CHECK(!Worr_NativeReadinessCanReceiveNativeV1(
        &fixture.client, UINT64_C(1110)));
    CHECK(fixture.client.telemetry.deadline_expirations == 1);
    check_failed(&fixture.client);

    fixture_begin(&fixture);
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(
        &fixture.server, UINT64_C(1100)));
    CHECK(fixture.server.telemetry.deadline_expirations == 1);
    check_failed(&fixture.server);

    fixture_complete(&fixture);
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(
        &fixture.client, UINT64_C(1029)));
    CHECK(fixture.client.telemetry.clock_regressions == 1);
    check_failed(&fixture.client);

    fill_bytes(&state, sizeof(state), 0x47);
    fill_bytes(&challenge, sizeof(challenge), 0x47);
    before = state;
    output_before = challenge;
    CHECK(Worr_NativeReadinessServerInitV1(
              &state, 7, NATIVE_CAPABILITIES, UINT64_C(1),
              UINT64_MAX - UINT64_C(2), UINT64_C(3), &challenge) ==
          WORR_NATIVE_READINESS_DEADLINE_OVERFLOW);
    CHECK(!memcmp(&state, &before, sizeof(state)));
    CHECK(!memcmp(&challenge, &output_before, sizeof(challenge)));

    CHECK(Worr_NativeReadinessClientInitV1(
              &state, 7, NATIVE_CAPABILITIES, UINT64_MAX - UINT64_C(10),
              UINT64_C(9)) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessRecordInitV1(
        &challenge, WORR_NATIVE_READINESS_RECORD_CHALLENGE, 7,
        NATIVE_CAPABILITIES, UINT64_C(1)));
    fill_bytes(&output, sizeof(output), 0x72);
    output_before = output;
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &state, &challenge, UINT64_MAX - UINT64_C(5), &output) ==
          WORR_NATIVE_READINESS_DEADLINE_OVERFLOW);
    CHECK(!memcmp(&output, &output_before, sizeof(output)));
    CHECK(state.telemetry.deadline_expirations == 0);
    check_failed(&state);
}

static void test_epoch_advance_and_exhaustion(void)
{
    readiness_fixture fixture;
    worr_native_readiness_state_v1 before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 challenge_before;
    worr_native_readiness_record_v1 max_challenge;

    fixture_complete(&fixture);
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, 8, NATIVE_CAPABILITIES, UINT64_C(200),
              UINT64_C(1040), UINT64_C(150), &challenge) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(fixture.server.phase ==
          WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY);
    CHECK(fixture.server.transport_epoch == 8);
    CHECK(fixture.server.nonce_floor == UINT64_C(100));
    CHECK(fixture.server.readiness_nonce == UINT64_C(200));
    CHECK(fixture.server.generation == 2);
    CHECK(fixture.server.telemetry.epoch_advances == 1);
    CHECK(fixture.server.telemetry.challenges_emitted == 2);
    CHECK(challenge.transport_epoch == 8);
    CHECK(challenge.readiness_nonce == UINT64_C(200));
    CHECK(Worr_NativeReadinessStateValidateV1(&fixture.server));

    CHECK(Worr_NativeReadinessClientAdvanceEpochV1(
              &fixture.client, 8, NATIVE_CAPABILITIES, UINT64_C(1040),
              UINT64_C(150)) == WORR_NATIVE_READINESS_OK);
    CHECK(fixture.client.phase ==
          WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE);
    CHECK(fixture.client.nonce_floor == UINT64_C(100));
    CHECK(fixture.client.readiness_nonce == 0);
    CHECK(fixture.client.generation == 2);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &challenge, UINT64_C(1050),
              &fixture.client_ready) == WORR_NATIVE_READINESS_OK);

    before = fixture.server;
    fill_bytes(&challenge, sizeof(challenge), 0x2d);
    challenge_before = challenge;
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, 8, NATIVE_CAPABILITIES, UINT64_C(201),
              UINT64_C(1060), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_EPOCH_NOT_NEWER);
    CHECK(!memcmp(&fixture.server, &before, sizeof(before)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, 9, NATIVE_CAPABILITIES, UINT64_C(200),
              UINT64_C(1060), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_NONCE_NOT_NEWER);
    CHECK(!memcmp(&fixture.server, &before, sizeof(before)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, 9, WORR_NET_CAP_LEGACY_STAGE_MASK,
              UINT64_C(201), UINT64_C(1060), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&fixture.server, &before, sizeof(before)));

    CHECK(Worr_NativeReadinessServerInitV1(
              &fixture.server, 1, NATIVE_CAPABILITIES, UINT64_MAX,
              UINT64_C(1), UINT64_C(10), &max_challenge) ==
          WORR_NATIVE_READINESS_OK);
    before = fixture.server;
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, 2, NATIVE_CAPABILITIES, UINT64_MAX,
              UINT64_C(2), UINT64_C(10), &challenge) ==
          WORR_NATIVE_READINESS_NONCE_EXHAUSTED);
    CHECK(!memcmp(&fixture.server, &before, sizeof(before)));

    CHECK(Worr_NativeReadinessClientInitV1(
              &fixture.client, 1, NATIVE_CAPABILITIES, UINT64_C(1),
              UINT64_C(10)) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &max_challenge, UINT64_C(2),
              &fixture.client_ready) == WORR_NATIVE_READINESS_OK);
    before = fixture.client;
    CHECK(Worr_NativeReadinessClientAdvanceEpochV1(
              &fixture.client, 2, NATIVE_CAPABILITIES, UINT64_C(3),
              UINT64_C(10)) == WORR_NATIVE_READINESS_NONCE_EXHAUSTED);
    CHECK(!memcmp(&fixture.client, &before, sizeof(before)));

    fixture_complete(&fixture);
    fixture.server.generation = UINT64_MAX;
    CHECK(Worr_NativeReadinessStateValidateV1(&fixture.server));
    before = fixture.server;
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, 8, NATIVE_CAPABILITIES, UINT64_C(200),
              UINT64_C(1040), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_GENERATION_EXHAUSTED);
    CHECK(!memcmp(&fixture.server, &before, sizeof(before)));
}

static void test_advance_limits_and_transactionality(void)
{
    readiness_fixture fixture;
    worr_native_readiness_state_v1 before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 challenge_before;

    fixture_complete(&fixture);
    fill_bytes(&challenge, sizeof(challenge), 0x3c);
    challenge_before = challenge;
    before = fixture.server;
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, 8, NATIVE_CAPABILITIES, UINT64_C(200),
              UINT64_C(1019), UINT64_C(100), &challenge) ==
          WORR_NATIVE_READINESS_CLOCK_REGRESSION);
    CHECK(!memcmp(&fixture.server, &before, sizeof(before)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, 8, NATIVE_CAPABILITIES, UINT64_C(200),
              UINT64_MAX - UINT64_C(2), UINT64_C(3), &challenge) ==
          WORR_NATIVE_READINESS_DEADLINE_OVERFLOW);
    CHECK(!memcmp(&fixture.server, &before, sizeof(before)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));

    before = fixture.client;
    CHECK(Worr_NativeReadinessClientAdvanceEpochV1(
              &fixture.client, 8, NATIVE_CAPABILITIES, UINT64_C(1029),
              UINT64_C(100)) == WORR_NATIVE_READINESS_CLOCK_REGRESSION);
    CHECK(!memcmp(&fixture.client, &before, sizeof(before)));
    CHECK(Worr_NativeReadinessClientAdvanceEpochV1(
              &fixture.client, 8, NATIVE_CAPABILITIES,
              UINT64_MAX - UINT64_C(2), UINT64_C(3)) ==
          WORR_NATIVE_READINESS_DEADLINE_OVERFLOW);
    CHECK(!memcmp(&fixture.client, &before, sizeof(before)));

    fixture_complete(&fixture);
    fixture.client.generation = UINT64_MAX;
    CHECK(Worr_NativeReadinessStateValidateV1(&fixture.client));
    before = fixture.client;
    CHECK(Worr_NativeReadinessClientAdvanceEpochV1(
              &fixture.client, 8, NATIVE_CAPABILITIES, UINT64_C(1040),
              UINT64_C(100)) == WORR_NATIVE_READINESS_GENERATION_EXHAUSTED);
    CHECK(!memcmp(&fixture.client, &before, sizeof(before)));

    CHECK(Worr_NativeReadinessServerInitV1(
              &fixture.server, UINT32_MAX, NATIVE_CAPABILITIES,
              UINT64_C(100), UINT64_C(1), UINT64_C(10),
              &fixture.challenge) == WORR_NATIVE_READINESS_OK);
    before = fixture.server;
    fill_bytes(&challenge, sizeof(challenge), 0xc3);
    challenge_before = challenge;
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &fixture.server, UINT32_MAX, NATIVE_CAPABILITIES,
              UINT64_C(101), UINT64_C(2), UINT64_C(10), &challenge) ==
          WORR_NATIVE_READINESS_EPOCH_EXHAUSTED);
    CHECK(!memcmp(&fixture.server, &before, sizeof(before)));
    CHECK(!memcmp(&challenge, &challenge_before, sizeof(challenge)));

    CHECK(Worr_NativeReadinessClientInitV1(
              &fixture.client, UINT32_MAX, NATIVE_CAPABILITIES,
              UINT64_C(1), UINT64_C(10)) == WORR_NATIVE_READINESS_OK);
    before = fixture.client;
    CHECK(Worr_NativeReadinessClientAdvanceEpochV1(
              &fixture.client, UINT32_MAX, NATIVE_CAPABILITIES,
              UINT64_C(2), UINT64_C(10)) ==
          WORR_NATIVE_READINESS_EPOCH_EXHAUSTED);
    CHECK(!memcmp(&fixture.client, &before, sizeof(before)));
}

static void test_aliasing_and_transactionality(void)
{
    readiness_fixture fixture;
    union aligned_storage_u {
        worr_native_readiness_state_v1 state;
        unsigned char bytes[sizeof(worr_native_readiness_state_v1) +
                            sizeof(worr_native_readiness_record_v1)];
    } storage;
    unsigned char before[sizeof(storage)];
    worr_native_readiness_state_v1 state_before;
    worr_native_readiness_record_v1 record_before;
    worr_native_readiness_record_v1 output;
    worr_native_readiness_record_v1 output_before;
    worr_native_readiness_record_v1 *input;
    worr_native_readiness_record_v1 *overlap;

    fill_bytes(&storage, sizeof(storage), 0x19);
    memcpy(before, &storage, sizeof(storage));
    overlap = (worr_native_readiness_record_v1 *)(void *)(storage.bytes + 24);
    CHECK(Worr_NativeReadinessServerInitV1(
              &storage.state, 7, NATIVE_CAPABILITIES, UINT64_C(100),
              UINT64_C(1), UINT64_C(10), overlap) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&storage, before, sizeof(storage)));

    fixture_complete(&fixture);
    storage.state = fixture.server;
    memcpy(before, &storage, sizeof(storage));
    overlap = (worr_native_readiness_record_v1 *)(void *)(storage.bytes + 32);
    CHECK(Worr_NativeReadinessServerAdvanceEpochV1(
              &storage.state, 8, NATIVE_CAPABILITIES, UINT64_C(200),
              UINT64_C(1040), UINT64_C(100), overlap) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&storage, before, sizeof(storage)));

    fixture_begin(&fixture);
    storage.state = fixture.client;
    memcpy(before, &storage, sizeof(storage));
    overlap = (worr_native_readiness_record_v1 *)(void *)(storage.bytes + 32);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &storage.state, &fixture.challenge, UINT64_C(1010), overlap) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&storage, before, sizeof(storage)));

    fixture_client_ready(&fixture);
    storage.state = fixture.server;
    memcpy(before, &storage, sizeof(storage));
    fill_bytes(&output, sizeof(output), 0xd4);
    output_before = output;
    overlap = (worr_native_readiness_record_v1 *)(void *)(storage.bytes + 24);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &storage.state, overlap, UINT64_C(1010), &output) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&storage, before, sizeof(storage)));
    CHECK(!memcmp(&output, &output_before, sizeof(output)));

    storage.state = fixture.server;
    memcpy(before, &storage, sizeof(storage));
    record_before = fixture.client_ready;
    overlap = (worr_native_readiness_record_v1 *)(void *)(storage.bytes + 24);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &storage.state, &fixture.client_ready, UINT64_C(1010),
              overlap) == WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&storage, before, sizeof(storage)));
    CHECK(!memcmp(&fixture.client_ready, &record_before,
                  sizeof(record_before)));

    fixture_client_ready(&fixture);
    fill_bytes(&storage, sizeof(storage), 0xe5);
    memcpy(storage.bytes, &fixture.client_ready, sizeof(fixture.client_ready));
    memcpy(before, &storage, sizeof(storage));
    state_before = fixture.server;
    input = (worr_native_readiness_record_v1 *)(void *)storage.bytes;
    overlap = (worr_native_readiness_record_v1 *)(void *)(storage.bytes + 8);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &fixture.server, input, UINT64_C(1010), overlap) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&fixture.server, &state_before, sizeof(state_before)));
    CHECK(!memcmp(&storage, before, sizeof(storage)));

    fixture_begin(&fixture);
    state_before = fixture.client;
    record_before = fixture.challenge;
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &fixture.client, &fixture.challenge, UINT64_C(1010),
              &fixture.challenge) == WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&fixture.client, &state_before, sizeof(state_before)));
    CHECK(!memcmp(&fixture.challenge, &record_before, sizeof(record_before)));

    storage.state = fixture.client;
    memcpy(before, &storage, sizeof(storage));
    overlap = (worr_native_readiness_record_v1 *)(void *)(storage.bytes + 8);
    CHECK(Worr_NativeReadinessClientObserveServerActiveV1(
              &storage.state, overlap, UINT64_C(1010)) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!memcmp(&storage, before, sizeof(storage)));

    CHECK(Worr_NativeReadinessClientInitV1(NULL, 7, NATIVE_CAPABILITIES,
                                           UINT64_C(1), UINT64_C(10)) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(Worr_NativeReadinessCheckDeadlineV1(NULL, UINT64_C(1)) ==
          WORR_NATIVE_READINESS_INVALID_ARGUMENT);
    CHECK(!Worr_NativeReadinessCanReceiveNativeV1(NULL, UINT64_C(1)));
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(NULL, UINT64_C(1)));
    CHECK(!Worr_NativeReadinessResetV1(NULL));
}

static void test_saturation_validation_and_reset(void)
{
    readiness_fixture fixture;
    worr_native_readiness_state_v1 invalid;
    worr_native_readiness_record_v1 repeated;
    unsigned char zero[sizeof(invalid)] = {0};

    fixture_complete(&fixture);
    fixture.server.telemetry.exact_duplicates = UINT64_MAX;
    fixture.server.telemetry.server_active_emitted = UINT64_MAX;
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &fixture.server, &fixture.client_ready, UINT64_C(1040),
              &repeated) == WORR_NATIVE_READINESS_EXACT_DUPLICATE);
    CHECK(fixture.server.telemetry.exact_duplicates == UINT64_MAX);
    CHECK(fixture.server.telemetry.server_active_emitted == UINT64_MAX);
    CHECK(Worr_NativeReadinessStateValidateV1(&fixture.server));

    invalid = fixture.client;
    invalid.struct_size = 0;
    CHECK(!Worr_NativeReadinessStateValidateV1(&invalid));
    invalid = fixture.client;
    invalid.role = 0;
    CHECK(!Worr_NativeReadinessStateValidateV1(&invalid));
    invalid = fixture.client;
    invalid.state_flags = 0;
    CHECK(!Worr_NativeReadinessStateValidateV1(&invalid));
    CHECK(Worr_NativeReadinessServerInitV1(
              &invalid, 9, NATIVE_CAPABILITIES, UINT64_C(300),
              UINT64_C(2000), UINT64_C(100), &repeated) ==
          WORR_NATIVE_READINESS_OK);
    invalid.deadline_tick++;
    CHECK(!Worr_NativeReadinessStateValidateV1(&invalid));
    invalid = fixture.client;
    invalid.readiness_nonce = invalid.nonce_floor;
    CHECK(!Worr_NativeReadinessStateValidateV1(&invalid));
    CHECK(!Worr_NativeReadinessStateValidateV1(NULL));
    CHECK(!Worr_NativeReadinessCanReceiveNativeV1(&invalid, UINT64_C(0)));
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(&invalid, UINT64_C(0)));

    CHECK(Worr_NativeReadinessResetV1(&fixture.server));
    CHECK(!memcmp(&fixture.server, zero, sizeof(zero)));
    CHECK(!Worr_NativeReadinessStateValidateV1(&fixture.server));
    CHECK(!Worr_NativeReadinessCanReceiveNativeV1(
        &fixture.server, UINT64_C(0)));
    CHECK(!Worr_NativeReadinessCanTransmitNativeV1(
        &fixture.server, UINT64_C(0)));
}

int main(void)
{
    test_record_validation();
    test_capability_gating();
    test_handshake_and_gates();
    test_exact_duplicates();
    test_event_stream_active_confirmation_barrier();
    test_event_stream_active_confirmation_fail_closed();
    test_snapshot_epoch_binding_and_confirmation();
    test_invalid_order_and_sticky_failure();
    test_binding_failures();
    test_fresh_reconnect_epoch_binding();
    test_deadlines_and_clock();
    test_epoch_advance_and_exhaustion();
    test_advance_limits_and_transactionality();
    test_aliasing_and_transactionality();
    test_saturation_validation_and_reset();
    puts("native_readiness_test: ok");
    return EXIT_SUCCESS;
}
