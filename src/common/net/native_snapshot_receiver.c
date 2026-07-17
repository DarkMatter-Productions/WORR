/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_snapshot_receiver.h"

#include <string.h>

static void increment_saturated(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static bool snapshot_id_equal(worr_snapshot_id_v2 left,
                              worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch &&
           left.sequence == right.sequence;
}

static int snapshot_id_compare(worr_snapshot_id_v2 left,
                               worr_snapshot_id_v2 right)
{
    if (left.epoch < right.epoch)
        return -1;
    if (left.epoch > right.epoch)
        return 1;
    if (left.sequence < right.sequence)
        return -1;
    if (left.sequence > right.sequence)
        return 1;
    return 0;
}

static bool hashes_equal(
    const worr_snapshot_projection_hashes_v2 *left,
    const worr_snapshot_projection_hashes_v2 *right)
{
    return left->struct_size == right->struct_size &&
           left->schema_version == right->schema_version &&
           left->endpoint_hash == right->endpoint_hash &&
           left->legacy_parity_hash == right->legacy_parity_hash &&
           left->semantic_player_hash == right->semantic_player_hash &&
           left->semantic_entity_hash == right->semantic_entity_hash &&
           left->semantic_area_hash == right->semantic_area_hash &&
           left->semantic_event_hash == right->semantic_event_hash;
}

static bool consumer_shape_valid(
    const worr_native_snapshot_consumer_v1 *consumer)
{
    return consumer != NULL &&
           consumer->struct_size == sizeof(*consumer) &&
           consumer->schema_version ==
               WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION &&
           consumer->reserved0 == 0 && consumer->opaque != NULL &&
           consumer->Reset != NULL &&
           consumer->ConsumeCanonicalSnapshot != NULL &&
           consumer->GetStatus != NULL;
}

static bool binding_snapshot_exact(
    const worr_native_session_binding_v1 *binding)
{
    return Worr_NativeSessionBindingValidateV1(binding) &&
           (binding->negotiated_capabilities &
            WORR_NET_CAP_CANONICAL_SNAPSHOT_V2) != 0 &&
           (binding->negotiated_capabilities &
            WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK) ==
               WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK;
}

static bool expectation_shape_valid(
    const worr_native_snapshot_expectation_v1 *expectation)
{
    return expectation != NULL &&
           expectation->struct_size == sizeof(*expectation) &&
           expectation->schema_version ==
               WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION &&
           expectation->reserved0 == 0 &&
           Worr_SnapshotIdValidV2(expectation->snapshot_id, false) &&
           expectation->hashes.struct_size ==
               sizeof(expectation->hashes) &&
           expectation->hashes.schema_version ==
               WORR_SNAPSHOT_PROJECTION_VERSION;
}

static int find_expectation(
    const worr_native_snapshot_receiver_v1 *receiver,
    worr_snapshot_id_v2 snapshot_id)
{
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY;
         ++index) {
        if (receiver->expectations[index].occupied != 0 &&
            snapshot_id_equal(
                receiver->expectations[index].expectation.snapshot_id,
                snapshot_id)) {
            return (int)index;
        }
    }
    return -1;
}

static int find_free_expectation(
    const worr_native_snapshot_receiver_v1 *receiver)
{
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY;
         ++index) {
        if (receiver->expectations[index].occupied == 0)
            return (int)index;
    }
    return -1;
}

static bool snapshot_referenced_by_rx(
    const worr_native_snapshot_receiver_v1 *receiver,
    worr_snapshot_id_v2 snapshot_id)
{
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY;
         ++index) {
        const worr_native_rx_slot_v1 *slot =
            &receiver->rx_slots[index];
        const worr_native_snapshot_receiver_pending_v1 *pending =
            &receiver->pending[index];

        if (slot->state_flags != 0 &&
            slot->reassembly.record.record_class ==
                WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
            slot->reassembly.record.object_epoch ==
                snapshot_id.epoch &&
            slot->reassembly.record.object_sequence ==
                snapshot_id.sequence) {
            return true;
        }
        if (pending->occupied != 0 &&
            pending->message.record.record_class ==
                WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
            pending->message.record.object_epoch ==
                snapshot_id.epoch &&
            pending->message.record.object_sequence ==
                snapshot_id.sequence) {
            return true;
        }
    }
    return false;
}

static int find_recyclable_expectation(
    const worr_native_snapshot_receiver_v1 *receiver,
    worr_snapshot_id_v2 incoming_snapshot_id,
    bool incoming_referenced_by_rx)
{
    int oldest = -1;
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY;
         ++index) {
        const worr_native_snapshot_receiver_expectation_slot_v1 *slot =
            &receiver->expectations[index];

        if (slot->occupied == 0 ||
            snapshot_referenced_by_rx(
                receiver, slot->expectation.snapshot_id)) {
            continue;
        }
        if (oldest < 0 ||
            snapshot_id_compare(
                slot->expectation.snapshot_id,
                receiver->expectations[oldest]
                    .expectation.snapshot_id) < 0) {
            oldest = (int)index;
        }
    }
    if (oldest < 0)
        return -1;

    /*
     * An expectation for DATA already resident in RX must always be allowed
     * to displace an unrelated observation.  Otherwise keep a rolling window
     * of the newest expectation-only frames and ignore older out-of-window
     * observations without turning bounded cache pressure into a disconnect.
     */
    if (!incoming_referenced_by_rx &&
        snapshot_id_compare(
            incoming_snapshot_id,
            receiver->expectations[oldest]
                .expectation.snapshot_id) <= 0) {
        return -1;
    }
    return oldest;
}

static int find_pending_by_id(
    const worr_native_snapshot_receiver_v1 *receiver,
    worr_snapshot_id_v2 snapshot_id)
{
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY;
         ++index) {
        const worr_native_snapshot_receiver_pending_v1 *pending =
            &receiver->pending[index];

        if (pending->occupied != 0 &&
            pending->message.record.record_class ==
                WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
            pending->message.record.object_epoch == snapshot_id.epoch &&
            pending->message.record.object_sequence ==
                snapshot_id.sequence) {
            return (int)index;
        }
    }
    return -1;
}

static void clear_expectation_by_id(
    worr_native_snapshot_receiver_v1 *receiver,
    worr_snapshot_id_v2 snapshot_id)
{
    const int index = find_expectation(receiver, snapshot_id);

    if (index >= 0) {
        memset(&receiver->expectations[index], 0,
               sizeof(receiver->expectations[index]));
    }
}

static void clear_pending(
    worr_native_snapshot_receiver_v1 *receiver,
    uint32_t pending_index)
{
    worr_snapshot_id_v2 snapshot_id;

    if (receiver->pending[pending_index].occupied == 0)
        return;
    snapshot_id.epoch =
        receiver->pending[pending_index].message.record.object_epoch;
    snapshot_id.sequence =
        receiver->pending[pending_index].message.record.object_sequence;
    memset(&receiver->pending[pending_index], 0,
           sizeof(receiver->pending[pending_index]));
    clear_expectation_by_id(receiver, snapshot_id);
}

static bool pending_matches_rx_slot(
    const worr_native_snapshot_receiver_v1 *receiver,
    uint32_t pending_index)
{
    const worr_native_snapshot_receiver_pending_v1 *pending =
        &receiver->pending[pending_index];
    const worr_native_rx_slot_v1 *slot =
        &receiver->rx_slots[pending_index];
    const worr_native_envelope_reassembly_v1 *reassembly =
        &slot->reassembly;

    return pending->occupied != 0 &&
           (slot->state_flags &
            (WORR_NATIVE_RX_SLOT_OCCUPIED |
             WORR_NATIVE_RX_SLOT_COMPLETE)) ==
               (WORR_NATIVE_RX_SLOT_OCCUPIED |
                WORR_NATIVE_RX_SLOT_COMPLETE) &&
           reassembly->record.record_class ==
               pending->message.record.record_class &&
           reassembly->record.record_schema_version ==
               pending->message.record.record_schema_version &&
           reassembly->record.object_epoch ==
               pending->message.record.object_epoch &&
           reassembly->record.object_sequence ==
               pending->message.record.object_sequence &&
           reassembly->transport_epoch ==
               pending->message.transport_epoch &&
           reassembly->message_sequence ==
               pending->message.message_sequence &&
           reassembly->total_payload_bytes ==
               pending->message.payload_bytes &&
           reassembly->payload_crc32 ==
               pending->message.payload_crc32;
}

static void reconcile_pending_with_rx(
    worr_native_snapshot_receiver_v1 *receiver)
{
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY;
         ++index) {
        if (receiver->pending[index].occupied != 0 &&
            !pending_matches_rx_slot(receiver, index)) {
            clear_pending(receiver, index);
        }
    }
}

/*
 * Committing a newer snapshot is allowed to retire an older complete RX slot
 * atomically inside the session core.  Mirror that retirement into this
 * owner's deferred descriptors, and discard expectations older than the
 * newly accepted semantic high-water mark so a lost native frame cannot
 * consume the bounded cache forever.
 */
static void reconcile_after_admission(
    worr_native_snapshot_receiver_v1 *receiver)
{
    uint32_t index;
    const worr_snapshot_id_v2 highwater =
        receiver->admission.last_accepted_snapshot;

    reconcile_pending_with_rx(receiver);
    if (!Worr_SnapshotIdValidV2(highwater, false))
        return;
    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY;
         ++index) {
        if (receiver->expectations[index].occupied != 0 &&
            snapshot_id_compare(
                receiver->expectations[index]
                    .expectation.snapshot_id,
                highwater) <= 0) {
            memset(&receiver->expectations[index], 0,
                   sizeof(receiver->expectations[index]));
        }
    }
}

static bool pending_message_shape_valid(
    const worr_native_snapshot_receiver_v1 *receiver,
    const worr_native_snapshot_receiver_pending_v1 *pending,
    uint32_t index)
{
    return pending->occupied <= 1 && pending->reserved0 == 0 &&
           (pending->occupied == 0 ||
            (pending->receive_time_us != 0 &&
             pending->message.struct_size ==
                 sizeof(pending->message) &&
             pending->message.schema_version ==
                 WORR_NATIVE_SESSION_ABI_VERSION &&
             pending->message.reserved0 == 0 &&
             pending->message.reserved1 == 0 &&
             pending->message.slot_index == index &&
             pending->message.slot_index <
                 WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY &&
             pending->message.payload_bytes != 0 &&
             pending->message.payload_bytes <=
                 WORR_NATIVE_SNAPSHOT_RECEIVER_PAYLOAD_STRIDE &&
             pending->message.record.record_class ==
                 WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
             pending->message.record.record_schema_version ==
                 WORR_SNAPSHOT_ABI_VERSION &&
             pending->message.record.object_epoch ==
                  receiver->snapshot_epoch &&
              pending->message.transport_epoch ==
                  receiver->binding.transport_epoch &&
              pending->message.connection_owner_id ==
                  receiver->binding.connection_owner_id &&
              pending_matches_rx_slot(receiver, index)));
}

static bool expectation_slot_shape_valid(
    const worr_native_snapshot_receiver_v1 *receiver,
    const worr_native_snapshot_receiver_expectation_slot_v1 *slot)
{
    return slot->occupied <= 1 && slot->reserved0 == 0 &&
           (slot->occupied == 0 ||
            (expectation_shape_valid(&slot->expectation) &&
             slot->expectation.snapshot_id.epoch ==
                 receiver->snapshot_epoch));
}

bool Worr_NativeSnapshotReceiverValidateV1(
    const worr_native_snapshot_receiver_v1 *receiver)
{
    uint32_t index;

    if (receiver == NULL ||
        receiver->struct_size != sizeof(*receiver) ||
        receiver->schema_version !=
            WORR_NATIVE_SNAPSHOT_RECEIVER_ABI_VERSION ||
        receiver->reserved0 != 0 ||
        (receiver->state_flags &
         ~(WORR_NATIVE_SNAPSHOT_RECEIVER_INITIALIZED |
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED |
           WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED)) != 0 ||
        (receiver->state_flags &
         WORR_NATIVE_SNAPSHOT_RECEIVER_INITIALIZED) == 0 ||
        receiver->snapshot_epoch == 0 ||
        receiver->max_entities == 0 ||
        !binding_snapshot_exact(&receiver->binding) ||
        !Worr_NativeSnapshotAdmissionValidateV1(
            &receiver->admission) ||
        !Worr_NativeRxSessionValidateV1(
            &receiver->rx, receiver->rx_slots,
            WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY) ||
        !Worr_NativeCarrierAckLedgerValidateV1(
            &receiver->ack_ledger) ||
        !consumer_shape_valid(&receiver->consumer) ||
        receiver->scratch.struct_size !=
            sizeof(receiver->scratch) ||
        receiver->scratch.schema_version !=
            WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION ||
        receiver->scratch.reserved0 != 0 ||
        receiver->scratch.reserved1 != 0 ||
        receiver->scratch.snapshot != &receiver->decoded_snapshot ||
        receiver->scratch.player != &receiver->decoded_player ||
        receiver->scratch.entities != receiver->decoded_entities ||
        receiver->scratch.area_bytes != receiver->decoded_area ||
        receiver->scratch.event_refs != receiver->decoded_events ||
        receiver->scratch.entity_capacity !=
            WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES ||
        receiver->scratch.area_capacity !=
            WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES ||
        receiver->scratch.event_ref_capacity !=
            WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS ||
        receiver->binding.connection_owner_id !=
            receiver->admission.connection_owner_id ||
        receiver->binding.connection_owner_id !=
            receiver->rx.connection_owner_id ||
        receiver->binding.connection_owner_id !=
            receiver->ack_ledger.connection_owner_id ||
        receiver->binding.transport_epoch !=
            receiver->admission.transport_epoch ||
        receiver->binding.transport_epoch !=
            receiver->rx.transport_epoch ||
        receiver->binding.transport_epoch !=
            receiver->ack_ledger.transport_epoch ||
        receiver->rx.payload_stride !=
            WORR_NATIVE_SNAPSHOT_RECEIVER_PAYLOAD_STRIDE ||
        ((receiver->state_flags &
          WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED) != 0 &&
         (receiver->rx.occupied_count != 0 ||
          receiver->ack_ledger.receipt_count != 0))) {
        return false;
    }

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY;
         ++index) {
        if (!pending_message_shape_valid(
                receiver, &receiver->pending[index], index)) {
            return false;
        }
    }
    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY;
         ++index) {
        if (!expectation_slot_shape_valid(
                receiver, &receiver->expectations[index])) {
            return false;
        }
    }
    return true;
}

bool Worr_NativeSnapshotReceiverInitV1(
    worr_native_snapshot_receiver_v1 *receiver,
    const worr_native_session_binding_v1 *binding,
    uint32_t snapshot_epoch,
    uint32_t max_entities,
    const worr_native_snapshot_consumer_v1 *consumer)
{
    if (receiver == NULL || binding == NULL || snapshot_epoch == 0 ||
        max_entities == 0 ||
        !binding_snapshot_exact(binding) ||
        !consumer_shape_valid(consumer)) {
        return false;
    }

    /*
     * Live adapters allocate a fresh owner before calling Init.  Avoid a
     * second ~400 KiB stack image here; a failed initialization is scrubbed
     * instead of publishing partially usable state.
     */
    memset(receiver, 0, sizeof(*receiver));
    receiver->struct_size = sizeof(*receiver);
    receiver->schema_version =
        WORR_NATIVE_SNAPSHOT_RECEIVER_ABI_VERSION;
    receiver->state_flags =
        WORR_NATIVE_SNAPSHOT_RECEIVER_INITIALIZED;
    receiver->snapshot_epoch = snapshot_epoch;
    receiver->max_entities = max_entities;
    receiver->binding = *binding;
    receiver->consumer = *consumer;
    receiver->scratch.struct_size = sizeof(receiver->scratch);
    receiver->scratch.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    receiver->scratch.snapshot = &receiver->decoded_snapshot;
    receiver->scratch.player = &receiver->decoded_player;
    receiver->scratch.entities = receiver->decoded_entities;
    receiver->scratch.area_bytes = receiver->decoded_area;
    receiver->scratch.event_refs = receiver->decoded_events;
    receiver->scratch.entity_capacity =
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES;
    receiver->scratch.area_capacity =
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES;
    receiver->scratch.event_ref_capacity =
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS;

    if (!Worr_NativeSnapshotAdmissionInitV1(
            &receiver->admission, binding) ||
        !Worr_NativeRxSessionInitV1(
            &receiver->rx, receiver->rx_slots,
            WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY,
            WORR_NATIVE_SNAPSHOT_RECEIVER_PAYLOAD_STRIDE,
            WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_TIMEOUT_TICKS,
            WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_TIMEOUT_TICKS,
            binding) ||
        !Worr_NativeCarrierAckLedgerInitV1(
            &receiver->ack_ledger, binding,
            WORR_NATIVE_SNAPSHOT_RECEIVER_ACK_PROACTIVE_HANDOFFS)) {
        memset(receiver, 0, sizeof(*receiver));
        return false;
    }
    if (!Worr_NativeSnapshotReceiverValidateV1(receiver)) {
        memset(receiver, 0, sizeof(*receiver));
        return false;
    }
    return true;
}

static worr_native_snapshot_receiver_result_v1 quarantine(
    worr_native_snapshot_receiver_v1 *receiver,
    worr_native_snapshot_receiver_result_v1 result)
{
    if ((receiver->state_flags &
         WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0) {
        increment_saturated(&receiver->telemetry.quarantines);
    }
    receiver->state_flags |=
        WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED;
    (void)Worr_NativeSnapshotAdmissionRequireKeyframeV1(
        &receiver->admission);
    receiver->last_result = (uint32_t)result;
    return result;
}

static worr_native_snapshot_receiver_result_v1
map_admission_result(
    worr_native_snapshot_receiver_v1 *receiver,
    worr_native_snapshot_admission_result_v1 result,
    bool repeat)
{
    receiver->last_result = (uint32_t)result;
    switch (result) {
    case WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_ACCEPTED:
    case WORR_NATIVE_SNAPSHOT_ADMISSION_FULL_UPDATE_ACCEPTED:
        increment_saturated(
            &receiver->telemetry.snapshots_admitted);
        return WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED;
    case WORR_NATIVE_SNAPSHOT_ADMISSION_REPEAT_REVALIDATED:
        if (!repeat)
            break;
        increment_saturated(
            &receiver->telemetry.repeats_revalidated);
        return WORR_NATIVE_SNAPSHOT_RECEIVER_REPEAT_REVALIDATED;
    case WORR_NATIVE_SNAPSHOT_ADMISSION_RETRY_UNCOMMITTED:
    case WORR_NATIVE_SNAPSHOT_ADMISSION_NOT_READY:
        increment_saturated(
            &receiver->telemetry.retry_deferrals);
        return WORR_NATIVE_SNAPSHOT_RECEIVER_RETRY_LATER;
    case WORR_NATIVE_SNAPSHOT_ADMISSION_STALE:
        return WORR_NATIVE_SNAPSHOT_RECEIVER_STALE;
    case WORR_NATIVE_SNAPSHOT_ADMISSION_WRONG_EPOCH:
        return quarantine(
            receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_WRONG_EPOCH);
    case WORR_NATIVE_SNAPSHOT_ADMISSION_PARITY_MISMATCH:
        return quarantine(
            receiver,
            WORR_NATIVE_SNAPSHOT_RECEIVER_PARITY_MISMATCH);
    case WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_REQUIRED:
        return WORR_NATIVE_SNAPSHOT_RECEIVER_KEYFRAME_REQUIRED;
    case WORR_NATIVE_SNAPSHOT_ADMISSION_CAPACITY:
        increment_saturated(
            &receiver->telemetry.capacity_stalls);
        return WORR_NATIVE_SNAPSHOT_RECEIVER_CAPACITY;
    case WORR_NATIVE_SNAPSHOT_ADMISSION_UNSUPPORTED:
    case WORR_NATIVE_SNAPSHOT_ADMISSION_NOT_NEGOTIATED:
        return quarantine(
            receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_UNSUPPORTED);
    case WORR_NATIVE_SNAPSHOT_ADMISSION_MALFORMED:
        return quarantine(
            receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_MALFORMED);
    case WORR_NATIVE_SNAPSHOT_ADMISSION_CONFLICT:
        return quarantine(
            receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_CONFLICT);
    case WORR_NATIVE_SNAPSHOT_ADMISSION_RESYNC_UNCOMMITTED:
        return quarantine(
            receiver,
            WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED_RESULT);
    default:
        break;
    }
    return quarantine(
        receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_STATE);
}

static worr_native_snapshot_receiver_result_v1 pump_pending(
    worr_native_snapshot_receiver_v1 *receiver,
    uint32_t pending_index,
    uint32_t expectation_index)
{
    worr_native_snapshot_receiver_pending_v1 *pending;
    worr_native_snapshot_receiver_expectation_slot_v1 *expectation;
    worr_native_snapshot_admission_result_v1 admitted;
    worr_native_snapshot_receiver_result_v1 mapped;

    pending = &receiver->pending[pending_index];
    expectation = &receiver->expectations[expectation_index];
    admitted = Worr_NativeSnapshotAdmissionCommitCompletedV1(
        &receiver->admission, &receiver->binding,
        &receiver->rx, receiver->rx_slots,
        WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY,
        receiver->payload_arena, sizeof(receiver->payload_arena),
        &receiver->ack_ledger, &pending->message,
        receiver->max_entities, pending->receive_time_us,
        &receiver->scratch, &expectation->expectation,
        &receiver->consumer);
    mapped = map_admission_result(receiver, admitted, false);
    if (mapped == WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED) {
        reconcile_after_admission(receiver);
    } else if (mapped == WORR_NATIVE_SNAPSHOT_RECEIVER_STALE) {
        const worr_native_rx_result_v1 discarded =
            Worr_NativeRxSessionDiscardV1(
                &receiver->rx, receiver->rx_slots,
                WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY,
                pending->message.slot_index,
                pending->message.message_sequence);

        if (discarded != WORR_NATIVE_RX_DISCARDED &&
            discarded != WORR_NATIVE_RX_NOT_FOUND) {
            return quarantine(
                receiver,
                WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_STATE);
        }
        clear_pending(receiver, pending_index);
    } else if (mapped !=
               WORR_NATIVE_SNAPSHOT_RECEIVER_RETRY_LATER) {
        increment_saturated(
            &receiver->telemetry.semantic_rejections);
    }
    return mapped;
}

worr_native_snapshot_receiver_result_v1
Worr_NativeSnapshotReceiverAcceptDataV1(
    worr_native_snapshot_receiver_v1 *receiver,
    uint64_t now_tick,
    uint64_t receive_time_us,
    const void *packet,
    size_t packet_bytes,
    uint16_t entry_index)
{
    worr_native_rx_result_v1 rx_result;
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 repeat_acknowledgement;
    worr_native_carrier_session_result_v1 bridge;
    worr_snapshot_id_v2 snapshot_id;
    int expectation_index;

    if (receiver == NULL || packet == NULL || packet_bytes == 0 ||
        receive_time_us == 0 ||
        !Worr_NativeSnapshotReceiverValidateV1(receiver)) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_ARGUMENT;
    }
    if ((receiver->state_flags &
         WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED) != 0) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT;
    }
    if ((receiver->state_flags &
         WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) != 0) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED_RESULT;
    }

    increment_saturated(&receiver->telemetry.packets_observed);
    memset(&message, 0, sizeof(message));
    memset(&repeat_acknowledgement, 0,
           sizeof(repeat_acknowledgement));
    bridge = Worr_NativeCarrierSessionAcceptDataV1(
        &receiver->rx, receiver->rx_slots,
        WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY,
        receiver->payload_arena, sizeof(receiver->payload_arena),
        now_tick, packet, packet_bytes, entry_index, &rx_result,
        &message, &repeat_acknowledgement);
    if (bridge != WORR_NATIVE_CARRIER_SESSION_OK) {
        if (bridge == WORR_NATIVE_CARRIER_SESSION_WRONG_EPOCH)
            return quarantine(
                receiver,
                WORR_NATIVE_SNAPSHOT_RECEIVER_WRONG_EPOCH);
        if (bridge == WORR_NATIVE_CARRIER_SESSION_UNSUPPORTED ||
            bridge ==
                WORR_NATIVE_CARRIER_SESSION_WRONG_ENTRY_TYPE) {
            return quarantine(
                receiver,
                WORR_NATIVE_SNAPSHOT_RECEIVER_UNSUPPORTED);
        }
        return quarantine(
            receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_MALFORMED);
    }
    /*
     * NativeRxSession may expire or reuse a complete slot before returning the
     * result for this packet.  Retire wrapper-owned descriptors immediately,
     * before a newly completed message is checked against that slot.
     */
    reconcile_pending_with_rx(receiver);

    switch (rx_result) {
    case WORR_NATIVE_RX_FRAGMENT_ACCEPTED:
        increment_saturated(
            &receiver->telemetry.fragments_accepted);
        return WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED;
    case WORR_NATIVE_RX_FRAGMENT_DUPLICATE:
        increment_saturated(
            &receiver->telemetry.fragment_duplicates);
        return WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_DUPLICATE;
    case WORR_NATIVE_RX_ALREADY_COMMITTED:
        return map_admission_result(
            receiver,
            Worr_NativeSnapshotAdmissionRevalidateCommittedRepeatV1(
                &receiver->admission, &receiver->binding,
                &receiver->rx, receiver->rx_slots,
                WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY,
                receiver->payload_arena,
                sizeof(receiver->payload_arena), now_tick,
                packet, packet_bytes, entry_index,
                &receiver->ack_ledger, &receiver->consumer),
            true);
    case WORR_NATIVE_RX_CAPACITY:
    case WORR_NATIVE_RX_STORAGE_CAPACITY:
        increment_saturated(
            &receiver->telemetry.capacity_stalls);
        return WORR_NATIVE_SNAPSHOT_RECEIVER_CAPACITY;
    case WORR_NATIVE_RX_STALE_REPLAY:
    case WORR_NATIVE_RX_STALE_SNAPSHOT:
        return WORR_NATIVE_SNAPSHOT_RECEIVER_STALE;
    case WORR_NATIVE_RX_WRONG_EPOCH:
        return quarantine(
            receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_WRONG_EPOCH);
    case WORR_NATIVE_RX_UNSUPPORTED:
        return quarantine(
            receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_UNSUPPORTED);
    case WORR_NATIVE_RX_MESSAGE_COMPLETE:
        break;
    default:
        return quarantine(
            receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_MALFORMED);
    }

    if (message.slot_index >=
            WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY ||
        message.record.record_class !=
            WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
        message.record.record_schema_version !=
            WORR_SNAPSHOT_ABI_VERSION ||
        message.record.object_epoch != receiver->snapshot_epoch ||
        receiver->pending[message.slot_index].occupied != 0) {
        return quarantine(
            receiver, message.record.object_epoch !=
                          receiver->snapshot_epoch
                      ? WORR_NATIVE_SNAPSHOT_RECEIVER_WRONG_EPOCH
                      : WORR_NATIVE_SNAPSHOT_RECEIVER_CONFLICT);
    }

    receiver->pending[message.slot_index].occupied = 1;
    receiver->pending[message.slot_index].receive_time_us =
        receive_time_us;
    receiver->pending[message.slot_index].message = message;
    increment_saturated(&receiver->telemetry.messages_completed);
    snapshot_id.epoch = message.record.object_epoch;
    snapshot_id.sequence = message.record.object_sequence;
    expectation_index = find_expectation(receiver, snapshot_id);
    if (expectation_index < 0) {
        increment_saturated(
            &receiver->telemetry.completions_deferred);
        return WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_PENDING;
    }
    return pump_pending(
        receiver, message.slot_index,
        (uint32_t)expectation_index);
}

worr_native_snapshot_receiver_result_v1
Worr_NativeSnapshotReceiverObserveExpectationV1(
    worr_native_snapshot_receiver_v1 *receiver,
    const worr_native_snapshot_expectation_v1 *expectation)
{
    int expectation_index;
    int pending_index;
    bool incoming_referenced_by_rx;

    if (receiver == NULL || !expectation_shape_valid(expectation) ||
        !Worr_NativeSnapshotReceiverValidateV1(receiver)) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_ARGUMENT;
    }
    if ((receiver->state_flags &
         WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED) != 0) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT;
    }
    if ((receiver->state_flags &
         WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) != 0) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED_RESULT;
    }
    if (expectation->snapshot_id.epoch !=
        receiver->snapshot_epoch) {
        return quarantine(
            receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_WRONG_EPOCH);
    }

    increment_saturated(
        &receiver->telemetry.expectations_observed);
    pending_index = find_pending_by_id(
        receiver, expectation->snapshot_id);
    incoming_referenced_by_rx =
        pending_index >= 0 ||
        snapshot_referenced_by_rx(
            receiver, expectation->snapshot_id);
    expectation_index = find_expectation(
        receiver, expectation->snapshot_id);
    if (expectation_index >= 0) {
        if (!hashes_equal(
                &receiver->expectations[expectation_index]
                     .expectation.hashes,
                &expectation->hashes)) {
            return quarantine(
                receiver,
                WORR_NATIVE_SNAPSHOT_RECEIVER_CONFLICT);
        }
    } else {
        expectation_index = find_free_expectation(receiver);
        if (expectation_index < 0) {
            expectation_index = find_recyclable_expectation(
                receiver, expectation->snapshot_id,
                incoming_referenced_by_rx);
            if (expectation_index < 0) {
                increment_saturated(
                    &receiver->telemetry.capacity_stalls);
                increment_saturated(
                    &receiver->telemetry.retry_deferrals);
                return WORR_NATIVE_SNAPSHOT_RECEIVER_RETRY_LATER;
            }
        }
        memset(&receiver->expectations[expectation_index], 0,
               sizeof(receiver->expectations[expectation_index]));
        receiver->expectations[expectation_index].occupied = 1;
        receiver->expectations[expectation_index].expectation =
            *expectation;
        increment_saturated(
            &receiver->telemetry.expectations_cached);
    }

    if (pending_index < 0)
        return WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED;
    return pump_pending(
        receiver, (uint32_t)pending_index,
        (uint32_t)expectation_index);
}

worr_native_snapshot_receiver_result_v1
Worr_NativeSnapshotReceiverQuarantineV1(
    worr_native_snapshot_receiver_v1 *receiver)
{
    if (receiver == NULL ||
        !Worr_NativeSnapshotReceiverValidateV1(receiver)) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_ARGUMENT;
    }
    if ((receiver->state_flags &
         WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED) != 0) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT;
    }
    return quarantine(
        receiver, WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED_RESULT);
}

worr_native_snapshot_receiver_result_v1
Worr_NativeSnapshotReceiverCancelV1(
    worr_native_snapshot_receiver_v1 *receiver,
    uint32_t *cancelled_messages_out,
    uint32_t *cancelled_receipts_out)
{
    worr_native_rx_session_v1 staged_rx;
    worr_native_rx_slot_v1 staged_slots[
        WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY];
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_rx_cancel_report_v1 rx_report;
    uint32_t cancelled_receipts;
    uint32_t cancelled_messages;
    uint32_t index;

    if (receiver == NULL || cancelled_messages_out == NULL ||
        cancelled_receipts_out == NULL ||
        !Worr_NativeSnapshotReceiverValidateV1(receiver)) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_ARGUMENT;
    }
    if ((receiver->state_flags &
         WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED) != 0) {
        *cancelled_messages_out = 0;
        *cancelled_receipts_out = 0;
        return WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT;
    }

    staged_rx = receiver->rx;
    memcpy(staged_slots, receiver->rx_slots,
           sizeof(staged_slots));
    staged_ledger = receiver->ack_ledger;
    memset(&rx_report, 0, sizeof(rx_report));
    if (Worr_NativeRxSessionCancelPendingV1(
            &staged_rx, staged_slots,
            WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY,
            &rx_report) != WORR_NATIVE_RX_CANCELLED) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_STATE;
    }
    cancelled_receipts = 0;
    if (Worr_NativeCarrierAckCancelAllV1(
            &staged_ledger, &cancelled_receipts) !=
        WORR_NATIVE_CARRIER_ACK_OK) {
        return WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_STATE;
    }
    cancelled_messages =
        rx_report.incomplete_messages + rx_report.complete_messages;
    receiver->rx = staged_rx;
    memcpy(receiver->rx_slots, staged_slots,
           sizeof(staged_slots));
    receiver->ack_ledger = staged_ledger;
    memset(receiver->pending, 0, sizeof(receiver->pending));
    memset(receiver->expectations, 0,
           sizeof(receiver->expectations));
    memset(receiver->payload_arena, 0,
           sizeof(receiver->payload_arena));
    receiver->state_flags |=
        WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED;
    for (index = 0; index < cancelled_messages; ++index)
        increment_saturated(
            &receiver->telemetry.cancelled_messages);
    for (index = 0; index < cancelled_receipts; ++index)
        increment_saturated(
            &receiver->telemetry.cancelled_receipts);
    *cancelled_messages_out = cancelled_messages;
    *cancelled_receipts_out = cancelled_receipts;
    return WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT;
}
