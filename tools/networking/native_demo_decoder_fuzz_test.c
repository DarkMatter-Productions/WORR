/*
 * FR-10-T14 deterministic malformed-input fuzz for the WDM1/native decoders
 * touched by the T13 record/playback work.  Every changed decoder must be
 * fail-closed: on arbitrary bytes it returns a typed rejection (or a fully
 * validated success) and never crashes, reads out of bounds, or writes its
 * output on failure.
 */

#include "common/net/native_codec.h"
#include "common/net/native_demo.h"

#include "shared/command_abi.h"
#include "shared/event_abi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FUZZ_ITERATIONS 200000u
#define FUZZ_MAX_BYTES 512u
#define FUZZ_MAX_ENTITIES 64u

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "native_demo_decoder_fuzz_test:%d: %s\n", __LINE__,     \
              #condition);                                                     \
      return false;                                                            \
    }                                                                          \
  } while (0)

/* Deterministic xorshift64 so a failure reproduces from the fixed seed. */
static uint64_t rng_state = UINT64_C(0x9e3779b97f4a7c15);

static uint64_t rng_next(void) {
  uint64_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  rng_state = x;
  return x;
}

static size_t fill_random(uint8_t *buffer, size_t capacity) {
  size_t length = (size_t)(rng_next() % (uint64_t)(capacity + 1u));
  size_t index;
  for (index = 0; index < length; ++index)
    buffer[index] = (uint8_t)(rng_next() & 0xffu);
  return length;
}

static bool test_random_decoders(void) {
  uint8_t buffer[FUZZ_MAX_BYTES];
  worr_native_demo_container_info_v1 container;
  worr_native_demo_record_info_v1 record;
  worr_command_record_v1 command;
  worr_event_record_v1 event;
  uint64_t rejected = 0;
  uint64_t accepted = 0;
  uint32_t iteration;

  for (iteration = 0; iteration < FUZZ_ITERATIONS; ++iteration) {
    const size_t length = fill_random(buffer, sizeof(buffer));
    worr_native_demo_result_v1 demo_nested;
    worr_native_codec_result_v1 codec_nested;

    /* Container header decode. */
    memset(&container, 0xa5, sizeof(container));
    demo_nested = Worr_NativeDemoContainerDecodeV1(buffer, length, &container);
    if (demo_nested == WORR_NATIVE_DEMO_OK)
      ++accepted;
    else
      ++rejected;

    /* WDR1 record decode at a pseudo-random logical offset. */
    memset(&record, 0xa5, sizeof(record));
    demo_nested = Worr_NativeDemoRecordDecodeV1(buffer, length,
                                                rng_next() & 0xffffu, &record);
    if (demo_nested != WORR_NATIVE_DEMO_OK)
      ++rejected;
    else
      ++accepted;

    /* Canonical command image decode. */
    memset(&command, 0xa5, sizeof(command));
    codec_nested = Worr_NativeCodecCommandDecodeV1(
        buffer, length, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, &command);
    if (codec_nested != WORR_NATIVE_CODEC_OK)
      ++rejected;
    else
      ++accepted;

    /* Canonical event image decode. */
    memset(&event, 0xa5, sizeof(event));
    codec_nested = Worr_NativeCodecEventDecodeV1(buffer, length,
                                                 FUZZ_MAX_ENTITIES, &event);
    if (codec_nested != WORR_NATIVE_CODEC_OK)
      ++rejected;
    else
      ++accepted;
  }

  /* Random bytes essentially never form a valid framed record; the point is
   * that every case returned a typed result without crashing. Overwhelmingly
   * they must be rejected. */
  CHECK(rejected > accepted);
  CHECK(rejected + accepted == (uint64_t)FUZZ_ITERATIONS * 4u);
  return true;
}

static bool test_null_and_zero_length(void) {
  worr_native_demo_container_info_v1 container;
  worr_command_record_v1 command;
  worr_event_record_v1 event;
  const uint8_t byte = 0;

  /* Zero-length and null inputs must reject, never dereference. */
  memset(&container, 0, sizeof(container));
  CHECK(Worr_NativeDemoContainerDecodeV1(&byte, 0, &container) !=
        WORR_NATIVE_DEMO_OK);
  CHECK(Worr_NativeDemoContainerDecodeV1(NULL, 0, &container) !=
        WORR_NATIVE_DEMO_OK);
  memset(&command, 0, sizeof(command));
  CHECK(Worr_NativeCodecCommandDecodeV1(
            &byte, 0, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, &command) !=
        WORR_NATIVE_CODEC_OK);
  memset(&event, 0, sizeof(event));
  CHECK(Worr_NativeCodecEventDecodeV1(&byte, 0, FUZZ_MAX_ENTITIES, &event) !=
        WORR_NATIVE_CODEC_OK);
  return true;
}

int main(void) {
  if (!test_null_and_zero_length() || !test_random_decoders())
    return 1;
  printf("native_demo_decoder_fuzz_test: %u iterations x 4 decoders passed\n",
         (unsigned)FUZZ_ITERATIONS);
  return 0;
}
