/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/event_abi.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define EVENT_KNOWN_FLAGS                                                   \
    ((uint32_t)(WORR_EVENT_FLAG_HAS_AUTHORITY_ID |                          \
                WORR_EVENT_FLAG_CRITICAL | WORR_EVENT_FLAG_REPLAY_SAFE |   \
                WORR_EVENT_FLAG_PRESENT_ONCE |                             \
                WORR_EVENT_FLAG_SNAPSHOT_FENCED))

/* Authority identity and immutable-snapshot lineage are delivery metadata.
 * They remain covered by the full record hash/wire bytes but do not make the
 * same raw legacy action a different semantic event. */
#define EVENT_SEMANTIC_IGNORED_FLAGS                                        \
    ((uint32_t)(WORR_EVENT_FLAG_HAS_AUTHORITY_ID |                          \
                WORR_EVENT_FLAG_SNAPSHOT_FENCED))

#define LEGACY_ENTITY_KNOWN_FLAGS                                           \
    ((uint16_t)(WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION |                \
                WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY))
#define LEGACY_TEMP_KNOWN_FIELDS                                            \
    ((uint16_t)(WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |                    \
                WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2 |                    \
                WORR_EVENT_LEGACY_TEMP_FIELD_OFFSET |                       \
                WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION |                    \
                WORR_EVENT_LEGACY_TEMP_FIELD_COUNT |                        \
                WORR_EVENT_LEGACY_TEMP_FIELD_COLOR |                        \
                WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |                      \
                WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2 |                      \
                WORR_EVENT_LEGACY_TEMP_FIELD_TIME))
#define MUZZLE_KNOWN_FLAGS ((uint32_t)WORR_EVENT_MUZZLE_FLAG_SILENCED)
#define SPATIAL_AUDIO_KNOWN_FLAGS                                           \
    ((uint16_t)(WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL |               \
                WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION |                     \
                WORR_EVENT_SPATIAL_AUDIO_RELIABLE |                         \
                WORR_EVENT_SPATIAL_AUDIO_NO_PHS |                           \
                WORR_EVENT_SPATIAL_AUDIO_LOCAL_ONLY |                       \
                WORR_EVENT_SPATIAL_AUDIO_POSITION_FORCED))
#define KEYED_POI_REQUIRED_RECORD_FLAGS                                     \
    ((uint32_t)(WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE))

static const uint64_t fnv_offset_basis = UINT64_C(14695981039346656037);
static const uint64_t fnv_prime = UINT64_C(1099511628211);

static uint64_t hash_u8(uint64_t hash, uint8_t value)
{
    hash ^= value;
    return hash * fnv_prime;
}

static uint64_t hash_u16(uint64_t hash, uint16_t value)
{
    hash = hash_u8(hash, (uint8_t)(value & 0xffu));
    return hash_u8(hash, (uint8_t)(value >> 8));
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    unsigned int i;
    for (i = 0; i < 4; ++i)
        hash = hash_u8(hash, (uint8_t)(value >> (i * 8)));
    return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int i;
    for (i = 0; i < 8; ++i)
        hash = hash_u8(hash, (uint8_t)(value >> (i * 8)));
    return hash;
}

static uint32_t float_bits(float value)
{
    uint32_t bits;
    if (value == 0.0f)
        return 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t hash_float(uint64_t hash, float value)
{
    return hash_u32(hash, float_bits(value));
}

static uint64_t begin_hash(uint32_t domain)
{
    uint64_t hash = fnv_offset_basis;
    hash = hash_u32(hash, UINT32_C(0x574f5252)); /* WORR */
    hash = hash_u32(hash, domain);
    return hash;
}

static bool bytes_are_zero(const uint8_t *bytes, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

static bool finite_vec3(const float value[3])
{
    return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static bool zero_vec3(const float value[3])
{
    return value[0] == 0.0f && value[1] == 0.0f && value[2] == 0.0f;
}

static bool prediction_key_zero(const worr_event_prediction_key_v1 *key)
{
    return key->command_epoch == 0 && key->command_sequence == 0 &&
           key->emitter_ordinal == 0 && key->lane == 0;
}

static bool prediction_lane_known(uint32_t lane)
{
    return lane == WORR_EVENT_PREDICTION_LANE_GAMEPLAY ||
           lane == WORR_EVENT_PREDICTION_LANE_AUDIO ||
           lane == WORR_EVENT_PREDICTION_LANE_EFFECT;
}

static bool event_type_known(uint16_t type)
{
    return type >= WORR_EVENT_TYPE_DAMAGE &&
           type <= WORR_EVENT_TYPE_AUTHORITY_RECEIPT;
}

static bool delivery_class_known(uint8_t delivery_class)
{
    return delivery_class <= WORR_EVENT_DELIVERY_PERSISTENT_STATE;
}

static bool prediction_class_known(uint8_t prediction_class)
{
    return prediction_class <= WORR_EVENT_PREDICTION_COMMAND_DEFERRED;
}

static uint16_t payload_size_for_kind(uint16_t kind)
{
    switch (kind) {
    case WORR_EVENT_PAYLOAD_NONE:
        return 0;
    case WORR_EVENT_PAYLOAD_VEC3:
        return (uint16_t)sizeof(worr_event_payload_vec3_v1);
    case WORR_EVENT_PAYLOAD_ENTITY_REF:
        return (uint16_t)sizeof(worr_event_payload_entity_ref_v1);
    case WORR_EVENT_PAYLOAD_DAMAGE:
        return (uint16_t)sizeof(worr_event_payload_damage_v1);
    case WORR_EVENT_PAYLOAD_AUDIO:
        return (uint16_t)sizeof(worr_event_payload_audio_v1);
    case WORR_EVENT_PAYLOAD_EFFECT:
        return (uint16_t)sizeof(worr_event_payload_effect_v1);
    case WORR_EVENT_PAYLOAD_U32X4:
        return (uint16_t)sizeof(worr_event_payload_u32x4_v1);
    case WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1:
        return (uint16_t)sizeof(worr_event_payload_legacy_entity_v1);
    case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1:
        return (uint16_t)sizeof(worr_event_payload_legacy_temp_v1);
    case WORR_EVENT_PAYLOAD_MUZZLE_V1:
        return (uint16_t)sizeof(worr_event_payload_muzzle_v1);
    case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1:
        return (uint16_t)sizeof(worr_event_payload_spatial_audio_v1);
    case WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1:
        return (uint16_t)sizeof(worr_local_interaction_authority_receipt_v1);
    case WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1:
        return (uint16_t)sizeof(
            worr_local_action_shadow_authority_receipt_v1);
    case WORR_EVENT_PAYLOAD_KEYED_POI_V1:
        return (uint16_t)sizeof(worr_event_payload_keyed_poi_v1);
    default:
        return UINT16_MAX;
    }
}

static bool payload_matches_event_type(uint16_t event_type,
                                       uint16_t payload_kind)
{
    switch (event_type) {
    case WORR_EVENT_TYPE_DAMAGE:
        return payload_kind == WORR_EVENT_PAYLOAD_DAMAGE;
    case WORR_EVENT_TYPE_WEAPON_FIRE:
        return payload_kind == WORR_EVENT_PAYLOAD_EFFECT ||
               payload_kind == WORR_EVENT_PAYLOAD_MUZZLE_V1;
    case WORR_EVENT_TYPE_VISUAL_EFFECT:
        return payload_kind == WORR_EVENT_PAYLOAD_EFFECT ||
               payload_kind == WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1 ||
               payload_kind == WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1 ||
               payload_kind == WORR_EVENT_PAYLOAD_MUZZLE_V1;
    case WORR_EVENT_TYPE_MOVEMENT_CUE:
        return payload_kind == WORR_EVENT_PAYLOAD_VEC3 ||
               payload_kind == WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1;
    case WORR_EVENT_TYPE_AUDIO_CUE:
        return payload_kind == WORR_EVENT_PAYLOAD_AUDIO ||
               payload_kind == WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1;
    case WORR_EVENT_TYPE_GAMEPLAY_CUE:
        return payload_kind == WORR_EVENT_PAYLOAD_NONE ||
               payload_kind == WORR_EVENT_PAYLOAD_U32X4 ||
               payload_kind == WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1;
    case WORR_EVENT_TYPE_STATE_CHANGE:
        return payload_kind == WORR_EVENT_PAYLOAD_U32X4 ||
               payload_kind == WORR_EVENT_PAYLOAD_ENTITY_REF ||
               payload_kind == WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1 ||
               payload_kind == WORR_EVENT_PAYLOAD_MUZZLE_V1 ||
               payload_kind == WORR_EVENT_PAYLOAD_KEYED_POI_V1;
    case WORR_EVENT_TYPE_LEGACY_BRIDGE:
        return payload_kind == WORR_EVENT_PAYLOAD_U32X4;
    case WORR_EVENT_TYPE_AUTHORITY_RECEIPT:
        return payload_kind ==
                   WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1 ||
               payload_kind ==
                   WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1;
    default:
        return false;
    }
}

bool Worr_EventLegacyTempFieldMaskV1(uint16_t subtype,
                                     int16_t raw_entity1,
                                     uint16_t *field_mask_out)
{
    uint16_t mask;
    if (!field_mask_out)
        return false;

    switch (subtype) {
    case WORR_EVENT_LEGACY_TEMP_GUNSHOT:
    case WORR_EVENT_LEGACY_TEMP_BLOOD:
    case WORR_EVENT_LEGACY_TEMP_BLASTER:
    case WORR_EVENT_LEGACY_TEMP_SHOTGUN:
    case WORR_EVENT_LEGACY_TEMP_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_SCREEN_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_SHIELD_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_BULLET_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_GREENBLOOD:
    case WORR_EVENT_LEGACY_TEMP_BLASTER2:
    case WORR_EVENT_LEGACY_TEMP_MOREBLOOD:
    case WORR_EVENT_LEGACY_TEMP_HEATBEAM_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_HEATBEAM_STEAM:
    case WORR_EVENT_LEGACY_TEMP_ELECTRIC_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_FLECHETTE:
    case WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_FIXED:
    case WORR_EVENT_LEGACY_TEMP_BERSERK_SLAM:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION;
        break;

    case WORR_EVENT_LEGACY_TEMP_SPLASH:
    case WORR_EVENT_LEGACY_TEMP_LASER_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_WELDING_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_TUNNEL_SPARKS:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_COUNT |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION |
               WORR_EVENT_LEGACY_TEMP_FIELD_COLOR;
        break;

    case WORR_EVENT_LEGACY_TEMP_RAILTRAIL:
    case WORR_EVENT_LEGACY_TEMP_BUBBLETRAIL:
    case WORR_EVENT_LEGACY_TEMP_BFG_LASER:
    case WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_BROKEN:
    case WORR_EVENT_LEGACY_TEMP_RAILTRAIL2:
    case WORR_EVENT_LEGACY_TEMP_DEBUGTRAIL:
    case WORR_EVENT_LEGACY_TEMP_BUBBLETRAIL2:
    case WORR_EVENT_LEGACY_TEMP_BFG_ZAP:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2;
        break;

    case WORR_EVENT_LEGACY_TEMP_EXPLOSION1:
    case WORR_EVENT_LEGACY_TEMP_EXPLOSION2:
    case WORR_EVENT_LEGACY_TEMP_ROCKET_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_GRENADE_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_ROCKET_EXPLOSION_WATER:
    case WORR_EVENT_LEGACY_TEMP_GRENADE_EXPLOSION_WATER:
    case WORR_EVENT_LEGACY_TEMP_BFG_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_BFG_BIGEXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_BOSSTPORT:
    case WORR_EVENT_LEGACY_TEMP_PLASMA_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_PLAIN_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_CHAINFIST_SMOKE:
    case WORR_EVENT_LEGACY_TEMP_TRACKER_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_TELEPORT_EFFECT:
    case WORR_EVENT_LEGACY_TEMP_DBALL_GOAL:
    case WORR_EVENT_LEGACY_TEMP_NUKEBLAST:
    case WORR_EVENT_LEGACY_TEMP_WIDOWSPLASH:
    case WORR_EVENT_LEGACY_TEMP_EXPLOSION1_BIG:
    case WORR_EVENT_LEGACY_TEMP_EXPLOSION1_NP:
    case WORR_EVENT_LEGACY_TEMP_EXPLOSION1_NL:
    case WORR_EVENT_LEGACY_TEMP_EXPLOSION2_NL:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1;
        break;

    case WORR_EVENT_LEGACY_TEMP_PARASITE_ATTACK:
    case WORR_EVENT_LEGACY_TEMP_MEDIC_CABLE_ATTACK:
    case WORR_EVENT_LEGACY_TEMP_HEATBEAM:
    case WORR_EVENT_LEGACY_TEMP_MONSTER_HEATBEAM:
    case WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE2:
    case WORR_EVENT_LEGACY_TEMP_LIGHTNING_BEAM:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2;
        break;

    case WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2 |
               WORR_EVENT_LEGACY_TEMP_FIELD_OFFSET;
        break;

    case WORR_EVENT_LEGACY_TEMP_LIGHTNING:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2 |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2;
        break;

    case WORR_EVENT_LEGACY_TEMP_FLASHLIGHT:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1;
        break;

    case WORR_EVENT_LEGACY_TEMP_FORCEWALL:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2 |
               WORR_EVENT_LEGACY_TEMP_FIELD_COLOR;
        break;

    case WORR_EVENT_LEGACY_TEMP_STEAM:
        /* The historical entity1 slot is a steam sustain ID, not an entity
         * reference.  -1 selects the immediate shape; sustained effects use
         * a strictly positive signed-short ID and additionally carry time. */
        if (raw_entity1 < -1 || raw_entity1 == 0)
            return false;
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2 |
               WORR_EVENT_LEGACY_TEMP_FIELD_COUNT |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION |
               WORR_EVENT_LEGACY_TEMP_FIELD_COLOR;
        if (raw_entity1 != -1)
            mask |= WORR_EVENT_LEGACY_TEMP_FIELD_TIME;
        break;

    case WORR_EVENT_LEGACY_TEMP_WIDOWBEAMOUT:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1;
        break;

    case WORR_EVENT_LEGACY_TEMP_POWER_SPLASH:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 |
               WORR_EVENT_LEGACY_TEMP_FIELD_COUNT;
        break;

    case WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT:
        mask = WORR_EVENT_LEGACY_TEMP_FIELD_COUNT;
        break;

    default:
        return false;
    }

    *field_mask_out = mask;
    return true;
}

bool Worr_EventEntityRefValidV1(worr_event_entity_ref_v1 ref,
                                uint32_t max_entities,
                                bool allow_absent)
{
    if (ref.index == WORR_EVENT_NO_ENTITY)
        return allow_absent && ref.generation == 0;
    return ref.generation != 0 && ref.index < max_entities;
}

static bool validate_legacy_entity_payload(
    const worr_event_record_v1 *record,
    const worr_event_payload_legacy_entity_v1 *payload)
{
    uint16_t expected_flags;
    uint16_t expected_type;

    if (payload->reserved0 != 0 ||
        (payload->flags & ~LEGACY_ENTITY_KNOWN_FLAGS) != 0) {
        return false;
    }

    switch (payload->raw_event) {
    case WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN:
        expected_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
        expected_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
        break;
    case WORR_EVENT_LEGACY_ENTITY_FOOTSTEP:
    case WORR_EVENT_LEGACY_ENTITY_FALL_SHORT:
    case WORR_EVENT_LEGACY_ENTITY_FALL_MEDIUM:
    case WORR_EVENT_LEGACY_ENTITY_FALL_FAR:
    case WORR_EVENT_LEGACY_ENTITY_OTHER_FOOTSTEP:
    case WORR_EVENT_LEGACY_ENTITY_LADDER_STEP:
        expected_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
        expected_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
        break;
    case WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT:
        expected_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION |
                         WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY;
        expected_type = WORR_EVENT_TYPE_STATE_CHANGE;
        break;
    case WORR_EVENT_LEGACY_ENTITY_OTHER_TELEPORT:
        expected_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY;
        expected_type = WORR_EVENT_TYPE_STATE_CHANGE;
        break;
    default:
        return false;
    }

    return payload->flags == expected_flags &&
           record->event_type == expected_type;
}

static bool temp_field_vec_valid(uint16_t fields,
                                 uint16_t field,
                                 const float value[3])
{
    return (fields & field) != 0 ? finite_vec3(value) : zero_vec3(value);
}

static bool temp_entity_ref_value_valid(int16_t value,
                                        uint32_t max_entities)
{
    return value >= 0 && (uint32_t)value < max_entities;
}

/* The legacy temp message calls both signed-short integer slots "entity",
 * but their semantic roles are subtype-dependent.  Keep this switch explicit:
 * adding a new integer-bearing subtype without assigning its roles must fail
 * validation instead of accidentally acquiring entity-reference bounds. */
static bool validate_legacy_temp_raw_integer_fields(
    const worr_event_payload_legacy_temp_v1 *payload,
    uint32_t max_entities)
{
    const bool entity1_present =
        (payload->valid_fields & WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1) != 0;
    const bool entity2_present =
        (payload->valid_fields & WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2) != 0;

    if ((!entity1_present && payload->raw_entity1 != 0) ||
        (!entity2_present && payload->raw_entity2 != 0)) {
        return false;
    }

    switch (payload->subtype) {
    case WORR_EVENT_LEGACY_TEMP_PARASITE_ATTACK:
    case WORR_EVENT_LEGACY_TEMP_MEDIC_CABLE_ATTACK:
    case WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE:
    case WORR_EVENT_LEGACY_TEMP_FLASHLIGHT:
    case WORR_EVENT_LEGACY_TEMP_HEATBEAM:
    case WORR_EVENT_LEGACY_TEMP_MONSTER_HEATBEAM:
    case WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE2:
    case WORR_EVENT_LEGACY_TEMP_POWER_SPLASH:
    case WORR_EVENT_LEGACY_TEMP_LIGHTNING_BEAM:
        return entity1_present && !entity2_present &&
               temp_entity_ref_value_valid(payload->raw_entity1,
                                           max_entities);

    case WORR_EVENT_LEGACY_TEMP_LIGHTNING:
        return entity1_present && entity2_present &&
               temp_entity_ref_value_valid(payload->raw_entity1,
                                           max_entities) &&
               temp_entity_ref_value_valid(payload->raw_entity2,
                                           max_entities);

    case WORR_EVENT_LEGACY_TEMP_STEAM:
        /* entity1 is -1 for an immediate effect or a positive sustain ID.
         * entity2 is a signed-short particle magnitude, not an entity. */
        return entity1_present && entity2_present &&
               (payload->raw_entity1 == -1 || payload->raw_entity1 > 0);

    case WORR_EVENT_LEGACY_TEMP_WIDOWBEAMOUT:
        /* The rerelease producers use positive sustain IDs 20001/20002. */
        return entity1_present && !entity2_present &&
               payload->raw_entity1 > 0;

    default:
        return !entity1_present && !entity2_present;
    }
}

static bool validate_legacy_temp_payload(
    const worr_event_record_v1 *record,
    const worr_event_payload_legacy_temp_v1 *payload,
    uint32_t max_entities)
{
    uint16_t expected_fields;
    const bool count_present =
        (payload->valid_fields & WORR_EVENT_LEGACY_TEMP_FIELD_COUNT) != 0;
    const bool color_present =
        (payload->valid_fields & WORR_EVENT_LEGACY_TEMP_FIELD_COLOR) != 0;
    const bool time_present =
        (payload->valid_fields & WORR_EVENT_LEGACY_TEMP_FIELD_TIME) != 0;

    if (payload->reserved0 != 0 ||
        (payload->valid_fields & ~LEGACY_TEMP_KNOWN_FIELDS) != 0 ||
        !Worr_EventLegacyTempFieldMaskV1(payload->subtype,
                                         payload->raw_entity1,
                                         &expected_fields) ||
        payload->valid_fields != expected_fields) {
        return false;
    }

    if (record->event_type !=
        (payload->subtype == WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT
             ? WORR_EVENT_TYPE_GAMEPLAY_CUE
             : WORR_EVENT_TYPE_VISUAL_EFFECT)) {
        return false;
    }

    if (!temp_field_vec_valid(payload->valid_fields,
                              WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1,
                              payload->position1) ||
        !temp_field_vec_valid(payload->valid_fields,
                              WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2,
                              payload->position2) ||
        !temp_field_vec_valid(payload->valid_fields,
                              WORR_EVENT_LEGACY_TEMP_FIELD_OFFSET,
                              payload->offset) ||
        !temp_field_vec_valid(payload->valid_fields,
                              WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION,
                              payload->direction)) {
        return false;
    }

    if (!validate_legacy_temp_raw_integer_fields(payload, max_entities)) {
        return false;
    }

    if (!count_present) {
        if (payload->count_or_amount != 0)
            return false;
    } else if (payload->count_or_amount < 0 ||
               payload->count_or_amount >
                   (payload->subtype == WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT
                        ? INT16_MAX
                        : UINT8_MAX)) {
        return false;
    }

    if ((!color_present && payload->color != 0) ||
        (color_present && (payload->color < 0 || payload->color > UINT8_MAX))) {
        return false;
    }

    if ((!time_present && payload->time_ms != 0) ||
        (time_present && payload->time_ms <= 0)) {
        return false;
    }

    return true;
}

static bool player_muzzle_id_valid(uint16_t flash_id)
{
    return flash_id <= WORR_EVENT_PLAYER_MUZZLE_PHALANX2 ||
           (flash_id >= WORR_EVENT_PLAYER_MUZZLE_ETF_RIFLE &&
            flash_id <= WORR_EVENT_PLAYER_MUZZLE_NUKE8);
}

bool Worr_EventMuzzleCarrierValidV1(uint32_t family,
                                    int32_t entity_index,
                                    uint32_t flash_id,
                                    uint32_t max_entities,
                                    uint32_t monster_flash_exclusive)
{
    if (max_entities <= 1 || entity_index <= 0 ||
        (uint32_t)entity_index >= max_entities || flash_id > UINT16_MAX) {
        return false;
    }
    if (family == WORR_EVENT_MUZZLE_FAMILY_PLAYER)
        return player_muzzle_id_valid((uint16_t)flash_id);
    if (family == WORR_EVENT_MUZZLE_FAMILY_MONSTER) {
        if (monster_flash_exclusive <= WORR_EVENT_MONSTER_MUZZLE_FIRST ||
            monster_flash_exclusive >
                WORR_EVENT_MONSTER_MUZZLE_LAST + 1u) {
            return false;
        }
        return flash_id >= WORR_EVENT_MONSTER_MUZZLE_FIRST &&
               flash_id < monster_flash_exclusive;
    }
    return false;
}

static bool validate_muzzle_payload(
    const worr_event_record_v1 *record,
    const worr_event_payload_muzzle_v1 *payload)
{
    uint16_t expected_type;

    if ((payload->flags & ~MUZZLE_KNOWN_FLAGS) != 0)
        return false;

    if (payload->family == WORR_EVENT_MUZZLE_FAMILY_PLAYER) {
        if (!player_muzzle_id_valid(payload->flash_id))
            return false;
        if (payload->flash_id >= WORR_EVENT_PLAYER_MUZZLE_LOGIN &&
            payload->flash_id <= WORR_EVENT_PLAYER_MUZZLE_RESPAWN) {
            expected_type = WORR_EVENT_TYPE_STATE_CHANGE;
        } else if (payload->flash_id ==
                       WORR_EVENT_PLAYER_MUZZLE_ITEM_RESPAWN ||
                   (payload->flash_id >= WORR_EVENT_PLAYER_MUZZLE_NUKE1 &&
                    payload->flash_id <= WORR_EVENT_PLAYER_MUZZLE_NUKE8)) {
            expected_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
        } else {
            expected_type = WORR_EVENT_TYPE_WEAPON_FIRE;
        }
    } else if (payload->family == WORR_EVENT_MUZZLE_FAMILY_MONSTER) {
        if (payload->flash_id < WORR_EVENT_MONSTER_MUZZLE_FIRST ||
            payload->flash_id > WORR_EVENT_MONSTER_MUZZLE_LAST ||
            payload->flags != 0) {
            return false;
        }
        expected_type = WORR_EVENT_TYPE_WEAPON_FIRE;
    } else {
        return false;
    }

    return record->event_type == expected_type;
}

static bool validate_spatial_audio_payload(
    const worr_event_payload_spatial_audio_v1 *payload,
    uint32_t max_entities)
{
    const bool has_entity =
        (payload->flags & WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL) != 0;
    const bool has_position =
        (payload->flags & WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION) != 0;

    if (payload->asset_id == 0 ||
        (payload->flags & ~SPATIAL_AUDIO_KNOWN_FLAGS) != 0 ||
        ((payload->flags & WORR_EVENT_SPATIAL_AUDIO_POSITION_FORCED) != 0 &&
         !has_position) ||
        !isfinite(payload->volume) || payload->volume < 0.0f ||
        payload->volume > 4.0f || !isfinite(payload->attenuation) ||
        payload->attenuation < 0.0f || payload->attenuation > 4.0f ||
        !isfinite(payload->time_offset) || payload->time_offset < 0.0f ||
        payload->time_offset > 0.255f || !isfinite(payload->pitch) ||
        payload->pitch <= 0.0f ||
        payload->pitch > WORR_EVENT_SPATIAL_AUDIO_MAX_PITCH) {
        return false;
    }

    if (has_entity) {
        if (payload->channel > 7 || payload->raw_entity >= max_entities)
            return false;
    } else if (payload->channel != 0 ||
               payload->raw_entity != WORR_EVENT_NO_ENTITY) {
        return false;
    }

    return has_position ? finite_vec3(payload->origin)
                        : zero_vec3(payload->origin);
}

static bool validate_keyed_poi_payload(
    const worr_event_record_v1 *record,
    const worr_event_payload_keyed_poi_v1 *payload)
{
    const bool removing =
        payload->lifetime_ms == WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS;
    const bool reliable =
        record->delivery_class == WORR_EVENT_DELIVERY_RELIABLE_ORDERED &&
        record->expiry_tick == 0;
    const bool transient =
        record->delivery_class == WORR_EVENT_DELIVERY_TRANSIENT &&
        record->source_tick != UINT32_MAX &&
        record->expiry_tick == record->source_tick + 1u;
    const uint32_t permitted_record_flags =
        KEYED_POI_REQUIRED_RECORD_FLAGS |
        WORR_EVENT_FLAG_HAS_AUTHORITY_ID |
        WORR_EVENT_FLAG_SNAPSHOT_FENCED;

    if (record->event_type != WORR_EVENT_TYPE_STATE_CHANGE ||
        (!reliable && !transient) ||
        record->prediction_class !=
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY ||
        (record->flags & KEYED_POI_REQUIRED_RECORD_FLAGS) !=
            KEYED_POI_REQUIRED_RECORD_FLAGS ||
        (record->flags & ~permitted_record_flags) != 0 || payload->key == 0 ||
        !finite_vec3(payload->position) ||
        (payload->flags & ~WORR_EVENT_KEYED_POI_KNOWN_FLAGS) != 0) {
        return false;
    }

    if (removing) {
        return zero_vec3(payload->position) && payload->image_index == 0 &&
               payload->color_index == 0 && payload->flags == 0;
    }

    return payload->lifetime_ms == 0 ||
           record->source_time_us <=
               UINT64_MAX - (uint64_t)payload->lifetime_ms * UINT64_C(1000);
}

static bool validate_payload(const worr_event_record_v1 *record,
                             uint32_t max_entities)
{
    if (!bytes_are_zero(record->payload + record->payload_size,
                        WORR_EVENT_PAYLOAD_CAPACITY - record->payload_size)) {
        return false;
    }

    switch (record->payload_kind) {
    case WORR_EVENT_PAYLOAD_NONE:
        return true;
    case WORR_EVENT_PAYLOAD_VEC3: {
        worr_event_payload_vec3_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return finite_vec3(payload.value);
    }
    case WORR_EVENT_PAYLOAD_ENTITY_REF: {
        worr_event_payload_entity_ref_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return Worr_EventEntityRefValidV1(payload.entity, max_entities,
                                          false);
    }
    case WORR_EVENT_PAYLOAD_DAMAGE: {
        worr_event_payload_damage_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return isfinite(payload.amount) && payload.amount >= 0.0f &&
               isfinite(payload.impulse) && payload.impulse >= 0.0f &&
               finite_vec3(payload.direction) && finite_vec3(payload.point) &&
               (payload.damage_flags & ~WORR_EVENT_DAMAGE_KNOWN_FLAGS) == 0;
    }
    case WORR_EVENT_PAYLOAD_AUDIO: {
        worr_event_payload_audio_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return payload.asset_id != 0 && isfinite(payload.volume) &&
               payload.volume >= 0.0f && payload.volume <= 4.0f &&
               isfinite(payload.attenuation) && payload.attenuation >= 0.0f &&
               isfinite(payload.pitch) && payload.pitch > 0.0f;
    }
    case WORR_EVENT_PAYLOAD_EFFECT: {
        worr_event_payload_effect_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return payload.effect_id != 0 && finite_vec3(payload.origin) &&
               finite_vec3(payload.direction);
    }
    case WORR_EVENT_PAYLOAD_U32X4:
        return true;
    case WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1: {
        worr_event_payload_legacy_entity_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return validate_legacy_entity_payload(record, &payload);
    }
    case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1: {
        worr_event_payload_legacy_temp_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return validate_legacy_temp_payload(record, &payload, max_entities);
    }
    case WORR_EVENT_PAYLOAD_MUZZLE_V1: {
        worr_event_payload_muzzle_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return validate_muzzle_payload(record, &payload);
    }
    case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1: {
        worr_event_payload_spatial_audio_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return validate_spatial_audio_payload(&payload, max_entities);
    }
    case WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1: {
        worr_local_interaction_authority_receipt_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return Worr_LocalInteractionAuthorityReceiptValidateV1(&payload);
    }
    case WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1: {
        worr_local_action_shadow_authority_receipt_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return Worr_LocalActionShadowAuthorityReceiptValidateV1(&payload);
    }
    case WORR_EVENT_PAYLOAD_KEYED_POI_V1: {
        worr_event_payload_keyed_poi_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return validate_keyed_poi_payload(record, &payload);
    }
    default:
        return false;
    }
}

static bool record_validate(const worr_event_record_v1 *record,
                            uint32_t max_entities,
                            bool allow_authority_candidate)
{
    const bool authoritative =
        record && (record->flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) != 0;
    const bool authority_receipt =
        record && record->event_type == WORR_EVENT_TYPE_AUTHORITY_RECEIPT;
    const bool expiring =
        record && (record->delivery_class == WORR_EVENT_DELIVERY_COSMETIC ||
                   record->delivery_class == WORR_EVENT_DELIVERY_TRANSIENT);
    uint16_t expected_payload_size;

    if (!record || max_entities == 0 ||
        record->struct_size != sizeof(*record) ||
        record->schema_version != WORR_EVENT_ABI_VERSION ||
        record->model_revision != WORR_EVENT_MODEL_REVISION ||
        (record->flags & ~EVENT_KNOWN_FLAGS) != 0 || record->reserved0 != 0 ||
        !event_type_known(record->event_type) ||
        !delivery_class_known(record->delivery_class) ||
        !prediction_class_known(record->prediction_class)) {
        return false;
    }

    if (!Worr_EventEntityRefValidV1(record->source_entity, max_entities,
                                    authority_receipt) ||
        !Worr_EventEntityRefValidV1(record->subject_entity, max_entities,
                                    true)) {
        return false;
    }

    if (authority_receipt) {
        const uint32_t expected_flags =
            WORR_EVENT_FLAG_CRITICAL |
            (authoritative ? WORR_EVENT_FLAG_HAS_AUTHORITY_ID : 0u);
        if (record->flags != expected_flags ||
            record->source_entity.index != WORR_EVENT_NO_ENTITY ||
            record->source_entity.generation != 0 ||
            record->subject_entity.index != WORR_EVENT_NO_ENTITY ||
            record->subject_entity.generation != 0 ||
            record->source_ordinal != 0 ||
            record->delivery_class != WORR_EVENT_DELIVERY_RELIABLE_ORDERED ||
            record->prediction_class !=
                WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY ||
            record->expiry_tick != 0) {
            return false;
        }
    }

    if ((record->flags & WORR_EVENT_FLAG_SNAPSHOT_FENCED) != 0 &&
        (authority_receipt ||
         record->prediction_class !=
             WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY ||
         record->source_tick == UINT32_MAX ||
         (record->flags & (WORR_EVENT_FLAG_REPLAY_SAFE |
                           WORR_EVENT_FLAG_PRESENT_ONCE)) !=
             (WORR_EVENT_FLAG_REPLAY_SAFE |
              WORR_EVENT_FLAG_PRESENT_ONCE))) {
        return false;
    }

    if (authoritative) {
        if (record->event_id.stream_epoch == 0 ||
            record->event_id.sequence == 0)
            return false;
    } else if (record->event_id.stream_epoch != 0 ||
               record->event_id.sequence != 0 ||
               (!allow_authority_candidate &&
                record->prediction_class ==
                    WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY)) {
        return false;
    }

    if (record->prediction_class ==
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY) {
        if (!prediction_key_zero(&record->prediction_key))
            return false;
    } else if (record->prediction_key.command_epoch == 0 ||
               record->prediction_key.command_sequence == 0 ||
               !prediction_lane_known(record->prediction_key.lane) ||
               record->prediction_key.emitter_ordinal !=
                   record->source_ordinal) {
        return false;
    }

    if ((record->flags & WORR_EVENT_FLAG_CRITICAL) != 0 &&
        record->delivery_class < WORR_EVENT_DELIVERY_RELIABLE_ORDERED) {
        return false;
    }

    if (expiring) {
        const uint32_t lifetime = record->expiry_tick - record->source_tick;
        if (lifetime == 0 || lifetime > INT32_MAX)
            return false;
    } else if (record->expiry_tick != 0) {
        return false;
    }

    expected_payload_size = payload_size_for_kind(record->payload_kind);
    if (expected_payload_size == UINT16_MAX ||
        record->payload_size != expected_payload_size ||
        record->payload_size > WORR_EVENT_PAYLOAD_CAPACITY ||
        !payload_matches_event_type(record->event_type,
                                    record->payload_kind)) {
        return false;
    }

    return validate_payload(record, max_entities);
}

bool Worr_EventRecordValidateV1(const worr_event_record_v1 *record,
                                uint32_t max_entities)
{
    return record_validate(record, max_entities, false);
}

bool Worr_EventRecordCandidateValidateV1(
    const worr_event_record_v1 *record,
    uint32_t max_entities)
{
    return record &&
           (record->flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0 &&
           record->event_id.stream_epoch == 0 &&
           record->event_id.sequence == 0 &&
           record_validate(record, max_entities, true);
}

static uint64_t hash_entity_ref(uint64_t hash,
                                worr_event_entity_ref_v1 ref)
{
    hash = hash_u32(hash, ref.index);
    return hash_u32(hash, ref.generation);
}

static uint64_t hash_prediction_key(
    uint64_t hash, const worr_event_prediction_key_v1 *key)
{
    hash = hash_u32(hash, key->command_epoch);
    hash = hash_u32(hash, key->command_sequence);
    hash = hash_u32(hash, key->emitter_ordinal);
    return hash_u32(hash, key->lane);
}

static uint64_t hash_payload(uint64_t hash,
                             const worr_event_record_v1 *record)
{
    switch (record->payload_kind) {
    case WORR_EVENT_PAYLOAD_NONE:
        return hash;
    case WORR_EVENT_PAYLOAD_VEC3: {
        worr_event_payload_vec3_v1 payload;
        unsigned int i;
        memcpy(&payload, record->payload, sizeof(payload));
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.value[i]);
        return hash;
    }
    case WORR_EVENT_PAYLOAD_ENTITY_REF: {
        worr_event_payload_entity_ref_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        return hash_entity_ref(hash, payload.entity);
    }
    case WORR_EVENT_PAYLOAD_DAMAGE: {
        worr_event_payload_damage_v1 payload;
        unsigned int i;
        memcpy(&payload, record->payload, sizeof(payload));
        hash = hash_float(hash, payload.amount);
        hash = hash_float(hash, payload.impulse);
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.direction[i]);
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.point[i]);
        hash = hash_u32(hash, payload.damage_flags);
        return hash_u32(hash, payload.means_of_death);
    }
    case WORR_EVENT_PAYLOAD_AUDIO: {
        worr_event_payload_audio_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        hash = hash_u32(hash, payload.asset_id);
        hash = hash_u32(hash, payload.channel);
        hash = hash_float(hash, payload.volume);
        hash = hash_float(hash, payload.attenuation);
        return hash_float(hash, payload.pitch);
    }
    case WORR_EVENT_PAYLOAD_EFFECT: {
        worr_event_payload_effect_v1 payload;
        unsigned int i;
        memcpy(&payload, record->payload, sizeof(payload));
        hash = hash_u32(hash, payload.effect_id);
        hash = hash_u32(hash, payload.variant);
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.origin[i]);
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.direction[i]);
        return hash;
    }
    case WORR_EVENT_PAYLOAD_U32X4: {
        worr_event_payload_u32x4_v1 payload;
        unsigned int i;
        memcpy(&payload, record->payload, sizeof(payload));
        for (i = 0; i < 4; ++i)
            hash = hash_u32(hash, payload.value[i]);
        return hash;
    }
    case WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1: {
        worr_event_payload_legacy_entity_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        hash = hash_u16(hash, payload.raw_event);
        hash = hash_u16(hash, payload.flags);
        return hash_u32(hash, payload.reserved0);
    }
    case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1: {
        worr_event_payload_legacy_temp_v1 payload;
        unsigned int i;
        memcpy(&payload, record->payload, sizeof(payload));
        hash = hash_u16(hash, payload.subtype);
        hash = hash_u16(hash, payload.valid_fields);
        hash = hash_u16(hash, (uint16_t)payload.raw_entity1);
        hash = hash_u16(hash, (uint16_t)payload.raw_entity2);
        hash = hash_u32(hash, (uint32_t)payload.time_ms);
        hash = hash_u32(hash, (uint32_t)payload.count_or_amount);
        hash = hash_u32(hash, (uint32_t)payload.color);
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.position1[i]);
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.position2[i]);
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.offset[i]);
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.direction[i]);
        return hash_u32(hash, payload.reserved0);
    }
    case WORR_EVENT_PAYLOAD_MUZZLE_V1: {
        worr_event_payload_muzzle_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        hash = hash_u16(hash, payload.family);
        hash = hash_u16(hash, payload.flash_id);
        return hash_u32(hash, payload.flags);
    }
    case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1: {
        worr_event_payload_spatial_audio_v1 payload;
        unsigned int i;
        memcpy(&payload, record->payload, sizeof(payload));
        hash = hash_u32(hash, payload.asset_id);
        hash = hash_u16(hash, payload.channel);
        hash = hash_u16(hash, payload.flags);
        hash = hash_u32(hash, payload.raw_entity);
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.origin[i]);
        hash = hash_float(hash, payload.volume);
        hash = hash_float(hash, payload.attenuation);
        hash = hash_float(hash, payload.time_offset);
        return hash_float(hash, payload.pitch);
    }
    case WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1: {
        worr_local_interaction_authority_receipt_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        hash = hash_u32(hash, payload.struct_size);
        hash = hash_u32(hash, payload.schema_version);
        hash = hash_u32(hash, payload.command_id.epoch);
        hash = hash_u32(hash, payload.command_id.sequence);
        hash = hash_u64(hash, payload.command_hash);
        hash = hash_u64(hash, payload.state_hash);
        hash = hash_u64(hash, payload.transaction_hash);
        hash = hash_u32(hash, payload.action_sequence);
        hash = hash_u32(hash, payload.state_flags);
        hash = hash_u32(hash, payload.outcome_flags);
        return hash_u32(hash, payload.reserved0);
    }
    case WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1: {
        worr_local_action_shadow_authority_receipt_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        hash = hash_u32(hash, payload.struct_size);
        hash = hash_u32(hash, payload.schema_version);
        hash = hash_u32(hash, payload.model_revision);
        hash = hash_u32(hash, payload.reserved0);
        hash = hash_u32(hash, payload.command_id.epoch);
        hash = hash_u32(hash, payload.command_id.sequence);
        hash = hash_u32(hash, payload.catalog_id);
        hash = hash_u32(hash, payload.flags);
        hash = hash_u32(hash, payload.v2_blockers);
        hash = hash_u32(hash, payload.reserved1);
        hash = hash_u64(hash, payload.command_hash);
        hash = hash_u64(hash, payload.descriptor_hash);
        return hash_u64(hash, payload.record_hash);
    }
    case WORR_EVENT_PAYLOAD_KEYED_POI_V1: {
        worr_event_payload_keyed_poi_v1 payload;
        unsigned int i;
        memcpy(&payload, record->payload, sizeof(payload));
        hash = hash_u16(hash, payload.key);
        hash = hash_u16(hash, payload.lifetime_ms);
        for (i = 0; i < 3; ++i)
            hash = hash_float(hash, payload.position[i]);
        hash = hash_u16(hash, payload.image_index);
        hash = hash_u8(hash, payload.color_index);
        return hash_u8(hash, payload.flags);
    }
    default:
        return 0;
    }
}

static uint64_t hash_record_fields(const worr_event_record_v1 *record,
                                   uint32_t domain,
                                   bool semantic)
{
    uint64_t hash = begin_hash(domain);
    hash = hash_u32(hash, record->struct_size);
    hash = hash_u32(hash, record->schema_version);
    hash = hash_u32(hash, record->model_revision);
    hash = hash_u32(hash,
                    semantic
                        ? record->flags &
                              ~EVENT_SEMANTIC_IGNORED_FLAGS
                        : record->flags);
    if (!semantic) {
        hash = hash_u32(hash, record->event_id.stream_epoch);
        hash = hash_u32(hash, record->event_id.sequence);
    }
    hash = hash_u32(hash, record->source_tick);
    hash = hash_u32(hash, record->source_ordinal);
    hash = hash_u64(hash, record->source_time_us);
    hash = hash_entity_ref(hash, record->source_entity);
    hash = hash_entity_ref(hash, record->subject_entity);
    hash = hash_u16(hash, record->event_type);
    hash = hash_u8(hash, record->delivery_class);
    hash = hash_u8(hash, record->prediction_class);
    hash = hash_prediction_key(hash, &record->prediction_key);
    hash = hash_u32(hash, record->expiry_tick);
    hash = hash_u16(hash, record->payload_kind);
    hash = hash_u16(hash, record->payload_size);
    return hash_payload(hash, record);
}

bool Worr_EventRecordHashV1(const worr_event_record_v1 *record,
                            uint32_t max_entities,
                            uint64_t *hash_out)
{
    if (!hash_out || !record_validate(record, max_entities, false))
        return false;

    *hash_out = hash_record_fields(record, UINT32_C(0x45565431), false);
    return true;
}

bool Worr_EventRecordSemanticHashV1(const worr_event_record_v1 *record,
                                    uint32_t max_entities,
                                    uint64_t *hash_out)
{
    if (!hash_out || !record_validate(record, max_entities, true))
        return false;

    *hash_out = hash_record_fields(record, UINT32_C(0x45534d31), true);
    return true;
}

static bool float_semantically_equal(float left, float right)
{
    return float_bits(left) == float_bits(right);
}

static bool vec3_semantically_equal(const float left[3],
                                    const float right[3])
{
    unsigned int i;
    for (i = 0; i < 3; ++i) {
        if (!float_semantically_equal(left[i], right[i]))
            return false;
    }
    return true;
}

static bool payload_semantically_equal(const worr_event_record_v1 *left,
                                       const worr_event_record_v1 *right)
{
    switch (left->payload_kind) {
    case WORR_EVENT_PAYLOAD_NONE:
        return true;
    case WORR_EVENT_PAYLOAD_VEC3: {
        worr_event_payload_vec3_v1 a;
        worr_event_payload_vec3_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return vec3_semantically_equal(a.value, b.value);
    }
    case WORR_EVENT_PAYLOAD_ENTITY_REF: {
        worr_event_payload_entity_ref_v1 a;
        worr_event_payload_entity_ref_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return a.entity.index == b.entity.index &&
               a.entity.generation == b.entity.generation;
    }
    case WORR_EVENT_PAYLOAD_DAMAGE: {
        worr_event_payload_damage_v1 a;
        worr_event_payload_damage_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return float_semantically_equal(a.amount, b.amount) &&
               float_semantically_equal(a.impulse, b.impulse) &&
               vec3_semantically_equal(a.direction, b.direction) &&
               vec3_semantically_equal(a.point, b.point) &&
               a.damage_flags == b.damage_flags &&
               a.means_of_death == b.means_of_death;
    }
    case WORR_EVENT_PAYLOAD_AUDIO: {
        worr_event_payload_audio_v1 a;
        worr_event_payload_audio_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return a.asset_id == b.asset_id && a.channel == b.channel &&
               float_semantically_equal(a.volume, b.volume) &&
               float_semantically_equal(a.attenuation, b.attenuation) &&
               float_semantically_equal(a.pitch, b.pitch);
    }
    case WORR_EVENT_PAYLOAD_EFFECT: {
        worr_event_payload_effect_v1 a;
        worr_event_payload_effect_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return a.effect_id == b.effect_id && a.variant == b.variant &&
               vec3_semantically_equal(a.origin, b.origin) &&
               vec3_semantically_equal(a.direction, b.direction);
    }
    case WORR_EVENT_PAYLOAD_U32X4: {
        worr_event_payload_u32x4_v1 a;
        worr_event_payload_u32x4_v1 b;
        unsigned int i;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        for (i = 0; i < 4; ++i) {
            if (a.value[i] != b.value[i])
                return false;
        }
        return true;
    }
    case WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1: {
        worr_event_payload_legacy_entity_v1 a;
        worr_event_payload_legacy_entity_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return a.raw_event == b.raw_event && a.flags == b.flags;
    }
    case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1: {
        worr_event_payload_legacy_temp_v1 a;
        worr_event_payload_legacy_temp_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return a.subtype == b.subtype &&
               a.valid_fields == b.valid_fields &&
               a.raw_entity1 == b.raw_entity1 &&
               a.raw_entity2 == b.raw_entity2 &&
               a.time_ms == b.time_ms &&
               a.count_or_amount == b.count_or_amount &&
               a.color == b.color &&
               vec3_semantically_equal(a.position1, b.position1) &&
               vec3_semantically_equal(a.position2, b.position2) &&
               vec3_semantically_equal(a.offset, b.offset) &&
               vec3_semantically_equal(a.direction, b.direction);
    }
    case WORR_EVENT_PAYLOAD_MUZZLE_V1: {
        worr_event_payload_muzzle_v1 a;
        worr_event_payload_muzzle_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return a.family == b.family && a.flash_id == b.flash_id &&
               a.flags == b.flags;
    }
    case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1: {
        worr_event_payload_spatial_audio_v1 a;
        worr_event_payload_spatial_audio_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return a.asset_id == b.asset_id && a.channel == b.channel &&
               a.flags == b.flags && a.raw_entity == b.raw_entity &&
               vec3_semantically_equal(a.origin, b.origin) &&
               float_semantically_equal(a.volume, b.volume) &&
               float_semantically_equal(a.attenuation, b.attenuation) &&
               float_semantically_equal(a.time_offset, b.time_offset) &&
               float_semantically_equal(a.pitch, b.pitch);
    }
    case WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1: {
        worr_local_interaction_authority_receipt_v1 a;
        worr_local_interaction_authority_receipt_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return a.struct_size == b.struct_size &&
               a.schema_version == b.schema_version &&
               a.command_id.epoch == b.command_id.epoch &&
               a.command_id.sequence == b.command_id.sequence &&
               a.command_hash == b.command_hash &&
               a.state_hash == b.state_hash &&
               a.transaction_hash == b.transaction_hash &&
               a.action_sequence == b.action_sequence &&
               a.state_flags == b.state_flags &&
               a.outcome_flags == b.outcome_flags &&
               a.reserved0 == b.reserved0;
    }
    case WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1: {
        worr_local_action_shadow_authority_receipt_v1 a;
        worr_local_action_shadow_authority_receipt_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return memcmp(&a, &b, sizeof(a)) == 0;
    }
    case WORR_EVENT_PAYLOAD_KEYED_POI_V1: {
        worr_event_payload_keyed_poi_v1 a;
        worr_event_payload_keyed_poi_v1 b;
        memcpy(&a, left->payload, sizeof(a));
        memcpy(&b, right->payload, sizeof(b));
        return a.key == b.key && a.lifetime_ms == b.lifetime_ms &&
               vec3_semantically_equal(a.position, b.position) &&
               a.image_index == b.image_index &&
               a.color_index == b.color_index && a.flags == b.flags;
    }
    default:
        return false;
    }
}

bool Worr_EventRecordSemanticallyEqualV1(
    const worr_event_record_v1 *left,
    const worr_event_record_v1 *right,
    uint32_t max_entities)
{
    if (!record_validate(left, max_entities, true) ||
        !record_validate(right, max_entities, true)) {
        return false;
    }

    return left->struct_size == right->struct_size &&
           left->schema_version == right->schema_version &&
           left->model_revision == right->model_revision &&
           (left->flags & ~EVENT_SEMANTIC_IGNORED_FLAGS) ==
               (right->flags & ~EVENT_SEMANTIC_IGNORED_FLAGS) &&
           left->source_tick == right->source_tick &&
           left->source_ordinal == right->source_ordinal &&
           left->source_time_us == right->source_time_us &&
           left->source_entity.index == right->source_entity.index &&
           left->source_entity.generation ==
               right->source_entity.generation &&
           left->subject_entity.index == right->subject_entity.index &&
           left->subject_entity.generation ==
               right->subject_entity.generation &&
           left->event_type == right->event_type &&
           left->delivery_class == right->delivery_class &&
           left->prediction_class == right->prediction_class &&
           left->prediction_key.command_epoch ==
               right->prediction_key.command_epoch &&
           left->prediction_key.command_sequence ==
               right->prediction_key.command_sequence &&
           left->prediction_key.emitter_ordinal ==
               right->prediction_key.emitter_ordinal &&
           left->prediction_key.lane == right->prediction_key.lane &&
           left->expiry_tick == right->expiry_tick &&
           left->payload_kind == right->payload_kind &&
           left->payload_size == right->payload_size &&
           left->reserved0 == right->reserved0 &&
           payload_semantically_equal(left, right);
}

uint64_t Worr_EventPredictionKeyHashV1(
    const worr_event_prediction_key_v1 *key)
{
    if (!key)
        return 0;
    return hash_prediction_key(begin_hash(UINT32_C(0x50524b31)), key);
}

bool Worr_EventIdNextV1(worr_event_id_v1 current,
                        worr_event_id_v1 *next)
{
    if (!next)
        return false;
    if (current.stream_epoch == 0) {
        if (current.sequence != 0)
            return false;
        next->stream_epoch = 1;
        next->sequence = 1;
        return true;
    }
    if (current.sequence == 0) {
        next->stream_epoch = current.stream_epoch;
        next->sequence = 1;
        return true;
    }
    if (current.sequence != UINT32_MAX) {
        next->stream_epoch = current.stream_epoch;
        next->sequence = current.sequence + 1;
        return true;
    }
    if (current.stream_epoch == UINT32_MAX)
        return false;
    next->stream_epoch = current.stream_epoch + 1;
    next->sequence = 1;
    return true;
}

static bool receipt_valid(const worr_event_receipt_ack_v1 *receipt)
{
    uint64_t valid_mask;
    uint64_t remaining;
    if (!receipt || receipt->struct_size != sizeof(*receipt) ||
        receipt->schema_version != WORR_EVENT_ABI_VERSION ||
        receipt->stream_epoch == 0) {
        return false;
    }
    if (receipt->highest_contiguous == UINT32_MAX)
        return receipt->selective_mask == 0;
    remaining = (uint64_t)UINT32_MAX - receipt->highest_contiguous;
    if (remaining >= WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY)
        return true;
    valid_mask = remaining == WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY
                     ? UINT64_MAX
                     : ((UINT64_C(1) << remaining) - 1u);
    return (receipt->selective_mask & ~valid_mask) == 0;
}

bool Worr_EventReceiptInitV1(worr_event_receipt_ack_v1 *receipt,
                             uint32_t stream_epoch)
{
    if (!receipt || stream_epoch == 0)
        return false;
    memset(receipt, 0, sizeof(*receipt));
    receipt->struct_size = sizeof(*receipt);
    receipt->schema_version = WORR_EVENT_ABI_VERSION;
    receipt->stream_epoch = stream_epoch;
    return true;
}

bool Worr_EventReceiptAdvanceEpochV1(worr_event_receipt_ack_v1 *receipt,
                                     uint32_t stream_epoch)
{
    if (!receipt_valid(receipt) || stream_epoch <= receipt->stream_epoch)
        return false;
    return Worr_EventReceiptInitV1(receipt, stream_epoch);
}

bool Worr_EventReceiptContainsV1(const worr_event_receipt_ack_v1 *receipt,
                                 worr_event_id_v1 event_id)
{
    uint64_t delta;
    if (!receipt_valid(receipt) || event_id.sequence == 0 ||
        event_id.stream_epoch != receipt->stream_epoch) {
        return false;
    }
    if (event_id.sequence <= receipt->highest_contiguous)
        return true;
    if (receipt->highest_contiguous == UINT32_MAX)
        return true;
    delta = (uint64_t)event_id.sequence -
            ((uint64_t)receipt->highest_contiguous + 1u);
    return delta < WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY &&
           (receipt->selective_mask & (UINT64_C(1) << delta)) != 0;
}

worr_event_receipt_result_v1 Worr_EventReceiptMarkV1(
    worr_event_receipt_ack_v1 *receipt, worr_event_id_v1 event_id)
{
    uint64_t delta;
    if (!receipt_valid(receipt) || event_id.sequence == 0)
        return WORR_EVENT_RECEIPT_INVALID;
    if (event_id.stream_epoch != receipt->stream_epoch)
        return WORR_EVENT_RECEIPT_WRONG_EPOCH;
    if (Worr_EventReceiptContainsV1(receipt, event_id))
        return WORR_EVENT_RECEIPT_DUPLICATE;
    if (receipt->highest_contiguous == UINT32_MAX)
        return WORR_EVENT_RECEIPT_DUPLICATE;

    delta = (uint64_t)event_id.sequence -
            ((uint64_t)receipt->highest_contiguous + 1u);
    if (delta >= WORR_EVENT_RECEIPT_SELECTIVE_CAPACITY)
        return WORR_EVENT_RECEIPT_OUTSIDE_WINDOW;

    receipt->selective_mask |= UINT64_C(1) << delta;
    while ((receipt->selective_mask & UINT64_C(1)) != 0) {
        ++receipt->highest_contiguous;
        receipt->selective_mask >>= 1;
        if (receipt->highest_contiguous == UINT32_MAX)
            break;
    }
    return WORR_EVENT_RECEIPT_ACCEPTED;
}
