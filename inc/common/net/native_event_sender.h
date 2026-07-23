/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_carrier_mixed.h"
#include "common/net/native_codec.h"
#include "common/net/native_event_batch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bounded, transport-neutral server-originated event sender (FR-10-T04/T05).
 * It owns one reliable descriptor followed by a FIFO of per-peer event
 * candidates.  Candidates remain ID-less until the descriptor's exact WTC1
 * ACK is applied; only then are authoritative IDs assigned in FIFO order.
 *
 * The object owns codec bytes and native TX retention, but no socket, netchan,
 * clock, cvar, snapshot store, or ACK ledger.  One live owner may retain a
 * previous object for late ACK release, but a retired object can never queue
 * or emit DATA.
 */
#define WORR_NATIVE_EVENT_SENDER_ABI_VERSION 1u
#define WORR_NATIVE_EVENT_SENDER_TX_CAPACITY 64u
#define WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY 512u
#define WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_BITS 6u
#define WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_MASK \
    ((UINT32_C(1) << WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_BITS) - UINT32_C(1))
#define WORR_NATIVE_EVENT_SENDER_PAYLOAD_GENERATION_MAX \
    (UINT32_MAX >> WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_BITS)
#define WORR_NATIVE_EVENT_SENDER_MAX_ENCODED_BYTES \
    WORR_NATIVE_EVENT_BATCH_MAX_PAYLOAD_BYTES

enum {
    WORR_NATIVE_EVENT_SENDER_INITIALIZED = 1u << 0,
    WORR_NATIVE_EVENT_SENDER_DESCRIPTOR_ACKED = 1u << 1,
    WORR_NATIVE_EVENT_SENDER_RETIRED = 1u << 2,
    WORR_NATIVE_EVENT_SENDER_SEQUENCE_EXHAUSTED = 1u << 3,
    WORR_NATIVE_EVENT_SENDER_PACKET_PREPARED = 1u << 4,
    WORR_NATIVE_EVENT_SENDER_CANCELLED = 1u << 5,
};

enum {
    WORR_NATIVE_EVENT_SENDER_PAYLOAD_OCCUPIED = 1u << 0,
    WORR_NATIVE_EVENT_SENDER_PAYLOAD_RETIRED = 1u << 1,
};

typedef struct worr_native_event_sender_payload_v1_s {
    uint32_t handle;
    uint32_t generation;
    worr_native_record_ref_v1 record;
    uint16_t encoded_bytes;
    uint16_t state_flags;
    uint8_t encoded[WORR_NATIVE_EVENT_SENDER_MAX_ENCODED_BYTES];
} worr_native_event_sender_payload_v1;

typedef struct worr_native_event_sender_telemetry_v1_s {
    uint64_t candidates_queued;
    uint64_t candidates_promoted;
    uint64_t backlog_stalls;
    uint64_t tx_capacity_stalls;
    uint64_t descriptors_acknowledged;
    uint64_t events_acknowledged;
    uint64_t packets_prepared;
    uint64_t packets_confirmed;
    uint64_t packets_rejected;
    uint64_t first_sends;
    uint64_t retries;
    uint64_t validation_failures;
    /* Successful schema-2 promotion only; retries and ACK refreshes do not
     * advance these logical construction counters. */
    uint64_t schema2_batches_promoted;
    uint64_t schema2_events_promoted;
} worr_native_event_sender_telemetry_v1;

typedef struct worr_native_event_sender_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    worr_native_session_binding_v1 binding;
    worr_event_stream_descriptor_v1 descriptor;
    uint32_t next_event_sequence;
    uint32_t max_entities;
    uint16_t max_datagram_bytes;
    uint16_t backlog_head;
    uint16_t backlog_count;
    uint16_t payload_occupied;
    uint16_t payload_retired;
    uint16_t next_payload_slot;
    uint16_t reserved0;
    uint32_t reserved1;
    worr_native_event_sender_telemetry_v1 telemetry;
    worr_native_tx_session_v1 tx;
    worr_native_tx_slot_v1 tx_slots[WORR_NATIVE_EVENT_SENDER_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 tx_gate;
    worr_native_carrier_dispatch_v1 dispatch;
    worr_native_carrier_mixed_token_v1 mixed_token;
    worr_native_event_sender_payload_v1
        payloads[WORR_NATIVE_EVENT_SENDER_TX_CAPACITY];
    worr_event_record_v1 backlog[WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY];
} worr_native_event_sender_v1;

typedef struct worr_native_event_sender_cancel_report_v1_s {
    uint32_t retained_messages;
    uint32_t backlog_candidates;
    uint32_t payloads_released;
} worr_native_event_sender_cancel_report_v1;

typedef enum worr_native_event_sender_result_v1_e {
    WORR_NATIVE_EVENT_SENDER_OK = 0,
    WORR_NATIVE_EVENT_SENDER_QUEUED = 1,
    WORR_NATIVE_EVENT_SENDER_PROMOTED = 2,
    WORR_NATIVE_EVENT_SENDER_ACK_APPLIED = 3,
    WORR_NATIVE_EVENT_SENDER_NOT_DUE = 4,
    WORR_NATIVE_EVENT_SENDER_CAPACITY = 5,
    WORR_NATIVE_EVENT_SENDER_RETIRED_RESULT = 6,
    WORR_NATIVE_EVENT_SENDER_SEQUENCE_LIMIT = 7,
    WORR_NATIVE_EVENT_SENDER_OUTPUT_TOO_SMALL = 8,
    WORR_NATIVE_EVENT_SENDER_INVALID_ARGUMENT = 9,
    WORR_NATIVE_EVENT_SENDER_INVALID_STATE = 10,
    WORR_NATIVE_EVENT_SENDER_INVALID_RECORD = 11,
    WORR_NATIVE_EVENT_SENDER_WRONG_EPOCH = 12,
    WORR_NATIVE_EVENT_SENDER_CLOCK_REGRESSION = 13,
    WORR_NATIVE_EVENT_SENDER_TRANSPORT_REJECTED = 14,
    WORR_NATIVE_EVENT_SENDER_CANCELLED_RESULT = 15,
} worr_native_event_sender_result_v1;

/*
 * max_datagram_bytes is the frozen WNE1 DATA budget after reserving carrier
 * overhead.  The binding must contain NATIVE_EVENT_PRIVATE_MASK, including the
 * epoch-cancellation barrier.  Initialization enqueues the descriptor
 * immediately.
 */
worr_native_event_sender_result_v1 Worr_NativeEventSenderInitV1(
    worr_native_event_sender_v1 *sender,
    const worr_native_session_binding_v1 *binding,
    const worr_event_stream_descriptor_v1 *descriptor,
    uint32_t max_entities,
    uint16_t max_datagram_bytes,
    uint64_t now_tick);

bool Worr_NativeEventSenderValidateV1(
    const worr_native_event_sender_v1 *sender);

/* Transactional batch append of ID-less canonical candidates. */
worr_native_event_sender_result_v1 Worr_NativeEventSenderQueueCandidatesV1(
    worr_native_event_sender_v1 *sender,
    const worr_event_record_v1 *candidates,
    uint32_t count);

/*
 * Assigns IDs and retains as many FIFO candidates as current TX/payload
 * capacity permits.  Before descriptor ACK this is a successful no-op.
 */
worr_native_event_sender_result_v1 Worr_NativeEventSenderPumpV1(
    worr_native_event_sender_v1 *sender,
    uint64_t now_tick,
    uint32_t *promoted_out);

/*
 * Applies every ACK in an admitted WTC1 packet to a staged TX/payload image.
 * Exact payload handles are released only when their retained slot disappears.
 * A retired sender accepts this operation solely for late release.
 */
worr_native_event_sender_result_v1 Worr_NativeEventSenderApplyAcksV1(
    worr_native_event_sender_v1 *sender,
    const void *packet,
    size_t packet_bytes,
    uint32_t *acknowledged_out);

/*
 * Non-mutating DATA scheduler query.  Call Pump after queueing candidates or
 * opening the descriptor gate; an active multi-fragment dispatch is due until
 * its next packet is prepared.  The result never includes ACK-only work.
 */
worr_native_event_sender_result_v1 Worr_NativeEventSenderDataDuePeekV1(
    const worr_native_event_sender_v1 *sender,
    uint64_t now_tick,
    uint32_t resend_interval_ticks,
    bool *due_out);

/*
 * Builds one DATA fragment plus zero-to-seven due ACK ranges.  Prepare mutates
 * the sender and caller-owned ledger only transactionally and stores the exact
 * mixed token in sender.  Confirm or Reject must terminate every preparation.
 */
worr_native_event_sender_result_v1 Worr_NativeEventSenderPrepareMixedV1(
    worr_native_event_sender_v1 *sender,
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
    uint64_t *token_out);

worr_native_event_sender_result_v1 Worr_NativeEventSenderConfirmMixedV1(
    worr_native_event_sender_v1 *sender,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    uint64_t handoff_tick,
    const void *packet,
    size_t packet_bytes);

/* Definite transport rejection aborts the whole DATA dispatch for reselection. */
worr_native_event_sender_result_v1 Worr_NativeEventSenderRejectMixedV1(
    worr_native_event_sender_v1 *sender,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const void *packet,
    size_t packet_bytes);

/* Requires no prepared packet; idempotent and permanently disables DATA. */
worr_native_event_sender_result_v1 Worr_NativeEventSenderRetireV1(
    worr_native_event_sender_v1 *sender);

/*
 * Explicit terminal cancellation.  No candidate is pumped or promoted.
 * Retention, payload ownership, and backlog are released transactionally and
 * counted while binding, stream/message high-water marks, payload-generation
 * anti-alias state, clocks, and telemetry remain intact.  A prepared packet
 * has an unknown transport outcome and is refused byte-identically.  A known
 * active multi-fragment dispatch is aborted locally before cancellation.
 * Success is idempotent and permanently disables DATA.
 */
worr_native_event_sender_result_v1 Worr_NativeEventSenderCancelV1(
    worr_native_event_sender_v1 *sender,
    worr_native_event_sender_cancel_report_v1 *report_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(WORR_NATIVE_EVENT_SENDER_TX_CAPACITY ==
                  (UINT32_C(1) <<
                   WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_BITS),
              "native event sender payload index width changed");
#else
_Static_assert(WORR_NATIVE_EVENT_SENDER_TX_CAPACITY ==
                   (UINT32_C(1) <<
                    WORR_NATIVE_EVENT_SENDER_PAYLOAD_INDEX_BITS),
               "native event sender payload index width changed");
#endif

#if defined(__cplusplus)
static_assert(sizeof(worr_native_event_sender_cancel_report_v1) == 12,
              "native event sender cancel report v1 layout changed");
#else
_Static_assert(sizeof(worr_native_event_sender_cancel_report_v1) == 12,
               "native event sender cancel report v1 layout changed");
#endif
