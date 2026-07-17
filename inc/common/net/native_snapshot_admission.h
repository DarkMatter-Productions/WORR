/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_carrier_ack.h"
#include "common/net/native_codec.h"
#include "shared/cgame_snapshot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transactional semantic admission for one completely reassembled native
 * canonical snapshot (FR-10-T04/T06).
 *
 * The adapter remains transport-hook-neutral and advertises no capability.
 * It decodes a complete canonical V2 view into caller-owned scratch, proves
 * exact transport-visible server/legacy-shadow parity, precommits RX plus
 * retained ACK state on private copies, synchronously feeds a snapshot
 * timeline, and publishes the transport commit only after fresh consumer
 * status proves the native endpoint's exact hash.
 */
#define WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION 1u

enum {
    WORR_NATIVE_SNAPSHOT_ADMISSION_INITIALIZED = 1u << 0,
    WORR_NATIVE_SNAPSHOT_ADMISSION_ACTIVE = 1u << 1,
    WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME = 1u << 2,
    WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED = 1u << 3,
};

/*
 * Pointer-free connection/snapshot-epoch owner.  A complete WNC1 snapshot is
 * a full canonical view: base_id remains lineage metadata and is never used
 * to reconstruct bytes in this admission layer.
 */
typedef struct worr_native_snapshot_admission_state_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint32_t transport_epoch;
    uint32_t active_snapshot_epoch;
    uint32_t highest_snapshot_epoch;
    uint32_t reserved0;
    worr_snapshot_id_v2 last_accepted_snapshot;
    uint64_t mutation_generation;
    uint64_t accepted_snapshots;
    uint64_t accepted_keyframes;
    uint64_t accepted_full_updates;
    uint64_t codec_rejections;
    uint64_t parity_rejections;
    uint64_t lineage_rejections;
    uint64_t keyframe_requests;
    uint64_t consumer_resyncs;
    uint64_t last_endpoint_hash;
    uint64_t last_legacy_parity_hash;
    uint64_t last_snapshot_hash;
    uint64_t last_receive_time_us;
    uint64_t connection_owner_id;
} worr_native_snapshot_admission_state_v1;

/*
 * Decode destinations are transient and caller-owned.  Zero ranges require
 * NULL plus zero capacity exactly, matching NativeCodecSnapshotDecode.
 */
typedef struct worr_native_snapshot_decode_storage_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    worr_snapshot_v2 *snapshot;
    worr_snapshot_player_v2 *player;
    worr_snapshot_entity_v2 *entities;
    uint8_t *area_bytes;
    worr_snapshot_event_ref_v2 *event_refs;
    uint32_t entity_capacity;
    uint32_t area_capacity;
    uint32_t event_ref_capacity;
    uint32_t reserved1;
} worr_native_snapshot_decode_storage_v1;

/*
 * Independent legacy-shadow observation for the same snapshot ID.  The
 * legacy-parity hash and four semantic component hashes are compared before
 * the consumer or transport commit is touched.  endpoint_hash is retained for
 * diagnostics but is intentionally endpoint-local: server authority and the
 * legacy reconstruction may differ in provenance/chronology while remaining
 * transport-semantically identical.
 */
typedef struct worr_native_snapshot_expectation_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    worr_snapshot_id_v2 snapshot_id;
    worr_snapshot_projection_hashes_v2 hashes;
} worr_native_snapshot_expectation_v1;

typedef void (*worr_native_snapshot_reset_v1)(
    void *opaque, uint32_t snapshot_epoch, uint32_t reason,
    uint64_t host_time_us);
typedef bool (*worr_native_snapshot_consume_v1)(
    void *opaque, const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_projection_hashes_v2 *hashes,
    uint64_t receive_time_us);
typedef bool (*worr_native_snapshot_get_status_v1)(
    void *opaque, worr_cgame_snapshot_timeline_status_v2 *status_out);

typedef struct worr_native_snapshot_consumer_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    void *opaque;
    worr_native_snapshot_reset_v1 Reset;
    worr_native_snapshot_consume_v1 ConsumeCanonicalSnapshot;
    worr_native_snapshot_get_status_v1 GetStatus;
} worr_native_snapshot_consumer_v1;

typedef enum worr_native_snapshot_admission_result_v1_e {
    WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_ACCEPTED = 0,
    WORR_NATIVE_SNAPSHOT_ADMISSION_FULL_UPDATE_ACCEPTED = 1,
    WORR_NATIVE_SNAPSHOT_ADMISSION_RETRY_UNCOMMITTED = 2,
    WORR_NATIVE_SNAPSHOT_ADMISSION_RESYNC_UNCOMMITTED = 3,
    WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_REQUIRED = 4,
    WORR_NATIVE_SNAPSHOT_ADMISSION_STALE = 5,
    WORR_NATIVE_SNAPSHOT_ADMISSION_WRONG_EPOCH = 6,
    WORR_NATIVE_SNAPSHOT_ADMISSION_PARITY_MISMATCH = 7,
    WORR_NATIVE_SNAPSHOT_ADMISSION_CAPACITY = 8,
    WORR_NATIVE_SNAPSHOT_ADMISSION_MALFORMED = 9,
    WORR_NATIVE_SNAPSHOT_ADMISSION_UNSUPPORTED = 10,
    WORR_NATIVE_SNAPSHOT_ADMISSION_NOT_READY = 11,
    WORR_NATIVE_SNAPSHOT_ADMISSION_NOT_NEGOTIATED = 12,
    WORR_NATIVE_SNAPSHOT_ADMISSION_CONFLICT = 13,
    WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_ARGUMENT = 14,
    WORR_NATIVE_SNAPSHOT_ADMISSION_INVALID_STATE = 15,
    WORR_NATIVE_SNAPSHOT_ADMISSION_REPEAT_REVALIDATED = 16,
} worr_native_snapshot_admission_result_v1;

/*
 * Fresh connection initialization.  WNC1 snapshots are complete reconstructed
 * views even when base_id is nonzero, so the first valid full update is
 * admissible; a keyframe barrier is armed only by explicit recovery policy or
 * semantic uncertainty.
 */
bool Worr_NativeSnapshotAdmissionInitV1(
    worr_native_snapshot_admission_state_v1 *state_out,
    const worr_native_session_binding_v1 *binding);
bool Worr_NativeSnapshotAdmissionValidateV1(
    const worr_native_snapshot_admission_state_v1 *state);

/*
 * Arms an explicit recovery barrier without changing owner/transport epoch or
 * the snapshot-epoch high-water.  This is the adapter hook for fragment loss,
 * map cancellation, or an independently detected parity failure.
 */
bool Worr_NativeSnapshotAdmissionRequireKeyframeV1(
    worr_native_snapshot_admission_state_v1 *state);

/*
 * Success commits state/session/slots/ACK ledger together.  Every rejection
 * before a consumer callback leaves session, slots, ledger, and consumer
 * untouched; decode scratch may contain the rejected candidate.  Counted
 * owner recovery telemetry and REQUIRE_KEYFRAME may advance.  If a callback
 * cannot be freshly proven, the consumer is hard-reset, the attempted epoch
 * is quarantined, no ACK is authorized, and RESYNC_UNCOMMITTED is returned.
 */
worr_native_snapshot_admission_result_v1
Worr_NativeSnapshotAdmissionCommitCompletedV1(
    worr_native_snapshot_admission_state_v1 *state,
    const worr_native_session_binding_v1 *binding,
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    const void *payload_arena,
    size_t payload_arena_bytes,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_rx_message_v1 *message,
    /* Exclusive valid entity-index limit; scratch owns record-count capacity. */
    uint32_t max_entities,
    uint64_t receive_time_us,
    const worr_native_snapshot_decode_storage_v1 *scratch,
    const worr_native_snapshot_expectation_v1 *expectation,
    const worr_native_snapshot_consumer_v1 *consumer);

/*
 * Revalidates one exact ALREADY_COMMITTED snapshot packet before rearming its
 * retained transport ACK.  No decode or consume callback is repeated; a fresh
 * exact cgame receipt for the owner's latest accepted snapshot is mandatory.
 */
worr_native_snapshot_admission_result_v1
Worr_NativeSnapshotAdmissionRevalidateCommittedRepeatV1(
    worr_native_snapshot_admission_state_v1 *state,
    const worr_native_session_binding_v1 *binding,
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    void *payload_arena,
    size_t payload_arena_bytes,
    uint64_t now_tick,
    const void *packet,
    size_t packet_bytes,
    uint16_t entry_index,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_snapshot_consumer_v1 *consumer);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_SNAPSHOT_ADMISSION_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_SNAPSHOT_ADMISSION_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_SNAPSHOT_ADMISSION_STATIC_ASSERT(
    sizeof(worr_native_snapshot_admission_state_v1) == 144,
    "native snapshot admission state layout changed");
WORR_NATIVE_SNAPSHOT_ADMISSION_STATIC_ASSERT(
    offsetof(worr_native_snapshot_admission_state_v1,
             last_accepted_snapshot) == 24,
    "native snapshot admission identity offset changed");
WORR_NATIVE_SNAPSHOT_ADMISSION_STATIC_ASSERT(
    offsetof(worr_native_snapshot_admission_state_v1,
             connection_owner_id) == 136,
    "native snapshot admission owner offset changed");

#undef WORR_NATIVE_SNAPSHOT_ADMISSION_STATIC_ASSERT
