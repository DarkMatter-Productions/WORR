// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "cg_entity_local.h"
#include "cg_snapshot_timeline.hpp"

#include <cstring>

namespace {

constexpr std::size_t EVENT_LOOKUP_CAPACITY = CG_EVENT_JOURNAL_CAPACITY * 2;
constexpr std::uint32_t DEBUG_INTERVAL_MS = 1000;

static_assert((CG_SNAPSHOT_TIMELINE_CAPACITY & (CG_SNAPSHOT_TIMELINE_CAPACITY - 1)) == 0,
              "snapshot capacity must be a power of two");
static_assert((CG_EVENT_JOURNAL_CAPACITY & (CG_EVENT_JOURNAL_CAPACITY - 1)) == 0,
              "event capacity must be a power of two");
static_assert((EVENT_LOOKUP_CAPACITY & (EVENT_LOOKUP_CAPACITY - 1)) == 0,
              "event lookup capacity must be a power of two");

struct event_storage_t {
    cg_event_journal_entry_t entry;
    std::uint32_t serial;
};

struct event_lookup_t {
    std::uint32_t serial;
    std::uint16_t journal_index;
};

struct snapshot_timeline_state_t {
    cg_snapshot_metadata_t snapshots[CG_SNAPSHOT_TIMELINE_CAPACITY];
    std::size_t snapshot_write_index;
    std::size_t snapshot_count;

    event_storage_t events[CG_EVENT_JOURNAL_CAPACITY];
    event_lookup_t event_lookup[EVENT_LOOKUP_CAPACITY];
    std::size_t event_write_index;
    std::size_t event_count;
    std::uint32_t next_event_serial;

    cg_prediction_reconciliation_t prediction;

    std::uint32_t epoch;
    std::int32_t active_snapshot_id;
    std::uint32_t active_snapshot_receive_time_ms;
    bool has_active_snapshot;

    std::uint32_t accepted_events;
    std::uint32_t duplicate_events;
    std::uint32_t accepted_events_since_debug;
    std::uint32_t duplicate_events_since_debug;

    std::uint32_t last_net_debug_time_ms;
    std::uint32_t last_event_debug_time_ms;
    bool net_debug_started;
    bool event_debug_started;
};

snapshot_timeline_state_t timeline;
cvar_t *cg_net_debug;
cvar_t *cg_event_debug;

void advance_epoch()
{
    ++timeline.epoch;
    if (!timeline.epoch)
        ++timeline.epoch;
}

bool event_identity_equal(const cg_event_identity_t &a, const cg_event_identity_t &b)
{
    return a.epoch == b.epoch &&
           a.snapshot_id == b.snapshot_id &&
           a.entity_id == b.entity_id &&
           a.raw_event == b.raw_event;
}

std::uint32_t event_identity_hash(const cg_event_identity_t &identity)
{
    // A small integer mixer keeps the fixed lookup table inexpensive while the
    // full tuple remains in the journal for collision-safe equality checks.
    std::uint32_t hash = identity.epoch * 0x9e3779b9u;
    hash ^= static_cast<std::uint32_t>(identity.snapshot_id) + 0x85ebca6bu +
            (hash << 6) + (hash >> 2);
    hash ^= identity.entity_id + 0xc2b2ae35u + (hash << 6) + (hash >> 2);
    hash ^= identity.raw_event + 0x27d4eb2fu + (hash << 6) + (hash >> 2);
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    return hash;
}

bool lookup_slot_active(const event_lookup_t &slot)
{
    return slot.serial &&
           slot.journal_index < CG_EVENT_JOURNAL_CAPACITY &&
           timeline.events[slot.journal_index].serial == slot.serial;
}

bool reserve_event_identity(const cg_event_identity_t &identity)
{
    const std::size_t mask = EVENT_LOOKUP_CAPACITY - 1;
    std::size_t lookup_index = event_identity_hash(identity) & mask;
    std::size_t reusable_index = EVENT_LOOKUP_CAPACITY;

    for (std::size_t probe = 0; probe < EVENT_LOOKUP_CAPACITY; ++probe) {
        event_lookup_t &slot = timeline.event_lookup[lookup_index];

        if (!slot.serial) {
            if (reusable_index == EVENT_LOOKUP_CAPACITY)
                reusable_index = lookup_index;
            break;
        }

        if (!lookup_slot_active(slot)) {
            if (reusable_index == EVENT_LOOKUP_CAPACITY)
                reusable_index = lookup_index;
        } else if (event_identity_equal(
                       timeline.events[slot.journal_index].entry.identity, identity)) {
            return false;
        }

        lookup_index = (lookup_index + 1) & mask;
    }

    // At most half of the lookup slots can be active, so a reusable slot always
    // exists even after the journal has wrapped many times.
    if (reusable_index == EVENT_LOOKUP_CAPACITY)
        return false;

    ++timeline.next_event_serial;
    if (!timeline.next_event_serial) {
        // Rebuild every retained mapping before restarting the serial sequence.
        // This keeps duplicate suppression correct even across counter wrap.
        std::memset(timeline.event_lookup, 0, sizeof(timeline.event_lookup));
        timeline.next_event_serial = 1;

        for (std::size_t journal = 0; journal < CG_EVENT_JOURNAL_CAPACITY;
             ++journal) {
            const event_storage_t &retained = timeline.events[journal];
            if (!retained.serial)
                continue;

            std::size_t rebuilt_index =
                event_identity_hash(retained.entry.identity) & mask;
            while (timeline.event_lookup[rebuilt_index].serial)
                rebuilt_index = (rebuilt_index + 1) & mask;

            timeline.event_lookup[rebuilt_index].serial = retained.serial;
            timeline.event_lookup[rebuilt_index].journal_index =
                static_cast<std::uint16_t>(journal);
        }

        reusable_index = event_identity_hash(identity) & mask;
        while (timeline.event_lookup[reusable_index].serial)
            reusable_index = (reusable_index + 1) & mask;
    }

    const std::size_t journal_index = timeline.event_write_index;
    event_storage_t &storage = timeline.events[journal_index];
    storage.entry.identity = identity;
    storage.entry.receive_time_ms = timeline.active_snapshot_receive_time_ms;
    storage.serial = timeline.next_event_serial;

    timeline.event_lookup[reusable_index].serial = storage.serial;
    timeline.event_lookup[reusable_index].journal_index =
        static_cast<std::uint16_t>(journal_index);

    timeline.event_write_index =
        (timeline.event_write_index + 1) & (CG_EVENT_JOURNAL_CAPACITY - 1);
    if (timeline.event_count < CG_EVENT_JOURNAL_CAPACITY)
        ++timeline.event_count;

    return true;
}

cg_snapshot_continuity_t classify_snapshot(const cg_accepted_snapshot_t &snapshot)
{
    const cg_snapshot_metadata_t *previous =
        CG_SnapshotTimeline_SnapshotFromNewest(0);

    if (!previous)
        return cg_snapshot_continuity_t::initial;
    if (snapshot.snapshot_id == previous->snapshot_id)
        return cg_snapshot_continuity_t::duplicate;
    if (snapshot.snapshot_id < previous->snapshot_id)
        return cg_snapshot_continuity_t::rewind;
    if (static_cast<std::int64_t>(snapshot.snapshot_id) !=
        static_cast<std::int64_t>(previous->snapshot_id) + 1)
        return cg_snapshot_continuity_t::sequence_gap;
    if (snapshot.delta_baseline_id <= 0)
        return cg_snapshot_continuity_t::full_snapshot;
    if (snapshot.delta_baseline_id != previous->snapshot_id)
        return cg_snapshot_continuity_t::baseline_jump;
    return cg_snapshot_continuity_t::contiguous;
}

const char *continuity_name(cg_snapshot_continuity_t continuity)
{
    switch (continuity) {
    case cg_snapshot_continuity_t::initial:
        return "initial";
    case cg_snapshot_continuity_t::contiguous:
        return "contiguous";
    case cg_snapshot_continuity_t::sequence_gap:
        return "sequence_gap";
    case cg_snapshot_continuity_t::baseline_jump:
        return "baseline_jump";
    case cg_snapshot_continuity_t::full_snapshot:
        return "full_snapshot";
    case cg_snapshot_continuity_t::duplicate:
        return "duplicate";
    case cg_snapshot_continuity_t::rewind:
        return "rewind";
    }

    return "unknown";
}

const char *prediction_correction_reason_name(
    cg_prediction_correction_reason_t reason)
{
    switch (reason) {
    case cg_prediction_correction_reason_t::none:
        return "none";
    case cg_prediction_correction_reason_t::input_range_invalid:
        return "input_range_invalid";
    case cg_prediction_correction_reason_t::retained_state_missing:
        return "retained_state_missing";
    case cg_prediction_correction_reason_t::config_discontinuity:
        return "config_discontinuity";
    case cg_prediction_correction_reason_t::replay_rejected:
        return "replay_rejected";
    case cg_prediction_correction_reason_t::state_divergence:
        return "state_divergence";
    case cg_prediction_correction_reason_t::correction_threshold_exceeded:
        return "correction_threshold_exceeded";
    case cg_prediction_correction_reason_t::canonical_authority_unavailable:
        return "canonical_authority_unavailable";
    case cg_prediction_correction_reason_t::canonical_authority_mismatch:
        return "canonical_authority_mismatch";
    case cg_prediction_correction_reason_t::snapshot_discontinuity:
        return "snapshot_discontinuity";
    }
    return "unknown";
}

bool debug_interval_elapsed(std::uint32_t now, std::uint32_t then, bool started)
{
    return !started || now - then >= DEBUG_INTERVAL_MS;
}

} // namespace

void CG_SnapshotTimeline_InitCvars()
{
    if (!cgei)
        return;

    cg_net_debug = Cvar_Get("cg_net_debug", "0", 0);
    cg_event_debug = Cvar_Get("cg_event_debug", "0", 0);
}

void CG_SnapshotTimeline_Reset(std::uint32_t receive_time_ms)
{
    std::uint32_t next_epoch = timeline.epoch + 1u;
    if (!next_epoch)
        next_epoch = 1u;

    std::memset(&timeline, 0, sizeof(timeline));
    timeline.epoch = next_epoch;
    timeline.last_net_debug_time_ms = receive_time_ms;
    timeline.last_event_debug_time_ms = receive_time_ms;
}

void CG_SnapshotTimeline_RecordAccepted(const cg_accepted_snapshot_t &snapshot)
{
    if (!timeline.epoch)
        advance_epoch();

    const cg_snapshot_continuity_t continuity = classify_snapshot(snapshot);
    if (continuity == cg_snapshot_continuity_t::rewind)
        advance_epoch();

    const cg_snapshot_metadata_t metadata = {
        .epoch = timeline.epoch,
        .snapshot_id = snapshot.snapshot_id,
        .delta_baseline_id = snapshot.delta_baseline_id,
        .server_time_ms = snapshot.server_time_ms,
        .receive_time_ms = snapshot.receive_time_ms,
        .frame_flags = snapshot.frame_flags,
        .entity_count = snapshot.entity_count,
        .inferred_acked_input_cmd = snapshot.inferred_acked_input_cmd,
        .continuity = continuity,
    };

    timeline.snapshots[timeline.snapshot_write_index] = metadata;
    timeline.snapshot_write_index =
        (timeline.snapshot_write_index + 1) & (CG_SNAPSHOT_TIMELINE_CAPACITY - 1);
    if (timeline.snapshot_count < CG_SNAPSHOT_TIMELINE_CAPACITY)
        ++timeline.snapshot_count;

    timeline.active_snapshot_id = snapshot.snapshot_id;
    timeline.active_snapshot_receive_time_ms = snapshot.receive_time_ms;
    timeline.has_active_snapshot = true;
}

bool CG_SnapshotTimeline_ShouldDispatchEvent(std::uint32_t entity_id,
                                             std::uint32_t raw_event)
{
    // Preserving dispatch is safer than dropping an event if an embedding host
    // calls the legacy event path before supplying its first snapshot record.
    if (!timeline.has_active_snapshot || !raw_event)
        return true;

    const cg_event_identity_t identity = {
        .epoch = timeline.epoch,
        .snapshot_id = timeline.active_snapshot_id,
        .entity_id = entity_id,
        .raw_event = raw_event,
    };

    if (!reserve_event_identity(identity)) {
        ++timeline.duplicate_events;
        ++timeline.duplicate_events_since_debug;
        return false;
    }

    ++timeline.accepted_events;
    ++timeline.accepted_events_since_debug;
    return true;
}

void CG_SnapshotTimeline_NotePredictionReplay(
    std::uint32_t inferred_acked_input_cmd,
    std::uint32_t current_input_cmd,
    std::uint32_t replay_count)
{
    timeline.prediction.inferred_acked_input_cmd = inferred_acked_input_cmd;
    timeline.prediction.current_input_cmd = current_input_cmd;
    timeline.prediction.replay_count = replay_count;
}

void CG_SnapshotTimeline_NotePredictionCorrection(
    std::uint32_t inferred_acked_input_cmd,
    std::uint32_t current_input_cmd,
    float correction_magnitude,
    bool hard_reset,
    cg_prediction_correction_reason_t reason)
{
    timeline.prediction.inferred_acked_input_cmd = inferred_acked_input_cmd;
    timeline.prediction.current_input_cmd = current_input_cmd;
    timeline.prediction.correction_magnitude = correction_magnitude;
    ++timeline.prediction.correction_count;
    timeline.prediction.last_correction_reason = reason;
    timeline.prediction.last_correction_was_hard_reset = hard_reset;
    if (hard_reset)
        ++timeline.prediction.hard_reset_count;
}

void CG_SnapshotTimeline_DebugTick(std::uint32_t realtime_ms)
{
    if (cg_net_debug && cg_net_debug->integer) {
        if (timeline.snapshot_count &&
            debug_interval_elapsed(realtime_ms, timeline.last_net_debug_time_ms,
                                   timeline.net_debug_started)) {
            const cg_snapshot_metadata_t *snapshot =
                CG_SnapshotTimeline_SnapshotFromNewest(0);
            const cg_prediction_reconciliation_t &prediction = timeline.prediction;

            Com_LPrintf(PRINT_ALL,
                        "cg_net: epoch=%u snapshot=%d delta=%d continuity=%s "
                        "entities=%u flags=0x%x inferred_ack=%u current=%u "
                        "replay=%u "
                        "correction=%.3f reason=%s hard_resets=%u\n",
                        snapshot->epoch, snapshot->snapshot_id,
                        snapshot->delta_baseline_id,
                        continuity_name(snapshot->continuity), snapshot->entity_count,
                        snapshot->frame_flags,
                        prediction.inferred_acked_input_cmd,
                        prediction.current_input_cmd, prediction.replay_count,
                        prediction.correction_magnitude,
                        prediction_correction_reason_name(
                            prediction.last_correction_reason),
                        prediction.hard_reset_count);

            timeline.last_net_debug_time_ms = realtime_ms;
            timeline.net_debug_started = true;
        }
    } else {
        timeline.net_debug_started = false;
    }

    if (cg_event_debug && cg_event_debug->integer) {
        if ((timeline.accepted_events_since_debug ||
             timeline.duplicate_events_since_debug) &&
            debug_interval_elapsed(realtime_ms, timeline.last_event_debug_time_ms,
                                   timeline.event_debug_started)) {
            const cg_event_journal_entry_t *event =
                CG_SnapshotTimeline_EventFromNewest(0);

            if (event) {
                Com_LPrintf(PRINT_ALL,
                            "cg_event: accepted=%u duplicates=%u total=%u/%u "
                            "last=%u:%d:%u:%u journal=%zu/%zu\n",
                            timeline.accepted_events_since_debug,
                            timeline.duplicate_events_since_debug,
                            timeline.accepted_events, timeline.duplicate_events,
                            event->identity.epoch, event->identity.snapshot_id,
                            event->identity.entity_id, event->identity.raw_event,
                            timeline.event_count, CG_EVENT_JOURNAL_CAPACITY);
            } else {
                Com_LPrintf(PRINT_ALL,
                            "cg_event: accepted=%u duplicates=%u total=%u/%u "
                            "journal=0/%zu\n",
                            timeline.accepted_events_since_debug,
                            timeline.duplicate_events_since_debug,
                            timeline.accepted_events, timeline.duplicate_events,
                            CG_EVENT_JOURNAL_CAPACITY);
            }

            timeline.accepted_events_since_debug = 0;
            timeline.duplicate_events_since_debug = 0;
            timeline.last_event_debug_time_ms = realtime_ms;
            timeline.event_debug_started = true;
        }
    } else {
        timeline.event_debug_started = false;
    }
}

std::size_t CG_SnapshotTimeline_SnapshotCount()
{
    return timeline.snapshot_count;
}

const cg_snapshot_metadata_t *CG_SnapshotTimeline_SnapshotFromNewest(std::size_t age)
{
    if (age >= timeline.snapshot_count)
        return nullptr;

    const std::size_t index =
        (timeline.snapshot_write_index + CG_SNAPSHOT_TIMELINE_CAPACITY - 1 - age) &
        (CG_SNAPSHOT_TIMELINE_CAPACITY - 1);
    return &timeline.snapshots[index];
}

std::size_t CG_SnapshotTimeline_EventCount()
{
    return timeline.event_count;
}

const cg_event_journal_entry_t *CG_SnapshotTimeline_EventFromNewest(std::size_t age)
{
    if (age >= timeline.event_count)
        return nullptr;

    const std::size_t index =
        (timeline.event_write_index + CG_EVENT_JOURNAL_CAPACITY - 1 - age) &
        (CG_EVENT_JOURNAL_CAPACITY - 1);
    return &timeline.events[index].entry;
}

const cg_prediction_reconciliation_t &CG_SnapshotTimeline_PredictionTelemetry()
{
    return timeline.prediction;
}
