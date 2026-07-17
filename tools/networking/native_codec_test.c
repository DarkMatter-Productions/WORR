/* Deterministic FR-10-T04 canonical native codec tests. */

#include "common/net/native_codec.h"

#include <stdio.h>
#include <string.h>

#define TEST_MAX_ENTITIES 64u
#define TEST_BUFFER_BYTES WORR_NATIVE_CODEC_MAX_ENCODED_BYTES

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "native_codec_test:%d: %s\n", __LINE__,     \
                    #condition);                                           \
            return false;                                                  \
        }                                                                  \
    } while (0)

static void store_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static worr_command_record_v1 make_command(void)
{
    worr_command_record_v1 record;
    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id.epoch = UINT32_C(0x01020304);
    record.command_id.sequence = UINT32_C(0x11223344);
    record.sample_time_us = UINT64_C(0x0102030405060708);
    record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    record.command.struct_size = sizeof(record.command);
    record.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    record.command.duration_ms = 16;
    record.command.buttons = 3;
    record.command.view_angles[0] = 0.0f;
    record.command.view_angles[1] = 90.0f;
    record.command.view_angles[2] = -90.0f;
    record.command.forward_move = 100.0f;
    record.command.side_move = -50.0f;
    record.render_watermark.struct_size = sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    record.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
    record.render_watermark.source_server_tick = 300;
    record.render_watermark.tick_interval_us = 16667;
    record.render_watermark.source_server_time_us = UINT64_C(5000000);
    record.render_watermark.rendered_server_time_us = UINT64_C(5000000);
    return record;
}

static bool test_command_codec(void)
{
    worr_command_record_v1 source = make_command();
    worr_command_record_v1 decoded;
    worr_command_record_v1 sentinel;
    worr_native_codec_info_v1 info;
    worr_native_codec_info_v1 info_sentinel;
    worr_native_record_ref_v1 record_ref;
    uint8_t encoded[256];
    uint8_t malformed[256];
    uint8_t small[256];
    uint8_t small_before[256];
    uint64_t source_semantic;
    uint64_t decoded_semantic;
    uint64_t source_content;
    uint64_t decoded_content;
    uint32_t preflight;
    uint32_t truncate;
    size_t encoded_bytes = 0;
    size_t size_sentinel = SIZE_MAX;

    memset(encoded, 0xa5, sizeof(encoded));
    CHECK(Worr_CommandRecordValidateV1(&source, 250));
    CHECK(Worr_NativeCodecCommandPreflightV1(&source, 250, &preflight) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(preflight == 110);
    memcpy(small, encoded, sizeof(small));
    memcpy(small_before, small, sizeof(small));
    CHECK(Worr_NativeCodecCommandEncodeV1(
              &source, 250, small, preflight - 1u, &size_sentinel) ==
          WORR_NATIVE_CODEC_OUTPUT_TOO_SMALL);
    CHECK(size_sentinel == SIZE_MAX);
    CHECK(memcmp(small, small_before, sizeof(small)) == 0);
    CHECK(Worr_NativeCodecCommandEncodeV1(
              &source, 250, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes == preflight);
    CHECK(memcmp(encoded, "WNC1", 4) == 0);
    CHECK(encoded[4] == 1 && encoded[5] == 0);
    CHECK(encoded[36] == 0x04 && encoded[37] == 0x03 &&
          encoded[38] == 0x02 && encoded[39] == 0x01);
    CHECK(encoded[40] == 0x44 && encoded[41] == 0x33 &&
          encoded[42] == 0x22 && encoded[43] == 0x11);
    CHECK(encoded[48] == 0x08 && encoded[49] == 0x07 &&
          encoded[55] == 0x01);
    CHECK(Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(info.record_class == WORR_NATIVE_RECORD_COMMAND_V1);
    CHECK(info.encoded_bytes == encoded_bytes);
    CHECK(info.fixed_body_bytes ==
          WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &record_ref));
    CHECK(record_ref.object_epoch == source.command_id.epoch);
    CHECK(record_ref.object_sequence == source.command_id.sequence);
    memset(&decoded, 0xcc, sizeof(decoded));
    CHECK(Worr_NativeCodecCommandDecodeV1(
              encoded, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_CommandRecordSemanticHashV1(
        &source, 250, &source_semantic));
    CHECK(Worr_CommandRecordSemanticHashV1(
        &decoded, 250, &decoded_semantic));
    CHECK(Worr_CommandRecordContentHashV1(&source, 250, &source_content));
    CHECK(Worr_CommandRecordContentHashV1(&decoded, 250, &decoded_content));
    CHECK(source_semantic == decoded_semantic);
    CHECK(source_content == decoded_content);

    memset(&sentinel, 0x5a, sizeof(sentinel));
    memset(&info_sentinel, 0x3c, sizeof(info_sentinel));
    memcpy(&decoded, &sentinel, sizeof(decoded));
    memcpy(malformed, encoded, encoded_bytes);
    malformed[9] = 1;
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_MALFORMED);
    CHECK(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
    for (truncate = 0; truncate < encoded_bytes; ++truncate) {
        info = info_sentinel;
        CHECK(Worr_NativeCodecInspectV1(encoded, truncate, &info) !=
              WORR_NATIVE_CODEC_OK);
        CHECK(memcmp(&info, &info_sentinel, sizeof(info)) == 0);
        decoded = sentinel;
        CHECK(Worr_NativeCodecCommandDecodeV1(
                  encoded, truncate, 250, &decoded) !=
              WORR_NATIVE_CODEC_OK);
        CHECK(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
    }
    memcpy(malformed, encoded, encoded_bytes);
    malformed[0] = 'X';
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_MALFORMED);
    memcpy(malformed, encoded, encoded_bytes);
    malformed[6] = 49;
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_UNSUPPORTED);
    memcpy(malformed, encoded, encoded_bytes);
    malformed[8] = 99;
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_UNSUPPORTED);
    memcpy(malformed, encoded, encoded_bytes);
    malformed[10] = 2;
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_UNSUPPORTED);
    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 12, 2);
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_UNSUPPORTED);
    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 20,
              WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES - 1u);
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_MALFORMED);
    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 24, 1);
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_MALFORMED);
    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 36, 0);
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_MALFORMED);
    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 44, 1);
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_MALFORMED);
    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 58, UINT32_C(0x80000000));
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_MALFORMED);
    memcpy(malformed, encoded, encoded_bytes);
    malformed[4] = 2;
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_UNSUPPORTED);
    CHECK(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 16, (uint32_t)encoded_bytes - 1u);
    CHECK(Worr_NativeCodecCommandDecodeV1(
              malformed, encoded_bytes, 250, &decoded) ==
          WORR_NATIVE_CODEC_MALFORMED);
    CHECK(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
    info = info_sentinel;
    CHECK(Worr_NativeCodecInspectV1(encoded, encoded_bytes - 1u, &info) ==
          WORR_NATIVE_CODEC_MALFORMED);
    CHECK(memcmp(&info, &info_sentinel, sizeof(info)) == 0);
    return true;
}

static worr_event_record_v1 make_event_base(uint32_t sequence)
{
    worr_event_record_v1 record;
    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
                   WORR_EVENT_FLAG_REPLAY_SAFE |
                   WORR_EVENT_FLAG_PRESENT_ONCE;
    record.event_id.stream_epoch = 7;
    record.event_id.sequence = sequence;
    record.source_tick = 1000 + sequence;
    record.source_ordinal = sequence;
    record.source_time_us = UINT64_C(9000000) + sequence;
    record.source_entity.index = 1;
    record.source_entity.generation = 4;
    record.subject_entity.index = WORR_EVENT_NO_ENTITY;
    record.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    record.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    return record;
}

static worr_event_record_v1 make_event_kind(uint16_t kind,
                                            uint32_t sequence)
{
    worr_event_record_v1 record = make_event_base(sequence);
    union {
        worr_event_payload_vec3_v1 vec3;
        worr_event_payload_entity_ref_v1 entity;
        worr_event_payload_damage_v1 damage;
        worr_event_payload_audio_v1 audio;
        worr_event_payload_effect_v1 effect;
        worr_event_payload_u32x4_v1 u32x4;
        worr_event_payload_legacy_entity_v1 legacy_entity;
        worr_event_payload_legacy_temp_v1 legacy_temp;
        worr_event_payload_muzzle_v1 muzzle;
        worr_event_payload_spatial_audio_v1 spatial;
    } payload;
    uint16_t temp_fields = 0;

    memset(&payload, 0, sizeof(payload));
    record.payload_kind = kind;
    switch (kind) {
    case WORR_EVENT_PAYLOAD_NONE:
        record.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
        record.payload_size = 0;
        break;
    case WORR_EVENT_PAYLOAD_VEC3:
        record.event_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
        payload.vec3.value[0] = 1.0f;
        payload.vec3.value[1] = 2.0f;
        payload.vec3.value[2] = 3.0f;
        record.payload_size = sizeof(payload.vec3);
        memcpy(record.payload, &payload.vec3, sizeof(payload.vec3));
        break;
    case WORR_EVENT_PAYLOAD_ENTITY_REF:
        record.event_type = WORR_EVENT_TYPE_STATE_CHANGE;
        payload.entity.entity.index = 2;
        payload.entity.entity.generation = 9;
        record.payload_size = sizeof(payload.entity);
        memcpy(record.payload, &payload.entity, sizeof(payload.entity));
        break;
    case WORR_EVENT_PAYLOAD_DAMAGE:
        record.event_type = WORR_EVENT_TYPE_DAMAGE;
        payload.damage.amount = 25.0f;
        payload.damage.impulse = 4.0f;
        payload.damage.direction[0] = 1.0f;
        payload.damage.point[1] = 2.0f;
        payload.damage.damage_flags = 3;
        payload.damage.means_of_death = 6;
        record.payload_size = sizeof(payload.damage);
        memcpy(record.payload, &payload.damage, sizeof(payload.damage));
        break;
    case WORR_EVENT_PAYLOAD_AUDIO:
        record.event_type = WORR_EVENT_TYPE_AUDIO_CUE;
        payload.audio.asset_id = 12;
        payload.audio.channel = 2;
        payload.audio.volume = 1.0f;
        payload.audio.attenuation = 1.0f;
        payload.audio.pitch = 1.25f;
        record.payload_size = sizeof(payload.audio);
        memcpy(record.payload, &payload.audio, sizeof(payload.audio));
        break;
    case WORR_EVENT_PAYLOAD_EFFECT:
        record.event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
        payload.effect.effect_id = 8;
        payload.effect.variant = 3;
        payload.effect.origin[0] = 1.0f;
        payload.effect.direction[2] = -1.0f;
        record.payload_size = sizeof(payload.effect);
        memcpy(record.payload, &payload.effect, sizeof(payload.effect));
        break;
    case WORR_EVENT_PAYLOAD_U32X4:
        record.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
        payload.u32x4.value[0] = UINT32_C(0x11223344);
        payload.u32x4.value[1] = 2;
        payload.u32x4.value[2] = 3;
        payload.u32x4.value[3] = 4;
        record.payload_size = sizeof(payload.u32x4);
        memcpy(record.payload, &payload.u32x4, sizeof(payload.u32x4));
        break;
    case WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1:
        record.event_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
        payload.legacy_entity.raw_event = WORR_EVENT_LEGACY_ENTITY_FOOTSTEP;
        payload.legacy_entity.flags =
            WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
        record.payload_size = sizeof(payload.legacy_entity);
        memcpy(record.payload, &payload.legacy_entity,
               sizeof(payload.legacy_entity));
        break;
    case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1:
        record.event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
        payload.legacy_temp.subtype = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
        (void)Worr_EventLegacyTempFieldMaskV1(
            payload.legacy_temp.subtype, 0, &temp_fields);
        payload.legacy_temp.valid_fields = temp_fields;
        payload.legacy_temp.position1[0] = 3.0f;
        payload.legacy_temp.direction[1] = 1.0f;
        record.payload_size = sizeof(payload.legacy_temp);
        memcpy(record.payload, &payload.legacy_temp,
               sizeof(payload.legacy_temp));
        break;
    case WORR_EVENT_PAYLOAD_MUZZLE_V1:
        record.event_type = WORR_EVENT_TYPE_WEAPON_FIRE;
        payload.muzzle.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
        payload.muzzle.flash_id = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
        payload.muzzle.flags = WORR_EVENT_MUZZLE_FLAG_SILENCED;
        record.payload_size = sizeof(payload.muzzle);
        memcpy(record.payload, &payload.muzzle, sizeof(payload.muzzle));
        break;
    case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1:
        record.event_type = WORR_EVENT_TYPE_AUDIO_CUE;
        payload.spatial.asset_id = 99;
        payload.spatial.raw_entity = WORR_EVENT_NO_ENTITY;
        payload.spatial.volume = 1.0f;
        payload.spatial.attenuation = 2.0f;
        payload.spatial.time_offset = 0.1f;
        payload.spatial.pitch = 0.75f;
        record.payload_size = sizeof(payload.spatial);
        memcpy(record.payload, &payload.spatial, sizeof(payload.spatial));
        break;
    default:
        break;
    }
    return record;
}

static bool test_event_codecs(void)
{
    static const uint16_t kinds[] = {
        WORR_EVENT_PAYLOAD_NONE,
        WORR_EVENT_PAYLOAD_VEC3,
        WORR_EVENT_PAYLOAD_ENTITY_REF,
        WORR_EVENT_PAYLOAD_DAMAGE,
        WORR_EVENT_PAYLOAD_AUDIO,
        WORR_EVENT_PAYLOAD_EFFECT,
        WORR_EVENT_PAYLOAD_U32X4,
        WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1,
        WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
        WORR_EVENT_PAYLOAD_MUZZLE_V1,
        WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1,
    };
    uint8_t encoded[256];
    uint8_t malformed[256];
    worr_event_record_v1 source;
    worr_event_record_v1 decoded;
    worr_event_record_v1 sentinel;
    worr_native_codec_info_v1 info;
    uint64_t source_hash;
    uint64_t decoded_hash;
    uint32_t preflight;
    size_t encoded_bytes;
    uint32_t index;

    for (index = 0; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        source = make_event_kind(kinds[index], index + 1u);
        CHECK(Worr_EventRecordValidateV1(&source, TEST_MAX_ENTITIES));
        CHECK(Worr_NativeCodecEventPreflightV1(
                  &source, TEST_MAX_ENTITIES, &preflight) ==
              WORR_NATIVE_CODEC_OK);
        memset(encoded, 0xa5, sizeof(encoded));
        encoded_bytes = 0;
        CHECK(Worr_NativeCodecEventEncodeV1(
                  &source, TEST_MAX_ENTITIES, encoded, sizeof(encoded),
                  &encoded_bytes) == WORR_NATIVE_CODEC_OK);
        CHECK(encoded_bytes == preflight);
        CHECK(Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info) ==
              WORR_NATIVE_CODEC_OK);
        CHECK(info.record_class == WORR_NATIVE_RECORD_EVENT_V1);
        memset(&decoded, 0xcc, sizeof(decoded));
        CHECK(Worr_NativeCodecEventDecodeV1(
                  encoded, encoded_bytes, TEST_MAX_ENTITIES, &decoded) ==
              WORR_NATIVE_CODEC_OK);
        CHECK(Worr_EventRecordHashV1(
            &source, TEST_MAX_ENTITIES, &source_hash));
        CHECK(Worr_EventRecordHashV1(
            &decoded, TEST_MAX_ENTITIES, &decoded_hash));
        CHECK(source_hash == decoded_hash);
        if (kinds[index] == WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1) {
            CHECK(source.payload_size == 8);
            CHECK(info.range_counts[0] == 4);
        }
        if (kinds[index] == WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1) {
            CHECK(source.payload_size == 72);
            CHECK(info.range_counts[0] == 68);
        }
    }

    {
        worr_local_interaction_authority_receipt_v1 receipt;
        memset(&receipt, 0, sizeof(receipt));
        receipt.struct_size = sizeof(receipt);
        receipt.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
        receipt.command_id.epoch = 44;
        receipt.command_id.sequence = 9;
        receipt.command_hash = UINT64_C(0x0102030405060708);
        receipt.state_hash = UINT64_C(0x1112131415161718);
        receipt.transaction_hash = UINT64_C(0x2122232425262728);
        receipt.action_sequence = 3;
        receipt.state_flags = WORR_LOCAL_INTERACTION_STATE_HOOK_HELD;
        receipt.outcome_flags =
            WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED |
            WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REJECTED;
        CHECK(Worr_LocalInteractionAuthorityReceiptValidateV1(&receipt));

        source = make_event_base(51);
        source.flags = WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
                       WORR_EVENT_FLAG_CRITICAL;
        source.source_ordinal = 0;
        source.source_entity.index = WORR_EVENT_NO_ENTITY;
        source.source_entity.generation = 0;
        source.subject_entity.index = WORR_EVENT_NO_ENTITY;
        source.subject_entity.generation = 0;
        source.event_type = WORR_EVENT_TYPE_AUTHORITY_RECEIPT;
        source.payload_kind =
            WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1;
        source.payload_size = sizeof(receipt);
        memcpy(source.payload, &receipt, sizeof(receipt));
        CHECK(Worr_EventRecordValidateV1(&source, TEST_MAX_ENTITIES));
        CHECK(Worr_NativeCodecEventEncodeV1(
                  &source, TEST_MAX_ENTITIES, encoded, sizeof(encoded),
                  &encoded_bytes) == WORR_NATIVE_CODEC_OK);
        CHECK(Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info) ==
              WORR_NATIVE_CODEC_OK);
        CHECK(info.range_counts[0] == sizeof(receipt));
        CHECK(Worr_NativeCodecEventDecodeV1(
                  encoded, encoded_bytes, TEST_MAX_ENTITIES, &decoded) ==
              WORR_NATIVE_CODEC_OK);
        CHECK(memcmp(decoded.payload, &receipt, sizeof(receipt)) == 0);

        source.source_entity.index = 1;
        CHECK(!Worr_EventRecordValidateV1(&source, TEST_MAX_ENTITIES));
    }

    source = make_event_kind(WORR_EVENT_PAYLOAD_U32X4, 50);
    CHECK(Worr_NativeCodecEventEncodeV1(
              &source, TEST_MAX_ENTITIES, encoded, sizeof(encoded),
              &encoded_bytes) == WORR_NATIVE_CODEC_OK);
    memset(&sentinel, 0x6b, sizeof(sentinel));
    decoded = sentinel;
    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 24, 15);
    store_u32(malformed + 16,
              WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                  WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES + 15u);
    CHECK(Worr_NativeCodecEventDecodeV1(
              malformed, encoded_bytes - 1u, TEST_MAX_ENTITIES,
              &decoded) == WORR_NATIVE_CODEC_MALFORMED);
    CHECK(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
    source.flags &= ~WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
    source.event_id.stream_epoch = 0;
    source.event_id.sequence = 0;
    source.prediction_class = WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE;
    source.prediction_key.command_epoch = 4;
    source.prediction_key.command_sequence = 8;
    source.prediction_key.emitter_ordinal = source.source_ordinal;
    source.prediction_key.lane = WORR_EVENT_PREDICTION_LANE_GAMEPLAY;
    CHECK(Worr_EventRecordValidateV1(&source, TEST_MAX_ENTITIES));
    CHECK(Worr_NativeCodecEventPreflightV1(
              &source, TEST_MAX_ENTITIES, &preflight) ==
          WORR_NATIVE_CODEC_INVALID_RECORD);
    return true;
}

static worr_snapshot_entity_generation_v2 make_generation(uint32_t index,
                                                           uint32_t value)
{
    worr_snapshot_entity_generation_v2 generation;
    memset(&generation, 0, sizeof(generation));
    generation.identity.index = index;
    generation.identity.generation = value;
    generation.provenance_flags = WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    return generation;
}

static worr_snapshot_player_v2 make_player(void)
{
    worr_snapshot_player_v2 player;
    uint32_t index;
    memset(&player, 0, sizeof(player));
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.controlled_entity = make_generation(1, 4);
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    player.movement.movement_type = 0;
    player.movement.origin[0] = 10.0f;
    player.movement.velocity[1] = -2.0f;
    player.movement.movement_flags = 5;
    player.movement.movement_time_ms = 17;
    player.movement.gravity = 800;
    player.movement.view_height = 22;
    player.movement.delta_angles[2] = 45.0f;
    player.view_angles[1] = 90.0f;
    player.view_offset[2] = 22.0f;
    player.gun_index = 7;
    player.gun_frame = 11;
    player.gun_skin = 2;
    player.gun_rate = 10;
    player.rdflags = 3;
    player.team_id = 2;
    player.fov = 100.0f;
    for (index = 0; index < WORR_SNAPSHOT_STATS_CAPACITY; ++index)
        player.stats[index] = (int16_t)index;
    return player;
}

static worr_snapshot_entity_v2 make_entity(void)
{
    worr_snapshot_entity_v2 entity;
    uint32_t index;
    memset(&entity, 0, sizeof(entity));
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.generation = make_generation(1, 4);
    entity.component_mask = WORR_SNAPSHOT_ENTITY_COMPONENTS_V2;
    for (index = 0; index < 3; ++index) {
        entity.origin[index] = (float)index + 0.25f;
        entity.angles[index] = (float)index * 15.0f;
        entity.old_origin[index] = entity.origin[index] - 0.5f;
    }
    entity.model_index[0] = 3;
    entity.model_index[1] = 4;
    entity.frame = 9;
    entity.sound = 12;
    entity.skin = 5;
    entity.solid = 6;
    entity.effects = UINT64_C(0x100000002);
    entity.renderfx = 7;
    entity.alpha = 0.75f;
    entity.scale = 1.25f;
    entity.loop_volume = 0.5f;
    entity.loop_attenuation = 2.0f;
    entity.owner.index = 1;
    entity.owner.generation = 4;
    entity.old_frame = 8;
    entity.instance_bits = 3;
    return entity;
}

static worr_snapshot_entity_v2 make_minimal_entity(void)
{
    worr_snapshot_entity_v2 entity;
    memset(&entity, 0, sizeof(entity));
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.generation = make_generation(2, 5);
    entity.component_mask = WORR_SNAPSHOT_ENTITY_TRANSFORM;
    entity.origin[0] = 4.0f;
    entity.angles[1] = 30.0f;
    entity.owner.index = WORR_EVENT_NO_ENTITY;
    return entity;
}

static worr_snapshot_v2 make_snapshot_metadata(void)
{
    worr_snapshot_v2 snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    snapshot.flags = WORR_SNAPSHOT_FLAG_KEYFRAME |
                     WORR_SNAPSHOT_FLAG_COMPLETE |
                     WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS |
                     WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
    snapshot.snapshot_id.epoch = 3;
    snapshot.snapshot_id.sequence = 1;
    snapshot.server_tick = 100;
    snapshot.server_time_us = UINT64_C(1666700);
    snapshot.controlled_entity = make_generation(1, 4);
    snapshot.discontinuity.flags =
        WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
        WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
    snapshot.discontinuity.reason =
        WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
    return snapshot;
}

typedef struct snapshot_fixture_s {
    worr_snapshot_store_v2 store;
    worr_snapshot_store_slot_v2 slots[1];
    worr_snapshot_entity_v2 entities[2];
    uint8_t area[8];
    worr_snapshot_event_ref_v2 events[2];
} snapshot_fixture;

static bool init_snapshot_fixture(snapshot_fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    return Worr_SnapshotStoreInitV2(
               &fixture->store, fixture->slots, 1, fixture->entities, 2, 2,
               fixture->area, 8, 8, fixture->events, 2, 2,
               TEST_MAX_ENTITIES) == WORR_SNAPSHOT_STORE_OK;
}

static bool build_source_snapshot(snapshot_fixture *fixture,
                                  worr_snapshot_ref_v2 *ref_out)
{
    worr_snapshot_v2 snapshot = make_snapshot_metadata();
    worr_snapshot_player_v2 player = make_player();
    worr_snapshot_entity_v2 entities[2];
    const uint8_t area[2] = {0x5a, 0xc3};
    worr_snapshot_event_ref_v2 event_ref;
    worr_snapshot_store_publish_v2 publication;
    memset(&event_ref, 0, sizeof(event_ref));
    event_ref.struct_size = sizeof(event_ref);
    event_ref.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    event_ref.provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
    event_ref.carrier_ordinal = 0;
    event_ref.semantic_version = WORR_EVENT_MODEL_REVISION;
    event_ref.authority_id.stream_epoch = 7;
    event_ref.authority_id.sequence = 9;
    event_ref.semantic_hash = UINT64_C(0x1122334455667788);
    entities[0] = make_entity();
    entities[1] = make_minimal_entity();
    memset(&publication, 0, sizeof(publication));
    publication.struct_size = sizeof(publication);
    publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
    publication.snapshot = &snapshot;
    publication.player = &player;
    publication.entities = entities;
    publication.area_bytes = area;
    publication.event_refs = &event_ref;
    publication.entity_count = 2;
    publication.area_byte_count = 2;
    publication.event_ref_count = 1;
    return Worr_SnapshotStorePublishV2(
               &fixture->store, &publication, ref_out) ==
           WORR_SNAPSHOT_STORE_OK;
}

static bool build_empty_source_snapshot(snapshot_fixture *fixture,
                                        worr_snapshot_ref_v2 *ref_out)
{
    worr_snapshot_v2 snapshot = make_snapshot_metadata();
    worr_snapshot_player_v2 player = make_player();
    worr_snapshot_store_publish_v2 publication;
    memset(&publication, 0, sizeof(publication));
    publication.struct_size = sizeof(publication);
    publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
    publication.snapshot = &snapshot;
    publication.player = &player;
    return Worr_SnapshotStorePublishV2(
               &fixture->store, &publication, ref_out) ==
           WORR_SNAPSHOT_STORE_OK;
}

static worr_snapshot_projection_view_v2 source_view(
    const snapshot_fixture *fixture,
    worr_snapshot_ref_v2 ref)
{
    worr_snapshot_projection_view_v2 view;
    memset(&view, 0, sizeof(view));
    view.struct_size = sizeof(view);
    view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    view.snapshot = &fixture->slots[ref.slot].snapshot;
    view.player = &fixture->slots[ref.slot].player;
    view.entities = fixture->entities;
    view.area_bytes = fixture->area;
    view.event_refs = fixture->events;
    view.entity_count = view.snapshot->entity_range.count;
    view.area_byte_count = view.snapshot->area_range.count;
    view.event_ref_count = view.snapshot->event_range.count;
    return view;
}

static bool test_snapshot_codec(void)
{
    snapshot_fixture source_fixture;
    snapshot_fixture destination_fixture;
    worr_snapshot_ref_v2 source_ref;
    worr_snapshot_ref_v2 destination_ref;
    worr_snapshot_projection_view_v2 view;
    worr_snapshot_v2 decoded_snapshot;
    worr_snapshot_v2 stored_snapshot;
    worr_snapshot_v2 snapshot_sentinel;
    worr_snapshot_player_v2 decoded_player;
    worr_snapshot_player_v2 player_sentinel;
    worr_snapshot_entity_v2 decoded_entities[2];
    worr_snapshot_entity_v2 entity_sentinel[2];
    uint8_t decoded_area[8];
    uint8_t area_sentinel[8];
    worr_snapshot_event_ref_v2 decoded_events[2];
    worr_snapshot_event_ref_v2 event_sentinel[2];
    worr_snapshot_projection_view_v2 decoded_view;
    worr_snapshot_projection_hashes_v2 decoded_hashes;
    worr_snapshot_projection_hashes_v2 source_hashes;
    worr_snapshot_store_publish_v2 publication;
    worr_snapshot_store_publish_v2 publication_sentinel;
    worr_native_codec_info_v1 info;
    uint8_t encoded[TEST_BUFFER_BYTES];
    uint8_t malformed[TEST_BUFFER_BYTES];
    uint32_t preflight;
    size_t encoded_bytes;

    CHECK(init_snapshot_fixture(&source_fixture));
    CHECK(init_snapshot_fixture(&destination_fixture));
    CHECK(build_source_snapshot(&source_fixture, &source_ref));
    view = source_view(&source_fixture, source_ref);
    CHECK(Worr_NativeCodecSnapshotPreflightV1(
              &view, TEST_MAX_ENTITIES, &preflight) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(preflight == WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                           WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES +
                           WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MAX_BYTES +
                           WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MIN_BYTES + 2u +
                           WORR_NATIVE_CODEC_SNAPSHOT_EVENT_REF_BYTES);
    memset(encoded, 0xa5, sizeof(encoded));
    CHECK(Worr_NativeCodecSnapshotEncodeV1(
              &view, TEST_MAX_ENTITIES, encoded, sizeof(encoded),
              &encoded_bytes) == WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes == preflight);
    CHECK(Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(info.record_class == WORR_NATIVE_RECORD_SNAPSHOT_V1);
    CHECK(info.range_counts[0] == 2);
    CHECK(info.range_counts[1] == 2);
    CHECK(info.range_counts[2] == 1);
    CHECK(Worr_SnapshotProjectionHashesV2(
        &view, TEST_MAX_ENTITIES, &source_hashes));
    memset(&decoded_snapshot, 0xcc, sizeof(decoded_snapshot));
    memset(&decoded_player, 0xcc, sizeof(decoded_player));
    memset(decoded_entities, 0xcc, sizeof(decoded_entities));
    memset(decoded_area, 0xcc, sizeof(decoded_area));
    memset(decoded_events, 0xcc, sizeof(decoded_events));
    memset(&decoded_view, 0xcc, sizeof(decoded_view));
    memset(&decoded_hashes, 0xcc, sizeof(decoded_hashes));
    CHECK(Worr_NativeCodecSnapshotDecodeProjectionV1(
              encoded, encoded_bytes, TEST_MAX_ENTITIES,
              &decoded_snapshot, &decoded_player, decoded_entities, 2,
              decoded_area, 8, decoded_events, 2, &decoded_view,
              &decoded_hashes) == WORR_NATIVE_CODEC_OK);
    CHECK(decoded_view.snapshot == &decoded_snapshot &&
          decoded_view.player == &decoded_player &&
          decoded_view.entities == decoded_entities &&
          decoded_view.area_bytes == decoded_area &&
          decoded_view.event_refs == decoded_events &&
          decoded_view.entity_count == 2 &&
          decoded_view.area_byte_count == 2 &&
          decoded_view.event_ref_count == 1);
    CHECK(memcmp(&decoded_hashes, &source_hashes,
                 sizeof(decoded_hashes)) == 0);
    CHECK(Worr_SnapshotValidateV2(
        &decoded_snapshot, TEST_MAX_ENTITIES));
    memset(&decoded_snapshot, 0xcc, sizeof(decoded_snapshot));
    memset(&decoded_player, 0xcc, sizeof(decoded_player));
    memset(decoded_entities, 0xcc, sizeof(decoded_entities));
    memset(decoded_area, 0xcc, sizeof(decoded_area));
    memset(decoded_events, 0xcc, sizeof(decoded_events));
    memset(&publication, 0xcc, sizeof(publication));
    CHECK(Worr_NativeCodecSnapshotDecodeV1(
              encoded, encoded_bytes, TEST_MAX_ENTITIES,
              &decoded_snapshot, &decoded_player, decoded_entities, 2,
              decoded_area, 8, decoded_events, 2, &publication) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(decoded_snapshot.entity_range.count == 0);
    CHECK(decoded_snapshot.area_range.count == 0);
    CHECK(decoded_snapshot.event_range.count == 0);
    CHECK(decoded_snapshot.snapshot_hash == 0);
    CHECK(publication.snapshot == &decoded_snapshot);
    CHECK(publication.player == &decoded_player);
    CHECK(publication.entities == decoded_entities);
    CHECK(publication.area_bytes == decoded_area);
    CHECK(publication.event_refs == decoded_events);
    CHECK(publication.entity_count == 2);
    CHECK(publication.area_byte_count == 2);
    CHECK(publication.event_ref_count == 1);
    CHECK(Worr_SnapshotStorePublishV2(
              &destination_fixture.store, &publication,
              &destination_ref) == WORR_SNAPSHOT_STORE_OK);
    CHECK(Worr_SnapshotStoreCopySnapshotV2(
              &destination_fixture.store, destination_ref,
              &stored_snapshot) == WORR_SNAPSHOT_STORE_OK);
    CHECK(stored_snapshot.snapshot_hash == view.snapshot->snapshot_hash);
    CHECK(stored_snapshot.player_hash == view.snapshot->player_hash);
    CHECK(stored_snapshot.entity_hash == view.snapshot->entity_hash);
    CHECK(stored_snapshot.area_hash == view.snapshot->area_hash);
    CHECK(stored_snapshot.event_hash == view.snapshot->event_hash);

    memset(&snapshot_sentinel, 0x6d, sizeof(snapshot_sentinel));
    memset(&player_sentinel, 0x4c, sizeof(player_sentinel));
    memset(entity_sentinel, 0x3b, sizeof(entity_sentinel));
    memset(area_sentinel, 0x2a, sizeof(area_sentinel));
    memset(event_sentinel, 0x19, sizeof(event_sentinel));
    memset(&publication_sentinel, 0x7e, sizeof(publication_sentinel));
    decoded_snapshot = snapshot_sentinel;
    decoded_player = player_sentinel;
    memcpy(decoded_entities, entity_sentinel, sizeof(decoded_entities));
    memcpy(decoded_area, area_sentinel, sizeof(decoded_area));
    memcpy(decoded_events, event_sentinel, sizeof(decoded_events));
    publication = publication_sentinel;
    memcpy(malformed, encoded, encoded_bytes);
    malformed[WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
              WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES + 2u] = 1;
    CHECK(Worr_NativeCodecSnapshotDecodeV1(
              malformed, encoded_bytes, TEST_MAX_ENTITIES,
              &decoded_snapshot, &decoded_player, decoded_entities, 2,
              decoded_area, 8, decoded_events, 2, &publication) ==
          WORR_NATIVE_CODEC_MALFORMED);
    CHECK(memcmp(&decoded_snapshot, &snapshot_sentinel,
                 sizeof(decoded_snapshot)) == 0);
    CHECK(memcmp(&publication, &publication_sentinel,
                 sizeof(publication)) == 0);
    CHECK(memcmp(&decoded_player, &player_sentinel,
                 sizeof(decoded_player)) == 0);
    CHECK(memcmp(decoded_entities, entity_sentinel,
                 sizeof(decoded_entities)) == 0);
    CHECK(memcmp(decoded_area, area_sentinel, sizeof(decoded_area)) == 0);
    CHECK(memcmp(decoded_events, event_sentinel,
                 sizeof(decoded_events)) == 0);

    decoded_snapshot = snapshot_sentinel;
    decoded_player = player_sentinel;
    memcpy(decoded_entities, entity_sentinel, sizeof(decoded_entities));
    memcpy(decoded_area, area_sentinel, sizeof(decoded_area));
    memcpy(decoded_events, event_sentinel, sizeof(decoded_events));
    publication = publication_sentinel;
    memcpy(malformed, encoded, encoded_bytes);
    malformed[WORR_NATIVE_CODEC_WIRE_HEADER_BYTES + 126u] ^= 1;
    CHECK(Worr_NativeCodecSnapshotDecodeV1(
              malformed, encoded_bytes, TEST_MAX_ENTITIES,
              &decoded_snapshot, &decoded_player, decoded_entities, 2,
              decoded_area, 8, decoded_events, 2, &publication) ==
          WORR_NATIVE_CODEC_CORRUPT);
    CHECK(memcmp(&decoded_snapshot, &snapshot_sentinel,
                 sizeof(decoded_snapshot)) == 0);
    CHECK(memcmp(&publication, &publication_sentinel,
                 sizeof(publication)) == 0);
    CHECK(memcmp(&decoded_player, &player_sentinel,
                 sizeof(decoded_player)) == 0);
    CHECK(memcmp(decoded_entities, entity_sentinel,
                 sizeof(decoded_entities)) == 0);
    CHECK(memcmp(decoded_area, area_sentinel, sizeof(decoded_area)) == 0);
    CHECK(memcmp(decoded_events, event_sentinel,
                 sizeof(decoded_events)) == 0);

    decoded_snapshot = snapshot_sentinel;
    decoded_player = player_sentinel;
    memcpy(decoded_entities, entity_sentinel, sizeof(decoded_entities));
    memcpy(decoded_area, area_sentinel, sizeof(decoded_area));
    memcpy(decoded_events, event_sentinel, sizeof(decoded_events));
    publication = publication_sentinel;
    CHECK(Worr_NativeCodecSnapshotDecodeV1(
              encoded, encoded_bytes, TEST_MAX_ENTITIES,
              &decoded_snapshot, &decoded_player, decoded_entities, 0,
              decoded_area, 8, decoded_events, 2, &publication) ==
          WORR_NATIVE_CODEC_CAPACITY);
    CHECK(memcmp(&decoded_snapshot, &snapshot_sentinel,
                 sizeof(decoded_snapshot)) == 0);
    CHECK(memcmp(&publication, &publication_sentinel,
                 sizeof(publication)) == 0);
    CHECK(memcmp(&decoded_player, &player_sentinel,
                 sizeof(decoded_player)) == 0);
    CHECK(memcmp(decoded_entities, entity_sentinel,
                 sizeof(decoded_entities)) == 0);
    CHECK(memcmp(decoded_area, area_sentinel, sizeof(decoded_area)) == 0);
    CHECK(memcmp(decoded_events, event_sentinel,
                 sizeof(decoded_events)) == 0);

    memcpy(malformed, encoded, encoded_bytes);
    store_u32(malformed + 24,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES + 1u);
    CHECK(Worr_NativeCodecInspectV1(
              malformed, encoded_bytes, &info) == WORR_NATIVE_CODEC_LIMIT);
    return true;
}

static bool test_empty_snapshot_codec(void)
{
    snapshot_fixture source_fixture;
    snapshot_fixture destination_fixture;
    worr_snapshot_ref_v2 source_ref;
    worr_snapshot_ref_v2 destination_ref;
    worr_snapshot_projection_view_v2 view;
    worr_snapshot_v2 decoded_snapshot;
    worr_snapshot_v2 stored_snapshot;
    worr_snapshot_player_v2 decoded_player;
    worr_snapshot_store_publish_v2 publication;
    uint8_t encoded[1024];
    size_t encoded_bytes;

    CHECK(init_snapshot_fixture(&source_fixture));
    CHECK(init_snapshot_fixture(&destination_fixture));
    CHECK(build_empty_source_snapshot(&source_fixture, &source_ref));
    view = source_view(&source_fixture, source_ref);
    CHECK(Worr_NativeCodecSnapshotEncodeV1(
              &view, TEST_MAX_ENTITIES, encoded, sizeof(encoded),
              &encoded_bytes) == WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes == WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                               WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES);
    CHECK(Worr_NativeCodecSnapshotDecodeV1(
              encoded, encoded_bytes, TEST_MAX_ENTITIES,
              &decoded_snapshot, &decoded_player, NULL, 0, NULL, 0,
              NULL, 0, &publication) == WORR_NATIVE_CODEC_OK);
    CHECK(publication.entities == NULL);
    CHECK(publication.area_bytes == NULL);
    CHECK(publication.event_refs == NULL);
    CHECK(publication.entity_count == 0);
    CHECK(publication.area_byte_count == 0);
    CHECK(publication.event_ref_count == 0);
    CHECK(Worr_SnapshotStorePublishV2(
              &destination_fixture.store, &publication,
              &destination_ref) == WORR_SNAPSHOT_STORE_OK);
    CHECK(Worr_SnapshotStoreCopySnapshotV2(
              &destination_fixture.store, destination_ref,
              &stored_snapshot) == WORR_SNAPSHOT_STORE_OK);
    CHECK(stored_snapshot.snapshot_hash == view.snapshot->snapshot_hash);
    return true;
}

static bool test_maximum_entity_snapshot_codec(void)
{
    static worr_snapshot_store_v2 source_store;
    static worr_snapshot_store_v2 destination_store;
    static worr_snapshot_store_slot_v2 source_slot[1];
    static worr_snapshot_store_slot_v2 destination_slot[1];
    static worr_snapshot_entity_v2 source_entities[
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES];
    static worr_snapshot_entity_v2 decoded_entities[
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES];
    static uint8_t source_area[
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES];
    static uint8_t decoded_area[
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES];
    static worr_snapshot_event_ref_v2 source_events[
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS];
    static worr_snapshot_event_ref_v2 decoded_events[
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS];
    static uint8_t encoded[WORR_NATIVE_CODEC_MAX_ENCODED_BYTES];
    worr_snapshot_v2 source_metadata = make_snapshot_metadata();
    worr_snapshot_v2 decoded_snapshot;
    worr_snapshot_v2 stored_snapshot;
    worr_snapshot_player_v2 source_player = make_player();
    worr_snapshot_player_v2 decoded_player;
    worr_snapshot_store_publish_v2 source_publication;
    worr_snapshot_store_publish_v2 decoded_publication;
    worr_snapshot_projection_view_v2 view;
    worr_snapshot_ref_v2 source_ref;
    worr_snapshot_ref_v2 destination_ref;
    size_t encoded_bytes;
    uint32_t preflight;
    uint32_t index;

    CHECK(Worr_SnapshotStoreInitV2(
              &source_store, source_slot, 1, source_entities,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES,
              source_area, WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES,
              source_events,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS,
              1024) == WORR_SNAPSHOT_STORE_OK);
    CHECK(Worr_SnapshotStoreInitV2(
              &destination_store, destination_slot, 1, decoded_entities,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES,
              decoded_area, WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES,
              decoded_events,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS,
              1024) == WORR_SNAPSHOT_STORE_OK);
    for (index = 0;
         index < WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES;
         ++index) {
        source_entities[index] = make_entity();
        source_entities[index].generation.identity.index = index + 1u;
    }
    for (index = 0;
         index < WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES;
         ++index) {
        source_area[index] = (uint8_t)(index * 37u);
    }
    memset(&source_publication, 0, sizeof(source_publication));
    source_publication.struct_size = sizeof(source_publication);
    source_publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
    source_publication.snapshot = &source_metadata;
    source_publication.player = &source_player;
    source_publication.entities = source_entities;
    source_publication.area_bytes = source_area;
    source_publication.entity_count =
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES;
    source_publication.area_byte_count =
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES;
    CHECK(Worr_SnapshotStorePublishV2(
              &source_store, &source_publication, &source_ref) ==
          WORR_SNAPSHOT_STORE_OK);
    memset(&view, 0, sizeof(view));
    view.struct_size = sizeof(view);
    view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    view.snapshot = &source_slot[source_ref.slot].snapshot;
    view.player = &source_slot[source_ref.slot].player;
    view.entities = source_entities;
    view.area_bytes = source_area;
    view.entity_count = WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES;
    view.area_byte_count = WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES;
    CHECK(Worr_NativeCodecSnapshotEncodeV1(
              &view, 1024, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes ==
          WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
              WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES +
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES *
                  WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MAX_BYTES +
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES);
    CHECK(encoded_bytes < WORR_NATIVE_CODEC_MAX_ENCODED_BYTES);
    CHECK(Worr_NativeCodecSnapshotDecodeV1(
              encoded, encoded_bytes, 1024,
              &decoded_snapshot, &decoded_player, decoded_entities,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES,
              decoded_area, WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES,
              NULL, 0, &decoded_publication) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_SnapshotStorePublishV2(
              &destination_store, &decoded_publication,
              &destination_ref) == WORR_SNAPSHOT_STORE_OK);
    CHECK(Worr_SnapshotStoreCopySnapshotV2(
              &destination_store, destination_ref, &stored_snapshot) ==
          WORR_SNAPSHOT_STORE_OK);
    CHECK(stored_snapshot.snapshot_hash == view.snapshot->snapshot_hash);
    CHECK(stored_snapshot.entity_hash == view.snapshot->entity_hash);
    CHECK(stored_snapshot.area_hash == view.snapshot->area_hash);

    for (index = 0;
         index < WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS;
         ++index) {
        memset(&source_events[index], 0, sizeof(source_events[index]));
        source_events[index].struct_size =
            sizeof(source_events[index]);
        source_events[index].schema_version =
            WORR_SNAPSHOT_ABI_VERSION;
        source_events[index].provenance =
            WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
        source_events[index].carrier_ordinal = index;
        source_events[index].semantic_version =
            WORR_EVENT_MODEL_REVISION;
        source_events[index].authority_id.stream_epoch = 8;
        source_events[index].authority_id.sequence = index + 1u;
        source_events[index].semantic_hash =
            UINT64_C(0x1234567800000000) + index;
    }
    source_publication.event_refs = source_events;
    source_publication.event_ref_count =
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS;
    CHECK(Worr_SnapshotStorePublishV2(
              &source_store, &source_publication, &source_ref) ==
          WORR_SNAPSHOT_STORE_OK);
    /* Rebuild the large projection after the store reuses its single slot. */
    memset(&view, 0, sizeof(view));
    view.struct_size = sizeof(view);
    view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    view.snapshot = &source_slot[source_ref.slot].snapshot;
    view.player = &source_slot[source_ref.slot].player;
    view.entities = source_entities;
    view.area_bytes = source_area;
    view.event_refs = source_events;
    view.entity_count = WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES;
    view.area_byte_count = WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES;
    view.event_ref_count =
        WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS;
    preflight = UINT32_C(0xdeadbeef);
    CHECK(Worr_NativeCodecSnapshotPreflightV1(
              &view, 1024, &preflight) == WORR_NATIVE_CODEC_OK);
    CHECK(preflight ==
          encoded_bytes +
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS *
                  WORR_NATIVE_CODEC_SNAPSHOT_EVENT_REF_BYTES);
    CHECK(preflight > UINT32_C(65536));
    CHECK(preflight <= WORR_NATIVE_CODEC_MAX_ENCODED_BYTES);
    CHECK(Worr_NativeCodecSnapshotEncodeV1(
              &view, 1024, encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes == preflight);
    CHECK(Worr_NativeCodecSnapshotDecodeV1(
              encoded, encoded_bytes, 1024,
              &decoded_snapshot, &decoded_player, decoded_entities,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES,
              decoded_area, WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES,
              decoded_events,
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS,
              &decoded_publication) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(decoded_publication.event_ref_count ==
          WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS);
    CHECK(decoded_events[
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS - 1u]
              .semantic_hash ==
          source_events[
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS - 1u]
              .semantic_hash);
    return true;
}

int main(void)
{
    if (!test_command_codec() || !test_event_codecs() ||
        !test_snapshot_codec() || !test_empty_snapshot_codec() ||
        !test_maximum_entity_snapshot_codec()) {
        return 1;
    }
    puts("native codec tests passed");
    return 0;
}
