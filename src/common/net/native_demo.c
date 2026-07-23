/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_demo.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

enum {
  WDM_MAGIC_OFFSET = 0,
  WDM_VERSION_OFFSET = 4,
  WDM_HEADER_BYTES_OFFSET = 6,
  WDM_TRANSPORT_OFFSET = 8,
  WDM_CODEC_VERSION_OFFSET = 10,
  WDM_FLAGS_OFFSET = 12,
  WDM_CAPABILITY_VERSION_OFFSET = 16,
  WDM_RESERVED0_OFFSET = 18,
  WDM_CAPABILITY_MASK_OFFSET = 20,
  WDM_TRANSPORT_EPOCH_OFFSET = 24,
  WDM_TIMELINE_EPOCH_OFFSET = 28,
  WDM_FIRST_ORDINAL_OFFSET = 32,
  WDM_CREATED_TIME_OFFSET = 40,
  WDM_CRC_OFFSET = 48,
  WDM_RECORD_HEADER_BYTES_OFFSET = 52,
  WDM_RESERVED1_OFFSET = 54,
  WDM_RESERVED2_OFFSET = 56,

  WDR_MAGIC_OFFSET = 0,
  WDR_VERSION_OFFSET = 4,
  WDR_HEADER_BYTES_OFFSET = 6,
  WDR_KIND_OFFSET = 8,
  WDR_FLAGS_OFFSET = 9,
  WDR_RESERVED0_OFFSET = 10,
  WDR_FRAME_BYTES_OFFSET = 12,
  WDR_PAYLOAD_BYTES_OFFSET = 16,
  WDR_CRC_OFFSET = 20,
  WDR_ORDINAL_OFFSET = 24,
  WDR_TIME_OFFSET = 32,
  WDR_RESERVED1_OFFSET = 40,

  ORDER_PHASE_NONE = 0,
  ORDER_PHASE_SNAPSHOT = 1,
  ORDER_PHASE_EVENT = 2,
};

static const uint8_t wdm_magic[4] = {'W', 'D', 'M', '1'};
static const uint8_t wdr_magic[4] = {'W', 'D', 'R', '1'};

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes) {
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

static uint16_t read_u16_le(const uint8_t *bytes) {
  return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *bytes) {
  return (uint64_t)read_u32_le(bytes) |
         ((uint64_t)read_u32_le(bytes + 4) << 32);
}

static void write_u16_le(uint8_t *bytes, uint16_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value) {
  write_u32_le(bytes, (uint32_t)value);
  write_u32_le(bytes + 4, (uint32_t)(value >> 32));
}

/* Same reflected IEEE CRC-32 convention as WNE1 and WTC1. */
static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes, size_t count) {
  size_t index;

  for (index = 0; index < count; ++index) {
    unsigned bit;

    crc ^= bytes[index];
    for (bit = 0; bit < 8; ++bit) {
      const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
    }
  }
  return crc;
}

static uint32_t crc32_zero_field(const uint8_t *header, size_t header_bytes,
                                 size_t crc_offset, const uint8_t *payload,
                                 size_t payload_bytes) {
  static const uint8_t zero_crc[sizeof(uint32_t)] = {0, 0, 0, 0};
  uint32_t crc = UINT32_MAX;

  crc = crc32_update(crc, header, crc_offset);
  crc = crc32_update(crc, zero_crc, sizeof(zero_crc));
  crc = crc32_update(crc, header + crc_offset + sizeof(uint32_t),
                     header_bytes - crc_offset - sizeof(uint32_t));
  if (payload_bytes != 0)
    crc = crc32_update(crc, payload, payload_bytes);
  return ~crc;
}

static bool record_kind_valid(uint8_t kind) {
  return kind == WORR_NATIVE_DEMO_RECORD_COMMAND_V1 ||
         kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1 ||
         kind == WORR_NATIVE_DEMO_RECORD_EVENT_V1;
}

static worr_native_demo_result_v1
codec_result(worr_native_codec_result_v1 result) {
  switch (result) {
  case WORR_NATIVE_CODEC_OK:
    return WORR_NATIVE_DEMO_OK;
  case WORR_NATIVE_CODEC_UNSUPPORTED:
    return WORR_NATIVE_DEMO_UNSUPPORTED;
  case WORR_NATIVE_CODEC_LIMIT:
    return WORR_NATIVE_DEMO_LIMIT;
  case WORR_NATIVE_CODEC_CORRUPT:
    return WORR_NATIVE_DEMO_CORRUPT;
  case WORR_NATIVE_CODEC_INVALID_ARGUMENT:
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  case WORR_NATIVE_CODEC_INVALID_RECORD:
  case WORR_NATIVE_CODEC_OUTPUT_TOO_SMALL:
  case WORR_NATIVE_CODEC_MALFORMED:
  case WORR_NATIVE_CODEC_CAPACITY:
    return WORR_NATIVE_DEMO_MALFORMED;
  }
  return WORR_NATIVE_DEMO_MALFORMED;
}

static bool
container_config_valid(const worr_native_demo_container_config_v1 *config) {
  return config->struct_size == sizeof(*config) &&
         config->schema_version == WORR_NATIVE_DEMO_ABI_VERSION &&
         config->transport == WORR_NATIVE_DEMO_TRANSPORT_WNC1_V1 &&
         (config->capability_mask & ~WORR_NET_CAP_KNOWN_MASK) == 0 &&
         config->transport_epoch != 0 && config->timeline_epoch != 0 &&
         config->reserved0 == 0 && config->first_record_ordinal != 0 &&
         config->reserved1 == 0;
}

static bool
container_info_valid(const worr_native_demo_container_info_v1 *container) {
  return container->struct_size == sizeof(*container) &&
         container->schema_version == WORR_NATIVE_DEMO_ABI_VERSION &&
         container->transport == WORR_NATIVE_DEMO_TRANSPORT_WNC1_V1 &&
         container->codec_wire_version == WORR_NATIVE_CODEC_WIRE_VERSION &&
         container->capability_version == WORR_NET_CAPABILITY_VERSION &&
         (container->capability_mask & ~WORR_NET_CAP_KNOWN_MASK) == 0 &&
         container->transport_epoch != 0 && container->timeline_epoch != 0 &&
         container->first_record_ordinal != 0 &&
         container->wire_header_bytes == WORR_NATIVE_DEMO_WIRE_HEADER_BYTES &&
         container->record_wire_header_bytes ==
             WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES &&
         container->reserved0 == 0;
}

worr_native_demo_result_v1 Worr_NativeDemoContainerEncodeV1(
    const worr_native_demo_container_config_v1 *config, void *encoded_out,
    size_t encoded_capacity, size_t *encoded_bytes_out) {
  uint8_t encoded[WORR_NATIVE_DEMO_WIRE_HEADER_BYTES];
  uint32_t crc;

  if (config == NULL || encoded_out == NULL || encoded_bytes_out == NULL)
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  if (!container_config_valid(config))
    return WORR_NATIVE_DEMO_INVALID_METADATA;
  if (encoded_capacity < sizeof(encoded))
    return WORR_NATIVE_DEMO_OUTPUT_TOO_SMALL;
  if (ranges_overlap(encoded_out, sizeof(encoded), encoded_bytes_out,
                     sizeof(*encoded_bytes_out)) ||
      ranges_overlap(encoded_out, sizeof(encoded), config, sizeof(*config)) ||
      ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out), config,
                     sizeof(*config))) {
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  }

  memset(encoded, 0, sizeof(encoded));
  memcpy(encoded + WDM_MAGIC_OFFSET, wdm_magic, sizeof(wdm_magic));
  write_u16_le(encoded + WDM_VERSION_OFFSET, WORR_NATIVE_DEMO_WIRE_VERSION);
  write_u16_le(encoded + WDM_HEADER_BYTES_OFFSET,
               WORR_NATIVE_DEMO_WIRE_HEADER_BYTES);
  write_u16_le(encoded + WDM_TRANSPORT_OFFSET, config->transport);
  write_u16_le(encoded + WDM_CODEC_VERSION_OFFSET,
               WORR_NATIVE_CODEC_WIRE_VERSION);
  write_u16_le(encoded + WDM_CAPABILITY_VERSION_OFFSET,
               WORR_NET_CAPABILITY_VERSION);
  write_u32_le(encoded + WDM_CAPABILITY_MASK_OFFSET, config->capability_mask);
  write_u32_le(encoded + WDM_TRANSPORT_EPOCH_OFFSET, config->transport_epoch);
  write_u32_le(encoded + WDM_TIMELINE_EPOCH_OFFSET, config->timeline_epoch);
  write_u64_le(encoded + WDM_FIRST_ORDINAL_OFFSET,
               config->first_record_ordinal);
  write_u64_le(encoded + WDM_CREATED_TIME_OFFSET, config->created_time_us);
  write_u16_le(encoded + WDM_RECORD_HEADER_BYTES_OFFSET,
               WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES);
  crc = crc32_zero_field(encoded, sizeof(encoded), WDM_CRC_OFFSET, NULL, 0);
  write_u32_le(encoded + WDM_CRC_OFFSET, crc);

  memcpy(encoded_out, encoded, sizeof(encoded));
  *encoded_bytes_out = sizeof(encoded);
  return WORR_NATIVE_DEMO_OK;
}

worr_native_demo_result_v1
Worr_NativeDemoContainerDecodeV1(const void *encoded, size_t available_bytes,
                                 worr_native_demo_container_info_v1 *info_out) {
  const uint8_t *bytes = (const uint8_t *)encoded;
  worr_native_demo_container_info_v1 info;
  uint32_t stored_crc;

  if (info_out == NULL || (available_bytes != 0 && encoded == NULL))
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  if (available_bytes < WORR_NATIVE_DEMO_WIRE_HEADER_BYTES)
    return WORR_NATIVE_DEMO_TRUNCATED;
  if (ranges_overlap(encoded, WORR_NATIVE_DEMO_WIRE_HEADER_BYTES, info_out,
                     sizeof(*info_out))) {
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  }
  if (memcmp(bytes + WDM_MAGIC_OFFSET, wdm_magic, sizeof(wdm_magic)) != 0)
    return WORR_NATIVE_DEMO_MALFORMED;
  if (read_u16_le(bytes + WDM_VERSION_OFFSET) !=
          WORR_NATIVE_DEMO_WIRE_VERSION ||
      read_u16_le(bytes + WDM_HEADER_BYTES_OFFSET) !=
          WORR_NATIVE_DEMO_WIRE_HEADER_BYTES ||
      read_u16_le(bytes + WDM_TRANSPORT_OFFSET) !=
          WORR_NATIVE_DEMO_TRANSPORT_WNC1_V1 ||
      read_u16_le(bytes + WDM_CODEC_VERSION_OFFSET) !=
          WORR_NATIVE_CODEC_WIRE_VERSION ||
      read_u16_le(bytes + WDM_CAPABILITY_VERSION_OFFSET) !=
          WORR_NET_CAPABILITY_VERSION ||
      read_u16_le(bytes + WDM_RECORD_HEADER_BYTES_OFFSET) !=
          WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES) {
    return WORR_NATIVE_DEMO_UNSUPPORTED;
  }
  if (read_u32_le(bytes + WDM_FLAGS_OFFSET) != 0 ||
      read_u16_le(bytes + WDM_RESERVED0_OFFSET) != 0 ||
      read_u16_le(bytes + WDM_RESERVED1_OFFSET) != 0 ||
      read_u64_le(bytes + WDM_RESERVED2_OFFSET) != 0 ||
      read_u32_le(bytes + WDM_TRANSPORT_EPOCH_OFFSET) == 0 ||
      read_u32_le(bytes + WDM_TIMELINE_EPOCH_OFFSET) == 0 ||
      read_u64_le(bytes + WDM_FIRST_ORDINAL_OFFSET) == 0) {
    return WORR_NATIVE_DEMO_MALFORMED;
  }
  if ((read_u32_le(bytes + WDM_CAPABILITY_MASK_OFFSET) &
       ~WORR_NET_CAP_KNOWN_MASK) != 0) {
    return WORR_NATIVE_DEMO_UNSUPPORTED;
  }
  stored_crc = read_u32_le(bytes + WDM_CRC_OFFSET);
  if (stored_crc != crc32_zero_field(bytes, WORR_NATIVE_DEMO_WIRE_HEADER_BYTES,
                                     WDM_CRC_OFFSET, NULL, 0)) {
    return WORR_NATIVE_DEMO_CORRUPT;
  }

  memset(&info, 0, sizeof(info));
  info.struct_size = sizeof(info);
  info.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  info.transport = read_u16_le(bytes + WDM_TRANSPORT_OFFSET);
  info.codec_wire_version = read_u16_le(bytes + WDM_CODEC_VERSION_OFFSET);
  info.capability_version = read_u16_le(bytes + WDM_CAPABILITY_VERSION_OFFSET);
  info.capability_mask = read_u32_le(bytes + WDM_CAPABILITY_MASK_OFFSET);
  info.transport_epoch = read_u32_le(bytes + WDM_TRANSPORT_EPOCH_OFFSET);
  info.timeline_epoch = read_u32_le(bytes + WDM_TIMELINE_EPOCH_OFFSET);
  info.first_record_ordinal = read_u64_le(bytes + WDM_FIRST_ORDINAL_OFFSET);
  info.created_time_us = read_u64_le(bytes + WDM_CREATED_TIME_OFFSET);
  info.header_crc32 = stored_crc;
  info.wire_header_bytes = WORR_NATIVE_DEMO_WIRE_HEADER_BYTES;
  info.record_wire_header_bytes = WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES;
  *info_out = info;
  return WORR_NATIVE_DEMO_OK;
}

static bool record_metadata_valid(const worr_native_demo_record_v1 *record) {
  return record->struct_size == sizeof(*record) &&
         record->schema_version == WORR_NATIVE_DEMO_ABI_VERSION &&
         record_kind_valid(record->record_kind) && record->flags == 0 &&
         record->ordinal != 0 && record->reserved0 == 0;
}

worr_native_demo_result_v1
Worr_NativeDemoRecordEncodeV1(const worr_native_demo_record_v1 *record,
                              const void *wnc1_payload, size_t payload_bytes,
                              void *encoded_out, size_t encoded_capacity,
                              size_t *encoded_bytes_out) {
  uint8_t header[WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES];
  worr_native_codec_info_v1 codec;
  worr_native_demo_result_v1 nested;
  size_t frame_bytes;
  uint32_t crc;

  if (record == NULL || wnc1_payload == NULL || encoded_out == NULL ||
      encoded_bytes_out == NULL) {
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  }
  if (!record_metadata_valid(record))
    return WORR_NATIVE_DEMO_INVALID_METADATA;
  if (payload_bytes == 0 ||
      payload_bytes > WORR_NATIVE_CODEC_MAX_ENCODED_BYTES) {
    return WORR_NATIVE_DEMO_LIMIT;
  }
  nested = codec_result(
      Worr_NativeCodecInspectV1(wnc1_payload, payload_bytes, &codec));
  if (nested != WORR_NATIVE_DEMO_OK)
    return nested;
  if (codec.record_class != record->record_kind)
    return WORR_NATIVE_DEMO_INVALID_METADATA;

  frame_bytes = WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES + payload_bytes;
  if (encoded_capacity < frame_bytes)
    return WORR_NATIVE_DEMO_OUTPUT_TOO_SMALL;
  if (ranges_overlap(encoded_out, frame_bytes, wnc1_payload, payload_bytes) ||
      ranges_overlap(encoded_out, frame_bytes, record, sizeof(*record)) ||
      ranges_overlap(encoded_out, frame_bytes, encoded_bytes_out,
                     sizeof(*encoded_bytes_out)) ||
      ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                     wnc1_payload, payload_bytes) ||
      ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out), record,
                     sizeof(*record))) {
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  }

  memset(header, 0, sizeof(header));
  memcpy(header + WDR_MAGIC_OFFSET, wdr_magic, sizeof(wdr_magic));
  write_u16_le(header + WDR_VERSION_OFFSET,
               WORR_NATIVE_DEMO_RECORD_WIRE_VERSION);
  write_u16_le(header + WDR_HEADER_BYTES_OFFSET,
               WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES);
  header[WDR_KIND_OFFSET] = record->record_kind;
  write_u32_le(header + WDR_FRAME_BYTES_OFFSET, (uint32_t)frame_bytes);
  write_u32_le(header + WDR_PAYLOAD_BYTES_OFFSET, (uint32_t)payload_bytes);
  write_u64_le(header + WDR_ORDINAL_OFFSET, record->ordinal);
  write_u64_le(header + WDR_TIME_OFFSET, record->time_us);
  crc = crc32_zero_field(header, sizeof(header), WDR_CRC_OFFSET,
                         (const uint8_t *)wnc1_payload, payload_bytes);
  write_u32_le(header + WDR_CRC_OFFSET, crc);

  memcpy(encoded_out, header, sizeof(header));
  memcpy((uint8_t *)encoded_out + sizeof(header), wnc1_payload, payload_bytes);
  *encoded_bytes_out = frame_bytes;
  return WORR_NATIVE_DEMO_OK;
}

static bool record_info_valid(const worr_native_demo_record_info_v1 *record,
                              worr_native_record_ref_v1 *record_ref_out) {
  worr_native_record_ref_v1 record_ref;
  uint32_t minimum_bytes;
  uint32_t maximum_bytes;

  if (record->struct_size != sizeof(*record) ||
      record->schema_version != WORR_NATIVE_DEMO_ABI_VERSION ||
      !record_kind_valid(record->record_kind) || record->flags != 0 ||
      record->reserved0 != 0 || record->ordinal == 0 ||
      record->payload_bytes == 0 ||
      record->payload_bytes > WORR_NATIVE_CODEC_MAX_ENCODED_BYTES ||
      record->frame_bytes !=
          WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES + record->payload_bytes ||
      record->record_offset >
          UINT64_MAX - WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES ||
      record->record_offset > UINT64_MAX - record->frame_bytes ||
      record->payload_offset !=
          record->record_offset +
              (uint64_t)WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES ||
      record->next_record_offset !=
          record->record_offset + (uint64_t)record->frame_bytes ||
      record->codec.struct_size != sizeof(record->codec) ||
      record->codec.schema_version != WORR_NATIVE_CODEC_ABI_VERSION ||
      record->codec.record_class != record->record_kind ||
      record->codec.encoded_bytes != record->payload_bytes ||
      record->codec.reserved0 != 0 || record->codec.reserved1 != 0 ||
      !Worr_NativeCodecInfoRecordRefV1(&record->codec, &record_ref)) {
    return false;
  }

  switch (record->record_kind) {
  case WORR_NATIVE_DEMO_RECORD_COMMAND_V1:
    if (record->codec.fixed_body_bytes !=
            WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES ||
        record->codec.range_counts[0] != 0 ||
        record->codec.range_counts[1] != 0 ||
        record->codec.range_counts[2] != 0 ||
        record->payload_bytes !=
            WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES) {
      return false;
    }
    break;
  case WORR_NATIVE_DEMO_RECORD_EVENT_V1:
    if (record->codec.fixed_body_bytes !=
            WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES ||
        record->codec.range_counts[0] > WORR_EVENT_PAYLOAD_CAPACITY ||
        record->codec.range_counts[1] != 0 ||
        record->codec.range_counts[2] != 0 ||
        record->payload_bytes != WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                                     WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES +
                                     record->codec.range_counts[0]) {
      return false;
    }
    break;
  case WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1:
    if (record->codec.fixed_body_bytes !=
            WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES ||
        record->codec.range_counts[0] >
            WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES ||
        record->codec.range_counts[1] >
            WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES ||
        record->codec.range_counts[2] >
            WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS) {
      return false;
    }
    minimum_bytes = WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                    WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES +
                    record->codec.range_counts[0] *
                        WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MIN_BYTES +
                    record->codec.range_counts[1] +
                    record->codec.range_counts[2] *
                        WORR_NATIVE_CODEC_SNAPSHOT_EVENT_REF_BYTES;
    maximum_bytes = WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                    WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES +
                    record->codec.range_counts[0] *
                        WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MAX_BYTES +
                    record->codec.range_counts[1] +
                    record->codec.range_counts[2] *
                        WORR_NATIVE_CODEC_SNAPSHOT_EVENT_REF_BYTES;
    if (record->payload_bytes < minimum_bytes ||
        record->payload_bytes > maximum_bytes) {
      return false;
    }
    break;
  default:
    return false;
  }
  if (record_ref_out != NULL)
    *record_ref_out = record_ref;
  return true;
}

worr_native_demo_result_v1
Worr_NativeDemoRecordDecodeV1(const void *encoded, size_t available_bytes,
                              uint64_t record_offset,
                              worr_native_demo_record_info_v1 *info_out) {
  const uint8_t *bytes = (const uint8_t *)encoded;
  worr_native_demo_record_info_v1 info;
  worr_native_codec_info_v1 codec;
  worr_native_demo_result_v1 nested;
  uint32_t frame_bytes;
  uint32_t payload_bytes;
  uint32_t stored_crc;

  if (info_out == NULL || (available_bytes != 0 && encoded == NULL))
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  if (available_bytes < sizeof(wdr_magic))
    return WORR_NATIVE_DEMO_TRUNCATED;
  if (memcmp(bytes + WDR_MAGIC_OFFSET, wdr_magic, sizeof(wdr_magic)) != 0)
    return WORR_NATIVE_DEMO_MALFORMED;
  if (available_bytes < WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES)
    return WORR_NATIVE_DEMO_TRUNCATED;
  if (read_u16_le(bytes + WDR_VERSION_OFFSET) !=
          WORR_NATIVE_DEMO_RECORD_WIRE_VERSION ||
      read_u16_le(bytes + WDR_HEADER_BYTES_OFFSET) !=
          WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES) {
    return WORR_NATIVE_DEMO_UNSUPPORTED;
  }
  if (!record_kind_valid(bytes[WDR_KIND_OFFSET]))
    return WORR_NATIVE_DEMO_UNSUPPORTED;
  if (bytes[WDR_FLAGS_OFFSET] != 0 ||
      read_u16_le(bytes + WDR_RESERVED0_OFFSET) != 0 ||
      read_u64_le(bytes + WDR_RESERVED1_OFFSET) != 0 ||
      read_u64_le(bytes + WDR_ORDINAL_OFFSET) == 0) {
    return WORR_NATIVE_DEMO_MALFORMED;
  }

  frame_bytes = read_u32_le(bytes + WDR_FRAME_BYTES_OFFSET);
  payload_bytes = read_u32_le(bytes + WDR_PAYLOAD_BYTES_OFFSET);
  if (payload_bytes == 0 ||
      frame_bytes !=
          WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES + payload_bytes) {
    return WORR_NATIVE_DEMO_MALFORMED;
  }
  if (payload_bytes > WORR_NATIVE_CODEC_MAX_ENCODED_BYTES ||
      frame_bytes > WORR_NATIVE_DEMO_MAX_RECORD_BYTES) {
    return WORR_NATIVE_DEMO_LIMIT;
  }
  if (available_bytes < frame_bytes)
    return WORR_NATIVE_DEMO_TRUNCATED;
  if (record_offset > UINT64_MAX - frame_bytes ||
      record_offset > UINT64_MAX - WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES) {
    return WORR_NATIVE_DEMO_LIMIT;
  }
  if (ranges_overlap(encoded, frame_bytes, info_out, sizeof(*info_out)))
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;

  stored_crc = read_u32_le(bytes + WDR_CRC_OFFSET);
  if (stored_crc !=
      crc32_zero_field(
          bytes, WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES, WDR_CRC_OFFSET,
          bytes + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES, payload_bytes)) {
    return WORR_NATIVE_DEMO_CORRUPT;
  }
  nested = codec_result(Worr_NativeCodecInspectV1(
      bytes + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES, payload_bytes,
      &codec));
  if (nested != WORR_NATIVE_DEMO_OK)
    return nested;
  if (codec.record_class != bytes[WDR_KIND_OFFSET])
    return WORR_NATIVE_DEMO_MALFORMED;

  memset(&info, 0, sizeof(info));
  info.struct_size = sizeof(info);
  info.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  info.record_kind = bytes[WDR_KIND_OFFSET];
  info.frame_bytes = frame_bytes;
  info.payload_bytes = payload_bytes;
  info.frame_crc32 = stored_crc;
  info.ordinal = read_u64_le(bytes + WDR_ORDINAL_OFFSET);
  info.time_us = read_u64_le(bytes + WDR_TIME_OFFSET);
  info.record_offset = record_offset;
  info.payload_offset =
      record_offset + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES;
  info.next_record_offset = record_offset + frame_bytes;
  info.codec = codec;
  *info_out = info;
  return WORR_NATIVE_DEMO_OK;
}

worr_native_demo_result_v1
Worr_NativeDemoOrderInitV1(const worr_native_demo_container_info_v1 *container,
                           worr_native_demo_order_state_v1 *state_out) {
  worr_native_demo_order_state_v1 state;

  if (container == NULL || state_out == NULL)
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  if (ranges_overlap(container, sizeof(*container), state_out,
                     sizeof(*state_out))) {
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  }
  if (!container_info_valid(container))
    return WORR_NATIVE_DEMO_INVALID_METADATA;

  memset(&state, 0, sizeof(state));
  state.struct_size = sizeof(state);
  state.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  state.state_flags = WORR_NATIVE_DEMO_ORDER_INITIALIZED;
  state.first_record_ordinal = container->first_record_ordinal;
  *state_out = state;
  return WORR_NATIVE_DEMO_OK;
}

static bool order_state_valid(const worr_native_demo_order_state_v1 *state) {
  const uint16_t known_flags =
      WORR_NATIVE_DEMO_ORDER_INITIALIZED | WORR_NATIVE_DEMO_ORDER_OBSERVED;
  const uint32_t command_bit = UINT32_C(1)
                               << WORR_NATIVE_DEMO_RECORD_COMMAND_V1;
  const uint32_t snapshot_bit = UINT32_C(1)
                                << WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1;
  const uint32_t event_bit = UINT32_C(1) << WORR_NATIVE_DEMO_RECORD_EVENT_V1;
  const uint32_t known_classes = command_bit | snapshot_bit | event_bit;
  const bool command_seen = (state->seen_class_mask & command_bit) != 0;
  const bool snapshot_seen = (state->seen_class_mask & snapshot_bit) != 0;
  const bool event_seen = (state->seen_class_mask & event_bit) != 0;

  if (state->struct_size != sizeof(*state) ||
      state->schema_version != WORR_NATIVE_DEMO_ABI_VERSION ||
      state->state_flags == 0 || (state->state_flags & ~known_flags) != 0 ||
      (state->state_flags & WORR_NATIVE_DEMO_ORDER_INITIALIZED) == 0 ||
      state->first_record_ordinal == 0 ||
      (state->seen_class_mask & ~known_classes) != 0 ||
      state->same_time_phase > ORDER_PHASE_EVENT || state->reserved0[0] != 0 ||
      state->reserved0[1] != 0 || state->reserved0[2] != 0 ||
      state->reserved1 != 0) {
    return false;
  }
  if ((command_seen &&
       (state->command_epoch == 0 || state->command_sequence == 0)) ||
      (!command_seen &&
       (state->command_epoch != 0 || state->command_sequence != 0)) ||
      (snapshot_seen &&
       (state->snapshot_epoch == 0 || state->snapshot_sequence == 0)) ||
      (!snapshot_seen &&
       (state->snapshot_epoch != 0 || state->snapshot_sequence != 0)) ||
      (event_seen && (state->event_epoch == 0 || state->event_sequence == 0)) ||
      (!event_seen &&
       (state->event_epoch != 0 || state->event_sequence != 0)) ||
      (event_seen && !snapshot_seen) ||
      (state->same_time_phase == ORDER_PHASE_SNAPSHOT && !snapshot_seen) ||
      (state->same_time_phase == ORDER_PHASE_EVENT && !event_seen)) {
    return false;
  }
  if ((state->state_flags & WORR_NATIVE_DEMO_ORDER_OBSERVED) == 0) {
    return state->last_ordinal == 0 && state->last_time_us == 0 &&
           state->seen_class_mask == 0 &&
           state->same_time_phase == ORDER_PHASE_NONE;
  }
  return state->last_ordinal >= state->first_record_ordinal &&
         state->seen_class_mask != 0;
}

static bool identity_increases(uint32_t previous_epoch,
                               uint32_t previous_sequence, uint32_t epoch,
                               uint32_t sequence) {
  return epoch > previous_epoch ||
         (epoch == previous_epoch && sequence > previous_sequence);
}

static worr_native_demo_result_v1
order_observe_ref(worr_native_demo_order_state_v1 *state, uint8_t record_kind,
                  uint64_t ordinal, uint64_t time_us,
                  worr_native_record_ref_v1 record_ref) {
  worr_native_demo_order_state_v1 next = *state;
  uint32_t class_bit;
  bool observed;

  observed = (next.state_flags & WORR_NATIVE_DEMO_ORDER_OBSERVED) != 0;
  if ((!observed && ordinal != next.first_record_ordinal) ||
      (observed && ordinal <= next.last_ordinal) ||
      (observed && time_us < next.last_time_us)) {
    return WORR_NATIVE_DEMO_ORDER;
  }
  if (!observed || time_us != next.last_time_us)
    next.same_time_phase = ORDER_PHASE_NONE;

  class_bit = UINT32_C(1) << record_kind;
  if ((next.seen_class_mask & class_bit) != 0) {
    uint32_t epoch;
    uint32_t sequence;

    if (record_kind == WORR_NATIVE_DEMO_RECORD_COMMAND_V1) {
      epoch = next.command_epoch;
      sequence = next.command_sequence;
    } else if (record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1) {
      epoch = next.snapshot_epoch;
      sequence = next.snapshot_sequence;
    } else {
      epoch = next.event_epoch;
      sequence = next.event_sequence;
    }
    if (!identity_increases(epoch, sequence, record_ref.object_epoch,
                            record_ref.object_sequence)) {
      return WORR_NATIVE_DEMO_ORDER;
    }
  }

  if (record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1) {
    if (next.same_time_phase == ORDER_PHASE_EVENT)
      return WORR_NATIVE_DEMO_ORDER;
    next.same_time_phase = ORDER_PHASE_SNAPSHOT;
    next.snapshot_epoch = record_ref.object_epoch;
    next.snapshot_sequence = record_ref.object_sequence;
  } else if (record_kind == WORR_NATIVE_DEMO_RECORD_EVENT_V1) {
    const uint32_t snapshot_bit = UINT32_C(1)
                                  << WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1;

    if ((next.seen_class_mask & snapshot_bit) == 0)
      return WORR_NATIVE_DEMO_ORDER;
    next.same_time_phase = ORDER_PHASE_EVENT;
    next.event_epoch = record_ref.object_epoch;
    next.event_sequence = record_ref.object_sequence;
  } else {
    next.command_epoch = record_ref.object_epoch;
    next.command_sequence = record_ref.object_sequence;
  }

  next.seen_class_mask |= class_bit;
  next.last_ordinal = ordinal;
  next.last_time_us = time_us;
  next.state_flags |= WORR_NATIVE_DEMO_ORDER_OBSERVED;
  *state = next;
  return WORR_NATIVE_DEMO_OK;
}

worr_native_demo_result_v1
Worr_NativeDemoOrderObserveV1(worr_native_demo_order_state_v1 *state,
                              const worr_native_demo_record_info_v1 *record) {
  worr_native_record_ref_v1 record_ref;

  if (state == NULL || record == NULL)
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  if (ranges_overlap(state, sizeof(*state), record, sizeof(*record)))
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  if (!order_state_valid(state))
    return WORR_NATIVE_DEMO_INVALID_STATE;
  if (!record_info_valid(record, &record_ref))
    return WORR_NATIVE_DEMO_INVALID_METADATA;
  return order_observe_ref(state, record->record_kind, record->ordinal,
                           record->time_us, record_ref);
}

worr_native_demo_result_v1 Worr_NativeDemoIndexEntryFromRecordV1(
    const worr_native_demo_record_info_v1 *record,
    worr_native_demo_index_entry_v1 *entry_out) {
  worr_native_demo_index_entry_v1 entry;
  worr_native_record_ref_v1 record_ref;

  if (record == NULL || entry_out == NULL)
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  if (ranges_overlap(record, sizeof(*record), entry_out, sizeof(*entry_out))) {
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  }
  if (!record_info_valid(record, &record_ref))
    return WORR_NATIVE_DEMO_INVALID_METADATA;

  memset(&entry, 0, sizeof(entry));
  entry.struct_size = sizeof(entry);
  entry.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  entry.record_kind = record->record_kind;
  entry.ordinal = record->ordinal;
  entry.time_us = record->time_us;
  entry.record_offset = record->record_offset;
  entry.next_record_offset = record->next_record_offset;
  entry.frame_bytes = record->frame_bytes;
  entry.frame_crc32 = record->frame_crc32;
  entry.record = record_ref;
  *entry_out = entry;
  return WORR_NATIVE_DEMO_OK;
}

static bool scan_config_valid(const worr_native_demo_scan_config_v1 *config) {
  return config->struct_size == sizeof(*config) &&
         config->schema_version == WORR_NATIVE_DEMO_ABI_VERSION &&
         config->flags == 0 && config->max_entities != 0 &&
         config->reserved0 == 0;
}

static bool order_state_equal(const worr_native_demo_order_state_v1 *left,
                              const worr_native_demo_order_state_v1 *right) {
  return left->struct_size == right->struct_size &&
         left->schema_version == right->schema_version &&
         left->state_flags == right->state_flags &&
         left->first_record_ordinal == right->first_record_ordinal &&
         left->last_ordinal == right->last_ordinal &&
         left->last_time_us == right->last_time_us &&
         left->command_epoch == right->command_epoch &&
         left->command_sequence == right->command_sequence &&
         left->snapshot_epoch == right->snapshot_epoch &&
         left->snapshot_sequence == right->snapshot_sequence &&
         left->event_epoch == right->event_epoch &&
         left->event_sequence == right->event_sequence &&
         left->seen_class_mask == right->seen_class_mask &&
         left->same_time_phase == right->same_time_phase &&
         left->reserved0[0] == right->reserved0[0] &&
         left->reserved0[1] == right->reserved0[1] &&
         left->reserved0[2] == right->reserved0[2] &&
         left->reserved1 == right->reserved1;
}

static bool scan_info_valid(const worr_native_demo_scan_info_v1 *scan) {
  const uint16_t known_flags = WORR_NATIVE_DEMO_SCAN_COMPLETE |
                               WORR_NATIVE_DEMO_SCAN_HAS_RECORDS |
                               WORR_NATIVE_DEMO_SCAN_HAS_SNAPSHOTS;
  const bool has_records =
      (scan->flags & WORR_NATIVE_DEMO_SCAN_HAS_RECORDS) != 0;
  const bool has_snapshots =
      (scan->flags & WORR_NATIVE_DEMO_SCAN_HAS_SNAPSHOTS) != 0;
  const bool observed =
      (scan->order.state_flags & WORR_NATIVE_DEMO_ORDER_OBSERVED) != 0;
  const uint32_t snapshot_bit = UINT32_C(1)
                                << WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1;

  if (scan->struct_size != sizeof(*scan) ||
      scan->schema_version != WORR_NATIVE_DEMO_ABI_VERSION ||
      (scan->flags & ~known_flags) != 0 ||
      (scan->flags & WORR_NATIVE_DEMO_SCAN_COMPLETE) == 0 ||
      scan->stream_bytes < WORR_NATIVE_DEMO_WIRE_HEADER_BYTES ||
      scan->stream_offset > UINT64_MAX - scan->stream_bytes ||
      scan->next_stream_offset != scan->stream_offset + scan->stream_bytes ||
      scan->snapshot_count > scan->record_count || scan->reserved0 != 0 ||
      !container_info_valid(&scan->container) ||
      !order_state_valid(&scan->order) ||
      scan->order.first_record_ordinal !=
          scan->container.first_record_ordinal) {
    return false;
  }
  if (has_records != (scan->record_count != 0) ||
      has_snapshots != (scan->snapshot_count != 0) ||
      observed != (scan->record_count != 0) ||
      (((scan->order.seen_class_mask & snapshot_bit) != 0) != has_snapshots)) {
    return false;
  }
  return true;
}

static bool record_ref_schema_valid(worr_native_record_ref_v1 record) {
  if (!Worr_NativeEnvelopeRecordRefValidV1(record))
    return false;
  switch (record.record_class) {
  case WORR_NATIVE_DEMO_RECORD_COMMAND_V1:
    return record.record_schema_version == WORR_COMMAND_ABI_VERSION;
  case WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1:
    return record.record_schema_version == WORR_SNAPSHOT_ABI_VERSION;
  case WORR_NATIVE_DEMO_RECORD_EVENT_V1:
    return record.record_schema_version == WORR_EVENT_ABI_VERSION;
  default:
    return false;
  }
}

static bool index_entry_valid(const worr_native_demo_index_entry_v1 *entry) {
  return entry->struct_size == sizeof(*entry) &&
         entry->schema_version == WORR_NATIVE_DEMO_ABI_VERSION &&
         record_kind_valid(entry->record_kind) && entry->flags == 0 &&
         entry->ordinal != 0 && entry->reserved0 == 0 &&
         entry->frame_bytes >= WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES +
                                   WORR_NATIVE_CODEC_WIRE_HEADER_BYTES &&
         entry->frame_bytes <= WORR_NATIVE_DEMO_MAX_RECORD_BYTES &&
         entry->record_offset <= UINT64_MAX - entry->frame_bytes &&
         entry->next_record_offset ==
             entry->record_offset + (uint64_t)entry->frame_bytes &&
         entry->record.record_class == entry->record_kind &&
         record_ref_schema_valid(entry->record);
}

static uint32_t
index_crc32_update(uint32_t crc, const worr_native_demo_index_entry_v1 *entry) {
  uint8_t bytes[56];

  memset(bytes, 0, sizeof(bytes));
  write_u16_le(bytes, entry->schema_version);
  bytes[2] = entry->record_kind;
  bytes[3] = entry->flags;
  write_u64_le(bytes + 4, entry->ordinal);
  write_u64_le(bytes + 12, entry->time_us);
  write_u64_le(bytes + 20, entry->record_offset);
  write_u64_le(bytes + 28, entry->next_record_offset);
  write_u32_le(bytes + 36, entry->frame_bytes);
  write_u32_le(bytes + 40, entry->frame_crc32);
  bytes[44] = entry->record.record_class;
  bytes[45] = entry->record.reserved0;
  write_u16_le(bytes + 46, entry->record.record_schema_version);
  write_u32_le(bytes + 48, entry->record.object_epoch);
  write_u32_le(bytes + 52, entry->record.object_sequence);
  return crc32_update(crc, bytes, sizeof(bytes));
}

worr_native_demo_result_v1 Worr_NativeDemoStreamScanV1(
    const worr_native_demo_scan_config_v1 *config, const void *encoded,
    size_t encoded_bytes, worr_native_demo_index_entry_v1 *entries_out,
    size_t index_capacity, worr_native_demo_scan_info_v1 *scan_out) {
  const uint8_t *bytes = (const uint8_t *)encoded;
  worr_native_demo_scan_info_v1 scan;
  worr_native_demo_record_info_v1 record;
  worr_native_demo_result_v1 nested;
  size_t entries_region_bytes = 0;
  size_t cursor;
  uint64_t stream_bytes;
  uint64_t index_capacity_u64;
  uint32_t index_crc = UINT32_MAX;

  if (config == NULL || scan_out == NULL ||
      (encoded_bytes != 0 && encoded == NULL) ||
      (entries_out == NULL && index_capacity != 0)) {
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  }
  if (!scan_config_valid(config))
    return WORR_NATIVE_DEMO_INVALID_METADATA;
  stream_bytes = (uint64_t)encoded_bytes;
  index_capacity_u64 = (uint64_t)index_capacity;
  if ((size_t)stream_bytes != encoded_bytes ||
      (size_t)index_capacity_u64 != index_capacity ||
      config->stream_offset > UINT64_MAX - stream_bytes) {
    return WORR_NATIVE_DEMO_LIMIT;
  }
  if (entries_out != NULL) {
    if (index_capacity > SIZE_MAX / sizeof(*entries_out))
      return WORR_NATIVE_DEMO_LIMIT;
    entries_region_bytes = index_capacity * sizeof(*entries_out);
  }
  if (ranges_overlap(encoded, encoded_bytes, scan_out, sizeof(*scan_out)) ||
      ranges_overlap(config, sizeof(*config), scan_out, sizeof(*scan_out)) ||
      ranges_overlap(entries_out, entries_region_bytes, scan_out,
                     sizeof(*scan_out)) ||
      ranges_overlap(encoded, encoded_bytes, entries_out,
                     entries_region_bytes) ||
      ranges_overlap(config, sizeof(*config), entries_out,
                     entries_region_bytes)) {
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  }

  memset(&scan, 0, sizeof(scan));
  nested =
      Worr_NativeDemoContainerDecodeV1(encoded, encoded_bytes, &scan.container);
  if (nested != WORR_NATIVE_DEMO_OK)
    return nested;
  nested = Worr_NativeDemoOrderInitV1(&scan.container, &scan.order);
  if (nested != WORR_NATIVE_DEMO_OK)
    return nested;

  cursor = WORR_NATIVE_DEMO_WIRE_HEADER_BYTES;
  while (cursor < encoded_bytes) {
    worr_native_demo_index_entry_v1 entry;
    const uint64_t logical_offset = config->stream_offset + (uint64_t)cursor;

    if (scan.record_count == config->max_records)
      return WORR_NATIVE_DEMO_LIMIT;
    nested = Worr_NativeDemoRecordDecodeV1(
        bytes + cursor, encoded_bytes - cursor, logical_offset, &record);
    if (nested != WORR_NATIVE_DEMO_OK)
      return nested;
    nested = codec_result(Worr_NativeCodecValidateV1(
        bytes + cursor + WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES,
        record.payload_bytes, config->max_entities));
    if (nested != WORR_NATIVE_DEMO_OK)
      return nested;
    nested = Worr_NativeDemoOrderObserveV1(&scan.order, &record);
    if (nested != WORR_NATIVE_DEMO_OK)
      return nested;
    nested = Worr_NativeDemoIndexEntryFromRecordV1(&record, &entry);
    if (nested != WORR_NATIVE_DEMO_OK)
      return nested;
    index_crc = index_crc32_update(index_crc, &entry);
    if (record.frame_bytes > encoded_bytes - cursor)
      return WORR_NATIVE_DEMO_INVALID_STATE;
    cursor += record.frame_bytes;
    ++scan.record_count;
    if (record.record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1)
      ++scan.snapshot_count;
  }

  scan.struct_size = sizeof(scan);
  scan.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  scan.flags = WORR_NATIVE_DEMO_SCAN_COMPLETE;
  if (scan.record_count != 0)
    scan.flags |= WORR_NATIVE_DEMO_SCAN_HAS_RECORDS;
  if (scan.snapshot_count != 0)
    scan.flags |= WORR_NATIVE_DEMO_SCAN_HAS_SNAPSHOTS;
  scan.stream_offset = config->stream_offset;
  scan.stream_bytes = stream_bytes;
  scan.next_stream_offset = config->stream_offset + stream_bytes;
  scan.index_crc32 = ~index_crc;

  if (entries_out != NULL && scan.record_count > index_capacity_u64)
    return WORR_NATIVE_DEMO_OUTPUT_TOO_SMALL;

  /* The validated input is required to remain byte-identical for this call. */
  if (entries_out != NULL) {
    uint64_t entry_index = 0;

    cursor = WORR_NATIVE_DEMO_WIRE_HEADER_BYTES;
    while (cursor < encoded_bytes) {
      worr_native_demo_index_entry_v1 entry;
      const uint64_t logical_offset = config->stream_offset + (uint64_t)cursor;

      nested = Worr_NativeDemoRecordDecodeV1(
          bytes + cursor, encoded_bytes - cursor, logical_offset, &record);
      if (nested != WORR_NATIVE_DEMO_OK)
        return WORR_NATIVE_DEMO_INVALID_STATE;
      nested = Worr_NativeDemoIndexEntryFromRecordV1(&record, &entry);
      if (nested != WORR_NATIVE_DEMO_OK)
        return WORR_NATIVE_DEMO_INVALID_STATE;
      entries_out[(size_t)entry_index] = entry;
      cursor += record.frame_bytes;
      ++entry_index;
    }
  }

  *scan_out = scan;
  return WORR_NATIVE_DEMO_OK;
}

static bool seek_query_valid(const worr_native_demo_seek_query_v1 *query) {
  const uint16_t known_flags =
      WORR_NATIVE_DEMO_SEEK_BY_TIME | WORR_NATIVE_DEMO_SEEK_BY_ORDINAL;

  return query->struct_size == sizeof(*query) &&
         query->schema_version == WORR_NATIVE_DEMO_ABI_VERSION &&
         query->flags != 0 && (query->flags & ~known_flags) == 0 &&
         ((query->flags & WORR_NATIVE_DEMO_SEEK_BY_TIME) != 0 ||
          query->target_time_us == 0) &&
         ((query->flags & WORR_NATIVE_DEMO_SEEK_BY_ORDINAL) != 0 ||
          query->target_ordinal == 0) &&
         query->reserved0 == 0;
}

worr_native_demo_result_v1 Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
    const worr_native_demo_scan_info_v1 *scan,
    const worr_native_demo_index_entry_v1 *entries, size_t entry_count,
    const worr_native_demo_seek_query_v1 *query,
    worr_native_demo_seek_result_v1 *result_out) {
  worr_native_demo_order_state_v1 order;
  worr_native_demo_index_entry_v1 selected;
  worr_native_demo_seek_result_v1 result;
  worr_native_demo_result_v1 nested;
  size_t entries_region_bytes = 0;
  uint64_t entry_count_u64;
  uint64_t expected_offset;
  uint64_t selected_index = 0;
  uint64_t snapshot_count = 0;
  uint32_t index_crc = UINT32_MAX;
  size_t index;
  bool found = false;

  if (scan == NULL || query == NULL || result_out == NULL ||
      (entry_count != 0 && entries == NULL)) {
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  }
  entry_count_u64 = (uint64_t)entry_count;
  if ((size_t)entry_count_u64 != entry_count ||
      entry_count > SIZE_MAX / sizeof(*entries)) {
    return WORR_NATIVE_DEMO_LIMIT;
  }
  entries_region_bytes = entry_count * sizeof(*entries);
  if (ranges_overlap(scan, sizeof(*scan), result_out, sizeof(*result_out)) ||
      ranges_overlap(query, sizeof(*query), result_out, sizeof(*result_out)) ||
      ranges_overlap(entries, entries_region_bytes, result_out,
                     sizeof(*result_out))) {
    return WORR_NATIVE_DEMO_INVALID_ARGUMENT;
  }
  if (!scan_info_valid(scan) || !seek_query_valid(query) ||
      entry_count_u64 != scan->record_count) {
    return WORR_NATIVE_DEMO_INVALID_METADATA;
  }

  nested = Worr_NativeDemoOrderInitV1(&scan->container, &order);
  if (nested != WORR_NATIVE_DEMO_OK)
    return WORR_NATIVE_DEMO_INVALID_METADATA;
  expected_offset = scan->stream_offset + WORR_NATIVE_DEMO_WIRE_HEADER_BYTES;
  for (index = 0; index < entry_count; ++index) {
    const worr_native_demo_index_entry_v1 *entry = &entries[index];
    bool matches = true;

    if (!index_entry_valid(entry) || entry->record_offset != expected_offset)
      return WORR_NATIVE_DEMO_INVALID_METADATA;
    index_crc = index_crc32_update(index_crc, entry);
    nested = order_observe_ref(&order, entry->record_kind, entry->ordinal,
                               entry->time_us, entry->record);
    if (nested != WORR_NATIVE_DEMO_OK)
      return nested;
    expected_offset = entry->next_record_offset;
    if (entry->record_kind != WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1)
      continue;
    ++snapshot_count;
    if ((query->flags & WORR_NATIVE_DEMO_SEEK_BY_TIME) != 0 &&
        entry->time_us > query->target_time_us) {
      matches = false;
    }
    if ((query->flags & WORR_NATIVE_DEMO_SEEK_BY_ORDINAL) != 0 &&
        entry->ordinal > query->target_ordinal) {
      matches = false;
    }
    if (matches) {
      selected = *entry;
      selected_index = (uint64_t)index;
      found = true;
    }
  }
  if (expected_offset != scan->next_stream_offset ||
      snapshot_count != scan->snapshot_count ||
      ~index_crc != scan->index_crc32 ||
      !order_state_equal(&order, &scan->order)) {
    return WORR_NATIVE_DEMO_INVALID_METADATA;
  }
  if (!found)
    return WORR_NATIVE_DEMO_NOT_FOUND;

  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.schema_version = WORR_NATIVE_DEMO_ABI_VERSION;
  result.flags = WORR_NATIVE_DEMO_SEEK_FOUND;
  if ((query->flags & WORR_NATIVE_DEMO_SEEK_BY_TIME) != 0)
    result.flags |= WORR_NATIVE_DEMO_SEEK_TIME_CONSTRAINED;
  if ((query->flags & WORR_NATIVE_DEMO_SEEK_BY_ORDINAL) != 0)
    result.flags |= WORR_NATIVE_DEMO_SEEK_ORDINAL_CONSTRAINED;
  result.entry_index = selected_index;
  result.entry = selected;
  *result_out = result;
  return WORR_NATIVE_DEMO_OK;
}
