#include "common/net/predicted_presentation.h"

#include <cstddef>
#include <type_traits>

static_assert(sizeof(worr_predicted_presentation_decision_v1) == 64);
static_assert(offsetof(worr_predicted_presentation_decision_v1,
                       schema_version) == 4);
static_assert(offsetof(worr_predicted_presentation_decision_v1, resolution) ==
              6);
static_assert(offsetof(worr_predicted_presentation_decision_v1,
                       authority_action) == 7);
static_assert(offsetof(worr_predicted_presentation_decision_v1,
                       prediction_key) == 8);
static_assert(offsetof(worr_predicted_presentation_decision_v1, authority_id) ==
              24);
static_assert(offsetof(worr_predicted_presentation_decision_v1,
                       predicted_semantic_hash) == 32);
static_assert(offsetof(worr_predicted_presentation_decision_v1,
                       authoritative_semantic_hash) == 40);
static_assert(offsetof(worr_predicted_presentation_decision_v1,
                       model_revision) == 48);
static_assert(offsetof(worr_predicted_presentation_decision_v1, flags) == 52);
static_assert(offsetof(worr_predicted_presentation_decision_v1, reserved0) ==
              56);
static_assert(
    std::is_standard_layout_v<worr_predicted_presentation_decision_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_predicted_presentation_decision_v1>);

int main() { return 0; }
