/* Deterministic contract checks for independent local interaction prediction. */
#include "shared/local_interaction_abi.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,       \
                    __LINE__, #condition);                                 \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static worr_command_record_v1 make_command(uint32_t sequence)
{
    worr_command_record_v1 command;
    memset(&command, 0, sizeof(command));
    command.struct_size = sizeof(command);
    command.schema_version = WORR_COMMAND_ABI_VERSION;
    command.command_id.epoch = 17;
    command.command_id.sequence = sequence;
    command.sample_time_us = (uint64_t)sequence * UINT64_C(16000);
    command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    command.command.struct_size = sizeof(command.command);
    command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.command.duration_ms = 16;
    command.render_watermark.struct_size = sizeof(command.render_watermark);
    command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    return command;
}

static worr_local_interaction_intent_v1 make_intent(bool held)
{
    worr_local_interaction_intent_v1 intent;
    memset(&intent, 0, sizeof(intent));
    intent.struct_size = sizeof(intent);
    intent.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
    if (held)
        intent.flags = WORR_LOCAL_INTERACTION_INTENT_HOOK_HELD;
    return intent;
}

static int test_prediction_confirmation_and_rejection(void)
{
    worr_local_interaction_state_v1 initial;
    worr_local_interaction_transaction_v1 predicted;
    worr_local_interaction_transaction_v1 confirmed;
    worr_local_interaction_transaction_v1 rejected;
    const worr_command_record_v1 command = make_command(1);
    const worr_local_interaction_intent_v1 intent = make_intent(true);

    memset(&initial, 0, sizeof(initial));
    memset(&predicted, 0, sizeof(predicted));
    memset(&confirmed, 0, sizeof(confirmed));
    memset(&rejected, 0, sizeof(rejected));
    CHECK(Worr_LocalInteractionStateInitV1(&initial, 17));
    CHECK(Worr_LocalInteractionBuildPredictedHookV1(
        &initial, &command, &intent, &predicted));
    CHECK(Worr_LocalInteractionTransactionValidateV1(&predicted));
    CHECK((predicted.outcome_flags &
           WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED) != 0);
    CHECK((predicted.state_after.flags &
           WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING) != 0);
    CHECK((predicted.state_after.flags &
           WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) == 0);

    CHECK(Worr_LocalInteractionBuildAuthoritativeHookV1(
        &initial, &command, &intent, true, &confirmed));
    CHECK(Worr_LocalInteractionTransactionValidateV1(&confirmed));
    CHECK(Worr_LocalInteractionClassifyV1(&predicted, &confirmed) ==
          WORR_LOCAL_INTERACTION_CORRECTION_HOOK_CONFIRMED);

    CHECK(Worr_LocalInteractionBuildAuthoritativeHookV1(
        &initial, &command, &intent, false, &rejected));
    CHECK(Worr_LocalInteractionTransactionValidateV1(&rejected));
    CHECK(Worr_LocalInteractionClassifyV1(&predicted, &rejected) ==
          WORR_LOCAL_INTERACTION_CORRECTION_HOOK_REJECTED);
    return 0;
}

static int test_authority_receipt(void)
{
    worr_local_interaction_state_v1 initial;
    worr_local_interaction_transaction_v1 predicted;
    worr_local_interaction_transaction_v1 confirmed;
    worr_local_interaction_transaction_v1 rejected;
    worr_local_interaction_authority_receipt_v1 confirmed_receipt;
    worr_local_interaction_authority_receipt_v1 rejected_receipt;
    worr_local_interaction_authority_receipt_v1 output;
    worr_local_interaction_authority_receipt_v1 before_output;
    const worr_command_record_v1 command = make_command(1);
    const worr_local_interaction_intent_v1 intent = make_intent(true);

    memset(&initial, 0, sizeof(initial));
    memset(&predicted, 0, sizeof(predicted));
    memset(&confirmed, 0, sizeof(confirmed));
    memset(&rejected, 0, sizeof(rejected));
    memset(&confirmed_receipt, 0, sizeof(confirmed_receipt));
    memset(&rejected_receipt, 0, sizeof(rejected_receipt));
    CHECK(Worr_LocalInteractionStateInitV1(&initial, 17));
    CHECK(Worr_LocalInteractionBuildPredictedHookV1(
        &initial, &command, &intent, &predicted));
    CHECK(Worr_LocalInteractionBuildAuthoritativeHookV1(
        &initial, &command, &intent, true, &confirmed));
    CHECK(Worr_LocalInteractionBuildAuthoritativeHookV1(
        &initial, &command, &intent, false, &rejected));
    CHECK(Worr_LocalInteractionAuthorityReceiptBuildV1(
        &confirmed, &confirmed_receipt));
    CHECK(Worr_LocalInteractionAuthorityReceiptBuildV1(
        &rejected, &rejected_receipt));
    CHECK(Worr_LocalInteractionAuthorityReceiptValidateV1(
        &confirmed_receipt));
    CHECK(Worr_LocalInteractionAuthorityReceiptValidateV1(
        &rejected_receipt));
    CHECK(Worr_LocalInteractionClassifyReceiptV1(
              &predicted, &confirmed_receipt) ==
          WORR_LOCAL_INTERACTION_CORRECTION_HOOK_CONFIRMED);
    CHECK(Worr_LocalInteractionClassifyReceiptV1(
              &predicted, &rejected_receipt) ==
          WORR_LOCAL_INTERACTION_CORRECTION_HOOK_REJECTED);

    output = confirmed_receipt;
    output.state_flags |= WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING;
    CHECK(!Worr_LocalInteractionAuthorityReceiptValidateV1(&output));
    output = confirmed_receipt;
    ++output.action_sequence;
    CHECK(Worr_LocalInteractionClassifyReceiptV1(&predicted, &output) ==
          WORR_LOCAL_INTERACTION_CORRECTION_DIVERGED);

    memset(&output, 0x5a, sizeof(output));
    before_output = output;
    CHECK(!Worr_LocalInteractionAuthorityReceiptBuildV1(
        &confirmed, &output));
    CHECK(memcmp(&output, &before_output, sizeof(output)) == 0);
    return 0;
}

static int test_continuation_and_fail_closed(void)
{
    worr_local_interaction_state_v1 state;
    worr_local_interaction_transaction_v1 first;
    worr_local_interaction_transaction_v1 second;
    worr_local_interaction_transaction_v1 output;
    worr_local_interaction_transaction_v1 before_output;
    const worr_command_record_v1 command1 = make_command(1);
    const worr_command_record_v1 command2 = make_command(2);
    const worr_local_interaction_intent_v1 held = make_intent(true);
    const worr_local_interaction_intent_v1 released = make_intent(false);

    memset(&state, 0, sizeof(state));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    CHECK(Worr_LocalInteractionStateInitV1(&state, 17));
    CHECK(Worr_LocalInteractionBuildAuthoritativeHookV1(
        &state, &command1, &held, true, &first));
    CHECK((first.state_after.flags &
           WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) != 0);
    CHECK((first.state_after.flags &
           WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING) == 0);
    CHECK(Worr_LocalInteractionBuildPredictedHookV1(
        &first.state_after, &command2, &held, &second));
    CHECK((second.outcome_flags &
           WORR_LOCAL_INTERACTION_OUTCOME_HOOK_PERSISTED) != 0);
    CHECK((second.outcome_flags &
           WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED) == 0);

    memset(&state, 0, sizeof(state));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    CHECK(Worr_LocalInteractionStateInitV1(&state, 17));
    CHECK(Worr_LocalInteractionBuildPredictedHookV1(
        &state, &command1, &held, &first));
    CHECK((first.state_after.flags &
           WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING) != 0);
    CHECK(Worr_LocalInteractionBuildPredictedHookV1(
        &first.state_after, &command2, &released, &second));
    CHECK((second.state_after.flags &
           WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING) != 0);
    CHECK((second.state_after.flags &
           WORR_LOCAL_INTERACTION_STATE_HOOK_HELD) == 0);

    memset(&state, 0, sizeof(state));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    CHECK(Worr_LocalInteractionStateInitV1(&state, 17));
    CHECK(Worr_LocalInteractionBuildPredictedHookV1(
        &state, &command1, &held, &first));
    CHECK(Worr_LocalInteractionBuildPredictedHookV1(
        &first.state_after, &command2, &held, &second));
    CHECK((second.outcome_flags &
           WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED) == 0);

    memset(&output, 0x5a, sizeof(output));
    before_output = output;
    CHECK(!Worr_LocalInteractionBuildPredictedHookV1(
        &state, &command2, &held, &output));
    CHECK(memcmp(&output, &before_output, sizeof(output)) == 0);

    output = second;
    output.state_after.flags |= UINT32_C(1) << 31;
    CHECK(!Worr_LocalInteractionTransactionValidateV1(&output));
    return 0;
}

static int test_authoritative_rebase(void)
{
    worr_local_interaction_state_v1 before;
    worr_local_interaction_transaction_v1 transaction;
    const worr_command_record_v1 command = make_command(5);
    const worr_local_interaction_intent_v1 held = make_intent(true);

    memset(&before, 0, sizeof(before));
    memset(&transaction, 0, sizeof(transaction));
    CHECK(Worr_LocalInteractionRebaseBeforeCommandV1(
        &command, false, false, &before));
    CHECK(before.applied_cursor.epoch == 17);
    CHECK(before.applied_cursor.contiguous_sequence == 4);
    CHECK(before.sample_time_us == UINT64_C(64000));
    CHECK((before.flags & WORR_LOCAL_INTERACTION_STATE_HOOK_HELD) == 0);
    CHECK(Worr_LocalInteractionBuildAuthoritativeHookV1(
        &before, &command, &held, true, &transaction));
    CHECK((transaction.outcome_flags &
           WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED) != 0);
    CHECK((transaction.outcome_flags &
           WORR_LOCAL_INTERACTION_OUTCOME_HOOK_ACTIVE) != 0);

    memset(&before, 0, sizeof(before));
    CHECK(Worr_LocalInteractionRebaseBeforeCommandV1(
        &command, true, true, &before));
    CHECK((before.flags & WORR_LOCAL_INTERACTION_STATE_HOOK_HELD) != 0);
    CHECK((before.flags & WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) != 0);
    CHECK(before.action_sequence == 0);

    before.struct_size = 0;
    {
        const worr_local_interaction_state_v1 saved = before;
        CHECK(!Worr_LocalInteractionRebaseBeforeCommandV1(
            &command, false, false, &before));
        CHECK(memcmp(&before, &saved, sizeof(before)) == 0);
    }
    return 0;
}

int main(void)
{
    const int first = test_prediction_confirmation_and_rejection();
    if (first)
        return first;
    {
        const int second = test_continuation_and_fail_closed();
        if (second)
            return second;
        {
            const int third = test_authoritative_rebase();
            return third ? third : test_authority_receipt();
        }
    }
}
