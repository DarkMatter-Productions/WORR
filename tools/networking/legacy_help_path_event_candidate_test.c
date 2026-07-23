/* Exact direct svc_help_path decode and canonical mapping coverage. */

#include "common/net/legacy_help_path_event_candidate.h"

#include "common/intreadwrite.h"
#include "shared/shared.h"
#include "common/math.h"
#include "common/protocol.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,       \
              #condition);                                                      \
      return 1;                                                                 \
    }                                                                           \
  } while (0)

static void write_float(uint8_t *bytes, float value)
{
  uint32_t bits;

  memcpy(&bits, &value, sizeof(bits));
  WL32(bytes, bits);
}

static void make_raw(uint8_t raw[15], uint8_t start, uint8_t direction)
{
  memset(raw, 0, 15);
  raw[0] = svc_help_path;
  raw[1] = start;
  write_float(&raw[2], 12.5f);
  write_float(&raw[6], -24.25f);
  write_float(&raw[10], 96.0f);
  raw[14] = direction;
}

static int test_exact_raw_decode(void)
{
  uint8_t raw[15];
  q2proto_svc_help_path_t help_path;

  make_raw(raw, 1, 42);
  memset(&help_path, 0xa5, sizeof(help_path));
  CHECK(Worr_LegacyHelpPathEventDecodeRawV1(
            raw, sizeof(raw), &help_path) ==
        WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_OK);
  CHECK(help_path.start && help_path.pos[0] == 12.5f &&
        help_path.pos[1] == -24.25f && help_path.pos[2] == 96.0f &&
        help_path.dir[0] == bytedirs[42][0] &&
        help_path.dir[1] == bytedirs[42][1] &&
        help_path.dir[2] == bytedirs[42][2]);
  return 0;
}

static int expect_atomic_decode_failure(const uint8_t *raw, size_t size)
{
  q2proto_svc_help_path_t help_path;
  q2proto_svc_help_path_t before;

  memset(&help_path, 0xa5, sizeof(help_path));
  before = help_path;
  CHECK(Worr_LegacyHelpPathEventDecodeRawV1(raw, size, &help_path) ==
        WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(&help_path, &before, sizeof(help_path)) == 0);
  return 0;
}

static int test_raw_rejection_is_atomic(void)
{
  uint8_t raw[16];

  make_raw(raw, 0, 7);
  raw[0] = svc_nop;
  CHECK(expect_atomic_decode_failure(raw, 15) == 0);

  make_raw(raw, 2, 7);
  CHECK(expect_atomic_decode_failure(raw, 15) == 0);

  make_raw(raw, 0, NUMVERTEXNORMALS);
  CHECK(expect_atomic_decode_failure(raw, 15) == 0);

  make_raw(raw, 0, 7);
  CHECK(expect_atomic_decode_failure(raw, 14) == 0);
  raw[15] = svc_nop;
  CHECK(expect_atomic_decode_failure(raw, 16) == 0);

  make_raw(raw, 0, 7);
  write_float(&raw[6], NAN);
  CHECK(expect_atomic_decode_failure(raw, 15) == 0);
  return 0;
}

static int test_canonical_candidate_and_atomic_failure(void)
{
  q2proto_svc_help_path_t help_path;
  worr_event_record_v1 candidate;
  worr_event_record_v1 before;
  worr_event_payload_effect_v1 payload;

  memset(&help_path, 0, sizeof(help_path));
  help_path.start = true;
  help_path.pos[0] = 12.5f;
  help_path.pos[1] = -24.25f;
  help_path.pos[2] = 96.0f;
  memcpy(help_path.dir, bytedirs[42], sizeof(help_path.dir));

  CHECK(Worr_LegacyHelpPathEventCandidateBuildV1(
            &help_path, 17, UINT64_C(425000), 64, &candidate) ==
        WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_OK);
  CHECK(candidate.source_tick == 17 &&
        candidate.source_time_us == UINT64_C(425000) &&
        candidate.source_entity.index == 0 &&
        candidate.source_entity.generation == 1 &&
        candidate.subject_entity.index == WORR_EVENT_NO_ENTITY &&
        candidate.event_type == WORR_EVENT_TYPE_VISUAL_EFFECT &&
        candidate.delivery_class == WORR_EVENT_DELIVERY_TRANSIENT &&
        candidate.prediction_class ==
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY &&
        candidate.expiry_tick == 18 &&
        candidate.payload_kind == WORR_EVENT_PAYLOAD_EFFECT &&
        Worr_EventRecordCandidateValidateV1(&candidate, 64));
  memcpy(&payload, candidate.payload, sizeof(payload));
  CHECK(payload.effect_id == WORR_EVENT_EFFECT_HELP_PATH_MARKER &&
        payload.variant == WORR_EVENT_HELP_PATH_VARIANT_START &&
        payload.origin[0] == 12.5f && payload.origin[1] == -24.25f &&
        payload.origin[2] == 96.0f &&
        payload.direction[0] == bytedirs[42][0]);

  memset(&candidate, 0xa5, sizeof(candidate));
  before = candidate;
  help_path.pos[0] = NAN;
  CHECK(Worr_LegacyHelpPathEventCandidateBuildV1(
            &help_path, 17, UINT64_C(425000), 64, &candidate) ==
        WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_INVALID_RECORD);
  CHECK(memcmp(&candidate, &before, sizeof(candidate)) == 0);
  return 0;
}

int main(void)
{
  if (test_exact_raw_decode() != 0 || test_raw_rejection_is_atomic() != 0 ||
      test_canonical_candidate_and_atomic_failure() != 0) {
    return EXIT_FAILURE;
  }
  puts("legacy help-path event candidate tests passed");
  return EXIT_SUCCESS;
}
