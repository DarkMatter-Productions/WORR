/* Deterministic hostile checks for the FR-10-T08/T09 live-observation ABI. */
#include "shared/local_action_observation.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,       \
                    __LINE__, #condition);                                  \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static worr_command_record_v1 make_command(void)
{
    worr_command_record_v1 command;
    memset(&command, 0, sizeof(command));
    command.struct_size = sizeof(command);
    command.schema_version = WORR_COMMAND_ABI_VERSION;
    command.command_id.epoch = 7;
    command.command_id.sequence = 11;
    command.sample_time_us = 123000;
    command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    command.command.struct_size = sizeof(command.command);
    command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.command.duration_ms = 16;
    command.render_watermark.struct_size = sizeof(command.render_watermark);
    command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    return command;
}

static worr_local_action_observation_state_v1 make_state(void)
{
    worr_local_action_observation_state_v1 state;
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    state.schema_version = WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION;
    state.flags = WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ALIVE |
                  WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ELIGIBLE;
    state.phase = WORR_LOCAL_ACTION_OBSERVATION_READY;
    state.inventory_hash = UINT64_C(0x5b3d9c7ea4120f11);
    state.active_weapon_id = 9;
    state.active_ammo_item_id = 31;
    state.active_ammo_units = 20;
    state.presentation_frame = 17;
    state.presentation_rate = 10;
    return state;
}

static int test_valid_record_and_differences(void)
{
    const worr_command_record_v1 command = make_command();
    const worr_local_action_observation_state_v1 before = make_state();
    worr_local_action_observation_state_v1 after = before;
    worr_local_action_observation_record_v1 record;

    CHECK(sizeof(before) == 64);
    CHECK(sizeof(record) == 256);
    after.flags |= WORR_LOCAL_ACTION_OBSERVATION_ATTACK_HELD |
                   WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_HELD |
                   WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_ACTIVE;
    after.phase = WORR_LOCAL_ACTION_OBSERVATION_FIRING;
    after.active_ammo_units = 19;
    after.presentation_frame = 18;
    after.think_remaining_ms = 100;
    after.inventory_hash ^= UINT64_C(0x9e3779b97f4a7c15);

    memset(&record, 0, sizeof(record));
    CHECK(Worr_LocalActionObservationBuildV1(2, &command, &before, &after,
                                              &record));
    CHECK(Worr_LocalActionObservationRecordValidateV1(&record));
    CHECK(record.difference_bits ==
          (WORR_LOCAL_ACTION_OBSERVATION_DIFF_FLAGS |
           WORR_LOCAL_ACTION_OBSERVATION_DIFF_PHASE |
           WORR_LOCAL_ACTION_OBSERVATION_DIFF_ACTIVE_AMMO |
           WORR_LOCAL_ACTION_OBSERVATION_DIFF_PRESENTATION_FRAME |
           WORR_LOCAL_ACTION_OBSERVATION_DIFF_TIMERS |
           WORR_LOCAL_ACTION_OBSERVATION_DIFF_INVENTORY));
    CHECK(record.semantic_hash != 0);
    return 0;
}

static int test_fail_closed_and_corruption(void)
{
    worr_command_record_v1 command = make_command();
    worr_local_action_observation_state_v1 before = make_state();
    worr_local_action_observation_state_v1 after = before;
    worr_local_action_observation_record_v1 record;
    worr_local_action_observation_record_v1 before_output;

    memset(&record, 0x91, sizeof(record));
    before_output = record;
    CHECK(!Worr_LocalActionObservationBuildV1(0, &command, &before, &after,
                                               &record));
    CHECK(memcmp(&record, &before_output, sizeof(record)) == 0);

    memset(&record, 0, sizeof(record));
    before.phase = 99;
    CHECK(!Worr_LocalActionObservationBuildV1(0, &command, &before, &after,
                                               &record));
    CHECK(record.struct_size == 0);

    before = make_state();
    command.command_id.sequence = 0;
    CHECK(!Worr_LocalActionObservationBuildV1(0, &command, &before, &after,
                                               &record));

    command = make_command();
    CHECK(Worr_LocalActionObservationBuildV1(0, &command, &before, &after,
                                              &record));
    record.state_after.presentation_rate = 1001;
    CHECK(!Worr_LocalActionObservationRecordValidateV1(&record));

    after = make_state();
    after.flags |= UINT32_C(1) << 31;
    memset(&record, 0, sizeof(record));
    CHECK(!Worr_LocalActionObservationBuildV1(0, &command, &before, &after,
                                               &record));
    return 0;
}

int main(void)
{
    int result;
    result = test_valid_record_and_differences();
    if (result)
        return result;
    return test_fail_closed_and_corruption();
}
