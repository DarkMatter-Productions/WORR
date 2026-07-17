/*
 * Headless production-wrapper coverage for the native canonical-snapshot
 * path.  The test installs the real client/server netchan callbacks, performs
 * the four-message readiness handshake, queues an exact final server
 * snapshot, and independently reconstructs the matching legacy expectation
 * through the production client snapshot shadow.
 *
 * No socket, renderer, window, input backend, or interactive client is
 * created.  All impairment is applied to copied application payloads.
 */

#include "client.h"
#include "client/cgame_event_runtime.h"
#include "client/native_readiness_pilot.h"
#include "client/snapshot_shadow.h"
#include "server/native_shadow.h"
#include "server/snapshot_shadow.h"

#include "common/net/native_carrier.h"
#include "common/net/native_codec.h"
#include "common/net/native_readiness_sideband.h"
#include "common/net/native_snapshot_receiver.h"
#include "cg_canonical_snapshot_timeline.hpp"
#include "cg_event_runtime.hpp"
#include "cg_prediction_authority.hpp"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

client_static_t cls{};
client_state_t cl{};
unsigned com_localTime{};
cgame_import_t cgi{};

#if USE_DEBUG
cvar_t *developer{};
#endif

namespace {

constexpr uint32_t kPublicCapabilities =
    WORR_NET_CAP_LEGACY_STAGE_MASK;
constexpr uint32_t kPrivateSnapshotCapabilities =
    WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK;
constexpr uint32_t kProjectionEntityCapacity = 64;
constexpr uint32_t kLegacyEntityIndexLimit = MAX_EDICTS_OLD;
constexpr uint32_t kRereleaseEntityIndexLimit = MAX_EDICTS;
constexpr uint32_t kMaxModels = 64;
constexpr uint32_t kMaxSounds = 64;
constexpr uint32_t kEntityCount = 48;
constexpr uint32_t kFirstEntity = 2;
constexpr uint32_t kShadowSlots = 4;
constexpr uint32_t kAreaBytesPerSlot = 8;
constexpr uint32_t kApplicationCeiling =
    WORR_NATIVE_CARRIER_MAX_PACKET_BYTES;
constexpr uint32_t kKeyframeNumber = 10;
constexpr uint64_t kKeyframeTimeUs = UINT64_C(160000);
constexpr uint32_t kCommandEpoch = 37;

static_assert(kEntityCount < kProjectionEntityCapacity);
static_assert(kLegacyEntityIndexLimit >
              kProjectionEntityCapacity);
static_assert(kRereleaseEntityIndexLimit >
              kLegacyEntityIndexLimit);
static_assert(kPrivateSnapshotCapabilities == UINT32_C(0x57));

cvar_t native_shadow_cvar{};
cvar_t event_shadow_cvar{};
cvar_t snapshot_shadow_mode_cvar{};
cvar_t probe_hold_cvar{};
cvar_t projection_shadow_cvar{};
cvar_t projection_debug_cvar{};
std::array<byte, 1024> reliable_storage{};

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(
        stderr,
        "native_snapshot_production_virtual_link_test:%d: %s\n",
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

bool consumed_command_equal(
    worr_snapshot_consumed_command_v2 left,
    worr_snapshot_consumed_command_v2 right)
{
    return left.cursor.epoch == right.cursor.epoch &&
           left.cursor.contiguous_sequence ==
               right.cursor.contiguous_sequence &&
           left.provenance == right.provenance &&
           left.reserved0 == right.reserved0;
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

bool expectation_parity_equal(
    const worr_snapshot_projection_hashes_v2 &left,
    const worr_snapshot_projection_hashes_v2 &right)
{
    return left.struct_size == right.struct_size &&
           left.schema_version == right.schema_version &&
           left.legacy_parity_hash == right.legacy_parity_hash &&
           left.semantic_player_hash == right.semantic_player_hash &&
           left.semantic_entity_hash == right.semantic_entity_hash &&
           left.semantic_area_hash == right.semantic_area_hash &&
           left.semantic_event_hash == right.semantic_event_hash;
}

struct wire_packet_t {
    std::array<byte, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES> bytes{};
    size_t count{};
    netchan_app_tx_prepare_output_v1_t completion{};
};

struct frame_carrier_t {
    q2proto_svc_frame_t wire{};
    std::array<q2proto_svc_frame_entity_delta_t,
               kEntityCount + 1>
        deltas{};
    uint32_t delta_count{};
    std::array<uint8_t, 4> area{1, 2, 3, 4};
    player_state_t legacy_player{};
    std::array<entity_state_t, kEntityCount> legacy_entities{};
    worr_snapshot_consumed_command_v2 consumed_command{};
};

struct server_projection_t {
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
};

struct fixture_t {
    sv_native_shadow_peer_v1 server{};
    netchan_t server_channel{};
    sv_snapshot_shadow_peer_v1 *server_snapshot_shadow{};
    bool server_live{};
    uint32_t client_rx_sequence{1};
    uint32_t server_rx_sequence{1};
    uint32_t official_epoch{};
    uint32_t snapshot_epoch{};
    uint32_t transport_epoch{};
    uint32_t entity_index_limit{};
    int server_protocol{PROTOCOL_VERSION_RERELEASE};
    worr_native_readiness_record_v1 deferred_active_confirm{};
    bool active_confirm_deferred{};
};

struct metrics_t {
    uint32_t fragmented_messages{};
    uint32_t server_to_client_losses{};
    uint32_t reordered_deliveries{};
    uint32_t duplicate_deliveries{};
    uint32_t lost_acknowledgements{};
    uint32_t repeat_revalidations{};
    uint32_t exact_cgame_consumes{};
    uint32_t hash_quarantines{};
    uint32_t wrong_epoch_rejections{};
    uint32_t real_domain_activations{};
    uint32_t expectation_window_rollovers{};
    uint32_t complete_timeout_recoveries{};
    uint32_t prediction_authorities{};
};

metrics_t metrics{};

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

q2proto_svc_playerstate_t full_wire_player()
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

player_state_t full_legacy_player()
{
    player_state_t player{};
    player.pmove.gravity = 800;
    player.pmove.viewheight = 22;
    player.fov = 100.0f;
    player.stats[2] = 77;
    return player;
}

q2proto_entity_state_delta_t entity_delta(uint16_t model,
                                           uint16_t frame)
{
    q2proto_entity_state_delta_t delta{};
    delta.delta_bits = Q2P_ESD_MODELINDEX | Q2P_ESD_FRAME;
    delta.modelindex = model;
    delta.frame = frame;
    return delta;
}

frame_carrier_t make_keyframe(
    uint32_t entity_index_limit,
    uint32_t frame_number = kKeyframeNumber)
{
    frame_carrier_t frame{};
    CHECK(entity_index_limit >
          kFirstEntity + kEntityCount);
    frame.wire.serverframe = static_cast<int32_t>(frame_number);
    frame.wire.deltaframe = -1;
    frame.wire.playerstate = full_wire_player();
    frame.legacy_player = full_legacy_player();
    frame.consumed_command.cursor.epoch = kCommandEpoch;
    frame.consumed_command.cursor.contiguous_sequence = frame_number;
    frame.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;

    for (uint32_t index = 0; index < kEntityCount; ++index) {
        const uint32_t entity_number =
            index + 1u == kEntityCount
                ? entity_index_limit - 1u
                : kFirstEntity + index;
        const uint16_t model =
            static_cast<uint16_t>((entity_number % (kMaxModels - 1u)) + 1u);
        auto &carrier = frame.deltas[index];
        carrier.newnum = static_cast<uint16_t>(entity_number);
        carrier.entity_delta = entity_delta(model, 1);

        auto &legacy = frame.legacy_entities[index];
        legacy.number = static_cast<int>(entity_number);
        legacy.modelindex = model;
        legacy.frame = 1;
    }
    frame.delta_count = kEntityCount + 1u;
    frame.deltas[kEntityCount] = {};
    return frame;
}

frame_carrier_t make_delta_frame(const frame_carrier_t &base,
                                 bool client_variant)
{
    frame_carrier_t frame{};
    frame.wire.serverframe =
        static_cast<int32_t>(kKeyframeNumber + 1u);
    frame.wire.deltaframe = static_cast<int32_t>(kKeyframeNumber);
    frame.area = base.area;
    frame.legacy_player = base.legacy_player;
    frame.legacy_entities = base.legacy_entities;
    frame.consumed_command = base.consumed_command;
    frame.consumed_command.cursor.contiguous_sequence =
        static_cast<uint32_t>(frame.wire.serverframe);

    const uint16_t model =
        static_cast<uint16_t>(client_variant ? 61u : 60u);
    const uint16_t animation =
        static_cast<uint16_t>(client_variant ? 3u : 2u);
    frame.deltas[0].newnum = kFirstEntity;
    frame.deltas[0].entity_delta =
        entity_delta(model, animation);
    frame.deltas[1] = {};
    frame.delta_count = 2;
    frame.legacy_entities[0].modelindex = model;
    frame.legacy_entities[0].frame = animation;
    return frame;
}

sv_snapshot_shadow_config_v1 snapshot_shadow_config(
    uint32_t snapshot_epoch, uint32_t entity_index_limit)
{
    sv_snapshot_shadow_config_v1 config{};
    config.struct_size = sizeof(config);
    config.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    config.snapshot_epoch = snapshot_epoch;
    config.max_entities = entity_index_limit;
    config.max_models = kMaxModels;
    config.max_sounds = kMaxSounds;
    config.slot_capacity = kShadowSlots;
    config.entities_per_slot =
        kProjectionEntityCapacity - 1u;
    config.area_bytes_per_slot = kAreaBytesPerSlot;
    config.beam_renderfx_mask = RF_BEAM;
    config.legacy_renderfx_allowed_mask = RF_SHELL_LITE_GREEN - 1u;
    config.legacy_beam_clear_mask = RF_GLOW;
    config.extended_entity_state = 1;
    return config;
}

sv_snapshot_shadow_ref_v1 commit_server_frame(
    sv_snapshot_shadow_peer_v1 *shadow, frame_carrier_t &frame,
    uint64_t server_time_us)
{
    CHECK(shadow != nullptr);
    frame.wire.areabits = frame.area.data();
    frame.wire.areabits_len =
        static_cast<uint32_t>(frame.area.size());

    sv_snapshot_shadow_frame_v1 input{};
    input.struct_size = sizeof(input);
    input.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    input.wire_frame = &frame.wire;
    input.authoritative_server_tick =
        static_cast<uint32_t>(frame.wire.serverframe);
    input.authoritative_tick_delta =
        frame.wire.deltaframe > 0 ? 1u : 0u;
    input.authoritative_server_time_us = server_time_us;
    input.controlled_entity_index = 1;
    input.canonical_movement_type = 0;
    input.canonical_movement_flags = 0;
    input.team_id = 0;
    input.consumed_command = frame.consumed_command;
    CHECK(SV_SnapshotShadowBeginFrameV1(shadow, &input) ==
          SV_SNAPSHOT_SHADOW_OK);
    for (uint32_t index = 0; index < frame.delta_count; ++index) {
        CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(
                  shadow, &frame.deltas[index]) ==
              SV_SNAPSHOT_SHADOW_OK);
    }
    sv_snapshot_shadow_ref_v1 ref{
        SV_SNAPSHOT_SHADOW_NO_SLOT, 0};
    CHECK(SV_SnapshotShadowCommitFrameV1(shadow, &ref) ==
          SV_SNAPSHOT_SHADOW_OK);
    return ref;
}

server_projection_t server_projection(
    sv_snapshot_shadow_peer_v1 *shadow,
    sv_snapshot_shadow_ref_v1 ref,
    uint32_t entity_index_limit)
{
    server_projection_t projection{};
    CHECK(SV_SnapshotShadowViewV1(
              shadow, ref, &projection.view,
              &projection.hashes) == SV_SNAPSHOT_SHADOW_OK);
    CHECK(projection.view.snapshot != nullptr);
    worr_snapshot_projection_hashes_v2 recomputed{};
    CHECK(Worr_SnapshotProjectionHashesV2(
        &projection.view, entity_index_limit, &recomputed));
    CHECK(hashes_equal(projection.hashes, recomputed));
    CHECK(projection.view.entity_count <=
          WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES);
    CHECK(projection.view.entity_count == kEntityCount);
    CHECK(projection.view.entities[kEntityCount - 1u]
              .generation.identity.index ==
          entity_index_limit - 1u);
    return projection;
}

void install_legacy_frame(const frame_carrier_t &frame)
{
    cl.frame = {};
    cl.frame.valid = true;
    cl.frame.number = frame.wire.serverframe;
    cl.frame.delta = frame.wire.deltaframe;
    cl.frame.areabytes = static_cast<int>(frame.area.size());
    std::memcpy(cl.frame.areabits, frame.area.data(),
                frame.area.size());
    cl.frame.ps = frame.legacy_player;
    cl.frame.clientNum = 0;
    cl.frame.consumed_command = frame.consumed_command;
    cl.frame.numEntities = static_cast<int>(kEntityCount);
    cl.frame.firstEntity = 0;
    for (uint32_t index = 0; index < kEntityCount; ++index)
        cl.entityStates[index] = frame.legacy_entities[index];
}

worr_native_snapshot_expectation_v1 publish_client_expectation(
    frame_carrier_t &frame, uint64_t server_time_us)
{
    frame.wire.areabits = frame.area.data();
    frame.wire.areabits_len =
        static_cast<uint32_t>(frame.area.size());
    CL_SnapshotShadowBeginFrame(&frame.wire);
    CHECK(CL_SnapshotShadowSetConsumedCommand(
        &frame.consumed_command));
    for (uint32_t index = 0; index < frame.delta_count; ++index)
        CL_SnapshotShadowCaptureEntityDelta(&frame.deltas[index]);
    install_legacy_frame(frame);
    CHECK(CL_SnapshotShadowAcceptFrameEx(
        server_time_us, 1, 0, 0, 0,
        CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY));

    cl_snapshot_shadow_status_v1 status{};
    CHECK(CL_SnapshotShadowGetStatus(&status));
    CHECK(status.last_parity_mismatch == 0);
    CHECK(status.last_accept_flags ==
          CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY);

    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    worr_snapshot_ref_v2 ref{};
    CHECK(CL_SnapshotShadowLatest(&view, &hashes, &ref));
    CHECK(view.snapshot != nullptr);
    worr_native_snapshot_expectation_v1 expectation{};
    CHECK(CL_SnapshotShadowGetNativeExpectation(
              view.snapshot->snapshot_id, &expectation) ==
          CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE);
    CHECK(hashes_equal(expectation.hashes, hashes));
    return expectation;
}

const worr_cgame_snapshot_timeline_export_v2 *cgame_timeline()
{
    const auto *api = CG_GetCanonicalSnapshotTimelineAPI();
    CHECK(api != nullptr);
    CHECK(api->struct_size == sizeof(*api));
    CHECK(api->api_version ==
          WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION);
    return api;
}

worr_cgame_snapshot_timeline_status_v2 cgame_status()
{
    worr_cgame_snapshot_timeline_status_v2 status{};
    CHECK(cgame_timeline()->GetStatus(&status));
    CHECK(status.struct_size == sizeof(status));
    CHECK(status.api_version ==
          WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION);
    return status;
}

void require_prediction_authority_ready(
    const server_projection_t &projection)
{
    CHECK(projection.view.snapshot != nullptr);
    CHECK(projection.view.player != nullptr);
    const auto &expected_snapshot = *projection.view.snapshot;
    const auto &expected_player = *projection.view.player;

    cg_prediction_authority_candidate_v1 candidate{};
    CHECK(CG_CanonicalSnapshotTimelineCopyPredictionSnapshot(
              expected_snapshot.snapshot_id.sequence,
              &candidate.timeline) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    candidate.input.struct_size = sizeof(candidate.input);
    candidate.input.api_version =
        WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    candidate.input.result = WORR_CGAME_PREDICTION_INPUT_OK;
    candidate.input.source =
        WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    candidate.input.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    candidate.input.consumed_command =
        expected_snapshot.consumed_command;
    candidate.input.authoritative_legacy_sequence =
        expected_snapshot.consumed_command.cursor.contiguous_sequence;
    candidate.input.current_legacy_sequence =
        candidate.input.authoritative_legacy_sequence;

    cg_prediction_authority_expectation_v1 expectation{};
    expectation.snapshot_sequence =
        expected_snapshot.snapshot_id.sequence;
    expectation.server_tick = expected_snapshot.server_tick;
    expectation.server_time_us = expected_snapshot.server_time_us;
    expectation.controlled_entity_index =
        expected_snapshot.controlled_entity.identity.index;

    cg_prediction_authority_v1 authority{};
    CHECK(CG_PredictionAuthoritySelectV1(
              &expectation, &candidate, &authority) ==
          cg_prediction_authority_result_v1::canonical);
    CHECK(authority.result ==
          cg_prediction_authority_result_v1::canonical);
    CHECK(snapshot_id_equal(
        authority.timeline.snapshot.snapshot_id,
        expected_snapshot.snapshot_id));
    CHECK(authority.timeline.snapshot.server_tick ==
          expected_snapshot.server_tick);
    CHECK(authority.timeline.snapshot.server_time_us ==
          expected_snapshot.server_time_us);
    CHECK(consumed_command_equal(
        authority.timeline.snapshot.consumed_command,
        expected_snapshot.consumed_command));
    CHECK(authority.timeline.snapshot.player_hash ==
          expected_snapshot.player_hash);
    CHECK(authority.timeline.snapshot.entity_hash ==
          expected_snapshot.entity_hash);
    CHECK(authority.timeline.snapshot.area_hash ==
          expected_snapshot.area_hash);
    CHECK(authority.timeline.snapshot.event_hash ==
          expected_snapshot.event_hash);
    CHECK(authority.timeline.snapshot.snapshot_hash ==
          expected_snapshot.snapshot_hash);
    CHECK(std::memcmp(
              &authority.timeline.player, &expected_player,
              sizeof(expected_player)) == 0);
    CHECK(consumed_command_equal(
        authority.input.consumed_command,
        expected_snapshot.consumed_command));
    CHECK(authority.input.command_count == 0);
    ++metrics.prediction_authorities;
}

cl_native_readiness_pilot_test_state_t client_state()
{
    cl_native_readiness_pilot_test_state_t state{};
    CHECK(CL_NativeReadinessPilotGetTestState(&state));
    return state;
}

sv_native_shadow_snapshot_status_v1 server_status(
    fixture_t &fixture, uint32_t now)
{
    sv_native_shadow_snapshot_status_v1 status{};
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &fixture.server, now, &status));
    CHECK(status.struct_size == sizeof(status));
    CHECK(status.schema_version ==
          SV_NATIVE_SHADOW_SNAPSHOT_STATUS_VERSION);
    return status;
}

void feed_record_to_client(
    const worr_native_readiness_record_v1 &record)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT>
        pairs{};
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs.data(),
        static_cast<uint32_t>(pairs.size())));
    CL_NativeReadinessPilotPacketBegin();
    for (const auto &pair : pairs) {
        CHECK(CL_NativeReadinessPilotObserveSetting(
            static_cast<int32_t>(pair.index),
            static_cast<int32_t>(pair.value)));
    }
    CL_NativeReadinessPilotPacketEnd();
}

worr_native_readiness_record_v1 decode_client_record(
    size_t offset)
{
    constexpr size_t kClcSettingWireBytes = 5u;
    constexpr size_t kRecordWireBytes =
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT *
        kClcSettingWireBytes;
    CHECK(cls.netchan.message.cursize >=
          offset + kRecordWireBytes);

    worr_native_readiness_sideband_parser_v1 parser{};
    CHECK(Worr_NativeReadinessSidebandParserInitV1(&parser));
    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED);
    const byte *wire = cls.netchan.message.data + offset;
    for (uint32_t index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        const byte *field = wire + index * kClcSettingWireBytes;
        CHECK(field[0] == static_cast<byte>(clc_setting));
        const auto setting_index = static_cast<int16_t>(
            static_cast<uint16_t>(field[1]) |
            (static_cast<uint16_t>(field[2]) << 8));
        const auto setting_value = static_cast<int16_t>(
            static_cast<uint16_t>(field[3]) |
            (static_cast<uint16_t>(field[4]) << 8));
        const auto result =
            Worr_NativeReadinessSidebandObservePairV1(
                &parser, setting_index, setting_value);
        CHECK(result ==
                  WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED ||
              result ==
                  WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED);
    }
    worr_native_readiness_record_v1 record{};
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(
              &parser, &record) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
    return record;
}

sv_native_shadow_observe_result_v1 feed_record_to_server(
    fixture_t &fixture,
    const worr_native_readiness_record_v1 &record,
    uint32_t now,
    worr_native_readiness_record_v1 *server_active_out = nullptr)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT>
        pairs{};
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs.data(),
        static_cast<uint32_t>(pairs.size())));
    CHECK(SV_NativeShadowPacketBeginV1(
        &fixture.server, now));
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

worr_native_readiness_record_v1 activate_snapshot_epoch_client(
    fixture_t &fixture, uint32_t now)
{
    com_localTime = now;
    confirm_public_capability(fixture.official_epoch);

    worr_native_readiness_record_v1 challenge{};
    CHECK(SV_NativeShadowBeginEpochBoundV1(
        &fixture.server, fixture.official_epoch,
        kPublicCapabilities, kPublicCapabilities,
        fixture.snapshot_epoch, now, &challenge));
    CHECK(challenge.record_kind ==
          WORR_NATIVE_READINESS_RECORD_CHALLENGE);
    CHECK(challenge.negotiated_capabilities ==
          kPrivateSnapshotCapabilities);
    CHECK(challenge.snapshot_epoch == fixture.snapshot_epoch);
    CHECK(challenge.snapshot_epoch != fixture.official_epoch);
    fixture.transport_epoch = challenge.transport_epoch;
    CHECK(fixture.transport_epoch != 0 &&
          fixture.transport_epoch != fixture.snapshot_epoch);

    const size_t ready_offset = cls.netchan.message.cursize;
    feed_record_to_client(challenge);
    const auto client_ready =
        decode_client_record(ready_offset);
    CHECK(client_ready.record_kind ==
          WORR_NATIVE_READINESS_RECORD_CLIENT_READY);
    CHECK(client_ready.snapshot_epoch ==
          fixture.snapshot_epoch);

    worr_native_readiness_record_v1 server_active{};
    CHECK(feed_record_to_server(
              fixture, client_ready, now + 1u,
              &server_active) ==
          SV_NATIVE_SHADOW_OBSERVE_SERVER_ACTIVE_READY);
    CHECK(server_active.record_kind ==
          WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE);
    CHECK(server_active.snapshot_epoch ==
          fixture.snapshot_epoch);
    CHECK(SV_NativeShadowServerActiveQueuedV1(
        &fixture.server));

    const size_t confirm_offset =
        cls.netchan.message.cursize;
    com_localTime = now + 1u;
    feed_record_to_client(server_active);
    const auto active_confirm =
        decode_client_record(confirm_offset);
    CHECK(active_confirm.record_kind ==
          WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM);
    CHECK(active_confirm.snapshot_epoch ==
          fixture.snapshot_epoch);

    const auto state = client_state();
    CHECK(state.snapshot_enabled);
    CHECK(state.snapshot_epoch == fixture.snapshot_epoch);
    CHECK(state.private_capabilities ==
          kPrivateSnapshotCapabilities);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_INITIALIZED) != 0);
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    const auto status = server_status(fixture, now + 2u);
    CHECK(status.mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT);
    CHECK(status.sender_initialized == 1);
    CHECK(status.tx_open == 0);
    CHECK(status.snapshot_epoch == fixture.snapshot_epoch);
    return active_confirm;
}

void complete_snapshot_epoch_server(
    fixture_t &fixture,
    const worr_native_readiness_record_v1 &active_confirm,
    uint32_t now)
{
    CHECK(feed_record_to_server(
              fixture, active_confirm, now) ==
          SV_NATIVE_SHADOW_OBSERVE_CLIENT_ACTIVE_CONFIRMED);
    const auto status = server_status(fixture, now);
    CHECK(status.mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT);
    CHECK(status.sender_initialized == 1);
    CHECK(status.tx_open == 1);
    CHECK(status.snapshot_epoch == fixture.snapshot_epoch);
}

void activate_snapshot_epoch(fixture_t &fixture, uint32_t now)
{
    const auto active_confirm =
        activate_snapshot_epoch_client(fixture, now);
    complete_snapshot_epoch_server(
        fixture, active_confirm, now + 2u);
}

void cleanup_fixture(fixture_t &fixture)
{
    if (fixture.server_live) {
        SV_NativeShadowPeerDestroyV1(&fixture.server);
        fixture.server_live = false;
    }
    if (fixture.server_snapshot_shadow) {
        SV_SnapshotShadowDestroyV1(
            fixture.server_snapshot_shadow);
        fixture.server_snapshot_shadow = nullptr;
    }
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    CL_SnapshotShadowShutdown();
    CHECK(CL_SnapshotShadowSetConsumer(nullptr));
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
}

void reset_fixture(fixture_t &fixture, uint32_t now,
                   uint32_t official_epoch,
                   uint32_t snapshot_epoch,
                   uint32_t entity_index_limit =
                       kRereleaseEntityIndexLimit,
                   int server_protocol =
                       PROTOCOL_VERSION_RERELEASE,
                   bool defer_active_confirm = false)
{
    cleanup_fixture(fixture);
    fixture = {};
    std::memset(&cls, 0, sizeof(cls));
    std::memset(&cl, 0, sizeof(cl));
    std::memset(&native_shadow_cvar, 0,
                sizeof(native_shadow_cvar));
    std::memset(&event_shadow_cvar, 0,
                sizeof(event_shadow_cvar));
    std::memset(&snapshot_shadow_mode_cvar, 0,
                sizeof(snapshot_shadow_mode_cvar));
    std::memset(&probe_hold_cvar, 0,
                sizeof(probe_hold_cvar));
    std::memset(&projection_shadow_cvar, 0,
                sizeof(projection_shadow_cvar));
    std::memset(&projection_debug_cvar, 0,
                sizeof(projection_debug_cvar));
    reliable_storage.fill(0);
    com_localTime = now;
    fixture.official_epoch = official_epoch;
    fixture.snapshot_epoch = snapshot_epoch;
    fixture.entity_index_limit = entity_index_limit;
    fixture.server_protocol = server_protocol;

    native_shadow_cvar.integer = 1;
    snapshot_shadow_mode_cvar.integer = 1;
    projection_shadow_cvar.integer = 1;
    cls.netchan.type = NETCHAN_NEW;
    cls.netchan.maxpacketlen = kApplicationCeiling;
    cls.serverProtocol = server_protocol;
    cls.realtime = now;
    cl.csr.max_edicts =
        static_cast<uint16_t>(entity_index_limit);
    SZ_InitWrite(&cls.netchan.message, reliable_storage.data(),
                 reliable_storage.size());
    fixture.server_channel.type = NETCHAN_NEW;
    fixture.server_channel.maxpacketlen = kApplicationCeiling;

    CG_EventRuntimeSetAuditEnabled(false);
    CHECK(!CG_EventRuntimeAuditEnabled());
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CHECK(CL_CGameEventRuntimeResetConnection() ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(CL_SnapshotShadowSetConsumer(cgame_timeline()));
    CL_SnapshotShadowBeginConnection(
        entity_index_limit, kMaxModels, kMaxSounds,
        server_protocol == PROTOCOL_VERSION_RERELEASE);
    cl_snapshot_shadow_status_v1 shadow_status{};
    CHECK(CL_SnapshotShadowGetStatus(&shadow_status));
    CHECK(shadow_status.active == 1);
    CHECK(shadow_status.consumer_attached == 1);

    CL_NativeReadinessPilotRegisterCvar();
    CHECK(CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(SV_NativeShadowPeerInitModeV1(
        &fixture.server, &fixture.server_channel, now,
        SV_NATIVE_SHADOW_MODE_SNAPSHOT));
    fixture.server_live = true;

    const auto config =
        snapshot_shadow_config(
            snapshot_epoch, entity_index_limit);
    fixture.server_snapshot_shadow =
        SV_SnapshotShadowCreateV1(&config);
    CHECK(fixture.server_snapshot_shadow != nullptr);
    if (defer_active_confirm) {
        fixture.deferred_active_confirm =
            activate_snapshot_epoch_client(fixture, now + 1u);
        fixture.active_confirm_deferred = true;
    } else {
        activate_snapshot_epoch(fixture, now + 1u);
    }
}

wire_packet_t prepare_packet(netchan_t &channel, uint32_t now)
{
    com_localTime = now;
    cls.realtime = now;
    wire_packet_t packet{};
    netchan_app_tx_prepare_info_v1_t info{};
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.outgoing_sequence = channel.outgoing_sequence++;
    info.max_application_bytes = kApplicationCeiling;
    info.packet_copies = 1;
    packet.completion.abi_version =
        NETCHAN_APP_TX_HOOK_ABI_V1;
    packet.completion.struct_size =
        sizeof(packet.completion);
    CHECK(channel.app_tx_prepare != nullptr);
    CHECK(channel.app_tx_prepare(
              channel.app_tx_opaque, &info, nullptr,
              packet.bytes.data(), &packet.completion) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    packet.count = packet.completion.application_bytes;
    CHECK(packet.count != 0 &&
          packet.count <= packet.bytes.size());
    return packet;
}

void accept_packet(netchan_t &channel,
                   const wire_packet_t &packet)
{
    netchan_app_tx_completion_info_v1_t info{};
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.result = NETCHAN_APP_TX_COMPLETION_ACCEPTED;
    info.packet_copies = 1;
    info.accepted_copies = 1;
    info.application_bytes =
        static_cast<uint32_t>(packet.count);
    info.token = packet.completion.token;
    CHECK(channel.app_tx_completion != nullptr);
    channel.app_tx_completion(
        channel.app_tx_opaque, &info, packet.bytes.data());
}

netchan_app_rx_result_t deliver_to_client(
    fixture_t &fixture, const wire_packet_t &packet,
    uint32_t now)
{
    com_localTime = now;
    cls.realtime = now;
    netchan_app_rx_info_v1_t info{};
    info.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.incoming_sequence = fixture.client_rx_sequence++;
    info.message_bytes = static_cast<uint32_t>(packet.count);
    info.application_bytes =
        static_cast<uint32_t>(packet.count);
    netchan_app_rx_output_v1_t output{};
    output.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output.struct_size = sizeof(output);
    CHECK(cls.netchan.app_rx != nullptr);
    const auto result = cls.netchan.app_rx(
        cls.netchan.app_rx_opaque, &info, packet.bytes.data(),
        &output);
    if (result == NETCHAN_APP_RX_EXPOSE_LEGACY)
        CHECK(output.legacy_bytes == 0);
    return result;
}

netchan_app_rx_result_t deliver_to_server(
    fixture_t &fixture, const wire_packet_t &packet,
    uint32_t now)
{
    CHECK(SV_NativeShadowAdvanceAdmissionClockV1(
        &fixture.server, now));
    netchan_app_rx_info_v1_t info{};
    info.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.incoming_sequence = fixture.server_rx_sequence++;
    info.message_bytes = static_cast<uint32_t>(packet.count);
    info.application_bytes =
        static_cast<uint32_t>(packet.count);
    netchan_app_rx_output_v1_t output{};
    output.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output.struct_size = sizeof(output);
    CHECK(fixture.server_channel.app_rx != nullptr);
    const auto result = fixture.server_channel.app_rx(
        fixture.server_channel.app_rx_opaque, &info,
        packet.bytes.data(), &output);
    if (result == NETCHAN_APP_RX_EXPOSE_LEGACY)
        CHECK(output.legacy_bytes == 0);
    return result;
}

worr_native_carrier_view_v1 carrier_view(
    const wire_packet_t &packet)
{
    worr_native_carrier_view_v1 view{};
    CHECK(Worr_NativeCarrierDecodeV1(
              packet.bytes.data(), packet.count, &view) ==
          WORR_NATIVE_CARRIER_OK);
    return view;
}

worr_native_envelope_frame_info_v1 snapshot_frame(
    const wire_packet_t &packet)
{
    const auto view = carrier_view(packet);
    const worr_native_carrier_entry_v1 *data = nullptr;
    for (uint16_t index = 0; index < view.entry_count; ++index) {
        if (view.entries[index].entry_type !=
            WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
            continue;
        }
        CHECK(data == nullptr);
        data = &view.entries[index];
    }
    CHECK(data != nullptr);
    worr_native_envelope_frame_info_v1 frame{};
    CHECK(Worr_NativeEnvelopeDecodeV1(
              packet.bytes.data() + data->data_offset,
              data->data_bytes, &frame) ==
          WORR_NATIVE_ENVELOPE_DECODE_OK);
    CHECK(frame.record.record_class ==
          WORR_NATIVE_RECORD_SNAPSHOT_V1);
    return frame;
}

std::vector<wire_packet_t> collect_server_burst(
    fixture_t &fixture, uint32_t now,
    worr_snapshot_id_v2 expected_snapshot)
{
    std::vector<wire_packet_t> burst;
    uint16_t fragment_count = 0;
    uint32_t message_sequence = 0;
    for (uint32_t guard = 0; guard < 512; ++guard) {
        CHECK(SV_NativeShadowOutputDueV1(
            &fixture.server, now));
        auto packet =
            prepare_packet(fixture.server_channel, now);
        const auto view = carrier_view(packet);
        CHECK(view.transport_epoch == fixture.transport_epoch);
        uint16_t data_count = 0;
        for (uint16_t index = 0; index < view.entry_count;
             ++index) {
            if (view.entries[index].entry_type ==
                WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
                ++data_count;
            }
        }
        CHECK(data_count == 1);
        const auto frame = snapshot_frame(packet);
        CHECK(frame.record.object_epoch ==
              expected_snapshot.epoch);
        CHECK(frame.record.object_sequence ==
              expected_snapshot.sequence);
        if (burst.empty()) {
            fragment_count = frame.fragment_count;
            message_sequence = frame.message_sequence;
            CHECK(fragment_count > 1);
        } else {
            CHECK(frame.fragment_count == fragment_count);
            CHECK(frame.message_sequence == message_sequence);
        }
        CHECK(frame.fragment_index == burst.size());
        accept_packet(fixture.server_channel, packet);
        burst.push_back(packet);
        if (frame.fragment_index + 1u == fragment_count)
            break;
    }
    CHECK(!burst.empty());
    CHECK(burst.size() == fragment_count);
    ++metrics.fragmented_messages;
    return burst;
}

void deliver_incomplete_reordered_burst(
    fixture_t &fixture,
    const std::vector<wire_packet_t> &burst, uint32_t now)
{
    CHECK(burst.size() > 2);
    /* Fragment zero is lost after the real server completion callback
     * accepted the local handoff.  Every other fragment arrives in reverse. */
    ++metrics.server_to_client_losses;
    for (size_t index = burst.size(); index-- > 1;) {
        CHECK(deliver_to_client(
                  fixture, burst[index], now) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        ++metrics.reordered_deliveries;
    }
}

void deliver_retry_reordered_with_duplicate(
    fixture_t &fixture,
    const std::vector<wire_packet_t> &burst, uint32_t now)
{
    CHECK(burst.size() > 1);
    CHECK(deliver_to_client(
              fixture, burst.back(), now) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    ++metrics.duplicate_deliveries;
    for (size_t index = burst.size(); index-- > 0;) {
        CHECK(deliver_to_client(
                  fixture, burst[index], now) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        ++metrics.reordered_deliveries;
    }
}

void check_ack_only(const wire_packet_t &packet,
                    uint32_t transport_epoch,
                    uint32_t expected_message_sequence)
{
    const auto view = carrier_view(packet);
    CHECK(view.transport_epoch == transport_epoch);
    CHECK(view.entry_count == 1);
    CHECK(view.entries[0].entry_type ==
          WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
    CHECK(view.entries[0].first_message_sequence ==
          expected_message_sequence);
    CHECK(view.entries[0].last_message_sequence ==
          expected_message_sequence);
}

wire_packet_t craft_projection_packet(
    const server_projection_t &projection,
    uint32_t entity_index_limit, uint32_t transport_epoch,
    uint32_t message_sequence)
{
    uint32_t preflight_bytes = 0;
    CHECK(Worr_NativeCodecSnapshotPreflightV1(
              &projection.view, entity_index_limit,
              &preflight_bytes) == WORR_NATIVE_CODEC_OK);
    std::vector<uint8_t> encoded(preflight_bytes);
    size_t encoded_bytes = 0;
    CHECK(Worr_NativeCodecSnapshotEncodeV1(
              &projection.view, entity_index_limit, encoded.data(),
              encoded.size(), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes == encoded.size());
    worr_native_codec_info_v1 info{};
    worr_native_record_ref_v1 record{};
    CHECK(Worr_NativeCodecInspectV1(
              encoded.data(), encoded.size(), &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &record));

    worr_native_envelope_fragmenter_v1 fragmenter{};
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, transport_epoch, message_sequence, record, 2,
        encoded.data(), static_cast<uint32_t>(encoded.size()),
        SV_NATIVE_SHADOW_SNAPSHOT_MAX_DATAGRAM_BYTES));
    std::array<byte, WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES>
        datagram{};
    size_t datagram_bytes = 0;
    CHECK(Worr_NativeEnvelopeFragmentNextV1(
              &fragmenter, encoded.data(),
              static_cast<uint32_t>(encoded.size()),
              datagram.data(), datagram.size(),
              &datagram_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_OK);

    wire_packet_t packet{};
    worr_native_carrier_entry_v1 entry{};
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entry.data_bytes = static_cast<uint32_t>(datagram_bytes);
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, nullptr, 0, datagram.data(),
              datagram_bytes, &entry, 1, packet.bytes.data(),
              packet.bytes.size(), &packet.count) ==
          WORR_NATIVE_CARRIER_OK);
    return packet;
}

void test_production_snapshot_loss_reorder_ack_loss_and_mismatch()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 401;
    constexpr uint32_t kSnapshotEpoch = 7001;
    constexpr uint32_t kQueueTime = 1010;
    reset_fixture(
        fixture, 1000, kOfficialEpoch, kSnapshotEpoch);
    CHECK(fixture.entity_index_limit ==
          kRereleaseEntityIndexLimit);
    CHECK(cl.csr.max_edicts ==
          kRereleaseEntityIndexLimit);
    ++metrics.real_domain_activations;

    auto keyframe = make_keyframe(
        fixture.entity_index_limit);
    const auto keyframe_ref = commit_server_frame(
        fixture.server_snapshot_shadow, keyframe,
        kKeyframeTimeUs);
    const auto keyframe_projection = server_projection(
        fixture.server_snapshot_shadow, keyframe_ref,
        fixture.entity_index_limit);
    CHECK(keyframe_projection.view.snapshot->snapshot_id.epoch ==
          kSnapshotEpoch);
    CHECK(keyframe_projection.view.snapshot->snapshot_id.sequence ==
          kKeyframeNumber + 1u);
    CHECK(keyframe_projection.view.entity_count == kEntityCount);
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        keyframe_ref, kQueueTime));

    const auto first_burst = collect_server_burst(
        fixture, kQueueTime,
        keyframe_projection.view.snapshot->snapshot_id);
    deliver_incomplete_reordered_burst(
        fixture, first_burst, kQueueTime);
    auto state = client_state();
    CHECK(state.snapshot_rx_occupied == 1);
    CHECK(state.snapshot_ack_receipts == 0);
    const auto cgame_before = cgame_status();

    const uint32_t retry_time =
        kQueueTime + SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS;
    const auto retry_burst = collect_server_burst(
        fixture, retry_time,
        keyframe_projection.view.snapshot->snapshot_id);
    deliver_retry_reordered_with_duplicate(
        fixture, retry_burst, retry_time);
    state = client_state();
    CHECK(state.snapshot_rx_occupied == 1);
    CHECK(state.snapshot_ack_receipts == 0);
    CHECK(cgame_status().accepted == cgame_before.accepted);
    CHECK(!CL_NativeReadinessPilotOutputDue());

    const auto client_expectation = publish_client_expectation(
        keyframe, kKeyframeTimeUs);
    CHECK(snapshot_id_equal(
        client_expectation.snapshot_id,
        keyframe_projection.view.snapshot->snapshot_id));
    CHECK(expectation_parity_equal(
        client_expectation.hashes,
        keyframe_projection.hashes));
    CL_NativeReadinessPilotSnapshotExpectationReady();

    state = client_state();
    CHECK(state.mode == 2);
    CHECK(state.snapshot_rx_occupied == 0);
    CHECK(state.snapshot_ack_receipts == 1);
    const auto cgame_after = cgame_status();
    CHECK(cgame_after.accepted == cgame_before.accepted + 1u);
    CHECK(cgame_after.consume_attempts ==
          cgame_before.consume_attempts + 1u);
    CHECK(snapshot_id_equal(
        cgame_after.last_snapshot_id,
        keyframe_projection.view.snapshot->snapshot_id));
    CHECK(cgame_after.last_endpoint_hash ==
          keyframe_projection.hashes.endpoint_hash);
    ++metrics.exact_cgame_consumes;
    require_prediction_authority_ready(keyframe_projection);

    /* The first exact reverse receipt is accepted locally and lost on the
     * virtual link. */
    com_localTime = retry_time + 1u;
    CHECK(CL_NativeReadinessPilotOutputDue());
    auto lost_ack =
        prepare_packet(cls.netchan, retry_time + 1u);
    const uint32_t message_sequence =
        snapshot_frame(first_burst.front()).message_sequence;
    check_ack_only(
        lost_ack, fixture.transport_epoch, message_sequence);
    accept_packet(cls.netchan, lost_ack);
    ++metrics.lost_acknowledgements;
    CHECK(server_status(fixture, retry_time + 1u)
              .retained_count == 1);

    /* A complete third real server retry is handed to the transport.  Only
     * its first fragment reaches the client: that exact committed repeat
     * revalidates the live cgame receipt and rearms the same ACK without a
     * second timeline consume. */
    const uint32_t repeat_time =
        retry_time + SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS;
    const auto repeat_burst = collect_server_burst(
        fixture, repeat_time,
        keyframe_projection.view.snapshot->snapshot_id);
    const auto before_repeat = cgame_status();
    CHECK(deliver_to_client(
              fixture, repeat_burst.front(), repeat_time) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    const auto after_repeat = cgame_status();
    CHECK(after_repeat.accepted == before_repeat.accepted);
    CHECK(after_repeat.consume_attempts ==
          before_repeat.consume_attempts);
    CHECK(client_state().snapshot_ack_receipts == 1);
    ++metrics.repeat_revalidations;

    com_localTime = repeat_time + 1u;
    CHECK(CL_NativeReadinessPilotOutputDue());
    auto final_ack =
        prepare_packet(cls.netchan, repeat_time + 1u);
    check_ack_only(
        final_ack, fixture.transport_epoch, message_sequence);
    accept_packet(cls.netchan, final_ack);
    CHECK(deliver_to_server(
              fixture, final_ack, repeat_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    const auto released =
        server_status(fixture, repeat_time + 1u);
    CHECK(released.retained_count == 0);
    CHECK(released.active_payload_bytes == 0);
    CHECK(released.acknowledgements_applied == 1);
    CHECK(released.payloads_released == 1);
    const uint32_t prior_ack_receipts =
        client_state().snapshot_ack_receipts;
    CHECK(prior_ack_receipts == 1);

    /* The next server final view and the independently accepted legacy view
     * intentionally disagree while retaining the same exact snapshot ID.
     * No consumer call or ACK is authorized; the native receiver quarantines
     * and ownership remains latched through DRAIN. */
    auto server_delta = make_delta_frame(keyframe, false);
    auto client_delta = make_delta_frame(keyframe, true);
    constexpr uint64_t kDeltaTimeUs =
        kKeyframeTimeUs + UINT64_C(16000);
    const auto delta_ref = commit_server_frame(
        fixture.server_snapshot_shadow, server_delta,
        kDeltaTimeUs);
    const auto delta_projection = server_projection(
        fixture.server_snapshot_shadow, delta_ref,
        fixture.entity_index_limit);
    const uint32_t delta_queue_time = repeat_time + 2u;
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        delta_ref, delta_queue_time));
    const auto delta_burst = collect_server_burst(
        fixture, delta_queue_time,
        delta_projection.view.snapshot->snapshot_id);
    for (size_t index = delta_burst.size(); index-- > 0;) {
        CHECK(deliver_to_client(
                  fixture, delta_burst[index],
                  delta_queue_time) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }
    CHECK(client_state().snapshot_rx_occupied == 1);
    CHECK(client_state().snapshot_ack_receipts ==
          prior_ack_receipts);

    const auto divergent_expectation =
        publish_client_expectation(client_delta, kDeltaTimeUs);
    CHECK(snapshot_id_equal(
        divergent_expectation.snapshot_id,
        delta_projection.view.snapshot->snapshot_id));
    CHECK(!expectation_parity_equal(
        divergent_expectation.hashes,
        delta_projection.hashes));
    const auto before_mismatch = cgame_status();
    CL_NativeReadinessPilotSnapshotExpectationReady();
    state = client_state();
    CHECK(state.mode == 3);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) != 0);
    CHECK(state.snapshot_ack_receipts == prior_ack_receipts);
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(!CL_NativeReadinessPilotOutputDue());
    const auto after_mismatch = cgame_status();
    CHECK(after_mismatch.accepted == before_mismatch.accepted);
    CHECK(after_mismatch.consume_attempts ==
          before_mismatch.consume_attempts);
    CHECK(server_status(fixture, delta_queue_time)
              .retained_count == 1);
    ++metrics.hash_quarantines;

    cleanup_fixture(fixture);
}

void test_production_expectation_window_with_delayed_confirm()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 403;
    constexpr uint32_t kSnapshotEpoch = 7201;
    constexpr uint32_t kStartTime = 4000;
    constexpr uint32_t kExpectationTotal =
        WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY + 8u;
    constexpr uint64_t kStartServerTimeUs = UINT64_C(320000);
    sv_snapshot_shadow_ref_v1 latest_ref{
        SV_SNAPSHOT_SHADOW_NO_SLOT, 0};
    worr_native_snapshot_expectation_v1 latest_expectation{};
    server_projection_t latest_projection{};

    reset_fixture(
        fixture, kStartTime, kOfficialEpoch, kSnapshotEpoch,
        kRereleaseEntityIndexLimit, PROTOCOL_VERSION_RERELEASE,
        true);
    CHECK(fixture.active_confirm_deferred);
    CHECK(server_status(fixture, kStartTime + 3u).tx_open == 0);
    const auto cgame_before = cgame_status();

    for (uint32_t index = 0; index < kExpectationTotal; ++index) {
        const uint32_t frame_number = kKeyframeNumber + index;
        const uint64_t server_time_us =
            kStartServerTimeUs +
            static_cast<uint64_t>(index) * UINT64_C(16000);
        auto frame = make_keyframe(
            fixture.entity_index_limit, frame_number);
        latest_ref = commit_server_frame(
            fixture.server_snapshot_shadow, frame,
            server_time_us);
        const auto projection = server_projection(
            fixture.server_snapshot_shadow, latest_ref,
            fixture.entity_index_limit);
        latest_projection = projection;
        latest_expectation =
            publish_client_expectation(frame, server_time_us);
        CHECK(snapshot_id_equal(
            latest_expectation.snapshot_id,
            projection.view.snapshot->snapshot_id));
        CHECK(expectation_parity_equal(
            latest_expectation.hashes,
            projection.hashes));
        if (index != 0) {
            CHECK(latest_expectation.hashes.endpoint_hash !=
                  projection.hashes.endpoint_hash);
        }
        CL_NativeReadinessPilotSnapshotExpectationReady();

        const auto state = client_state();
        CHECK(state.mode == 2);
        CHECK((state.snapshot_receiver_flags &
               WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);
        CHECK(state.snapshot_rx_occupied == 0);
        CHECK(state.snapshot_ack_receipts == 0);
        CHECK(cgame_status().accepted == cgame_before.accepted);
    }
    ++metrics.expectation_window_rollovers;

    complete_snapshot_epoch_server(
        fixture, fixture.deferred_active_confirm,
        kStartTime + 4u);
    fixture.active_confirm_deferred = false;
    CHECK(server_status(fixture, kStartTime + 4u).tx_open == 1);
    const uint32_t queue_time = kStartTime + 5u;
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        latest_ref, queue_time));
    const auto burst = collect_server_burst(
        fixture, queue_time, latest_expectation.snapshot_id);
    for (size_t index = burst.size(); index-- > 0;) {
        CHECK(deliver_to_client(
                  fixture, burst[index], queue_time) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }

    const auto state = client_state();
    CHECK(state.mode == 2);
    CHECK(state.snapshot_rx_occupied == 0);
    CHECK(state.snapshot_ack_receipts == 1);
    const auto cgame_after = cgame_status();
    CHECK(cgame_after.accepted == cgame_before.accepted + 1u);
    CHECK(cgame_after.consume_attempts ==
          cgame_before.consume_attempts + 1u);
    CHECK(snapshot_id_equal(
        cgame_after.last_snapshot_id,
        latest_expectation.snapshot_id));
    ++metrics.exact_cgame_consumes;
    require_prediction_authority_ready(latest_projection);
    ++metrics.real_domain_activations;
    cleanup_fixture(fixture);
}

void test_production_complete_timeout_reuses_pending_slot()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 405;
    constexpr uint32_t kSnapshotEpoch = 7401;
    constexpr uint32_t kStartTime = 6000;
    constexpr uint32_t kAQueueTime = 6010;
    constexpr uint32_t kBQueueTime =
        kAQueueTime +
        WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_TIMEOUT_TICKS + 1u;
    constexpr uint64_t kAServerTimeUs = UINT64_C(640000);
    constexpr uint64_t kBServerTimeUs = UINT64_C(656000);

    reset_fixture(
        fixture, kStartTime, kOfficialEpoch, kSnapshotEpoch);
    const auto cgame_before = cgame_status();

    auto frame_a = make_keyframe(
        fixture.entity_index_limit, kKeyframeNumber);
    const auto ref_a = commit_server_frame(
        fixture.server_snapshot_shadow, frame_a,
        kAServerTimeUs);
    const auto projection_a = server_projection(
        fixture.server_snapshot_shadow, ref_a,
        fixture.entity_index_limit);
    worr_native_snapshot_expectation_v1 absent{};
    CHECK(CL_SnapshotShadowGetNativeExpectation(
              projection_a.view.snapshot->snapshot_id, &absent) ==
          CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_PENDING);
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        ref_a, kAQueueTime));
    const auto burst_a = collect_server_burst(
        fixture, kAQueueTime,
        projection_a.view.snapshot->snapshot_id);
    CHECK(burst_a.size() > 1);
    for (const auto &packet : burst_a) {
        CHECK(deliver_to_client(
                  fixture, packet, kAQueueTime) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }
    auto state = client_state();
    CHECK(state.mode == 2);
    CHECK(state.snapshot_rx_occupied == 1);
    CHECK(state.snapshot_ack_receipts == 0);
    CHECK(cgame_status().accepted == cgame_before.accepted);
    CHECK(cgame_status().consume_attempts ==
          cgame_before.consume_attempts);
    CHECK(!CL_NativeReadinessPilotOutputDue());

    auto frame_b = make_keyframe(
        fixture.entity_index_limit, kKeyframeNumber + 1u);
    const auto ref_b = commit_server_frame(
        fixture.server_snapshot_shadow, frame_b,
        kBServerTimeUs);
    const auto projection_b = server_projection(
        fixture.server_snapshot_shadow, ref_b,
        fixture.entity_index_limit);
    const auto expectation_b = publish_client_expectation(
        frame_b, kBServerTimeUs);
    CHECK(snapshot_id_equal(
        expectation_b.snapshot_id,
        projection_b.view.snapshot->snapshot_id));
    CHECK(expectation_parity_equal(
        expectation_b.hashes, projection_b.hashes));
    CL_NativeReadinessPilotSnapshotExpectationReady();
    CHECK(client_state().snapshot_rx_occupied == 1);

    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        ref_b, kBQueueTime));
    const auto burst_b = collect_server_burst(
        fixture, kBQueueTime, expectation_b.snapshot_id);
    CHECK(burst_b.size() > 1);
    CHECK(deliver_to_client(
              fixture, burst_b.front(), kBQueueTime) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    state = client_state();
    CHECK(state.mode == 2);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);
    CHECK(state.snapshot_rx_occupied == 1);
    CHECK(state.snapshot_ack_receipts == 0);
    CHECK(cgame_status().accepted == cgame_before.accepted);
    CHECK(cgame_status().consume_attempts ==
          cgame_before.consume_attempts);

    for (size_t index = 1; index < burst_b.size(); ++index) {
        CHECK(deliver_to_client(
                  fixture, burst_b[index], kBQueueTime) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }
    state = client_state();
    CHECK(state.mode == 2);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);
    CHECK(state.snapshot_rx_occupied == 0);
    CHECK(state.snapshot_ack_receipts == 1);
    const auto cgame_after = cgame_status();
    CHECK(cgame_after.accepted == cgame_before.accepted + 1u);
    CHECK(cgame_after.consume_attempts ==
          cgame_before.consume_attempts + 1u);
    CHECK(snapshot_id_equal(
        cgame_after.last_snapshot_id,
        expectation_b.snapshot_id));
    CHECK(cgame_after.last_endpoint_hash ==
          projection_b.hashes.endpoint_hash);

    const uint32_t message_sequence =
        snapshot_frame(burst_b.front()).message_sequence;
    com_localTime = kBQueueTime + 1u;
    CHECK(CL_NativeReadinessPilotOutputDue());
    const auto acknowledgement =
        prepare_packet(cls.netchan, kBQueueTime + 1u);
    check_ack_only(
        acknowledgement, fixture.transport_epoch,
        message_sequence);
    accept_packet(cls.netchan, acknowledgement);

    ++metrics.complete_timeout_recoveries;
    ++metrics.exact_cgame_consumes;
    require_prediction_authority_ready(projection_b);
    ++metrics.real_domain_activations;
    cleanup_fixture(fixture);
}

void test_legacy_entity_domain_activation_and_admission()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 404;
    constexpr uint32_t kSnapshotEpoch = 7301;
    constexpr uint32_t kStartTime = 5000;
    constexpr uint64_t kServerTimeUs = UINT64_C(480000);

    reset_fixture(
        fixture, kStartTime, kOfficialEpoch, kSnapshotEpoch,
        kLegacyEntityIndexLimit, PROTOCOL_VERSION_DEFAULT);
    CHECK(fixture.entity_index_limit ==
          kLegacyEntityIndexLimit);
    CHECK(cl.csr.max_edicts ==
          kLegacyEntityIndexLimit);

    auto frame = make_keyframe(fixture.entity_index_limit);
    const auto ref = commit_server_frame(
        fixture.server_snapshot_shadow, frame, kServerTimeUs);
    const auto projection = server_projection(
        fixture.server_snapshot_shadow, ref,
        fixture.entity_index_limit);
    const auto expectation =
        publish_client_expectation(frame, kServerTimeUs);
    CHECK(snapshot_id_equal(
        expectation.snapshot_id,
        projection.view.snapshot->snapshot_id));
    CHECK(expectation_parity_equal(
        expectation.hashes, projection.hashes));
    CL_NativeReadinessPilotSnapshotExpectationReady();

    const auto before = cgame_status();
    const uint32_t queue_time = kStartTime + 10u;
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        ref, queue_time));
    const auto burst = collect_server_burst(
        fixture, queue_time, expectation.snapshot_id);
    for (const auto &packet : burst) {
        CHECK(deliver_to_client(
                  fixture, packet, queue_time) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }
    const auto state = client_state();
    CHECK(state.mode == 2);
    CHECK(state.snapshot_rx_occupied == 0);
    CHECK(state.snapshot_ack_receipts == 1);
    const auto after = cgame_status();
    CHECK(after.accepted == before.accepted + 1u);
    CHECK(after.consume_attempts ==
          before.consume_attempts + 1u);
    ++metrics.exact_cgame_consumes;
    require_prediction_authority_ready(projection);
    ++metrics.real_domain_activations;
    cleanup_fixture(fixture);
}

void test_wrong_snapshot_epoch_fails_closed()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 402;
    constexpr uint32_t kSnapshotEpoch = 7101;
    constexpr uint32_t kWrongSnapshotEpoch = 7102;
    reset_fixture(
        fixture, 3000, kOfficialEpoch, kSnapshotEpoch);

    const auto wrong_config =
        snapshot_shadow_config(
            kWrongSnapshotEpoch, fixture.entity_index_limit);
    auto *wrong_shadow =
        SV_SnapshotShadowCreateV1(&wrong_config);
    CHECK(wrong_shadow != nullptr);
    auto wrong_frame = make_keyframe(
        fixture.entity_index_limit);
    const auto wrong_ref = commit_server_frame(
        wrong_shadow, wrong_frame, kKeyframeTimeUs);
    const auto wrong_projection =
        server_projection(
            wrong_shadow, wrong_ref,
            fixture.entity_index_limit);
    CHECK(wrong_projection.view.snapshot->snapshot_id.epoch ==
          kWrongSnapshotEpoch);

    const auto before = cgame_status();
    const auto wrong_packet = craft_projection_packet(
        wrong_projection, fixture.entity_index_limit,
        fixture.transport_epoch, 77);
    CHECK(deliver_to_client(
              fixture, wrong_packet, 3010) ==
          NETCHAN_APP_RX_REJECT);
    const auto state = client_state();
    CHECK(state.mode == 3);
    CHECK(state.snapshot_ack_receipts == 0);
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(!CL_NativeReadinessPilotOutputDue());
    const auto after = cgame_status();
    CHECK(after.accepted == before.accepted);
    CHECK(after.consume_attempts == before.consume_attempts);
    ++metrics.wrong_epoch_rejections;

    SV_SnapshotShadowDestroyV1(wrong_shadow);
    cleanup_fixture(fixture);
}

} // namespace

extern "C" cvar_t *Cvar_Get(
    const char *name, const char *, int)
{
    if (name &&
        std::strcmp(name, "cl_worr_native_event_shadow") == 0) {
        return &event_shadow_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_worr_native_snapshot_shadow") == 0) {
        return &snapshot_shadow_mode_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_worr_native_shadow_probe_hold") == 0) {
        return &probe_hold_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_snapshot_shadow_debug") == 0) {
        return &projection_debug_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_snapshot_shadow") == 0) {
        return &projection_shadow_cvar;
    }
    return &native_shadow_cvar;
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

extern "C" void SZ_InitWrite(
    sizebuf_t *buffer, void *data, size_t size)
{
    SZ_Init(
        buffer, data, size,
        "native snapshot production virtual link test");
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
    CHECK(buffer && buffer->data &&
          size <= buffer->maxsize);
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
    if (!io_argument || !message ||
        message->type != Q2P_CLC_SETTING) {
        return Q2P_ERR_BAD_COMMAND;
    }
    auto *io =
        reinterpret_cast<q2protoio_ioarg_t *>(io_argument);
    if (!io->sz_write)
        return Q2P_ERR_BAD_DATA;
    byte *wire =
        static_cast<byte *>(SZ_GetSpace(io->sz_write, 5));
    wire[0] = static_cast<byte>(clc_setting);
    const uint16_t index =
        static_cast<uint16_t>(message->setting.index);
    const uint16_t value =
        static_cast<uint16_t>(message->setting.value);
    wire[1] = static_cast<byte>(index);
    wire[2] = static_cast<byte>(index >> 8);
    wire[3] = static_cast<byte>(value);
    wire[4] = static_cast<byte>(value >> 8);
    return Q2P_ERR_SUCCESS;
}

extern "C" void *Z_Mallocz(size_t size)
{
    return std::calloc(1, size);
}

extern "C" void Z_Free(void *pointer)
{
    std::free(pointer);
}

extern "C" void Com_LPrintf(
    print_type_t, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
}

int main()
{
    test_production_snapshot_loss_reorder_ack_loss_and_mismatch();
    test_production_expectation_window_with_delayed_confirm();
    test_production_complete_timeout_reuses_pending_slot();
    test_legacy_entity_domain_activation_and_admission();
    test_wrong_snapshot_epoch_fails_closed();
    std::printf(
        "native_snapshot_production_virtual_link_test: ok "
        "fragmented=%u s2c_loss=%u reordered=%u duplicates=%u "
        "ack_loss=%u repeat_revalidate=%u cgame_once=%u "
        "hash_quarantine=%u wrong_epoch=%u real_domains=%u "
        "expectation_rollovers=%u timeout_recoveries=%u "
        "prediction_ready=%u digest=%016llx\n",
        metrics.fragmented_messages,
        metrics.server_to_client_losses,
        metrics.reordered_deliveries,
        metrics.duplicate_deliveries,
        metrics.lost_acknowledgements,
        metrics.repeat_revalidations,
        metrics.exact_cgame_consumes,
        metrics.hash_quarantines,
        metrics.wrong_epoch_rejections,
        metrics.real_domain_activations,
        metrics.expectation_window_rollovers,
        metrics.complete_timeout_recoveries,
        metrics.prediction_authorities,
        static_cast<unsigned long long>(metrics_digest()));
    return EXIT_SUCCESS;
}
