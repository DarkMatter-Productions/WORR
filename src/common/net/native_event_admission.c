/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_event_admission.h"
#include "common/net/native_event_batch.h"

#include "native_carrier_ack_internal.h"

#include <string.h>

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes) {
  uintptr_t left_begin;
  uintptr_t right_begin;
  uintptr_t left_end;
  uintptr_t right_end;

  if (left == NULL || right == NULL || left_bytes == 0 || right_bytes == 0) {
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

static bool record_ref_equal(worr_native_record_ref_v1 left,
                             worr_native_record_ref_v1 right) {
  return left.record_class == right.record_class &&
         left.reserved0 == right.reserved0 &&
         left.record_schema_version == right.record_schema_version &&
         left.object_epoch == right.object_epoch &&
         left.object_sequence == right.object_sequence;
}

static bool consumer_valid(const worr_native_event_consumer_v1 *consumer) {
  return consumer != NULL && consumer->struct_size == sizeof(*consumer) &&
         consumer->schema_version == WORR_NATIVE_EVENT_ADMISSION_ABI_VERSION &&
         consumer->reserved0 == 0 && consumer->opaque != NULL &&
         consumer->ResetAuthority != NULL &&
         consumer->SubmitAuthoritativeBatch != NULL &&
         consumer->GetStatus != NULL;
}

static bool
state_ranges_valid(const worr_event_stream_owner_v1 *owner,
                   const worr_native_session_binding_v1 *binding,
                   const worr_native_rx_session_v1 *session,
                   const worr_native_rx_slot_v1 *slots, size_t slots_bytes,
                   const void *payload_arena, size_t payload_arena_bytes,
                   const worr_native_carrier_ack_ledger_v1 *ack_ledger,
                   const worr_native_rx_message_v1 *message,
                   const worr_native_event_consumer_v1 *consumer) {
  const void *objects[] = {
      owner,         binding,    session, slots,
      payload_arena, ack_ledger, message, consumer,
  };
  const size_t sizes[] = {
      sizeof(*owner),   sizeof(*binding),    sizeof(*session),
      slots_bytes,      payload_arena_bytes, sizeof(*ack_ledger),
      sizeof(*message), sizeof(*consumer),
  };
  size_t left;
  size_t right;

  for (left = 0; left < sizeof(objects) / sizeof(objects[0]); ++left) {
    if (objects[left] == NULL || sizes[left] == 0)
      return false;
    for (right = left + 1; right < sizeof(objects) / sizeof(objects[0]);
         ++right) {
      if (ranges_overlap(objects[left], sizes[left], objects[right],
                         sizes[right])) {
        return false;
      }
    }
  }
  /* The object extent behind opaque is consumer-defined, but even its
   * first byte may not be one of admission's borrowed/live objects. */
  if (consumer->opaque != NULL) {
    for (left = 0; left < sizeof(objects) / sizeof(objects[0]); ++left) {
      if (ranges_overlap(consumer->opaque, 1, objects[left], sizes[left])) {
        return false;
      }
    }
  }
  return true;
}

static bool
repeat_ranges_valid(const worr_event_stream_owner_v1 *owner,
                    const worr_native_session_binding_v1 *binding,
                    const worr_native_rx_session_v1 *session,
                    const worr_native_rx_slot_v1 *slots, size_t slots_bytes,
                    const void *payload_arena, size_t payload_arena_bytes,
                    const void *packet, size_t packet_bytes,
                    const worr_native_carrier_ack_ledger_v1 *ack_ledger,
                    const worr_native_event_consumer_v1 *consumer) {
  const void *objects[] = {
      owner, binding, session, slots, payload_arena, packet, ack_ledger,
      consumer,
  };
  const size_t sizes[] = {
      sizeof(*owner), sizeof(*binding), sizeof(*session), slots_bytes,
      payload_arena_bytes, packet_bytes, sizeof(*ack_ledger),
      sizeof(*consumer),
  };
  size_t left;
  size_t right;

  for (left = 0; left < sizeof(objects) / sizeof(objects[0]); ++left) {
    if (objects[left] == NULL || sizes[left] == 0)
      return false;
    for (right = left + 1; right < sizeof(objects) / sizeof(objects[0]);
         ++right) {
      if (ranges_overlap(objects[left], sizes[left], objects[right],
                         sizes[right])) {
        return false;
      }
    }
  }
  if (consumer->opaque != NULL) {
    for (left = 0; left < sizeof(objects) / sizeof(objects[0]); ++left) {
      if (ranges_overlap(consumer->opaque, 1, objects[left], sizes[left])) {
        return false;
      }
    }
  }
  return true;
}

static bool message_matches_complete_slot(
    const worr_native_rx_session_v1 *session,
    const worr_native_rx_slot_v1 *slots, uint16_t slot_capacity,
    size_t payload_arena_bytes, const worr_native_rx_message_v1 *message) {
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
      message->payload_bytes > payload_arena_bytes - message->payload_offset) {
    return false;
  }
  slot = &slots[message->slot_index];
  reassembly = &slot->reassembly;
  return (slot->state_flags &
          (WORR_NATIVE_RX_SLOT_OCCUPIED | WORR_NATIVE_RX_SLOT_COMPLETE)) ==
             (WORR_NATIVE_RX_SLOT_OCCUPIED | WORR_NATIVE_RX_SLOT_COMPLETE) &&
         record_ref_equal(message->record, reassembly->record) &&
         message->transport_epoch == reassembly->transport_epoch &&
         message->message_sequence == reassembly->message_sequence &&
         message->payload_bytes == reassembly->total_payload_bytes &&
         message->payload_crc32 == reassembly->payload_crc32;
}

static bool
status_header_valid(const worr_cgame_event_runtime_status_v1 *status) {
  const uint32_t known_flags = WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE |
                               WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED |
                               WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED |
                               WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC;

  return status != NULL && status->struct_size == sizeof(*status) &&
         status->api_version == WORR_CGAME_EVENT_RUNTIME_API_VERSION &&
         (status->state_flags & ~known_flags) == 0 &&
         (status->state_flags & WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE) != 0 &&
         status->authority_epoch != 0 &&
         status->next_presentation_sequence != 0 &&
         status->receipt.struct_size == sizeof(status->receipt) &&
         status->receipt.schema_version == WORR_EVENT_ABI_VERSION &&
         status->receipt.stream_epoch == status->authority_epoch &&
         (uint64_t)status->next_presentation_sequence <=
             (uint64_t)status->receipt.highest_contiguous + 1u;
}

static bool
fresh_reset_status(const worr_cgame_event_runtime_status_v1 *status,
                   const worr_event_stream_descriptor_v1 *descriptor) {
  return status_header_valid(status) &&
         status->authority_epoch == descriptor->stream_epoch &&
         (status->state_flags &
          (WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED |
           WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC)) == 0 &&
         status->next_presentation_sequence == descriptor->first_sequence &&
         status->authority_count == 0 &&
         status->receipt.highest_contiguous ==
             descriptor->first_sequence - 1u &&
         status->receipt.selective_mask == 0;
}

static bool active_status(const worr_cgame_event_runtime_status_v1 *status,
                          const worr_event_stream_descriptor_v1 *descriptor) {
  return status_header_valid(status) &&
         status->authority_epoch == descriptor->stream_epoch &&
         status->next_presentation_sequence >= descriptor->first_sequence &&
         status->receipt.highest_contiguous >=
             descriptor->first_sequence - 1u &&
         (status->state_flags &
          WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC) == 0;
}

static bool submit_result_accepted(worr_cgame_event_runtime_result_v1 result) {
  return result == WORR_CGAME_EVENT_RUNTIME_OK ||
         result == WORR_CGAME_EVENT_RUNTIME_DUPLICATE ||
         result == WORR_CGAME_EVENT_RUNTIME_MATCHED ||
         result == WORR_CGAME_EVENT_RUNTIME_CORRECTED ||
         result == WORR_CGAME_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION ||
         result == WORR_CGAME_EVENT_RUNTIME_DEGRADED;
}

static worr_native_carrier_ack_receipt_v1 *event_receipt_find(
    worr_native_carrier_ack_ledger_v1 *ledger, uint32_t message_sequence) {
  uint16_t index;

  for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++index) {
    worr_native_carrier_ack_receipt_v1 *receipt = &ledger->receipts[index];
    if ((receipt->state_flags &
         WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED) != 0 &&
        receipt->message_sequence == message_sequence &&
        receipt->record_class == WORR_NATIVE_RECORD_EVENT_V1)
      return receipt;
  }
  return NULL;
}

static bool event_batch_receipt_install(
    worr_native_carrier_ack_ledger_v1 *ledger, uint32_t message_sequence,
    uint32_t first_sequence) {
  worr_native_carrier_ack_receipt_v1 *receipt =
      event_receipt_find(ledger, message_sequence);

  if (receipt == NULL || first_sequence == 0 ||
      receipt->semantic_first_sequence != 0)
    return false;
  receipt->semantic_first_sequence = first_sequence;
  return Worr_NativeCarrierAckLedgerValidateV1(ledger);
}

static void
force_consumer_resync(worr_event_stream_owner_v1 *owner,
                      const worr_event_stream_owner_v1 *attempted_owner,
                      worr_native_carrier_ack_ledger_v1 *ack_ledger,
                      const worr_native_event_consumer_v1 *consumer) {
  (void)Worr_NativeCarrierAckRetireSemanticReceiptsInternalV1(ack_ledger);
  if (Worr_EventStreamOwnerValidateV1(attempted_owner))
    *owner = *attempted_owner;
  (void)Worr_EventStreamOwnerRequireResyncV1(owner);
  (void)consumer->ResetAuthority(consumer->opaque, 0, 0);
}

static bool committed_history_matches_frame(
    const worr_native_rx_session_v1 *session,
    const worr_native_envelope_frame_info_v1 *frame) {
  uint16_t index;

  for (index = 0; index < WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY;
       ++index) {
    const worr_native_receipt_history_entry_v1 *identity =
        &session->history[index];

    if (identity->message_sequence != frame->message_sequence)
      continue;
    return record_ref_equal(identity->record, frame->record) &&
           identity->total_payload_bytes == frame->total_payload_bytes &&
           identity->payload_crc32 == frame->payload_crc32 &&
           identity->fragment_stride == frame->fragment_stride &&
           identity->fragment_count == frame->fragment_count &&
           identity->priority == frame->priority;
  }
  return false;
}

static worr_native_event_admission_result_v1 repeat_record_owner_result(
    const worr_event_stream_owner_v1 *owner,
    worr_native_record_ref_v1 record) {
  if (record.record_class == WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1) {
    if (record.record_schema_version != WORR_EVENT_STREAM_ABI_VERSION)
      return WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED;
    if (record.object_epoch != owner->descriptor.stream_epoch)
      return WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH;
    if (record.object_sequence != owner->descriptor.first_sequence)
      return WORR_NATIVE_EVENT_ADMISSION_CONFLICT;
    return WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_REPEAT_REVALIDATED;
  }
  if (record.record_class == WORR_NATIVE_RECORD_EVENT_V1) {
    if (record.record_schema_version != WORR_EVENT_ABI_VERSION &&
        record.record_schema_version !=
            WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA)
      return WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED;
    if (record.object_epoch != owner->descriptor.stream_epoch ||
        record.object_sequence < owner->descriptor.first_sequence) {
      return WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH;
    }
    if (record.record_schema_version ==
            WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA &&
        (owner->descriptor.flags &
         WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2) == 0)
      return WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED;
    return WORR_NATIVE_EVENT_ADMISSION_EVENT_REPEAT_REVALIDATED;
  }
  return WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED;
}

static worr_native_event_admission_result_v1 carrier_repeat_failure(
    worr_native_carrier_session_result_v1 result) {
  switch (result) {
  case WORR_NATIVE_CARRIER_SESSION_WRONG_EPOCH:
    return WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH;
  case WORR_NATIVE_CARRIER_SESSION_WRONG_ENTRY_TYPE:
  case WORR_NATIVE_CARRIER_SESSION_UNSUPPORTED:
    return WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED;
  case WORR_NATIVE_CARRIER_SESSION_NO_CARRIER:
  case WORR_NATIVE_CARRIER_SESSION_MALFORMED:
  case WORR_NATIVE_CARRIER_SESSION_CORRUPT:
    return WORR_NATIVE_EVENT_ADMISSION_MALFORMED;
  default:
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE;
  }
}

static worr_native_event_admission_result_v1
rx_repeat_failure(worr_native_rx_result_v1 result) {
  switch (result) {
  case WORR_NATIVE_RX_WRONG_EPOCH:
    return WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH;
  case WORR_NATIVE_RX_UNSUPPORTED:
    return WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED;
  case WORR_NATIVE_RX_MALFORMED:
  case WORR_NATIVE_RX_DATAGRAM_CORRUPT:
  case WORR_NATIVE_RX_MESSAGE_CONFLICT:
  case WORR_NATIVE_RX_DUPLICATE_CONFLICT:
  case WORR_NATIVE_RX_MESSAGE_CHECKSUM:
    return WORR_NATIVE_EVENT_ADMISSION_MALFORMED;
  default:
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE;
  }
}

worr_native_event_admission_result_v1
Worr_NativeEventAdmissionCommitCompletedV1(
    worr_event_stream_owner_v1 *owner,
    const worr_native_session_binding_v1 *binding,
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, const void *payload_arena,
    size_t payload_arena_bytes, worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_rx_message_v1 *message,
    const worr_native_event_consumer_v1 *consumer) {
  worr_event_stream_owner_v1 staged_owner;
  worr_native_rx_session_v1 staged_session;
  worr_native_rx_slot_v1 staged_slots[WORR_NATIVE_SESSION_MAX_RX_SLOTS];
  worr_native_carrier_ack_ledger_v1 staged_ledger;
  worr_native_codec_info_v1 info;
  worr_native_record_ref_v1 codec_ref;
  worr_event_stream_descriptor_v1 descriptor;
  worr_event_record_v1 events[WORR_NATIVE_EVENT_BATCH_MAX_EVENTS];
  worr_native_event_batch_info_v1 batch_info;
  worr_cgame_event_runtime_status_v1 status;
  worr_event_stream_owner_result_v1 owner_result;
  worr_cgame_event_runtime_result_v1 consumer_result;
  const uint8_t *payload;
  uint32_t event_count = 0;
  uint32_t event_index;
  bool event_batch = false;
  const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);

  if (owner == NULL || binding == NULL || session == NULL || slots == NULL ||
      payload_arena == NULL || ack_ledger == NULL || message == NULL ||
      consumer == NULL || slot_capacity == 0 ||
      slot_capacity > WORR_NATIVE_SESSION_MAX_RX_SLOTS ||
      payload_arena_bytes == 0 ||
      !state_ranges_valid(owner, binding, session, slots, slots_bytes,
                          payload_arena, payload_arena_bytes, ack_ledger,
                          message, consumer)) {
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_ARGUMENT;
  }
  if (!consumer_valid(consumer) || !Worr_EventStreamOwnerValidateV1(owner) ||
      !Worr_NativeSessionBindingValidateV1(binding) ||
      !Worr_NativeRxSessionValidateV1(session, slots, slot_capacity) ||
      !Worr_NativeCarrierAckLedgerValidateV1(ack_ledger) ||
      binding->connection_owner_id != owner->connection_owner_id ||
      binding->connection_owner_id != session->connection_owner_id ||
      binding->transport_epoch != session->transport_epoch ||
      session->connection_owner_id != owner->connection_owner_id ||
      ack_ledger->connection_owner_id != owner->connection_owner_id ||
      ack_ledger->transport_epoch != session->transport_epoch ||
      session->payload_stride <
          WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
              WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES ||
      payload_arena_bytes != (size_t)slot_capacity * session->payload_stride ||
      !message_matches_complete_slot(session, slots, slot_capacity,
                                     payload_arena_bytes, message)) {
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE;
  }
  if ((binding->negotiated_capabilities &
       (WORR_NET_CAP_NATIVE_ENVELOPE_V1 |
        WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |
        WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1)) !=
      (WORR_NET_CAP_NATIVE_ENVELOPE_V1 |
       WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |
       WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1)) {
    return WORR_NATIVE_EVENT_ADMISSION_NOT_NEGOTIATED;
  }
  if ((owner->state_flags & WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED) !=
      0) {
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE;
  }

  payload = (const uint8_t *)payload_arena + message->payload_offset;
  memset(&info, 0, sizeof(info));
  memset(&codec_ref, 0, sizeof(codec_ref));
  memset(&batch_info, 0, sizeof(batch_info));
  if (message->record.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
      message->record.record_schema_version ==
          WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA) {
    const worr_native_event_batch_result_v1 inspected =
        Worr_NativeEventBatchInspectV1(
            payload, message->payload_bytes, &batch_info);
    if (inspected != WORR_NATIVE_EVENT_BATCH_OK)
      return inspected == WORR_NATIVE_EVENT_BATCH_UNSUPPORTED
                 ? WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED
                 : WORR_NATIVE_EVENT_ADMISSION_MALFORMED;
    codec_ref.record_class = WORR_NATIVE_RECORD_EVENT_V1;
    codec_ref.record_schema_version =
        WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA;
    codec_ref.object_epoch = batch_info.stream_epoch;
    codec_ref.object_sequence = batch_info.last_sequence;
    event_batch = true;
  } else {
    if (Worr_NativeCodecInspectV1(payload, message->payload_bytes, &info) !=
            WORR_NATIVE_CODEC_OK ||
        !Worr_NativeCodecInfoRecordRefV1(&info, &codec_ref)) {
      return WORR_NATIVE_EVENT_ADMISSION_MALFORMED;
    }
    if (info.record_class != WORR_NATIVE_RECORD_EVENT_V1 &&
        info.record_class !=
            WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1) {
      return WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED;
    }
  }
  if (!record_ref_equal(codec_ref, message->record))
    return WORR_NATIVE_EVENT_ADMISSION_MALFORMED;

  staged_owner = *owner;
  staged_session = *session;
  memcpy(staged_slots, slots, slots_bytes);
  staged_ledger = *ack_ledger;

  if (codec_ref.record_class ==
      WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1) {
    if (Worr_NativeCodecEventStreamDecodeV1(payload, message->payload_bytes,
                                            &descriptor) !=
        WORR_NATIVE_CODEC_OK) {
      return WORR_NATIVE_EVENT_ADMISSION_MALFORMED;
    }
    owner_result = Worr_EventStreamOwnerObserveV1(&staged_owner, &descriptor);
    if (owner_result == WORR_EVENT_STREAM_OWNER_WRONG_EPOCH)
      return WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH;
    if (owner_result == WORR_EVENT_STREAM_OWNER_CONFLICT)
      return WORR_NATIVE_EVENT_ADMISSION_CONFLICT;
    if (owner_result != WORR_EVENT_STREAM_OWNER_ACTIVATED &&
        owner_result != WORR_EVENT_STREAM_OWNER_EXACT_DUPLICATE) {
      return owner_result == WORR_EVENT_STREAM_OWNER_INVALID_DESCRIPTOR
                 ? WORR_NATIVE_EVENT_ADMISSION_MALFORMED
                 : WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE;
    }
    if (Worr_NativeCarrierSessionCommitRetainedInternalV1(
            &staged_session, staged_slots, slot_capacity, message->slot_index,
            message->message_sequence,
            &staged_ledger) != WORR_NATIVE_RX_COMMITTED) {
      return WORR_NATIVE_EVENT_ADMISSION_RETRY_UNCOMMITTED;
    }

    memset(&status, 0, sizeof(status));
    if (owner_result == WORR_EVENT_STREAM_OWNER_ACTIVATED) {
      consumer_result = consumer->ResetAuthority(
          consumer->opaque, descriptor.stream_epoch, descriptor.first_sequence);
      if (consumer_result != WORR_CGAME_EVENT_RUNTIME_OK ||
          !consumer->GetStatus(consumer->opaque, &status) ||
          !fresh_reset_status(&status, &descriptor)) {
        force_consumer_resync(owner, &staged_owner, ack_ledger, consumer);
        return WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED;
      }
    } else if (!consumer->GetStatus(consumer->opaque, &status) ||
               !active_status(&status, &descriptor)) {
      force_consumer_resync(owner, &staged_owner, ack_ledger, consumer);
      return WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED;
    }

    *owner = staged_owner;
    *session = staged_session;
    memcpy(slots, staged_slots, slots_bytes);
    *ack_ledger = staged_ledger;
    return owner_result == WORR_EVENT_STREAM_OWNER_ACTIVATED
               ? WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED
               : WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_DUPLICATE;
  }

  if ((owner->state_flags & WORR_EVENT_STREAM_OWNER_ACTIVE) == 0)
    return WORR_NATIVE_EVENT_ADMISSION_NOT_READY;
  if (event_batch &&
      (owner->descriptor.flags &
       WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2) == 0)
    return WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED;
  if (event_batch) {
    if (Worr_NativeEventBatchDecodeV1(
            payload, message->payload_bytes,
            WORR_EVENT_STREAM_MAX_ENTITIES_V1, events,
            WORR_NATIVE_EVENT_BATCH_MAX_EVENTS, &batch_info) !=
        WORR_NATIVE_EVENT_BATCH_OK) {
      return WORR_NATIVE_EVENT_ADMISSION_MALFORMED;
    }
    event_count = batch_info.event_count;
  } else {
    if (codec_ref.record_schema_version != WORR_EVENT_ABI_VERSION)
      return WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED;
    if (Worr_NativeCodecEventDecodeV1(
            payload, message->payload_bytes,
            WORR_EVENT_STREAM_MAX_ENTITIES_V1, &events[0]) !=
        WORR_NATIVE_CODEC_OK) {
      return WORR_NATIVE_EVENT_ADMISSION_MALFORMED;
    }
    event_count = 1;
  }
  if (events[0].event_id.stream_epoch != owner->descriptor.stream_epoch ||
      events[0].event_id.sequence < owner->descriptor.first_sequence) {
    return WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH;
  }
  if (Worr_NativeCarrierSessionCommitRetainedInternalV1(
          &staged_session, staged_slots, slot_capacity, message->slot_index,
          message->message_sequence,
          &staged_ledger) != WORR_NATIVE_RX_COMMITTED) {
    return WORR_NATIVE_EVENT_ADMISSION_RETRY_UNCOMMITTED;
  }
  if (event_batch &&
      !event_batch_receipt_install(
          &staged_ledger, message->message_sequence,
          events[0].event_id.sequence))
    return WORR_NATIVE_EVENT_ADMISSION_RETRY_UNCOMMITTED;

  memset(&status, 0, sizeof(status));
  if (!consumer->GetStatus(consumer->opaque, &status) ||
      !active_status(&status, &owner->descriptor)) {
    force_consumer_resync(owner, owner, ack_ledger, consumer);
    return WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED;
  }
  consumer_result = consumer->SubmitAuthoritativeBatch(
      consumer->opaque, events, event_count);
  memset(&status, 0, sizeof(status));
  if (!submit_result_accepted(consumer_result) ||
      !consumer->GetStatus(consumer->opaque, &status) ||
      !active_status(&status, &owner->descriptor)) {
    force_consumer_resync(owner, owner, ack_ledger, consumer);
    return WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED;
  }
  for (event_index = 0; event_index < event_count; ++event_index) {
    if (!Worr_EventReceiptContainsV1(
            &status.receipt, events[event_index].event_id)) {
      force_consumer_resync(owner, owner, ack_ledger, consumer);
      return WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED;
    }
  }

  *session = staged_session;
  memcpy(slots, staged_slots, slots_bytes);
  *ack_ledger = staged_ledger;
  if (consumer_result == WORR_CGAME_EVENT_RUNTIME_DUPLICATE)
    return WORR_NATIVE_EVENT_ADMISSION_EVENT_DUPLICATE;
  if (consumer_result == WORR_CGAME_EVENT_RUNTIME_DEGRADED ||
      (status.state_flags & WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED) != 0) {
    return WORR_NATIVE_EVENT_ADMISSION_EVENT_DEGRADED;
  }
  return WORR_NATIVE_EVENT_ADMISSION_EVENT_ACCEPTED;
}

worr_native_event_admission_result_v1
Worr_NativeEventAdmissionRevalidateCommittedRepeatV1(
    worr_event_stream_owner_v1 *owner,
    const worr_native_session_binding_v1 *binding,
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, void *payload_arena,
    size_t payload_arena_bytes, uint64_t now_tick, const void *packet,
    size_t packet_bytes, uint16_t entry_index,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_event_consumer_v1 *consumer) {
  worr_native_rx_session_v1 staged_session;
  worr_native_rx_slot_v1 staged_slots[WORR_NATIVE_SESSION_MAX_RX_SLOTS];
  worr_native_carrier_ack_ledger_v1 staged_ledger;
  worr_native_carrier_view_v1 view;
  worr_native_envelope_frame_info_v1 frame;
  worr_native_rx_message_v1 message;
  worr_native_ack_range_v1 repeat_acknowledgement;
  worr_cgame_event_runtime_status_v1 status;
  worr_native_carrier_result_v1 decoded;
  worr_native_envelope_decode_result_v1 envelope_decoded;
  worr_native_carrier_session_result_v1 accepted;
  worr_native_rx_result_v1 rx_result;
  worr_native_event_admission_result_v1 semantic_result;
  worr_event_id_v1 event_id;
  uint32_t event_first_sequence = 0;
  uint32_t event_sequence;
  const worr_native_carrier_entry_v1 *entry;
  const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);

  if (owner == NULL || binding == NULL || session == NULL || slots == NULL ||
      payload_arena == NULL || packet == NULL || packet_bytes == 0 ||
      ack_ledger == NULL || consumer == NULL || slot_capacity == 0 ||
      slot_capacity > WORR_NATIVE_SESSION_MAX_RX_SLOTS ||
      payload_arena_bytes == 0 ||
      !repeat_ranges_valid(owner, binding, session, slots, slots_bytes,
                           payload_arena, payload_arena_bytes, packet,
                           packet_bytes, ack_ledger, consumer)) {
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_ARGUMENT;
  }
  if (!consumer_valid(consumer) || !Worr_EventStreamOwnerValidateV1(owner) ||
      !Worr_NativeSessionBindingValidateV1(binding) ||
      !Worr_NativeRxSessionValidateV1(session, slots, slot_capacity) ||
      !Worr_NativeCarrierAckLedgerValidateV1(ack_ledger) ||
      binding->connection_owner_id != owner->connection_owner_id ||
      binding->connection_owner_id != session->connection_owner_id ||
      binding->transport_epoch != session->transport_epoch ||
      session->connection_owner_id != owner->connection_owner_id ||
      ack_ledger->connection_owner_id != owner->connection_owner_id ||
      ack_ledger->transport_epoch != session->transport_epoch ||
      payload_arena_bytes !=
          (size_t)slot_capacity * session->payload_stride) {
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE;
  }
  if ((binding->negotiated_capabilities &
       (WORR_NET_CAP_NATIVE_ENVELOPE_V1 |
        WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |
        WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1)) !=
      (WORR_NET_CAP_NATIVE_ENVELOPE_V1 |
       WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |
       WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1)) {
    return WORR_NATIVE_EVENT_ADMISSION_NOT_NEGOTIATED;
  }
  if ((owner->state_flags & WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED) !=
      0) {
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE;
  }
  if ((owner->state_flags & WORR_EVENT_STREAM_OWNER_ACTIVE) == 0)
    return WORR_NATIVE_EVENT_ADMISSION_NOT_READY;
  if ((ack_ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) !=
          0 ||
      ack_ledger->mutation_generation == UINT64_MAX) {
    return WORR_NATIVE_EVENT_ADMISSION_RETRY_UNCOMMITTED;
  }

  decoded = Worr_NativeCarrierDecodeV1(packet, packet_bytes, &view);
  if (decoded != WORR_NATIVE_CARRIER_OK) {
    return decoded == WORR_NATIVE_CARRIER_UNSUPPORTED
               ? WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED
               : WORR_NATIVE_EVENT_ADMISSION_MALFORMED;
  }
  if (view.transport_epoch != binding->transport_epoch)
    return WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH;
  if (entry_index >= view.entry_count)
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_ARGUMENT;
  entry = &view.entries[entry_index];
  if (entry->entry_type != WORR_NATIVE_CARRIER_ENTRY_DATA_V1)
    return WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED;
  envelope_decoded = Worr_NativeEnvelopeDecodeV1(
      (const uint8_t *)packet + entry->data_offset, entry->data_bytes, &frame);
  if (envelope_decoded != WORR_NATIVE_ENVELOPE_DECODE_OK) {
    return envelope_decoded == WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED
               ? WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED
               : WORR_NATIVE_EVENT_ADMISSION_MALFORMED;
  }
  if (frame.transport_epoch != binding->transport_epoch)
    return WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH;
  semantic_result = repeat_record_owner_result(owner, frame.record);
  if (semantic_result !=
          WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_REPEAT_REVALIDATED &&
      semantic_result != WORR_NATIVE_EVENT_ADMISSION_EVENT_REPEAT_REVALIDATED) {
    return semantic_result;
  }
  if (!committed_history_matches_frame(session, &frame))
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE;

  staged_session = *session;
  memcpy(staged_slots, slots, slots_bytes);
  staged_ledger = *ack_ledger;
  if (frame.record.record_class == WORR_NATIVE_RECORD_EVENT_V1) {
    event_first_sequence = frame.record.object_sequence;
    if (frame.record.record_schema_version ==
        WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA) {
      const worr_native_carrier_ack_receipt_v1 *receipt =
          event_receipt_find(&staged_ledger, frame.message_sequence);
      if (receipt == NULL || receipt->semantic_first_sequence == 0 ||
          receipt->semantic_first_sequence > frame.record.object_sequence ||
          frame.record.object_sequence - receipt->semantic_first_sequence >=
              WORR_NATIVE_EVENT_BATCH_MAX_EVENTS)
        return WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE;
      event_first_sequence = receipt->semantic_first_sequence;
    }
  }
  memset(&message, 0, sizeof(message));
  memset(&repeat_acknowledgement, 0, sizeof(repeat_acknowledgement));
  accepted = Worr_NativeCarrierSessionAcceptDataV1(
      &staged_session, staged_slots, slot_capacity, payload_arena,
      payload_arena_bytes, now_tick, packet, packet_bytes, entry_index,
      &rx_result, &message, &repeat_acknowledgement);
  if (accepted != WORR_NATIVE_CARRIER_SESSION_OK)
    return carrier_repeat_failure(accepted);
  if (rx_result != WORR_NATIVE_RX_ALREADY_COMMITTED)
    return rx_repeat_failure(rx_result);
  if (!Worr_NativeAckRangeValidateV1(&repeat_acknowledgement) ||
      repeat_acknowledgement.transport_epoch != session->transport_epoch ||
      repeat_acknowledgement.connection_owner_id !=
          session->connection_owner_id ||
      repeat_acknowledgement.first_message_sequence != frame.message_sequence ||
      repeat_acknowledgement.last_message_sequence != frame.message_sequence ||
      Worr_NativeCarrierAckRefreshObservedRepeatInternalV1(
          &staged_ledger, &staged_session, &repeat_acknowledgement) !=
          WORR_NATIVE_CARRIER_ACK_OK) {
    return WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE;
  }

  memset(&status, 0, sizeof(status));
  event_id.stream_epoch = frame.record.object_epoch;
  event_id.sequence = event_first_sequence;
  if (!consumer->GetStatus(consumer->opaque, &status) ||
      !active_status(&status, &owner->descriptor)) {
    force_consumer_resync(owner, owner, ack_ledger, consumer);
    return WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED;
  }
  if (frame.record.record_class == WORR_NATIVE_RECORD_EVENT_V1) {
    for (event_sequence = event_first_sequence;
         event_sequence <= frame.record.object_sequence;
         ++event_sequence) {
      event_id.sequence = event_sequence;
      if (!Worr_EventReceiptContainsV1(&status.receipt, event_id)) {
        force_consumer_resync(owner, owner, ack_ledger, consumer);
        return WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED;
      }
      if (event_sequence == UINT32_MAX)
        break;
    }
  }

  *session = staged_session;
  memcpy(slots, staged_slots, slots_bytes);
  *ack_ledger = staged_ledger;
  return semantic_result;
}
