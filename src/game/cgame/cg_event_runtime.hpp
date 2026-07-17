/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "cg_event_shadow.hpp"
#include "common/net/event_journal.h"
#include "shared/cgame_event_runtime.h"
#include "shared/command_abi.h"
#include "shared/snapshot_abi.h"

#include <cstdint>

constexpr std::uint32_t CG_EVENT_RUNTIME_VERSION = 1u;
constexpr std::uint32_t CG_EVENT_RUNTIME_JOURNAL_CAPACITY = 512u;
constexpr std::uint32_t CG_EVENT_RUNTIME_AUTHORITY_CAPACITY = 1024u;
constexpr std::uint32_t CG_EVENT_RUNTIME_PREDICTION_TOMBSTONE_CAPACITY =
    1024u;
constexpr std::uint32_t CG_EVENT_RUNTIME_REFERENCE_CAPACITY = 2048u;
constexpr std::uint32_t CG_EVENT_RUNTIME_LEGACY_BODY_CAPACITY = 2048u;

enum cg_event_runtime_result_v1 : std::uint32_t {
    CG_EVENT_RUNTIME_OK = WORR_CGAME_EVENT_RUNTIME_OK,
    CG_EVENT_RUNTIME_DUPLICATE = WORR_CGAME_EVENT_RUNTIME_DUPLICATE,
    CG_EVENT_RUNTIME_MATCHED = WORR_CGAME_EVENT_RUNTIME_MATCHED,
    CG_EVENT_RUNTIME_CORRECTED = WORR_CGAME_EVENT_RUNTIME_CORRECTED,
    CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION =
        WORR_CGAME_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION,
    CG_EVENT_RUNTIME_EMPTY = WORR_CGAME_EVENT_RUNTIME_EMPTY,
    CG_EVENT_RUNTIME_NOT_READY = WORR_CGAME_EVENT_RUNTIME_NOT_READY,
    CG_EVENT_RUNTIME_INVALID_ARGUMENT =
        WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT,
    CG_EVENT_RUNTIME_UNINITIALIZED =
        WORR_CGAME_EVENT_RUNTIME_UNINITIALIZED,
    CG_EVENT_RUNTIME_WRONG_EPOCH = WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH,
    CG_EVENT_RUNTIME_INVALID_RECORD =
        WORR_CGAME_EVENT_RUNTIME_INVALID_RECORD,
    CG_EVENT_RUNTIME_CONFLICT = WORR_CGAME_EVENT_RUNTIME_CONFLICT,
    CG_EVENT_RUNTIME_CAPACITY = WORR_CGAME_EVENT_RUNTIME_CAPACITY,
    CG_EVENT_RUNTIME_DEGRADED = WORR_CGAME_EVENT_RUNTIME_DEGRADED,
    CG_EVENT_RUNTIME_NOT_FOUND = WORR_CGAME_EVENT_RUNTIME_NOT_FOUND,
    CG_EVENT_RUNTIME_TERMINAL = WORR_CGAME_EVENT_RUNTIME_TERMINAL,
    CG_EVENT_RUNTIME_REENTRANT = WORR_CGAME_EVENT_RUNTIME_REENTRANT,
};

struct cg_event_runtime_status_v1 {
    std::uint32_t struct_size;
    std::uint32_t schema_version;
    std::uint32_t legacy_epoch;
    std::uint32_t authority_epoch;
    std::uint32_t snapshot_epoch;
    std::uint32_t next_authority_sequence;
    std::uint32_t authority_count;
    std::uint32_t prediction_tombstone_count;
    std::uint32_t reference_count;
    std::uint32_t legacy_body_count;
    std::uint32_t authority_high_water;
    std::uint32_t prediction_tombstone_high_water;
    std::uint32_t reference_high_water;
    std::uint32_t legacy_body_high_water;
    /* `degraded` is process-lifetime audit history. Authority health and
     * resync state are scoped to the current authority epoch. */
    std::uint32_t authority_degraded;
    std::uint32_t authority_requires_resync;
    std::uint32_t degraded;
    std::uint32_t audit_enabled;

    std::uint64_t legacy_resets;
    std::uint64_t authority_resets;
    std::uint64_t snapshot_resets;
    std::uint64_t authoritative_batches;
    std::uint64_t authoritative_records;
    std::uint64_t authoritative_duplicates;
    std::uint64_t authoritative_conflicts;
    std::uint64_t authoritative_capacity_failures;
    std::uint64_t authoritative_stale_or_coalesced;
    std::uint64_t predicted_batches;
    std::uint64_t predicted_records;
    std::uint64_t predicted_duplicates;
    std::uint64_t predicted_capacity_failures;
    std::uint64_t prediction_matches;
    std::uint64_t prediction_corrections;
    std::uint64_t prediction_late_corrections;
    std::uint64_t prediction_cancellations;
    std::uint64_t prediction_expirations;
    std::uint64_t authoritative_expirations;
    std::uint64_t prediction_tombstone_evictions;
    std::uint64_t prediction_tombstone_capacity_failures;
    std::uint64_t prediction_retire_calls;
    std::uint64_t prediction_tombstones_retired;
    std::uint64_t stale_prediction_rejections;
    std::uint64_t prediction_retire_regressions;

    std::uint64_t snapshots_observed;
    std::uint64_t snapshot_duplicates;
    std::uint64_t snapshot_rejections;
    std::uint64_t references_observed;
    std::uint64_t reference_duplicates;
    std::uint64_t reference_conflicts;
    std::uint64_t reference_capacity_failures;
    std::uint64_t authority_ref_body_joins;
    std::uint64_t legacy_ref_before_body_joins;
    std::uint64_t legacy_body_before_ref_joins;
    std::uint64_t legacy_ref_body_mismatches;

    std::uint64_t legacy_bodies_observed;
    std::uint64_t legacy_body_overruns;
    std::uint64_t legacy_body_capacity_failures;
    std::uint64_t legacy_snapshot_reset_discards;
    std::uint64_t predicted_presentations;
    std::uint64_t authoritative_presentations;
    std::uint64_t authoritative_prediction_suppressions;
    std::uint64_t authoritative_terminal_skips;
    std::uint64_t legacy_entity_presentations;
    std::uint64_t legacy_action_presentations;
    std::uint64_t future_time_stalls;
    std::uint64_t authority_sequence_stalls;
    std::uint64_t authority_reference_stalls;
    std::uint64_t legacy_reference_stalls;
    std::uint64_t tombstone_evictions;
    std::uint64_t advance_calls;
    std::uint64_t resident_order_exhaustions;
    std::uint64_t presentation_chain_hash;
    std::uint64_t last_render_time_us;
    std::uint32_t last_now_tick;
    std::uint32_t reserved0;
    worr_command_cursor_v1 prediction_retired_through;
    worr_event_receipt_ack_v1 receipt;
};

/*
 * The three lifetimes are deliberately independent. Legacy decode resets do
 * not invent an authority epoch, and a snapshot seek does not erase predicted
 * command events. A snapshot reset only invalidates snapshot-derived fences.
 */
cg_event_runtime_result_v1
CG_EventRuntimeResetLegacy(std::uint32_t stream_epoch);
/* {0, 0} deactivates and scrubs the authority/prediction domain. */
cg_event_runtime_result_v1
CG_EventRuntimeResetAuthority(std::uint32_t stream_epoch,
                              std::uint32_t first_sequence);
cg_event_runtime_result_v1
CG_EventRuntimeResetSnapshot(std::uint32_t snapshot_epoch);
void CG_EventRuntimeSetAuditEnabled(bool enabled);
bool CG_EventRuntimeAuditEnabled();

/* Latch an independent local-interaction reconciliation failure into the
 * private authority health domain before any further event admission or
 * presentation. The engine owner observes the normal resync status bit. */
void CG_EventRuntimeSynchronizeLocalInteractionHealth();

/* All batch calls are allocation-free and transactional. */
cg_event_runtime_result_v1 CG_EventRuntimeSubmitAuthoritativeBatch(
    const worr_event_record_v1 *records, std::uint32_t count);
cg_event_runtime_result_v1 CG_EventRuntimeSubmitPredictedBatch(
    const worr_event_record_v1 *records, std::uint32_t count);
cg_event_runtime_result_v1 CG_EventRuntimeCancelPrediction(
    const worr_event_prediction_key_v1 *key);
/* Retire terminal/reconciled prediction history only after the authoritative
 * consumed-command watermark makes key reuse stale. The monotonic cursor also
 * prevents a reclaimed cancellation from being submitted and presented again. */
cg_event_runtime_result_v1 CG_EventRuntimeRetirePredictionsThrough(
    worr_command_cursor_v1 consumed_cursor);

/*
 * Called only after canonical snapshot publication has succeeded. Both
 * authoritative and legacy-inferred references are correctness-validated
 * regardless of cg_event_runtime_audit; that cvar gates only legacy body
 * comparison/presentation diagnostics. Failure never rolls back the copied
 * timeline snapshot, but withholds the native event-fence receipt and ACK.
 */
cg_event_runtime_result_v1 CG_EventRuntimeObserveSnapshot(
    const worr_snapshot_v2 *snapshot,
    const worr_snapshot_event_ref_v2 *event_refs,
    std::uint32_t event_ref_count);

/* The entry remains owned by the existing presentation history. The runtime
 * copies only join/fence metadata, never a second legacy event body. */
cg_event_runtime_result_v1 CG_EventRuntimeObserveLegacyEntry(
    const cg_canonical_event_presentation_entry_v1 *entry);

/* The production sink is intentionally no-effects. It records deterministic
 * present-once evidence while legacy parsing remains presentation authority. */
cg_event_runtime_result_v1 CG_EventRuntimeAdvanceAudit(
    std::uint64_t render_time_us, std::uint32_t now_tick,
    std::uint32_t max_presentations, std::uint32_t *advanced_out);

bool CG_EventRuntimeGetStatus(cg_event_runtime_status_v1 *status_out);
bool CG_EventRuntimeSnapshotFenceHealthy(std::uint32_t snapshot_epoch);
const worr_cgame_event_runtime_export_v1 *CG_GetEventRuntimeAPI();
