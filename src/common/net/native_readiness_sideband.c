/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_readiness_sideband.h"

#include <limits.h>
#include <string.h>

static void saturating_increment(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static bool ranges_disjoint(const void *left, size_t left_bytes,
                            const void *right, size_t right_bytes)
{
    const uintptr_t left_begin = (uintptr_t)left;
    const uintptr_t right_begin = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left == NULL || right == NULL)
        return false;
    if (left_bytes > UINTPTR_MAX - left_begin ||
        right_bytes > UINTPTR_MAX - right_begin) {
        return false;
    }
    left_end = left_begin + left_bytes;
    right_end = right_begin + right_bytes;
    return left_end <= right_begin || right_end <= left_begin;
}

static uint16_t unsigned_word(int16_t value)
{
    uint16_t output;
    memcpy(&output, &value, sizeof(output));
    return output;
}

static int16_t signed_word(uint16_t value)
{
    int16_t output;
    memcpy(&output, &value, sizeof(output));
    return output;
}

static bool record_kind_valid(uint16_t kind)
{
    return kind >= WORR_NATIVE_READINESS_RECORD_CHALLENGE &&
           kind <=
               WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM;
}

static bool capabilities_valid(uint32_t capabilities)
{
    return (capabilities & ~WORR_NET_CAP_KNOWN_MASK) == 0 &&
           (capabilities & WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK) ==
               WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK;
}

static bool snapshot_epoch_binding_valid(uint32_t capabilities,
                                         uint32_t snapshot_epoch)
{
    return ((capabilities & WORR_NET_CAP_CANONICAL_SNAPSHOT_V2) != 0) ==
           (snapshot_epoch != 0);
}

static bool record_fields_valid(
    const worr_native_readiness_record_v1 *record)
{
    worr_native_readiness_record_v1 expected;
    return Worr_NativeReadinessRecordInitBoundV1(
        &expected, record->record_kind, record->transport_epoch,
        record->snapshot_epoch,
        record->negotiated_capabilities, record->readiness_nonce);
}

static uint16_t crc16_byte(uint16_t crc, uint8_t byte)
{
    unsigned bit;

    crc ^= (uint16_t)byte << 8;
    for (bit = 0; bit < 8; ++bit) {
        crc = (uint16_t)((crc << 1) ^
                         ((crc & UINT16_C(0x8000)) != 0
                              ? UINT16_C(0x1021)
                              : UINT16_C(0)));
    }
    return crc;
}

static uint16_t crc16_u16_le(uint16_t crc, uint16_t value)
{
    crc = crc16_byte(crc, (uint8_t)value);
    return crc16_byte(crc, (uint8_t)(value >> 8));
}

static uint16_t sideband_commit(
    const worr_native_readiness_setting_pair_v1 *prefix)
{
    uint16_t crc = UINT16_C(0xffff);
    uint32_t index;

    crc = crc16_byte(crc, (uint8_t)'W');
    crc = crc16_byte(crc, (uint8_t)'R');
    crc = crc16_byte(crc, (uint8_t)'S');
    crc = crc16_byte(crc, (uint8_t)'1');
    for (index = 0; index + 1 < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        crc = crc16_u16_le(crc, unsigned_word(prefix[index].index));
        crc = crc16_u16_le(crc, unsigned_word(prefix[index].value));
    }
    return crc;
}

static void build_prefix(
    const worr_native_readiness_record_v1 *record,
    worr_native_readiness_setting_pair_v1 *pairs)
{
    pairs[0] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_BEGIN,
        (int16_t)WORR_NATIVE_READINESS_SIDEBAND_VERSION};
    pairs[1] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_KIND,
        (int16_t)record->record_kind};
    pairs[2] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_EPOCH_LOW,
        signed_word((uint16_t)record->transport_epoch)};
    pairs[3] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_EPOCH_HIGH,
        signed_word((uint16_t)(record->transport_epoch >> 16))};
    pairs[4] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_LOW,
        signed_word((uint16_t)record->snapshot_epoch)};
    pairs[5] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_HIGH,
        signed_word((uint16_t)(record->snapshot_epoch >> 16))};
    pairs[6] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_CAPABILITIES_LOW,
        signed_word((uint16_t)record->negotiated_capabilities)};
    pairs[7] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_CAPABILITIES_HIGH,
        signed_word((uint16_t)(record->negotiated_capabilities >> 16))};
    pairs[8] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_NONCE_WORD0,
        signed_word((uint16_t)record->readiness_nonce)};
    pairs[9] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_NONCE_WORD1,
        signed_word((uint16_t)(record->readiness_nonce >> 16))};
    pairs[10] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_NONCE_WORD2,
        signed_word((uint16_t)(record->readiness_nonce >> 32))};
    pairs[11] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_NONCE_WORD3,
        signed_word((uint16_t)(record->readiness_nonce >> 48))};
    pairs[12] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_CHECKSUM_LOW,
        signed_word((uint16_t)record->record_checksum)};
    pairs[13] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_CHECKSUM_HIGH,
        signed_word((uint16_t)(record->record_checksum >> 16))};
}

bool Worr_NativeReadinessSidebandEncodeV1(
    const worr_native_readiness_record_v1 *record,
    worr_native_readiness_setting_pair_v1 *pairs_out,
    uint32_t pair_capacity)
{
    worr_native_readiness_setting_pair_v1 output[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];

    if (!Worr_NativeReadinessRecordValidateV1(record) || pairs_out == NULL ||
        pair_capacity < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT ||
        !ranges_disjoint(record, sizeof(*record), pairs_out, sizeof(output))) {
        return false;
    }
    build_prefix(record, output);
    output[14] = (worr_native_readiness_setting_pair_v1){
        WORR_NATIVE_READINESS_SETTING_COMMIT,
        signed_word(sideband_commit(output))};
    memcpy(pairs_out, output, sizeof(output));
    return true;
}

static bool record_is_zero(
    const worr_native_readiness_record_v1 *record)
{
    return record->struct_size == 0 && record->schema_version == 0 &&
           record->record_kind == 0 && record->transport_epoch == 0 &&
           record->negotiated_capabilities == 0 &&
           record->readiness_nonce == 0 && record->record_checksum == 0 &&
           record->snapshot_epoch == 0;
}

static bool pending_header_valid(
    const worr_native_readiness_record_v1 *record)
{
    return record->struct_size == sizeof(*record) &&
           record->schema_version == WORR_NATIVE_READINESS_ABI_VERSION;
}

static void parser_clear_pending(
    worr_native_readiness_sideband_parser_v1 *parser)
{
    memset(&parser->pending_record, 0, sizeof(parser->pending_record));
    parser->checksum_low = 0;
    parser->reserved2 = 0;
    parser->reserved3 = 0;
}

static void parser_idle(worr_native_readiness_sideband_parser_v1 *parser)
{
    parser_clear_pending(parser);
    parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_IDLE;
}

static void parser_poison(worr_native_readiness_sideband_parser_v1 *parser)
{
    parser_clear_pending(parser);
    parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED;
}

static bool phase_has_dangling_sequence(uint16_t phase)
{
    return phase >= WORR_NATIVE_READINESS_SIDEBAND_PHASE_KIND &&
           phase <= WORR_NATIVE_READINESS_SIDEBAND_PHASE_RECORD;
}

static bool sideband_index_recognized(int32_t index)
{
    return index >= WORR_NATIVE_READINESS_SETTING_BEGIN &&
           index <= WORR_NATIVE_READINESS_SETTING_COMMIT;
}

static int16_t expected_index(uint16_t phase)
{
    switch (phase) {
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_KIND:
        return WORR_NATIVE_READINESS_SETTING_KIND;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_EPOCH_LOW:
        return WORR_NATIVE_READINESS_SETTING_EPOCH_LOW;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_EPOCH_HIGH:
        return WORR_NATIVE_READINESS_SETTING_EPOCH_HIGH;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_SNAPSHOT_EPOCH_LOW:
        return WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_LOW;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_SNAPSHOT_EPOCH_HIGH:
        return WORR_NATIVE_READINESS_SETTING_SNAPSHOT_EPOCH_HIGH;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CAPABILITIES_LOW:
        return WORR_NATIVE_READINESS_SETTING_CAPABILITIES_LOW;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CAPABILITIES_HIGH:
        return WORR_NATIVE_READINESS_SETTING_CAPABILITIES_HIGH;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD0:
        return WORR_NATIVE_READINESS_SETTING_NONCE_WORD0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD1:
        return WORR_NATIVE_READINESS_SETTING_NONCE_WORD1;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD2:
        return WORR_NATIVE_READINESS_SETTING_NONCE_WORD2;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD3:
        return WORR_NATIVE_READINESS_SETTING_NONCE_WORD3;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CHECKSUM_LOW:
        return WORR_NATIVE_READINESS_SETTING_CHECKSUM_LOW;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CHECKSUM_HIGH:
        return WORR_NATIVE_READINESS_SETTING_CHECKSUM_HIGH;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_COMMIT:
        return WORR_NATIVE_READINESS_SETTING_COMMIT;
    default:
        return 0;
    }
}

static bool pending_prefix_valid(
    const worr_native_readiness_sideband_parser_v1 *parser)
{
    const worr_native_readiness_record_v1 *record = &parser->pending_record;

    if (!pending_header_valid(record))
        return false;
    switch (parser->phase) {
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_KIND:
        return record->record_kind == 0 && record->transport_epoch == 0 &&
               record->snapshot_epoch == 0 &&
               record->negotiated_capabilities == 0 &&
               record->readiness_nonce == 0 &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_EPOCH_LOW:
        return record_kind_valid(record->record_kind) &&
               record->transport_epoch == 0 &&
               record->snapshot_epoch == 0 &&
               record->negotiated_capabilities == 0 &&
               record->readiness_nonce == 0 &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_EPOCH_HIGH:
        return record_kind_valid(record->record_kind) &&
               record->transport_epoch <= UINT16_MAX &&
               record->snapshot_epoch == 0 &&
               record->negotiated_capabilities == 0 &&
               record->readiness_nonce == 0 &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_SNAPSHOT_EPOCH_LOW:
        return record_kind_valid(record->record_kind) &&
               record->transport_epoch != 0 &&
               record->snapshot_epoch == 0 &&
               record->negotiated_capabilities == 0 &&
               record->readiness_nonce == 0 &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_SNAPSHOT_EPOCH_HIGH:
        return record_kind_valid(record->record_kind) &&
               record->transport_epoch != 0 &&
               record->snapshot_epoch <= UINT16_MAX &&
               record->negotiated_capabilities == 0 &&
               record->readiness_nonce == 0 &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CAPABILITIES_LOW:
        return record_kind_valid(record->record_kind) &&
               record->transport_epoch != 0 &&
               record->negotiated_capabilities == 0 &&
               record->readiness_nonce == 0 &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CAPABILITIES_HIGH:
        return record_kind_valid(record->record_kind) &&
               record->transport_epoch != 0 &&
               record->negotiated_capabilities <= UINT16_MAX &&
               record->readiness_nonce == 0 &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD0:
        return record_kind_valid(record->record_kind) &&
               record->transport_epoch != 0 &&
               capabilities_valid(record->negotiated_capabilities) &&
               snapshot_epoch_binding_valid(
                   record->negotiated_capabilities,
                   record->snapshot_epoch) &&
               record->readiness_nonce == 0 &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD1:
        return record_kind_valid(record->record_kind) &&
               record->transport_epoch != 0 &&
               capabilities_valid(record->negotiated_capabilities) &&
               snapshot_epoch_binding_valid(
                   record->negotiated_capabilities,
                   record->snapshot_epoch) &&
               record->readiness_nonce <= UINT16_MAX &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD2:
        return record_kind_valid(record->record_kind) &&
               record->transport_epoch != 0 &&
               capabilities_valid(record->negotiated_capabilities) &&
               snapshot_epoch_binding_valid(
                   record->negotiated_capabilities,
                   record->snapshot_epoch) &&
               record->readiness_nonce <= UINT32_MAX &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD3:
        return record_kind_valid(record->record_kind) &&
               record->transport_epoch != 0 &&
               capabilities_valid(record->negotiated_capabilities) &&
               snapshot_epoch_binding_valid(
                   record->negotiated_capabilities,
                   record->snapshot_epoch) &&
               record->readiness_nonce <= UINT64_C(0x0000ffffffffffff) &&
               record->record_checksum == 0 && parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CHECKSUM_LOW:
        return record_fields_valid(record) && record->record_checksum == 0 &&
               parser->checksum_low == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CHECKSUM_HIGH:
        return record_fields_valid(record) && record->record_checksum == 0;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_COMMIT:
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_RECORD:
        return Worr_NativeReadinessRecordValidateV1(record) &&
               parser->checksum_low ==
                   (uint16_t)record->record_checksum;
    default:
        return false;
    }
}

bool Worr_NativeReadinessSidebandParserInitV1(
    worr_native_readiness_sideband_parser_v1 *parser_out)
{
    worr_native_readiness_sideband_parser_v1 output;

    if (parser_out == NULL)
        return false;
    memset(&output, 0, sizeof(output));
    output.struct_size = (uint32_t)sizeof(output);
    output.schema_version = WORR_NATIVE_READINESS_SIDEBAND_VERSION;
    *parser_out = output;
    return true;
}

bool Worr_NativeReadinessSidebandParserValidateV1(
    const worr_native_readiness_sideband_parser_v1 *parser)
{
    if (parser == NULL || parser->struct_size != sizeof(*parser) ||
        parser->schema_version != WORR_NATIVE_READINESS_SIDEBAND_VERSION ||
        parser->phase > WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED ||
        parser->packet_active > 1 || parser->reserved0 != 0 ||
        parser->reserved1 != 0 || parser->reserved2 != 0 ||
        parser->reserved3 != 0) {
        return false;
    }
    if (parser->packet_active == 0) {
        return parser->phase ==
                   WORR_NATIVE_READINESS_SIDEBAND_PHASE_IDLE &&
               record_is_zero(&parser->pending_record) &&
               parser->checksum_low == 0;
    }
    if (parser->phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_IDLE ||
        parser->phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED) {
        return record_is_zero(&parser->pending_record) &&
               parser->checksum_low == 0;
    }
    return pending_prefix_valid(parser);
}

static void account_discard(
    worr_native_readiness_sideband_parser_v1 *parser)
{
    if (parser->phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_RECORD)
        saturating_increment(&parser->telemetry.discarded_records);
    else if (phase_has_dangling_sequence(parser->phase))
        saturating_increment(&parser->telemetry.dangling_sequences);
}

worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandPacketBeginV1(
    worr_native_readiness_sideband_parser_v1 *parser)
{
    bool reset;

    if (parser == NULL)
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_NativeReadinessSidebandParserValidateV1(parser))
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_STATE;
    reset = parser->packet_active != 0;
    if (reset) {
        account_discard(parser);
        saturating_increment(&parser->telemetry.packet_boundary_resets);
    }
    parser_idle(parser);
    parser->packet_active = 1;
    saturating_increment(&parser->telemetry.packet_begins);
    return reset ? WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY
                 : WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED;
}

worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandPacketEndV1(
    worr_native_readiness_sideband_parser_v1 *parser)
{
    bool reset;

    if (parser == NULL)
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_NativeReadinessSidebandParserValidateV1(parser))
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_STATE;
    if (parser->packet_active == 0) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_STATE;
    }
    reset = parser->phase != WORR_NATIVE_READINESS_SIDEBAND_PHASE_IDLE;
    if (reset) {
        account_discard(parser);
        saturating_increment(&parser->telemetry.packet_boundary_resets);
    }
    parser_idle(parser);
    parser->packet_active = 0;
    saturating_increment(&parser->telemetry.packet_ends);
    return reset ? WORR_NATIVE_READINESS_SIDEBAND_RESET_PACKET_BOUNDARY
                 : WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED;
}

static worr_native_readiness_sideband_result_v1 intervening_reset(
    worr_native_readiness_sideband_parser_v1 *parser)
{
    if (parser->phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_IDLE)
        return WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND;
    account_discard(parser);
    parser_idle(parser);
    saturating_increment(&parser->telemetry.intervening_resets);
    return WORR_NATIVE_READINESS_SIDEBAND_RESET_INTERVENING_SERVICE;
}

static worr_native_readiness_sideband_result_v1 observe_non_sideband(
    worr_native_readiness_sideband_parser_v1 *parser)
{
    saturating_increment(&parser->telemetry.settings_seen);
    saturating_increment(&parser->telemetry.non_sideband_settings);
    return intervening_reset(parser);
}

static void poison_record(
    worr_native_readiness_sideband_parser_v1 *parser)
{
    if (parser->phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_RECORD)
        saturating_increment(&parser->telemetry.discarded_records);
    parser_poison(parser);
}

static worr_native_readiness_sideband_result_v1 observe_pair_validated(
    worr_native_readiness_sideband_parser_v1 *parser,
    int16_t index,
    int16_t value)
{
    worr_native_readiness_record_v1 expected;
    worr_native_readiness_setting_pair_v1 prefix[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    uint16_t word;
    uint32_t checksum;

    saturating_increment(&parser->telemetry.settings_seen);
    if (!sideband_index_recognized(index)) {
        saturating_increment(&parser->telemetry.non_sideband_settings);
        return intervening_reset(parser);
    }
    if (parser->phase == WORR_NATIVE_READINESS_SIDEBAND_PHASE_POISONED) {
        saturating_increment(&parser->telemetry.malformed_order);
        return WORR_NATIVE_READINESS_SIDEBAND_UNEXPECTED_FIELD;
    }
    if (index == WORR_NATIVE_READINESS_SETTING_BEGIN) {
        if (parser->phase != WORR_NATIVE_READINESS_SIDEBAND_PHASE_IDLE) {
            poison_record(parser);
            saturating_increment(&parser->telemetry.malformed_order);
            return WORR_NATIVE_READINESS_SIDEBAND_UNEXPECTED_FIELD;
        }
        saturating_increment(&parser->telemetry.sideband_begins);
        if (value !=
            (int16_t)WORR_NATIVE_READINESS_SIDEBAND_VERSION) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.unsupported_versions);
            return WORR_NATIVE_READINESS_SIDEBAND_UNSUPPORTED_VERSION;
        }
        parser->pending_record.struct_size =
            (uint32_t)sizeof(parser->pending_record);
        parser->pending_record.schema_version =
            WORR_NATIVE_READINESS_ABI_VERSION;
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_KIND;
        saturating_increment(&parser->telemetry.fields_accepted);
        return WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED;
    }
    if (index != expected_index(parser->phase)) {
        poison_record(parser);
        saturating_increment(&parser->telemetry.malformed_order);
        return WORR_NATIVE_READINESS_SIDEBAND_UNEXPECTED_FIELD;
    }

    word = unsigned_word(value);
    switch (parser->phase) {
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_KIND:
        if (!record_kind_valid(word)) {
            parser_poison(parser);
            saturating_increment(
                &parser->telemetry.record_validation_failures);
            return WORR_NATIVE_READINESS_SIDEBAND_RECORD_INVALID;
        }
        parser->pending_record.record_kind = word;
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_EPOCH_LOW;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_EPOCH_LOW:
        parser->pending_record.transport_epoch = word;
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_EPOCH_HIGH;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_EPOCH_HIGH:
        parser->pending_record.transport_epoch |= (uint32_t)word << 16;
        if (parser->pending_record.transport_epoch == 0) {
            parser_poison(parser);
            saturating_increment(
                &parser->telemetry.record_validation_failures);
            return WORR_NATIVE_READINESS_SIDEBAND_RECORD_INVALID;
        }
        parser->phase =
            WORR_NATIVE_READINESS_SIDEBAND_PHASE_SNAPSHOT_EPOCH_LOW;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_SNAPSHOT_EPOCH_LOW:
        parser->pending_record.snapshot_epoch = word;
        parser->phase =
            WORR_NATIVE_READINESS_SIDEBAND_PHASE_SNAPSHOT_EPOCH_HIGH;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_SNAPSHOT_EPOCH_HIGH:
        parser->pending_record.snapshot_epoch |= (uint32_t)word << 16;
        parser->phase =
            WORR_NATIVE_READINESS_SIDEBAND_PHASE_CAPABILITIES_LOW;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CAPABILITIES_LOW:
        parser->pending_record.negotiated_capabilities = word;
        parser->phase =
            WORR_NATIVE_READINESS_SIDEBAND_PHASE_CAPABILITIES_HIGH;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CAPABILITIES_HIGH:
        parser->pending_record.negotiated_capabilities |=
            (uint32_t)word << 16;
        if (!capabilities_valid(
                parser->pending_record.negotiated_capabilities) ||
            !snapshot_epoch_binding_valid(
                parser->pending_record.negotiated_capabilities,
                parser->pending_record.snapshot_epoch)) {
            parser_poison(parser);
            saturating_increment(
                &parser->telemetry.record_validation_failures);
            return WORR_NATIVE_READINESS_SIDEBAND_RECORD_INVALID;
        }
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD0;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD0:
        parser->pending_record.readiness_nonce = word;
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD1;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD1:
        parser->pending_record.readiness_nonce |= (uint64_t)word << 16;
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD2;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD2:
        parser->pending_record.readiness_nonce |= (uint64_t)word << 32;
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD3;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_NONCE_WORD3:
        parser->pending_record.readiness_nonce |= (uint64_t)word << 48;
        if (parser->pending_record.readiness_nonce == 0) {
            parser_poison(parser);
            saturating_increment(
                &parser->telemetry.record_validation_failures);
            return WORR_NATIVE_READINESS_SIDEBAND_RECORD_INVALID;
        }
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_CHECKSUM_LOW;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CHECKSUM_LOW:
        parser->checksum_low = word;
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_CHECKSUM_HIGH;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_CHECKSUM_HIGH:
        checksum = (uint32_t)word << 16 | parser->checksum_low;
        if (!Worr_NativeReadinessRecordInitBoundV1(
                &expected, parser->pending_record.record_kind,
                parser->pending_record.transport_epoch,
                parser->pending_record.snapshot_epoch,
                parser->pending_record.negotiated_capabilities,
                parser->pending_record.readiness_nonce)) {
            parser_poison(parser);
            saturating_increment(
                &parser->telemetry.record_validation_failures);
            return WORR_NATIVE_READINESS_SIDEBAND_RECORD_INVALID;
        }
        if (checksum != expected.record_checksum) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.checksum_failures);
            return WORR_NATIVE_READINESS_SIDEBAND_CHECKSUM_MISMATCH;
        }
        parser->pending_record = expected;
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_COMMIT;
        break;
    case WORR_NATIVE_READINESS_SIDEBAND_PHASE_COMMIT:
        build_prefix(&parser->pending_record, prefix);
        if (word != sideband_commit(prefix) ||
            !Worr_NativeReadinessRecordValidateV1(
                &parser->pending_record)) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.commit_failures);
            return WORR_NATIVE_READINESS_SIDEBAND_COMMIT_MISMATCH;
        }
        parser->phase = WORR_NATIVE_READINESS_SIDEBAND_PHASE_RECORD;
        saturating_increment(&parser->telemetry.fields_accepted);
        saturating_increment(&parser->telemetry.records_committed);
        return WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED;
    default:
        poison_record(parser);
        saturating_increment(&parser->telemetry.malformed_order);
        return WORR_NATIVE_READINESS_SIDEBAND_UNEXPECTED_FIELD;
    }
    saturating_increment(&parser->telemetry.fields_accepted);
    return WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED;
}

static worr_native_readiness_sideband_result_v1 parser_ready_for_setting(
    worr_native_readiness_sideband_parser_v1 *parser)
{
    if (parser == NULL)
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_NativeReadinessSidebandParserValidateV1(parser))
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_STATE;
    if (parser->packet_active == 0) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_STATE;
    }
    return WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED;
}

worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandObservePairV1(
    worr_native_readiness_sideband_parser_v1 *parser,
    int16_t index,
    int16_t value)
{
    const worr_native_readiness_sideband_result_v1 ready =
        parser_ready_for_setting(parser);

    if (ready != WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED)
        return ready;
    return observe_pair_validated(parser, index, value);
}

worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandObserveSvcSettingV1(
    worr_native_readiness_sideband_parser_v1 *parser,
    int32_t index,
    int32_t value)
{
    const worr_native_readiness_sideband_result_v1 ready =
        parser_ready_for_setting(parser);

    if (ready != WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED)
        return ready;
    if (!sideband_index_recognized(index))
        return observe_non_sideband(parser);
    if (value < INT16_MIN || value > INT16_MAX) {
        saturating_increment(&parser->telemetry.settings_seen);
        poison_record(parser);
        saturating_increment(&parser->telemetry.value_range_failures);
        return WORR_NATIVE_READINESS_SIDEBAND_VALUE_OUT_OF_RANGE;
    }
    return observe_pair_validated(parser, (int16_t)index, (int16_t)value);
}

worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandObserveInterveningServiceV1(
    worr_native_readiness_sideband_parser_v1 *parser)
{
    const worr_native_readiness_sideband_result_v1 ready =
        parser_ready_for_setting(parser);

    if (ready != WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED)
        return ready;
    return intervening_reset(parser);
}

worr_native_readiness_sideband_result_v1
Worr_NativeReadinessSidebandTakeRecordV1(
    worr_native_readiness_sideband_parser_v1 *parser,
    worr_native_readiness_record_v1 *record_out)
{
    worr_native_readiness_record_v1 output;

    if (parser == NULL)
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_NativeReadinessSidebandParserValidateV1(parser))
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_STATE;
    if (record_out == NULL) {
        saturating_increment(&parser->telemetry.invalid_arguments);
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT;
    }
    if (!ranges_disjoint(parser, sizeof(*parser),
                         record_out, sizeof(*record_out))) {
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_ARGUMENT;
    }
    if (parser->packet_active == 0) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_NATIVE_READINESS_SIDEBAND_INVALID_STATE;
    }
    if (parser->phase != WORR_NATIVE_READINESS_SIDEBAND_PHASE_RECORD)
        return WORR_NATIVE_READINESS_SIDEBAND_NO_RECORD;

    output = parser->pending_record;
    parser_idle(parser);
    saturating_increment(&parser->telemetry.records_taken);
    *record_out = output;
    return WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN;
}
