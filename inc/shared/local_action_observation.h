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

#include "shared/command_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is an observational contract, not a second weapon simulator.  It
 * freezes the real sgame weapon state around one authenticated command so a
 * complete catalogue model can be proved against live authority before it is
 * allowed to predict or suppress any presentation.
 */
#define WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION 1u
#define WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS 32u
#define WORR_LOCAL_ACTION_OBSERVATION_MAX_WEAPON_ID 65535u
#define WORR_LOCAL_ACTION_OBSERVATION_MAX_AMMO_UNITS 1000000
#define WORR_LOCAL_ACTION_OBSERVATION_MAX_TIMER_MS 60000

typedef enum worr_local_action_observation_phase_v1_e {
    WORR_LOCAL_ACTION_OBSERVATION_HOLSTERED = 0,
    WORR_LOCAL_ACTION_OBSERVATION_RAISING = 1,
    WORR_LOCAL_ACTION_OBSERVATION_READY = 2,
    WORR_LOCAL_ACTION_OBSERVATION_FIRING = 3,
    WORR_LOCAL_ACTION_OBSERVATION_LOWERING = 4,
} worr_local_action_observation_phase_v1;

enum {
    WORR_LOCAL_ACTION_OBSERVATION_ATTACK_HELD = UINT32_C(1) << 0,
    WORR_LOCAL_ACTION_OBSERVATION_ATTACK_LATCHED = UINT32_C(1) << 1,
    WORR_LOCAL_ACTION_OBSERVATION_FIRE_BUFFERED = UINT32_C(1) << 2,
    WORR_LOCAL_ACTION_OBSERVATION_WEAPON_THUNK = UINT32_C(1) << 3,
    WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ALIVE = UINT32_C(1) << 4,
    WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ELIGIBLE = UINT32_C(1) << 5,
    /*
     * Off-hand Hook is an independently mapped user-command action, not a
     * selected-weapon attack. Keep its submission and actual hook lifecycle
     * visible to the command-scoped oracle before any cgame prediction is
     * allowed to infer or present it.
     */
    WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_HELD = UINT32_C(1) << 6,
    WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_ACTIVE = UINT32_C(1) << 7,
};

enum {
    WORR_LOCAL_ACTION_OBSERVATION_DIFF_FLAGS = UINT32_C(1) << 0,
    WORR_LOCAL_ACTION_OBSERVATION_DIFF_PHASE = UINT32_C(1) << 1,
    WORR_LOCAL_ACTION_OBSERVATION_DIFF_ACTIVE_WEAPON = UINT32_C(1) << 2,
    WORR_LOCAL_ACTION_OBSERVATION_DIFF_PENDING_WEAPON = UINT32_C(1) << 3,
    WORR_LOCAL_ACTION_OBSERVATION_DIFF_ACTIVE_AMMO = UINT32_C(1) << 4,
    WORR_LOCAL_ACTION_OBSERVATION_DIFF_PRESENTATION_FRAME = UINT32_C(1) << 5,
    WORR_LOCAL_ACTION_OBSERVATION_DIFF_PRESENTATION_RATE = UINT32_C(1) << 6,
    WORR_LOCAL_ACTION_OBSERVATION_DIFF_TIMERS = UINT32_C(1) << 7,
    WORR_LOCAL_ACTION_OBSERVATION_DIFF_INVENTORY = UINT32_C(1) << 8,
};

typedef struct worr_local_action_observation_state_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t flags;
    uint32_t phase;
    uint64_t inventory_hash;
    uint32_t active_weapon_id;
    uint32_t pending_weapon_id;
    uint32_t active_ammo_item_id;
    int32_t active_ammo_units;
    uint32_t presentation_frame;
    uint32_t presentation_rate;
    int32_t think_remaining_ms;
    int32_t fire_remaining_ms;
    uint32_t reserved0;
    uint32_t reserved1;
} worr_local_action_observation_state_v1;

typedef struct worr_local_action_observation_record_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t client_index;
    uint32_t difference_bits;
    worr_command_record_v1 command;
    worr_local_action_observation_state_v1 state_before;
    worr_local_action_observation_state_v1 state_after;
    uint64_t semantic_hash;
} worr_local_action_observation_record_v1;

bool Worr_LocalActionObservationStateValidateV1(
    const worr_local_action_observation_state_v1 *state);

/* output must be entirely zero; failures leave it byte-identical. */
bool Worr_LocalActionObservationBuildV1(
    uint32_t client_index, const worr_command_record_v1 *command,
    const worr_local_action_observation_state_v1 *state_before,
    const worr_local_action_observation_state_v1 *state_after,
    worr_local_action_observation_record_v1 *record_out);
bool Worr_LocalActionObservationRecordValidateV1(
    const worr_local_action_observation_record_v1 *record);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LOCAL_ACTION_OBSERVATION_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_LOCAL_ACTION_OBSERVATION_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_LOCAL_ACTION_OBSERVATION_STATIC_ASSERT(
    sizeof(worr_local_action_observation_state_v1) == 64,
    "local-action observation state v1 layout changed");
WORR_LOCAL_ACTION_OBSERVATION_STATIC_ASSERT(
    offsetof(worr_local_action_observation_state_v1, inventory_hash) == 16,
    "local-action observation inventory hash offset changed");
WORR_LOCAL_ACTION_OBSERVATION_STATIC_ASSERT(
    sizeof(worr_local_action_observation_record_v1) == 256,
    "local-action observation record v1 layout changed");
WORR_LOCAL_ACTION_OBSERVATION_STATIC_ASSERT(
    offsetof(worr_local_action_observation_record_v1, command) == 16,
    "local-action observation command offset changed");
WORR_LOCAL_ACTION_OBSERVATION_STATIC_ASSERT(
    offsetof(worr_local_action_observation_record_v1, state_before) == 120,
    "local-action observation before-state offset changed");
WORR_LOCAL_ACTION_OBSERVATION_STATIC_ASSERT(
    offsetof(worr_local_action_observation_record_v1, state_after) == 184,
    "local-action observation after-state offset changed");

#undef WORR_LOCAL_ACTION_OBSERVATION_STATIC_ASSERT
