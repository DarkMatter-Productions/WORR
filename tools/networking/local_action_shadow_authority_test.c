#include "server/local_action_shadow_authority.h"
#include "shared/shared.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,         \
                    __LINE__, #condition);                                     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static worr_local_action_shadow_authority_receipt_v1 make_receipt(
    uint32_t epoch, uint32_t sequence)
{
    worr_command_record_v1 command;
    worr_local_action_observation_state_v1 before;
    worr_local_action_observation_state_v1 after;
    worr_local_action_observation_record_v1 observation;
    worr_local_action_shadow_v1 shadow;
    worr_local_action_shadow_authority_receipt_v1 receipt;
    memset(&command, 0, sizeof(command));
    command.struct_size = sizeof(command);
    command.schema_version = WORR_COMMAND_ABI_VERSION;
    command.command_id.epoch = epoch;
    command.command_id.sequence = sequence;
    command.sample_time_us = (uint64_t)sequence * UINT64_C(16000);
    command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    command.command.struct_size = sizeof(command.command);
    command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.command.duration_ms = 16;
    command.render_watermark.struct_size = sizeof(command.render_watermark);
    command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;

    memset(&before, 0, sizeof(before));
    before.struct_size = sizeof(before);
    before.schema_version = WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION;
    before.flags = WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ALIVE |
                   WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ELIGIBLE;
    before.phase = WORR_LOCAL_ACTION_OBSERVATION_READY;
    before.active_weapon_id = 9;
    before.presentation_frame = 7;
    before.presentation_rate = 10;
    after = before;
    after.presentation_frame = 8;

    memset(&observation, 0, sizeof(observation));
    memset(&shadow, 0, sizeof(shadow));
    memset(&receipt, 0, sizeof(receipt));
    if (!Worr_LocalActionObservationBuildV1(2, &command, &before, &after,
                                             &observation) ||
        !Worr_LocalActionShadowBuildV1(WORR_LOCAL_ACTION_CATALOG_BLASTER,
                                        &observation, &shadow) ||
        !Worr_LocalActionShadowAuthorityReceiptBuildV1(&shadow, &receipt)) {
        memset(&receipt, 0, sizeof(receipt));
    }
    return receipt;
}

int main(void)
{
    const worr_local_action_shadow_authority_import_v1 *import =
        SV_LocalActionShadowAuthorityImportV1();
    worr_local_action_shadow_authority_receipt_v1 receipt;
    worr_local_action_shadow_authority_receipt_v1 copied;
    sv_local_action_shadow_authority_failure_v1 failure;
    uint32_t sequence;

    CHECK(import && import->struct_size == sizeof(*import));
    CHECK(import->api_version ==
          WORR_LOCAL_ACTION_SHADOW_AUTHORITY_API_VERSION);
    CHECK(import->PublishReceipt != NULL);
    SV_LocalActionShadowAuthorityResetMap();
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(!SV_LocalActionShadowAuthorityTakeFailure(2, &failure));

    /* The exact latest frontier remains idempotent after FIFO consumption. */
    receipt = make_receipt(4, 1);
    CHECK(import->PublishReceipt(2, &receipt));
    CHECK(import->PublishReceipt(2, &receipt));
    memset(&copied, 0, sizeof(copied));
    CHECK(SV_LocalActionShadowAuthorityPeekNextReceipt(2, &copied));
    CHECK(copied.command_id.sequence == 1);
    memset(&copied, 0, sizeof(copied));
    CHECK(SV_LocalActionShadowAuthorityPeekNextReceipt(2, &copied));
    CHECK(copied.command_id.sequence == 1);
    CHECK(SV_LocalActionShadowAuthorityConsumeNextReceipt(2, &copied));
    CHECK(!SV_LocalActionShadowAuthorityPeekNextReceipt(2, &copied));
    CHECK(import->PublishReceipt(2, &receipt));
    CHECK(!SV_LocalActionShadowAuthorityPeekNextReceipt(2, &copied));
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(!SV_LocalActionShadowAuthorityTakeFailure(2, &failure));

    /* Conflicting equal-frontier bytes signal once and poison the mailbox
     * until the connection owner performs its normal client reset. */
    receipt.record_hash ^= UINT64_C(1);
    CHECK(!import->PublishReceipt(2, &receipt));
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(SV_LocalActionShadowAuthorityTakeFailure(2, &failure));
    CHECK(failure ==
          SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_COMMAND_CONFLICT);
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(!SV_LocalActionShadowAuthorityTakeFailure(2, &failure));
    receipt = make_receipt(4, 2);
    CHECK(!import->PublishReceipt(2, &receipt));
    SV_LocalActionShadowAuthorityResetClient(2);

    /* A lower sequence and an epoch change have distinct failure signals. */
    receipt = make_receipt(4, 2);
    CHECK(import->PublishReceipt(2, &receipt));
    receipt = make_receipt(4, 4);
    CHECK(import->PublishReceipt(2, &receipt));
    receipt = make_receipt(4, 3);
    CHECK(!import->PublishReceipt(2, &receipt));
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(SV_LocalActionShadowAuthorityTakeFailure(2, &failure));
    CHECK(failure ==
          SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_COMMAND_REGRESSION);
    CHECK(SV_LocalActionShadowAuthorityPeekNextReceipt(2, &copied));
    CHECK(copied.command_id.epoch == 4 && copied.command_id.sequence == 2);
    CHECK(SV_LocalActionShadowAuthorityConsumeNextReceipt(2, &copied));
    CHECK(SV_LocalActionShadowAuthorityPeekNextReceipt(2, &copied));
    CHECK(copied.command_id.epoch == 4 && copied.command_id.sequence == 4);
    CHECK(SV_LocalActionShadowAuthorityConsumeNextReceipt(2, &copied));
    CHECK(!SV_LocalActionShadowAuthorityPeekNextReceipt(2, &copied));
    SV_LocalActionShadowAuthorityResetClient(2);
    receipt = make_receipt(4, 1);
    CHECK(import->PublishReceipt(2, &receipt));
    receipt = make_receipt(5, 1);
    CHECK(!import->PublishReceipt(2, &receipt));
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(SV_LocalActionShadowAuthorityTakeFailure(2, &failure));
    CHECK(failure == SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_EPOCH_CHANGE);
    SV_LocalActionShadowAuthorityResetClient(2);
    CHECK(import->PublishReceipt(2, &receipt));
    CHECK(SV_LocalActionShadowAuthorityPeekNextReceipt(2, &copied));
    CHECK(copied.command_id.epoch == 5 && copied.command_id.sequence == 1);
    CHECK(SV_LocalActionShadowAuthorityConsumeNextReceipt(2, &copied));

    memset(&receipt, 0, sizeof(receipt));
    CHECK(!import->PublishReceipt(2, &receipt));
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(SV_LocalActionShadowAuthorityTakeFailure(2, &failure));
    CHECK(failure ==
          SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_INVALID_RECEIPT);
    SV_LocalActionShadowAuthorityResetClient(2);

    /* Capacity failure is reported directly. There is no test-only retry that
     * could hide the receipt lost by the production caller. */
    for (sequence = 1; sequence <= 32; ++sequence) {
        receipt = make_receipt(7, sequence);
        CHECK(import->PublishReceipt(3, &receipt));
    }
    receipt = make_receipt(7, 33);
    CHECK(!import->PublishReceipt(3, &receipt));
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(SV_LocalActionShadowAuthorityTakeFailure(3, &failure));
    CHECK(failure == SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_CAPACITY);
    for (sequence = 1; sequence <= 32; ++sequence) {
        memset(&copied, 0, sizeof(copied));
        CHECK(SV_LocalActionShadowAuthorityPeekNextReceipt(3, &copied));
        CHECK(copied.command_id.sequence == sequence);
        CHECK(SV_LocalActionShadowAuthorityConsumeNextReceipt(3, &copied));
    }
    CHECK(!SV_LocalActionShadowAuthorityPeekNextReceipt(3, &copied));
    receipt = make_receipt(7, 33);
    CHECK(!import->PublishReceipt(3, &receipt));
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(!SV_LocalActionShadowAuthorityTakeFailure(3, &failure));
    SV_LocalActionShadowAuthorityResetClient(3);
    CHECK(import->PublishReceipt(3, &receipt));
    CHECK(SV_LocalActionShadowAuthorityPeekNextReceipt(3, &copied));
    CHECK(copied.command_id.sequence == 33);
    CHECK(SV_LocalActionShadowAuthorityConsumeNextReceipt(3, &copied));

    receipt = make_receipt(9, 1);
    CHECK(!import->PublishReceipt(MAX_CLIENTS, &receipt));
    CHECK(!SV_LocalActionShadowAuthorityPeekNextReceipt(MAX_CLIENTS, &copied));
    CHECK(!SV_LocalActionShadowAuthorityConsumeNextReceipt(MAX_CLIENTS,
                                                            &receipt));
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(!SV_LocalActionShadowAuthorityTakeFailure(MAX_CLIENTS, &failure));
    CHECK(import->PublishReceipt(1, &receipt));
    copied = receipt;
    copied.record_hash ^= UINT64_C(1);
    CHECK(!SV_LocalActionShadowAuthorityConsumeNextReceipt(1, &copied));
    CHECK(SV_LocalActionShadowAuthorityPeekNextReceipt(1, &copied));
    SV_LocalActionShadowAuthorityResetClient(1);
    CHECK(!SV_LocalActionShadowAuthorityPeekNextReceipt(1, &copied));
    CHECK(import->PublishReceipt(1, &receipt));
    copied = receipt;
    copied.record_hash ^= UINT64_C(1);
    CHECK(!import->PublishReceipt(1, &copied));
    SV_LocalActionShadowAuthorityResetMap();
    CHECK(!SV_LocalActionShadowAuthorityPeekNextReceipt(1, &copied));
    failure = SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;
    CHECK(!SV_LocalActionShadowAuthorityTakeFailure(1, &failure));
    printf("local_action_shadow_authority capacity=32 fifo=exact "
           "command_id=monotonic idempotent=retained-frontier "
           "epoch_change=reset-required failure=latched\n");
    return 0;
}
