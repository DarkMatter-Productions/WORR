/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FR-10-T13 native-demo bootstrap payload (default-off).
 *
 * A native (WDM1) demo has no legacy serverdata packet, so to start a playable
 * session without a live connection it must carry the map-load gamestate itself:
 * the serverdata identity (protocol / gamedir / clientNum / configstring-remap)
 * plus the configstrings the client applies before loading the map and
 * initializing cgame (the world-model configstring loads the BSP; model / sound
 * / image / skin configstrings resolve the indices snapshots reference).
 *
 * This is the pointer-free serialization of that payload only.  It carries no
 * spawn baselines: the first snapshot in the stream already supplies entity
 * state.  It opens no files and touches no live client; the WDM1 record-class
 * wrapper, the recorder capture, and the client map-load path consume it.
 * Every wire integer is little-endian.
 */
#define WORR_NATIVE_DEMO_BOOTSTRAP_ABI_VERSION 1u

#define WORR_NATIVE_DEMO_BOOTSTRAP_MAX_GAMEDIR 64u
#define WORR_NATIVE_DEMO_BOOTSTRAP_MAX_MAPNAME 64u
/* CS_MAX_STRING_LENGTH in the client. */
#define WORR_NATIVE_DEMO_BOOTSTRAP_MAX_STRING 96u
/* Bounds the configstring table (an index fits u16; the client's
 * MAX_CONFIGSTRINGS is well under this). */
#define WORR_NATIVE_DEMO_BOOTSTRAP_MAX_CONFIGSTRINGS 8192u
/* Fixed header wire size; see the static_asserts below. */
#define WORR_NATIVE_DEMO_BOOTSTRAP_HEADER_WIRE_BYTES 184u
/* Per-configstring wire prefix: u16 index + u16 length. */
#define WORR_NATIVE_DEMO_BOOTSTRAP_CONFIGSTRING_WIRE_PREFIX 4u
/* Hard cap on a complete serialized bootstrap (keeps it well under a codec
 * payload). */
#define WORR_NATIVE_DEMO_BOOTSTRAP_MAX_ENCODED_BYTES (1024u * 1024u)

typedef enum worr_native_demo_bootstrap_result_v1_e {
  WORR_NATIVE_DEMO_BOOTSTRAP_OK = 0,
  WORR_NATIVE_DEMO_BOOTSTRAP_INVALID_ARGUMENT = 1,
  WORR_NATIVE_DEMO_BOOTSTRAP_INVALID_METADATA = 2,
  WORR_NATIVE_DEMO_BOOTSTRAP_OUTPUT_TOO_SMALL = 3,
  WORR_NATIVE_DEMO_BOOTSTRAP_TRUNCATED = 4,
  WORR_NATIVE_DEMO_BOOTSTRAP_MALFORMED = 5,
  WORR_NATIVE_DEMO_BOOTSTRAP_LIMIT = 6,
  WORR_NATIVE_DEMO_BOOTSTRAP_CAPACITY = 7,
} worr_native_demo_bootstrap_result_v1;

/* Fixed serverdata-identity header (mirrors what CL_ParseServerData installs). */
typedef struct worr_native_demo_bootstrap_header_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t protocol;
  uint32_t protocol_version;
  uint32_t game_api;
  uint32_t server_count;
  int32_t client_num;
  uint32_t csr_variant; /* configstring-remap identity (old/q2pro/rerelease) */
  uint32_t max_stats;
  uint32_t es_flags;
  uint32_t ps_flags;
  uint32_t map_checksum;
  uint32_t configstring_count;
  uint32_t reserved0;
  char gamedir[WORR_NATIVE_DEMO_BOOTSTRAP_MAX_GAMEDIR];
  char mapname[WORR_NATIVE_DEMO_BOOTSTRAP_MAX_MAPNAME];
} worr_native_demo_bootstrap_header_v1;

/* One populated configstring: value[0..length) with length <= MAX_STRING. */
typedef struct worr_native_demo_bootstrap_configstring_v1_s {
  uint16_t index;
  uint16_t length;
  char value[WORR_NATIVE_DEMO_BOOTSTRAP_MAX_STRING];
} worr_native_demo_bootstrap_configstring_v1;

/*
 * Computes the exact serialized size for a header + configstring table.  Fails
 * on a malformed header/table (bad struct_size/schema, count over the cap, an
 * out-of-range configstring length, a NUL-unterminated gamedir/mapname, or a
 * total over the hard cap).
 */
worr_native_demo_bootstrap_result_v1 Worr_NativeDemoBootstrapPreflightV1(
    const worr_native_demo_bootstrap_header_v1 *header,
    const worr_native_demo_bootstrap_configstring_v1 *strings,
    size_t *encoded_bytes_out);

/*
 * Serializes header + configstrings into encoded_out.  Transactional: on any
 * failure encoded_out and *encoded_bytes_out are untouched.  strings must hold
 * header->configstring_count entries; each entry's length <= MAX_STRING and
 * index < MAX_CONFIGSTRINGS, and indices must be strictly increasing (canonical
 * order, so a decoder can reject duplicates/reordering).
 */
worr_native_demo_bootstrap_result_v1 Worr_NativeDemoBootstrapEncodeV1(
    const worr_native_demo_bootstrap_header_v1 *header,
    const worr_native_demo_bootstrap_configstring_v1 *strings,
    void *encoded_out, size_t encoded_capacity, size_t *encoded_bytes_out);

/*
 * Decodes a serialized bootstrap into header_out + strings_out (capacity
 * strings_capacity entries).  Transactional: every output is untouched on
 * failure.  Rejects truncation, an over-cap count, out-of-range lengths/indices,
 * non-increasing indices, and trailing bytes.  On CAPACITY the required entry
 * count is written to header_out->configstring_count (only) so a caller can
 * resize and retry.
 */
worr_native_demo_bootstrap_result_v1 Worr_NativeDemoBootstrapDecodeV1(
    const void *encoded, size_t encoded_bytes,
    worr_native_demo_bootstrap_header_v1 *header_out,
    worr_native_demo_bootstrap_configstring_v1 *strings_out,
    size_t strings_capacity);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_DEMO_BOOTSTRAP_STATIC_ASSERT(condition, message)           \
  static_assert((condition), message)
#else
#define WORR_NATIVE_DEMO_BOOTSTRAP_STATIC_ASSERT(condition, message)           \
  _Static_assert((condition), message)
#endif

WORR_NATIVE_DEMO_BOOTSTRAP_STATIC_ASSERT(
    sizeof(worr_native_demo_bootstrap_header_v1) == 184,
    "native demo bootstrap header layout changed");
WORR_NATIVE_DEMO_BOOTSTRAP_STATIC_ASSERT(
    sizeof(worr_native_demo_bootstrap_configstring_v1) == 100,
    "native demo bootstrap configstring layout changed");

#undef WORR_NATIVE_DEMO_BOOTSTRAP_STATIC_ASSERT
