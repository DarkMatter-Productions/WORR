/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client/cgame_event_runtime.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,    \
                         __LINE__, #condition);                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

struct reset_call_t {
    uint32_t stream_epoch;
    uint32_t first_sequence;
};

struct fake_consumer_t {
    std::array<reset_call_t, 256> reset_calls{};
    uint32_t reset_call_count{};
    worr_cgame_event_runtime_result_v1 reset_result{
        WORR_CGAME_EVENT_RUNTIME_OK};

    uint32_t submit_call_count{};
    const worr_event_record_v1 *submitted_records{};
    uint32_t submitted_count{};
    worr_event_record_v1 copied_first_record{};
    worr_cgame_event_runtime_result_v1 submit_result{
        WORR_CGAME_EVENT_RUNTIME_OK};

    uint32_t status_call_count{};
    bool status_result{true};
    bool auto_status_on_reset{true};
    worr_cgame_event_runtime_status_v1 returned_status{};
};

fake_consumer_t fake;
fake_consumer_t replacement_fake;

void set_reset_status(fake_consumer_t &target, uint32_t stream_epoch,
                      uint32_t first_sequence)
{
    target.returned_status = {};
    target.returned_status.struct_size = sizeof(target.returned_status);
    target.returned_status.api_version =
        WORR_CGAME_EVENT_RUNTIME_API_VERSION;
    if (stream_epoch == 0)
        return;

    target.returned_status.authority_epoch = stream_epoch;
    target.returned_status.next_presentation_sequence = first_sequence;
    target.returned_status.state_flags =
        WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE;
    target.returned_status.receipt.struct_size =
        sizeof(target.returned_status.receipt);
    target.returned_status.receipt.schema_version = WORR_EVENT_ABI_VERSION;
    target.returned_status.receipt.stream_epoch = stream_epoch;
    target.returned_status.receipt.highest_contiguous = first_sequence - 1;
}

worr_cgame_event_runtime_result_v1 record_reset(
    fake_consumer_t &target, uint32_t stream_epoch, uint32_t first_sequence)
{
    CHECK(target.reset_call_count < target.reset_calls.size());
    target.reset_calls[target.reset_call_count++] = {
        stream_epoch,
        first_sequence,
    };
    if (target.reset_result == WORR_CGAME_EVENT_RUNTIME_OK &&
        target.auto_status_on_reset) {
        set_reset_status(target, stream_epoch, first_sequence);
    }
    return target.reset_result;
}

worr_cgame_event_runtime_result_v1 record_submit(
    fake_consumer_t &target, const worr_event_record_v1 *records,
    uint32_t count)
{
    ++target.submit_call_count;
    target.submitted_records = records;
    target.submitted_count = count;
    if (records && count != 0)
        target.copied_first_record = records[0];
    return target.submit_result;
}

bool record_status(fake_consumer_t &target,
                   worr_cgame_event_runtime_status_v1 *status_out)
{
    ++target.status_call_count;
    CHECK(status_out != nullptr);
    if (!target.status_result)
        return false;
    *status_out = target.returned_status;
    return true;
}

worr_cgame_event_runtime_result_v1 fake_reset(uint32_t stream_epoch,
                                               uint32_t first_sequence)
{
    return record_reset(fake, stream_epoch, first_sequence);
}

worr_cgame_event_runtime_result_v1 fake_submit(
    const worr_event_record_v1 *records, uint32_t count)
{
    return record_submit(fake, records, count);
}

bool fake_get_status(worr_cgame_event_runtime_status_v1 *status_out)
{
    return record_status(fake, status_out);
}

worr_cgame_event_runtime_result_v1 replacement_reset(
    uint32_t stream_epoch, uint32_t first_sequence)
{
    return record_reset(replacement_fake, stream_epoch, first_sequence);
}

worr_cgame_event_runtime_result_v1 replacement_submit(
    const worr_event_record_v1 *records, uint32_t count)
{
    return record_submit(replacement_fake, records, count);
}

bool replacement_get_status(
    worr_cgame_event_runtime_status_v1 *status_out)
{
    return record_status(replacement_fake, status_out);
}

const worr_cgame_event_runtime_export_v1 valid_consumer{
    sizeof(worr_cgame_event_runtime_export_v1),
    WORR_CGAME_EVENT_RUNTIME_API_VERSION,
    fake_reset,
    fake_submit,
    fake_get_status,
};

const worr_cgame_event_runtime_export_v1 replacement_consumer{
    sizeof(worr_cgame_event_runtime_export_v1),
    WORR_CGAME_EVENT_RUNTIME_API_VERSION,
    replacement_reset,
    replacement_submit,
    replacement_get_status,
};

void clear_fake()
{
    fake = {};
    fake.reset_result = WORR_CGAME_EVENT_RUNTIME_OK;
    fake.submit_result = WORR_CGAME_EVENT_RUNTIME_OK;
    fake.status_result = true;
    fake.auto_status_on_reset = true;
    set_reset_status(fake, 0, 0);
}

void clear_replacement_fake()
{
    replacement_fake = {};
    replacement_fake.reset_result = WORR_CGAME_EVENT_RUNTIME_OK;
    replacement_fake.submit_result = WORR_CGAME_EVENT_RUNTIME_OK;
    replacement_fake.status_result = true;
    replacement_fake.auto_status_on_reset = true;
    set_reset_status(replacement_fake, 0, 0);
}

void expect_reset(uint32_t call_index, uint32_t stream_epoch,
                  uint32_t first_sequence)
{
    CHECK(call_index < fake.reset_call_count);
    CHECK(fake.reset_calls[call_index].stream_epoch == stream_epoch);
    CHECK(fake.reset_calls[call_index].first_sequence == first_sequence);
}

worr_cgame_event_runtime_status_v1 inactive_status()
{
    worr_cgame_event_runtime_status_v1 result{};
    result.struct_size = sizeof(result);
    result.api_version = WORR_CGAME_EVENT_RUNTIME_API_VERSION;
    return result;
}

worr_cgame_event_runtime_status_v1 active_status(uint32_t stream_epoch,
                                                  uint32_t next_sequence)
{
    worr_cgame_event_runtime_status_v1 result{};
    result.struct_size = sizeof(result);
    result.api_version = WORR_CGAME_EVENT_RUNTIME_API_VERSION;
    result.authority_epoch = stream_epoch;
    result.next_presentation_sequence = next_sequence;
    result.authority_count = 3;
    result.state_flags = WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE |
                         WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED |
                         WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED;
    result.receipt.struct_size = sizeof(result.receipt);
    result.receipt.schema_version = WORR_EVENT_ABI_VERSION;
    result.receipt.stream_epoch = stream_epoch;
    result.receipt.highest_contiguous = next_sequence - 1;
    result.receipt.selective_mask = UINT64_C(0x1020408102040810);
    return result;
}

void expect_status_rejected(
    const worr_cgame_event_runtime_status_v1 &invalid_status)
{
    fake.returned_status = invalid_status;
    worr_cgame_event_runtime_status_v1 output{};
    std::memset(&output, 0xa5, sizeof(output));
    const auto before = output;
    const bool accepted = CL_CGameEventRuntimeGetStatus(&output);
    if (accepted) {
        std::fprintf(stderr,
                     "unexpected status acceptance: epoch=%u next=%u "
                     "flags=0x%x receipt_epoch=%u\n",
                     invalid_status.authority_epoch,
                     invalid_status.next_presentation_sequence,
                     invalid_status.state_flags,
                     invalid_status.receipt.stream_epoch);
    }
    CHECK(!accepted);
    CHECK(std::memcmp(&output, &before, sizeof(output)) == 0);
}

void restore_after_quarantine(uint32_t lost_epoch, uint32_t new_epoch)
{
    fake.reset_result = WORR_CGAME_EVENT_RUNTIME_OK;
    fake.submit_result = WORR_CGAME_EVENT_RUNTIME_OK;
    fake.status_result = true;
    CHECK(CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    expect_reset(fake.reset_call_count - 1, 0, 0);
    const auto before_wrong_epoch = fake.reset_call_count;
    CHECK(CL_CGameEventRuntimeResetAuthority(lost_epoch, 999) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    CHECK(fake.reset_call_count == before_wrong_epoch);
    CHECK(CL_CGameEventRuntimeResetAuthority(new_epoch, 12) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(!CL_CGameEventRuntimeRequiresResync());
}

void test_reset_replay_detach_and_deactivation()
{
    clear_fake();
    clear_replacement_fake();
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CHECK(!CL_CGameEventRuntimeRequiresResync());

    /* A reset made before cgame attachment is replayed synchronously. */
    CHECK(CL_CGameEventRuntimeResetAuthority(41, 9) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(fake.reset_call_count == 0);
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    CHECK(fake.reset_call_count == 1);
    expect_reset(0, 41, 9);
    CHECK(!CL_CGameEventRuntimeRequiresResync());

    /* Re-selecting the same live table is idempotent. */
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    CHECK(fake.reset_call_count == 1);

    /* An active detach loses runtime state and establishes a hard barrier. */
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CHECK(fake.reset_call_count == 2);
    expect_reset(1, 0, 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());

    /* There must be no callback through an unloaded table. */
    const auto reset_calls = fake.reset_call_count;
    const auto submit_calls = fake.submit_call_count;
    const auto status_calls = fake.status_call_count;
    worr_event_record_v1 record{};
    worr_cgame_event_runtime_status_v1 status{};
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_NOT_READY);
    CHECK(!CL_CGameEventRuntimeGetStatus(&status));
    CHECK(fake.reset_call_count == reset_calls);
    CHECK(fake.submit_call_count == submit_calls);
    CHECK(fake.status_call_count == status_calls);

    /* Reload starts inactive and cannot consume the old authority stream. */
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    CHECK(fake.reset_call_count == reset_calls + 1);
    expect_reset(reset_calls, 0, 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_NOT_READY);
    CHECK(fake.submit_call_count == submit_calls);
    CHECK(CL_CGameEventRuntimeResetAuthority(41, 20) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    CHECK(fake.reset_call_count == reset_calls + 1);

    CHECK(CL_CGameEventRuntimeResetAuthority(42, 10) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    expect_reset(reset_calls + 1, 42, 10);
    CHECK(!CL_CGameEventRuntimeRequiresResync());

    /* Reasserting the exact active descriptor is an owner-side no-op. */
    const auto before_no_op = fake.reset_call_count;
    CHECK(CL_CGameEventRuntimeResetAuthority(42, 10) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(fake.reset_call_count == before_no_op);
    CHECK(CL_CGameEventRuntimeResetAuthority(42, 11) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    CHECK(fake.reset_call_count == before_no_op);

    /* Replacing an active consumer also requires a fresh authority epoch. */
    CHECK(CL_CGameEventRuntimeSetConsumer(&replacement_consumer));
    CHECK(fake.reset_call_count == before_no_op + 1);
    expect_reset(before_no_op, 0, 0);
    CHECK(replacement_fake.reset_call_count == 1);
    CHECK(replacement_fake.reset_calls[0].stream_epoch == 0);
    CHECK(replacement_fake.reset_calls[0].first_sequence == 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_NOT_READY);
    CHECK(replacement_fake.submit_call_count == 0);
    CHECK(CL_CGameEventRuntimeResetAuthority(42, 99) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    CHECK(replacement_fake.reset_call_count == 1);

    /* The connection-lifetime high-water rejects every previously usable
     * epoch after A=41 and B=42 have both been lost. */
    CHECK(CL_CGameEventRuntimeResetAuthority(41, 9) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    CHECK(CL_CGameEventRuntimeResetAuthority(1, 1) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    CHECK(replacement_fake.reset_call_count == 1);
    CHECK(CL_CGameEventRuntimeResetAuthority(43, 1) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(replacement_fake.reset_call_count == 2);
    CHECK(replacement_fake.reset_calls[1].stream_epoch == 43);
    CHECK(replacement_fake.reset_calls[1].first_sequence == 1);
    CHECK(!CL_CGameEventRuntimeRequiresResync());

    /* Explicit {0, 0} clears the descriptor, barrier, and epoch high-water. */
    CHECK(CL_CGameEventRuntimeResetAuthority(0, 0) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(replacement_fake.reset_call_count == 3);
    CHECK(replacement_fake.reset_calls[2].stream_epoch == 0);
    CHECK(replacement_fake.reset_calls[2].first_sequence == 0);
    CHECK(!CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CHECK(!CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSetConsumer(&replacement_consumer));
    CHECK(replacement_fake.reset_calls[
              replacement_fake.reset_call_count - 1].stream_epoch == 0);
    CHECK(!CL_CGameEventRuntimeRequiresResync());

    /* A full disconnect intentionally permits a new connection to reuse an
     * otherwise stale epoch value. */
    CHECK(CL_CGameEventRuntimeResetAuthority(41, 2) ==
          WORR_CGAME_EVENT_RUNTIME_OK);

    /* Attach-before-reset forwards a new active descriptor normally. */
    CHECK(CL_CGameEventRuntimeResetAuthority(51, 4) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(!CL_CGameEventRuntimeRequiresResync());

    /* Restore the primary fake through another deliberate resync. */
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    CHECK(CL_CGameEventRuntimeRequiresResync());
    expect_reset(fake.reset_call_count - 1, 0, 0);
    CHECK(CL_CGameEventRuntimeResetAuthority(51, 5) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    CHECK(CL_CGameEventRuntimeResetAuthority(61, 12) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(!CL_CGameEventRuntimeRequiresResync());
}

void test_descriptor_and_callback_validation()
{
    const auto reset_calls = fake.reset_call_count;
    CHECK(CL_CGameEventRuntimeResetAuthority(0, 1) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(CL_CGameEventRuntimeResetAuthority(1, 0) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(fake.reset_call_count == reset_calls);

    auto bad_size = valid_consumer;
    bad_size.struct_size = sizeof(bad_size) - 1;
    auto bad_version = valid_consumer;
    bad_version.api_version = WORR_CGAME_EVENT_RUNTIME_API_VERSION + 1;
    auto no_reset = valid_consumer;
    no_reset.ResetAuthority = nullptr;
    auto no_submit = valid_consumer;
    no_submit.SubmitAuthoritativeBatch = nullptr;
    auto no_status = valid_consumer;
    no_status.GetStatus = nullptr;

    CHECK(!CL_CGameEventRuntimeSetConsumer(&bad_size));
    CHECK(!CL_CGameEventRuntimeSetConsumer(&bad_version));
    CHECK(!CL_CGameEventRuntimeSetConsumer(&no_reset));
    CHECK(!CL_CGameEventRuntimeSetConsumer(&no_submit));
    CHECK(!CL_CGameEventRuntimeSetConsumer(&no_status));
    CHECK(fake.reset_call_count == reset_calls);

    /* A rejected table cannot displace the current valid consumer. */
    worr_event_record_v1 record{};
    fake.submit_result = WORR_CGAME_EVENT_RUNTIME_DUPLICATE;
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_DUPLICATE);

    /* A non-OK reset leaves cgame state uncertain, quarantines the table, and
     * burns the attempted epoch because the callback was irreversible. */
    const auto before_failed_reset = fake.reset_call_count;
    fake.reset_result = WORR_CGAME_EVENT_RUNTIME_CONFLICT;
    CHECK(CL_CGameEventRuntimeResetAuthority(77, 3) ==
          WORR_CGAME_EVENT_RUNTIME_CONFLICT);
    CHECK(fake.reset_call_count == before_failed_reset + 2);
    expect_reset(before_failed_reset, 77, 3);
    expect_reset(before_failed_reset + 1, 0, 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_NOT_READY);

    fake.reset_result = WORR_CGAME_EVENT_RUNTIME_OK;
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    expect_reset(fake.reset_call_count - 1, 0, 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    const auto before_wrong_epoch = fake.reset_call_count;
    CHECK(CL_CGameEventRuntimeResetAuthority(77, 99) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    CHECK(fake.reset_call_count == before_wrong_epoch);
    CHECK(CL_CGameEventRuntimeResetAuthority(78, 12) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(!CL_CGameEventRuntimeRequiresResync());

    /* A failed A-to-B replacement loses A and never commits B. */
    clear_replacement_fake();
    replacement_fake.reset_result = WORR_CGAME_EVENT_RUNTIME_CONFLICT;
    const auto primary_before_replace = fake.reset_call_count;
    CHECK(!CL_CGameEventRuntimeSetConsumer(&replacement_consumer));
    CHECK(fake.reset_call_count == primary_before_replace + 1);
    expect_reset(primary_before_replace, 0, 0);
    CHECK(replacement_fake.reset_call_count == 2);
    CHECK(replacement_fake.reset_calls[0].stream_epoch == 0);
    CHECK(replacement_fake.reset_calls[1].stream_epoch == 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_NOT_READY);

    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    expect_reset(fake.reset_call_count - 1, 0, 0);
    CHECK(CL_CGameEventRuntimeResetAuthority(78, 100) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    CHECK(CL_CGameEventRuntimeResetAuthority(79, 12) ==
          WORR_CGAME_EVENT_RUNTIME_OK);

    /* Disconnect clears owner state even when cgame rejects the scrub. */
    fake.reset_result = WORR_CGAME_EVENT_RUNTIME_CONFLICT;
    CHECK(CL_CGameEventRuntimeResetAuthority(0, 0) ==
          WORR_CGAME_EVENT_RUNTIME_CONFLICT);
    CHECK(!CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_UNINITIALIZED);
    fake.reset_result = WORR_CGAME_EVENT_RUNTIME_OK;
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    expect_reset(fake.reset_call_count - 1, 0, 0);
    CHECK(!CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeResetAuthority(64, 12) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
}

void test_batch_forwarding_and_result_propagation()
{
    fake.submit_call_count = 0;
    fake.submitted_records = nullptr;
    fake.submitted_count = 0;

    worr_event_record_v1 record{};
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(nullptr, 1) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 0) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(
              &record, WORR_CGAME_EVENT_RUNTIME_MAX_BATCH + 1) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(fake.submit_call_count == 0);

    std::array<worr_event_record_v1, 2> records{};
    records[0].struct_size = sizeof(records[0]);
    records[0].schema_version = WORR_EVENT_ABI_VERSION;
    records[0].event_id = {61, 12};
    records[0].payload[0] = 0x5a;
    records[1].event_id = {61, 13};

    fake.submit_result = WORR_CGAME_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION;
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(records.data(),
                                                        records.size()) ==
          WORR_CGAME_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION);
    CHECK(fake.submit_call_count == 1);
    CHECK(fake.submitted_records == records.data());
    CHECK(fake.submitted_count == records.size());
    CHECK(std::memcmp(&fake.copied_first_record, &records[0],
                      sizeof(records[0])) == 0);

    fake.submit_result = WORR_CGAME_EVENT_RUNTIME_CAPACITY;
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(records.data(), 1) ==
          WORR_CGAME_EVENT_RUNTIME_CAPACITY);
    CHECK(fake.submit_call_count == 2);
    CHECK(!CL_CGameEventRuntimeRequiresResync());
}

void test_unknown_result_quarantine()
{
    worr_event_record_v1 record{};

    /* Unknown submit results cannot be trusted or forwarded to transport. */
    fake.submit_result = UINT32_MAX;
    const auto resets_before_submit = fake.reset_call_count;
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_DEGRADED);
    CHECK(fake.reset_call_count == resets_before_submit + 1);
    expect_reset(resets_before_submit, 0, 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_NOT_READY);
    restore_after_quarantine(64, 65);

    /* Unknown reset results quarantine and retain the attempted epoch. */
    fake.reset_result = UINT32_MAX;
    const auto resets_before_reset = fake.reset_call_count;
    CHECK(CL_CGameEventRuntimeResetAuthority(66, 12) ==
          WORR_CGAME_EVENT_RUNTIME_DEGRADED);
    CHECK(fake.reset_call_count == resets_before_reset + 2);
    expect_reset(resets_before_reset, 66, 12);
    expect_reset(resets_before_reset + 1, 0, 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    restore_after_quarantine(66, 67);

    /* An unknown disconnect result still clears the owner-side barrier. */
    fake.reset_result = UINT32_MAX;
    CHECK(CL_CGameEventRuntimeResetAuthority(0, 0) ==
          WORR_CGAME_EVENT_RUNTIME_DEGRADED);
    CHECK(!CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_UNINITIALIZED);
    fake.reset_result = WORR_CGAME_EVENT_RUNTIME_OK;
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    expect_reset(fake.reset_call_count - 1, 0, 0);
    CHECK(CL_CGameEventRuntimeResetAuthority(68, 12) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
}

void test_status_copy_and_validation()
{
    fake.status_call_count = 0;
    CHECK(!CL_CGameEventRuntimeGetStatus(nullptr));
    CHECK(fake.status_call_count == 0);

    /* Inactive and active status values are copied byte-for-byte. */
    CHECK(CL_CGameEventRuntimeResetAuthority(0, 0) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    fake.status_result = true;
    fake.returned_status = inactive_status();
    worr_cgame_event_runtime_status_v1 output{};
    CHECK(CL_CGameEventRuntimeGetStatus(&output));
    CHECK(std::memcmp(&output, &fake.returned_status, sizeof(output)) == 0);

    CHECK(CL_CGameEventRuntimeResetAuthority(69, 12) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    fake.returned_status = active_status(69, 12);
    CHECK(CL_CGameEventRuntimeGetStatus(&output));
    CHECK(std::memcmp(&output, &fake.returned_status, sizeof(output)) == 0);

    /* Callback failure is an uncertain module state and is quarantined. */
    fake.status_result = false;
    std::memset(&output, 0x3c, sizeof(output));
    const auto before_failure = output;
    CHECK(!CL_CGameEventRuntimeGetStatus(&output));
    CHECK(std::memcmp(&output, &before_failure, sizeof(output)) == 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    restore_after_quarantine(69, 70);

    /* Every malformed status is rejected without touching caller output. */
    uint32_t active_epoch = 70;
    for (uint32_t index = 0; index < 10; ++index) {
        auto invalid = active_status(active_epoch, 12);
        switch (index) {
        case 0:
            invalid.struct_size--;
            break;
        case 1:
            invalid.api_version++;
            break;
        case 2:
            invalid.state_flags |= UINT32_C(0x80000000);
            break;
        case 3:
            invalid = inactive_status();
            invalid.authority_epoch = active_epoch;
            break;
        case 4:
            invalid = inactive_status();
            invalid.receipt.struct_size = sizeof(invalid.receipt);
            break;
        case 5:
            invalid.authority_epoch = 0;
            break;
        case 6:
            invalid.next_presentation_sequence = 0;
            break;
        case 7:
            invalid.receipt.struct_size--;
            break;
        case 8:
            invalid.receipt.schema_version++;
            break;
        case 9:
            invalid.receipt.stream_epoch++;
            break;
        default:
            CHECK(false);
        }
        expect_status_rejected(invalid);
        CHECK(CL_CGameEventRuntimeRequiresResync());
        restore_after_quarantine(active_epoch, active_epoch + 1);
        ++active_epoch;
    }

    /* Structurally valid status must also agree with owner authority state. */
    expect_status_rejected(inactive_status());
    CHECK(CL_CGameEventRuntimeRequiresResync());
    restore_after_quarantine(active_epoch, active_epoch + 1);
    ++active_epoch;

    expect_status_rejected(active_status(active_epoch + 100, 12));
    CHECK(CL_CGameEventRuntimeRequiresResync());
    restore_after_quarantine(active_epoch, active_epoch + 1);
    ++active_epoch;

    expect_status_rejected(active_status(active_epoch, 11));
    CHECK(CL_CGameEventRuntimeRequiresResync());
    restore_after_quarantine(active_epoch, active_epoch + 1);
    ++active_epoch;

    /* A cgame-declared resync requirement becomes an owner hard barrier. */
    auto cgame_requires_resync = active_status(active_epoch, 12);
    cgame_requires_resync.state_flags |=
        WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC;
    expect_status_rejected(cgame_requires_resync);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    restore_after_quarantine(active_epoch, active_epoch + 1);
    ++active_epoch;

    /* OK is committed only after an exact post-reset status handshake. */
    fake.auto_status_on_reset = false;
    fake.returned_status = active_status(active_epoch + 100, 12);
    const auto status_calls_before_bad_reset = fake.status_call_count;
    CHECK(CL_CGameEventRuntimeResetAuthority(active_epoch + 1, 12) ==
          WORR_CGAME_EVENT_RUNTIME_DEGRADED);
    CHECK(fake.status_call_count == status_calls_before_bad_reset + 1);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    fake.auto_status_on_reset = true;
    restore_after_quarantine(active_epoch + 1, active_epoch + 2);
    active_epoch += 2;

    /* Attachment likewise verifies inactive state before accepting a table. */
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CHECK(CL_CGameEventRuntimeRequiresResync());
    fake.auto_status_on_reset = false;
    fake.returned_status = active_status(active_epoch, 12);
    CHECK(!CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    CHECK(CL_CGameEventRuntimeRequiresResync());
    fake.auto_status_on_reset = true;
    restore_after_quarantine(active_epoch, active_epoch + 1);
    ++active_epoch;

    /* Active cgame state is incoherent while the owner is disconnected. */
    CHECK(CL_CGameEventRuntimeResetAuthority(0, 0) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    fake.returned_status = active_status(9999, 12);
    expect_status_rejected(fake.returned_status);
    worr_event_record_v1 record{};
    CHECK(CL_CGameEventRuntimeSubmitAuthoritativeBatch(&record, 1) ==
          WORR_CGAME_EVENT_RUNTIME_UNINITIALIZED);

    fake.auto_status_on_reset = true;
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    CHECK(CL_CGameEventRuntimeResetAuthority(active_epoch + 1, 12) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    ++active_epoch;

    /* The same incoherence is rejected while awaiting reload resync. */
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CHECK(CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    fake.returned_status = active_status(active_epoch, 12);
    expect_status_rejected(fake.returned_status);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    restore_after_quarantine(active_epoch, active_epoch + 1);
}

void test_descriptor_quiesce_and_native_callbacks()
{
    CHECK(CL_CGameEventRuntimeResetConnection() ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    clear_fake();
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));

    worr_event_stream_descriptor_v1 descriptor{};
    CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 79, 4));
    const auto failed_first_reset = fake.reset_call_count;
    fake.reset_result = WORR_CGAME_EVENT_RUNTIME_CONFLICT;
    CHECK(CL_CGameEventRuntimeObserveDescriptor(&descriptor) ==
          WORR_CGAME_EVENT_RUNTIME_CONFLICT);
    expect_reset(failed_first_reset, 79, 4);
    expect_reset(failed_first_reset + 1, 0, 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    fake.reset_result = WORR_CGAME_EVENT_RUNTIME_OK;
    CHECK(CL_CGameEventRuntimeSetConsumer(&valid_consumer));
    expect_reset(fake.reset_call_count - 1, 0, 0);
    CHECK(CL_CGameEventRuntimeObserveDescriptor(&descriptor) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);

    CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 80, 5));
    const auto reset_before = fake.reset_call_count;
    CHECK(CL_CGameEventRuntimeObserveDescriptor(&descriptor) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(fake.reset_call_count == reset_before + 1);
    expect_reset(reset_before, 80, 5);

    CHECK(CL_CGameEventRuntimeQuiesceAuthority() ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    expect_reset(reset_before + 1, 0, 0);
    CHECK(CL_CGameEventRuntimeObserveDescriptor(&descriptor) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);

    CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 81, 9));
    CHECK(CL_CGameEventRuntimeObserveDescriptor(&descriptor) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(!CL_CGameEventRuntimeRequiresResync());

    worr_native_event_consumer_v1 callbacks{};
    CHECK(CL_CGameEventRuntimeGetNativeConsumerV1(&callbacks));
    CHECK(callbacks.struct_size == sizeof(callbacks));
    CHECK(callbacks.schema_version ==
          WORR_NATIVE_EVENT_ADMISSION_ABI_VERSION);
    CHECK(callbacks.reserved0 == 0 && callbacks.opaque != nullptr);
    CHECK(callbacks.ResetAuthority && callbacks.SubmitAuthoritativeBatch &&
          callbacks.GetStatus);
    CHECK(callbacks.ResetAuthority(
              callbacks.opaque, descriptor.stream_epoch,
              descriptor.first_sequence) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    worr_cgame_event_runtime_status_v1 status{};
    CHECK(callbacks.GetStatus(callbacks.opaque, &status));
    CHECK(status.authority_epoch == descriptor.stream_epoch);
    CHECK(callbacks.ResetAuthority(nullptr, 1, 1) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(!callbacks.GetStatus(nullptr, &status));

    const auto native_quiesce_reset = fake.reset_call_count;
    CHECK(callbacks.ResetAuthority(callbacks.opaque, 0, 0) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    expect_reset(native_quiesce_reset, 0, 0);
    CHECK(CL_CGameEventRuntimeRequiresResync());
    CHECK(CL_CGameEventRuntimeObserveDescriptor(&descriptor) ==
          WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH);
    CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 82, 10));
    CHECK(CL_CGameEventRuntimeObserveDescriptor(&descriptor) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(!CL_CGameEventRuntimeRequiresResync());

    CHECK(CL_CGameEventRuntimeResetConnection() ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(!CL_CGameEventRuntimeRequiresResync());
    CHECK(Worr_EventStreamDescriptorInitV1(&descriptor, 1, 1));
    descriptor.flags = WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2;
    CHECK(CL_CGameEventRuntimeObserveDescriptor(&descriptor) ==
          WORR_CGAME_EVENT_RUNTIME_OK);

    descriptor.flags |= UINT16_C(0x8000);
    CHECK(CL_CGameEventRuntimeObserveDescriptor(&descriptor) ==
          WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT);
    CHECK(CL_CGameEventRuntimeResetConnection() ==
          WORR_CGAME_EVENT_RUNTIME_OK);
}

} // namespace

int main()
{
    /* Normalize process-global owner state before exercising replay behavior. */
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CHECK(CL_CGameEventRuntimeResetAuthority(0, 0) ==
          WORR_CGAME_EVENT_RUNTIME_OK);

    test_reset_replay_detach_and_deactivation();
    test_descriptor_and_callback_validation();
    test_batch_forwarding_and_result_propagation();
    test_unknown_result_quarantine();
    test_status_copy_and_validation();
    test_descriptor_quiesce_and_native_callbacks();

    CHECK(CL_CGameEventRuntimeResetAuthority(0, 0) ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));

    std::puts("cgame event runtime owner tests passed");
    return 0;
}
