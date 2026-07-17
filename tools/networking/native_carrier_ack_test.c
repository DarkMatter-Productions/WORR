/* Deterministic retained-ACK ownership, emission, and lifecycle tests. */

#include "common/net/native_carrier_ack.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "native_carrier_ack_test:%d: %s\n",        \
                    __LINE__, #condition);                                \
            return 1;                                                     \
        }                                                                 \
    } while (0)

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

enum {
    TEST_RX_CAPACITY = 4,
    TEST_PAYLOAD_STRIDE = 256,
};

#define TEST_OWNER_A UINT64_C(0x1020304050607080)
#define TEST_OWNER_B UINT64_C(0x8877665544332211)

typedef struct test_fixture_s {
    worr_native_session_binding_v1 binding;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[TEST_RX_CAPACITY];
    uint8_t arena[TEST_RX_CAPACITY * TEST_PAYLOAD_STRIDE];
    worr_native_carrier_ack_ledger_v1 ledger;
} test_fixture;

static worr_native_session_binding_v1 test_binding(uint32_t epoch,
                                                    uint64_t owner_id)
{
    worr_native_session_binding_v1 binding;

    memset(&binding, 0, sizeof(binding));
    binding.struct_size = sizeof(binding);
    binding.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    binding.transport_epoch = epoch;
    binding.negotiated_capabilities = WORR_NET_CAP_NATIVE_ENVELOPE_V1;
    binding.connection_owner_id = owner_id;
    return binding;
}

static worr_native_record_ref_v1 test_record(uint8_t record_class,
                                             uint32_t sequence)
{
    worr_native_record_ref_v1 record;

    memset(&record, 0, sizeof(record));
    record.record_class = record_class;
    record.record_schema_version = 1;
    record.object_epoch = 1;
    record.object_sequence = sequence;
    return record;
}

static int fixture_init_owner(test_fixture *fixture, uint32_t epoch,
                              uint64_t owner_id,
                              uint8_t proactive_handoffs)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->binding = test_binding(epoch, owner_id);
    CHECK(Worr_NativeSessionBindingValidateV1(&fixture->binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &fixture->session, fixture->slots, TEST_RX_CAPACITY,
        TEST_PAYLOAD_STRIDE, 1000, 1000, &fixture->binding));
    CHECK(Worr_NativeCarrierAckLedgerInitV1(
        &fixture->ledger, &fixture->binding, proactive_handoffs));
    return 0;
}

static int fixture_init(test_fixture *fixture, uint32_t epoch,
                        uint8_t proactive_handoffs)
{
    return fixture_init_owner(
        fixture, epoch, TEST_OWNER_A, proactive_handoffs);
}

static int emit_datagram(uint32_t epoch, uint32_t message_sequence,
                         uint8_t record_class, uint8_t payload_tag,
                         uint8_t *datagram_out,
                         size_t *datagram_bytes_out)
{
    const uint8_t payload[4] = {
        payload_tag, (uint8_t)(payload_tag + 1u),
        (uint8_t)(payload_tag + 2u), (uint8_t)(payload_tag + 3u)};
    worr_native_envelope_fragmenter_v1 fragmenter;

    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, epoch, message_sequence,
        test_record(record_class, message_sequence), 1, payload,
        sizeof(payload), WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES));
    CHECK(Worr_NativeEnvelopeFragmentNextV1(
              &fragmenter, payload, sizeof(payload), datagram_out,
              WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES,
              datagram_bytes_out) == WORR_NATIVE_ENVELOPE_EMIT_OK);
    return 0;
}

/* Build one WTC1 packet with an optional DATA entry followed by exact ACKs. */
static int encode_packet(
    uint32_t epoch, const void *legacy, size_t legacy_bytes,
    const uint8_t *datagram, size_t datagram_bytes,
    const worr_native_ack_range_v1 *ranges, uint16_t range_count,
    uint8_t *packet_out, size_t packet_capacity,
    size_t *packet_bytes_out)
{
    worr_native_carrier_entry_v1 entries[WORR_NATIVE_CARRIER_MAX_ENTRIES];
    const uint16_t data_count = datagram_bytes == 0 ? 0u : 1u;
    uint16_t index;

    CHECK((datagram_bytes == 0) == (datagram == NULL));
    CHECK((range_count == 0) == (ranges == NULL));
    CHECK((uint16_t)(data_count + range_count) <=
          WORR_NATIVE_CARRIER_MAX_ENTRIES);
    memset(entries, 0, sizeof(entries));
    if (data_count != 0) {
        entries[0].struct_size = sizeof(entries[0]);
        entries[0].schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
        entries[0].entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
        entries[0].data_offset = 0;
        entries[0].data_bytes = (uint32_t)datagram_bytes;
    }
    for (index = 0; index < range_count; ++index) {
        worr_native_carrier_entry_v1 *entry =
            &entries[(uint16_t)(data_count + index)];

        entry->struct_size = sizeof(*entry);
        entry->schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
        entry->entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
        entry->first_message_sequence =
            ranges[index].first_message_sequence;
        entry->last_message_sequence = ranges[index].last_message_sequence;
    }
    CHECK(Worr_NativeCarrierEncodeV1(
              epoch, legacy, legacy_bytes, datagram, datagram_bytes,
              entries, (uint16_t)(data_count + range_count), packet_out,
              packet_capacity, packet_bytes_out) == WORR_NATIVE_CARRIER_OK);
    return 0;
}

static int wrap_datagram(uint32_t epoch, const uint8_t *datagram,
                         size_t datagram_bytes, uint8_t *packet_out,
                         size_t *packet_bytes_out)
{
    return encode_packet(
        epoch, NULL, 0, datagram, datagram_bytes, NULL, 0, packet_out,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES, packet_bytes_out);
}

static int accept_data(
    test_fixture *fixture, const uint8_t *packet, size_t packet_bytes,
    uint64_t now_tick, worr_native_rx_result_v1 expected_rx,
    worr_native_rx_message_v1 *message_out)
{
    worr_native_rx_result_v1 rx_result = WORR_NATIVE_RX_INVALID_STATE;

    CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
              &fixture->session, fixture->slots, TEST_RX_CAPACITY,
              fixture->arena, sizeof(fixture->arena), now_tick, packet,
              packet_bytes, 0, &fixture->ledger, &rx_result,
              message_out) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(rx_result == expected_rx);
    return 0;
}

static int stage_complete(
    test_fixture *fixture, uint32_t message_sequence, uint8_t record_class,
    uint64_t now_tick, uint8_t *packet_out, size_t *packet_bytes_out,
    worr_native_rx_message_v1 *message_out)
{
    uint8_t datagram[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t datagram_bytes;

    CHECK(emit_datagram(
              fixture->binding.transport_epoch, message_sequence,
              record_class, (uint8_t)message_sequence, datagram,
              &datagram_bytes) == 0);
    CHECK(wrap_datagram(
              fixture->binding.transport_epoch, datagram, datagram_bytes,
              packet_out, packet_bytes_out) == 0);
    memset(message_out, 0xa5, sizeof(*message_out));
    CHECK(accept_data(
              fixture, packet_out, *packet_bytes_out, now_tick,
              WORR_NATIVE_RX_MESSAGE_COMPLETE, message_out) == 0);
    CHECK(message_out->message_sequence == message_sequence);
    CHECK(message_out->connection_owner_id ==
          fixture->binding.connection_owner_id);
    return 0;
}

static int commit_rx_only(test_fixture *fixture,
                          const worr_native_rx_message_v1 *message)
{
    worr_native_ack_range_v1 acknowledgement;

    memset(&acknowledgement, 0xa5, sizeof(acknowledgement));
    CHECK(Worr_NativeRxSessionCommitV1(
              &fixture->session, fixture->slots, TEST_RX_CAPACITY,
              message->slot_index, message->message_sequence,
              &acknowledgement) == WORR_NATIVE_RX_COMMITTED);
    CHECK(Worr_NativeAckRangeValidateV1(&acknowledgement));
    CHECK(acknowledgement.transport_epoch ==
              fixture->binding.transport_epoch &&
          acknowledgement.connection_owner_id ==
              fixture->binding.connection_owner_id &&
          acknowledgement.first_message_sequence ==
              message->message_sequence &&
          acknowledgement.last_message_sequence ==
              message->message_sequence);
    return 0;
}

static int accept_complete(
    test_fixture *fixture, uint32_t message_sequence, uint8_t record_class,
    uint64_t now_tick, uint8_t *packet_out, size_t *packet_bytes_out)
{
    worr_native_rx_message_v1 message;

    CHECK(stage_complete(
              fixture, message_sequence, record_class, now_tick,
              packet_out, packet_bytes_out, &message) == 0);
    CHECK(Worr_NativeCarrierSessionCommitRetainedV1(
              &fixture->session, fixture->slots, TEST_RX_CAPACITY,
              message.slot_index, message_sequence, &fixture->ledger) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture->ledger));
    return 0;
}

static worr_native_carrier_ack_receipt_v1 *find_receipt(
    worr_native_carrier_ack_ledger_v1 *ledger, uint32_t sequence)
{
    uint16_t index;

    for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY;
         ++index) {
        if (ledger->receipts[index].message_sequence == sequence &&
            (ledger->receipts[index].state_flags &
             WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED) != 0) {
            return &ledger->receipts[index];
        }
    }
    return NULL;
}

static int test_init_layout_epoch_and_owner(void)
{
    worr_native_carrier_ack_ledger_v1 ledger;
    worr_native_session_binding_v1 binding = test_binding(11, TEST_OWNER_A);
    worr_native_session_binding_v1 next = test_binding(12, TEST_OWNER_A);
    worr_native_session_binding_v1 wrong_owner =
        test_binding(13, TEST_OWNER_B);
    worr_native_session_binding_v1 invalid = test_binding(11, 0);

    memset(&ledger, 0xa5, sizeof(ledger));
    CHECK(!Worr_NativeCarrierAckLedgerInitV1(&ledger, &invalid, 3));
    CHECK(ledger.struct_size == UINT32_C(0xa5a5a5a5));
    CHECK(!Worr_NativeCarrierAckLedgerInitV1(&ledger, &binding, 0));
    CHECK(ledger.struct_size == UINT32_C(0xa5a5a5a5));
    CHECK(Worr_NativeCarrierAckLedgerInitV1(&ledger, &binding, 3));
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&ledger));
    CHECK(ledger.receipt_count == 0 && ledger.mutation_generation == 1 &&
          ledger.next_token_id == 1 && ledger.transport_epoch == 11 &&
          ledger.connection_owner_id == TEST_OWNER_A &&
          ledger.proactive_handoffs == 3);
    CHECK(!Worr_NativeCarrierAckLedgerAdvanceEpochV1(&ledger, &binding));
    CHECK(!Worr_NativeCarrierAckLedgerAdvanceEpochV1(
        &ledger, &wrong_owner));
    CHECK(Worr_NativeCarrierAckLedgerAdvanceEpochV1(&ledger, &next));
    CHECK(ledger.transport_epoch == 12 && ledger.receipt_count == 0 &&
          ledger.mutation_generation == 1 && ledger.next_token_id == 1 &&
          ledger.connection_owner_id == TEST_OWNER_A &&
          ledger.proactive_handoffs == 3);
    return 0;
}

static int test_semantic_commit_and_repeat_gates(void)
{
    static const uint8_t semantic_classes[] = {
        WORR_NATIVE_RECORD_SNAPSHOT_V1,
        WORR_NATIVE_RECORD_EVENT_V1,
        WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1,
    };
    test_fixture fixture;
    test_fixture before;
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes;
    worr_native_rx_message_v1 completed;
    worr_native_rx_message_v1 output;
    worr_native_rx_message_v1 output_before;
    worr_native_rx_result_v1 rx_result;
    worr_native_rx_result_v1 rx_result_before;
    size_t index;

    for (index = 0; index < ARRAY_COUNT(semantic_classes); ++index) {
        CHECK(fixture_init(
                  &fixture, (uint32_t)(13u + index), 2) == 0);
        CHECK(stage_complete(
                  &fixture, 1, semantic_classes[index], 1, packet,
                  &packet_bytes, &completed) == 0);

        before = fixture;
        CHECK(Worr_NativeCarrierSessionCommitRetainedV1(
                  &fixture.session, fixture.slots, TEST_RX_CAPACITY,
                  completed.slot_index, completed.message_sequence,
                  &fixture.ledger) ==
              WORR_NATIVE_RX_SEMANTIC_ADMISSION_REQUIRED);
        CHECK(memcmp(&fixture, &before, sizeof(fixture)) == 0);

        CHECK(commit_rx_only(&fixture, &completed) == 0);
        CHECK(fixture.ledger.receipt_count == 0 &&
              fixture.ledger.telemetry.repeat_refreshes == 0);
        before = fixture;
        rx_result = WORR_NATIVE_RX_MESSAGE_CHECKSUM;
        rx_result_before = rx_result;
        memset(&output, 0xa5, sizeof(output));
        output_before = output;
        CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
                  &fixture.session, fixture.slots, TEST_RX_CAPACITY,
                  fixture.arena, sizeof(fixture.arena), 2, packet,
                  packet_bytes, 0, &fixture.ledger, &rx_result,
                  &output) ==
              WORR_NATIVE_CARRIER_SESSION_SEMANTIC_REVALIDATION_REQUIRED);
        CHECK(memcmp(&fixture, &before, sizeof(fixture)) == 0 &&
              rx_result == rx_result_before &&
              memcmp(&output, &output_before, sizeof(output)) == 0);
    }

    CHECK(fixture_init(&fixture, 15, 2) == 0);
    CHECK(stage_complete(
              &fixture, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1, packet,
              &packet_bytes, &completed) == 0);
    CHECK(Worr_NativeCarrierSessionCommitRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              completed.slot_index, completed.message_sequence,
              &fixture.ledger) == WORR_NATIVE_RX_COMMITTED);
    CHECK(fixture.session.history_count == 1 &&
          fixture.ledger.receipt_count == 1 &&
          find_receipt(&fixture.ledger, 1) != NULL &&
          Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    return 0;
}

static int test_ack_only_retry_and_combined_repeat(void)
{
    test_fixture fixture;
    uint8_t data_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t data_packet_bytes;
    size_t ack_packet_bytes;
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_carrier_ack_receipt_v1 *receipt;
    worr_native_rx_session_v1 session_before;
    worr_native_rx_slot_v1 slots_before[TEST_RX_CAPACITY];
    worr_native_carrier_ack_ledger_v1 ledger_before;
    uint8_t data_packet_before[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    worr_native_rx_message_v1 message;
    worr_native_rx_message_v1 message_canary;
    worr_native_rx_result_v1 rx_result = WORR_NATIVE_RX_INVALID_STATE;
    uint64_t tick;

    CHECK(fixture_init(&fixture, 21, 3) == 0);
    CHECK(accept_complete(
              &fixture, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1,
              data_packet, &data_packet_bytes) == 0);
    CHECK(fixture.ledger.receipt_count == 1 &&
          fixture.ledger.telemetry.commit_captures == 1);
    receipt = find_receipt(&fixture.ledger, 1);
    CHECK(receipt != NULL && receipt->handoffs_remaining == 3 &&
          (receipt->state_flags &
           WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE) != 0);
    ledger_before = fixture.ledger;
    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &fixture.ledger, 1, 5) == WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(memcmp(&fixture.ledger, &ledger_before,
                 sizeof(ledger_before)) == 0);

    for (tick = 10; tick <= 30; tick += 10) {
        CHECK(Worr_NativeCarrierAckPreparePacketV1(
                  &fixture.ledger, tick, 5, 48, NULL, 0, ack_packet,
                  sizeof(ack_packet), &ack_packet_bytes, &token) ==
              WORR_NATIVE_CARRIER_ACK_OK);
        CHECK(ack_packet_bytes == 48 && token.range_count == 1 &&
              (token.state_flags &
               WORR_NATIVE_CARRIER_ACK_TOKEN_PACKET_BOUND) != 0 &&
              token.connection_owner_id == TEST_OWNER_A &&
              token.ranges[0].first_message_sequence == 1 &&
              token.ranges[0].last_message_sequence == 1);
        ledger_before = fixture.ledger;
        CHECK(Worr_NativeCarrierAckPeekDueV1(
                  &fixture.ledger, tick, 5) ==
              WORR_NATIVE_CARRIER_ACK_EMIT_ACTIVE);
        CHECK(memcmp(&fixture.ledger, &ledger_before,
                     sizeof(ledger_before)) == 0);
        CHECK(Worr_NativeCarrierAckCommitHandoffV1(
                  &fixture.ledger, &token, tick, ack_packet,
                  ack_packet_bytes) == WORR_NATIVE_CARRIER_ACK_OK);
        CHECK(Worr_NativeCarrierAckPeekDueV1(
                  &fixture.ledger, tick, 5) ==
              WORR_NATIVE_CARRIER_ACK_NOT_DUE);
    }
    receipt = find_receipt(&fixture.ledger, 1);
    CHECK(receipt != NULL && receipt->handoffs_remaining == 0 &&
          receipt->handoff_attempts == 3 &&
          fixture.ledger.telemetry.proactive_bursts_completed == 1);
    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &fixture.ledger, 40, 5) ==
          WORR_NATIVE_CARRIER_ACK_NOT_DUE);

    memset(&token, 0xa5, sizeof(token));
    ack_packet_bytes = SIZE_MAX;
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 40, 5, 48, NULL, 0, ack_packet,
              sizeof(ack_packet), &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_NOT_DUE);
    CHECK(ack_packet_bytes == SIZE_MAX &&
          token.struct_size == UINT32_C(0xa5a5a5a5));

    memset(&message, 0xa5, sizeof(message));
    message_canary = message;
    session_before = fixture.session;
    memcpy(slots_before, fixture.slots, sizeof(slots_before));
    ledger_before = fixture.ledger;
    memcpy(data_packet_before, data_packet, data_packet_bytes);
    CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              data_packet, data_packet_bytes, 40, data_packet,
              data_packet_bytes, 0, &fixture.ledger, &rx_result,
              &message) == WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT);
    CHECK(rx_result == WORR_NATIVE_RX_INVALID_STATE &&
          memcmp(&message, &message_canary, sizeof(message)) == 0 &&
          memcmp(&fixture.session, &session_before,
                 sizeof(session_before)) == 0 &&
          memcmp(fixture.slots, slots_before, sizeof(slots_before)) == 0 &&
          memcmp(&fixture.ledger, &ledger_before,
                 sizeof(ledger_before)) == 0 &&
          memcmp(data_packet, data_packet_before,
                 data_packet_bytes) == 0);

    CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              fixture.arena, sizeof(fixture.arena), 40, data_packet,
              data_packet_bytes, 0, &fixture.ledger, &rx_result,
              &message) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(rx_result == WORR_NATIVE_RX_ALREADY_COMMITTED);
    CHECK(memcmp(&message, &message_canary, sizeof(message)) == 0);
    receipt = find_receipt(&fixture.ledger, 1);
    CHECK(receipt != NULL && receipt->handoffs_remaining == 3 &&
          (receipt->state_flags &
           WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE) != 0 &&
          fixture.ledger.telemetry.repeat_refreshes == 1);

    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 40, 1000, 48, NULL, 0, ack_packet,
              sizeof(ack_packet), &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &fixture.ledger, &token) == WORR_NATIVE_CARRIER_ACK_OK);
    return 0;
}

static int test_command_repeat_exact_only_semantic_isolation(void)
{
    test_fixture fixture;
    uint8_t command_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t semantic_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t command_packet_bytes;
    size_t semantic_packet_bytes;
    worr_native_rx_message_v1 completed;
    worr_native_rx_message_v1 output;
    worr_native_rx_message_v1 output_before;
    worr_native_carrier_ack_receipt_v1 descriptor_before;
    worr_native_carrier_ack_receipt_v1 *command_receipt;
    worr_native_carrier_ack_receipt_v1 *descriptor_receipt;
    worr_native_rx_result_v1 rx_result = WORR_NATIVE_RX_INVALID_STATE;
    uint64_t generation_before;
    uint64_t reconciled_before;
    uint64_t repeats_before;
    uint16_t receipt_count_before;
    uint16_t index;

    CHECK(fixture_init(&fixture, 22, 2) == 0);
    CHECK(accept_complete(
              &fixture, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1,
              command_packet, &command_packet_bytes) == 0);

    CHECK(stage_complete(
              &fixture, 2, WORR_NATIVE_RECORD_EVENT_V1, 2,
              semantic_packet, &semantic_packet_bytes, &completed) == 0);
    CHECK(commit_rx_only(&fixture, &completed) == 0);
    CHECK(find_receipt(&fixture.ledger, 2) == NULL);

    CHECK(stage_complete(
              &fixture, 3,
              WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1, 3,
              semantic_packet, &semantic_packet_bytes, &completed) == 0);
    CHECK(commit_rx_only(&fixture, &completed) == 0);

    for (index = 0;
         index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++index) {
        if ((fixture.ledger.receipts[index].state_flags &
             WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED) == 0) {
            break;
        }
    }
    CHECK(index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY);
    descriptor_receipt = &fixture.ledger.receipts[index];
    memset(descriptor_receipt, 0, sizeof(*descriptor_receipt));
    descriptor_receipt->message_sequence = 3;
    descriptor_receipt->handoff_attempts = 1;
    descriptor_receipt->record_class =
        WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1;
    descriptor_receipt->state_flags =
        WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED;
    ++fixture.ledger.receipt_count;

    command_receipt = find_receipt(&fixture.ledger, 1);
    CHECK(command_receipt != NULL);
    command_receipt->handoffs_remaining = 0;
    command_receipt->handoff_attempts = 1;
    command_receipt->state_flags =
        WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED;
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));

    descriptor_before = *descriptor_receipt;
    receipt_count_before = fixture.ledger.receipt_count;
    generation_before = fixture.ledger.mutation_generation;
    reconciled_before = fixture.ledger.telemetry.reconciled_receipts;
    repeats_before = fixture.ledger.telemetry.repeat_refreshes;
    memset(&output, 0xa5, sizeof(output));
    output_before = output;
    CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              fixture.arena, sizeof(fixture.arena), 4, command_packet,
              command_packet_bytes, 0, &fixture.ledger, &rx_result,
              &output) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(rx_result == WORR_NATIVE_RX_ALREADY_COMMITTED &&
          memcmp(&output, &output_before, sizeof(output)) == 0);

    command_receipt = find_receipt(&fixture.ledger, 1);
    descriptor_receipt = find_receipt(&fixture.ledger, 3);
    CHECK(command_receipt != NULL &&
          command_receipt->handoffs_remaining ==
              fixture.ledger.proactive_handoffs &&
          (command_receipt->state_flags &
           WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE) != 0);
    CHECK(find_receipt(&fixture.ledger, 2) == NULL &&
          descriptor_receipt != NULL &&
          memcmp(descriptor_receipt, &descriptor_before,
                 sizeof(descriptor_before)) == 0);
    CHECK(fixture.ledger.receipt_count == receipt_count_before &&
          fixture.ledger.telemetry.reconciled_receipts ==
              reconciled_before &&
          fixture.ledger.telemetry.repeat_refreshes ==
              repeats_before + 1u &&
          fixture.ledger.mutation_generation == generation_before + 1u &&
          Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    return 0;
}

static int test_range_budgets_no_gap_and_fairness(void)
{
    static const uint32_t sequences[] = {
        1, 2, 4, 6, 8, 10, 12, 14, 16, 18};
    test_fixture fixture;
    uint8_t receipt_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t datagram[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint8_t piggyback[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t ack_only[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t receipt_packet_bytes;
    size_t datagram_bytes;
    size_t piggyback_bytes;
    size_t ack_only_bytes;
    worr_native_ack_range_v1 ranges[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_carrier_view_v1 view;
    uint16_t count;
    uint16_t index;

    CHECK(fixture_init(&fixture, 31, 3) == 0);
    for (index = 0; index < ARRAY_COUNT(sequences); ++index) {
        CHECK(accept_complete(
                  &fixture, sequences[index], WORR_NATIVE_RECORD_COMMAND_V1,
                  (uint64_t)index + 1u, receipt_packet,
                  &receipt_packet_bytes) == 0);
    }

    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &fixture.ledger, 100, 1000, 7, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &count, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(count == 7 && ranges[0].first_message_sequence == 1 &&
          ranges[0].last_message_sequence == 2 &&
          ranges[1].first_message_sequence == 4 &&
          ranges[6].first_message_sequence == 14);
    for (index = 0; index < count; ++index) {
        CHECK(Worr_NativeAckRangeValidateV1(&ranges[index]) &&
              ranges[index].transport_epoch ==
                  fixture.binding.transport_epoch &&
              ranges[index].connection_owner_id == TEST_OWNER_A);
    }
    CHECK(emit_datagram(
              fixture.binding.transport_epoch, 1000,
              WORR_NATIVE_RECORD_EVENT_V1, 0x55, datagram,
              &datagram_bytes) == 0);
    CHECK(encode_packet(
              fixture.binding.transport_epoch, NULL, 0, datagram,
              datagram_bytes, ranges, count, piggyback,
              sizeof(piggyback), &piggyback_bytes) == 0);
    CHECK(Worr_NativeCarrierDecodeV1(
              piggyback, piggyback_bytes, &view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(view.entry_count == WORR_NATIVE_CARRIER_MAX_ENTRIES &&
          view.entries[0].entry_type == WORR_NATIVE_CARRIER_ENTRY_DATA_V1);
    CHECK(Worr_NativeCarrierAckBindPacketV1(
              &fixture.ledger, &token, piggyback,
              piggyback_bytes) == WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &fixture.ledger, &token) == WORR_NATIVE_CARRIER_ACK_OK);

    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 100, 1000, 160, NULL, 0, ack_only,
              sizeof(ack_only), &ack_only_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(ack_only_bytes == 160 && token.range_count == 8);
    CHECK(token.ranges[0].first_message_sequence == 1 &&
          token.ranges[0].last_message_sequence == 2 &&
          token.ranges[1].first_message_sequence == 4 &&
          token.ranges[7].first_message_sequence == 16);
    CHECK(Worr_NativeCarrierDecodeV1(
              ack_only, ack_only_bytes, &view) == WORR_NATIVE_CARRIER_OK);
    CHECK(view.entry_count == WORR_NATIVE_CARRIER_MAX_ENTRIES);
    for (index = 0; index < view.entry_count; ++index) {
        CHECK(view.entries[index].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
    }
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &fixture.ledger, &token, 100, ack_only,
              ack_only_bytes) == WORR_NATIVE_CARRIER_ACK_OK);

    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &fixture.ledger, 101, 1000, 8, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &count, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(count == 1 && ranges[0].first_message_sequence == 18 &&
          ranges[0].last_message_sequence == 18);
    CHECK(Worr_NativeCarrierAckAbortV1(
              &fixture.ledger, &token) == WORR_NATIVE_CARRIER_ACK_OK);
    return 0;
}

static int test_bind_exact_packet_and_replay(void)
{
    static const uint8_t legacy[] = {'b', 'a', 's', 'e'};
    static const uint8_t changed_legacy[] = {'c', 'a', 's', 'e'};
    test_fixture fixture;
    worr_native_carrier_ack_ledger_v1 bound_ledger;
    worr_native_carrier_ack_ledger_v1 committed_ledger;
    uint8_t receipt_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t datagram[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint8_t changed_datagram[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t changed_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t receipt_packet_bytes;
    size_t datagram_bytes;
    size_t changed_datagram_bytes;
    size_t packet_bytes;
    size_t changed_packet_bytes;
    worr_native_ack_range_v1 ranges[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
    worr_native_carrier_ack_emit_token_v1 token;
    uint16_t count;

    CHECK(fixture_init(&fixture, 41, 2) == 0);
    CHECK(accept_complete(
              &fixture, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1,
              receipt_packet, &receipt_packet_bytes) == 0);
    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &fixture.ledger, 10, 5, 7, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &count, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(count == 1);
    CHECK(emit_datagram(
              41, 50, WORR_NATIVE_RECORD_EVENT_V1, 0x20, datagram,
              &datagram_bytes) == 0);
    CHECK(encode_packet(
              41, legacy, sizeof(legacy), datagram, datagram_bytes, ranges,
              count, packet, sizeof(packet), &packet_bytes) == 0);
    CHECK(Worr_NativeCarrierAckBindPacketV1(
              &fixture.ledger, &token, packet, packet_bytes) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    bound_ledger = fixture.ledger;

    CHECK(encode_packet(
              41, changed_legacy, sizeof(changed_legacy), datagram,
              datagram_bytes, ranges, count, changed_packet,
              sizeof(changed_packet), &changed_packet_bytes) == 0);
    CHECK(changed_packet_bytes == packet_bytes);
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &fixture.ledger, &token, 10, changed_packet,
              changed_packet_bytes) == WORR_NATIVE_CARRIER_ACK_PACKET_MISMATCH);
    CHECK(memcmp(&fixture.ledger, &bound_ledger, sizeof(bound_ledger)) == 0);

    CHECK(emit_datagram(
              41, 51, WORR_NATIVE_RECORD_EVENT_V1, 0x21,
              changed_datagram, &changed_datagram_bytes) == 0);
    CHECK(encode_packet(
              41, legacy, sizeof(legacy), changed_datagram,
              changed_datagram_bytes, ranges, count, changed_packet,
              sizeof(changed_packet), &changed_packet_bytes) == 0);
    CHECK(changed_packet_bytes == packet_bytes);
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &fixture.ledger, &token, 10, changed_packet,
              changed_packet_bytes) == WORR_NATIVE_CARRIER_ACK_PACKET_MISMATCH);
    CHECK(memcmp(&fixture.ledger, &bound_ledger, sizeof(bound_ledger)) == 0);

    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &fixture.ledger, &token, 10, packet,
              packet_bytes) == WORR_NATIVE_CARRIER_ACK_OK);
    committed_ledger = fixture.ledger;
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &fixture.ledger, &token, 10, packet,
              packet_bytes) == WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION);
    CHECK(memcmp(
              &fixture.ledger, &committed_ledger,
              sizeof(committed_ledger)) == 0);
    return 0;
}

static int test_emit_lifecycle_and_mutation_serialization(void)
{
    test_fixture fixture;
    worr_native_carrier_ack_ledger_v1 before;
    worr_native_rx_session_v1 session_before;
    worr_native_rx_slot_v1 slots_before[TEST_RX_CAPACITY];
    uint8_t arena_before[TEST_RX_CAPACITY * TEST_PAYLOAD_STRIDE];
    uint8_t packet1[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t packet2[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet1_bytes;
    size_t packet2_bytes;
    size_t ack_packet_bytes;
    size_t output_bytes;
    worr_native_rx_message_v1 message2;
    worr_native_rx_message_v1 output_message;
    worr_native_rx_message_v1 output_message_canary;
    worr_native_rx_result_v1 rx_result = WORR_NATIVE_RX_INVALID_STATE;
    worr_native_ack_range_v1 ranges[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
    worr_native_ack_range_v1 ranges_canary[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_carrier_ack_emit_token_v1 token_copy;
    worr_native_carrier_ack_emit_token_v1 output_token;
    worr_native_carrier_ack_emit_token_v1 output_token_canary;
    uint16_t count;
    uint16_t output_count;

    CHECK(fixture_init(&fixture, 51, 2) == 0);
    CHECK(accept_complete(
              &fixture, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1, packet1,
              &packet1_bytes) == 0);
    CHECK(stage_complete(
              &fixture, 2, WORR_NATIVE_RECORD_COMMAND_V1, 2, packet2,
              &packet2_bytes, &message2) == 0);

    before = fixture.ledger;
    memset(ack_packet, 0xa5, sizeof(ack_packet));
    memset(&output_token, 0xa5, sizeof(output_token));
    output_token_canary = output_token;
    output_bytes = SIZE_MAX;
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 10, 5, 47, NULL, 0, ack_packet,
              sizeof(ack_packet), &output_bytes, &output_token) ==
          WORR_NATIVE_CARRIER_ACK_LIMIT);
    CHECK(memcmp(&fixture.ledger, &before, sizeof(before)) == 0 &&
          memcmp(&output_token, &output_token_canary,
                 sizeof(output_token)) == 0 &&
          output_bytes == SIZE_MAX);
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 10, 5, 48, NULL, 0, ack_packet, 47,
              &output_bytes, &output_token) ==
          WORR_NATIVE_CARRIER_ACK_OUTPUT_TOO_SMALL);
    CHECK(memcmp(&fixture.ledger, &before, sizeof(before)) == 0 &&
          memcmp(&output_token, &output_token_canary,
                 sizeof(output_token)) == 0 &&
          output_bytes == SIZE_MAX);

    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &fixture.ledger, 10, 5, 7, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &count, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    token_copy = token;
    before = fixture.ledger;
    memset(ranges_canary, 0xa5, sizeof(ranges_canary));
    memcpy(ranges, ranges_canary, sizeof(ranges));
    memset(&output_token, 0xa5, sizeof(output_token));
    output_token_canary = output_token;
    output_count = UINT16_C(0xa5a5);
    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &fixture.ledger, 10, 5, 7, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &output_count,
              &output_token) == WORR_NATIVE_CARRIER_ACK_EMIT_ACTIVE);
    CHECK(memcmp(&fixture.ledger, &before, sizeof(before)) == 0 &&
          memcmp(ranges, ranges_canary, sizeof(ranges)) == 0 &&
          memcmp(&output_token, &output_token_canary,
                 sizeof(output_token)) == 0 &&
          output_count == UINT16_C(0xa5a5));

    session_before = fixture.session;
    memcpy(slots_before, fixture.slots, sizeof(slots_before));
    memcpy(arena_before, fixture.arena, sizeof(arena_before));
    CHECK(Worr_NativeCarrierSessionCommitRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              message2.slot_index, 2, &fixture.ledger) ==
          WORR_NATIVE_RX_INVALID_STATE);
    CHECK(memcmp(&fixture.session, &session_before,
                 sizeof(session_before)) == 0 &&
          memcmp(fixture.slots, slots_before, sizeof(slots_before)) == 0 &&
          memcmp(fixture.arena, arena_before, sizeof(arena_before)) == 0 &&
          memcmp(&fixture.ledger, &before, sizeof(before)) == 0);

    memset(&output_message, 0xa5, sizeof(output_message));
    output_message_canary = output_message;
    CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              fixture.arena, sizeof(fixture.arena), 10, packet1,
              packet1_bytes, 0, &fixture.ledger, &rx_result,
              &output_message) == WORR_NATIVE_CARRIER_SESSION_INVALID_STATE);
    CHECK(rx_result == WORR_NATIVE_RX_INVALID_STATE &&
          memcmp(&output_message, &output_message_canary,
                 sizeof(output_message)) == 0 &&
          memcmp(&fixture.session, &session_before,
                 sizeof(session_before)) == 0 &&
          memcmp(fixture.slots, slots_before, sizeof(slots_before)) == 0 &&
          memcmp(fixture.arena, arena_before, sizeof(arena_before)) == 0 &&
          memcmp(&fixture.ledger, &before, sizeof(before)) == 0);

    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &fixture.ledger, &token, 10, packet1,
              packet1_bytes) == WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION);
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &fixture.ledger, &token) ==
          WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION);
    CHECK(Worr_NativeCarrierAckAbortV1(
              &fixture.ledger, &token) == WORR_NATIVE_CARRIER_ACK_OK);
    before = fixture.ledger;
    CHECK(Worr_NativeCarrierAckAbortV1(
              &fixture.ledger, &token_copy) ==
          WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION);
    CHECK(memcmp(&fixture.ledger, &before, sizeof(before)) == 0);

    CHECK(Worr_NativeCarrierSessionCommitRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              message2.slot_index, 2, &fixture.ledger) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 20, 5, 64, NULL, 0, ack_packet,
              sizeof(ack_packet), &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    before = fixture.ledger;
    CHECK(Worr_NativeCarrierAckAbortV1(
              &fixture.ledger, &token) ==
          WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION);
    CHECK(memcmp(&fixture.ledger, &before, sizeof(before)) == 0);
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &fixture.ledger, &token) == WORR_NATIVE_CARRIER_ACK_OK);
    before = fixture.ledger;
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &fixture.ledger, &token) ==
          WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION);
    CHECK(memcmp(&fixture.ledger, &before, sizeof(before)) == 0);
    return 0;
}

static int test_same_epoch_owner_and_cross_ledger_rejection(void)
{
    test_fixture first;
    test_fixture second;
    worr_native_carrier_ack_ledger_v1 ledger_before;
    worr_native_rx_session_v1 session_before;
    worr_native_rx_slot_v1 slots_before[TEST_RX_CAPACITY];
    uint8_t packet_first[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t packet_second[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_first_bytes;
    size_t packet_second_bytes;
    worr_native_rx_message_v1 message;
    worr_native_carrier_ack_emit_token_v1 token_first;
    worr_native_carrier_ack_emit_token_v1 token_second;

    CHECK(fixture_init_owner(&first, 61, TEST_OWNER_A, 2) == 0);
    CHECK(fixture_init_owner(&second, 61, TEST_OWNER_B, 2) == 0);
    CHECK(stage_complete(
              &first, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1, packet_first,
              &packet_first_bytes, &message) == 0);
    session_before = first.session;
    memcpy(slots_before, first.slots, sizeof(slots_before));
    ledger_before = second.ledger;
    CHECK(Worr_NativeCarrierSessionCommitRetainedV1(
              &first.session, first.slots, TEST_RX_CAPACITY,
              message.slot_index, 1, &second.ledger) ==
          WORR_NATIVE_RX_INVALID_STATE);
    CHECK(memcmp(&first.session, &session_before,
                 sizeof(session_before)) == 0 &&
          memcmp(first.slots, slots_before, sizeof(slots_before)) == 0 &&
          memcmp(&second.ledger, &ledger_before,
                 sizeof(ledger_before)) == 0);
    CHECK(Worr_NativeCarrierSessionCommitRetainedV1(
              &first.session, first.slots, TEST_RX_CAPACITY,
              message.slot_index, 1, &first.ledger) ==
          WORR_NATIVE_RX_COMMITTED);
    CHECK(accept_complete(
              &second, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1,
              packet_second, &packet_second_bytes) == 0);

    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &first.ledger, 10, 5, 48, NULL, 0, packet_first,
              sizeof(packet_first), &packet_first_bytes, &token_first) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &second.ledger, 10, 5, 48, NULL, 0, packet_second,
              sizeof(packet_second), &packet_second_bytes, &token_second) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    ledger_before = second.ledger;
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &second.ledger, &token_first, 10, packet_first,
              packet_first_bytes) == WORR_NATIVE_CARRIER_ACK_INVALID_STATE);
    CHECK(memcmp(&second.ledger, &ledger_before,
                 sizeof(ledger_before)) == 0);
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &second.ledger, &token_first) ==
          WORR_NATIVE_CARRIER_ACK_INVALID_STATE);
    CHECK(memcmp(&second.ledger, &ledger_before,
                 sizeof(ledger_before)) == 0);
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &second.ledger, &token_second) == WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &first.ledger, &token_first) == WORR_NATIVE_CARRIER_ACK_OK);
    return 0;
}

/*
 * Cross-component one-way-loss property: a reliable TX record stalled at the
 * 64-sequence receipt window remains provable by RX.  Exhausting every
 * proactive ACK does not lose authority; an exact DATA retry produces
 * ALREADY_COMMITTED, rearms the ledger, and reverse-path recovery releases
 * the sender so sequence allocation can continue.
 */
static int test_one_way_loss_window_stall_and_recovery(void)
{
    enum { TX_CAPACITY = 2 };
    test_fixture receiver;
    worr_native_tx_session_v1 tx;
    worr_native_tx_slot_v1 tx_slots[TX_CAPACITY];
    worr_native_tx_slot_v1 selected;
    uint8_t data_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t data_packet_bytes;
    size_t ack_packet_bytes;
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_carrier_ack_receipt_v1 *receipt;
    worr_native_rx_message_v1 message;
    uint32_t sequence = 0;
    uint32_t acknowledged = 0;
    uint32_t object_sequence;
    uint64_t tick;

    CHECK(fixture_init(&receiver, 66, 3) == 0);
    CHECK(Worr_NativeTxSessionInitV1(
        &tx, tx_slots, TX_CAPACITY, &receiver.binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, tx_slots, TX_CAPACITY,
              test_record(WORR_NATIVE_RECORD_COMMAND_V1, 1), 1, 100,
              4, WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES, 1,
              &sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(sequence == 1);
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &tx, tx_slots, TX_CAPACITY, 1, 10, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(selected.message_sequence == 1 && selected.send_attempts == 1);
    CHECK(accept_complete(
              &receiver, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1,
              data_packet, &data_packet_bytes) == 0);

    /* Three packets reached transport but are lost before the sender. */
    for (tick = 2; tick <= 4; ++tick) {
        CHECK(Worr_NativeCarrierAckPreparePacketV1(
                  &receiver.ledger, tick, 0, 48, NULL, 0, ack_packet,
                  sizeof(ack_packet), &ack_packet_bytes, &token) ==
              WORR_NATIVE_CARRIER_ACK_OK);
        CHECK(Worr_NativeCarrierAckCommitHandoffV1(
                  &receiver.ledger, &token, tick, ack_packet,
                  ack_packet_bytes) == WORR_NATIVE_CARRIER_ACK_OK);
    }
    receipt = find_receipt(&receiver.ledger, 1);
    CHECK(receipt != NULL && receipt->handoffs_remaining == 0 &&
          receipt->handoff_attempts == 3);

    for (object_sequence = 1; object_sequence <= 63;
         ++object_sequence) {
        const worr_native_tx_result_v1 expected =
            object_sequence == 1 ? WORR_NATIVE_TX_RETAINED
                                 : WORR_NATIVE_TX_SUPERSEDED;

        CHECK(Worr_NativeTxSessionEnqueueV1(
                  &tx, tx_slots, TX_CAPACITY,
                  test_record(WORR_NATIVE_RECORD_SNAPSHOT_V1,
                              object_sequence),
                  7, 1000 + object_sequence, 4,
                  WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES,
                  9u + object_sequence, &sequence) == expected);
        CHECK(sequence == object_sequence + 1u);
    }
    sequence = UINT32_C(0xa5a5a5a5);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, tx_slots, TX_CAPACITY,
              test_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 64), 7,
              1064, 4, WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES, 74,
              &sequence) == WORR_NATIVE_TX_RECEIPT_WINDOW);
    CHECK(sequence == UINT32_C(0xa5a5a5a5) &&
          tx.next_message_sequence == 65 &&
          tx.telemetry.receipt_window_stalls == 1 &&
          receiver.session.history_count == 1 &&
          find_receipt(&receiver.ledger, 1) != NULL);

    CHECK(Worr_NativeTxSessionSelectDueV1(
              &tx, tx_slots, TX_CAPACITY, 100, 10, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(selected.message_sequence == 1 && selected.send_attempts == 2);
    memset(&message, 0xa5, sizeof(message));
    CHECK(accept_data(
              &receiver, data_packet, data_packet_bytes, 100,
              WORR_NATIVE_RX_ALREADY_COMMITTED, &message) == 0);
    receipt = find_receipt(&receiver.ledger, 1);
    CHECK(receipt != NULL && receipt->handoffs_remaining == 3 &&
          (receipt->state_flags &
           WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE) != 0);

    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &receiver.ledger, 101, 1000, 48, NULL, 0, ack_packet,
              sizeof(ack_packet), &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierSessionApplyAcksV1(
              &tx, tx_slots, TX_CAPACITY, ack_packet, ack_packet_bytes,
              &acknowledged) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(acknowledged == 1 && tx.retained_count == 1);
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &receiver.ledger, &token, 101, ack_packet,
              ack_packet_bytes) == WORR_NATIVE_CARRIER_ACK_OK);

    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, tx_slots, TX_CAPACITY,
              test_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 64), 7,
              1064, 4, WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES, 102,
              &sequence) == WORR_NATIVE_TX_SUPERSEDED);
    CHECK(sequence == 65 && tx.next_message_sequence == 66);
    return 0;
}

static int test_clock_saturation_and_token_boundary(void)
{
    test_fixture fixture;
    test_fixture exhausted;
    worr_native_carrier_ack_ledger_v1 before;
    uint8_t data_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t data_packet_bytes;
    size_t ack_packet_bytes;
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_carrier_ack_receipt_v1 *receipt;
    worr_native_ack_range_v1 ranges[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
    uint16_t count;

    CHECK(fixture_init(&fixture, 71, 2) == 0);
    CHECK(accept_complete(
              &fixture, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1, data_packet,
              &data_packet_bytes) == 0);
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 100, 10, 48, NULL, 0, ack_packet,
              sizeof(ack_packet), &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &fixture.ledger, &token, 100, ack_packet,
              ack_packet_bytes) == WORR_NATIVE_CARRIER_ACK_OK);
    before = fixture.ledger;
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 99, 10, 48, NULL, 0, ack_packet,
              sizeof(ack_packet), &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_CLOCK_REGRESSION);
    CHECK(memcmp(&fixture.ledger, &before, sizeof(before)) == 0);
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 109, 10, 48, NULL, 0, ack_packet,
              sizeof(ack_packet), &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_NOT_DUE);
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 110, 10, 48, NULL, 0, ack_packet,
              sizeof(ack_packet), &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    before = fixture.ledger;
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &fixture.ledger, &token, 109, ack_packet,
              ack_packet_bytes) == WORR_NATIVE_CARRIER_ACK_CLOCK_REGRESSION);
    CHECK(memcmp(&fixture.ledger, &before, sizeof(before)) == 0);
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &fixture.ledger, &token, 110, ack_packet,
              ack_packet_bytes) == WORR_NATIVE_CARRIER_ACK_OK);

    CHECK(accept_data(
              &fixture, data_packet, data_packet_bytes, 111,
              WORR_NATIVE_RX_ALREADY_COMMITTED,
              &(worr_native_rx_message_v1){0}) == 0);
    receipt = find_receipt(&fixture.ledger, 1);
    CHECK(receipt != NULL);
    receipt->handoff_attempts = UINT32_MAX;
    fixture.ledger.telemetry.handoff_commits = UINT64_MAX;
    fixture.ledger.telemetry.ranges_handed_off = UINT64_MAX;
    fixture.ledger.telemetry.receipts_handed_off = UINT64_MAX;
    fixture.ledger.telemetry.proactive_bursts_completed = UINT64_MAX;
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, UINT64_MAX, 0, 48, NULL, 0, ack_packet,
              sizeof(ack_packet), &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &fixture.ledger, &token, UINT64_MAX, ack_packet,
              ack_packet_bytes) == WORR_NATIVE_CARRIER_ACK_OK);
    receipt = find_receipt(&fixture.ledger, 1);
    CHECK(receipt != NULL && receipt->handoff_attempts == UINT32_MAX &&
          fixture.ledger.telemetry.handoff_commits == UINT64_MAX &&
          fixture.ledger.telemetry.ranges_handed_off == UINT64_MAX &&
          fixture.ledger.telemetry.receipts_handed_off == UINT64_MAX &&
          fixture.ledger.telemetry.proactive_bursts_completed == UINT64_MAX);

    CHECK(fixture_init(&exhausted, 72, 1) == 0);
    CHECK(accept_complete(
              &exhausted, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1,
              data_packet, &data_packet_bytes) == 0);
    exhausted.ledger.next_token_id = UINT64_MAX - 1u;
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&exhausted.ledger));
    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &exhausted.ledger, 2, 1, 1, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &count, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(token.token_id == UINT64_MAX - 1u &&
          exhausted.ledger.next_token_id == UINT64_MAX &&
          (exhausted.ledger.state_flags &
           WORR_NATIVE_CARRIER_ACK_LEDGER_TOKEN_EXHAUSTED) == 0 &&
          Worr_NativeCarrierAckLedgerValidateV1(&exhausted.ledger));
    CHECK(Worr_NativeCarrierAckAbortV1(
              &exhausted.ledger, &token) == WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &exhausted.ledger, 2, 1, 1, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &count, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(token.token_id == UINT64_MAX &&
          exhausted.ledger.next_token_id == UINT64_MAX &&
          (exhausted.ledger.state_flags &
           WORR_NATIVE_CARRIER_ACK_LEDGER_TOKEN_EXHAUSTED) != 0 &&
          Worr_NativeCarrierAckLedgerValidateV1(&exhausted.ledger));
    CHECK(Worr_NativeCarrierAckAbortV1(
              &exhausted.ledger, &token) == WORR_NATIVE_CARRIER_ACK_OK);
    before = exhausted.ledger;
    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &exhausted.ledger, 2, 1, 1, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &count, &token) ==
          WORR_NATIVE_CARRIER_ACK_TOKEN_EXHAUSTED);
    CHECK(memcmp(&exhausted.ledger, &before, sizeof(before)) == 0);
    return 0;
}

static int test_uint32_max_and_receipt_retirement(void)
{
    static const uint8_t legacy[] = {'f', 'i', 'n', 'a', 'l'};
    test_fixture maximum;
    test_fixture window;
    uint8_t data_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t data_packet_bytes;
    size_t ack_packet_bytes;
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_carrier_view_v1 view;
    uint32_t sequence;

    CHECK(fixture_init(&maximum, 81, 1) == 0);
    CHECK(accept_complete(
              &maximum, UINT32_MAX, WORR_NATIVE_RECORD_COMMAND_V1, 1,
              data_packet, &data_packet_bytes) == 0);
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &maximum.ledger, 2, 1, 53, legacy, sizeof(legacy),
              ack_packet, sizeof(ack_packet), &ack_packet_bytes,
              &token) == WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(ack_packet_bytes == 53 && token.range_count == 1 &&
          token.ranges[0].first_message_sequence == UINT32_MAX &&
          token.ranges[0].last_message_sequence == UINT32_MAX);
    CHECK(Worr_NativeCarrierDecodeV1(
              ack_packet, ack_packet_bytes, &view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(view.legacy_bytes == sizeof(legacy) &&
          memcmp(ack_packet, legacy, sizeof(legacy)) == 0);
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(
              &maximum.ledger, &token, 2, ack_packet,
              ack_packet_bytes) == WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&maximum.ledger));

    CHECK(fixture_init(&window, 82, 2) == 0);
    for (sequence = 1; sequence <= 65; ++sequence) {
        CHECK(accept_complete(
                  &window, sequence, WORR_NATIVE_RECORD_COMMAND_V1,
                  sequence, data_packet, &data_packet_bytes) == 0);
    }
    CHECK(window.session.history_count ==
          WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY);
    CHECK(window.ledger.receipt_count ==
          WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY);
    CHECK(find_receipt(&window.ledger, 1) == NULL &&
          find_receipt(&window.ledger, 65) != NULL &&
          window.ledger.telemetry.receipts_retired == 1);
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&window.ledger));
    return 0;
}

static int test_explicit_counted_cancellation(void)
{
    test_fixture fixture;
    test_fixture active;
    worr_native_carrier_ack_ledger_v1 before;
    worr_native_carrier_ack_ledger_v1 expected;
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_session_binding_v1 next_binding;
    uint8_t data_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t data_packet_bytes;
    size_t ack_packet_bytes;
    uint32_t cancelled;

    CHECK(fixture_init(&fixture, 91, 3) == 0);
    before = fixture.ledger;
    expected = fixture.ledger;
    expected.state_flags |=
        WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED;
    ++expected.mutation_generation;
    cancelled = UINT32_MAX;
    CHECK(Worr_NativeCarrierAckCancelAllV1(
              &fixture.ledger, &cancelled) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(cancelled == 0 &&
          memcmp(&fixture.ledger, &expected, sizeof(expected)) == 0);
    before = fixture.ledger;
    CHECK(Worr_NativeCarrierAckCancelAllV1(
              &fixture.ledger, &cancelled) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(cancelled == 0 &&
          memcmp(&fixture.ledger, &before, sizeof(before)) == 0);
    CHECK(fixture_init(&fixture, 91, 3) == 0);

    CHECK(accept_complete(
              &fixture, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1,
              data_packet, &data_packet_bytes) == 0);
    CHECK(accept_complete(
              &fixture, 3, WORR_NATIVE_RECORD_COMMAND_V1, 2,
              data_packet, &data_packet_bytes) == 0);
    CHECK(fixture.ledger.receipt_count == 2);

    before = fixture.ledger;
    cancelled = UINT32_C(0xa5a5a5a5);
    CHECK(Worr_NativeCarrierAckCancelAllV1(
              &fixture.ledger,
              (uint32_t *)&fixture.ledger.receipts[0]) ==
          WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT);
    CHECK(memcmp(&fixture.ledger, &before, sizeof(before)) == 0 &&
          cancelled == UINT32_C(0xa5a5a5a5));

    expected = fixture.ledger;
    memset(expected.receipts, 0, sizeof(expected.receipts));
    expected.receipt_count = 0;
    expected.state_flags |=
        WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED;
    ++expected.mutation_generation;
    CHECK(Worr_NativeCarrierAckCancelAllV1(
              &fixture.ledger, &cancelled) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(cancelled == 2 &&
          memcmp(&fixture.ledger, &expected, sizeof(expected)) == 0 &&
          fixture.ledger.transport_epoch == 91 &&
          fixture.ledger.connection_owner_id == TEST_OWNER_A &&
          Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    before = fixture.ledger;
    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &fixture.ledger, 3, 100) ==
          WORR_NATIVE_CARRIER_ACK_INVALID_STATE);
    {
        worr_native_rx_result_v1 rx_result = WORR_NATIVE_RX_COMMITTED;
        worr_native_rx_message_v1 message;

        memset(&message, 0xa5, sizeof(message));
        CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
                  &fixture.session, fixture.slots, TEST_RX_CAPACITY,
                  fixture.arena, sizeof(fixture.arena), 3,
                  data_packet, data_packet_bytes, 0, &fixture.ledger,
                  &rx_result, &message) ==
              WORR_NATIVE_CARRIER_SESSION_INVALID_STATE);
        CHECK(rx_result == WORR_NATIVE_RX_COMMITTED &&
              message.struct_size == UINT32_C(0xa5a5a5a5));
    }
    CHECK(memcmp(&fixture.ledger, &before, sizeof(before)) == 0);
    before = fixture.ledger;
    cancelled = UINT32_MAX;
    CHECK(Worr_NativeCarrierAckCancelAllV1(
              &fixture.ledger, &cancelled) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(cancelled == 0 &&
          memcmp(&fixture.ledger, &before, sizeof(before)) == 0);
    next_binding = test_binding(93, TEST_OWNER_A);
    CHECK(Worr_NativeCarrierAckLedgerAdvanceEpochV1(
        &fixture.ledger, &next_binding));
    CHECK(fixture.ledger.transport_epoch == 93 &&
          fixture.ledger.receipt_count == 0 &&
          (fixture.ledger.state_flags &
           WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED) == 0 &&
          Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));

    /* A packet-bound token has an unknown transport outcome.  Cancellation
     * cannot guess that outcome or revoke its exact receipt. */
    CHECK(fixture_init(&active, 92, 3) == 0);
    CHECK(accept_complete(
              &active, 1, WORR_NATIVE_RECORD_COMMAND_V1, 1,
              data_packet, &data_packet_bytes) == 0);
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &active.ledger, 2, 100, 48, NULL, 0,
              ack_packet, sizeof(ack_packet), &ack_packet_bytes,
              &token) == WORR_NATIVE_CARRIER_ACK_OK);
    before = active.ledger;
    cancelled = UINT32_C(0x5a5a5a5a);
    CHECK(Worr_NativeCarrierAckCancelAllV1(
              &active.ledger, &cancelled) ==
          WORR_NATIVE_CARRIER_ACK_EMIT_ACTIVE);
    CHECK(cancelled == UINT32_C(0x5a5a5a5a) &&
          memcmp(&active.ledger, &before, sizeof(before)) == 0);
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &active.ledger, &token) == WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierAckCancelAllV1(
              &active.ledger, &cancelled) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(cancelled == 1 && active.ledger.receipt_count == 0 &&
          Worr_NativeCarrierAckLedgerValidateV1(&active.ledger));
    return 0;
}

int main(void)
{
    if (test_init_layout_epoch_and_owner() != 0 ||
        test_semantic_commit_and_repeat_gates() != 0 ||
        test_ack_only_retry_and_combined_repeat() != 0 ||
        test_command_repeat_exact_only_semantic_isolation() != 0 ||
        test_range_budgets_no_gap_and_fairness() != 0 ||
        test_bind_exact_packet_and_replay() != 0 ||
        test_emit_lifecycle_and_mutation_serialization() != 0 ||
        test_same_epoch_owner_and_cross_ledger_rejection() != 0 ||
        test_one_way_loss_window_stall_and_recovery() != 0 ||
        test_clock_saturation_and_token_boundary() != 0 ||
        test_uint32_max_and_receipt_retirement() != 0 ||
        test_explicit_counted_cancellation() != 0) {
        return 1;
    }
    printf("native carrier ACK tests passed\n");
    return 0;
}
