#include "common/net/native_demo.h"

#include <cstddef>
#include <type_traits>

static_assert(sizeof(worr_native_demo_container_config_v1) == 48);
static_assert(offsetof(worr_native_demo_container_config_v1,
                       first_record_ordinal) == 24);
static_assert(sizeof(worr_native_demo_container_info_v1) == 56);
static_assert(offsetof(worr_native_demo_container_info_v1,
                       first_record_ordinal) == 24);
static_assert(sizeof(worr_native_demo_record_v1) == 32);
static_assert(offsetof(worr_native_demo_record_v1, ordinal) == 8);
static_assert(sizeof(worr_native_demo_record_info_v1) == 112);
static_assert(offsetof(worr_native_demo_record_info_v1, record_offset) == 40);
static_assert(offsetof(worr_native_demo_record_info_v1, codec) == 64);
static_assert(sizeof(worr_native_demo_index_entry_v1) == 64);
static_assert(offsetof(worr_native_demo_index_entry_v1, record) == 48);
static_assert(sizeof(worr_native_demo_order_state_v1) == 72);
static_assert(offsetof(worr_native_demo_order_state_v1, seen_class_mask) == 56);
static_assert(sizeof(worr_native_demo_scan_config_v1) == 32);
static_assert(offsetof(worr_native_demo_scan_config_v1, max_records) == 16);
static_assert(offsetof(worr_native_demo_scan_config_v1, max_entities) == 24);
static_assert(sizeof(worr_native_demo_scan_info_v1) == 184);
static_assert(offsetof(worr_native_demo_scan_info_v1, container) == 48);
static_assert(offsetof(worr_native_demo_scan_info_v1, order) == 104);
static_assert(offsetof(worr_native_demo_scan_info_v1, index_crc32) == 176);
static_assert(sizeof(worr_native_demo_seek_query_v1) == 32);
static_assert(offsetof(worr_native_demo_seek_query_v1, target_time_us) == 8);
static_assert(sizeof(worr_native_demo_seek_result_v1) == 88);
static_assert(offsetof(worr_native_demo_seek_result_v1, entry) == 16);
static_assert(std::is_standard_layout_v<worr_native_demo_container_config_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_demo_container_config_v1>);
static_assert(std::is_standard_layout_v<worr_native_demo_container_info_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_demo_container_info_v1>);
static_assert(std::is_standard_layout_v<worr_native_demo_record_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_demo_record_v1>);
static_assert(std::is_standard_layout_v<worr_native_demo_record_info_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_demo_record_info_v1>);
static_assert(std::is_standard_layout_v<worr_native_demo_index_entry_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_demo_index_entry_v1>);
static_assert(std::is_standard_layout_v<worr_native_demo_order_state_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_demo_order_state_v1>);
static_assert(std::is_standard_layout_v<worr_native_demo_scan_config_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_demo_scan_config_v1>);
static_assert(std::is_standard_layout_v<worr_native_demo_scan_info_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_demo_scan_info_v1>);
static_assert(std::is_standard_layout_v<worr_native_demo_seek_query_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_demo_seek_query_v1>);
static_assert(std::is_standard_layout_v<worr_native_demo_seek_result_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_demo_seek_result_v1>);
static_assert(WORR_NATIVE_DEMO_WIRE_HEADER_BYTES == 64);
static_assert(WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES == 48);
static_assert(WORR_NATIVE_DEMO_MAX_RECORD_BYTES == 131120);

int main() { return 0; }
