#include "common/net/native_demo_playback.h"

#include <stddef.h>

_Static_assert(sizeof(worr_native_demo_playback_config_v1) == 32,
               "native demo playback config size changed");
_Static_assert(offsetof(worr_native_demo_playback_config_v1, max_records) ==
                   24,
               "native demo playback max-record offset changed");
_Static_assert(sizeof(worr_native_demo_playback_cursor_v1) == 296,
               "native demo playback cursor size changed");
_Static_assert(offsetof(worr_native_demo_playback_cursor_v1,
                        current_entry_index) == 16,
               "native demo playback current-index offset changed");
_Static_assert(offsetof(worr_native_demo_playback_cursor_v1, scan_config) ==
                   72,
               "native demo playback scan-config offset changed");
_Static_assert(offsetof(worr_native_demo_playback_cursor_v1, scan) == 104,
               "native demo playback scan offset changed");
_Static_assert(sizeof(worr_native_demo_playback_frame_v1) == 160,
               "native demo playback frame size changed");
_Static_assert(offsetof(worr_native_demo_playback_frame_v1, entry) == 32,
               "native demo playback frame entry offset changed");
_Static_assert(offsetof(worr_native_demo_playback_frame_v1, hashes) == 96,
               "native demo playback frame hashes offset changed");

int main(void) { return 0; }
