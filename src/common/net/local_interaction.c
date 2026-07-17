/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/local_interaction_abi.h"

#include <string.h>

#define LOCAL_INTERACTION_STATE_FLAGS                                      \
    ((uint32_t)(WORR_LOCAL_INTERACTION_STATE_HOOK_HELD |                  \
                WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE |                \
                WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING))
#define LOCAL_INTERACTION_INTENT_FLAGS                                     \
    ((uint32_t)WORR_LOCAL_INTERACTION_INTENT_HOOK_HELD)
#define LOCAL_INTERACTION_OUTCOME_FLAGS                                    \
    ((uint32_t)(WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED |           \
                WORR_LOCAL_INTERACTION_OUTCOME_HOOK_ACTIVE |              \
                WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REJECTED |            \
                WORR_LOCAL_INTERACTION_OUTCOME_HOOK_PERSISTED))

static bool bytes_are_zero(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;
    if (!data)
        return false;
    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

static uint64_t hash_begin(void)
{
    return UINT64_C(1469598103934665603);
}

static uint64_t hash_byte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * UINT64_C(1099511628211);
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    unsigned index;
    for (index = 0; index != 4; ++index) {
        hash = hash_byte(hash, (uint8_t)(value & UINT32_C(0xff)));
        value >>= 8;
    }
    return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned index;
    for (index = 0; index != 8; ++index) {
        hash = hash_byte(hash, (uint8_t)(value & UINT64_C(0xff)));
        value >>= 8;
    }
    return hash;
}

static bool producer_valid(uint32_t producer)
{
    return producer == WORR_LOCAL_INTERACTION_PRODUCER_PREDICTED ||
           producer == WORR_LOCAL_INTERACTION_PRODUCER_AUTHORITATIVE;
}

static bool cursor_next_matches(const worr_local_interaction_state_v1 *state,
                                const worr_command_record_v1 *command)
{
    worr_command_id_v1 next;
    return Worr_CommandCursorNextIdV1(state->applied_cursor, &next) &&
           next.epoch == command->command_id.epoch &&
           next.sequence == command->command_id.sequence;
}

bool Worr_LocalInteractionStateInitV1(
    worr_local_interaction_state_v1 *state, uint32_t command_epoch)
{
    worr_local_interaction_state_v1 candidate;
    if (!state || !bytes_are_zero(state, sizeof(*state)) || command_epoch == 0)
        return false;
    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
    candidate.model_revision = WORR_LOCAL_INTERACTION_MODEL_REVISION;
    candidate.applied_cursor.epoch = command_epoch;
    if (!Worr_LocalInteractionStateValidateV1(&candidate))
        return false;
    *state = candidate;
    return true;
}

bool Worr_LocalInteractionStateValidateV1(
    const worr_local_interaction_state_v1 *state)
{
    if (!state || state->struct_size != sizeof(*state) ||
        state->schema_version != WORR_LOCAL_INTERACTION_ABI_VERSION ||
        state->model_revision != WORR_LOCAL_INTERACTION_MODEL_REVISION ||
        state->reserved0 != 0 ||
        (state->flags & ~LOCAL_INTERACTION_STATE_FLAGS) != 0 ||
        !Worr_CommandCursorValidV1(state->applied_cursor)) {
        return false;
    }
    if ((state->flags & WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) &&
        (state->flags & WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING)) {
        return false;
    }
    return state->applied_cursor.contiguous_sequence == 0
               ? state->sample_time_us == 0 && state->action_sequence == 0
               : state->sample_time_us != 0;
}

bool Worr_LocalInteractionRebaseBeforeCommandV1(
    const worr_command_record_v1 *command, bool hook_held_before,
    bool hook_active_before, worr_local_interaction_state_v1 *state_out)
{
    worr_local_interaction_state_v1 candidate;
    const uint64_t duration_us =
        command ? (uint64_t)command->command.duration_ms * UINT64_C(1000)
                : 0;

    if (!command || !state_out ||
        !bytes_are_zero(state_out, sizeof(*state_out)) ||
        !Worr_CommandRecordValidateV1(
            command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ||
        command->sample_time_us < duration_us) {
        return false;
    }

    memset(&candidate, 0, sizeof(candidate));
    if (!Worr_LocalInteractionStateInitV1(
            &candidate, command->command_id.epoch)) {
        return false;
    }

    candidate.applied_cursor.contiguous_sequence =
        command->command_id.sequence - 1u;
    candidate.sample_time_us = command->sample_time_us - duration_us;
    if (hook_held_before)
        candidate.flags |= WORR_LOCAL_INTERACTION_STATE_HOOK_HELD;
    if (hook_active_before)
        candidate.flags |= WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE;
    if (!Worr_LocalInteractionStateValidateV1(&candidate))
        return false;

    *state_out = candidate;
    return true;
}

bool Worr_LocalInteractionIntentValidateV1(
    const worr_local_interaction_intent_v1 *intent)
{
    return intent && intent->struct_size == sizeof(*intent) &&
           intent->schema_version == WORR_LOCAL_INTERACTION_ABI_VERSION &&
           intent->reserved0 == 0 &&
           (intent->flags & ~LOCAL_INTERACTION_INTENT_FLAGS) == 0;
}

uint64_t Worr_LocalInteractionStateHashV1(
    const worr_local_interaction_state_v1 *state)
{
    uint64_t hash;
    if (!Worr_LocalInteractionStateValidateV1(state))
        return 0;
    hash = hash_begin();
    hash = hash_u32(hash, UINT32_C(0x4c495331)); /* LIS1 */
    hash = hash_u32(hash, state->flags);
    hash = hash_u32(hash, state->applied_cursor.epoch);
    hash = hash_u32(hash, state->applied_cursor.contiguous_sequence);
    hash = hash_u64(hash, state->sample_time_us);
    return hash_u32(hash, state->action_sequence);
}

static bool build_transaction(
    const worr_local_interaction_state_v1 *state_before,
    const worr_command_record_v1 *command,
    const worr_local_interaction_intent_v1 *intent, uint32_t producer,
    bool hook_active_after, worr_local_interaction_transaction_v1 *out)
{
    worr_local_interaction_transaction_v1 candidate;
    const bool held =
        (intent->flags & WORR_LOCAL_INTERACTION_INTENT_HOOK_HELD) != 0;
    const bool held_before =
        (state_before->flags & WORR_LOCAL_INTERACTION_STATE_HOOK_HELD) != 0;
    const bool active_before =
        (state_before->flags & WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) != 0;
    const bool pending_before =
        (state_before->flags &
         WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING) != 0;
    const bool request =
        held && !held_before && !active_before && !pending_before;
    uint64_t command_hash;
    uint64_t hash;

    if (!state_before || !command || !intent || !out ||
        !bytes_are_zero(out, sizeof(*out)) || !producer_valid(producer) ||
        !Worr_LocalInteractionStateValidateV1(state_before) ||
        !Worr_CommandRecordValidateV1(
            command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ||
        !Worr_LocalInteractionIntentValidateV1(intent) ||
        !cursor_next_matches(state_before, command) ||
        (request && state_before->action_sequence == UINT32_MAX)) {
        return false;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
    candidate.model_revision = WORR_LOCAL_INTERACTION_MODEL_REVISION;
    candidate.producer = producer;
    candidate.state_before = *state_before;
    candidate.command = *command;
    candidate.intent = *intent;
    candidate.state_after = *state_before;
    candidate.state_after.applied_cursor.epoch = command->command_id.epoch;
    candidate.state_after.applied_cursor.contiguous_sequence =
        command->command_id.sequence;
    candidate.state_after.sample_time_us = command->sample_time_us;
    candidate.state_after.flags &= ~(uint32_t)(
        WORR_LOCAL_INTERACTION_STATE_HOOK_HELD |
        WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE |
        WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING);
    if (held)
        candidate.state_after.flags |= WORR_LOCAL_INTERACTION_STATE_HOOK_HELD;
    if (request) {
        ++candidate.state_after.action_sequence;
        candidate.outcome_flags |=
            WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED;
    }

    if (producer == WORR_LOCAL_INTERACTION_PRODUCER_PREDICTED) {
        /*
         * A request remains local-pending until an authoritative transaction
         * for that command reconciles it.  Replaying a subsequent held or
         * release command may never silently clear a request the client has
         * not yet had authority to observe.
         */
        if (request || pending_before)
            candidate.state_after.flags |=
                WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING;
        if (active_before) {
            candidate.state_after.flags |=
                WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE;
            candidate.outcome_flags |=
                WORR_LOCAL_INTERACTION_OUTCOME_HOOK_ACTIVE |
                WORR_LOCAL_INTERACTION_OUTCOME_HOOK_PERSISTED;
        }
    } else if (hook_active_after) {
        candidate.state_after.flags |= WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE;
        candidate.outcome_flags |= WORR_LOCAL_INTERACTION_OUTCOME_HOOK_ACTIVE;
        if (active_before && !request)
            candidate.outcome_flags |=
                WORR_LOCAL_INTERACTION_OUTCOME_HOOK_PERSISTED;
    } else if (request) {
        candidate.outcome_flags |=
            WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REJECTED;
    }

    if (!Worr_LocalInteractionStateValidateV1(&candidate.state_after) ||
        !Worr_CommandRecordSemanticHashV1(
            command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, &command_hash)) {
        return false;
    }
    candidate.state_hash = Worr_LocalInteractionStateHashV1(&candidate.state_after);
    hash = hash_begin();
    hash = hash_u32(hash, UINT32_C(0x4c495431)); /* LIT1 */
    hash = hash_u32(hash, producer);
    hash = hash_u64(hash, Worr_LocalInteractionStateHashV1(state_before));
    hash = hash_u64(hash, command_hash);
    hash = hash_u32(hash, intent->flags);
    hash = hash_u64(hash, candidate.state_hash);
    hash = hash_u32(hash, candidate.outcome_flags);
    candidate.transaction_hash = hash;
    *out = candidate;
    return true;
}

bool Worr_LocalInteractionBuildPredictedHookV1(
    const worr_local_interaction_state_v1 *state_before,
    const worr_command_record_v1 *command,
    const worr_local_interaction_intent_v1 *intent,
    worr_local_interaction_transaction_v1 *transaction_out)
{
    return build_transaction(state_before, command, intent,
                             WORR_LOCAL_INTERACTION_PRODUCER_PREDICTED, false,
                             transaction_out);
}

bool Worr_LocalInteractionBuildAuthoritativeHookV1(
    const worr_local_interaction_state_v1 *state_before,
    const worr_command_record_v1 *command,
    const worr_local_interaction_intent_v1 *intent, bool hook_active_after,
    worr_local_interaction_transaction_v1 *transaction_out)
{
    return build_transaction(state_before, command, intent,
                             WORR_LOCAL_INTERACTION_PRODUCER_AUTHORITATIVE,
                             hook_active_after, transaction_out);
}

bool Worr_LocalInteractionTransactionValidateV1(
    const worr_local_interaction_transaction_v1 *transaction)
{
    worr_local_interaction_transaction_v1 expected;
    bool active_after;
    if (!transaction || transaction->struct_size != sizeof(*transaction) ||
        transaction->schema_version != WORR_LOCAL_INTERACTION_ABI_VERSION ||
        transaction->model_revision != WORR_LOCAL_INTERACTION_MODEL_REVISION ||
        transaction->reserved0 != 0 ||
        (transaction->outcome_flags & ~LOCAL_INTERACTION_OUTCOME_FLAGS) != 0 ||
        !Worr_LocalInteractionStateValidateV1(&transaction->state_before) ||
        !Worr_LocalInteractionStateValidateV1(&transaction->state_after) ||
        !Worr_LocalInteractionIntentValidateV1(&transaction->intent) ||
        !Worr_CommandRecordValidateV1(
            &transaction->command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ||
        !producer_valid(transaction->producer) ||
        !cursor_next_matches(&transaction->state_before,
                             &transaction->command)) {
        return false;
    }
    active_after = (transaction->state_after.flags &
                    WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) != 0;
    memset(&expected, 0, sizeof(expected));
    if (transaction->producer == WORR_LOCAL_INTERACTION_PRODUCER_PREDICTED) {
        if (!Worr_LocalInteractionBuildPredictedHookV1(
                &transaction->state_before, &transaction->command,
                &transaction->intent, &expected)) {
            return false;
        }
    } else if (!Worr_LocalInteractionBuildAuthoritativeHookV1(
                   &transaction->state_before, &transaction->command,
                   &transaction->intent, active_after, &expected)) {
        return false;
    }
    return memcmp(transaction, &expected, sizeof(expected)) == 0;
}

bool Worr_LocalInteractionAuthorityReceiptValidateV1(
    const worr_local_interaction_authority_receipt_v1 *receipt)
{
    const bool outcome_active =
        receipt && (receipt->outcome_flags &
                    WORR_LOCAL_INTERACTION_OUTCOME_HOOK_ACTIVE) != 0;
    const bool outcome_rejected =
        receipt && (receipt->outcome_flags &
                    WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REJECTED) != 0;
    const bool state_active =
        receipt && (receipt->state_flags &
                    WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) != 0;

    if (!receipt || receipt->struct_size != sizeof(*receipt) ||
        receipt->schema_version != WORR_LOCAL_INTERACTION_ABI_VERSION ||
        receipt->command_id.epoch == 0 || receipt->command_id.sequence == 0 ||
        receipt->command_hash == 0 || receipt->state_hash == 0 ||
        receipt->transaction_hash == 0 || receipt->action_sequence == 0 ||
        receipt->reserved0 != 0 ||
        (receipt->state_flags & ~LOCAL_INTERACTION_STATE_FLAGS) != 0 ||
        (receipt->outcome_flags & ~LOCAL_INTERACTION_OUTCOME_FLAGS) != 0 ||
        (receipt->state_flags &
         WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING) != 0 ||
        (receipt->state_flags & WORR_LOCAL_INTERACTION_STATE_HOOK_HELD) == 0 ||
        (receipt->outcome_flags &
         WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED) == 0 ||
        outcome_active == outcome_rejected || state_active != outcome_active) {
        return false;
    }
    return (receipt->outcome_flags &
            WORR_LOCAL_INTERACTION_OUTCOME_HOOK_PERSISTED) == 0;
}

bool Worr_LocalInteractionAuthorityReceiptBuildV1(
    const worr_local_interaction_transaction_v1 *authoritative,
    worr_local_interaction_authority_receipt_v1 *receipt_out)
{
    worr_local_interaction_authority_receipt_v1 candidate;
    uint64_t command_hash;
    const bool active =
        authoritative && (authoritative->outcome_flags &
                          WORR_LOCAL_INTERACTION_OUTCOME_HOOK_ACTIVE) != 0;
    const bool rejected =
        authoritative && (authoritative->outcome_flags &
                          WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REJECTED) != 0;

    if (!authoritative || !receipt_out ||
        !bytes_are_zero(receipt_out, sizeof(*receipt_out)) ||
        !Worr_LocalInteractionTransactionValidateV1(authoritative) ||
        authoritative->producer !=
            WORR_LOCAL_INTERACTION_PRODUCER_AUTHORITATIVE ||
        (authoritative->outcome_flags &
         WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED) == 0 ||
        active == rejected ||
        (authoritative->state_after.flags &
         WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING) != 0 ||
        !Worr_CommandRecordSemanticHashV1(
            &authoritative->command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            &command_hash)) {
        return false;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
    candidate.command_id = authoritative->command.command_id;
    candidate.command_hash = command_hash;
    candidate.state_hash = authoritative->state_hash;
    candidate.transaction_hash = authoritative->transaction_hash;
    candidate.action_sequence = authoritative->state_after.action_sequence;
    candidate.state_flags = authoritative->state_after.flags;
    candidate.outcome_flags = authoritative->outcome_flags;
    if (!Worr_LocalInteractionAuthorityReceiptValidateV1(&candidate))
        return false;

    *receipt_out = candidate;
    return true;
}

worr_local_interaction_correction_v1 Worr_LocalInteractionClassifyV1(
    const worr_local_interaction_transaction_v1 *predicted,
    const worr_local_interaction_transaction_v1 *authoritative)
{
    const bool predicted_pending =
        predicted && (predicted->state_after.flags &
                      WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING) != 0;
    const bool authoritative_active =
        authoritative && (authoritative->state_after.flags &
                          WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) != 0;
    const bool authoritative_rejected =
        authoritative && (authoritative->outcome_flags &
                          WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REJECTED) != 0;

    if (!Worr_LocalInteractionTransactionValidateV1(predicted) ||
        !Worr_LocalInteractionTransactionValidateV1(authoritative) ||
        predicted->producer != WORR_LOCAL_INTERACTION_PRODUCER_PREDICTED ||
        authoritative->producer !=
            WORR_LOCAL_INTERACTION_PRODUCER_AUTHORITATIVE ||
        predicted->command.command_id.epoch !=
            authoritative->command.command_id.epoch ||
        predicted->command.command_id.sequence !=
            authoritative->command.command_id.sequence) {
        return WORR_LOCAL_INTERACTION_CORRECTION_INVALID;
    }
    if (predicted_pending && authoritative_active)
        return WORR_LOCAL_INTERACTION_CORRECTION_HOOK_CONFIRMED;
    if (predicted_pending && authoritative_rejected)
        return WORR_LOCAL_INTERACTION_CORRECTION_HOOK_REJECTED;
    if (predicted->transaction_hash == authoritative->transaction_hash)
        return WORR_LOCAL_INTERACTION_CORRECTION_EXACT;
    return WORR_LOCAL_INTERACTION_CORRECTION_DIVERGED;
}

worr_local_interaction_correction_v1
Worr_LocalInteractionClassifyReceiptV1(
    const worr_local_interaction_transaction_v1 *predicted,
    const worr_local_interaction_authority_receipt_v1 *receipt)
{
    uint64_t command_hash;
    const bool predicted_pending =
        predicted && (predicted->state_after.flags &
                      WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING) != 0;
    const bool authoritative_active =
        receipt && (receipt->state_flags &
                    WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) != 0;
    const bool authoritative_rejected =
        receipt && (receipt->outcome_flags &
                    WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REJECTED) != 0;

    if (!Worr_LocalInteractionTransactionValidateV1(predicted) ||
        !Worr_LocalInteractionAuthorityReceiptValidateV1(receipt) ||
        predicted->producer != WORR_LOCAL_INTERACTION_PRODUCER_PREDICTED ||
        predicted->command.command_id.epoch != receipt->command_id.epoch ||
        predicted->command.command_id.sequence != receipt->command_id.sequence ||
        !Worr_CommandRecordSemanticHashV1(
            &predicted->command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            &command_hash) ||
        command_hash != receipt->command_hash) {
        return WORR_LOCAL_INTERACTION_CORRECTION_INVALID;
    }
    if (predicted->state_after.action_sequence != receipt->action_sequence)
        return WORR_LOCAL_INTERACTION_CORRECTION_DIVERGED;
    if (predicted_pending && authoritative_active)
        return WORR_LOCAL_INTERACTION_CORRECTION_HOOK_CONFIRMED;
    if (predicted_pending && authoritative_rejected)
        return WORR_LOCAL_INTERACTION_CORRECTION_HOOK_REJECTED;
    if (predicted->state_hash == receipt->state_hash)
        return WORR_LOCAL_INTERACTION_CORRECTION_EXACT;
    return WORR_LOCAL_INTERACTION_CORRECTION_DIVERGED;
}
