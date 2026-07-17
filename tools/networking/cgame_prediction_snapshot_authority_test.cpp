/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_prediction_authority.hpp"
#include "common/net/prediction_input.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,       \
                         #expression);                                      \
            return EXIT_FAILURE;                                            \
        }                                                                   \
    } while (0)

namespace {

constexpr std::uint32_t kSnapshotEpoch = 9u;
constexpr std::uint32_t kSnapshotSequence = 42u;
constexpr std::uint32_t kServerTick = 600u;
constexpr std::uint64_t kServerTimeUs = UINT64_C(9600000);
constexpr std::uint32_t kControlledEntityIndex = 1u;
constexpr std::uint32_t kControlledEntityGeneration = 7u;
constexpr std::uint32_t kCommandEpoch = 17u;
constexpr std::uint32_t kConsumedCommandSequence = 63u;

worr_snapshot_entity_generation_v2 controlled_entity(
    std::uint32_t index = kControlledEntityIndex,
    std::uint32_t generation = kControlledEntityGeneration)
{
    worr_snapshot_entity_generation_v2 value{};
    value.identity.index = index;
    value.identity.generation = generation;
    value.provenance_flags = WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    return value;
}

worr_snapshot_player_v2 make_player()
{
    worr_snapshot_player_v2 player{};
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.controlled_entity = controlled_entity();
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    player.movement.origin[0] = 128.0f;
    player.movement.origin[1] = -64.0f;
    player.movement.velocity[0] = 320.0f;
    player.movement.gravity = 800;
    player.movement.view_height = 22;
    player.view_angles[1] = 90.0f;
    player.view_offset[2] = 22.0f;
    player.screen_blend[3] = 0.25f;
    player.rdflags = 3;
    player.fov = 100.0f;
    return player;
}

bool refresh_hashes(cg_prediction_authority_candidate_v1 &candidate)
{
    auto &snapshot = candidate.timeline.snapshot;
    if (!Worr_SnapshotPlayerHashV2(
            &candidate.timeline.player,
            CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES,
            &snapshot.player_hash) ||
        !Worr_SnapshotEntityListHashV2(
            nullptr, 0, CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES,
            &snapshot.entity_hash) ||
        !Worr_SnapshotAreaHashV2(nullptr, 0, &snapshot.area_hash) ||
        !Worr_SnapshotEventRefsHashV2(nullptr, 0,
                                      &snapshot.event_hash) ||
        !Worr_SnapshotCalculateHashV2(
            &snapshot, CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES,
            &snapshot.snapshot_hash)) {
        return false;
    }
    return true;
}

worr_cgame_prediction_input_range_v1 make_input(
    const worr_snapshot_consumed_command_v2 &consumed)
{
    worr_cgame_prediction_input_range_v1 input{};
    input.struct_size = sizeof(input);
    input.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    input.result = WORR_CGAME_PREDICTION_INPUT_OK;
    input.source = WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    input.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    input.consumed_command = consumed;
    input.authoritative_legacy_sequence = 100u;
    input.current_legacy_sequence = 102u;
    input.command_count = 2u;
    for (std::uint32_t index = 0; index < input.command_count; ++index) {
        auto &entry = input.commands[index];
        entry.legacy_sequence =
            input.authoritative_legacy_sequence + index + 1u;
        entry.command_id.epoch = kCommandEpoch;
        entry.command_id.sequence =
            kConsumedCommandSequence + index + 1u;
        entry.command.struct_size = sizeof(entry.command);
        entry.command.schema_version = WORR_PREDICTION_ABI_VERSION;
        entry.command.duration_ms = 16u;
    }
    return input;
}

cg_prediction_authority_candidate_v1 make_candidate()
{
    cg_prediction_authority_candidate_v1 candidate{};
    auto &timeline = candidate.timeline;
    timeline.struct_size = sizeof(timeline);
    timeline.schema_version = CG_CANONICAL_PREDICTION_SNAPSHOT_VERSION;
    timeline.active_epoch = kSnapshotEpoch;
    timeline.ref.slot = 3u;
    timeline.ref.generation = 11u;

    auto &snapshot = timeline.snapshot;
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    snapshot.flags = WORR_SNAPSHOT_FLAG_COMPLETE |
                     WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS |
                     WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
    snapshot.snapshot_id = {kSnapshotEpoch, kSnapshotSequence};
    snapshot.base_id = {kSnapshotEpoch, kSnapshotSequence - 1u};
    snapshot.server_tick = kServerTick;
    snapshot.server_time_us = kServerTimeUs;
    snapshot.controlled_entity = controlled_entity();
    snapshot.consumed_command.cursor = {
        kCommandEpoch, kConsumedCommandSequence};
    snapshot.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    snapshot.discontinuity.previous = {
        kSnapshotEpoch, kSnapshotSequence - 1u};
    snapshot.discontinuity.server_tick_delta = 1u;

    timeline.player = make_player();
    candidate.input = make_input(snapshot.consumed_command);
    if (!refresh_hashes(candidate))
        std::abort();
    return candidate;
}

cg_prediction_authority_expectation_v1 make_expectation()
{
    cg_prediction_authority_expectation_v1 expectation{};
    expectation.snapshot_sequence = kSnapshotSequence;
    expectation.server_tick = kServerTick;
    expectation.server_time_us = kServerTimeUs;
    expectation.controlled_entity_index = kControlledEntityIndex;
    return expectation;
}

cg_prediction_authority_result_v1 select(
    const cg_prediction_authority_expectation_v1 &expectation,
    const cg_prediction_authority_candidate_v1 &candidate,
    cg_prediction_authority_v1 *authority_out = nullptr)
{
    cg_prediction_authority_v1 discarded{};
    return CG_PredictionAuthoritySelectV1(
        &expectation, &candidate,
        authority_out ? authority_out : &discarded);
}

} // namespace

int main()
{
    const auto exact_expectation = make_expectation();
    const auto exact_candidate = make_candidate();

    cg_prediction_authority_v1 authority{};
    CHECK(select(exact_expectation, exact_candidate, &authority) ==
          cg_prediction_authority_result_v1::canonical);
    CHECK(authority.result ==
          cg_prediction_authority_result_v1::canonical);
    CHECK(authority.history_reset_required == 0u);
    CHECK(std::memcmp(&authority.timeline, &exact_candidate.timeline,
                      sizeof(authority.timeline)) == 0);
    CHECK(std::memcmp(&authority.input, &exact_candidate.input,
                      sizeof(authority.input)) == 0);

    auto candidate = exact_candidate;
    candidate.timeline.snapshot.consumed_command.cursor.contiguous_sequence =
        0u;
    std::array<worr_cgame_prediction_input_command_v1, 2> bootstrap_history{
        candidate.input.commands[0],
        candidate.input.commands[1],
    };
    for (std::uint32_t index = 0; index < bootstrap_history.size();
         ++index) {
        bootstrap_history[index].command_id = {
            kCommandEpoch, index + 1u};
    }
    worr_prediction_input_resolve_request_v1 bootstrap_request{};
    bootstrap_request.struct_size = sizeof(bootstrap_request);
    bootstrap_request.schema_version =
        WORR_PREDICTION_INPUT_RESOLVER_VERSION;
    bootstrap_request.flags =
        WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY |
        WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED;
    bootstrap_request.identity_initial_epoch = kCommandEpoch;
    bootstrap_request.identity_baseline_legacy_sequence = 100u;
    bootstrap_request.current_legacy_sequence = 102u;
    bootstrap_request.history_count = bootstrap_history.size();
    bootstrap_request.consumed_command =
        candidate.timeline.snapshot.consumed_command;
    bootstrap_request.history = bootstrap_history.data();
    CHECK(Worr_PredictionInputResolveV1(
              &bootstrap_request, &candidate.input) ==
          WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(refresh_hashes(candidate));
    CHECK(select(exact_expectation, candidate, &authority) ==
          cg_prediction_authority_result_v1::canonical);
    CHECK(authority.input.commands[0].command_id.sequence == 1u);
    CHECK(authority.input.commands[1].command_id.sequence == 2u);

    CHECK(CG_PredictionAuthoritySelectV1(
              nullptr, &exact_candidate, &authority) ==
          cg_prediction_authority_result_v1::invalid_argument);
    CHECK(CG_PredictionAuthoritySelectV1(
              &exact_expectation, nullptr, &authority) ==
          cg_prediction_authority_result_v1::invalid_argument);
    CHECK(CG_PredictionAuthoritySelectV1(
              &exact_expectation, &exact_candidate, nullptr) ==
          cg_prediction_authority_result_v1::invalid_argument);

    candidate = exact_candidate;
    candidate.timeline.ref.slot = WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT;
    candidate.timeline.ref.generation = 0u;
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::timeline_unavailable);

    candidate = exact_candidate;
    candidate.timeline.snapshot.snapshot_hash ^= UINT64_C(1);
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::snapshot_invalid);

    candidate = exact_candidate;
    candidate.timeline.player.movement.origin[0] += 1.0f;
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::snapshot_invalid);

    candidate = exact_candidate;
    candidate.timeline.snapshot.flags &=
        ~WORR_SNAPSHOT_FLAG_COMPLETE;
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::snapshot_incomplete);

    candidate = exact_candidate;
    candidate.timeline.snapshot.flags &=
        ~WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
    CHECK(refresh_hashes(candidate));
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::snapshot_incomplete);

    auto expectation = exact_expectation;
    ++expectation.snapshot_sequence;
    CHECK(select(expectation, exact_candidate) ==
          cg_prediction_authority_result_v1::snapshot_misaligned);
    expectation = exact_expectation;
    --expectation.snapshot_sequence;
    CHECK(select(expectation, exact_candidate) ==
          cg_prediction_authority_result_v1::snapshot_misaligned);
    expectation = exact_expectation;
    ++expectation.server_tick;
    CHECK(select(expectation, exact_candidate) ==
          cg_prediction_authority_result_v1::snapshot_misaligned);
    expectation = exact_expectation;
    ++expectation.server_time_us;
    CHECK(select(expectation, exact_candidate) ==
          cg_prediction_authority_result_v1::snapshot_misaligned);

    candidate = exact_candidate;
    ++candidate.timeline.active_epoch;
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::snapshot_misaligned);

    candidate = exact_candidate;
    candidate.timeline.snapshot.flags &=
        ~WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
    candidate.timeline.snapshot.flags |=
        WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED;
    candidate.timeline.snapshot.discontinuity.flags |=
        WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED;
    candidate.timeline.snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_TRANSPORT_TRUNCATED;
    CHECK(refresh_hashes(candidate));
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::snapshot_truncated);

    expectation = exact_expectation;
    ++expectation.controlled_entity_index;
    CHECK(select(expectation, exact_candidate) ==
          cg_prediction_authority_result_v1::controlled_entity_mismatch);

    candidate = exact_candidate;
    ++candidate.timeline.player.controlled_entity.identity.index;
    CHECK(refresh_hashes(candidate));
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::controlled_entity_mismatch);

    candidate = exact_candidate;
    ++candidate.timeline.player.controlled_entity.identity.generation;
    CHECK(refresh_hashes(candidate));
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::controlled_entity_mismatch);

    candidate = exact_candidate;
    ++candidate.input.consumed_command.cursor.contiguous_sequence;
    for (std::uint32_t index = 0;
         index < candidate.input.command_count; ++index) {
        ++candidate.input.commands[index].command_id.sequence;
    }
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::consumed_cursor_mismatch);

    candidate = exact_candidate;
    candidate.timeline.snapshot.consumed_command = {};
    CHECK(refresh_hashes(candidate));
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::consumed_cursor_mismatch);

    candidate = exact_candidate;
    candidate.input.result =
        WORR_CGAME_PREDICTION_INPUT_INVALID_METADATA;
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::input_range_invalid);

    candidate = exact_candidate;
    ++candidate.input.current_legacy_sequence;
    CHECK(select(exact_expectation, candidate) ==
          cg_prediction_authority_result_v1::input_range_invalid);

    candidate = exact_candidate;
    candidate.timeline.snapshot.flags |= WORR_SNAPSHOT_FLAG_KEYFRAME;
    candidate.timeline.snapshot.base_id = {};
    candidate.timeline.snapshot.discontinuity.flags =
        WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT |
        WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC;
    candidate.timeline.snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_HARD_RESYNC;
    CHECK(refresh_hashes(candidate));
    authority = {};
    CHECK(select(exact_expectation, candidate, &authority) ==
          cg_prediction_authority_result_v1::canonical);
    CHECK(authority.history_reset_required == 1u);

    std::puts("cgame_prediction_snapshot_authority_test: ok");
    return EXIT_SUCCESS;
}
