/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/capability.h"
#include "common/net/native_readiness.h"
#include "shared/native_envelope.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Isolated FR-10-T04 session/retention foundation.  This API is not a live
 * network adapter and does not advertise WORR_NET_CAP_NATIVE_ENVELOPE_V1.
 * Canonical payloads remain opaque and caller-owned throughout.
 */
#define WORR_NATIVE_SESSION_ABI_VERSION 1u
#define WORR_NATIVE_SESSION_MAX_TX_SLOTS 64u
#define WORR_NATIVE_SESSION_MAX_RX_SLOTS 16u
#define WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY 64u
#define WORR_NATIVE_SESSION_SNAPSHOT_TOMBSTONE_CAPACITY 16u

typedef struct worr_native_session_binding_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    uint32_t transport_epoch;
    uint32_t negotiated_capabilities;
    uint64_t connection_owner_id;
} worr_native_session_binding_v1;

/* Inclusive, exact acknowledgement range.  No sequence outside the range is
 * implied to have arrived.  Single-message receipts use first == last.
 * connection_owner_id is process-local provenance and is never serialized. */
typedef struct worr_native_ack_range_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    uint32_t transport_epoch;
    uint32_t first_message_sequence;
    uint32_t last_message_sequence;
    uint32_t reserved1;
    uint64_t connection_owner_id;
} worr_native_ack_range_v1;

/* connection_owner_id must be nonzero and unique for this local connection
 * incarnation.  Do not reuse it while any binding-derived object or copied
 * token from an earlier incarnation can survive; monotonically allocated
 * process-local IDs are recommended.  It is not a wire value and must remain
 * stable while one session advances epochs.  Init creates fresh objects;
 * AdvanceEpoch is the only in-incarnation reset. */
bool Worr_NativeSessionBindingInitV1(
    worr_native_session_binding_v1 *binding_out,
    const worr_net_capability_state_v1 *capability,
    uint64_t connection_owner_id);

/*
 * Initializes a session binding from the private endpoint-readiness proof.
 * The official public capability tuple may remain legacy-only; it is neither
 * accepted nor modified here.  The caller must bind readiness to that public
 * policy before completing the readiness exchange.
 *
 * Readiness must be structurally valid and role-correctly final: SERVER_ACTIVE
 * for a server or CLIENT_ACTIVE for a client.  Its capability mask must contain
 * only known bits and include WORR_NET_CAP_NATIVE_ENVELOPE_V1.  The binding
 * copies the private transport epoch and capability mask exactly.  Input and
 * output must be disjoint.  Every failure leaves binding_out byte-identical.
 */
bool Worr_NativeSessionBindingInitFromReadinessV1(
    worr_native_session_binding_v1 *binding_out,
    const worr_native_readiness_state_v1 *readiness,
    uint64_t connection_owner_id);

/*
 * Receive-side variant for the server-DATA confirmation barrier.  In
 * addition to either final ACTIVE phase, this accepts only a server in
 * SERVER_WAIT_CLIENT_ACTIVE_CONFIRM for an event-stream or canonical-snapshot
 * binding whose mutating readiness RX gate succeeds at now_tick.  That narrow
 * phase is RX-safe because CLIENT_ACTIVE_CONFIRM may share its legacy packet
 * with client-originated native DATA; it is never a TX authorization.  The
 * input readiness object is copied before the deadline check and remains
 * unchanged.  Existing active-only callers should continue to use
 * InitFromReadinessV1.
 */
bool Worr_NativeSessionBindingInitReceiveFromReadinessV1(
    worr_native_session_binding_v1 *binding_out,
    const worr_native_readiness_state_v1 *readiness,
    uint64_t connection_owner_id,
    uint64_t now_tick);
bool Worr_NativeSessionBindingValidateV1(
    const worr_native_session_binding_v1 *binding);
bool Worr_NativeAckRangeValidateV1(
    const worr_native_ack_range_v1 *acknowledgement);

enum {
    WORR_NATIVE_TX_INITIALIZED = 1u << 0,
    WORR_NATIVE_TX_SEQUENCE_EXHAUSTED = 1u << 1,
    WORR_NATIVE_TX_TERMINAL_CANCELLED = 1u << 2,
    WORR_NATIVE_TX_SLOT_OCCUPIED = 1u << 0,
};

typedef struct worr_native_tx_slot_v1_s {
    worr_native_record_ref_v1 record;
    uint32_t message_sequence;
    uint32_t payload_handle;
    uint32_t payload_bytes;
    uint16_t fragment_stride;
    uint16_t fragment_count;
    uint64_t enqueue_tick;
    uint64_t last_send_tick;
    uint64_t enqueue_dispatch;
    uint32_t send_attempts;
    uint16_t state_flags;
    uint8_t priority;
    uint8_t reserved0;
} worr_native_tx_slot_v1;

enum {
    WORR_NATIVE_TX_SEND_TICKET_INITIALIZED = 1u << 0,
};

/*
 * Pointer-free proof of one non-mutating scheduler selection.  The retained
 * slot is copied before any send-attempt mutation so a transport can validate
 * the selection between fragments and commit it only after the complete
 * burst has been accepted for transmission.
 */
typedef struct worr_native_tx_send_ticket_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint32_t transport_epoch;
    uint16_t slot_index;
    uint16_t reserved0;
    uint64_t selection_tick;
    uint32_t resend_interval_ticks;
    uint32_t reserved1;
    worr_native_tx_slot_v1 pre_send_slot;
    uint64_t connection_owner_id;
} worr_native_tx_send_ticket_v1;

/* Every telemetry counter saturates at UINT64_MAX. */
typedef struct worr_native_tx_telemetry_v1_s {
    uint64_t enqueue_attempts;
    uint64_t retained;
    uint64_t superseded_snapshots;
    uint64_t duplicates;
    uint64_t conflicts;
    uint64_t stale_snapshots;
    uint64_t capacity_stalls;
    uint64_t receipt_window_stalls;
    uint64_t sequence_exhaustions;
    uint64_t select_attempts;
    uint64_t selected_first_sends;
    uint64_t selected_retries;
    uint64_t scheduler_rebases;
    uint64_t not_due;
    uint64_t acknowledgement_attempts;
    uint64_t acknowledged_reliable;
    uint64_t acknowledged_snapshots;
    uint64_t acknowledgement_empty;
    uint64_t clock_regressions;
    uint64_t wrong_epoch;
} worr_native_tx_telemetry_v1;

/* Pointer-free retained state.  Slot storage is supplied on every call. */
typedef struct worr_native_tx_session_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint32_t transport_epoch;
    uint16_t slot_capacity;
    uint16_t retained_count;
    uint32_t next_message_sequence;
    uint32_t latest_snapshot_epoch;
    uint32_t latest_snapshot_sequence;
    uint32_t reserved0;
    uint64_t last_tick;
    uint64_t dispatch_count;
    worr_native_tx_telemetry_v1 telemetry;
    uint64_t connection_owner_id;
} worr_native_tx_session_v1;

typedef enum worr_native_tx_result_v1_e {
    WORR_NATIVE_TX_RETAINED = 0,
    WORR_NATIVE_TX_SUPERSEDED = 1,
    WORR_NATIVE_TX_DUPLICATE = 2,
    WORR_NATIVE_TX_SELECTED = 3,
    WORR_NATIVE_TX_NOT_DUE = 4,
    WORR_NATIVE_TX_ACKNOWLEDGED = 5,
    WORR_NATIVE_TX_ACKNOWLEDGEMENT_EMPTY = 6,
    WORR_NATIVE_TX_STALE_SNAPSHOT = 7,
    WORR_NATIVE_TX_CONFLICT = 8,
    WORR_NATIVE_TX_CAPACITY = 9,
    WORR_NATIVE_TX_RECEIPT_WINDOW = 10,
    WORR_NATIVE_TX_SEQUENCE_EXHAUSTED_RESULT = 11,
    WORR_NATIVE_TX_WRONG_EPOCH = 12,
    WORR_NATIVE_TX_CLOCK_REGRESSION = 13,
    WORR_NATIVE_TX_INVALID_ARGUMENT = 14,
    WORR_NATIVE_TX_INVALID_STATE = 15,
    WORR_NATIVE_TX_CANCELLED = 16,
} worr_native_tx_result_v1;

/* Invalid initialization leaves both objects byte-identical. */
bool Worr_NativeTxSessionInitV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_session_binding_v1 *binding);

/* A live connection would use this only for a strictly newer negotiated
 * epoch.  Fresh reconnect objects may call InitV1 even when a restarted
 * server has allocated a numerically lower epoch. */
bool Worr_NativeTxSessionAdvanceEpochV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_session_binding_v1 *binding);

bool Worr_NativeTxSessionValidateV1(
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity);

/* Commands and events are reliable retained records.  Snapshots are the sole
 * supersedable class: a strictly newer canonical snapshot atomically replaces
 * the resident snapshot without evicting any reliable record.  Payload
 * handles are nonzero process-local caller handles; payload bytes are never
 * inspected or copied.  max_datagram_bytes freezes the fragmentation plan
 * for the message sequence.  Every send and retry must initialize the native
 * envelope fragmenter with fragment_stride + wire-header bytes from the
 * selected slot.  message_sequence_out is untouched on failure. */
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
    uint32_t *message_sequence_out);

/* Selects the most urgent due retained message, then the lowest transport
 * sequence.  The same eight-dispatch priority-aging quantum as the envelope
 * queue bounds a priority-7 record's starvation to 56 intervening dispatches.
 * Selection records a send attempt; slot_out must not alias the session or
 * slot array and is untouched on failure/not-due. */
worr_native_tx_result_v1 Worr_NativeTxSessionSelectDueV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint64_t now_tick,
    uint32_t resend_interval_ticks,
    worr_native_tx_slot_v1 *slot_out);

/*
 * Selects the same due record as SelectDueV1 without changing session or slot
 * storage.  The ticket contains the exact pre-send slot representation and is
 * untouched for every result except SELECTED.  A prepared ticket owns no
 * payload and does not itself record a send attempt.
 */
worr_native_tx_result_v1 Worr_NativeTxSessionPrepareDueV1(
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint64_t now_tick,
    uint32_t resend_interval_ticks,
    worr_native_tx_send_ticket_v1 *ticket_out);

/*
 * Revalidates the ticket header, epoch, due baseline, and byte-exact retained
 * slot without consuming it.  A transport may call this between fragments.
 */
bool Worr_NativeTxSessionPreparedValidateV1(
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_tx_send_ticket_v1 *ticket);

/*
 * Records a prepared send only after its complete transport burst has been
 * accepted.  completion_tick must be no earlier than both selection_tick and
 * the session clock.  The exact pre-send slot must still be resident.  A
 * successful confirmation always changes that slot, so replaying the same
 * immutable ticket is rejected.  Scheduler rebase, dispatch aging, attempt
 * timestamps, and telemetry match SelectDueV1's selected path.
 */
worr_native_tx_result_v1 Worr_NativeTxSessionConfirmPreparedV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_tx_send_ticket_v1 *ticket,
    uint64_t completion_tick);

/* Removes only retained messages named by the inclusive exact range.
 * acked_count_out is written for ACKNOWLEDGED and ACKNOWLEDGEMENT_EMPTY only. */
worr_native_tx_result_v1 Worr_NativeTxSessionApplyAckV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_ack_range_v1 *acknowledgement,
    uint32_t *acked_count_out);

/*
 * Explicitly disposes every retained TX slot without changing connection
 * owner, transport epoch, sequence/snapshot high-water marks, scheduler
 * clock, dispatch high-water, or telemetry.  The session core has no mutable
 * prepare token: a non-mutating send ticket becomes stale after success.
 * The resulting session is terminal until AdvanceEpoch reinitializes it.
 * Adapters with a carrier dispatch/completion gate must first prove that gate
 * has no packet whose transport outcome is unknown.  Success is idempotent,
 * returns CANCELLED, and writes the exact number of released slots.
 */
worr_native_tx_result_v1 Worr_NativeTxSessionCancelRetainedV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint32_t *cancelled_count_out);

enum {
    WORR_NATIVE_RX_INITIALIZED = 1u << 0,
    WORR_NATIVE_RX_TERMINAL_CANCELLED = 1u << 1,
    WORR_NATIVE_RX_SLOT_OCCUPIED = 1u << 0,
    WORR_NATIVE_RX_SLOT_COMPLETE = 1u << 1,
    WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY = 1u << 2,
};

typedef struct worr_native_rx_slot_v1_s {
    worr_native_envelope_reassembly_v1 reassembly;
    uint64_t first_fragment_tick;
    uint64_t last_activity_tick;
    uint32_t state_flags;
    uint32_t reserved0;
} worr_native_rx_slot_v1;

typedef struct worr_native_receipt_history_entry_v1_s {
    worr_native_record_ref_v1 record;
    uint32_t message_sequence;
    uint32_t total_payload_bytes;
    uint32_t payload_crc32;
    uint16_t fragment_stride;
    uint16_t fragment_count;
    uint8_t priority;
    uint8_t reserved0[3];
} worr_native_receipt_history_entry_v1;

/* Exact retry or committed-ACK identity retained outside active slots. */
enum {
    WORR_NATIVE_SNAPSHOT_IDENTITY_RETRY = 1u << 0,
    WORR_NATIVE_SNAPSHOT_IDENTITY_COMMITTED = 1u << 1,
};

typedef struct worr_native_snapshot_identity_v1_s {
    worr_native_record_ref_v1 record;
    uint32_t message_sequence;
    uint32_t total_payload_bytes;
    uint32_t payload_crc32;
    uint16_t fragment_stride;
    uint16_t fragment_count;
    uint8_t priority;
    uint8_t state_flags;
    uint8_t reserved0[2];
} worr_native_snapshot_identity_v1;

typedef struct worr_native_rx_telemetry_v1_s {
    uint64_t datagram_attempts;
    uint64_t fragments_accepted;
    uint64_t fragment_duplicates;
    uint64_t messages_completed;
    uint64_t already_committed;
    uint64_t malformed;
    uint64_t unsupported;
    uint64_t datagram_corrupt;
    uint64_t wrong_epoch;
    uint64_t message_conflicts;
    uint64_t duplicate_conflicts;
    uint64_t message_checksum_failures;
    uint64_t capacity_stalls;
    uint64_t storage_stalls;
    uint64_t commits;
    uint64_t discards;
    uint64_t fragment_timeouts;
    uint64_t complete_timeouts;
    uint64_t clock_regressions;
    uint64_t stale_replays;
    uint64_t stale_snapshots;
    uint64_t superseded_snapshots;
    uint64_t repeat_acknowledgements;
    uint64_t snapshot_tombstone_evictions;
} worr_native_rx_telemetry_v1;

typedef struct worr_native_rx_session_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint32_t transport_epoch;
    uint16_t slot_capacity;
    uint16_t occupied_count;
    uint32_t payload_stride;
    uint32_t fragment_timeout_ticks;
    uint32_t complete_timeout_ticks;
    uint16_t history_count;
    uint16_t reserved0;
    uint32_t highest_committed_sequence;
    uint32_t committed_snapshot_epoch;
    uint32_t committed_snapshot_sequence;
    uint16_t snapshot_tombstone_count;
    uint16_t reserved1;
    uint64_t receipt_mask;
    uint64_t last_tick;
    worr_native_receipt_history_entry_v1
        history[WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY];
    worr_native_snapshot_identity_v1
        snapshot_tombstones[WORR_NATIVE_SESSION_SNAPSHOT_TOMBSTONE_CAPACITY];
    worr_native_rx_telemetry_v1 telemetry;
    uint64_t connection_owner_id;
} worr_native_rx_session_v1;

/* Describes caller-owned bytes only while the named slot remains complete. */
typedef struct worr_native_rx_message_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    uint32_t slot_index;
    uint32_t payload_offset;
    worr_native_record_ref_v1 record;
    uint32_t transport_epoch;
    uint32_t message_sequence;
    uint32_t payload_bytes;
    uint32_t payload_crc32;
    uint32_t reserved1;
    uint64_t connection_owner_id;
} worr_native_rx_message_v1;

/* Counted terminal disposition for unresolved receive-side reassembly. */
typedef struct worr_native_rx_cancel_report_v1_s {
    uint32_t incomplete_messages;
    uint32_t complete_messages;
} worr_native_rx_cancel_report_v1;

typedef enum worr_native_rx_result_v1_e {
    WORR_NATIVE_RX_FRAGMENT_ACCEPTED = 0,
    WORR_NATIVE_RX_FRAGMENT_DUPLICATE = 1,
    WORR_NATIVE_RX_MESSAGE_COMPLETE = 2,
    WORR_NATIVE_RX_ALREADY_COMMITTED = 3,
    WORR_NATIVE_RX_COMMITTED = 4,
    WORR_NATIVE_RX_DISCARDED = 5,
    WORR_NATIVE_RX_EXPIRED = 6,
    WORR_NATIVE_RX_IDLE = 7,
    WORR_NATIVE_RX_NOT_FOUND = 8,
    WORR_NATIVE_RX_NOT_COMPLETE = 9,
    WORR_NATIVE_RX_CAPACITY = 10,
    WORR_NATIVE_RX_STORAGE_CAPACITY = 11,
    WORR_NATIVE_RX_WRONG_EPOCH = 12,
    WORR_NATIVE_RX_CLOCK_REGRESSION = 13,
    WORR_NATIVE_RX_MALFORMED = 14,
    WORR_NATIVE_RX_UNSUPPORTED = 15,
    WORR_NATIVE_RX_DATAGRAM_CORRUPT = 16,
    WORR_NATIVE_RX_MESSAGE_CONFLICT = 17,
    WORR_NATIVE_RX_DUPLICATE_CONFLICT = 18,
    WORR_NATIVE_RX_MESSAGE_CHECKSUM = 19,
    WORR_NATIVE_RX_STALE_REPLAY = 20,
    WORR_NATIVE_RX_STALE_SNAPSHOT = 21,
    WORR_NATIVE_RX_INVALID_ARGUMENT = 22,
    WORR_NATIVE_RX_INVALID_STATE = 23,
    WORR_NATIVE_RX_SEMANTIC_ADMISSION_REQUIRED = 24,
    WORR_NATIVE_RX_CANCELLED = 25,
} worr_native_rx_result_v1;

bool Worr_NativeRxSessionInitV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint32_t payload_stride,
    uint32_t fragment_timeout_ticks,
    uint32_t complete_timeout_ticks,
    const worr_native_session_binding_v1 *binding);

bool Worr_NativeRxSessionAdvanceEpochV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    const worr_native_session_binding_v1 *binding);

bool Worr_NativeRxSessionValidateV1(
    const worr_native_rx_session_v1 *session,
    const worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity);

/*
 * Explicitly disposes all incomplete and complete-but-uncommitted RX slots.
 * Committed receipt history, snapshot tombstones, semantic high-water marks,
 * owner/epoch provenance, clocks, configuration, and telemetry are retained.
 * The resulting session rejects all receive/commit work until AdvanceEpoch;
 * success is idempotent, returns CANCELLED, and reports both dispositions.
 */
worr_native_rx_result_v1 Worr_NativeRxSessionCancelPendingV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    worr_native_rx_cancel_report_v1 *report_out);

/* The payload arena is caller-owned and divided into slot_capacity regions of
 * payload_stride bytes.  A malformed, unsupported, corrupt, wrong-epoch, or
 * alias-rejected datagram never changes slots or payload bytes.  Deterministic
 * rejection telemetry may be the sole session-state change.
 *
 * On ALREADY_COMMITTED, repeat_acknowledgement_out receives the exact
 * one-message acknowledgement so loss of the first commit ACK cannot retain a
 * reliable TX record forever.  It is untouched for every other result.
 * message_out is written only for MESSAGE_COMPLETE.  Both outputs are
 * required, pairwise distinct, and must not alias any input/state/storage. */
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
    worr_native_ack_range_v1 *repeat_acknowledgement_out);

/* Commit only after the canonical consumer has accepted the opaque payload.
 * Commit emits a single-message exact acknowledgement, records replay
 * identity, and releases the slot atomically. */
worr_native_rx_result_v1 Worr_NativeRxSessionCommitV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint32_t slot_index,
    uint32_t message_sequence,
    worr_native_ack_range_v1 *acknowledgement_out);

/* Discard releases without acknowledging, allowing a reliable resend. */
worr_native_rx_result_v1 Worr_NativeRxSessionDiscardV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint32_t slot_index,
    uint32_t message_sequence);

/* Caller-tick-driven lifecycle; incomplete and complete slots use independent
 * timeouts.  expired_count_out is written on EXPIRED and IDLE only. */
worr_native_rx_result_v1 Worr_NativeRxSessionExpireV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint64_t now_tick,
    uint32_t *expired_count_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_SESSION_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_SESSION_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_SESSION_STATIC_ASSERT(
    sizeof(worr_native_session_binding_v1) == 24,
    "native session binding v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_session_binding_v1, connection_owner_id) == 16,
    "native session binding owner offset changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(sizeof(worr_native_ack_range_v1) == 32,
                                  "native ack range v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_ack_range_v1, connection_owner_id) == 24,
    "native ack range owner offset changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(sizeof(worr_native_tx_slot_v1) == 64,
                                  "native tx slot v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_tx_slot_v1, enqueue_tick) == 32,
    "native tx enqueue tick offset changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    sizeof(worr_native_tx_send_ticket_v1) == 104,
    "native tx send ticket v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_tx_send_ticket_v1, pre_send_slot) == 32,
    "native tx send ticket slot offset changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_tx_send_ticket_v1, connection_owner_id) == 96,
    "native tx send ticket owner offset changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    sizeof(worr_native_tx_telemetry_v1) == 160,
    "native tx telemetry v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(sizeof(worr_native_tx_session_v1) == 216,
                                  "native tx session v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_tx_session_v1, connection_owner_id) == 208,
    "native tx session owner offset changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(sizeof(worr_native_rx_slot_v1) == 88,
                                  "native rx slot v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    sizeof(worr_native_receipt_history_entry_v1) == 32,
    "native receipt history entry v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    sizeof(worr_native_snapshot_identity_v1) == 32,
    "native snapshot identity v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    sizeof(worr_native_rx_telemetry_v1) == 192,
    "native rx telemetry v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(sizeof(worr_native_rx_session_v1) == 2824,
                                  "native rx session v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_rx_session_v1, connection_owner_id) == 2816,
    "native rx session owner offset changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(sizeof(worr_native_rx_message_v1) == 56,
                                  "native rx message v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    sizeof(worr_native_rx_cancel_report_v1) == 8,
    "native rx cancel report v1 layout changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_rx_message_v1, connection_owner_id) == 48,
    "native rx message owner offset changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_rx_session_v1, history) == 64,
    "native rx history offset changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_rx_session_v1, snapshot_tombstones) == 2112,
    "native rx snapshot tombstone offset changed");
WORR_NATIVE_SESSION_STATIC_ASSERT(
    offsetof(worr_native_rx_session_v1, telemetry) == 2624,
    "native rx telemetry offset changed");

#undef WORR_NATIVE_SESSION_STATIC_ASSERT
