/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_carrier_ack.h"

/*
 * Common-net private bridge.  This is intentionally absent from inc/: callers
 * must first obtain `repeat_acknowledgement` from an ALREADY_COMMITTED
 * semantic-class packet in the same transaction.  Public code must use a
 * composite admission API so an old retained identity cannot be replayed
 * locally to rearm an ACK.
 */
worr_native_carrier_ack_result_v1
Worr_NativeCarrierAckRefreshObservedRepeatInternalV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    const worr_native_rx_session_v1 *session,
    const worr_native_ack_range_v1 *repeat_acknowledgement);

/* Fail-closed lifecycle fence used when cgame semantic authority is lost. */
worr_native_carrier_ack_result_v1
Worr_NativeCarrierAckRetireSemanticReceiptsInternalV1(
    worr_native_carrier_ack_ledger_v1 *ledger);

/*
 * Event/control semantic admission's transaction-only commit path.  The
 * public retained commit rejects every semantic class so no generic caller
 * can mint ACK authority without proving the corresponding cgame state.
 */
worr_native_rx_result_v1 Worr_NativeCarrierSessionCommitRetainedInternalV1(
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, uint32_t slot_index, uint32_t message_sequence,
    worr_native_carrier_ack_ledger_v1 *ledger);

/* Snapshot semantic admission's class-specific transaction-only commit. */
worr_native_rx_result_v1 Worr_NativeCarrierSessionCommitSnapshotInternalV1(
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, uint32_t slot_index, uint32_t message_sequence,
    worr_native_carrier_ack_ledger_v1 *ledger);
