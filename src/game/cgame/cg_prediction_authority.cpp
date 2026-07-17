/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_prediction_authority.hpp"

namespace {

constexpr std::uint32_t history_reset_discontinuities =
    WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
    WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP |
    WORR_SNAPSHOT_DISCONTINUITY_BASE_JUMP |
    WORR_SNAPSHOT_DISCONTINUITY_FRAGMENT_STALL |
    WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET |
    WORR_SNAPSHOT_DISCONTINUITY_DEMO_REWIND |
    WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED |
    WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC |
    WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH;

bool cursor_equal(worr_snapshot_consumed_command_v2 left,
                  worr_snapshot_consumed_command_v2 right)
{
    return left.cursor.epoch == right.cursor.epoch &&
           left.cursor.contiguous_sequence ==
               right.cursor.contiguous_sequence &&
           left.provenance == right.provenance &&
           left.reserved0 == right.reserved0;
}

bool generation_equal(worr_snapshot_entity_generation_v2 left,
                      worr_snapshot_entity_generation_v2 right)
{
    return left.identity.index == right.identity.index &&
           left.identity.generation == right.identity.generation &&
           left.provenance_flags == right.provenance_flags &&
           left.reserved0 == right.reserved0;
}

bool command_valid(const worr_prediction_command_v1 &command)
{
    return command.struct_size == sizeof(command) &&
           command.schema_version == WORR_PREDICTION_ABI_VERSION &&
           command.reserved0 == 0;
}

bool input_range_valid(
    const worr_cgame_prediction_input_range_v1 &range,
    worr_snapshot_consumed_command_v2 consumed_command)
{
    constexpr std::uint32_t known_flags =
        WORR_CGAME_PREDICTION_INPUT_CANONICAL |
        WORR_CGAME_PREDICTION_INPUT_HAS_PENDING;
    if (range.struct_size != sizeof(range) ||
        range.api_version != WORR_CGAME_PREDICTION_INPUT_API_VERSION ||
        range.result != WORR_CGAME_PREDICTION_INPUT_OK ||
        range.source !=
            WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR ||
        (range.flags & ~known_flags) != 0 ||
        (range.flags & WORR_CGAME_PREDICTION_INPUT_CANONICAL) == 0 ||
        range.reserved0 != 0 || range.reserved1 != 0 ||
        range.command_count >= WORR_CGAME_PREDICTION_INPUT_CAPACITY ||
        !cursor_equal(range.consumed_command, consumed_command)) {
        return false;
    }
    if (range.command_count !=
        range.current_legacy_sequence -
            range.authoritative_legacy_sequence) {
        return false;
    }

    worr_command_id_v1 expected{};
    if (range.command_count != 0 &&
        !Worr_CommandCursorNextIdV1(consumed_command.cursor, &expected)) {
        return false;
    }
    for (std::uint32_t index = 0; index < range.command_count; ++index) {
        const auto &entry = range.commands[index];
        if (entry.legacy_sequence !=
                range.authoritative_legacy_sequence + index + 1u ||
            entry.command_id.epoch != expected.epoch ||
            entry.command_id.sequence != expected.sequence ||
            entry.reserved0 != 0 || !command_valid(entry.command)) {
            return false;
        }
        if (index + 1u < range.command_count &&
            !Worr_CommandIdNextV1(expected, &expected)) {
            return false;
        }
    }

    const bool has_pending =
        (range.flags & WORR_CGAME_PREDICTION_INPUT_HAS_PENDING) != 0;
    if (!has_pending)
        return true;
    return range.pending_command.legacy_sequence ==
               range.current_legacy_sequence + 1u &&
           range.pending_command.command_id.epoch == 0 &&
           range.pending_command.command_id.sequence == 0 &&
           range.pending_command.reserved0 == 0 &&
           command_valid(range.pending_command.command);
}

cg_prediction_authority_result_v1 fail(
    cg_prediction_authority_result_v1 result,
    cg_prediction_authority_v1 *authority_out)
{
    if (authority_out) {
        *authority_out = {};
        authority_out->result = result;
    }
    return result;
}

} // namespace

cg_prediction_authority_result_v1 CG_PredictionAuthoritySelectV1(
    const cg_prediction_authority_expectation_v1 *expectation,
    const cg_prediction_authority_candidate_v1 *candidate,
    cg_prediction_authority_v1 *authority_out)
{
    if (!expectation || !candidate || !authority_out ||
        expectation->snapshot_sequence == 0 ||
        expectation->controlled_entity_index == 0 ||
        expectation->controlled_entity_index >=
            CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES) {
        return fail(cg_prediction_authority_result_v1::invalid_argument,
                    authority_out);
    }

    const auto &timeline = candidate->timeline;
    const auto &snapshot = timeline.snapshot;
    const auto &player = timeline.player;
    if (timeline.struct_size != sizeof(timeline) ||
        timeline.schema_version !=
            CG_CANONICAL_PREDICTION_SNAPSHOT_VERSION ||
        timeline.active_epoch == 0 || timeline.reserved0 != 0 ||
        timeline.ref.slot == WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT ||
        timeline.ref.generation == 0) {
        return fail(cg_prediction_authority_result_v1::timeline_unavailable,
                    authority_out);
    }
    if (snapshot.flags & WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED)
        return fail(cg_prediction_authority_result_v1::snapshot_truncated,
                    authority_out);
    constexpr std::uint32_t required_flags =
        WORR_SNAPSHOT_FLAG_COMPLETE |
        WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
    if ((snapshot.flags & required_flags) != required_flags)
        return fail(cg_prediction_authority_result_v1::snapshot_incomplete,
                    authority_out);
    std::uint64_t player_hash = 0;
    if (!Worr_SnapshotValidateV2(
            &snapshot, CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES) ||
        !Worr_SnapshotPlayerValidateV2(
            &player, CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES) ||
        !Worr_SnapshotPlayerHashV2(
            &player, CG_CANONICAL_SNAPSHOT_MAX_ENTITY_IDENTITIES,
            &player_hash) ||
        player_hash != snapshot.player_hash) {
        return fail(cg_prediction_authority_result_v1::snapshot_invalid,
                    authority_out);
    }

    if (snapshot.snapshot_id.epoch != timeline.active_epoch ||
        snapshot.snapshot_id.sequence != expectation->snapshot_sequence ||
        snapshot.server_tick != expectation->server_tick ||
        snapshot.server_time_us != expectation->server_time_us) {
        return fail(cg_prediction_authority_result_v1::snapshot_misaligned,
                    authority_out);
    }
    if (!generation_equal(snapshot.controlled_entity,
                          player.controlled_entity) ||
        snapshot.controlled_entity.identity.index !=
            expectation->controlled_entity_index) {
        return fail(
            cg_prediction_authority_result_v1::controlled_entity_mismatch,
            authority_out);
    }
    if (snapshot.consumed_command.provenance !=
            WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED ||
        snapshot.consumed_command.reserved0 != 0 ||
        !Worr_CommandCursorValidV1(snapshot.consumed_command.cursor)) {
        return fail(
            cg_prediction_authority_result_v1::consumed_cursor_mismatch,
            authority_out);
    }
    if (!cursor_equal(candidate->input.consumed_command,
                      snapshot.consumed_command)) {
        return fail(
            cg_prediction_authority_result_v1::consumed_cursor_mismatch,
            authority_out);
    }
    if (!input_range_valid(candidate->input,
                           snapshot.consumed_command)) {
        return fail(cg_prediction_authority_result_v1::input_range_invalid,
                    authority_out);
    }

    cg_prediction_authority_v1 authority{};
    authority.result = cg_prediction_authority_result_v1::canonical;
    authority.timeline = timeline;
    authority.input = candidate->input;
    authority.history_reset_required =
        (snapshot.discontinuity.flags &
         history_reset_discontinuities) != 0;
    *authority_out = authority;
    return authority.result;
}

const char *CG_PredictionAuthorityResultName(
    cg_prediction_authority_result_v1 result)
{
    switch (result) {
    case cg_prediction_authority_result_v1::canonical:
        return "canonical";
    case cg_prediction_authority_result_v1::invalid_argument:
        return "invalid_argument";
    case cg_prediction_authority_result_v1::timeline_unavailable:
        return "timeline_unavailable";
    case cg_prediction_authority_result_v1::snapshot_invalid:
        return "snapshot_invalid";
    case cg_prediction_authority_result_v1::snapshot_incomplete:
        return "snapshot_incomplete";
    case cg_prediction_authority_result_v1::snapshot_misaligned:
        return "snapshot_misaligned";
    case cg_prediction_authority_result_v1::snapshot_truncated:
        return "snapshot_truncated";
    case cg_prediction_authority_result_v1::controlled_entity_mismatch:
        return "controlled_entity_mismatch";
    case cg_prediction_authority_result_v1::consumed_cursor_mismatch:
        return "consumed_cursor_mismatch";
    case cg_prediction_authority_result_v1::input_range_invalid:
        return "input_range_invalid";
    }
    return "unknown";
}
