#include "common/net/native_codec.h"

#include <cstddef>
#include <type_traits>

static_assert(sizeof(worr_native_codec_info_v1) == 48);
static_assert(offsetof(worr_native_codec_info_v1, record_class) == 6);
static_assert(offsetof(worr_native_codec_info_v1, model_revision) == 12);
static_assert(offsetof(worr_native_codec_info_v1, range_counts) == 24);
static_assert(offsetof(worr_native_codec_info_v1, object_epoch) == 36);
static_assert(std::is_standard_layout_v<worr_native_codec_info_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_codec_info_v1>);
static_assert(WORR_NATIVE_CODEC_WIRE_HEADER_BYTES == 48);
static_assert(WORR_NATIVE_CODEC_MAX_ENCODED_BYTES == 131072);
static_assert(WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MIN_BYTES == 52);
static_assert(WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MAX_BYTES == 125);

int main()
{
    return 0;
}
