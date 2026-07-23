/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_local_interaction.hpp"

#include "common/net/native_event_sender.h"
#include "shared/local_interaction_abi.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

const worr_cgame_command_record_import_v1 *command_record_import;
const worr_cgame_command_record_import_v2 *command_record_import_v2;

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

constexpr std::uint32_t kLocalInteractionAuthorityPairCapacity = 128;
static_assert(kLocalInteractionAuthorityPairCapacity ==
                  WORR_CGAME_PREDICTION_INPUT_CAPACITY,
              "local authority evidence must cover one canonical command ring");

/* A command may remain unreconciled across every independently bounded stage:
 * 127 canonical successors before prediction range exhaustion, the complete
 * native event TX/backlog, the published server mailbox, and 63 selectively
 * admitted successors behind one missing reliable event head. Keep this a
 * compositional bound rather than depending on current producer/drain cadence.
 */
constexpr std::uint32_t kLocalActionEvidenceRequired =
    (WORR_CGAME_PREDICTION_INPUT_CAPACITY - 1u) +
    WORR_NATIVE_EVENT_SENDER_TX_CAPACITY +
    WORR_NATIVE_EVENT_SENDER_BACKLOG_CAPACITY +
    WORR_LOCAL_ACTION_SHADOW_AUTHORITY_MAILBOX_CAPACITY +
    (WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY - 1u);
static_assert(kLocalActionEvidenceRequired == 798u,
              "local action receipt flight bound changed");
static_assert(CG_LOCAL_ACTION_SHADOW_EVIDENCE_CAPACITY >=
                  kLocalActionEvidenceRequired,
              "local action evidence does not cover bounded receipt flight");
constexpr std::uint32_t kAttackButton = 1u << 0;
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

std::array<authority_pair_entry_t,
           kLocalInteractionAuthorityPairCapacity> authority_pairs;

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

bool valid_import(const worr_cgame_command_record_import_v2 *import)
{
    return import && import->struct_size == sizeof(*import) &&
           import->api_version ==
               WORR_CGAME_COMMAND_RECORD_API_VERSION_V2 &&
           import->ResolveCanonicalCommandById;
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

struct local_action_shadow_telemetry_t {
    std::uint64_t observation_passes{};
    std::uint64_t canonical_commands{};
    std::uint64_t authority_receipts{};
    std::uint64_t authority_duplicates{};
    std::uint64_t authority_unmatched{};
    std::uint64_t command_matches{};
    std::uint64_t command_mismatches{};
    std::uint64_t authority_conflicts{};
    std::uint64_t authority_expirations{};
    std::uint64_t capacity_failures{};
    std::uint64_t exact_lookup_attempts{};
    std::uint64_t exact_lookup_hits{};
    std::uint64_t exact_lookup_misses{};
    std::uint64_t command_frontier_prunes{};
    std::uint64_t terminal_frontier_prunes{};
    std::uint64_t receipt_frontier_advances{};
    std::uint64_t command_cache_evictions{};
    std::uint64_t coverage_loss_failures{};
    std::uint32_t requires_resync{};
};

local_action_shadow_telemetry_t action_shadow_telemetry;

enum : std::uint32_t {
    ACTION_SHADOW_OCCUPIED = 1u << 0,
    ACTION_SHADOW_COMMAND = 1u << 1,
    ACTION_SHADOW_RECEIPT = 1u << 2,
    ACTION_SHADOW_TERMINAL = 1u << 3,
};

struct local_action_shadow_pair_t {
    std::uint32_t flags{};
    std::uint32_t legacy_sequence{};
    worr_command_id_v1 command_id{};
    std::uint64_t command_hash{};
    worr_local_action_shadow_authority_receipt_v1 receipt{};
};

std::array<local_action_shadow_pair_t,
           CG_LOCAL_ACTION_SHADOW_EVIDENCE_CAPACITY>
    action_shadow_pairs;
std::uint32_t action_shadow_observed_through;
bool action_shadow_observation_started;
worr_command_id_v1 action_shadow_command_coverage_lost_through;
bool action_shadow_command_coverage_lost;
worr_local_action_shadow_authority_receipt_v1
    action_shadow_receipt_frontier;
bool action_shadow_receipt_frontier_started;

void require_action_shadow_resync()
{
    action_shadow_telemetry.requires_resync = 1;
}

int find_action_shadow_pair(worr_command_id_v1 command_id)
{
    for (std::uint32_t index = 0; index < action_shadow_pairs.size(); ++index) {
        const auto &entry = action_shadow_pairs[index];
        if ((entry.flags & ACTION_SHADOW_OCCUPIED) != 0 &&
            command_id_equal(entry.command_id, command_id)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int find_free_action_shadow_pair()
{
    for (std::uint32_t index = 0; index < action_shadow_pairs.size(); ++index) {
        if (action_shadow_pairs[index].flags == 0)
            return static_cast<int>(index);
    }
    return -1;
}

int find_oldest_action_shadow_command_only_pair()
{
    int selected = -1;
    for (std::uint32_t index = 0; index < action_shadow_pairs.size(); ++index) {
        const auto &entry = action_shadow_pairs[index];
        if (entry.flags !=
            (ACTION_SHADOW_OCCUPIED | ACTION_SHADOW_COMMAND)) {
            continue;
        }
        if (entry.legacy_sequence == 0) {
            require_action_shadow_resync();
            return -2;
        }
        if (selected < 0 ||
            entry.legacy_sequence <
                action_shadow_pairs[static_cast<std::size_t>(selected)]
                    .legacy_sequence) {
            selected = static_cast<int>(index);
        }
    }
    return selected;
}

bool note_action_shadow_command_coverage_loss(
    worr_command_id_v1 command_id)
{
    if (action_shadow_command_coverage_lost &&
        (command_id.epoch !=
             action_shadow_command_coverage_lost_through.epoch ||
         command_id.sequence <=
             action_shadow_command_coverage_lost_through.sequence)) {
        increment_saturated(action_shadow_telemetry.authority_conflicts);
        require_action_shadow_resync();
        return false;
    }
    action_shadow_command_coverage_lost_through = command_id;
    action_shadow_command_coverage_lost = true;
    increment_saturated(action_shadow_telemetry.command_cache_evictions);
    return true;
}

int allocate_action_shadow_pair_under_command_pressure()
{
    const int free_index = find_free_action_shadow_pair();
    if (free_index >= 0)
        return free_index;

    const int eviction_index =
        find_oldest_action_shadow_command_only_pair();
    if (eviction_index < 0)
        return eviction_index;
    auto &entry =
        action_shadow_pairs[static_cast<std::size_t>(eviction_index)];
    if (!note_action_shadow_command_coverage_loss(entry.command_id))
        return -2;
    entry = {};
    return eviction_index;
}

bool action_shadow_command_coverage_was_lost(
    worr_command_id_v1 command_id)
{
    return action_shadow_command_coverage_lost &&
           command_id.epoch ==
               action_shadow_command_coverage_lost_through.epoch &&
           command_id.sequence <=
               action_shadow_command_coverage_lost_through.sequence;
}

enum class action_shadow_exact_result_t {
    found,
    history_missing,
    invalid,
};

action_shadow_exact_result_t resolve_action_shadow_command_exact(
    worr_command_id_v1 command_id,
    worr_cgame_command_record_entry_v1 &command_out)
{
    command_out = {};
    if (!valid_import(command_record_import_v2))
        return action_shadow_exact_result_t::history_missing;

    increment_saturated(action_shadow_telemetry.exact_lookup_attempts);
    const std::uint32_t result =
        command_record_import_v2->ResolveCanonicalCommandById(
            command_id, &command_out);
    if (result == WORR_CGAME_COMMAND_RECORD_HISTORY_MISSING) {
        increment_saturated(action_shadow_telemetry.exact_lookup_misses);
        return action_shadow_exact_result_t::history_missing;
    }
    if (result != WORR_CGAME_COMMAND_RECORD_OK ||
        command_out.legacy_sequence == 0 || command_out.reserved0 != 0 ||
        !command_id_equal(command_out.command.command_id, command_id) ||
        !Worr_CommandRecordValidateV1(
            &command_out.command,
            WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS)) {
        require_action_shadow_resync();
        return action_shadow_exact_result_t::invalid;
    }

    increment_saturated(action_shadow_telemetry.exact_lookup_hits);
    return action_shadow_exact_result_t::found;
}

bool action_shadow_receipt_frontier_accepts(worr_command_id_v1 command_id)
{
    return !action_shadow_receipt_frontier_started ||
           (command_id.epoch ==
                action_shadow_receipt_frontier.command_id.epoch &&
            command_id.sequence >
                action_shadow_receipt_frontier.command_id.sequence);
}

bool prune_action_shadow_cache_before(
    worr_command_id_v1 command_id)
{
    for (auto &entry : action_shadow_pairs) {
        if ((entry.flags & ACTION_SHADOW_OCCUPIED) == 0)
            continue;
        if (entry.command_id.epoch != command_id.epoch) {
            increment_saturated(action_shadow_telemetry.authority_conflicts);
            require_action_shadow_resync();
            return false;
        }
        if (entry.command_id.sequence >= command_id.sequence)
            continue;

        if (entry.flags ==
            (ACTION_SHADOW_OCCUPIED | ACTION_SHADOW_COMMAND)) {
            entry = {};
            increment_saturated(
                action_shadow_telemetry.command_frontier_prunes);
            continue;
        }

        if (entry.flags ==
            (ACTION_SHADOW_OCCUPIED | ACTION_SHADOW_COMMAND |
             ACTION_SHADOW_RECEIPT | ACTION_SHADOW_TERMINAL)) {
            /* Ordered receipt command IDs are a server-consumption frontier.
             * A later receipt proves an older matched pair is terminal; its
             * event-level duplicate remains owned by the reliable event
             * runtime, while the most recent receipt's full bytes remain in
             * action_shadow_receipt_frontier. Receipt-only rows stay resident
             * because their local command can still arrive for reconciliation.
             */
            entry = {};
            increment_saturated(
                action_shadow_telemetry.terminal_frontier_prunes);
        }
    }
    return true;
}

void advance_action_shadow_receipt_frontier(
    const worr_local_action_shadow_authority_receipt_v1 &receipt)
{
    action_shadow_receipt_frontier = receipt;
    action_shadow_receipt_frontier_started = true;
    increment_saturated(action_shadow_telemetry.receipt_frontier_advances);
}

cg_local_action_shadow_receipt_result_v1 reconcile_action_shadow_pair(
    local_action_shadow_pair_t &entry)
{
    if ((entry.flags & (ACTION_SHADOW_COMMAND | ACTION_SHADOW_RECEIPT)) !=
        (ACTION_SHADOW_COMMAND | ACTION_SHADOW_RECEIPT)) {
        return cg_local_action_shadow_receipt_result_v1::accepted_unmatched;
    }
    if ((entry.flags & ACTION_SHADOW_TERMINAL) != 0)
        return cg_local_action_shadow_receipt_result_v1::duplicate;

    entry.flags |= ACTION_SHADOW_TERMINAL;
    if (entry.command_hash == entry.receipt.command_hash) {
        increment_saturated(action_shadow_telemetry.command_matches);
        return cg_local_action_shadow_receipt_result_v1::command_matched;
    }
    increment_saturated(action_shadow_telemetry.command_mismatches);
    require_action_shadow_resync();
    return cg_local_action_shadow_receipt_result_v1::command_mismatch;
}

bool prune_action_shadow_pairs(
    const worr_cgame_prediction_input_range_v1 &prediction_range,
    const worr_cgame_command_record_range_v1 *records)
{
    for (auto &entry : action_shadow_pairs) {
        if ((entry.flags & ACTION_SHADOW_OCCUPIED) == 0)
            continue;
        if ((entry.flags & ACTION_SHADOW_TERMINAL) != 0 &&
            entry.legacy_sequence != 0 &&
            entry.legacy_sequence <=
                prediction_range.authoritative_legacy_sequence) {
            entry = {};
            continue;
        }
        if (records && records->command_count != 0 &&
            (entry.flags & ACTION_SHADOW_COMMAND) == 0 &&
            entry.command_id.epoch ==
                records->commands[0].command.command_id.epoch &&
            entry.command_id.sequence <
                records->commands[0].command.command_id.sequence) {
            increment_saturated(action_shadow_telemetry.authority_expirations);
            require_action_shadow_resync();
            return false;
        }
    }
    return true;
}

bool resolve_action_shadow_record(
    std::uint32_t legacy_sequence,
    worr_cgame_command_record_range_v1 &records)
{
    records = {};
    const std::uint32_t result =
        command_record_import->ResolveCanonicalCommandRange(
            legacy_sequence, 1, &records);
    if (result == WORR_CGAME_COMMAND_RECORD_HISTORY_MISSING)
        return false;
    if (result != WORR_CGAME_COMMAND_RECORD_OK ||
        records.struct_size != sizeof(records) ||
        records.api_version != WORR_CGAME_COMMAND_RECORD_API_VERSION ||
        records.result != WORR_CGAME_COMMAND_RECORD_OK ||
        records.flags != WORR_CGAME_COMMAND_RECORD_CANONICAL ||
        records.first_legacy_sequence != legacy_sequence ||
        records.command_count != 1 ||
        records.commands[0].legacy_sequence != legacy_sequence ||
        records.commands[0].reserved0 != 0 ||
        !Worr_CommandRecordValidateV1(
            &records.commands[0].command,
            WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS)) {
        require_action_shadow_resync();
        return false;
    }
    return true;
}

enum class action_shadow_command_result_t {
    accepted,
    capacity,
    invalid,
};

action_shadow_command_result_t observe_action_shadow_command(
    const worr_cgame_command_record_entry_v1 &command,
    bool count_canonical_command)
{
    // This first receipt slice is intentionally attack-bearing on both ends.
    // Retaining idle commands would consume reconciliation capacity for
    // records the server is specified not to publish.
    if ((command.command.command.buttons & kAttackButton) == 0)
        return action_shadow_command_result_t::accepted;

    std::uint64_t command_hash = 0;
    if (!Worr_CommandRecordInputHashV1(
            &command.command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            &command_hash)) {
        require_action_shadow_resync();
        return action_shadow_command_result_t::invalid;
    }

    int index = find_action_shadow_pair(command.command.command_id);
    if (index < 0) {
        if (action_shadow_receipt_frontier_started) {
            if (command.command.command_id.epoch !=
                action_shadow_receipt_frontier.command_id.epoch) {
                increment_saturated(
                    action_shadow_telemetry.authority_conflicts);
                require_action_shadow_resync();
                return action_shadow_command_result_t::invalid;
            }
            if (command.command.command_id.sequence <
                action_shadow_receipt_frontier.command_id.sequence) {
                if (count_canonical_command)
                    increment_saturated(
                        action_shadow_telemetry.canonical_commands);
                return action_shadow_command_result_t::accepted;
            }
        }
        if (action_shadow_command_coverage_lost &&
            command.command.command_id.epoch !=
                action_shadow_command_coverage_lost_through.epoch) {
            increment_saturated(action_shadow_telemetry.authority_conflicts);
            require_action_shadow_resync();
            return action_shadow_command_result_t::invalid;
        }
        index = allocate_action_shadow_pair_under_command_pressure();
        if (index == -2)
            return action_shadow_command_result_t::invalid;
        if (index < 0) {
            increment_saturated(action_shadow_telemetry.capacity_failures);
            require_action_shadow_resync();
            return action_shadow_command_result_t::capacity;
        }
        action_shadow_pairs[index].flags = ACTION_SHADOW_OCCUPIED;
        action_shadow_pairs[index].command_id = command.command.command_id;
    }

    auto &entry = action_shadow_pairs[index];
    if ((entry.flags & ACTION_SHADOW_COMMAND) != 0) {
        if (entry.command_hash != command_hash ||
            entry.legacy_sequence != command.legacy_sequence) {
            increment_saturated(action_shadow_telemetry.authority_conflicts);
            require_action_shadow_resync();
            return action_shadow_command_result_t::invalid;
        }
        return action_shadow_command_result_t::accepted;
    }

    entry.command_hash = command_hash;
    entry.legacy_sequence = command.legacy_sequence;
    entry.flags |= ACTION_SHADOW_COMMAND;
    if (count_canonical_command)
        increment_saturated(action_shadow_telemetry.canonical_commands);
    if ((entry.flags & ACTION_SHADOW_RECEIPT) == 0)
        return action_shadow_command_result_t::accepted;
    return reconcile_action_shadow_pair(entry) ==
                   cg_local_action_shadow_receipt_result_v1::command_mismatch
               ? action_shadow_command_result_t::invalid
               : action_shadow_command_result_t::accepted;
}

std::uint64_t action_shadow_outstanding_receipts()
{
    std::uint64_t outstanding = 0;
    for (const auto &entry : action_shadow_pairs) {
        if ((entry.flags & (ACTION_SHADOW_OCCUPIED | ACTION_SHADOW_RECEIPT)) ==
                (ACTION_SHADOW_OCCUPIED | ACTION_SHADOW_RECEIPT) &&
            (entry.flags & ACTION_SHADOW_TERMINAL) == 0) {
            increment_saturated(outstanding);
        }
    }
    return outstanding;
}

} // namespace

void CG_LocalInteractionSetImport(
    const worr_cgame_command_record_import_v1 *import)
{
    command_record_import = valid_import(import) ? import : nullptr;
    CG_LocalInteractionReset();
}

void CG_LocalInteractionSetImportV2(
    const worr_cgame_command_record_import_v2 *import)
{
    command_record_import_v2 = valid_import(import) ? import : nullptr;
    CG_LocalInteractionReset();
}

void CG_LocalInteractionReset()
{
    telemetry = {};
    authority_pairs = {};
    action_shadow_telemetry = {};
    action_shadow_pairs = {};
    action_shadow_observed_through = 0;
    action_shadow_observation_started = false;
    action_shadow_command_coverage_lost_through = {};
    action_shadow_command_coverage_lost = false;
    action_shadow_receipt_frontier = {};
    action_shadow_receipt_frontier_started = false;
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

bool CG_LocalActionShadowRequiresResync()
{
    return action_shadow_telemetry.requires_resync != 0;
}

void CG_LocalActionShadowGetStatus(
    cg_local_action_shadow_status_v1 *status_out)
{
    if (!status_out)
        return;
    *status_out = {
        action_shadow_telemetry.observation_passes,
        action_shadow_telemetry.canonical_commands,
        action_shadow_telemetry.authority_receipts,
        action_shadow_telemetry.authority_duplicates,
        action_shadow_telemetry.authority_unmatched,
        action_shadow_telemetry.command_matches,
        action_shadow_telemetry.command_mismatches,
        action_shadow_telemetry.authority_conflicts,
        action_shadow_telemetry.authority_expirations,
        action_shadow_telemetry.capacity_failures,
        action_shadow_outstanding_receipts(),
        action_shadow_telemetry.exact_lookup_attempts,
        action_shadow_telemetry.exact_lookup_hits,
        action_shadow_telemetry.exact_lookup_misses,
        action_shadow_telemetry.command_frontier_prunes,
        action_shadow_telemetry.terminal_frontier_prunes,
        action_shadow_telemetry.receipt_frontier_advances,
        action_shadow_telemetry.command_cache_evictions,
        action_shadow_telemetry.coverage_loss_failures,
        action_shadow_command_coverage_lost_through,
        action_shadow_receipt_frontier.command_id,
        action_shadow_telemetry.requires_resync,
    };
}

cg_local_action_shadow_receipt_result_v1
CG_LocalActionShadowSubmitAuthorityReceipt(
    const worr_local_action_shadow_authority_receipt_v1 *receipt)
{
    int index = -1;

    if (action_shadow_telemetry.requires_resync != 0)
        return cg_local_action_shadow_receipt_result_v1::requires_resync;
    if (!Worr_LocalActionShadowAuthorityReceiptValidateV1(receipt)) {
        require_action_shadow_resync();
        return cg_local_action_shadow_receipt_result_v1::invalid;
    }

    index = find_action_shadow_pair(receipt->command_id);
    if (index >= 0 &&
        (action_shadow_pairs[index].flags & ACTION_SHADOW_RECEIPT) != 0) {
        auto &entry = action_shadow_pairs[index];
        if (std::memcmp(&entry.receipt, receipt, sizeof(*receipt)) == 0) {
            increment_saturated(action_shadow_telemetry.authority_duplicates);
            return cg_local_action_shadow_receipt_result_v1::duplicate;
        }
        increment_saturated(action_shadow_telemetry.authority_conflicts);
        require_action_shadow_resync();
        return cg_local_action_shadow_receipt_result_v1::conflict;
    }
    /* Terminal pairs are retired once the authoritative legacy command
     * frontier covers them, but the latest ordered receipt must remain exactly
     * idempotent. Keep its full bytes, not just its command ID. */
    if (action_shadow_receipt_frontier_started &&
        command_id_equal(receipt->command_id,
                         action_shadow_receipt_frontier.command_id)) {
        if (std::memcmp(&action_shadow_receipt_frontier, receipt,
                        sizeof(*receipt)) == 0) {
            increment_saturated(action_shadow_telemetry.authority_duplicates);
            return cg_local_action_shadow_receipt_result_v1::duplicate;
        }
        increment_saturated(action_shadow_telemetry.authority_conflicts);
        require_action_shadow_resync();
        return cg_local_action_shadow_receipt_result_v1::conflict;
    }
    if (!action_shadow_receipt_frontier_accepts(receipt->command_id)) {
        increment_saturated(action_shadow_telemetry.authority_conflicts);
        require_action_shadow_resync();
        return cg_local_action_shadow_receipt_result_v1::conflict;
    }
    if (action_shadow_command_coverage_lost &&
        receipt->command_id.epoch !=
            action_shadow_command_coverage_lost_through.epoch) {
        increment_saturated(action_shadow_telemetry.authority_conflicts);
        require_action_shadow_resync();
        return cg_local_action_shadow_receipt_result_v1::conflict;
    }
    /* The caller applies private receipts only after their reliable event IDs
     * become contiguous. Server command consumption and mailbox/event FIFO
     * ordering therefore make this command ID an authority-production
     * frontier: no new receipt for a lower command can arrive in this epoch.
     * Retire command-only rows and already-matched terminal pairs strictly
     * below it. Receipt-only rows remain unresolved evidence, and the latest
     * full receipt remains separately retained for exact idempotence. */
    if (!prune_action_shadow_cache_before(receipt->command_id))
        return cg_local_action_shadow_receipt_result_v1::conflict;

    /* Render cadence can skip the interval in which a command is pending,
     * especially across a reconnect or a deliberately frozen player. Resolve
     * the receipt's exact immutable command by canonical ID before recording
     * it as unmatched. No movement range, packet ack, or timestamp is inferred
     * here. */
    if (index < 0) {
        worr_cgame_command_record_entry_v1 command{};
        const auto exact_result = resolve_action_shadow_command_exact(
            receipt->command_id, command);
        if (exact_result == action_shadow_exact_result_t::found) {
            const bool count_canonical_command =
                !action_shadow_observation_started ||
                command.legacy_sequence > action_shadow_observed_through;
            const auto command_result = observe_action_shadow_command(
                command, count_canonical_command);
            if (command_result == action_shadow_command_result_t::capacity)
                return cg_local_action_shadow_receipt_result_v1::capacity;
            if (command_result == action_shadow_command_result_t::invalid)
                return cg_local_action_shadow_receipt_result_v1::invalid;
        } else if (exact_result == action_shadow_exact_result_t::invalid) {
            return cg_local_action_shadow_receipt_result_v1::invalid;
        } else if (action_shadow_command_coverage_was_lost(
                       receipt->command_id)) {
            increment_saturated(
                action_shadow_telemetry.coverage_loss_failures);
            increment_saturated(
                action_shadow_telemetry.authority_expirations);
            require_action_shadow_resync();
            return cg_local_action_shadow_receipt_result_v1::command_mismatch;
        }
    }

    index = find_action_shadow_pair(receipt->command_id);
    if (index < 0) {
        index = allocate_action_shadow_pair_under_command_pressure();
        if (index == -2)
            return cg_local_action_shadow_receipt_result_v1::invalid;
        if (index < 0) {
            increment_saturated(action_shadow_telemetry.capacity_failures);
            require_action_shadow_resync();
            return cg_local_action_shadow_receipt_result_v1::capacity;
        }
        action_shadow_pairs[index].flags = ACTION_SHADOW_OCCUPIED;
        action_shadow_pairs[index].command_id = receipt->command_id;
    }

    auto &entry = action_shadow_pairs[index];
    if ((entry.flags & ACTION_SHADOW_RECEIPT) != 0) {
        increment_saturated(action_shadow_telemetry.authority_conflicts);
        require_action_shadow_resync();
        return cg_local_action_shadow_receipt_result_v1::conflict;
    }

    entry.receipt = *receipt;
    entry.flags |= ACTION_SHADOW_RECEIPT;
    increment_saturated(action_shadow_telemetry.authority_receipts);
    advance_action_shadow_receipt_frontier(*receipt);
    if ((entry.flags & ACTION_SHADOW_COMMAND) == 0) {
        increment_saturated(action_shadow_telemetry.authority_unmatched);
        return cg_local_action_shadow_receipt_result_v1::accepted_unmatched;
    }
    return reconcile_action_shadow_pair(entry);
}

void CG_LocalActionShadowObserveCommands(
    const worr_cgame_prediction_input_range_v1 &prediction_range)
{
    if ((prediction_range.flags & WORR_CGAME_PREDICTION_INPUT_CANONICAL) == 0 ||
        !valid_import(command_record_import) ||
        action_shadow_telemetry.requires_resync != 0) {
        return;
    }
    const std::uint64_t newest_wide =
        static_cast<std::uint64_t>(
            prediction_range.authoritative_legacy_sequence) +
        prediction_range.command_count;
    if (newest_wide > UINT32_MAX) {
        require_action_shadow_resync();
        return;
    }
    const std::uint32_t newest = static_cast<std::uint32_t>(newest_wide);
    if (newest == 0 ||
        (action_shadow_observation_started &&
         newest == action_shadow_observed_through)) {
        (void)prune_action_shadow_pairs(prediction_range, nullptr);
        return;
    }
    if (action_shadow_observation_started &&
        newest < action_shadow_observed_through) {
        require_action_shadow_resync();
        return;
    }

    std::uint32_t first = 1;
    if (action_shadow_observation_started) {
        if (action_shadow_observed_through == UINT32_MAX) {
            require_action_shadow_resync();
            return;
        }
        first = action_shadow_observed_through + 1u;
    } else if (newest >= WORR_CGAME_PREDICTION_INPUT_CAPACITY) {
        first = newest - WORR_CGAME_PREDICTION_INPUT_CAPACITY + 1u;
    }

    bool observed_any = false;
    for (std::uint64_t sequence_wide = first;
         sequence_wide <= newest; ++sequence_wide) {
        const std::uint32_t sequence =
            static_cast<std::uint32_t>(sequence_wide);
        worr_cgame_command_record_range_v1 records{};
        if (!resolve_action_shadow_record(sequence, records)) {
            if (action_shadow_telemetry.requires_resync != 0)
                return;
            if (action_shadow_observation_started) {
                require_action_shadow_resync();
                return;
            }
            continue;
        }
        if (!observed_any &&
            !prune_action_shadow_pairs(prediction_range, &records)) {
            return;
        }
        if (observe_action_shadow_command(records.commands[0], true) !=
            action_shadow_command_result_t::accepted) {
            return;
        }
        action_shadow_observed_through = sequence;
        action_shadow_observation_started = true;
        observed_any = true;
    }
    if (observed_any)
        increment_saturated(action_shadow_telemetry.observation_passes);
    else
        (void)prune_action_shadow_pairs(prediction_range, nullptr);
}
