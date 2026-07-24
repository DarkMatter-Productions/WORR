/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/local_action_predictor.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FR-10-T08 optional, process-local sgame -> server seed mailbox.  It is an
 * additive named extension (see PF_GetExtension), so an unaware engine or game
 * simply never requests it; it changes no existing ABI and never touches the
 * legacy wire.  Default-off: nothing publishes unless sv_worr_weapon_predict
 * is set.
 */
#define WORR_WEAPON_PREDICT_SEED_IMPORT_V1 "WORR_WEAPON_PREDICT_SEED_IMPORT_V1"
#define WORR_WEAPON_PREDICT_SEED_IMPORT_API_VERSION 1u

typedef struct worr_weapon_predict_seed_import_v1_s {
  uint32_t struct_size;
  uint32_t api_version;
  /* Stores the latest READY seed for a client; older unread seeds are
   * superseded.  Rejects an invalid seed. */
  bool (*PublishSeed)(uint32_t client_index,
                      const worr_local_action_predict_seed_v1 *seed);
} worr_weapon_predict_seed_import_v1;

#ifdef __cplusplus
}
#endif
