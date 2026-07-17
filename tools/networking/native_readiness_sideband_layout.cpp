#include "common/net/native_readiness_sideband.h"

#include "common/net/consumed_cursor_sideband.h"
#include "common/net/demo_clock_sideband.h"
#include "common/net/legacy_command_adapter.h"

#include <cstddef>
#include <type_traits>

static_assert(
    std::is_standard_layout_v<worr_native_readiness_setting_pair_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_readiness_setting_pair_v1>);
static_assert(sizeof(worr_native_readiness_setting_pair_v1) == 4);
static_assert(offsetof(worr_native_readiness_setting_pair_v1, index) == 0);
static_assert(offsetof(worr_native_readiness_setting_pair_v1, value) == 2);
static_assert(WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT == 15u);

static_assert(std::is_standard_layout_v<
              worr_native_readiness_sideband_telemetry_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_native_readiness_sideband_telemetry_v1>);
static_assert(sizeof(worr_native_readiness_sideband_telemetry_v1) == 160);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       packet_begins) == 0);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       packet_ends) == 8);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       packet_boundary_resets) == 16);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       settings_seen) == 24);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       non_sideband_settings) == 32);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       sideband_begins) == 40);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       fields_accepted) == 48);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       records_committed) == 56);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       records_taken) == 64);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       intervening_resets) == 72);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       dangling_sequences) == 80);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       discarded_records) == 88);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       malformed_order) == 96);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       unsupported_versions) == 104);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       value_range_failures) == 112);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       record_validation_failures) == 120);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       checksum_failures) == 128);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       commit_failures) == 136);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       invalid_arguments) == 144);
static_assert(offsetof(worr_native_readiness_sideband_telemetry_v1,
                       invalid_state) == 152);

static_assert(std::is_standard_layout_v<
              worr_native_readiness_sideband_parser_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_native_readiness_sideband_parser_v1>);
static_assert(sizeof(worr_native_readiness_sideband_parser_v1) == 216);
static_assert(offsetof(worr_native_readiness_sideband_parser_v1,
                       struct_size) == 0);
static_assert(offsetof(worr_native_readiness_sideband_parser_v1,
                       schema_version) == 4);
static_assert(offsetof(worr_native_readiness_sideband_parser_v1, phase) == 6);
static_assert(offsetof(worr_native_readiness_sideband_parser_v1,
                       packet_active) == 8);
static_assert(offsetof(worr_native_readiness_sideband_parser_v1,
                       pending_record) == 16);
static_assert(offsetof(worr_native_readiness_sideband_parser_v1,
                       checksum_low) == 48);
static_assert(offsetof(worr_native_readiness_sideband_parser_v1,
                       telemetry) == 56);

static_assert(WORR_NATIVE_READINESS_SETTING_BEGIN == -31980);
static_assert(WORR_NATIVE_READINESS_SETTING_KIND ==
              WORR_NATIVE_READINESS_SETTING_BEGIN + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_EPOCH_LOW ==
              WORR_NATIVE_READINESS_SETTING_KIND + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_EPOCH_HIGH ==
              WORR_NATIVE_READINESS_SETTING_EPOCH_LOW + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_LOW ==
              WORR_NATIVE_READINESS_SETTING_EPOCH_HIGH + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_HIGH ==
              WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_LOW + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_CAPABILITIES_LOW ==
              WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_HIGH + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_CAPABILITIES_HIGH ==
              WORR_NATIVE_READINESS_SETTING_CAPABILITIES_LOW + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_NONCE_WORD0 ==
              WORR_NATIVE_READINESS_SETTING_CAPABILITIES_HIGH + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_NONCE_WORD1 ==
              WORR_NATIVE_READINESS_SETTING_NONCE_WORD0 + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_NONCE_WORD2 ==
              WORR_NATIVE_READINESS_SETTING_NONCE_WORD1 + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_NONCE_WORD3 ==
              WORR_NATIVE_READINESS_SETTING_NONCE_WORD2 + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_CHECKSUM_LOW ==
              WORR_NATIVE_READINESS_SETTING_NONCE_WORD3 + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_CHECKSUM_HIGH ==
              WORR_NATIVE_READINESS_SETTING_CHECKSUM_LOW + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_COMMIT ==
              WORR_NATIVE_READINESS_SETTING_CHECKSUM_HIGH + 1);
static_assert(WORR_NATIVE_READINESS_SETTING_COMMIT == -31966);

static_assert(static_cast<int>(WORR_LEGACY_COMMAND_SETTING_COMMIT) <
              static_cast<int>(WORR_NATIVE_READINESS_SETTING_BEGIN));
static_assert(WORR_NATIVE_READINESS_SETTING_COMMIT <
              WORR_NET_CAPABILITY_CONFIRM_EPOCH_SETTING);
static_assert(static_cast<int>(WORR_NATIVE_READINESS_SETTING_COMMIT) <
              static_cast<int>(WORR_CONSUMED_CURSOR_SETTING_BEGIN));
static_assert(static_cast<int>(WORR_NATIVE_READINESS_SETTING_COMMIT) <
              static_cast<int>(WORR_DEMO_CLOCK_SETTING_BEGIN));

int main()
{
    return 0;
}
