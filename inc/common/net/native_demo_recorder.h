/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_demo.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bounded snapshot-only WDM1 recording state (FR-10-T13).
 *
 * This layer owns framing, canonical ordering, byte/ordinal accounting and
 * exact-write transactions.  It owns no filesystem paths or file handles.
 * The caller supplies two fixed, non-overlapping scratch regions and an exact
 * write sink.  No projection pointer is retained after ObserveSnapshot.
 */
#define WORR_NATIVE_DEMO_RECORDER_ABI_VERSION 1u

enum {
  WORR_NATIVE_DEMO_RECORDER_INITIALIZED = 1u << 0,
  WORR_NATIVE_DEMO_RECORDER_ACTIVE = 1u << 1,
  WORR_NATIVE_DEMO_RECORDER_FAILED = 1u << 2,
  WORR_NATIVE_DEMO_RECORDER_STOPPED = 1u << 3,
  WORR_NATIVE_DEMO_RECORDER_HAS_SNAPSHOT = 1u << 4,
};

typedef enum worr_native_demo_recorder_result_v1_e {
  WORR_NATIVE_DEMO_RECORDER_OK = 0,
  WORR_NATIVE_DEMO_RECORDER_DUPLICATE = 1,
  WORR_NATIVE_DEMO_RECORDER_INVALID_ARGUMENT = 2,
  WORR_NATIVE_DEMO_RECORDER_INVALID_CONFIG = 3,
  WORR_NATIVE_DEMO_RECORDER_NOT_ACTIVE = 4,
  WORR_NATIVE_DEMO_RECORDER_CODEC = 5,
  WORR_NATIVE_DEMO_RECORDER_FRAMING = 6,
  WORR_NATIVE_DEMO_RECORDER_ORDER = 7,
  WORR_NATIVE_DEMO_RECORDER_TIMELINE_EPOCH = 8,
  WORR_NATIVE_DEMO_RECORDER_CONTROLLED_ENTITY = 9,
  WORR_NATIVE_DEMO_RECORDER_LIMIT = 10,
  WORR_NATIVE_DEMO_RECORDER_OUTPUT = 11,
} worr_native_demo_recorder_result_v1;

/* Return the exact number of bytes accepted, or a negative error. */
typedef int64_t (*worr_native_demo_recorder_write_v1)(
    void *opaque, const void *bytes, size_t byte_count);

typedef struct worr_native_demo_recorder_sink_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t reserved0;
  void *opaque;
  worr_native_demo_recorder_write_v1 WriteExact;
} worr_native_demo_recorder_sink_v1;

typedef struct worr_native_demo_recorder_storage_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t reserved0;
  uint8_t *codec_bytes;
  uint8_t *record_bytes;
  size_t codec_capacity;
  size_t record_capacity;
} worr_native_demo_recorder_storage_v1;

typedef struct worr_native_demo_recorder_config_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t reserved0;
  uint32_t capability_mask;
  uint32_t transport_epoch;
  uint32_t timeline_epoch;
  uint32_t max_entities;
  uint64_t created_time_us;
  uint64_t max_stream_bytes;
  worr_event_entity_ref_v1 controlled_entity;
  uint64_t reserved1;
} worr_native_demo_recorder_config_v1;

/* Pointer-free status suitable for console and test diagnostics. */
typedef struct worr_native_demo_recorder_status_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t state_flags;
  uint32_t last_result;
  uint32_t last_codec_result;
  uint32_t last_demo_result;
  uint32_t capability_mask;
  uint32_t transport_epoch;
  uint32_t timeline_epoch;
  uint32_t max_entities;
  uint64_t max_stream_bytes;
  uint64_t stream_bytes;
  uint64_t snapshot_count;
  uint64_t duplicate_count;
  uint64_t next_ordinal;
  int64_t last_write_result;
  worr_snapshot_id_v2 last_snapshot_id;
  worr_event_entity_ref_v1 controlled_entity;
  uint64_t last_endpoint_hash;
  uint64_t last_snapshot_hash;
} worr_native_demo_recorder_status_v1;

/* Runtime owner.  Pointer fields are process-local and never serialized. */
typedef struct worr_native_demo_recorder_state_v1_s {
  worr_native_demo_recorder_status_v1 status;
  worr_native_demo_recorder_config_v1 config;
  worr_native_demo_order_state_v1 order;
  worr_native_demo_recorder_write_v1 WriteExact;
  void *write_opaque;
  uint8_t *codec_bytes;
  uint8_t *record_bytes;
  size_t codec_capacity;
  size_t record_capacity;
  worr_snapshot_ref_v2 last_projection_ref;
} worr_native_demo_recorder_state_v1;

/*
 * Begin writes exactly one WDM1 header.  A short/failed sink write leaves a
 * terminal FAILED state with stream_bytes still zero.  Invalid arguments and
 * configuration leave state_out untouched.
 */
worr_native_demo_recorder_result_v1 Worr_NativeDemoRecorderBeginV1(
    worr_native_demo_recorder_state_v1 *state_out,
    const worr_native_demo_recorder_config_v1 *config,
    const worr_native_demo_recorder_storage_v1 *storage,
    const worr_native_demo_recorder_sink_v1 *sink);

/*
 * Encodes one complete immutable snapshot to WNC1/WDR1 staging, validates a
 * private next ordering state, and commits accounting only after an exact
 * sink write.  DUPLICATE performs no write and leaves the recorder active.
 * Every other runtime rejection is terminal so no admitted snapshot is
 * silently skipped.
 */
worr_native_demo_recorder_result_v1 Worr_NativeDemoRecorderObserveSnapshotV1(
    worr_native_demo_recorder_state_v1 *state,
    const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_projection_hashes_v2 *hashes,
    worr_snapshot_ref_v2 projection_ref);

worr_native_demo_recorder_result_v1 Worr_NativeDemoRecorderStopV1(
    worr_native_demo_recorder_state_v1 *state);
bool Worr_NativeDemoRecorderGetStatusV1(
    const worr_native_demo_recorder_state_v1 *state,
    worr_native_demo_recorder_status_v1 *status_out);

#ifdef __cplusplus
}
#endif
