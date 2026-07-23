/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"
#include "client/command_identity.h"
#include "client/net_capability.h"
#include "client/native_readiness_pilot.h"
#include "common/net/native_input_batch_sideband.h"

#include <cstring>

namespace {

enum confirm_phase_t : uint32_t {
    confirm_idle,
    confirm_supported,
    confirm_negotiated,
};

worr_net_capability_state_v1 capability_state{};
worr_net_capability_confirm_v1 pending_confirm{};
confirm_phase_t confirm_phase = confirm_idle;
bool packet_active{};
cvar_t *capability_offer_cvar{};
cvar_t *input_batch_offer_cvar{};
uint32_t capability_offer = WORR_NET_CAP_LEGACY_STAGE_MASK;

bool refresh_offer()
{
    char value[11];
    const uint32_t requested =
        CL_NativeReadinessPilotRequestedPublicCapabilities();
    if (!Worr_NetCapabilitiesFormatV1(requested, value, sizeof(value)))
        return false;
    capability_offer = requested;
    if (capability_offer_cvar)
        Cvar_SetByVar(capability_offer_cvar, value, FROM_CODE);
    if (input_batch_offer_cvar) {
        Cvar_SetByVar(input_batch_offer_cvar,
                      CL_NativeReadinessPilotRequestedInputBatchV1()
                          ? "1" : "0",
                      FROM_CODE);
    }
    return true;
}

void fail_confirmation()
{
    if (Worr_NetCapabilityStateValidateV1(&capability_state) &&
        (capability_state.phase == WORR_NET_CAPABILITY_OFFERED ||
         capability_state.phase == WORR_NET_CAPABILITY_CONFIRMED)) {
        capability_state.phase = WORR_NET_CAPABILITY_FAILED;
    }
    capability_state.peer_supported = 0;
    capability_state.negotiated = 0;
    pending_confirm = {};
    confirm_phase = confirm_idle;
}

} // namespace

extern "C" void CL_NetCapabilityRegisterOffer(void)
{
    char value[11];
    const uint32_t requested =
        CL_NativeReadinessPilotRequestedPublicCapabilities();
    if (!Worr_NetCapabilitiesFormatV1(requested, value, sizeof(value))) {
        Com_Error(ERR_FATAL, "failed to format WORR capability offer");
    }
    capability_offer_cvar = Cvar_Get(
        WORR_NET_CAPABILITY_USERINFO_KEY, value,
        CVAR_USERINFO | CVAR_ROM);
    capability_offer = requested;
    /* Override a command-line/user-created value: this userinfo field is a
     * derived connection contract, not a user-editable bitmask. */
    Cvar_SetByVar(capability_offer_cvar, value, FROM_CODE);
    input_batch_offer_cvar = Cvar_Get(
        WORR_NATIVE_INPUT_BATCH_USERINFO_KEY,
        CL_NativeReadinessPilotRequestedInputBatchV1() ? "1" : "0",
        CVAR_USERINFO | CVAR_ROM);
    Cvar_SetByVar(input_batch_offer_cvar,
                  CL_NativeReadinessPilotRequestedInputBatchV1()
                      ? "1" : "0",
                  FROM_CODE);
}

extern "C" void CL_NetCapabilityRefreshOffer(void)
{
    if (!refresh_offer())
        Com_Error(ERR_FATAL, "failed to refresh WORR capability offer");
}

extern "C" void CL_NetCapabilityReset(uint32_t connection_epoch)
{
    std::memset(&capability_state, 0, sizeof(capability_state));
    pending_confirm = {};
    confirm_phase = confirm_idle;
    packet_active = false;
    /* The server supplies the authoritative nonzero session epoch in the
     * confirmation tuple.  This provisional value only keeps the offered
     * state structurally valid until that first field arrives. */
    const uint32_t provisional_epoch =
        connection_epoch != 0 ? connection_epoch : 1u;
    if (!Worr_NetCapabilityStateInitV1(
            &capability_state, provisional_epoch,
            capability_offer, capability_offer)) {
        capability_state.struct_size = sizeof(capability_state);
        capability_state.schema_version = WORR_NET_CAPABILITY_VERSION;
        capability_state.phase = WORR_NET_CAPABILITY_FAILED;
    }
}

extern "C" void CL_NetCapabilityShutdown(void)
{
    std::memset(&capability_state, 0, sizeof(capability_state));
    pending_confirm = {};
    confirm_phase = confirm_idle;
    packet_active = false;
}

extern "C" bool CL_NetCapabilityPacketBegin(void)
{
    if (packet_active || confirm_phase != confirm_idle) {
        fail_confirmation();
        packet_active = false;
        return false;
    }
    if (Worr_NetCapabilityStateValidateV1(&capability_state) &&
        capability_state.phase == WORR_NET_CAPABILITY_FAILED) {
        return false;
    }
    packet_active = true;
    return true;
}

extern "C" bool CL_NetCapabilityPacketEnd(void)
{
    if (!packet_active)
        return false;
    packet_active = false;
    if (confirm_phase != confirm_idle) {
        fail_confirmation();
        return false;
    }
    return !Worr_NetCapabilityStateValidateV1(&capability_state) ||
           capability_state.phase != WORR_NET_CAPABILITY_FAILED;
}

extern "C" bool CL_NetCapabilityObserveSetting(int32_t index,
    int32_t value)
{
    if (index == WORR_NET_CAPABILITY_CONFIRM_EPOCH_SETTING) {
        worr_net_capability_state_v1 offered{};
        const uint32_t session_epoch = static_cast<uint32_t>(value);
        if (confirm_phase != confirm_idle ||
            capability_state.phase != WORR_NET_CAPABILITY_OFFERED ||
            !Worr_NetCapabilityStateInitV1(
                &offered, session_epoch,
                capability_offer, capability_offer)) {
            fail_confirmation();
            return true;
        }
        capability_state = offered;
        pending_confirm = {};
        pending_confirm.struct_size = sizeof(pending_confirm);
        pending_confirm.schema_version = WORR_NET_CAPABILITY_VERSION;
        pending_confirm.connection_epoch = session_epoch;
        confirm_phase = confirm_supported;
        return true;
    }
    if (index == WORR_NET_CAPABILITY_CONFIRM_SUPPORTED_SETTING) {
        if (confirm_phase != confirm_supported) {
            fail_confirmation();
            return true;
        }
        pending_confirm.supported = static_cast<uint32_t>(value);
        confirm_phase = confirm_negotiated;
        return true;
    }
    if (index == WORR_NET_CAPABILITY_CONFIRM_NEGOTIATED_SETTING) {
        if (confirm_phase != confirm_negotiated) {
            fail_confirmation();
            return true;
        }
        pending_confirm.negotiated = static_cast<uint32_t>(value);
        const worr_net_capability_result_v1 result =
            Worr_NetCapabilityConfirmV1(
                &capability_state, &pending_confirm);
        if (result == WORR_NET_CAPABILITY_OK) {
            CL_CommandIdentityReset(capability_state.connection_epoch);
            CL_NativeReadinessPilotCapabilityConfirmed(&capability_state);
        } else {
            CL_CommandIdentityShutdown();
            /* Retire a staged native endpoint before any readiness carrier
             * can follow a malformed or contradictory public tuple. */
            CL_NativeReadinessPilotCapabilityConfirmed(&capability_state);
        }
        pending_confirm = {};
        confirm_phase = confirm_idle;
        return true;
    }
    /* Confirmation fields are an adjacent tuple.  An ordinary setting is
     * still a service boundary and invalidates any partial tuple. */
    if (confirm_phase != confirm_idle)
        fail_confirmation();
    return false;
}

extern "C" void CL_NetCapabilityObserveInterveningService(void)
{
    if (confirm_phase != confirm_idle)
        fail_confirmation();
}

extern "C" uint32_t CL_NetCapabilityNegotiated(void)
{
    return Worr_NetCapabilityStateValidateV1(&capability_state) &&
                   capability_state.phase == WORR_NET_CAPABILITY_CONFIRMED
               ? capability_state.negotiated
               : 0;
}

extern "C" uint32_t CL_NetCapabilityOffered(void)
{
    return capability_offer;
}

extern "C" bool CL_NetCapabilityHas(uint32_t capability)
{
    return capability != 0 && (capability & ~WORR_NET_CAP_KNOWN_MASK) == 0 &&
           (CL_NetCapabilityNegotiated() & capability) == capability;
}

extern "C" bool CL_NetCapabilityGetState(
    worr_net_capability_state_v1 *state_out)
{
    if (!state_out || !Worr_NetCapabilityStateValidateV1(&capability_state))
        return false;
    *state_out = capability_state;
    return true;
}
