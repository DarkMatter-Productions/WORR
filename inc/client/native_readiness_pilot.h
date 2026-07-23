/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stddef.h>

#include "common/net/capability.h"
#include "common/net/chan.h"
#include "shared/command_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Default-off FR-10-T04 client readiness pilot.  The owner is process-static
 * and deliberately independent of client_state_t, which is wiped by
 * CL_ClearState during serverdata/map transitions.
 */
void CL_NativeReadinessPilotRegisterCvar(void);

/* Exact public offer requested by the current default-off controls. */
uint32_t CL_NativeReadinessPilotRequestedPublicCapabilities(void);
/* Private/default-off WNB1 request sampled into a separate ROM userinfo key.
 * It never changes the public capability offer. */
bool CL_NativeReadinessPilotRequestedInputBatchV1(void);

/* Called only after Netchan_Setup and q2proto client-context initialization. */
bool CL_NativeReadinessPilotBeginConnection(netchan_t *channel);

/* Must precede every close, replacement, or final disconnect transmit. */
void CL_NativeReadinessPilotBeforeNetchanClose(netchan_t *channel);

/* Map-local lifecycle.  Neither operation resamples the opt-in cvar. */
void CL_NativeReadinessPilotQuiesceMap(void);
void CL_NativeReadinessPilotServerDataReset(void);

/* Canonical packet transaction hooks for the SVC setting carrier. */
void CL_NativeReadinessPilotPacketBegin(void);
void CL_NativeReadinessPilotPacketEnd(void);
bool CL_NativeReadinessPilotObserveSetting(int32_t index, int32_t value);
void CL_NativeReadinessPilotObserveInterveningService(void);

/* Called only after the ordinary public capability core confirms a tuple. */
void CL_NativeReadinessPilotCapabilityConfirmed(
    const worr_net_capability_state_v1 *capability_state);

/*
 * Observational command-shadow input.  Finalized commands are supplied in
 * canonical sequence order; an encoded range is reported only after its
 * legacy MOVE/BATCH bytes have been committed to the outgoing message.  The
 * pilot never changes which legacy commands are authoritative or transmitted.
 */
void CL_NativeReadinessPilotObserveFinalizedCommand(
    uint32_t legacy_command_number,
    const worr_command_id_v1 *command_id,
    const worr_prediction_command_v1 *command);
void CL_NativeReadinessPilotObserveEncodedCommandRange(
    uint32_t first_legacy_command_number,
    uint32_t command_count);

/*
 * Native full-snapshot mode owns the canonical cgame timeline for the complete
 * private readiness epoch.  Legacy rendering remains authoritative, but the
 * independently reconstructed legacy shadow supplies only parity-qualified
 * expectations so the native DATA path is the sole timeline publisher.  Once
 * bound, ownership remains latched through fail-closed DRAIN and transport-hook
 * loss; only an explicit map or connection boundary releases it.
 */
bool CL_NativeReadinessPilotOwnsSnapshotTimeline(void);
void CL_NativeReadinessPilotSnapshotExpectationReady(void);
void CL_NativeReadinessPilotSnapshotExpectationFailed(void);

/*
 * Reports a separately confirmed exhaustive native effect cutover, not merely
 * an ACTIVE event carrier.  The current event capability does not guarantee
 * that every legacy service was transactionally captured and queued, so this
 * remains false and raw presentation stays authoritative.  A future cutover
 * contract may latch ownership through DRAIN and release it only at a whole-
 * stream map/connection/demo boundary.
 */
bool CL_NativeReadinessPilotOwnsEventPresentation(void);

/*
 * Non-mutating scheduler query for an outer client send loop.  True means a
 * zero-unreliable-prefix Netchan_Transmit would give the pilot an immediately
 * due command retry or semantic S2C receipt.  It never reserves a native
 * dispatch or ACK token and remains false for demos and disabled pilots.
 * During a map-quiesced DRAIN it may report only an already-authorized ACK;
 * client DATA remains ineligible.
 */
bool CL_NativeReadinessPilotOutputDue(void);

/* Private readiness carrier settings must not enter demos or GTV streams. */
bool CL_NativeReadinessPilotIsCarrierSetting(int32_t index);

/*
 * Pointer-free production observability for the default-off pilot.  Counters
 * saturate at UINT64_MAX and cover the current connection owner.  The getter
 * is read-only and remains available while the pilot is disabled so runtime
 * acceptance tools can distinguish an inactive pilot from a missing row.
 */
enum {
    CL_NATIVE_READINESS_PILOT_STATUS_ABI_V1 = 1,
};

typedef struct cl_native_readiness_pilot_status_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    uint32_t enabled;
    uint32_t mode;
    uint32_t hooks;
    uint32_t capability_confirmed;
    uint32_t readiness_phase;
    uint32_t official_epoch;
    uint32_t transport_epoch;
    uint32_t protocol;
    uint32_t public_mask;
    uint32_t private_mask;
    uint32_t probe_hold;
    uint32_t cancelled_through_transport_epoch;
    uint64_t challenges;
    uint64_t client_ready_queued;
    uint64_t server_active;
    uint64_t proof_enqueued;
    uint64_t retained;
    uint64_t retained_highwater;
    uint64_t retained_releases;
    uint64_t tx_first_sends;
    uint64_t tx_retries;
    uint64_t tx_handoffs;
    uint64_t ack_carriers;
    uint64_t acknowledged_reliable;
    uint64_t drains;
    uint64_t failures;
    uint64_t cancellation_barriers;
    uint64_t cancelled_transports;
    uint64_t cancelled_command_tx;
    uint64_t cancelled_event_rx;
    uint64_t cancelled_event_receipts;
    uint64_t stale_cancelled_carriers;
    uint64_t stale_cancelled_readiness_records;
    uint32_t last_failure;
    uint32_t reserved1;
} cl_native_readiness_pilot_status_v1;

/* Stable diagnostics for a fail-closed native-event receive rejection. */
enum {
    CL_NATIVE_READINESS_FAILURE_GENERIC = 1u,
    CL_NATIVE_READINESS_FAILURE_EVENT_CARRIER_DECODE = 101u,
    CL_NATIVE_READINESS_FAILURE_EVENT_ENTRY_TYPE = 102u,
    CL_NATIVE_READINESS_FAILURE_EVENT_DATA_COUNT = 103u,
    CL_NATIVE_READINESS_FAILURE_EVENT_DATA_SHAPE = 104u,
    CL_NATIVE_READINESS_FAILURE_EVENT_SESSION_STATE = 105u,
    CL_NATIVE_READINESS_FAILURE_EVENT_TRANSPORT_EPOCH = 106u,
    CL_NATIVE_READINESS_FAILURE_EVENT_READINESS = 107u,
    CL_NATIVE_READINESS_FAILURE_EVENT_ACK = 108u,
    CL_NATIVE_READINESS_FAILURE_EVENT_CLOCK = 109u,
    CL_NATIVE_READINESS_FAILURE_EVENT_ADMISSION = 110u,
};

#if defined(__cplusplus)
static_assert(sizeof(cl_native_readiness_pilot_status_v1) == 232,
              "client native readiness status V1 layout changed");
static_assert(offsetof(cl_native_readiness_pilot_status_v1, challenges) == 56,
              "client native readiness status V1 counter offset changed");
static_assert(offsetof(cl_native_readiness_pilot_status_v1, last_failure) == 224,
              "client native readiness status V1 tail offset changed");
#else
_Static_assert(sizeof(cl_native_readiness_pilot_status_v1) == 232,
               "client native readiness status V1 layout changed");
_Static_assert(offsetof(cl_native_readiness_pilot_status_v1, challenges) == 56,
               "client native readiness status V1 counter offset changed");
_Static_assert(offsetof(cl_native_readiness_pilot_status_v1, last_failure) == 224,
               "client native readiness status V1 tail offset changed");
#endif

bool CL_NativeReadinessPilotGetStatusV1(
    cl_native_readiness_pilot_status_v1 *status_out);

enum {
    CL_NATIVE_INPUT_BATCH_STATUS_ABI_V1 = 1,
};

typedef struct cl_native_input_batch_status_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    uint32_t requested;
    uint32_t confirmed;
    uint32_t enabled;
    uint32_t drained;
    uint32_t official_epoch;
    uint32_t transport_epoch;
    uint32_t received_sequence;
    uint32_t candidate_count;
    uint32_t active_first_sequence;
    uint32_t active_last_sequence;
    uint32_t active_command_count;
    uint32_t active_handoffs;
    uint64_t candidates_collected;
    uint64_t plans;
    uint64_t batches_encoded;
    uint64_t prepare_fallbacks;
    uint64_t first_handoffs;
    uint64_t retry_handoffs;
    uint64_t batches_acknowledged;
    uint64_t commands_acknowledged;
    uint64_t retry_exhaustions;
    uint64_t failures;
} cl_native_input_batch_status_v1;

#if defined(__cplusplus)
static_assert(sizeof(cl_native_input_batch_status_v1) == 136,
              "client native input batch status V1 layout changed");
#else
_Static_assert(sizeof(cl_native_input_batch_status_v1) == 136,
               "client native input batch status V1 layout changed");
#endif

bool CL_NativeReadinessPilotGetInputBatchStatusV1(
    cl_native_input_batch_status_v1 *status_out);

/* Scalar-only snapshot lane diagnostics emitted alongside the base status
 * command; absent receivers still produce a stable zero row. */
void CL_NativeReadinessPilotSnapshotStatus_f(void);

#if defined(WORR_NATIVE_READINESS_PILOT_TESTING)
typedef struct cl_native_readiness_pilot_test_state_s {
    uint32_t transport_epoch;
    uint32_t retained_messages;
    uint32_t retained_payloads;
    uint32_t retired_transport_epoch;
    uint32_t retired_messages;
    uint32_t retired_payloads;
    uint32_t message_sequence_highwater;
    uint32_t selected_send_attempts;
    uint32_t mode;
    uint32_t private_capabilities;
    uint32_t event_rx_occupied;
    uint32_t event_ack_receipts;
    uint32_t retired_event_rx_occupied;
    uint32_t retired_event_ack_receipts;
    uint32_t event_owner_flags;
    uint32_t event_owner_epoch_high_water;
    uint32_t snapshot_epoch;
    uint32_t snapshot_receiver_flags;
    uint32_t snapshot_rx_occupied;
    uint32_t snapshot_ack_receipts;
    uint32_t ack_next_bank;
    uint32_t cancelled_through_transport_epoch;
    uint64_t cancellation_barriers;
    uint64_t cancelled_transports;
    uint64_t cancelled_commands;
    uint64_t cancelled_event_rx;
    uint64_t cancelled_event_receipts;
    uint64_t stale_cancelled_carriers;
    uint64_t stale_cancelled_readiness_records;
    bool hooks_installed;
    bool readiness_committed;
    bool proof_enqueued_once;
    bool carrier_traffic_seen;
    bool event_enabled;
    bool snapshot_enabled;
    bool client_active_confirm_queued;
} cl_native_readiness_pilot_test_state_t;

bool CL_NativeReadinessPilotGetTestState(
    cl_native_readiness_pilot_test_state_t *state_out);
#endif

#ifdef __cplusplus
}
#endif
