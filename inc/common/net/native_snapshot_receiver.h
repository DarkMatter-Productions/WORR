/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_snapshot_admission.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bounded transport-neutral owner for server-originated canonical snapshot
 * DATA.  The owner deliberately separates transport completion from semantic
 * admission: native fragments may complete before the authoritative legacy
 * frame has supplied its independently parity-qualified expectation.
 */
#define WORR_NATIVE_SNAPSHOT_RECEIVER_ABI_VERSION 1u
#define WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY 2u
#define WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY 16u
#define WORR_NATIVE_SNAPSHOT_RECEIVER_PAYLOAD_STRIDE \
    WORR_NATIVE_CODEC_MAX_ENCODED_BYTES
#define WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_TIMEOUT_TICKS 1000u
#define WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_TIMEOUT_TICKS 1000u
#define WORR_NATIVE_SNAPSHOT_RECEIVER_ACK_PROACTIVE_HANDOFFS 3u

enum {
    WORR_NATIVE_SNAPSHOT_RECEIVER_INITIALIZED = 1u << 0,
    WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED = 1u << 1,
    WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED = 1u << 2,
};

typedef struct worr_native_snapshot_receiver_pending_v1_s {
    uint32_t occupied;
    uint32_t reserved0;
    uint64_t receive_time_us;
    worr_native_rx_message_v1 message;
} worr_native_snapshot_receiver_pending_v1;

typedef struct worr_native_snapshot_receiver_expectation_slot_v1_s {
    uint32_t occupied;
    uint32_t reserved0;
    worr_native_snapshot_expectation_v1 expectation;
} worr_native_snapshot_receiver_expectation_slot_v1;

typedef struct worr_native_snapshot_receiver_telemetry_v1_s {
    uint64_t packets_observed;
    uint64_t fragments_accepted;
    uint64_t fragment_duplicates;
    uint64_t messages_completed;
    uint64_t completions_deferred;
    uint64_t expectations_observed;
    uint64_t expectations_cached;
    uint64_t snapshots_admitted;
    uint64_t repeats_revalidated;
    uint64_t retry_deferrals;
    uint64_t capacity_stalls;
    uint64_t semantic_rejections;
    uint64_t quarantines;
    uint64_t cancelled_messages;
    uint64_t cancelled_receipts;
} worr_native_snapshot_receiver_telemetry_v1;

typedef struct worr_native_snapshot_receiver_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint32_t snapshot_epoch;
    /* Exclusive valid entity-index limit, not decoded entity storage count. */
    uint32_t max_entities;
    uint32_t last_result;
    uint32_t reserved0;
    worr_native_session_binding_v1 binding;
    worr_native_snapshot_admission_state_v1 admission;
    worr_native_rx_session_v1 rx;
    worr_native_rx_slot_v1
        rx_slots[WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY];
    worr_native_carrier_ack_ledger_v1 ack_ledger;
    worr_native_snapshot_receiver_pending_v1
        pending[WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY];
    worr_native_snapshot_receiver_expectation_slot_v1
        expectations[WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY];
    worr_native_snapshot_decode_storage_v1 scratch;
    worr_native_snapshot_consumer_v1 consumer;
    worr_native_snapshot_receiver_telemetry_v1 telemetry;
    worr_snapshot_v2 decoded_snapshot;
    worr_snapshot_player_v2 decoded_player;
    worr_snapshot_entity_v2
        decoded_entities[WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES];
    uint8_t decoded_area[WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES];
    worr_snapshot_event_ref_v2
        decoded_events[WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS];
    uint8_t payload_arena[
        WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY *
        WORR_NATIVE_SNAPSHOT_RECEIVER_PAYLOAD_STRIDE];
} worr_native_snapshot_receiver_v1;

typedef enum worr_native_snapshot_receiver_result_v1_e {
    WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED = 0,
    WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_DUPLICATE = 1,
    WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_PENDING = 2,
    WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED = 3,
    WORR_NATIVE_SNAPSHOT_RECEIVER_REPEAT_REVALIDATED = 4,
    WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED = 5,
    WORR_NATIVE_SNAPSHOT_RECEIVER_RETRY_LATER = 6,
    WORR_NATIVE_SNAPSHOT_RECEIVER_CAPACITY = 7,
    WORR_NATIVE_SNAPSHOT_RECEIVER_STALE = 8,
    WORR_NATIVE_SNAPSHOT_RECEIVER_WRONG_EPOCH = 9,
    WORR_NATIVE_SNAPSHOT_RECEIVER_PARITY_MISMATCH = 10,
    WORR_NATIVE_SNAPSHOT_RECEIVER_KEYFRAME_REQUIRED = 11,
    WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED_RESULT = 12,
    WORR_NATIVE_SNAPSHOT_RECEIVER_UNSUPPORTED = 13,
    WORR_NATIVE_SNAPSHOT_RECEIVER_MALFORMED = 14,
    WORR_NATIVE_SNAPSHOT_RECEIVER_CONFLICT = 15,
    WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT = 16,
    WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_ARGUMENT = 17,
    WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_STATE = 18,
} worr_native_snapshot_receiver_result_v1;

bool Worr_NativeSnapshotReceiverInitV1(
    worr_native_snapshot_receiver_v1 *receiver,
    const worr_native_session_binding_v1 *binding,
    uint32_t snapshot_epoch,
    /* Exclusive valid entity-index limit (1024/8192 in live Q2 profiles). */
    uint32_t max_entities,
    const worr_native_snapshot_consumer_v1 *consumer);

bool Worr_NativeSnapshotReceiverValidateV1(
    const worr_native_snapshot_receiver_v1 *receiver);

/*
 * Admits exactly one DATA entry from an already admitted sequenced-netchan
 * packet.  Fragment and complete transport state is retained even when the
 * matching legacy expectation has not arrived.  An exact committed repeat
 * is revalidated against the live consumer before its ACK is rearmed.
 */
worr_native_snapshot_receiver_result_v1
Worr_NativeSnapshotReceiverAcceptDataV1(
    worr_native_snapshot_receiver_v1 *receiver,
    uint64_t now_tick,
    uint64_t receive_time_us,
    const void *packet,
    size_t packet_bytes,
    uint16_t entry_index);

/*
 * Stores one independently parity-qualified legacy observation and pumps an
 * already-complete native message for the same exact ID if present.  Either
 * arrival order is supported; no ACK is authorized until admission succeeds.
 * Expectation-only traffic retains a rolling newest window.  Entries tied to
 * occupied native RX are protected from eviction, and bounded cache pressure
 * is a retry deferral rather than a semantic failure.
 */
worr_native_snapshot_receiver_result_v1
Worr_NativeSnapshotReceiverObserveExpectationV1(
    worr_native_snapshot_receiver_v1 *receiver,
    const worr_native_snapshot_expectation_v1 *expectation);

/* Explicit semantic uncertainty barrier used by the live adapter when the
 * legacy projector reports an unqualified/conflicting expectation. */
worr_native_snapshot_receiver_result_v1
Worr_NativeSnapshotReceiverQuarantineV1(
    worr_native_snapshot_receiver_v1 *receiver);

/*
 * Cancels uncommitted reassembly and every retained receipt.  A prepared ACK
 * handoff is an unknown transport outcome and makes cancellation fail without
 * mutation.  Success is terminal until a fresh owner is initialized.
 */
worr_native_snapshot_receiver_result_v1
Worr_NativeSnapshotReceiverCancelV1(
    worr_native_snapshot_receiver_v1 *receiver,
    uint32_t *cancelled_messages_out,
    uint32_t *cancelled_receipts_out);

#ifdef __cplusplus
}
#endif
