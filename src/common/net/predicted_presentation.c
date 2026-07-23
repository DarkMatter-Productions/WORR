/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/predicted_presentation.h"

#include <string.h>

static bool regions_overlap(const void *left, size_t left_bytes,
                            const void *right, size_t right_bytes) {
  const uintptr_t left_begin = (uintptr_t)left;
  const uintptr_t right_begin = (uintptr_t)right;
  uintptr_t left_end;
  uintptr_t right_end;

  if (left_bytes == 0 || right_bytes == 0)
    return false;
  if (left_begin > UINTPTR_MAX - left_bytes ||
      right_begin > UINTPTR_MAX - right_bytes) {
    return true;
  }
  left_end = left_begin + left_bytes;
  right_end = right_begin + right_bytes;
  return left_begin < right_end && right_begin < left_end;
}

static bool prediction_key_equal(worr_event_prediction_key_v1 left,
                                 worr_event_prediction_key_v1 right) {
  return left.command_epoch == right.command_epoch &&
         left.command_sequence == right.command_sequence &&
         left.emitter_ordinal == right.emitter_ordinal &&
         left.lane == right.lane;
}

static bool prediction_key_valid(worr_event_prediction_key_v1 key) {
  return key.command_epoch != 0 && key.command_sequence != 0 &&
         key.lane >= WORR_EVENT_PREDICTION_LANE_GAMEPLAY &&
         key.lane <= WORR_EVENT_PREDICTION_LANE_EFFECT;
}

static uint32_t expected_flags(uint8_t resolution) {
  switch (resolution) {
  case WORR_PREDICTED_PRESENTATION_CONFIRMED_PENDING:
    return WORR_PREDICTED_PRESENTATION_SEMANTIC_MATCH;
  case WORR_PREDICTED_PRESENTATION_CONFIRMED_SUPPRESSED:
    return WORR_PREDICTED_PRESENTATION_SEMANTIC_MATCH |
           WORR_PREDICTED_PRESENTATION_SIDE_EFFECT_PRESENTED |
           WORR_PREDICTED_PRESENTATION_AUTHORITY_SUPPRESSED;
  case WORR_PREDICTED_PRESENTATION_CORRECTED_BEFORE_PRESENTATION:
    return WORR_PREDICTED_PRESENTATION_CORRECTION_REQUIRED;
  case WORR_PREDICTED_PRESENTATION_CORRECTED_AFTER_PRESENTATION:
    return WORR_PREDICTED_PRESENTATION_SIDE_EFFECT_PRESENTED |
           WORR_PREDICTED_PRESENTATION_AUTHORITY_SUPPRESSED |
           WORR_PREDICTED_PRESENTATION_CORRECTION_REQUIRED;
  default:
    return 0;
  }
}

bool Worr_PredictedPresentationDecisionValidateV1(
    const worr_predicted_presentation_decision_v1 *decision) {
  uint8_t expected_action;
  uint32_t flags;

  if (!decision || decision->struct_size != sizeof(*decision) ||
      decision->schema_version != WORR_PREDICTED_PRESENTATION_VERSION ||
      decision->model_revision != WORR_EVENT_MODEL_REVISION ||
      !prediction_key_valid(decision->prediction_key) ||
      decision->authority_id.stream_epoch == 0 ||
      decision->authority_id.sequence == 0 || decision->reserved0 != 0) {
    return false;
  }

  flags = expected_flags(decision->resolution);
  if (flags == 0 || decision->flags != flags)
    return false;
  expected_action =
      (flags & WORR_PREDICTED_PRESENTATION_AUTHORITY_SUPPRESSED) != 0
          ? WORR_PREDICTED_PRESENTATION_SUPPRESS_AUTHORITY
          : WORR_PREDICTED_PRESENTATION_PRESENT_AUTHORITY;
  if (decision->authority_action != expected_action)
    return false;
  if ((flags & WORR_PREDICTED_PRESENTATION_SEMANTIC_MATCH) != 0 &&
      decision->predicted_semantic_hash !=
          decision->authoritative_semantic_hash) {
    return false;
  }
  return true;
}

worr_predicted_presentation_result_v1 Worr_PredictedPresentationResolveV1(
    const worr_event_record_v1 *predicted,
    const worr_event_record_v1 *authoritative, uint32_t max_entities,
    bool side_effect_presented,
    worr_predicted_presentation_decision_v1 *decision_out) {
  worr_predicted_presentation_decision_v1 decision;
  bool semantic_match;

  if (!predicted || !authoritative || !decision_out || max_entities == 0)
    return WORR_PREDICTED_PRESENTATION_INVALID_ARGUMENT;
  if (regions_overlap(decision_out, sizeof(*decision_out), predicted,
                      sizeof(*predicted)) ||
      regions_overlap(decision_out, sizeof(*decision_out), authoritative,
                      sizeof(*authoritative))) {
    return WORR_PREDICTED_PRESENTATION_OUTPUT_OVERLAP;
  }
  if (!Worr_EventRecordCandidateValidateV1(predicted, max_entities) ||
      predicted->prediction_class == WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY) {
    return WORR_PREDICTED_PRESENTATION_INVALID_PREDICTION;
  }
  if (!Worr_EventRecordValidateV1(authoritative, max_entities) ||
      authoritative->prediction_class ==
          WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY) {
    return WORR_PREDICTED_PRESENTATION_INVALID_AUTHORITY;
  }
  if (!prediction_key_equal(predicted->prediction_key,
                            authoritative->prediction_key)) {
    return WORR_PREDICTED_PRESENTATION_KEY_MISMATCH;
  }

  memset(&decision, 0, sizeof(decision));
  decision.struct_size = sizeof(decision);
  decision.schema_version = WORR_PREDICTED_PRESENTATION_VERSION;
  decision.prediction_key = predicted->prediction_key;
  decision.authority_id = authoritative->event_id;
  decision.model_revision = WORR_EVENT_MODEL_REVISION;
  if (!Worr_EventRecordSemanticHashV1(predicted, max_entities,
                                      &decision.predicted_semantic_hash) ||
      !Worr_EventRecordSemanticHashV1(authoritative, max_entities,
                                      &decision.authoritative_semantic_hash)) {
    return WORR_PREDICTED_PRESENTATION_INVALID_ARGUMENT;
  }

  semantic_match = Worr_EventRecordSemanticallyEqualV1(predicted, authoritative,
                                                       max_entities);
  if (semantic_match) {
    decision.flags |= WORR_PREDICTED_PRESENTATION_SEMANTIC_MATCH;
    decision.resolution = side_effect_presented
                              ? WORR_PREDICTED_PRESENTATION_CONFIRMED_SUPPRESSED
                              : WORR_PREDICTED_PRESENTATION_CONFIRMED_PENDING;
  } else {
    decision.flags |= WORR_PREDICTED_PRESENTATION_CORRECTION_REQUIRED;
    decision.resolution =
        side_effect_presented
            ? WORR_PREDICTED_PRESENTATION_CORRECTED_AFTER_PRESENTATION
            : WORR_PREDICTED_PRESENTATION_CORRECTED_BEFORE_PRESENTATION;
  }
  if (side_effect_presented) {
    decision.flags |= WORR_PREDICTED_PRESENTATION_SIDE_EFFECT_PRESENTED |
                      WORR_PREDICTED_PRESENTATION_AUTHORITY_SUPPRESSED;
    decision.authority_action = WORR_PREDICTED_PRESENTATION_SUPPRESS_AUTHORITY;
  } else {
    decision.authority_action = WORR_PREDICTED_PRESENTATION_PRESENT_AUTHORITY;
  }

  if (!Worr_PredictedPresentationDecisionValidateV1(&decision))
    return WORR_PREDICTED_PRESENTATION_INVALID_ARGUMENT;
  *decision_out = decision;
  return WORR_PREDICTED_PRESENTATION_OK;
}
