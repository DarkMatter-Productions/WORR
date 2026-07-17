/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_carrier_ack.h"

#include "native_carrier_ack_internal.h"

#include <limits.h>
#include <string.h>

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes) {
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

static void counter_add(uint64_t *counter, uint64_t amount) {
  if (UINT64_MAX - *counter < amount)
    *counter = UINT64_MAX;
  else
    *counter += amount;
}

static uint32_t crc32_bytes(const void *data, size_t bytes) {
  const uint8_t *cursor = (const uint8_t *)data;
  uint32_t crc = UINT32_MAX;
  size_t index;

  for (index = 0; index < bytes; ++index) {
    uint32_t value = crc ^ cursor[index];
    uint32_t bit;

    for (bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (value & 1u);
      value = (value >> 1) ^ (UINT32_C(0xedb88320) & mask);
    }
    crc = value;
  }
  return ~crc;
}

static bool token_validate(const worr_native_carrier_ack_emit_token_v1 *token);

static bool bytes_are_zero(const void *data, size_t bytes) {
  const uint8_t *cursor = (const uint8_t *)data;
  size_t index;

  for (index = 0; index < bytes; ++index) {
    if (cursor[index] != 0)
      return false;
  }
  return true;
}

static bool record_class_valid(uint8_t record_class) {
  return record_class == WORR_NATIVE_RECORD_COMMAND_V1 ||
         record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
         record_class == WORR_NATIVE_RECORD_EVENT_V1 ||
         record_class == WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1;
}

static bool
receipt_occupied(const worr_native_carrier_ack_receipt_v1 *receipt) {
  return (receipt->state_flags & WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED) != 0;
}

static bool receipt_valid(const worr_native_carrier_ack_ledger_v1 *ledger,
                          const worr_native_carrier_ack_receipt_v1 *receipt) {
  const uint8_t known_flags = WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED |
                              WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE;

  if (!receipt_occupied(receipt)) {
    const worr_native_carrier_ack_receipt_v1 zero = {0};
    return memcmp(receipt, &zero, sizeof(zero)) == 0;
  }
  if ((receipt->state_flags & ~known_flags) != 0 ||
      receipt->message_sequence == 0 ||
      !record_class_valid(receipt->record_class) || receipt->reserved0 != 0 ||
      receipt->handoffs_remaining > ledger->proactive_handoffs) {
    return false;
  }
  if ((receipt->state_flags & WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE) != 0 &&
      receipt->handoffs_remaining == 0) {
    return false;
  }
  if (receipt->handoff_attempts == 0)
    return receipt->last_handoff_tick == 0 && receipt->handoffs_remaining != 0;
  return receipt->last_handoff_tick <= ledger->last_handoff_tick;
}

bool Worr_NativeCarrierAckLedgerValidateV1(
    const worr_native_carrier_ack_ledger_v1 *ledger) {
  const uint16_t known_flags = WORR_NATIVE_CARRIER_ACK_LEDGER_INITIALIZED |
                               WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE |
                               WORR_NATIVE_CARRIER_ACK_LEDGER_TOKEN_EXHAUSTED |
                               WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED;
  const bool emit_active =
      ledger != NULL &&
      (ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0;
  uint16_t count = 0;
  uint16_t index;
  uint16_t other;

  if (ledger == NULL || ledger->struct_size != sizeof(*ledger) ||
      ledger->schema_version != WORR_NATIVE_CARRIER_ACK_ABI_VERSION ||
      (ledger->state_flags & ~known_flags) != 0 ||
      (ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_INITIALIZED) == 0 ||
      ledger->transport_epoch == 0 || ledger->connection_owner_id == 0 ||
      ledger->receipt_count > WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY ||
      ledger->proactive_handoffs == 0 ||
      ledger->proactive_handoffs >
          WORR_NATIVE_CARRIER_ACK_MAX_PROACTIVE_HANDOFFS ||
      ledger->reserved0 != 0 || ledger->mutation_generation == 0 ||
      ledger->next_token_id == 0 ||
      ((ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_TOKEN_EXHAUSTED) !=
           0 &&
       ledger->next_token_id != UINT64_MAX) ||
      ((ledger->state_flags &
        WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED) != 0 &&
       (emit_active || ledger->receipt_count != 0))) {
    return false;
  }
  if (!emit_active) {
    if (!bytes_are_zero(&ledger->active_token, sizeof(ledger->active_token))) {
      return false;
    }
  } else if (!token_validate(&ledger->active_token) ||
             ledger->active_token.transport_epoch != ledger->transport_epoch ||
             ledger->active_token.connection_owner_id !=
                 ledger->connection_owner_id ||
             ledger->active_token.ledger_generation !=
                 ledger->mutation_generation ||
             (((ledger->state_flags &
                WORR_NATIVE_CARRIER_ACK_LEDGER_TOKEN_EXHAUSTED) != 0)
                  ? ledger->active_token.token_id != UINT64_MAX
                  : ledger->active_token.token_id >= ledger->next_token_id)) {
    return false;
  }
  for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++index) {
    if (!receipt_valid(ledger, &ledger->receipts[index]))
      return false;
    if (!receipt_occupied(&ledger->receipts[index]))
      continue;
    ++count;
    for (other = (uint16_t)(index + 1);
         other < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++other) {
      if (receipt_occupied(&ledger->receipts[other]) &&
          ledger->receipts[index].message_sequence ==
              ledger->receipts[other].message_sequence) {
        return false;
      }
    }
  }
  return count == ledger->receipt_count;
}

bool Worr_NativeCarrierAckLedgerInitV1(
    worr_native_carrier_ack_ledger_v1 *ledger_out,
    const worr_native_session_binding_v1 *binding, uint8_t proactive_handoffs) {
  worr_native_carrier_ack_ledger_v1 initialized;

  if (ledger_out == NULL || binding == NULL || proactive_handoffs == 0 ||
      proactive_handoffs > WORR_NATIVE_CARRIER_ACK_MAX_PROACTIVE_HANDOFFS ||
      !Worr_NativeSessionBindingValidateV1(binding) ||
      ranges_overlap(ledger_out, sizeof(*ledger_out), binding,
                     sizeof(*binding))) {
    return false;
  }
  memset(&initialized, 0, sizeof(initialized));
  initialized.struct_size = sizeof(initialized);
  initialized.schema_version = WORR_NATIVE_CARRIER_ACK_ABI_VERSION;
  initialized.state_flags = WORR_NATIVE_CARRIER_ACK_LEDGER_INITIALIZED;
  initialized.transport_epoch = binding->transport_epoch;
  initialized.connection_owner_id = binding->connection_owner_id;
  initialized.proactive_handoffs = proactive_handoffs;
  initialized.mutation_generation = 1;
  initialized.next_token_id = 1;
  *ledger_out = initialized;
  return true;
}

bool Worr_NativeCarrierAckLedgerAdvanceEpochV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    const worr_native_session_binding_v1 *binding) {
  const uint8_t proactive_handoffs =
      ledger == NULL ? 0 : ledger->proactive_handoffs;

  if (ledger == NULL || binding == NULL ||
      !Worr_NativeCarrierAckLedgerValidateV1(ledger) ||
      !Worr_NativeSessionBindingValidateV1(binding) ||
      binding->connection_owner_id != ledger->connection_owner_id ||
      binding->transport_epoch <= ledger->transport_epoch ||
      ranges_overlap(ledger, sizeof(*ledger), binding, sizeof(*binding))) {
    return false;
  }
  return Worr_NativeCarrierAckLedgerInitV1(ledger, binding, proactive_handoffs);
}

worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckCancelAllV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    uint32_t *cancelled_count_out) {
  worr_native_carrier_ack_ledger_v1 staged;
  uint32_t cancelled;

  if (ledger == NULL || cancelled_count_out == NULL ||
      ranges_overlap(ledger, sizeof(*ledger), cancelled_count_out,
                     sizeof(*cancelled_count_out))) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT;
  }
  if (!Worr_NativeCarrierAckLedgerValidateV1(ledger))
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  if ((ledger->state_flags &
       WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED) != 0) {
    *cancelled_count_out = 0;
    return WORR_NATIVE_CARRIER_ACK_OK;
  }
  if ((ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0)
    return WORR_NATIVE_CARRIER_ACK_EMIT_ACTIVE;

  cancelled = ledger->receipt_count;
  staged = *ledger;
  memset(staged.receipts, 0, sizeof(staged.receipts));
  staged.receipt_count = 0;
  staged.state_flags |=
      WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED;
  if (staged.mutation_generation != UINT64_MAX)
    ++staged.mutation_generation;
  if (!Worr_NativeCarrierAckLedgerValidateV1(&staged))
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  *ledger = staged;
  *cancelled_count_out = cancelled;
  return WORR_NATIVE_CARRIER_ACK_OK;
}

static int receipt_find(const worr_native_carrier_ack_ledger_v1 *ledger,
                        uint32_t message_sequence) {
  uint16_t index;

  for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++index) {
    if (receipt_occupied(&ledger->receipts[index]) &&
        ledger->receipts[index].message_sequence == message_sequence) {
      return (int)index;
    }
  }
  return -1;
}

static bool
rx_authority_find(const worr_native_rx_session_v1 *session,
                  uint32_t message_sequence, uint8_t *record_class_out) {
  uint16_t index;

  for (index = 0; index < WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY;
       ++index) {
    if (session->history[index].message_sequence == message_sequence) {
      if (record_class_out != NULL) {
        *record_class_out = session->history[index].record.record_class;
      }
      return true;
    }
  }
  for (index = 0; index < WORR_NATIVE_SESSION_SNAPSHOT_TOMBSTONE_CAPACITY;
       ++index) {
    if (session->snapshot_tombstones[index].message_sequence ==
            message_sequence &&
        (session->snapshot_tombstones[index].state_flags &
         WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED) != 0) {
      if (record_class_out != NULL)
        *record_class_out =
            session->snapshot_tombstones[index].record.record_class;
      return true;
    }
  }
  return false;
}

static bool receipt_insert(worr_native_carrier_ack_ledger_v1 *ledger,
                           uint32_t message_sequence, uint8_t record_class) {
  uint16_t index;

  if (receipt_find(ledger, message_sequence) >= 0)
    return true;
  for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++index) {
    worr_native_carrier_ack_receipt_v1 receipt;

    if (receipt_occupied(&ledger->receipts[index]))
      continue;
    memset(&receipt, 0, sizeof(receipt));
    receipt.message_sequence = message_sequence;
    receipt.handoffs_remaining = ledger->proactive_handoffs;
    receipt.record_class = record_class;
    receipt.state_flags = WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED |
                          WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE;
    ledger->receipts[index] = receipt;
    ++ledger->receipt_count;
    counter_add(&ledger->telemetry.reconciled_receipts, 1);
    return true;
  }
  return false;
}

/* Remove receipts whose bounded RX authority has expired. */
static bool ledger_prune(worr_native_carrier_ack_ledger_v1 *ledger,
                         const worr_native_rx_session_v1 *session) {
  uint16_t index;

  if (ledger->transport_epoch != session->transport_epoch ||
      ledger->connection_owner_id != session->connection_owner_id) {
    return false;
  }

  for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++index) {
    uint8_t record_class = 0;

    if (!receipt_occupied(&ledger->receipts[index]))
      continue;
    if (rx_authority_find(session, ledger->receipts[index].message_sequence,
                          &record_class) &&
        record_class == ledger->receipts[index].record_class) {
      continue;
    }
    memset(&ledger->receipts[index], 0, sizeof(ledger->receipts[index]));
    --ledger->receipt_count;
    counter_add(&ledger->telemetry.receipts_retired, 1);
  }
  return true;
}

static bool receipt_refresh(worr_native_carrier_ack_ledger_v1 *ledger,
                            uint32_t message_sequence) {
  const int index = receipt_find(ledger, message_sequence);

  if (index < 0)
    return false;
  ledger->receipts[index].handoffs_remaining = ledger->proactive_handoffs;
  ledger->receipts[index].state_flags |=
      WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE;
  return true;
}

/*
 * Capture only the identity proven by the current commit/repeat operation.
 * Importing every retained RX identity here would let an unrelated command
 * repeat silently reauthorize a stale event ACK after cgame lost authority.
 */
static bool receipt_capture_exact(worr_native_carrier_ack_ledger_v1 *ledger,
                                  const worr_native_rx_session_v1 *session,
                                  uint32_t message_sequence) {
  uint8_t record_class = 0;

  return rx_authority_find(session, message_sequence, &record_class) &&
         ledger_prune(ledger, session) &&
         receipt_insert(ledger, message_sequence, record_class) &&
         receipt_refresh(ledger, message_sequence);
}

static worr_native_rx_result_v1 carrier_session_commit_retained(
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, uint32_t slot_index, uint32_t message_sequence,
    worr_native_carrier_ack_ledger_v1 *ledger,
    uint8_t required_semantic_class) {
  worr_native_rx_session_v1 staged_session;
  worr_native_rx_slot_v1 staged_slots[WORR_NATIVE_SESSION_MAX_RX_SLOTS];
  worr_native_carrier_ack_ledger_v1 staged_ledger;
  worr_native_ack_range_v1 acknowledgement;
  worr_native_rx_result_v1 committed;
  const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
  uint8_t record_class = 0;
  bool semantic_slot = false;

  if (session == NULL || slots == NULL || ledger == NULL ||
      slot_capacity == 0 || slot_capacity > WORR_NATIVE_SESSION_MAX_RX_SLOTS ||
      ranges_overlap(session, sizeof(*session), slots, slots_bytes) ||
      ranges_overlap(ledger, sizeof(*ledger), session, sizeof(*session)) ||
      ranges_overlap(ledger, sizeof(*ledger), slots, slots_bytes)) {
    return WORR_NATIVE_RX_INVALID_ARGUMENT;
  }
  if (!Worr_NativeRxSessionValidateV1(session, slots, slot_capacity) ||
      !Worr_NativeCarrierAckLedgerValidateV1(ledger) ||
      (session->state_flags & WORR_NATIVE_RX_TERMINAL_CANCELLED) != 0 ||
      (ledger->state_flags &
       WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED) != 0) {
    return WORR_NATIVE_RX_INVALID_STATE;
  }
  if (ledger->transport_epoch != session->transport_epoch)
    return WORR_NATIVE_RX_WRONG_EPOCH;
  if (ledger->connection_owner_id != session->connection_owner_id ||
      (ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0 ||
      ledger->mutation_generation == UINT64_MAX) {
    return WORR_NATIVE_RX_INVALID_STATE;
  }
  if (slot_index < slot_capacity &&
      slots[slot_index].reassembly.message_sequence == message_sequence &&
      (slots[slot_index].state_flags & WORR_NATIVE_RX_SLOT_COMPLETE) != 0) {
    record_class =
        slots[slot_index].reassembly.record.record_class;
    semantic_slot =
        record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
        record_class == WORR_NATIVE_RECORD_EVENT_V1 ||
        record_class == WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1;
  }
  if (required_semantic_class == 0 && semantic_slot) {
    return WORR_NATIVE_RX_SEMANTIC_ADMISSION_REQUIRED;
  }
  if (required_semantic_class == WORR_NATIVE_RECORD_EVENT_V1 &&
      record_class != WORR_NATIVE_RECORD_EVENT_V1 &&
      record_class != WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1) {
    return WORR_NATIVE_RX_INVALID_STATE;
  }
  if (required_semantic_class == WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
      record_class != WORR_NATIVE_RECORD_SNAPSHOT_V1) {
    return WORR_NATIVE_RX_INVALID_STATE;
  }
  if (required_semantic_class != 0 &&
      required_semantic_class != WORR_NATIVE_RECORD_EVENT_V1 &&
      required_semantic_class != WORR_NATIVE_RECORD_SNAPSHOT_V1) {
    return WORR_NATIVE_RX_INVALID_STATE;
  }

  staged_session = *session;
  memcpy(staged_slots, slots, slots_bytes);
  staged_ledger = *ledger;
  committed = Worr_NativeRxSessionCommitV1(&staged_session, staged_slots,
                                           slot_capacity, slot_index,
                                           message_sequence, &acknowledgement);
  if (committed != WORR_NATIVE_RX_COMMITTED)
    return committed;
  if (!Worr_NativeAckRangeValidateV1(&acknowledgement) ||
      acknowledgement.transport_epoch != staged_ledger.transport_epoch ||
      acknowledgement.connection_owner_id !=
          staged_ledger.connection_owner_id ||
      acknowledgement.first_message_sequence != message_sequence ||
      acknowledgement.last_message_sequence != message_sequence ||
      !receipt_capture_exact(&staged_ledger, &staged_session,
                             message_sequence)) {
    return WORR_NATIVE_RX_INVALID_STATE;
  }
  counter_add(&staged_ledger.telemetry.commit_captures, 1);
  ++staged_ledger.mutation_generation;
  *session = staged_session;
  memcpy(slots, staged_slots, slots_bytes);
  *ledger = staged_ledger;
  return WORR_NATIVE_RX_COMMITTED;
}

worr_native_rx_result_v1 Worr_NativeCarrierSessionCommitRetainedV1(
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, uint32_t slot_index, uint32_t message_sequence,
    worr_native_carrier_ack_ledger_v1 *ledger) {
  return carrier_session_commit_retained(
      session, slots, slot_capacity, slot_index, message_sequence, ledger,
      0);
}

worr_native_rx_result_v1 Worr_NativeCarrierSessionCommitRetainedInternalV1(
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, uint32_t slot_index, uint32_t message_sequence,
    worr_native_carrier_ack_ledger_v1 *ledger) {
  return carrier_session_commit_retained(
      session, slots, slot_capacity, slot_index, message_sequence, ledger,
      WORR_NATIVE_RECORD_EVENT_V1);
}

worr_native_rx_result_v1 Worr_NativeCarrierSessionCommitSnapshotInternalV1(
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, uint32_t slot_index, uint32_t message_sequence,
    worr_native_carrier_ack_ledger_v1 *ledger) {
  return carrier_session_commit_retained(
      session, slots, slot_capacity, slot_index, message_sequence, ledger,
      WORR_NATIVE_RECORD_SNAPSHOT_V1);
}

static bool
repeat_capture(worr_native_carrier_ack_ledger_v1 *ledger,
               const worr_native_rx_session_v1 *session,
               const worr_native_ack_range_v1 *repeat_acknowledgement) {
  return Worr_NativeAckRangeValidateV1(repeat_acknowledgement) &&
         repeat_acknowledgement->first_message_sequence ==
             repeat_acknowledgement->last_message_sequence &&
         repeat_acknowledgement->transport_epoch == ledger->transport_epoch &&
         repeat_acknowledgement->connection_owner_id ==
             ledger->connection_owner_id &&
         receipt_capture_exact(
             ledger, session,
             repeat_acknowledgement->first_message_sequence);
}

worr_native_carrier_ack_result_v1
Worr_NativeCarrierAckRefreshObservedRepeatInternalV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    const worr_native_rx_session_v1 *session,
    const worr_native_ack_range_v1 *repeat_acknowledgement) {
  worr_native_carrier_ack_ledger_v1 staged;
  uint8_t record_class = 0;

  if (ledger == NULL || session == NULL || repeat_acknowledgement == NULL ||
      ranges_overlap(ledger, sizeof(*ledger), session, sizeof(*session)) ||
      ranges_overlap(ledger, sizeof(*ledger), repeat_acknowledgement,
                     sizeof(*repeat_acknowledgement)) ||
      ranges_overlap(session, sizeof(*session), repeat_acknowledgement,
                     sizeof(*repeat_acknowledgement))) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT;
  }
  if (!Worr_NativeCarrierAckLedgerValidateV1(ledger) ||
      (ledger->state_flags &
       WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED) != 0 ||
      (session->state_flags & WORR_NATIVE_RX_TERMINAL_CANCELLED) != 0 ||
      (ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0 ||
      ledger->mutation_generation == UINT64_MAX ||
      ledger->transport_epoch != session->transport_epoch ||
      ledger->connection_owner_id != session->connection_owner_id ||
      !Worr_NativeAckRangeValidateV1(repeat_acknowledgement) ||
      repeat_acknowledgement->first_message_sequence !=
          repeat_acknowledgement->last_message_sequence ||
      !rx_authority_find(session,
                         repeat_acknowledgement->first_message_sequence,
                         &record_class) ||
      (record_class != WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
       record_class != WORR_NATIVE_RECORD_EVENT_V1 &&
       record_class != WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1)) {
    return ledger->transport_epoch != session->transport_epoch
               ? WORR_NATIVE_CARRIER_ACK_WRONG_EPOCH
               : WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  }

  staged = *ledger;
  if (!repeat_capture(&staged, session, repeat_acknowledgement)) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  }
  counter_add(&staged.telemetry.repeat_refreshes, 1);
  ++staged.mutation_generation;
  if (!Worr_NativeCarrierAckLedgerValidateV1(&staged))
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  *ledger = staged;
  return WORR_NATIVE_CARRIER_ACK_OK;
}

worr_native_carrier_ack_result_v1
Worr_NativeCarrierAckRetireSemanticReceiptsInternalV1(
    worr_native_carrier_ack_ledger_v1 *ledger) {
  worr_native_carrier_ack_ledger_v1 staged;
  uint64_t retired = 0;
  uint16_t index;

  if (ledger == NULL)
    return WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT;
  if (!Worr_NativeCarrierAckLedgerValidateV1(ledger) ||
      (ledger->state_flags &
       WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED) != 0 ||
      (ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0 ||
      ledger->mutation_generation == UINT64_MAX) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  }

  staged = *ledger;
  for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++index) {
    if (!receipt_occupied(&staged.receipts[index]) ||
        (staged.receipts[index].record_class !=
             WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
         staged.receipts[index].record_class != WORR_NATIVE_RECORD_EVENT_V1 &&
         staged.receipts[index].record_class !=
             WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1)) {
      continue;
    }
    memset(&staged.receipts[index], 0, sizeof(staged.receipts[index]));
    --staged.receipt_count;
    ++retired;
  }
  if (retired == 0)
    return WORR_NATIVE_CARRIER_ACK_OK;
  counter_add(&staged.telemetry.receipts_retired, retired);
  ++staged.mutation_generation;
  if (!Worr_NativeCarrierAckLedgerValidateV1(&staged))
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  *ledger = staged;
  return WORR_NATIVE_CARRIER_ACK_OK;
}

worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionAcceptDataRetainedV1(
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, void *payload_arena, size_t payload_arena_bytes,
    uint64_t now_tick, const void *packet, size_t packet_bytes,
    uint16_t entry_index, worr_native_carrier_ack_ledger_v1 *ledger,
    worr_native_rx_result_v1 *rx_result_out,
    worr_native_rx_message_v1 *message_out) {
  worr_native_rx_session_v1 staged_session;
  worr_native_rx_slot_v1 staged_slots[WORR_NATIVE_SESSION_MAX_RX_SLOTS];
  worr_native_carrier_ack_ledger_v1 staged_ledger;
  worr_native_rx_message_v1 staged_message;
  worr_native_ack_range_v1 repeat_acknowledgement;
  worr_native_rx_result_v1 rx_result;
  worr_native_carrier_session_result_v1 accepted;
  const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);

  if (session == NULL || slots == NULL || payload_arena == NULL ||
      packet == NULL || packet_bytes == 0 || ledger == NULL ||
      rx_result_out == NULL || message_out == NULL || slot_capacity == 0 ||
      slot_capacity > WORR_NATIVE_SESSION_MAX_RX_SLOTS ||
      ranges_overlap(session, sizeof(*session), slots, slots_bytes) ||
      ranges_overlap(session, sizeof(*session), payload_arena,
                     payload_arena_bytes) ||
      ranges_overlap(session, sizeof(*session), packet, packet_bytes) ||
      ranges_overlap(slots, slots_bytes, payload_arena, payload_arena_bytes) ||
      ranges_overlap(slots, slots_bytes, packet, packet_bytes) ||
      ranges_overlap(payload_arena, payload_arena_bytes, packet,
                     packet_bytes) ||
      ranges_overlap(ledger, sizeof(*ledger), session, sizeof(*session)) ||
      ranges_overlap(ledger, sizeof(*ledger), slots, slots_bytes) ||
      ranges_overlap(ledger, sizeof(*ledger), payload_arena,
                     payload_arena_bytes) ||
      ranges_overlap(ledger, sizeof(*ledger), packet, packet_bytes) ||
      ranges_overlap(rx_result_out, sizeof(*rx_result_out), session,
                     sizeof(*session)) ||
      ranges_overlap(rx_result_out, sizeof(*rx_result_out), slots,
                     slots_bytes) ||
      ranges_overlap(rx_result_out, sizeof(*rx_result_out), ledger,
                     sizeof(*ledger)) ||
      ranges_overlap(rx_result_out, sizeof(*rx_result_out), payload_arena,
                     payload_arena_bytes) ||
      ranges_overlap(rx_result_out, sizeof(*rx_result_out), packet,
                     packet_bytes) ||
      ranges_overlap(message_out, sizeof(*message_out), session,
                     sizeof(*session)) ||
      ranges_overlap(message_out, sizeof(*message_out), slots, slots_bytes) ||
      ranges_overlap(message_out, sizeof(*message_out), ledger,
                     sizeof(*ledger)) ||
      ranges_overlap(message_out, sizeof(*message_out), payload_arena,
                     payload_arena_bytes) ||
      ranges_overlap(message_out, sizeof(*message_out), packet, packet_bytes) ||
      ranges_overlap(message_out, sizeof(*message_out), rx_result_out,
                     sizeof(*rx_result_out))) {
    return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
  }
  if (!Worr_NativeRxSessionValidateV1(session, slots, slot_capacity) ||
      !Worr_NativeCarrierAckLedgerValidateV1(ledger) ||
      (session->state_flags & WORR_NATIVE_RX_TERMINAL_CANCELLED) != 0 ||
      (ledger->state_flags &
       WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED) != 0 ||
      (ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0 ||
      ledger->mutation_generation == UINT64_MAX ||
      ledger->connection_owner_id != session->connection_owner_id) {
    return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
  }
  if (ledger->transport_epoch != session->transport_epoch)
    return WORR_NATIVE_CARRIER_SESSION_WRONG_EPOCH;

  staged_session = *session;
  memcpy(staged_slots, slots, slots_bytes);
  staged_ledger = *ledger;
  memset(&staged_message, 0, sizeof(staged_message));
  memset(&repeat_acknowledgement, 0, sizeof(repeat_acknowledgement));
  accepted = Worr_NativeCarrierSessionAcceptDataV1(
      &staged_session, staged_slots, slot_capacity, payload_arena,
      payload_arena_bytes, now_tick, packet, packet_bytes, entry_index,
      &rx_result, &staged_message, &repeat_acknowledgement);
  if (accepted != WORR_NATIVE_CARRIER_SESSION_OK)
    return accepted;
  if (rx_result == WORR_NATIVE_RX_ALREADY_COMMITTED) {
    uint8_t record_class = 0;

    if (!rx_authority_find(
            &staged_session,
            repeat_acknowledgement.first_message_sequence,
            &record_class)) {
      return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }
    if (record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
        record_class == WORR_NATIVE_RECORD_EVENT_V1 ||
        record_class ==
            WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1) {
      return WORR_NATIVE_CARRIER_SESSION_SEMANTIC_REVALIDATION_REQUIRED;
    }
    if (!repeat_capture(&staged_ledger, &staged_session,
                        &repeat_acknowledgement)) {
      return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }
    counter_add(&staged_ledger.telemetry.repeat_refreshes, 1);
    ++staged_ledger.mutation_generation;
  }

  *session = staged_session;
  memcpy(slots, staged_slots, slots_bytes);
  *ledger = staged_ledger;
  *rx_result_out = rx_result;
  if (rx_result == WORR_NATIVE_RX_MESSAGE_COMPLETE)
    *message_out = staged_message;
  return WORR_NATIVE_CARRIER_SESSION_OK;
}

static bool receipt_due(const worr_native_carrier_ack_receipt_v1 *receipt,
                        uint64_t now_tick, uint32_t retry_interval_ticks) {
  if (!receipt_occupied(receipt) || receipt->handoffs_remaining == 0)
    return false;
  if ((receipt->state_flags & WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE) != 0 ||
      receipt->handoff_attempts == 0) {
    return true;
  }
  return now_tick - receipt->last_handoff_tick >= retry_interval_ticks;
}

worr_native_carrier_ack_result_v1
Worr_NativeCarrierAckPeekDueV1(const worr_native_carrier_ack_ledger_v1 *ledger,
                               uint64_t now_tick,
                               uint32_t retry_interval_ticks) {
  uint16_t index;

  if (!Worr_NativeCarrierAckLedgerValidateV1(ledger))
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  if ((ledger->state_flags &
       WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED) != 0)
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  if ((ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0) {
    return WORR_NATIVE_CARRIER_ACK_EMIT_ACTIVE;
  }
  if ((ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_TOKEN_EXHAUSTED) !=
      0) {
    return WORR_NATIVE_CARRIER_ACK_TOKEN_EXHAUSTED;
  }
  if (ledger->mutation_generation > UINT64_MAX - 3u)
    return WORR_NATIVE_CARRIER_ACK_LIMIT;
  if (now_tick < ledger->last_handoff_tick)
    return WORR_NATIVE_CARRIER_ACK_CLOCK_REGRESSION;

  for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++index) {
    if (receipt_due(&ledger->receipts[index], now_tick, retry_interval_ticks)) {
      return WORR_NATIVE_CARRIER_ACK_OK;
    }
  }
  return WORR_NATIVE_CARRIER_ACK_NOT_DUE;
}

static bool receipt_better(const worr_native_carrier_ack_receipt_v1 *candidate,
                           const worr_native_carrier_ack_receipt_v1 *best) {
  const bool candidate_forced =
      (candidate->state_flags & WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE) != 0;
  const bool best_forced =
      (best->state_flags & WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE) != 0;

  if (candidate_forced != best_forced)
    return candidate_forced;
  if ((candidate->handoff_attempts == 0) != (best->handoff_attempts == 0)) {
    return candidate->handoff_attempts == 0;
  }
  if (candidate->last_handoff_tick != best->last_handoff_tick)
    return candidate->last_handoff_tick < best->last_handoff_tick;
  return candidate->message_sequence < best->message_sequence;
}

static int due_receipt_find(const worr_native_carrier_ack_ledger_v1 *ledger,
                            const bool *selected, uint32_t message_sequence,
                            uint64_t now_tick, uint32_t retry_interval_ticks) {
  uint16_t index;

  for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++index) {
    if (!selected[index] &&
        ledger->receipts[index].message_sequence == message_sequence &&
        receipt_due(&ledger->receipts[index], now_tick, retry_interval_ticks)) {
      return (int)index;
    }
  }
  return -1;
}

static void sort_token_ranges(worr_native_carrier_ack_token_range_v1 *ranges,
                              uint16_t count) {
  uint16_t index;

  for (index = 1; index < count; ++index) {
    worr_native_carrier_ack_token_range_v1 value = ranges[index];
    uint16_t position = index;

    while (position > 0 && ranges[position - 1].first_message_sequence >
                               value.first_message_sequence) {
      ranges[position] = ranges[position - 1];
      --position;
    }
    ranges[position] = value;
  }
}

worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckPrepareRangesV1(
    worr_native_carrier_ack_ledger_v1 *ledger, uint64_t now_tick,
    uint32_t retry_interval_ticks, uint16_t max_ranges,
    worr_native_ack_range_v1 *ranges_out, uint16_t ranges_capacity,
    uint16_t *range_count_out,
    worr_native_carrier_ack_emit_token_v1 *token_out) {
  bool selected[WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY] = {false};
  worr_native_carrier_ack_token_range_v1
      compact[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
  worr_native_ack_range_v1 ranges[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
  worr_native_carrier_ack_emit_token_v1 token;
  worr_native_carrier_ack_ledger_v1 staged;
  worr_native_carrier_ack_result_v1 due;
  uint16_t count = 0;
  uint16_t index;
  const size_t ranges_bytes = (size_t)max_ranges * sizeof(*ranges_out);

  if (ledger == NULL || ranges_out == NULL || range_count_out == NULL ||
      token_out == NULL || max_ranges == 0 ||
      max_ranges > WORR_NATIVE_CARRIER_ACK_MAX_RANGES ||
      ranges_capacity < max_ranges ||
      ranges_overlap(ranges_out, ranges_bytes, ledger, sizeof(*ledger)) ||
      ranges_overlap(range_count_out, sizeof(*range_count_out), ledger,
                     sizeof(*ledger)) ||
      ranges_overlap(token_out, sizeof(*token_out), ledger, sizeof(*ledger)) ||
      ranges_overlap(ranges_out, ranges_bytes, range_count_out,
                     sizeof(*range_count_out)) ||
      ranges_overlap(ranges_out, ranges_bytes, token_out, sizeof(*token_out)) ||
      ranges_overlap(range_count_out, sizeof(*range_count_out), token_out,
                     sizeof(*token_out))) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT;
  }
  due = Worr_NativeCarrierAckPeekDueV1(ledger, now_tick, retry_interval_ticks);
  if (due != WORR_NATIVE_CARRIER_ACK_OK)
    return due;

  memset(compact, 0, sizeof(compact));
  while (count < max_ranges) {
    int best = -1;
    uint32_t first;
    uint32_t last;

    for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY; ++index) {
      if (selected[index] || !receipt_due(&ledger->receipts[index], now_tick,
                                          retry_interval_ticks)) {
        continue;
      }
      if (best < 0 ||
          receipt_better(&ledger->receipts[index], &ledger->receipts[best])) {
        best = (int)index;
      }
    }
    if (best < 0)
      break;
    selected[best] = true;
    first = ledger->receipts[best].message_sequence;
    last = first;
    while (first > 1) {
      const int adjacent = due_receipt_find(ledger, selected, first - 1u,
                                            now_tick, retry_interval_ticks);
      if (adjacent < 0)
        break;
      selected[adjacent] = true;
      --first;
    }
    while (last < UINT32_MAX) {
      const int adjacent = due_receipt_find(ledger, selected, last + 1u,
                                            now_tick, retry_interval_ticks);
      if (adjacent < 0)
        break;
      selected[adjacent] = true;
      ++last;
    }
    compact[count].first_message_sequence = first;
    compact[count].last_message_sequence = last;
    ++count;
  }
  if (count == 0)
    return WORR_NATIVE_CARRIER_ACK_NOT_DUE;

  sort_token_ranges(compact, count);
  memset(ranges, 0, sizeof(ranges));
  memset(&token, 0, sizeof(token));
  token.struct_size = sizeof(token);
  token.schema_version = WORR_NATIVE_CARRIER_ACK_ABI_VERSION;
  token.range_count = count;
  token.transport_epoch = ledger->transport_epoch;
  token.state_flags = WORR_NATIVE_CARRIER_ACK_TOKEN_INITIALIZED;
  token.connection_owner_id = ledger->connection_owner_id;
  token.token_id = ledger->next_token_id;
  token.prepare_tick = now_tick;
  for (index = 0; index < count; ++index) {
    ranges[index].struct_size = sizeof(ranges[index]);
    ranges[index].schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    ranges[index].transport_epoch = ledger->transport_epoch;
    ranges[index].first_message_sequence =
        compact[index].first_message_sequence;
    ranges[index].last_message_sequence = compact[index].last_message_sequence;
    ranges[index].connection_owner_id = ledger->connection_owner_id;
    token.ranges[index] = compact[index];
  }
  staged = *ledger;
  ++staged.mutation_generation;
  token.ledger_generation = staged.mutation_generation;
  staged.state_flags |= WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE;
  if (staged.next_token_id == UINT64_MAX) {
    staged.state_flags |= WORR_NATIVE_CARRIER_ACK_LEDGER_TOKEN_EXHAUSTED;
  } else {
    ++staged.next_token_id;
  }
  staged.active_token = token;
  *ledger = staged;
  memcpy(ranges_out, ranges, (size_t)count * sizeof(ranges[0]));
  *range_count_out = count;
  *token_out = token;
  return WORR_NATIVE_CARRIER_ACK_OK;
}

static worr_native_carrier_ack_result_v1
carrier_result(worr_native_carrier_result_v1 result) {
  switch (result) {
  case WORR_NATIVE_CARRIER_OK:
    return WORR_NATIVE_CARRIER_ACK_OK;
  case WORR_NATIVE_CARRIER_NO_CARRIER:
    return WORR_NATIVE_CARRIER_ACK_NO_CARRIER;
  case WORR_NATIVE_CARRIER_INVALID_ARGUMENT:
    return WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT;
  case WORR_NATIVE_CARRIER_OUTPUT_TOO_SMALL:
    return WORR_NATIVE_CARRIER_ACK_OUTPUT_TOO_SMALL;
  case WORR_NATIVE_CARRIER_LIMIT:
    return WORR_NATIVE_CARRIER_ACK_LIMIT;
  case WORR_NATIVE_CARRIER_MALFORMED:
    return WORR_NATIVE_CARRIER_ACK_MALFORMED;
  case WORR_NATIVE_CARRIER_UNSUPPORTED:
    return WORR_NATIVE_CARRIER_ACK_UNSUPPORTED;
  case WORR_NATIVE_CARRIER_CORRUPT:
    return WORR_NATIVE_CARRIER_ACK_CORRUPT;
  }
  return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
}

worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckPreparePacketV1(
    worr_native_carrier_ack_ledger_v1 *ledger, uint64_t now_tick,
    uint32_t retry_interval_ticks, uint16_t application_packet_budget,
    const void *legacy_packet, size_t legacy_bytes, void *packet_out,
    size_t packet_capacity, size_t *packet_bytes_out,
    worr_native_carrier_ack_emit_token_v1 *token_out) {
  uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
  worr_native_ack_range_v1 ranges[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
  worr_native_carrier_entry_v1 entries[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
  worr_native_carrier_ack_emit_token_v1 token;
  worr_native_carrier_ack_ledger_v1 staged;
  uint16_t range_count;
  uint16_t fit;
  uint16_t index;
  size_t encoded_bytes;
  worr_native_carrier_ack_result_v1 prepared;
  worr_native_carrier_result_v1 encoded;

  if (ledger == NULL || packet_out == NULL || packet_bytes_out == NULL ||
      token_out == NULL || application_packet_budget == 0 ||
      application_packet_budget > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES ||
      (legacy_bytes != 0 && legacy_packet == NULL) ||
      legacy_bytes > application_packet_budget ||
      ranges_overlap(packet_out, packet_capacity, ledger, sizeof(*ledger)) ||
      ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), ledger,
                     sizeof(*ledger)) ||
      ranges_overlap(token_out, sizeof(*token_out), ledger, sizeof(*ledger)) ||
      ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), packet_out,
                     packet_capacity) ||
      ranges_overlap(token_out, sizeof(*token_out), packet_out,
                     packet_capacity) ||
      ranges_overlap(token_out, sizeof(*token_out), packet_bytes_out,
                     sizeof(*packet_bytes_out)) ||
      ranges_overlap(legacy_packet, legacy_bytes, ledger, sizeof(*ledger)) ||
      ranges_overlap(legacy_packet, legacy_bytes, token_out,
                     sizeof(*token_out)) ||
      ranges_overlap(legacy_packet, legacy_bytes, packet_bytes_out,
                     sizeof(*packet_bytes_out))) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT;
  }
  if ((size_t)application_packet_budget <
      legacy_bytes + WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES +
          WORR_NATIVE_CARRIER_WIRE_ACK_ENTRY_BYTES) {
    return WORR_NATIVE_CARRIER_ACK_LIMIT;
  }
  fit = (uint16_t)(((size_t)application_packet_budget - legacy_bytes -
                    WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES) /
                   WORR_NATIVE_CARRIER_WIRE_ACK_ENTRY_BYTES);
  if (fit > WORR_NATIVE_CARRIER_ACK_MAX_RANGES)
    fit = WORR_NATIVE_CARRIER_ACK_MAX_RANGES;

  staged = *ledger;
  prepared = Worr_NativeCarrierAckPrepareRangesV1(
      &staged, now_tick, retry_interval_ticks, fit, ranges,
      WORR_NATIVE_CARRIER_ACK_MAX_RANGES, &range_count, &token);
  if (prepared != WORR_NATIVE_CARRIER_ACK_OK)
    return prepared;
  memset(entries, 0, sizeof(entries));
  for (index = 0; index < range_count; ++index) {
    entries[index].struct_size = sizeof(entries[index]);
    entries[index].schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entries[index].entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    entries[index].first_message_sequence =
        ranges[index].first_message_sequence;
    entries[index].last_message_sequence = ranges[index].last_message_sequence;
  }
  encoded = Worr_NativeCarrierEncodeV1(
      staged.transport_epoch, legacy_packet, legacy_bytes, NULL, 0, entries,
      range_count, packet, application_packet_budget, &encoded_bytes);
  if (encoded != WORR_NATIVE_CARRIER_OK)
    return carrier_result(encoded);
  if (encoded_bytes > packet_capacity)
    return WORR_NATIVE_CARRIER_ACK_OUTPUT_TOO_SMALL;
  prepared =
      Worr_NativeCarrierAckBindPacketV1(&staged, &token, packet, encoded_bytes);
  if (prepared != WORR_NATIVE_CARRIER_ACK_OK)
    return prepared;
  *ledger = staged;
  memcpy(packet_out, packet, encoded_bytes);
  *packet_bytes_out = encoded_bytes;
  *token_out = token;
  return WORR_NATIVE_CARRIER_ACK_OK;
}

static bool token_validate(const worr_native_carrier_ack_emit_token_v1 *token) {
  const uint16_t known_flags = WORR_NATIVE_CARRIER_ACK_TOKEN_INITIALIZED |
                               WORR_NATIVE_CARRIER_ACK_TOKEN_PACKET_BOUND;
  const bool packet_bound =
      token != NULL &&
      (token->state_flags & WORR_NATIVE_CARRIER_ACK_TOKEN_PACKET_BOUND) != 0;
  uint16_t index;

  if (token == NULL || token->struct_size != sizeof(*token) ||
      token->schema_version != WORR_NATIVE_CARRIER_ACK_ABI_VERSION ||
      token->range_count == 0 ||
      token->range_count > WORR_NATIVE_CARRIER_ACK_MAX_RANGES ||
      token->transport_epoch == 0 || (token->state_flags & ~known_flags) != 0 ||
      (token->state_flags & WORR_NATIVE_CARRIER_ACK_TOKEN_INITIALIZED) == 0 ||
      token->reserved0 != 0 || token->connection_owner_id == 0 ||
      token->ledger_generation == 0 || token->token_id == 0 ||
      token->reserved1 != 0 ||
      (packet_bound
           ? token->packet_bytes == 0 ||
                 token->packet_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES
           : token->packet_crc32 != 0 || token->packet_bytes != 0)) {
    return false;
  }
  for (index = 0; index < token->range_count; ++index) {
    if (token->ranges[index].first_message_sequence == 0 ||
        token->ranges[index].last_message_sequence <
            token->ranges[index].first_message_sequence) {
      return false;
    }
    if (index != 0 &&
        (token->ranges[index - 1].last_message_sequence == UINT32_MAX ||
         token->ranges[index].first_message_sequence <=
             token->ranges[index - 1].last_message_sequence + 1u)) {
      return false;
    }
  }
  for (; index < WORR_NATIVE_CARRIER_ACK_MAX_RANGES; ++index) {
    if (token->ranges[index].first_message_sequence != 0 ||
        token->ranges[index].last_message_sequence != 0) {
      return false;
    }
  }
  return true;
}

static bool
token_matches_active(const worr_native_carrier_ack_ledger_v1 *ledger,
                     const worr_native_carrier_ack_emit_token_v1 *token) {
  return (ledger->state_flags & WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) !=
             0 &&
         token_validate(token) &&
         memcmp(token, &ledger->active_token, sizeof(*token)) == 0;
}

static worr_native_carrier_ack_result_v1
packet_ack_identity(const worr_native_carrier_ack_emit_token_v1 *token,
                    const void *packet, size_t packet_bytes) {
  worr_native_carrier_view_v1 view;
  worr_native_carrier_result_v1 decoded;
  uint16_t ack_index = 0;
  uint16_t entry_index;

  decoded = Worr_NativeCarrierDecodeV1(packet, packet_bytes, &view);
  if (decoded != WORR_NATIVE_CARRIER_OK)
    return carrier_result(decoded);
  if (view.transport_epoch != token->transport_epoch)
    return WORR_NATIVE_CARRIER_ACK_WRONG_EPOCH;
  for (entry_index = 0; entry_index < view.entry_count; ++entry_index) {
    const worr_native_carrier_entry_v1 *entry = &view.entries[entry_index];

    if (entry->entry_type != WORR_NATIVE_CARRIER_ENTRY_ACK_V1)
      continue;
    if (ack_index >= token->range_count ||
        entry->first_message_sequence !=
            token->ranges[ack_index].first_message_sequence ||
        entry->last_message_sequence !=
            token->ranges[ack_index].last_message_sequence) {
      return WORR_NATIVE_CARRIER_ACK_PACKET_MISMATCH;
    }
    ++ack_index;
  }
  return ack_index == token->range_count
             ? WORR_NATIVE_CARRIER_ACK_OK
             : WORR_NATIVE_CARRIER_ACK_PACKET_MISMATCH;
}

worr_native_carrier_ack_result_v1
Worr_NativeCarrierAckBindPacketV1(worr_native_carrier_ack_ledger_v1 *ledger,
                                  worr_native_carrier_ack_emit_token_v1 *token,
                                  const void *packet, size_t packet_bytes) {
  worr_native_carrier_ack_ledger_v1 staged;
  worr_native_carrier_ack_emit_token_v1 bound;
  worr_native_carrier_ack_result_v1 identity;

  if (ledger == NULL || token == NULL || packet == NULL || packet_bytes == 0 ||
      packet_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES ||
      ranges_overlap(ledger, sizeof(*ledger), token, sizeof(*token)) ||
      ranges_overlap(ledger, sizeof(*ledger), packet, packet_bytes) ||
      ranges_overlap(token, sizeof(*token), packet, packet_bytes)) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT;
  }
  if (!Worr_NativeCarrierAckLedgerValidateV1(ledger) ||
      !token_validate(token)) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  }
  if (token->transport_epoch != ledger->transport_epoch)
    return WORR_NATIVE_CARRIER_ACK_WRONG_EPOCH;
  if (token->connection_owner_id != ledger->connection_owner_id)
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  if (!token_matches_active(ledger, token) ||
      (token->state_flags & WORR_NATIVE_CARRIER_ACK_TOKEN_PACKET_BOUND) != 0) {
    return WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION;
  }
  if (ledger->mutation_generation == UINT64_MAX)
    return WORR_NATIVE_CARRIER_ACK_LIMIT;
  identity = packet_ack_identity(token, packet, packet_bytes);
  if (identity != WORR_NATIVE_CARRIER_ACK_OK)
    return identity;

  staged = *ledger;
  bound = *token;
  bound.state_flags |= WORR_NATIVE_CARRIER_ACK_TOKEN_PACKET_BOUND;
  bound.packet_crc32 = crc32_bytes(packet, packet_bytes);
  bound.packet_bytes = (uint16_t)packet_bytes;
  ++staged.mutation_generation;
  bound.ledger_generation = staged.mutation_generation;
  staged.active_token = bound;
  *ledger = staged;
  *token = bound;
  return WORR_NATIVE_CARRIER_ACK_OK;
}

worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckCommitHandoffV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    const worr_native_carrier_ack_emit_token_v1 *token, uint64_t handoff_tick,
    const void *packet, size_t packet_bytes) {
  worr_native_carrier_ack_ledger_v1 staged;
  uint16_t entry_index;
  uint16_t range_index;
  uint64_t receipts_handed_off = 0;

  if (ledger == NULL || token == NULL || packet == NULL || packet_bytes == 0 ||
      ranges_overlap(ledger, sizeof(*ledger), token, sizeof(*token)) ||
      ranges_overlap(ledger, sizeof(*ledger), packet, packet_bytes)) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT;
  }
  if (!Worr_NativeCarrierAckLedgerValidateV1(ledger) ||
      !token_validate(token)) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  }
  if (token->transport_epoch != ledger->transport_epoch)
    return WORR_NATIVE_CARRIER_ACK_WRONG_EPOCH;
  if (token->connection_owner_id != ledger->connection_owner_id)
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  if (!token_matches_active(ledger, token) ||
      (token->state_flags & WORR_NATIVE_CARRIER_ACK_TOKEN_PACKET_BOUND) == 0 ||
      token->ledger_generation != ledger->mutation_generation) {
    return WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION;
  }
  if (ledger->mutation_generation == UINT64_MAX)
    return WORR_NATIVE_CARRIER_ACK_LIMIT;
  if (handoff_tick < token->prepare_tick ||
      handoff_tick < ledger->last_handoff_tick) {
    return WORR_NATIVE_CARRIER_ACK_CLOCK_REGRESSION;
  }

  if (packet_bytes != token->packet_bytes ||
      crc32_bytes(packet, packet_bytes) != token->packet_crc32 ||
      packet_ack_identity(token, packet, packet_bytes) !=
          WORR_NATIVE_CARRIER_ACK_OK) {
    return WORR_NATIVE_CARRIER_ACK_PACKET_MISMATCH;
  }

  staged = *ledger;
  for (range_index = 0; range_index < token->range_count; ++range_index) {
    const uint64_t expected =
        (uint64_t)token->ranges[range_index].last_message_sequence -
        token->ranges[range_index].first_message_sequence + 1u;
    uint64_t found = 0;

    for (entry_index = 0;
         entry_index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY;
         ++entry_index) {
      worr_native_carrier_ack_receipt_v1 *receipt =
          &staged.receipts[entry_index];

      if (!receipt_occupied(receipt) ||
          receipt->message_sequence <
              token->ranges[range_index].first_message_sequence ||
          receipt->message_sequence >
              token->ranges[range_index].last_message_sequence) {
        continue;
      }
      ++found;
      if (receipt->handoffs_remaining == 0)
        return WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION;
      if (receipt->handoff_attempts != UINT32_MAX)
        ++receipt->handoff_attempts;
      receipt->last_handoff_tick = handoff_tick;
      receipt->state_flags &=
          (uint8_t)~WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE;
      --receipt->handoffs_remaining;
      if (receipt->handoffs_remaining == 0) {
        counter_add(&staged.telemetry.proactive_bursts_completed, 1);
      }
    }
    if (found != expected)
      return WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION;
    receipts_handed_off += found;
  }
  staged.last_handoff_tick = handoff_tick;
  counter_add(&staged.telemetry.handoff_commits, 1);
  counter_add(&staged.telemetry.ranges_handed_off, token->range_count);
  counter_add(&staged.telemetry.receipts_handed_off, receipts_handed_off);
  ++staged.mutation_generation;
  staged.state_flags &= (uint16_t)~WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE;
  memset(&staged.active_token, 0, sizeof(staged.active_token));
  *ledger = staged;
  return WORR_NATIVE_CARRIER_ACK_OK;
}

static worr_native_carrier_ack_result_v1
emit_terminal(worr_native_carrier_ack_ledger_v1 *ledger,
              const worr_native_carrier_ack_emit_token_v1 *token,
              bool require_bound) {
  worr_native_carrier_ack_ledger_v1 staged;
  const bool packet_bound =
      token != NULL &&
      (token->state_flags & WORR_NATIVE_CARRIER_ACK_TOKEN_PACKET_BOUND) != 0;

  if (ledger == NULL || token == NULL ||
      ranges_overlap(ledger, sizeof(*ledger), token, sizeof(*token))) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT;
  }
  if (!Worr_NativeCarrierAckLedgerValidateV1(ledger) ||
      !token_validate(token)) {
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  }
  if (token->transport_epoch != ledger->transport_epoch)
    return WORR_NATIVE_CARRIER_ACK_WRONG_EPOCH;
  if (token->connection_owner_id != ledger->connection_owner_id)
    return WORR_NATIVE_CARRIER_ACK_INVALID_STATE;
  if (!token_matches_active(ledger, token) || packet_bound != require_bound) {
    return WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION;
  }
  if (ledger->mutation_generation == UINT64_MAX)
    return WORR_NATIVE_CARRIER_ACK_LIMIT;

  staged = *ledger;
  ++staged.mutation_generation;
  staged.state_flags &= (uint16_t)~WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE;
  memset(&staged.active_token, 0, sizeof(staged.active_token));
  *ledger = staged;
  return WORR_NATIVE_CARRIER_ACK_OK;
}

worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckRejectHandoffV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    const worr_native_carrier_ack_emit_token_v1 *token) {
  return emit_terminal(ledger, token, true);
}

worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckAbortV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    const worr_native_carrier_ack_emit_token_v1 *token) {
  return emit_terminal(ledger, token, false);
}
