// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#include <cstddef>
#include <cstdint>

// This module is deliberately fed value-only records instead of reading client
// globals.  A future cgame ABI can therefore populate the same records through
// GetSnapshot/GetInput/GetEventRange without changing timeline consumers.

constexpr std::size_t CG_SNAPSHOT_TIMELINE_CAPACITY = 128;
constexpr std::size_t CG_EVENT_JOURNAL_CAPACITY = 512;

enum class cg_snapshot_continuity_t : std::uint8_t {
    initial,        // first accepted snapshot in a reset timeline
    contiguous,     // next snapshot, delta-compressed from its predecessor
    sequence_gap,   // one or more snapshot identifiers were not accepted
    baseline_jump,  // contiguous id using an older accepted delta baseline
    full_snapshot,  // contiguous id with no delta baseline
    duplicate,      // same accepted id; preserve epoch for event suppression
    rewind          // older id; starts a collision-free event epoch
};

struct cg_accepted_snapshot_t {
    std::int32_t snapshot_id;
    std::int32_t delta_baseline_id;
    std::uint32_t server_time_ms;
    std::uint32_t receive_time_ms;
    std::uint32_t frame_flags;
    std::uint32_t entity_count;
    // Inferred from the acknowledged transport packet.  This is telemetry,
    // not an authoritative command-consumption watermark.
    std::uint32_t inferred_acked_input_cmd;
};

// Entries are immutable for their logical lifetime.  The fixed ring only
// overwrites an entry when that entry ages out of the bounded history.
struct cg_snapshot_metadata_t {
    std::uint32_t epoch;
    std::int32_t snapshot_id;
    std::int32_t delta_baseline_id;
    std::uint32_t server_time_ms;
    std::uint32_t receive_time_ms;
    std::uint32_t frame_flags;
    std::uint32_t entity_count;
    std::uint32_t inferred_acked_input_cmd;
    cg_snapshot_continuity_t continuity;
};

struct cg_event_identity_t {
    std::uint32_t epoch;
    std::int32_t snapshot_id;
    std::uint32_t entity_id;
    std::uint32_t raw_event;
};

struct cg_event_journal_entry_t {
    cg_event_identity_t identity;
    std::uint32_t receive_time_ms;
};

enum class cg_prediction_correction_reason_t : std::uint8_t {
    none,
    input_range_invalid,
    retained_state_missing,
    config_discontinuity,
    replay_rejected,
    state_divergence,
    correction_threshold_exceeded,
    canonical_authority_unavailable,
    canonical_authority_mismatch,
    snapshot_discontinuity,
};

struct cg_prediction_reconciliation_t {
    std::uint32_t inferred_acked_input_cmd;
    std::uint32_t current_input_cmd;
    std::uint32_t replay_count;
    float correction_magnitude;
    std::uint32_t correction_count;
    std::uint32_t hard_reset_count;
    cg_prediction_correction_reason_t last_correction_reason;
    bool last_correction_was_hard_reset;
};

void CG_SnapshotTimeline_InitCvars();
void CG_SnapshotTimeline_Reset(std::uint32_t receive_time_ms);
void CG_SnapshotTimeline_RecordAccepted(const cg_accepted_snapshot_t &snapshot);

// Call only after the existing protocol/FPS event eligibility checks.  A true
// result reserves the identity in the journal; a false result is a duplicate.
bool CG_SnapshotTimeline_ShouldDispatchEvent(std::uint32_t entity_id,
                                             std::uint32_t raw_event);

// Value-only prediction notes form the future GetInput boundary.  They are
// telemetry only and never participate in movement simulation decisions.
void CG_SnapshotTimeline_NotePredictionReplay(
    std::uint32_t inferred_acked_input_cmd,
    std::uint32_t current_input_cmd,
    std::uint32_t replay_count);
void CG_SnapshotTimeline_NotePredictionCorrection(
    std::uint32_t inferred_acked_input_cmd,
    std::uint32_t current_input_cmd,
    float correction_magnitude,
    bool hard_reset,
    cg_prediction_correction_reason_t reason);

// Debug output is aggregated and rate limited; callers may tick this from both
// snapshot and prediction paths without producing per-frame console spam.
void CG_SnapshotTimeline_DebugTick(std::uint32_t realtime_ms);

std::size_t CG_SnapshotTimeline_SnapshotCount();
const cg_snapshot_metadata_t *CG_SnapshotTimeline_SnapshotFromNewest(std::size_t age);
std::size_t CG_SnapshotTimeline_EventCount();
const cg_event_journal_entry_t *CG_SnapshotTimeline_EventFromNewest(std::size_t age);
const cg_prediction_reconciliation_t &CG_SnapshotTimeline_PredictionTelemetry();
