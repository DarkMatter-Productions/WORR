/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/local_action_observation.h"

#include <string.h>

#define LOCAL_ACTION_OBSERVATION_KNOWN_FLAGS                              \
    (WORR_LOCAL_ACTION_OBSERVATION_ATTACK_HELD |                         \
     WORR_LOCAL_ACTION_OBSERVATION_ATTACK_LATCHED |                      \
     WORR_LOCAL_ACTION_OBSERVATION_FIRE_BUFFERED |                       \
     WORR_LOCAL_ACTION_OBSERVATION_WEAPON_THUNK |                        \
     WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ALIVE |                        \
     WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ELIGIBLE |                     \
     WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_HELD |                   \
     WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_ACTIVE)

static uint64_t hash_begin(void)
{
    return UINT64_C(1469598103934665603);
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    unsigned int index;
    for (index = 0; index != 4; ++index) {
        hash ^= (uint8_t)(value & UINT32_C(0xff));
        hash *= UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int index;
    for (index = 0; index != 8; ++index) {
        hash ^= (uint8_t)(value & UINT64_C(0xff));
        hash *= UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}

static bool output_is_zero(const void *output, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)output;
    size_t index;

    if (!output)
        return false;
    for (index = 0; index != size; ++index) {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

static bool phase_valid(uint32_t phase)
{
    return phase <= WORR_LOCAL_ACTION_OBSERVATION_LOWERING;
}

static bool timer_valid(int32_t value)
{
    return value >= -WORR_LOCAL_ACTION_OBSERVATION_MAX_TIMER_MS &&
           value <= WORR_LOCAL_ACTION_OBSERVATION_MAX_TIMER_MS;
}

bool Worr_LocalActionObservationStateValidateV1(
    const worr_local_action_observation_state_v1 *state)
{
    if (!state || state->struct_size != sizeof(*state) ||
        state->schema_version != WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION ||
        (state->flags & ~LOCAL_ACTION_OBSERVATION_KNOWN_FLAGS) != 0 ||
        !phase_valid(state->phase) ||
        state->active_weapon_id > WORR_LOCAL_ACTION_OBSERVATION_MAX_WEAPON_ID ||
        state->pending_weapon_id > WORR_LOCAL_ACTION_OBSERVATION_MAX_WEAPON_ID ||
        state->active_ammo_item_id >
            WORR_LOCAL_ACTION_OBSERVATION_MAX_WEAPON_ID ||
        state->active_ammo_units < 0 ||
        state->active_ammo_units >
            WORR_LOCAL_ACTION_OBSERVATION_MAX_AMMO_UNITS ||
        state->presentation_rate > 1000 ||
        !timer_valid(state->think_remaining_ms) ||
        !timer_valid(state->fire_remaining_ms) || state->reserved0 != 0 ||
        state->reserved1 != 0) {
        return false;
    }

    if (state->phase == WORR_LOCAL_ACTION_OBSERVATION_HOLSTERED &&
        state->active_weapon_id != 0)
        return false;
    return true;
}

static uint32_t difference_bits(
    const worr_local_action_observation_state_v1 *before,
    const worr_local_action_observation_state_v1 *after)
{
    uint32_t bits = 0;
    if (before->flags != after->flags)
        bits |= WORR_LOCAL_ACTION_OBSERVATION_DIFF_FLAGS;
    if (before->phase != after->phase)
        bits |= WORR_LOCAL_ACTION_OBSERVATION_DIFF_PHASE;
    if (before->active_weapon_id != after->active_weapon_id)
        bits |= WORR_LOCAL_ACTION_OBSERVATION_DIFF_ACTIVE_WEAPON;
    if (before->pending_weapon_id != after->pending_weapon_id)
        bits |= WORR_LOCAL_ACTION_OBSERVATION_DIFF_PENDING_WEAPON;
    if (before->active_ammo_item_id != after->active_ammo_item_id ||
        before->active_ammo_units != after->active_ammo_units) {
        bits |= WORR_LOCAL_ACTION_OBSERVATION_DIFF_ACTIVE_AMMO;
    }
    if (before->presentation_frame != after->presentation_frame)
        bits |= WORR_LOCAL_ACTION_OBSERVATION_DIFF_PRESENTATION_FRAME;
    if (before->presentation_rate != after->presentation_rate)
        bits |= WORR_LOCAL_ACTION_OBSERVATION_DIFF_PRESENTATION_RATE;
    if (before->think_remaining_ms != after->think_remaining_ms ||
        before->fire_remaining_ms != after->fire_remaining_ms) {
        bits |= WORR_LOCAL_ACTION_OBSERVATION_DIFF_TIMERS;
    }
    if (before->inventory_hash != after->inventory_hash)
        bits |= WORR_LOCAL_ACTION_OBSERVATION_DIFF_INVENTORY;
    return bits;
}

static uint64_t hash_state(uint64_t hash,
                           const worr_local_action_observation_state_v1 *state)
{
    hash = hash_u32(hash, state->flags);
    hash = hash_u32(hash, state->phase);
    hash = hash_u64(hash, state->inventory_hash);
    hash = hash_u32(hash, state->active_weapon_id);
    hash = hash_u32(hash, state->pending_weapon_id);
    hash = hash_u32(hash, state->active_ammo_item_id);
    hash = hash_u32(hash, (uint32_t)state->active_ammo_units);
    hash = hash_u32(hash, state->presentation_frame);
    hash = hash_u32(hash, state->presentation_rate);
    hash = hash_u32(hash, (uint32_t)state->think_remaining_ms);
    return hash_u32(hash, (uint32_t)state->fire_remaining_ms);
}

static bool semantic_hash(uint32_t client_index,
                          const worr_command_record_v1 *command,
                          const worr_local_action_observation_state_v1 *before,
                          const worr_local_action_observation_state_v1 *after,
                          uint32_t bits, uint64_t *hash_out)
{
    uint64_t command_hash;
    uint64_t hash;

    if (!hash_out || !Worr_CommandRecordSemanticHashV1(
                         command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
                         &command_hash)) {
        return false;
    }
    hash = hash_begin();
    hash = hash_u32(hash, UINT32_C(0x4c414f31)); /* LAO1 */
    hash = hash_u32(hash, client_index);
    hash = hash_u64(hash, command_hash);
    hash = hash_u32(hash, bits);
    hash = hash_state(hash, before);
    *hash_out = hash_state(hash, after);
    return true;
}

bool Worr_LocalActionObservationBuildV1(
    uint32_t client_index, const worr_command_record_v1 *command,
    const worr_local_action_observation_state_v1 *state_before,
    const worr_local_action_observation_state_v1 *state_after,
    worr_local_action_observation_record_v1 *record_out)
{
    worr_local_action_observation_record_v1 candidate;

    if (!output_is_zero(record_out, sizeof(*record_out)) ||
        client_index >= WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS ||
        !Worr_CommandRecordValidateV1(
            command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ||
        !Worr_LocalActionObservationStateValidateV1(state_before) ||
        !Worr_LocalActionObservationStateValidateV1(state_after)) {
        return false;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION;
    candidate.client_index = client_index;
    candidate.difference_bits = difference_bits(state_before, state_after);
    candidate.command = *command;
    candidate.state_before = *state_before;
    candidate.state_after = *state_after;
    if (!semantic_hash(client_index, command, state_before, state_after,
                       candidate.difference_bits, &candidate.semantic_hash)) {
        return false;
    }
    *record_out = candidate;
    return true;
}

bool Worr_LocalActionObservationRecordValidateV1(
    const worr_local_action_observation_record_v1 *record)
{
    uint64_t expected_hash;

    if (!record || record->struct_size != sizeof(*record) ||
        record->schema_version != WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION ||
        record->client_index >= WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS ||
        !Worr_CommandRecordValidateV1(
            &record->command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ||
        !Worr_LocalActionObservationStateValidateV1(&record->state_before) ||
        !Worr_LocalActionObservationStateValidateV1(&record->state_after) ||
        record->difference_bits !=
            difference_bits(&record->state_before, &record->state_after)) {
        return false;
    }
    return semantic_hash(record->client_index, &record->command,
                         &record->state_before, &record->state_after,
                         record->difference_bits, &expected_hash) &&
           expected_hash == record->semantic_hash;
}
