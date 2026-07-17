/*
 * Production-shaped FR-10-T04/T06 snapshot virtual-link coverage.
 *
 * This standalone test begins at the real per-peer server final-emission
 * shadow, then crosses the native snapshot codec, fragmented envelope,
 * carrier, retained RX session, semantic admission transaction, and exact
 * cgame receipt boundary.  It never opens a socket or launches a client.
 */

#include "common/net/native_carrier.h"
#include "common/net/native_snapshot_admission.h"
#include "server/snapshot_shadow.h"
#include "shared/cgame_event_runtime.h"
#include "shared/event_abi.h"
#include "cg_canonical_snapshot_timeline.hpp"
#include "cg_event_runtime.hpp"
#include "cg_local.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

cgame_import_t cgi{};

namespace {

constexpr uint32_t kMaxEntities = 16;
constexpr uint32_t kShadowSlotCount = 4;
constexpr uint32_t kEntitiesPerSlot = 8;
constexpr uint32_t kAreaBytesPerSlot = 8;
constexpr uint16_t kRxSlotCount = 2;
constexpr uint32_t kPayloadStride = 8192;
constexpr uint16_t kFragmentDatagramBytes =
    WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES + 192u;
constexpr uint32_t kTransportEpoch = 91;
constexpr uint64_t kOwnerId = UINT64_C(0x3141592653589793);

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr, "native_snapshot_virtual_link_test:%d: %s\n",
                 line, expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

bool snapshot_id_equal(worr_snapshot_id_v2 left,
                       worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch &&
           left.sequence == right.sequence;
}

bool hashes_equal(const worr_snapshot_projection_hashes_v2 &left,
                  const worr_snapshot_projection_hashes_v2 &right)
{
    return left.struct_size == right.struct_size &&
           left.schema_version == right.schema_version &&
           left.endpoint_hash == right.endpoint_hash &&
           left.legacy_parity_hash == right.legacy_parity_hash &&
           left.semantic_player_hash == right.semantic_player_hash &&
           left.semantic_entity_hash == right.semantic_entity_hash &&
           left.semantic_area_hash == right.semantic_area_hash &&
           left.semantic_event_hash == right.semantic_event_hash;
}

q2proto_svc_playerstate_t full_player()
{
    q2proto_svc_playerstate_t player{};
    player.delta_bits = Q2P_PSD_PM_GRAVITY | Q2P_PSD_FOV |
                        Q2P_PSD_PM_VIEWHEIGHT;
    player.pm_gravity = 800;
    player.pm_viewheight = 22;
    player.fov = 100;
    player.statbits = UINT64_C(1) << 2;
    player.stats[2] = 77;
    return player;
}

q2proto_entity_state_delta_t entity_delta(uint16_t model, uint16_t frame,
                                           uint8_t event = 0)
{
    q2proto_entity_state_delta_t delta{};
    delta.delta_bits = Q2P_ESD_MODELINDEX | Q2P_ESD_FRAME;
    delta.modelindex = model;
    delta.frame = frame;
    if (event != 0) {
        delta.delta_bits |= Q2P_ESD_EVENT;
        delta.event = event;
    }
    return delta;
}

q2proto_entity_state_delta_t event_delta(uint8_t event)
{
    q2proto_entity_state_delta_t delta{};
    delta.delta_bits = Q2P_ESD_EVENT;
    delta.event = event;
    return delta;
}

struct frame_carrier_t {
    q2proto_svc_frame_t frame{};
    std::array<q2proto_svc_frame_entity_delta_t, 12> deltas{};
    uint32_t count{1};
    std::array<uint8_t, 4> area{1, 2, 3, 4};
};

frame_carrier_t make_frame(int wire_frame, int wire_base, bool full_ps)
{
    frame_carrier_t result{};
    result.frame.serverframe = wire_frame;
    result.frame.deltaframe = wire_base;
    result.frame.areabits_len = result.area.size();
    if (full_ps)
        result.frame.playerstate = full_player();
    return result;
}

void add_delta(frame_carrier_t &frame, uint16_t entity,
               const q2proto_entity_state_delta_t &delta,
               bool remove = false)
{
    CHECK(frame.count < frame.deltas.size());
    auto &carrier = frame.deltas[frame.count - 1u];
    carrier.newnum = entity;
    carrier.remove = remove;
    carrier.entity_delta = delta;
    ++frame.count;
    frame.deltas[frame.count - 1u] = {};
}

sv_snapshot_shadow_config_v1 shadow_config()
{
    sv_snapshot_shadow_config_v1 result{};
    result.struct_size = sizeof(result);
    result.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    result.snapshot_epoch = 31;
    result.max_entities = kMaxEntities;
    result.max_models = 64;
    result.max_sounds = 64;
    result.slot_capacity = kShadowSlotCount;
    result.entities_per_slot = kEntitiesPerSlot;
    result.area_bytes_per_slot = kAreaBytesPerSlot;
    result.beam_renderfx_mask = UINT32_C(1) << 7;
    result.legacy_renderfx_allowed_mask =
        (UINT32_C(1) << 19) - 1u;
    result.legacy_beam_clear_mask = UINT32_C(1) << 9;
    result.extended_entity_state = 1;
    return result;
}

sv_snapshot_shadow_ref_v1 send_frame(
    sv_snapshot_shadow_peer_v1 *peer, frame_carrier_t &carrier,
    uint32_t authoritative_tick, uint32_t authoritative_tick_delta,
    uint64_t authoritative_time_us)
{
    sv_snapshot_shadow_frame_v1 input{};
    input.struct_size = sizeof(input);
    input.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    carrier.frame.areabits = carrier.area.data();
    input.wire_frame = &carrier.frame;
    input.authoritative_server_tick = authoritative_tick;
    input.authoritative_tick_delta = authoritative_tick_delta;
    input.authoritative_server_time_us = authoritative_time_us;
    input.controlled_entity_index = 1;
    input.canonical_movement_type = 0;
    input.canonical_movement_flags = 0;
    input.team_id = 0;
    CHECK(SV_SnapshotShadowBeginFrameV1(peer, &input) ==
          SV_SNAPSHOT_SHADOW_OK);
    for (uint32_t index = 0; index < carrier.count; ++index) {
        CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(
                  peer, &carrier.deltas[index]) ==
              SV_SNAPSHOT_SHADOW_OK);
    }
    sv_snapshot_shadow_ref_v1 ref{UINT32_MAX, UINT32_MAX};
    CHECK(SV_SnapshotShadowCommitFrameV1(peer, &ref) ==
          SV_SNAPSHOT_SHADOW_OK);
    return ref;
}

struct server_projection_t {
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
};

server_projection_t server_projection(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref)
{
    server_projection_t result{};
    CHECK(SV_SnapshotShadowViewV1(
              peer, ref, &result.view, &result.hashes) ==
          SV_SNAPSHOT_SHADOW_OK);
    worr_snapshot_projection_hashes_v2 recomputed{};
    CHECK(Worr_SnapshotProjectionHashesV2(
        &result.view, kMaxEntities, &recomputed));
    CHECK(hashes_equal(result.hashes, recomputed));
    return result;
}

struct real_consumer_probe_t {
    worr_snapshot_projection_hashes_v2 last_hashes{};
    uint32_t reset_calls{};
    uint32_t consume_calls{};
    uint32_t status_calls{};
    uint32_t last_entity_count{};
    uint32_t last_event_count{};
};

const worr_cgame_snapshot_timeline_export_v2 *real_consumer_api()
{
    const auto *api = CG_GetCanonicalSnapshotTimelineAPI();

    CHECK(api != nullptr);
    CHECK(api->struct_size == sizeof(*api));
    CHECK(api->api_version ==
          WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION);
    return api;
}

void real_reset(void *opaque, uint32_t snapshot_epoch,
                uint32_t reason, uint64_t host_time_us)
{
    auto &probe = *static_cast<real_consumer_probe_t *>(opaque);

    ++probe.reset_calls;
    real_consumer_api()->Reset(
        snapshot_epoch, reason, host_time_us);
}

bool real_consume(
    void *opaque, const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_projection_hashes_v2 *hashes,
    uint64_t receive_time_us)
{
    auto &probe = *static_cast<real_consumer_probe_t *>(opaque);

    ++probe.consume_calls;
    if (view && hashes) {
        probe.last_hashes = *hashes;
        probe.last_entity_count = view->entity_count;
        probe.last_event_count = view->event_ref_count;
    }
    return real_consumer_api()->ConsumeCanonicalSnapshot(
        view, hashes, receive_time_us);
}

bool real_get_status(
    void *opaque,
    worr_cgame_snapshot_timeline_status_v2 *status_out)
{
    auto &probe = *static_cast<real_consumer_probe_t *>(opaque);

    ++probe.status_calls;
    return real_consumer_api()->GetStatus(status_out);
}

struct native_fixture_t {
    worr_native_session_binding_v1 binding{};
    worr_native_snapshot_admission_state_v1 admission{};
    worr_native_rx_session_v1 session{};
    std::array<worr_native_rx_slot_v1, kRxSlotCount> slots{};
    std::array<uint8_t, kRxSlotCount * kPayloadStride> arena{};
    worr_native_carrier_ack_ledger_v1 ledger{};
    worr_snapshot_v2 decoded_snapshot{};
    worr_snapshot_player_v2 decoded_player{};
    std::array<worr_snapshot_entity_v2, kEntitiesPerSlot>
        decoded_entities{};
    std::array<uint8_t, kAreaBytesPerSlot> decoded_area{};
    std::array<worr_snapshot_event_ref_v2, kEntitiesPerSlot>
        decoded_events{};
    worr_native_snapshot_decode_storage_v1 scratch{};
    real_consumer_probe_t probe{};
    worr_native_snapshot_consumer_v1 consumer{};
    uint32_t next_message_sequence{1};
    uint64_t next_tick{1};
};

void init_native_fixture(native_fixture_t &fixture)
{
    fixture = {};
    CG_EventRuntimeSetAuditEnabled(false);
    CHECK(!CG_EventRuntimeAuditEnabled());
    CHECK(CG_EventRuntimeResetAuthority(0, 0) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetLegacy(0) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(CG_EventRuntimeResetSnapshot(0) ==
          CG_EVENT_RUNTIME_OK);
    fixture.binding.struct_size = sizeof(fixture.binding);
    fixture.binding.schema_version =
        WORR_NATIVE_SESSION_ABI_VERSION;
    fixture.binding.transport_epoch = kTransportEpoch;
    fixture.binding.negotiated_capabilities =
        WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK;
    fixture.binding.connection_owner_id = kOwnerId;
    CHECK(Worr_NativeSessionBindingValidateV1(&fixture.binding));
    CHECK(Worr_NativeSnapshotAdmissionInitV1(
        &fixture.admission, &fixture.binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &fixture.session, fixture.slots.data(),
        static_cast<uint16_t>(fixture.slots.size()),
        kPayloadStride, 1000, 1000, &fixture.binding));
    CHECK(Worr_NativeCarrierAckLedgerInitV1(
        &fixture.ledger, &fixture.binding, 3));

    fixture.scratch.struct_size = sizeof(fixture.scratch);
    fixture.scratch.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    fixture.scratch.snapshot = &fixture.decoded_snapshot;
    fixture.scratch.player = &fixture.decoded_player;
    fixture.scratch.entities = fixture.decoded_entities.data();
    fixture.scratch.area_bytes = fixture.decoded_area.data();
    fixture.scratch.event_refs = fixture.decoded_events.data();
    fixture.scratch.entity_capacity =
        static_cast<uint32_t>(fixture.decoded_entities.size());
    fixture.scratch.area_capacity =
        static_cast<uint32_t>(fixture.decoded_area.size());
    fixture.scratch.event_ref_capacity =
        static_cast<uint32_t>(fixture.decoded_events.size());

    fixture.consumer.struct_size = sizeof(fixture.consumer);
    fixture.consumer.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    fixture.consumer.opaque = &fixture.probe;
    fixture.consumer.Reset = real_reset;
    fixture.consumer.ConsumeCanonicalSnapshot = real_consume;
    fixture.consumer.GetStatus = real_get_status;
}

struct encoded_projection_t {
    std::vector<uint8_t> bytes;
    worr_native_record_ref_v1 record{};
    worr_native_snapshot_expectation_v1 expectation{};
};

encoded_projection_t encode_projection(
    const server_projection_t &projection)
{
    encoded_projection_t result{};
    uint32_t preflight_bytes = 0;
    CHECK(Worr_NativeCodecSnapshotPreflightV1(
              &projection.view, kMaxEntities, &preflight_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(preflight_bytes != 0 &&
          preflight_bytes <= WORR_NATIVE_CODEC_MAX_ENCODED_BYTES);
    result.bytes.resize(preflight_bytes);
    size_t encoded_bytes = 0;
    CHECK(Worr_NativeCodecSnapshotEncodeV1(
              &projection.view, kMaxEntities, result.bytes.data(),
              result.bytes.size(), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes == preflight_bytes);

    worr_native_codec_info_v1 info{};
    CHECK(Worr_NativeCodecInspectV1(
              result.bytes.data(), result.bytes.size(), &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(
        &info, &result.record));
    result.expectation.struct_size =
        sizeof(result.expectation);
    result.expectation.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    result.expectation.snapshot_id =
        projection.view.snapshot->snapshot_id;
    result.expectation.hashes = projection.hashes;
    return result;
}

struct wire_packet_t {
    std::array<uint8_t, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES> bytes{};
    size_t count{};
};

wire_packet_t wrap_datagram(const native_fixture_t &fixture,
                            const void *datagram,
                            size_t datagram_bytes)
{
    wire_packet_t packet{};
    worr_native_carrier_entry_v1 entry{};
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entry.data_bytes = static_cast<uint32_t>(datagram_bytes);
    CHECK(Worr_NativeCarrierEncodeV1(
              fixture.binding.transport_epoch, nullptr, 0,
              datagram, datagram_bytes, &entry, 1,
              packet.bytes.data(), packet.bytes.size(),
              &packet.count) == WORR_NATIVE_CARRIER_OK);
    return packet;
}

struct staged_message_t {
    worr_native_rx_message_v1 message{};
    std::vector<wire_packet_t> packets;
};

staged_message_t stage_projection(
    native_fixture_t &fixture, const encoded_projection_t &encoded,
    bool reverse_delivery)
{
    staged_message_t staged{};
    worr_native_envelope_fragmenter_v1 fragmenter{};
    const uint32_t message_sequence =
        fixture.next_message_sequence++;

    CHECK(encoded.bytes.size() <= UINT32_MAX);
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, fixture.binding.transport_epoch,
        message_sequence, encoded.record, 2,
        encoded.bytes.data(),
        static_cast<uint32_t>(encoded.bytes.size()),
        kFragmentDatagramBytes));
    CHECK(fragmenter.fragment_count > 1);
    staged.packets.reserve(fragmenter.fragment_count);

    while ((fragmenter.state_flags &
            WORR_NATIVE_FRAGMENTER_EXHAUSTED) == 0) {
        std::array<uint8_t,
                   WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES>
            datagram{};
        size_t datagram_bytes = 0;
        CHECK(Worr_NativeEnvelopeFragmentNextV1(
                  &fragmenter, encoded.bytes.data(),
                  static_cast<uint32_t>(encoded.bytes.size()),
                  datagram.data(), datagram.size(),
                  &datagram_bytes) ==
              WORR_NATIVE_ENVELOPE_EMIT_OK);
        staged.packets.push_back(
            wrap_datagram(fixture, datagram.data(), datagram_bytes));
    }
    CHECK(staged.packets.size() == fragmenter.fragment_count);

    for (size_t delivery = 0;
         delivery < staged.packets.size(); ++delivery) {
        const size_t packet_index =
            reverse_delivery
                ? staged.packets.size() - delivery - 1u
                : delivery;
        const auto &packet = staged.packets[packet_index];
        worr_native_rx_result_v1 rx_result =
            WORR_NATIVE_RX_INVALID_STATE;
        worr_native_rx_message_v1 message{};
        CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
                  &fixture.session, fixture.slots.data(),
                  static_cast<uint16_t>(fixture.slots.size()),
                  fixture.arena.data(), fixture.arena.size(),
                  fixture.next_tick++, packet.bytes.data(),
                  packet.count, 0, &fixture.ledger,
                  &rx_result, &message) ==
              WORR_NATIVE_CARRIER_SESSION_OK);
        if (delivery + 1u == staged.packets.size()) {
            CHECK(rx_result == WORR_NATIVE_RX_MESSAGE_COMPLETE);
            staged.message = message;
        } else {
            CHECK(rx_result == WORR_NATIVE_RX_FRAGMENT_ACCEPTED);
        }
    }
    CHECK(staged.message.message_sequence == message_sequence);
    CHECK(staged.message.payload_bytes == encoded.bytes.size());
    return staged;
}

worr_native_snapshot_admission_result_v1 admit(
    native_fixture_t &fixture, const staged_message_t &staged,
    const worr_native_snapshot_expectation_v1 &expectation,
    uint64_t receive_time_us)
{
    return Worr_NativeSnapshotAdmissionCommitCompletedV1(
        &fixture.admission, &fixture.binding, &fixture.session,
        fixture.slots.data(),
        static_cast<uint16_t>(fixture.slots.size()),
        fixture.arena.data(), fixture.arena.size(),
        &fixture.ledger, &staged.message, kMaxEntities,
        receive_time_us, &fixture.scratch, &expectation,
        &fixture.consumer);
}

bool ledger_has_receipt(
    const worr_native_carrier_ack_ledger_v1 &ledger,
    uint32_t message_sequence)
{
    for (const auto &receipt : ledger.receipts) {
        if ((receipt.state_flags &
             WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED) != 0 &&
            receipt.message_sequence == message_sequence) {
            return true;
        }
    }
    return false;
}

void verify_exact_acceptance(
    const native_fixture_t &fixture,
    const server_projection_t &projection,
    const staged_message_t &staged,
    uint64_t receive_time_us,
    uint64_t accepted_count)
{
    const auto snapshot_id =
        projection.view.snapshot->snapshot_id;
    worr_cgame_snapshot_timeline_status_v2 consumer_status{};
    cg_event_runtime_status_v1 event_status{};

    CHECK(snapshot_id_equal(
        fixture.admission.last_accepted_snapshot, snapshot_id));
    CHECK(fixture.admission.last_endpoint_hash ==
          projection.hashes.endpoint_hash);
    CHECK(fixture.admission.last_legacy_parity_hash ==
          projection.hashes.legacy_parity_hash);
    CHECK(fixture.admission.last_snapshot_hash ==
          projection.view.snapshot->snapshot_hash);
    CHECK(fixture.admission.last_receive_time_us ==
          receive_time_us);
    CHECK(fixture.session.committed_snapshot_epoch ==
          snapshot_id.epoch);
    CHECK(fixture.session.committed_snapshot_sequence ==
          snapshot_id.sequence);
    CHECK(ledger_has_receipt(
        fixture.ledger, staged.message.message_sequence));

    CHECK(real_consumer_api()->GetStatus(&consumer_status));
    CHECK(consumer_status.active_epoch == snapshot_id.epoch);
    CHECK(consumer_status.accepted == accepted_count);
    CHECK(consumer_status.consume_attempts == accepted_count);
    CHECK(consumer_status.admission_generation ==
          accepted_count);
    CHECK(consumer_status.last_receive_time_us ==
          receive_time_us);
    CHECK(snapshot_id_equal(
        consumer_status.last_snapshot_id, snapshot_id));
    CHECK(consumer_status.last_snapshot_hash ==
          projection.view.snapshot->snapshot_hash);
    CHECK(consumer_status.last_endpoint_hash ==
          projection.hashes.endpoint_hash);
    CHECK(consumer_status.last_legacy_parity_hash ==
          projection.hashes.legacy_parity_hash);
    CHECK((consumer_status.receipt_flags &
           (WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED |
            WORR_CGAME_SNAPSHOT_RECEIPT_EVENT_FENCE_ACCEPTED)) ==
          (WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED |
           WORR_CGAME_SNAPSHOT_RECEIPT_EVENT_FENCE_ACCEPTED));
    CHECK(consumer_status.last_event_fence_result ==
          (projection.view.event_ref_count != 0
               ? CG_EVENT_RUNTIME_OK
               : CG_EVENT_RUNTIME_EMPTY));
    CHECK(hashes_equal(
        fixture.probe.last_hashes, projection.hashes));
    CHECK(fixture.probe.last_entity_count ==
          projection.view.entity_count);
    CHECK(fixture.probe.last_event_count ==
          projection.view.event_ref_count);
    CHECK(CG_EventRuntimeGetStatus(&event_status));
    CHECK(event_status.audit_enabled == 0);
    CHECK(event_status.snapshot_epoch == snapshot_id.epoch);
    CHECK(event_status.reference_count == 0);
    CHECK(CG_EventRuntimeSnapshotFenceHealthy(snapshot_id.epoch));
    CHECK(fixture.decoded_snapshot.snapshot_hash ==
          projection.view.snapshot->snapshot_hash);
}

struct transport_snapshot_t {
    worr_native_rx_session_v1 session{};
    std::array<worr_native_rx_slot_v1, kRxSlotCount> slots{};
    worr_native_carrier_ack_ledger_v1 ledger{};
};

transport_snapshot_t capture_transport(
    const native_fixture_t &fixture)
{
    transport_snapshot_t result{};
    result.session = fixture.session;
    result.slots = fixture.slots;
    result.ledger = fixture.ledger;
    return result;
}

bool transport_equal(const native_fixture_t &fixture,
                     const transport_snapshot_t &snapshot)
{
    return std::memcmp(
               &fixture.session, &snapshot.session,
               sizeof(fixture.session)) == 0 &&
           std::memcmp(
               fixture.slots.data(), snapshot.slots.data(),
               sizeof(fixture.slots)) == 0 &&
           std::memcmp(
               &fixture.ledger, &snapshot.ledger,
               sizeof(fixture.ledger)) == 0;
}

void revalidate_repeat(native_fixture_t &fixture,
                       const staged_message_t &staged)
{
    CHECK(!staged.packets.empty());
    const auto before = capture_transport(fixture);
    worr_native_rx_result_v1 rx_result =
        WORR_NATIVE_RX_INVALID_STATE;
    worr_native_rx_message_v1 message{};
    const auto &packet = staged.packets.front();

    CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
              &fixture.session, fixture.slots.data(),
              static_cast<uint16_t>(fixture.slots.size()),
              fixture.arena.data(), fixture.arena.size(),
              fixture.next_tick++, packet.bytes.data(),
              packet.count, 0, &fixture.ledger,
              &rx_result, &message) ==
          WORR_NATIVE_CARRIER_SESSION_SEMANTIC_REVALIDATION_REQUIRED);
    CHECK(transport_equal(fixture, before));
    const uint32_t consume_calls = fixture.probe.consume_calls;
    CHECK(Worr_NativeSnapshotAdmissionRevalidateCommittedRepeatV1(
              &fixture.admission, &fixture.binding,
              &fixture.session, fixture.slots.data(),
              static_cast<uint16_t>(fixture.slots.size()),
              fixture.arena.data(), fixture.arena.size(),
              fixture.next_tick++, packet.bytes.data(),
              packet.count, 0, &fixture.ledger,
              &fixture.consumer) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_REPEAT_REVALIDATED);
    CHECK(fixture.probe.consume_calls == consume_calls);
    CHECK(fixture.ledger.telemetry.repeat_refreshes == 1);
}

void test_server_final_view_native_virtual_link()
{
    const auto config = shadow_config();
    auto *peer = SV_SnapshotShadowCreateV1(&config);
    CHECK(peer != nullptr);
    native_fixture_t fixture{};
    init_native_fixture(fixture);

    auto frame10 = make_frame(10, -1, true);
    add_delta(
        frame10, 2,
        entity_delta(
            2, 1, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP));
    add_delta(frame10, 4, entity_delta(4, 1));
    add_delta(
        frame10, 7,
        entity_delta(
            7, 1,
            WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT));
    const auto ref10 = send_frame(
        peer, frame10, 1000, 0, UINT64_C(25000000));
    const auto keyframe = server_projection(peer, ref10);
    CHECK((keyframe.view.snapshot->flags &
           WORR_SNAPSHOT_FLAG_KEYFRAME) != 0);
    CHECK(keyframe.view.snapshot->server_tick == 1000);
    CHECK(keyframe.view.snapshot->snapshot_id.sequence == 11);
    CHECK(keyframe.view.entity_count == 3);
    CHECK(keyframe.view.event_ref_count == 2);
    CHECK(keyframe.hashes.endpoint_hash != 0);
    CHECK(keyframe.hashes.legacy_parity_hash != 0);

    const auto encoded_keyframe = encode_projection(keyframe);
    const auto staged_keyframe = stage_projection(
        fixture, encoded_keyframe, true);
    constexpr uint64_t keyframe_receive_time =
        UINT64_C(30000000);
    CHECK(admit(
              fixture, staged_keyframe,
              encoded_keyframe.expectation,
              keyframe_receive_time) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_ACCEPTED);
    verify_exact_acceptance(
        fixture, keyframe, staged_keyframe,
        keyframe_receive_time, 1);
    CHECK(fixture.admission.accepted_snapshots == 1);
    CHECK(fixture.admission.accepted_keyframes == 1);
    CHECK(fixture.admission.accepted_full_updates == 0);
    CHECK(fixture.probe.reset_calls == 1);
    revalidate_repeat(fixture, staged_keyframe);

    auto frame11 = make_frame(11, 10, false);
    const auto ref11 = send_frame(
        peer, frame11, 1004, 4, UINT64_C(25100000));
    const auto full_update = server_projection(peer, ref11);
    CHECK((full_update.view.snapshot->flags &
           WORR_SNAPSHOT_FLAG_KEYFRAME) == 0);
    CHECK(snapshot_id_equal(
        full_update.view.snapshot->base_id,
        keyframe.view.snapshot->snapshot_id));
    CHECK(full_update.view.snapshot->server_tick == 1004);
    CHECK(full_update.view.entity_count == 3);
    CHECK(full_update.view.event_ref_count == 0);
    CHECK((full_update.view.snapshot->discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_RATE_SUPPRESSED) != 0);

    const auto encoded_update = encode_projection(full_update);
    const auto staged_update = stage_projection(
        fixture, encoded_update, true);
    constexpr uint64_t update_receive_time =
        UINT64_C(30100000);
    CHECK(admit(
              fixture, staged_update,
              encoded_update.expectation,
              update_receive_time) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_FULL_UPDATE_ACCEPTED);
    verify_exact_acceptance(
        fixture, full_update, staged_update,
        update_receive_time, 2);
    CHECK(fixture.admission.accepted_snapshots == 2);
    CHECK(fixture.admission.accepted_keyframes == 1);
    CHECK(fixture.admission.accepted_full_updates == 1);
    CHECK(fixture.probe.consume_calls == 2);
    CHECK(fixture.probe.reset_calls == 1);

    auto frame12 = make_frame(12, 11, false);
    add_delta(
        frame12, 4,
        event_delta(
            WORR_EVENT_LEGACY_ENTITY_OTHER_TELEPORT));
    const auto ref12 = send_frame(
        peer, frame12, 1005, 1, UINT64_C(25125000));
    const auto endpoint_projection =
        server_projection(peer, ref12);
    CHECK(endpoint_projection.view.event_ref_count == 1);
    const auto encoded_endpoint =
        encode_projection(endpoint_projection);
    const auto staged_endpoint = stage_projection(
        fixture, encoded_endpoint, true);
    CHECK(!ledger_has_receipt(
        fixture.ledger,
        staged_endpoint.message.message_sequence));

    /*
     * Endpoint hashes include local provenance and chronology.  They are
     * validated exactly at the native cgame receipt boundary, but are not a
     * cross-endpoint legacy-parity proof.
     */
    auto endpoint_local_expectation =
        encoded_endpoint.expectation;
    endpoint_local_expectation.hashes.endpoint_hash ^=
        UINT64_C(1);
    CHECK(admit(
              fixture, staged_endpoint,
              endpoint_local_expectation,
              UINT64_C(30200000)) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_FULL_UPDATE_ACCEPTED);
    verify_exact_acceptance(
        fixture, endpoint_projection, staged_endpoint,
        UINT64_C(30200000), 3);

    auto frame13 = make_frame(13, 12, false);
    const auto ref13 = send_frame(
        peer, frame13, 1006, 1, UINT64_C(25150000));
    const auto mismatch_projection =
        server_projection(peer, ref13);
    const auto encoded_mismatch =
        encode_projection(mismatch_projection);
    const auto staged_mismatch = stage_projection(
        fixture, encoded_mismatch, true);
    CHECK(!ledger_has_receipt(
        fixture.ledger,
        staged_mismatch.message.message_sequence));

    const auto before_transport = capture_transport(fixture);
    const auto before_admission = fixture.admission;
    const uint32_t before_consume_calls =
        fixture.probe.consume_calls;
    const uint32_t before_reset_calls =
        fixture.probe.reset_calls;
    const uint32_t before_status_calls =
        fixture.probe.status_calls;
    auto mismatched_expectation =
        encoded_mismatch.expectation;
    mismatched_expectation.hashes.legacy_parity_hash ^=
        UINT64_C(1);
    CHECK(admit(
              fixture, staged_mismatch,
              mismatched_expectation,
              UINT64_C(30300000)) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_PARITY_MISMATCH);
    CHECK(transport_equal(fixture, before_transport));
    CHECK(!ledger_has_receipt(
        fixture.ledger,
        staged_mismatch.message.message_sequence));
    CHECK(snapshot_id_equal(
        fixture.admission.last_accepted_snapshot,
        before_admission.last_accepted_snapshot));
    CHECK(fixture.admission.last_endpoint_hash ==
          before_admission.last_endpoint_hash);
    CHECK(fixture.admission.last_legacy_parity_hash ==
          before_admission.last_legacy_parity_hash);
    CHECK(fixture.admission.last_snapshot_hash ==
          before_admission.last_snapshot_hash);
    CHECK(fixture.admission.parity_rejections ==
          before_admission.parity_rejections + 1u);
    CHECK((fixture.admission.state_flags &
           WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME) != 0);
    CHECK(fixture.probe.consume_calls == before_consume_calls);
    CHECK(fixture.probe.reset_calls == before_reset_calls);
    CHECK(fixture.probe.status_calls == before_status_calls);

    SV_SnapshotShadowDestroyV1(peer);
}

} // namespace

int main()
{
    test_server_final_view_native_virtual_link();
    std::puts("native_snapshot_virtual_link_test: ok");
    return EXIT_SUCCESS;
}
