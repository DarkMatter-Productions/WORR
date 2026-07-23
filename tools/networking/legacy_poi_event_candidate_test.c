/* Exact direct svc_poi decode and canonical keyed-state mapping coverage. */

#include "common/net/legacy_poi_event_candidate.h"

#include "common/intreadwrite.h"
#include "shared/shared.h"
#include "common/protocol.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                      \
  do {                                                                        \
    if (!(condition)) {                                                       \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,     \
              #condition);                                                    \
      return 1;                                                               \
    }                                                                         \
  } while (0)

static void write_float(uint8_t *bytes, float value)
{
  uint32_t bits;

  memcpy(&bits, &value, sizeof(bits));
  WL32(bytes, bits);
}

static void make_raw(uint8_t raw[WORR_LEGACY_KEYED_POI_RAW_SIZE],
                     uint16_t key, uint16_t lifetime_ms)
{
  memset(raw, 0, WORR_LEGACY_KEYED_POI_RAW_SIZE);
  raw[0] = svc_poi;
  WL16(&raw[1], key);
  WL16(&raw[3], lifetime_ms);
  write_float(&raw[5], 12.5f);
  write_float(&raw[9], -24.25f);
  write_float(&raw[13], 96.0f);
  WL16(&raw[17], 321);
  raw[19] = 208;
  raw[20] = WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM;
}

static int test_exact_raw_decode(void)
{
  uint8_t raw[WORR_LEGACY_KEYED_POI_RAW_SIZE];
  q2proto_svc_poi_t poi;

  make_raw(raw, 8192, 10000);
  memset(&poi, 0xa5, sizeof(poi));
  CHECK(Worr_LegacyKeyedPOIEventDecodeRawV1(
            raw, sizeof(raw), &poi) ==
        WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_OK);
  CHECK(poi.key == 8192 && poi.time == 10000 &&
        poi.pos[0] == 12.5f && poi.pos[1] == -24.25f &&
        poi.pos[2] == 96.0f && poi.image == 321 && poi.color == 208 &&
        poi.flags == WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM);
  return 0;
}

static int expect_atomic_decode_failure(const uint8_t *raw, size_t size)
{
  q2proto_svc_poi_t poi;
  q2proto_svc_poi_t before;

  memset(&poi, 0xa5, sizeof(poi));
  before = poi;
  CHECK(Worr_LegacyKeyedPOIEventDecodeRawV1(raw, size, &poi) ==
        WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(&poi, &before, sizeof(poi)) == 0);
  return 0;
}

static int test_raw_rejection_is_atomic(void)
{
  uint8_t raw[WORR_LEGACY_KEYED_POI_RAW_SIZE + 1u];

  make_raw(raw, 8192, 5000);
  raw[0] = svc_nop;
  CHECK(expect_atomic_decode_failure(
            raw, WORR_LEGACY_KEYED_POI_RAW_SIZE) == 0);

  make_raw(raw, 0, 5000);
  CHECK(expect_atomic_decode_failure(
            raw, WORR_LEGACY_KEYED_POI_RAW_SIZE) == 0);

  make_raw(raw, 8192, 5000);
  raw[20] = WORR_EVENT_KEYED_POI_KNOWN_FLAGS | 0x80u;
  CHECK(expect_atomic_decode_failure(
            raw, WORR_LEGACY_KEYED_POI_RAW_SIZE) == 0);

  make_raw(raw, 8192, 5000);
  write_float(&raw[9], NAN);
  CHECK(expect_atomic_decode_failure(
            raw, WORR_LEGACY_KEYED_POI_RAW_SIZE) == 0);

  make_raw(raw, 8192, 5000);
  CHECK(expect_atomic_decode_failure(
            raw, WORR_LEGACY_KEYED_POI_RAW_SIZE - 1u) == 0);
  raw[WORR_LEGACY_KEYED_POI_RAW_SIZE] = svc_nop;
  CHECK(expect_atomic_decode_failure(raw, sizeof(raw)) == 0);
  return 0;
}

static int test_canonical_candidate(void)
{
  q2proto_svc_poi_t poi;
  worr_event_record_v1 candidate;
  worr_event_payload_keyed_poi_v1 payload;

  memset(&poi, 0, sizeof(poi));
  poi.key = 8195;
  poi.time = 5000;
  poi.pos[0] = 12.5f;
  poi.pos[1] = -24.25f;
  poi.pos[2] = 96.0f;
  poi.image = 321;
  poi.color = 215;
  poi.flags = WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM;

  CHECK(Worr_LegacyKeyedPOIEventCandidateBuildV1(
            &poi, 17, UINT64_C(425000), 64, &candidate) ==
        WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_OK);
  CHECK(candidate.source_tick == 17 &&
        candidate.source_time_us == UINT64_C(425000) &&
        candidate.source_entity.index == 0 &&
        candidate.source_entity.generation == 1 &&
        candidate.subject_entity.index == WORR_EVENT_NO_ENTITY &&
        candidate.event_type == WORR_EVENT_TYPE_STATE_CHANGE &&
        candidate.delivery_class == WORR_EVENT_DELIVERY_RELIABLE_ORDERED &&
        candidate.prediction_class ==
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY &&
        candidate.expiry_tick == 0 &&
        candidate.payload_kind == WORR_EVENT_PAYLOAD_KEYED_POI_V1 &&
        candidate.payload_size == sizeof(payload) &&
        Worr_EventRecordCandidateValidateV1(&candidate, 64));
  memcpy(&payload, candidate.payload, sizeof(payload));
  CHECK(payload.key == 8195 && payload.lifetime_ms == 5000 &&
        payload.position[0] == 12.5f && payload.position[1] == -24.25f &&
        payload.position[2] == 96.0f && payload.image_index == 321 &&
        payload.color_index == 215 &&
        payload.flags == WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM);

  /* The shared adapter preserves an infinite lifetime. Whether it is legal
   * is determined later from the accepted reliable/unreliable path. */
  poi.time = 0;
  CHECK(Worr_LegacyKeyedPOIEventCandidateBuildV1(
            &poi, 18, UINT64_C(450000), 64, &candidate) ==
        WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_OK);
  memcpy(&payload, candidate.payload, sizeof(payload));
  CHECK(payload.lifetime_ms == 0 && payload.image_index == 321 &&
        candidate.delivery_class == WORR_EVENT_DELIVERY_RELIABLE_ORDERED &&
        candidate.expiry_tick == 0);
  return 0;
}

static int test_remove_canonicalization_and_atomic_failure(void)
{
  q2proto_svc_poi_t poi;
  worr_event_record_v1 candidate;
  worr_event_record_v1 before;
  worr_event_payload_keyed_poi_v1 payload;
  worr_event_payload_keyed_poi_v1 zero_payload;

  memset(&poi, 0, sizeof(poi));
  poi.key = 8192;
  poi.time = WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS;
  poi.pos[0] = 12.5f;
  poi.pos[1] = -24.25f;
  poi.pos[2] = 96.0f;
  poi.image = 65535;
  poi.color = 255;
  poi.flags = WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM;
  CHECK(Worr_LegacyKeyedPOIEventCandidateBuildV1(
            &poi, 19, UINT64_C(475000), 64, &candidate) ==
        WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_OK);
  memcpy(&payload, candidate.payload, sizeof(payload));
  memset(&zero_payload, 0, sizeof(zero_payload));
  zero_payload.key = poi.key;
  zero_payload.lifetime_ms = WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS;
  CHECK(memcmp(&payload, &zero_payload, sizeof(payload)) == 0);

  memset(&candidate, 0xa5, sizeof(candidate));
  before = candidate;
  poi.key = 0;
  CHECK(Worr_LegacyKeyedPOIEventCandidateBuildV1(
            &poi, 19, UINT64_C(475000), 64, &candidate) ==
        WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(&candidate, &before, sizeof(candidate)) == 0);

  poi.key = 8192;
  poi.pos[0] = INFINITY;
  CHECK(Worr_LegacyKeyedPOIEventCandidateBuildV1(
            &poi, 19, UINT64_C(475000), 64, &candidate) ==
        WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(&candidate, &before, sizeof(candidate)) == 0);

  poi.pos[0] = 12.5f;
  CHECK(Worr_LegacyKeyedPOIEventCandidateBuildV1(
            &poi, UINT32_MAX, UINT64_C(475000), 64, &candidate) ==
        WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_OK);
  CHECK(candidate.source_tick == UINT32_MAX &&
        candidate.delivery_class == WORR_EVENT_DELIVERY_RELIABLE_ORDERED &&
        candidate.expiry_tick == 0 &&
        Worr_EventRecordCandidateValidateV1(&candidate, 64));
  return 0;
}

int main(void)
{
  if (test_exact_raw_decode() != 0 ||
      test_raw_rejection_is_atomic() != 0 ||
      test_canonical_candidate() != 0 ||
      test_remove_canonicalization_and_atomic_failure() != 0) {
    return EXIT_FAILURE;
  }
  puts("legacy keyed-POI event candidate tests passed");
  return EXIT_SUCCESS;
}
