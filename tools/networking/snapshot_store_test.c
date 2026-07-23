/*
 * Standalone FR-10-T06 Stage A behavioral/fault test.  This intentionally
 * links only the transport-neutral ABI/store and T05 event ABI primitives.
 */

#include "common/net/snapshot_store.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_SLOTS 2u
#define TEST_ENTITIES_PER_SLOT 4u
#define TEST_AREA_PER_SLOT 8u
#define TEST_EVENTS_PER_SLOT 4u
#define TEST_MAX_ENTITIES 64u

#define EXPECTED_PLAYER_HASH UINT64_C(0x77c935128cf74466)
#define EXPECTED_ENTITY_HASH UINT64_C(0xe9490b844d2a5ef3)
#define EXPECTED_ENTITY_LIST_HASH UINT64_C(0xd982aea5f5df9450)
#define EXPECTED_AREA_HASH UINT64_C(0xeb3ec84d51df6b7a)
#define EXPECTED_EVENT_HASH UINT64_C(0x906b42c00fab2edf)
#define EXPECTED_SNAPSHOT_HASH UINT64_C(0xb2b60ae5a21f47ce)
#define EXPECTED_SOAK_CHAIN UINT64_C(0x6f3a2ad4178c5259)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "snapshot_store_test:%d: %s\n", __LINE__,      \
                    #condition);                                             \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_GOLDEN(actual, expected)                                       \
    do {                                                                      \
        if ((actual) != (expected)) {                                         \
            fprintf(stderr,                                                   \
                    "snapshot_store_test:%d: %s=%016llx expected=%016llx\n", \
                    __LINE__, #actual,                                        \
                    (unsigned long long)(actual),                             \
                    (unsigned long long)(expected));                          \
            return false;                                                     \
        }                                                                     \
    } while (0)

typedef struct test_fixture_s {
    worr_snapshot_store_v2 store;
    worr_snapshot_store_slot_v2 slots[TEST_SLOTS];
    worr_snapshot_entity_v2
        entities[TEST_SLOTS * TEST_ENTITIES_PER_SLOT];
    uint8_t area[TEST_SLOTS * TEST_AREA_PER_SLOT];
    worr_snapshot_event_ref_v2
        events[TEST_SLOTS * TEST_EVENTS_PER_SLOT];
} test_fixture;

typedef struct fixture_backup_s {
    worr_snapshot_store_v2 store;
    worr_snapshot_store_slot_v2 slots[TEST_SLOTS];
    worr_snapshot_entity_v2
        entities[TEST_SLOTS * TEST_ENTITIES_PER_SLOT];
    uint8_t area[TEST_SLOTS * TEST_AREA_PER_SLOT];
    worr_snapshot_event_ref_v2
        events[TEST_SLOTS * TEST_EVENTS_PER_SLOT];
} fixture_backup;

static worr_snapshot_entity_generation_v2 generation(uint32_t index,
                                                      uint32_t value,
                                                      bool authoritative)
{
    worr_snapshot_entity_generation_v2 result;
    memset(&result, 0, sizeof(result));
    result.identity.index = index;
    result.identity.generation = value;
    result.provenance_flags =
        authoritative ? WORR_SNAPSHOT_GENERATION_AUTHORITATIVE
                      : WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    return result;
}

static worr_snapshot_entity_v2 make_entity(uint32_t index,
                                           uint32_t generation_value,
                                           bool authoritative,
                                           uint32_t seed,
                                           int fill)
{
    worr_snapshot_entity_v2 entity;
    unsigned int i;

    memset(&entity, fill, sizeof(entity));
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.flags = 0;
    entity.generation = generation(index, generation_value, authoritative);
    entity.component_mask = WORR_SNAPSHOT_ENTITY_COMPONENTS_V2;
    for (i = 0; i < 3; ++i) {
        entity.origin[i] = (float)(seed + i) * 0.25f;
        entity.angles[i] = (float)(seed + i * 3u) * 1.5f;
        entity.old_origin[i] = entity.origin[i] - 0.125f;
    }
    for (i = 0; i < 4; ++i)
        entity.model_index[i] = (uint16_t)(seed + i + 1u);
    entity.frame = (uint16_t)(seed * 2u);
    entity.sound = (uint16_t)(seed + 20u);
    entity.skin = seed + 30u;
    entity.solid = seed + 40u;
    entity.effects = UINT64_C(0x100000000) + seed;
    entity.renderfx = seed + 50u;
    entity.alpha = 0.75f;
    entity.scale = 1.25f;
    entity.loop_volume = 0.5f;
    entity.loop_attenuation = 3.0f;
    entity.owner.index = 1;
    entity.owner.generation = 4;
    entity.old_frame = (int32_t)seed - 2;
    entity.instance_bits = (uint8_t)(seed & UINT32_C(0xff));
    memset(entity.reserved0, 0, sizeof(entity.reserved0));
    return entity;
}

static worr_snapshot_player_v2 make_player(uint32_t entity_index,
                                           uint32_t generation_value,
                                           bool authoritative,
                                           uint32_t seed,
                                           int fill)
{
    worr_snapshot_player_v2 player;
    unsigned int i;

    memset(&player, fill, sizeof(player));
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.flags = 0;
    player.controlled_entity =
        generation(entity_index, generation_value, authoritative);
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    player.movement.movement_type = 0;
    for (i = 0; i < 3; ++i) {
        player.movement.origin[i] = (float)(seed + i) * 0.5f;
        player.movement.velocity[i] = (float)(seed + i) * -0.25f;
        player.movement.delta_angles[i] = (float)(seed + i) * 2.0f;
        player.view_angles[i] = (float)(seed + i) * 3.0f;
        player.view_offset[i] = (float)i + 0.125f;
        player.kick_angles[i] = (float)i * -0.25f;
        player.gun_angles[i] = (float)i * 0.5f;
        player.gun_offset[i] = (float)i * 0.75f;
    }
    player.movement.movement_flags = UINT16_C(0x0005);
    player.movement.movement_time_ms = UINT16_C(17);
    player.movement.gravity = INT16_C(800);
    player.movement.view_height = INT8_C(22);
    player.movement.reserved0 = 0;
    for (i = 0; i < 4; ++i) {
        player.screen_blend[i] = (float)i * 0.1f;
        player.damage_blend[i] = (float)i * 0.05f;
    }
    player.gun_index = UINT16_C(7);
    player.gun_frame = UINT16_C(11);
    player.gun_skin = UINT8_C(2);
    player.gun_rate = UINT8_C(10);
    player.rdflags = UINT8_C(3);
    player.team_id = UINT8_C(2);
    player.fov = 100.0f;
    for (i = 0; i < WORR_SNAPSHOT_STATS_CAPACITY; ++i)
        player.stats[i] = (int16_t)((int)i - 20);
    return player;
}

static worr_snapshot_v2 make_snapshot(uint32_t sequence,
                                      bool authoritative,
                                      uint32_t controlled_generation)
{
    worr_snapshot_v2 snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    snapshot.flags = WORR_SNAPSHOT_FLAG_COMPLETE |
                     WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
    if (authoritative)
        snapshot.flags |= WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS;
    snapshot.snapshot_id.epoch = 1;
    snapshot.snapshot_id.sequence = sequence;
    snapshot.server_tick = sequence * 2u;
    snapshot.server_time_us = (uint64_t)sequence * UINT64_C(16666);
    snapshot.controlled_entity =
        generation(1, controlled_generation, authoritative);
    if (sequence == 1) {
        snapshot.flags |= WORR_SNAPSHOT_FLAG_KEYFRAME;
        snapshot.discontinuity.flags =
            WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
            WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
        snapshot.discontinuity.reason =
            WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
    } else {
        snapshot.base_id.epoch = 1;
        snapshot.base_id.sequence = sequence - 1u;
        snapshot.discontinuity.previous.epoch = 1;
        snapshot.discontinuity.previous.sequence = sequence - 1u;
        snapshot.discontinuity.server_tick_delta = 2;
    }
    return snapshot;
}

static worr_snapshot_event_ref_v2 make_event(uint32_t sequence,
                                             uint64_t semantic_hash,
                                             uint32_t carrier_ordinal)
{
    worr_snapshot_event_ref_v2 event;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    event.provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
    event.carrier_ordinal = carrier_ordinal;
    event.semantic_version = WORR_EVENT_MODEL_REVISION;
    event.authority_id.stream_epoch = 7;
    event.authority_id.sequence = sequence;
    event.semantic_hash = semantic_hash;
    return event;
}

static bool init_fixture(test_fixture *fixture)
{
    memset(fixture, 0xa5, sizeof(*fixture));
    return Worr_SnapshotStoreInitV2(
               &fixture->store, fixture->slots, TEST_SLOTS,
               fixture->entities,
               TEST_SLOTS * TEST_ENTITIES_PER_SLOT,
               TEST_ENTITIES_PER_SLOT, fixture->area,
               TEST_SLOTS * TEST_AREA_PER_SLOT, TEST_AREA_PER_SLOT,
               fixture->events, TEST_SLOTS * TEST_EVENTS_PER_SLOT,
               TEST_EVENTS_PER_SLOT, TEST_MAX_ENTITIES) ==
           WORR_SNAPSHOT_STORE_OK;
}

static worr_snapshot_store_publish_v2 make_publication(
    const worr_snapshot_v2 *snapshot,
    const worr_snapshot_player_v2 *player,
    const worr_snapshot_entity_v2 *entities,
    uint32_t entity_count,
    const uint8_t *area,
    uint32_t area_count,
    const worr_snapshot_event_ref_v2 *events,
    uint32_t event_count)
{
    worr_snapshot_store_publish_v2 publication;
    memset(&publication, 0, sizeof(publication));
    publication.struct_size = sizeof(publication);
    publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
    publication.snapshot = snapshot;
    publication.player = player;
    publication.entities = entities;
    publication.area_bytes = area;
    publication.event_refs = events;
    publication.entity_count = entity_count;
    publication.area_byte_count = area_count;
    publication.event_ref_count = event_count;
    return publication;
}

static void capture_fixture(const test_fixture *fixture,
                            fixture_backup *backup)
{
    backup->store = fixture->store;
    memcpy(backup->slots, fixture->slots, sizeof(backup->slots));
    memcpy(backup->entities, fixture->entities, sizeof(backup->entities));
    memcpy(backup->area, fixture->area, sizeof(backup->area));
    memcpy(backup->events, fixture->events, sizeof(backup->events));
}

static bool fixture_unchanged(const test_fixture *fixture,
                              const fixture_backup *backup)
{
    return memcmp(&fixture->store, &backup->store, sizeof(backup->store)) == 0 &&
           memcmp(fixture->slots, backup->slots, sizeof(backup->slots)) == 0 &&
           memcmp(fixture->entities, backup->entities,
                  sizeof(backup->entities)) == 0 &&
           memcmp(fixture->area, backup->area, sizeof(backup->area)) == 0 &&
           memcmp(fixture->events, backup->events,
                  sizeof(backup->events)) == 0;
}

static bool test_ids_and_generations(void)
{
    worr_snapshot_id_v2 id = {0, 0};
    worr_snapshot_id_v2 next = {99, 99};
    worr_snapshot_entity_generation_v2 value;

    CHECK(Worr_SnapshotIdValidV2(id, true));
    CHECK(!Worr_SnapshotIdValidV2(id, false));
    CHECK(Worr_SnapshotIdNextV2(id, &next));
    CHECK(next.epoch == 1 && next.sequence == 1);
    id.epoch = 3;
    id.sequence = UINT32_MAX;
    CHECK(Worr_SnapshotIdNextV2(id, &next));
    CHECK(next.epoch == 4 && next.sequence == 1);
    id.epoch = UINT32_MAX;
    id.sequence = UINT32_MAX;
    next.epoch = 99;
    next.sequence = 99;
    CHECK(!Worr_SnapshotIdNextV2(id, &next));
    CHECK(next.epoch == 99 && next.sequence == 99);

    value = generation(2, 1, true);
    CHECK(Worr_SnapshotGenerationValidV2(value, TEST_MAX_ENTITIES, false));
    value.provenance_flags |= WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    CHECK(!Worr_SnapshotGenerationValidV2(value, TEST_MAX_ENTITIES, false));
    value = generation(2, 1, false);
    CHECK(Worr_SnapshotGenerationValidV2(value, TEST_MAX_ENTITIES, false));
    value.identity.generation = 0;
    CHECK(!Worr_SnapshotGenerationValidV2(value, TEST_MAX_ENTITIES, false));
    memset(&value, 0, sizeof(value));
    value.identity.index = WORR_EVENT_NO_ENTITY;
    CHECK(Worr_SnapshotGenerationValidV2(value, TEST_MAX_ENTITIES, true));
    CHECK(!Worr_SnapshotGenerationValidV2(value, TEST_MAX_ENTITIES, false));
    return true;
}

static bool test_record_hash_contract(void)
{
    worr_snapshot_player_v2 player_a = make_player(1, 4, true, 10, 0);
    worr_snapshot_player_v2 player_b = make_player(1, 4, true, 10, 0x5a);
    worr_snapshot_entity_v2 entity_a = make_entity(2, 7, true, 6, 0);
    worr_snapshot_entity_v2 entity_b = make_entity(2, 7, true, 6, 0x5a);
    worr_snapshot_entity_v2 entities[2];
    worr_snapshot_event_ref_v2 events[2];
    const uint8_t area[3] = {UINT8_C(0x01), UINT8_C(0x80), UINT8_C(0x7f)};
    uint64_t player_hash_a = 0;
    uint64_t player_hash_b = 0;
    uint64_t entity_hash_a = 0;
    uint64_t entity_hash_b = 0;
    uint64_t list_hash = 0;
    uint64_t area_hash = 0;
    uint64_t event_hash = 0;
    uint64_t sentinel;

    CHECK(Worr_SnapshotPlayerHashV2(&player_a, TEST_MAX_ENTITIES,
                                    &player_hash_a));
    CHECK(Worr_SnapshotPlayerHashV2(&player_b, TEST_MAX_ENTITIES,
                                    &player_hash_b));
    CHECK(player_hash_a == player_hash_b);
    CHECK_GOLDEN(player_hash_a, EXPECTED_PLAYER_HASH);
    CHECK(Worr_SnapshotEntityHashV2(&entity_a, TEST_MAX_ENTITIES,
                                    &entity_hash_a));
    CHECK(Worr_SnapshotEntityHashV2(&entity_b, TEST_MAX_ENTITIES,
                                    &entity_hash_b));
    CHECK(entity_hash_a == entity_hash_b);
    CHECK_GOLDEN(entity_hash_a, EXPECTED_ENTITY_HASH);

    entity_b.origin[0] = -0.0f;
    entity_a.origin[0] = 0.0f;
    CHECK(Worr_SnapshotEntityHashV2(&entity_a, TEST_MAX_ENTITIES,
                                    &entity_hash_a));
    CHECK(Worr_SnapshotEntityHashV2(&entity_b, TEST_MAX_ENTITIES,
                                    &entity_hash_b));
    CHECK(entity_hash_a == entity_hash_b);
    entity_b.origin[1] += 1.0f;
    CHECK(Worr_SnapshotEntityHashV2(&entity_b, TEST_MAX_ENTITIES,
                                    &entity_hash_b));
    CHECK(entity_hash_a != entity_hash_b);

    entity_a = make_entity(2, 7, true, 6, 0);
    entity_a.origin[0] = NAN;
    sentinel = UINT64_C(0x1122334455667788);
    CHECK(!Worr_SnapshotEntityHashV2(&entity_a, TEST_MAX_ENTITIES,
                                     &sentinel));
    CHECK(sentinel == UINT64_C(0x1122334455667788));
    entity_a = make_entity(2, 7, true, 6, 0);
    entity_a.component_mask &= ~WORR_SNAPSHOT_ENTITY_TRANSFORM;
    CHECK(!Worr_SnapshotEntityValidateV2(&entity_a, TEST_MAX_ENTITIES));
    memset(entity_a.origin, 0, sizeof(entity_a.origin));
    memset(entity_a.angles, 0, sizeof(entity_a.angles));
    CHECK(!Worr_SnapshotEntityValidateV2(&entity_a, TEST_MAX_ENTITIES));
    entity_a = make_entity(2, 7, true, 6, 0);
    entity_a.component_mask &= ~WORR_SNAPSHOT_ENTITY_LOOP_SOUND;
    CHECK(!Worr_SnapshotEntityValidateV2(&entity_a, TEST_MAX_ENTITIES));

    player_a.movement.schema_version += 1u;
    CHECK(!Worr_SnapshotPlayerValidateV2(&player_a, TEST_MAX_ENTITIES));
    player_a = make_player(1, 4, true, 10, 0);
    player_a.movement.movement_flags = UINT16_C(0x2000);
    CHECK(!Worr_SnapshotPlayerValidateV2(&player_a, TEST_MAX_ENTITIES));
    player_a = make_player(1, 4, true, 10, 0);
    player_a.component_mask &= ~WORR_SNAPSHOT_PLAYER_STATS;
    CHECK(!Worr_SnapshotPlayerValidateV2(&player_a, TEST_MAX_ENTITIES));

    entities[0] = make_entity(2, 1, true, 3, 0);
    entities[1] = make_entity(5, 1, true, 4, 0);
    CHECK(Worr_SnapshotEntityListHashV2(entities, 2, TEST_MAX_ENTITIES,
                                        &list_hash));
    CHECK_GOLDEN(list_hash, EXPECTED_ENTITY_LIST_HASH);
    entities[1].generation.identity.index = 2;
    CHECK(!Worr_SnapshotEntityListHashV2(entities, 2, TEST_MAX_ENTITIES,
                                         &sentinel));

    CHECK(Worr_SnapshotAreaHashV2(area, 3, &area_hash));
    CHECK_GOLDEN(area_hash, EXPECTED_AREA_HASH);
    events[0] = make_event(10, UINT64_C(0x1234), 0);
    events[1] = make_event(11, UINT64_C(0x5678), 1);
    CHECK(Worr_SnapshotEventRefsHashV2(events, 2, &event_hash));
    CHECK_GOLDEN(event_hash, EXPECTED_EVENT_HASH);
    events[1].authority_id.sequence = 10;
    CHECK(!Worr_SnapshotEventRefsHashV2(events, 2, &sentinel));
    return true;
}

static bool publish_sample(test_fixture *fixture,
                           uint32_t sequence,
                           worr_snapshot_ref_v2 *ref_out,
                           worr_snapshot_v2 *metadata_out)
{
    worr_snapshot_v2 snapshot = make_snapshot(sequence, true, 4);
    worr_snapshot_player_v2 player = make_player(1, 4, true, 10, 0);
    worr_snapshot_entity_v2 entities[2];
    worr_snapshot_event_ref_v2 events[2];
    uint8_t area[3] = {UINT8_C(0x01), UINT8_C(0x80), UINT8_C(0x7f)};
    worr_snapshot_store_publish_v2 publication;

    entities[0] = make_entity(2, 1, true, 3, 0);
    entities[1] = make_entity(5, 1, true, 4, 0);
    events[0] = make_event(10, UINT64_C(0x1234), 0);
    events[1] = make_event(11, UINT64_C(0x5678), 1);
    publication = make_publication(&snapshot, &player, entities, 2,
                                   area, 3, events, 2);
    if (Worr_SnapshotStorePublishV2(&fixture->store, &publication,
                                    ref_out) != WORR_SNAPSHOT_STORE_OK) {
        return false;
    }
    if (metadata_out &&
        Worr_SnapshotStoreCopySnapshotV2(&fixture->store, *ref_out,
                                         metadata_out) !=
            WORR_SNAPSHOT_STORE_OK) {
        return false;
    }
    return true;
}

static bool test_store_publish_copy_and_stale_refs(void)
{
    test_fixture fixture;
    test_fixture independent;
    worr_snapshot_ref_v2 ref1;
    worr_snapshot_ref_v2 ref2;
    worr_snapshot_ref_v2 ref3;
    worr_snapshot_ref_v2 independent_ref;
    worr_snapshot_v2 header1;
    worr_snapshot_v2 header3;
    worr_snapshot_v2 independent_header;
    worr_snapshot_player_v2 player;
    worr_snapshot_entity_v2 entities[2];
    worr_snapshot_event_ref_v2 events[2];
    uint8_t area[3];
    uint32_t count = UINT32_C(0xdeadbeef);
    worr_snapshot_store_stats_v2 stats;
    worr_snapshot_entity_v2 sentinel_entities[2];
    uint32_t i;
    void *slot_address;
    void *entity_address;

    CHECK(init_fixture(&fixture));
    slot_address = fixture.store.slots;
    entity_address = fixture.store.entities;
    CHECK(publish_sample(&fixture, 1, &ref1, &header1));
    CHECK(ref1.slot == 0 && ref1.generation == 1);
    CHECK(fixture.store.slots == slot_address);
    CHECK(fixture.store.entities == entity_address);
    CHECK(Worr_SnapshotStoreRefValidV2(&fixture.store, ref1));
    CHECK(header1.entity_range.first_serial == 1 &&
          header1.entity_range.count == 2);
    CHECK(header1.area_range.first_serial == 1 &&
          header1.area_range.count == 3);
    CHECK(header1.event_range.first_ref_serial == 1 &&
          header1.event_range.count == 2);
    CHECK(header1.event_range.first_authority_id.sequence == 10);
    CHECK(header1.event_range.one_past_authority_id.sequence == 12);
    CHECK(header1.event_range.flags ==
          (WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY |
           WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER));
    CHECK_GOLDEN(header1.snapshot_hash, EXPECTED_SNAPSHOT_HASH);
    CHECK(Worr_SnapshotStoreCopyPlayerV2(&fixture.store, ref1, &player) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(player.controlled_entity.identity.index == 1);
    CHECK(Worr_SnapshotStoreCopyEntitiesV2(&fixture.store, ref1, entities,
                                           2, &count) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(count == 2 && entities[0].generation.identity.index == 2 &&
          entities[1].generation.identity.index == 5);
    CHECK(Worr_SnapshotStoreCopyAreaV2(&fixture.store, ref1, area, 3,
                                       &count) == WORR_SNAPSHOT_STORE_OK);
    CHECK(count == 3 && area[0] == UINT8_C(0x01) &&
          area[1] == UINT8_C(0x80) && area[2] == UINT8_C(0x7f));
    CHECK(Worr_SnapshotStoreCopyEventRefsV2(&fixture.store, ref1, events, 2,
                                            &count) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(count == 2 && events[1].authority_id.sequence == 11);

    memset(sentinel_entities, 0x6d, sizeof(sentinel_entities));
    count = UINT32_C(0xdeadbeef);
    CHECK(Worr_SnapshotStoreCopyEntitiesV2(
              &fixture.store, ref1, sentinel_entities, 1, &count) ==
          WORR_SNAPSHOT_STORE_BUFFER_TOO_SMALL);
    CHECK(count == UINT32_C(0xdeadbeef));
    for (i = 0; i < sizeof(sentinel_entities); ++i)
        CHECK(((const uint8_t *)sentinel_entities)[i] == UINT8_C(0x6d));

    CHECK(Worr_SnapshotStoreGetStatsV2(&fixture.store, &stats) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(stats.occupied == 1 && stats.occupied_high_water == 1 &&
          stats.entity_high_water == 2 && stats.area_high_water == 3 &&
          stats.event_high_water == 2 && stats.publish_count == 1);

    CHECK(publish_sample(&fixture, 2, &ref2, NULL));
    CHECK(ref2.slot == 1 && ref2.generation == 1);
    CHECK(publish_sample(&fixture, 3, &ref3, &header3));
    CHECK(ref3.slot == 0 && ref3.generation == 2);
    CHECK(!Worr_SnapshotStoreRefValidV2(&fixture.store, ref1));
    memset(&player, 0x7b, sizeof(player));
    CHECK(Worr_SnapshotStoreCopyPlayerV2(&fixture.store, ref1, &player) ==
          WORR_SNAPSHOT_STORE_STALE_REF);
    for (i = 0; i < sizeof(player); ++i)
        CHECK(((const uint8_t *)&player)[i] == UINT8_C(0x7b));

    CHECK(init_fixture(&independent));
    CHECK(publish_sample(&independent, 3, &independent_ref,
                         &independent_header));
    CHECK(header3.entity_range.first_serial !=
          independent_header.entity_range.first_serial);
    CHECK(header3.snapshot_hash == independent_header.snapshot_hash);
    return true;
}

static bool test_metadata_consistency(void)
{
    test_fixture fixture;
    worr_snapshot_ref_v2 ref;
    worr_snapshot_v2 header;
    worr_snapshot_v2 bad;
    uint64_t hash = UINT64_C(0xaabbccdd);

    CHECK(init_fixture(&fixture));
    CHECK(publish_sample(&fixture, 1, &ref, &header));

    bad = header;
    bad.flags &= ~WORR_SNAPSHOT_FLAG_KEYFRAME;
    CHECK(!Worr_SnapshotCalculateHashV2(&bad, TEST_MAX_ENTITIES, &hash));
    CHECK(hash == UINT64_C(0xaabbccdd));
    bad = header;
    bad.discontinuity.previous.epoch = 1;
    bad.discontinuity.previous.sequence = 1;
    CHECK(!Worr_SnapshotCalculateHashV2(&bad, TEST_MAX_ENTITIES, &hash));
    bad = header;
    bad.snapshot_id.sequence = 2;
    CHECK(!Worr_SnapshotCalculateHashV2(&bad, TEST_MAX_ENTITIES, &hash));
    bad = header;
    bad.event_range.one_past_authority_id.sequence += 1u;
    CHECK(!Worr_SnapshotCalculateHashV2(&bad, TEST_MAX_ENTITIES, &hash));
    bad = header;
    bad.flags |= WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION;
    CHECK(Worr_SnapshotCalculateHashV2(&bad, TEST_MAX_ENTITIES, &hash));
    bad = header;
    bad.flags &= ~WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS;
    CHECK(!Worr_SnapshotCalculateHashV2(&bad, TEST_MAX_ENTITIES, &hash));

    bad = make_snapshot(4, true, 4);
    bad.base_id.sequence = 4;
    bad.discontinuity.previous.sequence = 3;
    bad.entity_range.first_serial = 1;
    bad.entity_range.count = 1;
    bad.area_range.first_serial = 1;
    bad.area_range.count = 1;
    bad.player_hash = 1;
    bad.entity_hash = 2;
    bad.area_hash = 3;
    bad.event_hash = 4;
    CHECK(!Worr_SnapshotCalculateHashV2(&bad, TEST_MAX_ENTITIES, &hash));

    bad = make_snapshot(5, true, 4);
    bad.snapshot_id.sequence = 7;
    bad.base_id.sequence = 4;
    bad.discontinuity.previous.sequence = 5;
    bad.discontinuity.flags = WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP;
    bad.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_SEQUENCE_GAP;
    bad.discontinuity.skipped_sequences = 1;
    bad.entity_range.first_serial = 1;
    bad.entity_range.count = 1;
    bad.area_range.first_serial = 1;
    bad.area_range.count = 1;
    CHECK(Worr_SnapshotCalculateHashV2(&bad, TEST_MAX_ENTITIES, &hash));
    bad.discontinuity.skipped_sequences = 2;
    CHECK(!Worr_SnapshotCalculateHashV2(&bad, TEST_MAX_ENTITIES, &hash));
    return true;
}

static bool test_legacy_projection_can_be_promotion_eligible(void)
{
    test_fixture fixture;
    worr_snapshot_v2 snapshot = make_snapshot(1, false, 4);
    worr_snapshot_player_v2 player = make_player(1, 4, false, 10, 0);
    worr_snapshot_entity_v2 entity = make_entity(2, 1, false, 3, 0);
    worr_snapshot_store_publish_v2 publication;
    worr_snapshot_ref_v2 ref;
    worr_snapshot_v2 stored;

    CHECK(init_fixture(&fixture));
    snapshot.flags |= WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION;
    publication = make_publication(&snapshot, &player, &entity, 1,
                                   NULL, 0, NULL, 0);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(Worr_SnapshotStoreCopySnapshotV2(&fixture.store, ref, &stored) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK((stored.flags & WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE) != 0);
    CHECK((stored.flags & WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION) != 0);
    CHECK((stored.flags &
           WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS) == 0);
    CHECK((stored.controlled_entity.provenance_flags &
           WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED) != 0);

    snapshot.flags |= WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED;
    snapshot.discontinuity.flags |=
        WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED;
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_TRANSPORT_TRUNCATED;
    publication.snapshot = &snapshot;
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_SNAPSHOT);
    return true;
}

static bool test_event_range_wrap_and_gaps(void)
{
    test_fixture fixture;
    worr_snapshot_v2 snapshot;
    worr_snapshot_v2 header;
    worr_snapshot_player_v2 player = make_player(1, 4, true, 10, 0);
    worr_snapshot_event_ref_v2 events[2];
    worr_snapshot_store_publish_v2 publication;
    worr_snapshot_ref_v2 ref;
    uint64_t hash;

    CHECK(init_fixture(&fixture));
    snapshot = make_snapshot(1, true, 4);
    events[0] = make_event(UINT32_MAX, 1, 0);
    events[1] = make_event(1, 2, 1);
    events[0].authority_id.stream_epoch = 7;
    events[0].authority_id.sequence = UINT32_MAX;
    events[1].authority_id.stream_epoch = 8;
    events[1].authority_id.sequence = 1;
    publication = make_publication(&snapshot, &player, NULL, 0,
                                   NULL, 0, events, 2);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(Worr_SnapshotStoreCopySnapshotV2(&fixture.store, ref, &header) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(header.event_range.flags ==
          (WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY |
           WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER));
    CHECK(header.event_range.first_authority_id.stream_epoch == 7 &&
          header.event_range.first_authority_id.sequence == UINT32_MAX);
    CHECK(header.event_range.one_past_authority_id.stream_epoch == 8 &&
          header.event_range.one_past_authority_id.sequence == 2);
    CHECK(Worr_SnapshotCalculateHashV2(&header, TEST_MAX_ENTITIES, &hash));

    snapshot = make_snapshot(2, true, 4);
    events[1].authority_id.sequence = 2;
    publication.snapshot = &snapshot;
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(Worr_SnapshotStoreCopySnapshotV2(&fixture.store, ref, &header) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(header.event_range.flags ==
          WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER);
    CHECK(header.event_range.one_past_authority_id.stream_epoch == 8 &&
          header.event_range.one_past_authority_id.sequence == 3);

    header.event_range.flags =
        WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY |
        WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER;
    hash = UINT64_C(0x1020304050607080);
    CHECK(!Worr_SnapshotCalculateHashV2(&header, TEST_MAX_ENTITIES, &hash));
    CHECK(hash == UINT64_C(0x1020304050607080));

    CHECK(Worr_SnapshotStoreCopySnapshotV2(&fixture.store, ref, &header) ==
          WORR_SNAPSHOT_STORE_OK);
    header.event_range.first_authority_id.stream_epoch = 7;
    header.event_range.first_authority_id.sequence = 1;
    header.event_range.one_past_authority_id.stream_epoch = 8;
    header.event_range.one_past_authority_id.sequence = 1;
    header.event_range.count = UINT32_MAX;
    header.event_range.flags =
        WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY |
        WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER;
    CHECK(Worr_SnapshotCalculateHashV2(&header, TEST_MAX_ENTITIES, &hash));

    header.event_range.first_authority_id.stream_epoch = UINT32_MAX;
    header.event_range.first_authority_id.sequence = 1;
    header.event_range.one_past_authority_id.stream_epoch = UINT32_MAX;
    header.event_range.one_past_authority_id.sequence = UINT32_MAX;
    hash = UINT64_C(0x8877665544332211);
    CHECK(!Worr_SnapshotCalculateHashV2(&header, TEST_MAX_ENTITIES, &hash));
    CHECK(hash == UINT64_C(0x8877665544332211));
    return true;
}

static bool test_observer_attach_inferred_events_and_consumed_cursor(void)
{
    test_fixture fixture;
    worr_snapshot_v2 snapshot;
    worr_snapshot_v2 stored;
    worr_snapshot_player_v2 player;
    worr_snapshot_event_ref_v2 event;
    worr_snapshot_store_publish_v2 publication;
    worr_snapshot_ref_v2 ref;

    CHECK(init_fixture(&fixture));
    snapshot = make_snapshot(50, false, 4);
    snapshot.flags |= WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION;
    memset(&snapshot.discontinuity, 0, sizeof(snapshot.discontinuity));
    snapshot.discontinuity.flags =
        WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH;
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_OBSERVER_ATTACH;
    player = make_player(1, 4, false, 10, 0);
    event = make_event(1, UINT64_C(0x12345678), 0);
    event.provenance =
        WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;
    memset(&event.authority_id, 0, sizeof(event.authority_id));
    publication = make_publication(&snapshot, &player, NULL, 0, NULL, 0,
                                   &event, 1);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(Worr_SnapshotStoreCopySnapshotV2(&fixture.store, ref, &stored) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(stored.snapshot_id.sequence == 50 &&
          stored.discontinuity.previous.epoch == 0 &&
          stored.event_range.provenance ==
              WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED &&
          stored.event_range.flags ==
              WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER &&
          stored.event_range.first_authority_id.stream_epoch == 0);

    snapshot.discontinuity.previous.epoch = 1;
    snapshot.discontinuity.previous.sequence = 49;
    publication.snapshot = &snapshot;
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_SNAPSHOT);
    snapshot.discontinuity.previous.epoch = 0;
    snapshot.discontinuity.previous.sequence = 0;

    event.authority_id.stream_epoch = 9;
    event.authority_id.sequence = 1;
    publication.event_refs = &event;
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_EVENT);
    memset(&event.authority_id, 0, sizeof(event.authority_id));
    event.carrier_ordinal = 1;
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_EVENT_ORDER);
    event.carrier_ordinal = 0;

    snapshot.consumed_command.cursor.epoch = 7;
    snapshot.consumed_command.cursor.contiguous_sequence = 33;
    snapshot.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_NONE;
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_SNAPSHOT);
    snapshot.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_OK);
    snapshot.consumed_command.provenance = 2; /* packet ACK is not accepted */
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_SNAPSHOT);
    return true;
}

static bool test_init_bounds(void)
{
    worr_snapshot_store_v2 invalid_store;
    uint8_t invalid_before[sizeof(invalid_store)];

    memset(&invalid_store, 0x4c, sizeof(invalid_store));
    memcpy(invalid_before, &invalid_store, sizeof(invalid_before));
    CHECK(Worr_SnapshotStoreInitV2(&invalid_store, NULL, 1, NULL, 0, 0,
                                   NULL, 0, 0, NULL, 0, 0,
                                   TEST_MAX_ENTITIES) ==
          WORR_SNAPSHOT_STORE_INVALID_ARGUMENT);
    CHECK(memcmp(&invalid_store, invalid_before, sizeof(invalid_store)) == 0);
#if SIZE_MAX == UINT32_MAX
    worr_snapshot_store_v2 store;
    worr_snapshot_store_slot_v2 slot;
    const uint32_t overflowing_slots =
        (uint32_t)(SIZE_MAX / sizeof(slot)) + 1u;

    memset(&store, 0x4c, sizeof(store));
    CHECK(Worr_SnapshotStoreInitV2(&store, &slot, overflowing_slots,
                                   NULL, 0, 0, NULL, 0, 0,
                                   NULL, 0, 0, TEST_MAX_ENTITIES) ==
          WORR_SNAPSHOT_STORE_INVALID_ARGUMENT);
    {
        const uint8_t *bytes = (const uint8_t *)&store;
        size_t i;
        for (i = 0; i < sizeof(store); ++i)
            CHECK(bytes[i] == UINT8_C(0x4c));
    }
#endif
    return true;
}

static bool test_publish_atomic_failures(void)
{
    test_fixture fixture;
    fixture_backup before;
    worr_snapshot_v2 snapshot;
    worr_snapshot_player_v2 player;
    worr_snapshot_entity_v2 entities[5];
    worr_snapshot_event_ref_v2 events[2];
    uint8_t area[3] = {1, 2, 3};
    worr_snapshot_store_publish_v2 publication;
    worr_snapshot_ref_v2 ref;
    uint32_t i;

    snapshot = make_snapshot(1, true, 4);
    player = make_player(1, 4, true, 10, 0);
    for (i = 0; i < 5; ++i)
        entities[i] = make_entity(i + 2u, 1, true, i + 3u, 0);
    events[0] = make_event(10, 1, 0);
    events[1] = make_event(11, 2, 1);

    CHECK(init_fixture(&fixture));
    publication = make_publication(&snapshot, &player, entities, 5,
                                   area, 3, events, 2);
    ref.slot = UINT32_C(0xaaaaaaaa);
    ref.generation = UINT32_C(0xbbbbbbbb);
    capture_fixture(&fixture, &before);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_CAPACITY);
    CHECK(fixture_unchanged(&fixture, &before));
    CHECK(ref.slot == UINT32_C(0xaaaaaaaa) &&
          ref.generation == UINT32_C(0xbbbbbbbb));

    publication.entity_count = 2;
    events[1].authority_id.sequence = 10;
    capture_fixture(&fixture, &before);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_EVENT_ORDER);
    CHECK(fixture_unchanged(&fixture, &before));
    events[1] = make_event(11, 2, 1);

    player.controlled_entity.identity.generation += 1u;
    capture_fixture(&fixture, &before);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_PLAYER);
    CHECK(fixture_unchanged(&fixture, &before));
    player = make_player(1, 4, true, 10, 0);
    publication.player = &player;

    entities[0].generation.provenance_flags =
        WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    capture_fixture(&fixture, &before);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_ENTITY);
    CHECK(fixture_unchanged(&fixture, &before));
    entities[0] = make_entity(2, 1, true, 3, 0);

    entities[0] = make_entity(1, 5, true, 3, 0);
    capture_fixture(&fixture, &before);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_ENTITY);
    CHECK(fixture_unchanged(&fixture, &before));
    entities[0] = make_entity(2, 1, true, 3, 0);

    snapshot.discontinuity.flags &=
        ~WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
    capture_fixture(&fixture, &before);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_INVALID_SNAPSHOT);
    CHECK(fixture_unchanged(&fixture, &before));
    snapshot = make_snapshot(1, true, 4);
    publication.snapshot = &snapshot;

    fixture.store.next_entity_serial = UINT64_MAX;
    capture_fixture(&fixture, &before);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_SERIAL_EXHAUSTED);
    CHECK(fixture_unchanged(&fixture, &before));
    fixture.store.next_entity_serial = 1;

    fixture.slots[fixture.store.next_slot].generation = UINT32_MAX;
    capture_fixture(&fixture, &before);
    CHECK(Worr_SnapshotStorePublishV2(&fixture.store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_GENERATION_EXHAUSTED);
    CHECK(fixture_unchanged(&fixture, &before));
    return true;
}

static bool test_copy_faults_and_reset(void)
{
    test_fixture fixture;
    fixture_backup before;
    worr_snapshot_ref_v2 ref1;
    worr_snapshot_ref_v2 ref2;
    worr_snapshot_entity_v2 output[2];
    worr_snapshot_player_v2 player_output;
    worr_snapshot_v2 header_output;
    uint8_t bytes[3];
    uint32_t count;
    uint64_t entity_serial;
    unsigned int i;

    CHECK(init_fixture(&fixture));
    CHECK(publish_sample(&fixture, 1, &ref1, NULL));
    fixture.entities[0].origin[0] += 1.0f;
    memset(output, 0x55, sizeof(output));
    count = UINT32_C(0xabcdef01);
    CHECK(Worr_SnapshotStoreCopyEntitiesV2(&fixture.store, ref1, output, 2,
                                           &count) ==
          WORR_SNAPSHOT_STORE_CORRUPT);
    CHECK(count == UINT32_C(0xabcdef01));
    for (i = 0; i < sizeof(output); ++i)
        CHECK(((const uint8_t *)output)[i] == UINT8_C(0x55));
    fixture.entities[0].origin[0] -= 1.0f;

    fixture.slots[0].player.fov += 1.0f;
    memset(&player_output, 0x66, sizeof(player_output));
    CHECK(Worr_SnapshotStoreCopyPlayerV2(&fixture.store, ref1,
                                         &player_output) ==
          WORR_SNAPSHOT_STORE_CORRUPT);
    for (i = 0; i < sizeof(player_output); ++i)
        CHECK(((const uint8_t *)&player_output)[i] == UINT8_C(0x66));
    fixture.slots[0].player.fov -= 1.0f;

    fixture.area[0] ^= UINT8_C(0x20);
    memset(bytes, 0x77, sizeof(bytes));
    count = UINT32_C(0xabcdef01);
    CHECK(Worr_SnapshotStoreCopyAreaV2(&fixture.store, ref1, bytes, 3,
                                       &count) == WORR_SNAPSHOT_STORE_CORRUPT);
    CHECK(count == UINT32_C(0xabcdef01) && bytes[0] == UINT8_C(0x77));
    fixture.area[0] ^= UINT8_C(0x20);

    fixture.slots[0].snapshot.snapshot_hash ^= UINT64_C(1);
    memset(&header_output, 0x44, sizeof(header_output));
    CHECK(Worr_SnapshotStoreCopySnapshotV2(&fixture.store, ref1,
                                           &header_output) ==
          WORR_SNAPSHOT_STORE_CORRUPT);
    for (i = 0; i < sizeof(header_output); ++i)
        CHECK(((const uint8_t *)&header_output)[i] == UINT8_C(0x44));
    fixture.slots[0].snapshot.snapshot_hash ^= UINT64_C(1);

    entity_serial = fixture.store.next_entity_serial;
    CHECK(publish_sample(&fixture, 2, &ref2, NULL));
    CHECK(Worr_SnapshotStoreResetV2(&fixture.store) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(!Worr_SnapshotStoreRefValidV2(&fixture.store, ref1));
    CHECK(!Worr_SnapshotStoreRefValidV2(&fixture.store, ref2));
    CHECK(fixture.store.occupied == 0);
    CHECK(fixture.store.next_entity_serial > entity_serial);

    fixture.slots[1].generation = UINT32_MAX;
    capture_fixture(&fixture, &before);
    CHECK(Worr_SnapshotStoreResetV2(&fixture.store) ==
          WORR_SNAPSHOT_STORE_GENERATION_EXHAUSTED);
    CHECK(fixture_unchanged(&fixture, &before));
    return true;
}

static bool test_zero_payload_store(void)
{
    worr_snapshot_store_v2 store;
    worr_snapshot_store_slot_v2 slot;
    worr_snapshot_v2 snapshot = make_snapshot(1, true, 4);
    worr_snapshot_player_v2 player = make_player(1, 4, true, 10, 0);
    worr_snapshot_store_publish_v2 publication;
    worr_snapshot_ref_v2 ref;
    worr_snapshot_v2 output;
    uint32_t count = 99;

    CHECK(Worr_SnapshotStoreInitV2(&store, &slot, 1, NULL, 0, 0,
                                   NULL, 0, 0, NULL, 0, 0,
                                   TEST_MAX_ENTITIES) ==
          WORR_SNAPSHOT_STORE_OK);
    publication = make_publication(&snapshot, &player, NULL, 0,
                                   NULL, 0, NULL, 0);
    CHECK(Worr_SnapshotStorePublishV2(&store, &publication, &ref) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(Worr_SnapshotStoreCopySnapshotV2(&store, ref, &output) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(output.entity_range.first_serial == 0 &&
          output.area_range.first_serial == 0 &&
          output.event_range.first_ref_serial == 0);
    CHECK(Worr_SnapshotStoreCopyEntitiesV2(&store, ref, NULL, 0, &count) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(count == 0);
    return true;
}

static uint64_t soak_chain_step(uint64_t chain, uint64_t value)
{
    unsigned int i;
    for (i = 0; i < 8; ++i) {
        chain ^= (uint8_t)(value >> (i * 8));
        chain *= UINT64_C(1099511628211);
    }
    return chain;
}

static bool run_soak(uint64_t *chain_out)
{
    enum {
        SOAK_SLOTS = 4,
        SOAK_ENTITIES_PER_SLOT = 3,
        SOAK_AREA_PER_SLOT = 4,
        SOAK_EVENTS_PER_SLOT = 1,
        SOAK_COUNT = 100000
    };
    worr_snapshot_store_v2 store;
    worr_snapshot_store_slot_v2 slots[SOAK_SLOTS];
    worr_snapshot_entity_v2
        arena_entities[SOAK_SLOTS * SOAK_ENTITIES_PER_SLOT];
    uint8_t arena_area[SOAK_SLOTS * SOAK_AREA_PER_SLOT];
    worr_snapshot_event_ref_v2
        arena_events[SOAK_SLOTS * SOAK_EVENTS_PER_SLOT];
    worr_snapshot_entity_v2 entities[SOAK_ENTITIES_PER_SLOT];
    worr_snapshot_event_ref_v2 event;
    worr_snapshot_ref_v2 ref;
    worr_snapshot_v2 snapshot;
    worr_snapshot_v2 stored;
    worr_snapshot_player_v2 player;
    worr_snapshot_store_publish_v2 publication;
    worr_snapshot_store_stats_v2 stats;
    uint8_t area[SOAK_AREA_PER_SLOT];
    uint64_t chain = UINT64_C(14695981039346656037);
    uint32_t i;
    void *slots_address = slots;
    void *entities_address = arena_entities;

    CHECK(Worr_SnapshotStoreInitV2(
              &store, slots, SOAK_SLOTS, arena_entities,
              SOAK_SLOTS * SOAK_ENTITIES_PER_SLOT,
              SOAK_ENTITIES_PER_SLOT, arena_area,
              SOAK_SLOTS * SOAK_AREA_PER_SLOT, SOAK_AREA_PER_SLOT,
              arena_events, SOAK_SLOTS * SOAK_EVENTS_PER_SLOT,
              SOAK_EVENTS_PER_SLOT, TEST_MAX_ENTITIES) ==
          WORR_SNAPSHOT_STORE_OK);

    for (i = 0; i < SOAK_COUNT; ++i) {
        uint32_t e;
        snapshot = make_snapshot(i + 1u, true, 4);
        player = make_player(1, 4, true, i & UINT32_C(255), 0);
        for (e = 0; e < SOAK_ENTITIES_PER_SLOT; ++e) {
            entities[e] = make_entity(e + 2u, 1, true,
                                      (i + e) & UINT32_C(255), 0);
        }
        area[0] = (uint8_t)i;
        area[1] = (uint8_t)(i >> 8);
        area[2] = (uint8_t)(i >> 16);
        area[3] = (uint8_t)(i >> 24);
        event = make_event(i + 1u,
                           UINT64_C(0x9e3779b97f4a7c15) ^ i, 0);
        publication = make_publication(
            &snapshot, &player, entities, SOAK_ENTITIES_PER_SLOT,
            area, SOAK_AREA_PER_SLOT, &event, SOAK_EVENTS_PER_SLOT);
        CHECK(Worr_SnapshotStorePublishV2(&store, &publication, &ref) ==
              WORR_SNAPSHOT_STORE_OK);
        CHECK(Worr_SnapshotStoreCopySnapshotV2(&store, ref, &stored) ==
              WORR_SNAPSHOT_STORE_OK);
        chain = soak_chain_step(chain, stored.snapshot_hash);
    }
    CHECK(store.slots == slots_address && store.entities == entities_address);
    CHECK(Worr_SnapshotStoreGetStatsV2(&store, &stats) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(stats.publish_count == SOAK_COUNT && stats.occupied == SOAK_SLOTS &&
          stats.occupied_high_water == SOAK_SLOTS &&
          stats.entity_high_water == SOAK_ENTITIES_PER_SLOT &&
          stats.area_high_water == SOAK_AREA_PER_SLOT &&
          stats.event_high_water == SOAK_EVENTS_PER_SLOT);
    *chain_out = chain;
    return true;
}

static bool print_golden(void)
{
    test_fixture fixture;
    worr_snapshot_player_v2 player = make_player(1, 4, true, 10, 0);
    worr_snapshot_entity_v2 entity = make_entity(2, 7, true, 6, 0);
    worr_snapshot_entity_v2 entities[2];
    worr_snapshot_event_ref_v2 events[2];
    const uint8_t area[3] = {UINT8_C(0x01), UINT8_C(0x80), UINT8_C(0x7f)};
    worr_snapshot_ref_v2 ref;
    worr_snapshot_v2 header;
    uint64_t player_hash;
    uint64_t entity_hash;
    uint64_t entity_list_hash;
    uint64_t area_hash;
    uint64_t event_hash;
    uint64_t soak_chain;

    entities[0] = make_entity(2, 1, true, 3, 0);
    entities[1] = make_entity(5, 1, true, 4, 0);
    events[0] = make_event(10, UINT64_C(0x1234), 0);
    events[1] = make_event(11, UINT64_C(0x5678), 1);
    CHECK(Worr_SnapshotPlayerHashV2(&player, TEST_MAX_ENTITIES,
                                    &player_hash));
    CHECK(Worr_SnapshotEntityHashV2(&entity, TEST_MAX_ENTITIES,
                                    &entity_hash));
    CHECK(Worr_SnapshotEntityListHashV2(entities, 2, TEST_MAX_ENTITIES,
                                        &entity_list_hash));
    CHECK(Worr_SnapshotAreaHashV2(area, 3, &area_hash));
    CHECK(Worr_SnapshotEventRefsHashV2(events, 2, &event_hash));
    CHECK(init_fixture(&fixture));
    CHECK(publish_sample(&fixture, 1, &ref, &header));
    CHECK(run_soak(&soak_chain));
    printf("player=%016llx\n", (unsigned long long)player_hash);
    printf("entity=%016llx\n", (unsigned long long)entity_hash);
    printf("entity_list=%016llx\n", (unsigned long long)entity_list_hash);
    printf("area=%016llx\n", (unsigned long long)area_hash);
    printf("events=%016llx\n", (unsigned long long)event_hash);
    printf("snapshot=%016llx\n", (unsigned long long)header.snapshot_hash);
    printf("soak_chain=%016llx\n", (unsigned long long)soak_chain);
    return true;
}

int main(int argc, char **argv)
{
    uint64_t soak_chain;

    if (argc == 2 && strcmp(argv[1], "--print-golden") == 0)
        return print_golden() ? EXIT_SUCCESS : EXIT_FAILURE;
    if (argc != 1) {
        fprintf(stderr, "usage: snapshot_store_test [--print-golden]\n");
        return EXIT_FAILURE;
    }
    if (!test_ids_and_generations() || !test_record_hash_contract() ||
        !test_store_publish_copy_and_stale_refs() ||
        !test_metadata_consistency() ||
        !test_legacy_projection_can_be_promotion_eligible() ||
        !test_event_range_wrap_and_gaps() ||
        !test_observer_attach_inferred_events_and_consumed_cursor() ||
        !test_init_bounds() || !test_publish_atomic_failures() ||
        !test_copy_faults_and_reset() || !test_zero_payload_store() ||
        !run_soak(&soak_chain) || soak_chain != EXPECTED_SOAK_CHAIN) {
        return EXIT_FAILURE;
    }
    puts("snapshot ABI/store tests passed (100000 transactional publishes)");
    return EXIT_SUCCESS;
}
