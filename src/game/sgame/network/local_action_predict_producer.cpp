/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "local_action_predict_producer.hpp"

#include "local_action_weapon_catalog.hpp"

#include "../g_local.hpp"

#include "shared/local_action_rule_provider.h"
#include "shared/weapon_predict_seed_mailbox.h"

#include <cstring>

static cvar_t *sv_worr_weapon_predict = nullptr;

void SG_WeaponPredictInitCvars() {
  sv_worr_weapon_predict =
      gi.cvar("sv_worr_weapon_predict", "0", CVAR_NOFLAGS);
}

bool SG_WeaponPredictEnabled() {
  if (!sv_worr_weapon_predict)
    SG_WeaponPredictInitCvars();
  return sv_worr_weapon_predict && sv_worr_weapon_predict->integer != 0;
}

bool SG_WeaponPredictBuildSeed(const gentity_t *entity, uint32_t command_epoch,
                               worr_local_action_predict_seed_v1 *seed_out) {
  if (seed_out)
    memset(seed_out, 0, sizeof(*seed_out));
  if (!SG_WeaponPredictEnabled() || entity == nullptr ||
      entity->client == nullptr || command_epoch == 0 || seed_out == nullptr) {
    return false;
  }

  const Item *weapon = entity->client->pers.weapon;
  if (weapon == nullptr)
    return false;

  const worr_local_action_catalog_id_v1 catalog =
      SG_LocalActionCatalogFromItem(weapon->id);
  if (!Worr_LocalActionRuleProviderPredictableV1(
          static_cast<uint32_t>(catalog))) {
    return false;
  }

  uint32_t ammo = 0;
  if (weapon->ammo != IT_NULL) {
    const int32_t count = entity->client->pers.inventory[weapon->ammo];
    ammo = count > 0 ? static_cast<uint32_t>(count) : 0u;
  }

  /* READY-boundary seed: the client predicts forward from a stable idle
   * state.  Mid-cycle phase reconstruction (the LEGACY_FRAME_TIMELINE gap)
   * would need the legacy weapon-frame mapping and is deliberately omitted. */
  worr_local_action_state_v2 state;
  memset(&state, 0, sizeof(state));
  if (!Worr_LocalActionStateInitV2(&state, command_epoch,
                                   static_cast<uint32_t>(catalog), ammo, 0)) {
    return false;
  }
  return Worr_LocalActionPredictSeedBuildV1(&state, seed_out);
}

static const worr_weapon_predict_seed_import_v1 *sv_seed_import = nullptr;

void SG_WeaponPredictPublishSeed(const gentity_t *entity) {
  worr_local_action_predict_seed_v1 seed;

  if (!SG_WeaponPredictEnabled() || entity == nullptr ||
      entity->client == nullptr) {
    return;
  }
  if (sv_seed_import == nullptr) {
    sv_seed_import = static_cast<const worr_weapon_predict_seed_import_v1 *>(
        gi.GetExtension(WORR_WEAPON_PREDICT_SEED_IMPORT_V1));
    if (sv_seed_import == nullptr ||
        sv_seed_import->struct_size != sizeof(*sv_seed_import) ||
        sv_seed_import->api_version !=
            WORR_WEAPON_PREDICT_SEED_IMPORT_API_VERSION ||
        sv_seed_import->PublishSeed == nullptr) {
      sv_seed_import = nullptr;
      return;
    }
  }

  /* command_epoch is a placeholder: the engine overwrites the applied cursor
   * with the exact consumed command boundary when it drains this seed. */
  if (!SG_WeaponPredictBuildSeed(entity, 1u, &seed))
    return;
  (void)sv_seed_import->PublishSeed(
      static_cast<uint32_t>(entity->s.number - 1), &seed);
}
