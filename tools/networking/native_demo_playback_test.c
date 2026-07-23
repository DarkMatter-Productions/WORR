/* Deterministic FR-10-T13 WDM1 snapshot playback cursor tests. */

#include "common/net/native_demo_playback.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TEST_MAX_ENTITIES 64u
#define TEST_TRANSPORT_EPOCH 55u
#define TEST_TIMELINE_EPOCH 77u
#define TEST_RECORD_COUNT 8u
#define TEST_CODEC_BYTES 4096u
#define TEST_STREAM_BYTES 32768u

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "native_demo_playback_test:%d: %s\n", __LINE__,         \
              #condition);                                                     \
      return false;                                                            \
    }                                                                          \
  } while (0)

typedef struct stream_fixture_s {
  uint8_t bytes[TEST_STREAM_BYTES];
  size_t byte_count;
  worr_native_demo_scan_info_v1 scan;
  worr_native_demo_index_entry_v1 entries[TEST_RECORD_COUNT];
} stream_fixture;

typedef struct decode_outputs_s {
  worr_snapshot_v2 snapshot;
  worr_snapshot_player_v2 player;
  worr_snapshot_entity_v2 entities[2];
  uint8_t area[8];
  worr_snapshot_event_ref_v2 events[2];
  worr_native_demo_playback_frame_v1 frame;
} decode_outputs;

static void store_u64(uint8_t *bytes, uint64_t value) {
  unsigned index;

  for (index = 0; index < 8; ++index)
    bytes[index] = (uint8_t)(value >> (index * 8u));
}

static void store_u32(uint8_t *bytes, uint32_t value) {
  unsigned index;

  for (index = 0; index < 4; ++index)
    bytes[index] = (uint8_t)(value >> (index * 8u));
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes,
                             size_t count) {
  size_t index;

  for (index = 0; index < count; ++index) {
    unsigned bit;

    crc ^= bytes[index];
    for (bit = 0; bit < 8; ++bit) {
      const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
    }
  }
  return crc;
}

static uint32_t frame_crc(const uint8_t *frame, size_t frame_bytes) {
  static const uint8_t zero[4] = {0, 0, 0, 0};
  uint32_t crc = UINT32_MAX;

  crc = crc32_update(crc, frame, 20);
  crc = crc32_update(crc, zero, sizeof(zero));
  crc = crc32_update(crc, frame + 24, frame_bytes - 24);
  return ~crc;
}

static void refresh_frame_crc(uint8_t *frame, size_t frame_bytes) {
  store_u32(frame + 20, 0);
  store_u32(frame + 20, frame_crc(frame, frame_bytes));
}

static worr_snapshot_entity_generation_v2 generation(uint32_t index,
                                                      uint32_t value) {
  worr_snapshot_entity_generation_v2 result;

  memset(&result, 0, sizeof(result));
  result.identity.index = index;
  result.identity.generation = value;
  result.provenance_flags = WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
  return result;
}

static worr_snapshot_player_v2 player(void) {
  worr_snapshot_player_v2 result;
  uint32_t index;

  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  result.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  result.controlled_entity = generation(1, 4);
  result.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
  result.movement.struct_size = sizeof(result.movement);
  result.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
  result.movement.origin[0] = 10.0f;
  result.movement.gravity = 800;
  result.movement.view_height = 22;
  result.view_angles[1] = 90.0f;
  result.fov = 100.0f;
  for (index = 0; index < WORR_SNAPSHOT_STATS_CAPACITY; ++index)
    result.stats[index] = (int16_t)index;
  return result;
}

static worr_snapshot_entity_v2 entity(uint32_t sequence) {
  worr_snapshot_entity_v2 result;

  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  result.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  result.generation = generation(1, 4);
  result.component_mask = WORR_SNAPSHOT_ENTITY_TRANSFORM;
  result.origin[0] = (float)sequence;
  result.owner.index = WORR_EVENT_NO_ENTITY;
  return result;
}

typedef struct snapshot_storage_s {
  worr_snapshot_store_v2 store;
  worr_snapshot_store_slot_v2 slots[1];
  worr_snapshot_entity_v2 entities[1];
  uint8_t area[2];
  worr_snapshot_event_ref_v2 events[1];
} snapshot_storage;

static bool encode_snapshot(uint32_t epoch, uint32_t sequence,
                            uint64_t server_time_us, bool empty_ranges,
                            uint8_t *encoded, size_t encoded_capacity,
                            size_t *encoded_bytes_out) {
  snapshot_storage storage;
  worr_snapshot_v2 snapshot;
  worr_snapshot_player_v2 snapshot_player = player();
  worr_snapshot_entity_v2 snapshot_entity = entity(sequence);
  const uint8_t area[2] = {(uint8_t)sequence, (uint8_t)(sequence + 1u)};
  worr_snapshot_event_ref_v2 event_ref;
  worr_snapshot_store_publish_v2 publication;
  worr_snapshot_projection_view_v2 view;
  worr_snapshot_ref_v2 ref;

  memset(&storage, 0, sizeof(storage));
  CHECK(Worr_SnapshotStoreInitV2(
            &storage.store, storage.slots, 1, storage.entities, 1, 1,
            storage.area, 2, 2, storage.events, 1, 1,
            TEST_MAX_ENTITIES) == WORR_SNAPSHOT_STORE_OK);
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.struct_size = sizeof(snapshot);
  snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  snapshot.flags = WORR_SNAPSHOT_FLAG_KEYFRAME |
                   WORR_SNAPSHOT_FLAG_COMPLETE |
                   WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS |
                   WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
  snapshot.snapshot_id.epoch = epoch;
  snapshot.snapshot_id.sequence = sequence;
  snapshot.server_tick = 100u + sequence;
  snapshot.server_time_us = server_time_us;
  snapshot.controlled_entity = generation(1, 4);
  snapshot.discontinuity.flags = WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
  if (sequence == 1) {
    snapshot.discontinuity.flags |= WORR_SNAPSHOT_DISCONTINUITY_INITIAL;
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
  } else {
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_FULL_SNAPSHOT;
    snapshot.discontinuity.previous.epoch = epoch;
    snapshot.discontinuity.previous.sequence = sequence - 1u;
    snapshot.discontinuity.server_tick_delta = 1;
  }
  memset(&event_ref, 0, sizeof(event_ref));
  event_ref.struct_size = sizeof(event_ref);
  event_ref.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  event_ref.provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
  event_ref.carrier_ordinal = 0;
  event_ref.semantic_version = WORR_EVENT_MODEL_REVISION;
  event_ref.authority_id.stream_epoch = 9;
  event_ref.authority_id.sequence = sequence;
  event_ref.semantic_hash = UINT64_C(0x1122334400000000) + sequence;
  memset(&publication, 0, sizeof(publication));
  publication.struct_size = sizeof(publication);
  publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
  publication.snapshot = &snapshot;
  publication.player = &snapshot_player;
  if (!empty_ranges) {
    publication.entities = &snapshot_entity;
    publication.area_bytes = area;
    publication.event_refs = &event_ref;
    publication.entity_count = 1;
    publication.area_byte_count = 2;
    publication.event_ref_count = 1;
  }
  CHECK(Worr_SnapshotStorePublishV2(&storage.store, &publication, &ref) ==
        WORR_SNAPSHOT_STORE_OK);
  memset(&view, 0, sizeof(view));
  view.struct_size = sizeof(view);
  view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
  view.snapshot = &storage.slots[ref.slot].snapshot;
  view.player = &storage.slots[ref.slot].player;
  view.entities = empty_ranges ? NULL : storage.entities;
  view.area_bytes = empty_ranges ? NULL : storage.area;
  view.event_refs = empty_ranges ? NULL : storage.events;
  view.entity_count = view.snapshot->entity_range.count;
  view.area_byte_count = view.snapshot->area_range.count;
  view.event_ref_count = view.snapshot->event_range.count;
  return Worr_NativeCodecSnapshotEncodeV1(
             &view, TEST_MAX_ENTITIES, encoded, encoded_capacity,
             encoded_bytes_out) == WORR_NATIVE_CODEC_OK;
}

static bool encode_command(uint32_t sequence, uint8_t *encoded,
                           size_t capacity, size_t *encoded_bytes_out) {
  worr_command_record_v1 command;

  memset(&command, 0, sizeof(command));
  command.struct_size = sizeof(command);
  command.schema_version = WORR_COMMAND_ABI_VERSION;
  command.command_id.epoch = 5;
  command.command_id.sequence = sequence;
  command.sample_time_us = 500u + sequence;
  command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  command.command.struct_size = sizeof(command.command);
  command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  command.command.duration_ms = 16;
  command.render_watermark.struct_size = sizeof(command.render_watermark);
  command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  command.render_watermark.provenance =
      WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  command.render_watermark.source_server_tick = 1;
  command.render_watermark.tick_interval_us = 1000;
  command.render_watermark.source_server_time_us = 500;
  command.render_watermark.rendered_server_time_us = 500;
  return Worr_NativeCodecCommandEncodeV1(
             &command, 250, encoded, capacity, encoded_bytes_out) ==
         WORR_NATIVE_CODEC_OK;
}

static bool encode_event(uint32_t sequence, uint8_t *encoded,
                         size_t capacity, size_t *encoded_bytes_out) {
  worr_event_record_v1 event;

  memset(&event, 0, sizeof(event));
  event.struct_size = sizeof(event);
  event.schema_version = WORR_EVENT_ABI_VERSION;
  event.model_revision = WORR_EVENT_MODEL_REVISION;
  event.flags = WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
                WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE;
  event.event_id.stream_epoch = 12;
  event.event_id.sequence = sequence;
  event.source_tick = 50u + sequence;
  event.source_ordinal = sequence;
  event.source_time_us = 1000u + sequence;
  event.source_entity.index = 1;
  event.source_entity.generation = 4;
  event.subject_entity.index = WORR_EVENT_NO_ENTITY;
  event.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
  event.payload_kind = WORR_EVENT_PAYLOAD_NONE;
  event.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
  event.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
  return Worr_NativeCodecEventEncodeV1(
             &event, TEST_MAX_ENTITIES, encoded, capacity,
             encoded_bytes_out) == WORR_NATIVE_CODEC_OK;
}

static bool append_record(stream_fixture *fixture, uint8_t kind,
                          uint64_t ordinal, uint64_t time_us,
                          const uint8_t *payload, size_t payload_bytes) {
  worr_native_demo_record_v1 record;
  size_t frame_bytes;

  memset(&record, 0, sizeof(record));
  record.struct_size = sizeof(record);
  record.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  record.record_kind = kind;
  record.ordinal = ordinal;
  record.time_us = time_us;
  CHECK(Worr_NativeDemoRecordEncodeV1(
            &record, payload, payload_bytes,
            fixture->bytes + fixture->byte_count,
            sizeof(fixture->bytes) - fixture->byte_count, &frame_bytes) ==
        WORR_NATIVE_DEMO_OK);
  fixture->byte_count += frame_bytes;
  return true;
}

static worr_native_demo_scan_config_v1 scan_config(uint64_t max_records) {
  worr_native_demo_scan_config_v1 result;

  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  result.stream_offset = UINT64_C(0x100000000);
  result.max_records = max_records;
  result.max_entities = TEST_MAX_ENTITIES;
  return result;
}

static bool rescan(stream_fixture *fixture, uint64_t max_records) {
  const worr_native_demo_scan_config_v1 config = scan_config(max_records);

  return Worr_NativeDemoStreamScanV1(
             &config, fixture->bytes, fixture->byte_count,
             max_records == 0 ? NULL : fixture->entries,
             (size_t)max_records, &fixture->scan) == WORR_NATIVE_DEMO_OK;
}

static bool build_stream(stream_fixture *fixture, bool empty_ranges,
                         uint32_t snapshot_epoch,
                         uint64_t first_snapshot_record_time) {
  worr_native_demo_container_config_v1 config;
  uint8_t snapshot1[TEST_CODEC_BYTES];
  uint8_t snapshot2[TEST_CODEC_BYTES];
  uint8_t snapshot3[TEST_CODEC_BYTES];
  uint8_t command1[TEST_CODEC_BYTES];
  uint8_t command2[TEST_CODEC_BYTES];
  uint8_t command3[TEST_CODEC_BYTES];
  uint8_t event1[TEST_CODEC_BYTES];
  uint8_t event2[TEST_CODEC_BYTES];
  size_t snapshot1_bytes;
  size_t snapshot2_bytes;
  size_t snapshot3_bytes;
  size_t command1_bytes;
  size_t command2_bytes;
  size_t command3_bytes;
  size_t event1_bytes;
  size_t event2_bytes;

  memset(fixture, 0, sizeof(*fixture));
  memset(&config, 0, sizeof(config));
  config.struct_size = sizeof(config);
  config.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  config.transport = WORR_NATIVE_DEMO_TRANSPORT_WNC1_V1;
  config.capability_mask = WORR_NET_CAP_CANONICAL_SNAPSHOT_V2 |
                           WORR_NET_CAP_TYPED_EVENT_RANGE_V2 |
                           WORR_NET_CAP_NATIVE_ENVELOPE_V1;
  config.transport_epoch = TEST_TRANSPORT_EPOCH;
  config.timeline_epoch = TEST_TIMELINE_EPOCH;
  config.first_record_ordinal = 10;
  config.created_time_us = 1234;
  CHECK(Worr_NativeDemoContainerEncodeV1(
            &config, fixture->bytes, sizeof(fixture->bytes),
            &fixture->byte_count) == WORR_NATIVE_DEMO_OK);
  CHECK(encode_snapshot(snapshot_epoch, 1, 1000, empty_ranges, snapshot1,
                        sizeof(snapshot1), &snapshot1_bytes));
  CHECK(encode_snapshot(snapshot_epoch, 2, 2000, empty_ranges, snapshot2,
                        sizeof(snapshot2), &snapshot2_bytes));
  CHECK(encode_snapshot(snapshot_epoch, 3, 3000, empty_ranges, snapshot3,
                        sizeof(snapshot3), &snapshot3_bytes));
  CHECK(encode_command(1, command1, sizeof(command1), &command1_bytes));
  CHECK(encode_command(2, command2, sizeof(command2), &command2_bytes));
  CHECK(encode_command(3, command3, sizeof(command3), &command3_bytes));
  CHECK(encode_event(1, event1, sizeof(event1), &event1_bytes));
  CHECK(encode_event(2, event2, sizeof(event2), &event2_bytes));
  CHECK(append_record(fixture, WORR_NATIVE_DEMO_RECORD_COMMAND_V1, 10, 500,
                      command1, command1_bytes));
  CHECK(append_record(fixture, WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1, 11,
                      first_snapshot_record_time, snapshot1,
                      snapshot1_bytes));
  CHECK(append_record(fixture, WORR_NATIVE_DEMO_RECORD_COMMAND_V1, 12, 1100,
                      command2, command2_bytes));
  CHECK(append_record(fixture, WORR_NATIVE_DEMO_RECORD_EVENT_V1, 13, 1200,
                      event1, event1_bytes));
  CHECK(append_record(fixture, WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1, 14, 2000,
                      snapshot2, snapshot2_bytes));
  CHECK(append_record(fixture, WORR_NATIVE_DEMO_RECORD_EVENT_V1, 15, 2100,
                      event2, event2_bytes));
  CHECK(append_record(fixture, WORR_NATIVE_DEMO_RECORD_COMMAND_V1, 16, 2200,
                      command3, command3_bytes));
  CHECK(append_record(fixture, WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1, 17, 3000,
                      snapshot3, snapshot3_bytes));
  return rescan(fixture, TEST_RECORD_COUNT);
}

static bool build_header_only(stream_fixture *fixture) {
  worr_native_demo_container_config_v1 config;

  memset(fixture, 0, sizeof(*fixture));
  memset(&config, 0, sizeof(config));
  config.struct_size = sizeof(config);
  config.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  config.transport = WORR_NATIVE_DEMO_TRANSPORT_WNC1_V1;
  config.capability_mask = WORR_NET_CAP_CANONICAL_SNAPSHOT_V2;
  config.transport_epoch = TEST_TRANSPORT_EPOCH;
  config.timeline_epoch = TEST_TIMELINE_EPOCH;
  config.first_record_ordinal = 10;
  CHECK(Worr_NativeDemoContainerEncodeV1(
            &config, fixture->bytes, sizeof(fixture->bytes),
            &fixture->byte_count) == WORR_NATIVE_DEMO_OK);
  return rescan(fixture, 0);
}

static worr_native_demo_playback_config_v1 playback_config(
    uint64_t max_records) {
  worr_native_demo_playback_config_v1 result;

  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.schema_version = WORR_NATIVE_DEMO_PLAYBACK_ABI_VERSION;
  result.max_entities = TEST_MAX_ENTITIES;
  result.expected_transport_epoch = TEST_TRANSPORT_EPOCH;
  result.expected_timeline_epoch = TEST_TIMELINE_EPOCH;
  result.max_records = max_records;
  return result;
}

static worr_native_demo_seek_query_v1 seek_time(uint64_t time_us) {
  worr_native_demo_seek_query_v1 result;

  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  result.flags = WORR_NATIVE_DEMO_SEEK_BY_TIME;
  result.target_time_us = time_us;
  return result;
}

static worr_native_demo_seek_query_v1 seek_ordinal(uint64_t ordinal) {
  worr_native_demo_seek_query_v1 result;

  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  result.flags = WORR_NATIVE_DEMO_SEEK_BY_ORDINAL;
  result.target_ordinal = ordinal;
  return result;
}

static worr_native_demo_playback_result_v1 position_first(
    worr_native_demo_playback_cursor_v1 *cursor,
    const stream_fixture *fixture, decode_outputs *outputs) {
  return Worr_NativeDemoPlaybackPositionFirstV1(
      cursor, fixture->bytes, fixture->byte_count, fixture->entries,
      TEST_RECORD_COUNT, &outputs->snapshot, &outputs->player,
      outputs->entities, 2, outputs->area, sizeof(outputs->area),
      outputs->events, 2, &outputs->frame);
}

static worr_native_demo_playback_result_v1 step(
    worr_native_demo_playback_cursor_v1 *cursor,
    const stream_fixture *fixture, decode_outputs *outputs) {
  return Worr_NativeDemoPlaybackStepV1(
      cursor, fixture->bytes, fixture->byte_count, fixture->entries,
      TEST_RECORD_COUNT, &outputs->snapshot, &outputs->player,
      outputs->entities, 2, outputs->area, sizeof(outputs->area),
      outputs->events, 2, &outputs->frame);
}

static bool test_position_seek_step(void) {
  stream_fixture fixture;
  worr_native_demo_playback_config_v1 config =
      playback_config(TEST_RECORD_COUNT);
  worr_native_demo_playback_cursor_v1 cursor;
  worr_native_demo_playback_cursor_v1 cursor_before;
  worr_native_demo_seek_query_v1 query;
  decode_outputs outputs;
  decode_outputs outputs_before;

  CHECK(build_stream(&fixture, false, TEST_TIMELINE_EPOCH, 1000));
  memset(&cursor, 0xcc, sizeof(cursor));
  CHECK(Worr_NativeDemoPlaybackInitV1(
            &config, &fixture.scan, fixture.bytes, fixture.byte_count,
            fixture.entries, TEST_RECORD_COUNT, &cursor) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(cursor.state_flags == WORR_NATIVE_DEMO_PLAYBACK_CURSOR_INITIALIZED);
  CHECK(cursor.reset_generation == 0 && cursor.next_entry_index == 0);

  memset(&outputs, 0x31, sizeof(outputs));
  outputs_before = outputs;
  cursor_before = cursor;
  query = seek_time(999);
  CHECK(Worr_NativeDemoPlaybackSeekV1(
            &cursor, fixture.bytes, fixture.byte_count, fixture.entries,
            TEST_RECORD_COUNT, &query, &outputs.snapshot, &outputs.player,
            outputs.entities, 2, outputs.area, sizeof(outputs.area),
            outputs.events, 2, &outputs.frame) ==
        WORR_NATIVE_DEMO_PLAYBACK_NOT_FOUND);
  CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);

  memset(&outputs, 0xcc, sizeof(outputs));
  CHECK(position_first(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(outputs.snapshot.snapshot_id.epoch == TEST_TIMELINE_EPOCH &&
        outputs.snapshot.snapshot_id.sequence == 1);
  CHECK(outputs.snapshot.server_time_us == 1000);
  CHECK(outputs.frame.entry_index == 1 && outputs.frame.entry.ordinal == 11);
  CHECK(outputs.frame.flags ==
        (WORR_NATIVE_DEMO_PLAYBACK_FRAME_INITIAL_POSITION |
         WORR_NATIVE_DEMO_PLAYBACK_FRAME_RESET_REQUIRED));
  CHECK(outputs.frame.reset_generation == 1 &&
        outputs.frame.entity_count == 1 && outputs.frame.area_byte_count == 2 &&
        outputs.frame.event_ref_count == 1);
  CHECK(cursor.current_entry_index == 1 && cursor.next_entry_index == 2 &&
        cursor.reset_generation == 1);

  /* An explicit seek may re-emit the same snapshot only with a new reset. */
  query = seek_time(1000);
  CHECK(Worr_NativeDemoPlaybackSeekV1(
            &cursor, fixture.bytes, fixture.byte_count, fixture.entries,
            TEST_RECORD_COUNT, &query, &outputs.snapshot, &outputs.player,
            outputs.entities, 2, outputs.area, sizeof(outputs.area),
            outputs.events, 2, &outputs.frame) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(outputs.snapshot.snapshot_id.sequence == 1);
  CHECK(outputs.frame.flags ==
        (WORR_NATIVE_DEMO_PLAYBACK_FRAME_SEEK_POSITION |
         WORR_NATIVE_DEMO_PLAYBACK_FRAME_RESET_REQUIRED));
  CHECK(outputs.frame.reset_generation == 2 &&
        cursor.reset_generation == 2 && cursor.next_entry_index == 2);

  /* Step starts at entry 2, skips command+event, and emits snapshot 2 once. */
  CHECK(step(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(outputs.snapshot.snapshot_id.sequence == 2);
  CHECK(outputs.frame.entry_index == 4 && outputs.frame.entry.ordinal == 14);
  CHECK(outputs.frame.flags ==
        WORR_NATIVE_DEMO_PLAYBACK_FRAME_FORWARD_STEP);
  CHECK(outputs.frame.reset_generation == 2 &&
        cursor.next_entry_index == 5);
  CHECK(step(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(outputs.snapshot.snapshot_id.sequence == 3 &&
        outputs.frame.entry_index == 7 && cursor.next_entry_index == 8);

  query = seek_ordinal(14);
  CHECK(Worr_NativeDemoPlaybackSeekV1(
            &cursor, fixture.bytes, fixture.byte_count, fixture.entries,
            TEST_RECORD_COUNT, &query, &outputs.snapshot, &outputs.player,
            outputs.entities, 2, outputs.area, sizeof(outputs.area),
            outputs.events, 2, &outputs.frame) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(outputs.snapshot.snapshot_id.sequence == 2 &&
        outputs.frame.entry.ordinal == 14 &&
        outputs.frame.reset_generation == 3);
  CHECK(step(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(outputs.snapshot.snapshot_id.sequence == 3 &&
        outputs.frame.entry_index == 7 && cursor.next_entry_index == 8);

  cursor_before = cursor;
  memset(&outputs, 0x5a, sizeof(outputs));
  outputs_before = outputs;
  CHECK(step(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_NOT_FOUND);
  CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);

  /* A rewind is a new reset generation; its next Step advances only once. */
  query = seek_time(1000);
  CHECK(Worr_NativeDemoPlaybackSeekV1(
            &cursor, fixture.bytes, fixture.byte_count, fixture.entries,
            TEST_RECORD_COUNT, &query, &outputs.snapshot, &outputs.player,
            outputs.entities, 2, outputs.area, sizeof(outputs.area),
            outputs.events, 2, &outputs.frame) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(outputs.snapshot.snapshot_id.sequence == 1 &&
        outputs.frame.reset_generation == 4);
  CHECK(step(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(outputs.snapshot.snapshot_id.sequence == 2 &&
        outputs.frame.entry_index == 4);

  cursor_before = cursor;
  memset(&outputs, 0x6b, sizeof(outputs));
  outputs_before = outputs;
  CHECK(position_first(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_INVALID_STATE);
  CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);
  return true;
}

static bool test_header_only_and_empty_ranges(void) {
  stream_fixture fixture;
  worr_native_demo_playback_config_v1 config = playback_config(0);
  worr_native_demo_playback_cursor_v1 cursor;
  worr_native_demo_playback_cursor_v1 before;
  worr_native_demo_seek_query_v1 query = seek_time(UINT64_MAX);
  decode_outputs outputs;
  decode_outputs outputs_before;

  CHECK(build_header_only(&fixture));
  CHECK(Worr_NativeDemoPlaybackInitV1(
            &config, &fixture.scan, fixture.bytes, fixture.byte_count, NULL, 0,
            &cursor) == WORR_NATIVE_DEMO_PLAYBACK_OK);
  before = cursor;
  memset(&outputs, 0x3c, sizeof(outputs));
  outputs_before = outputs;
  CHECK(Worr_NativeDemoPlaybackPositionFirstV1(
            &cursor, fixture.bytes, fixture.byte_count, NULL, 0,
            &outputs.snapshot, &outputs.player, outputs.entities, 2,
            outputs.area, sizeof(outputs.area), outputs.events, 2,
            &outputs.frame) == WORR_NATIVE_DEMO_PLAYBACK_NOT_FOUND);
  CHECK(memcmp(&cursor, &before, sizeof(cursor)) == 0);
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);
  CHECK(Worr_NativeDemoPlaybackSeekV1(
            &cursor, fixture.bytes, fixture.byte_count, NULL, 0, &query,
            &outputs.snapshot, &outputs.player, outputs.entities, 2,
            outputs.area, sizeof(outputs.area), outputs.events, 2,
            &outputs.frame) == WORR_NATIVE_DEMO_PLAYBACK_NOT_FOUND);
  CHECK(Worr_NativeDemoPlaybackStepV1(
            &cursor, fixture.bytes, fixture.byte_count, NULL, 0,
            &outputs.snapshot, &outputs.player, outputs.entities, 2,
            outputs.area, sizeof(outputs.area), outputs.events, 2,
            &outputs.frame) == WORR_NATIVE_DEMO_PLAYBACK_NOT_FOUND);
  CHECK(memcmp(&cursor, &before, sizeof(cursor)) == 0);
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);

  /* Zero decoded ranges ignore even maximal unused capacities safely. */
  config = playback_config(TEST_RECORD_COUNT);
  CHECK(build_stream(&fixture, true, TEST_TIMELINE_EPOCH, 1000));
  CHECK(Worr_NativeDemoPlaybackInitV1(
            &config, &fixture.scan, fixture.bytes, fixture.byte_count,
            fixture.entries, TEST_RECORD_COUNT, &cursor) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  memset(&outputs, 0xcc, sizeof(outputs));
  CHECK(Worr_NativeDemoPlaybackPositionFirstV1(
            &cursor, fixture.bytes, fixture.byte_count, fixture.entries,
            TEST_RECORD_COUNT, &outputs.snapshot, &outputs.player, NULL,
            UINT32_MAX, NULL, UINT32_MAX, NULL, UINT32_MAX, &outputs.frame) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(outputs.frame.entity_count == 0 &&
        outputs.frame.area_byte_count == 0 &&
        outputs.frame.event_ref_count == 0);
  return true;
}

static bool test_mutation_and_transactionality(void) {
  stream_fixture fixture;
  worr_native_demo_playback_config_v1 config =
      playback_config(TEST_RECORD_COUNT);
  worr_native_demo_playback_cursor_v1 cursor;
  worr_native_demo_playback_cursor_v1 cursor_before;
  decode_outputs outputs;
  decode_outputs outputs_before;
  size_t index;

  CHECK(build_stream(&fixture, false, TEST_TIMELINE_EPOCH, 1000));
  CHECK(Worr_NativeDemoPlaybackInitV1(
            &config, &fixture.scan, fixture.bytes, fixture.byte_count,
            fixture.entries, TEST_RECORD_COUNT, &cursor) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  memset(&outputs, 0x4d, sizeof(outputs));
  outputs_before = outputs;
  cursor_before = cursor;

  /* Every stream byte and every canonical index byte is bound immutably. */
  for (index = 0; index < fixture.byte_count; ++index) {
    fixture.bytes[index] ^= 1;
    CHECK(position_first(&cursor, &fixture, &outputs) ==
          WORR_NATIVE_DEMO_PLAYBACK_MUTATED);
    CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
    CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);
    fixture.bytes[index] ^= 1;
  }
  for (index = 0; index < sizeof(fixture.entries); ++index) {
    ((uint8_t *)fixture.entries)[index] ^= 1;
    CHECK(position_first(&cursor, &fixture, &outputs) ==
          WORR_NATIVE_DEMO_PLAYBACK_MUTATED);
    CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
    CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);
    ((uint8_t *)fixture.entries)[index] ^= 1;
  }

  cursor.scan.index_crc32 ^= 1;
  CHECK(position_first(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_MUTATED);
  cursor = cursor_before;
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);
  ++cursor.scan_config.max_entities;
  CHECK(position_first(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_MUTATED);
  cursor = cursor_before;
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);

  /* Capacity and every written region are fail-closed and transactional. */
  CHECK(Worr_NativeDemoPlaybackPositionFirstV1(
            &cursor, fixture.bytes, fixture.byte_count, fixture.entries,
            TEST_RECORD_COUNT, &outputs.snapshot, &outputs.player,
            outputs.entities, 0, outputs.area, sizeof(outputs.area),
            outputs.events, 2, &outputs.frame) ==
        WORR_NATIVE_DEMO_PLAYBACK_CAPACITY);
  CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);
  CHECK(Worr_NativeDemoPlaybackPositionFirstV1(
            &cursor, fixture.bytes, fixture.byte_count, fixture.entries,
            TEST_RECORD_COUNT, (worr_snapshot_v2 *)fixture.bytes,
            &outputs.player, outputs.entities, 2, outputs.area,
            sizeof(outputs.area), outputs.events, 2, &outputs.frame) ==
        WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT);
  CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
  CHECK(Worr_NativeDemoPlaybackPositionFirstV1(
            &cursor, fixture.bytes, fixture.byte_count, fixture.entries,
            TEST_RECORD_COUNT, &outputs.snapshot,
            (worr_snapshot_player_v2 *)&outputs.snapshot, outputs.entities, 2,
            outputs.area, sizeof(outputs.area), outputs.events, 2,
            &outputs.frame) == WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT);
  CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);
  CHECK(Worr_NativeDemoPlaybackPositionFirstV1(
            &cursor, fixture.bytes, fixture.byte_count, fixture.entries,
            TEST_RECORD_COUNT, &outputs.snapshot, &outputs.player,
            outputs.entities, 2, (uint8_t *)outputs.entities,
            sizeof(outputs.area), outputs.events, 2, &outputs.frame) ==
        WORR_NATIVE_DEMO_PLAYBACK_INVALID_ARGUMENT);
  CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);
  return true;
}

static bool test_epoch_gap_time_and_overflow(void) {
  stream_fixture fixture;
  stream_fixture changed;
  worr_native_demo_playback_config_v1 config =
      playback_config(TEST_RECORD_COUNT);
  worr_native_demo_playback_cursor_v1 cursor;
  worr_native_demo_playback_cursor_v1 cursor_sentinel;
  worr_native_demo_playback_cursor_v1 cursor_before;
  worr_native_demo_seek_query_v1 query = seek_time(1000);
  decode_outputs outputs;
  decode_outputs outputs_before;
  size_t index;

  memset(&cursor_sentinel, 0x73, sizeof(cursor_sentinel));
  CHECK(build_stream(&fixture, false, TEST_TIMELINE_EPOCH + 1u, 1000));
  cursor = cursor_sentinel;
  CHECK(Worr_NativeDemoPlaybackInitV1(
            &config, &fixture.scan, fixture.bytes, fixture.byte_count,
            fixture.entries, TEST_RECORD_COUNT, &cursor) ==
        WORR_NATIVE_DEMO_PLAYBACK_WRONG_EPOCH);
  CHECK(memcmp(&cursor, &cursor_sentinel, sizeof(cursor)) == 0);

  /* A repaired WDR CRC cannot disguise a WDR/WNC1 class mismatch. */
  CHECK(build_stream(&fixture, false, TEST_TIMELINE_EPOCH, 1000));
  changed = fixture;
  {
    const uint64_t relative = changed.entries[1].record_offset -
                              changed.scan.stream_offset;
    uint8_t *frame = changed.bytes + (size_t)relative;

    frame[8] = WORR_NATIVE_DEMO_RECORD_COMMAND_V1;
    refresh_frame_crc(frame, changed.entries[1].frame_bytes);
  }
  cursor = cursor_sentinel;
  CHECK(Worr_NativeDemoPlaybackInitV1(
            &config, &fixture.scan, changed.bytes, changed.byte_count,
            fixture.entries, TEST_RECORD_COUNT, &cursor) ==
        WORR_NATIVE_DEMO_PLAYBACK_MALFORMED);
  CHECK(memcmp(&cursor, &cursor_sentinel, sizeof(cursor)) == 0);

  /* A valid WDR/index whose time disagrees with its canonical body is refused
   * before any decode output is written. */
  CHECK(build_stream(&fixture, false, TEST_TIMELINE_EPOCH, 1001));
  CHECK(Worr_NativeDemoPlaybackInitV1(
            &config, &fixture.scan, fixture.bytes, fixture.byte_count,
            fixture.entries, TEST_RECORD_COUNT, &cursor) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  cursor_before = cursor;
  memset(&outputs, 0x42, sizeof(outputs));
  outputs_before = outputs;
  CHECK(position_first(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_INVALID_METADATA);
  CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);

  /* Re-encoded outer CRCs and a fresh scan do not make ordinal gaps legal. */
  CHECK(build_stream(&changed, false, TEST_TIMELINE_EPOCH, 1000));
  for (index = 3; index < TEST_RECORD_COUNT; ++index) {
    const uint64_t relative =
        changed.entries[index].record_offset - changed.scan.stream_offset;
    uint8_t *frame = changed.bytes + (size_t)relative;

    store_u64(frame + 24, changed.entries[index].ordinal + 1u);
    refresh_frame_crc(frame, changed.entries[index].frame_bytes);
  }
  CHECK(rescan(&changed, TEST_RECORD_COUNT));
  cursor = cursor_sentinel;
  CHECK(Worr_NativeDemoPlaybackInitV1(
            &config, &changed.scan, changed.bytes, changed.byte_count,
            changed.entries, TEST_RECORD_COUNT, &cursor) ==
        WORR_NATIVE_DEMO_PLAYBACK_GAP);
  CHECK(memcmp(&cursor, &cursor_sentinel, sizeof(cursor)) == 0);

  /* Reset generation exhaustion cannot wrap or partially decode. */
  CHECK(build_stream(&fixture, false, TEST_TIMELINE_EPOCH, 1000));
  CHECK(Worr_NativeDemoPlaybackInitV1(
            &config, &fixture.scan, fixture.bytes, fixture.byte_count,
            fixture.entries, TEST_RECORD_COUNT, &cursor) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(position_first(&cursor, &fixture, &outputs) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  cursor.reset_generation = UINT32_MAX;
  cursor_before = cursor;
  memset(&outputs, 0x29, sizeof(outputs));
  outputs_before = outputs;
  CHECK(Worr_NativeDemoPlaybackSeekV1(
            &cursor, fixture.bytes, fixture.byte_count, fixture.entries,
            TEST_RECORD_COUNT, &query, &outputs.snapshot, &outputs.player,
            outputs.entities, 2, outputs.area, sizeof(outputs.area),
            outputs.events, 2, &outputs.frame) ==
        WORR_NATIVE_DEMO_PLAYBACK_LIMIT);
  CHECK(memcmp(&cursor, &cursor_before, sizeof(cursor)) == 0);
  CHECK(memcmp(&outputs, &outputs_before, sizeof(outputs)) == 0);

  /* A forged overflowing index fails Init without touching its destination. */
  changed = fixture;
  changed.entries[7].record_offset = UINT64_MAX - 4u;
  changed.entries[7].next_record_offset = UINT64_MAX;
  cursor = cursor_sentinel;
  CHECK(Worr_NativeDemoPlaybackInitV1(
            &config, &changed.scan, changed.bytes, changed.byte_count,
            changed.entries, TEST_RECORD_COUNT, &cursor) !=
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(memcmp(&cursor, &cursor_sentinel, sizeof(cursor)) == 0);
  return true;
}

int main(void) {
  if (!test_position_seek_step() ||
      !test_header_only_and_empty_ranges() ||
      !test_mutation_and_transactionality() ||
      !test_epoch_gap_time_and_overflow()) {
    return 1;
  }
  puts("native_demo_playback_test: ok");
  return 0;
}
