/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_demo_playback.h"

#include <limits.h>
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

static bool counted_bytes_u32(uint32_t count, size_t item_bytes,
                              size_t *bytes_out) {
  if (bytes_out == NULL || item_bytes == 0 ||
      (uint64_t)count > (uint64_t)SIZE_MAX / (uint64_t)item_bytes) {
    return false;
  }
  *bytes_out = (size_t)count * item_bytes;
  return true;
}

static worr_native_demo_playback_result_v1
demo_result(worr_native_demo_result_v1 result) {
  switch (result) {
  case WORR_NATIVE_DEMO_OK:
    return WORR_NATIVE_DEMO_PLAYBACK_OK;
  case WORR_NATIVE_DEMO_INVALID_ARGUMENT:
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
  case WORR_NATIVE_DEMO_INVALID_METADATA:
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_METADATA;
  case WORR_NATIVE_DEMO_OUTPUT_TOO_SMALL:
    return WORR_NATIVE_DEMO_PLAYBACK_CAPACITY;
  case WORR_NATIVE_DEMO_TRUNCATED:
    return WORR_NATIVE_DEMO_PLAYBACK_TRUNCATED;
  case WORR_NATIVE_DEMO_MALFORMED:
    return WORR_NATIVE_DEMO_PLAYBACK_MALFORMED;
  case WORR_NATIVE_DEMO_UNSUPPORTED:
    return WORR_NATIVE_DEMO_PLAYBACK_UNSUPPORTED;
  case WORR_NATIVE_DEMO_CORRUPT:
    return WORR_NATIVE_DEMO_PLAYBACK_CORRUPT;
  case WORR_NATIVE_DEMO_LIMIT:
    return WORR_NATIVE_DEMO_PLAYBACK_LIMIT;
  case WORR_NATIVE_DEMO_ORDER:
    return WORR_NATIVE_DEMO_PLAYBACK_ORDER;
  case WORR_NATIVE_DEMO_INVALID_STATE:
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_STATE;
  case WORR_NATIVE_DEMO_NOT_FOUND:
    return WORR_NATIVE_DEMO_PLAYBACK_NOT_FOUND;
  }
  return WORR_NATIVE_DEMO_PLAYBACK_INVALID_STATE;
}

static worr_native_demo_playback_result_v1
codec_result(worr_native_codec_result_v1 result) {
  switch (result) {
  case WORR_NATIVE_CODEC_OK:
    return WORR_NATIVE_DEMO_PLAYBACK_OK;
  case WORR_NATIVE_CODEC_INVALID_ARGUMENT:
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
  case WORR_NATIVE_CODEC_INVALID_RECORD:
    return WORR_NATIVE_DEMO_PLAYBACK_MALFORMED;
  case WORR_NATIVE_CODEC_OUTPUT_TOO_SMALL:
  case WORR_NATIVE_CODEC_CAPACITY:
    return WORR_NATIVE_DEMO_PLAYBACK_CAPACITY;
  case WORR_NATIVE_CODEC_MALFORMED:
    return WORR_NATIVE_DEMO_PLAYBACK_MALFORMED;
  case WORR_NATIVE_CODEC_UNSUPPORTED:
    return WORR_NATIVE_DEMO_PLAYBACK_UNSUPPORTED;
  case WORR_NATIVE_CODEC_LIMIT:
    return WORR_NATIVE_DEMO_PLAYBACK_LIMIT;
  case WORR_NATIVE_CODEC_CORRUPT:
    return WORR_NATIVE_DEMO_PLAYBACK_CORRUPT;
  }
  return WORR_NATIVE_DEMO_PLAYBACK_INVALID_STATE;
}

static uint64_t fingerprint_byte(uint64_t hash, uint8_t value) {
  return (hash ^ value) * UINT64_C(1099511628211);
}

static uint64_t fingerprint_u16(uint64_t hash, uint16_t value) {
  hash = fingerprint_byte(hash, (uint8_t)value);
  return fingerprint_byte(hash, (uint8_t)(value >> 8));
}

static uint64_t fingerprint_u32(uint64_t hash, uint32_t value) {
  unsigned index;

  for (index = 0; index < 4; ++index)
    hash = fingerprint_byte(hash, (uint8_t)(value >> (index * 8u)));
  return hash;
}

static uint64_t fingerprint_u64(uint64_t hash, uint64_t value) {
  unsigned index;

  for (index = 0; index < 8; ++index)
    hash = fingerprint_byte(hash, (uint8_t)(value >> (index * 8u)));
  return hash;
}

static uint64_t stream_fingerprint(
    const worr_native_demo_scan_config_v1 *config, const void *encoded,
    size_t encoded_bytes) {
  const uint8_t *bytes = (const uint8_t *)encoded;
  uint64_t hash = UINT64_C(14695981039346656037);
  size_t index;

  hash = fingerprint_u32(hash, UINT32_C(0x31534457)); /* WDS1 */
  hash = fingerprint_u64(hash, config->stream_offset);
  hash = fingerprint_u64(hash, config->max_records);
  hash = fingerprint_u32(hash, config->max_entities);
  hash = fingerprint_u64(hash, (uint64_t)encoded_bytes);
  for (index = 0; index < encoded_bytes; ++index)
    hash = fingerprint_byte(hash, bytes[index]);
  return hash;
}

static uint64_t index_fingerprint(
    const worr_native_demo_index_entry_v1 *entries, size_t entry_count) {
  uint64_t hash = UINT64_C(14695981039346656037);
  size_t index;

  hash = fingerprint_u32(hash, UINT32_C(0x31494457)); /* WDI1 */
  hash = fingerprint_u64(hash, (uint64_t)entry_count);
  for (index = 0; index < entry_count; ++index) {
    const worr_native_demo_index_entry_v1 *entry = &entries[index];

    hash = fingerprint_u32(hash, entry->struct_size);
    hash = fingerprint_u16(hash, entry->schema_version);
    hash = fingerprint_byte(hash, entry->record_kind);
    hash = fingerprint_byte(hash, entry->flags);
    hash = fingerprint_u64(hash, entry->ordinal);
    hash = fingerprint_u64(hash, entry->time_us);
    hash = fingerprint_u64(hash, entry->record_offset);
    hash = fingerprint_u64(hash, entry->next_record_offset);
    hash = fingerprint_u32(hash, entry->frame_bytes);
    hash = fingerprint_u32(hash, entry->frame_crc32);
    hash = fingerprint_byte(hash, entry->record.record_class);
    hash = fingerprint_byte(hash, entry->record.reserved0);
    hash = fingerprint_u16(hash, entry->record.record_schema_version);
    hash = fingerprint_u32(hash, entry->record.object_epoch);
    hash = fingerprint_u32(hash, entry->record.object_sequence);
    hash = fingerprint_u32(hash, entry->reserved0);
  }
  return hash;
}

static bool container_equal(
    const worr_native_demo_container_info_v1 *left,
    const worr_native_demo_container_info_v1 *right) {
  return left->struct_size == right->struct_size &&
         left->schema_version == right->schema_version &&
         left->transport == right->transport &&
         left->codec_wire_version == right->codec_wire_version &&
         left->capability_version == right->capability_version &&
         left->capability_mask == right->capability_mask &&
         left->transport_epoch == right->transport_epoch &&
         left->timeline_epoch == right->timeline_epoch &&
         left->first_record_ordinal == right->first_record_ordinal &&
         left->created_time_us == right->created_time_us &&
         left->header_crc32 == right->header_crc32 &&
         left->wire_header_bytes == right->wire_header_bytes &&
         left->record_wire_header_bytes == right->record_wire_header_bytes &&
         left->reserved0 == right->reserved0;
}

static bool order_equal(const worr_native_demo_order_state_v1 *left,
                        const worr_native_demo_order_state_v1 *right) {
  return left->struct_size == right->struct_size &&
         left->schema_version == right->schema_version &&
         left->state_flags == right->state_flags &&
         left->first_record_ordinal == right->first_record_ordinal &&
         left->last_ordinal == right->last_ordinal &&
         left->last_time_us == right->last_time_us &&
         left->command_epoch == right->command_epoch &&
         left->command_sequence == right->command_sequence &&
         left->snapshot_epoch == right->snapshot_epoch &&
         left->snapshot_sequence == right->snapshot_sequence &&
         left->event_epoch == right->event_epoch &&
         left->event_sequence == right->event_sequence &&
         left->seen_class_mask == right->seen_class_mask &&
         left->same_time_phase == right->same_time_phase &&
         left->reserved0[0] == right->reserved0[0] &&
         left->reserved0[1] == right->reserved0[1] &&
         left->reserved0[2] == right->reserved0[2] &&
         left->reserved1 == right->reserved1;
}

static bool scan_equal(const worr_native_demo_scan_info_v1 *left,
                       const worr_native_demo_scan_info_v1 *right) {
  return left->struct_size == right->struct_size &&
         left->schema_version == right->schema_version &&
         left->flags == right->flags &&
         left->stream_offset == right->stream_offset &&
         left->stream_bytes == right->stream_bytes &&
         left->next_stream_offset == right->next_stream_offset &&
         left->record_count == right->record_count &&
         left->snapshot_count == right->snapshot_count &&
         container_equal(&left->container, &right->container) &&
         order_equal(&left->order, &right->order) &&
         left->index_crc32 == right->index_crc32 &&
         left->reserved0 == right->reserved0;
}

static bool config_valid(
    const worr_native_demo_playback_config_v1 *config) {
  return config != NULL && config->struct_size == sizeof(*config) &&
         config->schema_version == WORR_NATIVE_DEMO_PLAYBACK_ABI_VERSION &&
         config->flags == 0 && config->max_entities != 0 &&
         config->expected_transport_epoch != 0 &&
         config->expected_timeline_epoch != 0 && config->reserved0 == 0;
}

static bool cursor_shape_valid(
    const worr_native_demo_playback_cursor_v1 *cursor) {
  const uint16_t known_flags =
      WORR_NATIVE_DEMO_PLAYBACK_CURSOR_INITIALIZED |
      WORR_NATIVE_DEMO_PLAYBACK_CURSOR_POSITIONED;
  bool positioned;

  if (cursor == NULL)
    return false;
  positioned =
      (cursor->state_flags & WORR_NATIVE_DEMO_PLAYBACK_CURSOR_POSITIONED) != 0;
  if (cursor->struct_size != sizeof(*cursor) ||
      cursor->schema_version != WORR_NATIVE_DEMO_PLAYBACK_ABI_VERSION ||
      cursor->state_flags == 0 ||
      (cursor->state_flags & ~known_flags) != 0 ||
      (cursor->state_flags & WORR_NATIVE_DEMO_PLAYBACK_CURSOR_INITIALIZED) ==
          0 ||
      cursor->reserved0 != 0 || cursor->reserved1 != 0 ||
      cursor->scan_config.struct_size != sizeof(cursor->scan_config) ||
      cursor->scan_config.schema_version != WORR_NATIVE_DEMO_ABI_VERSION ||
      cursor->scan_config.flags != 0 ||
      cursor->scan_config.stream_offset != cursor->scan.stream_offset ||
      cursor->scan_config.max_entities == 0 ||
      cursor->scan_config.reserved0 != 0 ||
      cursor->scan.record_count > cursor->scan_config.max_records) {
    return false;
  }
  if (!positioned) {
    return cursor->reset_generation == 0 &&
           cursor->current_entry_index == 0 &&
           cursor->next_entry_index == 0 && cursor->last_ordinal == 0 &&
           cursor->last_time_us == 0 && cursor->last_snapshot_id.epoch == 0 &&
           cursor->last_snapshot_id.sequence == 0;
  }
  return cursor->reset_generation != 0 &&
         cursor->current_entry_index < cursor->scan.record_count &&
         cursor->current_entry_index != UINT64_MAX &&
         cursor->next_entry_index == cursor->current_entry_index + 1u &&
         cursor->next_entry_index <= cursor->scan.record_count &&
         cursor->last_ordinal != 0 &&
         Worr_SnapshotIdValidV2(cursor->last_snapshot_id, false);
}

static worr_native_demo_playback_result_v1 validate_index(
    const worr_native_demo_scan_info_v1 *scan,
    const worr_native_demo_index_entry_v1 *entries, size_t entry_count) {
  worr_native_demo_seek_query_v1 query;
  worr_native_demo_seek_result_v1 result;
  worr_native_demo_result_v1 nested;
  uint64_t expected_ordinal;
  uint64_t snapshot_count = 0;
  size_t index;

  memset(&query, 0, sizeof(query));
  query.struct_size = sizeof(query);
  query.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  query.flags = WORR_NATIVE_DEMO_SEEK_BY_ORDINAL;
  query.target_ordinal = UINT64_MAX;
  nested = Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
      scan, entries, entry_count, &query, &result);
  if (nested != WORR_NATIVE_DEMO_OK &&
      nested != WORR_NATIVE_DEMO_NOT_FOUND) {
    return demo_result(nested);
  }
  if ((scan->snapshot_count == 0) !=
      (nested == WORR_NATIVE_DEMO_NOT_FOUND)) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_METADATA;
  }

  expected_ordinal = scan->container.first_record_ordinal;
  for (index = 0; index < entry_count; ++index) {
    const worr_native_demo_index_entry_v1 *entry = &entries[index];

    if (entry->ordinal != expected_ordinal)
      return WORR_NATIVE_DEMO_PLAYBACK_GAP;
    if (entry->record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1) {
      ++snapshot_count;
      if (entry->record.object_epoch != scan->container.timeline_epoch)
        return WORR_NATIVE_DEMO_PLAYBACK_WRONG_EPOCH;
    }
    if (index + 1u < entry_count) {
      if (expected_ordinal == UINT64_MAX)
        return WORR_NATIVE_DEMO_PLAYBACK_LIMIT;
      ++expected_ordinal;
    }
  }
  if (snapshot_count != scan->snapshot_count)
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_METADATA;
  return WORR_NATIVE_DEMO_PLAYBACK_OK;
}

static worr_native_demo_playback_result_v1 revalidate_binding(
    const worr_native_demo_playback_cursor_v1 *cursor, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count) {
  worr_native_demo_scan_info_v1 scan;
  worr_native_demo_result_v1 nested;
  worr_native_demo_playback_result_v1 result;
  uint64_t encoded_bytes_u64;
  uint64_t entry_count_u64;

  if (!cursor_shape_valid(cursor) ||
      (encoded_bytes != 0 && encoded == NULL) ||
      (entry_count != 0 && entries == NULL)) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
  }
  encoded_bytes_u64 = (uint64_t)encoded_bytes;
  if ((size_t)encoded_bytes_u64 != encoded_bytes ||
      encoded_bytes_u64 != cursor->scan.stream_bytes) {
    return WORR_NATIVE_DEMO_PLAYBACK_MUTATED;
  }
  if (ranges_overlap(cursor, sizeof(*cursor), encoded, encoded_bytes))
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
  if (stream_fingerprint(&cursor->scan_config, encoded, encoded_bytes) !=
      cursor->stream_fingerprint) {
    return WORR_NATIVE_DEMO_PLAYBACK_MUTATED;
  }
  nested = Worr_NativeDemoStreamScanV1(&cursor->scan_config, encoded,
                                       encoded_bytes, NULL, 0, &scan);
  if (nested != WORR_NATIVE_DEMO_OK || !scan_equal(&scan, &cursor->scan))
    return WORR_NATIVE_DEMO_PLAYBACK_MUTATED;

  entry_count_u64 = (uint64_t)entry_count;
  if ((size_t)entry_count_u64 != entry_count ||
      entry_count_u64 != scan.record_count ||
      entry_count > SIZE_MAX / sizeof(*entries)) {
    return WORR_NATIVE_DEMO_PLAYBACK_MUTATED;
  }
  if (ranges_overlap(encoded, encoded_bytes, entries,
                     entry_count * sizeof(*entries)) ||
      ranges_overlap(cursor, sizeof(*cursor), entries,
                     entry_count * sizeof(*entries))) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
  }
  if (index_fingerprint(entries, entry_count) != cursor->index_fingerprint)
    return WORR_NATIVE_DEMO_PLAYBACK_MUTATED;
  result = validate_index(&scan, entries, entry_count);
  if (result != WORR_NATIVE_DEMO_PLAYBACK_OK)
    return WORR_NATIVE_DEMO_PLAYBACK_MUTATED;

  if ((cursor->state_flags &
       WORR_NATIVE_DEMO_PLAYBACK_CURSOR_POSITIONED) != 0) {
    const worr_native_demo_index_entry_v1 *current =
        &entries[(size_t)cursor->current_entry_index];

    if (current->record_kind != WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1 ||
        current->ordinal != cursor->last_ordinal ||
        current->time_us != cursor->last_time_us ||
        current->record.object_epoch != cursor->last_snapshot_id.epoch ||
        current->record.object_sequence != cursor->last_snapshot_id.sequence) {
      return WORR_NATIVE_DEMO_PLAYBACK_MUTATED;
    }
  }
  return WORR_NATIVE_DEMO_PLAYBACK_OK;
}

static bool codec_info_equal(const worr_native_codec_info_v1 *left,
                             const worr_native_codec_info_v1 *right) {
  return left->struct_size == right->struct_size &&
         left->schema_version == right->schema_version &&
         left->record_class == right->record_class &&
         left->flags == right->flags &&
         left->record_schema_version == right->record_schema_version &&
         left->reserved0 == right->reserved0 &&
         left->model_revision == right->model_revision &&
         left->encoded_bytes == right->encoded_bytes &&
         left->fixed_body_bytes == right->fixed_body_bytes &&
         left->range_counts[0] == right->range_counts[0] &&
         left->range_counts[1] == right->range_counts[1] &&
         left->range_counts[2] == right->range_counts[2] &&
         left->object_epoch == right->object_epoch &&
         left->object_sequence == right->object_sequence &&
         left->reserved1 == right->reserved1;
}

static bool record_matches_entry(
    const worr_native_demo_record_info_v1 *record,
    const worr_native_demo_index_entry_v1 *entry) {
  worr_native_record_ref_v1 ref;

  return Worr_NativeCodecInfoRecordRefV1(&record->codec, &ref) &&
         record->record_kind == entry->record_kind &&
         record->flags == entry->flags &&
         record->ordinal == entry->ordinal &&
         record->time_us == entry->time_us &&
         record->record_offset == entry->record_offset &&
         record->next_record_offset == entry->next_record_offset &&
         record->frame_bytes == entry->frame_bytes &&
         record->frame_crc32 == entry->frame_crc32 &&
         ref.record_class == entry->record.record_class &&
         ref.reserved0 == entry->record.reserved0 &&
         ref.record_schema_version == entry->record.record_schema_version &&
         ref.object_epoch == entry->record.object_epoch &&
         ref.object_sequence == entry->record.object_sequence;
}

static worr_native_demo_playback_result_v1 validate_output_regions(
    worr_native_demo_playback_cursor_v1 *cursor, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, const void *extra_input, size_t extra_input_bytes,
    worr_snapshot_v2 *snapshot_out, worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out, uint32_t entity_capacity,
    uint32_t entity_count, uint8_t *area_bytes_out, uint32_t area_capacity,
    uint32_t area_count, worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity, uint32_t event_ref_count,
    worr_native_demo_playback_frame_v1 *frame_out) {
  const void *inputs[3];
  size_t input_sizes[3];
  void *outputs[6];
  size_t output_sizes[6];
  size_t entity_bytes;
  size_t event_bytes;
  size_t entry_bytes;
  size_t left;
  size_t right;

  if (snapshot_out == NULL || player_out == NULL || frame_out == NULL ||
      (entity_count != 0 && entities_out == NULL) ||
      (area_count != 0 && area_bytes_out == NULL) ||
      (event_ref_count != 0 && event_refs_out == NULL)) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
  }
  if (entity_count > entity_capacity || area_count > area_capacity ||
      event_ref_count > event_ref_capacity)
    return WORR_NATIVE_DEMO_PLAYBACK_CAPACITY;
  if ((entity_count != 0 &&
       !counted_bytes_u32(entity_capacity, sizeof(*entities_out),
                          &entity_bytes)) ||
      (event_ref_count != 0 &&
       !counted_bytes_u32(event_ref_capacity, sizeof(*event_refs_out),
                          &event_bytes)) ||
      entry_count > SIZE_MAX / sizeof(*entries)) {
    return WORR_NATIVE_DEMO_PLAYBACK_LIMIT;
  }
  if (entity_count == 0)
    entity_bytes = 0;
  if (event_ref_count == 0)
    event_bytes = 0;
  entry_bytes = entry_count * sizeof(*entries);

  inputs[0] = encoded;
  input_sizes[0] = encoded_bytes;
  inputs[1] = entries;
  input_sizes[1] = entry_bytes;
  inputs[2] = extra_input;
  input_sizes[2] = extra_input_bytes;
  outputs[0] = cursor;
  output_sizes[0] = sizeof(*cursor);
  outputs[1] = snapshot_out;
  output_sizes[1] = sizeof(*snapshot_out);
  outputs[2] = player_out;
  output_sizes[2] = sizeof(*player_out);
  outputs[3] = entities_out;
  output_sizes[3] = entity_bytes;
  outputs[4] = area_bytes_out;
  output_sizes[4] = area_count == 0 ? 0 : area_capacity;
  outputs[5] = event_refs_out;
  output_sizes[5] = event_bytes;

  for (left = 0; left < 6; ++left) {
    size_t input_index;

    if (ranges_overlap(outputs[left], output_sizes[left], frame_out,
                       sizeof(*frame_out))) {
      return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
    }
    for (input_index = 0; input_index < 3; ++input_index) {
      if (ranges_overlap(outputs[left], output_sizes[left],
                         inputs[input_index], input_sizes[input_index])) {
        return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
      }
    }
    for (right = left + 1; right < 6; ++right) {
      if (ranges_overlap(outputs[left], output_sizes[left], outputs[right],
                         output_sizes[right])) {
        return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
      }
    }
  }
  for (left = 0; left < 3; ++left) {
    if (ranges_overlap(inputs[left], input_sizes[left], frame_out,
                       sizeof(*frame_out))) {
      return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
    }
  }
  return WORR_NATIVE_DEMO_PLAYBACK_OK;
}

static worr_native_demo_playback_result_v1 decode_selected(
    worr_native_demo_playback_cursor_v1 *cursor, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, size_t selected_index, uint16_t frame_flags,
    uint32_t reset_generation, const void *extra_input,
    size_t extra_input_bytes, worr_snapshot_v2 *snapshot_out,
    worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out, uint32_t entity_capacity,
    uint8_t *area_bytes_out, uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_native_demo_playback_frame_v1 *frame_out) {
  const worr_native_demo_index_entry_v1 *entry;
  const uint8_t *bytes = (const uint8_t *)encoded;
  const uint8_t *payload;
  worr_native_demo_playback_cursor_v1 next;
  worr_native_demo_playback_frame_v1 frame;
  worr_native_demo_record_info_v1 record;
  worr_native_codec_snapshot_metadata_v1 metadata;
  worr_snapshot_projection_view_v2 view;
  worr_snapshot_projection_hashes_v2 decoded_hashes;
  worr_native_demo_result_v1 demo_nested;
  worr_native_codec_result_v1 codec_nested;
  worr_native_demo_playback_result_v1 result;
  uint64_t relative_u64;
  size_t relative;

  if (selected_index >= entry_count)
    return WORR_NATIVE_DEMO_PLAYBACK_GAP;
  entry = &entries[selected_index];
  if (entry->record_offset < cursor->scan.stream_offset)
    return WORR_NATIVE_DEMO_PLAYBACK_GAP;
  relative_u64 = entry->record_offset - cursor->scan.stream_offset;
  relative = (size_t)relative_u64;
  if ((uint64_t)relative != relative_u64 || relative > encoded_bytes ||
      entry->frame_bytes > encoded_bytes - relative) {
    return WORR_NATIVE_DEMO_PLAYBACK_LIMIT;
  }
  demo_nested = Worr_NativeDemoRecordDecodeV1(
      bytes + relative, entry->frame_bytes, entry->record_offset, &record);
  if (demo_nested != WORR_NATIVE_DEMO_OK)
    return demo_result(demo_nested);
  if (!record_matches_entry(&record, entry))
    return WORR_NATIVE_DEMO_PLAYBACK_MUTATED;
  if (record.record_kind != WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1 ||
      record.codec.record_class != WORR_NATIVE_RECORD_SNAPSHOT_V1)
    return WORR_NATIVE_DEMO_PLAYBACK_WRONG_CLASS;
  if (record.codec.object_epoch != cursor->scan.container.timeline_epoch)
    return WORR_NATIVE_DEMO_PLAYBACK_WRONG_EPOCH;

  payload = bytes + relative + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES;
  codec_nested = Worr_NativeCodecSnapshotMetadataV1(
      payload, record.payload_bytes, cursor->scan_config.max_entities,
      &metadata);
  if (codec_nested != WORR_NATIVE_CODEC_OK)
    return codec_result(codec_nested);
  if (!codec_info_equal(&record.codec, &metadata.codec))
    return WORR_NATIVE_DEMO_PLAYBACK_MUTATED;
  if (metadata.snapshot.snapshot_id.epoch !=
          cursor->scan.container.timeline_epoch ||
      metadata.snapshot.snapshot_id.epoch != entry->record.object_epoch)
    return WORR_NATIVE_DEMO_PLAYBACK_WRONG_EPOCH;
  if (metadata.snapshot.snapshot_id.sequence !=
          entry->record.object_sequence ||
      metadata.snapshot.server_time_us != entry->time_us) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_METADATA;
  }

  result = validate_output_regions(
      cursor, encoded, encoded_bytes, entries, entry_count, extra_input,
      extra_input_bytes, snapshot_out, player_out, entities_out,
      entity_capacity, metadata.codec.range_counts[0], area_bytes_out,
      area_capacity, metadata.codec.range_counts[1], event_refs_out,
      event_ref_capacity, metadata.codec.range_counts[2], frame_out);
  if (result != WORR_NATIVE_DEMO_PLAYBACK_OK)
    return result;

  codec_nested = Worr_NativeCodecSnapshotDecodeProjectionV1(
      payload, record.payload_bytes, cursor->scan_config.max_entities,
      snapshot_out, player_out,
      metadata.codec.range_counts[0] == 0 ? NULL : entities_out,
      metadata.codec.range_counts[0] == 0 ? 0 : entity_capacity,
      metadata.codec.range_counts[1] == 0 ? NULL : area_bytes_out,
      metadata.codec.range_counts[1] == 0 ? 0 : area_capacity,
      metadata.codec.range_counts[2] == 0 ? NULL : event_refs_out,
      metadata.codec.range_counts[2] == 0 ? 0 : event_ref_capacity, &view,
      &decoded_hashes);
  if (codec_nested != WORR_NATIVE_CODEC_OK)
    return codec_result(codec_nested);

  next = *cursor;
  next.state_flags |= WORR_NATIVE_DEMO_PLAYBACK_CURSOR_POSITIONED;
  next.reset_generation = reset_generation;
  next.current_entry_index = (uint64_t)selected_index;
  next.next_entry_index = (uint64_t)selected_index + 1u;
  next.last_ordinal = entry->ordinal;
  next.last_time_us = entry->time_us;
  next.last_snapshot_id = metadata.snapshot.snapshot_id;

  memset(&frame, 0, sizeof(frame));
  frame.struct_size = sizeof(frame);
  frame.schema_version = WORR_NATIVE_DEMO_PLAYBACK_ABI_VERSION;
  frame.flags = frame_flags;
  frame.reset_generation = reset_generation;
  frame.entity_count = metadata.codec.range_counts[0];
  frame.area_byte_count = metadata.codec.range_counts[1];
  frame.event_ref_count = metadata.codec.range_counts[2];
  frame.entry_index = (uint64_t)selected_index;
  frame.entry = *entry;
  frame.hashes = metadata.hashes;
  *cursor = next;
  *frame_out = frame;
  return WORR_NATIVE_DEMO_PLAYBACK_OK;
}

worr_native_demo_playback_result_v1 Worr_NativeDemoPlaybackInitV1(
    const worr_native_demo_playback_config_v1 *config,
    const worr_native_demo_scan_info_v1 *scan, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, worr_native_demo_playback_cursor_v1 *cursor_out) {
  worr_native_demo_playback_cursor_v1 cursor;
  worr_native_demo_scan_config_v1 scan_config;
  worr_native_demo_scan_info_v1 verified_scan;
  worr_native_demo_result_v1 nested;
  worr_native_demo_playback_result_v1 result;
  uint64_t encoded_bytes_u64;
  uint64_t entry_count_u64;
  size_t entry_bytes;

  if (config == NULL || scan == NULL || cursor_out == NULL ||
      (encoded_bytes != 0 && encoded == NULL) ||
      (entry_count != 0 && entries == NULL)) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
  }
  if (!config_valid(config))
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_CONFIG;
  encoded_bytes_u64 = (uint64_t)encoded_bytes;
  entry_count_u64 = (uint64_t)entry_count;
  if ((size_t)encoded_bytes_u64 != encoded_bytes ||
      (size_t)entry_count_u64 != entry_count) {
    return WORR_NATIVE_DEMO_PLAYBACK_LIMIT;
  }

  memset(&scan_config, 0, sizeof(scan_config));
  scan_config.struct_size = sizeof(scan_config);
  scan_config.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  scan_config.stream_offset = scan->stream_offset;
  scan_config.max_records = config->max_records;
  scan_config.max_entities = config->max_entities;
  nested = Worr_NativeDemoStreamScanV1(&scan_config, encoded, encoded_bytes,
                                       NULL, 0, &verified_scan);
  if (nested != WORR_NATIVE_DEMO_OK)
    return demo_result(nested);
  if (!scan_equal(scan, &verified_scan) ||
      verified_scan.container.transport_epoch !=
          config->expected_transport_epoch ||
      verified_scan.container.timeline_epoch !=
          config->expected_timeline_epoch) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_METADATA;
  }
  if (entry_count_u64 != verified_scan.record_count ||
      entry_count > SIZE_MAX / sizeof(*entries)) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_METADATA;
  }
  entry_bytes = entry_count * sizeof(*entries);
  if (ranges_overlap(encoded, encoded_bytes, entries, entry_bytes) ||
      ranges_overlap(cursor_out, sizeof(*cursor_out), config,
                     sizeof(*config)) ||
      ranges_overlap(cursor_out, sizeof(*cursor_out), scan, sizeof(*scan)) ||
      ranges_overlap(cursor_out, sizeof(*cursor_out), encoded,
                     encoded_bytes) ||
      ranges_overlap(cursor_out, sizeof(*cursor_out), entries, entry_bytes)) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
  }
  result = validate_index(&verified_scan, entries, entry_count);
  if (result != WORR_NATIVE_DEMO_PLAYBACK_OK)
    return result;

  memset(&cursor, 0, sizeof(cursor));
  cursor.struct_size = sizeof(cursor);
  cursor.schema_version = WORR_NATIVE_DEMO_PLAYBACK_ABI_VERSION;
  cursor.state_flags = WORR_NATIVE_DEMO_PLAYBACK_CURSOR_INITIALIZED;
  cursor.stream_fingerprint =
      stream_fingerprint(&scan_config, encoded, encoded_bytes);
  cursor.index_fingerprint = index_fingerprint(entries, entry_count);
  cursor.scan_config = scan_config;
  cursor.scan = verified_scan;
  *cursor_out = cursor;
  return WORR_NATIVE_DEMO_PLAYBACK_OK;
}

worr_native_demo_playback_result_v1 Worr_NativeDemoPlaybackPositionFirstV1(
    worr_native_demo_playback_cursor_v1 *cursor, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, worr_snapshot_v2 *snapshot_out,
    worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out, uint32_t entity_capacity,
    uint8_t *area_bytes_out, uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_native_demo_playback_frame_v1 *frame_out) {
  worr_native_demo_playback_result_v1 result;
  size_t index;

  result = revalidate_binding(cursor, encoded, encoded_bytes, entries,
                              entry_count);
  if (result != WORR_NATIVE_DEMO_PLAYBACK_OK)
    return result;
  if ((cursor->state_flags &
       WORR_NATIVE_DEMO_PLAYBACK_CURSOR_POSITIONED) != 0)
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_STATE;
  for (index = 0; index < entry_count; ++index) {
    if (entries[index].record_kind ==
        WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1) {
      return decode_selected(
          cursor, encoded, encoded_bytes, entries, entry_count, index,
          WORR_NATIVE_DEMO_PLAYBACK_FRAME_INITIAL_POSITION |
              WORR_NATIVE_DEMO_PLAYBACK_FRAME_RESET_REQUIRED,
          1, NULL, 0, snapshot_out, player_out, entities_out, entity_capacity,
          area_bytes_out, area_capacity, event_refs_out, event_ref_capacity,
          frame_out);
    }
  }
  return WORR_NATIVE_DEMO_PLAYBACK_NOT_FOUND;
}

worr_native_demo_playback_result_v1 Worr_NativeDemoPlaybackSeekV1(
    worr_native_demo_playback_cursor_v1 *cursor, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, const worr_native_demo_seek_query_v1 *query,
    worr_snapshot_v2 *snapshot_out, worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out, uint32_t entity_capacity,
    uint8_t *area_bytes_out, uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_native_demo_playback_frame_v1 *frame_out) {
  worr_native_demo_seek_result_v1 selected;
  worr_native_demo_result_v1 nested;
  worr_native_demo_playback_result_v1 result;
  uint32_t reset_generation;

  if (query == NULL || cursor == NULL ||
      ranges_overlap(query, sizeof(*query), cursor, sizeof(*cursor)) ||
      ranges_overlap(query, sizeof(*query), encoded, encoded_bytes)) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
  }
  result = revalidate_binding(cursor, encoded, encoded_bytes, entries,
                              entry_count);
  if (result != WORR_NATIVE_DEMO_PLAYBACK_OK)
    return result;
  if (entry_count > SIZE_MAX / sizeof(*entries) ||
      ranges_overlap(query, sizeof(*query), entries,
                     entry_count * sizeof(*entries))) {
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT;
  }
  nested = Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
      &cursor->scan, entries, entry_count, query, &selected);
  if (nested != WORR_NATIVE_DEMO_OK)
    return demo_result(nested);
  if (cursor->reset_generation == UINT32_MAX)
    return WORR_NATIVE_DEMO_PLAYBACK_LIMIT;
  reset_generation = cursor->reset_generation + 1u;
  return decode_selected(
      cursor, encoded, encoded_bytes, entries, entry_count,
      (size_t)selected.entry_index,
      WORR_NATIVE_DEMO_PLAYBACK_FRAME_SEEK_POSITION |
          WORR_NATIVE_DEMO_PLAYBACK_FRAME_RESET_REQUIRED,
      reset_generation, query, sizeof(*query), snapshot_out, player_out,
      entities_out, entity_capacity, area_bytes_out, area_capacity,
      event_refs_out, event_ref_capacity, frame_out);
}

worr_native_demo_playback_result_v1 Worr_NativeDemoPlaybackStepV1(
    worr_native_demo_playback_cursor_v1 *cursor, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, worr_snapshot_v2 *snapshot_out,
    worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out, uint32_t entity_capacity,
    uint8_t *area_bytes_out, uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_native_demo_playback_frame_v1 *frame_out) {
  worr_native_demo_playback_result_v1 result;
  size_t index;

  result = revalidate_binding(cursor, encoded, encoded_bytes, entries,
                              entry_count);
  if (result != WORR_NATIVE_DEMO_PLAYBACK_OK)
    return result;
  if (cursor->scan.snapshot_count == 0)
    return WORR_NATIVE_DEMO_PLAYBACK_NOT_FOUND;
  if ((cursor->state_flags &
       WORR_NATIVE_DEMO_PLAYBACK_CURSOR_POSITIONED) == 0)
    return WORR_NATIVE_DEMO_PLAYBACK_INVALID_STATE;
  index = (size_t)cursor->next_entry_index;
  while (index < entry_count) {
    if (entries[index].record_kind ==
        WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1) {
      if (entries[index].record.object_epoch ==
              cursor->last_snapshot_id.epoch &&
          entries[index].record.object_sequence <=
              cursor->last_snapshot_id.sequence) {
        return WORR_NATIVE_DEMO_PLAYBACK_ORDER;
      }
      return decode_selected(
          cursor, encoded, encoded_bytes, entries, entry_count, index,
          WORR_NATIVE_DEMO_PLAYBACK_FRAME_FORWARD_STEP,
          cursor->reset_generation, NULL, 0, snapshot_out, player_out,
          entities_out, entity_capacity, area_bytes_out, area_capacity,
          event_refs_out, event_ref_capacity, frame_out);
    }
    ++index;
  }
  return WORR_NATIVE_DEMO_PLAYBACK_NOT_FOUND;
}
