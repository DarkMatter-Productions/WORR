/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_input_batch_sideband.h"

#include <string.h>

#define WNB_SIDEBAND_FNV_OFFSET UINT32_C(2166136261)
#define WNB_SIDEBAND_FNV_PRIME UINT32_C(16777619)
#define WNB_SIDEBAND_CHECKSUM_DOMAIN UINT32_C(0x574e4243) /* WNBC */
#define WNB_SIDEBAND_COMMIT_DOMAIN UINT32_C(0x574e4231) /* WNB1 */

static uint32_t append_u32(uint32_t hash, uint32_t value)
{
    unsigned index;
    for (index = 0; index < 4; ++index) {
        hash ^= value & UINT32_C(0xff);
        hash *= WNB_SIDEBAND_FNV_PRIME;
        value >>= 8;
    }
    return hash;
}

static uint32_t rotate_left(uint32_t value, unsigned shift)
{
    return (value << shift) | (value >> (32u - shift));
}

static uint32_t packed_schema(void)
{
    return ((uint32_t)WORR_NATIVE_INPUT_BATCH_RECORD_SCHEMA << 16) |
           WORR_NATIVE_INPUT_BATCH_WIRE_VERSION;
}

static uint32_t confirm_checksum(
    const worr_native_input_batch_confirm_v1 *confirm)
{
    uint32_t hash = WNB_SIDEBAND_FNV_OFFSET;
    hash = append_u32(hash, WNB_SIDEBAND_CHECKSUM_DOMAIN);
    hash = append_u32(hash, WORR_NATIVE_INPUT_BATCH_SIDEBAND_VERSION);
    hash = append_u32(hash, confirm->official_connection_epoch);
    hash = append_u32(hash, confirm->transport_epoch);
    hash = append_u32(hash,
                      ((uint32_t)confirm->record_schema_version << 16) |
                          confirm->wire_version);
    return append_u32(hash, confirm->maximum_commands);
}

static uint32_t confirm_commit(
    const worr_native_input_batch_confirm_v1 *confirm)
{
    return WNB_SIDEBAND_COMMIT_DOMAIN ^
           rotate_left(confirm->header_checksum, 7) ^
           rotate_left(confirm->official_connection_epoch, 13) ^
           rotate_left(confirm->transport_epoch, 21);
}

static int32_t signed_bits(uint32_t value)
{
    int32_t output;
    memcpy(&output, &value, sizeof(output));
    return output;
}

static uint32_t unsigned_bits(int32_t value)
{
    uint32_t output;
    memcpy(&output, &value, sizeof(output));
    return output;
}

static void parser_clear(
    worr_native_input_batch_sideband_parser_v1 *parser)
{
    memset(&parser->pending, 0, sizeof(parser->pending));
    parser->observed_checksum = 0;
    parser->reserved[0] = 0;
    parser->reserved[1] = 0;
}

static void parser_idle(
    worr_native_input_batch_sideband_parser_v1 *parser)
{
    parser_clear(parser);
    parser->phase = WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_IDLE;
}

static void parser_poison(
    worr_native_input_batch_sideband_parser_v1 *parser)
{
    parser_clear(parser);
    parser->phase = WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_POISONED;
}

static bool pending_zero(
    const worr_native_input_batch_confirm_v1 *confirm)
{
    static const worr_native_input_batch_confirm_v1 zero;
    return memcmp(confirm, &zero, sizeof(zero)) == 0;
}

bool Worr_NativeInputBatchSettingRecognizedV1(int32_t index)
{
    return index >= WORR_NATIVE_INPUT_BATCH_SETTING_BEGIN &&
           index <= WORR_NATIVE_INPUT_BATCH_SETTING_COMMIT;
}

bool Worr_NativeInputBatchConfirmInitV1(
    worr_native_input_batch_confirm_v1 *confirm_out,
    uint32_t official_connection_epoch, uint32_t transport_epoch)
{
    worr_native_input_batch_confirm_v1 output;
    if (!confirm_out || official_connection_epoch == 0 || transport_epoch == 0)
        return false;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_NATIVE_INPUT_BATCH_SIDEBAND_VERSION;
    output.record_schema_version = WORR_NATIVE_INPUT_BATCH_RECORD_SCHEMA;
    output.wire_version = WORR_NATIVE_INPUT_BATCH_WIRE_VERSION;
    output.maximum_commands = WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS;
    output.official_connection_epoch = official_connection_epoch;
    output.transport_epoch = transport_epoch;
    output.header_checksum = confirm_checksum(&output);
    *confirm_out = output;
    return true;
}

bool Worr_NativeInputBatchConfirmValidateV1(
    const worr_native_input_batch_confirm_v1 *confirm)
{
    return confirm && confirm->struct_size == sizeof(*confirm) &&
           confirm->schema_version ==
               WORR_NATIVE_INPUT_BATCH_SIDEBAND_VERSION &&
           confirm->record_schema_version ==
               WORR_NATIVE_INPUT_BATCH_RECORD_SCHEMA &&
           confirm->wire_version == WORR_NATIVE_INPUT_BATCH_WIRE_VERSION &&
           confirm->maximum_commands ==
               WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS &&
           confirm->official_connection_epoch != 0 &&
           confirm->transport_epoch != 0 &&
           confirm->header_checksum == confirm_checksum(confirm) &&
           confirm->reserved[0] == 0 && confirm->reserved[1] == 0;
}

bool Worr_NativeInputBatchConfirmEqualV1(
    const worr_native_input_batch_confirm_v1 *left,
    const worr_native_input_batch_confirm_v1 *right)
{
    return Worr_NativeInputBatchConfirmValidateV1(left) &&
           Worr_NativeInputBatchConfirmValidateV1(right) &&
           memcmp(left, right, sizeof(*left)) == 0;
}

bool Worr_NativeInputBatchConfirmEncodeV1(
    const worr_native_input_batch_confirm_v1 *confirm,
    worr_native_input_batch_setting_pair_v1 *pairs,
    uint32_t pair_capacity)
{
    worr_native_input_batch_setting_pair_v1 output[
        WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT];
    if (!Worr_NativeInputBatchConfirmValidateV1(confirm) || !pairs ||
        pair_capacity < WORR_NATIVE_INPUT_BATCH_SIDEBAND_PAIR_COUNT)
        return false;
    output[0] = (worr_native_input_batch_setting_pair_v1){
        WORR_NATIVE_INPUT_BATCH_SETTING_BEGIN,
        (int32_t)WORR_NATIVE_INPUT_BATCH_SIDEBAND_VERSION};
    output[1] = (worr_native_input_batch_setting_pair_v1){
        WORR_NATIVE_INPUT_BATCH_SETTING_OFFICIAL_EPOCH,
        signed_bits(confirm->official_connection_epoch)};
    output[2] = (worr_native_input_batch_setting_pair_v1){
        WORR_NATIVE_INPUT_BATCH_SETTING_TRANSPORT_EPOCH,
        signed_bits(confirm->transport_epoch)};
    output[3] = (worr_native_input_batch_setting_pair_v1){
        WORR_NATIVE_INPUT_BATCH_SETTING_SCHEMA,
        signed_bits(packed_schema())};
    output[4] = (worr_native_input_batch_setting_pair_v1){
        WORR_NATIVE_INPUT_BATCH_SETTING_LIMITS,
        (int32_t)confirm->maximum_commands};
    output[5] = (worr_native_input_batch_setting_pair_v1){
        WORR_NATIVE_INPUT_BATCH_SETTING_CHECKSUM,
        signed_bits(confirm->header_checksum)};
    output[6] = (worr_native_input_batch_setting_pair_v1){
        WORR_NATIVE_INPUT_BATCH_SETTING_COMMIT,
        signed_bits(confirm_commit(confirm))};
    memcpy(pairs, output, sizeof(output));
    return true;
}

bool Worr_NativeInputBatchSidebandParserInitV1(
    worr_native_input_batch_sideband_parser_v1 *parser_out)
{
    worr_native_input_batch_sideband_parser_v1 output;
    if (!parser_out)
        return false;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_NATIVE_INPUT_BATCH_SIDEBAND_VERSION;
    *parser_out = output;
    return true;
}

bool Worr_NativeInputBatchSidebandParserValidateV1(
    const worr_native_input_batch_sideband_parser_v1 *parser)
{
    if (!parser || parser->struct_size != sizeof(*parser) ||
        parser->schema_version !=
            WORR_NATIVE_INPUT_BATCH_SIDEBAND_VERSION ||
        parser->phase > WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_POISONED ||
        parser->packet_active > 1 || parser->reserved[0] != 0 ||
        parser->reserved[1] != 0)
        return false;
    if (!parser->packet_active)
        return parser->phase == WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_IDLE &&
               parser->observed_checksum == 0 && pending_zero(&parser->pending);
    if (parser->phase == WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_IDLE ||
        parser->phase == WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_POISONED)
        return parser->observed_checksum == 0 && pending_zero(&parser->pending);
    if (parser->pending.struct_size != sizeof(parser->pending) ||
        parser->pending.schema_version !=
            WORR_NATIVE_INPUT_BATCH_SIDEBAND_VERSION ||
        parser->pending.reserved[0] != 0 || parser->pending.reserved[1] != 0)
        return false;
    return true;
}

worr_native_input_batch_sideband_result_v1
Worr_NativeInputBatchSidebandPacketBeginV1(
    worr_native_input_batch_sideband_parser_v1 *parser)
{
    if (!parser)
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_NativeInputBatchSidebandParserValidateV1(parser))
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_STATE;
    if (parser->packet_active) {
        parser_idle(parser);
        parser->packet_active = 1;
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_BOUNDARY;
    }
    parser_idle(parser);
    parser->packet_active = 1;
    return WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_STARTED;
}

worr_native_input_batch_sideband_result_v1
Worr_NativeInputBatchSidebandPacketEndV1(
    worr_native_input_batch_sideband_parser_v1 *parser)
{
    bool clean;
    if (!parser)
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_NativeInputBatchSidebandParserValidateV1(parser) ||
        !parser->packet_active)
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_STATE;
    clean = parser->phase == WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_IDLE;
    parser_idle(parser);
    parser->packet_active = 0;
    return clean ? WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_ENDED
                 : WORR_NATIVE_INPUT_BATCH_SIDEBAND_PACKET_BOUNDARY;
}

static int32_t expected_index(uint16_t phase)
{
    switch (phase) {
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_OFFICIAL_EPOCH:
        return WORR_NATIVE_INPUT_BATCH_SETTING_OFFICIAL_EPOCH;
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_TRANSPORT_EPOCH:
        return WORR_NATIVE_INPUT_BATCH_SETTING_TRANSPORT_EPOCH;
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_SCHEMA:
        return WORR_NATIVE_INPUT_BATCH_SETTING_SCHEMA;
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_LIMITS:
        return WORR_NATIVE_INPUT_BATCH_SETTING_LIMITS;
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_CHECKSUM:
        return WORR_NATIVE_INPUT_BATCH_SETTING_CHECKSUM;
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_COMMIT:
        return WORR_NATIVE_INPUT_BATCH_SETTING_COMMIT;
    default:
        return 0;
    }
}

worr_native_input_batch_sideband_result_v1
Worr_NativeInputBatchSidebandObserveSettingV1(
    worr_native_input_batch_sideband_parser_v1 *parser,
    int32_t index, int32_t value)
{
    uint32_t word;
    if (!parser)
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_NativeInputBatchSidebandParserValidateV1(parser) ||
        !parser->packet_active)
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_STATE;
    if (!Worr_NativeInputBatchSettingRecognizedV1(index)) {
        if (parser->phase != WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_IDLE) {
            parser_poison(parser);
            return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INTERVENING_SERVICE;
        }
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_NOT_SIDEBAND;
    }
    if (parser->phase == WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_POISONED ||
        parser->phase == WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_READY) {
        parser_poison(parser);
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_UNEXPECTED_FIELD;
    }
    if (index == WORR_NATIVE_INPUT_BATCH_SETTING_BEGIN) {
        if (parser->phase != WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_IDLE) {
            parser_poison(parser);
            return WORR_NATIVE_INPUT_BATCH_SIDEBAND_UNEXPECTED_FIELD;
        }
        if (value != (int32_t)WORR_NATIVE_INPUT_BATCH_SIDEBAND_VERSION) {
            parser_poison(parser);
            return WORR_NATIVE_INPUT_BATCH_SIDEBAND_UNSUPPORTED;
        }
        parser_clear(parser);
        parser->pending.struct_size = sizeof(parser->pending);
        parser->pending.schema_version =
            WORR_NATIVE_INPUT_BATCH_SIDEBAND_VERSION;
        parser->phase =
            WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_OFFICIAL_EPOCH;
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_FIELD_ACCEPTED;
    }
    if (index != expected_index(parser->phase)) {
        parser_poison(parser);
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_UNEXPECTED_FIELD;
    }

    word = unsigned_bits(value);
    switch (parser->phase) {
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_OFFICIAL_EPOCH:
        parser->pending.official_connection_epoch = word;
        parser->phase =
            WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_TRANSPORT_EPOCH;
        break;
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_TRANSPORT_EPOCH:
        parser->pending.transport_epoch = word;
        parser->phase = WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_SCHEMA;
        break;
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_SCHEMA:
        if (word != packed_schema()) {
            parser_poison(parser);
            return WORR_NATIVE_INPUT_BATCH_SIDEBAND_UNSUPPORTED;
        }
        parser->pending.record_schema_version = (uint16_t)(word >> 16);
        parser->pending.wire_version = (uint16_t)word;
        parser->phase = WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_LIMITS;
        break;
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_LIMITS:
        if (word != WORR_NATIVE_INPUT_BATCH_MAX_COMMANDS) {
            parser_poison(parser);
            return WORR_NATIVE_INPUT_BATCH_SIDEBAND_UNSUPPORTED;
        }
        parser->pending.maximum_commands = (uint16_t)word;
        parser->phase = WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_CHECKSUM;
        break;
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_CHECKSUM:
        parser->observed_checksum = word;
        parser->pending.header_checksum = confirm_checksum(&parser->pending);
        if (word != parser->pending.header_checksum) {
            parser_poison(parser);
            return WORR_NATIVE_INPUT_BATCH_SIDEBAND_CHECKSUM_MISMATCH;
        }
        parser->phase = WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_COMMIT;
        break;
    case WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_COMMIT:
        if (!Worr_NativeInputBatchConfirmValidateV1(&parser->pending) ||
            word != confirm_commit(&parser->pending)) {
            parser_poison(parser);
            return WORR_NATIVE_INPUT_BATCH_SIDEBAND_COMMIT_MISMATCH;
        }
        parser->phase = WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_READY;
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_RECORD_COMMITTED;
    default:
        parser_poison(parser);
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_UNEXPECTED_FIELD;
    }
    return WORR_NATIVE_INPUT_BATCH_SIDEBAND_FIELD_ACCEPTED;
}

worr_native_input_batch_sideband_result_v1
Worr_NativeInputBatchSidebandObserveInterveningServiceV1(
    worr_native_input_batch_sideband_parser_v1 *parser)
{
    if (!parser)
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_NativeInputBatchSidebandParserValidateV1(parser) ||
        !parser->packet_active)
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_STATE;
    if (parser->phase == WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_IDLE)
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_NOT_SIDEBAND;
    parser_poison(parser);
    return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INTERVENING_SERVICE;
}

worr_native_input_batch_sideband_result_v1
Worr_NativeInputBatchSidebandTakeRecordV1(
    worr_native_input_batch_sideband_parser_v1 *parser,
    worr_native_input_batch_confirm_v1 *confirm_out)
{
    if (!parser || !confirm_out)
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_NativeInputBatchSidebandParserValidateV1(parser) ||
        !parser->packet_active ||
        parser->phase != WORR_NATIVE_INPUT_BATCH_SIDEBAND_PHASE_READY ||
        !Worr_NativeInputBatchConfirmValidateV1(&parser->pending))
        return WORR_NATIVE_INPUT_BATCH_SIDEBAND_INVALID_STATE;
    *confirm_out = parser->pending;
    parser_idle(parser);
    return WORR_NATIVE_INPUT_BATCH_SIDEBAND_RECORD_TAKEN;
}
