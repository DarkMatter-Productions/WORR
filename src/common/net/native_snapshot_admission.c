/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_snapshot_admission.h"

#include "common/net/native_carrier.h"
#include "common/net/native_carrier_session.h"
#include "native_carrier_ack_internal.h"

#include <string.h>

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes)
{
    uintptr_t left_begin;
    uintptr_t right_begin;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left == NULL || right == NULL || left_bytes == 0 ||
        right_bytes == 0) {
        return false;
    }
    left_begin = (uintptr_t)left;
    right_begin = (uintptr_t)right;
    if (left_bytes > UINTPTR_MAX - left_begin ||
        right_bytes > UINTPTR_MAX - right_begin) {
        return true;
    }
    left_end = left_begin + left_bytes;
    right_end = right_begin + right_bytes;
    return left_begin < right_end && right_begin < left_end;
}

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

static bool expectation_parity_equal(
    const worr_snapshot_projection_hashes_v2 *left,
    const worr_snapshot_projection_hashes_v2 *right)
{
    return left->struct_size == right->struct_size &&
           left->schema_version == right->schema_version &&
           left->legacy_parity_hash == right->legacy_parity_hash &&
           left->semantic_player_hash == right->semantic_player_hash &&
           left->semantic_entity_hash == right->semantic_entity_hash &&
           left->semantic_area_hash == right->semantic_area_hash &&
           left->semantic_event_hash == right->semantic_event_hash;
}

static bool consumer_valid(
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

static bool scratch_valid(
    const worr_native_snapshot_decode_storage_v1 *scratch)
{
    return scratch != NULL &&
           scratch->struct_size == sizeof(*scratch) &&
           scratch->schema_version ==
               WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION &&
           scratch->reserved0 == 0 && scratch->reserved1 == 0 &&
           scratch->snapshot != NULL && scratch->player != NULL &&
           scratch->entity_capacity <=
               WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES &&
           scratch->area_capacity <=
               WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES &&
           scratch->event_ref_capacity <=
               WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS &&
           ((scratch->entity_capacity == 0) ==
            (scratch->entities == NULL)) &&
           ((scratch->area_capacity == 0) ==
            (scratch->area_bytes == NULL)) &&
           ((scratch->event_ref_capacity == 0) ==
            (scratch->event_refs == NULL));
}

static bool expectation_valid(
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

static bool status_header_valid(
    const worr_cgame_snapshot_timeline_status_v2 *status)
{
    const uint32_t known_receipt_flags =
        WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED |
        WORR_CGAME_SNAPSHOT_RECEIPT_EVENT_FENCE_ACCEPTED;

    return status != NULL &&
           status->struct_size == sizeof(*status) &&
           status->api_version ==
               WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION &&
           status->active_epoch != 0 &&
           (status->receipt_flags & ~known_receipt_flags) == 0 &&
           status->timeline.struct_size == sizeof(status->timeline) &&
           status->timeline.schema_version ==
               WORR_SNAPSHOT_TIMELINE_VERSION;
}

static bool status_active_epoch(
    const worr_cgame_snapshot_timeline_status_v2 *status,
    uint32_t snapshot_epoch)
{
    return status_header_valid(status) &&
           status->active_epoch == snapshot_epoch;
}

static bool status_exact_receipt(
    const worr_cgame_snapshot_timeline_status_v2 *status,
    worr_snapshot_id_v2 snapshot_id,
    uint64_t snapshot_hash,
    uint64_t receive_time_us,
    const worr_snapshot_projection_hashes_v2 *hashes)
{
    const uint32_t required_flags =
        WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED |
        WORR_CGAME_SNAPSHOT_RECEIPT_EVENT_FENCE_ACCEPTED;

    return status_active_epoch(status, snapshot_id.epoch) &&
           (status->receipt_flags & required_flags) == required_flags &&
           snapshot_id_equal(status->last_snapshot_id, snapshot_id) &&
           status->last_snapshot_hash == snapshot_hash &&
           status->last_receive_time_us == receive_time_us &&
           status->last_endpoint_hash == hashes->endpoint_hash &&
           status->last_legacy_parity_hash ==
               hashes->legacy_parity_hash;
}

static bool state_storage_distinct(
    const worr_native_snapshot_admission_state_v1 *state,
    const worr_native_session_binding_v1 *binding,
    const worr_native_rx_session_v1 *session,
    const worr_native_rx_slot_v1 *slots,
    size_t slots_bytes,
    const void *payload_arena,
    size_t payload_arena_bytes,
    const worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_rx_message_v1 *message,
    const worr_native_snapshot_decode_storage_v1 *scratch,
    const worr_native_snapshot_expectation_v1 *expectation,
    const worr_native_snapshot_consumer_v1 *consumer)
{
    const void *objects[] = {
        state, binding, session, slots, payload_arena, ack_ledger,
        message, scratch, expectation, consumer,
    };
    const size_t sizes[] = {
        sizeof(*state), sizeof(*binding), sizeof(*session), slots_bytes,
        payload_arena_bytes, sizeof(*ack_ledger), sizeof(*message),
        sizeof(*scratch), sizeof(*expectation), sizeof(*consumer),
    };
    const void *scratch_regions[] = {
        scratch->snapshot,
        scratch->player,
        scratch->entities,
        scratch->area_bytes,
        scratch->event_refs,
    };
    const size_t scratch_sizes[] = {
        sizeof(*scratch->snapshot),
        sizeof(*scratch->player),
        (size_t)scratch->entity_capacity * sizeof(*scratch->entities),
        scratch->area_capacity,
        (size_t)scratch->event_ref_capacity *
            sizeof(*scratch->event_refs),
    };
    size_t left;
    size_t right;

    for (left = 0; left < sizeof(objects) / sizeof(objects[0]); ++left) {
        if (objects[left] == NULL || sizes[left] == 0)
            return false;
        for (right = left + 1;
             right < sizeof(objects) / sizeof(objects[0]); ++right) {
            if (ranges_overlap(objects[left], sizes[left],
                               objects[right], sizes[right])) {
                return false;
            }
        }
    }
    for (left = 0;
         left < sizeof(scratch_regions) / sizeof(scratch_regions[0]);
         ++left) {
        if (scratch_sizes[left] == 0)
            continue;
        if (scratch_regions[left] == NULL)
            return false;
        for (right = 0;
             right < sizeof(objects) / sizeof(objects[0]); ++right) {
            if (ranges_overlap(scratch_regions[left], scratch_sizes[left],
                               objects[right], sizes[right])) {
                return false;
            }
        }
    }
    if (consumer->opaque != NULL) {
        for (left = 0;
             left < sizeof(objects) / sizeof(objects[0]); ++left) {
            if (ranges_overlap(consumer->opaque, 1,
                               objects[left], sizes[left])) {
                return false;
            }
        }
        for (left = 0;
             left < sizeof(scratch_regions) / sizeof(scratch_regions[0]);
             ++left) {
            if (ranges_overlap(consumer->opaque, 1,
                               scratch_regions[left],
                               scratch_sizes[left])) {
                return false;
            }
        }
    }
    return true;
}

static bool repeat_storage_distinct(
    const worr_native_snapshot_admission_state_v1 *state,
    const worr_native_session_binding_v1 *binding,
    const worr_native_rx_session_v1 *session,
    const worr_native_rx_slot_v1 *slots,
    size_t slots_bytes,
    const void *payload_arena,
    size_t payload_arena_bytes,
    const void *packet,
    size_t packet_bytes,
    const worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_snapshot_consumer_v1 *consumer)
{
    const void *objects[] = {
        state, binding, session, slots, payload_arena,
        packet, ack_ledger, consumer,
    };
    const size_t sizes[] = {
        sizeof(*state), sizeof(*binding), sizeof(*session), slots_bytes,
        payload_arena_bytes, packet_bytes, sizeof(*ack_ledger),
        sizeof(*consumer),
    };
    size_t left;
    size_t right;

    for (left = 0; left < sizeof(objects) / sizeof(objects[0]); ++left) {
        if (objects[left] == NULL || sizes[left] == 0)
            return false;
        for (right = left + 1;
             right < sizeof(objects) / sizeof(objects[0]); ++right) {
            if (ranges_overlap(objects[left], sizes[left],
                               objects[right], sizes[right])) {
                return false;
            }
        }
    }
    if (consumer->opaque != NULL) {
        for (left = 0;
             left < sizeof(objects) / sizeof(objects[0]); ++left) {
            if (ranges_overlap(consumer->opaque, 1,
                               objects[left], sizes[left])) {
                return false;
            }
        }
    }
    return true;
}

static bool message_matches_complete_slot(
    const worr_native_rx_session_v1 *session,
    const worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    size_t payload_arena_bytes,
    const worr_native_rx_message_v1 *message)
{
    const worr_native_rx_slot_v1 *slot;
    const worr_native_envelope_reassembly_v1 *reassembly;
    const size_t expected_offset =
        (size_t)message->slot_index * session->payload_stride;

    if (message->struct_size != sizeof(*message) ||
        message->schema_version != WORR_NATIVE_SESSION_ABI_VERSION ||
        message->reserved0 != 0 || message->reserved1 != 0 ||
        message->slot_index >= slot_capacity ||
        message->transport_epoch != session->transport_epoch ||
        message->connection_owner_id != session->connection_owner_id ||
        message->message_sequence == 0 || message->payload_bytes == 0 ||
        message->payload_bytes > session->payload_stride ||
        expected_offset > UINT32_MAX ||
        message->payload_offset != (uint32_t)expected_offset ||
        message->payload_offset > payload_arena_bytes ||
        message->payload_bytes >
            payload_arena_bytes - message->payload_offset) {
        return false;
    }
    slot = &slots[message->slot_index];
    reassembly = &slot->reassembly;
    return (slot->state_flags &
            (WORR_NATIVE_RX_SLOT_OCCUPIED |
             WORR_NATIVE_RX_SLOT_COMPLETE)) ==
               (WORR_NATIVE_RX_SLOT_OCCUPIED |
                WORR_NATIVE_RX_SLOT_COMPLETE) &&
           record_ref_equal(message->record, reassembly->record) &&
           message->transport_epoch == reassembly->transport_epoch &&
           message->message_sequence == reassembly->message_sequence &&
           message->payload_bytes == reassembly->total_payload_bytes &&
           message->payload_crc32 == reassembly->payload_crc32;
}

static bool binding_snapshot_negotiated(
    const worr_native_session_binding_v1 *binding)
{
    return (binding->negotiated_capabilities &
            WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK) ==
           WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK;
}

bool Worr_NativeSnapshotAdmissionValidateV1(
    const worr_native_snapshot_admission_state_v1 *state)
{
    const uint16_t known_flags =
        WORR_NATIVE_SNAPSHOT_ADMISSION_INITIALIZED |
        WORR_NATIVE_SNAPSHOT_ADMISSION_ACTIVE |
        WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME |
        WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED;
    const bool active =
        state != NULL &&
        (state->state_flags &
         WORR_NATIVE_SNAPSHOT_ADMISSION_ACTIVE) != 0;
    const bool quarantined =
        state != NULL &&
        (state->state_flags &
         WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED) != 0;

    if (state == NULL ||
        state->struct_size != sizeof(*state) ||
        state->schema_version !=
            WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION ||
        (state->state_flags & ~known_flags) != 0 ||
        (state->state_flags &
         WORR_NATIVE_SNAPSHOT_ADMISSION_INITIALIZED) == 0 ||
        state->transport_epoch == 0 ||
        state->connection_owner_id == 0 ||
        state->mutation_generation == 0 ||
        state->reserved0 != 0 ||
        (active != (state->active_snapshot_epoch != 0)) ||
        state->highest_snapshot_epoch < state->active_snapshot_epoch ||
        (quarantined &&
         (state->state_flags &
          WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME) == 0)) {
        return false;
    }
    if (!active) {
        return state->highest_snapshot_epoch == 0 &&
               snapshot_id_absent(state->last_accepted_snapshot) &&
               state->last_endpoint_hash == 0 &&
               state->last_legacy_parity_hash == 0 &&
               state->last_snapshot_hash == 0 &&
               state->last_receive_time_us == 0 &&
               !quarantined;
    }
    if (quarantined) {
        return snapshot_id_absent(state->last_accepted_snapshot) &&
               state->last_endpoint_hash == 0 &&
               state->last_legacy_parity_hash == 0 &&
               state->last_snapshot_hash == 0 &&
               state->last_receive_time_us == 0;
    }
    return Worr_SnapshotIdValidV2(
               state->last_accepted_snapshot, false) &&
           state->last_accepted_snapshot.epoch ==
               state->active_snapshot_epoch;
}

bool Worr_NativeSnapshotAdmissionInitV1(
    worr_native_snapshot_admission_state_v1 *state_out,
    const worr_native_session_binding_v1 *binding)
{
    worr_native_snapshot_admission_state_v1 state;

    if (state_out == NULL || binding == NULL ||
        ranges_overlap(state_out, sizeof(*state_out),
                       binding, sizeof(*binding)) ||
        !Worr_NativeSessionBindingValidateV1(binding)) {
        return false;
    }
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    state.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    state.state_flags =
        WORR_NATIVE_SNAPSHOT_ADMISSION_INITIALIZED;
    state.transport_epoch = binding->transport_epoch;
    state.mutation_generation = 1;
    state.connection_owner_id = binding->connection_owner_id;
    *state_out = state;
    return true;
}

bool Worr_NativeSnapshotAdmissionRequireKeyframeV1(
    worr_native_snapshot_admission_state_v1 *state)
{
    if (!Worr_NativeSnapshotAdmissionValidateV1(state) ||
        state->mutation_generation == UINT64_MAX) {
        return false;
    }
    if ((state->state_flags &
         WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME) == 0) {
        state->state_flags |=
            WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME;
        increment_saturated(&state->keyframe_requests);
    }
    ++state->mutation_generation;
    return true;
}

static void note_rejection(
    worr_native_snapshot_admission_state_v1 *state,
    uint64_t *counter,
    bool require_keyframe)
{
    increment_saturated(counter);
    if (require_keyframe &&
        (state->state_flags &
         WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME) == 0) {
        state->state_flags |=
            WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME;
        increment_saturated(&state->keyframe_requests);
    }
    ++state->mutation_generation;
}

static void quarantine_consumer(
    worr_native_snapshot_admission_state_v1 *state,
    uint32_t snapshot_epoch,
    uint64_t receive_time_us,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_snapshot_consumer_v1 *consumer)
{
    consumer->Reset(consumer->opaque, snapshot_epoch,
                    WORR_CGAME_SNAPSHOT_RESET_HARD_RESYNC,
                    receive_time_us);
    (void)Worr_NativeCarrierAckRetireSemanticReceiptsInternalV1(
        ack_ledger);
    state->state_flags |=
        WORR_NATIVE_SNAPSHOT_ADMISSION_ACTIVE |
        WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME |
        WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED;
    state->active_snapshot_epoch = snapshot_epoch;
    if (state->highest_snapshot_epoch < snapshot_epoch)
        state->highest_snapshot_epoch = snapshot_epoch;
    memset(&state->last_accepted_snapshot, 0,
           sizeof(state->last_accepted_snapshot));
    state->last_endpoint_hash = 0;
    state->last_legacy_parity_hash = 0;
    state->last_snapshot_hash = 0;
    state->last_receive_time_us = 0;
    increment_saturated(&state->consumer_resyncs);
    increment_saturated(&state->keyframe_requests);
    ++state->mutation_generation;
}

static worr_native_snapshot_admission_result_v1 codec_failure_result(
    worr_native_codec_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_CODEC_CAPACITY:
    case WORR_NATIVE_CODEC_OUTPUT_TOO_SMALL:
    case WORR_NATIVE_CODEC_LIMIT:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_CAPACITY;
    case WORR_NATIVE_CODEC_UNSUPPORTED:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_UNSUPPORTED;
    case WORR_NATIVE_CODEC_INVALID_ARGUMENT:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_ARGUMENT;
    case WORR_NATIVE_CODEC_MALFORMED:
    case WORR_NATIVE_CODEC_INVALID_RECORD:
    case WORR_NATIVE_CODEC_CORRUPT:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_MALFORMED;
    default:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_STATE;
    }
}

static worr_native_snapshot_admission_result_v1 commit_failure_result(
    worr_native_rx_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_RX_WRONG_EPOCH:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_WRONG_EPOCH;
    case WORR_NATIVE_RX_STALE_SNAPSHOT:
    case WORR_NATIVE_RX_STALE_REPLAY:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_STALE;
    case WORR_NATIVE_RX_NOT_COMPLETE:
    case WORR_NATIVE_RX_NOT_FOUND:
    case WORR_NATIVE_RX_CAPACITY:
    case WORR_NATIVE_RX_STORAGE_CAPACITY:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_RETRY_UNCOMMITTED;
    default:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_STATE;
    }
}

worr_native_snapshot_admission_result_v1
Worr_NativeSnapshotAdmissionCommitCompletedV1(
    worr_native_snapshot_admission_state_v1 *state,
    const worr_native_session_binding_v1 *binding,
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    const void *payload_arena,
    size_t payload_arena_bytes,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_rx_message_v1 *message,
    uint32_t max_entities,
    uint64_t receive_time_us,
    const worr_native_snapshot_decode_storage_v1 *scratch,
    const worr_native_snapshot_expectation_v1 *expectation,
    const worr_native_snapshot_consumer_v1 *consumer)
{
    worr_native_snapshot_admission_state_v1 staged_state;
    worr_native_rx_session_v1 staged_session;
    worr_native_rx_slot_v1
        staged_slots[WORR_NATIVE_SESSION_MAX_RX_SLOTS];
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 codec_ref;
    worr_snapshot_projection_view_v2 view;
    worr_snapshot_projection_hashes_v2 hashes;
    worr_cgame_snapshot_timeline_status_v2 before_status;
    worr_cgame_snapshot_timeline_status_v2 after_status;
    worr_native_codec_result_v1 codec_result;
    worr_native_rx_result_v1 commit_result;
    const uint8_t *payload;
    const size_t slots_bytes =
        (size_t)slot_capacity * sizeof(*slots);
    bool new_snapshot_epoch;
    bool reset_consumer;
    uint32_t reset_reason;

    if (state == NULL || binding == NULL || session == NULL ||
        slots == NULL || payload_arena == NULL || ack_ledger == NULL ||
        message == NULL || scratch == NULL || expectation == NULL ||
        consumer == NULL || slot_capacity == 0 ||
        slot_capacity > WORR_NATIVE_SESSION_MAX_RX_SLOTS ||
        payload_arena_bytes == 0 || max_entities == 0 ||
        !scratch_valid(scratch) || !expectation_valid(expectation) ||
        !consumer_valid(consumer) ||
        !state_storage_distinct(
            state, binding, session, slots, slots_bytes,
            payload_arena, payload_arena_bytes, ack_ledger, message,
            scratch, expectation, consumer)) {
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_ARGUMENT;
    }
    if (!Worr_NativeSnapshotAdmissionValidateV1(state) ||
        !Worr_NativeSessionBindingValidateV1(binding) ||
        !Worr_NativeRxSessionValidateV1(
            session, slots, slot_capacity) ||
        !Worr_NativeCarrierAckLedgerValidateV1(ack_ledger) ||
        state->mutation_generation == UINT64_MAX ||
        ack_ledger->mutation_generation == UINT64_MAX ||
        (ack_ledger->state_flags &
         WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0 ||
        state->connection_owner_id != binding->connection_owner_id ||
        state->connection_owner_id != session->connection_owner_id ||
        state->connection_owner_id != ack_ledger->connection_owner_id ||
        state->transport_epoch != binding->transport_epoch ||
        state->transport_epoch != session->transport_epoch ||
        state->transport_epoch != ack_ledger->transport_epoch ||
        payload_arena_bytes !=
            (size_t)slot_capacity * session->payload_stride ||
        !message_matches_complete_slot(
            session, slots, slot_capacity, payload_arena_bytes,
            message)) {
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_STATE;
    }
    if (!binding_snapshot_negotiated(binding))
        return WORR_NATIVE_SNAPSHOT_ADMISSION_NOT_NEGOTIATED;

    payload = (const uint8_t *)payload_arena +
              message->payload_offset;
    codec_result = Worr_NativeCodecInspectV1(
        payload, message->payload_bytes, &info);
    if (codec_result != WORR_NATIVE_CODEC_OK) {
        note_rejection(state, &state->codec_rejections, true);
        return codec_failure_result(codec_result);
    }
    if (!Worr_NativeCodecInfoRecordRefV1(&info, &codec_ref) ||
        !record_ref_equal(codec_ref, message->record)) {
        note_rejection(state, &state->codec_rejections, true);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_MALFORMED;
    }
    if (info.record_class != WORR_NATIVE_RECORD_SNAPSHOT_V1) {
        note_rejection(state, &state->codec_rejections, false);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_UNSUPPORTED;
    }

    memset(&view, 0, sizeof(view));
    memset(&hashes, 0, sizeof(hashes));
    codec_result = Worr_NativeCodecSnapshotDecodeProjectionV1(
        payload, message->payload_bytes, max_entities,
        scratch->snapshot, scratch->player,
        info.range_counts[0] == 0 ? NULL : scratch->entities,
        info.range_counts[0] == 0 ? 0 : scratch->entity_capacity,
        info.range_counts[1] == 0 ? NULL : scratch->area_bytes,
        info.range_counts[1] == 0 ? 0 : scratch->area_capacity,
        info.range_counts[2] == 0 ? NULL : scratch->event_refs,
        info.range_counts[2] == 0 ? 0 : scratch->event_ref_capacity,
        &view, &hashes);
    if (codec_result != WORR_NATIVE_CODEC_OK) {
        note_rejection(state, &state->codec_rejections, true);
        return codec_failure_result(codec_result);
    }
    if (!snapshot_id_equal(
            view.snapshot->snapshot_id, expectation->snapshot_id) ||
        !expectation_parity_equal(
            &hashes, &expectation->hashes)) {
        note_rejection(state, &state->parity_rejections, true);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_PARITY_MISMATCH;
    }

    new_snapshot_epoch =
        (state->state_flags &
         WORR_NATIVE_SNAPSHOT_ADMISSION_ACTIVE) == 0 ||
        view.snapshot->snapshot_id.epoch !=
            state->active_snapshot_epoch;
    if (state->highest_snapshot_epoch != 0 &&
        view.snapshot->snapshot_id.epoch <
            state->highest_snapshot_epoch) {
        note_rejection(state, &state->lineage_rejections, false);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_WRONG_EPOCH;
    }
    if (!new_snapshot_epoch &&
        (state->state_flags &
         WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED) == 0 &&
        snapshot_id_compare(
            view.snapshot->snapshot_id,
            state->last_accepted_snapshot) <= 0) {
        note_rejection(state, &state->lineage_rejections, false);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_STALE;
    }
    if ((state->state_flags &
         WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME) != 0 &&
        (view.snapshot->flags & WORR_SNAPSHOT_FLAG_KEYFRAME) == 0) {
        note_rejection(state, &state->lineage_rejections, false);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_REQUIRED;
    }

    staged_state = *state;
    staged_session = *session;
    memcpy(staged_slots, slots, slots_bytes);
    staged_ledger = *ack_ledger;
    commit_result = Worr_NativeCarrierSessionCommitSnapshotInternalV1(
        &staged_session, staged_slots, slot_capacity,
        message->slot_index, message->message_sequence,
        &staged_ledger);
    if (commit_result != WORR_NATIVE_RX_COMMITTED)
        return commit_failure_result(commit_result);

    reset_consumer =
        new_snapshot_epoch ||
        (state->state_flags &
         (WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME |
          WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED)) != 0;
    if (reset_consumer) {
        if ((state->state_flags &
             WORR_NATIVE_SNAPSHOT_ADMISSION_ACTIVE) == 0) {
            reset_reason = WORR_CGAME_SNAPSHOT_RESET_CONNECTION;
        } else if (new_snapshot_epoch) {
            reset_reason = WORR_CGAME_SNAPSHOT_RESET_MAP;
        } else {
            reset_reason = WORR_CGAME_SNAPSHOT_RESET_HARD_RESYNC;
        }
        consumer->Reset(consumer->opaque,
                        view.snapshot->snapshot_id.epoch,
                        reset_reason, receive_time_us);
    }

    memset(&before_status, 0, sizeof(before_status));
    if (!consumer->GetStatus(consumer->opaque, &before_status) ||
        !status_active_epoch(
            &before_status, view.snapshot->snapshot_id.epoch) ||
        before_status.admission_generation == UINT64_MAX ||
        before_status.consume_attempts == UINT64_MAX ||
        before_status.accepted == UINT64_MAX) {
        quarantine_consumer(
            state, view.snapshot->snapshot_id.epoch,
            receive_time_us, ack_ledger, consumer);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_RESYNC_UNCOMMITTED;
    }
    if (!consumer->ConsumeCanonicalSnapshot(
            consumer->opaque, &view, &hashes, receive_time_us)) {
        quarantine_consumer(
            state, view.snapshot->snapshot_id.epoch,
            receive_time_us, ack_ledger, consumer);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_RESYNC_UNCOMMITTED;
    }
    memset(&after_status, 0, sizeof(after_status));
    if (!consumer->GetStatus(consumer->opaque, &after_status) ||
        after_status.admission_generation !=
            before_status.admission_generation + 1u ||
        after_status.consume_attempts !=
            before_status.consume_attempts + 1u ||
        after_status.accepted != before_status.accepted + 1u ||
        !status_exact_receipt(
            &after_status, view.snapshot->snapshot_id,
            view.snapshot->snapshot_hash, receive_time_us, &hashes)) {
        quarantine_consumer(
            state, view.snapshot->snapshot_id.epoch,
            receive_time_us, ack_ledger, consumer);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_RESYNC_UNCOMMITTED;
    }

    staged_state.state_flags =
        WORR_NATIVE_SNAPSHOT_ADMISSION_INITIALIZED |
        WORR_NATIVE_SNAPSHOT_ADMISSION_ACTIVE;
    staged_state.active_snapshot_epoch =
        view.snapshot->snapshot_id.epoch;
    if (staged_state.highest_snapshot_epoch <
        view.snapshot->snapshot_id.epoch) {
        staged_state.highest_snapshot_epoch =
            view.snapshot->snapshot_id.epoch;
    }
    staged_state.last_accepted_snapshot =
        view.snapshot->snapshot_id;
    increment_saturated(&staged_state.accepted_snapshots);
    if ((view.snapshot->flags &
         WORR_SNAPSHOT_FLAG_KEYFRAME) != 0) {
        increment_saturated(&staged_state.accepted_keyframes);
    } else {
        increment_saturated(&staged_state.accepted_full_updates);
    }
    staged_state.last_endpoint_hash = hashes.endpoint_hash;
    staged_state.last_legacy_parity_hash =
        hashes.legacy_parity_hash;
    staged_state.last_snapshot_hash =
        view.snapshot->snapshot_hash;
    staged_state.last_receive_time_us = receive_time_us;
    ++staged_state.mutation_generation;
    if (!Worr_NativeSnapshotAdmissionValidateV1(&staged_state)) {
        quarantine_consumer(
            state, view.snapshot->snapshot_id.epoch,
            receive_time_us, ack_ledger, consumer);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_RESYNC_UNCOMMITTED;
    }

    *state = staged_state;
    *session = staged_session;
    memcpy(slots, staged_slots, slots_bytes);
    *ack_ledger = staged_ledger;
    return (view.snapshot->flags & WORR_SNAPSHOT_FLAG_KEYFRAME) != 0
               ? WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_ACCEPTED
               : WORR_NATIVE_SNAPSHOT_ADMISSION_FULL_UPDATE_ACCEPTED;
}

static bool committed_history_matches_frame(
    const worr_native_rx_session_v1 *session,
    const worr_native_envelope_frame_info_v1 *frame)
{
    uint16_t index;

    for (index = 0;
         index < WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY;
         ++index) {
        const worr_native_receipt_history_entry_v1 *identity =
            &session->history[index];

        if (identity->message_sequence != frame->message_sequence)
            continue;
        return record_ref_equal(identity->record, frame->record) &&
               identity->total_payload_bytes ==
                   frame->total_payload_bytes &&
               identity->payload_crc32 == frame->payload_crc32 &&
               identity->fragment_stride == frame->fragment_stride &&
               identity->fragment_count == frame->fragment_count &&
               identity->priority == frame->priority;
    }
    for (index = 0; index < session->snapshot_tombstone_count; ++index) {
        const worr_native_snapshot_identity_v1 *identity =
            &session->snapshot_tombstones[index];

        if (identity->state_flags !=
                WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED ||
            identity->message_sequence != frame->message_sequence) {
            continue;
        }
        return record_ref_equal(identity->record, frame->record) &&
               identity->total_payload_bytes ==
                   frame->total_payload_bytes &&
               identity->payload_crc32 == frame->payload_crc32 &&
               identity->fragment_stride == frame->fragment_stride &&
               identity->fragment_count == frame->fragment_count &&
               identity->priority == frame->priority;
    }
    return false;
}

static worr_native_snapshot_admission_result_v1 repeat_carrier_failure(
    worr_native_carrier_session_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_CARRIER_SESSION_WRONG_EPOCH:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_WRONG_EPOCH;
    case WORR_NATIVE_CARRIER_SESSION_UNSUPPORTED:
    case WORR_NATIVE_CARRIER_SESSION_WRONG_ENTRY_TYPE:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_UNSUPPORTED;
    case WORR_NATIVE_CARRIER_SESSION_NO_CARRIER:
    case WORR_NATIVE_CARRIER_SESSION_MALFORMED:
    case WORR_NATIVE_CARRIER_SESSION_CORRUPT:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_MALFORMED;
    default:
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_STATE;
    }
}

worr_native_snapshot_admission_result_v1
Worr_NativeSnapshotAdmissionRevalidateCommittedRepeatV1(
    worr_native_snapshot_admission_state_v1 *state,
    const worr_native_session_binding_v1 *binding,
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    void *payload_arena,
    size_t payload_arena_bytes,
    uint64_t now_tick,
    const void *packet,
    size_t packet_bytes,
    uint16_t entry_index,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_snapshot_consumer_v1 *consumer)
{
    worr_native_rx_session_v1 staged_session;
    worr_native_rx_slot_v1
        staged_slots[WORR_NATIVE_SESSION_MAX_RX_SLOTS];
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_view_v1 carrier;
    worr_native_envelope_frame_info_v1 frame;
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 repeat_acknowledgement;
    worr_cgame_snapshot_timeline_status_v2 status;
    worr_native_carrier_result_v1 carrier_result;
    worr_native_envelope_decode_result_v1 envelope_result;
    worr_native_carrier_session_result_v1 accepted;
    worr_native_rx_result_v1 rx_result;
    const worr_native_carrier_entry_v1 *entry;
    const size_t slots_bytes =
        (size_t)slot_capacity * sizeof(*slots);

    if (state == NULL || binding == NULL || session == NULL ||
        slots == NULL || payload_arena == NULL || packet == NULL ||
        packet_bytes == 0 || ack_ledger == NULL || consumer == NULL ||
        slot_capacity == 0 ||
        slot_capacity > WORR_NATIVE_SESSION_MAX_RX_SLOTS ||
        payload_arena_bytes == 0 ||
        !consumer_valid(consumer) ||
        !repeat_storage_distinct(
            state, binding, session, slots, slots_bytes,
            payload_arena, payload_arena_bytes, packet, packet_bytes,
            ack_ledger, consumer)) {
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_ARGUMENT;
    }
    if (!Worr_NativeSnapshotAdmissionValidateV1(state) ||
        !Worr_NativeSessionBindingValidateV1(binding) ||
        !Worr_NativeRxSessionValidateV1(
            session, slots, slot_capacity) ||
        !Worr_NativeCarrierAckLedgerValidateV1(ack_ledger) ||
        state->mutation_generation == UINT64_MAX ||
        ack_ledger->mutation_generation == UINT64_MAX ||
        (ack_ledger->state_flags &
         WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0 ||
        (state->state_flags &
         (WORR_NATIVE_SNAPSHOT_ADMISSION_ACTIVE |
          WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME |
          WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED)) !=
            WORR_NATIVE_SNAPSHOT_ADMISSION_ACTIVE ||
        state->connection_owner_id != binding->connection_owner_id ||
        state->connection_owner_id != session->connection_owner_id ||
        state->connection_owner_id != ack_ledger->connection_owner_id ||
        state->transport_epoch != binding->transport_epoch ||
        state->transport_epoch != session->transport_epoch ||
        state->transport_epoch != ack_ledger->transport_epoch ||
        payload_arena_bytes !=
            (size_t)slot_capacity * session->payload_stride) {
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_STATE;
    }
    if (!binding_snapshot_negotiated(binding))
        return WORR_NATIVE_SNAPSHOT_ADMISSION_NOT_NEGOTIATED;

    carrier_result = Worr_NativeCarrierDecodeV1(
        packet, packet_bytes, &carrier);
    if (carrier_result != WORR_NATIVE_CARRIER_OK) {
        return carrier_result == WORR_NATIVE_CARRIER_UNSUPPORTED
                   ? WORR_NATIVE_SNAPSHOT_ADMISSION_UNSUPPORTED
                   : WORR_NATIVE_SNAPSHOT_ADMISSION_MALFORMED;
    }
    if (carrier.transport_epoch != binding->transport_epoch)
        return WORR_NATIVE_SNAPSHOT_ADMISSION_WRONG_EPOCH;
    if (entry_index >= carrier.entry_count)
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_ARGUMENT;
    entry = &carrier.entries[entry_index];
    if (entry->entry_type != WORR_NATIVE_CARRIER_ENTRY_DATA_V1)
        return WORR_NATIVE_SNAPSHOT_ADMISSION_UNSUPPORTED;
    envelope_result = Worr_NativeEnvelopeDecodeV1(
        (const uint8_t *)packet + entry->data_offset,
        entry->data_bytes, &frame);
    if (envelope_result != WORR_NATIVE_ENVELOPE_DECODE_OK) {
        return envelope_result ==
                   WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED
                   ? WORR_NATIVE_SNAPSHOT_ADMISSION_UNSUPPORTED
                   : WORR_NATIVE_SNAPSHOT_ADMISSION_MALFORMED;
    }
    if (frame.transport_epoch != binding->transport_epoch)
        return WORR_NATIVE_SNAPSHOT_ADMISSION_WRONG_EPOCH;
    if (frame.record.record_class !=
            WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
        frame.record.record_schema_version !=
            WORR_SNAPSHOT_ABI_VERSION) {
        return WORR_NATIVE_SNAPSHOT_ADMISSION_UNSUPPORTED;
    }
    if (frame.record.object_epoch !=
            state->last_accepted_snapshot.epoch ||
        frame.record.object_sequence !=
            state->last_accepted_snapshot.sequence) {
        return WORR_NATIVE_SNAPSHOT_ADMISSION_CONFLICT;
    }
    if (!committed_history_matches_frame(session, &frame))
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_STATE;

    staged_session = *session;
    memcpy(staged_slots, slots, slots_bytes);
    staged_ledger = *ack_ledger;
    memset(&message, 0, sizeof(message));
    memset(&repeat_acknowledgement, 0,
           sizeof(repeat_acknowledgement));
    accepted = Worr_NativeCarrierSessionAcceptDataV1(
        &staged_session, staged_slots, slot_capacity, payload_arena,
        payload_arena_bytes, now_tick, packet, packet_bytes,
        entry_index, &rx_result, &message,
        &repeat_acknowledgement);
    if (accepted != WORR_NATIVE_CARRIER_SESSION_OK)
        return repeat_carrier_failure(accepted);
    if (rx_result != WORR_NATIVE_RX_ALREADY_COMMITTED ||
        !Worr_NativeAckRangeValidateV1(&repeat_acknowledgement) ||
        repeat_acknowledgement.transport_epoch !=
            state->transport_epoch ||
        repeat_acknowledgement.connection_owner_id !=
            state->connection_owner_id ||
        repeat_acknowledgement.first_message_sequence !=
            frame.message_sequence ||
        repeat_acknowledgement.last_message_sequence !=
            frame.message_sequence) {
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_STATE;
    }

    memset(&status, 0, sizeof(status));
    if (!consumer->GetStatus(consumer->opaque, &status) ||
        !status_exact_receipt(
            &status, state->last_accepted_snapshot,
            state->last_snapshot_hash,
            state->last_receive_time_us,
            &(worr_snapshot_projection_hashes_v2){
                .struct_size =
                    sizeof(worr_snapshot_projection_hashes_v2),
                .schema_version =
                    WORR_SNAPSHOT_PROJECTION_VERSION,
                .endpoint_hash = state->last_endpoint_hash,
                .legacy_parity_hash =
                    state->last_legacy_parity_hash,
            })) {
        quarantine_consumer(
            state, state->active_snapshot_epoch,
            state->last_receive_time_us, ack_ledger, consumer);
        return WORR_NATIVE_SNAPSHOT_ADMISSION_RESYNC_UNCOMMITTED;
    }
    if (Worr_NativeCarrierAckRefreshObservedRepeatInternalV1(
            &staged_ledger, &staged_session,
            &repeat_acknowledgement) !=
        WORR_NATIVE_CARRIER_ACK_OK) {
        return WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_STATE;
    }

    *session = staged_session;
    memcpy(slots, staged_slots, slots_bytes);
    *ack_ledger = staged_ledger;
    return WORR_NATIVE_SNAPSHOT_ADMISSION_REPEAT_REVALIDATED;
}
