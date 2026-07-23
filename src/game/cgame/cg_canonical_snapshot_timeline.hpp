/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_codec.h"
#include "shared/cgame_snapshot.h"

#include <cstdint>

/*
 * This is a cgame-private migration surface.  It never exposes the timeline
 * owner or borrowed projection pointers.  Render and prediction paths may
 * progressively consume copied snapshots, sampled entities and event
 * observations while the established legacy paths remain authoritative.
 */

constexpr std::uint32_t CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES = 8192u;
/* Fixed, value-owned timeline bounds.  Sixty-four snapshot slots retain 63
 * intervals, or 1008 ms at the maximum effective 16 ms (~62 Hz) server
 * cadence, covering the explicit one-second interpolation-delay bound.  The
 * per-snapshot payload capacities cover the native codec acceptance envelope;
 * oversized projections are rejected with a visible CAPACITY result/counter.
 * The larger value-owned ring and its linear scans are intentional correctness
 * costs here; indexed conflict/identity lookup remains performance debt. */
constexpr std::uint32_t CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY = 64u;
constexpr std::uint32_t CG_CANONICAL_SNAPSHOT_TIMELINE_ENTITY_CAPACITY = 512u;
constexpr std::uint32_t CG_CANONICAL_SNAPSHOT_TIMELINE_AREA_CAPACITY =
    WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES;
constexpr std::uint32_t CG_CANONICAL_SNAPSHOT_TIMELINE_EVENT_CAPACITY =
    CG_CANONICAL_SNAPSHOT_TIMELINE_ENTITY_CAPACITY;
constexpr std::uint32_t CG_CANONICAL_PREDICTION_RECEIPT_VERSION = 1u;
constexpr std::uint32_t CG_CANONICAL_PREDICTION_SNAPSHOT_VERSION = 2u;

/*
 * Immutable proof that one exact timeline generation passed both canonical
 * publication and the snapshot-event fence.  Prediction never derives this
 * receipt from mutable "latest" status and never accepts a timeline-only
 * publication as movement authority.
 */
struct cg_canonical_prediction_receipt_v1 {
    std::uint32_t struct_size;
    std::uint32_t schema_version;
    std::uint64_t admission_generation;
    std::uint32_t receipt_flags;
    std::uint32_t reserved0;
    worr_snapshot_timeline_ref_v1 ref;
    worr_snapshot_id_v2 snapshot_id;
    std::uint64_t snapshot_hash;
    worr_snapshot_consumed_command_v2 consumed_command;
    std::uint32_t server_tick;
    std::uint32_t controlled_entity_index;
    std::uint32_t controlled_entity_generation;
    std::uint32_t controlled_entity_provenance;
    std::uint64_t server_time_us;
};

/*
 * One transactional, value-only prediction copy.  The records are the
 * canonical snapshot/player ABIs themselves rather than a parallel state
 * schema; the wrapper only binds them to one generation-checked timeline ref.
 */
struct cg_canonical_prediction_snapshot_v2 {
    std::uint32_t struct_size;
    std::uint32_t schema_version;
    std::uint32_t active_epoch;
    std::uint32_t reserved0;
    worr_snapshot_timeline_ref_v1 ref;
    cg_canonical_prediction_receipt_v1 receipt;
    worr_snapshot_v2 snapshot;
    worr_snapshot_player_v2 player;
};

struct cg_canonical_snapshot_timeline_diagnostics_v1 {
    std::uint32_t struct_size;
    std::uint32_t initialized;
    std::uint32_t active;
    std::uint32_t pending_clock_reset;
    std::uint32_t active_epoch;
    std::uint32_t last_reset_reason;
    std::uint32_t last_clock_result;
    std::uint32_t last_query_result;
    std::uint64_t clock_failures;
    std::uint64_t query_failures;
    std::uint64_t capacity_rejections;
    worr_snapshot_timeline_ref_v1 latest_ref;
    worr_snapshot_timeline_clock_state_v1 clock;
};

const worr_cgame_snapshot_timeline_export_v2 *
CG_GetCanonicalSnapshotTimelineAPI();

/* Safe to call before the engine discovers the extension. */
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineInitialize();

/* Explicit deterministic render-clock control for later migration stages. */
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineClockApply(
    const worr_snapshot_timeline_clock_request_v1 *request,
    worr_snapshot_timeline_clock_state_v1 *state_out);
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineAdvanceClock(
    std::uint64_t host_time_us,
    worr_snapshot_timeline_clock_state_v1 *state_out);

/* Pair/entity/event helpers only return value records or generation-checked refs. */
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineSelectPair(
    const worr_snapshot_timeline_policy_v1 *policy,
    worr_snapshot_timeline_pair_v1 *pair_out);
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineSampleEntity(
    const worr_snapshot_timeline_policy_v1 *policy,
    const worr_snapshot_timeline_pair_v1 *pair,
    std::uint32_t entity_index,
    worr_snapshot_timeline_entity_sample_v1 *sample_out);
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopySnapshot(
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_v2 *snapshot_out);
/* Resolves one retained snapshot identity to a generation-checked reference.
 * This is used by event presentation to bind entity lineage to the exact
 * snapshot fence rather than mutable latest-frame state. */
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineFindSnapshot(
    worr_snapshot_id_v2 snapshot_id,
    worr_snapshot_timeline_ref_v1 *ref_out);
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopyPlayer(
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_player_v2 *player_out);
/* Copies one immutable entity range into caller-owned storage.  Failures are
 * transactional: neither the output array nor count is modified. */
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopyEntities(
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_entity_v2 *entities_out,
    std::uint32_t capacity,
    std::uint32_t *count_out);
worr_snapshot_timeline_result_v1
CG_CanonicalSnapshotTimelineCopyPredictionSnapshot(
    std::uint32_t snapshot_sequence,
    cg_canonical_prediction_snapshot_v2 *snapshot_out);
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineEventCursorBegin(
    worr_snapshot_timeline_event_cursor_v1 *cursor_out);
worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineEventNext(
    const worr_snapshot_timeline_event_cursor_v1 *cursor,
    worr_snapshot_timeline_event_cursor_v1 *next_cursor_out,
    worr_snapshot_timeline_event_observation_v1 *observation_out);

bool CG_CanonicalSnapshotTimelineGetDiagnostics(
    cg_canonical_snapshot_timeline_diagnostics_v1 *diagnostics_out);
