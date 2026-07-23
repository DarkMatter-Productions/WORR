/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server/local_action_shadow_authority.h"

#include "shared/shared.h"

#include <string.h>

typedef struct local_action_shadow_authority_slot_s {
    worr_local_action_shadow_authority_receipt_v1 receipt;
    uint64_t publish_order;
    uint8_t occupied;
    uint8_t reserved[7];
} local_action_shadow_authority_slot_t;

/*
 * Retain the most recently admitted receipt after mailbox consumption.  This
 * is the bounded command-ID frontier for one client: an exact retransmission
 * of the frontier remains idempotent, while a conflicting or regressed ID
 * cannot become a new FIFO entry.  A connection/map reset starts a new epoch.
 */
typedef struct local_action_shadow_authority_frontier_s {
    worr_local_action_shadow_authority_receipt_v1 receipt;
    uint8_t initialized;
    uint8_t reserved[7];
} local_action_shadow_authority_frontier_t;

static local_action_shadow_authority_slot_t
    mailboxes[MAX_CLIENTS]
             [WORR_LOCAL_ACTION_SHADOW_AUTHORITY_MAILBOX_CAPACITY];
static local_action_shadow_authority_frontier_t frontiers[MAX_CLIENTS];
static sv_local_action_shadow_authority_failure_v1 failures[MAX_CLIENTS];
static uint8_t failed[MAX_CLIENTS];
static uint64_t next_publish_order;

static void latch_failure(
    uint32_t client_index,
    sv_local_action_shadow_authority_failure_v1 failure)
{
    if (client_index >= MAX_CLIENTS ||
        failure == SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE ||
        failures[client_index] !=
            SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE) {
        return;
    }
    failures[client_index] = failure;
    failed[client_index] = 1;
}

static bool command_id_equal(worr_command_id_v1 left,
                             worr_command_id_v1 right)
{
    return left.epoch == right.epoch && left.sequence == right.sequence;
}

static bool publish_receipt(
    uint32_t client_index,
    const worr_local_action_shadow_authority_receipt_v1 *receipt)
{
    local_action_shadow_authority_frontier_t *frontier;
    uint32_t index;
    int free_index = -1;

    if (client_index >= MAX_CLIENTS)
        return false;
    if (failed[client_index])
        return false;
    if (!Worr_LocalActionShadowAuthorityReceiptValidateV1(receipt)) {
        latch_failure(
            client_index,
            SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_INVALID_RECEIPT);
        return false;
    }
    frontier = &frontiers[client_index];

    if (frontier->initialized) {
        if (receipt->command_id.epoch != frontier->receipt.command_id.epoch) {
            latch_failure(
                client_index,
                SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_EPOCH_CHANGE);
            return false;
        }
        if (receipt->command_id.sequence <
            frontier->receipt.command_id.sequence) {
            latch_failure(
                client_index,
                SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_COMMAND_REGRESSION);
            return false;
        }
        if (receipt->command_id.sequence ==
            frontier->receipt.command_id.sequence) {
            if (memcmp(&frontier->receipt, receipt, sizeof(*receipt)) == 0)
                return true;
            latch_failure(
                client_index,
                SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_COMMAND_CONFLICT);
            return false;
        }
    }

    for (index = 0;
         index < WORR_LOCAL_ACTION_SHADOW_AUTHORITY_MAILBOX_CAPACITY;
         ++index) {
        local_action_shadow_authority_slot_t *slot =
            &mailboxes[client_index][index];
        if (!slot->occupied) {
            if (free_index < 0)
                free_index = (int)index;
            continue;
        }
        if (!command_id_equal(slot->receipt.command_id, receipt->command_id))
            continue;
        if (memcmp(&slot->receipt, receipt, sizeof(*receipt)) == 0)
            return true;
        latch_failure(
            client_index,
            SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_COMMAND_CONFLICT);
        return false;
    }

    if (free_index < 0) {
        latch_failure(
            client_index,
            SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_CAPACITY);
        return false;
    }
    if (next_publish_order == UINT64_MAX) {
        latch_failure(
            client_index,
            SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_ORDER_EXHAUSTED);
        return false;
    }

    ++next_publish_order;
    mailboxes[client_index][free_index].receipt = *receipt;
    mailboxes[client_index][free_index].publish_order = next_publish_order;
    mailboxes[client_index][free_index].occupied = 1;
    frontier->receipt = *receipt;
    frontier->initialized = 1;
    return true;
}

static const worr_local_action_shadow_authority_import_v1 authority_import = {
    sizeof(authority_import),
    WORR_LOCAL_ACTION_SHADOW_AUTHORITY_API_VERSION,
    publish_receipt,
};

void SV_LocalActionShadowAuthorityResetMap(void)
{
    memset(mailboxes, 0, sizeof(mailboxes));
    memset(frontiers, 0, sizeof(frontiers));
    memset(failures, 0, sizeof(failures));
    memset(failed, 0, sizeof(failed));
    next_publish_order = 0;
}

void SV_LocalActionShadowAuthorityResetClient(uint32_t client_index)
{
    if (client_index >= MAX_CLIENTS)
        return;
    memset(mailboxes[client_index], 0, sizeof(mailboxes[client_index]));
    memset(&frontiers[client_index], 0, sizeof(frontiers[client_index]));
    failures[client_index] =
        SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    failed[client_index] = 0;
}

const worr_local_action_shadow_authority_import_v1 *
SV_LocalActionShadowAuthorityImportV1(void)
{
    return &authority_import;
}

static int oldest_receipt_index(uint32_t client_index)
{
    uint32_t index;
    int selected = -1;
    uint64_t selected_order = UINT64_MAX;

    for (index = 0;
         index < WORR_LOCAL_ACTION_SHADOW_AUTHORITY_MAILBOX_CAPACITY;
         ++index) {
        local_action_shadow_authority_slot_t *slot =
            &mailboxes[client_index][index];
        if (!slot->occupied || slot->publish_order >= selected_order)
            continue;
        selected = (int)index;
        selected_order = slot->publish_order;
    }
    return selected;
}

bool SV_LocalActionShadowAuthorityPeekNextReceipt(
    uint32_t client_index,
    worr_local_action_shadow_authority_receipt_v1 *receipt_out)
{
    int selected;

    if (client_index >= MAX_CLIENTS || !receipt_out)
        return false;
    selected = oldest_receipt_index(client_index);
    if (selected < 0)
        return false;
    if (!Worr_LocalActionShadowAuthorityReceiptValidateV1(
            &mailboxes[client_index][selected].receipt)) {
        memset(&mailboxes[client_index][selected], 0,
               sizeof(mailboxes[client_index][selected]));
        return false;
    }
    *receipt_out = mailboxes[client_index][selected].receipt;
    return true;
}

bool SV_LocalActionShadowAuthorityConsumeNextReceipt(
    uint32_t client_index,
    const worr_local_action_shadow_authority_receipt_v1 *expected)
{
    int selected;

    if (client_index >= MAX_CLIENTS ||
        !Worr_LocalActionShadowAuthorityReceiptValidateV1(expected)) {
        return false;
    }
    selected = oldest_receipt_index(client_index);
    if (selected < 0 ||
        memcmp(&mailboxes[client_index][selected].receipt, expected,
               sizeof(*expected)) != 0) {
        return false;
    }
    memset(&mailboxes[client_index][selected], 0,
           sizeof(mailboxes[client_index][selected]));
    return true;
}

bool SV_LocalActionShadowAuthorityTakeFailure(
    uint32_t client_index,
    sv_local_action_shadow_authority_failure_v1 *failure_out)
{
    if (client_index >= MAX_CLIENTS || !failure_out ||
        failures[client_index] ==
            SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE) {
        return false;
    }
    *failure_out = failures[client_index];
    failures[client_index] =
        SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    return true;
}
