/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/local_action_shadow.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sv_local_action_shadow_authority_failure_v1_e {
    SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE = 0,
    SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_INVALID_RECEIPT = 1,
    SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_EPOCH_CHANGE = 2,
    SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_COMMAND_REGRESSION = 3,
    SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_COMMAND_CONFLICT = 4,
    SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_CAPACITY = 5,
    SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_ORDER_EXHAUSTED = 6,
} sv_local_action_shadow_authority_failure_v1;

void SV_LocalActionShadowAuthorityResetMap(void);
void SV_LocalActionShadowAuthorityResetClient(uint32_t client_index);

const worr_local_action_shadow_authority_import_v1 *
SV_LocalActionShadowAuthorityImportV1(void);

/* Copies the oldest published receipt without removing it. */
bool SV_LocalActionShadowAuthorityPeekNextReceipt(
    uint32_t client_index,
    worr_local_action_shadow_authority_receipt_v1 *receipt_out);

/* Removes the oldest receipt only when it is byte-identical to expected. */
bool SV_LocalActionShadowAuthorityConsumeNextReceipt(
    uint32_t client_index,
    const worr_local_action_shadow_authority_receipt_v1 *expected);

/* Returns and clears the failure notification. A failed mailbox rejects all
 * further publications until client/map reset; exact duplicates do not fail. */
bool SV_LocalActionShadowAuthorityTakeFailure(
    uint32_t client_index,
    sv_local_action_shadow_authority_failure_v1 *failure_out);

#ifdef __cplusplus
}
#endif
