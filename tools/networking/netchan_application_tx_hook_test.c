/* Deterministic integration tests for the dormant post-assembly TX hook. */

#include "shared/shared.h"
#include "common/net/chan.h"
#include "common/cvar.h"
#include "common/zone.h"
#include "system/system.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "netchan_application_tx_hook_test:%d: %s\n", \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

enum {
    TEST_MAX_COPIES = 8,
    TEST_MAX_APPLICATION = 512,
    TEST_QPORT = 0x5a,
};

typedef struct test_send_capture_s {
    bool outcomes[TEST_MAX_COPIES];
    unsigned outcome_count;
    unsigned calls;
    size_t packet_bytes[TEST_MAX_COPIES];
    byte packets[TEST_MAX_COPIES][MAX_PACKETLEN];
} test_send_capture_t;

typedef enum test_prepare_mode_e {
    TEST_PREPARE_BYPASS,
    TEST_PREPARE_VALID,
    TEST_PREPARE_INVALID_ABI,
    TEST_PREPARE_INVALID_RESERVED,
    TEST_PREPARE_INVALID_OVERSIZE,
    TEST_PREPARE_UNKNOWN,
} test_prepare_mode_t;

typedef struct test_hook_s {
    test_prepare_mode_t mode;
    byte candidate[MAX_PACKETLEN_WRITABLE];
    uint32_t candidate_bytes;
    uint64_t token;
    netchan_t *clear_registration_on_prepare;
    bool clear_registration_result;
    netchan_t *append_queue_on_prepare;
    byte append_queue_bytes[16];
    uint32_t append_queue_byte_count;
    unsigned queue_appends;

    unsigned prepare_calls;
    netchan_app_tx_prepare_info_v1_t prepare_info;
    byte legacy[MAX_PACKETLEN_WRITABLE];
    size_t legacy_bytes;
    bool candidate_was_zeroed;

    unsigned completion_calls;
    netchan_app_tx_completion_info_v1_t completion_info;
    byte completed_application[MAX_PACKETLEN_WRITABLE];
    size_t completed_application_bytes;
} test_hook_t;

static test_send_capture_t test_send;

/* Minimal engine service definitions for linking the real chan.c. */
unsigned com_localTime = 1000;
sizebuf_t msg_read;
byte msg_read_buffer[MAX_MSGLEN];

void Com_LPrintf(print_type_t type, const char *fmt, ...)
{
    (void)type;
    (void)fmt;
}

q_noreturn void Com_Error(error_type_t code, const char *fmt, ...)
{
    (void)code;
    (void)fmt;
    abort();
}

size_t Q_vsnprintf(char *dest, size_t size, const char *fmt, va_list argptr)
{
    int result = vsnprintf(dest, size, fmt, argptr);

    return result < 0 ? size : (size_t)result;
}

char *va(const char *format, ...)
{
    static char buffer[128];
    va_list argptr;

    va_start(argptr, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    return buffer;
}

unsigned Sys_Milliseconds(void)
{
    return com_localTime;
}

cvar_t *Cvar_Get(const char *var_name, const char *value, int flags)
{
    static cvar_t value_stub;

    (void)var_name;
    (void)value;
    (void)flags;
    memset(&value_stub, 0, sizeof(value_stub));
    return &value_stub;
}

int Cvar_ClampInteger(cvar_t *var, int min_value, int max_value)
{
    if (var->integer < min_value)
        var->integer = min_value;
    if (var->integer > max_value)
        var->integer = max_value;
    return var->integer;
}

void *Z_TagMalloc(size_t size, memtag_t tag)
{
    (void)tag;
    return malloc(size);
}

void *Z_Malloc(size_t size)
{
    return malloc(size);
}

void *Z_Realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void Z_Free(void *ptr)
{
    free(ptr);
}

const char *NET_AdrToString(const netadr_t *address)
{
    (void)address;
    return "test-address";
}

bool NET_SendPacket(netsrc_t sock, const void *data, size_t len,
                    const netadr_t *to)
{
    const unsigned call = test_send.calls++;

    (void)sock;
    (void)to;
    if (call < TEST_MAX_COPIES) {
        test_send.packet_bytes[call] = len;
        if (len <= sizeof(test_send.packets[call]))
            memcpy(test_send.packets[call], data, len);
    }
    return call < test_send.outcome_count ? test_send.outcomes[call] : true;
}

void MSG_BeginReading(void)
{
}

int MSG_ReadByte(void)
{
    return -1;
}

int MSG_ReadShort(void)
{
    return -1;
}

int MSG_ReadWord(void)
{
    return -1;
}

int MSG_ReadLong(void)
{
    return -1;
}

static void reset_send(const bool *outcomes, unsigned count)
{
    memset(&test_send, 0, sizeof(test_send));
    if (outcomes && count) {
        memcpy(test_send.outcomes, outcomes, count * sizeof(outcomes[0]));
        test_send.outcome_count = count;
    }
}

static void init_channel(netchan_t *chan, netchan_type_t type,
                         netsrc_t sock, int qport, unsigned maxpacketlen)
{
    netadr_t address;

    memset(chan, 0, sizeof(*chan));
    memset(&address, 0, sizeof(address));
    Netchan_Setup(chan, sock, type, &address, qport, maxpacketlen, 0);
}

static size_t application_offset(const netchan_t *chan)
{
    return 8u + (chan->sock == NS_CLIENT && chan->qport ? 1u : 0u);
}

static netchan_app_tx_prepare_result_t test_prepare(
    void *opaque,
    const netchan_app_tx_prepare_info_v1_t *info,
    const byte *legacy_application,
    byte *candidate_application,
    netchan_app_tx_prepare_output_v1_t *output)
{
    test_hook_t *hook = opaque;

    hook->prepare_calls++;
    hook->prepare_info = *info;
    hook->legacy_bytes = info->legacy_application_bytes;
    if (hook->legacy_bytes)
        memcpy(hook->legacy, legacy_application, hook->legacy_bytes);

    hook->candidate_was_zeroed = true;
    for (uint32_t i = 0; i < info->max_application_bytes; i++) {
        if (candidate_application[i] != 0) {
            hook->candidate_was_zeroed = false;
            break;
        }
    }

    output->token = hook->token;
    if (hook->candidate_bytes)
        memcpy(candidate_application, hook->candidate,
               hook->candidate_bytes <= info->max_application_bytes
                   ? hook->candidate_bytes
                   : info->max_application_bytes);
    output->application_bytes = hook->candidate_bytes;

    if (hook->append_queue_on_prepare &&
        hook->append_queue_byte_count != 0) {
        SZ_Write(&hook->append_queue_on_prepare->message,
                 hook->append_queue_bytes,
                 hook->append_queue_byte_count);
        hook->queue_appends++;
    }

    if (hook->clear_registration_on_prepare) {
        hook->clear_registration_result = Netchan_SetApplicationTxHook(
            hook->clear_registration_on_prepare, NULL, NULL, NULL);
    }

    switch (hook->mode) {
    case TEST_PREPARE_BYPASS:
        output->reserved0 = UINT32_C(0xfeedface);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    case TEST_PREPARE_VALID:
        return NETCHAN_APP_TX_PREPARE_PREPARED;
    case TEST_PREPARE_INVALID_ABI:
        output->abi_version = NETCHAN_APP_TX_HOOK_ABI_V1 + 1;
        return NETCHAN_APP_TX_PREPARE_PREPARED;
    case TEST_PREPARE_INVALID_RESERVED:
        output->reserved0 = 1;
        return NETCHAN_APP_TX_PREPARE_PREPARED;
    case TEST_PREPARE_INVALID_OVERSIZE:
        output->application_bytes = info->max_application_bytes + 1;
        return NETCHAN_APP_TX_PREPARE_PREPARED;
    case TEST_PREPARE_UNKNOWN:
        return (netchan_app_tx_prepare_result_t)99;
    }

    return (netchan_app_tx_prepare_result_t)99;
}

static void test_completion(
    void *opaque,
    const netchan_app_tx_completion_info_v1_t *info,
    const byte *application)
{
    test_hook_t *hook = opaque;

    hook->completion_calls++;
    hook->completion_info = *info;
    hook->completed_application_bytes = info->application_bytes;
    if (info->application_bytes)
        memcpy(hook->completed_application, application,
               info->application_bytes);
}

static int test_default_and_bypass_are_byte_identical(void)
{
    static const byte unreliable[] = { 0x10, 0x20, 0x30, 0x40 };
    static const byte reliable[] = { 0xa1, 0xa2, 0xa3 };
    static const byte combined[] = {
        0xa1, 0xa2, 0xa3, 0x10, 0x20, 0x30, 0x40
    };
    static const bool accepted[] = { true, true };
    netchan_t chan;
    test_hook_t hook;
    byte baseline[MAX_PACKETLEN];
    size_t baseline_bytes;
    size_t offset;

    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, TEST_QPORT,
                 TEST_MAX_APPLICATION);
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, sizeof(unreliable), unreliable, 1) == 13);
    CHECK(test_send.calls == 1);
    offset = application_offset(&chan);
    CHECK(offset == 9 && test_send.packet_bytes[0] == offset + sizeof(unreliable));
    CHECK(test_send.packets[0][8] == TEST_QPORT);
    CHECK(memcmp(test_send.packets[0] + offset, unreliable,
                 sizeof(unreliable)) == 0);
    Netchan_Close(&chan);

    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, TEST_QPORT,
                 TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, reliable, sizeof(reliable));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, sizeof(unreliable), unreliable, 1) ==
          (int)(9 + sizeof(combined)));
    baseline_bytes = test_send.packet_bytes[0];
    memcpy(baseline, test_send.packets[0], baseline_bytes);
    Netchan_Close(&chan);

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_BYPASS;
    hook.candidate_bytes = 5;
    memset(hook.candidate, 0xee, hook.candidate_bytes);
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, TEST_QPORT,
                 TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, reliable, sizeof(reliable));
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 2);
    CHECK(Netchan_Transmit(&chan, sizeof(unreliable), unreliable, 2) ==
          (int)((9 + sizeof(combined)) * 2));
    CHECK(hook.prepare_calls == 1 && hook.completion_calls == 0);
    CHECK(hook.candidate_was_zeroed);
    CHECK(hook.prepare_info.abi_version == NETCHAN_APP_TX_HOOK_ABI_V1 &&
          hook.prepare_info.struct_size == sizeof(hook.prepare_info));
    CHECK(hook.prepare_info.outgoing_sequence == 1 &&
          hook.prepare_info.max_application_bytes == TEST_MAX_APPLICATION &&
          hook.prepare_info.reliable_bytes == sizeof(reliable) &&
          hook.prepare_info.unreliable_bytes == sizeof(unreliable) &&
          hook.prepare_info.legacy_application_bytes == sizeof(combined) &&
          hook.prepare_info.packet_copies == 2);
    CHECK(hook.legacy_bytes == sizeof(combined) &&
          memcmp(hook.legacy, combined, sizeof(combined)) == 0);
    CHECK(test_send.calls == 2 &&
          test_send.packet_bytes[0] == 9 + sizeof(combined) &&
          test_send.packet_bytes[1] == test_send.packet_bytes[0]);
    CHECK(memcmp(test_send.packets[0] + 9, combined, sizeof(combined)) == 0 &&
          memcmp(test_send.packets[1], test_send.packets[0],
                 test_send.packet_bytes[0]) == 0 &&
          test_send.packet_bytes[0] == baseline_bytes &&
          memcmp(test_send.packets[0], baseline, baseline_bytes) == 0);
    Netchan_Close(&chan);
    return 0;
}

static int run_acceptance_case(const bool *outcomes, unsigned accepted_count,
                               uint32_t expected_result)
{
    static const byte legacy[] = { 1, 2, 3 };
    static const byte candidate[] = { 9, 8, 7, 6, 5 };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = UINT64_C(0x1122334455667788);
    hook.candidate_bytes = sizeof(candidate);
    memcpy(hook.candidate, candidate, sizeof(candidate));
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(outcomes, 3);
    CHECK(Netchan_Transmit(&chan, sizeof(legacy), legacy, 3) ==
          (int)((8 + sizeof(candidate)) * 3));
    CHECK(test_send.calls == 3 && hook.prepare_calls == 1 &&
          hook.completion_calls == 1);
    CHECK(hook.completion_info.result == expected_result &&
          hook.completion_info.packet_copies == 3 &&
          hook.completion_info.accepted_copies == accepted_count &&
          hook.completion_info.application_bytes == sizeof(candidate) &&
          hook.completion_info.token == hook.token);
    CHECK(hook.completed_application_bytes == sizeof(candidate) &&
          memcmp(hook.completed_application, candidate, sizeof(candidate)) == 0);
    for (unsigned i = 0; i < 3; i++) {
        CHECK(test_send.packet_bytes[i] == 8 + sizeof(candidate));
        CHECK(memcmp(test_send.packets[i] + 8, candidate,
                     sizeof(candidate)) == 0);
    }
    Netchan_Close(&chan);
    return 0;
}

static int test_prepared_packetdup_completion(void)
{
    static const bool all[] = { true, true, true };
    static const bool mixed[] = { false, true, false };
    static const bool none[] = { false, false, false };

    CHECK(run_acceptance_case(all, 3,
                              NETCHAN_APP_TX_COMPLETION_ACCEPTED) == 0);
    CHECK(run_acceptance_case(mixed, 1,
                              NETCHAN_APP_TX_COMPLETION_ACCEPTED) == 0);
    CHECK(run_acceptance_case(none, 0,
                              NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED) == 0);
    return 0;
}

static int run_invalid_case(test_prepare_mode_t mode)
{
    static const byte legacy[] = { 0x41, 0x42, 0x43, 0x44 };
    static const bool outcomes[] = { false, true };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = mode;
    hook.token = UINT64_C(0xfedcba9876543210);
    hook.candidate_bytes = 6;
    memset(hook.candidate, 0xdd, hook.candidate_bytes);
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(outcomes, 2);
    CHECK(Netchan_Transmit(&chan, sizeof(legacy), legacy, 2) ==
          (int)((8 + sizeof(legacy)) * 2));
    CHECK(test_send.calls == 2 && hook.prepare_calls == 1 &&
          hook.completion_calls == 1);
    CHECK(hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID &&
          hook.completion_info.packet_copies == 2 &&
          hook.completion_info.accepted_copies == 1 &&
          hook.completion_info.application_bytes == sizeof(legacy) &&
          hook.completion_info.token == hook.token);
    CHECK(hook.completed_application_bytes == sizeof(legacy) &&
          memcmp(hook.completed_application, legacy, sizeof(legacy)) == 0);
    CHECK(memcmp(test_send.packets[0] + 8, legacy, sizeof(legacy)) == 0 &&
          memcmp(test_send.packets[1], test_send.packets[0],
                 test_send.packet_bytes[0]) == 0);
    Netchan_Close(&chan);
    return 0;
}

static int test_invalid_prepare_is_transactional(void)
{
    CHECK(run_invalid_case(TEST_PREPARE_INVALID_ABI) == 0);
    CHECK(run_invalid_case(TEST_PREPARE_INVALID_RESERVED) == 0);
    CHECK(run_invalid_case(TEST_PREPARE_INVALID_OVERSIZE) == 0);
    CHECK(run_invalid_case(TEST_PREPARE_UNKNOWN) == 0);
    return 0;
}

static int test_zero_packet_copies_still_completes(void)
{
    static const byte legacy[] = { 0x21, 0x22 };
    static const byte candidate[] = { 0x31, 0x32, 0x33 };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = UINT64_C(0x0102030405060708);
    hook.candidate_bytes = sizeof(candidate);
    memcpy(hook.candidate, candidate, sizeof(candidate));
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(NULL, 0);
    CHECK(Netchan_Transmit(&chan, sizeof(legacy), legacy, 0) == 0);
    CHECK(test_send.calls == 0 && hook.prepare_calls == 1 &&
          hook.completion_calls == 1);
    CHECK(hook.prepare_info.packet_copies == 0 &&
          hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED &&
          hook.completion_info.packet_copies == 0 &&
          hook.completion_info.accepted_copies == 0 &&
          hook.completion_info.application_bytes == sizeof(candidate) &&
          hook.completion_info.token == hook.token);
    CHECK(hook.completed_application_bytes == sizeof(candidate) &&
          memcmp(hook.completed_application, candidate, sizeof(candidate)) == 0);
    Netchan_Close(&chan);
    return 0;
}

static int test_callbacks_are_frozen_for_one_transmit(void)
{
    static const byte legacy[] = { 0x51 };
    static const byte candidate[] = { 0x61, 0x62 };
    static const bool accepted[] = { true };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = UINT64_C(0x8899aabbccddeeff);
    hook.candidate_bytes = sizeof(candidate);
    memcpy(hook.candidate, candidate, sizeof(candidate));
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0, TEST_MAX_APPLICATION);
    hook.clear_registration_on_prepare = &chan;
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, sizeof(legacy), legacy, 1) == 10);
    CHECK(hook.clear_registration_result && hook.prepare_calls == 1 &&
          hook.completion_calls == 1);
    CHECK(hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_ACCEPTED &&
          hook.completion_info.token == hook.token &&
          hook.completed_application_bytes == sizeof(candidate) &&
          memcmp(hook.completed_application, candidate, sizeof(candidate)) == 0);
    CHECK(chan.app_tx_prepare == NULL && chan.app_tx_completion == NULL &&
          chan.app_tx_opaque == NULL);
    Netchan_Close(&chan);
    return 0;
}

static int test_exact_capacity_and_zero_application(void)
{
    static const byte legacy[] = { 0x75 };
    static const bool accepted[] = { true };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = 7;
    hook.candidate_bytes = TEST_MAX_APPLICATION;
    for (size_t i = 0; i < hook.candidate_bytes; i++)
        hook.candidate[i] = (byte)i;
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, sizeof(legacy), legacy, 1) ==
          8 + TEST_MAX_APPLICATION);
    CHECK(test_send.packet_bytes[0] == 8 + TEST_MAX_APPLICATION &&
          memcmp(test_send.packets[0] + 8, hook.candidate,
                 TEST_MAX_APPLICATION) == 0);
    CHECK(hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_ACCEPTED &&
          hook.completion_info.application_bytes == TEST_MAX_APPLICATION);
    Netchan_Close(&chan);

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = 8;
    hook.candidate_bytes = 2;
    hook.candidate[0] = 0x91;
    hook.candidate[1] = 0x92;
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, 0, NULL, 1) == 10);
    CHECK(hook.prepare_calls == 1 &&
          hook.prepare_info.reliable_bytes == 0 &&
          hook.prepare_info.unreliable_bytes == 0 &&
          hook.prepare_info.legacy_application_bytes == 0);
    CHECK(hook.legacy_bytes == 0 && hook.completion_calls == 1 &&
          hook.completion_info.application_bytes == 2 &&
          test_send.packet_bytes[0] == 10 &&
          memcmp(test_send.packets[0] + 8, hook.candidate, 2) == 0);
    Netchan_Close(&chan);
    return 0;
}

static int test_reliable_only_and_fragment_bypass(void)
{
    byte oversized[TEST_MAX_APPLICATION + 1];
    static const byte reliable[] = { 0xb1, 0xb2, 0xb3, 0xb4 };
    static const bool accepted[] = { true };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.candidate_bytes = 1;
    hook.candidate[0] = 0xc7;
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0, TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, reliable, sizeof(reliable));
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, 0, NULL, 1) == 9);
    CHECK(hook.prepare_info.reliable_bytes == sizeof(reliable) &&
          hook.prepare_info.unreliable_bytes == 0 &&
          hook.legacy_bytes == sizeof(reliable) &&
          memcmp(hook.legacy, reliable, sizeof(reliable)) == 0);
    CHECK(test_send.packet_bytes[0] == 9 &&
          test_send.packets[0][8] == hook.candidate[0]);
    Netchan_Close(&chan);

    memset(oversized, 0x6d, sizeof(oversized));
    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.candidate_bytes = 1;
    hook.candidate[0] = 0xff;
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, sizeof(oversized), oversized, 3) ==
          10 + TEST_MAX_APPLICATION);
    CHECK(hook.prepare_calls == 0 && hook.completion_calls == 0);
    CHECK(test_send.calls == 1 && chan.fragment_pending &&
          chan.fragment_out.readcount == TEST_MAX_APPLICATION);

    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, 0, NULL, 3) == 11);
    CHECK(hook.prepare_calls == 0 && hook.completion_calls == 0 &&
          test_send.calls == 1 && !chan.fragment_pending);
    Netchan_Close(&chan);
    return 0;
}

static int test_queued_reliable_prefix_handoff_and_retry(void)
{
    static const byte prefix[] = {0xa1, 0xa2, 0xa3};
    static const byte callback_append[] = {0xbc, 0xbd};
    static const byte expected_tail[] = {
        0xb1, 0xb2, 0xb3, 0xbc, 0xbd
    };
    static const byte queued[] = {
        0xa1, 0xa2, 0xa3, 0xb1, 0xb2, 0xb3
    };
    static const bool accepted[] = {true};
    netchan_t chan;
    test_hook_t hook;
    sizebuf_t tail_descriptor;
    int transmit_bytes = -1;
    unsigned prepare_calls;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_BYPASS;
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0,
                 TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, queued, sizeof(queued));
    hook.append_queue_on_prepare = &chan;
    hook.append_queue_byte_count = sizeof(callback_append);
    memcpy(hook.append_queue_bytes, callback_append,
           sizeof(callback_append));
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_TransmitQueuedReliablePrefix(
        &chan, sizeof(prefix), 1, &transmit_bytes));
    CHECK(transmit_bytes == 8 + (int)sizeof(prefix) &&
          test_send.calls == 1 &&
          test_send.packet_bytes[0] == 8 + sizeof(prefix) &&
          memcmp(test_send.packets[0] + 8, prefix,
                 sizeof(prefix)) == 0);
    CHECK(chan.reliable_length == sizeof(prefix) &&
          memcmp(chan.reliable_buf, prefix, sizeof(prefix)) == 0 &&
          chan.message.cursize == sizeof(expected_tail) &&
          memcmp(chan.message.data, expected_tail,
                 sizeof(expected_tail)) == 0 &&
          chan.reliable_sequence && chan.last_reliable_sequence == 1 &&
          chan.outgoing_sequence == 2);
    CHECK(hook.prepare_calls == 1 && hook.completion_calls == 0 &&
          hook.queue_appends == 1 &&
          hook.prepare_info.reliable_bytes == sizeof(prefix) &&
          hook.prepare_info.unreliable_bytes == 0 &&
          hook.legacy_bytes == sizeof(prefix) &&
          memcmp(hook.legacy, prefix, sizeof(prefix)) == 0);

    /* A handed-off generation owns the reliable slot until its exact ACK.
     * Rejection is transactional and cannot consume the preserved tail. */
    tail_descriptor = chan.message;
    prepare_calls = hook.prepare_calls;
    hook.append_queue_on_prepare = NULL;
    reset_send(accepted, 1);
    transmit_bytes = -1;
    CHECK(!Netchan_TransmitQueuedReliablePrefix(
        &chan, 1, 1, &transmit_bytes));
    CHECK(transmit_bytes == 0 && test_send.calls == 0 &&
          hook.prepare_calls == prepare_calls &&
          memcmp(&chan.message, &tail_descriptor,
                 sizeof(tail_descriptor)) == 0 &&
          memcmp(chan.message.data, expected_tail,
                 sizeof(expected_tail)) == 0);

    /* Model a later packet acknowledging past this sequence with the wrong
     * reliable bit.  Netchan retries the exact prefix, never the queued tail. */
    chan.incoming_acknowledged = chan.last_reliable_sequence + 1;
    chan.incoming_reliable_acknowledged = !chan.reliable_sequence;
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, 0, NULL, 1) ==
          8 + (int)sizeof(prefix));
    CHECK(test_send.calls == 1 && hook.prepare_calls == prepare_calls + 1 &&
          hook.legacy_bytes == sizeof(prefix) &&
          memcmp(hook.legacy, prefix, sizeof(prefix)) == 0 &&
          chan.message.cursize == sizeof(expected_tail) &&
          memcmp(chan.message.data, expected_tail,
                 sizeof(expected_tail)) == 0 &&
          chan.reliable_length == sizeof(prefix));

    Netchan_Close(&chan);
    return 0;
}

static int test_isolated_reliable_handoff_preserves_queue(void)
{
    static const byte isolated[] = {0xc1, 0xc2};
    static const byte queued[] = {0xd1, 0xd2, 0xd3, 0xd4};
    static const byte callback_append[] = {0xda};
    static const byte expected_queue[] = {
        0xd1, 0xd2, 0xd3, 0xd4, 0xda
    };
    static const byte candidate[] = {0xe1, 0xe2, 0xe3};
    static const bool accepted[] = {true};
    netchan_t chan;
    test_hook_t hook;
    sizebuf_t queued_descriptor;
    int transmit_bytes = -1;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = UINT64_C(0x1020304050607080);
    hook.candidate_bytes = sizeof(candidate);
    memcpy(hook.candidate, candidate, sizeof(candidate));
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0,
                 TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, queued, sizeof(queued));
    queued_descriptor = chan.message;
    queued_descriptor.cursize += sizeof(callback_append);
    hook.append_queue_on_prepare = &chan;
    hook.append_queue_byte_count = sizeof(callback_append);
    memcpy(hook.append_queue_bytes, callback_append,
           sizeof(callback_append));
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_TransmitIsolatedReliable(
        &chan, isolated, sizeof(isolated), 1, &transmit_bytes));
    CHECK(transmit_bytes == 8 + (int)sizeof(candidate) &&
          test_send.calls == 1 &&
          test_send.packet_bytes[0] == 8 + sizeof(candidate) &&
          memcmp(test_send.packets[0] + 8, candidate,
                 sizeof(candidate)) == 0);
    CHECK(memcmp(&chan.message, &queued_descriptor,
                 sizeof(queued_descriptor)) == 0 &&
          memcmp(chan.message.data, expected_queue,
                 sizeof(expected_queue)) == 0 &&
          chan.reliable_length == sizeof(isolated) &&
          memcmp(chan.reliable_buf, isolated, sizeof(isolated)) == 0);
    CHECK(hook.prepare_calls == 1 && hook.completion_calls == 1 &&
          hook.queue_appends == 1 &&
          hook.prepare_info.reliable_bytes == sizeof(isolated) &&
          hook.prepare_info.unreliable_bytes == 0 &&
          hook.legacy_bytes == sizeof(isolated) &&
          memcmp(hook.legacy, isolated, sizeof(isolated)) == 0 &&
          hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_ACCEPTED &&
          hook.completion_info.token == hook.token);

    /* A retry remains isolated as well; the ordinary queue is still the
     * byte-identical later generation. */
    chan.incoming_acknowledged = chan.last_reliable_sequence + 1;
    chan.incoming_reliable_acknowledged = !chan.reliable_sequence;
    hook.append_queue_on_prepare = NULL;
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, 0, NULL, 1) ==
          8 + (int)sizeof(candidate));
    CHECK(hook.prepare_calls == 2 && hook.completion_calls == 2 &&
          hook.legacy_bytes == sizeof(isolated) &&
          memcmp(hook.legacy, isolated, sizeof(isolated)) == 0 &&
          memcmp(&chan.message, &queued_descriptor,
                 sizeof(queued_descriptor)) == 0 &&
          memcmp(chan.message.data, expected_queue,
                 sizeof(expected_queue)) == 0);

    /* Model the reliable ACK.  The next ordinary transfer takes the preserved
     * queue, proving it was neither merged into nor reordered before isolated. */
    chan.reliable_length = 0;
    chan.incoming_acknowledged = chan.last_reliable_sequence;
    chan.incoming_reliable_acknowledged = chan.reliable_sequence;
    hook.mode = TEST_PREPARE_BYPASS;
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, 0, NULL, 1) ==
          8 + (int)sizeof(expected_queue));
    CHECK(chan.message.cursize == 0 &&
          chan.reliable_length == sizeof(expected_queue) &&
          memcmp(chan.reliable_buf, expected_queue,
                 sizeof(expected_queue)) == 0 &&
          hook.prepare_calls == 3 &&
          hook.legacy_bytes == sizeof(expected_queue) &&
          memcmp(hook.legacy, expected_queue,
                 sizeof(expected_queue)) == 0);

    Netchan_Close(&chan);
    return 0;
}

static int test_reliable_handoff_fragment_and_busy_rejection(void)
{
    enum {
        PREFIX_BYTES = TEST_MAX_APPLICATION + 1,
        TAIL_BYTES = 2,
    };
    byte queued[PREFIX_BYTES + TAIL_BYTES];
    static const byte isolated[] = {0xf1};
    static const bool accepted[] = {true};
    netchan_t chan;
    test_hook_t hook;
    int transmit_bytes = -1;

    for (size_t index = 0; index < PREFIX_BYTES; ++index)
        queued[index] = (byte)index;
    queued[PREFIX_BYTES] = 0x71;
    queued[PREFIX_BYTES + 1] = 0x72;
    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.candidate_bytes = 1;
    hook.candidate[0] = 0xff;
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0,
                 TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, queued, sizeof(queued));
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_TransmitQueuedReliablePrefix(
        &chan, PREFIX_BYTES, 1, &transmit_bytes));
    CHECK(transmit_bytes == 10 + TEST_MAX_APPLICATION &&
          test_send.calls == 1 && hook.prepare_calls == 0 &&
          hook.completion_calls == 0 && chan.fragment_pending &&
          chan.fragment_out.cursize == PREFIX_BYTES &&
          chan.fragment_out.readcount == TEST_MAX_APPLICATION &&
          chan.reliable_length == PREFIX_BYTES &&
          memcmp(chan.reliable_buf, queued, PREFIX_BYTES) == 0 &&
          chan.message.cursize == TAIL_BYTES &&
          memcmp(chan.message.data, queued + PREFIX_BYTES,
                 TAIL_BYTES) == 0);

    /* Neither helper may create a new generation while the fragment owner is
     * live, and finishing fragments still leaves the reliable ACK gate busy. */
    reset_send(accepted, 1);
    transmit_bytes = -1;
    CHECK(!Netchan_TransmitQueuedReliablePrefix(
        &chan, 1, 1, &transmit_bytes));
    CHECK(transmit_bytes == 0 && test_send.calls == 0);
    transmit_bytes = -1;
    CHECK(!Netchan_TransmitIsolatedReliable(
        &chan, isolated, sizeof(isolated), 1, &transmit_bytes));
    CHECK(transmit_bytes == 0 && test_send.calls == 0);

    reset_send(accepted, 1);
    CHECK(Netchan_TransmitNextFragment(&chan) == 11);
    CHECK(test_send.calls == 1 && !chan.fragment_pending &&
          chan.fragment_out.cursize == 0 &&
          chan.fragment_out.readcount == 0 &&
          chan.reliable_length == PREFIX_BYTES);
    transmit_bytes = -1;
    CHECK(!Netchan_TransmitIsolatedReliable(
        &chan, isolated, sizeof(isolated), 1, &transmit_bytes));
    CHECK(transmit_bytes == 0 &&
          chan.message.cursize == TAIL_BYTES &&
          memcmp(chan.message.data, queued + PREFIX_BYTES,
                 TAIL_BYTES) == 0);

    Netchan_Close(&chan);
    return 0;
}

static int test_reliable_handoff_send_rejection_retains_retry(void)
{
    static const byte isolated[] = {0x61, 0x62};
    static const byte queued[] = {0x71, 0x72};
    static const byte candidate[] = {0x81, 0x82, 0x83};
    static const bool rejected[] = {false};
    netchan_t chan;
    test_hook_t hook;
    int transmit_bytes = -1;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.candidate_bytes = sizeof(candidate);
    memcpy(hook.candidate, candidate, sizeof(candidate));
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0,
                 TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, queued, sizeof(queued));
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(rejected, 1);
    CHECK(Netchan_TransmitIsolatedReliable(
        &chan, isolated, sizeof(isolated), 1, &transmit_bytes));
    CHECK(transmit_bytes == 8 + (int)sizeof(candidate) &&
          test_send.calls == 1 && hook.prepare_calls == 1 &&
          hook.completion_calls == 1 &&
          hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED &&
          hook.completion_info.accepted_copies == 0 &&
          chan.reliable_length == sizeof(isolated) &&
          memcmp(chan.reliable_buf, isolated, sizeof(isolated)) == 0 &&
          chan.message.cursize == sizeof(queued) &&
          memcmp(chan.message.data, queued, sizeof(queued)) == 0);

    chan.incoming_acknowledged = chan.last_reliable_sequence + 1;
    chan.incoming_reliable_acknowledged = !chan.reliable_sequence;
    reset_send(rejected, 1);
    CHECK(Netchan_Transmit(&chan, 0, NULL, 1) ==
          8 + (int)sizeof(candidate));
    CHECK(hook.prepare_calls == 2 && hook.completion_calls == 2 &&
          hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED &&
          chan.reliable_length == sizeof(isolated) &&
          memcmp(chan.reliable_buf, isolated, sizeof(isolated)) == 0 &&
          chan.message.cursize == sizeof(queued) &&
          memcmp(chan.message.data, queued, sizeof(queued)) == 0);

    Netchan_Close(&chan);
    return 0;
}

static int test_reliable_handoff_invalid_inputs_are_transactional(void)
{
    static const byte queued[] = {0x81, 0x82};
    static const byte isolated[] = {0x91};
    netchan_t old_chan;
    netchan_t chan;
    sizebuf_t descriptor;
    int transmit_bytes;

    init_channel(&old_chan, NETCHAN_OLD, NS_SERVER, 0,
                 TEST_MAX_APPLICATION);
    SZ_Write(&old_chan.message, queued, sizeof(queued));
    descriptor = old_chan.message;
    transmit_bytes = -1;
    CHECK(!Netchan_TransmitQueuedReliablePrefix(
        &old_chan, 1, 1, &transmit_bytes));
    CHECK(transmit_bytes == 0 &&
          memcmp(&old_chan.message, &descriptor, sizeof(descriptor)) == 0 &&
          memcmp(old_chan.message.data, queued, sizeof(queued)) == 0);
    transmit_bytes = -1;
    CHECK(!Netchan_TransmitIsolatedReliable(
        &old_chan, isolated, sizeof(isolated), 1, &transmit_bytes));
    CHECK(transmit_bytes == 0 &&
          memcmp(&old_chan.message, &descriptor, sizeof(descriptor)) == 0 &&
          memcmp(old_chan.message.data, queued, sizeof(queued)) == 0);
    Netchan_Close(&old_chan);

    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0,
                 TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, queued, sizeof(queued));
    descriptor = chan.message;
    reset_send(NULL, 0);
#define CHECK_PREFIX_REJECT(bytes_, copies_)                                \
    do {                                                                    \
        transmit_bytes = -1;                                                \
        CHECK(!Netchan_TransmitQueuedReliablePrefix(                        \
            &chan, (bytes_), (copies_), &transmit_bytes));                  \
        CHECK(transmit_bytes == 0 && test_send.calls == 0 &&                \
              memcmp(&chan.message, &descriptor, sizeof(descriptor)) == 0 &&\
              memcmp(chan.message.data, queued, sizeof(queued)) == 0);      \
    } while (0)
#define CHECK_ISOLATED_REJECT(data_, bytes_, copies_)                       \
    do {                                                                    \
        transmit_bytes = -1;                                                \
        CHECK(!Netchan_TransmitIsolatedReliable(                            \
            &chan, (data_), (bytes_), (copies_), &transmit_bytes));         \
        CHECK(transmit_bytes == 0 && test_send.calls == 0 &&                \
              memcmp(&chan.message, &descriptor, sizeof(descriptor)) == 0 &&\
              memcmp(chan.message.data, queued, sizeof(queued)) == 0);      \
    } while (0)

    CHECK_PREFIX_REJECT(0, 1);
    CHECK_PREFIX_REJECT(sizeof(queued) + 1, 1);
    CHECK_PREFIX_REJECT(1, 0);
    CHECK_PREFIX_REJECT(1, -1);
    CHECK_ISOLATED_REJECT(NULL, sizeof(isolated), 1);
    CHECK_ISOLATED_REJECT(isolated, 0, 1);
    CHECK_ISOLATED_REJECT(isolated, sizeof(isolated), 0);
    CHECK_ISOLATED_REJECT(isolated, sizeof(isolated), -1);
    CHECK_ISOLATED_REJECT(
        isolated, chan.message.maxsize + 1u, 1);

    chan.message.overflowed = true;
    descriptor = chan.message;
    CHECK_PREFIX_REJECT(1, 1);
    chan.message.overflowed = false;
    chan.message.readcount = 1;
    descriptor = chan.message;
    CHECK_PREFIX_REJECT(1, 1);
    chan.message.readcount = 0;
    chan.message.cursize = chan.message.maxsize + 1u;
    descriptor = chan.message;
    CHECK_ISOLATED_REJECT(isolated, sizeof(isolated), 1);
    chan.message.cursize = sizeof(queued);
    descriptor = chan.message;
    chan.reliable_length = 1;
    CHECK_ISOLATED_REJECT(isolated, sizeof(isolated), 1);
    chan.reliable_length = 0;
    chan.fragment_pending = true;
    CHECK_PREFIX_REJECT(1, 1);
    chan.fragment_pending = false;
    chan.fragment_out.cursize = 1;
    CHECK_ISOLATED_REJECT(isolated, sizeof(isolated), 1);
    chan.fragment_out.cursize = 0;
    chan.fragment_out.overflowed = true;
    CHECK_PREFIX_REJECT(1, 1);

#undef CHECK_ISOLATED_REJECT
#undef CHECK_PREFIX_REJECT
    Netchan_Close(&chan);
    return 0;
}

static int test_registration_and_teardown(void)
{
    netchan_t old_chan;
    netchan_t new_chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    init_channel(&old_chan, NETCHAN_OLD, NS_SERVER, 0,
                 TEST_MAX_APPLICATION);
    CHECK(!Netchan_SetApplicationTxHook(
              &old_chan, test_prepare, test_completion, &hook));
    CHECK(old_chan.app_tx_prepare == NULL &&
          old_chan.app_tx_completion == NULL &&
          old_chan.app_tx_opaque == NULL);
    Netchan_Close(&old_chan);

    init_channel(&new_chan, NETCHAN_NEW, NS_SERVER, 0,
                 TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &new_chan, test_prepare, test_completion, &hook));
    CHECK(new_chan.app_tx_prepare == test_prepare &&
          new_chan.app_tx_completion == test_completion &&
          new_chan.app_tx_opaque == &hook);
    CHECK(!Netchan_SetApplicationTxHook(
              &new_chan, test_prepare, NULL, NULL));
    CHECK(new_chan.app_tx_prepare == test_prepare &&
          new_chan.app_tx_completion == test_completion &&
          new_chan.app_tx_opaque == &hook);
    CHECK(Netchan_SetApplicationTxHook(&new_chan, NULL, NULL, &hook));
    CHECK(new_chan.app_tx_prepare == NULL &&
          new_chan.app_tx_completion == NULL &&
          new_chan.app_tx_opaque == NULL);
    CHECK(Netchan_SetApplicationTxHook(
              &new_chan, test_prepare, test_completion, &hook));
    Netchan_Close(&new_chan);
    for (size_t i = 0; i < sizeof(new_chan); i++)
        CHECK(((const byte *)&new_chan)[i] == 0);
    CHECK(!Netchan_SetApplicationTxHook(
              NULL, test_prepare, test_completion, &hook));
    return 0;
}

int main(void)
{
    Netchan_Init();
    if (test_default_and_bypass_are_byte_identical() != 0)
        return 1;
    if (test_prepared_packetdup_completion() != 0)
        return 1;
    if (test_invalid_prepare_is_transactional() != 0)
        return 1;
    if (test_zero_packet_copies_still_completes() != 0)
        return 1;
    if (test_callbacks_are_frozen_for_one_transmit() != 0)
        return 1;
    if (test_exact_capacity_and_zero_application() != 0)
        return 1;
    if (test_reliable_only_and_fragment_bypass() != 0)
        return 1;
    if (test_queued_reliable_prefix_handoff_and_retry() != 0)
        return 1;
    if (test_isolated_reliable_handoff_preserves_queue() != 0)
        return 1;
    if (test_reliable_handoff_fragment_and_busy_rejection() != 0)
        return 1;
    if (test_reliable_handoff_send_rejection_retains_retry() != 0)
        return 1;
    if (test_reliable_handoff_invalid_inputs_are_transactional() != 0)
        return 1;
    if (test_registration_and_teardown() != 0)
        return 1;

    puts("netchan application TX hook tests passed");
    return 0;
}
