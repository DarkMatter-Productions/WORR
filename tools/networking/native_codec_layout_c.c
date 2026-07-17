#include "common/net/native_codec.h"

#include <stddef.h>

_Static_assert(sizeof(worr_native_codec_info_v1) == 48,
               "native codec info size changed");
_Static_assert(offsetof(worr_native_codec_info_v1, record_class) == 6,
               "native codec record-class offset changed");
_Static_assert(offsetof(worr_native_codec_info_v1, model_revision) == 12,
               "native codec model-revision offset changed");
_Static_assert(offsetof(worr_native_codec_info_v1, range_counts) == 24,
               "native codec range-count offset changed");
_Static_assert(offsetof(worr_native_codec_info_v1, object_epoch) == 36,
               "native codec object identity offset changed");
_Static_assert(WORR_NATIVE_CODEC_WIRE_HEADER_BYTES == 48,
               "native codec wire header changed");
_Static_assert(WORR_NATIVE_CODEC_MAX_ENCODED_BYTES == 131072,
               "native codec envelope bound changed");
_Static_assert(WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MIN_BYTES == 52 &&
                   WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MAX_BYTES == 125,
               "native codec snapshot entity bounds changed");

int main(void)
{
    return 0;
}
