/*
 * FR-10-T13 native-demo bootstrap payload: proves the map-load gamestate
 * (serverdata identity + configstrings) round-trips byte-exactly and that
 * malformed / truncated / reordered / over-capacity inputs fail closed. Pure
 * core: opens no files, drives no client.
 */

#include "common/net/native_demo_bootstrap.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "native_demo_bootstrap_test:%d: %s\n", __LINE__,         \
              #condition);                                                     \
      return false;                                                            \
    }                                                                          \
  } while (0)

#define TEST_STRINGS 5u
#define TEST_CAP 32u

static uint8_t encoded[WORR_NATIVE_DEMO_BOOTSTRAP_MAX_ENCODED_BYTES];

static void make_header(worr_native_demo_bootstrap_header_v1 *h) {
  memset(h, 0, sizeof(*h));
  h->struct_size = sizeof(*h);
  h->schema_version = WORR_NATIVE_DEMO_BOOTSTRAP_ABI_VERSION;
  h->protocol = 2023;
  h->protocol_version = 3;
  h->game_api = 1;
  h->server_count = 42;
  h->client_num = 3;
  h->csr_variant = 2;
  h->max_stats = 64;
  h->es_flags = 0x11;
  h->ps_flags = 0x22;
  h->map_checksum = 0xDEADBEEFu;
  h->configstring_count = TEST_STRINGS;
  memcpy(h->gamedir, "baseq2", sizeof("baseq2"));
  memcpy(h->mapname, "maps/q2dm1.bsp", sizeof("maps/q2dm1.bsp"));
}

static void make_strings(worr_native_demo_bootstrap_configstring_v1 *s) {
  uint16_t i;
  memset(s, 0, sizeof(*s) * TEST_STRINGS);
  for (i = 0; i < TEST_STRINGS; ++i) {
    /* Strictly increasing indices; varied lengths incl. an empty one. */
    s[i].index = (uint16_t)(1 + i * 7);
    if (i == 2) {
      s[i].length = 0;
    } else {
      char buf[WORR_NATIVE_DEMO_BOOTSTRAP_MAX_STRING];
      int n = snprintf(buf, sizeof(buf), "models/cs_%u.md2", (unsigned)i);
      s[i].length = (uint16_t)n;
      memcpy(s[i].value, buf, (size_t)n);
    }
  }
}

static bool headers_equal(const worr_native_demo_bootstrap_header_v1 *a,
                          const worr_native_demo_bootstrap_header_v1 *b) {
  return memcmp(a, b, sizeof(*a)) == 0;
}

static bool strings_equal(const worr_native_demo_bootstrap_configstring_v1 *a,
                          const worr_native_demo_bootstrap_configstring_v1 *b,
                          uint32_t count) {
  return memcmp(a, b, sizeof(*a) * count) == 0;
}

static bool test_roundtrip(void) {
  worr_native_demo_bootstrap_header_v1 header;
  worr_native_demo_bootstrap_configstring_v1 strings[TEST_STRINGS];
  worr_native_demo_bootstrap_header_v1 decoded_header;
  worr_native_demo_bootstrap_configstring_v1 decoded[TEST_CAP];
  size_t pre = 0;
  size_t out = 0;

  make_header(&header);
  make_strings(strings);

  /* Preflight equals encode size and equals the header + per-string wire cost. */
  CHECK(Worr_NativeDemoBootstrapPreflightV1(&header, strings, &pre) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_OK);
  CHECK(Worr_NativeDemoBootstrapEncodeV1(&header, strings, encoded,
                                         sizeof(encoded), &out) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_OK);
  CHECK(out == pre);

  /* Decode reproduces the header and every configstring byte-exactly. */
  memset(&decoded_header, 0, sizeof(decoded_header));
  memset(decoded, 0, sizeof(decoded));
  CHECK(Worr_NativeDemoBootstrapDecodeV1(encoded, out, &decoded_header, decoded,
                                         TEST_CAP) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_OK);
  CHECK(headers_equal(&decoded_header, &header));
  CHECK(decoded_header.configstring_count == TEST_STRINGS);
  CHECK(strings_equal(decoded, strings, TEST_STRINGS));

  /* A header-only bootstrap (no configstrings) round-trips too. */
  header.configstring_count = 0;
  CHECK(Worr_NativeDemoBootstrapEncodeV1(&header, NULL, encoded,
                                         sizeof(encoded), &out) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_OK);
  CHECK(out == WORR_NATIVE_DEMO_BOOTSTRAP_HEADER_WIRE_BYTES);
  CHECK(Worr_NativeDemoBootstrapDecodeV1(encoded, out, &decoded_header, decoded,
                                         TEST_CAP) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_OK);
  CHECK(decoded_header.configstring_count == 0);
  return true;
}

static bool test_capacity(void) {
  worr_native_demo_bootstrap_header_v1 header;
  worr_native_demo_bootstrap_configstring_v1 strings[TEST_STRINGS];
  worr_native_demo_bootstrap_header_v1 decoded_header;
  worr_native_demo_bootstrap_configstring_v1 decoded[TEST_STRINGS - 1];
  size_t out = 0;

  make_header(&header);
  make_strings(strings);
  CHECK(Worr_NativeDemoBootstrapEncodeV1(&header, strings, encoded,
                                         sizeof(encoded), &out) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_OK);
  /* One slot short reports the required count and writes no configstring. */
  memset(&decoded_header, 0xAB, sizeof(decoded_header));
  CHECK(Worr_NativeDemoBootstrapDecodeV1(encoded, out, &decoded_header, decoded,
                                         TEST_STRINGS - 1) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_CAPACITY);
  CHECK(decoded_header.configstring_count == TEST_STRINGS);

  /* Output buffer one byte short fails closed. */
  CHECK(Worr_NativeDemoBootstrapEncodeV1(&header, strings, encoded, out - 1,
                                         &out) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_OUTPUT_TOO_SMALL);
  return true;
}

static bool test_malformed(void) {
  worr_native_demo_bootstrap_header_v1 header;
  worr_native_demo_bootstrap_configstring_v1 strings[TEST_STRINGS];
  worr_native_demo_bootstrap_header_v1 decoded_header;
  worr_native_demo_bootstrap_configstring_v1 decoded[TEST_CAP];
  size_t out = 0;

  make_header(&header);
  make_strings(strings);

  /* Non-increasing indices are rejected at encode. */
  strings[3].index = strings[2].index;
  CHECK(Worr_NativeDemoBootstrapEncodeV1(&header, strings, encoded,
                                         sizeof(encoded), &out) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_MALFORMED);
  make_strings(strings);

  /* Over-range configstring length is rejected. */
  strings[1].length = WORR_NATIVE_DEMO_BOOTSTRAP_MAX_STRING + 1;
  CHECK(Worr_NativeDemoBootstrapEncodeV1(&header, strings, encoded,
                                         sizeof(encoded), &out) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_MALFORMED);
  make_strings(strings);

  /* A non-terminated gamedir is rejected. */
  memset(header.gamedir, 'x', sizeof(header.gamedir));
  CHECK(Worr_NativeDemoBootstrapEncodeV1(&header, strings, encoded,
                                         sizeof(encoded), &out) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_INVALID_METADATA);
  make_header(&header);

  /* Encode a valid stream, then corrupt it for decode-side checks. */
  CHECK(Worr_NativeDemoBootstrapEncodeV1(&header, strings, encoded,
                                         sizeof(encoded), &out) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_OK);

  /* Truncation past the header fails closed. */
  CHECK(Worr_NativeDemoBootstrapDecodeV1(encoded, out - 1, &decoded_header,
                                         decoded, TEST_CAP) !=
        WORR_NATIVE_DEMO_BOOTSTRAP_OK);
  /* A short header fails closed. */
  CHECK(Worr_NativeDemoBootstrapDecodeV1(
            encoded, WORR_NATIVE_DEMO_BOOTSTRAP_HEADER_WIRE_BYTES - 1,
            &decoded_header, decoded, TEST_CAP) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_TRUNCATED);
  /* A trailing byte (claimed length shorter than the buffer) fails closed. */
  CHECK(Worr_NativeDemoBootstrapDecodeV1(encoded, out + 1, &decoded_header,
                                         decoded, TEST_CAP) ==
        WORR_NATIVE_DEMO_BOOTSTRAP_MALFORMED);
  return true;
}

int main(void) {
  if (!test_roundtrip())
    return 1;
  if (!test_capacity())
    return 1;
  if (!test_malformed())
    return 1;
  puts("native_demo_bootstrap_test: all checks passed");
  return 0;
}
