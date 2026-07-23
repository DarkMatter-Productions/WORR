/* Standalone FR-10-T07 snapshot timeline behavioral/fault test. */

#include "common/net/snapshot_timeline.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_SLOTS 3u
#define TEST_ENTITIES_PER_SLOT 2u
#define TEST_AREA_PER_SLOT 4u
#define TEST_EVENTS_PER_SLOT 2u
#define TEST_MAX_ENTITIES 64u

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(stderr, "snapshot_timeline_test:%d: %s\n", __LINE__, \
                    #expression);                                           \
            return false;                                                   \
        }                                                                   \
    } while (0)

typedef struct fixture_s {
    worr_snapshot_timeline_v1 timeline;
    worr_snapshot_timeline_slot_v1 slots[TEST_SLOTS];
    worr_snapshot_entity_v2 entities[TEST_SLOTS * TEST_ENTITIES_PER_SLOT];
    uint8_t area[TEST_SLOTS * TEST_AREA_PER_SLOT];
    worr_snapshot_event_ref_v2 events[TEST_SLOTS * TEST_EVENTS_PER_SLOT];
} fixture;

typedef struct fixture_backup_s {
    worr_snapshot_timeline_v1 timeline;
    worr_snapshot_timeline_slot_v1 slots[TEST_SLOTS];
    worr_snapshot_entity_v2 entities[TEST_SLOTS * TEST_ENTITIES_PER_SLOT];
    uint8_t area[TEST_SLOTS * TEST_AREA_PER_SLOT];
    worr_snapshot_event_ref_v2 events[TEST_SLOTS * TEST_EVENTS_PER_SLOT];
} fixture_backup;

typedef struct projection_s {
    worr_snapshot_v2 snapshot;
    worr_snapshot_player_v2 player;
    worr_snapshot_entity_v2 entities[1];
    uint8_t area[2];
    worr_snapshot_event_ref_v2 events[1];
    worr_snapshot_projection_view_v2 view;
} projection;

static worr_snapshot_entity_generation_v2 generation(uint32_t index,
                                                       uint32_t value)
{
    worr_snapshot_entity_generation_v2 result;
    memset(&result, 0, sizeof(result));
    result.identity.index = index;
    result.identity.generation = value;
    result.provenance_flags = WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    return result;
}

static worr_snapshot_player_v2 make_player(void)
{
    worr_snapshot_player_v2 player;
    memset(&player, 0, sizeof(player));
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.controlled_entity = generation(1, 1);
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    player.movement.gravity = 800;
    player.movement.view_height = 22;
    player.fov = 100.0f;
    return player;
}

static worr_snapshot_entity_v2 make_entity(float x, float angle,
                                            uint32_t generation_value)
{
    worr_snapshot_entity_v2 entity;
    memset(&entity, 0, sizeof(entity));
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.generation = generation(2, generation_value);
    entity.component_mask = WORR_SNAPSHOT_ENTITY_COMPONENTS_V2;
    entity.origin[0] = x;
    entity.angles[1] = angle;
    entity.old_origin[0] = x - 1.0f;
    entity.model_index[0] = 3;
    entity.frame = 4;
    entity.sound = 5;
    entity.skin = 6;
    entity.solid = 7;
    entity.effects = 8;
    entity.renderfx = 9;
    entity.alpha = 1.0f;
    entity.scale = 1.0f;
    entity.loop_volume = 1.0f;
    entity.loop_attenuation = 1.0f;
    entity.owner.index = 1;
    entity.owner.generation = 1;
    entity.old_frame = 3;
    return entity;
}

static worr_snapshot_event_ref_v2 make_event(uint32_t sequence,
                                              uint64_t semantic_hash)
{
    worr_snapshot_event_ref_v2 event;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    event.provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
    event.semantic_version = WORR_EVENT_MODEL_REVISION;
    event.authority_id.stream_epoch = 9;
    event.authority_id.sequence = sequence;
    event.semantic_hash = semantic_hash;
    return event;
}

static bool finalize_projection(projection *value, uint32_t epoch,
                                uint32_t sequence, uint64_t server_time_us,
                                float origin, float angle,
                                uint32_t entity_generation,
                                uint32_t event_sequence,
                                uint64_t event_semantic,
                                uint32_t boundary_flags,
                                uint16_t boundary_reason)
{
    uint64_t hash;
    memset(value, 0, sizeof(*value));
    value->player = make_player();
    value->entities[0] =
        make_entity(origin, angle, entity_generation);
    value->area[0] = (uint8_t)sequence;
    value->area[1] = (uint8_t)epoch;
    value->events[0] = make_event(event_sequence, event_semantic);

    value->snapshot.struct_size = sizeof(value->snapshot);
    value->snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    value->snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    value->snapshot.flags = WORR_SNAPSHOT_FLAG_COMPLETE |
                            WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE |
                            WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS;
    value->snapshot.snapshot_id.epoch = epoch;
    value->snapshot.snapshot_id.sequence = sequence;
    value->snapshot.server_tick = sequence;
    value->snapshot.server_time_us = server_time_us;
    value->snapshot.controlled_entity = generation(1, 1);
    value->snapshot.entity_range.first_serial =
        (uint64_t)sequence * 16u + 1u;
    value->snapshot.entity_range.count = 1;
    value->snapshot.area_range.first_serial =
        (uint64_t)sequence * 16u + 2u;
    value->snapshot.area_range.count = 2;
    value->snapshot.event_range.first_ref_serial =
        (uint64_t)sequence * 16u + 4u;
    value->snapshot.event_range.count = 1;
    value->snapshot.event_range.provenance =
        WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
    value->snapshot.event_range.flags =
        WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY |
        WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER;
    value->snapshot.event_range.first_authority_id =
        value->events[0].authority_id;
    if (!Worr_EventIdNextV1(value->events[0].authority_id,
                            &value->snapshot.event_range
                                 .one_past_authority_id)) {
        return false;
    }

    if (sequence == 1) {
        value->snapshot.flags |= WORR_SNAPSHOT_FLAG_KEYFRAME;
        value->snapshot.discontinuity.flags =
            WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT | boundary_flags;
        value->snapshot.discontinuity.reason = boundary_reason;
    } else {
        value->snapshot.base_id.epoch = epoch;
        value->snapshot.base_id.sequence = sequence - 1u;
        value->snapshot.discontinuity.previous.epoch = epoch;
        value->snapshot.discontinuity.previous.sequence = sequence - 1u;
        value->snapshot.discontinuity.server_tick_delta = 1;
        value->snapshot.discontinuity.flags = boundary_flags;
        value->snapshot.discontinuity.reason = boundary_reason;
        if ((boundary_flags &
             WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT) != 0) {
            value->snapshot.flags |= WORR_SNAPSHOT_FLAG_KEYFRAME;
            memset(&value->snapshot.base_id, 0,
                   sizeof(value->snapshot.base_id));
        }
    }

    if (!Worr_SnapshotPlayerHashV2(&value->player, TEST_MAX_ENTITIES,
                                   &value->snapshot.player_hash) ||
        !Worr_SnapshotEntityListHashV2(
            value->entities, 1, TEST_MAX_ENTITIES,
            &value->snapshot.entity_hash) ||
        !Worr_SnapshotAreaHashV2(value->area, 2,
                                 &value->snapshot.area_hash) ||
        !Worr_SnapshotEventRefsHashV2(
            value->events, 1, &value->snapshot.event_hash) ||
        !Worr_SnapshotCalculateHashV2(&value->snapshot,
                                      TEST_MAX_ENTITIES, &hash)) {
        return false;
    }
    value->snapshot.snapshot_hash = hash;
    value->view.struct_size = sizeof(value->view);
    value->view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    value->view.snapshot = &value->snapshot;
    value->view.player = &value->player;
    value->view.entities = value->entities;
    value->view.area_bytes = value->area;
    value->view.event_refs = value->events;
    value->view.entity_count = 1;
    value->view.area_byte_count = 2;
    value->view.event_ref_count = 1;
    return true;
}

static bool refresh_projection(projection *value)
{
    value->snapshot.snapshot_hash = 0;
    if (!Worr_SnapshotPlayerHashV2(&value->player, TEST_MAX_ENTITIES,
                                   &value->snapshot.player_hash) ||
        !Worr_SnapshotEntityListHashV2(
            value->view.entities, value->view.entity_count,
            TEST_MAX_ENTITIES, &value->snapshot.entity_hash) ||
        !Worr_SnapshotAreaHashV2(value->view.area_bytes,
                                 value->view.area_byte_count,
                                 &value->snapshot.area_hash) ||
        !Worr_SnapshotEventRefsHashV2(
            value->view.event_refs, value->view.event_ref_count,
            &value->snapshot.event_hash) ||
        !Worr_SnapshotCalculateHashV2(&value->snapshot,
                                      TEST_MAX_ENTITIES,
                                      &value->snapshot.snapshot_hash)) {
        return false;
    }
    return true;
}

static bool make_legacy_projection(projection *value)
{
    value->snapshot.flags &=
        ~WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS;
    value->snapshot.flags |= WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION;
    value->snapshot.controlled_entity.provenance_flags =
        WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    value->player.controlled_entity.provenance_flags =
        WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    value->entities[0].generation.provenance_flags =
        WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    value->events[0].provenance =
        WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;
    memset(&value->events[0].authority_id, 0,
           sizeof(value->events[0].authority_id));
    value->snapshot.event_range.provenance =
        WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;
    value->snapshot.event_range.flags =
        WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER;
    memset(&value->snapshot.event_range.first_authority_id, 0,
           sizeof(value->snapshot.event_range.first_authority_id));
    memset(&value->snapshot.event_range.one_past_authority_id, 0,
           sizeof(value->snapshot.event_range.one_past_authority_id));
    return refresh_projection(value);
}

static bool remove_projected_entity(projection *value)
{
    value->view.entities = NULL;
    value->view.entity_count = 0;
    value->snapshot.entity_range.first_serial = 0;
    value->snapshot.entity_range.count = 0;
    return refresh_projection(value);
}

static bool init_fixture(fixture *value)
{
    memset(value, 0xa5, sizeof(*value));
    return Worr_SnapshotTimelineInitV1(
               &value->timeline, value->slots, TEST_SLOTS,
               value->entities,
               TEST_SLOTS * TEST_ENTITIES_PER_SLOT,
               TEST_ENTITIES_PER_SLOT, value->area,
               TEST_SLOTS * TEST_AREA_PER_SLOT, TEST_AREA_PER_SLOT,
               value->events, TEST_SLOTS * TEST_EVENTS_PER_SLOT,
               TEST_EVENTS_PER_SLOT, TEST_MAX_ENTITIES) ==
           WORR_SNAPSHOT_TIMELINE_OK;
}

static void backup_fixture(const fixture *value, fixture_backup *backup)
{
    backup->timeline = value->timeline;
    memcpy(backup->slots, value->slots, sizeof(backup->slots));
    memcpy(backup->entities, value->entities, sizeof(backup->entities));
    memcpy(backup->area, value->area, sizeof(backup->area));
    memcpy(backup->events, value->events, sizeof(backup->events));
}

static bool fixture_unchanged(const fixture *value,
                              const fixture_backup *backup)
{
    return memcmp(&value->timeline, &backup->timeline,
                  sizeof(backup->timeline)) == 0 &&
           memcmp(value->slots, backup->slots,
                  sizeof(backup->slots)) == 0 &&
           memcmp(value->entities, backup->entities,
                  sizeof(backup->entities)) == 0 &&
           memcmp(value->area, backup->area,
                  sizeof(backup->area)) == 0 &&
           memcmp(value->events, backup->events,
                  sizeof(backup->events)) == 0;
}

static worr_snapshot_timeline_clock_request_v1 clock_request(
    uint16_t operation, uint64_t host_time_us, uint64_t render_time_us,
    uint32_t rate_q16, uint16_t reset_reason)
{
    worr_snapshot_timeline_clock_request_v1 request;
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    request.operation = operation;
    request.reset_reason = reset_reason;
    request.rate_q16 = rate_q16;
    request.host_time_us = host_time_us;
    request.render_time_us = render_time_us;
    return request;
}

static worr_snapshot_timeline_policy_v1 default_policy(void)
{
    worr_snapshot_timeline_policy_v1 policy;
    memset(&policy, 0, sizeof(policy));
    policy.struct_size = sizeof(policy);
    policy.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    policy.max_extrapolation_us = 50000;
    policy.teleport_distance = 128.0f;
    policy.max_linear_velocity = 1000.0f;
    policy.max_angular_velocity = 1000.0f;
    policy.allow_extrapolation = 1;
    return policy;
}

static bool publish(fixture *value, projection *input,
                    uint64_t receive_time_us,
                    worr_snapshot_timeline_ref_v1 *ref_out)
{
    return Worr_SnapshotTimelinePublishV1(
               &value->timeline, &input->view, receive_time_us,
               ref_out) == WORR_SNAPSHOT_TIMELINE_OK;
}

static bool test_clock_fraction_pause_seek_and_atomicity(void)
{
    fixture first;
    fixture second;
    fixture_backup backup;
    worr_snapshot_timeline_clock_request_v1 request;
    worr_snapshot_timeline_clock_state_v1 state;
    worr_snapshot_timeline_hashes_v1 first_hashes;
    worr_snapshot_timeline_hashes_v1 second_hashes;

    CHECK(init_fixture(&first) && init_fixture(&second));
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR, 100, 0,
                            WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16 / 2u,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_INITIAL);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&first.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(state.render_time_us == 0 && state.fractional_q16 == 0);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ADVANCE, 101, 0,
                            0, WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&first.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(state.render_time_us == 0 && state.fractional_q16 == 32768);
    request.host_time_us = 102;
    CHECK(Worr_SnapshotTimelineClockApplyV1(&first.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(state.render_time_us == 1 && state.fractional_q16 == 0);

    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR, 100, 0,
                            WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16 / 2u,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_INITIAL);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&second.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ADVANCE, 102, 0,
                            0, WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&second.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(state.render_time_us == 1 && state.fractional_q16 == 0);
    CHECK(Worr_SnapshotTimelineHashesV1(&first.timeline, &first_hashes) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineHashesV1(&second.timeline, &second_hashes) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(first_hashes.clock_hash == second_hashes.clock_hash);

    backup_fixture(&first, &backup);
    memset(&state, 0x6d, sizeof(state));
    request.host_time_us = 101;
    CHECK(Worr_SnapshotTimelineClockApplyV1(&first.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_CLOCK_REGRESSION);
    CHECK(fixture_unchanged(&first, &backup));
    CHECK(((const uint8_t *)&state)[0] == UINT8_C(0x6d));

    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_PAUSE, 104, 0, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&first.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(state.paused == 1 && state.render_time_us == 2);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ADVANCE, 110, 0, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&first.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(state.render_time_us == 2);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_RESUME, 120, 0, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&first.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_SET_RATE, 120, 0,
                            WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&first.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ADVANCE, 121, 0, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&first.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(state.render_time_us == 3);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK, 122, 500,
                            0, WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&first.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(state.epoch == 2 && state.render_time_us == 500 &&
          state.fractional_q16 == 0);
    return true;
}

static bool test_selection_sampling_events_and_seek_staleness(void)
{
    fixture value;
    projection first;
    projection second;
    projection full;
    worr_snapshot_timeline_ref_v1 ref;
    worr_snapshot_timeline_clock_request_v1 request;
    worr_snapshot_timeline_clock_state_v1 state;
    worr_snapshot_timeline_policy_v1 policy = default_policy();
    worr_snapshot_timeline_pair_v1 pair;
    worr_snapshot_timeline_pair_v1 old_pair;
    worr_snapshot_timeline_entity_sample_v1 sample;
    worr_snapshot_timeline_event_cursor_v1 cursor;
    worr_snapshot_timeline_event_cursor_v1 next;
    worr_snapshot_timeline_event_observation_v1 observation;

    CHECK(init_fixture(&value));
    CHECK(finalize_projection(
        &first, 1, 1, 100000, 0.0f, 350.0f, 1, 10,
        UINT64_C(0x1111), WORR_SNAPSHOT_DISCONTINUITY_INITIAL,
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL));
    CHECK(finalize_projection(&second, 1, 2, 200000, 10.0f, 10.0f, 1,
                              10, UINT64_C(0x1111), 0,
                              WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE));
    CHECK(publish(&value, &first, 1, &ref));
    CHECK(publish(&value, &second, 2, &ref));
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR, 0, 150000,
                            WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_INITIAL);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE &&
          pair.phase_numerator_us == 50000 &&
          pair.phase_denominator_us == 100000);
    old_pair = pair;
    CHECK(Worr_SnapshotTimelineSampleEntityV1(
              &value.timeline, &policy, &pair, 2, &sample) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(sample.visible &&
          sample.mode == WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED &&
          fabsf(sample.entity.origin[0] - 5.0f) < 0.0001f &&
          (sample.entity.angles[1] < 0.001f ||
           sample.entity.angles[1] > 359.999f));

    CHECK(Worr_SnapshotTimelineEventCursorBeginV1(&value.timeline,
                                                  &cursor) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineEventNextV1(
              &value.timeline, &cursor, &next, &observation) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(observation.retained_match_count == 0 &&
          (observation.flags &
           WORR_SNAPSHOT_TIMELINE_EVENT_HISTORY_COMPLETE) != 0);
    cursor = next;
    CHECK(Worr_SnapshotTimelineEventNextV1(
              &value.timeline, &cursor, &next, &observation) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(observation.retained_match_count == 1 &&
          observation.first_match_publish_serial == 1 &&
          (observation.flags &
           WORR_SNAPSHOT_TIMELINE_EVENT_RETAINED_MATCH) != 0);

    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK, 1,
                            225000, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSampleEntityV1(
              &value.timeline, &policy, &old_pair, 2, &sample) ==
          WORR_SNAPSHOT_TIMELINE_STALE_REF);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE &&
          pair.extrapolation_us == 25000);
    CHECK(Worr_SnapshotTimelineSampleEntityV1(
              &value.timeline, &policy, &pair, 2, &sample) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(sample.mode == WORR_SNAPSHOT_TIMELINE_ENTITY_EXTRAPOLATED &&
          fabsf(sample.entity.origin[0] - 12.5f) < 0.0001f &&
          fabsf(sample.entity.angles[1] - 15.0f) < 0.001f);

    CHECK(finalize_projection(
        &full, 1, 3, 300000, 20.0f, 20.0f, 1, 11,
        UINT64_C(0x2222), WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT,
        WORR_SNAPSHOT_DISCONTINUITY_REASON_FULL_SNAPSHOT));
    CHECK(publish(&value, &full, 3, &ref));
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK, 2,
                            250000, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_HOLD &&
          (pair.blocking_reasons &
           WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_DISCONTINUITY) != 0);
    CHECK(Worr_SnapshotTimelineSampleEntityV1(
              &value.timeline, &policy, &pair, 2, &sample) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(sample.mode == WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS &&
          fabsf(sample.entity.origin[0] - 10.0f) < 0.0001f);
    return true;
}

static bool test_ring_cursor_overrun_reset_and_faults(void)
{
    fixture value;
    fixture_backup backup;
    projection inputs[4];
    worr_snapshot_timeline_ref_v1 refs[4];
    worr_snapshot_timeline_event_cursor_v1 cursor;
    worr_snapshot_timeline_event_cursor_v1 next;
    worr_snapshot_timeline_event_observation_v1 observation;
    worr_snapshot_entity_v2 output;
    uint32_t count = UINT32_C(0xabcdef01);
    uint32_t i;

    CHECK(init_fixture(&value));
    for (i = 0; i < 4; ++i) {
        CHECK(finalize_projection(
            &inputs[i], 1, i + 1u, (uint64_t)(i + 1u) * 100000u,
            (float)i, (float)i, 1, i + 1u,
            UINT64_C(0x1000) + i,
            i == 0 ? WORR_SNAPSHOT_DISCONTINUITY_INITIAL : 0,
            i == 0 ? WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL
                   : WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE));
    }
    CHECK(publish(&value, &inputs[0], 1, &refs[0]));
    CHECK(Worr_SnapshotTimelineEventCursorBeginV1(&value.timeline,
                                                  &cursor) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(publish(&value, &inputs[1], 2, &refs[1]));
    CHECK(publish(&value, &inputs[2], 3, &refs[2]));
    CHECK(publish(&value, &inputs[3], 4, &refs[3]));
    CHECK(!Worr_SnapshotTimelineRefValidV1(&value.timeline, refs[0]));
    CHECK(Worr_SnapshotTimelineEventNextV1(
              &value.timeline, &cursor, &next, &observation) ==
          WORR_SNAPSHOT_TIMELINE_CURSOR_OVERRUN);

    memset(&output, 0x6d, sizeof(output));
    value.entities[refs[3].slot * TEST_ENTITIES_PER_SLOT].origin[0] +=
        1.0f;
    CHECK(Worr_SnapshotTimelineCopyEntitiesV1(
              &value.timeline, refs[3], &output, 1, &count) ==
          WORR_SNAPSHOT_TIMELINE_CORRUPT);
    CHECK(count == UINT32_C(0xabcdef01) &&
          ((const uint8_t *)&output)[0] == UINT8_C(0x6d));
    value.entities[refs[3].slot * TEST_ENTITIES_PER_SLOT].origin[0] -=
        1.0f;

    value.slots[refs[3].slot].entity_count =
        TEST_ENTITIES_PER_SLOT + 1u;
    CHECK(Worr_SnapshotTimelineCopyEntitiesV1(
              &value.timeline, refs[3], &output, 1, &count) ==
          WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE);
    value.slots[refs[3].slot].entity_count = 1;

    value.slots[0].generation = UINT32_MAX;
    backup_fixture(&value, &backup);
    CHECK(Worr_SnapshotTimelineResetV1(&value.timeline) ==
          WORR_SNAPSHOT_TIMELINE_GENERATION_EXHAUSTED);
    CHECK(fixture_unchanged(&value, &backup));
    value.slots[0].generation =
        backup.slots[0].generation == UINT32_MAX ? 2u
                                                 : backup.slots[0].generation;
    CHECK(Worr_SnapshotTimelineResetV1(&value.timeline) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(!Worr_SnapshotTimelineRefValidV1(&value.timeline, refs[3]));
    CHECK(Worr_SnapshotTimelineEventNextV1(
              &value.timeline, &cursor, &next, &observation) ==
          WORR_SNAPSHOT_TIMELINE_CURSOR_STALE);
    return true;
}

static bool test_boundaries_conflicts_generations_and_clamps(void)
{
    fixture value;
    projection first;
    projection conflict;
    projection replacement;
    projection boundary;
    worr_snapshot_timeline_ref_v1 ref;
    worr_snapshot_timeline_event_cursor_v1 cursor;
    worr_snapshot_timeline_event_cursor_v1 next;
    worr_snapshot_timeline_event_observation_v1 observation;
    worr_snapshot_timeline_clock_request_v1 request;
    worr_snapshot_timeline_clock_state_v1 state;
    worr_snapshot_timeline_policy_v1 policy = default_policy();
    worr_snapshot_timeline_pair_v1 pair;
    worr_snapshot_timeline_entity_sample_v1 sample;

    CHECK(init_fixture(&value));
    CHECK(finalize_projection(
        &first, 1, 1, 100000, 0.0f, 0.0f, 1, 1,
        UINT64_C(0x1111), WORR_SNAPSHOT_DISCONTINUITY_INITIAL,
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL));
    CHECK(publish(&value, &first, 1, &ref));
    CHECK(Worr_SnapshotTimelineEventCursorBeginV1(&value.timeline,
                                                  &cursor) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR, 0, 50000,
                            WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_INITIAL);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_EARLIEST);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK, 1,
                            100000, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_EXACT);

    CHECK(finalize_projection(&conflict, 1, 2, 200000, 10.0f, 0.0f, 2,
                              1, UINT64_C(0x2222), 0,
                              WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE));
    CHECK(Worr_SnapshotTimelinePublishV1(
              &value.timeline, &conflict.view, 2, &ref) ==
          WORR_SNAPSHOT_TIMELINE_EVENT_CONFLICT);
    CHECK(finalize_projection(&replacement, 1, 2, 200000, 10.0f, 0.0f, 2,
                              2, UINT64_C(0x2222), 0,
                              WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE));
    CHECK(publish(&value, &replacement, 2, &ref));
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK, 2,
                            150000, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSampleEntityV1(
              &value.timeline, &policy, &pair, 2, &sample) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(sample.visibility ==
              WORR_SNAPSHOT_TIMELINE_VISIBILITY_GENERATION_REPLACED &&
          sample.mode == WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS &&
          sample.compatible_component_mask == 0);

    policy.allow_extrapolation = 0;
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK, 3,
                            250000, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST &&
          (pair.blocking_reasons &
           WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_POLICY) != 0);

    CHECK(finalize_projection(
        &boundary, 2, 1, 50000, 30.0f, 0.0f, 1, 99,
        UINT64_C(0x9999), WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET,
        WORR_SNAPSHOT_DISCONTINUITY_REASON_MAP_RESET));
    CHECK(publish(&value, &boundary, 4, &ref));
    CHECK(value.timeline.active_segment == 2);
    CHECK(Worr_SnapshotTimelineEventNextV1(
              &value.timeline, &cursor, &next, &observation) ==
          WORR_SNAPSHOT_TIMELINE_CURSOR_STALE);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK, 4,
                            50000, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    policy.allow_extrapolation = 1;
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_EXACT &&
          pair.segment == 2 && pair.current.slot == ref.slot);
    return true;
}

static bool test_clock_overflow_and_init_overlap(void)
{
    fixture value;
    fixture_backup backup;
    worr_snapshot_timeline_clock_request_v1 request;
    worr_snapshot_timeline_clock_state_v1 state;
    union overlap_u {
        worr_snapshot_timeline_v1 timeline;
        worr_snapshot_timeline_slot_v1 slot;
    } overlap;
    uint8_t overlap_before[sizeof(overlap)];

    CHECK(init_fixture(&value));
    request = clock_request(
        WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR, 0, UINT64_MAX - 1u,
        WORR_SNAPSHOT_TIMELINE_RATE_MAX_Q16,
        WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_INITIAL);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    backup_fixture(&value, &backup);
    memset(&state, 0x6d, sizeof(state));
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ADVANCE, 1, 0, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_CLOCK_OVERFLOW);
    CHECK(fixture_unchanged(&value, &backup));
    CHECK(((const uint8_t *)&state)[0] == UINT8_C(0x6d));

    memset(&overlap, 0x4c, sizeof(overlap));
    memcpy(overlap_before, &overlap, sizeof(overlap));
    CHECK(Worr_SnapshotTimelineInitV1(
              &overlap.timeline, &overlap.slot, 1, NULL, 0, 0, NULL, 0, 0,
              NULL, 0, 0, TEST_MAX_ENTITIES) ==
          WORR_SNAPSHOT_TIMELINE_OVERLAP);
    CHECK(memcmp(&overlap, overlap_before, sizeof(overlap)) == 0);
    return true;
}

static bool test_legacy_dedup_and_removal_visibility(void)
{
    fixture value;
    projection first;
    projection second;
    worr_snapshot_timeline_ref_v1 ref;
    worr_snapshot_timeline_event_cursor_v1 cursor;
    worr_snapshot_timeline_event_cursor_v1 next;
    worr_snapshot_timeline_event_observation_v1 observation;
    worr_snapshot_timeline_clock_request_v1 request;
    worr_snapshot_timeline_clock_state_v1 state;
    worr_snapshot_timeline_policy_v1 policy = default_policy();
    worr_snapshot_timeline_pair_v1 pair;
    worr_snapshot_timeline_entity_sample_v1 sample;

    CHECK(init_fixture(&value));
    CHECK(finalize_projection(
        &first, 1, 1, 100000, 1.0f, 0.0f, 1, 1,
        UINT64_C(0xabcdef), WORR_SNAPSHOT_DISCONTINUITY_INITIAL,
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL));
    CHECK(finalize_projection(&second, 1, 2, 200000, 2.0f, 0.0f, 1, 2,
                              UINT64_C(0xabcdef), 0,
                              WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE));
    CHECK(make_legacy_projection(&first));
    CHECK(make_legacy_projection(&second));
    CHECK(remove_projected_entity(&second));
    CHECK(publish(&value, &first, 1, &ref));
    CHECK(publish(&value, &second, 2, &ref));
    CHECK(Worr_SnapshotTimelineEventCursorBeginV1(&value.timeline,
                                                  &cursor) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineEventNextV1(
              &value.timeline, &cursor, &next, &observation) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(observation.dedup_kind ==
              WORR_SNAPSHOT_TIMELINE_EVENT_DEDUP_LEGACY_SEMANTIC &&
          observation.retained_match_count == 0);
    cursor = next;
    CHECK(Worr_SnapshotTimelineEventNextV1(
              &value.timeline, &cursor, &next, &observation) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(observation.dedup_kind ==
              WORR_SNAPSHOT_TIMELINE_EVENT_DEDUP_LEGACY_SEMANTIC &&
          observation.retained_match_count == 1);

    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR, 0, 150000,
                            WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_INITIAL);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSampleEntityV1(
              &value.timeline, &policy, &pair, 2, &sample) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(sample.visibility ==
              WORR_SNAPSHOT_TIMELINE_VISIBILITY_REMOVED_AT_CURRENT &&
          sample.visible &&
          sample.mode == WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK, 1,
                            199999, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(pair.target_time_us == 199999 &&
          pair.phase_numerator_us == 99999 &&
          pair.phase_denominator_us == 100000);
    CHECK(Worr_SnapshotTimelineSampleEntityV1(
              &value.timeline, &policy, &pair, 2, &sample) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(sample.visibility ==
              WORR_SNAPSHOT_TIMELINE_VISIBILITY_REMOVED_AT_CURRENT &&
          sample.visible &&
          sample.mode == WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS);
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK, 1,
                            200000, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSampleEntityV1(
              &value.timeline, &policy, &pair, 2, &sample) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(sample.visibility ==
              WORR_SNAPSHOT_TIMELINE_VISIBILITY_REMOVED_AT_CURRENT &&
          !sample.visible &&
          sample.mode == WORR_SNAPSHOT_TIMELINE_ENTITY_NONE);
    return true;
}

static bool test_alias_envelopes_zero_payload_and_hashes(void)
{
    fixture value;
    fixture independent;
    fixture_backup backup;
    projection input;
    projection same_id_conflict;
    worr_snapshot_timeline_ref_v1 ref;
    worr_snapshot_timeline_ref_v1 sentinel = {
        UINT32_C(0xaaaaaaaa), UINT32_C(0xbbbbbbbb)};
    worr_snapshot_projection_view_v2 malformed;
    worr_snapshot_timeline_v1 zero;
    worr_snapshot_timeline_slot_v1 zero_slot;
    worr_snapshot_v2 copied;
    worr_snapshot_timeline_ref_v1 zero_ref;
    uint32_t count = 99;
    worr_snapshot_timeline_hashes_v1 first_hashes;
    worr_snapshot_timeline_hashes_v1 second_hashes;
    worr_snapshot_timeline_v1 broken;

    CHECK(init_fixture(&value) && init_fixture(&independent));
    CHECK(finalize_projection(
        &input, 1, 1, 100000, 1.0f, 2.0f, 1, 1,
        UINT64_C(0x1111), WORR_SNAPSHOT_DISCONTINUITY_INITIAL,
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL));
    malformed = input.view;
    malformed.struct_size -= 1u;
    backup_fixture(&value, &backup);
    CHECK(Worr_SnapshotTimelinePublishV1(&value.timeline, &malformed, 1,
                                         &sentinel) ==
          WORR_SNAPSHOT_TIMELINE_INVALID_PROJECTION);
    CHECK(fixture_unchanged(&value, &backup));
    CHECK(sentinel.slot == UINT32_C(0xaaaaaaaa) &&
          sentinel.generation == UINT32_C(0xbbbbbbbb));
    CHECK(publish(&value, &input, 1, &ref));
    CHECK(publish(&independent, &input, 1, &zero_ref));
    sentinel.slot = UINT32_C(0xaaaaaaaa);
    sentinel.generation = UINT32_C(0xbbbbbbbb);
    CHECK(Worr_SnapshotTimelinePublishV1(&value.timeline, &input.view, 2,
                                         &sentinel) ==
          WORR_SNAPSHOT_TIMELINE_DUPLICATE);
    CHECK(sentinel.slot == UINT32_C(0xaaaaaaaa) &&
          sentinel.generation == UINT32_C(0xbbbbbbbb));
    CHECK(finalize_projection(
        &same_id_conflict, 1, 1, 100000, 2.0f, 2.0f, 1, 1,
        UINT64_C(0x1111), WORR_SNAPSHOT_DISCONTINUITY_INITIAL,
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL));
    CHECK(Worr_SnapshotTimelinePublishV1(
              &value.timeline, &same_id_conflict.view, 2, &sentinel) ==
          WORR_SNAPSHOT_TIMELINE_CONFLICT);
    CHECK(Worr_SnapshotTimelineHashesV1(&value.timeline, &first_hashes) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineHashesV1(&independent.timeline,
                                        &second_hashes) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(first_hashes.retained_content_hash ==
              second_hashes.retained_content_hash &&
          first_hashes.clock_hash == second_hashes.clock_hash &&
          first_hashes.telemetry_hash == second_hashes.telemetry_hash);

    CHECK(Worr_SnapshotTimelinePublishV1(
              &value.timeline, &input.view, 1,
              (worr_snapshot_timeline_ref_v1 *)&value.timeline.next_slot) ==
          WORR_SNAPSHOT_TIMELINE_OVERLAP);
    CHECK(Worr_SnapshotTimelineCopySnapshotV1(
              &value.timeline, ref,
              (worr_snapshot_v2 *)&value.slots[0]) ==
          WORR_SNAPSHOT_TIMELINE_OVERLAP);

    memset(&broken, 0, sizeof(broken));
    CHECK(Worr_SnapshotTimelineCopySnapshotV1(&broken, ref, &copied) ==
          WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE);

    memset(&zero, 0x5a, sizeof(zero));
    memset(&zero_slot, 0x5a, sizeof(zero_slot));
    CHECK(Worr_SnapshotTimelineInitV1(
              &zero, &zero_slot, 1, NULL, 0, 0, NULL, 0, 0,
              NULL, 0, 0, TEST_MAX_ENTITIES) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    input.view.entities = NULL;
    input.view.area_bytes = NULL;
    input.view.event_refs = NULL;
    input.view.entity_count = 0;
    input.view.area_byte_count = 0;
    input.view.event_ref_count = 0;
    input.snapshot.entity_range.first_serial = 0;
    input.snapshot.entity_range.count = 0;
    input.snapshot.area_range.first_serial = 0;
    input.snapshot.area_range.count = 0;
    memset(&input.snapshot.event_range, 0,
           sizeof(input.snapshot.event_range));
    CHECK(Worr_SnapshotEntityListHashV2(NULL, 0, TEST_MAX_ENTITIES,
                                        &input.snapshot.entity_hash));
    CHECK(Worr_SnapshotAreaHashV2(NULL, 0, &input.snapshot.area_hash));
    CHECK(Worr_SnapshotEventRefsHashV2(NULL, 0,
                                       &input.snapshot.event_hash));
    input.snapshot.snapshot_hash = 0;
    CHECK(Worr_SnapshotCalculateHashV2(&input.snapshot,
                                       TEST_MAX_ENTITIES,
                                       &input.snapshot.snapshot_hash));
    CHECK(Worr_SnapshotTimelinePublishV1(&zero, &input.view, 1,
                                         &zero_ref) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineCopyEntitiesV1(
              &zero, zero_ref, NULL, 0, &count) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(count == 0);
    return true;
}

static bool check_motion_guard_direct_endpoints(
    float current_origin, float current_angle, float teleport_distance,
    float max_linear_velocity, float max_angular_velocity,
    uint32_t expected_block)
{
    fixture value;
    projection previous;
    projection current;
    worr_snapshot_timeline_ref_v1 ref;
    worr_snapshot_timeline_clock_request_v1 request;
    worr_snapshot_timeline_clock_state_v1 state;
    worr_snapshot_timeline_policy_v1 policy = default_policy();
    worr_snapshot_timeline_pair_v1 pair;
    worr_snapshot_timeline_entity_sample_v1 sample;
    worr_snapshot_timeline_stats_v1 stats;

    CHECK(init_fixture(&value));
    CHECK(finalize_projection(
        &previous, 1, 1, 100000, 0.0f, 0.0f, 1, 10,
        UINT64_C(0x1111), WORR_SNAPSHOT_DISCONTINUITY_INITIAL,
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL));
    CHECK(finalize_projection(
        &current, 1, 2, 200000, current_origin, current_angle, 1, 10,
        UINT64_C(0x1111), 0,
        WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE));
    CHECK(memcmp(&previous.entities[0], &current.entities[0],
                 sizeof(previous.entities[0])) != 0);
    CHECK(publish(&value, &previous, 1, &ref));
    CHECK(publish(&value, &current, 2, &ref));

    policy.teleport_distance = teleport_distance;
    policy.max_linear_velocity = max_linear_velocity;
    policy.max_angular_velocity = max_angular_velocity;
    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR, 0,
                            150000,
                            WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_INITIAL);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE &&
          pair.blocking_reasons == 0 &&
          pair.phase_numerator_us == 50000 &&
          pair.phase_denominator_us == 100000);
    CHECK(Worr_SnapshotTimelineSampleEntityV1(
              &value.timeline, &policy, &pair, 2, &sample) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(sample.visible &&
          sample.visibility == WORR_SNAPSHOT_TIMELINE_VISIBILITY_PRESENT &&
          sample.mode == WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS &&
          sample.blocking_reasons == expected_block);
    CHECK(sample.interpolated_component_mask == 0 &&
          sample.extrapolated_component_mask == 0);
    CHECK(sample.linear_velocity[0] == 0.0f &&
          sample.linear_velocity[1] == 0.0f &&
          sample.linear_velocity[2] == 0.0f &&
          sample.angular_velocity[0] == 0.0f &&
          sample.angular_velocity[1] == 0.0f &&
          sample.angular_velocity[2] == 0.0f);
    CHECK(memcmp(&sample.entity, &previous.entities[0],
                 sizeof(sample.entity)) == 0);

    request = clock_request(WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK, 1,
                            225000, 0,
                            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER);
    CHECK(Worr_SnapshotTimelineClockApplyV1(&value.timeline, &request,
                                            &state) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineSelectPairV1(&value.timeline, &policy,
                                            &pair) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE &&
          pair.blocking_reasons == 0 &&
          pair.extrapolation_us == 25000 &&
          pair.phase_denominator_us == 100000);
    CHECK(Worr_SnapshotTimelineSampleEntityV1(
              &value.timeline, &policy, &pair, 2, &sample) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(sample.visible &&
          sample.visibility == WORR_SNAPSHOT_TIMELINE_VISIBILITY_PRESENT &&
          sample.mode == WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT &&
          sample.blocking_reasons == expected_block);
    CHECK(sample.interpolated_component_mask == 0 &&
          sample.extrapolated_component_mask == 0);
    CHECK(sample.linear_velocity[0] == 0.0f &&
          sample.linear_velocity[1] == 0.0f &&
          sample.linear_velocity[2] == 0.0f &&
          sample.angular_velocity[0] == 0.0f &&
          sample.angular_velocity[1] == 0.0f &&
          sample.angular_velocity[2] == 0.0f);
    CHECK(memcmp(&sample.entity, &current.entities[0],
                 sizeof(sample.entity)) == 0);

    CHECK(Worr_SnapshotTimelineGetStatsV1(&value.timeline, &stats) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(stats.publish_count == 2 && stats.clock_update_count == 2 &&
          stats.clock_seek_count == 1 && stats.selection_count == 2 &&
          stats.interpolation_pair_count == 1 &&
          stats.extrapolation_pair_count == 1 &&
          stats.clamped_pair_count == 0);
    return true;
}

static bool test_teleport_discontinuity_direct_endpoints(void)
{
    return check_motion_guard_direct_endpoints(
        20.0f, 0.0f, 10.0f, 1000.0f, 1000.0f,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT);
}

static bool test_linear_speed_guard_direct_endpoints(void)
{
    return check_motion_guard_direct_endpoints(
        10.0f, 0.0f, 128.0f, 50.0f, 1000.0f,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED);
}

static bool test_angular_speed_guard_direct_endpoints(void)
{
    return check_motion_guard_direct_endpoints(
        0.0f, 20.0f, 128.0f, 1000.0f, 100.0f,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_ANGULAR_SPEED);
}

int main(void)
{
    if (!test_clock_fraction_pause_seek_and_atomicity() ||
        !test_selection_sampling_events_and_seek_staleness() ||
        !test_ring_cursor_overrun_reset_and_faults() ||
        !test_boundaries_conflicts_generations_and_clamps() ||
        !test_clock_overflow_and_init_overlap() ||
        !test_legacy_dedup_and_removal_visibility() ||
        !test_alias_envelopes_zero_payload_and_hashes() ||
        !test_teleport_discontinuity_direct_endpoints() ||
        !test_linear_speed_guard_direct_endpoints() ||
        !test_angular_speed_guard_direct_endpoints()) {
        return EXIT_FAILURE;
    }
    puts("snapshot timeline tests passed");
    return EXIT_SUCCESS;
}
