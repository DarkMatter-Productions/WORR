/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/weapon_predict_seed_mailbox.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Engine-side per-client single-slot seed mailbox (FR-10-T08, default-off). */
const worr_weapon_predict_seed_import_v1 *SV_WeaponPredictSeedImportV1(void);

/* Copies and clears a client's pending seed.  False when none is pending. */
bool SV_WeaponPredictSeedTake(uint32_t client_index,
                              worr_local_action_predict_seed_v1 *seed_out);

void SV_WeaponPredictSeedResetClient(uint32_t client_index);
void SV_WeaponPredictSeedResetMap(void);

#ifdef __cplusplus
}
#endif
