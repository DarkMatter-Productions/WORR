/*
 * Cgame-side shadow test: canonical records may create only a local request
 * transaction. No authority, collision, damage, attachment, or presentation
 * interface exists in this test seam.
 */
#include "cg_local_interaction.hpp"
#include "common/net/native_event_sender.h"
#include "shared/local_interaction_abi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            std::fprintf(stderr, "%s:%d: %s\\n", __FILE__, __LINE__,      \
                         #expression);                                      \
            return EXIT_FAILURE;                                             \
        }                                                                   \
    } while (0)

namespace {

constexpr std::size_t kActionShadowCapacity =
    CG_LOCAL_ACTION_SHADOW_EVIDENCE_CAPACITY;
constexpr std::size_t kRequiredEvidenceCount =
    (WORR_CGAME_PREDICTION_INPUT_CAPACITY - 1u) +
    WORR_NATIVE_EVENT_SENDER_TX_CAPACITY +
    WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY +
    WORR_LOCAL_ACTION_SHADOW_AUTHORITY_MAILBOX_CAPACITY +
    (WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY - 1u);
static_assert(kRequiredEvidenceCount == 798u);
static_assert(kActionShadowCapacity >= kRequiredEvidenceCount);
constexpr std::size_t kBeyondCommandRingCount =
    WORR_CGAME_PREDICTION_INPUT_CAPACITY + 1u;
constexpr std::size_t kNoReceiptStressCommandCount =
    kActionShadowCapacity * 2u + 1u;
constexpr std::size_t kReceiptStreamStressCommandCount =
    kActionShadowCapacity * 2u;
worr_cgame_command_record_entry_v1
    source_entries[kNoReceiptStressCommandCount];
bool corrupt_range;
bool exact_history_missing;
bool exact_input_mismatch;

worr_command_record_v1 make_record(std::uint32_t sequence, bool hook_held)
{
    worr_command_record_v1 record{};
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id = {41, sequence};
    record.sample_time_us = static_cast<std::uint64_t>(sequence) * 16000u;
    record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    record.command.struct_size = sizeof(record.command);
    record.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    record.command.duration_ms = 16;
    if (hook_held)
        record.command.buttons =
            WORR_LOCAL_INTERACTION_HOOK_BUTTON | (1u << 0);
    record.render_watermark.struct_size = sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    return record;
}

std::uint32_t resolve_by_id(
    worr_command_id_v1 command_id,
    worr_cgame_command_record_entry_v1 *entry_out)
{
    if (!entry_out || !Worr_CommandIdValidV1(command_id, false))
        return WORR_CGAME_COMMAND_RECORD_INVALID_ARGUMENT;
    if (!exact_history_missing) {
        for (const auto &entry : source_entries) {
            if (entry.command.command_id.epoch == command_id.epoch &&
                entry.command.command_id.sequence == command_id.sequence) {
                *entry_out = entry;
                if (exact_input_mismatch)
                    entry_out->command.command.buttons ^= 1u << 1;
                return WORR_CGAME_COMMAND_RECORD_OK;
            }
        }
    }
    return WORR_CGAME_COMMAND_RECORD_HISTORY_MISSING;
}

std::uint32_t resolve(std::uint32_t first_legacy_sequence,
                      std::uint32_t command_count,
                      worr_cgame_command_record_range_v1 *range_out)
{
    worr_cgame_command_record_range_v1 output{};
    if (!range_out)
        return WORR_CGAME_COMMAND_RECORD_INVALID_ARGUMENT;
    output.struct_size = sizeof(output);
    output.api_version = WORR_CGAME_COMMAND_RECORD_API_VERSION;
    output.flags = WORR_CGAME_COMMAND_RECORD_CANONICAL;
    output.first_legacy_sequence = first_legacy_sequence;
    for (std::uint32_t index = 0; index < command_count; ++index) {
        bool found = false;
        for (const auto &entry : source_entries) {
            if (entry.legacy_sequence == first_legacy_sequence + index) {
                output.commands[index] = entry;
                found = true;
                break;
            }
        }
        if (!found) {
            output.result = WORR_CGAME_COMMAND_RECORD_HISTORY_MISSING;
            *range_out = output;
            return output.result;
        }
    }
    output.command_count = command_count;
    if (corrupt_range && command_count > 1)
        output.commands[1].command.command_id.sequence = 9;
    output.result = WORR_CGAME_COMMAND_RECORD_OK;
    *range_out = output;
    return output.result;
}

worr_cgame_prediction_input_range_v1 prediction_range()
{
    worr_cgame_prediction_input_range_v1 range{};
    range.struct_size = sizeof(range);
    range.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    range.result = WORR_CGAME_PREDICTION_INPUT_OK;
    range.source = WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    range.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    range.authoritative_legacy_sequence = 100;
    range.current_legacy_sequence = 102;
    range.command_count = 2;
    for (std::uint32_t index = 0; index < range.command_count; ++index) {
        range.commands[index].legacy_sequence = 101 + index;
        range.commands[index].command_id = source_entries[index].command.command_id;
        range.commands[index].command = source_entries[index].command.command;
    }
    return range;
}

worr_cgame_prediction_input_range_v1 expired_prediction_range()
{
    worr_cgame_prediction_input_range_v1 range{};
    range.struct_size = sizeof(range);
    range.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    range.result = WORR_CGAME_PREDICTION_INPUT_OK;
    range.source = WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    range.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    range.authoritative_legacy_sequence = 102;
    range.current_legacy_sequence = 103;
    range.command_count = 1;
    range.commands[0].legacy_sequence = source_entries[2].legacy_sequence;
    range.commands[0].command_id = source_entries[2].command.command_id;
    range.commands[0].command = source_entries[2].command.command;
    return range;
}

worr_cgame_prediction_input_range_v1 single_prediction_range(
    std::size_t command_index)
{
    worr_cgame_prediction_input_range_v1 range{};
    const auto &entry = source_entries[command_index];
    range.struct_size = sizeof(range);
    range.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    range.result = WORR_CGAME_PREDICTION_INPUT_OK;
    range.source = WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    range.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    range.authoritative_legacy_sequence = entry.legacy_sequence - 1u;
    range.current_legacy_sequence = entry.legacy_sequence;
    range.command_count = 1;
    range.commands[0].legacy_sequence = entry.legacy_sequence;
    range.commands[0].command_id = entry.command.command_id;
    range.commands[0].command = entry.command.command;
    return range;
}

worr_cgame_prediction_input_range_v1 prefix_prediction_range(
    std::size_t command_count)
{
    if (command_count == 0 ||
        command_count > WORR_CGAME_PREDICTION_INPUT_CAPACITY) {
        std::abort();
    }

    worr_cgame_prediction_input_range_v1 range{};
    range.struct_size = sizeof(range);
    range.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    range.result = WORR_CGAME_PREDICTION_INPUT_OK;
    range.source = WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    range.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    range.authoritative_legacy_sequence = 100;
    range.current_legacy_sequence =
        100u + static_cast<std::uint32_t>(command_count);
    range.command_count = static_cast<std::uint32_t>(command_count);
    for (std::size_t index = 0; index < command_count; ++index) {
        range.commands[index].legacy_sequence =
            source_entries[index].legacy_sequence;
        range.commands[index].command_id =
            source_entries[index].command.command_id;
        range.commands[index].command = source_entries[index].command.command;
    }
    return range;
}

worr_local_interaction_authority_receipt_v1 authority_receipt(bool active)
{
    worr_local_interaction_state_v1 initial{};
    worr_local_interaction_intent_v1 intent{};
    worr_local_interaction_transaction_v1 transaction{};
    worr_local_interaction_authority_receipt_v1 receipt{};
    if (!Worr_LocalInteractionStateInitV1(&initial, 41))
        std::abort();
    intent.struct_size = sizeof(intent);
    intent.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
    intent.flags = WORR_LOCAL_INTERACTION_INTENT_HOOK_HELD;
    if (!Worr_LocalInteractionBuildAuthoritativeHookV1(
            &initial, &source_entries[0].command, &intent, active,
            &transaction) ||
        !Worr_LocalInteractionAuthorityReceiptBuildV1(&transaction,
                                                       &receipt)) {
        std::abort();
    }
    return receipt;
}

worr_local_action_shadow_authority_receipt_v1 action_shadow_receipt(
    std::size_t command_index, bool server_packet_watermark = false)
{
    worr_local_action_observation_state_v1 before{};
    before.struct_size = sizeof(before);
    before.schema_version = WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION;
    before.flags = WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ALIVE |
                   WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ELIGIBLE;
    before.phase = WORR_LOCAL_ACTION_OBSERVATION_READY;
    before.active_weapon_id = 9;
    before.presentation_frame = 7;
    before.presentation_rate = 10;
    auto after = before;
    after.presentation_frame = 8;
    worr_local_action_observation_record_v1 observation{};
    worr_local_action_shadow_v1 shadow{};
    worr_local_action_shadow_authority_receipt_v1 receipt{};
    auto command = source_entries[command_index].command;
    if (server_packet_watermark) {
        command.render_watermark.provenance =
            WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED;
        command.render_watermark.source_server_tick = 100;
        command.render_watermark.tick_interval_us = 16000;
        command.render_watermark.source_server_time_us = 1600000;
        command.render_watermark.rendered_server_time_us = 1600000;
    }
    if (!Worr_LocalActionObservationBuildV1(
            0,
            &command, &before, &after,
            &observation) ||
        !Worr_LocalActionShadowBuildV1(WORR_LOCAL_ACTION_CATALOG_BLASTER,
                                        &observation, &shadow) ||
        !Worr_LocalActionShadowAuthorityReceiptBuildV1(&shadow, &receipt)) {
        std::abort();
    }
    return receipt;
}

} // namespace

int main()
{
    source_entries[0].legacy_sequence = 101;
    source_entries[0].command = make_record(1, true);
    source_entries[1].legacy_sequence = 102;
    source_entries[1].command = make_record(2, true);
    source_entries[2].legacy_sequence = 103;
    source_entries[2].command = make_record(3, true);
    for (std::size_t index = 3;
         index < kNoReceiptStressCommandCount; ++index) {
        source_entries[index].legacy_sequence =
            101u + static_cast<std::uint32_t>(index);
        source_entries[index].command = make_record(
            1u + static_cast<std::uint32_t>(index), true);
    }

    const worr_cgame_command_record_import_v1 import = {
        sizeof(import), WORR_CGAME_COMMAND_RECORD_API_VERSION, resolve};
    const worr_cgame_command_record_import_v2 import_v2 = {
        sizeof(import_v2), WORR_CGAME_COMMAND_RECORD_API_VERSION_V2,
        resolve_by_id};
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionPredict(prediction_range());

    cg_local_interaction_shadow_status_v1 status{};
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.prediction_passes == 1);
    CHECK(status.transactions == 2);
    CHECK(status.pending_requests == 1);
    CHECK(status.unavailable_ranges == 0);
    CHECK(status.invalid_ranges == 0);

    const auto confirmed = authority_receipt(true);
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&confirmed) ==
          cg_local_interaction_receipt_result_v1::hook_confirmed);
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&confirmed) ==
          cg_local_interaction_receipt_result_v1::duplicate);
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.authority_receipts == 1);
    CHECK(status.authority_duplicates == 1);
    CHECK(status.corrections_confirmed == 1);
    CHECK(status.requires_resync == 0);

    corrupt_range = true;
    CG_LocalInteractionPredict(prediction_range());
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.prediction_passes == 1);
    CHECK(status.transactions == 2);
    CHECK(status.invalid_ranges == 1);

    corrupt_range = false;
    CG_LocalInteractionSetImport(&import);
    const auto rejected = authority_receipt(false);
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&rejected) ==
          cg_local_interaction_receipt_result_v1::accepted_unmatched);
    CG_LocalInteractionPredict(prediction_range());
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.authority_unmatched == 1);
    CHECK(status.corrections_rejected == 1);
    CHECK(status.requires_resync == 0);

    /* A receipt-first pair may wait for its prediction, but not silently
     * outlive the retained canonical history that could prove that pair. */
    CG_LocalInteractionSetImport(&import);
    const auto orphaned = authority_receipt(false);
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&orphaned) ==
          cg_local_interaction_receipt_result_v1::accepted_unmatched);
    CG_LocalInteractionPredict(expired_prediction_range());
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.authority_unmatched == 1);
    CHECK(status.authority_expirations == 1);
    CHECK(status.requires_resync == 1);
    CHECK(CG_LocalInteractionRequiresResync());

    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionPredict(prediction_range());
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&confirmed) ==
          cg_local_interaction_receipt_result_v1::hook_confirmed);
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&rejected) ==
          cg_local_interaction_receipt_result_v1::conflict);
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.authority_conflicts == 1);
    CHECK(status.requires_resync == 1);

    CG_LocalInteractionSetImport(nullptr);
    CG_LocalInteractionPredict(prediction_range());
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.prediction_passes == 0);
    CHECK(status.transactions == 0);

    CG_LocalInteractionSetImport(&import);
    CG_LocalActionShadowObserveCommands(prediction_range());
    cg_local_action_shadow_status_v1 action_status{};
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.observation_passes == 1);
    CHECK(action_status.canonical_commands == 2);
    const auto action_receipt = action_shadow_receipt(0);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&action_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&action_receipt) ==
          cg_local_action_shadow_receipt_result_v1::duplicate);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.authority_receipts == 1);
    CHECK(action_status.authority_duplicates == 1);
    CHECK(action_status.command_matches == 1);
    CHECK(action_status.requires_resync == 0);

    CG_LocalInteractionSetImport(&import);
    auto early_action_receipt = action_shadow_receipt(0);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&early_action_receipt) ==
          cg_local_action_shadow_receipt_result_v1::accepted_unmatched);
    CG_LocalActionShadowObserveCommands(prediction_range());
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.authority_unmatched == 1);
    CHECK(action_status.command_matches == 1);
    CHECK(action_status.requires_resync == 0);

    CG_LocalInteractionSetImport(&import);
    CG_LocalActionShadowObserveCommands(prediction_range());
    auto mismatched_action_receipt = action_shadow_receipt(1);
    mismatched_action_receipt.command_hash ^= UINT64_C(1);
    CHECK(Worr_LocalActionShadowAuthorityReceiptValidateV1(
        &mismatched_action_receipt));
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(
              &mismatched_action_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_mismatch);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.command_mismatches == 1);
    CHECK(action_status.requires_resync == 1);
    CHECK(CG_LocalActionShadowRequiresResync());

    /* Receipt-time lookup survives skipped render/prediction cadence.  The
     * server's later packet-shared watermark is intentionally different from
     * the client's pre-transport NONE watermark; input identity still pairs
     * exactly because render provenance is not command input. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = false;
    const auto exact_receipt = action_shadow_receipt(0, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&exact_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.exact_lookup_attempts == 1);
    CHECK(action_status.exact_lookup_hits == 1);
    CHECK(action_status.exact_lookup_misses == 0);
    CHECK(action_status.canonical_commands == 1);
    CHECK(action_status.command_matches == 1);
    CHECK(action_status.authority_outstanding == 0);
    CHECK(action_status.requires_resync == 0);

    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = true;
    const auto missing_receipt = action_shadow_receipt(1, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&missing_receipt) ==
          cg_local_action_shadow_receipt_result_v1::accepted_unmatched);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.exact_lookup_attempts == 1);
    CHECK(action_status.exact_lookup_hits == 0);
    CHECK(action_status.exact_lookup_misses == 1);
    CHECK(action_status.authority_outstanding == 1);
    CHECK(action_status.requires_resync == 0);

    /* Native receipt flight is independently larger than the 128-command V2
     * resolver. Retained cgame evidence must therefore join a delayed command
     * even when the engine explicitly reports that its own history wrapped. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = true;
    for (std::size_t index = 0; index < kBeyondCommandRingCount; ++index)
        CG_LocalActionShadowObserveCommands(single_prediction_range(index));
    const auto beyond_ring_receipt = action_shadow_receipt(0, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&beyond_ring_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.canonical_commands == kBeyondCommandRingCount);
    CHECK(action_status.command_matches == 1);
    CHECK(action_status.exact_lookup_attempts == 0);
    CHECK(action_status.command_cache_evictions == 0);
    CHECK(action_status.requires_resync == 0);

    /* The complete shared 798-record compositional bound likewise retains its
     * oldest command without borrowing recovery from the 128-record engine
     * history. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = true;
    for (std::size_t index = 0; index < kRequiredEvidenceCount; ++index)
        CG_LocalActionShadowObserveCommands(single_prediction_range(index));
    const auto required_bound_receipt = action_shadow_receipt(0, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(
              &required_bound_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.canonical_commands == kRequiredEvidenceCount);
    CHECK(action_status.command_matches == 1);
    CHECK(action_status.exact_lookup_attempts == 0);
    CHECK(action_status.command_cache_evictions == 0);
    CHECK(action_status.requires_resync == 0);

    /* Receipt-first ordering owns the same full bound. Preserve every validated
     * receipt-only row while V2 history is unavailable, then join each exact
     * command without capacity recovery or authority loss. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = true;
    for (std::size_t index = 0; index < kRequiredEvidenceCount; ++index) {
        const auto receipt = action_shadow_receipt(index, true);
        CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&receipt) ==
              cg_local_action_shadow_receipt_result_v1::accepted_unmatched);
    }
    for (std::size_t index = 0; index < kRequiredEvidenceCount; ++index)
        CG_LocalActionShadowObserveCommands(single_prediction_range(index));
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.canonical_commands == kRequiredEvidenceCount);
    CHECK(action_status.authority_receipts == kRequiredEvidenceCount);
    CHECK(action_status.authority_unmatched == kRequiredEvidenceCount);
    CHECK(action_status.command_matches == kRequiredEvidenceCount);
    CHECK(action_status.authority_outstanding == 0);
    CHECK(action_status.exact_lookup_attempts == kRequiredEvidenceCount);
    CHECK(action_status.exact_lookup_misses == kRequiredEvidenceCount);
    CHECK(action_status.command_cache_evictions == 0);
    CHECK(action_status.capacity_failures == 0);
    CHECK(action_status.requires_resync == 0);

    /* Receipt production can be disabled or absent for arbitrarily long
     * command runs. Roll the command-only cache without treating that absence
     * as a protocol failure, while retaining an explicit coverage boundary. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = false;
    exact_input_mismatch = false;
    for (std::size_t index = 0;
         index < kNoReceiptStressCommandCount; ++index)
        CG_LocalActionShadowObserveCommands(single_prediction_range(index));
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.canonical_commands == kNoReceiptStressCommandCount);
    CHECK(action_status.command_frontier_prunes == 0);
    CHECK(action_status.receipt_frontier_advances == 0);
    CHECK(action_status.receipt_frontier.epoch == 0);
    CHECK(action_status.receipt_frontier.sequence == 0);
    CHECK(action_status.command_cache_evictions ==
          kNoReceiptStressCommandCount - kActionShadowCapacity);
    CHECK(action_status.command_coverage_lost_through.epoch == 41);
    CHECK(action_status.command_coverage_lost_through.sequence ==
          kNoReceiptStressCommandCount - kActionShadowCapacity);
    CHECK(action_status.coverage_loss_failures == 0);
    CHECK(action_status.exact_lookup_attempts == 0);
    CHECK(action_status.exact_lookup_hits == 0);
    CHECK(action_status.exact_lookup_misses == 0);
    CHECK(action_status.capacity_failures == 0);
    CHECK(action_status.command_matches == 0);
    CHECK(action_status.requires_resync == 0);

    /* A delayed receipt behind the coverage boundary is still recoverable by
     * exact V2 command ID. A current resident receipt then advances the ordered
     * authority frontier without any false resync. */
    const auto evicted_delayed_receipt = action_shadow_receipt(0, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(
              &evicted_delayed_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    const auto current_receipt =
        action_shadow_receipt(kNoReceiptStressCommandCount - 1u, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&current_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.exact_lookup_attempts == 1);
    CHECK(action_status.exact_lookup_hits == 1);
    CHECK(action_status.exact_lookup_misses == 0);
    CHECK(action_status.command_cache_evictions ==
          kNoReceiptStressCommandCount - kActionShadowCapacity + 1u);
    CHECK(action_status.command_coverage_lost_through.sequence ==
          kNoReceiptStressCommandCount - kActionShadowCapacity + 1u);
    CHECK(action_status.authority_receipts == 2);
    CHECK(action_status.command_matches == 2);
    CHECK(action_status.receipt_frontier.sequence ==
          kNoReceiptStressCommandCount);
    CHECK(action_status.capacity_failures == 0);
    CHECK(action_status.coverage_loss_failures == 0);
    CHECK(action_status.requires_resync == 0);

    /* Once both the local cache and exact engine history have lost a command,
     * an actual receipt for it fails closed. The loss itself remains healthy. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = true;
    for (std::size_t index = 0;
         index < kNoReceiptStressCommandCount; ++index)
        CG_LocalActionShadowObserveCommands(single_prediction_range(index));
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(
              &evicted_delayed_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_mismatch);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.command_cache_evictions ==
          kNoReceiptStressCommandCount - kActionShadowCapacity);
    CHECK(action_status.exact_lookup_attempts == 1);
    CHECK(action_status.exact_lookup_hits == 0);
    CHECK(action_status.exact_lookup_misses == 1);
    CHECK(action_status.coverage_loss_failures == 1);
    CHECK(action_status.authority_expirations == 1);
    CHECK(action_status.authority_receipts == 0);
    CHECK(action_status.requires_resync == 1);

    /* Ordered receipts are command-production frontiers. Each one retires only
     * lower command-only cache rows, allowing a sustained stream to remain
     * bounded without ever discarding receipt or terminal evidence. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = false;
    for (std::size_t index = 0;
         index < kReceiptStreamStressCommandCount; ++index) {
        CG_LocalActionShadowObserveCommands(single_prediction_range(index));
        if ((index + 1u) % 32u == 0) {
            const auto receipt = action_shadow_receipt(index, true);
            CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&receipt) ==
                  cg_local_action_shadow_receipt_result_v1::command_matched);
        }
    }
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.canonical_commands ==
          kReceiptStreamStressCommandCount);
    CHECK(action_status.authority_receipts ==
          kReceiptStreamStressCommandCount / 32u);
    CHECK(action_status.command_matches ==
          kReceiptStreamStressCommandCount / 32u);
    CHECK(action_status.command_frontier_prunes ==
          (kReceiptStreamStressCommandCount / 32u) * 31u);
    CHECK(action_status.receipt_frontier_advances ==
          kReceiptStreamStressCommandCount / 32u);
    CHECK(action_status.receipt_frontier.epoch == 41);
    CHECK(action_status.receipt_frontier.sequence ==
          kReceiptStreamStressCommandCount);
    CHECK(action_status.capacity_failures == 0);
    CHECK(action_status.authority_outstanding == 0);
    CHECK(action_status.command_cache_evictions == 0);
    CHECK(action_status.requires_resync == 0);

    /* A receipt can be delayed while its command remains in the bounded cache,
     * and exact duplicates do not advance the ordered command frontier. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = false;
    exact_input_mismatch = false;
    for (std::size_t index = 0; index < 96; ++index)
        CG_LocalActionShadowObserveCommands(single_prediction_range(index));
    const auto delayed_receipt = action_shadow_receipt(31, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&delayed_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&delayed_receipt) ==
          cg_local_action_shadow_receipt_result_v1::duplicate);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.command_frontier_prunes == 31);
    CHECK(action_status.receipt_frontier_advances == 1);
    CHECK(action_status.receipt_frontier.sequence == 32);
    CHECK(action_status.authority_duplicates == 1);
    CHECK(action_status.capacity_failures == 0);
    CHECK(action_status.requires_resync == 0);

    /* Authoritative legacy progress can retire a terminal pair. The full
     * latest receipt frontier still makes an exact equal-ID replay idempotent,
     * while conflicting bytes at that same ID fail closed. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = false;
    CG_LocalActionShadowObserveCommands(single_prediction_range(0));
    const auto retired_frontier_receipt = action_shadow_receipt(0, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(
              &retired_frontier_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    CG_LocalActionShadowObserveCommands(single_prediction_range(1));
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(
              &retired_frontier_receipt) ==
          cg_local_action_shadow_receipt_result_v1::duplicate);
    auto conflicting_frontier_receipt = retired_frontier_receipt;
    conflicting_frontier_receipt.record_hash ^= UINT64_C(1);
    CHECK(Worr_LocalActionShadowAuthorityReceiptValidateV1(
        &conflicting_frontier_receipt));
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(
              &conflicting_frontier_receipt) ==
          cg_local_action_shadow_receipt_result_v1::conflict);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.authority_receipts == 1);
    CHECK(action_status.authority_duplicates == 1);
    CHECK(action_status.authority_conflicts == 1);
    CHECK(action_status.command_matches == 1);
    CHECK(action_status.receipt_frontier_advances == 1);
    CHECK(action_status.receipt_frontier.sequence == 1);
    CHECK(action_status.requires_resync == 1);

    /* The ordered caller contract makes a lower new receipt a regression.
     * The older command cache row was safely retired by sequence 3, so it may
     * not be reconstructed as if it were a newly ordered authority record. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    for (std::size_t index = 0; index < 4; ++index)
        CG_LocalActionShadowObserveCommands(single_prediction_range(index));
    const auto later_receipt = action_shadow_receipt(2);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&later_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    const auto regressing_receipt = action_shadow_receipt(1);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&regressing_receipt) ==
          cg_local_action_shadow_receipt_result_v1::conflict);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.receipt_frontier_advances == 1);
    CHECK(action_status.receipt_frontier.sequence == 3);
    CHECK(action_status.authority_conflicts == 1);
    CHECK(action_status.requires_resync == 1);

    /* Receipt-time V2 reconstruction still fails closed if the retained
     * canonical input no longer hashes to the authority receipt. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_input_mismatch = true;
    const auto inconsistent_receipt = action_shadow_receipt(4, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&inconsistent_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_mismatch);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.exact_lookup_attempts == 1);
    CHECK(action_status.exact_lookup_hits == 1);
    CHECK(action_status.command_mismatches == 1);
    CHECK(action_status.receipt_frontier_advances == 1);
    CHECK(action_status.requires_resync == 1);

    /* A sustained ordered receipt stream must not retain every already-matched
     * terminal pair. Each newer command frontier safely retires the preceding
     * match while receipt-only/unmatched evidence remains protected. Resolve
     * every command by exact V2 ID so no authoritative legacy watermark can
     * accidentally make this test pass by pruning the pairs first. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_input_mismatch = false;
    for (std::size_t index = 0;
         index < kReceiptStreamStressCommandCount; ++index) {
        const auto receipt = action_shadow_receipt(index);
        CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&receipt) ==
              cg_local_action_shadow_receipt_result_v1::command_matched);
    }
    const auto latest_receipt =
        action_shadow_receipt(kReceiptStreamStressCommandCount - 1u);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&latest_receipt) ==
          cg_local_action_shadow_receipt_result_v1::duplicate);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.canonical_commands ==
          kReceiptStreamStressCommandCount);
    CHECK(action_status.authority_receipts ==
          kReceiptStreamStressCommandCount);
    CHECK(action_status.authority_duplicates == 1);
    CHECK(action_status.command_matches ==
          kReceiptStreamStressCommandCount);
    CHECK(action_status.command_frontier_prunes == 0);
    CHECK(action_status.terminal_frontier_prunes ==
          kReceiptStreamStressCommandCount - 1u);
    CHECK(action_status.receipt_frontier_advances ==
          kReceiptStreamStressCommandCount);
    CHECK(action_status.receipt_frontier.sequence ==
          kReceiptStreamStressCommandCount);
    CHECK(action_status.exact_lookup_attempts ==
          kReceiptStreamStressCommandCount);
    CHECK(action_status.exact_lookup_hits ==
          kReceiptStreamStressCommandCount);
    CHECK(action_status.exact_lookup_misses == 0);
    CHECK(action_status.capacity_failures == 0);
    CHECK(action_status.authority_outstanding == 0);
    CHECK(action_status.requires_resync == 0);

    CG_LocalInteractionSetImportV2(nullptr);
    std::puts("cgame_local_interaction_test: ok");
    return EXIT_SUCCESS;
}
