/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_codec.h"

#include <string.h>

#define NATIVE_CODEC_MAGIC_0 ((uint8_t)'W')
#define NATIVE_CODEC_MAGIC_1 ((uint8_t)'N')
#define NATIVE_CODEC_MAGIC_2 ((uint8_t)'C')
#define NATIVE_CODEC_MAGIC_3 ((uint8_t)'1')

#define SNAPSHOT_ENTITY_BASE_BYTES 28u

typedef struct byte_writer_s {
    uint8_t *cursor;
} byte_writer;

typedef struct byte_reader_s {
    const uint8_t *cursor;
    const uint8_t *end;
} byte_reader;

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes)
{
    uintptr_t left_begin;
    uintptr_t right_begin;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left == NULL || right == NULL || left_bytes == 0 || right_bytes == 0)
        return false;
    left_begin = (uintptr_t)left;
    right_begin = (uintptr_t)right;
    if (left_bytes > UINTPTR_MAX - left_begin ||
        right_bytes > UINTPTR_MAX - right_begin) {
        return true;
    }
    left_end = left_begin + left_bytes;
    right_end = right_begin + right_bytes;
    return left_begin < right_end && right_begin < left_end;
}

static uint16_t load_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static uint32_t load_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void put_u8(byte_writer *writer, uint8_t value)
{
    *writer->cursor++ = value;
}

static void put_u16(byte_writer *writer, uint16_t value)
{
    put_u8(writer, (uint8_t)value);
    put_u8(writer, (uint8_t)(value >> 8));
}

static void put_u32(byte_writer *writer, uint32_t value)
{
    put_u8(writer, (uint8_t)value);
    put_u8(writer, (uint8_t)(value >> 8));
    put_u8(writer, (uint8_t)(value >> 16));
    put_u8(writer, (uint8_t)(value >> 24));
}

static void put_u64(byte_writer *writer, uint64_t value)
{
    unsigned int index;
    for (index = 0; index < 8; ++index)
        put_u8(writer, (uint8_t)(value >> (index * 8u)));
}

static uint32_t canonical_float_bits(float value)
{
    uint32_t bits;
    if (value == 0.0f)
        return 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void put_float(byte_writer *writer, float value)
{
    put_u32(writer, canonical_float_bits(value));
}

static bool get_u8(byte_reader *reader, uint8_t *value_out)
{
    if (reader->cursor == reader->end)
        return false;
    *value_out = *reader->cursor++;
    return true;
}

static bool get_u16(byte_reader *reader, uint16_t *value_out)
{
    if ((size_t)(reader->end - reader->cursor) < 2)
        return false;
    *value_out = load_u16(reader->cursor);
    reader->cursor += 2;
    return true;
}

static bool get_u32(byte_reader *reader, uint32_t *value_out)
{
    if ((size_t)(reader->end - reader->cursor) < 4)
        return false;
    *value_out = load_u32(reader->cursor);
    reader->cursor += 4;
    return true;
}

static bool get_u64(byte_reader *reader, uint64_t *value_out)
{
    uint64_t value = 0;
    unsigned int index;
    if ((size_t)(reader->end - reader->cursor) < 8)
        return false;
    for (index = 0; index < 8; ++index)
        value |= (uint64_t)reader->cursor[index] << (index * 8u);
    reader->cursor += 8;
    *value_out = value;
    return true;
}

static bool get_float(byte_reader *reader, float *value_out)
{
    uint32_t bits;
    if (!get_u32(reader, &bits))
        return false;
    /* Signed zero has one canonical wire image. */
    if (bits == UINT32_C(0x80000000))
        return false;
    memcpy(value_out, &bits, sizeof(bits));
    return true;
}

static bool get_i16(byte_reader *reader, int16_t *value_out)
{
    uint16_t value;
    if (!get_u16(reader, &value))
        return false;
    *value_out = (int16_t)value;
    return true;
}

static bool get_i32(byte_reader *reader, int32_t *value_out)
{
    uint32_t value;
    if (!get_u32(reader, &value))
        return false;
    *value_out = (int32_t)value;
    return true;
}

static void put_float_array(byte_writer *writer, const float *values,
                            uint32_t count)
{
    uint32_t index;
    for (index = 0; index < count; ++index)
        put_float(writer, values[index]);
}

static bool get_float_array(byte_reader *reader, float *values,
                            uint32_t count)
{
    uint32_t index;
    for (index = 0; index < count; ++index) {
        if (!get_float(reader, &values[index]))
            return false;
    }
    return true;
}

static void write_header(byte_writer *writer,
                         uint8_t record_class,
                         uint16_t record_schema_version,
                         uint32_t model_revision,
                         uint32_t encoded_bytes,
                         uint32_t fixed_body_bytes,
                         uint32_t range0,
                         uint32_t range1,
                         uint32_t range2,
                         uint32_t object_epoch,
                         uint32_t object_sequence)
{
    put_u8(writer, NATIVE_CODEC_MAGIC_0);
    put_u8(writer, NATIVE_CODEC_MAGIC_1);
    put_u8(writer, NATIVE_CODEC_MAGIC_2);
    put_u8(writer, NATIVE_CODEC_MAGIC_3);
    put_u16(writer, WORR_NATIVE_CODEC_WIRE_VERSION);
    put_u16(writer, WORR_NATIVE_CODEC_WIRE_HEADER_BYTES);
    put_u8(writer, record_class);
    put_u8(writer, 0);
    put_u16(writer, record_schema_version);
    put_u32(writer, model_revision);
    put_u32(writer, encoded_bytes);
    put_u32(writer, fixed_body_bytes);
    put_u32(writer, range0);
    put_u32(writer, range1);
    put_u32(writer, range2);
    put_u32(writer, object_epoch);
    put_u32(writer, object_sequence);
    put_u32(writer, 0);
}

static bool info_valid(const worr_native_codec_info_v1 *info)
{
    if (info == NULL || info->struct_size != sizeof(*info) ||
        info->schema_version != WORR_NATIVE_CODEC_ABI_VERSION ||
        info->flags != 0 || info->reserved0 != 0 ||
        info->reserved1 != 0 || info->object_epoch == 0 ||
        info->object_sequence == 0) {
        return false;
    }
    switch (info->record_class) {
    case WORR_NATIVE_RECORD_COMMAND_V1:
        return info->record_schema_version == WORR_COMMAND_ABI_VERSION &&
               info->model_revision == WORR_PREDICTION_MODEL_REVISION;
    case WORR_NATIVE_RECORD_SNAPSHOT_V1:
        return info->record_schema_version == WORR_SNAPSHOT_ABI_VERSION &&
               info->model_revision == WORR_SNAPSHOT_MODEL_REVISION;
    case WORR_NATIVE_RECORD_EVENT_V1:
        return info->record_schema_version == WORR_EVENT_ABI_VERSION &&
               info->model_revision == WORR_EVENT_MODEL_REVISION;
    case WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1:
        return info->record_schema_version ==
                   WORR_EVENT_STREAM_ABI_VERSION &&
               info->model_revision == WORR_EVENT_MODEL_REVISION;
    default:
        return false;
    }
}

worr_native_codec_result_v1 Worr_NativeCodecInspectV1(
    const void *encoded,
    size_t encoded_bytes,
    worr_native_codec_info_v1 *info_out)
{
    const uint8_t *bytes = (const uint8_t *)encoded;
    worr_native_codec_info_v1 info;
    uint16_t wire_version;
    uint16_t header_bytes;
    uint32_t minimum_bytes;

    if (encoded == NULL || info_out == NULL ||
        ranges_overlap(encoded, encoded_bytes, info_out, sizeof(*info_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    if (encoded_bytes < WORR_NATIVE_CODEC_WIRE_HEADER_BYTES)
        return WORR_NATIVE_CODEC_MALFORMED;
    if (bytes[0] != NATIVE_CODEC_MAGIC_0 ||
        bytes[1] != NATIVE_CODEC_MAGIC_1 ||
        bytes[2] != NATIVE_CODEC_MAGIC_2 ||
        bytes[3] != NATIVE_CODEC_MAGIC_3) {
        return WORR_NATIVE_CODEC_MALFORMED;
    }

    wire_version = load_u16(bytes + 4);
    header_bytes = load_u16(bytes + 6);
    if (wire_version != WORR_NATIVE_CODEC_WIRE_VERSION ||
        header_bytes != WORR_NATIVE_CODEC_WIRE_HEADER_BYTES) {
        return WORR_NATIVE_CODEC_UNSUPPORTED;
    }

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.schema_version = WORR_NATIVE_CODEC_ABI_VERSION;
    info.record_class = bytes[8];
    info.flags = bytes[9];
    info.record_schema_version = load_u16(bytes + 10);
    info.model_revision = load_u32(bytes + 12);
    info.encoded_bytes = load_u32(bytes + 16);
    info.fixed_body_bytes = load_u32(bytes + 20);
    info.range_counts[0] = load_u32(bytes + 24);
    info.range_counts[1] = load_u32(bytes + 28);
    info.range_counts[2] = load_u32(bytes + 32);
    info.object_epoch = load_u32(bytes + 36);
    info.object_sequence = load_u32(bytes + 40);
    info.reserved1 = load_u32(bytes + 44);

    if (info.flags != 0 || info.reserved1 != 0 ||
        info.encoded_bytes != encoded_bytes ||
        info.encoded_bytes > WORR_NATIVE_CODEC_MAX_ENCODED_BYTES ||
        info.object_epoch == 0 || info.object_sequence == 0) {
        return info.encoded_bytes > WORR_NATIVE_CODEC_MAX_ENCODED_BYTES
                   ? WORR_NATIVE_CODEC_LIMIT
                   : WORR_NATIVE_CODEC_MALFORMED;
    }

    switch (info.record_class) {
    case WORR_NATIVE_RECORD_COMMAND_V1:
        if (info.record_schema_version != WORR_COMMAND_ABI_VERSION ||
            info.model_revision != WORR_PREDICTION_MODEL_REVISION) {
            return WORR_NATIVE_CODEC_UNSUPPORTED;
        }
        if (info.fixed_body_bytes !=
                WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES ||
            info.range_counts[0] != 0 || info.range_counts[1] != 0 ||
            info.range_counts[2] != 0 ||
            info.encoded_bytes != WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                                      WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES) {
            return WORR_NATIVE_CODEC_MALFORMED;
        }
        break;
    case WORR_NATIVE_RECORD_EVENT_V1:
        if (info.record_schema_version != WORR_EVENT_ABI_VERSION ||
            info.model_revision != WORR_EVENT_MODEL_REVISION) {
            return WORR_NATIVE_CODEC_UNSUPPORTED;
        }
        if (info.fixed_body_bytes !=
                WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES ||
            info.range_counts[0] > WORR_EVENT_PAYLOAD_CAPACITY ||
            info.range_counts[1] != 0 || info.range_counts[2] != 0 ||
            info.encoded_bytes != WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                                      WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES +
                                      info.range_counts[0]) {
            return WORR_NATIVE_CODEC_MALFORMED;
        }
        break;
    case WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1:
        if (info.record_schema_version !=
                WORR_EVENT_STREAM_ABI_VERSION ||
            info.model_revision != WORR_EVENT_MODEL_REVISION) {
            return WORR_NATIVE_CODEC_UNSUPPORTED;
        }
        if (info.fixed_body_bytes !=
                WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES ||
            info.range_counts[0] != 0 || info.range_counts[1] != 0 ||
            info.range_counts[2] != 0 ||
            info.encoded_bytes !=
                WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                    WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES) {
            return WORR_NATIVE_CODEC_MALFORMED;
        }
        break;
    case WORR_NATIVE_RECORD_SNAPSHOT_V1:
        if (info.record_schema_version != WORR_SNAPSHOT_ABI_VERSION ||
            info.model_revision != WORR_SNAPSHOT_MODEL_REVISION) {
            return WORR_NATIVE_CODEC_UNSUPPORTED;
        }
        if (info.fixed_body_bytes !=
            WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES) {
            return WORR_NATIVE_CODEC_MALFORMED;
        }
        if (info.range_counts[0] >
                WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES ||
            info.range_counts[1] >
                WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES ||
            info.range_counts[2] >
                WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS) {
            return WORR_NATIVE_CODEC_LIMIT;
        }
        minimum_bytes = WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                        WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES +
                        info.range_counts[0] *
                            WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MIN_BYTES +
                        info.range_counts[1] +
                        info.range_counts[2] *
                            WORR_NATIVE_CODEC_SNAPSHOT_EVENT_REF_BYTES;
        if (info.encoded_bytes < minimum_bytes)
            return WORR_NATIVE_CODEC_MALFORMED;
        break;
    default:
        return WORR_NATIVE_CODEC_UNSUPPORTED;
    }

    *info_out = info;
    return WORR_NATIVE_CODEC_OK;
}

bool Worr_NativeCodecInfoRecordRefV1(
    const worr_native_codec_info_v1 *info,
    worr_native_record_ref_v1 *record_out)
{
    worr_native_record_ref_v1 record;
    if (!info_valid(info) || record_out == NULL ||
        ranges_overlap(info, sizeof(*info), record_out, sizeof(*record_out))) {
        return false;
    }
    memset(&record, 0, sizeof(record));
    record.record_class = info->record_class;
    record.record_schema_version = info->record_schema_version;
    record.object_epoch = info->object_epoch;
    record.object_sequence = info->object_sequence;
    if (!Worr_NativeEnvelopeRecordRefValidV1(record))
        return false;
    *record_out = record;
    return true;
}

static void write_event_stream_body(
    byte_writer *writer,
    const worr_event_stream_descriptor_v1 *descriptor)
{
    put_u32(writer, descriptor->event_schema_version);
    put_u32(writer, descriptor->flags);
}

static bool read_event_stream_body(
    byte_reader *reader,
    const worr_native_codec_info_v1 *info,
    worr_event_stream_descriptor_v1 *descriptor_out)
{
    worr_event_stream_descriptor_v1 descriptor;
    uint32_t flags;

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.struct_size = sizeof(descriptor);
    descriptor.schema_version = WORR_EVENT_STREAM_ABI_VERSION;
    descriptor.stream_epoch = info->object_epoch;
    descriptor.first_sequence = info->object_sequence;
    descriptor.model_revision = info->model_revision;
    if (!get_u32(reader, &descriptor.event_schema_version) ||
        !get_u32(reader, &flags) || flags > UINT16_MAX) {
        return false;
    }
    descriptor.flags = (uint16_t)flags;
    *descriptor_out = descriptor;
    return true;
}

worr_native_codec_result_v1 Worr_NativeCodecEventStreamPreflightV1(
    const worr_event_stream_descriptor_v1 *descriptor,
    uint32_t *encoded_bytes_out)
{
    const uint32_t encoded_bytes =
        WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
        WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES;

    if (descriptor == NULL || encoded_bytes_out == NULL ||
        ranges_overlap(descriptor, sizeof(*descriptor),
                       encoded_bytes_out, sizeof(*encoded_bytes_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    if (!Worr_EventStreamDescriptorValidateV1(descriptor))
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    *encoded_bytes_out = encoded_bytes;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecEventStreamEncodeV1(
    const worr_event_stream_descriptor_v1 *descriptor,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out)
{
    uint32_t encoded_bytes;
    byte_writer writer;
    worr_native_codec_result_v1 result;

    if (encoded_out == NULL || encoded_bytes_out == NULL)
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    result = Worr_NativeCodecEventStreamPreflightV1(
        descriptor, &encoded_bytes);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    if (encoded_capacity < encoded_bytes)
        return WORR_NATIVE_CODEC_OUTPUT_TOO_SMALL;
    if (ranges_overlap(descriptor, sizeof(*descriptor), encoded_out,
                       encoded_bytes) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       encoded_out, encoded_bytes) ||
        ranges_overlap(descriptor, sizeof(*descriptor),
                       encoded_bytes_out, sizeof(*encoded_bytes_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }

    writer.cursor = (uint8_t *)encoded_out;
    write_header(
        &writer, WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1,
        WORR_EVENT_STREAM_ABI_VERSION, descriptor->model_revision,
        encoded_bytes, WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES,
        0, 0, 0, descriptor->stream_epoch, descriptor->first_sequence);
    write_event_stream_body(&writer, descriptor);
    *encoded_bytes_out = encoded_bytes;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecEventStreamDecodeV1(
    const void *encoded,
    size_t encoded_bytes,
    worr_event_stream_descriptor_v1 *descriptor_out)
{
    worr_native_codec_info_v1 info;
    worr_event_stream_descriptor_v1 descriptor;
    byte_reader reader;
    worr_native_codec_result_v1 result;

    if (descriptor_out == NULL ||
        ranges_overlap(encoded, encoded_bytes, descriptor_out,
                       sizeof(*descriptor_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    result = Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    if (info.record_class !=
        WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1) {
        return WORR_NATIVE_CODEC_UNSUPPORTED;
    }
    reader.cursor = (const uint8_t *)encoded +
                    WORR_NATIVE_CODEC_WIRE_HEADER_BYTES;
    reader.end = (const uint8_t *)encoded + encoded_bytes;
    if (!read_event_stream_body(&reader, &info, &descriptor) ||
        reader.cursor != reader.end) {
        return WORR_NATIVE_CODEC_MALFORMED;
    }
    if (!Worr_EventStreamDescriptorValidateV1(&descriptor))
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    *descriptor_out = descriptor;
    return WORR_NATIVE_CODEC_OK;
}

static void write_command_body(byte_writer *writer,
                               const worr_command_record_v1 *record)
{
    const worr_prediction_command_v1 *command = &record->command;
    const worr_command_render_watermark_v1 *watermark =
        &record->render_watermark;

    put_u64(writer, record->sample_time_us);
    put_u8(writer, command->duration_ms);
    put_u8(writer, command->buttons);
    put_float_array(writer, command->view_angles, 3);
    put_float(writer, command->forward_move);
    put_float(writer, command->side_move);
    put_u32(writer, watermark->provenance);
    put_u32(writer, watermark->flags);
    put_u32(writer, watermark->source_server_tick);
    put_u32(writer, watermark->tick_interval_us);
    put_u64(writer, watermark->source_server_time_us);
    put_u64(writer, watermark->rendered_server_time_us);
}

static bool read_command_body(byte_reader *reader,
                              const worr_native_codec_info_v1 *info,
                              worr_command_record_v1 *record_out)
{
    worr_command_record_v1 record;
    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id.epoch = info->object_epoch;
    record.command_id.sequence = info->object_sequence;
    record.movement_model_revision = info->model_revision;
    record.command.struct_size = sizeof(record.command);
    record.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    record.render_watermark.struct_size = sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;

    if (!get_u64(reader, &record.sample_time_us) ||
        !get_u8(reader, &record.command.duration_ms) ||
        !get_u8(reader, &record.command.buttons) ||
        !get_float_array(reader, record.command.view_angles, 3) ||
        !get_float(reader, &record.command.forward_move) ||
        !get_float(reader, &record.command.side_move) ||
        !get_u32(reader, &record.render_watermark.provenance) ||
        !get_u32(reader, &record.render_watermark.flags) ||
        !get_u32(reader, &record.render_watermark.source_server_tick) ||
        !get_u32(reader, &record.render_watermark.tick_interval_us) ||
        !get_u64(reader, &record.render_watermark.source_server_time_us) ||
        !get_u64(reader, &record.render_watermark.rendered_server_time_us)) {
        return false;
    }
    *record_out = record;
    return true;
}

worr_native_codec_result_v1 Worr_NativeCodecCommandPreflightV1(
    const worr_command_record_v1 *record,
    uint16_t max_duration_ms,
    uint32_t *encoded_bytes_out)
{
    const uint32_t encoded_bytes =
        WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
        WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES;
    if (record == NULL || encoded_bytes_out == NULL ||
        ranges_overlap(record, sizeof(*record), encoded_bytes_out,
                       sizeof(*encoded_bytes_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    if (!Worr_CommandRecordValidateV1(record, max_duration_ms))
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    *encoded_bytes_out = encoded_bytes;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecCommandEncodeV1(
    const worr_command_record_v1 *record,
    uint16_t max_duration_ms,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out)
{
    uint32_t encoded_bytes;
    byte_writer writer;
    worr_native_codec_result_v1 result;

    if (encoded_out == NULL || encoded_bytes_out == NULL)
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    result = Worr_NativeCodecCommandPreflightV1(
        record, max_duration_ms, &encoded_bytes);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    if (encoded_capacity < encoded_bytes)
        return WORR_NATIVE_CODEC_OUTPUT_TOO_SMALL;
    if (ranges_overlap(record, sizeof(*record), encoded_out, encoded_bytes) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       encoded_out, encoded_bytes) ||
        ranges_overlap(record, sizeof(*record), encoded_bytes_out,
                       sizeof(*encoded_bytes_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }

    writer.cursor = (uint8_t *)encoded_out;
    write_header(&writer, WORR_NATIVE_RECORD_COMMAND_V1,
                 WORR_COMMAND_ABI_VERSION,
                 record->movement_model_revision, encoded_bytes,
                 WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES, 0, 0, 0,
                 record->command_id.epoch, record->command_id.sequence);
    write_command_body(&writer, record);
    *encoded_bytes_out = encoded_bytes;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecCommandDecodeV1(
    const void *encoded,
    size_t encoded_bytes,
    uint16_t max_duration_ms,
    worr_command_record_v1 *record_out)
{
    worr_native_codec_info_v1 info;
    worr_command_record_v1 record;
    byte_reader reader;
    worr_native_codec_result_v1 result;

    if (record_out == NULL ||
        ranges_overlap(encoded, encoded_bytes, record_out,
                       sizeof(*record_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    result = Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    if (info.record_class != WORR_NATIVE_RECORD_COMMAND_V1)
        return WORR_NATIVE_CODEC_UNSUPPORTED;
    reader.cursor = (const uint8_t *)encoded +
                    WORR_NATIVE_CODEC_WIRE_HEADER_BYTES;
    reader.end = (const uint8_t *)encoded + encoded_bytes;
    if (!read_command_body(&reader, &info, &record) ||
        reader.cursor != reader.end) {
        return WORR_NATIVE_CODEC_MALFORMED;
    }
    if (!Worr_CommandRecordValidateV1(&record, max_duration_ms))
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    *record_out = record;
    return WORR_NATIVE_CODEC_OK;
}

static uint32_t event_payload_wire_bytes(uint16_t payload_kind)
{
    switch (payload_kind) {
    case WORR_EVENT_PAYLOAD_NONE:
        return 0;
    case WORR_EVENT_PAYLOAD_VEC3:
        return 12;
    case WORR_EVENT_PAYLOAD_ENTITY_REF:
        return 8;
    case WORR_EVENT_PAYLOAD_DAMAGE:
        return 40;
    case WORR_EVENT_PAYLOAD_AUDIO:
        return 20;
    case WORR_EVENT_PAYLOAD_EFFECT:
        return 32;
    case WORR_EVENT_PAYLOAD_U32X4:
        return 16;
    case WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1:
        return 4;
    case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1:
        return 68;
    case WORR_EVENT_PAYLOAD_MUZZLE_V1:
        return 8;
    case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1:
        return 40;
    case WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1:
        return (uint32_t)sizeof(
            worr_local_interaction_authority_receipt_v1);
    case WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1:
        return (uint32_t)sizeof(
            worr_local_action_shadow_authority_receipt_v1);
    case WORR_EVENT_PAYLOAD_KEYED_POI_V1:
        return 20;
    default:
        return UINT32_MAX;
    }
}

static void write_event_payload(byte_writer *writer,
                                const worr_event_record_v1 *record)
{
    uint32_t index;
    switch (record->payload_kind) {
    case WORR_EVENT_PAYLOAD_NONE:
        break;
    case WORR_EVENT_PAYLOAD_VEC3: {
        worr_event_payload_vec3_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_float_array(writer, payload.value, 3);
        break;
    }
    case WORR_EVENT_PAYLOAD_ENTITY_REF: {
        worr_event_payload_entity_ref_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_u32(writer, payload.entity.index);
        put_u32(writer, payload.entity.generation);
        break;
    }
    case WORR_EVENT_PAYLOAD_DAMAGE: {
        worr_event_payload_damage_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_float(writer, payload.amount);
        put_float(writer, payload.impulse);
        put_float_array(writer, payload.direction, 3);
        put_float_array(writer, payload.point, 3);
        put_u32(writer, payload.damage_flags);
        put_u32(writer, payload.means_of_death);
        break;
    }
    case WORR_EVENT_PAYLOAD_AUDIO: {
        worr_event_payload_audio_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_u32(writer, payload.asset_id);
        put_u32(writer, payload.channel);
        put_float(writer, payload.volume);
        put_float(writer, payload.attenuation);
        put_float(writer, payload.pitch);
        break;
    }
    case WORR_EVENT_PAYLOAD_EFFECT: {
        worr_event_payload_effect_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_u32(writer, payload.effect_id);
        put_u32(writer, payload.variant);
        put_float_array(writer, payload.origin, 3);
        put_float_array(writer, payload.direction, 3);
        break;
    }
    case WORR_EVENT_PAYLOAD_U32X4: {
        worr_event_payload_u32x4_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        for (index = 0; index < 4; ++index)
            put_u32(writer, payload.value[index]);
        break;
    }
    case WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1: {
        worr_event_payload_legacy_entity_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_u16(writer, payload.raw_event);
        put_u16(writer, payload.flags);
        break;
    }
    case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1: {
        worr_event_payload_legacy_temp_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_u16(writer, payload.subtype);
        put_u16(writer, payload.valid_fields);
        put_u16(writer, (uint16_t)payload.raw_entity1);
        put_u16(writer, (uint16_t)payload.raw_entity2);
        put_u32(writer, (uint32_t)payload.time_ms);
        put_u32(writer, (uint32_t)payload.count_or_amount);
        put_u32(writer, (uint32_t)payload.color);
        put_float_array(writer, payload.position1, 3);
        put_float_array(writer, payload.position2, 3);
        put_float_array(writer, payload.offset, 3);
        put_float_array(writer, payload.direction, 3);
        break;
    }
    case WORR_EVENT_PAYLOAD_MUZZLE_V1: {
        worr_event_payload_muzzle_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_u16(writer, payload.family);
        put_u16(writer, payload.flash_id);
        put_u32(writer, payload.flags);
        break;
    }
    case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1: {
        worr_event_payload_spatial_audio_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_u32(writer, payload.asset_id);
        put_u16(writer, payload.channel);
        put_u16(writer, payload.flags);
        put_u32(writer, payload.raw_entity);
        put_float_array(writer, payload.origin, 3);
        put_float(writer, payload.volume);
        put_float(writer, payload.attenuation);
        put_float(writer, payload.time_offset);
        put_float(writer, payload.pitch);
        break;
    }
    case WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1: {
        worr_local_interaction_authority_receipt_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_u32(writer, payload.struct_size);
        put_u32(writer, payload.schema_version);
        put_u32(writer, payload.command_id.epoch);
        put_u32(writer, payload.command_id.sequence);
        put_u64(writer, payload.command_hash);
        put_u64(writer, payload.state_hash);
        put_u64(writer, payload.transaction_hash);
        put_u32(writer, payload.action_sequence);
        put_u32(writer, payload.state_flags);
        put_u32(writer, payload.outcome_flags);
        put_u32(writer, payload.reserved0);
        break;
    }
    case WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1: {
        worr_local_action_shadow_authority_receipt_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_u32(writer, payload.struct_size);
        put_u32(writer, payload.schema_version);
        put_u32(writer, payload.model_revision);
        put_u32(writer, payload.reserved0);
        put_u32(writer, payload.command_id.epoch);
        put_u32(writer, payload.command_id.sequence);
        put_u32(writer, payload.catalog_id);
        put_u32(writer, payload.flags);
        put_u32(writer, payload.v2_blockers);
        put_u32(writer, payload.reserved1);
        put_u64(writer, payload.command_hash);
        put_u64(writer, payload.descriptor_hash);
        put_u64(writer, payload.record_hash);
        break;
    }
    case WORR_EVENT_PAYLOAD_KEYED_POI_V1: {
        worr_event_payload_keyed_poi_v1 payload;
        memcpy(&payload, record->payload, sizeof(payload));
        put_u16(writer, payload.key);
        put_u16(writer, payload.lifetime_ms);
        put_float_array(writer, payload.position, 3);
        put_u16(writer, payload.image_index);
        put_u8(writer, payload.color_index);
        put_u8(writer, payload.flags);
        break;
    }
    default:
        break;
    }
}

static bool read_event_payload(byte_reader *reader,
                               worr_event_record_v1 *record)
{
    uint32_t index;
    switch (record->payload_kind) {
    case WORR_EVENT_PAYLOAD_NONE:
        return true;
    case WORR_EVENT_PAYLOAD_VEC3: {
        worr_event_payload_vec3_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_float_array(reader, payload.value, 3))
            return false;
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_ENTITY_REF: {
        worr_event_payload_entity_ref_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_u32(reader, &payload.entity.index) ||
            !get_u32(reader, &payload.entity.generation)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_DAMAGE: {
        worr_event_payload_damage_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_float(reader, &payload.amount) ||
            !get_float(reader, &payload.impulse) ||
            !get_float_array(reader, payload.direction, 3) ||
            !get_float_array(reader, payload.point, 3) ||
            !get_u32(reader, &payload.damage_flags) ||
            !get_u32(reader, &payload.means_of_death)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_AUDIO: {
        worr_event_payload_audio_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_u32(reader, &payload.asset_id) ||
            !get_u32(reader, &payload.channel) ||
            !get_float(reader, &payload.volume) ||
            !get_float(reader, &payload.attenuation) ||
            !get_float(reader, &payload.pitch)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_EFFECT: {
        worr_event_payload_effect_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_u32(reader, &payload.effect_id) ||
            !get_u32(reader, &payload.variant) ||
            !get_float_array(reader, payload.origin, 3) ||
            !get_float_array(reader, payload.direction, 3)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_U32X4: {
        worr_event_payload_u32x4_v1 payload;
        memset(&payload, 0, sizeof(payload));
        for (index = 0; index < 4; ++index) {
            if (!get_u32(reader, &payload.value[index]))
                return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1: {
        worr_event_payload_legacy_entity_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_u16(reader, &payload.raw_event) ||
            !get_u16(reader, &payload.flags)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1: {
        worr_event_payload_legacy_temp_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_u16(reader, &payload.subtype) ||
            !get_u16(reader, &payload.valid_fields) ||
            !get_i16(reader, &payload.raw_entity1) ||
            !get_i16(reader, &payload.raw_entity2) ||
            !get_i32(reader, &payload.time_ms) ||
            !get_i32(reader, &payload.count_or_amount) ||
            !get_i32(reader, &payload.color) ||
            !get_float_array(reader, payload.position1, 3) ||
            !get_float_array(reader, payload.position2, 3) ||
            !get_float_array(reader, payload.offset, 3) ||
            !get_float_array(reader, payload.direction, 3)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_MUZZLE_V1: {
        worr_event_payload_muzzle_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_u16(reader, &payload.family) ||
            !get_u16(reader, &payload.flash_id) ||
            !get_u32(reader, &payload.flags)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1: {
        worr_event_payload_spatial_audio_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_u32(reader, &payload.asset_id) ||
            !get_u16(reader, &payload.channel) ||
            !get_u16(reader, &payload.flags) ||
            !get_u32(reader, &payload.raw_entity) ||
            !get_float_array(reader, payload.origin, 3) ||
            !get_float(reader, &payload.volume) ||
            !get_float(reader, &payload.attenuation) ||
            !get_float(reader, &payload.time_offset) ||
            !get_float(reader, &payload.pitch)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1: {
        worr_local_interaction_authority_receipt_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_u32(reader, &payload.struct_size) ||
            !get_u32(reader, &payload.schema_version) ||
            !get_u32(reader, &payload.command_id.epoch) ||
            !get_u32(reader, &payload.command_id.sequence) ||
            !get_u64(reader, &payload.command_hash) ||
            !get_u64(reader, &payload.state_hash) ||
            !get_u64(reader, &payload.transaction_hash) ||
            !get_u32(reader, &payload.action_sequence) ||
            !get_u32(reader, &payload.state_flags) ||
            !get_u32(reader, &payload.outcome_flags) ||
            !get_u32(reader, &payload.reserved0)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1: {
        worr_local_action_shadow_authority_receipt_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_u32(reader, &payload.struct_size) ||
            !get_u32(reader, &payload.schema_version) ||
            !get_u32(reader, &payload.model_revision) ||
            !get_u32(reader, &payload.reserved0) ||
            !get_u32(reader, &payload.command_id.epoch) ||
            !get_u32(reader, &payload.command_id.sequence) ||
            !get_u32(reader, &payload.catalog_id) ||
            !get_u32(reader, &payload.flags) ||
            !get_u32(reader, &payload.v2_blockers) ||
            !get_u32(reader, &payload.reserved1) ||
            !get_u64(reader, &payload.command_hash) ||
            !get_u64(reader, &payload.descriptor_hash) ||
            !get_u64(reader, &payload.record_hash)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    case WORR_EVENT_PAYLOAD_KEYED_POI_V1: {
        worr_event_payload_keyed_poi_v1 payload;
        memset(&payload, 0, sizeof(payload));
        if (!get_u16(reader, &payload.key) ||
            !get_u16(reader, &payload.lifetime_ms) ||
            !get_float_array(reader, payload.position, 3) ||
            !get_u16(reader, &payload.image_index) ||
            !get_u8(reader, &payload.color_index) ||
            !get_u8(reader, &payload.flags)) {
            return false;
        }
        memcpy(record->payload, &payload, sizeof(payload));
        return true;
    }
    default:
        return false;
    }
}

static void write_event_body(byte_writer *writer,
                             const worr_event_record_v1 *record)
{
    put_u32(writer, record->flags);
    put_u32(writer, record->source_tick);
    put_u32(writer, record->source_ordinal);
    put_u64(writer, record->source_time_us);
    put_u32(writer, record->source_entity.index);
    put_u32(writer, record->source_entity.generation);
    put_u32(writer, record->subject_entity.index);
    put_u32(writer, record->subject_entity.generation);
    put_u16(writer, record->event_type);
    put_u8(writer, record->delivery_class);
    put_u8(writer, record->prediction_class);
    put_u32(writer, record->prediction_key.command_epoch);
    put_u32(writer, record->prediction_key.command_sequence);
    put_u32(writer, record->prediction_key.emitter_ordinal);
    put_u32(writer, record->prediction_key.lane);
    put_u32(writer, record->expiry_tick);
    put_u16(writer, record->payload_kind);
    put_u16(writer, record->payload_size);
    write_event_payload(writer, record);
}

static bool read_event_body(byte_reader *reader,
                            const worr_native_codec_info_v1 *info,
                            worr_event_record_v1 *record_out)
{
    worr_event_record_v1 record;
    uint32_t payload_wire_bytes;

    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = info->model_revision;
    record.event_id.stream_epoch = info->object_epoch;
    record.event_id.sequence = info->object_sequence;
    if (!get_u32(reader, &record.flags) ||
        !get_u32(reader, &record.source_tick) ||
        !get_u32(reader, &record.source_ordinal) ||
        !get_u64(reader, &record.source_time_us) ||
        !get_u32(reader, &record.source_entity.index) ||
        !get_u32(reader, &record.source_entity.generation) ||
        !get_u32(reader, &record.subject_entity.index) ||
        !get_u32(reader, &record.subject_entity.generation) ||
        !get_u16(reader, &record.event_type) ||
        !get_u8(reader, &record.delivery_class) ||
        !get_u8(reader, &record.prediction_class) ||
        !get_u32(reader, &record.prediction_key.command_epoch) ||
        !get_u32(reader, &record.prediction_key.command_sequence) ||
        !get_u32(reader, &record.prediction_key.emitter_ordinal) ||
        !get_u32(reader, &record.prediction_key.lane) ||
        !get_u32(reader, &record.expiry_tick) ||
        !get_u16(reader, &record.payload_kind) ||
        !get_u16(reader, &record.payload_size)) {
        return false;
    }
    payload_wire_bytes = event_payload_wire_bytes(record.payload_kind);
    if (payload_wire_bytes == UINT32_MAX ||
        payload_wire_bytes != info->range_counts[0] ||
        !read_event_payload(reader, &record)) {
        return false;
    }
    *record_out = record;
    return true;
}

worr_native_codec_result_v1 Worr_NativeCodecEventPreflightV1(
    const worr_event_record_v1 *record,
    uint32_t max_entities,
    uint32_t *encoded_bytes_out)
{
    uint32_t payload_bytes;
    uint32_t encoded_bytes;
    if (record == NULL || encoded_bytes_out == NULL ||
        ranges_overlap(record, sizeof(*record), encoded_bytes_out,
                       sizeof(*encoded_bytes_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    if (!Worr_EventRecordValidateV1(record, max_entities) ||
        (record->flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0) {
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    }
    payload_bytes = event_payload_wire_bytes(record->payload_kind);
    if (payload_bytes == UINT32_MAX)
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    encoded_bytes = WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                    WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES +
                    payload_bytes;
    *encoded_bytes_out = encoded_bytes;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecEventEncodeV1(
    const worr_event_record_v1 *record,
    uint32_t max_entities,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out)
{
    uint32_t encoded_bytes;
    uint32_t payload_bytes;
    byte_writer writer;
    worr_native_codec_result_v1 result;

    if (encoded_out == NULL || encoded_bytes_out == NULL)
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    result = Worr_NativeCodecEventPreflightV1(
        record, max_entities, &encoded_bytes);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    if (encoded_capacity < encoded_bytes)
        return WORR_NATIVE_CODEC_OUTPUT_TOO_SMALL;
    if (ranges_overlap(record, sizeof(*record), encoded_out, encoded_bytes) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       encoded_out, encoded_bytes) ||
        ranges_overlap(record, sizeof(*record), encoded_bytes_out,
                       sizeof(*encoded_bytes_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    payload_bytes = event_payload_wire_bytes(record->payload_kind);
    writer.cursor = (uint8_t *)encoded_out;
    write_header(&writer, WORR_NATIVE_RECORD_EVENT_V1,
                 WORR_EVENT_ABI_VERSION, WORR_EVENT_MODEL_REVISION,
                 encoded_bytes, WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES,
                 payload_bytes, 0, 0, record->event_id.stream_epoch,
                 record->event_id.sequence);
    write_event_body(&writer, record);
    *encoded_bytes_out = encoded_bytes;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecEventDecodeV1(
    const void *encoded,
    size_t encoded_bytes,
    uint32_t max_entities,
    worr_event_record_v1 *record_out)
{
    worr_native_codec_info_v1 info;
    worr_event_record_v1 record;
    byte_reader reader;
    worr_native_codec_result_v1 result;

    if (record_out == NULL ||
        ranges_overlap(encoded, encoded_bytes, record_out,
                       sizeof(*record_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    result = Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    if (info.record_class != WORR_NATIVE_RECORD_EVENT_V1)
        return WORR_NATIVE_CODEC_UNSUPPORTED;
    reader.cursor = (const uint8_t *)encoded +
                    WORR_NATIVE_CODEC_WIRE_HEADER_BYTES;
    reader.end = (const uint8_t *)encoded + encoded_bytes;
    if (!read_event_body(&reader, &info, &record) ||
        reader.cursor != reader.end) {
        return WORR_NATIVE_CODEC_MALFORMED;
    }
    if (!Worr_EventRecordValidateV1(&record, max_entities) ||
        (record.flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0) {
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    }
    *record_out = record;
    return WORR_NATIVE_CODEC_OK;
}

static const uint64_t snapshot_fnv_offset =
    UINT64_C(14695981039346656037);
static const uint64_t snapshot_fnv_prime = UINT64_C(1099511628211);

static uint64_t snapshot_hash_u8(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * snapshot_fnv_prime;
}

static uint64_t snapshot_hash_u16(uint64_t hash, uint16_t value)
{
    hash = snapshot_hash_u8(hash, (uint8_t)value);
    return snapshot_hash_u8(hash, (uint8_t)(value >> 8));
}

static uint64_t snapshot_hash_u32(uint64_t hash, uint32_t value)
{
    unsigned int index;
    for (index = 0; index < 4; ++index)
        hash = snapshot_hash_u8(hash, (uint8_t)(value >> (index * 8u)));
    return hash;
}

static uint64_t snapshot_hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int index;
    for (index = 0; index < 8; ++index)
        hash = snapshot_hash_u8(hash, (uint8_t)(value >> (index * 8u)));
    return hash;
}

static uint64_t snapshot_begin_hash(uint32_t domain)
{
    uint64_t hash = snapshot_fnv_offset;
    hash = snapshot_hash_u32(hash, UINT32_C(0x574f5252));
    return snapshot_hash_u32(hash, domain);
}

static uint64_t snapshot_hash_float(uint64_t hash, float value)
{
    uint32_t bits;

    if (value == 0.0f)
        bits = 0;
    else
        memcpy(&bits, &value, sizeof(bits));
    return snapshot_hash_u32(hash, bits);
}

static uint64_t snapshot_hash_floats(
    uint64_t hash, const float *values, uint32_t count)
{
    uint32_t index;

    for (index = 0; index < count; ++index)
        hash = snapshot_hash_float(hash, values[index]);
    return hash;
}

static uint64_t snapshot_hash_identity(
    uint64_t hash, worr_event_entity_ref_v1 identity)
{
    hash = snapshot_hash_u32(hash, identity.index);
    return snapshot_hash_u32(hash, identity.generation);
}

static uint64_t codec_semantic_player_hash(
    const worr_snapshot_player_v2 *player)
{
    const worr_prediction_state_v1 *movement = &player->movement;
    uint64_t hash =
        snapshot_begin_hash(UINT32_C(0x53504c32)); /* SPL2 */
    uint32_t index;

    hash = snapshot_hash_u32(hash, WORR_SNAPSHOT_MODEL_REVISION);
    hash = snapshot_hash_identity(
        hash, player->controlled_entity.identity);
    hash = snapshot_hash_u64(hash, player->component_mask);
    hash = snapshot_hash_u32(
        hash, (uint32_t)movement->movement_type);
    hash = snapshot_hash_floats(hash, movement->origin, 3);
    hash = snapshot_hash_floats(hash, movement->velocity, 3);
    hash = snapshot_hash_u16(hash, movement->movement_flags);
    hash = snapshot_hash_u16(hash, movement->movement_time_ms);
    hash = snapshot_hash_u16(hash, (uint16_t)movement->gravity);
    hash = snapshot_hash_u8(hash, (uint8_t)movement->view_height);
    hash = snapshot_hash_floats(hash, movement->delta_angles, 3);
    hash = snapshot_hash_floats(hash, player->view_angles, 3);
    hash = snapshot_hash_floats(hash, player->view_offset, 3);
    hash = snapshot_hash_floats(hash, player->kick_angles, 3);
    hash = snapshot_hash_floats(hash, player->gun_angles, 3);
    hash = snapshot_hash_floats(hash, player->gun_offset, 3);
    hash = snapshot_hash_floats(hash, player->screen_blend, 4);
    hash = snapshot_hash_floats(hash, player->damage_blend, 4);
    hash = snapshot_hash_u16(hash, player->gun_index);
    hash = snapshot_hash_u16(hash, player->gun_frame);
    hash = snapshot_hash_u8(hash, player->gun_skin);
    hash = snapshot_hash_u8(hash, player->gun_rate);
    hash = snapshot_hash_u8(hash, player->rdflags);
    hash = snapshot_hash_u8(hash, player->team_id);
    hash = snapshot_hash_float(hash, player->fov);
    for (index = 0; index < WORR_SNAPSHOT_STATS_CAPACITY; ++index)
        hash = snapshot_hash_u16(
            hash, (uint16_t)player->stats[index]);
    return hash;
}

static uint64_t codec_semantic_entity_hash(
    const worr_snapshot_entity_v2 *entity)
{
    uint64_t hash =
        snapshot_begin_hash(UINT32_C(0x53454e32)); /* SEN2 */
    uint32_t index;

    hash = snapshot_hash_u32(hash, WORR_SNAPSHOT_MODEL_REVISION);
    hash = snapshot_hash_identity(
        hash, entity->generation.identity);
    hash = snapshot_hash_u64(hash, entity->component_mask);
    hash = snapshot_hash_floats(hash, entity->origin, 3);
    hash = snapshot_hash_floats(hash, entity->angles, 3);
    hash = snapshot_hash_floats(hash, entity->old_origin, 3);
    for (index = 0; index < 4; ++index)
        hash = snapshot_hash_u16(hash, entity->model_index[index]);
    hash = snapshot_hash_u16(hash, entity->frame);
    hash = snapshot_hash_u16(hash, entity->sound);
    hash = snapshot_hash_u32(hash, entity->skin);
    hash = snapshot_hash_u32(hash, entity->solid);
    hash = snapshot_hash_u64(hash, entity->effects);
    hash = snapshot_hash_u32(hash, entity->renderfx);
    hash = snapshot_hash_float(hash, entity->alpha);
    hash = snapshot_hash_float(hash, entity->scale);
    hash = snapshot_hash_float(hash, entity->loop_volume);
    hash = snapshot_hash_float(hash, entity->loop_attenuation);
    hash = snapshot_hash_identity(hash, entity->owner);
    hash = snapshot_hash_u32(hash, (uint32_t)entity->old_frame);
    return snapshot_hash_u8(hash, entity->instance_bits);
}

static uint64_t codec_semantic_event_list_hash(
    const worr_snapshot_event_ref_v2 *events, uint32_t count)
{
    uint64_t hash =
        snapshot_begin_hash(UINT32_C(0x53455632)); /* SEV2 */
    uint32_t index;

    hash = snapshot_hash_u32(hash, count);
    for (index = 0; index < count; ++index) {
        hash = snapshot_hash_u32(
            hash, events[index].carrier_ordinal);
        hash = snapshot_hash_u32(
            hash, events[index].semantic_version);
        hash = snapshot_hash_u64(
            hash, events[index].semantic_hash);
    }
    return hash;
}

static bool generation_equal(worr_snapshot_entity_generation_v2 left,
                             worr_snapshot_entity_generation_v2 right)
{
    return left.identity.index == right.identity.index &&
           left.identity.generation == right.identity.generation &&
           left.provenance_flags == right.provenance_flags &&
           left.reserved0 == right.reserved0;
}

static uint32_t primary_generation_source(
    worr_snapshot_entity_generation_v2 generation)
{
    return generation.provenance_flags &
           (WORR_SNAPSHOT_GENERATION_AUTHORITATIVE |
            WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED);
}

static bool event_id_equal(worr_event_id_v1 left,
                           worr_event_id_v1 right)
{
    return left.stream_epoch == right.stream_epoch &&
           left.sequence == right.sequence;
}

static uint32_t snapshot_entity_wire_bytes(uint64_t component_mask)
{
    uint32_t bytes = SNAPSHOT_ENTITY_BASE_BYTES;
    if ((component_mask &
         ~(uint64_t)WORR_SNAPSHOT_ENTITY_COMPONENTS_V2) != 0 ||
        (component_mask & WORR_SNAPSHOT_ENTITY_TRANSFORM) == 0) {
        return UINT32_MAX;
    }
    if ((component_mask & WORR_SNAPSHOT_ENTITY_TRANSFORM) != 0)
        bytes += 24;
    if ((component_mask & WORR_SNAPSHOT_ENTITY_INTERPOLATION) != 0)
        bytes += 16;
    if ((component_mask & WORR_SNAPSHOT_ENTITY_MODELS) != 0)
        bytes += 8;
    if ((component_mask & WORR_SNAPSHOT_ENTITY_ANIMATION) != 0)
        bytes += 2;
    if ((component_mask & WORR_SNAPSHOT_ENTITY_APPEARANCE) != 0)
        bytes += 12;
    if ((component_mask & WORR_SNAPSHOT_ENTITY_EFFECTS) != 0)
        bytes += 12;
    if ((component_mask & WORR_SNAPSHOT_ENTITY_COLLISION) != 0)
        bytes += 4;
    if ((component_mask & WORR_SNAPSHOT_ENTITY_LOOP_SOUND) != 0)
        bytes += 10;
    if ((component_mask & WORR_SNAPSHOT_ENTITY_OWNER) != 0)
        bytes += 8;
    if ((component_mask & WORR_SNAPSHOT_ENTITY_INSTANCE) != 0)
        bytes += 1;
    return bytes;
}

static bool snapshot_event_range_matches(
    const worr_snapshot_event_range_v2 *range,
    const worr_snapshot_event_ref_v2 *events,
    uint32_t count)
{
    worr_snapshot_event_range_v2 expected;
    uint32_t index;
    bool contiguous = true;

    memset(&expected, 0, sizeof(expected));
    if (count != 0) {
        if (events == NULL)
            return false;
        expected.first_ref_serial = range->first_ref_serial;
        expected.count = count;
        expected.provenance = events[0].provenance;
        expected.flags = WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER;
        expected.first_carrier_ordinal = events[0].carrier_ordinal;
        if (expected.provenance ==
            WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY) {
            expected.first_authority_id = events[0].authority_id;
            if (!Worr_EventIdNextV1(events[count - 1].authority_id,
                                    &expected.one_past_authority_id)) {
                return false;
            }
            for (index = 1; index < count; ++index) {
                worr_event_id_v1 next;
                if (!Worr_EventIdNextV1(events[index - 1].authority_id,
                                        &next) ||
                    !event_id_equal(next, events[index].authority_id)) {
                    contiguous = false;
                }
            }
            if (contiguous) {
                expected.flags |=
                    WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY;
            }
        }
    }
    return range->count == expected.count &&
           range->provenance == expected.provenance &&
           range->flags == expected.flags &&
           event_id_equal(range->first_authority_id,
                          expected.first_authority_id) &&
           event_id_equal(range->one_past_authority_id,
                          expected.one_past_authority_id) &&
           range->first_carrier_ordinal ==
               expected.first_carrier_ordinal &&
           range->reserved0 == 0;
}

uint32_t Worr_NativeCodecSnapshotTransportFailureFlagsV1(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t max_entities)
{
    worr_snapshot_projection_hashes_v2 hashes;
    bool authoritative;
    uint32_t expected_source;
    uint32_t failures = 0;
    uint32_t index;

    if (view == NULL || max_entities == 0 ||
        view->snapshot == NULL || view->player == NULL ||
        (view->entity_count != 0 && view->entities == NULL) ||
        (view->area_byte_count != 0 && view->area_bytes == NULL) ||
        (view->event_ref_count != 0 && view->event_refs == NULL)) {
        return WORR_NATIVE_SNAPSHOT_TRANSPORT_INVALID_VIEW;
    }
    authoritative =
        (view->snapshot->flags &
         WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS) != 0;
    expected_source =
        authoritative ? WORR_SNAPSHOT_GENERATION_AUTHORITATIVE
                      : WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    if (!Worr_SnapshotProjectionHashesV2(view, max_entities, &hashes))
        failures |= WORR_NATIVE_SNAPSHOT_TRANSPORT_INVALID_PROJECTION;
    if (view->entity_count > WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES ||
        view->area_byte_count >
            WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES ||
        view->event_ref_count >
            WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS)
        failures |= WORR_NATIVE_SNAPSHOT_TRANSPORT_LIMIT;
    if (!generation_equal(view->snapshot->controlled_entity,
                          view->player->controlled_entity))
        failures |=
            WORR_NATIVE_SNAPSHOT_TRANSPORT_CONTROLLED_PLAYER_MISMATCH;
    if (primary_generation_source(view->player->controlled_entity) !=
        expected_source)
        failures |= WORR_NATIVE_SNAPSHOT_TRANSPORT_PLAYER_SOURCE_MISMATCH;
    if (!snapshot_event_range_matches(&view->snapshot->event_range,
                                      view->event_refs,
                                      view->event_ref_count))
        failures |= WORR_NATIVE_SNAPSHOT_TRANSPORT_EVENT_RANGE_MISMATCH;
    if ((failures & WORR_NATIVE_SNAPSHOT_TRANSPORT_LIMIT) == 0) {
        for (index = 0; index < view->entity_count; ++index) {
            if (primary_generation_source(
                    view->entities[index].generation) != expected_source)
                failures |=
                    WORR_NATIVE_SNAPSHOT_TRANSPORT_ENTITY_SOURCE_MISMATCH;
            if (view->entities[index].generation.identity.index ==
                    view->snapshot->controlled_entity.identity.index &&
                !generation_equal(view->entities[index].generation,
                                  view->snapshot->controlled_entity))
                failures |=
                    WORR_NATIVE_SNAPSHOT_TRANSPORT_CONTROLLED_ENTITY_MISMATCH;
        }
    }
    return failures;
}

static bool snapshot_encoded_size(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t *encoded_bytes_out)
{
    uint64_t total = WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                     WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES;
    uint32_t index;

    for (index = 0; index < view->entity_count; ++index) {
        const uint32_t entity_bytes = snapshot_entity_wire_bytes(
            view->entities[index].component_mask);
        if (entity_bytes == UINT32_MAX)
            return false;
        total += entity_bytes;
    }
    total += view->area_byte_count;
    total += (uint64_t)view->event_ref_count *
             WORR_NATIVE_CODEC_SNAPSHOT_EVENT_REF_BYTES;
    if (total > WORR_NATIVE_CODEC_MAX_ENCODED_BYTES)
        return false;
    *encoded_bytes_out = (uint32_t)total;
    return true;
}

static void put_generation(byte_writer *writer,
                           worr_snapshot_entity_generation_v2 generation)
{
    put_u32(writer, generation.identity.index);
    put_u32(writer, generation.identity.generation);
    put_u32(writer, generation.provenance_flags);
}

static bool get_generation(byte_reader *reader,
                           worr_snapshot_entity_generation_v2 *generation)
{
    return get_u32(reader, &generation->identity.index) &&
           get_u32(reader, &generation->identity.generation) &&
           get_u32(reader, &generation->provenance_flags);
}

static void write_snapshot_player(byte_writer *writer,
                                  const worr_snapshot_player_v2 *player)
{
    uint32_t index;
    put_u32(writer, player->flags);
    put_generation(writer, player->controlled_entity);
    put_u64(writer, player->component_mask);
    put_u32(writer, (uint32_t)player->movement.movement_type);
    put_float_array(writer, player->movement.origin, 3);
    put_float_array(writer, player->movement.velocity, 3);
    put_u16(writer, player->movement.movement_flags);
    put_u16(writer, player->movement.movement_time_ms);
    put_u16(writer, (uint16_t)player->movement.gravity);
    put_u8(writer, (uint8_t)player->movement.view_height);
    put_float_array(writer, player->movement.delta_angles, 3);
    put_float_array(writer, player->view_angles, 3);
    put_float_array(writer, player->view_offset, 3);
    put_float_array(writer, player->kick_angles, 3);
    put_float_array(writer, player->gun_angles, 3);
    put_float_array(writer, player->gun_offset, 3);
    put_float_array(writer, player->screen_blend, 4);
    put_float_array(writer, player->damage_blend, 4);
    put_u16(writer, player->gun_index);
    put_u16(writer, player->gun_frame);
    put_u8(writer, player->gun_skin);
    put_u8(writer, player->gun_rate);
    put_u8(writer, player->rdflags);
    put_u8(writer, player->team_id);
    put_float(writer, player->fov);
    for (index = 0; index < WORR_SNAPSHOT_STATS_CAPACITY; ++index)
        put_u16(writer, (uint16_t)player->stats[index]);
}

static bool read_snapshot_player(byte_reader *reader,
                                 worr_snapshot_player_v2 *player_out)
{
    worr_snapshot_player_v2 player;
    uint32_t movement_type;
    uint8_t view_height;
    uint32_t index;

    memset(&player, 0, sizeof(player));
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    if (!get_u32(reader, &player.flags) ||
        !get_generation(reader, &player.controlled_entity) ||
        !get_u64(reader, &player.component_mask) ||
        !get_u32(reader, &movement_type) ||
        !get_float_array(reader, player.movement.origin, 3) ||
        !get_float_array(reader, player.movement.velocity, 3) ||
        !get_u16(reader, &player.movement.movement_flags) ||
        !get_u16(reader, &player.movement.movement_time_ms) ||
        !get_i16(reader, &player.movement.gravity) ||
        !get_u8(reader, &view_height) ||
        !get_float_array(reader, player.movement.delta_angles, 3) ||
        !get_float_array(reader, player.view_angles, 3) ||
        !get_float_array(reader, player.view_offset, 3) ||
        !get_float_array(reader, player.kick_angles, 3) ||
        !get_float_array(reader, player.gun_angles, 3) ||
        !get_float_array(reader, player.gun_offset, 3) ||
        !get_float_array(reader, player.screen_blend, 4) ||
        !get_float_array(reader, player.damage_blend, 4) ||
        !get_u16(reader, &player.gun_index) ||
        !get_u16(reader, &player.gun_frame) ||
        !get_u8(reader, &player.gun_skin) ||
        !get_u8(reader, &player.gun_rate) ||
        !get_u8(reader, &player.rdflags) ||
        !get_u8(reader, &player.team_id) ||
        !get_float(reader, &player.fov)) {
        return false;
    }
    player.movement.movement_type = (int32_t)movement_type;
    player.movement.view_height = (int8_t)view_height;
    for (index = 0; index < WORR_SNAPSHOT_STATS_CAPACITY; ++index) {
        if (!get_i16(reader, &player.stats[index]))
            return false;
    }
    *player_out = player;
    return true;
}

static void write_snapshot_fixed(byte_writer *writer,
                                 const worr_snapshot_projection_view_v2 *view)
{
    const worr_snapshot_v2 *snapshot = view->snapshot;
    put_u32(writer, snapshot->flags);
    put_u32(writer, snapshot->base_id.epoch);
    put_u32(writer, snapshot->base_id.sequence);
    put_u32(writer, snapshot->server_tick);
    put_u64(writer, snapshot->server_time_us);
    put_generation(writer, snapshot->controlled_entity);
    put_u32(writer, snapshot->consumed_command.cursor.epoch);
    put_u32(writer,
            snapshot->consumed_command.cursor.contiguous_sequence);
    put_u32(writer, snapshot->consumed_command.provenance);
    put_u32(writer, snapshot->discontinuity.flags);
    put_u16(writer, snapshot->discontinuity.reason);
    put_u32(writer, snapshot->discontinuity.previous.epoch);
    put_u32(writer, snapshot->discontinuity.previous.sequence);
    put_u32(writer, snapshot->discontinuity.server_tick_delta);
    put_u32(writer, snapshot->discontinuity.skipped_sequences);
    put_u16(writer, snapshot->event_range.provenance);
    put_u16(writer, snapshot->event_range.flags);
    put_u32(writer, snapshot->event_range.first_authority_id.stream_epoch);
    put_u32(writer, snapshot->event_range.first_authority_id.sequence);
    put_u32(writer,
            snapshot->event_range.one_past_authority_id.stream_epoch);
    put_u32(writer, snapshot->event_range.one_past_authority_id.sequence);
    put_u32(writer, snapshot->event_range.first_carrier_ordinal);
    put_u64(writer, snapshot->player_hash);
    put_u64(writer, snapshot->entity_hash);
    put_u64(writer, snapshot->area_hash);
    put_u64(writer, snapshot->event_hash);
    put_u64(writer, snapshot->snapshot_hash);
    write_snapshot_player(writer, view->player);
}

static bool read_snapshot_fixed(byte_reader *reader,
                                const worr_native_codec_info_v1 *info,
                                worr_snapshot_v2 *snapshot_out,
                                worr_snapshot_player_v2 *player_out)
{
    worr_snapshot_v2 snapshot;
    worr_snapshot_player_v2 player;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = info->model_revision;
    snapshot.snapshot_id.epoch = info->object_epoch;
    snapshot.snapshot_id.sequence = info->object_sequence;
    if (!get_u32(reader, &snapshot.flags) ||
        !get_u32(reader, &snapshot.base_id.epoch) ||
        !get_u32(reader, &snapshot.base_id.sequence) ||
        !get_u32(reader, &snapshot.server_tick) ||
        !get_u64(reader, &snapshot.server_time_us) ||
        !get_generation(reader, &snapshot.controlled_entity) ||
        !get_u32(reader, &snapshot.consumed_command.cursor.epoch) ||
        !get_u32(reader,
                 &snapshot.consumed_command.cursor.contiguous_sequence) ||
        !get_u32(reader, &snapshot.consumed_command.provenance) ||
        !get_u32(reader, &snapshot.discontinuity.flags) ||
        !get_u16(reader, &snapshot.discontinuity.reason) ||
        !get_u32(reader, &snapshot.discontinuity.previous.epoch) ||
        !get_u32(reader, &snapshot.discontinuity.previous.sequence) ||
        !get_u32(reader, &snapshot.discontinuity.server_tick_delta) ||
        !get_u32(reader, &snapshot.discontinuity.skipped_sequences) ||
        !get_u16(reader, &snapshot.event_range.provenance) ||
        !get_u16(reader, &snapshot.event_range.flags) ||
        !get_u32(reader,
                 &snapshot.event_range.first_authority_id.stream_epoch) ||
        !get_u32(reader,
                 &snapshot.event_range.first_authority_id.sequence) ||
        !get_u32(reader,
                 &snapshot.event_range.one_past_authority_id.stream_epoch) ||
        !get_u32(reader,
                 &snapshot.event_range.one_past_authority_id.sequence) ||
        !get_u32(reader, &snapshot.event_range.first_carrier_ordinal) ||
        !get_u64(reader, &snapshot.player_hash) ||
        !get_u64(reader, &snapshot.entity_hash) ||
        !get_u64(reader, &snapshot.area_hash) ||
        !get_u64(reader, &snapshot.event_hash) ||
        !get_u64(reader, &snapshot.snapshot_hash) ||
        !read_snapshot_player(reader, &player)) {
        return false;
    }
    snapshot.entity_range.count = info->range_counts[0];
    snapshot.entity_range.first_serial =
        info->range_counts[0] == 0 ? 0 : 1;
    snapshot.area_range.count = info->range_counts[1];
    snapshot.area_range.first_serial =
        info->range_counts[1] == 0 ? 0 : 1;
    snapshot.event_range.count = info->range_counts[2];
    snapshot.event_range.first_ref_serial =
        info->range_counts[2] == 0 ? 0 : 1;
    *snapshot_out = snapshot;
    *player_out = player;
    return true;
}

static void write_snapshot_entity(byte_writer *writer,
                                  const worr_snapshot_entity_v2 *entity)
{
    const uint64_t mask = entity->component_mask;
    uint32_t index;
    put_u16(writer, (uint16_t)snapshot_entity_wire_bytes(mask));
    put_u16(writer, 0);
    put_u32(writer, entity->flags);
    put_generation(writer, entity->generation);
    put_u64(writer, mask);
    put_float_array(writer, entity->origin, 3);
    put_float_array(writer, entity->angles, 3);
    if ((mask & WORR_SNAPSHOT_ENTITY_INTERPOLATION) != 0) {
        put_float_array(writer, entity->old_origin, 3);
        put_u32(writer, (uint32_t)entity->old_frame);
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_MODELS) != 0) {
        for (index = 0; index < 4; ++index)
            put_u16(writer, entity->model_index[index]);
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_ANIMATION) != 0)
        put_u16(writer, entity->frame);
    if ((mask & WORR_SNAPSHOT_ENTITY_APPEARANCE) != 0) {
        put_u32(writer, entity->skin);
        put_float(writer, entity->alpha);
        put_float(writer, entity->scale);
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_EFFECTS) != 0) {
        put_u64(writer, entity->effects);
        put_u32(writer, entity->renderfx);
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_COLLISION) != 0)
        put_u32(writer, entity->solid);
    if ((mask & WORR_SNAPSHOT_ENTITY_LOOP_SOUND) != 0) {
        put_u16(writer, entity->sound);
        put_float(writer, entity->loop_volume);
        put_float(writer, entity->loop_attenuation);
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_OWNER) != 0) {
        put_u32(writer, entity->owner.index);
        put_u32(writer, entity->owner.generation);
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_INSTANCE) != 0)
        put_u8(writer, entity->instance_bits);
}

static bool read_snapshot_entity(byte_reader *reader,
                                 worr_snapshot_entity_v2 *entity_out)
{
    const uint8_t *item_begin = reader->cursor;
    byte_reader item;
    worr_snapshot_entity_v2 entity;
    uint16_t item_bytes;
    uint16_t reserved;
    uint32_t expected_bytes;
    uint32_t index;

    if (!get_u16(reader, &item_bytes) || !get_u16(reader, &reserved) ||
        reserved != 0 ||
        item_bytes < WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MIN_BYTES ||
        item_bytes > WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MAX_BYTES ||
        (size_t)(reader->end - item_begin) < item_bytes) {
        return false;
    }
    item.cursor = reader->cursor;
    item.end = item_begin + item_bytes;
    memset(&entity, 0, sizeof(entity));
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.owner.index = WORR_EVENT_NO_ENTITY;
    if (!get_u32(&item, &entity.flags) ||
        !get_generation(&item, &entity.generation) ||
        !get_u64(&item, &entity.component_mask)) {
        return false;
    }
    expected_bytes = snapshot_entity_wire_bytes(entity.component_mask);
    if (expected_bytes != item_bytes ||
        !get_float_array(&item, entity.origin, 3) ||
        !get_float_array(&item, entity.angles, 3)) {
        return false;
    }
    if ((entity.component_mask &
         WORR_SNAPSHOT_ENTITY_INTERPOLATION) != 0) {
        if (!get_float_array(&item, entity.old_origin, 3) ||
            !get_i32(&item, &entity.old_frame)) {
            return false;
        }
    }
    if ((entity.component_mask & WORR_SNAPSHOT_ENTITY_MODELS) != 0) {
        for (index = 0; index < 4; ++index) {
            if (!get_u16(&item, &entity.model_index[index]))
                return false;
        }
    }
    if ((entity.component_mask & WORR_SNAPSHOT_ENTITY_ANIMATION) != 0 &&
        !get_u16(&item, &entity.frame)) {
        return false;
    }
    if ((entity.component_mask & WORR_SNAPSHOT_ENTITY_APPEARANCE) != 0 &&
        (!get_u32(&item, &entity.skin) ||
         !get_float(&item, &entity.alpha) ||
         !get_float(&item, &entity.scale))) {
        return false;
    }
    if ((entity.component_mask & WORR_SNAPSHOT_ENTITY_EFFECTS) != 0 &&
        (!get_u64(&item, &entity.effects) ||
         !get_u32(&item, &entity.renderfx))) {
        return false;
    }
    if ((entity.component_mask & WORR_SNAPSHOT_ENTITY_COLLISION) != 0 &&
        !get_u32(&item, &entity.solid)) {
        return false;
    }
    if ((entity.component_mask & WORR_SNAPSHOT_ENTITY_LOOP_SOUND) != 0 &&
        (!get_u16(&item, &entity.sound) ||
         !get_float(&item, &entity.loop_volume) ||
         !get_float(&item, &entity.loop_attenuation))) {
        return false;
    }
    if ((entity.component_mask & WORR_SNAPSHOT_ENTITY_OWNER) != 0 &&
        (!get_u32(&item, &entity.owner.index) ||
         !get_u32(&item, &entity.owner.generation))) {
        return false;
    }
    if ((entity.component_mask & WORR_SNAPSHOT_ENTITY_INSTANCE) != 0 &&
        !get_u8(&item, &entity.instance_bits)) {
        return false;
    }
    if (item.cursor != item.end)
        return false;
    reader->cursor = item.end;
    *entity_out = entity;
    return true;
}

static void write_snapshot_event_ref(
    byte_writer *writer,
    const worr_snapshot_event_ref_v2 *event_ref)
{
    put_u16(writer, WORR_NATIVE_CODEC_SNAPSHOT_EVENT_REF_BYTES);
    put_u16(writer, 0);
    put_u16(writer, event_ref->provenance);
    put_u32(writer, event_ref->carrier_ordinal);
    put_u32(writer, event_ref->semantic_version);
    put_u32(writer, event_ref->authority_id.stream_epoch);
    put_u32(writer, event_ref->authority_id.sequence);
    put_u64(writer, event_ref->semantic_hash);
}

static bool read_snapshot_event_ref(
    byte_reader *reader,
    worr_snapshot_event_ref_v2 *event_ref_out)
{
    worr_snapshot_event_ref_v2 event_ref;
    uint16_t item_bytes;
    uint16_t reserved;

    memset(&event_ref, 0, sizeof(event_ref));
    event_ref.struct_size = sizeof(event_ref);
    event_ref.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    if (!get_u16(reader, &item_bytes) || !get_u16(reader, &reserved) ||
        item_bytes != WORR_NATIVE_CODEC_SNAPSHOT_EVENT_REF_BYTES ||
        reserved != 0 || !get_u16(reader, &event_ref.provenance) ||
        !get_u32(reader, &event_ref.carrier_ordinal) ||
        !get_u32(reader, &event_ref.semantic_version) ||
        !get_u32(reader, &event_ref.authority_id.stream_epoch) ||
        !get_u32(reader, &event_ref.authority_id.sequence) ||
        !get_u64(reader, &event_ref.semantic_hash)) {
        return false;
    }
    *event_ref_out = event_ref;
    return true;
}

typedef struct snapshot_decode_validation_s {
    worr_snapshot_v2 snapshot;
    worr_snapshot_player_v2 player;
    worr_snapshot_projection_hashes_v2 hashes;
    uint32_t entity_offset;
    uint32_t area_offset;
    uint32_t event_offset;
} snapshot_decode_validation;

static worr_native_codec_result_v1 validate_snapshot_wire(
    const void *encoded,
    size_t encoded_bytes,
    const worr_native_codec_info_v1 *info,
    uint32_t max_entities,
    snapshot_decode_validation *validation_out)
{
    worr_snapshot_event_ref_v2
        event_refs[WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS];
    snapshot_decode_validation validation;
    worr_snapshot_entity_v2 entity;
    worr_snapshot_entity_generation_v2 previous_generation;
    byte_reader reader;
    uint64_t entity_list_hash;
    uint64_t semantic_entity_list_hash;
    uint64_t entity_hash;
    uint64_t player_hash;
    uint64_t area_hash;
    uint64_t event_hash;
    uint64_t snapshot_hash;
    uint64_t hash;
    uint32_t parity_flags;
    uint32_t expected_source;
    uint32_t index;

    if (max_entities == 0)
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    memset(&validation, 0, sizeof(validation));
    memset(&previous_generation, 0, sizeof(previous_generation));
    reader.cursor = (const uint8_t *)encoded +
                    WORR_NATIVE_CODEC_WIRE_HEADER_BYTES;
    reader.end = (const uint8_t *)encoded + encoded_bytes;
    if (!read_snapshot_fixed(&reader, info, &validation.snapshot,
                             &validation.player) ||
        (size_t)(reader.cursor - (const uint8_t *)encoded) !=
            WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES) {
        return WORR_NATIVE_CODEC_MALFORMED;
    }
    if (!Worr_SnapshotPlayerHashV2(&validation.player, max_entities,
                                   &player_hash))
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    if (player_hash != validation.snapshot.player_hash)
        return WORR_NATIVE_CODEC_CORRUPT;
    if (!generation_equal(validation.snapshot.controlled_entity,
                          validation.player.controlled_entity)) {
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    }
    expected_source =
        (validation.snapshot.flags &
         WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS) != 0
            ? WORR_SNAPSHOT_GENERATION_AUTHORITATIVE
            : WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    if (primary_generation_source(validation.player.controlled_entity) !=
        expected_source) {
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    }

    validation.entity_offset =
        (uint32_t)(reader.cursor - (const uint8_t *)encoded);
    entity_list_hash = snapshot_begin_hash(UINT32_C(0x454c5332));
    entity_list_hash =
        snapshot_hash_u32(entity_list_hash, info->range_counts[0]);
    semantic_entity_list_hash =
        snapshot_begin_hash(UINT32_C(0x534c5332)); /* SLS2 */
    semantic_entity_list_hash = snapshot_hash_u32(
        semantic_entity_list_hash, info->range_counts[0]);
    for (index = 0; index < info->range_counts[0]; ++index) {
        if (!read_snapshot_entity(&reader, &entity))
            return WORR_NATIVE_CODEC_MALFORMED;
        if (!Worr_SnapshotEntityHashV2(&entity, max_entities,
                                       &entity_hash) ||
            primary_generation_source(entity.generation) !=
                expected_source ||
            (index != 0 &&
             previous_generation.identity.index >=
                 entity.generation.identity.index) ||
            (entity.generation.identity.index ==
                 validation.snapshot.controlled_entity.identity.index &&
             !generation_equal(entity.generation,
                               validation.snapshot.controlled_entity))) {
            return WORR_NATIVE_CODEC_INVALID_RECORD;
        }
        entity_list_hash =
            snapshot_hash_u64(entity_list_hash, entity_hash);
        semantic_entity_list_hash = snapshot_hash_u64(
            semantic_entity_list_hash,
            codec_semantic_entity_hash(&entity));
        previous_generation = entity.generation;
    }
    if (entity_list_hash != validation.snapshot.entity_hash)
        return WORR_NATIVE_CODEC_CORRUPT;

    validation.area_offset =
        (uint32_t)(reader.cursor - (const uint8_t *)encoded);
    if ((size_t)(reader.end - reader.cursor) < info->range_counts[1])
        return WORR_NATIVE_CODEC_MALFORMED;
    if (!Worr_SnapshotAreaHashV2(reader.cursor, info->range_counts[1],
                                 &area_hash))
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    if (area_hash != validation.snapshot.area_hash)
        return WORR_NATIVE_CODEC_CORRUPT;
    reader.cursor += info->range_counts[1];
    validation.event_offset =
        (uint32_t)(reader.cursor - (const uint8_t *)encoded);
    for (index = 0; index < info->range_counts[2]; ++index) {
        if (!read_snapshot_event_ref(&reader, &event_refs[index]))
            return WORR_NATIVE_CODEC_MALFORMED;
    }
    if (reader.cursor != reader.end)
        return WORR_NATIVE_CODEC_MALFORMED;
    if (!Worr_SnapshotEventRefsHashV2(event_refs, info->range_counts[2],
                                      &event_hash) ||
        !snapshot_event_range_matches(&validation.snapshot.event_range,
                                      event_refs,
                                      info->range_counts[2])) {
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    }
    if (event_hash != validation.snapshot.event_hash)
        return WORR_NATIVE_CODEC_CORRUPT;
    if (!Worr_SnapshotCalculateHashV2(&validation.snapshot, max_entities,
                                      &snapshot_hash)) {
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    }
    if (snapshot_hash != validation.snapshot.snapshot_hash)
        return WORR_NATIVE_CODEC_CORRUPT;

    validation.hashes.struct_size = sizeof(validation.hashes);
    validation.hashes.schema_version =
        WORR_SNAPSHOT_PROJECTION_VERSION;
    validation.hashes.semantic_player_hash =
        codec_semantic_player_hash(&validation.player);
    validation.hashes.semantic_entity_hash =
        semantic_entity_list_hash;
    validation.hashes.semantic_area_hash = area_hash;
    validation.hashes.semantic_event_hash =
        codec_semantic_event_list_hash(
            event_refs, info->range_counts[2]);

    hash = snapshot_begin_hash(UINT32_C(0x45504832)); /* EPH2 */
    hash = snapshot_hash_u64(
        hash, validation.snapshot.snapshot_hash);
    hash = snapshot_hash_u64(hash, player_hash);
    hash = snapshot_hash_u64(hash, entity_list_hash);
    hash = snapshot_hash_u64(hash, area_hash);
    hash = snapshot_hash_u64(hash, event_hash);
    validation.hashes.endpoint_hash = hash;

    parity_flags = validation.snapshot.flags &
        (WORR_SNAPSHOT_FLAG_KEYFRAME |
         WORR_SNAPSHOT_FLAG_COMPLETE |
         WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION);
    hash = snapshot_begin_hash(UINT32_C(0x50525932)); /* PRY2 */
    hash = snapshot_hash_u32(hash, WORR_SNAPSHOT_ABI_VERSION);
    hash = snapshot_hash_u32(hash, WORR_SNAPSHOT_MODEL_REVISION);
    hash = snapshot_hash_u32(hash, parity_flags);
    hash = snapshot_hash_u32(
        hash, validation.snapshot.snapshot_id.epoch);
    hash = snapshot_hash_u32(
        hash, validation.snapshot.snapshot_id.sequence);
    hash = snapshot_hash_u32(
        hash, validation.snapshot.base_id.epoch);
    hash = snapshot_hash_u32(
        hash, validation.snapshot.base_id.sequence);
    hash = snapshot_hash_identity(
        hash, validation.snapshot.controlled_entity.identity);
    hash = snapshot_hash_u32(hash, info->range_counts[0]);
    hash = snapshot_hash_u32(hash, info->range_counts[1]);
    hash = snapshot_hash_u32(hash, info->range_counts[2]);
    hash = snapshot_hash_u64(
        hash, validation.hashes.semantic_player_hash);
    hash = snapshot_hash_u64(
        hash, validation.hashes.semantic_entity_hash);
    hash = snapshot_hash_u64(
        hash, validation.hashes.semantic_area_hash);
    hash = snapshot_hash_u64(
        hash, validation.hashes.semantic_event_hash);
    validation.hashes.legacy_parity_hash = hash;
    *validation_out = validation;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecSnapshotMetadataV1(
    const void *encoded,
    size_t encoded_bytes,
    uint32_t max_entities,
    worr_native_codec_snapshot_metadata_v1 *metadata_out)
{
    worr_native_codec_info_v1 info;
    worr_native_codec_snapshot_metadata_v1 metadata;
    snapshot_decode_validation validation;
    worr_native_codec_result_v1 result;

    if (metadata_out == NULL || max_entities == 0 ||
        ranges_overlap(encoded, encoded_bytes, metadata_out,
                       sizeof(*metadata_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    result = Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    if (info.record_class != WORR_NATIVE_RECORD_SNAPSHOT_V1)
        return WORR_NATIVE_CODEC_UNSUPPORTED;
    result = validate_snapshot_wire(encoded, encoded_bytes, &info,
                                    max_entities, &validation);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    memset(&metadata, 0, sizeof(metadata));
    metadata.struct_size = sizeof(metadata);
    metadata.schema_version = WORR_NATIVE_CODEC_ABI_VERSION;
    metadata.codec = info;
    metadata.snapshot = validation.snapshot;
    metadata.hashes = validation.hashes;
    *metadata_out = metadata;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecSnapshotPreflightV1(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t max_entities,
    uint32_t *encoded_bytes_out)
{
    uint32_t encoded_bytes;
    size_t entity_bytes;
    size_t event_bytes;

    if (view == NULL || encoded_bytes_out == NULL || max_entities == 0 ||
        ranges_overlap(view, sizeof(*view), encoded_bytes_out,
                       sizeof(*encoded_bytes_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    if (view->entity_count > WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES ||
        view->area_byte_count >
            WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES ||
        view->event_ref_count >
            WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS) {
        return WORR_NATIVE_CODEC_LIMIT;
    }
    if (Worr_NativeCodecSnapshotTransportFailureFlagsV1(
            view, max_entities) != 0)
        return WORR_NATIVE_CODEC_INVALID_RECORD;
    entity_bytes =
        (size_t)view->entity_count * sizeof(*view->entities);
    event_bytes =
        (size_t)view->event_ref_count * sizeof(*view->event_refs);
    if (ranges_overlap(view->snapshot, sizeof(*view->snapshot),
                       encoded_bytes_out, sizeof(*encoded_bytes_out)) ||
        ranges_overlap(view->player, sizeof(*view->player),
                       encoded_bytes_out, sizeof(*encoded_bytes_out)) ||
        ranges_overlap(view->entities, entity_bytes, encoded_bytes_out,
                       sizeof(*encoded_bytes_out)) ||
        ranges_overlap(view->area_bytes, view->area_byte_count,
                       encoded_bytes_out, sizeof(*encoded_bytes_out)) ||
        ranges_overlap(view->event_refs, event_bytes, encoded_bytes_out,
                       sizeof(*encoded_bytes_out))) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    if (!snapshot_encoded_size(view, &encoded_bytes))
        return WORR_NATIVE_CODEC_LIMIT;
    *encoded_bytes_out = encoded_bytes;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecSnapshotEncodeV1(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t max_entities,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out)
{
    byte_writer writer;
    uint32_t encoded_bytes;
    uint32_t index;
    size_t entity_bytes;
    size_t event_bytes;
    worr_native_codec_result_v1 result;

    if (encoded_out == NULL || encoded_bytes_out == NULL)
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    result = Worr_NativeCodecSnapshotPreflightV1(
        view, max_entities, &encoded_bytes);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    if (encoded_capacity < encoded_bytes)
        return WORR_NATIVE_CODEC_OUTPUT_TOO_SMALL;
    entity_bytes =
        (size_t)view->entity_count * sizeof(*view->entities);
    event_bytes =
        (size_t)view->event_ref_count * sizeof(*view->event_refs);
    if (ranges_overlap(encoded_out, encoded_bytes, view, sizeof(*view)) ||
        ranges_overlap(encoded_out, encoded_bytes, view->snapshot,
                       sizeof(*view->snapshot)) ||
        ranges_overlap(encoded_out, encoded_bytes, view->player,
                       sizeof(*view->player)) ||
        ranges_overlap(encoded_out, encoded_bytes, view->entities,
                       entity_bytes) ||
        ranges_overlap(encoded_out, encoded_bytes, view->area_bytes,
                       view->area_byte_count) ||
        ranges_overlap(encoded_out, encoded_bytes, view->event_refs,
                       event_bytes) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       encoded_out, encoded_bytes) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out), view,
                       sizeof(*view)) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       view->snapshot, sizeof(*view->snapshot)) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       view->player, sizeof(*view->player)) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       view->entities, entity_bytes) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       view->area_bytes, view->area_byte_count) ||
        ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                       view->event_refs, event_bytes)) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }

    writer.cursor = (uint8_t *)encoded_out;
    write_header(&writer, WORR_NATIVE_RECORD_SNAPSHOT_V1,
                 WORR_SNAPSHOT_ABI_VERSION, WORR_SNAPSHOT_MODEL_REVISION,
                 encoded_bytes, WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES,
                 view->entity_count, view->area_byte_count,
                 view->event_ref_count, view->snapshot->snapshot_id.epoch,
                 view->snapshot->snapshot_id.sequence);
    write_snapshot_fixed(&writer, view);
    for (index = 0; index < view->entity_count; ++index)
        write_snapshot_entity(&writer, &view->entities[index]);
    if (view->area_byte_count != 0) {
        memcpy(writer.cursor, view->area_bytes, view->area_byte_count);
        writer.cursor += view->area_byte_count;
    }
    for (index = 0; index < view->event_ref_count; ++index)
        write_snapshot_event_ref(&writer, &view->event_refs[index]);
    *encoded_bytes_out = encoded_bytes;
    return WORR_NATIVE_CODEC_OK;
}

static bool output_regions_disjoint(const void *encoded,
                                    size_t encoded_bytes,
                                    worr_snapshot_v2 *snapshot_out,
                                    worr_snapshot_player_v2 *player_out,
                                    worr_snapshot_entity_v2 *entities_out,
                                    uint32_t entity_count,
                                    uint8_t *area_bytes_out,
                                    uint32_t area_count,
                                    worr_snapshot_event_ref_v2 *event_refs_out,
                                    uint32_t event_count,
                                    worr_snapshot_store_publish_v2 *publication_out)
{
    const void *regions[6];
    size_t sizes[6];
    uint32_t left;
    uint32_t right;

    regions[0] = snapshot_out;
    sizes[0] = sizeof(*snapshot_out);
    regions[1] = player_out;
    sizes[1] = sizeof(*player_out);
    regions[2] = entities_out;
    sizes[2] = (size_t)entity_count * sizeof(*entities_out);
    regions[3] = area_bytes_out;
    sizes[3] = area_count;
    regions[4] = event_refs_out;
    sizes[4] = (size_t)event_count * sizeof(*event_refs_out);
    regions[5] = publication_out;
    sizes[5] = sizeof(*publication_out);
    for (left = 0; left < 6; ++left) {
        if (ranges_overlap(encoded, encoded_bytes, regions[left],
                           sizes[left])) {
            return false;
        }
        for (right = left + 1; right < 6; ++right) {
            if (ranges_overlap(regions[left], sizes[left], regions[right],
                               sizes[right])) {
                return false;
            }
        }
    }
    return true;
}

static bool projection_output_regions_disjoint(
    const void *encoded,
    size_t encoded_bytes,
    worr_snapshot_v2 *snapshot_out,
    worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out,
    uint32_t entity_count,
    uint8_t *area_bytes_out,
    uint32_t area_count,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_count,
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out)
{
    const void *regions[7];
    size_t sizes[7];
    uint32_t left;
    uint32_t right;

    regions[0] = snapshot_out;
    sizes[0] = sizeof(*snapshot_out);
    regions[1] = player_out;
    sizes[1] = sizeof(*player_out);
    regions[2] = entities_out;
    sizes[2] = (size_t)entity_count * sizeof(*entities_out);
    regions[3] = area_bytes_out;
    sizes[3] = area_count;
    regions[4] = event_refs_out;
    sizes[4] = (size_t)event_count * sizeof(*event_refs_out);
    regions[5] = view_out;
    sizes[5] = sizeof(*view_out);
    regions[6] = hashes_out;
    sizes[6] = sizeof(*hashes_out);
    for (left = 0; left < 7; ++left) {
        if (ranges_overlap(encoded, encoded_bytes, regions[left],
                           sizes[left])) {
            return false;
        }
        for (right = left + 1; right < 7; ++right) {
            if (ranges_overlap(regions[left], sizes[left], regions[right],
                               sizes[right])) {
                return false;
            }
        }
    }
    return true;
}

worr_native_codec_result_v1
Worr_NativeCodecSnapshotDecodeProjectionV1(
    const void *encoded,
    size_t encoded_bytes,
    uint32_t max_entities,
    worr_snapshot_v2 *snapshot_out,
    worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out,
    uint32_t entity_capacity,
    uint8_t *area_bytes_out,
    uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out)
{
    worr_native_codec_info_v1 info;
    snapshot_decode_validation validation;
    worr_snapshot_projection_view_v2 view;
    byte_reader reader;
    uint32_t index;
    worr_native_codec_result_v1 result;

    if (snapshot_out == NULL || player_out == NULL || view_out == NULL ||
        hashes_out == NULL || max_entities == 0) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    result = Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    if (info.record_class != WORR_NATIVE_RECORD_SNAPSHOT_V1)
        return WORR_NATIVE_CODEC_UNSUPPORTED;
    result = validate_snapshot_wire(encoded, encoded_bytes, &info,
                                    max_entities, &validation);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;

    if ((info.range_counts[0] == 0 &&
         (entities_out != NULL || entity_capacity != 0)) ||
        (info.range_counts[0] != 0 &&
         (entities_out == NULL || entity_capacity < info.range_counts[0])) ||
        (info.range_counts[1] == 0 &&
         (area_bytes_out != NULL || area_capacity != 0)) ||
        (info.range_counts[1] != 0 &&
         (area_bytes_out == NULL || area_capacity < info.range_counts[1])) ||
        (info.range_counts[2] == 0 &&
         (event_refs_out != NULL || event_ref_capacity != 0)) ||
        (info.range_counts[2] != 0 &&
         (event_refs_out == NULL ||
          event_ref_capacity < info.range_counts[2]))) {
        return WORR_NATIVE_CODEC_CAPACITY;
    }
    if (!projection_output_regions_disjoint(
            encoded, encoded_bytes, snapshot_out, player_out, entities_out,
            info.range_counts[0], area_bytes_out, info.range_counts[1],
            event_refs_out, info.range_counts[2], view_out, hashes_out)) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }

    /*
     * validate_snapshot_wire already performed a complete dry run, including
     * exact end offsets and all five canonical hashes.  This commit pass has
     * no data-dependent failure after its first write.
     */
    reader.cursor = (const uint8_t *)encoded + validation.entity_offset;
    reader.end = (const uint8_t *)encoded + encoded_bytes;
    for (index = 0; index < info.range_counts[0]; ++index)
        (void)read_snapshot_entity(&reader, &entities_out[index]);
    if (info.range_counts[1] != 0) {
        memcpy(area_bytes_out,
               (const uint8_t *)encoded + validation.area_offset,
               info.range_counts[1]);
    }
    reader.cursor = (const uint8_t *)encoded + validation.event_offset;
    for (index = 0; index < info.range_counts[2]; ++index)
        (void)read_snapshot_event_ref(&reader, &event_refs_out[index]);

    memset(&view, 0, sizeof(view));
    view.struct_size = sizeof(view);
    view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    view.snapshot = snapshot_out;
    view.player = player_out;
    view.entities =
        info.range_counts[0] == 0 ? NULL : entities_out;
    view.area_bytes =
        info.range_counts[1] == 0 ? NULL : area_bytes_out;
    view.event_refs =
        info.range_counts[2] == 0 ? NULL : event_refs_out;
    view.entity_count = info.range_counts[0];
    view.area_byte_count = info.range_counts[1];
    view.event_ref_count = info.range_counts[2];
    *snapshot_out = validation.snapshot;
    *player_out = validation.player;

    *view_out = view;
    *hashes_out = validation.hashes;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecSnapshotDecodeV1(
    const void *encoded,
    size_t encoded_bytes,
    uint32_t max_entities,
    worr_snapshot_v2 *snapshot_out,
    worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out,
    uint32_t entity_capacity,
    uint8_t *area_bytes_out,
    uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_snapshot_store_publish_v2 *publication_out)
{
    worr_native_codec_info_v1 info;
    snapshot_decode_validation validation;
    worr_snapshot_store_publish_v2 publication;
    worr_snapshot_v2 snapshot;
    byte_reader reader;
    uint32_t index;
    worr_native_codec_result_v1 result;

    if (snapshot_out == NULL || player_out == NULL ||
        publication_out == NULL || max_entities == 0) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }
    result = Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;
    if (info.record_class != WORR_NATIVE_RECORD_SNAPSHOT_V1)
        return WORR_NATIVE_CODEC_UNSUPPORTED;
    result = validate_snapshot_wire(encoded, encoded_bytes, &info,
                                    max_entities, &validation);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;

    if ((info.range_counts[0] == 0 &&
         (entities_out != NULL || entity_capacity != 0)) ||
        (info.range_counts[0] != 0 &&
         (entities_out == NULL || entity_capacity < info.range_counts[0])) ||
        (info.range_counts[1] == 0 &&
         (area_bytes_out != NULL || area_capacity != 0)) ||
        (info.range_counts[1] != 0 &&
         (area_bytes_out == NULL || area_capacity < info.range_counts[1])) ||
        (info.range_counts[2] == 0 &&
         (event_refs_out != NULL || event_ref_capacity != 0)) ||
        (info.range_counts[2] != 0 &&
         (event_refs_out == NULL ||
          event_ref_capacity < info.range_counts[2]))) {
        return WORR_NATIVE_CODEC_CAPACITY;
    }
    if (!output_regions_disjoint(
            encoded, encoded_bytes, snapshot_out, player_out, entities_out,
            info.range_counts[0], area_bytes_out, info.range_counts[1],
            event_refs_out, info.range_counts[2], publication_out)) {
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    }

    /*
     * The first pass proved every boundary and exact end offset while all
     * destinations were untouched.  The encoded range cannot alias any
     * destination, so this second pass is an infallible commit over the same
     * immutable bytes and has no failure return after its first write.
     */
    reader.cursor = (const uint8_t *)encoded + validation.entity_offset;
    reader.end = (const uint8_t *)encoded + encoded_bytes;
    for (index = 0; index < info.range_counts[0]; ++index)
        (void)read_snapshot_entity(&reader, &entities_out[index]);
    if (info.range_counts[1] != 0)
        memcpy(area_bytes_out,
               (const uint8_t *)encoded + validation.area_offset,
               info.range_counts[1]);
    reader.cursor = (const uint8_t *)encoded + validation.event_offset;
    for (index = 0; index < info.range_counts[2]; ++index)
        (void)read_snapshot_event_ref(&reader, &event_refs_out[index]);

    snapshot = validation.snapshot;
    memset(&snapshot.entity_range, 0, sizeof(snapshot.entity_range));
    memset(&snapshot.area_range, 0, sizeof(snapshot.area_range));
    memset(&snapshot.event_range, 0, sizeof(snapshot.event_range));
    snapshot.player_hash = 0;
    snapshot.entity_hash = 0;
    snapshot.area_hash = 0;
    snapshot.event_hash = 0;
    snapshot.snapshot_hash = 0;
    memset(&publication, 0, sizeof(publication));
    publication.struct_size = sizeof(publication);
    publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
    publication.snapshot = snapshot_out;
    publication.player = player_out;
    publication.entities =
        info.range_counts[0] == 0 ? NULL : entities_out;
    publication.area_bytes =
        info.range_counts[1] == 0 ? NULL : area_bytes_out;
    publication.event_refs =
        info.range_counts[2] == 0 ? NULL : event_refs_out;
    publication.entity_count = info.range_counts[0];
    publication.area_byte_count = info.range_counts[1];
    publication.event_ref_count = info.range_counts[2];
    *snapshot_out = snapshot;
    *player_out = validation.player;
    *publication_out = publication;
    return WORR_NATIVE_CODEC_OK;
}

worr_native_codec_result_v1 Worr_NativeCodecValidateV1(
    const void *encoded,
    size_t encoded_bytes,
    uint32_t max_entities)
{
    worr_native_codec_info_v1 info;
    worr_native_codec_result_v1 result;

    if (max_entities == 0)
        return WORR_NATIVE_CODEC_INVALID_ARGUMENT;
    result = Worr_NativeCodecInspectV1(encoded, encoded_bytes, &info);
    if (result != WORR_NATIVE_CODEC_OK)
        return result;

    switch (info.record_class) {
    case WORR_NATIVE_RECORD_COMMAND_V1: {
        worr_command_record_v1 command;
        return Worr_NativeCodecCommandDecodeV1(
            encoded, encoded_bytes,
            WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, &command);
    }
    case WORR_NATIVE_RECORD_EVENT_V1: {
        worr_event_record_v1 event;
        return Worr_NativeCodecEventDecodeV1(
            encoded, encoded_bytes, max_entities, &event);
    }
    case WORR_NATIVE_RECORD_SNAPSHOT_V1: {
        snapshot_decode_validation validation;
        return validate_snapshot_wire(encoded, encoded_bytes, &info,
                                      max_entities, &validation);
    }
    default:
        return WORR_NATIVE_CODEC_UNSUPPORTED;
    }
}
