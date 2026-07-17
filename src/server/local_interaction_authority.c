/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server/local_interaction_authority.h"

#include "shared/shared.h"

#include <string.h>

#define SV_LOCAL_INTERACTION_AUTHORITY_CAPACITY 32u

typedef struct local_interaction_authority_slot_s {
    worr_local_interaction_authority_receipt_v1 receipt;
    uint8_t occupied;
    uint8_t reserved[7];
} local_interaction_authority_slot_t;

static local_interaction_authority_slot_t
    mailboxes[MAX_CLIENTS][SV_LOCAL_INTERACTION_AUTHORITY_CAPACITY];

static bool command_id_equal(worr_command_id_v1 left,
                             worr_command_id_v1 right)
{
    return left.epoch == right.epoch && left.sequence == right.sequence;
}

static bool publish_receipt(
    uint32_t client_index,
    const worr_local_interaction_authority_receipt_v1 *receipt)
{
    uint32_t index;
    int free_index = -1;

    if (client_index >= MAX_CLIENTS ||
        !Worr_LocalInteractionAuthorityReceiptValidateV1(receipt)) {
        return false;
    }
    for (index = 0; index < SV_LOCAL_INTERACTION_AUTHORITY_CAPACITY;
         ++index) {
        local_interaction_authority_slot_t *slot =
            &mailboxes[client_index][index];
        if (!slot->occupied) {
            if (free_index < 0)
                free_index = (int)index;
            continue;
        }
        if (!command_id_equal(slot->receipt.command_id, receipt->command_id))
            continue;
        return memcmp(&slot->receipt, receipt, sizeof(*receipt)) == 0;
    }
    if (free_index < 0)
        return false;

    mailboxes[client_index][free_index].receipt = *receipt;
    mailboxes[client_index][free_index].occupied = 1;
    return true;
}

static const worr_local_interaction_authority_import_v1 authority_import = {
    sizeof(authority_import),
    WORR_LOCAL_INTERACTION_AUTHORITY_API_VERSION,
    publish_receipt,
};

void SV_LocalInteractionAuthorityResetMap(void)
{
    memset(mailboxes, 0, sizeof(mailboxes));
}

const worr_local_interaction_authority_import_v1 *
SV_LocalInteractionAuthorityImportV1(void)
{
    return &authority_import;
}

bool SV_LocalInteractionAuthorityTakeReceiptForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_interaction_authority_receipt_v1 *receipt_out)
{
    uint32_t index;

    if (client_index >= MAX_CLIENTS || !receipt_out ||
        !Worr_CommandIdValidV1(command_id, false)) {
        return false;
    }
    for (index = 0; index < SV_LOCAL_INTERACTION_AUTHORITY_CAPACITY;
         ++index) {
        local_interaction_authority_slot_t *slot =
            &mailboxes[client_index][index];
        if (!slot->occupied ||
            !command_id_equal(slot->receipt.command_id, command_id)) {
            continue;
        }
        if (!Worr_LocalInteractionAuthorityReceiptValidateV1(&slot->receipt)) {
            *slot = (local_interaction_authority_slot_t){0};
            return false;
        }
        *receipt_out = slot->receipt;
        *slot = (local_interaction_authority_slot_t){0};
        return true;
    }
    return false;
}
