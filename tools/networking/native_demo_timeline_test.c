/*
 * FR-10-T13 present-once timeline planner: proves a validated WDM1 index
 * resolves to a single deterministic dispatch plan (each record once, in
 * canonical ordinal order, every event anchored to a prior snapshot), that the
 * plan fingerprint is reproducible, and that a reordered / mismatched index or
 * an unanchored event fails closed.  Pure core: opens no files, drives no live
 * client.
 */

#include "common/net/native_demo_playback.h"
#include "common/net/native_demo_recorder.h"
#include "common/net/native_demo_timeline.h"

#include "shared/command_abi.h"
#include "shared/event_abi.h"
#include "shared/prediction_abi.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TEST_MAX_ENTITIES 64u
#define TEST_FILE_BYTES (1024u * 1024u)
#define TEST_INDEX_CAPACITY 16u

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "native_demo_timeline_test:%d: %s\n", __LINE__,          \
              #condition);                                                     \
      return false;                                                            \
    }                                                                          \
  } while (0)

typedef struct memory_sink_s {
  uint8_t bytes[TEST_FILE_BYTES];
  size_t size;
} memory_sink;

static uint8_t codec_scratch[WORR_NATIVE_CODEC_MAX_ENCODED_BYTES];
static uint8_t record_scratch[WORR_NATIVE_DEMO_MAX_RECORD_BYTES];
static memory_sink sink_storage;
static worr_native_demo_index_entry_v1 index_scratch[TEST_INDEX_CAPACITY];
static worr_native_demo_timeline_step_v1 step_scratch[TEST_INDEX_CAPACITY];

static int64_t memory_write(void *opaque, const void *bytes,
                            size_t byte_count) {
  memory_sink *sink = (memory_sink *)opaque;

  if (byte_count > sizeof(sink->bytes) - sink->size)
    return -1;
  memcpy(sink->bytes + sink->size, bytes, byte_count);
  sink->size += byte_count;
  return (int64_t)byte_count;
}

static worr_snapshot_entity_generation_v2 make_generation(uint32_t index,
                                                          uint32_t generation) {
  worr_snapshot_entity_generation_v2 result;

  memset(&result, 0, sizeof(result));
  result.identity.index = index;
  result.identity.generation = generation;
  result.provenance_flags = WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
  return result;
}

static worr_snapshot_player_v2 make_player(uint32_t controlled_index) {
  worr_snapshot_player_v2 player;

  memset(&player, 0, sizeof(player));
  player.struct_size = sizeof(player);
  player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  player.controlled_entity = make_generation(controlled_index, 4);
  player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
  player.movement.struct_size = sizeof(player.movement);
  player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
  player.movement.gravity = 800;
  player.movement.view_height = 22;
  player.fov = 90.0f;
  return player;
}

typedef struct snapshot_fixture_s {
  worr_snapshot_store_v2 store;
  worr_snapshot_store_slot_v2 slot[1];
  worr_snapshot_projection_view_v2 view;
  worr_snapshot_projection_hashes_v2 hashes;
  worr_snapshot_ref_v2 ref;
} snapshot_fixture;

static bool build_snapshot(snapshot_fixture *fixture, uint32_t sequence,
                           uint32_t previous_sequence, uint64_t time_us,
                           uint32_t controlled_index) {
  worr_snapshot_v2 snapshot;
  worr_snapshot_player_v2 player = make_player(controlled_index);
  worr_snapshot_store_publish_v2 publication;

  memset(fixture, 0, sizeof(*fixture));
  if (Worr_SnapshotStoreInitV2(&fixture->store, fixture->slot, 1, NULL, 0, 0,
                               NULL, 0, 0, NULL, 0, 0,
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
  snapshot.snapshot_id.epoch = 9;
  snapshot.snapshot_id.sequence = sequence;
  snapshot.server_tick = 100 + sequence;
  snapshot.server_time_us = time_us;
  snapshot.controlled_entity = make_generation(controlled_index, 4);
  snapshot.discontinuity.flags = WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
  if (sequence == 1) {
    snapshot.discontinuity.flags |= WORR_SNAPSHOT_DISCONTINUITY_INITIAL;
    snapshot.discontinuity.reason = WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
  } else {
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_FULL_SNAPSHOT;
    snapshot.discontinuity.previous.epoch = 9;
    snapshot.discontinuity.previous.sequence = previous_sequence;
    snapshot.discontinuity.server_tick_delta = sequence - previous_sequence;
  }
  memset(&publication, 0, sizeof(publication));
  publication.struct_size = sizeof(publication);
  publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
  publication.snapshot = &snapshot;
  publication.player = &player;
  if (Worr_SnapshotStorePublishV2(&fixture->store, &publication,
                                  &fixture->ref) != WORR_SNAPSHOT_STORE_OK) {
    return false;
  }
  fixture->view.struct_size = sizeof(fixture->view);
  fixture->view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
  fixture->view.snapshot = &fixture->slot[fixture->ref.slot].snapshot;
  fixture->view.player = &fixture->slot[fixture->ref.slot].player;
  return Worr_SnapshotProjectionHashesV2(&fixture->view, TEST_MAX_ENTITIES,
                                         &fixture->hashes);
}

static worr_command_record_v1 build_command(uint32_t epoch, uint32_t sequence,
                                            uint8_t duration_ms,
                                            uint64_t sample_time_us) {
  worr_command_record_v1 record;

  memset(&record, 0, sizeof(record));
  record.struct_size = sizeof(record);
  record.schema_version = WORR_COMMAND_ABI_VERSION;
  record.command_id.epoch = epoch;
  record.command_id.sequence = sequence;
  record.sample_time_us = sample_time_us;
  record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  record.command.struct_size = sizeof(record.command);
  record.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  record.command.duration_ms = duration_ms;
  record.command.buttons = 1u;
  record.render_watermark.struct_size = sizeof(record.render_watermark);
  record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  record.render_watermark.provenance =
      WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  record.render_watermark.source_server_tick = sequence;
  record.render_watermark.tick_interval_us = 10000;
  record.render_watermark.source_server_time_us =
      (uint64_t)sequence * UINT64_C(10000);
  record.render_watermark.rendered_server_time_us =
      record.render_watermark.source_server_time_us;
  (void)Worr_CommandRecordCanonicalizeV1(
      &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS);
  return record;
}

static worr_event_record_v1 build_event(uint32_t epoch, uint32_t sequence,
                                        uint32_t source_tick,
                                        uint64_t source_time_us) {
  worr_event_record_v1 record;
  const worr_event_payload_u32x4_v1 payload = {
      {0x1111u, 0x2222u, sequence, 0}};

  memset(&record, 0, sizeof(record));
  record.struct_size = sizeof(record);
  record.schema_version = WORR_EVENT_ABI_VERSION;
  record.model_revision = WORR_EVENT_MODEL_REVISION;
  record.flags = WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE |
                 WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
  record.event_id.stream_epoch = epoch;
  record.event_id.sequence = sequence;
  record.source_tick = source_tick;
  record.source_time_us = source_time_us;
  record.source_entity.index = 4;
  record.source_entity.generation = 1;
  record.subject_entity.index = WORR_EVENT_NO_ENTITY;
  record.subject_entity.generation = 0;
  record.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
  record.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
  record.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
  record.payload_kind = WORR_EVENT_PAYLOAD_U32X4;
  record.payload_size = sizeof(payload);
  memcpy(record.payload, &payload, sizeof(payload));
  return record;
}

/* Record snap1, a command, an event, and snap2 in canonical order, then open
 * the image.  Leaves the validated index in index_scratch and the record count
 * in *record_count_out. */
static bool record_and_open(uint64_t *record_count_out) {
  worr_native_demo_recorder_config_v1 config;
  worr_native_demo_recorder_storage_v1 storage;
  worr_native_demo_recorder_sink_v1 output;
  worr_native_demo_recorder_state_v1 state;
  worr_native_demo_playback_config_v1 play_config;
  worr_native_demo_playback_cursor_v1 cursor;
  snapshot_fixture snap1;
  snapshot_fixture snap2;
  worr_command_record_v1 command;
  worr_event_record_v1 event;

  memset(&sink_storage, 0, sizeof(sink_storage));

  memset(&config, 0, sizeof(config));
  config.struct_size = sizeof(config);
  config.schema_version = WORR_NATIVE_DEMO_RECORDER_ABI_VERSION;
  config.capability_mask = WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK;
  config.transport_epoch = 17;
  config.timeline_epoch = 9;
  config.max_entities = TEST_MAX_ENTITIES;
  config.created_time_us = UINT64_C(1234567);
  config.max_stream_bytes = TEST_FILE_BYTES;
  config.controlled_entity.index = 1;
  config.controlled_entity.generation = 4;
  memset(&storage, 0, sizeof(storage));
  storage.struct_size = sizeof(storage);
  storage.schema_version = WORR_NATIVE_DEMO_RECORDER_ABI_VERSION;
  storage.codec_bytes = codec_scratch;
  storage.record_bytes = record_scratch;
  storage.codec_capacity = sizeof(codec_scratch);
  storage.record_capacity = sizeof(record_scratch);
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_NATIVE_DEMO_RECORDER_ABI_VERSION;
  output.opaque = &sink_storage;
  output.WriteExact = memory_write;
  CHECK(Worr_NativeDemoRecorderBeginV1(&state, &config, &storage, &output) ==
        WORR_NATIVE_DEMO_RECORDER_OK);

  CHECK(build_snapshot(&snap1, 1, 0, UINT64_C(1000000), 1));
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(&state, &snap1.view,
                                                 &snap1.hashes, snap1.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  command = build_command(5, 1, 40, UINT64_C(1100000));
  CHECK(Worr_NativeDemoRecorderObserveCommandV1(
            &state, &command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  event = build_event(7, 1, 101, UINT64_C(1200000));
  CHECK(Worr_NativeDemoRecorderObserveEventV1(&state, &event,
                                              TEST_MAX_ENTITIES) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(build_snapshot(&snap2, 2, 1, UINT64_C(2000000), 1));
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(&state, &snap2.view,
                                                 &snap2.hashes, snap2.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderStopV1(&state) == WORR_NATIVE_DEMO_RECORDER_OK);

  memset(&play_config, 0, sizeof(play_config));
  play_config.struct_size = sizeof(play_config);
  play_config.schema_version = WORR_NATIVE_DEMO_PLAYBACK_ABI_VERSION;
  play_config.max_entities = TEST_MAX_ENTITIES;
  play_config.expected_transport_epoch = 17;
  play_config.expected_timeline_epoch = 9;
  play_config.max_records = TEST_INDEX_CAPACITY;
  CHECK(Worr_NativeDemoPlaybackOpenV1(&play_config, sink_storage.bytes,
                                      sink_storage.size, index_scratch,
                                      TEST_INDEX_CAPACITY, &cursor,
                                      record_count_out) ==
        WORR_NATIVE_DEMO_PLAYBACK_OK);
  CHECK(*record_count_out == 4);
  return true;
}

static worr_native_demo_timeline_config_v1 make_config(void) {
  worr_native_demo_timeline_config_v1 config;

  memset(&config, 0, sizeof(config));
  config.struct_size = sizeof(config);
  config.schema_version = 1u;
  config.flags = 0;
  config.max_steps = 0;
  return config;
}

static bool test_plan(void) {
  worr_native_demo_timeline_config_v1 config = make_config();
  worr_native_demo_timeline_summary_v1 summary;
  worr_native_demo_timeline_summary_v1 summary2;
  worr_native_demo_timeline_step_v1 step_scratch2[TEST_INDEX_CAPACITY];
  uint64_t record_count = 0;
  uint64_t snapshot_anchor = 0;
  size_t i;

  CHECK(record_and_open(&record_count));

  /* Full plan: one step per record, in strictly increasing ordinal order. */
  CHECK(Worr_NativeDemoTimelineBuildV1(
            &config, sink_storage.bytes, sink_storage.size, index_scratch,
            (size_t)record_count, step_scratch, TEST_INDEX_CAPACITY,
            &summary) == WORR_NATIVE_DEMO_TIMELINE_OK);
  CHECK(summary.step_count == record_count);
  CHECK(summary.snapshot_count == 2);
  CHECK(summary.command_count == 1);
  CHECK(summary.event_count == 1);
  CHECK(summary.first_ordinal < summary.last_ordinal);
  CHECK((summary.flags & WORR_NATIVE_DEMO_TIMELINE_SUMMARY_LEADING_SNAPSHOT) !=
        0);

  for (i = 0; i < (size_t)record_count; ++i) {
    CHECK(step_scratch[i].present_sequence == (uint64_t)i);
    CHECK(step_scratch[i].entry_index == (uint64_t)i);
    CHECK(step_scratch[i].ordinal == index_scratch[i].ordinal);
    if (i > 0)
      CHECK(step_scratch[i].ordinal > step_scratch[i - 1].ordinal);
    if (step_scratch[i].record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1)
      snapshot_anchor = step_scratch[i].ordinal;
    /* Every event resolves against the most recent prior snapshot. */
    if (step_scratch[i].record_kind == WORR_NATIVE_DEMO_RECORD_EVENT_V1) {
      CHECK(snapshot_anchor != 0);
      CHECK(step_scratch[i].anchor_snapshot_ordinal == snapshot_anchor);
    }
  }
  CHECK((step_scratch[0].flags & WORR_NATIVE_DEMO_TIMELINE_STEP_FIRST) != 0);
  CHECK((step_scratch[0].flags &
         WORR_NATIVE_DEMO_TIMELINE_STEP_SNAPSHOT_ANCHOR) != 0);

  /* Determinism: a second build of the same stream is bit-identical. */
  CHECK(Worr_NativeDemoTimelineBuildV1(
            &config, sink_storage.bytes, sink_storage.size, index_scratch,
            (size_t)record_count, step_scratch2, TEST_INDEX_CAPACITY,
            &summary2) == WORR_NATIVE_DEMO_TIMELINE_OK);
  CHECK(summary2.plan_fingerprint == summary.plan_fingerprint);
  CHECK(memcmp(step_scratch2, step_scratch,
               sizeof(step_scratch[0]) * (size_t)record_count) == 0);
  return true;
}

static bool test_capacity(void) {
  worr_native_demo_timeline_config_v1 config = make_config();
  worr_native_demo_timeline_summary_v1 summary;
  uint64_t record_count = 0;

  CHECK(record_and_open(&record_count));
  /* One slot short of the record count reports the requirement and touches no
   * step. */
  memset(step_scratch, 0xAB, sizeof(step_scratch));
  CHECK(Worr_NativeDemoTimelineBuildV1(
            &config, sink_storage.bytes, sink_storage.size, index_scratch,
            (size_t)record_count, step_scratch, (size_t)record_count - 1,
            &summary) == WORR_NATIVE_DEMO_TIMELINE_CAPACITY);
  CHECK(summary.step_count == record_count);
  return true;
}

static bool test_unanchored_event_fails(void) {
  worr_native_demo_timeline_config_v1 config = make_config();
  worr_native_demo_timeline_summary_v1 summary;
  uint64_t record_count = 0;

  CHECK(record_and_open(&record_count));
  /* Make the first record present as an EVENT: present-once has no base
   * snapshot to anchor it, so the plan must fail closed. */
  index_scratch[0].record_kind = WORR_NATIVE_DEMO_RECORD_EVENT_V1;
  CHECK(Worr_NativeDemoTimelineBuildV1(
            &config, sink_storage.bytes, sink_storage.size, index_scratch,
            (size_t)record_count, step_scratch, TEST_INDEX_CAPACITY,
            &summary) == WORR_NATIVE_DEMO_TIMELINE_ORDER);
  return true;
}

static bool test_reordered_index_fails(void) {
  worr_native_demo_timeline_config_v1 config = make_config();
  worr_native_demo_timeline_summary_v1 summary;
  worr_native_demo_index_entry_v1 swap;
  uint64_t record_count = 0;

  CHECK(record_and_open(&record_count));
  /* Swap two entries so ordinals are no longer strictly increasing (and record
   * offsets are no longer contiguous): a reordered index cannot be replayed. */
  swap = index_scratch[1];
  index_scratch[1] = index_scratch[2];
  index_scratch[2] = swap;
  CHECK(Worr_NativeDemoTimelineBuildV1(
            &config, sink_storage.bytes, sink_storage.size, index_scratch,
            (size_t)record_count, step_scratch, TEST_INDEX_CAPACITY,
            &summary) != WORR_NATIVE_DEMO_TIMELINE_OK);
  return true;
}

static bool test_truncated_index_fails(void) {
  worr_native_demo_timeline_config_v1 config = make_config();
  worr_native_demo_timeline_summary_v1 summary;
  uint64_t record_count = 0;

  CHECK(record_and_open(&record_count));
  /* An index whose last record runs past the byte image fails closed. */
  index_scratch[record_count - 1].next_record_offset = sink_storage.size + 1;
  CHECK(Worr_NativeDemoTimelineBuildV1(
            &config, sink_storage.bytes, sink_storage.size, index_scratch,
            (size_t)record_count, step_scratch, TEST_INDEX_CAPACITY,
            &summary) == WORR_NATIVE_DEMO_TIMELINE_MALFORMED);
  return true;
}

static bool test_argument_guards(void) {
  worr_native_demo_timeline_config_v1 config = make_config();
  worr_native_demo_timeline_summary_v1 summary;
  uint64_t record_count = 0;

  CHECK(record_and_open(&record_count));
  /* Null and malformed-config guards. */
  CHECK(Worr_NativeDemoTimelineBuildV1(
            &config, sink_storage.bytes, sink_storage.size, NULL,
            (size_t)record_count, step_scratch, TEST_INDEX_CAPACITY,
            &summary) == WORR_NATIVE_DEMO_TIMELINE_INVALID_ARGUMENT);
  config.flags = 0x1;
  CHECK(Worr_NativeDemoTimelineBuildV1(
            &config, sink_storage.bytes, sink_storage.size, index_scratch,
            (size_t)record_count, step_scratch, TEST_INDEX_CAPACITY,
            &summary) == WORR_NATIVE_DEMO_TIMELINE_INVALID_CONFIG);
  config.flags = 0;
  config.max_steps = 1;
  CHECK(Worr_NativeDemoTimelineBuildV1(
            &config, sink_storage.bytes, sink_storage.size, index_scratch,
            (size_t)record_count, step_scratch, TEST_INDEX_CAPACITY,
            &summary) == WORR_NATIVE_DEMO_TIMELINE_LIMIT);
  return true;
}

int main(void) {
  if (!test_plan())
    return 1;
  if (!test_capacity())
    return 1;
  if (!test_unanchored_event_fails())
    return 1;
  if (!test_reordered_index_fails())
    return 1;
  if (!test_truncated_index_fails())
    return 1;
  if (!test_argument_guards())
    return 1;
  puts("native_demo_timeline_test: all checks passed");
  return 0;
}
