/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_demo_bootstrap.h"

#include <stdbool.h>
#include <string.h>

/* Little-endian primitives; no dependency on struct layout matching the wire. */
static void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xffu);
  p[1] = (uint8_t)((v >> 8) & 0xffu);
}
static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xffu);
  p[1] = (uint8_t)((v >> 8) & 0xffu);
  p[2] = (uint8_t)((v >> 16) & 0xffu);
  p[3] = (uint8_t)((v >> 24) & 0xffu);
}
static uint16_t get_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/* A fixed char field must contain a NUL within its bounds. */
static bool field_terminated(const char *field, size_t size) {
  size_t i;
  for (i = 0; i < size; ++i) {
    if (field[i] == '\0')
      return true;
  }
  return false;
}

static bool header_shape_valid(
    const worr_native_demo_bootstrap_header_v1 *header) {
  return header != NULL &&
         header->struct_size == sizeof(*header) &&
         header->schema_version == WORR_NATIVE_DEMO_BOOTSTRAP_ABI_VERSION &&
         header->reserved0 == 0 &&
         header->configstring_count <=
             WORR_NATIVE_DEMO_BOOTSTRAP_MAX_CONFIGSTRINGS &&
         field_terminated(header->gamedir, sizeof(header->gamedir)) &&
         field_terminated(header->mapname, sizeof(header->mapname));
}

/* Validates the configstring table and, on success, writes the exact serialized
 * size to *total_out.  Requires strictly increasing indices, in-range lengths,
 * and a total within the hard cap. */
static worr_native_demo_bootstrap_result_v1 validate_table(
    const worr_native_demo_bootstrap_header_v1 *header,
    const worr_native_demo_bootstrap_configstring_v1 *strings,
    size_t *total_out) {
  uint64_t total = WORR_NATIVE_DEMO_BOOTSTRAP_HEADER_WIRE_BYTES;
  uint32_t i;
  bool have_prev = false;
  uint16_t prev_index = 0;

  if (header->configstring_count != 0 && strings == NULL)
    return WORR_NATIVE_DEMO_BOOTSTRAP_INVALID_ARGUMENT;

  for (i = 0; i < header->configstring_count; ++i) {
    const worr_native_demo_bootstrap_configstring_v1 *entry = &strings[i];
    if (entry->length > WORR_NATIVE_DEMO_BOOTSTRAP_MAX_STRING ||
        entry->index >= WORR_NATIVE_DEMO_BOOTSTRAP_MAX_CONFIGSTRINGS)
      return WORR_NATIVE_DEMO_BOOTSTRAP_MALFORMED;
    if (have_prev && entry->index <= prev_index)
      return WORR_NATIVE_DEMO_BOOTSTRAP_MALFORMED;
    prev_index = entry->index;
    have_prev = true;
    total += WORR_NATIVE_DEMO_BOOTSTRAP_CONFIGSTRING_WIRE_PREFIX + entry->length;
    if (total > WORR_NATIVE_DEMO_BOOTSTRAP_MAX_ENCODED_BYTES)
      return WORR_NATIVE_DEMO_BOOTSTRAP_LIMIT;
  }
  *total_out = (size_t)total;
  return WORR_NATIVE_DEMO_BOOTSTRAP_OK;
}

worr_native_demo_bootstrap_result_v1 Worr_NativeDemoBootstrapPreflightV1(
    const worr_native_demo_bootstrap_header_v1 *header,
    const worr_native_demo_bootstrap_configstring_v1 *strings,
    size_t *encoded_bytes_out) {
  size_t total = 0;
  worr_native_demo_bootstrap_result_v1 result;

  if (encoded_bytes_out == NULL)
    return WORR_NATIVE_DEMO_BOOTSTRAP_INVALID_ARGUMENT;
  if (!header_shape_valid(header))
    return WORR_NATIVE_DEMO_BOOTSTRAP_INVALID_METADATA;
  result = validate_table(header, strings, &total);
  if (result != WORR_NATIVE_DEMO_BOOTSTRAP_OK)
    return result;
  *encoded_bytes_out = total;
  return WORR_NATIVE_DEMO_BOOTSTRAP_OK;
}

static void write_header(uint8_t *p,
                         const worr_native_demo_bootstrap_header_v1 *h) {
  put_u32(p + 0, h->struct_size);
  put_u32(p + 4, h->schema_version);
  put_u32(p + 8, h->protocol);
  put_u32(p + 12, h->protocol_version);
  put_u32(p + 16, h->game_api);
  put_u32(p + 20, h->server_count);
  put_u32(p + 24, (uint32_t)h->client_num);
  put_u32(p + 28, h->csr_variant);
  put_u32(p + 32, h->max_stats);
  put_u32(p + 36, h->es_flags);
  put_u32(p + 40, h->ps_flags);
  put_u32(p + 44, h->map_checksum);
  put_u32(p + 48, h->configstring_count);
  put_u32(p + 52, h->reserved0);
  memcpy(p + 56, h->gamedir, sizeof(h->gamedir));
  memcpy(p + 120, h->mapname, sizeof(h->mapname));
}

worr_native_demo_bootstrap_result_v1 Worr_NativeDemoBootstrapEncodeV1(
    const worr_native_demo_bootstrap_header_v1 *header,
    const worr_native_demo_bootstrap_configstring_v1 *strings,
    void *encoded_out, size_t encoded_capacity, size_t *encoded_bytes_out) {
  size_t total = 0;
  worr_native_demo_bootstrap_result_v1 result;
  uint8_t *out = (uint8_t *)encoded_out;
  size_t offset;
  uint32_t i;

  if (encoded_out == NULL || encoded_bytes_out == NULL)
    return WORR_NATIVE_DEMO_BOOTSTRAP_INVALID_ARGUMENT;
  if (!header_shape_valid(header))
    return WORR_NATIVE_DEMO_BOOTSTRAP_INVALID_METADATA;
  result = validate_table(header, strings, &total);
  if (result != WORR_NATIVE_DEMO_BOOTSTRAP_OK)
    return result;
  if (total > encoded_capacity)
    return WORR_NATIVE_DEMO_BOOTSTRAP_OUTPUT_TOO_SMALL;

  write_header(out, header);
  offset = WORR_NATIVE_DEMO_BOOTSTRAP_HEADER_WIRE_BYTES;
  for (i = 0; i < header->configstring_count; ++i) {
    const worr_native_demo_bootstrap_configstring_v1 *entry = &strings[i];
    put_u16(out + offset, entry->index);
    put_u16(out + offset + 2, entry->length);
    if (entry->length != 0)
      memcpy(out + offset + 4, entry->value, entry->length);
    offset += WORR_NATIVE_DEMO_BOOTSTRAP_CONFIGSTRING_WIRE_PREFIX +
              entry->length;
  }
  *encoded_bytes_out = total;
  return WORR_NATIVE_DEMO_BOOTSTRAP_OK;
}

/* Reads and validates the header from the wire; does not touch header_out until
 * validated. */
static worr_native_demo_bootstrap_result_v1 read_header(
    const uint8_t *bytes, size_t encoded_bytes,
    worr_native_demo_bootstrap_header_v1 *header_out) {
  worr_native_demo_bootstrap_header_v1 header;

  if (encoded_bytes < WORR_NATIVE_DEMO_BOOTSTRAP_HEADER_WIRE_BYTES)
    return WORR_NATIVE_DEMO_BOOTSTRAP_TRUNCATED;
  memset(&header, 0, sizeof(header));
  header.struct_size = get_u32(bytes + 0);
  header.schema_version = get_u32(bytes + 4);
  header.protocol = get_u32(bytes + 8);
  header.protocol_version = get_u32(bytes + 12);
  header.game_api = get_u32(bytes + 16);
  header.server_count = get_u32(bytes + 20);
  header.client_num = (int32_t)get_u32(bytes + 24);
  header.csr_variant = get_u32(bytes + 28);
  header.max_stats = get_u32(bytes + 32);
  header.es_flags = get_u32(bytes + 36);
  header.ps_flags = get_u32(bytes + 40);
  header.map_checksum = get_u32(bytes + 44);
  header.configstring_count = get_u32(bytes + 48);
  header.reserved0 = get_u32(bytes + 52);
  memcpy(header.gamedir, bytes + 56, sizeof(header.gamedir));
  memcpy(header.mapname, bytes + 120, sizeof(header.mapname));
  if (!header_shape_valid(&header))
    return WORR_NATIVE_DEMO_BOOTSTRAP_MALFORMED;
  *header_out = header;
  return WORR_NATIVE_DEMO_BOOTSTRAP_OK;
}

/* Walks the configstring table on the wire, validating each entry and the exact
 * total consumption.  Optionally fills strings_out (when non-null and large
 * enough).  Never writes past strings_capacity; returns CAPACITY if the table
 * needs more entries than capacity. */
static worr_native_demo_bootstrap_result_v1 walk_table(
    const uint8_t *bytes, size_t encoded_bytes, uint32_t count,
    worr_native_demo_bootstrap_configstring_v1 *strings_out,
    size_t strings_capacity) {
  size_t offset = WORR_NATIVE_DEMO_BOOTSTRAP_HEADER_WIRE_BYTES;
  uint32_t i;
  bool have_prev = false;
  uint16_t prev_index = 0;

  if (strings_out != NULL && (uint64_t)count > (uint64_t)strings_capacity)
    return WORR_NATIVE_DEMO_BOOTSTRAP_CAPACITY;

  for (i = 0; i < count; ++i) {
    uint16_t index;
    uint16_t length;
    if (offset + WORR_NATIVE_DEMO_BOOTSTRAP_CONFIGSTRING_WIRE_PREFIX >
        encoded_bytes)
      return WORR_NATIVE_DEMO_BOOTSTRAP_TRUNCATED;
    index = get_u16(bytes + offset);
    length = get_u16(bytes + offset + 2);
    if (length > WORR_NATIVE_DEMO_BOOTSTRAP_MAX_STRING ||
        index >= WORR_NATIVE_DEMO_BOOTSTRAP_MAX_CONFIGSTRINGS)
      return WORR_NATIVE_DEMO_BOOTSTRAP_MALFORMED;
    if (have_prev && index <= prev_index)
      return WORR_NATIVE_DEMO_BOOTSTRAP_MALFORMED;
    prev_index = index;
    have_prev = true;
    offset += WORR_NATIVE_DEMO_BOOTSTRAP_CONFIGSTRING_WIRE_PREFIX;
    if (offset + length > encoded_bytes)
      return WORR_NATIVE_DEMO_BOOTSTRAP_TRUNCATED;
    if (strings_out != NULL) {
      worr_native_demo_bootstrap_configstring_v1 *dst = &strings_out[i];
      memset(dst, 0, sizeof(*dst));
      dst->index = index;
      dst->length = length;
      if (length != 0)
        memcpy(dst->value, bytes + offset, length);
    }
    offset += length;
  }
  /* No trailing bytes: the stream must be exactly consumed. */
  if (offset != encoded_bytes)
    return WORR_NATIVE_DEMO_BOOTSTRAP_MALFORMED;
  return WORR_NATIVE_DEMO_BOOTSTRAP_OK;
}

worr_native_demo_bootstrap_result_v1 Worr_NativeDemoBootstrapDecodeV1(
    const void *encoded, size_t encoded_bytes,
    worr_native_demo_bootstrap_header_v1 *header_out,
    worr_native_demo_bootstrap_configstring_v1 *strings_out,
    size_t strings_capacity) {
  const uint8_t *bytes = (const uint8_t *)encoded;
  worr_native_demo_bootstrap_header_v1 header;
  worr_native_demo_bootstrap_result_v1 result;

  if (encoded == NULL || header_out == NULL || encoded_bytes == 0)
    return WORR_NATIVE_DEMO_BOOTSTRAP_INVALID_ARGUMENT;
  if (encoded_bytes > WORR_NATIVE_DEMO_BOOTSTRAP_MAX_ENCODED_BYTES)
    return WORR_NATIVE_DEMO_BOOTSTRAP_LIMIT;

  result = read_header(bytes, encoded_bytes, &header);
  if (result != WORR_NATIVE_DEMO_BOOTSTRAP_OK)
    return result;

  /* Capacity check before any output write (transactional). */
  if (strings_out != NULL &&
      (uint64_t)header.configstring_count > (uint64_t)strings_capacity) {
    memset(header_out, 0, sizeof(*header_out));
    header_out->configstring_count = header.configstring_count;
    return WORR_NATIVE_DEMO_BOOTSTRAP_CAPACITY;
  }

  /* Pass 1: validate the whole table without writing outputs. */
  result = walk_table(bytes, encoded_bytes, header.configstring_count, NULL, 0);
  if (result != WORR_NATIVE_DEMO_BOOTSTRAP_OK)
    return result;

  /* Pass 2: fill outputs now that the stream is proven. */
  if (strings_out != NULL) {
    result = walk_table(bytes, encoded_bytes, header.configstring_count,
                        strings_out, strings_capacity);
    if (result != WORR_NATIVE_DEMO_BOOTSTRAP_OK)
      return result;
  }
  *header_out = header;
  return WORR_NATIVE_DEMO_BOOTSTRAP_OK;
}
