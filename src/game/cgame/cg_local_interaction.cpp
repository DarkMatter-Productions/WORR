/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_local_interaction.hpp"

#include "shared/local_interaction_abi.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

const worr_cgame_command_record_import_v1 *command_record_import;

struct local_interaction_shadow_telemetry_t {
    std::uint64_t prediction_passes{};
    std::uint64_t transactions{};
    std::uint64_t pending_requests{};
    std::uint64_t unavailable_ranges{};
    std::uint64_t invalid_ranges{};
    std::uint64_t authority_receipts{};
    std::uint64_t authority_duplicates{};
    std::uint64_t authority_unmatched{};
    std::uint64_t authority_expirations{};
    std::uint64_t corrections_confirmed{};
    std::uint64_t corrections_rejected{};
    std::uint64_t corrections_diverged{};
    std::uint64_t corrections_invalid{};
    std::uint64_t authority_conflicts{};
    std::uint64_t capacity_failures{};
    std::uint32_t requires_resync{};
};

local_interaction_shadow_telemetry_t telemetry;

constexpr std::uint32_t kAuthorityPairCapacity = 128;
enum : std::uint32_t {
    AUTHORITY_PAIR_OCCUPIED = 1u << 0,
    AUTHORITY_PAIR_PREDICTED = 1u << 1,
    AUTHORITY_PAIR_RECEIPT = 1u << 2,
    AUTHORITY_PAIR_TERMINAL = 1u << 3,
};

struct authority_pair_entry_t {
    std::uint32_t flags{};
    std::uint32_t legacy_sequence{};
    worr_command_id_v1 command_id{};
    worr_local_interaction_transaction_v1 predicted{};
    worr_local_interaction_authority_receipt_v1 receipt{};
};

std::array<authority_pair_entry_t, kAuthorityPairCapacity> authority_pairs;

void increment_saturated(std::uint64_t &value)
{
    if (value != UINT64_MAX)
        ++value;
}

bool command_id_equal(worr_command_id_v1 left, worr_command_id_v1 right)
{
    return left.epoch == right.epoch && left.sequence == right.sequence;
}

bool receipt_equal(
    const worr_local_interaction_authority_receipt_v1 &left,
    const worr_local_interaction_authority_receipt_v1 &right)
{
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

void require_resync()
{
    telemetry.requires_resync = 1;
}

int find_authority_pair(worr_command_id_v1 command_id)
{
    for (std::uint32_t index = 0; index < authority_pairs.size(); ++index) {
        const auto &entry = authority_pairs[index];
        if ((entry.flags & AUTHORITY_PAIR_OCCUPIED) != 0 &&
            command_id_equal(entry.command_id, command_id)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int find_free_authority_pair()
{
    for (std::uint32_t index = 0; index < authority_pairs.size(); ++index) {
        if (authority_pairs[index].flags == 0)
            return static_cast<int>(index);
    }
    return -1;
}

cg_local_interaction_receipt_result_v1 reconcile_authority_pair(
    authority_pair_entry_t &entry)
{
    const bool has_prediction =
        (entry.flags & AUTHORITY_PAIR_PREDICTED) != 0;
    const bool has_receipt = (entry.flags & AUTHORITY_PAIR_RECEIPT) != 0;
    if (!has_prediction || !has_receipt)
        return cg_local_interaction_receipt_result_v1::accepted_unmatched;
    if ((entry.flags & AUTHORITY_PAIR_TERMINAL) != 0)
        return cg_local_interaction_receipt_result_v1::duplicate;

    entry.flags |= AUTHORITY_PAIR_TERMINAL;
    switch (Worr_LocalInteractionClassifyReceiptV1(&entry.predicted,
                                                   &entry.receipt)) {
    case WORR_LOCAL_INTERACTION_CORRECTION_HOOK_CONFIRMED:
        increment_saturated(telemetry.corrections_confirmed);
        return cg_local_interaction_receipt_result_v1::hook_confirmed;
    case WORR_LOCAL_INTERACTION_CORRECTION_HOOK_REJECTED:
        increment_saturated(telemetry.corrections_rejected);
        return cg_local_interaction_receipt_result_v1::hook_rejected;
    case WORR_LOCAL_INTERACTION_CORRECTION_DIVERGED:
        increment_saturated(telemetry.corrections_diverged);
        require_resync();
        return cg_local_interaction_receipt_result_v1::diverged;
    case WORR_LOCAL_INTERACTION_CORRECTION_INVALID:
    default:
        increment_saturated(telemetry.corrections_invalid);
        require_resync();
        return cg_local_interaction_receipt_result_v1::invalid;
    }
}

cg_local_interaction_receipt_result_v1 store_predicted_request(
    const worr_local_interaction_transaction_v1 &transaction,
    std::uint32_t legacy_sequence)
{
    const worr_command_id_v1 command_id = transaction.command.command_id;
    int index = find_authority_pair(command_id);
    if (index < 0) {
        index = find_free_authority_pair();
        if (index < 0) {
            increment_saturated(telemetry.capacity_failures);
            require_resync();
            return cg_local_interaction_receipt_result_v1::capacity;
        }
        authority_pairs[index].flags = AUTHORITY_PAIR_OCCUPIED;
        authority_pairs[index].command_id = command_id;
    }

    auto &entry = authority_pairs[index];
    if ((entry.flags & AUTHORITY_PAIR_PREDICTED) != 0) {
        if (std::memcmp(&entry.predicted, &transaction,
                        sizeof(transaction)) != 0) {
            increment_saturated(telemetry.authority_conflicts);
            require_resync();
            return cg_local_interaction_receipt_result_v1::conflict;
        }
        return (entry.flags & AUTHORITY_PAIR_RECEIPT) != 0
                   ? reconcile_authority_pair(entry)
                   : cg_local_interaction_receipt_result_v1::duplicate;
    }

    entry.predicted = transaction;
    entry.legacy_sequence = legacy_sequence;
    entry.flags |= AUTHORITY_PAIR_PREDICTED;
    return (entry.flags & AUTHORITY_PAIR_RECEIPT) != 0
               ? reconcile_authority_pair(entry)
               : cg_local_interaction_receipt_result_v1::accepted_unmatched;
}

bool prune_authority_pairs(
    const worr_cgame_prediction_input_range_v1 &prediction_range,
    const worr_cgame_command_record_range_v1 &records)
{
    const worr_command_id_v1 first_pending =
        records.commands[0].command.command_id;
    for (auto &entry : authority_pairs) {
        if ((entry.flags & AUTHORITY_PAIR_OCCUPIED) == 0)
            continue;
        if ((entry.flags & AUTHORITY_PAIR_TERMINAL) != 0 &&
            entry.legacy_sequence != 0 &&
            entry.legacy_sequence <=
                prediction_range.authoritative_legacy_sequence) {
            entry = {};
            continue;
        }
        /* A receipt that precedes local prediction becomes inert once the
         * next retained canonical record is already newer in the same epoch.
         * No outcome is manufactured; this merely bounds evidence cgame can
         * no longer match after its consumed-command frontier advanced. */
        if ((entry.flags & AUTHORITY_PAIR_PREDICTED) == 0 &&
            entry.command_id.epoch == first_pending.epoch &&
            entry.command_id.sequence < first_pending.sequence) {
            /* A receipt is authoritative evidence, not an optional cache
             * entry. Once the canonical prediction frontier has moved beyond
             * it, cgame can no longer prove the pair. Preserve the fail-closed
             * barrier instead of silently dropping that evidence. */
            increment_saturated(telemetry.authority_expirations);
            require_resync();
            return false;
        }
    }
    return true;
}

bool valid_import(const worr_cgame_command_record_import_v1 *import)
{
    return import &&
           import->struct_size == sizeof(*import) &&
           import->api_version == WORR_CGAME_COMMAND_RECORD_API_VERSION &&
           import->ResolveCanonicalCommandRange;
}

bool valid_range(const worr_cgame_prediction_input_range_v1 &prediction,
                 const worr_cgame_command_record_range_v1 &records)
{
    if (records.struct_size != sizeof(records) ||
        records.api_version != WORR_CGAME_COMMAND_RECORD_API_VERSION ||
        records.result != WORR_CGAME_COMMAND_RECORD_OK ||
        records.flags != WORR_CGAME_COMMAND_RECORD_CANONICAL ||
        records.first_legacy_sequence !=
            prediction.authoritative_legacy_sequence + 1u ||
        records.command_count != prediction.command_count) {
        return false;
    }

    for (std::uint32_t index = 0; index < records.command_count; ++index) {
        const auto &record = records.commands[index];
        const auto &movement = prediction.commands[index];
        if (record.legacy_sequence != records.first_legacy_sequence + index ||
            record.reserved0 != 0 ||
            !Worr_CommandRecordValidateV1(
                &record.command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ||
            record.command.command_id.epoch != movement.command_id.epoch ||
            record.command.command_id.sequence != movement.command_id.sequence ||
            std::memcmp(&record.command.command, &movement.command,
                        sizeof(record.command.command)) != 0) {
            return false;
        }
    }
    return true;
}

bool prior_hook_held(std::uint32_t first_legacy_sequence,
                     const worr_cgame_command_record_entry_v1 &first,
                     bool *known_out, bool *held_out)
{
    worr_cgame_command_record_range_v1 predecessor{};
    const std::uint32_t predecessor_sequence = first_legacy_sequence - 1u;
    const std::uint32_t result =
        command_record_import->ResolveCanonicalCommandRange(
            predecessor_sequence, 1, &predecessor);

    *known_out = false;
    *held_out = false;
    if (result != WORR_CGAME_COMMAND_RECORD_OK ||
        predecessor.struct_size != sizeof(predecessor) ||
        predecessor.api_version != WORR_CGAME_COMMAND_RECORD_API_VERSION ||
        predecessor.result != result ||
        predecessor.flags != WORR_CGAME_COMMAND_RECORD_CANONICAL ||
        predecessor.first_legacy_sequence != predecessor_sequence ||
        predecessor.command_count != 1 ||
        predecessor.commands[0].legacy_sequence != predecessor_sequence ||
        predecessor.commands[0].reserved0 != 0 ||
        !Worr_CommandRecordValidateV1(
            &predecessor.commands[0].command,
            WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ||
        predecessor.commands[0].command.command_id.epoch !=
            first.command.command_id.epoch ||
        predecessor.commands[0].command.command_id.sequence + 1u !=
            first.command.command_id.sequence) {
        return true;
    }

    *known_out = true;
    *held_out =
        (predecessor.commands[0].command.command.buttons &
         WORR_LOCAL_INTERACTION_HOOK_BUTTON) != 0;
    return true;
}

bool initialize_before_first(
    const worr_cgame_command_record_entry_v1 &first, bool prior_known,
    bool prior_held, worr_local_interaction_state_v1 *state_out)
{
    worr_local_interaction_state_v1 state{};
    const std::uint64_t duration_us =
        (std::uint64_t)first.command.command.duration_ms * UINT64_C(1000);
    const std::uint32_t sequence = first.command.command_id.sequence;

    if (!state_out ||
        !Worr_LocalInteractionStateInitV1(
            &state, first.command.command_id.epoch)) {
        return false;
    }
    if (sequence != 1) {
        if (first.command.sample_time_us < duration_us)
            return false;
        state.applied_cursor.contiguous_sequence = sequence - 1u;
        state.sample_time_us = first.command.sample_time_us - duration_us;
        /*
         * A prior record is the only safe way to carry held state across the
         * bounded range. Without it the first record is deliberately treated
         * as an already-held sample below, suppressing an unprovable edge.
         */
        if (prior_known && prior_held)
            state.flags |= WORR_LOCAL_INTERACTION_STATE_HOOK_HELD;
    }
    if (!Worr_LocalInteractionStateValidateV1(&state))
        return false;
    *state_out = state;
    return true;
}

} // namespace

void CG_LocalInteractionSetImport(
    const worr_cgame_command_record_import_v1 *import)
{
    command_record_import = valid_import(import) ? import : nullptr;
    CG_LocalInteractionReset();
}

void CG_LocalInteractionReset()
{
    telemetry = {};
    authority_pairs = {};
}

bool CG_LocalInteractionRequiresResync()
{
    return telemetry.requires_resync != 0;
}

void CG_LocalInteractionGetStatus(
    cg_local_interaction_shadow_status_v1 *status_out)
{
    if (!status_out)
        return;
    *status_out = {
        telemetry.prediction_passes,
        telemetry.transactions,
        telemetry.pending_requests,
        telemetry.unavailable_ranges,
        telemetry.invalid_ranges,
        telemetry.authority_receipts,
        telemetry.authority_duplicates,
        telemetry.authority_unmatched,
        telemetry.authority_expirations,
        telemetry.corrections_confirmed,
        telemetry.corrections_rejected,
        telemetry.corrections_diverged,
        telemetry.corrections_invalid,
        telemetry.authority_conflicts,
        telemetry.capacity_failures,
        telemetry.requires_resync,
    };
}

cg_local_interaction_receipt_result_v1
CG_LocalInteractionSubmitAuthorityReceipt(
    const worr_local_interaction_authority_receipt_v1 *receipt)
{
    int index;

    if (telemetry.requires_resync != 0)
        return cg_local_interaction_receipt_result_v1::requires_resync;
    if (!Worr_LocalInteractionAuthorityReceiptValidateV1(receipt)) {
        increment_saturated(telemetry.corrections_invalid);
        require_resync();
        return cg_local_interaction_receipt_result_v1::invalid;
    }

    index = find_authority_pair(receipt->command_id);
    if (index < 0) {
        index = find_free_authority_pair();
        if (index < 0) {
            increment_saturated(telemetry.capacity_failures);
            require_resync();
            return cg_local_interaction_receipt_result_v1::capacity;
        }
        authority_pairs[index].flags = AUTHORITY_PAIR_OCCUPIED;
        authority_pairs[index].command_id = receipt->command_id;
    }

    auto &entry = authority_pairs[index];
    if ((entry.flags & AUTHORITY_PAIR_RECEIPT) != 0) {
        if (receipt_equal(entry.receipt, *receipt)) {
            increment_saturated(telemetry.authority_duplicates);
            return cg_local_interaction_receipt_result_v1::duplicate;
        }
        increment_saturated(telemetry.authority_conflicts);
        require_resync();
        return cg_local_interaction_receipt_result_v1::conflict;
    }

    entry.receipt = *receipt;
    entry.flags |= AUTHORITY_PAIR_RECEIPT;
    increment_saturated(telemetry.authority_receipts);
    if ((entry.flags & AUTHORITY_PAIR_PREDICTED) == 0) {
        increment_saturated(telemetry.authority_unmatched);
        return cg_local_interaction_receipt_result_v1::accepted_unmatched;
    }
    return reconcile_authority_pair(entry);
}

void CG_LocalInteractionPredict(
    const worr_cgame_prediction_input_range_v1 &prediction_range)
{
    worr_cgame_command_record_range_v1 records{};
    worr_local_interaction_state_v1 state{};
    bool prior_known = false;
    bool prior_held = false;

    if ((prediction_range.flags & WORR_CGAME_PREDICTION_INPUT_CANONICAL) == 0 ||
        prediction_range.command_count == 0 || !valid_import(command_record_import) ||
        telemetry.requires_resync != 0) {
        return;
    }

    const std::uint32_t result =
        command_record_import->ResolveCanonicalCommandRange(
            prediction_range.authoritative_legacy_sequence + 1u,
            prediction_range.command_count, &records);
    if (result != WORR_CGAME_COMMAND_RECORD_OK) {
        increment_saturated(telemetry.unavailable_ranges);
        return;
    }
    if (!valid_range(prediction_range, records) ||
        !prior_hook_held(records.first_legacy_sequence, records.commands[0],
                         &prior_known, &prior_held) ||
        !initialize_before_first(records.commands[0], prior_known, prior_held,
                                 &state)) {
        increment_saturated(telemetry.invalid_ranges);
        return;
    }

    increment_saturated(telemetry.prediction_passes);
    if (!prune_authority_pairs(prediction_range, records))
        return;
    for (std::uint32_t index = 0; index < records.command_count; ++index) {
        const auto &entry = records.commands[index];
        worr_local_interaction_intent_v1 intent{};
        worr_local_interaction_transaction_v1 transaction{};
        const bool held =
            (entry.command.command.buttons &
             WORR_LOCAL_INTERACTION_HOOK_BUTTON) != 0;

        /*
         * The bounded record ring cannot prove the first edge when its
         * predecessor has aged out. Treat a held first sample as pre-held:
         * this is fail-closed (no predicted request) and the next exact edge
         * can still be shadowed normally.
         */
        if (index == 0 && !prior_known &&
            entry.command.command_id.sequence != 1 && held)
            state.flags |= WORR_LOCAL_INTERACTION_STATE_HOOK_HELD;

        intent.struct_size = sizeof(intent);
        intent.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
        if (held)
            intent.flags = WORR_LOCAL_INTERACTION_INTENT_HOOK_HELD;
        if (!Worr_LocalInteractionBuildPredictedHookV1(
                &state, &entry.command, &intent, &transaction)) {
            increment_saturated(telemetry.invalid_ranges);
            return;
        }

        state = transaction.state_after;
        increment_saturated(telemetry.transactions);
        if (transaction.outcome_flags &
            WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED) {
            increment_saturated(telemetry.pending_requests);
            const auto result = store_predicted_request(
                transaction, entry.legacy_sequence);
            if (result == cg_local_interaction_receipt_result_v1::capacity ||
                result == cg_local_interaction_receipt_result_v1::conflict ||
                result == cg_local_interaction_receipt_result_v1::diverged ||
                result == cg_local_interaction_receipt_result_v1::invalid ||
                result ==
                    cg_local_interaction_receipt_result_v1::requires_resync) {
                increment_saturated(telemetry.invalid_ranges);
                return;
            }
        }
    }
}
