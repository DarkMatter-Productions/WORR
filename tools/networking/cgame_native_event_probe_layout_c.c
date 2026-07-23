/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/cgame_native_event_probe.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct export_header_s {
    uint32_t struct_size;
    uint32_t api_version;
} export_header;

_Static_assert(sizeof(worr_cgame_native_event_probe_status_v1) == 336,
               "probe status ABI size");
_Static_assert(offsetof(worr_cgame_native_event_probe_status_v1,
                        raw_action_records) == 64,
               "probe status counter offset");
_Static_assert(offsetof(worr_cgame_native_event_probe_status_v1,
                        authoritative_presentations) == 168,
               "probe status runtime counter offset");
_Static_assert(offsetof(worr_cgame_native_event_probe_status_v1,
                        raw_action_by_kind) == 208,
               "probe status raw-kind offset");
_Static_assert(offsetof(worr_cgame_native_event_probe_status_v1,
                        probe_action_by_kind) == 272,
               "probe status probe-kind offset");

_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_STATUS_VERSION == 3u,
               "probe status schema version");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT == 8u,
               "probe kind count");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI == 7u,
               "keyed POI probe kind numbering");

_Static_assert(
    sizeof(worr_cgame_native_event_probe_checkpoint_receipt_v1) == 32,
    "checkpoint receipt ABI size");
_Static_assert(
    offsetof(worr_cgame_native_event_probe_checkpoint_receipt_v1,
             result) == 8,
    "checkpoint result offset");
_Static_assert(
    offsetof(worr_cgame_native_event_probe_checkpoint_receipt_v1,
             observed_map_generation) == 12,
    "checkpoint map offset");
_Static_assert(
    offsetof(worr_cgame_native_event_probe_checkpoint_receipt_v1,
             observed_authority_epoch) == 16,
    "checkpoint authority offset");
_Static_assert(
    offsetof(worr_cgame_native_event_probe_checkpoint_receipt_v1,
             checkpoint_id) == 24,
    "checkpoint id offset");

_Static_assert(sizeof(export_header) == 8, "extension header size");
_Static_assert(offsetof(worr_cgame_native_event_probe_export_v1,
                        CompleteLegacyDispatch) == sizeof(export_header),
               "V1 callback header compatibility");
_Static_assert(
    offsetof(worr_cgame_native_event_probe_export_v1, GetStatus) ==
        offsetof(worr_cgame_native_event_probe_export_v1,
                 CompleteLegacyDispatch) +
            sizeof(((worr_cgame_native_event_probe_export_v1 *)0)
                       ->CompleteLegacyDispatch),
    "V1 status callback offset");
_Static_assert(
    sizeof(worr_cgame_native_event_probe_export_v1) ==
        offsetof(worr_cgame_native_event_probe_export_v1, GetStatus) +
            sizeof(((worr_cgame_native_event_probe_export_v1 *)0)->GetStatus),
    "V1 trailing layout");
_Static_assert(offsetof(worr_cgame_native_event_probe_export_v2,
                        CompleteLegacyDispatch) == sizeof(export_header),
               "V2 callback header compatibility");
_Static_assert(
    offsetof(worr_cgame_native_event_probe_export_v2, Checkpoint) ==
        offsetof(worr_cgame_native_event_probe_export_v2, GetStatus) +
            sizeof(((worr_cgame_native_event_probe_export_v2 *)0)->GetStatus),
    "V2 checkpoint callback offset");
_Static_assert(
    sizeof(worr_cgame_native_event_probe_export_v2) ==
        offsetof(worr_cgame_native_event_probe_export_v2, Checkpoint) +
            sizeof(((worr_cgame_native_event_probe_export_v2 *)0)->Checkpoint),
    "V2 trailing layout");

_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION == 1u,
               "V1 API version");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION_V2 == 2u,
               "V2 API version");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_VERSION == 1u,
               "checkpoint schema version");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_INVALID_ARGUMENT == 0u,
               "checkpoint result numbering");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED == 1u,
               "checkpoint result numbering");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED == 2u,
               "checkpoint result numbering");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_NOT_READY == 3u,
               "checkpoint result numbering");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_MAP == 4u,
               "checkpoint result numbering");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_AUTHORITY == 5u,
               "checkpoint result numbering");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_CONFLICT == 6u,
               "checkpoint result numbering");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_BUSY == 7u,
               "checkpoint result numbering");
_Static_assert(WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_UNHEALTHY == 8u,
               "checkpoint result numbering");

int main(void)
{
    return strcmp(WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V1,
                  "WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V1") == 0 &&
                   strcmp(WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V2,
                          "WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V2") == 0
               ? 0
               : 1;
}
