/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/command_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_LOCAL_INTERACTION_ABI_VERSION 1u
#define WORR_LOCAL_INTERACTION_MODEL_REVISION 1u
#define WORR_LOCAL_INTERACTION_AUTHORITY_IMPORT_V1 \
    "WORR_LOCAL_INTERACTION_AUTHORITY_IMPORT_V1"
#define WORR_LOCAL_INTERACTION_AUTHORITY_API_VERSION 1u

/* The immutable command ABI encodes native off-hand Hook intent in bit 5. */
enum {
    WORR_LOCAL_INTERACTION_HOOK_BUTTON = UINT32_C(1) << 5,
};

typedef enum worr_local_interaction_producer_v1_e {
    WORR_LOCAL_INTERACTION_PRODUCER_PREDICTED = 1,
    WORR_LOCAL_INTERACTION_PRODUCER_AUTHORITATIVE = 2,
} worr_local_interaction_producer_v1;

enum {
    WORR_LOCAL_INTERACTION_STATE_HOOK_HELD = UINT32_C(1) << 0,
    WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE = UINT32_C(1) << 1,
    WORR_LOCAL_INTERACTION_STATE_HOOK_REQUEST_PENDING = UINT32_C(1) << 2,
};

enum {
    WORR_LOCAL_INTERACTION_INTENT_HOOK_HELD = UINT32_C(1) << 0,
};

enum {
    WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED = UINT32_C(1) << 0,
    WORR_LOCAL_INTERACTION_OUTCOME_HOOK_ACTIVE = UINT32_C(1) << 1,
    WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REJECTED = UINT32_C(1) << 2,
    WORR_LOCAL_INTERACTION_OUTCOME_HOOK_PERSISTED = UINT32_C(1) << 3,
};

/*
 * This is intentionally independent from selected-weapon state.  A client
 * may predict that a mapped off-hand input requested an interaction, but only
 * authority may declare the hook active.  The state is copied by value across
 * the future cgame/sgame shadow boundary.
 */
typedef struct worr_local_interaction_state_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t flags;
    worr_command_cursor_v1 applied_cursor;
    uint64_t sample_time_us;
    uint32_t action_sequence;
    uint32_t reserved0;
} worr_local_interaction_state_v1;

typedef struct worr_local_interaction_intent_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t flags;
    uint32_t reserved0;
} worr_local_interaction_intent_v1;

typedef struct worr_local_interaction_transaction_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t producer;
    worr_local_interaction_state_v1 state_before;
    worr_command_record_v1 command;
    worr_local_interaction_intent_v1 intent;
    worr_local_interaction_state_v1 state_after;
    uint32_t outcome_flags;
    uint32_t reserved0;
    uint64_t state_hash;
    uint64_t transaction_hash;
} worr_local_interaction_transaction_v1;

/*
 * Compact, transport-safe outcome for one authoritative Hook request edge.
 * It deliberately carries no collision, attachment, target, damage, audio,
 * or visual data.  The canonical command hash binds the receipt to the exact
 * command cgame retained locally; the state and transaction hashes make
 * conflicting retransmissions detectable without recreating server state.
 *
 * Only request edges are eligible: outcome_flags always contains REQUESTED
 * and exactly one of ACTIVE or REJECTED.  Continued held/release samples are
 * deliberately omitted so a reliable authority carrier cannot be saturated
 * by per-command state sampling.
 */
typedef struct worr_local_interaction_authority_receipt_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_command_id_v1 command_id;
    uint64_t command_hash;
    uint64_t state_hash;
    uint64_t transaction_hash;
    uint32_t action_sequence;
    uint32_t state_flags;
    uint32_t outcome_flags;
    uint32_t reserved0;
} worr_local_interaction_authority_receipt_v1;

/* Optional engine import used by sgame to publish an immutable receipt to the
 * owning server connection. The callback is process-local; it never serializes
 * data and never identifies a recipient beyond the authenticated client slot.
 */
typedef struct worr_local_interaction_authority_import_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    bool (*PublishReceipt)(
        uint32_t client_index,
        const worr_local_interaction_authority_receipt_v1 *receipt);
} worr_local_interaction_authority_import_v1;

typedef enum worr_local_interaction_correction_v1_e {
    WORR_LOCAL_INTERACTION_CORRECTION_EXACT = 0,
    WORR_LOCAL_INTERACTION_CORRECTION_HOOK_CONFIRMED = 1,
    WORR_LOCAL_INTERACTION_CORRECTION_HOOK_REJECTED = 2,
    WORR_LOCAL_INTERACTION_CORRECTION_DIVERGED = 3,
    WORR_LOCAL_INTERACTION_CORRECTION_INVALID = 4,
} worr_local_interaction_correction_v1;

bool Worr_LocalInteractionStateInitV1(
    worr_local_interaction_state_v1 *state, uint32_t command_epoch);
bool Worr_LocalInteractionStateValidateV1(
    const worr_local_interaction_state_v1 *state);

/*
 * Reconstructs the authoritative state immediately before an exact canonical
 * command. This is for lifecycle observation rebases (bootstrap, reset, or
 * an explicitly detected scope discontinuity), not prediction. `state_out`
 * must be zero and is untouched on failure.
 */
bool Worr_LocalInteractionRebaseBeforeCommandV1(
    const worr_command_record_v1 *command, bool hook_held_before,
    bool hook_active_before, worr_local_interaction_state_v1 *state_out);

bool Worr_LocalInteractionIntentValidateV1(
    const worr_local_interaction_intent_v1 *intent);

/*
 * Builds a cgame-side request transaction.  It predicts only the rising-edge
 * request; it never predicts an active hook or presentation outcome.
 */
bool Worr_LocalInteractionBuildPredictedHookV1(
    const worr_local_interaction_state_v1 *state_before,
    const worr_command_record_v1 *command,
    const worr_local_interaction_intent_v1 *intent,
    worr_local_interaction_transaction_v1 *transaction_out);

/*
 * Builds the matching authority transaction.  hook_active_after is a
 * server-observed lifecycle fact, not a client collision or damage result.
 */
bool Worr_LocalInteractionBuildAuthoritativeHookV1(
    const worr_local_interaction_state_v1 *state_before,
    const worr_command_record_v1 *command,
    const worr_local_interaction_intent_v1 *intent,
    bool hook_active_after,
    worr_local_interaction_transaction_v1 *transaction_out);
bool Worr_LocalInteractionTransactionValidateV1(
    const worr_local_interaction_transaction_v1 *transaction);
uint64_t Worr_LocalInteractionStateHashV1(
    const worr_local_interaction_state_v1 *state);

/* Builds an immutable sparse receipt from an authoritative request edge.
 * `receipt_out` must be zero and is left untouched on failure. */
bool Worr_LocalInteractionAuthorityReceiptBuildV1(
    const worr_local_interaction_transaction_v1 *authoritative,
    worr_local_interaction_authority_receipt_v1 *receipt_out);
bool Worr_LocalInteractionAuthorityReceiptValidateV1(
    const worr_local_interaction_authority_receipt_v1 *receipt);

worr_local_interaction_correction_v1 Worr_LocalInteractionClassifyV1(
    const worr_local_interaction_transaction_v1 *predicted,
    const worr_local_interaction_transaction_v1 *authoritative);
/* Classifies a locally retained prediction against a compact authoritative
 * receipt.  It is intentionally invalid unless both objects name and hash the
 * same exact canonical command. */
worr_local_interaction_correction_v1
Worr_LocalInteractionClassifyReceiptV1(
    const worr_local_interaction_transaction_v1 *predicted,
    const worr_local_interaction_authority_receipt_v1 *receipt);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LOCAL_INTERACTION_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_LOCAL_INTERACTION_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_LOCAL_INTERACTION_STATIC_ASSERT(
    sizeof(worr_local_interaction_state_v1) == 40,
    "local interaction state v1 layout changed");
WORR_LOCAL_INTERACTION_STATIC_ASSERT(
    sizeof(worr_local_interaction_intent_v1) == 16,
    "local interaction intent v1 layout changed");
WORR_LOCAL_INTERACTION_STATIC_ASSERT(
    offsetof(worr_local_interaction_transaction_v1, command) == 56,
    "local interaction transaction command offset changed");
WORR_LOCAL_INTERACTION_STATIC_ASSERT(
    offsetof(worr_local_interaction_transaction_v1, state_after) == 176,
    "local interaction transaction state-after offset changed");
WORR_LOCAL_INTERACTION_STATIC_ASSERT(
    sizeof(worr_local_interaction_authority_receipt_v1) == 56,
    "local interaction authority receipt v1 layout changed");
WORR_LOCAL_INTERACTION_STATIC_ASSERT(
    offsetof(worr_local_interaction_authority_receipt_v1, command_hash) == 16,
    "local interaction authority receipt command hash offset changed");
WORR_LOCAL_INTERACTION_STATIC_ASSERT(
    sizeof(worr_local_interaction_authority_import_v1) ==
        2 * sizeof(uint32_t) + sizeof(void *),
    "local interaction authority import v1 layout changed");

#undef WORR_LOCAL_INTERACTION_STATIC_ASSERT
