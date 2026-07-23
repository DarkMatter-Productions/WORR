/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "event_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_CGAME_EVENT_SHADOW_EXPORT_V1 \
    "WORR_CGAME_CANONICAL_EVENT_SHADOW_EXPORT_V1"
#define WORR_CGAME_EVENT_SHADOW_API_VERSION 1u
#define WORR_CGAME_EVENT_SHADOW_MAX_RECORDS 512u
#define WORR_CGAME_EVENT_SHADOW_MAX_ENTITIES 8192u
#define WORR_CGAME_EVENT_SHADOW_MAX_LEGACY_EVENT 9u

typedef enum worr_cgame_event_shadow_phase_v1_e {
    WORR_CGAME_EVENT_SHADOW_PHASE_ENTITY_FRAME = 1,
} worr_cgame_event_shadow_phase_v1;

typedef enum worr_cgame_event_shadow_range_flags_v1_e {
    WORR_CGAME_EVENT_SHADOW_RANGE_DEMO_PLAYBACK = 1u << 0,
} worr_cgame_event_shadow_range_flags_v1;

typedef enum worr_cgame_event_shadow_reset_reason_v1_e {
    WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE = 1,
    WORR_CGAME_EVENT_SHADOW_RESET_DEMO_RESTART = 2,
    WORR_CGAME_EVENT_SHADOW_RESET_FRAME_REWIND = 3,
    WORR_CGAME_EVENT_SHADOW_RESET_CONSUMER_ATTACH = 4,
} worr_cgame_event_shadow_reset_reason_v1;

typedef struct worr_cgame_event_range_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t stream_epoch;
    uint32_t batch_generation;
    uint64_t carrier_sequence;
    uint32_t first_carrier_ordinal;
    uint32_t count;
    uint32_t phase;
    uint32_t flags;
    const worr_event_record_v1 *records;
} worr_cgame_event_range_v1;

typedef struct worr_cgame_event_shadow_audit_status_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t stream_epoch;
    uint32_t last_batch_generation;
    uint64_t last_carrier_sequence;
    uint64_t reset_count;
    uint64_t accepted_batches;
    uint64_t accepted_records;
    uint64_t rejected_batches;
    uint64_t normalized_chain_hash;
    uint64_t last_batch_hash;
} worr_cgame_event_shadow_audit_status_v1;

typedef struct worr_cgame_event_shadow_export_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    void (*Reset)(uint32_t stream_epoch, uint32_t reason);
    void (*ConsumeCanonicalEventRange)(
        const worr_cgame_event_range_v1 *range);
    bool (*GetAuditStatus)(
        worr_cgame_event_shadow_audit_status_v1 *status_out);
} worr_cgame_event_shadow_export_v1;

/* Engine-side input includes every visible entity, not only event carriers. */
typedef struct worr_cgame_event_shadow_carrier_v1_s {
    uint32_t entity_index;
    uint32_t raw_event;
    uint32_t scan_order;
} worr_cgame_event_shadow_carrier_v1;

typedef struct worr_cgame_event_shadow_observed_v1_s {
    uint32_t generation;
    uint32_t last_seen_batch;
    uint8_t present;
    uint8_t reserved[3];
} worr_cgame_event_shadow_observed_v1;

typedef enum worr_cgame_event_shadow_build_result_v1_e {
    WORR_CGAME_EVENT_SHADOW_BUILD_DELIVERED = 0,
    WORR_CGAME_EVENT_SHADOW_BUILD_DUPLICATE_FRAME = 1,
    WORR_CGAME_EVENT_SHADOW_BUILD_INVALID_FRAME = 2,
    WORR_CGAME_EVENT_SHADOW_BUILD_CAPACITY = 3,
    WORR_CGAME_EVENT_SHADOW_BUILD_GENERATION_EXHAUSTED = 4,
    WORR_CGAME_EVENT_SHADOW_BUILD_ORDINAL_EXHAUSTED = 5,
    WORR_CGAME_EVENT_SHADOW_BUILD_REENTRANT = 6,
} worr_cgame_event_shadow_build_result_v1;

typedef void (*worr_cgame_event_shadow_consume_fn_v1)(
    void *context, const worr_cgame_event_range_v1 *range);

/* Runtime-only caller-owned builder.  Pointers are never serialized. */
typedef struct worr_cgame_event_shadow_builder_v1_s {
    worr_cgame_event_shadow_observed_v1 *observed;
    uint32_t *seen_markers;
    worr_event_record_v1 *scratch_records;
    uint32_t observed_capacity;
    uint32_t scratch_capacity;
    uint32_t stream_epoch;
    uint32_t batch_generation;
    uint32_t seen_marker;
    uint32_t next_carrier_ordinal;
    uint32_t last_source_tick;
    uint64_t carrier_sequence;
    uint8_t has_last_source_tick;
    uint8_t in_callback;
    uint8_t reserved[6];
} worr_cgame_event_shadow_builder_v1;

typedef struct worr_cgame_event_shadow_audit_v1_s {
    worr_cgame_event_shadow_audit_status_v1 status;
    uint8_t initialized;
    uint8_t reserved[7];
} worr_cgame_event_shadow_audit_v1;

/* V1 above is frozen as the entity-frame LEGACY_BRIDGE audit contract.  V2 is
 * a separate named extension because accepting additional payload kinds would
 * silently broaden V1's validation and callback semantics. */
#define WORR_CGAME_EVENT_RANGE_EXPORT_V2 \
    "WORR_CGAME_CANONICAL_EVENT_RANGE_EXPORT_V2"
#define WORR_CGAME_EVENT_RANGE_API_VERSION_V2 2u
#define WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2 512u
#define WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2 8192u
#define WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2 4u

typedef enum worr_cgame_event_range_phase_v2_e {
    WORR_CGAME_EVENT_RANGE_PHASE_ACTION_PRE_PRESENT_V2 = 1,
    WORR_CGAME_EVENT_RANGE_PHASE_ENTITY_FRAME_POST_PRESENT_V2 = 2,
} worr_cgame_event_range_phase_v2;

typedef enum worr_cgame_event_carrier_kind_v2_e {
    WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2 = 1,
    WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2 = 2,
    WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2 = 3,
    WORR_CGAME_EVENT_CARRIER_MONSTER_MUZZLE_V2 = 4,
    WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2 = 5,
    WORR_CGAME_EVENT_CARRIER_DAMAGE_V2 = 6,
    WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2 = 7,
    WORR_CGAME_EVENT_CARRIER_KIND_COUNT_V2 = 7,
} worr_cgame_event_carrier_kind_v2;

typedef enum worr_cgame_event_adapter_status_v2_e {
    WORR_CGAME_EVENT_ADAPTER_OK_V2 = 0,
    WORR_CGAME_EVENT_ADAPTER_UNSUPPORTED_ID_V2 = 1,
    WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2 = 2,
    WORR_CGAME_EVENT_ADAPTER_ENTITY_OUT_OF_RANGE_V2 = 3,
    WORR_CGAME_EVENT_ADAPTER_PAYLOAD_INVALID_V2 = 4,
} worr_cgame_event_adapter_status_v2;

typedef enum worr_cgame_event_range_flags_v2_e {
    WORR_CGAME_EVENT_RANGE_DEMO_PLAYBACK_V2 = 1u << 0,
    WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2 = 1u << 1,
    WORR_CGAME_EVENT_RANGE_SOURCE_TIME_INFERRED_V2 = 1u << 2,
    WORR_CGAME_EVENT_RANGE_ROUTING_UNKNOWN_V2 = 1u << 3,
    WORR_CGAME_EVENT_RANGE_ENTITY_GENERATION_OBSERVED_V2 = 1u << 4,
    WORR_CGAME_EVENT_RANGE_ADAPTER_REJECTED_V2 = 1u << 5,
    WORR_CGAME_EVENT_RANGE_FIRST_CHUNK_V2 = 1u << 6,
    WORR_CGAME_EVENT_RANGE_CONTINUATION_V2 = 1u << 7,
    WORR_CGAME_EVENT_RANGE_LAST_CHUNK_V2 = 1u << 8,
    WORR_CGAME_EVENT_RANGE_LEGACY_PRESENTER_AUTHORITATIVE_V2 = 1u << 9,
} worr_cgame_event_range_flags_v2;

typedef struct worr_cgame_event_range_v2_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t stream_epoch;
    uint32_t batch_generation;
    uint64_t carrier_sequence;
    /* Decode/acceptance order, independent of record.source_ordinal.  Empty
     * accepted/rejected carriers consume one ordinal; chunks advance by their
     * record count and retain one carrier identity. */
    uint64_t first_arrival_ordinal;
    uint64_t carrier_time_us;
    uint32_t carrier_tick;
    uint32_t count;
    uint32_t phase;
    uint32_t flags;
    uint32_t carrier_kind;
    uint32_t adapter_status;
    uint32_t chunk_index;
    uint32_t chunk_count;
    const worr_event_record_v1 *records;
} worr_cgame_event_range_v2;

/* The record is a pointer-free candidate template.  source_entity and
 * subject_entity must both be the absent reference; the accumulator replaces
 * them with client-observed references atomically after previewing generation
 * changes.  A single-action candidate has source_ordinal zero.  In an atomic
 * multi-record action, source_ordinal must equal the candidate's array index.
 * Legacy V2 candidates remain ID-less and prediction-key-free. */
typedef struct worr_cgame_event_action_candidate_v2_s {
    uint32_t struct_size;
    uint32_t source_entity_index;
    uint32_t subject_entity_index;
    uint32_t reserved0;
    worr_event_record_v1 record;
} worr_cgame_event_action_candidate_v2;

typedef struct worr_cgame_event_carrier_v2_s {
    uint32_t entity_index;
    uint32_t raw_event;
    uint32_t scan_order;
} worr_cgame_event_carrier_v2;

typedef struct worr_cgame_event_observed_v2_s {
    uint32_t generation;
    uint32_t last_seen_batch;
    uint8_t present;
    uint8_t provisional;
    uint8_t reserved[2];
} worr_cgame_event_observed_v2;

typedef enum worr_cgame_event_range_build_result_v2_e {
    WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2 = 0,
    WORR_CGAME_EVENT_RANGE_BUILD_REJECTED_V2 = 1,
    WORR_CGAME_EVENT_RANGE_BUILD_DUPLICATE_FRAME_V2 = 2,
    WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2 = 3,
    WORR_CGAME_EVENT_RANGE_BUILD_CAPACITY_V2 = 4,
    WORR_CGAME_EVENT_RANGE_BUILD_GENERATION_EXHAUSTED_V2 = 5,
    WORR_CGAME_EVENT_RANGE_BUILD_ORDER_EXHAUSTED_V2 = 6,
    WORR_CGAME_EVENT_RANGE_BUILD_REENTRANT_V2 = 7,
} worr_cgame_event_range_build_result_v2;

typedef void (*worr_cgame_event_range_consume_fn_v2)(
    void *context, const worr_cgame_event_range_v2 *range);

typedef struct worr_cgame_event_range_builder_v2_s {
    worr_cgame_event_observed_v2 *observed;
    uint32_t *seen_markers;
    worr_event_record_v1 *scratch_records;
    uint32_t observed_capacity;
    uint32_t scratch_capacity;
    uint32_t stream_epoch;
    uint32_t batch_generation;
    uint32_t last_frame_tick;
    uint64_t carrier_sequence;
    uint64_t next_arrival_ordinal;
    uint8_t has_last_frame_tick;
    uint8_t in_callback;
    uint8_t reserved[6];
} worr_cgame_event_range_builder_v2;

typedef struct worr_cgame_event_range_audit_status_v2_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t stream_epoch;
    uint32_t last_batch_generation;
    uint64_t last_carrier_sequence;
    uint64_t last_arrival_ordinal;
    uint32_t last_chunk_index;
    uint32_t last_carrier_kind;
    uint32_t last_adapter_status;
    uint32_t reserved0;
    uint64_t reset_count;
    uint64_t accepted_carriers;
    uint64_t accepted_ranges;
    uint64_t accepted_records;
    uint64_t rejected_carriers;
    uint64_t rejected_ranges;
    uint64_t normalized_chain_hash;
    uint64_t last_range_hash;
    uint64_t accepted_carriers_by_kind[WORR_CGAME_EVENT_CARRIER_KIND_COUNT_V2];
    uint64_t rejected_carriers_by_kind[WORR_CGAME_EVENT_CARRIER_KIND_COUNT_V2];
} worr_cgame_event_range_audit_status_v2;

typedef struct worr_cgame_event_range_audit_v2_s {
    worr_cgame_event_range_audit_status_v2 status;
    uint64_t active_carrier_sequence;
    uint64_t active_carrier_time_us;
    uint32_t active_batch_generation;
    uint32_t active_carrier_tick;
    uint32_t active_phase;
    uint32_t active_flags_base;
    uint32_t active_carrier_kind;
    uint32_t active_adapter_status;
    uint32_t next_chunk_index;
    uint32_t active_chunk_count;
    uint32_t active_last_source_ordinal;
    uint8_t initialized;
    uint8_t in_carrier;
    uint8_t has_active_source_ordinal;
    uint8_t reserved[1];
} worr_cgame_event_range_audit_v2;

typedef struct worr_cgame_event_range_export_v2_s {
    uint32_t struct_size;
    uint32_t api_version;
    void (*Reset)(uint32_t stream_epoch, uint32_t reason);
    void (*ConsumeCanonicalEventRange)(
        const worr_cgame_event_range_v2 *range);
    bool (*GetAuditStatus)(
        worr_cgame_event_range_audit_status_v2 *status_out);
} worr_cgame_event_range_export_v2;

bool Worr_CGameEventRangeBuilderInitV2(
    worr_cgame_event_range_builder_v2 *builder,
    worr_cgame_event_observed_v2 *observed,
    uint32_t *seen_markers,
    uint32_t observed_capacity,
    worr_event_record_v1 *scratch_records,
    uint32_t scratch_capacity,
    uint32_t stream_epoch);
bool Worr_CGameEventRangeBuilderResetV2(
    worr_cgame_event_range_builder_v2 *builder,
    uint32_t stream_epoch);
worr_cgame_event_range_build_result_v2
Worr_CGameEventRangeDeliverActionV2(
    worr_cgame_event_range_builder_v2 *builder,
    const worr_cgame_event_action_candidate_v2 *candidate,
    uint32_t carrier_kind,
    /* Caller supplies DEMO_PLAYBACK/DEMO_SEEK only.  Action inference,
     * routing, lifecycle, chunk, rejection, and presenter flags are owned by
     * the builder. */
    uint32_t range_flags,
    worr_cgame_event_range_consume_fn_v2 consume,
    void *consume_context);
worr_cgame_event_range_build_result_v2
Worr_CGameEventRangeDeliverActionBatchV2(
    worr_cgame_event_range_builder_v2 *builder,
    const worr_cgame_event_action_candidate_v2 *candidates,
    uint32_t candidate_count,
    uint32_t carrier_kind,
    /* Caller supplies DEMO_PLAYBACK/DEMO_SEEK only.  The complete batch is
     * validated and generation-previewed before any builder state changes. */
    uint32_t range_flags,
    worr_cgame_event_range_consume_fn_v2 consume,
    void *consume_context);
worr_cgame_event_range_build_result_v2
Worr_CGameEventRangeDeliverRejectedActionV2(
    worr_cgame_event_range_builder_v2 *builder,
    uint32_t carrier_tick,
    uint64_t carrier_time_us,
    uint32_t carrier_kind,
    uint32_t adapter_status,
    uint32_t range_flags, /* demo flags only */
    worr_cgame_event_range_consume_fn_v2 consume,
    void *consume_context);
worr_cgame_event_range_build_result_v2
Worr_CGameEventRangeDeliverFrameV2(
    worr_cgame_event_range_builder_v2 *builder,
    uint32_t source_tick,
    uint64_t source_time_us,
    const worr_cgame_event_carrier_v2 *carriers,
    uint32_t carrier_count,
    uint32_t range_flags, /* demo flags only */
    worr_cgame_event_range_consume_fn_v2 consume,
    void *consume_context);

void Worr_CGameEventRangeAuditResetV2(
    worr_cgame_event_range_audit_v2 *audit,
    uint32_t stream_epoch);
bool Worr_CGameEventRangeAuditConsumeV2(
    worr_cgame_event_range_audit_v2 *audit,
    const worr_cgame_event_range_v2 *range);
bool Worr_CGameEventRangeAuditStatusV2(
    const worr_cgame_event_range_audit_v2 *audit,
    worr_cgame_event_range_audit_status_v2 *status_out);

bool Worr_CGameEventShadowBuilderInitV1(
    worr_cgame_event_shadow_builder_v1 *builder,
    worr_cgame_event_shadow_observed_v1 *observed,
    uint32_t *seen_markers,
    uint32_t observed_capacity,
    worr_event_record_v1 *scratch_records,
    uint32_t scratch_capacity,
    uint32_t stream_epoch);
bool Worr_CGameEventShadowBuilderResetV1(
    worr_cgame_event_shadow_builder_v1 *builder,
    uint32_t stream_epoch);
worr_cgame_event_shadow_build_result_v1
Worr_CGameEventShadowDeliverFrameV1(
    worr_cgame_event_shadow_builder_v1 *builder,
    uint32_t source_tick,
    uint64_t source_time_us,
    const worr_cgame_event_shadow_carrier_v1 *carriers,
    uint32_t carrier_count,
    uint32_t range_flags,
    worr_cgame_event_shadow_consume_fn_v1 consume,
    void *consume_context);

uint64_t Worr_CGameEventShadowNormalizedRecordHashV1(
    const worr_event_record_v1 *record);

void Worr_CGameEventShadowAuditResetV1(
    worr_cgame_event_shadow_audit_v1 *audit,
    uint32_t stream_epoch);
bool Worr_CGameEventShadowAuditConsumeV1(
    worr_cgame_event_shadow_audit_v1 *audit,
    const worr_cgame_event_range_v1 *range);
bool Worr_CGameEventShadowAuditStatusV1(
    const worr_cgame_event_shadow_audit_v1 *audit,
    worr_cgame_event_shadow_audit_status_v1 *status_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_CGAME_EVENT_SHADOW_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_CGAME_EVENT_SHADOW_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_CGAME_EVENT_SHADOW_STATIC_ASSERT(
    sizeof(worr_cgame_event_shadow_carrier_v1) == 12,
    "cgame event shadow carrier v1 layout changed");
WORR_CGAME_EVENT_SHADOW_STATIC_ASSERT(
    sizeof(worr_cgame_event_shadow_observed_v1) == 12,
    "cgame event shadow observed state v1 layout changed");
WORR_CGAME_EVENT_SHADOW_STATIC_ASSERT(
    sizeof(worr_cgame_event_shadow_audit_status_v1) == 72,
    "cgame event shadow audit status v1 layout changed");
WORR_CGAME_EVENT_SHADOW_STATIC_ASSERT(
    sizeof(worr_cgame_event_action_candidate_v2) == 184,
    "cgame event action candidate v2 layout changed");
WORR_CGAME_EVENT_SHADOW_STATIC_ASSERT(
    sizeof(worr_cgame_event_carrier_v2) == 12,
    "cgame event carrier v2 layout changed");
WORR_CGAME_EVENT_SHADOW_STATIC_ASSERT(
    sizeof(worr_cgame_event_observed_v2) == 12,
    "cgame event observed v2 layout changed");
WORR_CGAME_EVENT_SHADOW_STATIC_ASSERT(
    sizeof(worr_cgame_event_range_audit_status_v2) == 224,
    "cgame event audit status v2 layout changed");

#undef WORR_CGAME_EVENT_SHADOW_STATIC_ASSERT
