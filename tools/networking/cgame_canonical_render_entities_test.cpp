/* Focused FR-10-T07 immutable canonical render-enumeration coverage. */

#include "cg_canonical_render_entities.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr cg_canonical_entity_adapter_limits_v1 kLimits{8192u, 4096u, 512u};

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr, "cgame_canonical_render_entities_test:%d: %s\n",
                 line, expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                    \
    do {                                                                     \
        if (!(expression))                                                   \
            fail(#expression, __LINE__);                                     \
    } while (0)

worr_snapshot_entity_v2 make_entity(std::uint32_t identity,
                                    std::uint32_t generation,
                                    float marker)
{
    worr_snapshot_entity_v2 entity{};
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.generation.identity.index = identity;
    entity.generation.identity.generation = generation;
    entity.generation.provenance_flags =
        WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    entity.component_mask =
        CG_CANONICAL_ENTITY_RENDER_REQUIRED_COMPONENTS_V1;
    entity.origin[0] = marker;
    entity.origin[1] = marker + 1.0f;
    entity.origin[2] = marker + 2.0f;
    entity.angles[0] = marker + 3.0f;
    entity.old_origin[0] = marker - 1.0f;
    entity.model_index[0] = 1u;
    entity.frame = static_cast<std::uint16_t>(identity + generation);
    entity.sound = 1u;
    entity.skin = identity * 17u;
    entity.solid = identity * 31u;
    entity.effects = identity;
    entity.renderfx = generation;
    entity.alpha = 1.0f;
    entity.scale = 1.0f;
    entity.loop_volume = 1.0f;
    entity.loop_attenuation = 3.0f;
    entity.owner.index = WORR_EVENT_NO_ENTITY;
    return entity;
}

template <std::size_t Capacity>
void expect_transactional_failure(
    const worr_snapshot_entity_v2 *previous, std::uint32_t previous_count,
    const worr_snapshot_entity_v2 *current, std::uint32_t current_count,
    cg_canonical_entity_adapter_limits_v1 limits,
    std::array<cg_canonical_render_entity_v1, Capacity> &output,
    std::uint32_t output_capacity,
    cg_canonical_render_entities_result_v1 expected)
{
    std::memset(output.data(), 0xa5, sizeof(output));
    const auto before = output;
    std::uint32_t count = UINT32_C(0xa5a5a5a5);
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              previous, previous_count, current, current_count, limits,
              output.data(), output_capacity, &count) == expected);
    CHECK(std::memcmp(output.data(), before.data(), sizeof(output)) == 0);
    CHECK(count == UINT32_C(0xa5a5a5a5));
}

void test_identity_union_and_generation_boundaries()
{
    const std::array previous{
        make_entity(1u, 10u, 100.0f),
        make_entity(2u, 20u, 200.0f),
        make_entity(4u, 40u, 400.0f),
    };
    auto current = std::array{
        make_entity(1u, 10u, 101.0f),
        make_entity(2u, 21u, 201.0f),
        make_entity(3u, 30u, 300.0f),
    };
    current[0].generation.provenance_flags =
        previous[0].generation.provenance_flags;

    std::array<cg_canonical_render_entity_v1, 6> output{};
    std::uint32_t count = 0u;
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              previous.data(), static_cast<std::uint32_t>(previous.size()),
              current.data(), static_cast<std::uint32_t>(current.size()),
              kLimits, output.data(), output.size(), &count) ==
          CG_CANONICAL_RENDER_ENTITIES_OK);
    CHECK(count == 4u);
    CHECK(output[0].identity == 1u && output[1].identity == 2u &&
          output[2].identity == 3u && output[3].identity == 4u);

    CHECK(output[0].flags ==
          (CG_CANONICAL_RENDER_ENTITY_CURRENT |
           CG_CANONICAL_RENDER_ENTITY_PREVIOUS |
           CG_CANONICAL_RENDER_ENTITY_GENERATION_CONTINUOUS));
    CHECK(output[0].current.origin[0] == 101.0f &&
          output[0].previous.origin[0] == 100.0f);
    CHECK(output[0].current.event == 0u && output[0].previous.event == 0u);

    CHECK(output[1].flags ==
          (CG_CANONICAL_RENDER_ENTITY_CURRENT |
           CG_CANONICAL_RENDER_ENTITY_PREVIOUS));
    CHECK(output[1].current_generation == 21u &&
          output[1].previous_generation == 20u);
    CHECK(output[1].current_provenance ==
              WORR_SNAPSHOT_GENERATION_AUTHORITATIVE &&
          output[1].previous_provenance ==
              WORR_SNAPSHOT_GENERATION_AUTHORITATIVE);
    CHECK(std::memcmp(&output[1].current, &output[1].previous,
                      sizeof(output[1].current)) == 0);

    CHECK(output[2].flags == CG_CANONICAL_RENDER_ENTITY_CURRENT);
    CHECK(output[2].current_generation == 30u &&
          output[2].previous_generation == 0u);
    CHECK(std::memcmp(&output[2].current, &output[2].previous,
                      sizeof(output[2].current)) == 0);

    CHECK(output[3].flags == CG_CANONICAL_RENDER_ENTITY_PREVIOUS);
    CHECK(output[3].current_generation == 0u &&
          output[3].previous_generation == 40u);
    entity_state_t zero{};
    CHECK(std::memcmp(&output[3].current, &zero, sizeof(zero)) == 0);
    CHECK(output[3].previous.origin[0] == 400.0f);
}

void test_provenance_change_blocks_continuity()
{
    auto previous = make_entity(5u, 50u, 500.0f);
    auto current = make_entity(5u, 50u, 501.0f);
    current.generation.provenance_flags =
        WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    std::array<cg_canonical_render_entity_v1, 1> output{};
    std::uint32_t count = 0u;
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              &previous, 1u, &current, 1u, kLimits, output.data(),
              output.size(), &count) == CG_CANONICAL_RENDER_ENTITIES_OK);
    CHECK(count == 1u);
    CHECK((output[0].flags &
           CG_CANONICAL_RENDER_ENTITY_GENERATION_CONTINUOUS) == 0u);
    CHECK(output[0].current_provenance ==
              WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED &&
          output[0].previous_provenance ==
              WORR_SNAPSHOT_GENERATION_AUTHORITATIVE);
    CHECK(std::memcmp(&output[0].current, &output[0].previous,
                      sizeof(output[0].current)) == 0);
}

worr_snapshot_timeline_entity_sample_v1 make_sample(
    const worr_snapshot_entity_v2 &entity, std::uint32_t mode,
    std::uint32_t blocks)
{
    worr_snapshot_timeline_entity_sample_v1 sample{};
    sample.struct_size = sizeof(sample);
    sample.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    sample.entity_index = entity.generation.identity.index;
    sample.visible = 1u;
    sample.visibility = WORR_SNAPSHOT_TIMELINE_VISIBILITY_PRESENT;
    sample.mode = mode;
    sample.blocking_reasons = blocks;
    sample.entity = entity;
    return sample;
}

void test_safe_sample_selection()
{
    auto previous = make_entity(6u, 60u, 0.0f);
    auto current = make_entity(6u, 60u, 512.0f);
    std::array<cg_canonical_render_entity_v1, 1> records{};
    std::uint32_t count = 0u;
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              &previous, 1u, &current, 1u, kLimits, records.data(), 1u,
              &count) == CG_CANONICAL_RENDER_ENTITIES_OK);

    auto interpolated = previous;
    interpolated.origin[0] = 256.0f;
    interpolated.origin[1] = 257.0f;
    interpolated.origin[2] = 258.0f;
    auto sample = make_sample(
        interpolated, WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED, 0u);
    cg_canonical_render_sample_v1 selected{};
    CHECK(CG_SelectCanonicalRenderSampleV1(
              &records[0], &sample, kLimits, &selected) ==
          CG_CANONICAL_RENDER_SAMPLE_OK);
    CHECK(selected.direct_endpoint == 0u &&
          selected.state.origin[0] == 256.0f);

    /* The exact legacy boundary remains blendable, including a diagonal. */
    previous.origin[0] = previous.origin[1] = previous.origin[2] = 0.0f;
    current.origin[0] = current.origin[1] = current.origin[2] = 512.0f;
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              &previous, 1u, &current, 1u, kLimits, records.data(), 1u,
              &count) == CG_CANONICAL_RENDER_ENTITIES_OK);
    sample = make_sample(
        interpolated, WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED, 0u);
    CHECK(CG_SelectCanonicalRenderSampleV1(
              &records[0], &sample, kLimits, &selected) ==
          CG_CANONICAL_RENDER_SAMPLE_OK);
    CHECK(selected.direct_endpoint == 0u);

    /* A single-axis step just above 512 selects the previous endpoint. */
    current.origin[0] = 512.01f;
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              &previous, 1u, &current, 1u, kLimits, records.data(), 1u,
              &count) == CG_CANONICAL_RENDER_ENTITIES_OK);
    sample = make_sample(
        interpolated, WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED, 0u);
    CHECK(CG_SelectCanonicalRenderSampleV1(
              &records[0], &sample, kLimits, &selected) ==
          CG_CANONICAL_RENDER_SAMPLE_OK);
    CHECK(selected.direct_endpoint == 1u &&
          selected.state.origin[0] == previous.origin[0]);

    /* Model identity changes never retain an interpolated transform. */
    current = previous;
    current.model_index[0] = 2u;
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              &previous, 1u, &current, 1u, kLimits, records.data(), 1u,
              &count) == CG_CANONICAL_RENDER_ENTITIES_OK);
    sample = make_sample(
        interpolated, WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_DISCRETE_TRANSITION);
    CHECK(CG_SelectCanonicalRenderSampleV1(
              &records[0], &sample, kLimits, &selected) ==
          CG_CANONICAL_RENDER_SAMPLE_OK);
    CHECK(selected.direct_endpoint == 1u &&
          selected.state.modelindex == records[0].previous.modelindex);

    /* Non-spatial discrete changes retain the core's hybrid smooth sample. */
    current = previous;
    current.origin[0] = 100.0f;
    current.frame = static_cast<std::uint16_t>(previous.frame + 1u);
    current.effects ^= 1u;
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              &previous, 1u, &current, 1u, kLimits, records.data(), 1u,
              &count) == CG_CANONICAL_RENDER_ENTITIES_OK);
    interpolated = previous;
    interpolated.origin[0] = 50.0f;
    sample = make_sample(
        interpolated, WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_DISCRETE_TRANSITION);
    CHECK(CG_SelectCanonicalRenderSampleV1(
              &records[0], &sample, kLimits, &selected) ==
          CG_CANONICAL_RENDER_SAMPLE_OK);
    CHECK(selected.direct_endpoint == 0u &&
          selected.state.origin[0] == 50.0f &&
          selected.state.frame == interpolated.frame &&
          selected.state.effects == interpolated.effects);

    auto extrapolated = current;
    extrapolated.origin[0] = 125.0f;
    sample = make_sample(
        extrapolated, WORR_SNAPSHOT_TIMELINE_ENTITY_EXTRAPOLATED,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_DISCRETE_TRANSITION);
    CHECK(CG_SelectCanonicalRenderSampleV1(
              &records[0], &sample, kLimits, &selected) ==
          CG_CANONICAL_RENDER_SAMPLE_OK);
    CHECK(selected.direct_endpoint == 0u &&
          selected.state.origin[0] == 125.0f &&
          selected.state.frame == current.frame &&
          selected.state.effects == current.effects);

    const auto before = selected;
    sample.blocking_reasons = UINT32_C(0x80000000);
    CHECK(CG_SelectCanonicalRenderSampleV1(
              &records[0], &sample, kLimits, &selected) ==
          CG_CANONICAL_RENDER_SAMPLE_INVALID_SAMPLE);
    CHECK(std::memcmp(&selected, &before, sizeof(selected)) == 0);
}

void test_previous_only_endpoint_and_evidence_ordering()
{
    const auto removed = make_entity(7u, 70u, 700.0f);
    std::array<cg_canonical_render_entity_v1, 1> records{};
    std::uint32_t count = 0u;
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              &removed, 1u, nullptr, 0u, kLimits, records.data(),
              records.size(), &count) == CG_CANONICAL_RENDER_ENTITIES_OK);
    CHECK(count == 1u &&
          records[0].flags == CG_CANONICAL_RENDER_ENTITY_PREVIOUS);

    /* Immediately before the current endpoint, the timeline's visible
     * previous-only sample is a valid renderer source. */
    auto sample = make_sample(
        removed, WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_MISSING);
    sample.visibility =
        WORR_SNAPSHOT_TIMELINE_VISIBILITY_REMOVED_AT_CURRENT;
    cg_canonical_render_sample_v1 selected{};
    CHECK(CG_SelectCanonicalRenderSampleV1(
              &records[0], &sample, kLimits, &selected) ==
          CG_CANONICAL_RENDER_SAMPLE_OK);
    CHECK(selected.generation == 70u &&
          selected.state.number == static_cast<int>(records[0].identity));

    /* At the exact current endpoint it disappears: an invisible/NONE sample
     * cannot become a selection and leaves the prior output untouched. */
    sample.visible = 0u;
    sample.mode = WORR_SNAPSHOT_TIMELINE_ENTITY_NONE;
    sample.entity = {};
    const auto before = selected;
    CHECK(CG_SelectCanonicalRenderSampleV1(
              &records[0], &sample, kLimits, &selected) ==
          CG_CANONICAL_RENDER_SAMPLE_INVALID_SAMPLE);
    CHECK(std::memcmp(&selected, &before, sizeof(selected)) == 0);

    CHECK(CG_CanonicalPreviousOnlyEvidenceOrderedV1(0u, 0u, 0u));
    CHECK(CG_CanonicalPreviousOnlyEvidenceOrderedV1(9u, 6u, 4u));
    CHECK(CG_CanonicalPreviousOnlyEvidenceOrderedV1(
        UINT64_MAX, UINT64_MAX, UINT64_MAX));
    CHECK(!CG_CanonicalPreviousOnlyEvidenceOrderedV1(5u, 6u, 4u));
    CHECK(!CG_CanonicalPreviousOnlyEvidenceOrderedV1(9u, 3u, 4u));
}

cg_canonical_render_lifecycle_decision_v1 resolve_lifecycle(
    const entity_state_t &cached, const entity_state_t &selected,
    std::uint32_t pending_blocks, std::uint64_t pending_key,
    std::uint64_t handled_key, std::uint32_t sample_mode,
    std::uint32_t sample_blocks, std::uint64_t sample_key)
{
    cg_canonical_render_lifecycle_decision_v1 decision{};
    CHECK(CG_ResolveCanonicalRenderLifecycleV1(
              &cached, &selected, pending_blocks, pending_key,
              handled_key, sample_mode, sample_blocks, sample_key,
              &decision) ==
          CG_CANONICAL_RENDER_LIFECYCLE_OK);
    return decision;
}

void test_discontinuity_lifecycle_fence()
{
    constexpr std::uint64_t kEndpointA = UINT64_C(0x0000001100000002);
    constexpr std::uint64_t kEndpointB = UINT64_C(0x0000001200000003);
    const auto canonical = make_entity(7u, 70u, 10.0f);
    entity_state_t cached{};
    CHECK(CG_CanonicalEntityToRenderStateV1(
              &canonical, kLimits, &cached) ==
          CG_CANONICAL_ENTITY_ADAPTER_OK);

    auto selected = cached;
    selected.origin[0] += 1.0f;
    for (const std::uint32_t benign : {
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_COMPONENT,
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_DISCRETE_TRANSITION,
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_POLICY}) {
        const auto decision = resolve_lifecycle(
            cached, selected, 0u, 0u, 0u,
            WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED, benign,
            kEndpointA);
        CHECK(decision.reset == 0u &&
              decision.pending_blocking_reasons == 0u);
    }

    for (const std::uint32_t discontinuity : {
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT,
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_GENERATION,
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_MISSING,
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT,
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED}) {
        const auto decision = resolve_lifecycle(
            cached, selected, 0u, 0u, 0u,
            WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT, discontinuity,
            kEndpointA);
        CHECK(decision.reset == 1u &&
              decision.pending_blocking_reasons == 0u);
    }

    auto rotated = cached;
    rotated.angles[1] += 1.0f;
    auto decision = resolve_lifecycle(
        cached, rotated, 0u, 0u, 0u,
        WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_ANGULAR_SPEED, kEndpointA);
    CHECK(decision.reset == 1u &&
          decision.pending_blocking_reasons == 0u);

    /* A previous-side direct endpoint arms one fence without restarting the
     * same centity on every held render frame. */
    decision = resolve_lifecycle(
        cached, cached, 0u, 0u, 0u,
        WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED, kEndpointA);
    CHECK(decision.reset == 0u &&
          decision.pending_blocking_reasons ==
              WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED &&
          decision.pending_discontinuity_key == kEndpointA);
    decision = resolve_lifecycle(
        cached, cached, decision.pending_blocking_reasons,
        decision.pending_discontinuity_key,
        decision.handled_discontinuity_key,
        WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED, kEndpointA);
    CHECK(decision.reset == 0u &&
          decision.pending_blocking_reasons ==
              WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED &&
          decision.pending_discontinuity_key == kEndpointA);

    /* A new held boundary cannot erase an older unconsumed fence.  Consume A
     * once while B remains armed, then keep a repeated B stable. */
    auto chained = resolve_lifecycle(
        cached, cached, 0u, 0u, 0u,
        WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED, kEndpointA);
    CHECK(chained.reset == 0u &&
          chained.pending_discontinuity_key == kEndpointA);
    chained = resolve_lifecycle(
        cached, cached, chained.pending_blocking_reasons,
        chained.pending_discontinuity_key,
        chained.handled_discontinuity_key,
        WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED, kEndpointB);
    CHECK(chained.reset == 1u &&
          chained.pending_blocking_reasons ==
              WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED &&
          chained.pending_discontinuity_key == kEndpointB &&
          chained.handled_discontinuity_key == kEndpointA);
    chained = resolve_lifecycle(
        cached, cached, chained.pending_blocking_reasons,
        chained.pending_discontinuity_key,
        chained.handled_discontinuity_key,
        WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED, kEndpointB);
    CHECK(chained.reset == 0u &&
          chained.pending_discontinuity_key == kEndpointB &&
          chained.handled_discontinuity_key == kEndpointA);

    /* Crossing the fence restarts once, even if the two endpoints happen to
     * have identical transforms, then clears it for the following frame. */
    decision = resolve_lifecycle(
        cached, cached, decision.pending_blocking_reasons,
        decision.pending_discontinuity_key,
        decision.handled_discontinuity_key,
        WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT, 0u, kEndpointA);
    CHECK(decision.reset == 1u &&
          decision.pending_blocking_reasons == 0u &&
          decision.pending_discontinuity_key == 0u &&
          decision.handled_discontinuity_key == kEndpointA);
    decision = resolve_lifecycle(
        cached, cached, 0u, 0u, decision.handled_discontinuity_key,
        WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT, 0u, kEndpointA);
    CHECK(decision.reset == 0u &&
          decision.pending_blocking_reasons == 0u);

    /* The first current-side discontinuity restarts even when its selected
     * transform is identical.  The exact endpoint key then suppresses churn
     * for repeated observations of that same discontinuity. */
    decision = resolve_lifecycle(
        cached, cached, 0u, 0u, 0u,
        WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT, kEndpointB);
    CHECK(decision.reset == 1u &&
          decision.pending_blocking_reasons == 0u &&
          decision.handled_discontinuity_key == kEndpointB);
    decision = resolve_lifecycle(
        cached, cached, 0u, 0u, decision.handled_discontinuity_key,
        WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT, kEndpointB);
    CHECK(decision.reset == 0u &&
          decision.handled_discontinuity_key == kEndpointB);

    decision = resolve_lifecycle(
        cached, cached, 0u, 0u, decision.handled_discontinuity_key,
        WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT, kEndpointA);
    CHECK(decision.reset == 1u &&
          decision.handled_discontinuity_key == kEndpointA);
    decision = resolve_lifecycle(
        cached, cached, 0u, 0u, decision.handled_discontinuity_key,
        WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT,
        WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT, kEndpointA);
    CHECK(decision.reset == 0u &&
          decision.handled_discontinuity_key == kEndpointA);

    selected = cached;
    selected.origin[0] += 512.0f;
    CHECK(resolve_lifecycle(
              cached, selected, 0u, 0u, 0u,
              WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT, 0u, kEndpointA)
              .reset == 0u);
    selected.origin[0] += 0.01f;
    CHECK(resolve_lifecycle(
              cached, selected, 0u, 0u, 0u,
              WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT, 0u, kEndpointA)
              .reset == 1u);
    selected = cached;
    ++selected.modelindex;
    CHECK(resolve_lifecycle(
              cached, selected, 0u, 0u, 0u,
              WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT, 0u, kEndpointA)
              .reset == 1u);

    cg_canonical_render_lifecycle_decision_v1 sentinel{
        UINT32_C(0xa5a5a5a5), UINT32_C(0x5a5a5a5a),
        UINT64_C(0x1111111122222222), UINT64_C(0x3333333344444444)};
    const auto before = sentinel;
    CHECK(CG_ResolveCanonicalRenderLifecycleV1(
              &cached, &cached, 0u, 0u, 0u,
              WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT,
              UINT32_C(0x80000000), kEndpointA, &sentinel) ==
          CG_CANONICAL_RENDER_LIFECYCLE_INVALID_INPUT);
    CHECK(std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0);

    const worr_snapshot_timeline_ref_v1 endpoint{3u, 18u};
    CHECK(CG_CanonicalRenderEndpointKeyV1(endpoint) == kEndpointB);
    CHECK(CG_CanonicalRenderEndpointKeyV1(
              {WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT, 0u}) == 0u);
    CHECK(CG_CanonicalRenderEndpointKeyV1({3u, 0u}) == 0u);
}

cg_canonical_interpolation_delay_config_v1 interpolation_config(
    bool enabled)
{
    cg_canonical_interpolation_delay_config_v1 config{};
    config.baseline_delay_us = UINT64_C(50000);
    config.maximum_delay_us = UINT64_C(150000);
    config.adaptive_enabled = enabled ? 1u : 0u;
    return config;
}

cg_canonical_interpolation_delay_observation_v1 interpolation_observation(
    std::uint32_t epoch, std::uint64_t reset_count,
    std::uint64_t accepted_count, std::uint64_t server_time_us,
    std::uint64_t receive_time_us)
{
    cg_canonical_interpolation_delay_observation_v1 observation{};
    observation.epoch = epoch;
    observation.snapshot_valid = accepted_count != 0u ? 1u : 0u;
    observation.stream_reset_count = reset_count;
    observation.accepted_snapshot_count = accepted_count;
    if (observation.snapshot_valid) {
        observation.snapshot_server_time_us = server_time_us;
        observation.snapshot_receive_time_us = receive_time_us;
    }
    return observation;
}

cg_canonical_interpolation_delay_status_v1 update_interpolation_delay(
    cg_canonical_interpolation_delay_state_v1 &state,
    const cg_canonical_interpolation_delay_config_v1 &config,
    const cg_canonical_interpolation_delay_observation_v1 &observation)
{
    cg_canonical_interpolation_delay_status_v1 status{};
    CHECK(CG_UpdateCanonicalInterpolationDelayV1(
              &state, &config, &observation, &status) ==
          CG_CANONICAL_INTERPOLATION_DELAY_OK);
    return status;
}

void test_interpolation_delay_disabled_is_exactly_fixed()
{
    cg_canonical_interpolation_delay_state_v1 state{};
    const auto config = interpolation_config(false);
    auto observation = interpolation_observation(
        7u, 1u, 1u, UINT64_C(100000), UINT64_C(200000));
    auto status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == config.baseline_delay_us);
    CHECK(status.adaptive_enabled == 0u);
    CHECK(status.last_adjustment ==
          CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_RESET);

    observation.accepted_snapshot_count = 2u;
    observation.snapshot_server_time_us = UINT64_C(150000);
    observation.snapshot_receive_time_us = UINT64_C(290000);
    observation.pair_valid = 1u;
    observation.pair_mode = WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST;
    observation.pair_extrapolation_us = UINT64_C(1000000);
    status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == config.baseline_delay_us);
    CHECK(status.interval_observations == 0u);
    CHECK(status.rise_adjustments == 0u);
    CHECK(status.recovery_adjustments == 0u);
    CHECK(status.pressure_observations == 0u);
    CHECK(status.last_adjustment ==
          CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_FIXED);
}

void test_interpolation_delay_rise_bound_and_recovery()
{
    cg_canonical_interpolation_delay_state_v1 state{};
    const auto config = interpolation_config(true);
    auto observation = interpolation_observation(
        9u, 4u, 1u, UINT64_C(100000), UINT64_C(200000));
    auto status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == UINT64_C(50000));
    CHECK(status.reset_count == 1u);

    observation.accepted_snapshot_count = 2u;
    observation.snapshot_server_time_us = UINT64_C(150000);
    observation.snapshot_receive_time_us = UINT64_C(250000);
    status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == UINT64_C(50000));
    CHECK(status.cadence_us == UINT64_C(50000));
    CHECK(status.jitter_us == 0u);
    CHECK(status.interval_observations == 1u);

    observation.accepted_snapshot_count = 3u;
    observation.snapshot_server_time_us = UINT64_C(200000);
    observation.snapshot_receive_time_us = UINT64_C(340000);
    status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == UINT64_C(90000));
    CHECK(status.jitter_us == UINT64_C(10000));
    CHECK(status.last_jitter_us == UINT64_C(40000));
    CHECK(status.rise_adjustments == 1u);
    CHECK(status.last_adjustment ==
          CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_JITTER_RISE);

    observation.pair_valid = 1u;
    observation.pair_mode = WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE;
    observation.pair_extrapolation_us = UINT64_C(40000);
    status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == UINT64_C(106250));
    CHECK(status.pressure_observations == 1u);
    CHECK(status.rise_adjustments == 2u);
    CHECK(status.last_adjustment ==
          CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_PRESSURE_RISE);

    observation.pair_mode = WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST;
    observation.pair_extrapolation_us = UINT64_C(100000);
    status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == config.maximum_delay_us);
    CHECK(status.pressure_observations == 2u);
    CHECK(status.rise_adjustments == 3u);

    observation.pair_valid = 0u;
    observation.pair_mode = 0u;
    observation.pair_extrapolation_us = 0u;
    for (std::uint64_t index = 0u; index < 40u; ++index) {
        ++observation.accepted_snapshot_count;
        observation.snapshot_server_time_us += UINT64_C(50000);
        observation.snapshot_receive_time_us += UINT64_C(50000);
        status = update_interpolation_delay(state, config, observation);
        if (index == 0u) {
            CHECK(status.delay_us == UINT64_C(146875));
            CHECK(status.last_adjustment ==
                  CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_RECOVERY);
        }
    }
    CHECK(status.delay_us == config.baseline_delay_us);
    CHECK(status.jitter_us == 0u);
    CHECK(status.interval_observations == 42u);
    CHECK(status.recovery_adjustments == 32u);
}

void test_interpolation_delay_epoch_and_stream_resets()
{
    cg_canonical_interpolation_delay_state_v1 state{};
    const auto config = interpolation_config(true);
    auto observation = interpolation_observation(
        11u, 8u, 1u, UINT64_C(100000), UINT64_C(100000));
    auto status = update_interpolation_delay(state, config, observation);
    observation.accepted_snapshot_count = 2u;
    observation.snapshot_server_time_us = UINT64_C(150000);
    observation.snapshot_receive_time_us = UINT64_C(180000);
    status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == UINT64_C(80000));

    observation.epoch = 12u;
    observation.stream_reset_count = 9u;
    observation.accepted_snapshot_count = 3u;
    observation.snapshot_server_time_us = UINT64_C(200000);
    observation.snapshot_receive_time_us = UINT64_C(230000);
    status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == config.baseline_delay_us);
    CHECK(status.jitter_us == 0u);
    CHECK(status.reset_count == 2u);
    CHECK(status.last_adjustment ==
          CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_RESET);

    /* Same-epoch reset serial changes cover map/hard reset and demo seek. */
    observation.stream_reset_count = 10u;
    observation.snapshot_valid = 0u;
    observation.snapshot_server_time_us = 0u;
    observation.snapshot_receive_time_us = 0u;
    status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == config.baseline_delay_us);
    CHECK(status.reset_count == 3u);
    CHECK(state.snapshot_anchor_valid == 0u);

    CG_ResetCanonicalInterpolationDelayV1(&state);
    CHECK(state.initialized == 0u && state.reset_count == 0u);
}

void test_interpolation_delay_saturating_pressure()
{
    cg_canonical_interpolation_delay_state_v1 state{};
    auto config = interpolation_config(true);
    config.baseline_delay_us = UINT64_C(900000);
    config.maximum_delay_us =
        WORR_SNAPSHOT_TIMELINE_MAX_INTERPOLATION_DELAY_US;
    auto observation = interpolation_observation(
        13u, 1u, 1u, UINT64_C(100000), UINT64_C(100000));
    auto status = update_interpolation_delay(state, config, observation);
    state.rise_adjustments = UINT64_MAX;
    state.pressure_observations = UINT64_MAX;
    observation.pair_valid = 1u;
    observation.pair_mode = WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST;
    observation.pair_extrapolation_us = UINT64_MAX;
    status = update_interpolation_delay(state, config, observation);
    CHECK(status.delay_us == config.maximum_delay_us);
    CHECK(status.rise_adjustments == UINT64_MAX);
    CHECK(status.pressure_observations == UINT64_MAX);
    CHECK(status.last_adjustment ==
          CG_CANONICAL_INTERPOLATION_DELAY_ADJUST_PRESSURE_RISE);
}

worr_snapshot_timeline_clock_state_v1 make_clock(std::uint32_t rate_q16,
                                                  bool paused)
{
    worr_snapshot_timeline_clock_state_v1 clock{};
    clock.struct_size = sizeof(clock);
    clock.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    clock.epoch = 3u;
    clock.host_time_us = 1000u;
    clock.render_time_us = 2000u;
    clock.rate_q16 = rate_q16;
    clock.paused = paused ? 1u : 0u;
    clock.initialized = 1u;
    return clock;
}

void test_render_clock_rate_pause_plan()
{
    auto clock = make_clock(WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16, false);
    cg_canonical_render_clock_plan_v1 plan{};
    CHECK(CG_BuildCanonicalRenderClockPlanV1(
              &clock, 2000u, WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16 / 2u,
              false, &plan) == CG_CANONICAL_RENDER_CLOCK_PLAN_OK);
    CHECK(plan.request_count == 1u);
    CHECK(plan.requests[0].operation ==
          WORR_SNAPSHOT_TIMELINE_CLOCK_SET_RATE);
    CHECK(plan.requests[0].rate_q16 ==
          WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16 / 2u);

    CHECK(CG_BuildCanonicalRenderClockPlanV1(
              &clock, 3000u, WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16 * 2u,
              true, &plan) == CG_CANONICAL_RENDER_CLOCK_PLAN_OK);
    CHECK(plan.request_count == 2u);
    CHECK(plan.requests[0].operation ==
          WORR_SNAPSHOT_TIMELINE_CLOCK_SET_RATE);
    CHECK(plan.requests[1].operation ==
          WORR_SNAPSHOT_TIMELINE_CLOCK_PAUSE);
    CHECK(plan.requests[0].host_time_us == 3000u &&
          plan.requests[1].host_time_us == 3000u);

    clock = make_clock(WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16 * 2u, true);
    CHECK(CG_BuildCanonicalRenderClockPlanV1(
              &clock, 500u, WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16 * 2u,
              false, &plan) == CG_CANONICAL_RENDER_CLOCK_PLAN_OK);
    CHECK(plan.request_count == 1u);
    CHECK(plan.requests[0].operation ==
          WORR_SNAPSHOT_TIMELINE_CLOCK_RESUME);
    CHECK(plan.requests[0].host_time_us == clock.host_time_us);

    const auto before = plan;
    CHECK(CG_BuildCanonicalRenderClockPlanV1(
              &clock, 2000u, 0u, false, &plan) ==
          CG_CANONICAL_RENDER_CLOCK_PLAN_INVALID_RATE);
    CHECK(std::memcmp(&plan, &before, sizeof(plan)) == 0);
}

void test_legacy_clock_alignment_is_bounded_and_fail_soft()
{
    std::uint64_t resolved = UINT64_C(0xa5a5a5a5a5a5a5a5);

    CHECK(CG_ResolveCanonicalLegacyAlignmentV1(
              1000u, 950u, 100u, &resolved) ==
          CG_CANONICAL_LEGACY_ALIGNMENT_OK);
    CHECK(resolved == 950u);

    CHECK(CG_ResolveCanonicalLegacyAlignmentV1(
              1000u, 1001u, 100u, &resolved) ==
          CG_CANONICAL_LEGACY_ALIGNMENT_CLAMPED_FUTURE);
    CHECK(resolved == 1000u);

    resolved = UINT64_C(0xa5a5a5a5a5a5a5a5);
    CHECK(CG_ResolveCanonicalLegacyAlignmentV1(
              1000u, 899u, 100u, &resolved) ==
          CG_CANONICAL_LEGACY_ALIGNMENT_PAST_LIMIT);
    CHECK(resolved == UINT64_C(0xa5a5a5a5a5a5a5a5));
    CHECK(CG_ResolveCanonicalLegacyAlignmentV1(
              1000u, 900u, 100u, &resolved) ==
          CG_CANONICAL_LEGACY_ALIGNMENT_OK);
    CHECK(resolved == 900u);

    CHECK(CG_ResolveCanonicalLegacyAlignmentV1(
              50u, 0u, 100u, &resolved) ==
          CG_CANONICAL_LEGACY_ALIGNMENT_OK);
    CHECK(resolved == 0u);
    resolved = UINT64_C(0xa5a5a5a5a5a5a5a5);
    CHECK(CG_ResolveCanonicalLegacyAlignmentV1(
              1000u, 999u, 0u, &resolved) ==
          CG_CANONICAL_LEGACY_ALIGNMENT_PAST_LIMIT);
    CHECK(resolved == UINT64_C(0xa5a5a5a5a5a5a5a5));

    const auto before = resolved;
    CHECK(CG_ResolveCanonicalLegacyAlignmentV1(
              1000u, 1000u, 100u, nullptr) ==
          CG_CANONICAL_LEGACY_ALIGNMENT_INVALID_ARGUMENT);
    CHECK(resolved == before);
}

void test_host_time_wrap_extension()
{
    cg_canonical_host_time_extender_v1 state{};
    std::uint64_t extended = 0u;
    CHECK(CG_ExtendCanonicalHostTimeV1(
              &state, UINT32_MAX - 5u, &extended) ==
          CG_CANONICAL_HOST_TIME_OK);
    CHECK(extended == UINT32_MAX - 5u);
    CHECK(CG_ExtendCanonicalHostTimeV1(&state, 3u, &extended) ==
          CG_CANONICAL_HOST_TIME_OK);
    CHECK(extended == static_cast<std::uint64_t>(UINT32_MAX) + 4u);

    const auto accepted = state;
    CHECK(CG_ExtendCanonicalHostTimeV1(&state, 2u, &extended) ==
          CG_CANONICAL_HOST_TIME_OK);
    CHECK(std::memcmp(&state, &accepted, sizeof(state)) == 0);
    CHECK(extended == accepted.extended_ms);

    state.extended_ms = UINT64_MAX;
    state.last_sample_ms = 10u;
    state.initialized = 1u;
    const auto before = state;
    extended = UINT64_C(0xa5a5a5a5a5a5a5a5);
    CHECK(CG_ExtendCanonicalHostTimeV1(&state, 11u, &extended) ==
          CG_CANONICAL_HOST_TIME_OVERFLOW);
    CHECK(std::memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(extended == UINT64_C(0xa5a5a5a5a5a5a5a5));
}

void test_native_authority_prebind_and_post_bind_loss()
{
    cg_canonical_native_authority_state_v1 state{};

    CHECK(CG_ResolveCanonicalNativeAuthorityV1(&state, true, false) ==
          CG_CANONICAL_NATIVE_AUTHORITY_PREBIND_LEGACY);
    CHECK(state.ownership_latched == 0u);
    CHECK(CG_ResolveCanonicalNativeAuthorityV1(&state, true, false) ==
          CG_CANONICAL_NATIVE_AUTHORITY_PREBIND_LEGACY);
    CHECK(state.ownership_latched == 0u);

    CHECK(CG_ResolveCanonicalNativeAuthorityV1(&state, true, true) ==
          CG_CANONICAL_NATIVE_AUTHORITY_FIRST_BIND);
    CHECK(state.ownership_latched == 1u);
    CHECK(CG_ResolveCanonicalNativeAuthorityV1(&state, true, true) ==
          CG_CANONICAL_NATIVE_AUTHORITY_OWNED);

    CHECK(CG_ResolveCanonicalNativeAuthorityV1(&state, true, false) ==
          CG_CANONICAL_NATIVE_AUTHORITY_POST_BIND_LOSS);
    CHECK(state.ownership_latched == 1u);
    CHECK(CG_ResolveCanonicalNativeAuthorityV1(&state, true, true) ==
          CG_CANONICAL_NATIVE_AUTHORITY_OWNED);

    CG_ResetCanonicalNativeAuthorityV1(&state);
    CHECK(state.ownership_latched == 0u);
    CHECK(CG_ResolveCanonicalNativeAuthorityV1(&state, true, false) ==
          CG_CANONICAL_NATIVE_AUTHORITY_PREBIND_LEGACY);
    CHECK(CG_ResolveCanonicalNativeAuthorityV1(&state, true, true) ==
          CG_CANONICAL_NATIVE_AUTHORITY_FIRST_BIND);

    CHECK(CG_ResolveCanonicalNativeAuthorityV1(&state, false, true) ==
          CG_CANONICAL_NATIVE_AUTHORITY_INACTIVE);
    CHECK(state.ownership_latched == 0u);
    CHECK(CG_ResolveCanonicalNativeAuthorityV1(&state, true, false) ==
          CG_CANONICAL_NATIVE_AUTHORITY_PREBIND_LEGACY);
    CHECK(CG_ResolveCanonicalNativeAuthorityV1(nullptr, true, true) ==
          CG_CANONICAL_NATIVE_AUTHORITY_INVALID);
    CG_ResetCanonicalNativeAuthorityV1(nullptr);
}

void test_transactional_rejections()
{
    auto one = make_entity(1u, 1u, 1.0f);
    auto two = make_entity(2u, 2u, 2.0f);
    std::array<cg_canonical_render_entity_v1, 4> output{};

    const std::array out_of_order{two, one};
    expect_transactional_failure(
        out_of_order.data(), out_of_order.size(), &one, 1u, kLimits, output,
        output.size(), CG_CANONICAL_RENDER_ENTITIES_OUT_OF_ORDER);

    const std::array duplicate{one, one};
    expect_transactional_failure(
        duplicate.data(), duplicate.size(), &two, 1u, kLimits, output,
        output.size(), CG_CANONICAL_RENDER_ENTITIES_OUT_OF_ORDER);

    auto invalid = one;
    invalid.component_mask &= ~WORR_SNAPSHOT_ENTITY_MODELS;
    std::memset(invalid.model_index, 0, sizeof(invalid.model_index));
    expect_transactional_failure(
        &invalid, 1u, &two, 1u, kLimits, output, output.size(),
        CG_CANONICAL_RENDER_ENTITIES_INVALID_ENTITY);

    expect_transactional_failure(
        &one, 1u, &two, 1u, kLimits, output, 1u,
        CG_CANONICAL_RENDER_ENTITIES_OUTPUT_TOO_SMALL);
    expect_transactional_failure(
        &one, 1u, &two, 1u, {1u, 1u, 1u}, output, output.size(),
        CG_CANONICAL_RENDER_ENTITIES_INVALID_LIMITS);
    expect_transactional_failure(
        &one, CG_CANONICAL_RENDER_SNAPSHOT_ENTITY_CAPACITY + 1u, &two, 1u,
        kLimits, output, output.size(),
        CG_CANONICAL_RENDER_ENTITIES_COUNT_LIMIT);

    std::uint32_t count = UINT32_C(0xa5a5a5a5);
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              &one, 1u, &two, 1u, kLimits,
              reinterpret_cast<cg_canonical_render_entity_v1 *>(&one), 1u,
              &count) == CG_CANONICAL_RENDER_ENTITIES_OUTPUT_TOO_SMALL);
    CHECK(count == UINT32_C(0xa5a5a5a5));

    std::array<worr_snapshot_entity_v2, 2> adjacent{one, two};
    count = UINT32_C(0xa5a5a5a5);
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              adjacent.data(), 1u, adjacent.data() + 1, 1u, kLimits,
              reinterpret_cast<cg_canonical_render_entity_v1 *>(
                  adjacent.data()),
              2u, &count) == CG_CANONICAL_RENDER_ENTITIES_OVERLAP);
    CHECK(count == UINT32_C(0xa5a5a5a5));

    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              nullptr, 1u, &two, 1u, kLimits, output.data(), output.size(),
              &count) == CG_CANONICAL_RENDER_ENTITIES_INVALID_ARGUMENT);
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              &one, 1u, &two, 1u, kLimits, nullptr, output.size(), &count) ==
          CG_CANONICAL_RENDER_ENTITIES_INVALID_ARGUMENT);
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              &one, 1u, &two, 1u, kLimits, output.data(), output.size(),
              nullptr) == CG_CANONICAL_RENDER_ENTITIES_INVALID_ARGUMENT);
}

void test_maximum_disjoint_union()
{
    static std::array<worr_snapshot_entity_v2,
                      CG_CANONICAL_RENDER_SNAPSHOT_ENTITY_CAPACITY>
        previous;
    static std::array<worr_snapshot_entity_v2,
                      CG_CANONICAL_RENDER_SNAPSHOT_ENTITY_CAPACITY>
        current;
    static std::array<cg_canonical_render_entity_v1,
                      CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY>
        output;
    for (std::uint32_t index = 0; index < previous.size(); ++index) {
        previous[index] = make_entity(index + 1u, 1u,
                                      static_cast<float>(index));
        current[index] = make_entity(
            index + 1u + previous.size(), 1u,
            static_cast<float>(index + previous.size()));
    }
    std::uint32_t count = 0u;
    CHECK(CG_BuildCanonicalRenderEntitiesV1(
              previous.data(), previous.size(), current.data(), current.size(),
              kLimits, output.data(), output.size(), &count) ==
          CG_CANONICAL_RENDER_ENTITIES_OK);
    CHECK(count == CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY);
    CHECK(output.front().identity == 1u &&
          output.back().identity ==
              CG_CANONICAL_RENDER_ENTITY_UNION_CAPACITY);
}

} // namespace

int main()
{
    test_identity_union_and_generation_boundaries();
    test_provenance_change_blocks_continuity();
    test_safe_sample_selection();
    test_previous_only_endpoint_and_evidence_ordering();
    test_discontinuity_lifecycle_fence();
    test_interpolation_delay_disabled_is_exactly_fixed();
    test_interpolation_delay_rise_bound_and_recovery();
    test_interpolation_delay_epoch_and_stream_resets();
    test_interpolation_delay_saturating_pressure();
    test_render_clock_rate_pause_plan();
    test_legacy_clock_alignment_is_bounded_and_fail_soft();
    test_host_time_wrap_extension();
    test_native_authority_prebind_and_post_bind_loss();
    test_transactional_rejections();
    test_maximum_disjoint_union();
    return EXIT_SUCCESS;
}
