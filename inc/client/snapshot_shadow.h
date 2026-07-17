/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_snapshot_admission.h"
#include "common/net/snapshot_q2proto.h"
#include "shared/cgame_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CL_SNAPSHOT_SHADOW_STATUS_VERSION 1u
/* Snapshot V2 player records currently require a controlled generation.  Use
 * this explicit value to audit and skip observer/cinematic frames without
 * fabricating an entity identity. */
#define CL_SNAPSHOT_SHADOW_NO_CONTROLLED_ENTITY UINT32_MAX

typedef enum cl_snapshot_shadow_lifecycle_v1_e {
    CL_SNAPSHOT_SHADOW_LIFECYCLE_NEVER_STARTED = 0,
    CL_SNAPSHOT_SHADOW_LIFECYCLE_DISABLED = 1,
    CL_SNAPSHOT_SHADOW_LIFECYCLE_ACTIVE = 2,
    CL_SNAPSHOT_SHADOW_LIFECYCLE_SHUTDOWN = 3,
    CL_SNAPSHOT_SHADOW_LIFECYCLE_EXHAUSTED = 4,
} cl_snapshot_shadow_lifecycle_v1;

typedef enum cl_snapshot_shadow_capture_failure_v1_e {
    CL_SNAPSHOT_SHADOW_CAPTURE_NONE = 0,
    CL_SNAPSHOT_SHADOW_CAPTURE_NULL_FRAME = 1,
    CL_SNAPSHOT_SHADOW_CAPTURE_AREA_CAPACITY = 2,
    CL_SNAPSHOT_SHADOW_CAPTURE_AREA_POINTER = 3,
    CL_SNAPSHOT_SHADOW_CAPTURE_NULL_DELTA = 4,
    CL_SNAPSHOT_SHADOW_CAPTURE_DELTA_CAPACITY = 5,
    CL_SNAPSHOT_SHADOW_CAPTURE_MISSING_TERMINATOR = 6,
    CL_SNAPSHOT_SHADOW_CAPTURE_NO_CONTROLLED_ENTITY = 7,
    CL_SNAPSHOT_SHADOW_CAPTURE_CONTROLLED_ENTITY_RANGE = 8,
    CL_SNAPSHOT_SHADOW_CAPTURE_CONSUMED_COMMAND = 9,
} cl_snapshot_shadow_capture_failure_v1;

/*
 * Result of looking up an independently legacy-qualified native snapshot
 * expectation.  AVAILABLE is the only result that writes a usable
 * expectation to the caller.
 */
typedef enum cl_snapshot_shadow_native_expectation_result_v1_e {
    CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE = 0,
    CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_PENDING = 1,
    CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_UNQUALIFIED = 2,
    CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_STALE = 3,
    CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_WRONG_EPOCH = 4,
    CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_INVALID = 5,
} cl_snapshot_shadow_native_expectation_result_v1;

enum {
    CL_SNAPSHOT_SHADOW_PARITY_METADATA = 1u << 0,
    CL_SNAPSHOT_SHADOW_PARITY_PLAYER = 1u << 1,
    CL_SNAPSHOT_SHADOW_PARITY_ENTITY_COUNT = 1u << 2,
    CL_SNAPSHOT_SHADOW_PARITY_ENTITY_PAYLOAD = 1u << 3,
    CL_SNAPSHOT_SHADOW_PARITY_AREA_COUNT = 1u << 4,
    CL_SNAPSHOT_SHADOW_PARITY_AREA_PAYLOAD = 1u << 5,
    CL_SNAPSHOT_SHADOW_PARITY_EVENT_COUNT = 1u << 6,
    CL_SNAPSHOT_SHADOW_PARITY_EVENT_PAYLOAD = 1u << 7,
    CL_SNAPSHOT_SHADOW_PARITY_HASH_BUILD = 1u << 8,
};

enum {
    /* Deliver only after the projection is independently parity-qualified. */
    CL_SNAPSHOT_SHADOW_ACCEPT_DELIVER_CONSUMER = 1u << 0,
    /* Compare the projection with the already accepted legacy cl.frame. */
    CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY = 1u << 1,
};

enum {
    /* Start a new canonical projector epoch.  Required for demo rewind, but
     * deliberately omitted for a forward seek so retained delta bases live. */
    CL_SNAPSHOT_SHADOW_RESET_PROJECTION_EPOCH = 1u << 0,
};

typedef struct cl_snapshot_shadow_status_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t active;
    uint32_t lifecycle;
    uint32_t snapshot_epoch;
    uint32_t pending_frame;
    uint32_t last_result;
    uint32_t last_capture_failure;
    uint32_t last_parity_mismatch;
    uint32_t last_accept_flags;
    uint32_t last_entity_count;
    uint32_t entity_high_water;
    uint32_t storage_entities_per_slot;
    uint32_t consumer_attached;
    uint32_t last_reset_reason;
    /* Timeline result captured immediately after the latest rejected
     * consumer callback. UINT32_MAX means the consumer status was unavailable. */
    uint32_t last_consumer_rejection_result;
    uint64_t connection_resets;
    uint64_t lifecycle_resets;
    uint64_t baseline_attempts;
    uint64_t baseline_failures;
    uint64_t frame_attempts;
    uint64_t frames_projected;
    uint64_t frames_published;
    uint64_t frames_lineage_only;
    uint64_t frames_promotion_eligible;
    uint64_t frames_without_controlled_entity;
    uint64_t frame_failures;
    uint64_t capture_overflows;
    uint64_t pending_aborts;
    uint64_t parity_comparisons;
    uint64_t parity_mismatches;
    uint64_t parity_metadata_mismatches;
    uint64_t parity_player_mismatches;
    uint64_t parity_entity_mismatches;
    uint64_t parity_area_mismatches;
    uint64_t parity_event_mismatches;
    uint64_t promotion_blocks;
    uint64_t consumer_attempts;
    uint64_t consumer_accepts;
    uint64_t consumer_rejections;
    uint64_t consumer_resets;
    uint64_t allocation_bytes;
    uint64_t last_endpoint_hash;
    uint64_t last_legacy_parity_hash;
    uint64_t last_legacy_observed_parity_hash;
} cl_snapshot_shadow_status_v1;

/* Start a fresh connection/map epoch after the final protocol limits are known. */
void CL_SnapshotShadowBeginConnection(uint32_t max_entities,
                                      uint32_t max_models,
                                      uint32_t max_sounds,
                                      bool extended_entity_state);
void CL_SnapshotShadowShutdown(void);
bool CL_SnapshotShadowNotifyReset(uint32_t reason, uint64_t host_time_us);
bool CL_SnapshotShadowNotifyResetEx(uint32_t reason,
                                    uint64_t host_time_us,
                                    uint32_t reset_flags);
/*
 * Rebind the projector to a server epoch learned from an independent
 * negotiated control path.  Native snapshot DATA must never call this API.
 * Existing map baselines are retained, while projected frames and native
 * expectations are invalidated transactionally.
 */
bool CL_SnapshotShadowBindNativeEpoch(uint32_t snapshot_epoch);

/* Capture exact public q2proto service values while the legacy parser owns them. */
void CL_SnapshotShadowSetBaseline(
    uint32_t entity_index,
    const q2proto_entity_state_delta_t *baseline_delta);
void CL_SnapshotShadowBeginFrame(const q2proto_svc_frame_t *frame);
bool CL_SnapshotShadowSetConsumedCommand(
    const worr_snapshot_consumed_command_v2 *consumed_command);
void CL_SnapshotShadowCaptureEntityDelta(
    const q2proto_svc_frame_entity_delta_t *delta);
void CL_SnapshotShadowAbortFrame(void);

/* Publish only after the legacy frame has been accepted and fully reconstructed. */
bool CL_SnapshotShadowAcceptFrame(uint64_t server_time_us,
                                  uint32_t controlled_entity_index,
                                  int32_t canonical_movement_type,
                                  uint16_t canonical_movement_flags,
                                  uint8_t team_id);
/*
 * A zero-flag accept still reconstructs and retains the frame so later wire
 * deltas keep a valid base, but neither reads cl.frame nor calls cgame.  This
 * is the required path while demo seeking or before normal presentation is
 * enabled.  The legacy comparison flag is valid only after cl.frame has been
 * installed as the accepted frame.  False means projection failed or a
 * requested promotion/consumer delivery was rejected; a zero-flag lineage
 * publication returns true after the projector commits it.
 */
bool CL_SnapshotShadowAcceptFrameEx(uint64_t server_time_us,
                                    uint32_t controlled_entity_index,
                                    int32_t canonical_movement_type,
                                    uint16_t canonical_movement_flags,
                                    uint8_t team_id,
                                    uint32_t accept_flags);
/* Qualify and deliver the most recent lineage-only projection after demo seek
 * presentation installs that same frame as cl.frame. */
bool CL_SnapshotShadowPromoteLatestFrame(
    uint32_t controlled_entity_index);

bool CL_SnapshotShadowLatest(
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out,
    worr_snapshot_ref_v2 *ref_out);
cl_snapshot_shadow_native_expectation_result_v1
CL_SnapshotShadowGetNativeExpectation(
    worr_snapshot_id_v2 snapshot_id,
    worr_native_snapshot_expectation_v1 *expectation_out);
bool CL_SnapshotShadowGetStatus(cl_snapshot_shadow_status_v1 *status_out);
void CL_SnapshotShadowStatus_f(void);
bool CL_SnapshotShadowSetConsumer(
    const worr_cgame_snapshot_timeline_export_v2 *consumer);
/*
 * Adapt the currently attached real cgame timeline to the transactional
 * native admission ABI.  The opaque lifetime cookie invalidates previously
 * exported adapters when cgame unloads or replaces its export.
 */
bool CL_SnapshotShadowGetNativeConsumerV1(
    worr_native_snapshot_consumer_v1 *consumer_out);

/* Stable simulation time for a wire frame, independent of render/seek time. */
bool CL_SnapshotShadowServerTimeUs(int32_t server_frame,
                                   uint32_t frame_time_ms,
                                   uint64_t *server_time_us_out);
/* Stateful clock: preserves continuity when SVS_FPS changes within a map. */
bool CL_SnapshotShadowResolveServerTimeUs(
    int32_t server_frame, uint32_t frame_time_ms,
    uint64_t *server_time_us_out);
/* Validated exact anchor used only by client-generated seek snapshots. */
bool CL_SnapshotShadowObserveExactServerTimeUs(
    int32_t server_frame, uint64_t server_time_us);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(offsetof(cl_snapshot_shadow_status_v1, connection_resets) == 64,
              "client snapshot shadow status header changed");
#else
_Static_assert(offsetof(cl_snapshot_shadow_status_v1, connection_resets) == 64,
               "client snapshot shadow status header changed");
#endif
