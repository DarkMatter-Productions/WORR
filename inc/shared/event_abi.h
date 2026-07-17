/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <float.h>

#include "shared/local_interaction_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_EVENT_ABI_VERSION 1u
#define WORR_EVENT_MODEL_REVISION 1u
#define WORR_EVENT_PAYLOAD_CAPACITY 80u
#define WORR_EVENT_NO_ENTITY UINT32_MAX
#define WORR_EVENT_SPATIAL_AUDIO_MAX_PITCH 4.0f

/* Epoch and sequence zero are reserved.  Streams start at { 1, 1 } and
 * advance the epoch instead of allowing a sequence to wrap inside an epoch. */
typedef struct worr_event_id_v1_s {
    uint32_t stream_epoch;
    uint32_t sequence;
} worr_event_id_v1;

/* An entity slot is never an identity by itself.  Every live reference has a
 * non-zero generation; the only absent reference is { NO_ENTITY, 0 }. */
typedef struct worr_event_entity_ref_v1_s {
    uint32_t index;
    uint32_t generation;
} worr_event_entity_ref_v1;

/* Deterministic command provenance used for prediction correlation.  This is
 * deliberately not an authoritative event ID. */
typedef struct worr_event_prediction_key_v1_s {
    uint32_t command_epoch;
    uint32_t command_sequence;
    uint32_t emitter_ordinal;
    uint32_t lane;
} worr_event_prediction_key_v1;

typedef enum worr_event_prediction_lane_v1_e {
    WORR_EVENT_PREDICTION_LANE_GAMEPLAY = 1,
    WORR_EVENT_PREDICTION_LANE_AUDIO = 2,
    WORR_EVENT_PREDICTION_LANE_EFFECT = 3,
} worr_event_prediction_lane_v1;

/* Values are stable schema IDs and must never be renumbered. */
typedef enum worr_event_type_v1_e {
    WORR_EVENT_TYPE_DAMAGE = 1,
    WORR_EVENT_TYPE_WEAPON_FIRE = 2,
    WORR_EVENT_TYPE_MOVEMENT_CUE = 3,
    WORR_EVENT_TYPE_AUDIO_CUE = 4,
    WORR_EVENT_TYPE_VISUAL_EFFECT = 5,
    WORR_EVENT_TYPE_GAMEPLAY_CUE = 6,
    WORR_EVENT_TYPE_STATE_CHANGE = 7,
    WORR_EVENT_TYPE_LEGACY_BRIDGE = 8,
    /* Private per-peer authority control record.  It is deliberately not a
     * world/presentation event and uses absent entity references. */
    WORR_EVENT_TYPE_AUTHORITY_RECEIPT = 9,
} worr_event_type_v1;

typedef enum worr_event_delivery_class_v1_e {
    WORR_EVENT_DELIVERY_COSMETIC = 0,
    WORR_EVENT_DELIVERY_TRANSIENT = 1,
    WORR_EVENT_DELIVERY_RELIABLE_ORDERED = 2,
    WORR_EVENT_DELIVERY_PERSISTENT_STATE = 3,
} worr_event_delivery_class_v1;

typedef enum worr_event_prediction_class_v1_e {
    WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY = 0,
    WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE = 1,
    WORR_EVENT_PREDICTION_COMMAND_DEFERRED = 2,
} worr_event_prediction_class_v1;

enum {
    WORR_EVENT_FLAG_HAS_AUTHORITY_ID = 1u << 0,
    WORR_EVENT_FLAG_CRITICAL = 1u << 1,
    WORR_EVENT_FLAG_REPLAY_SAFE = 1u << 2,
    WORR_EVENT_FLAG_PRESENT_ONCE = 1u << 3,
};

typedef enum worr_event_payload_kind_v1_e {
    WORR_EVENT_PAYLOAD_NONE = 0,
    WORR_EVENT_PAYLOAD_VEC3 = 1,
    WORR_EVENT_PAYLOAD_ENTITY_REF = 2,
    WORR_EVENT_PAYLOAD_DAMAGE = 3,
    WORR_EVENT_PAYLOAD_AUDIO = 4,
    WORR_EVENT_PAYLOAD_EFFECT = 5,
    WORR_EVENT_PAYLOAD_U32X4 = 6,
    WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1 = 7,
    WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1 = 8,
    WORR_EVENT_PAYLOAD_MUZZLE_V1 = 9,
    WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1 = 10,
    WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1 = 11,
} worr_event_payload_kind_v1;

/* Stable legacy entity-event wire values. */
typedef enum worr_event_legacy_entity_id_v1_e {
    WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN = 1,
    WORR_EVENT_LEGACY_ENTITY_FOOTSTEP = 2,
    WORR_EVENT_LEGACY_ENTITY_FALL_SHORT = 3,
    WORR_EVENT_LEGACY_ENTITY_FALL_MEDIUM = 4,
    WORR_EVENT_LEGACY_ENTITY_FALL_FAR = 5,
    WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT = 6,
    WORR_EVENT_LEGACY_ENTITY_OTHER_TELEPORT = 7,
    WORR_EVENT_LEGACY_ENTITY_OTHER_FOOTSTEP = 8,
    WORR_EVENT_LEGACY_ENTITY_LADDER_STEP = 9,
} worr_event_legacy_entity_id_v1;

enum {
    WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION = 1u << 0,
    WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY = 1u << 1,
};

/* Stable temporary-entity wire values.  Names deliberately disambiguate the
 * historical blue-hyperblaster collision and reserve the unsupported flame
 * value instead of inheriting differing C and C++ symbol names. */
typedef enum worr_event_legacy_temp_id_v1_e {
    WORR_EVENT_LEGACY_TEMP_GUNSHOT = 0,
    WORR_EVENT_LEGACY_TEMP_BLOOD = 1,
    WORR_EVENT_LEGACY_TEMP_BLASTER = 2,
    WORR_EVENT_LEGACY_TEMP_RAILTRAIL = 3,
    WORR_EVENT_LEGACY_TEMP_SHOTGUN = 4,
    WORR_EVENT_LEGACY_TEMP_EXPLOSION1 = 5,
    WORR_EVENT_LEGACY_TEMP_EXPLOSION2 = 6,
    WORR_EVENT_LEGACY_TEMP_ROCKET_EXPLOSION = 7,
    WORR_EVENT_LEGACY_TEMP_GRENADE_EXPLOSION = 8,
    WORR_EVENT_LEGACY_TEMP_SPARKS = 9,
    WORR_EVENT_LEGACY_TEMP_SPLASH = 10,
    WORR_EVENT_LEGACY_TEMP_BUBBLETRAIL = 11,
    WORR_EVENT_LEGACY_TEMP_SCREEN_SPARKS = 12,
    WORR_EVENT_LEGACY_TEMP_SHIELD_SPARKS = 13,
    WORR_EVENT_LEGACY_TEMP_BULLET_SPARKS = 14,
    WORR_EVENT_LEGACY_TEMP_LASER_SPARKS = 15,
    WORR_EVENT_LEGACY_TEMP_PARASITE_ATTACK = 16,
    WORR_EVENT_LEGACY_TEMP_ROCKET_EXPLOSION_WATER = 17,
    WORR_EVENT_LEGACY_TEMP_GRENADE_EXPLOSION_WATER = 18,
    WORR_EVENT_LEGACY_TEMP_MEDIC_CABLE_ATTACK = 19,
    WORR_EVENT_LEGACY_TEMP_BFG_EXPLOSION = 20,
    WORR_EVENT_LEGACY_TEMP_BFG_BIGEXPLOSION = 21,
    WORR_EVENT_LEGACY_TEMP_BOSSTPORT = 22,
    WORR_EVENT_LEGACY_TEMP_BFG_LASER = 23,
    WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE = 24,
    WORR_EVENT_LEGACY_TEMP_WELDING_SPARKS = 25,
    WORR_EVENT_LEGACY_TEMP_GREENBLOOD = 26,
    WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_BROKEN = 27,
    WORR_EVENT_LEGACY_TEMP_PLASMA_EXPLOSION = 28,
    WORR_EVENT_LEGACY_TEMP_TUNNEL_SPARKS = 29,
    WORR_EVENT_LEGACY_TEMP_BLASTER2 = 30,
    WORR_EVENT_LEGACY_TEMP_RAILTRAIL2 = 31,
    WORR_EVENT_LEGACY_TEMP_FLAME_RESERVED = 32,
    WORR_EVENT_LEGACY_TEMP_LIGHTNING = 33,
    WORR_EVENT_LEGACY_TEMP_DEBUGTRAIL = 34,
    WORR_EVENT_LEGACY_TEMP_PLAIN_EXPLOSION = 35,
    WORR_EVENT_LEGACY_TEMP_FLASHLIGHT = 36,
    WORR_EVENT_LEGACY_TEMP_FORCEWALL = 37,
    WORR_EVENT_LEGACY_TEMP_HEATBEAM = 38,
    WORR_EVENT_LEGACY_TEMP_MONSTER_HEATBEAM = 39,
    WORR_EVENT_LEGACY_TEMP_STEAM = 40,
    WORR_EVENT_LEGACY_TEMP_BUBBLETRAIL2 = 41,
    WORR_EVENT_LEGACY_TEMP_MOREBLOOD = 42,
    WORR_EVENT_LEGACY_TEMP_HEATBEAM_SPARKS = 43,
    WORR_EVENT_LEGACY_TEMP_HEATBEAM_STEAM = 44,
    WORR_EVENT_LEGACY_TEMP_CHAINFIST_SMOKE = 45,
    WORR_EVENT_LEGACY_TEMP_ELECTRIC_SPARKS = 46,
    WORR_EVENT_LEGACY_TEMP_TRACKER_EXPLOSION = 47,
    WORR_EVENT_LEGACY_TEMP_TELEPORT_EFFECT = 48,
    WORR_EVENT_LEGACY_TEMP_DBALL_GOAL = 49,
    WORR_EVENT_LEGACY_TEMP_WIDOWBEAMOUT = 50,
    WORR_EVENT_LEGACY_TEMP_NUKEBLAST = 51,
    WORR_EVENT_LEGACY_TEMP_WIDOWSPLASH = 52,
    WORR_EVENT_LEGACY_TEMP_EXPLOSION1_BIG = 53,
    WORR_EVENT_LEGACY_TEMP_EXPLOSION1_NP = 54,
    WORR_EVENT_LEGACY_TEMP_FLECHETTE = 55,
    WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_FIXED = 56,
    WORR_EVENT_LEGACY_TEMP_BFG_ZAP = 57,
    WORR_EVENT_LEGACY_TEMP_BERSERK_SLAM = 58,
    WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE2 = 59,
    WORR_EVENT_LEGACY_TEMP_POWER_SPLASH = 60,
    WORR_EVENT_LEGACY_TEMP_LIGHTNING_BEAM = 61,
    WORR_EVENT_LEGACY_TEMP_EXPLOSION1_NL = 62,
    WORR_EVENT_LEGACY_TEMP_EXPLOSION2_NL = 63,
    WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT = 128,
} worr_event_legacy_temp_id_v1;

enum {
    WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1 = 1u << 0,
    WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2 = 1u << 1,
    WORR_EVENT_LEGACY_TEMP_FIELD_OFFSET = 1u << 2,
    WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION = 1u << 3,
    WORR_EVENT_LEGACY_TEMP_FIELD_COUNT = 1u << 4,
    WORR_EVENT_LEGACY_TEMP_FIELD_COLOR = 1u << 5,
    WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1 = 1u << 6,
    WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2 = 1u << 7,
    WORR_EVENT_LEGACY_TEMP_FIELD_TIME = 1u << 8,
};

typedef enum worr_event_muzzle_family_v1_e {
    WORR_EVENT_MUZZLE_FAMILY_PLAYER = 1,
    WORR_EVENT_MUZZLE_FAMILY_MONSTER = 2,
} worr_event_muzzle_family_v1;

/* Stable player-muzzle values.  Values 21..29 remain unassigned. */
typedef enum worr_event_player_muzzle_id_v1_e {
    WORR_EVENT_PLAYER_MUZZLE_BLASTER = 0,
    WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN = 1,
    WORR_EVENT_PLAYER_MUZZLE_SHOTGUN = 2,
    WORR_EVENT_PLAYER_MUZZLE_CHAINGUN1 = 3,
    WORR_EVENT_PLAYER_MUZZLE_CHAINGUN2 = 4,
    WORR_EVENT_PLAYER_MUZZLE_CHAINGUN3 = 5,
    WORR_EVENT_PLAYER_MUZZLE_RAILGUN = 6,
    WORR_EVENT_PLAYER_MUZZLE_ROCKET = 7,
    WORR_EVENT_PLAYER_MUZZLE_GRENADE = 8,
    WORR_EVENT_PLAYER_MUZZLE_LOGIN = 9,
    WORR_EVENT_PLAYER_MUZZLE_LOGOUT = 10,
    WORR_EVENT_PLAYER_MUZZLE_RESPAWN = 11,
    WORR_EVENT_PLAYER_MUZZLE_BFG = 12,
    WORR_EVENT_PLAYER_MUZZLE_SUPER_SHOTGUN = 13,
    WORR_EVENT_PLAYER_MUZZLE_HYPERBLASTER = 14,
    WORR_EVENT_PLAYER_MUZZLE_ITEM_RESPAWN = 15,
    WORR_EVENT_PLAYER_MUZZLE_IONRIPPER = 16,
    WORR_EVENT_PLAYER_MUZZLE_BLUEHYPERBLASTER = 17,
    WORR_EVENT_PLAYER_MUZZLE_PHALANX = 18,
    WORR_EVENT_PLAYER_MUZZLE_BFG2 = 19,
    WORR_EVENT_PLAYER_MUZZLE_PHALANX2 = 20,
    WORR_EVENT_PLAYER_MUZZLE_ETF_RIFLE = 30,
    WORR_EVENT_PLAYER_MUZZLE_PROX = 31,
    WORR_EVENT_PLAYER_MUZZLE_ETF_RIFLE2 = 32,
    WORR_EVENT_PLAYER_MUZZLE_HEATBEAM = 33,
    WORR_EVENT_PLAYER_MUZZLE_BLASTER2 = 34,
    WORR_EVENT_PLAYER_MUZZLE_TRACKER = 35,
    WORR_EVENT_PLAYER_MUZZLE_NUKE1 = 36,
    WORR_EVENT_PLAYER_MUZZLE_NUKE2 = 37,
    WORR_EVENT_PLAYER_MUZZLE_NUKE4 = 38,
    WORR_EVENT_PLAYER_MUZZLE_NUKE8 = 39,
} worr_event_player_muzzle_id_v1;

enum {
    WORR_EVENT_PLAYER_MUZZLE_RESERVED_FIRST = 21,
    WORR_EVENT_PLAYER_MUZZLE_RESERVED_LAST = 29,
    WORR_EVENT_MONSTER_MUZZLE_FIRST = 1,
    WORR_EVENT_MONSTER_MUZZLE_LAST = 293,
    WORR_EVENT_MUZZLE_FLAG_SILENCED = 1u << 0,
};

enum {
    WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL = 1u << 0,
    WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION = 1u << 1,
    WORR_EVENT_SPATIAL_AUDIO_RELIABLE = 1u << 2,
    WORR_EVENT_SPATIAL_AUDIO_NO_PHS = 1u << 3,
    WORR_EVENT_SPATIAL_AUDIO_LOCAL_ONLY = 1u << 4,
    WORR_EVENT_SPATIAL_AUDIO_POSITION_FORCED = 1u << 5,
};

typedef struct worr_event_payload_vec3_v1_s {
    float value[3];
} worr_event_payload_vec3_v1;

typedef struct worr_event_payload_entity_ref_v1_s {
    worr_event_entity_ref_v1 entity;
} worr_event_payload_entity_ref_v1;

typedef struct worr_event_payload_damage_v1_s {
    float amount;
    float impulse;
    float direction[3];
    float point[3];
    uint32_t damage_flags;
    uint32_t means_of_death;
} worr_event_payload_damage_v1;

typedef struct worr_event_payload_audio_v1_s {
    uint32_t asset_id;
    uint32_t channel;
    float volume;
    float attenuation;
    float pitch;
} worr_event_payload_audio_v1;

typedef struct worr_event_payload_effect_v1_s {
    uint32_t effect_id;
    uint32_t variant;
    float origin[3];
    float direction[3];
} worr_event_payload_effect_v1;

typedef struct worr_event_payload_u32x4_v1_s {
    uint32_t value[4];
} worr_event_payload_u32x4_v1;

typedef struct worr_event_payload_legacy_entity_v1_s {
    uint16_t raw_event;
    uint16_t flags;
    uint32_t reserved0;
} worr_event_payload_legacy_entity_v1;

typedef struct worr_event_payload_legacy_temp_v1_s {
    uint16_t subtype;
    uint16_t valid_fields;
    /* Historical wire slot names.  Depending on subtype these are entity
     * indices, sustain IDs, or a particle magnitude; valid_fields plus subtype
     * determine the role. */
    int16_t raw_entity1;
    int16_t raw_entity2;
    int32_t time_ms;
    int32_t count_or_amount;
    int32_t color;
    float position1[3];
    float position2[3];
    float offset[3];
    float direction[3];
    uint32_t reserved0;
} worr_event_payload_legacy_temp_v1;

typedef struct worr_event_payload_muzzle_v1_s {
    uint16_t family;
    uint16_t flash_id;
    uint32_t flags;
} worr_event_payload_muzzle_v1;

typedef struct worr_event_payload_spatial_audio_v1_s {
    uint32_t asset_id;
    uint16_t channel;
    uint16_t flags;
    uint32_t raw_entity;
    float origin[3];
    float volume;
    float attenuation;
    float time_offset;
    float pitch;
} worr_event_payload_spatial_audio_v1;

/*
 * Pointer-free, transport-neutral canonical event record.  All unused payload
 * bytes and reserved fields are zero.  source_time_us is simulation time, not
 * a wall clock.  expiry_tick is an absolute, wrap-safe simulation tick for
 * cosmetic/transient events and zero for retained delivery classes.
 */
typedef struct worr_event_record_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t flags;
    worr_event_id_v1 event_id;
    uint32_t source_tick;
    uint32_t source_ordinal;
    uint64_t source_time_us;
    worr_event_entity_ref_v1 source_entity;
    worr_event_entity_ref_v1 subject_entity;
    uint16_t event_type;
    uint8_t delivery_class;
    uint8_t prediction_class;
    worr_event_prediction_key_v1 prediction_key;
    uint32_t expiry_tick;
    uint16_t payload_kind;
    uint16_t payload_size;
    uint32_t reserved0;
    uint8_t payload[WORR_EVENT_PAYLOAD_CAPACITY];
} worr_event_record_v1;

/* Selective receipt acknowledgement.  highest_contiguous == 0 means no event
 * is contiguous yet.  Bit n acknowledges highest_contiguous + 1 + n. */
typedef struct worr_event_receipt_ack_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t stream_epoch;
    uint32_t highest_contiguous;
    uint64_t selective_mask;
} worr_event_receipt_ack_v1;

typedef enum worr_event_receipt_result_v1_e {
    WORR_EVENT_RECEIPT_ACCEPTED = 0,
    WORR_EVENT_RECEIPT_DUPLICATE = 1,
    WORR_EVENT_RECEIPT_WRONG_EPOCH = 2,
    WORR_EVENT_RECEIPT_OUTSIDE_WINDOW = 3,
    WORR_EVENT_RECEIPT_INVALID = 4,
} worr_event_receipt_result_v1;

bool Worr_EventEntityRefValidV1(worr_event_entity_ref_v1 ref,
                                uint32_t max_entities,
                                bool allow_absent);
bool Worr_EventRecordValidateV1(const worr_event_record_v1 *record,
                                uint32_t max_entities);
/* Validates an ID-less pre-authority record.  Unlike the authoritative
 * validator this accepts AUTHORITATIVE_ONLY candidates while still requiring
 * a zero event ID and an absent HAS_AUTHORITY_ID flag. */
bool Worr_EventRecordCandidateValidateV1(
    const worr_event_record_v1 *record,
    uint32_t max_entities);
bool Worr_EventMuzzleCarrierValidV1(uint32_t family,
                                    int32_t entity_index,
                                    uint32_t flash_id,
                                    uint32_t max_entities,
                                    uint32_t monster_flash_exclusive);
bool Worr_EventRecordHashV1(const worr_event_record_v1 *record,
                            uint32_t max_entities,
                            uint64_t *hash_out);
/* Semantic hashing accepts authority candidates and ignores exactly the
 * authoritative event ID and HAS_AUTHORITY_ID flag.  Hash-table users must
 * use the collision-safe equality helper after a hash match. */
bool Worr_EventRecordSemanticHashV1(const worr_event_record_v1 *record,
                                    uint32_t max_entities,
                                    uint64_t *hash_out);
bool Worr_EventRecordSemanticallyEqualV1(
    const worr_event_record_v1 *left,
    const worr_event_record_v1 *right,
    uint32_t max_entities);
bool Worr_EventLegacyTempFieldMaskV1(uint16_t subtype,
                                     int16_t raw_entity1,
                                     uint16_t *field_mask_out);
uint64_t Worr_EventPredictionKeyHashV1(
    const worr_event_prediction_key_v1 *key);

bool Worr_EventIdNextV1(worr_event_id_v1 current,
                        worr_event_id_v1 *next);

bool Worr_EventReceiptInitV1(worr_event_receipt_ack_v1 *receipt,
                             uint32_t stream_epoch);
bool Worr_EventReceiptAdvanceEpochV1(worr_event_receipt_ack_v1 *receipt,
                                     uint32_t stream_epoch);
worr_event_receipt_result_v1 Worr_EventReceiptMarkV1(
    worr_event_receipt_ack_v1 *receipt, worr_event_id_v1 event_id);
bool Worr_EventReceiptContainsV1(const worr_event_receipt_ack_v1 *receipt,
                                 worr_event_id_v1 event_id);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_EVENT_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define WORR_EVENT_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

WORR_EVENT_STATIC_ASSERT(sizeof(float) == 4,
                         "event ABI requires 32-bit floats");
WORR_EVENT_STATIC_ASSERT(FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
                             FLT_MAX_EXP == 128,
                         "event ABI requires IEEE-754 binary32 floats");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_id_v1) == 8,
                         "event ID v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_entity_ref_v1) == 8,
                         "event entity reference v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_prediction_key_v1) == 16,
                         "event prediction key v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_payload_damage_v1) == 40,
                         "event damage payload v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_payload_vec3_v1) == 12,
                         "event vector payload v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_payload_entity_ref_v1) == 8,
                         "event entity payload v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_payload_audio_v1) == 20,
                         "event audio payload v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_payload_effect_v1) == 32,
                         "event effect payload v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_payload_u32x4_v1) == 16,
                         "event integer payload v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_payload_legacy_entity_v1) == 8,
                         "legacy entity payload v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_payload_legacy_temp_v1) == 72,
                         "legacy temp payload v1 layout changed");
WORR_EVENT_STATIC_ASSERT(
    offsetof(worr_event_payload_legacy_temp_v1, position1) == 20,
    "legacy temp payload v1 position offset changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_payload_muzzle_v1) == 8,
                         "muzzle payload v1 layout changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_payload_spatial_audio_v1) == 40,
                         "spatial audio payload v1 layout changed");
WORR_EVENT_STATIC_ASSERT(
    sizeof(worr_local_interaction_authority_receipt_v1) <=
        WORR_EVENT_PAYLOAD_CAPACITY,
    "local interaction authority receipt exceeds event payload capacity");
WORR_EVENT_STATIC_ASSERT(WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1 == 7 &&
                             WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1 == 8 &&
                             WORR_EVENT_PAYLOAD_MUZZLE_V1 == 9 &&
                             WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1 == 10 &&
                             WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1 ==
                                 11,
                         "event payload catalog IDs changed");
WORR_EVENT_STATIC_ASSERT(WORR_EVENT_TYPE_AUTHORITY_RECEIPT == 9,
                         "event type catalog IDs changed");
WORR_EVENT_STATIC_ASSERT(WORR_EVENT_LEGACY_TEMP_BOSSTPORT == 22 &&
                             WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_BROKEN ==
                                 27 &&
                             WORR_EVENT_LEGACY_TEMP_FLAME_RESERVED == 32 &&
                             WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_FIXED ==
                                 56 &&
                             WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT == 128,
                         "legacy temp numeric identities changed");
WORR_EVENT_STATIC_ASSERT(WORR_EVENT_PLAYER_MUZZLE_RESERVED_FIRST == 21 &&
                             WORR_EVENT_PLAYER_MUZZLE_RESERVED_LAST == 29 &&
                             WORR_EVENT_MONSTER_MUZZLE_LAST == 293,
                         "muzzle numeric identities changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_record_v1) == 168,
                         "event record v1 layout changed");
WORR_EVENT_STATIC_ASSERT(offsetof(worr_event_record_v1, source_time_us) == 32,
                         "event record v1 source time offset changed");
WORR_EVENT_STATIC_ASSERT(offsetof(worr_event_record_v1, source_entity) == 40,
                         "event record v1 source entity offset changed");
WORR_EVENT_STATIC_ASSERT(offsetof(worr_event_record_v1, prediction_key) == 60,
                         "event record v1 prediction key offset changed");
WORR_EVENT_STATIC_ASSERT(offsetof(worr_event_record_v1, payload) == 88,
                         "event record v1 payload offset changed");
WORR_EVENT_STATIC_ASSERT(sizeof(worr_event_receipt_ack_v1) == 24,
                         "event receipt acknowledgement v1 layout changed");

#undef WORR_EVENT_STATIC_ASSERT
