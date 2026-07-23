/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_event_sender.h"

#include <limits.h>
#include <string.h>

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
        right_bytes > UINTPTR_MAX - right_begin)
        return true;
    left_end = left_begin + left_bytes;
    right_end = right_begin + right_bytes;
    return left_begin < right_end && right_begin < left_end;
}

static void saturating_increment(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static void saturating_add(uint64_t *value, uint32_t amount)
{
    while (amount-- != 0)
        saturating_increment(value);
}

static uint16_t minimum_datagram(uint16_t configured, size_t payload_bytes)
{
    const size_t exact =
        payload_bytes + WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES;
    return exact < configured ? (uint16_t)exact : configured;
}

static bool record_ref_equal(worr_native_record_ref_v1 left,
                             worr_native_record_ref_v1 right)
{
    return left.record_class == right.record_class &&
           left.reserved0 == right.reserved0 &&
           left.record_schema_version == right.record_schema_version &&
           left.object_epoch == right.object_epoch &&
           left.object_sequence == right.object_sequence;
}

static bool combined_binding(
    const worr_native_session_binding_v1 *binding)
{
    return binding != NULL &&
           (binding->negotiated_capabilities &
            WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PRIVATE_MASK) ==
               WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PRIVATE_MASK;
}

static bool combined_event_sequence_space_valid(
    const worr_native_event_sender_v1 *sender)
{
    uint32_t index;

    if (!combined_binding(&sender->binding))
        return true;
    if (sender->tx.next_message_sequence >
        WORR_NATIVE_COMBINED_SNAPSHOT_MESSAGE_SEQUENCE_FIRST) {
        return false;
    }
    for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
        if ((sender->tx_slots[index].state_flags &
             WORR_NATIVE_TX_SLOT_OCCUPIED) != 0 &&
            sender->tx_slots[index].message_sequence >
                WORR_NATIVE_COMBINED_EVENT_MESSAGE_SEQUENCE_LAST) {
            return false;
        }
    }
    return true;
}

static uint32_t payload_handle(uint32_t generation, uint32_t index)
{
    return (generation << WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_BITS) | index;
}

static worr_native_event_sender_payload_v1 *payload_find(
    worr_native_event_sender_payload_v1 *payloads, uint32_t handle,
    uint32_t *index_out)
{
    const uint32_t index =
        handle & WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_MASK;
    const uint32_t generation =
        handle >> WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_BITS;
    worr_native_event_sender_payload_v1 *payload;

    if (generation == 0 || index >= WORR_NATIVE_EVENT_SENDER_TX_CAPACITY)
        return NULL;
    payload = &payloads[index];
    if ((payload->state_flags & WORR_NATIVE_EVENT_SENDER_PAYLOAD_OCCUPIED) == 0 ||
        payload->handle != handle || payload->generation != generation)
        return NULL;
    if (index_out != NULL)
        *index_out = index;
    return payload;
}

static const worr_native_event_sender_payload_v1 *payload_find_const(
    const worr_native_event_sender_payload_v1 *payloads, uint32_t handle)
{
    return payload_find((worr_native_event_sender_payload_v1 *)payloads,
                        handle, NULL);
}

static bool payload_zero_except_generation(
    const worr_native_event_sender_payload_v1 *payload)
{
    worr_native_event_sender_payload_v1 expected;

    memset(&expected, 0, sizeof(expected));
    expected.generation = payload->generation;
    return memcmp(payload, &expected, sizeof(expected)) == 0;
}

static bool payload_decode_matches(
    const worr_native_event_sender_v1 *sender,
    const worr_native_event_sender_payload_v1 *payload)
{
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 record;

    if (payload->encoded_bytes == 0 ||
        payload->encoded_bytes > sizeof(payload->encoded))
        return false;

    if (payload->record.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
        payload->record.record_schema_version ==
            WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA) {
        worr_event_record_v1 events[WORR_NATIVE_EVENT_BATCH_MAX_EVENTS];
        worr_native_event_batch_info_v1 batch;
        return (sender->descriptor.flags &
                WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2) != 0 &&
               Worr_NativeEventBatchDecodeV1(
                   payload->encoded, payload->encoded_bytes,
                   sender->max_entities, events,
                   WORR_NATIVE_EVENT_BATCH_MAX_EVENTS, &batch) ==
                   WORR_NATIVE_EVENT_BATCH_OK &&
               batch.stream_epoch == payload->record.object_epoch &&
               batch.last_sequence == payload->record.object_sequence;
    }

    if (Worr_NativeCodecInspectV1(payload->encoded, payload->encoded_bytes,
                                  &info) != WORR_NATIVE_CODEC_OK ||
        !Worr_NativeCodecInfoRecordRefV1(&info, &record) ||
        !record_ref_equal(record, payload->record))
        return false;

    if (record.record_class == WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1) {
        worr_event_stream_descriptor_v1 descriptor;
        return Worr_NativeCodecEventStreamDecodeV1(
                   payload->encoded, payload->encoded_bytes, &descriptor) ==
                   WORR_NATIVE_CODEC_OK &&
               Worr_EventStreamDescriptorEqualV1(&descriptor,
                                                 &sender->descriptor);
    }
    if (record.record_class == WORR_NATIVE_RECORD_EVENT_V1) {
        worr_event_record_v1 event;
        return Worr_NativeCodecEventDecodeV1(
                   payload->encoded, payload->encoded_bytes,
                   sender->max_entities, &event) == WORR_NATIVE_CODEC_OK &&
               event.event_id.stream_epoch == record.object_epoch &&
               event.event_id.sequence == record.object_sequence;
    }
    return false;
}

static uint32_t payload_logical_event_count(
    const worr_native_event_sender_payload_v1 *payload)
{
    worr_native_event_batch_info_v1 batch;

    if (payload == NULL ||
        payload->record.record_class != WORR_NATIVE_RECORD_EVENT_V1)
        return 0;
    if (payload->record.record_schema_version == WORR_EVENT_ABI_VERSION)
        return 1;
    if (payload->record.record_schema_version !=
            WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA ||
        Worr_NativeEventBatchInspectV1(
            payload->encoded, payload->encoded_bytes, &batch) !=
            WORR_NATIVE_EVENT_BATCH_OK ||
        batch.stream_epoch != payload->record.object_epoch ||
        batch.last_sequence != payload->record.object_sequence)
        return 0;
    return batch.event_count;
}

static bool retained_event_window_room(
    const worr_native_event_sender_v1 *sender, uint32_t *room_out)
{
    uint32_t floor_sequence = 0;
    uint32_t index;

    if (sender == NULL || room_out == NULL)
        return false;
    for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
        const worr_native_event_sender_payload_v1 *payload =
            &sender->payloads[index];
        uint32_t first_sequence;

        if ((payload->state_flags &
             WORR_NATIVE_EVENT_SENDER_PAYLOAD_OCCUPIED) == 0 ||
            payload->record.record_class != WORR_NATIVE_RECORD_EVENT_V1)
            continue;
        if (payload->record.record_schema_version ==
            WORR_EVENT_ABI_VERSION) {
            first_sequence = payload->record.object_sequence;
        } else if (payload->record.record_schema_version ==
                   WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA) {
            worr_native_event_batch_info_v1 batch;
            if (Worr_NativeEventBatchInspectV1(
                    payload->encoded, payload->encoded_bytes, &batch) !=
                    WORR_NATIVE_EVENT_BATCH_OK ||
                batch.last_sequence != payload->record.object_sequence)
                return false;
            first_sequence = batch.first_sequence;
        } else {
            return false;
        }
        if (floor_sequence == 0 || first_sequence < floor_sequence)
            floor_sequence = first_sequence;
    }
    if (floor_sequence == 0) {
        *room_out = WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY;
        return true;
    }
    if (floor_sequence > sender->next_event_sequence)
        return false;
    {
        const uint64_t distance =
            (uint64_t)sender->next_event_sequence - floor_sequence;
        if (distance > WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY)
            return false;
        *room_out = (uint32_t)(WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY -
                               distance);
    }
    return true;
}

static bool payloads_validate(const worr_native_event_sender_v1 *sender)
{
    uint16_t occupied = 0;
    uint16_t retired = 0;
    uint32_t index;

    for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
        const worr_native_event_sender_payload_v1 *payload =
            &sender->payloads[index];
        const uint16_t known =
            WORR_NATIVE_EVENT_SENDER_PAYLOAD_OCCUPIED |
            WORR_NATIVE_EVENT_SENDER_PAYLOAD_RETIRED;

        if ((payload->state_flags & ~known) != 0)
            return false;
        if ((payload->state_flags &
             WORR_NATIVE_EVENT_SENDER_PAYLOAD_OCCUPIED) != 0) {
            if ((payload->state_flags &
                 WORR_NATIVE_EVENT_SENDER_PAYLOAD_RETIRED) != 0 ||
                payload->generation == 0 ||
                payload->generation >
                    WORR_NATIVE_EVENT_SENDER_PAYLOAD_GENERATION_MAX ||
                payload->handle != payload_handle(payload->generation, index) ||
                !payload_decode_matches(sender, payload))
                return false;
            ++occupied;
        } else if ((payload->state_flags &
                    WORR_NATIVE_EVENT_SENDER_PAYLOAD_RETIRED) != 0) {
            if (payload->generation !=
                    WORR_NATIVE_EVENT_SENDER_PAYLOAD_GENERATION_MAX ||
                !payload_zero_except_generation(payload))
                return false;
            ++retired;
        } else if (payload->generation >=
                       WORR_NATIVE_EVENT_SENDER_PAYLOAD_GENERATION_MAX ||
                   !payload_zero_except_generation(payload)) {
            return false;
        }
    }
    return occupied == sender->payload_occupied &&
           retired == sender->payload_retired &&
           (uint32_t)occupied + retired <=
               WORR_NATIVE_EVENT_SENDER_TX_CAPACITY;
}

static bool backlog_validate(const worr_native_event_sender_v1 *sender)
{
    uint32_t offset;

    if (sender->backlog_head >= WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY ||
        sender->backlog_count > WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY)
        return false;
    for (offset = 0; offset < sender->backlog_count; ++offset) {
        const uint32_t index =
            (sender->backlog_head + offset) %
            WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY;
        if (!Worr_EventRecordCandidateValidateV1(&sender->backlog[index],
                                                 sender->max_entities))
            return false;
    }
    return true;
}

static bool telemetry_validate(
    const worr_native_event_sender_telemetry_v1 *telemetry)
{
    const uint64_t batches = telemetry->schema2_batches_promoted;
    const uint64_t events = telemetry->schema2_events_promoted;
    const uint64_t minimum_batches =
        events / WORR_NATIVE_EVENT_BATCH_MAX_EVENTS +
        (events % WORR_NATIVE_EVENT_BATCH_MAX_EVENTS != 0 ? 1u : 0u);

    if (events > telemetry->candidates_promoted ||
        ((batches == 0) != (events == 0)) ||
        batches < minimum_batches)
        return false;
    /* Once the event counter saturates, further valid batches may advance
     * the batch counter without preserving the two-events-per-batch ratio. */
    return events == UINT64_MAX ||
           batches <= events / WORR_NATIVE_EVENT_BATCH_MIN_EVENTS;
}

bool Worr_NativeEventSenderValidateV1(
    const worr_native_event_sender_v1 *sender)
{
    const uint16_t known =
        WORR_NATIVE_EVENT_SENDER_INITIALIZED |
        WORR_NATIVE_EVENT_SENDER_DESCRIPTOR_ACKED |
        WORR_NATIVE_EVENT_SENDER_RETIRED |
        WORR_NATIVE_EVENT_SENDER_SEQUENCE_EXHAUSTED |
        WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED |
        WORR_NATIVE_EVENT_SENDER_CANCELLED;
    const uint32_t required_caps =
        WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
    const bool prepared =
        sender != NULL &&
        (sender->state_flags &
         WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED) != 0;
    const bool cancelled =
        sender != NULL &&
        (sender->state_flags & WORR_NATIVE_EVENT_SENDER_CANCELLED) != 0;
    uint32_t index;
    uint32_t logical_window_room;
    uint16_t descriptor_payloads = 0;

    if (sender == NULL || sender->struct_size != sizeof(*sender) ||
        sender->schema_version != WORR_NATIVE_EVENT_SENDER_ABI_VERSION ||
        (sender->state_flags & ~known) != 0 ||
        (sender->state_flags & WORR_NATIVE_EVENT_SENDER_INITIALIZED) == 0 ||
        !Worr_NativeSessionBindingValidateV1(&sender->binding) ||
        (sender->binding.negotiated_capabilities & required_caps) !=
            required_caps ||
        !Worr_EventStreamDescriptorValidateV1(&sender->descriptor) ||
        sender->max_entities == 0 ||
        sender->max_entities > WORR_EVENT_STREAM_MAX_ENTITIES_V1 ||
        sender->max_datagram_bytes <=
            WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ||
        sender->next_payload_slot >= WORR_NATIVE_EVENT_SENDER_TX_CAPACITY ||
        sender->reserved0 != 0 || sender->reserved1 != 0 ||
        sender->next_event_sequence < sender->descriptor.first_sequence ||
        ((sender->state_flags &
          WORR_NATIVE_EVENT_SENDER_SEQUENCE_EXHAUSTED) != 0 &&
         sender->next_event_sequence != UINT32_MAX) ||
        ((sender->state_flags & WORR_NATIVE_EVENT_SENDER_RETIRED) != 0 &&
         (prepared || sender->backlog_count != 0)) ||
        (cancelled &&
         (((sender->state_flags & WORR_NATIVE_EVENT_SENDER_RETIRED) == 0) ||
          sender->tx.retained_count != 0 || sender->payload_occupied != 0 ||
          (sender->tx_gate.state_flags &
           WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0)) ||
         !Worr_NativeTxSessionValidateV1(
             &sender->tx, sender->tx_slots,
             WORR_NATIVE_EVENT_SENDER_TX_CAPACITY) ||
         !combined_event_sequence_space_valid(sender) ||
        !Worr_NativeCarrierTxGateValidateV1(&sender->tx_gate) ||
        sender->tx.transport_epoch != sender->binding.transport_epoch ||
        sender->tx.connection_owner_id != sender->binding.connection_owner_id ||
        sender->tx_gate.transport_epoch != sender->binding.transport_epoch ||
        sender->tx_gate.connection_owner_id !=
            sender->binding.connection_owner_id ||
        (cancelled !=
         ((sender->tx.state_flags &
           WORR_NATIVE_TX_TERMINAL_CANCELLED) != 0)) ||
        sender->tx.retained_count != sender->payload_occupied ||
        !payloads_validate(sender) || !backlog_validate(sender) ||
        !retained_event_window_room(sender, &logical_window_room) ||
        !telemetry_validate(&sender->telemetry))
        return false;

    for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
        if ((sender->payloads[index].state_flags &
             WORR_NATIVE_EVENT_SENDER_PAYLOAD_OCCUPIED) != 0 &&
            sender->payloads[index].record.record_class ==
                WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1)
            ++descriptor_payloads;
    }
    if (cancelled) {
        if (descriptor_payloads != 0)
            return false;
    } else if ((sender->state_flags &
                WORR_NATIVE_EVENT_SENDER_DESCRIPTOR_ACKED) != 0) {
        if (descriptor_payloads != 0)
            return false;
    } else if (descriptor_payloads != 1 || sender->tx.retained_count != 1) {
        return false;
    }

    if (prepared) {
        if ((sender->tx_gate.state_flags &
             WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) == 0 ||
            sender->mixed_token.struct_size != sizeof(sender->mixed_token) ||
            sender->mixed_token.schema_version !=
                WORR_NATIVE_CARRIER_MIXED_ABI_VERSION ||
            sender->mixed_token.dispatch_token_id != sender->dispatch.token_id)
            return false;
    } else if ((sender->tx_gate.state_flags &
                WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0 ||
               memcmp(&sender->mixed_token,
                      &(worr_native_carrier_mixed_token_v1){0},
                      sizeof(sender->mixed_token)) != 0) {
        return false;
    }
    return true;
}

static bool payload_select(const worr_native_event_sender_v1 *sender,
                           uint32_t *index_out, uint32_t *handle_out)
{
    uint32_t offset;

    for (offset = 0; offset < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++offset) {
        const uint32_t index =
            (sender->next_payload_slot + offset) %
            WORR_NATIVE_EVENT_SENDER_TX_CAPACITY;
        const worr_native_event_sender_payload_v1 *payload =
            &sender->payloads[index];
        if (payload->state_flags == 0) {
            const uint32_t generation = payload->generation + 1u;
            if (generation == 0 || generation >
                                       WORR_NATIVE_EVENT_SENDER_PAYLOAD_GENERATION_MAX)
                return false;
            *index_out = index;
            *handle_out = payload_handle(generation, index);
            return true;
        }
    }
    return false;
}

static void payload_install(worr_native_event_sender_v1 *sender,
                            uint32_t index, uint32_t handle,
                            worr_native_record_ref_v1 record,
                            const uint8_t *encoded, uint16_t encoded_bytes)
{
    worr_native_event_sender_payload_v1 payload;

    memset(&payload, 0, sizeof(payload));
    payload.handle = handle;
    payload.generation =
        handle >> WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_BITS;
    payload.record = record;
    payload.encoded_bytes = encoded_bytes;
    payload.state_flags = WORR_NATIVE_EVENT_SENDER_PAYLOAD_OCCUPIED;
    memcpy(payload.encoded, encoded, encoded_bytes);
    sender->payloads[index] = payload;
    ++sender->payload_occupied;
    sender->next_payload_slot =
        (uint16_t)((index + 1u) % WORR_NATIVE_EVENT_SENDER_TX_CAPACITY);
}

static bool payload_release(worr_native_event_sender_payload_v1 *payloads,
                            uint16_t *occupied, uint16_t *retired,
                            uint16_t *next_slot, uint32_t handle)
{
    uint32_t index;
    uint32_t generation;
    worr_native_event_sender_payload_v1 *payload =
        payload_find(payloads, handle, &index);

    if (payload == NULL || *occupied == 0)
        return false;
    generation = payload->generation;
    memset(payload, 0, sizeof(*payload));
    payload->generation = generation;
    --*occupied;
    if (generation == WORR_NATIVE_EVENT_SENDER_PAYLOAD_GENERATION_MAX) {
        payload->state_flags = WORR_NATIVE_EVENT_SENDER_PAYLOAD_RETIRED;
        ++*retired;
    } else {
        *next_slot = (uint16_t)index;
    }
    return true;
}

static uint8_t event_priority(const worr_event_record_v1 *event)
{
    if ((event->flags & WORR_EVENT_FLAG_CRITICAL) != 0 ||
        event->delivery_class == WORR_EVENT_DELIVERY_PERSISTENT_STATE)
        return 1;
    if (event->delivery_class == WORR_EVENT_DELIVERY_RELIABLE_ORDERED)
        return 2;
    if (event->delivery_class == WORR_EVENT_DELIVERY_TRANSIENT)
        return 4;
    return 6;
}

static worr_native_event_sender_result_v1 mixed_result(
    worr_native_carrier_mixed_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_CARRIER_MIXED_OK:
    case WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED:
    case WORR_NATIVE_CARRIER_MIXED_DISPATCH_RETIRED:
        return WORR_NATIVE_EVENT_SENDER_OK;
    case WORR_NATIVE_CARRIER_MIXED_NOT_DUE:
        return WORR_NATIVE_EVENT_SENDER_NOT_DUE;
    case WORR_NATIVE_CARRIER_MIXED_LIMIT:
        return WORR_NATIVE_EVENT_SENDER_CAPACITY;
    case WORR_NATIVE_CARRIER_MIXED_OUTPUT_TOO_SMALL:
        return WORR_NATIVE_EVENT_SENDER_OUTPUT_TOO_SMALL;
    case WORR_NATIVE_CARRIER_MIXED_WRONG_EPOCH:
        return WORR_NATIVE_EVENT_SENDER_WRONG_EPOCH;
    case WORR_NATIVE_CARRIER_MIXED_CLOCK_REGRESSION:
        return WORR_NATIVE_EVENT_SENDER_CLOCK_REGRESSION;
    case WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT:
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    case WORR_NATIVE_CARRIER_MIXED_INVALID_STATE:
    case WORR_NATIVE_CARRIER_MIXED_PACKET_PENDING:
    case WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION:
    case WORR_NATIVE_CARRIER_MIXED_TOKEN_EXHAUSTED:
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    default:
        return WORR_NATIVE_EVENT_SENDER_TRANSPORT_REJECTED;
    }
}

worr_native_event_sender_result_v1 Worr_NativeEventSenderInitV1(
    worr_native_event_sender_v1 *sender,
    const worr_native_session_binding_v1 *binding,
    const worr_event_stream_descriptor_v1 *descriptor,
    uint32_t max_entities, uint16_t max_datagram_bytes, uint64_t now_tick)
{
    worr_native_tx_session_v1 tx;
    worr_native_tx_slot_v1 tx_slots[WORR_NATIVE_EVENT_SENDER_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 tx_gate;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 record;
    uint8_t encoded[WORR_NATIVE_EVENT_SENDER_MAX_ENCODED_BYTES];
    size_t encoded_bytes = 0;
    uint32_t message_sequence;
    const uint32_t required_caps =
        WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;

    if (sender == NULL || binding == NULL || descriptor == NULL ||
        ranges_overlap(sender, sizeof(*sender), binding, sizeof(*binding)) ||
        ranges_overlap(sender, sizeof(*sender), descriptor,
                       sizeof(*descriptor)))
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeSessionBindingValidateV1(binding) ||
        (binding->negotiated_capabilities & required_caps) != required_caps ||
        !Worr_EventStreamDescriptorValidateV1(descriptor) ||
        max_entities == 0 || max_entities > WORR_EVENT_STREAM_MAX_ENTITIES_V1 ||
        max_datagram_bytes <= WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES)
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    if (Worr_NativeCodecEventStreamEncodeV1(
            descriptor, encoded, sizeof(encoded), &encoded_bytes) !=
            WORR_NATIVE_CODEC_OK ||
        encoded_bytes > UINT16_MAX ||
        Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info) !=
            WORR_NATIVE_CODEC_OK ||
        !Worr_NativeCodecInfoRecordRefV1(&info, &record))
        return WORR_NATIVE_EVENT_SENDER_INVALID_RECORD;

    memset(&tx, 0, sizeof(tx));
    memset(tx_slots, 0, sizeof(tx_slots));
    memset(&tx_gate, 0, sizeof(tx_gate));
    if (!Worr_NativeTxSessionInitV1(
            &tx, tx_slots, WORR_NATIVE_EVENT_SENDER_TX_CAPACITY, binding) ||
        !Worr_NativeCarrierTxGateInitV1(&tx_gate, binding) ||
        Worr_NativeTxSessionEnqueueV1(
            &tx, tx_slots, WORR_NATIVE_EVENT_SENDER_TX_CAPACITY, record, 0,
            payload_handle(1, 0), (uint32_t)encoded_bytes,
            minimum_datagram(max_datagram_bytes, encoded_bytes),
            now_tick, &message_sequence) !=
            WORR_NATIVE_TX_RETAINED)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;

    memset(sender, 0, sizeof(*sender));
    sender->struct_size = sizeof(*sender);
    sender->schema_version = WORR_NATIVE_EVENT_SENDER_ABI_VERSION;
    sender->state_flags = WORR_NATIVE_EVENT_SENDER_INITIALIZED;
    sender->binding = *binding;
    sender->descriptor = *descriptor;
    sender->next_event_sequence = descriptor->first_sequence;
    sender->max_entities = max_entities;
    sender->max_datagram_bytes = max_datagram_bytes;
    sender->tx = tx;
    memcpy(sender->tx_slots, tx_slots, sizeof(tx_slots));
    sender->tx_gate = tx_gate;
    payload_install(sender, 0, payload_handle(1, 0), record, encoded,
                    (uint16_t)encoded_bytes);
    return Worr_NativeEventSenderValidateV1(sender)
               ? WORR_NATIVE_EVENT_SENDER_OK
               : WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
}

worr_native_event_sender_result_v1 Worr_NativeEventSenderQueueCandidatesV1(
    worr_native_event_sender_v1 *sender,
    const worr_event_record_v1 *candidates, uint32_t count)
{
    uint32_t offset;

    if (sender == NULL || candidates == NULL || count == 0 ||
        count > WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY ||
        ranges_overlap(sender, sizeof(*sender), candidates,
                       (size_t)count * sizeof(*candidates)))
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeEventSenderValidateV1(sender))
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    if ((sender->state_flags & WORR_NATIVE_EVENT_SENDER_RETIRED) != 0)
        return WORR_NATIVE_EVENT_SENDER_RETIRED_RESULT;
    if (count > WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY -
                    sender->backlog_count) {
        saturating_increment(&sender->telemetry.backlog_stalls);
        return WORR_NATIVE_EVENT_SENDER_CAPACITY;
    }
    for (offset = 0; offset < count; ++offset) {
        if (!Worr_EventRecordCandidateValidateV1(&candidates[offset],
                                                 sender->max_entities)) {
            saturating_increment(&sender->telemetry.validation_failures);
            return WORR_NATIVE_EVENT_SENDER_INVALID_RECORD;
        }
    }
    for (offset = 0; offset < count; ++offset) {
        const uint32_t index =
            (sender->backlog_head + sender->backlog_count + offset) %
            WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY;
        sender->backlog[index] = candidates[offset];
    }
    sender->backlog_count = (uint16_t)(sender->backlog_count + count);
    while (count-- != 0)
        saturating_increment(&sender->telemetry.candidates_queued);
    return WORR_NATIVE_EVENT_SENDER_QUEUED;
}

worr_native_event_sender_result_v1 Worr_NativeEventSenderPumpV1(
    worr_native_event_sender_v1 *sender, uint64_t now_tick,
    uint32_t *promoted_out)
{
    uint32_t promoted = 0;
    bool capacity_stall = false;

    if (sender == NULL || promoted_out == NULL ||
        ranges_overlap(sender, sizeof(*sender), promoted_out,
                       sizeof(*promoted_out)))
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeEventSenderValidateV1(sender))
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    if ((sender->state_flags & WORR_NATIVE_EVENT_SENDER_RETIRED) != 0)
        return WORR_NATIVE_EVENT_SENDER_RETIRED_RESULT;
    if ((sender->state_flags &
         WORR_NATIVE_EVENT_SENDER_DESCRIPTOR_ACKED) == 0) {
        *promoted_out = 0;
        return WORR_NATIVE_EVENT_SENDER_OK;
    }

    while (sender->backlog_count != 0) {
        worr_event_record_v1 events[WORR_NATIVE_EVENT_BATCH_MAX_EVENTS];
        worr_native_tx_session_v1 staged_tx;
        worr_native_tx_slot_v1
            staged_slots[WORR_NATIVE_EVENT_SENDER_TX_CAPACITY];
        worr_native_codec_info_v1 info;
        worr_native_event_batch_info_v1 batch_info;
        worr_native_record_ref_v1 record;
        uint8_t encoded[WORR_NATIVE_EVENT_SENDER_MAX_ENCODED_BYTES];
        size_t encoded_bytes = 0;
        uint32_t batch_count;
        uint32_t batch_index;
        uint32_t batch_last_sequence;
        uint32_t batch_wire_bytes;
        uint32_t logical_window_room;
        uint32_t payload_index;
        uint32_t handle;
        uint32_t message_sequence;
        uint8_t priority;
        worr_native_tx_result_v1 enqueued;

        if ((sender->state_flags &
             WORR_NATIVE_EVENT_SENDER_SEQUENCE_EXHAUSTED) != 0) {
            *promoted_out = promoted;
            return WORR_NATIVE_EVENT_SENDER_SEQUENCE_LIMIT;
        }
        if (sender->tx.retained_count == WORR_NATIVE_EVENT_SENDER_TX_CAPACITY ||
            !payload_select(sender, &payload_index, &handle)) {
            capacity_stall = true;
            break;
        }
        if (combined_binding(&sender->binding) &&
            sender->tx.next_message_sequence >
                WORR_NATIVE_COMBINED_EVENT_MESSAGE_SEQUENCE_LAST) {
            *promoted_out = promoted;
            return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
        }

        batch_count = 1;
        while ((sender->descriptor.flags &
                WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2) != 0 &&
               batch_count < sender->backlog_count &&
               batch_count < WORR_NATIVE_EVENT_BATCH_MAX_EVENTS &&
               batch_count <= UINT32_MAX - sender->next_event_sequence) {
            const uint32_t first_index = sender->backlog_head;
            const uint32_t next_index =
                (sender->backlog_head + batch_count) %
                WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY;
            if (sender->backlog[next_index].source_tick !=
                    sender->backlog[first_index].source_tick ||
                sender->backlog[next_index].source_time_us !=
                    sender->backlog[first_index].source_time_us)
                break;
            ++batch_count;
        }
        for (batch_index = 0; batch_index < batch_count; ++batch_index) {
            const uint32_t backlog_index =
                (sender->backlog_head + batch_index) %
                WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY;
            events[batch_index] = sender->backlog[backlog_index];
            events[batch_index].event_id.stream_epoch =
                sender->descriptor.stream_epoch;
            events[batch_index].event_id.sequence =
                sender->next_event_sequence + batch_index;
            events[batch_index].flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
        }

        /* A schema-2 group is useful only when its whole WNE image fits the
         * sender's frozen one-DATA datagram.  Shrink from the FIFO tail until
         * that invariant holds; a singleton always uses schema 1. */
        while (batch_count > 1) {
            batch_wire_bytes = WORR_NATIVE_EVENT_BATCH_WIRE_HEADER_BYTES;
            for (batch_index = 0; batch_index < batch_count; ++batch_index) {
                uint32_t nested_bytes = 0;
                if (Worr_NativeCodecEventPreflightV1(
                        &events[batch_index], sender->max_entities,
                        &nested_bytes) != WORR_NATIVE_CODEC_OK ||
                    batch_wire_bytes > UINT32_MAX - nested_bytes) {
                    saturating_increment(
                        &sender->telemetry.validation_failures);
                    *promoted_out = promoted;
                    return WORR_NATIVE_EVENT_SENDER_INVALID_RECORD;
                }
                batch_wire_bytes += nested_bytes;
            }
            if (batch_wire_bytes <=
                sender->max_datagram_bytes -
                    WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES)
                break;
            --batch_count;
        }
        if (!retained_event_window_room(sender, &logical_window_room)) {
            *promoted_out = promoted;
            return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
        }
        if (logical_window_room == 0) {
            capacity_stall = true;
            break;
        }
        if (batch_count > logical_window_room)
            batch_count = logical_window_room;
        batch_last_sequence = sender->next_event_sequence + batch_count - 1u;
        priority = WORR_NATIVE_ENVELOPE_MAX_PRIORITY;
        for (batch_index = 0; batch_index < batch_count; ++batch_index) {
            if (event_priority(&events[batch_index]) < priority)
                priority = event_priority(&events[batch_index]);
        }

        memset(&record, 0, sizeof(record));
        if (batch_count == 1) {
            if (!Worr_EventRecordValidateV1(&events[0],
                                            sender->max_entities) ||
                Worr_NativeCodecEventEncodeV1(
                    &events[0], sender->max_entities, encoded,
                    sizeof(encoded), &encoded_bytes) !=
                    WORR_NATIVE_CODEC_OK ||
                Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info) !=
                    WORR_NATIVE_CODEC_OK ||
                !Worr_NativeCodecInfoRecordRefV1(&info, &record)) {
                saturating_increment(&sender->telemetry.validation_failures);
                *promoted_out = promoted;
                return WORR_NATIVE_EVENT_SENDER_INVALID_RECORD;
            }
        } else {
            memset(&batch_info, 0, sizeof(batch_info));
            if (Worr_NativeEventBatchEncodeV1(
                    events, batch_count, sender->max_entities, encoded,
                    sizeof(encoded), &encoded_bytes, &batch_info) !=
                    WORR_NATIVE_EVENT_BATCH_OK ||
                batch_info.stream_epoch != sender->descriptor.stream_epoch ||
                batch_info.first_sequence != sender->next_event_sequence ||
                batch_info.last_sequence != batch_last_sequence) {
                saturating_increment(&sender->telemetry.validation_failures);
                *promoted_out = promoted;
                return WORR_NATIVE_EVENT_SENDER_INVALID_RECORD;
            }
            record.record_class = WORR_NATIVE_RECORD_EVENT_V1;
            record.record_schema_version =
                WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA;
            record.object_epoch = batch_info.stream_epoch;
            record.object_sequence = batch_info.last_sequence;
        }
        if (encoded_bytes > UINT16_MAX ||
            !Worr_NativeEnvelopeRecordRefValidV1(record)) {
            saturating_increment(&sender->telemetry.validation_failures);
            *promoted_out = promoted;
            return WORR_NATIVE_EVENT_SENDER_INVALID_RECORD;
        }

        staged_tx = sender->tx;
        memcpy(staged_slots, sender->tx_slots, sizeof(staged_slots));
        enqueued = Worr_NativeTxSessionEnqueueV1(
            &staged_tx, staged_slots, WORR_NATIVE_EVENT_SENDER_TX_CAPACITY,
            record, priority, handle, (uint32_t)encoded_bytes,
            minimum_datagram(sender->max_datagram_bytes, encoded_bytes),
            now_tick, &message_sequence);
        if (enqueued == WORR_NATIVE_TX_CAPACITY ||
            enqueued == WORR_NATIVE_TX_RECEIPT_WINDOW) {
            capacity_stall = true;
            break;
        }
        if (enqueued != WORR_NATIVE_TX_RETAINED) {
            *promoted_out = promoted;
            return enqueued == WORR_NATIVE_TX_CLOCK_REGRESSION
                       ? WORR_NATIVE_EVENT_SENDER_CLOCK_REGRESSION
                       : WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
        }

        sender->tx = staged_tx;
        memcpy(sender->tx_slots, staged_slots, sizeof(staged_slots));
        payload_install(sender, payload_index, handle, record, encoded,
                        (uint16_t)encoded_bytes);
        for (batch_index = 0; batch_index < batch_count; ++batch_index) {
            memset(&sender->backlog[sender->backlog_head], 0,
                   sizeof(sender->backlog[sender->backlog_head]));
            sender->backlog_head = (uint16_t)(
                (sender->backlog_head + 1u) %
                WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY);
            --sender->backlog_count;
        }
        promoted += batch_count;
        saturating_add(&sender->telemetry.candidates_promoted, batch_count);
        if (batch_count > 1) {
            saturating_increment(
                &sender->telemetry.schema2_batches_promoted);
            saturating_add(
                &sender->telemetry.schema2_events_promoted,
                batch_count);
        }
        if (batch_last_sequence == UINT32_MAX) {
            sender->next_event_sequence = UINT32_MAX;
            sender->state_flags |=
                WORR_NATIVE_EVENT_SENDER_SEQUENCE_EXHAUSTED;
        } else {
            sender->next_event_sequence = batch_last_sequence + 1u;
        }
    }

    if (capacity_stall)
        saturating_increment(&sender->telemetry.tx_capacity_stalls);
    *promoted_out = promoted;
    return promoted != 0 ? WORR_NATIVE_EVENT_SENDER_PROMOTED
                         : (capacity_stall
                                ? WORR_NATIVE_EVENT_SENDER_CAPACITY
                                : WORR_NATIVE_EVENT_SENDER_OK);
}

worr_native_event_sender_result_v1 Worr_NativeEventSenderApplyAcksV1(
    worr_native_event_sender_v1 *sender, const void *packet,
    size_t packet_bytes, uint32_t *acknowledged_out)
{
    worr_native_tx_session_v1 staged_tx;
    worr_native_tx_slot_v1 staged_slots[WORR_NATIVE_EVENT_SENDER_TX_CAPACITY];
    worr_native_event_sender_payload_v1
        staged_payloads[WORR_NATIVE_EVENT_SENDER_TX_CAPACITY];
    uint16_t occupied;
    uint16_t retired;
    uint16_t next_slot;
    uint32_t acknowledged = 0;
    uint32_t descriptor_acks = 0;
    uint32_t event_payload_acks = 0;
    uint32_t logical_event_acks = 0;
    uint32_t index;

    if (sender == NULL || packet == NULL || packet_bytes == 0 ||
        acknowledged_out == NULL ||
        ranges_overlap(sender, sizeof(*sender), packet, packet_bytes) ||
        ranges_overlap(sender, sizeof(*sender), acknowledged_out,
                       sizeof(*acknowledged_out)) ||
        ranges_overlap(packet, packet_bytes, acknowledged_out,
                       sizeof(*acknowledged_out)))
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeEventSenderValidateV1(sender))
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    if ((sender->state_flags & WORR_NATIVE_EVENT_SENDER_CANCELLED) != 0)
        return WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT;

    staged_tx = sender->tx;
    memcpy(staged_slots, sender->tx_slots, sizeof(staged_slots));
    memcpy(staged_payloads, sender->payloads, sizeof(staged_payloads));
    occupied = sender->payload_occupied;
    retired = sender->payload_retired;
    next_slot = sender->next_payload_slot;
    if (Worr_NativeCarrierSessionApplyAcksV1(
            &staged_tx, staged_slots, WORR_NATIVE_EVENT_SENDER_TX_CAPACITY,
            packet, packet_bytes, &acknowledged) !=
            WORR_NATIVE_CARRIER_SESSION_OK)
        return WORR_NATIVE_EVENT_SENDER_TRANSPORT_REJECTED;
    if (acknowledged > sender->tx.retained_count)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;

    for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
        const bool was_occupied =
            (sender->tx_slots[index].state_flags &
             WORR_NATIVE_TX_SLOT_OCCUPIED) != 0;
        const bool still_occupied =
            (staged_slots[index].state_flags &
             WORR_NATIVE_TX_SLOT_OCCUPIED) != 0;
        if (!was_occupied)
            continue;
        if (still_occupied) {
            if (memcmp(&sender->tx_slots[index], &staged_slots[index],
                       sizeof(staged_slots[index])) != 0)
                return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
            continue;
        }
        if (!payload_release(staged_payloads, &occupied, &retired, &next_slot,
                             sender->tx_slots[index].payload_handle))
            return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
        if (sender->tx_slots[index].record.record_class ==
            WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1)
            ++descriptor_acks;
        else if (sender->tx_slots[index].record.record_class ==
                 WORR_NATIVE_RECORD_EVENT_V1) {
            const worr_native_event_sender_payload_v1 *payload =
                payload_find_const(
                    sender->payloads,
                    sender->tx_slots[index].payload_handle);
            const uint32_t logical_count =
                payload_logical_event_count(payload);
            if (logical_count == 0 ||
                logical_event_acks > UINT32_MAX - logical_count)
                return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
            ++event_payload_acks;
            logical_event_acks += logical_count;
        }
        else
            return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    }
    if (descriptor_acks > 1 ||
        descriptor_acks + event_payload_acks != acknowledged)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;

    sender->tx = staged_tx;
    memcpy(sender->tx_slots, staged_slots, sizeof(staged_slots));
    memcpy(sender->payloads, staged_payloads, sizeof(staged_payloads));
    sender->payload_occupied = occupied;
    sender->payload_retired = retired;
    sender->next_payload_slot = next_slot;
    if (descriptor_acks != 0) {
        sender->state_flags |= WORR_NATIVE_EVENT_SENDER_DESCRIPTOR_ACKED;
        saturating_increment(&sender->telemetry.descriptors_acknowledged);
    }
    saturating_add(&sender->telemetry.events_acknowledged,
                   logical_event_acks);
    *acknowledged_out = acknowledged;
    return WORR_NATIVE_EVENT_SENDER_ACK_APPLIED;
}

worr_native_event_sender_result_v1 Worr_NativeEventSenderDataDuePeekV1(
    const worr_native_event_sender_v1 *sender, uint64_t now_tick,
    uint32_t resend_interval_ticks, bool *due_out)
{
    worr_native_tx_send_ticket_v1 ticket;
    worr_native_tx_result_v1 selected;

    if (sender == NULL || due_out == NULL ||
        ranges_overlap(sender, sizeof(*sender), due_out, sizeof(*due_out)))
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeEventSenderValidateV1(sender))
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    if ((sender->state_flags & WORR_NATIVE_EVENT_SENDER_RETIRED) != 0) {
        *due_out = false;
        return WORR_NATIVE_EVENT_SENDER_RETIRED_RESULT;
    }
    if (now_tick < sender->tx.last_tick)
        return WORR_NATIVE_EVENT_SENDER_CLOCK_REGRESSION;
    if ((sender->state_flags &
         WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED) != 0) {
        *due_out = false;
        return WORR_NATIVE_EVENT_SENDER_OK;
    }
    if ((sender->tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0) {
        *due_out = true;
        return WORR_NATIVE_EVENT_SENDER_OK;
    }
    if ((sender->state_flags &
         WORR_NATIVE_EVENT_SENDER_DESCRIPTOR_ACKED) != 0 &&
        sender->backlog_count != 0) {
        uint32_t logical_window_room;
        if ((sender->state_flags &
             WORR_NATIVE_EVENT_SENDER_SEQUENCE_EXHAUSTED) != 0)
            return WORR_NATIVE_EVENT_SENDER_SEQUENCE_LIMIT;
        if (!retained_event_window_room(sender, &logical_window_room))
            return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
        if (logical_window_room != 0 &&
            (uint32_t)sender->payload_occupied + sender->payload_retired <
                WORR_NATIVE_EVENT_SENDER_TX_CAPACITY &&
            sender->tx.retained_count <
                WORR_NATIVE_EVENT_SENDER_TX_CAPACITY) {
            /* The prepare path pumps at the actual TX-selection boundary.
             * Make an unpromoted FIFO visible to the non-mutating eligibility
             * query so capture sites can coalesce one complete fence first. */
            *due_out = true;
            return WORR_NATIVE_EVENT_SENDER_OK;
        }
    }

    memset(&ticket, 0, sizeof(ticket));
    selected = Worr_NativeTxSessionPrepareDueV1(
        &sender->tx, sender->tx_slots, WORR_NATIVE_EVENT_SENDER_TX_CAPACITY,
        now_tick, resend_interval_ticks, &ticket);
    if (selected == WORR_NATIVE_TX_SELECTED) {
        *due_out = true;
        return WORR_NATIVE_EVENT_SENDER_OK;
    }
    if (selected == WORR_NATIVE_TX_NOT_DUE) {
        *due_out = false;
        return WORR_NATIVE_EVENT_SENDER_NOT_DUE;
    }
    return selected == WORR_NATIVE_TX_CLOCK_REGRESSION
               ? WORR_NATIVE_EVENT_SENDER_CLOCK_REGRESSION
               : WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
}

worr_native_event_sender_result_v1 Worr_NativeEventSenderPrepareMixedV1(
    worr_native_event_sender_v1 *sender,
    worr_native_carrier_ack_ledger_v1 *ack_ledger, uint64_t now_tick,
    uint32_t resend_interval_ticks, uint32_t ack_retry_interval_ticks,
    uint16_t application_packet_budget, const void *legacy_packet,
    uint16_t legacy_bytes, void *packet_out, size_t packet_capacity,
    size_t *packet_bytes_out, uint64_t *token_out)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_mixed_token_v1 token;
    const worr_native_event_sender_payload_v1 *payload;
    worr_native_carrier_mixed_result_v1 result;
    uint32_t promoted;
    bool began = false;

    if (sender == NULL || ack_ledger == NULL || packet_out == NULL ||
        packet_bytes_out == NULL || token_out == NULL ||
        (legacy_bytes != 0 && legacy_packet == NULL) ||
        ranges_overlap(sender, sizeof(*sender), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(sender, sizeof(*sender), packet_out, packet_capacity) ||
        ranges_overlap(sender, sizeof(*sender), packet_bytes_out,
                       sizeof(*packet_bytes_out)) ||
        ranges_overlap(sender, sizeof(*sender), token_out,
                       sizeof(*token_out)) ||
        ranges_overlap(ack_ledger, sizeof(*ack_ledger), token_out,
                       sizeof(*token_out)) ||
        ranges_overlap(packet_out, packet_capacity, token_out,
                       sizeof(*token_out)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out),
                       token_out, sizeof(*token_out)))
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeEventSenderValidateV1(sender) ||
        !Worr_NativeCarrierAckLedgerValidateV1(ack_ledger))
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    if ((sender->state_flags & WORR_NATIVE_EVENT_SENDER_RETIRED) != 0)
        return WORR_NATIVE_EVENT_SENDER_RETIRED_RESULT;
    if ((sender->state_flags &
         WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED) != 0)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;

    {
        const worr_native_event_sender_result_v1 pumped =
            Worr_NativeEventSenderPumpV1(sender, now_tick, &promoted);
        if (pumped != WORR_NATIVE_EVENT_SENDER_OK &&
            pumped != WORR_NATIVE_EVENT_SENDER_PROMOTED &&
            pumped != WORR_NATIVE_EVENT_SENDER_CAPACITY &&
            pumped != WORR_NATIVE_EVENT_SENDER_SEQUENCE_LIMIT)
            return pumped;
    }
    if (sender->tx.retained_count == 0)
        return WORR_NATIVE_EVENT_SENDER_NOT_DUE;

    staged_gate = sender->tx_gate;
    staged_dispatch = sender->dispatch;
    staged_ledger = *ack_ledger;
    if ((staged_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) == 0) {
        result = Worr_NativeCarrierMixedBeginV1(
            &staged_gate, &sender->tx, sender->tx_slots,
            WORR_NATIVE_EVENT_SENDER_TX_CAPACITY, now_tick,
            resend_interval_ticks, application_packet_budget, legacy_bytes,
            &staged_dispatch);
        if (result != WORR_NATIVE_CARRIER_MIXED_OK)
            return mixed_result(result);
        began = true;
    }

    payload = payload_find_const(
        sender->payloads,
        staged_dispatch.send_ticket.pre_send_slot.payload_handle);
    if (payload == NULL)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    if (began &&
        Worr_NativeCarrierSessionDispatchBindPayloadV1(
            &staged_gate, &staged_dispatch, payload->handle, payload->encoded,
            payload->encoded_bytes) != WORR_NATIVE_CARRIER_SESSION_OK)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;

    memset(&token, 0, sizeof(token));
    result = Worr_NativeCarrierMixedPreparePacketV1(
        &staged_gate, &sender->tx, sender->tx_slots,
        WORR_NATIVE_EVENT_SENDER_TX_CAPACITY, &staged_dispatch,
        &staged_ledger, now_tick, ack_retry_interval_ticks, payload->handle,
        payload->encoded, payload->encoded_bytes, legacy_packet, legacy_bytes,
        packet_out, packet_capacity, packet_bytes_out, &token);
    if (result != WORR_NATIVE_CARRIER_MIXED_OK)
        return mixed_result(result);

    sender->tx_gate = staged_gate;
    sender->dispatch = staged_dispatch;
    sender->mixed_token = token;
    sender->state_flags |= WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED;
    *ack_ledger = staged_ledger;
    *token_out = token.dispatch_token_id;
    saturating_increment(&sender->telemetry.packets_prepared);
    return WORR_NATIVE_EVENT_SENDER_OK;
}

worr_native_event_sender_result_v1 Worr_NativeEventSenderConfirmMixedV1(
    worr_native_event_sender_v1 *sender,
    worr_native_carrier_ack_ledger_v1 *ack_ledger, uint64_t handoff_tick,
    const void *packet, size_t packet_bytes)
{
    worr_native_tx_session_v1 staged_tx;
    worr_native_tx_slot_v1 staged_slots[WORR_NATIVE_EVENT_SENDER_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_mixed_result_v1 result;
    const worr_native_event_sender_payload_v1 *prepared_payload;
    uint32_t telemetry_units;
    const bool retry = sender != NULL &&
        sender->dispatch.send_ticket.pre_send_slot.send_attempts != 0;

    if (sender == NULL || ack_ledger == NULL || packet == NULL ||
        packet_bytes == 0 ||
        ranges_overlap(sender, sizeof(*sender), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(sender, sizeof(*sender), packet, packet_bytes) ||
        ranges_overlap(ack_ledger, sizeof(*ack_ledger), packet,
                       packet_bytes))
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeEventSenderValidateV1(sender) ||
        (sender->state_flags &
         WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED) == 0)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;

    prepared_payload = payload_find_const(
        sender->payloads,
        sender->dispatch.send_ticket.pre_send_slot.payload_handle);
    if (prepared_payload == NULL)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    telemetry_units =
        prepared_payload->record.record_class == WORR_NATIVE_RECORD_EVENT_V1
            ? payload_logical_event_count(prepared_payload)
            : 1u;
    if (telemetry_units == 0)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;

    staged_tx = sender->tx;
    memcpy(staged_slots, sender->tx_slots, sizeof(staged_slots));
    staged_gate = sender->tx_gate;
    staged_dispatch = sender->dispatch;
    staged_ledger = *ack_ledger;
    result = Worr_NativeCarrierMixedConfirmPacketV1(
        &staged_gate, &staged_tx, staged_slots,
        WORR_NATIVE_EVENT_SENDER_TX_CAPACITY, &staged_dispatch,
        &staged_ledger, &sender->mixed_token, handoff_tick, packet,
        packet_bytes);
    if (result != WORR_NATIVE_CARRIER_MIXED_OK &&
        result != WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED &&
        result != WORR_NATIVE_CARRIER_MIXED_DISPATCH_RETIRED)
        return mixed_result(result);

    sender->tx = staged_tx;
    memcpy(sender->tx_slots, staged_slots, sizeof(staged_slots));
    sender->tx_gate = staged_gate;
    sender->dispatch = staged_dispatch;
    *ack_ledger = staged_ledger;
    sender->state_flags &=
        (uint16_t)~WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED;
    memset(&sender->mixed_token, 0, sizeof(sender->mixed_token));
    saturating_increment(&sender->telemetry.packets_confirmed);
    if (result == WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED) {
        saturating_add(retry ? &sender->telemetry.retries
                             : &sender->telemetry.first_sends,
                       telemetry_units);
    }
    return WORR_NATIVE_EVENT_SENDER_OK;
}

worr_native_event_sender_result_v1 Worr_NativeEventSenderRejectMixedV1(
    worr_native_event_sender_v1 *sender,
    worr_native_carrier_ack_ledger_v1 *ack_ledger, const void *packet,
    size_t packet_bytes)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_mixed_result_v1 result;

    if (sender == NULL || ack_ledger == NULL || packet == NULL ||
        packet_bytes == 0 ||
        ranges_overlap(sender, sizeof(*sender), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(sender, sizeof(*sender), packet, packet_bytes) ||
        ranges_overlap(ack_ledger, sizeof(*ack_ledger), packet,
                       packet_bytes))
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeEventSenderValidateV1(sender) ||
        (sender->state_flags &
         WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED) == 0)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;

    staged_gate = sender->tx_gate;
    staged_dispatch = sender->dispatch;
    staged_ledger = *ack_ledger;
    result = Worr_NativeCarrierMixedRejectPacketV1(
        &staged_gate, &staged_dispatch, &staged_ledger,
        &sender->mixed_token, packet, packet_bytes);
    if (result != WORR_NATIVE_CARRIER_MIXED_OK)
        return mixed_result(result);
    result = Worr_NativeCarrierMixedAbortV1(
        &staged_gate, &staged_dispatch, &staged_ledger);
    if (result != WORR_NATIVE_CARRIER_MIXED_OK)
        return mixed_result(result);

    sender->tx_gate = staged_gate;
    sender->dispatch = staged_dispatch;
    *ack_ledger = staged_ledger;
    sender->state_flags &=
        (uint16_t)~WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED;
    memset(&sender->mixed_token, 0, sizeof(sender->mixed_token));
    saturating_increment(&sender->telemetry.packets_rejected);
    return WORR_NATIVE_EVENT_SENDER_OK;
}

worr_native_event_sender_result_v1 Worr_NativeEventSenderRetireV1(
    worr_native_event_sender_v1 *sender)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;

    if (sender == NULL)
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeEventSenderValidateV1(sender))
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    if ((sender->state_flags & WORR_NATIVE_EVENT_SENDER_RETIRED) != 0)
        return WORR_NATIVE_EVENT_SENDER_RETIRED_RESULT;
    if ((sender->state_flags &
         WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED) != 0)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;

    staged_gate = sender->tx_gate;
    staged_dispatch = sender->dispatch;
    if ((staged_gate.state_flags & WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
        Worr_NativeCarrierSessionDispatchAbortV1(
            &staged_gate, &staged_dispatch) != WORR_NATIVE_CARRIER_SESSION_OK)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    sender->tx_gate = staged_gate;
    sender->dispatch = staged_dispatch;
    memset(sender->backlog, 0, sizeof(sender->backlog));
    sender->backlog_head = 0;
    sender->backlog_count = 0;
    sender->state_flags |= WORR_NATIVE_EVENT_SENDER_RETIRED;
    return WORR_NATIVE_EVENT_SENDER_OK;
}

worr_native_event_sender_result_v1 Worr_NativeEventSenderCancelV1(
    worr_native_event_sender_v1 *sender,
    worr_native_event_sender_cancel_report_v1 *report_out)
{
    worr_native_tx_session_v1 staged_tx;
    worr_native_tx_slot_v1
        staged_slots[WORR_NATIVE_EVENT_SENDER_TX_CAPACITY];
    worr_native_event_sender_payload_v1
        staged_payloads[WORR_NATIVE_EVENT_SENDER_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_event_sender_cancel_report_v1 report;
    uint16_t occupied;
    uint16_t retired;
    uint16_t next_slot;
    uint32_t index;
    uint32_t cancelled_tx = 0;

    if (sender == NULL || report_out == NULL ||
        ranges_overlap(sender, sizeof(*sender), report_out,
                       sizeof(*report_out))) {
        return WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT;
    }
    if (!Worr_NativeEventSenderValidateV1(sender))
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    if ((sender->state_flags &
         WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED) != 0) {
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    }

    memset(&report, 0, sizeof(report));
    if ((sender->state_flags & WORR_NATIVE_EVENT_SENDER_CANCELLED) != 0) {
        *report_out = report;
        return WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT;
    }

    staged_gate = sender->tx_gate;
    staged_dispatch = sender->dispatch;
    if ((staged_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
        Worr_NativeCarrierSessionDispatchAbortV1(
            &staged_gate, &staged_dispatch) !=
            WORR_NATIVE_CARRIER_SESSION_OK) {
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    }

    staged_tx = sender->tx;
    memcpy(staged_slots, sender->tx_slots, sizeof(staged_slots));
    if (Worr_NativeTxSessionCancelRetainedV1(
            &staged_tx, staged_slots,
            WORR_NATIVE_EVENT_SENDER_TX_CAPACITY, &cancelled_tx) !=
        WORR_NATIVE_TX_CANCELLED) {
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
    }

    memcpy(staged_payloads, sender->payloads, sizeof(staged_payloads));
    occupied = sender->payload_occupied;
    retired = sender->payload_retired;
    next_slot = sender->next_payload_slot;
    for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
        if ((sender->tx_slots[index].state_flags &
             WORR_NATIVE_TX_SLOT_OCCUPIED) == 0)
            continue;
        if (!payload_release(staged_payloads, &occupied, &retired,
                             &next_slot,
                             sender->tx_slots[index].payload_handle)) {
            return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;
        }
        ++report.payloads_released;
    }
    report.retained_messages = cancelled_tx;
    report.backlog_candidates = sender->backlog_count;
    if (report.payloads_released != cancelled_tx || occupied != 0)
        return WORR_NATIVE_EVENT_SENDER_INVALID_STATE;

    sender->tx = staged_tx;
    memcpy(sender->tx_slots, staged_slots, sizeof(staged_slots));
    memcpy(sender->payloads, staged_payloads, sizeof(staged_payloads));
    sender->payload_occupied = occupied;
    sender->payload_retired = retired;
    sender->next_payload_slot = next_slot;
    sender->tx_gate = staged_gate;
    sender->dispatch = staged_dispatch;
    memset(&sender->mixed_token, 0, sizeof(sender->mixed_token));
    memset(sender->backlog, 0, sizeof(sender->backlog));
    sender->backlog_head = 0;
    sender->backlog_count = 0;
    sender->state_flags |=
        WORR_NATIVE_EVENT_SENDER_RETIRED |
        WORR_NATIVE_EVENT_SENDER_CANCELLED;
    *report_out = report;
    return WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT;
}
