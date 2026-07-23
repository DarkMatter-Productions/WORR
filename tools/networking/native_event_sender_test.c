/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/legacy_entity_event_candidate.h"
#include "common/net/native_event_sender.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,       \
              #condition);                                                     \
      return false;                                                            \
    }                                                                          \
  } while (0)

static worr_native_session_binding_v1 make_binding(uint32_t capabilities) {
  worr_native_session_binding_v1 binding;

  memset(&binding, 0, sizeof(binding));
  binding.struct_size = sizeof(binding);
  binding.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
  binding.transport_epoch = 71;
  binding.negotiated_capabilities = capabilities;
  binding.connection_owner_id = 7001;
  return binding;
}

static bool make_candidate(uint32_t tick, uint32_t entity_index,
                           uint8_t raw_event,
                           worr_event_record_v1 *candidate_out) {
  worr_event_entity_ref_v1 source_entity;
  uint64_t semantic_hash;

  source_entity.index = entity_index;
  source_entity.generation = tick + 1u;
  return Worr_LegacyEntityEventCandidateBuildV1(
      tick, (uint64_t)tick * UINT64_C(1000), entity_index, source_entity,
      raw_event, 1024, candidate_out, &semantic_hash);
}

static bool make_blaster_candidate(uint32_t tick, uint32_t entity_index,
                                   uint16_t payload_kind,
                                   worr_event_record_v1 *candidate_out) {
  worr_event_payload_legacy_temp_v1 temporary;
  worr_event_payload_muzzle_v1 muzzle;
  worr_event_payload_spatial_audio_v1 spatial;
  uint16_t valid_fields = 0;

  if (!make_candidate(tick, entity_index,
                      WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, candidate_out))
    return false;
  memset(candidate_out->payload, 0, sizeof(candidate_out->payload));
  candidate_out->payload_kind = payload_kind;
  switch (payload_kind) {
  case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1:
    memset(&temporary, 0, sizeof(temporary));
    temporary.subtype = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
    if (!Worr_EventLegacyTempFieldMaskV1(
            temporary.subtype, 0, &valid_fields))
      return false;
    temporary.valid_fields = valid_fields;
    temporary.position1[0] = 3.0f;
    temporary.direction[1] = 1.0f;
    candidate_out->event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
    candidate_out->payload_size = sizeof(temporary);
    memcpy(candidate_out->payload, &temporary, sizeof(temporary));
    break;
  case WORR_EVENT_PAYLOAD_MUZZLE_V1:
    memset(&muzzle, 0, sizeof(muzzle));
    muzzle.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
    muzzle.flash_id = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
    candidate_out->event_type = WORR_EVENT_TYPE_WEAPON_FIRE;
    candidate_out->payload_size = sizeof(muzzle);
    memcpy(candidate_out->payload, &muzzle, sizeof(muzzle));
    break;
  case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1:
    memset(&spatial, 0, sizeof(spatial));
    spatial.asset_id = 99;
    spatial.raw_entity = WORR_EVENT_NO_ENTITY;
    spatial.volume = 1.0f;
    spatial.attenuation = 2.0f;
    spatial.pitch = 1.0f;
    candidate_out->event_type = WORR_EVENT_TYPE_AUDIO_CUE;
    candidate_out->payload_size = sizeof(spatial);
    memcpy(candidate_out->payload, &spatial, sizeof(spatial));
    break;
  default:
    return false;
  }
  return Worr_EventRecordCandidateValidateV1(candidate_out, 1024);
}

static bool make_ack_packet_for_epoch(uint32_t transport_epoch, uint32_t first,
                                      uint32_t last, uint8_t *packet_out,
                                      size_t packet_capacity,
                                      size_t *packet_bytes_out) {
  worr_native_carrier_entry_v1 entry;

  memset(&entry, 0, sizeof(entry));
  entry.struct_size = sizeof(entry);
  entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
  entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
  entry.first_message_sequence = first;
  entry.last_message_sequence = last;
  return Worr_NativeCarrierEncodeV1(transport_epoch, NULL, 0, NULL, 0, &entry,
                                    1, packet_out, packet_capacity,
                                    packet_bytes_out) == WORR_NATIVE_CARRIER_OK;
}

static bool make_ack_packet(const worr_native_session_binding_v1 *binding,
                            uint32_t first, uint32_t last, uint8_t *packet_out,
                            size_t packet_capacity, size_t *packet_bytes_out) {
  return make_ack_packet_for_epoch(binding->transport_epoch, first, last,
                                   packet_out, packet_capacity,
                                   packet_bytes_out);
}

static bool
make_ack_ranges_packet(const worr_native_session_binding_v1 *binding,
                       const uint32_t (*ranges)[2], uint16_t range_count,
                       uint8_t *packet_out, size_t packet_capacity,
                       size_t *packet_bytes_out) {
  worr_native_carrier_entry_v1 entries[WORR_NATIVE_CARRIER_MAX_ENTRIES];
  uint16_t index;

  if (range_count == 0 || range_count > WORR_NATIVE_CARRIER_MAX_ENTRIES)
    return false;
  memset(entries, 0, sizeof(entries));
  for (index = 0; index < range_count; ++index) {
    entries[index].struct_size = sizeof(entries[index]);
    entries[index].schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entries[index].entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    entries[index].first_message_sequence = ranges[index][0];
    entries[index].last_message_sequence = ranges[index][1];
  }
  return Worr_NativeCarrierEncodeV1(binding->transport_epoch, NULL, 0, NULL, 0,
                                    entries, range_count, packet_out,
                                    packet_capacity,
                                    packet_bytes_out) == WORR_NATIVE_CARRIER_OK;
}

static bool decode_only_data(const uint8_t *packet, size_t packet_bytes,
                             worr_native_envelope_frame_info_v1 *frame_out) {
  worr_native_carrier_view_v1 view;

  memset(&view, 0, sizeof(view));
  return Worr_NativeCarrierDecodeV1(packet, packet_bytes, &view) ==
             WORR_NATIVE_CARRIER_OK &&
         view.entry_count == 1 &&
         view.entries[0].entry_type == WORR_NATIVE_CARRIER_ENTRY_DATA_V1 &&
         Worr_NativeEnvelopeDecodeV1(packet + view.entries[0].data_offset,
                                     view.entries[0].data_bytes, frame_out) ==
             WORR_NATIVE_ENVELOPE_DECODE_OK;
}

static bool test_descriptor_gate_and_exact_stride(void) {
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_event_stream_descriptor_v1 descriptor;
  worr_native_event_sender_v1 sender;
  worr_native_carrier_ack_ledger_v1 receipt_ledger;
  worr_event_record_v1 candidates[2];
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t legacy[760];
  size_t packet_bytes = 0;
  size_t ack_packet_bytes = 0;
  uint64_t token = 0;
  uint32_t promoted = UINT32_MAX;
  uint32_t acknowledged = UINT32_MAX;
  uint32_t descriptor_message;
  worr_native_envelope_frame_info_v1 frame;
  uint32_t index;
  uint32_t event_payloads = 0;
  bool due = false;

  memset(&sender, 0xa5, sizeof(sender));
  memset(legacy, 0x4c, sizeof(legacy));
  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 99, 10));
  CHECK(Worr_NativeEventSenderInitV1(&sender, &binding, &descriptor, 1024, 872,
                                     1000) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&receipt_ledger, &binding, 3));
  CHECK(Worr_NativeEventSenderValidateV1(&sender));
  CHECK(sender.tx.retained_count == 1);
  descriptor_message = sender.tx_slots[0].message_sequence;
  CHECK(Worr_NativeEventSenderDataDuePeekV1(&sender, 1000, 100, &due) ==
            WORR_NATIVE_EVENT_SENDER_OK &&
        due);

  CHECK(
      make_candidate(10, 4, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, &candidates[0]));
  CHECK(make_candidate(11, 5, WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN,
                       &candidates[1]));
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(&sender, candidates, 2) ==
        WORR_NATIVE_EVENT_SENDER_QUEUED);
  CHECK(Worr_NativeEventSenderPumpV1(&sender, 1001, &promoted) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(promoted == 0 && sender.backlog_count == 2 &&
        sender.tx.retained_count == 1);

  /* A descriptor codec payload is 56 bytes, so the exact WNE stride still
   * permits a 760-byte legacy prefix at a 1024-byte mixed budget. */
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 1002, 100, 100, 1024, legacy,
            sizeof(legacy), packet, sizeof(packet), &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(token != 0 && packet_bytes <= 1024);
  CHECK(Worr_NativeEventSenderDataDuePeekV1(&sender, 1002, 100, &due) ==
            WORR_NATIVE_EVENT_SENDER_OK &&
        !due);
  memset(&frame, 0, sizeof(frame));
  CHECK(decode_only_data(packet, packet_bytes, &frame));
  CHECK(frame.record.record_class ==
        WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(&sender, &receipt_ledger, 1002,
                                             packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderDataDuePeekV1(&sender, 1050, 100, &due) ==
            WORR_NATIVE_EVENT_SENDER_NOT_DUE &&
        !due);
  CHECK(Worr_NativeEventSenderDataDuePeekV1(&sender, 1102, 100, &due) ==
            WORR_NATIVE_EVENT_SENDER_OK &&
        due);

  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
        WORR_NATIVE_EVENT_SENDER_ACK_APPLIED);
  CHECK(acknowledged == 1 &&
        (sender.state_flags & WORR_NATIVE_EVENT_SENDER_DESCRIPTOR_ACKED) != 0);
  CHECK(Worr_NativeEventSenderPumpV1(&sender, 1003, &promoted) ==
        WORR_NATIVE_EVENT_SENDER_PROMOTED);
  CHECK(promoted == 2 && sender.backlog_count == 0 &&
        sender.tx.retained_count == 2);
  CHECK(Worr_NativeEventSenderDataDuePeekV1(&sender, 1003, 100, &due) ==
            WORR_NATIVE_EVENT_SENDER_OK &&
        due);

  for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
    const worr_native_event_sender_payload_v1 *payload =
        &sender.payloads[index];
    if ((payload->state_flags & WORR_NATIVE_EVENT_SENDER_PAYLOAD_OCCUPIED) !=
        0) {
      worr_event_record_v1 event;
      CHECK(payload->record.record_class == WORR_NATIVE_RECORD_EVENT_V1);
      CHECK(Worr_NativeCodecEventDecodeV1(payload->encoded,
                                          payload->encoded_bytes, 1024,
                                          &event) == WORR_NATIVE_CODEC_OK);
      CHECK(event.event_id.stream_epoch == 99);
      CHECK(event.event_id.sequence == 10 + event_payloads);
      ++event_payloads;
    }
  }
  CHECK(event_payloads == 2);
  CHECK(Worr_NativeEventSenderValidateV1(&sender));
  return true;
}

static bool test_event_handoff_reject_retire_and_late_ack(void) {
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_event_stream_descriptor_v1 descriptor;
  worr_native_event_sender_v1 sender;
  worr_native_carrier_ack_ledger_v1 receipt_ledger;
  worr_event_record_v1 candidate;
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t legacy[696];
  size_t packet_bytes;
  size_t ack_packet_bytes;
  uint64_t token;
  uint32_t promoted;
  uint32_t acknowledged;
  uint32_t descriptor_message;
  uint32_t event_message = 0;
  uint32_t index;

  memset(legacy, 0x22, sizeof(legacy));
  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 100, 1));
  CHECK(Worr_NativeEventSenderInitV1(&sender, &binding, &descriptor, 1024, 872,
                                     2000) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&receipt_ledger, &binding, 3));
  descriptor_message = sender.tx_slots[0].message_sequence;

  /* Mark the descriptor as previously sent, then release it exactly. */
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 2001, 100, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(&sender, &receipt_ledger, 2001,
                                             packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
        WORR_NATIVE_EVENT_SENDER_ACK_APPLIED);

  CHECK(make_candidate(20, 8, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, &candidate));
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(&sender, &candidate, 1) ==
        WORR_NATIVE_EVENT_SENDER_QUEUED);
  CHECK(Worr_NativeEventSenderPumpV1(&sender, 2002, &promoted) ==
        WORR_NATIVE_EVENT_SENDER_PROMOTED);
  for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
    if ((sender.tx_slots[index].state_flags & WORR_NATIVE_TX_SLOT_OCCUPIED) !=
        0)
      event_message = sender.tx_slots[index].message_sequence;
  }
  CHECK(event_message != 0);

  /* Legacy entity payloads encode to 120 bytes.  Their exact 176-byte WNE
   * datagram fits the mixed seven-ACK reserve beside 696 legacy bytes. */
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 2003, 100, 100, 1024, legacy,
            sizeof(legacy), packet, sizeof(packet), &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderRejectMixedV1(&sender, &receipt_ledger, packet,
                                            packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK((sender.tx_gate.state_flags & WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) == 0);

  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 2004, 100, 100, 1024, legacy,
            sizeof(legacy), packet, sizeof(packet), &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(&sender, &receipt_ledger, 2004,
                                             packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderRetireV1(&sender) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(&sender, &candidate, 1) ==
        WORR_NATIVE_EVENT_SENDER_RETIRED_RESULT);
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 2005, 100, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_RETIRED_RESULT);

  CHECK(make_ack_packet(&binding, event_message, event_message, ack_packet,
                        sizeof(ack_packet), &ack_packet_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
        WORR_NATIVE_EVENT_SENDER_ACK_APPLIED);
  CHECK(acknowledged == 1 && sender.tx.retained_count == 0 &&
        sender.payload_occupied == 0);
  CHECK(Worr_NativeEventSenderValidateV1(&sender));
  return true;
}

static bool test_fail_closed_inputs_and_backlog_capacity(void) {
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_native_session_binding_v1 legacy_binding = make_binding(
      WORR_NET_CAP_LEGACY_STAGE_MASK | WORR_NET_CAP_NATIVE_ENVELOPE_V1);
  worr_native_session_binding_v1 uncancelled_binding = make_binding(
      WORR_NET_CAP_LEGACY_STAGE_MASK |
      WORR_NET_CAP_NATIVE_ENVELOPE_V1 |
      WORR_NET_CAP_NATIVE_EVENT_STREAM_V1);
  worr_native_session_binding_v1 combined_binding = make_binding(
      WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PRIVATE_MASK);
  worr_event_stream_descriptor_v1 descriptor;
  worr_native_event_sender_v1 sender;
  worr_event_record_v1 candidates[WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY];
  worr_event_record_v1 extra;
  uint16_t before_count;
  uint64_t before_queued;
  uint32_t index;

  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 101, 1));
  memset(&sender, 0x7a, sizeof(sender));
  CHECK(Worr_NativeEventSenderInitV1(&sender, &legacy_binding, &descriptor,
                                     1024, 872, 3000) ==
        WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT);
  CHECK(Worr_NativeEventSenderInitV1(
            &sender, &uncancelled_binding, &descriptor, 1024, 872,
            3000) == WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT);
  CHECK(((const uint8_t *)&sender)[0] == 0x7a);

  CHECK(Worr_NativeEventSenderInitV1(
            &sender, &combined_binding, &descriptor, 1024, 872,
            3000) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderValidateV1(&sender));
  CHECK(sender.tx_slots[0].message_sequence == 1u &&
        sender.tx.next_message_sequence == 2u);

  CHECK(Worr_NativeEventSenderInitV1(&sender, &binding, &descriptor, 1024, 872,
                                     3000) == WORR_NATIVE_EVENT_SENDER_OK);
  for (index = 0; index < WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY; ++index) {
    CHECK(make_candidate(30 + index, index % 1024,
                         WORR_EVENT_LEGACY_ENTITY_FOOTSTEP,
                         &candidates[index]));
  }
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(
            &sender, candidates, WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY) ==
        WORR_NATIVE_EVENT_SENDER_QUEUED);
  CHECK(make_candidate(900, 3, WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN, &extra));
  before_count = sender.backlog_count;
  before_queued = sender.telemetry.candidates_queued;
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(&sender, &extra, 1) ==
        WORR_NATIVE_EVENT_SENDER_CAPACITY);
  CHECK(sender.backlog_count == before_count &&
        sender.telemetry.candidates_queued == before_queued &&
        Worr_NativeEventSenderValidateV1(&sender));
  return true;
}

static bool test_ack_faults_and_duplicate_idempotence(void) {
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_event_stream_descriptor_v1 descriptor;
  worr_native_event_sender_v1 sender;
  worr_native_event_sender_v1 sender_before;
  worr_native_carrier_ack_ledger_v1 receipt_ledger;
  worr_event_record_v1 candidate;
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  size_t packet_bytes = 0;
  size_t ack_packet_bytes = 0;
  uint64_t token = 0;
  uint32_t descriptor_message;
  uint32_t acknowledged;
  uint32_t promoted;
  worr_native_envelope_frame_info_v1 frame;

  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 102, 40));
  CHECK(Worr_NativeEventSenderInitV1(&sender, &binding, &descriptor, 1024, 872,
                                     4000) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&receipt_ledger, &binding, 3));
  CHECK(make_candidate(40, 10, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, &candidate));
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(&sender, &candidate, 1) ==
        WORR_NATIVE_EVENT_SENDER_QUEUED);
  descriptor_message = sender.tx_slots[0].message_sequence;
  CHECK(sender.backlog_count == 1 &&
        (sender.backlog[sender.backlog_head].flags &
         WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0);

  /* An exact ACK is not authority until the descriptor was handed off. */
  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
  sender_before = sender;
  acknowledged = UINT32_C(0x11111111);
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
        WORR_NATIVE_EVENT_SENDER_TRANSPORT_REJECTED);
  CHECK(acknowledged == UINT32_C(0x11111111) &&
        memcmp(&sender, &sender_before, sizeof(sender)) == 0 &&
        Worr_NativeEventSenderValidateV1(&sender));
  promoted = UINT32_MAX;
  CHECK(Worr_NativeEventSenderPumpV1(&sender, 4000, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_OK &&
        promoted == 0 && sender.backlog_count == 1 &&
        sender.next_event_sequence == descriptor.first_sequence);

  /* A future range cannot manufacture a retained descriptor identity. */
  CHECK(make_ack_packet(&binding, descriptor_message + 1u,
                        descriptor_message + 1u, ack_packet, sizeof(ack_packet),
                        &ack_packet_bytes));
  sender_before = sender;
  acknowledged = UINT32_C(0x22222222);
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
        WORR_NATIVE_EVENT_SENDER_TRANSPORT_REJECTED);
  CHECK(acknowledged == UINT32_C(0x22222222) &&
        memcmp(&sender, &sender_before, sizeof(sender)) == 0 &&
        (sender.state_flags & WORR_NATIVE_EVENT_SENDER_DESCRIPTOR_ACKED) == 0 &&
        Worr_NativeEventSenderValidateV1(&sender));

  /* A valid ACK from another transport epoch is equally inert. */
  CHECK(make_ack_packet_for_epoch(
      binding.transport_epoch + 1u, descriptor_message, descriptor_message,
      ack_packet, sizeof(ack_packet), &ack_packet_bytes));
  sender_before = sender;
  acknowledged = UINT32_C(0x33333333);
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
        WORR_NATIVE_EVENT_SENDER_TRANSPORT_REJECTED);
  CHECK(acknowledged == UINT32_C(0x33333333) &&
        memcmp(&sender, &sender_before, sizeof(sender)) == 0 &&
        sender.backlog_count == 1 && Worr_NativeEventSenderValidateV1(&sender));

  /* DATA with no ACK entry cannot be interpreted as an ACK. */
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 4001, 100, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OK);
  memset(&frame, 0, sizeof(frame));
  CHECK(decode_only_data(packet, packet_bytes, &frame));
  sender_before = sender;
  acknowledged = UINT32_C(0x44444444);
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, packet, packet_bytes,
                                          &acknowledged) ==
        WORR_NATIVE_EVENT_SENDER_TRANSPORT_REJECTED);
  CHECK(acknowledged == UINT32_C(0x44444444) &&
        memcmp(&sender, &sender_before, sizeof(sender)) == 0 &&
        (sender.state_flags & WORR_NATIVE_EVENT_SENDER_DESCRIPTOR_ACKED) == 0 &&
        sender.backlog_count == 1 &&
        (sender.backlog[sender.backlog_head].flags &
         WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0 &&
        Worr_NativeEventSenderValidateV1(&sender));
  CHECK(Worr_NativeEventSenderConfirmMixedV1(&sender, &receipt_ledger, 4001,
                                             packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);

  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
  acknowledged = UINT32_MAX;
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 1 && sender.backlog_count == 1 &&
        sender.next_event_sequence == descriptor.first_sequence &&
        (sender.backlog[sender.backlog_head].flags &
         WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0);

  /* Duplicate descriptor ACK is a no-op: it cannot assign an event ID. */
  acknowledged = UINT32_MAX;
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 0 && sender.telemetry.descriptors_acknowledged == 1 &&
        sender.tx.retained_count == 0 && sender.payload_occupied == 0 &&
        sender.backlog_count == 1 &&
        sender.next_event_sequence == descriptor.first_sequence &&
        (sender.backlog[sender.backlog_head].flags &
         WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0 &&
        Worr_NativeEventSenderValidateV1(&sender));
  promoted = UINT32_MAX;
  CHECK(Worr_NativeEventSenderPumpV1(&sender, 4002, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_PROMOTED &&
        promoted == 1 && sender.backlog_count == 0 &&
        sender.next_event_sequence == descriptor.first_sequence + 1u &&
        Worr_NativeEventSenderValidateV1(&sender));
  return true;
}

static bool test_partial_multi_range_event_release(void) {
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_event_stream_descriptor_v1 descriptor;
  worr_native_event_sender_v1 sender;
  worr_native_carrier_ack_ledger_v1 receipt_ledger;
  worr_event_record_v1 candidates[4];
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint32_t message_sequences[4] = {0};
  uint32_t ranges[2][2];
  size_t packet_bytes = 0;
  size_t ack_packet_bytes = 0;
  uint64_t token = 0;
  uint32_t descriptor_message;
  uint32_t acknowledged;
  uint32_t promoted;
  uint32_t index;

  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 103, 200));
  CHECK(Worr_NativeEventSenderInitV1(&sender, &binding, &descriptor, 1024, 872,
                                     5000) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&receipt_ledger, &binding, 3));
  descriptor_message = sender.tx_slots[0].message_sequence;
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 5001, 10000, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(&sender, &receipt_ledger, 5001,
                                             packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 1);

  for (index = 0; index < 4; ++index) {
    CHECK(make_candidate(200 + index, 20 + index,
                         WORR_EVENT_LEGACY_ENTITY_FOOTSTEP,
                         &candidates[index]));
  }
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(&sender, candidates, 4) ==
        WORR_NATIVE_EVENT_SENDER_QUEUED);
  CHECK(Worr_NativeEventSenderPumpV1(&sender, 5002, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_PROMOTED &&
        promoted == 4);
  for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
    const worr_native_tx_slot_v1 *slot = &sender.tx_slots[index];
    if ((slot->state_flags & WORR_NATIVE_TX_SLOT_OCCUPIED) != 0) {
      CHECK(slot->record.record_class == WORR_NATIVE_RECORD_EVENT_V1);
      CHECK(slot->record.object_sequence >= descriptor.first_sequence &&
            slot->record.object_sequence < descriptor.first_sequence + 4u);
      message_sequences[slot->record.object_sequence -
                        descriptor.first_sequence] = slot->message_sequence;
    }
  }
  for (index = 0; index < 4; ++index)
    CHECK(message_sequences[index] != 0);

  /* Give every retained event one confirmed send attempt. */
  for (index = 0; index < 4; ++index) {
    CHECK(Worr_NativeEventSenderPrepareMixedV1(
              &sender, &receipt_ledger, 5003 + index, 10000, 100, 1024, NULL, 0,
              packet, sizeof(packet), &packet_bytes,
              &token) == WORR_NATIVE_EVENT_SENDER_OK);
    CHECK(Worr_NativeEventSenderConfirmMixedV1(
              &sender, &receipt_ledger, 5003 + index, packet, packet_bytes) ==
          WORR_NATIVE_EVENT_SENDER_OK);
  }

  ranges[0][0] = message_sequences[0];
  ranges[0][1] = message_sequences[1];
  ranges[1][0] = message_sequences[3];
  ranges[1][1] = message_sequences[3];
  CHECK(make_ack_ranges_packet(&binding, ranges, 2, ack_packet,
                               sizeof(ack_packet), &ack_packet_bytes));
  acknowledged = UINT32_MAX;
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 3 && sender.tx.retained_count == 1 &&
        sender.payload_occupied == 1 &&
        sender.telemetry.events_acknowledged == 3 &&
        Worr_NativeEventSenderValidateV1(&sender));
  for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
    if ((sender.tx_slots[index].state_flags & WORR_NATIVE_TX_SLOT_OCCUPIED) !=
        0)
      CHECK(sender.tx_slots[index].message_sequence == message_sequences[2]);
  }

  CHECK(make_ack_packet(&binding, message_sequences[2], message_sequences[2],
                        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 1 && sender.tx.retained_count == 0 &&
        sender.payload_occupied == 0 &&
        sender.telemetry.events_acknowledged == 4 &&
        Worr_NativeEventSenderValidateV1(&sender));
  return true;
}

static bool test_exact_mixed_budget_failure_transactionality(void) {
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_event_stream_descriptor_v1 descriptor;
  worr_native_event_sender_v1 sender;
  worr_native_event_sender_v1 sender_before;
  worr_native_carrier_ack_ledger_v1 receipt_ledger;
  worr_native_carrier_ack_ledger_v1 ledger_before;
  worr_event_record_v1 candidate;
  uint8_t legacy[760];
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t packet_before[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  size_t packet_bytes;
  size_t ack_packet_bytes = 0;
  uint64_t token;
  uint32_t descriptor_message;
  uint32_t acknowledged;
  uint32_t promoted;

  memset(legacy, 0x61, sizeof(legacy));
  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 104, 300));
  CHECK(Worr_NativeEventSenderInitV1(&sender, &binding, &descriptor, 1024, 872,
                                     6000) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&receipt_ledger, &binding, 3));
  descriptor_message = sender.tx_slots[0].message_sequence;

  /* The valid 760-byte prefix still cannot fit in a 761-byte output. */
  memset(packet, 0x62, sizeof(packet));
  memcpy(packet_before, packet, sizeof(packet));
  sender_before = sender;
  ledger_before = receipt_ledger;
  packet_bytes = (size_t)UINT32_C(0x63636363);
  token = UINT64_C(0x6464646464646464);
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 6001, 100, 100, 1024, legacy,
            sizeof(legacy), packet, 761, &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OUTPUT_TOO_SMALL);
  CHECK(memcmp(&sender, &sender_before, sizeof(sender)) == 0 &&
        memcmp(&receipt_ledger, &ledger_before, sizeof(receipt_ledger)) == 0 &&
        memcmp(packet, packet_before, sizeof(packet)) == 0 &&
        packet_bytes == (size_t)UINT32_C(0x63636363) &&
        token == UINT64_C(0x6464646464646464) &&
        Worr_NativeEventSenderValidateV1(&sender) &&
        Worr_NativeCarrierAckLedgerValidateV1(&receipt_ledger));

  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 6001, 100, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(&sender, &receipt_ledger, 6001,
                                             packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 1);
  CHECK(make_candidate(300, 30, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, &candidate));
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(&sender, &candidate, 1) ==
        WORR_NATIVE_EVENT_SENDER_QUEUED);
  CHECK(Worr_NativeEventSenderPumpV1(&sender, 6002, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_PROMOTED &&
        promoted == 1);

  /* The valid 696-byte prefix still cannot fit in a 697-byte output. */
  memset(packet, 0x65, sizeof(packet));
  memcpy(packet_before, packet, sizeof(packet));
  sender_before = sender;
  ledger_before = receipt_ledger;
  packet_bytes = (size_t)UINT32_C(0x66666666);
  token = UINT64_C(0x6767676767676767);
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 6003, 100, 100, 1024, legacy, 696, packet,
            697, &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OUTPUT_TOO_SMALL);
  CHECK(memcmp(&sender, &sender_before, sizeof(sender)) == 0 &&
        memcmp(&receipt_ledger, &ledger_before, sizeof(receipt_ledger)) == 0 &&
        memcmp(packet, packet_before, sizeof(packet)) == 0 &&
        packet_bytes == (size_t)UINT32_C(0x66666666) &&
        token == UINT64_C(0x6767676767676767) &&
        Worr_NativeEventSenderValidateV1(&sender) &&
        Worr_NativeCarrierAckLedgerValidateV1(&receipt_ledger));
  return true;
}

static bool test_event_sequence_exhaustion(void) {
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_event_stream_descriptor_v1 descriptor;
  worr_native_event_sender_v1 sender;
  worr_native_event_sender_v1 sender_before;
  worr_native_carrier_ack_ledger_v1 receipt_ledger;
  worr_event_record_v1 candidates[2];
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  size_t packet_bytes = 0;
  size_t ack_packet_bytes = 0;
  uint64_t token = 0;
  uint32_t descriptor_message;
  uint32_t acknowledged;
  uint32_t promoted;
  uint32_t event_payloads = 0;
  uint32_t index;
  bool due = true;

  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 105, UINT32_MAX));
  CHECK(Worr_NativeEventSenderInitV1(&sender, &binding, &descriptor, 1024, 872,
                                     7000) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&receipt_ledger, &binding, 3));
  descriptor_message = sender.tx_slots[0].message_sequence;
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 7001, 100, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(&sender, &receipt_ledger, 7001,
                                             packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(&sender, ack_packet, ack_packet_bytes,
                                          &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 1);
  CHECK(make_candidate(400, 40, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP,
                       &candidates[0]));
  CHECK(make_candidate(401, 41, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP,
                       &candidates[1]));
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(&sender, candidates, 2) ==
        WORR_NATIVE_EVENT_SENDER_QUEUED);
  promoted = UINT32_MAX;
  CHECK(Worr_NativeEventSenderPumpV1(&sender, 7002, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_SEQUENCE_LIMIT &&
        promoted == 1 && sender.backlog_count == 1 &&
        sender.next_event_sequence == UINT32_MAX &&
        (sender.state_flags & WORR_NATIVE_EVENT_SENDER_SEQUENCE_EXHAUSTED) !=
            0 &&
        (sender.backlog[sender.backlog_head].flags &
         WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0);
  for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
    const worr_native_event_sender_payload_v1 *payload =
        &sender.payloads[index];
    if ((payload->state_flags & WORR_NATIVE_EVENT_SENDER_PAYLOAD_OCCUPIED) !=
        0) {
      worr_event_record_v1 event;
      CHECK(payload->record.record_class == WORR_NATIVE_RECORD_EVENT_V1);
      CHECK(Worr_NativeCodecEventDecodeV1(payload->encoded,
                                          payload->encoded_bytes, 1024,
                                          &event) == WORR_NATIVE_CODEC_OK);
      CHECK(event.event_id.stream_epoch == descriptor.stream_epoch &&
            event.event_id.sequence == UINT32_MAX);
      ++event_payloads;
    }
  }
  CHECK(event_payloads == 1 && Worr_NativeEventSenderValidateV1(&sender));
  CHECK(Worr_NativeEventSenderDataDuePeekV1(
            &sender, 7003, 100, &due) ==
            WORR_NATIVE_EVENT_SENDER_SEQUENCE_LIMIT &&
        due);
  sender_before = sender;
  promoted = UINT32_MAX;
  CHECK(Worr_NativeEventSenderPumpV1(&sender, 7003, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_SEQUENCE_LIMIT &&
        promoted == 0 && memcmp(&sender, &sender_before, sizeof(sender)) == 0 &&
        Worr_NativeEventSenderValidateV1(&sender));
  return true;
}

static bool test_schema2_receipt_window_stalls_on_oldest_unacked(void) {
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_event_stream_descriptor_v1 descriptor;
  static worr_native_event_sender_v1 sender;
  static worr_native_event_sender_v1 corrupt_sender;
  worr_native_carrier_ack_ledger_v1 ledger;
  worr_event_record_v1 candidates[72];
  worr_native_envelope_frame_info_v1 frame;
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  size_t packet_bytes;
  size_t ack_bytes;
  uint64_t token;
  uint32_t descriptor_message;
  uint32_t acknowledged;
  uint32_t promoted;
  uint32_t event_payloads = 0;
  uint32_t logical_events = 0;
  uint32_t earliest_outer_sequence = UINT32_MAX;
  uint32_t earliest_message = 0;
  uint32_t later_first_message = UINT32_MAX;
  uint32_t later_last_message = 0;
  uint32_t next_before_stall;
  uint16_t head_before_stall;
  uint32_t index;
  uint32_t sent_payloads;
  bool due = true;

  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 108, 1000));
  descriptor.flags = WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2;
  CHECK(Worr_NativeEventSenderInitV1(
            &sender, &binding, &descriptor, 1024, 872, 9500) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&ledger, &binding, 3));
  descriptor_message = sender.tx_slots[0].message_sequence;
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &ledger, 9501, 100, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes, &token) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(
            &sender, &ledger, 9501, packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(
            &sender, ack_packet, ack_bytes, &acknowledged) ==
        WORR_NATIVE_EVENT_SENDER_ACK_APPLIED);

  for (index = 0; index < 72; ++index) {
    CHECK(make_candidate(800, 100u + index,
                         WORR_EVENT_LEGACY_ENTITY_FOOTSTEP,
                         &candidates[index]));
  }
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(
            &sender, candidates, 72) == WORR_NATIVE_EVENT_SENDER_QUEUED);
  CHECK(Worr_NativeEventSenderPumpV1(
            &sender, 9502, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_PROMOTED &&
        promoted == WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY &&
        sender.backlog_count == 8 &&
        sender.next_event_sequence == 1064);

  for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
    const worr_native_tx_slot_v1 *slot = &sender.tx_slots[index];
    const worr_native_event_sender_payload_v1 *payload;
    worr_native_event_batch_info_v1 batch;
    if ((slot->state_flags & WORR_NATIVE_TX_SLOT_OCCUPIED) == 0 ||
        slot->record.record_class != WORR_NATIVE_RECORD_EVENT_V1)
      continue;
    payload = &sender.payloads[
        slot->payload_handle & WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_MASK];
    CHECK(Worr_NativeEventBatchInspectV1(
              payload->encoded, payload->encoded_bytes, &batch) ==
          WORR_NATIVE_EVENT_BATCH_OK);
    ++event_payloads;
    logical_events += batch.event_count;
    if (slot->record.object_sequence < earliest_outer_sequence) {
      if (earliest_message != 0) {
        if (earliest_message < later_first_message)
          later_first_message = earliest_message;
        if (earliest_message > later_last_message)
          later_last_message = earliest_message;
      }
      earliest_outer_sequence = slot->record.object_sequence;
      earliest_message = slot->message_sequence;
    } else {
      if (slot->message_sequence < later_first_message)
        later_first_message = slot->message_sequence;
      if (slot->message_sequence > later_last_message)
        later_last_message = slot->message_sequence;
    }
  }
  CHECK(event_payloads > 1 && logical_events == 64 &&
        earliest_message != 0 && later_first_message != UINT32_MAX &&
        later_last_message >= later_first_message);
  corrupt_sender = sender;
  ++corrupt_sender.next_event_sequence;
  CHECK(!Worr_NativeEventSenderValidateV1(&corrupt_sender));

  /* Hand off every transport payload once.  Model only the oldest batch's
   * packet/ACK as lost; later batches are eligible for exact ACK release. */
  for (sent_payloads = 0; sent_payloads < event_payloads; ++sent_payloads) {
    CHECK(Worr_NativeEventSenderPrepareMixedV1(
              &sender, &ledger, 9502, 100, 100, 1024, NULL, 0, packet,
              sizeof(packet), &packet_bytes, &token) ==
          WORR_NATIVE_EVENT_SENDER_OK);
    CHECK(decode_only_data(packet, packet_bytes, &frame));
    if (sent_payloads == 0)
      CHECK(frame.message_sequence == earliest_message);
    CHECK(Worr_NativeEventSenderConfirmMixedV1(
              &sender, &ledger, 9502, packet, packet_bytes) ==
          WORR_NATIVE_EVENT_SENDER_OK);
  }

  /* Later transport ACKs can free most slots, but they cannot widen the
   * cgame receipt window while the first logical batch remains unacknowledged. */
  CHECK(make_ack_packet(&binding, later_first_message, later_last_message,
                        ack_packet, sizeof(ack_packet), &ack_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(
            &sender, ack_packet, ack_bytes, &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == event_payloads - 1u &&
        sender.tx.retained_count == 1);
  next_before_stall = sender.next_event_sequence;
  head_before_stall = sender.backlog_head;
  promoted = UINT32_MAX;
  CHECK(Worr_NativeEventSenderPumpV1(
            &sender, 9503, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_CAPACITY &&
        promoted == 0 && sender.next_event_sequence == next_before_stall &&
        sender.backlog_head == head_before_stall &&
        sender.backlog_count == 8 && sender.tx.retained_count == 1);
  due = true;
  CHECK(Worr_NativeEventSenderDataDuePeekV1(
            &sender, 9503, 100, &due) ==
            WORR_NATIVE_EVENT_SENDER_NOT_DUE &&
        !due);

  CHECK(make_ack_packet(&binding, earliest_message, earliest_message,
                        ack_packet, sizeof(ack_packet), &ack_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(
            &sender, ack_packet, ack_bytes, &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 1 && sender.tx.retained_count == 0);
  CHECK(Worr_NativeEventSenderPumpV1(
            &sender, 9504, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_PROMOTED &&
        promoted == 8 && sender.backlog_count == 0 &&
        sender.next_event_sequence == 1072 &&
        sender.telemetry.schema2_events_promoted == 72 &&
        Worr_NativeEventSenderValidateV1(&sender));
  return true;
}

static bool test_retire_rejects_prepared_packet(void) {
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_event_stream_descriptor_v1 descriptor;
  worr_native_event_sender_v1 sender;
  worr_native_event_sender_v1 sender_before;
  worr_native_carrier_ack_ledger_v1 receipt_ledger;
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  size_t packet_bytes = 0;
  uint64_t token = 0;

  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 106, 500));
  CHECK(Worr_NativeEventSenderInitV1(&sender, &binding, &descriptor, 1024, 872,
                                     8000) == WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&receipt_ledger, &binding, 3));
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &receipt_ledger, 8001, 100, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes,
            &token) == WORR_NATIVE_EVENT_SENDER_OK);
  sender_before = sender;
  CHECK(Worr_NativeEventSenderRetireV1(&sender) ==
            WORR_NATIVE_EVENT_SENDER_INVALID_STATE &&
        memcmp(&sender, &sender_before, sizeof(sender)) == 0 &&
        Worr_NativeEventSenderValidateV1(&sender) &&
        Worr_NativeCarrierAckLedgerValidateV1(&receipt_ledger));
  CHECK(Worr_NativeEventSenderRejectMixedV1(&sender, &receipt_ledger, packet,
                                            packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderRetireV1(&sender) ==
            WORR_NATIVE_EVENT_SENDER_OK &&
        Worr_NativeEventSenderValidateV1(&sender));
  return true;
}

static bool test_explicit_counted_cancellation(void) {
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_event_stream_descriptor_v1 descriptor;
  static worr_native_event_sender_v1 sender;
  static worr_native_event_sender_v1 sender_before;
  static worr_native_event_sender_v1 empty;
  static worr_native_event_sender_v1 active;
  worr_native_carrier_ack_ledger_v1 receipt_ledger;
  worr_event_record_v1 candidates[2];
  worr_native_event_sender_cancel_report_v1 report;
  worr_native_event_sender_payload_v1 expected_payload;
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  size_t packet_bytes = 0;
  size_t ack_packet_bytes = 0;
  uint64_t token = 0;
  uint32_t descriptor_message;
  uint32_t acknowledged;
  uint32_t descriptor_payload_index = UINT32_MAX;
  uint32_t descriptor_payload_generation = 0;
  uint32_t index;
  worr_native_event_sender_result_v1 apply_result;

  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 107, 600));
  CHECK(Worr_NativeEventSenderInitV1(&sender, &binding, &descriptor, 1024,
                                     872, 9000) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(make_candidate(500, 50, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP,
                       &candidates[0]));
  CHECK(make_candidate(501, 51, WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN,
                       &candidates[1]));
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(
            &sender, candidates, 2) == WORR_NATIVE_EVENT_SENDER_QUEUED);
  CHECK(sender.tx.retained_count == 1 && sender.payload_occupied == 1 &&
        sender.backlog_count == 2 &&
        sender.telemetry.candidates_promoted == 0);
  for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
    if ((sender.payloads[index].state_flags &
         WORR_NATIVE_EVENT_SENDER_PAYLOAD_OCCUPIED) != 0) {
      descriptor_payload_index = index;
      descriptor_payload_generation = sender.payloads[index].generation;
      break;
    }
  }
  CHECK(descriptor_payload_index != UINT32_MAX &&
        descriptor_payload_generation != 0);

  sender_before = sender;
  memset(&report, 0xa5, sizeof(report));
  CHECK(Worr_NativeEventSenderCancelV1(
            &sender,
            (worr_native_event_sender_cancel_report_v1 *)&sender.payloads[0]) ==
        WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT);
  CHECK(memcmp(&sender, &sender_before, sizeof(sender)) == 0 &&
        report.retained_messages == UINT32_C(0xa5a5a5a5));

  CHECK(Worr_NativeEventSenderCancelV1(&sender, &report) ==
        WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT);
  CHECK(report.retained_messages == 1 &&
        report.backlog_candidates == 2 && report.payloads_released == 1 &&
        sender.tx.retained_count == 0 && sender.payload_occupied == 0 &&
        sender.backlog_count == 0 &&
        (sender.state_flags & WORR_NATIVE_EVENT_SENDER_RETIRED) != 0 &&
        (sender.state_flags & WORR_NATIVE_EVENT_SENDER_CANCELLED) != 0 &&
        sender.binding.transport_epoch ==
            sender_before.binding.transport_epoch &&
        sender.binding.connection_owner_id ==
            sender_before.binding.connection_owner_id &&
        sender.next_event_sequence == sender_before.next_event_sequence &&
        sender.tx.next_message_sequence ==
            sender_before.tx.next_message_sequence &&
        memcmp(&sender.telemetry, &sender_before.telemetry,
               sizeof(sender.telemetry)) == 0 &&
        sender.telemetry.candidates_promoted == 0 &&
        Worr_NativeEventSenderValidateV1(&sender));
  memset(&expected_payload, 0, sizeof(expected_payload));
  expected_payload.generation = descriptor_payload_generation;
  CHECK(memcmp(&sender.payloads[descriptor_payload_index],
               &expected_payload, sizeof(expected_payload)) == 0);
  acknowledged = UINT32_MAX;
  CHECK(make_ack_packet(&binding, 1, 1, ack_packet, sizeof(ack_packet),
                        &ack_packet_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(
            &sender, ack_packet, ack_packet_bytes, &acknowledged) ==
        WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT);
  CHECK(acknowledged == UINT32_MAX && sender.tx.retained_count == 0 &&
        Worr_NativeEventSenderValidateV1(&sender));
  sender_before = sender;
  memset(&report, 0xa5, sizeof(report));
  CHECK(Worr_NativeEventSenderCancelV1(&sender, &report) ==
        WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT);
  CHECK(report.retained_messages == 0 && report.backlog_candidates == 0 &&
        report.payloads_released == 0 &&
        memcmp(&sender, &sender_before, sizeof(sender)) == 0);

  /* An already empty active sender still receives an explicit terminal
   * disposition with zero counts. */
  CHECK(Worr_NativeEventSenderInitV1(&empty, &binding, &descriptor, 1024,
                                     872, 9100) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  descriptor_message = empty.tx_slots[0].message_sequence;
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&receipt_ledger, &binding, 3));
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &empty, &receipt_ledger, 9101, 100, 100, 1024, NULL, 0,
            packet, sizeof(packet), &packet_bytes, &token) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(
            &empty, &receipt_ledger, 9101, packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_packet_bytes));
  apply_result = Worr_NativeEventSenderApplyAcksV1(
      &empty, ack_packet, ack_packet_bytes, &acknowledged);
  CHECK(apply_result == WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 1 && empty.tx.retained_count == 0 &&
        empty.payload_occupied == 0);
  CHECK(Worr_NativeEventSenderCancelV1(&empty, &report) ==
        WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT);
  CHECK(report.retained_messages == 0 && report.backlog_candidates == 0 &&
        report.payloads_released == 0 &&
        Worr_NativeEventSenderValidateV1(&empty));

  /* A prepared packet has no terminal transport outcome.  Cancellation is
   * rejected transactionally until Reject/Confirm resolves it. */
  CHECK(Worr_NativeEventSenderInitV1(&active, &binding, &descriptor, 1024,
                                     872, 9200) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&receipt_ledger, &binding, 3));
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &active, &receipt_ledger, 9201, 100, 100, 1024, NULL, 0,
            packet, sizeof(packet), &packet_bytes, &token) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  sender_before = active;
  memset(&report, 0x5a, sizeof(report));
  CHECK(Worr_NativeEventSenderCancelV1(&active, &report) ==
        WORR_NATIVE_EVENT_SENDER_INVALID_STATE);
  CHECK(memcmp(&active, &sender_before, sizeof(active)) == 0 &&
        report.retained_messages == UINT32_C(0x5a5a5a5a));
  CHECK(Worr_NativeEventSenderRejectMixedV1(
            &active, &receipt_ledger, packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderCancelV1(&active, &report) ==
        WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT);
  CHECK(report.retained_messages == 1 &&
        report.backlog_candidates == 0 && report.payloads_released == 1 &&
        Worr_NativeEventSenderValidateV1(&active));
  return true;
}

static bool test_same_source_coordinates_schema2_batch_and_logical_telemetry(
    void) {
  static const uint16_t blaster_kinds[5] = {
      WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
      WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
      WORR_EVENT_PAYLOAD_MUZZLE_V1,
      WORR_EVENT_PAYLOAD_MUZZLE_V1,
      WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1,
  };
  const uint32_t capabilities = WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
  worr_native_session_binding_v1 binding = make_binding(capabilities);
  worr_event_stream_descriptor_v1 descriptor;
  static worr_native_event_sender_v1 sender;
  static worr_native_event_sender_v1 legacy_sender;
  static worr_native_event_sender_v1 corrupt_sender;
  worr_native_carrier_ack_ledger_v1 ledger;
  worr_native_carrier_ack_ledger_v1 legacy_ledger;
  worr_event_record_v1 candidates[5];
  worr_event_record_v1 decoded[WORR_NATIVE_EVENT_BATCH_MAX_EVENTS];
  worr_native_event_batch_info_v1 batch;
  worr_native_envelope_frame_info_v1 frame;
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  size_t packet_bytes;
  size_t ack_bytes;
  uint64_t token;
  uint32_t descriptor_message;
  uint32_t event_message = 0;
  uint32_t acknowledged;
  uint32_t promoted;
  uint32_t index;
  bool due = false;
  const worr_native_event_sender_payload_v1 *batch_payload = NULL;

  CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 109, 700));
  descriptor.flags |= WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2;
  CHECK(Worr_EventStreamDescriptorValidateV1(&descriptor));
  CHECK(Worr_NativeEventSenderInitV1(
            &sender, &binding, &descriptor, 1024, 872, 10000) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&ledger, &binding, 3));
  descriptor_message = sender.tx_slots[0].message_sequence;
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &ledger, 10001, 100, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes, &token) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(
            &sender, &ledger, 10001, packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(
            &sender, ack_packet, ack_bytes, &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 1);

  for (index = 0; index < 5; ++index) {
    CHECK(make_blaster_candidate(600, 60u + index,
                                 blaster_kinds[index],
                                 &candidates[index]));
  }
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(
            &sender, candidates, 5) == WORR_NATIVE_EVENT_SENDER_QUEUED);
  CHECK(Worr_NativeEventSenderDataDuePeekV1(
            &sender, 10000, 100, &due) ==
            WORR_NATIVE_EVENT_SENDER_CLOCK_REGRESSION &&
        !due);
  CHECK(Worr_NativeEventSenderPumpV1(&sender, 10002, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_PROMOTED &&
        promoted == 5 && sender.backlog_count == 0 &&
        sender.tx.retained_count == 1 && sender.payload_occupied == 1 &&
        sender.telemetry.candidates_promoted == 5 &&
        sender.telemetry.schema2_batches_promoted == 1 &&
        sender.telemetry.schema2_events_promoted == 5);
  corrupt_sender = sender;
  corrupt_sender.telemetry.schema2_events_promoted = 6;
  CHECK(!Worr_NativeEventSenderValidateV1(&corrupt_sender));
  corrupt_sender = sender;
  corrupt_sender.telemetry.schema2_batches_promoted = 0;
  CHECK(!Worr_NativeEventSenderValidateV1(&corrupt_sender));
  corrupt_sender = sender;
  corrupt_sender.telemetry.schema2_batches_promoted = 3;
  CHECK(!Worr_NativeEventSenderValidateV1(&corrupt_sender));
  corrupt_sender = sender;
  corrupt_sender.telemetry.candidates_promoted = 9;
  corrupt_sender.telemetry.schema2_events_promoted = 9;
  CHECK(!Worr_NativeEventSenderValidateV1(&corrupt_sender));
  corrupt_sender = sender;
  corrupt_sender.telemetry.candidates_queued = UINT64_MAX;
  corrupt_sender.telemetry.candidates_promoted = UINT64_MAX;
  corrupt_sender.telemetry.schema2_batches_promoted = UINT64_MAX;
  corrupt_sender.telemetry.schema2_events_promoted = UINT64_MAX;
  CHECK(Worr_NativeEventSenderValidateV1(&corrupt_sender));
  corrupt_sender = sender;
  corrupt_sender.descriptor.flags = 0;
  CHECK(!Worr_NativeEventSenderValidateV1(&corrupt_sender));
  for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
    if ((sender.tx_slots[index].state_flags &
         WORR_NATIVE_TX_SLOT_OCCUPIED) != 0) {
      event_message = sender.tx_slots[index].message_sequence;
      batch_payload = &sender.payloads[
          sender.tx_slots[index].payload_handle &
          WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_MASK];
    }
  }
  CHECK(event_message != 0 && batch_payload != NULL &&
        batch_payload->record.record_schema_version ==
            WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA);
  CHECK(Worr_NativeEventBatchDecodeV1(
            batch_payload->encoded, batch_payload->encoded_bytes, 1024,
            decoded, WORR_NATIVE_EVENT_BATCH_MAX_EVENTS, &batch) ==
            WORR_NATIVE_EVENT_BATCH_OK &&
        batch.event_count == 5 && batch.first_sequence == 700 &&
        batch.last_sequence == 704 &&
        batch.nested_event_bytes == 752 &&
        batch_payload->encoded_bytes == 784 &&
        batch_payload->encoded_bytes +
                WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ==
            840 &&
        batch_payload->encoded_bytes +
                WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES <=
            872);
  for (index = 0; index < 5; ++index)
    CHECK(decoded[index].event_id.sequence == 700u + index);

  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &ledger, 10002, 100, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes, &token) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(decode_only_data(packet, packet_bytes, &frame));
  CHECK(frame.record.record_schema_version ==
            WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA &&
        frame.record.object_sequence == 704 && frame.fragment_count == 1);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(
            &sender, &ledger, 10002, packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(sender.telemetry.first_sends == 6);

  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &sender, &ledger, 10102, 100, 100, 1024, NULL, 0, packet,
            sizeof(packet), &packet_bytes, &token) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(decode_only_data(packet, packet_bytes, &frame));
  CHECK(frame.message_sequence == event_message &&
        frame.record.record_schema_version ==
            WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA &&
        frame.record.object_sequence == 704 && frame.fragment_count == 1);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(
            &sender, &ledger, 10102, packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(sender.telemetry.first_sends == 6 &&
        sender.telemetry.retries == 5 &&
        sender.telemetry.schema2_batches_promoted == 1 &&
        sender.telemetry.schema2_events_promoted == 5);

  CHECK(make_ack_packet(&binding, event_message, event_message, ack_packet,
                        sizeof(ack_packet), &ack_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(
            &sender, ack_packet, ack_bytes, &acknowledged) ==
            WORR_NATIVE_EVENT_SENDER_ACK_APPLIED &&
        acknowledged == 1 && sender.telemetry.events_acknowledged == 5 &&
        sender.tx.retained_count == 0);

  /* Without the descriptor opt-in, even a set with exact shared producer
   * coordinates remains five schema-1 transport objects. */
  descriptor.flags = 0;
  CHECK(Worr_NativeEventSenderInitV1(
            &legacy_sender, &binding, &descriptor, 1024, 872, 10100) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeCarrierAckLedgerInitV1(&legacy_ledger, &binding, 3));
  descriptor_message = legacy_sender.tx_slots[0].message_sequence;
  CHECK(Worr_NativeEventSenderPrepareMixedV1(
            &legacy_sender, &legacy_ledger, 10101, 100, 100, 1024, NULL, 0,
            packet, sizeof(packet), &packet_bytes, &token) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(Worr_NativeEventSenderConfirmMixedV1(
            &legacy_sender, &legacy_ledger, 10101, packet, packet_bytes) ==
        WORR_NATIVE_EVENT_SENDER_OK);
  CHECK(make_ack_packet(&binding, descriptor_message, descriptor_message,
                        ack_packet, sizeof(ack_packet), &ack_bytes));
  CHECK(Worr_NativeEventSenderApplyAcksV1(
            &legacy_sender, ack_packet, ack_bytes, &acknowledged) ==
        WORR_NATIVE_EVENT_SENDER_ACK_APPLIED);
  CHECK(Worr_NativeEventSenderQueueCandidatesV1(
            &legacy_sender, candidates, 5) ==
        WORR_NATIVE_EVENT_SENDER_QUEUED);
  CHECK(Worr_NativeEventSenderPumpV1(
            &legacy_sender, 10102, &promoted) ==
            WORR_NATIVE_EVENT_SENDER_PROMOTED &&
        promoted == 5 && legacy_sender.tx.retained_count == 5);
  CHECK(legacy_sender.telemetry.schema2_batches_promoted == 0 &&
        legacy_sender.telemetry.schema2_events_promoted == 0);
  for (index = 0; index < WORR_NATIVE_EVENT_SENDER_TX_CAPACITY; ++index) {
    if ((legacy_sender.tx_slots[index].state_flags &
         WORR_NATIVE_TX_SLOT_OCCUPIED) != 0)
      CHECK(legacy_sender.tx_slots[index].record.record_schema_version ==
            WORR_EVENT_ABI_VERSION);
  }
  return true;
}

int main(void) {
  if (!test_descriptor_gate_and_exact_stride() ||
      !test_event_handoff_reject_retire_and_late_ack() ||
      !test_fail_closed_inputs_and_backlog_capacity() ||
      !test_ack_faults_and_duplicate_idempotence() ||
      !test_partial_multi_range_event_release() ||
      !test_exact_mixed_budget_failure_transactionality() ||
      !test_event_sequence_exhaustion() ||
      !test_schema2_receipt_window_stalls_on_oldest_unacked() ||
      !test_retire_rejects_prepared_packet() ||
      !test_same_source_coordinates_schema2_batch_and_logical_telemetry() ||
      !test_explicit_counted_cancellation())
    return 1;
  puts("native event sender tests passed");
  return 0;
}
