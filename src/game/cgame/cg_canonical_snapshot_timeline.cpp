/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_canonical_snapshot_timeline.hpp"
#include "cg_event_runtime.hpp"
#include "cg_local.h"

#include <array>
#include <cstdio>

namespace {

constexpr std::uint32_t no_slot = WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT;

static_assert(CG_CANONICAL_SNAPSHOT_TIMELINE_ENTITY_CAPACITY <=
                  CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES,
              "canonical cgame entity capacity exceeds the protocol limit");

struct canonical_snapshot_timeline_state_t {
    worr_snapshot_timeline_v1 timeline;
    std::array<worr_snapshot_timeline_slot_v1,
               CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY> slots;
    std::array<worr_snapshot_entity_v2,
               CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY *
                   CG_CANONICAL_SNAPSHOT_TIMELINE_ENTITY_CAPACITY> entities;
    std::array<std::uint8_t,
               CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY *
                   CG_CANONICAL_SNAPSHOT_TIMELINE_AREA_CAPACITY> area_bytes;
    std::array<worr_snapshot_event_ref_v2,
               CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY *
                   CG_CANONICAL_SNAPSHOT_TIMELINE_EVENT_CAPACITY> event_refs;
    std::array<cg_canonical_prediction_receipt_v1,
               CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY>
        prediction_receipts;

    worr_cgame_snapshot_timeline_status_v2 status;
    worr_snapshot_timeline_ref_v1 latest_ref;
    std::uint64_t reset_host_time_us;
    std::uint64_t accepted_in_epoch;
    std::uint64_t clock_failures;
    std::uint64_t query_failures;
    std::uint64_t capacity_rejections;
    std::uint32_t last_reset_reason;
    std::uint32_t last_clock_result;
    std::uint32_t last_query_result;
    bool initialized;
    bool active;
    bool pending_clock_reset;
};

canonical_snapshot_timeline_state_t canonical;

void increment_saturated(std::uint64_t &value)
{
    if (value != UINT64_MAX)
        ++value;
}

void clear_latest()
{
    canonical.latest_ref = { no_slot, 0u };
    canonical.accepted_in_epoch = 0;
    canonical.status.last_receive_time_us = 0;
    canonical.status.last_endpoint_hash = 0;
    canonical.status.last_legacy_parity_hash = 0;
    canonical.status.last_snapshot_id = {};
    canonical.status.last_snapshot_hash = 0;
    canonical.status.last_event_fence_result =
        CG_EVENT_RUNTIME_UNINITIALIZED;
    canonical.status.receipt_flags = 0;
    canonical.prediction_receipts.fill({});
}

worr_snapshot_timeline_result_v1 ensure_initialized()
{
    if (canonical.initialized)
        return WORR_SNAPSHOT_TIMELINE_OK;

    const auto result = Worr_SnapshotTimelineInitV1(
        &canonical.timeline, canonical.slots.data(),
        static_cast<std::uint32_t>(canonical.slots.size()),
        canonical.entities.data(),
        static_cast<std::uint32_t>(canonical.entities.size()),
        CG_CANONICAL_SNAPSHOT_TIMELINE_ENTITY_CAPACITY,
        canonical.area_bytes.data(),
        static_cast<std::uint32_t>(canonical.area_bytes.size()),
        CG_CANONICAL_SNAPSHOT_TIMELINE_AREA_CAPACITY,
        canonical.event_refs.data(),
        static_cast<std::uint32_t>(canonical.event_refs.size()),
        CG_CANONICAL_SNAPSHOT_TIMELINE_EVENT_CAPACITY,
        CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES);

    canonical.status = {};
    canonical.status.struct_size = sizeof(canonical.status);
    canonical.status.api_version = WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION;
    canonical.status.last_result = result;
    canonical.last_clock_result = result;
    canonical.last_query_result = result;
    clear_latest();
    canonical.initialized = result == WORR_SNAPSHOT_TIMELINE_OK;
    return result;
}

bool reset_reason_valid(std::uint32_t reason)
{
    return reason >= WORR_CGAME_SNAPSHOT_RESET_CONNECTION &&
           reason <= WORR_CGAME_SNAPSHOT_RESET_UNLOAD;
}

std::uint16_t clock_reset_reason(std::uint32_t reason)
{
    switch (reason) {
    case WORR_CGAME_SNAPSHOT_RESET_MAP:
        return WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_MAP;
    case WORR_CGAME_SNAPSHOT_RESET_DEMO_SEEK:
        return WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_DEMO_REWIND;
    case WORR_CGAME_SNAPSHOT_RESET_HARD_RESYNC:
        return WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_HARD_RESYNC;
    case WORR_CGAME_SNAPSHOT_RESET_CONNECTION:
    case WORR_CGAME_SNAPSHOT_RESET_UNLOAD:
    default:
        return WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_INITIAL;
    }
}

bool hashes_equal(const worr_snapshot_projection_hashes_v2 &left,
                  const worr_snapshot_projection_hashes_v2 &right)
{
    return left.struct_size == right.struct_size &&
           left.schema_version == right.schema_version &&
           left.endpoint_hash == right.endpoint_hash &&
           left.legacy_parity_hash == right.legacy_parity_hash &&
           left.semantic_player_hash == right.semantic_player_hash &&
           left.semantic_entity_hash == right.semantic_entity_hash &&
           left.semantic_area_hash == right.semantic_area_hash &&
           left.semantic_event_hash == right.semantic_event_hash;
}

void reject(worr_snapshot_timeline_result_v1 result)
{
    canonical.status.last_result = result;
    increment_saturated(canonical.status.rejected);
    if (result == WORR_SNAPSHOT_TIMELINE_CAPACITY)
        increment_saturated(canonical.capacity_rejections);
}

worr_snapshot_timeline_result_v1 synchronize_clock_after_reset(
    const worr_snapshot_v2 &snapshot, std::uint64_t receive_time_us)
{
    if (!canonical.pending_clock_reset)
        return WORR_SNAPSHOT_TIMELINE_OK;

    worr_snapshot_timeline_clock_request_v1 request{};
    request.struct_size = sizeof(request);
    request.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    request.host_time_us = receive_time_us;
    if (request.host_time_us < canonical.reset_host_time_us)
        request.host_time_us = canonical.reset_host_time_us;
    if (canonical.timeline.clock.initialized &&
        request.host_time_us < canonical.timeline.clock.host_time_us) {
        request.host_time_us = canonical.timeline.clock.host_time_us;
    }
    request.render_time_us = snapshot.server_time_us;
    request.reset_reason = clock_reset_reason(canonical.last_reset_reason);

    if (canonical.timeline.clock.initialized) {
        request.operation = canonical.last_reset_reason ==
                                    WORR_CGAME_SNAPSHOT_RESET_DEMO_SEEK
                                ? WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK
                                : WORR_SNAPSHOT_TIMELINE_CLOCK_RESET;
    } else {
        request.operation = WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR;
        request.rate_q16 = WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16;
    }

    worr_snapshot_timeline_clock_state_v1 state{};
    const auto result =
        Worr_SnapshotTimelineClockApplyV1(&canonical.timeline, &request, &state);
    canonical.last_clock_result = result;
    if (result == WORR_SNAPSHOT_TIMELINE_OK)
        canonical.pending_clock_reset = false;
    else
        increment_saturated(canonical.clock_failures);
    return result;
}

void reset_consumer(std::uint32_t snapshot_epoch, std::uint32_t reason,
                    std::uint64_t host_time_us)
{
    auto result = ensure_initialized();
    increment_saturated(canonical.status.resets);
    if (result != WORR_SNAPSHOT_TIMELINE_OK) {
        canonical.status.last_result = result;
        canonical.active = false;
        canonical.status.active_epoch = 0;
        return;
    }
    if (!reset_reason_valid(reason) ||
        (reason != WORR_CGAME_SNAPSHOT_RESET_UNLOAD && snapshot_epoch == 0)) {
        canonical.status.last_result = WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
        canonical.active = false;
        canonical.status.active_epoch = 0;
        return;
    }

    result = Worr_SnapshotTimelineResetV1(&canonical.timeline);
    canonical.status.last_result = result;
    canonical.active = result == WORR_SNAPSHOT_TIMELINE_OK &&
                       reason != WORR_CGAME_SNAPSHOT_RESET_UNLOAD;
    canonical.status.active_epoch = canonical.active ? snapshot_epoch : 0;
    canonical.last_reset_reason = reason;
    canonical.last_clock_result =
        canonical.active ? WORR_SNAPSHOT_TIMELINE_CLOCK_UNINITIALIZED
                         : result;
    canonical.reset_host_time_us = host_time_us;
    canonical.pending_clock_reset = canonical.active;
    clear_latest();
    (void)CG_EventRuntimeResetSnapshot(
        canonical.active ? snapshot_epoch : 0u);
}

bool consume_snapshot(const worr_snapshot_projection_view_v2 *view,
                      const worr_snapshot_projection_hashes_v2 *hashes,
                      std::uint64_t receive_time_us)
{
    auto result = ensure_initialized();
    increment_saturated(canonical.status.consume_attempts);
    if (result != WORR_SNAPSHOT_TIMELINE_OK) {
        reject(result);
        return false;
    }
    if (!canonical.active) {
        reject(WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE);
        return false;
    }
    if (!view || !hashes ||
        view->struct_size != sizeof(*view) ||
        view->schema_version != WORR_SNAPSHOT_PROJECTION_VERSION ||
        hashes->struct_size != sizeof(*hashes) ||
        hashes->schema_version != WORR_SNAPSHOT_PROJECTION_VERSION ||
        !view->snapshot ||
        view->snapshot->snapshot_id.epoch != canonical.status.active_epoch) {
        if (canonical.status.rejected == 0 && cgi.Com_Print) {
            cgi.Com_Print(
                "cgame canonical snapshot timeline: rejected projection "
                "metadata or epoch\n");
        }
        reject(WORR_SNAPSHOT_TIMELINE_INVALID_PROJECTION);
        return false;
    }
    if (view->entity_count >
            CG_CANONICAL_SNAPSHOT_TIMELINE_ENTITY_CAPACITY ||
        view->area_byte_count >
            CG_CANONICAL_SNAPSHOT_TIMELINE_AREA_CAPACITY ||
        view->event_ref_count >
            CG_CANONICAL_SNAPSHOT_TIMELINE_EVENT_CAPACITY) {
        reject(WORR_SNAPSHOT_TIMELINE_CAPACITY);
        return false;
    }
    if (canonical.accepted_in_epoch != 0 &&
        receive_time_us < canonical.status.last_receive_time_us) {
        reject(WORR_SNAPSHOT_TIMELINE_TIME_ORDER);
        return false;
    }

    worr_snapshot_projection_hashes_v2 computed{};
    if (!Worr_SnapshotProjectionHashesV2(
            view, CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES, &computed) ||
        !hashes_equal(computed, *hashes)) {
        if (canonical.status.rejected == 0 && cgi.Com_Print) {
            cgi.Com_Print(
                "cgame canonical snapshot timeline: rejected projection "
                "hashes\n");
        }
        reject(WORR_SNAPSHOT_TIMELINE_INVALID_PROJECTION);
        return false;
    }

    worr_snapshot_timeline_ref_v1 ref{};
    result = Worr_SnapshotTimelinePublishV1(
        &canonical.timeline, view, receive_time_us, &ref);
    if (result != WORR_SNAPSHOT_TIMELINE_OK) {
        if (canonical.status.rejected == 0 && cgi.Com_Print) {
            char diagnostic[384];
            const std::uint32_t source_mask =
                WORR_SNAPSHOT_GENERATION_AUTHORITATIVE |
                WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
            const std::uint32_t expected_source =
                (view->snapshot->flags &
                 WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS)
                    ? WORR_SNAPSHOT_GENERATION_AUTHORITATIVE
                    : WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
            std::uint32_t bad_entity = UINT32_MAX;
            for (std::uint32_t i = 0; i < view->entity_count; ++i) {
                const auto &generation = view->entities[i].generation;
                if ((generation.provenance_flags & source_mask) !=
                        expected_source ||
                    (generation.identity.index ==
                         view->snapshot->controlled_entity.identity.index &&
                     (generation.identity.generation !=
                          view->snapshot->controlled_entity.identity.generation ||
                      generation.provenance_flags !=
                          view->snapshot->controlled_entity.provenance_flags))) {
                    bad_entity = i;
                    break;
                }
            }
            std::snprintf(
                diagnostic, sizeof(diagnostic),
                "cgame canonical snapshot timeline: publication result=%u "
                "flags=0x%x expected_source=0x%x snapshot_controlled=%u/%u/0x%x "
                "player_controlled=%u/%u/0x%x bad_entity=%u\n",
                static_cast<unsigned>(result), view->snapshot->flags,
                expected_source,
                view->snapshot->controlled_entity.identity.index,
                view->snapshot->controlled_entity.identity.generation,
                view->snapshot->controlled_entity.provenance_flags,
                view->player->controlled_entity.identity.index,
                view->player->controlled_entity.identity.generation,
                view->player->controlled_entity.provenance_flags,
                bad_entity);
            cgi.Com_Print(diagnostic);
        }
        reject(result);
        return false;
    }

    canonical.latest_ref = ref;
    canonical.prediction_receipts[ref.slot] = {};
    increment_saturated(canonical.accepted_in_epoch);
    increment_saturated(canonical.status.accepted);
    canonical.status.last_receive_time_us = receive_time_us;
    canonical.status.last_endpoint_hash = computed.endpoint_hash;
    canonical.status.last_legacy_parity_hash = computed.legacy_parity_hash;
    canonical.status.last_result = WORR_SNAPSHOT_TIMELINE_OK;

    /*
     * The timeline publication above is already committed.  Native transport
     * ACK authority additionally requires a fresh exact receipt proving that
     * the event fence accepted this same snapshot.  Legacy presentation stays
     * authoritative if that secondary fence fails; its caller can continue
     * while native admission observes the missing receipt and hard-resyncs.
     */
    const auto event_result = CG_EventRuntimeObserveSnapshot(
        view->snapshot, view->event_refs, view->event_ref_count);
    const bool event_fence_accepted =
        CG_EventRuntimeSnapshotFenceHealthy(
            view->snapshot->snapshot_id.epoch) &&
        (event_result == CG_EVENT_RUNTIME_OK ||
         event_result == CG_EVENT_RUNTIME_DUPLICATE ||
         (event_result == CG_EVENT_RUNTIME_EMPTY &&
          view->event_ref_count == 0));
    canonical.status.last_snapshot_id = view->snapshot->snapshot_id;
    canonical.status.last_snapshot_hash = view->snapshot->snapshot_hash;
    canonical.status.last_event_fence_result =
        static_cast<std::uint32_t>(event_result);
    canonical.status.receipt_flags =
        WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED;
    if (event_fence_accepted &&
        canonical.status.admission_generation != UINT64_MAX) {
        canonical.status.receipt_flags |=
            WORR_CGAME_SNAPSHOT_RECEIPT_EVENT_FENCE_ACCEPTED;
        ++canonical.status.admission_generation;
        auto &receipt = canonical.prediction_receipts[ref.slot];
        receipt.struct_size = sizeof(receipt);
        receipt.schema_version =
            CG_CANONICAL_PREDICTION_RECEIPT_VERSION;
        receipt.admission_generation =
            canonical.status.admission_generation;
        receipt.receipt_flags = canonical.status.receipt_flags;
        receipt.ref = ref;
        receipt.snapshot_id = view->snapshot->snapshot_id;
        receipt.snapshot_hash = view->snapshot->snapshot_hash;
        receipt.consumed_command =
            view->snapshot->consumed_command;
        receipt.server_tick = view->snapshot->server_tick;
        receipt.controlled_entity_index =
            view->snapshot->controlled_entity.identity.index;
        receipt.controlled_entity_generation =
            view->snapshot->controlled_entity.identity.generation;
        receipt.controlled_entity_provenance =
            view->snapshot->controlled_entity.provenance_flags;
        receipt.server_time_us = view->snapshot->server_time_us;
    }

    /* Publication is already durable at this point.  A terminal clock error
     * is reported without pretending that the copied snapshot was rejected. */
    const auto clock_result =
        synchronize_clock_after_reset(*view->snapshot, receive_time_us);
    if (clock_result != WORR_SNAPSHOT_TIMELINE_OK)
        canonical.status.last_result = clock_result;
    return true;
}

bool get_status(worr_cgame_snapshot_timeline_status_v2 *status_out)
{
    if (!status_out)
        return false;

    const auto init_result = ensure_initialized();
    if (init_result != WORR_SNAPSHOT_TIMELINE_OK)
        return false;

    worr_cgame_snapshot_timeline_status_v2 status = canonical.status;
    const auto result =
        Worr_SnapshotTimelineGetStatsV1(&canonical.timeline, &status.timeline);
    if (result != WORR_SNAPSHOT_TIMELINE_OK) {
        canonical.status.last_result = result;
        return false;
    }
    *status_out = status;
    return true;
}

const worr_cgame_snapshot_timeline_export_v2 snapshot_timeline_api = {
    sizeof(snapshot_timeline_api),
    WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION,
    reset_consumer,
    consume_snapshot,
    get_status,
};

worr_snapshot_timeline_result_v1 record_query_result(
    worr_snapshot_timeline_result_v1 result)
{
    canonical.last_query_result = result;
    if (result != WORR_SNAPSHOT_TIMELINE_OK)
        increment_saturated(canonical.query_failures);
    return result;
}

} // namespace

const worr_cgame_snapshot_timeline_export_v2 *
CG_GetCanonicalSnapshotTimelineAPI()
{
    return &snapshot_timeline_api;
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineInitialize()
{
    return ensure_initialized();
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineClockApply(
    const worr_snapshot_timeline_clock_request_v1 *request,
    worr_snapshot_timeline_clock_state_v1 *state_out)
{
    auto result = ensure_initialized();
    if (result != WORR_SNAPSHOT_TIMELINE_OK)
        return record_query_result(result);
    if (!canonical.active)
        return record_query_result(WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE);
    if (canonical.pending_clock_reset && request &&
        request->operation != WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR &&
        request->operation != WORR_SNAPSHOT_TIMELINE_CLOCK_RESET &&
        request->operation != WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK) {
        return record_query_result(
            WORR_SNAPSHOT_TIMELINE_CLOCK_UNINITIALIZED);
    }

    result = Worr_SnapshotTimelineClockApplyV1(
        &canonical.timeline, request, state_out);
    canonical.last_clock_result = result;
    if (result == WORR_SNAPSHOT_TIMELINE_OK && request &&
        (request->operation == WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR ||
         request->operation == WORR_SNAPSHOT_TIMELINE_CLOCK_RESET ||
         request->operation == WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK)) {
        canonical.pending_clock_reset = false;
    } else if (result != WORR_SNAPSHOT_TIMELINE_OK) {
        increment_saturated(canonical.clock_failures);
    }
    return record_query_result(result);
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineAdvanceClock(
    std::uint64_t host_time_us,
    worr_snapshot_timeline_clock_state_v1 *state_out)
{
    worr_snapshot_timeline_clock_request_v1 request{};
    request.struct_size = sizeof(request);
    request.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    request.operation = WORR_SNAPSHOT_TIMELINE_CLOCK_ADVANCE;
    request.reset_reason = WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE;
    request.host_time_us = host_time_us;
    return CG_CanonicalSnapshotTimelineClockApply(&request, state_out);
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineSelectPair(
    const worr_snapshot_timeline_policy_v1 *policy,
    worr_snapshot_timeline_pair_v1 *pair_out)
{
    auto result = ensure_initialized();
    if (result == WORR_SNAPSHOT_TIMELINE_OK && !canonical.active)
        result = WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (result == WORR_SNAPSHOT_TIMELINE_OK && canonical.pending_clock_reset)
        result = WORR_SNAPSHOT_TIMELINE_CLOCK_UNINITIALIZED;
    if (result == WORR_SNAPSHOT_TIMELINE_OK) {
        result = Worr_SnapshotTimelineSelectPairV1(
            &canonical.timeline, policy, pair_out);
    }
    return record_query_result(result);
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineSampleEntity(
    const worr_snapshot_timeline_policy_v1 *policy,
    const worr_snapshot_timeline_pair_v1 *pair,
    std::uint32_t entity_index,
    worr_snapshot_timeline_entity_sample_v1 *sample_out)
{
    auto result = ensure_initialized();
    if (result == WORR_SNAPSHOT_TIMELINE_OK && !canonical.active)
        result = WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (result == WORR_SNAPSHOT_TIMELINE_OK) {
        result = Worr_SnapshotTimelineSampleEntityV1(
            &canonical.timeline, policy, pair, entity_index, sample_out);
    }
    return record_query_result(result);
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopySnapshot(
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_v2 *snapshot_out)
{
    auto result = ensure_initialized();
    if (result == WORR_SNAPSHOT_TIMELINE_OK && !canonical.active)
        result = WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (result == WORR_SNAPSHOT_TIMELINE_OK) {
        result = Worr_SnapshotTimelineCopySnapshotV1(
            &canonical.timeline, ref, snapshot_out);
    }
    return record_query_result(result);
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineFindSnapshot(
    worr_snapshot_id_v2 snapshot_id,
    worr_snapshot_timeline_ref_v1 *ref_out)
{
    auto result = ensure_initialized();
    if (result == WORR_SNAPSHOT_TIMELINE_OK &&
        (!ref_out || snapshot_id.epoch == 0 || snapshot_id.sequence == 0)) {
        result = WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    }
    if (result == WORR_SNAPSHOT_TIMELINE_OK && !canonical.active)
        result = WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (result == WORR_SNAPSHOT_TIMELINE_OK &&
        snapshot_id.epoch != canonical.status.active_epoch) {
        result = WORR_SNAPSHOT_TIMELINE_NOT_FOUND;
    }
    if (result != WORR_SNAPSHOT_TIMELINE_OK)
        return record_query_result(result);

    std::uint32_t selected = UINT32_MAX;
    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(canonical.slots.size());
         ++index) {
        const auto &slot = canonical.slots[index];
        if (!slot.committed ||
            slot.snapshot.snapshot_id.epoch != snapshot_id.epoch ||
            slot.snapshot.snapshot_id.sequence != snapshot_id.sequence) {
            continue;
        }
        if (selected != UINT32_MAX)
            return record_query_result(WORR_SNAPSHOT_TIMELINE_CORRUPT);
        selected = index;
    }
    if (selected == UINT32_MAX)
        return record_query_result(WORR_SNAPSHOT_TIMELINE_NOT_FOUND);

    const worr_snapshot_timeline_ref_v1 ref{
        selected, canonical.slots[selected].generation};
    if (!Worr_SnapshotTimelineRefValidV1(&canonical.timeline, ref))
        return record_query_result(WORR_SNAPSHOT_TIMELINE_CORRUPT);
    *ref_out = ref;
    return record_query_result(WORR_SNAPSHOT_TIMELINE_OK);
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopyPlayer(
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_player_v2 *player_out)
{
    auto result = ensure_initialized();
    if (result == WORR_SNAPSHOT_TIMELINE_OK && !canonical.active)
        result = WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (result == WORR_SNAPSHOT_TIMELINE_OK) {
        result = Worr_SnapshotTimelineCopyPlayerV1(
            &canonical.timeline, ref, player_out);
    }
    return record_query_result(result);
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopyEntities(
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_entity_v2 *entities_out,
    std::uint32_t capacity,
    std::uint32_t *count_out)
{
    auto result = ensure_initialized();
    if (result == WORR_SNAPSHOT_TIMELINE_OK && !canonical.active)
        result = WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (result == WORR_SNAPSHOT_TIMELINE_OK) {
        result = Worr_SnapshotTimelineCopyEntitiesV1(
            &canonical.timeline, ref, entities_out, capacity, count_out);
    }
    return record_query_result(result);
}

worr_snapshot_timeline_result_v1
CG_CanonicalSnapshotTimelineCopyPredictionSnapshot(
    std::uint32_t snapshot_sequence,
    cg_canonical_prediction_snapshot_v2 *snapshot_out)
{
    auto result = ensure_initialized();
    if (result == WORR_SNAPSHOT_TIMELINE_OK &&
        (!snapshot_out || snapshot_sequence == 0)) {
        result = WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    }
    if (result == WORR_SNAPSHOT_TIMELINE_OK && !canonical.active)
        result = WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (result == WORR_SNAPSHOT_TIMELINE_OK &&
        canonical.pending_clock_reset) {
        result = WORR_SNAPSHOT_TIMELINE_CLOCK_UNINITIALIZED;
    }
    if (result != WORR_SNAPSHOT_TIMELINE_OK)
        return record_query_result(result);

    std::uint32_t selected = UINT32_MAX;
    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(canonical.slots.size());
         ++index) {
        const auto &slot = canonical.slots[index];
        if (!slot.committed ||
            slot.snapshot.snapshot_id.epoch !=
                canonical.status.active_epoch ||
            slot.snapshot.snapshot_id.sequence != snapshot_sequence) {
            continue;
        }
        if (selected != UINT32_MAX)
            return record_query_result(WORR_SNAPSHOT_TIMELINE_CORRUPT);
        selected = index;
    }
    if (selected == UINT32_MAX)
        return record_query_result(WORR_SNAPSHOT_TIMELINE_NOT_FOUND);

    const auto &slot = canonical.slots[selected];
    const worr_snapshot_timeline_ref_v1 ref{
        selected, slot.generation
    };
    const auto &stored_receipt =
        canonical.prediction_receipts[selected];
    constexpr std::uint32_t required_receipt_flags =
        WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED |
        WORR_CGAME_SNAPSHOT_RECEIPT_EVENT_FENCE_ACCEPTED;
    if (stored_receipt.struct_size != sizeof(stored_receipt) ||
        stored_receipt.schema_version !=
            CG_CANONICAL_PREDICTION_RECEIPT_VERSION ||
        stored_receipt.admission_generation == 0 ||
        stored_receipt.receipt_flags != required_receipt_flags ||
        stored_receipt.ref.slot != ref.slot ||
        stored_receipt.ref.generation != ref.generation) {
        return record_query_result(WORR_SNAPSHOT_TIMELINE_NOT_FOUND);
    }
    cg_canonical_prediction_snapshot_v2 staged{};
    staged.struct_size = sizeof(staged);
    staged.schema_version = CG_CANONICAL_PREDICTION_SNAPSHOT_VERSION;
    staged.active_epoch = canonical.status.active_epoch;
    staged.ref = ref;
    staged.receipt = stored_receipt;

    result = Worr_SnapshotTimelineCopySnapshotV1(
        &canonical.timeline, ref, &staged.snapshot);
    if (result == WORR_SNAPSHOT_TIMELINE_OK) {
        result = Worr_SnapshotTimelineCopyPlayerV1(
            &canonical.timeline, ref, &staged.player);
    }
    const auto &snapshot_generation = staged.snapshot.controlled_entity;
    const auto &player_generation = staged.player.controlled_entity;
    if (result == WORR_SNAPSHOT_TIMELINE_OK &&
        (canonical.status.active_epoch != staged.active_epoch ||
         !Worr_SnapshotTimelineRefValidV1(&canonical.timeline, ref) ||
         staged.snapshot.snapshot_id.epoch != staged.active_epoch ||
         staged.snapshot.snapshot_id.sequence != snapshot_sequence ||
         staged.receipt.snapshot_id.epoch !=
             staged.snapshot.snapshot_id.epoch ||
         staged.receipt.snapshot_id.sequence !=
             staged.snapshot.snapshot_id.sequence ||
         staged.receipt.snapshot_hash !=
             staged.snapshot.snapshot_hash ||
         staged.receipt.server_tick != staged.snapshot.server_tick ||
         staged.receipt.server_time_us !=
             staged.snapshot.server_time_us ||
         staged.receipt.controlled_entity_index !=
             snapshot_generation.identity.index ||
         staged.receipt.controlled_entity_generation !=
             snapshot_generation.identity.generation ||
         staged.receipt.controlled_entity_provenance !=
             snapshot_generation.provenance_flags ||
         snapshot_generation.identity.index !=
             player_generation.identity.index ||
         snapshot_generation.identity.generation !=
             player_generation.identity.generation ||
         snapshot_generation.provenance_flags !=
             player_generation.provenance_flags ||
         snapshot_generation.reserved0 != player_generation.reserved0)) {
        result = WORR_SNAPSHOT_TIMELINE_CORRUPT;
    }
    if (result == WORR_SNAPSHOT_TIMELINE_OK)
        *snapshot_out = staged;
    return record_query_result(result);
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineEventCursorBegin(
    worr_snapshot_timeline_event_cursor_v1 *cursor_out)
{
    auto result = ensure_initialized();
    if (result == WORR_SNAPSHOT_TIMELINE_OK && !canonical.active)
        result = WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (result == WORR_SNAPSHOT_TIMELINE_OK) {
        result = Worr_SnapshotTimelineEventCursorBeginV1(
            &canonical.timeline, cursor_out);
    }
    return record_query_result(result);
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineEventNext(
    const worr_snapshot_timeline_event_cursor_v1 *cursor,
    worr_snapshot_timeline_event_cursor_v1 *next_cursor_out,
    worr_snapshot_timeline_event_observation_v1 *observation_out)
{
    auto result = ensure_initialized();
    if (result == WORR_SNAPSHOT_TIMELINE_OK && !canonical.active)
        result = WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (result == WORR_SNAPSHOT_TIMELINE_OK) {
        result = Worr_SnapshotTimelineEventNextV1(
            &canonical.timeline, cursor, next_cursor_out, observation_out);
    }
    return record_query_result(result);
}

bool CG_CanonicalSnapshotTimelineGetDiagnostics(
    cg_canonical_snapshot_timeline_diagnostics_v1 *diagnostics_out)
{
    if (!diagnostics_out ||
        ensure_initialized() != WORR_SNAPSHOT_TIMELINE_OK) {
        return false;
    }

    cg_canonical_snapshot_timeline_diagnostics_v1 diagnostics{};
    diagnostics.struct_size = sizeof(diagnostics);
    diagnostics.initialized = canonical.initialized ? 1u : 0u;
    diagnostics.active = canonical.active ? 1u : 0u;
    diagnostics.pending_clock_reset =
        canonical.pending_clock_reset ? 1u : 0u;
    diagnostics.active_epoch = canonical.status.active_epoch;
    diagnostics.last_reset_reason = canonical.last_reset_reason;
    diagnostics.last_clock_result = canonical.last_clock_result;
    diagnostics.last_query_result = canonical.last_query_result;
    diagnostics.clock_failures = canonical.clock_failures;
    diagnostics.query_failures = canonical.query_failures;
    diagnostics.capacity_rejections = canonical.capacity_rejections;
    diagnostics.latest_ref = canonical.latest_ref;
    diagnostics.clock = canonical.timeline.clock;
    *diagnostics_out = diagnostics;
    return true;
}
