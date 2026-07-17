#include "common/net/native_readiness.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<worr_native_readiness_record_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_readiness_record_v1>);
static_assert(sizeof(worr_native_readiness_record_v1) == 32);
static_assert(offsetof(worr_native_readiness_record_v1, struct_size) == 0);
static_assert(offsetof(worr_native_readiness_record_v1, schema_version) == 4);
static_assert(offsetof(worr_native_readiness_record_v1, record_kind) == 6);
static_assert(offsetof(worr_native_readiness_record_v1, transport_epoch) == 8);
static_assert(offsetof(worr_native_readiness_record_v1,
                       negotiated_capabilities) == 12);
static_assert(offsetof(worr_native_readiness_record_v1, readiness_nonce) == 16);
static_assert(offsetof(worr_native_readiness_record_v1, record_checksum) == 24);
static_assert(offsetof(worr_native_readiness_record_v1, snapshot_epoch) == 28);

static_assert(std::is_standard_layout_v<worr_native_readiness_telemetry_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_readiness_telemetry_v1>);
static_assert(sizeof(worr_native_readiness_telemetry_v1) == 120);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       challenges_emitted) == 0);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       challenges_accepted) == 8);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       client_ready_emitted) == 16);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       client_ready_accepted) == 24);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       server_active_emitted) == 32);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       server_active_accepted) == 40);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       exact_duplicates) == 48);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       binding_mismatches) == 56);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       order_failures) == 64);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       invalid_records) == 72);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       deadline_checks) == 80);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       deadline_expirations) == 88);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       clock_regressions) == 96);
static_assert(offsetof(worr_native_readiness_telemetry_v1,
                       epoch_advances) == 104);
static_assert(offsetof(worr_native_readiness_telemetry_v1, failures) == 112);

static_assert(std::is_standard_layout_v<worr_native_readiness_state_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_readiness_state_v1>);
static_assert(sizeof(worr_native_readiness_state_v1) == 200);
static_assert(offsetof(worr_native_readiness_state_v1, struct_size) == 0);
static_assert(offsetof(worr_native_readiness_state_v1, schema_version) == 4);
static_assert(offsetof(worr_native_readiness_state_v1, role) == 6);
static_assert(offsetof(worr_native_readiness_state_v1, phase) == 8);
static_assert(offsetof(worr_native_readiness_state_v1, state_flags) == 10);
static_assert(offsetof(worr_native_readiness_state_v1, transport_epoch) == 12);
static_assert(offsetof(worr_native_readiness_state_v1,
                       negotiated_capabilities) == 16);
static_assert(offsetof(worr_native_readiness_state_v1, snapshot_epoch) == 20);
static_assert(offsetof(worr_native_readiness_state_v1, readiness_nonce) == 24);
static_assert(offsetof(worr_native_readiness_state_v1, nonce_floor) == 32);
static_assert(offsetof(worr_native_readiness_state_v1, generation) == 40);
static_assert(offsetof(worr_native_readiness_state_v1, timeout_ticks) == 48);
static_assert(offsetof(worr_native_readiness_state_v1, phase_start_tick) == 56);
static_assert(offsetof(worr_native_readiness_state_v1, deadline_tick) == 64);
static_assert(offsetof(worr_native_readiness_state_v1, last_tick) == 72);
static_assert(offsetof(worr_native_readiness_state_v1, telemetry) == 80);

static_assert(WORR_NATIVE_READINESS_ROLE_SERVER == 1);
static_assert(WORR_NATIVE_READINESS_ROLE_CLIENT == 2);
static_assert(WORR_NATIVE_READINESS_PHASE_RESET == 0);
static_assert(WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY == 1);
static_assert(WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE == 2);
static_assert(WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE == 3);
static_assert(WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE == 4);
static_assert(WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE == 5);
static_assert(WORR_NATIVE_READINESS_PHASE_FAILED == 6);
static_assert(
    WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM == 7);
static_assert(WORR_NATIVE_READINESS_RECORD_CHALLENGE == 1);
static_assert(WORR_NATIVE_READINESS_RECORD_CLIENT_READY == 2);
static_assert(WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE == 3);
static_assert(WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM == 4);
static_assert(WORR_NATIVE_READINESS_OK == 0);
static_assert(WORR_NATIVE_READINESS_EXACT_DUPLICATE == 1);
static_assert(WORR_NATIVE_READINESS_INVALID_ARGUMENT == 2);
static_assert(WORR_NATIVE_READINESS_INVALID_STATE == 3);
static_assert(WORR_NATIVE_READINESS_INVALID_RECORD == 4);
static_assert(WORR_NATIVE_READINESS_WRONG_ROLE == 5);
static_assert(WORR_NATIVE_READINESS_WRONG_ORDER == 6);
static_assert(WORR_NATIVE_READINESS_BINDING_MISMATCH == 7);
static_assert(WORR_NATIVE_READINESS_CLOCK_REGRESSION == 8);
static_assert(WORR_NATIVE_READINESS_DEADLINE_EXPIRED == 9);
static_assert(WORR_NATIVE_READINESS_DEADLINE_OVERFLOW == 10);
static_assert(WORR_NATIVE_READINESS_EPOCH_NOT_NEWER == 11);
static_assert(WORR_NATIVE_READINESS_NONCE_NOT_NEWER == 12);
static_assert(WORR_NATIVE_READINESS_NONCE_EXHAUSTED == 13);
static_assert(WORR_NATIVE_READINESS_GENERATION_EXHAUSTED == 14);
static_assert(WORR_NATIVE_READINESS_EPOCH_EXHAUSTED == 15);
static_assert(WORR_NATIVE_READINESS_STATE_INITIALIZED == 1);
static_assert(WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1 == UINT32_C(0x40));
static_assert(WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK == UINT32_C(0x50));
static_assert(WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK == UINT32_C(0x53));
static_assert(WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK == UINT32_C(0x73));
static_assert(WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK == UINT32_C(0x57));
static_assert((WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK |
               WORR_NET_CAP_CANONICAL_SNAPSHOT_V2) == UINT32_C(0x77));

int main()
{
    return 0;
}
