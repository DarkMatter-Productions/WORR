/*
 * Deterministic full-duplex impairment coverage for the live FR-10 native
 * command/event shadow adapters.  This is a hook-level virtual link: it uses
 * the exported client/server lifecycle APIs and their real netchan callbacks,
 * but never creates a socket or launches a visible client.
 */

#include "client.h"
#include "client/cgame_event_runtime.h"
#include "client/native_readiness_pilot.h"
#include "client/snapshot_shadow.h"
#include "server/native_shadow.h"

#include "common/net/legacy_entity_event_candidate.h"
#include "common/net/legacy_temp_event_candidate.h"
#include "common/net/native_carrier.h"
#include "common/net/native_readiness_sideband.h"
#include "cg_event_runtime.hpp"
#include "cg_local_interaction.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

client_static_t cls{};
client_state_t cl{};
unsigned com_localTime{};

namespace {

constexpr uint32_t kPublicCapabilities =
    WORR_NET_CAP_LEGACY_STAGE_MASK;
constexpr uint32_t kPrivateEventCapabilities =
    WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
constexpr uint32_t kApplicationCeiling = 1024;

cvar_t shadow_cvar{};
cvar_t event_shadow_cvar{};
cvar_t snapshot_shadow_cvar{};
cvar_t probe_hold_cvar{};
std::array<byte, 1024> reliable_storage{};

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr, "native_event_virtual_link_test:%d: %s\n",
                 line, expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

struct fake_event_runtime_t {
    bool active{};
    uint32_t stream_epoch{};
    uint32_t first_sequence{};
    uint32_t next_sequence{};
    uint32_t authority_count{};
    uint32_t reset_calls{};
    uint32_t submit_calls{};
    worr_event_receipt_ack_v1 receipt{};
};

fake_event_runtime_t fake_event_runtime{};

constexpr uint32_t kInteractionPredecessorLegacySequence = 116;
constexpr uint32_t kInteractionRequestLegacySequence = 117;
constexpr uint32_t kInteractionPostRequestLegacySequence = 118;
std::array<worr_cgame_command_record_entry_v1, 3>
    interaction_command_records{};

worr_command_record_v1 interaction_command_record(
    uint32_t sequence, bool hook_held)
{
    worr_command_record_v1 record{};
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id = {91, sequence};
    record.sample_time_us = static_cast<uint64_t>(sequence) * 16000u;
    record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    record.command.struct_size = sizeof(record.command);
    record.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    record.command.duration_ms = 16;
    if (hook_held)
        record.command.buttons = WORR_LOCAL_INTERACTION_HOOK_BUTTON;
    record.render_watermark.struct_size = sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    CHECK(Worr_CommandRecordCanonicalizeV1(
        &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    return record;
}

uint32_t resolve_interaction_command_range(
    uint32_t first_legacy_sequence, uint32_t command_count,
    worr_cgame_command_record_range_v1 *range_out)
{
    worr_cgame_command_record_range_v1 output{};
    if (!range_out)
        return WORR_CGAME_COMMAND_RECORD_INVALID_ARGUMENT;

    output.struct_size = sizeof(output);
    output.api_version = WORR_CGAME_COMMAND_RECORD_API_VERSION;
    output.flags = WORR_CGAME_COMMAND_RECORD_CANONICAL;
    output.first_legacy_sequence = first_legacy_sequence;
    if (command_count == 1 &&
        first_legacy_sequence >= kInteractionPredecessorLegacySequence &&
        first_legacy_sequence <= kInteractionPostRequestLegacySequence) {
        output.command_count = 1;
        output.commands[0] = interaction_command_records[
            first_legacy_sequence - kInteractionPredecessorLegacySequence];
        output.result = WORR_CGAME_COMMAND_RECORD_OK;
    } else {
        output.result = WORR_CGAME_COMMAND_RECORD_HISTORY_MISSING;
    }
    *range_out = output;
    return output.result;
}

const worr_cgame_command_record_import_v1 interaction_command_import{
    sizeof(interaction_command_import),
    WORR_CGAME_COMMAND_RECORD_API_VERSION,
    resolve_interaction_command_range,
};

worr_local_interaction_authority_receipt_v1
configure_interaction_prediction(bool hook_active_after)
{
    worr_local_interaction_state_v1 state_before{};
    worr_local_interaction_intent_v1 intent{};
    worr_local_interaction_transaction_v1 authoritative{};
    worr_local_interaction_authority_receipt_v1 receipt{};

    interaction_command_records = {};
    interaction_command_records[0].legacy_sequence =
        kInteractionPredecessorLegacySequence;
    interaction_command_records[0].command =
        interaction_command_record(16, false);
    interaction_command_records[1].legacy_sequence =
        kInteractionRequestLegacySequence;
    interaction_command_records[1].command =
        interaction_command_record(17, true);
    interaction_command_records[2].legacy_sequence =
        kInteractionPostRequestLegacySequence;
    interaction_command_records[2].command =
        interaction_command_record(18, true);
    CHECK(Worr_CommandRecordValidateV1(
        &interaction_command_records[0].command,
        WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(Worr_CommandRecordValidateV1(
        &interaction_command_records[1].command,
        WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(Worr_CommandRecordValidateV1(
        &interaction_command_records[2].command,
        WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));

    intent.struct_size = sizeof(intent);
    intent.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
    intent.flags = WORR_LOCAL_INTERACTION_INTENT_HOOK_HELD;
    CHECK(Worr_LocalInteractionRebaseBeforeCommandV1(
        &interaction_command_records[1].command, false, false,
        &state_before));
    CHECK(Worr_LocalInteractionBuildAuthoritativeHookV1(
        &state_before, &interaction_command_records[1].command, &intent,
        hook_active_after, &authoritative));
    CHECK(Worr_LocalInteractionAuthorityReceiptBuildV1(&authoritative,
                                                        &receipt));
    return receipt;
}

void install_configured_interaction_import()
{
    CG_LocalInteractionSetImport(&interaction_command_import);
}

void predict_configured_interaction()
{
    worr_cgame_prediction_input_range_v1 range{};
    range.struct_size = sizeof(range);
    range.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    range.result = WORR_CGAME_PREDICTION_INPUT_OK;
    range.source = WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    range.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    range.authoritative_legacy_sequence =
        kInteractionPredecessorLegacySequence;
    range.current_legacy_sequence = kInteractionRequestLegacySequence;
    range.command_count = 1;
    range.commands[0].legacy_sequence = kInteractionRequestLegacySequence;
    range.commands[0].command_id =
        interaction_command_records[1].command.command_id;
    range.commands[0].command = interaction_command_records[1].command.command;
    CG_LocalInteractionPredict(range);
}

void predict_past_configured_interaction()
{
    worr_cgame_prediction_input_range_v1 range{};
    range.struct_size = sizeof(range);
    range.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    range.result = WORR_CGAME_PREDICTION_INPUT_OK;
    range.source = WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    range.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    range.authoritative_legacy_sequence = kInteractionRequestLegacySequence;
    range.current_legacy_sequence = kInteractionPostRequestLegacySequence;
    range.command_count = 1;
    range.commands[0].legacy_sequence =
        kInteractionPostRequestLegacySequence;
    range.commands[0].command_id =
        interaction_command_records[2].command.command_id;
    range.commands[0].command = interaction_command_records[2].command.command;
    CG_LocalInteractionPredict(range);
}

worr_cgame_event_runtime_result_v1 fake_reset_authority(
    uint32_t stream_epoch, uint32_t first_sequence)
{
    ++fake_event_runtime.reset_calls;
    if ((stream_epoch == 0) != (first_sequence == 0))
        return WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT;
    fake_event_runtime.active = stream_epoch != 0;
    fake_event_runtime.stream_epoch = stream_epoch;
    fake_event_runtime.first_sequence = first_sequence;
    fake_event_runtime.next_sequence = first_sequence;
    fake_event_runtime.authority_count = 0;
    if (stream_epoch != 0 &&
        !Worr_EventReceiptInitV1(&fake_event_runtime.receipt,
                                 stream_epoch)) {
        return WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT;
    }
    return WORR_CGAME_EVENT_RUNTIME_OK;
}

worr_cgame_event_runtime_result_v1 fake_submit_authority(
    const worr_event_record_v1 *records, uint32_t count)
{
    if (!fake_event_runtime.active || !records || count == 0)
        return WORR_CGAME_EVENT_RUNTIME_NOT_READY;
    for (uint32_t index = 0; index < count; ++index) {
        if (!Worr_EventRecordValidateV1(
                &records[index], WORR_EVENT_STREAM_MAX_ENTITIES_V1) ||
            records[index].event_id.stream_epoch !=
                fake_event_runtime.stream_epoch) {
            return WORR_CGAME_EVENT_RUNTIME_CONFLICT;
        }
        if (Worr_EventReceiptContainsV1(&fake_event_runtime.receipt,
                                        records[index].event_id)) {
            return WORR_CGAME_EVENT_RUNTIME_DUPLICATE;
        }
        if (Worr_EventReceiptMarkV1(&fake_event_runtime.receipt,
                                    records[index].event_id) !=
            WORR_EVENT_RECEIPT_ACCEPTED) {
            return WORR_CGAME_EVENT_RUNTIME_CONFLICT;
        }
        ++fake_event_runtime.authority_count;
    }
    fake_event_runtime.next_sequence =
        fake_event_runtime.receipt.highest_contiguous + 1u;
    ++fake_event_runtime.submit_calls;
    return WORR_CGAME_EVENT_RUNTIME_OK;
}

bool fake_get_status(worr_cgame_event_runtime_status_v1 *status_out)
{
    if (!status_out)
        return false;
    worr_cgame_event_runtime_status_v1 status{};
    status.struct_size = sizeof(status);
    status.api_version = WORR_CGAME_EVENT_RUNTIME_API_VERSION;
    if (fake_event_runtime.active) {
        status.authority_epoch = fake_event_runtime.stream_epoch;
        status.next_presentation_sequence =
            fake_event_runtime.next_sequence;
        status.authority_count = fake_event_runtime.authority_count;
        status.state_flags = WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE;
        status.receipt = fake_event_runtime.receipt;
    }
    *status_out = status;
    return true;
}

const worr_cgame_event_runtime_export_v1 fake_event_export{
    sizeof(fake_event_export),
    WORR_CGAME_EVENT_RUNTIME_API_VERSION,
    fake_reset_authority,
    fake_submit_authority,
    fake_get_status,
};

struct wire_packet_t {
    std::array<byte, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES> bytes{};
    size_t count{};
    netchan_app_tx_prepare_output_v1_t completion{};
};

struct fixture_t {
    sv_native_shadow_peer_v1 server{};
    netchan_t server_channel{};
    bool server_live{};
    uint32_t client_rx_sequence{1};
    uint32_t server_rx_sequence{1};
};

struct impairment_metrics_t {
    uint32_t converged_scenarios{};
    uint32_t server_to_client_losses{};
    uint32_t client_to_server_losses{};
    uint32_t ack_losses{};
    uint32_t reordered_deliveries{};
    uint32_t duplicate_deliveries{};
    uint32_t corrupt_server_to_client{};
    uint32_t corrupt_client_to_server{};
    uint32_t cancelled_ack_strips{};
    uint32_t cancelled_data_strips{};
    uint32_t cancelled_corrupt_rejections{};
    uint32_t exactly_once_presentations{};
};

impairment_metrics_t metrics{};

/* This hard-gated lifecycle proof is intentionally printed separately from
 * metrics_digest(): it records the exact cancellation disposition that
 * closes the finite proactive-ACK-credit gap across two map rotations. */
struct lifecycle_exhaustion_diagnostic_t {
    uint32_t accepted_lost_ack_handoffs{};
    uint32_t client_due_after_exhaustion{};
    uint32_t old_retained_before_rotation{};
    uint32_t client_cancelled_receipts{};
    uint32_t server_cancelled_events{};
    uint32_t client_cancellation_floor{};
    uint32_t server_cancellation_floor{};
    uint32_t retired_ack_receipts_after_rotation{};
    uint32_t retired_retained_after_rotation{};
    uint32_t release_due{};
    uint32_t second_rotation_without_retired_bank{};
    uint32_t delayed_old_ack_stripped{};
    uint32_t delayed_old_data_stripped{};
    uint32_t corrupt_old_rejections{};
};

lifecycle_exhaustion_diagnostic_t lifecycle_diagnostic{};

uint64_t metrics_digest()
{
    uint64_t digest = UINT64_C(1469598103934665603);
    const auto *bytes = reinterpret_cast<const uint8_t *>(&metrics);
    for (size_t index = 0; index < sizeof(metrics); ++index) {
        digest ^= bytes[index];
        digest *= UINT64_C(1099511628211);
    }
    return digest;
}

void feed_record_to_client(const worr_native_readiness_record_v1 &record)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT> pairs{};
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs.data(), static_cast<uint32_t>(pairs.size())));
    CL_NativeReadinessPilotPacketBegin();
    for (const auto &pair : pairs) {
        CHECK(CL_NativeReadinessPilotObserveSetting(
            static_cast<int32_t>(pair.index),
            static_cast<int32_t>(pair.value)));
    }
    CL_NativeReadinessPilotPacketEnd();
}

worr_native_readiness_record_v1 decode_client_record(size_t offset)
{
    constexpr size_t kClcSettingWireBytes = 5u;
    constexpr size_t kReadinessRecordWireBytes =
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT *
        kClcSettingWireBytes;
    CHECK(cls.netchan.message.cursize >=
          offset + kReadinessRecordWireBytes);
    worr_native_readiness_sideband_parser_v1 parser{};
    CHECK(Worr_NativeReadinessSidebandParserInitV1(&parser));
    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED);
    const byte *wire = cls.netchan.message.data + offset;
    for (uint32_t index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT; ++index) {
        const byte *field = wire + index * kClcSettingWireBytes;
        CHECK(field[0] == static_cast<byte>(clc_setting));
        const auto setting_index = static_cast<int16_t>(
            static_cast<uint16_t>(field[1]) |
            (static_cast<uint16_t>(field[2]) << 8));
        const auto setting_value = static_cast<int16_t>(
            static_cast<uint16_t>(field[3]) |
            (static_cast<uint16_t>(field[4]) << 8));
        const auto result = Worr_NativeReadinessSidebandObservePairV1(
            &parser, setting_index, setting_value);
        CHECK(result == WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED ||
              result == WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED);
    }
    worr_native_readiness_record_v1 record{};
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, &record) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
    return record;
}

sv_native_shadow_observe_result_v1 feed_record_to_server(
    fixture_t &fixture, const worr_native_readiness_record_v1 &record,
    uint32_t now,
    worr_native_readiness_record_v1 *server_active_out = nullptr)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT> pairs{};
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs.data(), static_cast<uint32_t>(pairs.size())));
    CHECK(SV_NativeShadowPacketBeginV1(&fixture.server, now));
    sv_native_shadow_observe_result_v1 result =
        SV_NATIVE_SHADOW_OBSERVE_NOT_SIDEBAND;
    worr_native_readiness_record_v1 scratch{};
    for (const auto &pair : pairs) {
        result = SV_NativeShadowObserveSettingV1(
            &fixture.server, pair.index, pair.value,
            server_active_out ? server_active_out : &scratch);
    }
    CHECK(SV_NativeShadowPacketEndV1(&fixture.server));
    return result;
}

void confirm_public_capability(uint32_t official_epoch)
{
    worr_net_capability_state_v1 state{};
    CHECK(Worr_NetCapabilityStateInitV1(
        &state, official_epoch, kPublicCapabilities,
        kPublicCapabilities));
    worr_net_capability_confirm_v1 confirm{};
    confirm.struct_size = sizeof(confirm);
    confirm.schema_version = WORR_NET_CAPABILITY_VERSION;
    confirm.connection_epoch = official_epoch;
    confirm.supported = kPublicCapabilities;
    confirm.negotiated = kPublicCapabilities;
    CHECK(Worr_NetCapabilityConfirmV1(&state, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CL_NativeReadinessPilotCapabilityConfirmed(&state);
}

worr_native_readiness_record_v1 activate_epoch(
    fixture_t &fixture, uint32_t official_epoch, uint32_t now)
{
    com_localTime = now;
    confirm_public_capability(official_epoch);
    worr_native_readiness_record_v1 challenge{};
    CHECK(SV_NativeShadowBeginEpochV1(
        &fixture.server, official_epoch, kPublicCapabilities,
        kPublicCapabilities, now, &challenge));
    CHECK(challenge.negotiated_capabilities ==
              kPrivateEventCapabilities &&
          challenge.snapshot_epoch == 0);

    const size_t ready_offset = cls.netchan.message.cursize;
    feed_record_to_client(challenge);
    const auto client_ready = decode_client_record(ready_offset);
    CHECK(client_ready.record_kind ==
              WORR_NATIVE_READINESS_RECORD_CLIENT_READY &&
          client_ready.snapshot_epoch == 0);

    worr_native_readiness_record_v1 server_active{};
    CHECK(feed_record_to_server(
              fixture, client_ready, now + 1u, &server_active) ==
          SV_NATIVE_SHADOW_OBSERVE_SERVER_ACTIVE_READY);
    CHECK(server_active.snapshot_epoch == 0);
    CHECK(SV_NativeShadowServerActiveQueuedV1(&fixture.server));

    const size_t confirm_offset = cls.netchan.message.cursize;
    com_localTime = now + 1u;
    feed_record_to_client(server_active);
    const auto active_confirm = decode_client_record(confirm_offset);
    CHECK(active_confirm.record_kind ==
              WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM &&
          active_confirm.snapshot_epoch == 0);
    CHECK(feed_record_to_server(
              fixture, active_confirm, now + 2u) ==
          SV_NATIVE_SHADOW_OBSERVE_CLIENT_ACTIVE_CONFIRMED);
    return challenge;
}

void reset_fixture_with_consumer(
    fixture_t &fixture, uint32_t now, uint32_t official_epoch,
    const worr_cgame_event_runtime_export_v1 *event_consumer)
{
    if (fixture.server_live)
        SV_NativeShadowPeerDestroyV1(&fixture.server);
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CHECK(CL_CGameEventRuntimeResetConnection() ==
          WORR_CGAME_EVENT_RUNTIME_OK);

    fixture = {};
    std::memset(&cls, 0, sizeof(cls));
    std::memset(&cl, 0, sizeof(cl));
    std::memset(&shadow_cvar, 0, sizeof(shadow_cvar));
    std::memset(&event_shadow_cvar, 0, sizeof(event_shadow_cvar));
    std::memset(&probe_hold_cvar, 0, sizeof(probe_hold_cvar));
    reliable_storage.fill(0);
    fake_event_runtime = {};
    com_localTime = now;

    cls.netchan.type = NETCHAN_NEW;
    cls.netchan.maxpacketlen = kApplicationCeiling;
    cls.serverProtocol = PROTOCOL_VERSION_RERELEASE;
    SZ_InitWrite(&cls.netchan.message, reliable_storage.data(),
                 reliable_storage.size());
    fixture.server_channel.type = NETCHAN_NEW;
    fixture.server_channel.maxpacketlen = kApplicationCeiling;

    CL_NativeReadinessPilotRegisterCvar();
    shadow_cvar.integer = 1;
    event_shadow_cvar.integer = 1;
    CHECK(event_consumer != nullptr);
    CHECK(CL_CGameEventRuntimeSetConsumer(event_consumer));
    CHECK(CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(SV_NativeShadowPeerInitModeV1(
        &fixture.server, &fixture.server_channel, now,
        SV_NATIVE_SHADOW_MODE_EVENT));
    fixture.server_live = true;
    (void)activate_epoch(fixture, official_epoch, now + 1u);
}

void reset_fixture(fixture_t &fixture, uint32_t now,
                   uint32_t official_epoch)
{
    reset_fixture_with_consumer(fixture, now, official_epoch,
                                &fake_event_export);
}

wire_packet_t prepare_packet(netchan_t &channel, uint32_t now)
{
    com_localTime = now;
    wire_packet_t packet{};
    netchan_app_tx_prepare_info_v1_t info{};
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.outgoing_sequence = channel.outgoing_sequence++;
    info.max_application_bytes = kApplicationCeiling;
    info.packet_copies = 1;
    packet.completion.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    packet.completion.struct_size = sizeof(packet.completion);
    CHECK(channel.app_tx_prepare);
    CHECK(channel.app_tx_prepare(
              channel.app_tx_opaque, &info, nullptr,
              packet.bytes.data(), &packet.completion) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    packet.count = packet.completion.application_bytes;
    CHECK(packet.count != 0 && packet.count <= packet.bytes.size());
    return packet;
}

void accept_packet(netchan_t &channel, const wire_packet_t &packet)
{
    netchan_app_tx_completion_info_v1_t info{};
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.result = NETCHAN_APP_TX_COMPLETION_ACCEPTED;
    info.packet_copies = 1;
    info.accepted_copies = 1;
    info.application_bytes = static_cast<uint32_t>(packet.count);
    info.token = packet.completion.token;
    CHECK(channel.app_tx_completion);
    channel.app_tx_completion(channel.app_tx_opaque, &info,
                              packet.bytes.data());
}

netchan_app_rx_result_t deliver_to_client(
    fixture_t &fixture, const wire_packet_t &packet, uint32_t now)
{
    (void)fixture;
    com_localTime = now;
    netchan_app_rx_info_v1_t info{};
    info.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.incoming_sequence = fixture.client_rx_sequence++;
    info.message_bytes = static_cast<uint32_t>(packet.count);
    info.application_bytes = static_cast<uint32_t>(packet.count);
    netchan_app_rx_output_v1_t output{};
    output.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output.struct_size = sizeof(output);
    CHECK(cls.netchan.app_rx);
    return cls.netchan.app_rx(
        cls.netchan.app_rx_opaque, &info, packet.bytes.data(), &output);
}

netchan_app_rx_result_t deliver_to_server(
    fixture_t &fixture, const wire_packet_t &packet, uint32_t now)
{
    CHECK(SV_NativeShadowAdvanceAdmissionClockV1(
        &fixture.server, now));
    netchan_app_rx_info_v1_t info{};
    info.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.incoming_sequence = fixture.server_rx_sequence++;
    info.message_bytes = static_cast<uint32_t>(packet.count);
    info.application_bytes = static_cast<uint32_t>(packet.count);
    netchan_app_rx_output_v1_t output{};
    output.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output.struct_size = sizeof(output);
    CHECK(fixture.server_channel.app_rx);
    return fixture.server_channel.app_rx(
        fixture.server_channel.app_rx_opaque, &info,
        packet.bytes.data(), &output);
}

worr_native_carrier_view_v1 carrier_view(const wire_packet_t &packet)
{
    worr_native_carrier_view_v1 view{};
    CHECK(Worr_NativeCarrierDecodeV1(
              packet.bytes.data(), packet.count, &view) ==
          WORR_NATIVE_CARRIER_OK);
    return view;
}

worr_native_record_ref_v1 data_record_ref(const wire_packet_t &packet)
{
    const auto view = carrier_view(packet);
    const worr_native_carrier_entry_v1 *data_entry = nullptr;
    for (uint16_t index = 0; index < view.entry_count; ++index) {
        const auto &entry = view.entries[index];
        if (entry.entry_type != WORR_NATIVE_CARRIER_ENTRY_DATA_V1)
            continue;
        CHECK(data_entry == nullptr);
        data_entry = &entry;
    }
    CHECK(data_entry != nullptr && data_entry->data_bytes != 0);
    worr_native_envelope_frame_info_v1 frame{};
    CHECK(Worr_NativeEnvelopeDecodeV1(
              packet.bytes.data() + data_entry->data_offset,
              data_entry->data_bytes, &frame) == WORR_NATIVE_ENVELOPE_DECODE_OK);
    return frame.record;
}

wire_packet_t corrupted_copy(const wire_packet_t &packet)
{
    wire_packet_t corrupt = packet;
    CHECK(corrupt.count > 8u);
    /* Keep the terminal WORRWTC1 discriminator intact and damage the
     * CRC-covered byte immediately before it.  Marker damage is deliberately
     * indistinguishable from a legacy datagram; CRC damage is a recognized,
     * fail-closed carrier corruption. */
    corrupt.bytes[corrupt.count - 9u] ^= 0x5au;
    worr_native_carrier_view_v1 view{};
    CHECK(Worr_NativeCarrierDecodeV1(
              corrupt.bytes.data(), corrupt.count, &view) ==
          WORR_NATIVE_CARRIER_CORRUPT);
    return corrupt;
}

worr_prediction_command_v1 prediction_command(uint32_t sequence)
{
    worr_prediction_command_v1 command{};
    command.struct_size = sizeof(command);
    command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.duration_ms = 10;
    command.buttons = static_cast<uint8_t>(sequence & 3u);
    command.view_angles[1] =
        static_cast<float>((sequence % 36u) * 10u);
    command.forward_move =
        static_cast<float>((sequence % 100u) * 3u);
    command.side_move = -static_cast<float>(sequence % 100u);
    return command;
}

worr_command_record_v1 queue_client_command(uint32_t command_epoch,
                                             uint32_t sequence)
{
    const worr_command_id_v1 id{command_epoch, sequence};
    const auto command = prediction_command(sequence);
    CL_NativeReadinessPilotObserveFinalizedCommand(sequence, &id, &command);
    CL_NativeReadinessPilotObserveEncodedCommandRange(sequence, 1);

    worr_native_command_shadow_builder_v1 builder{};
    CHECK(Worr_NativeCommandShadowBuilderInitV1(
        &builder, command_epoch,
        WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    worr_command_record_v1 record{};
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, id, &command, &record) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_BUILT);
    record.render_watermark.struct_size = sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    record.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED;
    record.render_watermark.source_server_tick = 1;
    record.render_watermark.tick_interval_us = 10000;
    record.render_watermark.source_server_time_us = 10000;
    record.render_watermark.rendered_server_time_us = 10000;
    CHECK(Worr_CommandRecordCanonicalizeV1(
        &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    return record;
}

worr_event_record_v1 event_candidate(uint32_t tick,
                                      uint32_t entity_index)
{
    worr_event_record_v1 candidate{};
    worr_event_entity_ref_v1 source{};
    source.index = entity_index;
    source.generation = tick + 1u;
    uint64_t semantic_hash = 0;
    CHECK(Worr_LegacyEntityEventCandidateBuildV1(
        tick, static_cast<uint64_t>(tick) * 1000u, entity_index,
        source, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP,
        WORR_EVENT_STREAM_MAX_ENTITIES_V1, &candidate,
        &semantic_hash));
    CHECK(semantic_hash != 0);
    return candidate;
}

worr_event_record_v1 temp_candidate(uint32_t tick)
{
    q2proto_svc_temp_entity_t temp_entity{};
    worr_event_record_v1 candidate{};
    uint32_t source_entity_index = 0;
    uint32_t subject_entity_index = WORR_EVENT_NO_ENTITY;

    temp_entity.type = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
    temp_entity.position1[0] = 16.0f;
    temp_entity.position1[1] = -4.0f;
    temp_entity.direction[2] = 1.0f;
    CHECK(Worr_LegacyTempEventCandidateBuildV1(
        &temp_entity, tick,
        static_cast<uint64_t>(tick) * 1000u,
        WORR_EVENT_STREAM_MAX_ENTITIES_V1, &candidate, &source_entity_index,
        &subject_entity_index) == WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK);
    CHECK(source_entity_index == 0 && subject_entity_index == WORR_EVENT_NO_ENTITY);
    candidate.source_entity.index = 0;
    candidate.source_entity.generation = 1;
    CHECK(Worr_EventRecordCandidateValidateV1(
        &candidate, WORR_EVENT_STREAM_MAX_ENTITIES_V1));
    return candidate;
}

worr_event_record_v1 authority_receipt_candidate(
    const worr_local_interaction_authority_receipt_v1 &receipt)
{
    worr_event_record_v1 candidate{};
    CHECK(Worr_LocalInteractionAuthorityReceiptValidateV1(&receipt));

    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_EVENT_ABI_VERSION;
    candidate.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate.flags = WORR_EVENT_FLAG_CRITICAL;
    candidate.source_tick = 700;
    candidate.source_time_us = UINT64_C(7000000);
    candidate.source_entity = {WORR_EVENT_NO_ENTITY, 0};
    candidate.subject_entity = {WORR_EVENT_NO_ENTITY, 0};
    candidate.event_type = WORR_EVENT_TYPE_AUTHORITY_RECEIPT;
    candidate.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    candidate.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate.payload_kind =
        WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1;
    candidate.payload_size = sizeof(receipt);
    std::memcpy(candidate.payload, &receipt, sizeof(receipt));
    CHECK(Worr_EventRecordCandidateValidateV1(
        &candidate, WORR_EVENT_STREAM_MAX_ENTITIES_V1));
    return candidate;
}

sv_native_shadow_event_status_v1 event_status(fixture_t &fixture,
                                               uint32_t now)
{
    sv_native_shadow_event_status_v1 status{};
    CHECK(SV_NativeShadowGetEventStatusV1(
        &fixture.server, now, &status));
    return status;
}

cl_native_readiness_pilot_test_state_t client_state()
{
    cl_native_readiness_pilot_test_state_t state{};
    CHECK(CL_NativeReadinessPilotGetTestState(&state));
    return state;
}

void test_private_authority_receipt_reconciles_over_native_virtual_link(
    bool hook_active_after, bool receipt_before_prediction,
    bool inject_conflicting_receipt)
{
    fixture_t fixture{};
    const uint32_t official_epoch =
        701u + (hook_active_after ? 1u : 0u) +
        (receipt_before_prediction ? 2u : 0u);
    const uint32_t descriptor_time =
        8010u + (hook_active_after ? 1000u : 0u) +
        (receipt_before_prediction ? 2000u : 0u);
    reset_fixture_with_consumer(fixture, 8000, official_epoch,
                                CG_GetEventRuntimeAPI());
    CG_EventRuntimeSetAuditEnabled(false);

    const auto authority_receipt =
        configure_interaction_prediction(hook_active_after);
    const auto candidate = authority_receipt_candidate(authority_receipt);
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &fixture.server, &candidate, 1, descriptor_time));

    /* The real cgame export accepts the transport descriptor before any
     * control record is admitted. This remains an in-process virtual link:
     * no socket, window, or client input subsystem is involved. */
    auto descriptor = prepare_packet(fixture.server_channel, descriptor_time);
    const auto descriptor_ref = data_record_ref(descriptor);
    CHECK(descriptor_ref.record_class ==
          WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
    const uint32_t stream_epoch = descriptor_ref.object_epoch;
    const uint32_t first_sequence = descriptor_ref.object_sequence;
    accept_packet(fixture.server_channel, descriptor);
    CHECK(deliver_to_client(fixture, descriptor, descriptor_time) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    auto descriptor_ack = prepare_packet(cls.netchan, descriptor_time + 1u);
    accept_packet(cls.netchan, descriptor_ack);
    CHECK(deliver_to_server(fixture, descriptor_ack, descriptor_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);

    auto status = event_status(fixture, descriptor_time + 1u);
    CHECK(status.descriptor_acked == 1 && status.candidates_promoted == 1 &&
          status.retained_count == 1);
    /* Import installation resets the interaction cache by contract, so it
     * must precede either prediction or authority receipt arrival. */
    install_configured_interaction_import();
    cg_local_interaction_shadow_status_v1 interaction{};
    if (!receipt_before_prediction) {
        predict_configured_interaction();
        CG_LocalInteractionGetStatus(&interaction);
        CHECK(interaction.prediction_passes == 1 &&
              interaction.transactions == 1 &&
              interaction.pending_requests == 1 &&
              interaction.authority_receipts == 0 &&
              interaction.requires_resync == 0);
    }
    cg_event_runtime_status_v1 cgame_before_receipt{};
    CHECK(CG_EventRuntimeGetStatus(&cgame_before_receipt));
    CHECK(cgame_before_receipt.authority_epoch == stream_epoch);
    CHECK(cgame_before_receipt.authority_count == 0);
    CHECK(cgame_before_receipt.receipt.highest_contiguous ==
          first_sequence - 1u);

    auto receipt_event =
        prepare_packet(fixture.server_channel, descriptor_time + 2u);
    const auto receipt_ref = data_record_ref(receipt_event);
    CHECK(receipt_ref.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
          receipt_ref.object_epoch == stream_epoch &&
          receipt_ref.object_sequence == first_sequence);
    accept_packet(fixture.server_channel, receipt_event);
    CHECK(deliver_to_client(fixture, receipt_event, descriptor_time + 2u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);

    if (receipt_before_prediction) {
        CG_LocalInteractionGetStatus(&interaction);
        CHECK(interaction.authority_receipts == 1 &&
              interaction.authority_unmatched == 1 &&
              interaction.corrections_confirmed == 0 &&
              interaction.corrections_rejected == 0 &&
              interaction.requires_resync == 0);
        predict_configured_interaction();
    }
    CG_LocalInteractionGetStatus(&interaction);
    CHECK(interaction.authority_receipts == 1 &&
          interaction.authority_unmatched ==
              (receipt_before_prediction ? 1u : 0u) &&
          interaction.authority_duplicates == 0 &&
          interaction.corrections_confirmed ==
              (hook_active_after ? 1u : 0u) &&
          interaction.corrections_rejected ==
              (hook_active_after ? 0u : 1u) &&
          interaction.requires_resync == 0);
    cg_event_runtime_status_v1 cgame_status{};
    CHECK(CG_EventRuntimeGetStatus(&cgame_status));
    CHECK(cgame_status.authority_epoch == stream_epoch);
    CHECK(cgame_status.authority_count == 1);
    CHECK(cgame_status.authoritative_records ==
          cgame_before_receipt.authoritative_records + 1u);
    CHECK(cgame_status.authoritative_presentations == 0);
    CHECK(cgame_status.receipt.highest_contiguous == first_sequence);

    std::uint32_t advanced = UINT32_MAX;
    const auto terminal_skips = cgame_status.authoritative_terminal_skips;
    CHECK(CG_EventRuntimeAdvanceAudit(UINT64_C(7000000), 700, 1, &advanced) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(advanced == 0);
    CHECK(CG_EventRuntimeGetStatus(&cgame_status));
    CHECK(cgame_status.authoritative_terminal_skips == terminal_skips + 1u &&
          cgame_status.authoritative_presentations == 0 &&
          cgame_status.authority_requires_resync == 0);

    /* Drop the exact event receipt, then deliver the server retry twice. The
     * native admission owner rearms the ACK without reinserting the control
     * record, so cgame reconciliation remains exactly-once. */
    auto lost_receipt_ack = prepare_packet(cls.netchan, descriptor_time + 2u);
    accept_packet(cls.netchan, lost_receipt_ack);
    const uint32_t retry_time =
        descriptor_time + 2u + SV_NATIVE_SHADOW_EVENT_RESEND_MS;
    CHECK(SV_NativeShadowOutputDueV1(&fixture.server, retry_time));
    auto retry = prepare_packet(fixture.server_channel, retry_time);
    accept_packet(fixture.server_channel, retry);
    CHECK(deliver_to_client(fixture, retry, retry_time) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(deliver_to_client(fixture, retry, retry_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CG_LocalInteractionGetStatus(&interaction);
    CHECK(interaction.authority_receipts == 1 &&
          interaction.authority_unmatched ==
              (receipt_before_prediction ? 1u : 0u) &&
          interaction.authority_duplicates == 0 &&
          interaction.corrections_confirmed ==
              (hook_active_after ? 1u : 0u) &&
          interaction.corrections_rejected ==
              (hook_active_after ? 0u : 1u) &&
          interaction.requires_resync == 0);

    auto final_ack = prepare_packet(cls.netchan, retry_time + 1u);
    accept_packet(cls.netchan, final_ack);
    CHECK(deliver_to_server(fixture, final_ack, retry_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    status = event_status(fixture, retry_time + 1u);
    CHECK(status.retained_count == 0 && status.events_acknowledged == 1 &&
          status.retries >= 1);

    if (inject_conflicting_receipt) {
        auto conflicting_receipt = authority_receipt;
        conflicting_receipt.state_hash ^= UINT64_C(1);
        CHECK(Worr_LocalInteractionAuthorityReceiptValidateV1(
            &conflicting_receipt));
        const auto conflicting_candidate =
            authority_receipt_candidate(conflicting_receipt);
        const uint32_t conflict_time = retry_time + 2u;
        CHECK(SV_NativeShadowQueueEventCandidatesV1(
            &fixture.server, &conflicting_candidate, 1, conflict_time));
        CHECK(SV_NativeShadowOutputDueV1(&fixture.server, conflict_time));
        auto conflict_packet =
            prepare_packet(fixture.server_channel, conflict_time);
        const auto conflict_ref = data_record_ref(conflict_packet);
        CHECK(conflict_ref.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
              conflict_ref.object_epoch == stream_epoch &&
              conflict_ref.object_sequence == first_sequence + 1u);
        accept_packet(fixture.server_channel, conflict_packet);

        /* A distinct receipt for an already reconciled command must never be
         * committed or acknowledged. Native admission scrubs cgame authority,
         * leaves the new server record retained, and drains this transport
         * until a fresh descriptor/epoch restores a trusted baseline. */
        CHECK(deliver_to_client(fixture, conflict_packet, conflict_time) ==
              NETCHAN_APP_RX_REJECT);
        CHECK(CL_CGameEventRuntimeRequiresResync());
        CHECK(client_state().mode == 3u);
        status = event_status(fixture, conflict_time);
        CHECK(status.retained_count == 1 && status.events_acknowledged == 1);

        /* The failed epoch cannot be revived. A fresh negotiated map epoch
         * cancels its retained conflict, then only a fresh descriptor may
         * clear the engine resync latch and establish new cgame authority. */
        CL_NativeReadinessPilotQuiesceMap();
        CL_NativeReadinessPilotServerDataReset();
        const uint32_t recovery_time = conflict_time + 10u;
        const auto recovery_challenge = activate_epoch(
            fixture, official_epoch + 100u, recovery_time);
        CHECK(recovery_challenge.transport_epoch != 0);
        CHECK(CL_CGameEventRuntimeRequiresResync());
        CHECK(fixture.server.cancelled_event_records >= 1);

        /* SERVER_ACTIVE creates a fresh descriptor gate.  It must be
         * delivered and acknowledged before this epoch can promote data. */
        auto recovery_descriptor = prepare_packet(
            fixture.server_channel, recovery_time + 1u);
        const auto recovery_descriptor_ref =
            data_record_ref(recovery_descriptor);
        CHECK(recovery_descriptor_ref.record_class ==
              WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1 &&
              recovery_descriptor_ref.object_epoch != stream_epoch);
        const uint32_t recovery_stream_epoch =
            recovery_descriptor_ref.object_epoch;
        const uint32_t recovery_first_sequence =
            recovery_descriptor_ref.object_sequence;
        accept_packet(fixture.server_channel, recovery_descriptor);
        CHECK(deliver_to_client(
                  fixture, recovery_descriptor, recovery_time + 1u) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        CHECK(!CL_CGameEventRuntimeRequiresResync());
        auto recovery_descriptor_ack = prepare_packet(
            cls.netchan, recovery_time + 2u);
        accept_packet(cls.netchan, recovery_descriptor_ack);
        CHECK(deliver_to_server(
                  fixture, recovery_descriptor_ack, recovery_time + 2u) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        status = event_status(fixture, recovery_time + 2u);
        CHECK(status.descriptor_acked == 1 && status.candidates_promoted == 0 &&
              status.retained_count == 0);

        CHECK(SV_NativeShadowQueueEventCandidatesV1(
            &fixture.server, &candidate, 1, recovery_time + 3u));
        status = event_status(fixture, recovery_time + 3u);
        CHECK(status.candidates_promoted == 1 && status.retained_count == 1);

        install_configured_interaction_import();
        predict_configured_interaction();
        CG_LocalInteractionGetStatus(&interaction);
        CHECK(interaction.prediction_passes == 1 &&
              interaction.pending_requests == 1 &&
              interaction.requires_resync == 0);
        auto recovery_event = prepare_packet(
            fixture.server_channel, recovery_time + 4u);
        const auto recovery_event_ref = data_record_ref(recovery_event);
        CHECK(recovery_event_ref.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
              recovery_event_ref.object_epoch == recovery_stream_epoch &&
              recovery_event_ref.object_sequence == recovery_first_sequence);
        accept_packet(fixture.server_channel, recovery_event);
        CHECK(deliver_to_client(fixture, recovery_event, recovery_time + 4u) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        CG_LocalInteractionGetStatus(&interaction);
        CHECK(interaction.authority_receipts == 1 &&
              interaction.corrections_rejected == 1 &&
              interaction.requires_resync == 0);

        auto recovery_final_ack = prepare_packet(cls.netchan, recovery_time + 5u);
        accept_packet(cls.netchan, recovery_final_ack);
        CHECK(deliver_to_server(
                  fixture, recovery_final_ack, recovery_time + 5u) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        status = event_status(fixture, recovery_time + 5u);
        CHECK(status.retained_count == 0 && status.events_acknowledged == 1);
    }

    SV_NativeShadowPeerDestroyV1(&fixture.server);
    fixture.server_live = false;
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CG_LocalInteractionSetImport(nullptr);
}

void test_private_authority_receipt_rejection_over_native_virtual_link()
{
    test_private_authority_receipt_reconciles_over_native_virtual_link(false,
                                                                        false,
                                                                        false);
}

void test_private_authority_receipt_confirmation_over_native_virtual_link()
{
    test_private_authority_receipt_reconciles_over_native_virtual_link(true,
                                                                        false,
                                                                        false);
}

void test_private_authority_receipt_first_reconciliation_over_native_virtual_link()
{
    test_private_authority_receipt_reconciles_over_native_virtual_link(false,
                                                                        true,
                                                                        false);
    test_private_authority_receipt_reconciles_over_native_virtual_link(true,
                                                                        true,
                                                                        false);
}

void test_private_authority_receipt_conflict_forces_native_resync()
{
    test_private_authority_receipt_reconciles_over_native_virtual_link(false,
                                                                        false,
                                                                        true);
}

void test_receipt_history_loss_forces_native_resync()
{
    fixture_t fixture{};
    constexpr uint32_t official_epoch = 705;
    constexpr uint32_t descriptor_time = 12010;
    reset_fixture_with_consumer(fixture, 12000, official_epoch,
                                CG_GetEventRuntimeAPI());
    CG_EventRuntimeSetAuditEnabled(false);

    const auto receipt = configure_interaction_prediction(false);
    const auto candidate = authority_receipt_candidate(receipt);
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &fixture.server, &candidate, 1, descriptor_time));
    auto descriptor = prepare_packet(fixture.server_channel, descriptor_time);
    const auto descriptor_ref = data_record_ref(descriptor);
    CHECK(descriptor_ref.record_class ==
          WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
    accept_packet(fixture.server_channel, descriptor);
    CHECK(deliver_to_client(fixture, descriptor, descriptor_time) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    auto descriptor_ack = prepare_packet(cls.netchan, descriptor_time + 1u);
    accept_packet(cls.netchan, descriptor_ack);
    CHECK(deliver_to_server(fixture, descriptor_ack, descriptor_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);

    install_configured_interaction_import();
    auto receipt_event = prepare_packet(
        fixture.server_channel, descriptor_time + 2u);
    const auto receipt_ref = data_record_ref(receipt_event);
    CHECK(receipt_ref.record_class == WORR_NATIVE_RECORD_EVENT_V1);
    accept_packet(fixture.server_channel, receipt_event);
    CHECK(deliver_to_client(fixture, receipt_event, descriptor_time + 2u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    cg_local_interaction_shadow_status_v1 interaction{};
    CG_LocalInteractionGetStatus(&interaction);
    CHECK(interaction.authority_receipts == 1 &&
          interaction.authority_unmatched == 1 &&
          interaction.requires_resync == 0);

    /* A receipt-first arrival remains admissible only while the exact
     * canonical command can still be predicted. Advancing past that command
     * is a history loss, so it must fail closed before the queued ACK is
     * transmitted to the server. */
    predict_past_configured_interaction();
    CG_LocalInteractionGetStatus(&interaction);
    CHECK(interaction.authority_expirations == 1 &&
          interaction.requires_resync == 1);
    cg_event_runtime_status_v1 cgame_status{};
    CHECK(CG_EventRuntimeGetStatus(&cgame_status));
    CHECK(cgame_status.authority_requires_resync == 1 &&
          cgame_status.authority_degraded == 1);

    worr_cgame_event_runtime_status_v1 owner_status{};
    CHECK(!CL_CGameEventRuntimeGetStatus(&owner_status));
    CHECK(CL_CGameEventRuntimeRequiresResync());
    const auto server_status = event_status(fixture, descriptor_time + 3u);
    CHECK(server_status.retained_count == 1 &&
          server_status.events_acknowledged == 0);

    SV_NativeShadowPeerDestroyV1(&fixture.server);
    fixture.server_live = false;
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CG_LocalInteractionSetImport(nullptr);
}

void test_bidirectional_loss_reorder_duplicate_and_ack_loss()
{
    fixture_t fixture{};
    constexpr uint32_t command_epoch = 301;
    reset_fixture(fixture, 1000, command_epoch);

    auto candidate = temp_candidate(50);
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &fixture.server, &candidate, 1, 1010));
    const auto legacy_command = queue_client_command(command_epoch, 1);

    /* Server -> client DATA loss after a successful local handoff. */
    CHECK(SV_NativeShadowOutputDueV1(&fixture.server, 1010));
    auto lost_descriptor = prepare_packet(fixture.server_channel, 1010);
    auto descriptor_view = carrier_view(lost_descriptor);
    CHECK(descriptor_view.entry_count == 1 &&
          descriptor_view.entries[0].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_DATA_V1);
    accept_packet(fixture.server_channel, lost_descriptor);
    ++metrics.server_to_client_losses;

    CHECK(SV_NativeShadowOutputDueV1(
        &fixture.server, 1010 + SV_NATIVE_SHADOW_EVENT_RESEND_MS));
    auto descriptor_retry = prepare_packet(
        fixture.server_channel,
        1010 + SV_NATIVE_SHADOW_EVENT_RESEND_MS);
    accept_packet(fixture.server_channel, descriptor_retry);
    CHECK(deliver_to_client(
              fixture, descriptor_retry,
              1010 + SV_NATIVE_SHADOW_EVENT_RESEND_MS) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(fake_event_runtime.active &&
          fake_event_runtime.authority_count == 0);

    /* Client -> server mixed command DATA + descriptor ACK loss. */
    auto lost_client_mixed = prepare_packet(
        cls.netchan, 1010 + SV_NATIVE_SHADOW_EVENT_RESEND_MS);
    auto client_view = carrier_view(lost_client_mixed);
    CHECK(client_view.entry_count == 2 &&
          client_view.entries[0].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_DATA_V1 &&
          client_view.entries[1].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
    accept_packet(cls.netchan, lost_client_mixed);
    ++metrics.client_to_server_losses;

    const uint32_t retry_time =
        1010 + 2u * SV_NATIVE_SHADOW_EVENT_RESEND_MS;
    com_localTime = retry_time;
    CHECK(CL_NativeReadinessPilotOutputDue());
    auto client_mixed_retry = prepare_packet(cls.netchan, retry_time);
    accept_packet(cls.netchan, client_mixed_retry);
    CHECK(deliver_to_server(fixture, client_mixed_retry, retry_time) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &fixture.server, &legacy_command, retry_time));

    auto status = event_status(fixture, retry_time);
    CHECK(status.descriptor_acked == 1 &&
          status.candidates_promoted == 1 &&
          status.output_due == 1);

    /* The delayed descriptor arrives after the newer reverse-direction
     * carrier.  It is a semantic repeat: it rearms the existing receipt but
     * cannot reset cgame authority or present an event twice. */
    const uint32_t resets_before = fake_event_runtime.reset_calls;
    CHECK(deliver_to_client(fixture, descriptor_retry, retry_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    ++metrics.reordered_deliveries;
    ++metrics.duplicate_deliveries;
    CHECK(fake_event_runtime.reset_calls == resets_before);

    /* The next server packet is genuinely full duplex: event DATA plus the
     * exact command ACK. */
    CHECK(SV_NativeShadowOutputDueV1(
        &fixture.server, retry_time + 1u));
    auto event_and_command_ack = prepare_packet(
        fixture.server_channel, retry_time + 1u);
    auto server_view = carrier_view(event_and_command_ack);
    CHECK(server_view.entry_count == 2 &&
          server_view.entries[0].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_DATA_V1 &&
          server_view.entries[1].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
    accept_packet(fixture.server_channel, event_and_command_ack);
    CHECK(deliver_to_client(
              fixture, event_and_command_ack, retry_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(fake_event_runtime.authority_count == 1 &&
          fake_event_runtime.submit_calls == 1);
    CHECK(client_state().retained_messages == 0);

    /* Lose the first event receipt.  The server retries DATA, and two
     * duplicate deliveries remain presentation-idempotent while refreshing
     * only the same semantic ACK. */
    auto lost_event_ack = prepare_packet(cls.netchan, retry_time + 1u);
    accept_packet(cls.netchan, lost_event_ack);
    ++metrics.client_to_server_losses;
    ++metrics.ack_losses;
    const uint32_t event_retry_time =
        retry_time + 1u + SV_NATIVE_SHADOW_EVENT_RESEND_MS;
    CHECK(SV_NativeShadowOutputDueV1(
        &fixture.server, event_retry_time));
    auto event_retry = prepare_packet(
        fixture.server_channel, event_retry_time);
    accept_packet(fixture.server_channel, event_retry);
    CHECK(deliver_to_client(fixture, event_retry, event_retry_time) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    ++metrics.duplicate_deliveries;
    CHECK(deliver_to_client(
              fixture, event_retry, event_retry_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    ++metrics.duplicate_deliveries;
    CHECK(fake_event_runtime.authority_count == 1 &&
          fake_event_runtime.submit_calls == 1);

    auto final_ack = prepare_packet(cls.netchan, event_retry_time + 1u);
    accept_packet(cls.netchan, final_ack);
    CHECK(deliver_to_server(
              fixture, final_ack, event_retry_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    status = event_status(fixture, event_retry_time + 1u);
    CHECK(status.retained_count == 0 &&
          status.events_acknowledged == 1 &&
          status.retries >= 2);
    ++metrics.converged_scenarios;
    metrics.exactly_once_presentations +=
        fake_event_runtime.authority_count;

    SV_NativeShadowPeerDestroyV1(&fixture.server);
    fixture.server_live = false;
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
}

void test_multiple_events_selective_receipt_under_semantic_reordering()
{
    fixture_t fixture{};
    constexpr uint32_t command_epoch = 351;
    constexpr uint32_t descriptor_time = 11000;
    reset_fixture(fixture, 10900, command_epoch);

    const std::array<worr_event_record_v1, 3> candidates{
        temp_candidate(90), temp_candidate(91), temp_candidate(92)};
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &fixture.server, candidates.data(),
        static_cast<uint32_t>(candidates.size()), descriptor_time));

    /* Establish authority, then release the descriptor gate.  All three
     * canonical candidates become retained, independently schedulable DATA. */
    auto descriptor = prepare_packet(fixture.server_channel, descriptor_time);
    const auto descriptor_ref = data_record_ref(descriptor);
    CHECK(descriptor_ref.record_class ==
              WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1 &&
          descriptor_ref.object_epoch != 0 &&
          descriptor_ref.object_sequence != 0);
    const uint32_t stream_epoch = descriptor_ref.object_epoch;
    const uint32_t first_event_sequence = descriptor_ref.object_sequence;
    accept_packet(fixture.server_channel, descriptor);
    CHECK(deliver_to_client(fixture, descriptor, descriptor_time) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    auto descriptor_ack = prepare_packet(cls.netchan, descriptor_time + 1u);
    accept_packet(cls.netchan, descriptor_ack);
    CHECK(deliver_to_server(fixture, descriptor_ack, descriptor_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    auto status = event_status(fixture, descriptor_time + 1u);
    CHECK(status.descriptor_acked == 1 && status.candidates_promoted == 3 &&
          status.retained_count == 3);

    /* Event 1 arrives and is released.  Event 2 is locally handed off but
     * lost by the virtual link; the unsent event 3 remains immediately due
     * and therefore crosses the semantic gap before event 2's retry. */
    auto event1 = prepare_packet(fixture.server_channel, descriptor_time + 2u);
    const auto event1_ref = data_record_ref(event1);
    CHECK(event1_ref.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
          event1_ref.object_epoch == stream_epoch &&
          event1_ref.object_sequence == first_event_sequence);
    accept_packet(fixture.server_channel, event1);
    CHECK(deliver_to_client(fixture, event1, descriptor_time + 2u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(fake_event_runtime.authority_count == 1 &&
          fake_event_runtime.receipt.highest_contiguous ==
              first_event_sequence &&
          fake_event_runtime.receipt.selective_mask == 0 &&
          fake_event_runtime.next_sequence == first_event_sequence + 1u);
    auto event1_ack = prepare_packet(cls.netchan, descriptor_time + 3u);
    accept_packet(cls.netchan, event1_ack);
    CHECK(deliver_to_server(fixture, event1_ack, descriptor_time + 3u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);

    auto lost_event2 =
        prepare_packet(fixture.server_channel, descriptor_time + 4u);
    const auto event2_ref = data_record_ref(lost_event2);
    CHECK(event2_ref.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
          event2_ref.object_epoch == stream_epoch &&
          event2_ref.object_sequence == first_event_sequence + 1u);
    accept_packet(fixture.server_channel, lost_event2);

    auto event3 = prepare_packet(fixture.server_channel, descriptor_time + 5u);
    const auto event3_ref = data_record_ref(event3);
    CHECK(event3_ref.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
          event3_ref.object_epoch == stream_epoch &&
          event3_ref.object_sequence == first_event_sequence + 2u);
    accept_packet(fixture.server_channel, event3);
    CHECK(deliver_to_client(fixture, event3, descriptor_time + 5u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(fake_event_runtime.authority_count == 2 &&
          fake_event_runtime.receipt.highest_contiguous ==
              first_event_sequence &&
          fake_event_runtime.receipt.selective_mask == 2 &&
          fake_event_runtime.next_sequence == first_event_sequence + 1u);

    /* The client sends an exact receipt only for semantic event 3.  The
     * server retires 3 while retaining the lost 2, proving the receipt is
     * selective rather than a cumulative acknowledgement. */
    auto event3_ack = prepare_packet(cls.netchan, descriptor_time + 6u);
    accept_packet(cls.netchan, event3_ack);
    CHECK(deliver_to_server(fixture, event3_ack, descriptor_time + 6u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    status = event_status(fixture, descriptor_time + 6u);
    CHECK(status.retained_count == 1 && status.events_acknowledged == 2);

    const uint32_t retry_time =
        descriptor_time + 4u + SV_NATIVE_SHADOW_EVENT_RESEND_MS;
    CHECK(SV_NativeShadowOutputDueV1(&fixture.server, retry_time));
    auto event2_retry = prepare_packet(fixture.server_channel, retry_time);
    const auto event2_retry_ref = data_record_ref(event2_retry);
    CHECK(event2_retry_ref.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
          event2_retry_ref.object_epoch == stream_epoch &&
          event2_retry_ref.object_sequence == first_event_sequence + 1u);
    accept_packet(fixture.server_channel, event2_retry);
    CHECK(deliver_to_client(fixture, event2_retry, retry_time) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(fake_event_runtime.authority_count == 3 &&
          fake_event_runtime.receipt.highest_contiguous ==
              first_event_sequence + 2u &&
          fake_event_runtime.receipt.selective_mask == 0 &&
          fake_event_runtime.next_sequence == first_event_sequence + 3u);

    auto final_ack = prepare_packet(cls.netchan, retry_time + 1u);
    accept_packet(cls.netchan, final_ack);
    CHECK(deliver_to_server(fixture, final_ack, retry_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    status = event_status(fixture, retry_time + 1u);
    CHECK(status.retained_count == 0 && status.events_acknowledged == 3 &&
          status.retries >= 1);

    SV_NativeShadowPeerDestroyV1(&fixture.server);
    fixture.server_live = false;
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
}

void test_corruption_is_directional_and_fail_closed()
{
    fixture_t fixture{};
    reset_fixture(fixture, 3000, 401);
    auto candidate = event_candidate(60, 8);
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &fixture.server, &candidate, 1, 3010));
    auto descriptor = prepare_packet(fixture.server_channel, 3010);
    accept_packet(fixture.server_channel, descriptor);
    const auto bad_descriptor = corrupted_copy(descriptor);
    ++metrics.corrupt_server_to_client;
    CHECK(deliver_to_client(fixture, bad_descriptor, 3010) ==
          NETCHAN_APP_RX_REJECT);
    CHECK(client_state().mode == 3u);
    CHECK(fake_event_runtime.authority_count == 0);

    reset_fixture(fixture, 4000, 402);
    candidate = event_candidate(61, 9);
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &fixture.server, &candidate, 1, 4010));
    descriptor = prepare_packet(fixture.server_channel, 4010);
    accept_packet(fixture.server_channel, descriptor);
    CHECK(deliver_to_client(fixture, descriptor, 4010) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    auto ack = prepare_packet(cls.netchan, 4010);
    accept_packet(cls.netchan, ack);
    const auto bad_ack = corrupted_copy(ack);
    ++metrics.corrupt_client_to_server;
    CHECK(deliver_to_server(fixture, bad_ack, 4010) ==
          NETCHAN_APP_RX_REJECT);
    CHECK(!SV_NativeShadowPeerEnabledV1(&fixture.server));

    SV_NativeShadowPeerDestroyV1(&fixture.server);
    fixture.server_live = false;
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
}

void test_epoch_cancellation_strips_old_ack_and_data()
{
    fixture_t fixture{};
    reset_fixture(fixture, 5000, 501);
    auto candidate = event_candidate(70, 10);
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &fixture.server, &candidate, 1, 5010));
    auto old_descriptor = prepare_packet(fixture.server_channel, 5010);
    accept_packet(fixture.server_channel, old_descriptor);
    const auto old_view = carrier_view(old_descriptor);
    const uint32_t old_transport_epoch = old_view.transport_epoch;
    CHECK(deliver_to_client(fixture, old_descriptor, 5010) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(client_state().event_ack_receipts == 1);

    /* Save an exact locally accepted ACK whose remote outcome is loss.  The
     * next negotiated CHALLENGE explicitly disposes both this receipt and the
     * server's unacknowledged descriptor instead of manufacturing a retired
     * transport bank. */
    auto delayed_old_ack = prepare_packet(cls.netchan, 5011);
    CHECK(carrier_view(delayed_old_ack).transport_epoch ==
          old_transport_epoch);
    accept_packet(cls.netchan, delayed_old_ack);
    CHECK(client_state().event_ack_receipts == 1);

    CL_NativeReadinessPilotQuiesceMap();
    CL_NativeReadinessPilotServerDataReset();
    const auto next_challenge = activate_epoch(fixture, 502, 5020);
    CHECK(next_challenge.transport_epoch != old_transport_epoch);

    auto client = client_state();
    CHECK(client.cancelled_through_transport_epoch ==
              old_transport_epoch &&
          client.cancelled_transports == 1 &&
          client.cancelled_event_receipts == 1 &&
          client.retired_transport_epoch == 0 &&
          client.retired_event_ack_receipts == 0);
    auto status = event_status(fixture, 5022);
    CHECK(status.sender_initialized == 1 &&
          status.retired_sender_initialized == 0 &&
          status.retired_retained_count == 0 &&
          status.retained_count == 1);
    CHECK(fixture.server.cancelled_through_transport_epoch ==
              old_transport_epoch &&
          fixture.server.cancelled_transports == 1 &&
          fixture.server.cancelled_event_records == 2 &&
          fixture.server.retired_transport_initialized == 0);

    /* Fully valid canceled carriers keep their legacy prefixes authoritative
     * but cannot acknowledge, rearm, reset authority, or mutate either fresh
     * native transport. */
    const uint32_t server_stale_before =
        static_cast<uint32_t>(fixture.server.stale_cancelled_carriers);
    CHECK(deliver_to_server(fixture, delayed_old_ack, 5022) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(fixture.server.stale_cancelled_carriers ==
          server_stale_before + 1u);
    status = event_status(fixture, 5022);
    CHECK(status.retained_count == 1 &&
          status.retired_retained_count == 0 &&
          status.descriptor_acked == 0 &&
          status.events_acknowledged == 0);
    ++metrics.cancelled_ack_strips;

    const auto client_before_old_data = client_state();
    const uint32_t submits_before = fake_event_runtime.submit_calls;
    const uint32_t resets_before = fake_event_runtime.reset_calls;
    CHECK(deliver_to_client(fixture, old_descriptor, 5023) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    ++metrics.reordered_deliveries;
    ++metrics.cancelled_data_strips;
    client = client_state();
    CHECK(client.stale_cancelled_carriers ==
              client_before_old_data.stale_cancelled_carriers + 1u &&
          client.retained_messages ==
              client_before_old_data.retained_messages &&
          client.event_ack_receipts ==
              client_before_old_data.event_ack_receipts &&
          fake_event_runtime.submit_calls == submits_before &&
          fake_event_runtime.reset_calls == resets_before);

    /* Cancellation is not a permissive parser: CRC-corrupt old traffic is
     * still recognized as native and rejected in both directions. */
    const auto corrupt_old_data = corrupted_copy(old_descriptor);
    const auto corrupt_old_ack = corrupted_copy(delayed_old_ack);
    CHECK(deliver_to_client(fixture, corrupt_old_data, 5024) ==
          NETCHAN_APP_RX_REJECT);
    ++metrics.cancelled_corrupt_rejections;
    CHECK(deliver_to_server(fixture, corrupt_old_ack, 5024) ==
          NETCHAN_APP_RX_REJECT);
    ++metrics.cancelled_corrupt_rejections;

    SV_NativeShadowPeerDestroyV1(&fixture.server);
    fixture.server_live = false;
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
}

void diagnose_exhausted_ack_credit_lifecycle_gap()
{
    fixture_t fixture{};
    constexpr uint32_t first_official_epoch = 601;
    constexpr uint32_t second_official_epoch = 602;
    constexpr uint32_t third_official_epoch = 603;
    constexpr uint32_t descriptor_time = 7010;
    reset_fixture(fixture, 7000, first_official_epoch);

    auto candidate = event_candidate(80, 11);
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &fixture.server, &candidate, 1, descriptor_time));
    auto old_descriptor = prepare_packet(
        fixture.server_channel, descriptor_time);
    accept_packet(fixture.server_channel, old_descriptor);
    const auto old_descriptor_view = carrier_view(old_descriptor);
    const uint32_t old_transport_epoch =
        old_descriptor_view.transport_epoch;
    CHECK(deliver_to_client(
              fixture, old_descriptor, descriptor_time) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(client_state().event_ack_receipts == 1);

    /* Spend every proactive receipt handoff as a locally accepted packet
     * that the virtual link drops.  No DATA repeat is delivered to rearm the
     * exact semantic receipt. */
    wire_packet_t delayed_old_ack{};
    for (uint32_t attempt = 0;
         attempt < SV_NATIVE_SHADOW_ACK_PROACTIVE_HANDOFFS;
         ++attempt) {
        const uint32_t ack_time =
            descriptor_time +
            attempt * SV_NATIVE_SHADOW_ACK_RETRY_MS;
        com_localTime = ack_time;
        CHECK(CL_NativeReadinessPilotOutputDue());
        auto lost_ack = prepare_packet(cls.netchan, ack_time);
        const auto ack_view = carrier_view(lost_ack);
        CHECK(ack_view.transport_epoch == old_transport_epoch &&
              ack_view.entry_count == 1 &&
              ack_view.entries[0].entry_type ==
                  WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
        accept_packet(cls.netchan, lost_ack);
        delayed_old_ack = lost_ack;
        ++lifecycle_diagnostic.accepted_lost_ack_handoffs;
    }
    CHECK(lifecycle_diagnostic.accepted_lost_ack_handoffs ==
          SV_NATIVE_SHADOW_ACK_PROACTIVE_HANDOFFS);

    const uint32_t exhausted_time =
        descriptor_time +
        SV_NATIVE_SHADOW_ACK_PROACTIVE_HANDOFFS *
            SV_NATIVE_SHADOW_ACK_RETRY_MS;
    com_localTime = exhausted_time;
    lifecycle_diagnostic.client_due_after_exhaustion =
        CL_NativeReadinessPilotOutputDue() ? 1u : 0u;
    auto status = event_status(fixture, exhausted_time);
    lifecycle_diagnostic.old_retained_before_rotation =
        status.retained_count;

    /* The fresh negotiated CHALLENGE is the hard disposition for the
     * exhausted receipt and unacknowledged descriptor.  It must remove both
     * without depending on another ACK handoff. */
    CL_NativeReadinessPilotQuiesceMap();
    CL_NativeReadinessPilotServerDataReset();
    const uint32_t first_rotation_time = exhausted_time + 10u;
    const auto second_challenge = activate_epoch(
        fixture, second_official_epoch, first_rotation_time);
    const uint32_t second_transport_epoch =
        second_challenge.transport_epoch;
    CHECK(second_transport_epoch != old_transport_epoch);

    auto client = client_state();
    lifecycle_diagnostic.client_cancelled_receipts =
        static_cast<uint32_t>(client.cancelled_event_receipts);
    lifecycle_diagnostic.client_cancellation_floor =
        client.cancelled_through_transport_epoch;
    lifecycle_diagnostic.retired_ack_receipts_after_rotation =
        client.retired_event_ack_receipts;
    status = event_status(fixture, first_rotation_time + 2u);
    lifecycle_diagnostic.server_cancelled_events =
        static_cast<uint32_t>(
            fixture.server.cancelled_event_records);
    lifecycle_diagnostic.server_cancellation_floor =
        fixture.server.cancelled_through_transport_epoch;
    lifecycle_diagnostic.retired_retained_after_rotation =
        status.retired_retained_count;
    CHECK(client.cancelled_transports == 1 &&
          client.retired_transport_epoch == 0 &&
          client.event_ack_receipts == 0 &&
          fixture.server.cancelled_transports == 1 &&
          fixture.server.retired_transport_initialized == 0 &&
          status.sender_initialized == 1 &&
          status.retired_sender_initialized == 0 &&
          status.retained_count == 1);

    const uint32_t release_probe_time = first_rotation_time +
        SV_NATIVE_SHADOW_ACK_RETRY_MS + 2u;
    com_localTime = release_probe_time;
    lifecycle_diagnostic.release_due =
        CL_NativeReadinessPilotOutputDue() ? 1u : 0u;
    CHECK(lifecycle_diagnostic.release_due == 0);

    /* A second activation advances the monotonic floors without overwriting
     * any retired bank because no retired bank exists. */
    CL_NativeReadinessPilotQuiesceMap();
    CL_NativeReadinessPilotServerDataReset();
    const uint32_t second_rotation_time = release_probe_time + 10u;
    const auto third_challenge = activate_epoch(
        fixture, third_official_epoch, second_rotation_time);
    CHECK(third_challenge.transport_epoch != second_transport_epoch);

    client = client_state();
    status = event_status(fixture, second_rotation_time + 2u);
    lifecycle_diagnostic.second_rotation_without_retired_bank =
        client.cancelled_through_transport_epoch ==
                second_transport_epoch &&
            client.cancelled_transports == 2 &&
            client.retired_transport_epoch == 0 &&
            fixture.server.cancelled_through_transport_epoch ==
                second_transport_epoch &&
            fixture.server.cancelled_transports == 2 &&
            fixture.server.retired_transport_initialized == 0 &&
            status.retired_sender_initialized == 0 &&
            status.retired_retained_count == 0
        ? 1u
        : 0u;
    CHECK(lifecycle_diagnostic.second_rotation_without_retired_bank == 1);

    const auto delayed_ack_result = deliver_to_server(
        fixture, delayed_old_ack, second_rotation_time + 2u);
    lifecycle_diagnostic.delayed_old_ack_stripped =
        delayed_ack_result == NETCHAN_APP_RX_EXPOSE_LEGACY ? 1u : 0u;
    const uint32_t submits_before = fake_event_runtime.submit_calls;
    const uint32_t resets_before = fake_event_runtime.reset_calls;
    const auto delayed_data_result = deliver_to_client(
        fixture, old_descriptor, second_rotation_time + 3u);
    lifecycle_diagnostic.delayed_old_data_stripped =
        delayed_data_result == NETCHAN_APP_RX_EXPOSE_LEGACY ? 1u : 0u;
    CHECK(fake_event_runtime.submit_calls == submits_before &&
          fake_event_runtime.reset_calls == resets_before);

    const auto corrupt_old_data = corrupted_copy(old_descriptor);
    const auto corrupt_old_ack = corrupted_copy(delayed_old_ack);
    if (deliver_to_client(
            fixture, corrupt_old_data,
            second_rotation_time + 4u) == NETCHAN_APP_RX_REJECT) {
        ++lifecycle_diagnostic.corrupt_old_rejections;
    }
    if (deliver_to_server(
            fixture, corrupt_old_ack,
            second_rotation_time + 4u) == NETCHAN_APP_RX_REJECT) {
        ++lifecycle_diagnostic.corrupt_old_rejections;
    }

    CHECK(lifecycle_diagnostic.client_due_after_exhaustion == 0 &&
          lifecycle_diagnostic.old_retained_before_rotation == 1 &&
          lifecycle_diagnostic.client_cancelled_receipts == 1 &&
          lifecycle_diagnostic.server_cancelled_events == 2 &&
          lifecycle_diagnostic.client_cancellation_floor ==
              old_transport_epoch &&
          lifecycle_diagnostic.server_cancellation_floor ==
              old_transport_epoch &&
          lifecycle_diagnostic.retired_ack_receipts_after_rotation == 0 &&
          lifecycle_diagnostic.retired_retained_after_rotation == 0 &&
          lifecycle_diagnostic.delayed_old_ack_stripped == 1 &&
          lifecycle_diagnostic.delayed_old_data_stripped == 1 &&
          lifecycle_diagnostic.corrupt_old_rejections == 2);

    SV_NativeShadowPeerDestroyV1(&fixture.server);
    fixture.server_live = false;
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
}

} // namespace

extern "C" cvar_t *Cvar_Get(const char *name, const char *, int)
{
    if (name &&
        std::strcmp(name, "cl_worr_native_event_shadow") == 0) {
        return &event_shadow_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_worr_native_snapshot_shadow") == 0) {
        return &snapshot_shadow_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_worr_native_shadow_probe_hold") == 0) {
        return &probe_hold_cvar;
    }
    return &shadow_cvar;
}

extern "C" bool Netchan_SetApplicationTxHook(
    netchan_t *channel, netchan_app_tx_prepare_fn prepare,
    netchan_app_tx_completion_fn completion, void *opaque)
{
    if (!channel || channel->type != NETCHAN_NEW ||
        (!!prepare != !!completion)) {
        return false;
    }
    channel->app_tx_prepare = prepare;
    channel->app_tx_completion = completion;
    channel->app_tx_opaque = prepare ? opaque : nullptr;
    return true;
}

extern "C" bool Netchan_SetApplicationRxHook(
    netchan_t *channel, netchan_app_rx_fn receive, void *opaque)
{
    if (!channel || channel->type != NETCHAN_NEW)
        return false;
    channel->app_rx = receive;
    channel->app_rx_opaque = receive ? opaque : nullptr;
    return true;
}

extern "C" void SZ_Init(sizebuf_t *buffer, void *data, size_t size,
                         const char *tag)
{
    std::memset(buffer, 0, sizeof(*buffer));
    buffer->data = static_cast<byte *>(data);
    buffer->maxsize = static_cast<uint32_t>(size);
    buffer->tag = tag;
}

extern "C" void SZ_InitWrite(sizebuf_t *buffer, void *data, size_t size)
{
    SZ_Init(buffer, data, size, "native event virtual link test");
    buffer->allowoverflow = true;
}

extern "C" void SZ_Clear(sizebuf_t *buffer)
{
    buffer->cursize = 0;
    buffer->readcount = 0;
    buffer->overflowed = false;
}

extern "C" void *SZ_GetSpace(sizebuf_t *buffer, size_t size)
{
    CHECK(buffer && buffer->data && size <= buffer->maxsize);
    if (size > buffer->maxsize - buffer->cursize) {
        CHECK(buffer->allowoverflow);
        SZ_Clear(buffer);
        buffer->overflowed = true;
    }
    byte *result = buffer->data + buffer->cursize;
    buffer->cursize += static_cast<uint32_t>(size);
    return result;
}

extern "C" q2proto_error_t q2proto_client_write(
    q2proto_clientcontext_t *, uintptr_t io_argument,
    const q2proto_clc_message_t *message)
{
    if (!io_argument || !message || message->type != Q2P_CLC_SETTING)
        return Q2P_ERR_BAD_COMMAND;
    auto *io = reinterpret_cast<q2protoio_ioarg_t *>(io_argument);
    if (!io->sz_write)
        return Q2P_ERR_BAD_DATA;
    byte *wire = static_cast<byte *>(SZ_GetSpace(io->sz_write, 5));
    wire[0] = static_cast<byte>(clc_setting);
    const uint16_t index = static_cast<uint16_t>(message->setting.index);
    const uint16_t value = static_cast<uint16_t>(message->setting.value);
    wire[1] = static_cast<byte>(index);
    wire[2] = static_cast<byte>(index >> 8);
    wire[3] = static_cast<byte>(value);
    wire[4] = static_cast<byte>(value >> 8);
    return Q2P_ERR_SUCCESS;
}

extern "C" bool CL_SnapshotShadowBindNativeEpoch(uint32_t)
{
    return false;
}

extern "C" bool CL_SnapshotShadowLatest(
    worr_snapshot_projection_view_v2 *,
    worr_snapshot_projection_hashes_v2 *,
    worr_snapshot_ref_v2 *)
{
    return false;
}

extern "C" cl_snapshot_shadow_native_expectation_result_v1
CL_SnapshotShadowGetNativeExpectation(
    worr_snapshot_id_v2,
    worr_native_snapshot_expectation_v1 *)
{
    return CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_INVALID;
}

extern "C" bool CL_SnapshotShadowGetNativeConsumerV1(
    worr_native_snapshot_consumer_v1 *)
{
    return false;
}

int main()
{
    test_bidirectional_loss_reorder_duplicate_and_ack_loss();
    test_multiple_events_selective_receipt_under_semantic_reordering();
    test_corruption_is_directional_and_fail_closed();
    test_epoch_cancellation_strips_old_ack_and_data();
    test_private_authority_receipt_rejection_over_native_virtual_link();
    test_private_authority_receipt_confirmation_over_native_virtual_link();
    test_private_authority_receipt_first_reconciliation_over_native_virtual_link();
    test_receipt_history_loss_forces_native_resync();
    test_private_authority_receipt_conflict_forces_native_resync();
    diagnose_exhausted_ack_credit_lifecycle_gap();
    std::printf(
        "native_event_virtual_link_test: ok "
        "converged=%u s2c_loss=%u c2s_loss=%u ack_loss=%u "
        "reordered=%u duplicates=%u corrupt_s2c=%u corrupt_c2s=%u "
        "cancelled_ack_strip=%u cancelled_data_strip=%u "
        "cancelled_corrupt_reject=%u presented=%u "
        "digest=%016llx\n",
        metrics.converged_scenarios,
        metrics.server_to_client_losses,
        metrics.client_to_server_losses,
        metrics.ack_losses,
        metrics.reordered_deliveries,
        metrics.duplicate_deliveries,
        metrics.corrupt_server_to_client,
        metrics.corrupt_client_to_server,
        metrics.cancelled_ack_strips,
        metrics.cancelled_data_strips,
        metrics.cancelled_corrupt_rejections,
        metrics.exactly_once_presentations,
        static_cast<unsigned long long>(metrics_digest()));
    std::printf(
        "native_event_virtual_link_test: lifecycle_diagnostic "
        "ack_handoffs=%u due_after_exhaustion=%u "
        "old_retained=%u client_cancelled_receipts=%u "
        "server_cancelled_events=%u client_floor=%u server_floor=%u "
        "retired_receipts=%u retired_retained=%u release_due=%u "
        "second_rotation_no_retired=%u delayed_old_ack_strip=%u "
        "delayed_old_data_strip=%u corrupt_old_rejections=%u\n",
        lifecycle_diagnostic.accepted_lost_ack_handoffs,
        lifecycle_diagnostic.client_due_after_exhaustion,
        lifecycle_diagnostic.old_retained_before_rotation,
        lifecycle_diagnostic.client_cancelled_receipts,
        lifecycle_diagnostic.server_cancelled_events,
        lifecycle_diagnostic.client_cancellation_floor,
        lifecycle_diagnostic.server_cancellation_floor,
        lifecycle_diagnostic.retired_ack_receipts_after_rotation,
        lifecycle_diagnostic.retired_retained_after_rotation,
        lifecycle_diagnostic.release_due,
        lifecycle_diagnostic.second_rotation_without_retired_bank,
        lifecycle_diagnostic.delayed_old_ack_stripped,
        lifecycle_diagnostic.delayed_old_data_stripped,
        lifecycle_diagnostic.corrupt_old_rejections);
    return EXIT_SUCCESS;
}
