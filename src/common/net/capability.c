/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/capability.h"

#include <stdio.h>
#include <string.h>

static bool mask_valid(uint32_t capabilities)
{
    return (capabilities & ~WORR_NET_CAP_KNOWN_MASK) == 0;
}

uint32_t Worr_NetCapabilityPublicOfferV1(bool native_enabled,
                                         bool event_enabled,
                                         bool snapshot_enabled)
{
    if (!native_enabled)
        return WORR_NET_CAP_LEGACY_STAGE_MASK;
    if (event_enabled && snapshot_enabled)
        return WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PUBLIC_MASK;
    if (event_enabled)
        return WORR_NET_CAP_NATIVE_EVENT_PUBLIC_MASK;
    if (snapshot_enabled)
        return WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK;
    return WORR_NET_CAP_NATIVE_COMMAND_PUBLIC_MASK;
}

bool Worr_NetCapabilityIsPublicNativeMaskV1(uint32_t capabilities)
{
    return capabilities == WORR_NET_CAP_NATIVE_COMMAND_PUBLIC_MASK ||
           capabilities == WORR_NET_CAP_NATIVE_EVENT_PUBLIC_MASK ||
           capabilities == WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK ||
           capabilities ==
               WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PUBLIC_MASK;
}

uint32_t Worr_NetCapabilityPublicSupportV1(
    uint32_t offered, uint32_t configured, bool native_endpoint_ready)
{
    if (native_endpoint_ready && offered == configured &&
        Worr_NetCapabilityIsPublicNativeMaskV1(configured)) {
        return configured;
    }
    return WORR_NET_CAP_LEGACY_STAGE_MASK;
}

bool Worr_NetCapabilitiesFormatV1(uint32_t capabilities,
                                  char *text_out,
                                  size_t text_capacity)
{
    int length;
    char scratch[11];

    if (!text_out || text_capacity == 0 || !mask_valid(capabilities))
        return false;
    length = snprintf(scratch, sizeof(scratch), "%u", capabilities);
    if (length < 0 || (size_t)length >= sizeof(scratch) ||
        (size_t)length >= text_capacity) {
        return false;
    }
    memcpy(text_out, scratch, (size_t)length + 1u);
    return true;
}

worr_net_capability_result_v1 Worr_NetCapabilitiesParseV1(
    const char *text, uint32_t *capabilities_out)
{
    uint32_t value = 0;
    size_t index;

    if (!text || !capabilities_out)
        return WORR_NET_CAPABILITY_INVALID_ARGUMENT;
    if (text[0] == '\0' || (text[0] == '0' && text[1] != '\0'))
        return WORR_NET_CAPABILITY_INVALID_TEXT;
    for (index = 0; text[index] != '\0'; ++index) {
        uint32_t digit;
        if (index == 10 || text[index] < '0' || text[index] > '9')
            return WORR_NET_CAPABILITY_INVALID_TEXT;
        digit = (uint32_t)(text[index] - '0');
        if (value > (UINT32_MAX - digit) / 10u)
            return WORR_NET_CAPABILITY_INVALID_TEXT;
        value = value * 10u + digit;
    }
    if (!mask_valid(value))
        return WORR_NET_CAPABILITY_UNKNOWN_BITS;
    *capabilities_out = value;
    return WORR_NET_CAPABILITY_OK;
}

bool Worr_NetCapabilityStateValidateV1(
    const worr_net_capability_state_v1 *state)
{
    if (!state || state->struct_size != sizeof(*state) ||
        state->schema_version != WORR_NET_CAPABILITY_VERSION ||
        state->phase > WORR_NET_CAPABILITY_FAILED ||
        state->connection_epoch == 0 || !mask_valid(state->offered) ||
        !mask_valid(state->supported) ||
        !mask_valid(state->peer_supported) ||
        !mask_valid(state->negotiated) ||
        state->reserved0 != 0 ||
        (state->offered & ~state->supported) != 0 ||
        (state->negotiated & ~state->offered) != 0 ||
        (state->negotiated & ~state->supported) != 0 ||
        (state->negotiated & ~state->peer_supported) != 0) {
        return false;
    }
    if (state->phase == WORR_NET_CAPABILITY_CONFIRMED)
        return state->negotiated ==
               (state->offered & state->supported & state->peer_supported);
    return state->peer_supported == 0 && state->negotiated == 0;
}

bool Worr_NetCapabilityStateInitV1(
    worr_net_capability_state_v1 *state, uint32_t connection_epoch,
    uint32_t offered, uint32_t supported)
{
    worr_net_capability_state_v1 initialized;

    if (!state || connection_epoch == 0 || !mask_valid(offered) ||
        !mask_valid(supported) || (offered & ~supported) != 0) {
        return false;
    }
    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_NET_CAPABILITY_VERSION;
    initialized.phase = WORR_NET_CAPABILITY_OFFERED;
    initialized.connection_epoch = connection_epoch;
    initialized.offered = offered;
    initialized.supported = supported;
    *state = initialized;
    return true;
}

worr_net_capability_result_v1 Worr_NetCapabilitySelectV1(
    uint32_t connection_epoch, uint32_t offered, uint32_t supported,
    worr_net_capability_confirm_v1 *confirm_out)
{
    worr_net_capability_confirm_v1 confirm;

    if (!confirm_out || connection_epoch == 0)
        return WORR_NET_CAPABILITY_INVALID_ARGUMENT;
    if (!mask_valid(offered) || !mask_valid(supported))
        return WORR_NET_CAPABILITY_UNKNOWN_BITS;
    memset(&confirm, 0, sizeof(confirm));
    confirm.struct_size = sizeof(confirm);
    confirm.schema_version = WORR_NET_CAPABILITY_VERSION;
    confirm.connection_epoch = connection_epoch;
    confirm.supported = supported;
    confirm.negotiated = offered & supported;
    *confirm_out = confirm;
    return WORR_NET_CAPABILITY_OK;
}

worr_net_capability_result_v1 Worr_NetCapabilityConfirmV1(
    worr_net_capability_state_v1 *state,
    const worr_net_capability_confirm_v1 *confirm)
{
    worr_net_capability_state_v1 updated;
    worr_net_capability_result_v1 result = WORR_NET_CAPABILITY_OK;

    if (!state || !confirm)
        return WORR_NET_CAPABILITY_INVALID_ARGUMENT;
    if (!Worr_NetCapabilityStateValidateV1(state))
        return WORR_NET_CAPABILITY_INVALID_STATE;
    if (state->phase == WORR_NET_CAPABILITY_CONFIRMED)
        return WORR_NET_CAPABILITY_ALREADY_CONFIRMED;
    if (state->phase != WORR_NET_CAPABILITY_OFFERED)
        return WORR_NET_CAPABILITY_INVALID_STATE;

    if (confirm->struct_size != sizeof(*confirm) ||
        confirm->reserved0 != 0) {
        result = WORR_NET_CAPABILITY_INVALID_ARGUMENT;
    } else if (confirm->schema_version != WORR_NET_CAPABILITY_VERSION) {
        result = WORR_NET_CAPABILITY_VERSION_MISMATCH;
    } else if (confirm->connection_epoch != state->connection_epoch) {
        result = WORR_NET_CAPABILITY_EPOCH_MISMATCH;
    } else if (!mask_valid(confirm->supported) ||
               !mask_valid(confirm->negotiated)) {
        result = WORR_NET_CAPABILITY_UNKNOWN_BITS;
    } else if ((confirm->negotiated & ~state->offered) != 0 ||
               (confirm->negotiated & ~state->supported) != 0 ||
               (confirm->negotiated & ~confirm->supported) != 0) {
        result = WORR_NET_CAPABILITY_UNOFFERED_BITS;
    } else if (confirm->negotiated !=
               (state->offered & state->supported & confirm->supported)) {
        /* Selection is deterministic; the peer cannot silently choose bits. */
        result = WORR_NET_CAPABILITY_UNOFFERED_BITS;
    }

    updated = *state;
    if (result == WORR_NET_CAPABILITY_OK) {
        updated.phase = WORR_NET_CAPABILITY_CONFIRMED;
        updated.peer_supported = confirm->supported;
        updated.negotiated = confirm->negotiated;
    } else {
        updated.phase = WORR_NET_CAPABILITY_FAILED;
        updated.peer_supported = 0;
        updated.negotiated = 0;
    }
    *state = updated;
    return result;
}
