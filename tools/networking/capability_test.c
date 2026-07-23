/* Standalone FR-10-T04 connection-bound capability negotiation tests. */

#include "common/net/capability.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(stderr, "capability_test:%d: %s\n", __LINE__,         \
                    #expression);                                           \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    } while (0)

static void test_text(void)
{
    char text[11] = {0};
    uint32_t value = UINT32_MAX;
    CHECK(WORR_NET_CAP_LEGACY_STAGE_MASK == UINT32_C(0x03));
    CHECK(WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK == UINT32_C(0x53));
    CHECK(WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK == UINT32_C(0x73));
    CHECK(WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK == UINT32_C(0x57));
    CHECK(WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PRIVATE_MASK ==
          UINT32_C(0x77));
    CHECK(WORR_NET_CAP_NATIVE_COMMAND_PUBLIC_MASK == UINT32_C(0x53));
    CHECK(WORR_NET_CAP_NATIVE_EVENT_PUBLIC_MASK == UINT32_C(0x73));
    CHECK(WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK == UINT32_C(0x57));
    CHECK(WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PUBLIC_MASK ==
          UINT32_C(0x77));
    CHECK(Worr_NetCapabilitiesFormatV1(WORR_NET_CAP_LEGACY_STAGE_MASK,
                                       text, sizeof(text)));
    CHECK(!strcmp(text, "3"));
    CHECK(Worr_NetCapabilitiesParseV1(text, &value) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(value == WORR_NET_CAP_LEGACY_STAGE_MASK);
    CHECK(Worr_NetCapabilitiesParseV1("", &value) ==
          WORR_NET_CAPABILITY_INVALID_TEXT);
    CHECK(Worr_NetCapabilitiesParseV1("00", &value) ==
          WORR_NET_CAPABILITY_INVALID_TEXT);
    CHECK(Worr_NetCapabilitiesParseV1("+1", &value) ==
          WORR_NET_CAPABILITY_INVALID_TEXT);
    CHECK(Worr_NetCapabilitiesParseV1("4294967296", &value) ==
          WORR_NET_CAPABILITY_INVALID_TEXT);
    CHECK(Worr_NetCapabilitiesParseV1("32", &value) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(value == WORR_NET_CAP_NATIVE_EVENT_STREAM_V1);
    CHECK(Worr_NetCapabilitiesFormatV1(
        WORR_NET_CAP_NATIVE_EVENT_STREAM_V1, text, sizeof(text)));
    CHECK(!strcmp(text, "32"));
    CHECK(Worr_NetCapabilitiesParseV1("64", &value) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(value == WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1);
    CHECK(Worr_NetCapabilitiesFormatV1(
        WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1, text, sizeof(text)));
    CHECK(!strcmp(text, "64"));
    CHECK(Worr_NetCapabilitiesParseV1("128", &value) ==
          WORR_NET_CAPABILITY_UNKNOWN_BITS);
    CHECK(!Worr_NetCapabilitiesFormatV1(UINT32_C(128), text,
                                        sizeof(text)));
}

static void test_confirmation(void)
{
    worr_net_capability_state_v1 state;
    worr_net_capability_confirm_v1 confirm;
    CHECK(Worr_NetCapabilityStateInitV1(
        &state, 17, WORR_NET_CAP_LEGACY_STAGE_MASK,
        WORR_NET_CAP_LEGACY_STAGE_MASK));
    CHECK(Worr_NetCapabilitySelectV1(
        17, state.offered, state.supported, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(Worr_NetCapabilityConfirmV1(&state, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(state.phase == WORR_NET_CAPABILITY_CONFIRMED);
    CHECK(state.peer_supported == WORR_NET_CAP_LEGACY_STAGE_MASK);
    CHECK(state.negotiated == WORR_NET_CAP_LEGACY_STAGE_MASK);
    CHECK(Worr_NetCapabilityConfirmV1(&state, &confirm) ==
          WORR_NET_CAPABILITY_ALREADY_CONFIRMED);

    CHECK(Worr_NetCapabilityStateInitV1(
        &state, 18, WORR_NET_CAP_LEGACY_STAGE_MASK,
        WORR_NET_CAP_LEGACY_STAGE_MASK));
    CHECK(Worr_NetCapabilitySelectV1(
        18, state.offered,
        WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(Worr_NetCapabilityConfirmV1(&state, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(state.peer_supported ==
          WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1);
    CHECK(state.negotiated ==
          WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1);
}

static void test_public_native_policy(void)
{
    static const struct {
        bool native_enabled;
        bool event_enabled;
        bool snapshot_enabled;
        uint32_t expected;
    } offers[] = {
        {false, false, false, WORR_NET_CAP_LEGACY_STAGE_MASK},
        {false, true, false, WORR_NET_CAP_LEGACY_STAGE_MASK},
        {false, false, true, WORR_NET_CAP_LEGACY_STAGE_MASK},
        {false, true, true, WORR_NET_CAP_LEGACY_STAGE_MASK},
        {true, false, false, WORR_NET_CAP_NATIVE_COMMAND_PUBLIC_MASK},
        {true, true, false, WORR_NET_CAP_NATIVE_EVENT_PUBLIC_MASK},
        {true, false, true, WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK},
        {true, true, true,
         WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PUBLIC_MASK},
    };
    size_t index;
    size_t peer;
    uint32_t mask;

    for (index = 0; index < sizeof(offers) / sizeof(offers[0]); ++index) {
        const uint32_t configured = Worr_NetCapabilityPublicOfferV1(
            offers[index].native_enabled, offers[index].event_enabled,
            offers[index].snapshot_enabled);
        CHECK(configured == offers[index].expected);
        CHECK(Worr_NetCapabilityIsPublicNativeMaskV1(configured) ==
              offers[index].native_enabled);
        CHECK(Worr_NetCapabilityPublicSupportV1(
                  configured, configured, false) ==
              WORR_NET_CAP_LEGACY_STAGE_MASK);

        for (peer = 0; peer < sizeof(offers) / sizeof(offers[0]); ++peer) {
            const uint32_t offered = offers[peer].expected;
            const uint32_t supported =
                Worr_NetCapabilityPublicSupportV1(
                    offered, configured, true);
            const uint32_t expected =
                offers[index].native_enabled && offered == configured
                    ? configured
                    : WORR_NET_CAP_LEGACY_STAGE_MASK;
            CHECK(supported == expected);
        }
    }

    CHECK(!Worr_NetCapabilityIsPublicNativeMaskV1(
        WORR_NET_CAP_LEGACY_STAGE_MASK));
    CHECK(!Worr_NetCapabilityIsPublicNativeMaskV1(
        WORR_NET_CAP_NATIVE_ENVELOPE_V1));
    CHECK(Worr_NetCapabilityPublicSupportV1(
              WORR_NET_CAP_NATIVE_EVENT_PUBLIC_MASK,
              WORR_NET_CAP_NATIVE_COMMAND_PUBLIC_MASK, true) ==
          WORR_NET_CAP_LEGACY_STAGE_MASK);

    /* Exhaust every currently valid bit combination so a future partial
     * intersection cannot become a public native bundle accidentally. */
    for (mask = 0; mask <= WORR_NET_CAP_KNOWN_MASK; ++mask) {
        const bool exact_native =
            mask == WORR_NET_CAP_NATIVE_COMMAND_PUBLIC_MASK ||
            mask == WORR_NET_CAP_NATIVE_EVENT_PUBLIC_MASK ||
            mask == WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK ||
            mask == WORR_NET_CAP_NATIVE_EVENT_SNAPSHOT_PUBLIC_MASK;
        CHECK(Worr_NetCapabilityIsPublicNativeMaskV1(mask) == exact_native);
        CHECK(Worr_NetCapabilityPublicSupportV1(mask, mask, true) ==
              (exact_native ? mask : WORR_NET_CAP_LEGACY_STAGE_MASK));
    }
}

static void test_private_bit_echo_and_downgrade(void)
{
    worr_net_capability_state_v1 state;
    worr_net_capability_confirm_v1 confirm;

    CHECK(Worr_NetCapabilityStateInitV1(
        &state, 19, WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK,
        WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK));
    CHECK(Worr_NetCapabilitySelectV1(
        19, state.offered, state.supported, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(confirm.negotiated == WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK);
    CHECK((confirm.negotiated & WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1) != 0);
    CHECK(Worr_NetCapabilityConfirmV1(&state, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(state.negotiated == WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK);

    CHECK(Worr_NetCapabilityStateInitV1(
        &state, 20, WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK,
        WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK));
    CHECK(Worr_NetCapabilitySelectV1(
        20, state.offered, WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK,
        &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(confirm.negotiated == WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK);
    CHECK((confirm.negotiated & WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1) != 0);
    CHECK(Worr_NetCapabilityConfirmV1(&state, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(state.negotiated == WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK);
}

static void test_fail_closed(void)
{
    worr_net_capability_state_v1 state;
    worr_net_capability_state_v1 fresh;
    worr_net_capability_confirm_v1 confirm;
    CHECK(Worr_NetCapabilityStateInitV1(&state, 3, 3, 3));
    CHECK(Worr_NetCapabilitySelectV1(4, 3, 3, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    CHECK(Worr_NetCapabilityConfirmV1(&state, &confirm) ==
          WORR_NET_CAPABILITY_EPOCH_MISMATCH);
    CHECK(state.phase == WORR_NET_CAPABILITY_FAILED);
    CHECK(state.negotiated == 0);

    CHECK(Worr_NetCapabilityStateInitV1(&fresh, 4, 3, 3));
    CHECK(Worr_NetCapabilitySelectV1(4, 3, 3, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    confirm.negotiated = 1;
    CHECK(Worr_NetCapabilityConfirmV1(&fresh, &confirm) ==
          WORR_NET_CAPABILITY_UNOFFERED_BITS);
    CHECK(fresh.phase == WORR_NET_CAPABILITY_FAILED);

    CHECK(Worr_NetCapabilityStateInitV1(&fresh, 5, 1, 1));
    CHECK(Worr_NetCapabilitySelectV1(5, 1, 1, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    confirm.schema_version++;
    CHECK(Worr_NetCapabilityConfirmV1(&fresh, &confirm) ==
          WORR_NET_CAPABILITY_VERSION_MISMATCH);
    CHECK(fresh.phase == WORR_NET_CAPABILITY_FAILED);
}

int main(void)
{
    test_text();
    test_confirmation();
    test_public_native_policy();
    test_private_bit_echo_and_downgrade();
    test_fail_closed();
    puts("capability_test: ok");
    return EXIT_SUCCESS;
}
