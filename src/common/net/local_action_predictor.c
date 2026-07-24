/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/local_action_predictor.h"

#include <string.h>

#include "shared/local_action_rule_provider.h"

static bool bytes_are_zero(const void *data, size_t size) {
  const unsigned char *bytes = (const unsigned char *)data;
  size_t index;
  for (index = 0; index < size; ++index) {
    if (bytes[index] != 0)
      return false;
  }
  return true;
}

static bool predictor_shape_valid(
    const worr_local_action_predictor_v1 *predictor) {
  return predictor != NULL &&
         predictor->struct_size == sizeof(*predictor) &&
         predictor->schema_version ==
             WORR_LOCAL_ACTION_PREDICTOR_ABI_VERSION &&
         predictor->reserved0 == 0;
}

static void deactivate(worr_local_action_predictor_v1 *predictor) {
  predictor->flags &= ~(uint32_t)WORR_LOCAL_ACTION_PREDICTOR_ACTIVE;
  if (predictor->resync_count != UINT64_MAX)
    ++predictor->resync_count;
}

static void make_absent_rule(worr_local_action_weapon_rule_v2 *rule) {
  memset(rule, 0, sizeof(*rule));
  rule->struct_size = sizeof(*rule);
  rule->schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
  rule->model_revision = WORR_LOCAL_ACTION_MODEL_REVISION;
}

bool Worr_LocalActionPredictorSeedV1(worr_local_action_predictor_v1 *predictor,
                                     uint32_t command_epoch,
                                     uint32_t catalog_id,
                                     uint32_t active_ammo_units,
                                     uint32_t presentation_frame) {
  worr_local_action_predictor_v1 next;

  if (predictor == NULL || command_epoch == 0)
    return false;

  memset(&next, 0, sizeof(next));
  next.struct_size = sizeof(next);
  next.schema_version = WORR_LOCAL_ACTION_PREDICTOR_ABI_VERSION;

  /* A zero weapon is a valid "nothing predictable" seed: the predictor stays
   * inactive but the call succeeds so the caller need not special-case it. */
  if (catalog_id == 0) {
    *predictor = next;
    return true;
  }

  if (!Worr_LocalActionRuleProviderPredictableV1(catalog_id)) {
    *predictor = next;
    return false;
  }
  if (!Worr_LocalActionStateInitV2(&next.state, command_epoch, catalog_id,
                                   active_ammo_units, presentation_frame)) {
    *predictor = next;
    return false;
  }
  next.flags = WORR_LOCAL_ACTION_PREDICTOR_ACTIVE;
  next.catalog_id = catalog_id;
  *predictor = next;
  return true;
}

/*
 * Resolves the active/pending rules and intent for one command and builds the
 * transaction with the requested role.  transaction_out must be zero on entry
 * and is left byte-identical on any failure (rule/intent resolution never
 * touches it, and the core build is itself byte-atomic).
 */
static bool build_transaction_for(
    const worr_local_action_state_v2 *state_before,
    const worr_command_record_v1 *command, bool attack_held,
    bool switch_requested, uint32_t requested_weapon_id,
    uint32_t requested_ammo_units, uint32_t producer_role,
    worr_local_action_transaction_v2 *transaction_out) {
  worr_local_action_weapon_rule_v2 active;
  worr_local_action_weapon_rule_v2 pending;
  worr_local_action_intent_v2 intent;
  uint32_t pending_target;

  if (state_before == NULL || command == NULL)
    return false;

  memset(&active, 0, sizeof(active));
  if (!Worr_LocalActionRuleProviderBuildRuleV1(state_before->active_weapon_id,
                                               &active)) {
    return false;
  }

  memset(&pending, 0, sizeof(pending));
  pending_target = switch_requested ? requested_weapon_id
                                    : state_before->pending_weapon_id;
  if (pending_target != 0) {
    /* Switching to a non-predictable weapon cannot be represented. */
    if (!Worr_LocalActionRuleProviderBuildRuleV1(pending_target, &pending))
      return false;
  } else {
    make_absent_rule(&pending);
  }

  memset(&intent, 0, sizeof(intent));
  if (!Worr_LocalActionRuleProviderBuildIntentV1(
          attack_held, switch_requested, requested_weapon_id,
          requested_ammo_units, &intent)) {
    return false;
  }

  return Worr_LocalActionBuildTransactionV2(state_before, command, &intent,
                                            &active, &pending, producer_role,
                                            transaction_out);
}

bool Worr_LocalActionPredictorReplayV1(
    worr_local_action_predictor_v1 *predictor,
    const worr_command_record_v1 *command, bool attack_held,
    bool switch_requested, uint32_t requested_weapon_id,
    uint32_t requested_ammo_units,
    worr_local_action_transaction_v2 *transaction_out) {
  worr_local_action_transaction_v2 transaction;

  if (!predictor_shape_valid(predictor) || command == NULL)
    return false;
  if ((predictor->flags & WORR_LOCAL_ACTION_PREDICTOR_ACTIVE) == 0)
    return false;

  memset(&transaction, 0, sizeof(transaction));
  if (!build_transaction_for(&predictor->state, command, attack_held,
                             switch_requested, requested_weapon_id,
                             requested_ammo_units,
                             WORR_LOCAL_ACTION_PRODUCER_PREDICTED,
                             &transaction)) {
    deactivate(predictor);
    return false;
  }

  predictor->state = transaction.state_after;
  if (predictor->replay_count != UINT64_MAX)
    ++predictor->replay_count;
  if (transaction_out != NULL &&
      bytes_are_zero(transaction_out, sizeof(*transaction_out))) {
    *transaction_out = transaction;
  }
  return true;
}

bool Worr_LocalActionPredictorProduceV1(
    const worr_local_action_state_v2 *state_before,
    const worr_command_record_v1 *command, bool attack_held,
    bool switch_requested, uint32_t requested_weapon_id,
    uint32_t requested_ammo_units, uint32_t producer_role,
    worr_local_action_transaction_v2 *transaction_out) {
  if (transaction_out == NULL ||
      !bytes_are_zero(transaction_out, sizeof(*transaction_out))) {
    return false;
  }
  if (producer_role != WORR_LOCAL_ACTION_PRODUCER_PREDICTED &&
      producer_role != WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE) {
    return false;
  }
  return build_transaction_for(state_before, command, attack_held,
                               switch_requested, requested_weapon_id,
                               requested_ammo_units, producer_role,
                               transaction_out);
}

bool Worr_LocalActionPredictorReconcileV1(
    worr_local_action_predictor_v1 *predictor,
    const worr_local_action_transaction_v2 *predicted,
    const worr_local_action_transaction_v2 *authoritative,
    worr_local_action_correction_v2 *correction_out) {
  worr_local_action_correction_v2 correction;

  if (!predictor_shape_valid(predictor) || predicted == NULL ||
      authoritative == NULL) {
    return false;
  }
  if ((predictor->flags & WORR_LOCAL_ACTION_PREDICTOR_ACTIVE) == 0)
    return false;

  memset(&correction, 0, sizeof(correction));
  if (!Worr_LocalActionCorrectionClassifyV2(predicted, authoritative,
                                            &correction)) {
    deactivate(predictor);
    return false;
  }

  if (correction.classification == WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC) {
    deactivate(predictor);
    if (correction_out != NULL &&
        bytes_are_zero(correction_out, sizeof(*correction_out))) {
      *correction_out = correction;
    }
    return false;
  }

  /* Authority immediately owns gameplay state; presentation-only differences
   * keep the predicted state and are smoothed by the caller. */
  if (correction.classification ==
      WORR_LOCAL_ACTION_CORRECTION_GAMEPLAY_IMMEDIATE) {
    predictor->state = authoritative->state_after;
    if (predictor->correction_count != UINT64_MAX)
      ++predictor->correction_count;
  }
  if (correction_out != NULL &&
      bytes_are_zero(correction_out, sizeof(*correction_out))) {
    *correction_out = correction;
  }
  return true;
}

bool Worr_LocalActionPredictorGetStateV1(
    const worr_local_action_predictor_v1 *predictor,
    worr_local_action_state_v2 *state_out) {
  if (!predictor_shape_valid(predictor) || state_out == NULL)
    return false;
  if ((predictor->flags & WORR_LOCAL_ACTION_PREDICTOR_ACTIVE) == 0)
    return false;
  *state_out = predictor->state;
  return true;
}

bool Worr_LocalActionPredictSeedBuildV1(
    const worr_local_action_state_v2 *state,
    worr_local_action_predict_seed_v1 *seed_out) {
  if (seed_out == NULL)
    return false;
  memset(seed_out, 0, sizeof(*seed_out));
  if (state == NULL || !Worr_LocalActionStateValidateV2(state) ||
      state->phase != WORR_LOCAL_ACTION_PHASE_READY ||
      !Worr_LocalActionRuleProviderPredictableV1(state->active_weapon_id)) {
    return false;
  }
  seed_out->struct_size = sizeof(*seed_out);
  seed_out->schema_version = WORR_LOCAL_ACTION_PREDICT_SEED_ABI_VERSION;
  seed_out->catalog_id = state->active_weapon_id;
  seed_out->active_ammo_units = state->active_ammo_units;
  seed_out->presentation_frame = state->presentation_frame;
  seed_out->applied_cursor = state->applied_cursor;
  seed_out->sample_time_us = state->sample_time_us;
  return true;
}

bool Worr_LocalActionPredictSeedValidateV1(
    const worr_local_action_predict_seed_v1 *seed) {
  return seed != NULL && seed->struct_size == sizeof(*seed) &&
         seed->schema_version == WORR_LOCAL_ACTION_PREDICT_SEED_ABI_VERSION &&
         seed->reserved0 == 0 &&
         Worr_LocalActionRuleProviderPredictableV1(seed->catalog_id) &&
         Worr_CommandCursorValidV1(seed->applied_cursor);
}

bool Worr_LocalActionPredictSeedApplyV1(
    const worr_local_action_predict_seed_v1 *seed,
    worr_local_action_predictor_v1 *predictor_out) {
  worr_local_action_state_v2 state;

  if (predictor_out == NULL)
    return false;
  memset(predictor_out, 0, sizeof(*predictor_out));
  predictor_out->struct_size = sizeof(*predictor_out);
  predictor_out->schema_version = WORR_LOCAL_ACTION_PREDICTOR_ABI_VERSION;
  if (!Worr_LocalActionPredictSeedValidateV1(seed))
    return false;

  /* Reconstruct the exact READY state, then restore the transported command
   * boundary so the client replays only strictly-later commands. */
  memset(&state, 0, sizeof(state));
  if (!Worr_LocalActionStateInitV2(&state, seed->applied_cursor.epoch,
                                   seed->catalog_id, seed->active_ammo_units,
                                   seed->presentation_frame)) {
    return false;
  }
  state.applied_cursor = seed->applied_cursor;
  state.sample_time_us = seed->sample_time_us;
  if (!Worr_LocalActionStateValidateV2(&state))
    return false;

  predictor_out->flags = WORR_LOCAL_ACTION_PREDICTOR_ACTIVE;
  predictor_out->catalog_id = seed->catalog_id;
  predictor_out->state = state;
  return true;
}
