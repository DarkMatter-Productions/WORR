/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_snapshot_sender.h"

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

static void saturating_add(uint64_t *value, uint64_t amount)
{
    if (UINT64_MAX - *value < amount)
        *value = UINT64_MAX;
    else
        *value += amount;
}

static bool snapshot_id_equal(worr_snapshot_id_v2 left,
                              worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch && left.sequence == right.sequence;
}

static int snapshot_id_compare(worr_snapshot_id_v2 left,
                               worr_snapshot_id_v2 right)
{
    if (left.epoch != right.epoch)
        return left.epoch < right.epoch ? -1 : 1;
    if (left.sequence != right.sequence)
        return left.sequence < right.sequence ? -1 : 1;
    return 0;
}

static bool snapshot_id_absent(worr_snapshot_id_v2 id)
{
    return id.epoch == 0 && id.sequence == 0;
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

static bool projection_hashes_equal(
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

static worr_native_record_ref_v1 snapshot_record(worr_snapshot_id_v2 id)
{
    worr_native_record_ref_v1 record;

    memset(&record, 0, sizeof(record));
    record.record_class = WORR_NATIVE_RECORD_SNAPSHOT_V1;
    record.record_schema_version = WORR_SNAPSHOT_ABI_VERSION;
    record.object_epoch = id.epoch;
    record.object_sequence = id.sequence;
    return record;
}

static uint16_t minimum_datagram(uint16_t configured, uint32_t payload_bytes)
{
    const uint64_t exact =
        (uint64_t)payload_bytes + WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES;

    return exact < configured ? (uint16_t)exact : configured;
}

static bool payload_fits_datagram_plan(uint32_t payload_bytes,
                                      uint16_t max_datagram_bytes)
{
    const uint32_t stride =
        max_datagram_bytes - WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES;

    return stride != 0 &&
           (uint64_t)payload_bytes <=
               (uint64_t)stride * WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS;
}

static uint32_t payload_handle(uint32_t generation, uint32_t index)
{
    return (generation <<
            WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_INDEX_BITS) |
           index;
}

static worr_native_snapshot_sender_payload_v1 *payload_find(
    worr_native_snapshot_sender_payload_v1 *payloads,
    uint32_t handle,
    uint32_t *index_out)
{
    const uint32_t index =
        handle & WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_INDEX_MASK;
    const uint32_t generation =
        handle >> WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_INDEX_BITS;
    worr_native_snapshot_sender_payload_v1 *payload;

    if (generation == 0 ||
        index >= WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS)
        return NULL;
    payload = &payloads[index];
    if ((payload->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_OCCUPIED) == 0 ||
        payload->handle != handle || payload->generation != generation)
        return NULL;
    if (index_out != NULL)
        *index_out = index;
    return payload;
}

static const worr_native_snapshot_sender_payload_v1 *payload_find_const(
    const worr_native_snapshot_sender_payload_v1 *payloads,
    uint32_t handle)
{
    return payload_find(
        (worr_native_snapshot_sender_payload_v1 *)payloads, handle, NULL);
}

static bool payload_zero_except_generation(
    const worr_native_snapshot_sender_payload_v1 *payload)
{
    worr_native_snapshot_sender_payload_v1 expected;

    memset(&expected, 0, sizeof(expected));
    expected.generation = payload->generation;
    expected.state_flags = payload->state_flags;
    return memcmp(payload, &expected, sizeof(expected)) == 0;
}

static bool payload_decode_matches(
    const worr_native_snapshot_sender_payload_v1 *payload)
{
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 record;

    if (payload->encoded_bytes == 0 ||
        payload->encoded_bytes > sizeof(payload->encoded) ||
        payload->reserved0 != 0 ||
        payload->hashes.struct_size != sizeof(payload->hashes) ||
        payload->hashes.schema_version !=
            WORR_SNAPSHOT_PROJECTION_VERSION ||
        Worr_NativeCodecInspectV1(
            payload->encoded, payload->encoded_bytes, &info) !=
            WORR_NATIVE_CODEC_OK ||
        !Worr_NativeCodecInfoRecordRefV1(&info, &record) ||
        !record_ref_equal(record, payload->record) ||
        record.record_class != WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
        record.record_schema_version != WORR_SNAPSHOT_ABI_VERSION)
        return false;
    return true;
}

static bool payload_is_tx_referenced(
    const worr_native_snapshot_sender_v1 *sender, uint32_t handle)
{
    return sender->tx.retained_count != 0 &&
           sender->tx_slots[0].state_flags == WORR_NATIVE_TX_SLOT_OCCUPIED &&
           sender->tx_slots[0].payload_handle == handle;
}

static bool payload_is_dispatch_referenced(
    const worr_native_snapshot_sender_v1 *sender, uint32_t handle)
{
    return (sender->tx_gate.state_flags &
            WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
           (sender->dispatch.state_flags &
            WORR_NATIVE_CARRIER_DISPATCH_PAYLOAD_BOUND) != 0 &&
           sender->dispatch.payload_handle == handle;
}

static bool payload_is_pending(
    const worr_native_snapshot_sender_v1 *sender, uint32_t index)
{
    return sender->pending_bank !=
               WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK &&
           sender->pending_bank == index;
}

static bool payloads_validate(
    const worr_native_snapshot_sender_v1 *sender)
{
    uint8_t occupied = 0;
    uint8_t retired = 0;
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS; ++index) {
        const worr_native_snapshot_sender_payload_v1 *payload =
            &sender->payloads[index];
        const uint16_t known =
            WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_OCCUPIED |
            WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_RETIRED;

        if ((payload->state_flags & ~known) != 0)
            return false;
        if ((payload->state_flags &
             WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_OCCUPIED) != 0) {
            const bool tx_ref =
                payload_is_tx_referenced(sender, payload->handle);
            const bool dispatch_ref =
                payload_is_dispatch_referenced(sender, payload->handle);
            const bool pending_ref = payload_is_pending(sender, index);

            if ((payload->state_flags &
                 WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_RETIRED) != 0 ||
                payload->generation == 0 ||
                payload->generation >
                    WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_GENERATION_MAX ||
                payload->handle !=
                    payload_handle(payload->generation, index) ||
                !payload_decode_matches(payload) ||
                (!tx_ref && !dispatch_ref && !pending_ref) ||
                (pending_ref && (tx_ref || dispatch_ref)))
                return false;
            ++occupied;
        } else if ((payload->state_flags &
                    WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_RETIRED) != 0) {
            if (payload->generation !=
                    WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_GENERATION_MAX ||
                !payload_zero_except_generation(payload))
                return false;
            ++retired;
        } else if (payload->generation >=
                       WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_GENERATION_MAX ||
                   !payload_zero_except_generation(payload)) {
            return false;
        }
    }
    return occupied == sender->payload_occupied &&
           retired == sender->payload_retired &&
           (uint32_t)occupied + retired <=
               WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS;
}

bool Worr_NativeSnapshotSenderValidateV1(
    const worr_native_snapshot_sender_v1 *sender)
{
    const uint16_t known =
        WORR_NATIVE_SNAPSHOT_SENDER_INITIALIZED |
        WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED |
        WORR_NATIVE_SNAPSHOT_SENDER_RETIRED |
        WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED |
        WORR_NATIVE_SNAPSHOT_SENDER_PROMOTION_STALLED;
    const bool prepared =
        sender != NULL &&
        (sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED) != 0;
    const bool retired =
        sender != NULL &&
        (sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_RETIRED) != 0;
    const bool cancelled =
        sender != NULL &&
        (sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED) != 0;
    const bool gate_active =
        sender != NULL &&
        (sender->tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0;

    if (sender == NULL || sender->struct_size != sizeof(*sender) ||
        sender->schema_version !=
            WORR_NATIVE_SNAPSHOT_SENDER_ABI_VERSION ||
        (sender->state_flags & ~known) != 0 ||
        (sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_INITIALIZED) == 0 ||
        !Worr_NativeSessionBindingValidateV1(&sender->binding) ||
        sender->binding.negotiated_capabilities !=
            WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK ||
        sender->max_entities == 0 ||
        sender->max_datagram_bytes <=
            WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ||
        sender->max_datagram_bytes >
            WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES ||
        (sender->pending_bank !=
             WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK &&
         sender->pending_bank >=
             WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS) ||
        sender->next_payload_bank >=
            WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS ||
        sender->reserved0 != 0 ||
        sender->last_tick < sender->tx.last_tick ||
        !Worr_NativeTxSessionValidateV1(
            &sender->tx, sender->tx_slots,
            WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY) ||
        !Worr_NativeCarrierTxGateValidateV1(&sender->tx_gate) ||
        sender->tx.transport_epoch != sender->binding.transport_epoch ||
        sender->tx.connection_owner_id !=
            sender->binding.connection_owner_id ||
        sender->tx_gate.transport_epoch !=
            sender->binding.transport_epoch ||
        sender->tx_gate.connection_owner_id !=
            sender->binding.connection_owner_id ||
        sender->tx.retained_count >
            WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY ||
        (sender->tx.retained_count != 0 &&
         (sender->tx_slots[0].record.record_class !=
              WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
          sender->tx_slots[0].priority !=
              WORR_NATIVE_SNAPSHOT_SENDER_PRIORITY ||
          payload_find_const(
              sender->payloads,
              sender->tx_slots[0].payload_handle) == NULL)) ||
        (gate_active &&
         payload_find_const(
             sender->payloads, sender->dispatch.payload_handle) == NULL) ||
        (sender->pending_bank !=
             WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK &&
         (sender->payloads[sender->pending_bank].state_flags &
          WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_OCCUPIED) == 0) ||
        ((sender->state_flags &
          WORR_NATIVE_SNAPSHOT_SENDER_PROMOTION_STALLED) != 0 &&
         sender->pending_bank ==
             WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK) ||
        (retired &&
         (prepared || gate_active ||
          sender->pending_bank !=
              WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK)) ||
        (cancelled &&
         (!retired || sender->tx.retained_count != 0 ||
          sender->payload_occupied != 0 ||
          (sender->tx.state_flags &
           WORR_NATIVE_TX_TERMINAL_CANCELLED) == 0)) ||
        (!cancelled &&
         (sender->tx.state_flags &
          WORR_NATIVE_TX_TERMINAL_CANCELLED) != 0) ||
        !payloads_validate(sender))
        return false;

    if (prepared) {
        if (!gate_active ||
            (sender->tx_gate.state_flags &
             WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) == 0 ||
            sender->mixed_token.struct_size !=
                sizeof(sender->mixed_token) ||
            sender->mixed_token.schema_version !=
                WORR_NATIVE_CARRIER_MIXED_ABI_VERSION ||
            sender->mixed_token.dispatch_token_id !=
                sender->dispatch.token_id)
            return false;
    } else if ((sender->tx_gate.state_flags &
                WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0 ||
               memcmp(&sender->mixed_token,
                      &(worr_native_carrier_mixed_token_v1){0},
                      sizeof(sender->mixed_token)) != 0) {
        return false;
    }
    if (sender->pending_bank !=
        WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK) {
        const worr_native_snapshot_sender_payload_v1 *pending =
            &sender->payloads[sender->pending_bank];
        const worr_snapshot_id_v2 pending_id = {
            pending->record.object_epoch,
            pending->record.object_sequence,
        };

        if (!snapshot_id_equal(
                pending_id, sender->latest_offered_snapshot))
            return false;
    }
    return true;
}

static bool payload_select_free(
    const worr_native_snapshot_sender_v1 *sender,
    uint32_t *index_out,
    uint32_t *handle_out)
{
    uint32_t offset;

    for (offset = 0;
         offset < WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS; ++offset) {
        const uint32_t index =
            (sender->next_payload_bank + offset) %
            WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS;
        const worr_native_snapshot_sender_payload_v1 *payload =
            &sender->payloads[index];

        if (payload->state_flags == 0) {
            const uint32_t generation = payload->generation + 1u;

            if (generation == 0 ||
                generation >
                    WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_GENERATION_MAX)
                continue;
            *index_out = index;
            *handle_out = payload_handle(generation, index);
            return true;
        }
    }
    return false;
}

static void payload_install_free(
    worr_native_snapshot_sender_v1 *sender,
    uint32_t index,
    uint32_t handle,
    worr_native_record_ref_v1 record,
    const worr_snapshot_projection_hashes_v2 *hashes,
    uint64_t snapshot_hash,
    uint64_t queue_tick,
    uint32_t encoded_bytes)
{
    worr_native_snapshot_sender_payload_v1 *payload =
        &sender->payloads[index];

    payload->handle = handle;
    payload->generation =
        handle >> WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_INDEX_BITS;
    payload->encoded_bytes = encoded_bytes;
    payload->state_flags =
        WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_OCCUPIED;
    payload->reserved0 = 0;
    payload->record = record;
    payload->hashes = *hashes;
    payload->snapshot_hash = snapshot_hash;
    payload->queue_tick = queue_tick;
    ++sender->payload_occupied;
    sender->next_payload_bank =
        (uint8_t)((index + 1u) %
                  WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS);
}

static void payload_replace_pending(
    worr_native_snapshot_sender_payload_v1 *payload,
    worr_native_record_ref_v1 record,
    const worr_snapshot_projection_hashes_v2 *hashes,
    uint64_t snapshot_hash,
    uint64_t queue_tick,
    uint32_t encoded_bytes)
{
    payload->encoded_bytes = encoded_bytes;
    payload->record = record;
    payload->hashes = *hashes;
    payload->snapshot_hash = snapshot_hash;
    payload->queue_tick = queue_tick;
}

static bool payload_release(
    worr_native_snapshot_sender_v1 *sender, uint32_t handle)
{
    uint32_t index;
    uint32_t generation;
    worr_native_snapshot_sender_payload_v1 *payload =
        payload_find(sender->payloads, handle, &index);

    if (payload == NULL || sender->payload_occupied == 0 ||
        payload_is_tx_referenced(sender, handle) ||
        payload_is_dispatch_referenced(sender, handle) ||
        payload_is_pending(sender, index))
        return false;
    generation = payload->generation;
    memset(payload, 0, sizeof(*payload));
    payload->generation = generation;
    --sender->payload_occupied;
    if (generation ==
        WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_GENERATION_MAX) {
        payload->state_flags =
            WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_RETIRED;
        ++sender->payload_retired;
    } else {
        sender->next_payload_bank = (uint8_t)index;
    }
    saturating_increment(&sender->telemetry.payloads_released);
    return true;
}

static bool release_unreferenced_payloads(
    worr_native_snapshot_sender_v1 *sender)
{
    uint32_t handles[WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS] = {0};
    uint32_t count = 0;
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS; ++index) {
        const worr_native_snapshot_sender_payload_v1 *payload =
            &sender->payloads[index];

        if ((payload->state_flags &
             WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_OCCUPIED) == 0)
            continue;
        if (!payload_is_tx_referenced(sender, payload->handle) &&
            !payload_is_dispatch_referenced(sender, payload->handle) &&
            !payload_is_pending(sender, index))
            handles[count++] = payload->handle;
    }
    for (index = 0; index < count; ++index) {
        if (!payload_release(sender, handles[index]))
            return false;
    }
    return true;
}

static worr_native_snapshot_sender_result_v1 mixed_result(
    worr_native_carrier_mixed_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_CARRIER_MIXED_OK:
    case WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED:
    case WORR_NATIVE_CARRIER_MIXED_DISPATCH_RETIRED:
        return WORR_NATIVE_SNAPSHOT_SENDER_OK;
    case WORR_NATIVE_CARRIER_MIXED_NOT_DUE:
        return WORR_NATIVE_SNAPSHOT_SENDER_NOT_DUE;
    case WORR_NATIVE_CARRIER_MIXED_LIMIT:
        return WORR_NATIVE_SNAPSHOT_SENDER_CAPACITY;
    case WORR_NATIVE_CARRIER_MIXED_OUTPUT_TOO_SMALL:
        return WORR_NATIVE_SNAPSHOT_SENDER_OUTPUT_TOO_SMALL;
    case WORR_NATIVE_CARRIER_MIXED_WRONG_EPOCH:
        return WORR_NATIVE_SNAPSHOT_SENDER_WRONG_EPOCH;
    case WORR_NATIVE_CARRIER_MIXED_CLOCK_REGRESSION:
        return WORR_NATIVE_SNAPSHOT_SENDER_CLOCK_REGRESSION;
    case WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT:
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;
    case WORR_NATIVE_CARRIER_MIXED_INVALID_STATE:
    case WORR_NATIVE_CARRIER_MIXED_PACKET_PENDING:
    case WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION:
    case WORR_NATIVE_CARRIER_MIXED_TOKEN_EXHAUSTED:
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    default:
        return WORR_NATIVE_SNAPSHOT_SENDER_TRANSPORT_REJECTED;
    }
}

static worr_native_snapshot_sender_result_v1 promote_pending(
    worr_native_snapshot_sender_v1 *sender, uint64_t now_tick)
{
    worr_native_tx_session_v1 staged_tx;
    worr_native_tx_slot_v1
        staged_slots[WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY];
    const worr_native_snapshot_sender_payload_v1 *pending;
    uint32_t old_handle = 0;
    uint32_t message_sequence;
    worr_native_tx_result_v1 result;

    if (sender->pending_bank ==
        WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK)
        return WORR_NATIVE_SNAPSHOT_SENDER_OK;
    if ((sender->tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 ||
        (sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED) != 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;

    pending = &sender->payloads[sender->pending_bank];
    if (sender->tx.retained_count != 0)
        old_handle = sender->tx_slots[0].payload_handle;
    staged_tx = sender->tx;
    memcpy(staged_slots, sender->tx_slots, sizeof(staged_slots));
    result = Worr_NativeTxSessionEnqueueV1(
        &staged_tx, staged_slots,
        WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY,
        pending->record, WORR_NATIVE_SNAPSHOT_SENDER_PRIORITY,
        pending->handle, pending->encoded_bytes,
        minimum_datagram(
            sender->max_datagram_bytes, pending->encoded_bytes),
        now_tick, &message_sequence);
    if (result != WORR_NATIVE_TX_RETAINED &&
        result != WORR_NATIVE_TX_SUPERSEDED) {
        sender->state_flags |=
            WORR_NATIVE_SNAPSHOT_SENDER_PROMOTION_STALLED;
        saturating_increment(&sender->telemetry.promotion_stalls);
        return result == WORR_NATIVE_TX_SEQUENCE_EXHAUSTED_RESULT
                   ? WORR_NATIVE_SNAPSHOT_SENDER_SEQUENCE_LIMIT
               : result == WORR_NATIVE_TX_CLOCK_REGRESSION
                   ? WORR_NATIVE_SNAPSHOT_SENDER_CLOCK_REGRESSION
                   : WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    }

    sender->tx = staged_tx;
    memcpy(sender->tx_slots, staged_slots, sizeof(staged_slots));
    sender->pending_bank =
        WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK;
    sender->state_flags &=
        (uint16_t)~WORR_NATIVE_SNAPSHOT_SENDER_PROMOTION_STALLED;
    sender->last_tick = now_tick;
    saturating_increment(&sender->telemetry.pending_promoted);
    if (result == WORR_NATIVE_TX_RETAINED)
        saturating_increment(&sender->telemetry.snapshots_retained);
    else
        saturating_increment(&sender->telemetry.snapshots_superseded);
    if (old_handle != 0 && old_handle != pending->handle &&
        !payload_release(sender, old_handle))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    return result == WORR_NATIVE_TX_RETAINED
               ? WORR_NATIVE_SNAPSHOT_SENDER_RETAINED
               : WORR_NATIVE_SNAPSHOT_SENDER_SUPERSEDED;
}

worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderInitV1(
    worr_native_snapshot_sender_v1 *sender,
    const worr_native_session_binding_v1 *binding,
    uint32_t max_entities,
    uint16_t max_datagram_bytes,
    uint64_t now_tick)
{
    worr_native_tx_session_v1 tx;
    worr_native_tx_slot_v1
        tx_slots[WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 gate;

    if (sender == NULL || binding == NULL ||
        ranges_overlap(sender, sizeof(*sender), binding, sizeof(*binding)))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeSessionBindingValidateV1(binding) ||
        binding->negotiated_capabilities !=
            WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK ||
        max_entities == 0 ||
        max_datagram_bytes <=
            WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ||
        max_datagram_bytes >
            WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;

    memset(&tx, 0, sizeof(tx));
    memset(tx_slots, 0, sizeof(tx_slots));
    memset(&gate, 0, sizeof(gate));
    if (!Worr_NativeTxSessionInitV1(
            &tx, tx_slots,
            WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY, binding) ||
        !Worr_NativeCarrierTxGateInitV1(&gate, binding))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;

    memset(sender, 0, sizeof(*sender));
    sender->struct_size = sizeof(*sender);
    sender->schema_version =
        WORR_NATIVE_SNAPSHOT_SENDER_ABI_VERSION;
    sender->state_flags =
        WORR_NATIVE_SNAPSHOT_SENDER_INITIALIZED;
    sender->binding = *binding;
    sender->max_entities = max_entities;
    sender->max_datagram_bytes = max_datagram_bytes;
    sender->pending_bank =
        WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK;
    sender->last_tick = now_tick;
    sender->tx = tx;
    memcpy(sender->tx_slots, tx_slots, sizeof(tx_slots));
    sender->tx_gate = gate;
    return Worr_NativeSnapshotSenderValidateV1(sender)
               ? WORR_NATIVE_SNAPSHOT_SENDER_OK
               : WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
}

worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderQueueV1(
    worr_native_snapshot_sender_v1 *sender,
    const worr_snapshot_projection_view_v2 *view,
    uint64_t now_tick)
{
    worr_snapshot_projection_hashes_v2 hashes;
    worr_snapshot_id_v2 snapshot_id;
    worr_native_record_ref_v1 record;
    uint32_t encoded_bytes;
    int identity_order;
    uint32_t index;
    uint32_t handle;
    worr_native_snapshot_sender_payload_v1 *payload;

    if (sender == NULL || view == NULL ||
        ranges_overlap(sender, sizeof(*sender), view, sizeof(*view)))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeSnapshotSenderValidateV1(sender))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED) != 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED_RESULT;
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_RETIRED) != 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_RETIRED_RESULT;
    if (now_tick < sender->last_tick)
        return WORR_NATIVE_SNAPSHOT_SENDER_CLOCK_REGRESSION;
    if (Worr_NativeCodecSnapshotPreflightV1(
            view, sender->max_entities, &encoded_bytes) !=
            WORR_NATIVE_CODEC_OK ||
        !Worr_SnapshotProjectionHashesV2(
            view, sender->max_entities, &hashes))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_RECORD;

    snapshot_id = view->snapshot->snapshot_id;
    record = snapshot_record(snapshot_id);
    identity_order =
        snapshot_id_absent(sender->latest_offered_snapshot)
            ? 1
            : snapshot_id_compare(
                  snapshot_id, sender->latest_offered_snapshot);
    saturating_increment(&sender->telemetry.queue_attempts);
    if (!payload_fits_datagram_plan(
            encoded_bytes, sender->max_datagram_bytes)) {
        saturating_increment(&sender->telemetry.capacity_stalls);
        return WORR_NATIVE_SNAPSHOT_SENDER_CAPACITY;
    }
    if (identity_order < 0) {
        saturating_increment(&sender->telemetry.stale_snapshots);
        return WORR_NATIVE_SNAPSHOT_SENDER_STALE_SNAPSHOT;
    }
    if (identity_order == 0) {
        for (index = 0;
             index < WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS; ++index) {
            payload = &sender->payloads[index];
            if ((payload->state_flags &
                 WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_OCCUPIED) == 0 ||
                !record_ref_equal(payload->record, record))
                continue;
            if (payload->snapshot_hash ==
                    view->snapshot->snapshot_hash &&
                projection_hashes_equal(&payload->hashes, &hashes)) {
                saturating_increment(&sender->telemetry.duplicates);
                return WORR_NATIVE_SNAPSHOT_SENDER_DUPLICATE;
            }
            saturating_increment(&sender->telemetry.conflicts);
            return WORR_NATIVE_SNAPSHOT_SENDER_CONFLICT;
        }
        saturating_increment(&sender->telemetry.stale_snapshots);
        return WORR_NATIVE_SNAPSHOT_SENDER_STALE_SNAPSHOT;
    }

    if ((sender->tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0) {
        const bool coalescing =
            sender->pending_bank !=
            WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK;

        if (coalescing) {
            index = sender->pending_bank;
            payload = &sender->payloads[index];
            handle = payload->handle;
        } else if (!payload_select_free(sender, &index, &handle)) {
            saturating_increment(&sender->telemetry.capacity_stalls);
            return WORR_NATIVE_SNAPSHOT_SENDER_CAPACITY;
        } else {
            payload = &sender->payloads[index];
        }

        {
            size_t written = 0;
            if (Worr_NativeCodecSnapshotEncodeV1(
                    view, sender->max_entities, payload->encoded,
                    sizeof(payload->encoded), &written) !=
                    WORR_NATIVE_CODEC_OK ||
                written != encoded_bytes) {
                return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_RECORD;
            }
        }
        if (coalescing) {
            payload_replace_pending(
                payload, record, &hashes,
                view->snapshot->snapshot_hash, now_tick,
                encoded_bytes);
            saturating_increment(
                &sender->telemetry.pending_coalesced);
        } else {
            payload_install_free(
                sender, index, handle, record, &hashes,
                view->snapshot->snapshot_hash, now_tick,
                encoded_bytes);
            sender->pending_bank = (uint8_t)index;
            saturating_increment(
                &sender->telemetry.snapshots_pending);
        }
        sender->latest_offered_snapshot = snapshot_id;
        sender->last_tick = now_tick;
        return coalescing
                   ? WORR_NATIVE_SNAPSHOT_SENDER_COALESCED
                   : WORR_NATIVE_SNAPSHOT_SENDER_PENDING;
    }

    if (!payload_select_free(sender, &index, &handle)) {
        saturating_increment(&sender->telemetry.capacity_stalls);
        return WORR_NATIVE_SNAPSHOT_SENDER_CAPACITY;
    }
    payload = &sender->payloads[index];
    {
        worr_native_tx_session_v1 staged_tx;
        worr_native_tx_slot_v1
            staged_slots[WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY];
        size_t written = 0;
        uint32_t message_sequence;
        uint32_t old_handle = 0;
        worr_native_tx_result_v1 enqueued;

        if (Worr_NativeCodecSnapshotEncodeV1(
                view, sender->max_entities, payload->encoded,
                sizeof(payload->encoded), &written) !=
                WORR_NATIVE_CODEC_OK ||
            written != encoded_bytes) {
            return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_RECORD;
        }
        if (sender->tx.retained_count != 0)
            old_handle = sender->tx_slots[0].payload_handle;
        staged_tx = sender->tx;
        memcpy(staged_slots, sender->tx_slots, sizeof(staged_slots));
        enqueued = Worr_NativeTxSessionEnqueueV1(
            &staged_tx, staged_slots,
            WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY,
            record, WORR_NATIVE_SNAPSHOT_SENDER_PRIORITY,
            handle, encoded_bytes,
            minimum_datagram(
                sender->max_datagram_bytes, encoded_bytes),
            now_tick, &message_sequence);
        if (enqueued != WORR_NATIVE_TX_RETAINED &&
            enqueued != WORR_NATIVE_TX_SUPERSEDED) {
            const uint32_t generation = payload->generation;
            memset(payload, 0, sizeof(*payload));
            payload->generation = generation;
            if (enqueued ==
                WORR_NATIVE_TX_SEQUENCE_EXHAUSTED_RESULT)
                return WORR_NATIVE_SNAPSHOT_SENDER_SEQUENCE_LIMIT;
            if (enqueued == WORR_NATIVE_TX_CLOCK_REGRESSION)
                return WORR_NATIVE_SNAPSHOT_SENDER_CLOCK_REGRESSION;
            return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
        }

        sender->tx = staged_tx;
        memcpy(sender->tx_slots, staged_slots, sizeof(staged_slots));
        payload_install_free(
            sender, index, handle, record, &hashes,
            view->snapshot->snapshot_hash, now_tick,
            encoded_bytes);
        sender->latest_offered_snapshot = snapshot_id;
        sender->last_tick = now_tick;
        if (enqueued == WORR_NATIVE_TX_RETAINED) {
            saturating_increment(
                &sender->telemetry.snapshots_retained);
            return WORR_NATIVE_SNAPSHOT_SENDER_RETAINED;
        }
        saturating_increment(
            &sender->telemetry.snapshots_superseded);
        if (old_handle == 0 || old_handle == handle ||
            !payload_release(sender, old_handle))
            return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
        return WORR_NATIVE_SNAPSHOT_SENDER_SUPERSEDED;
    }
}

worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderApplyAcksV1(
    worr_native_snapshot_sender_v1 *sender,
    const void *packet,
    size_t packet_bytes,
    uint32_t *acknowledged_out)
{
    worr_native_tx_session_v1 staged_tx;
    worr_native_tx_slot_v1
        staged_slots[WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    uint32_t old_handle = 0;
    uint32_t acknowledged = 0;
    bool dispatch_aborted = false;

    if (sender == NULL || packet == NULL || packet_bytes == 0 ||
        acknowledged_out == NULL ||
        ranges_overlap(sender, sizeof(*sender), packet, packet_bytes) ||
        ranges_overlap(sender, sizeof(*sender), acknowledged_out,
                       sizeof(*acknowledged_out)) ||
        ranges_overlap(packet, packet_bytes, acknowledged_out,
                       sizeof(*acknowledged_out)))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeSnapshotSenderValidateV1(sender))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED) != 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED_RESULT;

    if (sender->tx.retained_count != 0)
        old_handle = sender->tx_slots[0].payload_handle;
    staged_tx = sender->tx;
    memcpy(staged_slots, sender->tx_slots, sizeof(staged_slots));
    staged_gate = sender->tx_gate;
    staged_dispatch = sender->dispatch;
    if (Worr_NativeCarrierSessionApplyAcksV1(
            &staged_tx, staged_slots,
            WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY,
            packet, packet_bytes, &acknowledged) !=
        WORR_NATIVE_CARRIER_SESSION_OK)
        return WORR_NATIVE_SNAPSHOT_SENDER_TRANSPORT_REJECTED;
    if (acknowledged > sender->tx.retained_count)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    /*
     * A semantic repeat may ACK an already committed snapshot between
     * fragments of a retry burst.  With no packet in an unknown handoff
     * state, the dispatch can be retired immediately instead of leaving a
     * gate whose prepared ticket no longer has a resident TX slot.
     */
    if (acknowledged != 0 &&
        (sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED) == 0 &&
        (staged_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
        staged_dispatch.payload_handle == old_handle) {
        if (Worr_NativeCarrierSessionDispatchAbortV1(
                &staged_gate, &staged_dispatch) !=
            WORR_NATIVE_CARRIER_SESSION_OK)
            return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
        dispatch_aborted = true;
    }

    sender->tx = staged_tx;
    memcpy(sender->tx_slots, staged_slots, sizeof(staged_slots));
    sender->tx_gate = staged_gate;
    sender->dispatch = staged_dispatch;
    if (acknowledged != 0 && old_handle != 0 &&
        !payload_is_dispatch_referenced(sender, old_handle) &&
        !payload_release(sender, old_handle))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if (!release_unreferenced_payloads(sender))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if (dispatch_aborted)
        (void)promote_pending(sender, sender->last_tick);
    saturating_add(
        &sender->telemetry.acknowledgements_applied,
        acknowledged);
    *acknowledged_out = acknowledged;
    return WORR_NATIVE_SNAPSHOT_SENDER_ACK_APPLIED;
}

worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderDataDuePeekV1(
    const worr_native_snapshot_sender_v1 *sender,
    uint64_t now_tick,
    uint32_t resend_interval_ticks,
    bool *due_out)
{
    worr_native_tx_send_ticket_v1 ticket;
    worr_native_tx_result_v1 selected;

    if (sender == NULL || due_out == NULL ||
        ranges_overlap(sender, sizeof(*sender), due_out, sizeof(*due_out)))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeSnapshotSenderValidateV1(sender))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if (now_tick < sender->last_tick)
        return WORR_NATIVE_SNAPSHOT_SENDER_CLOCK_REGRESSION;
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_RETIRED) != 0) {
        *due_out = false;
        return WORR_NATIVE_SNAPSHOT_SENDER_RETIRED_RESULT;
    }
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED) != 0) {
        *due_out = false;
        return WORR_NATIVE_SNAPSHOT_SENDER_OK;
    }
    if ((sender->tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0) {
        *due_out = true;
        return WORR_NATIVE_SNAPSHOT_SENDER_OK;
    }
    if (sender->tx.retained_count == 0) {
        *due_out = false;
        return (sender->state_flags &
                WORR_NATIVE_SNAPSHOT_SENDER_PROMOTION_STALLED) != 0
                   ? WORR_NATIVE_SNAPSHOT_SENDER_SEQUENCE_LIMIT
                   : WORR_NATIVE_SNAPSHOT_SENDER_NOT_DUE;
    }

    memset(&ticket, 0, sizeof(ticket));
    selected = Worr_NativeTxSessionPrepareDueV1(
        &sender->tx, sender->tx_slots,
        WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY,
        now_tick, resend_interval_ticks, &ticket);
    if (selected == WORR_NATIVE_TX_SELECTED) {
        *due_out = true;
        return WORR_NATIVE_SNAPSHOT_SENDER_OK;
    }
    if (selected == WORR_NATIVE_TX_NOT_DUE) {
        *due_out = false;
        return WORR_NATIVE_SNAPSHOT_SENDER_NOT_DUE;
    }
    return selected == WORR_NATIVE_TX_CLOCK_REGRESSION
               ? WORR_NATIVE_SNAPSHOT_SENDER_CLOCK_REGRESSION
               : WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
}

worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderPrepareMixedV1(
    worr_native_snapshot_sender_v1 *sender,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    uint64_t now_tick,
    uint32_t resend_interval_ticks,
    uint32_t ack_retry_interval_ticks,
    uint16_t application_packet_budget,
    const void *legacy_packet,
    uint16_t legacy_bytes,
    void *packet_out,
    size_t packet_capacity,
    size_t *packet_bytes_out,
    uint64_t *token_out)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_mixed_token_v1 token;
    const worr_native_snapshot_sender_payload_v1 *payload;
    worr_native_carrier_mixed_result_v1 result;
    bool began = false;

    if (sender == NULL || ack_ledger == NULL || packet_out == NULL ||
        packet_bytes_out == NULL || token_out == NULL ||
        (legacy_bytes != 0 && legacy_packet == NULL) ||
        ranges_overlap(sender, sizeof(*sender), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(sender, sizeof(*sender), packet_out,
                       packet_capacity) ||
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
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeSnapshotSenderValidateV1(sender) ||
        !Worr_NativeCarrierAckLedgerValidateV1(ack_ledger))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_RETIRED) != 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_RETIRED_RESULT;
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED) != 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if (now_tick < sender->last_tick)
        return WORR_NATIVE_SNAPSHOT_SENDER_CLOCK_REGRESSION;
    if (sender->tx.retained_count == 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_NOT_DUE;

    staged_gate = sender->tx_gate;
    staged_dispatch = sender->dispatch;
    staged_ledger = *ack_ledger;
    if ((staged_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) == 0) {
        result = Worr_NativeCarrierMixedBeginV1(
            &staged_gate, &sender->tx, sender->tx_slots,
            WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY,
            now_tick, resend_interval_ticks,
            application_packet_budget, legacy_bytes,
            &staged_dispatch);
        if (result != WORR_NATIVE_CARRIER_MIXED_OK)
            return mixed_result(result);
        began = true;
    }

    payload = payload_find_const(
        sender->payloads,
        staged_dispatch.send_ticket.pre_send_slot.payload_handle);
    if (payload == NULL)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if (began &&
        Worr_NativeCarrierSessionDispatchBindPayloadV1(
            &staged_gate, &staged_dispatch, payload->handle,
            payload->encoded, payload->encoded_bytes) !=
            WORR_NATIVE_CARRIER_SESSION_OK)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;

    memset(&token, 0, sizeof(token));
    result = Worr_NativeCarrierMixedPreparePacketV1(
        &staged_gate, &sender->tx, sender->tx_slots,
        WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY,
        &staged_dispatch, &staged_ledger, now_tick,
        ack_retry_interval_ticks, payload->handle,
        payload->encoded, payload->encoded_bytes,
        legacy_packet, legacy_bytes, packet_out,
        packet_capacity, packet_bytes_out, &token);
    if (result != WORR_NATIVE_CARRIER_MIXED_OK)
        return mixed_result(result);

    sender->tx_gate = staged_gate;
    sender->dispatch = staged_dispatch;
    sender->mixed_token = token;
    sender->state_flags |=
        WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED;
    sender->last_tick = now_tick;
    *ack_ledger = staged_ledger;
    *token_out = token.dispatch_token_id;
    saturating_increment(&sender->telemetry.packets_prepared);
    return WORR_NATIVE_SNAPSHOT_SENDER_OK;
}

worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderConfirmMixedV1(
    worr_native_snapshot_sender_v1 *sender,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    uint64_t handoff_tick,
    const void *packet,
    size_t packet_bytes)
{
    worr_native_tx_session_v1 staged_tx;
    worr_native_tx_slot_v1
        staged_slots[WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_mixed_result_v1 result;
    worr_native_snapshot_sender_result_v1 promoted;
    const bool retry =
        sender != NULL &&
        sender->dispatch.send_ticket.pre_send_slot.send_attempts != 0;
    bool terminal;

    if (sender == NULL || ack_ledger == NULL || packet == NULL ||
        packet_bytes == 0 ||
        ranges_overlap(sender, sizeof(*sender), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(sender, sizeof(*sender), packet, packet_bytes) ||
        ranges_overlap(ack_ledger, sizeof(*ack_ledger), packet,
                       packet_bytes))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeSnapshotSenderValidateV1(sender) ||
        (sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED) == 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if (handoff_tick < sender->last_tick)
        return WORR_NATIVE_SNAPSHOT_SENDER_CLOCK_REGRESSION;

    staged_tx = sender->tx;
    memcpy(staged_slots, sender->tx_slots, sizeof(staged_slots));
    staged_gate = sender->tx_gate;
    staged_dispatch = sender->dispatch;
    staged_ledger = *ack_ledger;
    result = Worr_NativeCarrierMixedConfirmPacketV1(
        &staged_gate, &staged_tx, staged_slots,
        WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY,
        &staged_dispatch, &staged_ledger,
        &sender->mixed_token, handoff_tick,
        packet, packet_bytes);
    if (result != WORR_NATIVE_CARRIER_MIXED_OK &&
        result != WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED &&
        result != WORR_NATIVE_CARRIER_MIXED_DISPATCH_RETIRED)
        return mixed_result(result);

    terminal =
        result == WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED ||
        result == WORR_NATIVE_CARRIER_MIXED_DISPATCH_RETIRED;
    sender->tx = staged_tx;
    memcpy(sender->tx_slots, staged_slots, sizeof(staged_slots));
    sender->tx_gate = staged_gate;
    sender->dispatch = staged_dispatch;
    *ack_ledger = staged_ledger;
    sender->state_flags &=
        (uint16_t)~WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED;
    memset(&sender->mixed_token, 0, sizeof(sender->mixed_token));
    sender->last_tick = handoff_tick;
    saturating_increment(&sender->telemetry.packets_confirmed);
    if (result == WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED) {
        saturating_increment(
            retry ? &sender->telemetry.retries
                  : &sender->telemetry.first_sends);
    }
    if (!terminal)
        return WORR_NATIVE_SNAPSHOT_SENDER_OK;
    if (!release_unreferenced_payloads(sender))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    promoted = promote_pending(sender, handoff_tick);
    if (promoted == WORR_NATIVE_SNAPSHOT_SENDER_RETAINED ||
        promoted == WORR_NATIVE_SNAPSHOT_SENDER_SUPERSEDED ||
        promoted == WORR_NATIVE_SNAPSHOT_SENDER_OK)
        return WORR_NATIVE_SNAPSHOT_SENDER_OK;
    return promoted;
}

worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderRejectMixedV1(
    worr_native_snapshot_sender_v1 *sender,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const void *packet,
    size_t packet_bytes)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_mixed_result_v1 result;
    worr_native_snapshot_sender_result_v1 promoted;

    if (sender == NULL || ack_ledger == NULL || packet == NULL ||
        packet_bytes == 0 ||
        ranges_overlap(sender, sizeof(*sender), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(sender, sizeof(*sender), packet, packet_bytes) ||
        ranges_overlap(ack_ledger, sizeof(*ack_ledger), packet,
                       packet_bytes))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeSnapshotSenderValidateV1(sender) ||
        (sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED) == 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;

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
        (uint16_t)~WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED;
    memset(&sender->mixed_token, 0, sizeof(sender->mixed_token));
    saturating_increment(&sender->telemetry.packets_rejected);
    if (!release_unreferenced_payloads(sender))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    promoted = promote_pending(sender, sender->last_tick);
    if (promoted == WORR_NATIVE_SNAPSHOT_SENDER_RETAINED ||
        promoted == WORR_NATIVE_SNAPSHOT_SENDER_SUPERSEDED ||
        promoted == WORR_NATIVE_SNAPSHOT_SENDER_OK)
        return WORR_NATIVE_SNAPSHOT_SENDER_OK;
    return promoted;
}

worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderRetireV1(
    worr_native_snapshot_sender_v1 *sender)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    uint32_t pending_handle = 0;

    if (sender == NULL)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeSnapshotSenderValidateV1(sender))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_RETIRED) != 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_RETIRED_RESULT;
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED) != 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;

    staged_gate = sender->tx_gate;
    staged_dispatch = sender->dispatch;
    if ((staged_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
        Worr_NativeCarrierSessionDispatchAbortV1(
            &staged_gate, &staged_dispatch) !=
            WORR_NATIVE_CARRIER_SESSION_OK)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if (sender->pending_bank !=
        WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK)
        pending_handle =
            sender->payloads[sender->pending_bank].handle;
    sender->tx_gate = staged_gate;
    sender->dispatch = staged_dispatch;
    sender->pending_bank =
        WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK;
    sender->state_flags &=
        (uint16_t)~WORR_NATIVE_SNAPSHOT_SENDER_PROMOTION_STALLED;
    if (pending_handle != 0 &&
        !payload_release(sender, pending_handle))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if (!release_unreferenced_payloads(sender))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    sender->state_flags |=
        WORR_NATIVE_SNAPSHOT_SENDER_RETIRED;
    return WORR_NATIVE_SNAPSHOT_SENDER_OK;
}

worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderCancelV1(
    worr_native_snapshot_sender_v1 *sender,
    worr_native_snapshot_sender_cancel_report_v1 *report_out)
{
    worr_native_tx_session_v1 staged_tx;
    worr_native_tx_slot_v1
        staged_slots[WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_snapshot_sender_cancel_report_v1 report;
    uint32_t handles[WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS] = {0};
    uint32_t handle_count = 0;
    uint32_t cancelled_tx = 0;
    uint32_t index;

    if (sender == NULL || report_out == NULL ||
        ranges_overlap(sender, sizeof(*sender), report_out,
                       sizeof(*report_out)))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT;
    if (!Worr_NativeSnapshotSenderValidateV1(sender))
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED) != 0)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;

    memset(&report, 0, sizeof(report));
    if ((sender->state_flags &
         WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED) != 0) {
        *report_out = report;
        return WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED_RESULT;
    }

    staged_gate = sender->tx_gate;
    staged_dispatch = sender->dispatch;
    if ((staged_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
        Worr_NativeCarrierSessionDispatchAbortV1(
            &staged_gate, &staged_dispatch) !=
            WORR_NATIVE_CARRIER_SESSION_OK)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    staged_tx = sender->tx;
    memcpy(staged_slots, sender->tx_slots, sizeof(staged_slots));
    if (Worr_NativeTxSessionCancelRetainedV1(
            &staged_tx, staged_slots,
            WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY,
            &cancelled_tx) != WORR_NATIVE_TX_CANCELLED)
        return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS; ++index) {
        if ((sender->payloads[index].state_flags &
             WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_OCCUPIED) != 0)
            handles[handle_count++] =
                sender->payloads[index].handle;
    }
    report.retained_messages = cancelled_tx;
    report.pending_snapshots =
        sender->pending_bank ==
                WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK
            ? 0u
            : 1u;
    report.payloads_released = handle_count;

    sender->tx = staged_tx;
    memcpy(sender->tx_slots, staged_slots, sizeof(staged_slots));
    sender->tx_gate = staged_gate;
    sender->dispatch = staged_dispatch;
    sender->pending_bank =
        WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK;
    for (index = 0; index < handle_count; ++index) {
        if (!payload_release(sender, handles[index]))
            return WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
    }
    sender->state_flags &=
        (uint16_t)~WORR_NATIVE_SNAPSHOT_SENDER_PROMOTION_STALLED;
    sender->state_flags |=
        WORR_NATIVE_SNAPSHOT_SENDER_RETIRED |
        WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED;
    *report_out = report;
    return Worr_NativeSnapshotSenderValidateV1(sender)
               ? WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED_RESULT
               : WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE;
}

bool Worr_NativeSnapshotSenderGetStatusV1(
    const worr_native_snapshot_sender_v1 *sender,
    worr_native_snapshot_sender_status_v1 *status_out)
{
    worr_native_snapshot_sender_status_v1 status;
    const worr_native_snapshot_sender_payload_v1 *active = NULL;

    if (sender == NULL || status_out == NULL ||
        ranges_overlap(sender, sizeof(*sender), status_out,
                       sizeof(*status_out)) ||
        !Worr_NativeSnapshotSenderValidateV1(sender))
        return false;

    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    status.schema_version =
        WORR_NATIVE_SNAPSHOT_SENDER_ABI_VERSION;
    status.state_flags = sender->state_flags;
    status.transport_epoch = sender->binding.transport_epoch;
    status.retained_messages =
        (uint8_t)sender->tx.retained_count;
    status.payload_occupied = sender->payload_occupied;
    status.connection_owner_id =
        sender->binding.connection_owner_id;
    status.last_tick = sender->last_tick;
    status.telemetry = sender->telemetry;

    if (sender->tx.retained_count != 0) {
        status.active_message_sequence =
            sender->tx_slots[0].message_sequence;
        status.active_fragment_count =
            sender->tx_slots[0].fragment_count;
        active = payload_find_const(
            sender->payloads,
            sender->tx_slots[0].payload_handle);
    } else if ((sender->tx_gate.state_flags &
                WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0) {
        status.active_message_sequence =
            sender->dispatch.send_ticket.pre_send_slot
                .message_sequence;
        status.active_fragment_count =
            sender->dispatch.send_ticket.pre_send_slot
                .fragment_count;
        active = payload_find_const(
            sender->payloads, sender->dispatch.payload_handle);
    }
    if (active != NULL) {
        status.active_snapshot.epoch =
            active->record.object_epoch;
        status.active_snapshot.sequence =
            active->record.object_sequence;
        status.active_payload_bytes = active->encoded_bytes;
    }
    if (sender->pending_bank !=
        WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK) {
        const worr_native_snapshot_sender_payload_v1 *pending =
            &sender->payloads[sender->pending_bank];

        status.pending_snapshot.epoch =
            pending->record.object_epoch;
        status.pending_snapshot.sequence =
            pending->record.object_sequence;
        status.pending_payload_bytes = pending->encoded_bytes;
    }
    *status_out = status;
    return true;
}
