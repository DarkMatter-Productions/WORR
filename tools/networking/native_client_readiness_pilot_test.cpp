/* Standalone FR-10-T04 client readiness/carrier integration tests. */

#include "client.h"
#include "client/cgame_event_runtime.h"
#include "client/native_readiness_pilot.h"
#include "client/snapshot_shadow.h"

#include "common/net/native_carrier.h"
#include "common/net/native_codec.h"
#include "common/net/native_readiness.h"
#include "common/net/native_readiness_sideband.h"
#include "common/net/native_snapshot_receiver.h"
#include "shared/native_envelope.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

client_static_t cls{};
client_state_t cl{};
unsigned com_localTime{};

namespace {

constexpr uint32_t kPrivateCapabilities =
    WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK;
constexpr uint32_t kEventPrivateCapabilities =
    WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK;
constexpr uint32_t kSnapshotPrivateCapabilities =
    WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK;
static_assert(kPrivateCapabilities == UINT32_C(0x53));
static_assert(kEventPrivateCapabilities == UINT32_C(0x73));
static_assert(kSnapshotPrivateCapabilities == UINT32_C(0x57));
constexpr size_t kEncodedReadinessBytes =
    WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT * 5u;
static_assert(kEncodedReadinessBytes == 75u);
constexpr size_t kApplicationCeiling = 1024;
constexpr uint32_t kSnapshotMaxEntities = 64;
constexpr uint32_t kSnapshotStoreSlots = 8;
constexpr uint32_t kSnapshotEntitiesPerSlot = 2;
constexpr uint32_t kSnapshotAreaBytesPerSlot = 8;
constexpr uint32_t kSnapshotEventsPerSlot = 2;

cvar_t pilot_cvar{};
cvar_t event_pilot_cvar{};
cvar_t snapshot_pilot_cvar{};
cvar_t probe_hold_cvar{};
std::array<byte, 512> reliable_storage{};

struct fake_event_runtime_t {
    bool active{};
    bool fail_status{};
    uint32_t stream_epoch{};
    uint32_t first_sequence{};
    uint32_t next_sequence{};
    uint32_t authority_count{};
    uint32_t reset_calls{};
    uint32_t submit_calls{};
};

fake_event_runtime_t fake_event_runtime{};

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
                fake_event_runtime.stream_epoch ||
            records[index].event_id.sequence !=
                fake_event_runtime.next_sequence) {
            return WORR_CGAME_EVENT_RUNTIME_CONFLICT;
        }
        ++fake_event_runtime.next_sequence;
        ++fake_event_runtime.authority_count;
    }
    ++fake_event_runtime.submit_calls;
    return WORR_CGAME_EVENT_RUNTIME_OK;
}

bool fake_get_status(worr_cgame_event_runtime_status_v1 *status_out)
{
    if (!status_out || fake_event_runtime.fail_status)
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
        status.receipt.struct_size = sizeof(status.receipt);
        status.receipt.schema_version = WORR_EVENT_ABI_VERSION;
        status.receipt.stream_epoch = fake_event_runtime.stream_epoch;
        status.receipt.highest_contiguous =
            fake_event_runtime.next_sequence - 1u;
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

struct fake_snapshot_shadow_t {
    bool bind_ok{true};
    bool consumer_available{true};
    bool latest_available{};
    cl_snapshot_shadow_native_expectation_result_v1 expectation_result{
        CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_PENDING};
    uint32_t bound_epoch{};
    uint32_t bind_calls{};
    uint32_t consume_calls{};
    uint32_t status_calls{};
    worr_snapshot_store_v2 store{};
    std::array<worr_snapshot_store_slot_v2,
               kSnapshotStoreSlots> slots{};
    std::array<worr_snapshot_entity_v2,
               kSnapshotStoreSlots * kSnapshotEntitiesPerSlot>
        entities{};
    std::array<uint8_t,
               kSnapshotStoreSlots * kSnapshotAreaBytesPerSlot>
        area{};
    std::array<worr_snapshot_event_ref_v2,
               kSnapshotStoreSlots * kSnapshotEventsPerSlot>
        events{};
    worr_snapshot_ref_v2 ref{};
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    worr_native_snapshot_expectation_v1 expectation{};
    worr_cgame_snapshot_timeline_status_v2 consumer_status{};
};

fake_snapshot_shadow_t fake_snapshot_shadow{};

void fake_snapshot_reset(
    void *, uint32_t snapshot_epoch, uint32_t, uint64_t)
{
    const uint64_t generation =
        fake_snapshot_shadow.consumer_status.admission_generation;
    const uint64_t resets =
        fake_snapshot_shadow.consumer_status.resets + 1u;
    fake_snapshot_shadow.consumer_status = {};
    auto &status = fake_snapshot_shadow.consumer_status;
    status.struct_size = sizeof(status);
    status.api_version =
        WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION;
    status.active_epoch = snapshot_epoch;
    status.resets = resets;
    status.admission_generation = generation;
    status.timeline.struct_size = sizeof(status.timeline);
    status.timeline.schema_version =
        WORR_SNAPSHOT_TIMELINE_VERSION;
}

bool fake_snapshot_consume(
    void *, const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_projection_hashes_v2 *hashes,
    uint64_t receive_time_us)
{
    ++fake_snapshot_shadow.consume_calls;
    auto &status = fake_snapshot_shadow.consumer_status;
    ++status.consume_attempts;
    worr_snapshot_projection_hashes_v2 computed{};
    if (!view || !hashes ||
        !Worr_SnapshotProjectionHashesV2(
            view, kSnapshotMaxEntities, &computed) ||
        std::memcmp(&computed, hashes, sizeof(computed)) != 0 ||
        !view->snapshot ||
        view->snapshot->snapshot_id.epoch != status.active_epoch) {
        ++status.rejected;
        return false;
    }
    ++status.accepted;
    ++status.timeline.publish_count;
    status.last_receive_time_us = receive_time_us;
    status.last_endpoint_hash = hashes->endpoint_hash;
    status.last_legacy_parity_hash = hashes->legacy_parity_hash;
    status.last_snapshot_id = view->snapshot->snapshot_id;
    status.last_snapshot_hash = view->snapshot->snapshot_hash;
    status.last_event_fence_result = WORR_CGAME_EVENT_RUNTIME_OK;
    status.receipt_flags =
        WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED |
        WORR_CGAME_SNAPSHOT_RECEIPT_EVENT_FENCE_ACCEPTED;
    ++status.admission_generation;
    return true;
}

bool fake_snapshot_status(
    void *, worr_cgame_snapshot_timeline_status_v2 *status_out)
{
    ++fake_snapshot_shadow.status_calls;
    if (!status_out)
        return false;
    *status_out = fake_snapshot_shadow.consumer_status;
    return true;
}

struct packet_t {
    std::array<byte, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES> bytes{};
    size_t count{};
};

struct tx_attempt_t {
    netchan_app_tx_prepare_result_t result{NETCHAN_APP_TX_PREPARE_BYPASS};
    netchan_app_tx_prepare_output_v1_t output{};
    std::array<byte, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES> candidate{};
    std::array<byte, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES> legacy{};
    size_t legacy_bytes{};
};

netchan_app_tx_prepare_result_t occupied_tx_prepare(
    void *, const netchan_app_tx_prepare_info_v1_t *, const byte *, byte *,
    netchan_app_tx_prepare_output_v1_t *)
{
    return NETCHAN_APP_TX_PREPARE_BYPASS;
}

void occupied_tx_completion(
    void *, const netchan_app_tx_completion_info_v1_t *, const byte *)
{
}

netchan_app_rx_result_t occupied_rx(
    void *, const netchan_app_rx_info_v1_t *, const byte *,
    netchan_app_rx_output_v1_t *)
{
    return NETCHAN_APP_RX_BYPASS;
}

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr, "native_client_readiness_pilot_test:%d: %s\n",
                 line, expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

cl_native_readiness_pilot_test_state_t pilot_state()
{
    cl_native_readiness_pilot_test_state_t state{};
    CHECK(CL_NativeReadinessPilotGetTestState(&state));
    return state;
}

cl_native_readiness_pilot_status_v1 pilot_status()
{
    cl_native_readiness_pilot_status_v1 status{};
    CHECK(CL_NativeReadinessPilotGetStatusV1(&status));
    CHECK(status.struct_size == sizeof(status));
    CHECK(status.schema_version ==
          CL_NATIVE_READINESS_PILOT_STATUS_ABI_V1);
    return status;
}

worr_snapshot_entity_generation_v2 snapshot_generation(
    uint32_t index, uint32_t generation)
{
    worr_snapshot_entity_generation_v2 value{};
    value.identity.index = index;
    value.identity.generation = generation;
    value.provenance_flags =
        WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    return value;
}

worr_snapshot_player_v2 snapshot_player(uint32_t sequence)
{
    worr_snapshot_player_v2 player{};
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.controlled_entity = snapshot_generation(1, 4);
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    player.movement.origin[0] = 10.0f + sequence;
    player.movement.velocity[1] = -2.0f;
    player.movement.movement_flags = 5;
    player.movement.movement_time_ms = 16;
    player.movement.gravity = 800;
    player.movement.view_height = 22;
    player.view_angles[1] = 90.0f;
    player.gun_index = 7;
    player.gun_frame = static_cast<uint16_t>(10u + sequence);
    player.fov = 100.0f;
    for (uint32_t index = 0;
         index < WORR_SNAPSHOT_STATS_CAPACITY; ++index) {
        player.stats[index] =
            static_cast<int16_t>(index + sequence);
    }
    return player;
}

worr_snapshot_entity_v2 snapshot_entity(
    uint32_t index, uint32_t generation, uint32_t sequence)
{
    worr_snapshot_entity_v2 entity{};
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.generation = snapshot_generation(index, generation);
    entity.component_mask =
        WORR_SNAPSHOT_ENTITY_TRANSFORM |
        WORR_SNAPSHOT_ENTITY_INTERPOLATION |
        WORR_SNAPSHOT_ENTITY_MODELS |
        WORR_SNAPSHOT_ENTITY_ANIMATION |
        WORR_SNAPSHOT_ENTITY_APPEARANCE |
        WORR_SNAPSHOT_ENTITY_EFFECTS |
        WORR_SNAPSHOT_ENTITY_COLLISION;
    entity.origin[0] =
        static_cast<float>(index * 10u + sequence);
    entity.angles[1] = 5.0f * sequence;
    entity.old_origin[0] = entity.origin[0] - 1.0f;
    entity.model_index[0] =
        static_cast<uint16_t>(index + 2u);
    entity.frame = static_cast<uint16_t>(sequence);
    entity.skin = index;
    entity.solid = 1;
    entity.effects = sequence;
    entity.alpha = 1.0f;
    entity.scale = 1.0f;
    entity.owner.index = WORR_EVENT_NO_ENTITY;
    entity.old_frame = static_cast<int32_t>(sequence) - 1;
    return entity;
}

void prepare_snapshot_projection(
    uint32_t snapshot_epoch, uint32_t sequence)
{
    auto &shadow = fake_snapshot_shadow;
    CHECK(Worr_SnapshotStoreInitV2(
              &shadow.store, shadow.slots.data(),
              kSnapshotStoreSlots, shadow.entities.data(),
              static_cast<uint32_t>(shadow.entities.size()),
              kSnapshotEntitiesPerSlot, shadow.area.data(),
              static_cast<uint32_t>(shadow.area.size()),
              kSnapshotAreaBytesPerSlot, shadow.events.data(),
              static_cast<uint32_t>(shadow.events.size()),
              kSnapshotEventsPerSlot, kSnapshotMaxEntities) ==
          WORR_SNAPSHOT_STORE_OK);

    worr_snapshot_v2 snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    snapshot.flags = WORR_SNAPSHOT_FLAG_COMPLETE |
                     WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS |
                     WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE |
                     WORR_SNAPSHOT_FLAG_KEYFRAME;
    snapshot.snapshot_id.epoch = snapshot_epoch;
    snapshot.snapshot_id.sequence = sequence;
    snapshot.server_tick = 100u + sequence;
    snapshot.server_time_us =
        UINT64_C(1000000) +
        static_cast<uint64_t>(sequence) * UINT64_C(16000);
    snapshot.controlled_entity = snapshot_generation(1, 4);
    snapshot.consumed_command.cursor.epoch = 2;
    snapshot.consumed_command.cursor.contiguous_sequence =
        40u + sequence;
    snapshot.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    if (sequence == 1u) {
        snapshot.discontinuity.flags =
            WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
            WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
        snapshot.discontinuity.reason =
            WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
    } else {
        snapshot.discontinuity.flags =
            WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT |
            WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC;
        snapshot.discontinuity.reason =
            WORR_SNAPSHOT_DISCONTINUITY_REASON_HARD_RESYNC;
        snapshot.discontinuity.previous.epoch = snapshot_epoch;
        snapshot.discontinuity.previous.sequence = sequence - 1u;
        snapshot.discontinuity.server_tick_delta = 1;
    }

    const auto player = snapshot_player(sequence);
    const std::array<worr_snapshot_entity_v2, 2> entities{
        snapshot_entity(1, 4, sequence),
        snapshot_entity(2, 7, sequence)};
    const std::array<uint8_t, 2> area{
        static_cast<uint8_t>(sequence),
        static_cast<uint8_t>(sequence ^ 0x5au)};
    worr_snapshot_event_ref_v2 event{};
    event.struct_size = sizeof(event);
    event.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    event.provenance =
        WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
    event.carrier_ordinal = 0;
    event.semantic_version = WORR_EVENT_MODEL_REVISION;
    event.authority_id.stream_epoch = 5;
    event.authority_id.sequence = sequence;
    event.semantic_hash =
        UINT64_C(0xabc0000000000000) + sequence;
    worr_snapshot_store_publish_v2 publication{};
    publication.struct_size = sizeof(publication);
    publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
    publication.snapshot = &snapshot;
    publication.player = &player;
    publication.entities = entities.data();
    publication.area_bytes = area.data();
    publication.event_refs = &event;
    publication.entity_count =
        static_cast<uint32_t>(entities.size());
    publication.area_byte_count =
        static_cast<uint32_t>(area.size());
    publication.event_ref_count = 1;
    shadow.ref = {};
    const auto publish_result = Worr_SnapshotStorePublishV2(
        &shadow.store, &publication, &shadow.ref);
    CHECK(publish_result == WORR_SNAPSHOT_STORE_OK);

    const size_t entity_offset =
        static_cast<size_t>(shadow.ref.slot) *
        kSnapshotEntitiesPerSlot;
    const size_t area_offset =
        static_cast<size_t>(shadow.ref.slot) *
        kSnapshotAreaBytesPerSlot;
    const size_t event_offset =
        static_cast<size_t>(shadow.ref.slot) *
        kSnapshotEventsPerSlot;
    shadow.view = {};
    shadow.view.struct_size = sizeof(shadow.view);
    shadow.view.schema_version =
        WORR_SNAPSHOT_PROJECTION_VERSION;
    shadow.view.snapshot =
        &shadow.slots[shadow.ref.slot].snapshot;
    shadow.view.player =
        &shadow.slots[shadow.ref.slot].player;
    shadow.view.entities =
        shadow.entities.data() + entity_offset;
    shadow.view.area_bytes = shadow.area.data() + area_offset;
    shadow.view.event_refs =
        shadow.events.data() + event_offset;
    shadow.view.entity_count =
        shadow.view.snapshot->entity_range.count;
    shadow.view.area_byte_count =
        shadow.view.snapshot->area_range.count;
    shadow.view.event_ref_count =
        shadow.view.snapshot->event_range.count;
    CHECK(Worr_SnapshotProjectionHashesV2(
        &shadow.view, kSnapshotMaxEntities, &shadow.hashes));
    shadow.expectation = {};
    shadow.expectation.struct_size =
        sizeof(shadow.expectation);
    shadow.expectation.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    shadow.expectation.snapshot_id =
        shadow.view.snapshot->snapshot_id;
    shadow.expectation.hashes = shadow.hashes;
    shadow.expectation_result =
        CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE;
    shadow.latest_available = true;
}

void reset_environment(uint32_t raw_time, size_t reliable_capacity)
{
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CHECK(CL_CGameEventRuntimeResetConnection() ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    std::memset(&cls, 0, sizeof(cls));
    std::memset(&cl, 0, sizeof(cl));
    std::memset(&pilot_cvar, 0, sizeof(pilot_cvar));
    std::memset(&event_pilot_cvar, 0, sizeof(event_pilot_cvar));
    std::memset(&snapshot_pilot_cvar, 0,
                sizeof(snapshot_pilot_cvar));
    std::memset(&probe_hold_cvar, 0, sizeof(probe_hold_cvar));
    fake_event_runtime = {};
    fake_snapshot_shadow = {};
    fake_snapshot_shadow.bind_ok = true;
    fake_snapshot_shadow.consumer_available = true;
    reliable_storage.fill(0);
    com_localTime = raw_time;
    cls.netchan.type = NETCHAN_NEW;
    cls.serverProtocol = PROTOCOL_VERSION_RERELEASE;
    cl.csr.max_edicts = kSnapshotMaxEntities;
    cls.netchan.maxpacketlen = kApplicationCeiling;
    SZ_InitWrite(&cls.netchan.message, reliable_storage.data(),
                 reliable_capacity);
    CL_NativeReadinessPilotRegisterCvar();
}

bool begin_enabled(uint32_t raw_time,
                   size_t reliable_capacity = reliable_storage.size())
{
    reset_environment(raw_time, reliable_capacity);
    pilot_cvar.integer = 1;
    return CL_NativeReadinessPilotBeginConnection(&cls.netchan);
}

bool begin_event_enabled(
    uint32_t raw_time,
    size_t reliable_capacity = reliable_storage.size())
{
    reset_environment(raw_time, reliable_capacity);
    pilot_cvar.integer = 1;
    event_pilot_cvar.integer = 1;
    CHECK(CL_CGameEventRuntimeSetConsumer(&fake_event_export));
    return CL_NativeReadinessPilotBeginConnection(&cls.netchan);
}

bool begin_snapshot_enabled(
    uint32_t raw_time,
    size_t reliable_capacity = reliable_storage.size(),
    uint32_t max_entities = kSnapshotMaxEntities,
    int server_protocol = PROTOCOL_VERSION_RERELEASE)
{
    reset_environment(raw_time, reliable_capacity);
    cl.csr.max_edicts = static_cast<uint16_t>(max_entities);
    cls.serverProtocol = server_protocol;
    pilot_cvar.integer = 1;
    snapshot_pilot_cvar.integer = 1;
    return CL_NativeReadinessPilotBeginConnection(&cls.netchan);
}

worr_net_capability_state_v1 confirmed_capability(uint32_t epoch)
{
    worr_net_capability_state_v1 state{};
    CHECK(Worr_NetCapabilityStateInitV1(
        &state, epoch, WORR_NET_CAP_LEGACY_STAGE_MASK,
        WORR_NET_CAP_LEGACY_STAGE_MASK));
    worr_net_capability_confirm_v1 confirm{};
    confirm.struct_size = sizeof(confirm);
    confirm.schema_version = WORR_NET_CAPABILITY_VERSION;
    confirm.connection_epoch = epoch;
    confirm.supported = WORR_NET_CAP_LEGACY_STAGE_MASK;
    confirm.negotiated = WORR_NET_CAP_LEGACY_STAGE_MASK;
    CHECK(Worr_NetCapabilityConfirmV1(&state, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    return state;
}

void confirm_capability(uint32_t epoch)
{
    const auto state = confirmed_capability(epoch);
    CL_NativeReadinessPilotCapabilityConfirmed(&state);
}

void feed_svc_record(const worr_native_readiness_record_v1 &record)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT> pairs{};
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs.data(), static_cast<uint32_t>(pairs.size())));
    for (const auto &pair : pairs) {
        CHECK(CL_NativeReadinessPilotObserveSetting(
            static_cast<int32_t>(pair.index),
            static_cast<int32_t>(pair.value)));
    }
}

void feed_packet_record(const worr_native_readiness_record_v1 &record)
{
    CL_NativeReadinessPilotPacketBegin();
    feed_svc_record(record);
    CL_NativeReadinessPilotPacketEnd();
}

worr_native_readiness_record_v1 decode_client_record(size_t offset)
{
    CHECK(cls.netchan.message.cursize >=
          offset + kEncodedReadinessBytes);
    worr_native_readiness_sideband_parser_v1 parser{};
    CHECK(Worr_NativeReadinessSidebandParserInitV1(&parser));
    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED);
    const byte *wire = cls.netchan.message.data + offset;
    for (uint32_t index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT; ++index) {
        const byte *field = wire + index * 5u;
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

worr_native_readiness_record_v1 server_challenge(
    worr_native_readiness_state_v1 &server, uint32_t transport_epoch,
    uint64_t nonce, uint64_t now, bool advance,
    uint32_t snapshot_epoch = 0)
{
    worr_native_readiness_record_v1 challenge{};
    const uint32_t private_capabilities =
        event_pilot_cvar.integer
            ? kEventPrivateCapabilities
            : snapshot_pilot_cvar.integer
                  ? kSnapshotPrivateCapabilities
                  : kPrivateCapabilities;
    const auto result = snapshot_pilot_cvar.integer
        ? (advance
               ? Worr_NativeReadinessServerAdvanceEpochBoundV1(
                     &server, transport_epoch, snapshot_epoch,
                     private_capabilities, nonce, now, 10000,
                     &challenge)
               : Worr_NativeReadinessServerInitBoundV1(
                     &server, transport_epoch, snapshot_epoch,
                     private_capabilities, nonce, now, 10000,
                     &challenge))
        : (advance
               ? Worr_NativeReadinessServerAdvanceEpochV1(
                     &server, transport_epoch, private_capabilities,
                     nonce, now, 10000, &challenge)
               : Worr_NativeReadinessServerInitV1(
                     &server, transport_epoch, private_capabilities,
                     nonce, now, 10000, &challenge));
    CHECK(result == WORR_NATIVE_READINESS_OK);
    return challenge;
}

worr_native_readiness_record_v1 server_accept_ready(
    worr_native_readiness_state_v1 &server,
    const worr_native_readiness_record_v1 &ready, uint64_t now)
{
    worr_native_readiness_record_v1 active{};
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &server, &ready, now, &active) == WORR_NATIVE_READINESS_OK);
    return active;
}

worr_native_readiness_record_v1 begin_handshake(
    worr_native_readiness_state_v1 &server, uint32_t transport_epoch,
    uint64_t nonce, bool advance, uint32_t snapshot_epoch = 0)
{
    const size_t ready_offset = cls.netchan.message.cursize;
    const auto challenge = server_challenge(
        server, transport_epoch, nonce, com_localTime, advance,
        snapshot_epoch);
    feed_packet_record(challenge);
    CHECK(cls.netchan.message.cursize ==
          ready_offset + kEncodedReadinessBytes);
    const auto ready = decode_client_record(ready_offset);
    CHECK(ready.record_kind == WORR_NATIVE_READINESS_RECORD_CLIENT_READY);
    return server_accept_ready(server, ready, com_localTime);
}

void complete_handshake(
    const worr_native_readiness_record_v1 &server_active)
{
    feed_packet_record(server_active);
    const auto state = pilot_state();
    CHECK(state.mode == 2u && state.hooks_installed);
    CHECK(state.transport_epoch == server_active.transport_epoch);
}

void complete_event_handshake(
    worr_native_readiness_state_v1 &server,
    const worr_native_readiness_record_v1 &server_active)
{
    const size_t confirm_offset = cls.netchan.message.cursize;
    complete_handshake(server_active);
    CHECK(cls.netchan.message.cursize ==
          confirm_offset + kEncodedReadinessBytes);
    const auto confirm = decode_client_record(confirm_offset);
    CHECK(confirm.record_kind ==
          WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM);
    CHECK(confirm.negotiated_capabilities == kEventPrivateCapabilities);
    CHECK(Worr_NativeReadinessServerObserveClientActiveConfirmV1(
              &server, &confirm, com_localTime) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(server.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    const auto state = pilot_state();
    CHECK(state.event_enabled && state.client_active_confirm_queued);
    CHECK(state.private_capabilities == kEventPrivateCapabilities);
}

void complete_snapshot_handshake(
    worr_native_readiness_state_v1 &server,
    const worr_native_readiness_record_v1 &server_active,
    uint32_t snapshot_epoch)
{
    const size_t confirm_offset = cls.netchan.message.cursize;
    complete_handshake(server_active);
    CHECK(cls.netchan.message.cursize ==
          confirm_offset + kEncodedReadinessBytes);
    const auto confirm = decode_client_record(confirm_offset);
    CHECK(confirm.record_kind ==
          WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM);
    CHECK(confirm.negotiated_capabilities ==
          kSnapshotPrivateCapabilities);
    CHECK(confirm.snapshot_epoch == snapshot_epoch);
    CHECK(Worr_NativeReadinessServerObserveClientActiveConfirmV1(
              &server, &confirm, com_localTime) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(server.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    const auto state = pilot_state();
    CHECK(state.snapshot_enabled &&
          state.client_active_confirm_queued);
    CHECK(state.private_capabilities ==
          kSnapshotPrivateCapabilities);
    CHECK(state.snapshot_epoch == snapshot_epoch);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_INITIALIZED) != 0);
}

worr_prediction_command_v1 prediction_command(uint32_t sequence)
{
    worr_prediction_command_v1 command{};
    command.struct_size = sizeof(command);
    command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.duration_ms = 10;
    command.buttons = static_cast<uint8_t>(sequence & 3u);
    command.view_angles[1] = static_cast<float>((sequence % 36u) * 10u);
    command.forward_move = static_cast<float>((sequence % 100u) * 3u);
    command.side_move = -static_cast<float>(sequence % 100u);
    return command;
}

void build_command(uint32_t command_epoch, uint32_t sequence)
{
    const worr_command_id_v1 id{command_epoch, sequence};
    const auto command = prediction_command(sequence);
    CL_NativeReadinessPilotObserveFinalizedCommand(
        sequence, &id, &command);
}

void build_commands(uint32_t command_epoch, uint32_t count)
{
    for (uint32_t sequence = 1; sequence <= count; ++sequence)
        build_command(command_epoch, sequence);
}

tx_attempt_t prepare_tx(size_t legacy_bytes,
                        uint32_t application_ceiling = kApplicationCeiling)
{
    CHECK(legacy_bytes <= WORR_NATIVE_CARRIER_MAX_PACKET_BYTES);
    tx_attempt_t attempt{};
    attempt.legacy_bytes = legacy_bytes;
    for (size_t index = 0; index < legacy_bytes; ++index)
        attempt.legacy[index] = static_cast<byte>((index * 17u) ^ 0x5au);
    attempt.candidate.fill(0xcc);

    netchan_app_tx_prepare_info_v1_t info{};
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.outgoing_sequence = cls.netchan.outgoing_sequence;
    info.max_application_bytes = application_ceiling;
    info.unreliable_bytes = static_cast<uint32_t>(legacy_bytes);
    info.legacy_application_bytes = static_cast<uint32_t>(legacy_bytes);
    info.packet_copies = 1;
    attempt.output.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    attempt.output.struct_size = sizeof(attempt.output);
    CHECK(cls.netchan.app_tx_prepare);
    attempt.result = cls.netchan.app_tx_prepare(
        cls.netchan.app_tx_opaque, &info, attempt.legacy.data(),
        attempt.candidate.data(), &attempt.output);
    return attempt;
}

void complete_tx(const tx_attempt_t &attempt,
                 netchan_app_tx_completion_result_t result)
{
    CHECK(attempt.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    netchan_app_tx_completion_info_v1_t info{};
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.result = result;
    info.packet_copies = 1;
    info.accepted_copies =
        result == NETCHAN_APP_TX_COMPLETION_ACCEPTED ? 1 : 0;
    const bool fallback =
        result == NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID;
    info.application_bytes = static_cast<uint32_t>(
        fallback ? attempt.legacy_bytes : attempt.output.application_bytes);
    info.token = attempt.output.token;
    CHECK(cls.netchan.app_tx_completion);
    cls.netchan.app_tx_completion(
        cls.netchan.app_tx_opaque, &info,
        fallback ? attempt.legacy.data() : attempt.candidate.data());
}

worr_native_envelope_frame_info_v1 inspect_command_packet(
    const tx_attempt_t &attempt, uint32_t expected_epoch,
    uint32_t expected_command_sequence,
    uint32_t expected_message_sequence = 1u)
{
    CHECK(attempt.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    CHECK(attempt.output.application_bytes ==
          attempt.legacy_bytes + 206u);
    CHECK(std::memcmp(attempt.candidate.data(), attempt.legacy.data(),
                      attempt.legacy_bytes) == 0);

    worr_native_carrier_view_v1 view{};
    CHECK(Worr_NativeCarrierDecodeV1(
              attempt.candidate.data(), attempt.output.application_bytes,
              &view) == WORR_NATIVE_CARRIER_OK);
    CHECK(view.transport_epoch == expected_epoch);
    CHECK(view.legacy_bytes == attempt.legacy_bytes);
    CHECK(view.entry_count == 1);
    CHECK(view.entries[0].entry_type == WORR_NATIVE_CARRIER_ENTRY_DATA_V1);
    CHECK(view.entries[0].data_bytes == 166u);

    const byte *datagram =
        attempt.candidate.data() + view.entries[0].data_offset;
    worr_native_envelope_frame_info_v1 frame{};
    CHECK(Worr_NativeEnvelopeDecodeV1(
              datagram, view.entries[0].data_bytes, &frame) ==
          WORR_NATIVE_ENVELOPE_DECODE_OK);
    CHECK(frame.transport_epoch == expected_epoch);
    CHECK(frame.message_sequence == expected_message_sequence);
    CHECK(frame.record.record_class == WORR_NATIVE_RECORD_COMMAND_V1);
    CHECK(frame.record.object_sequence == expected_command_sequence);
    CHECK(frame.priority == 0u);
    CHECK(frame.fragment_count == 1u && frame.fragment_index == 0u);
    CHECK(frame.total_payload_bytes == 110u);

    worr_command_record_v1 command{};
    CHECK(Worr_NativeCodecCommandDecodeV1(
              datagram + frame.payload_offset, frame.fragment_payload_bytes,
              WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, &command) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(command.command_id.sequence == expected_command_sequence);
    CHECK(command.sample_time_us ==
          static_cast<uint64_t>(expected_command_sequence) * 10000u);
    return frame;
}

packet_t ack_packet(uint32_t transport_epoch, uint32_t first,
                    uint32_t last, size_t legacy_bytes = 0)
{
    packet_t packet{};
    std::array<byte, 32> legacy{};
    for (size_t index = 0; index < legacy_bytes; ++index)
        legacy[index] = static_cast<byte>(0xa0u + index);
    worr_native_carrier_entry_v1 entry{};
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    entry.first_message_sequence = first;
    entry.last_message_sequence = last;
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, legacy_bytes ? legacy.data() : nullptr,
              legacy_bytes, nullptr, 0, &entry, 1, packet.bytes.data(),
              packet.bytes.size(), &packet.count) == WORR_NATIVE_CARRIER_OK);
    return packet;
}

packet_t multi_ack_packet(uint32_t transport_epoch,
                          uint32_t first_a = 1,
                          uint32_t last_a = 1,
                          uint32_t first_b = 1,
                          uint32_t last_b = 1)
{
    packet_t packet{};
    std::array<worr_native_carrier_entry_v1, 2> entries{};
    for (auto &entry : entries) {
        entry.struct_size = sizeof(entry);
        entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
        entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    }
    entries[0].first_message_sequence = first_a;
    entries[0].last_message_sequence = last_a;
    entries[1].first_message_sequence = first_b;
    entries[1].last_message_sequence = last_b;
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, nullptr, 0, nullptr, 0, entries.data(),
              static_cast<uint16_t>(entries.size()), packet.bytes.data(),
              packet.bytes.size(), &packet.count) == WORR_NATIVE_CARRIER_OK);
    return packet;
}

packet_t native_payload_packet(
    uint32_t transport_epoch, uint32_t message_sequence,
    worr_native_record_ref_v1 record, const void *payload,
    uint32_t payload_bytes, bool include_ack = false,
    uint32_t ack_first = 0, uint32_t ack_last = 0,
    size_t legacy_bytes = 0)
{
    CHECK(payload && payload_bytes != 0);
    std::array<byte, WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES> datagram{};
    worr_native_envelope_fragmenter_v1 fragmenter{};
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, transport_epoch, message_sequence, record, 1,
        payload, payload_bytes, datagram.size()));
    size_t datagram_bytes = 0;
    CHECK(Worr_NativeEnvelopeFragmentNextV1(
              &fragmenter, payload, payload_bytes, datagram.data(),
              datagram.size(), &datagram_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_OK);
    CHECK(fragmenter.next_fragment == fragmenter.fragment_count);

    std::array<worr_native_carrier_entry_v1, 2> entries{};
    entries[0].struct_size = sizeof(entries[0]);
    entries[0].schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entries[0].entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entries[0].data_bytes = static_cast<uint32_t>(datagram_bytes);
    uint16_t entry_count = 1;
    if (include_ack) {
        CHECK(ack_first != 0 && ack_first <= ack_last);
        entries[1].struct_size = sizeof(entries[1]);
        entries[1].schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
        entries[1].entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
        entries[1].first_message_sequence = ack_first;
        entries[1].last_message_sequence = ack_last;
        entry_count = 2;
    }

    std::array<byte, 32> legacy{};
    CHECK(legacy_bytes <= legacy.size());
    for (size_t index = 0; index < legacy_bytes; ++index)
        legacy[index] = static_cast<byte>(0x30u + index);
    packet_t packet{};
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, legacy_bytes ? legacy.data() : nullptr,
              legacy_bytes, datagram.data(), datagram_bytes,
              entries.data(), entry_count, packet.bytes.data(),
              packet.bytes.size(), &packet.count) ==
          WORR_NATIVE_CARRIER_OK);
    return packet;
}

std::vector<packet_t> snapshot_packets(
    uint32_t transport_epoch, uint32_t message_sequence)
{
    uint32_t preflight_bytes = 0;
    CHECK(Worr_NativeCodecSnapshotPreflightV1(
              &fake_snapshot_shadow.view, kSnapshotMaxEntities,
              &preflight_bytes) == WORR_NATIVE_CODEC_OK);
    CHECK(preflight_bytes != 0 &&
          preflight_bytes <= WORR_NATIVE_CODEC_MAX_ENCODED_BYTES);
    std::vector<byte> encoded(preflight_bytes);
    size_t encoded_bytes = 0;
    CHECK(Worr_NativeCodecSnapshotEncodeV1(
              &fake_snapshot_shadow.view, kSnapshotMaxEntities,
              encoded.data(), encoded.size(), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes == encoded.size());
    worr_native_codec_info_v1 info{};
    worr_native_record_ref_v1 record{};
    CHECK(Worr_NativeCodecInspectV1(
              encoded.data(), encoded.size(), &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &record));

    constexpr uint32_t kFragmentDatagramBytes = 512;
    worr_native_envelope_fragmenter_v1 fragmenter{};
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, transport_epoch, message_sequence, record, 2,
        encoded.data(), static_cast<uint32_t>(encoded.size()),
        kFragmentDatagramBytes));
    std::vector<packet_t> packets;
    packets.reserve(fragmenter.fragment_count);
    while ((fragmenter.state_flags &
            WORR_NATIVE_FRAGMENTER_EXHAUSTED) == 0) {
        std::array<byte,
                   WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES>
            datagram{};
        size_t datagram_bytes = 0;
        CHECK(Worr_NativeEnvelopeFragmentNextV1(
                  &fragmenter, encoded.data(),
                  static_cast<uint32_t>(encoded.size()),
                  datagram.data(), datagram.size(),
                  &datagram_bytes) ==
              WORR_NATIVE_ENVELOPE_EMIT_OK);
        packet_t packet{};
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
        packets.push_back(packet);
    }
    CHECK(packets.size() == fragmenter.fragment_count);
    return packets;
}

packet_t descriptor_packet(
    uint32_t transport_epoch, uint32_t message_sequence,
    const worr_event_stream_descriptor_v1 &descriptor,
    bool include_ack = false, uint32_t ack_first = 0,
    uint32_t ack_last = 0, size_t legacy_bytes = 0)
{
    std::array<byte, 128> encoded{};
    size_t encoded_bytes = 0;
    CHECK(Worr_NativeCodecEventStreamEncodeV1(
              &descriptor, encoded.data(), encoded.size(),
              &encoded_bytes) == WORR_NATIVE_CODEC_OK);
    worr_native_codec_info_v1 info{};
    worr_native_record_ref_v1 record{};
    CHECK(Worr_NativeCodecInspectV1(
              encoded.data(), encoded_bytes, &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &record));
    return native_payload_packet(
        transport_epoch, message_sequence, record, encoded.data(),
        static_cast<uint32_t>(encoded_bytes), include_ack, ack_first,
        ack_last, legacy_bytes);
}

worr_event_record_v1 event_record(uint32_t stream_epoch,
                                  uint32_t sequence)
{
    worr_event_record_v1 record{};
    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
                   WORR_EVENT_FLAG_REPLAY_SAFE |
                   WORR_EVENT_FLAG_PRESENT_ONCE;
    record.event_id.stream_epoch = stream_epoch;
    record.event_id.sequence = sequence;
    record.source_tick = 1000u + sequence;
    record.source_ordinal = sequence;
    record.source_time_us = UINT64_C(9000000) + sequence;
    record.source_entity.index = 1;
    record.source_entity.generation = 4;
    record.subject_entity.index = WORR_EVENT_NO_ENTITY;
    record.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
    record.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    record.prediction_class =
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record.payload_kind = WORR_EVENT_PAYLOAD_NONE;
    CHECK(Worr_EventRecordValidateV1(
        &record, WORR_EVENT_STREAM_MAX_ENTITIES_V1));
    return record;
}

packet_t event_packet(
    uint32_t transport_epoch, uint32_t message_sequence,
    const worr_event_record_v1 &event, bool include_ack = false,
    uint32_t ack_first = 0, uint32_t ack_last = 0,
    size_t legacy_bytes = 0)
{
    std::array<byte, WORR_NATIVE_CODEC_MAX_EVENT_ENCODED_BYTES> encoded{};
    size_t encoded_bytes = 0;
    CHECK(Worr_NativeCodecEventEncodeV1(
              &event, WORR_EVENT_STREAM_MAX_ENTITIES_V1,
              encoded.data(), encoded.size(), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    worr_native_codec_info_v1 info{};
    worr_native_record_ref_v1 record{};
    CHECK(Worr_NativeCodecInspectV1(
              encoded.data(), encoded_bytes, &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &record));
    return native_payload_packet(
        transport_epoch, message_sequence, record, encoded.data(),
        static_cast<uint32_t>(encoded_bytes), include_ack, ack_first,
        ack_last, legacy_bytes);
}

netchan_app_rx_result_t receive_packet(
    const packet_t &packet, netchan_app_rx_output_v1_t &output)
{
    netchan_app_rx_info_v1_t info{};
    info.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.message_bytes = static_cast<uint32_t>(packet.count);
    info.application_bytes = static_cast<uint32_t>(packet.count);
    output = {};
    output.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output.struct_size = sizeof(output);
    CHECK(cls.netchan.app_rx);
    return cls.netchan.app_rx(cls.netchan.app_rx_opaque, &info,
                              packet.bytes.data(), &output);
}

void test_default_off_demo_and_hook_ownership()
{
    reset_environment(10, reliable_storage.size());
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);
    /* With no pilot hook installed, Netchan_Transmit remains on its legacy
     * path. The real transmit/capture byte-identity contract is exercised by
     * network-netchan-application-tx-hook. */
    CHECK(CL_NativeReadinessPilotIsCarrierSetting(
        WORR_NATIVE_READINESS_SETTING_BEGIN));

    /* Event streaming is a subordinate opt-in and cannot activate a pilot
     * without the base native shadow cvar. */
    reset_environment(10, reliable_storage.size());
    event_pilot_cvar.integer = 1;
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    reset_environment(10, reliable_storage.size());
    snapshot_pilot_cvar.integer = 1;
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    /* Event and full-snapshot semantic S2C modes intentionally remain
     * mutually exclusive until their independent sequence spaces can be
     * negotiated together. */
    reset_environment(10, reliable_storage.size());
    pilot_cvar.integer = 1;
    event_pilot_cvar.integer = 1;
    snapshot_pilot_cvar.integer = 1;
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    reset_environment(11, reliable_storage.size());
    pilot_cvar.integer = 1;
    cls.demo.playback = 1;
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    reset_environment(12, reliable_storage.size());
    pilot_cvar.integer = 1;
    cls.demo.seeking = true;
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    reset_environment(13, reliable_storage.size());
    pilot_cvar.integer = 1;
    cls.netchan.app_tx_prepare = occupied_tx_prepare;
    cls.netchan.app_tx_completion = occupied_tx_completion;
    cls.netchan.app_tx_opaque = &pilot_cvar;
    cls.netchan.app_rx = occupied_rx;
    cls.netchan.app_rx_opaque = &reliable_storage;
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(cls.netchan.app_tx_prepare == occupied_tx_prepare);
    CHECK(cls.netchan.app_tx_completion == occupied_tx_completion);
    CHECK(cls.netchan.app_tx_opaque == &pilot_cvar);
    CHECK(cls.netchan.app_rx == occupied_rx);
    CHECK(cls.netchan.app_rx_opaque == &reliable_storage);

    /* The read-only status reflects exact live hook ownership, not stale
     * adapter bookkeeping, and does not mutate or detach either hook. */
    CHECK(begin_enabled(14));
    const auto saved_rx = cls.netchan.app_rx;
    void *const saved_rx_opaque = cls.netchan.app_rx_opaque;
    cls.netchan.app_rx = occupied_rx;
    cls.netchan.app_rx_opaque = &reliable_storage;
    CHECK(pilot_status().enabled == 1u && pilot_status().hooks == 0u);
    CHECK(cls.netchan.app_rx == occupied_rx &&
          cls.netchan.app_rx_opaque == &reliable_storage);
    cls.netchan.app_rx = saved_rx;
    cls.netchan.app_rx_opaque = saved_rx_opaque;
    CHECK(pilot_status().hooks == 1u);
}

void test_initial_exact_boundary_loss_retry_ack_release()
{
    CHECK(begin_enabled(100));
    confirm_capability(7);
    worr_native_readiness_state_v1 server{};
    const auto active = begin_handshake(server, 101, 500, false);

    /* The builder observes every finalized command before CLIENT_ACTIVE. */
    /* Command four is already built, but it is outside the exact encoded
     * [1,3] range and must not be selected. */
    build_commands(7, 4);
    complete_handshake(active);

    /* The acceptance-only hold suppresses native retention without changing
     * finalized-command observation or the already-encoded legacy range. */
    probe_hold_cvar.integer = 1;
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 3);
    auto state = pilot_state();
    CHECK(!state.proof_enqueued_once && state.retained_messages == 0u);
    auto status = pilot_status();
    CHECK(status.enabled == 1u && status.mode == 2u && status.hooks == 1u);
    CHECK(status.probe_hold == 1u && status.proof_enqueued == 0u &&
          status.retained == 0u && status.retained_highwater == 0u);

    probe_hold_cvar.integer = 0;
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 3);
    state = pilot_state();
    CHECK(state.proof_enqueued_once && state.retained_messages == 1u);
    CHECK(state.retained_payloads == 1u);
    CHECK(state.message_sequence_highwater == 1u);

    const auto too_large = prepare_tx(819);
    CHECK(too_large.result == NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(pilot_state().selected_send_attempts == 0u);

    const auto exact = prepare_tx(818);
    CHECK(exact.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    CHECK(exact.output.application_bytes == 1024u);
    inspect_command_packet(exact, 101, 3);
    complete_tx(exact, NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID);
    state = pilot_state();
    CHECK(state.retained_messages == 1u &&
          state.retained_payloads == 1u &&
          state.selected_send_attempts == 0u);

    const auto rejected_retry = prepare_tx(818);
    CHECK(rejected_retry.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    inspect_command_packet(rejected_retry, 101, 3);
    complete_tx(rejected_retry, NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED);
    CHECK(pilot_state().selected_send_attempts == 0u);

    const auto retry = prepare_tx(818);
    CHECK(retry.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    inspect_command_packet(retry, 101, 3);
    CHECK(std::memcmp(exact.candidate.data(), retry.candidate.data(),
                      retry.output.application_bytes) == 0);
    complete_tx(retry, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    CHECK(pilot_state().selected_send_attempts == 1u);

    /* Lost receipt: the retained command becomes due without re-enqueue. */
    ++com_localTime;
    CHECK(prepare_tx(0).result == NETCHAN_APP_TX_PREPARE_BYPASS);
    com_localTime += 100;
    const auto resend = prepare_tx(0);
    CHECK(resend.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    inspect_command_packet(resend, 101, 3);
    complete_tx(resend, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    CHECK(pilot_state().selected_send_attempts == 2u);

    const auto acknowledgement = ack_packet(101, 1, 1, 3);
    netchan_app_rx_output_v1_t output{};
    CHECK(receive_packet(acknowledgement, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(output.legacy_bytes == 3u);
    state = pilot_state();
    CHECK(state.retained_messages == 0u && state.retained_payloads == 0u);

    /* Exact duplicate ACK is idempotent. */
    CHECK(receive_packet(acknowledgement, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().retained_payloads == 0u);
    status = pilot_status();
    CHECK(status.readiness_phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    CHECK(status.official_epoch == 7u && status.transport_epoch == 101u);
    CHECK(status.protocol == PROTOCOL_VERSION_RERELEASE);
    CHECK(status.public_mask == WORR_NET_CAP_LEGACY_STAGE_MASK &&
          status.private_mask == kPrivateCapabilities);
    CHECK(status.challenges == 1u && status.client_ready_queued == 1u &&
          status.server_active == 1u && status.proof_enqueued == 1u);
    CHECK(status.retained == 0u && status.retained_highwater == 1u &&
          status.retained_releases == 1u);
    CHECK(status.tx_first_sends == 1u && status.tx_retries == 1u &&
          status.tx_handoffs == 2u);
    CHECK(status.ack_carriers == 2u &&
          status.acknowledged_reliable == 1u);
    CHECK(status.drains == 0u && status.failures == 0u &&
          status.last_failure == 0u);

    /* Once ACK releases the stop-and-wait slot, a later legacy command range
     * may contribute another observational native sample. */
    CL_NativeReadinessPilotObserveEncodedCommandRange(4, 1);
    CHECK(pilot_state().message_sequence_highwater == 2u);
    CHECK(pilot_state().retained_messages == 1u);
    const auto second = prepare_tx(0);
    inspect_command_packet(second, 101, 4, 2);
    complete_tx(second, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    const auto second_acknowledgement = ack_packet(101, 2, 2);
    CHECK(receive_packet(second_acknowledgement, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().retained_messages == 0u);

    /* Repeated and older range notifications cannot re-enqueue a sample. */
    CL_NativeReadinessPilotObserveEncodedCommandRange(4, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 3);
    CHECK(pilot_state().message_sequence_highwater == 2u);
    CHECK(prepare_tx(0).result == NETCHAN_APP_TX_PREPARE_BYPASS);
}

void test_repeated_stop_and_wait_loss_retry_ack_release()
{
    constexpr uint32_t kCommandCount = 256;
    constexpr uint32_t kCommandEpoch = 8;
    constexpr uint32_t kTransportEpoch = 301;

    CHECK(begin_enabled(300));
    confirm_capability(kCommandEpoch);
    worr_native_readiness_state_v1 server{};
    const auto active = begin_handshake(
        server, kTransportEpoch, 700, false);
    complete_handshake(active);

    netchan_app_rx_output_v1_t output{};
    uint64_t accepted_retries = 0;
    uint64_t duplicate_acks = 0;
    for (uint32_t sequence = 1; sequence <= kCommandCount; ++sequence) {
        build_command(kCommandEpoch, sequence);
        CL_NativeReadinessPilotObserveEncodedCommandRange(sequence, 1);
        auto state = pilot_state();
        CHECK(state.mode == 2u && state.proof_enqueued_once);
        CHECK(state.retained_messages == 1u &&
              state.retained_payloads == 1u);
        CHECK(state.message_sequence_highwater == sequence);

        /* While occupied, even a malformed later callback is observationally
         * ignored: the authoritative legacy send path must never be stalled. */
        CL_NativeReadinessPilotObserveEncodedCommandRange(0, 0);
        CHECK(pilot_status().failures == 0u);

        const size_t legacy_bytes = sequence % 17u;
        auto outgoing = prepare_tx(legacy_bytes);
        inspect_command_packet(outgoing, kTransportEpoch, sequence,
                               sequence);
        if (sequence % 17u == 0u) {
            complete_tx(outgoing, NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED);
            CHECK(pilot_state().selected_send_attempts == 0u);
            outgoing = prepare_tx(legacy_bytes);
            inspect_command_packet(outgoing, kTransportEpoch, sequence,
                                   sequence);
        }
        complete_tx(outgoing, NETCHAN_APP_TX_COMPLETION_ACCEPTED);

        /* Deterministic lost-receipt samples prove byte-identical resend of
         * retained DATA without allocating a second command. */
        if (sequence % 23u == 0u) {
            com_localTime += 100;
            const auto retry = prepare_tx(legacy_bytes);
            inspect_command_packet(retry, kTransportEpoch, sequence,
                                   sequence);
            CHECK(std::memcmp(outgoing.candidate.data(),
                              retry.candidate.data(),
                              retry.output.application_bytes) == 0);
            complete_tx(retry, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
            ++accepted_retries;
        }

        const auto acknowledgement = sequence >= 3u && sequence % 19u == 0u
            ? multi_ack_packet(
                  kTransportEpoch, 1, 1, sequence, sequence)
            : ack_packet(
                  kTransportEpoch, sequence, sequence, sequence % 5u);
        CHECK(receive_packet(acknowledgement, output) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        state = pilot_state();
        CHECK(state.retained_messages == 0u &&
              state.retained_payloads == 0u);

        if (sequence % 31u == 0u) {
            CHECK(receive_packet(acknowledgement, output) ==
                  NETCHAN_APP_RX_EXPOSE_LEGACY);
            ++duplicate_acks;
        }

        CL_NativeReadinessPilotObserveEncodedCommandRange(sequence, 1);
        if (sequence > 1u) {
            CL_NativeReadinessPilotObserveEncodedCommandRange(
                sequence - 1u, 1);
        }
        CHECK(pilot_state().retained_messages == 0u);
        CHECK(pilot_state().message_sequence_highwater == sequence);
    }

    const auto status = pilot_status();
    CHECK(status.proof_enqueued == kCommandCount);
    CHECK(status.retained == 0u && status.retained_highwater == 1u);
    CHECK(status.retained_releases == kCommandCount);
    CHECK(status.tx_first_sends == kCommandCount);
    CHECK(status.tx_retries == accepted_retries);
    CHECK(status.tx_handoffs == kCommandCount + accepted_retries);
    CHECK(status.ack_carriers == kCommandCount + duplicate_acks);
    CHECK(status.acknowledged_reliable == kCommandCount);
    CHECK(status.drains == 0u && status.failures == 0u &&
          status.last_failure == 0u);
}

void test_map_cancellation_barrier_epoch_switch_and_reconnect()
{
    CHECK(begin_enabled(500));
    confirm_capability(20);
    worr_native_readiness_state_v1 server{};
    auto active = begin_handshake(server, 501, 900, false);
    complete_handshake(active);
    build_commands(20, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    auto first = prepare_tx(0);
    inspect_command_packet(first, 501, 1);
    complete_tx(first, NETCHAN_APP_TX_COMPLETION_ACCEPTED);

    CL_NativeReadinessPilotQuiesceMap();
    CHECK(pilot_state().mode == 3u);
    CHECK(prepare_tx(0).result == NETCHAN_APP_TX_PREPARE_BYPASS);
    feed_packet_record(active);
    CHECK(pilot_state().mode == 3u &&
          pilot_state().transport_epoch == 501u);

    CL_NativeReadinessPilotServerDataReset();
    confirm_capability(21);
    ++com_localTime;
    active = begin_handshake(server, 502, 901, true);
    auto state = pilot_state();
    CHECK(state.mode == 1u && state.transport_epoch == 0u &&
          state.retained_messages == 0u &&
          state.retained_payloads == 0u &&
          state.retired_transport_epoch == 0u &&
          state.cancelled_through_transport_epoch == 501u &&
          state.cancelled_transports == 1u &&
          state.cancelled_commands == 1u);
    CHECK(pilot_status().cancelled_through_transport_epoch == 501u &&
          pilot_status().retained == 0u);
    complete_handshake(active);
    CHECK(pilot_state().transport_epoch == 502u &&
          pilot_state().retired_transport_epoch == 0u);
    build_commands(21, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    CHECK(pilot_state().retained_messages == 1u);
    CHECK(pilot_status().retained == 1u &&
          pilot_status().retained_highwater == 1u);

    /* The negotiated barrier makes delayed old ACKs legacy-only.  They do
     * not release the fresh epoch's identically numbered retained command. */
    netchan_app_rx_output_v1_t output{};
    const auto old_ack = ack_packet(501, 1, 1, 3);
    const auto before_old_ack = pilot_state();
    const auto status_before_old_ack = pilot_status();
    CHECK(receive_packet(old_ack, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(output.legacy_bytes == 3u);
    CHECK(pilot_state().retained_messages == 1u &&
          pilot_state().retained_payloads == 1u);
    CHECK(pilot_status().retained == 1u &&
          pilot_status().retained_releases == 0u &&
          pilot_status().ack_carriers ==
              status_before_old_ack.ack_carriers);
    CHECK(pilot_state().message_sequence_highwater ==
              before_old_ack.message_sequence_highwater &&
          pilot_state().cancelled_through_transport_epoch == 501u);

    auto second = prepare_tx(0);
    const auto second_frame = inspect_command_packet(second, 502, 1);
    CHECK(second_frame.message_sequence == 1u);
    complete_tx(second, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    const auto current_ack = ack_packet(502, 1, 1);
    CHECK(receive_packet(current_ack, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().retained_messages == 0u &&
          pilot_state().retained_payloads == 0u);

    /* A second rotation advances the monotonic floor and again creates a
     * fresh current transport without overwriting/accumulating retired work. */
    CL_NativeReadinessPilotQuiesceMap();
    CL_NativeReadinessPilotServerDataReset();
    confirm_capability(23);
    ++com_localTime;
    active = begin_handshake(server, 503, 902, true);
    CHECK(pilot_state().transport_epoch == 0u &&
          pilot_state().retired_transport_epoch == 0u &&
          pilot_state().cancelled_through_transport_epoch == 502u &&
          pilot_state().cancelled_transports == 2u);
    complete_handshake(active);
    CHECK(pilot_state().transport_epoch == 503u &&
          pilot_state().retired_transport_epoch == 0u &&
          pilot_state().retired_messages == 0u &&
          pilot_state().retired_payloads == 0u);

    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_tx_completion &&
          !cls.netchan.app_tx_opaque && !cls.netchan.app_rx &&
          !cls.netchan.app_rx_opaque);

    /* A fresh owner/session may bind a numerically lower server epoch. */
    CHECK(begin_enabled(700));
    confirm_capability(22);
    server = {};
    active = begin_handshake(server, 41, 1000, false);
    complete_handshake(active);
    CHECK(pilot_state().cancelled_through_transport_epoch == 0u);
    build_commands(22, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    const auto reconnect = prepare_tx(0);
    inspect_command_packet(reconnect, 41, 1);
}

void test_pending_handshake_epoch_enters_cancellation_floor()
{
    constexpr uint32_t kPendingEpoch = 601;
    constexpr uint32_t kReplacementEpoch = 602;

    CHECK(begin_enabled(600));
    confirm_capability(24);
    worr_native_readiness_state_v1 server{};
    const size_t first_ready_offset = cls.netchan.message.cursize;
    const auto first_challenge = server_challenge(
        server, kPendingEpoch, 1050, com_localTime, false);
    feed_packet_record(first_challenge);
    const auto first_ready = decode_client_record(first_ready_offset);
    CHECK(first_ready.transport_epoch == kPendingEpoch);
    const auto first_active = server_accept_ready(
        server, first_ready, com_localTime);
    CHECK(first_active.transport_epoch == kPendingEpoch);
    auto state = pilot_state();
    CHECK(state.transport_epoch == 0u &&
          state.cancelled_through_transport_epoch == 0u &&
          state.cancelled_transports == 0u);
    const uint64_t barriers_before = state.cancellation_barriers;

    /* Replace the map while CLIENT_READY is pending and before any transport
     * bank exists.  The observed epoch is still an issued private identity
     * and must become part of the next cancellation declaration. */
    CL_NativeReadinessPilotQuiesceMap();
    CL_NativeReadinessPilotServerDataReset();
    confirm_capability(25);
    ++com_localTime;
    const size_t second_ready_offset = cls.netchan.message.cursize;
    const auto second_challenge = server_challenge(
        server, kReplacementEpoch, 1051, com_localTime, true);
    feed_packet_record(second_challenge);
    const auto second_ready = decode_client_record(second_ready_offset);
    CHECK(second_ready.transport_epoch == kReplacementEpoch);
    state = pilot_state();
    CHECK(state.mode == 1u && state.transport_epoch == 0u &&
          state.retired_transport_epoch == 0u &&
          state.cancelled_through_transport_epoch == kPendingEpoch &&
          state.cancellation_barriers == barriers_before + 1u &&
          state.cancelled_transports == 0u &&
          state.cancelled_commands == 0u &&
          state.cancelled_event_rx == 0u &&
          state.cancelled_event_receipts == 0u);

    /* Both control declarations were valid for the replaced pending epoch.
     * Their reliable delivery may occur after the newer CHALLENGE; consume
     * them at the cancellation floor without requeueing READY/CONFIRM,
     * reactivating the old session, or entering DRAIN. */
    const size_t stale_control_offset = cls.netchan.message.cursize;
    const auto status_before_stale_control = pilot_status();
    feed_packet_record(first_challenge);
    feed_packet_record(first_active);
    const auto after_stale_control = pilot_state();
    const auto status_after_stale_control = pilot_status();
    CHECK(cls.netchan.message.cursize == stale_control_offset &&
          after_stale_control.mode == state.mode &&
          after_stale_control.transport_epoch == state.transport_epoch &&
          after_stale_control.cancelled_through_transport_epoch ==
              state.cancelled_through_transport_epoch &&
          status_after_stale_control.server_active ==
              status_before_stale_control.server_active &&
          status_after_stale_control.failures ==
              status_before_stale_control.failures &&
          status_after_stale_control.stale_cancelled_readiness_records ==
              status_before_stale_control
                      .stale_cancelled_readiness_records + 2u);

    netchan_app_rx_output_v1_t output{};
    const auto delayed_pending_ack = ack_packet(
        kPendingEpoch, 1, 1, 2);
    CHECK(receive_packet(delayed_pending_ack, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(output.legacy_bytes == 2u &&
          pilot_status().stale_cancelled_carriers == 1u);

    const auto replacement_active = server_accept_ready(
        server, second_ready, com_localTime);
    complete_handshake(replacement_active);
    CHECK(pilot_state().transport_epoch == kReplacementEpoch &&
          pilot_state().retired_transport_epoch == 0u &&
          pilot_state().cancelled_through_transport_epoch ==
              kPendingEpoch);
}

void test_no_carrier_malformed_wrong_direction_and_epoch()
{
    CHECK(begin_enabled(800));
    confirm_capability(30);
    worr_native_readiness_state_v1 server{};
    auto active = begin_handshake(server, 801, 1100, false);
    complete_handshake(active);
    build_commands(30, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    auto outgoing = prepare_tx(5);
    inspect_command_packet(outgoing, 801, 1);

    packet_t legacy{};
    legacy.count = 7;
    for (size_t index = 0; index < legacy.count; ++index)
        legacy.bytes[index] = static_cast<byte>(index + 1u);
    const auto legacy_before = legacy;
    netchan_app_rx_output_v1_t output{};
    CHECK(receive_packet(legacy, output) == NETCHAN_APP_RX_BYPASS);
    CHECK(std::memcmp(legacy.bytes.data(), legacy_before.bytes.data(),
                      legacy.count) == 0);

    /* Client receive direction is ACK-only; DATA is rejected into DRAIN. */
    packet_t wrong_direction{};
    wrong_direction.count = outgoing.output.application_bytes;
    std::memcpy(wrong_direction.bytes.data(), outgoing.candidate.data(),
                wrong_direction.count);
    complete_tx(outgoing, NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED);
    CHECK(receive_packet(wrong_direction, output) ==
          NETCHAN_APP_RX_REJECT);
    CHECK(pilot_state().mode == 3u && pilot_state().hooks_installed);

    CHECK(begin_enabled(900));
    confirm_capability(31);
    server = {};
    active = begin_handshake(server, 901, 1200, false);
    complete_handshake(active);
    build_commands(31, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    outgoing = prepare_tx(0);
    complete_tx(outgoing, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    const auto wrong_epoch = ack_packet(902, 1, 1);
    CHECK(receive_packet(wrong_epoch, output) == NETCHAN_APP_RX_REJECT);
    CHECK(pilot_state().mode == 3u && pilot_state().retained_payloads == 1u);
    const auto correct_after_failure = ack_packet(901, 1, 1);
    CHECK(receive_packet(correct_after_failure, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().mode == 3u &&
          pilot_state().retained_payloads == 0u);

    CHECK(begin_enabled(950));
    confirm_capability(33);
    server = {};
    active = begin_handshake(server, 951, 1250, false);
    complete_handshake(active);
    build_commands(33, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    outgoing = prepare_tx(0);
    complete_tx(outgoing, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    /* ACK-only carriers may contain the protocol's full bounded range list;
     * redundant ranges remain idempotent and release at most one retained
     * stop-and-wait message. */
    const auto multiple = multi_ack_packet(951);
    CHECK(receive_packet(multiple, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().mode == 2u &&
          pilot_state().retained_payloads == 0u);

    CHECK(begin_enabled(1000));
    confirm_capability(34);
    server = {};
    active = begin_handshake(server, 1001, 1300, false);
    complete_handshake(active);
    build_commands(34, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    outgoing = prepare_tx(0);
    complete_tx(outgoing, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    auto corrupt = ack_packet(1001, 1, 1);
    CHECK(corrupt.count > WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES);
    corrupt.bytes[0] ^= 0x40u;
    CHECK(receive_packet(corrupt, output) == NETCHAN_APP_RX_REJECT);
    CHECK(pilot_state().mode == 3u && pilot_state().hooks_installed);
}

void test_readiness_atomic_capacity_and_pretraffic_failure()
{
    worr_native_readiness_state_v1 server{};
    CHECK(begin_enabled(1100, 64));
    confirm_capability(40);
    const auto challenge = server_challenge(server, 1101, 1400, 1100,
                                             false);
    feed_packet_record(challenge);
    CHECK(cls.netchan.message.cursize == 0u);
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    CHECK(begin_enabled(UINT32_MAX - 5u));
    confirm_capability(41);
    server = {};
    const auto wrapped = server_challenge(
        server, 1102, 1401, UINT32_MAX - UINT64_C(5), false);
    com_localTime = 3;
    feed_packet_record(wrapped);
    CHECK(cls.netchan.message.cursize == kEncodedReadinessBytes);

    CHECK(begin_enabled(1200));
    CL_NativeReadinessPilotPacketBegin();
    CL_NativeReadinessPilotPacketBegin();
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    /* SERVERDATA deliberately resets the parser inside an already-open
     * packet; the canonical parse loop immediately opens the post-reset
     * transaction before consuming capability/readiness settings. */
    CHECK(begin_enabled(1300));
    CL_NativeReadinessPilotPacketBegin();
    CL_NativeReadinessPilotServerDataReset();
    CL_NativeReadinessPilotPacketBegin();
    confirm_capability(42);
    server = {};
    const auto same_packet =
        server_challenge(server, 1301, 1500, 1300, false);
    feed_svc_record(same_packet);
    CL_NativeReadinessPilotPacketEnd();
    CHECK(cls.netchan.message.cursize == kEncodedReadinessBytes);
    CHECK(pilot_state().hooks_installed);

    /* Once CLIENT_READY is in the reliable queue it cannot be withdrawn.
     * A later invalid ACTIVE must retain DRAIN hooks so peer WTC bytes can
     * never fall through into the legacy parser. */
    CHECK(begin_enabled(1400));
    confirm_capability(43);
    server = {};
    const auto valid_active = begin_handshake(
        server, 1401, 1600, false);
    CHECK(pilot_state().readiness_committed);
    worr_native_readiness_record_v1 invalid_active{};
    CHECK(Worr_NativeReadinessRecordInitV1(
        &invalid_active, WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE,
        valid_active.transport_epoch, kPrivateCapabilities,
        valid_active.readiness_nonce + 1u));
    feed_packet_record(invalid_active);
    CHECK(pilot_state().mode == 3u && pilot_state().hooks_installed);
    CHECK(!pilot_state().carrier_traffic_seen);

    /* Fail-closed DRAIN is monotonic for this map epoch.  A later valid
     * SERVER_ACTIVE cannot resurrect the native session or select DATA. */
    feed_packet_record(valid_active);
    CHECK(pilot_state().mode == 3u && pilot_state().hooks_installed);
    CHECK(pilot_state().transport_epoch == 0u);
    build_commands(43, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    CHECK(pilot_state().retained_messages == 0u &&
          pilot_state().retained_payloads == 0u);
    CHECK(prepare_tx(0).result == NETCHAN_APP_TX_PREPARE_BYPASS);

    /* A rotation with insufficient reliable capacity proves cancellation on
     * copies but cannot publish it: no CLIENT_READY byte, floor advance, or
     * retained-state disposition occurs before the queue point of no return. */
    CHECK(begin_enabled(1500));
    confirm_capability(44);
    server = {};
    auto active = begin_handshake(server, 1501, 1700, false);
    complete_handshake(active);
    build_command(44, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    CHECK(pilot_state().retained_messages == 1u);
    CL_NativeReadinessPilotQuiesceMap();
    CL_NativeReadinessPilotServerDataReset();
    confirm_capability(45);
    const auto before_capacity_barrier = pilot_state();
    const size_t reliable_before = cls.netchan.message.cursize;
    cls.netchan.message.maxsize =
        static_cast<uint32_t>(reliable_before + 64u);
    ++com_localTime;
    const auto capacity_challenge = server_challenge(
        server, 1502, 1701, com_localTime, true);
    feed_packet_record(capacity_challenge);
    const auto after_capacity_barrier = pilot_state();
    CHECK(cls.netchan.message.cursize == reliable_before &&
          after_capacity_barrier.mode == 3u &&
          after_capacity_barrier.transport_epoch == 1501u &&
          after_capacity_barrier.retained_messages == 1u &&
          after_capacity_barrier.retained_payloads == 1u &&
          after_capacity_barrier.cancelled_through_transport_epoch == 0u &&
          after_capacity_barrier.cancellation_barriers ==
              before_capacity_barrier.cancellation_barriers &&
          after_capacity_barrier.cancelled_transports ==
              before_capacity_barrier.cancelled_transports &&
          after_capacity_barrier.hooks_installed);
}

void test_event_opt_in_semantic_admission_and_mixed_egress()
{
    constexpr uint32_t kCommandEpoch = 70;
    constexpr uint32_t kTransportEpoch = 1701;
    constexpr uint32_t kStreamEpoch = 9001;

    CHECK(begin_event_enabled(1700));
    CHECK(pilot_state().event_enabled);
    CHECK(pilot_status().private_mask == kEventPrivateCapabilities);
    confirm_capability(kCommandEpoch);
    worr_native_readiness_state_v1 server{};
    const auto active = begin_handshake(
        server, kTransportEpoch, 2700, false);
    CHECK(server.phase ==
          WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM);
    complete_event_handshake(server, active);

    worr_event_stream_descriptor_v1 descriptor{};
    CHECK(Worr_EventStreamDescriptorInitV1(
        &descriptor, kStreamEpoch, 1));
    const auto descriptor_data = descriptor_packet(
        kTransportEpoch, 1, descriptor, false, 0, 0, 3);
    netchan_app_rx_output_v1_t output{};
    const uint32_t reset_calls_before = fake_event_runtime.reset_calls;
    CHECK(receive_packet(descriptor_data, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(output.legacy_bytes == 3u);
    CHECK(fake_event_runtime.active &&
          fake_event_runtime.stream_epoch == kStreamEpoch &&
          fake_event_runtime.reset_calls == reset_calls_before + 1u);
    auto state = pilot_state();
    CHECK(state.event_rx_occupied == 0u &&
          state.event_ack_receipts == 1u);
    CHECK((state.event_owner_flags &
           WORR_EVENT_STREAM_OWNER_ACTIVE) != 0);
    CHECK(CL_NativeReadinessPilotOutputDue());

    auto ack_only = prepare_tx(0);
    CHECK(ack_only.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    worr_native_carrier_view_v1 ack_view{};
    CHECK(Worr_NativeCarrierDecodeV1(
              ack_only.candidate.data(),
              ack_only.output.application_bytes, &ack_view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(ack_view.transport_epoch == kTransportEpoch &&
          ack_view.entry_count == 1u &&
          ack_view.entries[0].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_ACK_V1 &&
          ack_view.entries[0].first_message_sequence == 1u &&
          ack_view.entries[0].last_message_sequence == 1u);
    complete_tx(ack_only, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    CHECK(!CL_NativeReadinessPilotOutputDue());

    /* Exact committed descriptor DATA revalidates against a fresh cgame
     * status and rearms only its existing semantic receipt. */
    const uint32_t resets_after_descriptor =
        fake_event_runtime.reset_calls;
    CHECK(receive_packet(descriptor_data, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(fake_event_runtime.reset_calls == resets_after_descriptor);
    CHECK(CL_NativeReadinessPilotOutputDue());

    com_localTime += 100;
    const auto event = event_record(kStreamEpoch, 1);
    const auto event_data = event_packet(
        kTransportEpoch, 2, event);
    CHECK(receive_packet(event_data, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(fake_event_runtime.submit_calls == 1u &&
          fake_event_runtime.authority_count == 1u &&
          fake_event_runtime.next_sequence == 2u);
    state = pilot_state();
    CHECK(state.event_ack_receipts == 2u);

    build_command(kCommandEpoch, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    CHECK(CL_NativeReadinessPilotOutputDue());
    const auto mixed = prepare_tx(0);
    CHECK(mixed.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    worr_native_carrier_view_v1 mixed_view{};
    CHECK(Worr_NativeCarrierDecodeV1(
              mixed.candidate.data(), mixed.output.application_bytes,
              &mixed_view) == WORR_NATIVE_CARRIER_OK);
    CHECK(mixed_view.transport_epoch == kTransportEpoch &&
          mixed_view.entry_count == 2u &&
          mixed_view.entries[0].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_DATA_V1 &&
          mixed_view.entries[1].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_ACK_V1 &&
          mixed_view.entries[1].first_message_sequence == 1u &&
          mixed_view.entries[1].last_message_sequence == 2u);
    worr_native_envelope_frame_info_v1 command_frame{};
    CHECK(Worr_NativeEnvelopeDecodeV1(
              mixed.candidate.data() + mixed_view.entries[0].data_offset,
              mixed_view.entries[0].data_bytes, &command_frame) ==
          WORR_NATIVE_ENVELOPE_DECODE_OK);
    CHECK(command_frame.record.record_class ==
              WORR_NATIVE_RECORD_COMMAND_V1 &&
          command_frame.record.object_sequence == 1u);
    complete_tx(mixed, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    CHECK(pilot_state().retained_messages == 1u);

    const auto command_ack = ack_packet(kTransportEpoch, 1, 1);
    CHECK(receive_packet(command_ack, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().retained_messages == 0u);
    CHECK(!CL_NativeReadinessPilotOutputDue());
    com_localTime += 100;
    CHECK(CL_NativeReadinessPilotOutputDue());
}

void test_event_ack_exhaustion_cancellation_and_stale_floor()
{
    constexpr uint32_t kOldTransportEpoch = 1801;
    constexpr uint32_t kNewTransportEpoch = 1802;

    CHECK(begin_event_enabled(1800));
    confirm_capability(80);
    worr_native_readiness_state_v1 server{};
    auto active = begin_handshake(
        server, kOldTransportEpoch, 2800, false);
    complete_event_handshake(server, active);

    worr_event_stream_descriptor_v1 old_descriptor{};
    CHECK(Worr_EventStreamDescriptorInitV1(
        &old_descriptor, 9101, 1));
    const auto old_data = descriptor_packet(
        kOldTransportEpoch, 1, old_descriptor, false, 0, 0, 4);
    netchan_app_rx_output_v1_t output{};
    CHECK(receive_packet(old_data, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(output.legacy_bytes == 4u);

    build_command(80, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    auto old_mixed = prepare_tx(0);
    CHECK(old_mixed.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    complete_tx(old_mixed, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    CHECK(pilot_state().retained_messages == 1u);
    /* The descriptor receipt has three bounded proactive handoffs.  Lose all
     * three while keeping the client command retained on the old transport. */
    com_localTime += 100;
    CL_NativeReadinessPilotQuiesceMap();
    CHECK(pilot_state().mode == 3u);
    CHECK(CL_NativeReadinessPilotOutputDue());
    for (uint32_t handoff = 0; handoff < 2; ++handoff) {
        const auto drain_ack = prepare_tx(0);
        worr_native_carrier_view_v1 drain_view{};
        CHECK(Worr_NativeCarrierDecodeV1(
                  drain_ack.candidate.data(),
                  drain_ack.output.application_bytes, &drain_view) ==
              WORR_NATIVE_CARRIER_OK);
        CHECK(drain_view.transport_epoch == kOldTransportEpoch &&
              drain_view.entry_count == 1u &&
              drain_view.entries[0].entry_type ==
                  WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
        complete_tx(drain_ack, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
        CHECK(pilot_state().retained_messages == 1u);
        if (handoff == 0) {
            com_localTime += 100;
            CHECK(CL_NativeReadinessPilotOutputDue());
        }
    }
    CHECK(pilot_state().event_ack_receipts == 1u);
    CHECK(!CL_NativeReadinessPilotOutputDue());
    CHECK(prepare_tx(0).result == NETCHAN_APP_TX_PREPARE_BYPASS);

    CL_NativeReadinessPilotServerDataReset();
    confirm_capability(81);
    ++com_localTime;
    const size_t ready_offset = cls.netchan.message.cursize;
    const auto challenge = server_challenge(
        server, kNewTransportEpoch, 2801, com_localTime, true);
    const auto before_barrier = pilot_state();
    feed_packet_record(challenge);
    CHECK(cls.netchan.message.cursize ==
          ready_offset + kEncodedReadinessBytes);
    const auto ready = decode_client_record(ready_offset);
    auto state = pilot_state();
    CHECK(state.mode == 1u && state.transport_epoch == 0u &&
          state.retired_transport_epoch == 0u &&
          state.retained_messages == 0u &&
          state.retained_payloads == 0u &&
          state.event_ack_receipts == 0u &&
          state.cancelled_through_transport_epoch ==
              kOldTransportEpoch &&
          state.cancellation_barriers ==
              before_barrier.cancellation_barriers + 1u &&
          state.cancelled_transports == 1u &&
          state.cancelled_commands == 1u &&
          state.cancelled_event_receipts == 1u);
    CHECK((state.event_owner_flags &
           WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC) != 0 &&
          state.event_owner_epoch_high_water == 9101u);
    const auto cancellation_status = pilot_status();
    CHECK(cancellation_status.cancelled_through_transport_epoch ==
              kOldTransportEpoch &&
          cancellation_status.cancellation_barriers ==
              state.cancellation_barriers &&
          cancellation_status.cancelled_transports == 1u &&
          cancellation_status.cancelled_command_tx == 1u &&
          cancellation_status.cancelled_event_rx == 0u &&
          cancellation_status.cancelled_event_receipts == 1u &&
          cancellation_status.stale_cancelled_carriers == 0u &&
          cancellation_status.stale_cancelled_readiness_records == 0u);

    /* An exact duplicate requeues the exact CLIENT_READY but cannot reopen
     * cancellation, advance its floor, or reset cgame/event-owner state. */
    const size_t duplicate_offset = cls.netchan.message.cursize;
    const uint32_t resets_before_duplicate =
        fake_event_runtime.reset_calls;
    feed_packet_record(challenge);
    CHECK(cls.netchan.message.cursize ==
          duplicate_offset + kEncodedReadinessBytes);
    const auto duplicate_ready = decode_client_record(duplicate_offset);
    CHECK(std::memcmp(&ready, &duplicate_ready, sizeof(ready)) == 0);
    const auto duplicate_state = pilot_state();
    CHECK(duplicate_state.cancelled_through_transport_epoch ==
              state.cancelled_through_transport_epoch &&
          duplicate_state.cancellation_barriers ==
              state.cancellation_barriers &&
          duplicate_state.cancelled_transports ==
              state.cancelled_transports &&
          fake_event_runtime.reset_calls == resets_before_duplicate);

    active = server_accept_ready(server, ready, com_localTime);
    complete_event_handshake(server, active);
    state = pilot_state();
    CHECK(state.transport_epoch == kNewTransportEpoch &&
          state.retired_transport_epoch == 0u &&
          state.event_ack_receipts == 0u);

    /* Valid canceled DATA and ACK carriers expose their exact legacy prefix
     * without touching the fresh transport, readiness, ACKs, or cgame. */
    build_command(81, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    CHECK(pilot_state().retained_messages == 1u);
    const auto before_stale = pilot_state();
    const auto status_before_stale = pilot_status();
    const uint32_t resets_before_stale = fake_event_runtime.reset_calls;
    const uint32_t submits_before_stale = fake_event_runtime.submit_calls;
    CHECK(receive_packet(old_data, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(output.legacy_bytes == 4u);
    const auto old_command_ack = ack_packet(
        kOldTransportEpoch, 1, 1, 3);
    CHECK(receive_packet(old_command_ack, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(output.legacy_bytes == 3u);
    const auto after_stale = pilot_state();
    const auto status_after_stale = pilot_status();
    CHECK(after_stale.transport_epoch == before_stale.transport_epoch &&
          after_stale.retained_messages ==
              before_stale.retained_messages &&
          after_stale.retained_payloads ==
              before_stale.retained_payloads &&
          after_stale.message_sequence_highwater ==
              before_stale.message_sequence_highwater &&
          after_stale.event_rx_occupied ==
              before_stale.event_rx_occupied &&
          after_stale.event_ack_receipts ==
              before_stale.event_ack_receipts &&
          after_stale.event_owner_flags ==
              before_stale.event_owner_flags &&
          after_stale.event_owner_epoch_high_water ==
              before_stale.event_owner_epoch_high_water &&
          after_stale.cancelled_through_transport_epoch ==
              before_stale.cancelled_through_transport_epoch &&
          status_after_stale.readiness_phase ==
              status_before_stale.readiness_phase &&
          status_after_stale.ack_carriers ==
              status_before_stale.ack_carriers &&
          status_after_stale.stale_cancelled_carriers ==
              status_before_stale.stale_cancelled_carriers + 2u &&
          fake_event_runtime.reset_calls == resets_before_stale &&
          fake_event_runtime.submit_calls == submits_before_stale);

    /* The floor is neither a wrong-direction/malformed envelope nor a
     * corrupt-packet bypass. */
    packet_t malformed_old{};
    malformed_old.count = old_mixed.output.application_bytes;
    std::memcpy(malformed_old.bytes.data(), old_mixed.candidate.data(),
                malformed_old.count);
    CHECK(receive_packet(malformed_old, output) ==
          NETCHAN_APP_RX_REJECT);
    CHECK(pilot_state().mode == 3u &&
          fake_event_runtime.reset_calls == resets_before_stale + 1u &&
          fake_event_runtime.submit_calls == submits_before_stale);

    auto corrupt_old = old_data;
    corrupt_old.bytes[4] ^= 0x40u;
    CHECK(receive_packet(corrupt_old, output) == NETCHAN_APP_RX_REJECT);
    CHECK(pilot_state().mode == 3u &&
          fake_event_runtime.reset_calls == resets_before_stale + 1u &&
          fake_event_runtime.submit_calls == submits_before_stale);
}

void test_event_repeat_requires_fresh_cgame_receipt()
{
    CHECK(begin_event_enabled(1900));
    confirm_capability(90);
    worr_native_readiness_state_v1 server{};
    const auto active = begin_handshake(server, 1901, 2900, false);
    complete_event_handshake(server, active);

    worr_event_stream_descriptor_v1 descriptor{};
    CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 9201, 1));
    const auto data = descriptor_packet(1901, 1, descriptor);
    netchan_app_rx_output_v1_t output{};
    CHECK(receive_packet(data, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().event_ack_receipts == 1u);

    fake_event_runtime.fail_status = true;
    CHECK(receive_packet(data, output) == NETCHAN_APP_RX_REJECT);
    const auto state = pilot_state();
    CHECK(state.mode == 3u);
    CHECK(state.event_ack_receipts == 0u);
    CHECK((state.event_owner_flags &
           WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC) != 0);
}

void test_snapshot_epoch_deferred_admission_ack_and_conflict_drain()
{
    constexpr uint32_t kCommandEpoch = 100;
    constexpr uint32_t kTransportEpoch = 2001;
    constexpr uint32_t kSnapshotEpoch = 5001;

    CHECK(begin_snapshot_enabled(2000));
    CHECK(pilot_status().private_mask ==
          kSnapshotPrivateCapabilities);
    confirm_capability(kCommandEpoch);
    worr_native_readiness_state_v1 server{};
    const auto active = begin_handshake(
        server, kTransportEpoch, 3000, false, kSnapshotEpoch);
    CHECK(fake_snapshot_shadow.bind_calls == 1u);
    CHECK(fake_snapshot_shadow.bound_epoch == kSnapshotEpoch);
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(server.phase ==
          WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM);
    complete_snapshot_handshake(
        server, active, kSnapshotEpoch);

    prepare_snapshot_projection(kSnapshotEpoch, 1);
    fake_snapshot_shadow.expectation_result =
        CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_PENDING;
    const auto packets = snapshot_packets(
        kTransportEpoch, 1);
    CHECK(!packets.empty());
    netchan_app_rx_output_v1_t output{};
    for (size_t delivery = 0; delivery < packets.size(); ++delivery) {
        const auto &packet =
            packets[packets.size() - delivery - 1u];
        CHECK(receive_packet(packet, output) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        CHECK(output.legacy_bytes == 0u);
    }
    auto state = pilot_state();
    CHECK(state.snapshot_rx_occupied == 1u);
    CHECK(state.snapshot_ack_receipts == 0u);
    CHECK(fake_snapshot_shadow.consume_calls == 0u);
    CHECK(!CL_NativeReadinessPilotOutputDue());

    fake_snapshot_shadow.expectation_result =
        CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE;
    CL_NativeReadinessPilotSnapshotExpectationReady();
    state = pilot_state();
    CHECK(state.mode == 2u);
    CHECK(state.snapshot_rx_occupied == 0u);
    CHECK(state.snapshot_ack_receipts == 1u);
    CHECK(fake_snapshot_shadow.consume_calls == 1u);
    CHECK(CL_NativeReadinessPilotOutputDue());

    build_command(kCommandEpoch, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    const auto mixed = prepare_tx(0);
    CHECK(mixed.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    worr_native_carrier_view_v1 mixed_view{};
    CHECK(Worr_NativeCarrierDecodeV1(
              mixed.candidate.data(),
              mixed.output.application_bytes, &mixed_view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(mixed_view.transport_epoch == kTransportEpoch);
    CHECK(mixed_view.entry_count == 2u);
    CHECK(mixed_view.entries[0].entry_type ==
          WORR_NATIVE_CARRIER_ENTRY_DATA_V1);
    CHECK(mixed_view.entries[1].entry_type ==
          WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
    CHECK(mixed_view.entries[1].first_message_sequence == 1u);
    CHECK(mixed_view.entries[1].last_message_sequence == 1u);
    complete_tx(mixed, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    CHECK(pilot_state().retained_messages == 1u);

    const auto command_ack =
        ack_packet(kTransportEpoch, 1, 1);
    CHECK(receive_packet(command_ack, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().retained_messages == 0u);

    /* A lost semantic receipt is rearmed by an exact committed DATA repeat
     * after fresh cgame receipt revalidation, without consuming twice. */
    CHECK(receive_packet(packets.front(), output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(fake_snapshot_shadow.consume_calls == 1u);
    CHECK(CL_NativeReadinessPilotOutputDue());

    prepare_snapshot_projection(kSnapshotEpoch, 2);
    CL_NativeReadinessPilotSnapshotExpectationReady();
    CHECK(pilot_state().mode == 2u);
    fake_snapshot_shadow.expectation.hashes.endpoint_hash ^= 1u;
    CL_NativeReadinessPilotSnapshotExpectationReady();
    state = pilot_state();
    CHECK(state.mode == 3u);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) != 0);
    /* Ownership is latched through diagnostic DRAIN.  CL_DeltaFrame will
     * therefore keep using COMPARE_LEGACY without DELIVER_CONSUMER instead
     * of switching the bound epoch back to legacy cgame publication. */
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
}

void test_snapshot_map_barrier_cancels_retained_receipt()
{
    constexpr uint32_t kOldTransportEpoch = 2101;
    constexpr uint32_t kNewTransportEpoch = 2102;
    constexpr uint32_t kOldSnapshotEpoch = 5101;
    constexpr uint32_t kNewSnapshotEpoch = 5102;

    CHECK(begin_snapshot_enabled(2100));
    confirm_capability(110);
    worr_native_readiness_state_v1 server{};
    auto active = begin_handshake(
        server, kOldTransportEpoch, 3100, false,
        kOldSnapshotEpoch);
    complete_snapshot_handshake(
        server, active, kOldSnapshotEpoch);

    prepare_snapshot_projection(kOldSnapshotEpoch, 1);
    const auto packets = snapshot_packets(kOldTransportEpoch, 1);
    netchan_app_rx_output_v1_t output{};
    for (const auto &packet : packets) {
        CHECK(receive_packet(packet, output) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }
    CHECK(pilot_state().snapshot_ack_receipts == 1u);

    CL_NativeReadinessPilotQuiesceMap();
    CL_NativeReadinessPilotServerDataReset();
    confirm_capability(111);
    ++com_localTime;
    active = begin_handshake(
        server, kNewTransportEpoch, 3101, true,
        kNewSnapshotEpoch);
    const auto state = pilot_state();
    CHECK(state.mode == 1u);
    CHECK(state.transport_epoch == 0u);
    CHECK(state.snapshot_epoch == 0u);
    CHECK(state.snapshot_ack_receipts == 0u);
    CHECK(state.cancelled_through_transport_epoch ==
          kOldTransportEpoch);
    CHECK(fake_snapshot_shadow.bound_epoch == kNewSnapshotEpoch);
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    complete_snapshot_handshake(
        server, active, kNewSnapshotEpoch);
}

void test_snapshot_hook_loss_preserves_timeline_until_boundary()
{
    constexpr uint32_t kTransportEpoch = 2151;
    constexpr uint32_t kSnapshotEpoch = 5151;

    CHECK(begin_snapshot_enabled(2150));
    confirm_capability(115);
    worr_native_readiness_state_v1 server{};
    const auto active = begin_handshake(
        server, kTransportEpoch, 3150, false, kSnapshotEpoch);
    complete_snapshot_handshake(
        server, active, kSnapshotEpoch);
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(pilot_status().hooks == 1u);

    /*
     * Simulate a foreign subsystem replacing one application hook after the
     * native epoch has bound.  The ownership query must fail transport closed
     * without allowing CL_DeltaFrame to resume legacy cgame publication.
     */
    cls.netchan.app_rx = occupied_rx;
    cls.netchan.app_rx_opaque = &reliable_storage;
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    const auto state = pilot_state();
    const auto status = pilot_status();
    CHECK(state.mode == 3u && !state.hooks_installed);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) != 0);
    CHECK(status.enabled == 1u && status.mode == 3u &&
          status.hooks == 0u && status.drains == 1u);
    CHECK(!cls.netchan.app_tx_prepare &&
          !cls.netchan.app_tx_completion &&
          !cls.netchan.app_tx_opaque);
    CHECK(cls.netchan.app_rx == occupied_rx &&
          cls.netchan.app_rx_opaque == &reliable_storage);

    /* Repeated authority checks are stable and do not disturb the foreign
     * hook or increment DRAIN telemetry again. */
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(pilot_status().drains == 1u);
    CHECK(cls.netchan.app_rx == occupied_rx &&
          cls.netchan.app_rx_opaque == &reliable_storage);

    CL_NativeReadinessPilotQuiesceMap();
    CHECK(!CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(pilot_status().enabled == 0u);
    CHECK(cls.netchan.app_rx == occupied_rx &&
          cls.netchan.app_rx_opaque == &reliable_storage);

    /* SERVERDATA is independently a map boundary; it must release the same
     * latch even when QuiesceMap was not called first. */
    CHECK(begin_snapshot_enabled(2160));
    confirm_capability(116);
    server = {};
    const auto reset_active = begin_handshake(
        server, kTransportEpoch + 1u, 3160, false,
        kSnapshotEpoch + 1u);
    complete_snapshot_handshake(
        server, reset_active, kSnapshotEpoch + 1u);
    cls.netchan.app_rx = occupied_rx;
    cls.netchan.app_rx_opaque = &reliable_storage;
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CL_NativeReadinessPilotServerDataReset();
    CHECK(!CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(pilot_status().enabled == 0u);
    CHECK(cls.netchan.app_rx == occupied_rx &&
          cls.netchan.app_rx_opaque == &reliable_storage);

    /* Only closure of the pilot's actual netchan is a connection boundary.
     * An unrelated close cannot release authority; the matching close does
     * and still leaves the foreign replacement hook untouched. */
    CHECK(begin_snapshot_enabled(2170));
    confirm_capability(117);
    server = {};
    const auto close_active = begin_handshake(
        server, kTransportEpoch + 2u, 3170, false,
        kSnapshotEpoch + 2u);
    complete_snapshot_handshake(
        server, close_active, kSnapshotEpoch + 2u);
    cls.netchan.app_rx = occupied_rx;
    cls.netchan.app_rx_opaque = &reliable_storage;
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    netchan_t unrelated{};
    unrelated.type = NETCHAN_NEW;
    CL_NativeReadinessPilotBeforeNetchanClose(&unrelated);
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(pilot_status().enabled == 1u &&
          pilot_status().mode == 3u);
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    CHECK(!CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(pilot_status().enabled == 0u);
    CHECK(cls.netchan.app_rx == occupied_rx &&
          cls.netchan.app_rx_opaque == &reliable_storage);
}

void test_snapshot_real_protocol_domains_and_expectation_window()
{
    struct domain_case_t {
        uint32_t max_entities;
        int protocol;
    };
    constexpr std::array<domain_case_t, 2> cases{{
        {MAX_EDICTS_OLD, PROTOCOL_VERSION_DEFAULT},
        {MAX_EDICTS, PROTOCOL_VERSION_RERELEASE},
    }};

    for (size_t case_index = 0; case_index < cases.size();
         ++case_index) {
        const auto &domain = cases[case_index];
        const uint32_t command_epoch =
            120u + static_cast<uint32_t>(case_index);
        const uint32_t transport_epoch =
            2201u + static_cast<uint32_t>(case_index);
        const uint32_t snapshot_epoch =
            5201u + static_cast<uint32_t>(case_index);
        const uint32_t raw_time =
            2200u + static_cast<uint32_t>(case_index) * 100u;

        CHECK(begin_snapshot_enabled(
            raw_time, reliable_storage.size(),
            domain.max_entities, domain.protocol));
        CHECK(cl.csr.max_edicts == domain.max_entities);
        CHECK(cls.serverProtocol == domain.protocol);
        confirm_capability(command_epoch);
        worr_native_readiness_state_v1 server{};
        const auto active = begin_handshake(
            server, transport_epoch,
            3200u + static_cast<uint32_t>(case_index),
            false, snapshot_epoch);
        complete_snapshot_handshake(
            server, active, snapshot_epoch);
        auto state = pilot_state();
        CHECK(state.mode == 2u);
        CHECK((state.snapshot_receiver_flags &
               WORR_NATIVE_SNAPSHOT_RECEIVER_INITIALIZED) != 0);
        CHECK((state.snapshot_receiver_flags &
               WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);

        if (domain.max_entities != MAX_EDICTS)
            continue;

        constexpr uint32_t expectation_total =
            WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY + 8u;
        for (uint32_t sequence = 1;
             sequence <= expectation_total; ++sequence) {
            prepare_snapshot_projection(snapshot_epoch, sequence);
            CL_NativeReadinessPilotSnapshotExpectationReady();
            state = pilot_state();
            CHECK(state.mode == 2u);
            CHECK((state.snapshot_receiver_flags &
                   WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);
            CHECK(state.snapshot_rx_occupied == 0u);
            CHECK(state.snapshot_ack_receipts == 0u);
            CHECK(fake_snapshot_shadow.consume_calls == 0u);
        }

        const auto packets =
            snapshot_packets(transport_epoch, 1);
        CHECK(!packets.empty());
        netchan_app_rx_output_v1_t output{};
        for (const auto &packet : packets) {
            CHECK(receive_packet(packet, output) ==
                  NETCHAN_APP_RX_EXPOSE_LEGACY);
        }
        state = pilot_state();
        CHECK(state.mode == 2u);
        CHECK(state.snapshot_rx_occupied == 0u);
        CHECK(state.snapshot_ack_receipts == 1u);
        CHECK(fake_snapshot_shadow.consume_calls == 1u);
    }
}

} // namespace

extern "C" bool CL_SnapshotShadowBindNativeEpoch(
    uint32_t snapshot_epoch)
{
    ++fake_snapshot_shadow.bind_calls;
    if (!fake_snapshot_shadow.bind_ok || snapshot_epoch == 0)
        return false;
    fake_snapshot_shadow.bound_epoch = snapshot_epoch;
    fake_snapshot_reset(
        nullptr, snapshot_epoch,
        WORR_CGAME_SNAPSHOT_RESET_CONNECTION, com_localTime);
    return true;
}

extern "C" bool CL_SnapshotShadowLatest(
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out,
    worr_snapshot_ref_v2 *ref_out)
{
    if (!view_out || !hashes_out || !ref_out ||
        !fake_snapshot_shadow.latest_available) {
        return false;
    }
    *view_out = fake_snapshot_shadow.view;
    *hashes_out = fake_snapshot_shadow.hashes;
    *ref_out = fake_snapshot_shadow.ref;
    return true;
}

extern "C"
cl_snapshot_shadow_native_expectation_result_v1
CL_SnapshotShadowGetNativeExpectation(
    worr_snapshot_id_v2 snapshot_id,
    worr_native_snapshot_expectation_v1 *expectation_out)
{
    if (expectation_out)
        *expectation_out = {};
    if (!expectation_out || snapshot_id.epoch == 0 ||
        snapshot_id.sequence == 0 ||
        snapshot_id.epoch != fake_snapshot_shadow.bound_epoch) {
        return CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_WRONG_EPOCH;
    }
    if (!fake_snapshot_shadow.latest_available)
        return CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_PENDING;
    const auto expected =
        fake_snapshot_shadow.expectation.snapshot_id;
    if (snapshot_id.epoch != expected.epoch ||
        snapshot_id.sequence != expected.sequence) {
        return snapshot_id.sequence > expected.sequence
            ? CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_PENDING
            : CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_STALE;
    }
    if (fake_snapshot_shadow.expectation_result ==
        CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE) {
        *expectation_out = fake_snapshot_shadow.expectation;
    }
    return fake_snapshot_shadow.expectation_result;
}

extern "C" bool CL_SnapshotShadowGetNativeConsumerV1(
    worr_native_snapshot_consumer_v1 *consumer_out)
{
    if (!consumer_out ||
        !fake_snapshot_shadow.consumer_available) {
        return false;
    }
    *consumer_out = {};
    consumer_out->struct_size = sizeof(*consumer_out);
    consumer_out->schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    consumer_out->opaque = &fake_snapshot_shadow;
    consumer_out->Reset = fake_snapshot_reset;
    consumer_out->ConsumeCanonicalSnapshot =
        fake_snapshot_consume;
    consumer_out->GetStatus = fake_snapshot_status;
    return true;
}

extern "C" cvar_t *Cvar_Get(const char *name, const char *, int)
{
    if (name && std::strcmp(name, "cl_worr_native_shadow_probe_hold") == 0)
        return &probe_hold_cvar;
    if (name &&
        std::strcmp(name, "cl_worr_native_snapshot_shadow") == 0) {
        return &snapshot_pilot_cvar;
    }
    if (name && std::strcmp(name, "cl_worr_native_event_shadow") == 0)
        return &event_pilot_cvar;
    return &pilot_cvar;
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
    SZ_Init(buffer, data, size, "pilot test");
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

int main()
{
    test_default_off_demo_and_hook_ownership();
    test_initial_exact_boundary_loss_retry_ack_release();
    test_repeated_stop_and_wait_loss_retry_ack_release();
    test_map_cancellation_barrier_epoch_switch_and_reconnect();
    test_pending_handshake_epoch_enters_cancellation_floor();
    test_no_carrier_malformed_wrong_direction_and_epoch();
    test_readiness_atomic_capacity_and_pretraffic_failure();
    test_event_opt_in_semantic_admission_and_mixed_egress();
    test_event_ack_exhaustion_cancellation_and_stale_floor();
    test_event_repeat_requires_fresh_cgame_receipt();
    test_snapshot_epoch_deferred_admission_ack_and_conflict_drain();
    test_snapshot_map_barrier_cancels_retained_receipt();
    test_snapshot_hook_loss_preserves_timeline_until_boundary();
    test_snapshot_real_protocol_domains_and_expectation_window();
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    std::puts("native_client_readiness_pilot_test: ok");
    return EXIT_SUCCESS;
}
