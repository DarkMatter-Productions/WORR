/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_demo_recorder.h"

#include <stdbool.h>
#include <string.h>

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes) {
  const uintptr_t left_begin = (uintptr_t)left;
  const uintptr_t right_begin = (uintptr_t)right;
  uintptr_t left_end;
  uintptr_t right_end;

  if (left == NULL || right == NULL || left_bytes == 0 || right_bytes == 0)
    return false;
  if (left_bytes > UINTPTR_MAX - left_begin ||
      right_bytes > UINTPTR_MAX - right_begin) {
    return true;
  }
  left_end = left_begin + left_bytes;
  right_end = right_begin + right_bytes;
  return left_begin < right_end && right_begin < left_end;
}

static bool config_valid(
    const worr_native_demo_recorder_config_v1 *config) {
  return config != NULL && config->struct_size == sizeof(*config) &&
         config->schema_version == WORR_NATIVE_DEMO_RECORDER_ABI_VERSION &&
         config->reserved0 == 0 &&
         (config->capability_mask & ~WORR_NET_CAP_KNOWN_MASK) == 0 &&
         (config->capability_mask & WORR_NET_CAP_CANONICAL_SNAPSHOT_V2) != 0 &&
         config->transport_epoch != 0 && config->timeline_epoch != 0 &&
         config->max_entities != 0 &&
         config->max_stream_bytes >= WORR_NATIVE_DEMO_WIRE_HEADER_BYTES &&
         Worr_EventEntityRefValidV1(config->controlled_entity,
                                    config->max_entities, false) &&
         config->reserved1 == 0;
}

static bool storage_valid(
    const worr_native_demo_recorder_storage_v1 *storage) {
  return storage != NULL && storage->struct_size == sizeof(*storage) &&
         storage->schema_version == WORR_NATIVE_DEMO_RECORDER_ABI_VERSION &&
         storage->reserved0 == 0 && storage->codec_bytes != NULL &&
         storage->record_bytes != NULL &&
         storage->codec_capacity >= WORR_NATIVE_CODEC_MAX_ENCODED_BYTES &&
         storage->record_capacity >= WORR_NATIVE_DEMO_MAX_RECORD_BYTES &&
         !ranges_overlap(storage->codec_bytes, storage->codec_capacity,
                         storage->record_bytes, storage->record_capacity);
}

static bool sink_valid(const worr_native_demo_recorder_sink_v1 *sink) {
  return sink != NULL && sink->struct_size == sizeof(*sink) &&
         sink->schema_version == WORR_NATIVE_DEMO_RECORDER_ABI_VERSION &&
         sink->reserved0 == 0 && sink->WriteExact != NULL;
}

static bool state_initialized(
    const worr_native_demo_recorder_state_v1 *state) {
  return state != NULL &&
         state->status.struct_size == sizeof(state->status) &&
         state->status.schema_version ==
             WORR_NATIVE_DEMO_RECORDER_ABI_VERSION &&
         (state->status.state_flags &
          WORR_NATIVE_DEMO_RECORDER_INITIALIZED) != 0;
}

static worr_native_demo_recorder_result_v1 fail_active(
    worr_native_demo_recorder_state_v1 *state,
    worr_native_demo_recorder_result_v1 result) {
  if (state_initialized(state)) {
    state->status.state_flags &=
        (uint16_t)~WORR_NATIVE_DEMO_RECORDER_ACTIVE;
    state->status.state_flags |= WORR_NATIVE_DEMO_RECORDER_FAILED;
    state->status.last_result = result;
  }
  return result;
}

static bool projection_hashes_valid(
    const worr_snapshot_projection_hashes_v2 *hashes) {
  return hashes != NULL && hashes->struct_size == sizeof(*hashes) &&
         hashes->schema_version == WORR_SNAPSHOT_PROJECTION_VERSION;
}

static bool projection_hashes_equal(
    const worr_snapshot_projection_hashes_v2 *left,
    const worr_snapshot_projection_hashes_v2 *right) {
  return left->struct_size == right->struct_size &&
         left->schema_version == right->schema_version &&
         left->endpoint_hash == right->endpoint_hash &&
         left->legacy_parity_hash == right->legacy_parity_hash &&
         left->semantic_player_hash == right->semantic_player_hash &&
         left->semantic_entity_hash == right->semantic_entity_hash &&
         left->semantic_area_hash == right->semantic_area_hash &&
         left->semantic_event_hash == right->semantic_event_hash;
}

static bool hashes_match_view(
    const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_projection_hashes_v2 *hashes,
    uint32_t max_entities) {
  worr_snapshot_projection_hashes_v2 calculated;

  memset(&calculated, 0, sizeof(calculated));
  return Worr_SnapshotProjectionHashesV2(view, max_entities, &calculated) &&
         projection_hashes_equal(&calculated, hashes);
}

static bool entity_ref_equal(worr_event_entity_ref_v1 left,
                             worr_event_entity_ref_v1 right) {
  return left.index == right.index && left.generation == right.generation;
}

static bool snapshot_id_equal(worr_snapshot_id_v2 left,
                              worr_snapshot_id_v2 right) {
  return left.epoch == right.epoch && left.sequence == right.sequence;
}

worr_native_demo_recorder_result_v1 Worr_NativeDemoRecorderBeginV1(
    worr_native_demo_recorder_state_v1 *state_out,
    const worr_native_demo_recorder_config_v1 *config,
    const worr_native_demo_recorder_storage_v1 *storage,
    const worr_native_demo_recorder_sink_v1 *sink) {
  worr_native_demo_recorder_state_v1 next;
  worr_native_demo_container_config_v1 container;
  worr_native_demo_container_info_v1 container_info;
  worr_native_demo_result_v1 demo_result;
  size_t header_bytes = 0;
  int64_t written;

  if (state_out == NULL || config == NULL || storage == NULL || sink == NULL)
    return WORR_NATIVE_DEMO_RECORDER_INVALID_ARGUMENT;
  if (!config_valid(config) || !storage_valid(storage) || !sink_valid(sink) ||
      ranges_overlap(state_out, sizeof(*state_out), storage->codec_bytes,
                     storage->codec_capacity) ||
      ranges_overlap(state_out, sizeof(*state_out), storage->record_bytes,
                     storage->record_capacity)) {
    return WORR_NATIVE_DEMO_RECORDER_INVALID_CONFIG;
  }

  memset(&next, 0, sizeof(next));
  next.status.struct_size = sizeof(next.status);
  next.status.schema_version = WORR_NATIVE_DEMO_RECORDER_ABI_VERSION;
  next.status.state_flags = WORR_NATIVE_DEMO_RECORDER_INITIALIZED;
  next.status.last_result = WORR_NATIVE_DEMO_RECORDER_OK;
  next.status.last_codec_result = WORR_NATIVE_CODEC_OK;
  next.status.last_demo_result = WORR_NATIVE_DEMO_OK;
  next.status.capability_mask = config->capability_mask;
  next.status.transport_epoch = config->transport_epoch;
  next.status.timeline_epoch = config->timeline_epoch;
  next.status.max_entities = config->max_entities;
  next.status.max_stream_bytes = config->max_stream_bytes;
  next.status.next_ordinal = 1;
  next.status.controlled_entity = config->controlled_entity;
  next.config = *config;
  next.WriteExact = sink->WriteExact;
  next.write_opaque = sink->opaque;
  next.codec_bytes = storage->codec_bytes;
  next.record_bytes = storage->record_bytes;
  next.codec_capacity = storage->codec_capacity;
  next.record_capacity = storage->record_capacity;

  memset(&container, 0, sizeof(container));
  container.struct_size = sizeof(container);
  container.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  container.transport = WORR_NATIVE_DEMO_TRANSPORT_WNC1_V1;
  container.capability_mask = config->capability_mask;
  container.transport_epoch = config->transport_epoch;
  container.timeline_epoch = config->timeline_epoch;
  container.first_record_ordinal = 1;
  container.created_time_us = config->created_time_us;
  demo_result = Worr_NativeDemoContainerEncodeV1(
      &container, next.record_bytes, next.record_capacity, &header_bytes);
  next.status.last_demo_result = demo_result;
  if (demo_result != WORR_NATIVE_DEMO_OK) {
    next.status.state_flags |= WORR_NATIVE_DEMO_RECORDER_FAILED;
    next.status.last_result = WORR_NATIVE_DEMO_RECORDER_FRAMING;
    *state_out = next;
    return WORR_NATIVE_DEMO_RECORDER_FRAMING;
  }
  demo_result = Worr_NativeDemoContainerDecodeV1(
      next.record_bytes, header_bytes, &container_info);
  if (demo_result == WORR_NATIVE_DEMO_OK)
    demo_result = Worr_NativeDemoOrderInitV1(&container_info, &next.order);
  next.status.last_demo_result = demo_result;
  if (demo_result != WORR_NATIVE_DEMO_OK) {
    next.status.state_flags |= WORR_NATIVE_DEMO_RECORDER_FAILED;
    next.status.last_result = WORR_NATIVE_DEMO_RECORDER_FRAMING;
    *state_out = next;
    return WORR_NATIVE_DEMO_RECORDER_FRAMING;
  }

  written = next.WriteExact(next.write_opaque, next.record_bytes, header_bytes);
  next.status.last_write_result = written;
  if (written != (int64_t)header_bytes) {
    next.status.state_flags |= WORR_NATIVE_DEMO_RECORDER_FAILED;
    next.status.last_result = WORR_NATIVE_DEMO_RECORDER_OUTPUT;
    *state_out = next;
    return WORR_NATIVE_DEMO_RECORDER_OUTPUT;
  }

  next.status.stream_bytes = header_bytes;
  next.status.state_flags |= WORR_NATIVE_DEMO_RECORDER_ACTIVE;
  *state_out = next;
  return WORR_NATIVE_DEMO_RECORDER_OK;
}

worr_native_demo_recorder_result_v1 Worr_NativeDemoRecorderObserveSnapshotV1(
    worr_native_demo_recorder_state_v1 *state,
    const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_projection_hashes_v2 *hashes,
    worr_snapshot_ref_v2 projection_ref) {
  worr_native_demo_order_state_v1 next_order;
  worr_native_demo_record_v1 record;
  worr_native_demo_record_info_v1 record_info;
  worr_native_codec_result_v1 codec_result;
  worr_native_demo_result_v1 demo_result;
  size_t codec_bytes = 0;
  size_t record_bytes = 0;
  int64_t written;

  if (!state_initialized(state) || view == NULL || hashes == NULL)
    return WORR_NATIVE_DEMO_RECORDER_INVALID_ARGUMENT;
  if ((state->status.state_flags & WORR_NATIVE_DEMO_RECORDER_ACTIVE) == 0)
    return WORR_NATIVE_DEMO_RECORDER_NOT_ACTIVE;
  if (view->snapshot == NULL || !projection_hashes_valid(hashes) ||
      projection_ref.generation == 0) {
    return fail_active(state, WORR_NATIVE_DEMO_RECORDER_INVALID_ARGUMENT);
  }

  if ((state->status.state_flags &
       WORR_NATIVE_DEMO_RECORDER_HAS_SNAPSHOT) != 0) {
    if (snapshot_id_equal(state->status.last_snapshot_id,
                          view->snapshot->snapshot_id)) {
      /* Store-slot reuse must not turn the same canonical snapshot into a
       * second record. Ref identity is diagnostic; canonical ID + hashes are
       * the durable deduplication identity. */
      if (state->status.last_endpoint_hash == hashes->endpoint_hash &&
          state->status.last_snapshot_hash == view->snapshot->snapshot_hash) {
        if (!hashes_match_view(view, hashes, state->config.max_entities))
          return fail_active(state, WORR_NATIVE_DEMO_RECORDER_CODEC);
        if (state->status.duplicate_count != UINT64_MAX)
          ++state->status.duplicate_count;
        state->status.last_result = WORR_NATIVE_DEMO_RECORDER_DUPLICATE;
        return WORR_NATIVE_DEMO_RECORDER_DUPLICATE;
      }
      return fail_active(state, WORR_NATIVE_DEMO_RECORDER_ORDER);
    }
    if (view->snapshot->snapshot_id.epoch ==
            state->status.last_snapshot_id.epoch &&
        view->snapshot->snapshot_id.sequence <=
            state->status.last_snapshot_id.sequence) {
      return fail_active(state, WORR_NATIVE_DEMO_RECORDER_ORDER);
    }
  }

  if (view->snapshot->snapshot_id.epoch != state->config.timeline_epoch)
    return fail_active(state,
                       WORR_NATIVE_DEMO_RECORDER_TIMELINE_EPOCH);
  if (!entity_ref_equal(view->snapshot->controlled_entity.identity,
                        state->config.controlled_entity)) {
    return fail_active(state,
                       WORR_NATIVE_DEMO_RECORDER_CONTROLLED_ENTITY);
  }
  if (!hashes_match_view(view, hashes, state->config.max_entities))
    return fail_active(state, WORR_NATIVE_DEMO_RECORDER_CODEC);
  if (state->status.next_ordinal == 0)
    return fail_active(state, WORR_NATIVE_DEMO_RECORDER_LIMIT);

  codec_result = Worr_NativeCodecSnapshotEncodeV1(
      view, state->config.max_entities, state->codec_bytes,
      state->codec_capacity, &codec_bytes);
  state->status.last_codec_result = codec_result;
  if (codec_result != WORR_NATIVE_CODEC_OK)
    return fail_active(state, WORR_NATIVE_DEMO_RECORDER_CODEC);

  memset(&record, 0, sizeof(record));
  record.struct_size = sizeof(record);
  record.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  record.record_kind = WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1;
  record.ordinal = state->status.next_ordinal;
  record.time_us = view->snapshot->server_time_us;
  demo_result = Worr_NativeDemoRecordEncodeV1(
      &record, state->codec_bytes, codec_bytes, state->record_bytes,
      state->record_capacity, &record_bytes);
  state->status.last_demo_result = demo_result;
  if (demo_result != WORR_NATIVE_DEMO_OK)
    return fail_active(state, WORR_NATIVE_DEMO_RECORDER_FRAMING);
  if (state->status.stream_bytes > UINT64_MAX - (uint64_t)record_bytes ||
      (uint64_t)record_bytes > state->config.max_stream_bytes ||
      state->status.stream_bytes + (uint64_t)record_bytes >
          state->config.max_stream_bytes) {
    return fail_active(state, WORR_NATIVE_DEMO_RECORDER_LIMIT);
  }

  demo_result = Worr_NativeDemoRecordDecodeV1(
      state->record_bytes, record_bytes, state->status.stream_bytes,
      &record_info);
  next_order = state->order;
  if (demo_result == WORR_NATIVE_DEMO_OK)
    demo_result = Worr_NativeDemoOrderObserveV1(&next_order, &record_info);
  state->status.last_demo_result = demo_result;
  if (demo_result != WORR_NATIVE_DEMO_OK) {
    return fail_active(
        state, demo_result == WORR_NATIVE_DEMO_ORDER
                   ? WORR_NATIVE_DEMO_RECORDER_ORDER
                   : WORR_NATIVE_DEMO_RECORDER_FRAMING);
  }

  written = state->WriteExact(state->write_opaque, state->record_bytes,
                              record_bytes);
  state->status.last_write_result = written;
  if (written != (int64_t)record_bytes)
    return fail_active(state, WORR_NATIVE_DEMO_RECORDER_OUTPUT);

  state->order = next_order;
  state->status.stream_bytes += (uint64_t)record_bytes;
  if (state->status.snapshot_count != UINT64_MAX)
    ++state->status.snapshot_count;
  state->status.last_snapshot_id = view->snapshot->snapshot_id;
  state->status.last_endpoint_hash = hashes->endpoint_hash;
  state->status.last_snapshot_hash = view->snapshot->snapshot_hash;
  state->last_projection_ref = projection_ref;
  state->status.state_flags |= WORR_NATIVE_DEMO_RECORDER_HAS_SNAPSHOT;
  state->status.last_result = WORR_NATIVE_DEMO_RECORDER_OK;
  state->status.next_ordinal =
      record.ordinal == UINT64_MAX ? 0 : record.ordinal + 1;
  return WORR_NATIVE_DEMO_RECORDER_OK;
}

worr_native_demo_recorder_result_v1 Worr_NativeDemoRecorderStopV1(
    worr_native_demo_recorder_state_v1 *state) {
  if (!state_initialized(state))
    return WORR_NATIVE_DEMO_RECORDER_INVALID_ARGUMENT;
  if ((state->status.state_flags & WORR_NATIVE_DEMO_RECORDER_ACTIVE) == 0)
    return WORR_NATIVE_DEMO_RECORDER_NOT_ACTIVE;
  state->status.state_flags &=
      (uint16_t)~WORR_NATIVE_DEMO_RECORDER_ACTIVE;
  state->status.state_flags |= WORR_NATIVE_DEMO_RECORDER_STOPPED;
  state->status.last_result = WORR_NATIVE_DEMO_RECORDER_OK;
  return WORR_NATIVE_DEMO_RECORDER_OK;
}

bool Worr_NativeDemoRecorderGetStatusV1(
    const worr_native_demo_recorder_state_v1 *state,
    worr_native_demo_recorder_status_v1 *status_out) {
  if (!state_initialized(state) || status_out == NULL ||
      ranges_overlap(state, sizeof(*state), status_out,
                     sizeof(*status_out))) {
    return false;
  }
  *status_out = state->status;
  return true;
}
