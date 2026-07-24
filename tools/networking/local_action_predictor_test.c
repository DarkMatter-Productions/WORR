/*
 * FR-10-T08 predictable-weapon replay engine tests: full fire/drain/refire
 * lifecycle plus authority reconciliation (match and hard resync).
 */

#include "common/net/local_action_predictor.h"

#include "shared/command_abi.h"
#include "shared/local_action_abi.h"
#include "shared/local_action_catalog.h"
#include "shared/prediction_abi.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "local_action_predictor_test:%d: %s\n", __LINE__,       \
              #condition);                                                     \
      return false;                                                            \
    }                                                                          \
  } while (0)

static worr_command_record_v1 make_command(
    const worr_local_action_state_v2 *state, uint8_t duration_ms) {
  worr_command_record_v1 command;
  worr_command_id_v1 next_id = {0, 0};

  memset(&command, 0, sizeof(command));
  command.struct_size = sizeof(command);
  command.schema_version = WORR_COMMAND_ABI_VERSION;
  (void)Worr_CommandCursorNextIdV1(state->applied_cursor, &next_id);
  command.command_id = next_id;
  command.sample_time_us =
      state->sample_time_us + (uint64_t)duration_ms * UINT64_C(1000);
  command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  command.command.struct_size = sizeof(command.command);
  command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  command.command.duration_ms = duration_ms;
  command.render_watermark.struct_size = sizeof(command.render_watermark);
  command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  return command;
}

static bool replay(worr_local_action_predictor_v1 *predictor, uint8_t duration,
                   bool attack, worr_local_action_transaction_v2 *out) {
  const worr_command_record_v1 command =
      make_command(&predictor->state, duration);
  if (out != NULL)
    memset(out, 0, sizeof(*out));
  return Worr_LocalActionPredictorReplayV1(predictor, &command, attack, false,
                                           0, 0, out);
}

static bool test_seed_and_inactive(void) {
  worr_local_action_predictor_v1 predictor;
  worr_local_action_state_v2 state;

  /* A zero weapon seeds successfully but inactive. */
  CHECK(Worr_LocalActionPredictorSeedV1(&predictor, 5u, 0, 0, 0));
  CHECK((predictor.flags & WORR_LOCAL_ACTION_PREDICTOR_ACTIVE) == 0);
  CHECK(!Worr_LocalActionPredictorGetStateV1(&predictor, &state));

  /* A non-predictable weapon (Machinegun) fails and stays inactive. */
  CHECK(!Worr_LocalActionPredictorSeedV1(
      &predictor, 5u, WORR_LOCAL_ACTION_CATALOG_MACHINEGUN, 50, 0));
  CHECK((predictor.flags & WORR_LOCAL_ACTION_PREDICTOR_ACTIVE) == 0);

  /* A zero epoch is rejected. */
  CHECK(!Worr_LocalActionPredictorSeedV1(
      &predictor, 0u, WORR_LOCAL_ACTION_CATALOG_SHOTGUN, 5, 0));
  return true;
}

static bool test_fire_drain_refire(void) {
  worr_local_action_predictor_v1 predictor;
  worr_local_action_state_v2 state;
  worr_local_action_transaction_v2 fired;
  uint32_t guard;

  CHECK(Worr_LocalActionPredictorSeedV1(
      &predictor, 5u, WORR_LOCAL_ACTION_CATALOG_SHOTGUN, 5, 0));
  CHECK((predictor.flags & WORR_LOCAL_ACTION_PREDICTOR_ACTIVE) != 0);
  CHECK(Worr_LocalActionPredictorGetStateV1(&predictor, &state));
  CHECK(state.phase == WORR_LOCAL_ACTION_PHASE_READY);
  CHECK(state.active_ammo_units == 5);

  /* First attack rising edge fires once and debits one shell. */
  CHECK(replay(&predictor, 10, true, &fired));
  CHECK(Worr_LocalActionPredictorGetStateV1(&predictor, &state));
  CHECK(state.phase == WORR_LOCAL_ACTION_PHASE_FIRING);
  CHECK(state.active_ammo_units == 4);
  CHECK(state.shot_sequence == 1);

  /* Holding attack while FIRING drains the refire timer without re-firing. */
  for (guard = 0; guard < 64u; ++guard) {
    CHECK(replay(&predictor, 250, true, NULL));
    CHECK(Worr_LocalActionPredictorGetStateV1(&predictor, &state));
    if (state.phase == WORR_LOCAL_ACTION_PHASE_READY)
      break;
  }
  CHECK(state.phase == WORR_LOCAL_ACTION_PHASE_READY);
  CHECK(state.active_ammo_units == 4);
  CHECK(state.shot_sequence == 1);

  /* Releasing then pressing attack fires the next shot. */
  CHECK(replay(&predictor, 10, false, NULL));
  CHECK(replay(&predictor, 10, true, NULL));
  CHECK(Worr_LocalActionPredictorGetStateV1(&predictor, &state));
  CHECK(state.phase == WORR_LOCAL_ACTION_PHASE_FIRING);
  CHECK(state.active_ammo_units == 3);
  CHECK(state.shot_sequence == 2);
  CHECK(predictor.replay_count >= 3);
  return true;
}

static bool drain_to_ready(worr_local_action_predictor_v1 *predictor,
                           worr_local_action_state_v2 *state) {
  uint32_t guard;
  for (guard = 0; guard < 64u; ++guard) {
    CHECK(replay(predictor, 250, true, NULL));
    CHECK(Worr_LocalActionPredictorGetStateV1(predictor, state));
    if (state->phase == WORR_LOCAL_ACTION_PHASE_READY)
      return true;
  }
  return false;
}

static bool test_dry_fire(void) {
  worr_local_action_predictor_v1 predictor;
  worr_local_action_state_v2 state;

  /* One shell: the first press fires it, a later press with no ammo dry-fires
   * -- no shot, no ammo change, and the dry-fire latch is set. */
  CHECK(Worr_LocalActionPredictorSeedV1(
      &predictor, 5u, WORR_LOCAL_ACTION_CATALOG_SHOTGUN, 1, 0));
  CHECK(replay(&predictor, 10, true, NULL));
  CHECK(Worr_LocalActionPredictorGetStateV1(&predictor, &state));
  CHECK(state.active_ammo_units == 0);
  CHECK(state.shot_sequence == 1);
  CHECK(drain_to_ready(&predictor, &state));

  CHECK(replay(&predictor, 10, false, NULL));
  CHECK(replay(&predictor, 10, true, NULL));
  CHECK(Worr_LocalActionPredictorGetStateV1(&predictor, &state));
  CHECK(state.phase == WORR_LOCAL_ACTION_PHASE_READY);
  CHECK(state.active_ammo_units == 0);
  CHECK(state.shot_sequence == 1);
  CHECK((state.flags & WORR_LOCAL_ACTION_STATE_DRY_FIRE_LATCHED) != 0);
  return true;
}

static bool replay_switch(worr_local_action_predictor_v1 *predictor,
                          uint8_t duration, uint32_t target,
                          uint32_t target_ammo) {
  const worr_command_record_v1 command =
      make_command(&predictor->state, duration);
  return Worr_LocalActionPredictorReplayV1(predictor, &command, false, true,
                                           target, target_ammo, NULL);
}

static bool test_switch(void) {
  worr_local_action_predictor_v1 predictor;
  worr_local_action_state_v2 state;
  uint32_t guard;

  /* Switching between two predictable weapons is itself predictable: the
   * request lowers the Shotgun, then the commit raises the Railgun. */
  CHECK(Worr_LocalActionPredictorSeedV1(
      &predictor, 5u, WORR_LOCAL_ACTION_CATALOG_SHOTGUN, 5, 0));
  CHECK(replay_switch(&predictor, 10, WORR_LOCAL_ACTION_CATALOG_RAILGUN, 4));
  CHECK(Worr_LocalActionPredictorGetStateV1(&predictor, &state));
  CHECK(state.phase == WORR_LOCAL_ACTION_PHASE_LOWERING);
  CHECK(state.pending_weapon_id == WORR_LOCAL_ACTION_CATALOG_RAILGUN);

  for (guard = 0; guard < 64u; ++guard) {
    CHECK(replay(&predictor, 250, false, NULL));
    CHECK(Worr_LocalActionPredictorGetStateV1(&predictor, &state));
    if (state.active_weapon_id == WORR_LOCAL_ACTION_CATALOG_RAILGUN &&
        state.phase == WORR_LOCAL_ACTION_PHASE_READY)
      break;
  }
  CHECK(state.active_weapon_id == WORR_LOCAL_ACTION_CATALOG_RAILGUN);
  CHECK(state.pending_weapon_id == 0);
  CHECK(state.active_ammo_units == 4);
  CHECK(state.phase == WORR_LOCAL_ACTION_PHASE_READY);
  return true;
}

static bool test_reconcile_match_and_resync(void) {
  worr_local_action_predictor_v1 predictor;
  worr_local_action_transaction_v2 predicted_one;
  worr_local_action_transaction_v2 predicted_two;
  worr_local_action_transaction_v2 authoritative;
  worr_local_action_correction_v2 correction;

  CHECK(Worr_LocalActionPredictorSeedV1(
      &predictor, 5u, WORR_LOCAL_ACTION_CATALOG_RAILGUN, 5, 0));

  /* Capture predicted transactions for two consecutive commands. */
  CHECK(replay(&predictor, 10, true, &predicted_one));
  CHECK(replay(&predictor, 20, true, &predicted_two));

  /* An authoritative transaction rebuilt from the exact same inputs matches
   * bit-for-bit, so reconciliation reports no correction. */
  memset(&authoritative, 0, sizeof(authoritative));
  CHECK(Worr_LocalActionBuildTransactionV2(
      &predicted_one.state_before, &predicted_one.command,
      &predicted_one.intent, &predicted_one.active_rule,
      &predicted_one.pending_rule, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE,
      &authoritative));
  memset(&correction, 0, sizeof(correction));
  CHECK(Worr_LocalActionPredictorReconcileV1(&predictor, &predicted_one,
                                             &authoritative, &correction));
  CHECK(correction.classification == WORR_LOCAL_ACTION_CORRECTION_NONE);
  CHECK(predictor.correction_count == 0);
  CHECK((predictor.flags & WORR_LOCAL_ACTION_PREDICTOR_ACTIVE) != 0);

  /* Reconciling a command-1 prediction against a command-2 authority is a
   * command-identity mismatch: a hard resync that deactivates the predictor. */
  memset(&authoritative, 0, sizeof(authoritative));
  CHECK(Worr_LocalActionBuildTransactionV2(
      &predicted_two.state_before, &predicted_two.command,
      &predicted_two.intent, &predicted_two.active_rule,
      &predicted_two.pending_rule, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE,
      &authoritative));
  memset(&correction, 0, sizeof(correction));
  CHECK(!Worr_LocalActionPredictorReconcileV1(&predictor, &predicted_one,
                                              &authoritative, &correction));
  CHECK(correction.classification ==
        WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC);
  CHECK((predictor.flags & WORR_LOCAL_ACTION_PREDICTOR_ACTIVE) == 0);
  CHECK(predictor.resync_count >= 1);
  return true;
}

static bool test_producer_matches_predictor(void) {
  worr_local_action_predictor_v1 predictor;
  worr_local_action_state_v2 before;
  worr_local_action_transaction_v2 predicted;
  worr_local_action_transaction_v2 authoritative;
  worr_local_action_transaction_v2 zero_tx;
  worr_local_action_correction_v2 correction;
  worr_command_record_v1 command;

  /* The client predictor (PREDICTED) and the server producer (AUTHORITATIVE),
   * given the same observed state and command, produce hash-identical
   * transactions -- the invariant that lets reconciliation converge across the
   * two seams. */
  CHECK(Worr_LocalActionPredictorSeedV1(
      &predictor, 5u, WORR_LOCAL_ACTION_CATALOG_GRENADE_LAUNCHER, 5, 0));
  CHECK(Worr_LocalActionPredictorGetStateV1(&predictor, &before));
  command = make_command(&before, 15);

  memset(&predicted, 0, sizeof(predicted));
  CHECK(Worr_LocalActionPredictorReplayV1(&predictor, &command, true, false, 0,
                                          0, &predicted));
  memset(&authoritative, 0, sizeof(authoritative));
  CHECK(Worr_LocalActionPredictorProduceV1(
      &before, &command, true, false, 0, 0,
      WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authoritative));

  CHECK(predicted.producer_role == WORR_LOCAL_ACTION_PRODUCER_PREDICTED);
  CHECK(authoritative.producer_role ==
        WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE);
  CHECK(predicted.transaction_hash == authoritative.transaction_hash);
  CHECK(predicted.state_hash == authoritative.state_hash);
  CHECK(predicted.event_hash == authoritative.event_hash);

  memset(&correction, 0, sizeof(correction));
  CHECK(Worr_LocalActionPredictorReconcileV1(&predictor, &predicted,
                                             &authoritative, &correction));
  CHECK(correction.classification == WORR_LOCAL_ACTION_CORRECTION_NONE);

  /* A non-zero output buffer and an invalid role both reject. */
  CHECK(!Worr_LocalActionPredictorProduceV1(
      &before, &command, true, false, 0, 0,
      WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authoritative));
  memset(&zero_tx, 0, sizeof(zero_tx));
  CHECK(!Worr_LocalActionPredictorProduceV1(&before, &command, true, false, 0,
                                            0, 99u, &zero_tx));
  return true;
}

int main(void) {
  if (!test_seed_and_inactive() || !test_fire_drain_refire() ||
      !test_dry_fire() || !test_switch() ||
      !test_producer_matches_predictor() ||
      !test_reconcile_match_and_resync()) {
    return 1;
  }
  puts("local_action_predictor_test: all checks passed");
  return 0;
}
