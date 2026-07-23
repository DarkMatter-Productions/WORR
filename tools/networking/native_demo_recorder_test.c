/* Deterministic FR-10-T13 snapshot-only recorder transaction tests. */

#include "common/net/native_demo_recorder.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TEST_MAX_ENTITIES 64u
#define TEST_FILE_BYTES (1024u * 1024u)

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "native_demo_recorder_test:%d: %s\n", __LINE__,         \
              #condition);                                                     \
      return false;                                                            \
    }                                                                          \
  } while (0)

typedef struct memory_sink_s {
  uint8_t bytes[TEST_FILE_BYTES];
  size_t size;
  size_t calls;
  size_t short_on_call;
} memory_sink;

static uint8_t codec_scratch[WORR_NATIVE_CODEC_MAX_ENCODED_BYTES];
static uint8_t record_scratch[WORR_NATIVE_DEMO_MAX_RECORD_BYTES];
static memory_sink sink_storage;

static int64_t memory_write(void *opaque, const void *bytes,
                            size_t byte_count) {
  memory_sink *sink = (memory_sink *)opaque;
  size_t accepted = byte_count;

  ++sink->calls;
  if (sink->short_on_call == sink->calls && accepted != 0)
    --accepted;
  if (accepted > sizeof(sink->bytes) - sink->size)
    return -1;
  memcpy(sink->bytes + sink->size, bytes, accepted);
  sink->size += accepted;
  return (int64_t)accepted;
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
  snapshot.discontinuity.flags =
      WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
  if (sequence == 1) {
    snapshot.discontinuity.flags |= WORR_SNAPSHOT_DISCONTINUITY_INITIAL;
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
  } else {
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_FULL_SNAPSHOT;
    snapshot.discontinuity.previous.epoch = 9;
    snapshot.discontinuity.previous.sequence = previous_sequence;
    snapshot.discontinuity.server_tick_delta = sequence - previous_sequence;
    if (sequence - previous_sequence > 1) {
      snapshot.discontinuity.flags |=
          WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP;
      snapshot.discontinuity.skipped_sequences =
          sequence - previous_sequence - 1;
    }
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
  return Worr_SnapshotProjectionHashesV2(
      &fixture->view, TEST_MAX_ENTITIES, &fixture->hashes);
}

static worr_native_demo_recorder_result_v1 begin_recorder(
    worr_native_demo_recorder_state_v1 *state, memory_sink *sink,
    uint64_t max_stream_bytes) {
  worr_native_demo_recorder_config_v1 config;
  worr_native_demo_recorder_storage_v1 storage;
  worr_native_demo_recorder_sink_v1 output;

  memset(&config, 0, sizeof(config));
  config.struct_size = sizeof(config);
  config.schema_version = WORR_NATIVE_DEMO_RECORDER_ABI_VERSION;
  config.capability_mask = WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK;
  config.transport_epoch = 17;
  config.timeline_epoch = 9;
  config.max_entities = TEST_MAX_ENTITIES;
  config.created_time_us = UINT64_C(1234567);
  config.max_stream_bytes = max_stream_bytes;
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
  output.opaque = sink;
  output.WriteExact = memory_write;
  return Worr_NativeDemoRecorderBeginV1(state, &config, &storage, &output);
}

static bool test_success_gap_dedup_and_scan(void) {
  worr_native_demo_recorder_state_v1 state;
  worr_native_demo_recorder_status_v1 status;
  worr_native_demo_scan_config_v1 scan_config;
  worr_native_demo_scan_info_v1 scan;
  snapshot_fixture first;
  snapshot_fixture second;
  snapshot_fixture gap;
  worr_snapshot_ref_v2 reused_ref;
  size_t before_duplicate;

  memset(&sink_storage, 0, sizeof(sink_storage));
  memset(&state, 0, sizeof(state));
  CHECK(build_snapshot(&first, 1, 0, 1000, 1));
  CHECK(build_snapshot(&second, 2, 1, 2000, 1));
  CHECK(build_snapshot(&gap, 4, 2, 4000, 1));
  CHECK(begin_recorder(&state, &sink_storage, TEST_FILE_BYTES) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &first.hashes, first.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  before_duplicate = sink_storage.size;
  reused_ref = first.ref;
  reused_ref.generation += 77;
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &first.hashes, reused_ref) ==
        WORR_NATIVE_DEMO_RECORDER_DUPLICATE);
  CHECK(sink_storage.size == before_duplicate);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &second.view, &second.hashes, second.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &gap.view, &gap.hashes, gap.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderGetStatusV1(&state, &status));
  CHECK(status.snapshot_count == 3);
  CHECK(status.duplicate_count == 1);
  CHECK(status.stream_bytes == sink_storage.size);
  memset(&scan_config, 0, sizeof(scan_config));
  scan_config.struct_size = sizeof(scan_config);
  scan_config.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  scan_config.max_records = 4;
  scan_config.max_entities = TEST_MAX_ENTITIES;
  CHECK(Worr_NativeDemoStreamScanV1(
            &scan_config, sink_storage.bytes, sink_storage.size, NULL, 0,
            &scan) == WORR_NATIVE_DEMO_OK);
  CHECK(scan.record_count == 3 && scan.snapshot_count == 3);
  CHECK(scan.order.snapshot_epoch == 9 &&
        scan.order.snapshot_sequence == 4);
  CHECK(Worr_NativeDemoRecorderStopV1(&state) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &gap.view, &gap.hashes, gap.ref) ==
        WORR_NATIVE_DEMO_RECORDER_NOT_ACTIVE);
  return true;
}

static bool test_conflicting_duplicate_is_transactional(void) {
  worr_native_demo_recorder_state_v1 state;
  worr_native_demo_recorder_status_v1 before;
  worr_native_demo_recorder_status_v1 after;
  snapshot_fixture first;
  worr_snapshot_projection_hashes_v2 conflicting;
  size_t sink_before;

  memset(&sink_storage, 0, sizeof(sink_storage));
  CHECK(build_snapshot(&first, 1, 0, 1000, 1));
  CHECK(begin_recorder(&state, &sink_storage, TEST_FILE_BYTES) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &first.hashes, first.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderGetStatusV1(&state, &before));
  sink_before = sink_storage.size;
  conflicting = first.hashes;
  conflicting.endpoint_hash ^= UINT64_C(1);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &conflicting, first.ref) ==
        WORR_NATIVE_DEMO_RECORDER_ORDER);
  CHECK(Worr_NativeDemoRecorderGetStatusV1(&state, &after));
  CHECK(sink_storage.size == sink_before);
  CHECK(after.stream_bytes == before.stream_bytes);
  CHECK(after.snapshot_count == before.snapshot_count);
  CHECK((after.state_flags & WORR_NATIVE_DEMO_RECORDER_FAILED) != 0);
  return true;
}

static bool test_regression_is_transactional(void) {
  worr_native_demo_recorder_state_v1 state;
  worr_native_demo_recorder_status_v1 before;
  worr_native_demo_recorder_status_v1 after;
  snapshot_fixture first;
  snapshot_fixture second;
  size_t sink_before;

  memset(&sink_storage, 0, sizeof(sink_storage));
  CHECK(build_snapshot(&first, 1, 0, 1000, 1));
  CHECK(build_snapshot(&second, 2, 1, 2000, 1));
  CHECK(begin_recorder(&state, &sink_storage, TEST_FILE_BYTES) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &first.hashes, first.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &second.view, &second.hashes, second.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderGetStatusV1(&state, &before));
  sink_before = sink_storage.size;
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &first.hashes, first.ref) ==
        WORR_NATIVE_DEMO_RECORDER_ORDER);
  CHECK(Worr_NativeDemoRecorderGetStatusV1(&state, &after));
  CHECK(sink_storage.size == sink_before);
  CHECK(after.stream_bytes == before.stream_bytes);
  CHECK(after.snapshot_count == before.snapshot_count);
  return true;
}

static bool test_short_writes_do_not_commit(void) {
  worr_native_demo_recorder_state_v1 state;
  worr_native_demo_recorder_status_v1 status;
  snapshot_fixture first;

  CHECK(build_snapshot(&first, 1, 0, 1000, 1));
  memset(&sink_storage, 0, sizeof(sink_storage));
  sink_storage.short_on_call = 1;
  CHECK(begin_recorder(&state, &sink_storage, TEST_FILE_BYTES) ==
        WORR_NATIVE_DEMO_RECORDER_OUTPUT);
  CHECK(Worr_NativeDemoRecorderGetStatusV1(&state, &status));
  CHECK(status.stream_bytes == 0 && status.snapshot_count == 0);
  CHECK((status.state_flags & WORR_NATIVE_DEMO_RECORDER_FAILED) != 0);

  memset(&sink_storage, 0, sizeof(sink_storage));
  sink_storage.short_on_call = 2;
  CHECK(begin_recorder(&state, &sink_storage, TEST_FILE_BYTES) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &first.hashes, first.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OUTPUT);
  CHECK(Worr_NativeDemoRecorderGetStatusV1(&state, &status));
  CHECK(status.stream_bytes == WORR_NATIVE_DEMO_WIRE_HEADER_BYTES);
  CHECK(status.snapshot_count == 0);
  CHECK((status.state_flags & WORR_NATIVE_DEMO_RECORDER_FAILED) != 0);
  return true;
}

static bool test_epoch_pov_and_limit_fail_closed(void) {
  worr_native_demo_recorder_state_v1 state;
  worr_native_demo_recorder_status_v1 status;
  snapshot_fixture first;
  snapshot_fixture second;
  snapshot_fixture other_pov;
  snapshot_fixture time_rewind;
  worr_snapshot_v2 changed_epoch;

  CHECK(build_snapshot(&first, 1, 0, 1000, 1));
  CHECK(build_snapshot(&second, 2, 1, 2000, 1));
  CHECK(build_snapshot(&other_pov, 2, 1, 2000, 2));
  CHECK(build_snapshot(&time_rewind, 2, 1, 500, 1));

  memset(&sink_storage, 0, sizeof(sink_storage));
  CHECK(begin_recorder(&state, &sink_storage, TEST_FILE_BYTES) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &first.hashes, first.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &other_pov.view, &other_pov.hashes, other_pov.ref) ==
        WORR_NATIVE_DEMO_RECORDER_CONTROLLED_ENTITY);

  memset(&sink_storage, 0, sizeof(sink_storage));
  CHECK(begin_recorder(&state, &sink_storage, TEST_FILE_BYTES) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &first.hashes, first.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &time_rewind.view, &time_rewind.hashes,
            time_rewind.ref) == WORR_NATIVE_DEMO_RECORDER_ORDER);

  memset(&sink_storage, 0, sizeof(sink_storage));
  CHECK(begin_recorder(&state, &sink_storage, TEST_FILE_BYTES) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  changed_epoch = *first.view.snapshot;
  changed_epoch.snapshot_id.epoch = 10;
  first.view.snapshot = &changed_epoch;
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &first.hashes, first.ref) ==
        WORR_NATIVE_DEMO_RECORDER_TIMELINE_EPOCH);

  CHECK(build_snapshot(&first, 1, 0, 1000, 1));
  memset(&sink_storage, 0, sizeof(sink_storage));
  CHECK(begin_recorder(&state, &sink_storage, TEST_FILE_BYTES) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &first.view, &first.hashes, first.ref) ==
        WORR_NATIVE_DEMO_RECORDER_OK);
  CHECK(Worr_NativeDemoRecorderGetStatusV1(&state, &status));
  state.config.max_stream_bytes = status.stream_bytes;
  CHECK(Worr_NativeDemoRecorderObserveSnapshotV1(
            &state, &second.view, &second.hashes, second.ref) ==
        WORR_NATIVE_DEMO_RECORDER_LIMIT);
  CHECK(Worr_NativeDemoRecorderGetStatusV1(&state, &status));
  CHECK(status.snapshot_count == 1);
  CHECK(status.stream_bytes == sink_storage.size);
  return true;
}

int main(void) {
  if (!test_success_gap_dedup_and_scan() ||
      !test_conflicting_duplicate_is_transactional() ||
      !test_regression_is_transactional() ||
      !test_short_writes_do_not_commit() ||
      !test_epoch_pov_and_limit_fail_closed()) {
    return 1;
  }
  puts("native_demo_recorder_test: all checks passed");
  return 0;
}
