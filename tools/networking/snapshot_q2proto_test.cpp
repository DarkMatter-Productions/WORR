/* Standalone FR-10-T06 Stage B public-q2proto reconstruction tests. */

#include "common/net/snapshot_q2proto.h"
#include "common/net/snapshot_timeline.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint32_t max_entities = 32;
constexpr uint32_t slot_count = 8;
constexpr uint32_t entities_per_slot = 12;
constexpr uint32_t area_per_slot = 8;
constexpr uint32_t events_per_slot = 8;
constexpr uint32_t inferred_reset_provenance =
    WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED |
    WORR_SNAPSHOT_GENERATION_EPOCH_RESET;

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr, "snapshot_q2proto_test:%d: %s\n", line,
                 expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

struct fixture_t {
    worr_snapshot_q2proto_context_v2 context{};
    std::array<worr_snapshot_q2proto_slot_v2, slot_count> slots{};
    std::array<worr_snapshot_entity_v2,
               slot_count * entities_per_slot> entities{};
    std::array<uint8_t, slot_count * area_per_slot> areas{};
    std::array<worr_snapshot_event_ref_v2,
               slot_count * events_per_slot> events{};
    std::array<worr_snapshot_q2proto_lineage_v2,
               slot_count * max_entities> lineages{};
    std::array<worr_snapshot_entity_v2, max_entities> baselines{};
    std::array<uint8_t, max_entities> baseline_present{};
    std::array<worr_snapshot_entity_v2, entities_per_slot> scratch_entities{};
    std::array<uint8_t, area_per_slot> scratch_area{};
    std::array<worr_snapshot_event_ref_v2, events_per_slot> scratch_events{};
    std::array<worr_snapshot_q2proto_lineage_v2, max_entities>
        scratch_lineage{};
};

void init_fixture(fixture_t &fixture, uint32_t epoch = 3)
{
    fixture = {};
    worr_snapshot_q2proto_profile_v2 profile{};
    profile.struct_size = sizeof(profile);
    profile.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    profile.snapshot_epoch = epoch;
    profile.max_entities = max_entities;
    profile.max_models = 64;
    profile.max_sounds = 64;
    profile.beam_renderfx_mask = UINT32_C(1) << 7;
    profile.legacy_renderfx_allowed_mask = (UINT32_C(1) << 19) - 1u;
    profile.legacy_beam_clear_mask = UINT32_C(1) << 9;
    profile.extended_entity_state = 1;

    worr_snapshot_q2proto_storage_v2 storage{};
    storage.struct_size = sizeof(storage);
    storage.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    storage.slots = fixture.slots.data();
    storage.entities = fixture.entities.data();
    storage.area_bytes = fixture.areas.data();
    storage.event_refs = fixture.events.data();
    storage.lineages = fixture.lineages.data();
    storage.baselines = fixture.baselines.data();
    storage.baseline_present = fixture.baseline_present.data();
    storage.scratch_entities = fixture.scratch_entities.data();
    storage.scratch_area_bytes = fixture.scratch_area.data();
    storage.scratch_event_refs = fixture.scratch_events.data();
    storage.scratch_lineage = fixture.scratch_lineage.data();
    storage.slot_capacity = slot_count;
    storage.entities_per_slot = entities_per_slot;
    storage.area_bytes_per_slot = area_per_slot;
    storage.event_refs_per_slot = events_per_slot;
    storage.entity_storage_capacity = fixture.entities.size();
    storage.area_storage_capacity = fixture.areas.size();
    storage.event_storage_capacity = fixture.events.size();
    storage.lineage_storage_capacity = fixture.lineages.size();
    storage.scratch_entity_capacity = fixture.scratch_entities.size();
    storage.scratch_area_capacity = fixture.scratch_area.size();
    storage.scratch_event_capacity = fixture.scratch_events.size();
    storage.scratch_lineage_capacity = fixture.scratch_lineage.size();
    CHECK(Worr_SnapshotQ2ProtoInitV2(&fixture.context, &profile, &storage) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
}

q2proto_entity_state_delta_t entity_delta(uint16_t model, uint16_t frame,
                                           uint8_t event = 0)
{
    q2proto_entity_state_delta_t delta{};
    delta.delta_bits = Q2P_ESD_MODELINDEX | Q2P_ESD_FRAME;
    delta.modelindex = model;
    delta.frame = frame;
    if (event != 0) {
        delta.delta_bits |= Q2P_ESD_EVENT;
        delta.event = event;
    }
    return delta;
}

void set_origin(q2proto_entity_state_delta_t &delta, float x, float y,
                float z)
{
    const float origin[3] = {x, y, z};
    delta.origin.read.diff_bits = 0;
    delta.origin.read.value.delta_bits = 7;
    q2proto_var_coords_set_float(&delta.origin.read.value.values, origin);
}

q2proto_svc_playerstate_t full_player()
{
    q2proto_svc_playerstate_t player{};
    player.delta_bits = Q2P_PSD_PM_GRAVITY | Q2P_PSD_FOV |
                        Q2P_PSD_PM_VIEWHEIGHT;
    player.pm_gravity = 800;
    player.pm_viewheight = 22;
    player.fov = 100;
    player.statbits = UINT64_C(1) << 2;
    player.stats[2] = 77;
    return player;
}

struct frame_carrier_t {
    q2proto_svc_frame_t frame{};
    std::array<q2proto_svc_frame_entity_delta_t, 8> deltas{};
    uint32_t count = 1;
    std::array<uint8_t, 4> area{1, 2, 3, 4};
};

frame_carrier_t make_frame(int serverframe, int deltaframe, bool full_ps)
{
    frame_carrier_t result{};
    result.frame.serverframe = serverframe;
    result.frame.deltaframe = deltaframe;
    result.frame.areabits_len = result.area.size();
    result.frame.areabits = result.area.data();
    if (full_ps)
        result.frame.playerstate = full_player();
    return result;
}

void add_delta(frame_carrier_t &frame, uint16_t entity,
               const q2proto_entity_state_delta_t &delta, bool remove = false)
{
    CHECK(frame.count < frame.deltas.size());
    auto &carrier = frame.deltas[frame.count - 1u];
    carrier.newnum = entity;
    carrier.remove = remove;
    carrier.entity_delta = delta;
    ++frame.count;
    frame.deltas[frame.count - 1u] = {};
}

worr_snapshot_ref_v2 publish(fixture_t &fixture, frame_carrier_t &carrier,
                             uint32_t flags = 0,
                             int lineage_parent = 0,
                             worr_snapshot_consumed_command_v2 consumed = {})
{
    worr_snapshot_q2proto_frame_input_v2 input{};
    input.struct_size = sizeof(input);
    input.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    input.frame = &carrier.frame;
    input.entity_deltas = carrier.deltas.data();
    input.entity_delta_count = carrier.count;
    input.flags = flags;
    input.controlled_entity_index = 1;
    input.lineage_parent_serverframe = lineage_parent;
    input.server_time_us =
        static_cast<uint64_t>(carrier.frame.serverframe) * UINT64_C(25000);
    input.consumed_command = consumed;
    worr_snapshot_ref_v2 ref{};
    const auto result = Worr_SnapshotQ2ProtoPublishV2(
        &fixture.context, &input, &ref);
    if (result != WORR_SNAPSHOT_Q2PROTO_OK) {
        std::fprintf(stderr, "snapshot publish result=%u frame=%d base=%d\n",
                     static_cast<unsigned>(result), carrier.frame.serverframe,
                     carrier.frame.deltaframe);
        fail("Worr_SnapshotQ2ProtoPublishV2 == OK", __LINE__);
    }
    return ref;
}

worr_snapshot_projection_view_v2 view(
    const fixture_t &fixture, worr_snapshot_ref_v2 ref,
    worr_snapshot_projection_hashes_v2 *hashes = nullptr)
{
    worr_snapshot_projection_view_v2 result{};
    worr_snapshot_projection_hashes_v2 local{};
    CHECK(Worr_SnapshotQ2ProtoViewV2(&fixture.context, ref, &result,
                                     hashes == nullptr ? &local : hashes) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
    return result;
}

const worr_snapshot_entity_v2 &find_entity(
    const worr_snapshot_projection_view_v2 &snapshot, uint32_t index)
{
    for (uint32_t i = 0; i < snapshot.entity_count; ++i) {
        if (snapshot.entities[i].generation.identity.index == index)
            return snapshot.entities[i];
    }
    fail("entity not found", __LINE__);
}

bool has_entity(const worr_snapshot_projection_view_v2 &snapshot,
                uint32_t index)
{
    for (uint32_t i = 0; i < snapshot.entity_count; ++i) {
        if (snapshot.entities[i].generation.identity.index == index)
            return true;
    }
    return false;
}

void check_controlled_generation(
    const worr_snapshot_projection_view_v2 &snapshot,
    uint32_t expected_provenance, bool expect_entity)
{
    const auto &controlled = snapshot.snapshot->controlled_entity;
    CHECK(controlled.identity.index == 1);
    CHECK(controlled.identity.generation == 1);
    CHECK(controlled.provenance_flags == expected_provenance);
    CHECK(std::memcmp(&controlled, &snapshot.player->controlled_entity,
                      sizeof(controlled)) == 0);
    CHECK(has_entity(snapshot, 1) == expect_entity);
    if (expect_entity) {
        CHECK(std::memcmp(&controlled, &find_entity(snapshot, 1).generation,
                          sizeof(controlled)) == 0);
    }
}

struct timeline_fixture_t {
    static constexpr uint32_t slots = 4;
    worr_snapshot_timeline_v1 timeline{};
    std::array<worr_snapshot_timeline_slot_v1, slots> slot_storage{};
    std::array<worr_snapshot_entity_v2, slots * entities_per_slot>
        entity_storage{};
    std::array<uint8_t, slots * area_per_slot> area_storage{};
    std::array<worr_snapshot_event_ref_v2, slots * events_per_slot>
        event_storage{};
};

void init_timeline(timeline_fixture_t &fixture)
{
    fixture = {};
    CHECK(Worr_SnapshotTimelineInitV1(
              &fixture.timeline, fixture.slot_storage.data(),
              fixture.slot_storage.size(), fixture.entity_storage.data(),
              fixture.entity_storage.size(), entities_per_slot,
              fixture.area_storage.data(), fixture.area_storage.size(),
              area_per_slot, fixture.event_storage.data(),
              fixture.event_storage.size(), events_per_slot, max_entities) ==
          WORR_SNAPSHOT_TIMELINE_OK);
}

void publish_timeline(timeline_fixture_t &fixture,
                      const worr_snapshot_projection_view_v2 &snapshot)
{
    worr_snapshot_timeline_ref_v1 ref{};
    CHECK(Worr_SnapshotTimelinePublishV1(
              &fixture.timeline, &snapshot, 1, &ref) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(Worr_SnapshotTimelineRefValidV1(&fixture.timeline, ref));
}

void test_reconstruction_lineage_and_branch_bases()
{
    fixture_t fixture;
    init_fixture(fixture);

    auto baseline = entity_delta(7, 1);
    CHECK(Worr_SnapshotQ2ProtoSetBaselineV2(&fixture.context, 5,
                                             &baseline) ==
          WORR_SNAPSHOT_Q2PROTO_OK);

    auto frame10 = make_frame(10, -1, true);
    auto entity2 = entity_delta(2, 1,
        WORR_EVENT_LEGACY_ENTITY_FOOTSTEP);
    set_origin(entity2, 10.0f, 20.0f, 30.0f);
    auto entity4 = entity_delta(4, 2);
    set_origin(entity4, 4.0f, 5.0f, 6.0f);
    add_delta(frame10, 2, entity2);
    add_delta(frame10, 4, entity4);
    const auto ref10 = publish(
        fixture, frame10, WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);
    const auto view10 = view(fixture, ref10);
    CHECK(view10.snapshot->snapshot_id.sequence == 11);
    CHECK((view10.snapshot->discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH) != 0);
    CHECK(view10.entity_count == 2 && view10.event_ref_count == 1);
    CHECK(view10.event_refs[0].provenance ==
          WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED);
    CHECK(view10.event_refs[0].authority_id.stream_epoch == 0);
    CHECK(find_entity(view10, 2).generation.identity.generation == 1);

    auto frame11 = make_frame(11, 10, false);
    add_delta(frame11, 2, {}, true);
    auto update4 = entity_delta(4, 3,
        WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT);
    set_origin(update4, 8.0f, 9.0f, 10.0f);
    add_delta(frame11, 4, update4);
    auto from_baseline = entity_delta(0, 5);
    from_baseline.delta_bits = Q2P_ESD_FRAME;
    add_delta(frame11, 5, from_baseline);
    const auto ref11 = publish(fixture, frame11);
    const auto view11 = view(fixture, ref11);
    CHECK(view11.player->fov == 100.0f &&
          view11.player->movement.gravity == 800 &&
          view11.player->stats[2] == 77);
    CHECK(view11.entity_count == 2 && view11.event_ref_count == 1);
    CHECK(find_entity(view11, 4).old_origin[0] == 4.0f);
    CHECK(find_entity(view11, 5).model_index[0] == 7);

    /* Packet loss/branch base: this frame deliberately branches from 10,
     * retaining entity 2 even though chronological frame 11 removed it. */
    auto frame12 = make_frame(12, 10, false);
    add_delta(frame12, 6, entity_delta(6, 1));
    const auto ref12 = publish(fixture, frame12);
    const auto view12 = view(fixture, ref12);
    CHECK(view12.entity_count == 3 && view12.event_ref_count == 0);
    CHECK(find_entity(view12, 2).generation.identity.generation == 1);
    /* A q2proto-omitted unchanged entity follows the legacy parser's null
     * delta path: non-beam old_origin advances to the retained origin. */
    CHECK(find_entity(view12, 4).origin[0] == 4.0f);
    CHECK(find_entity(view12, 4).old_origin[0] == 4.0f);
    CHECK(find_entity(view(fixture, ref10), 4).old_origin[0] == 0.0f);
    CHECK((view12.snapshot->discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_BASE_JUMP) != 0);
    CHECK(view12.snapshot->discontinuity.reason ==
          WORR_SNAPSHOT_DISCONTINUITY_REASON_BASE_JUMP);

    auto frame13 = make_frame(13, 11, false);
    add_delta(frame13, 2, entity_delta(2, 9));
    const auto ref13 = publish(fixture, frame13);
    const auto view13 = view(fixture, ref13);
    CHECK(find_entity(view13, 2).generation.identity.generation == 2);

    auto frame14 = make_frame(14, 12, false);
    auto update2 = entity_delta(2, 10);
    add_delta(frame14, 2, update2);
    const auto ref14 = publish(fixture, frame14);
    const auto view14 = view(fixture, ref14);
    CHECK(find_entity(view14, 2).generation.identity.generation == 1);

    auto frame15 = make_frame(15, -1, true);
    add_delta(frame15, 2, entity_delta(2, 11));
    const auto ref15 = publish(
        fixture, frame15,
        WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID, 14);
    CHECK(find_entity(view(fixture, ref15), 2)
              .generation.identity.generation == 1);

    auto frame16 = make_frame(16, -1, true);
    add_delta(frame16, 2, entity_delta(2, 12));
    const auto ref16 = publish(fixture, frame16);
    const auto &reset_entity = find_entity(view(fixture, ref16), 2);
    CHECK(reset_entity.generation.identity.generation == 1);
    CHECK((reset_entity.generation.provenance_flags &
           WORR_SNAPSHOT_GENERATION_EPOCH_RESET) != 0);
}

void test_truncation_malformed_and_atomic_failures()
{
    fixture_t fixture;
    init_fixture(fixture, 8);
    auto frame20 = make_frame(20, -1, true);
    add_delta(frame20, 2, entity_delta(2, 1));
    const auto ref20 = publish(
        fixture, frame20, WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);
    (void)ref20;

    const auto context_before = fixture.context;
    const auto slots_before = fixture.slots;
    const auto entities_before = fixture.entities;
    const auto lineages_before = fixture.lineages;
    auto invalid_base = make_frame(21, 19, false);
    worr_snapshot_q2proto_frame_input_v2 input{};
    input.struct_size = sizeof(input);
    input.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    input.frame = &invalid_base.frame;
    input.entity_deltas = invalid_base.deltas.data();
    input.entity_delta_count = invalid_base.count;
    input.controlled_entity_index = 1;
    input.server_time_us = 525000;
    worr_snapshot_ref_v2 sentinel{77, 88};
    CHECK(Worr_SnapshotQ2ProtoPublishV2(&fixture.context, &input,
                                        &sentinel) ==
          WORR_SNAPSHOT_Q2PROTO_INVALID_BASE);
    CHECK(sentinel.slot == 77 && sentinel.generation == 88);
    CHECK(std::memcmp(&fixture.context, &context_before,
                      sizeof(context_before)) == 0);
    CHECK(std::memcmp(fixture.slots.data(), slots_before.data(),
                      sizeof(slots_before)) == 0);
    CHECK(std::memcmp(fixture.entities.data(), entities_before.data(),
                      sizeof(entities_before)) == 0);
    CHECK(std::memcmp(fixture.lineages.data(), lineages_before.data(),
                      sizeof(lineages_before)) == 0);

    auto malformed = make_frame(21, 20, false);
    add_delta(malformed, 4, entity_delta(4, 1));
    add_delta(malformed, 3, entity_delta(3, 1));
    input.frame = &malformed.frame;
    input.entity_deltas = malformed.deltas.data();
    input.entity_delta_count = malformed.count;
    CHECK(Worr_SnapshotQ2ProtoPublishV2(&fixture.context, &input,
                                        &sentinel) ==
          WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY_ORDER);
    CHECK(std::memcmp(fixture.slots.data(), slots_before.data(),
                      sizeof(slots_before)) == 0);

    auto truncated = make_frame(21, 20, false);
    const auto ref21 = publish(
        fixture, truncated,
        WORR_SNAPSHOT_Q2PROTO_FRAME_TRANSPORT_TRUNCATED);
    worr_snapshot_projection_hashes_v2 hashes{};
    const auto truncated_view = view(fixture, ref21, &hashes);
    CHECK((truncated_view.snapshot->flags &
           WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED) != 0);
    CHECK((truncated_view.snapshot->flags &
           WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE) == 0);
    CHECK((truncated_view.snapshot->discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED) != 0);
    CHECK(hashes.endpoint_hash != 0 && hashes.legacy_parity_hash != 0);

    /* Server-only transport diagnostics are endpoint state, not data a
     * legacy receiver can independently reconstruct. */
    worr_snapshot_v2 without_transport = *truncated_view.snapshot;
    without_transport.flags &= ~WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED;
    without_transport.discontinuity.flags &=
        ~WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED;
    without_transport.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE;
    CHECK(Worr_SnapshotCalculateHashV2(
        &without_transport, max_entities,
        &without_transport.snapshot_hash));
    auto comparison = truncated_view;
    comparison.snapshot = &without_transport;
    worr_snapshot_projection_hashes_v2 comparison_hashes{};
    CHECK(Worr_SnapshotProjectionHashesV2(
        &comparison, max_entities, &comparison_hashes));
    CHECK(comparison_hashes.endpoint_hash != hashes.endpoint_hash);
    CHECK(comparison_hashes.legacy_parity_hash ==
          hashes.legacy_parity_hash);

    CHECK(Worr_SnapshotQ2ProtoResetV2(&fixture.context, 9) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
    worr_snapshot_projection_view_v2 stale_view{};
    CHECK(Worr_SnapshotQ2ProtoViewV2(&fixture.context, ref21, &stale_view,
                                     &hashes) ==
          WORR_SNAPSHOT_Q2PROTO_STALE_REF);
}

void test_epoch_rebind_preserves_refs_and_delta_bases()
{
    fixture_t fixture;
    init_fixture(fixture, 3);

    auto frame10 = make_frame(10, -1, true);
    add_delta(frame10, 2, entity_delta(2, 1));
    const auto ref10 = publish(
        fixture, frame10, WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);
    auto frame11 = make_frame(11, 10, false);
    add_delta(frame11, 2, entity_delta(2, 2));
    const auto ref11 = publish(fixture, frame11);
    worr_snapshot_projection_hashes_v2 old_hashes{};
    const auto old_view = view(fixture, ref11, &old_hashes);
    CHECK(old_view.snapshot->snapshot_id.epoch == 3);
    CHECK(old_view.snapshot->base_id.epoch == 3);

    CHECK(Worr_SnapshotQ2ProtoRebindEpochV2(&fixture.context, 9) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
    CHECK(fixture.context.profile.snapshot_epoch == 9);
    CHECK(fixture.context.last_observed.epoch == 9);
    const auto rebound10 = view(fixture, ref10);
    worr_snapshot_projection_hashes_v2 rebound_hashes{};
    const auto rebound11 = view(fixture, ref11, &rebound_hashes);
    CHECK(rebound10.snapshot->snapshot_id.epoch == 9);
    CHECK(rebound10.snapshot->base_id.epoch == 0);
    CHECK(rebound11.snapshot->snapshot_id.epoch == 9);
    CHECK(rebound11.snapshot->base_id.epoch == 9);
    CHECK(rebound11.snapshot->discontinuity.previous.epoch == 9);
    CHECK(rebound11.snapshot->snapshot_id.sequence ==
          old_view.snapshot->snapshot_id.sequence);
    CHECK(rebound_hashes.endpoint_hash != old_hashes.endpoint_hash);
    CHECK(rebound_hashes.legacy_parity_hash !=
          old_hashes.legacy_parity_hash);

    auto frame12 = make_frame(12, 11, false);
    add_delta(frame12, 3, entity_delta(3, 1));
    const auto ref12 = publish(fixture, frame12);
    const auto rebound12 = view(fixture, ref12);
    CHECK(rebound12.snapshot->snapshot_id.epoch == 9);
    CHECK(rebound12.snapshot->base_id.epoch == 9);
    CHECK(rebound12.snapshot->base_id.sequence == 12);
    CHECK(find_entity(rebound12, 2).frame == 2);

    const auto before_same_epoch = fixture;
    CHECK(Worr_SnapshotQ2ProtoRebindEpochV2(&fixture.context, 9) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
    CHECK(std::memcmp(&fixture, &before_same_epoch, sizeof(fixture)) == 0);
    CHECK(Worr_SnapshotQ2ProtoRebindEpochV2(&fixture.context, 0) ==
          WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
    CHECK(std::memcmp(&fixture, &before_same_epoch, sizeof(fixture)) == 0);
}

void test_controlled_entity_lineage_without_first_person_carrier()
{
    fixture_t fixture;
    timeline_fixture_t timeline;
    init_fixture(fixture, 9);
    init_timeline(timeline);

    auto frame40 = make_frame(40, -1, true);
    add_delta(frame40, 2, entity_delta(2, 1));
    const auto ref40 = publish(
        fixture, frame40, WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);
    const auto view40 = view(fixture, ref40);
    check_controlled_generation(
        view40, inferred_reset_provenance, false);
    publish_timeline(timeline, view40);

    /* Playerstate proves entity 1's lifecycle exists even though the
     * first-person entity was omitted from frame 40's entity range. Its first
     * later carrier is therefore an update to generation 1, not a reuse, and
     * retains the provenance of that playerstate assignment. */
    auto frame41 = make_frame(41, 40, false);
    add_delta(frame41, 1, entity_delta(1, 2));
    const auto ref41 = publish(fixture, frame41);
    const auto view41 = view(fixture, ref41);
    check_controlled_generation(
        view41, inferred_reset_provenance, true);
    publish_timeline(timeline, view41);
}

void test_initial_controlled_carrier_generation_and_timeline_acceptance()
{
    fixture_t fixture;
    timeline_fixture_t timeline;
    init_fixture(fixture, 12);
    init_timeline(timeline);

    auto frame0 = make_frame(40, -1, true);
    add_delta(frame0, 1, entity_delta(3, 1));
    const auto ref0 = publish(
        fixture, frame0, WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);
    const auto view0 = view(fixture, ref0);
    check_controlled_generation(
        view0, inferred_reset_provenance, true);
    publish_timeline(timeline, view0);

    /* A later delta can carry the controlled entity unchanged. Its original
     * epoch-reset assignment provenance must remain identical in the entity,
     * player, and snapshot identities; otherwise native snapshot preflight
     * rejects an otherwise valid projection as CONTROLLED_ENTITY_MISMATCH. */
    auto frame1 = make_frame(41, 40, false);
    add_delta(frame1, 2, entity_delta(4, 2));
    const auto ref1 = publish(fixture, frame1);
    const auto view1 = view(fixture, ref1);
    check_controlled_generation(
        view1, inferred_reset_provenance, true);
    publish_timeline(timeline, view1);
}

void test_controlled_generation_provenance_survives_explicit_delta()
{
    fixture_t fixture;
    timeline_fixture_t timeline;
    init_fixture(fixture, 13);
    init_timeline(timeline);

    auto frame40 = make_frame(40, -1, true);
    add_delta(frame40, 1, entity_delta(3, 1));
    const auto ref40 = publish(
        fixture, frame40, WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);
    const auto view40 = view(fixture, ref40);
    check_controlled_generation(
        view40, inferred_reset_provenance, true);
    publish_timeline(timeline, view40);

    /* An explicit entity delta changes components, not the generation
     * assignment that established this controlled lifecycle. */
    auto frame41 = make_frame(41, 40, false);
    add_delta(frame41, 1, entity_delta(5, 2));
    const auto ref41 = publish(fixture, frame41);
    const auto view41 = view(fixture, ref41);
    CHECK(find_entity(view41, 1).model_index[0] == 5);
    check_controlled_generation(
        view41, inferred_reset_provenance, true);
    publish_timeline(timeline, view41);
}

void test_controlled_generation_provenance_survives_lineage_keyframes()
{
    fixture_t fixture;
    timeline_fixture_t timeline;
    init_fixture(fixture, 14);
    init_timeline(timeline);

    auto frame40 = make_frame(40, -1, true);
    add_delta(frame40, 1, entity_delta(3, 1));
    const auto ref40 = publish(
        fixture, frame40, WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);
    const auto view40 = view(fixture, ref40);
    check_controlled_generation(
        view40, inferred_reset_provenance, true);
    publish_timeline(timeline, view40);

    /* A full frame uses its parent only for generation lineage. A carried
     * controlled entity still inherits that lifecycle's exact provenance. */
    auto frame41 = make_frame(41, -1, true);
    add_delta(frame41, 1, entity_delta(6, 2));
    const auto ref41 = publish(
        fixture, frame41,
        WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID, 40);
    const auto view41 = view(fixture, ref41);
    check_controlled_generation(
        view41, inferred_reset_provenance, true);
    publish_timeline(timeline, view41);

    /* The same exact record must also survive a generation-only keyframe
     * whose packet-entity range omits the first-person entity entirely. */
    auto frame42 = make_frame(42, -1, true);
    add_delta(frame42, 2, entity_delta(7, 3));
    const auto ref42 = publish(
        fixture, frame42,
        WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID, 41);
    const auto view42 = view(fixture, ref42);
    check_controlled_generation(
        view42, inferred_reset_provenance, false);
    publish_timeline(timeline, view42);
}

void test_fragment_stall_discontinuity()
{
    fixture_t fixture;
    init_fixture(fixture, 10);

    auto frame50 = make_frame(50, -1, true);
    add_delta(frame50, 2, entity_delta(2, 1));
    (void)publish(
        fixture, frame50, WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);

    /* The adapter is told the missing sequence was a server-side fragment
     * stall. The generic gap remains the primary reason; the cause is retained
     * as an additional discontinuity flag. */
    auto frame52 = make_frame(52, 50, false);
    const auto ref52 = publish(
        fixture, frame52, WORR_SNAPSHOT_Q2PROTO_FRAME_FRAGMENT_STALL);
    const auto view52 = view(fixture, ref52);
    CHECK((view52.snapshot->discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP) != 0);
    CHECK((view52.snapshot->discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_FRAGMENT_STALL) != 0);
    CHECK(view52.snapshot->discontinuity.reason ==
          WORR_SNAPSHOT_DISCONTINUITY_REASON_SEQUENCE_GAP);
    CHECK(view52.snapshot->discontinuity.skipped_sequences == 1);
}

void test_hostile_alias_rejection()
{
    fixture_t fixture;
    init_fixture(fixture, 11);

    std::array<uint8_t, sizeof(fixture_t)> bytes_before{};
    std::memcpy(bytes_before.data(), &fixture, sizeof(fixture));

    /* Initialization must reject an output context placed inside one of the
     * caller-owned arenas before clearing slots or baselines. */
    auto *arena_context =
        reinterpret_cast<worr_snapshot_q2proto_context_v2 *>(
            fixture.slots.data());
    CHECK(Worr_SnapshotQ2ProtoInitV2(
              arena_context, &fixture.context.profile,
              &fixture.context.storage) ==
          WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
    CHECK(std::memcmp(bytes_before.data(), &fixture, sizeof(fixture)) == 0);

    auto frame60 = make_frame(60, -1, true);
    add_delta(frame60, 2, entity_delta(2, 1));
    worr_snapshot_q2proto_frame_input_v2 input{};
    input.struct_size = sizeof(input);
    input.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    input.frame = &frame60.frame;
    input.entity_deltas = frame60.deltas.data();
    input.entity_delta_count = frame60.count;
    input.flags = WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH;
    input.controlled_entity_index = 1;
    input.server_time_us = UINT64_C(1500000);

    /* Publication must not let its process-local ref overwrite the context. */
    auto *context_ref = reinterpret_cast<worr_snapshot_ref_v2 *>(
        &fixture.context);
    CHECK(Worr_SnapshotQ2ProtoPublishV2(
              &fixture.context, &input, context_ref) ==
          WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
    CHECK(std::memcmp(bytes_before.data(), &fixture, sizeof(fixture)) == 0);

    const auto valid_ref = publish(
        fixture, frame60, WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);
    std::memcpy(bytes_before.data(), &fixture, sizeof(fixture));
    worr_snapshot_projection_hashes_v2 hashes{
        77, 88, 99, 111, 222, 333, 444, 555};
    const auto hashes_before = hashes;

    /* View copy-out has the same rule: outputs cannot alias context/storage. */
    auto *context_view =
        reinterpret_cast<worr_snapshot_projection_view_v2 *>(
            &fixture.context);
    CHECK(Worr_SnapshotQ2ProtoViewV2(
              &fixture.context, valid_ref, context_view, &hashes) ==
          WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
    CHECK(std::memcmp(bytes_before.data(), &fixture, sizeof(fixture)) == 0);
    CHECK(std::memcmp(&hashes, &hashes_before, sizeof(hashes)) == 0);
}

void test_consumed_cursor_validation_and_attach_contract()
{
    worr_snapshot_v2 snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    snapshot.flags = WORR_SNAPSHOT_FLAG_COMPLETE |
                     WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION;
    snapshot.snapshot_id = {4, 99};
    snapshot.base_id = {4, 98};
    snapshot.controlled_entity = {
        {1, 1}, WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED, 0};
    snapshot.discontinuity.flags =
        WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH;
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_OBSERVER_ATTACH;
    snapshot.player_hash = 1;
    snapshot.entity_hash = 2;
    snapshot.area_hash = 3;
    snapshot.event_hash = 4;
    uint64_t hash;
    CHECK(Worr_SnapshotCalculateHashV2(&snapshot, max_entities, &hash));
    snapshot.consumed_command.cursor = {7, 12};
    snapshot.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    CHECK(Worr_SnapshotCalculateHashV2(&snapshot, max_entities, &hash));
    snapshot.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_NONE;
    CHECK(!Worr_SnapshotCalculateHashV2(&snapshot, max_entities, &hash));

    fixture_t fixture;
    init_fixture(fixture, 8);
    auto frame = make_frame(1, -1, true);
    add_delta(frame, 2, entity_delta(2, 1));
    worr_snapshot_consumed_command_v2 consumed{};
    consumed.cursor = {77, 9};
    consumed.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    const auto consumed_ref = publish(
        fixture, frame,
        WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH, 0, consumed);
    const auto consumed_view = view(fixture, consumed_ref);
    CHECK(consumed_view.snapshot->consumed_command.cursor.epoch == 77);
    CHECK(consumed_view.snapshot->consumed_command.cursor
              .contiguous_sequence == 9);
    CHECK(consumed_view.snapshot->consumed_command.provenance ==
          WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED);

    auto malformed = make_frame(2, 1, false);
    worr_snapshot_q2proto_frame_input_v2 input{};
    input.struct_size = sizeof(input);
    input.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    input.frame = &malformed.frame;
    input.entity_deltas = malformed.deltas.data();
    input.entity_delta_count = malformed.count;
    input.controlled_entity_index = 1;
    input.server_time_us = 50000;
    input.consumed_command.cursor = {77, 10};
    input.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_NONE;
    worr_snapshot_ref_v2 sentinel{91, 92};
    CHECK(Worr_SnapshotQ2ProtoPublishV2(
              &fixture.context, &input, &sentinel) ==
          WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT);
    CHECK(sentinel.slot == 91 && sentinel.generation == 92);
}

void test_endpoint_and_legacy_parity_domains()
{
    fixture_t fixture;
    init_fixture(fixture, 12);
    auto frame = make_frame(30, -1, true);
    add_delta(frame, 2, entity_delta(
        2, 1, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP));
    const auto original_ref = publish(
        fixture, frame, WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);
    worr_snapshot_projection_hashes_v2 original_hashes{};
    const auto original = view(fixture, original_ref, &original_hashes);

    worr_snapshot_v2 snapshot = *original.snapshot;
    worr_snapshot_player_v2 player = *original.player;
    std::array<worr_snapshot_entity_v2, entities_per_slot> entities{};
    std::array<worr_snapshot_event_ref_v2, events_per_slot> events{};
    std::memcpy(entities.data(), original.entities,
                sizeof(entities[0]) * original.entity_count);
    std::memcpy(events.data(), original.event_refs,
                sizeof(events[0]) * original.event_ref_count);

    snapshot.flags |= WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS;
    snapshot.server_time_us += 999;
    snapshot.consumed_command.cursor = {3, 44};
    snapshot.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    snapshot.controlled_entity.provenance_flags =
        WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    player.controlled_entity.provenance_flags =
        WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    for (uint32_t i = 0; i < original.entity_count; ++i) {
        entities[i].generation.provenance_flags =
            WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    }
    for (uint32_t i = 0; i < original.event_ref_count; ++i) {
        events[i].provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
        events[i].authority_id = {9, i + 1u};
    }
    snapshot.event_range.provenance =
        WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
    snapshot.event_range.flags =
        WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY |
        WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER;
    snapshot.event_range.first_authority_id = {9, 1};
    snapshot.event_range.one_past_authority_id = {
        9, original.event_ref_count + 1u};
    CHECK(Worr_SnapshotPlayerHashV2(&player, max_entities,
                                     &snapshot.player_hash));
    CHECK(Worr_SnapshotEntityListHashV2(
        entities.data(), original.entity_count, max_entities,
        &snapshot.entity_hash));
    CHECK(Worr_SnapshotEventRefsHashV2(
        events.data(), original.event_ref_count, &snapshot.event_hash));
    CHECK(Worr_SnapshotCalculateHashV2(&snapshot, max_entities,
                                        &snapshot.snapshot_hash));

    worr_snapshot_projection_view_v2 comparison{};
    comparison.struct_size = sizeof(comparison);
    comparison.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    comparison.snapshot = &snapshot;
    comparison.player = &player;
    comparison.entities = entities.data();
    comparison.area_bytes = original.area_bytes;
    comparison.event_refs = events.data();
    comparison.entity_count = original.entity_count;
    comparison.area_byte_count = original.area_byte_count;
    comparison.event_ref_count = original.event_ref_count;
    worr_snapshot_projection_hashes_v2 comparison_hashes{};
    CHECK(Worr_SnapshotProjectionHashesV2(
        &comparison, max_entities, &comparison_hashes));
    CHECK(comparison_hashes.endpoint_hash != original_hashes.endpoint_hash);
    CHECK(comparison_hashes.legacy_parity_hash ==
          original_hashes.legacy_parity_hash);
}

} // namespace

int main()
{
    test_reconstruction_lineage_and_branch_bases();
    test_truncation_malformed_and_atomic_failures();
    test_epoch_rebind_preserves_refs_and_delta_bases();
    test_controlled_entity_lineage_without_first_person_carrier();
    test_initial_controlled_carrier_generation_and_timeline_acceptance();
    test_controlled_generation_provenance_survives_explicit_delta();
    test_controlled_generation_provenance_survives_lineage_keyframes();
    test_fragment_stall_discontinuity();
    test_hostile_alias_rejection();
    test_consumed_cursor_validation_and_attach_contract();
    test_endpoint_and_legacy_parity_domains();
    std::puts("snapshot_q2proto_test: ok");
    return 0;
}
