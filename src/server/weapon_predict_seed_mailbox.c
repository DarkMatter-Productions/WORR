/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server/weapon_predict_seed_mailbox.h"

#include <string.h>

#define WORR_WEAPON_PREDICT_SEED_MAILBOX_MAX_CLIENTS 256u

static struct {
    bool has_seed;
    worr_local_action_predict_seed_v1 seed;
} g_seed_mailbox[WORR_WEAPON_PREDICT_SEED_MAILBOX_MAX_CLIENTS];

/*
 * Structural check only: the client's Worr_LocalActionPredictSeedApplyV1 does
 * the full predictable-weapon and state validation, so the mailbox stays free
 * of the predictor/rule-provider link dependency.
 */
static bool seed_structurally_valid(
    const worr_local_action_predict_seed_v1 *seed)
{
    return seed != NULL && seed->struct_size == sizeof(*seed) &&
           seed->schema_version ==
               WORR_LOCAL_ACTION_PREDICT_SEED_ABI_VERSION &&
           seed->reserved0 == 0;
}

static bool publish_seed(uint32_t client_index,
                         const worr_local_action_predict_seed_v1 *seed)
{
    if (client_index >= WORR_WEAPON_PREDICT_SEED_MAILBOX_MAX_CLIENTS ||
        !seed_structurally_valid(seed)) {
        return false;
    }
    /* Latest seed supersedes any older unread one. */
    g_seed_mailbox[client_index].seed = *seed;
    g_seed_mailbox[client_index].has_seed = true;
    return true;
}

static const worr_weapon_predict_seed_import_v1 g_seed_import = {
    sizeof(g_seed_import), WORR_WEAPON_PREDICT_SEED_IMPORT_API_VERSION,
    publish_seed};

const worr_weapon_predict_seed_import_v1 *SV_WeaponPredictSeedImportV1(void)
{
    return &g_seed_import;
}

bool SV_WeaponPredictSeedTake(uint32_t client_index,
                              worr_local_action_predict_seed_v1 *seed_out)
{
    if (client_index >= WORR_WEAPON_PREDICT_SEED_MAILBOX_MAX_CLIENTS ||
        seed_out == NULL || !g_seed_mailbox[client_index].has_seed) {
        return false;
    }
    *seed_out = g_seed_mailbox[client_index].seed;
    g_seed_mailbox[client_index].has_seed = false;
    return true;
}

void SV_WeaponPredictSeedResetClient(uint32_t client_index)
{
    if (client_index < WORR_WEAPON_PREDICT_SEED_MAILBOX_MAX_CLIENTS)
        memset(&g_seed_mailbox[client_index], 0,
               sizeof(g_seed_mailbox[client_index]));
}

void SV_WeaponPredictSeedResetMap(void)
{
    memset(g_seed_mailbox, 0, sizeof(g_seed_mailbox));
}
