/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_snapshot_sender.h"
#include "common/net/snapshot_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_MAX_ENTITIES 1024u
#define TEST_WNE_DATAGRAM_BYTES 128u
#define TEST_RESEND_TICKS 100u
#define TEST_ACK_RETRY_TICKS 100u

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,          \
                    __LINE__, #condition);                                     \
            return false;                                                      \
        }                                                                      \
    } while (0)

typedef struct snapshot_fixture_s {
    worr_snapshot_store_v2 store;
    worr_snapshot_store_slot_v2 slots[1];
    worr_snapshot_entity_v2 entities[2];
    uint8_t area[8];
    worr_snapshot_event_ref_v2 events[2];
    worr_snapshot_ref_v2 ref;
    worr_snapshot_projection_view_v2 view;
} snapshot_fixture;

static worr_native_snapshot_sender_v1 sender;
static worr_native_snapshot_sender_v1 sender_before;
static uint8_t active_copy[WORR_NATIVE_SNAPSHOT_SENDER_MAX_ENCODED_BYTES];

static worr_native_session_binding_v1 make_binding(uint32_t capabilities)
{
    worr_native_session_binding_v1 binding;

    memset(&binding, 0, sizeof(binding));
    binding.struct_size = sizeof(binding);
    binding.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    binding.transport_epoch = 91;
    binding.negotiated_capabilities = capabilities;
    binding.connection_owner_id = UINT64_C(91001);
    return binding;
}

static worr_snapshot_entity_generation_v2 make_generation(uint32_t index,
                                                          uint32_t generation)
{
    worr_snapshot_entity_generation_v2 result;

    memset(&result, 0, sizeof(result));
    result.identity.index = index;
    result.identity.generation = generation;
    result.provenance_flags =
        WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    return result;
}

static worr_snapshot_player_v2 make_player(void)
{
    worr_snapshot_player_v2 player;
    uint32_t index;

    memset(&player, 0, sizeof(player));
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.controlled_entity = make_generation(1, 4);
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    player.movement.origin[0] = 10.0f;
    player.movement.velocity[1] = -2.0f;
    player.movement.movement_flags = 5;
    player.movement.movement_time_ms = 17;
    player.movement.gravity = 800;
    player.movement.view_height = 22;
    player.movement.delta_angles[2] = 45.0f;
    player.view_angles[1] = 90.0f;
    player.view_offset[2] = 22.0f;
    player.gun_index = 7;
    player.gun_frame = 11;
    player.gun_skin = 2;
    player.gun_rate = 10;
    player.rdflags = 3;
    player.team_id = 2;
    player.fov = 100.0f;
    for (index = 0; index < WORR_SNAPSHOT_STATS_CAPACITY; ++index)
        player.stats[index] = (int16_t)index;
    return player;
}

static worr_snapshot_v2 make_snapshot(uint32_t epoch)
{
    worr_snapshot_v2 snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    snapshot.flags =
        WORR_SNAPSHOT_FLAG_KEYFRAME |
        WORR_SNAPSHOT_FLAG_COMPLETE |
        WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS |
        WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
    snapshot.snapshot_id.epoch = epoch;
    snapshot.snapshot_id.sequence = 1;
    snapshot.server_tick = epoch * 10u;
    snapshot.server_time_us = (uint64_t)epoch * UINT64_C(100000);
    snapshot.controlled_entity = make_generation(1, 4);
    snapshot.discontinuity.flags =
        WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
        WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
    return snapshot;
}

static bool init_fixture(snapshot_fixture *fixture, uint32_t epoch)
{
    worr_snapshot_v2 snapshot = make_snapshot(epoch);
    worr_snapshot_player_v2 player = make_player();
    worr_snapshot_store_publish_v2 publication;

    memset(fixture, 0, sizeof(*fixture));
    CHECK(Worr_SnapshotStoreInitV2(
              &fixture->store, fixture->slots, 1,
              fixture->entities, 2, 2, fixture->area, 8, 8,
              fixture->events, 2, 2, TEST_MAX_ENTITIES) ==
          WORR_SNAPSHOT_STORE_OK);
    memset(&publication, 0, sizeof(publication));
    publication.struct_size = sizeof(publication);
    publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
    publication.snapshot = &snapshot;
    publication.player = &player;
    CHECK(Worr_SnapshotStorePublishV2(
              &fixture->store, &publication, &fixture->ref) ==
          WORR_SNAPSHOT_STORE_OK);

    memset(&fixture->view, 0, sizeof(fixture->view));
    fixture->view.struct_size = sizeof(fixture->view);
    fixture->view.schema_version =
        WORR_SNAPSHOT_PROJECTION_VERSION;
    fixture->view.snapshot =
        &fixture->slots[fixture->ref.slot].snapshot;
    fixture->view.player =
        &fixture->slots[fixture->ref.slot].player;
    fixture->view.entities = fixture->entities;
    fixture->view.area_bytes = fixture->area;
    fixture->view.event_refs = fixture->events;
    fixture->view.entity_count =
        fixture->view.snapshot->entity_range.count;
    fixture->view.area_byte_count =
        fixture->view.snapshot->area_range.count;
    fixture->view.event_ref_count =
        fixture->view.snapshot->event_range.count;
    return true;
}

static bool make_ack_packet(
    const worr_native_session_binding_v1 *binding,
    uint32_t first,
    uint32_t last,
    uint8_t *packet_out,
    size_t packet_capacity,
    size_t *packet_bytes_out)
{
    worr_native_carrier_entry_v1 entry;

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    entry.first_message_sequence = first;
    entry.last_message_sequence = last;
    return Worr_NativeCarrierEncodeV1(
               binding->transport_epoch, NULL, 0, NULL, 0,
               &entry, 1, packet_out, packet_capacity,
               packet_bytes_out) == WORR_NATIVE_CARRIER_OK;
}

static bool decode_data_record(
    const uint8_t *packet,
    size_t packet_bytes,
    worr_native_carrier_view_v1 *carrier_out,
    worr_native_envelope_frame_info_v1 *frame_out)
{
    worr_native_carrier_view_v1 carrier;

    memset(&carrier, 0, sizeof(carrier));
    if (Worr_NativeCarrierDecodeV1(
            packet, packet_bytes, &carrier) !=
            WORR_NATIVE_CARRIER_OK ||
        carrier.entry_count == 0 ||
        carrier.entries[0].entry_type !=
            WORR_NATIVE_CARRIER_ENTRY_DATA_V1 ||
        Worr_NativeEnvelopeDecodeV1(
            packet + carrier.entries[0].data_offset,
            carrier.entries[0].data_bytes, frame_out) !=
            WORR_NATIVE_ENVELOPE_DECODE_OK)
        return false;
    *carrier_out = carrier;
    return true;
}

static void seed_due_ack(worr_native_carrier_ack_ledger_v1 *ledger,
                         uint32_t message_sequence)
{
    worr_native_carrier_ack_receipt_v1 *receipt =
        &ledger->receipts[0];

    memset(receipt, 0, sizeof(*receipt));
    receipt->message_sequence = message_sequence;
    receipt->handoffs_remaining = ledger->proactive_handoffs;
    receipt->record_class = WORR_NATIVE_RECORD_COMMAND_V1;
    receipt->state_flags =
        WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED |
        WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE;
    ledger->receipt_count = 1;
}

static bool send_remaining_burst(
    worr_native_snapshot_sender_v1 *stream,
    worr_native_carrier_ack_ledger_v1 *ledger,
    uint32_t expected_snapshot_epoch,
    uint64_t *tick,
    uint32_t *packet_count_out)
{
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint32_t count = 0;

    do {
        worr_native_carrier_view_v1 carrier;
        worr_native_envelope_frame_info_v1 frame;
        size_t packet_bytes = 0;
        uint64_t token = 0;

        memset(&frame, 0, sizeof(frame));
        CHECK(Worr_NativeSnapshotSenderPrepareMixedV1(
                  stream, ledger, *tick, TEST_RESEND_TICKS,
                  TEST_ACK_RETRY_TICKS,
                  WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
                  NULL, 0, packet, sizeof(packet),
                  &packet_bytes, &token) ==
              WORR_NATIVE_SNAPSHOT_SENDER_OK);
        CHECK(token != 0);
        CHECK(decode_data_record(
            packet, packet_bytes, &carrier, &frame));
        CHECK(frame.record.record_class ==
              WORR_NATIVE_RECORD_SNAPSHOT_V1);
        CHECK(frame.record.object_epoch ==
              expected_snapshot_epoch);
        CHECK(Worr_NativeSnapshotSenderConfirmMixedV1(
                  stream, ledger, *tick, packet, packet_bytes) ==
              WORR_NATIVE_SNAPSHOT_SENDER_OK);
        ++*tick;
        ++count;
    } while ((stream->tx_gate.state_flags &
              WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0);

    *packet_count_out = count;
    return true;
}

static bool test_multifragment_coalescing_and_exact_release(void)
{
    const worr_native_session_binding_v1 binding =
        make_binding(WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK);
    snapshot_fixture first;
    snapshot_fixture middle;
    snapshot_fixture newest;
    snapshot_fixture cancellation;
    snapshot_fixture cancellation_pending;
    worr_native_carrier_ack_ledger_v1 ledger;
    worr_native_snapshot_sender_status_v1 status;
    worr_native_snapshot_sender_cancel_report_v1 cancel_report;
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes = 0;
    size_t ack_packet_bytes = 0;
    uint64_t token = 0;
    uint64_t tick = 1000;
    uint32_t first_message;
    uint32_t newest_message;
    uint32_t active_handle;
    uint32_t active_index;
    uint32_t active_bytes;
    uint32_t packet_count;
    uint32_t acknowledged = UINT32_MAX;
    bool due = false;
    worr_native_carrier_view_v1 carrier;
    worr_native_envelope_frame_info_v1 frame;

    CHECK(init_fixture(&first, 3));
    CHECK(init_fixture(&middle, 4));
    CHECK(init_fixture(&newest, 5));
    CHECK(init_fixture(&cancellation, 6));
    CHECK(init_fixture(&cancellation_pending, 7));
    memset(&sender, 0xa5, sizeof(sender));
    CHECK(Worr_NativeSnapshotSenderInitV1(
              &sender, &binding, TEST_MAX_ENTITIES,
              TEST_WNE_DATAGRAM_BYTES, tick) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK(Worr_NativeCarrierAckLedgerInitV1(
        &ledger, &binding, 3));
    seed_due_ack(&ledger, 77);
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&ledger));

    CHECK(Worr_NativeSnapshotSenderQueueV1(
              &sender, &first.view, ++tick) ==
          WORR_NATIVE_SNAPSHOT_SENDER_RETAINED);
    CHECK(sender.tx.retained_count == 1);
    first_message = sender.tx_slots[0].message_sequence;
    active_handle = sender.tx_slots[0].payload_handle;
    active_index =
        active_handle &
        WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_INDEX_MASK;
    active_bytes = sender.payloads[active_index].encoded_bytes;
    CHECK(active_bytes > TEST_WNE_DATAGRAM_BYTES);
    memcpy(active_copy, sender.payloads[active_index].encoded,
           active_bytes);

    memset(&frame, 0, sizeof(frame));
    CHECK(Worr_NativeSnapshotSenderPrepareMixedV1(
              &sender, &ledger, ++tick, TEST_RESEND_TICKS,
              TEST_ACK_RETRY_TICKS,
              WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
              NULL, 0, packet, sizeof(packet),
              &packet_bytes, &token) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK(decode_data_record(
        packet, packet_bytes, &carrier, &frame));
    CHECK(carrier.entry_count == 2);
    CHECK(carrier.entries[1].entry_type ==
          WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
    CHECK(frame.record.object_epoch == 3);
    CHECK(frame.fragment_count > 1);
    CHECK(Worr_NativeSnapshotSenderConfirmMixedV1(
              &sender, &ledger, tick, packet, packet_bytes) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    ++tick;
    CHECK((sender.tx_gate.state_flags &
           WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0);
    CHECK(ledger.telemetry.handoff_commits == 1);

    CHECK(Worr_NativeSnapshotSenderQueueV1(
              &sender, &middle.view, tick) ==
          WORR_NATIVE_SNAPSHOT_SENDER_PENDING);
    CHECK(Worr_NativeSnapshotSenderQueueV1(
              &sender, &newest.view, ++tick) ==
          WORR_NATIVE_SNAPSHOT_SENDER_COALESCED);
    CHECK(sender.pending_bank !=
          WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK);
    CHECK(sender.payloads[sender.pending_bank].record.object_epoch == 5);
    CHECK(sender.tx_slots[0].payload_handle == active_handle);
    CHECK(sender.payloads[active_index].encoded_bytes == active_bytes);
    CHECK(memcmp(sender.payloads[active_index].encoded,
                 active_copy, active_bytes) == 0);

    CHECK(send_remaining_burst(
        &sender, &ledger, 3, &tick, &packet_count));
    CHECK(packet_count + 1u == frame.fragment_count);
    CHECK(sender.tx.retained_count == 1);
    CHECK(sender.tx_slots[0].record.object_epoch == 5);
    CHECK(sender.pending_bank ==
          WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK);
    CHECK(sender.payload_occupied == 1);
    newest_message = sender.tx_slots[0].message_sequence;
    CHECK(newest_message == first_message + 1u);
    CHECK(sender.telemetry.pending_promoted == 1);
    CHECK(sender.telemetry.pending_coalesced == 1);
    CHECK(sender.telemetry.snapshots_superseded == 1);

    CHECK(make_ack_packet(
        &binding, first_message, first_message,
        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
    CHECK(Worr_NativeSnapshotSenderApplyAcksV1(
              &sender, ack_packet, ack_packet_bytes,
              &acknowledged) ==
          WORR_NATIVE_SNAPSHOT_SENDER_ACK_APPLIED);
    CHECK(acknowledged == 0);
    CHECK(sender.tx.retained_count == 1);
    CHECK(sender.tx_slots[0].message_sequence == newest_message);

    CHECK(send_remaining_burst(
        &sender, &ledger, 5, &tick, &packet_count));
    CHECK(packet_count > 1);
    CHECK(Worr_NativeSnapshotSenderDataDuePeekV1(
              &sender, tick + TEST_RESEND_TICKS - 2u,
              TEST_RESEND_TICKS, &due) ==
          WORR_NATIVE_SNAPSHOT_SENDER_NOT_DUE);
    CHECK(!due);
    tick += TEST_RESEND_TICKS;
    CHECK(Worr_NativeSnapshotSenderDataDuePeekV1(
              &sender, tick, TEST_RESEND_TICKS, &due) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK(due);

    CHECK(Worr_NativeSnapshotSenderPrepareMixedV1(
              &sender, &ledger, tick, TEST_RESEND_TICKS,
              TEST_ACK_RETRY_TICKS,
              WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
              NULL, 0, packet, sizeof(packet),
              &packet_bytes, &token) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK(Worr_NativeSnapshotSenderRejectMixedV1(
              &sender, &ledger, packet, packet_bytes) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK((sender.tx_gate.state_flags &
           WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) == 0);
    CHECK(sender.tx.retained_count == 1);
    ++tick;
    CHECK(send_remaining_burst(
        &sender, &ledger, 5, &tick, &packet_count));
    CHECK(sender.telemetry.retries == 1);
    CHECK(sender.telemetry.packets_rejected == 1);

    /*
     * Begin another retry, then admit its semantic ACK between fragments.
     * The ACK must retire the now-unnecessary gate and release the exact
     * payload rather than leaving a dispatch bound to an empty TX slot.
     */
    tick += TEST_RESEND_TICKS;
    CHECK(Worr_NativeSnapshotSenderPrepareMixedV1(
              &sender, &ledger, tick, TEST_RESEND_TICKS,
              TEST_ACK_RETRY_TICKS,
              WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
              NULL, 0, packet, sizeof(packet),
              &packet_bytes, &token) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK(Worr_NativeSnapshotSenderConfirmMixedV1(
              &sender, &ledger, tick, packet, packet_bytes) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK((sender.tx_gate.state_flags &
           WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0);

    CHECK(make_ack_packet(
        &binding, newest_message, newest_message,
        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
    acknowledged = UINT32_MAX;
    CHECK(Worr_NativeSnapshotSenderApplyAcksV1(
              &sender, ack_packet, ack_packet_bytes,
              &acknowledged) ==
          WORR_NATIVE_SNAPSHOT_SENDER_ACK_APPLIED);
    CHECK(acknowledged == 1);
    CHECK(sender.tx.retained_count == 0);
    CHECK(sender.payload_occupied == 0);
    CHECK((sender.tx_gate.state_flags &
           WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) == 0);
    CHECK(Worr_NativeSnapshotSenderGetStatusV1(
        &sender, &status));
    CHECK(status.active_message_sequence == 0);
    CHECK(status.telemetry.acknowledgements_applied == 1);

    CHECK(Worr_NativeSnapshotSenderQueueV1(
              &sender, &cancellation.view, ++tick) ==
          WORR_NATIVE_SNAPSHOT_SENDER_RETAINED);
    CHECK(Worr_NativeSnapshotSenderPrepareMixedV1(
              &sender, &ledger, ++tick, TEST_RESEND_TICKS,
              TEST_ACK_RETRY_TICKS,
              WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
              NULL, 0, packet, sizeof(packet),
              &packet_bytes, &token) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK(Worr_NativeSnapshotSenderConfirmMixedV1(
              &sender, &ledger, tick, packet, packet_bytes) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK((sender.tx_gate.state_flags &
           WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0);
    CHECK(Worr_NativeSnapshotSenderQueueV1(
              &sender, &cancellation_pending.view, ++tick) ==
          WORR_NATIVE_SNAPSHOT_SENDER_PENDING);
    CHECK(Worr_NativeSnapshotSenderCancelV1(
              &sender, &cancel_report) ==
          WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED_RESULT);
    CHECK(cancel_report.retained_messages == 1);
    CHECK(cancel_report.pending_snapshots == 1);
    CHECK(cancel_report.payloads_released == 2);
    CHECK(Worr_NativeSnapshotSenderValidateV1(&sender));
    memset(&cancel_report, 0xa5, sizeof(cancel_report));
    CHECK(Worr_NativeSnapshotSenderCancelV1(
              &sender, &cancel_report) ==
          WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED_RESULT);
    CHECK(cancel_report.retained_messages == 0);
    CHECK(cancel_report.pending_snapshots == 0);
    CHECK(cancel_report.payloads_released == 0);
    return true;
}

static bool test_fail_closed_binding_queue_and_late_ack(void)
{
    const worr_native_session_binding_v1 binding =
        make_binding(WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK);
    const worr_native_session_binding_v1 combined_binding =
        make_binding(WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK |
                     WORR_NET_CAP_NATIVE_EVENT_STREAM_V1);
    snapshot_fixture fixture;
    worr_snapshot_projection_view_v2 invalid_view;
    worr_native_carrier_ack_ledger_v1 ledger;
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes = 0;
    size_t ack_packet_bytes = 0;
    uint64_t token = 0;
    uint64_t tick = 5000;
    uint32_t packet_count;
    uint32_t message_sequence;
    uint32_t acknowledged = UINT32_MAX;

    CHECK(init_fixture(&fixture, 20));
    memset(&sender, 0x7a, sizeof(sender));
    CHECK(Worr_NativeSnapshotSenderInitV1(
              &sender, &combined_binding, TEST_MAX_ENTITIES,
              TEST_WNE_DATAGRAM_BYTES, tick) ==
          WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT);
    CHECK(((const uint8_t *)&sender)[0] == 0x7a);
    CHECK(((const uint8_t *)&sender)[sizeof(sender) - 1u] == 0x7a);

    CHECK(Worr_NativeSnapshotSenderInitV1(
              &sender, &binding, TEST_MAX_ENTITIES,
              TEST_WNE_DATAGRAM_BYTES, tick) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK(Worr_NativeCarrierAckLedgerInitV1(
        &ledger, &binding, 3));
    invalid_view = fixture.view;
    invalid_view.schema_version = 0;
    sender_before = sender;
    CHECK(Worr_NativeSnapshotSenderQueueV1(
              &sender, &invalid_view, ++tick) ==
          WORR_NATIVE_SNAPSHOT_SENDER_INVALID_RECORD);
    CHECK(memcmp(&sender, &sender_before, sizeof(sender)) == 0);

    CHECK(Worr_NativeSnapshotSenderQueueV1(
              &sender, &fixture.view, tick) ==
          WORR_NATIVE_SNAPSHOT_SENDER_RETAINED);
    message_sequence = sender.tx_slots[0].message_sequence;
    CHECK(send_remaining_burst(
        &sender, &ledger, 20, &tick, &packet_count));
    CHECK(packet_count > 1);
    CHECK(Worr_NativeSnapshotSenderRetireV1(&sender) ==
          WORR_NATIVE_SNAPSHOT_SENDER_OK);
    CHECK(Worr_NativeSnapshotSenderPrepareMixedV1(
              &sender, &ledger, tick, TEST_RESEND_TICKS,
              TEST_ACK_RETRY_TICKS,
              WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
              NULL, 0, packet, sizeof(packet), &packet_bytes,
              &token) ==
          WORR_NATIVE_SNAPSHOT_SENDER_RETIRED_RESULT);
    CHECK(make_ack_packet(
        &binding, message_sequence, message_sequence,
        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
    CHECK(Worr_NativeSnapshotSenderApplyAcksV1(
              &sender, ack_packet, ack_packet_bytes,
              &acknowledged) ==
          WORR_NATIVE_SNAPSHOT_SENDER_ACK_APPLIED);
    CHECK(acknowledged == 1);
    CHECK(sender.payload_occupied == 0);
    CHECK(Worr_NativeSnapshotSenderValidateV1(&sender));
    return true;
}

int main(void)
{
    if (!test_multifragment_coalescing_and_exact_release() ||
        !test_fail_closed_binding_queue_and_late_ack())
        return EXIT_FAILURE;
    puts("native_snapshot_sender_test: ok");
    return EXIT_SUCCESS;
}
