/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server/local_interaction_authority.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "local interaction authority test:%d: %s\n", \
                    __LINE__, #condition);                                 \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static worr_local_interaction_authority_receipt_v1 make_receipt(
    uint32_t epoch, uint32_t sequence)
{
    worr_local_interaction_authority_receipt_v1 receipt;
    memset(&receipt, 0, sizeof(receipt));
    receipt.struct_size = sizeof(receipt);
    receipt.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
    receipt.command_id.epoch = epoch;
    receipt.command_id.sequence = sequence;
    receipt.command_hash = UINT64_C(0x1100000000000000) | sequence;
    receipt.state_hash = UINT64_C(0x2200000000000000) | sequence;
    receipt.transaction_hash = UINT64_C(0x3300000000000000) | sequence;
    receipt.action_sequence = sequence;
    receipt.state_flags = WORR_LOCAL_INTERACTION_STATE_HOOK_HELD;
    receipt.outcome_flags = WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED |
                            WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REJECTED;
    if (!Worr_LocalInteractionAuthorityReceiptValidateV1(&receipt))
        return (worr_local_interaction_authority_receipt_v1){0};
    return receipt;
}

int main(void)
{
    const worr_local_interaction_authority_import_v1 *import;
    worr_local_interaction_authority_receipt_v1 receipt;
    worr_local_interaction_authority_receipt_v1 copied;
    uint32_t index;

    SV_LocalInteractionAuthorityResetMap();
    import = SV_LocalInteractionAuthorityImportV1();
    CHECK(import != NULL);
    CHECK(import->struct_size == sizeof(*import));
    CHECK(import->api_version == WORR_LOCAL_INTERACTION_AUTHORITY_API_VERSION);
    CHECK(import->PublishReceipt != NULL);

    receipt = make_receipt(3, 1);
    CHECK(import->PublishReceipt(2, &receipt));
    CHECK(import->PublishReceipt(2, &receipt));
    CHECK(!SV_LocalInteractionAuthorityTakeReceiptForCommand(
        2, (worr_command_id_v1){3, 2}, &copied));
    CHECK(SV_LocalInteractionAuthorityTakeReceiptForCommand(
        2, receipt.command_id, &copied));
    CHECK(memcmp(&copied, &receipt, sizeof(receipt)) == 0);
    CHECK(!SV_LocalInteractionAuthorityTakeReceiptForCommand(
        2, receipt.command_id, &copied));

    receipt = make_receipt(4, 1);
    CHECK(import->PublishReceipt(2, &receipt));
    receipt.state_hash ^= UINT64_C(1);
    CHECK(Worr_LocalInteractionAuthorityReceiptValidateV1(&receipt));
    CHECK(!import->PublishReceipt(2, &receipt));
    CHECK(SV_LocalInteractionAuthorityTakeReceiptForCommand(
        2, receipt.command_id, &copied));
    CHECK(copied.state_hash != receipt.state_hash);

    SV_LocalInteractionAuthorityResetMap();
    for (index = 1; index <= 32; ++index) {
        receipt = make_receipt(5, index);
        CHECK(import->PublishReceipt(7, &receipt));
    }
    receipt = make_receipt(5, 33);
    CHECK(!import->PublishReceipt(7, &receipt));
    receipt = make_receipt(5, 1);
    CHECK(SV_LocalInteractionAuthorityTakeReceiptForCommand(
        7, receipt.command_id, &copied));
    receipt = make_receipt(5, 33);
    CHECK(import->PublishReceipt(7, &receipt));

    receipt = make_receipt(6, 1);
    CHECK(import->PublishReceipt(8, &receipt));
    CHECK(!SV_LocalInteractionAuthorityTakeReceiptForCommand(
        7, receipt.command_id, &copied));
    CHECK(SV_LocalInteractionAuthorityTakeReceiptForCommand(
        8, receipt.command_id, &copied));
    SV_LocalInteractionAuthorityResetMap();
    CHECK(!SV_LocalInteractionAuthorityTakeReceiptForCommand(
        7, (worr_command_id_v1){5, 2}, &copied));

    puts("local interaction authority tests passed");
    return 0;
}
