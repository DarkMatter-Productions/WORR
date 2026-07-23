/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/cgame_prediction.h"
#include "shared/local_interaction_abi.h"
#include "shared/local_action_shadow.h"

#include <cstdint>

/*
 * The interaction shadow is intentionally separate from movement replay and
 * selected-weapon prediction. It creates no collision, damage, attachment,
 * audio, or visual result. The authority/event bridge carries only immutable
 * receipts; the interaction shadow remains unable to create gameplay or
 * presentation side effects.
 */
void CG_LocalInteractionSetImport(
    const worr_cgame_command_record_import_v1 *import);
void CG_LocalInteractionSetImportV2(
    const worr_cgame_command_record_import_v2 *import);
void CG_LocalInteractionReset();
void CG_LocalInteractionPredict(
    const worr_cgame_prediction_input_range_v1 &prediction_range);
bool CG_LocalInteractionRequiresResync();

enum class cg_local_interaction_receipt_result_v1 : std::uint32_t {
    accepted_unmatched = 0,
    duplicate = 1,
    hook_confirmed = 2,
    hook_rejected = 3,
    diverged = 4,
    invalid = 5,
    conflict = 6,
    capacity = 7,
    requires_resync = 8,
};

/*
 * Receipts may arrive before or after the prediction pass that retained their
 * command. This does not create gameplay, collision, attachment, audio, or
 * visuals; it only pairs immutable evidence and records a correction class.
 */
cg_local_interaction_receipt_result_v1
CG_LocalInteractionSubmitAuthorityReceipt(
    const worr_local_interaction_authority_receipt_v1 *receipt);

struct cg_local_interaction_shadow_status_v1 {
    std::uint64_t prediction_passes;
    std::uint64_t transactions;
    std::uint64_t pending_requests;
    std::uint64_t unavailable_ranges;
    std::uint64_t invalid_ranges;
    std::uint64_t authority_receipts;
    std::uint64_t authority_duplicates;
    std::uint64_t authority_unmatched;
    std::uint64_t authority_expirations;
    std::uint64_t corrections_confirmed;
    std::uint64_t corrections_rejected;
    std::uint64_t corrections_diverged;
    std::uint64_t corrections_invalid;
    std::uint64_t authority_conflicts;
    std::uint64_t capacity_failures;
    std::uint32_t requires_resync;
};

void CG_LocalInteractionGetStatus(
    cg_local_interaction_shadow_status_v1 *status_out);

/*
 * Descriptor-complete weapon receipts currently reconcile only against the
 * exact canonical command cgame retained. They do not construct a local V2
 * weapon transaction or present an event while all 22 blocker masks are set.
 */
enum class cg_local_action_shadow_receipt_result_v1 : std::uint32_t {
    accepted_unmatched = 0,
    duplicate = 1,
    command_matched = 2,
    command_mismatch = 3,
    invalid = 4,
    conflict = 5,
    capacity = 6,
    requires_resync = 7,
};

/* One local client owns this process-local evidence table. Its capacity covers
 * the complete compositional command/receipt flight bound; cg_local_interaction
 * keeps the derivation tied to the contributing shared transport constants. */
constexpr std::uint32_t CG_LOCAL_ACTION_SHADOW_EVIDENCE_CAPACITY = 1024u;

void CG_LocalActionShadowObserveCommands(
    const worr_cgame_prediction_input_range_v1 &prediction_range);
cg_local_action_shadow_receipt_result_v1
CG_LocalActionShadowSubmitAuthorityReceipt(
    const worr_local_action_shadow_authority_receipt_v1 *receipt);
bool CG_LocalActionShadowRequiresResync();

struct cg_local_action_shadow_status_v1 {
    std::uint64_t observation_passes;
    std::uint64_t canonical_commands;
    std::uint64_t authority_receipts;
    std::uint64_t authority_duplicates;
    std::uint64_t authority_unmatched;
    std::uint64_t command_matches;
    std::uint64_t command_mismatches;
    std::uint64_t authority_conflicts;
    std::uint64_t authority_expirations;
    std::uint64_t capacity_failures;
    std::uint64_t authority_outstanding;
    std::uint64_t exact_lookup_attempts;
    std::uint64_t exact_lookup_hits;
    std::uint64_t exact_lookup_misses;
    std::uint64_t command_frontier_prunes;
    std::uint64_t terminal_frontier_prunes;
    std::uint64_t receipt_frontier_advances;
    std::uint64_t command_cache_evictions;
    std::uint64_t coverage_loss_failures;
    worr_command_id_v1 command_coverage_lost_through;
    worr_command_id_v1 receipt_frontier;
    std::uint32_t requires_resync;
};

void CG_LocalActionShadowGetStatus(
    cg_local_action_shadow_status_v1 *status_out);
void CG_LocalActionShadowReportParity();
