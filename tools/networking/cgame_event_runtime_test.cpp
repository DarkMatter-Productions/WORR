/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_event_runtime.hpp"
#include "cg_local_interaction.hpp"
#include "cg_canonical_snapshot_timeline.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* Production cgame reports receipt parity through cg_predict.cpp.  This
 * isolated runtime harness installs a counting observer without linking the
 * prediction/UI layer. */
std::uint64_t local_action_shadow_report_callbacks;
std::uint64_t replacement_local_action_shadow_report_callbacks;

struct private_reconciliation_reentry_probe_t {
    bool active;
    bool status_ok;
    bool checkpoint_ready;
    std::uint32_t guarded_callback_entries;
    std::uint32_t exact_resolver_calls;
    cg_event_runtime_status_v1 callback_status;
    cg_event_runtime_status_v1 checkpoint_status;
    cg_event_runtime_result_v1 reset_legacy_result;
    cg_event_runtime_result_v1 reset_authority_result;
    cg_event_runtime_result_v1 reset_snapshot_result;
    cg_event_runtime_result_v1 submit_authority_result;
    cg_event_runtime_result_v1 submit_prediction_result;
    cg_event_runtime_result_v1 cancel_prediction_result;
    cg_event_runtime_result_v1 retire_prediction_result;
    cg_event_runtime_result_v1 observe_snapshot_result;
    cg_event_runtime_result_v1 observe_legacy_result;
    cg_event_runtime_result_v1 advance_result;
    std::uint32_t reentry_advanced;
};

private_reconciliation_reentry_probe_t private_reentry_probe;

void replacement_local_action_shadow_report()
{
    ++replacement_local_action_shadow_report_callbacks;
}

void exercise_private_reconciliation_reentry()
{
    if (!private_reentry_probe.active)
        return;

    ++private_reentry_probe.guarded_callback_entries;
    private_reentry_probe.status_ok = CG_EventRuntimeGetStatus(
        &private_reentry_probe.callback_status);
    private_reentry_probe.checkpoint_status.struct_size = UINT32_MAX;
    private_reentry_probe.checkpoint_ready =
        CG_EventRuntimeCheckpointReady(
            private_reentry_probe.callback_status.authority_epoch,
            &private_reentry_probe.checkpoint_status);
    private_reentry_probe.reset_legacy_result =
        CG_EventRuntimeResetLegacy(7776u);
    private_reentry_probe.reset_authority_result =
        CG_EventRuntimeResetAuthority(7777u, 1u);
    private_reentry_probe.reset_snapshot_result =
        CG_EventRuntimeResetSnapshot(7778u);
    private_reentry_probe.submit_authority_result =
        CG_EventRuntimeSubmitAuthoritativeBatch(nullptr, 0);
    private_reentry_probe.submit_prediction_result =
        CG_EventRuntimeSubmitPredictedBatch(nullptr, 0);
    private_reentry_probe.cancel_prediction_result =
        CG_EventRuntimeCancelPrediction(nullptr);
    private_reentry_probe.retire_prediction_result =
        CG_EventRuntimeRetirePredictionsThrough({});
    private_reentry_probe.observe_snapshot_result =
        CG_EventRuntimeObserveSnapshot(nullptr, nullptr, 0);
    private_reentry_probe.observe_legacy_result =
        CG_EventRuntimeObserveLegacyEntry(nullptr);
    private_reentry_probe.reentry_advanced = UINT32_MAX;
    private_reentry_probe.advance_result = CG_EventRuntimeAdvanceAudit(
        1, 1, 1, &private_reentry_probe.reentry_advanced);
    CG_EventRuntimeSetAuditEnabled(!CG_EventRuntimeAuditEnabled());
    CG_EventRuntimeSetPresenter(nullptr, nullptr);
    CG_EventRuntimeSetLocalActionShadowReportCallback(
        &replacement_local_action_shadow_report);
    CG_EventRuntimeSynchronizeLocalInteractionHealth();
}

std::uint32_t resolve_private_reentry_command_by_id(
    worr_command_id_v1 command_id,
    worr_cgame_command_record_entry_v1 *entry_out)
{
    (void)command_id;
    ++private_reentry_probe.exact_resolver_calls;
    exercise_private_reconciliation_reentry();
    if (entry_out)
        *entry_out = {};
    return WORR_CGAME_COMMAND_RECORD_HISTORY_MISSING;
}

const worr_cgame_command_record_import_v2 private_reentry_command_import{
    sizeof(private_reentry_command_import),
    WORR_CGAME_COMMAND_RECORD_API_VERSION_V2,
    &resolve_private_reentry_command_by_id,
};

void CG_LocalActionShadowReportParity()
{
    ++local_action_shadow_report_callbacks;
    exercise_private_reconciliation_reentry();
}

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

struct presenter_probe_t {
    bool accept;
    bool reenter_can_present;
    bool reenter_present;
    std::uint32_t can_present_calls;
    std::uint32_t present_calls;
    std::uint64_t callback_serial;
    std::uint64_t can_present_serial;
    std::uint64_t present_serial;
    worr_event_record_v1 can_present_record;
    worr_event_record_v1 present_record;
    cg_event_runtime_presentation_context_v1 can_present_context;
    cg_event_runtime_presentation_context_v1 present_context;
    cg_event_runtime_status_v1 can_present_status;
    cg_event_runtime_status_v1 present_status;
    cg_event_runtime_status_v1 can_present_checkpoint_status;
    cg_event_runtime_status_v1 present_checkpoint_status;
    bool can_present_checkpoint_ready;
    bool present_checkpoint_ready;
    cg_event_runtime_result_v1 can_present_advance_result;
    cg_event_runtime_result_v1 can_present_reset_result;
    cg_event_runtime_result_v1 present_advance_result;
    cg_event_runtime_result_v1 present_reset_result;
    std::uint32_t can_present_reentry_advanced;
    std::uint32_t present_reentry_advanced;
};

presenter_probe_t presenter_probe;

void reset_presenter_probe(bool accept)
{
    presenter_probe = {};
    presenter_probe.accept = accept;
}

bool probe_can_present(
    const worr_event_record_v1 *record,
    const cg_event_runtime_presentation_context_v1 *context)
{
    CHECK(record != nullptr);
    CHECK(context != nullptr);
    ++presenter_probe.can_present_calls;
    presenter_probe.can_present_serial =
        ++presenter_probe.callback_serial;
    presenter_probe.can_present_record = *record;
    presenter_probe.can_present_context = *context;
    presenter_probe.can_present_status = status();
    presenter_probe.can_present_checkpoint_status.struct_size = UINT32_MAX;
    presenter_probe.can_present_checkpoint_ready =
        CG_EventRuntimeCheckpointReady(
            presenter_probe.can_present_status.authority_epoch,
            &presenter_probe.can_present_checkpoint_status);
    if (presenter_probe.reenter_can_present) {
        presenter_probe.can_present_reentry_advanced = UINT32_MAX;
        presenter_probe.can_present_advance_result =
            CG_EventRuntimeAdvanceAudit(
                context->fence_time_us, context->fence_tick, 1,
                &presenter_probe.can_present_reentry_advanced);
        presenter_probe.can_present_reset_result =
            CG_EventRuntimeResetAuthority(7777u, 1u);
        CG_EventRuntimeSetPresenter(nullptr, nullptr);
    }
    return presenter_probe.accept;
}

void probe_present(
    const worr_event_record_v1 *record,
    const cg_event_runtime_presentation_context_v1 *context)
{
    CHECK(record != nullptr);
    CHECK(context != nullptr);
    CHECK(presenter_probe.can_present_calls ==
          presenter_probe.present_calls + 1u);
    CHECK(std::memcmp(
              record, &presenter_probe.can_present_record,
              sizeof(*record)) == 0);
    CHECK(std::memcmp(
              context, &presenter_probe.can_present_context,
              sizeof(*context)) == 0);
    ++presenter_probe.present_calls;
    presenter_probe.present_serial =
        ++presenter_probe.callback_serial;
    presenter_probe.present_record = *record;
    presenter_probe.present_context = *context;
    presenter_probe.present_status = status();
    presenter_probe.present_checkpoint_status.struct_size = UINT32_MAX;
    presenter_probe.present_checkpoint_ready =
        CG_EventRuntimeCheckpointReady(
            presenter_probe.present_status.authority_epoch,
            &presenter_probe.present_checkpoint_status);
    if (presenter_probe.reenter_present) {
        presenter_probe.present_reentry_advanced = UINT32_MAX;
        presenter_probe.present_advance_result =
            CG_EventRuntimeAdvanceAudit(
                context->fence_time_us, context->fence_tick, 1,
                &presenter_probe.present_reentry_advanced);
        presenter_probe.present_reset_result =
            CG_EventRuntimeResetAuthority(8888u, 1u);
        CG_EventRuntimeSetPresenter(nullptr, nullptr);
    }
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

worr_event_record_v1 make_local_action_shadow_authority_receipt_event(
    std::uint32_t epoch, std::uint32_t sequence)
{
    worr_command_record_v1 command{};
    worr_local_action_observation_state_v1 before{};
    worr_local_action_observation_record_v1 observation{};
    worr_local_action_shadow_v1 shadow{};
    worr_local_action_shadow_authority_receipt_v1 receipt{};
    worr_event_record_v1 record{};

    command.struct_size = sizeof(command);
    command.schema_version = WORR_COMMAND_ABI_VERSION;
    command.command_id = {91, 19};
    command.sample_time_us = UINT64_C(304000);
    command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    command.command.struct_size = sizeof(command.command);
    command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.command.duration_ms = 16;
    command.command.buttons = 1;
    command.render_watermark.struct_size = sizeof(command.render_watermark);
    command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    CHECK(Worr_CommandRecordCanonicalizeV1(
        &command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));

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
    CHECK(Worr_LocalActionObservationBuildV1(
        0, &command, &before, &after, &observation));
    CHECK(Worr_LocalActionShadowBuildV1(
        WORR_LOCAL_ACTION_CATALOG_BLASTER, &observation, &shadow));
    CHECK(Worr_LocalActionShadowAuthorityReceiptBuildV1(&shadow, &receipt));

    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_HAS_AUTHORITY_ID | WORR_EVENT_FLAG_CRITICAL;
    record.event_id = {epoch, sequence};
    record.source_tick = 701;
    record.source_time_us = UINT64_C(7010000);
    record.source_entity = {WORR_EVENT_NO_ENTITY, 0};
    record.subject_entity = {WORR_EVENT_NO_ENTITY, 0};
    record.event_type = WORR_EVENT_TYPE_AUTHORITY_RECEIPT;
    record.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    record.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record.payload_kind =
        WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1;
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
    const worr_snapshot_event_ref_v2 *refs, std::uint32_t count,
    std::uint32_t snapshot_flags = 0)
{
    auto snapshot = make_snapshot(
        snapshot_epoch, snapshot_sequence, tick, time_us, refs, count);
    snapshot.flags = snapshot_flags;
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
    CHECK(current.next_authority_sequence == 3);
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
          CG_EVENT_RUNTIME_NOT_READY);
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

void test_declared_snapshot_fence_bridge_both_arrival_orders()
{
    constexpr std::uint32_t first_authority_epoch = 891;
    constexpr std::uint32_t first_snapshot_epoch = 991;
    constexpr std::uint32_t first_tick = 400;
    constexpr std::uint64_t first_source_time = UINT64_C(40000000);
    constexpr std::uint64_t first_fence_time = UINT64_C(4000000);

    CG_EventRuntimeSetPresenter(&probe_can_present, &probe_present);
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(first_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(first_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    /* Snapshot first: the body joins the retained immutable metadata during
     * authority admission without requiring an event-ref range entry. */
    constexpr std::uint32_t first_simulation_tick = 2400;
    CHECK(observe(first_snapshot_epoch, first_tick + 1u,
                  first_simulation_tick,
                  first_fence_time, nullptr, 0,
                  WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION) ==
          CG_EVENT_RUNTIME_EMPTY);
    auto first = make_event(
        true, first_authority_epoch, 1, 0, 0x8910, first_tick,
        first_source_time, WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    const auto first_unfenced = first;
    first.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    CHECK(Worr_EventRecordValidateV1(
        &first, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));
    CHECK(Worr_EventRecordSemanticallyEqualV1(
        &first, &first_unfenced,
        WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));
    CHECK(semantic_hash(first) == semantic_hash(first_unfenced));

    const auto before_first = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&first, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(status().authority_ref_body_joins ==
          before_first.authority_ref_body_joins + 1u);
    CHECK(advance(first_fence_time, first_simulation_tick, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(presenter_probe.present_calls == 1);
    CHECK(presenter_probe.present_context.fence_snapshot_id.epoch ==
          first_snapshot_epoch);
    CHECK(presenter_probe.present_context.fence_snapshot_id.sequence ==
          first_tick + 1u);
    CHECK(presenter_probe.present_context.fence_tick ==
          first_simulation_tick);
    CHECK(presenter_probe.present_context.fence_time_us == first_fence_time);

    constexpr std::uint32_t second_authority_epoch = 892;
    constexpr std::uint32_t second_snapshot_epoch = 992;
    constexpr std::uint32_t second_tick = 500;
    constexpr std::uint64_t second_source_time = UINT64_C(50000000);
    constexpr std::uint64_t second_fence_time = UINT64_C(5000000);
    CHECK(CG_EventRuntimeResetAuthority(second_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(second_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    reset_presenter_probe(true);

    /* Event first: admission is ACK-eligible but presentation remains fenced
     * until the exact future snapshot arrives and joins the pending body. */
    auto second = make_event(
        true, second_authority_epoch, 1, 0, 0x8920, second_tick,
        second_source_time, WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    second.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    const auto before_second = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&second, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(status().authority_ref_body_joins ==
          before_second.authority_ref_body_joins);
    constexpr std::uint32_t second_simulation_tick = 3000;
    CHECK(advance(second_fence_time, second_simulation_tick, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    CHECK(presenter_probe.can_present_calls == 0);
    CHECK(status().authoritative_expirations ==
          before_second.authoritative_expirations);
    CHECK(observe(second_snapshot_epoch, second_tick + 1u,
                  second_simulation_tick,
                  second_fence_time, nullptr, 0,
                  WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(status().authority_ref_body_joins ==
          before_second.authority_ref_body_joins + 1u);
    CHECK(advance(second_fence_time, second_simulation_tick, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(presenter_probe.present_calls == 1);
    CHECK(presenter_probe.present_context.fence_snapshot_id.epoch ==
          second_snapshot_epoch);
    CHECK(presenter_probe.present_context.fence_snapshot_id.sequence ==
          second_tick + 1u);
    CHECK(presenter_probe.present_context.fence_tick ==
          second_simulation_tick);
    CHECK(presenter_probe.present_context.fence_time_us == second_fence_time);
    CHECK(status().authoritative_expirations ==
          before_second.authoritative_expirations);
    CG_EventRuntimeSetPresenter(nullptr, nullptr);
}

void test_declared_snapshot_fenced_authority_ref_requires_exact_id()
{
    constexpr std::uint32_t body_first_authority_epoch = 901;
    constexpr std::uint32_t body_first_snapshot_epoch = 1001;
    constexpr std::uint32_t body_first_source_tick = 1300;
    CHECK(CG_EventRuntimeResetAuthority(body_first_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(body_first_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    auto body_first = make_event(
        true, body_first_authority_epoch, 1, 0, 0x9010,
        body_first_source_tick, UINT64_C(19000000),
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    body_first.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&body_first, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto body_first_ref = authority_ref(0, body_first);
    const auto before_body_first = status();
    /* The semantic ref is valid, but its snapshot is one ID before the
     * declared {epoch, source_tick+1} lineage. */
    CHECK(observe(body_first_snapshot_epoch, body_first_source_tick,
                  6000, UINT64_C(19001000), &body_first_ref, 1) ==
          CG_EVENT_RUNTIME_DEGRADED);
    auto current = status();
    CHECK(current.reference_conflicts ==
          before_body_first.reference_conflicts + 1u);
    CHECK(current.authority_ref_body_joins ==
          before_body_first.authority_ref_body_joins);
    CHECK(current.authority_requires_resync == 1);
    CHECK(current.authority_degraded == 1);

    constexpr std::uint32_t ref_first_authority_epoch = 902;
    constexpr std::uint32_t ref_first_snapshot_epoch = 1002;
    constexpr std::uint32_t ref_first_source_tick = 1400;
    CHECK(CG_EventRuntimeResetAuthority(ref_first_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(ref_first_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    auto ref_first = make_event(
        true, ref_first_authority_epoch, 1, 0, 0x9020,
        ref_first_source_tick, UINT64_C(20000000),
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    ref_first.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    auto ref_first_ref = authority_ref(0, ref_first);
    CHECK(observe(ref_first_snapshot_epoch, ref_first_source_tick,
                  7000, UINT64_C(20001000), &ref_first_ref, 1) ==
          CG_EVENT_RUNTIME_OK);
    const auto before_ref_first = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&ref_first, 1) ==
          CG_EVENT_RUNTIME_DEGRADED);
    current = status();
    CHECK(current.reference_conflicts ==
          before_ref_first.reference_conflicts + 1u);
    CHECK(current.authority_ref_body_joins ==
          before_ref_first.authority_ref_body_joins);
    CHECK(current.authority_requires_resync == 1);
    CHECK(current.authority_degraded == 1);

    constexpr std::uint32_t correct_authority_epoch = 903;
    constexpr std::uint32_t correct_snapshot_epoch = 1003;
    constexpr std::uint32_t correct_source_tick = 1500;
    constexpr std::uint32_t correct_render_tick = 8000;
    constexpr std::uint64_t correct_render_time = UINT64_C(21000000);
    CHECK(CG_EventRuntimeResetAuthority(correct_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(correct_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    auto correct = make_event(
        true, correct_authority_epoch, 1, 0, 0x9030,
        correct_source_tick, correct_render_time,
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    correct.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    auto correct_ref = authority_ref(0, correct);
    const auto before_correct = status();
    CHECK(observe(correct_snapshot_epoch, correct_source_tick + 1u,
                  correct_render_tick, correct_render_time,
                  &correct_ref, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&correct, 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.authority_ref_body_joins ==
          before_correct.authority_ref_body_joins + 1u);
    CHECK(current.reference_conflicts ==
          before_correct.reference_conflicts);
    CHECK(current.authority_requires_resync == 0);
    CHECK(advance(correct_render_time, correct_render_tick, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
}

void test_declared_snapshot_fenced_transient_expiry_identity()
{
    constexpr std::uint32_t authority_epoch = 895;
    constexpr std::uint32_t snapshot_epoch = 995;
    constexpr std::uint32_t source_wire_tick = 700;
    constexpr std::uint32_t source_render_tick = 1000;
    constexpr std::uint32_t deadline_render_tick = 9000;
    constexpr std::uint64_t source_time = UINT64_C(10000000);
    constexpr std::uint64_t deadline_time = UINT64_C(11000000);

    CG_EventRuntimeSetPresenter(&probe_can_present, &probe_present);
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(snapshot_epoch, source_wire_tick + 1u,
                  source_render_tick, source_time, nullptr, 0) ==
          CG_EVENT_RUNTIME_EMPTY);
    auto live = make_event(
        true, authority_epoch, 1, 0, 0x8950, source_wire_tick,
        source_time, WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    live.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    const auto before_live = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&live, 1) ==
          CG_EVENT_RUNTIME_OK);

    /* The exact wire-domain deadline maps to snapshot ID expiry_tick+1. Its
     * own server time is the render-domain deadline. The interpolation pair's
     * current endpoint may already be that snapshot while target time remains
     * between previous/current; that future endpoint tick must not expire the
     * event one render frame early. */
    CHECK(observe(snapshot_epoch, live.expiry_tick + 1u,
                  deadline_render_tick, deadline_time, nullptr, 0) ==
          CG_EVENT_RUNTIME_EMPTY);
    CHECK(advance(deadline_time - 1u, deadline_render_tick, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(presenter_probe.present_calls == 1);
    CHECK(current.authoritative_expirations ==
          before_live.authoritative_expirations);
    CHECK(current.authoritative_presentations ==
          before_live.authoritative_presentations + 1u);
    CHECK(current.next_authority_sequence == 2u);
    CHECK(current.authority_requires_resync == 0);

    /* Event-first arrival attaches both exact identities as their snapshots
     * arrive. Retaining the exact source proof keeps the present-once event
     * eligible at the deadline despite unrelated wire/render-tick cadences. */
    constexpr std::uint32_t expired_authority_epoch = 896;
    constexpr std::uint32_t expired_snapshot_epoch = 996;
    constexpr std::uint32_t expired_source_wire_tick = 800;
    constexpr std::uint32_t expired_source_render_tick = 2000;
    constexpr std::uint32_t expired_deadline_render_tick = 12000;
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(expired_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(expired_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    auto expired = make_event(
        true, expired_authority_epoch, 1, 0, 0x8960,
        expired_source_wire_tick, UINT64_C(12000000),
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    expired.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    const auto before_expired = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&expired, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(expired_snapshot_epoch,
                  expired_source_wire_tick + 1u,
                  expired_source_render_tick, UINT64_C(12000000),
                  nullptr, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(observe(expired_snapshot_epoch, expired.expiry_tick + 1u,
                  expired_deadline_render_tick, UINT64_C(13000000),
                  nullptr, 0) == CG_EVENT_RUNTIME_EMPTY);
    CHECK(advance(UINT64_C(13000000), expired_deadline_render_tick - 1u,
                  1, 1) == CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(presenter_probe.present_calls == 1);
    CHECK(current.authoritative_expirations ==
          before_expired.authoritative_expirations);
    CHECK(current.authoritative_terminal_skips ==
          before_expired.authoritative_terminal_skips);
    CHECK(current.authoritative_presentations ==
          before_expired.authoritative_presentations + 1u);
    CHECK(current.next_authority_sequence == 2u);
    CHECK(current.authority_requires_resync == 0);

    /* A modular render-tick wrap does not change the exact time deadline. */
    constexpr std::uint32_t render_wrap_authority_epoch = 900;
    constexpr std::uint32_t render_wrap_snapshot_epoch = 1000;
    constexpr std::uint32_t render_wrap_source_wire_tick = 900;
    constexpr std::uint32_t render_wrap_source_tick = UINT32_MAX - 5u;
    constexpr std::uint32_t render_wrap_deadline_tick = UINT32_MAX - 1u;
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(render_wrap_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(render_wrap_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(render_wrap_snapshot_epoch,
                  render_wrap_source_wire_tick + 1u,
                  render_wrap_source_tick, UINT64_C(13500000), nullptr,
                  0) == CG_EVENT_RUNTIME_EMPTY);
    auto render_wrap = make_event(
        true, render_wrap_authority_epoch, 1, 0, 0x9000,
        render_wrap_source_wire_tick, UINT64_C(13500000),
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    render_wrap.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    const auto before_render_wrap = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&render_wrap, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(render_wrap_snapshot_epoch,
                  render_wrap.expiry_tick + 1u,
                  render_wrap_deadline_tick, UINT64_C(14000000),
                  nullptr, 0) == CG_EVENT_RUNTIME_EMPTY);
    CHECK(advance(UINT64_C(14000000), 2u, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(presenter_probe.present_calls == 1);
    CHECK(current.authoritative_expirations ==
          before_render_wrap.authoritative_expirations);
    CHECK(current.authoritative_terminal_skips ==
          before_render_wrap.authoritative_terminal_skips);
    CHECK(current.authoritative_presentations ==
          before_render_wrap.authoritative_presentations + 1u);
    CHECK(current.next_authority_sequence == 2u);
    CHECK(current.authority_requires_resync == 0);

    /* A modular wire deadline belongs beyond this snapshot-ID epoch. It must
     * not be compared to, or synthesized from, the unrelated render tick. */
    constexpr std::uint32_t wrapping_authority_epoch = 897;
    constexpr std::uint32_t wrapping_snapshot_epoch = 997;
    constexpr std::uint32_t wrapping_source_tick = UINT32_MAX - 2u;
    constexpr std::uint32_t wrapping_render_tick = 4000;
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(wrapping_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(wrapping_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(wrapping_snapshot_epoch, wrapping_source_tick + 1u,
                  wrapping_render_tick, UINT64_C(14000000), nullptr,
                  0) == CG_EVENT_RUNTIME_EMPTY);
    auto wrapping = make_event(
        true, wrapping_authority_epoch, 1, 0, 0x8970,
        wrapping_source_tick, UINT64_C(14000000),
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    wrapping.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    CHECK(wrapping.expiry_tick == 1u);
    const auto before_wrapping = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&wrapping, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(UINT64_C(14000000), wrapping_render_tick, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(presenter_probe.present_calls == 1);
    CHECK(current.authoritative_expirations ==
          before_wrapping.authoritative_expirations);
    CHECK(current.authoritative_presentations ==
          before_wrapping.authoritative_presentations + 1u);
    CHECK(current.authority_requires_resync == 0);
    CG_EventRuntimeSetPresenter(nullptr, nullptr);
}

void test_declared_snapshot_fenced_transient_retained_source_lifetime()
{
    CG_EventRuntimeSetPresenter(&probe_can_present, &probe_present);

    /* Model delayed DATA: render has already sampled the exact deadline, but
     * the bounded snapshot mirror still retains both immutable identities.
     * The late body gets one ordered presentation, never a second one. */
    constexpr std::uint32_t retained_authority_epoch = 1895;
    constexpr std::uint32_t retained_snapshot_epoch = 1995;
    constexpr std::uint32_t retained_source_wire_tick = 1300;
    constexpr std::uint32_t retained_source_render_tick = 3000;
    constexpr std::uint32_t retained_deadline_render_tick = 3004;
    constexpr std::uint64_t retained_source_time = UINT64_C(30000000);
    constexpr std::uint64_t retained_deadline_time = UINT64_C(30016000);
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(retained_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(retained_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(retained_snapshot_epoch,
                  retained_source_wire_tick + 1u,
                  retained_source_render_tick, retained_source_time,
                  nullptr, 0) == CG_EVENT_RUNTIME_EMPTY);
    auto retained = make_event(
        true, retained_authority_epoch, 1, 0, 0x18950,
        retained_source_wire_tick, retained_source_time,
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    retained.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    CHECK(observe(retained_snapshot_epoch, retained.expiry_tick + 1u,
                  retained_deadline_render_tick,
                  retained_deadline_time, nullptr, 0) ==
          CG_EVENT_RUNTIME_EMPTY);
    CHECK(advance(retained_deadline_time,
                  retained_deadline_render_tick, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    const auto before_retained = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&retained, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advance(retained_deadline_time + 1u,
                  retained_deadline_render_tick, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(presenter_probe.present_calls == 1);
    CHECK(current.authoritative_expirations ==
          before_retained.authoritative_expirations);
    CHECK(current.authoritative_terminal_skips ==
          before_retained.authoritative_terminal_skips);
    CHECK(current.authoritative_presentations ==
          before_retained.authoritative_presentations + 1u);
    CHECK(current.next_authority_sequence == 2u);
    const auto after_retained = current;
    CHECK(advance(retained_deadline_time + 2u,
                  retained_deadline_render_tick, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    current = status();
    CHECK(presenter_probe.present_calls == 1);
    CHECK(current.authoritative_presentations ==
          after_retained.authoritative_presentations);

    /* Retaining the exact deadline in the authority sidecar does not grant
     * an unbounded lifetime. Fill the mirror and evict only its oldest entry,
     * the exact source fence; the still-unpresented transient then expires. */
    constexpr std::uint32_t evicted_authority_epoch = 1896;
    constexpr std::uint32_t evicted_snapshot_epoch = 1996;
    constexpr std::uint32_t evicted_source_wire_tick = 1400;
    constexpr std::uint32_t evicted_source_render_tick = 4000;
    constexpr std::uint32_t evicted_deadline_render_tick = 4004;
    constexpr std::uint64_t evicted_source_time = UINT64_C(31000000);
    constexpr std::uint64_t evicted_deadline_time = UINT64_C(31016000);
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(evicted_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(evicted_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(evicted_snapshot_epoch,
                  evicted_source_wire_tick + 1u,
                  evicted_source_render_tick, evicted_source_time,
                  nullptr, 0) == CG_EVENT_RUNTIME_EMPTY);
    auto evicted = make_event(
        true, evicted_authority_epoch, 1, 0, 0x18960,
        evicted_source_wire_tick, evicted_source_time,
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    evicted.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&evicted, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(evicted_snapshot_epoch, evicted.expiry_tick + 1u,
                  evicted_deadline_render_tick,
                  evicted_deadline_time, nullptr, 0) ==
          CG_EVENT_RUNTIME_EMPTY);
    const auto before_evicted = status();
    for (std::uint32_t offset = 1;
         offset < CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY;
         ++offset) {
        CHECK(observe(
                  evicted_snapshot_epoch,
                  evicted.expiry_tick + 1u + offset,
                  evicted_deadline_render_tick + offset,
                  evicted_deadline_time + offset, nullptr, 0) ==
              CG_EVENT_RUNTIME_EMPTY);
    }
    CHECK(advance(
              evicted_deadline_time +
                  CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY,
              evicted_deadline_render_tick +
                  CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY,
              1, 0) == CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(presenter_probe.present_calls == 0);
    CHECK(current.authoritative_expirations ==
          before_evicted.authoritative_expirations + 1u);
    CHECK(current.authoritative_terminal_skips ==
          before_evicted.authoritative_terminal_skips + 1u);
    CHECK(current.authoritative_presentations ==
          before_evicted.authoritative_presentations);
    CHECK(current.next_authority_sequence == 2u);

    /* Reset discards the retained proof beneath an unresolved authority and
     * requires a new authority epoch; the old event cannot present later. */
    constexpr std::uint32_t reset_authority_epoch = 1897;
    constexpr std::uint32_t reset_snapshot_epoch = 1997;
    constexpr std::uint32_t reset_source_wire_tick = 1500;
    constexpr std::uint32_t reset_source_render_tick = 5000;
    constexpr std::uint32_t reset_deadline_render_tick = 5004;
    constexpr std::uint64_t reset_source_time = UINT64_C(32000000);
    constexpr std::uint64_t reset_deadline_time = UINT64_C(32016000);
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(reset_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(reset_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(reset_snapshot_epoch, reset_source_wire_tick + 1u,
                  reset_source_render_tick, reset_source_time, nullptr,
                  0) == CG_EVENT_RUNTIME_EMPTY);
    auto reset_pending = make_event(
        true, reset_authority_epoch, 1, 0, 0x18970,
        reset_source_wire_tick, reset_source_time,
        WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    reset_pending.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&reset_pending, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(reset_snapshot_epoch, reset_pending.expiry_tick + 1u,
                  reset_deadline_render_tick, reset_deadline_time,
                  nullptr, 0) == CG_EVENT_RUNTIME_EMPTY);
    const auto before_reset = status();
    CHECK(CG_EventRuntimeResetSnapshot(reset_snapshot_epoch + 1u) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.authority_requires_resync == 1);
    CHECK(advance(reset_deadline_time + 1u,
                  reset_deadline_render_tick, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    current = status();
    CHECK(presenter_probe.present_calls == 0);
    CHECK(current.authoritative_presentations ==
          before_reset.authoritative_presentations);
    CHECK(current.next_authority_sequence == 1u);

    CG_EventRuntimeSetPresenter(nullptr, nullptr);
}

void test_declared_snapshot_fenced_missing_expiry_render_bound()
{
    constexpr std::uint32_t authority_epoch = 898;
    constexpr std::uint32_t snapshot_epoch = 998;
    constexpr std::uint32_t source_wire_tick = 1000;
    constexpr std::uint32_t source_render_tick = 10000;
    constexpr std::uint32_t crossing_render_tick = 20000;

    CG_EventRuntimeSetPresenter(&probe_can_present, &probe_present);
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(snapshot_epoch, source_wire_tick + 1u,
                  source_render_tick, UINT64_C(15000000), nullptr,
                  0) == CG_EVENT_RUNTIME_EMPTY);
    auto record = make_event(
        true, authority_epoch, 1, 0, 0x8980, source_wire_tick,
        UINT64_C(15000000), WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    record.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    const auto before = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_OK);

    /* Skip the exact deadline ID. Until render reaches a real later snapshot
     * time, neither a made-up deadline nor a potentially stale presentation
     * is allowed. The pair's current endpoint may already be that crossing
     * snapshot; target time remains the terminal decision. */
    CHECK(observe(snapshot_epoch, record.expiry_tick + 2u,
                  crossing_render_tick, UINT64_C(16000000), nullptr,
                  0) == CG_EVENT_RUNTIME_EMPTY);
    for (std::uint32_t offset = 1;
         offset <= CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY + 1u;
         ++offset) {
        CHECK(observe(snapshot_epoch,
                      record.expiry_tick + 2u + offset,
                      crossing_render_tick + 1000u + offset,
                      UINT64_C(16000000) + offset, nullptr, 0) ==
              CG_EVENT_RUNTIME_EMPTY);
    }
    CHECK(advance(UINT64_C(16000000) - 1u,
                  crossing_render_tick,
                  1, 0) == CG_EVENT_RUNTIME_NOT_READY);
    auto current = status();
    CHECK(presenter_probe.can_present_calls == 0);
    CHECK(presenter_probe.present_calls == 0);
    CHECK(current.authoritative_expirations ==
          before.authoritative_expirations);
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations);
    CHECK(current.next_authority_sequence == 1u);

    CHECK(advance(UINT64_C(16000000),
                  crossing_render_tick, 1, 0) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.authoritative_expirations ==
          before.authoritative_expirations + 1u);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips + 1u);
    CHECK(current.next_authority_sequence == 2u);
    CHECK(current.authority_requires_resync == 0);
    CG_EventRuntimeSetPresenter(nullptr, nullptr);
}

void test_delayed_snapshot_fenced_transient_batch_retains_source_proof()
{
    constexpr std::uint32_t authority_epoch = 1898;
    constexpr std::uint32_t snapshot_epoch = 1998;
    constexpr std::uint32_t source_wire_tick = 617;
    constexpr std::uint32_t source_render_tick = 617;
    constexpr std::uint32_t delayed_snapshot_gap = 42;
    constexpr std::uint64_t source_fence_time = UINT64_C(10000000);
    constexpr std::uint64_t producer_source_time = UINT64_C(16784000);
    constexpr std::uint64_t snapshot_interval_us = UINT64_C(16000);
    constexpr std::size_t record_count = 5;

    static_assert(CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY == 64u);
    static_assert(delayed_snapshot_gap <
                  CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY);

    CG_EventRuntimeSetPresenter(&probe_can_present, &probe_present);
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    std::array<worr_event_record_v1, record_count> records{};
    std::array<worr_snapshot_event_ref_v2, record_count> refs{};
    for (std::uint32_t index = 0; index < records.size(); ++index) {
        records[index] = make_event(
            true, authority_epoch, index + 1u, index,
            UINT32_C(0x189800) + index, source_wire_tick,
            producer_source_time, WORR_EVENT_DELIVERY_TRANSIENT,
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
        records[index].flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
        /* Match the one-snapshot lifetime of the direct schema-2
         * temp/temp/audio group exercised by the delayed-ACK probe. */
        records[index].expiry_tick = source_wire_tick + 1u;
        refs[index] = authority_ref(index, records[index]);
    }

    /* References arrive with the immutable source projection before the
     * direct body batch.  Forty-two newer 16 ms snapshots model the v88 hitch
     * and ACK delay while remaining inside the one-second retention bound. */
    CHECK(observe(snapshot_epoch, source_wire_tick + 1u,
                  source_render_tick, source_fence_time, refs.data(),
                  static_cast<std::uint32_t>(refs.size())) ==
          CG_EVENT_RUNTIME_OK);
    for (std::uint32_t offset = 1; offset <= delayed_snapshot_gap;
         ++offset) {
        CHECK(observe(snapshot_epoch,
                      source_wire_tick + 1u + offset,
                      source_render_tick + offset,
                      source_fence_time + snapshot_interval_us * offset,
                      nullptr, 0) == CG_EVENT_RUNTIME_EMPTY);
    }

    const auto before = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
              records.data(),
              static_cast<std::uint32_t>(records.size())) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.authority_ref_body_joins ==
          before.authority_ref_body_joins + record_count);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips);

    const auto delayed_render_time =
        source_fence_time +
        snapshot_interval_us * delayed_snapshot_gap;
    CHECK(advance(delayed_render_time,
                  source_render_tick + delayed_snapshot_gap,
                  static_cast<std::uint32_t>(record_count),
                  static_cast<std::uint32_t>(record_count)) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(presenter_probe.present_calls == record_count);
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations + record_count);
    CHECK(current.authoritative_expirations ==
          before.authoritative_expirations);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips);
    CHECK(current.next_authority_sequence == record_count + 1u);
    CHECK(current.authority_requires_resync == 0);
    CHECK(current.authority_degraded == 0);
    CHECK(advance(delayed_render_time,
                  source_render_tick + delayed_snapshot_gap,
                  static_cast<std::uint32_t>(record_count), 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    CHECK(presenter_probe.present_calls == record_count);
    CG_EventRuntimeSetPresenter(nullptr, nullptr);
}

void test_declared_snapshot_fenced_source_retention_bounds_lifetime()
{
    constexpr std::uint32_t authority_epoch = 899;
    constexpr std::uint32_t snapshot_epoch = 999;
    constexpr std::uint32_t source_wire_tick = 1200;
    constexpr std::uint32_t source_render_tick = 30000;
    constexpr std::uint32_t deadline_render_tick = 50000;
    constexpr std::uint64_t source_time = UINT64_C(17000000);
    constexpr std::uint64_t deadline_time = UINT64_C(18000000);

    CG_EventRuntimeSetPresenter(&probe_can_present, &probe_present);
    reset_presenter_probe(true);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(snapshot_epoch, source_wire_tick + 1u,
                  source_render_tick, source_time, nullptr, 0) ==
          CG_EVENT_RUNTIME_EMPTY);
    auto record = make_event(
        true, authority_epoch, 1, 0, 0x8990, source_wire_tick,
        source_time, WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    record.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    const auto before = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(snapshot_epoch, record.expiry_tick + 1u,
                  deadline_render_tick, deadline_time, nullptr, 0) ==
          CG_EVENT_RUNTIME_EMPTY);

    /* Cached sidecar identities do not outlive their bounded proof. After
     * both exact snapshots leave the mirror, fail closed even if a lagging
     * render time remains numerically before the cached deadline time. */
    for (std::uint32_t offset = 1;
         offset <= CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY + 1u;
         ++offset) {
        CHECK(observe(snapshot_epoch,
                      record.expiry_tick + 1u + offset,
                      deadline_render_tick + offset,
                      deadline_time + offset, nullptr, 0) ==
              CG_EVENT_RUNTIME_EMPTY);
    }
    CHECK(advance(deadline_time - 1u,
                  deadline_render_tick +
                      CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY + 1u,
                  1, 0) ==
          CG_EVENT_RUNTIME_OK);
    const auto current = status();
    CHECK(presenter_probe.present_calls == 0);
    CHECK(current.authoritative_expirations ==
          before.authoritative_expirations + 1u);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips + 1u);
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations);
    CHECK(current.next_authority_sequence == 2u);
    CHECK(current.authority_requires_resync == 0);
    CG_EventRuntimeSetPresenter(nullptr, nullptr);
}

void test_crossed_transient_fence_terminalizes_admitted_batch()
{
    constexpr std::uint32_t authority_epoch = 904;
    constexpr std::uint32_t snapshot_epoch = 1004;
    constexpr std::uint32_t source_tick = 1700;
    constexpr std::uint64_t source_time = UINT64_C(22000000);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    std::array<worr_event_record_v1, 2> records{
        make_event(
            true, authority_epoch, 1, 0, 0x9040, source_tick,
            source_time, WORR_EVENT_DELIVERY_TRANSIENT,
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY),
        make_event(
            true, authority_epoch, 2, 1, 0x9041, source_tick,
            source_time, WORR_EVENT_DELIVERY_TRANSIENT,
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY),
    };
    for (auto &record : records)
        record.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;

    /* The immutable source projection was skipped, and snapshot-ID order has
     * already reached the discardable records' translated deadline. */
    CHECK(observe(snapshot_epoch, records[0].expiry_tick + 1u,
                  source_tick + 10u, source_time + 100u,
                  nullptr, 0) == CG_EVENT_RUNTIME_EMPTY);
    const auto before = status();

    /* Admit sequence 2 first. It is terminalizable but cannot be drained
     * across the missing ordered head. A read-only checkpoint must treat the
     * SKIP-only resident as unsettled and report it without baselining the
     * terminal-skip counter that sequence 1 will make reachable. */
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&records[1], 1) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.authority_count == 1u);
    CHECK(current.next_authority_sequence == 1u);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips);
    cg_event_runtime_status_v1 unsettled{};
    unsettled.struct_size = UINT32_MAX;
    const auto unsettled_sentinel = unsettled;
    CHECK(!CG_EventRuntimeCheckpointReady(authority_epoch, &unsettled));
    CHECK(std::memcmp(&unsettled, &unsettled_sentinel,
                      sizeof(unsettled)) == 0);
    cg_event_runtime_checkpoint_block_v1 block{};
    CHECK(CG_EventRuntimeGetCheckpointBlock(authority_epoch, &block));
    CHECK(block.reason == CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_MISSING_HEAD);
    CHECK(block.pending_sequence == 2u);
    CHECK((block.authority_state & (1u << 2)) != 0u);

    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&records[0], 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.receipt.stream_epoch == authority_epoch);
    CHECK(current.receipt.highest_contiguous == 2u);
    CHECK(current.receipt.selective_mask == 0);
    CHECK(current.authority_count == 2u);
    CHECK(current.next_authority_sequence == 3u);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips + 2u);
    CHECK(current.authoritative_stale_or_coalesced ==
          before.authoritative_stale_or_coalesced + 2u);
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations);
    CHECK(current.authoritative_expirations ==
          before.authoritative_expirations);
    CHECK(current.reference_conflicts == before.reference_conflicts);
    CHECK(current.authority_ref_body_joins ==
          before.authority_ref_body_joins);
    CHECK(current.advance_calls == before.advance_calls);
    CHECK(current.authority_requires_resync == 0);
    CHECK(current.authority_degraded == 0);
    cg_event_runtime_status_v1 checkpoint{};
    CHECK(CG_EventRuntimeCheckpointReady(authority_epoch, &checkpoint));
    block = {};
    CHECK(!CG_EventRuntimeGetCheckpointBlock(authority_epoch, &block));
    CHECK(block.reason == CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_NONE);
}

void test_missing_transient_fence_waits_for_deadline_then_terminalizes()
{
    constexpr std::uint32_t authority_epoch = 905;
    constexpr std::uint32_t snapshot_epoch = 1005;
    constexpr std::uint32_t source_tick = 1800;
    constexpr std::uint64_t source_time = UINT64_C(23000000);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    auto record = make_event(
        true, authority_epoch, 1, 0, 0x9050, source_tick,
        source_time, WORR_EVENT_DELIVERY_TRANSIENT,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    record.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;

    /* Source identity is already irrecoverable, but the transient deadline
     * has not yet been crossed. Admission and receipt remain healthy while
     * the ordered presentation head waits for that terminal proof. */
    CHECK(observe(snapshot_epoch, source_tick + 2u,
                  source_tick + 1u, source_time + 1u,
                  nullptr, 0) == CG_EVENT_RUNTIME_EMPTY);
    const auto before = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.receipt.stream_epoch == authority_epoch);
    CHECK(current.receipt.highest_contiguous == 1u);
    CHECK(current.receipt.selective_mask == 0);
    CHECK(current.next_authority_sequence == 1u);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips);
    CHECK(current.authoritative_stale_or_coalesced ==
          before.authoritative_stale_or_coalesced);
    CHECK(current.advance_calls == before.advance_calls);
    CHECK(current.authority_requires_resync == 0);
    CHECK(current.authority_degraded == 0);

    CHECK(observe(snapshot_epoch, record.expiry_tick + 1u,
                  source_tick + 10u, source_time + 100u,
                  nullptr, 0) == CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.receipt.highest_contiguous == 1u);
    CHECK(current.receipt.selective_mask == 0);
    CHECK(current.next_authority_sequence == 2u);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips + 1u);
    CHECK(current.authoritative_stale_or_coalesced ==
          before.authoritative_stale_or_coalesced + 1u);
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations);
    CHECK(current.authoritative_expirations ==
          before.authoritative_expirations);
    CHECK(current.advance_calls == before.advance_calls);
    CHECK(current.reference_conflicts == before.reference_conflicts);
    CHECK(current.authority_requires_resync == 0);
    CHECK(current.authority_degraded == 0);
    cg_event_runtime_status_v1 checkpoint{};
    CHECK(CG_EventRuntimeCheckpointReady(authority_epoch, &checkpoint));
}

void test_declared_snapshot_fence_crossing_and_eviction_fail_closed()
{
    constexpr std::uint32_t authority_epoch = 893;
    constexpr std::uint32_t snapshot_epoch = 993;
    constexpr std::uint32_t source_tick = 600;
    constexpr std::uint64_t source_time = UINT64_C(6000000);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    /* The observed timeline has crossed the declared exact identity without
     * retaining it.  A source-clock value cannot substitute for that missing
     * immutable snapshot. */
    CHECK(observe(snapshot_epoch, source_tick + 2u, source_tick + 1u,
                  source_time + 1u, nullptr, 0) ==
          CG_EVENT_RUNTIME_EMPTY);
    auto crossed = make_event(
        true, authority_epoch, 1, 0, 0x8930, source_tick,
        source_time, WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    crossed.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    const auto before_mismatch = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&crossed, 1) ==
          CG_EVENT_RUNTIME_DEGRADED);
    auto current = status();
    CHECK(current.authority_requires_resync == 1);
    CHECK(current.authority_degraded == 1);
    CHECK(current.reference_conflicts ==
          before_mismatch.reference_conflicts + 1u);
    CHECK(current.authority_ref_body_joins ==
          before_mismatch.authority_ref_body_joins);

    constexpr std::uint32_t stale_authority_epoch = 894;
    constexpr std::uint32_t stale_snapshot_epoch = 994;
    CHECK(CG_EventRuntimeResetAuthority(stale_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(stale_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    for (std::uint32_t tick = 0;
         tick < CG_EVENT_RUNTIME_SNAPSHOT_FENCE_CAPACITY + 1u; ++tick) {
        CHECK(observe(stale_snapshot_epoch, tick + 1u, tick,
                      UINT64_C(7000000) + tick, nullptr, 0) ==
              CG_EVENT_RUNTIME_EMPTY);
    }
    auto evicted = make_event(
        true, stale_authority_epoch, 1, 0, 0x8940, 0,
        UINT64_C(7000000), WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    evicted.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    const auto before_evicted = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&evicted, 1) ==
          CG_EVENT_RUNTIME_DEGRADED);
    current = status();
    CHECK(current.authority_requires_resync == 1);
    CHECK(current.authority_degraded == 1);
    CHECK(current.reference_conflicts ==
          before_evicted.reference_conflicts + 1u);
    CHECK(current.authority_ref_body_joins ==
          before_evicted.authority_ref_body_joins);

    constexpr std::uint32_t persistent_authority_epoch = 906;
    constexpr std::uint32_t persistent_snapshot_epoch = 1006;
    constexpr std::uint32_t persistent_source_tick = 1900;
    CHECK(CG_EventRuntimeResetAuthority(persistent_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(persistent_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(observe(persistent_snapshot_epoch,
                  persistent_source_tick + 2u,
                  persistent_source_tick + 1u,
                  UINT64_C(24000001), nullptr, 0) ==
          CG_EVENT_RUNTIME_EMPTY);
    auto persistent = make_event(
        true, persistent_authority_epoch, 1, 0, 0x9060,
        persistent_source_tick, UINT64_C(24000000),
        WORR_EVENT_DELIVERY_PERSISTENT_STATE,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    persistent.flags |= WORR_EVENT_FLAG_SNAPSHOT_FENCED;
    const auto before_persistent = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&persistent, 1) ==
          CG_EVENT_RUNTIME_DEGRADED);
    current = status();
    CHECK(current.authority_requires_resync == 1);
    CHECK(current.authority_degraded == 1);
    CHECK(current.reference_conflicts ==
          before_persistent.reference_conflicts + 1u);
    CHECK(current.authority_ref_body_joins ==
          before_persistent.authority_ref_body_joins);
    CHECK(current.authoritative_terminal_skips ==
          before_persistent.authoritative_terminal_skips);
    CHECK(current.authoritative_stale_or_coalesced ==
          before_persistent.authoritative_stale_or_coalesced);
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
    const auto before_submit = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_OK);
    cg_local_interaction_shadow_status_v1 interaction{};
    CG_LocalInteractionGetStatus(&interaction);
    CHECK(interaction.authority_receipts == 1);
    CHECK(interaction.authority_unmatched == 1);
    CHECK(interaction.requires_resync == 0);
    const auto after_submit = status();
    CHECK(after_submit.next_authority_sequence == 2);
    CHECK(after_submit.next_private_reconciliation_sequence == 2);
    CHECK(after_submit.authoritative_terminal_skips ==
          before_submit.authoritative_terminal_skips + 1);
    CHECK(after_submit.authoritative_presentations ==
          before_submit.authoritative_presentations);
    CHECK(after_submit.advance_calls == before_submit.advance_calls);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    CG_LocalInteractionGetStatus(&interaction);
    CHECK(interaction.authority_duplicates == 0);
}

void test_private_local_action_shadow_receipt_bridge()
{
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(192, 1) == CG_EVENT_RUNTIME_OK);
    const auto record =
        make_local_action_shadow_authority_receipt_event(192, 1);
    const auto callbacks_before = local_action_shadow_report_callbacks;
    const auto before_submit = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(local_action_shadow_report_callbacks == callbacks_before + 1);
    cg_local_action_shadow_status_v1 action{};
    CG_LocalActionShadowGetStatus(&action);
    CHECK(action.authority_receipts == 1);
    CHECK(action.authority_unmatched == 1);
    CHECK(action.requires_resync == 0);
    const auto after_submit = status();
    CHECK(after_submit.next_authority_sequence == 2);
    CHECK(after_submit.next_private_reconciliation_sequence == 2);
    CHECK(after_submit.authoritative_terminal_skips ==
          before_submit.authoritative_terminal_skips + 1);
    CHECK(after_submit.authoritative_presentations ==
          before_submit.authoritative_presentations);
    CHECK(after_submit.advance_calls == before_submit.advance_calls);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    CHECK(local_action_shadow_report_callbacks == callbacks_before + 1);
    CG_LocalActionShadowGetStatus(&action);
    CHECK(action.authority_duplicates == 0);
}

void test_private_reconciliation_callback_reentry_is_guarded()
{
    constexpr std::uint32_t authority_epoch = 201;
    constexpr std::uint32_t legacy_epoch = 301;
    constexpr std::uint32_t snapshot_epoch = 401;
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CG_LocalInteractionSetImportV2(&private_reentry_command_import);
    CHECK(CG_EventRuntimeResetLegacy(legacy_epoch) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CG_EventRuntimeSetAuditEnabled(false);

    const auto record =
        make_local_action_shadow_authority_receipt_event(
            authority_epoch, 1);
    const auto callbacks_before = local_action_shadow_report_callbacks;
    const auto replacement_before =
        replacement_local_action_shadow_report_callbacks;
    private_reentry_probe = {};
    private_reentry_probe.active = true;
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_OK);
    private_reentry_probe.active = false;

    CHECK(private_reentry_probe.status_ok);
    CHECK(!private_reentry_probe.checkpoint_ready);
    CHECK(private_reentry_probe.checkpoint_status.struct_size ==
          UINT32_MAX);
    CHECK(private_reentry_probe.guarded_callback_entries == 2);
    CHECK(private_reentry_probe.exact_resolver_calls == 1);
    CHECK(private_reentry_probe.callback_status.authority_epoch ==
          authority_epoch);
    CHECK(private_reentry_probe.callback_status.next_authority_sequence == 1);
    CHECK(private_reentry_probe.callback_status
              .next_private_reconciliation_sequence == 1);
    CHECK(private_reentry_probe.callback_status.authority_count == 1);
    CHECK(private_reentry_probe.callback_status.receipt.highest_contiguous ==
          1);
    CHECK(private_reentry_probe.reset_legacy_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(private_reentry_probe.reset_authority_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(private_reentry_probe.reset_snapshot_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(private_reentry_probe.submit_authority_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(private_reentry_probe.submit_prediction_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(private_reentry_probe.cancel_prediction_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(private_reentry_probe.retire_prediction_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(private_reentry_probe.observe_snapshot_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(private_reentry_probe.observe_legacy_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(private_reentry_probe.advance_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(private_reentry_probe.reentry_advanced == UINT32_MAX);
    CHECK(!CG_EventRuntimeAuditEnabled());

    auto current = status();
    CHECK(current.legacy_epoch == legacy_epoch);
    CHECK(current.snapshot_epoch == snapshot_epoch);
    CHECK(current.authority_epoch == authority_epoch);
    CHECK(current.next_authority_sequence == 2);
    CHECK(current.next_private_reconciliation_sequence == 2);
    CHECK(current.authority_sequence_exhausted == 0);
    CHECK(current.private_reconciliation_sequence_exhausted == 0);
    CHECK(current.authority_requires_resync == 0);
    CHECK(local_action_shadow_report_callbacks == callbacks_before + 1);
    CHECK(replacement_local_action_shadow_report_callbacks ==
          replacement_before);

    /* The attempted callback replacement was also a mutation and must have
     * been ignored. A distinct event ID carrying the same idempotent receipt
     * therefore still reaches the original observer exactly once. */
    auto second = record;
    second.event_id.sequence = 2;
    CHECK(Worr_EventRecordValidateV1(
        &second, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&second, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(local_action_shadow_report_callbacks == callbacks_before + 2);
    CHECK(replacement_local_action_shadow_report_callbacks ==
          replacement_before);
    current = status();
    CHECK(current.next_authority_sequence == 3);
    CHECK(current.next_private_reconciliation_sequence == 3);
    CHECK(current.authority_requires_resync == 0);
    CHECK(private_reentry_probe.exact_resolver_calls == 1);
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CG_LocalInteractionSetImportV2(nullptr);
}

void test_private_receipt_consumes_uint32_max_once()
{
    constexpr std::uint32_t authority_epoch = 202;
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, UINT32_MAX) ==
          CG_EVENT_RUNTIME_OK);

    const auto record =
        make_local_action_shadow_authority_receipt_event(
            authority_epoch, UINT32_MAX);
    const auto before = status();
    const auto callbacks_before = local_action_shadow_report_callbacks;
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_OK);

    auto current = status();
    CHECK(current.next_authority_sequence == UINT32_MAX);
    CHECK(current.next_private_reconciliation_sequence == UINT32_MAX);
    CHECK(current.authority_sequence_exhausted == 1);
    CHECK(current.private_reconciliation_sequence_exhausted == 1);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips + 1);
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations);
    CHECK(current.receipt.highest_contiguous == UINT32_MAX);
    CHECK(current.receipt.selective_mask == 0);
    CHECK(current.authority_requires_resync == 0);
    CHECK(current.authority_degraded == 0);
    CHECK(local_action_shadow_report_callbacks == callbacks_before + 1);

    worr_cgame_event_runtime_status_v1 compact{};
    const auto *runtime_api = CG_GetEventRuntimeAPI();
    CHECK(runtime_api != nullptr);
    CHECK(runtime_api->GetStatus(&compact));
    CHECK(compact.authority_epoch == authority_epoch);
    CHECK(compact.next_presentation_sequence == UINT32_MAX);
    CHECK(compact.receipt.highest_contiguous == UINT32_MAX);
    CHECK((compact.state_flags &
           (WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED |
            WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC)) == 0);

    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    CHECK(local_action_shadow_report_callbacks == callbacks_before + 1);
    CHECK(advance(UINT64_MAX, UINT32_MAX, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    current = status();
    CHECK(current.next_authority_sequence == UINT32_MAX);
    CHECK(current.next_private_reconciliation_sequence == UINT32_MAX);
    CHECK(current.authority_sequence_exhausted == 1);
    CHECK(current.private_reconciliation_sequence_exhausted == 1);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips + 1);
    CHECK(current.authority_requires_resync == 0);
}

void test_visual_authority_consumes_uint32_max_once()
{
    constexpr std::uint32_t authority_epoch = 203;
    constexpr std::uint32_t snapshot_epoch = 403;
    constexpr std::uint32_t source_tick = 720;
    constexpr std::uint64_t source_time_us = UINT64_C(7200000);
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, UINT32_MAX) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    const auto record = make_event(
        true, authority_epoch, UINT32_MAX, 1, 0x9f01, source_tick,
        source_time_us, WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    const auto before = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.next_authority_sequence == UINT32_MAX);
    CHECK(current.next_private_reconciliation_sequence == UINT32_MAX);
    CHECK(current.authority_sequence_exhausted == 0);
    CHECK(current.private_reconciliation_sequence_exhausted == 1);
    CHECK(current.receipt.highest_contiguous == UINT32_MAX);
    CHECK(current.authority_requires_resync == 0);

    const auto ref = authority_ref(0, record);
    CHECK(observe(snapshot_epoch, 1, source_tick, source_time_us,
                  &ref, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(advance(source_time_us, source_tick, 4, 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.next_authority_sequence == UINT32_MAX);
    CHECK(current.next_private_reconciliation_sequence == UINT32_MAX);
    CHECK(current.authority_sequence_exhausted == 1);
    CHECK(current.private_reconciliation_sequence_exhausted == 1);
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations + 1);
    CHECK(current.authority_requires_resync == 0);
    CHECK(current.authority_degraded == 0);

    CHECK(advance(source_time_us, source_tick, 4, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_DUPLICATE);
    const auto after_duplicate = status();
    CHECK(after_duplicate.authoritative_presentations ==
          before.authoritative_presentations + 1);
    CHECK(after_duplicate.authority_sequence_exhausted == 1);
    CHECK(after_duplicate.authority_requires_resync == 0);
}

void test_private_receipt_admission_drains_ordered_heads()
{
    constexpr std::uint32_t authority_epoch = 195;
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);

    const auto before = status();
    const auto callbacks_before = local_action_shadow_report_callbacks;
    const auto sequence2 =
        make_local_action_shadow_authority_receipt_event(
            authority_epoch, 2);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&sequence2, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.next_authority_sequence == 1);
    CHECK(current.next_private_reconciliation_sequence == 1);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips);
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations);
    CHECK(current.advance_calls == before.advance_calls);
    CHECK(current.receipt.highest_contiguous == 0);
    CHECK(current.receipt.selective_mask == (UINT64_C(1) << 1));
    CHECK(local_action_shadow_report_callbacks == callbacks_before);
    cg_local_action_shadow_status_v1 action{};
    CG_LocalActionShadowGetStatus(&action);
    CHECK(action.authority_receipts == 0);

    const auto sequence1 =
        make_local_action_shadow_authority_receipt_event(
            authority_epoch, 1);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&sequence1, 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.next_authority_sequence == 3);
    CHECK(current.next_private_reconciliation_sequence == 3);
    CHECK(current.authoritative_records == before.authoritative_records + 2);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips + 2);
    CHECK(current.authoritative_presentations ==
          before.authoritative_presentations);
    CHECK(current.presentation_chain_hash ==
          before.presentation_chain_hash);
    CHECK(current.advance_calls == before.advance_calls);
    CHECK(current.last_render_time_us == before.last_render_time_us);
    CHECK(current.last_now_tick == before.last_now_tick);
    CHECK(current.receipt.highest_contiguous == 2);
    CHECK(current.receipt.selective_mask == 0);
    CHECK(current.authority_requires_resync == 0);
    CHECK(current.authority_degraded == 0);
    CHECK(local_action_shadow_report_callbacks == callbacks_before + 2);
    CG_LocalActionShadowGetStatus(&action);
    CHECK(action.authority_receipts == 1);
    CHECK(action.authority_duplicates == 1);
}

void test_private_receipt_admission_reclaims_bounded_storage()
{
    constexpr std::uint32_t authority_epoch = 196;
    constexpr std::uint32_t batch_count =
        WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2;
    constexpr std::uint32_t receipt_count =
        CG_EVENT_RUNTIME_AUTHORITY_CAPACITY + 1u;
    static_assert(CG_EVENT_RUNTIME_AUTHORITY_CAPACITY == batch_count * 2u);

    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    const auto before = status();
    const auto callbacks_before = local_action_shadow_report_callbacks;
    const auto prototype =
        make_local_action_shadow_authority_receipt_event(
            authority_epoch, 1);
    std::array<worr_event_record_v1, batch_count> records{};

    for (std::uint32_t batch = 0; batch < 2; ++batch) {
        for (std::uint32_t index = 0; index < batch_count; ++index) {
            records[index] = prototype;
            records[index].event_id.sequence =
                batch * batch_count + index + 1u;
            CHECK(Worr_EventRecordValidateV1(
                &records[index], WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));
        }
        CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
                  records.data(), batch_count) == CG_EVENT_RUNTIME_OK);
    }

    auto final_record = prototype;
    final_record.event_id.sequence = receipt_count;
    CHECK(Worr_EventRecordValidateV1(
        &final_record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&final_record, 1) ==
          CG_EVENT_RUNTIME_OK);

    const auto after = status();
    CHECK(after.next_authority_sequence == receipt_count + 1u);
    CHECK(after.next_private_reconciliation_sequence == receipt_count + 1u);
    CHECK(after.authority_count == CG_EVENT_RUNTIME_AUTHORITY_CAPACITY);
    CHECK(after.authoritative_batches == before.authoritative_batches + 3);
    CHECK(after.authoritative_records ==
          before.authoritative_records + receipt_count);
    CHECK(after.authoritative_terminal_skips ==
          before.authoritative_terminal_skips + receipt_count);
    CHECK(after.authoritative_presentations ==
          before.authoritative_presentations);
    CHECK(after.authoritative_capacity_failures ==
          before.authoritative_capacity_failures);
    CHECK(after.tombstone_evictions == before.tombstone_evictions + 1);
    CHECK(after.presentation_chain_hash == before.presentation_chain_hash);
    CHECK(after.advance_calls == before.advance_calls);
    CHECK(after.last_render_time_us == before.last_render_time_us);
    CHECK(after.last_now_tick == before.last_now_tick);
    CHECK(after.receipt.highest_contiguous == receipt_count);
    CHECK(after.receipt.selective_mask == 0);
    CHECK(after.authority_requires_resync == 0);
    CHECK(after.authority_degraded == 0);
    CHECK(local_action_shadow_report_callbacks ==
          callbacks_before + receipt_count);

    cg_local_action_shadow_status_v1 action{};
    CG_LocalActionShadowGetStatus(&action);
    CHECK(action.authority_receipts == 1);
    CHECK(action.authority_duplicates == receipt_count - 1u);
    CHECK(action.capacity_failures == 0);
    CHECK(action.requires_resync == 0);
}

void test_out_of_order_private_rejection_applies_lower_id_first()
{
    constexpr std::uint32_t authority_epoch = 200;
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);

    const auto sequence1 =
        make_local_action_shadow_authority_receipt_event(
            authority_epoch, 1);
    auto sequence2 = sequence1;
    sequence2.event_id.sequence = 2;
    worr_local_action_shadow_authority_receipt_v1 conflicting{};
    std::memcpy(&conflicting, sequence2.payload, sizeof(conflicting));
    conflicting.record_hash ^= UINT64_C(1);
    CHECK(Worr_LocalActionShadowAuthorityReceiptValidateV1(&conflicting));
    std::memcpy(sequence2.payload, &conflicting, sizeof(conflicting));
    CHECK(Worr_EventRecordValidateV1(
        &sequence2, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));

    const auto before = status();
    const auto callbacks_before = local_action_shadow_report_callbacks;
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&sequence2, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.next_private_reconciliation_sequence == 1);
    CHECK(current.authority_requires_resync == 0);
    CHECK(local_action_shadow_report_callbacks == callbacks_before);
    cg_local_action_shadow_status_v1 action{};
    CG_LocalActionShadowGetStatus(&action);
    CHECK(action.authority_receipts == 0);
    CHECK(action.authority_conflicts == 0);

    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&sequence1, 1) ==
          CG_EVENT_RUNTIME_DEGRADED);
    current = status();
    CHECK(current.next_private_reconciliation_sequence == 2);
    CHECK(current.next_authority_sequence == 1);
    CHECK(current.authoritative_terminal_skips ==
          before.authoritative_terminal_skips);
    CHECK(current.authority_requires_resync == 1);
    CHECK(current.authority_degraded == 1);
    CHECK(local_action_shadow_report_callbacks == callbacks_before + 1);
    CG_LocalActionShadowGetStatus(&action);
    CHECK(action.authority_receipts == 1);
    CHECK(action.authority_conflicts == 1);
    CHECK(action.requires_resync == 1);
}

void test_generic_stale_skip_does_not_present_replacement()
{
    constexpr std::uint32_t authority_epoch = 197;
    constexpr std::uint32_t snapshot_epoch = 397;
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 2) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    const auto newer = make_event(
        true, authority_epoch, 3, 3, 0x9733, 703,
        UINT64_C(7030000), WORR_EVENT_DELIVERY_PERSISTENT_STATE,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    const auto stale = make_event(
        true, authority_epoch, 2, 2, 0x9722, 702,
        UINT64_C(7020000), WORR_EVENT_DELIVERY_PERSISTENT_STATE,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&newer, 1) ==
          CG_EVENT_RUNTIME_OK);
    const auto before_stale = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&stale, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto current = status();
    CHECK(current.next_authority_sequence == 3);
    CHECK(current.authoritative_terminal_skips ==
          before_stale.authoritative_terminal_skips + 1);
    CHECK(current.authoritative_stale_or_coalesced ==
          before_stale.authoritative_stale_or_coalesced + 1);
    CHECK(current.advance_calls == before_stale.advance_calls);

    const auto ref = authority_ref(0, newer);
    CHECK(observe(snapshot_epoch, 1, 704, UINT64_C(7040000),
                  &ref, 1) == CG_EVENT_RUNTIME_OK);
    const auto before_present = status();
    CHECK(advance(UINT64_C(7040000), 704, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    current = status();
    CHECK(current.next_authority_sequence == 4);
    CHECK(current.authoritative_presentations ==
          before_present.authoritative_presentations + 1);
    CHECK(current.authoritative_prediction_suppressions ==
          before_present.authoritative_prediction_suppressions);
    CHECK(current.authority_requires_resync == 0);
}

void test_private_receipt_rejection_does_not_advance_skip_cursor()
{
    constexpr std::uint32_t authority_epoch = 198;
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);

    const auto record =
        make_local_action_shadow_authority_receipt_event(
            authority_epoch, 1);
    worr_local_action_shadow_authority_receipt_v1 receipt{};
    std::memcpy(&receipt, record.payload, sizeof(receipt));
    auto conflicting = receipt;
    conflicting.record_hash ^= UINT64_C(1);
    CHECK(Worr_LocalActionShadowAuthorityReceiptValidateV1(&conflicting));
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&conflicting) ==
          cg_local_action_shadow_receipt_result_v1::accepted_unmatched);

    const auto before = status();
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_DEGRADED);
    const auto after = status();
    CHECK(after.next_authority_sequence == 1);
    CHECK(after.next_private_reconciliation_sequence == 1);
    CHECK(after.authority_count == 1);
    CHECK(after.authoritative_records == before.authoritative_records + 1);
    CHECK(after.authoritative_terminal_skips ==
          before.authoritative_terminal_skips);
    CHECK(after.authoritative_presentations ==
          before.authoritative_presentations);
    CHECK(after.advance_calls == before.advance_calls);
    CHECK(after.receipt.highest_contiguous == 1);
    CHECK(after.receipt.selective_mask == 0);
    CHECK(after.authority_requires_resync == 1);
    CHECK(after.authority_degraded == 1);

    cg_local_action_shadow_status_v1 action{};
    CG_LocalActionShadowGetStatus(&action);
    CHECK(action.authority_receipts == 1);
    CHECK(action.authority_conflicts == 1);
    CHECK(action.requires_resync == 1);
}

void test_visual_release_drains_newly_unblocked_private_skip()
{
    constexpr std::uint32_t authority_epoch = 199;
    constexpr std::uint32_t snapshot_epoch = 399;
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    const auto receipt =
        make_local_action_shadow_authority_receipt_event(
            authority_epoch, 2);
    const auto callbacks_before = local_action_shadow_report_callbacks;
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&receipt, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(status().next_authority_sequence == 1);
    CHECK(status().next_private_reconciliation_sequence == 1);
    CHECK(local_action_shadow_report_callbacks == callbacks_before);

    const auto visual = make_event(
        true, authority_epoch, 1, 1, 0x9911, 710,
        UINT64_C(7100000), WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&visual, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(status().next_authority_sequence == 1);
    CHECK(status().next_private_reconciliation_sequence == 3);
    CHECK(local_action_shadow_report_callbacks == callbacks_before + 1);
    const auto ref = authority_ref(0, visual);
    CHECK(observe(snapshot_epoch, 1, 711, UINT64_C(7110000),
                  &ref, 1) == CG_EVENT_RUNTIME_OK);

    const auto before = status();
    CHECK(advance(UINT64_C(7110000), 711, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    const auto after = status();
    CHECK(after.next_authority_sequence == 3);
    CHECK(after.next_private_reconciliation_sequence == 3);
    CHECK(after.authoritative_presentations ==
          before.authoritative_presentations + 1);
    CHECK(after.authoritative_terminal_skips ==
          before.authoritative_terminal_skips + 1);
    CHECK(after.advance_calls == before.advance_calls + 1);
    CHECK(after.authority_requires_resync == 0);
    CHECK(after.authority_degraded == 0);
}

void test_local_action_shadow_resync_latches_runtime_health()
{
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetAuthority(193, 1) == CG_EVENT_RUNTIME_OK);

    const auto record =
        make_local_action_shadow_authority_receipt_event(193, 1);
    worr_local_action_shadow_authority_receipt_v1 receipt{};
    std::memcpy(&receipt, record.payload, sizeof(receipt));
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&receipt) ==
          cg_local_action_shadow_receipt_result_v1::accepted_unmatched);
    auto conflicting_receipt = receipt;
    conflicting_receipt.record_hash ^= UINT64_C(1);
    CHECK(Worr_LocalActionShadowAuthorityReceiptValidateV1(
        &conflicting_receipt));
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&conflicting_receipt) ==
          cg_local_action_shadow_receipt_result_v1::conflict);
    CHECK(CG_LocalActionShadowRequiresResync());

    CG_EventRuntimeSynchronizeLocalInteractionHealth();
    const auto current = status();
    CHECK(current.authority_requires_resync == 1);
    CHECK(current.authority_degraded == 1);
    CHECK(advance(7110000, 711, 1, 0) == CG_EVENT_RUNTIME_NOT_READY);

    CHECK(CG_EventRuntimeResetAuthority(194, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(!CG_LocalActionShadowRequiresResync());
    CHECK(status().authority_requires_resync == 0);
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

void test_checkpoint_readiness_requires_terminal_authority()
{
    constexpr std::uint32_t authority_epoch = 870;
    constexpr std::uint32_t snapshot_epoch = 970;
    constexpr std::uint32_t source_tick = 8700;
    constexpr std::uint64_t source_time_us = UINT64_C(87000000);

    CG_EventRuntimeSetPresenter(nullptr, nullptr);
    CG_EventRuntimeSetAuditEnabled(false);
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);

    cg_event_runtime_status_v1 untouched{};
    untouched.struct_size = UINT32_MAX;
    untouched.schema_version = UINT32_MAX;
    const auto untouched_expected = untouched;
    CHECK(!CG_EventRuntimeCheckpointReady(authority_epoch, &untouched));
    CHECK(std::memcmp(&untouched, &untouched_expected,
                      sizeof(untouched)) == 0);

    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(!CG_EventRuntimeCheckpointReady(0, &untouched));
    CHECK(!CG_EventRuntimeCheckpointReady(authority_epoch + 1u,
                                          &untouched));
    CHECK(!CG_EventRuntimeCheckpointReady(authority_epoch, nullptr));

    cg_event_runtime_status_v1 baseline{};
    CHECK(CG_EventRuntimeCheckpointReady(authority_epoch, &baseline));
    CHECK(baseline.struct_size == sizeof(baseline));
    CHECK(baseline.schema_version == CG_EVENT_RUNTIME_VERSION);
    CHECK(baseline.authority_epoch == authority_epoch);
    CHECK(baseline.authority_count == 0);
    CHECK(baseline.authority_requires_resync == 0);
    CHECK(baseline.authority_degraded == 0);

    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    const auto record = make_event(
        true, authority_epoch, 1, 1, 0x8701, source_tick,
        source_time_us, WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          CG_EVENT_RUNTIME_OK);

    cg_event_runtime_status_v1 pending = untouched_expected;
    CHECK(!CG_EventRuntimeCheckpointReady(authority_epoch, &pending));
    CHECK(std::memcmp(&pending, &untouched_expected,
                      sizeof(pending)) == 0);

    const auto ref = authority_ref(0, record);
    CHECK(observe(snapshot_epoch, 1, source_tick, source_time_us,
                  &ref, 1) == CG_EVENT_RUNTIME_OK);
    CHECK(advance(source_time_us, source_tick, 1, 1) ==
          CG_EVENT_RUNTIME_OK);

    cg_event_runtime_status_v1 presented{};
    CHECK(CG_EventRuntimeCheckpointReady(authority_epoch, &presented));
    /* Presented entries remain resident.  Readiness therefore cannot be
     * inferred from authority_count alone. */
    CHECK(presented.authority_count == 1);
    CHECK(presented.next_authority_sequence == 2);
    CHECK(presented.authoritative_presentations ==
          baseline.authoritative_presentations + 1);
    CHECK(presented.receipt.highest_contiguous == 1);

    /* Losing the fence beneath a fresh unresolved entry latches current
     * authority health and makes the query fail closed. */
    constexpr std::uint32_t unhealthy_epoch = authority_epoch + 1u;
    CHECK(CG_EventRuntimeResetAuthority(unhealthy_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch + 1u) ==
          CG_EVENT_RUNTIME_OK);
    const auto unresolved = make_event(
        true, unhealthy_epoch, 1, 2, 0x8711, source_tick + 1u,
        source_time_us + 1u, WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&unresolved, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch + 2u) ==
          CG_EVENT_RUNTIME_OK);
    const auto unhealthy = status();
    CHECK(unhealthy.authority_requires_resync == 1);
    CHECK(unhealthy.authority_degraded == 1);
    CHECK(!CG_EventRuntimeCheckpointReady(unhealthy_epoch, &pending));

    /* Sticky process audit history does not poison a later clean authority
     * epoch; the checkpoint records it as a baseline counter instead. */
    constexpr std::uint32_t recovered_epoch = authority_epoch + 2u;
    CHECK(CG_EventRuntimeResetAuthority(recovered_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    cg_event_runtime_status_v1 recovered{};
    CHECK(CG_EventRuntimeCheckpointReady(recovered_epoch, &recovered));
    CHECK(recovered.degraded != 0);
    CHECK(recovered.authority_requires_resync == 0);
    CHECK(recovered.authority_degraded == 0);
    CHECK(CG_EventRuntimeResetAuthority(0, 0) == CG_EVENT_RUNTIME_OK);
}

void test_two_phase_presenter_contract()
{
    constexpr std::uint32_t authority_epoch = 881;
    constexpr std::uint32_t snapshot_epoch = 981;
    constexpr std::uint32_t snapshot_sequence = 17;
    constexpr std::uint32_t source_tick = 900;
    constexpr std::uint32_t fence_tick = 901;
    constexpr std::uint64_t source_time_us = UINT64_C(9000000);
    constexpr std::uint64_t fence_time_us = UINT64_C(9010000);

    CG_EventRuntimeSetPresenter(&probe_can_present, &probe_present);
    CHECK(CG_EventRuntimeResetAuthority(authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    const auto authoritative = make_event(
        true, authority_epoch, 1, 1, 0x8811, source_tick,
        source_time_us, WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
              &authoritative, 1) == CG_EVENT_RUNTIME_OK);
    const auto authoritative_ref = authority_ref(0, authoritative);
    CHECK(observe(snapshot_epoch, snapshot_sequence, fence_tick,
                  fence_time_us, &authoritative_ref, 1) ==
          CG_EVENT_RUNTIME_OK);

    reset_presenter_probe(true);
    presenter_probe.reenter_can_present = true;
    presenter_probe.reenter_present = true;
    const auto authority_before = status();
    CHECK(advance(fence_time_us, fence_tick, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(presenter_probe.can_present_calls == 1);
    CHECK(presenter_probe.present_calls == 1);
    CHECK(!presenter_probe.can_present_checkpoint_ready);
    CHECK(!presenter_probe.present_checkpoint_ready);
    CHECK(presenter_probe.can_present_checkpoint_status.struct_size ==
          UINT32_MAX);
    CHECK(presenter_probe.present_checkpoint_status.struct_size ==
          UINT32_MAX);
    CHECK(presenter_probe.can_present_serial == 1);
    CHECK(presenter_probe.present_serial == 2);
    CHECK(presenter_probe.can_present_advance_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(presenter_probe.can_present_reset_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(presenter_probe.present_advance_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(presenter_probe.present_reset_result ==
          CG_EVENT_RUNTIME_REENTRANT);
    CHECK(presenter_probe.can_present_reentry_advanced == UINT32_MAX);
    CHECK(presenter_probe.present_reentry_advanced == UINT32_MAX);
    CHECK(std::memcmp(
              &presenter_probe.can_present_record, &authoritative,
              sizeof(authoritative)) == 0);
    CHECK(std::memcmp(
              &presenter_probe.present_record, &authoritative,
              sizeof(authoritative)) == 0);
    const auto &authority_context =
        presenter_probe.can_present_context;
    CHECK(authority_context.struct_size == sizeof(authority_context));
    CHECK(authority_context.schema_version ==
          CG_EVENT_RUNTIME_PRESENTER_VERSION);
    CHECK(authority_context.provenance ==
          CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY);
    CHECK(authority_context.reserved0 == 0);
    CHECK(authority_context.fence_snapshot_id.epoch == snapshot_epoch);
    CHECK(authority_context.fence_snapshot_id.sequence ==
          snapshot_sequence);
    CHECK(authority_context.fence_tick == fence_tick);
    CHECK(authority_context.reserved1 == 0);
    CHECK(authority_context.fence_time_us == fence_time_us);
    CHECK(std::memcmp(
              &presenter_probe.present_context, &authority_context,
              sizeof(authority_context)) == 0);

    /* CanPresent observes the pre-commit counters. Present observes the
     * already committed journal/audit state. */
    CHECK(presenter_probe.can_present_status.authoritative_presentations ==
          authority_before.authoritative_presentations);
    CHECK(presenter_probe.can_present_status.presentation_chain_hash ==
          authority_before.presentation_chain_hash);
    CHECK(presenter_probe.present_status.authoritative_presentations ==
          authority_before.authoritative_presentations + 1);
    CHECK(presenter_probe.present_status.presentation_chain_hash !=
          authority_before.presentation_chain_hash);

    const auto authority_after = status();
    CHECK(authority_after.next_authority_sequence == 2);
    CHECK(advance(fence_time_us, fence_tick, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    CHECK(presenter_probe.can_present_calls == 1);
    CHECK(presenter_probe.present_calls == 1);
    CHECK(status().presentation_chain_hash ==
          authority_after.presentation_chain_hash);

    constexpr std::uint32_t rejected_authority_epoch = 882;
    constexpr std::uint32_t rejected_snapshot_epoch = 982;
    constexpr std::uint32_t rejected_snapshot_sequence = 29;
    CHECK(CG_EventRuntimeResetAuthority(rejected_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(rejected_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    const auto rejected = make_event(
        true, rejected_authority_epoch, 1, 2, 0x8821, 910,
        UINT64_C(9100000), WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(&rejected, 1) ==
          CG_EVENT_RUNTIME_OK);
    const auto rejected_ref = authority_ref(0, rejected);
    CHECK(observe(rejected_snapshot_epoch, rejected_snapshot_sequence,
                  911, UINT64_C(9110000), &rejected_ref, 1) ==
          CG_EVENT_RUNTIME_OK);

    reset_presenter_probe(false);
    const auto rejection_before = status();
    CHECK(advance(UINT64_C(9110000), 911, 1, 0) ==
          CG_EVENT_RUNTIME_DEGRADED);
    CHECK(presenter_probe.can_present_calls == 1);
    CHECK(presenter_probe.present_calls == 0);
    CHECK(presenter_probe.can_present_serial == 1);
    CHECK(presenter_probe.present_serial == 0);
    CHECK(presenter_probe.can_present_context.provenance ==
          CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY);
    CHECK(presenter_probe.can_present_context.fence_snapshot_id.epoch ==
          rejected_snapshot_epoch);
    CHECK(presenter_probe.can_present_context.fence_snapshot_id.sequence ==
          rejected_snapshot_sequence);
    CHECK(presenter_probe.can_present_status.authoritative_presentations ==
          rejection_before.authoritative_presentations);
    CHECK(presenter_probe.can_present_status.presentation_chain_hash ==
          rejection_before.presentation_chain_hash);
    auto rejected_status = status();
    CHECK(rejected_status.next_authority_sequence == 1);
    CHECK(rejected_status.authoritative_presentations ==
          rejection_before.authoritative_presentations);
    CHECK(rejected_status.presentation_chain_hash ==
          rejection_before.presentation_chain_hash);
    CHECK(rejected_status.authority_requires_resync == 1);
    CHECK(rejected_status.authority_degraded == 1);
    CHECK(advance(UINT64_C(9110000), 911, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    CHECK(presenter_probe.can_present_calls == 1);
    CHECK(presenter_probe.present_calls == 0);

    constexpr std::uint32_t half_presenter_authority_epoch = 884;
    constexpr std::uint32_t half_presenter_snapshot_epoch = 984;
    CHECK(CG_EventRuntimeResetAuthority(
              half_presenter_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(
              half_presenter_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);
    const auto half_presenter_record = make_event(
        true, half_presenter_authority_epoch, 1, 4, 0x8841, 915,
        UINT64_C(9150000), WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(CG_EventRuntimeSubmitAuthoritativeBatch(
              &half_presenter_record, 1) == CG_EVENT_RUNTIME_OK);
    const auto half_presenter_ref =
        authority_ref(0, half_presenter_record);
    CHECK(observe(half_presenter_snapshot_epoch, 31, 916,
                  UINT64_C(9160000), &half_presenter_ref, 1) ==
          CG_EVENT_RUNTIME_OK);

    reset_presenter_probe(true);
    CG_EventRuntimeSetPresenter(&probe_can_present, nullptr);
    const auto half_presenter_before = status();
    CHECK(advance(UINT64_C(9160000), 916, 1, 0) ==
          CG_EVENT_RUNTIME_DEGRADED);
    CHECK(presenter_probe.can_present_calls == 0);
    CHECK(presenter_probe.present_calls == 0);
    const auto half_presenter_after = status();
    CHECK(half_presenter_after.next_authority_sequence == 1);
    CHECK(half_presenter_after.authoritative_presentations ==
          half_presenter_before.authoritative_presentations);
    CHECK(half_presenter_after.presentation_chain_hash ==
          half_presenter_before.presentation_chain_hash);
    CHECK(half_presenter_after.authority_requires_resync == 1);
    CHECK(half_presenter_after.authority_degraded == 1);

    constexpr std::uint32_t predicted_authority_epoch = 883;
    CG_EventRuntimeSetPresenter(&probe_can_present, &probe_present);
    CHECK(CG_EventRuntimeResetAuthority(predicted_authority_epoch, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(0) == CG_EVENT_RUNTIME_OK);
    const auto predicted = make_event(
        false, 0, 0, 3, 0x8831, 920, UINT64_C(9200000),
        WORR_EVENT_DELIVERY_RELIABLE_ORDERED,
        WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&predicted, 1) ==
          CG_EVENT_RUNTIME_OK);

    reset_presenter_probe(true);
    const auto prediction_before = status();
    CHECK(advance(UINT64_C(9200000), 920, 1, 1) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(presenter_probe.can_present_calls == 1);
    CHECK(presenter_probe.present_calls == 1);
    CHECK(presenter_probe.can_present_serial == 1);
    CHECK(presenter_probe.present_serial == 2);
    CHECK(std::memcmp(
              &presenter_probe.can_present_record, &predicted,
              sizeof(predicted)) == 0);
    const auto &prediction_context =
        presenter_probe.can_present_context;
    CHECK(prediction_context.struct_size == sizeof(prediction_context));
    CHECK(prediction_context.schema_version ==
          CG_EVENT_RUNTIME_PRESENTER_VERSION);
    CHECK(prediction_context.provenance ==
          CG_EVENT_RUNTIME_PRESENTATION_PREDICTED);
    CHECK(prediction_context.fence_snapshot_id.epoch == 0);
    CHECK(prediction_context.fence_snapshot_id.sequence == 0);
    CHECK(prediction_context.fence_tick == predicted.source_tick);
    CHECK(prediction_context.fence_time_us == predicted.source_time_us);
    CHECK(std::memcmp(
              &presenter_probe.present_context, &prediction_context,
              sizeof(prediction_context)) == 0);
    CHECK(presenter_probe.can_present_status.predicted_presentations ==
          prediction_before.predicted_presentations);
    CHECK(presenter_probe.can_present_status.presentation_chain_hash ==
          prediction_before.presentation_chain_hash);
    CHECK(presenter_probe.present_status.predicted_presentations ==
          prediction_before.predicted_presentations + 1);
    CHECK(presenter_probe.present_status.presentation_chain_hash !=
          prediction_before.presentation_chain_hash);

    const auto prediction_after = status();
    CHECK(advance(UINT64_C(9200000), 920, 1, 0) ==
          CG_EVENT_RUNTIME_NOT_READY);
    CHECK(presenter_probe.can_present_calls == 1);
    CHECK(presenter_probe.present_calls == 1);
    CHECK(status().presentation_chain_hash ==
          prediction_after.presentation_chain_hash);
    CG_EventRuntimeSetPresenter(nullptr, nullptr);
}

} // namespace

int main()
{
    CG_EventRuntimeSetAuditEnabled(true);
    CG_EventRuntimeSetLocalActionShadowReportCallback(
        &CG_LocalActionShadowReportParity);
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
    test_declared_snapshot_fence_bridge_both_arrival_orders();
    test_declared_snapshot_fenced_authority_ref_requires_exact_id();
    test_declared_snapshot_fenced_transient_expiry_identity();
    test_declared_snapshot_fenced_transient_retained_source_lifetime();
    test_declared_snapshot_fenced_missing_expiry_render_bound();
    test_delayed_snapshot_fenced_transient_batch_retains_source_proof();
    test_declared_snapshot_fenced_source_retention_bounds_lifetime();
    test_crossed_transient_fence_terminalizes_admitted_batch();
    test_missing_transient_fence_waits_for_deadline_then_terminalizes();
    test_declared_snapshot_fence_crossing_and_eviction_fail_closed();
    test_reverse_reconciliation_binds_existing_authority_id();
    test_strict_mismatch_degradation();
    test_private_authority_receipt_bridge();
    test_private_local_action_shadow_receipt_bridge();
    test_private_reconciliation_callback_reentry_is_guarded();
    test_private_receipt_consumes_uint32_max_once();
    test_visual_authority_consumes_uint32_max_once();
    test_private_receipt_admission_drains_ordered_heads();
    test_private_receipt_admission_reclaims_bounded_storage();
    test_out_of_order_private_rejection_applies_lower_id_first();
    test_generic_stale_skip_does_not_present_replacement();
    test_private_receipt_rejection_does_not_advance_skip_cursor();
    test_visual_release_drains_newly_unblocked_private_skip();
    test_local_interaction_resync_latches_runtime_health();
    test_local_action_shadow_resync_latches_runtime_health();
    test_legacy_snapshot_fence_is_audit_independent_and_bounded();
    test_checkpoint_readiness_requires_terminal_authority();
    test_two_phase_presenter_contract();
    std::puts("cgame event runtime tests passed");
    return 0;
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineFindSnapshot(
    worr_snapshot_id_v2,
    worr_snapshot_timeline_ref_v1 *)
{
    return WORR_SNAPSHOT_TIMELINE_NOT_FOUND;
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopySnapshot(
    worr_snapshot_timeline_ref_v1,
    worr_snapshot_v2 *)
{
    return WORR_SNAPSHOT_TIMELINE_STALE_REF;
}

bool CG_CanonicalSnapshotTimelineGetDiagnostics(
    cg_canonical_snapshot_timeline_diagnostics_v1 *)
{
    return false;
}
