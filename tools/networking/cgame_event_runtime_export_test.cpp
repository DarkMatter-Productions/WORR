/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_event_runtime.hpp"
#include "cg_canonical_snapshot_timeline.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

static_assert(WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC ==
              (1u << 3));

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,   \
                         __LINE__, #condition);                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

const worr_cgame_event_runtime_export_v1 *api()
{
    const auto *result = CG_GetEventRuntimeAPI();
    CHECK(result != nullptr);
    CHECK(result == CG_GetEventRuntimeAPI());
    CHECK(result->struct_size == sizeof(*result));
    CHECK(result->api_version == WORR_CGAME_EVENT_RUNTIME_API_VERSION);
    CHECK(result->ResetAuthority != nullptr);
    CHECK(result->SubmitAuthoritativeBatch != nullptr);
    CHECK(result->GetStatus != nullptr);
    return result;
}

worr_cgame_event_runtime_status_v1 compact_status(
    const worr_cgame_event_runtime_export_v1 *runtime_api)
{
    worr_cgame_event_runtime_status_v1 result;
    std::memset(&result, 0xa5, sizeof(result));
    CHECK(runtime_api->GetStatus(&result));
    CHECK(result.struct_size == sizeof(result));
    CHECK(result.api_version == WORR_CGAME_EVENT_RUNTIME_API_VERSION);
    return result;
}

cg_event_runtime_status_v1 detailed_status()
{
    cg_event_runtime_status_v1 result{};
    CHECK(CG_EventRuntimeGetStatus(&result));
    CHECK(result.struct_size == sizeof(result));
    CHECK(result.schema_version == CG_EVENT_RUNTIME_VERSION);
    return result;
}

void set_marker(worr_event_record_v1 &record, std::uint32_t marker)
{
    worr_event_payload_u32x4_v1 payload{};
    std::memcpy(&payload, record.payload, sizeof(payload));
    payload.value[0] = marker;
    payload.value[1] = marker ^ UINT32_C(0x55aa55aa);
    std::memcpy(record.payload, &payload, sizeof(payload));
}

worr_event_record_v1 make_event(std::uint32_t source_ordinal,
                                std::uint32_t marker,
                                std::uint32_t source_tick,
                                std::uint32_t command_sequence,
                                std::uint8_t delivery_class =
                                    WORR_EVENT_DELIVERY_RELIABLE_ORDERED)
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
    record.source_tick = source_tick;
    record.source_ordinal = source_ordinal;
    record.source_time_us = static_cast<std::uint64_t>(source_tick) * 1000u;
    record.source_entity = {4, 1};
    record.subject_entity = {WORR_EVENT_NO_ENTITY, 0};
    record.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
    record.delivery_class = delivery_class;
    record.prediction_class = command_sequence
                                  ? WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE
                                  : WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    if (command_sequence) {
        record.prediction_key.command_epoch = 19;
        record.prediction_key.command_sequence = command_sequence;
        record.prediction_key.emitter_ordinal = source_ordinal;
        record.prediction_key.lane = WORR_EVENT_PREDICTION_LANE_GAMEPLAY;
    }
    if (delivery_class <= WORR_EVENT_DELIVERY_TRANSIENT)
        record.expiry_tick = source_tick + 4u;
    record.payload_kind = WORR_EVENT_PAYLOAD_U32X4;
    record.payload_size = sizeof(payload);
    std::memcpy(record.payload, &payload, sizeof(payload));
    CHECK(command_sequence
              ? Worr_EventRecordValidateV1(
                    &record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2)
              : Worr_EventRecordCandidateValidateV1(
                    &record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));
    return record;
}

worr_event_record_v1 authorize(worr_event_record_v1 record,
                               std::uint32_t stream_epoch,
                               std::uint32_t sequence)
{
    record.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
    record.event_id = {stream_epoch, sequence};
    CHECK(Worr_EventRecordValidateV1(
        &record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));
    return record;
}

worr_event_record_v1 make_authority(std::uint32_t stream_epoch,
                                    std::uint32_t sequence,
                                    std::uint32_t marker,
                                    std::uint32_t source_ordinal,
                                    std::uint8_t delivery_class =
                                        WORR_EVENT_DELIVERY_RELIABLE_ORDERED)
{
    return authorize(make_event(source_ordinal, marker,
                                1000u + source_ordinal, 0,
                                delivery_class),
                     stream_epoch, sequence);
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
    worr_snapshot_event_ref_v2 result{};
    result.struct_size = sizeof(result);
    result.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    result.provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
    result.carrier_ordinal = carrier_ordinal;
    result.semantic_version = record.model_revision;
    result.authority_id = record.event_id;
    result.semantic_hash = semantic_hash(record);
    return result;
}

worr_snapshot_v2 make_snapshot(
    std::uint32_t epoch, std::uint32_t sequence,
    std::uint32_t server_tick, std::uint64_t server_time_us,
    const worr_snapshot_event_ref_v2 *refs, std::uint32_t count)
{
    worr_snapshot_v2 result{};
    result.struct_size = sizeof(result);
    result.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    result.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    result.snapshot_id = {epoch, sequence};
    result.server_tick = server_tick;
    result.server_time_us = server_time_us;
    result.event_range.count = count;
    CHECK(Worr_SnapshotEventRefsHashV2(refs, count,
                                       &result.event_hash));
    return result;
}

cg_event_runtime_result_v1 observe_authority_ref(
    std::uint32_t snapshot_epoch, std::uint32_t snapshot_sequence,
    std::uint32_t tick, std::uint64_t time_us,
    const worr_snapshot_event_ref_v2 &ref)
{
    const auto snapshot = make_snapshot(
        snapshot_epoch, snapshot_sequence, tick, time_us, &ref, 1);
    return CG_EventRuntimeObserveSnapshot(&snapshot, &ref, 1);
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

void check_active_receipt(const worr_cgame_event_runtime_status_v1 &status,
                          std::uint32_t epoch,
                          std::uint32_t highest_contiguous,
                          std::uint64_t selective_mask)
{
    CHECK((status.state_flags & WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE) != 0);
    CHECK(status.authority_epoch == epoch);
    CHECK(status.receipt.struct_size == sizeof(status.receipt));
    CHECK(status.receipt.schema_version == WORR_EVENT_ABI_VERSION);
    CHECK(status.receipt.stream_epoch == epoch);
    CHECK(status.receipt.highest_contiguous == highest_contiguous);
    CHECK(status.receipt.selective_mask == selective_mask);
}

void test_table_validation_and_compact_status(
    const worr_cgame_event_runtime_export_v1 *runtime_api)
{
    CHECK(!runtime_api->GetStatus(nullptr));
    CG_EventRuntimeSetAuditEnabled(false);
    CHECK(runtime_api->ResetAuthority(0, 0) ==
          WORR_CGAME_EVENT_RUNTIME_OK);

    auto current = compact_status(runtime_api);
    CHECK(current.authority_epoch == 0);
    CHECK(current.next_presentation_sequence == 0);
    CHECK(current.authority_count == 0);
    CHECK(current.state_flags == 0);
    CHECK(current.receipt.struct_size == 0);
    CHECK(current.receipt.schema_version == 0);
    CHECK(current.receipt.stream_epoch == 0);
    CHECK(current.receipt.highest_contiguous == 0);
    CHECK(current.receipt.selective_mask == 0);

    CHECK(runtime_api->ResetAuthority(77, 0) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(runtime_api->ResetAuthority(0, 1) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    auto record = make_authority(77, 1, 0x1001, 1);
    CHECK(runtime_api->SubmitAuthoritativeBatch(nullptr, 1) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(runtime_api->SubmitAuthoritativeBatch(&record, 0) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(runtime_api->SubmitAuthoritativeBatch(
              &record, WORR_CGAME_EVENT_RUNTIME_MAX_BATCH + 1u) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    current = compact_status(runtime_api);
    CHECK(current.authority_epoch == 0 && current.authority_count == 0);

    CG_EventRuntimeSetAuditEnabled(true);
    current = compact_status(runtime_api);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED) != 0);
    CG_EventRuntimeSetAuditEnabled(false);
    current = compact_status(runtime_api);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED) == 0);
}

void test_reordered_gap_closure_and_input_copy(
    const worr_cgame_event_runtime_export_v1 *runtime_api)
{
    constexpr std::uint32_t epoch = 71;
    constexpr std::uint32_t first_sequence = 400;
    CHECK(runtime_api->ResetAuthority(epoch, first_sequence) ==
          WORR_CGAME_EVENT_RUNTIME_OK);

    auto current = compact_status(runtime_api);
    CHECK(current.next_presentation_sequence == first_sequence);
    CHECK(current.authority_count == 0);
    check_active_receipt(current, epoch, first_sequence - 1u, 0);

    auto borrowed = make_authority(epoch, first_sequence + 1u,
                                   0x2002, 2);
    const auto accepted_copy = borrowed;
    CHECK(runtime_api->SubmitAuthoritativeBatch(&borrowed, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    std::memset(&borrowed, 0xa5, sizeof(borrowed));

    current = compact_status(runtime_api);
    CHECK(current.authority_count == 1);
    CHECK(current.next_presentation_sequence == first_sequence);
    check_active_receipt(current, epoch, first_sequence - 1u,
                         UINT64_C(1) << 1);

    /* A duplicate sourced from independent storage proves that the runtime
     * retained an owned value rather than the now-destroyed caller buffer. */
    CHECK(runtime_api->SubmitAuthoritativeBatch(&accepted_copy, 1) ==
          WORR_CGAME_EVENT_RUNTIME_DUPLICATE);

    const auto closes_gap = make_authority(epoch, first_sequence,
                                           0x2001, 1);
    CHECK(runtime_api->SubmitAuthoritativeBatch(&closes_gap, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    current = compact_status(runtime_api);
    CHECK(current.authority_count == 2);
    CHECK(current.next_presentation_sequence == first_sequence);
    check_active_receipt(current, epoch, first_sequence + 1u, 0);
}

void test_correction_duplicate_and_conflict(
    const worr_cgame_event_runtime_export_v1 *runtime_api)
{
    constexpr std::uint32_t epoch = 72;
    constexpr std::uint32_t sequence = 10;
    CHECK(runtime_api->ResetAuthority(epoch, sequence) ==
          WORR_CGAME_EVENT_RUNTIME_OK);

    const auto prediction = make_event(3, 0x3001, 1300, 9001);
    CHECK(CG_EventRuntimeSubmitPredictedBatch(&prediction, 1) ==
          CG_EVENT_RUNTIME_OK);
    auto corrected = authorize(prediction, epoch, sequence);
    set_marker(corrected, 0x3002);
    CHECK(Worr_EventRecordValidateV1(
        &corrected, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2));

    CHECK(runtime_api->SubmitAuthoritativeBatch(&corrected, 1) ==
          WORR_CGAME_EVENT_RUNTIME_CORRECTED);
    auto current = compact_status(runtime_api);
    CHECK(current.authority_count == 1);
    CHECK(current.next_presentation_sequence == sequence);
    check_active_receipt(current, epoch, sequence, 0);

    CHECK(runtime_api->SubmitAuthoritativeBatch(&corrected, 1) ==
          WORR_CGAME_EVENT_RUNTIME_DUPLICATE);
    auto conflicting = corrected;
    set_marker(conflicting, 0x3003);
    CHECK(runtime_api->SubmitAuthoritativeBatch(&conflicting, 1) ==
          WORR_CGAME_EVENT_RUNTIME_CONFLICT);

    current = compact_status(runtime_api);
    CHECK(current.authority_count == 1);
    check_active_receipt(current, epoch, sequence, 0);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED) != 0);
}

void test_authority_reset_clears_epoch_degraded(
    const worr_cgame_event_runtime_export_v1 *runtime_api)
{
    constexpr std::uint32_t degraded_epoch = 74;
    CHECK(runtime_api->ResetAuthority(degraded_epoch, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    const auto accepted = make_authority(degraded_epoch, 1,
                                         0x3501, 1);
    CHECK(runtime_api->SubmitAuthoritativeBatch(&accepted, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    auto conflicting = accepted;
    set_marker(conflicting, 0x35ff);
    CHECK(runtime_api->SubmitAuthoritativeBatch(&conflicting, 1) ==
          WORR_CGAME_EVENT_RUNTIME_CONFLICT);

    auto current = compact_status(runtime_api);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED) != 0);
    CHECK(detailed_status().degraded != 0);

    CHECK(runtime_api->ResetAuthority(degraded_epoch + 1u, 50) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    current = compact_status(runtime_api);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED) == 0);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC) == 0);
    check_active_receipt(current, degraded_epoch + 1u, 49, 0);

    /* The stable export reports health for the active authority epoch. The
     * detailed private telemetry deliberately retains historical evidence. */
    CHECK(detailed_status().degraded != 0);
}

void test_transaction_rollback(
    const worr_cgame_event_runtime_export_v1 *runtime_api)
{
    constexpr std::uint32_t epoch = 73;
    constexpr std::uint32_t first_sequence = 100;
    CHECK(runtime_api->ResetAuthority(epoch, first_sequence) ==
          WORR_CGAME_EVENT_RUNTIME_OK);

    const auto first = make_authority(epoch, first_sequence,
                                      0x4001, 1);
    const auto wrong_epoch = make_authority(epoch + 1u,
                                            first_sequence + 1u,
                                            0x4002, 2);
    const std::array<worr_event_record_v1, 2> rejected{
        first, wrong_epoch,
    };
    CHECK(runtime_api->SubmitAuthoritativeBatch(
              rejected.data(), static_cast<std::uint32_t>(rejected.size())) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    auto current = compact_status(runtime_api);
    CHECK(current.authority_count == 0);
    check_active_receipt(current, epoch, first_sequence - 1u, 0);

    CHECK(runtime_api->SubmitAuthoritativeBatch(&first, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    const auto second = make_authority(epoch, first_sequence + 1u,
                                       0x4002, 2);
    auto conflicts_with_first = first;
    set_marker(conflicts_with_first, 0x4fff);
    const std::array<worr_event_record_v1, 2> conflict_batch{
        second, conflicts_with_first,
    };
    CHECK(runtime_api->SubmitAuthoritativeBatch(
              conflict_batch.data(),
              static_cast<std::uint32_t>(conflict_batch.size())) ==
          WORR_CGAME_EVENT_RUNTIME_CONFLICT);
    current = compact_status(runtime_api);
    CHECK(current.authority_count == 1);
    check_active_receipt(current, epoch, first_sequence, 0);

    CHECK(runtime_api->SubmitAuthoritativeBatch(&second, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    current = compact_status(runtime_api);
    CHECK(current.authority_count == 2);
    check_active_receipt(current, epoch, first_sequence + 1u, 0);
}

void test_authority_progress_is_independent_of_audit(
    const worr_cgame_event_runtime_export_v1 *runtime_api)
{
    constexpr std::uint32_t authority_epoch = 80;
    constexpr std::uint32_t snapshot_epoch = 180;
    CG_EventRuntimeSetAuditEnabled(false);
    CHECK(runtime_api->ResetAuthority(authority_epoch, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    const auto reliable = make_authority(authority_epoch, 1,
                                         0x5001, 1);
    const auto expiring = make_authority(
        authority_epoch, 2, 0x5002, 2,
        WORR_EVENT_DELIVERY_TRANSIENT);
    const std::array<worr_event_record_v1, 2> batch{
        reliable, expiring,
    };
    CHECK(runtime_api->SubmitAuthoritativeBatch(
              batch.data(), static_cast<std::uint32_t>(batch.size())) ==
          WORR_CGAME_EVENT_RUNTIME_OK);

    const auto ref = authority_ref(0, reliable);
    CHECK(observe_authority_ref(
              snapshot_epoch, 1, reliable.source_tick,
              reliable.source_time_us, ref) == CG_EVENT_RUNTIME_OK);
    auto private_before_toggle = detailed_status();
    CHECK(private_before_toggle.reference_count == 1);
    CHECK(private_before_toggle.authority_ref_body_joins != 0);
    CHECK(!CG_EventRuntimeAuditEnabled());

    CG_EventRuntimeSetAuditEnabled(true);
    CG_EventRuntimeSetAuditEnabled(false);
    const auto private_after_toggle = detailed_status();
    CHECK(private_after_toggle.reference_count ==
          private_before_toggle.reference_count);
    CHECK(private_after_toggle.authority_ref_body_joins ==
          private_before_toggle.authority_ref_body_joins);
    auto current = compact_status(runtime_api);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED) == 0);

    const auto before_present = detailed_status();
    CHECK(advance(reliable.source_time_us, reliable.source_tick,
                  1, 1) == CG_EVENT_RUNTIME_OK);
    auto after_present = detailed_status();
    CHECK(after_present.next_authority_sequence == 2);
    CHECK(after_present.authoritative_presentations ==
          before_present.authoritative_presentations + 1u);

    CHECK(advance(expiring.source_time_us, expiring.expiry_tick,
                  1, 0) == CG_EVENT_RUNTIME_OK);
    const auto after_expiry = detailed_status();
    CHECK(after_expiry.next_authority_sequence == 3);
    CHECK(after_expiry.authoritative_expirations ==
          after_present.authoritative_expirations + 1u);
    CHECK(after_expiry.authoritative_terminal_skips ==
          after_present.authoritative_terminal_skips + 1u);
    current = compact_status(runtime_api);
    CHECK(current.next_presentation_sequence == 3);
}

void test_snapshot_reset_requires_authority_resync(
    const worr_cgame_event_runtime_export_v1 *runtime_api)
{
    constexpr std::uint32_t authority_epoch = 81;
    constexpr std::uint32_t old_snapshot_epoch = 181;
    constexpr std::uint32_t new_snapshot_epoch = 182;
    CHECK(runtime_api->ResetAuthority(authority_epoch, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(old_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    const auto unresolved = make_authority(authority_epoch, 1,
                                           0x6001, 1);
    CHECK(runtime_api->SubmitAuthoritativeBatch(&unresolved, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(new_snapshot_epoch) ==
          CG_EVENT_RUNTIME_OK);

    auto current = compact_status(runtime_api);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE) != 0);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC) != 0);
    CHECK(current.authority_count == 1);
    check_active_receipt(current, authority_epoch, 1, 0);

    const auto blocked = make_authority(authority_epoch, 2,
                                        0x6002, 2);
    CHECK(runtime_api->SubmitAuthoritativeBatch(&blocked, 1) ==
          WORR_CGAME_EVENT_RUNTIME_NOT_READY);
    current = compact_status(runtime_api);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC) != 0);
    CHECK(current.authority_count == 1);
    check_active_receipt(current, authority_epoch, 1, 0);

    constexpr std::uint32_t replacement_epoch = 82;
    constexpr std::uint32_t replacement_first = 20;
    CHECK(runtime_api->ResetAuthority(replacement_epoch,
                                      replacement_first) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    current = compact_status(runtime_api);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC) == 0);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED) == 0);
    CHECK(current.authority_count == 0);
    check_active_receipt(current, replacement_epoch,
                         replacement_first - 1u, 0);

    const auto replacement = make_authority(
        replacement_epoch, replacement_first, 0x6003, 3);
    CHECK(runtime_api->SubmitAuthoritativeBatch(&replacement, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
}

void test_deactivate_and_scrub(
    const worr_cgame_event_runtime_export_v1 *runtime_api)
{
    CHECK(runtime_api->ResetAuthority(0, 0) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    const auto current = compact_status(runtime_api);
    CHECK(current.authority_epoch == 0);
    CHECK(current.next_presentation_sequence == 0);
    CHECK(current.authority_count == 0);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE) == 0);
    CHECK((current.state_flags &
           WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED) == 0);
    CHECK(current.receipt.struct_size == 0);
    CHECK(current.receipt.schema_version == 0);
    CHECK(current.receipt.stream_epoch == 0);
    CHECK(current.receipt.highest_contiguous == 0);
    CHECK(current.receipt.selective_mask == 0);

    const auto stale = make_authority(73, 102, 0x5001, 1);
    CHECK(runtime_api->SubmitAuthoritativeBatch(&stale, 1) ==
          WORR_CGAME_EVENT_RUNTIME_UNINITIALIZED);
}

} // namespace

int main()
{
    const auto *runtime_api = api();
    test_table_validation_and_compact_status(runtime_api);
    test_reordered_gap_closure_and_input_copy(runtime_api);
    test_authority_reset_clears_epoch_degraded(runtime_api);
    test_correction_duplicate_and_conflict(runtime_api);
    test_transaction_rollback(runtime_api);
    test_authority_progress_is_independent_of_audit(runtime_api);
    test_snapshot_reset_requires_authority_resync(runtime_api);
    test_deactivate_and_scrub(runtime_api);
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
