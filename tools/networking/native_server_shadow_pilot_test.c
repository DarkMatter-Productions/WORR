/* Standalone FR-10-T04 server readiness pilot tests. */

#include "server/native_shadow.h"

#include "common/intreadwrite.h"
#include "common/net/legacy_entity_event_candidate.h"
#include "common/net/native_event_batch.h"
#include "common/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(stderr, "native_server_shadow_pilot_test:%d: %s\n",   \
                    __LINE__, #expression);                                 \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    } while (0)

/* Focused hook-registration stubs keep this adapter test independent of the
 * complete engine/netchan link. */
bool Netchan_SetApplicationTxHook(netchan_t *chan,
                                  netchan_app_tx_prepare_fn prepare,
                                  netchan_app_tx_completion_fn completion,
                                  void *opaque)
{
    if (chan == NULL || chan->type != NETCHAN_NEW ||
        (!!prepare != !!completion)) {
        return false;
    }
    chan->app_tx_prepare = prepare;
    chan->app_tx_completion = completion;
    chan->app_tx_opaque = prepare != NULL ? opaque : NULL;
    return true;
}

bool Netchan_SetApplicationRxHook(netchan_t *chan,
                                  netchan_app_rx_fn receive,
                                  void *opaque)
{
    if (chan == NULL || chan->type != NETCHAN_NEW)
        return false;
    chan->app_rx = receive;
    chan->app_rx_opaque = receive != NULL ? opaque : NULL;
    return true;
}

static void init_chan(netchan_t *chan)
{
    memset(chan, 0, sizeof(*chan));
    chan->type = NETCHAN_NEW;
}

static uint32_t native_public_mask(
    const sv_native_shadow_peer_v1 *peer)
{
    CHECK(peer != NULL);
    return SV_NativeShadowModePublicCapabilitiesV1(peer->mode);
}

static void init_write_buffer(sizebuf_t *buffer, byte *data,
                              uint32_t capacity, uint32_t cursize)
{
    CHECK(buffer != NULL && data != NULL && cursize <= capacity);
    memset(buffer, 0, sizeof(*buffer));
    buffer->data = data;
    buffer->maxsize = capacity;
    buffer->cursize = cursize;
}

static uint32_t read_u32_le(const byte *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void make_client_ready(
    const worr_native_readiness_record_v1 *challenge,
    uint64_t now_tick,
    worr_native_readiness_record_v1 *client_ready)
{
    worr_native_readiness_state_v1 client;

    CHECK(Worr_NativeReadinessClientInitBoundV1(
              &client, challenge->transport_epoch,
              challenge->snapshot_epoch,
              challenge->negotiated_capabilities, now_tick,
              SV_NATIVE_SHADOW_TIMEOUT_MS) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, challenge, now_tick, client_ready) ==
          WORR_NATIVE_READINESS_OK);
}

static void feed_ready(
    sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms,
    const worr_native_readiness_record_v1 *client_ready,
    worr_native_readiness_record_v1 *server_active)
{
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    uint32_t index;

    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        client_ready, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    CHECK(SV_NativeShadowPacketBeginV1(peer, raw_time_ms));
    for (index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        const sv_native_shadow_observe_result_v1 result =
            SV_NativeShadowObserveSettingV1(
                peer, pairs[index].index, pairs[index].value,
                server_active);
        CHECK(result ==
              (index + 1 == WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT
                   ? SV_NATIVE_SHADOW_OBSERVE_SERVER_ACTIVE_READY
                   : SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED));
    }
    CHECK(SV_NativeShadowPacketEndV1(peer));
}

static void feed_active_confirm(
    sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms,
    const worr_native_readiness_record_v1 *active_confirm)
{
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    worr_native_readiness_record_v1 untouched;
    uint32_t index;

    memset(&untouched, 0xa5, sizeof(untouched));
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        active_confirm, pairs,
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    CHECK(SV_NativeShadowPacketBeginV1(peer, raw_time_ms));
    for (index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        const sv_native_shadow_observe_result_v1 result =
            SV_NativeShadowObserveSettingV1(
                peer, pairs[index].index, pairs[index].value,
                &untouched);
        CHECK(result ==
              (index + 1 == WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT
                   ? SV_NATIVE_SHADOW_OBSERVE_CLIENT_ACTIVE_CONFIRMED
                   : SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED));
    }
    CHECK(SV_NativeShadowPacketEndV1(peer));
}

static void begin_event_peer_wait_confirm(
    sv_native_shadow_peer_v1 *peer,
    netchan_t *chan,
    uint32_t official_epoch,
    uint32_t raw_time_ms,
    worr_native_readiness_state_v1 *client_out,
    worr_native_readiness_record_v1 *challenge_out,
    worr_native_readiness_record_v1 *active_confirm_out)
{
    worr_native_readiness_state_v1 client;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    sv_native_shadow_event_status_v1 status;

    init_chan(chan);
    CHECK(SV_NativeShadowPeerInitModeV1(
        peer, chan, raw_time_ms, SV_NATIVE_SHADOW_MODE_EVENT));
    CHECK(peer->event_state != NULL);
    CHECK(SV_NativeShadowBeginEpochV1(
        peer, official_epoch, native_public_mask(peer),
        native_public_mask(peer), raw_time_ms, &challenge));
    CHECK(challenge.negotiated_capabilities ==
          WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK);
    CHECK(Worr_NativeReadinessClientInitV1(
              &client, challenge.transport_epoch,
              challenge.negotiated_capabilities, peer->clock_ticks,
              SV_NATIVE_SHADOW_TIMEOUT_MS) == WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &challenge, peer->clock_ticks, &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    feed_ready(peer, raw_time_ms + 1u, &client_ready, &server_active);
    CHECK(peer->readiness.phase ==
          WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM);
    CHECK(SV_NativeShadowServerActiveQueuedV1(peer));
    CHECK(peer->transport_initialized == 1 &&
          peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE);
    CHECK(SV_NativeShadowGetEventStatusV1(
        peer, raw_time_ms + 1u, &status));
    CHECK(status.mode == SV_NATIVE_SHADOW_MODE_EVENT &&
          status.sender_initialized == 1 && status.tx_open == 0 &&
          status.descriptor_acked == 0 && status.retained_count == 1 &&
          status.stream_epoch != 0 && status.output_due == 0);
    CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &client, &server_active, peer->clock_ticks,
              active_confirm_out) == WORR_NATIVE_READINESS_OK);
    if (client_out != NULL)
        *client_out = client;
    if (challenge_out != NULL)
        *challenge_out = challenge;
}

typedef struct command_carrier_fixture_s {
    worr_command_record_v1 record;
    worr_native_record_ref_v1 record_ref;
    byte payload[SV_NATIVE_SHADOW_PAYLOAD_BYTES];
    byte envelope[SV_NATIVE_SHADOW_WNE_BYTES];
    byte packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes;
} command_carrier_fixture;

typedef struct input_batch_carrier_fixture_s {
    worr_command_record_v1 records[WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS];
    worr_native_input_batch_info_v1 info;
    worr_native_record_ref_v1 record_ref;
    byte payload[WORR_NATIVE_INPUT_BATCH_MAX_PAYLOAD_BYTES];
    byte envelope[WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES +
                  WORR_NATIVE_INPUT_BATCH_MAX_PAYLOAD_BYTES];
    byte packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t payload_bytes;
    size_t envelope_bytes;
    size_t packet_bytes;
} input_batch_carrier_fixture;

static worr_prediction_command_v1 make_prediction_command(uint8_t marker)
{
    worr_prediction_command_v1 command;

    memset(&command, 0, sizeof(command));
    command.struct_size = sizeof(command);
    command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.duration_ms = 10;
    command.buttons = marker;
    command.view_angles[0] = (float)marker;
    command.forward_move = (float)(int8_t)marker;
    command.side_move = -(float)(int8_t)marker;
    return command;
}

static worr_command_record_v1 make_native_record(uint32_t epoch,
                                                  uint32_t sequence,
                                                  uint8_t marker)
{
    worr_command_record_v1 record;

    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id = (worr_command_id_v1){epoch, sequence};
    record.sample_time_us = (uint64_t)sequence * UINT64_C(10000);
    record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    record.command = make_prediction_command(marker);
    record.render_watermark.struct_size =
        sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    record.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_NONE;
    CHECK(Worr_CommandRecordCanonicalizeV1(
        &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    return record;
}

static worr_event_record_v1 make_event_candidate(
    uint32_t tick, uint32_t entity_index, uint8_t raw_event)
{
    worr_event_entity_ref_v1 source_entity;
    worr_event_record_v1 candidate;
    uint64_t semantic_hash;

    source_entity.index = entity_index;
    source_entity.generation = tick + 1u;
    CHECK(Worr_LegacyEntityEventCandidateBuildV1(
        tick, (uint64_t)tick * UINT64_C(1000), entity_index,
        source_entity, raw_event, MAX_EDICTS, &candidate,
        &semantic_hash));
    return candidate;
}

enum {
    SNAPSHOT_TEST_MAX_ENTITIES = 128,
    SNAPSHOT_TEST_ENTITY_COUNT = 48,
    SNAPSHOT_TEST_SLOT_CAPACITY = 2,
    SNAPSHOT_TEST_ENTITIES_PER_SLOT = 64,
    SNAPSHOT_TEST_AREA_BYTES_PER_SLOT = 8,
};

typedef struct snapshot_frame_fixture_s {
    q2proto_svc_frame_t frame;
    q2proto_svc_frame_entity_delta_t
        deltas[SNAPSHOT_TEST_ENTITY_COUNT + 1];
    byte area[4];
} snapshot_frame_fixture;

static sv_snapshot_shadow_config_v1 make_snapshot_shadow_config(
    uint32_t snapshot_epoch)
{
    sv_snapshot_shadow_config_v1 config;

    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    config.snapshot_epoch = snapshot_epoch;
    config.max_entities = SNAPSHOT_TEST_MAX_ENTITIES;
    config.max_models = 64;
    config.max_sounds = 64;
    config.slot_capacity = SNAPSHOT_TEST_SLOT_CAPACITY;
    config.entities_per_slot = SNAPSHOT_TEST_ENTITIES_PER_SLOT;
    config.area_bytes_per_slot = SNAPSHOT_TEST_AREA_BYTES_PER_SLOT;
    config.beam_renderfx_mask = UINT32_C(1) << 7;
    config.legacy_renderfx_allowed_mask =
        (UINT32_C(1) << 19) - 1u;
    config.legacy_beam_clear_mask = UINT32_C(1) << 9;
    config.extended_entity_state = 1;
    return config;
}

static void init_snapshot_frame(snapshot_frame_fixture *fixture,
                                int32_t wire_snapshot_number)
{
    uint32_t index;

    CHECK(fixture != NULL);
    memset(fixture, 0, sizeof(*fixture));
    fixture->area[0] = 1;
    fixture->area[1] = 2;
    fixture->area[2] = 3;
    fixture->area[3] = 4;
    fixture->frame.serverframe = wire_snapshot_number;
    fixture->frame.deltaframe = -1;
    fixture->frame.areabits_len = sizeof(fixture->area);
    fixture->frame.areabits = fixture->area;
    fixture->frame.playerstate.delta_bits =
        Q2P_PSD_PM_GRAVITY | Q2P_PSD_FOV;
    fixture->frame.playerstate.pm_gravity = 800;
    fixture->frame.playerstate.fov = 100;
    for (index = 0; index < SNAPSHOT_TEST_ENTITY_COUNT; ++index) {
        q2proto_svc_frame_entity_delta_t *delta =
            &fixture->deltas[index];

        delta->newnum = (uint16_t)(index + 1u);
        delta->entity_delta.delta_bits =
            Q2P_ESD_MODELINDEX | Q2P_ESD_FRAME;
        delta->entity_delta.modelindex =
            (uint16_t)((index % 8u) + 1u);
        delta->entity_delta.frame =
            (uint16_t)(index + 1u);
    }
    /* The final zero entity is the required end-of-frame sentinel. */
    fixture->deltas[SNAPSHOT_TEST_ENTITY_COUNT].newnum = 0;
}

static sv_snapshot_shadow_ref_v1 commit_snapshot_frame(
    sv_snapshot_shadow_peer_v1 *shadow,
    snapshot_frame_fixture *fixture,
    uint32_t server_tick,
    uint64_t server_time_us)
{
    sv_snapshot_shadow_frame_v1 input;
    sv_snapshot_shadow_ref_v1 ref;
    uint32_t index;

    CHECK(shadow != NULL && fixture != NULL);
    memset(&input, 0, sizeof(input));
    input.struct_size = sizeof(input);
    input.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    input.wire_frame = &fixture->frame;
    input.authoritative_server_tick = server_tick;
    input.authoritative_tick_delta = 0;
    input.authoritative_server_time_us = server_time_us;
    input.controlled_entity_index = 1;
    CHECK(SV_SnapshotShadowBeginFrameV1(shadow, &input) ==
          SV_SNAPSHOT_SHADOW_OK);
    for (index = 0; index <= SNAPSHOT_TEST_ENTITY_COUNT; ++index) {
        CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(
                  shadow, &fixture->deltas[index]) ==
              SV_SNAPSHOT_SHADOW_OK);
    }
    memset(&ref, 0, sizeof(ref));
    CHECK(SV_SnapshotShadowCommitFrameV1(shadow, &ref) ==
          SV_SNAPSHOT_SHADOW_OK);
    return ref;
}

static void make_raw_keyed_poi(
    byte raw[WORR_LEGACY_KEYED_POI_RAW_SIZE], uint16_t key,
    uint16_t lifetime_ms, uint16_t image_index, uint8_t color_index)
{
    uint32_t float_bits;

    memset(raw, 0, WORR_LEGACY_KEYED_POI_RAW_SIZE);
    raw[0] = svc_poi;
    WL16(&raw[1], key);
    WL16(&raw[3], lifetime_ms);
    memcpy(&float_bits, &(float){12.5f}, sizeof(float_bits));
    WL32(&raw[5], float_bits);
    memcpy(&float_bits, &(float){-24.25f}, sizeof(float_bits));
    WL32(&raw[9], float_bits);
    memcpy(&float_bits, &(float){96.0f}, sizeof(float_bits));
    WL32(&raw[13], float_bits);
    WL16(&raw[17], image_index);
    raw[19] = color_index;
    raw[20] = WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM;
}

static worr_command_record_v1 make_legacy_record(
    const worr_command_record_v1 *native_record)
{
    worr_command_record_v1 record = *native_record;

    memset(&record.render_watermark, 0,
           sizeof(record.render_watermark));
    record.render_watermark.struct_size =
        sizeof(record.render_watermark);
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

static void build_command_carrier(
    command_carrier_fixture *fixture,
    uint32_t transport_epoch,
    uint32_t command_epoch,
    uint32_t command_sequence,
    uint32_t message_sequence,
    uint8_t marker,
    const byte *legacy,
    size_t legacy_bytes)
{
    worr_native_envelope_fragmenter_v1 fragmenter;
    worr_native_carrier_entry_v1 entry;
    size_t payload_bytes = 0;
    size_t envelope_bytes = 0;

    CHECK(fixture != NULL);
    memset(fixture, 0, sizeof(*fixture));
    fixture->record = make_native_record(
        command_epoch, command_sequence, marker);
    fixture->record_ref.record_class = WORR_NATIVE_RECORD_COMMAND_V1;
    fixture->record_ref.record_schema_version = WORR_COMMAND_ABI_VERSION;
    fixture->record_ref.object_epoch = command_epoch;
    fixture->record_ref.object_sequence = command_sequence;
    CHECK(Worr_NativeCodecCommandEncodeV1(
              &fixture->record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
              fixture->payload, sizeof(fixture->payload), &payload_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(payload_bytes == SV_NATIVE_SHADOW_PAYLOAD_BYTES);
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, transport_epoch, message_sequence,
        fixture->record_ref, 0, fixture->payload,
        (uint32_t)payload_bytes, SV_NATIVE_SHADOW_WNE_BYTES));
    CHECK(fragmenter.fragment_count == 1);
    CHECK(Worr_NativeEnvelopeFragmentNextV1(
              &fragmenter, fixture->payload, (uint32_t)payload_bytes,
              fixture->envelope, sizeof(fixture->envelope),
              &envelope_bytes) == WORR_NATIVE_ENVELOPE_EMIT_OK);
    CHECK(envelope_bytes == SV_NATIVE_SHADOW_WNE_BYTES);

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entry.data_bytes = (uint32_t)envelope_bytes;
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, legacy, legacy_bytes,
              fixture->envelope, envelope_bytes, &entry, 1,
              fixture->packet, sizeof(fixture->packet),
              &fixture->packet_bytes) == WORR_NATIVE_CARRIER_OK);
}

static uint32_t input_batch_crc32_update(
    uint32_t crc, const byte *bytes, size_t count)
{
    size_t index;
    for (index = 0; index < count; ++index) {
        unsigned bit;
        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^
                  (UINT32_C(0xedb88320) &
                   (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return crc;
}

static uint32_t input_batch_payload_crc32(
    const byte *bytes, size_t count)
{
    enum { CRC_OFFSET = 28 };
    static const byte zero_crc[4];
    uint32_t crc = UINT32_MAX;
    crc = input_batch_crc32_update(crc, bytes, CRC_OFFSET);
    crc = input_batch_crc32_update(crc, zero_crc, sizeof(zero_crc));
    crc = input_batch_crc32_update(
        crc, bytes + CRC_OFFSET + sizeof(uint32_t),
        count - CRC_OFFSET - sizeof(uint32_t));
    return ~crc;
}

static void input_batch_write_u32_le(byte *bytes, uint32_t value)
{
    bytes[0] = (byte)value;
    bytes[1] = (byte)(value >> 8);
    bytes[2] = (byte)(value >> 16);
    bytes[3] = (byte)(value >> 24);
}

static void rebuild_input_batch_carrier(
    input_batch_carrier_fixture *fixture,
    uint32_t transport_epoch,
    uint32_t message_sequence,
    const byte *legacy,
    size_t legacy_bytes,
    bool include_ack)
{
    worr_native_envelope_fragmenter_v1 fragmenter;
    worr_native_carrier_entry_v1 entries[2];
    const uint16_t entry_count = include_ack ? 2u : 1u;

    fixture->record_ref.record_class = WORR_NATIVE_RECORD_COMMAND_V1;
    fixture->record_ref.record_schema_version =
        WORR_NATIVE_INPUT_BATCH_RECORD_SCHEMA;
    fixture->record_ref.object_epoch = fixture->info.command_epoch;
    fixture->record_ref.object_sequence = fixture->info.last_sequence;
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, transport_epoch, message_sequence,
        fixture->record_ref, 0, fixture->payload,
        (uint32_t)fixture->payload_bytes,
        (uint32_t)(WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES +
                   fixture->payload_bytes)));
    CHECK(fragmenter.fragment_count == 1);
    fixture->envelope_bytes = 0;
    CHECK(Worr_NativeEnvelopeFragmentNextV1(
              &fragmenter, fixture->payload,
              (uint32_t)fixture->payload_bytes,
              fixture->envelope, sizeof(fixture->envelope),
              &fixture->envelope_bytes) == WORR_NATIVE_ENVELOPE_EMIT_OK);
    CHECK(fixture->envelope_bytes ==
          WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES + fixture->payload_bytes);

    memset(entries, 0, sizeof(entries));
    entries[0].struct_size = sizeof(entries[0]);
    entries[0].schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entries[0].entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entries[0].data_bytes = (uint32_t)fixture->envelope_bytes;
    if (include_ack) {
        entries[1].struct_size = sizeof(entries[1]);
        entries[1].schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
        entries[1].entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
        entries[1].first_message_sequence = 1;
        entries[1].last_message_sequence = 1;
    }
    fixture->packet_bytes = 0;
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, legacy, legacy_bytes,
              fixture->envelope, fixture->envelope_bytes,
              entries, entry_count, fixture->packet,
              sizeof(fixture->packet), &fixture->packet_bytes) ==
          WORR_NATIVE_CARRIER_OK);
}

static void build_input_batch_carrier(
    input_batch_carrier_fixture *fixture,
    uint32_t transport_epoch,
    uint32_t command_epoch,
    uint32_t first_command_sequence,
    uint32_t command_count,
    uint32_t message_sequence,
    uint8_t marker,
    const byte *legacy,
    size_t legacy_bytes,
    bool include_ack)
{
    uint32_t index;

    CHECK(fixture != NULL &&
          command_count >= WORR_NATIVE_INPUT_BATCH_MIN_COMMANDS &&
          command_count <= WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS);
    memset(fixture, 0, sizeof(*fixture));
    for (index = 0; index < command_count; ++index) {
        fixture->records[index] = make_native_record(
            command_epoch, first_command_sequence + index,
            (uint8_t)(marker + index));
    }
    CHECK(Worr_NativeInputBatchEncodeV1(
              fixture->records, command_count,
              WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
              fixture->payload, sizeof(fixture->payload),
              &fixture->payload_bytes, &fixture->info) ==
          WORR_NATIVE_INPUT_BATCH_OK);
    rebuild_input_batch_carrier(
        fixture, transport_epoch, message_sequence, legacy,
        legacy_bytes, include_ack);
}

static void make_input_batch_noncontiguous(
    input_batch_carrier_fixture *fixture,
    uint32_t transport_epoch,
    uint32_t message_sequence,
    const byte *legacy,
    size_t legacy_bytes)
{
    enum {
        BATCH_CRC_OFFSET = 28,
        NESTED_OBJECT_SEQUENCE_OFFSET = 40,
    };
    const size_t second_sequence_offset =
        WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES +
        WORR_NATIVE_INPUT_BATCH_COMMAND_BYTES +
        NESTED_OBJECT_SEQUENCE_OFFSET;

    CHECK(fixture != NULL && fixture->info.command_count >= 2);
    input_batch_write_u32_le(
        fixture->payload + second_sequence_offset,
        fixture->info.first_sequence + 2u);
    input_batch_write_u32_le(
        fixture->payload + BATCH_CRC_OFFSET,
        input_batch_payload_crc32(
            fixture->payload, fixture->payload_bytes));
    CHECK(Worr_NativeInputBatchInspectV1(
              fixture->payload, fixture->payload_bytes,
              &fixture->info) == WORR_NATIVE_INPUT_BATCH_OK);
    rebuild_input_batch_carrier(
        fixture, transport_epoch, message_sequence, legacy,
        legacy_bytes, false);
}

static void corrupt_input_batch_payload_crc(
    input_batch_carrier_fixture *fixture,
    uint32_t transport_epoch,
    uint32_t message_sequence,
    const byte *legacy,
    size_t legacy_bytes,
    bool include_ack)
{
    CHECK(fixture != NULL && fixture->payload_bytes != 0);
    fixture->payload[fixture->payload_bytes - 1u] ^= 1u;
    rebuild_input_batch_carrier(
        fixture, transport_epoch, message_sequence, legacy,
        legacy_bytes, include_ack);
}

static void activate_input_batch_peer(
    sv_native_shadow_peer_v1 *peer,
    netchan_t *chan,
    uint32_t official_epoch,
    uint32_t raw_time_ms,
    bool confirmation_capacity,
    worr_native_readiness_record_v1 *challenge_out)
{
    enum {
        READINESS_BYTES = SV_NATIVE_SHADOW_SVC_WIRE_BYTES,
        COMBINED_BYTES = SV_NATIVE_SHADOW_SVC_WIRE_BYTES +
                         WORR_NATIVE_INPUT_BATCH_SIDEBAND_WIRE_BYTES,
    };
    byte message_data[COMBINED_BYTES];
    byte reliable_data[COMBINED_BYTES];
    sizebuf_t message;
    sizebuf_t reliable;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    const uint32_t capacity = confirmation_capacity
        ? COMBINED_BYTES : READINESS_BYTES;

    init_chan(chan);
    CHECK(SV_NativeShadowPeerInitV1(peer, chan, raw_time_ms));
    CHECK(SV_NativeShadowConfigureInputBatchV1(peer, true));
    CHECK(SV_NativeShadowBeginEpochV1(
        peer, official_epoch, native_public_mask(peer),
        native_public_mask(peer), raw_time_ms, &challenge));
    make_client_ready(&challenge, peer->clock_ticks, &client_ready);
    feed_ready(peer, raw_time_ms + 1u, &client_ready, &server_active);
    memset(message_data, 0xa5, sizeof(message_data));
    memset(reliable_data, 0x5a, sizeof(reliable_data));
    init_write_buffer(&message, message_data, capacity, 0);
    init_write_buffer(&reliable, reliable_data, capacity, 0);
    CHECK(SV_NativeShadowAppendServerActiveV1(
        peer, &message, &reliable, svc_q2pro_setting,
        &server_active));
    CHECK(message.cursize == capacity);
    if (confirmation_capacity) {
        CHECK(peer->input_batch_confirm_pending == 1 &&
              peer->input_batch_declined == 0);
    } else {
        CHECK(peer->input_batch_confirm_pending == 0 &&
              peer->input_batch_declined == 1);
    }
    CHECK(SV_NativeShadowServerActiveQueuedV1(peer));
    CHECK(peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          peer->transport_initialized == 1);
    CHECK(peer->input_batch_confirmed ==
          (confirmation_capacity ? 1u : 0u));
    if (challenge_out != NULL)
        *challenge_out = challenge;
}

static void activate_peer(sv_native_shadow_peer_v1 *peer,
                          netchan_t *chan,
                          uint32_t official_epoch,
                          uint32_t raw_time_ms,
                          worr_native_readiness_record_v1 *challenge_out)
{
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;

    init_chan(chan);
    CHECK(SV_NativeShadowPeerInitV1(peer, chan, raw_time_ms));
    CHECK(SV_NativeShadowBeginEpochV1(
        peer, official_epoch, native_public_mask(peer),
        native_public_mask(peer), raw_time_ms, &challenge));
    make_client_ready(&challenge, peer->clock_ticks, &client_ready);
    feed_ready(peer, raw_time_ms + 1u, &client_ready, &server_active);
    CHECK(peer->activation_pending == 1 &&
          peer->transport_initialized == 0);
    CHECK(SV_NativeShadowServerActiveQueuedV1(peer));
    CHECK(peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          peer->transport_initialized == 1);
    if (challenge_out != NULL)
        *challenge_out = challenge;
}

static netchan_app_rx_result_t receive_application(
    netchan_t *chan,
    sv_native_shadow_peer_v1 *peer,
    const byte *application,
    size_t application_bytes,
    netchan_app_rx_output_v1_t *output)
{
    netchan_app_rx_info_v1_t info;

    CHECK(application_bytes <= UINT32_MAX);
    memset(&info, 0, sizeof(info));
    info.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.incoming_sequence = 1;
    info.message_bytes = (uint32_t)application_bytes;
    info.application_bytes = (uint32_t)application_bytes;
    memset(output, 0xa5, sizeof(*output));
    return chan->app_rx(peer, &info, application, output);
}

static void decode_single_data_frame(
    const byte *packet,
    size_t packet_bytes,
    worr_native_envelope_frame_info_v1 *frame_out)
{
    worr_native_carrier_view_v1 view;
    uint16_t index;
    uint16_t data_count = 0;

    CHECK(Worr_NativeCarrierDecodeV1(
              packet, packet_bytes, &view) == WORR_NATIVE_CARRIER_OK);
    for (index = 0; index < view.entry_count; ++index) {
        if (view.entries[index].entry_type ==
            WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
            ++data_count;
            CHECK(Worr_NativeEnvelopeDecodeV1(
                      packet + view.entries[index].data_offset,
                      view.entries[index].data_bytes, frame_out) ==
                  WORR_NATIVE_ENVELOPE_DECODE_OK);
        }
    }
    CHECK(data_count == 1);
}

static const byte *decode_single_data_payload(
    const byte *packet,
    size_t packet_bytes,
    worr_native_envelope_frame_info_v1 *frame_out,
    size_t *payload_bytes_out)
{
    worr_native_carrier_view_v1 view;
    const byte *payload = NULL;
    uint16_t index;
    uint16_t data_count = 0;

    CHECK(packet != NULL && frame_out != NULL && payload_bytes_out != NULL);
    CHECK(Worr_NativeCarrierDecodeV1(
              packet, packet_bytes, &view) == WORR_NATIVE_CARRIER_OK);
    for (index = 0; index < view.entry_count; ++index) {
        const byte *envelope;

        if (view.entries[index].entry_type !=
            WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
            continue;
        }
        ++data_count;
        envelope = packet + view.entries[index].data_offset;
        CHECK(Worr_NativeEnvelopeDecodeV1(
                  envelope, view.entries[index].data_bytes, frame_out) ==
              WORR_NATIVE_ENVELOPE_DECODE_OK);
        CHECK(frame_out->fragment_count == 1 &&
              frame_out->fragment_index == 0 &&
              frame_out->fragment_offset == 0 &&
              frame_out->fragment_payload_bytes ==
                  frame_out->total_payload_bytes);
        payload = envelope + frame_out->payload_offset;
        *payload_bytes_out = frame_out->fragment_payload_bytes;
    }
    CHECK(data_count == 1 && payload != NULL);
    return payload;
}

static netchan_app_tx_prepare_result_t prepare_application(
    netchan_t *chan,
    sv_native_shadow_peer_v1 *peer,
    const byte *legacy,
    size_t legacy_bytes,
    byte *candidate,
    uint32_t candidate_capacity,
    netchan_app_tx_prepare_output_v1_t *output)
{
    netchan_app_tx_prepare_info_v1_t info;

    CHECK(legacy_bytes <= candidate_capacity);
    memset(&info, 0, sizeof(info));
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.outgoing_sequence = 1;
    info.max_application_bytes = candidate_capacity;
    info.unreliable_bytes = (uint32_t)legacy_bytes;
    info.legacy_application_bytes = (uint32_t)legacy_bytes;
    info.packet_copies = 1;
    memset(output, 0xa5, sizeof(*output));
    return chan->app_tx_prepare(
        peer, &info, legacy, candidate, output);
}

static void complete_application(
    netchan_t *chan,
    sv_native_shadow_peer_v1 *peer,
    const netchan_app_tx_prepare_output_v1_t *prepared,
    netchan_app_tx_completion_result_t result,
    const byte *application)
{
    netchan_app_tx_completion_info_v1_t info;

    memset(&info, 0, sizeof(info));
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.result = result;
    info.packet_copies = 1;
    info.accepted_copies =
        result == NETCHAN_APP_TX_COMPLETION_ACCEPTED ? 1u : 0u;
    info.application_bytes = prepared->application_bytes;
    info.token = prepared->token;
    chan->app_tx_completion(peer, &info, application);
}

static void test_hooks_and_handshake(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_record_v1 duplicate_active;
    netchan_app_tx_prepare_info_v1_t tx_info;
    netchan_app_tx_prepare_output_v1_t tx_output;
    netchan_app_tx_prepare_output_v1_t tx_output_before;
    netchan_app_rx_info_v1_t rx_info;
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_rx_output_v1_t rx_output_before;
    byte legacy[1] = {0};
    byte candidate[1] = {0xa5};
    const byte candidate_before = candidate[0];

    init_chan(&chan);
    CHECK(SV_NativeShadowPeerInitV1(&peer, &chan, 100));
    CHECK(peer.connection_owner_id != 0 &&
          peer.mode == SV_NATIVE_SHADOW_MODE_COMMAND &&
          peer.event_state == NULL);
    CHECK(chan.app_tx_prepare != NULL && chan.app_rx != NULL);

    memset(&tx_info, 0, sizeof(tx_info));
    memset(&tx_output, 0xa5, sizeof(tx_output));
    memcpy(&tx_output_before, &tx_output, sizeof(tx_output_before));
    CHECK(chan.app_tx_prepare(
              &peer, &tx_info, legacy, candidate, &tx_output) ==
          NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(!memcmp(&tx_output, &tx_output_before, sizeof(tx_output)));
    CHECK(candidate[0] == candidate_before);
    memset(&rx_info, 0, sizeof(rx_info));
    memset(&rx_output, 0xa5, sizeof(rx_output));
    memcpy(&rx_output_before, &rx_output, sizeof(rx_output_before));
    CHECK(chan.app_rx(&peer, &rx_info, legacy, &rx_output) ==
          NETCHAN_APP_RX_BYPASS);
    CHECK(!memcmp(&rx_output, &rx_output_before, sizeof(rx_output)));
    CHECK(peer.tx_bypass_calls == 1 && peer.rx_bypass_calls == 1);

    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 7, native_public_mask(&peer),
        native_public_mask(&peer), 100, &challenge));
    CHECK(challenge.negotiated_capabilities ==
          WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK);
    make_client_ready(&challenge, peer.clock_ticks, &client_ready);
    feed_ready(&peer, 101, &client_ready, &server_active);
    CHECK(server_active.record_kind ==
          WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE);
    CHECK(peer.readiness.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    CHECK(peer.activation_pending == 1 &&
          peer.transport_initialized == 0 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS);
    CHECK(SV_NativeShadowServerActiveQueuedV1(&peer));
    CHECK(peer.activation_pending == 0 && peer.transport_initialized == 1 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE);

    feed_ready(&peer, 102, &client_ready, &duplicate_active);
    CHECK(!memcmp(&server_active, &duplicate_active,
                  sizeof(server_active)));
    CHECK(peer.duplicate_client_ready_records == 1);
    CHECK(peer.activation_pending == 0);
    CHECK(SV_NativeShadowServerActiveQueuedV1(&peer));

    SV_NativeShadowPeerDetachV1(&peer);
    CHECK(chan.app_tx_prepare == NULL && chan.app_rx == NULL);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_post_bootstrap_queue_idle_gate(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;

    init_chan(&chan);
    CHECK(SV_NativeShadowPeerInitV1(&peer, &chan, 25));
    CHECK(SV_NativeShadowPostBootstrapQueueIdleV1(&peer));
    CHECK(SV_NativeShadowReliableGenerationIdleV1(&peer));

    chan.message.cursize = 1;
    CHECK(!SV_NativeShadowPostBootstrapQueueIdleV1(&peer));
    /* Later reliable bytes are allowed behind the immutable Begin barrier;
     * they do not make the previously handed-off generation busy. */
    CHECK(SV_NativeShadowReliableGenerationIdleV1(&peer));
    chan.message.cursize = 0;
    chan.message.overflowed = true;
    CHECK(!SV_NativeShadowPostBootstrapQueueIdleV1(&peer));
    CHECK(!SV_NativeShadowReliableGenerationIdleV1(&peer));
    chan.message.overflowed = false;
    chan.reliable_length = 1;
    CHECK(!SV_NativeShadowPostBootstrapQueueIdleV1(&peer));
    CHECK(!SV_NativeShadowReliableGenerationIdleV1(&peer));
    chan.reliable_length = 0;
    chan.fragment_pending = true;
    CHECK(!SV_NativeShadowPostBootstrapQueueIdleV1(&peer));
    CHECK(!SV_NativeShadowReliableGenerationIdleV1(&peer));
    chan.fragment_pending = false;
    chan.fragment_out.cursize = 1;
    CHECK(!SV_NativeShadowPostBootstrapQueueIdleV1(&peer));
    CHECK(!SV_NativeShadowReliableGenerationIdleV1(&peer));
    chan.fragment_out.cursize = 0;
    CHECK(SV_NativeShadowPostBootstrapQueueIdleV1(&peer));
    CHECK(SV_NativeShadowReliableGenerationIdleV1(&peer));

    SV_NativeShadowPeerDetachV1(&peer);
    CHECK(!SV_NativeShadowPostBootstrapQueueIdleV1(&peer));
    CHECK(!SV_NativeShadowReliableGenerationIdleV1(&peer));
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_challenge_queue_deadline(void)
{
    const uint32_t timeout =
        SV_NATIVE_SHADOW_CHALLENGE_QUEUE_TIMEOUT_MS;

    CHECK(!SV_NativeShadowChallengeQueueExpiredV1(100u, 100u));
    CHECK(!SV_NativeShadowChallengeQueueExpiredV1(
        100u, 100u + timeout - 1u));
    CHECK(SV_NativeShadowChallengeQueueExpiredV1(
        100u, 100u + timeout));
    CHECK(!SV_NativeShadowChallengeQueueExpiredV1(
        UINT32_MAX - 10u, timeout - 12u));
    CHECK(SV_NativeShadowChallengeQueueExpiredV1(
        UINT32_MAX - 10u, timeout - 11u));
}

static void test_epoch_advance_inside_packet(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 first;
    worr_native_readiness_record_v1 second;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    netchan_app_tx_prepare_fn tx_prepare;
    netchan_app_tx_completion_fn tx_completion;
    netchan_app_rx_fn rx;
    uint64_t first_generation;

    init_chan(&chan);
    CHECK(SV_NativeShadowPeerInitV1(&peer, &chan, 500));
    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 10, native_public_mask(&peer),
        native_public_mask(&peer), 500, &first));
    first_generation = peer.readiness.generation;
    tx_prepare = chan.app_tx_prepare;
    tx_completion = chan.app_tx_completion;
    rx = chan.app_rx;
    make_client_ready(&first, peer.clock_ticks, &client_ready);
    feed_ready(&peer, 501, &client_ready, &server_active);
    CHECK(peer.readiness.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    CHECK(SV_NativeShadowServerActiveQueuedV1(&peer));

    CHECK(SV_NativeShadowPacketBeginV1(&peer, 502));
    CHECK(SV_NativeShadowObserveInterveningServiceV1(&peer));

    /* SV_New is dispatched by that just-observed string command. */
    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 11, native_public_mask(&peer),
        native_public_mask(&peer), 502, &second));
    CHECK(peer.packet_open == 1);
    CHECK(peer.transport_initialized == 0 &&
          peer.retired_transport_initialized == 0 &&
          peer.cancelled_through_transport_epoch ==
              first.transport_epoch &&
          peer.cancellation_barriers == 2 &&
          peer.cancelled_transports == 1 &&
          peer.lifecycle ==
              SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS);
    CHECK(second.transport_epoch > first.transport_epoch);
    CHECK(second.readiness_nonce > first.readiness_nonce);
    CHECK(peer.readiness.generation > first_generation);
    CHECK(chan.app_tx_prepare == tx_prepare &&
          chan.app_tx_completion == tx_completion && chan.app_rx == rx);
    CHECK(chan.app_tx_opaque == &peer && chan.app_rx_opaque == &peer);
    CHECK(SV_NativeShadowPacketEndV1(&peer));
    make_client_ready(&second, peer.clock_ticks, &client_ready);
    feed_ready(&peer, 503, &client_ready, &server_active);
    CHECK(peer.readiness.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    CHECK(peer.activation_pending == 1 && peer.transport_initialized == 0);
    CHECK(SV_NativeShadowServerActiveQueuedV1(&peer));
    CHECK(peer.transport_initialized == 1 &&
          peer.retired_transport_initialized == 0 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_pending_challenge_epoch_is_cancelled(void)
{
    static const byte legacy[] = {0x41, 0x42};
    byte full_message_data[SV_NATIVE_SHADOW_SVC_WIRE_BYTES];
    byte full_queue_data[SV_NATIVE_SHADOW_SVC_WIRE_BYTES - 1u];
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 first;
    worr_native_readiness_record_v1 second;
    worr_native_readiness_record_v1 delayed_ready;
    worr_native_readiness_record_v1 unused_server_active;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    sizebuf_t full_message;
    sizebuf_t full_queue;
    command_carrier_fixture old_carrier;
    netchan_app_rx_output_v1_t output;
    sv_native_shadow_status_v1 status;
    uint32_t index;

    init_chan(&chan);
    CHECK(SV_NativeShadowPeerInitV1(&peer, &chan, 550));
    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 12, native_public_mask(&peer),
        native_public_mask(&peer), 550, &first));
    CHECK(peer.transport_initialized == 0 &&
          peer.cancelled_through_transport_epoch == 0 &&
          peer.cancellation_barriers == 1 &&
          peer.cancelled_transports == 0);

    /* No CLIENT_READY or transport object was created for the first
     * challenge.  The second challenge must still revoke its advertised
     * private epoch and recognize a structurally valid delayed carrier as
     * canceled legacy-only traffic. */
    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 13, native_public_mask(&peer),
        native_public_mask(&peer), 551, &second));
    CHECK(second.transport_epoch > first.transport_epoch &&
          peer.private_transport_epoch == second.transport_epoch &&
          peer.cancelled_through_transport_epoch ==
              first.transport_epoch &&
          peer.transport_initialized == 0 &&
          peer.retired_transport_initialized == 0 &&
          peer.cancellation_barriers == 2 &&
          peer.cancelled_transports == 0);

    /* This is the same physical-capacity predicate the production CLC wrapper
     * supplies.  It is deliberately full before delayed control arrives. */
    memset(full_message_data, 0x3c, sizeof(full_message_data));
    memset(full_queue_data, 0x5d, sizeof(full_queue_data));
    init_write_buffer(&full_message, full_message_data,
                      (uint32_t)sizeof(full_message_data), 0);
    init_write_buffer(&full_queue, full_queue_data,
                      (uint32_t)sizeof(full_queue_data), 0);
    CHECK(!SV_NativeShadowCanAppendSvcReadinessV1(
        &full_message, &full_queue));

    /* The reliable CLIENT_READY can outlive its CHALLENGE.  Once the second
     * epoch is issued, this fully valid old declaration is cancellation-floor
     * traffic rather than a binding failure for the replacement state. */
    make_client_ready(&first, peer.clock_ticks, &delayed_ready);
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &delayed_ready, pairs,
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    CHECK(SV_NativeShadowPacketBeginV1(&peer, 552));
    for (index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        CHECK(SV_NativeShadowObserveSettingWithResponseCapacityV1(
                  &peer, pairs[index].index, pairs[index].value,
                  &full_message, &full_queue,
                  &unused_server_active) ==
              SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED);
    }
    CHECK(SV_NativeShadowPacketEndV1(&peer));
    CHECK(peer.client_ready_records == 0 &&
          peer.server_active_records == 0 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS &&
          peer.stale_cancelled_readiness_records == 1);
    CHECK(SV_NativeShadowGetStatusV1(&peer, 552, &status));
    CHECK(status.stale_cancelled_readiness_records == 1 &&
          status.stale_cancelled_carriers == 0);

    build_command_carrier(
        &old_carrier, first.transport_epoch, 12, 1, 1, 3,
        legacy, sizeof(legacy));
    memset(&output, 0, sizeof(output));
    CHECK(receive_application(
              &chan, &peer, old_carrier.packet,
              old_carrier.packet_bytes, &output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(output.legacy_bytes == sizeof(legacy) &&
          peer.stale_cancelled_carriers == 1 &&
          peer.rx_commits == 0 && peer.rx_rejections == 0);

    /* A current CLIENT_READY against the same full queue still fails closed.
     * The key distinction is that the stale record above was classified before
     * capacity was relevant, so it could not disable this replacement epoch. */
    make_client_ready(&second, peer.clock_ticks, &delayed_ready);
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &delayed_ready, pairs,
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    CHECK(SV_NativeShadowPacketBeginV1(&peer, 553));
    for (index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        CHECK(SV_NativeShadowObserveSettingWithResponseCapacityV1(
                  &peer, pairs[index].index, pairs[index].value,
                  &full_message, &full_queue,
                  &unused_server_active) ==
              (index + 1 == WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT
                   ? SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED
                   : SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED));
    }
    CHECK(!SV_NativeShadowPacketEndV1(&peer));
    CHECK(!SV_NativeShadowPeerEnabledV1(&peer) &&
          peer.last_failure == SV_NATIVE_SHADOW_FAILURE_QUEUE &&
          peer.native_wire_committed == 0 &&
          peer.transport_initialized == 0 &&
          peer.retired_transport_initialized == 0 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          chan.app_tx_prepare != NULL && chan.app_rx != NULL);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_command_observation_ack_and_drain(void)
{
    static const byte legacy_prefix[] = {0x11, 0x22, 0x33};
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;
    command_carrier_fixture first;
    command_carrier_fixture second;
    command_carrier_fixture third;
    worr_command_record_v1 ordinary;
    worr_command_record_v1 legacy;
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_tx_prepare_output_v1_t tx_output;
    netchan_app_tx_prepare_output_v1_t bypass_output;
    worr_native_carrier_view_v1 ack_view;
    worr_native_tx_session_v1 sender;
    worr_native_tx_slot_v1 sender_slots[1];
    worr_native_tx_slot_v1 selected;
    byte candidate[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte full_legacy[977];
    uint32_t sender_sequence = 0;
    uint32_t acknowledged = 0;

    activate_peer(&peer, &chan, 50, 100, &challenge);
    CHECK(peer.transport.binding.transport_epoch ==
          challenge.transport_epoch);
    build_command_carrier(
        &first, challenge.transport_epoch, 50, 1, 1, 7,
        legacy_prefix, sizeof(legacy_prefix));
    CHECK(first.packet_bytes == sizeof(legacy_prefix) + 206u);
    CHECK(receive_application(
              &chan, &peer, first.packet, first.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy_prefix));
    CHECK(peer.rx_commits == 1 && peer.native_commands_accepted == 1 &&
          peer.pending_native_valid == 1 &&
          peer.transport.ack_ledger.receipt_count == 1 &&
          peer.transport.command_join.occupied_count == 1);

    ordinary = make_native_record(50, 2, 8);
    legacy = make_legacy_record(&ordinary);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &peer, &legacy, 102));
    CHECK(peer.pending_native_valid == 1 &&
          peer.legacy_join_observations == 0 &&
          peer.transport.command_join.occupied_count == 1);
    legacy = make_legacy_record(&first.record);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &peer, &legacy, 103));
    CHECK(peer.pending_native_valid == 0 && peer.command_matches == 1 &&
          peer.legacy_join_observations == 1 &&
          peer.transport.command_join.occupied_count == 0 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE);

    CHECK(SV_NativeShadowAckDueV1(&peer, 103));
    memset(candidate, 0, sizeof(candidate));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, candidate, sizeof(candidate),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    CHECK(tx_output.application_bytes == 48 && tx_output.token != 0);
    CHECK(Worr_NativeCarrierDecodeV1(
              candidate, tx_output.application_bytes, &ack_view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(ack_view.transport_epoch == challenge.transport_epoch &&
          ack_view.legacy_bytes == 0 && ack_view.entry_count == 1 &&
          ack_view.entries[0].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_ACK_V1 &&
          ack_view.entries[0].first_message_sequence == 1 &&
          ack_view.entries[0].last_message_sequence == 1);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED, candidate);
    CHECK(peer.tx_ack_rejections == 1 && peer.ack_emit_active == 0);

    CHECK(prepare_application(
              &chan, &peer, NULL, 0, candidate, sizeof(candidate),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    CHECK(Worr_NativeTxSessionInitV1(
        &sender, sender_slots, 1, &peer.transport.binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &sender, sender_slots, 1, first.record_ref, 0, 1,
              SV_NATIVE_SHADOW_PAYLOAD_BYTES,
              SV_NATIVE_SHADOW_WNE_BYTES, 1, &sender_sequence) ==
          WORR_NATIVE_TX_RETAINED);
    CHECK(sender_sequence == 1);
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &sender, sender_slots, 1, 1, 100, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(Worr_NativeCarrierSessionApplyAcksV1(
              &sender, sender_slots, 1, candidate,
              tx_output.application_bytes, &acknowledged) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(acknowledged == 1 && sender.retained_count == 0);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, candidate);
    CHECK(peer.tx_ack_handoffs == 1);

    /* A proven repeat rearms only the exact committed receipt. */
    CHECK(receive_application(
              &chan, &peer, first.packet, first.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_repeat_refreshes == 1 &&
          peer.transport.ack_ledger.receipt_count == 1);
    CHECK(SV_NativeShadowAckDueV1(&peer, 104));
    memset(full_legacy, 0x5a, sizeof(full_legacy));
    CHECK(prepare_application(
              &chan, &peer, full_legacy, 976, candidate,
              1024, &tx_output) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    CHECK(tx_output.application_bytes == 1024);
    CHECK(Worr_NativeCarrierDecodeV1(
              candidate, tx_output.application_bytes, &ack_view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(ack_view.legacy_bytes == 976 && ack_view.entry_count == 1);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED, candidate);
    CHECK(prepare_application(
              &chan, &peer, full_legacy, sizeof(full_legacy), candidate,
              1024, &bypass_output) ==
          NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(peer.ack_emit_active == 0);

    /* Once the compared slot retires, the next distinct command is admitted
     * into the same bounded stop-and-wait state. */
    build_command_carrier(
        &second, challenge.transport_epoch, 50, 2, 2, 8,
        legacy_prefix, sizeof(legacy_prefix));
    CHECK(receive_application(
              &chan, &peer, second.packet, second.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy_prefix) &&
          peer.rx_drained == 0 &&
          peer.rx_commits == 2 && peer.native_commands_accepted == 2 &&
          peer.pending_native_valid == 1 &&
          peer.pending_native_id.sequence == 2 &&
          peer.transport.command_join.occupied_count == 1 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE);

    /* An exact committed repeat still refreshes its ACK before the pending
     * guard, but a distinct command cannot queue behind it. */
    CHECK(receive_application(
              &chan, &peer, second.packet, second.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_repeat_refreshes == 2);
    build_command_carrier(
        &third, challenge.transport_epoch, 50, 3, 3, 9,
        legacy_prefix, sizeof(legacy_prefix));
    CHECK(receive_application(
              &chan, &peer, third.packet, third.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy_prefix) &&
          peer.rx_commits == 2 && peer.native_commands_accepted == 2 &&
          peer.rx_drained == 1 && peer.drain_entries == 1 &&
          peer.pending_native_valid == 1 &&
          peer.pending_native_id.sequence == 2 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          peer.last_failure ==
              SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW);
    CHECK(receive_application(
              &chan, &peer, second.packet, second.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_repeat_refreshes == 3 && peer.drain_entries == 1);

    legacy = make_legacy_record(&second.record);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &peer, &legacy, 105));
    CHECK(peer.pending_native_valid == 0 && peer.command_matches == 2 &&
          peer.legacy_join_observations == 2 &&
          peer.transport.command_join.occupied_count == 0 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN);
    CHECK(SV_NativeShadowAckDueV1(&peer, 105));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, candidate, sizeof(candidate),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, candidate);
    CHECK(peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          chan.app_tx_prepare != NULL && chan.app_rx != NULL);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_input_batch_confirmation_and_fallbacks(void)
{
    static const byte legacy_prefix[] = {0x71, 0x72, 0x73};
    netchan_t preconfirm_chan;
    netchan_t decline_chan;
    netchan_t mutation_chan;
    sv_native_shadow_peer_v1 preconfirm_peer;
    sv_native_shadow_peer_v1 decline_peer;
    sv_native_shadow_peer_v1 mutation_peer;
    sv_native_shadow_transport_v1 transport_before;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    input_batch_carrier_fixture batch;
    command_carrier_fixture command;
    worr_command_record_v1 legacy;
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_tx_prepare_output_v1_t tx_output;
    worr_native_carrier_view_v1 ack_view;
    byte candidate[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];

    /* A schema-2 carrier cannot make private state before the same-packet
     * confirmation has been appended and committed.  Even its mixed ACK
     * entry is ignored: the authoritative legacy prefix is the only effect. */
    init_chan(&preconfirm_chan);
    CHECK(SV_NativeShadowPeerInitV1(
        &preconfirm_peer, &preconfirm_chan, 50000));
    CHECK(SV_NativeShadowConfigureInputBatchV1(
        &preconfirm_peer, true));
    CHECK(SV_NativeShadowBeginEpochV1(
        &preconfirm_peer, 110, native_public_mask(&preconfirm_peer),
        native_public_mask(&preconfirm_peer), 50000, &challenge));
    make_client_ready(
        &challenge, preconfirm_peer.clock_ticks, &client_ready);
    feed_ready(
        &preconfirm_peer, 50001, &client_ready, &server_active);
    CHECK(SV_NativeShadowServerActiveQueuedV1(&preconfirm_peer));
    CHECK(preconfirm_peer.input_batch_requested == 1 &&
          preconfirm_peer.input_batch_confirmed == 0 &&
          preconfirm_peer.input_batch_declined == 0);
    build_input_batch_carrier(
        &batch, challenge.transport_epoch, 110, 1, 2, 1, 21,
        legacy_prefix, sizeof(legacy_prefix), true);
    corrupt_input_batch_payload_crc(
        &batch, challenge.transport_epoch, 1, legacy_prefix,
        sizeof(legacy_prefix), true);
    transport_before = preconfirm_peer.transport;
    CHECK(receive_application(
              &preconfirm_chan, &preconfirm_peer, batch.packet,
              batch.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy_prefix) &&
          memcmp(&preconfirm_peer.transport, &transport_before,
                 sizeof(transport_before)) == 0 &&
          preconfirm_peer.lifecycle ==
              SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          preconfirm_peer.enabled == 1 &&
          preconfirm_peer.rx_rejections == 0 &&
          preconfirm_peer.pending_native_valid == 0 &&
          preconfirm_peer.input_batch_pending_count == 0 &&
          preconfirm_peer.input_batch_legacy_fallbacks == 1);

    /* Schema-2 sizing is also private-state admission.  A short WNB1 body
     * with otherwise valid WTC/WNE framing must reach the same pre-confirm
     * legacy-only fallback instead of being rejected by the outer classifier. */
    memset(batch.payload, 0xa7,
           WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES);
    batch.payload_bytes = WORR_NATIVE_INPUT_BATCH_WIRE_HEADER_BYTES;
    rebuild_input_batch_carrier(
        &batch, challenge.transport_epoch, 2, legacy_prefix,
        sizeof(legacy_prefix), true);
    transport_before = preconfirm_peer.transport;
    CHECK(receive_application(
              &preconfirm_chan, &preconfirm_peer, batch.packet,
              batch.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy_prefix) &&
          memcmp(&preconfirm_peer.transport, &transport_before,
                 sizeof(transport_before)) == 0 &&
          preconfirm_peer.lifecycle ==
              SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          preconfirm_peer.rx_rejections == 0 &&
          preconfirm_peer.input_batch_pending_count == 0 &&
          preconfirm_peer.input_batch_legacy_fallbacks == 2);
    SV_NativeShadowPeerDestroyV1(&preconfirm_peer);

    /* Capacity for readiness but not readiness+confirmation declines only
     * WNB1.  SERVER_ACTIVE and the baseline schema-1 command/ACK path remain
     * fully usable. */
    activate_input_batch_peer(
        &decline_peer, &decline_chan, 111, 50100, false, &challenge);
    CHECK(decline_peer.input_batch_declined == 1 &&
          decline_peer.input_batch_ack_blocked == 0 &&
          decline_peer.input_batch_legacy_fallbacks == 1);
    build_input_batch_carrier(
        &batch, challenge.transport_epoch, 111, 1, 2, 7, 31,
        legacy_prefix, sizeof(legacy_prefix), true);
    corrupt_input_batch_payload_crc(
        &batch, challenge.transport_epoch, 7, legacy_prefix,
        sizeof(legacy_prefix), true);
    transport_before = decline_peer.transport;
    CHECK(receive_application(
              &decline_chan, &decline_peer, batch.packet,
              batch.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy_prefix) &&
          memcmp(&decline_peer.transport, &transport_before,
                 sizeof(transport_before)) == 0 &&
          decline_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          decline_peer.enabled == 1 && decline_peer.rx_rejections == 0 &&
          decline_peer.input_batch_legacy_fallbacks == 2);

    build_command_carrier(
        &command, challenge.transport_epoch, 111, 1, 1, 41,
        legacy_prefix, sizeof(legacy_prefix));
    CHECK(receive_application(
              &decline_chan, &decline_peer, command.packet,
              command.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    legacy = make_legacy_record(&command.record);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &decline_peer, &legacy, 50102));
    CHECK(SV_NativeShadowAckDueV1(&decline_peer, 50102));
    CHECK(prepare_application(
              &decline_chan, &decline_peer, NULL, 0, candidate,
              sizeof(candidate), &tx_output) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    CHECK(Worr_NativeCarrierDecodeV1(
              candidate, tx_output.application_bytes, &ack_view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(ack_view.entry_count == 1 &&
          ack_view.entries[0].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_ACK_V1 &&
          ack_view.entries[0].first_message_sequence == 1 &&
          ack_view.entries[0].last_message_sequence == 1);
    complete_application(
        &decline_chan, &decline_peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, candidate);
    SV_NativeShadowPeerDestroyV1(&decline_peer);

    /* A post-confirm userinfo mutation with no admitted WNB1 has no receipt
     * to quarantine.  It declines future batches without suppressing a later
     * baseline schema-1 receipt. */
    activate_input_batch_peer(
        &mutation_peer, &mutation_chan, 112, 50200, true, &challenge);
    SV_NativeShadowDisableInputBatchV1(&mutation_peer);
    CHECK(mutation_peer.input_batch_requested == 0 &&
          mutation_peer.input_batch_confirmed == 1 &&
          mutation_peer.input_batch_declined == 1 &&
          mutation_peer.input_batch_pending_count == 0 &&
          mutation_peer.input_batch_ack_blocked == 0);
    build_command_carrier(
        &command, challenge.transport_epoch, 112, 1, 1, 51,
        NULL, 0);
    CHECK(receive_application(
              &mutation_chan, &mutation_peer, command.packet,
              command.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    legacy = make_legacy_record(&command.record);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &mutation_peer, &legacy, 50202));
    CHECK(SV_NativeShadowAckDueV1(&mutation_peer, 50202));
    CHECK(prepare_application(
              &mutation_chan, &mutation_peer, NULL, 0, candidate,
              sizeof(candidate), &tx_output) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    complete_application(
        &mutation_chan, &mutation_peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, candidate);
    SV_NativeShadowPeerDestroyV1(&mutation_peer);
}

static void test_input_batch_ack_gate_and_highwater(void)
{
    static const byte legacy_prefix[] = {0x61, 0x62};
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    sv_native_shadow_transport_v1 transport_before;
    worr_native_readiness_record_v1 challenge;
    input_batch_carrier_fixture batch;
    input_batch_carrier_fixture replay;
    worr_command_record_v1 legacy;
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_tx_prepare_output_v1_t tx_output;
    worr_native_carrier_view_v1 ack_view;
    sv_native_shadow_input_batch_status_v1 status;
    byte candidate[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte candidate_before[sizeof(candidate)];
    uint32_t index;

    activate_input_batch_peer(
        &peer, &chan, 120, 51000, true, &challenge);
    CHECK(peer.input_batch_confirmed == 1 &&
          peer.input_batch_confirmations_appended == 1);
    build_input_batch_carrier(
        &batch, challenge.transport_epoch, 120, 1, 3, 1, 70,
        legacy_prefix, sizeof(legacy_prefix), false);
    CHECK(batch.packet_bytes == sizeof(legacy_prefix) + 128u + 330u);
    CHECK(receive_application(
              &chan, &peer, batch.packet, batch.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy_prefix) &&
          peer.rx_commits == 1 && peer.native_commands_accepted == 3 &&
          peer.transport.ack_ledger.receipt_count == 1 &&
          peer.transport.command_join.occupied_count == 3 &&
          peer.input_batch_pending_count == 3 &&
          peer.input_batch_ack_blocked == 1 &&
          peer.input_batches_received == 1 &&
          peer.input_batch_commands_received == 3);
    CHECK(!SV_NativeShadowAckDueV1(&peer, 51001));
    CHECK(!SV_NativeShadowOutputDueV1(&peer, 51001));
    memset(candidate, 0x9a, sizeof(candidate));
    memcpy(candidate_before, candidate, sizeof(candidate));
    CHECK(prepare_application(
              &chan, &peer, legacy_prefix, sizeof(legacy_prefix),
              candidate, sizeof(candidate), &tx_output) ==
          NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(memcmp(candidate, candidate_before, sizeof(candidate)) == 0);

    /* A committed repeat may refresh the receipt, but cannot release it
     * while any nested command still lacks the authoritative legacy half. */
    CHECK(receive_application(
              &chan, &peer, batch.packet, batch.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_repeat_refreshes == 1 &&
          peer.input_batch_repeat_refreshes == 1 &&
          peer.input_batch_ack_blocked == 1 &&
          peer.input_batch_pending_count == 3);

    for (index = 0; index < 3; ++index) {
        legacy = make_legacy_record(&batch.records[index]);
        CHECK(SV_NativeShadowObserveLegacyCommandV1(
            &peer, &legacy, 51002u + index));
        CHECK(peer.input_batch_pending_count == 2u - index &&
              peer.command_matches == index + 1u);
        if (index != 2) {
            CHECK(peer.input_batch_ack_blocked == 1);
            CHECK(!SV_NativeShadowAckDueV1(
                &peer, 51002u + index));
        }
    }
    CHECK(peer.input_batch_ack_blocked == 0 &&
          peer.input_batch_acknowledgements_unblocked == 1 &&
          peer.transport.command_join.occupied_count == 0 &&
          peer.matched_native_highwater_valid == 1 &&
          peer.matched_native_highwater.sequence == 3);
    CHECK(SV_NativeShadowGetInputBatchStatusV1(&peer, &status));
    CHECK(status.pending_count == 0 && status.ack_blocked == 0 &&
          status.legacy_joins == 3 &&
          status.acknowledgements_unblocked == 1);
    CHECK(SV_NativeShadowAckDueV1(&peer, 51004));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, candidate, sizeof(candidate),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    CHECK(Worr_NativeCarrierDecodeV1(
              candidate, tx_output.application_bytes, &ack_view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(ack_view.entry_count == 1 &&
          ack_view.entries[0].first_message_sequence == 1 &&
          ack_view.entries[0].last_message_sequence == 1);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, candidate);

    /* The replay gate is based on the first nested ID, not the batch tail.
     * Here the first ID equals matched high-water while the tail is fresh. */
    build_input_batch_carrier(
        &replay, challenge.transport_epoch, 120, 3, 2, 2, 80,
        legacy_prefix, sizeof(legacy_prefix), false);
    transport_before = peer.transport;
    CHECK(receive_application(
              &chan, &peer, replay.packet, replay.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy_prefix) &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          peer.last_failure == SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW &&
          peer.rx_commits == 1 && peer.input_batches_received == 1 &&
          peer.input_batch_pending_count == 0 &&
          memcmp(&peer.transport, &transport_before,
                 sizeof(transport_before)) == 0);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_input_batch_mismatch_expiry_and_malformed(void)
{
    netchan_t mismatch_chan;
    netchan_t expiry_chan;
    netchan_t malformed_chan;
    netchan_t corrupt_chan;
    sv_native_shadow_peer_v1 mismatch_peer;
    sv_native_shadow_peer_v1 expiry_peer;
    sv_native_shadow_peer_v1 malformed_peer;
    sv_native_shadow_peer_v1 corrupt_peer;
    sv_native_shadow_transport_v1 transport_before;
    worr_native_readiness_record_v1 challenge;
    input_batch_carrier_fixture batch;
    worr_command_record_v1 legacy;
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_tx_prepare_output_v1_t tx_output;
    byte candidate[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte candidate_before[sizeof(candidate)];

    activate_input_batch_peer(
        &mismatch_peer, &mismatch_chan, 121, 52000, true,
        &challenge);
    build_input_batch_carrier(
        &batch, challenge.transport_epoch, 121, 1, 2, 1, 90,
        NULL, 0, false);
    CHECK(receive_application(
              &mismatch_chan, &mismatch_peer, batch.packet,
              batch.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    legacy = make_legacy_record(&batch.records[0]);
    legacy.command.buttons ^= 1u;
    CHECK(Worr_CommandRecordCanonicalizeV1(
        &legacy, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &mismatch_peer, &legacy, 52002));
    CHECK(mismatch_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          mismatch_peer.command_mismatches == 1 &&
          mismatch_peer.input_batch_pending_count == 2 &&
          mismatch_peer.input_batch_ack_blocked == 1 &&
          mismatch_peer.transport.ack_ledger.receipt_count == 1);
    CHECK(!SV_NativeShadowAckDueV1(&mismatch_peer, 52002));
    memset(candidate, 0x4a, sizeof(candidate));
    memcpy(candidate_before, candidate, sizeof(candidate));
    CHECK(prepare_application(
              &mismatch_chan, &mismatch_peer, NULL, 0, candidate,
              sizeof(candidate), &tx_output) ==
          NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(memcmp(candidate, candidate_before, sizeof(candidate)) == 0);
    SV_NativeShadowPeerDestroyV1(&mismatch_peer);

    activate_input_batch_peer(
        &expiry_peer, &expiry_chan, 122, 52100, true, &challenge);
    build_input_batch_carrier(
        &batch, challenge.transport_epoch, 122, 1, 2, 1, 100,
        NULL, 0, false);
    CHECK(receive_application(
              &expiry_chan, &expiry_peer, batch.packet,
              batch.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(SV_NativeShadowReconcileCommandStreamV1(
        &expiry_peer, NULL,
        52101u + (uint32_t)SV_NATIVE_SHADOW_JOIN_EXPIRY_MS + 1u));
    CHECK(expiry_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          expiry_peer.join_expiries == 1 &&
          expiry_peer.input_batch_failures == 1 &&
          expiry_peer.input_batch_pending_count == 2 &&
          expiry_peer.input_batch_ack_blocked == 1 &&
          expiry_peer.transport.ack_ledger.receipt_count == 1);
    CHECK(!SV_NativeShadowAckDueV1(
        &expiry_peer,
        52101u + (uint32_t)SV_NATIVE_SHADOW_JOIN_EXPIRY_MS + 1u));
    memset(candidate, 0x5b, sizeof(candidate));
    memcpy(candidate_before, candidate, sizeof(candidate));
    CHECK(prepare_application(
              &expiry_chan, &expiry_peer, NULL, 0, candidate,
              sizeof(candidate), &tx_output) ==
          NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(memcmp(candidate, candidate_before, sizeof(candidate)) == 0);
    SV_NativeShadowPeerDestroyV1(&expiry_peer);

    /* The outer WNB1 framing/CRC remains valid, but the second nested WNC1
     * skips an identity.  Decode rejects the complete batch transactionally;
     * no RX slot, receipt, or join mutation is committed. */
    activate_input_batch_peer(
        &malformed_peer, &malformed_chan, 123, 52200, true,
        &challenge);
    build_input_batch_carrier(
        &batch, challenge.transport_epoch, 123, 1, 2, 1, 110,
        NULL, 0, false);
    make_input_batch_noncontiguous(
        &batch, challenge.transport_epoch, 1, NULL, 0);
    transport_before = malformed_peer.transport;
    CHECK(receive_application(
              &malformed_chan, &malformed_peer, batch.packet,
              batch.packet_bytes, &rx_output) == NETCHAN_APP_RX_REJECT);
    CHECK(malformed_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          malformed_peer.last_failure == SV_NATIVE_SHADOW_FAILURE_CODEC &&
          malformed_peer.rx_rejections == 1 &&
          malformed_peer.rx_commits == 0 &&
          malformed_peer.input_batch_pending_count == 0 &&
          memcmp(&malformed_peer.transport, &transport_before,
                 sizeof(transport_before)) == 0);
    SV_NativeShadowPeerDestroyV1(&malformed_peer);

    activate_input_batch_peer(
        &corrupt_peer, &corrupt_chan, 124, 52300, true,
        &challenge);
    build_input_batch_carrier(
        &batch, challenge.transport_epoch, 124, 1, 2, 1, 120,
        NULL, 0, false);
    corrupt_input_batch_payload_crc(
        &batch, challenge.transport_epoch, 1, NULL, 0, false);
    transport_before = corrupt_peer.transport;
    CHECK(receive_application(
              &corrupt_chan, &corrupt_peer, batch.packet,
              batch.packet_bytes, &rx_output) == NETCHAN_APP_RX_REJECT);
    CHECK(corrupt_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          corrupt_peer.last_failure == SV_NATIVE_SHADOW_FAILURE_CODEC &&
          corrupt_peer.rx_rejections == 1 &&
          corrupt_peer.rx_commits == 0 &&
          corrupt_peer.input_batch_pending_count == 0 &&
          memcmp(&corrupt_peer.transport, &transport_before,
                 sizeof(transport_before)) == 0);
    SV_NativeShadowPeerDestroyV1(&corrupt_peer);
}

static void test_input_batch_legacy_history_reconciliation(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;
    input_batch_carrier_fixture first_batch;
    input_batch_carrier_fixture second_batch;
    worr_command_stream_v1 stream;
    worr_command_stream_slot_v1 stream_slots[8];
    worr_command_record_v1 legacy;
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_tx_prepare_output_v1_t tx_output;
    byte candidate[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint32_t index;

    activate_input_batch_peer(
        &peer, &chan, 124, 53000, true, &challenge);
    CHECK(Worr_CommandStreamInitV1(
        &stream, stream_slots, 8,
        WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
        (worr_command_cursor_v1){124, 0}, 0));

    build_input_batch_carrier(
        &first_batch, challenge.transport_epoch, 124, 1, 2, 1,
        120, NULL, 0, false);

    /* The first one-command legacy packet was authoritative before the client
     * could form a minimum-size WNB1.  Production retains it in the canonical
     * command stream even though no native join was pending at that instant. */
    legacy = make_legacy_record(&first_batch.records[0]);
    CHECK(Worr_CommandStreamInsertV1(&stream, &legacy) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &peer, &legacy, 53002));
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, legacy.command_id, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(peer.input_batch_pending_count == 0 &&
          peer.command_matches == 0);

    /* Packet two carries WNB1 [1,2] beside only legacy command 2.  The
     * pre-parse production reconciliation seam must join retained command 1
     * before the current packet's command 2 is observed. */
    CHECK(receive_application(
              &chan, &peer, first_batch.packet,
              first_batch.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.input_batch_pending_count == 2 &&
          peer.input_batch_ack_blocked == 1);
    CHECK(SV_NativeShadowReconcileCommandStreamV1(
        &peer, &stream, 53003));
    CHECK(peer.input_batch_pending_count == 1 &&
          peer.input_batch_pending_ids[0].sequence == 2 &&
          peer.input_batch_legacy_joins == 1 &&
          peer.command_matches == 1 &&
          peer.input_batch_ack_blocked == 1);

    legacy = make_legacy_record(&first_batch.records[1]);
    CHECK(Worr_CommandStreamInsertV1(&stream, &legacy) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &peer, &legacy, 53004));
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, legacy.command_id, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(peer.input_batch_pending_count == 0 &&
          peer.input_batch_ack_blocked == 0 &&
          peer.input_batch_acknowledgements_unblocked == 1 &&
          peer.command_matches == 2);

    /* While batch one waits for its receipt at the client, later legacy
     * packets 3 and 4 can reach authoritative intake.  Preserve both exact
     * records, then release batch one and admit the delayed second batch. */
    build_input_batch_carrier(
        &second_batch, challenge.transport_epoch, 124, 3, 2, 2,
        122, NULL, 0, false);
    for (index = 0; index < 2; ++index) {
        legacy = make_legacy_record(&second_batch.records[index]);
        CHECK(Worr_CommandStreamInsertV1(&stream, &legacy) ==
              WORR_COMMAND_STREAM_INSERTED);
        CHECK(SV_NativeShadowObserveLegacyCommandV1(
            &peer, &legacy, 53005 + index));
        CHECK(Worr_CommandStreamConsumeV1(
                  &stream, legacy.command_id, NULL) ==
              WORR_COMMAND_STREAM_CONSUMED);
    }
    CHECK(peer.command_matches == 2 &&
          peer.input_batch_pending_count == 0);
    CHECK(SV_NativeShadowAckDueV1(&peer, 53006));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, candidate,
              sizeof(candidate), &tx_output) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, candidate);
    CHECK(!SV_NativeShadowAckDueV1(&peer, 53006));

    CHECK(receive_application(
              &chan, &peer, second_batch.packet,
              second_batch.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.input_batch_pending_count == 2 &&
          peer.input_batch_ack_blocked == 1);
    CHECK(SV_NativeShadowReconcileCommandStreamV1(
        &peer, &stream, 53007));
    CHECK(peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          peer.input_batch_pending_count == 0 &&
          peer.input_batch_ack_blocked == 0 &&
          peer.input_batches_received == 2 &&
          peer.input_batch_commands_received == 4 &&
          peer.input_batch_legacy_joins == 4 &&
          peer.input_batch_acknowledgements_unblocked == 2 &&
          peer.command_matches == 4 &&
          peer.input_batch_failures == 0 &&
          SV_NativeShadowAckDueV1(&peer, 53007));
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_many_sequential_commands_and_count_saturation(void)
{
    enum { COMMAND_COUNT = 256 };
    netchan_t chan;
    netchan_t saturation_chan;
    sv_native_shadow_peer_v1 peer;
    sv_native_shadow_peer_v1 saturation_peer;
    worr_native_readiness_record_v1 challenge;
    command_carrier_fixture fixture;
    worr_command_record_v1 legacy;
    netchan_app_rx_output_v1_t rx_output;
    uint32_t sequence;

    activate_peer(&peer, &chan, 70, 20000, &challenge);
    for (sequence = 1; sequence <= COMMAND_COUNT; ++sequence) {
        const uint32_t raw_time = 20000u + sequence * 2u;

        build_command_carrier(
            &fixture, challenge.transport_epoch, 70, sequence, sequence,
            (uint8_t)((sequence % 251u) + 1u), NULL, 0);
        CHECK(receive_application(
                  &chan, &peer, fixture.packet, fixture.packet_bytes,
                  &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
        CHECK(peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
              peer.pending_native_valid == 1 &&
              peer.pending_native_id.epoch == 70 &&
              peer.pending_native_id.sequence == sequence &&
              peer.native_commands_accepted == sequence &&
              peer.rx_commits == sequence &&
              peer.transport.command_join.occupied_count == 1);

        CHECK(receive_application(
                  &chan, &peer, fixture.packet, fixture.packet_bytes,
                  &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
        CHECK(peer.rx_repeat_refreshes == sequence &&
              peer.pending_native_valid == 1 &&
              peer.transport.command_join.occupied_count == 1);

        legacy = make_legacy_record(&fixture.record);
        CHECK(SV_NativeShadowObserveLegacyCommandV1(
            &peer, &legacy, raw_time));
        CHECK(peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
              peer.pending_native_valid == 0 &&
              peer.command_matches == sequence &&
              peer.legacy_join_observations == sequence &&
              peer.transport.command_join.occupied_count == 0 &&
              Worr_NativeCommandShadowJoinValidateV1(
                  &peer.transport.command_join));
    }
    CHECK(peer.native_commands_accepted == COMMAND_COUNT &&
          peer.rx_commits == COMMAND_COUNT &&
          peer.rx_repeat_refreshes == COMMAND_COUNT &&
          peer.command_matches == COMMAND_COUNT &&
          peer.rx_drained == 0 && peer.failures == 0);
    SV_NativeShadowPeerDestroyV1(&peer);

    activate_peer(
        &saturation_peer, &saturation_chan, 71, 30000, &challenge);
    saturation_peer.native_commands_accepted = UINT32_MAX;
    build_command_carrier(
        &fixture, challenge.transport_epoch, 71, 1, 1, 42, NULL, 0);
    CHECK(receive_application(
              &saturation_chan, &saturation_peer, fixture.packet,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(saturation_peer.native_commands_accepted == UINT32_MAX &&
          saturation_peer.rx_commits == 1 &&
          saturation_peer.pending_native_valid == 1);
    legacy = make_legacy_record(&fixture.record);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &saturation_peer, &legacy, 30001));
    CHECK(saturation_peer.native_commands_accepted == UINT32_MAX &&
          saturation_peer.pending_native_valid == 0 &&
          saturation_peer.command_matches == 1 &&
          saturation_peer.transport.command_join.occupied_count == 0 &&
          saturation_peer.lifecycle ==
              SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE);
    SV_NativeShadowPeerDestroyV1(&saturation_peer);
}

static void test_fresh_message_command_id_replay_drains(void)
{
    static const byte legacy_prefix[] = {0x51, 0x52, 0x53};
    uint32_t trial;

    for (trial = 0; trial < 2; ++trial) {
        netchan_t chan;
        sv_native_shadow_peer_v1 peer;
        worr_native_readiness_record_v1 challenge;
        command_carrier_fixture fixture;
        worr_command_record_v1 legacy;
        netchan_app_rx_output_v1_t rx_output;
        uint32_t sequence;
        const uint32_t command_epoch = 72u + trial;
        const uint32_t replay_sequence = trial == 0 ? 2u : 1u;

        activate_peer(
            &peer, &chan, command_epoch, 40000u + trial * 100u,
            &challenge);
        for (sequence = 1; sequence <= 2; ++sequence) {
            build_command_carrier(
                &fixture, challenge.transport_epoch, command_epoch,
                sequence, sequence, (uint8_t)(60u + sequence),
                legacy_prefix, sizeof(legacy_prefix));
            CHECK(receive_application(
                      &chan, &peer, fixture.packet,
                      fixture.packet_bytes, &rx_output) ==
                  NETCHAN_APP_RX_EXPOSE_LEGACY);
            legacy = make_legacy_record(&fixture.record);
            CHECK(SV_NativeShadowObserveLegacyCommandV1(
                &peer, &legacy, 40001u + trial * 100u + sequence));
        }
        CHECK(peer.matched_native_highwater_valid == 1 &&
              peer.matched_native_highwater.epoch == command_epoch &&
              peer.matched_native_highwater.sequence == 2 &&
              peer.pending_native_valid == 0 &&
              peer.rx_commits == 2 && peer.command_matches == 2);

        /* A distinct native message identity cannot make either the exact
         * high-water command or an older command fresh again. */
        build_command_carrier(
            &fixture, challenge.transport_epoch, command_epoch,
            replay_sequence, 3, (uint8_t)(80u + trial),
            legacy_prefix, sizeof(legacy_prefix));
        CHECK(receive_application(
                  &chan, &peer, fixture.packet, fixture.packet_bytes,
                  &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
        CHECK(rx_output.legacy_bytes == sizeof(legacy_prefix) &&
              peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
              peer.last_failure ==
                  SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW &&
              peer.rx_commits == 2 && peer.command_matches == 2 &&
              peer.rx_drained == 1 && peer.drain_entries == 1 &&
              peer.pending_native_valid == 0 &&
              peer.transport.command_join.occupied_count == 0 &&
              peer.matched_native_highwater.sequence == 2);
        SV_NativeShadowPeerDestroyV1(&peer);
    }
}

static void test_legacy_first_reconcile_mismatch_and_expiry(void)
{
    netchan_t reconcile_chan;
    netchan_t mismatch_chan;
    netchan_t expiry_chan;
    sv_native_shadow_peer_v1 reconcile_peer;
    sv_native_shadow_peer_v1 mismatch_peer;
    sv_native_shadow_peer_v1 expiry_peer;
    worr_native_readiness_record_v1 challenge;
    command_carrier_fixture fixture;
    worr_command_record_v1 legacy;
    worr_command_stream_v1 stream;
    worr_command_stream_slot_v1 stream_slots[2];
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_tx_prepare_output_v1_t tx_output;
    byte candidate[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];

    activate_peer(&reconcile_peer, &reconcile_chan, 60, 1000,
                  &challenge);
    build_command_carrier(
        &fixture, challenge.transport_epoch, 60, 1, 1, 9,
        NULL, 0);
    legacy = make_legacy_record(&fixture.record);
    CHECK(Worr_CommandStreamInitV1(
        &stream, stream_slots, 2,
        WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
        (worr_command_cursor_v1){60, 0}, 0));
    CHECK(Worr_CommandStreamInsertV1(&stream, &legacy) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, legacy.command_id, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(receive_application(
              &reconcile_chan, &reconcile_peer, fixture.packet,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(reconcile_peer.pending_native_valid == 1);
    CHECK(SV_NativeShadowReconcileCommandStreamV1(
        &reconcile_peer, &stream, 1002));
    CHECK(reconcile_peer.pending_native_valid == 0 &&
          reconcile_peer.command_matches == 1 &&
          reconcile_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE);
    SV_NativeShadowPeerDestroyV1(&reconcile_peer);

    activate_peer(&mismatch_peer, &mismatch_chan, 61, 2000,
                  &challenge);
    build_command_carrier(
        &fixture, challenge.transport_epoch, 61, 1, 1, 10,
        NULL, 0);
    CHECK(receive_application(
              &mismatch_chan, &mismatch_peer, fixture.packet,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    legacy = make_legacy_record(&fixture.record);
    legacy.command.buttons ^= 1u;
    CHECK(Worr_CommandRecordCanonicalizeV1(
        &legacy, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &mismatch_peer, &legacy, 2002));
    CHECK(mismatch_peer.pending_native_valid == 1 &&
          mismatch_peer.pending_native_id.sequence == 1 &&
          mismatch_peer.command_mismatches == 1 &&
          mismatch_peer.transport.command_join.occupied_count == 1 &&
          mismatch_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          mismatch_peer.last_failure ==
              SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &mismatch_peer, &legacy, 2003));
    CHECK(mismatch_peer.pending_native_valid == 1 &&
          mismatch_peer.legacy_join_observations == 2 &&
          mismatch_peer.command_mismatches == 1 &&
          mismatch_peer.transport.command_join.occupied_count == 1 &&
          mismatch_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN);
    CHECK(SV_NativeShadowAckDueV1(&mismatch_peer, 2003));
    CHECK(prepare_application(
              &mismatch_chan, &mismatch_peer, NULL, 0, candidate,
              sizeof(candidate), &tx_output) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    complete_application(
        &mismatch_chan, &mismatch_peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, candidate);
    SV_NativeShadowPeerDestroyV1(&mismatch_peer);

    activate_peer(&expiry_peer, &expiry_chan, 62, 3000,
                  &challenge);
    build_command_carrier(
        &fixture, challenge.transport_epoch, 62, 1, 1, 11,
        NULL, 0);
    CHECK(receive_application(
              &expiry_chan, &expiry_peer, fixture.packet,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(SV_NativeShadowReconcileCommandStreamV1(
        &expiry_peer, NULL,
        3001u + (uint32_t)SV_NATIVE_SHADOW_JOIN_EXPIRY_MS + 1u));
    CHECK(expiry_peer.pending_native_valid == 0 &&
          expiry_peer.join_expiries == 1 &&
          expiry_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN);
    CHECK(SV_NativeShadowAckDueV1(
        &expiry_peer,
        3001u + (uint32_t)SV_NATIVE_SHADOW_JOIN_EXPIRY_MS + 1u));
    CHECK(prepare_application(
              &expiry_chan, &expiry_peer, NULL, 0, candidate,
              sizeof(candidate), &tx_output) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    complete_application(
        &expiry_chan, &expiry_peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, candidate);
    SV_NativeShadowPeerDestroyV1(&expiry_peer);
}

static void test_epoch_cancellation_routing(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 old_challenge;
    worr_native_readiness_record_v1 new_challenge;
    worr_native_readiness_record_v1 third_challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    command_carrier_fixture old_command;
    command_carrier_fixture old_distinct;
    command_carrier_fixture new_command;
    worr_command_record_v1 legacy;
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_tx_prepare_output_v1_t tx_output;
    sv_native_shadow_transport_v1 active_before;
    byte candidate[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint32_t new_transport_epoch;
    uint64_t stale_before;
    uint64_t drained_before;

    activate_peer(&peer, &chan, 70, 4000, &old_challenge);
    CHECK(old_challenge.negotiated_capabilities ==
          WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK);
    build_command_carrier(
        &old_command, old_challenge.transport_epoch, 70, 1, 1, 12,
        NULL, 0);
    CHECK(receive_application(
              &chan, &peer, old_command.packet,
              old_command.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_commits == 1 && peer.pending_native_valid == 1);

    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 71, native_public_mask(&peer),
        native_public_mask(&peer), 4002, &new_challenge));
    CHECK(peer.transport_initialized == 0 &&
          peer.retired_transport_initialized == 0 &&
          peer.cancelled_through_transport_epoch ==
              old_challenge.transport_epoch &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS &&
          peer.pending_native_valid == 0 &&
          peer.cancellation_barriers == 2 &&
          peer.cancelled_transports == 1 &&
          peer.cancelled_receipts == 1);

    /* Exact old DATA is valid but below the monotonic cancellation floor.  It
     * exposes only legacy and cannot rearm a receipt or mutate readiness. */
    stale_before = peer.stale_cancelled_carriers;
    drained_before = peer.rx_drained;
    CHECK(receive_application(
              &chan, &peer, old_command.packet,
              old_command.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_repeat_refreshes == 0 && peer.rx_commits == 1 &&
          peer.stale_cancelled_carriers == stale_before + 1 &&
          peer.rx_drained == drained_before + 1 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS);

    make_client_ready(&new_challenge, peer.clock_ticks, &client_ready);
    feed_ready(&peer, 4003, &client_ready, &server_active);
    CHECK(peer.activation_pending == 1 &&
          peer.transport_initialized == 0 &&
          peer.retired_transport_initialized == 0);
    CHECK(SV_NativeShadowServerActiveQueuedV1(&peer));
    CHECK(peer.transport_initialized == 1 &&
          peer.retired_transport_initialized == 0 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE);
    new_transport_epoch = peer.transport.binding.transport_epoch;
    CHECK(new_transport_epoch == new_challenge.transport_epoch &&
          new_transport_epoch >
              peer.cancelled_through_transport_epoch);

    build_command_carrier(
        &new_command, new_transport_epoch, 71, 1, 1, 13,
        NULL, 0);
    CHECK(receive_application(
              &chan, &peer, new_command.packet,
              new_command.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_commits == 2);
    legacy = make_legacy_record(&new_command.record);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &peer, &legacy, 4004));
    CHECK(peer.matched_native_highwater_valid == 1 &&
          peer.matched_native_highwater.epoch == 71 &&
          peer.matched_native_highwater.sequence == 1 &&
          peer.pending_native_valid == 0);

    /* A distinct old message is stripped before any current-bank state is
     * touched.  Only the explicit stale/drain telemetry may advance. */
    build_command_carrier(
        &old_distinct, old_challenge.transport_epoch, 70, 2, 2, 14,
        NULL, 0);
    active_before = peer.transport;
    stale_before = peer.stale_cancelled_carriers;
    drained_before = peer.rx_drained;
    CHECK(receive_application(
              &chan, &peer, old_distinct.packet,
              old_distinct.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(memcmp(&active_before, &peer.transport,
                 sizeof(active_before)) == 0 &&
          peer.stale_cancelled_carriers == stale_before + 1 &&
          peer.rx_drained == drained_before + 1 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE);

    CHECK(SV_NativeShadowAckDueV1(&peer, 4004));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, candidate, sizeof(candidate),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    CHECK(peer.ack_emit_bank ==
          SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED, candidate);

    /* A second consecutive rotation advances one floor; no bank is silently
     * overwritten and the current receipt receives a counted cancellation. */
    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 72, native_public_mask(&peer),
        native_public_mask(&peer), 4005, &third_challenge));
    CHECK(peer.transport_initialized == 0 &&
          peer.retired_transport_initialized == 0 &&
          peer.cancelled_through_transport_epoch ==
              new_transport_epoch &&
          third_challenge.transport_epoch > new_transport_epoch &&
          peer.cancellation_barriers == 3 &&
          peer.cancelled_transports == 2 &&
          peer.cancelled_receipts == 2);

    /* Cancellation recognizes only structurally valid old carriers. */
    old_distinct.packet[
        old_distinct.packet_bytes -
        WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES - 1u] ^= 0x01;
    CHECK(receive_application(
              &chan, &peer, old_distinct.packet,
              old_distinct.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_REJECT);
    CHECK(peer.last_failure == SV_NATIVE_SHADOW_FAILURE_CARRIER);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_map_quiesce_cancels_pending_command_before_stream_reset(void)
{
    static const byte legacy[] = {0x51, 0x52};
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 old_challenge;
    worr_native_readiness_record_v1 new_challenge;
    worr_native_readiness_record_v1 delayed_ready;
    worr_native_readiness_record_v1 new_ready;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_record_v1 untouched;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    command_carrier_fixture old_command;
    netchan_app_rx_output_v1_t rx_output;
    sv_native_shadow_status_v1 status;
    uint64_t barriers;
    uint32_t index;

    activate_peer(&peer, &chan, 500, 12000, &old_challenge);
    build_command_carrier(
        &old_command, old_challenge.transport_epoch, 500, 1, 1, 0x41,
        legacy, sizeof(legacy));
    CHECK(receive_application(
              &chan, &peer, old_command.packet,
              old_command.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.pending_native_valid == 1 &&
          peer.transport.ack_ledger.receipt_count == 1);

    CHECK(SV_NativeShadowPeerQuiesceMapV1(&peer));
    CHECK(peer.lifecycle ==
              SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS &&
          peer.transport_initialized == 0 &&
          peer.retired_transport_initialized == 0 &&
          peer.pending_native_valid == 0 &&
          peer.input_batch_pending_count == 0 &&
          peer.matched_native_highwater_valid == 0 &&
          peer.cancelled_through_transport_epoch ==
              old_challenge.transport_epoch &&
          peer.cancellation_barriers == 2 &&
          peer.cancelled_transports == 1 &&
          peer.cancelled_receipts == 1 &&
          peer.failures == 0 &&
          peer.last_failure == SV_NATIVE_SHADOW_FAILURE_NONE &&
          chan.app_tx_opaque == &peer && chan.app_rx_opaque == &peer);

    barriers = peer.cancellation_barriers;
    CHECK(SV_NativeShadowPeerQuiesceMapV1(&peer));
    CHECK(peer.cancellation_barriers == barriers &&
          peer.failures == 0);

    /* A delayed old command remains valid carrier syntax, exposes the exact
     * authoritative legacy prefix, and cannot recreate a pending native half
     * after the map cancellation floor is installed. */
    CHECK(receive_application(
              &chan, &peer, old_command.packet,
              old_command.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy) &&
          peer.pending_native_valid == 0 &&
          peer.stale_cancelled_carriers == 1 &&
          peer.failures == 0);

    /* The same floor consumes a delayed reliable CLIENT_READY from the old
     * map without advancing or failing the retained readiness machine. */
    make_client_ready(&old_challenge, peer.clock_ticks, &delayed_ready);
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &delayed_ready, pairs,
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    memset(&untouched, 0xa5, sizeof(untouched));
    CHECK(SV_NativeShadowPacketBeginV1(&peer, 12002));
    for (index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        CHECK(SV_NativeShadowObserveSettingV1(
                  &peer, pairs[index].index, pairs[index].value,
                  &untouched) ==
              SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED);
    }
    CHECK(SV_NativeShadowPacketEndV1(&peer));
    CHECK(peer.stale_cancelled_readiness_records == 1 &&
          peer.failures == 0);

    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 501, native_public_mask(&peer),
        native_public_mask(&peer), 12003, &new_challenge));
    CHECK(new_challenge.transport_epoch >
              old_challenge.transport_epoch &&
          peer.cancelled_through_transport_epoch ==
              old_challenge.transport_epoch &&
          peer.cancellation_barriers == barriers + 1);
    make_client_ready(&new_challenge, peer.clock_ticks, &new_ready);
    feed_ready(&peer, 12004, &new_ready, &server_active);
    CHECK(SV_NativeShadowServerActiveQueuedV1(&peer));
    CHECK(peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          peer.transport_initialized == 1 &&
          peer.transport.binding.transport_epoch ==
              new_challenge.transport_epoch &&
          peer.failures == 0);
    CHECK(SV_NativeShadowGetStatusV1(&peer, 12004, &status));
    CHECK(status.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          status.cancelled_through_transport_epoch ==
              old_challenge.transport_epoch &&
          status.last_failure == SV_NATIVE_SHADOW_FAILURE_NONE);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_map_quiesce_busy_peer_fails_closed(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;

    activate_peer(&peer, &chan, 510, 12100, &challenge);
    peer.packet_open = 1;
    CHECK(!SV_NativeShadowPeerQuiesceMapV1(&peer));
    CHECK(peer.enabled == 0 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          peer.packet_open == 0 &&
          peer.last_failure == SV_NATIVE_SHADOW_FAILURE_SESSION &&
          peer.failures == 1);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void encode_ack_range_carrier(uint32_t transport_epoch,
                                     uint32_t first_sequence,
                                     uint32_t last_sequence,
                                     byte *packet,
                                     size_t packet_capacity,
                                     size_t *packet_bytes)
{
    worr_native_carrier_entry_v1 entry;

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    entry.first_message_sequence = first_sequence;
    entry.last_message_sequence = last_sequence;
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, NULL, 0, NULL, 0, &entry, 1,
              packet, packet_capacity, packet_bytes) ==
          WORR_NATIVE_CARRIER_OK);
}

static void build_command_ack_carrier(
    command_carrier_fixture *fixture,
    uint32_t transport_epoch,
    uint32_t command_epoch,
    uint32_t command_sequence,
    uint32_t message_sequence,
    uint8_t marker,
    uint32_t ack_first,
    uint32_t ack_last)
{
    worr_native_carrier_entry_v1 entries[2];

    build_command_carrier(
        fixture, transport_epoch, command_epoch, command_sequence,
        message_sequence, marker, NULL, 0);
    memset(entries, 0, sizeof(entries));
    entries[0].struct_size = sizeof(entries[0]);
    entries[0].schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entries[0].entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entries[0].data_bytes = SV_NATIVE_SHADOW_WNE_BYTES;
    entries[1].struct_size = sizeof(entries[1]);
    entries[1].schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entries[1].entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    entries[1].first_message_sequence = ack_first;
    entries[1].last_message_sequence = ack_last;
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, NULL, 0, fixture->envelope,
              sizeof(fixture->envelope), entries, 2,
              fixture->packet, sizeof(fixture->packet),
              &fixture->packet_bytes) == WORR_NATIVE_CARRIER_OK);
}

static void encode_ack_only_carrier(uint32_t transport_epoch,
                                    byte *packet,
                                    size_t packet_capacity,
                                    size_t *packet_bytes)
{
    encode_ack_range_carrier(
        transport_epoch, 1, 1, packet, packet_capacity, packet_bytes);
}

static void test_carrier_rejections_and_completion_provenance(void)
{
    static const byte plain_legacy[] = {0x45, 0x46, 0x47};
    netchan_t direction_chan;
    netchan_t malformed_chan;
    netchan_t epoch_chan;
    netchan_t reference_chan;
    netchan_t completion_chan;
    sv_native_shadow_peer_v1 direction_peer;
    sv_native_shadow_peer_v1 malformed_peer;
    sv_native_shadow_peer_v1 epoch_peer;
    sv_native_shadow_peer_v1 reference_peer;
    sv_native_shadow_peer_v1 completion_peer;
    worr_native_readiness_record_v1 challenge;
    command_carrier_fixture fixture;
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_rx_output_v1_t rx_before;
    netchan_app_tx_prepare_output_v1_t tx_output;
    netchan_app_tx_completion_info_v1_t bad_completion;
    byte packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte malformed[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte candidate[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes = 0;
    uint64_t failures_before;

    activate_peer(&direction_peer, &direction_chan, 80, 5000,
                  &challenge);
    memset(&rx_output, 0xa5, sizeof(rx_output));
    rx_before = rx_output;
    CHECK(receive_application(
              &direction_chan, &direction_peer, plain_legacy,
              sizeof(plain_legacy), &rx_output) ==
          NETCHAN_APP_RX_BYPASS);
    CHECK(!memcmp(&rx_output, &rx_before, sizeof(rx_output)));
    encode_ack_only_carrier(
        challenge.transport_epoch, packet, sizeof(packet), &packet_bytes);
    CHECK(receive_application(
              &direction_chan, &direction_peer, packet, packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(direction_peer.lifecycle ==
              SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          direction_peer.last_failure == SV_NATIVE_SHADOW_FAILURE_NONE &&
          direction_chan.app_tx_prepare != NULL &&
          direction_chan.app_rx != NULL);
    build_command_carrier(
        &fixture, challenge.transport_epoch, 80, 1, 1, 14,
        plain_legacy, sizeof(plain_legacy));
    CHECK(receive_application(
              &direction_chan, &direction_peer, fixture.packet,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(plain_legacy) &&
          direction_peer.rx_commits == 1 &&
          direction_peer.rx_drained == 0);
    SV_NativeShadowPeerDestroyV1(&direction_peer);

    activate_peer(&malformed_peer, &malformed_chan, 81, 6000,
                  &challenge);
    build_command_carrier(
        &fixture, challenge.transport_epoch, 81, 1, 1, 15,
        NULL, 0);
    memcpy(malformed, fixture.packet, fixture.packet_bytes);
    malformed[0] ^= 1u;
    CHECK(receive_application(
              &malformed_chan, &malformed_peer, malformed,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_REJECT);
    CHECK(malformed_peer.lifecycle ==
              SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          malformed_peer.rx_rejections == 1);
    SV_NativeShadowPeerDestroyV1(&malformed_peer);

    activate_peer(&epoch_peer, &epoch_chan, 82, 7000, &challenge);
    build_command_carrier(
        &fixture, challenge.transport_epoch + 1u, 82, 1, 1, 16,
        NULL, 0);
    CHECK(receive_application(
              &epoch_chan, &epoch_peer, fixture.packet,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_REJECT);
    CHECK(epoch_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          epoch_peer.rx_rejections == 1);
    SV_NativeShadowPeerDestroyV1(&epoch_peer);

    activate_peer(&reference_peer, &reference_chan, 84, 7500,
                  &challenge);
    build_command_carrier(
        &fixture, challenge.transport_epoch, 83, 1, 1, 16,
        NULL, 0);
    CHECK(receive_application(
              &reference_chan, &reference_peer, fixture.packet,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_REJECT);
    CHECK(reference_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          reference_peer.last_failure ==
              SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW &&
          reference_peer.rx_rejections == 1 &&
          reference_peer.rx_commits == 0);
    SV_NativeShadowPeerDestroyV1(&reference_peer);

    activate_peer(&completion_peer, &completion_chan, 83, 8000,
                  &challenge);
    build_command_carrier(
        &fixture, challenge.transport_epoch, 83, 1, 1, 17,
        NULL, 0);
    CHECK(receive_application(
              &completion_chan, &completion_peer, fixture.packet,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(SV_NativeShadowAckDueV1(&completion_peer, 8002));
    CHECK(prepare_application(
              &completion_chan, &completion_peer, NULL, 0, candidate,
              sizeof(candidate), &tx_output) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    memset(&bad_completion, 0, sizeof(bad_completion));
    bad_completion.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    bad_completion.struct_size = sizeof(bad_completion);
    bad_completion.result = NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED;
    bad_completion.packet_copies = 1;
    bad_completion.application_bytes = tx_output.application_bytes;
    bad_completion.token = tx_output.token + 1u;
    completion_chan.app_tx_completion(
        &completion_peer, &bad_completion, candidate);
    CHECK(completion_peer.ack_emit_active == 0 &&
          completion_peer.lifecycle ==
              SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          completion_peer.drain_entries == 1 &&
          completion_peer.last_failure == SV_NATIVE_SHADOW_FAILURE_ACK &&
          completion_chan.app_tx_prepare != NULL &&
          completion_chan.app_rx != NULL);
    failures_before = completion_peer.failures;
    CHECK(SV_NativeShadowAckDueV1(&completion_peer, 8002));
    CHECK(prepare_application(
              &completion_chan, &completion_peer, NULL, 0, candidate,
              sizeof(candidate), &tx_output) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    complete_application(
        &completion_chan, &completion_peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID, candidate);
    CHECK(completion_peer.failures == failures_before &&
          completion_peer.drain_entries == 1 &&
          completion_peer.tx_ack_rejections == 2 &&
          completion_peer.ack_emit_active == 0);
    SV_NativeShadowPeerDestroyV1(&completion_peer);
}

static void test_clock_wrap_and_regression(void)
{
    netchan_t wrap_chan;
    netchan_t regress_chan;
    sv_native_shadow_peer_v1 wrap_peer;
    sv_native_shadow_peer_v1 regress_peer;
    worr_native_readiness_record_v1 challenge;
    const uint64_t start = UINT32_MAX - UINT32_C(5);

    init_chan(&wrap_chan);
    CHECK(SV_NativeShadowPeerInitV1(
        &wrap_peer, &wrap_chan, UINT32_MAX - UINT32_C(5)));
    CHECK(SV_NativeShadowBeginEpochV1(
        &wrap_peer, 20, native_public_mask(&wrap_peer),
        native_public_mask(&wrap_peer),
        UINT32_MAX - UINT32_C(5), &challenge));
    CHECK(SV_NativeShadowPacketBeginV1(&wrap_peer, 3));
    CHECK(wrap_peer.clock_ticks == start + UINT64_C(9));
    CHECK(SV_NativeShadowPacketEndV1(&wrap_peer));
    SV_NativeShadowPeerDestroyV1(&wrap_peer);

    init_chan(&regress_chan);
    CHECK(SV_NativeShadowPeerInitV1(&regress_peer, &regress_chan, 100));
    CHECK(SV_NativeShadowBeginEpochV1(
        &regress_peer, 21, native_public_mask(&regress_peer),
        native_public_mask(&regress_peer), 100, &challenge));
    CHECK(!SV_NativeShadowPacketBeginV1(&regress_peer, 50));
    CHECK(!SV_NativeShadowPeerEnabledV1(&regress_peer));
    CHECK(regress_peer.last_failure == SV_NATIVE_SHADOW_FAILURE_CLOCK);
    CHECK(regress_chan.app_tx_prepare == NULL &&
          regress_chan.app_rx == NULL);
    SV_NativeShadowPeerDestroyV1(&regress_peer);
}

static void test_binding_and_interleaving_fail_closed(void)
{
    netchan_t binding_chan;
    netchan_t parser_chan;
    sv_native_shadow_peer_v1 binding_peer;
    sv_native_shadow_peer_v1 parser_peer;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];

    init_chan(&binding_chan);
    CHECK(SV_NativeShadowPeerInitV1(&binding_peer, &binding_chan, 1));
    CHECK(!SV_NativeShadowBeginEpochV1(
        &binding_peer, 1, WORR_NET_CAP_LEGACY_STAGE_MASK,
        WORR_NET_CAP_LEGACY_STAGE_MASK, 1, &challenge));
    CHECK(!SV_NativeShadowPeerEnabledV1(&binding_peer));
    CHECK(binding_peer.last_failure ==
          SV_NATIVE_SHADOW_FAILURE_OFFICIAL_BINDING);
    SV_NativeShadowPeerDestroyV1(&binding_peer);

    init_chan(&parser_chan);
    CHECK(SV_NativeShadowPeerInitV1(&parser_peer, &parser_chan, 2));
    CHECK(SV_NativeShadowBeginEpochV1(
        &parser_peer, 2, native_public_mask(&parser_peer),
        native_public_mask(&parser_peer), 2, &challenge));
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &challenge, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    CHECK(SV_NativeShadowPacketBeginV1(&parser_peer, 3));
    CHECK(SV_NativeShadowObserveSettingV1(
              &parser_peer, pairs[0].index, pairs[0].value,
              &challenge) == SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED);
    CHECK(!SV_NativeShadowObserveInterveningServiceV1(&parser_peer));
    CHECK(!SV_NativeShadowPeerEnabledV1(&parser_peer));
    CHECK(parser_chan.app_tx_prepare == NULL && parser_chan.app_rx == NULL);
    SV_NativeShadowPeerDestroyV1(&parser_peer);
}

static void test_svc_append_capacity_and_wire(void)
{
    enum {
        SEED_BYTES = 5,
        QUEUE_SEED_BYTES = 7,
        WIRE_BYTES = SV_NATIVE_SHADOW_SVC_WIRE_BYTES,
    };
    byte short_message_data[SEED_BYTES + WIRE_BYTES - 1];
    byte short_message_before[sizeof(short_message_data)];
    byte short_queue_data[QUEUE_SEED_BYTES + SEED_BYTES + WIRE_BYTES];
    byte short_queue_before[sizeof(short_queue_data)];
    byte exact_message_data[SEED_BYTES + WIRE_BYTES];
    byte exact_message_before[sizeof(exact_message_data)];
    byte exact_queue_data[QUEUE_SEED_BYTES + SEED_BYTES + WIRE_BYTES];
    byte exact_queue_before[sizeof(exact_queue_data)];
    byte aliased_data[QUEUE_SEED_BYTES + SEED_BYTES + WIRE_BYTES + 2];
    byte aliased_before[sizeof(aliased_data)];
    sizebuf_t short_message;
    sizebuf_t short_message_struct_before;
    sizebuf_t short_queue;
    sizebuf_t short_queue_struct_before;
    sizebuf_t exact_message;
    sizebuf_t exact_message_struct_before;
    sizebuf_t exact_queue;
    sizebuf_t exact_queue_struct_before;
    sizebuf_t aliased_message;
    sizebuf_t aliased_message_struct_before;
    sizebuf_t aliased_queue;
    sizebuf_t aliased_queue_struct_before;
    worr_native_readiness_record_v1 record;
    worr_native_readiness_record_v1 invalid_record;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    const byte *wire;
    uint32_t pair_index;
    uint32_t offset;

    CHECK(Worr_NativeReadinessRecordInitV1(
        &record, WORR_NATIVE_READINESS_RECORD_CHALLENGE,
        UINT32_C(0xfedcba98),
        WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK,
        UINT64_C(0xfedcba9876543210)));

    /* One byte below the exact record width must leave both sizebuf metadata
     * and every backing byte unchanged. */
    memset(short_message_data, 0x51, sizeof(short_message_data));
    memset(short_queue_data, 0x62, sizeof(short_queue_data));
    init_write_buffer(&short_message, short_message_data,
                      (uint32_t)sizeof(short_message_data), SEED_BYTES);
    init_write_buffer(&short_queue, short_queue_data,
                      (uint32_t)sizeof(short_queue_data), QUEUE_SEED_BYTES);
    memcpy(short_message_before, short_message_data,
           sizeof(short_message_before));
    memcpy(short_queue_before, short_queue_data,
           sizeof(short_queue_before));
    memcpy(&short_message_struct_before, &short_message,
           sizeof(short_message_struct_before));
    memcpy(&short_queue_struct_before, &short_queue,
           sizeof(short_queue_struct_before));
    CHECK(!SV_NativeShadowCanAppendSvcReadinessV1(
        &short_message, &short_queue));
    CHECK(!SV_NativeShadowAppendSvcReadinessV1(
        &short_message, &short_queue, svc_q2pro_setting, &record));
    CHECK(!memcmp(&short_message, &short_message_struct_before,
                  sizeof(short_message)));
    CHECK(!memcmp(&short_queue, &short_queue_struct_before,
                  sizeof(short_queue)));
    CHECK(!memcmp(short_message_data, short_message_before,
                  sizeof(short_message_data)));
    CHECK(!memcmp(short_queue_data, short_queue_before,
                  sizeof(short_queue_data)));

    /* The reliable destination needs the entire eventual message, not merely
     * the newly appended count-derived readiness record. */
    memset(exact_message_data, 0x73, sizeof(exact_message_data));
    memset(exact_queue_data, 0x84, sizeof(exact_queue_data));
    init_write_buffer(&exact_message, exact_message_data,
                      (uint32_t)sizeof(exact_message_data), SEED_BYTES);
    init_write_buffer(&exact_queue, exact_queue_data,
                      QUEUE_SEED_BYTES + WIRE_BYTES, QUEUE_SEED_BYTES);
    memcpy(exact_message_before, exact_message_data,
           sizeof(exact_message_before));
    memcpy(exact_queue_before, exact_queue_data,
           sizeof(exact_queue_before));
    memcpy(&exact_message_struct_before, &exact_message,
           sizeof(exact_message_struct_before));
    memcpy(&exact_queue_struct_before, &exact_queue,
           sizeof(exact_queue_struct_before));
    CHECK(!SV_NativeShadowCanAppendSvcReadinessV1(
        &exact_message, &exact_queue));
    CHECK(!SV_NativeShadowAppendSvcReadinessV1(
        &exact_message, &exact_queue, svc_q2pro_setting, &record));
    CHECK(!memcmp(&exact_message, &exact_message_struct_before,
                  sizeof(exact_message)));
    CHECK(!memcmp(&exact_queue, &exact_queue_struct_before,
                  sizeof(exact_queue)));
    CHECK(!memcmp(exact_message_data, exact_message_before,
                  sizeof(exact_message_data)));
    CHECK(!memcmp(exact_queue_data, exact_queue_before,
                  sizeof(exact_queue_data)));

    /* Exact source and destination capacity succeeds.  Only the current
     * message cursor and its final count-derived record may change. */
    memset(exact_message_data, 0x95, sizeof(exact_message_data));
    memset(exact_queue_data, 0xa6, sizeof(exact_queue_data));
    for (offset = 0; offset < SEED_BYTES; ++offset)
        exact_message_data[offset] = (byte)(UINT32_C(0xc0) + offset);
    init_write_buffer(&exact_message, exact_message_data,
                      (uint32_t)sizeof(exact_message_data), SEED_BYTES);
    init_write_buffer(&exact_queue, exact_queue_data,
                      (uint32_t)sizeof(exact_queue_data), QUEUE_SEED_BYTES);
    memcpy(exact_message_before, exact_message_data,
           sizeof(exact_message_before));
    memcpy(exact_queue_before, exact_queue_data,
           sizeof(exact_queue_before));
    memcpy(&exact_queue_struct_before, &exact_queue,
           sizeof(exact_queue_struct_before));
    CHECK(SV_NativeShadowCanAppendSvcReadinessV1(
        &exact_message, &exact_queue));
    CHECK(SV_NativeShadowAppendSvcReadinessV1(
        &exact_message, &exact_queue, svc_q2pro_setting, &record));
    CHECK(exact_message.cursize == sizeof(exact_message_data));
    CHECK(!exact_message.overflowed);
    CHECK(!memcmp(exact_message_data, exact_message_before, SEED_BYTES));
    CHECK(!memcmp(&exact_queue, &exact_queue_struct_before,
                  sizeof(exact_queue)));
    CHECK(!memcmp(exact_queue_data, exact_queue_before,
                   sizeof(exact_queue_data)));

    /* Distinct sizebufs and offset data pointers are still aliases when their
     * writable backing ranges overlap.  Reject before encoding or mutation. */
    memset(aliased_data, 0xd9, sizeof(aliased_data));
    init_write_buffer(&aliased_message, aliased_data + 1,
                      SEED_BYTES + WIRE_BYTES, SEED_BYTES);
    init_write_buffer(&aliased_queue, aliased_data,
                      (uint32_t)sizeof(aliased_data), QUEUE_SEED_BYTES);
    memcpy(aliased_before, aliased_data, sizeof(aliased_before));
    memcpy(&aliased_message_struct_before, &aliased_message,
           sizeof(aliased_message_struct_before));
    memcpy(&aliased_queue_struct_before, &aliased_queue,
           sizeof(aliased_queue_struct_before));
    CHECK(!SV_NativeShadowCanAppendSvcReadinessV1(
        &aliased_message, &aliased_queue));
    CHECK(!SV_NativeShadowAppendSvcReadinessV1(
        &aliased_message, &aliased_queue, svc_q2pro_setting, &record));
    CHECK(!memcmp(&aliased_message, &aliased_message_struct_before,
                  sizeof(aliased_message)));
    CHECK(!memcmp(&aliased_queue, &aliased_queue_struct_before,
                  sizeof(aliased_queue)));
    CHECK(!memcmp(aliased_data, aliased_before, sizeof(aliased_data)));

    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    CHECK((int)svc_q2pro_setting == 24);
    wire = exact_message_data + SEED_BYTES;
    for (pair_index = 0;
         pair_index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++pair_index) {
        offset = pair_index * 9;
        CHECK(wire[offset] == (byte)svc_q2pro_setting);
        CHECK(read_u32_le(wire + offset + 1) ==
              (uint32_t)(int32_t)pairs[pair_index].index);
        CHECK(read_u32_le(wire + offset + 5) ==
              (uint32_t)(int32_t)pairs[pair_index].value);
    }
    /* Golden negative words: BEGIN index and epoch-low value are sign
     * extended from int16, not zero-extended. */
    CHECK(wire[0] == UINT8_C(24));
    CHECK(wire[1] == UINT8_C(0x14) && wire[2] == UINT8_C(0x83) &&
          wire[3] == UINT8_C(0xff) && wire[4] == UINT8_C(0xff));
    offset = UINT32_C(2) * UINT32_C(9) + UINT32_C(5);
    CHECK(wire[offset] == UINT8_C(0x98) &&
          wire[offset + 1] == UINT8_C(0xba) &&
          wire[offset + 2] == UINT8_C(0xff) &&
          wire[offset + 3] == UINT8_C(0xff));

    /* An encode failure after successful capacity preflight is also atomic. */
    invalid_record = record;
    invalid_record.record_checksum ^= UINT32_C(1);
    memset(exact_message_data, 0xb7, sizeof(exact_message_data));
    memset(exact_queue_data, 0xc8, sizeof(exact_queue_data));
    init_write_buffer(&exact_message, exact_message_data,
                      (uint32_t)sizeof(exact_message_data), SEED_BYTES);
    init_write_buffer(&exact_queue, exact_queue_data,
                      (uint32_t)sizeof(exact_queue_data), QUEUE_SEED_BYTES);
    memcpy(exact_message_before, exact_message_data,
           sizeof(exact_message_before));
    memcpy(exact_queue_before, exact_queue_data,
           sizeof(exact_queue_before));
    memcpy(&exact_message_struct_before, &exact_message,
           sizeof(exact_message_struct_before));
    memcpy(&exact_queue_struct_before, &exact_queue,
           sizeof(exact_queue_struct_before));
    CHECK(!SV_NativeShadowAppendSvcReadinessV1(
        &exact_message, &exact_queue, svc_q2pro_setting, &invalid_record));
    CHECK(!memcmp(&exact_message, &exact_message_struct_before,
                  sizeof(exact_message)));
    CHECK(!memcmp(&exact_queue, &exact_queue_struct_before,
                  sizeof(exact_queue)));
    CHECK(!memcmp(exact_message_data, exact_message_before,
                  sizeof(exact_message_data)));
    CHECK(!memcmp(exact_queue_data, exact_queue_before,
                  sizeof(exact_queue_data)));
}

static void test_queue_failure_disables_without_native_traffic(void)
{
    netchan_t chan;
    netchan_t committed_chan;
    netchan_t postqueue_chan;
    sv_native_shadow_peer_v1 peer;
    sv_native_shadow_peer_v1 committed_peer;
    sv_native_shadow_peer_v1 postqueue_peer;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 first_postqueue_challenge;
    worr_native_readiness_record_v1 postqueue_challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    command_carrier_fixture fixture;
    netchan_app_rx_output_v1_t rx_output;
    static const byte legacy[] = {0x71, 0x72};

    init_chan(&chan);
    CHECK(SV_NativeShadowPeerInitV1(&peer, &chan, 30));
    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 30, native_public_mask(&peer),
        native_public_mask(&peer), 30, &challenge));
    /* Production reaches this path only if the exact staged append fails;
     * readiness state and both hooks are then retired before native bytes. */
    SV_NativeShadowPeerDisableV1(
        &peer, SV_NATIVE_SHADOW_FAILURE_QUEUE);
    CHECK(!SV_NativeShadowPeerEnabledV1(&peer));
    CHECK(peer.readiness_initialized == 0);
    CHECK(peer.last_failure == SV_NATIVE_SHADOW_FAILURE_QUEUE);
    CHECK(chan.app_tx_prepare == NULL && chan.app_rx == NULL);
    SV_NativeShadowPeerDestroyV1(&peer);

    /* Once SERVER_ACTIVE crossed the reliable-queue boundary, native wire
     * commitment is irreversible for this connection.  A later failure keeps
     * both hooks so a legitimate late WTC1 is stripped safely in DRAIN. */
    activate_peer(
        &committed_peer, &committed_chan, 31, 40, &challenge);
    CHECK(committed_peer.native_wire_committed == 1);
    SV_NativeShadowPeerDisableV1(
        &committed_peer, SV_NATIVE_SHADOW_FAILURE_QUEUE);
    CHECK(!SV_NativeShadowPeerEnabledV1(&committed_peer) &&
          committed_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          committed_chan.app_tx_prepare != NULL &&
          committed_chan.app_rx != NULL);
    build_command_carrier(
        &fixture, challenge.transport_epoch, 31, 1, 1, 18,
        legacy, sizeof(legacy));
    CHECK(receive_application(
              &committed_chan, &committed_peer, fixture.packet,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy) &&
          committed_peer.rx_commits == 0 &&
          committed_peer.rx_drained == 1 &&
          committed_peer.last_failure == SV_NATIVE_SHADOW_FAILURE_QUEUE &&
          committed_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN);
    SV_NativeShadowPeerDestroyV1(&committed_peer);

    /* The postqueue API is itself the point of no return.  Inject a binding
     * failure after SERVER_ACTIVE was notionally transferred: hooks must stay
     * attached, and a valid carrier for that exact committed epoch must still
     * expose its authoritative legacy prefix without a native commit. */
    activate_peer(
        &postqueue_peer, &postqueue_chan, 32, 50,
        &first_postqueue_challenge);
    CHECK(postqueue_peer.wire_committed_transport_epoch ==
          first_postqueue_challenge.transport_epoch);
    CHECK(SV_NativeShadowBeginEpochV1(
        &postqueue_peer, 33, native_public_mask(&postqueue_peer),
        native_public_mask(&postqueue_peer), 52,
        &postqueue_challenge));
    make_client_ready(
        &postqueue_challenge, postqueue_peer.clock_ticks, &client_ready);
    feed_ready(
        &postqueue_peer, 53, &client_ready, &server_active);
    CHECK(postqueue_peer.activation_pending == 1 &&
          postqueue_peer.native_wire_committed == 1 &&
          postqueue_peer.wire_committed_transport_epoch ==
              first_postqueue_challenge.transport_epoch);
    postqueue_peer.readiness.negotiated_capabilities =
        WORR_NET_CAP_LEGACY_STAGE_MASK;
    CHECK(!SV_NativeShadowServerActiveQueuedV1(&postqueue_peer));
    CHECK(postqueue_peer.native_wire_committed == 1 &&
          postqueue_peer.wire_committed_transport_epoch ==
              postqueue_challenge.transport_epoch &&
          postqueue_peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          postqueue_chan.app_tx_prepare != NULL &&
          postqueue_chan.app_rx != NULL &&
          postqueue_peer.transport_initialized == 0);
    build_command_carrier(
        &fixture, postqueue_challenge.transport_epoch, 33, 1, 1, 19,
        legacy, sizeof(legacy));
    CHECK(receive_application(
              &postqueue_chan, &postqueue_peer, fixture.packet,
              fixture.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == sizeof(legacy) &&
          postqueue_peer.rx_commits == 0 &&
          postqueue_peer.rx_drained == 1);
    SV_NativeShadowPeerDestroyV1(&postqueue_peer);
}

static void test_uncommitted_advanced_epoch_is_rejected(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 active_challenge;
    worr_native_readiness_record_v1 waiting_challenge;
    sv_native_shadow_status_v1 status;
    command_carrier_fixture fixture;
    netchan_app_rx_output_v1_t rx_output;
    static const byte legacy[] = {0x75, 0x76};

    activate_peer(&peer, &chan, 34, 3000, &active_challenge);
    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 35, native_public_mask(&peer),
        native_public_mask(&peer), 3001, &waiting_challenge));
    CHECK(peer.private_transport_epoch == waiting_challenge.transport_epoch &&
          peer.wire_committed_transport_epoch ==
              active_challenge.transport_epoch &&
          peer.transport_initialized == 0 &&
          peer.retired_transport_initialized == 0 &&
          peer.cancelled_through_transport_epoch ==
              active_challenge.transport_epoch);
    CHECK(SV_NativeShadowGetStatusV1(&peer, 3001, &status));
    CHECK(status.transport_epoch == waiting_challenge.transport_epoch &&
          status.wire_committed == 1 &&
          status.wire_committed_transport_epoch ==
              active_challenge.transport_epoch &&
          status.wire_committed_transport_epoch !=
              status.transport_epoch);
    build_command_carrier(
        &fixture, waiting_challenge.transport_epoch, 35, 1, 1, 21,
        legacy, sizeof(legacy));
    CHECK(receive_application(
              &chan, &peer, fixture.packet, fixture.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_REJECT);
    CHECK(peer.rx_rejections == 1 && peer.rx_drained == 0 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          peer.last_failure == SV_NATIVE_SHADOW_FAILURE_CARRIER);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_idle_admission_clock_preserves_same_packet_join(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;
    command_carrier_fixture fixture;
    netchan_app_rx_output_v1_t rx_output;
    static const byte legacy[] = {0x73, 0x74};
    const uint32_t admitted_time = 2000;

    activate_peer(&peer, &chan, 33, 1000, &challenge);
    build_command_carrier(
        &fixture, challenge.transport_epoch, 33, 1, 1, 20,
        legacy, sizeof(legacy));

    /* The idle gap exceeds the 500 ms join lifetime.  Admission must advance
     * the clock before RX stores the native half; reconciliation at the same
     * raw packet time then matches instead of pruning a falsely old entry. */
    CHECK(admitted_time - 1000u > SV_NATIVE_SHADOW_JOIN_EXPIRY_MS);
    CHECK(SV_NativeShadowAdvanceAdmissionClockV1(
        &peer, admitted_time));
    CHECK(receive_application(
              &chan, &peer, fixture.packet, fixture.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.pending_native_valid == 1 && peer.join_expiries == 0);
    CHECK(SV_NativeShadowObserveLegacyCommandV1(
        &peer, &fixture.record, admitted_time));
    CHECK(peer.pending_native_valid == 0 && peer.join_expiries == 0 &&
          peer.command_matches == 1 &&
          peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_deadline_double_begin_and_dangling(void)
{
    netchan_t deadline_chan;
    netchan_t begin_chan;
    netchan_t dangling_chan;
    sv_native_shadow_peer_v1 deadline_peer;
    sv_native_shadow_peer_v1 begin_peer;
    sv_native_shadow_peer_v1 dangling_peer;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];

    init_chan(&deadline_chan);
    CHECK(SV_NativeShadowPeerInitV1(&deadline_peer, &deadline_chan, 0));
    CHECK(SV_NativeShadowBeginEpochV1(
        &deadline_peer, 40, native_public_mask(&deadline_peer),
        native_public_mask(&deadline_peer), 0, &challenge));
    CHECK(!SV_NativeShadowPacketBeginV1(
        &deadline_peer, (uint32_t)SV_NATIVE_SHADOW_TIMEOUT_MS));
    CHECK(deadline_peer.last_failure ==
          SV_NATIVE_SHADOW_FAILURE_READINESS);
    SV_NativeShadowPeerDestroyV1(&deadline_peer);

    init_chan(&begin_chan);
    CHECK(SV_NativeShadowPeerInitV1(&begin_peer, &begin_chan, 0));
    CHECK(SV_NativeShadowBeginEpochV1(
        &begin_peer, 41, native_public_mask(&begin_peer),
        native_public_mask(&begin_peer), 0, &challenge));
    CHECK(SV_NativeShadowPacketBeginV1(&begin_peer, 1));
    CHECK(!SV_NativeShadowPacketBeginV1(&begin_peer, 2));
    CHECK(!SV_NativeShadowPeerEnabledV1(&begin_peer));
    SV_NativeShadowPeerDestroyV1(&begin_peer);

    init_chan(&dangling_chan);
    CHECK(SV_NativeShadowPeerInitV1(&dangling_peer, &dangling_chan, 0));
    CHECK(SV_NativeShadowBeginEpochV1(
        &dangling_peer, 42, native_public_mask(&dangling_peer),
        native_public_mask(&dangling_peer), 0, &challenge));
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &challenge, pairs, WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT));
    CHECK(SV_NativeShadowPacketBeginV1(&dangling_peer, 1));
    CHECK(SV_NativeShadowObserveSettingV1(
              &dangling_peer, pairs[0].index, pairs[0].value,
              &challenge) == SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED);
    CHECK(!SV_NativeShadowPacketEndV1(&dangling_peer));
    CHECK(!SV_NativeShadowPeerEnabledV1(&dangling_peer));
    SV_NativeShadowPeerDestroyV1(&dangling_peer);
}

static void test_occupied_hooks_are_not_replaced(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 owner;
    sv_native_shadow_peer_v1 rejected;
    sv_native_shadow_status_v1 status;
    netchan_app_tx_prepare_fn prepare;
    netchan_app_rx_fn receive;

    init_chan(&chan);
    CHECK(SV_NativeShadowPeerInitV1(&owner, &chan, 1));
    prepare = chan.app_tx_prepare;
    receive = chan.app_rx;
    memset(&rejected, 0xa5, sizeof(rejected));
    CHECK(!SV_NativeShadowPeerInitV1(&rejected, &chan, 1));
    CHECK(chan.app_tx_prepare == prepare && chan.app_rx == receive);
    CHECK(chan.app_tx_opaque == &owner && chan.app_rx_opaque == &owner);
    chan.app_rx_opaque = &rejected;
    CHECK(!SV_NativeShadowPeerEnabledV1(&owner));
    CHECK(SV_NativeShadowGetStatusV1(&owner, 1, &status));
    CHECK(status.hooks_attached == 0);
    chan.app_rx_opaque = &owner;
    CHECK(SV_NativeShadowPeerEnabledV1(&owner));
    owner.reserved1 = 1;
    SV_NativeShadowPeerDestroyV1(&owner);
    CHECK(chan.app_tx_prepare == NULL && chan.app_rx == NULL);
}

static void test_owner_nonreuse_and_old_channel_rejection(void)
{
    netchan_t first_chan;
    netchan_t second_chan;
    netchan_t old_chan;
    sv_native_shadow_peer_v1 first;
    sv_native_shadow_peer_v1 second;
    sv_native_shadow_peer_v1 old;

    init_chan(&first_chan);
    init_chan(&second_chan);
    memset(&old_chan, 0, sizeof(old_chan));
    old_chan.type = NETCHAN_OLD;
    CHECK(SV_NativeShadowPeerInitV1(&first, &first_chan, 1));
    CHECK(SV_NativeShadowPeerInitV1(&second, &second_chan, 1));
    CHECK(second.connection_owner_id > first.connection_owner_id);
    CHECK(!SV_NativeShadowPeerInitV1(&old, &old_chan, 1));
    CHECK(old_chan.app_tx_prepare == NULL && old_chan.app_rx == NULL);
    SV_NativeShadowPeerDestroyV1(&first);
    SV_NativeShadowPeerDestroyV1(&second);
}

static void test_nonmutating_ack_status_and_async_telemetry(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    sv_native_shadow_peer_v1 before;
    sv_native_shadow_status_v1 status;
    worr_native_readiness_record_v1 challenge;
    command_carrier_fixture fixture;
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_tx_prepare_output_v1_t tx_output;
    byte candidate[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];

    activate_peer(&peer, &chan, 90, 9000, &challenge);
    build_command_carrier(
        &fixture, challenge.transport_epoch, 90, 1, 1, 33,
        NULL, 0);
    CHECK(receive_application(
              &chan, &peer, fixture.packet, fixture.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_commits == 1);

    before = peer;
    CHECK(SV_NativeShadowAckEligiblePeekV1(&peer, 9002));
    CHECK(!memcmp(&peer, &before, sizeof(peer)));
    CHECK(!SV_NativeShadowAckEligiblePeekV1(&peer, 8000));
    CHECK(!memcmp(&peer, &before, sizeof(peer)));

    memset(&status, 0xa5, sizeof(status));
    CHECK(SV_NativeShadowGetStatusV1(&peer, 9002, &status));
    CHECK(!memcmp(&peer, &before, sizeof(peer)));
    CHECK(status.struct_size == sizeof(status) &&
          status.schema_version == SV_NATIVE_SHADOW_STATUS_VERSION &&
          status.reserved0 == 0 &&
          status.cancelled_through_transport_epoch == 0 &&
          status.reserved_counter_alignment == 0 &&
          status.last_failure_detail == 0);
    CHECK(status.enabled == 1 &&
          status.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          status.hooks_attached == 1 &&
          status.readiness_phase ==
              WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    CHECK(status.official_connection_epoch == 90 &&
          status.transport_epoch == challenge.transport_epoch &&
          status.public_capabilities == native_public_mask(&peer) &&
          status.private_capabilities ==
              WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK &&
          status.wire_committed == 1 &&
          status.wire_committed_transport_epoch ==
              challenge.transport_epoch &&
          status.ack_eligible == 1);
    CHECK(status.challenges_queued == 1 &&
          status.client_ready_records == 1 &&
           status.server_active_records == 1 &&
           status.rx_carriers == 1 && status.rx_commits == 1 &&
           status.rx_rejections == 0 && status.rx_drained == 0 &&
           status.drain_entries == 0 && status.failures == 0 &&
           status.last_failure == SV_NATIVE_SHADOW_FAILURE_NONE);
    CHECK(status.cancellation_barriers == 1 &&
          status.cancelled_transports == 0 &&
          status.cancelled_rx_messages == 0 &&
          status.cancelled_receipts == 0 &&
          status.cancelled_event_records == 0 &&
          status.stale_cancelled_carriers == 0 &&
          status.stale_cancelled_readiness_records == 0);

    SV_NativeShadowRecordAsyncRateDeferralV1(&peer);
    SV_NativeShadowRecordAsyncFragmentDeferralV1(&peer);
    CHECK(SV_NativeShadowBeginAsyncWakeV1(&peer));
    CHECK(!SV_NativeShadowBeginAsyncWakeV1(&peer));
    SV_NativeShadowEndAsyncWakeV1(&peer);
    CHECK(peer.async_wake_attempts == 1 &&
          peer.async_wake_no_handoff == 1 &&
          peer.async_ack_handoffs == 0);
    CHECK(SV_NativeShadowBeginAsyncWakeV1(&peer));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, candidate, sizeof(candidate),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, candidate);
    SV_NativeShadowEndAsyncWakeV1(&peer);
    CHECK(peer.async_rate_deferrals == 1 &&
          peer.async_fragment_deferrals == 1 &&
          peer.async_wake_attempts == 2 &&
          peer.async_ack_handoffs == 1 &&
          peer.async_wake_no_handoff == 1 &&
          peer.tx_ack_handoffs == 1);

    /* A callback-confirmed handoff remains observable when its cumulative
     * counter has saturated. */
    CHECK(receive_application(
              &chan, &peer, fixture.packet, fixture.packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_repeat_refreshes == 1);
    CHECK(SV_NativeShadowAckDueV1(&peer, 9003));
    peer.tx_ack_handoffs = UINT64_MAX;
    CHECK(SV_NativeShadowBeginAsyncWakeV1(&peer));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, candidate, sizeof(candidate),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, candidate);
    SV_NativeShadowEndAsyncWakeV1(&peer);
    CHECK(peer.tx_ack_handoffs == UINT64_MAX &&
          peer.async_wake_attempts == 3 &&
          peer.async_ack_handoffs == 2 &&
          peer.async_wake_no_handoff == 1);

    peer.async_rate_deferrals = UINT64_MAX;
    SV_NativeShadowRecordAsyncRateDeferralV1(&peer);
    CHECK(peer.async_rate_deferrals == UINT64_MAX);
    CHECK(SV_NativeShadowGetStatusV1(&peer, 9002, &status));
    CHECK(status.async_rate_deferrals == UINT64_MAX &&
          status.async_fragment_deferrals == 1 &&
          status.async_wake_attempts == 3 &&
          status.async_ack_handoffs == 2 &&
          status.async_wake_no_handoff == 1 &&
          status.tx_ack_handoffs == UINT64_MAX &&
          status.drain_entries == 0);

    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_event_wait_confirm_accepts_command_rx(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 active_confirm;
    command_carrier_fixture command;
    netchan_app_rx_output_v1_t output;
    sv_native_shadow_event_status_v1 status;

    begin_event_peer_wait_confirm(
        &peer, &chan, 300, 10000, NULL, &challenge,
        &active_confirm);
    build_command_carrier(
        &command, challenge.transport_epoch, 300, 1, 1, 0x31,
        NULL, 0);

    /* The hook runs before the legacy parser.  Stamp one packet time, admit
     * its command DATA in WAIT_CONFIRM, then consume the fourth readiness
     * record at the same exact connection tick. */
    CHECK(SV_NativeShadowAdvanceAdmissionClockV1(
        &peer, 10002));
    CHECK(receive_application(
              &chan, &peer, command.packet, command.packet_bytes,
              &output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.readiness.phase ==
              WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM &&
          peer.rx_commits == 1 && peer.pending_native_valid == 1 &&
          peer.transport.ack_ledger.receipt_count == 1);
    feed_active_confirm(&peer, 10002, &active_confirm);
    CHECK(peer.readiness.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE &&
          peer.client_active_confirm_records == 1);
    CHECK(SV_NativeShadowGetEventStatusV1(&peer, 10002, &status));
    CHECK(status.tx_open == 1 && status.output_due == 1);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_event_mode_mixed_tx_ack_release_and_retirement(void)
{
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 active_confirm;
    worr_native_readiness_record_v1 next_challenge;
    sv_native_shadow_event_status_v1 status;
    sv_native_shadow_event_status_v1 before_status;
    worr_event_record_v1 candidate_event;
    command_carrier_fixture command_ack;
    command_carrier_fixture retired_data;
    netchan_app_rx_output_v1_t rx_output;
    netchan_app_tx_prepare_output_v1_t tx_output;
    netchan_app_tx_prepare_output_v1_t tx_output_before;
    worr_native_envelope_frame_info_v1 frame;
    byte packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte packet_before[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte legacy_761[761];
    byte ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte *huge_legacy;
    byte *huge_candidate;
    byte *huge_before;
    size_t ack_packet_bytes;
    uint32_t old_transport_epoch;
    uint32_t descriptor_message;
    uint32_t event_message;
    uint64_t drained_before;

    begin_event_peer_wait_confirm(
        &peer, &chan, 301, 11000, NULL, &challenge,
        &active_confirm);
    old_transport_epoch = challenge.transport_epoch;
    candidate_event = make_event_candidate(
        50, 7, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP);
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &peer, &candidate_event, 1, 11001));
    CHECK(SV_NativeShadowGetEventStatusV1(&peer, 11001, &status));
    CHECK(status.backlog_count == 1 && status.retained_count == 1 &&
          status.tx_open == 0 && status.output_due == 0);

    memset(packet, 0x41, sizeof(packet));
    memcpy(packet_before, packet, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(memcmp(packet, packet_before, sizeof(packet)) == 0);

    feed_active_confirm(&peer, 11002, &active_confirm);
    CHECK(SV_NativeShadowOutputDueV1(&peer, 11002));
    CHECK(SV_NativeShadowGetEventStatusV1(&peer, 11002, &before_status));
    CHECK(before_status.tx_open == 1 && before_status.output_due == 1 &&
          before_status.packets_prepared == 0);

    /* Ordinary legacy pressure is a retained retry, never a native fault. */
    memset(legacy_761, 0x51, sizeof(legacy_761));
    memset(packet, 0x52, sizeof(packet));
    memcpy(packet_before, packet, sizeof(packet));
    memset(&tx_output, 0xa5, sizeof(tx_output));
    memcpy(&tx_output_before, &tx_output, sizeof(tx_output));
    CHECK(prepare_application(
              &chan, &peer, legacy_761, sizeof(legacy_761), packet,
              1024, &tx_output) == NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(memcmp(packet, packet_before, sizeof(packet)) == 0 &&
          memcmp(&tx_output, &tx_output_before, sizeof(tx_output)) == 0 &&
          SV_NativeShadowPeerEnabledV1(&peer));
    CHECK(SV_NativeShadowGetEventStatusV1(&peer, 11002, &status));
    CHECK(status.packets_prepared == before_status.packets_prepared &&
          status.retained_count == before_status.retained_count &&
          status.backlog_count == before_status.backlog_count);

    /* Wider synthetic hook lengths fail before the uint16 carrier boundary
     * and leave sender/output/candidate bytes untouched. */
    huge_legacy = malloc(70000);
    huge_candidate = malloc(70000);
    huge_before = malloc(70000);
    CHECK(huge_legacy != NULL && huge_candidate != NULL &&
          huge_before != NULL);
    memset(huge_legacy, 0x61, 70000);
    memset(huge_candidate, 0x62, 70000);
    memcpy(huge_before, huge_candidate, 70000);
    memset(&tx_output, 0xa5, sizeof(tx_output));
    memcpy(&tx_output_before, &tx_output, sizeof(tx_output));
    CHECK(prepare_application(
              &chan, &peer, huge_legacy, 70000, huge_candidate,
              70000, &tx_output) == NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(memcmp(huge_candidate, huge_before, 70000) == 0 &&
          memcmp(&tx_output, &tx_output_before, sizeof(tx_output)) == 0 &&
          SV_NativeShadowPeerEnabledV1(&peer));
    free(huge_before);
    free(huge_candidate);
    free(huge_legacy);

    /* An empty async handoff now emits the descriptor DATA. */
    memset(packet, 0, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    decode_single_data_frame(
        packet, tx_output.application_bytes, &frame);
    CHECK(frame.record.record_class ==
          WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
    descriptor_message = frame.message_sequence;
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, packet);

    /* One current packet may carry one COMMAND DATA plus the exact event ACK.
     * Both halves commit, while FIFO promotion remains deferred to the
     * following output-due/TX-selection boundary. */
    build_command_ack_carrier(
        &command_ack, old_transport_epoch, 301, 1, 1, 0x32,
        descriptor_message, descriptor_message);
    CHECK(SV_NativeShadowAdvanceAdmissionClockV1(&peer, 11003));
    CHECK(receive_application(
              &chan, &peer, command_ack.packet,
              command_ack.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_commits == 1 && peer.pending_native_valid == 1);
    CHECK(SV_NativeShadowGetEventStatusV1(&peer, 11003, &status));
    CHECK(status.descriptor_acked == 1 && status.backlog_count == 1 &&
          status.retained_count == 0 && status.candidates_promoted == 0 &&
          status.descriptors_acknowledged == 1 && status.output_due == 1);

    CHECK(SV_NativeShadowOutputDueV1(&peer, 11003));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    decode_single_data_frame(
        packet, tx_output.application_bytes, &frame);
    CHECK(frame.record.record_class == WORR_NATIVE_RECORD_EVENT_V1);
    event_message = frame.message_sequence;
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, packet);

    CHECK(SV_NativeShadowBeginEpochV1(
        &peer, 302, native_public_mask(&peer),
        native_public_mask(&peer), 11004, &next_challenge));
    CHECK(next_challenge.negotiated_capabilities ==
          WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK);
    CHECK(SV_NativeShadowGetEventStatusV1(&peer, 11004, &status));
    CHECK(status.sender_initialized == 0 &&
          status.retired_sender_initialized == 0 &&
          status.retained_count == 0 &&
          status.retired_retained_count == 0 &&
          peer.cancelled_through_transport_epoch ==
              old_transport_epoch &&
          peer.cancelled_transports == 1 &&
          peer.cancelled_event_records == 1);

    drained_before = peer.rx_drained;
    encode_ack_range_carrier(
        old_transport_epoch, event_message, event_message,
        ack_packet, sizeof(ack_packet), &ack_packet_bytes);
    CHECK(receive_application(
              &chan, &peer, ack_packet, ack_packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(SV_NativeShadowGetEventStatusV1(&peer, 11004, &status));
    CHECK(status.retired_retained_count == 0 &&
          peer.rx_drained == drained_before + 1 &&
          peer.stale_cancelled_carriers == 1);

    drained_before = peer.rx_drained;
    build_command_carrier(
        &retired_data, old_transport_epoch, 301, 2, 2, 0x33,
        NULL, 0);
    CHECK(receive_application(
              &chan, &peer, retired_data.packet,
              retired_data.packet_bytes, &rx_output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(peer.rx_drained == drained_before + 1 &&
          peer.pending_native_valid == 0);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_event_schema2_batch_status_export(void)
{
    enum {
        RAW_TIME_MS = 11500,
        OFFICIAL_EPOCH = 350,
    };
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 active_confirm;
    sv_native_shadow_event_status_v1 status;
    worr_event_record_v1 candidates[5];
    netchan_app_tx_prepare_output_v1_t tx_output;
    netchan_app_rx_output_v1_t rx_output;
    worr_native_envelope_frame_info_v1 frame;
    byte packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t ack_packet_bytes;
    uint32_t descriptor_message;
    uint32_t index;

    begin_event_peer_wait_confirm(
        &peer, &chan, OFFICIAL_EPOCH, RAW_TIME_MS, NULL,
        &challenge, &active_confirm);
    feed_active_confirm(&peer, RAW_TIME_MS + 2u, &active_confirm);
    CHECK(SV_NativeShadowOutputDueV1(&peer, RAW_TIME_MS + 2u));
    memset(packet, 0, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    decode_single_data_frame(
        packet, tx_output.application_bytes, &frame);
    CHECK(frame.record.record_class ==
          WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
    descriptor_message = frame.message_sequence;
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, packet);

    for (index = 0; index < 5; ++index) {
        candidates[index] = make_event_candidate(
            700, 70u + index, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP);
    }
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &peer, candidates, 2, RAW_TIME_MS + 2u));

    encode_ack_range_carrier(
        challenge.transport_epoch, descriptor_message,
        descriptor_message, ack_packet, sizeof(ack_packet),
        &ack_packet_bytes);
    CHECK(receive_application(
              &chan, &peer, ack_packet, ack_packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == 0);
    CHECK(SV_NativeShadowGetEventStatusV1(
        &peer, RAW_TIME_MS + 2u, &status));
    CHECK(status.descriptor_acked == 1 && status.backlog_count == 2 &&
          status.retained_count == 0 &&
          status.schema2_batches_promoted == 0 &&
          status.schema2_events_promoted == 0);

    /* The descriptor ACK between two independent capture sites must release
     * credit without promoting the first prefix of this exact fence. */
    CHECK(SV_NativeShadowQueueEventCandidatesV1(
        &peer, candidates + 2, 3, RAW_TIME_MS + 3u));
    CHECK(SV_NativeShadowGetEventStatusV1(
        &peer, RAW_TIME_MS + 3u, &status));
    CHECK(status.backlog_count == 5 && status.retained_count == 0 &&
          status.candidates_promoted == 0 &&
          status.schema2_batches_promoted == 0 &&
          status.schema2_events_promoted == 0 && status.output_due == 1);

    memset(packet, 0, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    decode_single_data_frame(
        packet, tx_output.application_bytes, &frame);
    CHECK(frame.record.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
          frame.record.record_schema_version ==
              WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA &&
          frame.record.object_sequence == 5 && frame.fragment_count == 1);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, packet);
    CHECK(SV_NativeShadowGetEventStatusV1(
        &peer, RAW_TIME_MS + 3u, &status));
    CHECK(status.backlog_count == 0 && status.retained_count == 1 &&
          status.candidates_queued == 5 &&
          status.candidates_promoted == 5 &&
          status.schema2_batches_promoted == 1 &&
          status.schema2_events_promoted == 5 &&
          status.first_sends == 6);

    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_snapshot_event_transport_adds_final_emission_fence(void)
{
    enum {
        SNAPSHOT_EPOCH = 93,
        RAW_TIME_MS = 11800,
        OFFICIAL_EPOCH = 375,
    };
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 active_confirm;
    sv_snapshot_shadow_config_v1 shadow_config;
    sv_snapshot_shadow_peer_v1 *shadow;
    snapshot_frame_fixture frame_fixture;
    sv_snapshot_shadow_ref_v1 snapshot_ref;
    worr_event_record_v1 projection_candidate;
    worr_event_record_v1 transport_record;
    netchan_app_tx_prepare_output_v1_t tx_output;
    netchan_app_rx_output_v1_t rx_output;
    worr_native_envelope_frame_info_v1 frame;
    const byte *payload;
    byte packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t payload_bytes = 0;
    size_t ack_packet_bytes = 0;
    uint32_t descriptor_message;
    uint32_t candidate_count = 0;

    begin_event_peer_wait_confirm(
        &peer, &chan, OFFICIAL_EPOCH, RAW_TIME_MS, NULL,
        &challenge, &active_confirm);
    feed_active_confirm(&peer, RAW_TIME_MS + 2u, &active_confirm);
    CHECK(SV_NativeShadowOutputDueV1(&peer, RAW_TIME_MS + 2u));
    memset(packet, 0, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    decode_single_data_frame(
        packet, tx_output.application_bytes, &frame);
    CHECK(frame.record.record_class ==
          WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
    descriptor_message = frame.message_sequence;
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, packet);
    encode_ack_range_carrier(
        challenge.transport_epoch, descriptor_message,
        descriptor_message, ack_packet, sizeof(ack_packet),
        &ack_packet_bytes);
    CHECK(receive_application(
              &chan, &peer, ack_packet, ack_packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);

    shadow_config = make_snapshot_shadow_config(SNAPSHOT_EPOCH);
    shadow = SV_SnapshotShadowCreateV1(&shadow_config);
    CHECK(shadow != NULL);
    init_snapshot_frame(&frame_fixture, 610);
    frame_fixture.deltas[5].entity_delta.delta_bits |= Q2P_ESD_EVENT;
    frame_fixture.deltas[5].entity_delta.event =
        WORR_EVENT_LEGACY_ENTITY_FOOTSTEP;
    snapshot_ref = commit_snapshot_frame(
        shadow, &frame_fixture, 6100, UINT64_C(61000000));

    CHECK(SV_SnapshotShadowCopyEventCandidatesV1(
              shadow, snapshot_ref, &projection_candidate, 1,
              &candidate_count) == SV_SNAPSHOT_EVENT_CANDIDATES_OK);
    CHECK(candidate_count == 1 &&
          (projection_candidate.flags &
           WORR_EVENT_FLAG_SNAPSHOT_FENCED) == 0);

    CHECK(SV_NativeShadowQueueSnapshotEventsV1(
        &peer, shadow, snapshot_ref, RAW_TIME_MS + 3u));
    memset(packet, 0, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    payload = decode_single_data_payload(
        packet, tx_output.application_bytes, &frame, &payload_bytes);
    CHECK(frame.record.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
          frame.record.record_schema_version == WORR_EVENT_ABI_VERSION);
    CHECK(Worr_NativeCodecEventDecodeV1(
              payload, payload_bytes, MAX_EDICTS, &transport_record) ==
          WORR_NATIVE_CODEC_OK);
    CHECK((transport_record.flags & WORR_EVENT_FLAG_SNAPSHOT_FENCED) != 0 &&
          transport_record.event_id.stream_epoch != 0 &&
          transport_record.event_id.sequence != 0 &&
          (transport_record.flags &
           ~(WORR_EVENT_FLAG_SNAPSHOT_FENCED |
             WORR_EVENT_FLAG_HAS_AUTHORITY_ID)) ==
              projection_candidate.flags &&
          transport_record.source_tick == projection_candidate.source_tick &&
          transport_record.source_time_us ==
              projection_candidate.source_time_us &&
          transport_record.source_entity.index ==
              projection_candidate.source_entity.index &&
          transport_record.source_entity.generation ==
              projection_candidate.source_entity.generation);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, packet);

    SV_SnapshotShadowDestroyV1(shadow);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static uint16_t send_snapshot_burst(
    netchan_t *chan,
    sv_native_shadow_peer_v1 *peer,
    uint32_t snapshot_epoch,
    uint32_t snapshot_sequence,
    uint32_t *message_sequence_io)
{
    netchan_app_tx_prepare_output_v1_t tx_output;
    worr_native_envelope_frame_info_v1 frame;
    byte packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint16_t expected_fragment_count = 0;
    uint16_t fragment_index = 0;

    CHECK(chan != NULL && peer != NULL &&
          message_sequence_io != NULL);
    do {
        memset(packet, 0, sizeof(packet));
        CHECK(prepare_application(
                  chan, peer, NULL, 0, packet, sizeof(packet),
                  &tx_output) ==
              NETCHAN_APP_TX_PREPARE_PREPARED);
        memset(&frame, 0, sizeof(frame));
        decode_single_data_frame(
            packet, tx_output.application_bytes, &frame);
        CHECK(frame.record.record_class ==
                  WORR_NATIVE_RECORD_SNAPSHOT_V1 &&
              frame.record.object_epoch == snapshot_epoch &&
              frame.record.object_sequence == snapshot_sequence &&
              frame.fragment_index == fragment_index);
        if (fragment_index == 0) {
            CHECK(frame.fragment_count > 1);
            expected_fragment_count = frame.fragment_count;
            if (*message_sequence_io == 0)
                *message_sequence_io = frame.message_sequence;
        }
        CHECK(frame.fragment_count == expected_fragment_count &&
              frame.message_sequence == *message_sequence_io);
        complete_application(
            chan, peer, &tx_output,
            NETCHAN_APP_TX_COMPLETION_ACCEPTED, packet);
        ++fragment_index;
    } while (fragment_index < expected_fragment_count);
    return expected_fragment_count;
}

static void test_reliable_keyed_poi_unified_fifo(void)
{
    enum {
        RAW_TIME_MS = 11850,
        OFFICIAL_EPOCH = 380,
        SPAWNCOUNT = 76,
    };
    const uint8_t first_game[] = {
        svc_muzzleflash, 5, 0, WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN,
    };
    const uint8_t last_game[] = {
        svc_muzzleflash, 7, 0, WORR_EVENT_PLAYER_MUZZLE_SHOTGUN,
    };
    byte reliable_poi[WORR_LEGACY_KEYED_POI_RAW_SIZE];
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 active_confirm;
    sv_snapshot_shadow_config_v1 shadow_config;
    sv_snapshot_shadow_peer_v1 *shadow;
    snapshot_frame_fixture frame_fixture;
    sv_snapshot_shadow_ref_v1 snapshot_ref;
    sv_native_shadow_event_status_v1 status;
    netchan_app_tx_prepare_output_v1_t tx_output;
    netchan_app_rx_output_v1_t rx_output;
    worr_native_envelope_frame_info_v1 envelope;
    worr_native_event_batch_info_v1 batch_info;
    worr_event_record_v1 records[3];
    worr_event_payload_muzzle_v1 first_payload;
    worr_event_payload_keyed_poi_v1 poi_payload;
    worr_event_payload_muzzle_v1 last_payload;
    const byte *payload;
    byte packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t payload_bytes = 0;
    size_t ack_packet_bytes = 0;
    uint32_t descriptor_message;

    make_raw_keyed_poi(reliable_poi, 8192, 0, 321, 208);
    begin_event_peer_wait_confirm(
        &peer, &chan, OFFICIAL_EPOCH, RAW_TIME_MS, NULL,
        &challenge, &active_confirm);
    CHECK(SV_NativeShadowCaptureReliableGameEventsV1(
              &peer, SPAWNCOUNT, first_game, sizeof(first_game)) ==
          SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_CAPTURED);
    CHECK(SV_NativeShadowCaptureReliableKeyedPOIV1(
              &peer, SPAWNCOUNT, reliable_poi,
              sizeof(reliable_poi)) ==
          SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_CAPTURED);
    CHECK(SV_NativeShadowCaptureReliableGameEventsV1(
              &peer, SPAWNCOUNT, last_game, sizeof(last_game)) ==
          SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_CAPTURED);

    shadow_config = make_snapshot_shadow_config(90);
    shadow = SV_SnapshotShadowCreateV1(&shadow_config);
    CHECK(shadow != NULL);
    init_snapshot_frame(&frame_fixture, 485);
    snapshot_ref = commit_snapshot_frame(
        shadow, &frame_fixture, 4850, UINT64_C(48500000));
    CHECK(SV_NativeShadowFlushReliableEventsV1(
        &peer, shadow, snapshot_ref, SNAPSHOT_TEST_MAX_ENTITIES,
        SPAWNCOUNT, RAW_TIME_MS + 2u));
    CHECK(SV_NativeShadowGetEventStatusV1(
        &peer, RAW_TIME_MS + 2u, &status));
    CHECK(status.backlog_count == 3 && status.candidates_queued == 3);

    feed_active_confirm(&peer, RAW_TIME_MS + 2u, &active_confirm);
    memset(packet, 0, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    decode_single_data_frame(
        packet, tx_output.application_bytes, &envelope);
    CHECK(envelope.record.record_class ==
          WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1);
    descriptor_message = envelope.message_sequence;
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, packet);
    encode_ack_range_carrier(
        challenge.transport_epoch, descriptor_message,
        descriptor_message, ack_packet, sizeof(ack_packet),
        &ack_packet_bytes);
    CHECK(receive_application(
              &chan, &peer, ack_packet, ack_packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);

    memset(packet, 0, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    payload = decode_single_data_payload(
        packet, tx_output.application_bytes, &envelope,
        &payload_bytes);
    CHECK(envelope.record.record_class == WORR_NATIVE_RECORD_EVENT_V1 &&
          envelope.record.record_schema_version ==
              WORR_NATIVE_EVENT_BATCH_RECORD_SCHEMA);
    memset(&batch_info, 0, sizeof(batch_info));
    CHECK(Worr_NativeEventBatchDecodeV1(
              payload, payload_bytes, SNAPSHOT_TEST_MAX_ENTITIES,
              records, 3, &batch_info) == WORR_NATIVE_EVENT_BATCH_OK);
    CHECK(batch_info.event_count == 3 &&
          records[0].event_id.sequence + 1u ==
              records[1].event_id.sequence &&
          records[1].event_id.sequence + 1u ==
              records[2].event_id.sequence &&
          records[0].payload_kind == WORR_EVENT_PAYLOAD_MUZZLE_V1 &&
          records[1].payload_kind == WORR_EVENT_PAYLOAD_KEYED_POI_V1 &&
          records[2].payload_kind == WORR_EVENT_PAYLOAD_MUZZLE_V1);
    memcpy(&first_payload, records[0].payload, sizeof(first_payload));
    memcpy(&poi_payload, records[1].payload, sizeof(poi_payload));
    memcpy(&last_payload, records[2].payload, sizeof(last_payload));
    CHECK(first_payload.flash_id == WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN &&
          poi_payload.key == 8192 && poi_payload.lifetime_ms == 0 &&
          poi_payload.image_index == 321 && poi_payload.color_index == 208 &&
          last_payload.flash_id == WORR_EVENT_PLAYER_MUZZLE_SHOTGUN);
    CHECK(records[1].source_entity.index == 0 &&
          records[1].source_entity.generation == 1 &&
          records[1].subject_entity.index == 1 &&
          records[1].subject_entity.generation == 1 &&
          records[1].delivery_class ==
              WORR_EVENT_DELIVERY_RELIABLE_ORDERED &&
          records[1].expiry_tick == 0);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, packet);

    /* Map quiesce must clear a newly captured deferred entry before a later
     * map can bind it against a reused entity generation. */
    CHECK(SV_NativeShadowCaptureReliableKeyedPOIV1(
              &peer, SPAWNCOUNT + 1u, reliable_poi,
              sizeof(reliable_poi)) ==
          SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_CAPTURED);
    CHECK(SV_NativeShadowPeerQuiesceMapV1(&peer));
    CHECK(SV_NativeShadowFlushReliableEventsV1(
        &peer, shadow, snapshot_ref, SNAPSHOT_TEST_MAX_ENTITIES,
        SPAWNCOUNT + 1u, RAW_TIME_MS + 3u));

    SV_SnapshotShadowDestroyV1(shadow);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_reliable_mixed_game_event_snapshot_fence(void)
{
    enum {
        RAW_TIME_MS = 11900,
        OFFICIAL_EPOCH = 390,
        SPAWNCOUNT = 77,
    };
    const uint8_t reliable_sequence[] = {
        svc_muzzleflash, 5, 0,
        WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN | MZ_SILENCED,
        svc_muzzleflash, 7, 0,
        WORR_EVENT_PLAYER_MUZZLE_SHOTGUN,
        svc_temp_entity, WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT, 23,
    };
    const uint8_t unsupported_sequence[] = {svc_nop};
    byte reliable_poi[WORR_LEGACY_KEYED_POI_RAW_SIZE];
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 active_confirm;
    sv_snapshot_shadow_config_v1 shadow_config;
    sv_snapshot_shadow_peer_v1 *shadow;
    snapshot_frame_fixture frame_fixture;
    sv_snapshot_shadow_ref_v1 snapshot_ref;
    sv_native_shadow_event_status_v1 status;
    uint32_t index;

    begin_event_peer_wait_confirm(
        &peer, &chan, OFFICIAL_EPOCH, RAW_TIME_MS, NULL,
        &challenge, &active_confirm);
    make_raw_keyed_poi(reliable_poi, 8192, 5000, 321, 208);
    CHECK(SV_NativeShadowCaptureReliableGameEventsV1(
              &peer, SPAWNCOUNT, unsupported_sequence,
              sizeof(unsupported_sequence)) ==
          SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_IGNORED);
    CHECK(SV_NativeShadowCaptureReliableGameEventsV1(
              &peer, SPAWNCOUNT, reliable_sequence,
              sizeof(reliable_sequence)) ==
          SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_CAPTURED);

    shadow_config = make_snapshot_shadow_config(91);
    shadow = SV_SnapshotShadowCreateV1(&shadow_config);
    CHECK(shadow != NULL);
    init_snapshot_frame(&frame_fixture, 490);
    snapshot_ref = commit_snapshot_frame(
        shadow, &frame_fixture, 4900, UINT64_C(49000000));
    CHECK(SV_NativeShadowFlushReliableGameEventsV1(
        &peer, shadow, snapshot_ref, SNAPSHOT_TEST_MAX_ENTITIES,
        SPAWNCOUNT, RAW_TIME_MS + 2u));
    CHECK(SV_NativeShadowGetEventStatusV1(
        &peer, RAW_TIME_MS + 2u, &status));
    CHECK(status.backlog_count == 3 && status.retained_count == 1 &&
          status.candidates_queued == 3);

    /* A flush is one-shot, and a map-generation mismatch cannot borrow the
     * later snapshot's entity generations. */
    CHECK(SV_NativeShadowFlushReliableGameEventsV1(
        &peer, shadow, snapshot_ref, SNAPSHOT_TEST_MAX_ENTITIES,
        SPAWNCOUNT, RAW_TIME_MS + 2u));
    CHECK(SV_NativeShadowCaptureReliableGameEventsV1(
              &peer, SPAWNCOUNT - 1u, reliable_sequence,
              sizeof(reliable_sequence)) ==
          SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_CAPTURED);
    CHECK(SV_NativeShadowFlushReliableGameEventsV1(
        &peer, shadow, snapshot_ref, SNAPSHOT_TEST_MAX_ENTITIES,
        SPAWNCOUNT, RAW_TIME_MS + 2u));
    CHECK(SV_NativeShadowGetEventStatusV1(
        &peer, RAW_TIME_MS + 2u, &status));
    CHECK(status.backlog_count == 3 && status.candidates_queued == 3);

    for (index = 0;
         index < SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_BATCH_CAPACITY;
         ++index) {
        if ((index & 1u) == 0) {
            CHECK(SV_NativeShadowCaptureReliableGameEventsV1(
                      &peer, SPAWNCOUNT, reliable_sequence,
                      sizeof(reliable_sequence)) ==
                  SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_CAPTURED);
        } else {
            CHECK(SV_NativeShadowCaptureReliableKeyedPOIV1(
                      &peer, SPAWNCOUNT, reliable_poi,
                      sizeof(reliable_poi)) ==
                  SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_CAPTURED);
        }
    }
    CHECK(SV_NativeShadowCaptureReliableGameEventsV1(
              &peer, SPAWNCOUNT, reliable_sequence,
              sizeof(reliable_sequence)) ==
          SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_CAPACITY);
    CHECK(SV_NativeShadowFlushReliableGameEventsV1(
        &peer, shadow, snapshot_ref, SNAPSHOT_TEST_MAX_ENTITIES,
        SPAWNCOUNT, RAW_TIME_MS + 2u));
    CHECK(SV_NativeShadowGetEventStatusV1(
        &peer, RAW_TIME_MS + 2u, &status));
    CHECK(status.backlog_count == 67 && status.candidates_queued == 67);

    SV_SnapshotShadowDestroyV1(shadow);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_snapshot_mode_fail_closed_bindings(void)
{
    netchan_t missing_epoch_chan;
    netchan_t wrong_mode_chan;
    sv_native_shadow_peer_v1 missing_epoch_peer;
    sv_native_shadow_peer_v1 wrong_mode_peer;
    worr_native_readiness_record_v1 challenge;

    init_chan(&missing_epoch_chan);
    CHECK(SV_NativeShadowPeerInitModeV1(
        &missing_epoch_peer, &missing_epoch_chan, 12000,
        SV_NATIVE_SHADOW_MODE_SNAPSHOT));
    CHECK(missing_epoch_peer.snapshot_state != NULL &&
          missing_epoch_peer.event_state == NULL);
    CHECK(!SV_NativeShadowBeginEpochV1(
        &missing_epoch_peer, 400,
        native_public_mask(&missing_epoch_peer),
        native_public_mask(&missing_epoch_peer), 12000, &challenge));
    CHECK(!SV_NativeShadowPeerEnabledV1(&missing_epoch_peer) &&
          missing_epoch_peer.last_failure ==
              SV_NATIVE_SHADOW_FAILURE_OFFICIAL_BINDING);
    SV_NativeShadowPeerDestroyV1(&missing_epoch_peer);

    init_chan(&wrong_mode_chan);
    CHECK(SV_NativeShadowPeerInitModeV1(
        &wrong_mode_peer, &wrong_mode_chan, 12010,
        SV_NATIVE_SHADOW_MODE_COMMAND));
    CHECK(!SV_NativeShadowBeginEpochBoundV1(
        &wrong_mode_peer, 401,
        native_public_mask(&wrong_mode_peer),
        native_public_mask(&wrong_mode_peer), 77, 12010,
        &challenge));
    CHECK(!SV_NativeShadowPeerEnabledV1(&wrong_mode_peer) &&
          wrong_mode_peer.last_failure ==
              SV_NATIVE_SHADOW_FAILURE_OFFICIAL_BINDING);
    SV_NativeShadowPeerDestroyV1(&wrong_mode_peer);
}

static void test_snapshot_mode_multifragment_retry_ack(void)
{
    enum {
        SNAPSHOT_EPOCH = 77,
        OFFICIAL_EPOCH = 402,
        RAW_TIME_MS = 12100,
    };
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_state_v1 client;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_record_v1 active_confirm;
    sv_snapshot_shadow_config_v1 shadow_config;
    sv_snapshot_shadow_config_v1 wrong_shadow_config;
    sv_snapshot_shadow_peer_v1 *shadow;
    sv_snapshot_shadow_peer_v1 *wrong_shadow;
    snapshot_frame_fixture frame_fixture;
    snapshot_frame_fixture wrong_frame_fixture;
    sv_snapshot_shadow_ref_v1 snapshot_ref;
    sv_snapshot_shadow_ref_v1 wrong_snapshot_ref;
    sv_snapshot_shadow_sent_v1 sent;
    sv_native_shadow_snapshot_status_v1 status;
    netchan_app_tx_prepare_output_v1_t tx_output;
    netchan_app_rx_output_v1_t rx_output;
    worr_native_envelope_frame_info_v1 retry_frame;
    byte packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte packet_before[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t ack_packet_bytes = 0;
    uint32_t message_sequence = 0;
    uint16_t first_fragment_count;
    uint16_t retry_fragment_count;

    init_chan(&chan);
    CHECK(SV_NativeShadowPeerInitModeV1(
        &peer, &chan, RAW_TIME_MS,
        SV_NATIVE_SHADOW_MODE_SNAPSHOT));
    CHECK(peer.snapshot_state != NULL && peer.event_state == NULL);
    CHECK(SV_NativeShadowBeginEpochBoundV1(
        &peer, OFFICIAL_EPOCH,
        native_public_mask(&peer), native_public_mask(&peer), SNAPSHOT_EPOCH,
        RAW_TIME_MS, &challenge));
    CHECK(challenge.negotiated_capabilities ==
              WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK &&
          challenge.snapshot_epoch == SNAPSHOT_EPOCH);
    CHECK(Worr_NativeReadinessClientInitBoundV1(
              &client, challenge.transport_epoch, SNAPSHOT_EPOCH,
              challenge.negotiated_capabilities, peer.clock_ticks,
              SV_NATIVE_SHADOW_TIMEOUT_MS) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &challenge, peer.clock_ticks,
              &client_ready) == WORR_NATIVE_READINESS_OK);
    feed_ready(
        &peer, RAW_TIME_MS + 1u, &client_ready, &server_active);
    CHECK(server_active.snapshot_epoch == SNAPSHOT_EPOCH &&
          peer.readiness.phase ==
              WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM);
    CHECK(SV_NativeShadowServerActiveQueuedV1(&peer));
    CHECK(peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
          peer.transport_initialized == 1);
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &peer, RAW_TIME_MS + 1u, &status));
    CHECK(status.mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT &&
          status.sender_initialized == 1 &&
          status.snapshot_epoch == SNAPSHOT_EPOCH &&
          status.tx_open == 0 && status.output_due == 0 &&
          status.retained_count == 0);

    shadow_config = make_snapshot_shadow_config(SNAPSHOT_EPOCH);
    shadow = SV_SnapshotShadowCreateV1(&shadow_config);
    CHECK(shadow != NULL);
    init_snapshot_frame(&frame_fixture, 500);
    snapshot_ref = commit_snapshot_frame(
        shadow, &frame_fixture, 5000, UINT64_C(50000000));
    memset(&sent, 0, sizeof(sent));
    CHECK(SV_SnapshotShadowGetSentV1(
              shadow, snapshot_ref, &sent) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(sent.snapshot.snapshot_id.epoch == SNAPSHOT_EPOCH);
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &peer, shadow, snapshot_ref, RAW_TIME_MS + 2u));
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &peer, RAW_TIME_MS + 2u, &status));
    CHECK(status.snapshot_epoch == SNAPSHOT_EPOCH &&
          status.snapshots_queued == 1 &&
          status.retained_count == 1 &&
          status.active_snapshot.epoch == SNAPSHOT_EPOCH &&
          status.active_snapshot.sequence ==
              sent.snapshot.snapshot_id.sequence &&
          status.active_payload_bytes > 0 &&
          status.tx_open == 0 && status.output_due == 0);

    memset(packet, 0x41, sizeof(packet));
    memcpy(packet_before, packet, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(memcmp(packet, packet_before, sizeof(packet)) == 0);

    CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &client, &server_active, peer.clock_ticks,
              &active_confirm) == WORR_NATIVE_READINESS_OK);
    CHECK(active_confirm.snapshot_epoch == SNAPSHOT_EPOCH);
    feed_active_confirm(
        &peer, RAW_TIME_MS + 3u, &active_confirm);
    CHECK(peer.readiness.phase ==
              WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE &&
          peer.client_active_confirm_records == 1);
    CHECK(SV_NativeShadowOutputDueV1(
        &peer, RAW_TIME_MS + 3u));
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &peer, RAW_TIME_MS + 3u, &status));
    CHECK(status.active_confirms == 1 && status.tx_open == 1 &&
          status.output_due == 1 && status.retained_count == 1);

    first_fragment_count = send_snapshot_burst(
        &chan, &peer, SNAPSHOT_EPOCH,
        sent.snapshot.snapshot_id.sequence, &message_sequence);
    CHECK(first_fragment_count > 1 && message_sequence != 0);
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &peer, RAW_TIME_MS + 3u, &status));
    CHECK(status.retained_count == 1 &&
          status.packets_confirmed == first_fragment_count &&
          status.first_sends == 1 && status.retries == 0);

    CHECK(SV_NativeShadowOutputDueV1(
        &peer, RAW_TIME_MS + 3u +
                   SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS));
    memset(packet, 0, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    memset(&retry_frame, 0, sizeof(retry_frame));
    decode_single_data_frame(
        packet, tx_output.application_bytes, &retry_frame);
    CHECK(retry_frame.message_sequence == message_sequence &&
          retry_frame.fragment_index == 0 &&
          retry_frame.fragment_count == first_fragment_count);
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED, packet);
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &peer, RAW_TIME_MS + 3u +
                   SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS,
        &status));
    CHECK(status.retained_count == 1 &&
          status.packets_rejected == 1 &&
          status.retries == 0);

    retry_fragment_count = send_snapshot_burst(
        &chan, &peer, SNAPSHOT_EPOCH,
        sent.snapshot.snapshot_id.sequence, &message_sequence);
    CHECK(retry_fragment_count == first_fragment_count);
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &peer, RAW_TIME_MS + 3u +
                   SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS,
        &status));
    CHECK(status.retained_count == 1 &&
          status.packets_confirmed ==
              (uint64_t)first_fragment_count +
                  retry_fragment_count &&
          status.packets_rejected == 1 &&
          status.first_sends == 1 && status.retries == 1);

    encode_ack_range_carrier(
        challenge.transport_epoch, message_sequence,
        message_sequence, ack_packet, sizeof(ack_packet),
        &ack_packet_bytes);
    CHECK(receive_application(
              &chan, &peer, ack_packet, ack_packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(rx_output.legacy_bytes == 0);
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &peer, RAW_TIME_MS + 3u +
                   SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS,
        &status));
    CHECK(status.retained_count == 0 &&
          status.active_payload_bytes == 0 &&
          status.acknowledgements_applied == 1 &&
          status.payloads_released == 1 &&
          status.output_due == 0);

    wrong_shadow_config =
        make_snapshot_shadow_config(SNAPSHOT_EPOCH + 1u);
    wrong_shadow =
        SV_SnapshotShadowCreateV1(&wrong_shadow_config);
    CHECK(wrong_shadow != NULL);
    init_snapshot_frame(&wrong_frame_fixture, 501);
    wrong_snapshot_ref = commit_snapshot_frame(
        wrong_shadow, &wrong_frame_fixture, 5001,
        UINT64_C(50010000));
    CHECK(!SV_NativeShadowQueueSnapshotV1(
        &peer, wrong_shadow, wrong_snapshot_ref,
        RAW_TIME_MS + 4u +
            SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS));
    CHECK(peer.lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN &&
          peer.last_failure ==
              SV_NATIVE_SHADOW_FAILURE_SNAPSHOT_PROJECTION &&
          !SV_NativeShadowPeerEnabledV1(&peer));

    SV_SnapshotShadowDestroyV1(wrong_shadow);
    SV_SnapshotShadowDestroyV1(shadow);
    SV_NativeShadowPeerDestroyV1(&peer);
}

static void test_combined_mode_fair_data_and_disjoint_ack_ranges(void)
{
    enum {
        SNAPSHOT_EPOCH = 88,
        OFFICIAL_EPOCH = 450,
        RAW_TIME_MS = 13000,
    };
    netchan_t chan;
    sv_native_shadow_peer_v1 peer;
    worr_native_readiness_state_v1 client;
    worr_native_readiness_record_v1 challenge;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_record_v1 active_confirm;
    sv_snapshot_shadow_config_v1 shadow_config;
    sv_snapshot_shadow_peer_v1 *shadow;
    snapshot_frame_fixture frame_fixture;
    sv_snapshot_shadow_ref_v1 snapshot_ref;
    sv_snapshot_shadow_sent_v1 sent;
    sv_native_shadow_event_status_v1 event_status;
    sv_native_shadow_snapshot_status_v1 snapshot_status;
    netchan_app_tx_prepare_output_v1_t tx_output;
    netchan_app_rx_output_v1_t rx_output;
    worr_native_envelope_frame_info_v1 frame;
    byte packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    byte ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t ack_packet_bytes = 0;
    uint32_t snapshot_message_sequence = 0;
    uint32_t event_message_sequence;

    init_chan(&chan);
    CHECK(SV_NativeShadowPeerInitModeV1(
        &peer, &chan, RAW_TIME_MS,
        SV_NATIVE_SHADOW_MODE_EVENT_SNAPSHOT));
    CHECK(peer.event_state != NULL && peer.snapshot_state != NULL);
    CHECK(SV_NativeShadowBeginEpochBoundV1(
        &peer, OFFICIAL_EPOCH, native_public_mask(&peer),
        native_public_mask(&peer), SNAPSHOT_EPOCH,
        RAW_TIME_MS, &challenge));
    CHECK(challenge.negotiated_capabilities ==
              WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PRIVATE_MASK &&
          challenge.snapshot_epoch == SNAPSHOT_EPOCH);
    CHECK(Worr_NativeReadinessClientInitBoundV1(
              &client, challenge.transport_epoch, SNAPSHOT_EPOCH,
              challenge.negotiated_capabilities, peer.clock_ticks,
              SV_NATIVE_SHADOW_TIMEOUT_MS) ==
          WORR_NATIVE_READINESS_OK);
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &challenge, peer.clock_ticks, &client_ready) ==
          WORR_NATIVE_READINESS_OK);
    feed_ready(
        &peer, RAW_TIME_MS + 1u, &client_ready, &server_active);
    CHECK(SV_NativeShadowServerActiveQueuedV1(&peer));
    CHECK(SV_NativeShadowGetEventStatusV1(
        &peer, RAW_TIME_MS + 1u, &event_status));
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &peer, RAW_TIME_MS + 1u, &snapshot_status));
    CHECK(event_status.mode == SV_NATIVE_SHADOW_MODE_EVENT_SNAPSHOT &&
          event_status.sender_initialized == 1 &&
          event_status.retained_count == 1);
    CHECK(snapshot_status.mode ==
              SV_NATIVE_SHADOW_MODE_EVENT_SNAPSHOT &&
          snapshot_status.sender_initialized == 1 &&
          snapshot_status.retained_count == 0);

    shadow_config = make_snapshot_shadow_config(SNAPSHOT_EPOCH);
    shadow = SV_SnapshotShadowCreateV1(&shadow_config);
    CHECK(shadow != NULL);
    init_snapshot_frame(&frame_fixture, 600);
    snapshot_ref = commit_snapshot_frame(
        shadow, &frame_fixture, 6000, UINT64_C(60000000));
    memset(&sent, 0, sizeof(sent));
    CHECK(SV_SnapshotShadowGetSentV1(
              shadow, snapshot_ref, &sent) == SV_SNAPSHOT_SHADOW_OK);
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &peer, shadow, snapshot_ref, RAW_TIME_MS + 2u));

    CHECK(Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
              &client, &server_active, peer.clock_ticks,
              &active_confirm) == WORR_NATIVE_READINESS_OK);
    feed_active_confirm(&peer, RAW_TIME_MS + 3u, &active_confirm);

    memset(packet, 0, sizeof(packet));
    CHECK(prepare_application(
              &chan, &peer, NULL, 0, packet, sizeof(packet),
              &tx_output) == NETCHAN_APP_TX_PREPARE_PREPARED);
    memset(&frame, 0, sizeof(frame));
    decode_single_data_frame(
        packet, tx_output.application_bytes, &frame);
    CHECK(frame.record.record_class ==
              WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1 &&
          frame.message_sequence == 1u);
    event_message_sequence = frame.message_sequence;
    complete_application(
        &chan, &peer, &tx_output,
        NETCHAN_APP_TX_COMPLETION_ACCEPTED, packet);

    CHECK(send_snapshot_burst(
              &chan, &peer, SNAPSHOT_EPOCH,
              sent.snapshot.snapshot_id.sequence,
              &snapshot_message_sequence) > 1);
    CHECK(snapshot_message_sequence ==
          WORR_NATIVE_COMBINED_SNAPSHOT_MESSAGE_SEQUENCE_FIRST);

    encode_ack_range_carrier(
        challenge.transport_epoch, event_message_sequence,
        event_message_sequence, ack_packet, sizeof(ack_packet),
        &ack_packet_bytes);
    CHECK(receive_application(
              &chan, &peer, ack_packet, ack_packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(SV_NativeShadowGetEventStatusV1(
        &peer, RAW_TIME_MS + 3u, &event_status));
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &peer, RAW_TIME_MS + 3u, &snapshot_status));
    CHECK(event_status.retained_count == 0 &&
          event_status.descriptor_acked == 1 &&
          snapshot_status.retained_count == 1);

    encode_ack_range_carrier(
        challenge.transport_epoch, snapshot_message_sequence,
        snapshot_message_sequence, ack_packet, sizeof(ack_packet),
        &ack_packet_bytes);
    CHECK(receive_application(
              &chan, &peer, ack_packet, ack_packet_bytes,
              &rx_output) == NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &peer, RAW_TIME_MS + 3u, &snapshot_status));
    CHECK(snapshot_status.retained_count == 0 &&
          snapshot_status.acknowledgements_applied == 1);

    SV_SnapshotShadowDestroyV1(shadow);
    SV_NativeShadowPeerDestroyV1(&peer);
}

int main(void)
{
    CHECK(SV_NATIVE_SHADOW_SVC_WIRE_BYTES ==
          WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT * 9u);
    test_hooks_and_handshake();
    test_post_bootstrap_queue_idle_gate();
    test_challenge_queue_deadline();
    test_epoch_advance_inside_packet();
    test_pending_challenge_epoch_is_cancelled();
    test_command_observation_ack_and_drain();
    test_input_batch_confirmation_and_fallbacks();
    test_input_batch_ack_gate_and_highwater();
    test_input_batch_mismatch_expiry_and_malformed();
    test_input_batch_legacy_history_reconciliation();
    test_many_sequential_commands_and_count_saturation();
    test_fresh_message_command_id_replay_drains();
    test_legacy_first_reconcile_mismatch_and_expiry();
    test_epoch_cancellation_routing();
    test_map_quiesce_cancels_pending_command_before_stream_reset();
    test_map_quiesce_busy_peer_fails_closed();
    test_carrier_rejections_and_completion_provenance();
    test_clock_wrap_and_regression();
    test_binding_and_interleaving_fail_closed();
    test_svc_append_capacity_and_wire();
    test_queue_failure_disables_without_native_traffic();
    test_uncommitted_advanced_epoch_is_rejected();
    test_idle_admission_clock_preserves_same_packet_join();
    test_deadline_double_begin_and_dangling();
    test_occupied_hooks_are_not_replaced();
    test_owner_nonreuse_and_old_channel_rejection();
    test_nonmutating_ack_status_and_async_telemetry();
    test_event_wait_confirm_accepts_command_rx();
    test_event_mode_mixed_tx_ack_release_and_retirement();
    test_event_schema2_batch_status_export();
    test_snapshot_event_transport_adds_final_emission_fence();
    test_reliable_keyed_poi_unified_fifo();
    test_reliable_mixed_game_event_snapshot_fence();
    test_snapshot_mode_fail_closed_bindings();
    test_snapshot_mode_multifragment_retry_ack();
    test_combined_mode_fair_data_and_disjoint_ack_ranges();
    puts("native_server_shadow_pilot_test: ok");
    return EXIT_SUCCESS;
}
