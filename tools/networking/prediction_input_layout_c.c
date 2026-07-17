#include "shared/cgame_prediction.h"

#include <stddef.h>

_Static_assert(sizeof(worr_cgame_prediction_input_command_v1) == 48,
               "C command layout drift");
_Static_assert(sizeof(worr_cgame_prediction_input_range_v1) == 6248,
               "C range layout drift");
_Static_assert(offsetof(worr_cgame_prediction_input_range_v1, commands) == 104,
               "C command-array offset drift");
_Static_assert(sizeof(worr_cgame_prediction_input_request_v2) == 32,
               "C request v2 layout drift");
_Static_assert(
    offsetof(worr_cgame_prediction_input_request_v2, consumed_command) == 16,
    "C request v2 cursor offset drift");
_Static_assert(sizeof(worr_cgame_command_record_entry_v1) == 112,
               "C command-record entry layout drift");
_Static_assert(sizeof(worr_cgame_command_record_range_v1) == 14360,
               "C command-record range layout drift");
_Static_assert(offsetof(worr_cgame_command_record_range_v1, commands) == 24,
               "C command-record array offset drift");

int main(void)
{
    return 0;
}
