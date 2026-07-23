/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "../../../inc/shared/cgame_event_shadow.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint64_t fnv_offset_basis = UINT64_C(14695981039346656037);
static const uint64_t fnv_prime = UINT64_C(1099511628211);

static uint64_t hash_u8(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * fnv_prime;
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    unsigned int index;
    for (index = 0; index < 4; ++index)
        hash = hash_u8(hash, (uint8_t)(value >> (index * 8)));
    return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int index;
    for (index = 0; index < 8; ++index)
        hash = hash_u8(hash, (uint8_t)(value >> (index * 8)));
    return hash;
}

static uint64_t begin_hash(uint32_t domain)
{
    uint64_t hash = hash_u32(fnv_offset_basis, UINT32_C(0x574f5252));
    return hash_u32(hash, domain);
}

static void increment_u64(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static bool bytes_are_zero(const uint8_t *bytes, size_t count)
{
    size_t index;
    for (index = 0; index < count; ++index) {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

static bool builder_shape_valid(
    const worr_cgame_event_shadow_builder_v1 *builder)
{
    return builder && builder->observed && builder->seen_markers &&
           builder->scratch_records && builder->observed_capacity != 0 &&
           builder->observed_capacity <=
               WORR_CGAME_EVENT_SHADOW_MAX_ENTITIES &&
           builder->scratch_capacity != 0 &&
           builder->scratch_capacity <=
               WORR_CGAME_EVENT_SHADOW_MAX_RECORDS &&
           builder->stream_epoch != 0;
}

bool Worr_CGameEventShadowBuilderInitV1(
    worr_cgame_event_shadow_builder_v1 *builder,
    worr_cgame_event_shadow_observed_v1 *observed,
    uint32_t *seen_markers,
    uint32_t observed_capacity,
    worr_event_record_v1 *scratch_records,
    uint32_t scratch_capacity,
    uint32_t stream_epoch)
{
    if (!builder || !observed || !seen_markers || !scratch_records ||
        observed_capacity == 0 ||
        observed_capacity > WORR_CGAME_EVENT_SHADOW_MAX_ENTITIES ||
        scratch_capacity == 0 ||
        scratch_capacity > WORR_CGAME_EVENT_SHADOW_MAX_RECORDS ||
        stream_epoch == 0) {
        return false;
    }
    memset(builder, 0, sizeof(*builder));
    builder->observed = observed;
    builder->seen_markers = seen_markers;
    builder->scratch_records = scratch_records;
    builder->observed_capacity = observed_capacity;
    builder->scratch_capacity = scratch_capacity;
    return Worr_CGameEventShadowBuilderResetV1(builder, stream_epoch);
}

bool Worr_CGameEventShadowBuilderResetV1(
    worr_cgame_event_shadow_builder_v1 *builder,
    uint32_t stream_epoch)
{
    if (!builder || !builder->observed || !builder->seen_markers ||
        !builder->scratch_records || builder->observed_capacity == 0 ||
        builder->scratch_capacity == 0 || stream_epoch == 0)
        return false;

    memset(builder->observed, 0,
           sizeof(builder->observed[0]) * builder->observed_capacity);
    memset(builder->seen_markers, 0,
           sizeof(builder->seen_markers[0]) * builder->observed_capacity);
    memset(builder->scratch_records, 0,
           sizeof(builder->scratch_records[0]) * builder->scratch_capacity);
    builder->stream_epoch = stream_epoch;
    builder->batch_generation = 0;
    builder->seen_marker = 0;
    builder->next_carrier_ordinal = 1;
    builder->last_source_tick = 0;
    builder->carrier_sequence = 0;
    builder->has_last_source_tick = 0;
    builder->in_callback = 0;
    memset(builder->reserved, 0, sizeof(builder->reserved));
    return true;
}

static bool prevalidate_carriers(
    worr_cgame_event_shadow_builder_v1 *builder,
    const worr_cgame_event_shadow_carrier_v1 *carriers,
    uint32_t carrier_count,
    uint32_t *event_count_out)
{
    uint32_t index;
    uint32_t event_count = 0;
    for (index = 0; index < carrier_count; ++index) {
        const worr_cgame_event_shadow_carrier_v1 *carrier =
            &carriers[index];
        if (carrier->entity_index == 0 ||
            carrier->entity_index >= builder->observed_capacity ||
            carrier->scan_order != index ||
            carrier->raw_event >
                WORR_CGAME_EVENT_SHADOW_MAX_LEGACY_EVENT ||
            builder->seen_markers[carrier->entity_index] != 0) {
            uint32_t clear_index;
            for (clear_index = 0; clear_index < index; ++clear_index) {
                builder->seen_markers[
                    carriers[clear_index].entity_index] = 0;
            }
            return false;
        }
        builder->seen_markers[carrier->entity_index] = 1;
        if (carrier->raw_event != 0)
            ++event_count;
    }
    for (index = 0; index < carrier_count; ++index)
        builder->seen_markers[carriers[index].entity_index] = 0;
    *event_count_out = event_count;
    return true;
}

static void initialize_record(worr_event_record_v1 *record,
                              uint32_t source_tick,
                              uint64_t source_time_us,
                              uint32_t carrier_ordinal,
                              uint32_t entity_index,
                              uint32_t generation,
                              uint32_t raw_event,
                              uint32_t scan_order)
{
    worr_event_payload_u32x4_v1 payload = {
        {raw_event, entity_index, generation, scan_order},
    };
    memset(record, 0, sizeof(*record));
    record->struct_size = sizeof(*record);
    record->schema_version = WORR_EVENT_ABI_VERSION;
    record->model_revision = WORR_EVENT_MODEL_REVISION;
    record->flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                    WORR_EVENT_FLAG_PRESENT_ONCE;
    record->source_tick = source_tick;
    record->source_ordinal = carrier_ordinal;
    record->source_time_us = source_time_us;
    record->source_entity.index = entity_index;
    record->source_entity.generation = generation;
    record->subject_entity.index = WORR_EVENT_NO_ENTITY;
    record->event_type = WORR_EVENT_TYPE_LEGACY_BRIDGE;
    record->delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    record->prediction_class =
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record->expiry_tick = source_tick + 1u;
    record->payload_kind = WORR_EVENT_PAYLOAD_U32X4;
    record->payload_size = sizeof(payload);
    memcpy(record->payload, &payload, sizeof(payload));
}

worr_cgame_event_shadow_build_result_v1
Worr_CGameEventShadowDeliverFrameV1(
    worr_cgame_event_shadow_builder_v1 *builder,
    uint32_t source_tick,
    uint64_t source_time_us,
    const worr_cgame_event_shadow_carrier_v1 *carriers,
    uint32_t carrier_count,
    uint32_t range_flags,
    worr_cgame_event_shadow_consume_fn_v1 consume,
    void *consume_context)
{
    worr_cgame_event_range_v1 range;
    uint32_t event_count;
    uint32_t batch_generation;
    uint32_t index;
    uint32_t output_index = 0;
    uint32_t ordinal;

    if (!builder_shape_valid(builder) || !consume ||
        (carrier_count != 0 && !carriers) ||
        carrier_count > builder->observed_capacity ||
        (range_flags &
         ~(uint32_t)WORR_CGAME_EVENT_SHADOW_RANGE_DEMO_PLAYBACK) != 0) {
        return WORR_CGAME_EVENT_SHADOW_BUILD_INVALID_FRAME;
    }
    if (builder->in_callback)
        return WORR_CGAME_EVENT_SHADOW_BUILD_REENTRANT;
    if (builder->has_last_source_tick) {
        if (source_tick == builder->last_source_tick)
            return WORR_CGAME_EVENT_SHADOW_BUILD_DUPLICATE_FRAME;
        if (source_tick < builder->last_source_tick)
            return WORR_CGAME_EVENT_SHADOW_BUILD_INVALID_FRAME;
    }
    if (!prevalidate_carriers(builder, carriers, carrier_count,
                              &event_count)) {
        return WORR_CGAME_EVENT_SHADOW_BUILD_INVALID_FRAME;
    }
    if (event_count > builder->scratch_capacity)
        return WORR_CGAME_EVENT_SHADOW_BUILD_CAPACITY;
    if (builder->batch_generation == UINT32_MAX ||
        builder->carrier_sequence == UINT64_MAX ||
        builder->next_carrier_ordinal == 0 ||
        (uint64_t)event_count >
            (uint64_t)UINT32_MAX - builder->next_carrier_ordinal + 1u) {
        return WORR_CGAME_EVENT_SHADOW_BUILD_ORDINAL_EXHAUSTED;
    }
    for (index = 0; index < carrier_count; ++index) {
        const worr_cgame_event_shadow_observed_v1 *observed =
            &builder->observed[carriers[index].entity_index];
        if (!observed->present && observed->generation == UINT32_MAX)
            return WORR_CGAME_EVENT_SHADOW_BUILD_GENERATION_EXHAUSTED;
    }

    batch_generation = builder->batch_generation + 1u;
    for (index = 0; index < carrier_count; ++index) {
        worr_cgame_event_shadow_observed_v1 *observed =
            &builder->observed[carriers[index].entity_index];
        if (!observed->present)
            ++observed->generation;
        observed->present = 1;
        observed->last_seen_batch = batch_generation;
    }
    for (index = 1; index < builder->observed_capacity; ++index) {
        worr_cgame_event_shadow_observed_v1 *observed =
            &builder->observed[index];
        if (observed->present &&
            observed->last_seen_batch != batch_generation) {
            observed->present = 0;
        }
    }

    ordinal = builder->next_carrier_ordinal;
    for (index = 0; index < carrier_count; ++index) {
        const worr_cgame_event_shadow_carrier_v1 *carrier =
            &carriers[index];
        if (carrier->raw_event == 0)
            continue;
        initialize_record(
            &builder->scratch_records[output_index++], source_tick,
            source_time_us, ordinal++, carrier->entity_index,
            builder->observed[carrier->entity_index].generation,
            carrier->raw_event, carrier->scan_order);
    }

    memset(&range, 0, sizeof(range));
    range.struct_size = sizeof(range);
    range.api_version = WORR_CGAME_EVENT_SHADOW_API_VERSION;
    range.stream_epoch = builder->stream_epoch;
    range.batch_generation = batch_generation;
    range.carrier_sequence = builder->carrier_sequence + 1u;
    range.first_carrier_ordinal = builder->next_carrier_ordinal;
    range.count = event_count;
    range.phase = WORR_CGAME_EVENT_SHADOW_PHASE_ENTITY_FRAME;
    range.flags = range_flags;
    range.records = builder->scratch_records;

    builder->batch_generation = batch_generation;
    builder->carrier_sequence = range.carrier_sequence;
    builder->next_carrier_ordinal = ordinal;
    builder->last_source_tick = source_tick;
    builder->has_last_source_tick = 1;
    builder->in_callback = 1;
    consume(consume_context, &range);
    builder->in_callback = 0;

    /* Callback-scoped immutable storage: destroy the range contents as soon
     * as the consumer returns so pointer retention cannot appear to work. */
    memset(builder->scratch_records, 0,
           sizeof(builder->scratch_records[0]) * event_count);
    return WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED;
}

uint64_t Worr_CGameEventShadowNormalizedRecordHashV1(
    const worr_event_record_v1 *record)
{
    worr_event_payload_u32x4_v1 payload;
    uint64_t hash;
    if (!record || record->struct_size != sizeof(*record) ||
        record->schema_version != WORR_EVENT_ABI_VERSION ||
        record->model_revision != WORR_EVENT_MODEL_REVISION ||
        record->flags != (WORR_EVENT_FLAG_REPLAY_SAFE |
                          WORR_EVENT_FLAG_PRESENT_ONCE) ||
        record->event_id.stream_epoch != 0 ||
        record->event_id.sequence != 0 ||
        record->source_ordinal == 0 ||
        record->source_entity.index == 0 ||
        record->source_entity.index >=
            WORR_CGAME_EVENT_SHADOW_MAX_ENTITIES ||
        record->source_entity.generation == 0 ||
        record->subject_entity.index != WORR_EVENT_NO_ENTITY ||
        record->subject_entity.generation != 0 ||
        record->event_type != WORR_EVENT_TYPE_LEGACY_BRIDGE ||
        record->delivery_class != WORR_EVENT_DELIVERY_TRANSIENT ||
        record->prediction_class !=
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY ||
        record->prediction_key.command_epoch != 0 ||
        record->prediction_key.command_sequence != 0 ||
        record->prediction_key.emitter_ordinal != 0 ||
        record->prediction_key.lane != 0 ||
        record->expiry_tick != record->source_tick + 1u ||
        record->payload_kind != WORR_EVENT_PAYLOAD_U32X4 ||
        record->payload_size != sizeof(payload) || record->reserved0 != 0 ||
        !bytes_are_zero(record->payload + sizeof(payload),
                        WORR_EVENT_PAYLOAD_CAPACITY - sizeof(payload))) {
        return 0;
    }
    memcpy(&payload, record->payload, sizeof(payload));
    if (payload.value[0] == 0 ||
        payload.value[0] > WORR_CGAME_EVENT_SHADOW_MAX_LEGACY_EVENT ||
        payload.value[1] != record->source_entity.index ||
        payload.value[2] != record->source_entity.generation)
        return 0;
    hash = begin_hash(UINT32_C(0x43455331)); /* CES1 */
    hash = hash_u32(hash, record->source_tick);
    hash = hash_u32(hash, payload.value[0]);
    hash = hash_u32(hash, payload.value[1]);
    return hash_u32(hash, payload.value[3]);
}

void Worr_CGameEventShadowAuditResetV1(
    worr_cgame_event_shadow_audit_v1 *audit,
    uint32_t stream_epoch)
{
    uint64_t reset_count;
    if (!audit || stream_epoch == 0)
        return;
    reset_count = audit->initialized ? audit->status.reset_count : 0;
    memset(audit, 0, sizeof(*audit));
    audit->initialized = 1;
    audit->status.struct_size = sizeof(audit->status);
    audit->status.api_version = WORR_CGAME_EVENT_SHADOW_API_VERSION;
    audit->status.stream_epoch = stream_epoch;
    audit->status.reset_count = reset_count;
    increment_u64(&audit->status.reset_count);
    audit->status.normalized_chain_hash =
        begin_hash(UINT32_C(0x43454331)); /* CEC1 */
}

bool Worr_CGameEventShadowAuditConsumeV1(
    worr_cgame_event_shadow_audit_v1 *audit,
    const worr_cgame_event_range_v1 *range)
{
    uint64_t batch_hash;
    uint64_t chain_hash;
    uint32_t index;
    bool valid;

    valid = audit && audit->initialized && range &&
            range->struct_size == sizeof(*range) &&
            range->api_version == WORR_CGAME_EVENT_SHADOW_API_VERSION &&
            range->stream_epoch == audit->status.stream_epoch &&
            range->batch_generation != 0 &&
            range->batch_generation > audit->status.last_batch_generation &&
            range->carrier_sequence >
                audit->status.last_carrier_sequence &&
            range->count <= WORR_CGAME_EVENT_SHADOW_MAX_RECORDS &&
            (range->count == 0 || range->records) &&
            range->phase ==
                WORR_CGAME_EVENT_SHADOW_PHASE_ENTITY_FRAME &&
            (range->flags &
             ~(uint32_t)WORR_CGAME_EVENT_SHADOW_RANGE_DEMO_PLAYBACK) == 0;
    batch_hash = begin_hash(UINT32_C(0x43454231)); /* CEB1 */
    batch_hash = hash_u32(batch_hash, range ? range->count : 0);

    if (valid) {
        for (index = 0; index < range->count; ++index) {
            const uint64_t record_hash =
                Worr_CGameEventShadowNormalizedRecordHashV1(
                    &range->records[index]);
            if (record_hash == 0 ||
                range->records[index].source_ordinal !=
                    range->first_carrier_ordinal + index) {
                valid = false;
                break;
            }
            batch_hash = hash_u64(batch_hash, record_hash);
        }
    }
    if (!valid) {
        if (audit && audit->initialized)
            increment_u64(&audit->status.rejected_batches);
        return false;
    }

    chain_hash = begin_hash(UINT32_C(0x43454831)); /* CEH1 */
    chain_hash = hash_u64(chain_hash,
                          audit->status.normalized_chain_hash);
    chain_hash = hash_u64(chain_hash, batch_hash);
    audit->status.last_batch_generation = range->batch_generation;
    audit->status.last_carrier_sequence = range->carrier_sequence;
    increment_u64(&audit->status.accepted_batches);
    if (UINT64_MAX - audit->status.accepted_records < range->count)
        audit->status.accepted_records = UINT64_MAX;
    else
        audit->status.accepted_records += range->count;
    audit->status.last_batch_hash = batch_hash;
    audit->status.normalized_chain_hash = chain_hash;
    return true;
}

bool Worr_CGameEventShadowAuditStatusV1(
    const worr_cgame_event_shadow_audit_v1 *audit,
    worr_cgame_event_shadow_audit_status_v1 *status_out)
{
    if (!audit || !audit->initialized || !status_out)
        return false;
    *status_out = audit->status;
    return true;
}

/* ------------------------------------------------------------------------- */
/* V2 typed, decode-order event-range accumulator.                            */

#define V2_CALLER_RANGE_FLAGS                                                \
    ((uint32_t)(WORR_CGAME_EVENT_RANGE_DEMO_PLAYBACK_V2 |                   \
                WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2))

#define V2_ALL_RANGE_FLAGS                                                   \
    ((uint32_t)(V2_CALLER_RANGE_FLAGS |                                     \
                WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2 |            \
                WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2 |                 \
                WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2 |      \
                WORR_CGAME_EVENT_RANGE_ADAPTER_REJECTED_V2 |                \
                WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |                     \
                WORR_CGAME_EVENT_RANGE_CONTINUATION_V2 |                    \
                WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2 |                      \
                WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2))

typedef struct v2_byte_range_s {
    uintptr_t begin;
    uintptr_t end;
} v2_byte_range;

static bool v2_make_byte_range(const void *pointer,
                               size_t element_size,
                               uint32_t element_count,
                               size_t alignment,
                               v2_byte_range *range_out)
{
    uintptr_t begin;
    size_t byte_count;

    if (!pointer || !range_out || element_size == 0 || element_count == 0 ||
        alignment == 0 || (uintptr_t)pointer % alignment != 0 ||
        element_count > SIZE_MAX / element_size) {
        return false;
    }
    byte_count = element_size * element_count;
    begin = (uintptr_t)pointer;
    if (byte_count > UINTPTR_MAX || begin > UINTPTR_MAX - byte_count)
        return false;
    range_out->begin = begin;
    range_out->end = begin + byte_count;
    return true;
}

static bool v2_ranges_disjoint(v2_byte_range left, v2_byte_range right)
{
    return left.end <= right.begin || right.end <= left.begin;
}

static bool v2_storage_shape_valid(
    const worr_cgame_event_range_builder_v2 *builder,
    const worr_cgame_event_observed_v2 *observed,
    const uint32_t *seen_markers,
    uint32_t observed_capacity,
    const worr_event_record_v1 *scratch_records,
    uint32_t scratch_capacity)
{
    v2_byte_range builder_range;
    v2_byte_range observed_range;
    v2_byte_range marker_range;
    v2_byte_range scratch_range;

    if (observed_capacity <= 1 ||
        observed_capacity > WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2 ||
        scratch_capacity == 0 ||
        scratch_capacity > WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2 ||
        !v2_make_byte_range(builder, sizeof(*builder), 1,
                            _Alignof(worr_cgame_event_range_builder_v2),
                            &builder_range) ||
        !v2_make_byte_range(observed, sizeof(*observed), observed_capacity,
                            _Alignof(worr_cgame_event_observed_v2),
                            &observed_range) ||
        !v2_make_byte_range(seen_markers, sizeof(*seen_markers),
                            observed_capacity, _Alignof(uint32_t),
                            &marker_range) ||
        !v2_make_byte_range(scratch_records, sizeof(*scratch_records),
                            scratch_capacity,
                            _Alignof(worr_event_record_v1),
                            &scratch_range)) {
        return false;
    }

    return v2_ranges_disjoint(builder_range, observed_range) &&
           v2_ranges_disjoint(builder_range, marker_range) &&
           v2_ranges_disjoint(builder_range, scratch_range) &&
           v2_ranges_disjoint(observed_range, marker_range) &&
           v2_ranges_disjoint(observed_range, scratch_range) &&
           v2_ranges_disjoint(marker_range, scratch_range);
}

static bool v2_external_storage_disjoint(
    const worr_cgame_event_range_builder_v2 *builder,
    const void *external,
    size_t element_size,
    uint32_t element_count,
    size_t alignment)
{
    v2_byte_range builder_range;
    v2_byte_range observed_range;
    v2_byte_range marker_range;
    v2_byte_range scratch_range;
    v2_byte_range external_range;

    if (!v2_storage_shape_valid(
            builder, builder->observed, builder->seen_markers,
            builder->observed_capacity, builder->scratch_records,
            builder->scratch_capacity) ||
        !v2_make_byte_range(builder, sizeof(*builder), 1,
                            _Alignof(worr_cgame_event_range_builder_v2),
                            &builder_range) ||
        !v2_make_byte_range(builder->observed, sizeof(builder->observed[0]),
                            builder->observed_capacity,
                            _Alignof(worr_cgame_event_observed_v2),
                            &observed_range) ||
        !v2_make_byte_range(builder->seen_markers,
                            sizeof(builder->seen_markers[0]),
                            builder->observed_capacity, _Alignof(uint32_t),
                            &marker_range) ||
        !v2_make_byte_range(builder->scratch_records,
                            sizeof(builder->scratch_records[0]),
                            builder->scratch_capacity,
                            _Alignof(worr_event_record_v1), &scratch_range) ||
        !v2_make_byte_range(external, element_size, element_count, alignment,
                            &external_range)) {
        return false;
    }

    return v2_ranges_disjoint(external_range, builder_range) &&
           v2_ranges_disjoint(external_range, observed_range) &&
           v2_ranges_disjoint(external_range, marker_range) &&
           v2_ranges_disjoint(external_range, scratch_range);
}

static bool v2_caller_range_flags_valid(uint32_t range_flags)
{
    return (range_flags & ~V2_CALLER_RANGE_FLAGS) == 0 &&
           ((range_flags & WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2) == 0 ||
            (range_flags & WORR_CGAME_EVENT_RANGE_DEMO_PLAYBACK_V2) != 0);
}

static bool v2_builder_shape_valid(
    const worr_cgame_event_range_builder_v2 *builder)
{
    return builder &&
           v2_storage_shape_valid(
               builder, builder->observed, builder->seen_markers,
               builder->observed_capacity, builder->scratch_records,
               builder->scratch_capacity) &&
           builder->stream_epoch != 0 &&
           builder->next_arrival_ordinal != 0;
}

static bool v2_action_kind_valid(uint32_t carrier_kind)
{
    return carrier_kind >= WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2 &&
           carrier_kind <= WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2;
}

static bool v2_carrier_kind_valid(uint32_t carrier_kind)
{
    return carrier_kind >= WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2 &&
           carrier_kind <= WORR_CGAME_EVENT_CARRIER_KIND_COUNT_V2;
}

static bool v2_adapter_status_valid(uint32_t adapter_status)
{
    return adapter_status <=
           WORR_CGAME_EVENT_ADAPTER_PAYLOAD_INVALID_V2;
}

bool Worr_CGameEventRangeBuilderInitV2(
    worr_cgame_event_range_builder_v2 *builder,
    worr_cgame_event_observed_v2 *observed,
    uint32_t *seen_markers,
    uint32_t observed_capacity,
    worr_event_record_v1 *scratch_records,
    uint32_t scratch_capacity,
    uint32_t stream_epoch)
{
    if (stream_epoch == 0 ||
        !v2_storage_shape_valid(builder, observed, seen_markers,
                                observed_capacity, scratch_records,
                                scratch_capacity)) {
        return false;
    }

    memset(builder, 0, sizeof(*builder));
    builder->observed = observed;
    builder->seen_markers = seen_markers;
    builder->scratch_records = scratch_records;
    builder->observed_capacity = observed_capacity;
    builder->scratch_capacity = scratch_capacity;
    return Worr_CGameEventRangeBuilderResetV2(builder, stream_epoch);
}

bool Worr_CGameEventRangeBuilderResetV2(
    worr_cgame_event_range_builder_v2 *builder,
    uint32_t stream_epoch)
{
    if (!builder || stream_epoch == 0 ||
        !v2_storage_shape_valid(
            builder, builder->observed, builder->seen_markers,
            builder->observed_capacity, builder->scratch_records,
            builder->scratch_capacity)) {
        return false;
    }

    memset(builder->observed, 0,
           sizeof(builder->observed[0]) * builder->observed_capacity);
    memset(builder->seen_markers, 0,
           sizeof(builder->seen_markers[0]) * builder->observed_capacity);
    memset(builder->scratch_records, 0,
           sizeof(builder->scratch_records[0]) * builder->scratch_capacity);
    builder->observed[0].generation = 1;
    builder->observed[0].present = 1;
    builder->stream_epoch = stream_epoch;
    builder->batch_generation = 0;
    builder->last_frame_tick = 0;
    builder->carrier_sequence = 0;
    builder->next_arrival_ordinal = 1;
    builder->has_last_frame_tick = 0;
    builder->in_callback = 0;
    memset(builder->reserved, 0, sizeof(builder->reserved));
    return true;
}

static worr_cgame_event_range_build_result_v2 v2_preview_entity_ref(
    const worr_cgame_event_range_builder_v2 *builder,
    uint32_t entity_index,
    bool allow_absent,
    worr_event_entity_ref_v1 *ref_out)
{
    const worr_cgame_event_observed_v2 *observed;

    if (!ref_out)
        return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
    if (entity_index == WORR_EVENT_NO_ENTITY) {
        if (!allow_absent)
            return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
        ref_out->index = WORR_EVENT_NO_ENTITY;
        ref_out->generation = 0;
        return WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2;
    }
    if (entity_index >= builder->observed_capacity)
        return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
    if (entity_index == 0) {
        ref_out->index = 0;
        ref_out->generation = 1;
        return WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2;
    }

    observed = &builder->observed[entity_index];
    ref_out->index = entity_index;
    if (observed->present || observed->provisional) {
        if (observed->generation == 0)
            return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
        ref_out->generation = observed->generation;
        return WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2;
    }
    if (observed->generation == UINT32_MAX)
        return WORR_CGAME_EVENT_RANGE_BUILD_GENERATION_EXHAUSTED_V2;
    ref_out->generation = observed->generation + 1u;
    return WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2;
}

static void v2_commit_action_ref(
    worr_cgame_event_range_builder_v2 *builder,
    uint32_t entity_index)
{
    worr_cgame_event_observed_v2 *observed;
    if (entity_index == 0 || entity_index == WORR_EVENT_NO_ENTITY)
        return;
    observed = &builder->observed[entity_index];
    if (!observed->present && !observed->provisional) {
        ++observed->generation;
        observed->provisional = 1;
    }
}

static bool v2_candidate_template_valid(
    const worr_cgame_event_action_candidate_v2 *candidate,
    uint32_t expected_source_ordinal)
{
    const worr_event_record_v1 *record;
    if (!candidate || candidate->struct_size != sizeof(*candidate) ||
        candidate->reserved0 != 0)
        return false;
    record = &candidate->record;
    return record->source_entity.index == WORR_EVENT_NO_ENTITY &&
           record->source_entity.generation == 0 &&
           record->subject_entity.index == WORR_EVENT_NO_ENTITY &&
           record->subject_entity.generation == 0 &&
           record->event_id.stream_epoch == 0 &&
           record->event_id.sequence == 0 &&
           (record->flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0 &&
           record->source_ordinal == expected_source_ordinal &&
           record->prediction_class ==
               WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY &&
           record->prediction_key.command_epoch == 0 &&
           record->prediction_key.command_sequence == 0 &&
           record->prediction_key.emitter_ordinal == 0 &&
           record->prediction_key.lane == 0;
}

static bool v2_record_matches_carrier_kind(
    const worr_event_record_v1 *record,
    uint32_t carrier_kind)
{
    worr_event_payload_muzzle_v1 muzzle;

    if (!record)
        return false;
    switch (carrier_kind) {
    case WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2:
        return record->payload_kind ==
               WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1;
    case WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2:
        return record->payload_kind == WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1;
    case WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2:
    case WORR_CGAME_EVENT_CARRIER_MONSTER_MUZZLE_V2:
        if (record->payload_kind != WORR_EVENT_PAYLOAD_MUZZLE_V1 ||
            record->payload_size != sizeof(muzzle)) {
            return false;
        }
        memcpy(&muzzle, record->payload, sizeof(muzzle));
        return muzzle.family ==
               (carrier_kind == WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2
                    ? WORR_EVENT_MUZZLE_FAMILY_PLAYER
                    : WORR_EVENT_MUZZLE_FAMILY_MONSTER);
    case WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2:
        return record->payload_kind ==
               WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1;
    case WORR_CGAME_EVENT_CARRIER_DAMAGE_V2:
        return record->payload_kind == WORR_EVENT_PAYLOAD_DAMAGE;
    case WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2:
        return record->payload_kind ==
               WORR_EVENT_PAYLOAD_KEYED_POI_V1;
    default:
        return false;
    }
}

static bool v2_legacy_record_identity_valid(
    const worr_event_record_v1 *record)
{
    return record &&
           (record->flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0 &&
           record->event_id.stream_epoch == 0 &&
           record->event_id.sequence == 0 &&
           record->prediction_class ==
               WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY &&
           record->prediction_key.command_epoch == 0 &&
           record->prediction_key.command_sequence == 0 &&
           record->prediction_key.emitter_ordinal == 0 &&
           record->prediction_key.lane == 0;
}

static void v2_invoke_range(
    worr_cgame_event_range_builder_v2 *builder,
    worr_cgame_event_range_v2 *range,
    worr_cgame_event_range_consume_fn_v2 consume,
    void *consume_context)
{
    builder->in_callback = 1;
    consume(consume_context, range);
    builder->in_callback = 0;
    if (range->count != 0) {
        memset(builder->scratch_records, 0,
               sizeof(builder->scratch_records[0]) * range->count);
    }
}

static void v2_initialize_range(
    const worr_cgame_event_range_builder_v2 *builder,
    worr_cgame_event_range_v2 *range,
    uint32_t carrier_tick,
    uint64_t carrier_time_us,
    uint32_t phase,
    uint32_t flags,
    uint32_t carrier_kind,
    uint32_t adapter_status,
    uint32_t chunk_index,
    uint32_t chunk_count,
    uint64_t first_arrival_ordinal,
    uint32_t count)
{
    memset(range, 0, sizeof(*range));
    range->struct_size = sizeof(*range);
    range->api_version = WORR_CGAME_EVENT_RANGE_API_VERSION_V2;
    range->stream_epoch = builder->stream_epoch;
    range->batch_generation = builder->batch_generation;
    range->carrier_sequence = builder->carrier_sequence;
    range->first_arrival_ordinal = first_arrival_ordinal;
    range->carrier_time_us = carrier_time_us;
    range->carrier_tick = carrier_tick;
    range->count = count;
    range->phase = phase;
    range->flags = flags;
    range->carrier_kind = carrier_kind;
    range->adapter_status = adapter_status;
    range->chunk_index = chunk_index;
    range->chunk_count = chunk_count;
    range->records = count != 0 ? builder->scratch_records : NULL;
}

static bool v2_cursor_can_advance(
    const worr_cgame_event_range_builder_v2 *builder,
    uint64_t arrival_count)
{
    return builder->batch_generation != UINT32_MAX &&
           builder->carrier_sequence != UINT64_MAX &&
           arrival_count != 0 && arrival_count <=
               UINT64_MAX - builder->next_arrival_ordinal;
}

worr_cgame_event_range_build_result_v2
Worr_CGameEventRangeDeliverActionV2(
    worr_cgame_event_range_builder_v2 *builder,
    const worr_cgame_event_action_candidate_v2 *candidate,
    uint32_t carrier_kind,
    uint32_t range_flags,
    worr_cgame_event_range_consume_fn_v2 consume,
    void *consume_context)
{
    return Worr_CGameEventRangeDeliverActionBatchV2(
        builder, candidate, 1, carrier_kind, range_flags, consume,
        consume_context);
}

static bool v2_action_record_shape_valid(
    const worr_event_record_v1 *record, uint32_t carrier_kind)
{
    if (!record || !v2_record_matches_carrier_kind(record, carrier_kind))
        return false;
    if ((carrier_kind == WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2 ||
         carrier_kind == WORR_CGAME_EVENT_CARRIER_MONSTER_MUZZLE_V2) &&
        (record->source_entity.index == 0 ||
         record->subject_entity.index != WORR_EVENT_NO_ENTITY)) {
        return false;
    }
    if ((carrier_kind == WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 ||
         carrier_kind == WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2) &&
        (record->source_entity.index != 0 ||
         record->subject_entity.index == 0 ||
         record->subject_entity.index == WORR_EVENT_NO_ENTITY)) {
        return false;
    }
    return true;
}

worr_cgame_event_range_build_result_v2
Worr_CGameEventRangeDeliverActionBatchV2(
    worr_cgame_event_range_builder_v2 *builder,
    const worr_cgame_event_action_candidate_v2 *candidates,
    uint32_t candidate_count,
    uint32_t carrier_kind,
    uint32_t range_flags,
    worr_cgame_event_range_consume_fn_v2 consume,
    void *consume_context)
{
    worr_cgame_event_range_build_result_v2 result;
    worr_cgame_event_range_v2 range;
    worr_event_entity_ref_v1 source_ref;
    worr_event_entity_ref_v1 subject_ref;
    worr_event_record_v1 record;
    uint32_t flags;
    uint32_t index;

    if (!v2_builder_shape_valid(builder) || !consume ||
        !v2_action_kind_valid(carrier_kind) || candidate_count == 0 ||
        candidate_count > builder->scratch_capacity ||
        (carrier_kind == WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 &&
         candidate_count > WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2) ||
        (carrier_kind != WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 &&
         candidate_count != 1) ||
        !v2_caller_range_flags_valid(range_flags) ||
        !v2_external_storage_disjoint(
            builder, candidates, sizeof(*candidates), candidate_count,
            _Alignof(worr_cgame_event_action_candidate_v2))) {
        return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
    }
    if (builder->in_callback)
        return WORR_CGAME_EVENT_RANGE_BUILD_REENTRANT_V2;
    if (!v2_cursor_can_advance(builder, candidate_count))
        return WORR_CGAME_EVENT_RANGE_BUILD_ORDER_EXHAUSTED_V2;

    /* First pass is failure-atomic: preview and validate the complete carrier
     * without touching generations, cursors, or scratch storage. */
    for (index = 0; index < candidate_count; ++index) {
        const worr_cgame_event_action_candidate_v2 *candidate =
            &candidates[index];
        if (!v2_candidate_template_valid(candidate, index))
            return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
        if (index != 0 &&
            (candidate->record.source_tick !=
                 candidates[0].record.source_tick ||
             candidate->record.source_time_us !=
                 candidates[0].record.source_time_us ||
             (carrier_kind == WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 &&
              candidate->subject_entity_index !=
                  candidates[0].subject_entity_index))) {
            return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
        }
        result = v2_preview_entity_ref(
            builder, candidate->source_entity_index, false, &source_ref);
        if (result != WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2)
            return result;
        result = v2_preview_entity_ref(
            builder, candidate->subject_entity_index, true, &subject_ref);
        if (result != WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2)
            return result;
        record = candidate->record;
        record.source_entity = source_ref;
        record.subject_entity = subject_ref;
        if (!Worr_EventRecordCandidateValidateV1(
                &record, builder->observed_capacity) ||
            !v2_action_record_shape_valid(&record, carrier_kind)) {
            return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
        }
    }

    flags = range_flags |
            WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2 |
            WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2 |
            WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |
            WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2 |
            WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2;
    for (index = 0; index < candidate_count; ++index) {
        const worr_cgame_event_action_candidate_v2 *candidate =
            &candidates[index];
        /* These previews cannot fail after the unchanged first pass. */
        (void)v2_preview_entity_ref(
            builder, candidate->source_entity_index, false, &source_ref);
        (void)v2_preview_entity_ref(
            builder, candidate->subject_entity_index, true, &subject_ref);
        record = candidate->record;
        record.source_entity = source_ref;
        record.subject_entity = subject_ref;
        builder->scratch_records[index] = record;
        v2_commit_action_ref(builder, candidate->source_entity_index);
        v2_commit_action_ref(builder, candidate->subject_entity_index);
        if (candidate->source_entity_index != 0 ||
            (candidate->subject_entity_index != WORR_EVENT_NO_ENTITY &&
             candidate->subject_entity_index != 0)) {
            flags |= WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2;
        }
    }
    ++builder->batch_generation;
    ++builder->carrier_sequence;
    v2_initialize_range(
        builder, &range, candidates[0].record.source_tick,
        candidates[0].record.source_time_us,
        WORR_CGAME_EVENT_RANGE_PHASE_ACTION_PRE_PRESENT_V2, flags,
        carrier_kind, WORR_CGAME_EVENT_ADAPTER_OK_V2, 0, 1,
        builder->next_arrival_ordinal, candidate_count);
    builder->next_arrival_ordinal += candidate_count;
    v2_invoke_range(builder, &range, consume, consume_context);
    return WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2;
}

worr_cgame_event_range_build_result_v2
Worr_CGameEventRangeDeliverRejectedActionV2(
    worr_cgame_event_range_builder_v2 *builder,
    uint32_t carrier_tick,
    uint64_t carrier_time_us,
    uint32_t carrier_kind,
    uint32_t adapter_status,
    uint32_t range_flags,
    worr_cgame_event_range_consume_fn_v2 consume,
    void *consume_context)
{
    worr_cgame_event_range_v2 range;
    uint32_t flags;

    if (!v2_builder_shape_valid(builder) || !consume ||
        !v2_action_kind_valid(carrier_kind) ||
        !v2_adapter_status_valid(adapter_status) ||
        adapter_status == WORR_CGAME_EVENT_ADAPTER_OK_V2 ||
        !v2_caller_range_flags_valid(range_flags)) {
        return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
    }
    if (builder->in_callback)
        return WORR_CGAME_EVENT_RANGE_BUILD_REENTRANT_V2;
    if (!v2_cursor_can_advance(builder, 1))
        return WORR_CGAME_EVENT_RANGE_BUILD_ORDER_EXHAUSTED_V2;

    ++builder->batch_generation;
    ++builder->carrier_sequence;
    flags = range_flags |
            WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2 |
            WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2 |
            WORR_CGAME_EVENT_RANGE_ADAPTER_REJECTED_V2 |
            WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |
            WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2 |
            WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2;
    v2_initialize_range(
        builder, &range, carrier_tick, carrier_time_us,
        WORR_CGAME_EVENT_RANGE_PHASE_ACTION_PRE_PRESENT_V2, flags,
        carrier_kind, adapter_status, 0, 1,
        builder->next_arrival_ordinal, 0);
    ++builder->next_arrival_ordinal;
    v2_invoke_range(builder, &range, consume, consume_context);
    return WORR_CGAME_EVENT_RANGE_BUILD_REJECTED_V2;
}

static bool v2_initialize_legacy_entity_record(
    worr_event_record_v1 *record,
    uint32_t source_tick,
    uint64_t source_time_us,
    uint32_t source_ordinal,
    worr_event_entity_ref_v1 source_entity,
    uint32_t raw_event)
{
    worr_event_payload_legacy_entity_v1 payload;
    uint16_t event_type;
    uint16_t payload_flags;

    switch (raw_event) {
    case WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN:
        event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
        payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
        break;
    case WORR_EVENT_LEGACY_ENTITY_FOOTSTEP:
    case WORR_EVENT_LEGACY_ENTITY_FALL_SHORT:
    case WORR_EVENT_LEGACY_ENTITY_FALL_MEDIUM:
    case WORR_EVENT_LEGACY_ENTITY_FALL_FAR:
    case WORR_EVENT_LEGACY_ENTITY_OTHER_FOOTSTEP:
    case WORR_EVENT_LEGACY_ENTITY_LADDER_STEP:
        event_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
        payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
        break;
    case WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT:
        event_type = WORR_EVENT_TYPE_STATE_CHANGE;
        payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION |
                        WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY;
        break;
    case WORR_EVENT_LEGACY_ENTITY_OTHER_TELEPORT:
        event_type = WORR_EVENT_TYPE_STATE_CHANGE;
        payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY;
        break;
    default:
        return false;
    }

    memset(record, 0, sizeof(*record));
    memset(&payload, 0, sizeof(payload));
    payload.raw_event = (uint16_t)raw_event;
    payload.flags = payload_flags;
    record->struct_size = sizeof(*record);
    record->schema_version = WORR_EVENT_ABI_VERSION;
    record->model_revision = WORR_EVENT_MODEL_REVISION;
    record->flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                    WORR_EVENT_FLAG_PRESENT_ONCE;
    record->source_tick = source_tick;
    record->source_ordinal = source_ordinal;
    record->source_time_us = source_time_us;
    record->source_entity = source_entity;
    record->subject_entity.index = WORR_EVENT_NO_ENTITY;
    record->event_type = event_type;
    record->delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    record->prediction_class =
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record->expiry_tick = source_tick + 1u;
    record->payload_kind = WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1;
    record->payload_size = sizeof(payload);
    memcpy(record->payload, &payload, sizeof(payload));
    return true;
}

static worr_cgame_event_range_build_result_v2
v2_prevalidate_frame(
    worr_cgame_event_range_builder_v2 *builder,
    uint32_t source_tick,
    uint64_t source_time_us,
    const worr_cgame_event_carrier_v2 *carriers,
    uint32_t carrier_count,
    uint32_t *event_count_out,
    bool *adapter_rejected_out,
    uint32_t *adapter_status_out)
{
    uint32_t index;
    uint32_t marked_count = 0;
    uint32_t event_count = 0;
    worr_cgame_event_range_build_result_v2 result =
        WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2;
    bool adapter_rejected = false;
    uint32_t adapter_status = WORR_CGAME_EVENT_ADAPTER_OK_V2;

    for (index = 0; index < carrier_count; ++index) {
        worr_event_entity_ref_v1 source_ref;
        worr_event_record_v1 record;
        const worr_cgame_event_carrier_v2 *carrier = &carriers[index];

        if (carrier->entity_index == 0 ||
            carrier->entity_index >= builder->observed_capacity ||
            carrier->scan_order != index ||
            builder->seen_markers[carrier->entity_index] != 0) {
            result = WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
            break;
        }
        builder->seen_markers[carrier->entity_index] = 1;
        ++marked_count;
        result = v2_preview_entity_ref(builder, carrier->entity_index,
                                       false, &source_ref);
        if (result != WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2)
            break;
        if (carrier->raw_event == 0)
            continue;
        if (carrier->raw_event > WORR_EVENT_LEGACY_ENTITY_LADDER_STEP) {
            adapter_rejected = true;
            adapter_status = WORR_CGAME_EVENT_ADAPTER_UNSUPPORTED_ID_V2;
            continue;
        }
        if (!v2_initialize_legacy_entity_record(
                &record, source_tick, source_time_us, carrier->scan_order,
                source_ref, carrier->raw_event) ||
            !Worr_EventRecordCandidateValidateV1(
                &record, builder->observed_capacity)) {
            adapter_rejected = true;
            adapter_status = WORR_CGAME_EVENT_ADAPTER_PAYLOAD_INVALID_V2;
            continue;
        }
        ++event_count;
    }

    for (index = 0; index < marked_count; ++index)
        builder->seen_markers[carriers[index].entity_index] = 0;
    if (result != WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2)
        return result;
    *event_count_out = event_count;
    *adapter_rejected_out = adapter_rejected;
    *adapter_status_out = adapter_status;
    return WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2;
}

static void v2_commit_frame_lifecycle(
    worr_cgame_event_range_builder_v2 *builder,
    const worr_cgame_event_carrier_v2 *carriers,
    uint32_t carrier_count,
    uint32_t batch_generation)
{
    uint32_t index;
    for (index = 0; index < carrier_count; ++index) {
        worr_cgame_event_observed_v2 *observed =
            &builder->observed[carriers[index].entity_index];
        if (!observed->present && !observed->provisional)
            ++observed->generation;
        observed->present = 1;
        observed->provisional = 0;
        observed->last_seen_batch = batch_generation;
    }
    for (index = 1; index < builder->observed_capacity; ++index) {
        worr_cgame_event_observed_v2 *observed = &builder->observed[index];
        if (observed->last_seen_batch != batch_generation) {
            observed->present = 0;
            observed->provisional = 0;
        }
    }
}

worr_cgame_event_range_build_result_v2
Worr_CGameEventRangeDeliverFrameV2(
    worr_cgame_event_range_builder_v2 *builder,
    uint32_t source_tick,
    uint64_t source_time_us,
    const worr_cgame_event_carrier_v2 *carriers,
    uint32_t carrier_count,
    uint32_t range_flags,
    worr_cgame_event_range_consume_fn_v2 consume,
    void *consume_context)
{
    worr_cgame_event_range_build_result_v2 result;
    uint32_t event_count;
    uint32_t adapter_status;
    uint32_t arrival_count;
    uint32_t chunk_count;
    uint32_t chunk_index = 0;
    uint32_t output_count = 0;
    uint32_t emitted_count = 0;
    uint32_t index;
    uint32_t flags;
    uint64_t arrival_ordinal;
    bool adapter_rejected;
    worr_cgame_event_range_v2 range;

    if (!v2_builder_shape_valid(builder) || !consume ||
        (carrier_count != 0 &&
         (!carriers ||
          !v2_external_storage_disjoint(
              builder, carriers, sizeof(carriers[0]), carrier_count,
              _Alignof(worr_cgame_event_carrier_v2)))) ||
        carrier_count > builder->observed_capacity ||
        !v2_caller_range_flags_valid(range_flags)) {
        return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
    }
    if (builder->in_callback)
        return WORR_CGAME_EVENT_RANGE_BUILD_REENTRANT_V2;
    if (builder->has_last_frame_tick) {
        if (source_tick == builder->last_frame_tick)
            return WORR_CGAME_EVENT_RANGE_BUILD_DUPLICATE_FRAME_V2;
        if (source_tick < builder->last_frame_tick)
            return WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2;
    }

    result = v2_prevalidate_frame(
        builder, source_tick, source_time_us, carriers, carrier_count,
        &event_count, &adapter_rejected, &adapter_status);
    if (result != WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2)
        return result;
    arrival_count = event_count == 0 ? 1u : event_count;
    if (!v2_cursor_can_advance(builder, arrival_count)) {
        return WORR_CGAME_EVENT_RANGE_BUILD_ORDER_EXHAUSTED_V2;
    }

    ++builder->batch_generation;
    ++builder->carrier_sequence;
    builder->last_frame_tick = source_tick;
    builder->has_last_frame_tick = 1;
    v2_commit_frame_lifecycle(builder, carriers, carrier_count,
                              builder->batch_generation);

    flags = range_flags |
            WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2 |
            WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2;
    if (adapter_rejected) {
        flags |= WORR_CGAME_EVENT_RANGE_ADAPTER_REJECTED_V2 |
                 WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |
                 WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2;
        v2_initialize_range(
            builder, &range, source_tick, source_time_us,
            WORR_CGAME_EVENT_RANGE_PHASE_ENTITY_FRAME_POST_PRESENT_V2,
            flags, WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2,
            adapter_status, 0, 1, builder->next_arrival_ordinal, 0);
        ++builder->next_arrival_ordinal;
        v2_invoke_range(builder, &range, consume, consume_context);
        return WORR_CGAME_EVENT_RANGE_BUILD_REJECTED_V2;
    }

    chunk_count = event_count == 0
                      ? 1u
                      : (event_count + builder->scratch_capacity - 1u) /
                            builder->scratch_capacity;
    arrival_ordinal = builder->next_arrival_ordinal;
    builder->next_arrival_ordinal += arrival_count;

    if (event_count == 0) {
        flags |= WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |
                 WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2;
        v2_initialize_range(
            builder, &range, source_tick, source_time_us,
            WORR_CGAME_EVENT_RANGE_PHASE_ENTITY_FRAME_POST_PRESENT_V2,
            flags, WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2,
            WORR_CGAME_EVENT_ADAPTER_OK_V2, 0, 1, arrival_ordinal, 0);
        v2_invoke_range(builder, &range, consume, consume_context);
        return WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2;
    }

    for (index = 0; index < carrier_count; ++index) {
        const worr_cgame_event_carrier_v2 *carrier = &carriers[index];
        worr_event_entity_ref_v1 source_ref;
        if (carrier->raw_event == 0)
            continue;
        source_ref.index = carrier->entity_index;
        source_ref.generation =
            builder->observed[carrier->entity_index].generation;
        /* All records were constructed and candidate-validated with the same
         * prospective references before lifecycle or cursor state committed. */
        (void)v2_initialize_legacy_entity_record(
            &builder->scratch_records[output_count], source_tick,
            source_time_us, carrier->scan_order, source_ref,
            carrier->raw_event);
        ++output_count;
        ++emitted_count;

        if (output_count == builder->scratch_capacity ||
            emitted_count == event_count) {
            uint32_t chunk_flags = flags;
            if (chunk_index == 0)
                chunk_flags |= WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2;
            else
                chunk_flags |= WORR_CGAME_EVENT_RANGE_CONTINUATION_V2;
            if (chunk_index + 1u == chunk_count)
                chunk_flags |= WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2;
            v2_initialize_range(
                builder, &range, source_tick, source_time_us,
                WORR_CGAME_EVENT_RANGE_PHASE_ENTITY_FRAME_POST_PRESENT_V2,
                chunk_flags, WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2,
                WORR_CGAME_EVENT_ADAPTER_OK_V2, chunk_index, chunk_count,
                arrival_ordinal, output_count);
            v2_invoke_range(builder, &range, consume, consume_context);
            arrival_ordinal += output_count;
            output_count = 0;
            ++chunk_index;
        }
    }
    return WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2;
}

void Worr_CGameEventRangeAuditResetV2(
    worr_cgame_event_range_audit_v2 *audit,
    uint32_t stream_epoch)
{
    uint64_t reset_count;
    if (!audit || stream_epoch == 0)
        return;
    reset_count = audit->initialized ? audit->status.reset_count : 0;
    memset(audit, 0, sizeof(*audit));
    audit->initialized = 1;
    audit->status.struct_size = sizeof(audit->status);
    audit->status.api_version = WORR_CGAME_EVENT_RANGE_API_VERSION_V2;
    audit->status.stream_epoch = stream_epoch;
    audit->status.reset_count = reset_count;
    increment_u64(&audit->status.reset_count);
    audit->status.normalized_chain_hash =
        begin_hash(UINT32_C(0x43455232)); /* CER2 */
}

static bool v2_range_flags_valid(const worr_cgame_event_range_v2 *range)
{
    const bool first =
        (range->flags & WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2) != 0;
    const bool continuation =
        (range->flags & WORR_CGAME_EVENT_RANGE_CONTINUATION_V2) != 0;
    const bool last =
        (range->flags & WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2) != 0;
    const bool rejected =
        (range->flags & WORR_CGAME_EVENT_RANGE_ADAPTER_REJECTED_V2) != 0;
    const bool generation_observed =
        (range->flags &
         WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2) != 0;

    if ((range->flags & ~V2_ALL_RANGE_FLAGS) != 0 ||
        ((range->flags & WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2) != 0 &&
         (range->flags & WORR_CGAME_EVENT_RANGE_DEMO_PLAYBACK_V2) == 0) ||
        (range->flags &
         WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2) == 0 ||
        range->chunk_count == 0 || range->chunk_index >= range->chunk_count ||
        first != (range->chunk_index == 0) ||
        continuation != (range->chunk_index != 0) ||
        last != (range->chunk_index + 1u == range->chunk_count) ||
        rejected !=
            (range->adapter_status != WORR_CGAME_EVENT_ADAPTER_OK_V2)) {
        return false;
    }
    if (rejected &&
        (range->count != 0 || range->chunk_count != 1 ||
         range->chunk_index != 0)) {
        return false;
    }
    if (range->phase ==
        WORR_CGAME_EVENT_RANGE_PHASE_ACTION_PRE_PRESENT_V2) {
        bool expected_generation_observed = false;
        if (!rejected) {
            if ((range->carrier_kind ==
                     WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 &&
                 (range->count == 0 ||
                  range->count >
                      WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2)) ||
                (range->carrier_kind !=
                     WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 &&
                 range->count != 1)) {
                return false;
            }
            for (uint32_t index = 0; index < range->count; ++index) {
                expected_generation_observed =
                    expected_generation_observed ||
                    range->records[index].source_entity.index != 0 ||
                    (range->records[index].subject_entity.index !=
                         WORR_EVENT_NO_ENTITY &&
                     range->records[index].subject_entity.index != 0);
            }
        }
        return v2_action_kind_valid(range->carrier_kind) &&
               range->chunk_count == 1 &&
               generation_observed == expected_generation_observed &&
               (range->flags &
                WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2) != 0 &&
               (range->flags &
                WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2) != 0;
    }
    if (range->phase ==
        WORR_CGAME_EVENT_RANGE_PHASE_ENTITY_FRAME_POST_PRESENT_V2) {
        return range->carrier_kind ==
                   WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2 &&
               (range->chunk_count == 1 || range->count != 0) &&
               (range->flags &
                (WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2 |
                 WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2)) == 0 &&
               (range->flags &
                WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2) != 0;
    }
    return false;
}

bool Worr_CGameEventRangeAuditConsumeV2(
    worr_cgame_event_range_audit_v2 *audit,
    const worr_cgame_event_range_v2 *range)
{
    uint64_t range_hash;
    uint64_t chain_hash;
    uint64_t expected_arrival;
    uint64_t arrival_count;
    uint32_t index;
    uint32_t base_flags;
    uint32_t last_source_ordinal = 0;
    bool first;
    bool has_source_ordinal = false;
    bool last;
    bool valid;

    valid = audit && audit->initialized && range &&
            range->struct_size == sizeof(*range) &&
            range->api_version == WORR_CGAME_EVENT_RANGE_API_VERSION_V2 &&
            range->stream_epoch == audit->status.stream_epoch &&
            range->batch_generation != 0 &&
            range->carrier_sequence != 0 &&
            range->count <= WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2 &&
            ((range->count == 0 && range->records == NULL) ||
             (range->count != 0 && range->records != NULL)) &&
            v2_carrier_kind_valid(range->carrier_kind) &&
            v2_adapter_status_valid(range->adapter_status) &&
            v2_range_flags_valid(range);
    if (!valid) {
        if (audit && audit->initialized)
            increment_u64(&audit->status.rejected_ranges);
        return false;
    }

    first = range->chunk_index == 0;
    last = range->chunk_index + 1u == range->chunk_count;
    base_flags = range->flags &
        ~(uint32_t)(WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 |
                    WORR_CGAME_EVENT_RANGE_CONTINUATION_V2 |
                    WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2);
    arrival_count = range->count == 0 ? 1u : range->count;
    valid = audit->status.last_arrival_ordinal != UINT64_MAX &&
            range->first_arrival_ordinal <=
                UINT64_MAX - (arrival_count - 1u);
    expected_arrival = audit->status.last_arrival_ordinal + 1u;
    if (first) {
        valid = valid && !audit->in_carrier &&
                audit->status.last_batch_generation != UINT32_MAX &&
                audit->status.last_carrier_sequence != UINT64_MAX &&
                range->batch_generation ==
                    audit->status.last_batch_generation + 1u &&
                range->carrier_sequence ==
                    audit->status.last_carrier_sequence + 1u;
    } else {
        valid = valid && audit->in_carrier &&
                range->carrier_sequence == audit->active_carrier_sequence &&
                range->carrier_time_us == audit->active_carrier_time_us &&
                range->batch_generation == audit->active_batch_generation &&
                range->carrier_tick == audit->active_carrier_tick &&
                range->phase == audit->active_phase &&
                base_flags == audit->active_flags_base &&
                range->carrier_kind == audit->active_carrier_kind &&
                range->adapter_status == audit->active_adapter_status &&
                range->chunk_count == audit->active_chunk_count &&
                range->chunk_index == audit->next_chunk_index;
    }
    if (!valid || range->first_arrival_ordinal != expected_arrival) {
        increment_u64(&audit->status.rejected_ranges);
        return false;
    }

    range_hash = begin_hash(UINT32_C(0x43454232)); /* CEB2 */
    range_hash = hash_u64(range_hash, range->carrier_sequence);
    range_hash = hash_u64(range_hash, range->first_arrival_ordinal);
    range_hash = hash_u64(range_hash, range->carrier_time_us);
    range_hash = hash_u32(range_hash, range->carrier_tick);
    range_hash = hash_u32(range_hash, range->count);
    range_hash = hash_u32(range_hash, range->phase);
    range_hash = hash_u32(range_hash, range->flags);
    range_hash = hash_u32(range_hash, range->carrier_kind);
    range_hash = hash_u32(range_hash, range->adapter_status);
    range_hash = hash_u32(range_hash, range->chunk_index);
    range_hash = hash_u32(range_hash, range->chunk_count);
    if (!first &&
        range->phase ==
            WORR_CGAME_EVENT_RANGE_PHASE_ENTITY_FRAME_POST_PRESENT_V2) {
        has_source_ordinal = audit->has_active_source_ordinal != 0;
        last_source_ordinal = audit->active_last_source_ordinal;
    }
    for (index = 0; index < range->count; ++index) {
        uint64_t record_hash;
        if (range->records[index].source_tick != range->carrier_tick ||
            range->records[index].source_time_us != range->carrier_time_us ||
            !v2_legacy_record_identity_valid(&range->records[index]) ||
            !Worr_EventRecordCandidateValidateV1(
                &range->records[index],
                WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2) ||
            !Worr_EventRecordSemanticHashV1(
                &range->records[index],
                WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
                &record_hash)) {
            increment_u64(&audit->status.rejected_ranges);
            return false;
        }
        if (range->phase ==
            WORR_CGAME_EVENT_RANGE_PHASE_ACTION_PRE_PRESENT_V2) {
            const uint32_t expected_source_ordinal =
                range->carrier_kind ==
                        WORR_CGAME_EVENT_CARRIER_DAMAGE_V2
                    ? index
                    : 0;
            if (range->records[index].source_ordinal !=
                    expected_source_ordinal ||
                !v2_action_record_shape_valid(
                    &range->records[index], range->carrier_kind) ||
                (range->carrier_kind ==
                     WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 &&
                 index != 0 &&
                 (range->records[index].source_entity.index !=
                      range->records[0].source_entity.index ||
                  range->records[index].subject_entity.index !=
                      range->records[0].subject_entity.index ||
                  range->records[index].subject_entity.generation !=
                      range->records[0].subject_entity.generation))) {
                increment_u64(&audit->status.rejected_ranges);
                return false;
            }
        } else {
            if (!v2_record_matches_carrier_kind(
                    &range->records[index], range->carrier_kind) ||
                range->records[index].source_entity.index == 0 ||
                range->records[index].subject_entity.index !=
                    WORR_EVENT_NO_ENTITY ||
                (has_source_ordinal &&
                 range->records[index].source_ordinal <=
                     last_source_ordinal)) {
                increment_u64(&audit->status.rejected_ranges);
                return false;
            }
            last_source_ordinal = range->records[index].source_ordinal;
            has_source_ordinal = true;
        }
        range_hash = hash_u64(range_hash, record_hash);
    }

    chain_hash = begin_hash(UINT32_C(0x43454832)); /* CEH2 */
    chain_hash = hash_u64(chain_hash,
                          audit->status.normalized_chain_hash);
    chain_hash = hash_u64(chain_hash, range_hash);
    increment_u64(&audit->status.accepted_ranges);
    if (UINT64_MAX - audit->status.accepted_records < range->count)
        audit->status.accepted_records = UINT64_MAX;
    else
        audit->status.accepted_records += range->count;
    audit->status.last_arrival_ordinal =
        range->first_arrival_ordinal + arrival_count - 1u;
    audit->status.last_chunk_index = range->chunk_index;
    audit->status.last_carrier_kind = range->carrier_kind;
    audit->status.last_adapter_status = range->adapter_status;
    audit->status.last_range_hash = range_hash;
    audit->status.normalized_chain_hash = chain_hash;

    if (first && !last) {
        audit->in_carrier = 1;
        audit->active_carrier_sequence = range->carrier_sequence;
        audit->active_carrier_time_us = range->carrier_time_us;
        audit->active_batch_generation = range->batch_generation;
        audit->active_carrier_tick = range->carrier_tick;
        audit->active_phase = range->phase;
        audit->active_flags_base = base_flags;
        audit->active_carrier_kind = range->carrier_kind;
        audit->active_adapter_status = range->adapter_status;
        audit->active_chunk_count = range->chunk_count;
        audit->active_last_source_ordinal = last_source_ordinal;
        audit->has_active_source_ordinal = has_source_ordinal ? 1 : 0;
        audit->next_chunk_index = 1;
    } else if (!first) {
        audit->active_last_source_ordinal = last_source_ordinal;
        audit->has_active_source_ordinal = has_source_ordinal ? 1 : 0;
        ++audit->next_chunk_index;
    }
    if (last) {
        const uint32_t kind_index = range->carrier_kind - 1u;
        audit->status.last_batch_generation = range->batch_generation;
        audit->status.last_carrier_sequence = range->carrier_sequence;
        if ((range->flags &
             WORR_CGAME_EVENT_RANGE_ADAPTER_REJECTED_V2) != 0) {
            increment_u64(&audit->status.rejected_carriers);
            increment_u64(
                &audit->status.rejected_carriers_by_kind[kind_index]);
        } else {
            increment_u64(&audit->status.accepted_carriers);
            increment_u64(
                &audit->status.accepted_carriers_by_kind[kind_index]);
        }
        audit->in_carrier = 0;
        audit->has_active_source_ordinal = 0;
        audit->next_chunk_index = 0;
    }
    return true;
}

bool Worr_CGameEventRangeAuditStatusV2(
    const worr_cgame_event_range_audit_v2 *audit,
    worr_cgame_event_range_audit_status_v2 *status_out)
{
    if (!audit || !audit->initialized || !status_out)
        return false;
    *status_out = audit->status;
    return true;
}

#undef V2_ALL_RANGE_FLAGS
#undef V2_CALLER_RANGE_FLAGS
