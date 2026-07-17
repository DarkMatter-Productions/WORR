/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_session.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "native session check failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition);                        \
            return false;                                                   \
        }                                                                   \
    } while (0)

enum {
    TEST_TX_CAPACITY = 4,
    TEST_RX_CAPACITY = 2,
    TEST_PAYLOAD_STRIDE = 4096,
};

#define TEST_CONNECTION_OWNER_ID UINT64_C(0x5a17000100000001)
#define TEST_OTHER_CONNECTION_OWNER_ID UINT64_C(0x5a17000100000002)

static uint8_t payload[TEST_PAYLOAD_STRIDE];
static uint8_t arena[TEST_RX_CAPACITY * TEST_PAYLOAD_STRIDE];
static uint8_t datagrams[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS]
                        [WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
static size_t datagram_bytes[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS];
static worr_native_ack_range_v1 repeat_ack;

static bool memory_is_zero(const void *data, size_t bytes)
{
    const uint8_t *value = (const uint8_t *)data;
    size_t index;

    for (index = 0; index < bytes; ++index) {
        if (value[index] != 0)
            return false;
    }
    return true;
}

static worr_native_record_ref_v1 make_record(uint8_t record_class,
                                              uint32_t object_epoch,
                                              uint32_t object_sequence)
{
    worr_native_record_ref_v1 record;

    memset(&record, 0, sizeof(record));
    record.record_class = record_class;
    record.record_schema_version = 1;
    record.object_epoch = object_epoch;
    record.object_sequence = object_sequence;
    return record;
}

static void fill_payload(uint32_t bytes, uint32_t salt)
{
    uint32_t index;

    for (index = 0; index < bytes; ++index)
        payload[index] = (uint8_t)((index * 37u + salt * 19u) & 0xffu);
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
    size_t index;

    for (index = 0; index < count; ++index) {
        unsigned bit;

        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc;
}

static void repair_datagram_crc(uint8_t *datagram, size_t bytes)
{
    static const uint8_t zeros[4] = { 0, 0, 0, 0 };
    uint32_t crc = UINT32_MAX;

    crc = crc32_update(crc, datagram, 52);
    crc = crc32_update(crc, zeros, sizeof(zeros));
    crc = crc32_update(crc, datagram + 56, bytes - 56);
    write_u32_le(datagram + 52, ~crc);
}

static bool make_binding_for_owner(
    uint32_t epoch,
    uint64_t connection_owner_id,
    worr_native_session_binding_v1 *binding)
{
    worr_net_capability_state_v1 capability;
    worr_net_capability_confirm_v1 confirm;
    const uint32_t mask = WORR_NET_CAP_NATIVE_ENVELOPE_V1 |
                          WORR_NET_CAP_CANONICAL_SNAPSHOT_V2;

    CHECK(Worr_NetCapabilityStateInitV1(&capability, epoch, mask, mask));
    CHECK(Worr_NetCapabilitySelectV1(epoch, mask, mask, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(Worr_NetCapabilityConfirmV1(&capability, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(Worr_NativeSessionBindingInitV1(
        binding, &capability, connection_owner_id));
    CHECK(Worr_NativeSessionBindingValidateV1(binding));
    return true;
}

static bool make_binding(uint32_t epoch,
                         worr_native_session_binding_v1 *binding)
{
    return make_binding_for_owner(
        epoch, TEST_CONNECTION_OWNER_ID, binding);
}

static bool make_active_readiness_pair(
    uint32_t transport_epoch,
    uint32_t snapshot_epoch,
    uint32_t negotiated_capabilities,
    worr_native_readiness_state_v1 *server,
    worr_native_readiness_state_v1 *client)
{
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_record_v1 client_confirm;
    const uint64_t nonce = UINT64_C(0x8a11000000000000) |
                           transport_epoch;
    const bool snapshot_capable =
        (negotiated_capabilities &
         WORR_NET_CAP_CANONICAL_SNAPSHOT_V2) != 0;
    const bool server_originated_semantic_data =
        (negotiated_capabilities &
         (WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |
          WORR_NET_CAP_CANONICAL_SNAPSHOT_V2)) != 0;

    CHECK(snapshot_capable == (snapshot_epoch != 0));
    if (snapshot_capable) {
        CHECK(Worr_NativeReadinessServerInitBoundV1(
                  server, transport_epoch, snapshot_epoch,
                  negotiated_capabilities, nonce, 10, 100,
                  &challenge) == WORR_NATIVE_READINESS_OK);
        CHECK(Worr_NativeReadinessClientInitBoundV1(
                  client, transport_epoch, snapshot_epoch,
                  negotiated_capabilities, 10, 100) ==
              WORR_NATIVE_READINESS_OK);
    } else {
        CHECK(Worr_NativeReadinessServerInitV1(
                  server, transport_epoch, negotiated_capabilities,
                  nonce, 10, 100, &challenge) ==
              WORR_NATIVE_READINESS_OK);
        CHECK(Worr_NativeReadinessClientInitV1(
                  client, transport_epoch, negotiated_capabilities,
                  10, 100) == WORR_NATIVE_READINESS_OK);
    }
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              client, &challenge, 11, &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              server, &client_ready, 12, &server_active) ==
          WORR_NATIVE_READINESS_OK);
    if (server_originated_semantic_data) {
        CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
                  client, &server_active, 13, &client_confirm) ==
              WORR_NATIVE_READINESS_OK);
        CHECK(Worr_NativeReadinessServerObserveClientActiveConfirmV1(
                  server, &client_confirm, 14) ==
              WORR_NATIVE_READINESS_OK);
    } else {
        CHECK(Worr_NativeReadinessClientObserveServerActiveV1(
                  client, &server_active, 13) ==
              WORR_NATIVE_READINESS_OK);
    }
    CHECK(Worr_NativeReadinessStateValidateV1(server));
    CHECK(Worr_NativeReadinessStateValidateV1(client));
    CHECK(server->snapshot_epoch == snapshot_epoch &&
          client->snapshot_epoch == snapshot_epoch);
    CHECK(server->role == WORR_NATIVE_READINESS_ROLE_SERVER &&
          server->phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    CHECK(client->role == WORR_NATIVE_READINESS_ROLE_CLIENT &&
          client->phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    return true;
}

static bool readiness_binding_rejected_unchanged(
    const worr_native_readiness_state_v1 *readiness,
    uint64_t connection_owner_id)
{
    worr_native_session_binding_v1 binding;
    worr_native_session_binding_v1 before;

    memset(&binding, 0xa5, sizeof(binding));
    before = binding;
    CHECK(!Worr_NativeSessionBindingInitFromReadinessV1(
        &binding, readiness, connection_owner_id));
    CHECK(memcmp(&binding, &before, sizeof(binding)) == 0);
    return true;
}

static bool encode_message(uint32_t transport_epoch,
                           uint32_t message_sequence,
                           worr_native_record_ref_v1 record,
                           uint32_t payload_bytes,
                           uint16_t mtu,
                           uint8_t priority,
                           uint16_t *fragment_count_out)
{
    worr_native_envelope_fragmenter_v1 fragmenter;
    uint16_t count = 0;

    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, transport_epoch, message_sequence, record, priority,
        payload, payload_bytes, mtu));
    while ((fragmenter.state_flags & WORR_NATIVE_FRAGMENTER_EXHAUSTED) == 0) {
        CHECK(count < WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS);
        CHECK(Worr_NativeEnvelopeFragmentNextV1(
                  &fragmenter, payload, payload_bytes, datagrams[count],
                  sizeof(datagrams[count]), &datagram_bytes[count]) ==
              WORR_NATIVE_ENVELOPE_EMIT_OK);
        ++count;
    }
    *fragment_count_out = count;
    return true;
}

static int find_tx_sequence(const worr_native_tx_slot_v1 *slots,
                            uint16_t capacity,
                            uint32_t sequence)
{
    uint16_t index;

    for (index = 0; index < capacity; ++index) {
        if (slots[index].state_flags != 0 &&
            slots[index].message_sequence == sequence)
            return (int)index;
    }
    return -1;
}

static bool test_binding_and_ack_validation(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_session_binding_v1 before;
    worr_net_capability_state_v1 capability;
    worr_native_ack_range_v1 acknowledgement;
    const uint32_t mask = WORR_NET_CAP_NATIVE_ENVELOPE_V1;

    memset(&binding, 0xa5, sizeof(binding));
    before = binding;
    CHECK(Worr_NetCapabilityStateInitV1(&capability, 4, mask, mask));
    CHECK(!Worr_NativeSessionBindingInitV1(
        &binding, &capability, TEST_CONNECTION_OWNER_ID));
    CHECK(memcmp(&binding, &before, sizeof(binding)) == 0);
    CHECK(!Worr_NativeSessionBindingInitV1(
        (worr_native_session_binding_v1 *)&capability, &capability,
        TEST_CONNECTION_OWNER_ID));
    CHECK(!Worr_NativeSessionBindingInitV1(
        &binding, &capability, 0));
    CHECK(memcmp(&binding, &before, sizeof(binding)) == 0);
    CHECK(make_binding(4, &binding));
    CHECK(binding.transport_epoch == 4 &&
          binding.connection_owner_id == TEST_CONNECTION_OWNER_ID);
    CHECK((binding.negotiated_capabilities &
           WORR_NET_CAP_NATIVE_ENVELOPE_V1) != 0);

    memset(&acknowledgement, 0, sizeof(acknowledgement));
    acknowledgement.struct_size = sizeof(acknowledgement);
    acknowledgement.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    acknowledgement.transport_epoch = 4;
    acknowledgement.first_message_sequence = 5;
    acknowledgement.last_message_sequence = 9;
    acknowledgement.connection_owner_id = TEST_CONNECTION_OWNER_ID;
    CHECK(Worr_NativeAckRangeValidateV1(&acknowledgement));
    acknowledgement.first_message_sequence = 0;
    CHECK(!Worr_NativeAckRangeValidateV1(&acknowledgement));
    acknowledgement.first_message_sequence = 10;
    CHECK(!Worr_NativeAckRangeValidateV1(&acknowledgement));
    acknowledgement.first_message_sequence = 5;
    acknowledgement.connection_owner_id = 0;
    CHECK(!Worr_NativeAckRangeValidateV1(&acknowledgement));
    binding.connection_owner_id = 0;
    CHECK(!Worr_NativeSessionBindingValidateV1(&binding));
    return true;
}

static bool test_binding_from_active_readiness(void)
{
    const uint32_t production_private_mask =
        WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK;
    const uint32_t future_known_mask =
        WORR_NET_CAP_NATIVE_ENVELOPE_V1 |
        WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1 |
        WORR_NET_CAP_CANONICAL_SNAPSHOT_V2 |
        WORR_NET_CAP_TYPED_EVENT_RANGE_V2;
    worr_native_readiness_state_v1 server;
    worr_native_readiness_state_v1 client;
    worr_native_readiness_state_v1 server_before;
    worr_native_readiness_state_v1 invalid;
    worr_native_readiness_state_v1 waiting_server;
    worr_native_readiness_state_v1 waiting_client;
    worr_native_readiness_state_v1 alias_state;
    worr_native_readiness_state_v1 alias_before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_session_binding_v1 binding;
    worr_native_session_binding_v1 binding_before;
    worr_net_capability_state_v1 public_capability;
    worr_net_capability_confirm_v1 public_confirm;

    CHECK(production_private_mask == UINT32_C(0x53));
    CHECK(make_active_readiness_pair(
        70, 0, production_private_mask, &server, &client));

    /* The official live tuple may remain exactly legacy-only; the old public
     * capability initializer correctly refuses it while readiness can supply
     * the private native binding. */
    CHECK(Worr_NetCapabilityStateInitV1(
        &public_capability, 70, WORR_NET_CAP_LEGACY_STAGE_MASK,
        WORR_NET_CAP_LEGACY_STAGE_MASK));
    CHECK(Worr_NetCapabilitySelectV1(
              70, WORR_NET_CAP_LEGACY_STAGE_MASK,
              WORR_NET_CAP_LEGACY_STAGE_MASK, &public_confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(Worr_NetCapabilityConfirmV1(
              &public_capability, &public_confirm) ==
          WORR_NET_CAPABILITY_OK);
    memset(&binding, 0xa5, sizeof(binding));
    binding_before = binding;
    CHECK(!Worr_NativeSessionBindingInitV1(
        &binding, &public_capability, TEST_CONNECTION_OWNER_ID));
    CHECK(memcmp(&binding, &binding_before, sizeof(binding)) == 0);

    server_before = server;
    CHECK(Worr_NativeSessionBindingInitFromReadinessV1(
        &binding, &server, TEST_CONNECTION_OWNER_ID));
    CHECK(memcmp(&server, &server_before, sizeof(server)) == 0);
    CHECK(Worr_NativeSessionBindingValidateV1(&binding));
    CHECK(binding.struct_size == sizeof(binding) &&
          binding.schema_version == WORR_NATIVE_SESSION_ABI_VERSION &&
          binding.reserved0 == 0 && binding.transport_epoch == 70 &&
          binding.negotiated_capabilities == production_private_mask &&
          binding.connection_owner_id == TEST_CONNECTION_OWNER_ID);

    CHECK(Worr_NativeSessionBindingInitFromReadinessV1(
        &binding, &client, TEST_OTHER_CONNECTION_OWNER_ID));
    CHECK(binding.transport_epoch == 70 &&
          binding.negotiated_capabilities == production_private_mask &&
          binding.connection_owner_id ==
              TEST_OTHER_CONNECTION_OWNER_ID);

    CHECK(make_active_readiness_pair(
        71, 701, future_known_mask, &server, &client));
    CHECK(Worr_NativeSessionBindingInitFromReadinessV1(
        &binding, &client, UINT64_MAX));
    CHECK(binding.transport_epoch == 71 &&
          binding.negotiated_capabilities == future_known_mask &&
          binding.connection_owner_id == UINT64_MAX);

    CHECK(readiness_binding_rejected_unchanged(NULL,
                                                TEST_CONNECTION_OWNER_ID));
    server_before = server;
    CHECK(!Worr_NativeSessionBindingInitFromReadinessV1(
        NULL, &server, TEST_CONNECTION_OWNER_ID));
    CHECK(memcmp(&server, &server_before, sizeof(server)) == 0);
    CHECK(readiness_binding_rejected_unchanged(&server, 0));

    /* Structurally valid waiting states are not sufficient, even though a
     * client waiting for SERVER_ACTIVE may already admit receive setup. */
    CHECK(Worr_NativeReadinessServerInitV1(
              &waiting_server, 72, production_private_mask,
              UINT64_C(0x7200), 20, 100, &challenge) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientInitV1(
              &waiting_client, 72, production_private_mask,
              20, 100) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &waiting_client, &challenge, 21, &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessStateValidateV1(&waiting_server));
    CHECK(Worr_NativeReadinessStateValidateV1(&waiting_client));
    CHECK(waiting_server.phase ==
              WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY &&
          waiting_client.phase ==
              WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE);
    CHECK(readiness_binding_rejected_unchanged(
        &waiting_server, TEST_CONNECTION_OWNER_ID));
    CHECK(readiness_binding_rejected_unchanged(
        &waiting_client, TEST_CONNECTION_OWNER_ID));

    invalid = server;
    invalid.phase = WORR_NATIVE_READINESS_PHASE_FAILED;
    CHECK(Worr_NativeReadinessStateValidateV1(&invalid));
    CHECK(readiness_binding_rejected_unchanged(
        &invalid, TEST_CONNECTION_OWNER_ID));

    invalid = server;
    invalid.role = WORR_NATIVE_READINESS_ROLE_CLIENT;
    CHECK(!Worr_NativeReadinessStateValidateV1(&invalid));
    CHECK(readiness_binding_rejected_unchanged(
        &invalid, TEST_CONNECTION_OWNER_ID));
    invalid = client;
    invalid.phase = WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE;
    CHECK(!Worr_NativeReadinessStateValidateV1(&invalid));
    CHECK(readiness_binding_rejected_unchanged(
        &invalid, TEST_CONNECTION_OWNER_ID));

    invalid = server;
    invalid.negotiated_capabilities &=
        ~WORR_NET_CAP_NATIVE_ENVELOPE_V1;
    CHECK(readiness_binding_rejected_unchanged(
        &invalid, TEST_CONNECTION_OWNER_ID));
    invalid = server;
    invalid.negotiated_capabilities |= UINT32_C(1) << 31;
    CHECK(readiness_binding_rejected_unchanged(
        &invalid, TEST_CONNECTION_OWNER_ID));
    invalid = server;
    invalid.transport_epoch = 0;
    CHECK(readiness_binding_rejected_unchanged(
        &invalid, TEST_CONNECTION_OWNER_ID));

    alias_state = server;
    alias_before = alias_state;
    CHECK(!Worr_NativeSessionBindingInitFromReadinessV1(
        (worr_native_session_binding_v1 *)&alias_state,
        &alias_state, TEST_CONNECTION_OWNER_ID));
    CHECK(memcmp(&alias_state, &alias_before, sizeof(alias_state)) == 0);

    alias_state = server;
    alias_before = alias_state;
    CHECK(!Worr_NativeSessionBindingInitFromReadinessV1(
        (worr_native_session_binding_v1 *)
            ((uint8_t *)&alias_state + 8),
        &alias_state, TEST_CONNECTION_OWNER_ID));
    CHECK(memcmp(&alias_state, &alias_before, sizeof(alias_state)) == 0);
    return true;
}

static bool test_receive_binding_during_event_confirmation(void)
{
    const uint32_t event_private_mask =
        WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
    worr_native_readiness_state_v1 server;
    worr_native_readiness_state_v1 client;
    worr_native_readiness_state_v1 server_before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_record_v1 client_confirm;
    worr_native_session_binding_v1 binding;
    worr_native_session_binding_v1 before;

    CHECK(event_private_mask == UINT32_C(0x73));
    CHECK(Worr_NativeReadinessServerInitV1(
              &server, 73, event_private_mask, UINT64_C(0x7300),
              10, 100, &challenge) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientInitV1(
              &client, 73, event_private_mask, 10, 100) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &challenge, 11, &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &server, &client_ready, 12, &server_active) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(server.phase ==
          WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM);

    /* The old API stays final-phase-only.  The receive-specific API admits
     * exactly this event-confirmation barrier without mutating its deadline. */
    memset(&binding, 0xa5, sizeof(binding));
    before = binding;
    CHECK(!Worr_NativeSessionBindingInitFromReadinessV1(
        &binding, &server, TEST_CONNECTION_OWNER_ID));
    CHECK(memcmp(&binding, &before, sizeof(binding)) == 0);
    server_before = server;
    CHECK(Worr_NativeSessionBindingInitReceiveFromReadinessV1(
        &binding, &server, TEST_CONNECTION_OWNER_ID, 12));
    CHECK(memcmp(&server, &server_before, sizeof(server)) == 0);
    CHECK(binding.transport_epoch == 73 &&
          binding.negotiated_capabilities == event_private_mask &&
          binding.connection_owner_id == TEST_CONNECTION_OWNER_ID);

    memset(&binding, 0x5a, sizeof(binding));
    before = binding;
    CHECK(!Worr_NativeSessionBindingInitReceiveFromReadinessV1(
        &binding, &server, TEST_CONNECTION_OWNER_ID, 112));
    CHECK(memcmp(&binding, &before, sizeof(binding)) == 0);
    CHECK(!Worr_NativeSessionBindingInitReceiveFromReadinessV1(
        &binding, &client, TEST_CONNECTION_OWNER_ID, 12));
    CHECK(memcmp(&binding, &before, sizeof(binding)) == 0);

    CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &client, &server_active, 13, &client_confirm) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessServerObserveClientActiveConfirmV1(
              &server, &client_confirm, 14) == WORR_NATIVE_READINESS_OK);
    CHECK(server.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE &&
          client.phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    CHECK(Worr_NativeSessionBindingInitReceiveFromReadinessV1(
        &binding, &server, TEST_CONNECTION_OWNER_ID, 14));
    CHECK(Worr_NativeSessionBindingInitFromReadinessV1(
        &binding, &client, TEST_OTHER_CONNECTION_OWNER_ID));
    return true;
}

static bool test_tx_retention_supersession_and_ack(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_tx_session_v1 session;
    worr_native_tx_slot_v1 slots[TEST_TX_CAPACITY];
    worr_native_tx_slot_v1 selected;
    worr_native_ack_range_v1 acknowledgement;
    worr_native_tx_slot_v1 slots_before[TEST_TX_CAPACITY];
    uint32_t sequence = 0;
    uint32_t command_sequence;
    uint32_t event_sequence;
    uint32_t snapshot_sequence;
    uint32_t acknowledged = 99;

    CHECK(make_binding(11, &binding));
    CHECK(Worr_NativeTxSessionInitV1(&session, slots,
                                     TEST_TX_CAPACITY, &binding));
    CHECK(Worr_NativeTxSessionValidateV1(&session, slots,
                                         TEST_TX_CAPACITY));

    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_COMMAND_V1, 8, 1),
              4, 101, 80, 700, 10, &sequence) == WORR_NATIVE_TX_RETAINED);
    command_sequence = sequence;
    CHECK(command_sequence == 1);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 8, 1),
              1, 102, 96, 700, 10, &sequence) == WORR_NATIVE_TX_RETAINED);
    event_sequence = sequence;
    CHECK(event_sequence == 2);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 8, 20),
              3, 103, 512, 700, 10, &sequence) == WORR_NATIVE_TX_RETAINED);
    snapshot_sequence = sequence;
    CHECK(snapshot_sequence == 3 && session.retained_count == 3);

    sequence = UINT32_MAX;
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 8, 1),
              1, 102, 96, 700, 11, &sequence) == WORR_NATIVE_TX_DUPLICATE);
    CHECK(sequence == event_sequence && session.retained_count == 3);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 8, 1),
              1, 102, 96, 800, 11, &sequence) == WORR_NATIVE_TX_CONFLICT);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 8, 1),
              1, 222, 96, 700, 11, &sequence) == WORR_NATIVE_TX_CONFLICT);

    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 8, 21),
              3, 104, 640, 700, 12, &sequence) == WORR_NATIVE_TX_SUPERSEDED);
    CHECK(sequence == 4 && session.retained_count == 3);
    CHECK(find_tx_sequence(slots, TEST_TX_CAPACITY, snapshot_sequence) < 0);
    snapshot_sequence = sequence;
    sequence = UINT32_MAX;
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 8, 19),
              3, 105, 400, 700, 13, &sequence) ==
          WORR_NATIVE_TX_STALE_SNAPSHOT);
    CHECK(sequence == UINT32_MAX && session.next_message_sequence == 5);

    memset(&selected, 0xa5, sizeof(selected));
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &session, slots, TEST_TX_CAPACITY, 14, 10, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(selected.message_sequence == event_sequence &&
          selected.send_attempts == 1);
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &session, slots, TEST_TX_CAPACITY, 14, 10, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(selected.message_sequence == snapshot_sequence);
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &session, slots, TEST_TX_CAPACITY, 14, 10, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(selected.message_sequence == command_sequence);
    memset(&selected, 0xa5, sizeof(selected));
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &session, slots, TEST_TX_CAPACITY, 15, 10, &selected) ==
          WORR_NATIVE_TX_NOT_DUE);
    CHECK(selected.message_sequence == UINT32_C(0xa5a5a5a5));
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &session, slots, TEST_TX_CAPACITY, 24, 10, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(selected.message_sequence == event_sequence &&
          selected.send_attempts == 2);

    memset(&acknowledgement, 0, sizeof(acknowledgement));
    acknowledgement.struct_size = sizeof(acknowledgement);
    acknowledgement.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    acknowledgement.transport_epoch = 99;
    acknowledgement.first_message_sequence = event_sequence;
    acknowledgement.last_message_sequence = snapshot_sequence;
    acknowledgement.connection_owner_id = binding.connection_owner_id;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_NativeTxSessionApplyAckV1(
              &session, slots, TEST_TX_CAPACITY, &acknowledgement,
              &acknowledged) == WORR_NATIVE_TX_WRONG_EPOCH);
    CHECK(memcmp(slots_before, slots, sizeof(slots)) == 0);
    CHECK(acknowledged == 99);

    acknowledgement.transport_epoch = binding.transport_epoch;
    CHECK(Worr_NativeTxSessionApplyAckV1(
              &session, slots, TEST_TX_CAPACITY, &acknowledgement,
              &acknowledged) == WORR_NATIVE_TX_ACKNOWLEDGED);
    CHECK(acknowledged == 2 && session.retained_count == 1);
    CHECK(find_tx_sequence(slots, TEST_TX_CAPACITY, command_sequence) >= 0);
    CHECK(find_tx_sequence(slots, TEST_TX_CAPACITY, event_sequence) < 0);
    CHECK(find_tx_sequence(slots, TEST_TX_CAPACITY, snapshot_sequence) < 0);
    CHECK(Worr_NativeTxSessionApplyAckV1(
              &session, slots, TEST_TX_CAPACITY, &acknowledgement,
              &acknowledged) == WORR_NATIVE_TX_ACKNOWLEDGEMENT_EMPTY);
    CHECK(acknowledged == 0);
    CHECK(session.telemetry.acknowledged_reliable == 1);
    CHECK(session.telemetry.acknowledged_snapshots == 1);
    CHECK(session.telemetry.superseded_snapshots == 1);
    CHECK(session.telemetry.selected_retries == 1);
    session.telemetry.enqueue_attempts = UINT64_MAX;
    session.telemetry.duplicates = UINT64_MAX;
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_COMMAND_V1, 8, 1),
              4, 101, 80, 700, 24, &sequence) == WORR_NATIVE_TX_DUPLICATE);
    CHECK(session.telemetry.enqueue_attempts == UINT64_MAX &&
          session.telemetry.duplicates == UINT64_MAX);
    CHECK(Worr_NativeTxSessionValidateV1(&session, slots,
                                         TEST_TX_CAPACITY));
    return true;
}

static bool test_tx_capacity_exhaustion_and_epoch_reset(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_session_binding_v1 next_binding;
    worr_native_session_binding_v1 wrong_owner_binding;
    worr_native_tx_session_v1 session;
    worr_native_tx_slot_v1 slots[2];
    worr_native_tx_slot_v1 slots_before[2];
    uint32_t sequence;

    CHECK(make_binding(20, &binding));
    CHECK(Worr_NativeTxSessionInitV1(&session, slots, 2, &binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_COMMAND_V1, 2, 1),
              2, 1, 20, 700, 0, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 2, 1),
              2, 2, 20, 700, 0, &sequence) == WORR_NATIVE_TX_RETAINED);
    sequence = 77;
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 2, 1),
              2, 3, 20, 700, 0, &sequence) == WORR_NATIVE_TX_CAPACITY);
    CHECK(sequence == 77 && session.retained_count == 2);

    /* A resident snapshot is replaceable only by a newer snapshot. A new
     * reliable record must not treat its occupied slot as free capacity. */
    CHECK(Worr_NativeTxSessionInitV1(&session, slots, 2, &binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 3, 1),
              2, 4, 20, 700, 0, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_COMMAND_V1, 3, 1),
              2, 5, 20, 700, 0, &sequence) == WORR_NATIVE_TX_RETAINED);
    memcpy(slots_before, slots, sizeof(slots));
    sequence = 77;
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 3, 1),
              2, 6, 20, 700, 0, &sequence) == WORR_NATIVE_TX_CAPACITY);
    CHECK(sequence == 77 && session.retained_count == 2 &&
          session.next_message_sequence == 3 &&
          session.telemetry.capacity_stalls == 1 &&
          memcmp(slots, slots_before, sizeof(slots)) == 0);

    memset(slots, 0, sizeof(slots));
    session.retained_count = 0;
    session.next_message_sequence = UINT32_MAX;
    CHECK(Worr_NativeTxSessionValidateV1(&session, slots, 2));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 2, 9),
              0, 9, 20, 700, 1, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(sequence == UINT32_MAX);
    CHECK((session.state_flags & WORR_NATIVE_TX_SEQUENCE_EXHAUSTED) != 0);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 2, 10),
              0, 10, 20, 700, 1, &sequence) ==
          WORR_NATIVE_TX_SEQUENCE_EXHAUSTED_RESULT);

    CHECK(make_binding(21, &next_binding));
    CHECK(Worr_NativeTxSessionAdvanceEpochV1(
        &session, slots, 2, &next_binding));
    CHECK(session.transport_epoch == 21 &&
          session.connection_owner_id == TEST_CONNECTION_OWNER_ID &&
          session.retained_count == 0 &&
          session.next_message_sequence == 1 &&
          session.telemetry.enqueue_attempts == 0);
    CHECK(memory_is_zero(slots, sizeof(slots)));
    CHECK(!Worr_NativeTxSessionAdvanceEpochV1(
        &session, slots, 2, &binding));
    CHECK(make_binding_for_owner(
        22, TEST_OTHER_CONNECTION_OWNER_ID, &wrong_owner_binding));
    CHECK(!Worr_NativeTxSessionAdvanceEpochV1(
        &session, slots, 2, &wrong_owner_binding));
    CHECK(session.transport_epoch == 21 &&
          session.connection_owner_id == TEST_CONNECTION_OWNER_ID);
    return true;
}

static bool test_tx_receipt_window_and_scheduler_aging(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_tx_session_v1 session;
    worr_native_tx_slot_v1 slots[2];
    worr_native_tx_slot_v1 selected;
    worr_native_ack_range_v1 acknowledgement;
    uint32_t sequence;
    uint32_t acknowledged;
    uint32_t object_sequence;
    uint32_t selection;
    bool aged_record_selected = false;

    CHECK(make_binding(25, &binding));
    CHECK(Worr_NativeTxSessionInitV1(&session, slots, 2, &binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 4, 1),
              0, 100, 32, 700, 0, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(sequence == 1);
    for (object_sequence = 1; object_sequence <= 63; ++object_sequence) {
        const worr_native_tx_result_v1 expected =
            object_sequence == 1 ? WORR_NATIVE_TX_RETAINED
                                 : WORR_NATIVE_TX_SUPERSEDED;
        CHECK(Worr_NativeTxSessionEnqueueV1(
                  &session, slots, 2,
                  make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 4,
                              object_sequence),
                  3, 1000 + object_sequence, 128, 700, object_sequence,
                  &sequence) == expected);
        CHECK(sequence == object_sequence + 1u);
    }
    CHECK(session.next_message_sequence == 65);
    sequence = 99;
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 4, 64),
              3, 1064, 128, 700, 64, &sequence) ==
          WORR_NATIVE_TX_RECEIPT_WINDOW);
    CHECK(sequence == 99 && session.next_message_sequence == 65 &&
          session.telemetry.receipt_window_stalls == 1);

    memset(&acknowledgement, 0, sizeof(acknowledgement));
    acknowledgement.struct_size = sizeof(acknowledgement);
    acknowledgement.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    acknowledgement.transport_epoch = binding.transport_epoch;
    acknowledgement.first_message_sequence = 1;
    acknowledgement.last_message_sequence = 1;
    acknowledgement.connection_owner_id = binding.connection_owner_id;
    CHECK(Worr_NativeTxSessionApplyAckV1(
              &session, slots, 2, &acknowledgement, &acknowledged) ==
          WORR_NATIVE_TX_ACKNOWLEDGED);
    CHECK(acknowledged == 1);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 4, 64),
              3, 1064, 128, 700, 65, &sequence) == WORR_NATIVE_TX_SUPERSEDED);
    CHECK(sequence == 65);

    CHECK(Worr_NativeTxSessionInitV1(&session, slots, 2, &binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_COMMAND_V1, 7, 1),
              0, 1, 20, 700, 0, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 7, 1),
              WORR_NATIVE_ENVELOPE_MAX_PRIORITY, 2, 20, 700, 0,
              &sequence) == WORR_NATIVE_TX_RETAINED);
    for (selection = 0; selection <= 56; ++selection) {
        CHECK(Worr_NativeTxSessionSelectDueV1(
                  &session, slots, 2, selection, 0, &selected) ==
              WORR_NATIVE_TX_SELECTED);
        if (selected.record.record_class == WORR_NATIVE_RECORD_EVENT_V1) {
            CHECK(selection == 56);
            aged_record_selected = true;
            break;
        }
    }
    CHECK(aged_record_selected);
    CHECK(session.dispatch_count == 57);

    session.dispatch_count = UINT64_MAX;
    slots[0].enqueue_dispatch = UINT64_MAX;
    slots[1].enqueue_dispatch = UINT64_MAX;
    CHECK(Worr_NativeTxSessionValidateV1(&session, slots, 2));
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &session, slots, 2, 57, 0, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(session.dispatch_count == 1 &&
          session.telemetry.scheduler_rebases == 1);
    return true;
}

static bool test_tx_prepared_send_confirmation(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_tx_session_v1 session;
    worr_native_tx_session_v1 session_before;
    worr_native_tx_session_v1 direct_session;
    worr_native_tx_slot_v1 slots[2];
    worr_native_tx_slot_v1 slots_before[2];
    worr_native_tx_slot_v1 direct_slots[2];
    worr_native_tx_slot_v1 direct_selected;
    worr_native_tx_send_ticket_v1 ticket;
    worr_native_tx_send_ticket_v1 ticket_before;
    worr_native_tx_send_ticket_v1 malformed;
    worr_native_tx_send_ticket_v1 wrong_epoch;
    uint32_t sequence;
    uint32_t selected_sequence;
    uint16_t selected_index;

    CHECK(make_binding(26, &binding));
    CHECK(Worr_NativeTxSessionInitV1(&session, slots, 2, &binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 8, 1),
              1, 700, 96, 700, 10, &sequence) == WORR_NATIVE_TX_RETAINED);
    selected_sequence = sequence;
    session_before = session;
    memcpy(slots_before, slots, sizeof(slots));
    memset(&ticket, 0xa5, sizeof(ticket));
    CHECK(Worr_NativeTxSessionPrepareDueV1(
              &session, slots, 2, 12, 5, &ticket) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0 &&
          memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(ticket.struct_size == sizeof(ticket) &&
          ticket.schema_version == WORR_NATIVE_SESSION_ABI_VERSION &&
          ticket.state_flags == WORR_NATIVE_TX_SEND_TICKET_INITIALIZED &&
          ticket.transport_epoch == binding.transport_epoch &&
          ticket.connection_owner_id == binding.connection_owner_id &&
          ticket.selection_tick == 12 &&
          ticket.resend_interval_ticks == 5 &&
          ticket.pre_send_slot.message_sequence == selected_sequence &&
          ticket.pre_send_slot.send_attempts == 0);
    CHECK(Worr_NativeTxSessionPreparedValidateV1(
        &session, slots, 2, &ticket));
    CHECK(Worr_NativeTxSessionPreparedValidateV1(
        &session, slots, 2, &ticket));
    selected_index = ticket.slot_index;

    wrong_epoch = ticket;
    ++wrong_epoch.transport_epoch;
    session_before = session;
    CHECK(Worr_NativeTxSessionConfirmPreparedV1(
              &session, slots, 2, &wrong_epoch, 12) ==
          WORR_NATIVE_TX_WRONG_EPOCH);
    ++session_before.telemetry.wrong_epoch;
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0 &&
          memcmp(slots, slots_before, sizeof(slots)) == 0 &&
          Worr_NativeTxSessionPreparedValidateV1(
              &session, slots, 2, &ticket));

    /* A selected slot may be validated after each emitted fragment without
     * consuming the ticket.  Any byte-level baseline change is stale. */
    ++slots[selected_index].payload_handle;
    CHECK(!Worr_NativeTxSessionPreparedValidateV1(
        &session, slots, 2, &ticket));
    slots[selected_index] = ticket.pre_send_slot;
    CHECK(Worr_NativeTxSessionPreparedValidateV1(
        &session, slots, 2, &ticket));

    session_before = session;
    CHECK(Worr_NativeTxSessionConfirmPreparedV1(
              &session, slots, 2, &ticket, 11) ==
          WORR_NATIVE_TX_CLOCK_REGRESSION);
    ++session_before.telemetry.clock_regressions;
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0 &&
          memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(Worr_NativeTxSessionPreparedValidateV1(
        &session, slots, 2, &ticket));

    /* Confirmation installs exactly the ordinary selected-path mutation,
     * using the monotonic completion tick as the attempt timestamp. */
    direct_session = session;
    memcpy(direct_slots, slots, sizeof(slots));
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &direct_session, direct_slots, 2, 13, 5,
              &direct_selected) == WORR_NATIVE_TX_SELECTED);
    CHECK(direct_selected.message_sequence == selected_sequence);
    CHECK(Worr_NativeTxSessionConfirmPreparedV1(
              &session, slots, 2, &ticket, 13) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(memcmp(&session, &direct_session, sizeof(session)) == 0 &&
          memcmp(slots, direct_slots, sizeof(slots)) == 0 &&
          slots[selected_index].send_attempts == 1 &&
          slots[selected_index].last_send_tick == 13);

    /* The changed retained slot consumes every copy of the immutable ticket. */
    session_before = session;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(!Worr_NativeTxSessionPreparedValidateV1(
        &session, slots, 2, &ticket));
    CHECK(Worr_NativeTxSessionConfirmPreparedV1(
              &session, slots, 2, &ticket, 13) ==
          WORR_NATIVE_TX_INVALID_STATE);
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0 &&
          memcmp(slots, slots_before, sizeof(slots)) == 0);

    memset(&ticket, 0xa5, sizeof(ticket));
    ticket_before = ticket;
    CHECK(Worr_NativeTxSessionPrepareDueV1(
              &session, slots, 2, 14, 100, &ticket) ==
          WORR_NATIVE_TX_NOT_DUE);
    CHECK(memcmp(&ticket, &ticket_before, sizeof(ticket)) == 0);
    CHECK(Worr_NativeTxSessionPrepareDueV1(
              &session, slots, 2, 12, 100, &ticket) ==
          WORR_NATIVE_TX_CLOCK_REGRESSION);
    CHECK(memcmp(&ticket, &ticket_before, sizeof(ticket)) == 0);
    CHECK(Worr_NativeTxSessionPrepareDueV1(
              &session, slots, 2, 14, 100,
              (worr_native_tx_send_ticket_v1 *)&session) ==
          WORR_NATIVE_TX_INVALID_ARGUMENT);
    CHECK(!Worr_NativeTxSessionPreparedValidateV1(
        &session, slots, 2,
        (const worr_native_tx_send_ticket_v1 *)&slots[0]));
    CHECK(Worr_NativeTxSessionConfirmPreparedV1(
              &session, slots, 2,
              (const worr_native_tx_send_ticket_v1 *)&session, 14) ==
          WORR_NATIVE_TX_INVALID_ARGUMENT);

    malformed = ticket_before;
    malformed.struct_size = sizeof(malformed);
    malformed.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    malformed.state_flags = WORR_NATIVE_TX_SEND_TICKET_INITIALIZED;
    malformed.transport_epoch = binding.transport_epoch;
    malformed.connection_owner_id = binding.connection_owner_id;
    malformed.reserved1 = 1;
    CHECK(!Worr_NativeTxSessionPreparedValidateV1(
        &session, slots, 2, &malformed));
    CHECK(Worr_NativeTxSessionConfirmPreparedV1(
              &session, slots, 2, &malformed, 14) ==
          WORR_NATIVE_TX_INVALID_ARGUMENT);

    /* Unrelated retained work may arrive while a burst is emitted; only the
     * selected slot is the ticket's baseline authority. */
    CHECK(Worr_NativeTxSessionInitV1(&session, slots, 2, &binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_COMMAND_V1, 9, 1),
              0, 800, 64, 700, 20, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(Worr_NativeTxSessionPrepareDueV1(
              &session, slots, 2, 20, 5, &ticket) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 9, 1),
              7, 801, 64, 700, 21, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(Worr_NativeTxSessionPreparedValidateV1(
        &session, slots, 2, &ticket));
    CHECK(Worr_NativeTxSessionConfirmPreparedV1(
              &session, slots, 2, &ticket, 21) ==
          WORR_NATIVE_TX_SELECTED);

    /* UINT64_MAX is a real occupied dispatch value, not the empty sentinel.
     * Confirmation rebases every resident age before recording the send. */
    CHECK(Worr_NativeTxSessionInitV1(&session, slots, 2, &binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_COMMAND_V1, 10, 1),
              0, 900, 32, 700, 30, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 10, 1),
              7, 901, 32, 700, 30, &sequence) == WORR_NATIVE_TX_RETAINED);
    session.dispatch_count = UINT64_MAX;
    slots[0].enqueue_dispatch = UINT64_MAX;
    slots[1].enqueue_dispatch = UINT64_MAX;
    CHECK(Worr_NativeTxSessionValidateV1(&session, slots, 2));
    session_before = session;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_NativeTxSessionPrepareDueV1(
              &session, slots, 2, 31, 0, &ticket) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0 &&
          memcmp(slots, slots_before, sizeof(slots)) == 0);
    selected_index = ticket.slot_index;
    direct_session = session;
    memcpy(direct_slots, slots, sizeof(slots));
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &direct_session, direct_slots, 2, 31, 0,
              &direct_selected) == WORR_NATIVE_TX_SELECTED);
    CHECK(direct_selected.message_sequence ==
          ticket.pre_send_slot.message_sequence);
    CHECK(Worr_NativeTxSessionConfirmPreparedV1(
              &session, slots, 2, &ticket, 31) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(session.dispatch_count == 1 &&
          session.telemetry.scheduler_rebases == 1 &&
          slots[selected_index].enqueue_dispatch == 1 &&
          slots[1u - selected_index].enqueue_dispatch == 0 &&
          memcmp(&session, &direct_session, sizeof(session)) == 0 &&
          memcmp(slots, direct_slots, sizeof(slots)) == 0 &&
          Worr_NativeTxSessionValidateV1(&session, slots, 2));

    /* A saturated attempt counter needs a strictly newer send timestamp so
     * the retained baseline can never cycle back to the prepared ticket. */
    CHECK(Worr_NativeTxSessionInitV1(&session, slots, 2, &binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 11, 1),
              0, 1000, 32, 700, 40, &sequence) == WORR_NATIVE_TX_RETAINED);
    slots[0].send_attempts = UINT32_MAX;
    slots[0].last_send_tick = 40;
    CHECK(Worr_NativeTxSessionValidateV1(&session, slots, 2));
    memset(&ticket, 0xa5, sizeof(ticket));
    ticket_before = ticket;
    CHECK(Worr_NativeTxSessionPrepareDueV1(
              &session, slots, 2, 40, 0, &ticket) ==
          WORR_NATIVE_TX_INVALID_STATE);
    CHECK(memcmp(&ticket, &ticket_before, sizeof(ticket)) == 0);
    CHECK(Worr_NativeTxSessionPrepareDueV1(
              &session, slots, 2, 41, 0, &ticket) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(Worr_NativeTxSessionConfirmPreparedV1(
              &session, slots, 2, &ticket, 41) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(slots[0].send_attempts == UINT32_MAX &&
          slots[0].last_send_tick == 41 &&
          Worr_NativeTxSessionConfirmPreparedV1(
              &session, slots, 2, &ticket, 41) ==
              WORR_NATIVE_TX_INVALID_STATE);
    return true;
}

static bool test_same_epoch_cross_owner_rejection(void)
{
    worr_native_session_binding_v1 first_binding;
    worr_native_session_binding_v1 second_binding;
    worr_native_tx_session_v1 first_session;
    worr_native_tx_session_v1 second_session;
    worr_native_tx_session_v1 second_session_before;
    worr_native_tx_slot_v1 first_slots[1];
    worr_native_tx_slot_v1 second_slots[1];
    worr_native_tx_slot_v1 second_slots_before[1];
    worr_native_tx_send_ticket_v1 ticket;
    worr_native_ack_range_v1 acknowledgement;
    uint32_t first_sequence;
    uint32_t second_sequence;
    uint32_t acknowledged = 77;

    CHECK(make_binding_for_owner(
        27, TEST_CONNECTION_OWNER_ID, &first_binding));
    CHECK(make_binding_for_owner(
        27, TEST_OTHER_CONNECTION_OWNER_ID, &second_binding));
    CHECK(Worr_NativeTxSessionInitV1(
        &first_session, first_slots, 1, &first_binding));
    CHECK(Worr_NativeTxSessionInitV1(
        &second_session, second_slots, 1, &second_binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &first_session, first_slots, 1,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 27, 1),
              2, 2701, 64, 700, 10, &first_sequence) ==
          WORR_NATIVE_TX_RETAINED);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &second_session, second_slots, 1,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 27, 1),
              2, 2701, 64, 700, 10, &second_sequence) ==
          WORR_NATIVE_TX_RETAINED);
    CHECK(first_sequence == second_sequence);
    CHECK(Worr_NativeTxSessionPrepareDueV1(
              &first_session, first_slots, 1, 10, 5, &ticket) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(ticket.transport_epoch == second_session.transport_epoch &&
          ticket.connection_owner_id == first_session.connection_owner_id &&
          ticket.connection_owner_id != second_session.connection_owner_id);
    CHECK(!Worr_NativeTxSessionPreparedValidateV1(
        &second_session, second_slots, 1, &ticket));
    second_session_before = second_session;
    memcpy(second_slots_before, second_slots, sizeof(second_slots));
    CHECK(Worr_NativeTxSessionConfirmPreparedV1(
              &second_session, second_slots, 1, &ticket, 10) ==
          WORR_NATIVE_TX_INVALID_ARGUMENT);
    CHECK(memcmp(&second_session, &second_session_before,
                 sizeof(second_session)) == 0 &&
          memcmp(second_slots, second_slots_before,
                 sizeof(second_slots)) == 0);

    memset(&acknowledgement, 0, sizeof(acknowledgement));
    acknowledgement.struct_size = sizeof(acknowledgement);
    acknowledgement.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    acknowledgement.transport_epoch = first_binding.transport_epoch;
    acknowledgement.first_message_sequence = first_sequence;
    acknowledgement.last_message_sequence = first_sequence;
    acknowledgement.connection_owner_id = first_binding.connection_owner_id;
    CHECK(Worr_NativeAckRangeValidateV1(&acknowledgement));
    CHECK(Worr_NativeTxSessionApplyAckV1(
              &second_session, second_slots, 1, &acknowledgement,
              &acknowledged) == WORR_NATIVE_TX_INVALID_ARGUMENT);
    CHECK(acknowledged == 77 &&
          memcmp(&second_session, &second_session_before,
                 sizeof(second_session)) == 0 &&
          memcmp(second_slots, second_slots_before,
                 sizeof(second_slots)) == 0);

    acknowledgement.connection_owner_id =
        second_binding.connection_owner_id;
    CHECK(Worr_NativeTxSessionApplyAckV1(
              &second_session, second_slots, 1, &acknowledgement,
              &acknowledged) == WORR_NATIVE_TX_ACKNOWLEDGED);
    CHECK(acknowledged == 1 && second_session.retained_count == 0);
    return true;
}

static bool rx_commit_single(worr_native_rx_session_v1 *session,
                             worr_native_rx_slot_v1 *slots,
                             uint16_t slot_capacity,
                             uint32_t sequence,
                             uint32_t object_sequence,
                             uint64_t tick)
{
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 acknowledgement;
    uint16_t fragment_count;

    fill_payload(48, sequence);
    CHECK(encode_message(
        session->transport_epoch, sequence,
        make_record(WORR_NATIVE_RECORD_EVENT_V1, 12, object_sequence),
        48, 700, 1, &fragment_count));
    CHECK(fragment_count == 1);
    CHECK(Worr_NativeRxSessionAcceptV1(
              session, slots, slot_capacity, arena,
              (size_t)slot_capacity * session->payload_stride, tick,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(message.connection_owner_id == session->connection_owner_id);
    CHECK(Worr_NativeRxSessionCommitV1(
              session, slots, slot_capacity, message.slot_index,
              sequence, &acknowledgement) == WORR_NATIVE_RX_COMMITTED);
    CHECK(acknowledgement.first_message_sequence == sequence &&
          acknowledgement.last_message_sequence == sequence &&
          acknowledgement.connection_owner_id ==
              session->connection_owner_id);
    return true;
}

static bool test_rx_receipt_window_and_delayed_replay(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[TEST_RX_CAPACITY];
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 acknowledgement;
    uint8_t delayed_sequence_one[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t delayed_sequence_one_bytes;
    uint16_t fragment_count;
    uint32_t sequence;

    CHECK(make_binding(35, &binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &session, slots, TEST_RX_CAPACITY, TEST_PAYLOAD_STRIDE,
        5, 9, &binding));

    fill_payload(48, 1);
    CHECK(encode_message(
        35, 1, make_record(WORR_NATIVE_RECORD_EVENT_V1, 12, 1),
        48, 700, 1, &fragment_count));
    memcpy(delayed_sequence_one, datagrams[0], datagram_bytes[0]);
    delayed_sequence_one_bytes = datagram_bytes[0];

    CHECK(rx_commit_single(&session, slots, TEST_RX_CAPACITY, 2, 2, 1));
    CHECK(session.highest_committed_sequence == 2 &&
          session.receipt_mask == UINT64_C(1));
    CHECK(rx_commit_single(&session, slots, TEST_RX_CAPACITY, 1, 1, 2));
    CHECK(session.highest_committed_sequence == 2 &&
          session.receipt_mask == UINT64_C(3));
    CHECK(rx_commit_single(&session, slots, TEST_RX_CAPACITY, 4, 4, 3));
    CHECK(session.highest_committed_sequence == 4 &&
          session.receipt_mask == UINT64_C(13));
    CHECK(rx_commit_single(&session, slots, TEST_RX_CAPACITY, 3, 3, 4));
    CHECK(session.receipt_mask == UINT64_C(15));

    for (sequence = 5; sequence <= 64; ++sequence)
        CHECK(rx_commit_single(&session, slots, TEST_RX_CAPACITY,
                               sequence, sequence, sequence));
    CHECK(session.highest_committed_sequence == 64 &&
          session.history_count == 64 &&
          session.receipt_mask == UINT64_MAX);
    CHECK(rx_commit_single(&session, slots, TEST_RX_CAPACITY, 65, 65, 65));
    CHECK(session.highest_committed_sequence == 65 &&
          session.history_count == 64 &&
          session.receipt_mask == UINT64_MAX);
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 66,
              delayed_sequence_one, delayed_sequence_one_bytes, &message,
              &repeat_ack) ==
          WORR_NATIVE_RX_STALE_REPLAY);
    CHECK(session.telemetry.stale_replays == 1);
    CHECK(Worr_NativeRxSessionValidateV1(&session, slots,
                                         TEST_RX_CAPACITY));

    /* A newly advanced receipt high-water evicts an already complete slot
     * before that stale slot can reach Commit's bitmap shift. */
    CHECK(Worr_NativeRxSessionInitV1(
        &session, slots, TEST_RX_CAPACITY, TEST_PAYLOAD_STRIDE,
        5, 9, &binding));
    fill_payload(48, 1);
    CHECK(encode_message(
        35, 1, make_record(WORR_NATIVE_RECORD_EVENT_V1, 12, 1),
        48, 700, 1, &fragment_count));
    memcpy(delayed_sequence_one, datagrams[0], datagram_bytes[0]);
    delayed_sequence_one_bytes = datagram_bytes[0];
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 1,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(rx_commit_single(&session, slots, TEST_RX_CAPACITY,
                           100, 100, 2));
    CHECK(session.highest_committed_sequence == 100 &&
          session.history_count == 1 && session.receipt_mask == 1 &&
          session.occupied_count == 0 &&
          session.telemetry.stale_replays == 1);
    memset(&acknowledgement, 0xa5, sizeof(acknowledgement));
    CHECK(Worr_NativeRxSessionCommitV1(
              &session, slots, TEST_RX_CAPACITY, message.slot_index, 1,
              &acknowledgement) == WORR_NATIVE_RX_NOT_FOUND);
    CHECK(acknowledgement.struct_size == UINT32_C(0xa5a5a5a5));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 3,
              delayed_sequence_one, delayed_sequence_one_bytes, &message,
              &repeat_ack) ==
          WORR_NATIVE_RX_STALE_REPLAY);
    CHECK(session.telemetry.stale_replays == 2);

    /* Snapshot commit identity remains ACK-reproducible after the ordinary
     * transport receipt has aged out of the 64-entry window. */
    CHECK(Worr_NativeRxSessionInitV1(
        &session, slots, TEST_RX_CAPACITY, TEST_PAYLOAD_STRIDE,
        5, 9, &binding));
    fill_payload(48, 200);
    CHECK(encode_message(
        35, 1, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 50, 1),
        48, 700, 1, &fragment_count));
    memcpy(delayed_sequence_one, datagrams[0], datagram_bytes[0]);
    delayed_sequence_one_bytes = datagram_bytes[0];
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 1,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(Worr_NativeRxSessionCommitV1(
              &session, slots, TEST_RX_CAPACITY, message.slot_index,
              message.message_sequence, &acknowledgement) ==
          WORR_NATIVE_RX_COMMITTED);
    for (sequence = 2; sequence <= 65; ++sequence)
        CHECK(rx_commit_single(&session, slots, TEST_RX_CAPACITY,
                               sequence, sequence, sequence));
    CHECK(session.highest_committed_sequence == 65 &&
          session.history_count == 64 &&
          session.history[1].message_sequence == 65 &&
          session.committed_snapshot_epoch == 50 &&
          session.committed_snapshot_sequence == 1);
    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 66,
              delayed_sequence_one, delayed_sequence_one_bytes, &message,
              &repeat_ack) == WORR_NATIVE_RX_ALREADY_COMMITTED);
    CHECK(Worr_NativeAckRangeValidateV1(&repeat_ack) &&
          repeat_ack.first_message_sequence == 1 &&
          repeat_ack.last_message_sequence == 1 &&
          session.telemetry.repeat_acknowledgements == 1);
    return true;
}

static bool test_lost_commit_ack_releases_retained_tx(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_tx_session_v1 tx_session;
    worr_native_tx_slot_v1 tx_slots[2];
    worr_native_tx_slot_v1 selected;
    worr_native_rx_session_v1 rx_session;
    worr_native_rx_slot_v1 rx_slots[TEST_RX_CAPACITY];
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 lost_ack;
    uint8_t retained_datagram[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t retained_datagram_bytes;
    uint32_t sequence;
    uint32_t acknowledged;
    uint16_t fragment_count;

    CHECK(make_binding(38, &binding));
    CHECK(Worr_NativeTxSessionInitV1(
        &tx_session, tx_slots, 2, &binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &rx_session, rx_slots, TEST_RX_CAPACITY, TEST_PAYLOAD_STRIDE,
        5, 9, &binding));

    fill_payload(80, 1);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx_session, tx_slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 7, 1),
              2, 101, 80, 700, 1, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(sequence == 1);
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &tx_session, tx_slots, 2, 1, 5, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(selected.fragment_stride ==
              700 - WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES &&
          selected.fragment_count == 1);
    CHECK(encode_message(
        binding.transport_epoch, selected.message_sequence, selected.record,
        selected.payload_bytes,
        (uint16_t)(selected.fragment_stride +
                   WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES),
        selected.priority, &fragment_count));
    CHECK(fragment_count == 1);
    memcpy(retained_datagram, datagrams[0], datagram_bytes[0]);
    retained_datagram_bytes = datagram_bytes[0];
    CHECK(Worr_NativeRxSessionAcceptV1(
              &rx_session, rx_slots, TEST_RX_CAPACITY, arena, sizeof(arena),
              1, datagrams[0], datagram_bytes[0], &message,
              &repeat_ack) == WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(Worr_NativeRxSessionCommitV1(
              &rx_session, rx_slots, TEST_RX_CAPACITY, message.slot_index,
              message.message_sequence, &lost_ack) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(tx_session.retained_count == 1);

    /* A retry must use the fragmentation plan retained with this sequence. */
    CHECK(encode_message(
        binding.transport_epoch, selected.message_sequence, selected.record,
        selected.payload_bytes, 800, selected.priority, &fragment_count));
    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &rx_session, rx_slots, TEST_RX_CAPACITY, arena, sizeof(arena),
              2, datagrams[0], datagram_bytes[0], &message,
              &repeat_ack) == WORR_NATIVE_RX_MESSAGE_CONFLICT);
    CHECK(repeat_ack.struct_size == UINT32_C(0xa5a5a5a5));

    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &rx_session, rx_slots, TEST_RX_CAPACITY, arena, sizeof(arena),
              3, retained_datagram, retained_datagram_bytes, &message,
              &repeat_ack) == WORR_NATIVE_RX_ALREADY_COMMITTED);
    CHECK(Worr_NativeAckRangeValidateV1(&repeat_ack) &&
          repeat_ack.first_message_sequence == 1);
    CHECK(Worr_NativeTxSessionApplyAckV1(
              &tx_session, tx_slots, 2, &repeat_ack, &acknowledged) ==
          WORR_NATIVE_TX_ACKNOWLEDGED);
    CHECK(acknowledged == 1 && tx_session.retained_count == 0);

    fill_payload(96, 2);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx_session, tx_slots, 2,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 7, 10),
              3, 102, 96, 700, 4, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(sequence == 2);
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &tx_session, tx_slots, 2, 4, 5, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(encode_message(
        binding.transport_epoch, selected.message_sequence, selected.record,
        selected.payload_bytes,
        (uint16_t)(selected.fragment_stride +
                   WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES),
        selected.priority, &fragment_count));
    CHECK(fragment_count == 1);
    CHECK(Worr_NativeRxSessionAcceptV1(
              &rx_session, rx_slots, TEST_RX_CAPACITY, arena, sizeof(arena),
              4, datagrams[0], datagram_bytes[0], &message,
              &repeat_ack) == WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(Worr_NativeRxSessionCommitV1(
              &rx_session, rx_slots, TEST_RX_CAPACITY, message.slot_index,
              message.message_sequence, &lost_ack) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(tx_session.retained_count == 1);

    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &rx_session, rx_slots, TEST_RX_CAPACITY, arena, sizeof(arena),
              5, datagrams[0], datagram_bytes[0], &message,
              &repeat_ack) == WORR_NATIVE_RX_ALREADY_COMMITTED);
    CHECK(Worr_NativeAckRangeValidateV1(&repeat_ack) &&
          repeat_ack.first_message_sequence == 2);
    CHECK(Worr_NativeTxSessionApplyAckV1(
              &tx_session, tx_slots, 2, &repeat_ack, &acknowledged) ==
          WORR_NATIVE_TX_ACKNOWLEDGED);
    CHECK(acknowledged == 1 && tx_session.retained_count == 0);

    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx_session, tx_slots, 2,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 7, 2),
              1, 103, 32, 700, 6, &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(sequence == 3 && tx_session.next_message_sequence == 4 &&
          tx_session.telemetry.receipt_window_stalls == 0 &&
          rx_session.telemetry.repeat_acknowledgements == 2 &&
          rx_session.telemetry.message_conflicts == 1);
    return true;
}

static bool test_rx_uint32_max_repeat_ack(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[1];
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 acknowledgement;
    uint16_t fragment_count;

    CHECK(make_binding(39, &binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &session, slots, 1, TEST_PAYLOAD_STRIDE, 5, 9, &binding));
    fill_payload(32, UINT32_MAX);
    CHECK(encode_message(
        39, UINT32_MAX,
        make_record(WORR_NATIVE_RECORD_EVENT_V1, 7, UINT32_MAX),
        32, 700, 1, &fragment_count));
    CHECK(fragment_count == 1);
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 1,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(Worr_NativeRxSessionCommitV1(
              &session, slots, 1, message.slot_index,
              message.message_sequence, &acknowledgement) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(Worr_NativeAckRangeValidateV1(&acknowledgement) &&
          acknowledgement.first_message_sequence == UINT32_MAX &&
          acknowledgement.last_message_sequence == UINT32_MAX);
    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 2,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_ALREADY_COMMITTED);
    CHECK(Worr_NativeAckRangeValidateV1(&repeat_ack) &&
          repeat_ack.first_message_sequence == UINT32_MAX &&
          repeat_ack.last_message_sequence == UINT32_MAX);
    return true;
}

static bool test_rx_snapshot_commit_freshness_and_retry(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_session_binding_v1 next_binding;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[TEST_RX_CAPACITY];
    worr_native_rx_message_v1 old_message;
    worr_native_rx_message_v1 new_message;
    worr_native_ack_range_v1 acknowledgement;
    uint8_t old_snapshot[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint8_t retry_snapshot[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint8_t high_snapshot[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS]
                         [WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t high_snapshot_bytes[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS];
    size_t old_snapshot_bytes;
    size_t retry_snapshot_bytes;
    uint16_t fragment_count;
    uint16_t index;
    uint32_t expired;

    CHECK(make_binding(45, &binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &session, slots, TEST_RX_CAPACITY, TEST_PAYLOAD_STRIDE,
        3, 100, &binding));
    fill_payload(64, 1);
    CHECK(encode_message(
        45, 1, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 20, 10),
        64, 700, 2, &fragment_count));
    memcpy(old_snapshot, datagrams[0], datagram_bytes[0]);
    old_snapshot_bytes = datagram_bytes[0];
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 1,
              datagrams[0], datagram_bytes[0], &old_message,
              &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(session.committed_snapshot_epoch == 0 &&
          session.committed_snapshot_sequence == 0);

    fill_payload(64, 2);
    CHECK(encode_message(
        45, 2, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 20, 11),
        64, 700, 2, &fragment_count));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 2,
              datagrams[0], datagram_bytes[0], &new_message,
              &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(new_message.message_sequence == 2 &&
          session.committed_snapshot_epoch == 0 &&
          session.committed_snapshot_sequence == 0 &&
          session.occupied_count == 2 &&
          session.telemetry.superseded_snapshots == 0);
    CHECK(Worr_NativeRxSessionCommitV1(
              &session, slots, TEST_RX_CAPACITY, old_message.slot_index,
              old_message.message_sequence, &acknowledgement) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(acknowledgement.first_message_sequence == 1 &&
          session.committed_snapshot_epoch == 20 &&
          session.committed_snapshot_sequence == 10 &&
          session.occupied_count == 1);
    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 3,
              old_snapshot, old_snapshot_bytes, &old_message,
              &repeat_ack) ==
          WORR_NATIVE_RX_ALREADY_COMMITTED);
    CHECK(Worr_NativeAckRangeValidateV1(&repeat_ack) &&
          repeat_ack.first_message_sequence == 1 &&
          repeat_ack.last_message_sequence == 1);
    CHECK(Worr_NativeRxSessionCommitV1(
              &session, slots, TEST_RX_CAPACITY, new_message.slot_index,
              new_message.message_sequence, &acknowledgement) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(session.committed_snapshot_epoch == 20 &&
          session.committed_snapshot_sequence == 11 &&
          session.occupied_count == 0);

    fill_payload(64, 3);
    CHECK(encode_message(
        45, 3, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 20, 11),
        64, 700, 2, &fragment_count));
    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 4,
              datagrams[0], datagram_bytes[0], &old_message,
              &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_CONFLICT);
    CHECK(repeat_ack.struct_size == UINT32_C(0xa5a5a5a5));

    fill_payload(64, 4);
    CHECK(encode_message(
        45, 4, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 20, 10),
        64, 700, 2, &fragment_count));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 5,
              datagrams[0], datagram_bytes[0], &old_message,
              &repeat_ack) ==
          WORR_NATIVE_RX_STALE_SNAPSHOT);
    CHECK(session.telemetry.stale_snapshots == 1 &&
          session.telemetry.message_conflicts == 1);

    CHECK(make_binding(46, &next_binding));
    CHECK(Worr_NativeRxSessionAdvanceEpochV1(
        &session, slots, TEST_RX_CAPACITY, &next_binding));
    CHECK(session.committed_snapshot_epoch == 0 &&
          session.committed_snapshot_sequence == 0 &&
          session.snapshot_tombstone_count == 0 &&
          session.highest_committed_sequence == 0 &&
          session.receipt_mask == 0);
    fill_payload(64, 5);
    CHECK(encode_message(
        46, 1, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 1, 1),
        64, 700, 2, &fragment_count));
    memcpy(retry_snapshot, datagrams[0], datagram_bytes[0]);
    retry_snapshot_bytes = datagram_bytes[0];
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 1,
              datagrams[0], datagram_bytes[0], &new_message,
              &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(Worr_NativeRxSessionExpireV1(
              &session, slots, TEST_RX_CAPACITY, 101,
              &expired) == WORR_NATIVE_RX_EXPIRED);
    CHECK(expired == 1 && session.occupied_count == 0 &&
          session.committed_snapshot_epoch == 0 &&
          session.snapshot_tombstone_count == 1 &&
          session.snapshot_tombstones[0].state_flags ==
              WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY);

    /* A different canonical record may not reuse a retained retry sequence. */
    fill_payload(64, 6);
    CHECK(encode_message(
        46, 1, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 1, 2),
        64, 700, 2, &fragment_count));
    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 102,
              datagrams[0], datagram_bytes[0], &new_message,
              &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_CONFLICT);
    CHECK(repeat_ack.struct_size == UINT32_C(0xa5a5a5a5) &&
          session.snapshot_tombstone_count == 1 &&
          Worr_NativeRxSessionValidateV1(
              &session, slots, TEST_RX_CAPACITY));

    /* Timeout and explicit discard both preserve an exact retry path. */
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 103,
              retry_snapshot, retry_snapshot_bytes, &new_message,
              &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(Worr_NativeRxSessionDiscardV1(
              &session, slots, TEST_RX_CAPACITY, new_message.slot_index,
              new_message.message_sequence) == WORR_NATIVE_RX_DISCARDED);
    fill_payload(64, 7);
    CHECK(encode_message(
        46, 1, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 1, 1),
        64, 700, 2, &fragment_count));
    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 104,
              datagrams[0], datagram_bytes[0], &new_message,
              &repeat_ack) == WORR_NATIVE_RX_MESSAGE_CONFLICT);
    CHECK(repeat_ack.struct_size == UINT32_C(0xa5a5a5a5));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 105,
              retry_snapshot, retry_snapshot_bytes, &new_message,
              &repeat_ack) == WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(Worr_NativeRxSessionCommitV1(
              &session, slots, TEST_RX_CAPACITY, new_message.slot_index,
              new_message.message_sequence, &acknowledgement) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(session.committed_snapshot_epoch == 1 &&
          session.committed_snapshot_sequence == 1);

    CHECK(make_binding(47, &next_binding));
    CHECK(Worr_NativeRxSessionAdvanceEpochV1(
        &session, slots, TEST_RX_CAPACITY, &next_binding));
    CHECK(session.fragment_timeout_ticks == 3 &&
          session.complete_timeout_ticks == 100);

    /* A hostile high canonical object cannot evict a prior complete object. */
    fill_payload(64, 7);
    CHECK(encode_message(
        47, 1, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 5, 1),
        64, 700, 2, &fragment_count));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 1,
              datagrams[0], datagram_bytes[0], &old_message,
              &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);

    fill_payload(1800, 8);
    CHECK(encode_message(
        47, 2, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 999, 1),
        1800, 700, 2, &fragment_count));
    CHECK(fragment_count > 1);
    for (index = 0; index < fragment_count; ++index) {
        memcpy(high_snapshot[index], datagrams[index],
               datagram_bytes[index]);
        high_snapshot_bytes[index] = datagram_bytes[index];
    }
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 2,
              high_snapshot[0], high_snapshot_bytes[0], &new_message,
              &repeat_ack) ==
          WORR_NATIVE_RX_FRAGMENT_ACCEPTED);
    CHECK(Worr_NativeRxSessionExpireV1(
              &session, slots, TEST_RX_CAPACITY, 5, &expired) ==
          WORR_NATIVE_RX_EXPIRED);
    CHECK(expired == 1 && session.occupied_count == 1 &&
          slots[old_message.slot_index].state_flags ==
              (WORR_NATIVE_RX_SLOT_OCCUPIED |
               WORR_NATIVE_RX_SLOT_COMPLETE));

    /* Whole-message checksum failure is retryable with corrected fragments
     * carrying the same immutable header identity. */
    memcpy(datagrams[0], high_snapshot[0], high_snapshot_bytes[0]);
    datagrams[0][WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES] ^= UINT8_C(0x80);
    repair_datagram_crc(datagrams[0], high_snapshot_bytes[0]);
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 6,
              datagrams[0], high_snapshot_bytes[0], &new_message,
              &repeat_ack) == WORR_NATIVE_RX_FRAGMENT_ACCEPTED);
    for (index = 1; index < fragment_count; ++index) {
        const worr_native_rx_result_v1 expected =
            index + 1u == fragment_count
                ? WORR_NATIVE_RX_MESSAGE_CHECKSUM
                : WORR_NATIVE_RX_FRAGMENT_ACCEPTED;

        CHECK(Worr_NativeRxSessionAcceptV1(
                  &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena),
                  (uint64_t)(6 + index), high_snapshot[index],
                  high_snapshot_bytes[index], &new_message,
                  &repeat_ack) == expected);
    }
    CHECK(session.occupied_count == 1 &&
          slots[old_message.slot_index].state_flags ==
              (WORR_NATIVE_RX_SLOT_OCCUPIED |
               WORR_NATIVE_RX_SLOT_COMPLETE) &&
          session.committed_snapshot_epoch == 0);

    for (index = 0; index < fragment_count; ++index) {
        const worr_native_rx_result_v1 expected =
            index + 1u == fragment_count
                ? WORR_NATIVE_RX_MESSAGE_COMPLETE
                : WORR_NATIVE_RX_FRAGMENT_ACCEPTED;

        CHECK(Worr_NativeRxSessionAcceptV1(
                  &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena),
                  (uint64_t)(10 + index), high_snapshot[index],
                  high_snapshot_bytes[index], &new_message,
                  &repeat_ack) == expected);
    }
    CHECK(session.occupied_count == 2 &&
          slots[old_message.slot_index].state_flags ==
              (WORR_NATIVE_RX_SLOT_OCCUPIED |
               WORR_NATIVE_RX_SLOT_COMPLETE));
    CHECK(Worr_NativeRxSessionCommitV1(
              &session, slots, TEST_RX_CAPACITY, old_message.slot_index,
              old_message.message_sequence, &acknowledgement) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(session.committed_snapshot_epoch == 5 &&
          session.committed_snapshot_sequence == 1 &&
          session.occupied_count == 1);
    CHECK(Worr_NativeRxSessionCommitV1(
              &session, slots, TEST_RX_CAPACITY, new_message.slot_index,
              new_message.message_sequence, &acknowledgement) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(session.committed_snapshot_epoch == 999 &&
          session.committed_snapshot_sequence == 1 &&
          session.occupied_count == 0 &&
          session.telemetry.fragment_timeouts == 1 &&
          session.telemetry.message_checksum_failures == 1 &&
          Worr_NativeRxSessionValidateV1(
              &session, slots, TEST_RX_CAPACITY));
    return true;
}

static bool test_rx_snapshot_tombstone_pressure(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[TEST_RX_CAPACITY];
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 acknowledgement;
    uint8_t retry_datagrams[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS]
                           [WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t retry_datagram_bytes[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS];
    uint16_t fragment_count;
    uint16_t retry_fragment_count = 0;
    uint32_t sequence;
    uint32_t active_slot = UINT32_MAX;
    uint16_t index;
    bool committed_identity_found = false;
    bool retry_identity_found = false;

    CHECK(make_binding(48, &binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &session, slots, TEST_RX_CAPACITY, TEST_PAYLOAD_STRIDE,
        1000, 1000, &binding));

    fill_payload(64, 1);
    CHECK(encode_message(
        48, 1, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 1, 1),
        64, 700, 2, &fragment_count));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 1,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(Worr_NativeRxSessionCommitV1(
              &session, slots, TEST_RX_CAPACITY, message.slot_index,
              message.message_sequence, &acknowledgement) ==
          WORR_NATIVE_RX_COMMITTED);

    for (sequence = 2;
         sequence <= WORR_NATIVE_SESSION_SNAPSHOT_TOMBSTONE_CAPACITY;
         ++sequence) {
        const uint32_t bytes = sequence == 2 ? 1800u : 64u;

        fill_payload(bytes, sequence);
        CHECK(encode_message(
            48, sequence,
            make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 1, sequence),
            bytes, 700, 2, &fragment_count));
        if (sequence == 2) {
            retry_fragment_count = fragment_count;
            CHECK(retry_fragment_count > 1);
            for (index = 0; index < retry_fragment_count; ++index) {
                memcpy(retry_datagrams[index], datagrams[index],
                       datagram_bytes[index]);
                retry_datagram_bytes[index] = datagram_bytes[index];
            }
        }
        for (index = 0; index < fragment_count; ++index) {
            const worr_native_rx_result_v1 expected =
                index + 1u == fragment_count
                    ? WORR_NATIVE_RX_MESSAGE_COMPLETE
                    : WORR_NATIVE_RX_FRAGMENT_ACCEPTED;

            CHECK(Worr_NativeRxSessionAcceptV1(
                      &session, slots, TEST_RX_CAPACITY, arena,
                      sizeof(arena), sequence, datagrams[index],
                      datagram_bytes[index], &message,
                      &repeat_ack) == expected);
        }
        CHECK(Worr_NativeRxSessionDiscardV1(
                  &session, slots, TEST_RX_CAPACITY, message.slot_index,
                  message.message_sequence) == WORR_NATIVE_RX_DISCARDED);
    }
    CHECK(session.snapshot_tombstone_count ==
          WORR_NATIVE_SESSION_SNAPSHOT_TOMBSTONE_CAPACITY);

    /* Starting an exact retry moves its retained identity to the MRU tail. */
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 17,
              retry_datagrams[0], retry_datagram_bytes[0], &message,
              &repeat_ack) == WORR_NATIVE_RX_FRAGMENT_ACCEPTED);
    for (index = 0; index < TEST_RX_CAPACITY; ++index) {
        if (slots[index].state_flags != 0 &&
            slots[index].reassembly.message_sequence == 2) {
            active_slot = index;
        }
    }
    CHECK(active_slot != UINT32_MAX &&
          (slots[active_slot].state_flags &
           WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY) != 0);

    /* Churn every cache position while the retry remains active. Its slot
     * authorization must survive even after its tombstone is evicted. */
    for (sequence = 17; sequence <= 32; ++sequence) {
        worr_native_rx_message_v1 pressure_message;

        fill_payload(64, sequence);
        CHECK(encode_message(
            48, sequence,
            make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 1, sequence),
            64, 700, 2, &fragment_count));
        CHECK(Worr_NativeRxSessionAcceptV1(
                  &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena),
                  (uint64_t)(sequence + 1u), datagrams[0],
                  datagram_bytes[0], &pressure_message,
                  &repeat_ack) == WORR_NATIVE_RX_MESSAGE_COMPLETE);
        CHECK(Worr_NativeRxSessionDiscardV1(
                  &session, slots, TEST_RX_CAPACITY,
                  pressure_message.slot_index,
                  pressure_message.message_sequence) ==
              WORR_NATIVE_RX_DISCARDED);
    }
    for (index = 0; index < session.snapshot_tombstone_count; ++index) {
        if (session.snapshot_tombstones[index].record.object_sequence == 1 &&
            session.snapshot_tombstones[index].state_flags ==
                WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED) {
            committed_identity_found = true;
        }
        if (session.snapshot_tombstones[index].record.object_sequence == 2)
            retry_identity_found = true;
    }
    CHECK(committed_identity_found);
    CHECK(!retry_identity_found);
    CHECK(session.telemetry.snapshot_tombstone_evictions == 16);
    CHECK(Worr_NativeRxSessionValidateV1(
        &session, slots, TEST_RX_CAPACITY));

    CHECK(rx_commit_single(
        &session, slots, TEST_RX_CAPACITY, 66, 66, 34));
    CHECK(session.highest_committed_sequence == 66 &&
          session.occupied_count == 1 &&
          Worr_NativeRxSessionValidateV1(
              &session, slots, TEST_RX_CAPACITY));

    for (index = 1; index < retry_fragment_count; ++index) {
        const worr_native_rx_result_v1 expected =
            index + 1u == retry_fragment_count
                ? WORR_NATIVE_RX_MESSAGE_COMPLETE
                : WORR_NATIVE_RX_FRAGMENT_ACCEPTED;

        CHECK(Worr_NativeRxSessionAcceptV1(
                  &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena),
                  (uint64_t)(34 + index), retry_datagrams[index],
                  retry_datagram_bytes[index], &message,
                  &repeat_ack) == expected);
    }
    CHECK(message.message_sequence == 2 &&
          message.slot_index == active_slot);
    CHECK(Worr_NativeRxSessionDiscardV1(
              &session, slots, TEST_RX_CAPACITY, message.slot_index,
              message.message_sequence) == WORR_NATIVE_RX_DISCARDED);

    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    for (index = 0; index < retry_fragment_count; ++index) {
        const worr_native_rx_result_v1 expected =
            index + 1u == retry_fragment_count
                ? WORR_NATIVE_RX_MESSAGE_COMPLETE
                : WORR_NATIVE_RX_FRAGMENT_ACCEPTED;

        CHECK(Worr_NativeRxSessionAcceptV1(
                  &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena),
                  (uint64_t)(40 + index), retry_datagrams[index],
                  retry_datagram_bytes[index], &message,
                  &repeat_ack) == expected);
    }
    CHECK(repeat_ack.struct_size == UINT32_C(0xa5a5a5a5) &&
          (slots[message.slot_index].state_flags &
           WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY) != 0 &&
          Worr_NativeRxSessionValidateV1(
              &session, slots, TEST_RX_CAPACITY));
    return true;
}

static bool test_rx_reassembly_commit_replay_and_conflict(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[TEST_RX_CAPACITY];
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 acknowledgement;
    worr_native_record_ref_v1 record =
        make_record(WORR_NATIVE_RECORD_EVENT_V1, 5, 9);
    uint16_t fragment_count;
    uint16_t index;

    CHECK(make_binding(30, &binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &session, slots, TEST_RX_CAPACITY, TEST_PAYLOAD_STRIDE,
        5, 9, &binding));
    memset(arena, 0xcc, sizeof(arena));
    fill_payload(2600, 1);
    CHECK(encode_message(30, 7, record, 2600, 700, 2,
                         &fragment_count));
    CHECK(fragment_count > 1);

    for (index = fragment_count; index-- > 0;) {
        const worr_native_rx_result_v1 result =
            Worr_NativeRxSessionAcceptV1(
                &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena),
                (uint64_t)(10 + fragment_count - index), datagrams[index],
                datagram_bytes[index], &message, &repeat_ack);
        if (index == 0)
            CHECK(result == WORR_NATIVE_RX_MESSAGE_COMPLETE);
        else
            CHECK(result == WORR_NATIVE_RX_FRAGMENT_ACCEPTED);
    }
    CHECK(message.transport_epoch == 30 &&
          message.connection_owner_id == binding.connection_owner_id &&
          message.message_sequence == 7 &&
          message.payload_bytes == 2600 &&
          message.slot_index < TEST_RX_CAPACITY);
    CHECK(memcmp(arena + message.payload_offset, payload, 2600) == 0);
    CHECK(session.occupied_count == 1);
    CHECK(Worr_NativeRxSessionCommitV1(
              &session, slots, TEST_RX_CAPACITY, message.slot_index,
              message.message_sequence, &acknowledgement) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(Worr_NativeAckRangeValidateV1(&acknowledgement));
    CHECK(acknowledgement.first_message_sequence == 7 &&
          acknowledgement.last_message_sequence == 7 &&
          acknowledgement.connection_owner_id ==
              binding.connection_owner_id);
    CHECK(session.occupied_count == 0 && session.history_count == 1);

    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 20,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_ALREADY_COMMITTED);
    CHECK(Worr_NativeAckRangeValidateV1(&repeat_ack) &&
          repeat_ack.first_message_sequence == 7 &&
          repeat_ack.last_message_sequence == 7 &&
          repeat_ack.connection_owner_id == binding.connection_owner_id);
    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(encode_message(
        30, 7, record, 2600, 700, 3, &fragment_count));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, TEST_RX_CAPACITY, arena, sizeof(arena), 21,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_CONFLICT);
    CHECK(repeat_ack.struct_size == UINT32_C(0xa5a5a5a5));
    CHECK(session.occupied_count == 0 && session.history_count == 1);
    CHECK(session.telemetry.messages_completed == 1 &&
          session.telemetry.commits == 1 &&
          session.telemetry.already_committed == 1 &&
          session.telemetry.repeat_acknowledgements == 1 &&
          session.telemetry.message_conflicts == 1);
    CHECK(Worr_NativeRxSessionValidateV1(&session, slots,
                                         TEST_RX_CAPACITY));
    return true;
}

static bool test_extended_fragment_capacity(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_tx_session_v1 tx;
    worr_native_tx_session_v1 tx_before;
    worr_native_tx_slot_v1 tx_slots[1];
    worr_native_tx_slot_v1 tx_slots_before[1];
    worr_native_rx_session_v1 rx;
    worr_native_rx_slot_v1 rx_slots[1];
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 acknowledgement;
    uint32_t message_sequence = UINT32_MAX;
    uint16_t fragment_count;
    uint16_t index;

    CHECK(make_binding(61, &binding));
    CHECK(Worr_NativeTxSessionInitV1(
        &tx, tx_slots, 1, &binding));
    tx_before = tx;
    memcpy(tx_slots_before, tx_slots, sizeof(tx_slots));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, tx_slots, 1,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 61, 1),
              3, 9001, WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES,
              1079, 1, &message_sequence) ==
          WORR_NATIVE_TX_INVALID_ARGUMENT);
    CHECK(message_sequence == UINT32_MAX &&
          memcmp(&tx, &tx_before, sizeof(tx)) == 0 &&
          memcmp(tx_slots, tx_slots_before, sizeof(tx_slots)) == 0);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, tx_slots, 1,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 61, 1),
              3, 9001, WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES,
              1080, 1, &message_sequence) ==
          WORR_NATIVE_TX_RETAINED);
    CHECK(message_sequence == 1 &&
          tx_slots[0].payload_bytes ==
              WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES &&
          tx_slots[0].fragment_stride == 1024 &&
          tx_slots[0].fragment_count ==
              WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS);
    CHECK(Worr_NativeTxSessionValidateV1(&tx, tx_slots, 1));

    CHECK(Worr_NativeRxSessionInitV1(
        &rx, rx_slots, 1, TEST_PAYLOAD_STRIDE,
        1000, 2000, &binding));
    fill_payload(TEST_PAYLOAD_STRIDE, 61);
    CHECK(encode_message(
        61, 1, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 61, 1),
        TEST_PAYLOAD_STRIDE, 112, 3, &fragment_count));
    CHECK(fragment_count == 74);
    memset(arena, 0xa5, sizeof(arena));
    for (index = fragment_count; index-- > 0;) {
        const worr_native_rx_result_v1 result =
            Worr_NativeRxSessionAcceptV1(
                &rx, rx_slots, 1, arena, TEST_PAYLOAD_STRIDE,
                (uint64_t)(10 + fragment_count - index),
                datagrams[index], datagram_bytes[index],
                &message, &repeat_ack);

        CHECK(datagram_bytes[index] <= 112 &&
              datagram_bytes[index] <=
                  WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES);
        CHECK(result ==
              (index == 0
                   ? WORR_NATIVE_RX_MESSAGE_COMPLETE
                   : WORR_NATIVE_RX_FRAGMENT_ACCEPTED));
        if (index == (uint16_t)(fragment_count - 1u)) {
            worr_native_rx_slot_v1 malformed_slot = rx_slots[0];

            CHECK(rx_slots[0].reassembly.received_bitmap[0] == 0);
            CHECK(rx_slots[0].reassembly.received_bitmap[1] ==
                  (UINT64_C(1) << 9));
            malformed_slot.reassembly.received_bitmap[1] |=
                UINT64_C(1) << 63;
            CHECK(!Worr_NativeRxSessionValidateV1(
                &rx, &malformed_slot, 1));
        }
    }
    CHECK(rx_slots[0].reassembly.received_bitmap[0] == UINT64_MAX &&
          rx_slots[0].reassembly.received_bitmap[1] ==
              ((UINT64_C(1) << 10) - 1u));
    CHECK(message.payload_bytes == TEST_PAYLOAD_STRIDE &&
          memcmp(arena + message.payload_offset,
                 payload, TEST_PAYLOAD_STRIDE) == 0);
    CHECK(Worr_NativeRxSessionValidateV1(&rx, rx_slots, 1));
    CHECK(Worr_NativeRxSessionCommitV1(
              &rx, rx_slots, 1, message.slot_index,
              message.message_sequence, &acknowledgement) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(Worr_NativeAckRangeValidateV1(&acknowledgement));
    return true;
}

static bool test_rx_timeout_discard_and_capacity(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[1];
    worr_native_rx_message_v1 message;
    uint8_t first_message[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t first_message_bytes;
    uint16_t fragment_count;
    uint32_t expired = 99;

    CHECK(make_binding(40, &binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &session, slots, 1, TEST_PAYLOAD_STRIDE, 5, 7, &binding));
    fill_payload(1800, 3);
    CHECK(encode_message(
        40, 1, make_record(WORR_NATIVE_RECORD_COMMAND_V1, 3, 1),
        1800, 700, 1, &fragment_count));
    CHECK(fragment_count > 1);
    memcpy(first_message, datagrams[0], datagram_bytes[0]);
    first_message_bytes = datagram_bytes[0];
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 10,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_FRAGMENT_ACCEPTED);

    fill_payload(90, 4);
    CHECK(encode_message(
        40, 2, make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 3, 1),
        90, 700, 1, &fragment_count));
    CHECK(fragment_count == 1);
    memset(&message, 0xa5, sizeof(message));
    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 11,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_CAPACITY);
    CHECK(session.occupied_count == 1 &&
          session.committed_snapshot_epoch == 0 &&
          session.committed_snapshot_sequence == 0 &&
          session.snapshot_tombstone_count == 0 &&
          message.struct_size == UINT32_C(0xa5a5a5a5) &&
          repeat_ack.struct_size == UINT32_C(0xa5a5a5a5));
    CHECK(Worr_NativeRxSessionExpireV1(
              &session, slots, 1, 14, &expired) == WORR_NATIVE_RX_IDLE);
    CHECK(expired == 0 && session.occupied_count == 1);
    CHECK(Worr_NativeRxSessionExpireV1(
              &session, slots, 1, 15, &expired) == WORR_NATIVE_RX_EXPIRED);
    CHECK(expired == 1 && session.occupied_count == 0);

    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 16,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(message.message_sequence == 2 &&
          session.committed_snapshot_epoch == 0 &&
          session.committed_snapshot_sequence == 0);
    CHECK(Worr_NativeRxSessionDiscardV1(
              &session, slots, 1, message.slot_index,
              message.message_sequence) == WORR_NATIVE_RX_DISCARDED);

    fill_payload(90, 5);
    CHECK(encode_message(
        40, 3, make_record(WORR_NATIVE_RECORD_EVENT_V1, 3, 2),
        90, 700, 1, &fragment_count));
    CHECK(fragment_count == 1);
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 17,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(Worr_NativeRxSessionDiscardV1(
              &session, slots, 1, message.slot_index,
              message.message_sequence) == WORR_NATIVE_RX_DISCARDED);
    CHECK(session.history_count == 0 && session.occupied_count == 0);
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 18,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(Worr_NativeRxSessionExpireV1(
              &session, slots, 1, 24, &expired) == WORR_NATIVE_RX_IDLE);
    CHECK(Worr_NativeRxSessionExpireV1(
              &session, slots, 1, 25, &expired) == WORR_NATIVE_RX_EXPIRED);
    CHECK(expired == 1 && session.occupied_count == 0);
    CHECK(session.telemetry.fragment_timeouts == 1 &&
          session.telemetry.complete_timeouts == 1 &&
          session.telemetry.discards == 2 &&
          session.telemetry.capacity_stalls == 1);

    /* An expired incomplete message can start again with the same sequence. */
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 26,
              first_message, first_message_bytes, &message, &repeat_ack) ==
          WORR_NATIVE_RX_FRAGMENT_ACCEPTED);
    return true;
}

static bool test_rx_malformed_alias_and_epoch_transactionality(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_rx_session_v1 session;
    worr_native_rx_session_v1 session_before;
    worr_native_rx_slot_v1 slots[1];
    worr_native_rx_slot_v1 slots_before[1];
    worr_native_rx_message_v1 message;
    uint8_t arena_before[TEST_PAYLOAD_STRIDE];
    uint8_t corrupt[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint16_t fragment_count;

    CHECK(make_binding(50, &binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &session, slots, 1, TEST_PAYLOAD_STRIDE, 5, 7, &binding));
    memset(arena, 0x5a, TEST_PAYLOAD_STRIDE);
    fill_payload(100, 7);
    CHECK(encode_message(
        50, 1, make_record(WORR_NATIVE_RECORD_EVENT_V1, 9, 1),
        100, 700, 0, &fragment_count));
    CHECK(fragment_count == 1);

    session_before = session;
    memcpy(slots_before, slots, sizeof(slots));
    memcpy(arena_before, arena, TEST_PAYLOAD_STRIDE);
    memset(&repeat_ack, 0xa5, sizeof(repeat_ack));
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 1,
              datagrams[0], 10, &message, &repeat_ack) ==
          WORR_NATIVE_RX_MALFORMED);
    ++session_before.telemetry.datagram_attempts;
    ++session_before.telemetry.malformed;
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(memcmp(arena, arena_before, TEST_PAYLOAD_STRIDE) == 0);
    CHECK(repeat_ack.struct_size == UINT32_C(0xa5a5a5a5));

    memcpy(corrupt, datagrams[0], datagram_bytes[0]);
    corrupt[datagram_bytes[0] - 1u] ^= 0x80u;
    session_before = session;
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 1,
              corrupt, datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_DATAGRAM_CORRUPT);
    ++session_before.telemetry.datagram_attempts;
    ++session_before.telemetry.datagram_corrupt;
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(memcmp(arena, arena_before, TEST_PAYLOAD_STRIDE) == 0);

    fill_payload(100, 8);
    CHECK(encode_message(
        51, 2, make_record(WORR_NATIVE_RECORD_EVENT_V1, 9, 2),
        100, 700, 0, &fragment_count));
    session_before = session;
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 1,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_WRONG_EPOCH);
    ++session_before.telemetry.datagram_attempts;
    ++session_before.telemetry.wrong_epoch;
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(memcmp(arena, arena_before, TEST_PAYLOAD_STRIDE) == 0);

    session_before = session;
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 1,
              arena, datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_INVALID_ARGUMENT);
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(memcmp(arena, arena_before, TEST_PAYLOAD_STRIDE) == 0);

    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 1,
              datagrams[0], datagram_bytes[0],
              (worr_native_rx_message_v1 *)slots, &repeat_ack) ==
          WORR_NATIVE_RX_INVALID_ARGUMENT);
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 1,
              datagrams[0], datagram_bytes[0], &message,
              (worr_native_ack_range_v1 *)slots) ==
          WORR_NATIVE_RX_INVALID_ARGUMENT);
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 1,
              datagrams[0], datagram_bytes[0], &message,
              (worr_native_ack_range_v1 *)&message) ==
          WORR_NATIVE_RX_INVALID_ARGUMENT);
    CHECK(memcmp(&session, &session_before, sizeof(session)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    session.telemetry.datagram_attempts = UINT64_MAX;
    session.telemetry.malformed = UINT64_MAX;
    CHECK(Worr_NativeRxSessionAcceptV1(
              &session, slots, 1, arena, TEST_PAYLOAD_STRIDE, 1,
              datagrams[0], 10, &message, &repeat_ack) ==
          WORR_NATIVE_RX_MALFORMED);
    CHECK(session.telemetry.datagram_attempts == UINT64_MAX &&
          session.telemetry.malformed == UINT64_MAX);
    return true;
}

static bool test_rx_epoch_advance(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_session_binding_v1 next_binding;
    worr_native_session_binding_v1 wrong_owner_binding;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[1];

    CHECK(make_binding(60, &binding));
    CHECK(make_binding(61, &next_binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &session, slots, 1, 512, 3, 5, &binding));
    session.telemetry.datagram_attempts = 10;
    CHECK(Worr_NativeRxSessionAdvanceEpochV1(
        &session, slots, 1, &next_binding));
    CHECK(session.transport_epoch == 61 &&
          session.connection_owner_id == TEST_CONNECTION_OWNER_ID &&
          session.payload_stride == 512 &&
          session.fragment_timeout_ticks == 3 &&
          session.complete_timeout_ticks == 5 &&
          session.telemetry.datagram_attempts == 0);
    CHECK(memory_is_zero(slots, sizeof(slots)));
    CHECK(!Worr_NativeRxSessionAdvanceEpochV1(
        &session, slots, 1, &binding));
    CHECK(make_binding_for_owner(
        62, TEST_OTHER_CONNECTION_OWNER_ID, &wrong_owner_binding));
    CHECK(!Worr_NativeRxSessionAdvanceEpochV1(
        &session, slots, 1, &wrong_owner_binding));
    CHECK(session.transport_epoch == 61 &&
          session.connection_owner_id == TEST_CONNECTION_OWNER_ID);
    return true;
}

static bool test_explicit_tx_and_rx_cancellation(void)
{
    worr_native_session_binding_v1 binding;
    worr_native_session_binding_v1 next_binding;
    worr_native_tx_session_v1 tx;
    worr_native_tx_session_v1 tx_before;
    worr_native_tx_session_v1 tx_expected;
    worr_native_tx_slot_v1 tx_slots[TEST_TX_CAPACITY];
    worr_native_tx_slot_v1 tx_slots_before[TEST_TX_CAPACITY];
    worr_native_tx_send_ticket_v1 ticket;
    worr_native_rx_session_v1 rx;
    worr_native_rx_session_v1 rx_before;
    worr_native_rx_session_v1 rx_expected;
    worr_native_rx_slot_v1 rx_slots[TEST_RX_CAPACITY];
    worr_native_rx_slot_v1 rx_slots_before[TEST_RX_CAPACITY];
    worr_native_rx_cancel_report_v1 report;
    worr_native_rx_message_v1 message;
    uint16_t fragment_count;
    uint32_t sequence;
    uint32_t cancelled;

    CHECK(make_binding(59, &binding));
    CHECK(make_binding(60, &next_binding));
    CHECK(Worr_NativeTxSessionInitV1(
        &tx, tx_slots, TEST_TX_CAPACITY, &binding));

    /* Empty cancellation is successful, counted, and enters a terminal state
     * whose second cancellation is byte-idempotent. */
    tx_before = tx;
    memcpy(tx_slots_before, tx_slots, sizeof(tx_slots));
    tx_expected = tx;
    tx_expected.state_flags |= WORR_NATIVE_TX_TERMINAL_CANCELLED;
    cancelled = UINT32_MAX;
    CHECK(Worr_NativeTxSessionCancelRetainedV1(
              &tx, tx_slots, TEST_TX_CAPACITY, &cancelled) ==
          WORR_NATIVE_TX_CANCELLED);
    CHECK(cancelled == 0 &&
          memcmp(&tx, &tx_expected, sizeof(tx)) == 0 &&
          memcmp(tx_slots, tx_slots_before, sizeof(tx_slots)) == 0);
    tx_before = tx;
    CHECK(Worr_NativeTxSessionCancelRetainedV1(
              &tx, tx_slots, TEST_TX_CAPACITY, &cancelled) ==
          WORR_NATIVE_TX_CANCELLED);
    CHECK(cancelled == 0 && memcmp(&tx, &tx_before, sizeof(tx)) == 0);
    CHECK(Worr_NativeTxSessionAdvanceEpochV1(
        &tx, tx_slots, TEST_TX_CAPACITY, &next_binding));
    CHECK(tx.transport_epoch == 60 && tx.retained_count == 0 &&
          tx.next_message_sequence == 1 &&
          (tx.state_flags & WORR_NATIVE_TX_TERMINAL_CANCELLED) == 0);
    CHECK(Worr_NativeTxSessionInitV1(
        &tx, tx_slots, TEST_TX_CAPACITY, &binding));

    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, tx_slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_COMMAND_V1, 59, 1),
              2, 101, 80, 700, 10, &sequence) ==
          WORR_NATIVE_TX_RETAINED);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, tx_slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 59, 2),
              3, 102, 96, 700, 11, &sequence) ==
          WORR_NATIVE_TX_RETAINED);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, tx_slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 59, 3),
              1, 103, 112, 700, 12, &sequence) ==
          WORR_NATIVE_TX_RETAINED);
    CHECK(tx.retained_count == 3 && tx.next_message_sequence == 4 &&
          tx.latest_snapshot_epoch == 59 &&
          tx.latest_snapshot_sequence == 2);
    CHECK(Worr_NativeTxSessionPrepareDueV1(
              &tx, tx_slots, TEST_TX_CAPACITY, 12, 100, &ticket) ==
          WORR_NATIVE_TX_SELECTED);

    /* Aliasing is rejected without touching retained storage or output. */
    tx_before = tx;
    memcpy(tx_slots_before, tx_slots, sizeof(tx_slots));
    CHECK(Worr_NativeTxSessionCancelRetainedV1(
              &tx, tx_slots, TEST_TX_CAPACITY,
              (uint32_t *)&tx_slots[0]) ==
          WORR_NATIVE_TX_INVALID_ARGUMENT);
    CHECK(memcmp(&tx, &tx_before, sizeof(tx)) == 0 &&
          memcmp(tx_slots, tx_slots_before, sizeof(tx_slots)) == 0);

    tx_expected = tx;
    tx_expected.retained_count = 0;
    tx_expected.state_flags |= WORR_NATIVE_TX_TERMINAL_CANCELLED;
    cancelled = UINT32_MAX;
    CHECK(Worr_NativeTxSessionCancelRetainedV1(
              &tx, tx_slots, TEST_TX_CAPACITY, &cancelled) ==
          WORR_NATIVE_TX_CANCELLED);
    CHECK(cancelled == 3 &&
          memcmp(&tx, &tx_expected, sizeof(tx)) == 0 &&
          memory_is_zero(tx_slots, sizeof(tx_slots)) &&
          Worr_NativeTxSessionValidateV1(
              &tx, tx_slots, TEST_TX_CAPACITY));
    /* Prepare tickets are non-mutating proofs, not unknown transport
     * outcomes.  Cancellation makes every copied ticket stale. */
    CHECK(!Worr_NativeTxSessionPreparedValidateV1(
        &tx, tx_slots, TEST_TX_CAPACITY, &ticket));
    tx_before = tx;
    sequence = UINT32_MAX;
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, tx_slots, TEST_TX_CAPACITY,
              make_record(WORR_NATIVE_RECORD_EVENT_V1, 59, 4),
              1, 104, 64, 700, 13, &sequence) ==
          WORR_NATIVE_TX_INVALID_STATE);
    CHECK(sequence == UINT32_MAX &&
          memcmp(&tx, &tx_before, sizeof(tx)) == 0);
    tx_before = tx;
    cancelled = UINT32_MAX;
    CHECK(Worr_NativeTxSessionCancelRetainedV1(
              &tx, tx_slots, TEST_TX_CAPACITY, &cancelled) ==
          WORR_NATIVE_TX_CANCELLED);
    CHECK(cancelled == 0 && memcmp(&tx, &tx_before, sizeof(tx)) == 0);

    CHECK(Worr_NativeRxSessionInitV1(
        &rx, rx_slots, TEST_RX_CAPACITY, TEST_PAYLOAD_STRIDE,
        5, 9, &binding));
    memset(&report, 0xa5, sizeof(report));
    rx_before = rx;
    rx_expected = rx;
    rx_expected.state_flags |= WORR_NATIVE_RX_TERMINAL_CANCELLED;
    CHECK(Worr_NativeRxSessionCancelPendingV1(
              &rx, rx_slots, TEST_RX_CAPACITY, &report) ==
          WORR_NATIVE_RX_CANCELLED);
    CHECK(report.incomplete_messages == 0 &&
          report.complete_messages == 0 &&
          memcmp(&rx, &rx_expected, sizeof(rx)) == 0);
    rx_before = rx;
    CHECK(Worr_NativeRxSessionCancelPendingV1(
              &rx, rx_slots, TEST_RX_CAPACITY, &report) ==
          WORR_NATIVE_RX_CANCELLED);
    CHECK(report.incomplete_messages == 0 &&
          report.complete_messages == 0 &&
          memcmp(&rx, &rx_before, sizeof(rx)) == 0);
    CHECK(Worr_NativeRxSessionInitV1(
        &rx, rx_slots, TEST_RX_CAPACITY, TEST_PAYLOAD_STRIDE,
        5, 9, &binding));

    /* Preserve committed replay and semantic high-water state while
     * disposing one incomplete and one complete-but-uncommitted message. */
    CHECK(rx_commit_single(
        &rx, rx_slots, TEST_RX_CAPACITY, 1, 1, 1));
    fill_payload(1000, 2);
    CHECK(encode_message(
        59, 2, make_record(WORR_NATIVE_RECORD_EVENT_V1, 59, 2),
        1000, 300, 2, &fragment_count));
    CHECK(fragment_count > 1);
    CHECK(Worr_NativeRxSessionAcceptV1(
              &rx, rx_slots, TEST_RX_CAPACITY, arena, sizeof(arena), 2,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_FRAGMENT_ACCEPTED);
    fill_payload(48, 3);
    CHECK(encode_message(
        59, 3, make_record(WORR_NATIVE_RECORD_EVENT_V1, 59, 3),
        48, 700, 1, &fragment_count));
    CHECK(fragment_count == 1);
    CHECK(Worr_NativeRxSessionAcceptV1(
              &rx, rx_slots, TEST_RX_CAPACITY, arena, sizeof(arena), 3,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(rx.occupied_count == 2 && rx.highest_committed_sequence == 1 &&
          rx.history_count == 1 && rx.receipt_mask == UINT64_C(1));

    rx_before = rx;
    memcpy(rx_slots_before, rx_slots, sizeof(rx_slots));
    CHECK(Worr_NativeRxSessionCancelPendingV1(
              &rx, rx_slots, TEST_RX_CAPACITY,
              (worr_native_rx_cancel_report_v1 *)&rx_slots[0]) ==
          WORR_NATIVE_RX_INVALID_ARGUMENT);
    CHECK(memcmp(&rx, &rx_before, sizeof(rx)) == 0 &&
          memcmp(rx_slots, rx_slots_before, sizeof(rx_slots)) == 0);

    rx_expected = rx;
    rx_expected.occupied_count = 0;
    rx_expected.state_flags |= WORR_NATIVE_RX_TERMINAL_CANCELLED;
    memset(&report, 0xa5, sizeof(report));
    CHECK(Worr_NativeRxSessionCancelPendingV1(
              &rx, rx_slots, TEST_RX_CAPACITY, &report) ==
          WORR_NATIVE_RX_CANCELLED);
    CHECK(report.incomplete_messages == 1 &&
          report.complete_messages == 1 &&
          memcmp(&rx, &rx_expected, sizeof(rx)) == 0 &&
          memory_is_zero(rx_slots, sizeof(rx_slots)) &&
          rx.highest_committed_sequence == 1 && rx.history_count == 1 &&
          rx.receipt_mask == UINT64_C(1) &&
          Worr_NativeRxSessionValidateV1(
              &rx, rx_slots, TEST_RX_CAPACITY));
    rx_before = rx;
    CHECK(Worr_NativeRxSessionAcceptV1(
              &rx, rx_slots, TEST_RX_CAPACITY, arena, sizeof(arena), 4,
              datagrams[0], datagram_bytes[0], &message, &repeat_ack) ==
          WORR_NATIVE_RX_INVALID_STATE);
    CHECK(memcmp(&rx, &rx_before, sizeof(rx)) == 0);
    rx_before = rx;
    CHECK(Worr_NativeRxSessionCancelPendingV1(
              &rx, rx_slots, TEST_RX_CAPACITY, &report) ==
          WORR_NATIVE_RX_CANCELLED);
    CHECK(report.incomplete_messages == 0 &&
          report.complete_messages == 0 &&
          memcmp(&rx, &rx_before, sizeof(rx)) == 0);
    CHECK(Worr_NativeRxSessionAdvanceEpochV1(
        &rx, rx_slots, TEST_RX_CAPACITY, &next_binding));
    CHECK(rx.transport_epoch == 60 && rx.occupied_count == 0 &&
          rx.history_count == 0 && rx.highest_committed_sequence == 0 &&
          (rx.state_flags & WORR_NATIVE_RX_TERMINAL_CANCELLED) == 0);
    return true;
}

int main(void)
{
    if (!test_binding_and_ack_validation() ||
        !test_binding_from_active_readiness() ||
        !test_receive_binding_during_event_confirmation() ||
        !test_tx_retention_supersession_and_ack() ||
        !test_tx_capacity_exhaustion_and_epoch_reset() ||
        !test_tx_receipt_window_and_scheduler_aging() ||
        !test_tx_prepared_send_confirmation() ||
        !test_same_epoch_cross_owner_rejection() ||
        !test_rx_reassembly_commit_replay_and_conflict() ||
        !test_extended_fragment_capacity() ||
        !test_rx_receipt_window_and_delayed_replay() ||
        !test_lost_commit_ack_releases_retained_tx() ||
        !test_rx_uint32_max_repeat_ack() ||
        !test_rx_snapshot_commit_freshness_and_retry() ||
        !test_rx_snapshot_tombstone_pressure() ||
        !test_rx_timeout_discard_and_capacity() ||
        !test_rx_malformed_alias_and_epoch_transactionality() ||
        !test_explicit_tx_and_rx_cancellation() ||
        !test_rx_epoch_advance()) {
        return 1;
    }
    puts("native transport session tests passed");
    return 0;
}
