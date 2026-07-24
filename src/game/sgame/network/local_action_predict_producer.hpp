/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/local_action_predictor.h"

struct gentity_t;

/*
 * FR-10-T08 server weapon-prediction producer seam (default-off, UNVERIFIED).
 *
 * Resolves a client's current weapon to the transport-neutral catalog id with
 * the existing SG_LocalActionCatalog adapter, reads its ammo, and builds the
 * authoritative READY-boundary seed (worr_local_action_predict_seed_v1) the
 * client uses to seed its predictor.  It moves no gameplay authority and is a
 * complete no-op when sv_worr_weapon_predict is 0 (default).  The actual emit
 * over the native channel and the per-command authoritative production hook
 * still require a running game to confirm.
 */
void SG_WeaponPredictInitCvars();
bool SG_WeaponPredictEnabled();

/*
 * Builds a READY seed for the entity's current weapon.  Returns false (leaving
 * seed_out zeroed) when disabled, the entity has no client/weapon, or the
 * weapon is not client-predictable.  command_epoch must be non-zero.
 */
bool SG_WeaponPredictBuildSeed(const gentity_t *entity,
                               uint32_t command_epoch,
                               worr_local_action_predict_seed_v1 *seed_out);

/*
 * Builds and publishes a READY seed for the entity's client to the engine seed
 * mailbox (the engine stamps the exact command boundary).  A no-op when
 * disabled, the extension is unavailable, or the weapon is not predictable.
 */
void SG_WeaponPredictPublishSeed(const gentity_t *entity);
