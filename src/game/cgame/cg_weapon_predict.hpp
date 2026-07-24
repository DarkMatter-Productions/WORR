/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/local_action_predictor.h"
#include "shared/cgame_prediction.h"
#include "shared/command_abi.h"

/*
 * FR-10-T08 client weapon-prediction seam (defined in cg_predict.cpp,
 * default-off, UNVERIFIED).  A no-op unless cg_predict_weapon is set.
 */
void CG_WeaponPredictReset();
bool CG_WeaponPredictApplySeed(const worr_local_action_predict_seed_v1 *seed);
bool CG_WeaponPredictReplayCommand(const worr_command_record_v1 *command,
                                   bool attack_held);
bool CG_WeaponPredictGetState(worr_local_action_state_v2 *state_out);

/*
 * Registers the seed-apply callback the event runtime invokes when a predict
 * seed arrives.  Passing null (the default) leaves seed batches consumed but
 * inert, which keeps the event runtime free of any hard link to cg_predict.
 */
typedef bool (*cg_weapon_predict_seed_apply_fn)(
    const worr_local_action_predict_seed_v1 *seed);
void CG_EventRuntimeSetWeaponSeedApply(cg_weapon_predict_seed_apply_fn fn);

/*
 * Replays the canonical command range through the client weapon predictor
 * (defined in cg_local_interaction.cpp, which owns the canonical command
 * record import).  A no-op when disabled or the predictor is unseeded.
 */
void CG_WeaponPredictReplayRange(
    const worr_cgame_prediction_input_range_v1 &prediction_range);

/*
 * Registers the per-command replay callback invoked by CG_WeaponPredictReplayRange.
 * Null (the default) leaves the range walk inert, so cg_local_interaction has
 * no hard link to cg_predict and its unit tests build without it.
 */
typedef bool (*cg_weapon_predict_replay_fn)(
    const worr_command_record_v1 *command, bool attack_held);
void CG_WeaponPredictSetReplay(cg_weapon_predict_replay_fn fn);
