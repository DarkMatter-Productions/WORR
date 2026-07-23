#include "common/net/native_demo.h"

#include <stddef.h>

_Static_assert(sizeof(worr_native_demo_container_config_v1) == 48,
               "native demo config size changed");
_Static_assert(offsetof(worr_native_demo_container_config_v1,
                        first_record_ordinal) == 24,
               "native demo first ordinal offset changed");
_Static_assert(sizeof(worr_native_demo_container_info_v1) == 56,
               "native demo container info size changed");
_Static_assert(offsetof(worr_native_demo_container_info_v1,
                        first_record_ordinal) == 24,
               "native demo decoded first ordinal offset changed");
_Static_assert(sizeof(worr_native_demo_record_v1) == 32,
               "native demo record size changed");
_Static_assert(offsetof(worr_native_demo_record_v1, ordinal) == 8,
               "native demo ordinal offset changed");
_Static_assert(sizeof(worr_native_demo_record_info_v1) == 112,
               "native demo record info size changed");
_Static_assert(offsetof(worr_native_demo_record_info_v1, record_offset) == 40,
               "native demo record offset changed");
_Static_assert(offsetof(worr_native_demo_record_info_v1, codec) == 64,
               "native demo codec info offset changed");
_Static_assert(sizeof(worr_native_demo_index_entry_v1) == 64,
               "native demo index entry size changed");
_Static_assert(offsetof(worr_native_demo_index_entry_v1, record) == 48,
               "native demo index record-ref offset changed");
_Static_assert(sizeof(worr_native_demo_order_state_v1) == 72,
               "native demo order state size changed");
_Static_assert(offsetof(worr_native_demo_order_state_v1, seen_class_mask) == 56,
               "native demo order seen-mask offset changed");
_Static_assert(sizeof(worr_native_demo_scan_config_v1) == 32,
               "native demo scan config size changed");
_Static_assert(offsetof(worr_native_demo_scan_config_v1, max_records) == 16,
               "native demo scan bound offset changed");
_Static_assert(offsetof(worr_native_demo_scan_config_v1, max_entities) == 24,
               "native demo scan entity bound offset changed");
_Static_assert(sizeof(worr_native_demo_scan_info_v1) == 184,
               "native demo scan info size changed");
_Static_assert(offsetof(worr_native_demo_scan_info_v1, container) == 48,
               "native demo scan container offset changed");
_Static_assert(offsetof(worr_native_demo_scan_info_v1, order) == 104,
               "native demo scan order offset changed");
_Static_assert(offsetof(worr_native_demo_scan_info_v1, index_crc32) == 176,
               "native demo scan index CRC offset changed");
_Static_assert(sizeof(worr_native_demo_seek_query_v1) == 32,
               "native demo seek query size changed");
_Static_assert(offsetof(worr_native_demo_seek_query_v1, target_time_us) == 8,
               "native demo seek time offset changed");
_Static_assert(sizeof(worr_native_demo_seek_result_v1) == 88,
               "native demo seek result size changed");
_Static_assert(offsetof(worr_native_demo_seek_result_v1, entry) == 16,
               "native demo seek entry offset changed");
_Static_assert(WORR_NATIVE_DEMO_WIRE_HEADER_BYTES == 64,
               "native demo wire header changed");
_Static_assert(WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES == 48,
               "native demo record wire header changed");
_Static_assert(WORR_NATIVE_DEMO_MAX_RECORD_BYTES == 131120,
               "native demo record bound changed");

int main(void) { return 0; }
