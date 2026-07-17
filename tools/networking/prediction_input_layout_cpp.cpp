#include "shared/cgame_prediction.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<
              worr_cgame_prediction_input_command_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_cgame_prediction_input_range_v1>);
static_assert(sizeof(worr_cgame_prediction_input_range_v1) == 6248);
static_assert(offsetof(worr_cgame_prediction_input_range_v1, commands) == 104);
static_assert(std::is_standard_layout_v<
              worr_cgame_prediction_input_request_v2>);
static_assert(sizeof(worr_cgame_prediction_input_request_v2) == 32);
static_assert(offsetof(worr_cgame_prediction_input_request_v2,
                       consumed_command) == 16);
static_assert(sizeof(worr_cgame_command_record_entry_v1) == 112);
static_assert(sizeof(worr_cgame_command_record_range_v1) == 14360);
static_assert(offsetof(worr_cgame_command_record_range_v1, commands) == 24);

int main()
{
    return 0;
}
