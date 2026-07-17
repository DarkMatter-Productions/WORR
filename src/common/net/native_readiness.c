/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_readiness.h"

#include <limits.h>
#include <string.h>

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes)
{
    const uintptr_t left_begin = (uintptr_t)left;
    const uintptr_t right_begin = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left == NULL || right == NULL || left_bytes == 0 || right_bytes == 0)
        return false;
    if (left_bytes > UINTPTR_MAX - left_begin ||
        right_bytes > UINTPTR_MAX - right_begin) {
        return true;
    }
    left_end = left_begin + left_bytes;
    right_end = right_begin + right_bytes;
    return left_begin < right_end && right_begin < left_end;
}

static void counter_increment(uint64_t *counter)
{
    if (*counter != UINT64_MAX)
        ++*counter;
}

static bool role_valid(uint16_t role)
{
    return role == WORR_NATIVE_READINESS_ROLE_SERVER ||
           role == WORR_NATIVE_READINESS_ROLE_CLIENT;
}

static bool record_kind_valid(uint16_t record_kind)
{
    return record_kind >= WORR_NATIVE_READINESS_RECORD_CHALLENGE &&
           record_kind <=
               WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM;
}

static bool capability_mask_valid(uint32_t capabilities)
{
    return (capabilities & ~WORR_NET_CAP_KNOWN_MASK) == 0 &&
           (capabilities & WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK) ==
               WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK;
}

static bool snapshot_epoch_binding_valid(uint32_t capabilities,
                                         uint32_t snapshot_epoch)
{
    const bool snapshot_capable =
        (capabilities & WORR_NET_CAP_CANONICAL_SNAPSHOT_V2) != 0;

    return snapshot_capable == (snapshot_epoch != 0);
}

static bool active_confirm_required(uint32_t capabilities)
{
    return (capabilities &
            (WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |
             WORR_NET_CAP_CANONICAL_SNAPSHOT_V2)) != 0;
}

static bool record_kind_binding_valid(uint16_t record_kind,
                                      uint32_t capabilities)
{
    return record_kind !=
               WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM ||
           active_confirm_required(capabilities);
}

static bool phase_waiting(uint16_t phase)
{
    return phase == WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY ||
           phase == WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE ||
           phase == WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE ||
           phase ==
               WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM;
}

static bool phase_valid_for_role(uint16_t role, uint16_t phase)
{
    if (phase == WORR_NATIVE_READINESS_PHASE_FAILED)
        return true;
    if (role == WORR_NATIVE_READINESS_ROLE_SERVER) {
        return phase ==
                   WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY ||
               phase ==
                   WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM ||
               phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE;
    }
    if (role == WORR_NATIVE_READINESS_ROLE_CLIENT) {
        return phase == WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE ||
               phase ==
                   WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE ||
               phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE;
    }
    return false;
}

static uint32_t crc32_byte(uint32_t crc, uint8_t value)
{
    unsigned bit;

    crc ^= value;
    for (bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
    return crc;
}

static uint32_t crc32_u16_le(uint32_t crc, uint16_t value)
{
    crc = crc32_byte(crc, (uint8_t)value);
    return crc32_byte(crc, (uint8_t)(value >> 8));
}

static uint32_t crc32_u32_le(uint32_t crc, uint32_t value)
{
    unsigned index;

    for (index = 0; index < 4; ++index)
        crc = crc32_byte(crc, (uint8_t)(value >> (index * 8u)));
    return crc;
}

static uint32_t crc32_u64_le(uint32_t crc, uint64_t value)
{
    unsigned index;

    for (index = 0; index < 8; ++index)
        crc = crc32_byte(crc, (uint8_t)(value >> (index * 8u)));
    return crc;
}

static uint32_t record_checksum(
    const worr_native_readiness_record_v1 *record)
{
    uint32_t crc = UINT32_MAX;

    crc = crc32_byte(crc, (uint8_t)'W');
    crc = crc32_byte(crc, (uint8_t)'N');
    crc = crc32_byte(crc, (uint8_t)'R');
    crc = crc32_byte(crc, (uint8_t)'1');
    crc = crc32_u32_le(crc, record->struct_size);
    crc = crc32_u16_le(crc, record->schema_version);
    crc = crc32_u16_le(crc, record->record_kind);
    crc = crc32_u32_le(crc, record->transport_epoch);
    crc = crc32_u32_le(crc, record->negotiated_capabilities);
    crc = crc32_u64_le(crc, record->readiness_nonce);
    crc = crc32_u32_le(crc, record->snapshot_epoch);
    return ~crc;
}

static bool record_init_local(worr_native_readiness_record_v1 *record_out,
                              uint16_t record_kind,
                              uint32_t transport_epoch,
                              uint32_t snapshot_epoch,
                              uint32_t negotiated_capabilities,
                              uint64_t readiness_nonce)
{
    worr_native_readiness_record_v1 record;

    if (record_out == NULL || !record_kind_valid(record_kind) ||
        transport_epoch == 0 ||
        !capability_mask_valid(negotiated_capabilities) ||
        !snapshot_epoch_binding_valid(negotiated_capabilities,
                                      snapshot_epoch) ||
        !record_kind_binding_valid(record_kind,
                                   negotiated_capabilities) ||
        readiness_nonce == 0) {
        return false;
    }
    memset(&record, 0, sizeof(record));
    record.struct_size = (uint32_t)sizeof(record);
    record.schema_version = WORR_NATIVE_READINESS_ABI_VERSION;
    record.record_kind = record_kind;
    record.transport_epoch = transport_epoch;
    record.negotiated_capabilities = negotiated_capabilities;
    record.readiness_nonce = readiness_nonce;
    record.snapshot_epoch = snapshot_epoch;
    record.record_checksum = record_checksum(&record);
    *record_out = record;
    return true;
}

bool Worr_NativeReadinessRecordInitV1(
    worr_native_readiness_record_v1 *record_out, uint16_t record_kind,
    uint32_t transport_epoch, uint32_t negotiated_capabilities,
    uint64_t readiness_nonce)
{
    return Worr_NativeReadinessRecordInitBoundV1(
        record_out, record_kind, transport_epoch, 0,
        negotiated_capabilities, readiness_nonce);
}

bool Worr_NativeReadinessRecordInitBoundV1(
    worr_native_readiness_record_v1 *record_out, uint16_t record_kind,
    uint32_t transport_epoch, uint32_t snapshot_epoch,
    uint32_t negotiated_capabilities, uint64_t readiness_nonce)
{
    worr_native_readiness_record_v1 record;

    if (record_out == NULL ||
        !record_init_local(&record, record_kind, transport_epoch,
                           snapshot_epoch, negotiated_capabilities,
                           readiness_nonce)) {
        return false;
    }
    *record_out = record;
    return true;
}

bool Worr_NativeReadinessRecordValidateV1(
    const worr_native_readiness_record_v1 *record)
{
    return record != NULL && record->struct_size == sizeof(*record) &&
           record->schema_version == WORR_NATIVE_READINESS_ABI_VERSION &&
           record_kind_valid(record->record_kind) &&
           record->transport_epoch != 0 &&
           capability_mask_valid(record->negotiated_capabilities) &&
           snapshot_epoch_binding_valid(record->negotiated_capabilities,
                                        record->snapshot_epoch) &&
           record_kind_binding_valid(record->record_kind,
                                     record->negotiated_capabilities) &&
           record->readiness_nonce != 0 &&
           record->record_checksum == record_checksum(record);
}

bool Worr_NativeReadinessStateValidateV1(
    const worr_native_readiness_state_v1 *state)
{
    if (state == NULL || state->struct_size != sizeof(*state) ||
        state->schema_version != WORR_NATIVE_READINESS_ABI_VERSION ||
        !role_valid(state->role) ||
        !phase_valid_for_role(state->role, state->phase) ||
        state->state_flags != WORR_NATIVE_READINESS_STATE_INITIALIZED ||
        state->transport_epoch == 0 ||
        !capability_mask_valid(state->negotiated_capabilities) ||
        !snapshot_epoch_binding_valid(state->negotiated_capabilities,
                                      state->snapshot_epoch) ||
        state->generation == 0 ||
        state->timeout_ticks == 0 || state->last_tick < state->phase_start_tick) {
        return false;
    }

    if (phase_waiting(state->phase)) {
        if (state->timeout_ticks > UINT64_MAX - state->phase_start_tick ||
            state->deadline_tick !=
                state->phase_start_tick + state->timeout_ticks ||
            state->deadline_tick <= state->last_tick) {
            return false;
        }
    } else if (state->deadline_tick != 0) {
        return false;
    }

    if (state->phase == WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE) {
        return state->role == WORR_NATIVE_READINESS_ROLE_CLIENT &&
               state->readiness_nonce == 0 &&
               state->nonce_floor != UINT64_MAX;
    }
    if (state->phase ==
            WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM &&
        !active_confirm_required(state->negotiated_capabilities)) {
        return false;
    }
    if (state->readiness_nonce != 0 &&
        state->readiness_nonce <= state->nonce_floor) {
        return false;
    }
    if (state->role == WORR_NATIVE_READINESS_ROLE_SERVER &&
        state->readiness_nonce == 0) {
        return false;
    }
    if (state->phase != WORR_NATIVE_READINESS_PHASE_FAILED &&
        state->readiness_nonce == 0) {
        return false;
    }
    return true;
}

static bool deadline_make(uint64_t now_tick, uint64_t timeout_ticks,
                          uint64_t *deadline_out)
{
    if (timeout_ticks == 0 || timeout_ticks > UINT64_MAX - now_tick)
        return false;
    *deadline_out = now_tick + timeout_ticks;
    return true;
}

static void state_header_init(worr_native_readiness_state_v1 *state,
                              uint16_t role,
                              uint16_t phase,
                              uint32_t transport_epoch,
                              uint32_t snapshot_epoch,
                              uint32_t negotiated_capabilities,
                              uint64_t now_tick,
                              uint64_t timeout_ticks,
                              uint64_t deadline_tick)
{
    memset(state, 0, sizeof(*state));
    state->struct_size = (uint32_t)sizeof(*state);
    state->schema_version = WORR_NATIVE_READINESS_ABI_VERSION;
    state->role = role;
    state->phase = phase;
    state->state_flags = WORR_NATIVE_READINESS_STATE_INITIALIZED;
    state->transport_epoch = transport_epoch;
    state->negotiated_capabilities = negotiated_capabilities;
    state->snapshot_epoch = snapshot_epoch;
    state->generation = 1;
    state->timeout_ticks = timeout_ticks;
    state->phase_start_tick = now_tick;
    state->deadline_tick = deadline_tick;
    state->last_tick = now_tick;
}

static worr_native_readiness_result_v1 init_arguments_validate(
    uint32_t transport_epoch, uint32_t snapshot_epoch,
    uint32_t negotiated_capabilities, uint64_t now_tick,
    uint64_t timeout_ticks, uint64_t *deadline_out)
{
    if (transport_epoch == 0 ||
        !capability_mask_valid(negotiated_capabilities) || timeout_ticks == 0) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    if (!snapshot_epoch_binding_valid(negotiated_capabilities,
                                      snapshot_epoch)) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    if (!deadline_make(now_tick, timeout_ticks, deadline_out))
        return WORR_NATIVE_READINESS_DEADLINE_OVERFLOW;
    return WORR_NATIVE_READINESS_OK;
}

worr_native_readiness_result_v1 Worr_NativeReadinessServerInitV1(
    worr_native_readiness_state_v1 *state_out, uint32_t transport_epoch,
    uint32_t negotiated_capabilities, uint64_t readiness_nonce,
    uint64_t now_tick, uint64_t timeout_ticks,
    worr_native_readiness_record_v1 *challenge_out)
{
    return Worr_NativeReadinessServerInitBoundV1(
        state_out, transport_epoch, 0, negotiated_capabilities,
        readiness_nonce, now_tick, timeout_ticks, challenge_out);
}

worr_native_readiness_result_v1 Worr_NativeReadinessServerInitBoundV1(
    worr_native_readiness_state_v1 *state_out, uint32_t transport_epoch,
    uint32_t snapshot_epoch, uint32_t negotiated_capabilities,
    uint64_t readiness_nonce, uint64_t now_tick, uint64_t timeout_ticks,
    worr_native_readiness_record_v1 *challenge_out)
{
    worr_native_readiness_state_v1 state;
    worr_native_readiness_record_v1 challenge;
    uint64_t deadline;
    worr_native_readiness_result_v1 result;

    if (state_out == NULL || challenge_out == NULL || readiness_nonce == 0 ||
        ranges_overlap(state_out, sizeof(*state_out), challenge_out,
                       sizeof(*challenge_out))) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    result = init_arguments_validate(
        transport_epoch, snapshot_epoch, negotiated_capabilities,
        now_tick, timeout_ticks, &deadline);
    if (result != WORR_NATIVE_READINESS_OK)
        return result;
    state_header_init(
        &state, WORR_NATIVE_READINESS_ROLE_SERVER,
        WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY,
        transport_epoch, snapshot_epoch, negotiated_capabilities, now_tick,
        timeout_ticks, deadline);
    state.readiness_nonce = readiness_nonce;
    counter_increment(&state.telemetry.challenges_emitted);
    if (!record_init_local(&challenge,
                           WORR_NATIVE_READINESS_RECORD_CHALLENGE,
                           transport_epoch, snapshot_epoch,
                           negotiated_capabilities,
                           readiness_nonce) ||
        !Worr_NativeReadinessStateValidateV1(&state)) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    *state_out = state;
    *challenge_out = challenge;
    return WORR_NATIVE_READINESS_OK;
}

worr_native_readiness_result_v1 Worr_NativeReadinessClientInitV1(
    worr_native_readiness_state_v1 *state_out, uint32_t transport_epoch,
    uint32_t negotiated_capabilities, uint64_t now_tick,
    uint64_t timeout_ticks)
{
    return Worr_NativeReadinessClientInitBoundV1(
        state_out, transport_epoch, 0, negotiated_capabilities,
        now_tick, timeout_ticks);
}

worr_native_readiness_result_v1 Worr_NativeReadinessClientInitBoundV1(
    worr_native_readiness_state_v1 *state_out, uint32_t transport_epoch,
    uint32_t snapshot_epoch, uint32_t negotiated_capabilities,
    uint64_t now_tick, uint64_t timeout_ticks)
{
    worr_native_readiness_state_v1 state;
    uint64_t deadline;
    worr_native_readiness_result_v1 result;

    if (state_out == NULL)
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    result = init_arguments_validate(
        transport_epoch, snapshot_epoch, negotiated_capabilities,
        now_tick, timeout_ticks, &deadline);
    if (result != WORR_NATIVE_READINESS_OK)
        return result;
    state_header_init(
        &state, WORR_NATIVE_READINESS_ROLE_CLIENT,
        WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE,
        transport_epoch, snapshot_epoch, negotiated_capabilities, now_tick,
        timeout_ticks, deadline);
    if (!Worr_NativeReadinessStateValidateV1(&state))
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    *state_out = state;
    return WORR_NATIVE_READINESS_OK;
}

static void state_fail(worr_native_readiness_state_v1 *state,
                       uint64_t *category_counter)
{
    state->phase = WORR_NATIVE_READINESS_PHASE_FAILED;
    state->deadline_tick = 0;
    if (category_counter != NULL)
        counter_increment(category_counter);
    counter_increment(&state->telemetry.failures);
}

static worr_native_readiness_result_v1 state_check_time(
    worr_native_readiness_state_v1 *state, uint64_t now_tick)
{
    counter_increment(&state->telemetry.deadline_checks);
    if (now_tick < state->last_tick) {
        state_fail(state, &state->telemetry.clock_regressions);
        return WORR_NATIVE_READINESS_CLOCK_REGRESSION;
    }
    state->last_tick = now_tick;
    if (phase_waiting(state->phase) && now_tick >= state->deadline_tick) {
        state_fail(state, &state->telemetry.deadline_expirations);
        return WORR_NATIVE_READINESS_DEADLINE_EXPIRED;
    }
    return WORR_NATIVE_READINESS_OK;
}

static worr_native_readiness_result_v1 advance_common_validate(
    const worr_native_readiness_state_v1 *state, uint16_t expected_role,
    uint32_t transport_epoch, uint32_t snapshot_epoch,
    uint32_t negotiated_capabilities, uint64_t now_tick,
    uint64_t timeout_ticks, uint64_t *deadline_out)
{
    if (!Worr_NativeReadinessStateValidateV1(state) ||
        state->phase == WORR_NATIVE_READINESS_PHASE_FAILED) {
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }
    if (state->role != expected_role)
        return WORR_NATIVE_READINESS_WRONG_ROLE;
    if (state->transport_epoch == UINT32_MAX)
        return WORR_NATIVE_READINESS_EPOCH_EXHAUSTED;
    if (transport_epoch <= state->transport_epoch)
        return WORR_NATIVE_READINESS_EPOCH_NOT_NEWER;
    if (!capability_mask_valid(negotiated_capabilities) ||
        !snapshot_epoch_binding_valid(negotiated_capabilities,
                                      snapshot_epoch) ||
        timeout_ticks == 0) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    if (now_tick < state->last_tick)
        return WORR_NATIVE_READINESS_CLOCK_REGRESSION;
    if (state->generation == UINT64_MAX)
        return WORR_NATIVE_READINESS_GENERATION_EXHAUSTED;
    if (!deadline_make(now_tick, timeout_ticks, deadline_out))
        return WORR_NATIVE_READINESS_DEADLINE_OVERFLOW;
    return WORR_NATIVE_READINESS_OK;
}

worr_native_readiness_result_v1 Worr_NativeReadinessServerAdvanceEpochV1(
    worr_native_readiness_state_v1 *state, uint32_t transport_epoch,
    uint32_t negotiated_capabilities, uint64_t readiness_nonce,
    uint64_t now_tick, uint64_t timeout_ticks,
    worr_native_readiness_record_v1 *challenge_out)
{
    return Worr_NativeReadinessServerAdvanceEpochBoundV1(
        state, transport_epoch, 0, negotiated_capabilities,
        readiness_nonce, now_tick, timeout_ticks, challenge_out);
}

worr_native_readiness_result_v1
Worr_NativeReadinessServerAdvanceEpochBoundV1(
    worr_native_readiness_state_v1 *state, uint32_t transport_epoch,
    uint32_t snapshot_epoch, uint32_t negotiated_capabilities,
    uint64_t readiness_nonce, uint64_t now_tick, uint64_t timeout_ticks,
    worr_native_readiness_record_v1 *challenge_out)
{
    worr_native_readiness_state_v1 updated;
    worr_native_readiness_record_v1 challenge;
    uint64_t deadline;
    worr_native_readiness_result_v1 result;

    if (state == NULL || challenge_out == NULL ||
        ranges_overlap(state, sizeof(*state), challenge_out,
                       sizeof(*challenge_out))) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    result = advance_common_validate(
        state, WORR_NATIVE_READINESS_ROLE_SERVER, transport_epoch,
        snapshot_epoch,
        negotiated_capabilities, now_tick, timeout_ticks, &deadline);
    if (result != WORR_NATIVE_READINESS_OK)
        return result;
    if (state->readiness_nonce == UINT64_MAX)
        return WORR_NATIVE_READINESS_NONCE_EXHAUSTED;
    if (readiness_nonce <= state->readiness_nonce)
        return WORR_NATIVE_READINESS_NONCE_NOT_NEWER;

    updated = *state;
    updated.phase =
        WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY;
    updated.transport_epoch = transport_epoch;
    updated.negotiated_capabilities = negotiated_capabilities;
    updated.snapshot_epoch = snapshot_epoch;
    updated.nonce_floor = state->readiness_nonce;
    updated.readiness_nonce = readiness_nonce;
    ++updated.generation;
    updated.timeout_ticks = timeout_ticks;
    updated.phase_start_tick = now_tick;
    updated.deadline_tick = deadline;
    updated.last_tick = now_tick;
    counter_increment(&updated.telemetry.epoch_advances);
    counter_increment(&updated.telemetry.challenges_emitted);
    if (!record_init_local(&challenge,
                           WORR_NATIVE_READINESS_RECORD_CHALLENGE,
                           transport_epoch, snapshot_epoch,
                           negotiated_capabilities,
                           readiness_nonce) ||
        !Worr_NativeReadinessStateValidateV1(&updated)) {
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }
    *state = updated;
    *challenge_out = challenge;
    return WORR_NATIVE_READINESS_OK;
}

worr_native_readiness_result_v1 Worr_NativeReadinessClientAdvanceEpochV1(
    worr_native_readiness_state_v1 *state, uint32_t transport_epoch,
    uint32_t negotiated_capabilities, uint64_t now_tick,
    uint64_t timeout_ticks)
{
    return Worr_NativeReadinessClientAdvanceEpochBoundV1(
        state, transport_epoch, 0, negotiated_capabilities,
        now_tick, timeout_ticks);
}

worr_native_readiness_result_v1
Worr_NativeReadinessClientAdvanceEpochBoundV1(
    worr_native_readiness_state_v1 *state, uint32_t transport_epoch,
    uint32_t snapshot_epoch, uint32_t negotiated_capabilities,
    uint64_t now_tick, uint64_t timeout_ticks)
{
    worr_native_readiness_state_v1 updated;
    uint64_t deadline;
    uint64_t nonce_floor;
    worr_native_readiness_result_v1 result;

    if (state == NULL)
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    result = advance_common_validate(
        state, WORR_NATIVE_READINESS_ROLE_CLIENT, transport_epoch,
        snapshot_epoch,
        negotiated_capabilities, now_tick, timeout_ticks, &deadline);
    if (result != WORR_NATIVE_READINESS_OK)
        return result;
    nonce_floor = state->readiness_nonce != 0 ? state->readiness_nonce
                                              : state->nonce_floor;
    if (nonce_floor == UINT64_MAX)
        return WORR_NATIVE_READINESS_NONCE_EXHAUSTED;

    updated = *state;
    updated.phase = WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE;
    updated.transport_epoch = transport_epoch;
    updated.negotiated_capabilities = negotiated_capabilities;
    updated.snapshot_epoch = snapshot_epoch;
    updated.readiness_nonce = 0;
    updated.nonce_floor = nonce_floor;
    ++updated.generation;
    updated.timeout_ticks = timeout_ticks;
    updated.phase_start_tick = now_tick;
    updated.deadline_tick = deadline;
    updated.last_tick = now_tick;
    counter_increment(&updated.telemetry.epoch_advances);
    if (!Worr_NativeReadinessStateValidateV1(&updated))
        return WORR_NATIVE_READINESS_INVALID_STATE;
    *state = updated;
    return WORR_NATIVE_READINESS_OK;
}

static bool record_binding_equal(
    const worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *record)
{
    return record->transport_epoch == state->transport_epoch &&
           record->negotiated_capabilities ==
               state->negotiated_capabilities &&
           record->snapshot_epoch == state->snapshot_epoch &&
           record->readiness_nonce == state->readiness_nonce;
}

static worr_native_readiness_result_v1 observe_preamble(
    worr_native_readiness_state_v1 *updated,
    const worr_native_readiness_record_v1 *record,
    uint16_t expected_role,
    uint16_t expected_kind,
    uint64_t now_tick)
{
    worr_native_readiness_result_v1 result;

    if (updated->role != expected_role) {
        state_fail(updated, &updated->telemetry.order_failures);
        return WORR_NATIVE_READINESS_WRONG_ROLE;
    }
    result = state_check_time(updated, now_tick);
    if (result != WORR_NATIVE_READINESS_OK)
        return result;
    if (!Worr_NativeReadinessRecordValidateV1(record)) {
        state_fail(updated, &updated->telemetry.invalid_records);
        return WORR_NATIVE_READINESS_INVALID_RECORD;
    }
    if (record->record_kind != expected_kind) {
        state_fail(updated, &updated->telemetry.order_failures);
        return WORR_NATIVE_READINESS_WRONG_ORDER;
    }
    return WORR_NATIVE_READINESS_OK;
}

worr_native_readiness_result_v1 Worr_NativeReadinessClientObserveChallengeV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *challenge, uint64_t now_tick,
    worr_native_readiness_record_v1 *client_ready_out)
{
    worr_native_readiness_state_v1 updated;
    worr_native_readiness_record_v1 response;
    worr_native_readiness_result_v1 result;
    bool duplicate = false;
    uint64_t deadline;

    if (state == NULL || challenge == NULL || client_ready_out == NULL ||
        ranges_overlap(state, sizeof(*state), challenge, sizeof(*challenge)) ||
        ranges_overlap(state, sizeof(*state), client_ready_out,
                       sizeof(*client_ready_out)) ||
        ranges_overlap(challenge, sizeof(*challenge), client_ready_out,
                       sizeof(*client_ready_out))) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    if (!Worr_NativeReadinessStateValidateV1(state) ||
        state->phase == WORR_NATIVE_READINESS_PHASE_FAILED) {
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }
    updated = *state;
    result = observe_preamble(
        &updated, challenge, WORR_NATIVE_READINESS_ROLE_CLIENT,
        WORR_NATIVE_READINESS_RECORD_CHALLENGE, now_tick);
    if (result != WORR_NATIVE_READINESS_OK) {
        *state = updated;
        return result;
    }

    if (updated.phase == WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE) {
        if (challenge->transport_epoch != updated.transport_epoch ||
            challenge->negotiated_capabilities !=
                updated.negotiated_capabilities ||
            challenge->snapshot_epoch != updated.snapshot_epoch) {
            state_fail(&updated, &updated.telemetry.binding_mismatches);
            *state = updated;
            return WORR_NATIVE_READINESS_BINDING_MISMATCH;
        }
        if (updated.nonce_floor == UINT64_MAX) {
            state_fail(&updated, &updated.telemetry.binding_mismatches);
            *state = updated;
            return WORR_NATIVE_READINESS_NONCE_EXHAUSTED;
        }
        if (challenge->readiness_nonce <= updated.nonce_floor) {
            state_fail(&updated, &updated.telemetry.binding_mismatches);
            *state = updated;
            return WORR_NATIVE_READINESS_NONCE_NOT_NEWER;
        }
        if (!deadline_make(now_tick, updated.timeout_ticks, &deadline)) {
            state_fail(&updated, NULL);
            *state = updated;
            return WORR_NATIVE_READINESS_DEADLINE_OVERFLOW;
        }
        updated.readiness_nonce = challenge->readiness_nonce;
        updated.phase =
            WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE;
        updated.phase_start_tick = now_tick;
        updated.deadline_tick = deadline;
        counter_increment(&updated.telemetry.challenges_accepted);
    } else if ((updated.phase ==
                    WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE ||
                updated.phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE) &&
               record_binding_equal(&updated, challenge)) {
        duplicate = true;
        counter_increment(&updated.telemetry.exact_duplicates);
    } else if (updated.phase ==
                   WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE ||
               updated.phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE) {
        state_fail(&updated, &updated.telemetry.binding_mismatches);
        *state = updated;
        return WORR_NATIVE_READINESS_BINDING_MISMATCH;
    } else {
        state_fail(&updated, &updated.telemetry.order_failures);
        *state = updated;
        return WORR_NATIVE_READINESS_WRONG_ORDER;
    }

    if (!record_init_local(&response,
                           WORR_NATIVE_READINESS_RECORD_CLIENT_READY,
                           updated.transport_epoch,
                           updated.snapshot_epoch,
                           updated.negotiated_capabilities,
                           updated.readiness_nonce)) {
        state_fail(&updated, NULL);
        *state = updated;
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }
    counter_increment(&updated.telemetry.client_ready_emitted);
    *state = updated;
    *client_ready_out = response;
    return duplicate ? WORR_NATIVE_READINESS_EXACT_DUPLICATE
                     : WORR_NATIVE_READINESS_OK;
}

worr_native_readiness_result_v1
Worr_NativeReadinessServerObserveClientReadyV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *client_ready, uint64_t now_tick,
    worr_native_readiness_record_v1 *server_active_out)
{
    worr_native_readiness_state_v1 updated;
    worr_native_readiness_record_v1 response;
    worr_native_readiness_result_v1 result;
    bool duplicate = false;
    uint64_t deadline = 0;

    if (state == NULL || client_ready == NULL || server_active_out == NULL ||
        ranges_overlap(state, sizeof(*state), client_ready,
                       sizeof(*client_ready)) ||
        ranges_overlap(state, sizeof(*state), server_active_out,
                       sizeof(*server_active_out)) ||
        ranges_overlap(client_ready, sizeof(*client_ready), server_active_out,
                       sizeof(*server_active_out))) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    if (!Worr_NativeReadinessStateValidateV1(state) ||
        state->phase == WORR_NATIVE_READINESS_PHASE_FAILED) {
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }
    updated = *state;
    result = observe_preamble(
        &updated, client_ready, WORR_NATIVE_READINESS_ROLE_SERVER,
        WORR_NATIVE_READINESS_RECORD_CLIENT_READY, now_tick);
    if (result != WORR_NATIVE_READINESS_OK) {
        *state = updated;
        return result;
    }
    if (!record_binding_equal(&updated, client_ready)) {
        state_fail(&updated, &updated.telemetry.binding_mismatches);
        *state = updated;
        return WORR_NATIVE_READINESS_BINDING_MISMATCH;
    }
    if (updated.phase ==
        WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY) {
        if (active_confirm_required(updated.negotiated_capabilities)) {
            if (!deadline_make(now_tick, updated.timeout_ticks, &deadline)) {
                state_fail(&updated, NULL);
                *state = updated;
                return WORR_NATIVE_READINESS_DEADLINE_OVERFLOW;
            }
            updated.phase =
                WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM;
            updated.deadline_tick = deadline;
        } else {
            updated.phase = WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE;
            updated.deadline_tick = 0;
        }
        updated.phase_start_tick = now_tick;
        counter_increment(&updated.telemetry.client_ready_accepted);
    } else if (updated.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE ||
               updated.phase ==
                   WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM) {
        duplicate = true;
        counter_increment(&updated.telemetry.exact_duplicates);
    } else {
        state_fail(&updated, &updated.telemetry.order_failures);
        *state = updated;
        return WORR_NATIVE_READINESS_WRONG_ORDER;
    }
    if (!record_init_local(&response,
                           WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE,
                           updated.transport_epoch,
                           updated.snapshot_epoch,
                           updated.negotiated_capabilities,
                           updated.readiness_nonce)) {
        state_fail(&updated, NULL);
        *state = updated;
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }
    counter_increment(&updated.telemetry.server_active_emitted);
    *state = updated;
    *server_active_out = response;
    return duplicate ? WORR_NATIVE_READINESS_EXACT_DUPLICATE
                     : WORR_NATIVE_READINESS_OK;
}

static worr_native_readiness_result_v1
client_observe_server_active(
    worr_native_readiness_state_v1 *updated,
    const worr_native_readiness_record_v1 *server_active,
    uint64_t now_tick);

worr_native_readiness_result_v1
Worr_NativeReadinessClientObserveServerActiveV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *server_active,
    uint64_t now_tick)
{
    if (state == NULL || server_active == NULL ||
        ranges_overlap(state, sizeof(*state), server_active,
                       sizeof(*server_active))) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    if (!Worr_NativeReadinessStateValidateV1(state) ||
        state->phase == WORR_NATIVE_READINESS_PHASE_FAILED) {
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }
    if (active_confirm_required(state->negotiated_capabilities))
        return WORR_NATIVE_READINESS_INVALID_STATE;

    {
        worr_native_readiness_state_v1 updated = *state;
        const worr_native_readiness_result_v1 result =
            client_observe_server_active(
                &updated, server_active, now_tick);
        *state = updated;
        return result;
    }
}

static worr_native_readiness_result_v1
client_observe_server_active(
    worr_native_readiness_state_v1 *updated,
    const worr_native_readiness_record_v1 *server_active,
    uint64_t now_tick)
{
    worr_native_readiness_result_v1 result;

    result = observe_preamble(
        updated, server_active, WORR_NATIVE_READINESS_ROLE_CLIENT,
        WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE, now_tick);
    if (result != WORR_NATIVE_READINESS_OK)
        return result;
    if (!record_binding_equal(updated, server_active)) {
        state_fail(updated, &updated->telemetry.binding_mismatches);
        return WORR_NATIVE_READINESS_BINDING_MISMATCH;
    }
    if (updated->phase ==
        WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE) {
        updated->phase = WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE;
        updated->phase_start_tick = now_tick;
        updated->deadline_tick = 0;
        counter_increment(&updated->telemetry.server_active_accepted);
        return WORR_NATIVE_READINESS_OK;
    }
    if (updated->phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE) {
        counter_increment(&updated->telemetry.exact_duplicates);
        return WORR_NATIVE_READINESS_EXACT_DUPLICATE;
    }
    state_fail(updated, &updated->telemetry.order_failures);
    return WORR_NATIVE_READINESS_WRONG_ORDER;
}

worr_native_readiness_result_v1
Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *server_active,
    uint64_t now_tick,
    worr_native_readiness_record_v1 *client_active_confirm_out)
{
    worr_native_readiness_state_v1 updated;
    worr_native_readiness_record_v1 response;
    worr_native_readiness_result_v1 result;

    if (state == NULL || server_active == NULL ||
        client_active_confirm_out == NULL ||
        ranges_overlap(state, sizeof(*state), server_active,
                       sizeof(*server_active)) ||
        ranges_overlap(state, sizeof(*state), client_active_confirm_out,
                       sizeof(*client_active_confirm_out)) ||
        ranges_overlap(server_active, sizeof(*server_active),
                       client_active_confirm_out,
                       sizeof(*client_active_confirm_out))) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    if (!Worr_NativeReadinessStateValidateV1(state) ||
        state->phase == WORR_NATIVE_READINESS_PHASE_FAILED ||
        !active_confirm_required(state->negotiated_capabilities)) {
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }

    updated = *state;
    result = client_observe_server_active(
        &updated, server_active, now_tick);
    if (result != WORR_NATIVE_READINESS_OK &&
        result != WORR_NATIVE_READINESS_EXACT_DUPLICATE) {
        *state = updated;
        return result;
    }
    if (!record_init_local(
            &response,
            WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM,
            updated.transport_epoch, updated.snapshot_epoch,
            updated.negotiated_capabilities,
            updated.readiness_nonce)) {
        state_fail(&updated, NULL);
        *state = updated;
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }
    *state = updated;
    *client_active_confirm_out = response;
    return result;
}

worr_native_readiness_result_v1
Worr_NativeReadinessServerObserveClientActiveConfirmV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *client_active_confirm,
    uint64_t now_tick)
{
    worr_native_readiness_state_v1 updated;
    worr_native_readiness_result_v1 result;

    if (state == NULL || client_active_confirm == NULL ||
        ranges_overlap(state, sizeof(*state), client_active_confirm,
                       sizeof(*client_active_confirm))) {
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    }
    if (!Worr_NativeReadinessStateValidateV1(state) ||
        state->phase == WORR_NATIVE_READINESS_PHASE_FAILED ||
        !active_confirm_required(state->negotiated_capabilities)) {
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }
    updated = *state;
    result = observe_preamble(
        &updated, client_active_confirm,
        WORR_NATIVE_READINESS_ROLE_SERVER,
        WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM, now_tick);
    if (result != WORR_NATIVE_READINESS_OK) {
        *state = updated;
        return result;
    }
    if (!record_binding_equal(&updated, client_active_confirm)) {
        state_fail(&updated, &updated.telemetry.binding_mismatches);
        *state = updated;
        return WORR_NATIVE_READINESS_BINDING_MISMATCH;
    }
    if (updated.phase ==
        WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM) {
        updated.phase = WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE;
        updated.phase_start_tick = now_tick;
        updated.deadline_tick = 0;
        *state = updated;
        return WORR_NATIVE_READINESS_OK;
    }
    if (updated.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE) {
        counter_increment(&updated.telemetry.exact_duplicates);
        *state = updated;
        return WORR_NATIVE_READINESS_EXACT_DUPLICATE;
    }
    state_fail(&updated, &updated.telemetry.order_failures);
    *state = updated;
    return WORR_NATIVE_READINESS_WRONG_ORDER;
}

worr_native_readiness_result_v1 Worr_NativeReadinessCheckDeadlineV1(
    worr_native_readiness_state_v1 *state, uint64_t now_tick)
{
    worr_native_readiness_state_v1 updated;
    worr_native_readiness_result_v1 result;

    if (state == NULL)
        return WORR_NATIVE_READINESS_INVALID_ARGUMENT;
    if (!Worr_NativeReadinessStateValidateV1(state) ||
        state->phase == WORR_NATIVE_READINESS_PHASE_FAILED) {
        return WORR_NATIVE_READINESS_INVALID_STATE;
    }
    updated = *state;
    result = state_check_time(&updated, now_tick);
    *state = updated;
    return result;
}

static bool state_gate_time(worr_native_readiness_state_v1 *state,
                            uint64_t now_tick)
{
    worr_native_readiness_state_v1 updated;

    if (!Worr_NativeReadinessStateValidateV1(state) ||
        state->phase == WORR_NATIVE_READINESS_PHASE_FAILED) {
        return false;
    }
    updated = *state;
    if (state_check_time(&updated, now_tick) != WORR_NATIVE_READINESS_OK) {
        *state = updated;
        return false;
    }
    *state = updated;
    return true;
}

bool Worr_NativeReadinessCanReceiveNativeV1(
    worr_native_readiness_state_v1 *state, uint64_t now_tick)
{
    if (!state_gate_time(state, now_tick))
        return false;
    if (state->role == WORR_NATIVE_READINESS_ROLE_SERVER)
        return state->phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE ||
               (state->phase ==
                    WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM &&
                active_confirm_required(state->negotiated_capabilities));
    return state->phase ==
               WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE ||
           state->phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE;
}

bool Worr_NativeReadinessCanTransmitNativeV1(
    worr_native_readiness_state_v1 *state, uint64_t now_tick)
{
    if (!state_gate_time(state, now_tick))
        return false;
    return (state->role == WORR_NATIVE_READINESS_ROLE_SERVER &&
            state->phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE) ||
           (state->role == WORR_NATIVE_READINESS_ROLE_CLIENT &&
            state->phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
}

bool Worr_NativeReadinessResetV1(worr_native_readiness_state_v1 *state)
{
    if (state == NULL)
        return false;
    memset(state, 0, sizeof(*state));
    return true;
}
