/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/cgame_native_event_probe.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

struct export_header {
    std::uint32_t struct_size;
    std::uint32_t api_version;
};

static_assert(std::is_standard_layout_v<
              worr_cgame_native_event_probe_status_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_cgame_native_event_probe_status_v1>);
static_assert(sizeof(worr_cgame_native_event_probe_status_v1) == 336);
static_assert(offsetof(worr_cgame_native_event_probe_status_v1,
                       raw_action_records) == 64);
static_assert(offsetof(worr_cgame_native_event_probe_status_v1,
                       authoritative_presentations) == 168);
static_assert(offsetof(worr_cgame_native_event_probe_status_v1,
                       raw_action_by_kind) == 208);
static_assert(offsetof(worr_cgame_native_event_probe_status_v1,
                       probe_action_by_kind) == 272);

static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_STATUS_VERSION == 3u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT == 8u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI == 7u);

static_assert(std::is_standard_layout_v<
              worr_cgame_native_event_probe_checkpoint_receipt_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_cgame_native_event_probe_checkpoint_receipt_v1>);
static_assert(
    sizeof(worr_cgame_native_event_probe_checkpoint_receipt_v1) == 32);
static_assert(offsetof(worr_cgame_native_event_probe_checkpoint_receipt_v1,
                       result) == 8);
static_assert(offsetof(worr_cgame_native_event_probe_checkpoint_receipt_v1,
                       observed_map_generation) == 12);
static_assert(offsetof(worr_cgame_native_event_probe_checkpoint_receipt_v1,
                       observed_authority_epoch) == 16);
static_assert(offsetof(worr_cgame_native_event_probe_checkpoint_receipt_v1,
                       checkpoint_id) == 24);

static_assert(sizeof(export_header) == 8);
static_assert(offsetof(worr_cgame_native_event_probe_export_v1,
                       CompleteLegacyDispatch) == sizeof(export_header));
static_assert(
    offsetof(worr_cgame_native_event_probe_export_v1, GetStatus) ==
    offsetof(worr_cgame_native_event_probe_export_v1,
             CompleteLegacyDispatch) +
        sizeof(worr_cgame_native_event_probe_export_v1::
                   CompleteLegacyDispatch));
static_assert(
    sizeof(worr_cgame_native_event_probe_export_v1) ==
    offsetof(worr_cgame_native_event_probe_export_v1, GetStatus) +
        sizeof(worr_cgame_native_event_probe_export_v1::GetStatus));
static_assert(offsetof(worr_cgame_native_event_probe_export_v2,
                       CompleteLegacyDispatch) == sizeof(export_header));
static_assert(
    offsetof(worr_cgame_native_event_probe_export_v2, Checkpoint) ==
    offsetof(worr_cgame_native_event_probe_export_v2, GetStatus) +
        sizeof(worr_cgame_native_event_probe_export_v2::GetStatus));
static_assert(
    sizeof(worr_cgame_native_event_probe_export_v2) ==
    offsetof(worr_cgame_native_event_probe_export_v2, Checkpoint) +
        sizeof(worr_cgame_native_event_probe_export_v2::Checkpoint));

static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION == 1u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION_V2 == 2u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_VERSION == 1u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_INVALID_ARGUMENT == 0u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED == 1u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED == 2u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_NOT_READY == 3u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_MAP == 4u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_AUTHORITY == 5u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_CONFLICT == 6u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_BUSY == 7u);
static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_UNHEALTHY == 8u);

int main()
{
    return std::strcmp(WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V1,
                       "WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V1") == 0 &&
                   std::strcmp(WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V2,
                               "WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V2") == 0
               ? 0
               : 1;
}
