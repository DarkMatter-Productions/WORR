/* Deterministic FR-10-T16 adaptive native-input delivery policy tests. */

#include "common/net/native_input_delivery.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__,      \
                    __LINE__, #condition);                                 \
            return false;                                                  \
        }                                                                  \
    } while (0)

static worr_native_input_delivery_feedback_v1 make_feedback(
    uint32_t epoch, uint32_t received, uint32_t consumed, uint64_t time_ms)
{
    worr_native_input_delivery_feedback_v1 feedback;
    memset(&feedback, 0, sizeof(feedback));
    feedback.struct_size = sizeof(feedback);
    feedback.schema_version = WORR_NATIVE_INPUT_DELIVERY_VERSION;
    feedback.received_cursor.epoch = epoch;
    feedback.received_cursor.contiguous_sequence = received;
    feedback.consumed_cursor.epoch = epoch;
    feedback.consumed_cursor.contiguous_sequence = consumed;
    feedback.observed_at_ms = time_ms;
    return feedback;
}

static worr_adaptive_input_output_v1 make_adaptive(uint64_t time_ms,
                                                   uint32_t redundancy)
{
    worr_adaptive_input_output_v1 output;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_ADAPTIVE_INPUT_VERSION;
    output.result = WORR_ADAPTIVE_INPUT_OK;
    output.evaluated_at_ms = time_ms;
    output.packets_per_second = 60;
    output.redundancy_frames = redundancy;
    output.send_interval_ms = 16;
    output.flags = WORR_ADAPTIVE_INPUT_OUTPUT_VALID |
                   WORR_ADAPTIVE_INPUT_OUTPUT_REDUNDANCY_AVAILABLE;
    return output;
}

static void make_candidates(
    worr_native_input_delivery_candidate_v1 *candidates, size_t count,
    uint32_t epoch, uint32_t first_sequence)
{
    size_t index;
    memset(candidates, 0, count * sizeof(*candidates));
    for (index = 0; index < count; ++index) {
        candidates[index].command_id.epoch = epoch;
        candidates[index].command_id.sequence =
            first_sequence + (uint32_t)index;
        candidates[index].sample_time_us =
            UINT64_C(16000) * (uint64_t)(first_sequence + index);
    }
}

static bool plan_is_sentinel(
    const worr_native_input_delivery_plan_v1 *plan)
{
    const unsigned char *bytes = (const unsigned char *)(const void *)plan;
    size_t index;
    for (index = 0; index < sizeof(*plan); ++index) {
        if (bytes[index] != 0xa5)
            return false;
    }
    return true;
}

static bool test_defaults_and_reset(void)
{
    worr_native_input_delivery_config_v1 config;
    worr_native_input_delivery_state_v1 state;
    worr_native_input_delivery_state_v1 before;

    memset(&config, 0xa5, sizeof(config));
    Worr_NativeInputDeliveryDefaultConfigV1(&config);
    CHECK(config.struct_size == sizeof(config));
    CHECK(config.schema_version == WORR_NATIVE_INPUT_DELIVERY_VERSION);
    CHECK(config.maximum_batch_commands == 8);
    CHECK(config.maximum_redundant_commands == 3);
    CHECK(config.datagram_budget_bytes == 1200);
    CHECK(config.batch_overhead_bytes == 32);
    CHECK(config.per_command_overhead_bytes == 64);
    CHECK(config.retry_interval_ms == 100);
    CHECK(config.maximum_transmissions_per_command == 8);
    CHECK(config.batch_overhead_bytes +
              2u * (WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES +
                    config.per_command_overhead_bytes) <=
          config.datagram_budget_bytes);

    memset(&state, 0xa5, sizeof(state));
    before = state;
    CHECK(!Worr_NativeInputDeliveryResetV1(&state, 0));
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(Worr_NativeInputDeliveryResetV1(&state, 7));
    CHECK(Worr_NativeInputDeliveryValidateV1(&state));
    CHECK(state.command_epoch == 7 && state.decision_serial == 0);
    state.reserved[1] = 1;
    CHECK(!Worr_NativeInputDeliveryValidateV1(&state));
    return true;
}

static bool test_clean_batch_and_freshness_reservation(void)
{
    worr_native_input_delivery_config_v1 config;
    worr_native_input_delivery_state_v1 state;
    worr_native_input_delivery_feedback_v1 feedback =
        make_feedback(7, 0, 0, 1000);
    worr_adaptive_input_output_v1 adaptive = make_adaptive(1000, 1);
    worr_native_input_delivery_candidate_v1 candidates[5];
    worr_native_input_delivery_plan_v1 plan;
    uint32_t result;

    Worr_NativeInputDeliveryDefaultConfigV1(&config);
    config.maximum_batch_commands = 3;
    config.maximum_redundant_commands = 2;
    CHECK(Worr_NativeInputDeliveryResetV1(&state, 7));
    make_candidates(candidates, 5, 7, 1);
    memset(&plan, 0xa5, sizeof(plan));

    result = Worr_NativeInputDeliveryPlanV1(
        &state, &config, &feedback, &adaptive, candidates, 5, 1000,
        &plan);
    CHECK(result == WORR_NATIVE_INPUT_DELIVERY_PLANNED);
    CHECK(plan.result == result && plan.decision_serial == 1);
    CHECK(plan.selection_count == 3 && plan.fresh_count == 3 &&
          plan.redundant_count == 0 && plan.deferred_count == 2);
    CHECK(plan.selections[0].command_id.sequence == 1 &&
          plan.selections[1].command_id.sequence == 2 &&
          plan.selections[2].command_id.sequence == 5);
    CHECK((plan.selections[0].flags &
           WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRONTIER) != 0);
    CHECK((plan.selections[2].flags &
           WORR_NATIVE_INPUT_DELIVERY_SELECTION_NEWEST_FRESH) != 0);
    CHECK((plan.flags &
           (WORR_NATIVE_INPUT_DELIVERY_PLAN_HAS_FRESH |
            WORR_NATIVE_INPUT_DELIVERY_PLAN_FRONTIER_INCLUDED |
            WORR_NATIVE_INPUT_DELIVERY_PLAN_NEWEST_FRESH_INCLUDED |
            WORR_NATIVE_INPUT_DELIVERY_PLAN_BATCH_CAPPED)) ==
          (WORR_NATIVE_INPUT_DELIVERY_PLAN_HAS_FRESH |
           WORR_NATIVE_INPUT_DELIVERY_PLAN_FRONTIER_INCLUDED |
           WORR_NATIVE_INPUT_DELIVERY_PLAN_NEWEST_FRESH_INCLUDED |
           WORR_NATIVE_INPUT_DELIVERY_PLAN_BATCH_CAPPED));
    CHECK(plan.wire_bytes ==
          config.batch_overhead_bytes +
              3u * (WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES +
                    config.per_command_overhead_bytes));
    CHECK(state.telemetry.plans == 1 &&
          state.telemetry.selected_fresh == 3 &&
          state.telemetry.selected_redundant == 0 &&
          state.telemetry.deferred == 2);
    return true;
}

static bool test_selective_recovery_and_exact_feedback(void)
{
    worr_native_input_delivery_config_v1 config;
    worr_native_input_delivery_state_v1 state;
    worr_native_input_delivery_feedback_v1 feedback =
        make_feedback(9, 10, 8, 1000);
    worr_adaptive_input_output_v1 adaptive = make_adaptive(1000, 1);
    worr_native_input_delivery_candidate_v1 candidates[5];
    worr_native_input_delivery_plan_v1 plan;

    Worr_NativeInputDeliveryDefaultConfigV1(&config);
    config.maximum_batch_commands = 4;
    CHECK(Worr_NativeInputDeliveryResetV1(&state, 9));
    make_candidates(candidates, 5, 9, 11);
    candidates[0].transmit_count = 1;
    candidates[0].last_transmit_time_ms = 800; /* due frontier */
    candidates[1].transmit_count = 1;
    candidates[1].last_transmit_time_ms = 950; /* not due */
    candidates[2].transmit_count = config.maximum_transmissions_per_command;
    candidates[2].last_transmit_time_ms = 700; /* exhausted */

    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 5, 1000,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_PLANNED);
    CHECK(plan.selection_count == 3 && plan.fresh_count == 2 &&
          plan.redundant_count == 1);
    CHECK(plan.selections[0].command_id.sequence == 11 &&
          plan.selections[1].command_id.sequence == 14 &&
          plan.selections[2].command_id.sequence == 15);
    CHECK(plan.selections[0].attempt_ordinal == 2);
    CHECK((plan.selections[0].flags &
           (WORR_NATIVE_INPUT_DELIVERY_SELECTION_REDUNDANT |
            WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRONTIER)) ==
          (WORR_NATIVE_INPUT_DELIVERY_SELECTION_REDUNDANT |
           WORR_NATIVE_INPUT_DELIVERY_SELECTION_FRONTIER));
    CHECK((plan.flags & WORR_NATIVE_INPUT_DELIVERY_PLAN_RETRY_NOT_DUE) != 0);
    CHECK((plan.flags & WORR_NATIVE_INPUT_DELIVERY_PLAN_RETRY_EXHAUSTED) != 0);
    CHECK(plan.retry_exhausted_count == 1);
    CHECK(plan.received_cursor.contiguous_sequence == 10 &&
          plan.consumed_cursor.contiguous_sequence == 8);
    CHECK(state.last_received_sequence == 10 &&
          state.last_consumed_sequence == 8 &&
          state.last_feedback_time_ms == 1000);

    /* Exact feedback retires the prior frontier; the next accepted range must
     * begin at sequence 12 and cannot redundantly select acknowledged 11. */
    feedback = make_feedback(9, 11, 10, 1100);
    adaptive = make_adaptive(1100, 1);
    make_candidates(candidates, 4, 9, 12);
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 4, 1100,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_PLANNED);
    CHECK(plan.selection_count == 4 &&
          plan.selections[0].command_id.sequence == 12 &&
          plan.selections[3].command_id.sequence == 15);
    CHECK(state.last_received_sequence == 11 &&
          state.last_consumed_sequence == 10 &&
          state.telemetry.feedback_advances == 2);
    return true;
}

static bool test_nothing_due_and_adaptive_depth(void)
{
    worr_native_input_delivery_config_v1 config;
    worr_native_input_delivery_state_v1 state;
    worr_native_input_delivery_feedback_v1 feedback =
        make_feedback(4, 20, 20, 500);
    worr_adaptive_input_output_v1 adaptive = make_adaptive(500, 0);
    worr_native_input_delivery_candidate_v1 candidates[3];
    worr_native_input_delivery_plan_v1 plan;

    Worr_NativeInputDeliveryDefaultConfigV1(&config);
    CHECK(Worr_NativeInputDeliveryResetV1(&state, 4));
    make_candidates(candidates, 3, 4, 21);
    candidates[0].transmit_count = 1;
    candidates[0].last_transmit_time_ms = 100;
    candidates[1].transmit_count = 1;
    candidates[1].last_transmit_time_ms = 100;
    candidates[2].transmit_count = 1;
    candidates[2].last_transmit_time_ms = 100;

    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 3, 500,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_NOTHING_DUE);
    CHECK(plan.result == WORR_NATIVE_INPUT_DELIVERY_NOTHING_DUE);
    CHECK(plan.selection_count == 0 && plan.wire_bytes == 0 &&
          plan.deferred_count == 3);
    CHECK((plan.flags & WORR_NATIVE_INPUT_DELIVERY_PLAN_VALID) != 0);
    CHECK((plan.flags &
           WORR_NATIVE_INPUT_DELIVERY_PLAN_REDUNDANCY_CAPPED) != 0);
    CHECK(state.telemetry.nothing_due == 1);

    feedback.observed_at_ms = 600;
    adaptive = make_adaptive(600, 2);
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 3, 600,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_PLANNED);
    CHECK(plan.selection_count == 2 && plan.fresh_count == 0 &&
          plan.redundant_count == 2);
    CHECK(plan.selections[0].command_id.sequence == 21 &&
          plan.selections[1].command_id.sequence == 22);
    CHECK(plan.adaptive_redundancy_limit == 2);
    return true;
}

static bool test_wire_budget_and_retry_boundary(void)
{
    worr_native_input_delivery_config_v1 config;
    worr_native_input_delivery_state_v1 state;
    worr_native_input_delivery_feedback_v1 feedback =
        make_feedback(5, 0, 0, 1000);
    worr_adaptive_input_output_v1 adaptive = make_adaptive(1000, 3);
    worr_native_input_delivery_candidate_v1 candidates[4];
    worr_native_input_delivery_plan_v1 plan;
    const uint32_t command_bytes =
        WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES + 4u;

    Worr_NativeInputDeliveryDefaultConfigV1(&config);
    config.per_command_overhead_bytes = 4;
    config.datagram_budget_bytes =
        config.batch_overhead_bytes + 2u * command_bytes;
    CHECK(Worr_NativeInputDeliveryResetV1(&state, 5));
    make_candidates(candidates, 4, 5, 1);
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 4, 1000,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_PLANNED);
    CHECK(plan.selection_count == 2);
    CHECK(plan.selections[0].command_id.sequence == 1 &&
          plan.selections[1].command_id.sequence == 4);
    CHECK(plan.wire_bytes == config.datagram_budget_bytes);
    CHECK((plan.flags &
           WORR_NATIVE_INPUT_DELIVERY_PLAN_WIRE_BUDGET_CAPPED) != 0);

    /* Exactly the retry interval is due. */
    feedback = make_feedback(5, 0, 0, 1100);
    adaptive = make_adaptive(1100, 1);
    candidates[0].transmit_count = 1;
    candidates[0].last_transmit_time_ms = 1000;
    candidates[1].transmit_count = 1;
    candidates[1].last_transmit_time_ms = 1001;
    candidates[2].transmit_count = 1;
    candidates[2].last_transmit_time_ms = 1001;
    candidates[3].transmit_count = 1;
    candidates[3].last_transmit_time_ms = 1001;
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 4, 1100,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_PLANNED);
    CHECK(plan.selection_count == 1 && plan.redundant_count == 1);
    CHECK(plan.selections[0].command_id.sequence == 1);

    config.datagram_budget_bytes -= 1;
    memset(&plan, 0xa5, sizeof(plan));
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 4, 1100,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_INVALID_CONFIG);
    CHECK(plan_is_sentinel(&plan));
    return true;
}

static bool test_transactional_rejections(void)
{
    worr_native_input_delivery_config_v1 config;
    worr_native_input_delivery_state_v1 state;
    worr_native_input_delivery_feedback_v1 feedback =
        make_feedback(12, 5, 4, 100);
    worr_adaptive_input_output_v1 adaptive = make_adaptive(100, 1);
    worr_native_input_delivery_candidate_v1 candidates[2];
    worr_native_input_delivery_plan_v1 plan;
    uint64_t serial;

    Worr_NativeInputDeliveryDefaultConfigV1(&config);
    CHECK(Worr_NativeInputDeliveryResetV1(&state, 12));
    make_candidates(candidates, 2, 12, 6);
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 2, 100,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_PLANNED);
    serial = state.decision_serial;

    feedback.received_cursor.contiguous_sequence = 4;
    feedback.consumed_cursor.contiguous_sequence = 4;
    feedback.observed_at_ms = 101;
    make_candidates(candidates, 2, 12, 5);
    memset(&plan, 0xa5, sizeof(plan));
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 2, 101,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_FEEDBACK_ROLLBACK);
    CHECK(plan_is_sentinel(&plan) && state.decision_serial == serial);
    CHECK(state.last_received_sequence == 5 &&
          state.telemetry.feedback_rollbacks == 1);

    feedback = make_feedback(12, 5, 4, 102);
    adaptive = make_adaptive(102, 1);
    make_candidates(candidates, 2, 12, 7); /* gap at sequence 6 */
    memset(&plan, 0xa5, sizeof(plan));
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 2, 102,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_INVALID_CANDIDATES);
    CHECK(plan_is_sentinel(&plan) && state.decision_serial == serial);
    CHECK(state.telemetry.invalid_candidates == 1);

    make_candidates(candidates, 2, 12, 6);
    adaptive.flags = 0;
    memset(&plan, 0xa5, sizeof(plan));
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 2, 102,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_INVALID_ADAPTIVE_OUTPUT);
    CHECK(plan_is_sentinel(&plan) && state.decision_serial == serial);

    adaptive = make_adaptive(102, 1);
    feedback.consumed_cursor.contiguous_sequence = 6;
    memset(&plan, 0xa5, sizeof(plan));
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates, 2, 102,
              &plan) == WORR_NATIVE_INPUT_DELIVERY_INVALID_FEEDBACK);
    CHECK(plan_is_sentinel(&plan) && state.decision_serial == serial);
    return true;
}

static bool test_output_overlap(void)
{
    union {
        worr_native_input_delivery_plan_v1 plan;
        worr_native_input_delivery_candidate_v1 candidates[2];
    } shared;
    worr_native_input_delivery_config_v1 config;
    worr_native_input_delivery_state_v1 state;
    worr_native_input_delivery_feedback_v1 feedback =
        make_feedback(3, 0, 0, 10);
    worr_adaptive_input_output_v1 adaptive = make_adaptive(10, 1);

    Worr_NativeInputDeliveryDefaultConfigV1(&config);
    CHECK(Worr_NativeInputDeliveryResetV1(&state, 3));
    make_candidates(shared.candidates, 2, 3, 1);
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, shared.candidates, 2,
              10, &shared.plan) ==
          WORR_NATIVE_INPUT_DELIVERY_OUTPUT_OVERLAP);
    CHECK(state.decision_serial == 0 && state.telemetry.output_overlaps == 1);
    return true;
}

static bool test_maximum_range_and_sequence_ceiling(void)
{
    worr_native_input_delivery_config_v1 config;
    worr_native_input_delivery_state_v1 state;
    worr_native_input_delivery_feedback_v1 feedback =
        make_feedback(22, 0, 0, 1);
    worr_adaptive_input_output_v1 adaptive = make_adaptive(1, 3);
    worr_native_input_delivery_candidate_v1
        candidates[WORR_NATIVE_INPUT_DELIVERY_MAX_CANDIDATES + 1u];
    worr_native_input_delivery_plan_v1 first;
    worr_native_input_delivery_plan_v1 second;

    Worr_NativeInputDeliveryDefaultConfigV1(&config);
    /* Exercise the public eight-selection ceiling with an explicitly
     * amortized future-batch reservation; defaults conservatively fit six. */
    config.batch_overhead_bytes = 96;
    config.per_command_overhead_bytes = 4;
    CHECK(Worr_NativeInputDeliveryResetV1(&state, 22));
    make_candidates(candidates, WORR_NATIVE_INPUT_DELIVERY_MAX_CANDIDATES,
                    22, 1);
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates,
              WORR_NATIVE_INPUT_DELIVERY_MAX_CANDIDATES, 1, &first) ==
          WORR_NATIVE_INPUT_DELIVERY_PLANNED);
    CHECK(first.selection_count == 8 &&
          first.selections[0].command_id.sequence == 1 &&
          first.selections[6].command_id.sequence == 7 &&
          first.selections[7].command_id.sequence == 64);

    /* Repeating from an independently reset state is byte-deterministic. */
    CHECK(Worr_NativeInputDeliveryResetV1(&state, 22));
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates,
              WORR_NATIVE_INPUT_DELIVERY_MAX_CANDIDATES, 1, &second) ==
          WORR_NATIVE_INPUT_DELIVERY_PLANNED);
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);

    make_candidates(candidates,
                    WORR_NATIVE_INPUT_DELIVERY_MAX_CANDIDATES + 1u, 22,
                    1);
    memset(&second, 0xa5, sizeof(second));
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, candidates,
              WORR_NATIVE_INPUT_DELIVERY_MAX_CANDIDATES + 1u, 1,
              &second) == WORR_NATIVE_INPUT_DELIVERY_INVALID_CANDIDATES);
    CHECK(plan_is_sentinel(&second));

    feedback = make_feedback(22, UINT32_MAX, UINT32_MAX, 2);
    adaptive = make_adaptive(2, 3);
    CHECK(Worr_NativeInputDeliveryPlanV1(
              &state, &config, &feedback, &adaptive, NULL, 0, 2,
              &second) == WORR_NATIVE_INPUT_DELIVERY_NOTHING_DUE);
    CHECK(second.selection_count == 0);
    return true;
}

int main(void)
{
    if (!test_defaults_and_reset() ||
        !test_clean_batch_and_freshness_reservation() ||
        !test_selective_recovery_and_exact_feedback() ||
        !test_nothing_due_and_adaptive_depth() ||
        !test_wire_budget_and_retry_boundary() ||
        !test_transactional_rejections() || !test_output_overlap() ||
        !test_maximum_range_and_sequence_ceiling()) {
        return 1;
    }
    puts("native input delivery policy tests passed");
    return 0;
}
