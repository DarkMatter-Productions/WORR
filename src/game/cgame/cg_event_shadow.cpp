/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_event_shadow.hpp"
#include "cg_event_runtime.hpp"
#include "cg_canonical_snapshot_timeline.hpp"

#include <array>
#include <cstring>

namespace {

static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_RAW_PENDING_CAPACITY ==
              WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2);

worr_cgame_event_shadow_audit_v1 audit_v1;
worr_cgame_event_range_audit_v2 audit_v2;

struct canonical_event_presentation_state_t {
    std::array<cg_canonical_event_presentation_entry_v1,
               CG_CANONICAL_EVENT_PRESENTATION_CAPACITY> entries;
    std::uint32_t stream_epoch;
    std::uint32_t journal_generation;
    std::uint32_t retained_count;
    std::uint64_t next_serial;
    std::uint64_t reset_count;
    std::uint64_t accepted_ranges;
    std::uint64_t accepted_records;
    std::uint64_t rejected_ranges;
    std::uint64_t empty_carriers;
    std::uint64_t adapter_rejections;
    std::uint64_t overwritten_records;
    cg_canonical_event_presentation_cursor_v1 audit_cursor;
    std::uint64_t audit_ready_records;
    std::uint64_t audit_future_stalls;
    std::uint64_t audit_overrun_recoveries;
    std::uint64_t audit_last_ready_serial;
    std::uint64_t audit_last_render_time_us;
    bool initialized;
};

canonical_event_presentation_state_t presentation;

struct native_event_probe_raw_token_t {
    std::uint32_t carrier_kind;
    std::uint32_t presenter_kind;
    std::uint64_t semantic_hash;
};

struct native_event_probe_raw_state_t {
    std::array<native_event_probe_raw_token_t,
               WORR_CGAME_NATIVE_EVENT_PROBE_RAW_PENDING_CAPACITY>
        pending;
    std::array<std::uint64_t,
               WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT>
        action_by_kind;
    std::uint32_t pending_head;
    std::uint32_t pending_count;
    std::uint64_t action_records;
    std::uint64_t action_chain_hash;
    std::uint64_t effect_dispatches;
    std::uint64_t effect_chain_hash;
    std::uint64_t effect_suppressions;
    std::uint64_t pair_failures;
    bool map_active;
    bool parity_armed;
};

native_event_probe_raw_state_t raw_probe;

void parity_hash_byte(std::uint64_t &hash, std::uint8_t value)
{
    hash ^= value;
    hash *= UINT64_C(1099511628211);
}

void parity_hash_u32(std::uint64_t &hash, std::uint32_t value)
{
    for (std::uint32_t index = 0; index < 4; ++index)
        parity_hash_byte(
            hash, static_cast<std::uint8_t>(value >> (index * 8u)));
}

void parity_hash_u64(std::uint64_t &hash, std::uint64_t value)
{
    for (std::uint32_t index = 0; index < 8; ++index)
        parity_hash_byte(
            hash, static_cast<std::uint8_t>(value >> (index * 8u)));
}

bool keyed_poi_presentation_projection_hash(
    const worr_event_record_v1 &record,
    const worr_snapshot_v2 &snapshot,
    std::uint64_t *hash_out)
{
    const auto &controlled = snapshot.controlled_entity.identity;
    if (!hash_out || snapshot.snapshot_id.epoch == 0 ||
        record.source_entity.index != 0 ||
        record.source_entity.generation != 1 ||
        controlled.index == 0 || controlled.index == WORR_EVENT_NO_ENTITY ||
        controlled.generation == 0 ||
        record.subject_entity.index != controlled.index ||
        record.subject_entity.generation != controlled.generation) {
        return false;
    }

    worr_event_record_v1 neutral = record;
    neutral.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                    WORR_EVENT_FLAG_PRESENT_ONCE;
    neutral.event_id = {};
    neutral.source_tick = 0;
    neutral.source_ordinal = 0;
    neutral.source_time_us = 0;
    neutral.source_entity = {0, 1};
    neutral.subject_entity = {WORR_EVENT_NO_ENTITY, 0};
    neutral.prediction_key = {};
    neutral.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    neutral.expiry_tick = 0;
    std::uint64_t semantic_hash = 0;
    if (!Worr_EventRecordCandidateValidateV1(
            &neutral, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2) ||
        !Worr_EventRecordSemanticHashV1(
            &neutral, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
            &semantic_hash)) {
        return false;
    }

    static constexpr char domain[] =
        "WORR-KEYED-POI-PRESENTATION-PARITY-V1";
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (std::uint32_t index = 0; index < sizeof(domain) - 1u; ++index)
        parity_hash_byte(hash, static_cast<std::uint8_t>(domain[index]));
    parity_hash_u32(hash, snapshot.snapshot_id.epoch);
    parity_hash_u64(hash, semantic_hash);
    *hash_out = hash;
    return true;
}

bool presentation_projection_hash(
    const worr_event_record_v1 &record,
    const worr_snapshot_v2 &snapshot,
    std::uint64_t *hash_out)
{
    if (!hash_out || snapshot.snapshot_id.epoch == 0 ||
        snapshot.snapshot_id.sequence == 0 || snapshot.snapshot_hash == 0)
        return false;
    if (record.payload_kind == WORR_EVENT_PAYLOAD_KEYED_POI_V1) {
        return keyed_poi_presentation_projection_hash(
            record, snapshot, hash_out);
    }
    worr_event_record_v1 normalized = record;
    normalized.source_time_us = 0;
    std::uint64_t semantic_hash = 0;
    if (!Worr_EventRecordSemanticHashV1(
            &normalized, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
            &semantic_hash)) {
        return false;
    }

    static constexpr char domain[] =
        "WORR-EVENT-PRESENTATION-PARITY-V2";
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (std::uint32_t index = 0; index < sizeof(domain) - 1u; ++index)
        parity_hash_byte(hash, static_cast<std::uint8_t>(domain[index]));
    parity_hash_u32(hash, snapshot.snapshot_id.epoch);
    parity_hash_u32(hash, snapshot.snapshot_id.sequence);
    parity_hash_u64(hash, snapshot.snapshot_hash);
    parity_hash_u64(hash, semantic_hash);
    *hash_out = hash;
    return true;
}

bool precheckpoint_presentation_hash(
    const worr_event_record_v1 &record,
    std::uint64_t *hash_out)
{
    if (!hash_out)
        return false;
    worr_event_record_v1 normalized = record;
    normalized.source_time_us = 0;
    return Worr_EventRecordSemanticHashV1(
        &normalized, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2, hash_out);
}

bool copy_parity_snapshot(
    worr_snapshot_id_v2 snapshot_id,
    worr_snapshot_v2 *snapshot_out)
{
    if (!snapshot_out || snapshot_id.epoch == 0 ||
        snapshot_id.sequence == 0)
        return false;
    worr_snapshot_timeline_ref_v1 ref{};
    worr_snapshot_v2 snapshot{};
    if (CG_CanonicalSnapshotTimelineFindSnapshot(snapshot_id, &ref) !=
            WORR_SNAPSHOT_TIMELINE_OK ||
        CG_CanonicalSnapshotTimelineCopySnapshot(ref, &snapshot) !=
            WORR_SNAPSHOT_TIMELINE_OK ||
        snapshot.snapshot_id.epoch != snapshot_id.epoch ||
        snapshot.snapshot_id.sequence != snapshot_id.sequence ||
        snapshot.snapshot_hash == 0) {
        return false;
    }
    *snapshot_out = snapshot;
    return true;
}

void increment_saturated(std::uint64_t &value)
{
    if (value != UINT64_MAX)
        ++value;
}

void add_saturated(std::uint64_t &value, std::uint64_t addend)
{
    if (value > UINT64_MAX - addend)
        value = UINT64_MAX;
    else
        value += addend;
}

void discard_raw_pending(bool account_failure)
{
    if (account_failure)
        add_saturated(raw_probe.pair_failures, raw_probe.pending_count);
    raw_probe.pending_head = 0;
    raw_probe.pending_count = 0;
    raw_probe.pending = {};
}

bool raw_action_kind(
    const worr_cgame_event_range_v2 &range,
    std::uint32_t record_index,
    std::uint32_t *presenter_kind_out)
{
    if (!presenter_kind_out || !range.records ||
        record_index >= range.count ||
        range.phase !=
            WORR_CGAME_EVENT_RANGE_PHASE_ACTION_PRE_PRESENT_V2 ||
        range.adapter_status != WORR_CGAME_EVENT_ADAPTER_OK_V2)
        return false;

    switch (range.carrier_kind) {
    case WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2:
        if (range.count != 1 ||
            range.records[record_index].payload_kind !=
                WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1) {
            return false;
        }
        *presenter_kind_out =
            WORR_CGAME_NATIVE_EVENT_PROBE_KIND_LEGACY_TEMP;
        return true;
    case WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2:
    case WORR_CGAME_EVENT_CARRIER_MONSTER_MUZZLE_V2: {
        if (range.count != 1 ||
            range.records[record_index].payload_kind !=
                WORR_EVENT_PAYLOAD_MUZZLE_V1)
            return false;
        worr_event_payload_muzzle_v1 payload{};
        std::memcpy(
            &payload, range.records[record_index].payload,
            sizeof(payload));
        const std::uint16_t expected_family =
            range.carrier_kind ==
                    WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2
                ? WORR_EVENT_MUZZLE_FAMILY_PLAYER
                : WORR_EVENT_MUZZLE_FAMILY_MONSTER;
        if (payload.family != expected_family)
            return false;
        *presenter_kind_out = WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE;
        return true;
    }
    case WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2:
        if (range.count != 1 ||
            range.records[record_index].payload_kind !=
                WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1) {
            return false;
        }
        *presenter_kind_out =
            WORR_CGAME_NATIVE_EVENT_PROBE_KIND_SPATIAL_AUDIO;
        return true;
    case WORR_CGAME_EVENT_CARRIER_DAMAGE_V2:
        if (range.count == 0 ||
            range.count > WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2 ||
            range.records[record_index].payload_kind !=
                WORR_EVENT_PAYLOAD_DAMAGE ||
            range.records[record_index].source_ordinal != record_index) {
            return false;
        }
        *presenter_kind_out =
            WORR_CGAME_NATIVE_EVENT_PROBE_KIND_DAMAGE;
        return true;
    case WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2:
        if (range.count != 1 ||
            range.records[record_index].payload_kind !=
                WORR_EVENT_PAYLOAD_KEYED_POI_V1) {
            return false;
        }
        *presenter_kind_out =
            WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI;
        return true;
    default:
        return false;
    }
}

void capture_raw_actions(const worr_cgame_event_range_v2 &range)
{
    if (!raw_probe.map_active || !range.count || !range.records)
        return;

    std::array<std::uint32_t, WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2>
        presenter_kinds{};
    std::array<std::uint64_t, WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2>
        presentation_hashes{};
    if (range.count > presenter_kinds.size())
        return;
    for (std::uint32_t index = 0; index < range.count; ++index) {
        if (!raw_action_kind(
                range, index, &presenter_kinds[index])) {
            return;
        }
        const bool hash_valid = raw_probe.parity_armed
            ? CG_NativeEventProbeLegacyActionHash(
                  &range, index, &presentation_hashes[index])
            : precheckpoint_presentation_hash(
                  range.records[index], &presentation_hashes[index]);
        if (presenter_kinds[index] >= raw_probe.action_by_kind.size() ||
            !hash_valid) {
            increment_saturated(raw_probe.pair_failures);
            return;
        }
    }
    if (range.count > raw_probe.pending.size() - raw_probe.pending_count) {
        /* Never retain a prefix of one atomic carrier. */
        increment_saturated(raw_probe.pair_failures);
        return;
    }

    for (std::uint32_t index = 0; index < range.count; ++index) {
        const std::uint32_t presenter_kind = presenter_kinds[index];
        const std::uint64_t presentation_hash =
            presentation_hashes[index];
        increment_saturated(raw_probe.action_records);
        increment_saturated(raw_probe.action_by_kind[presenter_kind]);
        raw_probe.action_chain_hash =
            Worr_CGameNativeEventProbeChainAppendV1(
                raw_probe.action_chain_hash, presenter_kind,
                presentation_hash);

        const std::uint32_t tail =
            (raw_probe.pending_head + raw_probe.pending_count) %
            static_cast<std::uint32_t>(raw_probe.pending.size());
        raw_probe.pending[tail] = {
            range.carrier_kind, presenter_kind, presentation_hash};
        ++raw_probe.pending_count;
    }
}

std::uint64_t oldest_serial()
{
    if (!presentation.retained_count)
        return presentation.next_serial;
    return presentation.next_serial - presentation.retained_count;
}

cg_canonical_event_presentation_cursor_v1 make_cursor(
    std::uint64_t next_serial);

void reset_presentation(std::uint32_t stream_epoch)
{
    const std::uint64_t reset_count = presentation.reset_count;
    std::uint32_t generation = presentation.journal_generation + 1u;
    if (!generation)
        generation = 1u;
    presentation = {};
    presentation.initialized = stream_epoch != 0;
    presentation.stream_epoch = stream_epoch;
    presentation.journal_generation = generation;
    presentation.next_serial = 1;
    presentation.reset_count = reset_count;
    increment_saturated(presentation.reset_count);
    if (presentation.initialized)
        presentation.audit_cursor = make_cursor(1);
}

bool cursor_shape_valid(
    const cg_canonical_event_presentation_cursor_v1 *cursor)
{
    return cursor && cursor->struct_size == sizeof(*cursor) &&
           cursor->schema_version ==
               CG_CANONICAL_EVENT_PRESENTATION_VERSION &&
           cursor->stream_epoch != 0 &&
           cursor->journal_generation != 0 && cursor->next_serial != 0;
}

cg_canonical_event_presentation_cursor_v1 make_cursor(
    std::uint64_t next_serial)
{
    cg_canonical_event_presentation_cursor_v1 cursor{};
    cursor.struct_size = sizeof(cursor);
    cursor.schema_version = CG_CANONICAL_EVENT_PRESENTATION_VERSION;
    cursor.stream_epoch = presentation.stream_epoch;
    cursor.journal_generation = presentation.journal_generation;
    cursor.next_serial = next_serial;
    return cursor;
}

void ensure_serial_capacity(std::uint32_t incoming_count)
{
    if (incoming_count == 0 ||
        presentation.next_serial <= UINT64_MAX - incoming_count) {
        return;
    }

    /* A serial space reset is local presentation lifetime management. It does
     * not reset the validated producer stream or its ordering audit. */
    const std::uint64_t resets = presentation.reset_count;
    const std::uint64_t accepted_ranges = presentation.accepted_ranges;
    const std::uint64_t accepted_records = presentation.accepted_records;
    const std::uint64_t rejected_ranges = presentation.rejected_ranges;
    const std::uint64_t empty_carriers = presentation.empty_carriers;
    const std::uint64_t adapter_rejections =
        presentation.adapter_rejections;
    const std::uint64_t overwritten_records =
        presentation.overwritten_records >
                UINT64_MAX - presentation.retained_count
            ? UINT64_MAX
            : presentation.overwritten_records +
                  presentation.retained_count;
    const std::uint64_t audit_ready_records =
        presentation.audit_ready_records;
    const std::uint64_t audit_future_stalls =
        presentation.audit_future_stalls;
    const std::uint64_t audit_overrun_recoveries =
        presentation.audit_overrun_recoveries;
    const std::uint32_t stream_epoch = presentation.stream_epoch;
    std::uint32_t generation = presentation.journal_generation + 1u;
    if (!generation)
        generation = 1u;

    presentation = {};
    presentation.initialized = true;
    presentation.stream_epoch = stream_epoch;
    presentation.journal_generation = generation;
    presentation.next_serial = 1;
    presentation.reset_count = resets;
    presentation.accepted_ranges = accepted_ranges;
    presentation.accepted_records = accepted_records;
    presentation.rejected_ranges = rejected_ranges;
    presentation.empty_carriers = empty_carriers;
    presentation.adapter_rejections = adapter_rejections;
    presentation.overwritten_records = overwritten_records;
    presentation.audit_cursor = make_cursor(1);
    presentation.audit_ready_records = audit_ready_records;
    presentation.audit_future_stalls = audit_future_stalls;
    presentation.audit_overrun_recoveries = audit_overrun_recoveries;
    (void)CG_EventRuntimeResetLegacy(stream_epoch);
}

void reset_v1(uint32_t stream_epoch, uint32_t)
{
    Worr_CGameEventShadowAuditResetV1(&audit_v1, stream_epoch);
}

void consume_v1(const worr_cgame_event_range_v1 *range)
{
    /* Audit only.  No record pointer or record copy survives this callback,
     * and legacy parse_entity_event remains the sole presenter. */
    (void)Worr_CGameEventShadowAuditConsumeV1(&audit_v1, range);
}

bool get_status_v1(worr_cgame_event_shadow_audit_status_v1 *status_out)
{
    return Worr_CGameEventShadowAuditStatusV1(&audit_v1, status_out);
}

const worr_cgame_event_shadow_export_v1 event_shadow_api = {
    sizeof(event_shadow_api),
    WORR_CGAME_EVENT_SHADOW_API_VERSION,
    reset_v1,
    consume_v1,
    get_status_v1,
};

void reset_v2(uint32_t stream_epoch, uint32_t)
{
    if (raw_probe.map_active)
        discard_raw_pending(true);
    Worr_CGameEventRangeAuditResetV2(&audit_v2, stream_epoch);
    reset_presentation(stream_epoch);
    (void)CG_EventRuntimeResetLegacy(stream_epoch);
}

void consume_v2(const worr_cgame_event_range_v2 *range)
{
    /* Validate the complete carrier/chunk ordering contract before copying any
     * record. Decode-time and entity-frame legacy presenters remain
     * authoritative until the durable journal's parity gate is promoted. */
    if (!Worr_CGameEventRangeAuditConsumeV2(&audit_v2, range)) {
        if (presentation.initialized)
            increment_saturated(presentation.rejected_ranges);
        return;
    }

    if (!presentation.initialized || !range ||
        range->stream_epoch != presentation.stream_epoch) {
        increment_saturated(presentation.rejected_ranges);
        return;
    }

    std::array<std::uint64_t, WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2>
        semantic_hashes{};
    for (std::uint32_t index = 0; index < range->count; ++index) {
        if (!Worr_EventRecordSemanticHashV1(
                &range->records[index],
                WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
                &semantic_hashes[index])) {
            increment_saturated(presentation.rejected_ranges);
            return;
        }
    }

    increment_saturated(presentation.accepted_ranges);
    if (range->adapter_status != WORR_CGAME_EVENT_ADAPTER_OK_V2)
        increment_saturated(presentation.adapter_rejections);
    if (range->count == 0) {
        increment_saturated(presentation.empty_carriers);
        return;
    }

    capture_raw_actions(*range);

    ensure_serial_capacity(range->count);
    for (std::uint32_t index = 0; index < range->count; ++index) {
        const std::uint64_t serial = presentation.next_serial++;
        const std::uint32_t slot = static_cast<std::uint32_t>(
            (serial - 1u) % CG_CANONICAL_EVENT_PRESENTATION_CAPACITY);
        auto &entry = presentation.entries[slot];
        entry = {};
        entry.struct_size = sizeof(entry);
        entry.schema_version = CG_CANONICAL_EVENT_PRESENTATION_VERSION;
        entry.stream_epoch = range->stream_epoch;
        entry.batch_generation = range->batch_generation;
        entry.journal_serial = serial;
        entry.carrier_sequence = range->carrier_sequence;
        entry.arrival_ordinal = range->first_arrival_ordinal + index;
        entry.carrier_time_us = range->carrier_time_us;
        entry.semantic_hash = semantic_hashes[index];
        entry.carrier_tick = range->carrier_tick;
        entry.phase = range->phase;
        entry.range_flags = range->flags;
        entry.carrier_kind = range->carrier_kind;
        entry.adapter_status = range->adapter_status;
        entry.record = range->records[index];

        if (presentation.retained_count ==
            CG_CANONICAL_EVENT_PRESENTATION_CAPACITY) {
            increment_saturated(presentation.overwritten_records);
        } else {
            ++presentation.retained_count;
        }
        increment_saturated(presentation.accepted_records);
        /* The runtime retains only this entry's join metadata. The value-owned
         * body remains solely in the presentation history above. */
        (void)CG_EventRuntimeObserveLegacyEntry(&entry);
    }
}

bool get_status_v2(worr_cgame_event_range_audit_status_v2 *status_out)
{
    return Worr_CGameEventRangeAuditStatusV2(&audit_v2, status_out);
}

const worr_cgame_event_range_export_v2 event_range_api_v2 = {
    sizeof(event_range_api_v2),
    WORR_CGAME_EVENT_RANGE_API_VERSION_V2,
    reset_v2,
    consume_v2,
    get_status_v2,
};

} // namespace

const worr_cgame_event_shadow_export_v1 *CG_GetEventShadowAPI()
{
    return &event_shadow_api;
}

const worr_cgame_event_range_export_v2 *CG_GetEventRangeAPIv2()
{
    return &event_range_api_v2;
}

void CG_NativeEventProbeRawBeginMap()
{
    raw_probe = {};
    raw_probe.map_active = true;
}

void CG_NativeEventProbeRawEndMap()
{
    if (!raw_probe.map_active)
        return;
    discard_raw_pending(true);
    raw_probe.map_active = false;
}

void CG_NativeEventProbeRawUninstall()
{
    raw_probe = {};
}

bool CG_NativeEventProbeLegacyActionHash(
    const worr_cgame_event_range_v2 *range,
    std::uint32_t record_index,
    std::uint64_t *hash_out)
{
    constexpr std::uint32_t required_range_flags =
        WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2 |
        WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2 |
        WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |
        WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2 |
        WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2;
    std::uint32_t ignored_kind = 0;
    if (!range || !hash_out ||
        range->struct_size != sizeof(*range) ||
        range->api_version != WORR_CGAME_EVENT_RANGE_API_VERSION_V2 ||
        range->phase !=
            WORR_CGAME_EVENT_RANGE_PHASE_ACTION_PRE_PRESENT_V2 ||
        range->adapter_status != WORR_CGAME_EVENT_ADAPTER_OK_V2 ||
        range->chunk_index != 0 || range->chunk_count != 1 ||
        (range->flags & required_range_flags) != required_range_flags ||
        (range->flags &
         (WORR_CGAME_EVENT_RANGE_CONTINUATION_V2 |
          WORR_CGAME_EVENT_RANGE_ADAPTER_REJECTED_V2)) != 0 ||
        !raw_action_kind(*range, record_index, &ignored_kind)) {
        return false;
    }
    const worr_event_record_v1 &record = range->records[record_index];
    if ((record.flags &
         (WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
          WORR_EVENT_FLAG_SNAPSHOT_FENCED)) != 0 ||
        record.event_id.stream_epoch != 0 ||
        record.event_id.sequence != 0 ||
        record.source_tick == UINT32_MAX ||
        record.source_tick != range->carrier_tick ||
        record.source_time_us != range->carrier_time_us) {
        return false;
    }
    cg_canonical_snapshot_timeline_diagnostics_v1 diagnostics{};
    if (!CG_CanonicalSnapshotTimelineGetDiagnostics(&diagnostics) ||
        !diagnostics.initialized || !diagnostics.active ||
        diagnostics.active_epoch == 0) {
        return false;
    }
    const worr_snapshot_id_v2 snapshot_id{
        diagnostics.active_epoch, record.source_tick + 1u};
    worr_snapshot_v2 snapshot{};
    if (!copy_parity_snapshot(snapshot_id, &snapshot) ||
        snapshot.server_tick != record.source_tick) {
        return false;
    }
    return presentation_projection_hash(
        record, snapshot, hash_out);
}

bool CG_NativeEventProbeAuthorityHash(
    const worr_event_record_v1 *record,
    const cg_event_runtime_presentation_context_v1 *context,
    std::uint64_t *hash_out)
{
    if (!record || !context || !hash_out ||
        context->struct_size != sizeof(*context) ||
        context->schema_version != CG_EVENT_RUNTIME_PRESENTER_VERSION ||
        context->provenance !=
            CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY ||
        context->reserved0 != 0 || context->reserved1 != 0 ||
        (record->flags &
         (WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
          WORR_EVENT_FLAG_SNAPSHOT_FENCED)) !=
            (WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
             WORR_EVENT_FLAG_SNAPSHOT_FENCED) ||
        record->event_id.stream_epoch == 0 ||
        record->event_id.sequence == 0 ||
        record->source_tick == UINT32_MAX) {
        return false;
    }
    const worr_snapshot_id_v2 expected_id{
        context->fence_snapshot_id.epoch, record->source_tick + 1u};
    if (expected_id.epoch == 0 ||
        context->fence_snapshot_id.epoch != expected_id.epoch ||
        context->fence_snapshot_id.sequence != expected_id.sequence) {
        return false;
    }
    worr_snapshot_v2 snapshot{};
    if (!copy_parity_snapshot(expected_id, &snapshot) ||
        snapshot.server_tick != context->fence_tick ||
        snapshot.server_time_us != context->fence_time_us ||
        snapshot.server_tick != record->source_tick) {
        return false;
    }
    return presentation_projection_hash(
        *record, snapshot, hash_out);
}

bool CG_NativeEventProbeCompleteLegacyDispatch(
    std::uint32_t carrier_kind,
    worr_cgame_native_event_probe_legacy_disposition_v1 disposition)
{
    if (!raw_probe.map_active ||
        (disposition !=
             WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED &&
         disposition !=
             WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_SUPPRESSED) ||
        raw_probe.pending_count == 0) {
        increment_saturated(raw_probe.pair_failures);
        return false;
    }

    const auto &token = raw_probe.pending[raw_probe.pending_head];
    if (token.carrier_kind != carrier_kind) {
        increment_saturated(raw_probe.pair_failures);
        return false;
    }

    if (disposition ==
        WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED) {
        increment_saturated(raw_probe.effect_dispatches);
        raw_probe.effect_chain_hash =
            Worr_CGameNativeEventProbeChainAppendV1(
                raw_probe.effect_chain_hash, token.presenter_kind,
                token.semantic_hash);
    } else {
        increment_saturated(raw_probe.effect_suppressions);
    }

    raw_probe.pending[raw_probe.pending_head] = {};
    raw_probe.pending_head =
        (raw_probe.pending_head + 1u) %
        static_cast<std::uint32_t>(raw_probe.pending.size());
    --raw_probe.pending_count;
    if (!raw_probe.pending_count)
        raw_probe.pending_head = 0;
    return true;
}

bool CG_NativeEventProbeFillRawStatus(
    worr_cgame_native_event_probe_status_v1 *status_out)
{
    if (!status_out)
        return false;
    status_out->raw_pending_count = raw_probe.pending_count;
    status_out->raw_action_records = raw_probe.action_records;
    status_out->raw_action_chain_hash = raw_probe.action_chain_hash;
    status_out->raw_effect_dispatches = raw_probe.effect_dispatches;
    status_out->raw_effect_chain_hash = raw_probe.effect_chain_hash;
    status_out->raw_effect_suppressions = raw_probe.effect_suppressions;
    status_out->raw_pair_failures = raw_probe.pair_failures;
    for (std::uint32_t index = 0;
         index < WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT; ++index) {
        status_out->raw_action_by_kind[index] =
            raw_probe.action_by_kind[index];
    }
    return true;
}

bool CG_NativeEventProbeRawCheckpointReady()
{
    return raw_probe.map_active && raw_probe.pending_count == 0 &&
           raw_probe.pair_failures == 0 &&
           raw_probe.effect_suppressions == 0;
}

void CG_NativeEventProbeRawApplyCheckpoint()
{
    /* The presenter calls this only after all cross-lane preconditions have
     * succeeded. Keep lifecycle and the already-empty FIFO intact while
     * starting a fresh comparable action/effect window. */
    if (!CG_NativeEventProbeRawCheckpointReady())
        return;
    raw_probe.action_by_kind = {};
    raw_probe.action_records = 0;
    raw_probe.action_chain_hash = 0;
    raw_probe.effect_dispatches = 0;
    raw_probe.effect_chain_hash = 0;
    raw_probe.effect_suppressions = 0;
    raw_probe.pair_failures = 0;
    raw_probe.parity_armed = true;
}

cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationBegin(
    cg_canonical_event_presentation_cursor_v1 *cursor_out)
{
    if (!cursor_out)
        return CG_CANONICAL_EVENT_PRESENTATION_INVALID_ARGUMENT;
    if (!presentation.initialized)
        return CG_CANONICAL_EVENT_PRESENTATION_UNINITIALIZED;
    *cursor_out = make_cursor(oldest_serial());
    return CG_CANONICAL_EVENT_PRESENTATION_OK;
}

cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationTail(
    cg_canonical_event_presentation_cursor_v1 *cursor_out)
{
    if (!cursor_out)
        return CG_CANONICAL_EVENT_PRESENTATION_INVALID_ARGUMENT;
    if (!presentation.initialized)
        return CG_CANONICAL_EVENT_PRESENTATION_UNINITIALIZED;
    *cursor_out = make_cursor(presentation.next_serial);
    return CG_CANONICAL_EVENT_PRESENTATION_OK;
}

cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationNext(
    const cg_canonical_event_presentation_cursor_v1 *cursor,
    cg_canonical_event_presentation_cursor_v1 *next_cursor_out,
    cg_canonical_event_presentation_entry_v1 *entry_out)
{
    if (!cursor_shape_valid(cursor) || !next_cursor_out || !entry_out)
        return CG_CANONICAL_EVENT_PRESENTATION_INVALID_ARGUMENT;
    if (!presentation.initialized)
        return CG_CANONICAL_EVENT_PRESENTATION_UNINITIALIZED;
    if (cursor->stream_epoch != presentation.stream_epoch ||
        cursor->journal_generation != presentation.journal_generation) {
        return CG_CANONICAL_EVENT_PRESENTATION_STALE_CURSOR;
    }

    const std::uint64_t oldest = oldest_serial();
    if (cursor->next_serial < oldest)
        return CG_CANONICAL_EVENT_PRESENTATION_CURSOR_OVERRUN;
    if (cursor->next_serial == presentation.next_serial)
        return CG_CANONICAL_EVENT_PRESENTATION_EMPTY;
    if (cursor->next_serial > presentation.next_serial)
        return CG_CANONICAL_EVENT_PRESENTATION_CORRUPT;

    const std::uint32_t slot = static_cast<std::uint32_t>(
        (cursor->next_serial - 1u) %
        CG_CANONICAL_EVENT_PRESENTATION_CAPACITY);
    const auto &entry = presentation.entries[slot];
    if (entry.struct_size != sizeof(entry) ||
        entry.schema_version != CG_CANONICAL_EVENT_PRESENTATION_VERSION ||
        entry.stream_epoch != presentation.stream_epoch ||
        entry.journal_serial != cursor->next_serial) {
        return CG_CANONICAL_EVENT_PRESENTATION_CORRUPT;
    }

    *entry_out = entry;
    *next_cursor_out = make_cursor(cursor->next_serial + 1u);
    return CG_CANONICAL_EVENT_PRESENTATION_OK;
}

bool CG_CanonicalEventPresentationGetStatus(
    cg_canonical_event_presentation_status_v1 *status_out)
{
    if (!status_out || !presentation.initialized)
        return false;

    cg_canonical_event_presentation_status_v1 status{};
    status.struct_size = sizeof(status);
    status.schema_version = CG_CANONICAL_EVENT_PRESENTATION_VERSION;
    status.stream_epoch = presentation.stream_epoch;
    status.journal_generation = presentation.journal_generation;
    status.capacity = CG_CANONICAL_EVENT_PRESENTATION_CAPACITY;
    status.retained_count = presentation.retained_count;
    status.oldest_serial = oldest_serial();
    status.next_serial = presentation.next_serial;
    status.reset_count = presentation.reset_count;
    status.accepted_ranges = presentation.accepted_ranges;
    status.accepted_records = presentation.accepted_records;
    status.rejected_ranges = presentation.rejected_ranges;
    status.empty_carriers = presentation.empty_carriers;
    status.adapter_rejections = presentation.adapter_rejections;
    status.overwritten_records = presentation.overwritten_records;
    status.audit_ready_records = presentation.audit_ready_records;
    status.audit_future_stalls = presentation.audit_future_stalls;
    status.audit_overrun_recoveries =
        presentation.audit_overrun_recoveries;
    status.audit_last_ready_serial = presentation.audit_last_ready_serial;
    status.audit_last_render_time_us =
        presentation.audit_last_render_time_us;
    if (!Worr_CGameEventRangeAuditStatusV2(&audit_v2,
                                            &status.range_audit)) {
        return false;
    }
    *status_out = status;
    return true;
}

cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationAdvanceAudit(
    std::uint64_t render_time_us,
    std::uint32_t max_records,
    std::uint32_t *advanced_out)
{
    if (!advanced_out || max_records == 0)
        return CG_CANONICAL_EVENT_PRESENTATION_INVALID_ARGUMENT;
    if (!presentation.initialized)
        return CG_CANONICAL_EVENT_PRESENTATION_UNINITIALIZED;

    std::uint32_t advanced = 0;
    while (advanced < max_records) {
        cg_canonical_event_presentation_cursor_v1 next{};
        cg_canonical_event_presentation_entry_v1 entry{};
        const auto result = CG_CanonicalEventPresentationNext(
            &presentation.audit_cursor, &next, &entry);
        if (result == CG_CANONICAL_EVENT_PRESENTATION_EMPTY) {
            presentation.audit_last_render_time_us = render_time_us;
            *advanced_out = advanced;
            return CG_CANONICAL_EVENT_PRESENTATION_OK;
        }
        if (result == CG_CANONICAL_EVENT_PRESENTATION_CURSOR_OVERRUN) {
            increment_saturated(presentation.audit_overrun_recoveries);
            presentation.audit_cursor = make_cursor(oldest_serial());
            presentation.audit_last_render_time_us = render_time_us;
            *advanced_out = advanced;
            return result;
        }
        if (result != CG_CANONICAL_EVENT_PRESENTATION_OK) {
            *advanced_out = advanced;
            return result;
        }
        if (entry.record.source_time_us > render_time_us) {
            increment_saturated(presentation.audit_future_stalls);
            presentation.audit_last_render_time_us = render_time_us;
            *advanced_out = advanced;
            return CG_CANONICAL_EVENT_PRESENTATION_NOT_READY;
        }

        presentation.audit_cursor = next;
        presentation.audit_last_ready_serial = entry.journal_serial;
        increment_saturated(presentation.audit_ready_records);
        ++advanced;
    }

    presentation.audit_last_render_time_us = render_time_us;
    *advanced_out = advanced;
    return CG_CANONICAL_EVENT_PRESENTATION_OK;
}
