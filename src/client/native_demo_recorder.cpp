/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"
#include "client/native_demo_recorder.h"
#include "client/net_capability.h"
#include "client/snapshot_shadow.h"
#include "common/net/native_demo_recorder.h"

#include <cstring>

namespace {

constexpr uint32_t native_demo_default_max_mb = 64;
constexpr uint32_t native_demo_hard_max_mb = 64;
constexpr uint64_t bytes_per_megabyte = UINT64_C(1024) * UINT64_C(1024);

enum class publication_state_t : uint8_t {
    idle = 0,
    active = 1,
    completed = 2,
    failed = 3,
};

enum class stop_reason_t : uint8_t {
    none = 0,
    user = 1,
    map_boundary = 2,
    disconnect = 3,
    core_failure = 4,
    flush_failure = 5,
    close_failure = 6,
    validation_failure = 7,
    publish_failure = 8,
};

struct client_native_demo_recorder_t {
    worr_native_demo_recorder_state_v1 core{};
    uint8_t codec_bytes[WORR_NATIVE_CODEC_MAX_ENCODED_BYTES]{};
    uint8_t record_bytes[WORR_NATIVE_DEMO_MAX_RECORD_BYTES]{};
    qhandle_t file{};
    char temp_path[MAX_OSPATH]{};
    char final_path[MAX_OSPATH]{};
    publication_state_t publication_state{publication_state_t::idle};
    stop_reason_t stop_reason{stop_reason_t::none};
    bool initialized{};
};

client_native_demo_recorder_t recorder;
cvar_t *cl_native_demo_max_mb;

const char *publication_state_name(publication_state_t state)
{
    switch (state) {
    case publication_state_t::idle:
        return "idle";
    case publication_state_t::active:
        return "active";
    case publication_state_t::completed:
        return "completed";
    case publication_state_t::failed:
        return "failed";
    }
    return "unknown";
}

const char *stop_reason_name(stop_reason_t reason)
{
    switch (reason) {
    case stop_reason_t::none:
        return "none";
    case stop_reason_t::user:
        return "user";
    case stop_reason_t::map_boundary:
        return "map_boundary";
    case stop_reason_t::disconnect:
        return "disconnect";
    case stop_reason_t::core_failure:
        return "core_failure";
    case stop_reason_t::flush_failure:
        return "flush_failure";
    case stop_reason_t::close_failure:
        return "close_failure";
    case stop_reason_t::validation_failure:
        return "validation_failure";
    case stop_reason_t::publish_failure:
        return "publish_failure";
    }
    return "unknown";
}

const char *core_result_name(uint32_t result)
{
    switch (result) {
    case WORR_NATIVE_DEMO_RECORDER_OK:
        return "ok";
    case WORR_NATIVE_DEMO_RECORDER_DUPLICATE:
        return "duplicate";
    case WORR_NATIVE_DEMO_RECORDER_INVALID_ARGUMENT:
        return "invalid_argument";
    case WORR_NATIVE_DEMO_RECORDER_INVALID_CONFIG:
        return "invalid_config";
    case WORR_NATIVE_DEMO_RECORDER_NOT_ACTIVE:
        return "not_active";
    case WORR_NATIVE_DEMO_RECORDER_CODEC:
        return "codec";
    case WORR_NATIVE_DEMO_RECORDER_FRAMING:
        return "framing";
    case WORR_NATIVE_DEMO_RECORDER_ORDER:
        return "order";
    case WORR_NATIVE_DEMO_RECORDER_TIMELINE_EPOCH:
        return "timeline_epoch";
    case WORR_NATIVE_DEMO_RECORDER_CONTROLLED_ENTITY:
        return "controlled_entity";
    case WORR_NATIVE_DEMO_RECORDER_LIMIT:
        return "limit";
    case WORR_NATIVE_DEMO_RECORDER_OUTPUT:
        return "output";
    default:
        return "unknown";
    }
}

bool path_has_parent_component_or_root(const char *path)
{
    const char *component = path;

    if (!path || !path[0] || path[0] == '/' || path[0] == '\\' ||
        std::strchr(path, ':')) {
        return true;
    }
    for (const char *cursor = path;; ++cursor) {
        if (*cursor == '/' || *cursor == '\\' || *cursor == '\0') {
            const size_t length = static_cast<size_t>(cursor - component);
            if ((length == 1 && component[0] == '.') ||
                (length == 2 && component[0] == '.' && component[1] == '.')) {
                return true;
            }
            if (*cursor == '\0')
                break;
            component = cursor + 1;
        }
    }
    return false;
}

bool normalize_recording_name(const char *input, char *base_out,
                              size_t base_capacity)
{
    char normalized[MAX_OSPATH];
    size_t length;

    if (!input || !input[0] || path_has_parent_component_or_root(input))
        return false;
    length = std::strlen(input);
    if (length == 0 ||
        static_cast<unsigned char>(input[length - 1]) <= ' ' ||
        FS_NormalizePathBuffer(normalized, input, sizeof(normalized)) >=
            sizeof(normalized) ||
        FS_ValidatePath(normalized) == PATH_INVALID || !normalized[0]) {
        return false;
    }
    if (!COM_CompareExtension(normalized, ".wdm")) {
        length = std::strlen(normalized);
        if (length <= 4)
            return false;
        normalized[length - 4] = '\0';
    }
    return normalized[0] &&
           Q_strlcpy(base_out, normalized, base_capacity) < base_capacity;
}

bool projection_hashes_equal(
    const worr_snapshot_projection_hashes_v2 &left,
    const worr_snapshot_projection_hashes_v2 &right)
{
    return left.struct_size == right.struct_size &&
           left.schema_version == right.schema_version &&
           left.endpoint_hash == right.endpoint_hash &&
           left.legacy_parity_hash == right.legacy_parity_hash &&
           left.semantic_player_hash == right.semantic_player_hash &&
           left.semantic_entity_hash == right.semantic_entity_hash &&
           left.semantic_area_hash == right.semantic_area_hash &&
           left.semantic_event_hash == right.semantic_event_hash;
}

int64_t write_file_exact(void *, const void *bytes, size_t byte_count)
{
    if (!recorder.file)
        return Q_ERR(EBADF);
    return FS_Write(bytes, byte_count, recorder.file);
}

void close_failed_recording(stop_reason_t reason, const char *detail)
{
    if (recorder.file) {
        FS_CloseFile(recorder.file);
        recorder.file = 0;
    }
    recorder.publication_state = publication_state_t::failed;
    recorder.stop_reason = reason;
    Com_EPrintf("Native snapshot recording stopped%s%s. Partial capture "
                "remains quarantined at %s.\n",
                detail && detail[0] ? ": " : "",
                detail && detail[0] ? detail : "", recorder.temp_path);
}

bool validate_temporary_stream(
    const worr_native_demo_recorder_status_v1 &expected)
{
    worr_native_demo_scan_config_v1 scan_config{};
    worr_native_demo_scan_info_v1 scan{};
    void *bytes = nullptr;
    const int loaded = FS_LoadFileEx(recorder.temp_path, &bytes, 0,
                                     TAG_FILESYSTEM);
    bool valid = false;

    if (loaded < static_cast<int>(WORR_NATIVE_DEMO_WIRE_HEADER_BYTES) ||
        !bytes ||
        static_cast<uint64_t>(loaded) != expected.stream_bytes) {
        if (bytes)
            FS_FreeFile(bytes);
        return false;
    }
    scan_config.struct_size = sizeof(scan_config);
    scan_config.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
    scan_config.max_records =
        static_cast<uint64_t>(loaded - WORR_NATIVE_DEMO_WIRE_HEADER_BYTES) /
        (WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES +
         WORR_NATIVE_CODEC_WIRE_HEADER_BYTES);
    scan_config.max_entities = expected.max_entities;
    valid = Worr_NativeDemoStreamScanV1(
                &scan_config, bytes, static_cast<size_t>(loaded), nullptr, 0,
                &scan) == WORR_NATIVE_DEMO_OK &&
            (scan.flags & WORR_NATIVE_DEMO_SCAN_COMPLETE) != 0 &&
            (scan.flags & WORR_NATIVE_DEMO_SCAN_HAS_SNAPSHOTS) != 0 &&
            expected.snapshot_count != 0 &&
            scan.record_count == expected.snapshot_count &&
            scan.snapshot_count == expected.snapshot_count &&
            scan.stream_bytes == expected.stream_bytes &&
            scan.container.transport_epoch == expected.transport_epoch &&
            scan.container.timeline_epoch == expected.timeline_epoch &&
            scan.container.capability_mask == expected.capability_mask;
    FS_FreeFile(bytes);
    return valid;
}

void finish_recording(stop_reason_t reason)
{
    worr_native_demo_recorder_status_v1 status{};
    int result;

    if (recorder.publication_state != publication_state_t::active)
        return;
    if (Worr_NativeDemoRecorderStopV1(&recorder.core) !=
            WORR_NATIVE_DEMO_RECORDER_OK ||
        !Worr_NativeDemoRecorderGetStatusV1(&recorder.core, &status)) {
        close_failed_recording(stop_reason_t::core_failure,
                               "invalid recorder state");
        return;
    }
    result = FS_Flush(recorder.file);
    if (result != Q_ERR_SUCCESS) {
        close_failed_recording(stop_reason_t::flush_failure,
                               Q_ErrorString(result));
        return;
    }
    result = FS_CloseFile(recorder.file);
    recorder.file = 0;
    if (result != Q_ERR_SUCCESS) {
        close_failed_recording(stop_reason_t::close_failure,
                               Q_ErrorString(result));
        return;
    }
    if (!validate_temporary_stream(status)) {
        close_failed_recording(stop_reason_t::validation_failure,
                               "complete-stream validation failed");
        return;
    }
    if (FS_FileExists(recorder.final_path)) {
        close_failed_recording(stop_reason_t::publish_failure,
                               "destination already exists");
        return;
    }
    result = FS_RenameFile(recorder.temp_path, recorder.final_path);
    if (result != Q_ERR_SUCCESS) {
        close_failed_recording(stop_reason_t::publish_failure,
                               Q_ErrorString(result));
        return;
    }
    recorder.publication_state = publication_state_t::completed;
    recorder.stop_reason = reason;
    Com_Printf("Published native snapshot capture %s (%" PRIu64
               " snapshots, %" PRIu64 " bytes).\n",
               recorder.final_path, status.snapshot_count,
               status.stream_bytes);
}

void CL_NativeDemoRecordStatus_f()
{
    worr_native_demo_recorder_status_v1 status{};
    const bool has_core_status =
        Worr_NativeDemoRecorderGetStatusV1(&recorder.core, &status);

    Com_Printf("native snapshot recording: state=%s reason=%s temp=%s "
               "output=%s\n",
               publication_state_name(recorder.publication_state),
               stop_reason_name(recorder.stop_reason),
               recorder.temp_path[0] ? recorder.temp_path : "-",
               recorder.final_path[0] ? recorder.final_path : "-");
    if (has_core_status) {
        Com_Printf("native snapshot recording core: active=%u failed=%u "
                   "result=%s codec=%u framing=%u snapshots=%" PRIu64
                   " duplicates=%" PRIu64 " bytes=%" PRIu64 "/%" PRIu64
                   " epoch=%u:%u next_ordinal=%" PRIu64 "\n",
                   (status.state_flags & WORR_NATIVE_DEMO_RECORDER_ACTIVE) != 0,
                   (status.state_flags & WORR_NATIVE_DEMO_RECORDER_FAILED) != 0,
                   core_result_name(status.last_result),
                   status.last_codec_result, status.last_demo_result,
                   status.snapshot_count, status.duplicate_count,
                   status.stream_bytes, status.max_stream_bytes,
                   status.transport_epoch, status.timeline_epoch,
                   status.next_ordinal);
    }
}

void CL_NativeDemoStop_f()
{
    if (recorder.publication_state != publication_state_t::active) {
        Com_Printf("No native snapshot recording is active.\n");
        return;
    }
    finish_recording(stop_reason_t::user);
}

void CL_NativeDemoRecord_f()
{
    char base[MAX_OSPATH];
    worr_net_capability_state_v1 capability{};
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    worr_snapshot_ref_v2 projection_ref{};
    worr_native_snapshot_expectation_v1 expectation{};
    worr_native_demo_recorder_config_v1 config{};
    worr_native_demo_recorder_storage_v1 storage{};
    worr_native_demo_recorder_sink_v1 sink{};
    qhandle_t file;

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: native_record <filename[.wdm]>\n");
        Com_Printf("Records promotion-qualified canonical snapshots only; "
                   "the result is not yet a playable demo.\n");
        return;
    }
    if (recorder.publication_state == publication_state_t::active) {
        Com_Printf("A native snapshot recording is already active.\n");
        return;
    }
    if (cls.state != ca_active || cls.demo.playback || cls.demo.seeking) {
        Com_Printf("Native snapshot recording requires active live play.\n");
        return;
    }
    if (cl.serverstate == ss_broadcast ||
        MVD_GetDemoStatus(nullptr, nullptr, nullptr)) {
        Com_Printf("Native snapshot recording does not yet support MVD/GTV "
                   "or spectator relay playback.\n");
        return;
    }
    if (!CL_NetCapabilityGetState(&capability) ||
        capability.phase != WORR_NET_CAPABILITY_CONFIRMED ||
        (capability.negotiated & WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK) !=
            WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK) {
        Com_Printf("The current connection has no confirmed native snapshot "
                   "bundle.\n");
        return;
    }
    if (!CL_SnapshotShadowLatest(&view, &hashes, &projection_ref) ||
        !view.snapshot ||
        (view.snapshot->flags & WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE) == 0 ||
        CL_SnapshotShadowGetNativeExpectation(
            view.snapshot->snapshot_id, &expectation) !=
            CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE ||
        !projection_hashes_equal(hashes, expectation.hashes)) {
        Com_Printf("No promotion-qualified canonical snapshot is available.\n");
        return;
    }
    if (!normalize_recording_name(Cmd_Argv(1), base, sizeof(base)) ||
        Q_concat(recorder.final_path, sizeof(recorder.final_path),
                 "demos/", base, ".wdm") >= sizeof(recorder.final_path)) {
        Com_Printf("Invalid native recording filename. Absolute paths and "
                   "parent traversal are not allowed.\n");
        return;
    }
    if (FS_FileExists(recorder.final_path)) {
        Com_Printf("Native snapshot capture already exists: %s\n",
                   recorder.final_path);
        return;
    }
    file = FS_EasyOpenFile(recorder.temp_path, sizeof(recorder.temp_path),
                           FS_MODE_WRITE | FS_FLAG_EXCL, "demos/", base,
                           ".wdm.tmp");
    if (!file)
        return;

    recorder.core = {};
    recorder.file = file;
    recorder.publication_state = publication_state_t::active;
    recorder.stop_reason = stop_reason_t::none;
    config.struct_size = sizeof(config);
    config.schema_version = WORR_NATIVE_DEMO_RECORDER_ABI_VERSION;
    config.capability_mask = capability.negotiated;
    config.transport_epoch = capability.connection_epoch;
    config.timeline_epoch = view.snapshot->snapshot_id.epoch;
    config.max_entities = static_cast<uint32_t>(cl.csr.max_edicts);
    config.created_time_us = com_unscaledTimeUs;
    config.max_stream_bytes =
        static_cast<uint64_t>(Cvar_ClampInteger(
            cl_native_demo_max_mb, 1, native_demo_hard_max_mb)) *
        bytes_per_megabyte;
    config.controlled_entity = view.snapshot->controlled_entity.identity;
    storage.struct_size = sizeof(storage);
    storage.schema_version = WORR_NATIVE_DEMO_RECORDER_ABI_VERSION;
    storage.codec_bytes = recorder.codec_bytes;
    storage.record_bytes = recorder.record_bytes;
    storage.codec_capacity = sizeof(recorder.codec_bytes);
    storage.record_capacity = sizeof(recorder.record_bytes);
    sink.struct_size = sizeof(sink);
    sink.schema_version = WORR_NATIVE_DEMO_RECORDER_ABI_VERSION;
    sink.WriteExact = write_file_exact;

    const auto begin_result = Worr_NativeDemoRecorderBeginV1(
        &recorder.core, &config, &storage, &sink);
    if (begin_result != WORR_NATIVE_DEMO_RECORDER_OK) {
        close_failed_recording(stop_reason_t::core_failure,
                               core_result_name(begin_result));
        return;
    }
    const auto snapshot_result = Worr_NativeDemoRecorderObserveSnapshotV1(
        &recorder.core, &view, &hashes, projection_ref);
    if (snapshot_result != WORR_NATIVE_DEMO_RECORDER_OK) {
        close_failed_recording(stop_reason_t::core_failure,
                               core_result_name(snapshot_result));
        return;
    }
    Com_Printf("Recording canonical snapshots to temporary capture %s "
               "(publish target %s).\n",
               recorder.temp_path, recorder.final_path);
}

const cmdreg_t native_demo_commands[] = {
    {"native_record", CL_NativeDemoRecord_f},
    {"native_stop", CL_NativeDemoStop_f},
    {"native_record_status", CL_NativeDemoRecordStatus_f},
    {nullptr},
};

} // namespace

extern "C" void CL_NativeDemoRecorderInit(void)
{
    if (recorder.initialized)
        return;
    recorder.initialized = true;
    cl_native_demo_max_mb = Cvar_Get(
        "cl_native_demo_max_mb", va("%u", native_demo_default_max_mb), 0);
    Cmd_Register(native_demo_commands);
    CL_SnapshotShadowSetRecordObserver(
        CL_NativeDemoRecorderObserveSnapshot);
}

extern "C" void CL_NativeDemoRecorderCleanup(void)
{
    if (recorder.publication_state == publication_state_t::active)
        finish_recording(stop_reason_t::disconnect);
}

extern "C" void CL_NativeDemoRecorderMapBoundary(void)
{
    if (recorder.publication_state == publication_state_t::active)
        finish_recording(stop_reason_t::map_boundary);
}

extern "C" void CL_NativeDemoRecorderObserveSnapshot(
    const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_projection_hashes_v2 *hashes,
    worr_snapshot_ref_v2 projection_ref)
{
    if (recorder.publication_state != publication_state_t::active)
        return;
    const auto result = Worr_NativeDemoRecorderObserveSnapshotV1(
        &recorder.core, view, hashes, projection_ref);
    if (result == WORR_NATIVE_DEMO_RECORDER_OK ||
        result == WORR_NATIVE_DEMO_RECORDER_DUPLICATE) {
        return;
    }
    close_failed_recording(stop_reason_t::core_failure,
                           core_result_name(result));
}
