/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "cg_canonical_snapshot_timeline.hpp"
#include "shared/cgame_prediction.h"

#include <cstdint>

enum class cg_prediction_authority_result_v1 : std::uint32_t {
    canonical = 0,
    invalid_argument,
    timeline_unavailable,
    snapshot_invalid,
    snapshot_incomplete,
    snapshot_misaligned,
    snapshot_truncated,
    controlled_entity_mismatch,
    consumed_cursor_mismatch,
    input_range_invalid,
};

struct cg_prediction_authority_expectation_v1 {
    std::uint32_t snapshot_sequence;
    std::uint32_t server_tick;
    std::uint64_t server_time_us;
    std::uint32_t controlled_entity_index;
};

struct cg_prediction_authority_candidate_v1 {
    cg_canonical_prediction_snapshot_v1 timeline;
    worr_cgame_prediction_input_range_v1 input;
};

struct cg_prediction_authority_v1 {
    cg_prediction_authority_result_v1 result;
    cg_canonical_prediction_snapshot_v1 timeline;
    worr_cgame_prediction_input_range_v1 input;
    std::uint32_t history_reset_required;
};

cg_prediction_authority_result_v1 CG_PredictionAuthoritySelectV1(
    const cg_prediction_authority_expectation_v1 *expectation,
    const cg_prediction_authority_candidate_v1 *candidate,
    cg_prediction_authority_v1 *authority_out);

const char *CG_PredictionAuthorityResultName(
    cg_prediction_authority_result_v1 result);
