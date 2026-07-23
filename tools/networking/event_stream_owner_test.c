/*
Copyright (C) 2026 WORR contributors

Deterministic canonical event-stream connection-owner tests.
*/

#include "common/net/event_stream_owner.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "event_stream_owner_test:%d: %s\n",          \
                    __LINE__, #condition);                                  \
            return false;                                                   \
        }                                                                   \
    } while (0)

static worr_event_stream_descriptor_v1 make_descriptor(
    uint32_t stream_epoch, uint32_t first_sequence)
{
    worr_event_stream_descriptor_v1 descriptor;

    memset(&descriptor, 0xa5, sizeof(descriptor));
    if (!Worr_EventStreamDescriptorInitV1(
            &descriptor, stream_epoch, first_sequence)) {
        fprintf(stderr,
                "event_stream_owner_test:%d: descriptor fixture init\n",
                __LINE__);
        memset(&descriptor, 0, sizeof(descriptor));
    }
    return descriptor;
}

static worr_event_stream_owner_v1 make_owner(uint64_t connection_owner_id)
{
    worr_event_stream_owner_v1 owner;

    memset(&owner, 0xa5, sizeof(owner));
    if (!Worr_EventStreamOwnerInitV1(&owner, connection_owner_id)) {
        fprintf(stderr,
                "event_stream_owner_test:%d: owner fixture init\n",
                __LINE__);
        memset(&owner, 0, sizeof(owner));
    }
    return owner;
}

static bool descriptor_is_zero(
    const worr_event_stream_descriptor_v1 *descriptor)
{
    const worr_event_stream_descriptor_v1 zero = { 0 };
    return memcmp(descriptor, &zero, sizeof(zero)) == 0;
}

static bool test_initialization_and_provenance(void)
{
    worr_event_stream_owner_v1 owner;
    worr_event_stream_owner_v1 expected;
    worr_event_stream_owner_v1 before;
    worr_event_stream_owner_v1 maximum;

    memset(&owner, 0xa5, sizeof(owner));
    CHECK(Worr_EventStreamOwnerInitV1(&owner, UINT64_C(0x1020304050607080)));
    memset(&expected, 0, sizeof(expected));
    expected.struct_size = sizeof(expected);
    expected.schema_version = WORR_EVENT_STREAM_OWNER_ABI_VERSION;
    expected.state_flags = WORR_EVENT_STREAM_OWNER_INITIALIZED;
    expected.mutation_generation = 1;
    expected.connection_owner_id = UINT64_C(0x1020304050607080);
    CHECK(memcmp(&owner, &expected, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerValidateV1(&owner));
    CHECK(descriptor_is_zero(&owner.descriptor));
    CHECK(owner.epoch_high_water == 0);

    maximum = make_owner(UINT64_MAX);
    CHECK(Worr_EventStreamOwnerValidateV1(&maximum));
    CHECK(maximum.connection_owner_id == UINT64_MAX);

    memset(&owner, 0x5a, sizeof(owner));
    before = owner;
    CHECK(!Worr_EventStreamOwnerInitV1(&owner, 0));
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(!Worr_EventStreamOwnerInitV1(NULL, 1));
    return true;
}

static bool test_validation_matrix(void)
{
    const worr_event_stream_descriptor_v1 descriptor =
        make_descriptor(17, 4);
    worr_event_stream_owner_v1 valid = make_owner(2001);
    worr_event_stream_owner_v1 batch_owner = make_owner(2004);
    worr_event_stream_descriptor_v1 batch_descriptor =
        make_descriptor(18, 5);
    worr_event_stream_owner_v1 malformed;
    uint32_t index;

    CHECK(Worr_EventStreamOwnerObserveV1(&valid, &descriptor) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    CHECK(Worr_EventStreamOwnerValidateV1(&valid));
    CHECK(!Worr_EventStreamOwnerValidateV1(NULL));

    for (index = 0; index < 13; ++index) {
        malformed = valid;
        switch (index) {
        case 0:
            malformed.struct_size = 0;
            break;
        case 1:
            malformed.schema_version = 0;
            break;
        case 2:
            malformed.state_flags |= UINT16_C(0x8000);
            break;
        case 3:
            malformed.state_flags &=
                (uint16_t)~WORR_EVENT_STREAM_OWNER_INITIALIZED;
            break;
        case 4:
            malformed.state_flags |=
                WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC;
            break;
        case 5:
            malformed.state_flags |=
                WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED;
            break;
        case 6:
            malformed.reserved0 = 1;
            break;
        case 7:
            malformed.mutation_generation = 0;
            break;
        case 8:
            malformed.connection_owner_id = 0;
            break;
        case 9:
            malformed.epoch_high_water++;
            break;
        case 10:
            malformed.descriptor.flags = UINT16_C(0x8000);
            break;
        case 11:
            memset(&malformed.descriptor, 0,
                   sizeof(malformed.descriptor));
            break;
        case 12:
            malformed.mutation_generation = UINT64_MAX;
            break;
        default:
            CHECK(false);
        }
        CHECK(!Worr_EventStreamOwnerValidateV1(&malformed));
    }

    malformed = make_owner(2002);
    malformed.descriptor = descriptor;
    CHECK(!Worr_EventStreamOwnerValidateV1(&malformed));
    malformed = make_owner(2003);
    malformed.epoch_high_water = descriptor.stream_epoch;
    CHECK(!Worr_EventStreamOwnerValidateV1(&malformed));
    malformed.state_flags |= WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC;
    CHECK(Worr_EventStreamOwnerValidateV1(&malformed));

    batch_descriptor.flags = WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2;
    CHECK(Worr_EventStreamOwnerObserveV1(
              &batch_owner, &batch_descriptor) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    CHECK(Worr_EventStreamOwnerValidateV1(&batch_owner));
    CHECK(Worr_EventStreamDescriptorEqualV1(
        &batch_owner.descriptor, &batch_descriptor));
    return true;
}

static bool test_observation_epoch_rules(void)
{
    worr_event_stream_owner_v1 owner = make_owner(3001);
    const worr_event_stream_descriptor_v1 first =
        make_descriptor(40, 7);
    const worr_event_stream_descriptor_v1 conflicting =
        make_descriptor(40, 8);
    const worr_event_stream_descriptor_v1 lower =
        make_descriptor(39, 99);
    const worr_event_stream_descriptor_v1 higher =
        make_descriptor(41, 3);
    worr_event_stream_owner_v1 before;

    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &first) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    CHECK(Worr_EventStreamOwnerValidateV1(&owner));
    CHECK(owner.state_flags ==
          (WORR_EVENT_STREAM_OWNER_INITIALIZED |
           WORR_EVENT_STREAM_OWNER_ACTIVE));
    CHECK(Worr_EventStreamDescriptorEqualV1(&owner.descriptor, &first));
    CHECK(owner.epoch_high_water == 40);
    CHECK(owner.mutation_generation == 2);
    CHECK(owner.connection_owner_id == 3001);

    before = owner;
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &first) ==
          WORR_EVENT_STREAM_OWNER_EXACT_DUPLICATE);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);

    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &conflicting) ==
          WORR_EVENT_STREAM_OWNER_CONFLICT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &lower) ==
          WORR_EVENT_STREAM_OWNER_WRONG_EPOCH);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);

    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &higher) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    CHECK(Worr_EventStreamDescriptorEqualV1(&owner.descriptor, &higher));
    CHECK(owner.epoch_high_water == 41);
    CHECK(owner.mutation_generation == 3);
    CHECK(owner.connection_owner_id == 3001);

    before = owner;
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &first) ==
          WORR_EVENT_STREAM_OWNER_WRONG_EPOCH);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    return true;
}

static bool test_resync_barrier_and_high_water(void)
{
    worr_event_stream_owner_v1 owner = make_owner(4001);
    const worr_event_stream_descriptor_v1 active =
        make_descriptor(70, 11);
    const worr_event_stream_descriptor_v1 same_epoch =
        make_descriptor(70, 12);
    const worr_event_stream_descriptor_v1 stale =
        make_descriptor(69, 1);
    const worr_event_stream_descriptor_v1 recovery =
        make_descriptor(71, 400);
    worr_event_stream_owner_v1 before;

    before = owner;
    CHECK(!Worr_EventStreamOwnerRequireResyncV1(&owner));
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);

    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &active) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    CHECK(Worr_EventStreamOwnerRequireResyncV1(&owner));
    CHECK(Worr_EventStreamOwnerValidateV1(&owner));
    CHECK(owner.state_flags ==
          (WORR_EVENT_STREAM_OWNER_INITIALIZED |
           WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC));
    CHECK(descriptor_is_zero(&owner.descriptor));
    CHECK(owner.epoch_high_water == 70);
    CHECK(owner.mutation_generation == 3);
    CHECK(owner.connection_owner_id == 4001);

    before = owner;
    CHECK(Worr_EventStreamOwnerRequireResyncV1(&owner));
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &active) ==
          WORR_EVENT_STREAM_OWNER_CONFLICT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &same_epoch) ==
          WORR_EVENT_STREAM_OWNER_CONFLICT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &stale) ==
          WORR_EVENT_STREAM_OWNER_WRONG_EPOCH);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);

    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &recovery) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    CHECK(Worr_EventStreamOwnerValidateV1(&owner));
    CHECK(owner.state_flags ==
          (WORR_EVENT_STREAM_OWNER_INITIALIZED |
           WORR_EVENT_STREAM_OWNER_ACTIVE));
    CHECK(Worr_EventStreamDescriptorEqualV1(
        &owner.descriptor, &recovery));
    CHECK(owner.epoch_high_water == 71);
    CHECK(owner.mutation_generation == 4);
    CHECK(owner.connection_owner_id == 4001);
    return true;
}

static bool test_reconnect_reuse(void)
{
    worr_event_stream_owner_v1 owner = make_owner(5001);
    const worr_event_stream_descriptor_v1 old =
        make_descriptor(900, 27);
    const worr_event_stream_descriptor_v1 reused_lower =
        make_descriptor(4, 2);

    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &old) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    CHECK(Worr_EventStreamOwnerRequireResyncV1(&owner));
    CHECK(owner.epoch_high_water == 900);

    /* OwnerInit is the explicit full-reconnect boundary. */
    CHECK(Worr_EventStreamOwnerInitV1(&owner, 5002));
    CHECK(Worr_EventStreamOwnerValidateV1(&owner));
    CHECK(owner.connection_owner_id == 5002);
    CHECK(owner.epoch_high_water == 0);
    CHECK(owner.mutation_generation == 1);
    CHECK(owner.state_flags == WORR_EVENT_STREAM_OWNER_INITIALIZED);
    CHECK(descriptor_is_zero(&owner.descriptor));
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &reused_lower) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    CHECK(owner.connection_owner_id == 5002);
    CHECK(owner.epoch_high_water == 4);

    /* A second fresh connection may reuse the exact same numeric epoch. */
    CHECK(Worr_EventStreamOwnerInitV1(&owner, 5003));
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &reused_lower) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    CHECK(owner.connection_owner_id == 5003);
    CHECK(owner.epoch_high_water == 4);
    return true;
}

static bool test_alias_and_failure_atomicity(void)
{
    worr_event_stream_owner_v1 owner = make_owner(6001);
    const worr_event_stream_descriptor_v1 valid =
        make_descriptor(18, 6);
    worr_event_stream_descriptor_v1 invalid = valid;
    worr_event_stream_owner_v1 before;
    worr_event_stream_owner_v1 malformed;

    before = owner;
    CHECK(Worr_EventStreamOwnerObserveV1(NULL, &valid) ==
          WORR_EVENT_STREAM_OWNER_INVALID_ARGUMENT);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, NULL) ==
          WORR_EVENT_STREAM_OWNER_INVALID_ARGUMENT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);

    invalid.flags = UINT16_C(0x8000);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &invalid) ==
          WORR_EVENT_STREAM_OWNER_INVALID_DESCRIPTOR);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);

    /* The embedded descriptor is a complete overlap with its owner. */
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &owner.descriptor) ==
          WORR_EVENT_STREAM_OWNER_INVALID_ARGUMENT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(
              &owner,
              (const worr_event_stream_descriptor_v1 *)(const void *)&owner) ==
          WORR_EVENT_STREAM_OWNER_INVALID_ARGUMENT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);

    malformed = owner;
    malformed.reserved0 = 1;
    before = malformed;
    CHECK(Worr_EventStreamOwnerObserveV1(&malformed, &valid) ==
          WORR_EVENT_STREAM_OWNER_INVALID_STATE);
    CHECK(memcmp(&malformed, &before, sizeof(malformed)) == 0);
    CHECK(!Worr_EventStreamOwnerRequireResyncV1(&malformed));
    CHECK(memcmp(&malformed, &before, sizeof(malformed)) == 0);
    CHECK(!Worr_EventStreamOwnerRequireResyncV1(NULL));
    return true;
}

static bool test_generation_boundaries(void)
{
    worr_event_stream_owner_v1 owner = make_owner(7001);
    const worr_event_stream_descriptor_v1 first =
        make_descriptor(100, 1);
    const worr_event_stream_descriptor_v1 same_epoch =
        make_descriptor(100, 2);
    const worr_event_stream_descriptor_v1 lower =
        make_descriptor(99, 1);
    const worr_event_stream_descriptor_v1 next =
        make_descriptor(101, 1);
    const worr_event_stream_descriptor_v1 current_conflict =
        make_descriptor(101, 2);
    const worr_event_stream_descriptor_v1 final =
        make_descriptor(102, UINT32_MAX);
    worr_event_stream_owner_v1 before;
    worr_event_stream_owner_v1 malformed;

    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &first) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    owner.mutation_generation = UINT64_MAX - 2u;
    CHECK(Worr_EventStreamOwnerValidateV1(&owner));

    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &next) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    CHECK(owner.mutation_generation == UINT64_MAX - 1u);
    CHECK((owner.state_flags &
           WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED) == 0);
    CHECK(owner.epoch_high_water == 101);
    CHECK(Worr_EventStreamOwnerValidateV1(&owner));

    before = owner;
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &next) ==
          WORR_EVENT_STREAM_OWNER_EXACT_DUPLICATE);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &final) ==
          WORR_EVENT_STREAM_OWNER_GENERATION_LIMIT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &current_conflict) ==
          WORR_EVENT_STREAM_OWNER_CONFLICT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &same_epoch) ==
          WORR_EVENT_STREAM_OWNER_WRONG_EPOCH);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &lower) ==
          WORR_EVENT_STREAM_OWNER_WRONG_EPOCH);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);

    /* The final generation is reserved for fail-closed authority loss. */
    CHECK(Worr_EventStreamOwnerRequireResyncV1(&owner));
    CHECK(owner.mutation_generation == UINT64_MAX);
    CHECK(owner.state_flags ==
          (WORR_EVENT_STREAM_OWNER_INITIALIZED |
           WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC |
           WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED));
    CHECK(descriptor_is_zero(&owner.descriptor));
    CHECK(owner.epoch_high_water == 101);
    CHECK(Worr_EventStreamOwnerValidateV1(&owner));

    before = owner;
    CHECK(Worr_EventStreamOwnerRequireResyncV1(&owner));
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &final) ==
          WORR_EVENT_STREAM_OWNER_GENERATION_LIMIT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &next) ==
          WORR_EVENT_STREAM_OWNER_CONFLICT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);

    malformed = before;
    malformed.state_flags &=
        (uint16_t)~WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED;
    CHECK(!Worr_EventStreamOwnerValidateV1(&malformed));
    malformed = before;
    malformed.mutation_generation = UINT64_MAX - 1u;
    CHECK(!Worr_EventStreamOwnerValidateV1(&malformed));

    /* A structurally valid exhausted active state is fail-closed if imported
     * from persistent diagnostics, even though normal API transitions reserve
     * the last generation for resync. */
    owner = make_owner(7002);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &first) ==
          WORR_EVENT_STREAM_OWNER_ACTIVATED);
    owner.mutation_generation = UINT64_MAX;
    owner.state_flags |= WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED;
    CHECK(Worr_EventStreamOwnerValidateV1(&owner));
    before = owner;
    CHECK(!Worr_EventStreamOwnerRequireResyncV1(&owner));
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &first) ==
          WORR_EVENT_STREAM_OWNER_EXACT_DUPLICATE);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    CHECK(Worr_EventStreamOwnerObserveV1(&owner, &next) ==
          WORR_EVENT_STREAM_OWNER_GENERATION_LIMIT);
    CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
    return true;
}

int main(void)
{
    if (!test_initialization_and_provenance() ||
        !test_validation_matrix() ||
        !test_observation_epoch_rules() ||
        !test_resync_barrier_and_high_water() ||
        !test_reconnect_reuse() ||
        !test_alias_and_failure_atomicity() ||
        !test_generation_boundaries()) {
        return 1;
    }
    puts("event stream owner tests passed");
    return 0;
}
