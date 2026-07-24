/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "shared/command_abi.h"
#include "shared/local_action_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FR-10-T08 pure predictable-weapon replay engine.
 *
 * A pointer-free state machine that owns one worr_local_action_state_v2 and
 * advances it one canonical command at a time through the deterministic v2
 * convergence core, using rules from the ratified rule provider.  The same
 * engine serves the client predictor (producer_role PREDICTED) and, seeded
 * from observed authoritative weapon state, the server producer
 * (producer_role AUTHORITATIVE); because producer role is excluded from every
 * semantic hash, a matching command yields identical transaction hashes on
 * both sides.  Reconciliation always resolves in authority's favour, so an
 * imperfect rule costs correction frequency, never correctness.
 *
 * This engine opens no sockets, reads no live game state, and presents
 * nothing.  A caller seeds it from an authoritative weapon state, replays the
 * unacknowledged commands, and reconciles each authoritative transaction.
 */

#define WORR_LOCAL_ACTION_PREDICTOR_ABI_VERSION 1u

enum {
  WORR_LOCAL_ACTION_PREDICTOR_ACTIVE = 1u << 0,
};

typedef struct worr_local_action_predictor_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t flags;
  uint32_t catalog_id;
  worr_local_action_state_v2 state;
  uint64_t replay_count;
  uint64_t correction_count;
  uint64_t resync_count;
  uint64_t reserved0;
} worr_local_action_predictor_v1;

/*
 * Seeds the predictor from an authoritative weapon state.  Fails (leaving the
 * predictor inactive and zeroed) unless the weapon is client-predictable per
 * Worr_LocalActionRuleProviderPredictableV1 and the seed state is valid.  A
 * catalog_id of zero is a valid "no predictable weapon" seed that deactivates
 * prediction without failing.  command_epoch must be non-zero.
 */
bool Worr_LocalActionPredictorSeedV1(worr_local_action_predictor_v1 *predictor,
                                     uint32_t command_epoch,
                                     uint32_t catalog_id,
                                     uint32_t active_ammo_units,
                                     uint32_t presentation_frame);

/*
 * Replays one canonical command, advancing the owned state through the v2 core
 * with the provider's rule for the current weapon.  On success the predictor's
 * state becomes the transaction's state_after and, if transaction_out is
 * non-null and entirely zero, the full PREDICTED transaction is written there.
 * Any core rejection (unpredictable weapon, non-cursor-next or non-time-exact
 * command, counter exhaustion) fails closed: the predictor is deactivated and
 * false is returned so the caller can hard-resync.  Returns false with the
 * predictor left inactive when the predictor is not active.
 */
bool Worr_LocalActionPredictorReplayV1(
    worr_local_action_predictor_v1 *predictor,
    const worr_command_record_v1 *command, bool attack_held,
    bool switch_requested, uint32_t requested_weapon_id,
    uint32_t requested_ammo_units,
    worr_local_action_transaction_v2 *transaction_out);

/*
 * Reconciles a predicted transaction against the authoritative transaction for
 * the same command and applies authority.  Classifies the difference; on any
 * class above PRESENTATION_ONLY the owned state is snapped to the
 * authoritative state_after and correction_count is incremented.  correction_out
 * (if non-null and zero) receives the classification.  Returns false and
 * deactivates on a hard resync or invalid input so the caller reseeds.
 */
bool Worr_LocalActionPredictorReconcileV1(
    worr_local_action_predictor_v1 *predictor,
    const worr_local_action_transaction_v2 *predicted,
    const worr_local_action_transaction_v2 *authoritative,
    worr_local_action_correction_v2 *correction_out);

/*
 * Stateless authoritative/predicted transaction builder shared by the client
 * predictor replay and the server producer.  Given an immutable state_before
 * and a command, it resolves the active and pending weapon rules from the
 * ratified provider, adapts the intent, and builds the transaction with the
 * caller's producer_role (PREDICTED or AUTHORITATIVE).  The server producer
 * calls this with the weapon state it observed for the command; the client
 * predictor replay is exactly this with role PREDICTED plus state ownership.
 * Because producer role is excluded from all semantic hashes, the two sides
 * produce identical hashes when their state, command, and rules agree.
 * transaction_out must be entirely zero on entry; every failure (non-
 * predictable weapon, invalid intent, core rejection) leaves it byte-identical.
 */
bool Worr_LocalActionPredictorProduceV1(
    const worr_local_action_state_v2 *state_before,
    const worr_command_record_v1 *command, bool attack_held,
    bool switch_requested, uint32_t requested_weapon_id,
    uint32_t requested_ammo_units, uint32_t producer_role,
    worr_local_action_transaction_v2 *transaction_out);

/* Copies the current owned state.  Fails if the predictor is not active. */
bool Worr_LocalActionPredictorGetStateV1(
    const worr_local_action_predictor_v1 *predictor,
    worr_local_action_state_v2 *state_out);

/*
 * Compact pointer-free seed record: the transport payload that carries the
 * server's exact authoritative weapon state to the client so the client can
 * seed its predictor at a known command boundary.  It rides the native
 * side-channel (a versioned record, not the frozen snapshot ABI or a
 * legacy-compat-breaking stat), so it is default-off and never alters the
 * legacy wire.  The state's applied_cursor names the boundary; the client
 * replays commands after it.
 */
#define WORR_LOCAL_ACTION_PREDICT_SEED_ABI_VERSION 1u

typedef struct worr_local_action_predict_seed_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t catalog_id;
  uint32_t reserved0;
  worr_local_action_state_v2 state;
} worr_local_action_predict_seed_v1;

/*
 * Builds a seed from an authoritative weapon state.  Fails (and zeroes
 * seed_out) unless the state is valid and its weapon is client-predictable.
 */
bool Worr_LocalActionPredictSeedBuildV1(
    const worr_local_action_state_v2 *state,
    worr_local_action_predict_seed_v1 *seed_out);

bool Worr_LocalActionPredictSeedValidateV1(
    const worr_local_action_predict_seed_v1 *seed);

/*
 * Applies a received seed to a predictor, activating it at the seed's exact
 * state.  Fails (leaving the predictor inactive/zeroed) on an invalid seed.
 */
bool Worr_LocalActionPredictSeedApplyV1(
    const worr_local_action_predict_seed_v1 *seed,
    worr_local_action_predictor_v1 *predictor_out);

#ifdef __cplusplus
}
#endif
