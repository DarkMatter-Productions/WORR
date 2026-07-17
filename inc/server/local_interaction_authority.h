/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/local_interaction_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clears process-local, map-scoped receipt mailboxes before sgame begins its
 * next SpawnEntities callback. */
void SV_LocalInteractionAuthorityResetMap(void);

/* Extension returned by the engine to sgame. */
const worr_local_interaction_authority_import_v1 *
SV_LocalInteractionAuthorityImportV1(void);

/* Moves one exact receipt out of the authenticated owner's bounded mailbox.
 * It is intentionally a one-shot transfer: once native queueing has observed
 * the receipt, that per-peer reliable sender owns retransmission/retirement. */
bool SV_LocalInteractionAuthorityTakeReceiptForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_interaction_authority_receipt_v1 *receipt_out);

#ifdef __cplusplus
}
#endif
