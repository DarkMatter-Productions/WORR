/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_event_runtime.hpp"
#include "cg_local_interaction.hpp"

#include <array>
#include <cstring>

namespace {

enum : std::uint32_t {
    BODY_REQUIRES_SNAPSHOT_REF = 1u << 0,
    BODY_HAS_SNAPSHOT_REF = 1u << 1,
    BODY_PRESENTED = 1u << 2,
    BODY_MISMATCH = 1u << 3,
};

enum : std::uint32_t {
    AUTHORITY_HAS_SNAPSHOT_REF = 1u << 0,
    AUTHORITY_PRESENTED = 1u << 1,
    AUTHORITY_SKIP = 1u << 2,
    AUTHORITY_REF_MISMATCH = 1u << 3,
    AUTHORITY_SIDE_EFFECT_PRESENTED = 1u << 4,
};

struct authority_entry_t {
    worr_event_record_v1 record;
    worr_event_slot_ref_v1 slot;
    std::uint64_t semantic_hash;
    std::uint64_t resident_order;
    std::uint64_t fence_time_us;
    std::uint32_t fence_tick;
    std::uint32_t state;
    bool occupied;
};

struct prediction_tombstone_t {
    worr_event_record_v1 record;
    worr_event_slot_ref_v1 slot;
    worr_event_id_v1 authority_id;
    std::uint64_t semantic_hash;
    std::uint64_t resident_order;
    bool occupied;
    bool presented;
    bool terminal;
    bool reconciled;
    bool has_authority_id;
};

struct reference_entry_t {
    worr_snapshot_event_ref_v2 ref;
    worr_snapshot_id_v2 snapshot_id;
    std::uint64_t snapshot_time_us;
    std::uint64_t resident_order;
    std::uint32_t snapshot_tick;
    bool occupied;
    bool consumed;
};

struct legacy_body_entry_t {
    std::uint64_t journal_serial;
    std::uint64_t semantic_hash;
    std::uint64_t source_time_us;
    std::uint64_t resident_order;
    std::uint64_t fence_time_us;
    std::uint32_t source_tick;
    std::uint32_t source_ordinal;
    std::uint32_t dense_event_ordinal;
    std::uint32_t carrier_kind;
    std::uint32_t fence_tick;
    std::uint32_t state;
    bool occupied;
};

struct runtime_state_t {
    std::array<worr_event_journal_slot_v1,
               CG_EVENT_RUNTIME_JOURNAL_CAPACITY> journal_slots;
    worr_event_journal_v1 journal;
    std::array<authority_entry_t,
               CG_EVENT_RUNTIME_AUTHORITY_CAPACITY> authority;
    std::array<prediction_tombstone_t,
               CG_EVENT_RUNTIME_PREDICTION_TOMBSTONE_CAPACITY>
        prediction_tombstones;
    std::array<reference_entry_t,
               CG_EVENT_RUNTIME_REFERENCE_CAPACITY> references;
    std::array<legacy_body_entry_t,
               CG_EVENT_RUNTIME_LEGACY_BODY_CAPACITY> legacy_bodies;
    cg_event_runtime_status_v1 status;
    worr_snapshot_id_v2 last_snapshot_id;
    std::uint64_t last_snapshot_time_us;
    std::uint64_t last_snapshot_event_hash;
    std::uint64_t next_resident_order;
    bool legacy_initialized;
    bool authority_initialized;
    bool snapshot_initialized;
    bool has_last_snapshot;
    bool has_prediction_retired_cursor;
};

runtime_state_t runtime;
runtime_state_t staging;
bool transaction_active;

void increment_saturated(std::uint64_t &value)
{
    if (value != UINT64_MAX)
        ++value;
}

std::uint64_t next_order(runtime_state_t &state)
{
    if (state.next_resident_order == UINT64_MAX) {
        increment_saturated(state.status.resident_order_exhaustions);
        state.status.degraded = 1;
        return 0;
    }
    return ++state.next_resident_order;
}

void rebind_journal(runtime_state_t &state)
{
    if (state.journal.struct_size == sizeof(state.journal))
        state.journal.slots = state.journal_slots.data();
}

void copy_state(runtime_state_t &destination,
                const runtime_state_t &source)
{
    destination = source;
    rebind_journal(destination);
}

void commit_staging()
{
    runtime = staging;
    rebind_journal(runtime);
}

void mark_degraded(runtime_state_t &state)
{
    state.status.degraded = 1;
}

void mark_authority_degraded(runtime_state_t &state)
{
    state.status.authority_degraded = 1;
    mark_degraded(state);
}

bool authority_has_unpresented_records(const runtime_state_t &state)
{
    for (const auto &entry : state.authority) {
        if (entry.occupied &&
            (entry.state & (AUTHORITY_PRESENTED | AUTHORITY_SKIP)) == 0) {
            return true;
        }
    }
    return false;
}

bool event_id_equal(worr_event_id_v1 left, worr_event_id_v1 right)
{
    return left.stream_epoch == right.stream_epoch &&
           left.sequence == right.sequence;
}

bool snapshot_id_equal(worr_snapshot_id_v2 left,
                       worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch && left.sequence == right.sequence;
}

bool prediction_key_equal(worr_event_prediction_key_v1 left,
                          worr_event_prediction_key_v1 right)
{
    return left.command_epoch == right.command_epoch &&
           left.command_sequence == right.command_sequence &&
           left.emitter_ordinal == right.emitter_ordinal &&
           left.lane == right.lane;
}

bool is_local_interaction_authority_receipt(
    const worr_event_record_v1 &record)
{
    return record.event_type == WORR_EVENT_TYPE_AUTHORITY_RECEIPT &&
           record.payload_kind ==
               WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1 &&
           record.payload_size ==
               sizeof(worr_local_interaction_authority_receipt_v1);
}

bool local_interaction_receipt_accepted(
    cg_local_interaction_receipt_result_v1 result)
{
    switch (result) {
    case cg_local_interaction_receipt_result_v1::accepted_unmatched:
    case cg_local_interaction_receipt_result_v1::duplicate:
    case cg_local_interaction_receipt_result_v1::hook_confirmed:
    case cg_local_interaction_receipt_result_v1::hook_rejected:
        return true;
    case cg_local_interaction_receipt_result_v1::diverged:
    case cg_local_interaction_receipt_result_v1::invalid:
    case cg_local_interaction_receipt_result_v1::conflict:
    case cg_local_interaction_receipt_result_v1::capacity:
    case cg_local_interaction_receipt_result_v1::requires_resync:
        return false;
    }
    return false;
}

void latch_local_interaction_resync()
{
    if (!runtime.authority_initialized ||
        runtime.status.authority_requires_resync != 0 ||
        !CG_LocalInteractionRequiresResync()) {
        return;
    }
    runtime.status.authority_requires_resync = 1;
    mark_authority_degraded(runtime);
}

bool prediction_key_at_or_before_cursor(
    worr_event_prediction_key_v1 key, worr_command_cursor_v1 cursor)
{
    return key.command_epoch < cursor.epoch ||
           (key.command_epoch == cursor.epoch &&
            key.command_sequence <= cursor.contiguous_sequence);
}

bool cursor_before(worr_command_cursor_v1 left,
                   worr_command_cursor_v1 right)
{
    return left.epoch < right.epoch ||
           (left.epoch == right.epoch &&
            left.contiguous_sequence < right.contiguous_sequence);
}

bool cursor_equal(worr_command_cursor_v1 left,
                  worr_command_cursor_v1 right)
{
    return left.epoch == right.epoch &&
           left.contiguous_sequence == right.contiguous_sequence;
}

bool tick_reached(std::uint32_t now_tick, std::uint32_t deadline_tick)
{
    return now_tick - deadline_tick <= INT32_MAX;
}

bool authoritative_records_equal(const worr_event_record_v1 &left,
                                 const worr_event_record_v1 &right)
{
    return left.flags == right.flags &&
           event_id_equal(left.event_id, right.event_id) &&
           Worr_EventRecordSemanticallyEqualV1(
               &left, &right, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2);
}

void update_high_water(std::uint32_t current, std::uint32_t &high_water)
{
    if (current > high_water)
        high_water = current;
}

void decrement_if_nonzero(std::uint32_t &value)
{
    if (value)
        --value;
}

std::uint64_t hash_u32(std::uint64_t hash, std::uint32_t value)
{
    constexpr std::uint64_t prime = UINT64_C(1099511628211);
    for (unsigned shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<std::uint8_t>(value >> shift);
        hash *= prime;
    }
    return hash;
}

std::uint64_t hash_u64(std::uint64_t hash, std::uint64_t value)
{
    hash = hash_u32(hash, static_cast<std::uint32_t>(value));
    return hash_u32(hash, static_cast<std::uint32_t>(value >> 32));
}

void audit_present(runtime_state_t &state, std::uint32_t provenance,
                   std::uint64_t serial_or_id, std::uint64_t semantic_hash,
                   std::uint32_t fence_tick, std::uint64_t fence_time_us)
{
    std::uint64_t hash = state.status.presentation_chain_hash;
    if (!hash)
        hash = UINT64_C(1469598103934665603);
    hash = hash_u32(hash, UINT32_C(0x45565231)); /* EVR1 */
    hash = hash_u32(hash, provenance);
    hash = hash_u64(hash, serial_or_id);
    hash = hash_u64(hash, semantic_hash);
    hash = hash_u32(hash, fence_tick);
    hash = hash_u64(hash, fence_time_us);
    state.status.presentation_chain_hash = hash;
}

authority_entry_t *find_authority(runtime_state_t &state,
                                  worr_event_id_v1 id)
{
    for (auto &entry : state.authority) {
        if (entry.occupied && event_id_equal(entry.record.event_id, id))
            return &entry;
    }
    return nullptr;
}

const authority_entry_t *find_authority(const runtime_state_t &state,
                                        worr_event_id_v1 id)
{
    for (const auto &entry : state.authority) {
        if (entry.occupied && event_id_equal(entry.record.event_id, id))
            return &entry;
    }
    return nullptr;
}

authority_entry_t *find_authority_by_prediction_key(
    runtime_state_t &state, worr_event_prediction_key_v1 key)
{
    for (auto &entry : state.authority) {
        if (entry.occupied &&
            entry.record.prediction_class !=
                WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY &&
            prediction_key_equal(entry.record.prediction_key, key)) {
            return &entry;
        }
    }
    return nullptr;
}

prediction_tombstone_t *find_prediction_tombstone(
    runtime_state_t &state, worr_event_prediction_key_v1 key)
{
    for (auto &entry : state.prediction_tombstones) {
        if (entry.occupied &&
            prediction_key_equal(entry.record.prediction_key, key)) {
            return &entry;
        }
    }
    return nullptr;
}

prediction_tombstone_t *allocate_prediction_tombstone(
    runtime_state_t &state)
{
    for (auto &entry : state.prediction_tombstones) {
        if (!entry.occupied) {
            ++state.status.prediction_tombstone_count;
            update_high_water(
                state.status.prediction_tombstone_count,
                state.status.prediction_tombstone_high_water);
            return &entry;
        }
    }

    prediction_tombstone_t *oldest = nullptr;
    for (auto &entry : state.prediction_tombstones) {
        const bool safely_reclaimable =
            state.has_prediction_retired_cursor &&
            prediction_key_at_or_before_cursor(
                entry.record.prediction_key,
                state.status.prediction_retired_through) &&
            (entry.reconciled || (entry.terminal && !entry.presented));
        if (safely_reclaimable &&
            (!oldest || entry.resident_order < oldest->resident_order)) {
            oldest = &entry;
        }
    }
    if (!oldest)
        return nullptr;
    increment_saturated(state.status.prediction_tombstone_evictions);
    *oldest = {};
    return oldest;
}

authority_entry_t *allocate_authority(runtime_state_t &state)
{
    for (auto &entry : state.authority) {
        if (!entry.occupied) {
            ++state.status.authority_count;
            update_high_water(state.status.authority_count,
                              state.status.authority_high_water);
            return &entry;
        }
    }

    authority_entry_t *oldest = nullptr;
    for (auto &entry : state.authority) {
        if ((entry.state & AUTHORITY_PRESENTED) != 0 &&
            (!oldest || entry.resident_order < oldest->resident_order)) {
            oldest = &entry;
        }
    }
    if (!oldest)
        return nullptr;
    increment_saturated(state.status.tombstone_evictions);
    *oldest = {};
    return oldest;
}

reference_entry_t *allocate_reference(runtime_state_t &state)
{
    for (auto &entry : state.references) {
        if (!entry.occupied) {
            ++state.status.reference_count;
            update_high_water(state.status.reference_count,
                              state.status.reference_high_water);
            return &entry;
        }
    }

    reference_entry_t *oldest = nullptr;
    for (auto &entry : state.references) {
        if (entry.consumed &&
            (!oldest || entry.resident_order < oldest->resident_order)) {
            oldest = &entry;
        }
    }
    if (!oldest)
        return nullptr;
    *oldest = {};
    return oldest;
}

legacy_body_entry_t *allocate_legacy_body(runtime_state_t &state)
{
    for (auto &entry : state.legacy_bodies) {
        if (!entry.occupied) {
            ++state.status.legacy_body_count;
            update_high_water(state.status.legacy_body_count,
                              state.status.legacy_body_high_water);
            return &entry;
        }
    }

    legacy_body_entry_t *oldest = nullptr;
    for (auto &entry : state.legacy_bodies) {
        if ((entry.state & BODY_PRESENTED) != 0 &&
            (!oldest || entry.resident_order < oldest->resident_order)) {
            oldest = &entry;
        }
    }
    if (!oldest) {
        for (auto &entry : state.legacy_bodies) {
            if (!oldest || entry.resident_order < oldest->resident_order)
                oldest = &entry;
        }
        if (!oldest)
            return nullptr;
        increment_saturated(state.status.legacy_body_overruns);
        mark_degraded(state);
    }
    *oldest = {};
    return oldest;
}

reference_entry_t *find_authority_reference(runtime_state_t &state,
                                            worr_event_id_v1 id)
{
    for (auto &entry : state.references) {
        if (entry.occupied && !entry.consumed &&
            entry.ref.provenance ==
                WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY &&
            event_id_equal(entry.ref.authority_id, id)) {
            return &entry;
        }
    }
    return nullptr;
}

reference_entry_t *find_legacy_reference(runtime_state_t &state,
                                         std::uint32_t tick,
                                         std::uint32_t dense_event_ordinal)
{
    for (auto &entry : state.references) {
        if (entry.occupied && !entry.consumed &&
            entry.ref.provenance ==
                WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED &&
            entry.snapshot_tick == tick &&
            entry.ref.carrier_ordinal == dense_event_ordinal) {
            return &entry;
        }
    }
    return nullptr;
}

legacy_body_entry_t *find_legacy_body(runtime_state_t &state,
                                      std::uint32_t tick,
                                      std::uint32_t dense_event_ordinal)
{
    for (auto &entry : state.legacy_bodies) {
        if (entry.occupied &&
            (entry.state & BODY_REQUIRES_SNAPSHOT_REF) != 0 &&
            entry.source_tick == tick &&
            entry.dense_event_ordinal == dense_event_ordinal) {
            return &entry;
        }
    }
    return nullptr;
}

bool attach_authority_reference(runtime_state_t &state,
                                authority_entry_t &authority,
                                reference_entry_t &reference)
{
    if (reference.ref.semantic_version != authority.record.model_revision ||
        reference.ref.semantic_hash != authority.semantic_hash) {
        if ((authority.state & AUTHORITY_REF_MISMATCH) == 0) {
            authority.state |= AUTHORITY_REF_MISMATCH;
            increment_saturated(state.status.reference_conflicts);
            mark_degraded(state);
        }
        return false;
    }
    authority.state |= AUTHORITY_HAS_SNAPSHOT_REF;
    authority.fence_tick = reference.snapshot_tick;
    authority.fence_time_us = reference.snapshot_time_us;
    reference.consumed = true;
    increment_saturated(state.status.authority_ref_body_joins);
    return true;
}

bool attach_legacy_reference(runtime_state_t &state,
                             legacy_body_entry_t &body,
                             reference_entry_t &reference,
                             bool reference_arrived_first)
{
    if (reference.ref.semantic_version != WORR_EVENT_MODEL_REVISION ||
        reference.ref.semantic_hash != body.semantic_hash) {
        if ((body.state & BODY_MISMATCH) == 0) {
            body.state |= BODY_MISMATCH;
            increment_saturated(state.status.legacy_ref_body_mismatches);
            mark_degraded(state);
        }
        return false;
    }
    body.state |= BODY_HAS_SNAPSHOT_REF;
    body.fence_tick = reference.snapshot_tick;
    body.fence_time_us = reference.snapshot_time_us;
    reference.consumed = true;
    if (reference_arrived_first)
        increment_saturated(state.status.legacy_ref_before_body_joins);
    else
        increment_saturated(state.status.legacy_body_before_ref_joins);
    return true;
}

cg_event_runtime_result_v1 map_journal_result(
    worr_event_journal_result_v1 result)
{
    switch (result) {
    case WORR_EVENT_JOURNAL_INSERTED:
    case WORR_EVENT_JOURNAL_COALESCED:
    case WORR_EVENT_JOURNAL_SUPERSEDED:
    case WORR_EVENT_JOURNAL_DROPPED_STALE:
        return CG_EVENT_RUNTIME_OK;
    case WORR_EVENT_JOURNAL_DUPLICATE:
        return CG_EVENT_RUNTIME_DUPLICATE;
    case WORR_EVENT_JOURNAL_MATCHED:
        return CG_EVENT_RUNTIME_MATCHED;
    case WORR_EVENT_JOURNAL_CORRECTED:
        return CG_EVENT_RUNTIME_CORRECTED;
    case WORR_EVENT_JOURNAL_CORRECTED_AFTER_PRESENTATION:
        return CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION;
    case WORR_EVENT_JOURNAL_WRONG_EPOCH:
        return CG_EVENT_RUNTIME_WRONG_EPOCH;
    case WORR_EVENT_JOURNAL_INVALID_RECORD:
        return CG_EVENT_RUNTIME_INVALID_RECORD;
    case WORR_EVENT_JOURNAL_CONFLICT:
        return CG_EVENT_RUNTIME_CONFLICT;
    case WORR_EVENT_JOURNAL_DROPPED_OVERFLOW:
    case WORR_EVENT_JOURNAL_CAPACITY_FATAL:
    case WORR_EVENT_JOURNAL_ACK_WINDOW:
        return CG_EVENT_RUNTIME_CAPACITY;
    case WORR_EVENT_JOURNAL_NOT_FOUND:
        return CG_EVENT_RUNTIME_NOT_FOUND;
    case WORR_EVENT_JOURNAL_TERMINAL:
        return CG_EVENT_RUNTIME_TERMINAL;
    default:
        return CG_EVENT_RUNTIME_DEGRADED;
    }
}

int result_rank(cg_event_runtime_result_v1 result)
{
    switch (result) {
    case CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION:
        return 4;
    case CG_EVENT_RUNTIME_CORRECTED:
        return 3;
    case CG_EVENT_RUNTIME_MATCHED:
        return 2;
    case CG_EVENT_RUNTIME_DUPLICATE:
        return 1;
    default:
        return 0;
    }
}

void note_reconciliation(runtime_state_t &state,
                         cg_event_runtime_result_v1 result)
{
    switch (result) {
    case CG_EVENT_RUNTIME_DUPLICATE:
        increment_saturated(state.status.predicted_duplicates);
        break;
    case CG_EVENT_RUNTIME_MATCHED:
        increment_saturated(state.status.prediction_matches);
        break;
    case CG_EVENT_RUNTIME_CORRECTED:
        increment_saturated(state.status.prediction_corrections);
        break;
    case CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION:
        increment_saturated(state.status.prediction_late_corrections);
        break;
    default:
        break;
    }
}

void mark_displaced_authority(runtime_state_t &state,
                              worr_event_slot_ref_v1 replacement)
{
    for (auto &entry : state.authority) {
        if (!entry.occupied || (entry.state & AUTHORITY_PRESENTED) != 0 ||
            (entry.state & AUTHORITY_SIDE_EFFECT_PRESENTED) != 0)
            continue;
        if (entry.slot.index == replacement.index &&
            entry.slot.generation != replacement.generation &&
            (entry.state & AUTHORITY_SKIP) == 0) {
            entry.state |= AUTHORITY_SKIP;
            increment_saturated(
                state.status.authoritative_stale_or_coalesced);
        }
    }
}

void mark_displaced_predictions(runtime_state_t &state,
                                worr_event_slot_ref_v1 replacement)
{
    for (auto &entry : state.prediction_tombstones) {
        if (!entry.occupied || entry.reconciled || entry.presented ||
            entry.terminal) {
            continue;
        }
        if (entry.slot.index == replacement.index &&
            entry.slot.generation != replacement.generation) {
            entry.terminal = true;
        }
    }
}

cg_event_runtime_result_v1 insert_authority(runtime_state_t &state,
                                            const worr_event_record_v1 &record)
{
    if (auto *existing = find_authority(state, record.event_id)) {
        if (authoritative_records_equal(existing->record, record)) {
            increment_saturated(state.status.authoritative_duplicates);
            return CG_EVENT_RUNTIME_DUPLICATE;
        }
        increment_saturated(state.status.authoritative_conflicts);
        mark_degraded(state);
        return CG_EVENT_RUNTIME_CONFLICT;
    }

    prediction_tombstone_t *prediction = nullptr;
    if (record.prediction_class !=
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY) {
        if (auto *key_authority = find_authority_by_prediction_key(
                state, record.prediction_key)) {
            if (!event_id_equal(key_authority->record.event_id,
                                record.event_id)) {
                increment_saturated(
                    state.status.authoritative_conflicts);
                mark_degraded(state);
                return CG_EVENT_RUNTIME_CONFLICT;
            }
        }
        prediction = find_prediction_tombstone(
            state, record.prediction_key);
        if (prediction && prediction->reconciled &&
            prediction->has_authority_id &&
            !event_id_equal(prediction->authority_id,
                            record.event_id)) {
            increment_saturated(state.status.authoritative_conflicts);
            mark_degraded(state);
            return CG_EVENT_RUNTIME_CONFLICT;
        }
    }

    std::uint64_t semantic_hash = 0;
    if (!Worr_EventRecordSemanticHashV1(
            &record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
            &semantic_hash)) {
        return CG_EVENT_RUNTIME_INVALID_RECORD;
    }

    worr_event_slot_ref_v1 slot{};
    const auto journal_result = Worr_EventJournalInsertAuthoritativeV1(
        &state.journal, &record, &slot);
    const auto result = map_journal_result(journal_result);
    if (journal_result == WORR_EVENT_JOURNAL_DUPLICATE) {
        increment_saturated(state.status.authoritative_duplicates);
        return CG_EVENT_RUNTIME_DUPLICATE;
    }
    if (result == CG_EVENT_RUNTIME_CONFLICT ||
        result == CG_EVENT_RUNTIME_CAPACITY ||
        result == CG_EVENT_RUNTIME_INVALID_RECORD ||
        result == CG_EVENT_RUNTIME_WRONG_EPOCH ||
        result == CG_EVENT_RUNTIME_DEGRADED) {
        return result;
    }

    authority_entry_t *binding = allocate_authority(state);
    if (!binding)
        return CG_EVENT_RUNTIME_CAPACITY;

    if (slot.index != WORR_EVENT_SLOT_INVALID) {
        mark_displaced_authority(state, slot);
        mark_displaced_predictions(state, slot);
    }

    auto reconciliation_result = result;
    if (prediction) {
        const bool semantic_match =
            Worr_EventRecordSemanticallyEqualV1(
                &prediction->record, &record,
                WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2);
        reconciliation_result = semantic_match
                                    ? CG_EVENT_RUNTIME_MATCHED
                                    : (prediction->presented
                                           ? CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION
                                           : CG_EVENT_RUNTIME_CORRECTED);
        prediction->reconciled = true;
        prediction->authority_id = record.event_id;
        prediction->has_authority_id = true;
        if (prediction->presented &&
            slot.index < state.journal.capacity) {
            auto &resolved = state.journal_slots[slot.index];
            if (resolved.generation == slot.generation &&
                (resolved.state & WORR_EVENT_SLOT_RECEIVED) != 0 &&
                event_id_equal(resolved.record.event_id,
                               record.event_id)) {
                resolved.state |= WORR_EVENT_SLOT_PRESENTED;
            }
        }
    }

    *binding = {};
    binding->record = record;
    binding->slot = slot;
    binding->semantic_hash = semantic_hash;
    binding->resident_order = next_order(state);
    if (!binding->resident_order)
        return CG_EVENT_RUNTIME_DEGRADED;
    binding->occupied = true;
    if (prediction && prediction->presented)
        binding->state |= AUTHORITY_SIDE_EFFECT_PRESENTED;
    if (is_local_interaction_authority_receipt(record)) {
        /* Authority receipts carry reconciliation evidence only. They are
         * never snapshot-fenced or presented by the generic event runtime. */
        binding->state |= AUTHORITY_SKIP;
    } else if (journal_result == WORR_EVENT_JOURNAL_DROPPED_STALE) {
        binding->state |= AUTHORITY_SKIP;
        increment_saturated(
            state.status.authoritative_stale_or_coalesced);
    }
    bool attachment_degraded = false;
    if (auto *reference = find_authority_reference(
            state, record.event_id)) {
        attachment_degraded =
            !attach_authority_reference(state, *binding, *reference);
    }

    increment_saturated(state.status.authoritative_records);
    note_reconciliation(state, reconciliation_result);
    return attachment_degraded ? CG_EVENT_RUNTIME_DEGRADED
                               : reconciliation_result;
}

cg_event_runtime_result_v1 insert_prediction(runtime_state_t &state,
                                             const worr_event_record_v1 &record)
{
    std::uint64_t semantic_hash = 0;
    if (!Worr_EventRecordSemanticHashV1(
            &record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
            &semantic_hash)) {
        return CG_EVENT_RUNTIME_INVALID_RECORD;
    }
    if (state.has_prediction_retired_cursor &&
        prediction_key_at_or_before_cursor(
            record.prediction_key,
            state.status.prediction_retired_through)) {
        increment_saturated(state.status.stale_prediction_rejections);
        return CG_EVENT_RUNTIME_TERMINAL;
    }

    if (auto *existing = find_prediction_tombstone(
            state, record.prediction_key)) {
        if (!Worr_EventRecordSemanticallyEqualV1(
                &existing->record, &record,
                WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2)) {
            return CG_EVENT_RUNTIME_CONFLICT;
        }
        increment_saturated(state.status.predicted_duplicates);
        return CG_EVENT_RUNTIME_DUPLICATE;
    }

    if (auto *authority = find_authority_by_prediction_key(
            state, record.prediction_key)) {
        const bool semantic_match =
            Worr_EventRecordSemanticallyEqualV1(
                &authority->record, &record,
                WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2);
        const bool authority_presented =
            (authority->state & (AUTHORITY_PRESENTED |
                                 AUTHORITY_SIDE_EFFECT_PRESENTED)) != 0;
        const auto result = semantic_match
                                ? CG_EVENT_RUNTIME_MATCHED
                                : (authority_presented
                                       ? CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION
                                       : CG_EVENT_RUNTIME_CORRECTED);
        auto *tombstone = allocate_prediction_tombstone(state);
        if (!tombstone) {
            increment_saturated(
                state.status.prediction_tombstone_capacity_failures);
            return CG_EVENT_RUNTIME_CAPACITY;
        }
        *tombstone = {};
        tombstone->record = record;
        tombstone->slot = {WORR_EVENT_SLOT_INVALID, 0};
        tombstone->semantic_hash = semantic_hash;
        tombstone->resident_order = next_order(state);
        if (!tombstone->resident_order)
            return CG_EVENT_RUNTIME_DEGRADED;
        tombstone->occupied = true;
        tombstone->presented = authority_presented;
        tombstone->reconciled = true;
        tombstone->authority_id = authority->record.event_id;
        tombstone->has_authority_id = true;
        increment_saturated(state.status.predicted_records);
        note_reconciliation(state, result);
        return result;
    }

    worr_event_slot_ref_v1 slot{};
    const auto result = map_journal_result(
        Worr_EventJournalInsertPredictedV1(&state.journal, &record, &slot));
    if (result != CG_EVENT_RUNTIME_CONFLICT &&
        result != CG_EVENT_RUNTIME_CAPACITY &&
        result != CG_EVENT_RUNTIME_INVALID_RECORD &&
        result != CG_EVENT_RUNTIME_WRONG_EPOCH &&
        result != CG_EVENT_RUNTIME_DEGRADED &&
        slot.index != WORR_EVENT_SLOT_INVALID) {
        mark_displaced_authority(state, slot);
        mark_displaced_predictions(state, slot);
    }
    if (result == CG_EVENT_RUNTIME_CONFLICT ||
        result == CG_EVENT_RUNTIME_CAPACITY ||
        result == CG_EVENT_RUNTIME_INVALID_RECORD ||
        result == CG_EVENT_RUNTIME_WRONG_EPOCH ||
        result == CG_EVENT_RUNTIME_DEGRADED) {
        return result;
    }

    auto *tombstone = allocate_prediction_tombstone(state);
    if (!tombstone) {
        increment_saturated(
            state.status.prediction_tombstone_capacity_failures);
        return CG_EVENT_RUNTIME_CAPACITY;
    }
    *tombstone = {};
    tombstone->record = record;
    tombstone->slot = slot;
    tombstone->semantic_hash = semantic_hash;
    tombstone->resident_order = next_order(state);
    if (!tombstone->resident_order)
        return CG_EVENT_RUNTIME_DEGRADED;
    tombstone->occupied = true;
    tombstone->reconciled =
        result == CG_EVENT_RUNTIME_MATCHED ||
        result == CG_EVENT_RUNTIME_CORRECTED ||
        result == CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION;
    if (slot.index < state.journal.capacity) {
        const auto &resolved = state.journal_slots[slot.index];
        if (resolved.generation == slot.generation) {
            tombstone->presented =
                (resolved.state & WORR_EVENT_SLOT_PRESENTED) != 0;
            tombstone->terminal =
                (resolved.state & (WORR_EVENT_SLOT_CANCELED |
                                   WORR_EVENT_SLOT_EXPIRED)) != 0;
            if (tombstone->reconciled &&
                (resolved.state & WORR_EVENT_SLOT_RECEIVED) != 0) {
                tombstone->authority_id = resolved.record.event_id;
                tombstone->has_authority_id = true;
            }
        }
    }
    if (tombstone->reconciled && !tombstone->has_authority_id)
        return CG_EVENT_RUNTIME_DEGRADED;
    increment_saturated(state.status.predicted_records);
    note_reconciliation(state, result);
    return result;
}

bool reference_key_equal(const reference_entry_t &entry,
                         const worr_snapshot_event_ref_v2 &ref,
                         std::uint32_t snapshot_tick)
{
    if (entry.ref.provenance != ref.provenance)
        return false;
    if (ref.provenance == WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY)
        return event_id_equal(entry.ref.authority_id, ref.authority_id);
    return entry.snapshot_tick == snapshot_tick &&
           entry.ref.carrier_ordinal == ref.carrier_ordinal;
}

cg_event_runtime_result_v1 insert_reference(
    runtime_state_t &state, const worr_snapshot_v2 &snapshot,
    const worr_snapshot_event_ref_v2 &ref)
{
    for (auto &entry : state.references) {
        if (!entry.occupied || !reference_key_equal(
                                   entry, ref, snapshot.server_tick)) {
            continue;
        }
        if (entry.ref.semantic_version == ref.semantic_version &&
            entry.ref.semantic_hash == ref.semantic_hash) {
            increment_saturated(state.status.reference_duplicates);
            return CG_EVENT_RUNTIME_DUPLICATE;
        }
        increment_saturated(state.status.reference_conflicts);
        mark_degraded(state);
        return CG_EVENT_RUNTIME_CONFLICT;
    }

    reference_entry_t *entry = allocate_reference(state);
    if (!entry)
        return CG_EVENT_RUNTIME_CAPACITY;
    *entry = {};
    entry->ref = ref;
    entry->snapshot_id = snapshot.snapshot_id;
    entry->snapshot_time_us = snapshot.server_time_us;
    entry->snapshot_tick = snapshot.server_tick;
    entry->resident_order = next_order(state);
    if (!entry->resident_order) {
        *entry = {};
        decrement_if_nonzero(state.status.reference_count);
        return CG_EVENT_RUNTIME_DEGRADED;
    }
    entry->occupied = true;
    increment_saturated(state.status.references_observed);

    if (ref.provenance == WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY) {
        if (auto *authority = find_authority(state, ref.authority_id)) {
            if (!attach_authority_reference(state, *authority, *entry))
                return CG_EVENT_RUNTIME_DEGRADED;
        }
    } else if (auto *body = find_legacy_body(
                   state, snapshot.server_tick, ref.carrier_ordinal)) {
        if (!attach_legacy_reference(state, *body, *entry, false))
            return CG_EVENT_RUNTIME_DEGRADED;
    }
    return CG_EVENT_RUNTIME_OK;
}

void clear_authority_references(runtime_state_t &state)
{
    for (auto &entry : state.references) {
        if (entry.occupied && entry.ref.provenance ==
                                  WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY) {
            entry = {};
            decrement_if_nonzero(state.status.reference_count);
        }
    }
}

void clear_legacy_references(runtime_state_t &state)
{
    for (auto &entry : state.references) {
        if (entry.occupied && entry.ref.provenance ==
                                  WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED) {
            entry = {};
            decrement_if_nonzero(state.status.reference_count);
        }
    }
}

void clear_snapshot_fences(runtime_state_t &state)
{
    for (auto &entry : state.references)
        entry = {};
    state.status.reference_count = 0;
    for (auto &entry : state.authority) {
        entry.state &= ~(AUTHORITY_HAS_SNAPSHOT_REF |
                         AUTHORITY_REF_MISMATCH);
        entry.fence_tick = 0;
        entry.fence_time_us = 0;
    }
    for (auto &entry : state.legacy_bodies) {
        if (!entry.occupied ||
            (entry.state & BODY_REQUIRES_SNAPSHOT_REF) == 0) {
            continue;
        }
        if ((entry.state & BODY_PRESENTED) == 0) {
            increment_saturated(
                state.status.legacy_snapshot_reset_discards);
        }
        entry = {};
        decrement_if_nonzero(state.status.legacy_body_count);
    }
}

worr_event_journal_slot_v1 *resolve_journal_mutable(
    runtime_state_t &state, worr_event_slot_ref_v1 ref)
{
    if (ref.index >= state.journal.capacity || ref.generation == 0)
        return nullptr;
    auto &slot = state.journal_slots[ref.index];
    if (slot.state == 0 || slot.generation != ref.generation)
        return nullptr;
    return &slot;
}

cg_event_runtime_result_v1 present_predictions(
    runtime_state_t &state, std::uint64_t render_time_us,
    std::uint32_t now_tick, std::uint32_t max_presentations,
    std::uint32_t &advanced)
{
    while (advanced < max_presentations) {
        worr_event_journal_slot_v1 *best = nullptr;
        std::uint32_t best_index = 0;
        for (std::uint32_t index = 0; index < state.journal.capacity;
             ++index) {
            auto &slot = state.journal_slots[index];
            if (slot.state == 0 ||
                (slot.state & (WORR_EVENT_SLOT_PREDICTED |
                               WORR_EVENT_SLOT_RECEIVED |
                               WORR_EVENT_SLOT_PRESENTED |
                               WORR_EVENT_SLOT_EXPIRED |
                               WORR_EVENT_SLOT_CANCELED)) !=
                    WORR_EVENT_SLOT_PREDICTED ||
                slot.record.prediction_class !=
                    WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE) {
                continue;
            }
            if (!best || slot.resident_order < best->resident_order) {
                best = &slot;
                best_index = index;
            }
        }
        if (!best)
            return CG_EVENT_RUNTIME_OK;
        if (best->record.source_time_us > render_time_us ||
            !tick_reached(now_tick, best->record.source_tick)) {
            increment_saturated(state.status.future_time_stalls);
            return CG_EVENT_RUNTIME_NOT_READY;
        }
        worr_event_slot_ref_v1 ref{best_index, best->generation};
        if (!Worr_EventJournalNeedsPresentationV1(
                &state.journal, ref, now_tick)) {
            continue;
        }
        if (Worr_EventJournalMarkPresentedV1(&state.journal, ref) !=
            WORR_EVENT_JOURNAL_INSERTED) {
            mark_degraded(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        auto *tombstone = find_prediction_tombstone(
            state, best->record.prediction_key);
        if (!tombstone) {
            mark_degraded(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        tombstone->presented = true;
        std::uint64_t semantic_hash = 0;
        if (!Worr_EventRecordSemanticHashV1(
                &best->record, state.journal.max_entities,
                &semantic_hash)) {
            mark_degraded(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        audit_present(state, 1u, best->resident_order, semantic_hash,
                      best->record.source_tick,
                      best->record.source_time_us);
        increment_saturated(state.status.predicted_presentations);
        ++advanced;
    }
    return CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1 present_authority(
    runtime_state_t &state, std::uint64_t render_time_us,
    std::uint32_t now_tick, std::uint32_t max_presentations,
    std::uint32_t &advanced)
{
    std::uint32_t processed = 0;
    while (advanced < max_presentations &&
           processed < max_presentations) {
        worr_event_id_v1 id{state.status.authority_epoch,
                            state.status.next_authority_sequence};
        authority_entry_t *entry = find_authority(state, id);
        if (!entry) {
            increment_saturated(state.status.authority_sequence_stalls);
            return CG_EVENT_RUNTIME_NOT_READY;
        }
        ++processed;

        if ((entry->state & AUTHORITY_SKIP) != 0) {
            entry->state |= AUTHORITY_PRESENTED;
            increment_saturated(state.status.authoritative_terminal_skips);
            ++state.status.next_authority_sequence;
            if (!state.status.next_authority_sequence) {
                mark_degraded(state);
                return CG_EVENT_RUNTIME_DEGRADED;
            }
            continue;
        }
        worr_event_journal_slot_v1 *slot = nullptr;
        if ((entry->state & AUTHORITY_SIDE_EFFECT_PRESENTED) == 0) {
            slot = resolve_journal_mutable(state, entry->slot);
            if (!slot) {
                mark_degraded(state);
                return CG_EVENT_RUNTIME_DEGRADED;
            }
            /* Once an authoritative transient is terminal it can no longer
             * emit a side effect. Account its ordered sequence even if the
             * ref-carrying snapshot was lost, otherwise every later event is
             * permanently head-of-line blocked behind an impossible join. */
            if ((slot->state & (WORR_EVENT_SLOT_EXPIRED |
                                WORR_EVENT_SLOT_CANCELED)) != 0) {
                entry->state |= AUTHORITY_PRESENTED;
                increment_saturated(
                    state.status.authoritative_terminal_skips);
                ++state.status.next_authority_sequence;
                if (!state.status.next_authority_sequence) {
                    mark_degraded(state);
                    return CG_EVENT_RUNTIME_DEGRADED;
                }
                continue;
            }
        }

        if ((entry->state & AUTHORITY_REF_MISMATCH) != 0) {
            mark_degraded(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        if ((entry->state & AUTHORITY_HAS_SNAPSHOT_REF) == 0) {
            increment_saturated(state.status.authority_reference_stalls);
            return CG_EVENT_RUNTIME_NOT_READY;
        }
        if (entry->fence_time_us > render_time_us ||
            entry->record.source_time_us > render_time_us ||
            !tick_reached(now_tick, entry->fence_tick) ||
            !tick_reached(now_tick, entry->record.source_tick)) {
            increment_saturated(state.status.future_time_stalls);
            return CG_EVENT_RUNTIME_NOT_READY;
        }

        if ((entry->state & AUTHORITY_SIDE_EFFECT_PRESENTED) != 0) {
            entry->state |= AUTHORITY_PRESENTED;
            increment_saturated(
                state.status.authoritative_prediction_suppressions);
            ++state.status.next_authority_sequence;
            if (!state.status.next_authority_sequence) {
                mark_degraded(state);
                return CG_EVENT_RUNTIME_DEGRADED;
            }
            continue;
        }

        if ((slot->state & WORR_EVENT_SLOT_PRESENTED) != 0) {
            entry->state |= AUTHORITY_PRESENTED;
            increment_saturated(
                state.status.authoritative_prediction_suppressions);
        } else if ((slot->state & (WORR_EVENT_SLOT_EXPIRED |
                                   WORR_EVENT_SLOT_CANCELED)) != 0) {
            entry->state |= AUTHORITY_PRESENTED;
            increment_saturated(state.status.authoritative_terminal_skips);
        } else {
            if (!Worr_EventJournalNeedsPresentationV1(
                    &state.journal, entry->slot, now_tick)) {
                return CG_EVENT_RUNTIME_NOT_READY;
            }
            if (Worr_EventJournalMarkPresentedV1(
                    &state.journal, entry->slot) !=
                WORR_EVENT_JOURNAL_INSERTED) {
                mark_degraded(state);
                return CG_EVENT_RUNTIME_DEGRADED;
            }
            entry->state |= AUTHORITY_PRESENTED;
            audit_present(state, 2u, entry->record.event_id.sequence,
                          entry->semantic_hash, entry->fence_tick,
                          entry->fence_time_us);
            increment_saturated(
                state.status.authoritative_presentations);
            ++advanced;
        }

        ++state.status.next_authority_sequence;
        if (!state.status.next_authority_sequence) {
            mark_degraded(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
    }
    return CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1 present_legacy(
    runtime_state_t &state, std::uint64_t render_time_us,
    std::uint32_t now_tick, std::uint32_t max_presentations,
    std::uint32_t &advanced)
{
    while (advanced < max_presentations) {
        legacy_body_entry_t *best = nullptr;
        for (auto &entry : state.legacy_bodies) {
            if (!entry.occupied ||
                (entry.state & BODY_PRESENTED) != 0)
                continue;
            if (!best || entry.journal_serial < best->journal_serial)
                best = &entry;
        }
        if (!best)
            return CG_EVENT_RUNTIME_OK;
        if ((best->state & BODY_MISMATCH) != 0) {
            mark_degraded(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        if ((best->state & BODY_REQUIRES_SNAPSHOT_REF) != 0 &&
            (best->state & BODY_HAS_SNAPSHOT_REF) == 0) {
            increment_saturated(state.status.legacy_reference_stalls);
            return CG_EVENT_RUNTIME_NOT_READY;
        }
        const std::uint64_t fence_time =
            (best->state & BODY_REQUIRES_SNAPSHOT_REF) != 0
                ? best->fence_time_us
                : best->source_time_us;
        const std::uint32_t fence_tick =
            (best->state & BODY_REQUIRES_SNAPSHOT_REF) != 0
                ? best->fence_tick
                : best->source_tick;
        if (fence_time > render_time_us ||
            best->source_time_us > render_time_us ||
            !tick_reached(now_tick, fence_tick) ||
            !tick_reached(now_tick, best->source_tick)) {
            increment_saturated(state.status.future_time_stalls);
            return CG_EVENT_RUNTIME_NOT_READY;
        }
        best->state |= BODY_PRESENTED;
        audit_present(state, 3u, best->journal_serial,
                      best->semantic_hash, fence_tick, fence_time);
        if ((best->state & BODY_REQUIRES_SNAPSHOT_REF) != 0)
            increment_saturated(state.status.legacy_entity_presentations);
        else
            increment_saturated(state.status.legacy_action_presentations);
        ++advanced;
    }
    return CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1 combine_advance_result(
    cg_event_runtime_result_v1 current,
    cg_event_runtime_result_v1 candidate)
{
    if (candidate == CG_EVENT_RUNTIME_DEGRADED ||
        candidate == CG_EVENT_RUNTIME_CAPACITY)
        return candidate;
    if (candidate == CG_EVENT_RUNTIME_NOT_READY &&
        current == CG_EVENT_RUNTIME_OK)
        return candidate;
    return current;
}

} // namespace

cg_event_runtime_result_v1
CG_EventRuntimeResetLegacy(std::uint32_t stream_epoch)
{
    const auto resets = runtime.status.legacy_resets;
    for (auto &body : runtime.legacy_bodies)
        body = {};
    clear_legacy_references(runtime);
    runtime.status.legacy_body_count = 0;
    runtime.legacy_initialized = false;
    runtime.status.legacy_epoch = 0;
    runtime.status.legacy_resets = resets;
    increment_saturated(runtime.status.legacy_resets);
    if (!stream_epoch)
        return CG_EVENT_RUNTIME_OK;
    runtime.legacy_initialized = true;
    runtime.status.legacy_epoch = stream_epoch;
    return CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1
CG_EventRuntimeResetAuthority(std::uint32_t stream_epoch,
                              std::uint32_t first_sequence)
{
    if ((stream_epoch == 0) != (first_sequence == 0))
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    /* The private authority carrier shares this epoch boundary. Retaining a
     * receipt across native epoch cancellation could pair it with recycled
     * command identity, so the local evidence cache is always scrubbed. */
    CG_LocalInteractionReset();
    const auto resets = runtime.status.authority_resets;
    for (auto &entry : runtime.authority)
        entry = {};
    for (auto &entry : runtime.prediction_tombstones)
        entry = {};
    clear_authority_references(runtime);
    runtime.status.authority_count = 0;
    runtime.status.prediction_tombstone_count = 0;
    runtime.status.prediction_retired_through = {};
    runtime.has_prediction_retired_cursor = false;
    runtime.authority_initialized = false;
    runtime.status.authority_epoch = 0;
    runtime.status.next_authority_sequence = 0;
    runtime.status.receipt = {};
    runtime.status.authority_degraded = 0;
    runtime.status.authority_requires_resync = 0;
    if (!stream_epoch) {
        runtime.journal = {};
        for (auto &slot : runtime.journal_slots)
            slot = {};
        runtime.status.authority_resets = resets;
        increment_saturated(runtime.status.authority_resets);
        return CG_EVENT_RUNTIME_OK;
    }
    runtime.authority_initialized = Worr_EventJournalInitV1(
        &runtime.journal, runtime.journal_slots.data(),
        static_cast<std::uint32_t>(runtime.journal_slots.size()),
        WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2, stream_epoch);
    runtime.status.authority_epoch =
        runtime.authority_initialized ? stream_epoch : 0;
    runtime.status.next_authority_sequence =
        runtime.authority_initialized ? first_sequence : 0;
    runtime.status.authority_resets = resets;
    increment_saturated(runtime.status.authority_resets);
    if (!runtime.authority_initialized) {
        mark_authority_degraded(runtime);
        return CG_EVENT_RUNTIME_DEGRADED;
    }
    runtime.journal.receipt.highest_contiguous = first_sequence - 1u;
    runtime.journal.receipt.selective_mask = 0;
    runtime.status.receipt = runtime.journal.receipt;
    return CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1
CG_EventRuntimeResetSnapshot(std::uint32_t snapshot_epoch)
{
    const auto resets = runtime.status.snapshot_resets;
    /* Snapshot-reference fences are part of the presentation proof. Losing
     * one beneath unresolved authoritative records cannot be repaired by
     * replaying only the event epoch/sequence descriptor. Require a fresh
     * event epoch instead of ever presenting those records unfenced. */
    if (runtime.authority_initialized &&
        authority_has_unpresented_records(runtime)) {
        runtime.status.authority_requires_resync = 1;
        mark_authority_degraded(runtime);
    }
    clear_snapshot_fences(runtime);
    runtime.snapshot_initialized = snapshot_epoch != 0;
    runtime.status.snapshot_epoch = snapshot_epoch;
    runtime.has_last_snapshot = false;
    runtime.last_snapshot_id = {};
    runtime.last_snapshot_time_us = 0;
    runtime.last_snapshot_event_hash = 0;
    runtime.status.snapshot_resets = resets;
    increment_saturated(runtime.status.snapshot_resets);
    return CG_EVENT_RUNTIME_OK;
}

void CG_EventRuntimeSetAuditEnabled(bool enabled)
{
    if ((runtime.status.audit_enabled != 0) == enabled)
        return;

    /*
     * A toggle starts a clean legacy comparison window. The exact snapshot
     * receipt and chronology remain intact; only optional legacy body/join
     * diagnostics are discarded.
     */
    clear_legacy_references(runtime);
    for (auto &entry : runtime.legacy_bodies)
        entry = {};
    runtime.status.legacy_body_count = 0;
    runtime.status.audit_enabled = enabled ? 1u : 0u;
}

bool CG_EventRuntimeAuditEnabled()
{
    return runtime.status.audit_enabled != 0;
}

void CG_EventRuntimeSynchronizeLocalInteractionHealth()
{
    latch_local_interaction_resync();
}

cg_event_runtime_result_v1 CG_EventRuntimeSubmitAuthoritativeBatch(
    const worr_event_record_v1 *records, std::uint32_t count)
{
    if (!records || !count ||
        count > WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2)
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    if (!runtime.authority_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
    latch_local_interaction_resync();
    if (runtime.status.authority_requires_resync != 0)
        return CG_EVENT_RUNTIME_NOT_READY;
    if (transaction_active)
        return CG_EVENT_RUNTIME_REENTRANT;

    transaction_active = true;
    copy_state(staging, runtime);
    increment_saturated(staging.status.authoritative_batches);
    cg_event_runtime_result_v1 aggregate = CG_EVENT_RUNTIME_OK;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto result = insert_authority(staging, records[index]);
        const bool accepted_degraded =
            result == CG_EVENT_RUNTIME_DEGRADED &&
            find_authority(staging, records[index].event_id) != nullptr;
        if (result == CG_EVENT_RUNTIME_CONFLICT ||
            result == CG_EVENT_RUNTIME_CAPACITY ||
            result == CG_EVENT_RUNTIME_INVALID_RECORD ||
            result == CG_EVENT_RUNTIME_WRONG_EPOCH ||
            (result == CG_EVENT_RUNTIME_DEGRADED &&
             !accepted_degraded)) {
            transaction_active = false;
            if (result == CG_EVENT_RUNTIME_CONFLICT) {
                increment_saturated(runtime.status.authoritative_conflicts);
                mark_authority_degraded(runtime);
            } else if (result == CG_EVENT_RUNTIME_CAPACITY) {
                increment_saturated(
                    runtime.status.authoritative_capacity_failures);
                mark_authority_degraded(runtime);
            } else if (result == CG_EVENT_RUNTIME_DEGRADED) {
                if (staging.status.resident_order_exhaustions !=
                    runtime.status.resident_order_exhaustions) {
                    increment_saturated(
                        runtime.status.resident_order_exhaustions);
                }
                mark_authority_degraded(runtime);
            }
            return result;
        }
        if (accepted_degraded)
            aggregate = CG_EVENT_RUNTIME_DEGRADED;
        else if (aggregate != CG_EVENT_RUNTIME_DEGRADED &&
                 result_rank(result) > result_rank(aggregate))
            aggregate = result;
    }
    commit_staging();
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto &record = records[index];
        if (!is_local_interaction_authority_receipt(record))
            continue;
        worr_local_interaction_authority_receipt_v1 receipt{};
        std::memcpy(&receipt, record.payload, sizeof(receipt));
        if (!local_interaction_receipt_accepted(
                CG_LocalInteractionSubmitAuthorityReceipt(&receipt))) {
            runtime.status.authority_requires_resync = 1;
            mark_authority_degraded(runtime);
            aggregate = CG_EVENT_RUNTIME_DEGRADED;
        }
    }
    if (aggregate == CG_EVENT_RUNTIME_DEGRADED)
        mark_authority_degraded(runtime);
    runtime.status.receipt = runtime.journal.receipt;
    transaction_active = false;
    return aggregate;
}

cg_event_runtime_result_v1 CG_EventRuntimeSubmitPredictedBatch(
    const worr_event_record_v1 *records, std::uint32_t count)
{
    if (!records || !count ||
        count > WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2)
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    if (!runtime.authority_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
    latch_local_interaction_resync();
    if (runtime.status.authority_requires_resync != 0)
        return CG_EVENT_RUNTIME_NOT_READY;
    if (transaction_active)
        return CG_EVENT_RUNTIME_REENTRANT;

    transaction_active = true;
    copy_state(staging, runtime);
    increment_saturated(staging.status.predicted_batches);
    cg_event_runtime_result_v1 aggregate = CG_EVENT_RUNTIME_OK;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto result = insert_prediction(staging, records[index]);
        if (result == CG_EVENT_RUNTIME_CONFLICT ||
            result == CG_EVENT_RUNTIME_CAPACITY ||
            result == CG_EVENT_RUNTIME_INVALID_RECORD ||
            result == CG_EVENT_RUNTIME_WRONG_EPOCH ||
            result == CG_EVENT_RUNTIME_TERMINAL ||
            result == CG_EVENT_RUNTIME_DEGRADED) {
            transaction_active = false;
            if (result == CG_EVENT_RUNTIME_CONFLICT) {
                mark_authority_degraded(runtime);
            } else if (result == CG_EVENT_RUNTIME_CAPACITY) {
                increment_saturated(
                    runtime.status.predicted_capacity_failures);
                if (staging.status.prediction_tombstone_capacity_failures !=
                    runtime.status.prediction_tombstone_capacity_failures) {
                    increment_saturated(
                        runtime.status.prediction_tombstone_capacity_failures);
                }
                mark_authority_degraded(runtime);
            } else if (result == CG_EVENT_RUNTIME_DEGRADED) {
                if (staging.status.resident_order_exhaustions !=
                    runtime.status.resident_order_exhaustions) {
                    increment_saturated(
                        runtime.status.resident_order_exhaustions);
                }
                mark_authority_degraded(runtime);
            } else if (result == CG_EVENT_RUNTIME_TERMINAL &&
                       staging.status.stale_prediction_rejections !=
                           runtime.status.stale_prediction_rejections) {
                increment_saturated(
                    runtime.status.stale_prediction_rejections);
            }
            return result;
        }
        if (result_rank(result) > result_rank(aggregate))
            aggregate = result;
    }
    commit_staging();
    if (aggregate == CG_EVENT_RUNTIME_DEGRADED)
        mark_authority_degraded(runtime);
    runtime.status.receipt = runtime.journal.receipt;
    transaction_active = false;
    return aggregate;
}

cg_event_runtime_result_v1 CG_EventRuntimeCancelPrediction(
    const worr_event_prediction_key_v1 *key)
{
    if (!key || key->command_epoch == 0 || key->command_sequence == 0 ||
        key->lane < WORR_EVENT_PREDICTION_LANE_GAMEPLAY ||
        key->lane > WORR_EVENT_PREDICTION_LANE_EFFECT) {
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    }
    if (!runtime.authority_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
    if (runtime.status.authority_requires_resync != 0)
        return CG_EVENT_RUNTIME_NOT_READY;
    for (std::uint32_t index = 0; index < runtime.journal.capacity;
         ++index) {
        auto &slot = runtime.journal_slots[index];
        if (slot.state == 0 ||
            (slot.state & WORR_EVENT_SLOT_PREDICTED) == 0 ||
            (slot.state & WORR_EVENT_SLOT_RECEIVED) != 0 ||
            !prediction_key_equal(slot.record.prediction_key, *key)) {
            continue;
        }
        const worr_event_slot_ref_v1 ref{index, slot.generation};
        const auto result = Worr_EventJournalCancelV1(&runtime.journal, ref);
        if (result == WORR_EVENT_JOURNAL_INSERTED) {
            if (auto *tombstone = find_prediction_tombstone(runtime, *key))
                tombstone->terminal = true;
            else
                mark_authority_degraded(runtime);
            increment_saturated(runtime.status.prediction_cancellations);
            return CG_EVENT_RUNTIME_OK;
        }
        const auto mapped = map_journal_result(result);
        if (mapped == CG_EVENT_RUNTIME_CONFLICT ||
            mapped == CG_EVENT_RUNTIME_CAPACITY ||
            mapped == CG_EVENT_RUNTIME_DEGRADED) {
            mark_authority_degraded(runtime);
        }
        return mapped;
    }
    if (runtime.has_prediction_retired_cursor &&
        prediction_key_at_or_before_cursor(
            *key, runtime.status.prediction_retired_through)) {
        return CG_EVENT_RUNTIME_TERMINAL;
    }
    return CG_EVENT_RUNTIME_NOT_FOUND;
}

cg_event_runtime_result_v1 CG_EventRuntimeRetirePredictionsThrough(
    worr_command_cursor_v1 consumed_cursor)
{
    if (consumed_cursor.epoch == 0)
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    if (!runtime.authority_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
    if (runtime.status.authority_requires_resync != 0)
        return CG_EVENT_RUNTIME_NOT_READY;
    if (transaction_active)
        return CG_EVENT_RUNTIME_REENTRANT;

    increment_saturated(runtime.status.prediction_retire_calls);
    bool duplicate = false;
    if (runtime.has_prediction_retired_cursor) {
        if (cursor_equal(consumed_cursor,
                         runtime.status.prediction_retired_through)) {
            duplicate = true;
        }
        if (!duplicate &&
            cursor_before(consumed_cursor,
                          runtime.status.prediction_retired_through)) {
            increment_saturated(runtime.status.prediction_retire_regressions);
            mark_authority_degraded(runtime);
            return CG_EVENT_RUNTIME_CONFLICT;
        }
    }

    if (!duplicate) {
        runtime.status.prediction_retired_through = consumed_cursor;
        runtime.has_prediction_retired_cursor = true;
    }
    for (auto &entry : runtime.prediction_tombstones) {
        if (!entry.occupied ||
            !prediction_key_at_or_before_cursor(
                entry.record.prediction_key, consumed_cursor) ||
            (!entry.reconciled &&
             (!entry.terminal || entry.presented))) {
            continue;
        }
        entry = {};
        decrement_if_nonzero(runtime.status.prediction_tombstone_count);
        increment_saturated(runtime.status.prediction_tombstones_retired);
    }
    return duplicate ? CG_EVENT_RUNTIME_DUPLICATE : CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1 CG_EventRuntimeObserveSnapshot(
    const worr_snapshot_v2 *snapshot,
    const worr_snapshot_event_ref_v2 *event_refs,
    std::uint32_t event_ref_count)
{
    const bool observe_authority = runtime.authority_initialized &&
                                   runtime.status.authority_requires_resync == 0;
    const bool observe_legacy = CG_EventRuntimeAuditEnabled();
    std::uint64_t computed_event_hash = 0;
    if (!snapshot ||
        (event_ref_count != 0 && !event_refs) ||
        snapshot->struct_size != sizeof(*snapshot) ||
        snapshot->schema_version != WORR_SNAPSHOT_ABI_VERSION ||
        snapshot->event_range.count != event_ref_count ||
        !Worr_SnapshotIdValidV2(snapshot->snapshot_id, false) ||
        !Worr_SnapshotEventRefsHashV2(
            event_refs, event_ref_count, &computed_event_hash) ||
        snapshot->event_hash != computed_event_hash) {
        increment_saturated(runtime.status.snapshot_rejections);
        mark_degraded(runtime);
        if (observe_authority)
            runtime.status.authority_degraded = 1;
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    }
    if (!runtime.snapshot_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
    if (snapshot->snapshot_id.epoch != runtime.status.snapshot_epoch)
        return CG_EVENT_RUNTIME_WRONG_EPOCH;
    /*
     * A snapshot event range has one provenance for the complete range.
     * Legacy-inferred references are always correctness-validated even when
     * comparison audit is disabled. Authority references, however, cannot be
     * acknowledged without the matching live authority lifecycle.
     */
    if (event_ref_count != 0 &&
        event_refs[0].provenance ==
            WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY &&
        !observe_authority) {
        return CG_EVENT_RUNTIME_NOT_READY;
    }
    if (runtime.has_last_snapshot) {
        if (snapshot_id_equal(snapshot->snapshot_id,
                              runtime.last_snapshot_id)) {
            if (snapshot->server_time_us == runtime.last_snapshot_time_us &&
                snapshot->event_hash == runtime.last_snapshot_event_hash) {
                increment_saturated(runtime.status.snapshot_duplicates);
                return CG_EVENT_RUNTIME_DUPLICATE;
            }
            increment_saturated(runtime.status.snapshot_rejections);
            mark_degraded(runtime);
            if (observe_authority)
                runtime.status.authority_degraded = 1;
            return CG_EVENT_RUNTIME_CONFLICT;
        }
        if (snapshot->snapshot_id.sequence <
                runtime.last_snapshot_id.sequence ||
            snapshot->server_time_us < runtime.last_snapshot_time_us) {
            increment_saturated(runtime.status.snapshot_rejections);
            mark_degraded(runtime);
            if (observe_authority)
                runtime.status.authority_degraded = 1;
            return CG_EVENT_RUNTIME_DEGRADED;
        }
    }
    if (transaction_active)
        return CG_EVENT_RUNTIME_REENTRANT;

    transaction_active = true;
    copy_state(staging, runtime);
    cg_event_runtime_result_v1 aggregate = CG_EVENT_RUNTIME_EMPTY;
    bool authority_attachment_degraded = false;
    for (std::uint32_t index = 0; index < event_ref_count; ++index) {
        const bool authority_reference =
            event_refs[index].provenance ==
            WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
        if (!authority_reference && !observe_legacy) {
            /*
             * Structural/order/hash validation above is the correctness
             * fence. Materialized legacy joins exist only for the optional
             * body-comparison audit, so audit-off operation records the
             * observation without consuming fixed reference-table capacity.
             */
            increment_saturated(staging.status.references_observed);
            aggregate = CG_EVENT_RUNTIME_OK;
            continue;
        }
        const auto result = insert_reference(
            staging, *snapshot, event_refs[index]);
        if (result == CG_EVENT_RUNTIME_CONFLICT ||
            result == CG_EVENT_RUNTIME_CAPACITY) {
            transaction_active = false;
            increment_saturated(runtime.status.snapshot_rejections);
            if (result == CG_EVENT_RUNTIME_CONFLICT)
                increment_saturated(runtime.status.reference_conflicts);
            else {
                increment_saturated(
                    runtime.status.reference_capacity_failures);
            }
            mark_degraded(runtime);
            if (authority_reference)
                runtime.status.authority_degraded = 1;
            return result;
        }
        if (result == CG_EVENT_RUNTIME_DEGRADED) {
            aggregate = result;
            authority_attachment_degraded |= authority_reference;
        } else if (aggregate == CG_EVENT_RUNTIME_EMPTY) {
            aggregate = result;
        }
    }
    staging.has_last_snapshot = true;
    staging.last_snapshot_id = snapshot->snapshot_id;
    staging.last_snapshot_time_us = snapshot->server_time_us;
    staging.last_snapshot_event_hash = snapshot->event_hash;
    increment_saturated(staging.status.snapshots_observed);
    commit_staging();
    if (authority_attachment_degraded)
        mark_authority_degraded(runtime);
    transaction_active = false;
    if (snapshot->consumed_command.cursor.epoch != 0) {
        const auto retire_result =
            CG_EventRuntimeRetirePredictionsThrough(
                snapshot->consumed_command.cursor);
        if (retire_result == CG_EVENT_RUNTIME_CONFLICT ||
            retire_result == CG_EVENT_RUNTIME_DEGRADED) {
            return CG_EVENT_RUNTIME_DEGRADED;
        }
    }
    return aggregate;
}

cg_event_runtime_result_v1 CG_EventRuntimeObserveLegacyEntry(
    const cg_canonical_event_presentation_entry_v1 *entry)
{
    if (!CG_EventRuntimeAuditEnabled())
        return CG_EVENT_RUNTIME_EMPTY;
    if (!entry ||
        entry->struct_size != sizeof(*entry) ||
        entry->schema_version !=
            CG_CANONICAL_EVENT_PRESENTATION_VERSION ||
        entry->journal_serial == 0 ||
        entry->carrier_kind <
            WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2 ||
        entry->carrier_kind >
            WORR_CGAME_EVENT_CARRIER_KIND_COUNT_V2) {
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    }
    if (!runtime.legacy_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
    if (entry->stream_epoch != runtime.status.legacy_epoch)
        return CG_EVENT_RUNTIME_WRONG_EPOCH;
    if (transaction_active)
        return CG_EVENT_RUNTIME_REENTRANT;

    std::uint64_t semantic_hash = 0;
    if (!Worr_EventRecordSemanticHashV1(
            &entry->record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
            &semantic_hash) || semantic_hash != entry->semantic_hash) {
        mark_degraded(runtime);
        return CG_EVENT_RUNTIME_INVALID_RECORD;
    }
    for (const auto &existing : runtime.legacy_bodies) {
        if (!existing.occupied ||
            existing.journal_serial != entry->journal_serial) {
            continue;
        }
        if (existing.semantic_hash == entry->semantic_hash &&
            existing.source_tick == entry->record.source_tick &&
            existing.source_ordinal == entry->record.source_ordinal &&
            existing.carrier_kind == entry->carrier_kind) {
            return CG_EVENT_RUNTIME_DUPLICATE;
        }
        mark_degraded(runtime);
        return CG_EVENT_RUNTIME_CONFLICT;
    }

    const std::uint64_t overruns_before =
        runtime.status.legacy_body_overruns;
    legacy_body_entry_t *body = allocate_legacy_body(runtime);
    if (!body) {
        increment_saturated(
            runtime.status.legacy_body_capacity_failures);
        mark_degraded(runtime);
        return CG_EVENT_RUNTIME_CAPACITY;
    }
    *body = {};
    body->journal_serial = entry->journal_serial;
    body->semantic_hash = entry->semantic_hash;
    body->source_time_us = entry->record.source_time_us;
    body->source_tick = entry->record.source_tick;
    body->source_ordinal = entry->record.source_ordinal;
    body->carrier_kind = entry->carrier_kind;
    body->resident_order = next_order(runtime);
    if (!body->resident_order) {
        *body = {};
        decrement_if_nonzero(runtime.status.legacy_body_count);
        return CG_EVENT_RUNTIME_DEGRADED;
    }
    body->occupied = true;
    if (entry->carrier_kind ==
        WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2) {
        body->state |= BODY_REQUIRES_SNAPSHOT_REF;
        /* Snapshot carrier_ordinal is dense within the event-ref range;
         * source_ordinal is the sparse entity scan position. Derive the same
         * dense event rank while retaining source_ordinal in the hash. */
        for (const auto &prior : runtime.legacy_bodies) {
            if (prior.occupied && &prior != body &&
                (prior.state & BODY_REQUIRES_SNAPSHOT_REF) != 0 &&
                prior.source_tick == body->source_tick) {
                ++body->dense_event_ordinal;
            }
        }
        if (auto *reference = find_legacy_reference(
                runtime, body->source_tick,
                body->dense_event_ordinal)) {
            if (!attach_legacy_reference(
                    runtime, *body, *reference, true)) {
                increment_saturated(runtime.status.legacy_bodies_observed);
                return CG_EVENT_RUNTIME_DEGRADED;
            }
        }
    }
    increment_saturated(runtime.status.legacy_bodies_observed);
    return runtime.status.legacy_body_overruns != overruns_before
               ? CG_EVENT_RUNTIME_DEGRADED
               : CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1 CG_EventRuntimeAdvanceAudit(
    std::uint64_t render_time_us, std::uint32_t now_tick,
    std::uint32_t max_presentations, std::uint32_t *advanced_out)
{
    if (!advanced_out || !max_presentations)
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    const bool observe_legacy = CG_EventRuntimeAuditEnabled();
    if (!observe_legacy && !runtime.authority_initialized) {
        *advanced_out = 0;
        return CG_EVENT_RUNTIME_EMPTY;
    }
    if (!runtime.legacy_initialized && !runtime.authority_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
    if (transaction_active)
        return CG_EVENT_RUNTIME_REENTRANT;

    *advanced_out = 0;
    latch_local_interaction_resync();
    increment_saturated(runtime.status.advance_calls);
    runtime.status.last_render_time_us = render_time_us;
    runtime.status.last_now_tick = now_tick;
    if (runtime.authority_initialized &&
        runtime.status.authority_requires_resync == 0) {
        std::array<std::uint8_t, CG_EVENT_RUNTIME_JOURNAL_CAPACITY>
            was_expired{};
        for (std::uint32_t index = 0; index < runtime.journal.capacity;
             ++index) {
            was_expired[index] =
                (runtime.journal_slots[index].state &
                 WORR_EVENT_SLOT_EXPIRED) != 0;
        }
        (void)Worr_EventJournalExpireV1(&runtime.journal, now_tick);
        for (std::uint32_t index = 0; index < runtime.journal.capacity;
             ++index) {
            const auto &slot = runtime.journal_slots[index];
            if (was_expired[index] ||
                (slot.state & WORR_EVENT_SLOT_EXPIRED) == 0) {
                continue;
            }
            if ((slot.state & WORR_EVENT_SLOT_RECEIVED) != 0) {
                increment_saturated(
                    runtime.status.authoritative_expirations);
            } else {
                if (auto *tombstone = find_prediction_tombstone(
                        runtime, slot.record.prediction_key)) {
                    tombstone->terminal = true;
                } else {
                    mark_authority_degraded(runtime);
                }
                increment_saturated(
                    runtime.status.prediction_expirations);
            }
        }
    }

    cg_event_runtime_result_v1 result =
        runtime.status.authority_requires_resync != 0
            ? CG_EVENT_RUNTIME_NOT_READY
            : CG_EVENT_RUNTIME_OK;
    if (runtime.authority_initialized &&
        runtime.status.authority_requires_resync == 0) {
        const auto prediction_result = present_predictions(
            runtime, render_time_us, now_tick, max_presentations,
            *advanced_out);
        result = combine_advance_result(result, prediction_result);
        if (prediction_result == CG_EVENT_RUNTIME_DEGRADED ||
            prediction_result == CG_EVENT_RUNTIME_CAPACITY) {
            mark_authority_degraded(runtime);
        }
        if (result != CG_EVENT_RUNTIME_DEGRADED &&
            result != CG_EVENT_RUNTIME_CAPACITY) {
            const auto authority_result = present_authority(
                runtime, render_time_us, now_tick, max_presentations,
                *advanced_out);
            result = combine_advance_result(result, authority_result);
            if (authority_result == CG_EVENT_RUNTIME_DEGRADED ||
                authority_result == CG_EVENT_RUNTIME_CAPACITY) {
                mark_authority_degraded(runtime);
            }
        }
    }
    if (observe_legacy && runtime.legacy_initialized &&
        result != CG_EVENT_RUNTIME_DEGRADED &&
        result != CG_EVENT_RUNTIME_CAPACITY) {
        result = combine_advance_result(
            result, present_legacy(runtime, render_time_us, now_tick,
                                   max_presentations, *advanced_out));
    }
    if (runtime.authority_initialized)
        runtime.status.receipt = runtime.journal.receipt;
    return result;
}

bool CG_EventRuntimeGetStatus(cg_event_runtime_status_v1 *status_out)
{
    if (!status_out)
        return false;
    latch_local_interaction_resync();
    auto status = runtime.status;
    status.struct_size = sizeof(status);
    status.schema_version = CG_EVENT_RUNTIME_VERSION;
    if (runtime.authority_initialized)
        status.receipt = runtime.journal.receipt;
    *status_out = status;
    return true;
}

bool CG_EventRuntimeSnapshotFenceHealthy(std::uint32_t snapshot_epoch)
{
    cg_event_runtime_status_v1 status{};

    return snapshot_epoch != 0 &&
           CG_EventRuntimeGetStatus(&status) &&
           status.snapshot_epoch == snapshot_epoch &&
           status.authority_requires_resync == 0 &&
           status.authority_degraded == 0;
}

namespace {

worr_cgame_event_runtime_result_v1 export_reset_authority(
    std::uint32_t stream_epoch, std::uint32_t first_sequence)
{
    return static_cast<worr_cgame_event_runtime_result_v1>(
        CG_EventRuntimeResetAuthority(stream_epoch, first_sequence));
}

worr_cgame_event_runtime_result_v1 export_submit_authority(
    const worr_event_record_v1 *records, std::uint32_t count)
{
    return static_cast<worr_cgame_event_runtime_result_v1>(
        CG_EventRuntimeSubmitAuthoritativeBatch(records, count));
}

bool export_get_status(worr_cgame_event_runtime_status_v1 *status_out)
{
    if (!status_out)
        return false;
    cg_event_runtime_status_v1 private_status{};
    if (!CG_EventRuntimeGetStatus(&private_status))
        return false;

    worr_cgame_event_runtime_status_v1 status{};
    status.struct_size = sizeof(status);
    status.api_version = WORR_CGAME_EVENT_RUNTIME_API_VERSION;
    status.authority_epoch = private_status.authority_epoch;
    status.next_presentation_sequence =
        private_status.next_authority_sequence;
    status.authority_count = private_status.authority_count;
    if (private_status.authority_epoch != 0)
        status.state_flags |= WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE;
    if (private_status.authority_degraded != 0)
        status.state_flags |= WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED;
    if (private_status.audit_enabled != 0) {
        status.state_flags |=
            WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED;
    }
    if (private_status.authority_requires_resync != 0) {
        status.state_flags |=
            WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC;
    }
    status.receipt = private_status.receipt;
    *status_out = status;
    return true;
}

const worr_cgame_event_runtime_export_v1 event_runtime_api{
    sizeof(event_runtime_api),
    WORR_CGAME_EVENT_RUNTIME_API_VERSION,
    export_reset_authority,
    export_submit_authority,
    export_get_status,
};

static_assert(WORR_CGAME_EVENT_RUNTIME_MAX_BATCH ==
              WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2);

} // namespace

const worr_cgame_event_runtime_export_v1 *CG_GetEventRuntimeAPI()
{
    return &event_runtime_api;
}
