/* Focused FR-10-T07 cgame-owned canonical entity-range copy coverage. */

#include "cg_canonical_snapshot_timeline.hpp"
#include "cg_event_runtime.hpp"
#include "cg_local.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

cgame_import_t cgi{};

namespace {

constexpr std::uint32_t kMaxEntities = 64u;
constexpr std::uint32_t kSnapshotEpoch = 71u;

std::uint32_t event_snapshot_epoch{};

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr,
                 "cgame_canonical_snapshot_copy_entities_test:%d: %s\n",
                 line, expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

worr_snapshot_entity_generation_v2 generation(std::uint32_t index,
                                                std::uint32_t value)
{
    worr_snapshot_entity_generation_v2 result{};
    result.identity.index = index;
    result.identity.generation = value;
    result.provenance_flags = WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    return result;
}

worr_snapshot_player_v2 make_player()
{
    worr_snapshot_player_v2 player{};
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.controlled_entity = generation(1u, 1u);
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    player.movement.gravity = 800;
    player.movement.view_height = 22;
    player.fov = 100.0f;
    return player;
}

worr_snapshot_entity_v2 make_entity(std::uint32_t index, float origin)
{
    worr_snapshot_entity_v2 entity{};
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.generation = generation(index, 1u);
    entity.component_mask = WORR_SNAPSHOT_ENTITY_COMPONENTS_V2;
    entity.origin[0] = origin;
    entity.old_origin[0] = origin - 1.0f;
    entity.model_index[0] = static_cast<std::uint16_t>(index + 1u);
    entity.frame = 4u;
    entity.sound = 5u;
    entity.skin = 6u;
    entity.solid = 7u;
    entity.effects = 8u;
    entity.renderfx = 9u;
    entity.alpha = 1.0f;
    entity.scale = 1.0f;
    entity.loop_volume = 1.0f;
    entity.loop_attenuation = 1.0f;
    entity.owner.index = 1u;
    entity.owner.generation = 1u;
    entity.old_frame = 3u;
    return entity;
}

struct projection_t {
    worr_snapshot_v2 snapshot{};
    worr_snapshot_player_v2 player{};
    std::array<worr_snapshot_entity_v2, 2> entities{};
    std::array<std::uint8_t,
               WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES> area{};
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
};

static_assert(CG_CANONICAL_SNAPSHOT_TIMELINE_AREA_CAPACITY ==
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES);

void initialize_projection(projection_t &result)
{
    result = {};
    result.player = make_player();
    result.entities[0] = make_entity(2u, 16.0f);
    result.entities[1] = make_entity(3u, 32.0f);
    for (std::size_t index = 0; index < result.area.size(); ++index)
        result.area[index] = static_cast<std::uint8_t>(index);

    auto &snapshot = result.snapshot;
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    snapshot.flags = WORR_SNAPSHOT_FLAG_COMPLETE |
                     WORR_SNAPSHOT_FLAG_KEYFRAME |
                     WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE |
                     WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS;
    snapshot.snapshot_id = {kSnapshotEpoch, 1u};
    snapshot.server_tick = 1u;
    snapshot.server_time_us = UINT64_C(100000);
    snapshot.controlled_entity = generation(1u, 1u);
    snapshot.entity_range.first_serial = 1u;
    snapshot.entity_range.count =
        static_cast<std::uint32_t>(result.entities.size());
    snapshot.area_range.first_serial = 3u;
    snapshot.area_range.count =
        static_cast<std::uint32_t>(result.area.size());
    snapshot.discontinuity.flags =
        WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
        WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;

    CHECK(Worr_SnapshotPlayerHashV2(
        &result.player, kMaxEntities, &snapshot.player_hash));
    CHECK(Worr_SnapshotEntityListHashV2(
        result.entities.data(),
        static_cast<std::uint32_t>(result.entities.size()),
        kMaxEntities, &snapshot.entity_hash));
    CHECK(Worr_SnapshotAreaHashV2(
        result.area.data(), static_cast<std::uint32_t>(result.area.size()),
        &snapshot.area_hash));
    CHECK(Worr_SnapshotEventRefsHashV2(nullptr, 0u,
                                      &snapshot.event_hash));
    CHECK(Worr_SnapshotCalculateHashV2(
        &snapshot, kMaxEntities, &snapshot.snapshot_hash));

    result.view.struct_size = sizeof(result.view);
    result.view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    result.view.snapshot = &result.snapshot;
    result.view.player = &result.player;
    result.view.entities = result.entities.data();
    result.view.area_bytes = result.area.data();
    result.view.entity_count =
        static_cast<std::uint32_t>(result.entities.size());
    result.view.area_byte_count =
        static_cast<std::uint32_t>(result.area.size());
    CHECK(Worr_SnapshotProjectionHashesV2(
        &result.view, kMaxEntities, &result.hashes));
}

void set_projection_identity(projection_t &result,
                             std::uint32_t epoch,
                             std::uint32_t sequence)
{
    result.snapshot.snapshot_id = {epoch, sequence};
    result.snapshot.server_tick = sequence;
    result.snapshot.server_time_us =
        static_cast<std::uint64_t>(sequence) * UINT64_C(16000);
    if (sequence == 1u) {
        result.snapshot.discontinuity.flags =
            WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
            WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
        result.snapshot.discontinuity.reason =
            WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
    } else {
        result.snapshot.discontinuity = {};
        result.snapshot.discontinuity.flags =
            WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
        result.snapshot.discontinuity.reason =
            WORR_SNAPSHOT_DISCONTINUITY_REASON_FULL_SNAPSHOT;
        result.snapshot.discontinuity.previous = {epoch, sequence - 1u};
        result.snapshot.discontinuity.server_tick_delta = 1u;
    }
    result.snapshot.snapshot_hash = 0;
    CHECK(Worr_SnapshotCalculateHashV2(
        &result.snapshot, kMaxEntities,
        &result.snapshot.snapshot_hash));
    CHECK(Worr_SnapshotProjectionHashesV2(
        &result.view, kMaxEntities, &result.hashes));
}

struct copy_outputs_t {
    std::array<worr_snapshot_entity_v2, 2> entities{};
    std::uint32_t count{};
};

copy_outputs_t sentinel_outputs()
{
    copy_outputs_t result;
    std::memset(&result, 0xa5, sizeof(result));
    result.count = UINT32_C(0xabcdef01);
    return result;
}

void check_sentinel(const copy_outputs_t &actual)
{
    std::array<worr_snapshot_entity_v2, 2> expected{};
    std::memset(expected.data(), 0xa5, sizeof(expected));
    CHECK(std::memcmp(actual.entities.data(), expected.data(),
                      sizeof(expected)) == 0);
    CHECK(actual.count == UINT32_C(0xabcdef01));
}

} // namespace

cg_event_runtime_result_v1
CG_EventRuntimeResetSnapshot(std::uint32_t snapshot_epoch)
{
    event_snapshot_epoch = snapshot_epoch;
    return CG_EVENT_RUNTIME_OK;
}

cg_event_runtime_result_v1 CG_EventRuntimeObserveSnapshot(
    const worr_snapshot_v2 *snapshot,
    const worr_snapshot_event_ref_v2 *event_refs,
    std::uint32_t event_ref_count)
{
    if (!snapshot || snapshot->snapshot_id.epoch != event_snapshot_epoch ||
        event_refs || event_ref_count != 0u) {
        return CG_EVENT_RUNTIME_INVALID_ARGUMENT;
    }
    return CG_EVENT_RUNTIME_EMPTY;
}

bool CG_EventRuntimeSnapshotFenceHealthy(std::uint32_t snapshot_epoch)
{
    return snapshot_epoch != 0u && snapshot_epoch == event_snapshot_epoch;
}

int main()
{
    const auto *api = CG_GetCanonicalSnapshotTimelineAPI();
    CHECK(api != nullptr);

    worr_snapshot_timeline_ref_v1 ref{
        WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT, 0u};
    auto inactive = sentinel_outputs();
    CHECK(CG_CanonicalSnapshotTimelineCopyEntities(
              ref, inactive.entities.data(),
              static_cast<std::uint32_t>(inactive.entities.size()),
              &inactive.count) == WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE);
    check_sentinel(inactive);

    api->Reset(kSnapshotEpoch, WORR_CGAME_SNAPSHOT_RESET_CONNECTION,
               UINT64_C(1000));
    projection_t projection{};
    initialize_projection(projection);
    CHECK(projection.view.area_byte_count ==
          WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES);
    CHECK(api->ConsumeCanonicalSnapshot(
        &projection.view, &projection.hashes, UINT64_C(2000)));

    cg_canonical_snapshot_timeline_diagnostics_v1 diagnostics{};
    CHECK(CG_CanonicalSnapshotTimelineGetDiagnostics(&diagnostics));
    CHECK(diagnostics.active != 0u);
    ref = diagnostics.latest_ref;

    worr_snapshot_timeline_ref_v1 exact_ref{
        WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT, UINT32_C(0xfeedbeef)};
    CHECK(CG_CanonicalSnapshotTimelineFindSnapshot(
              projection.snapshot.snapshot_id, &exact_ref) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(exact_ref.slot == ref.slot &&
          exact_ref.generation == ref.generation);
    const auto exact_sentinel = exact_ref;
    CHECK(CG_CanonicalSnapshotTimelineFindSnapshot(
              {kSnapshotEpoch, 2u}, &exact_ref) ==
          WORR_SNAPSHOT_TIMELINE_NOT_FOUND);
    CHECK(exact_ref.slot == exact_sentinel.slot &&
          exact_ref.generation == exact_sentinel.generation);
    CHECK(CG_CanonicalSnapshotTimelineFindSnapshot(
              {kSnapshotEpoch + 1u, 1u}, &exact_ref) ==
          WORR_SNAPSHOT_TIMELINE_NOT_FOUND);
    CHECK(exact_ref.slot == exact_sentinel.slot &&
          exact_ref.generation == exact_sentinel.generation);
    CHECK(CG_CanonicalSnapshotTimelineFindSnapshot(
              {}, &exact_ref) ==
          WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT);
    CHECK(exact_ref.slot == exact_sentinel.slot &&
          exact_ref.generation == exact_sentinel.generation);

    auto copied = sentinel_outputs();
    CHECK(CG_CanonicalSnapshotTimelineCopyEntities(
              ref, copied.entities.data(),
              static_cast<std::uint32_t>(copied.entities.size()),
              &copied.count) == WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(copied.count == projection.entities.size());
    CHECK(std::memcmp(copied.entities.data(), projection.entities.data(),
                      sizeof(projection.entities)) == 0);

    auto undersized = sentinel_outputs();
    CHECK(CG_CanonicalSnapshotTimelineCopyEntities(
              ref, undersized.entities.data(), 1u,
              &undersized.count) ==
          WORR_SNAPSHOT_TIMELINE_BUFFER_TOO_SMALL);
    check_sentinel(undersized);

    /* The cgame-owned value ring must retain one exact source projection
     * through 63 newer 16 ms snapshots (1008 ms total coverage), then evict
     * it deterministically when the 64th newer snapshot is published. */
    static_assert(CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY >= 64u);
    constexpr std::uint32_t retention_epoch = kSnapshotEpoch + 1u;
    api->Reset(retention_epoch, WORR_CGAME_SNAPSHOT_RESET_MAP,
               UINT64_C(3000));
    projection_t retention{};
    initialize_projection(retention);
    set_projection_identity(retention, retention_epoch, 1u);
    CHECK(api->ConsumeCanonicalSnapshot(
        &retention.view, &retention.hashes, UINT64_C(4001)));
    worr_snapshot_timeline_ref_v1 retained_source{};
    CHECK(CG_CanonicalSnapshotTimelineFindSnapshot(
              {retention_epoch, 1u}, &retained_source) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    for (std::uint32_t sequence = 2u;
         sequence <= CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY;
         ++sequence) {
        set_projection_identity(retention, retention_epoch, sequence);
        CHECK(api->ConsumeCanonicalSnapshot(
            &retention.view, &retention.hashes,
            UINT64_C(4000) + sequence));
    }
    worr_snapshot_timeline_ref_v1 boundary_ref{};
    CHECK(CG_CanonicalSnapshotTimelineFindSnapshot(
              {retention_epoch, 1u}, &boundary_ref) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(boundary_ref.slot == retained_source.slot &&
          boundary_ref.generation == retained_source.generation);

    set_projection_identity(
        retention, retention_epoch,
        CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY + 1u);
    CHECK(api->ConsumeCanonicalSnapshot(
        &retention.view, &retention.hashes,
        UINT64_C(4000) +
            CG_CANONICAL_SNAPSHOT_TIMELINE_SLOT_CAPACITY + 1u));
    CHECK(CG_CanonicalSnapshotTimelineFindSnapshot(
              {retention_epoch, 1u}, &boundary_ref) ==
          WORR_SNAPSHOT_TIMELINE_NOT_FOUND);

    api->Reset(kSnapshotEpoch + 2u, WORR_CGAME_SNAPSHOT_RESET_MAP,
               UINT64_C(5000));
    auto stale = sentinel_outputs();
    CHECK(CG_CanonicalSnapshotTimelineCopyEntities(
              ref, stale.entities.data(),
              static_cast<std::uint32_t>(stale.entities.size()),
              &stale.count) == WORR_SNAPSHOT_TIMELINE_STALE_REF);
    check_sentinel(stale);

    api->Reset(0u, WORR_CGAME_SNAPSHOT_RESET_UNLOAD, UINT64_C(6000));
    auto unloaded = sentinel_outputs();
    CHECK(CG_CanonicalSnapshotTimelineCopyEntities(
              ref, unloaded.entities.data(),
              static_cast<std::uint32_t>(unloaded.entities.size()),
              &unloaded.count) ==
          WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE);
    check_sentinel(unloaded);

    std::puts("cgame canonical snapshot entity copy tests passed");
    return EXIT_SUCCESS;
}
