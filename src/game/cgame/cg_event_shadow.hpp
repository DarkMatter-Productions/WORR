/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/cgame_event_shadow.h"
#include "shared/cgame_native_event_probe.h"

#include <cstdint>

struct cg_event_runtime_presentation_context_v1;

constexpr std::uint32_t CG_CANONICAL_EVENT_PRESENTATION_VERSION = 1u;
constexpr std::uint32_t CG_CANONICAL_EVENT_PRESENTATION_CAPACITY = 2048u;

enum cg_canonical_event_presentation_result_v1 : std::uint32_t {
    CG_CANONICAL_EVENT_PRESENTATION_OK = 0,
    CG_CANONICAL_EVENT_PRESENTATION_EMPTY = 1,
    CG_CANONICAL_EVENT_PRESENTATION_INVALID_ARGUMENT = 2,
    CG_CANONICAL_EVENT_PRESENTATION_UNINITIALIZED = 3,
    CG_CANONICAL_EVENT_PRESENTATION_STALE_CURSOR = 4,
    CG_CANONICAL_EVENT_PRESENTATION_CURSOR_OVERRUN = 5,
    CG_CANONICAL_EVENT_PRESENTATION_CORRUPT = 6,
    CG_CANONICAL_EVENT_PRESENTATION_NOT_READY = 7,
};

struct cg_canonical_event_presentation_cursor_v1 {
    std::uint32_t struct_size;
    std::uint32_t schema_version;
    std::uint32_t stream_epoch;
    std::uint32_t journal_generation;
    std::uint64_t next_serial;
};

struct cg_canonical_event_presentation_entry_v1 {
    std::uint32_t struct_size;
    std::uint32_t schema_version;
    std::uint32_t stream_epoch;
    std::uint32_t batch_generation;
    std::uint64_t journal_serial;
    std::uint64_t carrier_sequence;
    std::uint64_t arrival_ordinal;
    std::uint64_t carrier_time_us;
    std::uint64_t semantic_hash;
    std::uint32_t carrier_tick;
    std::uint32_t phase;
    std::uint32_t range_flags;
    std::uint32_t carrier_kind;
    std::uint32_t adapter_status;
    std::uint32_t reserved0;
    worr_event_record_v1 record;
};

struct cg_canonical_event_presentation_status_v1 {
    std::uint32_t struct_size;
    std::uint32_t schema_version;
    std::uint32_t stream_epoch;
    std::uint32_t journal_generation;
    std::uint32_t capacity;
    std::uint32_t retained_count;
    std::uint64_t oldest_serial;
    std::uint64_t next_serial;
    std::uint64_t reset_count;
    std::uint64_t accepted_ranges;
    std::uint64_t accepted_records;
    std::uint64_t rejected_ranges;
    std::uint64_t empty_carriers;
    std::uint64_t adapter_rejections;
    std::uint64_t overwritten_records;
    std::uint64_t audit_ready_records;
    std::uint64_t audit_future_stalls;
    std::uint64_t audit_overrun_recoveries;
    std::uint64_t audit_last_ready_serial;
    std::uint64_t audit_last_render_time_us;
    worr_cgame_event_range_audit_status_v2 range_audit;
};

const worr_cgame_event_shadow_export_v1 *CG_GetEventShadowAPI();
const worr_cgame_event_range_export_v2 *CG_GetEventRangeAPIv2();

/* Private cgame half of the public native-event probe extension. Accepted raw
 * actions are captured by cg_event_shadow.cpp; cg_native_event_presenter.cpp
 * owns lifecycle aggregation and publishes the extension table. */
void CG_NativeEventProbeRawBeginMap();
void CG_NativeEventProbeRawEndMap();
void CG_NativeEventProbeRawUninstall();
bool CG_NativeEventProbeLegacyActionHash(
    const worr_cgame_event_range_v2 *range,
    std::uint32_t record_index,
    std::uint64_t *hash_out);
bool CG_NativeEventProbeAuthorityHash(
    const worr_event_record_v1 *record,
    const cg_event_runtime_presentation_context_v1 *context,
    std::uint64_t *hash_out);
bool CG_NativeEventProbeCompleteLegacyDispatch(
    std::uint32_t carrier_kind,
    worr_cgame_native_event_probe_legacy_disposition_v1 disposition);
bool CG_NativeEventProbeFillRawStatus(
    worr_cgame_native_event_probe_status_v1 *status_out);
bool CG_NativeEventProbeRawCheckpointReady();
void CG_NativeEventProbeRawApplyCheckpoint();

/*
 * Value-only, cgame-owned presentation history. Begin starts at the oldest
 * retained record; Tail starts after the newest record. Next never exposes the
 * producer's transient range storage and detects reset and overwrite races.
 */
cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationBegin(
    cg_canonical_event_presentation_cursor_v1 *cursor_out);
cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationTail(
    cg_canonical_event_presentation_cursor_v1 *cursor_out);
cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationNext(
    const cg_canonical_event_presentation_cursor_v1 *cursor,
    cg_canonical_event_presentation_cursor_v1 *next_cursor_out,
    cg_canonical_event_presentation_entry_v1 *entry_out);
bool CG_CanonicalEventPresentationGetStatus(
    cg_canonical_event_presentation_status_v1 *status_out);

/* Advances one ordered, present-once shadow dispatcher up to render_time_us.
 * A future head record blocks later records, preserving producer order. */
cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationAdvanceAudit(
    std::uint64_t render_time_us,
    std::uint32_t max_records,
    std::uint32_t *advanced_out);
