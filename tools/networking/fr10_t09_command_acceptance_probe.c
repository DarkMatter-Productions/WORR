/*
Copyright (C) 2026 WORR contributors

Task-level deterministic acceptance probe for FR-10-T09.
*/

#include "common/net/command_stream.h"
#include "common/net/legacy_command_adapter.h"
#include "common/net/native_codec.h"
#include "common/net/native_command_shadow.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CANONICAL_COMMAND_COUNT UINT32_C(1000000)
#define LEGACY_COMMAND_COUNT UINT32_C(100002)
#define STREAM_CAPACITY UINT32_C(128)
#define STALE_DISTANCE UINT32_C(129)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return false;                                                    \
        }                                                                    \
    } while (0)

typedef struct acceptance_result_s {
    uint64_t canonical_commands;
    uint64_t legacy_commands;
    uint64_t native_round_trips;
    uint64_t duplicate_acknowledgements;
    uint64_t adversarial_rejections;
    uint64_t sequence_wraps;
    uint64_t digest;
} acceptance_result;

static void digest_u64(uint64_t *digest, uint64_t value)
{
    uint32_t index;
    for (index = 0; index < 8; ++index) {
        *digest ^= (uint8_t)(value >> (index * 8u));
        *digest *= UINT64_C(1099511628211);
    }
}

static worr_prediction_command_v1 make_command(uint32_t ordinal,
                                                uint8_t duration_ms)
{
    worr_prediction_command_v1 command;
    const int8_t signed_marker = (int8_t)(ordinal & UINT32_C(0x7f));
    memset(&command, 0, sizeof(command));
    command.struct_size = sizeof(command);
    command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.duration_ms = duration_ms;
    command.buttons = ordinal & 3u;
    command.view_angles[0] = (float)(ordinal % 89u);
    command.view_angles[1] = (float)(ordinal % 179u) - 89.0f;
    command.view_angles[2] = (float)(ordinal % 45u) - 22.0f;
    command.forward_move = (float)signed_marker;
    command.side_move = -(float)signed_marker;
    return command;
}

static worr_command_render_watermark_v1 make_watermark(uint32_t provenance,
                                                        uint32_t tick)
{
    worr_command_render_watermark_v1 watermark;
    memset(&watermark, 0, sizeof(watermark));
    watermark.struct_size = sizeof(watermark);
    watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    watermark.provenance = provenance;
    if (provenance != WORR_COMMAND_RENDER_PROVENANCE_NONE) {
        watermark.source_server_tick = tick;
        watermark.tick_interval_us = 10000;
        watermark.source_server_time_us =
            (uint64_t)tick * UINT64_C(10000);
        watermark.rendered_server_time_us =
            watermark.source_server_time_us;
    }
    return watermark;
}

static worr_command_record_v1 make_record(worr_command_id_v1 command_id,
                                           uint32_t ordinal,
                                           uint8_t duration_ms,
                                           uint64_t sample_time_us,
                                           uint32_t provenance)
{
    worr_command_record_v1 record;
    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id = command_id;
    record.sample_time_us = sample_time_us;
    record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    record.command = make_command(ordinal, duration_ms);
    record.render_watermark = make_watermark(provenance, ordinal + 1u);
    (void)Worr_CommandRecordCanonicalizeV1(
        &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS);
    return record;
}

static bool cursor_equal(worr_command_cursor_v1 a,
                         worr_command_cursor_v1 b)
{
    return a.epoch == b.epoch &&
           a.contiguous_sequence == b.contiguous_sequence;
}

static bool run_canonical_stream(acceptance_result *result)
{
    worr_command_stream_v1 stream;
    worr_command_stream_slot_v1 slots[STREAM_CAPACITY];
    worr_command_record_v1 stale_ring[STALE_DISTANCE];
    worr_command_id_v1 command_id;
    uint64_t sample_time_us = 0;
    uint32_t index;

    memset(stale_ring, 0, sizeof(stale_ring));
    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, STREAM_CAPACITY,
        WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
        (worr_command_cursor_v1){41, UINT32_MAX - UINT32_C(500000)}, 0));
    CHECK(Worr_CommandCursorNextIdV1(stream.received_cursor, &command_id));

    for (index = 0; index < CANONICAL_COMMAND_COUNT; ++index) {
        const uint8_t duration_ms = (uint8_t)(index % 251u);
        worr_command_record_v1 record;
        worr_command_record_v1 consumed;
        worr_command_record_v1 old_record;
        bool have_old = index >= STALE_DISTANCE;
        uint64_t input_hash;

        if (have_old)
            old_record = stale_ring[index % STALE_DISTANCE];
        sample_time_us += (uint64_t)duration_ms * UINT64_C(1000);
        record = make_record(
            command_id, index, duration_ms, sample_time_us,
            WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
        CHECK(Worr_CommandRecordValidateV1(
            &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
        CHECK(Worr_CommandStreamInsertV1(&stream, &record) ==
              WORR_COMMAND_STREAM_INSERTED);
        CHECK(Worr_CommandStreamInsertV1(&stream, &record) ==
              WORR_COMMAND_STREAM_DUPLICATE);
        ++result->duplicate_acknowledgements;

        if ((index % 10u) == 0) {
            worr_command_record_v1 conflict = record;
            conflict.command.buttons ^= 1u;
            CHECK(Worr_CommandStreamInsertV1(&stream, &conflict) ==
                  WORR_COMMAND_STREAM_CONFLICT);
            ++result->adversarial_rejections;
        }
        if ((index % 100u) == 0) {
            worr_command_id_v1 next_id;
            worr_command_id_v1 future_id;
            worr_command_record_v1 candidate;
            const worr_command_cursor_v1 received_before =
                stream.received_cursor;
            const worr_command_cursor_v1 consumed_before =
                stream.consumed_cursor;
            const uint64_t sample_before =
                stream.last_received_sample_time_us;

            CHECK(Worr_CommandIdNextV1(command_id, &next_id));
            CHECK(Worr_CommandIdNextV1(next_id, &future_id));
            candidate = make_record(
                future_id, index + 1u, 1,
                sample_time_us + UINT64_C(1000),
                WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
            CHECK(Worr_CommandStreamInsertV1(&stream, &candidate) ==
                  WORR_COMMAND_STREAM_FUTURE_GAP);
            ++result->adversarial_rejections;

            candidate.command_id.epoch = command_id.epoch + 5u;
            candidate.command_id.sequence = 1;
            CHECK(Worr_CommandStreamInsertV1(&stream, &candidate) ==
                  WORR_COMMAND_STREAM_WRONG_EPOCH);
            ++result->adversarial_rejections;

            candidate = make_record(
                next_id, index + 1u, 1,
                sample_time_us + UINT64_C(1000),
                WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
            candidate.command.duration_ms = 251;
            CHECK(Worr_CommandStreamInsertV1(&stream, &candidate) ==
                  WORR_COMMAND_STREAM_INVALID_RECORD);
            ++result->adversarial_rejections;
            CHECK(cursor_equal(stream.received_cursor, received_before));
            CHECK(cursor_equal(stream.consumed_cursor, consumed_before));
            CHECK(stream.last_received_sample_time_us == sample_before);
        }

        CHECK(Worr_CommandStreamConsumeV1(
                  &stream, command_id, &consumed) ==
              WORR_COMMAND_STREAM_CONSUMED);
        CHECK(memcmp(&consumed, &record, sizeof(record)) == 0);
        CHECK(Worr_CommandStreamConsumeV1(
                  &stream, command_id, NULL) ==
              WORR_COMMAND_STREAM_ALREADY_CONSUMED);
        ++result->duplicate_acknowledgements;

        if (have_old && (index % 100u) == 0) {
            const worr_command_stream_result_v1 stale_result =
                Worr_CommandStreamInsertV1(&stream, &old_record);
            CHECK(stale_result == WORR_COMMAND_STREAM_STALE ||
                  stale_result == WORR_COMMAND_STREAM_WRONG_EPOCH);
            ++result->adversarial_rejections;
        }

        stale_ring[index % STALE_DISTANCE] = record;
        CHECK(Worr_CommandRecordInputHashV1(
            &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            &input_hash));
        digest_u64(&result->digest, input_hash);
        if ((index & UINT32_C(0xfff)) == 0)
            CHECK(Worr_CommandStreamValidateV1(&stream));
        if (index + 1u < CANONICAL_COMMAND_COUNT)
            CHECK(Worr_CommandIdNextV1(command_id, &command_id));
    }

    CHECK(Worr_CommandStreamValidateV1(&stream));
    CHECK(stream.telemetry.inserted == CANONICAL_COMMAND_COUNT);
    CHECK(stream.telemetry.duplicates == CANONICAL_COMMAND_COUNT);
    CHECK(stream.telemetry.consumed == CANONICAL_COMMAND_COUNT);
    CHECK(stream.telemetry.already_consumed == CANONICAL_COMMAND_COUNT);
    CHECK(stream.telemetry.conflicts == CANONICAL_COMMAND_COUNT / 10u);
    CHECK(stream.telemetry.future_gaps == CANONICAL_COMMAND_COUNT / 100u);
    CHECK(stream.telemetry.invalid_records ==
          CANONICAL_COMMAND_COUNT / 100u);
    CHECK(stream.telemetry.epoch_wraps == 1);
    result->canonical_commands = CANONICAL_COMMAND_COUNT;
    result->sequence_wraps += stream.telemetry.epoch_wraps;
    digest_u64(&result->digest, stream.received_cursor.epoch);
    digest_u64(&result->digest,
               stream.received_cursor.contiguous_sequence);
    digest_u64(&result->digest, stream.last_received_sample_time_us);
    return true;
}

static bool feed_legacy_sideband(
    worr_legacy_command_sideband_parser_v1 *parser,
    const worr_legacy_command_range_v1 *range)
{
    worr_legacy_command_setting_pair_v1 pairs[
        WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT];
    worr_legacy_command_range_v1 decoded;
    uint32_t index;

    CHECK(Worr_LegacyCommandSidebandEncodeV1(
        range, pairs, WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT));
    CHECK(Worr_LegacyCommandSidebandPacketBeginV1(parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_PACKET_STARTED);
    for (index = 0;
         index < WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT; ++index) {
        const worr_legacy_command_sideband_result_v1 observed =
            Worr_LegacyCommandSidebandObserveSettingV1(
                parser, pairs[index].index, pairs[index].value);
        CHECK(observed ==
              (index + 1u == WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT
                   ? WORR_LEGACY_COMMAND_SIDEBAND_HEADER_COMMITTED
                   : WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED));
    }
    CHECK(Worr_LegacyCommandSidebandConsumeMoveV1(
              parser, WORR_LEGACY_COMMAND_CARRIER_MOVE,
              WORR_LEGACY_COMMAND_MOVE_COUNT, &decoded) ==
          WORR_LEGACY_COMMAND_SIDEBAND_MOVE_MATCHED);
    CHECK(memcmp(&decoded, range, sizeof(decoded)) == 0);
    CHECK(Worr_LegacyCommandSidebandPacketEndV1(parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_PACKET_ENDED);
    return true;
}

static bool run_dual_adapter_stream(acceptance_result *result)
{
    worr_command_stream_v1 legacy_stream;
    worr_command_stream_slot_v1 legacy_slots[STREAM_CAPACITY];
    worr_command_stream_slot_v1 stream_scratch[STREAM_CAPACITY];
    worr_command_record_v1 record_scratch[
        WORR_LEGACY_COMMAND_MOVE_COUNT];
    worr_prediction_command_v1 commands[WORR_LEGACY_COMMAND_MOVE_COUNT];
    worr_legacy_command_sideband_parser_v1 parser;
    worr_native_command_shadow_comparator_v1 comparator;
    worr_command_id_v1 first_id = {
        77, UINT32_MAX - UINT32_C(50001)};
    uint64_t native_sample_time_us = 0;
    uint32_t processed = 0;

    CHECK(Worr_CommandStreamInitV1(
        &legacy_stream, legacy_slots, STREAM_CAPACITY,
        WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
        (worr_command_cursor_v1){first_id.epoch,
                                 first_id.sequence - 1u},
        UINT64_C(50000)));
    CHECK(Worr_LegacyCommandSidebandParserInitV1(&parser));
    CHECK(Worr_NativeCommandShadowComparatorInitV1(
        &comparator, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));

    while (processed < LEGACY_COMMAND_COUNT) {
        worr_legacy_command_range_v1 range;
        worr_legacy_command_adapter_report_v1 report;
        worr_command_render_watermark_v1 watermark =
            make_watermark(
                WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED,
                processed / WORR_LEGACY_COMMAND_MOVE_COUNT + 1u);
        worr_command_id_v1 command_id = first_id;
        uint32_t index;

        for (index = 0; index < WORR_LEGACY_COMMAND_MOVE_COUNT; ++index)
            commands[index] = make_command(
                processed + index,
                (uint8_t)((processed + index) % 16u + 1u));
        CHECK(Worr_LegacyCommandRangeInitV1(
            &range, first_id, WORR_LEGACY_COMMAND_MOVE_COUNT));
        CHECK(feed_legacy_sideband(&parser, &range));
        CHECK(Worr_LegacyCommandAdapterApplyV1(
                  &legacy_stream, &range, commands,
                  WORR_LEGACY_COMMAND_MOVE_COUNT,
                  WORR_PREDICTION_MODEL_REVISION, &watermark,
                  record_scratch, WORR_LEGACY_COMMAND_MOVE_COUNT,
                  stream_scratch, STREAM_CAPACITY, &report) ==
              WORR_LEGACY_COMMAND_ADAPTER_APPLIED);
        CHECK(report.inserted_count == WORR_LEGACY_COMMAND_MOVE_COUNT);
        CHECK(Worr_LegacyCommandAdapterApplyV1(
                  &legacy_stream, &range, commands,
                  WORR_LEGACY_COMMAND_MOVE_COUNT,
                  WORR_PREDICTION_MODEL_REVISION, &watermark,
                  record_scratch, WORR_LEGACY_COMMAND_MOVE_COUNT,
                  stream_scratch, STREAM_CAPACITY, &report) ==
              WORR_LEGACY_COMMAND_ADAPTER_IDEMPOTENT);
        CHECK(report.duplicate_count == WORR_LEGACY_COMMAND_MOVE_COUNT);
        result->duplicate_acknowledgements +=
            WORR_LEGACY_COMMAND_MOVE_COUNT;

        for (index = 0; index < WORR_LEGACY_COMMAND_MOVE_COUNT; ++index) {
            worr_command_record_v1 legacy_record;
            worr_command_record_v1 native_record;
            worr_command_record_v1 decoded;
            worr_native_command_shadow_compare_report_v1 comparison;
            uint8_t encoded[WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES];
            size_t encoded_bytes = 0;
            uint64_t input_hash;

            native_sample_time_us +=
                (uint64_t)commands[index].duration_ms * UINT64_C(1000);
            CHECK(Worr_CommandStreamCopyRecordV1(
                &legacy_stream, command_id, &legacy_record));
            native_record = make_record(
                command_id, processed + index,
                commands[index].duration_ms, native_sample_time_us,
                WORR_COMMAND_RENDER_PROVENANCE_NONE);
            CHECK(Worr_NativeCommandShadowCompareV1(
                      &comparator, &native_record, &legacy_record,
                      &comparison) ==
                  WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED);
            CHECK(comparison.observed_offset_direction ==
                  WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_LEGACY_AHEAD);
            CHECK(comparison.observed_offset_us == UINT64_C(50000));
            CHECK(Worr_NativeCodecCommandEncodeV1(
                      &native_record,
                      WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
                      encoded, sizeof(encoded), &encoded_bytes) ==
                  WORR_NATIVE_CODEC_OK);
            CHECK(encoded_bytes == WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES);
            CHECK(Worr_NativeCodecCommandDecodeV1(
                      encoded, encoded_bytes,
                      WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
                      &decoded) == WORR_NATIVE_CODEC_OK);
            CHECK(Worr_CommandRecordSemanticallyEqualV1(
                &native_record, &decoded,
                WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
            CHECK(Worr_CommandRecordInputHashV1(
                &decoded, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
                &input_hash));
            digest_u64(&result->digest, input_hash);
            CHECK(Worr_CommandStreamConsumeV1(
                      &legacy_stream, command_id, NULL) ==
                  WORR_COMMAND_STREAM_CONSUMED);
            ++result->legacy_commands;
            ++result->native_round_trips;
            if (index + 1u < WORR_LEGACY_COMMAND_MOVE_COUNT)
                CHECK(Worr_CommandIdNextV1(command_id, &command_id));
        }

        processed += WORR_LEGACY_COMMAND_MOVE_COUNT;
        first_id = command_id;
        CHECK(Worr_CommandIdNextV1(first_id, &first_id));
        if ((processed & UINT32_C(0xfff)) == 0) {
            CHECK(Worr_CommandStreamValidateV1(&legacy_stream));
            CHECK(Worr_LegacyCommandSidebandParserValidateV1(&parser));
            CHECK(Worr_NativeCommandShadowComparatorValidateV1(
                &comparator));
        }
    }

    CHECK(result->legacy_commands == LEGACY_COMMAND_COUNT);
    CHECK(result->native_round_trips == LEGACY_COMMAND_COUNT);
    CHECK(legacy_stream.telemetry.epoch_wraps == 1);
    CHECK(comparator.telemetry.matched == LEGACY_COMMAND_COUNT);
    CHECK(comparator.telemetry.command_mismatches == 0);
    CHECK(comparator.telemetry.offset_mismatches == 0);
    CHECK(parser.telemetry.moves_matched ==
          LEGACY_COMMAND_COUNT / WORR_LEGACY_COMMAND_MOVE_COUNT);
    CHECK(Worr_CommandStreamValidateV1(&legacy_stream));
    CHECK(Worr_LegacyCommandSidebandParserValidateV1(&parser));
    CHECK(Worr_NativeCommandShadowComparatorValidateV1(&comparator));
    result->sequence_wraps += legacy_stream.telemetry.epoch_wraps;
    digest_u64(&result->digest, legacy_stream.received_cursor.epoch);
    digest_u64(&result->digest,
               legacy_stream.received_cursor.contiguous_sequence);
    digest_u64(&result->digest,
               legacy_stream.last_received_sample_time_us);
    return true;
}

int main(void)
{
    acceptance_result result;
    memset(&result, 0, sizeof(result));
    result.digest = UINT64_C(1469598103934665603);

    if (!run_canonical_stream(&result) ||
        !run_dual_adapter_stream(&result)) {
        return 1;
    }
    if (result.adversarial_rejections < UINT64_C(100000) ||
        result.sequence_wraps != 2) {
        fprintf(stderr, "acceptance floors were not met\n");
        return 1;
    }

    printf("{\"schema\":\"worr.networking.fr10-t09-command-probe.v1\","
           "\"canonical_commands\":%" PRIu64 ","
           "\"legacy_commands\":%" PRIu64 ","
           "\"native_round_trips\":%" PRIu64 ","
           "\"duplicate_acknowledgements\":%" PRIu64 ","
           "\"adversarial_rejections\":%" PRIu64 ","
           "\"sequence_wraps\":%" PRIu64 ","
           "\"digest\":\"%016" PRIx64 "\"}\n",
           result.canonical_commands, result.legacy_commands,
           result.native_round_trips,
           result.duplicate_acknowledgements,
           result.adversarial_rejections, result.sequence_wraps,
           result.digest);
    return 0;
}
