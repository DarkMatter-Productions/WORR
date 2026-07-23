#include "common/net/native_demo_playback.h"

#include <cstddef>
#include <type_traits>

static_assert(sizeof(worr_native_demo_playback_config_v1) == 32);
static_assert(offsetof(worr_native_demo_playback_config_v1, max_records) ==
              24);
static_assert(sizeof(worr_native_demo_playback_cursor_v1) == 296);
static_assert(offsetof(worr_native_demo_playback_cursor_v1,
                       current_entry_index) == 16);
static_assert(offsetof(worr_native_demo_playback_cursor_v1, scan_config) == 72);
static_assert(offsetof(worr_native_demo_playback_cursor_v1, scan) == 104);
static_assert(sizeof(worr_native_demo_playback_frame_v1) == 160);
static_assert(offsetof(worr_native_demo_playback_frame_v1, entry) == 32);
static_assert(offsetof(worr_native_demo_playback_frame_v1, hashes) == 96);
static_assert(
    std::is_standard_layout_v<worr_native_demo_playback_config_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_demo_playback_config_v1>);
static_assert(
    std::is_standard_layout_v<worr_native_demo_playback_cursor_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_demo_playback_cursor_v1>);
static_assert(
    std::is_standard_layout_v<worr_native_demo_playback_frame_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_demo_playback_frame_v1>);

int main() { return 0; }
