/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/local_action_predictor.h"
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
