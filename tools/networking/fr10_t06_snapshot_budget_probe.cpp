/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_codec.h"
#include "common/net/native_snapshot_receiver.h"
#include "common/net/native_snapshot_sender.h"
#include "common/net/snapshot_projection.h"
#include "common/net/snapshot_store.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kMaxEntities = 8192;
constexpr uint32_t kEntityCount = WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES;
constexpr uint32_t kAreaBytes = WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES;
constexpr uint32_t kEventCount = WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS;
constexpr uint32_t kHistorySlots = 64;
constexpr uint32_t kWarmupIterations = 24;
constexpr uint32_t kMeasuredIterations = 256;
constexpr uint64_t kMinimumFrameBudgetNs = UINT64_C(16666000);
constexpr uint64_t kSnapshotWorkBudgetNs = kMinimumFrameBudgetNs / 10;
constexpr uint64_t kCanonicalHistoryBudgetBytes = UINT64_C(16) * 1024 * 1024;
constexpr uint64_t kNativeOwnerBudgetBytes = UINT64_C(2) * 1024 * 1024;
constexpr uint32_t kExpectedEncodedBytes =
    WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
    WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES +
    kEntityCount * WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MAX_BYTES + kAreaBytes +
    kEventCount * WORR_NATIVE_CODEC_SNAPSHOT_EVENT_REF_BYTES;

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (0)

struct Fixture {
  worr_snapshot_store_v2 store;
  worr_snapshot_store_slot_v2 slots[1];
  worr_snapshot_entity_v2 stored_entities[kEntityCount];
  uint8_t stored_area[kAreaBytes];
  worr_snapshot_event_ref_v2 stored_events[kEventCount];

  worr_snapshot_v2 source_snapshot;
  worr_snapshot_player_v2 source_player;
  worr_snapshot_entity_v2 source_entities[kEntityCount];
  uint8_t source_area[kAreaBytes];
  worr_snapshot_event_ref_v2 source_events[kEventCount];

  uint8_t encoded[WORR_NATIVE_CODEC_MAX_ENCODED_BYTES];
  worr_snapshot_v2 decoded_snapshot;
  worr_snapshot_player_v2 decoded_player;
  worr_snapshot_entity_v2 decoded_entities[kEntityCount];
  uint8_t decoded_area[kAreaBytes];
  worr_snapshot_event_ref_v2 decoded_events[kEventCount];
  worr_snapshot_projection_view_v2 decoded_view;
  worr_snapshot_projection_hashes_v2 decoded_hashes;
};

Fixture fixture;

worr_snapshot_entity_generation_v2 make_generation(uint32_t index,
                                                   uint32_t generation) {
  worr_snapshot_entity_generation_v2 value{};
  value.identity.index = index;
  value.identity.generation = generation;
  value.provenance_flags = WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
  return value;
}

worr_snapshot_player_v2 make_player() {
  worr_snapshot_player_v2 player{};
  player.struct_size = sizeof(player);
  player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  player.controlled_entity = make_generation(1, 4);
  player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
  player.movement.struct_size = sizeof(player.movement);
  player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
  player.movement.origin[0] = 10.0f;
  player.movement.velocity[1] = -2.0f;
  player.movement.movement_flags = 5;
  player.movement.movement_time_ms = 17;
  player.movement.gravity = 800;
  player.movement.view_height = 22;
  player.movement.delta_angles[2] = 45.0f;
  player.view_angles[1] = 90.0f;
  player.view_offset[2] = 22.0f;
  player.gun_index = 7;
  player.gun_frame = 11;
  player.gun_skin = 2;
  player.gun_rate = 10;
  player.rdflags = 3;
  player.team_id = 2;
  player.fov = 100.0f;
  for (uint32_t index = 0; index < WORR_SNAPSHOT_STATS_CAPACITY; ++index)
    player.stats[index] = static_cast<int16_t>(index);
  return player;
}

worr_snapshot_entity_v2 make_entity(uint32_t entity_index) {
  worr_snapshot_entity_v2 entity{};
  entity.struct_size = sizeof(entity);
  entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  entity.generation = make_generation(entity_index, 4);
  entity.component_mask = WORR_SNAPSHOT_ENTITY_COMPONENTS_V2;
  for (uint32_t axis = 0; axis < 3; ++axis) {
    entity.origin[axis] = static_cast<float>(entity_index + axis) + 0.25f;
    entity.angles[axis] = static_cast<float>(axis) * 15.0f;
    entity.old_origin[axis] = entity.origin[axis] - 0.5f;
  }
  entity.model_index[0] = static_cast<uint16_t>((entity_index % 255) + 1);
  entity.model_index[1] = 4;
  entity.frame = static_cast<uint16_t>(entity_index % 128);
  entity.sound = 12;
  entity.skin = entity_index;
  entity.solid = 6;
  entity.effects = UINT64_C(0x100000002) + entity_index;
  entity.renderfx = 7;
  entity.alpha = 0.75f;
  entity.scale = 1.25f;
  entity.loop_volume = 0.5f;
  entity.loop_attenuation = 2.0f;
  entity.owner.index = entity_index == 1 ? 2 : 1;
  entity.owner.generation = 4;
  entity.old_frame = static_cast<int32_t>((entity_index - 1) % 128);
  entity.instance_bits = static_cast<uint8_t>(entity_index & 3u);
  return entity;
}

worr_snapshot_event_ref_v2 make_event(uint32_t index) {
  worr_snapshot_event_ref_v2 event{};
  event.struct_size = sizeof(event);
  event.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  event.provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
  event.carrier_ordinal = index;
  event.semantic_version = WORR_EVENT_MODEL_REVISION;
  event.authority_id.stream_epoch = 7;
  event.authority_id.sequence = index + 1;
  event.semantic_hash = UINT64_C(0x1122334400000000) + index + 1;
  return event;
}

void initialize_fixture() {
  std::memset(&fixture, 0, sizeof(fixture));
  fixture.source_snapshot.struct_size = sizeof(fixture.source_snapshot);
  fixture.source_snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  fixture.source_snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  fixture.source_snapshot.flags = WORR_SNAPSHOT_FLAG_KEYFRAME |
                                  WORR_SNAPSHOT_FLAG_COMPLETE |
                                  WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS |
                                  WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
  fixture.source_snapshot.snapshot_id.epoch = 9;
  fixture.source_snapshot.snapshot_id.sequence = 1;
  fixture.source_snapshot.server_tick = 1;
  fixture.source_snapshot.server_time_us = 16666;
  fixture.source_snapshot.controlled_entity = make_generation(1, 4);
  fixture.source_snapshot.discontinuity.flags =
      WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
      WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
  fixture.source_snapshot.discontinuity.reason =
      WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
  fixture.source_player = make_player();
  for (uint32_t index = 0; index < kEntityCount; ++index)
    fixture.source_entities[index] = make_entity(index + 1);
  for (uint32_t index = 0; index < kAreaBytes; ++index)
    fixture.source_area[index] = static_cast<uint8_t>(index * 37u + 11u);
  for (uint32_t index = 0; index < kEventCount; ++index)
    fixture.source_events[index] = make_event(index);
}

worr_snapshot_projection_view_v2 make_view(worr_snapshot_ref_v2 ref) {
  worr_snapshot_projection_view_v2 view{};
  view.struct_size = sizeof(view);
  view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
  view.snapshot = &fixture.slots[ref.slot].snapshot;
  view.player = &fixture.slots[ref.slot].player;
  view.entities = fixture.stored_entities;
  view.area_bytes = fixture.stored_area;
  view.event_refs = fixture.stored_events;
  view.entity_count = view.snapshot->entity_range.count;
  view.area_byte_count = view.snapshot->area_range.count;
  view.event_ref_count = view.snapshot->event_range.count;
  return view;
}

uint64_t percentile95(std::array<uint64_t, kMeasuredIterations> values) {
  std::sort(values.begin(), values.end());
  constexpr size_t index = (kMeasuredIterations * 95 + 99) / 100 - 1;
  return values[index];
}

int publish_and_encode(size_t *encoded_bytes,
                       worr_snapshot_projection_hashes_v2 *hashes) {
  worr_snapshot_store_publish_v2 publication{};
  worr_snapshot_ref_v2 ref{};
  publication.struct_size = sizeof(publication);
  publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
  publication.snapshot = &fixture.source_snapshot;
  publication.player = &fixture.source_player;
  publication.entities = fixture.source_entities;
  publication.area_bytes = fixture.source_area;
  publication.event_refs = fixture.source_events;
  publication.entity_count = kEntityCount;
  publication.area_byte_count = kAreaBytes;
  publication.event_ref_count = kEventCount;
  const worr_snapshot_store_result_v2 publish_result =
      Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref);
  if (publish_result != WORR_SNAPSHOT_STORE_OK) {
    std::fprintf(stderr, "snapshot publish failed: %u\n",
                 static_cast<unsigned>(publish_result));
    return 1;
  }
  const worr_snapshot_projection_view_v2 view = make_view(ref);
  if (!Worr_SnapshotProjectionHashesV2(&view, kMaxEntities, hashes)) {
    std::fprintf(stderr, "snapshot projection hash failed\n");
    return 1;
  }
  const worr_native_codec_result_v1 encode_result =
      Worr_NativeCodecSnapshotEncodeV1(&view, kMaxEntities, fixture.encoded,
                                       sizeof(fixture.encoded), encoded_bytes);
  if (encode_result != WORR_NATIVE_CODEC_OK) {
    std::fprintf(stderr, "snapshot encode failed: %u\n",
                 static_cast<unsigned>(encode_result));
    return 1;
  }
  return 0;
}

int decode(size_t encoded_bytes) {
  return Worr_NativeCodecSnapshotDecodeProjectionV1(
             fixture.encoded, encoded_bytes, kMaxEntities,
             &fixture.decoded_snapshot, &fixture.decoded_player,
             fixture.decoded_entities, kEntityCount, fixture.decoded_area,
             kAreaBytes, fixture.decoded_events, kEventCount,
             &fixture.decoded_view,
             &fixture.decoded_hashes) == WORR_NATIVE_CODEC_OK
             ? 0
             : 1;
}

void advance_snapshot() {
  fixture.source_snapshot.discontinuity.previous =
      fixture.source_snapshot.snapshot_id;
  fixture.source_snapshot.snapshot_id.sequence++;
  fixture.source_snapshot.server_tick++;
  fixture.source_snapshot.server_time_us += 16666;
  fixture.source_snapshot.discontinuity.flags =
      WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
  fixture.source_snapshot.discontinuity.reason =
      WORR_SNAPSHOT_DISCONTINUITY_REASON_FULL_SNAPSHOT;
  fixture.source_snapshot.discontinuity.server_tick_delta = 1;
}

} // namespace

int main() {
  using clock = std::chrono::steady_clock;
  std::array<uint64_t, kMeasuredIterations> build_encode_ns{};
  std::array<uint64_t, kMeasuredIterations> decode_ns{};
  worr_snapshot_projection_hashes_v2 hashes{};
  size_t encoded_bytes = 0;

  initialize_fixture();
  CHECK(Worr_SnapshotStoreInitV2(
            &fixture.store, fixture.slots, 1, fixture.stored_entities,
            kEntityCount, kEntityCount, fixture.stored_area, kAreaBytes,
            kAreaBytes, fixture.stored_events, kEventCount, kEventCount,
            kMaxEntities) == WORR_SNAPSHOT_STORE_OK);

  for (uint32_t iteration = 0; iteration < kWarmupIterations; ++iteration) {
    CHECK(publish_and_encode(&encoded_bytes, &hashes) == 0);
    CHECK(encoded_bytes == kExpectedEncodedBytes);
    CHECK(decode(encoded_bytes) == 0);
    advance_snapshot();
  }

  for (uint32_t iteration = 0; iteration < kMeasuredIterations; ++iteration) {
    const auto build_start = clock::now();
    CHECK(publish_and_encode(&encoded_bytes, &hashes) == 0);
    const auto build_end = clock::now();
    CHECK(encoded_bytes == kExpectedEncodedBytes);
    const auto decode_start = clock::now();
    CHECK(decode(encoded_bytes) == 0);
    const auto decode_end = clock::now();
    CHECK(hashes.endpoint_hash == fixture.decoded_hashes.endpoint_hash);
    CHECK(hashes.legacy_parity_hash ==
          fixture.decoded_hashes.legacy_parity_hash);
    build_encode_ns[iteration] = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(build_end -
                                                             build_start)
            .count());
    decode_ns[iteration] = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(decode_end -
                                                             decode_start)
            .count());
    advance_snapshot();
  }

  const uint64_t build_p95 = percentile95(build_encode_ns);
  const uint64_t decode_p95 = percentile95(decode_ns);
  const uint64_t history_bytes =
      sizeof(worr_snapshot_store_v2) +
      kHistorySlots * sizeof(worr_snapshot_store_slot_v2) +
      static_cast<uint64_t>(kHistorySlots) * kEntityCount *
          sizeof(worr_snapshot_entity_v2) +
      static_cast<uint64_t>(kHistorySlots) * kAreaBytes +
      static_cast<uint64_t>(kHistorySlots) * kEventCount *
          sizeof(worr_snapshot_event_ref_v2);
  const uint64_t native_owner_bytes = sizeof(worr_native_snapshot_sender_v1) +
                                      sizeof(worr_native_snapshot_receiver_v1);

  CHECK(build_p95 <= kSnapshotWorkBudgetNs);
  CHECK(decode_p95 <= kSnapshotWorkBudgetNs);
  CHECK(history_bytes <= kCanonicalHistoryBudgetBytes);
  CHECK(native_owner_bytes <= kNativeOwnerBudgetBytes);
  CHECK(encoded_bytes <= WORR_NATIVE_CODEC_MAX_ENCODED_BYTES);

  std::printf("{\"schema\":\"worr.networking.fr10-t06-snapshot-budget.v1\","
              "\"status\":\"ok\",\"entities\":%u,\"area_bytes\":%u,"
              "\"event_refs\":%u,\"history_slots\":%u,"
              "\"encoded_bytes\":%zu,\"encoded_capacity\":%u,"
              "\"build_encode_p95_ns\":%" PRIu64 ","
              "\"decode_p95_ns\":%" PRIu64 ","
              "\"work_budget_ns\":%" PRIu64 ","
              "\"canonical_history_bytes\":%" PRIu64 ","
              "\"canonical_history_budget_bytes\":%" PRIu64 ","
              "\"native_owner_bytes\":%" PRIu64 ","
              "\"native_owner_budget_bytes\":%" PRIu64 ","
              "\"endpoint_hash\":\"%016" PRIx64 "\"}\n",
              kEntityCount, kAreaBytes, kEventCount, kHistorySlots,
              encoded_bytes, WORR_NATIVE_CODEC_MAX_ENCODED_BYTES, build_p95,
              decode_p95, kSnapshotWorkBudgetNs, history_bytes,
              kCanonicalHistoryBudgetBytes, native_owner_bytes,
              kNativeOwnerBudgetBytes, hashes.endpoint_hash);
  return 0;
}
