/* Deterministic FR-10-T13 native demo framing tests. */

#include "common/net/native_demo.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TEST_MAX_ENTITIES 64u
#define TEST_CODEC_BYTES 2048u
#define TEST_FRAME_BYTES 4096u
#define TEST_FILE_BYTES 16384u
#define TEST_INDEX_ENTRIES 5u

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "native_demo_test:%d: %s\n", __LINE__, #condition);      \
      return false;                                                            \
    }                                                                          \
  } while (0)

static uint16_t load_u16(const uint8_t *bytes) {
  return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t load_u32(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t load_u64(const uint8_t *bytes) {
  return (uint64_t)load_u32(bytes) | ((uint64_t)load_u32(bytes + 4) << 32);
}

static void store_u16(uint8_t *bytes, uint16_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
}

static void store_u32(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

static void store_u64(uint8_t *bytes, uint64_t value) {
  store_u32(bytes, (uint32_t)value);
  store_u32(bytes + 4, (uint32_t)(value >> 32));
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes, size_t count) {
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
  static const uint8_t zero_crc[4] = {0, 0, 0, 0};
  uint32_t crc = UINT32_MAX;

  crc = crc32_update(crc, frame, 20);
  crc = crc32_update(crc, zero_crc, sizeof(zero_crc));
  crc = crc32_update(crc, frame + 24, frame_bytes - 24);
  return ~crc;
}

static void refresh_frame_crc(uint8_t *frame, size_t frame_bytes) {
  store_u32(frame + 20, 0);
  store_u32(frame + 20, frame_crc(frame, frame_bytes));
}

static worr_native_demo_container_config_v1 make_config(void) {
  worr_native_demo_container_config_v1 config;

  memset(&config, 0, sizeof(config));
  config.struct_size = sizeof(config);
  config.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  config.transport = WORR_NATIVE_DEMO_TRANSPORT_WNC1_V1;
  config.capability_mask = WORR_NET_CAP_CANONICAL_SNAPSHOT_V2 |
                           WORR_NET_CAP_TYPED_EVENT_RANGE_V2 |
                           WORR_NET_CAP_NATIVE_ENVELOPE_V1;
  config.transport_epoch = UINT32_C(0x11223344);
  config.timeline_epoch = UINT32_C(0x55667788);
  config.first_record_ordinal = 100;
  config.created_time_us = UINT64_C(0x0102030405060708);
  return config;
}

static worr_command_record_v1 make_command(uint32_t sequence) {
  worr_command_record_v1 record;

  memset(&record, 0, sizeof(record));
  record.struct_size = sizeof(record);
  record.schema_version = WORR_COMMAND_ABI_VERSION;
  record.command_id.epoch = 5;
  record.command_id.sequence = sequence;
  record.sample_time_us = UINT64_C(5000000) + sequence;
  record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  record.command.struct_size = sizeof(record.command);
  record.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  record.command.duration_ms = 16;
  record.command.buttons = 3;
  record.command.view_angles[1] = 90.0f;
  record.command.forward_move = 100.0f;
  record.command.side_move = -50.0f;
  record.render_watermark.struct_size = sizeof(record.render_watermark);
  record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  record.render_watermark.provenance =
      WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  record.render_watermark.source_server_tick = 300;
  record.render_watermark.tick_interval_us = 16667;
  record.render_watermark.source_server_time_us = UINT64_C(5000000);
  record.render_watermark.rendered_server_time_us = UINT64_C(5000000);
  return record;
}

static bool encode_command(uint32_t sequence, uint8_t *encoded, size_t capacity,
                           size_t *encoded_bytes) {
  const worr_command_record_v1 record = make_command(sequence);

  return Worr_NativeCodecCommandEncodeV1(&record, 250, encoded, capacity,
                                         encoded_bytes) == WORR_NATIVE_CODEC_OK;
}

static worr_event_record_v1 make_event(uint32_t sequence) {
  worr_event_record_v1 record;

  memset(&record, 0, sizeof(record));
  record.struct_size = sizeof(record);
  record.schema_version = WORR_EVENT_ABI_VERSION;
  record.model_revision = WORR_EVENT_MODEL_REVISION;
  record.flags = WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
                 WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE;
  record.event_id.stream_epoch = 7;
  record.event_id.sequence = sequence;
  record.source_tick = 1000 + sequence;
  record.source_ordinal = sequence;
  record.source_time_us = UINT64_C(9000000) + sequence;
  record.source_entity.index = 1;
  record.source_entity.generation = 4;
  record.subject_entity.index = WORR_EVENT_NO_ENTITY;
  record.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
  record.payload_kind = WORR_EVENT_PAYLOAD_NONE;
  record.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
  record.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
  return record;
}

static bool encode_event(uint32_t sequence, uint8_t *encoded, size_t capacity,
                         size_t *encoded_bytes) {
  const worr_event_record_v1 record = make_event(sequence);

  return Worr_NativeCodecEventEncodeV1(&record, TEST_MAX_ENTITIES, encoded,
                                       capacity,
                                       encoded_bytes) == WORR_NATIVE_CODEC_OK;
}

static worr_snapshot_entity_generation_v2 make_generation(uint32_t index,
                                                          uint32_t value) {
  worr_snapshot_entity_generation_v2 generation;

  memset(&generation, 0, sizeof(generation));
  generation.identity.index = index;
  generation.identity.generation = value;
  generation.provenance_flags = WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
  return generation;
}

static worr_snapshot_player_v2 make_player(void) {
  worr_snapshot_player_v2 player;
  uint32_t index;

  memset(&player, 0, sizeof(player));
  player.struct_size = sizeof(player);
  player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  player.controlled_entity = make_generation(1, 4);
  player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
  player.movement.struct_size = sizeof(player.movement);
  player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
  player.movement.origin[0] = 10.0f;
  player.movement.gravity = 800;
  player.movement.view_height = 22;
  player.fov = 100.0f;
  for (index = 0; index < WORR_SNAPSHOT_STATS_CAPACITY; ++index)
    player.stats[index] = (int16_t)index;
  return player;
}

typedef struct snapshot_fixture_s {
  worr_snapshot_store_v2 store;
  worr_snapshot_store_slot_v2 slot[1];
  worr_snapshot_entity_v2 entities[1];
  uint8_t area[1];
  worr_snapshot_event_ref_v2 events[1];
} snapshot_fixture;

static bool encode_snapshot(uint32_t sequence, uint8_t *encoded,
                            size_t capacity, size_t *encoded_bytes) {
  snapshot_fixture fixture;
  worr_snapshot_v2 snapshot;
  worr_snapshot_player_v2 player = make_player();
  worr_snapshot_store_publish_v2 publication;
  worr_snapshot_projection_view_v2 view;
  worr_snapshot_ref_v2 ref;

  memset(&fixture, 0, sizeof(fixture));
  if (Worr_SnapshotStoreInitV2(&fixture.store, fixture.slot, 1,
                               fixture.entities, 1, 1, fixture.area, 1, 1,
                               fixture.events, 1, 1,
                               TEST_MAX_ENTITIES) != WORR_SNAPSHOT_STORE_OK) {
    return false;
  }
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.struct_size = sizeof(snapshot);
  snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  snapshot.flags = WORR_SNAPSHOT_FLAG_KEYFRAME | WORR_SNAPSHOT_FLAG_COMPLETE |
                   WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS |
                   WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
  snapshot.snapshot_id.epoch = 2 + sequence;
  snapshot.snapshot_id.sequence = 1;
  snapshot.server_tick = 100 + sequence;
  snapshot.server_time_us = UINT64_C(1666700) + sequence;
  snapshot.controlled_entity = make_generation(1, 4);
  snapshot.discontinuity.flags = WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
                                 WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
  snapshot.discontinuity.reason = WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
  memset(&publication, 0, sizeof(publication));
  publication.struct_size = sizeof(publication);
  publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
  publication.snapshot = &snapshot;
  publication.player = &player;
  if (Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) !=
      WORR_SNAPSHOT_STORE_OK) {
    return false;
  }
  memset(&view, 0, sizeof(view));
  view.struct_size = sizeof(view);
  view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
  view.snapshot = &fixture.slot[ref.slot].snapshot;
  view.player = &fixture.slot[ref.slot].player;
  view.entities = fixture.entities;
  view.area_bytes = fixture.area;
  view.event_refs = fixture.events;
  return Worr_NativeCodecSnapshotEncodeV1(&view, TEST_MAX_ENTITIES, encoded,
                                          capacity, encoded_bytes) ==
         WORR_NATIVE_CODEC_OK;
}

static worr_native_demo_record_v1 make_record(uint8_t kind, uint64_t ordinal,
                                              uint64_t time_us) {
  worr_native_demo_record_v1 record;

  memset(&record, 0, sizeof(record));
  record.struct_size = sizeof(record);
  record.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  record.record_kind = kind;
  record.ordinal = ordinal;
  record.time_us = time_us;
  return record;
}

static bool encode_frame(uint8_t kind, uint64_t ordinal, uint64_t time_us,
                         const uint8_t *payload, size_t payload_bytes,
                         uint8_t *frame, size_t capacity, size_t *frame_bytes) {
  const worr_native_demo_record_v1 record = make_record(kind, ordinal, time_us);

  return Worr_NativeDemoRecordEncodeV1(&record, payload, payload_bytes, frame,
                                       capacity,
                                       frame_bytes) == WORR_NATIVE_DEMO_OK;
}

static bool build_stream(uint8_t *file, size_t capacity,
                         size_t *file_bytes_out) {
  worr_native_demo_container_config_v1 config = make_config();
  uint8_t snapshot1[TEST_CODEC_BYTES];
  uint8_t snapshot2[TEST_CODEC_BYTES];
  uint8_t event1[TEST_CODEC_BYTES];
  uint8_t event2[TEST_CODEC_BYTES];
  uint8_t command[TEST_CODEC_BYTES];
  size_t snapshot1_bytes;
  size_t snapshot2_bytes;
  size_t event1_bytes;
  size_t event2_bytes;
  size_t command_bytes;
  size_t file_bytes;
  size_t frame_bytes;

  CHECK(file != NULL && file_bytes_out != NULL);
  CHECK(encode_snapshot(1, snapshot1, sizeof(snapshot1), &snapshot1_bytes));
  CHECK(encode_snapshot(2, snapshot2, sizeof(snapshot2), &snapshot2_bytes));
  CHECK(encode_event(1, event1, sizeof(event1), &event1_bytes));
  CHECK(encode_event(2, event2, sizeof(event2), &event2_bytes));
  CHECK(encode_command(1, command, sizeof(command), &command_bytes));
  CHECK(Worr_NativeDemoContainerEncodeV1(&config, file, capacity,
                                         &file_bytes) == WORR_NATIVE_DEMO_OK);
  CHECK(encode_frame(WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1, 100, 1000, snapshot1,
                     snapshot1_bytes, file + file_bytes, capacity - file_bytes,
                     &frame_bytes));
  file_bytes += frame_bytes;
  CHECK(encode_frame(WORR_NATIVE_DEMO_RECORD_EVENT_V1, 101, 1500, event1,
                     event1_bytes, file + file_bytes, capacity - file_bytes,
                     &frame_bytes));
  file_bytes += frame_bytes;
  CHECK(encode_frame(WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1, 102, 2000, snapshot2,
                     snapshot2_bytes, file + file_bytes, capacity - file_bytes,
                     &frame_bytes));
  file_bytes += frame_bytes;
  CHECK(encode_frame(WORR_NATIVE_DEMO_RECORD_COMMAND_V1, 103, 2000, command,
                     command_bytes, file + file_bytes, capacity - file_bytes,
                     &frame_bytes));
  file_bytes += frame_bytes;
  CHECK(encode_frame(WORR_NATIVE_DEMO_RECORD_EVENT_V1, 104, 2000, event2,
                     event2_bytes, file + file_bytes, capacity - file_bytes,
                     &frame_bytes));
  file_bytes += frame_bytes;
  *file_bytes_out = file_bytes;
  return true;
}

static bool test_container(void) {
  worr_native_demo_container_config_v1 config = make_config();
  worr_native_demo_container_info_v1 info;
  worr_native_demo_container_info_v1 sentinel;
  uint8_t encoded[80];
  uint8_t before[80];
  uint8_t malformed[80];
  size_t encoded_bytes = 0;
  size_t size_sentinel = SIZE_MAX;
  size_t cut;

  memset(encoded, 0xa5, sizeof(encoded));
  CHECK(Worr_NativeDemoContainerEncodeV1(&config, encoded, sizeof(encoded),
                                         &encoded_bytes) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(encoded_bytes == WORR_NATIVE_DEMO_WIRE_HEADER_BYTES);
  CHECK(memcmp(encoded, "WDM1", 4) == 0);
  CHECK(load_u16(encoded + 4) == WORR_NATIVE_DEMO_WIRE_VERSION);
  CHECK(load_u16(encoded + 6) == WORR_NATIVE_DEMO_WIRE_HEADER_BYTES);
  CHECK(load_u16(encoded + 8) == WORR_NATIVE_DEMO_TRANSPORT_WNC1_V1);
  CHECK(load_u16(encoded + 10) == WORR_NATIVE_CODEC_WIRE_VERSION);
  CHECK(load_u32(encoded + 20) == config.capability_mask);
  CHECK(load_u32(encoded + 24) == config.transport_epoch);
  CHECK(load_u32(encoded + 28) == config.timeline_epoch);
  CHECK(load_u64(encoded + 32) == config.first_record_ordinal);
  CHECK(load_u64(encoded + 40) == config.created_time_us);
  CHECK(load_u16(encoded + 52) == WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES);
  CHECK(load_u32(encoded + 48) == UINT32_C(0x6f78869d)); /* golden */

  memset(&info, 0xcc, sizeof(info));
  CHECK(Worr_NativeDemoContainerDecodeV1(encoded, sizeof(encoded), &info) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(info.capability_mask == config.capability_mask);
  CHECK(info.transport_epoch == config.transport_epoch);
  CHECK(info.timeline_epoch == config.timeline_epoch);
  CHECK(info.first_record_ordinal == config.first_record_ordinal);
  CHECK(info.created_time_us == config.created_time_us);
  CHECK(info.header_crc32 == load_u32(encoded + 48));

  memset(&sentinel, 0x5a, sizeof(sentinel));
  for (cut = 0; cut < WORR_NATIVE_DEMO_WIRE_HEADER_BYTES; ++cut) {
    info = sentinel;
    CHECK(Worr_NativeDemoContainerDecodeV1(encoded, cut, &info) ==
          WORR_NATIVE_DEMO_TRUNCATED);
    CHECK(memcmp(&info, &sentinel, sizeof(info)) == 0);
  }
  memcpy(malformed, encoded, sizeof(encoded));
  malformed[0] = 'X';
  info = sentinel;
  CHECK(Worr_NativeDemoContainerDecodeV1(malformed, sizeof(malformed), &info) ==
        WORR_NATIVE_DEMO_MALFORMED);
  CHECK(memcmp(&info, &sentinel, sizeof(info)) == 0);
  memcpy(malformed, encoded, sizeof(encoded));
  store_u16(malformed + 4, 2);
  CHECK(Worr_NativeDemoContainerDecodeV1(malformed, sizeof(malformed), &info) ==
        WORR_NATIVE_DEMO_UNSUPPORTED);
  memcpy(malformed, encoded, sizeof(encoded));
  malformed[40] ^= 1;
  CHECK(Worr_NativeDemoContainerDecodeV1(malformed, sizeof(malformed), &info) ==
        WORR_NATIVE_DEMO_CORRUPT);
  memcpy(malformed, encoded, sizeof(encoded));
  malformed[54] = 1;
  CHECK(Worr_NativeDemoContainerDecodeV1(malformed, sizeof(malformed), &info) ==
        WORR_NATIVE_DEMO_MALFORMED);
  memcpy(malformed, encoded, sizeof(encoded));
  store_u32(malformed + 20, UINT32_C(0x80000000));
  CHECK(Worr_NativeDemoContainerDecodeV1(malformed, sizeof(malformed), &info) ==
        WORR_NATIVE_DEMO_UNSUPPORTED);

  memset(encoded, 0xa5, sizeof(encoded));
  memcpy(before, encoded, sizeof(encoded));
  CHECK(Worr_NativeDemoContainerEncodeV1(
            &config, encoded, WORR_NATIVE_DEMO_WIRE_HEADER_BYTES - 1u,
            &size_sentinel) == WORR_NATIVE_DEMO_OUTPUT_TOO_SMALL);
  CHECK(size_sentinel == SIZE_MAX);
  CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);
  config.reserved0 = 1;
  CHECK(Worr_NativeDemoContainerEncodeV1(&config, encoded, sizeof(encoded),
                                         &size_sentinel) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(size_sentinel == SIZE_MAX);
  CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);
  return true;
}

static bool test_record_codec(void) {
  worr_native_demo_record_v1 record;
  worr_native_demo_record_info_v1 info;
  worr_native_demo_record_info_v1 sentinel;
  worr_native_demo_index_entry_v1 entry;
  worr_native_demo_index_entry_v1 entry_sentinel;
  uint8_t payload[TEST_CODEC_BYTES];
  uint8_t frame[TEST_FRAME_BYTES];
  uint8_t malformed[TEST_FRAME_BYTES];
  uint8_t before[TEST_FRAME_BYTES];
  size_t payload_bytes;
  size_t encoded_bytes = 0;
  size_t size_sentinel = SIZE_MAX;
  size_t cut;

  CHECK(encode_command(9, payload, sizeof(payload), &payload_bytes));
  record = make_record(WORR_NATIVE_DEMO_RECORD_COMMAND_V1, 100, 1000);
  memset(frame, 0xa5, sizeof(frame));
  CHECK(Worr_NativeDemoRecordEncodeV1(&record, payload, payload_bytes, frame,
                                      sizeof(frame),
                                      &encoded_bytes) == WORR_NATIVE_DEMO_OK);
  CHECK(encoded_bytes ==
        WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES + payload_bytes);
  CHECK(memcmp(frame, "WDR1", 4) == 0);
  CHECK(load_u16(frame + 4) == WORR_NATIVE_DEMO_RECORD_WIRE_VERSION);
  CHECK(load_u16(frame + 6) == WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES);
  CHECK(frame[8] == WORR_NATIVE_DEMO_RECORD_COMMAND_V1);
  CHECK(load_u32(frame + 12) == encoded_bytes);
  CHECK(load_u32(frame + 16) == payload_bytes);
  CHECK(load_u64(frame + 24) == record.ordinal);
  CHECK(load_u64(frame + 32) == record.time_us);
  CHECK(load_u32(frame + 20) == UINT32_C(0x126f4c5c)); /* golden */

  memset(frame + encoded_bytes, 0x77, 16);
  memset(&info, 0xcc, sizeof(info));
  CHECK(Worr_NativeDemoRecordDecodeV1(frame, encoded_bytes + 16, 64, &info) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(info.frame_bytes == encoded_bytes);
  CHECK(info.payload_bytes == payload_bytes);
  CHECK(info.ordinal == 100 && info.time_us == 1000);
  CHECK(info.record_offset == 64);
  CHECK(info.payload_offset == 64 + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES);
  CHECK(info.next_record_offset == 64 + encoded_bytes);
  CHECK(info.codec.record_class == WORR_NATIVE_RECORD_COMMAND_V1);
  CHECK(info.codec.object_epoch == 5 && info.codec.object_sequence == 9);

  memset(&entry, 0xcc, sizeof(entry));
  CHECK(Worr_NativeDemoIndexEntryFromRecordV1(&info, &entry) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(entry.record_offset == info.record_offset);
  CHECK(entry.next_record_offset == info.next_record_offset);
  CHECK(entry.frame_crc32 == info.frame_crc32);
  CHECK(entry.record.record_class == WORR_NATIVE_RECORD_COMMAND_V1);
  CHECK(entry.record.object_epoch == 5 && entry.record.object_sequence == 9);

  memset(&sentinel, 0x5a, sizeof(sentinel));
  for (cut = 0; cut < encoded_bytes; ++cut) {
    info = sentinel;
    CHECK(Worr_NativeDemoRecordDecodeV1(frame, cut, 64, &info) ==
          WORR_NATIVE_DEMO_TRUNCATED);
    CHECK(memcmp(&info, &sentinel, sizeof(info)) == 0);
  }
  memcpy(malformed, frame, encoded_bytes);
  malformed[WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES + 50] ^= 1;
  info = sentinel;
  CHECK(Worr_NativeDemoRecordDecodeV1(malformed, encoded_bytes, 64, &info) ==
        WORR_NATIVE_DEMO_CORRUPT);
  CHECK(memcmp(&info, &sentinel, sizeof(info)) == 0);
  memcpy(malformed, frame, encoded_bytes);
  store_u16(malformed + 4, 2);
  CHECK(Worr_NativeDemoRecordDecodeV1(malformed, encoded_bytes, 64, &info) ==
        WORR_NATIVE_DEMO_UNSUPPORTED);
  memcpy(malformed, frame, encoded_bytes);
  malformed[10] = 1;
  CHECK(Worr_NativeDemoRecordDecodeV1(malformed, encoded_bytes, 64, &info) ==
        WORR_NATIVE_DEMO_MALFORMED);
  memcpy(malformed, frame, encoded_bytes);
  malformed[8] = WORR_NATIVE_DEMO_RECORD_EVENT_V1;
  refresh_frame_crc(malformed, encoded_bytes);
  CHECK(Worr_NativeDemoRecordDecodeV1(malformed, encoded_bytes, 64, &info) ==
        WORR_NATIVE_DEMO_MALFORMED);
  memcpy(malformed, frame, encoded_bytes);
  malformed[WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES + 4] = 2;
  refresh_frame_crc(malformed, encoded_bytes);
  CHECK(Worr_NativeDemoRecordDecodeV1(malformed, encoded_bytes, 64, &info) ==
        WORR_NATIVE_DEMO_UNSUPPORTED);
  memcpy(malformed, frame, encoded_bytes);
  malformed[WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES] = 'X';
  refresh_frame_crc(malformed, encoded_bytes);
  CHECK(Worr_NativeDemoRecordDecodeV1(malformed, encoded_bytes, 64, &info) ==
        WORR_NATIVE_DEMO_MALFORMED);
  memcpy(malformed, frame, encoded_bytes);
  malformed[8] = 99;
  CHECK(Worr_NativeDemoRecordDecodeV1(malformed, encoded_bytes, 64, &info) ==
        WORR_NATIVE_DEMO_UNSUPPORTED);
  memcpy(malformed, frame, encoded_bytes);
  store_u32(malformed + 16, (uint32_t)payload_bytes - 1u);
  CHECK(Worr_NativeDemoRecordDecodeV1(malformed, encoded_bytes, 64, &info) ==
        WORR_NATIVE_DEMO_MALFORMED);
  memcpy(malformed, frame, encoded_bytes);
  store_u32(malformed + 12, WORR_NATIVE_DEMO_MAX_RECORD_BYTES + 1u);
  store_u32(malformed + 16, WORR_NATIVE_CODEC_MAX_ENCODED_BYTES + 1u);
  CHECK(Worr_NativeDemoRecordDecodeV1(malformed, encoded_bytes, 64, &info) ==
        WORR_NATIVE_DEMO_LIMIT);
  CHECK(Worr_NativeDemoRecordDecodeV1(
            frame, encoded_bytes,
            UINT64_MAX - WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES + 1u,
            &info) == WORR_NATIVE_DEMO_LIMIT);

  memcpy(before, frame, sizeof(frame));
  CHECK(Worr_NativeDemoRecordEncodeV1(&record, payload, payload_bytes, frame,
                                      encoded_bytes - 1u, &size_sentinel) ==
        WORR_NATIVE_DEMO_OUTPUT_TOO_SMALL);
  CHECK(size_sentinel == SIZE_MAX);
  CHECK(memcmp(frame, before, sizeof(frame)) == 0);
  record.record_kind = WORR_NATIVE_DEMO_RECORD_EVENT_V1;
  CHECK(Worr_NativeDemoRecordEncodeV1(&record, payload, payload_bytes, frame,
                                      sizeof(frame), &size_sentinel) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(size_sentinel == SIZE_MAX);
  CHECK(memcmp(frame, before, sizeof(frame)) == 0);

  memcpy(malformed, payload, payload_bytes);
  memcpy(before, malformed, sizeof(malformed));
  record.record_kind = WORR_NATIVE_DEMO_RECORD_COMMAND_V1;
  CHECK(Worr_NativeDemoRecordEncodeV1(
            &record, malformed, payload_bytes, malformed, sizeof(malformed),
            &size_sentinel) == WORR_NATIVE_DEMO_INVALID_ARGUMENT);
  CHECK(size_sentinel == SIZE_MAX);
  CHECK(memcmp(malformed, before, sizeof(malformed)) == 0);

  memset(&entry_sentinel, 0x6b, sizeof(entry_sentinel));
  entry = entry_sentinel;
  info = sentinel;
  CHECK(Worr_NativeDemoIndexEntryFromRecordV1(&info, &entry) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(memcmp(&entry, &entry_sentinel, sizeof(entry)) == 0);

  CHECK(Worr_NativeDemoRecordDecodeV1(frame, encoded_bytes, 64, &info) ==
        WORR_NATIVE_DEMO_OK);
  info.record_offset = UINT64_MAX - 10u;
  info.payload_offset =
      info.record_offset + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES;
  info.next_record_offset = info.record_offset + info.frame_bytes;
  entry = entry_sentinel;
  CHECK(Worr_NativeDemoIndexEntryFromRecordV1(&info, &entry) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(memcmp(&entry, &entry_sentinel, sizeof(entry)) == 0);
  return true;
}

typedef struct decoded_sequence_s {
  worr_native_demo_container_info_v1 container;
  worr_native_demo_record_info_v1 snapshot1;
  worr_native_demo_record_info_v1 event1;
  worr_native_demo_record_info_v1 snapshot2;
  worr_native_demo_record_info_v1 command;
  worr_native_demo_record_info_v1 event2;
} decoded_sequence;

static bool build_sequence(decoded_sequence *sequence) {
  uint8_t file[TEST_FILE_BYTES];
  size_t file_bytes;
  uint64_t offset;

  CHECK(build_stream(file, sizeof(file), &file_bytes));

  CHECK(Worr_NativeDemoContainerDecodeV1(
            file, file_bytes, &sequence->container) == WORR_NATIVE_DEMO_OK);
  offset = WORR_NATIVE_DEMO_WIRE_HEADER_BYTES;
  CHECK(Worr_NativeDemoRecordDecodeV1(
            file + offset, file_bytes - (size_t)offset, offset,
            &sequence->snapshot1) == WORR_NATIVE_DEMO_OK);
  CHECK(sequence->snapshot1.record_offset == offset);
  offset = sequence->snapshot1.next_record_offset;
  CHECK(Worr_NativeDemoRecordDecodeV1(
            file + offset, file_bytes - (size_t)offset, offset,
            &sequence->event1) == WORR_NATIVE_DEMO_OK);
  CHECK(sequence->event1.record_offset == offset);
  offset = sequence->event1.next_record_offset;
  CHECK(Worr_NativeDemoRecordDecodeV1(
            file + offset, file_bytes - (size_t)offset, offset,
            &sequence->snapshot2) == WORR_NATIVE_DEMO_OK);
  CHECK(sequence->snapshot2.record_offset == offset);
  offset = sequence->snapshot2.next_record_offset;
  CHECK(Worr_NativeDemoRecordDecodeV1(
            file + offset, file_bytes - (size_t)offset, offset,
            &sequence->command) == WORR_NATIVE_DEMO_OK);
  CHECK(sequence->command.record_offset == offset);
  offset = sequence->command.next_record_offset;
  CHECK(Worr_NativeDemoRecordDecodeV1(
            file + offset, file_bytes - (size_t)offset, offset,
            &sequence->event2) == WORR_NATIVE_DEMO_OK);
  CHECK(sequence->event2.next_record_offset == file_bytes);
  return true;
}

static bool
expect_order_failure_unchanged(worr_native_demo_order_state_v1 *state,
                               const worr_native_demo_record_info_v1 *record) {
  const worr_native_demo_order_state_v1 before = *state;

  CHECK(Worr_NativeDemoOrderObserveV1(state, record) == WORR_NATIVE_DEMO_ORDER);
  CHECK(memcmp(state, &before, sizeof(*state)) == 0);
  return true;
}

static bool test_ordering(void) {
  decoded_sequence sequence;
  worr_native_demo_order_state_v1 state;
  worr_native_demo_order_state_v1 state_sentinel;
  worr_native_demo_record_info_v1 changed;

  CHECK(build_sequence(&sequence));
  memset(&state, 0xcc, sizeof(state));
  CHECK(Worr_NativeDemoOrderInitV1(&sequence.container, &state) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(state.first_record_ordinal == 100);
  CHECK(Worr_NativeDemoOrderObserveV1(&state, &sequence.snapshot1) ==
        WORR_NATIVE_DEMO_OK);
  /* An event strictly between two snapshot times is valid. */
  CHECK(sequence.snapshot1.time_us < sequence.event1.time_us &&
        sequence.event1.time_us < sequence.snapshot2.time_us);
  CHECK(Worr_NativeDemoOrderObserveV1(&state, &sequence.event1) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(Worr_NativeDemoOrderObserveV1(&state, &sequence.snapshot2) ==
        WORR_NATIVE_DEMO_OK);
  /* Same-time commands do not disturb snapshot-before-event ordering. */
  CHECK(Worr_NativeDemoOrderObserveV1(&state, &sequence.command) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(Worr_NativeDemoOrderObserveV1(&state, &sequence.event2) ==
        WORR_NATIVE_DEMO_OK);

  /* An event before any snapshot is invalid and transactional. */
  CHECK(Worr_NativeDemoOrderInitV1(&sequence.container, &state) ==
        WORR_NATIVE_DEMO_OK);
  changed = sequence.event1;
  changed.ordinal = 100;
  CHECK(expect_order_failure_unchanged(&state, &changed));

  /* A same-time event followed by a snapshot is invalid. */
  CHECK(Worr_NativeDemoOrderObserveV1(&state, &sequence.snapshot1) ==
        WORR_NATIVE_DEMO_OK);
  changed = sequence.event1;
  changed.time_us = 2000;
  CHECK(Worr_NativeDemoOrderObserveV1(&state, &changed) == WORR_NATIVE_DEMO_OK);
  changed = sequence.snapshot2;
  CHECK(expect_order_failure_unchanged(&state, &changed));

  /* First ordinal, monotonic time, and per-class identity all fail closed. */
  CHECK(Worr_NativeDemoOrderInitV1(&sequence.container, &state) ==
        WORR_NATIVE_DEMO_OK);
  changed = sequence.snapshot1;
  changed.ordinal = 101;
  CHECK(expect_order_failure_unchanged(&state, &changed));
  CHECK(Worr_NativeDemoOrderObserveV1(&state, &sequence.snapshot1) ==
        WORR_NATIVE_DEMO_OK);
  changed = sequence.event1;
  changed.ordinal = 100;
  CHECK(expect_order_failure_unchanged(&state, &changed));
  changed = sequence.event1;
  changed.time_us = 999;
  CHECK(expect_order_failure_unchanged(&state, &changed));
  CHECK(Worr_NativeDemoOrderObserveV1(&state, &sequence.event1) ==
        WORR_NATIVE_DEMO_OK);
  changed = sequence.event1;
  changed.ordinal = 102;
  changed.time_us = 1600;
  CHECK(expect_order_failure_unchanged(&state, &changed));

  memset(&state_sentinel, 0x39, sizeof(state_sentinel));
  sequence.container.reserved0 = 1;
  state = state_sentinel;
  CHECK(Worr_NativeDemoOrderInitV1(&sequence.container, &state) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(memcmp(&state, &state_sentinel, sizeof(state)) == 0);

  sequence.container.reserved0 = 0;
  CHECK(Worr_NativeDemoOrderInitV1(&sequence.container, &state) ==
        WORR_NATIVE_DEMO_OK);
  state.state_flags |= WORR_NATIVE_DEMO_ORDER_OBSERVED;
  state.last_ordinal = 100;
  state.seen_class_mask = UINT32_C(1) << WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1;
  state_sentinel = state;
  CHECK(Worr_NativeDemoOrderObserveV1(&state, &sequence.event1) ==
        WORR_NATIVE_DEMO_INVALID_STATE);
  CHECK(memcmp(&state, &state_sentinel, sizeof(state)) == 0);
  return true;
}

static worr_native_demo_scan_config_v1 make_scan_config(uint64_t max_records) {
  worr_native_demo_scan_config_v1 config;

  memset(&config, 0, sizeof(config));
  config.struct_size = sizeof(config);
  config.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  config.stream_offset = UINT64_C(0x100000000);
  config.max_records = max_records;
  config.max_entities = TEST_MAX_ENTITIES;
  return config;
}

static worr_native_demo_seek_query_v1
make_seek_query(uint16_t flags, uint64_t time_us, uint64_t ordinal) {
  worr_native_demo_seek_query_v1 query;

  memset(&query, 0, sizeof(query));
  query.struct_size = sizeof(query);
  query.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  query.flags = flags;
  query.target_time_us = time_us;
  query.target_ordinal = ordinal;
  return query;
}

static bool test_stream_scan_and_seek(void) {
  uint8_t file[TEST_FILE_BYTES];
  uint8_t changed[TEST_FILE_BYTES];
  worr_native_demo_scan_config_v1 config = make_scan_config(TEST_INDEX_ENTRIES);
  worr_native_demo_scan_info_v1 scan;
  worr_native_demo_scan_info_v1 failed_scan;
  worr_native_demo_scan_info_v1 scan_sentinel;
  worr_native_demo_scan_info_v1 changed_scan;
  worr_native_demo_index_entry_v1 entries[TEST_INDEX_ENTRIES];
  worr_native_demo_index_entry_v1 failed_entries[TEST_INDEX_ENTRIES];
  worr_native_demo_index_entry_v1 entries_sentinel[TEST_INDEX_ENTRIES];
  worr_native_demo_index_entry_v1 changed_entries[TEST_INDEX_ENTRIES];
  worr_native_demo_seek_query_v1 query;
  worr_native_demo_seek_result_v1 result;
  worr_native_demo_seek_result_v1 result_sentinel;
  worr_native_demo_result_v1 scan_result;
  size_t file_bytes;
  size_t first_offset;
  size_t event_offset;
  size_t command_offset;
  size_t index;

  CHECK(build_stream(file, sizeof(file), &file_bytes));
  memset(&scan_sentinel, 0x3c, sizeof(scan_sentinel));
  memset(entries_sentinel, 0xa7, sizeof(entries_sentinel));

  /* Count-only scanning validates the entire image without retaining bytes. */
  scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, file, file_bytes, NULL, 0,
                                    &scan) == WORR_NATIVE_DEMO_OK);
  CHECK(scan.struct_size == sizeof(scan));
  CHECK(scan.flags ==
        (WORR_NATIVE_DEMO_SCAN_COMPLETE | WORR_NATIVE_DEMO_SCAN_HAS_RECORDS |
         WORR_NATIVE_DEMO_SCAN_HAS_SNAPSHOTS));
  CHECK(scan.stream_offset == config.stream_offset);
  CHECK(scan.stream_bytes == file_bytes);
  CHECK(scan.next_stream_offset == config.stream_offset + file_bytes);
  CHECK(scan.record_count == TEST_INDEX_ENTRIES);
  CHECK(scan.snapshot_count == 2);
  CHECK(scan.container.first_record_ordinal == 100);
  CHECK(scan.order.last_ordinal == 104);
  CHECK(scan.order.last_time_us == 2000);

  memcpy(entries, entries_sentinel, sizeof(entries));
  scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, file, file_bytes, entries,
                                    TEST_INDEX_ENTRIES,
                                    &scan) == WORR_NATIVE_DEMO_OK);
  CHECK(entries[0].record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1);
  CHECK(entries[0].ordinal == 100 && entries[0].time_us == 1000);
  CHECK(entries[1].record_kind == WORR_NATIVE_DEMO_RECORD_EVENT_V1);
  CHECK(entries[1].ordinal == 101 && entries[1].time_us == 1500);
  CHECK(entries[2].record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1);
  CHECK(entries[2].ordinal == 102 && entries[2].time_us == 2000);
  CHECK(entries[3].record_kind == WORR_NATIVE_DEMO_RECORD_COMMAND_V1);
  CHECK(entries[4].record_kind == WORR_NATIVE_DEMO_RECORD_EVENT_V1);
  CHECK(entries[0].record_offset ==
        config.stream_offset + WORR_NATIVE_DEMO_WIRE_HEADER_BYTES);
  for (index = 1; index < TEST_INDEX_ENTRIES; ++index) {
    CHECK(entries[index].record_offset ==
          entries[index - 1].next_record_offset);
  }
  CHECK(entries[TEST_INDEX_ENTRIES - 1].next_record_offset ==
        scan.next_stream_offset);
  CHECK(scan.index_crc32 != 0);

  /* Every single-bit corruption in the complete image fails transactionally. */
  for (index = 0; index < file_bytes; ++index) {
    memcpy(changed, file, file_bytes);
    changed[index] ^= 1;
    failed_scan = scan_sentinel;
    scan_result = Worr_NativeDemoStreamScanV1(&config, changed, file_bytes,
                                              NULL, 0, &failed_scan);
    CHECK(scan_result != WORR_NATIVE_DEMO_OK);
    CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);
  }

  /* Both enabled bounds apply; the latest qualifying snapshot wins. */
  query = make_seek_query(WORR_NATIVE_DEMO_SEEK_BY_TIME |
                              WORR_NATIVE_DEMO_SEEK_BY_ORDINAL,
                          2000, 101);
  memset(&result, 0xcc, sizeof(result));
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &scan, entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(result.flags ==
        (WORR_NATIVE_DEMO_SEEK_FOUND | WORR_NATIVE_DEMO_SEEK_TIME_CONSTRAINED |
         WORR_NATIVE_DEMO_SEEK_ORDINAL_CONSTRAINED));
  CHECK(result.entry_index == 0 && result.entry.ordinal == 100);

  query = make_seek_query(WORR_NATIVE_DEMO_SEEK_BY_TIME |
                              WORR_NATIVE_DEMO_SEEK_BY_ORDINAL,
                          2000, 102);
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &scan, entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(result.entry_index == 2 && result.entry.ordinal == 102);

  query = make_seek_query(WORR_NATIVE_DEMO_SEEK_BY_TIME, 1999, 0);
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &scan, entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(result.entry_index == 0 && result.entry.ordinal == 100);
  query = make_seek_query(WORR_NATIVE_DEMO_SEEK_BY_ORDINAL, 0, 102);
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &scan, entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_OK);
  CHECK(result.entry_index == 2 && result.entry.ordinal == 102);

  memset(&result_sentinel, 0x69, sizeof(result_sentinel));
  result = result_sentinel;
  query = make_seek_query(WORR_NATIVE_DEMO_SEEK_BY_TIME |
                              WORR_NATIVE_DEMO_SEEK_BY_ORDINAL,
                          999, 99);
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &scan, entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_NOT_FOUND);
  CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);

  /* Caller bounds and index capacity fail without partial output. */
  config.max_records = TEST_INDEX_ENTRIES - 1;
  failed_scan = scan_sentinel;
  memcpy(failed_entries, entries_sentinel, sizeof(failed_entries));
  CHECK(Worr_NativeDemoStreamScanV1(&config, file, file_bytes, failed_entries,
                                    TEST_INDEX_ENTRIES,
                                    &failed_scan) == WORR_NATIVE_DEMO_LIMIT);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);
  CHECK(memcmp(failed_entries, entries_sentinel, sizeof(failed_entries)) == 0);

  config.max_records = TEST_INDEX_ENTRIES;
  failed_scan = scan_sentinel;
  memcpy(failed_entries, entries_sentinel, sizeof(failed_entries));
  CHECK(Worr_NativeDemoStreamScanV1(&config, file, file_bytes, failed_entries,
                                    TEST_INDEX_ENTRIES - 1, &failed_scan) ==
        WORR_NATIVE_DEMO_OUTPUT_TOO_SMALL);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);
  CHECK(memcmp(failed_entries, entries_sentinel, sizeof(failed_entries)) == 0);

  failed_scan = scan_sentinel;
  memcpy(failed_entries, entries_sentinel, sizeof(failed_entries));
  CHECK(Worr_NativeDemoStreamScanV1(
            &config, file, file_bytes - 1, failed_entries, TEST_INDEX_ENTRIES,
            &failed_scan) == WORR_NATIVE_DEMO_TRUNCATED);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);
  CHECK(memcmp(failed_entries, entries_sentinel, sizeof(failed_entries)) == 0);

  memcpy(changed, file, file_bytes);
  changed[file_bytes] = 0x7f;
  config.max_records = TEST_INDEX_ENTRIES + 1;
  failed_scan = scan_sentinel;
  memcpy(failed_entries, entries_sentinel, sizeof(failed_entries));
  CHECK(Worr_NativeDemoStreamScanV1(
            &config, changed, file_bytes + 1, failed_entries,
            TEST_INDEX_ENTRIES, &failed_scan) == WORR_NATIVE_DEMO_TRUNCATED);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);
  CHECK(memcmp(failed_entries, entries_sentinel, sizeof(failed_entries)) == 0);
  config.max_records = TEST_INDEX_ENTRIES;

  /* Corrupt container and record data are distinguished from truncation. */
  memcpy(changed, file, file_bytes);
  changed[40] ^= 1;
  failed_scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, changed, file_bytes, NULL, 0,
                                    &failed_scan) == WORR_NATIVE_DEMO_CORRUPT);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);

  first_offset = (size_t)(entries[0].record_offset - config.stream_offset);
  event_offset = (size_t)(entries[1].record_offset - config.stream_offset);
  command_offset = (size_t)(entries[3].record_offset - config.stream_offset);
  memcpy(changed, file, file_bytes);
  changed[event_offset + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES] ^= 1;
  failed_scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, changed, file_bytes, NULL, 0,
                                    &failed_scan) == WORR_NATIVE_DEMO_CORRUPT);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);

  /* Valid outer CRCs cannot conceal invalid canonical class bodies. */
  memcpy(changed, file, file_bytes);
  changed[command_offset + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES +
          WORR_NATIVE_CODEC_WIRE_HEADER_BYTES + 8] = UINT8_C(251);
  refresh_frame_crc(changed + command_offset, entries[3].frame_bytes);
  failed_scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, changed, file_bytes, NULL, 0,
                                    &failed_scan) ==
        WORR_NATIVE_DEMO_MALFORMED);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);

  memcpy(changed, file, file_bytes);
  store_u32(changed + event_offset + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES +
                WORR_NATIVE_CODEC_WIRE_HEADER_BYTES,
            0);
  refresh_frame_crc(changed + event_offset, entries[1].frame_bytes);
  failed_scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, changed, file_bytes, NULL, 0,
                                    &failed_scan) ==
        WORR_NATIVE_DEMO_MALFORMED);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);

  memcpy(changed, file, file_bytes);
  store_u32(changed + first_offset + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES +
                WORR_NATIVE_CODEC_WIRE_HEADER_BYTES,
            0);
  refresh_frame_crc(changed + first_offset, entries[0].frame_bytes);
  failed_scan = scan_sentinel;
  scan_result = Worr_NativeDemoStreamScanV1(&config, changed, file_bytes, NULL,
                                            0, &failed_scan);
  CHECK(scan_result == WORR_NATIVE_DEMO_MALFORMED ||
        scan_result == WORR_NATIVE_DEMO_CORRUPT);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);

  memcpy(changed, file, file_bytes);
  store_u32(changed + first_offset + 12,
            WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES);
  failed_scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, changed, file_bytes, NULL, 0,
                                    &failed_scan) ==
        WORR_NATIVE_DEMO_MALFORMED);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);

  /* A same-time event followed by a snapshot is rejected after valid CRC. */
  memcpy(changed, file, file_bytes);
  store_u64(changed + event_offset + 32, 2000);
  refresh_frame_crc(changed + event_offset, entries[1].frame_bytes);
  failed_scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, changed, file_bytes, NULL, 0,
                                    &failed_scan) == WORR_NATIVE_DEMO_ORDER);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);

  config.stream_offset = UINT64_MAX - (uint64_t)file_bytes + 1;
  failed_scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, file, file_bytes, NULL, 0,
                                    &failed_scan) == WORR_NATIVE_DEMO_LIMIT);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);
  config = make_scan_config(TEST_INDEX_ENTRIES);

  config.max_entities = 0;
  failed_scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, file, file_bytes, NULL, 0,
                                    &failed_scan) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);
  config.max_entities = 1;
  failed_scan = scan_sentinel;
  scan_result = Worr_NativeDemoStreamScanV1(&config, file, file_bytes, NULL, 0,
                                            &failed_scan);
  CHECK(scan_result != WORR_NATIVE_DEMO_OK);
  CHECK(memcmp(&failed_scan, &scan_sentinel, sizeof(failed_scan)) == 0);
  config = make_scan_config(TEST_INDEX_ENTRIES);

  /* Seek validates every index entry, not only the selected prefix. */
  memcpy(changed_entries, entries, sizeof(changed_entries));
  changed_entries[2].ordinal = 101;
  result = result_sentinel;
  query = make_seek_query(WORR_NATIVE_DEMO_SEEK_BY_TIME, 2000, 0);
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &scan, changed_entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_ORDER);
  CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);

  memcpy(changed_entries, entries, sizeof(changed_entries));
  changed_entries[0].time_us = 1100;
  result = result_sentinel;
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &scan, changed_entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);

  memcpy(changed_entries, entries, sizeof(changed_entries));
  changed_entries[2].record_offset += 1;
  result = result_sentinel;
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &scan, changed_entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);

  memcpy(changed_entries, entries, sizeof(changed_entries));
  changed_entries[4].frame_crc32 ^= 1;
  result = result_sentinel;
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &scan, changed_entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);

  changed_scan = scan;
  changed_scan.index_crc32 ^= 1;
  result = result_sentinel;
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &changed_scan, entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);

  query.flags = 0;
  result = result_sentinel;
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
            &scan, entries, TEST_INDEX_ENTRIES, &query, &result) ==
        WORR_NATIVE_DEMO_INVALID_METADATA);
  CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);

  /* Header-only streams are valid within a zero-record work bound. */
  config = make_scan_config(0);
  failed_scan = scan_sentinel;
  CHECK(Worr_NativeDemoStreamScanV1(&config, file,
                                    WORR_NATIVE_DEMO_WIRE_HEADER_BYTES, NULL, 0,
                                    &failed_scan) == WORR_NATIVE_DEMO_OK);
  CHECK(failed_scan.flags == WORR_NATIVE_DEMO_SCAN_COMPLETE);
  CHECK(failed_scan.record_count == 0 && failed_scan.snapshot_count == 0);
  CHECK(failed_scan.index_crc32 == 0);
  query = make_seek_query(WORR_NATIVE_DEMO_SEEK_BY_TIME, UINT64_MAX, 0);
  result = result_sentinel;
  CHECK(Worr_NativeDemoSeekSnapshotAtOrBeforeV1(&failed_scan, NULL, 0, &query,
                                                &result) ==
        WORR_NATIVE_DEMO_NOT_FOUND);
  CHECK(memcmp(&result, &result_sentinel, sizeof(result)) == 0);
  return true;
}

int main(void) {
  if (!test_container() || !test_record_codec() || !test_ordering() ||
      !test_stream_scan_and_seek())
    return 1;
  puts("native_demo_test: all tests passed");
  return 0;
}
