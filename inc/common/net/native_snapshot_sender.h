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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transport-neutral owner for one server-to-client canonical snapshot stream.
 *
 * The native TX session deliberately has one supersedable slot.  Two
 * generation-tagged payload banks keep the selected WNC1 bytes immutable for
 * an entire multi-fragment dispatch while newer projections coalesce into one
 * latest-pending bank.  Once the dispatch has a definite terminal outcome,
 * the pending bank is promoted through NativeTxSessionEnqueueV1, atomically
 * superseding the older retained snapshot when it is still present.
 *
 * The object owns encoded bytes and carrier transaction state, but no socket,
 * netchan, clock, snapshot store, cvar, or inbound ACK ledger.  It is intended
 * to be allocated only for peers that completed the exact private canonical
 * snapshot readiness binding.
 */
#define WORR_NATIVE_SNAPSHOT_SENDER_ABI_VERSION 1u
#define WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY 1u
#define WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS 2u
#define WORR_NATIVE_SNAPSHOT_SENDER_NO_BANK UINT8_MAX
#define WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_INDEX_BITS 1u
#define WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_INDEX_MASK UINT32_C(1)
#define WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_GENERATION_MAX \
    (UINT32_MAX >> WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_INDEX_BITS)
#define WORR_NATIVE_SNAPSHOT_SENDER_MAX_ENCODED_BYTES \
    WORR_NATIVE_CODEC_MAX_ENCODED_BYTES
#define WORR_NATIVE_SNAPSHOT_SENDER_PRIORITY 2u

enum {
    WORR_NATIVE_SNAPSHOT_SENDER_INITIALIZED = 1u << 0,
    WORR_NATIVE_SNAPSHOT_SENDER_PACKET_PREPARED = 1u << 1,
    WORR_NATIVE_SNAPSHOT_SENDER_RETIRED = 1u << 2,
    WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED = 1u << 3,
    WORR_NATIVE_SNAPSHOT_SENDER_PROMOTION_STALLED = 1u << 4,
};

enum {
    WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_OCCUPIED = 1u << 0,
    WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_RETIRED = 1u << 1,
};

typedef struct worr_native_snapshot_sender_payload_v1_s {
    uint32_t handle;
    uint32_t generation;
    uint32_t encoded_bytes;
    uint16_t state_flags;
    uint16_t reserved0;
    worr_native_record_ref_v1 record;
    worr_snapshot_projection_hashes_v2 hashes;
    uint64_t snapshot_hash;
    uint64_t queue_tick;
    uint8_t encoded[WORR_NATIVE_SNAPSHOT_SENDER_MAX_ENCODED_BYTES];
} worr_native_snapshot_sender_payload_v1;

/* Every counter saturates at UINT64_MAX. */
typedef struct worr_native_snapshot_sender_telemetry_v1_s {
    uint64_t queue_attempts;
    uint64_t snapshots_retained;
    uint64_t snapshots_superseded;
    uint64_t snapshots_pending;
    uint64_t pending_coalesced;
    uint64_t pending_promoted;
    uint64_t duplicates;
    uint64_t stale_snapshots;
    uint64_t conflicts;
    uint64_t capacity_stalls;
    uint64_t promotion_stalls;
    uint64_t acknowledgements_applied;
    uint64_t payloads_released;
    uint64_t packets_prepared;
    uint64_t packets_confirmed;
    uint64_t packets_rejected;
    uint64_t first_sends;
    uint64_t retries;
} worr_native_snapshot_sender_telemetry_v1;

typedef struct worr_native_snapshot_sender_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    worr_native_session_binding_v1 binding;
    uint32_t max_entities;
    uint16_t max_datagram_bytes;
    uint8_t pending_bank;
    uint8_t next_payload_bank;
    uint8_t payload_occupied;
    uint8_t payload_retired;
    uint16_t reserved0;
    worr_snapshot_id_v2 latest_offered_snapshot;
    uint64_t last_tick;
    worr_native_snapshot_sender_telemetry_v1 telemetry;
    worr_native_tx_session_v1 tx;
    worr_native_tx_slot_v1
        tx_slots[WORR_NATIVE_SNAPSHOT_SENDER_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 tx_gate;
    worr_native_carrier_dispatch_v1 dispatch;
    worr_native_carrier_mixed_token_v1 mixed_token;
    worr_native_snapshot_sender_payload_v1
        payloads[WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS];
} worr_native_snapshot_sender_v1;

typedef struct worr_native_snapshot_sender_status_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint32_t transport_epoch;
    uint32_t active_message_sequence;
    worr_snapshot_id_v2 active_snapshot;
    worr_snapshot_id_v2 pending_snapshot;
    uint32_t active_payload_bytes;
    uint32_t pending_payload_bytes;
    uint16_t active_fragment_count;
    uint8_t retained_messages;
    uint8_t payload_occupied;
    uint64_t connection_owner_id;
    uint64_t last_tick;
    worr_native_snapshot_sender_telemetry_v1 telemetry;
} worr_native_snapshot_sender_status_v1;

typedef struct worr_native_snapshot_sender_cancel_report_v1_s {
    uint32_t retained_messages;
    uint32_t pending_snapshots;
    uint32_t payloads_released;
} worr_native_snapshot_sender_cancel_report_v1;

typedef enum worr_native_snapshot_sender_result_v1_e {
    WORR_NATIVE_SNAPSHOT_SENDER_OK = 0,
    WORR_NATIVE_SNAPSHOT_SENDER_RETAINED = 1,
    WORR_NATIVE_SNAPSHOT_SENDER_SUPERSEDED = 2,
    WORR_NATIVE_SNAPSHOT_SENDER_PENDING = 3,
    WORR_NATIVE_SNAPSHOT_SENDER_COALESCED = 4,
    WORR_NATIVE_SNAPSHOT_SENDER_ACK_APPLIED = 5,
    WORR_NATIVE_SNAPSHOT_SENDER_DUPLICATE = 6,
    WORR_NATIVE_SNAPSHOT_SENDER_NOT_DUE = 7,
    WORR_NATIVE_SNAPSHOT_SENDER_STALE_SNAPSHOT = 8,
    WORR_NATIVE_SNAPSHOT_SENDER_CONFLICT = 9,
    WORR_NATIVE_SNAPSHOT_SENDER_CAPACITY = 10,
    WORR_NATIVE_SNAPSHOT_SENDER_SEQUENCE_LIMIT = 11,
    WORR_NATIVE_SNAPSHOT_SENDER_RETIRED_RESULT = 12,
    WORR_NATIVE_SNAPSHOT_SENDER_CANCELLED_RESULT = 13,
    WORR_NATIVE_SNAPSHOT_SENDER_OUTPUT_TOO_SMALL = 14,
    WORR_NATIVE_SNAPSHOT_SENDER_WRONG_EPOCH = 15,
    WORR_NATIVE_SNAPSHOT_SENDER_CLOCK_REGRESSION = 16,
    WORR_NATIVE_SNAPSHOT_SENDER_TRANSPORT_REJECTED = 17,
    WORR_NATIVE_SNAPSHOT_SENDER_INVALID_ARGUMENT = 18,
    WORR_NATIVE_SNAPSHOT_SENDER_INVALID_STATE = 19,
    WORR_NATIVE_SNAPSHOT_SENDER_INVALID_RECORD = 20,
} worr_native_snapshot_sender_result_v1;

/*
 * max_datagram_bytes is the frozen maximum complete WNE1 datagram for every
 * retained snapshot.  The binding must include the exact native snapshot
 * private mask, including canonical-snapshot and epoch-cancellation bits.
 * Initialization retains no DATA and leaves sender byte-identical on failure.
 */
worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderInitV1(
    worr_native_snapshot_sender_v1 *sender,
    const worr_native_session_binding_v1 *binding,
    uint32_t max_entities,
    uint16_t max_datagram_bytes,
    uint64_t now_tick);

bool Worr_NativeSnapshotSenderValidateV1(
    const worr_native_snapshot_sender_v1 *sender);

/*
 * Encodes and takes an immutable peer-owned copy of one exact canonical
 * projection.  Without an active dispatch it is immediately retained or
 * supersedes the resident snapshot.  During a dispatch it becomes the sole
 * pending snapshot; later calls atomically coalesce only to a strictly newer
 * canonical identity.  Duplicate/stale/conflicting inputs never alter payload
 * ownership or scheduling state.
 */
worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderQueueV1(
    worr_native_snapshot_sender_v1 *sender,
    const worr_snapshot_projection_view_v2 *view,
    uint64_t now_tick);

/*
 * Applies every exact ACK range in an admitted WTC1 packet transactionally.
 * A payload acknowledged during its active dispatch remains owned until that
 * dispatch retires; otherwise the exact bank is released immediately.
 */
worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderApplyAcksV1(
    worr_native_snapshot_sender_v1 *sender,
    const void *packet,
    size_t packet_bytes,
    uint32_t *acknowledged_out);

/* Non-mutating DATA scheduling query; ACK-only work is caller-owned. */
worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderDataDuePeekV1(
    const worr_native_snapshot_sender_v1 *sender,
    uint64_t now_tick,
    uint32_t resend_interval_ticks,
    bool *due_out);

/*
 * Builds one snapshot DATA fragment plus zero-to-seven due ranges from the
 * caller-owned ACK ledger.  Every successful Prepare must be terminated by
 * Confirm or Reject with the byte-identical packet.
 */
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
    uint64_t *token_out);

worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderConfirmMixedV1(
    worr_native_snapshot_sender_v1 *sender,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    uint64_t handoff_tick,
    const void *packet,
    size_t packet_bytes);

/* A definite transport rejection aborts the dispatch before pending promotion. */
worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderRejectMixedV1(
    worr_native_snapshot_sender_v1 *sender,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const void *packet,
    size_t packet_bytes);

/* Stops queueing/DATA while retaining sent payloads for exact late ACK release. */
worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderRetireV1(
    worr_native_snapshot_sender_v1 *sender);

/*
 * Terminal cancellation requires no prepared packet.  It aborts any known
 * dispatch, cancels the TX slot, releases both payload banks, and reports the
 * exact retained/pending ownership removed.  Success is idempotent.
 */
worr_native_snapshot_sender_result_v1 Worr_NativeSnapshotSenderCancelV1(
    worr_native_snapshot_sender_v1 *sender,
    worr_native_snapshot_sender_cancel_report_v1 *report_out);

bool Worr_NativeSnapshotSenderGetStatusV1(
    const worr_native_snapshot_sender_v1 *sender,
    worr_native_snapshot_sender_status_v1 *status_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS ==
                  (UINT32_C(1) <<
                   WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_INDEX_BITS),
              "native snapshot sender payload index width changed");
static_assert(sizeof(worr_native_snapshot_sender_cancel_report_v1) == 12,
              "native snapshot sender cancel report v1 layout changed");
#else
_Static_assert(WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_BANKS ==
                   (UINT32_C(1) <<
                    WORR_NATIVE_SNAPSHOT_SENDER_PAYLOAD_INDEX_BITS),
               "native snapshot sender payload index width changed");
_Static_assert(sizeof(worr_native_snapshot_sender_cancel_report_v1) == 12,
               "native snapshot sender cancel report v1 layout changed");
#endif
