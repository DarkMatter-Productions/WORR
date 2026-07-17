/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server/native_shadow.h"

#include "common/net/native_event_sender.h"
#include "common/net/native_snapshot_sender.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * Server connection setup and parsing are single-threaded.  These counters
 * deliberately live outside svs so SV_Shutdown and slot reuse cannot recycle
 * local provenance during this process lifetime.
 */
static uint64_t next_connection_owner_id;
static uint32_t next_private_transport_epoch;
static uint32_t next_event_stream_epoch;
static uint64_t next_readiness_nonce;

enum {
    SV_NATIVE_SHADOW_TX_EMIT_NONE = 0,
    SV_NATIVE_SHADOW_TX_EMIT_ACK = 1,
    SV_NATIVE_SHADOW_TX_EMIT_EVENT_MIXED = 2,
    SV_NATIVE_SHADOW_TX_EMIT_SNAPSHOT_MIXED = 3,
};

struct sv_native_shadow_event_state_v1_s {
    uint32_t current_sender_initialized;
    uint32_t retired_sender_initialized;
    uint32_t stream_epoch;
    uint32_t reserved0;
    uint64_t snapshots_queued;
    uint64_t snapshot_queue_failures;
    worr_native_event_sender_v1 current_sender;
    worr_native_event_sender_v1 retired_sender;
    worr_event_record_v1
        candidate_scratch[WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY];
};

struct sv_native_shadow_snapshot_state_v1_s {
    uint32_t current_sender_initialized;
    uint32_t retired_sender_initialized;
    uint32_t snapshot_epoch;
    uint32_t reserved0;
    uint64_t snapshots_queued;
    uint64_t snapshot_queue_failures;
    worr_native_snapshot_sender_v1 current_sender;
    worr_native_snapshot_sender_v1 retired_sender;
};

static bool pump_event_sender_at_tick(
    sv_native_shadow_peer_v1 *peer,
    uint64_t now_tick,
    uint32_t *promoted_out);

static void saturating_increment(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static void saturating_increment_u32(uint32_t *value)
{
    if (*value != UINT32_MAX)
        ++*value;
}

static void saturating_add(uint64_t *value, uint64_t amount)
{
    if (UINT64_MAX - *value < amount)
        *value = UINT64_MAX;
    else
        *value += amount;
}

static bool allocate_owner(uint64_t *owner_out)
{
    if (owner_out == NULL ||
        next_connection_owner_id == UINT64_MAX) {
        return false;
    }
    *owner_out = ++next_connection_owner_id;
    return true;
}

static bool allocate_epoch_and_nonce(uint32_t *epoch_out,
                                     uint64_t *nonce_out)
{
    if (epoch_out == NULL || nonce_out == NULL ||
        next_private_transport_epoch == UINT32_MAX ||
        next_readiness_nonce == UINT64_MAX) {
        return false;
    }
    *epoch_out = ++next_private_transport_epoch;
    *nonce_out = ++next_readiness_nonce;
    return true;
}

static bool allocate_event_stream_epoch(uint32_t *epoch_out)
{
    if (epoch_out == NULL || next_event_stream_epoch == UINT32_MAX)
        return false;
    *epoch_out = ++next_event_stream_epoch;
    return true;
}

static bool peer_structural_valid(const sv_native_shadow_peer_v1 *peer)
{
    return peer != NULL &&
           peer->version == SV_NATIVE_SHADOW_VERSION &&
           peer->initialized == 1 && peer->netchan != NULL &&
           peer->connection_owner_id != 0 &&
           (peer->mode == SV_NATIVE_SHADOW_MODE_COMMAND ||
            peer->mode == SV_NATIVE_SHADOW_MODE_EVENT ||
            peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT) &&
           ((peer->mode == SV_NATIVE_SHADOW_MODE_COMMAND &&
             peer->event_state == NULL &&
             peer->snapshot_state == NULL) ||
            (peer->mode == SV_NATIVE_SHADOW_MODE_EVENT &&
             peer->event_state != NULL &&
             peer->snapshot_state == NULL) ||
            (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT &&
             peer->event_state == NULL &&
             peer->snapshot_state != NULL)) &&
           peer->clock_initialized == 1 &&
           peer->reserved1 == 0 &&
           peer->lifecycle >=
               SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS &&
           peer->lifecycle <= SV_NATIVE_SHADOW_LIFECYCLE_DETACHED &&
           peer->transport_initialized <= 1 &&
           peer->retired_transport_initialized <= 1 &&
           peer->activation_pending <= 1 &&
           peer->carrier_traffic_seen <= 1 &&
           peer->native_wire_committed <= 1 &&
           ((peer->native_wire_committed == 0 &&
             peer->wire_committed_transport_epoch == 0) ||
            (peer->native_wire_committed == 1 &&
             peer->wire_committed_transport_epoch != 0)) &&
           peer->pending_native_valid <= 1 &&
           (peer->pending_native_valid == 0 ||
            (peer->pending_native_id.epoch != 0 &&
             peer->pending_native_id.sequence != 0)) &&
           peer->matched_native_highwater_valid <= 1 &&
           (peer->matched_native_highwater_valid == 0 ||
            (peer->matched_native_highwater.epoch != 0 &&
             peer->matched_native_highwater.sequence != 0)) &&
           peer->ack_emit_active <= 1 &&
           peer->ack_emit_bank <=
               SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED &&
           peer->tx_emit_kind <=
               SV_NATIVE_SHADOW_TX_EMIT_SNAPSHOT_MIXED &&
           ((peer->ack_emit_active == 0 &&
             peer->tx_emit_kind == SV_NATIVE_SHADOW_TX_EMIT_NONE &&
             peer->ack_emit_bank ==
                 SV_NATIVE_SHADOW_TRANSPORT_BANK_NONE) ||
            (peer->ack_emit_active == 1 &&
             peer->tx_emit_kind != SV_NATIVE_SHADOW_TX_EMIT_NONE &&
             peer->ack_emit_bank !=
                 SV_NATIVE_SHADOW_TRANSPORT_BANK_NONE)) &&
           ((peer->tx_emit_kind !=
                 SV_NATIVE_SHADOW_TX_EMIT_EVENT_MIXED &&
             peer->tx_emit_kind !=
                 SV_NATIVE_SHADOW_TX_EMIT_SNAPSHOT_MIXED) ||
            peer->ack_emit_bank ==
                SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT) &&
           peer->async_wake_active <= 1 &&
           peer->async_wake_handoff_seen <= 1 &&
           (peer->async_wake_active != 0 ||
            peer->async_wake_handoff_seen == 0) &&
           (peer->ack_next_bank ==
                SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT ||
            peer->ack_next_bank ==
                SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED);
}

static bool event_state_valid(const sv_native_shadow_peer_v1 *peer)
{
    const sv_native_shadow_event_state_v1 *state;

    if (!peer_structural_valid(peer))
        return false;
    if (peer->mode != SV_NATIVE_SHADOW_MODE_EVENT)
        return true;
    state = peer->event_state;
    if (state->current_sender_initialized > 1 ||
        state->retired_sender_initialized > 1 || state->reserved0 != 0)
        return false;
    if (state->current_sender_initialized) {
        if (state->stream_epoch == 0 ||
            !Worr_NativeEventSenderValidateV1(&state->current_sender) ||
            state->current_sender.descriptor.stream_epoch !=
                state->stream_epoch ||
            (state->current_sender.state_flags &
             WORR_NATIVE_EVENT_SENDER_RETIRED) != 0) {
            return false;
        }
    }
    if (state->retired_sender_initialized &&
        (!Worr_NativeEventSenderValidateV1(&state->retired_sender) ||
         (state->retired_sender.state_flags &
          WORR_NATIVE_EVENT_SENDER_RETIRED) == 0)) {
        return false;
    }
    return true;
}

static bool snapshot_state_valid(const sv_native_shadow_peer_v1 *peer)
{
    const sv_native_shadow_snapshot_state_v1 *state;

    if (!peer_structural_valid(peer))
        return false;
    if (peer->mode != SV_NATIVE_SHADOW_MODE_SNAPSHOT)
        return true;
    state = peer->snapshot_state;
    if (state->current_sender_initialized > 1 ||
        state->retired_sender_initialized > 1 ||
        state->reserved0 != 0 ||
        ((state->current_sender_initialized != 0 ||
          state->retired_sender_initialized != 0) &&
         state->snapshot_epoch == 0)) {
        return false;
    }
    if (state->current_sender_initialized &&
        (!Worr_NativeSnapshotSenderValidateV1(
             &state->current_sender) ||
         state->current_sender.binding.negotiated_capabilities !=
             WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK ||
         (state->current_sender.state_flags &
          WORR_NATIVE_SNAPSHOT_SENDER_RETIRED) != 0)) {
        return false;
    }
    if (state->retired_sender_initialized &&
        (!Worr_NativeSnapshotSenderValidateV1(
             &state->retired_sender) ||
         (state->retired_sender.state_flags &
          WORR_NATIVE_SNAPSHOT_SENDER_RETIRED) == 0)) {
        return false;
    }
    return true;
}

static bool extend_clock(sv_native_shadow_peer_v1 *peer,
                         uint32_t raw_time_ms,
                         uint64_t *extended_out)
{
    uint32_t delta;

    if (!peer_structural_valid(peer) || extended_out == NULL)
        return false;
    delta = raw_time_ms - peer->clock_last_raw;
    /* A real forward gap can never be distinguished from a regression after
     * half the 32-bit domain, so fail closed instead of guessing. */
    if (delta > INT32_MAX || peer->clock_ticks > UINT64_MAX - delta)
        return false;
    peer->clock_last_raw = raw_time_ms;
    peer->clock_ticks += delta;
    *extended_out = peer->clock_ticks;
    return true;
}

static bool project_clock(const sv_native_shadow_peer_v1 *peer,
                          uint32_t raw_time_ms,
                          uint64_t *extended_out)
{
    uint32_t delta;

    if (!peer_structural_valid(peer) || extended_out == NULL)
        return false;
    delta = raw_time_ms - peer->clock_last_raw;
    if (delta > INT32_MAX || peer->clock_ticks > UINT64_MAX - delta)
        return false;
    *extended_out = peer->clock_ticks + delta;
    return true;
}

static bool ack_due_at_tick(
    const sv_native_shadow_peer_v1 *peer,
    uint64_t now_tick,
    bool *due_out)
{
    worr_native_carrier_ack_result_v1 current;
    worr_native_carrier_ack_result_v1 retired;

    if (!peer_structural_valid(peer) || due_out == NULL)
        return false;
    current = peer->transport_initialized
        ? Worr_NativeCarrierAckPeekDueV1(
              &peer->transport.ack_ledger, now_tick,
              SV_NATIVE_SHADOW_ACK_RETRY_MS)
        : WORR_NATIVE_CARRIER_ACK_NOT_DUE;
    retired = peer->retired_transport_initialized
        ? Worr_NativeCarrierAckPeekDueV1(
              &peer->retired_transport.ack_ledger, now_tick,
              SV_NATIVE_SHADOW_ACK_RETRY_MS)
        : WORR_NATIVE_CARRIER_ACK_NOT_DUE;
    if ((current != WORR_NATIVE_CARRIER_ACK_OK &&
         current != WORR_NATIVE_CARRIER_ACK_NOT_DUE) ||
        (retired != WORR_NATIVE_CARRIER_ACK_OK &&
         retired != WORR_NATIVE_CARRIER_ACK_NOT_DUE)) {
        return false;
    }
    *due_out = current == WORR_NATIVE_CARRIER_ACK_OK ||
               retired == WORR_NATIVE_CARRIER_ACK_OK;
    return true;
}

static bool transport_valid(
    const sv_native_shadow_transport_v1 *transport)
{
    return transport != NULL &&
           transport->official_connection_epoch != 0 &&
           transport->reserved0 == 0 &&
           Worr_NativeSessionBindingValidateV1(&transport->binding) &&
           Worr_NativeRxSessionValidateV1(
               &transport->rx_session, transport->rx_slots,
               SV_NATIVE_SHADOW_RX_SLOT_CAPACITY) &&
           Worr_NativeCarrierAckLedgerValidateV1(
               &transport->ack_ledger) &&
           Worr_NativeCommandShadowJoinValidateV1(
               &transport->command_join);
}

static bool transport_cancellable(
    const sv_native_shadow_transport_v1 *transport)
{
    return transport_valid(transport) &&
           (transport->ack_ledger.state_flags &
            WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) == 0;
}

static bool event_sender_cancellable(
    const worr_native_event_sender_v1 *sender)
{
    return Worr_NativeEventSenderValidateV1(sender) &&
           (sender->state_flags &
            WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED) == 0;
}

static bool snapshot_sender_cancellable(
    const worr_native_snapshot_sender_v1 *sender)
{
    return Worr_NativeSnapshotSenderValidateV1(sender) &&
           (sender->state_flags &
            WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED) == 0;
}

/*
 * A fresh cancellation-capable CHALLENGE is a generational barrier, not a
 * second retired-bank rotation.  Validate every component before the first
 * mutation; server connection processing is single-threaded, so the explicit
 * cancel calls are then an infallible publish sequence.  Each core records a
 * counted terminal disposition before its enclosing adapter storage is reset.
 */
static bool cancel_prior_native_epochs(
    sv_native_shadow_peer_v1 *peer,
    uint32_t new_transport_epoch)
{
    sv_native_shadow_event_state_v1 *event_state;
    sv_native_shadow_snapshot_state_v1 *snapshot_state;
    worr_native_event_sender_cancel_report_v1 event_report;
    worr_native_snapshot_sender_cancel_report_v1 snapshot_report;
    worr_native_rx_cancel_report_v1 rx_report;
    uint32_t receipts;
    uint32_t cancelled_through;
    uint64_t cancelled_events = 0;
    uint64_t cancelled_snapshots = 0;
    uint64_t cancelled_rx = 0;
    uint64_t cancelled_receipts = 0;
    uint64_t cancelled_transports = 0;
    worr_native_event_sender_result_v1 event_result;
    worr_native_snapshot_sender_result_v1 snapshot_result;
    worr_native_rx_result_v1 rx_result;
    worr_native_carrier_ack_result_v1 ack_result;

    if (!event_state_valid(peer) || !snapshot_state_valid(peer) ||
        new_transport_epoch == 0 ||
        new_transport_epoch <=
            peer->cancelled_through_transport_epoch ||
        peer->ack_emit_active ||
        peer->tx_emit_kind != SV_NATIVE_SHADOW_TX_EMIT_NONE ||
        (peer->transport_initialized &&
         !transport_cancellable(&peer->transport)) ||
        (peer->retired_transport_initialized &&
         !transport_cancellable(&peer->retired_transport))) {
        return false;
    }

    event_state = peer->event_state;
    snapshot_state = peer->snapshot_state;
    if (peer->mode == SV_NATIVE_SHADOW_MODE_EVENT) {
        if (event_state->current_sender_initialized !=
                peer->transport_initialized ||
            event_state->retired_sender_initialized !=
                peer->retired_transport_initialized ||
            (event_state->current_sender_initialized &&
             !event_sender_cancellable(
                 &event_state->current_sender)) ||
            (event_state->retired_sender_initialized &&
             !event_sender_cancellable(
                 &event_state->retired_sender))) {
            return false;
        }
    }
    if (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT) {
        if (snapshot_state->current_sender_initialized !=
                peer->transport_initialized ||
            snapshot_state->retired_sender_initialized !=
                peer->retired_transport_initialized ||
            (snapshot_state->current_sender_initialized &&
             !snapshot_sender_cancellable(
                 &snapshot_state->current_sender)) ||
            (snapshot_state->retired_sender_initialized &&
             !snapshot_sender_cancellable(
                 &snapshot_state->retired_sender))) {
            return false;
        }
    }

    cancelled_through = peer->cancelled_through_transport_epoch;
    /* The barrier covers authority advertised by an earlier CHALLENGE even
     * when that readiness epoch never reached transport activation.  This is
     * what makes consecutive map/challenge advances a monotonic revocation,
     * rather than leaving an uninstantiated epoch between the floor and the
     * newly issued epoch. */
    if (peer->private_transport_epoch != 0) {
        if (peer->private_transport_epoch >= new_transport_epoch)
            return false;
        cancelled_through = max(
            cancelled_through, peer->private_transport_epoch);
    }
    if (peer->transport_initialized) {
        if (peer->transport.binding.transport_epoch >=
            new_transport_epoch) {
            return false;
        }
        cancelled_through = max(
            cancelled_through,
            peer->transport.binding.transport_epoch);
    }
    if (peer->retired_transport_initialized) {
        if (peer->retired_transport.binding.transport_epoch >=
            new_transport_epoch) {
            return false;
        }
        cancelled_through = max(
            cancelled_through,
            peer->retired_transport.binding.transport_epoch);
    }

    if (peer->mode == SV_NATIVE_SHADOW_MODE_EVENT) {
        if (event_state->current_sender_initialized) {
            memset(&event_report, 0, sizeof(event_report));
            event_result = Worr_NativeEventSenderCancelV1(
                &event_state->current_sender, &event_report);
            if (event_result !=
                WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT) {
                return false;
            }
            cancelled_events += event_report.retained_messages;
            cancelled_events += event_report.backlog_candidates;
        }
        if (event_state->retired_sender_initialized) {
            memset(&event_report, 0, sizeof(event_report));
            event_result = Worr_NativeEventSenderCancelV1(
                &event_state->retired_sender, &event_report);
            if (event_result !=
                WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT) {
                return false;
            }
            cancelled_events += event_report.retained_messages;
            cancelled_events += event_report.backlog_candidates;
        }
    }
    if (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT) {
        if (snapshot_state->current_sender_initialized) {
            memset(&snapshot_report, 0, sizeof(snapshot_report));
            snapshot_result = Worr_NativeSnapshotSenderCancelV1(
                &snapshot_state->current_sender, &snapshot_report);
            if (snapshot_result !=
                WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED_RESULT) {
                return false;
            }
            cancelled_snapshots += snapshot_report.retained_messages;
            cancelled_snapshots += snapshot_report.pending_snapshots;
        }
        if (snapshot_state->retired_sender_initialized) {
            memset(&snapshot_report, 0, sizeof(snapshot_report));
            snapshot_result = Worr_NativeSnapshotSenderCancelV1(
                &snapshot_state->retired_sender, &snapshot_report);
            if (snapshot_result !=
                WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED_RESULT) {
                return false;
            }
            cancelled_snapshots += snapshot_report.retained_messages;
            cancelled_snapshots += snapshot_report.pending_snapshots;
        }
    }

#define CANCEL_TRANSPORT(bank_)                                             \
    do {                                                                    \
        memset(&rx_report, 0, sizeof(rx_report));                           \
        rx_result = Worr_NativeRxSessionCancelPendingV1(                    \
            &(bank_).rx_session, (bank_).rx_slots,                         \
            SV_NATIVE_SHADOW_RX_SLOT_CAPACITY, &rx_report);                 \
        if (rx_result != WORR_NATIVE_RX_CANCELLED)                          \
            return false;                                                   \
        receipts = 0;                                                       \
        ack_result = Worr_NativeCarrierAckCancelAllV1(                      \
            &(bank_).ack_ledger, &receipts);                               \
        if (ack_result != WORR_NATIVE_CARRIER_ACK_OK)                       \
            return false;                                                   \
        cancelled_rx += rx_report.incomplete_messages;                     \
        cancelled_rx += rx_report.complete_messages;                       \
        cancelled_receipts += receipts;                                    \
        ++cancelled_transports;                                             \
    } while (0)

    if (peer->transport_initialized)
        CANCEL_TRANSPORT(peer->transport);
    if (peer->retired_transport_initialized)
        CANCEL_TRANSPORT(peer->retired_transport);

#undef CANCEL_TRANSPORT

    if (peer->mode == SV_NATIVE_SHADOW_MODE_EVENT) {
        memset(&event_state->current_sender, 0,
               sizeof(event_state->current_sender));
        memset(&event_state->retired_sender, 0,
               sizeof(event_state->retired_sender));
        event_state->current_sender_initialized = 0;
        event_state->retired_sender_initialized = 0;
    }
    if (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT) {
        memset(&snapshot_state->current_sender, 0,
               sizeof(snapshot_state->current_sender));
        memset(&snapshot_state->retired_sender, 0,
               sizeof(snapshot_state->retired_sender));
        snapshot_state->current_sender_initialized = 0;
        snapshot_state->retired_sender_initialized = 0;
    }
    memset(&peer->transport, 0, sizeof(peer->transport));
    memset(&peer->retired_transport, 0,
           sizeof(peer->retired_transport));
    peer->transport_initialized = 0;
    peer->retired_transport_initialized = 0;
    peer->cancelled_through_transport_epoch = cancelled_through;
    saturating_increment(&peer->cancellation_barriers);
    saturating_add(&peer->cancelled_transports,
                   cancelled_transports);
    saturating_add(&peer->cancelled_rx_messages, cancelled_rx);
    saturating_add(&peer->cancelled_receipts,
                   cancelled_receipts);
    saturating_add(&peer->cancelled_event_records,
                   cancelled_events);
    saturating_add(&peer->cancelled_snapshot_records,
                   cancelled_snapshots);
    return true;
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

static void enter_drain(sv_native_shadow_peer_v1 *peer,
                        sv_native_shadow_failure_v1 failure,
                        bool disable_readiness)
{
    if (!peer_structural_valid(peer))
        return;
    if (peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DRAIN)
        saturating_increment(&peer->drain_entries);
    peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_DRAIN;
    peer->last_failure = (uint32_t)failure;
    if (failure != SV_NATIVE_SHADOW_FAILURE_NONE)
        saturating_increment(&peer->failures);
    if (!disable_readiness)
        return;
    peer->enabled = 0;
    peer->readiness_initialized = 0;
    peer->activation_pending = 0;
    peer->packet_open = 0;
    memset(&peer->readiness, 0, sizeof(peer->readiness));
    memset(&peer->sideband, 0, sizeof(peer->sideband));
}

static void carrier_reject(sv_native_shadow_peer_v1 *peer,
                           sv_native_shadow_failure_v1 failure)
{
    peer->carrier_traffic_seen = 1;
    saturating_increment(&peer->rx_rejections);
    enter_drain(peer, failure, true);
}

static bool carrier_command_shape(
    const byte *packet,
    const worr_native_carrier_view_v1 *view,
    bool *has_data_out,
    bool *has_ack_out,
    uint16_t *data_entry_out,
    worr_native_envelope_frame_info_v1 *frame_out)
{
    worr_native_envelope_frame_info_v1 frame;
    const worr_native_carrier_entry_v1 *entry = NULL;
    uint16_t data_entry = 0;
    uint16_t index;

    bool has_ack = false;

    if (packet == NULL || view == NULL || has_data_out == NULL ||
        has_ack_out == NULL ||
        data_entry_out == NULL || frame_out == NULL ||
        view->entry_count == 0)
        return false;
    memset(&frame, 0, sizeof(frame));
    for (index = 0; index < view->entry_count; ++index) {
        if (view->entries[index].entry_type ==
            WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
            if (entry != NULL)
                return false;
            entry = &view->entries[index];
            data_entry = index;
        } else if (view->entries[index].entry_type !=
                   WORR_NATIVE_CARRIER_ENTRY_ACK_V1) {
            return false;
        } else {
            has_ack = true;
        }
    }
    if (entry == NULL) {
        *has_data_out = false;
        *has_ack_out = has_ack;
        *data_entry_out = 0;
        memset(frame_out, 0, sizeof(*frame_out));
        return true;
    }
    if (
        entry->data_bytes != SV_NATIVE_SHADOW_WNE_BYTES ||
        entry->data_offset > view->packet_bytes ||
        entry->data_bytes > view->packet_bytes - entry->data_offset ||
        Worr_NativeEnvelopeDecodeV1(
            packet + entry->data_offset, entry->data_bytes, &frame) !=
            WORR_NATIVE_ENVELOPE_DECODE_OK) {
        return false;
    }
    if (frame.transport_epoch != view->transport_epoch ||
        frame.record.record_class != WORR_NATIVE_RECORD_COMMAND_V1 ||
        frame.record.reserved0 != 0 ||
        frame.record.record_schema_version != WORR_COMMAND_ABI_VERSION ||
        frame.total_payload_bytes != SV_NATIVE_SHADOW_PAYLOAD_BYTES ||
        frame.fragment_offset != 0 ||
        frame.fragment_payload_bytes != SV_NATIVE_SHADOW_PAYLOAD_BYTES ||
        frame.fragment_index != 0 || frame.fragment_count != 1 ||
        frame.fragment_stride != SV_NATIVE_SHADOW_PAYLOAD_BYTES ||
        frame.fragment_flags !=
            (WORR_NATIVE_ENVELOPE_FRAGMENT_FIRST |
             WORR_NATIVE_ENVELOPE_FRAGMENT_LAST) ||
        frame.payload_offset != WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES) {
        return false;
    }
    *has_data_out = true;
    *has_ack_out = has_ack;
    *data_entry_out = data_entry;
    *frame_out = frame;
    return true;
}

static sv_native_shadow_transport_v1 *transport_for_epoch(
    sv_native_shadow_peer_v1 *peer, uint32_t transport_epoch,
    sv_native_shadow_transport_bank_v1 *bank_out)
{
    if (peer->transport_initialized &&
        peer->transport.binding.transport_epoch == transport_epoch) {
        *bank_out = SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT;
        return &peer->transport;
    }
    if (peer->retired_transport_initialized &&
        peer->retired_transport.binding.transport_epoch == transport_epoch) {
        *bank_out = SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED;
        return &peer->retired_transport;
    }
    *bank_out = SV_NATIVE_SHADOW_TRANSPORT_BANK_NONE;
    return NULL;
}

static worr_native_event_sender_v1 *event_sender_for_bank(
    sv_native_shadow_peer_v1 *peer,
    sv_native_shadow_transport_bank_v1 bank)
{
    sv_native_shadow_event_state_v1 *state;

    if (!event_state_valid(peer) ||
        peer->mode != SV_NATIVE_SHADOW_MODE_EVENT)
        return NULL;
    state = peer->event_state;
    if (bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT &&
        state->current_sender_initialized)
        return &state->current_sender;
    if (bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED &&
        state->retired_sender_initialized)
        return &state->retired_sender;
    return NULL;
}

static worr_native_snapshot_sender_v1 *snapshot_sender_for_bank(
    sv_native_shadow_peer_v1 *peer,
    sv_native_shadow_transport_bank_v1 bank)
{
    sv_native_shadow_snapshot_state_v1 *state;

    if (!snapshot_state_valid(peer) ||
        peer->mode != SV_NATIVE_SHADOW_MODE_SNAPSHOT)
        return NULL;
    state = peer->snapshot_state;
    if (bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT &&
        state->current_sender_initialized)
        return &state->current_sender;
    if (bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED &&
        state->retired_sender_initialized)
        return &state->retired_sender;
    return NULL;
}

static bool decode_completed_command(
    const sv_native_shadow_transport_v1 *transport,
    const worr_native_rx_message_v1 *message,
    worr_command_record_v1 *record_out)
{
    worr_native_codec_info_v1 codec_info;
    worr_native_record_ref_v1 codec_record;
    const byte *payload;

    if (transport == NULL || message == NULL || record_out == NULL ||
        message->payload_bytes != SV_NATIVE_SHADOW_PAYLOAD_BYTES ||
        message->payload_offset > sizeof(transport->payload_arena) ||
        message->payload_bytes >
            sizeof(transport->payload_arena) - message->payload_offset) {
        return false;
    }
    payload = transport->payload_arena + message->payload_offset;
    if (Worr_NativeCodecInspectV1(
            payload, message->payload_bytes, &codec_info) !=
            WORR_NATIVE_CODEC_OK ||
        !Worr_NativeCodecInfoRecordRefV1(
            &codec_info, &codec_record) ||
        !record_ref_equal(codec_record, message->record) ||
        Worr_NativeCodecCommandDecodeV1(
            payload, message->payload_bytes,
            WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            record_out) != WORR_NATIVE_CODEC_OK ||
        record_out->command_id.epoch != message->record.object_epoch ||
        record_out->command_id.sequence !=
            message->record.object_sequence) {
        return false;
    }
    return true;
}

static void expose_legacy(netchan_app_rx_output_v1_t *output,
                          uint32_t legacy_bytes)
{
    memset(output, 0, sizeof(*output));
    output->abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output->struct_size = sizeof(*output);
    output->legacy_bytes = legacy_bytes;
}

static netchan_app_tx_prepare_result_t native_shadow_tx_prepare(
    void *opaque,
    const netchan_app_tx_prepare_info_v1_t *info,
    const byte *legacy_application,
    byte *candidate_application,
    netchan_app_tx_prepare_output_v1_t *output)
{
    sv_native_shadow_peer_v1 *peer = opaque;
    sv_native_shadow_transport_v1 *selected;
    sv_native_shadow_transport_v1 staged;
    worr_native_event_sender_v1 *event_sender;
    worr_native_snapshot_sender_v1 *snapshot_sender;
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_event_sender_result_v1 event_result;
    worr_native_snapshot_sender_result_v1 snapshot_result;
    worr_native_carrier_ack_result_v1 current_due;
    worr_native_carrier_ack_result_v1 retired_due;
    worr_native_carrier_ack_result_v1 result;
    worr_native_readiness_state_v1 readiness;
    sv_native_shadow_transport_bank_v1 bank;
    bool event_data_due = false;
    bool snapshot_data_due = false;
    bool current_work;
    bool retired_work;
    uint16_t budget;
    size_t packet_bytes;
    uint64_t event_token;

    if (!peer_structural_valid(peer) || !info || !output ||
        info->abi_version != NETCHAN_APP_TX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info) ||
        info->legacy_application_bytes > info->max_application_bytes ||
        (info->legacy_application_bytes != 0 && !legacy_application) ||
        info->max_application_bytes == 0 || !candidate_application ||
        peer->ack_emit_active ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DETACHED) {
        if (peer_structural_valid(peer))
            saturating_increment(&peer->tx_bypass_calls);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    if (peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
        readiness = peer->readiness;
        if (!Worr_NativeReadinessCanTransmitNativeV1(
                &readiness, peer->clock_ticks)) {
            peer->readiness = readiness;
            saturating_increment(&peer->tx_bypass_calls);
            if (readiness.phase == WORR_NATIVE_READINESS_PHASE_FAILED)
                enter_drain(
                    peer, SV_NATIVE_SHADOW_FAILURE_READINESS, true);
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }
        peer->readiness = readiness;
    }

    current_due = peer->transport_initialized
        ? Worr_NativeCarrierAckPeekDueV1(
              &peer->transport.ack_ledger, peer->clock_ticks,
              SV_NATIVE_SHADOW_ACK_RETRY_MS)
        : WORR_NATIVE_CARRIER_ACK_NOT_DUE;
    retired_due = peer->retired_transport_initialized
        ? Worr_NativeCarrierAckPeekDueV1(
              &peer->retired_transport.ack_ledger, peer->clock_ticks,
              SV_NATIVE_SHADOW_ACK_RETRY_MS)
        : WORR_NATIVE_CARRIER_ACK_NOT_DUE;
    if ((current_due != WORR_NATIVE_CARRIER_ACK_OK &&
         current_due != WORR_NATIVE_CARRIER_ACK_NOT_DUE) ||
        (retired_due != WORR_NATIVE_CARRIER_ACK_OK &&
         retired_due != WORR_NATIVE_CARRIER_ACK_NOT_DUE)) {
        saturating_increment(&peer->tx_bypass_calls);
        enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    event_sender = event_sender_for_bank(
        peer, SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT);
    if (peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
        event_sender != NULL) {
        event_result = Worr_NativeEventSenderDataDuePeekV1(
            event_sender, peer->clock_ticks,
            SV_NATIVE_SHADOW_EVENT_RESEND_MS, &event_data_due);
        if (event_result != WORR_NATIVE_EVENT_SENDER_OK &&
            event_result != WORR_NATIVE_EVENT_SENDER_NOT_DUE) {
            saturating_increment(&peer->tx_bypass_calls);
            enter_drain(
                peer, SV_NATIVE_SHADOW_FAILURE_EVENT_SENDER, true);
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }
    }
    snapshot_sender = snapshot_sender_for_bank(
        peer, SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT);
    if (peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
        snapshot_sender != NULL) {
        snapshot_result = Worr_NativeSnapshotSenderDataDuePeekV1(
            snapshot_sender, peer->clock_ticks,
            SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS,
            &snapshot_data_due);
        if (snapshot_result != WORR_NATIVE_SNAPSHOT_SENDER_OK &&
            snapshot_result !=
                WORR_NATIVE_SNAPSHOT_SENDER_NOT_DUE) {
            saturating_increment(&peer->tx_bypass_calls);
            enter_drain(
                peer, SV_NATIVE_SHADOW_FAILURE_SNAPSHOT_SENDER,
                true);
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }
    }
    current_work = current_due == WORR_NATIVE_CARRIER_ACK_OK ||
                   event_data_due || snapshot_data_due;
    retired_work = retired_due == WORR_NATIVE_CARRIER_ACK_OK;
    if (!current_work && !retired_work) {
        saturating_increment(&peer->tx_bypass_calls);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (current_work && retired_work) {
        bank = (sv_native_shadow_transport_bank_v1)peer->ack_next_bank;
    } else {
        bank = current_work
            ? SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT
            : SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED;
    }
    selected = bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT
        ? &peer->transport : &peer->retired_transport;
    staged = *selected;
    budget = (uint16_t)min(
        info->max_application_bytes,
        (uint32_t)WORR_NATIVE_CARRIER_MAX_PACKET_BYTES);
    if (info->legacy_application_bytes > budget ||
        info->legacy_application_bytes > UINT16_MAX) {
        saturating_increment(&peer->tx_bypass_calls);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (peer->next_completion_token == UINT64_MAX) {
        saturating_increment(&peer->tx_bypass_calls);
        enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    packet_bytes = 0;

    if (bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT &&
        event_data_due && event_sender != NULL) {
        event_token = 0;
        event_result = Worr_NativeEventSenderPrepareMixedV1(
            event_sender, &selected->ack_ledger, peer->clock_ticks,
            SV_NATIVE_SHADOW_EVENT_RESEND_MS,
            SV_NATIVE_SHADOW_ACK_RETRY_MS, budget,
            legacy_application,
            (uint16_t)info->legacy_application_bytes,
            candidate_application, budget, &packet_bytes, &event_token);
        if (event_result == WORR_NATIVE_EVENT_SENDER_OK) {
            peer->ack_emit_active = 1;
            peer->ack_emit_bank =
                SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT;
            peer->tx_emit_kind =
                SV_NATIVE_SHADOW_TX_EMIT_EVENT_MIXED;
            peer->active_completion_token =
                ++peer->next_completion_token;
            peer->ack_next_bank =
                SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED;

            memset(output, 0, sizeof(*output));
            output->abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
            output->struct_size = sizeof(*output);
            output->application_bytes = (uint32_t)packet_bytes;
            output->token = peer->active_completion_token;
            return NETCHAN_APP_TX_PREPARE_PREPARED;
        }
        if (event_result != WORR_NATIVE_EVENT_SENDER_OUTPUT_TOO_SMALL &&
            event_result != WORR_NATIVE_EVENT_SENDER_CAPACITY &&
            event_result != WORR_NATIVE_EVENT_SENDER_NOT_DUE) {
            saturating_increment(&peer->tx_bypass_calls);
            enter_drain(
                peer, SV_NATIVE_SHADOW_FAILURE_EVENT_SENDER, true);
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }
        /* A large legacy payload may leave no DATA budget.  If an old bank
         * also needs service, use this handoff for that ACK instead; event
         * DATA remains retained for an empty async wake. */
        if (retired_work) {
            bank = SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED;
            selected = &peer->retired_transport;
            staged = *selected;
        } else if (current_due != WORR_NATIVE_CARRIER_ACK_OK) {
            saturating_increment(&peer->tx_bypass_calls);
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }
    }

    if (bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT &&
        snapshot_data_due && snapshot_sender != NULL) {
        event_token = 0;
        snapshot_result = Worr_NativeSnapshotSenderPrepareMixedV1(
            snapshot_sender, &selected->ack_ledger,
            peer->clock_ticks,
            SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS,
            SV_NATIVE_SHADOW_ACK_RETRY_MS, budget,
            legacy_application,
            (uint16_t)info->legacy_application_bytes,
            candidate_application, budget, &packet_bytes,
            &event_token);
        if (snapshot_result == WORR_NATIVE_SNAPSHOT_SENDER_OK) {
            peer->ack_emit_active = 1;
            peer->ack_emit_bank =
                SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT;
            peer->tx_emit_kind =
                SV_NATIVE_SHADOW_TX_EMIT_SNAPSHOT_MIXED;
            peer->active_completion_token =
                ++peer->next_completion_token;
            peer->ack_next_bank =
                SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED;

            memset(output, 0, sizeof(*output));
            output->abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
            output->struct_size = sizeof(*output);
            output->application_bytes = (uint32_t)packet_bytes;
            output->token = peer->active_completion_token;
            return NETCHAN_APP_TX_PREPARE_PREPARED;
        }
        if (snapshot_result !=
                WORR_NATIVE_SNAPSHOT_SENDER_OUTPUT_TOO_SMALL &&
            snapshot_result != WORR_NATIVE_SNAPSHOT_SENDER_CAPACITY &&
            snapshot_result != WORR_NATIVE_SNAPSHOT_SENDER_NOT_DUE) {
            saturating_increment(&peer->tx_bypass_calls);
            enter_drain(
                peer, SV_NATIVE_SHADOW_FAILURE_SNAPSHOT_SENDER,
                true);
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }
        if (retired_work) {
            bank = SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED;
            selected = &peer->retired_transport;
            staged = *selected;
        } else if (current_due != WORR_NATIVE_CARRIER_ACK_OK) {
            saturating_increment(&peer->tx_bypass_calls);
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }
    }

    result = Worr_NativeCarrierAckPreparePacketV1(
        &staged.ack_ledger, peer->clock_ticks,
        SV_NATIVE_SHADOW_ACK_RETRY_MS, budget,
        legacy_application, info->legacy_application_bytes,
        candidate_application, budget, &packet_bytes, &token);
    if (result != WORR_NATIVE_CARRIER_ACK_OK) {
        saturating_increment(&peer->tx_bypass_calls);
        if (result != WORR_NATIVE_CARRIER_ACK_NOT_DUE &&
            result != WORR_NATIVE_CARRIER_ACK_OUTPUT_TOO_SMALL &&
            result != WORR_NATIVE_CARRIER_ACK_LIMIT) {
            enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
        }
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (packet_bytes > UINT32_MAX) {
        saturating_increment(&peer->tx_bypass_calls);
        enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    *selected = staged;
    peer->ack_emit_token = token;
    peer->ack_emit_active = 1;
    peer->ack_emit_bank = (uint32_t)bank;
    peer->tx_emit_kind = SV_NATIVE_SHADOW_TX_EMIT_ACK;
    peer->active_completion_token = ++peer->next_completion_token;
    peer->ack_next_bank =
        bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT
            ? SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED
            : SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT;
    saturating_increment(&peer->tx_ack_prepares);

    memset(output, 0, sizeof(*output));
    output->abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    output->struct_size = sizeof(*output);
    output->application_bytes = (uint32_t)packet_bytes;
    output->token = peer->active_completion_token;
    return NETCHAN_APP_TX_PREPARE_PREPARED;
}

static void native_shadow_tx_completion(
    void *opaque,
    const netchan_app_tx_completion_info_v1_t *info,
    const byte *application)
{
    sv_native_shadow_peer_v1 *peer = opaque;
    sv_native_shadow_transport_v1 *transport;
    worr_native_event_sender_v1 *event_sender;
    worr_native_snapshot_sender_v1 *snapshot_sender;
    worr_native_event_sender_result_v1 event_result;
    worr_native_snapshot_sender_result_v1 snapshot_result;
    worr_native_carrier_ack_result_v1 result;
    const bool info_valid = info != NULL &&
        info->abi_version == NETCHAN_APP_TX_HOOK_ABI_V1 &&
        info->struct_size == sizeof(*info);
    bool accepted_completion;
    bool definite_rejection;
    bool provenance_valid;
    bool completion_fault = false;

    if (!peer_structural_valid(peer) || !peer->ack_emit_active)
        return;
    transport = peer->ack_emit_bank ==
            SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT
        ? &peer->transport : &peer->retired_transport;

    accepted_completion = info_valid &&
        info->result == NETCHAN_APP_TX_COMPLETION_ACCEPTED &&
        info->packet_copies != 0 && info->accepted_copies != 0 &&
        info->accepted_copies <= info->packet_copies;
    definite_rejection = info_valid &&
        (info->result == NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED ||
         info->result == NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID);
    provenance_valid = info_valid &&
        info->token == peer->active_completion_token;

    if (peer->tx_emit_kind ==
        SV_NATIVE_SHADOW_TX_EMIT_EVENT_MIXED) {
        event_sender = event_sender_for_bank(
            peer, SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT);
        if (event_sender == NULL) {
            completion_fault = true;
        } else if (provenance_valid && accepted_completion && application) {
            event_result = Worr_NativeEventSenderConfirmMixedV1(
                event_sender, &transport->ack_ledger,
                peer->clock_ticks, application,
                info->application_bytes);
            completion_fault =
                event_result != WORR_NATIVE_EVENT_SENDER_OK;
            if (!completion_fault) {
                peer->carrier_traffic_seen = 1;
                if (peer->async_wake_active)
                    peer->async_wake_handoff_seen = 1;
            }
        } else if (application != NULL) {
            event_result = Worr_NativeEventSenderRejectMixedV1(
                event_sender, &transport->ack_ledger, application,
                info_valid ? info->application_bytes : 0);
            saturating_increment(&peer->tx_ack_rejections);
            completion_fault =
                event_result != WORR_NATIVE_EVENT_SENDER_OK ||
                !provenance_valid || !definite_rejection;
        } else {
            saturating_increment(&peer->tx_ack_rejections);
            completion_fault = true;
        }
    } else if (peer->tx_emit_kind ==
               SV_NATIVE_SHADOW_TX_EMIT_SNAPSHOT_MIXED) {
        snapshot_sender = snapshot_sender_for_bank(
            peer, SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT);
        if (snapshot_sender == NULL) {
            completion_fault = true;
        } else if (provenance_valid && accepted_completion && application) {
            snapshot_result =
                Worr_NativeSnapshotSenderConfirmMixedV1(
                    snapshot_sender, &transport->ack_ledger,
                    peer->clock_ticks, application,
                    info->application_bytes);
            completion_fault =
                snapshot_result != WORR_NATIVE_SNAPSHOT_SENDER_OK;
            if (!completion_fault) {
                peer->carrier_traffic_seen = 1;
                if (peer->async_wake_active)
                    peer->async_wake_handoff_seen = 1;
            }
        } else if (application != NULL) {
            snapshot_result =
                Worr_NativeSnapshotSenderRejectMixedV1(
                    snapshot_sender, &transport->ack_ledger,
                    application,
                    info_valid ? info->application_bytes : 0);
            saturating_increment(&peer->tx_ack_rejections);
            completion_fault =
                snapshot_result != WORR_NATIVE_SNAPSHOT_SENDER_OK ||
                !provenance_valid || !definite_rejection;
        } else {
            saturating_increment(&peer->tx_ack_rejections);
            completion_fault = true;
        }
    } else {
        if (provenance_valid && accepted_completion && application) {
            result = Worr_NativeCarrierAckCommitHandoffV1(
                &transport->ack_ledger, &peer->ack_emit_token,
                peer->clock_ticks, application, info->application_bytes);
            if (result == WORR_NATIVE_CARRIER_ACK_OK) {
                peer->carrier_traffic_seen = 1;
                saturating_increment(&peer->tx_ack_handoffs);
                if (peer->async_wake_active)
                    peer->async_wake_handoff_seen = 1;
            } else {
                (void)Worr_NativeCarrierAckRejectHandoffV1(
                    &transport->ack_ledger, &peer->ack_emit_token);
                completion_fault = true;
            }
        } else if (provenance_valid && definite_rejection) {
            result = Worr_NativeCarrierAckRejectHandoffV1(
                &transport->ack_ledger, &peer->ack_emit_token);
            saturating_increment(&peer->tx_ack_rejections);
            if (result != WORR_NATIVE_CARRIER_ACK_OK)
                completion_fault = true;
        } else {
            (void)Worr_NativeCarrierAckRejectHandoffV1(
                &transport->ack_ledger, &peer->ack_emit_token);
            saturating_increment(&peer->tx_ack_rejections);
            completion_fault = true;
        }
    }
    peer->ack_emit_active = 0;
    peer->ack_emit_bank = SV_NATIVE_SHADOW_TRANSPORT_BANK_NONE;
    peer->tx_emit_kind = SV_NATIVE_SHADOW_TX_EMIT_NONE;
    peer->active_completion_token = 0;
    memset(&peer->ack_emit_token, 0, sizeof(peer->ack_emit_token));
    if (completion_fault)
        enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
}

static sv_native_shadow_failure_v1 server_data_sender_failure(
    const sv_native_shadow_peer_v1 *peer)
{
    return peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT
               ? SV_NATIVE_SHADOW_FAILURE_SNAPSHOT_SENDER
               : SV_NATIVE_SHADOW_FAILURE_EVENT_SENDER;
}

static bool apply_server_data_ack_entries(
    sv_native_shadow_peer_v1 *peer,
    sv_native_shadow_transport_bank_v1 bank,
    const void *packet,
    size_t packet_bytes,
    bool has_ack,
    uint64_t now_tick)
{
    worr_native_event_sender_v1 *sender;
    worr_native_snapshot_sender_v1 *snapshot_sender;
    worr_native_event_sender_result_v1 result;
    worr_native_snapshot_sender_result_v1 snapshot_result;
    uint32_t acknowledged;
    uint32_t promoted;

    if (!event_state_valid(peer) || !snapshot_state_valid(peer))
        return false;
    if (!has_ack)
        return true;
    if (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT) {
        snapshot_sender = snapshot_sender_for_bank(peer, bank);
        if (snapshot_sender == NULL)
            return true;
        acknowledged = 0;
        snapshot_result = Worr_NativeSnapshotSenderApplyAcksV1(
            snapshot_sender, packet, packet_bytes, &acknowledged);
        return snapshot_result ==
               WORR_NATIVE_SNAPSHOT_SENDER_ACK_APPLIED;
    }
    if (peer->mode != SV_NATIVE_SHADOW_MODE_EVENT)
        return true;
    sender = event_sender_for_bank(peer, bank);
    if (sender == NULL)
        return true;
    acknowledged = 0;
    result = Worr_NativeEventSenderApplyAcksV1(
        sender, packet, packet_bytes, &acknowledged);
    if (result != WORR_NATIVE_EVENT_SENDER_ACK_APPLIED)
        return false;
    if (bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED)
        return true;

    promoted = 0;
    result = Worr_NativeEventSenderPumpV1(
        sender, now_tick, &promoted);
    return result == WORR_NATIVE_EVENT_SENDER_OK ||
           result == WORR_NATIVE_EVENT_SENDER_PROMOTED ||
           result == WORR_NATIVE_EVENT_SENDER_CAPACITY;
}

static netchan_app_rx_result_t native_shadow_rx(
    void *opaque,
    const netchan_app_rx_info_v1_t *info,
    const byte *application,
    netchan_app_rx_output_v1_t *output)
{
    sv_native_shadow_peer_v1 *peer = opaque;
    worr_native_carrier_view_v1 view;
    worr_native_envelope_frame_info_v1 frame;
    worr_native_readiness_state_v1 readiness;
    worr_native_carrier_result_v1 carrier_result;
    sv_native_shadow_transport_v1 *transport;
    sv_native_shadow_transport_v1 staged;
    sv_native_shadow_transport_bank_v1 bank;
    worr_native_carrier_session_result_v1 bridge_result;
    worr_native_rx_result_v1 rx_result;
    worr_native_rx_message_v1 message;
    worr_command_record_v1 record;
    worr_native_command_shadow_compare_report_v1 report;
    worr_native_command_shadow_join_result_v1 join_result;
    uint16_t data_entry;
    bool has_data;
    bool has_ack;
    bool readiness_staged = false;
    uint32_t pruned;

    if (!peer_structural_valid(peer) || !info || !output ||
        info->abi_version != NETCHAN_APP_RX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info) ||
        info->application_bytes == 0 || !application) {
        if (peer_structural_valid(peer))
            saturating_increment(&peer->rx_bypass_calls);
        return NETCHAN_APP_RX_BYPASS;
    }
    carrier_result = Worr_NativeCarrierDecodeV1(
        application, info->application_bytes, &view);
    if (carrier_result == WORR_NATIVE_CARRIER_NO_CARRIER) {
        saturating_increment(&peer->rx_bypass_calls);
        return NETCHAN_APP_RX_BYPASS;
    }

    peer->carrier_traffic_seen = 1;
    saturating_increment(&peer->rx_carriers);
    if (carrier_result != WORR_NATIVE_CARRIER_OK ||
        !carrier_command_shape(
            application, &view, &has_data, &has_ack, &data_entry,
            &frame)) {
        carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_CARRIER);
        return NETCHAN_APP_RX_REJECT;
    }
    /* A negotiated readiness barrier deliberately terminates every older
     * private transport epoch.  Decode and shape validation still run first so
     * corrupt old carriers cannot use cancellation as a permissive parser.
     * A valid canceled carrier has no native side effects; its legacy prefix
     * remains the authoritative protocol payload. */
    if (peer->cancelled_through_transport_epoch != 0 &&
        view.transport_epoch <=
            peer->cancelled_through_transport_epoch) {
        saturating_increment(&peer->stale_cancelled_carriers);
        saturating_increment(&peer->rx_drained);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }
    transport = transport_for_epoch(
        peer, view.transport_epoch, &bank);
    if (!transport || !transport_valid(transport)) {
        /* SERVER_ACTIVE may already be on the wire when a later local
         * transport setup/validation fault is discovered.  The exact
         * committed epoch is still trustworthy connection provenance: strip
         * its structurally valid carrier and preserve the authoritative
         * legacy prefix, but never admit or acknowledge native DATA. */
        if (peer->native_wire_committed &&
            view.transport_epoch ==
                peer->wire_committed_transport_epoch) {
            if (peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DRAIN) {
                enter_drain(
                    peer, SV_NATIVE_SHADOW_FAILURE_SESSION, true);
            }
            saturating_increment(&peer->rx_drained);
            expose_legacy(output, view.legacy_bytes);
            return NETCHAN_APP_RX_EXPOSE_LEGACY;
        }
        carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_CARRIER);
        return NETCHAN_APP_RX_REJECT;
    }

    if (bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT &&
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
        readiness = peer->readiness;
        if (!Worr_NativeReadinessCanReceiveNativeV1(
                &readiness, peer->clock_ticks)) {
            peer->readiness = readiness;
            carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
            return NETCHAN_APP_RX_REJECT;
        }
        readiness_staged = true;
    }

    /* A retired epoch is release-only.  Its exact ACK ranges can retire old
     * server event DATA, but even a well-formed old COMMAND DATA entry is
     * never presented to reassembly or semantic admission. */
    if (bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED) {
        if (!apply_server_data_ack_entries(
                peer, bank, application, info->application_bytes,
                has_ack,
                peer->clock_ticks)) {
            carrier_reject(
                peer, server_data_sender_failure(peer));
            return NETCHAN_APP_RX_REJECT;
        }
        if (has_data)
            saturating_increment(&peer->rx_drained);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }

    if (!has_data) {
        if (!apply_server_data_ack_entries(
                peer, bank, application, info->application_bytes,
                has_ack,
                peer->clock_ticks)) {
            carrier_reject(
                peer, server_data_sender_failure(peer));
            return NETCHAN_APP_RX_REJECT;
        }
        if (readiness_staged)
            peer->readiness = readiness;
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }

    staged = *transport;
    memset(&message, 0, sizeof(message));
    bridge_result = Worr_NativeCarrierSessionAcceptDataRetainedV1(
        &staged.rx_session, staged.rx_slots,
        SV_NATIVE_SHADOW_RX_SLOT_CAPACITY,
        staged.payload_arena, sizeof(staged.payload_arena),
        peer->clock_ticks, application, info->application_bytes,
        data_entry,
        &staged.ack_ledger, &rx_result, &message);
    if (bridge_result != WORR_NATIVE_CARRIER_SESSION_OK) {
        carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_SESSION);
        return NETCHAN_APP_RX_REJECT;
    }
    if (rx_result == WORR_NATIVE_RX_ALREADY_COMMITTED) {
        if (!apply_server_data_ack_entries(
                peer, bank, application, info->application_bytes,
                has_ack,
                peer->clock_ticks)) {
            carrier_reject(
                peer, server_data_sender_failure(peer));
            return NETCHAN_APP_RX_REJECT;
        }
        *transport = staged;
        if (readiness_staged)
            peer->readiness = readiness;
        saturating_increment(&peer->rx_repeat_refreshes);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }
    if (rx_result != WORR_NATIVE_RX_MESSAGE_COMPLETE ||
        !decode_completed_command(&staged, &message, &record) ||
        !record_ref_equal(frame.record, message.record)) {
        carrier_reject(
            peer, rx_result == WORR_NATIVE_RX_MESSAGE_COMPLETE
                      ? SV_NATIVE_SHADOW_FAILURE_CODEC
                      : SV_NATIVE_SHADOW_FAILURE_SESSION);
        return NETCHAN_APP_RX_REJECT;
    }
    if (record.command_id.epoch !=
        transport->official_connection_epoch) {
        carrier_reject(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW);
        return NETCHAN_APP_RX_REJECT;
    }

    if (peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
        if (!apply_server_data_ack_entries(
                peer, bank, application, info->application_bytes,
                has_ack,
                peer->clock_ticks)) {
            carrier_reject(
                peer, server_data_sender_failure(peer));
            return NETCHAN_APP_RX_REJECT;
        }
        saturating_increment(&peer->rx_drained);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }
    if (peer->matched_native_highwater_valid &&
        (record.command_id.epoch !=
             peer->matched_native_highwater.epoch ||
         record.command_id.sequence <=
             peer->matched_native_highwater.sequence)) {
        /* A new native message identity may not replay an already reconciled
         * command ID.  Leave the staged RX/ACK mutation uncommitted, preserve
         * the authoritative legacy prefix, and retain hooks in DRAIN so a
         * previously committed receipt can still be re-ACKed. */
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, false);
        saturating_increment(&peer->rx_drained);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }
    if (peer->pending_native_valid) {
        /* The authoritative legacy half has not joined the previous native
         * command yet.  Never replace or queue behind that exact identity:
         * strip this distinct carrier, preserve its legacy prefix, and stop
         * admitting new native commands for the connection.  Proven repeats
         * of the committed command returned above before reaching this gate. */
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, false);
        saturating_increment(&peer->rx_drained);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }

    if (Worr_NativeCommandShadowJoinPruneV1(
            &staged.command_join, peer->clock_ticks, &pruned) !=
            WORR_NATIVE_COMMAND_SHADOW_JOIN_PRUNE_COMPLETE) {
        carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW);
        return NETCHAN_APP_RX_REJECT;
    }
    join_result = Worr_NativeCommandShadowJoinObserveV1(
        &staged.command_join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
        &record, peer->clock_ticks, &report);
    if (join_result != WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE ||
        Worr_NativeCarrierSessionCommitRetainedV1(
            &staged.rx_session, staged.rx_slots,
            SV_NATIVE_SHADOW_RX_SLOT_CAPACITY,
            message.slot_index, message.message_sequence,
            &staged.ack_ledger) != WORR_NATIVE_RX_COMMITTED) {
        carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW);
        return NETCHAN_APP_RX_REJECT;
    }

    if (!apply_server_data_ack_entries(
            peer, bank, application, info->application_bytes,
            has_ack,
            peer->clock_ticks)) {
        carrier_reject(peer, server_data_sender_failure(peer));
        return NETCHAN_APP_RX_REJECT;
    }

    *transport = staged;
    if (readiness_staged)
        peer->readiness = readiness;
    saturating_increment_u32(&peer->native_commands_accepted);
    peer->pending_native_id = record.command_id;
    peer->pending_native_valid = 1;
    saturating_increment(&peer->rx_commits);
    expose_legacy(output, view.legacy_bytes);
    return NETCHAN_APP_RX_EXPOSE_LEGACY;
}

static bool native_shadow_hooks_exact(
    const sv_native_shadow_peer_v1 *peer)
{
    const netchan_t *netchan;

    if (!peer_structural_valid(peer))
        return false;
    netchan = peer->netchan;
    return netchan->type == NETCHAN_NEW &&
           netchan->app_tx_prepare == native_shadow_tx_prepare &&
           netchan->app_tx_completion == native_shadow_tx_completion &&
           netchan->app_tx_opaque == peer &&
           netchan->app_rx == native_shadow_rx &&
           netchan->app_rx_opaque == peer;
}

bool SV_NativeShadowPeerInitModeV1(
    sv_native_shadow_peer_v1 *peer,
    netchan_t *netchan,
    uint32_t raw_time_ms,
    sv_native_shadow_mode_v1 mode)
{
    sv_native_shadow_event_state_v1 *event_state = NULL;
    sv_native_shadow_snapshot_state_v1 *snapshot_state = NULL;
    uint64_t owner;

    if (peer == NULL || netchan == NULL || netchan->type != NETCHAN_NEW ||
        (mode != SV_NATIVE_SHADOW_MODE_COMMAND &&
         mode != SV_NATIVE_SHADOW_MODE_EVENT &&
         mode != SV_NATIVE_SHADOW_MODE_SNAPSHOT) ||
        netchan->app_tx_prepare != NULL ||
        netchan->app_tx_completion != NULL ||
        netchan->app_tx_opaque != NULL || netchan->app_rx != NULL ||
        netchan->app_rx_opaque != NULL || !allocate_owner(&owner)) {
        return false;
    }
    if (mode == SV_NATIVE_SHADOW_MODE_EVENT) {
        event_state = calloc(1, sizeof(*event_state));
        if (event_state == NULL)
            return false;
    } else if (mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT) {
        snapshot_state = calloc(1, sizeof(*snapshot_state));
        if (snapshot_state == NULL)
            return false;
    }

    memset(peer, 0, sizeof(*peer));
    peer->version = SV_NATIVE_SHADOW_VERSION;
    peer->initialized = 1;
    peer->netchan = netchan;
    peer->connection_owner_id = owner;
    peer->mode = (uint32_t)mode;
    peer->event_state = event_state;
    peer->snapshot_state = snapshot_state;
    peer->enabled = 1;
    peer->clock_initialized = 1;
    peer->clock_last_raw = raw_time_ms;
    peer->clock_ticks = raw_time_ms;
    peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS;
    peer->ack_next_bank = SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT;

    if (!Netchan_SetApplicationTxHook(
            netchan, native_shadow_tx_prepare,
            native_shadow_tx_completion, peer)) {
        free(event_state);
        free(snapshot_state);
        memset(peer, 0, sizeof(*peer));
        return false;
    }
    if (!Netchan_SetApplicationRxHook(
            netchan, native_shadow_rx, peer)) {
        (void)Netchan_SetApplicationTxHook(
            netchan, NULL, NULL, NULL);
        free(event_state);
        free(snapshot_state);
        memset(peer, 0, sizeof(*peer));
        return false;
    }
    peer->hooks_attached = 1;
    return true;
}

bool SV_NativeShadowPeerInitV1(sv_native_shadow_peer_v1 *peer,
                               netchan_t *netchan,
                               uint32_t raw_time_ms)
{
    return SV_NativeShadowPeerInitModeV1(
        peer, netchan, raw_time_ms, SV_NATIVE_SHADOW_MODE_COMMAND);
}

void SV_NativeShadowPeerDetachV1(sv_native_shadow_peer_v1 *peer)
{
    netchan_t *netchan;

    /* Teardown is a lifetime operation, not a semantic state transition.
     * Clear callbacks from the minimal initialized ownership tuple even when
     * a separate invariant has failed, so Destroy can never leave netchan
     * pointing at storage it is about to clear/free. */
    if (peer == NULL || peer->version != SV_NATIVE_SHADOW_VERSION ||
        peer->initialized != 1 || peer->netchan == NULL) {
        return;
    }
    netchan = peer->netchan;
    if (netchan->type == NETCHAN_NEW) {
        if (netchan->app_tx_opaque == peer) {
            (void)Netchan_SetApplicationTxHook(
                netchan, NULL, NULL, NULL);
        }
        if (netchan->app_rx_opaque == peer)
            (void)Netchan_SetApplicationRxHook(netchan, NULL, NULL);
    }
    peer->hooks_attached = 0;
    peer->async_wake_active = 0;
    peer->async_wake_handoff_seen = 0;
    peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_DETACHED;
}

void SV_NativeShadowPeerDisableV1(sv_native_shadow_peer_v1 *peer,
                                  sv_native_shadow_failure_v1 failure)
{
    if (!peer_structural_valid(peer))
        return;
    if (peer->native_wire_committed || peer->carrier_traffic_seen ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN) {
        enter_drain(peer, failure, true);
        return;
    }
    if (peer->enabled != 0)
        saturating_increment(&peer->failures);
    peer->enabled = 0;
    peer->readiness_initialized = 0;
    peer->activation_pending = 0;
    peer->packet_open = 0;
    peer->last_failure = (uint32_t)failure;
    memset(&peer->readiness, 0, sizeof(peer->readiness));
    memset(&peer->sideband, 0, sizeof(peer->sideband));
    SV_NativeShadowPeerDetachV1(peer);
}

void SV_NativeShadowPeerDestroyV1(sv_native_shadow_peer_v1 *peer)
{
    sv_native_shadow_event_state_v1 *event_state;
    sv_native_shadow_snapshot_state_v1 *snapshot_state;

    if (peer == NULL)
        return;
    event_state = peer->version == SV_NATIVE_SHADOW_VERSION &&
                          peer->initialized == 1 &&
                          peer->mode == SV_NATIVE_SHADOW_MODE_EVENT
                      ? peer->event_state
                      : NULL;
    snapshot_state = peer->version == SV_NATIVE_SHADOW_VERSION &&
                             peer->initialized == 1 &&
                             peer->mode ==
                                 SV_NATIVE_SHADOW_MODE_SNAPSHOT
                         ? peer->snapshot_state
                         : NULL;
    SV_NativeShadowPeerDetachV1(peer);
    free(event_state);
    free(snapshot_state);
    memset(peer, 0, sizeof(*peer));
}

bool SV_NativeShadowPeerEnabledV1(
    const sv_native_shadow_peer_v1 *peer)
{
    return peer_structural_valid(peer) && peer->enabled == 1 &&
           native_shadow_hooks_exact(peer) &&
           peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DETACHED;
}

bool SV_NativeShadowPostBootstrapQueueIdleV1(
    const sv_native_shadow_peer_v1 *peer)
{
    const netchan_t *netchan;

    if (!SV_NativeShadowPeerEnabledV1(peer))
        return false;
    netchan = peer->netchan;
    return netchan->message.cursize == 0 &&
           !netchan->message.overflowed &&
           netchan->reliable_length == 0 &&
           !netchan->fragment_pending &&
           netchan->fragment_out.cursize == 0;
}

bool SV_NativeShadowChallengeQueueExpiredV1(
    uint32_t requested_at_ms,
    uint32_t now_ms)
{
    return (uint32_t)(now_ms - requested_at_ms) >=
           SV_NATIVE_SHADOW_CHALLENGE_QUEUE_TIMEOUT_MS;
}

bool SV_NativeShadowSettingIndexV1(int16_t index)
{
    return index >= WORR_NATIVE_READINESS_SETTING_BEGIN &&
           index <= WORR_NATIVE_READINESS_SETTING_COMMIT;
}

bool SV_NativeShadowAdvanceAdmissionClockV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms)
{
    uint64_t now_tick;

    if (!native_shadow_hooks_exact(peer))
        return false;
    if (extend_clock(peer, raw_time_ms, &now_tick))
        return true;
    SV_NativeShadowPeerDisableV1(
        peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
    return false;
}

static bool append_buffer_valid(const sizebuf_t *buffer)
{
    return buffer != NULL && buffer->data != NULL &&
           !buffer->overflowed && buffer->cursize <= buffer->maxsize;
}

static bool backing_ranges_overlap(const sizebuf_t *left,
                                   const sizebuf_t *right)
{
    const uintptr_t left_begin = (uintptr_t)left->data;
    const uintptr_t right_begin = (uintptr_t)right->data;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left->maxsize == 0 || right->maxsize == 0)
        return false;
    if (left->maxsize > UINTPTR_MAX - left_begin ||
        right->maxsize > UINTPTR_MAX - right_begin) {
        return true;
    }
    left_end = left_begin + left->maxsize;
    right_end = right_begin + right->maxsize;
    return left_begin < right_end && right_begin < left_end;
}

bool SV_NativeShadowCanAppendSvcReadinessV1(
    const sizebuf_t *message,
    const sizebuf_t *reliable_queue)
{
    uint32_t eventual_message_bytes;

    if (!append_buffer_valid(message) ||
        !append_buffer_valid(reliable_queue) ||
        message == reliable_queue ||
        backing_ranges_overlap(message, reliable_queue) ||
        message->maxsize - message->cursize <
            SV_NATIVE_SHADOW_SVC_WIRE_BYTES) {
        return false;
    }
    eventual_message_bytes = message->cursize +
                             SV_NATIVE_SHADOW_SVC_WIRE_BYTES;
    return reliable_queue->maxsize - reliable_queue->cursize >=
           eventual_message_bytes;
}

static void write_i32_le(byte *destination, int32_t value)
{
    const uint32_t bits = (uint32_t)value;

    destination[0] = (byte)(bits & UINT32_C(0xff));
    destination[1] = (byte)((bits >> 8) & UINT32_C(0xff));
    destination[2] = (byte)((bits >> 16) & UINT32_C(0xff));
    destination[3] = (byte)((bits >> 24) & UINT32_C(0xff));
}

bool SV_NativeShadowAppendSvcReadinessV1(
    sizebuf_t *message,
    const sizebuf_t *reliable_queue,
    int setting_opcode,
    const worr_native_readiness_record_v1 *record)
{
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    byte staging[SV_NATIVE_SHADOW_SVC_WIRE_BYTES];
    uint32_t pair_index;
    uint32_t staging_offset = 0;

    if (record == NULL || setting_opcode < 0 || setting_opcode > UINT8_MAX ||
        !SV_NativeShadowCanAppendSvcReadinessV1(
            message, reliable_queue) ||
        !Worr_NativeReadinessSidebandEncodeV1(
            record, pairs,
            WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT)) {
        return false;
    }

    for (pair_index = 0;
         pair_index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++pair_index) {
        staging[staging_offset] = (byte)setting_opcode;
        write_i32_le(staging + staging_offset + 1,
                     (int32_t)pairs[pair_index].index);
        write_i32_le(staging + staging_offset + 5,
                     (int32_t)pairs[pair_index].value);
        staging_offset += 9;
    }
    if (staging_offset != SV_NATIVE_SHADOW_SVC_WIRE_BYTES)
        return false;

    memcpy(message->data + message->cursize, staging, sizeof(staging));
    message->cursize += SV_NATIVE_SHADOW_SVC_WIRE_BYTES;
    return true;
}

bool SV_NativeShadowBeginEpochBoundV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t official_connection_epoch,
    uint32_t official_supported,
    uint32_t official_negotiated,
    uint32_t snapshot_epoch,
    uint32_t raw_time_ms,
    worr_native_readiness_record_v1 *challenge_out)
{
    uint32_t transport_epoch;
    uint32_t event_stream_epoch = 0;
    uint64_t readiness_nonce;
    uint64_t now_tick;
    uint32_t private_capabilities;
    worr_native_readiness_result_v1 result;
    worr_native_readiness_state_v1 readiness;
    worr_native_readiness_sideband_parser_v1 sideband;
    worr_native_readiness_record_v1 challenge;
    bool reopen_packet;

    if (!SV_NativeShadowPeerEnabledV1(peer) || challenge_out == NULL)
        return false;
    if (official_connection_epoch == 0 ||
        official_supported != WORR_NET_CAP_LEGACY_STAGE_MASK ||
        official_negotiated != WORR_NET_CAP_LEGACY_STAGE_MASK ||
        ((peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT) !=
         (snapshot_epoch != 0))) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_OFFICIAL_BINDING);
        return false;
    }
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    if (!allocate_epoch_and_nonce(&transport_epoch, &readiness_nonce)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_IDENTITY_EXHAUSTED);
        return false;
    }
    if (peer->mode == SV_NATIVE_SHADOW_MODE_EVENT &&
        !allocate_event_stream_epoch(&event_stream_epoch)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_IDENTITY_EXHAUSTED);
        return false;
    }

    private_capabilities =
        peer->mode == SV_NATIVE_SHADOW_MODE_EVENT
            ? WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK
            : (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT
                   ? WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK
                   : WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK);
    memset(&readiness, 0, sizeof(readiness));
    if (peer->readiness_initialized == 0) {
        result = Worr_NativeReadinessServerInitBoundV1(
            &readiness, transport_epoch, snapshot_epoch,
            private_capabilities,
            readiness_nonce, now_tick, SV_NATIVE_SHADOW_TIMEOUT_MS,
            &challenge);
    } else {
        readiness = peer->readiness;
        result = Worr_NativeReadinessServerAdvanceEpochBoundV1(
            &readiness, transport_epoch, snapshot_epoch,
            private_capabilities,
            readiness_nonce, now_tick, SV_NATIVE_SHADOW_TIMEOUT_MS,
            &challenge);
    }
    if (result != WORR_NATIVE_READINESS_OK) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
        return false;
    }

    reopen_packet = peer->packet_open != 0;
    if (!Worr_NativeReadinessSidebandParserInitV1(&sideband) ||
        (reopen_packet &&
         Worr_NativeReadinessSidebandPacketBeginV1(&sideband) !=
             WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
        return false;
    }

    if (!cancel_prior_native_epochs(peer, transport_epoch)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SESSION);
        return false;
    }
    if (peer->mode == SV_NATIVE_SHADOW_MODE_EVENT)
        peer->event_state->stream_epoch = event_stream_epoch;
    if (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT)
        peer->snapshot_state->snapshot_epoch = snapshot_epoch;
    memset(&peer->pending_native_id, 0,
           sizeof(peer->pending_native_id));
    peer->pending_native_valid = 0;
    memset(&peer->matched_native_highwater, 0,
           sizeof(peer->matched_native_highwater));
    peer->matched_native_highwater_valid = 0;
    peer->readiness = readiness;
    peer->sideband = sideband;
    peer->readiness_initialized = 1;
    peer->packet_open = reopen_packet ? 1u : 0u;
    peer->activation_pending = 0;
    peer->native_commands_accepted = 0;
    peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS;
    peer->official_connection_epoch = official_connection_epoch;
    peer->private_transport_epoch = transport_epoch;
    peer->readiness_nonce = readiness_nonce;
    saturating_increment(&peer->challenges_queued);
    *challenge_out = challenge;
    return true;
}

bool SV_NativeShadowBeginEpochV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t official_connection_epoch,
    uint32_t official_supported,
    uint32_t official_negotiated,
    uint32_t raw_time_ms,
    worr_native_readiness_record_v1 *challenge_out)
{
    return SV_NativeShadowBeginEpochBoundV1(
        peer, official_connection_epoch, official_supported,
        official_negotiated, 0, raw_time_ms, challenge_out);
}

static bool transport_init(
    sv_native_shadow_transport_v1 *transport_out,
    const worr_native_session_binding_v1 *binding,
    uint32_t official_connection_epoch)
{
    sv_native_shadow_transport_v1 transport;

    if (!transport_out || !binding || official_connection_epoch == 0)
        return false;
    memset(&transport, 0, sizeof(transport));
    transport.binding = *binding;
    transport.official_connection_epoch = official_connection_epoch;
    if (!Worr_NativeRxSessionInitV1(
            &transport.rx_session, transport.rx_slots,
            SV_NATIVE_SHADOW_RX_SLOT_CAPACITY,
            SV_NATIVE_SHADOW_PAYLOAD_BYTES,
            SV_NATIVE_SHADOW_RX_FRAGMENT_TIMEOUT_MS,
            SV_NATIVE_SHADOW_RX_COMPLETE_TIMEOUT_MS, binding) ||
        !Worr_NativeCarrierAckLedgerInitV1(
            &transport.ack_ledger, binding,
            SV_NATIVE_SHADOW_ACK_PROACTIVE_HANDOFFS) ||
        !Worr_NativeCommandShadowJoinInitV1(
            &transport.command_join,
            WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            SV_NATIVE_SHADOW_JOIN_EXPIRY_MS) ||
        !transport_valid(&transport)) {
        return false;
    }
    *transport_out = transport;
    return true;
}

bool SV_NativeShadowServerActiveQueuedV1(
    sv_native_shadow_peer_v1 *peer)
{
    worr_native_session_binding_v1 binding;
    worr_event_stream_descriptor_v1 descriptor;
    sv_native_shadow_transport_v1 transport;
    bool readiness_phase_valid;
    const bool server_data_mode =
        peer != NULL &&
        (peer->mode == SV_NATIVE_SHADOW_MODE_EVENT ||
         peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT);

    /* This API's contract says SERVER_ACTIVE is already resident in the
     * reliable queue.  From this point the client may legitimately observe
     * ACTIVE even if any local validation or transport initialization below
     * fails, so hook retention becomes irreversible before the first
     * fallible operation. */
    if (!native_shadow_hooks_exact(peer))
        return false;
    peer->native_wire_committed = 1;
    peer->wire_committed_transport_epoch =
        peer->private_transport_epoch;
    readiness_phase_valid = server_data_mode
        ? peer->readiness.phase ==
              WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM
        : peer->readiness.phase ==
              WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE;
    if (!SV_NativeShadowPeerEnabledV1(peer) ||
        peer->readiness_initialized == 0 || !readiness_phase_valid) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
        return false;
    }
    if (peer->transport_initialized &&
        peer->transport.binding.transport_epoch ==
            peer->readiness.transport_epoch) {
        if ((peer->mode == SV_NATIVE_SHADOW_MODE_EVENT &&
             (!event_state_valid(peer) ||
              !peer->event_state->current_sender_initialized)) ||
            (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT &&
             (!snapshot_state_valid(peer) ||
              !peer->snapshot_state->current_sender_initialized))) {
            SV_NativeShadowPeerDisableV1(
                peer, server_data_sender_failure(peer));
            return false;
        }
        peer->activation_pending = 0;
        return true;
    }
    if (!peer->activation_pending ||
        !(server_data_mode
              ? Worr_NativeSessionBindingInitReceiveFromReadinessV1(
                    &binding, &peer->readiness,
                    peer->connection_owner_id, peer->clock_ticks)
              : Worr_NativeSessionBindingInitFromReadinessV1(
                    &binding, &peer->readiness,
                    peer->connection_owner_id))) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SESSION);
        return false;
    }
    if (peer->transport_initialized ||
        peer->retired_transport_initialized ||
        !transport_init(
            &transport, &binding,
            peer->official_connection_epoch)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SESSION);
        return false;
    }

    if (peer->mode == SV_NATIVE_SHADOW_MODE_EVENT) {
        if (!event_state_valid(peer) ||
            peer->event_state->stream_epoch == 0 ||
            peer->event_state->current_sender_initialized ||
            !Worr_EventStreamDescriptorInitV1(
                &descriptor, peer->event_state->stream_epoch,
                SV_NATIVE_SHADOW_EVENT_FIRST_SEQUENCE) ||
            Worr_NativeEventSenderInitV1(
                &peer->event_state->current_sender, &binding,
                &descriptor, MAX_EDICTS,
                SV_NATIVE_SHADOW_EVENT_MAX_DATAGRAM_BYTES,
                peer->clock_ticks) != WORR_NATIVE_EVENT_SENDER_OK) {
            SV_NativeShadowPeerDisableV1(
                peer, SV_NATIVE_SHADOW_FAILURE_EVENT_SENDER);
            return false;
        }
        peer->event_state->current_sender_initialized = 1;
    }
    if (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT) {
        if (!snapshot_state_valid(peer) ||
            peer->snapshot_state->snapshot_epoch == 0 ||
            peer->snapshot_state->snapshot_epoch !=
                peer->readiness.snapshot_epoch ||
            peer->snapshot_state->current_sender_initialized ||
            Worr_NativeSnapshotSenderInitV1(
                &peer->snapshot_state->current_sender, &binding,
                MAX_EDICTS,
                SV_NATIVE_SHADOW_SNAPSHOT_MAX_DATAGRAM_BYTES,
                peer->clock_ticks) !=
                WORR_NATIVE_SNAPSHOT_SENDER_OK) {
            SV_NativeShadowPeerDisableV1(
                peer, SV_NATIVE_SHADOW_FAILURE_SNAPSHOT_SENDER);
            return false;
        }
        peer->snapshot_state->current_sender_initialized = 1;
    }

    peer->transport = transport;
    peer->transport_initialized = 1;
    peer->activation_pending = 0;
    peer->native_commands_accepted = 0;
    memset(&peer->pending_native_id, 0,
           sizeof(peer->pending_native_id));
    peer->pending_native_valid = 0;
    memset(&peer->matched_native_highwater, 0,
           sizeof(peer->matched_native_highwater));
    peer->matched_native_highwater_valid = 0;
    peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE;
    return true;
}

bool SV_NativeShadowPacketBeginV1(sv_native_shadow_peer_v1 *peer,
                                  uint32_t raw_time_ms)
{
    uint64_t now_tick;
    worr_native_readiness_result_v1 readiness_result;

    if (!SV_NativeShadowPeerEnabledV1(peer) ||
        peer->readiness_initialized == 0)
        return false;
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    readiness_result = Worr_NativeReadinessCheckDeadlineV1(
        &peer->readiness, now_tick);
    if (readiness_result != WORR_NATIVE_READINESS_OK) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
        return false;
    }
    if (Worr_NativeReadinessSidebandPacketBeginV1(&peer->sideband) !=
        WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
        return false;
    }
    peer->packet_open = 1;
    return true;
}

sv_native_shadow_observe_result_v1 SV_NativeShadowObserveSettingV1(
    sv_native_shadow_peer_v1 *peer,
    int16_t index,
    int16_t value,
    worr_native_readiness_record_v1 *server_active_out)
{
    worr_native_readiness_sideband_result_v1 sideband_result;
    worr_native_readiness_result_v1 readiness_result;
    worr_native_readiness_record_v1 peer_record;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_state_v1 readiness;

    if (!SV_NativeShadowPeerEnabledV1(peer) ||
        peer->readiness_initialized == 0 || peer->packet_open == 0 ||
        server_active_out == NULL) {
        return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
    }

    sideband_result = Worr_NativeReadinessSidebandObservePairV1(
        &peer->sideband, index, value);
    if (sideband_result == WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND)
        return SV_NATIVE_SHADOW_OBSERVE_NOT_SIDEBAND;
    if (sideband_result == WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED)
        return SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED;
    if (sideband_result !=
        WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
        return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
    }
    if (Worr_NativeReadinessSidebandTakeRecordV1(
            &peer->sideband, &peer_record) !=
        WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
        return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
    }

    if (!Worr_NativeReadinessRecordValidateV1(&peer_record) ||
        (peer_record.record_kind !=
             WORR_NATIVE_READINESS_RECORD_CLIENT_READY &&
         peer_record.record_kind !=
             WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM) ||
        peer_record.negotiated_capabilities !=
            peer->readiness.negotiated_capabilities) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
        return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
    }

    /* Cancellation includes reliable lifecycle declarations, not only WTC
     * carrier payload.  A late valid CLIENT_READY/ACTIVE_CONFIRM below the
     * monotonic floor is consumed without touching the new readiness machine;
     * malformed, wrong-direction, or downgrade records above remain
     * fail-closed. */
    if (peer->cancelled_through_transport_epoch != 0 &&
        peer_record.transport_epoch <=
            peer->cancelled_through_transport_epoch) {
        saturating_increment(
            &peer->stale_cancelled_readiness_records);
        return SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED;
    }

    readiness = peer->readiness;
    if (peer_record.record_kind ==
        WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM) {
        if ((peer->mode != SV_NATIVE_SHADOW_MODE_EVENT &&
             peer->mode != SV_NATIVE_SHADOW_MODE_SNAPSHOT) ||
            !peer->transport_initialized ||
            (peer->mode == SV_NATIVE_SHADOW_MODE_EVENT &&
             (!event_state_valid(peer) ||
              !peer->event_state->current_sender_initialized)) ||
            (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT &&
             (!snapshot_state_valid(peer) ||
              !peer->snapshot_state->current_sender_initialized))) {
            SV_NativeShadowPeerDisableV1(
                peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
            return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
        }
        readiness_result =
            Worr_NativeReadinessServerObserveClientActiveConfirmV1(
                &readiness, &peer_record, peer->clock_ticks);
        if (readiness_result != WORR_NATIVE_READINESS_OK &&
            readiness_result != WORR_NATIVE_READINESS_EXACT_DUPLICATE) {
            peer->readiness = readiness;
            SV_NativeShadowPeerDisableV1(
                peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
            return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
        }
        peer->readiness = readiness;
        saturating_increment(
            &peer->client_active_confirm_records);
        return SV_NATIVE_SHADOW_OBSERVE_CLIENT_ACTIVE_CONFIRMED;
    }
    if (peer_record.record_kind !=
        WORR_NATIVE_READINESS_RECORD_CLIENT_READY) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
        return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
    }
    readiness_result = Worr_NativeReadinessServerObserveClientReadyV1(
        &readiness, &peer_record, peer->clock_ticks,
        &server_active);
    if (readiness_result != WORR_NATIVE_READINESS_OK &&
        readiness_result != WORR_NATIVE_READINESS_EXACT_DUPLICATE) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
        return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
    }
    peer->readiness = readiness;
    peer->activation_pending =
        !peer->transport_initialized ||
        peer->transport.binding.transport_epoch !=
            readiness.transport_epoch;
    saturating_increment(&peer->client_ready_records);
    if (readiness_result == WORR_NATIVE_READINESS_EXACT_DUPLICATE)
        saturating_increment(&peer->duplicate_client_ready_records);
    saturating_increment(&peer->server_active_records);
    *server_active_out = server_active;
    return SV_NATIVE_SHADOW_OBSERVE_SERVER_ACTIVE_READY;
}

sv_native_shadow_observe_result_v1
SV_NativeShadowObserveSettingWithResponseCapacityV1(
    sv_native_shadow_peer_v1 *peer,
    int16_t index,
    int16_t value,
    const sizebuf_t *message,
    const sizebuf_t *reliable_queue,
    worr_native_readiness_record_v1 *server_active_out)
{
    const sv_native_shadow_observe_result_v1 result =
        SV_NativeShadowObserveSettingV1(
            peer, index, value, server_active_out);

    /* A valid canceled readiness record is response-free.  Test response
     * capacity only after it has been decoded and floor-classified, so a full
     * reliable queue cannot turn delayed old control traffic into a private
     * readiness failure. */
    if (result != SV_NATIVE_SHADOW_OBSERVE_SERVER_ACTIVE_READY)
        return result;
    if (SV_NativeShadowCanAppendSvcReadinessV1(
            message, reliable_queue)) {
        return result;
    }

    SV_NativeShadowPeerDisableV1(peer, SV_NATIVE_SHADOW_FAILURE_QUEUE);
    return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
}

static bool prune_pending_native(
    sv_native_shadow_peer_v1 *peer, uint64_t now_tick)
{
    worr_native_command_shadow_join_v1 join;
    worr_native_command_shadow_join_slot_v1 slot;
    worr_native_command_shadow_join_result_v1 result;
    uint32_t pruned;

    if (!peer_structural_valid(peer) || !peer->transport_initialized)
        return false;
    join = peer->transport.command_join;
    if (Worr_NativeCommandShadowJoinPruneV1(
            &join, now_tick, &pruned) !=
            WORR_NATIVE_COMMAND_SHADOW_JOIN_PRUNE_COMPLETE) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, true);
        return false;
    }
    peer->transport.command_join = join;
    if (!peer->pending_native_valid)
        return true;

    result = Worr_NativeCommandShadowJoinFindV1(
        &join, peer->pending_native_id, &slot);
    if (result == WORR_NATIVE_COMMAND_SHADOW_JOIN_NOT_FOUND) {
        memset(&peer->pending_native_id, 0,
               sizeof(peer->pending_native_id));
        peer->pending_native_valid = 0;
        saturating_increment(&peer->join_expiries);
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, false);
        return true;
    }
    if (result != WORR_NATIVE_COMMAND_SHADOW_JOIN_FOUND ||
        (slot.state_flags &
         WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE) == 0) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, true);
        return false;
    }
    return true;
}

bool SV_NativeShadowObserveLegacyCommandV1(
    sv_native_shadow_peer_v1 *peer,
    const worr_command_record_v1 *record,
    uint32_t raw_time_ms)
{
    worr_native_command_shadow_join_v1 join;
    worr_native_command_shadow_compare_report_v1 report;
    worr_native_command_shadow_join_result_v1 result;
    uint64_t now_tick;

    if (!peer_structural_valid(peer) || !record)
        return false;
    if (!peer->transport_initialized ||
        (peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
         peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DRAIN)) {
        return true;
    }
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    if (!prune_pending_native(peer, now_tick))
        return false;
    if (!peer->pending_native_valid ||
        record->command_id.epoch != peer->pending_native_id.epoch ||
        record->command_id.sequence !=
            peer->pending_native_id.sequence) {
        return true;
    }

    join = peer->transport.command_join;
    result = Worr_NativeCommandShadowJoinObserveV1(
        &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
        record, now_tick, &report);
    if (result !=
            WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED &&
        result != WORR_NATIVE_COMMAND_SHADOW_JOIN_COMMAND_MISMATCH &&
        result !=
            WORR_NATIVE_COMMAND_SHADOW_JOIN_SAMPLE_OFFSET_MISMATCH &&
        result != WORR_NATIVE_COMMAND_SHADOW_JOIN_DUPLICATE_LEGACY) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, true);
        return false;
    }
    saturating_increment(&peer->legacy_join_observations);
    if (result ==
        WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED) {
        if (Worr_NativeCommandShadowJoinRetireComparedV1(
                &join, peer->pending_native_id) !=
            WORR_NATIVE_COMMAND_SHADOW_JOIN_RETIRED) {
            enter_drain(
                peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, true);
            return false;
        }
        peer->transport.command_join = join;
        peer->matched_native_highwater = peer->pending_native_id;
        peer->matched_native_highwater_valid = 1;
        memset(&peer->pending_native_id, 0,
               sizeof(peer->pending_native_id));
        peer->pending_native_valid = 0;
        saturating_increment(&peer->command_matches);
    } else if (result ==
               WORR_NATIVE_COMMAND_SHADOW_JOIN_COMMAND_MISMATCH) {
        peer->transport.command_join = join;
        saturating_increment(&peer->command_mismatches);
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, false);
    } else if (result ==
               WORR_NATIVE_COMMAND_SHADOW_JOIN_SAMPLE_OFFSET_MISMATCH) {
        peer->transport.command_join = join;
        saturating_increment(&peer->sample_offset_mismatches);
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, false);
    } else {
        /* A repeated authoritative half after a mismatch is diagnostic only.
         * Keep the compared slot and pending identity intact in DRAIN. */
        peer->transport.command_join = join;
    }
    return true;
}

bool SV_NativeShadowReconcileCommandStreamV1(
    sv_native_shadow_peer_v1 *peer,
    const worr_command_stream_v1 *stream,
    uint32_t raw_time_ms)
{
    worr_command_record_v1 record;
    uint64_t now_tick;

    if (!peer_structural_valid(peer))
        return false;
    if (!peer->transport_initialized || !peer->pending_native_valid ||
        (peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
         peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DRAIN)) {
        return true;
    }
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    if (stream != NULL && !Worr_CommandStreamValidateV1(stream)) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, true);
        return false;
    }
    if (stream != NULL && Worr_CommandStreamCopyRecordV1(
            stream, peer->pending_native_id, &record)) {
        return SV_NativeShadowObserveLegacyCommandV1(
            peer, &record, raw_time_ms);
    }
    return prune_pending_native(peer, now_tick);
}

static bool queue_event_candidates_at_tick(
    sv_native_shadow_peer_v1 *peer,
    const worr_event_record_v1 *candidates,
    uint32_t candidate_count,
    uint64_t now_tick)
{
    worr_native_event_sender_v1 *sender;
    worr_native_event_sender_result_v1 queue_result;
    uint32_t promoted;

    if (!event_state_valid(peer) || candidates == NULL ||
        candidate_count == 0 ||
        peer->mode != SV_NATIVE_SHADOW_MODE_EVENT ||
        peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE ||
        !peer->event_state->current_sender_initialized)
        return false;
    sender = &peer->event_state->current_sender;
    if (candidate_count > WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY ||
        candidate_count >
            WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY -
                sender->backlog_count) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_EVENT_SENDER, true);
        return false;
    }
    queue_result = Worr_NativeEventSenderQueueCandidatesV1(
        sender, candidates, candidate_count);
    if (queue_result != WORR_NATIVE_EVENT_SENDER_QUEUED) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_EVENT_SENDER, true);
        return false;
    }
    promoted = 0;
    return pump_event_sender_at_tick(peer, now_tick, &promoted);
}

bool SV_NativeShadowQueueEventCandidatesV1(
    sv_native_shadow_peer_v1 *peer,
    const worr_event_record_v1 *candidates,
    uint32_t candidate_count,
    uint32_t raw_time_ms)
{
    uint64_t now_tick;

    if (!native_shadow_hooks_exact(peer) || candidates == NULL ||
        candidate_count == 0)
        return false;
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    return queue_event_candidates_at_tick(
        peer, candidates, candidate_count, now_tick);
}

bool SV_NativeShadowQueueSnapshotEventsV1(
    sv_native_shadow_peer_v1 *peer,
    const sv_snapshot_shadow_peer_v1 *snapshot_shadow,
    sv_snapshot_shadow_ref_v1 snapshot_ref,
    uint32_t raw_time_ms)
{
    sv_native_shadow_event_state_v1 *state;
    worr_native_event_sender_v1 *sender;
    sv_snapshot_event_candidates_result_v1 copy_result;
    uint32_t candidate_count = 0;
    uint32_t copied_count = 0;
    uint64_t now_tick;

    if (!native_shadow_hooks_exact(peer) || snapshot_shadow == NULL ||
        peer->mode != SV_NATIVE_SHADOW_MODE_EVENT ||
        peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE ||
        !event_state_valid(peer) ||
        !peer->event_state->current_sender_initialized) {
        return false;
    }
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    state = peer->event_state;
    sender = &state->current_sender;
    copy_result = SV_SnapshotShadowCopyEventCandidatesV1(
        snapshot_shadow, snapshot_ref, NULL, 0, &candidate_count);
    if (copy_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK &&
        copy_result != SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY) {
        saturating_increment(&state->snapshot_queue_failures);
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_SNAPSHOT_EVENTS, true);
        return false;
    }
    if (candidate_count == 0) {
        saturating_increment(&state->snapshots_queued);
        return true;
    }
    if (candidate_count > WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY ||
        candidate_count >
            WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY -
                sender->backlog_count) {
        saturating_increment(&state->snapshot_queue_failures);
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_SNAPSHOT_EVENTS, true);
        return false;
    }
    copy_result = SV_SnapshotShadowCopyEventCandidatesV1(
        snapshot_shadow, snapshot_ref, state->candidate_scratch,
        WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY, &copied_count);
    if (copy_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK ||
        copied_count != candidate_count) {
        saturating_increment(&state->snapshot_queue_failures);
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_SNAPSHOT_EVENTS, true);
        return false;
    }
    if (!queue_event_candidates_at_tick(
            peer, state->candidate_scratch, candidate_count,
            now_tick)) {
        saturating_increment(&state->snapshot_queue_failures);
        return false;
    }
    saturating_increment(&state->snapshots_queued);
    return true;
}

bool SV_NativeShadowQueueSnapshotV1(
    sv_native_shadow_peer_v1 *peer,
    sv_snapshot_shadow_peer_v1 *snapshot_shadow,
    sv_snapshot_shadow_ref_v1 snapshot_ref,
    uint32_t raw_time_ms)
{
    sv_native_shadow_snapshot_state_v1 *state;
    worr_snapshot_projection_view_v2 view;
    worr_snapshot_projection_hashes_v2 hashes;
    worr_native_snapshot_sender_result_v1 queue_result;
    uint64_t now_tick;

    if (!native_shadow_hooks_exact(peer) || snapshot_shadow == NULL ||
        peer->mode != SV_NATIVE_SHADOW_MODE_SNAPSHOT ||
        peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE ||
        !snapshot_state_valid(peer) ||
        !peer->snapshot_state->current_sender_initialized) {
        return false;
    }
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    state = peer->snapshot_state;
    memset(&view, 0, sizeof(view));
    memset(&hashes, 0, sizeof(hashes));
    if (SV_SnapshotShadowViewV1(
            snapshot_shadow, snapshot_ref, &view, &hashes) !=
            SV_SNAPSHOT_SHADOW_OK ||
        view.snapshot == NULL ||
        view.snapshot->snapshot_id.epoch != state->snapshot_epoch ||
        hashes.struct_size != sizeof(hashes) ||
        hashes.schema_version != WORR_SNAPSHOT_PROJECTION_VERSION) {
        saturating_increment(&state->snapshot_queue_failures);
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_SNAPSHOT_PROJECTION,
            true);
        return false;
    }

    queue_result = Worr_NativeSnapshotSenderQueueV1(
        &state->current_sender, &view, now_tick);
    switch (queue_result) {
    case WORR_NATIVE_SNAPSHOT_SENDER_RETAINED:
    case WORR_NATIVE_SNAPSHOT_SENDER_SUPERSEDED:
    case WORR_NATIVE_SNAPSHOT_SENDER_PENDING:
    case WORR_NATIVE_SNAPSHOT_SENDER_COALESCED:
    case WORR_NATIVE_SNAPSHOT_SENDER_DUPLICATE:
    case WORR_NATIVE_SNAPSHOT_SENDER_STALE_SNAPSHOT:
        saturating_increment(&state->snapshots_queued);
        return true;
    default:
        break;
    }
    saturating_increment(&state->snapshot_queue_failures);
    enter_drain(
        peer, SV_NATIVE_SHADOW_FAILURE_SNAPSHOT_SENDER, true);
    return false;
}

static bool server_data_due_at_tick(
    const sv_native_shadow_peer_v1 *peer,
    uint64_t now_tick,
    bool *due_out)
{
    worr_native_event_sender_result_v1 result;
    worr_native_snapshot_sender_result_v1 snapshot_result;

    if (!event_state_valid(peer) || !snapshot_state_valid(peer) ||
        due_out == NULL)
        return false;
    *due_out = false;
    if (peer->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT) {
        if (peer->snapshot_state->current_sender_initialized == 0 ||
            peer->lifecycle !=
                SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
            return true;
        }
        snapshot_result = Worr_NativeSnapshotSenderDataDuePeekV1(
            &peer->snapshot_state->current_sender, now_tick,
            SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS, due_out);
        return snapshot_result == WORR_NATIVE_SNAPSHOT_SENDER_OK ||
               snapshot_result ==
                   WORR_NATIVE_SNAPSHOT_SENDER_NOT_DUE;
    }
    if (peer->mode != SV_NATIVE_SHADOW_MODE_EVENT ||
        peer->event_state->current_sender_initialized == 0 ||
        peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
        return true;
    }
    result = Worr_NativeEventSenderDataDuePeekV1(
        &peer->event_state->current_sender, now_tick,
        SV_NATIVE_SHADOW_EVENT_RESEND_MS, due_out);
    return result == WORR_NATIVE_EVENT_SENDER_OK ||
           result == WORR_NATIVE_EVENT_SENDER_NOT_DUE;
}

static bool pump_event_sender_at_tick(
    sv_native_shadow_peer_v1 *peer,
    uint64_t now_tick,
    uint32_t *promoted_out)
{
    worr_native_event_sender_result_v1 result;
    uint32_t promoted;

    if (!event_state_valid(peer) ||
        peer->mode != SV_NATIVE_SHADOW_MODE_EVENT ||
        !peer->event_state->current_sender_initialized)
        return false;
    promoted = 0;
    result = Worr_NativeEventSenderPumpV1(
        &peer->event_state->current_sender, now_tick, &promoted);
    if (result != WORR_NATIVE_EVENT_SENDER_OK &&
        result != WORR_NATIVE_EVENT_SENDER_PROMOTED &&
        result != WORR_NATIVE_EVENT_SENDER_CAPACITY) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_EVENT_SENDER, true);
        return false;
    }
    if (promoted_out != NULL)
        *promoted_out = promoted;
    return true;
}

bool SV_NativeShadowEventPumpV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms,
    uint32_t *promoted_out)
{
    uint64_t now_tick;

    if (!native_shadow_hooks_exact(peer) || promoted_out == NULL ||
        peer->mode != SV_NATIVE_SHADOW_MODE_EVENT ||
        !peer->event_state->current_sender_initialized)
        return false;
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    return pump_event_sender_at_tick(peer, now_tick, promoted_out);
}

bool SV_NativeShadowOutputDueV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms)
{
    worr_native_readiness_state_v1 readiness;
    bool ack_due;
    bool event_due;
    uint64_t now_tick;

    if (!native_shadow_hooks_exact(peer) ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DETACHED) {
        return false;
    }
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    if (peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
        readiness = peer->readiness;
        if (!Worr_NativeReadinessCanTransmitNativeV1(
                &readiness, now_tick)) {
            peer->readiness = readiness;
            if (readiness.phase == WORR_NATIVE_READINESS_PHASE_FAILED)
                SV_NativeShadowPeerDisableV1(
                    peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
            return false;
        }
        peer->readiness = readiness;
        if (peer->mode == SV_NATIVE_SHADOW_MODE_EVENT &&
            !pump_event_sender_at_tick(peer, now_tick, NULL))
            return false;
    }
    if (!ack_due_at_tick(peer, now_tick, &ack_due) ||
        !server_data_due_at_tick(peer, now_tick, &event_due)) {
        enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
        return false;
    }
    return ack_due || event_due;
}

bool SV_NativeShadowOutputEligiblePeekV1(
    const sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms)
{
    worr_native_readiness_state_v1 readiness;
    bool ack_due;
    bool event_due;
    uint64_t now_tick;

    if (!native_shadow_hooks_exact(peer) ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DETACHED ||
        !project_clock(peer, raw_time_ms, &now_tick)) {
        return false;
    }
    if (peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
        readiness = peer->readiness;
        if (!Worr_NativeReadinessStateValidateV1(&readiness) ||
            !Worr_NativeReadinessCanTransmitNativeV1(
                &readiness, now_tick)) {
            return false;
        }
    }
    return ack_due_at_tick(peer, now_tick, &ack_due) &&
           server_data_due_at_tick(peer, now_tick, &event_due) &&
           (ack_due || event_due);
}

bool SV_NativeShadowAckDueV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms)
{
    return SV_NativeShadowOutputDueV1(peer, raw_time_ms);
}

bool SV_NativeShadowAckEligiblePeekV1(
    const sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms)
{
    return SV_NativeShadowOutputEligiblePeekV1(peer, raw_time_ms);
}

void SV_NativeShadowRecordAsyncRateDeferralV1(
    sv_native_shadow_peer_v1 *peer)
{
    if (peer_structural_valid(peer))
        saturating_increment(&peer->async_rate_deferrals);
}

void SV_NativeShadowRecordAsyncFragmentDeferralV1(
    sv_native_shadow_peer_v1 *peer)
{
    if (peer_structural_valid(peer))
        saturating_increment(&peer->async_fragment_deferrals);
}

bool SV_NativeShadowBeginAsyncWakeV1(
    sv_native_shadow_peer_v1 *peer)
{
    if (!peer_structural_valid(peer) || peer->async_wake_active)
        return false;
    saturating_increment(&peer->async_wake_attempts);
    peer->async_wake_active = 1;
    peer->async_wake_handoff_seen = 0;
    return true;
}

void SV_NativeShadowEndAsyncWakeV1(
    sv_native_shadow_peer_v1 *peer)
{
    if (!peer_structural_valid(peer) || !peer->async_wake_active)
        return;
    if (peer->async_wake_handoff_seen)
        saturating_increment(&peer->async_ack_handoffs);
    else
        saturating_increment(&peer->async_wake_no_handoff);
    peer->async_wake_active = 0;
    peer->async_wake_handoff_seen = 0;
}

bool SV_NativeShadowGetStatusV1(
    const sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms,
    sv_native_shadow_status_v1 *status_out)
{
    sv_native_shadow_status_v1 status;

    if (!peer_structural_valid(peer) || status_out == NULL)
        return false;
    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    status.schema_version = SV_NATIVE_SHADOW_STATUS_VERSION;
    status.enabled = peer->enabled;
    status.lifecycle = peer->lifecycle;
    status.hooks_attached = native_shadow_hooks_exact(peer) ? 1u : 0u;
    status.readiness_phase = peer->readiness_initialized
        ? peer->readiness.phase : WORR_NATIVE_READINESS_PHASE_RESET;
    status.official_connection_epoch = peer->official_connection_epoch;
    status.transport_epoch = peer->private_transport_epoch;
    status.public_capabilities = peer->official_connection_epoch != 0
        ? WORR_NET_CAP_LEGACY_STAGE_MASK : 0;
    status.private_capabilities = peer->readiness_initialized
        ? peer->readiness.negotiated_capabilities
        : (peer->transport_initialized
               ? peer->transport.binding.negotiated_capabilities : 0);
    status.wire_committed = peer->native_wire_committed;
    status.wire_committed_transport_epoch =
        peer->wire_committed_transport_epoch;
    status.ack_eligible =
        SV_NativeShadowAckEligiblePeekV1(peer, raw_time_ms) ? 1u : 0u;
    status.cancelled_through_transport_epoch =
        peer->cancelled_through_transport_epoch;
    status.challenges_queued = peer->challenges_queued;
    status.client_ready_records = peer->client_ready_records;
    status.server_active_records = peer->server_active_records;
    status.rx_carriers = peer->rx_carriers;
    status.rx_commits = peer->rx_commits;
    status.rx_repeat_refreshes = peer->rx_repeat_refreshes;
    status.legacy_join_observations = peer->legacy_join_observations;
    status.command_matches = peer->command_matches;
    status.command_mismatches = peer->command_mismatches;
    status.sample_offset_mismatches = peer->sample_offset_mismatches;
    status.tx_ack_prepares = peer->tx_ack_prepares;
    status.tx_ack_handoffs = peer->tx_ack_handoffs;
    status.async_rate_deferrals = peer->async_rate_deferrals;
    status.async_fragment_deferrals = peer->async_fragment_deferrals;
    status.async_wake_attempts = peer->async_wake_attempts;
    status.async_ack_handoffs = peer->async_ack_handoffs;
    status.async_wake_no_handoff = peer->async_wake_no_handoff;
    status.rx_rejections = peer->rx_rejections;
    status.tx_ack_rejections = peer->tx_ack_rejections;
    status.rx_drained = peer->rx_drained;
    status.drain_entries = peer->drain_entries;
    status.failures = peer->failures;
    status.cancellation_barriers = peer->cancellation_barriers;
    status.cancelled_transports = peer->cancelled_transports;
    status.cancelled_rx_messages = peer->cancelled_rx_messages;
    status.cancelled_receipts = peer->cancelled_receipts;
    status.cancelled_event_records = peer->cancelled_event_records;
    status.stale_cancelled_carriers = peer->stale_cancelled_carriers;
    status.stale_cancelled_readiness_records =
        peer->stale_cancelled_readiness_records;
    status.last_failure = peer->last_failure;
    *status_out = status;
    return true;
}

bool SV_NativeShadowGetEventStatusV1(
    const sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms,
    sv_native_shadow_event_status_v1 *status_out)
{
    sv_native_shadow_event_status_v1 status;
    const sv_native_shadow_event_state_v1 *state;
    const worr_native_event_sender_v1 *sender;
    worr_native_readiness_state_v1 readiness;
    uint64_t now_tick;

    if (!event_state_valid(peer) || status_out == NULL ||
        !project_clock(peer, raw_time_ms, &now_tick))
        return false;
    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    status.schema_version = SV_NATIVE_SHADOW_EVENT_STATUS_VERSION;
    status.mode = peer->mode;
    status.active_confirms = peer->client_active_confirm_records;
    status.output_due =
        SV_NativeShadowOutputEligiblePeekV1(peer, raw_time_ms) ? 1u : 0u;
    if (peer->mode != SV_NATIVE_SHADOW_MODE_EVENT) {
        *status_out = status;
        return true;
    }

    state = peer->event_state;
    status.sender_initialized = state->current_sender_initialized;
    status.retired_sender_initialized =
        state->retired_sender_initialized;
    status.stream_epoch = state->stream_epoch;
    status.snapshots_queued = state->snapshots_queued;
    status.snapshot_queue_failures = state->snapshot_queue_failures;
    status.retired_retained_count =
        state->retired_sender_initialized
            ? state->retired_sender.tx.retained_count
            : 0;
    if (peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
        readiness = peer->readiness;
        status.tx_open =
            Worr_NativeReadinessCanTransmitNativeV1(
                &readiness, now_tick)
                ? 1u
                : 0u;
    }
    if (!state->current_sender_initialized) {
        *status_out = status;
        return true;
    }

    sender = &state->current_sender;
    status.descriptor_acked =
        (sender->state_flags &
         WORR_NATIVE_EVENT_SENDER_DESCRIPTOR_ACKED) != 0;
    status.backlog_count = sender->backlog_count;
    status.retained_count = sender->tx.retained_count;
    status.candidates_queued = sender->telemetry.candidates_queued;
    status.candidates_promoted = sender->telemetry.candidates_promoted;
    status.descriptors_acknowledged =
        sender->telemetry.descriptors_acknowledged;
    status.events_acknowledged =
        sender->telemetry.events_acknowledged;
    status.packets_prepared = sender->telemetry.packets_prepared;
    status.packets_confirmed = sender->telemetry.packets_confirmed;
    status.packets_rejected = sender->telemetry.packets_rejected;
    status.first_sends = sender->telemetry.first_sends;
    status.retries = sender->telemetry.retries;
    *status_out = status;
    return true;
}

bool SV_NativeShadowGetSnapshotStatusV1(
    const sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms,
    sv_native_shadow_snapshot_status_v1 *status_out)
{
    sv_native_shadow_snapshot_status_v1 status;
    worr_native_snapshot_sender_status_v1 sender_status;
    const sv_native_shadow_snapshot_state_v1 *state;
    worr_native_readiness_state_v1 readiness;
    uint64_t now_tick;

    if (!snapshot_state_valid(peer) || status_out == NULL ||
        !project_clock(peer, raw_time_ms, &now_tick)) {
        return false;
    }
    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    status.schema_version =
        SV_NATIVE_SHADOW_SNAPSHOT_STATUS_VERSION;
    status.mode = peer->mode;
    status.active_confirms = peer->client_active_confirm_records;
    status.output_due =
        SV_NativeShadowOutputEligiblePeekV1(peer, raw_time_ms)
            ? 1u
            : 0u;
    if (peer->mode != SV_NATIVE_SHADOW_MODE_SNAPSHOT) {
        *status_out = status;
        return true;
    }

    state = peer->snapshot_state;
    status.sender_initialized = state->current_sender_initialized;
    status.retired_sender_initialized =
        state->retired_sender_initialized;
    status.snapshot_epoch = state->snapshot_epoch;
    status.snapshots_queued = state->snapshots_queued;
    status.snapshot_queue_failures =
        state->snapshot_queue_failures;
    if (state->retired_sender_initialized &&
        Worr_NativeSnapshotSenderGetStatusV1(
            &state->retired_sender, &sender_status)) {
        status.retired_retained_count =
            sender_status.retained_messages;
    }
    if (peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
        readiness = peer->readiness;
        status.tx_open =
            Worr_NativeReadinessCanTransmitNativeV1(
                &readiness, now_tick)
                ? 1u
                : 0u;
    }
    if (!state->current_sender_initialized) {
        *status_out = status;
        return true;
    }
    memset(&sender_status, 0, sizeof(sender_status));
    if (!Worr_NativeSnapshotSenderGetStatusV1(
            &state->current_sender, &sender_status)) {
        return false;
    }
    status.retained_count = sender_status.retained_messages;
    status.active_payload_bytes =
        sender_status.active_payload_bytes;
    status.pending_payload_bytes =
        sender_status.pending_payload_bytes;
    status.active_snapshot = sender_status.active_snapshot;
    status.pending_snapshot = sender_status.pending_snapshot;
    status.snapshots_superseded =
        sender_status.telemetry.snapshots_superseded;
    status.pending_coalesced =
        sender_status.telemetry.pending_coalesced;
    status.acknowledgements_applied =
        sender_status.telemetry.acknowledgements_applied;
    status.payloads_released =
        sender_status.telemetry.payloads_released;
    status.packets_prepared =
        sender_status.telemetry.packets_prepared;
    status.packets_confirmed =
        sender_status.telemetry.packets_confirmed;
    status.packets_rejected =
        sender_status.telemetry.packets_rejected;
    status.first_sends = sender_status.telemetry.first_sends;
    status.retries = sender_status.telemetry.retries;
    *status_out = status;
    return true;
}

bool SV_NativeShadowObserveInterveningServiceV1(
    sv_native_shadow_peer_v1 *peer)
{
    worr_native_readiness_sideband_result_v1 result;

    if (!SV_NativeShadowPeerEnabledV1(peer) ||
        peer->readiness_initialized == 0 || peer->packet_open == 0)
        return false;
    result = Worr_NativeReadinessSidebandObserveInterveningServiceV1(
        &peer->sideband);
    if (result == WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND)
        return true;
    SV_NativeShadowPeerDisableV1(
        peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
    return false;
}

bool SV_NativeShadowPacketEndV1(sv_native_shadow_peer_v1 *peer)
{
    worr_native_readiness_sideband_result_v1 result;

    if (!SV_NativeShadowPeerEnabledV1(peer) ||
        peer->readiness_initialized == 0 || peer->packet_open == 0)
        return false;
    result = Worr_NativeReadinessSidebandPacketEndV1(&peer->sideband);
    peer->packet_open = 0;
    if (result == WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED)
        return true;
    SV_NativeShadowPeerDisableV1(
        peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
    return false;
}
