/* Deterministic FR-10-T08 predicted audiovisual reconciliation checks. */
#include "common/net/predicted_presentation.h"
#include "shared/local_action_abi.h"

#include <stdio.h>
#include <string.h>

#define MAX_ENTITIES UINT32_C(64)

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,         \
              #condition);                                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

typedef struct presentation_fixture_s {
  worr_event_record_v1 predicted[2];
  worr_event_record_v1 authoritative[2];
} presentation_fixture;

static worr_local_action_weapon_rule_v2 absent_rule(void) {
  worr_local_action_weapon_rule_v2 rule;
  memset(&rule, 0, sizeof(rule));
  rule.struct_size = sizeof(rule);
  rule.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
  rule.model_revision = WORR_LOCAL_ACTION_MODEL_REVISION;
  return rule;
}

static worr_local_action_weapon_rule_v2 audiovisual_rule(void) {
  worr_local_action_weapon_rule_v2 rule = absent_rule();
  rule.flags = WORR_LOCAL_ACTION_WEAPON_USES_AMMO |
               WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO |
               WORR_LOCAL_ACTION_WEAPON_PREDICT_EFFECT;
  rule.weapon_id = 1;
  rule.ammo_per_shot = 1;
  rule.refire_duration_ms = 20;
  rule.ready_frame = 11;
  rule.fire_frame = 12;
  rule.fire_audio_asset_id = 101;
  rule.dry_audio_asset_id = 102;
  rule.fire_effect_id = 103;
  rule.switch_effect_id = 104;
  return rule;
}

static worr_command_record_v1
next_command(const worr_local_action_state_v2 *state) {
  worr_command_record_v1 command;
  worr_command_id_v1 command_id = {0, 0};

  memset(&command, 0, sizeof(command));
  command.struct_size = sizeof(command);
  command.schema_version = WORR_COMMAND_ABI_VERSION;
  (void)Worr_CommandCursorNextIdV1(state->applied_cursor, &command_id);
  command.command_id = command_id;
  command.sample_time_us = state->sample_time_us;
  command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  command.command.struct_size = sizeof(command.command);
  command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  command.render_watermark.struct_size = sizeof(command.render_watermark);
  command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  return command;
}

static int build_fixture(presentation_fixture *fixture) {
  worr_local_action_state_v2 state;
  const worr_local_action_weapon_rule_v2 rule = audiovisual_rule();
  const worr_local_action_weapon_rule_v2 absent = absent_rule();
  worr_local_action_intent_v2 intent;
  worr_command_record_v1 command;
  worr_local_action_transaction_v2 predicted_transaction;
  worr_local_action_transaction_v2 authority_transaction;
  worr_local_action_event_record_context_v2 predicted_context;
  worr_local_action_event_record_context_v2 authority_context;
  uint32_t found = 0;
  uint32_t i;

  memset(fixture, 0, sizeof(*fixture));
  memset(&intent, 0, sizeof(intent));
  intent.struct_size = sizeof(intent);
  intent.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
  intent.flags = WORR_LOCAL_ACTION_INTENT_ATTACK_HELD;

  CHECK(Worr_LocalActionStateInitV2(&state, 7, 1, 3, rule.ready_frame));
  command = next_command(&state);
  memset(&predicted_transaction, 0, sizeof(predicted_transaction));
  memset(&authority_transaction, 0, sizeof(authority_transaction));
  CHECK(Worr_LocalActionBuildTransactionV2(
      &state, &command, &intent, &rule, &absent,
      WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &predicted_transaction));
  CHECK(Worr_LocalActionBuildTransactionV2(
      &state, &command, &intent, &rule, &absent,
      WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authority_transaction));
  CHECK(predicted_transaction.event_count == 3);
  CHECK(authority_transaction.event_count == 3);

  memset(&predicted_context, 0, sizeof(predicted_context));
  predicted_context.struct_size = sizeof(predicted_context);
  predicted_context.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
  predicted_context.model_revision = WORR_LOCAL_ACTION_MODEL_REVISION;
  predicted_context.producer_role = WORR_LOCAL_ACTION_PRODUCER_PREDICTED;
  predicted_context.source_tick = 100;
  predicted_context.lifetime_ticks = 8;
  predicted_context.source_entity.index = 1;
  predicted_context.source_entity.generation = 2;
  predicted_context.subject_entity.index = WORR_EVENT_NO_ENTITY;
  authority_context = predicted_context;
  authority_context.producer_role = WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE;
  authority_context.event_id.stream_epoch = 3;

  for (i = 0; i < predicted_transaction.event_count; ++i) {
    uint32_t output_index;
    const uint32_t lane = predicted_transaction.events[i].prediction_key.lane;

    if (lane == WORR_EVENT_PREDICTION_LANE_AUDIO)
      output_index = 0;
    else if (lane == WORR_EVENT_PREDICTION_LANE_EFFECT)
      output_index = 1;
    else
      continue;
    authority_context.event_id.sequence = i + 1;
    CHECK(Worr_LocalActionBuildEventRecordV2(
        &predicted_transaction.events[i], &predicted_context, MAX_ENTITIES,
        &fixture->predicted[output_index]));
    CHECK(Worr_LocalActionBuildEventRecordV2(
        &authority_transaction.events[i], &authority_context, MAX_ENTITIES,
        &fixture->authoritative[output_index]));
    ++found;
  }

  CHECK(found == 2);
  CHECK(fixture->predicted[0].event_type == WORR_EVENT_TYPE_AUDIO_CUE);
  CHECK(fixture->predicted[1].event_type == WORR_EVENT_TYPE_VISUAL_EFFECT);
  return 0;
}

static void mutate_authority_payload(worr_event_record_v1 *record) {
  if (record->payload_kind == WORR_EVENT_PAYLOAD_AUDIO) {
    worr_event_payload_audio_v1 payload;
    memcpy(&payload, record->payload, sizeof(payload));
    ++payload.asset_id;
    memcpy(record->payload, &payload, sizeof(payload));
  } else {
    worr_event_payload_effect_v1 payload;
    memcpy(&payload, record->payload, sizeof(payload));
    ++payload.effect_id;
    memcpy(record->payload, &payload, sizeof(payload));
  }
}

static int check_resolution(const worr_event_record_v1 *predicted,
                            const worr_event_record_v1 *authoritative,
                            bool presented, uint8_t expected_resolution,
                            uint8_t expected_action, uint32_t expected_flags) {
  worr_predicted_presentation_decision_v1 decision;

  memset(&decision, 0xa5, sizeof(decision));
  CHECK(Worr_PredictedPresentationResolveV1(
            predicted, authoritative, MAX_ENTITIES, presented, &decision) ==
        WORR_PREDICTED_PRESENTATION_OK);
  CHECK(Worr_PredictedPresentationDecisionValidateV1(&decision));
  CHECK(decision.resolution == expected_resolution);
  CHECK(decision.authority_action == expected_action);
  CHECK(decision.flags == expected_flags);
  CHECK(memcmp(&decision.prediction_key, &predicted->prediction_key,
               sizeof(decision.prediction_key)) == 0);
  CHECK(memcmp(&decision.authority_id, &authoritative->event_id,
               sizeof(decision.authority_id)) == 0);
  if ((expected_flags & WORR_PREDICTED_PRESENTATION_SEMANTIC_MATCH) != 0)
    CHECK(decision.predicted_semantic_hash ==
          decision.authoritative_semantic_hash);
  else
    CHECK(decision.predicted_semantic_hash !=
          decision.authoritative_semantic_hash);
  return 0;
}

static int test_audiovisual_resolution_matrix(void) {
  presentation_fixture fixture;
  uint32_t i;

  CHECK(build_fixture(&fixture) == 0);
  for (i = 0; i < 2; ++i) {
    worr_event_record_v1 corrected = fixture.authoritative[i];

    CHECK(check_resolution(&fixture.predicted[i], &fixture.authoritative[i],
                           false, WORR_PREDICTED_PRESENTATION_CONFIRMED_PENDING,
                           WORR_PREDICTED_PRESENTATION_PRESENT_AUTHORITY,
                           WORR_PREDICTED_PRESENTATION_SEMANTIC_MATCH) == 0);
    CHECK(check_resolution(
              &fixture.predicted[i], &fixture.authoritative[i], true,
              WORR_PREDICTED_PRESENTATION_CONFIRMED_SUPPRESSED,
              WORR_PREDICTED_PRESENTATION_SUPPRESS_AUTHORITY,
              WORR_PREDICTED_PRESENTATION_SEMANTIC_MATCH |
                  WORR_PREDICTED_PRESENTATION_SIDE_EFFECT_PRESENTED |
                  WORR_PREDICTED_PRESENTATION_AUTHORITY_SUPPRESSED) == 0);

    mutate_authority_payload(&corrected);
    CHECK(Worr_EventRecordValidateV1(&corrected, MAX_ENTITIES));
    CHECK(check_resolution(
              &fixture.predicted[i], &corrected, false,
              WORR_PREDICTED_PRESENTATION_CORRECTED_BEFORE_PRESENTATION,
              WORR_PREDICTED_PRESENTATION_PRESENT_AUTHORITY,
              WORR_PREDICTED_PRESENTATION_CORRECTION_REQUIRED) == 0);
    CHECK(check_resolution(
              &fixture.predicted[i], &corrected, true,
              WORR_PREDICTED_PRESENTATION_CORRECTED_AFTER_PRESENTATION,
              WORR_PREDICTED_PRESENTATION_SUPPRESS_AUTHORITY,
              WORR_PREDICTED_PRESENTATION_SIDE_EFFECT_PRESENTED |
                  WORR_PREDICTED_PRESENTATION_AUTHORITY_SUPPRESSED |
                  WORR_PREDICTED_PRESENTATION_CORRECTION_REQUIRED) == 0);
  }
  return 0;
}

static int
expect_unchanged_failure(const worr_event_record_v1 *predicted,
                         const worr_event_record_v1 *authoritative,
                         uint32_t max_entities,
                         worr_predicted_presentation_result_v1 expected) {
  worr_predicted_presentation_decision_v1 decision;
  worr_predicted_presentation_decision_v1 before;

  memset(&decision, 0xa5, sizeof(decision));
  before = decision;
  CHECK(Worr_PredictedPresentationResolveV1(predicted, authoritative,
                                            max_entities, false,
                                            &decision) == expected);
  CHECK(memcmp(&decision, &before, sizeof(decision)) == 0);
  return 0;
}

static int test_fail_closed_inputs_and_overlap(void) {
  presentation_fixture fixture;
  worr_event_record_v1 bad_prediction;
  worr_event_record_v1 bad_authority;
  worr_event_record_v1 prediction_before;

  CHECK(build_fixture(&fixture) == 0);
  CHECK(expect_unchanged_failure(
            NULL, &fixture.authoritative[0], MAX_ENTITIES,
            WORR_PREDICTED_PRESENTATION_INVALID_ARGUMENT) == 0);
  CHECK(expect_unchanged_failure(
            &fixture.predicted[0], NULL, MAX_ENTITIES,
            WORR_PREDICTED_PRESENTATION_INVALID_ARGUMENT) == 0);
  CHECK(expect_unchanged_failure(
            &fixture.predicted[0], &fixture.authoritative[0], 0,
            WORR_PREDICTED_PRESENTATION_INVALID_ARGUMENT) == 0);
  CHECK(Worr_PredictedPresentationResolveV1(
            &fixture.predicted[0], &fixture.authoritative[0], MAX_ENTITIES,
            false, NULL) == WORR_PREDICTED_PRESENTATION_INVALID_ARGUMENT);

  bad_prediction = fixture.predicted[0];
  bad_prediction.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
  CHECK(expect_unchanged_failure(
            &bad_prediction, &fixture.authoritative[0], MAX_ENTITIES,
            WORR_PREDICTED_PRESENTATION_INVALID_PREDICTION) == 0);
  bad_prediction = fixture.predicted[0];
  bad_prediction.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
  memset(&bad_prediction.prediction_key, 0,
         sizeof(bad_prediction.prediction_key));
  CHECK(Worr_EventRecordCandidateValidateV1(&bad_prediction, MAX_ENTITIES));
  CHECK(expect_unchanged_failure(
            &bad_prediction, &fixture.authoritative[0], MAX_ENTITIES,
            WORR_PREDICTED_PRESENTATION_INVALID_PREDICTION) == 0);

  bad_authority = fixture.authoritative[0];
  bad_authority.reserved0 = 1;
  CHECK(expect_unchanged_failure(
            &fixture.predicted[0], &bad_authority, MAX_ENTITIES,
            WORR_PREDICTED_PRESENTATION_INVALID_AUTHORITY) == 0);
  bad_authority = fixture.authoritative[0];
  bad_authority.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
  memset(&bad_authority.prediction_key, 0,
         sizeof(bad_authority.prediction_key));
  CHECK(Worr_EventRecordValidateV1(&bad_authority, MAX_ENTITIES));
  CHECK(expect_unchanged_failure(
            &fixture.predicted[0], &bad_authority, MAX_ENTITIES,
            WORR_PREDICTED_PRESENTATION_INVALID_AUTHORITY) == 0);

  bad_authority = fixture.authoritative[0];
  ++bad_authority.prediction_key.command_sequence;
  CHECK(Worr_EventRecordValidateV1(&bad_authority, MAX_ENTITIES));
  CHECK(expect_unchanged_failure(
            &fixture.predicted[0], &bad_authority, MAX_ENTITIES,
            WORR_PREDICTED_PRESENTATION_KEY_MISMATCH) == 0);

  prediction_before = fixture.predicted[0];
  CHECK(Worr_PredictedPresentationResolveV1(
            &fixture.predicted[0], &fixture.authoritative[0], MAX_ENTITIES,
            false,
            (worr_predicted_presentation_decision_v1 *)(void *)&fixture
                .predicted[0]) == WORR_PREDICTED_PRESENTATION_OUTPUT_OVERLAP);
  CHECK(memcmp(&fixture.predicted[0], &prediction_before,
               sizeof(prediction_before)) == 0);
  bad_authority = fixture.authoritative[0];
  CHECK(
      Worr_PredictedPresentationResolveV1(
          &fixture.predicted[0], &bad_authority, MAX_ENTITIES, false,
          (worr_predicted_presentation_decision_v1 *)(void *)&bad_authority) ==
      WORR_PREDICTED_PRESENTATION_OUTPUT_OVERLAP);
  CHECK(memcmp(&bad_authority, &fixture.authoritative[0],
               sizeof(bad_authority)) == 0);
  return 0;
}

static int test_decision_validator_rejects_corruption(void) {
  presentation_fixture fixture;
  worr_predicted_presentation_decision_v1 decision;
  worr_predicted_presentation_decision_v1 corrupt;

  CHECK(build_fixture(&fixture) == 0);
  CHECK(Worr_PredictedPresentationResolveV1(
            &fixture.predicted[1], &fixture.authoritative[1], MAX_ENTITIES,
            false, &decision) == WORR_PREDICTED_PRESENTATION_OK);
  CHECK(Worr_PredictedPresentationDecisionValidateV1(&decision));

  corrupt = decision;
  corrupt.flags |= WORR_PREDICTED_PRESENTATION_CORRECTION_REQUIRED;
  CHECK(!Worr_PredictedPresentationDecisionValidateV1(&corrupt));
  corrupt = decision;
  corrupt.authority_action = WORR_PREDICTED_PRESENTATION_SUPPRESS_AUTHORITY;
  CHECK(!Worr_PredictedPresentationDecisionValidateV1(&corrupt));
  corrupt = decision;
  corrupt.resolution = 0;
  CHECK(!Worr_PredictedPresentationDecisionValidateV1(&corrupt));
  corrupt = decision;
  corrupt.prediction_key.lane = 0;
  CHECK(!Worr_PredictedPresentationDecisionValidateV1(&corrupt));
  corrupt = decision;
  corrupt.authority_id.sequence = 0;
  CHECK(!Worr_PredictedPresentationDecisionValidateV1(&corrupt));
  corrupt = decision;
  corrupt.authoritative_semantic_hash ^= UINT64_C(1);
  CHECK(!Worr_PredictedPresentationDecisionValidateV1(&corrupt));
  corrupt = decision;
  corrupt.reserved0 = 1;
  CHECK(!Worr_PredictedPresentationDecisionValidateV1(&corrupt));
  return 0;
}

int main(void) {
  CHECK(test_audiovisual_resolution_matrix() == 0);
  CHECK(test_fail_closed_inputs_and_overlap() == 0);
  CHECK(test_decision_validator_rejects_corruption() == 0);
  puts("predicted presentation tests passed");
  return 0;
}
