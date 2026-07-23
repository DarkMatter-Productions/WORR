/* Deterministic transactional native event-admission tests (FR-10-T05). */

#include "common/net/native_event_admission.h"
#include "common/net/native_event_batch.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "native_event_admission_test:%d: %s\n",      \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

enum {
    TEST_RX_CAPACITY = 4,
    TEST_PAYLOAD_STRIDE = WORR_NATIVE_EVENT_BATCH_MAX_PAYLOAD_BYTES,
};

#define TEST_OWNER_ID UINT64_C(0x1020304050607080)
#define TEST_TRANSPORT_EPOCH UINT32_C(41)
#define TEST_STREAM_EPOCH UINT32_C(7)

typedef struct fake_consumer_s {
    worr_cgame_event_runtime_status_v1 status;
    worr_event_record_v1 last_record;
    worr_event_record_v1 last_batch[WORR_NATIVE_EVENT_BATCH_MAX_EVENTS];
    uint32_t last_batch_count;
    uint32_t first_sequence;
    uint32_t reset_calls;
    uint32_t scrub_calls;
    uint32_t submit_calls;
    uint32_t status_calls;
    uint32_t fail_status_call;
    worr_cgame_event_runtime_result_v1 forced_submit_result;
    bool fail_reset;
    bool force_submit_result;
    bool omit_receipt;
    bool report_degraded;
} fake_consumer;

typedef struct test_fixture_s {
    worr_native_session_binding_v1 binding;
    worr_event_stream_owner_v1 owner;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[TEST_RX_CAPACITY];
    uint8_t arena[TEST_RX_CAPACITY * TEST_PAYLOAD_STRIDE];
    worr_native_carrier_ack_ledger_v1 ledger;
    fake_consumer fake;
    worr_native_event_consumer_v1 consumer;
    uint32_t next_message_sequence;
    uint64_t next_tick;
} test_fixture;

typedef struct state_snapshot_s {
    worr_event_stream_owner_v1 owner;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[TEST_RX_CAPACITY];
    uint8_t arena[TEST_RX_CAPACITY * TEST_PAYLOAD_STRIDE];
    worr_native_carrier_ack_ledger_v1 ledger;
} state_snapshot;

static bool record_ref_equal(worr_native_record_ref_v1 left,
                             worr_native_record_ref_v1 right)
{
    return left.record_class == right.record_class &&
           left.reserved0 == right.reserved0 &&
           left.record_schema_version == right.record_schema_version &&
           left.object_epoch == right.object_epoch &&
           left.object_sequence == right.object_sequence;
}

static worr_cgame_event_runtime_result_v1 fake_reset(
    void *opaque, uint32_t stream_epoch, uint32_t first_sequence)
{
    fake_consumer *fake = (fake_consumer *)opaque;

    ++fake->reset_calls;
    if (stream_epoch == 0 && first_sequence == 0) {
        ++fake->scrub_calls;
        memset(&fake->status, 0, sizeof(fake->status));
        fake->first_sequence = 0;
        return WORR_CGAME_EVENT_RUNTIME_OK;
    }
    if (fake->fail_reset)
        return WORR_CGAME_EVENT_RUNTIME_CONFLICT;

    memset(&fake->status, 0, sizeof(fake->status));
    fake->status.struct_size = sizeof(fake->status);
    fake->status.api_version = WORR_CGAME_EVENT_RUNTIME_API_VERSION;
    fake->status.authority_epoch = stream_epoch;
    fake->status.next_presentation_sequence = first_sequence;
    fake->status.state_flags = WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE;
    fake->status.receipt.struct_size = sizeof(fake->status.receipt);
    fake->status.receipt.schema_version = WORR_EVENT_ABI_VERSION;
    fake->status.receipt.stream_epoch = stream_epoch;
    fake->status.receipt.highest_contiguous = first_sequence - 1u;
    fake->first_sequence = first_sequence;
    return WORR_CGAME_EVENT_RUNTIME_OK;
}

static worr_cgame_event_runtime_result_v1 fake_submit(
    void *opaque, const worr_event_record_v1 *records, uint32_t count)
{
    fake_consumer *fake = (fake_consumer *)opaque;
    worr_event_receipt_ack_v1 staged_receipt;
    worr_event_receipt_result_v1 receipt_result;
    uint32_t accepted = 0;
    uint32_t index;
    bool all_duplicate = true;

    ++fake->submit_calls;
    if (fake->force_submit_result)
        return fake->forced_submit_result;
    if (count == 0 || count > WORR_NATIVE_EVENT_BATCH_MAX_EVENTS ||
        records == NULL)
        return WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT;
    staged_receipt = fake->status.receipt;
    for (index = 0; index < count; ++index) {
        if (Worr_EventReceiptContainsV1(
                &staged_receipt, records[index].event_id))
            continue;
        all_duplicate = false;
        ++accepted;
        if (!fake->omit_receipt) {
            receipt_result = Worr_EventReceiptMarkV1(
                &staged_receipt, records[index].event_id);
            if (receipt_result != WORR_EVENT_RECEIPT_ACCEPTED)
                return WORR_CGAME_EVENT_RUNTIME_INVALID_RECORD;
        }
    }
    memcpy(fake->last_batch, records, count * sizeof(*records));
    fake->last_batch_count = count;
    fake->last_record = records[count - 1u];
    fake->status.authority_count += accepted;
    if (!fake->omit_receipt)
        fake->status.receipt = staged_receipt;
    if (all_duplicate)
        return WORR_CGAME_EVENT_RUNTIME_DUPLICATE;
    if (fake->report_degraded) {
        fake->status.state_flags |=
            WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED;
        return WORR_CGAME_EVENT_RUNTIME_DEGRADED;
    }
    return WORR_CGAME_EVENT_RUNTIME_OK;
}

static bool fake_get_status(
    void *opaque, worr_cgame_event_runtime_status_v1 *status_out)
{
    fake_consumer *fake = (fake_consumer *)opaque;

    ++fake->status_calls;
    if (fake->fail_status_call == fake->status_calls)
        return false;
    if (status_out == NULL)
        return false;
    *status_out = fake->status;
    return true;
}

static worr_native_session_binding_v1 make_binding(void)
{
    worr_native_session_binding_v1 binding;

    memset(&binding, 0, sizeof(binding));
    binding.struct_size = sizeof(binding);
    binding.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    binding.transport_epoch = TEST_TRANSPORT_EPOCH;
    binding.negotiated_capabilities =
        WORR_NET_CAP_NATIVE_ENVELOPE_V1 |
        WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |
        WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1;
    binding.connection_owner_id = TEST_OWNER_ID;
    return binding;
}

static int fixture_init(test_fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->binding = make_binding();
    CHECK(Worr_NativeSessionBindingValidateV1(&fixture->binding));
    CHECK(Worr_EventStreamOwnerInitV1(
        &fixture->owner, fixture->binding.connection_owner_id));
    CHECK(Worr_NativeRxSessionInitV1(
        &fixture->session, fixture->slots, TEST_RX_CAPACITY,
        TEST_PAYLOAD_STRIDE, 1000, 1000, &fixture->binding));
    CHECK(Worr_NativeCarrierAckLedgerInitV1(
        &fixture->ledger, &fixture->binding, 3));

    fixture->consumer.struct_size = sizeof(fixture->consumer);
    fixture->consumer.schema_version =
        WORR_NATIVE_EVENT_ADMISSION_ABI_VERSION;
    fixture->consumer.opaque = &fixture->fake;
    fixture->consumer.ResetAuthority = fake_reset;
    fixture->consumer.SubmitAuthoritativeBatch = fake_submit;
    fixture->consumer.GetStatus = fake_get_status;
    fixture->next_message_sequence = 1;
    fixture->next_tick = 1;
    return 0;
}

static void capture_state(const test_fixture *fixture,
                          state_snapshot *snapshot)
{
    snapshot->owner = fixture->owner;
    snapshot->session = fixture->session;
    memcpy(snapshot->slots, fixture->slots, sizeof(snapshot->slots));
    memcpy(snapshot->arena, fixture->arena, sizeof(snapshot->arena));
    snapshot->ledger = fixture->ledger;
}

static bool state_equal(const test_fixture *fixture,
                        const state_snapshot *snapshot)
{
    return memcmp(&fixture->owner, &snapshot->owner,
                  sizeof(snapshot->owner)) == 0 &&
           memcmp(&fixture->session, &snapshot->session,
                  sizeof(snapshot->session)) == 0 &&
           memcmp(fixture->slots, snapshot->slots,
                  sizeof(snapshot->slots)) == 0 &&
           memcmp(fixture->arena, snapshot->arena,
                  sizeof(snapshot->arena)) == 0 &&
           memcmp(&fixture->ledger, &snapshot->ledger,
                  sizeof(snapshot->ledger)) == 0;
}

static bool transport_equal(const test_fixture *fixture,
                            const state_snapshot *snapshot)
{
    return memcmp(&fixture->session, &snapshot->session,
                  sizeof(snapshot->session)) == 0 &&
           memcmp(fixture->slots, snapshot->slots,
                  sizeof(snapshot->slots)) == 0 &&
           memcmp(fixture->arena, snapshot->arena,
                  sizeof(snapshot->arena)) == 0 &&
           memcmp(&fixture->ledger, &snapshot->ledger,
                  sizeof(snapshot->ledger)) == 0;
}

static bool rx_storage_equal(const test_fixture *fixture,
                             const state_snapshot *snapshot)
{
    return memcmp(&fixture->session, &snapshot->session,
                  sizeof(snapshot->session)) == 0 &&
           memcmp(fixture->slots, snapshot->slots,
                  sizeof(snapshot->slots)) == 0 &&
           memcmp(fixture->arena, snapshot->arena,
                  sizeof(snapshot->arena)) == 0;
}

static int encode_data_datagram_packet(
    const test_fixture *fixture, const void *datagram,
    size_t datagram_bytes, uint8_t *packet_out,
    size_t *packet_bytes_out)
{
    worr_native_carrier_entry_v1 entry;

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entry.data_bytes = (uint32_t)datagram_bytes;
    CHECK(Worr_NativeCarrierEncodeV1(
              fixture->binding.transport_epoch, NULL, 0, datagram,
              datagram_bytes, &entry, 1, packet_out,
              WORR_NATIVE_CARRIER_MAX_PACKET_BYTES, packet_bytes_out) ==
          WORR_NATIVE_CARRIER_OK);
    return 0;
}

static int encode_one_data_packet(
    const test_fixture *fixture, uint32_t message_sequence,
    worr_native_record_ref_v1 record, const void *payload,
    uint32_t payload_bytes, uint8_t *packet_out,
    size_t *packet_bytes_out)
{
    worr_native_envelope_fragmenter_v1 fragmenter;
    uint8_t datagram[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t datagram_bytes;

    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, fixture->binding.transport_epoch, message_sequence,
        record, 1, payload, payload_bytes,
        WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES));
    CHECK(Worr_NativeEnvelopeFragmentNextV1(
              &fragmenter, payload, payload_bytes, datagram,
              sizeof(datagram), &datagram_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_OK);
    CHECK(fragmenter.next_fragment == fragmenter.fragment_count);
    return encode_data_datagram_packet(
        fixture, datagram, datagram_bytes, packet_out, packet_bytes_out);
}

static int stage_payload_with_ref(
    test_fixture *fixture, const void *payload, uint32_t payload_bytes,
    worr_native_record_ref_v1 envelope_ref,
    worr_native_rx_message_v1 *message_out)
{
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes;
    worr_native_rx_result_v1 rx_result = WORR_NATIVE_RX_INVALID_STATE;
    const uint32_t message_sequence = fixture->next_message_sequence++;

    CHECK(encode_one_data_packet(
              fixture, message_sequence, envelope_ref, payload,
              payload_bytes, packet, &packet_bytes) == 0);
    memset(message_out, 0xa5, sizeof(*message_out));
    CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
              &fixture->session, fixture->slots, TEST_RX_CAPACITY,
              fixture->arena, sizeof(fixture->arena), fixture->next_tick++,
              packet, packet_bytes, 0, &fixture->ledger, &rx_result,
              message_out) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(rx_result == WORR_NATIVE_RX_MESSAGE_COMPLETE);
    CHECK(message_out->message_sequence == message_sequence);
    CHECK(message_out->connection_owner_id == TEST_OWNER_ID);
    CHECK(record_ref_equal(message_out->record, envelope_ref));
    CHECK(message_out->payload_bytes == payload_bytes);
    CHECK(memcmp(fixture->arena + message_out->payload_offset,
                 payload, payload_bytes) == 0);
    return 0;
}

static int stage_descriptor_with_ref(
    test_fixture *fixture,
    const worr_event_stream_descriptor_v1 *descriptor,
    const worr_native_record_ref_v1 *override_ref,
    worr_native_rx_message_v1 *message_out)
{
    uint8_t encoded[128];
    size_t encoded_bytes;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 codec_ref;

    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              descriptor, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInspectV1(
              encoded, encoded_bytes, &info) == WORR_NATIVE_CODEC_OK);
    CHECK(info.record_class ==
          WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &codec_ref));
    CHECK(codec_ref.object_epoch == descriptor->stream_epoch);
    CHECK(codec_ref.object_sequence == descriptor->first_sequence);
    return stage_payload_with_ref(
        fixture, encoded, (uint32_t)encoded_bytes,
        override_ref == NULL ? codec_ref : *override_ref, message_out);
}

static worr_event_record_v1 make_event(uint32_t stream_epoch,
                                       uint32_t sequence)
{
    worr_event_record_v1 record;

    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
                   WORR_EVENT_FLAG_REPLAY_SAFE |
                   WORR_EVENT_FLAG_PRESENT_ONCE;
    record.event_id.stream_epoch = stream_epoch;
    record.event_id.sequence = sequence;
    record.source_tick = 1000u + sequence;
    record.source_ordinal = sequence;
    record.source_time_us = UINT64_C(9000000) + sequence;
    record.source_entity.index = 1;
    record.source_entity.generation = 4;
    record.subject_entity.index = WORR_EVENT_NO_ENTITY;
    record.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
    record.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    record.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record.payload_kind = WORR_EVENT_PAYLOAD_NONE;
    return record;
}

static int stage_event_with_ref(
    test_fixture *fixture, const worr_event_record_v1 *event,
    const worr_native_record_ref_v1 *override_ref,
    worr_native_rx_message_v1 *message_out)
{
    uint8_t encoded[WORR_NATIVE_CODEC_MAX_EVENT_ENCODED_BYTES];
    size_t encoded_bytes;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 codec_ref;

    CHECK(Worr_EventRecordValidateV1(
        event, WORR_EVENT_STREAM_MAX_ENTITIES_V1));
    CHECK(Worr_NativeCodecEventEncodeV1(
              event, WORR_EVENT_STREAM_MAX_ENTITIES_V1, encoded,
              sizeof(encoded), &encoded_bytes) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInspectV1(
              encoded, encoded_bytes, &info) == WORR_NATIVE_CODEC_OK);
    CHECK(info.record_class == WORR_NATIVE_RECORD_EVENT_V1);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &codec_ref));
    CHECK(codec_ref.object_epoch == event->event_id.stream_epoch);
    CHECK(codec_ref.object_sequence == event->event_id.sequence);
    return stage_payload_with_ref(
        fixture, encoded, (uint32_t)encoded_bytes,
        override_ref == NULL ? codec_ref : *override_ref, message_out);
}

static int stage_event_batch(
    test_fixture *fixture, const worr_event_record_v1 *events,
    uint32_t event_count, uint8_t *encoded_out,
    size_t *encoded_bytes_out, worr_native_record_ref_v1 *ref_out,
    worr_native_rx_message_v1 *message_out)
{
    worr_native_event_batch_info_v1 info;

    CHECK(Worr_NativeEventBatchEncodeV1(
              events, event_count, WORR_EVENT_STREAM_MAX_ENTITIES_V1,
              encoded_out, WORR_NATIVE_EVENT_BATCH_MAX_PAYLOAD_BYTES,
              encoded_bytes_out, &info) == WORR_NATIVE_EVENT_BATCH_OK);
    memset(ref_out, 0, sizeof(*ref_out));
    ref_out->record_class = WORR_NATIVE_RECORD_EVENT_V1;
    ref_out->record_schema_version =
        WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA;
    ref_out->object_epoch = info.stream_epoch;
    ref_out->object_sequence = info.last_sequence;
    return stage_payload_with_ref(
        fixture, encoded_out, (uint32_t)*encoded_bytes_out, *ref_out,
        message_out);
}

static int encode_descriptor_repeat_packet(
    const test_fixture *fixture,
    const worr_event_stream_descriptor_v1 *descriptor,
    uint32_t message_sequence, uint8_t *packet_out,
    size_t *packet_bytes_out)
{
    uint8_t encoded[128];
    size_t encoded_bytes;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 ref;

    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              descriptor, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInspectV1(
              encoded, encoded_bytes, &info) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &ref));
    return encode_one_data_packet(
        fixture, message_sequence, ref, encoded, (uint32_t)encoded_bytes,
        packet_out, packet_bytes_out);
}

static int encode_event_repeat_packet(
    const test_fixture *fixture, const worr_event_record_v1 *event,
    uint32_t message_sequence, uint8_t *packet_out,
    size_t *packet_bytes_out)
{
    uint8_t encoded[WORR_NATIVE_CODEC_MAX_EVENT_ENCODED_BYTES];
    size_t encoded_bytes;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 ref;

    CHECK(Worr_NativeCodecEventEncodeV1(
              event, WORR_EVENT_STREAM_MAX_ENTITIES_V1, encoded,
              sizeof(encoded), &encoded_bytes) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInspectV1(
              encoded, encoded_bytes, &info) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &ref));
    return encode_one_data_packet(
        fixture, message_sequence, ref, encoded, (uint32_t)encoded_bytes,
        packet_out, packet_bytes_out);
}

static worr_native_event_admission_result_v1 revalidate_repeat(
    test_fixture *fixture, const void *packet, size_t packet_bytes)
{
    return Worr_NativeEventAdmissionRevalidateCommittedRepeatV1(
        &fixture->owner, &fixture->binding, &fixture->session,
        fixture->slots, TEST_RX_CAPACITY, fixture->arena,
        sizeof(fixture->arena), fixture->next_tick++, packet, packet_bytes,
        0, &fixture->ledger, &fixture->consumer);
}

static int stage_fragmented_event(
    test_fixture *fixture, const worr_event_record_v1 *event,
    worr_native_rx_message_v1 *message_out, uint8_t *first_packet_out,
    size_t *first_packet_bytes_out, uint16_t *fragment_count_out)
{
    uint8_t encoded[WORR_NATIVE_CODEC_MAX_EVENT_ENCODED_BYTES];
    uint8_t datagram[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t encoded_bytes;
    size_t datagram_bytes;
    size_t packet_bytes;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 ref;
    worr_native_envelope_fragmenter_v1 fragmenter;
    worr_native_rx_result_v1 rx_result;
    worr_native_rx_message_v1 message;
    const uint32_t message_sequence = fixture->next_message_sequence++;
    uint16_t fragment_index = 0;

    CHECK(Worr_NativeCodecEventEncodeV1(
              event, WORR_EVENT_STREAM_MAX_ENTITIES_V1, encoded,
              sizeof(encoded), &encoded_bytes) == WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes <= TEST_PAYLOAD_STRIDE);
    CHECK(Worr_NativeCodecInspectV1(
              encoded, encoded_bytes, &info) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &ref));
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, fixture->binding.transport_epoch, message_sequence,
        ref, 1, encoded, (uint32_t)encoded_bytes,
        WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES + 24u));
    CHECK(fragmenter.fragment_count > 1);

    while ((fragmenter.state_flags & WORR_NATIVE_FRAGMENTER_EXHAUSTED) == 0) {
        CHECK(Worr_NativeEnvelopeFragmentNextV1(
                  &fragmenter, encoded, (uint32_t)encoded_bytes, datagram,
                  sizeof(datagram), &datagram_bytes) ==
              WORR_NATIVE_ENVELOPE_EMIT_OK);
        CHECK(encode_data_datagram_packet(
                  fixture, datagram, datagram_bytes, packet,
                  &packet_bytes) == 0);
        if (fragment_index == 0) {
            memcpy(first_packet_out, packet, packet_bytes);
            *first_packet_bytes_out = packet_bytes;
        }
        memset(&message, 0xa5, sizeof(message));
        rx_result = WORR_NATIVE_RX_INVALID_STATE;
        CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
                  &fixture->session, fixture->slots, TEST_RX_CAPACITY,
                  fixture->arena, sizeof(fixture->arena),
                  fixture->next_tick++, packet, packet_bytes, 0,
                  &fixture->ledger, &rx_result, &message) ==
              WORR_NATIVE_CARRIER_SESSION_OK);
        if ((fragmenter.state_flags & WORR_NATIVE_FRAGMENTER_EXHAUSTED) != 0) {
            CHECK(rx_result == WORR_NATIVE_RX_MESSAGE_COMPLETE);
            *message_out = message;
        } else {
            CHECK(rx_result == WORR_NATIVE_RX_FRAGMENT_ACCEPTED);
        }
        ++fragment_index;
    }
    CHECK(fragment_index == fragmenter.fragment_count);
    *fragment_count_out = fragment_index;
    return 0;
}

static worr_native_event_admission_result_v1 admit(
    test_fixture *fixture, const worr_native_rx_message_v1 *message)
{
    return Worr_NativeEventAdmissionCommitCompletedV1(
        &fixture->owner, &fixture->binding, &fixture->session, fixture->slots,
        TEST_RX_CAPACITY, fixture->arena, sizeof(fixture->arena),
        &fixture->ledger, message, &fixture->consumer);
}

static worr_native_carrier_ack_receipt_v1 *find_transport_receipt(
    test_fixture *fixture, uint32_t message_sequence)
{
    uint16_t index;

    for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY;
         ++index) {
        if ((fixture->ledger.receipts[index].state_flags &
             WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED) != 0 &&
            fixture->ledger.receipts[index].message_sequence ==
                message_sequence) {
            return &fixture->ledger.receipts[index];
        }
    }
    return NULL;
}

static int check_no_ack_due(test_fixture *fixture)
{
    worr_native_ack_range_v1 ranges[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
    worr_native_ack_range_v1 ranges_before[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_carrier_ack_emit_token_v1 token_before;
    uint16_t range_count = UINT16_C(0xa5a5);
    uint16_t range_count_before = range_count;

    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &fixture->ledger, fixture->next_tick, 0) ==
          WORR_NATIVE_CARRIER_ACK_NOT_DUE);
    memset(ranges, 0xa5, sizeof(ranges));
    memcpy(ranges_before, ranges, sizeof(ranges));
    memset(&token, 0xa5, sizeof(token));
    token_before = token;
    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &fixture->ledger, fixture->next_tick, 0,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &range_count,
              &token) == WORR_NATIVE_CARRIER_ACK_NOT_DUE);
    CHECK(memcmp(ranges, ranges_before, sizeof(ranges)) == 0);
    CHECK(memcmp(&token, &token_before, sizeof(token)) == 0);
    CHECK(range_count == range_count_before);
    return 0;
}

static int activate_default_descriptor(test_fixture *fixture)
{
    worr_event_stream_descriptor_v1 descriptor;
    worr_native_rx_message_v1 message;

    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    CHECK(stage_descriptor_with_ref(
              fixture, &descriptor, NULL, &message) == 0);
    CHECK(admit(fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED);
    CHECK(Worr_EventStreamOwnerValidateV1(&fixture->owner));
    CHECK((fixture->owner.state_flags &
           WORR_EVENT_STREAM_OWNER_ACTIVE) != 0);
    return 0;
}

static int activate_batch_descriptor(test_fixture *fixture)
{
    worr_event_stream_descriptor_v1 descriptor;
    worr_native_rx_message_v1 message;

    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    descriptor.flags |= WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2;
    CHECK(Worr_EventStreamDescriptorValidateV1(&descriptor));
    CHECK(stage_descriptor_with_ref(
              fixture, &descriptor, NULL, &message) == 0);
    CHECK(admit(fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED);
    return 0;
}

static int test_descriptor_lifecycle_and_precommit(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_stream_descriptor_v1 descriptor;
    worr_event_stream_descriptor_v1 conflict;
    worr_native_rx_message_v1 message;
    worr_native_carrier_ack_receipt_v1 *receipt;
    worr_native_ack_range_v1 ranges[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
    worr_native_carrier_ack_emit_token_v1 token;
    uint16_t range_count;
    uint32_t reset_calls;
    uint32_t status_calls;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED);
    CHECK(fixture.fake.reset_calls == 1 &&
          fixture.fake.status_calls == 1 &&
          fixture.fake.submit_calls == 0);
    CHECK(fixture.owner.epoch_high_water == TEST_STREAM_EPOCH);
    CHECK(Worr_EventStreamDescriptorEqualV1(
        &fixture.owner.descriptor, &descriptor));
    receipt = find_transport_receipt(&fixture, 1);
    CHECK(receipt != NULL &&
          receipt->record_class ==
              WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);

    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_DUPLICATE);
    CHECK(fixture.fake.reset_calls == 1 &&
          fixture.fake.status_calls == 2);
    receipt = find_transport_receipt(&fixture, 2);
    CHECK(receipt != NULL &&
          receipt->record_class ==
              WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);

    /* A prepared ACK token makes the retained commit retryable.  The
     * semantic consumer must not be called before that precommit succeeds. */
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &fixture.ledger, fixture.next_tick, 1000, 2, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &range_count, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(range_count == 1);
    reset_calls = fixture.fake.reset_calls;
    status_calls = fixture.fake.status_calls;
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_RETRY_UNCOMMITTED);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls == reset_calls &&
          fixture.fake.status_calls == status_calls &&
          fixture.fake.submit_calls == 0);
    CHECK(Worr_NativeCarrierAckAbortV1(
              &fixture.ledger, &token) == WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeRxSessionDiscardV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              message.slot_index, message.message_sequence) ==
          WORR_NATIVE_RX_DISCARDED);

    CHECK(Worr_EventStreamDescriptorInitV1(
        &conflict, TEST_STREAM_EPOCH, 2));
    CHECK(stage_descriptor_with_ref(
              &fixture, &conflict, NULL, &message) == 0);
    reset_calls = fixture.fake.reset_calls;
    status_calls = fixture.fake.status_calls;
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_CONFLICT);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls == reset_calls &&
          fixture.fake.status_calls == status_calls);
    return 0;
}

static int test_event_accept_duplicate_and_degraded(void)
{
    test_fixture fixture;
    worr_event_record_v1 event;
    worr_native_rx_message_v1 message;
    worr_native_carrier_ack_receipt_v1 *receipt;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_default_descriptor(&fixture) == 0);

    event = make_event(TEST_STREAM_EPOCH, 1);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_EVENT_ACCEPTED);
    CHECK(fixture.fake.submit_calls == 1 &&
          fixture.fake.status_calls == 3);
    CHECK(memcmp(&fixture.fake.last_record, &event, sizeof(event)) == 0);
    CHECK(Worr_EventReceiptContainsV1(
        &fixture.fake.status.receipt, event.event_id));
    receipt = find_transport_receipt(&fixture, 2);
    CHECK(receipt != NULL &&
          receipt->record_class == WORR_NATIVE_RECORD_EVENT_V1);

    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_EVENT_DUPLICATE);
    CHECK(fixture.fake.submit_calls == 2 &&
          fixture.fake.status.authority_count == 1);

    event = make_event(TEST_STREAM_EPOCH, 2);
    fixture.fake.report_degraded = true;
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_EVENT_DEGRADED);
    CHECK(Worr_EventReceiptContainsV1(
        &fixture.fake.status.receipt, event.event_id));
    CHECK((fixture.fake.status.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED) != 0);
    CHECK(Worr_NativeRxSessionValidateV1(
        &fixture.session, fixture.slots, TEST_RX_CAPACITY));
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    return 0;
}

static worr_command_record_v1 make_command(void)
{
    worr_command_record_v1 record;

    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id.epoch = 3;
    record.command_id.sequence = 1;
    record.sample_time_us = 16000;
    record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    record.command.struct_size = sizeof(record.command);
    record.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    record.command.duration_ms = 16;
    record.render_watermark.struct_size = sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    record.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
    record.render_watermark.source_server_tick = 1;
    record.render_watermark.tick_interval_us = 16000;
    record.render_watermark.source_server_time_us = 16000;
    record.render_watermark.rendered_server_time_us = 16000;
    return record;
}

static int test_descriptor_gates_and_exact_identity(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_record_v1 event;
    worr_event_stream_descriptor_v1 descriptor;
    worr_command_record_v1 command;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 ref;
    worr_native_rx_message_v1 message;
    uint8_t encoded[256];
    size_t encoded_bytes;
    uint32_t callback_count;

    /* EVENT cannot establish its own semantic stream. */
    CHECK(fixture_init(&fixture) == 0);
    event = make_event(TEST_STREAM_EPOCH, 1);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_NOT_READY);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls == 0 &&
          fixture.fake.submit_calls == 0 &&
          fixture.fake.status_calls == 0);

    /* A codec identity that differs from the complete envelope proof is
     * malformed, even though both identities are independently valid. */
    CHECK(fixture_init(&fixture) == 0);
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &descriptor, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInspectV1(
              encoded, encoded_bytes, &info) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &ref));
    ++ref.object_sequence;
    CHECK(stage_payload_with_ref(
              &fixture, encoded, (uint32_t)encoded_bytes, ref,
              &message) == 0);
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_MALFORMED);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls == 0 && fixture.fake.status_calls == 0);

    /* A fully matching but unrelated canonical class is unsupported and is
     * never committed or shown to the event consumer. */
    CHECK(fixture_init(&fixture) == 0);
    command = make_command();
    CHECK(Worr_CommandRecordValidateV1(&command, 250));
    CHECK(Worr_NativeCodecCommandEncodeV1(
              &command, 250, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInspectV1(
              encoded, encoded_bytes, &info) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &ref));
    CHECK(stage_payload_with_ref(
              &fixture, encoded, (uint32_t)encoded_bytes, ref,
              &message) == 0);
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED);
    CHECK(state_equal(&fixture, &before));

    /* Descriptor epoch and first-sequence gates are semantic and independent
     * of the still-valid transport epoch. */
    CHECK(fixture_init(&fixture) == 0);
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 5));
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED);
    callback_count = fixture.fake.submit_calls + fixture.fake.status_calls;
    event = make_event(TEST_STREAM_EPOCH + 1u, 5);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.submit_calls + fixture.fake.status_calls ==
          callback_count);
    event = make_event(TEST_STREAM_EPOCH, 4);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH);
    CHECK(state_equal(&fixture, &before));
    return 0;
}

static int check_owner_resync(const test_fixture *fixture,
                              uint32_t epoch)
{
    CHECK(Worr_EventStreamOwnerValidateV1(&fixture->owner));
    CHECK((fixture->owner.state_flags &
           WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC) != 0);
    CHECK((fixture->owner.state_flags &
           WORR_EVENT_STREAM_OWNER_ACTIVE) == 0);
    CHECK(fixture->owner.epoch_high_water == epoch);
    CHECK(fixture->fake.scrub_calls == 1);
    return 0;
}

static int test_reset_failure_resync(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_stream_descriptor_v1 descriptor;
    worr_native_rx_message_v1 message;

    CHECK(fixture_init(&fixture) == 0);
    fixture.fake.fail_reset = true;
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(transport_equal(&fixture, &before));
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);
    CHECK(fixture.fake.reset_calls == 2 &&
          fixture.fake.status_calls == 0 &&
          fixture.fake.submit_calls == 0);
    CHECK(fixture.session.transport_epoch == TEST_TRANSPORT_EPOCH &&
          fixture.ledger.transport_epoch == TEST_TRANSPORT_EPOCH);
    return 0;
}

static int test_reset_status_failure_resync(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_stream_descriptor_v1 descriptor;
    worr_native_rx_message_v1 message;

    CHECK(fixture_init(&fixture) == 0);
    fixture.fake.fail_status_call = 1;
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(transport_equal(&fixture, &before));
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);
    CHECK(fixture.fake.reset_calls == 2 &&
          fixture.fake.status_calls == 1);
    return 0;
}

static int test_event_pre_status_failure_resync(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_record_v1 event;
    worr_native_rx_message_v1 message;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_default_descriptor(&fixture) == 0);
    event = make_event(TEST_STREAM_EPOCH, 1);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    fixture.fake.fail_status_call = fixture.fake.status_calls + 1u;
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(rx_storage_equal(&fixture, &before));
    CHECK(fixture.ledger.receipt_count == 0);
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);
    CHECK(fixture.fake.submit_calls == 0);
    return 0;
}

static int test_event_submit_failure_resync(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_record_v1 event;
    worr_native_rx_message_v1 message;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_default_descriptor(&fixture) == 0);
    event = make_event(TEST_STREAM_EPOCH, 1);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    fixture.fake.force_submit_result = true;
    fixture.fake.forced_submit_result = WORR_CGAME_EVENT_RUNTIME_CONFLICT;
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(rx_storage_equal(&fixture, &before));
    CHECK(fixture.ledger.receipt_count == 0);
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);
    CHECK(fixture.fake.submit_calls == 1);
    return 0;
}

static int test_event_post_status_failure_resync(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_record_v1 event;
    worr_native_rx_message_v1 message;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_default_descriptor(&fixture) == 0);
    event = make_event(TEST_STREAM_EPOCH, 1);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    fixture.fake.fail_status_call = fixture.fake.status_calls + 2u;
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(rx_storage_equal(&fixture, &before));
    CHECK(fixture.ledger.receipt_count == 0);
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);
    CHECK(fixture.fake.submit_calls == 1);
    return 0;
}

static int test_receipt_proof_required(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_record_v1 event;
    worr_native_rx_message_v1 message;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_default_descriptor(&fixture) == 0);
    event = make_event(TEST_STREAM_EPOCH, 1);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    fixture.fake.omit_receipt = true;
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(rx_storage_equal(&fixture, &before));
    CHECK(fixture.ledger.receipt_count == 0);
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);
    CHECK(fixture.fake.submit_calls == 1);
    return 0;
}

static int test_capability_baseline_and_generation_fences(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_stream_descriptor_v1 descriptor;
    worr_event_record_v1 event;
    worr_native_rx_message_v1 message;
    uint32_t callbacks_before;

    /* Event transport without the negotiated epoch-cancellation contract
     * cannot activate the semantic endpoint. */
    CHECK(fixture_init(&fixture) == 0);
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    fixture.binding.negotiated_capabilities =
        WORR_NET_CAP_NATIVE_ENVELOPE_V1 |
        WORR_NET_CAP_NATIVE_EVENT_STREAM_V1;
    CHECK(Worr_NativeSessionBindingValidateV1(&fixture.binding));
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_NOT_NEGOTIATED);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls == 0 &&
          fixture.fake.submit_calls == 0 &&
          fixture.fake.status_calls == 0);

    /* Exact-epoch status is insufficient: the callback must also prove it
     * has not regressed behind the descriptor's semantic baseline. */
    CHECK(fixture_init(&fixture) == 0);
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 5));
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED);
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    fixture.fake.status.next_presentation_sequence = 4;
    fixture.fake.status.receipt.highest_contiguous = 3;
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(rx_storage_equal(&fixture, &before));
    CHECK(fixture.ledger.receipt_count == 0);
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);

    CHECK(fixture_init(&fixture) == 0);
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 5));
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED);
    event = make_event(TEST_STREAM_EPOCH, 5);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    fixture.fake.status.next_presentation_sequence = 4;
    fixture.fake.status.receipt.highest_contiguous = 3;
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(rx_storage_equal(&fixture, &before));
    CHECK(fixture.ledger.receipt_count == 0);
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);
    CHECK(fixture.fake.submit_calls == 0);

    /* An imported exhausted-active owner cannot reserve the fail-closed
     * generation needed after an irreversible callback, so admission stops
     * before observing or mutating the consumer. */
    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_default_descriptor(&fixture) == 0);
    event = make_event(TEST_STREAM_EPOCH, 1);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    fixture.owner.mutation_generation = UINT64_MAX;
    fixture.owner.state_flags |=
        WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED;
    CHECK(Worr_EventStreamOwnerValidateV1(&fixture.owner));
    callbacks_before = fixture.fake.reset_calls +
                       fixture.fake.submit_calls +
                       fixture.fake.status_calls;
    capture_state(&fixture, &before);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls + fixture.fake.submit_calls +
              fixture.fake.status_calls ==
          callbacks_before);
    return 0;
}

static int test_alias_and_invalid_state_rollback(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_stream_descriptor_v1 descriptor;
    worr_native_rx_message_v1 message;
    worr_native_rx_message_v1 bad_message;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    capture_state(&fixture, &before);
    CHECK(Worr_NativeEventAdmissionCommitCompletedV1(
              &fixture.owner, &fixture.binding, &fixture.session,
              fixture.slots,
              TEST_RX_CAPACITY, fixture.arena, sizeof(fixture.arena),
              &fixture.ledger,
              (const worr_native_rx_message_v1 *)&fixture.owner,
              &fixture.consumer) ==
          WORR_NATIVE_EVENT_ADMISSION_INVALID_ARGUMENT);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls == 0 && fixture.fake.status_calls == 0);

    bad_message = message;
    bad_message.payload_crc32 ^= 1u;
    CHECK(admit(&fixture, &bad_message) ==
          WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls == 0 && fixture.fake.status_calls == 0);

    CHECK(Worr_NativeEventAdmissionCommitCompletedV1(
              &fixture.owner, &fixture.binding, &fixture.session,
              fixture.slots,
              TEST_RX_CAPACITY, fixture.arena, sizeof(fixture.arena),
              &fixture.ledger, &message,
              (const worr_native_event_consumer_v1 *)&fixture.owner) ==
          WORR_NATIVE_EVENT_ADMISSION_INVALID_ARGUMENT);
    CHECK(state_equal(&fixture, &before));

    fixture.consumer.opaque = &fixture.owner;
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_INVALID_ARGUMENT);
    CHECK(state_equal(&fixture, &before));
    return 0;
}

static int test_descriptor_repeat_revalidation_and_generic_gate(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_stream_descriptor_v1 descriptor;
    worr_native_rx_message_v1 message;
    worr_native_rx_message_v1 output_message;
    worr_native_rx_message_v1 output_message_before;
    worr_native_carrier_ack_receipt_v1 *receipt;
    worr_native_rx_result_v1 rx_result = WORR_NATIVE_RX_INVALID_STATE;
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes;
    uint32_t reset_calls;
    uint32_t submit_calls;
    uint32_t status_calls;
    uint64_t already_committed;
    uint64_t repeat_acknowledgements;
    uint64_t repeat_refreshes;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    CHECK(encode_descriptor_repeat_packet(
              &fixture, &descriptor, fixture.next_message_sequence,
              packet, &packet_bytes) == 0);
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    CHECK(message.message_sequence == 1);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED);

    receipt = find_transport_receipt(&fixture, message.message_sequence);
    CHECK(receipt != NULL);
    receipt->handoffs_remaining = 0;
    receipt->handoff_attempts = fixture.ledger.proactive_handoffs;
    receipt->last_handoff_tick = 1;
    receipt->state_flags &=
        (uint8_t)~WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE;
    fixture.ledger.last_handoff_tick = 1;
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &fixture.ledger, fixture.next_tick, 0) ==
          WORR_NATIVE_CARRIER_ACK_NOT_DUE);

    reset_calls = fixture.fake.reset_calls;
    submit_calls = fixture.fake.submit_calls;
    status_calls = fixture.fake.status_calls;
    already_committed = fixture.session.telemetry.already_committed;
    repeat_acknowledgements =
        fixture.session.telemetry.repeat_acknowledgements;
    repeat_refreshes = fixture.ledger.telemetry.repeat_refreshes;
    CHECK(revalidate_repeat(&fixture, packet, packet_bytes) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_REPEAT_REVALIDATED);
    CHECK(fixture.fake.reset_calls == reset_calls &&
          fixture.fake.submit_calls == submit_calls &&
          fixture.fake.status_calls == status_calls + 1u);
    CHECK(fixture.session.telemetry.already_committed ==
          already_committed + 1u);
    CHECK(fixture.session.telemetry.repeat_acknowledgements ==
          repeat_acknowledgements + 1u);
    CHECK(fixture.ledger.telemetry.repeat_refreshes ==
          repeat_refreshes + 1u);
    receipt = find_transport_receipt(&fixture, message.message_sequence);
    CHECK(receipt != NULL &&
          receipt->handoffs_remaining == fixture.ledger.proactive_handoffs &&
          (receipt->state_flags &
           WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE) != 0);
    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &fixture.ledger, fixture.next_tick, 0) ==
          WORR_NATIVE_CARRIER_ACK_OK);

    /* The generic retained bridge may observe this repeat internally, but
     * cannot publish RX telemetry or rearm a semantic receipt. */
    capture_state(&fixture, &before);
    memset(&output_message, 0xa5, sizeof(output_message));
    output_message_before = output_message;
    CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              fixture.arena, sizeof(fixture.arena), fixture.next_tick,
              packet, packet_bytes, 0, &fixture.ledger, &rx_result,
              &output_message) ==
          WORR_NATIVE_CARRIER_SESSION_SEMANTIC_REVALIDATION_REQUIRED);
    CHECK(state_equal(&fixture, &before));
    CHECK(rx_result == WORR_NATIVE_RX_INVALID_STATE);
    CHECK(memcmp(&output_message, &output_message_before,
                 sizeof(output_message)) == 0);
    CHECK(fixture.fake.status_calls == status_calls + 1u);
    return 0;
}

static int test_event_repeat_revalidation_and_multifragment(void)
{
    test_fixture fixture;
    worr_event_record_v1 event;
    worr_native_rx_message_v1 message;
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t first_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes;
    size_t first_packet_bytes;
    uint16_t fragment_count;
    uint32_t reset_calls;
    uint32_t submit_calls;
    uint32_t status_calls;
    uint64_t repeat_refreshes;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_default_descriptor(&fixture) == 0);

    event = make_event(TEST_STREAM_EPOCH, 1);
    CHECK(encode_event_repeat_packet(
              &fixture, &event, fixture.next_message_sequence,
              packet, &packet_bytes) == 0);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_EVENT_ACCEPTED);
    reset_calls = fixture.fake.reset_calls;
    submit_calls = fixture.fake.submit_calls;
    status_calls = fixture.fake.status_calls;
    repeat_refreshes = fixture.ledger.telemetry.repeat_refreshes;
    CHECK(revalidate_repeat(&fixture, packet, packet_bytes) ==
          WORR_NATIVE_EVENT_ADMISSION_EVENT_REPEAT_REVALIDATED);
    CHECK(fixture.fake.reset_calls == reset_calls &&
          fixture.fake.submit_calls == submit_calls &&
          fixture.fake.status_calls == status_calls + 1u);
    CHECK(fixture.ledger.telemetry.repeat_refreshes ==
          repeat_refreshes + 1u);

    event = make_event(TEST_STREAM_EPOCH, 2);
    CHECK(stage_fragmented_event(
              &fixture, &event, &message, first_packet,
              &first_packet_bytes, &fragment_count) == 0);
    CHECK(fragment_count > 1);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_EVENT_ACCEPTED);
    reset_calls = fixture.fake.reset_calls;
    submit_calls = fixture.fake.submit_calls;
    status_calls = fixture.fake.status_calls;
    CHECK(revalidate_repeat(
              &fixture, first_packet, first_packet_bytes) ==
          WORR_NATIVE_EVENT_ADMISSION_EVENT_REPEAT_REVALIDATED);
    CHECK(fixture.fake.reset_calls == reset_calls &&
          fixture.fake.submit_calls == submit_calls &&
          fixture.fake.status_calls == status_calls + 1u);
    CHECK(Worr_NativeRxSessionValidateV1(
        &fixture.session, fixture.slots, TEST_RX_CAPACITY));
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    return 0;
}

static int test_repeat_resync_retires_semantic_receipts(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_stream_descriptor_v1 descriptor;
    worr_event_record_v1 event;
    worr_native_rx_message_v1 message;
    worr_native_carrier_ack_receipt_v1 command_before;
    worr_native_carrier_ack_receipt_v1 *command_receipt;
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes;
    uint32_t reset_calls;
    uint32_t submit_calls;
    uint32_t status_calls;
    uint64_t receipts_retired;
    uint64_t ledger_generation;
    uint16_t receipt_index;

    /* A failed fresh descriptor-status read cannot publish the staged
     * ALREADY_COMMITTED telemetry and retires its former semantic ACK. */
    CHECK(fixture_init(&fixture) == 0);
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    CHECK(encode_descriptor_repeat_packet(
              &fixture, &descriptor, fixture.next_message_sequence,
              packet, &packet_bytes) == 0);
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED);
    reset_calls = fixture.fake.reset_calls;
    submit_calls = fixture.fake.submit_calls;
    status_calls = fixture.fake.status_calls;
    receipts_retired = fixture.ledger.telemetry.receipts_retired;
    ledger_generation = fixture.ledger.mutation_generation;
    fixture.fake.fail_status_call = status_calls + 1u;
    capture_state(&fixture, &before);
    CHECK(revalidate_repeat(&fixture, packet, packet_bytes) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(rx_storage_equal(&fixture, &before));
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);
    CHECK(fixture.fake.reset_calls == reset_calls + 1u &&
          fixture.fake.submit_calls == submit_calls &&
          fixture.fake.status_calls == status_calls + 1u);
    CHECK(fixture.ledger.receipt_count == 0);
    CHECK(fixture.ledger.telemetry.receipts_retired ==
          receipts_retired + 1u);
    CHECK(fixture.ledger.mutation_generation == ledger_generation + 1u);
    CHECK(check_no_ack_due(&fixture) == 0);

    /* An active status without the exact event receipt is equally
     * insufficient; descriptor and event ACKs are both retired. */
    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_default_descriptor(&fixture) == 0);
    event = make_event(TEST_STREAM_EPOCH, 1);
    CHECK(encode_event_repeat_packet(
              &fixture, &event, fixture.next_message_sequence,
              packet, &packet_bytes) == 0);
    CHECK(stage_event_with_ref(&fixture, &event, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_EVENT_ACCEPTED);
    for (receipt_index = 0;
         receipt_index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY;
         ++receipt_index) {
        if ((fixture.ledger.receipts[receipt_index].state_flags &
             WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED) == 0) {
            break;
        }
    }
    CHECK(receipt_index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY);
    command_receipt = &fixture.ledger.receipts[receipt_index];
    memset(command_receipt, 0, sizeof(*command_receipt));
    command_receipt->message_sequence = UINT32_C(0x7ffffffe);
    command_receipt->handoff_attempts = 1;
    command_receipt->record_class = WORR_NATIVE_RECORD_COMMAND_V1;
    command_receipt->state_flags =
        WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED;
    ++fixture.ledger.receipt_count;
    command_before = *command_receipt;
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    fixture.fake.status.receipt.highest_contiguous = 0;
    fixture.fake.status.receipt.selective_mask = 0;
    CHECK(!Worr_EventReceiptContainsV1(
        &fixture.fake.status.receipt, event.event_id));
    reset_calls = fixture.fake.reset_calls;
    submit_calls = fixture.fake.submit_calls;
    status_calls = fixture.fake.status_calls;
    receipts_retired = fixture.ledger.telemetry.receipts_retired;
    ledger_generation = fixture.ledger.mutation_generation;
    CHECK(fixture.ledger.receipt_count == 3);
    capture_state(&fixture, &before);
    CHECK(revalidate_repeat(&fixture, packet, packet_bytes) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(rx_storage_equal(&fixture, &before));
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);
    CHECK(fixture.fake.reset_calls == reset_calls + 1u &&
          fixture.fake.submit_calls == submit_calls &&
          fixture.fake.status_calls == status_calls + 1u);
    CHECK(fixture.ledger.receipt_count == 1);
    command_receipt = find_transport_receipt(
        &fixture, command_before.message_sequence);
    CHECK(command_receipt != NULL &&
          memcmp(command_receipt, &command_before,
                 sizeof(command_before)) == 0);
    CHECK(fixture.ledger.telemetry.receipts_retired ==
          receipts_retired + 2u);
    CHECK(fixture.ledger.mutation_generation == ledger_generation + 1u);
    CHECK(check_no_ack_due(&fixture) == 0);
    return 0;
}

static int test_repeat_transactional_rejections(void)
{
    test_fixture fixture;
    state_snapshot before;
    worr_event_stream_descriptor_v1 descriptor;
    worr_event_record_v1 event;
    worr_command_record_v1 command;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 ref;
    worr_native_envelope_fragmenter_v1 fragmenter;
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 ranges[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
    worr_native_carrier_ack_emit_token_v1 token;
    uint8_t encoded[WORR_NATIVE_CODEC_MAX_EVENT_ENCODED_BYTES];
    uint8_t datagram[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t encoded_bytes;
    size_t datagram_bytes;
    size_t packet_bytes;
    uint16_t range_count;
    uint32_t callbacks;
    uint32_t message_sequence;

    /* A first fragment for a new semantic message is rejected before the
     * copied base acceptor can write reassembly bytes or slot/session state. */
    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_default_descriptor(&fixture) == 0);
    event = make_event(TEST_STREAM_EPOCH, 1);
    CHECK(Worr_NativeCodecEventEncodeV1(
              &event, WORR_EVENT_STREAM_MAX_ENTITIES_V1, encoded,
              sizeof(encoded), &encoded_bytes) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInspectV1(
              encoded, encoded_bytes, &info) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &ref));
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, fixture.binding.transport_epoch,
        fixture.next_message_sequence, ref, 1, encoded,
        (uint32_t)encoded_bytes,
        WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES + 24u));
    CHECK(fragmenter.fragment_count > 1);
    CHECK(Worr_NativeEnvelopeFragmentNextV1(
              &fragmenter, encoded, (uint32_t)encoded_bytes, datagram,
              sizeof(datagram), &datagram_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_OK);
    CHECK(encode_data_datagram_packet(
              &fixture, datagram, datagram_bytes, packet,
              &packet_bytes) == 0);
    callbacks = fixture.fake.reset_calls + fixture.fake.submit_calls +
                fixture.fake.status_calls;
    capture_state(&fixture, &before);
    CHECK(revalidate_repeat(&fixture, packet, packet_bytes) ==
          WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls + fixture.fake.submit_calls +
              fixture.fake.status_calls ==
          callbacks);

    /* A genuinely committed but unrelated canonical class is not an event
     * repeat and cannot reach the semantic consumer. */
    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_default_descriptor(&fixture) == 0);
    command = make_command();
    CHECK(Worr_NativeCodecCommandEncodeV1(
              &command, 250, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInspectV1(
              encoded, encoded_bytes, &info) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &ref));
    message_sequence = fixture.next_message_sequence;
    CHECK(encode_one_data_packet(
              &fixture, message_sequence, ref, encoded,
              (uint32_t)encoded_bytes, packet, &packet_bytes) == 0);
    CHECK(stage_payload_with_ref(
              &fixture, encoded, (uint32_t)encoded_bytes, ref,
              &message) == 0);
    CHECK(message.message_sequence == message_sequence);
    CHECK(Worr_NativeCarrierSessionCommitRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              message.slot_index, message.message_sequence,
              &fixture.ledger) == WORR_NATIVE_RX_COMMITTED);
    callbacks = fixture.fake.reset_calls + fixture.fake.submit_calls +
                fixture.fake.status_calls;
    capture_state(&fixture, &before);
    CHECK(revalidate_repeat(&fixture, packet, packet_bytes) ==
          WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls + fixture.fake.submit_calls +
              fixture.fake.status_calls ==
          callbacks);

    /* An in-flight ACK token serializes the ledger: retry is returned before
     * packet observation, callbacks, or any live-state publication. */
    CHECK(fixture_init(&fixture) == 0);
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, TEST_STREAM_EPOCH, 1));
    CHECK(encode_descriptor_repeat_packet(
              &fixture, &descriptor, fixture.next_message_sequence,
              packet, &packet_bytes) == 0);
    CHECK(stage_descriptor_with_ref(
              &fixture, &descriptor, NULL, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED);
    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &fixture.ledger, fixture.next_tick, 0, 1, ranges,
              WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &range_count, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(range_count == 1);
    callbacks = fixture.fake.reset_calls + fixture.fake.submit_calls +
                fixture.fake.status_calls;
    capture_state(&fixture, &before);
    CHECK(revalidate_repeat(&fixture, packet, packet_bytes) ==
          WORR_NATIVE_EVENT_ADMISSION_RETRY_UNCOMMITTED);
    CHECK(state_equal(&fixture, &before));
    CHECK(fixture.fake.reset_calls + fixture.fake.submit_calls +
              fixture.fake.status_calls ==
          callbacks);
    CHECK(Worr_NativeCarrierAckAbortV1(
              &fixture.ledger, &token) == WORR_NATIVE_CARRIER_ACK_OK);
    return 0;
}

static void make_same_fence_events(worr_event_record_v1 *events,
                                   uint32_t count)
{
    uint32_t index;
    for (index = 0; index < count; ++index) {
        events[index] = make_event(TEST_STREAM_EPOCH, index + 1u);
        events[index].source_tick = 2222;
        events[index].source_time_us = UINT64_C(3333000);
    }
}

static int test_schema2_batch_atomic_admission_and_repeat_range(void)
{
    test_fixture fixture;
    test_fixture fallback;
    test_fixture backpressure;
    state_snapshot before;
    worr_event_record_v1 events[5];
    worr_native_rx_message_v1 message;
    worr_native_record_ref_v1 ref;
    worr_native_carrier_ack_receipt_v1 *receipt;
    uint8_t encoded[WORR_NATIVE_EVENT_BATCH_MAX_PAYLOAD_BYTES];
    uint8_t repeat_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t encoded_bytes;
    size_t repeat_bytes;
    uint32_t submit_calls;
    uint32_t status_calls;
    uint32_t index;

    make_same_fence_events(events, 5);
    CHECK(fixture_init(&fixture) == 0);
    CHECK(activate_batch_descriptor(&fixture) == 0);
    CHECK(stage_event_batch(&fixture, events, 5, encoded, &encoded_bytes,
                            &ref, &message) == 0);
    CHECK(admit(&fixture, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_EVENT_ACCEPTED);
    CHECK(fixture.fake.submit_calls == 1 &&
          fixture.fake.last_batch_count == 5 &&
          fixture.fake.status.authority_count == 5);
    for (index = 0; index < 5; ++index) {
      CHECK(fixture.fake.last_batch[index].event_id.sequence == index + 1u);
      CHECK(Worr_EventReceiptContainsV1(
          &fixture.fake.status.receipt, events[index].event_id));
    }
    receipt = find_transport_receipt(&fixture, message.message_sequence);
    CHECK(receipt != NULL && receipt->semantic_first_sequence == 1);

    CHECK(encode_one_data_packet(
              &fixture, message.message_sequence, ref, encoded,
              (uint32_t)encoded_bytes, repeat_packet, &repeat_bytes) == 0);
    submit_calls = fixture.fake.submit_calls;
    status_calls = fixture.fake.status_calls;
    CHECK(revalidate_repeat(&fixture, repeat_packet, repeat_bytes) ==
          WORR_NATIVE_EVENT_ADMISSION_EVENT_REPEAT_REVALIDATED);
    CHECK(fixture.fake.submit_calls == submit_calls &&
          fixture.fake.status_calls == status_calls + 1u);
    receipt = find_transport_receipt(&fixture, message.message_sequence);
    CHECK(receipt != NULL && receipt->semantic_first_sequence == 1);

    /* Remove only nested sequence 2 while retaining 1 and 3..5.  Repeat
     * revalidation must inspect the persisted full batch range, not merely
     * the outer last-ID reference. */
    fixture.fake.status.receipt.highest_contiguous = 1;
    fixture.fake.status.receipt.selective_mask = UINT64_C(0x0e);
    CHECK(Worr_EventReceiptContainsV1(
        &fixture.fake.status.receipt, events[0].event_id));
    CHECK(!Worr_EventReceiptContainsV1(
        &fixture.fake.status.receipt, events[1].event_id));
    CHECK(Worr_EventReceiptContainsV1(
        &fixture.fake.status.receipt, events[4].event_id));
    CHECK(revalidate_repeat(&fixture, repeat_packet, repeat_bytes) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(check_owner_resync(&fixture, TEST_STREAM_EPOCH) == 0);

    /* Schema 2 is rejected before transport or semantic mutation unless the
     * acknowledged descriptor explicitly opted in. */
    CHECK(fixture_init(&fallback) == 0);
    CHECK(activate_default_descriptor(&fallback) == 0);
    CHECK(stage_event_batch(&fallback, events, 5, encoded, &encoded_bytes,
                            &ref, &message) == 0);
    capture_state(&fallback, &before);
    submit_calls = fallback.fake.submit_calls;
    status_calls = fallback.fake.status_calls;
    CHECK(admit(&fallback, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED);
    CHECK(state_equal(&fallback, &before) &&
          fallback.fake.submit_calls == submit_calls &&
          fallback.fake.status_calls == status_calls);

    /* A consumer backpressure result cannot publish the transport commit or
     * a prefix of the logical batch.  The existing fail-closed contract then
     * enters semantic resync because a callback was invoked. */
    CHECK(fixture_init(&backpressure) == 0);
    CHECK(activate_batch_descriptor(&backpressure) == 0);
    CHECK(stage_event_batch(&backpressure, events, 5, encoded,
                            &encoded_bytes, &ref, &message) == 0);
    backpressure.fake.force_submit_result = true;
    backpressure.fake.forced_submit_result =
        WORR_CGAME_EVENT_RUNTIME_CAPACITY;
    capture_state(&backpressure, &before);
    CHECK(admit(&backpressure, &message) ==
          WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(rx_storage_equal(&backpressure, &before) &&
          backpressure.fake.status.authority_count == 0 &&
          backpressure.fake.last_batch_count == 0 &&
          backpressure.fake.submit_calls == 1);
    CHECK(check_owner_resync(&backpressure, TEST_STREAM_EPOCH) == 0);
    return 0;
}

int main(void)
{
    if (test_descriptor_lifecycle_and_precommit() != 0 ||
        test_event_accept_duplicate_and_degraded() != 0 ||
        test_descriptor_gates_and_exact_identity() != 0 ||
        test_reset_failure_resync() != 0 ||
        test_reset_status_failure_resync() != 0 ||
        test_event_pre_status_failure_resync() != 0 ||
        test_event_submit_failure_resync() != 0 ||
        test_event_post_status_failure_resync() != 0 ||
        test_receipt_proof_required() != 0 ||
        test_capability_baseline_and_generation_fences() != 0 ||
        test_alias_and_invalid_state_rollback() != 0 ||
        test_descriptor_repeat_revalidation_and_generic_gate() != 0 ||
        test_event_repeat_revalidation_and_multifragment() != 0 ||
        test_schema2_batch_atomic_admission_and_repeat_range() != 0 ||
        test_repeat_resync_retires_semantic_receipts() != 0 ||
        test_repeat_transactional_rejections() != 0) {
        return 1;
    }
    puts("native event admission tests passed");
    return 0;
}
