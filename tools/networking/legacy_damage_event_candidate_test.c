/* Shared canonical construction coverage for per-client damage indicators. */

#include "common/net/legacy_damage_event_candidate.h"
#include "shared/shared.h"
#include "common/math.h"
#include "common/protocol.h"

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

static int test_exact_raw_decode(void)
{
  const uint8_t raw[] = {
      svc_damage, 2, 11 | 0x20 | 0x40, 7, 31 | 0x80, 42};
  q2proto_svc_damage_t damage;

  memset(&damage, 0xa5, sizeof(damage));
  CHECK(Worr_LegacyDamageEventDecodeRawV1(raw, sizeof(raw), &damage) ==
        WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_OK);
  CHECK(damage.count == 2 && damage.damage[0].damage == 11 &&
        damage.damage[0].health && damage.damage[0].armor &&
        !damage.damage[0].shield && damage.damage[1].damage == 31 &&
        !damage.damage[1].health && !damage.damage[1].armor &&
        damage.damage[1].shield);
  CHECK(damage.damage[0].direction[0] == bytedirs[7][0] &&
        damage.damage[0].direction[1] == bytedirs[7][1] &&
        damage.damage[0].direction[2] == bytedirs[7][2] &&
        damage.damage[1].direction[0] == bytedirs[42][0]);
  return 0;
}

static int expect_atomic_decode_failure(const uint8_t *raw, size_t size)
{
  q2proto_svc_damage_t damage;
  q2proto_svc_damage_t before;

  memset(&damage, 0xa5, sizeof(damage));
  before = damage;
  CHECK(Worr_LegacyDamageEventDecodeRawV1(raw, size, &damage) ==
        WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(&damage, &before, sizeof(damage)) == 0);
  return 0;
}

static int test_raw_rejection_is_atomic(void)
{
  const uint8_t wrong_opcode[] = {svc_nop, 1, 1 | 0x20, 7};
  const uint8_t zero_count[] = {svc_damage, 0};
  const uint8_t excess_count[] = {
      svc_damage, 5, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
  const uint8_t truncated[] = {svc_damage, 2, 1 | 0x20, 7};
  const uint8_t trailing[] = {svc_damage, 1, 1 | 0x20, 7, svc_nop};
  const uint8_t invalid_direction[] = {
      svc_damage, 1, 1 | 0x20, NUMVERTEXNORMALS};

  CHECK(expect_atomic_decode_failure(wrong_opcode, sizeof(wrong_opcode)) == 0);
  CHECK(expect_atomic_decode_failure(zero_count, sizeof(zero_count)) == 0);
  CHECK(expect_atomic_decode_failure(excess_count, sizeof(excess_count)) == 0);
  CHECK(expect_atomic_decode_failure(truncated, sizeof(truncated)) == 0);
  CHECK(expect_atomic_decode_failure(trailing, sizeof(trailing)) == 0);
  CHECK(expect_atomic_decode_failure(invalid_direction,
                                     sizeof(invalid_direction)) == 0);
  return 0;
}

static int test_canonical_batch_and_atomic_capacity(void)
{
  q2proto_svc_damage_t damage;
  worr_event_record_v1 candidates[Q2PROTO_MAX_DAMAGE_INDICATORS];
  worr_event_record_v1 before[Q2PROTO_MAX_DAMAGE_INDICATORS];
  worr_event_payload_damage_v1 payload;
  uint32_t count = UINT32_C(0x12345678);
  uint32_t count_before;

  memset(&damage, 0, sizeof(damage));
  damage.count = 2;
  damage.damage[0].damage = 11;
  damage.damage[0].health = true;
  damage.damage[0].armor = true;
  memcpy(damage.damage[0].direction, bytedirs[7], sizeof(bytedirs[7]));
  damage.damage[1].damage = 31;
  damage.damage[1].shield = true;
  memcpy(damage.damage[1].direction, bytedirs[42], sizeof(bytedirs[42]));

  CHECK(Worr_LegacyDamageEventCandidatesBuildV1(
            &damage, 17, UINT64_C(425000), 64, candidates,
            Q2PROTO_MAX_DAMAGE_INDICATORS, &count) ==
        WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_OK);
  CHECK(count == 2);
  CHECK(candidates[0].source_tick == 17 &&
        candidates[0].source_time_us == UINT64_C(425000) &&
        candidates[0].source_ordinal == 0 &&
        candidates[1].source_ordinal == 1 &&
        candidates[0].source_entity.index == 0 &&
        candidates[0].source_entity.generation == 1 &&
        candidates[0].subject_entity.index == WORR_EVENT_NO_ENTITY &&
        candidates[0].event_type == WORR_EVENT_TYPE_DAMAGE &&
        candidates[0].delivery_class == WORR_EVENT_DELIVERY_TRANSIENT &&
        candidates[0].prediction_class ==
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY &&
        candidates[0].expiry_tick == 18 &&
        candidates[0].payload_kind == WORR_EVENT_PAYLOAD_DAMAGE &&
        Worr_EventRecordCandidateValidateV1(&candidates[0], 64) &&
        Worr_EventRecordCandidateValidateV1(&candidates[1], 64));

  memcpy(&payload, candidates[0].payload, sizeof(payload));
  CHECK(payload.amount == 11.0f && payload.impulse == 0.0f &&
        payload.direction[0] == bytedirs[7][0] && payload.point[0] == 0.0f &&
        payload.damage_flags ==
            (WORR_EVENT_DAMAGE_FLAG_HEALTH | WORR_EVENT_DAMAGE_FLAG_ARMOR) &&
        payload.means_of_death == 0);
  memcpy(&payload, candidates[1].payload, sizeof(payload));
  CHECK(payload.amount == 31.0f &&
        payload.damage_flags == WORR_EVENT_DAMAGE_FLAG_SHIELD);

  payload.damage_flags |= UINT32_C(0x80000000);
  memcpy(candidates[1].payload, &payload, sizeof(payload));
  CHECK(!Worr_EventRecordCandidateValidateV1(&candidates[1], 64));

  memset(candidates, 0xa5, sizeof(candidates));
  memcpy(before, candidates, sizeof(candidates));
  count = UINT32_C(0x12345678);
  count_before = count;
  CHECK(Worr_LegacyDamageEventCandidatesBuildV1(
            &damage, 17, UINT64_C(425000), 64, candidates, 1, &count) ==
        WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_CAPACITY);
  CHECK(memcmp(candidates, before, sizeof(candidates)) == 0 &&
        count == count_before);

  damage.count = 0;
  CHECK(Worr_LegacyDamageEventCandidatesBuildV1(
            &damage, 17, UINT64_C(425000), 64, candidates,
            Q2PROTO_MAX_DAMAGE_INDICATORS, &count) ==
        WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(candidates, before, sizeof(candidates)) == 0 &&
        count == count_before);
  return 0;
}

int main(void)
{
  if (test_exact_raw_decode() != 0 ||
      test_raw_rejection_is_atomic() != 0 ||
      test_canonical_batch_and_atomic_capacity() != 0) {
    return EXIT_FAILURE;
  }
  puts("legacy damage event candidate tests passed");
  return EXIT_SUCCESS;
}
