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

constexpr std::uint32_t CG_EVENT_RUNTIME_VERSION = 2u;
constexpr std::uint32_t CG_EVENT_RUNTIME_PRESENTER_VERSION = 1u;
constexpr std::uint32_t CG_EVENT_RUNTIME_JOURNAL_CAPACITY = 512u;
constexpr std::uint32_t CG_EVENT_RUNTIME_AUTHORITY_CAPACITY = 1024u;
constexpr std::uint32_t CG_EVENT_RUNTIME_PREDICTION_TOMBSTONE_CAPACITY =
    1024u;
constexpr std::uint32_t CG_EVENT_RUNTIME_REFERENCE_CAPACITY = 2048u;
constexpr std::uint32_t CG_EVENT_RUNTIME_LEGACY_BODY_CAPACITY = 2048u;
/* Mirrors the immutable cgame timeline's 64-slot bound.  Its 63 retained
 * 16 ms intervals cover the explicit one-second interpolation window at the
 * maximum effective ~62 Hz server cadence, so delayed direct-native bodies
 * can still join exact final-emission source proof in either arrival order. */
constexpr std::uint32_t CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY = 64u;

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
    /* Ordered admission/application cursor for private reconciliation
     * receipts. Unlike next_authority_sequence, this cursor may cross an
     * admitted visual record before that record is snapshot-fenced or
     * presented. */
    std::uint32_t next_private_reconciliation_sequence;
    /* UINT32_MAX is a valid final event sequence. Once that record has been
     * consumed the corresponding cursor remains pinned at UINT32_MAX and its
     * exhausted bit distinguishes terminal completion from a pending final
     * record. A fresh authority epoch resets both bits. */
    std::uint32_t authority_sequence_exhausted;
    std::uint32_t private_reconciliation_sequence_exhausted;
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

/* Optional diagnostics observer. Event admission remains independent of the
 * prediction/UI translation unit so focused runtime consumers can link the
 * production authority path without a presentation logger. Status inspection
 * is allowed from the observer; mutating runtime calls report REENTRANT (or are
 * ignored for void setters) until ordered reconciliation returns. */
using cg_local_action_shadow_report_callback_v1 = void (*)();
void CG_EventRuntimeSetLocalActionShadowReportCallback(
    cg_local_action_shadow_report_callback_v1 callback);

enum cg_event_runtime_presentation_provenance_v1 : std::uint32_t {
    CG_EVENT_RUNTIME_PRESENTATION_PREDICTED = 1u,
    CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY = 2u,
};

/*
 * Production presentation is deliberately a two-phase, synchronous contract.
 * `CanPresent` must be side-effect free and validates every already-prepared
 * resource and generation needed by `Present`; registration/allocation belongs
 * to an earlier lifecycle phase. The runtime then marks the journal entry
 * presented before invoking the total/no-fail `Present` callback. This keeps a
 * rejected event fail-closed without making a committed effect retryable.
 * Neither callback may retain the borrowed record/context or re-enter a
 * mutating event-runtime operation. Such result-returning reentry reports
 * `CG_EVENT_RUNTIME_REENTRANT`; void configuration setters are ignored while a
 * callback is active. Status inspection remains available for diagnostics.
 */
struct cg_event_runtime_presentation_context_v1 {
    std::uint32_t struct_size;
    std::uint32_t schema_version;
    std::uint32_t provenance;
    std::uint32_t reserved0;
    worr_snapshot_id_v2 fence_snapshot_id;
    std::uint32_t fence_tick;
    std::uint32_t reserved1;
    std::uint64_t fence_time_us;
};

using cg_event_runtime_can_present_callback_v1 = bool (*)(
    const worr_event_record_v1 *record,
    const cg_event_runtime_presentation_context_v1 *context);
using cg_event_runtime_present_callback_v1 = void (*)(
    const worr_event_record_v1 *record,
    const cg_event_runtime_presentation_context_v1 *context);

void CG_EventRuntimeSetPresenter(
    cg_event_runtime_can_present_callback_v1 can_present,
    cg_event_runtime_present_callback_v1 present);

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

/* Advances the deterministic present-once sink.  With no presenter installed
 * it remains audit-only; production cgame installs the real value presenter. */
cg_event_runtime_result_v1 CG_EventRuntimeAdvanceAudit(
    std::uint64_t render_time_us, std::uint32_t now_tick,
    std::uint32_t max_presentations, std::uint32_t *advanced_out);

bool CG_EventRuntimeGetStatus(cg_event_runtime_status_v1 *status_out);

enum cg_event_runtime_checkpoint_block_reason_v1 : std::uint32_t {
    CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_NONE = 0u,
    CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_MUTATION = 1u,
    CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_UNINITIALIZED = 2u,
    CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_WRONG_EPOCH = 3u,
    CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_UNHEALTHY = 4u,
    CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_MISSING_HEAD = 5u,
    CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_PENDING_HEAD = 6u,
};

/* Cgame-private checkpoint diagnostics. This is deliberately outside the
 * engine/cgame export ABI: it describes the current ordered authority head for
 * developer telemetry without changing readiness or mutating runtime state. */
struct cg_event_runtime_checkpoint_block_v1 {
    std::uint32_t struct_size;
    std::uint32_t reason;
    std::uint32_t expected_authority_epoch;
    std::uint32_t authority_epoch;
    std::uint32_t next_authority_sequence;
    std::uint32_t pending_sequence;
    std::uint32_t authority_state;
    std::uint32_t slot_state;
    std::uint32_t record_flags;
    std::uint32_t payload_kind;
    std::uint32_t delivery_class;
    std::uint32_t source_tick;
    std::uint32_t expiry_tick;
    std::uint32_t fence_tick;
    worr_snapshot_id_v2 fence_snapshot_id;
    worr_snapshot_id_v2 last_snapshot_id;
    std::uint64_t fence_time_us;
    std::uint64_t last_snapshot_time_us;
    std::uint64_t last_render_time_us;
    std::uint32_t last_now_tick;
    std::uint32_t reserved0;
};

bool CG_EventRuntimeGetCheckpointBlock(
    std::uint32_t expected_authority_epoch,
    cg_event_runtime_checkpoint_block_v1 *block_out);
/* Capture one checkpoint baseline only while the requested live authority
 * epoch is healthy and has no authoritative record still awaiting terminal
 * presentation.  The readiness decision and status copy are one runtime
 * operation; status_out is left untouched when the function returns false. */
bool CG_EventRuntimeCheckpointReady(
    std::uint32_t expected_authority_epoch,
    cg_event_runtime_status_v1 *status_out);
bool CG_EventRuntimeSnapshotFenceHealthy(std::uint32_t snapshot_epoch);
const worr_cgame_event_runtime_export_v1 *CG_GetEventRuntimeAPI();
