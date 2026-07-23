#include "common/net/predicted_presentation.h"

#include <stddef.h>

_Static_assert(sizeof(worr_predicted_presentation_decision_v1) == 64,
               "predicted presentation decision size changed");
_Static_assert(offsetof(worr_predicted_presentation_decision_v1,
                        schema_version) == 4,
               "predicted presentation schema offset changed");
_Static_assert(offsetof(worr_predicted_presentation_decision_v1, resolution) ==
                   6,
               "predicted presentation resolution offset changed");
_Static_assert(offsetof(worr_predicted_presentation_decision_v1,
                        authority_action) == 7,
               "predicted presentation action offset changed");
_Static_assert(offsetof(worr_predicted_presentation_decision_v1,
                        prediction_key) == 8,
               "predicted presentation key offset changed");
_Static_assert(offsetof(worr_predicted_presentation_decision_v1,
                        authority_id) == 24,
               "predicted presentation authority offset changed");
_Static_assert(offsetof(worr_predicted_presentation_decision_v1,
                        predicted_semantic_hash) == 32,
               "predicted presentation predicted hash offset changed");
_Static_assert(offsetof(worr_predicted_presentation_decision_v1,
                        authoritative_semantic_hash) == 40,
               "predicted presentation authority hash offset changed");
_Static_assert(offsetof(worr_predicted_presentation_decision_v1,
                        model_revision) == 48,
               "predicted presentation model offset changed");
_Static_assert(offsetof(worr_predicted_presentation_decision_v1, flags) == 52,
               "predicted presentation flags offset changed");
_Static_assert(offsetof(worr_predicted_presentation_decision_v1, reserved0) ==
                   56,
               "predicted presentation reserved offset changed");

int main(void) { return 0; }
