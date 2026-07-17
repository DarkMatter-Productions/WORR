/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"
#include "client/cgame_event_runtime.h"
#include "client/native_readiness_pilot.h"
#include "client/snapshot_shadow.h"

#include "common/net/native_carrier.h"
#include "common/net/native_carrier_ack.h"
#include "common/net/native_carrier_mixed.h"
#include "common/net/native_carrier_session.h"
#include "common/net/native_command_shadow.h"
#include "common/net/native_event_admission.h"
#include "common/net/native_readiness.h"
#include "common/net/native_readiness_sideband.h"
#include "common/net/native_session.h"
#include "common/net/native_snapshot_receiver.h"
#include "common/q2proto_shared.h"
#include "q2proto/q2proto.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

namespace {

constexpr uint32_t kCommandPrivateCapabilities =
    WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK;
constexpr uint32_t kEventPrivateCapabilities =
    WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
constexpr uint32_t kSnapshotPrivateCapabilities =
    WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK;
/* The server starts CHALLENGE after bootstrap.  This timeout covers the
 * bounded private readiness exchange only; authoritative legacy continues
 * independently if the default-off pilot fails closed. */
constexpr uint64_t kReadinessTimeoutMilliseconds = UINT64_C(10000);
constexpr uint32_t kResendIntervalMilliseconds = UINT32_C(100);
constexpr uint32_t kAckRetryIntervalMilliseconds = UINT32_C(100);
constexpr uint32_t kRxFragmentTimeoutMilliseconds = UINT32_C(1000);
constexpr uint32_t kRxCompleteTimeoutMilliseconds = UINT32_C(1000);
constexpr uint16_t kTxSlotCapacity = 1;
constexpr uint16_t kEventRxSlotCapacity =
    WORR_NATIVE_SESSION_MAX_RX_SLOTS;
constexpr uint8_t kAckProactiveHandoffs = 3;
constexpr uint8_t kProofPriority = 0;
constexpr uint16_t kCommandPayloadBytes =
    WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES;
constexpr uint16_t kCommandDatagramBytes =
    WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES + kCommandPayloadBytes;
constexpr uint16_t kCommandCarrierOverhead =
    WORR_NATIVE_CARRIER_WIRE_ENTRY_HEADER_BYTES +
    kCommandDatagramBytes + WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES;
constexpr size_t kEncodedClientReadyBytes =
    WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT * 5u;
constexpr size_t kScratchBytes = 128u;
constexpr uint32_t kEventPayloadStride =
    WORR_NATIVE_CODEC_MAX_EVENT_ENCODED_BYTES;
constexpr size_t kEventPayloadArenaBytes =
    static_cast<size_t>(kEventRxSlotCapacity) * kEventPayloadStride;

static_assert(WORR_NET_CAP_LEGACY_STAGE_MASK == UINT32_C(3));
static_assert(kCommandPrivateCapabilities == UINT32_C(0x53));
static_assert(kEventPrivateCapabilities == UINT32_C(0x73));
static_assert(kSnapshotPrivateCapabilities == UINT32_C(0x57));
static_assert(kEncodedClientReadyBytes == 75u);
static_assert(kCommandPayloadBytes == 110u);
static_assert(kCommandDatagramBytes == 166u);
static_assert(kCommandCarrierOverhead == 206u);
static_assert(818u + kCommandCarrierOverhead == 1024u);
static_assert(819u + kCommandCarrierOverhead > 1024u);
static_assert(kEventPayloadStride == 192u);
static_assert(kEventPayloadArenaBytes == 3072u);
static_assert((CMD_BACKUP & (CMD_BACKUP - 1u)) == 0u);

enum class pilot_mode_t : uint32_t {
    arming = 1,
    active = 2,
    drain = 3,
};

enum class tx_packet_kind_t : uint32_t {
    none = 0,
    command_mixed = 1,
    ack_current = 2,
    ack_retired = 3,
};

enum class event_ack_bank_t : uint32_t {
    current = 1,
    retired = 2,
};

struct checked_tick_extender_t {
    bool initialized{};
    uint32_t last_raw{};
    uint64_t extended{};
};

struct command_ring_entry_t {
    bool valid{};
    uint32_t legacy_command_number{};
    worr_command_record_v1 record{};
};

struct snapshot_receiver_deleter_t {
    void operator()(worr_native_snapshot_receiver_v1 *receiver) const
    {
        std::free(receiver);
    }
};

using snapshot_receiver_ptr_t =
    std::unique_ptr<worr_native_snapshot_receiver_v1,
                    snapshot_receiver_deleter_t>;

struct client_native_readiness_telemetry_t {
    uint64_t challenges{};
    uint64_t client_ready_queued{};
    uint64_t server_active{};
    uint64_t proof_enqueued{};
    uint64_t retained_highwater{};
    uint64_t retained_releases{};
    uint64_t tx_first_sends{};
    uint64_t tx_retries{};
    uint64_t tx_handoffs{};
    uint64_t ack_carriers{};
    uint64_t acknowledged_reliable{};
    uint64_t drains{};
    uint64_t failures{};
    uint64_t cancellation_barriers{};
    uint64_t cancelled_transports{};
    uint64_t cancelled_commands{};
    uint64_t cancelled_event_rx{};
    uint64_t cancelled_event_receipts{};
    uint64_t cancelled_snapshot_rx{};
    uint64_t cancelled_snapshot_receipts{};
    uint64_t stale_cancelled_carriers{};
    uint64_t stale_cancelled_readiness_records{};
    uint32_t last_failure{};
};

struct client_native_readiness_pilot_t {
    bool enabled{};
    bool event_enabled{};
    bool snapshot_enabled{};
    bool snapshot_timeline_owned{};
    bool tx_hook_registered{};
    bool rx_hook_registered{};
    bool capability_confirmed{};
    bool map_quiesced{};
    bool readiness_initialized{};
    bool session_initialized{};
    bool retired_session_initialized{};
    bool builder_initialized{};
    bool last_enqueued_command_valid{};
    bool readiness_committed{};
    bool carrier_traffic_seen{};
    bool client_active_confirm_queued{};
    pilot_mode_t mode{pilot_mode_t::arming};
    tx_packet_kind_t tx_packet_kind{tx_packet_kind_t::none};
    event_ack_bank_t ack_next_bank{event_ack_bank_t::current};
    netchan_t *channel{};
    uint64_t connection_owner_id{};
    uint32_t private_capabilities{};
    uint32_t cancelled_through_transport_epoch{};
    uint64_t next_completion_token{};
    uint64_t active_completion_token{};
    uint32_t prepared_application_bytes{};
    std::array<byte, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES>
        prepared_application{};
    checked_tick_extender_t clock{};
    worr_native_readiness_sideband_parser_v1 parser{};
    worr_native_readiness_state_v1 readiness{};
    worr_native_session_binding_v1 binding{};
    worr_native_tx_session_v1 tx{};
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity> tx_slots{};
    worr_native_carrier_tx_gate_v1 tx_gate{};
    worr_native_carrier_dispatch_v1 dispatch{};
    worr_native_command_shadow_payload_registry_v1 payload_registry{};
    worr_native_tx_session_v1 retired_tx{};
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity> retired_tx_slots{};
    worr_native_command_shadow_payload_registry_v1 retired_payload_registry{};
    worr_native_carrier_mixed_token_v1 mixed_token{};
    worr_native_carrier_ack_emit_token_v1 ack_emit_token{};
    worr_native_rx_session_v1 event_rx{};
    std::array<worr_native_rx_slot_v1, kEventRxSlotCapacity>
        event_rx_slots{};
    std::array<byte, kEventPayloadArenaBytes> event_payload_arena{};
    worr_native_carrier_ack_ledger_v1 event_ack_ledger{};
    worr_native_rx_session_v1 retired_event_rx{};
    std::array<worr_native_rx_slot_v1, kEventRxSlotCapacity>
        retired_event_rx_slots{};
    std::array<byte, kEventPayloadArenaBytes>
        retired_event_payload_arena{};
    worr_native_carrier_ack_ledger_v1 retired_event_ack_ledger{};
    snapshot_receiver_ptr_t snapshot_receiver{};
    snapshot_receiver_ptr_t retired_snapshot_receiver{};
    worr_event_stream_owner_v1 event_owner{};
    worr_native_event_consumer_v1 event_consumer{};
    worr_native_command_shadow_builder_v1 builder{};
    worr_command_id_v1 last_enqueued_command_id{};
    std::array<command_ring_entry_t, CMD_BACKUP> command_ring{};
};

struct client_native_cancellation_stage_t {
    bool current_initialized{};
    bool retired_initialized{};
    uint32_t cancelled_through_transport_epoch{};
    uint32_t cancelled_transports{};
    uint32_t cancelled_commands{};
    uint32_t cancelled_event_rx{};
    uint32_t cancelled_event_receipts{};
    uint32_t cancelled_snapshot_rx{};
    uint32_t cancelled_snapshot_receipts{};
    worr_native_tx_session_v1 tx{};
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity> tx_slots{};
    worr_native_carrier_tx_gate_v1 tx_gate{};
    worr_native_carrier_dispatch_v1 dispatch{};
    worr_native_command_shadow_payload_registry_v1 payload_registry{};
    worr_native_rx_session_v1 event_rx{};
    std::array<worr_native_rx_slot_v1, kEventRxSlotCapacity>
        event_rx_slots{};
    worr_native_carrier_ack_ledger_v1 event_ack_ledger{};
    worr_native_tx_session_v1 retired_tx{};
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity>
        retired_tx_slots{};
    worr_native_command_shadow_payload_registry_v1
        retired_payload_registry{};
    worr_native_rx_session_v1 retired_event_rx{};
    std::array<worr_native_rx_slot_v1, kEventRxSlotCapacity>
        retired_event_rx_slots{};
    worr_native_carrier_ack_ledger_v1 retired_event_ack_ledger{};
    snapshot_receiver_ptr_t snapshot_receiver{};
    snapshot_receiver_ptr_t retired_snapshot_receiver{};
};

cvar_t *cl_worr_native_shadow{};
cvar_t *cl_worr_native_event_shadow{};
cvar_t *cl_worr_native_snapshot_shadow{};
cvar_t *cl_worr_native_shadow_probe_hold{};
client_native_readiness_pilot_t pilot{};
client_native_readiness_telemetry_t telemetry{};
uint64_t next_connection_owner_id{1};

netchan_app_tx_prepare_result_t pilot_tx_prepare(
    void *, const netchan_app_tx_prepare_info_v1_t *, const byte *, byte *,
    netchan_app_tx_prepare_output_v1_t *);
void pilot_tx_completion(void *,
                         const netchan_app_tx_completion_info_v1_t *,
                         const byte *);
netchan_app_rx_result_t pilot_rx(
    void *, const netchan_app_rx_info_v1_t *, const byte *,
    netchan_app_rx_output_v1_t *);

bool extend_tick(checked_tick_extender_t &clock, uint32_t raw,
                 uint64_t &extended_out)
{
    if (!clock.initialized) {
        clock.initialized = true;
        clock.last_raw = raw;
        clock.extended = raw;
        extended_out = clock.extended;
        return true;
    }

    const uint32_t delta = raw - clock.last_raw;
    /* The half-range rule distinguishes a genuine 32-bit wrap from a clock
     * regression without guessing how many wraps elapsed. */
    if (delta > UINT32_MAX / 2u ||
        clock.extended > UINT64_MAX - delta) {
        return false;
    }

    clock.last_raw = raw;
    clock.extended += delta;
    extended_out = clock.extended;
    return true;
}

bool current_tick(uint64_t &tick_out)
{
    return extend_tick(pilot.clock, static_cast<uint32_t>(com_localTime),
                       tick_out);
}

bool projected_tick(uint64_t &tick_out)
{
    checked_tick_extender_t projected = pilot.clock;
    return extend_tick(projected, static_cast<uint32_t>(com_localTime),
                       tick_out);
}

bool allocate_connection_owner(uint64_t &owner_out)
{
    if (next_connection_owner_id == 0)
        return false;
    owner_out = next_connection_owner_id;
    if (next_connection_owner_id == UINT64_MAX)
        next_connection_owner_id = 0;
    else
        ++next_connection_owner_id;
    return true;
}

void counter_increment(uint64_t &counter)
{
    if (counter != UINT64_MAX)
        ++counter;
}

void counter_add(uint64_t &counter, uint64_t amount)
{
    if (amount > UINT64_MAX - counter)
        counter = UINT64_MAX;
    else
        counter += amount;
}

snapshot_receiver_ptr_t allocate_snapshot_receiver()
{
    return snapshot_receiver_ptr_t{
        static_cast<worr_native_snapshot_receiver_v1 *>(
            std::calloc(1, sizeof(worr_native_snapshot_receiver_v1)))};
}

snapshot_receiver_ptr_t clone_snapshot_receiver(
    const worr_native_snapshot_receiver_v1 *source)
{
    if (!source ||
        !Worr_NativeSnapshotReceiverValidateV1(source)) {
        return {};
    }
    auto clone = allocate_snapshot_receiver();
    if (!clone)
        return {};
    std::memcpy(clone.get(), source, sizeof(*source));
    clone->scratch.snapshot = &clone->decoded_snapshot;
    clone->scratch.player = &clone->decoded_player;
    clone->scratch.entities = clone->decoded_entities;
    clone->scratch.area_bytes = clone->decoded_area;
    clone->scratch.event_refs = clone->decoded_events;
    if (!Worr_NativeSnapshotReceiverValidateV1(clone.get()))
        return {};
    return clone;
}

bool semantic_ack_enabled()
{
    return pilot.event_enabled || pilot.snapshot_enabled;
}

worr_native_carrier_ack_ledger_v1 *current_semantic_ack_ledger()
{
    if (pilot.event_enabled)
        return &pilot.event_ack_ledger;
    if (pilot.snapshot_enabled && pilot.snapshot_receiver)
        return &pilot.snapshot_receiver->ack_ledger;
    return nullptr;
}

worr_native_carrier_ack_ledger_v1 *retired_semantic_ack_ledger()
{
    if (!pilot.retired_session_initialized)
        return nullptr;
    if (pilot.event_enabled)
        return &pilot.retired_event_ack_ledger;
    if (pilot.snapshot_enabled && pilot.retired_snapshot_receiver)
        return &pilot.retired_snapshot_receiver->ack_ledger;
    return nullptr;
}

bool cancel_staged_command_bank(
    worr_native_tx_session_v1 &tx,
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity> &slots,
    worr_native_command_shadow_payload_registry_v1 &registry,
    uint32_t &cancelled_out)
{
    if (!Worr_NativeTxSessionValidateV1(
            &tx, slots.data(), kTxSlotCapacity) ||
        !Worr_NativeCommandShadowPayloadRegistryValidateV1(&registry) ||
        registry.occupied_count != tx.retained_count) {
        return false;
    }

    std::array<uint32_t, kTxSlotCapacity> handles{};
    uint16_t handle_count = 0;
    for (const auto &slot : slots) {
        if (slot.state_flags != 0)
            handles[handle_count++] = slot.payload_handle;
    }

    uint32_t cancelled = 0;
    if (Worr_NativeTxSessionCancelRetainedV1(
            &tx, slots.data(), kTxSlotCapacity, &cancelled) !=
            WORR_NATIVE_TX_CANCELLED ||
        cancelled != handle_count) {
        return false;
    }
    for (uint16_t index = 0; index < handle_count; ++index) {
        if (Worr_NativeCommandShadowPayloadReleaseV1(
                &registry, handles[index]) !=
            WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RELEASED) {
            return false;
        }
    }
    if (!Worr_NativeTxSessionValidateV1(
            &tx, slots.data(), kTxSlotCapacity) ||
        !Worr_NativeCommandShadowPayloadRegistryValidateV1(&registry) ||
        tx.retained_count != 0 || registry.occupied_count != 0) {
        return false;
    }
    cancelled_out = cancelled;
    return true;
}

bool cancel_staged_event_bank(
    worr_native_rx_session_v1 &rx,
    std::array<worr_native_rx_slot_v1, kEventRxSlotCapacity> &slots,
    worr_native_carrier_ack_ledger_v1 &ledger,
    uint32_t &cancelled_rx_out, uint32_t &cancelled_receipts_out)
{
    if (!Worr_NativeRxSessionValidateV1(
            &rx, slots.data(), kEventRxSlotCapacity) ||
        !Worr_NativeCarrierAckLedgerValidateV1(&ledger)) {
        return false;
    }
    worr_native_rx_cancel_report_v1 rx_report{};
    uint32_t receipts = 0;
    if (Worr_NativeRxSessionCancelPendingV1(
            &rx, slots.data(), kEventRxSlotCapacity, &rx_report) !=
            WORR_NATIVE_RX_CANCELLED ||
        Worr_NativeCarrierAckCancelAllV1(&ledger, &receipts) !=
            WORR_NATIVE_CARRIER_ACK_OK ||
        !Worr_NativeRxSessionValidateV1(
            &rx, slots.data(), kEventRxSlotCapacity) ||
        !Worr_NativeCarrierAckLedgerValidateV1(&ledger) ||
        rx.occupied_count != 0 || ledger.receipt_count != 0) {
        return false;
    }
    cancelled_rx_out = rx_report.incomplete_messages +
                       rx_report.complete_messages;
    cancelled_receipts_out = receipts;
    return true;
}

bool stage_prior_native_epoch_cancellation(
    uint32_t new_transport_epoch,
    client_native_cancellation_stage_t &stage)
{
    if (new_transport_epoch == 0 ||
        new_transport_epoch <=
            pilot.cancelled_through_transport_epoch ||
        pilot.tx_packet_kind != tx_packet_kind_t::none ||
        pilot.active_completion_token != 0 ||
        (pilot.retired_session_initialized &&
         !pilot.session_initialized)) {
        return false;
    }

    stage = {};
    stage.cancelled_through_transport_epoch =
        pilot.cancelled_through_transport_epoch;
    if (pilot.readiness_initialized) {
        if (!Worr_NativeReadinessStateValidateV1(&pilot.readiness) ||
            pilot.readiness.transport_epoch >= new_transport_epoch) {
            return false;
        }
        if (stage.cancelled_through_transport_epoch <
            pilot.readiness.transport_epoch) {
            stage.cancelled_through_transport_epoch =
                pilot.readiness.transport_epoch;
        }
    }
    if (pilot.session_initialized) {
        if (!Worr_NativeSessionBindingValidateV1(&pilot.binding) ||
            pilot.binding.transport_epoch >= new_transport_epoch ||
            !Worr_NativeCarrierTxGateValidateV1(&pilot.tx_gate) ||
            pilot.tx.transport_epoch != pilot.binding.transport_epoch ||
            pilot.tx_gate.transport_epoch !=
                pilot.binding.transport_epoch ||
            (pilot.tx_gate.state_flags &
             WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0) {
            return false;
        }
        stage.current_initialized = true;
        stage.tx = pilot.tx;
        stage.tx_slots = pilot.tx_slots;
        stage.tx_gate = pilot.tx_gate;
        stage.dispatch = pilot.dispatch;
        stage.payload_registry = pilot.payload_registry;
        if ((stage.tx_gate.state_flags &
             WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
            Worr_NativeCarrierSessionDispatchAbortV1(
                &stage.tx_gate, &stage.dispatch) !=
                WORR_NATIVE_CARRIER_SESSION_OK) {
            return false;
        }
        if ((stage.tx_gate.state_flags &
             WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 ||
            !cancel_staged_command_bank(
                stage.tx, stage.tx_slots, stage.payload_registry,
                stage.cancelled_commands)) {
            return false;
        }
        if (pilot.event_enabled) {
            if (pilot.event_rx.transport_epoch !=
                    pilot.binding.transport_epoch ||
                pilot.event_ack_ledger.transport_epoch !=
                    pilot.binding.transport_epoch) {
                return false;
            }
            stage.event_rx = pilot.event_rx;
            stage.event_rx_slots = pilot.event_rx_slots;
            stage.event_ack_ledger = pilot.event_ack_ledger;
            uint32_t rx = 0;
            uint32_t receipts = 0;
            if (!cancel_staged_event_bank(
                    stage.event_rx, stage.event_rx_slots,
                    stage.event_ack_ledger, rx, receipts)) {
                return false;
            }
            stage.cancelled_event_rx += rx;
            stage.cancelled_event_receipts += receipts;
        } else if (pilot.snapshot_enabled) {
            if (!pilot.snapshot_receiver ||
                !Worr_NativeSnapshotReceiverValidateV1(
                    pilot.snapshot_receiver.get()) ||
                pilot.snapshot_receiver->binding.transport_epoch !=
                    pilot.binding.transport_epoch) {
                return false;
            }
            auto receiver = clone_snapshot_receiver(
                pilot.snapshot_receiver.get());
            if (!receiver)
                return false;
            uint32_t messages = 0;
            uint32_t receipts = 0;
            if (Worr_NativeSnapshotReceiverCancelV1(
                    receiver.get(), &messages, &receipts) !=
                    WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT ||
                !Worr_NativeSnapshotReceiverValidateV1(
                    receiver.get())) {
                return false;
            }
            stage.snapshot_receiver = std::move(receiver);
            stage.cancelled_snapshot_rx += messages;
            stage.cancelled_snapshot_receipts += receipts;
        }
        stage.cancelled_through_transport_epoch =
            pilot.binding.transport_epoch;
        ++stage.cancelled_transports;
    }

    if (pilot.retired_session_initialized) {
        if (pilot.retired_tx.transport_epoch >= new_transport_epoch ||
            (pilot.session_initialized &&
             pilot.retired_tx.transport_epoch >=
                 pilot.tx.transport_epoch)) {
            return false;
        }
        stage.retired_initialized = true;
        stage.retired_tx = pilot.retired_tx;
        stage.retired_tx_slots = pilot.retired_tx_slots;
        stage.retired_payload_registry =
            pilot.retired_payload_registry;
        uint32_t commands = 0;
        if (!cancel_staged_command_bank(
                stage.retired_tx, stage.retired_tx_slots,
                stage.retired_payload_registry, commands)) {
            return false;
        }
        stage.cancelled_commands += commands;
        if (pilot.event_enabled) {
            if (pilot.retired_event_rx.transport_epoch !=
                    pilot.retired_tx.transport_epoch ||
                pilot.retired_event_ack_ledger.transport_epoch !=
                    pilot.retired_tx.transport_epoch) {
                return false;
            }
            stage.retired_event_rx = pilot.retired_event_rx;
            stage.retired_event_rx_slots =
                pilot.retired_event_rx_slots;
            stage.retired_event_ack_ledger =
                pilot.retired_event_ack_ledger;
            uint32_t rx = 0;
            uint32_t receipts = 0;
            if (!cancel_staged_event_bank(
                    stage.retired_event_rx,
                    stage.retired_event_rx_slots,
                    stage.retired_event_ack_ledger, rx, receipts)) {
                return false;
            }
            stage.cancelled_event_rx += rx;
            stage.cancelled_event_receipts += receipts;
        } else if (pilot.snapshot_enabled) {
            if (!pilot.retired_snapshot_receiver ||
                !Worr_NativeSnapshotReceiverValidateV1(
                    pilot.retired_snapshot_receiver.get()) ||
                pilot.retired_snapshot_receiver->binding
                        .transport_epoch !=
                    pilot.retired_tx.transport_epoch) {
                return false;
            }
            auto receiver = clone_snapshot_receiver(
                pilot.retired_snapshot_receiver.get());
            if (!receiver)
                return false;
            uint32_t messages = 0;
            uint32_t receipts = 0;
            if (Worr_NativeSnapshotReceiverCancelV1(
                    receiver.get(), &messages, &receipts) !=
                    WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT ||
                !Worr_NativeSnapshotReceiverValidateV1(
                    receiver.get())) {
                return false;
            }
            stage.retired_snapshot_receiver =
                std::move(receiver);
            stage.cancelled_snapshot_rx += messages;
            stage.cancelled_snapshot_receipts += receipts;
        }
        if (stage.cancelled_through_transport_epoch <
            pilot.retired_tx.transport_epoch) {
            stage.cancelled_through_transport_epoch =
                pilot.retired_tx.transport_epoch;
        }
        ++stage.cancelled_transports;
    }
    return stage.cancelled_through_transport_epoch <
           new_transport_epoch;
}

void commit_prior_native_epoch_cancellation(
    const client_native_cancellation_stage_t &stage)
{
    /* Publish the prevalidated terminal dispositions only after CLIENT_READY
     * is durably resident in the reliable queue.  The enclosing transport
     * storage can then be reset without manufacturing a second retired bank. */
    if (stage.current_initialized) {
        pilot.tx = stage.tx;
        pilot.tx_slots = stage.tx_slots;
        pilot.tx_gate = stage.tx_gate;
        pilot.dispatch = stage.dispatch;
        pilot.payload_registry = stage.payload_registry;
        if (pilot.event_enabled) {
            pilot.event_rx = stage.event_rx;
            pilot.event_rx_slots = stage.event_rx_slots;
            pilot.event_ack_ledger = stage.event_ack_ledger;
        }
    }
    if (stage.retired_initialized) {
        pilot.retired_tx = stage.retired_tx;
        pilot.retired_tx_slots = stage.retired_tx_slots;
        pilot.retired_payload_registry =
            stage.retired_payload_registry;
        if (pilot.event_enabled) {
            pilot.retired_event_rx = stage.retired_event_rx;
            pilot.retired_event_rx_slots =
                stage.retired_event_rx_slots;
            pilot.retired_event_ack_ledger =
                stage.retired_event_ack_ledger;
        }
    }

    pilot.binding = {};
    pilot.tx = {};
    pilot.tx_slots = {};
    pilot.tx_gate = {};
    pilot.dispatch = {};
    pilot.payload_registry = {};
    pilot.retired_tx = {};
    pilot.retired_tx_slots = {};
    pilot.retired_payload_registry = {};
    pilot.event_rx = {};
    pilot.event_rx_slots = {};
    pilot.event_payload_arena = {};
    pilot.event_ack_ledger = {};
    pilot.retired_event_rx = {};
    pilot.retired_event_rx_slots = {};
    pilot.retired_event_payload_arena = {};
    pilot.retired_event_ack_ledger = {};
    pilot.snapshot_receiver.reset();
    pilot.retired_snapshot_receiver.reset();
    pilot.session_initialized = false;
    pilot.retired_session_initialized = false;
    pilot.last_enqueued_command_valid = false;
    pilot.last_enqueued_command_id = {};
    pilot.tx_packet_kind = tx_packet_kind_t::none;
    pilot.ack_next_bank = event_ack_bank_t::current;
    pilot.active_completion_token = 0;
    pilot.prepared_application_bytes = 0;
    pilot.prepared_application = {};
    pilot.mixed_token = {};
    pilot.ack_emit_token = {};
    pilot.cancelled_through_transport_epoch =
        stage.cancelled_through_transport_epoch;
    counter_increment(telemetry.cancellation_barriers);
    counter_add(telemetry.cancelled_transports,
                stage.cancelled_transports);
    counter_add(telemetry.cancelled_commands,
                stage.cancelled_commands);
    counter_add(telemetry.cancelled_event_rx,
                stage.cancelled_event_rx);
    counter_add(telemetry.cancelled_event_receipts,
                stage.cancelled_event_receipts);
    counter_add(telemetry.cancelled_snapshot_rx,
                stage.cancelled_snapshot_rx);
    counter_add(telemetry.cancelled_snapshot_receipts,
                stage.cancelled_snapshot_receipts);
}

void detach_owned_hooks()
{
    netchan_t *const channel = pilot.channel;
    if (channel && pilot.rx_hook_registered &&
        channel->app_rx == pilot_rx &&
        channel->app_rx_opaque == &pilot) {
        (void)Netchan_SetApplicationRxHook(channel, nullptr, nullptr);
    }
    if (channel && pilot.tx_hook_registered &&
        channel->app_tx_prepare == pilot_tx_prepare &&
        channel->app_tx_completion == pilot_tx_completion &&
        channel->app_tx_opaque == &pilot) {
        (void)Netchan_SetApplicationTxHook(
            channel, nullptr, nullptr, nullptr);
    }
    pilot.rx_hook_registered = false;
    pilot.tx_hook_registered = false;
}

void disable_pilot()
{
    const bool reset_event_connection = pilot.event_enabled;
    detach_owned_hooks();
    pilot = {};
    if (reset_event_connection)
        (void)CL_CGameEventRuntimeResetConnection();
}

bool hooks_attached_exact()
{
    return pilot.enabled && !cls.demo.playback && !cls.demo.seeking &&
           pilot.channel && pilot.channel == &cls.netchan &&
           pilot.channel->type == NETCHAN_NEW &&
           pilot.tx_hook_registered && pilot.rx_hook_registered &&
           pilot.channel->app_tx_prepare == pilot_tx_prepare &&
           pilot.channel->app_tx_completion == pilot_tx_completion &&
           pilot.channel->app_tx_opaque == &pilot &&
           pilot.channel->app_rx == pilot_rx &&
           pilot.channel->app_rx_opaque == &pilot;
}

void enter_drain(bool diagnostic_failure = true);

void detach_transport_fail_closed()
{
    if (!pilot.enabled)
        return;

    /*
     * Once a native snapshot epoch owns the cgame timeline, transport loss
     * cannot authorize a mid-epoch return to legacy publication.  Quarantine
     * the semantic receiver, abandon any non-pending dispatch, and detach
     * only hooks that are still ours.  The ownership latch survives until an
     * explicit map or connection boundary.
     */
    enter_drain(true);
    detach_owned_hooks();
}

bool live_pilot()
{
    if (!pilot.enabled)
        return false;
    if (cls.demo.playback || cls.demo.seeking) {
        /* Native carrier traffic is deliberately absent from demo paths. */
        if (pilot.snapshot_timeline_owned && !pilot.map_quiesced)
            detach_transport_fail_closed();
        else
            disable_pilot();
        return false;
    }
    if (!hooks_attached_exact()) {
        if (pilot.snapshot_timeline_owned && !pilot.map_quiesced)
            detach_transport_fail_closed();
        else
            disable_pilot();
        return false;
    }
    return true;
}

void enter_drain(bool diagnostic_failure)
{
    if (!pilot.enabled)
        return;
    if (diagnostic_failure && pilot.mode != pilot_mode_t::drain)
        counter_increment(telemetry.drains);
    const bool first_entry = pilot.mode != pilot_mode_t::drain;
    pilot.mode = pilot_mode_t::drain;

    if (first_entry && pilot.event_enabled) {
        if (pilot.event_owner.epoch_high_water != 0)
            (void)Worr_EventStreamOwnerRequireResyncV1(
                &pilot.event_owner);
        (void)CL_CGameEventRuntimeQuiesceAuthority();
    } else if (first_entry && diagnostic_failure &&
               pilot.snapshot_enabled && pilot.snapshot_receiver) {
        (void)Worr_NativeSnapshotReceiverQuarantineV1(
            pilot.snapshot_receiver.get());
    }

    /* No callback is re-entrant on this netchan.  A non-pending dispatch can
     * therefore be abandoned immediately; a pending accepted outcome stays
     * frozen and DRAIN will never select further DATA. */
    if (pilot.session_initialized &&
        (pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
        (pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) == 0) {
        (void)Worr_NativeCarrierSessionDispatchAbortV1(
            &pilot.tx_gate, &pilot.dispatch);
    }
}

bool enter_map_boundary()
{
    if (!pilot.enabled)
        return false;

    /*
     * This is the first boundary allowed to release snapshot timeline
     * ownership.  If transport ownership was already lost, finish disabling
     * the pilot without disturbing a foreign replacement hook.
     */
    pilot.snapshot_timeline_owned = false;
    pilot.map_quiesced = true;
    enter_drain(false);
    if (!hooks_attached_exact()) {
        disable_pilot();
        return false;
    }
    return true;
}

void pilot_failure()
{
    counter_increment(telemetry.failures);
    telemetry.last_failure = 1;
    if (pilot.readiness_committed || pilot.carrier_traffic_seen)
        enter_drain(true);
    else
        disable_pilot();
}

bool capability_is_exact_legacy_confirmation(
    const worr_net_capability_state_v1 *state)
{
    return state && Worr_NetCapabilityStateValidateV1(state) &&
           state->phase == WORR_NET_CAPABILITY_CONFIRMED &&
           state->offered == WORR_NET_CAP_LEGACY_STAGE_MASK &&
           state->supported == WORR_NET_CAP_LEGACY_STAGE_MASK &&
           state->peer_supported == WORR_NET_CAP_LEGACY_STAGE_MASK &&
           state->negotiated == WORR_NET_CAP_LEGACY_STAGE_MASK;
}

bool queue_readiness_record(
    const worr_native_readiness_record_v1 &record)
{
    if (record.record_kind != WORR_NATIVE_READINESS_RECORD_CLIENT_READY &&
        record.record_kind !=
            WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM) {
        return false;
    }
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT> pairs{};
    if (!Worr_NativeReadinessSidebandEncodeV1(
            &record, pairs.data(),
            static_cast<uint32_t>(pairs.size()))) {
        return false;
    }

    std::array<byte, kScratchBytes> bytes{};
    sizebuf_t scratch{};
    SZ_InitWrite(&scratch, bytes.data(), bytes.size());
    q2protoio_ioarg_t io{};
    io.sz_write = &scratch;
    io.max_msg_len = scratch.maxsize;

    q2proto_clc_message_t message{};
    message.type = Q2P_CLC_SETTING;
    for (const auto &pair : pairs) {
        message.setting.index = pair.index;
        message.setting.value = pair.value;
        if (q2proto_client_write(
                &cls.q2proto_ctx, reinterpret_cast<uintptr_t>(&io),
                &message) != Q2P_ERR_SUCCESS) {
            return false;
        }
    }
    if (scratch.overflowed || scratch.cursize != kEncodedClientReadyBytes)
        return false;

    netchan_t *const channel = pilot.channel;
    if (!channel || channel != &cls.netchan || !channel->message.data ||
        channel->message.overflowed ||
        channel->message.cursize > channel->message.maxsize ||
        scratch.cursize >
            channel->message.maxsize - channel->message.cursize) {
        return false;
    }

    /* One preflighted append is the atomic reliable-queue commit. */
    SZ_Write(&channel->message, scratch.data, scratch.cursize);
    pilot.readiness_committed = true;
    if (record.record_kind == WORR_NATIVE_READINESS_RECORD_CLIENT_READY) {
        counter_increment(telemetry.client_ready_queued);
    } else if (record.record_kind ==
               WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM) {
        pilot.client_active_confirm_queued = true;
    }
    return true;
}

bool observe_challenge(
    const worr_native_readiness_record_v1 &challenge, uint64_t now)
{
    const bool starts_fresh_map_epoch = pilot.map_quiesced;
    const bool fresh_challenge_expected =
        !pilot.readiness_initialized || starts_fresh_map_epoch;
    worr_native_readiness_state_v1 next{};
    if (!pilot.readiness_initialized) {
        const auto initialized = pilot.snapshot_enabled
            ? Worr_NativeReadinessClientInitBoundV1(
                  &next, challenge.transport_epoch,
                  challenge.snapshot_epoch,
                  pilot.private_capabilities, now,
                  kReadinessTimeoutMilliseconds)
            : Worr_NativeReadinessClientInitV1(
                  &next, challenge.transport_epoch,
                  pilot.private_capabilities, now,
                  kReadinessTimeoutMilliseconds);
        if (initialized !=
            WORR_NATIVE_READINESS_OK) {
            return false;
        }
    } else {
        next = pilot.readiness;
        if (pilot.map_quiesced) {
            const auto advanced = pilot.snapshot_enabled
                ? Worr_NativeReadinessClientAdvanceEpochBoundV1(
                      &next, challenge.transport_epoch,
                      challenge.snapshot_epoch,
                      pilot.private_capabilities, now,
                      kReadinessTimeoutMilliseconds)
                : Worr_NativeReadinessClientAdvanceEpochV1(
                      &next, challenge.transport_epoch,
                      pilot.private_capabilities, now,
                      kReadinessTimeoutMilliseconds);
            if (advanced !=
                WORR_NATIVE_READINESS_OK) {
                return false;
            }
        } else if (challenge.transport_epoch != next.transport_epoch) {
            return false;
        }
    }

    client_native_cancellation_stage_t cancellation{};
    if (fresh_challenge_expected &&
        !stage_prior_native_epoch_cancellation(
            challenge.transport_epoch, cancellation)) {
        return false;
    }

    worr_native_readiness_record_v1 client_ready{};
    const worr_native_readiness_result_v1 result =
        Worr_NativeReadinessClientObserveChallengeV1(
            &next, &challenge, now, &client_ready);
    if (result != WORR_NATIVE_READINESS_OK &&
        result != WORR_NATIVE_READINESS_EXACT_DUPLICATE) {
        return false;
    }
    if (fresh_challenge_expected !=
        (result == WORR_NATIVE_READINESS_OK)) {
        return false;
    }
    if (pilot.snapshot_enabled &&
        !CL_SnapshotShadowBindNativeEpoch(
            challenge.snapshot_epoch)) {
        return false;
    }

    /* queue_readiness_record preflights and stages the complete reliable
     * append.  Its successful append is the transaction's point of no return;
     * every cancellation below was already proven on isolated copies. */
    if (!queue_readiness_record(client_ready))
        return false;

    if (result == WORR_NATIVE_READINESS_OK)
        commit_prior_native_epoch_cancellation(cancellation);

    if (result == WORR_NATIVE_READINESS_OK)
        counter_increment(telemetry.challenges);

    pilot.readiness = next;
    pilot.readiness_initialized = true;
    pilot.map_quiesced = false;
    pilot.snapshot_timeline_owned = pilot.snapshot_enabled;
    if (starts_fresh_map_epoch)
        pilot.mode = pilot_mode_t::arming;
    return true;
}

bool activate_native_session(
    const worr_native_readiness_state_v1 &readiness)
{
    worr_native_session_binding_v1 binding{};
    worr_native_tx_session_v1 next_tx{};
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity> next_slots{};
    worr_native_carrier_tx_gate_v1 next_gate{};
    worr_native_command_shadow_payload_registry_v1 next_registry{};
    worr_native_rx_session_v1 next_event_rx{};
    std::array<worr_native_rx_slot_v1, kEventRxSlotCapacity>
        next_event_slots{};
    std::array<byte, kEventPayloadArenaBytes> next_event_arena{};
    worr_native_carrier_ack_ledger_v1 next_event_ledger{};
    snapshot_receiver_ptr_t next_snapshot_receiver{};

    if (!Worr_NativeSessionBindingInitFromReadinessV1(
            &binding, &readiness, pilot.connection_owner_id) ||
        binding.negotiated_capabilities != pilot.private_capabilities ||
        !Worr_NativeCommandShadowPayloadRegistryInitV1(
            &next_registry, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS)) {
        return false;
    }

    if (!pilot.session_initialized) {
        if (!Worr_NativeTxSessionInitV1(
                &next_tx, next_slots.data(), kTxSlotCapacity, &binding) ||
            !Worr_NativeCarrierTxGateInitV1(&next_gate, &binding)) {
            return false;
        }
    } else {
        if (!Worr_NativeCommandShadowPayloadRegistryValidateV1(
                &pilot.payload_registry) ||
            pilot.payload_registry.occupied_count !=
                pilot.tx.retained_count) {
            return false;
        }
        next_tx = pilot.tx;
        next_slots = pilot.tx_slots;
        next_gate = pilot.tx_gate;
        if (!Worr_NativeTxSessionAdvanceEpochV1(
                &next_tx, next_slots.data(), kTxSlotCapacity, &binding) ||
            !Worr_NativeCarrierTxGateAdvanceEpochV1(
                &next_gate, &binding)) {
            return false;
        }
    }

    if (pilot.event_enabled) {
        if (!Worr_EventStreamOwnerValidateV1(&pilot.event_owner) ||
            pilot.event_owner.connection_owner_id !=
                pilot.connection_owner_id ||
            pilot.event_consumer.struct_size !=
                sizeof(pilot.event_consumer) ||
            pilot.event_consumer.schema_version !=
                WORR_NATIVE_EVENT_ADMISSION_ABI_VERSION) {
            return false;
        }
        if (!pilot.session_initialized) {
            if (!Worr_NativeRxSessionInitV1(
                    &next_event_rx, next_event_slots.data(),
                    kEventRxSlotCapacity, kEventPayloadStride,
                    kRxFragmentTimeoutMilliseconds,
                    kRxCompleteTimeoutMilliseconds, &binding) ||
                !Worr_NativeCarrierAckLedgerInitV1(
                    &next_event_ledger, &binding,
                    kAckProactiveHandoffs)) {
                return false;
            }
        } else {
            if (!Worr_NativeRxSessionValidateV1(
                    &pilot.event_rx, pilot.event_rx_slots.data(),
                    kEventRxSlotCapacity) ||
                !Worr_NativeCarrierAckLedgerValidateV1(
                    &pilot.event_ack_ledger)) {
                return false;
            }
            next_event_rx = pilot.event_rx;
            next_event_slots = pilot.event_rx_slots;
            next_event_ledger = pilot.event_ack_ledger;
            if (!Worr_NativeRxSessionAdvanceEpochV1(
                    &next_event_rx, next_event_slots.data(),
                    kEventRxSlotCapacity, &binding) ||
                !Worr_NativeCarrierAckLedgerAdvanceEpochV1(
                    &next_event_ledger, &binding)) {
                return false;
            }
        }
    } else if (pilot.snapshot_enabled) {
        if (pilot.session_initialized ||
            readiness.snapshot_epoch == 0 ||
            cl.csr.max_edicts <= 1 ||
            cl.csr.max_edicts > MAX_EDICTS) {
            return false;
        }
        worr_native_snapshot_consumer_v1 consumer{};
        if (!CL_SnapshotShadowGetNativeConsumerV1(&consumer)) {
            return false;
        }
        next_snapshot_receiver = allocate_snapshot_receiver();
        if (!next_snapshot_receiver ||
            !Worr_NativeSnapshotReceiverInitV1(
                next_snapshot_receiver.get(), &binding,
                readiness.snapshot_epoch, cl.csr.max_edicts,
                &consumer)) {
            return false;
        }
    }

    if (pilot.session_initialized) {
        /* Preserve exactly the immediately preceding transport bank so an
         * ACK already in flight may release its retained command handle after
         * the new epoch becomes active.  A later activation replaces it. */
        pilot.retired_tx = pilot.tx;
        pilot.retired_tx_slots = pilot.tx_slots;
        pilot.retired_payload_registry = pilot.payload_registry;
        if (pilot.event_enabled) {
            pilot.retired_event_rx = pilot.event_rx;
            pilot.retired_event_rx_slots = pilot.event_rx_slots;
            pilot.retired_event_payload_arena =
                pilot.event_payload_arena;
            pilot.retired_event_ack_ledger = pilot.event_ack_ledger;
        } else if (pilot.snapshot_enabled) {
            pilot.retired_snapshot_receiver =
                std::move(pilot.snapshot_receiver);
        }
        pilot.retired_session_initialized = true;
    }
    pilot.binding = binding;
    pilot.tx = next_tx;
    pilot.tx_slots = next_slots;
    pilot.tx_gate = next_gate;
    pilot.dispatch = {};
    pilot.payload_registry = next_registry;
    if (pilot.event_enabled) {
        pilot.event_rx = next_event_rx;
        pilot.event_rx_slots = next_event_slots;
        pilot.event_payload_arena = next_event_arena;
        pilot.event_ack_ledger = next_event_ledger;
    } else if (pilot.snapshot_enabled) {
        pilot.snapshot_receiver =
            std::move(next_snapshot_receiver);
    }
    pilot.session_initialized = true;
    pilot.last_enqueued_command_valid = false;
    pilot.last_enqueued_command_id = {};
    pilot.tx_packet_kind = tx_packet_kind_t::none;
    pilot.active_completion_token = 0;
    pilot.mixed_token = {};
    pilot.ack_emit_token = {};
    pilot.mode = pilot_mode_t::active;
    return true;
}

bool observe_server_active(
    const worr_native_readiness_record_v1 &server_active, uint64_t now)
{
    /* DRAIN is fail-closed within one map epoch.  Only a successfully
     * validated fresh challenge after map quiesce may move it back to ARMING;
     * a later ACTIVE record from the failed epoch cannot resurrect DATA. */
    if (!pilot.readiness_initialized || pilot.mode == pilot_mode_t::drain)
        return false;
    worr_native_readiness_state_v1 next = pilot.readiness;
    worr_native_readiness_record_v1 client_active_confirm{};
    const worr_native_readiness_result_v1 result =
        semantic_ack_enabled()
        ? Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &next, &server_active, now, &client_active_confirm)
        : Worr_NativeReadinessClientObserveServerActiveV1(
              &next, &server_active, now);
    if (result != WORR_NATIVE_READINESS_OK &&
        result != WORR_NATIVE_READINESS_EXACT_DUPLICATE) {
        return false;
    }

    /* An old SERVER_ACTIVE duplicate must never resurrect a map-quiesced
     * epoch.  Only the state machine's fresh transition installs a session. */
    if (result == WORR_NATIVE_READINESS_OK &&
        !activate_native_session(next))
        return false;
    if (semantic_ack_enabled() &&
        !queue_readiness_record(client_active_confirm))
        return false;
    if (result == WORR_NATIVE_READINESS_OK)
        counter_increment(telemetry.server_active);
    pilot.readiness = next;
    return true;
}

bool observe_record(const worr_native_readiness_record_v1 &record)
{
    if (!Worr_NativeReadinessRecordValidateV1(&record) ||
        (record.record_kind !=
             WORR_NATIVE_READINESS_RECORD_CHALLENGE &&
         record.record_kind !=
             WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE) ||
        !pilot.capability_confirmed ||
        record.negotiated_capabilities != pilot.private_capabilities) {
        return false;
    }

    /* A fully validated readiness declaration from an epoch explicitly
     * canceled by a later CHALLENGE cannot rearm the old lifecycle.  Keep its
     * sideband carrier consumed and count it separately from stale WTC DATA
     * so reliable reordering across the map boundary remains harmless. */
    if (pilot.cancelled_through_transport_epoch != 0 &&
        record.transport_epoch <=
            pilot.cancelled_through_transport_epoch) {
        counter_increment(telemetry.stale_cancelled_readiness_records);
        return true;
    }

    uint64_t now;
    if (!current_tick(now))
        return false;
    switch (record.record_kind) {
    case WORR_NATIVE_READINESS_RECORD_CHALLENGE:
        return observe_challenge(record, now);
    case WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE:
        return observe_server_active(record, now);
    default:
        return false;
    }
}

bool accepted_observe_result(
    worr_native_readiness_sideband_result_v1 result)
{
    return result == WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND ||
           result == WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED;
}

worr_native_record_ref_v1 command_record_ref(
    worr_command_id_v1 command_id)
{
    worr_native_record_ref_v1 ref{};
    ref.record_class = WORR_NATIVE_RECORD_COMMAND_V1;
    ref.record_schema_version = WORR_COMMAND_ABI_VERSION;
    ref.object_epoch = command_id.epoch;
    ref.object_sequence = command_id.sequence;
    return ref;
}

void abort_dispatch_if_active()
{
    if ((pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) == 0) {
        return;
    }
    if ((pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0) {
        if (Worr_NativeCarrierSessionDispatchRejectPacketV1(
                &pilot.tx_gate, &pilot.dispatch) !=
            WORR_NATIVE_CARRIER_SESSION_OK) {
            return;
        }
    }
    (void)Worr_NativeCarrierSessionDispatchAbortV1(
        &pilot.tx_gate, &pilot.dispatch);
}

netchan_app_tx_prepare_result_t pilot_tx_prepare_command_only(
    void *opaque, const netchan_app_tx_prepare_info_v1_t *info,
    const byte *legacy_application, byte *candidate_application,
    netchan_app_tx_prepare_output_v1_t *output)
{
    if (opaque != &pilot || !live_pilot())
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    if (pilot.mode != pilot_mode_t::active ||
        !pilot.session_initialized ||
        pilot.tx.retained_count == 0) {
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (!info || !output || !candidate_application ||
        info->abi_version != NETCHAN_APP_TX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info) ||
        info->reliable_bytes > UINT32_MAX - info->unreliable_bytes ||
        info->reliable_bytes + info->unreliable_bytes !=
            info->legacy_application_bytes ||
        info->legacy_application_bytes > info->max_application_bytes ||
        info->legacy_application_bytes > UINT16_MAX ||
        (info->legacy_application_bytes != 0 && !legacy_application)) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    const uint16_t application_budget = static_cast<uint16_t>(
        info->max_application_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES
            ? WORR_NATIVE_CARRIER_MAX_PACKET_BYTES
            : info->max_application_bytes);
    const uint16_t legacy_bytes =
        static_cast<uint16_t>(info->legacy_application_bytes);
    if (application_budget == 0 || legacy_bytes > application_budget)
        return NETCHAN_APP_TX_PREPARE_BYPASS;

    uint64_t now;
    if (!current_tick(now) ||
        !Worr_NativeReadinessCanTransmitNativeV1(
            &pilot.readiness, now)) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    if ((pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    const worr_native_carrier_session_result_v1 begun =
        Worr_NativeCarrierSessionDispatchBeginV1(
            &pilot.tx_gate, &pilot.tx, pilot.tx_slots.data(),
            kTxSlotCapacity, now, kResendIntervalMilliseconds,
            application_budget, legacy_bytes, &pilot.dispatch);
    if (begun == WORR_NATIVE_CARRIER_SESSION_NOT_DUE ||
        begun == WORR_NATIVE_CARRIER_SESSION_LIMIT) {
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (begun != WORR_NATIVE_CARRIER_SESSION_OK) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    const uint32_t handle =
        pilot.dispatch.send_ticket.pre_send_slot.payload_handle;
    std::array<byte, kCommandPayloadBytes> payload{};
    size_t payload_bytes = 0;
    worr_command_id_v1 payload_command_id{};
    if (Worr_NativeCommandShadowPayloadCopyV1(
            &pilot.payload_registry, handle, payload.data(),
            payload.size(), &payload_bytes, &payload_command_id) !=
            WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_COPIED ||
        payload_bytes != kCommandPayloadBytes ||
        payload_command_id.epoch !=
            pilot.dispatch.send_ticket.pre_send_slot.record.object_epoch ||
        payload_command_id.sequence !=
            pilot.dispatch.send_ticket.pre_send_slot.record.object_sequence ||
        Worr_NativeCarrierSessionDispatchBindPayloadV1(
            &pilot.tx_gate, &pilot.dispatch, handle, payload.data(),
            static_cast<uint32_t>(payload_bytes)) !=
            WORR_NATIVE_CARRIER_SESSION_OK) {
        abort_dispatch_if_active();
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    size_t packet_bytes = 0;
    const worr_native_carrier_session_result_v1 prepared =
        Worr_NativeCarrierSessionDispatchPreparePacketV1(
            &pilot.tx_gate, &pilot.tx, pilot.tx_slots.data(),
            kTxSlotCapacity, &pilot.dispatch, handle, payload.data(),
            static_cast<uint32_t>(payload_bytes), legacy_application,
            legacy_bytes, candidate_application,
            info->max_application_bytes, &packet_bytes);
    if (prepared != WORR_NATIVE_CARRIER_SESSION_OK) {
        abort_dispatch_if_active();
        if (prepared != WORR_NATIVE_CARRIER_SESSION_LIMIT &&
            prepared != WORR_NATIVE_CARRIER_SESSION_OUTPUT_TOO_SMALL) {
            pilot_failure();
        }
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (packet_bytes !=
            static_cast<size_t>(legacy_bytes) + kCommandCarrierOverhead ||
        packet_bytes > UINT32_MAX) {
        abort_dispatch_if_active();
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    output->abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    output->struct_size = sizeof(*output);
    output->application_bytes = static_cast<uint32_t>(packet_bytes);
    output->reserved0 = 0;
    output->token = pilot.dispatch.token_id;
    return NETCHAN_APP_TX_PREPARE_PREPARED;
}

void pilot_tx_completion_command_only(
    void *opaque, const netchan_app_tx_completion_info_v1_t *info,
    const byte *application)
{
    if (opaque != &pilot || !live_pilot())
        return;
    if (!info || info->abi_version != NETCHAN_APP_TX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info) ||
        info->token == 0 || info->token != pilot.dispatch.token_id ||
        (info->application_bytes != 0 && !application) ||
        (pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) == 0) {
        pilot_failure();
        return;
    }

    if (info->result == NETCHAN_APP_TX_COMPLETION_ACCEPTED) {
        pilot.carrier_traffic_seen = true;
        const bool retry =
            pilot.dispatch.send_ticket.pre_send_slot.send_attempts != 0;
        uint64_t now;
        if (!current_tick(now) ||
            Worr_NativeCarrierSessionDispatchConfirmPacketV1(
                &pilot.tx_gate, &pilot.tx, pilot.tx_slots.data(),
                kTxSlotCapacity, &pilot.dispatch, now, application,
                info->application_bytes) !=
                WORR_NATIVE_CARRIER_SESSION_DISPATCH_COMMITTED) {
            enter_drain(true);
        } else {
            counter_increment(telemetry.tx_handoffs);
            counter_increment(retry ? telemetry.tx_retries
                                    : telemetry.tx_first_sends);
        }
        return;
    }

    if (info->result == NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED ||
        info->result == NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID) {
        const bool rejected =
            Worr_NativeCarrierSessionDispatchRejectPacketV1(
                &pilot.tx_gate, &pilot.dispatch) ==
            WORR_NATIVE_CARRIER_SESSION_OK;
        const bool aborted = rejected &&
            Worr_NativeCarrierSessionDispatchAbortV1(
                &pilot.tx_gate, &pilot.dispatch) ==
                WORR_NATIVE_CARRIER_SESSION_OK;
        if (!aborted)
            pilot_failure();
        return;
    }

    pilot_failure();
}

void clear_event_tx_packet()
{
    pilot.tx_packet_kind = tx_packet_kind_t::none;
    pilot.active_completion_token = 0;
    pilot.prepared_application_bytes = 0;
    pilot.mixed_token = {};
    pilot.ack_emit_token = {};
}

bool allocate_completion_token(uint64_t &token_out)
{
    if (pilot.next_completion_token == UINT64_MAX)
        return false;
    token_out = ++pilot.next_completion_token;
    return token_out != 0;
}

bool event_ack_due(const worr_native_carrier_ack_ledger_v1 &ledger,
                   uint64_t now, bool &due_out)
{
    const auto result = Worr_NativeCarrierAckPeekDueV1(
        &ledger, now, kAckRetryIntervalMilliseconds);
    if (result != WORR_NATIVE_CARRIER_ACK_OK &&
        result != WORR_NATIVE_CARRIER_ACK_NOT_DUE) {
        return false;
    }
    due_out = result == WORR_NATIVE_CARRIER_ACK_OK;
    return true;
}

bool map_drain_ack_service_active()
{
    /* Map quiesce freezes all native DATA, but receipts already authorized
     * by semantic admission remain valid release information for the peer.
     * Keep this exception narrower than generic fail-closed DRAIN so a
     * protocol/semantic failure cannot continue emitting native traffic. */
    return semantic_ack_enabled() && pilot.session_initialized &&
           pilot.mode == pilot_mode_t::drain && pilot.map_quiesced;
}

netchan_app_tx_prepare_result_t prepare_event_ack_only(
    const byte *legacy_application, byte *candidate_application,
    netchan_app_tx_prepare_output_v1_t *output, uint64_t now,
    uint16_t application_budget, uint16_t legacy_bytes)
{
    auto *const current_ledger =
        current_semantic_ack_ledger();
    auto *const retired_ledger =
        retired_semantic_ack_ledger();
    if (!current_ledger ||
        (pilot.retired_session_initialized && !retired_ledger)) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    bool current_due = false;
    bool retired_due = false;
    if (!event_ack_due(*current_ledger, now, current_due) ||
        (pilot.retired_session_initialized &&
         !event_ack_due(*retired_ledger, now,
                        retired_due))) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (!current_due && !retired_due)
        return NETCHAN_APP_TX_PREPARE_BYPASS;

    event_ack_bank_t bank;
    if (current_due && retired_due)
        bank = pilot.ack_next_bank;
    else
        bank = current_due ? event_ack_bank_t::current
                           : event_ack_bank_t::retired;

    auto *const live_ledger = bank == event_ack_bank_t::current
        ? current_ledger
        : retired_ledger;
    auto staged_ledger = *live_ledger;
    worr_native_carrier_ack_emit_token_v1 token{};
    size_t packet_bytes = 0;
    const auto prepared = Worr_NativeCarrierAckPreparePacketV1(
        &staged_ledger, now, kAckRetryIntervalMilliseconds,
        application_budget, legacy_application, legacy_bytes,
        candidate_application, application_budget, &packet_bytes, &token);
    if (prepared == WORR_NATIVE_CARRIER_ACK_NOT_DUE ||
        prepared == WORR_NATIVE_CARRIER_ACK_LIMIT ||
        prepared == WORR_NATIVE_CARRIER_ACK_OUTPUT_TOO_SMALL) {
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (prepared != WORR_NATIVE_CARRIER_ACK_OK || packet_bytes == 0 ||
        packet_bytes > application_budget || packet_bytes > UINT32_MAX) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    uint64_t completion_token = 0;
    if (!allocate_completion_token(completion_token)) {
        (void)Worr_NativeCarrierAckRejectHandoffV1(
            &staged_ledger, &token);
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    *live_ledger = staged_ledger;
    pilot.ack_emit_token = token;
    pilot.tx_packet_kind = bank == event_ack_bank_t::current
        ? tx_packet_kind_t::ack_current
        : tx_packet_kind_t::ack_retired;
    pilot.ack_next_bank = bank == event_ack_bank_t::current
        ? event_ack_bank_t::retired
        : event_ack_bank_t::current;
    pilot.active_completion_token = completion_token;
    pilot.prepared_application_bytes =
        static_cast<uint32_t>(packet_bytes);
    std::memcpy(pilot.prepared_application.data(), candidate_application,
                packet_bytes);

    *output = {};
    output->abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    output->struct_size = sizeof(*output);
    output->application_bytes = static_cast<uint32_t>(packet_bytes);
    output->token = completion_token;
    return NETCHAN_APP_TX_PREPARE_PREPARED;
}

netchan_app_tx_prepare_result_t pilot_tx_prepare_event(
    void *opaque, const netchan_app_tx_prepare_info_v1_t *info,
    const byte *legacy_application, byte *candidate_application,
    netchan_app_tx_prepare_output_v1_t *output)
{
    if (opaque != &pilot || !live_pilot())
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    const bool active_traffic =
        pilot.mode == pilot_mode_t::active && !pilot.map_quiesced;
    const bool drain_ack_service = map_drain_ack_service_active();
    if ((!active_traffic && !drain_ack_service) ||
        !pilot.session_initialized || !semantic_ack_enabled())
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    if (!info || !output || !candidate_application ||
        info->abi_version != NETCHAN_APP_TX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info) ||
        info->reliable_bytes > UINT32_MAX - info->unreliable_bytes ||
        info->reliable_bytes + info->unreliable_bytes !=
            info->legacy_application_bytes ||
        info->legacy_application_bytes > info->max_application_bytes ||
        info->legacy_application_bytes > UINT16_MAX ||
        (info->legacy_application_bytes != 0 && !legacy_application) ||
        pilot.tx_packet_kind != tx_packet_kind_t::none) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    const uint16_t application_budget = static_cast<uint16_t>(
        info->max_application_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES
            ? WORR_NATIVE_CARRIER_MAX_PACKET_BYTES
            : info->max_application_bytes);
    const uint16_t legacy_bytes =
        static_cast<uint16_t>(info->legacy_application_bytes);
    if (application_budget == 0 || legacy_bytes > application_budget)
        return NETCHAN_APP_TX_PREPARE_BYPASS;

    uint64_t now = 0;
    if (!current_tick(now)) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (drain_ack_service) {
        return prepare_event_ack_only(
            legacy_application, candidate_application, output, now,
            application_budget, legacy_bytes);
    }
    if (!Worr_NativeReadinessCanTransmitNativeV1(
            &pilot.readiness, now)) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    const bool continuing_dispatch =
        (pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0;
    if (continuing_dispatch &&
        (pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    bool data_due = continuing_dispatch;
    if (!continuing_dispatch && pilot.tx.retained_count != 0) {
        const auto begun = Worr_NativeCarrierMixedBeginV1(
            &pilot.tx_gate, &pilot.tx, pilot.tx_slots.data(),
            kTxSlotCapacity, now, kResendIntervalMilliseconds,
            application_budget, legacy_bytes, &pilot.dispatch);
        data_due = begun == WORR_NATIVE_CARRIER_MIXED_OK;
        if (!data_due &&
            begun != WORR_NATIVE_CARRIER_MIXED_NOT_DUE &&
            begun != WORR_NATIVE_CARRIER_MIXED_LIMIT) {
            pilot_failure();
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }
    }
    if (data_due) {
        auto *const current_ledger =
            current_semantic_ack_ledger();
        if (!current_ledger) {
            pilot_failure();
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }
        const uint32_t handle =
            pilot.dispatch.send_ticket.pre_send_slot.payload_handle;
        std::array<byte, kCommandPayloadBytes> payload{};
        size_t payload_bytes = 0;
        worr_command_id_v1 payload_command_id{};
        if (Worr_NativeCommandShadowPayloadCopyV1(
                &pilot.payload_registry, handle, payload.data(),
                payload.size(), &payload_bytes,
                &payload_command_id) !=
                WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_COPIED ||
            payload_bytes != kCommandPayloadBytes ||
            payload_command_id.epoch !=
                pilot.dispatch.send_ticket.pre_send_slot.record
                    .object_epoch ||
            payload_command_id.sequence !=
                pilot.dispatch.send_ticket.pre_send_slot.record
                    .object_sequence ||
            (!continuing_dispatch &&
             Worr_NativeCarrierSessionDispatchBindPayloadV1(
                 &pilot.tx_gate, &pilot.dispatch, handle,
                 payload.data(),
                 static_cast<uint32_t>(payload_bytes)) !=
                 WORR_NATIVE_CARRIER_SESSION_OK)) {
            (void)Worr_NativeCarrierMixedAbortV1(
                &pilot.tx_gate, &pilot.dispatch,
                current_ledger);
            pilot_failure();
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }

        size_t packet_bytes = 0;
        worr_native_carrier_mixed_token_v1 token{};
        const auto prepared = Worr_NativeCarrierMixedPreparePacketV1(
            &pilot.tx_gate, &pilot.tx, pilot.tx_slots.data(),
            kTxSlotCapacity, &pilot.dispatch,
            current_ledger, now,
            kAckRetryIntervalMilliseconds, handle, payload.data(),
            static_cast<uint32_t>(payload_bytes), legacy_application,
            legacy_bytes, candidate_application, application_budget,
            &packet_bytes, &token);
        if (prepared != WORR_NATIVE_CARRIER_MIXED_OK) {
            (void)Worr_NativeCarrierMixedAbortV1(
                &pilot.tx_gate, &pilot.dispatch,
                current_ledger);
            if (prepared != WORR_NATIVE_CARRIER_MIXED_LIMIT &&
                prepared !=
                    WORR_NATIVE_CARRIER_MIXED_OUTPUT_TOO_SMALL) {
                pilot_failure();
            }
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }

        uint64_t completion_token = 0;
        if (packet_bytes == 0 || packet_bytes > application_budget ||
            packet_bytes > UINT32_MAX ||
            !allocate_completion_token(completion_token)) {
            (void)Worr_NativeCarrierMixedAbortPacketV1(
                &pilot.tx_gate, &pilot.dispatch,
                current_ledger, &token,
                candidate_application, packet_bytes);
            pilot_failure();
            return NETCHAN_APP_TX_PREPARE_BYPASS;
        }

        pilot.mixed_token = token;
        pilot.tx_packet_kind = tx_packet_kind_t::command_mixed;
        pilot.active_completion_token = completion_token;
        pilot.prepared_application_bytes =
            static_cast<uint32_t>(packet_bytes);
        std::memcpy(pilot.prepared_application.data(),
                    candidate_application, packet_bytes);
        *output = {};
        output->abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
        output->struct_size = sizeof(*output);
        output->application_bytes =
            static_cast<uint32_t>(packet_bytes);
        output->token = completion_token;
        return NETCHAN_APP_TX_PREPARE_PREPARED;
    }

    return prepare_event_ack_only(
        legacy_application, candidate_application, output, now,
        application_budget, legacy_bytes);
}

void pilot_tx_completion_event(
    void *opaque, const netchan_app_tx_completion_info_v1_t *info,
    const byte *application)
{
    if (opaque != &pilot || !live_pilot())
        return;
    if (pilot.tx_packet_kind == tx_packet_kind_t::none)
        return;

    const bool info_valid = info &&
        info->abi_version == NETCHAN_APP_TX_HOOK_ABI_V1 &&
        info->struct_size == sizeof(*info) &&
        info->token == pilot.active_completion_token;
    const bool accepted = info_valid &&
        info->result == NETCHAN_APP_TX_COMPLETION_ACCEPTED &&
        info->packet_copies != 0 && info->accepted_copies != 0 &&
        info->accepted_copies <= info->packet_copies && application &&
        info->application_bytes == pilot.prepared_application_bytes;
    const bool rejected = info_valid &&
        (info->result == NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED ||
         info->result == NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID);
    bool terminal_ok = false;

    if (pilot.tx_packet_kind == tx_packet_kind_t::command_mixed) {
        auto *const ledger = current_semantic_ack_ledger();
        const bool retry =
            pilot.dispatch.send_ticket.pre_send_slot.send_attempts != 0;
        if (!ledger) {
            terminal_ok = false;
        } else if (accepted) {
            uint64_t now = 0;
            const auto result = current_tick(now)
                ? Worr_NativeCarrierMixedConfirmPacketV1(
                      &pilot.tx_gate, &pilot.tx, pilot.tx_slots.data(),
                      kTxSlotCapacity, &pilot.dispatch,
                      ledger, &pilot.mixed_token, now,
                      application, info->application_bytes)
                : WORR_NATIVE_CARRIER_MIXED_CLOCK_REGRESSION;
            terminal_ok =
                result == WORR_NATIVE_CARRIER_MIXED_OK ||
                result ==
                    WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED ||
                result == WORR_NATIVE_CARRIER_MIXED_DISPATCH_RETIRED;
            if (terminal_ok) {
                pilot.carrier_traffic_seen = true;
                counter_increment(telemetry.tx_handoffs);
                counter_increment(retry ? telemetry.tx_retries
                                        : telemetry.tx_first_sends);
            }
        } else if (rejected) {
            const auto reject_result =
                Worr_NativeCarrierMixedRejectPacketV1(
                    &pilot.tx_gate, &pilot.dispatch,
                    ledger, &pilot.mixed_token,
                    pilot.prepared_application.data(),
                    pilot.prepared_application_bytes);
            const auto abort_result = reject_result ==
                    WORR_NATIVE_CARRIER_MIXED_OK
                ? Worr_NativeCarrierMixedAbortV1(
                      &pilot.tx_gate, &pilot.dispatch,
                      ledger)
                : WORR_NATIVE_CARRIER_MIXED_INVALID_STATE;
            terminal_ok = reject_result == WORR_NATIVE_CARRIER_MIXED_OK &&
                          abort_result == WORR_NATIVE_CARRIER_MIXED_OK;
        }
    } else {
        auto *const ledger =
            pilot.tx_packet_kind == tx_packet_kind_t::ack_current
            ? current_semantic_ack_ledger()
            : retired_semantic_ack_ledger();
        if (!ledger) {
            terminal_ok = false;
        } else if (accepted) {
            uint64_t now = 0;
            terminal_ok = current_tick(now) &&
                Worr_NativeCarrierAckCommitHandoffV1(
                    ledger, &pilot.ack_emit_token, now, application,
                    info->application_bytes) ==
                    WORR_NATIVE_CARRIER_ACK_OK;
            if (terminal_ok)
                pilot.carrier_traffic_seen = true;
        } else if (rejected) {
            terminal_ok = Worr_NativeCarrierAckRejectHandoffV1(
                              ledger, &pilot.ack_emit_token) ==
                          WORR_NATIVE_CARRIER_ACK_OK;
        }
    }

    clear_event_tx_packet();
    if (!terminal_ok)
        pilot_failure();
}

netchan_app_tx_prepare_result_t pilot_tx_prepare(
    void *opaque, const netchan_app_tx_prepare_info_v1_t *info,
    const byte *legacy_application, byte *candidate_application,
    netchan_app_tx_prepare_output_v1_t *output)
{
    return semantic_ack_enabled()
        ? pilot_tx_prepare_event(
              opaque, info, legacy_application, candidate_application,
              output)
        : pilot_tx_prepare_command_only(
              opaque, info, legacy_application, candidate_application,
              output);
}

void pilot_tx_completion(
    void *opaque, const netchan_app_tx_completion_info_v1_t *info,
    const byte *application)
{
    if (semantic_ack_enabled())
        pilot_tx_completion_event(opaque, info, application);
    else
        pilot_tx_completion_command_only(opaque, info, application);
}

bool apply_ack_to_bank(
    worr_native_tx_session_v1 &tx,
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity> &slots,
    worr_native_command_shadow_payload_registry_v1 &registry,
    const byte *application, uint32_t application_bytes,
    uint32_t &acknowledged_out)
{
    worr_native_tx_session_v1 next_tx = tx;
    auto next_slots = slots;
    auto next_registry = registry;
    const bool retained_before = next_slots[0].state_flags != 0;
    const uint32_t retained_handle =
        retained_before ? next_slots[0].payload_handle : 0;
    uint32_t acknowledged = 0;
    if (Worr_NativeCarrierSessionApplyAcksV1(
            &next_tx, next_slots.data(), kTxSlotCapacity,
            application, application_bytes, &acknowledged) !=
            WORR_NATIVE_CARRIER_SESSION_OK ||
        acknowledged > 1 ||
        (acknowledged == 1 &&
         (!retained_before || next_slots[0].state_flags != 0)) ||
        (acknowledged == 0 && retained_before !=
            (next_slots[0].state_flags != 0))) {
        return false;
    }
    if (acknowledged == 1 &&
        Worr_NativeCommandShadowPayloadReleaseV1(
            &next_registry, retained_handle) !=
            WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RELEASED) {
        return false;
    }
    if (!Worr_NativeTxSessionValidateV1(
            &next_tx, next_slots.data(), kTxSlotCapacity) ||
        !Worr_NativeCommandShadowPayloadRegistryValidateV1(
            &next_registry) ||
        next_registry.occupied_count != next_tx.retained_count) {
        return false;
    }
    tx = next_tx;
    slots = next_slots;
    registry = next_registry;
    acknowledged_out = acknowledged;
    return true;
}

void expose_legacy(netchan_app_rx_output_v1_t *output,
                   uint32_t legacy_bytes)
{
    *output = {};
    output->abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output->struct_size = sizeof(*output);
    output->legacy_bytes = legacy_bytes;
}

netchan_app_rx_result_t pilot_rx_command_only(
    void *opaque, const netchan_app_rx_info_v1_t *info,
    const byte *application, netchan_app_rx_output_v1_t *output)
{
    if (opaque != &pilot || !live_pilot())
        return NETCHAN_APP_RX_BYPASS;
    if (!info || !output || !application ||
        info->abi_version != NETCHAN_APP_RX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info)) {
        pilot_failure();
        return NETCHAN_APP_RX_REJECT;
    }

    worr_native_carrier_view_v1 view{};
    const worr_native_carrier_result_v1 decoded =
        Worr_NativeCarrierDecodeV1(
            application, info->application_bytes, &view);
    if (decoded == WORR_NATIVE_CARRIER_NO_CARRIER)
        return NETCHAN_APP_RX_BYPASS;

    if (decoded != WORR_NATIVE_CARRIER_OK) {
        /* A terminal WTC marker makes corrupt bytes native traffic. */
        pilot.carrier_traffic_seen = true;
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }
    bool ack_only = view.entry_count != 0;
    for (uint16_t index = 0; ack_only && index < view.entry_count;
         ++index) {
        ack_only = view.entries[index].entry_type ==
            WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    }
    if (!ack_only) {
        pilot.carrier_traffic_seen = true;
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }
    if (pilot.cancelled_through_transport_epoch != 0 &&
        view.transport_epoch <=
            pilot.cancelled_through_transport_epoch) {
        counter_increment(telemetry.stale_cancelled_carriers);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }

    pilot.carrier_traffic_seen = true;
    if (!pilot.session_initialized ||
        (pilot.mode != pilot_mode_t::active &&
         pilot.mode != pilot_mode_t::drain)) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    if (pilot.mode == pilot_mode_t::active) {
        uint64_t now;
        if (!current_tick(now) ||
            !Worr_NativeReadinessCanReceiveNativeV1(
                &pilot.readiness, now)) {
            enter_drain(true);
            return NETCHAN_APP_RX_REJECT;
        }
    }

    const bool current_epoch =
        view.transport_epoch == pilot.tx.transport_epoch;
    const bool retired_epoch = pilot.retired_session_initialized &&
        view.transport_epoch == pilot.retired_tx.transport_epoch;
    if (current_epoch == retired_epoch) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }
    uint32_t acknowledged = 0;
    const bool applied = current_epoch
        ? apply_ack_to_bank(
              pilot.tx, pilot.tx_slots, pilot.payload_registry,
              application, info->application_bytes, acknowledged)
        : apply_ack_to_bank(
              pilot.retired_tx, pilot.retired_tx_slots,
              pilot.retired_payload_registry, application,
              info->application_bytes, acknowledged);
    if (!applied) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }
    counter_increment(telemetry.ack_carriers);
    if (acknowledged != 0) {
        counter_increment(telemetry.retained_releases);
        counter_increment(telemetry.acknowledged_reliable);
    }
    output->abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output->struct_size = sizeof(*output);
    output->legacy_bytes = view.legacy_bytes;
    output->reserved0 = 0;
    return NETCHAN_APP_RX_EXPOSE_LEGACY;
}

enum class event_data_admission_t {
    accepted,
    retry_later,
    rejected,
};

bool event_admission_committed(
    worr_native_event_admission_result_v1 result)
{
    return result ==
               WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED ||
           result ==
               WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_DUPLICATE ||
           result == WORR_NATIVE_EVENT_ADMISSION_EVENT_ACCEPTED ||
           result == WORR_NATIVE_EVENT_ADMISSION_EVENT_DUPLICATE ||
           result == WORR_NATIVE_EVENT_ADMISSION_EVENT_DEGRADED;
}

bool event_repeat_revalidated(
    worr_native_event_admission_result_v1 result)
{
    return result ==
               WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_REPEAT_REVALIDATED ||
           result ==
               WORR_NATIVE_EVENT_ADMISSION_EVENT_REPEAT_REVALIDATED;
}

event_data_admission_t admit_current_event_data(
    const byte *application, size_t application_bytes,
    uint16_t entry_index, uint64_t now)
{
    worr_native_rx_session_v1 staged_session = pilot.event_rx;
    auto staged_slots = pilot.event_rx_slots;
    auto staged_arena = pilot.event_payload_arena;
    worr_native_rx_result_v1 rx_result{};
    worr_native_rx_message_v1 message{};
    worr_native_ack_range_v1 repeat_acknowledgement{};
    const auto bridge = Worr_NativeCarrierSessionAcceptDataV1(
        &staged_session, staged_slots.data(), kEventRxSlotCapacity,
        staged_arena.data(), staged_arena.size(), now, application,
        application_bytes, entry_index, &rx_result, &message,
        &repeat_acknowledgement);
    if (bridge != WORR_NATIVE_CARRIER_SESSION_OK)
        return event_data_admission_t::rejected;

    if (rx_result == WORR_NATIVE_RX_ALREADY_COMMITTED) {
        const auto repeated =
            Worr_NativeEventAdmissionRevalidateCommittedRepeatV1(
                &pilot.event_owner, &pilot.binding, &pilot.event_rx,
                pilot.event_rx_slots.data(), kEventRxSlotCapacity,
                pilot.event_payload_arena.data(),
                pilot.event_payload_arena.size(), now, application,
                application_bytes, entry_index,
                &pilot.event_ack_ledger, &pilot.event_consumer);
        return event_repeat_revalidated(repeated)
            ? event_data_admission_t::accepted
            : event_data_admission_t::rejected;
    }

    if (rx_result == WORR_NATIVE_RX_MESSAGE_COMPLETE) {
        const auto admitted = Worr_NativeEventAdmissionCommitCompletedV1(
            &pilot.event_owner, &pilot.binding, &staged_session,
            staged_slots.data(), kEventRxSlotCapacity,
            staged_arena.data(), staged_arena.size(),
            &pilot.event_ack_ledger, &message, &pilot.event_consumer);
        if (!event_admission_committed(admitted))
            return admitted == WORR_NATIVE_EVENT_ADMISSION_RETRY_UNCOMMITTED
                ? event_data_admission_t::retry_later
                : event_data_admission_t::rejected;
        pilot.event_rx = staged_session;
        pilot.event_rx_slots = staged_slots;
        pilot.event_payload_arena = staged_arena;
        return event_data_admission_t::accepted;
    }

    if (rx_result == WORR_NATIVE_RX_FRAGMENT_ACCEPTED ||
        rx_result == WORR_NATIVE_RX_FRAGMENT_DUPLICATE ||
        rx_result == WORR_NATIVE_RX_CAPACITY ||
        rx_result == WORR_NATIVE_RX_STORAGE_CAPACITY) {
        pilot.event_rx = staged_session;
        pilot.event_rx_slots = staged_slots;
        pilot.event_payload_arena = staged_arena;
        return rx_result == WORR_NATIVE_RX_CAPACITY ||
                       rx_result == WORR_NATIVE_RX_STORAGE_CAPACITY
            ? event_data_admission_t::retry_later
            : event_data_admission_t::accepted;
    }
    return event_data_admission_t::rejected;
}

bool event_data_entry_shape_valid(
    const byte *application, const worr_native_carrier_view_v1 &view,
    uint16_t entry_index)
{
    if (!application || entry_index >= view.entry_count)
        return false;
    const auto &entry = view.entries[entry_index];
    if (entry.entry_type != WORR_NATIVE_CARRIER_ENTRY_DATA_V1 ||
        entry.data_offset > view.packet_bytes ||
        entry.data_bytes > view.packet_bytes - entry.data_offset) {
        return false;
    }
    worr_native_envelope_frame_info_v1 frame{};
    if (Worr_NativeEnvelopeDecodeV1(
            application + entry.data_offset, entry.data_bytes, &frame) !=
            WORR_NATIVE_ENVELOPE_DECODE_OK ||
        frame.transport_epoch != view.transport_epoch ||
        frame.record.reserved0 != 0) {
        return false;
    }
    return (frame.record.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
            frame.record.record_schema_version ==
                WORR_EVENT_ABI_VERSION) ||
           (frame.record.record_class ==
                WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1 &&
            frame.record.record_schema_version ==
                WORR_EVENT_STREAM_ABI_VERSION);
}

netchan_app_rx_result_t pilot_rx_event(
    void *opaque, const netchan_app_rx_info_v1_t *info,
    const byte *application, netchan_app_rx_output_v1_t *output)
{
    if (opaque != &pilot || !live_pilot())
        return NETCHAN_APP_RX_BYPASS;
    if (!info || !output || !application ||
        info->abi_version != NETCHAN_APP_RX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info)) {
        pilot_failure();
        return NETCHAN_APP_RX_REJECT;
    }

    worr_native_carrier_view_v1 view{};
    const auto decoded = Worr_NativeCarrierDecodeV1(
        application, info->application_bytes, &view);
    if (decoded == WORR_NATIVE_CARRIER_NO_CARRIER)
        return NETCHAN_APP_RX_BYPASS;

    if (decoded != WORR_NATIVE_CARRIER_OK) {
        pilot.carrier_traffic_seen = true;
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    uint16_t data_count = 0;
    uint16_t data_index = 0;
    bool has_ack = false;
    for (uint16_t index = 0; index < view.entry_count; ++index) {
        if (view.entries[index].entry_type ==
            WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
            data_index = index;
            ++data_count;
        } else if (view.entries[index].entry_type ==
                   WORR_NATIVE_CARRIER_ENTRY_ACK_V1) {
            has_ack = true;
        } else {
            pilot.carrier_traffic_seen = true;
            enter_drain(true);
            return NETCHAN_APP_RX_REJECT;
        }
    }
    if (data_count > 1) {
        pilot.carrier_traffic_seen = true;
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }
    if (data_count != 0 &&
        !event_data_entry_shape_valid(
            application, view, data_index)) {
        pilot.carrier_traffic_seen = true;
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }
    if (pilot.cancelled_through_transport_epoch != 0 &&
        view.transport_epoch <=
            pilot.cancelled_through_transport_epoch) {
        counter_increment(telemetry.stale_cancelled_carriers);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }

    pilot.carrier_traffic_seen = true;
    if (!pilot.session_initialized ||
        (pilot.mode != pilot_mode_t::active &&
         pilot.mode != pilot_mode_t::drain)) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    const bool current_epoch =
        view.transport_epoch == pilot.binding.transport_epoch;
    const bool retired_epoch = pilot.retired_session_initialized &&
        view.transport_epoch == pilot.retired_tx.transport_epoch;
    if (current_epoch == retired_epoch ||
        (retired_epoch && data_count != 0) ||
        (pilot.mode == pilot_mode_t::drain && data_count != 0)) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    if (current_epoch && pilot.mode == pilot_mode_t::active) {
        uint64_t gate_now = 0;
        if (!current_tick(gate_now) ||
            !Worr_NativeReadinessCanReceiveNativeV1(
                &pilot.readiness, gate_now)) {
            enter_drain(true);
            return NETCHAN_APP_RX_REJECT;
        }
    }

    worr_native_tx_session_v1 staged_tx = current_epoch
        ? pilot.tx
        : pilot.retired_tx;
    auto staged_slots = current_epoch
        ? pilot.tx_slots
        : pilot.retired_tx_slots;
    auto staged_registry = current_epoch
        ? pilot.payload_registry
        : pilot.retired_payload_registry;
    uint32_t acknowledged = 0;
    if (has_ack &&
        !apply_ack_to_bank(
            staged_tx, staged_slots, staged_registry, application,
            info->application_bytes, acknowledged)) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    if (data_count != 0) {
        uint64_t now = 0;
        if (!current_tick(now)) {
            enter_drain(true);
            return NETCHAN_APP_RX_REJECT;
        }
        const auto admitted = admit_current_event_data(
            application, info->application_bytes, data_index, now);
        if (admitted == event_data_admission_t::rejected) {
            enter_drain(true);
            return NETCHAN_APP_RX_REJECT;
        }
    }

    if (has_ack) {
        if (current_epoch) {
            pilot.tx = staged_tx;
            pilot.tx_slots = staged_slots;
            pilot.payload_registry = staged_registry;
        } else {
            pilot.retired_tx = staged_tx;
            pilot.retired_tx_slots = staged_slots;
            pilot.retired_payload_registry = staged_registry;
        }
        counter_increment(telemetry.ack_carriers);
        if (acknowledged != 0) {
            counter_increment(telemetry.retained_releases);
            counter_increment(telemetry.acknowledged_reliable);
        }
    }

    expose_legacy(output, view.legacy_bytes);
    return NETCHAN_APP_RX_EXPOSE_LEGACY;
}

enum class snapshot_expectation_pump_t {
    ready,
    pending,
    stale,
    retry_later,
    keyframe_required,
    rejected,
};

snapshot_expectation_pump_t observe_snapshot_expectation(
    worr_snapshot_id_v2 snapshot_id)
{
    if (!pilot.snapshot_receiver)
        return snapshot_expectation_pump_t::pending;

    worr_native_snapshot_expectation_v1 expectation{};
    const auto lookup = CL_SnapshotShadowGetNativeExpectation(
        snapshot_id, &expectation);
    if (lookup == CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_PENDING)
        return snapshot_expectation_pump_t::pending;
    if (lookup == CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_STALE)
        return snapshot_expectation_pump_t::stale;
    if (lookup != CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE)
        return snapshot_expectation_pump_t::rejected;

    const auto observed =
        Worr_NativeSnapshotReceiverObserveExpectationV1(
            pilot.snapshot_receiver.get(), &expectation);
    switch (observed) {
    case WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_REPEAT_REVALIDATED:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED:
        return snapshot_expectation_pump_t::ready;
    case WORR_NATIVE_SNAPSHOT_RECEIVER_RETRY_LATER:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_CAPACITY:
        return snapshot_expectation_pump_t::retry_later;
    case WORR_NATIVE_SNAPSHOT_RECEIVER_STALE:
        return snapshot_expectation_pump_t::stale;
    case WORR_NATIVE_SNAPSHOT_RECEIVER_KEYFRAME_REQUIRED:
        return snapshot_expectation_pump_t::keyframe_required;
    default:
        return snapshot_expectation_pump_t::rejected;
    }
}

bool snapshot_data_entry_shape_valid(
    const byte *application, const worr_native_carrier_view_v1 &view,
    uint16_t entry_index, worr_snapshot_id_v2 &snapshot_id_out)
{
    if (!application || entry_index >= view.entry_count)
        return false;
    const auto &entry = view.entries[entry_index];
    if (entry.entry_type != WORR_NATIVE_CARRIER_ENTRY_DATA_V1 ||
        entry.data_offset > view.packet_bytes ||
        entry.data_bytes > view.packet_bytes - entry.data_offset) {
        return false;
    }
    worr_native_envelope_frame_info_v1 frame{};
    if (Worr_NativeEnvelopeDecodeV1(
            application + entry.data_offset, entry.data_bytes, &frame) !=
            WORR_NATIVE_ENVELOPE_DECODE_OK ||
        frame.transport_epoch != view.transport_epoch ||
        frame.record.reserved0 != 0 ||
        frame.record.record_class != WORR_NATIVE_RECORD_SNAPSHOT_V1 ||
        frame.record.record_schema_version !=
            WORR_SNAPSHOT_ABI_VERSION) {
        return false;
    }
    snapshot_id_out.epoch = frame.record.object_epoch;
    snapshot_id_out.sequence = frame.record.object_sequence;
    return Worr_SnapshotIdValidV2(snapshot_id_out, false);
}

bool admit_current_snapshot_data(
    const byte *application, size_t application_bytes,
    uint16_t entry_index, uint64_t now,
    worr_snapshot_id_v2 snapshot_id)
{
    if (!pilot.snapshot_receiver ||
        snapshot_id.epoch !=
            pilot.snapshot_receiver->snapshot_epoch ||
        now > UINT64_MAX / UINT64_C(1000)) {
        return false;
    }

    const auto expectation =
        observe_snapshot_expectation(snapshot_id);
    if (expectation == snapshot_expectation_pump_t::rejected)
        return false;
    if (expectation == snapshot_expectation_pump_t::stale ||
        expectation ==
            snapshot_expectation_pump_t::retry_later ||
        expectation ==
            snapshot_expectation_pump_t::keyframe_required) {
        return true;
    }

    const auto admitted =
        Worr_NativeSnapshotReceiverAcceptDataV1(
            pilot.snapshot_receiver.get(), now,
            now * UINT64_C(1000), application, application_bytes,
            entry_index);
    switch (admitted) {
    case WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_DUPLICATE:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_PENDING:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_REPEAT_REVALIDATED:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_RETRY_LATER:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_CAPACITY:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_STALE:
    case WORR_NATIVE_SNAPSHOT_RECEIVER_KEYFRAME_REQUIRED:
        return true;
    default:
        return false;
    }
}

netchan_app_rx_result_t pilot_rx_snapshot(
    void *opaque, const netchan_app_rx_info_v1_t *info,
    const byte *application, netchan_app_rx_output_v1_t *output)
{
    if (opaque != &pilot || !live_pilot())
        return NETCHAN_APP_RX_BYPASS;
    if (!info || !output || !application ||
        info->abi_version != NETCHAN_APP_RX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info)) {
        pilot_failure();
        return NETCHAN_APP_RX_REJECT;
    }

    worr_native_carrier_view_v1 view{};
    const auto decoded = Worr_NativeCarrierDecodeV1(
        application, info->application_bytes, &view);
    if (decoded == WORR_NATIVE_CARRIER_NO_CARRIER)
        return NETCHAN_APP_RX_BYPASS;
    if (decoded != WORR_NATIVE_CARRIER_OK) {
        pilot.carrier_traffic_seen = true;
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    uint16_t data_count = 0;
    uint16_t data_index = 0;
    bool has_ack = false;
    for (uint16_t index = 0; index < view.entry_count; ++index) {
        if (view.entries[index].entry_type ==
            WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
            data_index = index;
            ++data_count;
        } else if (view.entries[index].entry_type ==
                   WORR_NATIVE_CARRIER_ENTRY_ACK_V1) {
            has_ack = true;
        } else {
            pilot.carrier_traffic_seen = true;
            enter_drain(true);
            return NETCHAN_APP_RX_REJECT;
        }
    }
    if (data_count > 1) {
        pilot.carrier_traffic_seen = true;
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    worr_snapshot_id_v2 snapshot_id{};
    if (data_count != 0 &&
        !snapshot_data_entry_shape_valid(
            application, view, data_index, snapshot_id)) {
        pilot.carrier_traffic_seen = true;
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }
    if (pilot.cancelled_through_transport_epoch != 0 &&
        view.transport_epoch <=
            pilot.cancelled_through_transport_epoch) {
        counter_increment(telemetry.stale_cancelled_carriers);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }

    pilot.carrier_traffic_seen = true;
    if (!pilot.session_initialized ||
        (pilot.mode != pilot_mode_t::active &&
         pilot.mode != pilot_mode_t::drain)) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    const bool current_epoch =
        view.transport_epoch == pilot.binding.transport_epoch;
    const bool retired_epoch = pilot.retired_session_initialized &&
        view.transport_epoch == pilot.retired_tx.transport_epoch;
    if (current_epoch == retired_epoch ||
        (retired_epoch && data_count != 0) ||
        (pilot.mode == pilot_mode_t::drain && data_count != 0)) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    if (current_epoch && pilot.mode == pilot_mode_t::active) {
        uint64_t gate_now = 0;
        if (!current_tick(gate_now) ||
            !Worr_NativeReadinessCanReceiveNativeV1(
                &pilot.readiness, gate_now)) {
            enter_drain(true);
            return NETCHAN_APP_RX_REJECT;
        }
    }

    worr_native_tx_session_v1 staged_tx = current_epoch
        ? pilot.tx
        : pilot.retired_tx;
    auto staged_slots = current_epoch
        ? pilot.tx_slots
        : pilot.retired_tx_slots;
    auto staged_registry = current_epoch
        ? pilot.payload_registry
        : pilot.retired_payload_registry;
    uint32_t acknowledged = 0;
    if (has_ack &&
        !apply_ack_to_bank(
            staged_tx, staged_slots, staged_registry, application,
            info->application_bytes, acknowledged)) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    if (data_count != 0) {
        uint64_t now = 0;
        if (!current_tick(now) ||
            !admit_current_snapshot_data(
                application, info->application_bytes, data_index, now,
                snapshot_id)) {
            enter_drain(true);
            return NETCHAN_APP_RX_REJECT;
        }
    }

    if (has_ack) {
        if (current_epoch) {
            pilot.tx = staged_tx;
            pilot.tx_slots = staged_slots;
            pilot.payload_registry = staged_registry;
        } else {
            pilot.retired_tx = staged_tx;
            pilot.retired_tx_slots = staged_slots;
            pilot.retired_payload_registry = staged_registry;
        }
        counter_increment(telemetry.ack_carriers);
        if (acknowledged != 0) {
            counter_increment(telemetry.retained_releases);
            counter_increment(telemetry.acknowledged_reliable);
        }
    }

    expose_legacy(output, view.legacy_bytes);
    return NETCHAN_APP_RX_EXPOSE_LEGACY;
}

netchan_app_rx_result_t pilot_rx(
    void *opaque, const netchan_app_rx_info_v1_t *info,
    const byte *application, netchan_app_rx_output_v1_t *output)
{
    if (pilot.event_enabled)
        return pilot_rx_event(opaque, info, application, output);
    if (pilot.snapshot_enabled)
        return pilot_rx_snapshot(opaque, info, application, output);
    return pilot_rx_command_only(opaque, info, application, output);
}

} // namespace

extern "C" void CL_NativeReadinessPilotRegisterCvar(void)
{
    cl_worr_native_shadow = Cvar_Get("cl_worr_native_shadow", "0", 0);
    cl_worr_native_event_shadow = Cvar_Get(
        "cl_worr_native_event_shadow", "0", 0);
    cl_worr_native_snapshot_shadow = Cvar_Get(
        "cl_worr_native_snapshot_shadow", "0", 0);
    cl_worr_native_shadow_probe_hold = Cvar_Get(
        "cl_worr_native_shadow_probe_hold", "0", CVAR_NOARCHIVE);
}

extern "C" bool CL_NativeReadinessPilotBeginConnection(netchan_t *channel)
{
    disable_pilot();
    telemetry = {};
    if (cls.demo.playback || cls.demo.seeking ||
        !channel || channel != &cls.netchan ||
        channel->type != NETCHAN_NEW ||
        channel->app_tx_prepare || channel->app_tx_completion ||
        channel->app_tx_opaque || channel->app_rx ||
        channel->app_rx_opaque) {
        return false;
    }

    /* The opt-in is sampled exactly once per fresh NEW netchan. */
    if (!cl_worr_native_shadow || !cl_worr_native_shadow->integer)
        return false;
    const bool event_enabled =
        cl_worr_native_event_shadow &&
        cl_worr_native_event_shadow->integer;
    const bool snapshot_enabled =
        cl_worr_native_snapshot_shadow &&
        cl_worr_native_snapshot_shadow->integer;
    if (event_enabled && snapshot_enabled)
        return false;

    uint64_t owner;
    if (!allocate_connection_owner(owner))
        return false;
    pilot.enabled = true;
    pilot.event_enabled = event_enabled;
    pilot.snapshot_enabled = snapshot_enabled;
    pilot.channel = channel;
    pilot.connection_owner_id = owner;
    pilot.private_capabilities = pilot.event_enabled
        ? kEventPrivateCapabilities
        : pilot.snapshot_enabled
              ? kSnapshotPrivateCapabilities
              : kCommandPrivateCapabilities;
    pilot.map_quiesced = true;
    pilot.mode = pilot_mode_t::arming;
    pilot.ack_next_bank = event_ack_bank_t::current;
    uint64_t ignored_tick;
    if (!current_tick(ignored_tick) ||
        !Worr_NativeReadinessSidebandParserInitV1(&pilot.parser)) {
        disable_pilot();
        return false;
    }
    if (pilot.event_enabled &&
        (!Worr_EventStreamOwnerInitV1(
             &pilot.event_owner, pilot.connection_owner_id) ||
         !CL_CGameEventRuntimeGetNativeConsumerV1(
             &pilot.event_consumer) ||
         CL_CGameEventRuntimeResetConnection() !=
             WORR_CGAME_EVENT_RUNTIME_OK)) {
        disable_pilot();
        return false;
    }

    if (!Netchan_SetApplicationTxHook(
            channel, pilot_tx_prepare, pilot_tx_completion, &pilot)) {
        disable_pilot();
        return false;
    }
    pilot.tx_hook_registered = true;
    if (!Netchan_SetApplicationRxHook(channel, pilot_rx, &pilot)) {
        disable_pilot();
        return false;
    }
    pilot.rx_hook_registered = true;
    return true;
}

extern "C" void CL_NativeReadinessPilotBeforeNetchanClose(
    netchan_t *channel)
{
    if (channel && pilot.channel == channel)
        disable_pilot();
}

extern "C" void CL_NativeReadinessPilotQuiesceMap(void)
{
    (void)enter_map_boundary();
}

extern "C" void CL_NativeReadinessPilotServerDataReset(void)
{
    if (!enter_map_boundary())
        return;
    pilot.capability_confirmed = false;
    pilot.builder_initialized = false;
    pilot.builder = {};
    pilot.command_ring = {};
    if (!Worr_NativeReadinessSidebandParserInitV1(&pilot.parser))
        pilot_failure();
}

extern "C" void CL_NativeReadinessPilotPacketBegin(void)
{
    if (!live_pilot())
        return;
    uint64_t now;
    if (!current_tick(now)) {
        pilot_failure();
        return;
    }
    const auto result =
        Worr_NativeReadinessSidebandPacketBeginV1(&pilot.parser);
    if (result != WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED) {
        pilot_failure();
        return;
    }

    if (pilot.readiness_initialized) {
        worr_native_readiness_state_v1 next = pilot.readiness;
        if (Worr_NativeReadinessCheckDeadlineV1(&next, now) !=
            WORR_NATIVE_READINESS_OK) {
            pilot_failure();
            return;
        }
        pilot.readiness = next;
    }
}

extern "C" void CL_NativeReadinessPilotPacketEnd(void)
{
    if (!live_pilot())
        return;
    const auto result =
        Worr_NativeReadinessSidebandPacketEndV1(&pilot.parser);
    if (result != WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED)
        pilot_failure();
}

extern "C" bool CL_NativeReadinessPilotObserveSetting(
    int32_t index, int32_t value)
{
    const bool carrier = CL_NativeReadinessPilotIsCarrierSetting(index);
    if (!live_pilot())
        return carrier;

    const auto result = Worr_NativeReadinessSidebandObserveSvcSettingV1(
        &pilot.parser, index, value);
    if (result == WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED) {
        worr_native_readiness_record_v1 record{};
        if (Worr_NativeReadinessSidebandTakeRecordV1(
                &pilot.parser, &record) !=
                WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN ||
            !observe_record(record)) {
            pilot_failure();
        }
        return carrier;
    }
    if (!accepted_observe_result(result))
        pilot_failure();
    return carrier;
}

extern "C" void CL_NativeReadinessPilotObserveInterveningService(void)
{
    if (!live_pilot())
        return;
    const auto result =
        Worr_NativeReadinessSidebandObserveInterveningServiceV1(
            &pilot.parser);
    if (result != WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND)
        pilot_failure();
}

extern "C" void CL_NativeReadinessPilotCapabilityConfirmed(
    const worr_net_capability_state_v1 *capability_state)
{
    if (!live_pilot())
        return;
    if (!capability_is_exact_legacy_confirmation(capability_state)) {
        pilot_failure();
        return;
    }
    if (pilot.builder_initialized) {
        if (pilot.builder.command_epoch !=
            capability_state->connection_epoch) {
            pilot_failure();
            return;
        }
    } else {
        worr_native_command_shadow_builder_v1 builder{};
        if (!Worr_NativeCommandShadowBuilderInitV1(
                &builder, capability_state->connection_epoch,
                WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS)) {
            pilot_failure();
            return;
        }
        pilot.builder = builder;
        pilot.builder_initialized = true;
        pilot.command_ring = {};
    }
    pilot.capability_confirmed = true;
}

extern "C" void CL_NativeReadinessPilotObserveFinalizedCommand(
    uint32_t legacy_command_number,
    const worr_command_id_v1 *command_id,
    const worr_prediction_command_v1 *command)
{
    if (!live_pilot())
        return;
    if (!pilot.builder_initialized || !command_id || !command ||
        legacy_command_number == 0) {
        pilot_failure();
        return;
    }

    worr_command_record_v1 record{};
    if (Worr_NativeCommandShadowBuilderBuildV1(
            &pilot.builder, *command_id, command, &record) !=
        WORR_NATIVE_COMMAND_SHADOW_BUILD_BUILT) {
        pilot_failure();
        return;
    }
    command_ring_entry_t &entry =
        pilot.command_ring[legacy_command_number & CMD_MASK];
    entry.valid = true;
    entry.legacy_command_number = legacy_command_number;
    entry.record = record;
}

extern "C" void CL_NativeReadinessPilotObserveEncodedCommandRange(
    uint32_t first_legacy_command_number, uint32_t command_count)
{
    if (!live_pilot() || pilot.mode != pilot_mode_t::active ||
        !pilot.session_initialized)
        return;
    if (pilot.tx.retained_count > kTxSlotCapacity) {
        pilot_failure();
        return;
    }
    /* The pilot is deliberately stop-and-wait.  Legacy command encoding is
     * never stalled while the one retained native sample awaits its receipt. */
    if (pilot.tx.retained_count != 0)
        return;
    if (first_legacy_command_number == 0 || command_count == 0 ||
        command_count - 1u >
            UINT32_MAX - first_legacy_command_number) {
        pilot_failure();
        return;
    }
    const uint32_t latest =
        first_legacy_command_number + command_count - 1u;
    const command_ring_entry_t &entry =
        pilot.command_ring[latest & CMD_MASK];
    if (!entry.valid || entry.legacy_command_number != latest) {
        pilot_failure();
        return;
    }
    const worr_command_id_v1 command_id = entry.record.command_id;
    if (!Worr_CommandIdValidV1(command_id, false)) {
        pilot_failure();
        return;
    }
    if (pilot.last_enqueued_command_valid) {
        if (command_id.epoch != pilot.last_enqueued_command_id.epoch) {
            pilot_failure();
            return;
        }
        /* Repeated MOVE/BATCH range notifications and older ring entries are
         * observational no-ops.  A later sample may skip commands that passed
         * while stop-and-wait was occupied; legacy remains authoritative. */
        if (command_id.sequence <=
            pilot.last_enqueued_command_id.sequence) {
            return;
        }
    }
    if (cl_worr_native_shadow_probe_hold &&
        cl_worr_native_shadow_probe_hold->integer) {
        return;
    }

    auto next_registry = pilot.payload_registry;
    worr_native_tx_session_v1 next_tx = pilot.tx;
    auto next_slots = pilot.tx_slots;
    uint32_t handle = 0;
    const worr_native_command_shadow_payload_result_v1 retained =
        Worr_NativeCommandShadowPayloadRetainV1(
            &next_registry, &entry.record, &handle);
    if (retained == WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY_STALL) {
        /* A bounded observational stall never perturbs legacy transmission. */
        return;
    }
    if (retained != WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RETAINED) {
        pilot_failure();
        return;
    }

    uint64_t now;
    if (!current_tick(now)) {
        pilot_failure();
        return;
    }
    uint32_t message_sequence = 0;
    const worr_native_tx_result_v1 enqueued =
        Worr_NativeTxSessionEnqueueV1(
            &next_tx, next_slots.data(), kTxSlotCapacity,
            command_record_ref(entry.record.command_id), kProofPriority,
            handle, kCommandPayloadBytes, kCommandDatagramBytes, now,
            &message_sequence);
    if (enqueued == WORR_NATIVE_TX_CAPACITY ||
        enqueued == WORR_NATIVE_TX_RECEIPT_WINDOW) {
        return;
    }
    if (enqueued != WORR_NATIVE_TX_RETAINED || message_sequence == 0 ||
        next_tx.retained_count != 1 ||
        next_registry.occupied_count != 1) {
        pilot_failure();
        return;
    }

    pilot.payload_registry = next_registry;
    pilot.tx = next_tx;
    pilot.tx_slots = next_slots;
    pilot.last_enqueued_command_id = command_id;
    pilot.last_enqueued_command_valid = true;
    counter_increment(telemetry.proof_enqueued);
    const uint64_t retained_total = next_tx.retained_count +
        (pilot.retired_session_initialized
             ? pilot.retired_tx.retained_count
             : 0u);
    if (telemetry.retained_highwater < retained_total)
        telemetry.retained_highwater = retained_total;
}

extern "C" bool CL_NativeReadinessPilotOwnsSnapshotTimeline(void)
{
    if (!pilot.enabled || !pilot.snapshot_enabled ||
        !pilot.snapshot_timeline_owned || pilot.map_quiesced) {
        return false;
    }

    /*
     * Validate attachment for its fail-closed side effect, but never make
     * transport liveness the authority decision for an already-bound epoch.
     */
    (void)live_pilot();
    return pilot.enabled && pilot.snapshot_timeline_owned &&
           !pilot.map_quiesced;
}

extern "C" void
CL_NativeReadinessPilotSnapshotExpectationReady(void)
{
    if (!CL_NativeReadinessPilotOwnsSnapshotTimeline())
        return;

    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    worr_snapshot_ref_v2 ref{};
    if (!CL_SnapshotShadowLatest(&view, &hashes, &ref) ||
        !view.snapshot ||
        !Worr_SnapshotIdValidV2(
            view.snapshot->snapshot_id, false)) {
        CL_NativeReadinessPilotSnapshotExpectationFailed();
        return;
    }

    /* SERVER_ACTIVE may not have arrived yet.  The snapshot shadow retains
     * this independently qualified expectation; DATA admission will look it
     * up by exact ID once the heap-owned receiver exists. */
    if (!pilot.snapshot_receiver)
        return;

    const auto result = observe_snapshot_expectation(
        view.snapshot->snapshot_id);
    if (result == snapshot_expectation_pump_t::rejected ||
        result == snapshot_expectation_pump_t::pending ||
        result == snapshot_expectation_pump_t::stale) {
        CL_NativeReadinessPilotSnapshotExpectationFailed();
    }
}

extern "C" void
CL_NativeReadinessPilotSnapshotExpectationFailed(void)
{
    if (!CL_NativeReadinessPilotOwnsSnapshotTimeline())
        return;
    if (pilot.snapshot_receiver) {
        (void)Worr_NativeSnapshotReceiverQuarantineV1(
            pilot.snapshot_receiver.get());
    }
    enter_drain(true);
}

extern "C" bool CL_NativeReadinessPilotOutputDue(void)
{
    const bool active_traffic = pilot.mode == pilot_mode_t::active &&
                                !pilot.map_quiesced;
    const bool drain_ack_service = map_drain_ack_service_active();
    if (!pilot.enabled || (!active_traffic && !drain_ack_service) ||
        !pilot.session_initialized ||
        cls.demo.playback || cls.demo.seeking ||
        !hooks_attached_exact() || !pilot.channel ||
        pilot.channel->maxpacketlen == 0)
        return false;

    uint64_t now = 0;
    if (!projected_tick(now))
        return false;
    if (active_traffic) {
        auto readiness = pilot.readiness;
        if (!Worr_NativeReadinessCanTransmitNativeV1(
                &readiness, now)) {
            return false;
        }
    }

    if (semantic_ack_enabled()) {
        auto *const current_ledger =
            current_semantic_ack_ledger();
        auto *const retired_ledger =
            retired_semantic_ack_ledger();
        if (!current_ledger ||
            (pilot.retired_session_initialized &&
             !retired_ledger)) {
            return false;
        }
        bool due = false;
        if (!event_ack_due(*current_ledger, now, due))
            return false;
        if (due)
            return true;
        if (pilot.retired_session_initialized) {
            if (!event_ack_due(*retired_ledger, now, due))
                return false;
            if (due)
                return true;
        }
    }

    /* The quiesced exception above services only pre-authorized ACK ledgers.
     * It must never fall through to current command DATA scheduling. */
    if (drain_ack_service)
        return false;

    if (pilot.tx.retained_count == 0)
        return false;
    if ((pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0)
        return false;
    if ((pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0)
        return true;

    const uint16_t application_budget = static_cast<uint16_t>(
        pilot.channel->maxpacketlen > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES
            ? WORR_NATIVE_CARRIER_MAX_PACKET_BYTES
            : pilot.channel->maxpacketlen);
    if (application_budget == 0)
        return false;
    auto gate = pilot.tx_gate;
    worr_native_carrier_dispatch_v1 dispatch{};
    if (semantic_ack_enabled()) {
        return Worr_NativeCarrierMixedBeginV1(
                   &gate, &pilot.tx, pilot.tx_slots.data(),
                   kTxSlotCapacity, now, kResendIntervalMilliseconds,
                   application_budget, 0, &dispatch) ==
               WORR_NATIVE_CARRIER_MIXED_OK;
    }
    return Worr_NativeCarrierSessionDispatchBeginV1(
               &gate, &pilot.tx, pilot.tx_slots.data(), kTxSlotCapacity,
               now, kResendIntervalMilliseconds, application_budget, 0,
               &dispatch) == WORR_NATIVE_CARRIER_SESSION_OK;
}

extern "C" bool CL_NativeReadinessPilotIsCarrierSetting(int32_t index)
{
    return index >= WORR_NATIVE_READINESS_SETTING_BEGIN &&
           index <= WORR_NATIVE_READINESS_SETTING_COMMIT;
}

extern "C" bool CL_NativeReadinessPilotGetStatusV1(
    cl_native_readiness_pilot_status_v1 *status_out)
{
    if (!status_out)
        return false;

    cl_native_readiness_pilot_status_v1 status{};
    status.struct_size = sizeof(status);
    status.schema_version = CL_NATIVE_READINESS_PILOT_STATUS_ABI_V1;
    status.enabled = pilot.enabled ? 1u : 0u;
    status.mode = pilot.enabled ? static_cast<uint32_t>(pilot.mode) : 0u;
    status.hooks = hooks_attached_exact() ? 1u : 0u;
    status.capability_confirmed = pilot.capability_confirmed ? 1u : 0u;
    status.readiness_phase = pilot.readiness_initialized
                                 ? pilot.readiness.phase
                                 : WORR_NATIVE_READINESS_PHASE_RESET;
    status.official_epoch =
        pilot.builder_initialized ? pilot.builder.command_epoch : 0u;
    status.transport_epoch = pilot.readiness_initialized
                                 ? pilot.readiness.transport_epoch
                                 : 0u;
    status.protocol = static_cast<uint32_t>(cls.serverProtocol);
    status.public_mask = WORR_NET_CAP_LEGACY_STAGE_MASK;
    status.private_mask = pilot.enabled
                              ? pilot.private_capabilities
                              : kCommandPrivateCapabilities;
    status.probe_hold = cl_worr_native_shadow_probe_hold &&
                                cl_worr_native_shadow_probe_hold->integer
                            ? 1u
                            : 0u;
    status.cancelled_through_transport_epoch =
        pilot.cancelled_through_transport_epoch;
    status.challenges = telemetry.challenges;
    status.client_ready_queued = telemetry.client_ready_queued;
    status.server_active = telemetry.server_active;
    status.proof_enqueued = telemetry.proof_enqueued;
    status.retained =
        (pilot.session_initialized ? pilot.tx.retained_count : 0u) +
        (pilot.retired_session_initialized
             ? pilot.retired_tx.retained_count
             : 0u);
    status.retained_highwater = telemetry.retained_highwater;
    status.retained_releases = telemetry.retained_releases;
    status.tx_first_sends = telemetry.tx_first_sends;
    status.tx_retries = telemetry.tx_retries;
    status.tx_handoffs = telemetry.tx_handoffs;
    status.ack_carriers = telemetry.ack_carriers;
    status.acknowledged_reliable = telemetry.acknowledged_reliable;
    status.drains = telemetry.drains;
    status.failures = telemetry.failures;
    status.cancellation_barriers = telemetry.cancellation_barriers;
    status.cancelled_transports = telemetry.cancelled_transports;
    status.cancelled_command_tx = telemetry.cancelled_commands;
    status.cancelled_event_rx = telemetry.cancelled_event_rx;
    status.cancelled_event_receipts =
        telemetry.cancelled_event_receipts;
    status.stale_cancelled_carriers =
        telemetry.stale_cancelled_carriers;
    status.stale_cancelled_readiness_records =
        telemetry.stale_cancelled_readiness_records;
    status.last_failure = telemetry.last_failure;
    *status_out = status;
    return true;
}

#if defined(WORR_NATIVE_READINESS_PILOT_TESTING)
extern "C" bool CL_NativeReadinessPilotGetTestState(
    cl_native_readiness_pilot_test_state_t *state_out)
{
    if (!state_out || !pilot.enabled)
        return false;
    cl_native_readiness_pilot_test_state_t state{};
    state.transport_epoch =
        pilot.session_initialized ? pilot.tx.transport_epoch : 0;
    state.retained_messages =
        pilot.session_initialized ? pilot.tx.retained_count : 0;
    state.retained_payloads =
        pilot.session_initialized
            ? pilot.payload_registry.occupied_count
            : 0;
    state.retired_transport_epoch =
        pilot.retired_session_initialized
            ? pilot.retired_tx.transport_epoch
            : 0;
    state.retired_messages =
        pilot.retired_session_initialized
            ? pilot.retired_tx.retained_count
            : 0;
    state.retired_payloads =
        pilot.retired_session_initialized
            ? pilot.retired_payload_registry.occupied_count
            : 0;
    state.message_sequence_highwater =
        pilot.session_initialized
            ? pilot.tx.next_message_sequence - 1u
            : 0;
    state.selected_send_attempts =
        pilot.session_initialized && pilot.tx_slots[0].state_flags != 0
            ? pilot.tx_slots[0].send_attempts
            : 0;
    state.mode = static_cast<uint32_t>(pilot.mode);
    state.private_capabilities = pilot.private_capabilities;
    state.event_rx_occupied =
        pilot.event_enabled && pilot.session_initialized
            ? pilot.event_rx.occupied_count
            : 0;
    state.event_ack_receipts =
        pilot.event_enabled && pilot.session_initialized
            ? pilot.event_ack_ledger.receipt_count
            : 0;
    state.retired_event_rx_occupied =
        pilot.event_enabled && pilot.retired_session_initialized
            ? pilot.retired_event_rx.occupied_count
            : 0;
    state.retired_event_ack_receipts =
        pilot.event_enabled && pilot.retired_session_initialized
            ? pilot.retired_event_ack_ledger.receipt_count
            : 0;
    state.event_owner_flags = pilot.event_enabled
                                  ? pilot.event_owner.state_flags
                                  : 0;
    state.event_owner_epoch_high_water = pilot.event_enabled
                                             ? pilot.event_owner
                                                   .epoch_high_water
                                             : 0;
    state.snapshot_epoch =
        pilot.snapshot_enabled && pilot.snapshot_receiver
            ? pilot.snapshot_receiver->snapshot_epoch
            : 0;
    state.snapshot_receiver_flags =
        pilot.snapshot_enabled && pilot.snapshot_receiver
            ? pilot.snapshot_receiver->state_flags
            : 0;
    state.snapshot_rx_occupied =
        pilot.snapshot_enabled && pilot.snapshot_receiver
            ? pilot.snapshot_receiver->rx.occupied_count
            : 0;
    state.snapshot_ack_receipts =
        pilot.snapshot_enabled && pilot.snapshot_receiver
            ? pilot.snapshot_receiver->ack_ledger.receipt_count
            : 0;
    state.ack_next_bank = semantic_ack_enabled()
                              ? static_cast<uint32_t>(pilot.ack_next_bank)
                              : 0;
    state.cancelled_through_transport_epoch =
        pilot.cancelled_through_transport_epoch;
    state.cancellation_barriers = telemetry.cancellation_barriers;
    state.cancelled_transports = telemetry.cancelled_transports;
    state.cancelled_commands = telemetry.cancelled_commands;
    state.cancelled_event_rx = telemetry.cancelled_event_rx;
    state.cancelled_event_receipts =
        telemetry.cancelled_event_receipts;
    state.stale_cancelled_carriers =
        telemetry.stale_cancelled_carriers;
    state.stale_cancelled_readiness_records =
        telemetry.stale_cancelled_readiness_records;
    state.hooks_installed =
        pilot.tx_hook_registered && pilot.rx_hook_registered;
    state.readiness_committed = pilot.readiness_committed;
    /* Retain the testing field name for source compatibility with the
     * original one-command pilot; it now means that this transport epoch has
     * successfully enqueued at least one native command. */
    state.proof_enqueued_once = pilot.last_enqueued_command_valid;
    state.carrier_traffic_seen = pilot.carrier_traffic_seen;
    state.event_enabled = pilot.event_enabled;
    state.snapshot_enabled = pilot.snapshot_enabled;
    state.client_active_confirm_queued =
        pilot.client_active_confirm_queued;
    *state_out = state;
    return true;
}
#endif
