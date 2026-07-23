/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_event_shadow.hpp"
#include "cg_canonical_snapshot_timeline.hpp"
#include "cg_event_runtime.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,    \
                         __LINE__, #condition);                                 \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

constexpr std::uint32_t max_entities = 16;
constexpr std::uint32_t scratch_capacity = 4;

struct builder_storage_t {
    worr_cgame_event_range_builder_v2 builder{};
    std::array<worr_cgame_event_observed_v2, max_entities> observed{};
    std::array<std::uint32_t, max_entities> seen{};
    std::array<worr_event_record_v1, scratch_capacity> scratch{};
};

const worr_cgame_event_range_export_v2 *consumer;
worr_snapshot_v2 parity_snapshot{};
bool parity_snapshot_available;

void consume_range(void *, const worr_cgame_event_range_v2 *range)
{
    CHECK(consumer != nullptr);
    consumer->ConsumeCanonicalEventRange(range);
}

void initialize(builder_storage_t &storage, std::uint32_t epoch)
{
    storage = {};
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &storage.builder, storage.observed.data(), storage.seen.data(),
        static_cast<std::uint32_t>(storage.observed.size()),
        storage.scratch.data(),
        static_cast<std::uint32_t>(storage.scratch.size()), epoch));
    consumer->Reset(epoch, WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE);
}

worr_cgame_event_range_build_result_v2 deliver_frame(
    builder_storage_t &storage, std::uint32_t tick, std::uint64_t time_us,
    const worr_cgame_event_carrier_v2 *carriers, std::uint32_t count)
{
    return Worr_CGameEventRangeDeliverFrameV2(
        &storage.builder, tick, time_us, carriers, count, 0,
        consume_range, nullptr);
}

worr_cgame_event_action_candidate_v2 make_action_candidate(
    std::uint32_t presenter_kind, std::uint32_t tick)
{
    worr_cgame_event_action_candidate_v2 candidate{};
    candidate.struct_size = sizeof(candidate);
    candidate.source_entity_index = 1u;
    candidate.subject_entity_index = WORR_EVENT_NO_ENTITY;
    auto &record = candidate.record;
    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                   WORR_EVENT_FLAG_PRESENT_ONCE;
    record.source_tick = tick;
    record.source_time_us = static_cast<std::uint64_t>(tick) * 1000u;
    record.source_entity.index = WORR_EVENT_NO_ENTITY;
    record.subject_entity.index = WORR_EVENT_NO_ENTITY;
    record.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    record.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record.expiry_tick = tick + 1u;

    switch (presenter_kind) {
    case WORR_CGAME_NATIVE_EVENT_PROBE_KIND_LEGACY_TEMP: {
        worr_event_payload_legacy_temp_v1 payload{};
        payload.subtype = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
        CHECK(Worr_EventLegacyTempFieldMaskV1(
            payload.subtype, payload.raw_entity1,
            &payload.valid_fields));
        record.event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
        record.payload_kind = WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1;
        record.payload_size = sizeof(payload);
        std::memcpy(record.payload, &payload, sizeof(payload));
        break;
    }
    case WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE: {
        worr_event_payload_muzzle_v1 payload{};
        payload.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
        payload.flash_id = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
        record.event_type = WORR_EVENT_TYPE_WEAPON_FIRE;
        record.payload_kind = WORR_EVENT_PAYLOAD_MUZZLE_V1;
        record.payload_size = sizeof(payload);
        std::memcpy(record.payload, &payload, sizeof(payload));
        break;
    }
    case WORR_CGAME_NATIVE_EVENT_PROBE_KIND_SPATIAL_AUDIO: {
        worr_event_payload_spatial_audio_v1 payload{};
        payload.asset_id = 1u;
        payload.channel = 1u;
        payload.flags = WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL;
        payload.raw_entity = 1u;
        payload.volume = 1.0f;
        payload.attenuation = 1.0f;
        payload.pitch = 1.0f;
        record.event_type = WORR_EVENT_TYPE_AUDIO_CUE;
        record.payload_kind = WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1;
        record.payload_size = sizeof(payload);
        std::memcpy(record.payload, &payload, sizeof(payload));
        break;
    }
    case WORR_CGAME_NATIVE_EVENT_PROBE_KIND_DAMAGE: {
        worr_event_payload_damage_v1 payload{};
        payload.amount = 15.0f;
        payload.direction[0] = 1.0f;
        payload.damage_flags = WORR_EVENT_DAMAGE_FLAG_HEALTH;
        candidate.source_entity_index = 0u;
        candidate.subject_entity_index = 2u;
        record.event_type = WORR_EVENT_TYPE_DAMAGE;
        record.payload_kind = WORR_EVENT_PAYLOAD_DAMAGE;
        record.payload_size = sizeof(payload);
        std::memcpy(record.payload, &payload, sizeof(payload));
        break;
    }
    case WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI: {
        worr_event_payload_keyed_poi_v1 payload{};
        payload.key = 19u;
        payload.lifetime_ms = 2500u;
        payload.position[0] = 1.25f;
        payload.position[1] = -2.5f;
        payload.position[2] = 3.75f;
        payload.image_index = 4u;
        payload.color_index = 2u;
        payload.flags = WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM;
        candidate.source_entity_index = 0u;
        candidate.subject_entity_index = 2u;
        record.event_type = WORR_EVENT_TYPE_STATE_CHANGE;
        record.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
        record.expiry_tick = 0u;
        record.payload_kind = WORR_EVENT_PAYLOAD_KEYED_POI_V1;
        record.payload_size = sizeof(payload);
        std::memcpy(record.payload, &payload, sizeof(payload));
        break;
    }
    default:
        CHECK(false);
    }
    return candidate;
}

std::uint32_t carrier_for_kind(std::uint32_t presenter_kind)
{
    switch (presenter_kind) {
    case WORR_CGAME_NATIVE_EVENT_PROBE_KIND_LEGACY_TEMP:
        return WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2;
    case WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE:
        return WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2;
    case WORR_CGAME_NATIVE_EVENT_PROBE_KIND_SPATIAL_AUDIO:
        return WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2;
    case WORR_CGAME_NATIVE_EVENT_PROBE_KIND_DAMAGE:
        return WORR_CGAME_EVENT_CARRIER_DAMAGE_V2;
    case WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI:
        return WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2;
    default:
        CHECK(false);
        return 0;
    }
}

worr_cgame_event_range_build_result_v2 deliver_action(
    builder_storage_t &storage, std::uint32_t presenter_kind,
    std::uint32_t tick)
{
    const auto candidate = make_action_candidate(presenter_kind, tick);
    return Worr_CGameEventRangeDeliverActionV2(
        &storage.builder, &candidate, carrier_for_kind(presenter_kind), 0,
        consume_range, nullptr);
}

void test_order_copy_reset_and_empty()
{
    builder_storage_t storage{};
    initialize(storage, 7);

    cg_canonical_event_presentation_cursor_v1 empty_cursor{};
    CHECK(CG_CanonicalEventPresentationBegin(&empty_cursor) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);

    cg_canonical_event_presentation_cursor_v1 next{};
    cg_canonical_event_presentation_entry_v1 entry{};
    CHECK(CG_CanonicalEventPresentationNext(&empty_cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_EMPTY);

    const worr_cgame_event_carrier_v2 first[] = {
        {1, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, 0},
        {2, 0, 1},
    };
    CHECK(deliver_frame(storage, 100, UINT64_C(1000000), first, 2) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);

    std::uint32_t audit_advanced = UINT32_MAX;
    CHECK(CG_CanonicalEventPresentationAdvanceAudit(
              UINT64_C(999999), 16, &audit_advanced) ==
          CG_CANONICAL_EVENT_PRESENTATION_NOT_READY);
    CHECK(audit_advanced == 0);
    CHECK(CG_CanonicalEventPresentationAdvanceAudit(
              UINT64_C(1000000), 16, &audit_advanced) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(audit_advanced == 1);

    cg_canonical_event_presentation_cursor_v1 cursor{};
    CHECK(CG_CanonicalEventPresentationBegin(&cursor) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(entry.journal_serial == 1);
    CHECK(entry.arrival_ordinal == 1);
    CHECK(entry.carrier_tick == 100);
    CHECK(entry.carrier_kind == WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2);
    CHECK(entry.record.source_entity.index == 1);
    CHECK(entry.record.source_entity.generation == 1);
    CHECK(entry.semantic_hash != 0);

    worr_event_payload_legacy_entity_v1 payload{};
    std::memcpy(&payload, entry.record.payload, sizeof(payload));
    CHECK(payload.raw_event == WORR_EVENT_LEGACY_ENTITY_FOOTSTEP);

    /* Prove the cgame journal owns a copy rather than the builder scratch. */
    std::memset(storage.scratch.data(), 0xA5,
                sizeof(storage.scratch));
    cg_canonical_event_presentation_entry_v1 copied_again{};
    cg_canonical_event_presentation_cursor_v1 ignored{};
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &ignored,
                                             &copied_again) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(copied_again.semantic_hash == entry.semantic_hash);
    CHECK(std::memcmp(&copied_again.record, &entry.record,
                      sizeof(entry.record)) == 0);

    cursor = next;
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_EMPTY);
    CHECK(deliver_frame(storage, 100, UINT64_C(1000000), first, 2) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DUPLICATE_FRAME_V2);

    const worr_cgame_event_carrier_v2 second[] = {
        {1, WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN, 0},
        {2, WORR_EVENT_LEGACY_ENTITY_OTHER_FOOTSTEP, 1},
    };
    CHECK(deliver_frame(storage, 101, UINT64_C(1010000), second, 2) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(entry.journal_serial == 2 && entry.arrival_ordinal == 2);
    cursor = next;
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(entry.journal_serial == 3 && entry.arrival_ordinal == 3);
    CHECK(entry.record.source_ordinal == 1);

    const worr_cgame_event_carrier_v2 empty[] = {
        {1, 0, 0},
        {2, 0, 1},
    };
    CHECK(deliver_frame(storage, 102, UINT64_C(1020000), empty, 2) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(CG_CanonicalEventPresentationAdvanceAudit(
              UINT64_C(1020000), 16, &audit_advanced) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(audit_advanced == 2);

    cg_canonical_event_presentation_status_v1 status{};
    CHECK(CG_CanonicalEventPresentationGetStatus(&status));
    CHECK(status.accepted_records == 3);
    CHECK(status.retained_count == 3);
    CHECK(status.empty_carriers == 1);
    CHECK(status.audit_ready_records == 3);
    CHECK(status.audit_future_stalls == 1);
    CHECK(status.audit_last_ready_serial == 3);
    CHECK(status.range_audit.accepted_records == 3);

    CHECK(Worr_CGameEventRangeBuilderResetV2(&storage.builder, 8));
    consumer->Reset(8, WORR_CGAME_EVENT_SHADOW_RESET_DEMO_RESTART);
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_STALE_CURSOR);
    CHECK(CG_CanonicalEventPresentationGetStatus(&status));
    CHECK(status.stream_epoch == 8);
    CHECK(status.retained_count == 0);
    CHECK(status.reset_count == 2);
}

void test_invalid_range_and_overrun()
{
    builder_storage_t storage{};
    initialize(storage, 20);

    worr_cgame_event_range_v2 invalid{};
    invalid.struct_size = sizeof(invalid);
    invalid.api_version = WORR_CGAME_EVENT_RANGE_API_VERSION_V2;
    invalid.stream_epoch = 20;
    consumer->ConsumeCanonicalEventRange(&invalid);

    cg_canonical_event_presentation_status_v1 status{};
    CHECK(CG_CanonicalEventPresentationGetStatus(&status));
    CHECK(status.rejected_ranges == 1);
    CHECK(status.retained_count == 0);

    cg_canonical_event_presentation_cursor_v1 unread{};
    CHECK(CG_CanonicalEventPresentationTail(&unread) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);

    worr_cgame_event_carrier_v2 carrier{
        1, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, 0};
    for (std::uint32_t index = 0;
         index < CG_CANONICAL_EVENT_PRESENTATION_CAPACITY + 1u; ++index) {
        const std::uint32_t tick = 1000u + index;
        CHECK(deliver_frame(
                  storage, tick,
                  UINT64_C(5000000) + static_cast<std::uint64_t>(index) * 1000u,
                  &carrier, 1) ==
              WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    }

    cg_canonical_event_presentation_cursor_v1 next{};
    cg_canonical_event_presentation_entry_v1 entry{};
    CHECK(CG_CanonicalEventPresentationNext(&unread, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_CURSOR_OVERRUN);
    CHECK(CG_CanonicalEventPresentationGetStatus(&status));
    CHECK(status.retained_count == CG_CANONICAL_EVENT_PRESENTATION_CAPACITY);
    CHECK(status.overwritten_records == 1);
    CHECK(status.oldest_serial == 2);

    std::uint32_t audit_advanced = UINT32_MAX;
    CHECK(CG_CanonicalEventPresentationAdvanceAudit(
              UINT64_MAX, CG_CANONICAL_EVENT_PRESENTATION_CAPACITY,
              &audit_advanced) ==
          CG_CANONICAL_EVENT_PRESENTATION_CURSOR_OVERRUN);
    CHECK(audit_advanced == 0);
    CHECK(CG_CanonicalEventPresentationAdvanceAudit(
              UINT64_MAX, CG_CANONICAL_EVENT_PRESENTATION_CAPACITY,
              &audit_advanced) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(audit_advanced == CG_CANONICAL_EVENT_PRESENTATION_CAPACITY);
    CHECK(CG_CanonicalEventPresentationGetStatus(&status));
    CHECK(status.audit_overrun_recoveries == 1);
    CHECK(status.audit_ready_records ==
          CG_CANONICAL_EVENT_PRESENTATION_CAPACITY);

    CHECK(CG_CanonicalEventPresentationBegin(&unread) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(CG_CanonicalEventPresentationNext(&unread, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(entry.journal_serial == 2);
    CHECK(entry.carrier_tick == 1001);
}

void test_native_probe_raw_fifo_hash_overflow_and_reset()
{
    CHECK(sizeof(worr_cgame_native_event_probe_status_v1) == 336u);
    builder_storage_t storage{};
    CG_NativeEventProbeRawBeginMap();
    initialize(storage, 40u);

    const std::array<std::uint32_t, 5> kinds{
        WORR_CGAME_NATIVE_EVENT_PROBE_KIND_LEGACY_TEMP,
        WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE,
        WORR_CGAME_NATIVE_EVENT_PROBE_KIND_SPATIAL_AUDIO,
        WORR_CGAME_NATIVE_EVENT_PROBE_KIND_DAMAGE,
        WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI,
    };
    for (std::uint32_t index = 0; index < kinds.size(); ++index) {
        CHECK(deliver_action(storage, kinds[index], 100u + index) ==
              WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    }

    cg_canonical_event_presentation_cursor_v1 cursor{};
    CHECK(CG_CanonicalEventPresentationBegin(&cursor) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    std::uint64_t expected_action_hash = 0;
    std::array<std::uint64_t, 5> semantic_hashes{};
    for (std::uint32_t index = 0; index < kinds.size(); ++index) {
        cg_canonical_event_presentation_cursor_v1 next{};
        cg_canonical_event_presentation_entry_v1 entry{};
        CHECK(CG_CanonicalEventPresentationNext(
                  &cursor, &next, &entry) ==
              CG_CANONICAL_EVENT_PRESENTATION_OK);
        auto normalized = entry.record;
        normalized.source_time_us = 0;
        CHECK(Worr_EventRecordSemanticHashV1(
            &normalized, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
            &semantic_hashes[index]));
        expected_action_hash =
            Worr_CGameNativeEventProbeChainAppendV1(
                expected_action_hash, kinds[index], semantic_hashes[index]);
        cursor = next;
    }

    worr_cgame_native_event_probe_status_v1 status{};
    CHECK(CG_NativeEventProbeFillRawStatus(&status));
    CHECK(status.raw_action_records == 5u);
    CHECK(status.raw_pending_count == 5u);
    CHECK(status.raw_action_chain_hash == expected_action_hash);
    CHECK(status.raw_action_by_kind
              [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_LEGACY_TEMP] == 1u);
    CHECK(status.raw_action_by_kind
              [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE] == 1u);
    CHECK(status.raw_action_by_kind
              [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_SPATIAL_AUDIO] == 1u);
    CHECK(status.raw_action_by_kind
              [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_DAMAGE] == 1u);
    CHECK(status.raw_action_by_kind
              [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI] == 1u);
    CHECK(status.raw_effect_dispatches == 0u);
    CHECK(status.raw_effect_chain_hash == 0u);

    /* A wrong carrier or disposition cannot consume a later FIFO token. */
    CHECK(!CG_NativeEventProbeCompleteLegacyDispatch(
        WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2,
        WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));
    CHECK(!CG_NativeEventProbeCompleteLegacyDispatch(
        WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2, 99u));
    CHECK(CG_NativeEventProbeCompleteLegacyDispatch(
        WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2,
        WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));
    CHECK(CG_NativeEventProbeCompleteLegacyDispatch(
        WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2,
        WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_SUPPRESSED));
    CHECK(CG_NativeEventProbeCompleteLegacyDispatch(
        WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2,
        WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));
    CHECK(CG_NativeEventProbeCompleteLegacyDispatch(
        WORR_CGAME_EVENT_CARRIER_DAMAGE_V2,
        WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));
    CHECK(CG_NativeEventProbeCompleteLegacyDispatch(
        WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2,
        WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));
    CHECK(!CG_NativeEventProbeCompleteLegacyDispatch(
        WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2,
        WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));

    const std::uint64_t expected_effect_hash =
        Worr_CGameNativeEventProbeChainAppendV1(
            Worr_CGameNativeEventProbeChainAppendV1(
                Worr_CGameNativeEventProbeChainAppendV1(
                    0, kinds[0], semantic_hashes[0]),
                kinds[2], semantic_hashes[2]),
            kinds[3], semantic_hashes[3]);
    const std::uint64_t expected_effect_hash_with_poi =
        Worr_CGameNativeEventProbeChainAppendV1(
            expected_effect_hash, kinds[4], semantic_hashes[4]);
    CHECK(CG_NativeEventProbeFillRawStatus(&status));
    CHECK(status.raw_pending_count == 0u);
    CHECK(status.raw_effect_dispatches == 4u);
    CHECK(status.raw_effect_suppressions == 1u);
    CHECK(status.raw_effect_chain_hash == expected_effect_hash_with_poi);
    CHECK(status.raw_pair_failures == 3u);

    CG_NativeEventProbeRawEndMap();
    CHECK(CG_NativeEventProbeFillRawStatus(&status));
    CHECK(status.raw_action_records == 5u);
    CHECK(status.raw_effect_dispatches == 4u);
    CHECK(status.raw_pair_failures == 3u);

    /* The next map is a hard per-map reset. A full FIFO preserves its 512
     * retained heads, records overflow without overwriting, and never pairs
     * the unretained 513th action with an earlier token. */
    CG_NativeEventProbeRawBeginMap();
    initialize(storage, 41u);
    for (std::uint32_t index = 0;
         index < WORR_CGAME_NATIVE_EVENT_PROBE_RAW_PENDING_CAPACITY + 1u;
         ++index) {
        CHECK(deliver_action(
                  storage, WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE,
                  1000u + index) ==
              WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    }
    CHECK(CG_NativeEventProbeFillRawStatus(&status));
    CHECK(status.raw_action_records ==
          WORR_CGAME_NATIVE_EVENT_PROBE_RAW_PENDING_CAPACITY);
    CHECK(status.raw_pending_count ==
          WORR_CGAME_NATIVE_EVENT_PROBE_RAW_PENDING_CAPACITY);
    CHECK(status.raw_pair_failures == 1u);
    for (std::uint32_t index = 0;
         index < WORR_CGAME_NATIVE_EVENT_PROBE_RAW_PENDING_CAPACITY;
         ++index) {
        CHECK(CG_NativeEventProbeCompleteLegacyDispatch(
            WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2,
            WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));
    }
    CHECK(!CG_NativeEventProbeCompleteLegacyDispatch(
        WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2,
        WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));
    CHECK(CG_NativeEventProbeFillRawStatus(&status));
    CHECK(status.raw_pending_count == 0u);
    CHECK(status.raw_effect_dispatches ==
          WORR_CGAME_NATIVE_EVENT_PROBE_RAW_PENDING_CAPACITY);
    CHECK(status.raw_pair_failures == 2u);

    /* A stream reset invalidates and accounts every unmatched raw token while
     * retaining the ended-map evidence until the next BeginMap. */
    CG_NativeEventProbeRawBeginMap();
    initialize(storage, 42u);
    CHECK(deliver_action(
              storage, WORR_CGAME_NATIVE_EVENT_PROBE_KIND_LEGACY_TEMP,
              2000u) == WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(deliver_action(
              storage, WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE,
              2001u) == WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    consumer->Reset(43u, WORR_CGAME_EVENT_SHADOW_RESET_DEMO_RESTART);
    CHECK(CG_NativeEventProbeFillRawStatus(&status));
    CHECK(status.raw_pending_count == 0u);
    CHECK(status.raw_action_records == 2u);
    CHECK(status.raw_pair_failures == 2u);
    CG_NativeEventProbeRawEndMap();

    CG_NativeEventProbeRawBeginMap();
    CHECK(CG_NativeEventProbeFillRawStatus(&status));
    CHECK(status.raw_action_records == 0u);
    CHECK(status.raw_action_chain_hash == 0u);
    CHECK(status.raw_effect_dispatches == 0u);
    CHECK(status.raw_effect_chain_hash == 0u);
    CHECK(status.raw_pair_failures == 0u);
    CHECK(status.raw_pending_count == 0u);
    CG_NativeEventProbeRawEndMap();
    CG_NativeEventProbeRawUninstall();
}

void test_snapshot_bound_probe_parity_projection()
{
    builder_storage_t storage{};
    CG_NativeEventProbeRawBeginMap();
    initialize(storage, 60u);

    constexpr std::uint32_t tick = 300u;
    parity_snapshot = {};
    parity_snapshot.snapshot_id = {7u, tick + 1u};
    parity_snapshot.server_tick = tick;
    parity_snapshot.server_time_us = UINT64_C(9000000);
    parity_snapshot.snapshot_hash = UINT64_C(0x123456789abcdef0);
    parity_snapshot_available = true;
    CHECK(CG_NativeEventProbeRawCheckpointReady());
    CG_NativeEventProbeRawApplyCheckpoint();

    CHECK(deliver_action(
              storage, WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE,
              tick) == WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    worr_cgame_native_event_probe_status_v1 status{};
    CHECK(CG_NativeEventProbeFillRawStatus(&status));
    CHECK(status.raw_action_records == 1u &&
          status.raw_pending_count == 1u &&
          status.raw_pair_failures == 0u);

    cg_canonical_event_presentation_cursor_v1 cursor{};
    cg_canonical_event_presentation_cursor_v1 next{};
    cg_canonical_event_presentation_entry_v1 entry{};
    CHECK(CG_CanonicalEventPresentationBegin(&cursor) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);

    auto authority = entry.record;
    authority.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
                       WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    authority.event_id = {3u, 9u};
    authority.source_time_us = UINT64_C(42000000);
    cg_event_runtime_presentation_context_v1 context{};
    context.struct_size = sizeof(context);
    context.schema_version = CG_EVENT_RUNTIME_PRESENTER_VERSION;
    context.provenance = CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY;
    context.fence_snapshot_id = parity_snapshot.snapshot_id;
    context.fence_tick = parity_snapshot.server_tick;
    context.fence_time_us = parity_snapshot.server_time_us;

    std::uint64_t authority_hash = 0;
    CHECK(CG_NativeEventProbeAuthorityHash(
        &authority, &context, &authority_hash));
    CHECK(status.raw_action_chain_hash ==
          Worr_CGameNativeEventProbeChainAppendV1(
              0, WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE,
              authority_hash));

    auto changed = authority;
    changed.source_time_us += UINT64_C(999999);
    std::uint64_t changed_hash = 0;
    CHECK(CG_NativeEventProbeAuthorityHash(
              &changed, &context, &changed_hash) &&
          changed_hash == authority_hash);

    changed = authority;
    ++changed.source_ordinal;
    CHECK(CG_NativeEventProbeAuthorityHash(
              &changed, &context, &changed_hash) &&
          changed_hash != authority_hash);
    changed = authority;
    ++changed.source_entity.generation;
    CHECK(CG_NativeEventProbeAuthorityHash(
              &changed, &context, &changed_hash) &&
          changed_hash != authority_hash);
    changed = authority;
    auto muzzle = worr_event_payload_muzzle_v1{};
    std::memcpy(&muzzle, changed.payload, sizeof(muzzle));
    ++muzzle.flash_id;
    std::memcpy(changed.payload, &muzzle, sizeof(muzzle));
    CHECK(CG_NativeEventProbeAuthorityHash(
              &changed, &context, &changed_hash) &&
          changed_hash != authority_hash);

    auto wrong_context = context;
    ++wrong_context.fence_snapshot_id.sequence;
    CHECK(!CG_NativeEventProbeAuthorityHash(
        &authority, &wrong_context, &changed_hash));
    parity_snapshot.snapshot_hash ^= UINT64_C(0x0100000000000000);
    CHECK(CG_NativeEventProbeAuthorityHash(
              &authority, &context, &changed_hash) &&
          changed_hash != authority_hash);

    CHECK(CG_NativeEventProbeCompleteLegacyDispatch(
        WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2,
        WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));
    parity_snapshot_available = false;
    CG_NativeEventProbeRawEndMap();
    CG_NativeEventProbeRawUninstall();
}

void test_keyed_poi_projection_and_same_key_fifo()
{
    builder_storage_t storage{};
    CG_NativeEventProbeRawBeginMap();
    initialize(storage, 61u);

    constexpr std::uint32_t tick = 310u;
    parity_snapshot = {};
    parity_snapshot.snapshot_id = {9u, tick + 1u};
    parity_snapshot.server_tick = tick;
    parity_snapshot.server_time_us = UINT64_C(9100000);
    parity_snapshot.controlled_entity.identity = {2u, 1u};
    parity_snapshot.snapshot_hash = UINT64_C(0x223456789abcdef0);
    parity_snapshot_available = true;
    CHECK(CG_NativeEventProbeRawCheckpointReady());
    CG_NativeEventProbeRawApplyCheckpoint();

    auto first = make_action_candidate(
        WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI, tick);
    auto second = first;
    worr_event_payload_keyed_poi_v1 second_payload{};
    std::memcpy(&second_payload, second.record.payload,
                sizeof(second_payload));
    second_payload.position[0] += 100.0f;
    second_payload.color_index += 1u;
    std::memcpy(second.record.payload, &second_payload,
                sizeof(second_payload));

    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &storage.builder, &first,
              WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2, 0,
              consume_range, nullptr) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &storage.builder, &second,
              WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2, 0,
              consume_range, nullptr) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);

    worr_cgame_native_event_probe_status_v1 status{};
    CHECK(CG_NativeEventProbeFillRawStatus(&status));
    CHECK(status.raw_action_records == 2u &&
          status.raw_pending_count == 2u &&
          status.raw_action_by_kind[
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI] == 2u);

    constexpr std::uint32_t native_tick = tick + 1u;
    parity_snapshot.snapshot_id = {9u, native_tick + 1u};
    parity_snapshot.server_tick = native_tick;
    parity_snapshot.server_time_us = UINT64_C(9200000);
    parity_snapshot.controlled_entity.identity = {2u, 2u};
    parity_snapshot.snapshot_hash = UINT64_C(0x323456789abcdef0);

    cg_event_runtime_presentation_context_v1 context{};
    context.struct_size = sizeof(context);
    context.schema_version = CG_EVENT_RUNTIME_PRESENTER_VERSION;
    context.provenance = CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY;
    context.fence_snapshot_id = parity_snapshot.snapshot_id;
    context.fence_tick = parity_snapshot.server_tick;
    context.fence_time_us = parity_snapshot.server_time_us;

    cg_canonical_event_presentation_cursor_v1 cursor{};
    CHECK(CG_CanonicalEventPresentationBegin(&cursor) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    std::array<std::uint64_t, 2> chain_prefixes{};
    std::uint64_t expected_chain = 0;
    worr_event_record_v1 first_authority{};
    std::uint64_t first_authority_hash = 0;
    for (std::uint32_t index = 0; index < 2u; ++index) {
        cg_canonical_event_presentation_cursor_v1 next{};
        cg_canonical_event_presentation_entry_v1 entry{};
        CHECK(CG_CanonicalEventPresentationNext(
                  &cursor, &next, &entry) ==
              CG_CANONICAL_EVENT_PRESENTATION_OK);
        CHECK(entry.carrier_kind ==
                  WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2 &&
              entry.record.delivery_class ==
                  WORR_EVENT_DELIVERY_RELIABLE_ORDERED &&
              entry.record.expiry_tick == 0u);
        worr_event_payload_keyed_poi_v1 payload{};
        std::memcpy(&payload, entry.record.payload, sizeof(payload));
        CHECK(payload.key == second_payload.key);
        CHECK(payload.position[0] ==
              (index == 0u ? second_payload.position[0] - 100.0f
                           : second_payload.position[0]));

        auto authority = entry.record;
        authority.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
                           WORR_EVENT_FLAG_SNAPSHOT_FENCED;
        authority.event_id = {5u, index + 1u};
        authority.source_tick = native_tick;
        authority.source_time_us = UINT64_C(52000000) + index;
        authority.subject_entity = {2u, 2u};
        authority.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
        authority.expiry_tick = authority.source_tick + 1u;
        std::uint64_t authority_hash = 0;
        CHECK(CG_NativeEventProbeAuthorityHash(
            &authority, &context, &authority_hash));
        expected_chain = Worr_CGameNativeEventProbeChainAppendV1(
            expected_chain,
            WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI,
            authority_hash);
        if (index == 0u) {
            first_authority = authority;
            first_authority_hash = authority_hash;
        }
        chain_prefixes[index] = expected_chain;
        cursor = next;
    }
    CHECK(status.raw_action_chain_hash == chain_prefixes.back());

    auto changed_payload_record = first_authority;
    worr_event_payload_keyed_poi_v1 changed_payload{};
    std::memcpy(&changed_payload, changed_payload_record.payload,
                sizeof(changed_payload));
    ++changed_payload.image_index;
    std::memcpy(changed_payload_record.payload, &changed_payload,
                sizeof(changed_payload));
    std::uint64_t changed_hash = 0;
    CHECK(CG_NativeEventProbeAuthorityHash(
              &changed_payload_record, &context, &changed_hash) &&
          changed_hash != first_authority_hash);

    const auto native_snapshot = parity_snapshot;
    parity_snapshot.controlled_entity.identity.generation = 3u;
    CHECK(!CG_NativeEventProbeAuthorityHash(
        &first_authority, &context, &changed_hash));
    parity_snapshot = native_snapshot;

    auto other_epoch_context = context;
    other_epoch_context.fence_snapshot_id.epoch = 10u;
    parity_snapshot.snapshot_id.epoch = 10u;
    parity_snapshot.snapshot_hash ^= UINT64_C(0x0100000000000000);
    CHECK(CG_NativeEventProbeAuthorityHash(
              &first_authority, &other_epoch_context, &changed_hash) &&
          changed_hash != first_authority_hash);
    parity_snapshot = native_snapshot;

    for (std::uint32_t index = 0; index < 2u; ++index) {
        CHECK(CG_NativeEventProbeCompleteLegacyDispatch(
            WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2,
            WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));
        CHECK(CG_NativeEventProbeFillRawStatus(&status));
        CHECK(status.raw_pending_count == 1u - index &&
              status.raw_effect_dispatches == index + 1u &&
              status.raw_effect_chain_hash == chain_prefixes[index] &&
              status.raw_pair_failures == 0u);
    }

    parity_snapshot_available = false;
    CG_NativeEventProbeRawEndMap();
    CG_NativeEventProbeRawUninstall();
}

void test_damage_batch_probe_fifo_parity_order()
{
    builder_storage_t storage{};
    CG_NativeEventProbeRawBeginMap();
    initialize(storage, 70u);

    constexpr std::uint32_t tick = 400u;
    parity_snapshot = {};
    parity_snapshot.snapshot_id = {8u, tick + 1u};
    parity_snapshot.server_tick = tick;
    parity_snapshot.server_time_us = UINT64_C(10000000);
    parity_snapshot.snapshot_hash = UINT64_C(0x0fedcba987654321);
    parity_snapshot_available = true;
    CHECK(CG_NativeEventProbeRawCheckpointReady());
    CG_NativeEventProbeRawApplyCheckpoint();

    std::array<worr_cgame_event_action_candidate_v2,
               WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2>
        candidates{};
    for (std::uint32_t index = 0; index < candidates.size(); ++index) {
        candidates[index] = make_action_candidate(
            WORR_CGAME_NATIVE_EVENT_PROBE_KIND_DAMAGE, tick);
        candidates[index].record.source_ordinal = index;
        worr_event_payload_damage_v1 payload{};
        std::memcpy(
            &payload, candidates[index].record.payload, sizeof(payload));
        payload.amount = 20.0f + static_cast<float>(index);
        payload.direction[0] = index == 0u ? 1.0f : 0.0f;
        payload.direction[1] = index == 1u ? 1.0f : 0.0f;
        payload.direction[2] = index >= 2u ? 1.0f : 0.0f;
        std::memcpy(
            candidates[index].record.payload, &payload, sizeof(payload));
    }
    CHECK(Worr_CGameEventRangeDeliverActionBatchV2(
              &storage.builder, candidates.data(),
              static_cast<std::uint32_t>(candidates.size()),
              WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, 0,
              consume_range, nullptr) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);

    worr_cgame_native_event_probe_status_v1 status{};
    CHECK(CG_NativeEventProbeFillRawStatus(&status));
    CHECK(status.raw_action_records == candidates.size());
    CHECK(status.raw_pending_count == candidates.size());
    CHECK(status.raw_action_by_kind
              [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_DAMAGE] ==
          candidates.size());
    CHECK(status.raw_effect_dispatches == 0u &&
          status.raw_effect_chain_hash == 0u &&
          status.raw_effect_suppressions == 0u &&
          status.raw_pair_failures == 0u);

    cg_event_runtime_presentation_context_v1 context{};
    context.struct_size = sizeof(context);
    context.schema_version = CG_EVENT_RUNTIME_PRESENTER_VERSION;
    context.provenance = CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY;
    context.fence_snapshot_id = parity_snapshot.snapshot_id;
    context.fence_tick = parity_snapshot.server_tick;
    context.fence_time_us = parity_snapshot.server_time_us;

    cg_canonical_event_presentation_cursor_v1 cursor{};
    CHECK(CG_CanonicalEventPresentationBegin(&cursor) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    std::array<std::uint64_t, WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2>
        authority_hashes{};
    std::array<std::uint64_t, WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2>
        chain_prefixes{};
    std::uint64_t expected_chain = 0;
    for (std::uint32_t index = 0; index < candidates.size(); ++index) {
        cg_canonical_event_presentation_cursor_v1 next{};
        cg_canonical_event_presentation_entry_v1 entry{};
        CHECK(CG_CanonicalEventPresentationNext(
                  &cursor, &next, &entry) ==
              CG_CANONICAL_EVENT_PRESENTATION_OK);
        CHECK(entry.batch_generation == 1u &&
              entry.journal_serial == index + 1u &&
              entry.carrier_sequence == 1u &&
              entry.arrival_ordinal == index + 1u &&
              entry.carrier_tick == tick &&
              entry.carrier_kind == WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 &&
              entry.record.source_ordinal == index);
        worr_event_payload_damage_v1 payload{};
        std::memcpy(&payload, entry.record.payload, sizeof(payload));
        CHECK(payload.amount == 20.0f + static_cast<float>(index));

        auto authority = entry.record;
        authority.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
                           WORR_EVENT_FLAG_SNAPSHOT_FENCED;
        authority.event_id = {4u, index + 1u};
        authority.source_time_us = UINT64_C(50000000) + index;
        CHECK(CG_NativeEventProbeAuthorityHash(
            &authority, &context, &authority_hashes[index]));
        if (index > 0u)
            CHECK(authority_hashes[index] != authority_hashes[index - 1u]);
        expected_chain = Worr_CGameNativeEventProbeChainAppendV1(
            expected_chain, WORR_CGAME_NATIVE_EVENT_PROBE_KIND_DAMAGE,
            authority_hashes[index]);
        chain_prefixes[index] = expected_chain;
        cursor = next;
    }
    cg_canonical_event_presentation_cursor_v1 ignored{};
    cg_canonical_event_presentation_entry_v1 no_entry{};
    CHECK(CG_CanonicalEventPresentationNext(
              &cursor, &ignored, &no_entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_EMPTY);
    CHECK(status.raw_action_chain_hash == chain_prefixes.back());

    /* Each completion must consume the next damage token, not merely any
     * token with the same carrier kind. The effect-chain prefix makes the
     * four-record FIFO order observable after every completion. */
    for (std::uint32_t index = 0; index < candidates.size(); ++index) {
        CHECK(CG_NativeEventProbeCompleteLegacyDispatch(
            WORR_CGAME_EVENT_CARRIER_DAMAGE_V2,
            WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED));
        CHECK(CG_NativeEventProbeFillRawStatus(&status));
        CHECK(status.raw_pending_count == candidates.size() - index - 1u &&
              status.raw_effect_dispatches == index + 1u &&
              status.raw_effect_chain_hash == chain_prefixes[index] &&
              status.raw_effect_suppressions == 0u &&
              status.raw_pair_failures == 0u);
    }

    parity_snapshot_available = false;
    CG_NativeEventProbeRawEndMap();
    CG_NativeEventProbeRawUninstall();
}

} // namespace

int main()
{
    consumer = CG_GetEventRangeAPIv2();
    CHECK(consumer != nullptr);
    CHECK(consumer->struct_size == sizeof(*consumer));
    CHECK(consumer->api_version == WORR_CGAME_EVENT_RANGE_API_VERSION_V2);

    test_order_copy_reset_and_empty();
    test_invalid_range_and_overrun();
    test_native_probe_raw_fifo_hash_overflow_and_reset();
    test_snapshot_bound_probe_parity_projection();
    test_keyed_poi_projection_and_same_key_fifo();
    test_damage_batch_probe_fifo_parity_order();
    std::puts("cgame canonical event presentation tests passed");
    return 0;
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineFindSnapshot(
    worr_snapshot_id_v2 snapshot_id,
    worr_snapshot_timeline_ref_v1 *ref_out)
{
    if (!parity_snapshot_available || !ref_out ||
        snapshot_id.epoch != parity_snapshot.snapshot_id.epoch ||
        snapshot_id.sequence != parity_snapshot.snapshot_id.sequence) {
        return WORR_SNAPSHOT_TIMELINE_NOT_FOUND;
    }
    *ref_out = {0u, 1u};
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopySnapshot(
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_v2 *snapshot_out)
{
    if (!parity_snapshot_available || !snapshot_out || ref.slot != 0u ||
        ref.generation != 1u) {
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    }
    *snapshot_out = parity_snapshot;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

bool CG_CanonicalSnapshotTimelineGetDiagnostics(
    cg_canonical_snapshot_timeline_diagnostics_v1 *diagnostics_out)
{
    if (!diagnostics_out || !parity_snapshot_available)
        return false;
    *diagnostics_out = {};
    diagnostics_out->struct_size = sizeof(*diagnostics_out);
    diagnostics_out->initialized = 1u;
    diagnostics_out->active = 1u;
    diagnostics_out->active_epoch = parity_snapshot.snapshot_id.epoch;
    return true;
}
