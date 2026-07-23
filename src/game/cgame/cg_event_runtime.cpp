/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_event_runtime.hpp"
#include "cg_canonical_snapshot_timeline.hpp"
#include "cg_local_interaction.hpp"

#include "common/net/predicted_presentation.h"

#include <array>
#include <cstring>

namespace {

static_assert(CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY ==
                  CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY,
              "event source-proof and presenter snapshot retention must stay aligned");
static_assert(CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY >= 64u,
              "event source proof must cover the one-second retention bound");
static_assert(CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY >= 64u,
              "presenter snapshots must cover the one-second retention bound");

cg_local_action_shadow_report_callback_v1 local_action_shadow_report_callback;
cg_event_runtime_can_present_callback_v1 event_can_present_callback;
cg_event_runtime_present_callback_v1 event_present_callback;

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
    AUTHORITY_PRIVATE_RECONCILED = 1u << 5,
    AUTHORITY_HAS_EXPIRY_SNAPSHOT_REF = 1u << 6,
    AUTHORITY_HAS_EXPIRY_CROSSING_BOUND = 1u << 7,
};

struct authority_entry_t {
    worr_event_record_v1 record;
    worr_event_slot_ref_v1 slot;
    std::uint64_t semantic_hash;
    std::uint64_t resident_order;
    std::uint64_t fence_time_us;
    worr_snapshot_id_v2 fence_snapshot_id;
    worr_snapshot_id_v2 expiry_snapshot_id;
    worr_snapshot_id_v2 expiry_crossing_snapshot_id;
    std::uint32_t fence_tick;
    std::uint32_t expiry_fence_tick;
    std::uint32_t expiry_crossing_tick;
    std::uint64_t expiry_fence_time_us;
    std::uint64_t expiry_crossing_time_us;
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

struct snapshot_fence_entry_t {
    worr_snapshot_id_v2 snapshot_id;
    std::uint64_t snapshot_time_us;
    std::uint32_t snapshot_tick;
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
    std::array<snapshot_fence_entry_t,
               CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY> snapshot_fences;
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
bool presentation_callback_active;

bool runtime_mutation_blocked()
{
    return transaction_active || presentation_callback_active;
}

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
        /* AUTHORITY_SKIP is only a terminalization request.  Its ordered
         * counter/cursor effects are not settled until the drain marks the
         * entry presented, so a read-only checkpoint must not baseline it. */
        if (entry.occupied &&
            (entry.state & AUTHORITY_PRESENTED) == 0) {
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

bool is_local_action_shadow_authority_receipt(
    const worr_event_record_v1 &record)
{
    return record.event_type == WORR_EVENT_TYPE_AUTHORITY_RECEIPT &&
           record.payload_kind ==
               WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1 &&
           record.payload_size ==
               sizeof(worr_local_action_shadow_authority_receipt_v1);
}

bool is_private_authority_receipt(const worr_event_record_v1 &record)
{
    return is_local_interaction_authority_receipt(record) ||
           is_local_action_shadow_authority_receipt(record);
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

bool local_action_shadow_receipt_accepted(
    cg_local_action_shadow_receipt_result_v1 result)
{
    switch (result) {
    case cg_local_action_shadow_receipt_result_v1::accepted_unmatched:
    case cg_local_action_shadow_receipt_result_v1::duplicate:
    case cg_local_action_shadow_receipt_result_v1::command_matched:
        return true;
    case cg_local_action_shadow_receipt_result_v1::command_mismatch:
    case cg_local_action_shadow_receipt_result_v1::invalid:
    case cg_local_action_shadow_receipt_result_v1::conflict:
    case cg_local_action_shadow_receipt_result_v1::capacity:
    case cg_local_action_shadow_receipt_result_v1::requires_resync:
        return false;
    }
    return false;
}

void latch_local_interaction_resync()
{
    if (!runtime.authority_initialized ||
        runtime.status.authority_requires_resync != 0 ||
        (!CG_LocalInteractionRequiresResync() &&
         !CG_LocalActionShadowRequiresResync())) {
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

enum class fenced_authority_expiry_v1 {
    active,
    crossed_without_render_bound,
    terminal,
};

fenced_authority_expiry_v1 evaluate_fenced_authority_expiry(
    const runtime_state_t &state, const authority_entry_t &authority,
    std::uint64_t render_time_us);

authority_entry_t *find_authority_for_slot(
    runtime_state_t &state, std::uint32_t slot_index,
    const worr_event_journal_slot_v1 &slot)
{
    if ((slot.record.flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0)
        return nullptr;
    auto *authority = find_authority(state, slot.record.event_id);
    if (!authority || authority->slot.index != slot_index ||
        authority->slot.generation != slot.generation) {
        return nullptr;
    }
    return authority;
}

void expire_runtime_journal(runtime_state_t &state,
                            std::uint64_t render_time_us,
                            std::uint32_t now_tick)
{
    for (std::uint32_t index = 0; index < state.journal.capacity;
         ++index) {
        auto &slot = state.journal_slots[index];
        if (slot.state == 0 ||
            (slot.state & (WORR_EVENT_SLOT_EXPIRED |
                           WORR_EVENT_SLOT_CANCELED)) != 0 ||
            slot.record.delivery_class >
                WORR_EVENT_DELIVERY_TRANSIENT) {
            continue;
        }

        bool terminal = false;
        if ((slot.record.flags &
             WORR_EVENT_FLAG_SNAPSHOT_FENCED) != 0) {
            auto *authority = find_authority_for_slot(
                state, index, slot);
            if (!authority) {
                continue;
            }
            terminal = evaluate_fenced_authority_expiry(
                           state, *authority, render_time_us) ==
                       fenced_authority_expiry_v1::terminal;
        } else {
            terminal = tick_reached(now_tick, slot.record.expiry_tick);
        }
        if (terminal)
            slot.state |= WORR_EVENT_SLOT_EXPIRED;
    }
}

bool authority_slot_needs_presentation(
    const runtime_state_t &state, const authority_entry_t &authority,
    const worr_event_journal_slot_v1 &slot,
    std::uint64_t render_time_us, std::uint32_t now_tick)
{
    if ((authority.record.flags &
         WORR_EVENT_FLAG_SNAPSHOT_FENCED) == 0 ||
        authority.record.delivery_class >
            WORR_EVENT_DELIVERY_TRANSIENT) {
        return Worr_EventJournalNeedsPresentationV1(
            &state.journal, authority.slot, now_tick);
    }

    if ((slot.state & (WORR_EVENT_SLOT_PRESENTED |
                       WORR_EVENT_SLOT_EXPIRED |
                       WORR_EVENT_SLOT_CANCELED)) != 0 ||
        (slot.state & WORR_EVENT_SLOT_RECEIVED) == 0 ||
        evaluate_fenced_authority_expiry(
            state, authority, render_time_us) !=
            fenced_authority_expiry_v1::active) {
        return false;
    }
    return true;
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

void mark_authority_fence_mismatch(runtime_state_t &state,
                                   authority_entry_t &authority,
                                   bool require_resync)
{
    if ((authority.state & AUTHORITY_REF_MISMATCH) != 0)
        return;
    authority.state |= AUTHORITY_REF_MISMATCH;
    increment_saturated(state.status.reference_conflicts);
    if (require_resync && state.authority_initialized)
        state.status.authority_requires_resync = 1;
    mark_degraded(state);
}

bool attach_authority_fence(runtime_state_t &state,
                            authority_entry_t &authority,
                            worr_snapshot_id_v2 snapshot_id,
                            std::uint32_t snapshot_tick,
                            std::uint64_t snapshot_time_us,
                            bool require_resync)
{
    if ((authority.state & AUTHORITY_HAS_SNAPSHOT_REF) != 0) {
        if (snapshot_id_equal(authority.fence_snapshot_id, snapshot_id) &&
            authority.fence_tick == snapshot_tick &&
            authority.fence_time_us == snapshot_time_us) {
            return true;
        }
        mark_authority_fence_mismatch(
            state, authority, require_resync);
        return false;
    }
    authority.state |= AUTHORITY_HAS_SNAPSHOT_REF;
    authority.fence_snapshot_id = snapshot_id;
    authority.fence_tick = snapshot_tick;
    authority.fence_time_us = snapshot_time_us;
    increment_saturated(state.status.authority_ref_body_joins);
    return true;
}

snapshot_fence_entry_t *find_snapshot_fence(
    runtime_state_t &state, worr_snapshot_id_v2 snapshot_id)
{
    for (auto &entry : state.snapshot_fences) {
        if (entry.occupied &&
            snapshot_id_equal(entry.snapshot_id, snapshot_id)) {
            return &entry;
        }
    }
    return nullptr;
}

const snapshot_fence_entry_t *find_snapshot_fence(
    const runtime_state_t &state, worr_snapshot_id_v2 snapshot_id)
{
    for (const auto &entry : state.snapshot_fences) {
        if (entry.occupied &&
            snapshot_id_equal(entry.snapshot_id, snapshot_id)) {
            return &entry;
        }
    }
    return nullptr;
}

void retain_snapshot_fence(runtime_state_t &state,
                           const worr_snapshot_v2 &snapshot)
{
    snapshot_fence_entry_t *selected = nullptr;
    for (auto &entry : state.snapshot_fences) {
        if (!entry.occupied) {
            selected = &entry;
            break;
        }
        if (!selected || entry.snapshot_id.sequence <
                             selected->snapshot_id.sequence) {
            selected = &entry;
        }
    }
    if (!selected)
        return;
    *selected = {};
    selected->snapshot_id = snapshot.snapshot_id;
    selected->snapshot_tick = snapshot.server_tick;
    selected->snapshot_time_us = snapshot.server_time_us;
    selected->occupied = true;
}

bool derive_declared_snapshot_expiry(
    const authority_entry_t &authority,
    worr_snapshot_id_v2 &snapshot_id)
{
    if ((authority.record.flags &
         WORR_EVENT_FLAG_SNAPSHOT_FENCED) == 0 ||
        authority.record.delivery_class >
            WORR_EVENT_DELIVERY_TRANSIENT ||
        (authority.state & AUTHORITY_HAS_SNAPSHOT_REF) == 0) {
        return false;
    }

    /* expiry_tick is an absolute per-client wire-snapshot number, just like
     * source_tick for an explicit snapshot fence. Translate that identity
     * directly; adding its numeric lifetime to fence_tick would mix the wire
     * and simulation clocks. A modular wire wrap cannot occur inside one
     * monotonic snapshot-ID epoch, so it remains future until the mandatory
     * epoch reset either presents or invalidates the unresolved authority. */
    if (authority.record.expiry_tick < authority.record.source_tick ||
        authority.record.expiry_tick == UINT32_MAX) {
        return false;
    }
    snapshot_id.epoch = authority.fence_snapshot_id.epoch;
    snapshot_id.sequence = authority.record.expiry_tick + 1u;
    return Worr_SnapshotIdValidV2(snapshot_id, false) &&
           snapshot_id.sequence > authority.fence_snapshot_id.sequence;
}

void try_attach_declared_snapshot_expiry(
    runtime_state_t &state, authority_entry_t &authority)
{
    if ((authority.state & (AUTHORITY_HAS_EXPIRY_SNAPSHOT_REF |
                            AUTHORITY_HAS_EXPIRY_CROSSING_BOUND)) != 0) {
        return;
    }

    worr_snapshot_id_v2 snapshot_id{};
    if (!derive_declared_snapshot_expiry(authority, snapshot_id))
        return;
    const auto *fence = find_snapshot_fence(state, snapshot_id);
    if (fence) {
        authority.state |= AUTHORITY_HAS_EXPIRY_SNAPSHOT_REF;
        authority.expiry_snapshot_id = fence->snapshot_id;
        authority.expiry_fence_tick = fence->snapshot_tick;
        authority.expiry_fence_time_us = fence->snapshot_time_us;
        return;
    }

    /* If the exact deadline was skipped or already evicted, cache the first
     * retained snapshot beyond it. This is a conservative render bound, not a
     * fabricated deadline: presentation remains uncertain before its time and
     * terminal expiry is proven at its time. Caching prevents bounded fence
     * eviction from moving that proof forward forever under render lag. */
    const snapshot_fence_entry_t *crossing = nullptr;
    for (const auto &entry : state.snapshot_fences) {
        if (!entry.occupied || entry.snapshot_id.epoch != snapshot_id.epoch ||
            entry.snapshot_id.sequence <= snapshot_id.sequence) {
            continue;
        }
        if (!crossing || entry.snapshot_id.sequence <
                             crossing->snapshot_id.sequence) {
            crossing = &entry;
        }
    }
    if (!crossing)
        return;
    authority.state |= AUTHORITY_HAS_EXPIRY_CROSSING_BOUND;
    authority.expiry_crossing_snapshot_id = crossing->snapshot_id;
    authority.expiry_crossing_tick = crossing->snapshot_tick;
    authority.expiry_crossing_time_us = crossing->snapshot_time_us;
}

fenced_authority_expiry_v1 evaluate_exact_fenced_retention(
    const runtime_state_t &state, const authority_entry_t &authority)
{
    /* Event DATA can arrive after render has crossed a one-snapshot transient
     * deadline. Keep that present-once event eligible while the bounded
     * snapshot mirror still retains its exact source proof. Once the proof is
     * evicted, fail closed instead of extending the event indefinitely. */
    if (find_snapshot_fence(state, authority.fence_snapshot_id)) {
        return fenced_authority_expiry_v1::active;
    }

    return fenced_authority_expiry_v1::terminal;
}

fenced_authority_expiry_v1 evaluate_fenced_authority_expiry(
    const runtime_state_t &state, const authority_entry_t &authority,
    std::uint64_t render_time_us)
{
    if ((authority.state & AUTHORITY_HAS_SNAPSHOT_REF) == 0)
        return fenced_authority_expiry_v1::crossed_without_render_bound;

    if ((authority.state & AUTHORITY_HAS_EXPIRY_SNAPSHOT_REF) != 0) {
        /* Exact source and deadline identities are known. Their bounded
         * source-fence retention, rather than a potentially already-crossed
         * render sample, defines the present-once lifetime. */
        return evaluate_exact_fenced_retention(state, authority);
    }
    if ((authority.state & AUTHORITY_HAS_EXPIRY_CROSSING_BOUND) != 0) {
        return render_time_us >= authority.expiry_crossing_time_us
                   ? fenced_authority_expiry_v1::terminal
                   : fenced_authority_expiry_v1::
                         crossed_without_render_bound;
    }

    worr_snapshot_id_v2 deadline_id{};
    if (!derive_declared_snapshot_expiry(authority, deadline_id)) {
        /* The deadline belongs to the next snapshot epoch. It is still
         * provably future in this epoch; ResetSnapshot fail-closes any
         * authority that remains unresolved across that boundary. */
        return fenced_authority_expiry_v1::active;
    }

    if (find_snapshot_fence(state, deadline_id)) {
        return evaluate_exact_fenced_retention(state, authority);
    }

    if (!state.has_last_snapshot ||
        state.last_snapshot_id.epoch != deadline_id.epoch ||
        state.last_snapshot_id.sequence < deadline_id.sequence) {
        /* The exact deadline has not arrived. Wire-ID order proves that the
         * event is still before its deadline, so presentation remains safe. */
        return fenced_authority_expiry_v1::active;
    }

    /* Attachment normally caches the first retained crossing snapshot before
     * evaluation. If the invariant is ever broken, fail closed instead of
     * selecting a later moving bound here. */
    return fenced_authority_expiry_v1::crossed_without_render_bound;
}

bool derive_declared_snapshot_fence(
    const runtime_state_t &state, const worr_event_record_v1 &record,
    worr_snapshot_id_v2 &snapshot_id)
{
    if ((record.flags & WORR_EVENT_FLAG_SNAPSHOT_FENCED) == 0 ||
        !state.snapshot_initialized || state.status.snapshot_epoch == 0 ||
        record.source_tick == UINT32_MAX) {
        return false;
    }
    snapshot_id.epoch = state.status.snapshot_epoch;
    snapshot_id.sequence = record.source_tick + 1u;
    return snapshot_id.sequence != 0;
}

enum class crossed_transient_fence_v1 {
    not_terminalizable,
    waiting_for_expiry,
    terminalized,
};

crossed_transient_fence_v1 handle_crossed_transient_fence(
    runtime_state_t &state, authority_entry_t &authority,
    worr_snapshot_id_v2 source_snapshot_id)
{
    const auto &record = authority.record;
    if (record.delivery_class > WORR_EVENT_DELIVERY_TRANSIENT ||
        record.expiry_tick < record.source_tick ||
        record.expiry_tick == UINT32_MAX) {
        return crossed_transient_fence_v1::not_terminalizable;
    }

    const worr_snapshot_id_v2 expiry_snapshot_id{
        source_snapshot_id.epoch,
        record.expiry_tick + 1u,
    };
    if (!Worr_SnapshotIdValidV2(expiry_snapshot_id, false) ||
        !state.has_last_snapshot ||
        state.last_snapshot_id.epoch != expiry_snapshot_id.epoch) {
        return crossed_transient_fence_v1::not_terminalizable;
    }
    if (state.last_snapshot_id.sequence < expiry_snapshot_id.sequence)
        return crossed_transient_fence_v1::waiting_for_expiry;

    const auto *slot = Worr_EventJournalResolveV1(
        &state.journal, authority.slot);
    if (!slot ||
        (slot->state & WORR_EVENT_SLOT_RECEIVED) == 0 ||
        !event_id_equal(slot->record.event_id, record.event_id) ||
        Worr_EventJournalCancelV1(
            &state.journal, authority.slot) !=
            WORR_EVENT_JOURNAL_INSERTED) {
        return crossed_transient_fence_v1::not_terminalizable;
    }

    /* The source projection was skipped, so no presentation context can be
     * reconstructed.  Once snapshot-ID order also crosses the transient's
     * deadline, cancellation is final without inventing a render-time expiry
     * bound.  Keep reliable/persistent records on the strict resync path. */
    authority.state |= AUTHORITY_SKIP;
    increment_saturated(state.status.authoritative_stale_or_coalesced);
    return crossed_transient_fence_v1::terminalized;
}

cg_event_runtime_result_v1 try_attach_declared_snapshot_fence(
    runtime_state_t &state, authority_entry_t &authority)
{
    if ((authority.record.flags & WORR_EVENT_FLAG_SNAPSHOT_FENCED) == 0 ||
        (authority.state & AUTHORITY_HAS_SNAPSHOT_REF) != 0) {
        return CG_EVENT_RUNTIME_OK;
    }

    worr_snapshot_id_v2 snapshot_id{};
    if (!derive_declared_snapshot_fence(
            state, authority.record, snapshot_id)) {
        return CG_EVENT_RUNTIME_NOT_READY;
    }
    if (auto *fence = find_snapshot_fence(state, snapshot_id)) {
        /* source_tick is the legacy per-client wire snapshot number used to
         * derive snapshot_id.sequence.  The exact ID is the cross-domain
         * lineage proof: an event-only peer reconstructs a legacy projection
         * clock from that per-client wire stream, while the event retains the
         * server map clock. No existing snapshot flag asserts clock-domain
         * identity, so neither source_tick nor source_time_us is compared to
         * the exact snapshot's own tick/time. */
        return attach_authority_fence(
                   state, authority, fence->snapshot_id,
                   fence->snapshot_tick, fence->snapshot_time_us, true)
                   ? CG_EVENT_RUNTIME_OK
                   : CG_EVENT_RUNTIME_DEGRADED;
    }

    /* A future source snapshot can still arrive.  Once the observed timeline
     * has crossed the declared identity, absence from the mirrored retention
     * window is irrecoverable and must not leave a reliable ordered head
     * stalled forever. */
    if (!state.has_last_snapshot ||
        state.last_snapshot_id.epoch != snapshot_id.epoch ||
        state.last_snapshot_id.sequence < snapshot_id.sequence) {
        return CG_EVENT_RUNTIME_NOT_READY;
    }

    const auto transient = handle_crossed_transient_fence(
        state, authority, snapshot_id);
    if (transient == crossed_transient_fence_v1::terminalized)
        return CG_EVENT_RUNTIME_OK;
    if (transient == crossed_transient_fence_v1::waiting_for_expiry)
        return CG_EVENT_RUNTIME_NOT_READY;

    mark_authority_fence_mismatch(state, authority, true);
    return CG_EVENT_RUNTIME_DEGRADED;
}

bool attach_authority_reference(runtime_state_t &state,
                                authority_entry_t &authority,
                                reference_entry_t &reference)
{
    if ((authority.record.flags &
         WORR_EVENT_FLAG_SNAPSHOT_FENCED) != 0) {
        worr_snapshot_id_v2 declared_snapshot_id{};
        if (!derive_declared_snapshot_fence(
                state, authority.record, declared_snapshot_id) ||
            !snapshot_id_equal(reference.snapshot_id,
                               declared_snapshot_id)) {
            /* An explicit fence's exact ID is lineage, not a hint. A
             * semantically matching authority ref at any other snapshot must
             * not bypass the declared source identity in either arrival
             * order. */
            mark_authority_fence_mismatch(state, authority, true);
            return false;
        }
    }
    if (reference.ref.semantic_version != authority.record.model_revision ||
        reference.ref.semantic_hash != authority.semantic_hash) {
        mark_authority_fence_mismatch(state, authority, false);
        return false;
    }
    if (!attach_authority_fence(
            state, authority, reference.snapshot_id,
            reference.snapshot_tick, reference.snapshot_time_us, false)) {
        return false;
    }
    reference.consumed = true;
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

cg_event_runtime_result_v1 map_predicted_presentation_resolution(
    std::uint8_t resolution)
{
    switch (resolution) {
    case WORR_PREDICTED_PRESENTATION_CONFIRMED_PENDING:
    case WORR_PREDICTED_PRESENTATION_CONFIRMED_SUPPRESSED:
        return CG_EVENT_RUNTIME_MATCHED;
    case WORR_PREDICTED_PRESENTATION_CORRECTED_BEFORE_PRESENTATION:
        return CG_EVENT_RUNTIME_CORRECTED;
    case WORR_PREDICTED_PRESENTATION_CORRECTED_AFTER_PRESENTATION:
        return CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION;
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
    worr_predicted_presentation_decision_v1 presentation_decision{};
    bool suppress_authority = false;

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

    if (prediction) {
        const auto presentation_result =
            Worr_PredictedPresentationResolveV1(
                &prediction->record, &record,
                WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
                prediction->presented, &presentation_decision);
        if (presentation_result != WORR_PREDICTED_PRESENTATION_OK) {
            mark_degraded(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        suppress_authority =
            presentation_decision.authority_action ==
            WORR_PREDICTED_PRESENTATION_SUPPRESS_AUTHORITY;
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
        reconciliation_result = map_predicted_presentation_resolution(
            presentation_decision.resolution);
        prediction->reconciled = true;
        prediction->authority_id = record.event_id;
        prediction->has_authority_id = true;
        if (suppress_authority && slot.index < state.journal.capacity) {
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
    if (suppress_authority)
        binding->state |= AUTHORITY_SIDE_EFFECT_PRESENTED;
    if (is_private_authority_receipt(record)) {
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
    } else if ((record.flags & WORR_EVENT_FLAG_SNAPSHOT_FENCED) != 0) {
        attachment_degraded =
            try_attach_declared_snapshot_fence(state, *binding) ==
            CG_EVENT_RUNTIME_DEGRADED;
    }
    if (!attachment_degraded)
        try_attach_declared_snapshot_expiry(state, *binding);

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
        const bool authority_presented =
            (authority->state & (AUTHORITY_PRESENTED |
                                 AUTHORITY_SIDE_EFFECT_PRESENTED)) != 0;
        worr_predicted_presentation_decision_v1 presentation_decision{};
        const auto presentation_result =
            Worr_PredictedPresentationResolveV1(
                &record, &authority->record,
                WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
                authority_presented, &presentation_decision);
        if (presentation_result != WORR_PREDICTED_PRESENTATION_OK) {
            mark_degraded(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        const auto result = map_predicted_presentation_resolution(
            presentation_decision.resolution);
        const bool suppress_further_presentation =
            presentation_decision.authority_action ==
            WORR_PREDICTED_PRESENTATION_SUPPRESS_AUTHORITY;
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
        tombstone->presented = suppress_further_presentation;
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
    for (auto &entry : state.snapshot_fences)
        entry = {};
    for (auto &entry : state.references)
        entry = {};
    state.status.reference_count = 0;
    for (auto &entry : state.authority) {
        entry.state &= ~(AUTHORITY_HAS_SNAPSHOT_REF |
                         AUTHORITY_HAS_EXPIRY_SNAPSHOT_REF |
                         AUTHORITY_HAS_EXPIRY_CROSSING_BOUND |
                         AUTHORITY_REF_MISMATCH);
        entry.fence_tick = 0;
        entry.expiry_fence_tick = 0;
        entry.expiry_crossing_tick = 0;
        entry.fence_time_us = 0;
        entry.expiry_fence_time_us = 0;
        entry.expiry_crossing_time_us = 0;
        entry.fence_snapshot_id = {};
        entry.expiry_snapshot_id = {};
        entry.expiry_crossing_snapshot_id = {};
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

void require_authority_resync(runtime_state_t &state)
{
    state.status.authority_requires_resync = 1;
    mark_authority_degraded(state);
}

void consume_authority_cursor(runtime_state_t &state)
{
    if (state.status.next_authority_sequence == UINT32_MAX) {
        state.status.authority_sequence_exhausted = 1;
        return;
    }
    ++state.status.next_authority_sequence;
}

void consume_private_reconciliation_cursor(runtime_state_t &state)
{
    if (state.status.next_private_reconciliation_sequence == UINT32_MAX) {
        state.status.private_reconciliation_sequence_exhausted = 1;
        return;
    }
    ++state.status.next_private_reconciliation_sequence;
}

cg_event_runtime_result_v1 terminalize_authority_skip_head(
    runtime_state_t &state, bool &terminalized)
{
    terminalized = false;
    if (state.status.authority_sequence_exhausted != 0)
        return CG_EVENT_RUNTIME_OK;
    const worr_event_id_v1 id{
        state.status.authority_epoch,
        state.status.next_authority_sequence,
    };
    authority_entry_t *entry = find_authority(state, id);
    if (!entry || (entry->state & AUTHORITY_SKIP) == 0)
        return CG_EVENT_RUNTIME_OK;

    if (is_private_authority_receipt(entry->record)) {
        /* Private reconciliation is an irreversible application side effect.
         * Its independent ordered cursor must commit the callback before the
         * journal/presentation transaction can make this skip reclaimable. */
        if ((entry->state & AUTHORITY_PRIVATE_RECONCILED) == 0) {
            require_authority_resync(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        /* A private receipt owns a normal reliable journal resident even
         * though it has no generic presentation side effect. Prove that the
         * slot still contains this exact record before making it reclaimable.
         * Other AUTHORITY_SKIP producers can alias a newer persistent record
         * or a recycled generation and must never mark that slot presented. */
        worr_event_journal_slot_v1 *slot =
            resolve_journal_mutable(state, entry->slot);
        if (!slot ||
            (slot->state & WORR_EVENT_SLOT_RECEIVED) == 0 ||
            !authoritative_records_equal(slot->record, entry->record)) {
            require_authority_resync(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        if ((slot->state & WORR_EVENT_SLOT_PRESENTED) != 0) {
            require_authority_resync(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        const bool already_terminal =
            (slot->state & (WORR_EVENT_SLOT_EXPIRED |
                            WORR_EVENT_SLOT_CANCELED)) != 0;
        if (!already_terminal &&
            Worr_EventJournalMarkPresentedV1(
                &state.journal, entry->slot) !=
                WORR_EVENT_JOURNAL_INSERTED) {
            require_authority_resync(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
    }

    entry->state |= AUTHORITY_PRESENTED;
    increment_saturated(state.status.authoritative_terminal_skips);
    consume_authority_cursor(state);
    terminalized = true;
    return CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1 apply_private_reconciliation(
    runtime_state_t &state, authority_entry_t &entry)
{
    const auto &record = entry.record;
    if (is_local_interaction_authority_receipt(record)) {
        worr_local_interaction_authority_receipt_v1 receipt{};
        std::memcpy(&receipt, record.payload, sizeof(receipt));
        if (!local_interaction_receipt_accepted(
                CG_LocalInteractionSubmitAuthorityReceipt(&receipt))) {
            require_authority_resync(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
    } else if (is_local_action_shadow_authority_receipt(record)) {
        worr_local_action_shadow_authority_receipt_v1 receipt{};
        std::memcpy(&receipt, record.payload, sizeof(receipt));
        if (!local_action_shadow_receipt_accepted(
                CG_LocalActionShadowSubmitAuthorityReceipt(&receipt))) {
            require_authority_resync(state);
            return CG_EVENT_RUNTIME_DEGRADED;
        }
        /* Report at the ordered receipt application boundary as well as the
         * prediction pass. A receipt can complete a pair after the last replay
         * in a frame; waiting for another replay would make live parity
         * evidence depend on renderer/client cadence. */
        if (local_action_shadow_report_callback)
            local_action_shadow_report_callback();
    } else {
        require_authority_resync(state);
        return CG_EVENT_RUNTIME_DEGRADED;
    }

    entry.state |= AUTHORITY_PRIVATE_RECONCILED;
    return CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1 drain_private_reconciliation_heads(
    runtime_state_t &state)
{
    for (;;) {
        if (state.status.private_reconciliation_sequence_exhausted != 0)
            return CG_EVENT_RUNTIME_OK;
        const worr_event_id_v1 id{
            state.status.authority_epoch,
            state.status.next_private_reconciliation_sequence,
        };
        authority_entry_t *entry = find_authority(state, id);
        if (!entry)
            return CG_EVENT_RUNTIME_OK;

        if (is_private_authority_receipt(entry->record) &&
            (entry->state & AUTHORITY_PRIVATE_RECONCILED) == 0) {
            const auto result = apply_private_reconciliation(state, *entry);
            if (result != CG_EVENT_RUNTIME_OK)
                return result;
        }

        /* This application cursor orders only private reconciliation. It may
         * cross admitted visual records without marking their journal slots or
         * changing the independent presentation cursor. */
        consume_private_reconciliation_cursor(state);
    }
}

cg_event_runtime_result_v1 drain_authority_skip_heads(
    runtime_state_t &state)
{
    for (;;) {
        bool terminalized = false;
        const auto result =
            terminalize_authority_skip_head(state, terminalized);
        if (result != CG_EVENT_RUNTIME_OK || !terminalized)
            return result;
    }
}

cg_event_runtime_presentation_context_v1 presentation_context(
    std::uint32_t provenance, worr_snapshot_id_v2 snapshot_id,
    std::uint32_t fence_tick, std::uint64_t fence_time_us)
{
    cg_event_runtime_presentation_context_v1 context{};
    context.struct_size = sizeof(context);
    context.schema_version = CG_EVENT_RUNTIME_PRESENTER_VERSION;
    context.provenance = provenance;
    context.fence_snapshot_id = snapshot_id;
    context.fence_tick = fence_tick;
    context.fence_time_us = fence_time_us;
    return context;
}

bool presenter_ready(const worr_event_record_v1 &record,
                     const cg_event_runtime_presentation_context_v1 &context)
{
    if (!event_can_present_callback && !event_present_callback)
        return true;
    if (!event_can_present_callback || !event_present_callback ||
        presentation_callback_active) {
        return false;
    }

    presentation_callback_active = true;
    const bool ready = event_can_present_callback(&record, &context);
    presentation_callback_active = false;
    return ready;
}

void presenter_commit(
    const worr_event_record_v1 &record,
    const cg_event_runtime_presentation_context_v1 &context)
{
    if (!event_present_callback || presentation_callback_active)
        return;

    presentation_callback_active = true;
    event_present_callback(&record, &context);
    presentation_callback_active = false;
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
        const auto context = presentation_context(
            CG_EVENT_RUNTIME_PRESENTATION_PREDICTED, {},
            best->record.source_tick, best->record.source_time_us);
        if (!presenter_ready(best->record, context)) {
            state.status.authority_requires_resync = 1;
            mark_authority_degraded(state);
            return CG_EVENT_RUNTIME_DEGRADED;
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
        presenter_commit(best->record, context);
        ++advanced;
    }
    return CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1 present_authority(
    runtime_state_t &state, std::uint64_t render_time_us,
    std::uint32_t now_tick, std::uint32_t max_presentations,
    std::uint32_t &advanced)
{
    if (state.status.authority_sequence_exhausted != 0)
        return advanced < max_presentations
                   ? CG_EVENT_RUNTIME_NOT_READY
                   : CG_EVENT_RUNTIME_OK;

    std::uint32_t processed = 0;
    while (state.status.authority_sequence_exhausted == 0 &&
           advanced < max_presentations &&
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
            bool terminalized = false;
            const auto terminal_result =
                terminalize_authority_skip_head(state, terminalized);
            if (terminal_result != CG_EVENT_RUNTIME_OK)
                return terminal_result;
            if (!terminalized) {
                require_authority_resync(state);
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
                consume_authority_cursor(state);
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
        /* Explicit snapshot-fenced legacy events retain the producer's server
         * map clock even when an event-only client presents from a per-client
         * legacy projection clock. For ordinary ref-bound records, retain the
         * source-clock gate as an independent semantic-order check. */
        const bool explicit_cross_domain_fence =
            (entry->record.flags & WORR_EVENT_FLAG_SNAPSHOT_FENCED) != 0;
        if (entry->fence_time_us > render_time_us ||
            !tick_reached(now_tick, entry->fence_tick) ||
            (!explicit_cross_domain_fence &&
             (entry->record.source_time_us > render_time_us ||
              !tick_reached(now_tick, entry->record.source_tick)))) {
            increment_saturated(state.status.future_time_stalls);
            return CG_EVENT_RUNTIME_NOT_READY;
        }

        if ((entry->state & AUTHORITY_SIDE_EFFECT_PRESENTED) != 0) {
            entry->state |= AUTHORITY_PRESENTED;
            increment_saturated(
                state.status.authoritative_prediction_suppressions);
            consume_authority_cursor(state);
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
            if (!authority_slot_needs_presentation(
                    state, *entry, *slot, render_time_us,
                    now_tick)) {
                return CG_EVENT_RUNTIME_NOT_READY;
            }
            const auto context = presentation_context(
                CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY,
                entry->fence_snapshot_id, entry->fence_tick,
                entry->fence_time_us);
            if (!presenter_ready(entry->record, context)) {
                state.status.authority_requires_resync = 1;
                mark_authority_degraded(state);
                return CG_EVENT_RUNTIME_DEGRADED;
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
            presenter_commit(entry->record, context);
            ++advanced;
        }

        consume_authority_cursor(state);
    }
    /* A visual record can consume the final presentation budget while
     * revealing one or more ordered control/stale skips immediately behind
     * it. Skips have no presentation side effect and must not wait for a
     * later render frame merely because the visual budget reached zero. */
    return drain_authority_skip_heads(state);
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

void CG_EventRuntimeSetPresenter(
    cg_event_runtime_can_present_callback_v1 can_present,
    cg_event_runtime_present_callback_v1 present)
{
    if (runtime_mutation_blocked())
        return;
    /* Half-installed presentation authority would turn a validated journal
     * commit into either a lost effect or an unvalidated side effect.  Keep
     * the mismatch observable: presenter_ready() rejects it before the
     * journal mark and the normal authority resync latch closes the stream. */
    event_can_present_callback = can_present;
    event_present_callback = present;
}

void CG_EventRuntimeSetLocalActionShadowReportCallback(
    cg_local_action_shadow_report_callback_v1 callback)
{
    if (runtime_mutation_blocked())
        return;
    local_action_shadow_report_callback = callback;
}

cg_event_runtime_result_v1
CG_EventRuntimeResetLegacy(std::uint32_t stream_epoch)
{
    if (runtime_mutation_blocked())
        return CG_EVENT_RUNTIME_REENTRANT;
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
    if (runtime_mutation_blocked())
        return CG_EVENT_RUNTIME_REENTRANT;
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
    runtime.status.next_private_reconciliation_sequence = 0;
    runtime.status.authority_sequence_exhausted = 0;
    runtime.status.private_reconciliation_sequence_exhausted = 0;
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
    runtime.status.next_private_reconciliation_sequence =
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
    if (runtime_mutation_blocked())
        return CG_EVENT_RUNTIME_REENTRANT;
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
    if (runtime_mutation_blocked())
        return;
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
    if (runtime_mutation_blocked())
        return;
    latch_local_interaction_resync();
}

cg_event_runtime_result_v1 CG_EventRuntimeSubmitAuthoritativeBatch(
    const worr_event_record_v1 *records, std::uint32_t count)
{
    if (runtime_mutation_blocked())
        return CG_EVENT_RUNTIME_REENTRANT;
    if (!records || !count ||
        count > WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2)
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    if (!runtime.authority_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
    latch_local_interaction_resync();
    if (runtime.status.authority_requires_resync != 0)
        return CG_EVENT_RUNTIME_NOT_READY;
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
    bool private_reconciliation_accepted =
        drain_private_reconciliation_heads(runtime) == CG_EVENT_RUNTIME_OK;
    if (!private_reconciliation_accepted)
        aggregate = CG_EVENT_RUNTIME_DEGRADED;
    latch_local_interaction_resync();
    if (runtime.status.authority_requires_resync != 0) {
        private_reconciliation_accepted = false;
        aggregate = CG_EVENT_RUNTIME_DEGRADED;
        mark_authority_degraded(runtime);
    }
    if (private_reconciliation_accepted) {
        /* Reconciliation callbacks are irreversible, so the application
         * cursor runs only after the accepted authority batch commits. The
         * subsequent presentation cursor/journal terminalization remains an
         * all-or-nothing local-state transaction: an invariant failure retains
         * the committed evidence and applied reconciliation, leaves the
         * presentation cursor untouched, and requires a fresh authority epoch. */
        copy_state(staging, runtime);
        const auto drain_result = drain_authority_skip_heads(staging);
        if (drain_result == CG_EVENT_RUNTIME_OK) {
            commit_staging();
        } else {
            require_authority_resync(runtime);
            aggregate = drain_result;
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
    if (runtime_mutation_blocked())
        return CG_EVENT_RUNTIME_REENTRANT;
    if (!records || !count ||
        count > WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2)
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    if (!runtime.authority_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
    latch_local_interaction_resync();
    if (runtime.status.authority_requires_resync != 0)
        return CG_EVENT_RUNTIME_NOT_READY;
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
    if (runtime_mutation_blocked())
        return CG_EVENT_RUNTIME_REENTRANT;
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
    if (runtime_mutation_blocked())
        return CG_EVENT_RUNTIME_REENTRANT;
    if (consumed_cursor.epoch == 0)
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    if (!runtime.authority_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
    if (runtime.status.authority_requires_resync != 0)
        return CG_EVENT_RUNTIME_NOT_READY;
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
    if (runtime_mutation_blocked())
        return CG_EVENT_RUNTIME_REENTRANT;
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
    transaction_active = true;
    copy_state(staging, runtime);
    cg_event_runtime_result_v1 aggregate = CG_EVENT_RUNTIME_EMPTY;
    bool authority_attachment_degraded = false;
    bool authority_terminalized = false;
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
    retain_snapshot_fence(staging, *snapshot);
    staging.has_last_snapshot = true;
    staging.last_snapshot_id = snapshot->snapshot_id;
    staging.last_snapshot_time_us = snapshot->server_time_us;
    staging.last_snapshot_event_hash = snapshot->event_hash;
    for (auto &authority : staging.authority) {
        if (!authority.occupied ||
            (authority.state & (AUTHORITY_SKIP |
                                AUTHORITY_PRESENTED)) != 0 ||
            (authority.record.flags &
             WORR_EVENT_FLAG_SNAPSHOT_FENCED) == 0) {
            continue;
        }
        if ((authority.state & AUTHORITY_HAS_SNAPSHOT_REF) == 0) {
            const bool was_skipped =
                (authority.state & AUTHORITY_SKIP) != 0;
            const auto fence_result =
                try_attach_declared_snapshot_fence(staging, authority);
            authority_terminalized |=
                !was_skipped &&
                (authority.state & AUTHORITY_SKIP) != 0;
            if (fence_result == CG_EVENT_RUNTIME_DEGRADED) {
                aggregate = CG_EVENT_RUNTIME_DEGRADED;
                authority_attachment_degraded = true;
            } else if (fence_result == CG_EVENT_RUNTIME_OK &&
                       aggregate == CG_EVENT_RUNTIME_EMPTY) {
                aggregate = CG_EVENT_RUNTIME_OK;
            }
        }
        if ((authority.state & AUTHORITY_REF_MISMATCH) == 0)
            try_attach_declared_snapshot_expiry(staging, authority);
    }
    if (authority_terminalized &&
        drain_authority_skip_heads(staging) != CG_EVENT_RUNTIME_OK) {
        aggregate = CG_EVENT_RUNTIME_DEGRADED;
        authority_attachment_degraded = true;
    }
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
    if (runtime_mutation_blocked())
        return CG_EVENT_RUNTIME_REENTRANT;
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
    if (runtime_mutation_blocked())
        return CG_EVENT_RUNTIME_REENTRANT;
    if (!advanced_out || !max_presentations)
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    const bool observe_legacy = CG_EventRuntimeAuditEnabled();
    if (!observe_legacy && !runtime.authority_initialized) {
        *advanced_out = 0;
        return CG_EVENT_RUNTIME_EMPTY;
    }
    if (!runtime.legacy_initialized && !runtime.authority_initialized)
        return CG_EVENT_RUNTIME_UNINITIALIZED;
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
        expire_runtime_journal(runtime, render_time_us, now_tick);
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
    /* Status inspection is allowed from presenter and private-reconciliation
     * callbacks so diagnostics can observe the exact pre/post-commit boundary.
     * Do not let that read path latch external health and mutate the runtime
     * while any callback/transaction guard is active. */
    if (!runtime_mutation_blocked())
        latch_local_interaction_resync();
    auto status = runtime.status;
    status.struct_size = sizeof(status);
    status.schema_version = CG_EVENT_RUNTIME_VERSION;
    if (runtime.authority_initialized)
        status.receipt = runtime.journal.receipt;
    *status_out = status;
    return true;
}

bool CG_EventRuntimeGetCheckpointBlock(
    std::uint32_t expected_authority_epoch,
    cg_event_runtime_checkpoint_block_v1 *block_out)
{
    if (!block_out)
        return false;

    *block_out = {};
    block_out->struct_size = sizeof(*block_out);
    block_out->expected_authority_epoch = expected_authority_epoch;
    block_out->authority_epoch = runtime.status.authority_epoch;
    block_out->next_authority_sequence =
        runtime.status.next_authority_sequence;
    block_out->last_snapshot_id = runtime.last_snapshot_id;
    block_out->last_snapshot_time_us = runtime.last_snapshot_time_us;
    block_out->last_render_time_us = runtime.status.last_render_time_us;
    block_out->last_now_tick = runtime.status.last_now_tick;

    if (runtime_mutation_blocked()) {
        block_out->reason = CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_MUTATION;
        return true;
    }
    if (!runtime.authority_initialized) {
        block_out->reason =
            CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_UNINITIALIZED;
        return true;
    }
    if (runtime.status.authority_epoch != expected_authority_epoch) {
        block_out->reason = CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_WRONG_EPOCH;
        return true;
    }
    if (runtime.status.authority_requires_resync != 0 ||
        runtime.status.authority_degraded != 0) {
        block_out->reason = CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_UNHEALTHY;
        return true;
    }

    const worr_event_id_v1 head_id{
        runtime.status.authority_epoch,
        runtime.status.next_authority_sequence,
    };
    const authority_entry_t *pending = find_authority(runtime, head_id);
    if (!pending ||
        (pending->state & AUTHORITY_PRESENTED) != 0) {
        pending = nullptr;
        for (const auto &entry : runtime.authority) {
            if (!entry.occupied ||
                (entry.state & AUTHORITY_PRESENTED) != 0) {
                continue;
            }
            if (!pending || entry.record.event_id.sequence <
                                pending->record.event_id.sequence) {
                pending = &entry;
            }
        }
        if (!pending) {
            block_out->reason = CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_NONE;
            return false;
        }
        block_out->reason = CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_MISSING_HEAD;
    } else {
        block_out->reason = CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_PENDING_HEAD;
    }

    block_out->pending_sequence = pending->record.event_id.sequence;
    block_out->authority_state = pending->state;
    block_out->record_flags = pending->record.flags;
    block_out->payload_kind = pending->record.payload_kind;
    block_out->delivery_class = pending->record.delivery_class;
    block_out->source_tick = pending->record.source_tick;
    block_out->expiry_tick = pending->record.expiry_tick;
    block_out->fence_tick = pending->fence_tick;
    block_out->fence_snapshot_id = pending->fence_snapshot_id;
    block_out->fence_time_us = pending->fence_time_us;
    if (pending->slot.index < runtime.journal.capacity) {
        const auto &slot = runtime.journal_slots[pending->slot.index];
        if (slot.generation == pending->slot.generation)
            block_out->slot_state = slot.state;
    }
    return true;
}

bool CG_EventRuntimeCheckpointReady(
    std::uint32_t expected_authority_epoch,
    cg_event_runtime_status_v1 *status_out)
{
    if (!status_out || expected_authority_epoch == 0 ||
        runtime_mutation_blocked()) {
        return false;
    }

    /* Fold independent reconciliation health into the same decision that
     * produces the counter baseline.  Splitting this into GetStatus plus a
     * second readiness query would allow a caller to pair unlike states. */
    latch_local_interaction_resync();
    if (!runtime.authority_initialized ||
        runtime.status.authority_epoch != expected_authority_epoch ||
        runtime.status.authority_requires_resync != 0 ||
        runtime.status.authority_degraded != 0 ||
        authority_has_unpresented_records(runtime)) {
        return false;
    }

    auto status = runtime.status;
    status.struct_size = sizeof(status);
    status.schema_version = CG_EVENT_RUNTIME_VERSION;
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
