/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_demo.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Allocation-free WDM1 snapshot playback cursor (FR-10-T13).
 *
 * The cursor is pointer-free.  Stream bytes and the scan index always remain
 * caller-owned and must be supplied again for each operation.  Every
 * operation revalidates their complete binding before selecting a frame.
 * This core does not open files, publish to a snapshot store, or drive a
 * client playback clock.
 */
#define WORR_NATIVE_DEMO_PLAYBACK_ABI_VERSION 1u

typedef enum worr_native_demo_playback_result_v1_e {
  WORR_NATIVE_DEMO_PLAYBACK_OK = 0,
  WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT = 1,
  WORR_NATIVE_DEMO_PLAYBACK_INVALID_CONFIG = 2,
  WORR_NATIVE_DEMO_PLAYBACK_INVALID_STATE = 3,
  WORR_NATIVE_DEMO_PLAYBACK_INVALID_METADATA = 4,
  WORR_NATIVE_DEMO_PLAYBACK_UNSUPPORTED = 5,
  WORR_NATIVE_DEMO_PLAYBACK_TRUNCATED = 6,
  WORR_NATIVE_DEMO_PLAYBACK_MALFORMED = 7,
  WORR_NATIVE_DEMO_PLAYBACK_CORRUPT = 8,
  WORR_NATIVE_DEMO_PLAYBACK_LIMIT = 9,
  WORR_NATIVE_DEMO_PLAYBACK_CAPACITY = 10,
  WORR_NATIVE_DEMO_PLAYBACK_ORDER = 11,
  WORR_NATIVE_DEMO_PLAYBACK_NOT_FOUND = 12,
  WORR_NATIVE_DEMO_PLAYBACK_MUTATED = 13,
  WORR_NATIVE_DEMO_PLAYBACK_WRONG_CLASS = 14,
  WORR_NATIVE_DEMO_PLAYBACK_WRONG_EPOCH = 15,
  WORR_NATIVE_DEMO_PLAYBACK_GAP = 16,
} worr_native_demo_playback_result_v1;

typedef struct worr_native_demo_playback_config_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t flags;
  uint32_t max_entities;
  uint32_t expected_transport_epoch;
  uint32_t expected_timeline_epoch;
  uint32_t reserved0;
  uint64_t max_records;
} worr_native_demo_playback_config_v1;

enum {
  WORR_NATIVE_DEMO_PLAYBACK_CURSOR_INITIALIZED = 1u << 0,
  WORR_NATIVE_DEMO_PLAYBACK_CURSOR_POSITIONED = 1u << 1,
};

/*
 * Persistent pointer-free binding and position.  The complete validated scan
 * is retained by value so a caller cannot silently substitute another WDM1
 * image with merely similar high-water marks.
 */
typedef struct worr_native_demo_playback_cursor_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t state_flags;
  uint32_t reset_generation;
  uint32_t reserved0;
  uint64_t current_entry_index;
  uint64_t next_entry_index;
  uint64_t last_ordinal;
  uint64_t last_time_us;
  worr_snapshot_id_v2 last_snapshot_id;
  uint64_t stream_fingerprint;
  uint64_t index_fingerprint;
  worr_native_demo_scan_config_v1 scan_config;
  worr_native_demo_scan_info_v1 scan;
  uint64_t reserved1;
} worr_native_demo_playback_cursor_v1;

enum {
  WORR_NATIVE_DEMO_PLAYBACK_FRAME_INITIAL_POSITION = 1u << 0,
  WORR_NATIVE_DEMO_PLAYBACK_FRAME_SEEK_POSITION = 1u << 1,
  WORR_NATIVE_DEMO_PLAYBACK_FRAME_FORWARD_STEP = 1u << 2,
  WORR_NATIVE_DEMO_PLAYBACK_FRAME_RESET_REQUIRED = 1u << 3,
};

/* Pointer-free metadata for one successfully decoded canonical snapshot. */
typedef struct worr_native_demo_playback_frame_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t flags;
  uint32_t reset_generation;
  uint32_t entity_count;
  uint32_t area_byte_count;
  uint32_t event_ref_count;
  uint64_t entry_index;
  worr_native_demo_index_entry_v1 entry;
  worr_snapshot_projection_hashes_v2 hashes;
  uint64_t reserved0;
} worr_native_demo_playback_frame_v1;

/*
 * Binds an exact, already scanned WDM1 image and its complete caller-owned
 * index.  The stream is rescanned and the index independently revalidated;
 * cursor_out remains unchanged on every failure.
 */
worr_native_demo_playback_result_v1 Worr_NativeDemoPlaybackInitV1(
    const worr_native_demo_playback_config_v1 *config,
    const worr_native_demo_scan_info_v1 *scan, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, worr_native_demo_playback_cursor_v1 *cursor_out);

/*
 * All decode operations are transactional across cursor, metadata, snapshot,
 * player, and the three variable-sized destinations.  A zero decoded range
 * permits either a null or non-null destination; no byte is written there.
 * Nonzero ranges require a distinct, sufficiently large destination.
 */
worr_native_demo_playback_result_v1 Worr_NativeDemoPlaybackPositionFirstV1(
    worr_native_demo_playback_cursor_v1 *cursor, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, worr_snapshot_v2 *snapshot_out,
    worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out, uint32_t entity_capacity,
    uint8_t *area_bytes_out, uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_native_demo_playback_frame_v1 *frame_out);

worr_native_demo_playback_result_v1 Worr_NativeDemoPlaybackSeekV1(
    worr_native_demo_playback_cursor_v1 *cursor, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, const worr_native_demo_seek_query_v1 *query,
    worr_snapshot_v2 *snapshot_out, worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out, uint32_t entity_capacity,
    uint8_t *area_bytes_out, uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_native_demo_playback_frame_v1 *frame_out);

worr_native_demo_playback_result_v1 Worr_NativeDemoPlaybackStepV1(
    worr_native_demo_playback_cursor_v1 *cursor, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, worr_snapshot_v2 *snapshot_out,
    worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out, uint32_t entity_capacity,
    uint8_t *area_bytes_out, uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_native_demo_playback_frame_v1 *frame_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_DEMO_PLAYBACK_STATIC_ASSERT(condition, message)            \
  static_assert((condition), message)
#else
#define WORR_NATIVE_DEMO_PLAYBACK_STATIC_ASSERT(condition, message)            \
  _Static_assert((condition), message)
#endif

WORR_NATIVE_DEMO_PLAYBACK_STATIC_ASSERT(
    sizeof(worr_native_demo_playback_config_v1) == 32,
    "native demo playback config layout changed");
WORR_NATIVE_DEMO_PLAYBACK_STATIC_ASSERT(
    sizeof(worr_native_demo_playback_cursor_v1) == 296,
    "native demo playback cursor layout changed");
WORR_NATIVE_DEMO_PLAYBACK_STATIC_ASSERT(
    offsetof(worr_native_demo_playback_cursor_v1, scan_config) == 72,
    "native demo playback scan-config offset changed");
WORR_NATIVE_DEMO_PLAYBACK_STATIC_ASSERT(
    offsetof(worr_native_demo_playback_cursor_v1, scan) == 104,
    "native demo playback scan offset changed");
WORR_NATIVE_DEMO_PLAYBACK_STATIC_ASSERT(
    sizeof(worr_native_demo_playback_frame_v1) == 160,
    "native demo playback frame layout changed");
WORR_NATIVE_DEMO_PLAYBACK_STATIC_ASSERT(
    offsetof(worr_native_demo_playback_frame_v1, entry) == 32,
    "native demo playback entry offset changed");
WORR_NATIVE_DEMO_PLAYBACK_STATIC_ASSERT(
    offsetof(worr_native_demo_playback_frame_v1, hashes) == 96,
    "native demo playback hashes offset changed");

#undef WORR_NATIVE_DEMO_PLAYBACK_STATIC_ASSERT
