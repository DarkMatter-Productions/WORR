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

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_NET_CAPABILITY_VERSION 1u
#define WORR_NET_CAPABILITY_USERINFO_KEY "worr_caps"
/* Consecutive server-to-client q2proto settings sent only after the offer. */
#define WORR_NET_CAPABILITY_CONFIRM_EPOCH_SETTING (-31799)
#define WORR_NET_CAPABILITY_CONFIRM_SUPPORTED_SETTING (-31798)
#define WORR_NET_CAPABILITY_CONFIRM_NEGOTIATED_SETTING (-31797)

enum {
    WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1 = UINT32_C(1) << 0,
    WORR_NET_CAP_CONSUMED_COMMAND_CURSOR_V1 = UINT32_C(1) << 1,
    WORR_NET_CAP_CANONICAL_SNAPSHOT_V2 = UINT32_C(1) << 2,
    WORR_NET_CAP_TYPED_EVENT_RANGE_V2 = UINT32_C(1) << 3,
    /* Reserved until FR-10-T04's separate packet envelope is integrated. */
    WORR_NET_CAP_NATIVE_ENVELOPE_V1 = UINT32_C(1) << 4,
    /* Reserved until the retained event descriptor/DATA path is promoted. */
    WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 = UINT32_C(1) << 5,
    /*
     * Private readiness binding for the epoch-cancellation barrier.  This bit
     * is carried by CHALLENGE/CLIENT_READY records and is not part of the
     * public legacy-stage offer.
     */
    WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1 = UINT32_C(1) << 6,
};

#define WORR_NET_CAP_KNOWN_MASK                                      \
    ((uint32_t)(WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1 |           \
                WORR_NET_CAP_CONSUMED_COMMAND_CURSOR_V1 |           \
                WORR_NET_CAP_CANONICAL_SNAPSHOT_V2 |                \
                WORR_NET_CAP_TYPED_EVENT_RANGE_V2 |                 \
                WORR_NET_CAP_NATIVE_ENVELOPE_V1 |                   \
                WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |               \
                WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1))

/* Capabilities proven end-to-end in the current live legacy-shadow stage.
 * Expand this mask only when both peers have production consumers. */
#define WORR_NET_CAP_LEGACY_STAGE_MASK                               \
    ((uint32_t)(WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1 |           \
                WORR_NET_CAP_CONSUMED_COMMAND_CURSOR_V1))

/* Every private native readiness binding requires these two capabilities. */
#define WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK                  \
    ((uint32_t)(WORR_NET_CAP_NATIVE_ENVELOPE_V1 |                   \
                WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1))

/* Exact private bindings; neither mask is part of the public 0x03 offer. */
#define WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK                     \
    ((uint32_t)(WORR_NET_CAP_LEGACY_STAGE_MASK |                    \
                WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK))
#define WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK                       \
    ((uint32_t)(WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK |          \
                WORR_NET_CAP_NATIVE_EVENT_STREAM_V1))
/*
 * Private full-snapshot transaction binding.  This remains outside the
 * public legacy-stage offer until native snapshot DATA has a live endpoint
 * on both peers.
 */
#define WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK                    \
    ((uint32_t)(WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK |          \
                WORR_NET_CAP_CANONICAL_SNAPSHOT_V2))
#define WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PRIVATE_MASK              \
    ((uint32_t)(WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK |             \
                WORR_NET_CAP_CANONICAL_SNAPSHOT_V2))

typedef enum worr_net_capability_phase_v1_e {
    WORR_NET_CAPABILITY_RESET = 0,
    WORR_NET_CAPABILITY_OFFERED = 1,
    WORR_NET_CAPABILITY_CONFIRMED = 2,
    WORR_NET_CAPABILITY_FAILED = 3,
} worr_net_capability_phase_v1;

typedef enum worr_net_capability_result_v1_e {
    WORR_NET_CAPABILITY_OK = 0,
    WORR_NET_CAPABILITY_INVALID_ARGUMENT = 1,
    WORR_NET_CAPABILITY_INVALID_STATE = 2,
    WORR_NET_CAPABILITY_INVALID_TEXT = 3,
    WORR_NET_CAPABILITY_UNKNOWN_BITS = 4,
    WORR_NET_CAPABILITY_VERSION_MISMATCH = 5,
    WORR_NET_CAPABILITY_UNOFFERED_BITS = 6,
    WORR_NET_CAPABILITY_EPOCH_MISMATCH = 7,
    WORR_NET_CAPABILITY_ALREADY_CONFIRMED = 8,
} worr_net_capability_result_v1;

typedef struct worr_net_capability_confirm_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    uint32_t connection_epoch;
    uint32_t supported;
    uint32_t negotiated;
} worr_net_capability_confirm_v1;

typedef struct worr_net_capability_state_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t phase;
    uint32_t connection_epoch;
    uint32_t offered;
    uint32_t supported;
    uint32_t peer_supported;
    uint32_t negotiated;
    uint32_t reserved0;
} worr_net_capability_state_v1;

bool Worr_NetCapabilitiesFormatV1(uint32_t capabilities,
                                  char *text_out,
                                  size_t text_capacity);
worr_net_capability_result_v1 Worr_NetCapabilitiesParseV1(
    const char *text, uint32_t *capabilities_out);

bool Worr_NetCapabilityStateInitV1(
    worr_net_capability_state_v1 *state,
    uint32_t connection_epoch,
    uint32_t offered,
    uint32_t supported);
bool Worr_NetCapabilityStateValidateV1(
    const worr_net_capability_state_v1 *state);

/* Server-side selection: output is always the offered/supported intersection. */
worr_net_capability_result_v1 Worr_NetCapabilitySelectV1(
    uint32_t connection_epoch,
    uint32_t offered,
    uint32_t supported,
    worr_net_capability_confirm_v1 *confirm_out);

/* Client-side confirmation. Failure is sticky until a new connection state. */
worr_net_capability_result_v1 Worr_NetCapabilityConfirmV1(
    worr_net_capability_state_v1 *state,
    const worr_net_capability_confirm_v1 *confirm);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(worr_net_capability_confirm_v1) == 20,
              "network capability confirm layout changed");
static_assert(sizeof(worr_net_capability_state_v1) == 32,
              "network capability state layout changed");
#else
_Static_assert(sizeof(worr_net_capability_confirm_v1) == 20,
               "network capability confirm layout changed");
_Static_assert(sizeof(worr_net_capability_state_v1) == 32,
               "network capability state layout changed");
#endif
