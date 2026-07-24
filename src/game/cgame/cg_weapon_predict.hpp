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
bool CG_WeaponPredictEnabled();
bool CG_WeaponPredictApplySeed(const worr_local_action_predict_seed_v1 *seed);
bool CG_WeaponPredictReplayCommand(const worr_command_record_v1 *command,
                                   bool attack_held);
bool CG_WeaponPredictGetState(worr_local_action_state_v2 *state_out);

/*
 * Predicted presenter (default-off): returns the predicted active-weapon ammo
 * for the local player's HUD when the predictor is actively seeded for a live
 * weapon, otherwise returns authoritative_ammo unchanged.  It only ever
 * substitutes a real, already-displayable count (authoritative_ammo >= 0) and
 * converges to authority as snapshots arrive, so it cannot show ammo for a
 * hidden/no-ammo weapon.  The predicted fire cue (muzzle flash/sound) is
 * deliberately NOT presented here: the authoritative path already presents the
 * local muzzle, so a predicted cue would need present-once dedup against it to
 * avoid a double flash -- a reconciliation-aware change that a running game
 * must verify.
 */
int CG_WeaponPredictPresentAmmo(int authoritative_ammo);

/*
 * Predicted fire-cue presenter (default-off, best-effort, UNVERIFIED).  The
 * predictor owns the local player's muzzle flash/sound: when a predicted shot
 * lands the emit callback presents the cue early, and the matching
 * authoritative local muzzle is suppressed via a debt counter (see
 * cg_effects.cpp), so exactly one cue shows per shot.  The cue is driven off a
 * monotonic shot-sequence high-water, so re-prediction of the same command
 * never re-emits; debt and high-water reset on every seed apply and hard
 * resync, bounding drift to one reconciliation window.  It is a strict no-op
 * while cg_predict_weapon is 0 (nothing ever increments the debt, so nothing is
 * ever suppressed).  Its runtime correctness depends on the (still
 * running-game-unverified) weapon replay cadence; see the verification runbook.
 *
 * The callbacks live in cg_effects.cpp (which owns the muzzle infrastructure)
 * and are registered into cg_predict.cpp from CL_InitEffects; they default to
 * null, so cg_predict's unit tests link without cg_effects.
 */
typedef void (*cg_weapon_predict_fire_emit_fn)(uint32_t shots);
typedef void (*cg_weapon_predict_fire_reset_fn)(void);
void CG_WeaponPredictSetFireCue(cg_weapon_predict_fire_emit_fn emit_fn,
                                cg_weapon_predict_fire_reset_fn reset_fn);

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
