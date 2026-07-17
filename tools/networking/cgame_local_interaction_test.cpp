/*
 * Cgame-side shadow test: canonical records may create only a local request
 * transaction. No authority, collision, damage, attachment, or presentation
 * interface exists in this test seam.
 */
#include "cg_local_interaction.hpp"
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

worr_cgame_command_record_entry_v1 source_entries[3];
bool corrupt_range;

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
        record.command.buttons = WORR_LOCAL_INTERACTION_HOOK_BUTTON;
    record.render_watermark.struct_size = sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    return record;
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

} // namespace

int main()
{
    source_entries[0].legacy_sequence = 101;
    source_entries[0].command = make_record(1, true);
    source_entries[1].legacy_sequence = 102;
    source_entries[1].command = make_record(2, true);
    source_entries[2].legacy_sequence = 103;
    source_entries[2].command = make_record(3, true);

    const worr_cgame_command_record_import_v1 import = {
        sizeof(import), WORR_CGAME_COMMAND_RECORD_API_VERSION, resolve};
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
    return EXIT_SUCCESS;
}
