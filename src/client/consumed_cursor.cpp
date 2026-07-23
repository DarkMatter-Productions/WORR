/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"
#include "client/consumed_cursor.h"
#include "client/net_capability.h"

#include <cstring>

namespace {

worr_consumed_cursor_sideband_parser_v1 parser{};
bool initialized{};
bool canonical_established{};
worr_command_cursor_v1 last_canonical_cursor{};

bool capability_active()
{
    return CL_NetCapabilityHas(
        WORR_NET_CAP_CONSUMED_COMMAND_CURSOR_V1);
}

bool ensure_initialized()
{
    /* An initialized parser becoming invalid is an internal/protocol state
     * failure, not an invitation to silently discard an authenticated
     * partial tuple.  Only explicit lifecycle entry points may reset it. */
    if (initialized)
        return Worr_ConsumedCursorSidebandParserValidateV1(&parser);
    initialized =
        Worr_ConsumedCursorSidebandParserInitV1(&parser);
    return initialized;
}

bool observe_canonical_cursor(worr_command_cursor_v1 cursor)
{
    if (cursor.epoch == 0) {
        return cursor.contiguous_sequence == 0 &&
               !canonical_established;
    }
    if (!canonical_established) {
        worr_net_capability_state_v1 capability{};
        if (!CL_NetCapabilityGetState(&capability) ||
            capability.phase != WORR_NET_CAPABILITY_CONFIRMED ||
            cursor.epoch != capability.connection_epoch) {
            return false;
        }
        canonical_established = true;
        last_canonical_cursor = cursor;
        return true;
    }

    if (cursor.epoch == last_canonical_cursor.epoch) {
        if (cursor.contiguous_sequence <
            last_canonical_cursor.contiguous_sequence) {
            return false;
        }
    } else {
        if (last_canonical_cursor.epoch == UINT32_MAX ||
            cursor.epoch != last_canonical_cursor.epoch + 1u ||
            cursor.contiguous_sequence == 0) {
            return false;
        }
    }
    last_canonical_cursor = cursor;
    return true;
}

} // namespace

extern "C" void CL_ConsumedCursorReset(void)
{
    std::memset(&parser, 0, sizeof(parser));
    canonical_established = false;
    last_canonical_cursor = {};
    initialized =
        Worr_ConsumedCursorSidebandParserInitV1(&parser);
}

extern "C" void CL_ConsumedCursorShutdown(void)
{
    std::memset(&parser, 0, sizeof(parser));
    initialized = false;
    canonical_established = false;
    last_canonical_cursor = {};
}

extern "C" bool CL_ConsumedCursorPacketBegin(void)
{
    if (!ensure_initialized())
        return false;
    const auto result =
        Worr_ConsumedCursorSidebandPacketBeginV1(&parser);
    if (result == WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_STARTED)
        return true;

    /* Before negotiation a stale parser cannot authenticate anything and the
     * core has already reset it transactionally.  Once active, however, a
     * crossed packet boundary is a protocol violation. */
    return !capability_active() &&
           result ==
               WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY;
}

extern "C" bool CL_ConsumedCursorPacketEnd(void)
{
    if (!ensure_initialized())
        return false;
    const bool required = capability_active();
    const auto result =
        Worr_ConsumedCursorSidebandPacketEndV1(&parser);
    if (result == WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_ENDED)
        return true;
    return !required &&
           result ==
               WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY;
}

extern "C" bool CL_ConsumedCursorObserveSetting(int32_t index,
                                                   int32_t value)
{
    if (!capability_active())
        return true;
    if (!ensure_initialized())
        return false;
    const auto result = Worr_ConsumedCursorSidebandObserveSettingV1(
        &parser, index, value);
    return result == WORR_CONSUMED_CURSOR_SIDEBAND_NOT_SIDEBAND ||
           result == WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED ||
           result ==
               WORR_CONSUMED_CURSOR_SIDEBAND_HEADER_COMMITTED;
}

extern "C" bool CL_ConsumedCursorObserveInterveningService(void)
{
    if (!capability_active())
        return true;
    if (!ensure_initialized())
        return false;
    return Worr_ConsumedCursorSidebandObserveInterveningServiceV1(
               &parser) ==
           WORR_CONSUMED_CURSOR_SIDEBAND_NOT_SIDEBAND;
}

extern "C" bool CL_ConsumedCursorConsumeFrame(
    worr_snapshot_consumed_command_v2 *consumed_command_out)
{
    if (!consumed_command_out)
        return false;

    worr_snapshot_consumed_command_v2 output{};
    if (!capability_active()) {
        *consumed_command_out = output;
        return true;
    }
    if (!ensure_initialized())
        return false;

    worr_consumed_cursor_sideband_v1 sideband{};
    if (Worr_ConsumedCursorSidebandConsumeFrameV1(
            &parser, &sideband) !=
        WORR_CONSUMED_CURSOR_SIDEBAND_FRAME_MATCHED) {
        return false;
    }

    if (!observe_canonical_cursor(sideband.consumed_cursor))
        return false;

    if (sideband.consumed_cursor.epoch != 0) {
        output.cursor = sideband.consumed_cursor;
        output.provenance =
            WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    }
    *consumed_command_out = output;
    return true;
}

extern "C" bool CL_ConsumedCursorCanonicalEstablished(void)
{
    return capability_active() && canonical_established;
}

extern "C" bool CL_ConsumedCursorGetLatestV1(
    worr_command_cursor_v1 *cursor_out)
{
    if (!cursor_out || !capability_active() || !canonical_established)
        return false;
    *cursor_out = last_canonical_cursor;
    return true;
}

extern "C" bool CL_ConsumedCursorGetTelemetry(
    worr_consumed_cursor_sideband_telemetry_v1 *telemetry_out)
{
    if (!telemetry_out || !ensure_initialized())
        return false;
    *telemetry_out = parser.telemetry;
    return true;
}
