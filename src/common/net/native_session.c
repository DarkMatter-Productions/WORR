/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_session.h"

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
        right_bytes > UINTPTR_MAX - right_begin) {
        return true;
    }
    left_end = left_begin + left_bytes;
    right_end = right_begin + right_bytes;
    return left_begin < right_end && right_begin < left_end;
}

static bool bytes_are_zero(const void *data, size_t bytes)
{
    const uint8_t *value = (const uint8_t *)data;
    size_t index;

    for (index = 0; index < bytes; ++index) {
        if (value[index] != 0)
            return false;
    }
    return true;
}

static void counter_increment(uint64_t *counter)
{
    if (*counter != UINT64_MAX)
        ++*counter;
}

static void counter_add(uint64_t *counter, uint64_t amount)
{
    if (amount > UINT64_MAX - *counter)
        *counter = UINT64_MAX;
    else
        *counter += amount;
}

static bool record_equal(worr_native_record_ref_v1 left,
                         worr_native_record_ref_v1 right)
{
    return left.record_class == right.record_class &&
           left.record_schema_version == right.record_schema_version &&
           left.object_epoch == right.object_epoch &&
           left.object_sequence == right.object_sequence;
}

static int object_id_compare(uint32_t left_epoch, uint32_t left_sequence,
                             uint32_t right_epoch, uint32_t right_sequence)
{
    if (left_epoch != right_epoch)
        return left_epoch < right_epoch ? -1 : 1;
    if (left_sequence != right_sequence)
        return left_sequence < right_sequence ? -1 : 1;
    return 0;
}

static uint16_t fragment_count_for(uint32_t payload_bytes,
                                   uint16_t fragment_stride)
{
    const uint32_t quotient = payload_bytes / fragment_stride;
    const uint32_t remainder = payload_bytes % fragment_stride;
    const uint32_t count = quotient + (remainder != 0 ? 1u : 0u);

    return count == 0 || count > UINT16_MAX ? 0 : (uint16_t)count;
}

bool Worr_NativeSessionBindingValidateV1(
    const worr_native_session_binding_v1 *binding)
{
    return binding != NULL && binding->struct_size == sizeof(*binding) &&
           binding->schema_version == WORR_NATIVE_SESSION_ABI_VERSION &&
           binding->reserved0 == 0 && binding->transport_epoch != 0 &&
           binding->connection_owner_id != 0 &&
           (binding->negotiated_capabilities &
            ~WORR_NET_CAP_KNOWN_MASK) == 0 &&
           (binding->negotiated_capabilities &
            WORR_NET_CAP_NATIVE_ENVELOPE_V1) != 0;
}

static bool binding_initialize_local(
    worr_native_session_binding_v1 *binding_out,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t connection_owner_id)
{
    worr_native_session_binding_v1 initialized;

    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    initialized.transport_epoch = transport_epoch;
    initialized.negotiated_capabilities = negotiated_capabilities;
    initialized.connection_owner_id = connection_owner_id;
    if (!Worr_NativeSessionBindingValidateV1(&initialized))
        return false;
    *binding_out = initialized;
    return true;
}

bool Worr_NativeSessionBindingInitV1(
    worr_native_session_binding_v1 *binding_out,
    const worr_net_capability_state_v1 *capability,
    uint64_t connection_owner_id)
{
    if (binding_out == NULL || capability == NULL ||
        connection_owner_id == 0 ||
        ranges_overlap(binding_out, sizeof(*binding_out),
                       capability, sizeof(*capability)) ||
        !Worr_NetCapabilityStateValidateV1(capability) ||
        capability->phase != WORR_NET_CAPABILITY_CONFIRMED ||
        (capability->negotiated & WORR_NET_CAP_NATIVE_ENVELOPE_V1) == 0) {
        return false;
    }

    return binding_initialize_local(
        binding_out, capability->connection_epoch, capability->negotiated,
        connection_owner_id);
}

bool Worr_NativeSessionBindingInitFromReadinessV1(
    worr_native_session_binding_v1 *binding_out,
    const worr_native_readiness_state_v1 *readiness,
    uint64_t connection_owner_id)
{
    bool role_active;

    if (binding_out == NULL || readiness == NULL ||
        connection_owner_id == 0 ||
        ranges_overlap(binding_out, sizeof(*binding_out),
                       readiness, sizeof(*readiness)) ||
        !Worr_NativeReadinessStateValidateV1(readiness)) {
        return false;
    }

    role_active =
        (readiness->role == WORR_NATIVE_READINESS_ROLE_SERVER &&
         readiness->phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE) ||
        (readiness->role == WORR_NATIVE_READINESS_ROLE_CLIENT &&
         readiness->phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    if (!role_active || readiness->transport_epoch == 0 ||
        (readiness->negotiated_capabilities &
         ~WORR_NET_CAP_KNOWN_MASK) != 0 ||
        (readiness->negotiated_capabilities &
         WORR_NET_CAP_NATIVE_ENVELOPE_V1) == 0) {
        return false;
    }

    return binding_initialize_local(
        binding_out, readiness->transport_epoch,
        readiness->negotiated_capabilities, connection_owner_id);
}

bool Worr_NativeSessionBindingInitReceiveFromReadinessV1(
    worr_native_session_binding_v1 *binding_out,
    const worr_native_readiness_state_v1 *readiness,
    uint64_t connection_owner_id,
    uint64_t now_tick)
{
    worr_native_readiness_state_v1 checked;
    const uint32_t server_data_caps =
        WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |
        WORR_NET_CAP_CANONICAL_SNAPSHOT_V2;
    bool receive_phase;

    if (binding_out == NULL || readiness == NULL ||
        connection_owner_id == 0 ||
        ranges_overlap(binding_out, sizeof(*binding_out),
                       readiness, sizeof(*readiness)) ||
        !Worr_NativeReadinessStateValidateV1(readiness)) {
        return false;
    }

    receive_phase =
        (readiness->role == WORR_NATIVE_READINESS_ROLE_SERVER &&
         (readiness->phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE ||
          (readiness->phase ==
               WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM &&
           (readiness->negotiated_capabilities &
            WORR_NET_CAP_NATIVE_ENVELOPE_V1) != 0 &&
           (readiness->negotiated_capabilities &
            server_data_caps) != 0))) ||
        (readiness->role == WORR_NATIVE_READINESS_ROLE_CLIENT &&
         readiness->phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    if (!receive_phase || readiness->transport_epoch == 0 ||
        (readiness->negotiated_capabilities &
         ~WORR_NET_CAP_KNOWN_MASK) != 0 ||
        (readiness->negotiated_capabilities &
         WORR_NET_CAP_NATIVE_ENVELOPE_V1) == 0) {
        return false;
    }

    checked = *readiness;
    if (!Worr_NativeReadinessCanReceiveNativeV1(&checked, now_tick))
        return false;
    return binding_initialize_local(
        binding_out, readiness->transport_epoch,
        readiness->negotiated_capabilities, connection_owner_id);
}

bool Worr_NativeAckRangeValidateV1(
    const worr_native_ack_range_v1 *acknowledgement)
{
    return acknowledgement != NULL &&
           acknowledgement->struct_size == sizeof(*acknowledgement) &&
           acknowledgement->schema_version ==
               WORR_NATIVE_SESSION_ABI_VERSION &&
           acknowledgement->reserved0 == 0 &&
           acknowledgement->transport_epoch != 0 &&
           acknowledgement->first_message_sequence != 0 &&
           acknowledgement->last_message_sequence >=
               acknowledgement->first_message_sequence &&
           acknowledgement->reserved1 == 0 &&
           acknowledgement->connection_owner_id != 0;
}

static bool tx_slot_storage_distinct(
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity)
{
    return !ranges_overlap(session, sizeof(*session), slots,
                           (size_t)slot_capacity * sizeof(*slots));
}

static bool tx_slot_valid(const worr_native_tx_session_v1 *session,
                          const worr_native_tx_slot_v1 *slot,
                          uint32_t sequence_highwater)
{
    if (!Worr_NativeEnvelopeRecordRefValidV1(slot->record) ||
        slot->message_sequence == 0 ||
        slot->message_sequence > sequence_highwater ||
        slot->payload_handle == 0 || slot->payload_bytes == 0 ||
        slot->payload_bytes > WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES ||
        slot->fragment_stride == 0 ||
        slot->fragment_stride >
            WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES -
                WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ||
        slot->fragment_count == 0 ||
        slot->fragment_count > WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS ||
        fragment_count_for(slot->payload_bytes, slot->fragment_stride) !=
            slot->fragment_count ||
        slot->enqueue_tick > session->last_tick ||
        slot->enqueue_dispatch > session->dispatch_count ||
        slot->state_flags != WORR_NATIVE_TX_SLOT_OCCUPIED ||
        slot->priority > WORR_NATIVE_ENVELOPE_MAX_PRIORITY ||
        slot->reserved0 != 0) {
        return false;
    }
    if (slot->send_attempts == 0)
        return slot->last_send_tick == 0;
    return slot->last_send_tick >= slot->enqueue_tick &&
           slot->last_send_tick <= session->last_tick;
}

static bool tx_terminal_cancelled(
    const worr_native_tx_session_v1 *session)
{
    return (session->state_flags & WORR_NATIVE_TX_TERMINAL_CANCELLED) != 0;
}

bool Worr_NativeTxSessionValidateV1(
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity)
{
    uint32_t sequence_highwater;
    uint16_t occupied = 0;
    int snapshot_index = -1;
    uint16_t index;

    if (session == NULL || slots == NULL ||
        slot_capacity == 0 ||
        slot_capacity > WORR_NATIVE_SESSION_MAX_TX_SLOTS ||
        !tx_slot_storage_distinct(session, slots, slot_capacity) ||
        session->struct_size != sizeof(*session) ||
        session->schema_version != WORR_NATIVE_SESSION_ABI_VERSION ||
        (session->state_flags &
         ~(WORR_NATIVE_TX_INITIALIZED |
           WORR_NATIVE_TX_SEQUENCE_EXHAUSTED |
           WORR_NATIVE_TX_TERMINAL_CANCELLED)) != 0 ||
        (session->state_flags & WORR_NATIVE_TX_INITIALIZED) == 0 ||
        session->transport_epoch == 0 ||
        session->connection_owner_id == 0 ||
        session->slot_capacity != slot_capacity ||
        session->retained_count > slot_capacity ||
        (tx_terminal_cancelled(session) && session->retained_count != 0) ||
        session->next_message_sequence == 0 || session->reserved0 != 0 ||
        ((session->latest_snapshot_epoch == 0) !=
         (session->latest_snapshot_sequence == 0))) {
        return false;
    }

    if ((session->state_flags & WORR_NATIVE_TX_SEQUENCE_EXHAUSTED) != 0) {
        if (session->next_message_sequence != UINT32_MAX)
            return false;
        sequence_highwater = UINT32_MAX;
    } else {
        sequence_highwater = session->next_message_sequence - 1u;
    }

    for (index = 0; index < slot_capacity; ++index) {
        uint16_t previous;

        if (slots[index].state_flags == 0) {
            if (!bytes_are_zero(&slots[index], sizeof(slots[index])))
                return false;
            continue;
        }
        if (!tx_slot_valid(session, &slots[index], sequence_highwater))
            return false;
        ++occupied;
        if (slots[index].record.record_class ==
            WORR_NATIVE_RECORD_SNAPSHOT_V1) {
            if (snapshot_index >= 0 ||
                object_id_compare(
                    slots[index].record.object_epoch,
                    slots[index].record.object_sequence,
                    session->latest_snapshot_epoch,
                    session->latest_snapshot_sequence) != 0) {
                return false;
            }
            snapshot_index = (int)index;
        }
        for (previous = 0; previous < index; ++previous) {
            if (slots[previous].state_flags == 0)
                continue;
            if (slots[previous].message_sequence ==
                    slots[index].message_sequence ||
                slots[previous].payload_handle == slots[index].payload_handle ||
                record_equal(slots[previous].record, slots[index].record)) {
                return false;
            }
        }
    }
    return occupied == session->retained_count;
}

static bool tx_init_arguments_valid(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_session_binding_v1 *binding)
{
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);

    return session != NULL && slots != NULL && binding != NULL &&
           slot_capacity != 0 &&
           slot_capacity <= WORR_NATIVE_SESSION_MAX_TX_SLOTS &&
           Worr_NativeSessionBindingValidateV1(binding) &&
           !ranges_overlap(session, sizeof(*session), slots, slots_bytes) &&
           !ranges_overlap(binding, sizeof(*binding), session,
                           sizeof(*session)) &&
           !ranges_overlap(binding, sizeof(*binding), slots, slots_bytes);
}

bool Worr_NativeTxSessionInitV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_session_binding_v1 *binding)
{
    worr_native_tx_session_v1 initialized;

    if (!tx_init_arguments_valid(session, slots, slot_capacity, binding))
        return false;

    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    initialized.state_flags = WORR_NATIVE_TX_INITIALIZED;
    initialized.transport_epoch = binding->transport_epoch;
    initialized.connection_owner_id = binding->connection_owner_id;
    initialized.slot_capacity = slot_capacity;
    initialized.next_message_sequence = 1;
    memset(slots, 0, (size_t)slot_capacity * sizeof(*slots));
    *session = initialized;
    return true;
}

bool Worr_NativeTxSessionAdvanceEpochV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_session_binding_v1 *binding)
{
    if (!tx_init_arguments_valid(session, slots, slot_capacity, binding) ||
        !Worr_NativeTxSessionValidateV1(session, slots, slot_capacity) ||
        binding->connection_owner_id != session->connection_owner_id ||
        binding->transport_epoch <= session->transport_epoch) {
        return false;
    }
    return Worr_NativeTxSessionInitV1(session, slots, slot_capacity, binding);
}

static bool tx_output_distinct(const worr_native_tx_session_v1 *session,
                               const worr_native_tx_slot_v1 *slots,
                               uint16_t slot_capacity,
                               const void *output,
                               size_t output_bytes)
{
    return output != NULL &&
           !ranges_overlap(output, output_bytes, session, sizeof(*session)) &&
           !ranges_overlap(output, output_bytes, slots,
                           (size_t)slot_capacity * sizeof(*slots));
}

static uint32_t tx_allocate_sequence(worr_native_tx_session_v1 *session)
{
    const uint32_t sequence = session->next_message_sequence;

    if (sequence == UINT32_MAX)
        session->state_flags |= WORR_NATIVE_TX_SEQUENCE_EXHAUSTED;
    else
        ++session->next_message_sequence;
    return sequence;
}

worr_native_tx_result_v1 Worr_NativeTxSessionEnqueueV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    worr_native_record_ref_v1 record,
    uint8_t priority,
    uint32_t payload_handle,
    uint32_t payload_bytes,
    uint16_t max_datagram_bytes,
    uint64_t now_tick,
    uint32_t *message_sequence_out)
{
    int free_index = -1;
    int snapshot_index = -1;
    uint16_t index;
    uint32_t message_sequence;
    uint32_t oldest_reliable_sequence = UINT32_MAX;
    uint16_t fragment_stride;
    uint16_t fragment_count;
    worr_native_tx_slot_v1 retained;

    if (session == NULL || slots == NULL || message_sequence_out == NULL ||
        !Worr_NativeEnvelopeRecordRefValidV1(record) ||
        priority > WORR_NATIVE_ENVELOPE_MAX_PRIORITY || payload_handle == 0 ||
        payload_bytes == 0 ||
        payload_bytes > WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES ||
        max_datagram_bytes <= WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ||
        max_datagram_bytes > WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES ||
        !tx_output_distinct(session, slots, slot_capacity,
                            message_sequence_out,
                            sizeof(*message_sequence_out))) {
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    }
    fragment_stride = (uint16_t)(
        max_datagram_bytes - WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES);
    fragment_count = fragment_count_for(payload_bytes, fragment_stride);
    if (fragment_count == 0 ||
        fragment_count > WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS) {
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    }
    if (!Worr_NativeTxSessionValidateV1(session, slots, slot_capacity) ||
        tx_terminal_cancelled(session))
        return WORR_NATIVE_TX_INVALID_STATE;
    if (now_tick < session->last_tick) {
        counter_increment(&session->telemetry.clock_regressions);
        return WORR_NATIVE_TX_CLOCK_REGRESSION;
    }

    counter_increment(&session->telemetry.enqueue_attempts);
    session->last_tick = now_tick;

    for (index = 0; index < slot_capacity; ++index) {
        if (slots[index].state_flags == 0) {
            if (free_index < 0)
                free_index = (int)index;
            continue;
        }
        if (record_equal(slots[index].record, record)) {
            if (slots[index].payload_handle == payload_handle &&
                slots[index].payload_bytes == payload_bytes &&
                slots[index].fragment_stride == fragment_stride &&
                slots[index].fragment_count == fragment_count &&
                slots[index].priority == priority) {
                counter_increment(&session->telemetry.duplicates);
                *message_sequence_out = slots[index].message_sequence;
                return WORR_NATIVE_TX_DUPLICATE;
            }
            counter_increment(&session->telemetry.conflicts);
            return WORR_NATIVE_TX_CONFLICT;
        }
        if (slots[index].payload_handle == payload_handle) {
            counter_increment(&session->telemetry.conflicts);
            return WORR_NATIVE_TX_CONFLICT;
        }
        if (slots[index].record.record_class ==
            WORR_NATIVE_RECORD_SNAPSHOT_V1) {
            snapshot_index = (int)index;
        } else if (slots[index].message_sequence <
                   oldest_reliable_sequence) {
            oldest_reliable_sequence = slots[index].message_sequence;
        }
    }

    if (record.record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
        session->latest_snapshot_epoch != 0 &&
        object_id_compare(record.object_epoch, record.object_sequence,
                          session->latest_snapshot_epoch,
                          session->latest_snapshot_sequence) <= 0) {
        counter_increment(&session->telemetry.stale_snapshots);
        return WORR_NATIVE_TX_STALE_SNAPSHOT;
    }
    if (oldest_reliable_sequence != UINT32_MAX &&
        session->next_message_sequence - oldest_reliable_sequence >=
            WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY) {
        counter_increment(&session->telemetry.receipt_window_stalls);
        return WORR_NATIVE_TX_RECEIPT_WINDOW;
    }
    if ((session->state_flags & WORR_NATIVE_TX_SEQUENCE_EXHAUSTED) != 0) {
        counter_increment(&session->telemetry.sequence_exhaustions);
        return WORR_NATIVE_TX_SEQUENCE_EXHAUSTED_RESULT;
    }
    if (free_index < 0 &&
        (record.record_class != WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
         snapshot_index < 0)) {
        counter_increment(&session->telemetry.capacity_stalls);
        return WORR_NATIVE_TX_CAPACITY;
    }

    message_sequence = tx_allocate_sequence(session);
    memset(&retained, 0, sizeof(retained));
    retained.record = record;
    retained.message_sequence = message_sequence;
    retained.payload_handle = payload_handle;
    retained.payload_bytes = payload_bytes;
    retained.fragment_stride = fragment_stride;
    retained.fragment_count = fragment_count;
    retained.enqueue_tick = now_tick;
    retained.enqueue_dispatch = session->dispatch_count;
    retained.state_flags = WORR_NATIVE_TX_SLOT_OCCUPIED;
    retained.priority = priority;

    if (record.record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1) {
        session->latest_snapshot_epoch = record.object_epoch;
        session->latest_snapshot_sequence = record.object_sequence;
    }
    if (snapshot_index >= 0 &&
        record.record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1) {
        slots[snapshot_index] = retained;
        counter_increment(&session->telemetry.superseded_snapshots);
        *message_sequence_out = message_sequence;
        return WORR_NATIVE_TX_SUPERSEDED;
    }

    slots[free_index] = retained;
    ++session->retained_count;
    counter_increment(&session->telemetry.retained);
    *message_sequence_out = message_sequence;
    return WORR_NATIVE_TX_RETAINED;
}

static uint8_t tx_effective_priority(
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slot)
{
    const uint64_t waited = session->dispatch_count - slot->enqueue_dispatch;
    const uint64_t promotion =
        waited / WORR_NATIVE_ENVELOPE_AGING_QUANTUM;

    return promotion >= slot->priority
               ? 0
               : (uint8_t)(slot->priority - promotion);
}

static void tx_rebase_dispatch_if_possible(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity)
{
    uint64_t minimum = UINT64_MAX;
    uint16_t index;
    bool occupied = false;

    if (session->dispatch_count != UINT64_MAX)
        return;
    for (index = 0; index < slot_capacity; ++index) {
        if (slots[index].state_flags != 0) {
            occupied = true;
            if (slots[index].enqueue_dispatch < minimum)
                minimum = slots[index].enqueue_dispatch;
        }
    }
    if (!occupied) {
        session->dispatch_count = 0;
        counter_increment(&session->telemetry.scheduler_rebases);
        return;
    }
    if (minimum == 0)
        return;
    for (index = 0; index < slot_capacity; ++index) {
        if (slots[index].state_flags != 0)
            slots[index].enqueue_dispatch -= minimum;
    }
    session->dispatch_count -= minimum;
    counter_increment(&session->telemetry.scheduler_rebases);
}

static int tx_find_due_index(const worr_native_tx_session_v1 *session,
                             const worr_native_tx_slot_v1 *slots,
                             uint16_t slot_capacity,
                             uint64_t now_tick,
                             uint32_t resend_interval_ticks)
{
    int best = -1;
    uint16_t index;

    for (index = 0; index < slot_capacity; ++index) {
        bool due;
        uint8_t candidate_priority;
        uint8_t best_priority;

        if (slots[index].state_flags == 0)
            continue;
        due = slots[index].send_attempts == 0 ||
              now_tick - slots[index].last_send_tick >=
                  resend_interval_ticks;
        if (!due)
            continue;
        candidate_priority = tx_effective_priority(session, &slots[index]);
        best_priority = best < 0
                            ? UINT8_MAX
                            : tx_effective_priority(session, &slots[best]);
        if (best < 0 || candidate_priority < best_priority ||
            (candidate_priority == best_priority &&
             slots[index].enqueue_dispatch <
                 slots[best].enqueue_dispatch) ||
            (candidate_priority == best_priority &&
             slots[index].enqueue_dispatch ==
                 slots[best].enqueue_dispatch &&
             slots[index].message_sequence <
                 slots[best].message_sequence)) {
            best = (int)index;
        }
    }
    return best;
}

static void tx_selection_attempt_begin(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint64_t now_tick)
{
    counter_increment(&session->telemetry.select_attempts);
    session->last_tick = now_tick;
    tx_rebase_dispatch_if_possible(session, slots, slot_capacity);
}

static void tx_record_selected(worr_native_tx_session_v1 *session,
                               worr_native_tx_slot_v1 *slot,
                               uint64_t now_tick)
{
    const bool retry = slot->send_attempts != 0;

    if (slot->send_attempts != UINT32_MAX)
        ++slot->send_attempts;
    slot->last_send_tick = now_tick;
    if (session->dispatch_count != UINT64_MAX)
        ++session->dispatch_count;
    slot->enqueue_dispatch = session->dispatch_count;
    if (retry)
        counter_increment(&session->telemetry.selected_retries);
    else
        counter_increment(&session->telemetry.selected_first_sends);
}

worr_native_tx_result_v1 Worr_NativeTxSessionSelectDueV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint64_t now_tick,
    uint32_t resend_interval_ticks,
    worr_native_tx_slot_v1 *slot_out)
{
    int best;

    if (session == NULL || slots == NULL || slot_out == NULL ||
        !tx_output_distinct(session, slots, slot_capacity,
                            slot_out, sizeof(*slot_out))) {
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    }
    if (!Worr_NativeTxSessionValidateV1(session, slots, slot_capacity) ||
        tx_terminal_cancelled(session))
        return WORR_NATIVE_TX_INVALID_STATE;
    if (now_tick < session->last_tick) {
        counter_increment(&session->telemetry.clock_regressions);
        return WORR_NATIVE_TX_CLOCK_REGRESSION;
    }

    tx_selection_attempt_begin(session, slots, slot_capacity, now_tick);
    best = tx_find_due_index(session, slots, slot_capacity, now_tick,
                             resend_interval_ticks);
    if (best < 0) {
        counter_increment(&session->telemetry.not_due);
        return WORR_NATIVE_TX_NOT_DUE;
    }

    tx_record_selected(session, &slots[best], now_tick);
    *slot_out = slots[best];
    return WORR_NATIVE_TX_SELECTED;
}

static bool tx_send_ticket_header_valid(
    const worr_native_tx_send_ticket_v1 *ticket)
{
    return ticket != NULL && ticket->struct_size == sizeof(*ticket) &&
           ticket->schema_version == WORR_NATIVE_SESSION_ABI_VERSION &&
           ticket->state_flags == WORR_NATIVE_TX_SEND_TICKET_INITIALIZED &&
           ticket->transport_epoch != 0 &&
           ticket->connection_owner_id != 0 && ticket->reserved0 == 0 &&
           ticket->reserved1 == 0 &&
           ticket->pre_send_slot.state_flags ==
               WORR_NATIVE_TX_SLOT_OCCUPIED;
}

static bool tx_send_ticket_was_due(
    const worr_native_tx_send_ticket_v1 *ticket)
{
    const worr_native_tx_slot_v1 *slot = &ticket->pre_send_slot;

    if (ticket->selection_tick < slot->enqueue_tick)
        return false;
    if (slot->send_attempts == 0)
        return slot->last_send_tick == 0;
    return (slot->send_attempts != UINT32_MAX ||
            ticket->selection_tick > slot->last_send_tick) &&
           ticket->selection_tick >= slot->last_send_tick &&
           ticket->selection_tick - slot->last_send_tick >=
               ticket->resend_interval_ticks;
}

static bool tx_prepared_storage_distinct(
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_tx_send_ticket_v1 *ticket)
{
    return ticket != NULL &&
           !ranges_overlap(ticket, sizeof(*ticket), session,
                           sizeof(*session)) &&
           !ranges_overlap(ticket, sizeof(*ticket), slots,
                           (size_t)slot_capacity * sizeof(*slots));
}

worr_native_tx_result_v1 Worr_NativeTxSessionPrepareDueV1(
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint64_t now_tick,
    uint32_t resend_interval_ticks,
    worr_native_tx_send_ticket_v1 *ticket_out)
{
    worr_native_tx_send_ticket_v1 prepared;
    int best;

    if (session == NULL || slots == NULL || ticket_out == NULL ||
        !tx_output_distinct(session, slots, slot_capacity,
                            ticket_out, sizeof(*ticket_out))) {
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    }
    if (!Worr_NativeTxSessionValidateV1(session, slots, slot_capacity) ||
        tx_terminal_cancelled(session))
        return WORR_NATIVE_TX_INVALID_STATE;
    if (now_tick < session->last_tick)
        return WORR_NATIVE_TX_CLOCK_REGRESSION;

    /* A dispatch rebase subtracts one common value from every occupied slot
     * and the session counter, preserving waited ages and every tie-break. */
    best = tx_find_due_index(session, slots, slot_capacity, now_tick,
                             resend_interval_ticks);
    if (best < 0)
        return WORR_NATIVE_TX_NOT_DUE;
    /* Once the attempt counter saturates, a strictly newer timestamp is the
     * remaining monotonic slot field that makes a ticket permanently
     * single-use. */
    if (slots[best].send_attempts == UINT32_MAX &&
        now_tick == slots[best].last_send_tick) {
        return WORR_NATIVE_TX_INVALID_STATE;
    }

    memset(&prepared, 0, sizeof(prepared));
    prepared.struct_size = sizeof(prepared);
    prepared.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    prepared.state_flags = WORR_NATIVE_TX_SEND_TICKET_INITIALIZED;
    prepared.transport_epoch = session->transport_epoch;
    prepared.slot_index = (uint16_t)best;
    prepared.selection_tick = now_tick;
    prepared.resend_interval_ticks = resend_interval_ticks;
    prepared.connection_owner_id = session->connection_owner_id;
    memcpy(&prepared.pre_send_slot, &slots[best],
           sizeof(prepared.pre_send_slot));
    memcpy(ticket_out, &prepared, sizeof(prepared));
    return WORR_NATIVE_TX_SELECTED;
}

bool Worr_NativeTxSessionPreparedValidateV1(
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_tx_send_ticket_v1 *ticket)
{
    return session != NULL && slots != NULL &&
           tx_prepared_storage_distinct(session, slots, slot_capacity,
                                        ticket) &&
           Worr_NativeTxSessionValidateV1(session, slots, slot_capacity) &&
           !tx_terminal_cancelled(session) &&
           tx_send_ticket_header_valid(ticket) &&
           ticket->transport_epoch == session->transport_epoch &&
           ticket->connection_owner_id == session->connection_owner_id &&
           ticket->slot_index < slot_capacity &&
           tx_send_ticket_was_due(ticket) &&
           memcmp(&slots[ticket->slot_index], &ticket->pre_send_slot,
                  sizeof(ticket->pre_send_slot)) == 0;
}

worr_native_tx_result_v1 Worr_NativeTxSessionConfirmPreparedV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_tx_send_ticket_v1 *ticket,
    uint64_t completion_tick)
{
    worr_native_tx_session_v1 staged_session;
    worr_native_tx_slot_v1 staged_slots[WORR_NATIVE_SESSION_MAX_TX_SLOTS];
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);

    if (session == NULL || slots == NULL || ticket == NULL ||
        !tx_prepared_storage_distinct(session, slots, slot_capacity,
                                      ticket)) {
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    }
    if (!tx_send_ticket_header_valid(ticket))
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    if (!Worr_NativeTxSessionValidateV1(session, slots, slot_capacity) ||
        tx_terminal_cancelled(session))
        return WORR_NATIVE_TX_INVALID_STATE;
    if (ticket->transport_epoch != session->transport_epoch) {
        counter_increment(&session->telemetry.wrong_epoch);
        return WORR_NATIVE_TX_WRONG_EPOCH;
    }
    if (ticket->connection_owner_id != session->connection_owner_id)
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    if (ticket->slot_index >= slot_capacity ||
        !tx_send_ticket_was_due(ticket)) {
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    }
    if (memcmp(&slots[ticket->slot_index], &ticket->pre_send_slot,
               sizeof(ticket->pre_send_slot)) != 0) {
        return WORR_NATIVE_TX_INVALID_STATE;
    }
    if (completion_tick < ticket->selection_tick ||
        completion_tick < session->last_tick) {
        counter_increment(&session->telemetry.clock_regressions);
        return WORR_NATIVE_TX_CLOCK_REGRESSION;
    }

    staged_session = *session;
    memset(staged_slots, 0, sizeof(staged_slots));
    memcpy(staged_slots, slots, slots_bytes);
    tx_selection_attempt_begin(&staged_session, staged_slots,
                               slot_capacity, completion_tick);
    tx_record_selected(&staged_session,
                       &staged_slots[ticket->slot_index], completion_tick);

    /* Even fully saturated counters cannot make a successful ticket
     * replayable: at least one byte of the retained slot must advance. */
    if (memcmp(&staged_slots[ticket->slot_index],
               &ticket->pre_send_slot,
               sizeof(ticket->pre_send_slot)) == 0 ||
        !Worr_NativeTxSessionValidateV1(&staged_session, staged_slots,
                                        slot_capacity)) {
        return WORR_NATIVE_TX_INVALID_STATE;
    }

    *session = staged_session;
    memcpy(slots, staged_slots, slots_bytes);
    return WORR_NATIVE_TX_SELECTED;
}

worr_native_tx_result_v1 Worr_NativeTxSessionApplyAckV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_ack_range_v1 *acknowledgement,
    uint32_t *acked_count_out)
{
    uint32_t acknowledged = 0;
    uint32_t reliable = 0;
    uint32_t snapshots = 0;
    uint32_t sequence_highwater;
    uint16_t index;
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);

    if (session == NULL || slots == NULL || acknowledgement == NULL ||
        acked_count_out == NULL ||
        ranges_overlap(acknowledgement, sizeof(*acknowledgement),
                       session, sizeof(*session)) ||
        ranges_overlap(acknowledgement, sizeof(*acknowledgement),
                       slots, slots_bytes) ||
        !tx_output_distinct(session, slots, slot_capacity,
                            acked_count_out, sizeof(*acked_count_out)) ||
        ranges_overlap(acknowledgement, sizeof(*acknowledgement),
                       acked_count_out, sizeof(*acked_count_out)) ||
        !Worr_NativeAckRangeValidateV1(acknowledgement)) {
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    }
    if (!Worr_NativeTxSessionValidateV1(session, slots, slot_capacity) ||
        tx_terminal_cancelled(session))
        return WORR_NATIVE_TX_INVALID_STATE;
    if (acknowledgement->connection_owner_id !=
        session->connection_owner_id) {
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    }

    counter_increment(&session->telemetry.acknowledgement_attempts);
    if (acknowledgement->transport_epoch != session->transport_epoch) {
        counter_increment(&session->telemetry.wrong_epoch);
        return WORR_NATIVE_TX_WRONG_EPOCH;
    }
    sequence_highwater =
        (session->state_flags & WORR_NATIVE_TX_SEQUENCE_EXHAUSTED) != 0
            ? UINT32_MAX
            : session->next_message_sequence - 1u;
    if (acknowledgement->last_message_sequence > sequence_highwater)
        return WORR_NATIVE_TX_INVALID_ARGUMENT;

    for (index = 0; index < slot_capacity; ++index) {
        if (slots[index].state_flags == 0 ||
            slots[index].message_sequence <
                acknowledgement->first_message_sequence ||
            slots[index].message_sequence >
                acknowledgement->last_message_sequence) {
            continue;
        }
        if (slots[index].record.record_class ==
            WORR_NATIVE_RECORD_SNAPSHOT_V1)
            ++snapshots;
        else
            ++reliable;
        memset(&slots[index], 0, sizeof(slots[index]));
        ++acknowledged;
    }
    session->retained_count =
        (uint16_t)(session->retained_count - acknowledged);
    counter_add(&session->telemetry.acknowledged_reliable, reliable);
    counter_add(&session->telemetry.acknowledged_snapshots, snapshots);
    *acked_count_out = acknowledged;
    if (acknowledged == 0) {
        counter_increment(&session->telemetry.acknowledgement_empty);
        return WORR_NATIVE_TX_ACKNOWLEDGEMENT_EMPTY;
    }
    return WORR_NATIVE_TX_ACKNOWLEDGED;
}

worr_native_tx_result_v1 Worr_NativeTxSessionCancelRetainedV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint32_t *cancelled_count_out)
{
    worr_native_tx_session_v1 staged_session;
    worr_native_tx_slot_v1 staged_slots[WORR_NATIVE_SESSION_MAX_TX_SLOTS];
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    uint32_t cancelled;

    if (session == NULL || slots == NULL || cancelled_count_out == NULL ||
        !tx_output_distinct(session, slots, slot_capacity,
                            cancelled_count_out,
                            sizeof(*cancelled_count_out))) {
        return WORR_NATIVE_TX_INVALID_ARGUMENT;
    }
    if (!Worr_NativeTxSessionValidateV1(session, slots, slot_capacity))
        return WORR_NATIVE_TX_INVALID_STATE;

    if (tx_terminal_cancelled(session)) {
        *cancelled_count_out = 0;
        return WORR_NATIVE_TX_CANCELLED;
    }

    cancelled = session->retained_count;
    staged_session = *session;
    staged_session.retained_count = 0;
    staged_session.state_flags |= WORR_NATIVE_TX_TERMINAL_CANCELLED;
    memset(staged_slots, 0, sizeof(staged_slots));
    if (!Worr_NativeTxSessionValidateV1(
            &staged_session, staged_slots, slot_capacity)) {
        return WORR_NATIVE_TX_INVALID_STATE;
    }
    *session = staged_session;
    memcpy(slots, staged_slots, slots_bytes);
    *cancelled_count_out = cancelled;
    return WORR_NATIVE_TX_CANCELLED;
}

static uint16_t bitmap_count(uint64_t value)
{
    uint16_t count = 0;

    while (value != 0) {
        value &= value - 1u;
        ++count;
    }
    return count;
}

static uint16_t reassembly_bitmap_count(const uint64_t bitmap[2])
{
    return (uint16_t)(bitmap_count(bitmap[0]) +
                      bitmap_count(bitmap[1]));
}

static uint64_t bitmap_valid_bits(uint16_t fragment_count,
                                  unsigned word_index)
{
    const uint16_t word_first_fragment =
        (uint16_t)(word_index * 64u);
    uint16_t word_fragment_count;

    if (fragment_count <= word_first_fragment)
        return 0;
    word_fragment_count =
        (uint16_t)(fragment_count - word_first_fragment);
    if (word_fragment_count >= 64u)
        return UINT64_MAX;
    return (UINT64_C(1) << word_fragment_count) - 1u;
}

static bool bitmap_has_fragment(const uint64_t bitmap[2],
                                uint16_t fragment_index)
{
    const unsigned word_index = fragment_index / 64u;
    const unsigned bit_index = fragment_index % 64u;

    return (bitmap[word_index] & (UINT64_C(1) << bit_index)) != 0;
}

static bool reassembly_valid(
    const worr_native_envelope_reassembly_v1 *reassembly,
    uint32_t transport_epoch)
{
    uint32_t expected_bytes = 0;
    uint16_t index;
    bool complete;

    if (reassembly->struct_size != sizeof(*reassembly) ||
        reassembly->schema_version != WORR_NATIVE_ENVELOPE_ABI_VERSION ||
        (reassembly->state_flags &
         ~(WORR_NATIVE_REASSEMBLY_INITIALIZED |
           WORR_NATIVE_REASSEMBLY_COMPLETE)) != 0 ||
        (reassembly->state_flags & WORR_NATIVE_REASSEMBLY_INITIALIZED) == 0 ||
        !Worr_NativeEnvelopeRecordRefValidV1(reassembly->record) ||
        reassembly->transport_epoch != transport_epoch ||
        reassembly->message_sequence == 0 ||
        reassembly->total_payload_bytes == 0 ||
        reassembly->total_payload_bytes >
            WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES ||
        reassembly->fragment_stride == 0 ||
        reassembly->fragment_count == 0 ||
        reassembly->fragment_count > WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS ||
        fragment_count_for(reassembly->total_payload_bytes,
                           reassembly->fragment_stride) !=
            reassembly->fragment_count ||
        reassembly->received_fragment_count > reassembly->fragment_count ||
        reassembly->received_payload_bytes > reassembly->total_payload_bytes ||
        reassembly->priority > WORR_NATIVE_ENVELOPE_MAX_PRIORITY ||
        reassembly->reserved0 != 0) {
        return false;
    }
    if ((reassembly->received_bitmap[0] &
         ~bitmap_valid_bits(reassembly->fragment_count, 0)) != 0 ||
        (reassembly->received_bitmap[1] &
         ~bitmap_valid_bits(reassembly->fragment_count, 1)) != 0 ||
        reassembly_bitmap_count(reassembly->received_bitmap) !=
            reassembly->received_fragment_count) {
        return false;
    }
    for (index = 0; index < reassembly->fragment_count; ++index) {
        if (bitmap_has_fragment(reassembly->received_bitmap, index)) {
            const uint32_t offset =
                (uint32_t)index * reassembly->fragment_stride;
            const uint32_t remaining =
                reassembly->total_payload_bytes - offset;
            const uint32_t bytes = remaining < reassembly->fragment_stride
                                       ? remaining
                                       : reassembly->fragment_stride;
            if (expected_bytes > reassembly->total_payload_bytes - bytes)
                return false;
            expected_bytes += bytes;
        }
    }
    if (expected_bytes != reassembly->received_payload_bytes)
        return false;
    complete = reassembly->received_fragment_count ==
                   reassembly->fragment_count &&
               reassembly->received_payload_bytes ==
                   reassembly->total_payload_bytes;
    return ((reassembly->state_flags & WORR_NATIVE_REASSEMBLY_COMPLETE) != 0) ==
           complete;
}

static bool receipt_entry_valid(
    const worr_native_receipt_history_entry_v1 *entry)
{
    return Worr_NativeEnvelopeRecordRefValidV1(entry->record) &&
           entry->message_sequence != 0 &&
           entry->total_payload_bytes != 0 &&
           entry->total_payload_bytes <=
               WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES &&
           entry->fragment_stride != 0 && entry->fragment_count != 0 &&
           entry->fragment_count <= WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS &&
           fragment_count_for(entry->total_payload_bytes,
                              entry->fragment_stride) ==
               entry->fragment_count &&
           entry->priority <= WORR_NATIVE_ENVELOPE_MAX_PRIORITY &&
           bytes_are_zero(entry->reserved0, sizeof(entry->reserved0));
}

static bool receipt_identity_matches(
    const worr_native_receipt_history_entry_v1 *entry,
    const worr_native_envelope_frame_info_v1 *info)
{
    return entry->message_sequence == info->message_sequence &&
           record_equal(entry->record, info->record) &&
           entry->total_payload_bytes == info->total_payload_bytes &&
           entry->payload_crc32 == info->payload_crc32 &&
           entry->fragment_stride == info->fragment_stride &&
           entry->fragment_count == info->fragment_count &&
           entry->priority == info->priority;
}

static bool receipt_identity_matches_snapshot(
    const worr_native_receipt_history_entry_v1 *entry,
    const worr_native_snapshot_identity_v1 *identity)
{
    return entry->message_sequence == identity->message_sequence &&
           record_equal(entry->record, identity->record) &&
           entry->total_payload_bytes == identity->total_payload_bytes &&
           entry->payload_crc32 == identity->payload_crc32 &&
           entry->fragment_stride == identity->fragment_stride &&
           entry->fragment_count == identity->fragment_count &&
           entry->priority == identity->priority;
}

static bool frame_identity_matches_reassembly(
    const worr_native_envelope_reassembly_v1 *reassembly,
    const worr_native_envelope_frame_info_v1 *info)
{
    return record_equal(reassembly->record, info->record) &&
           reassembly->transport_epoch == info->transport_epoch &&
           reassembly->message_sequence == info->message_sequence &&
           reassembly->total_payload_bytes == info->total_payload_bytes &&
           reassembly->payload_crc32 == info->payload_crc32 &&
           reassembly->fragment_stride == info->fragment_stride &&
           reassembly->fragment_count == info->fragment_count &&
           reassembly->priority == info->priority;
}

static void make_single_ack(uint32_t transport_epoch,
                            uint64_t connection_owner_id,
                            uint32_t message_sequence,
                            worr_native_ack_range_v1 *acknowledgement)
{
    memset(acknowledgement, 0, sizeof(*acknowledgement));
    acknowledgement->struct_size = sizeof(*acknowledgement);
    acknowledgement->schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    acknowledgement->transport_epoch = transport_epoch;
    acknowledgement->connection_owner_id = connection_owner_id;
    acknowledgement->first_message_sequence = message_sequence;
    acknowledgement->last_message_sequence = message_sequence;
}

static bool snapshot_identity_valid(
    const worr_native_snapshot_identity_v1 *identity)
{
    return Worr_NativeEnvelopeRecordRefValidV1(identity->record) &&
           identity->record.record_class ==
               WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
           identity->message_sequence != 0 &&
           identity->total_payload_bytes != 0 &&
           identity->total_payload_bytes <=
               WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES &&
           identity->fragment_stride != 0 &&
           identity->fragment_count != 0 &&
           identity->fragment_count <= WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS &&
           fragment_count_for(identity->total_payload_bytes,
                              identity->fragment_stride) ==
               identity->fragment_count &&
           identity->priority <= WORR_NATIVE_ENVELOPE_MAX_PRIORITY &&
           (identity->state_flags == WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY ||
            identity->state_flags ==
                WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED) &&
           identity->reserved0[0] == 0 && identity->reserved0[1] == 0;
}

static worr_native_snapshot_identity_v1 snapshot_identity_from_info(
    const worr_native_envelope_frame_info_v1 *info,
    uint8_t state_flags)
{
    worr_native_snapshot_identity_v1 identity;

    memset(&identity, 0, sizeof(identity));
    identity.record = info->record;
    identity.message_sequence = info->message_sequence;
    identity.total_payload_bytes = info->total_payload_bytes;
    identity.payload_crc32 = info->payload_crc32;
    identity.fragment_stride = info->fragment_stride;
    identity.fragment_count = info->fragment_count;
    identity.priority = info->priority;
    identity.state_flags = state_flags;
    return identity;
}

static worr_native_snapshot_identity_v1 snapshot_identity_from_reassembly(
    const worr_native_envelope_reassembly_v1 *reassembly,
    uint8_t state_flags)
{
    worr_native_snapshot_identity_v1 identity;

    memset(&identity, 0, sizeof(identity));
    identity.record = reassembly->record;
    identity.message_sequence = reassembly->message_sequence;
    identity.total_payload_bytes = reassembly->total_payload_bytes;
    identity.payload_crc32 = reassembly->payload_crc32;
    identity.fragment_stride = reassembly->fragment_stride;
    identity.fragment_count = reassembly->fragment_count;
    identity.priority = reassembly->priority;
    identity.state_flags = state_flags;
    return identity;
}

static bool snapshot_identity_matches_info(
    const worr_native_snapshot_identity_v1 *identity,
    const worr_native_envelope_frame_info_v1 *info)
{
    return record_equal(identity->record, info->record) &&
           identity->message_sequence == info->message_sequence &&
           identity->total_payload_bytes == info->total_payload_bytes &&
           identity->payload_crc32 == info->payload_crc32 &&
           identity->fragment_stride == info->fragment_stride &&
           identity->fragment_count == info->fragment_count &&
           identity->priority == info->priority;
}

static bool snapshot_identity_matches_reassembly(
    const worr_native_snapshot_identity_v1 *identity,
    const worr_native_envelope_reassembly_v1 *reassembly)
{
    return record_equal(identity->record, reassembly->record) &&
           identity->message_sequence == reassembly->message_sequence &&
           identity->total_payload_bytes == reassembly->total_payload_bytes &&
           identity->payload_crc32 == reassembly->payload_crc32 &&
           identity->fragment_stride == reassembly->fragment_stride &&
           identity->fragment_count == reassembly->fragment_count &&
           identity->priority == reassembly->priority;
}

static bool snapshot_identity_same_message(
    const worr_native_snapshot_identity_v1 *left,
    const worr_native_snapshot_identity_v1 *right)
{
    return record_equal(left->record, right->record) &&
           left->message_sequence == right->message_sequence &&
           left->total_payload_bytes == right->total_payload_bytes &&
           left->payload_crc32 == right->payload_crc32 &&
           left->fragment_stride == right->fragment_stride &&
           left->fragment_count == right->fragment_count &&
           left->priority == right->priority;
}

static int snapshot_identity_find_canonical(
    const worr_native_rx_session_v1 *session,
    worr_native_record_ref_v1 record)
{
    uint16_t index;

    for (index = 0; index < session->snapshot_tombstone_count; ++index) {
        if (record_equal(session->snapshot_tombstones[index].record, record))
            return (int)index;
    }
    return -1;
}

static int snapshot_identity_find_message_sequence(
    const worr_native_rx_session_v1 *session,
    uint32_t message_sequence)
{
    uint16_t index;

    for (index = 0; index < session->snapshot_tombstone_count; ++index) {
        if (session->snapshot_tombstones[index].message_sequence ==
            message_sequence) {
            return (int)index;
        }
    }
    return -1;
}

static void snapshot_identity_remove(worr_native_rx_session_v1 *session,
                                     uint16_t index)
{
    if ((uint16_t)(index + 1u) < session->snapshot_tombstone_count) {
        memmove(&session->snapshot_tombstones[index],
                &session->snapshot_tombstones[index + 1u],
                (size_t)(session->snapshot_tombstone_count - index - 1u) *
                    sizeof(session->snapshot_tombstones[0]));
    }
    --session->snapshot_tombstone_count;
    memset(&session->snapshot_tombstones[session->snapshot_tombstone_count],
           0, sizeof(session->snapshot_tombstones[0]));
}

static void snapshot_identity_store(
    worr_native_rx_session_v1 *session,
    worr_native_snapshot_identity_v1 identity)
{
    int existing = snapshot_identity_find_canonical(session, identity.record);

    if (existing >= 0) {
        snapshot_identity_remove(session, (uint16_t)existing);
        session->snapshot_tombstones[session->snapshot_tombstone_count++] =
            identity;
        return;
    }
    if (session->snapshot_tombstone_count ==
        WORR_NATIVE_SESSION_SNAPSHOT_TOMBSTONE_CAPACITY) {
        uint16_t eviction = 0;

        while (eviction < session->snapshot_tombstone_count &&
               session->snapshot_tombstones[eviction].state_flags ==
                   WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED) {
            ++eviction;
        }
        if (eviction == session->snapshot_tombstone_count)
            eviction = 0;
        snapshot_identity_remove(session, eviction);
        counter_increment(
            &session->telemetry.snapshot_tombstone_evictions);
    }
    session->snapshot_tombstones[session->snapshot_tombstone_count++] =
        identity;
}

static bool rx_slot_storage_distinct(
    const worr_native_rx_session_v1 *session,
    const worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity)
{
    return !ranges_overlap(session, sizeof(*session), slots,
                           (size_t)slot_capacity * sizeof(*slots));
}

bool Worr_NativeRxSessionValidateV1(
    const worr_native_rx_session_v1 *session,
    const worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity)
{
    uint16_t occupied = 0;
    uint16_t history_occupied = 0;
    bool committed_snapshot_identity_found = false;
    uint16_t index;

    if (session == NULL || slots == NULL || slot_capacity == 0 ||
        slot_capacity > WORR_NATIVE_SESSION_MAX_RX_SLOTS ||
        !rx_slot_storage_distinct(session, slots, slot_capacity) ||
        session->struct_size != sizeof(*session) ||
        session->schema_version != WORR_NATIVE_SESSION_ABI_VERSION ||
        (session->state_flags &
         ~(WORR_NATIVE_RX_INITIALIZED |
           WORR_NATIVE_RX_TERMINAL_CANCELLED)) != 0 ||
        (session->state_flags & WORR_NATIVE_RX_INITIALIZED) == 0 ||
        session->transport_epoch == 0 ||
        session->connection_owner_id == 0 ||
        session->slot_capacity != slot_capacity ||
        session->occupied_count > slot_capacity ||
        ((session->state_flags & WORR_NATIVE_RX_TERMINAL_CANCELLED) != 0 &&
         session->occupied_count != 0) ||
        session->payload_stride == 0 ||
        session->payload_stride > WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES ||
        session->fragment_timeout_ticks == 0 ||
        session->complete_timeout_ticks == 0 ||
        session->history_count >
            WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY ||
        session->snapshot_tombstone_count >
            WORR_NATIVE_SESSION_SNAPSHOT_TOMBSTONE_CAPACITY ||
        session->reserved0 != 0 || session->reserved1 != 0 ||
        ((session->committed_snapshot_epoch == 0) !=
         (session->committed_snapshot_sequence == 0))) {
        return false;
    }
    if ((session->highest_committed_sequence == 0) !=
            (session->history_count == 0) ||
        (session->history_count == 0 && session->receipt_mask != 0) ||
        (session->history_count != 0 &&
         (session->receipt_mask & UINT64_C(1)) == 0) ||
        bitmap_count(session->receipt_mask) != session->history_count) {
        return false;
    }
    for (index = 0;
         index < WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY;
         ++index) {
        uint16_t previous;
        uint32_t distance;

        if (session->history[index].message_sequence == 0) {
            if (!bytes_are_zero(&session->history[index],
                                sizeof(session->history[index]))) {
                return false;
            }
            continue;
        }
        if (!receipt_entry_valid(&session->history[index]))
            return false;
        if (session->history[index].message_sequence >
            session->highest_committed_sequence) {
            return false;
        }
        distance = session->highest_committed_sequence -
                   session->history[index].message_sequence;
        if (distance >= WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY ||
            (session->receipt_mask & (UINT64_C(1) << distance)) == 0 ||
            index != (uint16_t)(session->history[index].message_sequence %
                                WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY)) {
            return false;
        }
        ++history_occupied;
        for (previous = 0; previous < index; ++previous) {
            if (session->history[previous].message_sequence != 0 &&
                session->history[previous].message_sequence ==
                session->history[index].message_sequence) {
                return false;
            }
        }
    }
    if (history_occupied != session->history_count)
        return false;

    for (index = 0;
         index < WORR_NATIVE_SESSION_SNAPSHOT_TOMBSTONE_CAPACITY;
         ++index) {
        uint16_t previous;
        int comparison = 1;

        if (index >= session->snapshot_tombstone_count) {
            if (!bytes_are_zero(&session->snapshot_tombstones[index],
                                sizeof(session->snapshot_tombstones[index]))) {
                return false;
            }
            continue;
        }
        if (!snapshot_identity_valid(&session->snapshot_tombstones[index]))
            return false;
        if (session->committed_snapshot_epoch != 0) {
            comparison = object_id_compare(
                session->snapshot_tombstones[index].record.object_epoch,
                session->snapshot_tombstones[index].record.object_sequence,
                session->committed_snapshot_epoch,
                session->committed_snapshot_sequence);
            if (comparison < 0 ||
                (comparison == 0 &&
                 session->snapshot_tombstones[index].state_flags !=
                     WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED)) {
                return false;
            }
        }
        if (session->snapshot_tombstones[index].state_flags ==
            WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED) {
            if (session->committed_snapshot_epoch == 0 || comparison != 0 ||
                committed_snapshot_identity_found) {
                return false;
            }
            committed_snapshot_identity_found = true;
        }
        for (previous = 0;
             previous < WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY;
             ++previous) {
            if (session->history[previous].message_sequence !=
                session->snapshot_tombstones[index].message_sequence) {
                continue;
            }
            if (session->snapshot_tombstones[index].state_flags !=
                    WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED ||
                !receipt_identity_matches_snapshot(
                    &session->history[previous],
                    &session->snapshot_tombstones[index])) {
                return false;
            }
        }
        for (previous = 0; previous < index; ++previous) {
            if (record_equal(
                    session->snapshot_tombstones[previous].record,
                    session->snapshot_tombstones[index].record) ||
                session->snapshot_tombstones[previous].message_sequence ==
                    session->snapshot_tombstones[index].message_sequence) {
                return false;
            }
        }
    }
    if ((session->committed_snapshot_epoch != 0) !=
        committed_snapshot_identity_found) {
        return false;
    }

    for (index = 0; index < slot_capacity; ++index) {
        uint16_t previous;
        uint16_t history_index;
        int snapshot_identity_index = -1;
        int sequence_identity_index;
        const bool complete =
            (slots[index].state_flags & WORR_NATIVE_RX_SLOT_COMPLETE) != 0;

        if (slots[index].state_flags == 0) {
            if (!bytes_are_zero(&slots[index], sizeof(slots[index])))
                return false;
            continue;
        }
        if ((slots[index].state_flags &
             ~(WORR_NATIVE_RX_SLOT_OCCUPIED |
               WORR_NATIVE_RX_SLOT_COMPLETE |
               WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY)) != 0 ||
            (slots[index].state_flags & WORR_NATIVE_RX_SLOT_OCCUPIED) == 0 ||
            slots[index].reserved0 != 0 ||
            slots[index].first_fragment_tick >
                slots[index].last_activity_tick ||
            slots[index].last_activity_tick > session->last_tick ||
            !reassembly_valid(&slots[index].reassembly,
                              session->transport_epoch) ||
            complete !=
                ((slots[index].reassembly.state_flags &
                  WORR_NATIVE_REASSEMBLY_COMPLETE) != 0)) {
            return false;
        }
        ++occupied;
        if ((slots[index].state_flags &
             WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY) != 0 &&
            slots[index].reassembly.record.record_class !=
                WORR_NATIVE_RECORD_SNAPSHOT_V1) {
            return false;
        }
        sequence_identity_index = snapshot_identity_find_message_sequence(
            session, slots[index].reassembly.message_sequence);
        if (sequence_identity_index >= 0 &&
            (slots[index].reassembly.record.record_class !=
                 WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
             session->snapshot_tombstones[sequence_identity_index]
                     .state_flags !=
                 WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY ||
             !snapshot_identity_matches_reassembly(
                 &session->snapshot_tombstones[sequence_identity_index],
                 &slots[index].reassembly))) {
            return false;
        }
        if (slots[index].reassembly.record.record_class ==
            WORR_NATIVE_RECORD_SNAPSHOT_V1) {
            snapshot_identity_index = snapshot_identity_find_canonical(
                session, slots[index].reassembly.record);
            if (session->committed_snapshot_epoch != 0 &&
                object_id_compare(
                    slots[index].reassembly.record.object_epoch,
                    slots[index].reassembly.record.object_sequence,
                    session->committed_snapshot_epoch,
                    session->committed_snapshot_sequence) <= 0) {
                return false;
            }
            if (snapshot_identity_index >= 0 &&
                ((slots[index].state_flags &
                  WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY) == 0 ||
                 session->snapshot_tombstones[snapshot_identity_index]
                         .state_flags !=
                     WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY ||
                 !snapshot_identity_matches_reassembly(
                     &session->snapshot_tombstones[snapshot_identity_index],
                     &slots[index].reassembly))) {
                return false;
            }
        }
        if (session->highest_committed_sequence != 0 &&
            slots[index].reassembly.message_sequence <=
                session->highest_committed_sequence &&
            session->highest_committed_sequence -
                    slots[index].reassembly.message_sequence >=
                WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY &&
            (slots[index].state_flags &
             WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY) == 0) {
            return false;
        }
        for (previous = 0; previous < index; ++previous) {
            if (slots[previous].state_flags != 0) {
                if (slots[previous].reassembly.message_sequence ==
                    slots[index].reassembly.message_sequence) {
                    return false;
                }
                if (slots[index].reassembly.record.record_class ==
                        WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
                    slots[previous].reassembly.record.record_class ==
                        WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
                    record_equal(slots[previous].reassembly.record,
                                 slots[index].reassembly.record)) {
                    return false;
                }
            }
        }
        for (history_index = 0;
             history_index <
                 WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY;
             ++history_index) {
            if (session->history[history_index].message_sequence != 0 &&
                session->history[history_index].message_sequence ==
                slots[index].reassembly.message_sequence) {
                return false;
            }
        }
    }
    return occupied == session->occupied_count;
}

worr_native_rx_result_v1 Worr_NativeRxSessionCancelPendingV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    worr_native_rx_cancel_report_v1 *report_out)
{
    worr_native_rx_session_v1 staged_session;
    worr_native_rx_slot_v1 staged_slots[WORR_NATIVE_SESSION_MAX_RX_SLOTS];
    worr_native_rx_cancel_report_v1 report;
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    uint16_t index;

    if (session == NULL || slots == NULL || report_out == NULL ||
        ranges_overlap(report_out, sizeof(*report_out),
                       session, sizeof(*session)) ||
        ranges_overlap(report_out, sizeof(*report_out),
                       slots, slots_bytes)) {
        return WORR_NATIVE_RX_INVALID_ARGUMENT;
    }
    if (!Worr_NativeRxSessionValidateV1(session, slots, slot_capacity))
        return WORR_NATIVE_RX_INVALID_STATE;

    if ((session->state_flags & WORR_NATIVE_RX_TERMINAL_CANCELLED) != 0) {
        memset(&report, 0, sizeof(report));
        *report_out = report;
        return WORR_NATIVE_RX_CANCELLED;
    }

    memset(&report, 0, sizeof(report));
    for (index = 0; index < slot_capacity; ++index) {
        if ((slots[index].state_flags & WORR_NATIVE_RX_SLOT_OCCUPIED) == 0)
            continue;
        if ((slots[index].state_flags & WORR_NATIVE_RX_SLOT_COMPLETE) != 0)
            ++report.complete_messages;
        else
            ++report.incomplete_messages;
    }
    staged_session = *session;
    staged_session.occupied_count = 0;
    staged_session.state_flags |= WORR_NATIVE_RX_TERMINAL_CANCELLED;
    memset(staged_slots, 0, sizeof(staged_slots));
    if (!Worr_NativeRxSessionValidateV1(
            &staged_session, staged_slots, slot_capacity)) {
        return WORR_NATIVE_RX_INVALID_STATE;
    }
    *session = staged_session;
    memcpy(slots, staged_slots, slots_bytes);
    *report_out = report;
    return WORR_NATIVE_RX_CANCELLED;
}

static bool rx_init_arguments_valid(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint32_t payload_stride,
    uint32_t fragment_timeout_ticks,
    uint32_t complete_timeout_ticks,
    const worr_native_session_binding_v1 *binding)
{
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);

    return session != NULL && slots != NULL && binding != NULL &&
           slot_capacity != 0 &&
           slot_capacity <= WORR_NATIVE_SESSION_MAX_RX_SLOTS &&
           payload_stride != 0 &&
           payload_stride <= WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES &&
           fragment_timeout_ticks != 0 && complete_timeout_ticks != 0 &&
           Worr_NativeSessionBindingValidateV1(binding) &&
           !ranges_overlap(session, sizeof(*session), slots, slots_bytes) &&
           !ranges_overlap(binding, sizeof(*binding), session,
                           sizeof(*session)) &&
           !ranges_overlap(binding, sizeof(*binding), slots, slots_bytes);
}

bool Worr_NativeRxSessionInitV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint32_t payload_stride,
    uint32_t fragment_timeout_ticks,
    uint32_t complete_timeout_ticks,
    const worr_native_session_binding_v1 *binding)
{
    worr_native_rx_session_v1 initialized;

    if (!rx_init_arguments_valid(session, slots, slot_capacity,
                                 payload_stride, fragment_timeout_ticks,
                                 complete_timeout_ticks, binding)) {
        return false;
    }
    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    initialized.state_flags = WORR_NATIVE_RX_INITIALIZED;
    initialized.transport_epoch = binding->transport_epoch;
    initialized.connection_owner_id = binding->connection_owner_id;
    initialized.slot_capacity = slot_capacity;
    initialized.payload_stride = payload_stride;
    initialized.fragment_timeout_ticks = fragment_timeout_ticks;
    initialized.complete_timeout_ticks = complete_timeout_ticks;
    memset(slots, 0, (size_t)slot_capacity * sizeof(*slots));
    *session = initialized;
    return true;
}

bool Worr_NativeRxSessionAdvanceEpochV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_session_binding_v1 *binding)
{
    uint32_t payload_stride;
    uint32_t fragment_timeout_ticks;
    uint32_t complete_timeout_ticks;

    if (session == NULL || slots == NULL || binding == NULL ||
        !Worr_NativeRxSessionValidateV1(session, slots, slot_capacity) ||
        !Worr_NativeSessionBindingValidateV1(binding) ||
        binding->connection_owner_id != session->connection_owner_id ||
        binding->transport_epoch <= session->transport_epoch ||
        ranges_overlap(binding, sizeof(*binding), session,
                       sizeof(*session)) ||
        ranges_overlap(binding, sizeof(*binding), slots,
                       (size_t)slot_capacity * sizeof(*slots))) {
        return false;
    }
    payload_stride = session->payload_stride;
    fragment_timeout_ticks = session->fragment_timeout_ticks;
    complete_timeout_ticks = session->complete_timeout_ticks;
    return Worr_NativeRxSessionInitV1(
        session, slots, slot_capacity, payload_stride,
        fragment_timeout_ticks, complete_timeout_ticks, binding);
}

static uint32_t rx_expire_internal(worr_native_rx_session_v1 *session,
                                   worr_native_rx_slot_v1 *slots,
                                   uint16_t slot_capacity,
                                   uint64_t now_tick)
{
    uint32_t expired = 0;
    uint16_t index;

    for (index = 0; index < slot_capacity; ++index) {
        uint32_t timeout;
        bool complete;

        if (slots[index].state_flags == 0)
            continue;
        complete = (slots[index].state_flags &
                    WORR_NATIVE_RX_SLOT_COMPLETE) != 0;
        timeout = complete ? session->complete_timeout_ticks
                           : session->fragment_timeout_ticks;
        if (now_tick - slots[index].last_activity_tick < timeout)
            continue;
        if (slots[index].reassembly.record.record_class ==
            WORR_NATIVE_RECORD_SNAPSHOT_V1) {
            snapshot_identity_store(
                session,
                snapshot_identity_from_reassembly(
                    &slots[index].reassembly,
                    WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY));
        }
        memset(&slots[index], 0, sizeof(slots[index]));
        --session->occupied_count;
        ++expired;
        if (complete)
            counter_increment(&session->telemetry.complete_timeouts);
        else
            counter_increment(&session->telemetry.fragment_timeouts);
    }
    session->last_tick = now_tick;
    return expired;
}

static int rx_history_find(const worr_native_rx_session_v1 *session,
                           uint32_t message_sequence)
{
    uint16_t index;

    for (index = 0;
         index < WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY;
         ++index) {
        if (session->history[index].message_sequence == message_sequence)
            return (int)index;
    }
    return -1;
}

static worr_native_rx_result_v1 rx_decode_result(
    worr_native_rx_session_v1 *session,
    worr_native_envelope_decode_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED:
        counter_increment(&session->telemetry.unsupported);
        return WORR_NATIVE_RX_UNSUPPORTED;
    case WORR_NATIVE_ENVELOPE_DECODE_CORRUPT:
        counter_increment(&session->telemetry.datagram_corrupt);
        return WORR_NATIVE_RX_DATAGRAM_CORRUPT;
    case WORR_NATIVE_ENVELOPE_DECODE_MALFORMED:
        counter_increment(&session->telemetry.malformed);
        return WORR_NATIVE_RX_MALFORMED;
    case WORR_NATIVE_ENVELOPE_DECODE_INVALID_ARGUMENT:
        return WORR_NATIVE_RX_INVALID_ARGUMENT;
    case WORR_NATIVE_ENVELOPE_DECODE_OK:
    default:
        return WORR_NATIVE_RX_INVALID_STATE;
    }
}

static worr_native_rx_result_v1 rx_accept_result(
    worr_native_rx_session_v1 *session,
    worr_native_envelope_accept_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_ENVELOPE_REJECT_MESSAGE_CONFLICT:
        counter_increment(&session->telemetry.message_conflicts);
        return WORR_NATIVE_RX_MESSAGE_CONFLICT;
    case WORR_NATIVE_ENVELOPE_REJECT_DUPLICATE_CONFLICT:
        counter_increment(&session->telemetry.duplicate_conflicts);
        return WORR_NATIVE_RX_DUPLICATE_CONFLICT;
    case WORR_NATIVE_ENVELOPE_REJECT_MESSAGE_CHECKSUM:
        counter_increment(&session->telemetry.message_checksum_failures);
        return WORR_NATIVE_RX_MESSAGE_CHECKSUM;
    case WORR_NATIVE_ENVELOPE_REJECT_STORAGE_CAPACITY:
        counter_increment(&session->telemetry.storage_stalls);
        return WORR_NATIVE_RX_STORAGE_CAPACITY;
    case WORR_NATIVE_ENVELOPE_REJECT_UNSUPPORTED:
        counter_increment(&session->telemetry.unsupported);
        return WORR_NATIVE_RX_UNSUPPORTED;
    case WORR_NATIVE_ENVELOPE_REJECT_DATAGRAM_CHECKSUM:
        counter_increment(&session->telemetry.datagram_corrupt);
        return WORR_NATIVE_RX_DATAGRAM_CORRUPT;
    case WORR_NATIVE_ENVELOPE_REJECT_MALFORMED:
        counter_increment(&session->telemetry.malformed);
        return WORR_NATIVE_RX_MALFORMED;
    case WORR_NATIVE_ENVELOPE_REJECT_INVALID_ARGUMENT:
        return WORR_NATIVE_RX_INVALID_ARGUMENT;
    case WORR_NATIVE_ENVELOPE_REJECT_INVALID_STATE:
    default:
        return WORR_NATIVE_RX_INVALID_STATE;
    }
}

worr_native_rx_result_v1 Worr_NativeRxSessionAcceptV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    void *payload_arena,
    size_t payload_arena_bytes,
    uint64_t now_tick,
    const void *datagram,
    size_t datagram_bytes,
    worr_native_rx_message_v1 *message_out,
    worr_native_ack_range_v1 *repeat_acknowledgement_out)
{
    worr_native_envelope_frame_info_v1 info;
    worr_native_envelope_frame_info_v1 accepted_info;
    worr_native_rx_slot_v1 updated_slot;
    worr_native_rx_message_v1 completed;
    worr_native_envelope_decode_result_v1 decode;
    worr_native_envelope_accept_result_v1 accepted;
    size_t slots_bytes;
    size_t arena_required;
    int history_index;
    int snapshot_identity_index = -1;
    int sequence_identity_index;
    int slot_index = -1;
    int free_index = -1;
    uint16_t index;
    uint8_t *slot_payload;
    bool new_slot;
    bool snapshot_retry = false;

    if (session == NULL || slots == NULL || payload_arena == NULL ||
        datagram == NULL || message_out == NULL ||
        repeat_acknowledgement_out == NULL)
        return WORR_NATIVE_RX_INVALID_ARGUMENT;
    if (!Worr_NativeRxSessionValidateV1(session, slots, slot_capacity) ||
        (session->state_flags & WORR_NATIVE_RX_TERMINAL_CANCELLED) != 0)
        return WORR_NATIVE_RX_INVALID_STATE;
    slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    arena_required = (size_t)slot_capacity * session->payload_stride;
    if (payload_arena_bytes < arena_required ||
        ranges_overlap(session, sizeof(*session), payload_arena,
                       arena_required) ||
        ranges_overlap(slots, slots_bytes, payload_arena, arena_required) ||
        ranges_overlap(datagram, datagram_bytes, session, sizeof(*session)) ||
        ranges_overlap(datagram, datagram_bytes, slots, slots_bytes) ||
        ranges_overlap(datagram, datagram_bytes,
                       payload_arena, arena_required) ||
        ranges_overlap(message_out, sizeof(*message_out),
                       session, sizeof(*session)) ||
        ranges_overlap(message_out, sizeof(*message_out),
                       slots, slots_bytes) ||
        ranges_overlap(message_out, sizeof(*message_out),
                       payload_arena, arena_required) ||
        ranges_overlap(message_out, sizeof(*message_out),
                       datagram, datagram_bytes) ||
        ranges_overlap(repeat_acknowledgement_out,
                       sizeof(*repeat_acknowledgement_out),
                       session, sizeof(*session)) ||
        ranges_overlap(repeat_acknowledgement_out,
                       sizeof(*repeat_acknowledgement_out),
                       slots, slots_bytes) ||
        ranges_overlap(repeat_acknowledgement_out,
                       sizeof(*repeat_acknowledgement_out),
                       payload_arena, arena_required) ||
        ranges_overlap(repeat_acknowledgement_out,
                       sizeof(*repeat_acknowledgement_out),
                       datagram, datagram_bytes) ||
        ranges_overlap(repeat_acknowledgement_out,
                       sizeof(*repeat_acknowledgement_out),
                       message_out, sizeof(*message_out))) {
        return WORR_NATIVE_RX_INVALID_ARGUMENT;
    }
    if (now_tick < session->last_tick) {
        counter_increment(&session->telemetry.clock_regressions);
        return WORR_NATIVE_RX_CLOCK_REGRESSION;
    }

    counter_increment(&session->telemetry.datagram_attempts);
    decode = Worr_NativeEnvelopeDecodeV1(datagram, datagram_bytes, &info);
    if (decode != WORR_NATIVE_ENVELOPE_DECODE_OK)
        return rx_decode_result(session, decode);
    if (info.transport_epoch != session->transport_epoch) {
        counter_increment(&session->telemetry.wrong_epoch);
        return WORR_NATIVE_RX_WRONG_EPOCH;
    }
    if (info.total_payload_bytes > session->payload_stride) {
        counter_increment(&session->telemetry.storage_stalls);
        return WORR_NATIVE_RX_STORAGE_CAPACITY;
    }

    (void)rx_expire_internal(session, slots, slot_capacity, now_tick);
    history_index = rx_history_find(session, info.message_sequence);
    if (history_index >= 0) {
        if (!receipt_identity_matches(&session->history[history_index],
                                      &info)) {
            counter_increment(&session->telemetry.message_conflicts);
            return WORR_NATIVE_RX_MESSAGE_CONFLICT;
        }
        counter_increment(&session->telemetry.already_committed);
        counter_increment(&session->telemetry.repeat_acknowledgements);
        make_single_ack(session->transport_epoch,
                        session->connection_owner_id,
                        info.message_sequence,
                        repeat_acknowledgement_out);
        return WORR_NATIVE_RX_ALREADY_COMMITTED;
    }

    sequence_identity_index = snapshot_identity_find_message_sequence(
        session, info.message_sequence);
    if (sequence_identity_index >= 0 &&
        !snapshot_identity_matches_info(
            &session->snapshot_tombstones[sequence_identity_index],
            &info)) {
        counter_increment(&session->telemetry.message_conflicts);
        return WORR_NATIVE_RX_MESSAGE_CONFLICT;
    }

    if (info.record.record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1) {
        snapshot_identity_index = snapshot_identity_find_canonical(
            session, info.record);
        if (snapshot_identity_index >= 0) {
            const worr_native_snapshot_identity_v1 *identity =
                &session->snapshot_tombstones[snapshot_identity_index];

            if (!snapshot_identity_matches_info(identity, &info)) {
                counter_increment(&session->telemetry.message_conflicts);
                return WORR_NATIVE_RX_MESSAGE_CONFLICT;
            }
            if (identity->state_flags ==
                WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED) {
                counter_increment(&session->telemetry.already_committed);
                counter_increment(
                    &session->telemetry.repeat_acknowledgements);
                make_single_ack(session->transport_epoch,
                                session->connection_owner_id,
                                info.message_sequence,
                                repeat_acknowledgement_out);
                return WORR_NATIVE_RX_ALREADY_COMMITTED;
            }
            snapshot_retry = true;
        }
        if (session->committed_snapshot_epoch != 0 &&
            object_id_compare(info.record.object_epoch,
                              info.record.object_sequence,
                              session->committed_snapshot_epoch,
                              session->committed_snapshot_sequence) <= 0) {
            counter_increment(&session->telemetry.stale_snapshots);
            return WORR_NATIVE_RX_STALE_SNAPSHOT;
        }
    }
    for (index = 0; index < slot_capacity; ++index) {
        if (slots[index].state_flags == 0 ||
            slots[index].reassembly.message_sequence !=
                info.message_sequence) {
            continue;
        }
        if (!frame_identity_matches_reassembly(
                &slots[index].reassembly, &info)) {
            counter_increment(&session->telemetry.message_conflicts);
            return WORR_NATIVE_RX_MESSAGE_CONFLICT;
        }
        if ((slots[index].state_flags &
             WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY) != 0) {
            snapshot_retry = true;
        }
        break;
    }
    if (!snapshot_retry && session->highest_committed_sequence != 0 &&
        info.message_sequence <= session->highest_committed_sequence &&
        session->highest_committed_sequence - info.message_sequence >=
            WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY) {
        counter_increment(&session->telemetry.stale_replays);
        return WORR_NATIVE_RX_STALE_REPLAY;
    }

    for (index = 0; index < slot_capacity; ++index) {
        if (slots[index].state_flags == 0) {
            if (free_index < 0)
                free_index = (int)index;
        } else if (slots[index].reassembly.message_sequence ==
                   info.message_sequence) {
            slot_index = (int)index;
        }
        if (slots[index].state_flags != 0 &&
            info.record.record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
            slots[index].reassembly.record.record_class ==
                WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
            record_equal(slots[index].reassembly.record, info.record) &&
            !frame_identity_matches_reassembly(
                &slots[index].reassembly, &info)) {
            counter_increment(&session->telemetry.message_conflicts);
            return WORR_NATIVE_RX_MESSAGE_CONFLICT;
        }
    }
    if (slot_index >= 0 &&
        !frame_identity_matches_reassembly(
            &slots[slot_index].reassembly, &info)) {
        counter_increment(&session->telemetry.message_conflicts);
        return WORR_NATIVE_RX_MESSAGE_CONFLICT;
    }
    if (slot_index < 0 && free_index < 0 &&
        info.record.record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1) {
        int victim = -1;

        for (index = 0; index < slot_capacity; ++index) {
            if (slots[index].state_flags == 0 ||
                slots[index].reassembly.record.record_class !=
                    WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
                (slots[index].state_flags &
                 WORR_NATIVE_RX_SLOT_COMPLETE) != 0 ||
                object_id_compare(
                    info.record.object_epoch,
                    info.record.object_sequence,
                    slots[index].reassembly.record.object_epoch,
                    slots[index].reassembly.record.object_sequence) <= 0) {
                continue;
            }
            if (victim < 0 || object_id_compare(
                                  slots[index].reassembly.record.object_epoch,
                                  slots[index].reassembly.record.object_sequence,
                                  slots[victim].reassembly.record.object_epoch,
                                  slots[victim].reassembly.record.object_sequence) <
                                  0) {
                victim = (int)index;
            }
        }
        if (victim >= 0) {
            snapshot_identity_store(
                session,
                snapshot_identity_from_reassembly(
                    &slots[victim].reassembly,
                    WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY));
            memset(&slots[victim], 0, sizeof(slots[victim]));
            --session->occupied_count;
            counter_increment(&session->telemetry.superseded_snapshots);
            free_index = victim;
        }
    }
    if (slot_index < 0) {
        if (free_index < 0) {
            counter_increment(&session->telemetry.capacity_stalls);
            return WORR_NATIVE_RX_CAPACITY;
        }
        slot_index = free_index;
    }

    new_slot = slots[slot_index].state_flags == 0;
    if (new_slot) {
        memset(&updated_slot, 0, sizeof(updated_slot));
        Worr_NativeEnvelopeReassemblyResetV1(&updated_slot.reassembly);
        updated_slot.first_fragment_tick = now_tick;
        updated_slot.last_activity_tick = now_tick;
        updated_slot.state_flags =
            WORR_NATIVE_RX_SLOT_OCCUPIED |
            (snapshot_retry ? WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY : 0u);
    } else {
        updated_slot = slots[slot_index];
    }
    slot_payload = (uint8_t *)payload_arena +
                   (size_t)slot_index * session->payload_stride;
    accepted = Worr_NativeEnvelopeReassemblyAcceptV1(
        &updated_slot.reassembly, slot_payload, session->payload_stride,
        datagram, datagram_bytes, &accepted_info);

    if (accepted == WORR_NATIVE_ENVELOPE_ACCEPTED ||
        accepted == WORR_NATIVE_ENVELOPE_ACCEPTED_COMPLETE) {
        updated_slot.last_activity_tick = now_tick;
        if (accepted == WORR_NATIVE_ENVELOPE_ACCEPTED_COMPLETE)
            updated_slot.state_flags |= WORR_NATIVE_RX_SLOT_COMPLETE;
        slots[slot_index] = updated_slot;
        if (new_slot)
            ++session->occupied_count;
        if (new_slot && snapshot_retry) {
            snapshot_identity_store(
                session,
                snapshot_identity_from_info(
                    &info, WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY));
        }
        if (accepted == WORR_NATIVE_ENVELOPE_ACCEPTED) {
            counter_increment(&session->telemetry.fragments_accepted);
            return WORR_NATIVE_RX_FRAGMENT_ACCEPTED;
        }

        memset(&completed, 0, sizeof(completed));
        completed.struct_size = sizeof(completed);
        completed.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
        completed.slot_index = (uint32_t)slot_index;
        completed.payload_offset =
            (uint32_t)((size_t)slot_index * session->payload_stride);
        completed.record = accepted_info.record;
        completed.transport_epoch = accepted_info.transport_epoch;
        completed.message_sequence = accepted_info.message_sequence;
        completed.payload_bytes = accepted_info.total_payload_bytes;
        completed.payload_crc32 = accepted_info.payload_crc32;
        completed.connection_owner_id = session->connection_owner_id;
        counter_increment(&session->telemetry.fragments_accepted);
        counter_increment(&session->telemetry.messages_completed);
        *message_out = completed;
        return WORR_NATIVE_RX_MESSAGE_COMPLETE;
    }
    if (accepted == WORR_NATIVE_ENVELOPE_ACCEPTED_DUPLICATE) {
        counter_increment(&session->telemetry.fragment_duplicates);
        return WORR_NATIVE_RX_FRAGMENT_DUPLICATE;
    }
    if (accepted == WORR_NATIVE_ENVELOPE_REJECT_MESSAGE_CHECKSUM &&
        info.record.record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1) {
        snapshot_identity_store(
            session,
            snapshot_identity_from_info(
                &info, WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY));
    }
    if (accepted == WORR_NATIVE_ENVELOPE_REJECT_MESSAGE_CHECKSUM) {
        if (!new_slot) {
            memset(&slots[slot_index], 0, sizeof(slots[slot_index]));
            --session->occupied_count;
        }
    }
    return rx_accept_result(session, accepted);
}

static void rx_history_insert(worr_native_rx_session_v1 *session,
                              const worr_native_rx_slot_v1 *slot)
{
    worr_native_receipt_history_entry_v1 entry;
    const uint32_t sequence = slot->reassembly.message_sequence;
    const uint16_t index = (uint16_t)(
        sequence % WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY);
    uint16_t history_index;

    memset(&entry, 0, sizeof(entry));
    entry.record = slot->reassembly.record;
    entry.message_sequence = sequence;
    entry.total_payload_bytes = slot->reassembly.total_payload_bytes;
    entry.payload_crc32 = slot->reassembly.payload_crc32;
    entry.fragment_stride = slot->reassembly.fragment_stride;
    entry.fragment_count = slot->reassembly.fragment_count;
    entry.priority = slot->reassembly.priority;
    if (sequence > session->highest_committed_sequence)
        session->highest_committed_sequence = sequence;
    for (history_index = 0;
         history_index < WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY;
         ++history_index) {
        if (session->history[history_index].message_sequence != 0 &&
            session->highest_committed_sequence -
                    session->history[history_index].message_sequence >=
                WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY) {
            memset(&session->history[history_index], 0,
                   sizeof(session->history[history_index]));
        }
    }
    session->history[index] = entry;
    session->history_count = 0;
    session->receipt_mask = 0;
    for (history_index = 0;
         history_index < WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY;
         ++history_index) {
        uint32_t distance;

        if (session->history[history_index].message_sequence == 0)
            continue;
        distance = session->highest_committed_sequence -
                   session->history[history_index].message_sequence;
        session->receipt_mask |= UINT64_C(1) << distance;
        ++session->history_count;
    }
}

static bool rx_history_can_insert(const worr_native_rx_session_v1 *session,
                                  uint32_t message_sequence)
{
    return session->highest_committed_sequence == 0 ||
           message_sequence > session->highest_committed_sequence ||
           session->highest_committed_sequence - message_sequence <
               WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY;
}

static bool snapshot_slot_has_retry_identity(
    const worr_native_rx_session_v1 *session,
    const worr_native_rx_slot_v1 *slot)
{
    (void)session;
    return (slot->state_flags &
            WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY) != 0;
}

static void snapshot_identities_prune_committed(
    worr_native_rx_session_v1 *session,
    const worr_native_snapshot_identity_v1 *committed)
{
    uint16_t index = 0;

    while (index < session->snapshot_tombstone_count) {
        const int comparison = object_id_compare(
            session->snapshot_tombstones[index].record.object_epoch,
            session->snapshot_tombstones[index].record.object_sequence,
            committed->record.object_epoch,
            committed->record.object_sequence);

        if (comparison < 0 ||
            (comparison == 0 &&
             !snapshot_identity_same_message(
                 &session->snapshot_tombstones[index], committed))) {
            snapshot_identity_remove(session, index);
            continue;
        }
        ++index;
    }
}

worr_native_rx_result_v1 Worr_NativeRxSessionCommitV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint32_t slot_index,
    uint32_t message_sequence,
    worr_native_ack_range_v1 *acknowledgement_out)
{
    worr_native_ack_range_v1 acknowledgement;
    worr_native_snapshot_identity_v1 committed_snapshot;
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    bool is_snapshot;
    uint16_t index;

    if (session == NULL || slots == NULL || acknowledgement_out == NULL ||
        slot_index >= slot_capacity || message_sequence == 0 ||
        ranges_overlap(acknowledgement_out, sizeof(*acknowledgement_out),
                       session, sizeof(*session)) ||
        ranges_overlap(acknowledgement_out, sizeof(*acknowledgement_out),
                       slots, slots_bytes)) {
        return WORR_NATIVE_RX_INVALID_ARGUMENT;
    }
    if (!Worr_NativeRxSessionValidateV1(session, slots, slot_capacity) ||
        (session->state_flags & WORR_NATIVE_RX_TERMINAL_CANCELLED) != 0)
        return WORR_NATIVE_RX_INVALID_STATE;
    if (slots[slot_index].state_flags == 0 ||
        slots[slot_index].reassembly.message_sequence != message_sequence)
        return WORR_NATIVE_RX_NOT_FOUND;
    if ((slots[slot_index].state_flags &
         WORR_NATIVE_RX_SLOT_COMPLETE) == 0) {
        return WORR_NATIVE_RX_NOT_COMPLETE;
    }
    if (rx_history_find(session, message_sequence) >= 0)
        return WORR_NATIVE_RX_INVALID_STATE;

    is_snapshot = slots[slot_index].reassembly.record.record_class ==
                  WORR_NATIVE_RECORD_SNAPSHOT_V1;
    if (is_snapshot && session->committed_snapshot_epoch != 0 &&
        object_id_compare(
            slots[slot_index].reassembly.record.object_epoch,
            slots[slot_index].reassembly.record.object_sequence,
            session->committed_snapshot_epoch,
            session->committed_snapshot_sequence) <= 0) {
        return WORR_NATIVE_RX_STALE_SNAPSHOT;
    }
    if (!rx_history_can_insert(session, message_sequence) && !is_snapshot)
        return WORR_NATIVE_RX_INVALID_STATE;

    make_single_ack(session->transport_epoch,
                    session->connection_owner_id,
                    message_sequence, &acknowledgement);
    if (rx_history_can_insert(session, message_sequence))
        rx_history_insert(session, &slots[slot_index]);

    if (is_snapshot) {
        committed_snapshot = snapshot_identity_from_reassembly(
            &slots[slot_index].reassembly,
            WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED);
        session->committed_snapshot_epoch =
            committed_snapshot.record.object_epoch;
        session->committed_snapshot_sequence =
            committed_snapshot.record.object_sequence;
        snapshot_identity_store(session, committed_snapshot);
        snapshot_identities_prune_committed(session, &committed_snapshot);
    }
    for (index = 0; index < slot_capacity; ++index) {
        bool superseded_snapshot = false;
        bool stale_transport = false;

        if (index == slot_index || slots[index].state_flags == 0)
            continue;

        if (is_snapshot &&
            slots[index].reassembly.record.record_class ==
                WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
            object_id_compare(
                slots[index].reassembly.record.object_epoch,
                slots[index].reassembly.record.object_sequence,
                session->committed_snapshot_epoch,
                session->committed_snapshot_sequence) <= 0) {
            superseded_snapshot = true;
        }
        if (session->highest_committed_sequence != 0 &&
            slots[index].reassembly.message_sequence <=
                session->highest_committed_sequence &&
            session->highest_committed_sequence -
                    slots[index].reassembly.message_sequence >=
                WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY &&
            !(slots[index].reassembly.record.record_class ==
                  WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
              snapshot_slot_has_retry_identity(session, &slots[index]))) {
            stale_transport = true;
        }
        if (!superseded_snapshot && !stale_transport)
            continue;

        if (superseded_snapshot)
            counter_increment(&session->telemetry.superseded_snapshots);
        if (stale_transport)
            counter_increment(&session->telemetry.stale_replays);
        if (slots[index].reassembly.record.record_class ==
                WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
            !superseded_snapshot) {
            snapshot_identity_store(
                session,
                snapshot_identity_from_reassembly(
                    &slots[index].reassembly,
                    WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY));
        }
        memset(&slots[index], 0, sizeof(slots[index]));
        --session->occupied_count;
    }
    memset(&slots[slot_index], 0, sizeof(slots[slot_index]));
    --session->occupied_count;
    counter_increment(&session->telemetry.commits);
    *acknowledgement_out = acknowledgement;
    return WORR_NATIVE_RX_COMMITTED;
}

worr_native_rx_result_v1 Worr_NativeRxSessionDiscardV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint32_t slot_index,
    uint32_t message_sequence)
{
    if (session == NULL || slots == NULL || slot_index >= slot_capacity ||
        message_sequence == 0)
        return WORR_NATIVE_RX_INVALID_ARGUMENT;
    if (!Worr_NativeRxSessionValidateV1(session, slots, slot_capacity) ||
        (session->state_flags & WORR_NATIVE_RX_TERMINAL_CANCELLED) != 0)
        return WORR_NATIVE_RX_INVALID_STATE;
    if (slots[slot_index].state_flags == 0 ||
        slots[slot_index].reassembly.message_sequence != message_sequence)
        return WORR_NATIVE_RX_NOT_FOUND;

    if (slots[slot_index].reassembly.record.record_class ==
        WORR_NATIVE_RECORD_SNAPSHOT_V1) {
        snapshot_identity_store(
            session,
            snapshot_identity_from_reassembly(
                &slots[slot_index].reassembly,
                WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY));
    }
    memset(&slots[slot_index], 0, sizeof(slots[slot_index]));
    --session->occupied_count;
    counter_increment(&session->telemetry.discards);
    return WORR_NATIVE_RX_DISCARDED;
}

worr_native_rx_result_v1 Worr_NativeRxSessionExpireV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint64_t now_tick,
    uint32_t *expired_count_out)
{
    uint32_t expired;

    if (session == NULL || slots == NULL || expired_count_out == NULL ||
        ranges_overlap(expired_count_out, sizeof(*expired_count_out),
                       session, sizeof(*session)) ||
        ranges_overlap(expired_count_out, sizeof(*expired_count_out),
                       slots, (size_t)slot_capacity * sizeof(*slots))) {
        return WORR_NATIVE_RX_INVALID_ARGUMENT;
    }
    if (!Worr_NativeRxSessionValidateV1(session, slots, slot_capacity) ||
        (session->state_flags & WORR_NATIVE_RX_TERMINAL_CANCELLED) != 0)
        return WORR_NATIVE_RX_INVALID_STATE;
    if (now_tick < session->last_tick) {
        counter_increment(&session->telemetry.clock_regressions);
        return WORR_NATIVE_RX_CLOCK_REGRESSION;
    }
    expired = rx_expire_internal(session, slots, slot_capacity, now_tick);
    *expired_count_out = expired;
    return expired == 0 ? WORR_NATIVE_RX_IDLE : WORR_NATIVE_RX_EXPIRED;
}
