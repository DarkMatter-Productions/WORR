/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/event_abi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure reconciliation policy for one command-predicted event and its exact
 * authoritative counterpart.  Presentation remains caller-owned: this core
 * only decides whether canonical authority presentation remains pending or
 * must be suppressed because a side effect for this key already escaped.
 */
#define WORR_PREDICTED_PRESENTATION_VERSION UINT16_C(1)

typedef enum worr_predicted_presentation_result_v1_e {
  WORR_PREDICTED_PRESENTATION_OK = 0,
  WORR_PREDICTED_PRESENTATION_INVALID_ARGUMENT = 1,
  WORR_PREDICTED_PRESENTATION_INVALID_PREDICTION = 2,
  WORR_PREDICTED_PRESENTATION_INVALID_AUTHORITY = 3,
  WORR_PREDICTED_PRESENTATION_KEY_MISMATCH = 4,
  WORR_PREDICTED_PRESENTATION_OUTPUT_OVERLAP = 5,
} worr_predicted_presentation_result_v1;

typedef enum worr_predicted_presentation_resolution_v1_e {
  WORR_PREDICTED_PRESENTATION_CONFIRMED_PENDING = 1,
  WORR_PREDICTED_PRESENTATION_CONFIRMED_SUPPRESSED = 2,
  WORR_PREDICTED_PRESENTATION_CORRECTED_BEFORE_PRESENTATION = 3,
  WORR_PREDICTED_PRESENTATION_CORRECTED_AFTER_PRESENTATION = 4,
} worr_predicted_presentation_resolution_v1;

typedef enum worr_predicted_presentation_authority_action_v1_e {
  WORR_PREDICTED_PRESENTATION_PRESENT_AUTHORITY = 1,
  WORR_PREDICTED_PRESENTATION_SUPPRESS_AUTHORITY = 2,
} worr_predicted_presentation_authority_action_v1;

enum {
  WORR_PREDICTED_PRESENTATION_SEMANTIC_MATCH = UINT32_C(1) << 0,
  WORR_PREDICTED_PRESENTATION_SIDE_EFFECT_PRESENTED = UINT32_C(1) << 1,
  WORR_PREDICTED_PRESENTATION_AUTHORITY_SUPPRESSED = UINT32_C(1) << 2,
  WORR_PREDICTED_PRESENTATION_CORRECTION_REQUIRED = UINT32_C(1) << 3,
};

/* Pointer-free diagnostic decision.  It is safe to retain or serialize. */
typedef struct worr_predicted_presentation_decision_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint8_t resolution;
  uint8_t authority_action;
  worr_event_prediction_key_v1 prediction_key;
  worr_event_id_v1 authority_id;
  uint64_t predicted_semantic_hash;
  uint64_t authoritative_semantic_hash;
  uint32_t model_revision;
  uint32_t flags;
  uint64_t reserved0;
} worr_predicted_presentation_decision_v1;

bool Worr_PredictedPresentationDecisionValidateV1(
    const worr_predicted_presentation_decision_v1 *decision);

/*
 * Both records must name the same non-authoritative prediction key.  Failure
 * leaves decision_out byte-identical.  The output must not overlap an input.
 */
worr_predicted_presentation_result_v1 Worr_PredictedPresentationResolveV1(
    const worr_event_record_v1 *predicted,
    const worr_event_record_v1 *authoritative, uint32_t max_entities,
    bool side_effect_presented,
    worr_predicted_presentation_decision_v1 *decision_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_PREDICTED_PRESENTATION_STATIC_ASSERT(condition, message)          \
  static_assert((condition), message)
#else
#define WORR_PREDICTED_PRESENTATION_STATIC_ASSERT(condition, message)          \
  _Static_assert((condition), message)
#endif

WORR_PREDICTED_PRESENTATION_STATIC_ASSERT(
    sizeof(worr_predicted_presentation_decision_v1) == 64,
    "predicted presentation decision layout changed");
WORR_PREDICTED_PRESENTATION_STATIC_ASSERT(
    offsetof(worr_predicted_presentation_decision_v1, prediction_key) == 8,
    "predicted presentation key offset changed");
WORR_PREDICTED_PRESENTATION_STATIC_ASSERT(
    offsetof(worr_predicted_presentation_decision_v1,
             predicted_semantic_hash) == 32,
    "predicted presentation hash offset changed");

#undef WORR_PREDICTED_PRESENTATION_STATIC_ASSERT
