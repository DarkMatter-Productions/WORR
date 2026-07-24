/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"
#include "client/native_demo_player.h"
#include "client/snapshot_shadow.h"

#include "common/net/native_demo.h"
#include "common/net/native_demo_playback.h"
#include "common/net/native_demo_timeline.h"
#include "common/net/snapshot_projection.h"

#include <cstring>

namespace {

/* Decode scratch is fixed-capacity; a snapshot whose record exceeds any of
 * these is skipped and reported rather than growing per frame. */
constexpr uint32_t player_entity_capacity = 1024;
constexpr uint32_t player_area_capacity = 4096;
constexpr uint32_t player_event_ref_capacity = 256;
constexpr uint64_t player_max_records = UINT64_C(1) << 20;

struct client_native_demo_player_t {
    bool initialized{};
    bool loaded{};
    void *bytes{};
    size_t byte_count{};
    worr_native_demo_index_entry_v1 *index{};
    worr_native_demo_timeline_step_v1 *steps{};
    uint64_t record_count{};
    worr_native_demo_playback_cursor_v1 playback_cursor{};
    worr_native_demo_timeline_summary_v1 summary{};
    worr_native_demo_timeline_cursor_v1 exec_cursor{};
    uint64_t clock_us{};
    uint64_t last_real_us{};
    uint64_t presented_snapshots{};
    uint64_t skipped_no_consumer{};
    bool clock_started{};
    char path[MAX_OSPATH]{};
};

client_native_demo_player_t player;
cvar_t *cl_native_demo_play;

worr_snapshot_entity_v2 decode_entities[player_entity_capacity];
uint8_t decode_area[player_area_capacity];
worr_snapshot_event_ref_v2 decode_event_refs[player_event_ref_capacity];

void release_storage()
{
    if (player.bytes) {
        FS_FreeFile(player.bytes);
        player.bytes = nullptr;
    }
    if (player.index) {
        Z_Free(player.index);
        player.index = nullptr;
    }
    if (player.steps) {
        Z_Free(player.steps);
        player.steps = nullptr;
    }
    player.byte_count = 0;
    player.record_count = 0;
    player.loaded = false;
    player.clock_started = false;
    player.clock_us = 0;
    player.presented_snapshots = 0;
    player.skipped_no_consumer = 0;
    player.path[0] = '\0';
}

bool normalize_play_name(const char *in, char *out, size_t out_size)
{
    if (!in || !*in)
        return false;
    /* Reject absolute paths and parent traversal, mirroring the recorder. */
    if (strchr(in, ':') || strstr(in, ".."))
        return false;
    if (in[0] == '/' || in[0] == '\\')
        return false;
    return Q_strlcpy(out, in, out_size) < out_size;
}

/* Opens the loaded byte image, sizing the index from a count-only pass. */
bool open_loaded_stream(const worr_native_demo_container_info_v1 *info)
{
    worr_native_demo_playback_config_v1 config{};
    uint64_t record_count = 0;
    worr_native_demo_playback_result_v1 result;

    config.struct_size = sizeof(config);
    config.schema_version = WORR_NATIVE_DEMO_PLAYBACK_ABI_VERSION;
    config.max_entities = player_entity_capacity;
    config.expected_transport_epoch = info->transport_epoch;
    config.expected_timeline_epoch = info->timeline_epoch;
    config.max_records = player_max_records;

    /* Count-only pass: CAPACITY reports the required entry count. */
    result = Worr_NativeDemoPlaybackOpenV1(&config, player.bytes,
                                           player.byte_count, nullptr, 0,
                                           &player.playback_cursor,
                                           &record_count);
    if (result != WORR_NATIVE_DEMO_PLAYBACK_CAPACITY &&
        result != WORR_NATIVE_DEMO_PLAYBACK_OK) {
        Com_Printf("Native demo open failed (%d).\n", (int)result);
        return false;
    }
    if (record_count == 0 || record_count > player_max_records) {
        Com_Printf("Native demo has no playable records.\n");
        return false;
    }

    player.index = (worr_native_demo_index_entry_v1 *)Z_Malloc(
        sizeof(worr_native_demo_index_entry_v1) * (size_t)record_count);
    result = Worr_NativeDemoPlaybackOpenV1(
        &config, player.bytes, player.byte_count, player.index,
        (size_t)record_count, &player.playback_cursor, &record_count);
    if (result != WORR_NATIVE_DEMO_PLAYBACK_OK) {
        Com_Printf("Native demo index build failed (%d).\n", (int)result);
        return false;
    }
    player.record_count = record_count;
    return true;
}

/* Builds the present-once plan and arms the execution cursor. */
bool build_plan()
{
    worr_native_demo_timeline_config_v1 tl_config{};
    worr_native_demo_timeline_result_v1 result;

    tl_config.struct_size = sizeof(tl_config);
    tl_config.schema_version = 1u;

    player.steps = (worr_native_demo_timeline_step_v1 *)Z_Malloc(
        sizeof(worr_native_demo_timeline_step_v1) *
        (size_t)player.record_count);
    result = Worr_NativeDemoTimelineBuildV1(
        &tl_config, player.bytes, player.byte_count, player.index,
        (size_t)player.record_count, player.steps,
        (size_t)player.record_count, &player.summary);
    if (result != WORR_NATIVE_DEMO_TIMELINE_OK) {
        Com_Printf("Native demo timeline is not a valid present-once plan "
                   "(%d).\n",
                   (int)result);
        return false;
    }
    result = Worr_NativeDemoTimelineCursorInitV1(&player.summary,
                                                 &player.exec_cursor);
    if (result != WORR_NATIVE_DEMO_TIMELINE_OK) {
        Com_Printf("Native demo cursor init failed (%d).\n", (int)result);
        return false;
    }
    return true;
}

/* Decodes and presents the snapshot at the given index through the cgame
 * timeline consumer, when one is available.  Returns true if presented. */
bool present_snapshot_at(uint64_t entry_index, uint64_t receive_time_us)
{
    worr_snapshot_v2 snapshot{};
    worr_snapshot_player_v2 snapshot_player{};
    worr_native_demo_playback_frame_v1 frame{};
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    worr_native_snapshot_consumer_v1 consumer{};
    worr_native_demo_playback_result_v1 decode;

    decode = Worr_NativeDemoPlaybackDecodeSnapshotAtV1(
        &player.playback_cursor, player.bytes, player.byte_count, player.index,
        (size_t)player.record_count, entry_index, &snapshot, &snapshot_player,
        decode_entities, player_entity_capacity, decode_area,
        player_area_capacity, decode_event_refs, player_event_ref_capacity,
        &frame);
    if (decode != WORR_NATIVE_DEMO_PLAYBACK_OK)
        return false;

    /* A fully playable session requires cgame to be loaded (the standalone
     * bootstrap / map-load seam is still open).  With no consumer available
     * the plan still advances; we just have nothing to present into. */
    if (!CL_SnapshotShadowGetNativeConsumerV1(&consumer) ||
        consumer.ConsumeCanonicalSnapshot == nullptr) {
        player.skipped_no_consumer += 1;
        return false;
    }

    view.struct_size = sizeof(view);
    view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    view.snapshot = &snapshot;
    view.player = &snapshot_player;
    view.entities = frame.entity_count ? decode_entities : nullptr;
    view.area_bytes = frame.area_byte_count ? decode_area : nullptr;
    view.event_refs = frame.event_ref_count ? decode_event_refs : nullptr;
    view.entity_count = frame.entity_count;
    view.area_byte_count = frame.area_byte_count;
    view.event_ref_count = frame.event_ref_count;
    if (!Worr_SnapshotProjectionHashesV2(&view, player_entity_capacity,
                                         &hashes))
        return false;

    if (consumer.ConsumeCanonicalSnapshot(consumer.opaque, &view, &hashes,
                                          receive_time_us)) {
        player.presented_snapshots += 1;
        return true;
    }
    return false;
}

void CL_NativeDemoPlay_f()
{
    char base[MAX_OSPATH];
    char full[MAX_OSPATH];
    worr_native_demo_container_info_v1 info{};
    void *bytes = nullptr;
    int length;

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: native_play <filename[.wdm]>\n");
        Com_Printf("Loads a native (WDM1) demo and drives its present-once "
                   "timeline. Requires cl_native_demo_play 1.\n");
        return;
    }
    if (!cl_native_demo_play || cl_native_demo_play->integer == 0) {
        Com_Printf("Native demo playback is disabled. Set cl_native_demo_play "
                   "1 to enable.\n");
        return;
    }
    if (player.loaded) {
        Com_Printf("A native demo is already loaded. Use native_stop_play "
                   "first.\n");
        return;
    }
    if (!normalize_play_name(Cmd_Argv(1), base, sizeof(base)) ||
        Q_concat(full, sizeof(full), "demos/", base, ".wdm") >= sizeof(full)) {
        Com_Printf("Invalid native demo filename. Absolute paths and parent "
                   "traversal are not allowed.\n");
        return;
    }

    length = FS_LoadFile(full, &bytes);
    if (length <= 0 || bytes == nullptr) {
        /* A zero-length file still returns a (tiny) non-null allocation; free
         * it here since player.bytes has not been assigned yet. */
        if (bytes)
            FS_FreeFile(bytes);
        Com_Printf("Couldn't open %s.\n", full);
        return;
    }

    player.bytes = bytes;
    player.byte_count = (size_t)length;
    Q_strlcpy(player.path, full, sizeof(player.path));

    if (Worr_NativeDemoContainerDecodeV1(player.bytes, player.byte_count,
                                         &info) != WORR_NATIVE_DEMO_OK) {
        Com_Printf("%s is not a valid native (WDM1) demo.\n", full);
        release_storage();
        return;
    }
    if (!open_loaded_stream(&info) || !build_plan()) {
        release_storage();
        return;
    }

    player.loaded = true;
    player.clock_started = false;
    Com_Printf("Loaded native demo %s: %llu records (%llu snapshots, %llu "
               "commands, %llu events).\n",
               full, (unsigned long long)player.summary.step_count,
               (unsigned long long)player.summary.snapshot_count,
               (unsigned long long)player.summary.command_count,
               (unsigned long long)player.summary.event_count);
    worr_native_snapshot_consumer_v1 probe{};
    if (!CL_SnapshotShadowGetNativeConsumerV1(&probe))
        Com_Printf("Note: no cgame timeline is active, so the plan advances "
                   "but presents nothing until a map is loaded (standalone "
                   "bootstrap is not yet wired).\n");
}

void CL_NativeDemoStopPlay_f()
{
    if (!player.loaded) {
        Com_Printf("No native demo is loaded.\n");
        return;
    }
    Com_Printf("Stopped native demo playback (%llu/%llu snapshots presented).\n",
               (unsigned long long)player.presented_snapshots,
               (unsigned long long)player.summary.snapshot_count);
    release_storage();
}

void CL_NativeDemoPlayStatus_f()
{
    if (!player.loaded) {
        Com_Printf("Native demo playback: idle "
                   "(cl_native_demo_play %d).\n",
                   cl_native_demo_play ? cl_native_demo_play->integer : 0);
        return;
    }
    Com_Printf("Native demo playback: %s\n", player.path);
    Com_Printf("  plan: %llu steps, %llu snapshots, %llu commands, %llu "
               "events\n",
               (unsigned long long)player.summary.step_count,
               (unsigned long long)player.summary.snapshot_count,
               (unsigned long long)player.summary.command_count,
               (unsigned long long)player.summary.event_count);
    Com_Printf("  frontier: %llu/%llu; presented %llu snapshots; %llu skipped "
               "(no consumer)\n",
               (unsigned long long)player.exec_cursor.next_step,
               (unsigned long long)player.summary.step_count,
               (unsigned long long)player.presented_snapshots,
               (unsigned long long)player.skipped_no_consumer);
}

const cmdreg_t native_demo_player_commands[] = {
    {"native_play", CL_NativeDemoPlay_f},
    {"native_stop_play", CL_NativeDemoStopPlay_f},
    {"native_play_status", CL_NativeDemoPlayStatus_f},
    {nullptr},
};

} // namespace

extern "C" void CL_NativeDemoPlayerInit(void)
{
    if (player.initialized)
        return;
    player.initialized = true;
    cl_native_demo_play = Cvar_Get("cl_native_demo_play", "0", 0);
    Cmd_Register(native_demo_player_commands);
}

extern "C" void CL_NativeDemoPlayerCleanup(void)
{
    if (player.loaded)
        release_storage();
}

extern "C" void CL_NativeDemoPlayerFrame(void)
{
    worr_native_demo_timeline_batch_v1 batch{};
    uint64_t now;
    uint64_t i;

    if (!player.loaded)
        return;

    now = com_unscaledTimeUs;
    if (!cl_native_demo_play || cl_native_demo_play->integer == 0) {
        /* Disabled acts as pause: keep the real-time anchor current so the
         * disabled span is not later counted as demo-clock advance (which would
         * fast-forward the whole paused duration in one frame on re-enable). */
        player.last_real_us = now;
        return;
    }

    if (!player.clock_started) {
        /* Anchor the demo clock at the first step's time. */
        player.clock_us = player.steps[0].time_us;
        player.last_real_us = now;
        player.clock_started = true;
    } else {
        player.clock_us += now - player.last_real_us;
        player.last_real_us = now;
    }

    if (Worr_NativeDemoTimelineCursorAdvanceToTimeV1(
            &player.exec_cursor, player.steps, (size_t)player.record_count,
            player.summary.plan_fingerprint, player.clock_us, &batch) !=
        WORR_NATIVE_DEMO_TIMELINE_OK) {
        return;
    }

    for (i = 0; i < batch.count; ++i) {
        const uint64_t step = batch.first_step + i;
        if (player.steps[step].record_kind ==
            WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1) {
            (void)present_snapshot_at(player.steps[step].entry_index,
                                      player.steps[step].time_us);
        }
        /* Command and event replay into the live runtime present through the
         * command/event consumers; wiring those is the remaining live-runtime
         * step tracked in the roadmap and the verification runbook. */
    }
}
