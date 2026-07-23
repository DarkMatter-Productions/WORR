#include "common/net/native_input_batch.h"
#include "common/net/native_input_batch_sideband.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<worr_native_input_batch_info_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_input_batch_info_v1>);
static_assert(sizeof(worr_native_input_batch_info_v1) == 36);
static_assert(offsetof(worr_native_input_batch_info_v1, payload_crc32) == 28);
static_assert(WORR_NATIVE_INPUT_BATCH_MAX_PAYLOAD_BYTES == 912);

static_assert(
    std::is_standard_layout_v<worr_native_input_batch_confirm_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_input_batch_confirm_v1>);
static_assert(sizeof(worr_native_input_batch_confirm_v1) == 32);
static_assert(sizeof(worr_native_input_batch_sideband_parser_v1) == 56);
static_assert(WORR_NATIVE_INPUT_BATCH_SIDEBAND_WIRE_BYTES == 63);

int main()
{
    return 0;
}
