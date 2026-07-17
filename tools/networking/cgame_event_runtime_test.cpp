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
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,    \
                         __LINE__, #condition);                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

constexpr std::uint32_t max_entities = 32;
constexpr std::uint32_t scratch_capacity = 8;

struct builder_storage_t {
    worr_cgame_event_range_builder_v2 builder{};
    std::array<worr_cgame_event_observed_v2, max_entities> observed{};
    std::array<std::uint32_t, max_entities> seen{};
    std::array<worr_event_record_v1, scratch_capacity> scratch{};
};

const worr_cgame_event_range_export_v2 *legacy_consumer;

cg_event_runtime_status_v1 status()
{
    cg_event_runtime_status_v1 result{};
    CHECK(CG_EventRuntimeGetStatus(&result));
    CHECK(result.struct_size == sizeof(result));
    CHECK(result.schema_version == CG_EVENT_RUNTIME_VERSION);
    return result;
}

cg_event_runtime_result_v1 advance(std::uint64_t render_time_us,
                                   std::uint32_t now_tick,
                                   std::uint32_t max_presentations,
                                   std::uint32_t expected_advanced)
{
    std::uint32_t advanced = UINT32_MAX;
    const auto result = CG_EventRuntimeAdvanceAudit(
        render_time_us, now_tick, max_presentations, &advanced);
    CHECK(advanced == expected_advanced);
    return result;
}

worr_event_record_v1 make_event(bool authoritative,
                                std::uint32_t authority_epoch,
                                std::uint32_t authority_sequence,
                                std::uint32_t source_ordinal,
                                std::uint32_t marker,
                                std::uint32_t source_tick,
                                std::uint64_t source_time_us,
                                std::uint8_t delivery_class,
                                std::uint8_t prediction_class)
{
    worr_event_record_v1 record{};
    const worr_event_payload_u32x4_v1 payload{
        {marker, marker ^ UINT32_C(0x55aa55aa), source_ordinal, 0},
    };
    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                   WORR_EVENT_FLAG_PRESENT_ONCE;
    if (authoritative) {
        record.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
        record.event_id = {authority_epoch, authority_sequence};
    }
    record.source_tick = source_tick;
    record.source_ordinal = source_ordinal;
    record.source_time_us = source_time_us;
    record.source_entity = {4, 1};
    record.subject_entity = {WORR_EVENT_NO_ENTITY, 0};
    record.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
    record.delivery_class = delivery_class;
    record.prediction_class = prediction_class;
    if (prediction_class != WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY) {
        record.prediction_key.command_epoch = 77;
        record.prediction_key.command_sequence = 1000 + source_ordinal;
        record.prediction_key.emitter_ordinal = source_ordinal;
        record.prediction_key.lane = WORR_EVENT_PREDICTION_LANE_GAMEPLAY;
    }
    if (delivery_class <= WORR_EVENT_DELIVERY_TRANSIENT)
        record.expiry_tick = source_tick + 4;
    record.payload_kind = WORR_EVENT_PAYLOAD_U32X4;
    record.payload_size = sizeof(payload);
    std::memcpy(record.payload, &payload, sizeof(payload));
    return record;
}

worr_event_record_v1 authorize(worr_event_record_v1 predicted,
                               std::uint32_t epoch,
                               std::uint32_t sequence)
{
    predicted.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
    predicted.event_id = {epoch, sequence};
    return predicted;
}

worr_event_record_v1 make_local_interaction_authority_receipt_event(
    std::uint32_t epoch, std::uint32_t sequence)
{
    worr_local_interaction_authority_receipt_v1 receipt{};
    worr_event_record_v1 record{};

    receipt.struct_size = sizeof(receipt);
    receipt.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
    receipt.command_id = {91, 17};
    receipt.command_hash = UINT64_C(0x1000000000000001);
    receipt.state_hash = UINT64_C(0x2000000000000001);
    receipt.transaction_hash = UINT64_C(0x3000000000000001);
    receipt.action_sequence = 1;
    receipt.state_flags = WORR_LOCAL_INTERACTION_STATE_HOOK_HELD;
    receipt.outcome_flags = WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED |
                            WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REJECTED;
    CHECK(Worr_LocalInteractionAuthorityReceiptValidateV1(&receipt));

    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_HAS_AUTHORITY_ID | WORR_EVENT_FLAG_CRITICAL;
    record.event_id = {epoch, sequence};
    record.source_tick = 700;
    record.source_time_us = UINT64_C(7000000);
    record.source_entity = {WORR_EVENT_NO_ENTITY, 0};
    record.subject_entity = {WORR_EVENT_NO_ENTITY, 0};
    record.event_type = WORR_EVENT_TYPE_AUTHORITY_RECEIPT;
    record.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    record.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record.payload_kind =
        WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1;
    record.payload_size = sizeof(receipt);
    std::memcpy(record.payload, &receipt, sizeof(receipt));
    CHECK(Worr_EventRecordValidateV1(
        &record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));
    return record;
}

std::uint64_t semantic_hash(const worr_event_record_v1 &record)
{
    std::uint64_t result = 0;
    CHECK(Worr_EventRecordSemanticHashV1(
        &record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2, &result));
    CHECK(result != 0);
    return result;
}

worr_snapshot_event_ref_v2 authority_ref(
    std::uint32_t carrier_ordinal,
    const worr_event_record_v1 &record)
{
    worr_snapshot_event_ref_v2 ref{};
    ref.struct_size = sizeof(ref);
    ref.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    ref.provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
    ref.carrier_ordinal = carrier_ordinal;
    ref.semantic_version = WORR_EVENT_MODEL_REVISION;
    ref.authority_id = record.event_id;
    ref.semantic_hash = semantic_hash(record);
    return ref;
}

worr_snapshot_event_ref_v2 legacy_ref(std::uint32_t carrier_ordinal,
                                      std::uint64_t hash)
{
    worr_snapshot_event_ref_v2 ref{};
    ref.struct_size = sizeof(ref);
    ref.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    ref.provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;
    ref.carrier_ordinal = carrier_ordinal;
    ref.semantic_version = WORR_EVENT_MODEL_REVISION;
    ref.semantic_hash = hash;
    return ref;
}

worr_snapshot_v2 make_snapshot(
    std::uint32_t epoch, std::uint32_t sequence,
    std::uint32_t server_tick, std::uint64_t server_time_us,
    const worr_snapshot_event_ref_v2 *refs, std::uint32_t count,
    worr_command_cursor_v1 consumed = {})
{
    worr_snapshot_v2 snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    snapshot.snapshot_id = {epoch, sequence};
    snapshot.server_tick = server_tick;
    snapshot.server_time_us = server_time_us;
    snapshot.consumed_command.cursor = consumed;
    snapshot.consumed_command.provenance =
        consumed.epoch != 0
            ? WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED
            : WORR_SNAPSHOT_CONSUMED_COMMAND_NONE;
    snapshot.event_range.count = count;
    CHECK(Worr_SnapshotEventRefsHashV2(refs, count,
                                       &snapshot.event_hash));
    return snapshot;
}

cg_event_runtime_result_v1 observe(
    std::uint32_t snapshot_epoch, std::uint32_t snapshot_sequence,
    std::uint32_t tick, std::uint64_t time_us,
    const worr_snapshot_event_ref_v2 *refs, std::uint32_t count)
{
    const auto snapshot = make_snapshot(
        snapshot_epoch, snapshot_sequence, tick, time_us, refs, count);
    return CG_EventRuntimeObserveSnapshot(&snapshot, refs, count);
}

void consume_legacy_range(void *, const worr_cgame_event_range_v2 *range)
{
    CHECK(legacy_consumer != nullptr);
    legacy_consumer->ConsumeCanonicalEventRange(range);
}

void initialize_legacy(builder_storage_t &storage, std::uint32_t epoch)
{
    storage = {};
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &storage.builder, storage.observed.data(), storage.seen.data(),
        static_cast<std::uint32_t>(storage.observed.size()),
        storage.scratch.data(),
        static_cast<std::uint32_t>(storage.scratch.size()), epoch));
    legacy_consumer->Reset(
        epoch, WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE);
    const auto current = status();
    CHECK(current.legacy_epoch == epoch);
}

worr_cgame_event_range_build_result_v2 deliver_frame(
    builder_storage_t &storage, std::uint32_t tick,
    std::uint64_t time_us, const worr_cgame_event_carrier_v2 *carriers,
    std::uint32_t count)
{
    return Worr_CGameEventRangeDeliverFrameV2(
        &storage.builder, tick, time_us, carriers, count, 0,
        consume_legacy_range, nullptr);
}

worr_event_record_v1 make_legacy_entity_record(
    std::uint32_t tick, std::uint64_t time_us,
    std::uint32_t source_ordinal, std::uint32_t entity_index,
    std::uint32_t generation, std::uint16_t raw_event)
{
    worr_event_record_v1 record{};
    worr_event_payload_legacy_entity_v1 payload{};
    payload.raw_event = raw_event;
    payload.flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                   WORR_EVENT_FLAG_PRESENT_ONCE;
    record.source_tick = tick;
    record.source_ordinal = source_ordinal;
    record.source_time_us = time_us;
    record.source_entity = {entity_index, generation};
    record.subject_entity = {WORR_EVENT_NO_ENTITY, 0};
    switch (raw_event) {
    case WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN:
        record.event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
        break;
    case WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT:
        record.event_type = WORR_EVENT_TYPE_STATE_CHANGE;
        payload.flags |= WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY;
        break;
    default:
        record.event_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
        break;
    }
    record.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    record.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record.expiry_tick = tick + 1;
    record.payload_kind = WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1;
    record.payload_size = sizeof(payload);
    std::memcpy(record.payload, &payload, sizeof(payload));
    CHECK(Worr_EventRecordCandidateValidateV1(
        &record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));
    return record;
}

worr_cgame_event_action_candidate_v2 make_temp_action(
    std::uint32_t tick, std::uint64_t time_us,
    std::uint32_t source_entity)
{
    worr_cgame_event_action_candidate_v2 candidate{};
    worr_event_payload_legacy_temp_v1 payload{};
    std::uint16_t fields = 0;
    payload.subtype = WORR_EVENT_LEGACY_TEMP_PARASITE_ATTACK;
    CHECK(Worr_EventLegacyTempFieldMaskV1(
        payload.subtype, static_cast<std::int16_t>(source_entity),
        &fields));
    payload.valid_fields = fields;
    payload.raw_entity1 = static_cast<std::int16_t>(source_entity);
    candidate.struct_size = sizeof(candidate);
    candidate.source_entity_index = source_entity;
    candidate.subject_entity_index = WORR_EVENT_NO_ENTITY;
    auto &record = candidate.record;
    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                   WORR_EVENT_FLAG_PRESENT_ONCE;
    record.source_tick = tick;
    record.source_time_us = time_us;
    record.source_entity = {WORR_EVENT_NO_ENTITY, 0};
    record.subject_entity = {WORR_EVENT_NO_ENTITY, 0};
    record.event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
    record.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    record.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record.expiry_tick = tick + 1;
    record.payload_kind = WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1;
    record.payload_size = sizeof(payload);
    std::memcpy(record.payload, &payload, sizeof(payload));
    return candidate;
}

void test_transactional_authority_and_ordered_release()
{
    constexpr std::uint32_t authority_epoch = 101;
    constexpr std::uint32_t snapshot_epoch = 201;
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    const auto first = make_event(
        true, authority_epoch, 1, 1, 0x1001, 11, 110000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    auto invalid = make_event(
        true, authority_epoch + 1, 2, 2, 0x1002, 12, 120000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    const std::array<worr_event_record_v1, 2> rejected{first, invalid};
    const auto before_rejected = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
              rejected.data(), static_cast<std::uint32_t>(rejected.size())) ==
          CG_EVENT_RUNTIME_WRONG_EPOCH);
    auto current = status();
    CHECK(current.authority_count == 0);
    CHECK(current.receipt.highest_contiguous == 0);
    CHECK(current.receipt.selective_mask == 0);
    CHECK(current.authoritative_records ==
          before_rejected.authoritative_records);
    CHECK(current.authoritative_batches ==
          before_rejected.authoritative_batches);

    const auto second = make_event(
        true, authority_epoch, 2, 2, 0x1002, 12, 120000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&second, 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.receipt.highest_contiguous == 0);
    CHECK(current.receipt.selective_mask == 2);

    auto second_reference = authority_ref(0, second);
    CHECK(observe(snapshot_epoch, 1, 12, 120000,
                  &second_reference, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(advance(120000, 12, 8, 0) == CG_EVENT_RUNTIME_NOT_READY);
    CHECK(status().next_authority_sequence == 1);

    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&first, 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.receipt.highest_contiguous == 2);
    CHECK(current.receipt.selective_mask == 0);

    auto first_reference = authority_ref(0, first);
    CHECK(observe(snapshot_epoch, 2, 13, 130000,
                  &first_reference, 1) == CG_EVENT_RUNTIME_OK);
    const auto before_present = status();
    CHECK(advance(130000, 13, 2, 2) == CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.next_authority_sequence == 3);
    CHECK(current.authoritative_presentations ==
          before_present.authoritative_presentations + 2);
    CHECK(current.authority_ref_body_joins ==
          before_present.authority_ref_body_joins);

    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&first, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    const auto duplicate_snapshot = make_snapshot(
        snapshot_epoch, 2, 13, 130000, &first_reference, 1);
    CHECK(CG_EventRuntimeObserveSnapshot(
              &duplicate_snapshot, &first_reference, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    const auto exactly_once = status();
    CHECK(advance(130000, 13, 8, 0) == CG_EVENT_RUNTIME_NOT_READY);
    current = status();
    CHECK(current.authoritative_presentations ==
          exactly_once.authoritative_presentations);
    CHECK(current.presentation_chain_hash ==
          exactly_once.presentation_chain_hash);
}

void test_reference_before_authority_body()
{
    constexpr std::uint32_t authority_epoch = 102;
    constexpr std::uint32_t snapshot_epoch = 202;
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    const auto event = make_event(
        true, authority_epoch, 1, 1, 0x2001, 20, 200000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    auto ref = authority_ref(0, event);
    const auto before = status();
    CHECK(observe(snapshot_epoch, 1, 20, 200000, &ref, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&event, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.authority_ref_body_joins ==
          before.authority_ref_body_joins + 1);
    CHECK(advance(200000, 20, 1, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&event, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    current = status();
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations + 1);
}

void run_prediction_case(std::uint32_t authority_epoch,
                         std::uint32_t snapshot_epoch,
                         bool authority_first,
                         bool mismatch,
                         bool present_before_reconciliation)
{
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    auto predicted = make_event(
        false, 0, 0, 1, 0x3000 + authority_epoch, 30, 300000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    auto authoritative = authorize(predicted, authority_epoch, 1);
    if (mismatch)
        authoritative.payload[0] ^= 1;
    auto ref = authority_ref(0, authoritative);
    const auto before = status();
    cg_event_runtime_result_v1 reconciliation = CG_EVENT_RUNTIME_OK;

    if (authority_first) {
        CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
                  &authoritative, 1) == CG_EVENT_RUNTIME_OK);
        CHECK(observe(snapshot_epoch, 1, 30, 300000, &ref, 1) ==
              CG_EVENT_RUNTIME_OK);
        if (present_before_reconciliation)
            CHECK(advance(300000, 30, 1, 1) == CG_EVENT_RUNTIME_OK);
        reconciliation = CG_EventRuntimeSubmitPredictedBatch(&predicted, 1);
        if (!present_before_reconciliation)
            CHECK(advance(300000, 30, 1, 1) == CG_EVENT_RUNTIME_OK);
        else
            CHECK(advance(300000, 30, 1, 0) ==
                  CG_EVENT_RUNTIME_NOT_READY);
    } else {
        CHECK(CG_EventRuntimeSubmitPredictedBatch(&predicted, 1) ==
              CG_EVENT_RUNTIME_OK);
        if (present_before_reconciliation)
            CHECK(advance(300000, 30, 1, 1) == CG_EVENT_RUNTIME_OK);
        reconciliation = CG_EventRuntimeSubmitAuthoritativeBatch(
            &authoritative, 1);
        CHECK(observe(snapshot_epoch, 1, 30, 300000, &ref, 1) ==
              CG_EVENT_RUNTIME_OK);
        if (present_before_reconciliation)
            CHECK(advance(300000, 30, 1, 0) ==
                  CG_EVENT_RUNTIME_OK);
        else
            CHECK(advance(300000, 30, 1, 1) == CG_EVENT_RUNTIME_OK);
    }

    const auto expected =
        mismatch
            ? (present_before_reconciliation
                   ? CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION
                   : CG_EVENT_RUNTIME_CORRECTED)
            : CG_EVENT_RUNTIME_MATCHED;
    CHECK(reconciliation == expected);
    const auto current = status();
    CHECK(current.next_authority_sequence == 2);
    if (present_before_reconciliation) {
        CHECK(current.predicted_presentations ==
              before.predicted_presentations +
                  (authority_first ? 0 : 1));
        CHECK(current.authoritative_presentations ==
              before.authoritative_presentations +
                  (authority_first ? 1 : 0));
        CHECK(current.authoritative_prediction_suppressions ==
              before.authoritative_prediction_suppressions +
                  (authority_first ? 0 : 1));
    } else {
        CHECK(current.predicted_presentations ==
              before.predicted_presentations);
        CHECK(current.authoritative_presentations ==
              before.authoritative_presentations + 1);
        CHECK(current.authoritative_prediction_suppressions ==
              before.authoritative_prediction_suppressions);
    }
    if (mismatch && present_before_reconciliation) {
        CHECK(current.prediction_late_corrections ==
              before.prediction_late_corrections + 1);
    } else if (mismatch) {
        CHECK(current.prediction_corrections ==
              before.prediction_corrections + 1);
    } else {
        CHECK(current.prediction_matches == before.prediction_matches + 1);
    }
}

void test_prediction_reconciliation_matrix()
{
    std::uint32_t authority_epoch = 110;
    std::uint32_t snapshot_epoch = 210;
    for (const bool authority_first : {false, true}) {
        for (const bool mismatch : {false, true}) {
            for (const bool presented : {false, true}) {
                run_prediction_case(authority_epoch++, snapshot_epoch++,
                                    authority_first, mismatch, presented);
            }
        }
    }
}

void test_cancel_expiry_and_reset_separation()
{
    CHECK(CG_EventRuntimeResetAuthority(130, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(230) == CG_EVENT_RUNTIME_OK);
    const auto predicted = make_event(
        false, 0, 0, 1, 0x4001, 40, 400000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    auto authoritative = authorize(predicted, 130, 1);
    const auto before_cancel = status();
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&predicted, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeCancelPrediction(&predicted.prediction_key) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(400000, 40, 1, 0) == CG_EVENT_RUNTIME_NOT_READY);
    CHECK(status().prediction_cancellations ==
          before_cancel.prediction_cancellations + 1);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&authoritative, 1) ==
          CG_EVENT_RUNTIME_MATCHED);
    auto ref = authority_ref(0, authoritative);
    CHECK(observe(230, 1, 40, 400000, &ref, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(advance(400000, 40, 1, 1) == CG_EVENT_RUNTIME_OK);

    CHECK(CG_EventRuntimeResetAuthority(131, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(231) == CG_EVENT_RUNTIME_OK);
    const auto expiring = make_event(
        false, 0, 0, 2, 0x4002, 50, 500000,
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    const auto expiring_authority = authorize(expiring, 131, 1);
    const auto before_expiry = status();
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&expiring, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(500000, expiring.expiry_tick, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    CHECK(status().prediction_expirations ==
          before_expiry.prediction_expirations + 1);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
              &expiring_authority, 1) == CG_EVENT_RUNTIME_MATCHED);
    ref = authority_ref(0, expiring_authority);
    CHECK(observe(231, 1, 50, 500000, &ref, 1) == CG_EVENT_RUNTIME_OK);
    const auto before_terminal = status();
    CHECK(advance(500000, expiring.expiry_tick, 1, 0) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.authoritative_terminal_skips ==
          before_terminal.authoritative_terminal_skips + 1);
    CHECK(current.next_authority_sequence == 2);

    CHECK(CG_EventRuntimeResetAuthority(132, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(232) == CG_EVENT_RUNTIME_OK);
    const auto survives_snapshot_reset = make_event(
        false, 0, 0, 3, 0x4003, 60, 600000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(
              &survives_snapshot_reset, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(233) == CG_EVENT_RUNTIME_OK);
    const auto before_survivor = status();
    CHECK(advance(600000, 60, 1, 1) == CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.authority_epoch == 132);
    CHECK(current.snapshot_epoch == 233);
    CHECK(current.predicted_presentations ==
          before_survivor.predicted_presentations + 1);

    CHECK(CG_EventRuntimeResetAuthority(133, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(234) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeSnapshotFenceHealthy(234));
    const auto survives_fence_reset = make_event(
        true, 133, 1, 1, 0x4004, 70, 700000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
              &survives_fence_reset, 1) == CG_EVENT_RUNTIME_OK);
    ref = authority_ref(0, survives_fence_reset);
    CHECK(observe(234, 1, 70, 700000, &ref, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(235) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetLegacy(0) == CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.authority_epoch == 133);
    CHECK(current.receipt.highest_contiguous == 1);
    CHECK(current.authority_count == 1);
    CHECK(current.authority_requires_resync == 1);
    CHECK(!CG_EventRuntimeSnapshotFenceHealthy(235));
    CHECK(advance(700000, 70, 1, 0) == CG_EVENT_RUNTIME_NOT_READY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
              &survives_fence_reset, 1) == CG_EVENT_RUNTIME_NOT_READY);

    /* A lost unresolved fence is a hard boundary. A new event epoch may
     * reuse the new snapshot timeline, but the old record cannot. */
    CHECK(CG_EventRuntimeResetAuthority(134, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeSnapshotFenceHealthy(235));
    const auto after_fence_resync = make_event(
        true, 134, 1, 1, 0x4004, 70, 700000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
              &after_fence_resync, 1) == CG_EVENT_RUNTIME_OK);
    ref = authority_ref(0, after_fence_resync);
    CHECK(observe(235, 1, 70, 700000, &ref, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(advance(700000, 70, 1, 1) == CG_EVENT_RUNTIME_OK);
}

void test_legacy_body_reference_orders_and_action_gating()
{
    builder_storage_t storage{};
    initialize_legacy(storage, 301);
    CHECK(CG_EventRuntimeResetSnapshot(301) == CG_EVENT_RUNTIME_OK);
    const auto authority_before_reset = status();
    CHECK(authority_before_reset.authority_epoch == 134);
    CHECK(authority_before_reset.next_authority_sequence == 2);

    /* The snapshot's dense event-ref ordinal is zero even though the event's
     * source ordinal is one because entity zero in scan order had no event. */
    const auto sparse_first_record = make_legacy_entity_record(
        100, 1000000, 1, 2, 1,
        WORR_EVENT_LEGACY_ENTITY_FOOTSTEP);
    auto inferred = legacy_ref(0, semantic_hash(sparse_first_record));
    const auto before = status();
    CHECK(observe(301, 1, 100, 1000000, &inferred, 1) ==
          CG_EVENT_RUNTIME_OK);
    const worr_cgame_event_carrier_v2 sparse_first[] = {
        {1, 0, 0},
        {2, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, 1},
    };
    CHECK(deliver_frame(storage, 100, 1000000, sparse_first, 2) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    auto current = status();
    CHECK(current.legacy_ref_before_body_joins ==
          before.legacy_ref_before_body_joins + 1);
    CHECK(advance(1000000, 100, 1, 1) ==
          CG_EVENT_RUNTIME_NOT_READY);

    const worr_cgame_event_carrier_v2 sparse_second[] = {
        {1, 0, 0},
        {2, 0, 1},
        {3, WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN, 2},
    };
    const auto sparse_second_record = make_legacy_entity_record(
        101, 1010000, 2, 3, 1,
        WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN);
    const auto before_body_first = status();
    CHECK(deliver_frame(storage, 101, 1010000, sparse_second, 3) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(advance(1010000, 101, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    inferred = legacy_ref(0, semantic_hash(sparse_second_record));
    CHECK(observe(301, 2, 101, 1010000, &inferred, 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.legacy_body_before_ref_joins ==
          before_body_first.legacy_body_before_ref_joins + 1);
    CHECK(advance(1010000, 101, 1, 1) ==
          CG_EVENT_RUNTIME_NOT_READY);
    CHECK(deliver_frame(storage, 101, 1010000, sparse_second, 3) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DUPLICATE_FRAME_V2);
    const auto after_two = status();
    CHECK(advance(1010000, 101, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    current = status();
    CHECK(current.legacy_entity_presentations ==
          after_two.legacy_entity_presentations);
    CHECK(current.legacy_entity_presentations ==
          before.legacy_entity_presentations + 2);

    auto action = make_temp_action(200, 2000000, 2);
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &storage.builder, &action,
              WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2, 0,
              consume_legacy_range, nullptr) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    const auto before_action = status();
    CHECK(advance(1999999, 200, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    CHECK(advance(2000000, 200, 1, 1) ==
          CG_EVENT_RUNTIME_NOT_READY);
    current = status();
    CHECK(current.legacy_action_presentations ==
          before_action.legacy_action_presentations + 1);

    action = make_temp_action(300, 3000000, 2);
    CHECK(Worr_CGameEventRangeDeliverActionV2(
              &storage.builder, &action,
              WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2, 0,
              consume_legacy_range, nullptr) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    const auto before_authority_reset = status();
    CHECK(CG_EventRuntimeResetAuthority(140, 1) == CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.legacy_epoch == 301);
    CHECK(current.legacy_body_count ==
          before_authority_reset.legacy_body_count);
    CHECK(advance(3000000, 300, 1, 1) ==
          CG_EVENT_RUNTIME_NOT_READY);
    CHECK(status().legacy_action_presentations ==
          before_authority_reset.legacy_action_presentations + 1);
}

void fill_prediction_journal_after_presented_slot(
    std::uint32_t marker_base, std::uint32_t source_tick,
    std::uint64_t source_time_us)
{
    std::array<worr_event_record_v1,
               CG_EVENT_RUNTIME_JOURNAL_CAPACITY - 1u> fillers{};
    for (std::uint32_t index = 0; index < fillers.size(); ++index) {
        fillers[index] = make_event(
            false, 0, 0, index + 2u, marker_base + index,
            source_tick, source_time_us,
            WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
            WORR_EVENT_PREDICTION_COMMAND_DEFERRED);
    }
    CHECK(CG_EventRuntimeSubmitPredictedBatch(
              fillers.data(), static_cast<std::uint32_t>(fillers.size())) ==
          CG_EVENT_RUNTIME_OK);
}

void run_presented_prediction_displacement_case(
    std::uint32_t authority_epoch, std::uint32_t snapshot_epoch,
    bool mismatch)
{
    CHECK(CG_EventRuntimeResetLegacy(0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    const auto predicted = make_event(
        false, 0, 0, 1, 0x6000 + authority_epoch, 10, 100000,
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    auto authoritative = authorize(predicted, authority_epoch, 1);
    if (mismatch)
        authoritative.payload[0] ^= 1;

    const auto before = status();
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&predicted, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(100000, 10, 1, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(advance(100000, predicted.expiry_tick, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    fill_prediction_journal_after_presented_slot(
        0x61000000u + authority_epoch, 20, 200000);

    /* With all 512 journal slots occupied, this transient authority can only
     * replace the expired slot that used to hold the presented prediction. */
    const auto displacer = make_event(
        true, authority_epoch, 2, 700, 0x6200 + authority_epoch,
        20, 200000, WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&displacer, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(200000, displacer.expiry_tick, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);

    const auto reconciliation =
        CG_EventRuntimeSubmitAuthoritativeBatch(&authoritative, 1);
    CHECK(reconciliation ==
          (mismatch ? CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION
                    : CG_EVENT_RUNTIME_MATCHED));
    auto ref = authority_ref(0, authoritative);
    CHECK(observe(snapshot_epoch, 1, 10, 100000, &ref, 1) ==
          CG_EVENT_RUNTIME_OK);
    const auto before_suppression = status();
    CHECK(advance(200000, displacer.expiry_tick, 1, 0) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.next_authority_sequence == 2);
    CHECK(current.authoritative_prediction_suppressions ==
          before_suppression.authoritative_prediction_suppressions + 1);
    CHECK(current.predicted_presentations ==
          before.predicted_presentations + 1);
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations);
    if (mismatch) {
        CHECK(current.prediction_late_corrections ==
              before.prediction_late_corrections + 1);
    } else {
        CHECK(current.prediction_matches == before.prediction_matches + 1);
    }
    const auto exactly_once = current;
    CHECK(advance(200000, displacer.expiry_tick, 1, 0) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.authoritative_prediction_suppressions ==
          exactly_once.authoritative_prediction_suppressions);
    CHECK(current.presentation_chain_hash ==
          exactly_once.presentation_chain_hash);
}

void test_presented_prediction_survives_slot_displacement()
{
    run_presented_prediction_displacement_case(160, 360, false);
    run_presented_prediction_displacement_case(161, 361, true);
}

void fill_authority_journal_after_presented_slot(
    std::uint32_t authority_epoch, std::uint32_t marker_base)
{
    std::array<worr_event_record_v1,
               CG_EVENT_RUNTIME_JOURNAL_CAPACITY - 1u> fillers{};
    for (std::uint32_t index = 0; index < fillers.size(); ++index) {
        const std::uint32_t sequence = index + 2u;
        fillers[index] = make_event(
            true, authority_epoch, sequence, sequence,
            marker_base + index, 100 + sequence,
            UINT64_C(1000000) + sequence,
            WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    }
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
              fillers.data(), static_cast<std::uint32_t>(fillers.size())) ==
          CG_EVENT_RUNTIME_OK);
}

void run_presented_authority_displacement_case(
    std::uint32_t authority_epoch, std::uint32_t snapshot_epoch,
    bool mismatch)
{
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    const auto predicted = make_event(
        false, 0, 0, 1, 0x7000 + authority_epoch, 30, 300000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    const auto authoritative = authorize(predicted, authority_epoch, 1);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&authoritative, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto ref = authority_ref(0, authoritative);
    CHECK(observe(snapshot_epoch, 1, 30, 300000, &ref, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(300000, 30, 1, 1) == CG_EVENT_RUNTIME_OK);
    fill_authority_journal_after_presented_slot(
        authority_epoch, 0x71000000u + authority_epoch);

    /* All other journal residents are unpresented reliable authority. */
    const auto displacer = make_event(
        true, authority_epoch, CG_EVENT_RUNTIME_JOURNAL_CAPACITY + 1u,
        900, 0x7200 + authority_epoch, 900, 900000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&displacer, 1) ==
          CG_EVENT_RUNTIME_OK);

    auto late_prediction = predicted;
    if (mismatch)
        late_prediction.payload[0] ^= 1;
    const auto before_late = status();
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&late_prediction, 1) ==
          (mismatch ? CG_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION
                    : CG_EVENT_RUNTIME_MATCHED));
    auto current = status();
    CHECK(current.predicted_presentations ==
          before_late.predicted_presentations);
    CHECK(current.authoritative_presentations ==
          before_late.authoritative_presentations);
    CHECK(current.presentation_chain_hash ==
          before_late.presentation_chain_hash);
    if (mismatch) {
        CHECK(current.prediction_late_corrections ==
              before_late.prediction_late_corrections + 1);
    } else {
        CHECK(current.prediction_matches ==
              before_late.prediction_matches + 1);
    }
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&late_prediction, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    const auto duplicate = status();
    CHECK(advance(UINT64_MAX, UINT32_MAX, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    current = status();
    CHECK(current.predicted_presentations ==
          duplicate.predicted_presentations);
    CHECK(current.presentation_chain_hash ==
          duplicate.presentation_chain_hash);
}

void test_presented_authority_survives_slot_displacement()
{
    run_presented_authority_displacement_case(162, 362, false);
    run_presented_authority_displacement_case(163, 363, true);
}

void test_source_fence_and_midstream_reset()
{
    CHECK(CG_EventRuntimeResetAuthority(164, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(364) == CG_EVENT_RUNTIME_OK);
    const auto future_source = make_event(
        true, 164, 1, 1, 0x8001, 50, 500000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&future_source, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto ref = authority_ref(0, future_source);
    CHECK(observe(364, 1, 40, 400000, &ref, 1) ==
          CG_EVENT_RUNTIME_OK);
    const auto before_fence = status();
    CHECK(advance(400000, 40, 1, 0) == CG_EVENT_RUNTIME_NOT_READY);
    CHECK(advance(500000, 49, 1, 0) == CG_EVENT_RUNTIME_NOT_READY);
    CHECK(advance(499999, 50, 1, 0) == CG_EVENT_RUNTIME_NOT_READY);
    CHECK(advance(500000, 50, 1, 1) == CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.future_time_stalls == before_fence.future_time_stalls + 3);
    CHECK(current.authoritative_presentations ==
          before_fence.authoritative_presentations + 1);

    constexpr std::uint32_t first_sequence = 400;
    CHECK(CG_EventRuntimeResetAuthority(165, first_sequence) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(365) == CG_EVENT_RUNTIME_OK);
    const auto sequence400 = make_event(
        true, 165, 400, 1, 0x8100, 60, 600000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    const auto sequence401 = make_event(
        true, 165, 401, 2, 0x8101, 61, 610000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    current = status();
    CHECK(current.next_authority_sequence == first_sequence);
    CHECK(current.receipt.highest_contiguous == first_sequence - 1u);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&sequence401, 1) ==
          CG_EVENT_RUNTIME_OK);
    ref = authority_ref(0, sequence401);
    CHECK(observe(365, 1, 61, 610000, &ref, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(610000, 61, 2, 0) == CG_EVENT_RUNTIME_NOT_READY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&sequence400, 1) ==
          CG_EVENT_RUNTIME_OK);
    ref = authority_ref(0, sequence400);
    CHECK(observe(365, 2, 62, 620000, &ref, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(620000, 62, 2, 2) == CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.next_authority_sequence == 402);
    CHECK(current.receipt.highest_contiguous == 401);
    CHECK(current.receipt.selective_mask == 0);
}

void test_reconciled_prediction_key_cannot_change_authority_id()
{
    CHECK(CG_EventRuntimeResetAuthority(166, 1) == CG_EVENT_RUNTIME_OK);
    const auto predicted = make_event(
        false, 0, 0, 1, 0x8200, 70, 700000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    const auto first = authorize(predicted, 166, 1);
    const auto second = authorize(predicted, 166, 2);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&predicted, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&first, 1) ==
          CG_EVENT_RUNTIME_MATCHED);
    const auto before_conflict = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&second, 1) ==
          CG_EVENT_RUNTIME_CONFLICT);
    const auto current = status();
    CHECK(current.degraded == 1);
    CHECK(current.authority_count == 1);
    CHECK(current.receipt.highest_contiguous == 1);
    CHECK(current.authoritative_records ==
          before_conflict.authoritative_records);
    CHECK(current.authoritative_conflicts ==
          before_conflict.authoritative_conflicts + 1);
}

void test_ref_before_authority_mismatch_retains_evidence()
{
    CHECK(CG_EventRuntimeResetAuthority(167, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(367) == CG_EVENT_RUNTIME_OK);
    const auto event = make_event(
        true, 167, 1, 1, 0x8300, 80, 800000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    auto bad_ref = authority_ref(0, event);
    bad_ref.semantic_hash ^= 1;
    CHECK(observe(367, 1, 80, 800000, &bad_ref, 1) ==
          CG_EVENT_RUNTIME_OK);
    const auto before_body = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&event, 1) ==
          CG_EVENT_RUNTIME_DEGRADED);
    auto current = status();
    CHECK(current.degraded == 1);
    CHECK(current.authority_count == before_body.authority_count + 1);
    CHECK(current.authoritative_records ==
          before_body.authoritative_records + 1);
    CHECK(current.receipt.highest_contiguous == 1);
    CHECK(current.reference_count == before_body.reference_count);
    CHECK(current.reference_conflicts ==
          before_body.reference_conflicts + 1);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&event, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    CHECK(advance(800000, 80, 1, 0) ==
          CG_EVENT_RUNTIME_DEGRADED);
    current = status();
    CHECK(current.authority_count == before_body.authority_count + 1);
    CHECK(current.receipt.highest_contiguous == 1);
}

void test_prediction_retirement_cursor_and_no_resurrection()
{
    CHECK(CG_EventRuntimeResetLegacy(0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(168, 1) == CG_EVENT_RUNTIME_OK);
    const auto canceled = make_event(
        false, 0, 0, 1, 0x8400, 90, 900000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    const auto before = status();
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&canceled, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeCancelPrediction(&canceled.prediction_key) ==
          CG_EVENT_RUNTIME_OK);

    /* The terminal tombstone remains the no-resurrection fence until the
     * authoritative consumed-command cursor makes reclamation safe. */
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&canceled, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    CHECK(advance(900000, 90, 1, 0) == CG_EVENT_RUNTIME_NOT_READY);
    auto current = status();
    CHECK(current.prediction_tombstone_count == 1);
    CHECK(current.predicted_presentations == before.predicted_presentations);

    const worr_command_cursor_v1 consumed{
        canceled.prediction_key.command_epoch,
        canceled.prediction_key.command_sequence,
    };
    const auto before_retire = status();
    CHECK(CG_EventRuntimeRetirePredictionsThrough(consumed) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.prediction_tombstone_count == 0);
    CHECK(current.prediction_tombstones_retired ==
          before_retire.prediction_tombstones_retired + 1);
    CHECK(current.prediction_retired_through.epoch == consumed.epoch);
    CHECK(current.prediction_retired_through.contiguous_sequence ==
          consumed.contiguous_sequence);

    const auto before_stale = current;
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&canceled, 1) ==
          CG_EVENT_RUNTIME_TERMINAL);
    current = status();
    CHECK(current.stale_prediction_rejections ==
          before_stale.stale_prediction_rejections + 1);
    CHECK(current.prediction_tombstone_count == 0);

    auto newer = make_event(
        false, 0, 0, 2, 0x8401, 91, 910000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    CHECK(newer.prediction_key.command_sequence ==
          consumed.contiguous_sequence + 1u);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&newer, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(910000, 91, 1, 1) == CG_EVENT_RUNTIME_OK);

    const auto before_duplicate_cursor = status();
    CHECK(CG_EventRuntimeRetirePredictionsThrough(consumed) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    current = status();
    CHECK(current.prediction_retire_calls ==
          before_duplicate_cursor.prediction_retire_calls + 1);
    const worr_command_cursor_v1 regression{
        consumed.epoch, consumed.contiguous_sequence - 1u};
    const auto before_regression = current;
    CHECK(CG_EventRuntimeRetirePredictionsThrough(regression) ==
          CG_EVENT_RUNTIME_CONFLICT);
    current = status();
    CHECK(current.degraded == 1);
    CHECK(current.prediction_retire_regressions ==
          before_regression.prediction_retire_regressions + 1);
    CHECK(current.prediction_retired_through.epoch == consumed.epoch);
    CHECK(current.prediction_retired_through.contiguous_sequence ==
          consumed.contiguous_sequence);
}

void test_snapshot_consumed_cursor_retires_prediction_history()
{
    CHECK(CG_EventRuntimeResetAuthority(173, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(373) == CG_EVENT_RUNTIME_OK);
    auto predicted = make_event(
        false, 0, 0, 1, 0x7102, 91, 910000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&predicted, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeCancelPrediction(&predicted.prediction_key) ==
          CG_EVENT_RUNTIME_OK);

    const auto before = status();
    const worr_command_cursor_v1 consumed{
        predicted.prediction_key.command_epoch,
        predicted.prediction_key.command_sequence,
    };
    const auto snapshot = make_snapshot(
        373, 1, 91, 910000, nullptr, 0, consumed);
    CHECK(CG_EventRuntimeObserveSnapshot(&snapshot, nullptr, 0) ==
          CG_EVENT_RUNTIME_EMPTY);
    const auto current = status();
    CHECK(current.prediction_tombstones_retired ==
          before.prediction_tombstones_retired + 1);
    CHECK(current.prediction_retired_through.epoch == consumed.epoch);
    CHECK(current.prediction_retired_through.contiguous_sequence ==
          consumed.contiguous_sequence);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&predicted, 1) ==
          CG_EVENT_RUNTIME_TERMINAL);
}

void test_retired_but_resident_prediction_can_still_be_canceled()
{
    CHECK(CG_EventRuntimeResetAuthority(174, 1) == CG_EVENT_RUNTIME_OK);
    const auto before = status();
    const auto pending = make_event(
        false, 0, 0, 1, 0x7103, 120, 1200000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&pending, 1) ==
          CG_EVENT_RUNTIME_OK);
    const worr_command_cursor_v1 consumed{
        pending.prediction_key.command_epoch,
        pending.prediction_key.command_sequence,
    };
    CHECK(CG_EventRuntimeRetirePredictionsThrough(consumed) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(status().prediction_tombstone_count == 1);

    /* The stale-key guard must not mask the still-resident slot. */
    CHECK(CG_EventRuntimeCancelPrediction(&pending.prediction_key) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(1300000, 130, 1, 0) == CG_EVENT_RUNTIME_NOT_READY);
    const auto before_sweep = status();
    CHECK(before_sweep.predicted_presentations ==
          before.predicted_presentations);
    CHECK(CG_EventRuntimeRetirePredictionsThrough(consumed) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    const auto after_sweep = status();
    CHECK(after_sweep.prediction_tombstone_count == 0);
    CHECK(after_sweep.prediction_tombstones_retired ==
          before_sweep.prediction_tombstones_retired + 1);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&pending, 1) ==
          CG_EVENT_RUNTIME_TERMINAL);
}

void test_authority_deactivation_scrubs_persistent_cgame_state()
{
    CHECK(CG_EventRuntimeResetLegacy(0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(175, 5) == CG_EVENT_RUNTIME_OK);
    const auto predicted = make_event(
        false, 0, 0, 1, 0x7104, 140, 1400000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&predicted, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(status().prediction_tombstone_count == 1);

    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    const auto cleared = status();
    CHECK(cleared.authority_epoch == 0);
    CHECK(cleared.next_authority_sequence == 0);
    CHECK(cleared.authority_count == 0);
    CHECK(cleared.prediction_tombstone_count == 0);
    CHECK(cleared.prediction_retired_through.epoch == 0);
    CHECK(cleared.receipt.stream_epoch == 0);
    CHECK(cleared.receipt.highest_contiguous == 0);
    CHECK(cleared.receipt.selective_mask == 0);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&predicted, 1) ==
          CG_EVENT_RUNTIME_UNINITIALIZED);
    CHECK(CG_EventRuntimeResetAuthority(0, 1) ==
          CG_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(CG_EventRuntimeResetAuthority(1, 0) ==
          CG_EVENT_RUNTIME_INVALID_ARGUMENT);
}

void run_unref_authority_expiration_case(
    std::uint32_t authority_epoch, std::uint32_t snapshot_epoch,
    std::uint8_t delivery_class)
{
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    const auto expiring = make_event(
        true, authority_epoch, 1, 1, 0x8500 + delivery_class,
        100, 1000000, delivery_class,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    const auto later = make_event(
        true, authority_epoch, 2, 2, 0x8510 + delivery_class,
        101, 1010000, WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&expiring, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&later, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto ref = authority_ref(0, later);
    CHECK(observe(snapshot_epoch, 1, 101, 1010000, &ref, 1) ==
          CG_EVENT_RUNTIME_OK);

    const auto before_expiry = status();
    CHECK(advance(1010000, expiring.expiry_tick, 1, 0) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.next_authority_sequence == 2);
    CHECK(current.authoritative_expirations ==
          before_expiry.authoritative_expirations + 1);
    CHECK(current.authoritative_terminal_skips ==
          before_expiry.authoritative_terminal_skips + 1);
    CHECK(current.authority_reference_stalls ==
          before_expiry.authority_reference_stalls);

    const auto before_later = current;
    CHECK(advance(1010000, expiring.expiry_tick, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.next_authority_sequence == 3);
    CHECK(current.authoritative_presentations ==
          before_later.authoritative_presentations + 1);
}

void test_unref_authority_expiration_does_not_stall_sequence()
{
    run_unref_authority_expiration_case(
        169, 369, WORR_EVENT_DELIVERY_TRANSIENT);
    run_unref_authority_expiration_case(
        170, 370, WORR_EVENT_DELIVERY_COSMETIC);
}

void test_reverse_reconciliation_binds_existing_authority_id()
{
    CHECK(CG_EventRuntimeResetAuthority(171, 1) == CG_EVENT_RUNTIME_OK);
    const auto predicted = make_event(
        false, 0, 0, 1, 0x8600, 110, 1100000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    const auto first_authority = authorize(predicted, 171, 1);
    const auto reused_id = authorize(predicted, 171, 2);

    /* Public reverse reconciliation finds the durable authority sidecar
     * before consulting the journal, then records that exact ID in the new
     * prediction tombstone. The journal-only fallback cannot be isolated via
     * the public API while the sidecar invariant is intact. */
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&first_authority, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&predicted, 1) ==
          CG_EVENT_RUNTIME_MATCHED);
    const auto before_conflict = status();
    CHECK(before_conflict.prediction_tombstone_count == 1);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&reused_id, 1) ==
          CG_EVENT_RUNTIME_CONFLICT);
    const auto current = status();
    CHECK(current.authority_count == 1);
    CHECK(current.receipt.highest_contiguous == 1);
    CHECK(current.authoritative_records ==
          before_conflict.authoritative_records);
    CHECK(current.authoritative_conflicts ==
          before_conflict.authoritative_conflicts + 1);
}

void test_strict_mismatch_degradation()
{
    CHECK(CG_EventRuntimeResetLegacy(0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(150, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(350) == CG_EVENT_RUNTIME_OK);
    const auto event = make_event(
        true, 150, 1, 1, 0x5001, 400, 4000000,
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&event, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto mismatched = authority_ref(0, event);
    mismatched.semantic_hash ^= 1;
    const auto before_authority = status();
    CHECK(observe(350, 1, 400, 4000000, &mismatched, 1) ==
          CG_EVENT_RUNTIME_DEGRADED);
    auto current = status();
    CHECK(current.degraded == 1);
    CHECK(current.reference_conflicts ==
          before_authority.reference_conflicts + 1);
    CHECK(current.authoritative_presentations ==
          before_authority.authoritative_presentations);
    CHECK(advance(4000000, 400, 1, 0) ==
          CG_EVENT_RUNTIME_DEGRADED);

    CHECK(CG_EventRuntimeResetAuthority(151, 1) == CG_EVENT_RUNTIME_OK);
    builder_storage_t storage{};
    initialize_legacy(storage, 302);
    CHECK(CG_EventRuntimeResetSnapshot(351) == CG_EVENT_RUNTIME_OK);
    const worr_cgame_event_carrier_v2 carriers[] = {
        {1, 0, 0},
        {2, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, 1},
    };
    const auto record = make_legacy_entity_record(
        500, 5000000, 1, 2, 1,
        WORR_EVENT_LEGACY_ENTITY_FOOTSTEP);
    CHECK(deliver_frame(storage, 500, 5000000, carriers, 2) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    auto bad_legacy_ref = legacy_ref(0, semantic_hash(record) ^ 1);
    const auto before_legacy = status();
    CHECK(observe(351, 1, 500, 5000000, &bad_legacy_ref, 1) ==
          CG_EVENT_RUNTIME_DEGRADED);
    current = status();
    CHECK(current.legacy_ref_body_mismatches ==
          before_legacy.legacy_ref_body_mismatches + 1);
    CHECK(advance(5000000, 500, 1, 0) ==
          CG_EVENT_RUNTIME_DEGRADED);
    CHECK(status().legacy_entity_presentations ==
          before_legacy.legacy_entity_presentations);

    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&event, 1) ==
          CG_EVENT_RUNTIME_WRONG_EPOCH);
}

void test_private_authority_receipt_bridge()
{
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetLegacy(0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(190, 1) == CG_EVENT_RUNTIME_OK);
    const auto record =
        make_local_interaction_authority_receipt_event(190, 1);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_OK);
    cg_local_interaction_shadow_status_v1 interaction{};
    CG_LocalInteractionGetStatus(&interaction);
    CHECK(interaction.authority_receipts == 1);
    CHECK(interaction.authority_unmatched == 1);
    CHECK(interaction.requires_resync == 0);
    const auto before_advance = status();
    CHECK(advance(7000000, 700, 1, 0) == CG_EVENT_RUNTIME_OK);
    const auto after_advance = status();
    CHECK(after_advance.authoritative_terminal_skips ==
          before_advance.authoritative_terminal_skips + 1);
    CHECK(after_advance.authoritative_presentations ==
          before_advance.authoritative_presentations);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    CG_LocalInteractionGetStatus(&interaction);
    CHECK(interaction.authority_duplicates == 1);
}

void test_local_interaction_resync_latches_runtime_health()
{
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(191, 1) == CG_EVENT_RUNTIME_OK);

    const auto record =
        make_local_interaction_authority_receipt_event(191, 1);
    worr_local_interaction_authority_receipt_v1 receipt{};
    std::memcpy(&receipt, record.payload, sizeof(receipt));
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&receipt) ==
          cg_local_interaction_receipt_result_v1::accepted_unmatched);
    auto conflicting_receipt = receipt;
    conflicting_receipt.state_hash ^= UINT64_C(1);
    CHECK(Worr_LocalInteractionAuthorityReceiptValidateV1(
        &conflicting_receipt));
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&conflicting_receipt) ==
          cg_local_interaction_receipt_result_v1::conflict);
    CHECK(CG_LocalInteractionRequiresResync());

    /* Prediction-side reconciliation may fail between carrier deliveries. It
     * still has to become visible through the normal runtime health bit before
     * any later native admission or presentation proceeds. */
    CG_EventRuntimeSynchronizeLocalInteractionHealth();
    const auto current = status();
    CHECK(current.authority_requires_resync == 1);
    CHECK(current.authority_degraded == 1);
    worr_cgame_event_runtime_status_v1 exported{};
    const auto *api = CG_GetEventRuntimeAPI();
    CHECK(api && api->GetStatus(&exported));
    CHECK((exported.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC) != 0);
    CHECK(advance(7100000, 710, 1, 0) == CG_EVENT_RUNTIME_NOT_READY);

    CHECK(CG_EventRuntimeResetAuthority(192, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(!CG_LocalInteractionRequiresResync());
    CHECK(status().authority_requires_resync == 0);
}

void test_legacy_snapshot_fence_is_audit_independent_and_bounded()
{
    constexpr std::uint32_t snapshot_epoch = 601;
    constexpr std::uint32_t references_per_snapshot = 512;
    constexpr std::uint32_t snapshot_count = 5;
    std::array<worr_snapshot_event_ref_v2,
               references_per_snapshot> refs{};

    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetLegacy(0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(0) == CG_EVENT_RUNTIME_OK);
    CG_EventRuntimeSetAuditEnabled(false);
    CHECK(!CG_EventRuntimeAuditEnabled());
    CHECK(CG_EventRuntimeResetLegacy(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    for (std::uint32_t index = 0; index < refs.size(); ++index) {
        refs[index] = legacy_ref(
            index, UINT64_C(0x6000000000000000) + index + 1u);
    }
    CHECK(Worr_SnapshotEventRefsValidateV2(
        refs.data(), static_cast<std::uint32_t>(refs.size())));

    const auto before = status();
    for (std::uint32_t sequence = 1;
         sequence <= snapshot_count; ++sequence) {
        CHECK(observe(
                  snapshot_epoch, sequence, 1000u + sequence,
                  UINT64_C(20000000) + sequence * UINT64_C(1000),
                  refs.data(),
                  static_cast<std::uint32_t>(refs.size())) ==
              CG_EVENT_RUNTIME_OK);
        CHECK(CG_EventRuntimeSnapshotFenceHealthy(snapshot_epoch));
    }

    auto current = status();
    CHECK(current.audit_enabled == 0);
    CHECK(current.snapshots_observed ==
          before.snapshots_observed + snapshot_count);
    CHECK(current.references_observed ==
          before.references_observed +
              snapshot_count * references_per_snapshot);
    CHECK(current.reference_capacity_failures ==
          before.reference_capacity_failures);
    CHECK(current.reference_count == 0);
    CHECK(current.legacy_body_count == 0);
    CHECK(advance(UINT64_C(21000000), 1005, 1, 0) ==
          CG_EVENT_RUNTIME_EMPTY);

    const auto last_snapshot = make_snapshot(
        snapshot_epoch, snapshot_count,
        1000u + snapshot_count,
        UINT64_C(20000000) +
            snapshot_count * UINT64_C(1000),
        refs.data(), static_cast<std::uint32_t>(refs.size()));
    const auto before_bad_hash = status();
    auto bad_hash_snapshot = last_snapshot;
    bad_hash_snapshot.event_hash ^= UINT64_C(1);
    CHECK(CG_EventRuntimeObserveSnapshot(
              &bad_hash_snapshot, refs.data(),
              static_cast<std::uint32_t>(refs.size())) ==
          CG_EVENT_RUNTIME_INVALID_ARGUMENT);
    current = status();
    CHECK(current.snapshot_rejections ==
          before_bad_hash.snapshot_rejections + 1u);
    CHECK(current.snapshots_observed ==
          before_bad_hash.snapshots_observed);
    CHECK(current.reference_count ==
          before_bad_hash.reference_count);

    /*
     * More than the complete fixed reference-table capacity was fenced above
     * without retaining audit-only join entries. Toggling or resetting the
     * legacy comparison domain cannot erase the exact snapshot chronology.
     */
    CG_EventRuntimeSetAuditEnabled(true);
    CHECK(CG_EventRuntimeObserveSnapshot(
              &last_snapshot, refs.data(),
              static_cast<std::uint32_t>(refs.size())) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    CHECK(CG_EventRuntimeResetLegacy(snapshot_epoch + 1u) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeObserveSnapshot(
              &last_snapshot, refs.data(),
              static_cast<std::uint32_t>(refs.size())) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    CHECK(CG_EventRuntimeSnapshotFenceHealthy(snapshot_epoch));

    constexpr std::uint32_t authority_snapshot_epoch =
        snapshot_epoch + 1u;
    CHECK(CG_EventRuntimeResetSnapshot(authority_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    const auto authority_record = make_event(
        true, 777, 1, 0, 0x7777, 2000, UINT64_C(22000000),
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    const auto missing_authority_ref =
        authority_ref(0, authority_record);
    const auto missing_authority_snapshot = make_snapshot(
        authority_snapshot_epoch, 1, 2000, UINT64_C(22000000),
        &missing_authority_ref, 1);
    const auto before_not_ready = status();
    CHECK(CG_EventRuntimeObserveSnapshot(
              &missing_authority_snapshot,
              &missing_authority_ref, 1) ==
          CG_EVENT_RUNTIME_NOT_READY);
    current = status();
    CHECK(current.snapshots_observed ==
          before_not_ready.snapshots_observed);
    CHECK(current.reference_count ==
          before_not_ready.reference_count);

    CHECK(CG_EventRuntimeResetSnapshot(0) == CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.reference_count == 0);
    CHECK(!CG_EventRuntimeSnapshotFenceHealthy(
        authority_snapshot_epoch));
}

} // namespace

int main()
{
    CG_EventRuntimeSetAuditEnabled(true);
    legacy_consumer = CG_GetEventRangeAPIv2();
    CHECK(legacy_consumer != nullptr);
    CHECK(legacy_consumer->struct_size == sizeof(*legacy_consumer));
    CHECK(legacy_consumer->api_version ==
          WORR_CGAME_EVENT_RANGE_API_VERSION_V2);

    test_transactional_authority_and_ordered_release();
    test_reference_before_authority_body();
    test_prediction_reconciliation_matrix();
    test_cancel_expiry_and_reset_separation();
    test_legacy_body_reference_orders_and_action_gating();
    test_presented_prediction_survives_slot_displacement();
    test_presented_authority_survives_slot_displacement();
    test_source_fence_and_midstream_reset();
    test_reconciled_prediction_key_cannot_change_authority_id();
    test_ref_before_authority_mismatch_retains_evidence();
    test_prediction_retirement_cursor_and_no_resurrection();
    test_snapshot_consumed_cursor_retires_prediction_history();
    test_retired_but_resident_prediction_can_still_be_canceled();
    test_authority_deactivation_scrubs_persistent_cgame_state();
    test_unref_authority_expiration_does_not_stall_sequence();
    test_reverse_reconciliation_binds_existing_authority_id();
    test_strict_mismatch_degradation();
    test_private_authority_receipt_bridge();
    test_local_interaction_resync_latches_runtime_health();
    test_legacy_snapshot_fence_is_audit_independent_and_bounded();
    std::puts("cgame event runtime tests passed");
    return 0;
}
