/* C++20 pointer-free layout proof for the FR-10-T16 delivery planner. */

#include "common/net/native_input_delivery.h"

#include <cstddef>
#include <type_traits>

template <typename T>
constexpr bool portable_record = std::is_standard_layout_v<T> &&
                                 std::is_trivially_copyable_v<T> &&
                                 std::is_trivially_destructible_v<T>;

static_assert(portable_record<worr_native_input_delivery_config_v1>);
static_assert(portable_record<worr_native_input_delivery_feedback_v1>);
static_assert(portable_record<worr_native_input_delivery_candidate_v1>);
static_assert(portable_record<worr_native_input_delivery_selection_v1>);
static_assert(portable_record<worr_native_input_delivery_plan_v1>);
static_assert(portable_record<worr_native_input_delivery_telemetry_v1>);
static_assert(portable_record<worr_native_input_delivery_state_v1>);

static_assert(sizeof(worr_native_input_delivery_config_v1) == 48);
static_assert(sizeof(worr_native_input_delivery_feedback_v1) == 40);
static_assert(sizeof(worr_native_input_delivery_candidate_v1) == 32);
static_assert(sizeof(worr_native_input_delivery_selection_v1) == 32);
static_assert(sizeof(worr_native_input_delivery_plan_v1) == 336);
static_assert(sizeof(worr_native_input_delivery_telemetry_v1) == 120);
static_assert(sizeof(worr_native_input_delivery_state_v1) == 168);
static_assert(offsetof(worr_native_input_delivery_plan_v1, selections) == 80);
static_assert(offsetof(worr_native_input_delivery_state_v1, telemetry) == 48);
static_assert(!std::is_pointer_v<decltype(
              worr_native_input_delivery_candidate_v1::command_id)>);
static_assert(!std::is_pointer_v<decltype(
              worr_native_input_delivery_plan_v1::selections)>);
static_assert(!std::is_pointer_v<decltype(
              worr_native_input_delivery_state_v1::telemetry)>);

int main()
{
    return WORR_NATIVE_INPUT_DELIVERY_VERSION == 1 ? 0 : 1;
}
