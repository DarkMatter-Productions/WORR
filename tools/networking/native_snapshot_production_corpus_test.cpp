/*
 * Deterministic, headless production-path canonical snapshot corpus.
 *
 * The small fixture and engine stubs live in the focused production
 * virtual-link test.  Including that translation unit keeps this corpus on
 * the exact same production sender/codec/carrier/receiver/admission/cgame
 * seams without growing a second mock networking stack.  Its main is renamed
 * and deliberately not called.
 */
#define main worr_native_snapshot_production_virtual_link_focused_main
#include "native_snapshot_production_virtual_link_test.cpp"
#undef main

#include "common/net/impair.h"
#include "common/net/prediction_input.h"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <climits>
#include <string_view>

namespace {

constexpr std::uint64_t kDefaultSnapshots = UINT64_C(100000);
constexpr std::uint64_t kDefaultSeed = UINT64_C(0x51a7c0de);
constexpr std::uint64_t kMaximumSnapshots = UINT64_C(1000000);
constexpr std::uint32_t kCorpusEpochLimit = 4;
constexpr std::uint32_t kCorpusEntityCount = 8;
constexpr std::uint32_t kCorpusFirstEntity = 2;
constexpr std::uint32_t kCorpusEntityLimit = 64;
constexpr std::uint32_t kCorpusFirstWireFrame = 1;
constexpr std::uint32_t kCorpusOfficialEpochBase = 19001;
constexpr std::uint32_t kCorpusSnapshotEpochBase = 29001;
constexpr std::uint32_t kCorpusCommandEpochBase = 39001;
constexpr std::uint32_t kCorpusStartTime = 100000;
constexpr std::uint64_t kCorpusTickUs = UINT64_C(16000);
constexpr std::uint64_t kFnvOffset = UINT64_C(1469598103934665603);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

static_assert(kCorpusEntityCount <= kEntityCount);
static_assert(kCorpusFirstEntity + kCorpusEntityCount <
              kCorpusEntityLimit);
static_assert(kCorpusEntityLimit <= kProjectionEntityCapacity);

struct corpus_options_t {
    std::uint64_t snapshots{kDefaultSnapshots};
    std::uint64_t seed{kDefaultSeed};
};

struct corpus_metrics_t {
    std::uint64_t requested_frames{};
    std::uint64_t serialized_frames{};
    std::uint64_t accepted_frames{};
    std::uint64_t acknowledged_frames{};
    std::uint64_t released_frames{};
    std::uint64_t prediction_authorities{};
    std::uint64_t resolved_prediction_commands{};
    std::uint64_t prediction_pending_ranges{};
    std::uint64_t prediction_nonpending_ranges{};
    std::uint64_t prediction_max_replay{};
    std::uint64_t prediction_bootstrap_ranges{};
    std::uint64_t prediction_nonzero_cursor_ranges{};
    std::uint64_t prediction_limit_ranges{};
    std::uint64_t prediction_range_exhaustion_rejections{};
    std::uint64_t negative_probe_frames{};
    std::uint64_t corrupt_ack_probe_acceptances{};
    std::uint64_t corrupt_ack_probe_prediction_authorities{};
    std::uint64_t authority_history_resets{};
    std::uint64_t epochs{};
    std::uint64_t connection_activations{};
    std::uint64_t sequence_gaps{};
    std::uint64_t fragment_stalls{};
    std::uint64_t rate_suppressions{};
    std::uint64_t late_expectations{};
    std::uint64_t server_transmissions{};
    std::uint64_t server_packets{};
    std::uint64_t server_packet_bytes{};
    std::uint64_t server_to_client_deliveries{};
    std::uint64_t server_to_client_delivery_bytes{};
    std::uint64_t client_to_server_deliveries{};
    std::uint64_t client_to_server_delivery_bytes{};
    std::uint64_t server_to_client_losses{};
    std::uint64_t server_to_client_impairment_decisions{};
    std::uint64_t client_to_server_impairment_decisions{};
    std::uint64_t burst_packet_losses{};
    std::uint64_t server_to_client_fragment_release_inversions{};
    std::uint64_t duplicate_deliveries{};
    std::uint64_t acknowledgement_losses{};
    std::uint64_t upstream_ack_stalls{};
    std::uint64_t repeat_revalidations{};
    std::uint64_t corrupt_server_to_client{};
    std::uint64_t corrupt_client_to_server{};
    std::uint64_t corrupt_rejections{};
    std::uint64_t exact_once_checks{};
    std::uint64_t premature_ack_checks{};
    std::uint64_t premature_release_checks{};
    std::uint64_t retained_until_epoch_reset{};
};

struct corpus_digest_t {
    std::uint64_t value{kFnvOffset};

    void bytes(const void *data, std::size_t count)
    {
        const auto *input =
            static_cast<const std::uint8_t *>(data);
        for (std::size_t index = 0; index < count; ++index) {
            value ^= input[index];
            value *= kFnvPrime;
        }
    }

    void u64(std::uint64_t input)
    {
        std::array<std::uint8_t, 8> wire{};
        for (std::uint32_t index = 0; index < wire.size(); ++index)
            wire[index] =
                static_cast<std::uint8_t>(input >> (index * 8u));
        bytes(wire.data(), wire.size());
    }

    void tagged_packet(std::uint64_t tag,
                       const wire_packet_t &packet)
    {
        u64(tag);
        u64(packet.count);
        bytes(packet.bytes.data(), packet.count);
    }
};

struct corpus_epoch_state_t {
    std::array<entity_state_t, kCorpusEntityCount> entities{};
    std::uint32_t command_epoch{};
    std::uint32_t previous_wire{};
    std::uint32_t next_wire{kCorpusFirstWireFrame};
    bool initialized{};
};

struct corpus_frame_t {
    frame_carrier_t carrier{};
    std::uint32_t tick_delta{};
};

struct frame_decision_t {
    bool late_expectation{};
    bool sequence_gap{};
    bool fragment_stall{};
    bool rate_suppressed{};
};

struct corpus_impairment_state_t {
    net_impair_model_t server_to_client{};
    net_impair_model_t client_to_server{};
    net_impair_model_t corrupt_server_to_client{};
    net_impair_model_t corrupt_client_to_server{};
};

corpus_metrics_t corpus_metrics{};
corpus_digest_t corpus_digest{};
corpus_impairment_state_t corpus_impairment{};
std::uint64_t corpus_current_frame{};

[[noreturn]] void corpus_fail(const char *expression, int line)
{
    std::fprintf(
        stderr,
        "native_snapshot_production_corpus_test:%d: frame=%llu %s\n",
        line,
        static_cast<unsigned long long>(corpus_current_frame),
        expression);
    std::exit(EXIT_FAILURE);
}

#undef CHECK
#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            corpus_fail(#expression, __LINE__);                               \
    } while (0)

std::uint64_t mix64(std::uint64_t value)
{
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30u)) *
            UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27u)) *
            UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31u);
}

frame_decision_t frame_decision(std::uint64_t seed,
                                std::uint64_t frame_index,
                                bool first_in_epoch)
{
    const std::uint64_t value =
        mix64(seed ^ (frame_index *
                      UINT64_C(0xd6e8feb86659fd93)));
    frame_decision_t decision{};
    decision.late_expectation =
        ((value >> 32u) % 37u) == 0;
    decision.sequence_gap =
        !first_in_epoch && ((value >> 40u) % 8191u) == 0;
    decision.fragment_stall =
        ((value >> 48u) % 4093u) == 0;
    decision.rate_suppressed =
        !first_in_epoch && ((value >> 56u) % 997u) == 0;
    return decision;
}

void initialize_corpus_impairment(std::uint64_t seed)
{
    net_impair_config_t config{};
    config.seed = static_cast<std::uint32_t>(seed);
    config.latency_ms = 50;
    config.jitter_ms = 25;
    config.upstream_stall_ms = 100;
    config.loss_basis_points = 500;
    config.burst_start_basis_points = 200;
    config.reorder_basis_points = 500;
    config.duplicate_basis_points = 500;
    config.burst_length = 5;

    net_impair_config_t server_to_client = config;
    server_to_client.seed ^=
        UINT32_C(0x9e3779b9) * 1u;
    NetImpair_Init(
        &corpus_impairment.server_to_client,
        &server_to_client);

    net_impair_config_t client_to_server = config;
    client_to_server.seed ^=
        UINT32_C(0x9e3779b9) * 2u;
    NetImpair_Init(
        &corpus_impairment.client_to_server,
        &client_to_server);

    net_impair_config_t corrupt_config{};
    corrupt_config.seed =
        static_cast<std::uint32_t>(seed) ^
        UINT32_C(0xc0117e57);
    corrupt_config.corrupt_basis_points =
        NET_IMPAIR_PERCENT_SCALE;
    NetImpair_Init(
        &corpus_impairment.corrupt_server_to_client,
        &corrupt_config);
    corrupt_config.seed ^= UINT32_C(0x9e3779b9);
    NetImpair_Init(
        &corpus_impairment.corrupt_client_to_server,
        &corrupt_config);
}

bool parse_u64(const char *text, std::uint64_t *value_out)
{
    if (!text || !*text || !value_out || text[0] == '-')
        return false;
    errno = 0;
    char *end = nullptr;
    const unsigned long long value =
        std::strtoull(text, &end, 0);
    if (errno == ERANGE || end == text || *end != '\0')
        return false;
    *value_out = static_cast<std::uint64_t>(value);
    return true;
}

corpus_options_t parse_options(int argc, char **argv)
{
    corpus_options_t options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        std::uint64_t value = 0;
        if (argument == "--snapshots") {
            CHECK(index + 1 < argc);
            CHECK(parse_u64(argv[++index], &value));
            CHECK(value != 0 && value <= kMaximumSnapshots);
            options.snapshots = value;
        } else if (argument == "--seed") {
            CHECK(index + 1 < argc);
            CHECK(parse_u64(argv[++index], &value));
            options.seed = value;
        } else {
            CHECK(false);
        }
    }
    return options;
}

q2proto_svc_playerstate_t corpus_wire_player(
    std::uint64_t global_frame)
{
    q2proto_svc_playerstate_t player{};
    player.delta_bits = Q2P_PSD_PM_GRAVITY | Q2P_PSD_FOV |
                        Q2P_PSD_PM_VIEWHEIGHT;
    player.pm_gravity =
        static_cast<std::int16_t>(700u + global_frame % 201u);
    player.pm_viewheight =
        static_cast<std::int8_t>(18u + global_frame % 9u);
    player.fov =
        static_cast<std::uint8_t>(80u + global_frame % 31u);
    player.statbits = UINT64_C(1) << 2;
    player.stats[2] =
        static_cast<std::int16_t>((global_frame * 17u) % 1000u);
    return player;
}

player_state_t corpus_legacy_player(std::uint64_t global_frame)
{
    player_state_t player{};
    player.pmove.gravity =
        static_cast<std::int16_t>(700u + global_frame % 201u);
    player.pmove.viewheight =
        static_cast<std::int8_t>(18u + global_frame % 9u);
    player.fov =
        static_cast<float>(80u + global_frame % 31u);
    player.stats[2] =
        static_cast<std::int16_t>((global_frame * 17u) % 1000u);
    return player;
}

void set_corpus_entity(entity_state_t &entity,
                       std::uint32_t slot,
                       std::uint64_t global_frame)
{
    const std::uint32_t number = kCorpusFirstEntity + slot;
    entity = {};
    entity.number = static_cast<int>(number);
    entity.modelindex = static_cast<int>(
        1u + ((global_frame * 7u + number * 3u) %
              (kMaxModels - 1u)));
    entity.frame = static_cast<int>(
        (global_frame * 11u + number) % 256u);
}

corpus_frame_t make_corpus_frame(
    corpus_epoch_state_t &state, std::uint64_t global_frame,
    const frame_decision_t &decision)
{
    corpus_frame_t result{};
    auto &frame = result.carrier;
    const bool first = !state.initialized;
    if (decision.sequence_gap)
        ++state.next_wire;
    const std::uint32_t wire_frame = state.next_wire++;

    frame.wire.serverframe =
        static_cast<std::int32_t>(wire_frame);
    frame.wire.deltaframe =
        first ? -1 : static_cast<std::int32_t>(state.previous_wire);
    frame.wire.suppress_count =
        decision.rate_suppressed ? 1u : 0u;
    frame.wire.playerstate = corpus_wire_player(global_frame);
    frame.legacy_player = corpus_legacy_player(global_frame);
    frame.consumed_command.cursor.epoch = state.command_epoch;
    frame.consumed_command.cursor.contiguous_sequence =
        first ? 0u : wire_frame - 1u;
    frame.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    for (std::uint32_t index = 0; index < frame.area.size(); ++index) {
        frame.area[index] = static_cast<std::uint8_t>(
            mix64(global_frame + index) & UINT64_C(0xff));
    }

    if (first) {
        for (std::uint32_t slot = 0;
             slot < kCorpusEntityCount; ++slot) {
            set_corpus_entity(
                state.entities[slot], slot, global_frame + slot);
            auto &delta = frame.deltas[slot];
            delta.newnum =
                static_cast<std::uint16_t>(
                    state.entities[slot].number);
            delta.entity_delta = entity_delta(
                static_cast<std::uint16_t>(
                    state.entities[slot].modelindex),
                static_cast<std::uint16_t>(
                    state.entities[slot].frame));
        }
        frame.delta_count = kCorpusEntityCount + 1u;
        frame.deltas[kCorpusEntityCount] = {};
    } else {
        const std::uint32_t slot =
            static_cast<std::uint32_t>(
                mix64(global_frame) % kCorpusEntityCount);
        set_corpus_entity(state.entities[slot], slot, global_frame);
        frame.deltas[0].newnum =
            static_cast<std::uint16_t>(
                state.entities[slot].number);
        frame.deltas[0].entity_delta = entity_delta(
            static_cast<std::uint16_t>(
                state.entities[slot].modelindex),
            static_cast<std::uint16_t>(
                state.entities[slot].frame));
        frame.deltas[1] = {};
        frame.delta_count = 2;
    }

    for (std::uint32_t slot = 0;
         slot < kCorpusEntityCount; ++slot) {
        frame.legacy_entities[slot] = state.entities[slot];
    }
    result.tick_delta =
        first ? 0u : wire_frame - state.previous_wire;
    state.previous_wire = wire_frame;
    state.initialized = true;
    return result;
}

sv_snapshot_shadow_ref_v1 commit_corpus_frame(
    sv_snapshot_shadow_peer_v1 *shadow, frame_carrier_t &frame,
    std::uint64_t server_time_us, std::uint32_t tick_delta,
    bool fragment_stall)
{
    CHECK(shadow != nullptr);
    frame.wire.areabits = frame.area.data();
    frame.wire.areabits_len =
        static_cast<std::uint32_t>(frame.area.size());

    sv_snapshot_shadow_frame_v1 input{};
    input.struct_size = sizeof(input);
    input.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    input.wire_frame = &frame.wire;
    input.authoritative_server_tick =
        static_cast<std::uint32_t>(frame.wire.serverframe);
    input.authoritative_tick_delta = tick_delta;
    input.authoritative_server_time_us = server_time_us;
    input.controlled_entity_index = 1;
    input.consumed_command = frame.consumed_command;
    CHECK(SV_SnapshotShadowBeginFrameV1(shadow, &input) ==
          SV_SNAPSHOT_SHADOW_OK);
    if (fragment_stall)
        SV_SnapshotShadowMarkFragmentStallV1(shadow);
    for (std::uint32_t index = 0;
         index < frame.delta_count; ++index) {
        CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(
                  shadow, &frame.deltas[index]) ==
              SV_SNAPSHOT_SHADOW_OK);
    }
    sv_snapshot_shadow_ref_v1 ref{
        SV_SNAPSHOT_SHADOW_NO_SLOT, 0};
    CHECK(SV_SnapshotShadowCommitFrameV1(shadow, &ref) ==
          SV_SNAPSHOT_SHADOW_OK);
    return ref;
}

server_projection_t corpus_server_projection(
    sv_snapshot_shadow_peer_v1 *shadow,
    sv_snapshot_shadow_ref_v1 ref)
{
    server_projection_t projection{};
    CHECK(SV_SnapshotShadowViewV1(
              shadow, ref, &projection.view,
              &projection.hashes) == SV_SNAPSHOT_SHADOW_OK);
    CHECK(projection.view.snapshot != nullptr);
    CHECK(projection.view.player != nullptr);
    CHECK(projection.view.entity_count == kCorpusEntityCount);
    CHECK(projection.view.area_byte_count == 4u);
    worr_snapshot_projection_hashes_v2 recomputed{};
    CHECK(Worr_SnapshotProjectionHashesV2(
        &projection.view, kCorpusEntityLimit, &recomputed));
    CHECK(hashes_equal(projection.hashes, recomputed));
    return projection;
}

void install_corpus_legacy_frame(const frame_carrier_t &frame)
{
    cl.frame = {};
    cl.frame.valid = true;
    cl.frame.number = frame.wire.serverframe;
    cl.frame.delta = frame.wire.deltaframe;
    cl.frame.areabytes = static_cast<int>(frame.area.size());
    std::memcpy(
        cl.frame.areabits, frame.area.data(), frame.area.size());
    cl.frame.ps = frame.legacy_player;
    cl.frame.clientNum = 0;
    cl.frame.consumed_command = frame.consumed_command;
    cl.frame.numEntities =
        static_cast<int>(kCorpusEntityCount);
    cl.frame.firstEntity = 0;
    for (std::uint32_t index = 0;
         index < kCorpusEntityCount; ++index) {
        cl.entityStates[index] = frame.legacy_entities[index];
    }
}

worr_native_snapshot_expectation_v1 publish_corpus_expectation(
    frame_carrier_t &frame, std::uint64_t server_time_us)
{
    frame.wire.areabits = frame.area.data();
    frame.wire.areabits_len =
        static_cast<std::uint32_t>(frame.area.size());
    CL_SnapshotShadowBeginFrame(&frame.wire);
    CHECK(CL_SnapshotShadowSetConsumedCommand(
        &frame.consumed_command));
    for (std::uint32_t index = 0;
         index < frame.delta_count; ++index) {
        CL_SnapshotShadowCaptureEntityDelta(&frame.deltas[index]);
    }
    install_corpus_legacy_frame(frame);
    CHECK(CL_SnapshotShadowAcceptFrameEx(
        server_time_us, 1, 0, 0, 0,
        CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY));

    cl_snapshot_shadow_status_v1 status{};
    CHECK(CL_SnapshotShadowGetStatus(&status));
    CHECK(status.last_parity_mismatch == 0);
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    worr_snapshot_ref_v2 ref{};
    CHECK(CL_SnapshotShadowLatest(&view, &hashes, &ref));
    CHECK(view.snapshot != nullptr);
    worr_native_snapshot_expectation_v1 expectation{};
    CHECK(CL_SnapshotShadowGetNativeExpectation(
              view.snapshot->snapshot_id, &expectation) ==
          CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE);
    CHECK(hashes_equal(expectation.hashes, hashes));
    return expectation;
}

std::vector<wire_packet_t> collect_corpus_burst(
    fixture_t &fixture, std::uint32_t now,
    worr_snapshot_id_v2 expected_snapshot)
{
    std::vector<wire_packet_t> burst;
    std::uint16_t fragment_count = 0;
    std::uint32_t message_sequence = 0;
    for (std::uint32_t guard = 0; guard < 64u; ++guard) {
        CHECK(SV_NativeShadowOutputDueV1(
            &fixture.server, now));
        auto packet =
            prepare_packet(fixture.server_channel, now);
        const auto view = carrier_view(packet);
        CHECK(view.transport_epoch == fixture.transport_epoch);
        const auto frame = snapshot_frame(packet);
        CHECK(frame.record.object_epoch ==
              expected_snapshot.epoch);
        CHECK(frame.record.object_sequence ==
              expected_snapshot.sequence);
        if (burst.empty()) {
            fragment_count = frame.fragment_count;
            message_sequence = frame.message_sequence;
            CHECK(fragment_count != 0);
        } else {
            CHECK(frame.fragment_count == fragment_count);
            CHECK(frame.message_sequence == message_sequence);
        }
        CHECK(frame.fragment_index == burst.size());
        accept_packet(fixture.server_channel, packet);
        corpus_digest.tagged_packet(1u, packet);
        corpus_metrics.server_packets++;
        corpus_metrics.server_packet_bytes += packet.count;
        burst.push_back(packet);
        if (frame.fragment_index + 1u == fragment_count)
            break;
    }
    CHECK(!burst.empty());
    CHECK(burst.size() == fragment_count);
    corpus_metrics.server_transmissions++;
    return burst;
}

netchan_app_rx_result_t corpus_deliver_to_client(
    fixture_t &fixture, const wire_packet_t &packet,
    std::uint32_t now, std::uint64_t tag = 2u)
{
    corpus_digest.tagged_packet(tag, packet);
    corpus_metrics.server_to_client_deliveries++;
    corpus_metrics.server_to_client_delivery_bytes += packet.count;
    return deliver_to_client(fixture, packet, now);
}

netchan_app_rx_result_t corpus_deliver_to_server(
    fixture_t &fixture, const wire_packet_t &packet,
    std::uint32_t now, std::uint64_t tag = 3u)
{
    corpus_digest.tagged_packet(tag, packet);
    corpus_metrics.client_to_server_deliveries++;
    corpus_metrics.client_to_server_delivery_bytes += packet.count;
    return deliver_to_server(fixture, packet, now);
}

void digest_impairment_decision(
    std::uint64_t direction, std::uint64_t packet_ordinal,
    std::size_t packet_bytes,
    const net_impair_decision_t &decision)
{
    corpus_digest.u64(direction);
    corpus_digest.u64(packet_ordinal);
    corpus_digest.u64(packet_bytes);
    corpus_digest.u64(decision.drop ? 1u : 0u);
    corpus_digest.u64(decision.burst_drop ? 1u : 0u);
    corpus_digest.u64(decision.reordered ? 1u : 0u);
    corpus_digest.u64(decision.corrupt ? 1u : 0u);
    corpus_digest.u64(decision.copies);
    corpus_digest.u64(decision.corrupt_xor);
    corpus_digest.u64(decision.corrupt_offset);
    corpus_digest.u64(decision.release_ms[0]);
    corpus_digest.u64(decision.release_ms[1]);
}

struct scheduled_server_packet_t {
    const wire_packet_t *packet{};
    std::uint64_t release_ms{};
    std::uint64_t order{};
    std::size_t source_index{};
    bool duplicate{};
};

struct impaired_server_burst_result_t {
    std::uint32_t dropped_packets{};
    std::uint32_t delivered_copies{};
    std::uint32_t message_sequence{};
};

impaired_server_burst_result_t deliver_impaired_server_burst(
    fixture_t &fixture,
    const std::vector<wire_packet_t> &burst,
    std::uint32_t &now)
{
    CHECK(!burst.empty());
    CHECK(burst.size() <= WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS);
    std::array<scheduled_server_packet_t,
               WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS * 2u>
        scheduled{};
    std::size_t scheduled_count = 0;
    impaired_server_burst_result_t result{};
    result.message_sequence =
        snapshot_frame(burst.front()).message_sequence;

    for (std::size_t index = 0; index < burst.size(); ++index) {
        const auto decision = NetImpair_Decide(
            &corpus_impairment.server_to_client, now,
            burst[index].count, NET_IMPAIR_PACKET_NONE);
        digest_impairment_decision(
            10u, index, burst[index].count, decision);
        corpus_metrics.server_to_client_impairment_decisions++;
        if (decision.drop) {
            corpus_metrics.server_to_client_losses++;
            result.dropped_packets++;
            if (decision.burst_drop)
                corpus_metrics.burst_packet_losses++;
            continue;
        }
        CHECK(decision.copies == 1u || decision.copies == 2u);
        if (decision.copies == 2u)
            corpus_metrics.duplicate_deliveries++;
        for (std::uint32_t copy = 0;
             copy < decision.copies; ++copy) {
            CHECK(scheduled_count < scheduled.size());
            scheduled[scheduled_count++] = {
                &burst[index],
                decision.release_ms[copy],
                index * 2u + copy,
                index,
                copy != 0,
            };
        }
    }

    std::sort(
        scheduled.begin(),
        scheduled.begin() +
            static_cast<std::ptrdiff_t>(scheduled_count),
        [](const scheduled_server_packet_t &left,
           const scheduled_server_packet_t &right) {
            if (left.release_ms != right.release_ms)
                return left.release_ms < right.release_ms;
            return left.order < right.order;
        });
    bool have_primary = false;
    std::size_t greatest_primary_source = 0;
    for (std::size_t index = 0; index < scheduled_count; ++index) {
        const auto &delivery = scheduled[index];
        if (!delivery.duplicate) {
            if (have_primary &&
                delivery.source_index < greatest_primary_source) {
                corpus_metrics
                    .server_to_client_fragment_release_inversions++;
            }
            greatest_primary_source = have_primary
                ? (std::max)(greatest_primary_source,
                             delivery.source_index)
                : delivery.source_index;
            have_primary = true;
        }
        CHECK(delivery.release_ms <= UINT32_MAX);
        if (delivery.release_ms > now)
            now = static_cast<std::uint32_t>(delivery.release_ms);
        CHECK(corpus_deliver_to_client(
                  fixture, *delivery.packet, now,
                  delivery.duplicate ? 11u : 2u) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        result.delivered_copies++;
    }
    return result;
}

net_impair_decision_t decide_impaired_client_ack(
    const wire_packet_t &packet, std::uint32_t now)
{
    const auto decision = NetImpair_Decide(
        &corpus_impairment.client_to_server, now,
        packet.count, NET_IMPAIR_PACKET_UPSTREAM_SEQUENCED);
    digest_impairment_decision(
        20u, corpus_metrics.client_to_server_impairment_decisions,
        packet.count, decision);
    corpus_metrics.client_to_server_impairment_decisions++;
    if (decision.burst_drop)
        corpus_metrics.burst_packet_losses++;
    if (!decision.drop)
        corpus_metrics.upstream_ack_stalls++;
    if (decision.copies == 2u)
        corpus_metrics.duplicate_deliveries++;
    return decision;
}

wire_packet_t corrupted_packet(
    const wire_packet_t &packet, net_impair_model_t *model,
    std::uint64_t direction, std::uint32_t now)
{
    wire_packet_t corrupt = packet;
    const auto decision = NetImpair_Decide(
        model, now, corrupt.count,
        direction == 21u
            ? NET_IMPAIR_PACKET_UPSTREAM_SEQUENCED
            : NET_IMPAIR_PACKET_NONE);
    digest_impairment_decision(
        direction, 0u, corrupt.count, decision);
    if (direction == 21u)
        corpus_metrics.client_to_server_impairment_decisions++;
    else
        corpus_metrics.server_to_client_impairment_decisions++;
    CHECK(!decision.drop);
    CHECK(decision.copies == 1u);
    CHECK(decision.corrupt);
    CHECK(decision.corrupt_offset < corrupt.count);
    CHECK(decision.corrupt_xor != 0);
    corrupt.bytes[decision.corrupt_offset] ^=
        decision.corrupt_xor;
    worr_native_carrier_view_v1 view{};
    CHECK(Worr_NativeCarrierDecodeV1(
              corrupt.bytes.data(), corrupt.count, &view) ==
          WORR_NATIVE_CARRIER_CORRUPT);
    return corrupt;
}

bool corpus_ack_contains_sequence(
    const wire_packet_t &packet, std::uint32_t transport_epoch,
    std::uint32_t message_sequence)
{
    const auto view = carrier_view(packet);
    CHECK(view.transport_epoch == transport_epoch);
    CHECK(view.entry_count != 0);
    bool found = false;
    for (std::uint16_t index = 0;
         index < view.entry_count; ++index) {
        const auto &entry = view.entries[index];
        CHECK(entry.entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
        CHECK(entry.first_message_sequence <=
              entry.last_message_sequence);
        if (message_sequence >= entry.first_message_sequence &&
            message_sequence <= entry.last_message_sequence) {
            found = true;
        }
    }
    return found;
}

void check_pending_without_authority(
    fixture_t &fixture,
    const worr_cgame_snapshot_timeline_status_v2 &cgame_before,
    std::uint32_t ack_receipts_before,
    std::uint64_t releases_before,
    std::uint32_t now)
{
    const auto client = client_state();
    CHECK(client.snapshot_ack_receipts == ack_receipts_before);
    const auto cgame = cgame_status();
    CHECK(cgame.accepted == cgame_before.accepted);
    CHECK(cgame.consume_attempts ==
          cgame_before.consume_attempts);
    const auto server = server_status(fixture, now);
    CHECK(server.retained_count == 1);
    CHECK(server.active_payload_bytes != 0);
    CHECK(server.payloads_released == releases_before);
    corpus_metrics.premature_ack_checks++;
    corpus_metrics.premature_release_checks++;
}

worr_prediction_command_v1 corpus_prediction_command(
    std::uint64_t global_frame, std::uint32_t ordinal)
{
    worr_prediction_command_v1 command{};
    command.struct_size = sizeof(command);
    command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.duration_ms =
        static_cast<std::uint8_t>(8u + (ordinal & 7u));
    command.buttons =
        static_cast<std::uint8_t>((global_frame + ordinal) & 3u);
    command.view_angles[0] =
        static_cast<float>((global_frame + ordinal * 3u) % 90u);
    command.view_angles[1] =
        static_cast<float>((global_frame * 7u + ordinal * 11u) % 360u);
    command.view_angles[2] =
        static_cast<float>((global_frame + ordinal) % 45u);
    command.forward_move = static_cast<float>(
        static_cast<std::int32_t>(
            (global_frame * 13u + ordinal * 17u) % 801u) -
        400);
    command.side_move = static_cast<float>(
        static_cast<std::int32_t>(
            (global_frame * 19u + ordinal * 23u) % 801u) -
        400);
    return command;
}

void digest_prediction_command(
    const worr_cgame_prediction_input_command_v1 &entry)
{
    corpus_digest.u64(entry.legacy_sequence);
    corpus_digest.u64(entry.command_id.epoch);
    corpus_digest.u64(entry.command_id.sequence);
    corpus_digest.u64(entry.command.duration_ms);
    corpus_digest.u64(entry.command.buttons);
    for (float angle : entry.command.view_angles)
        corpus_digest.u64(std::bit_cast<std::uint32_t>(angle));
    corpus_digest.u64(
        std::bit_cast<std::uint32_t>(entry.command.forward_move));
    corpus_digest.u64(
        std::bit_cast<std::uint32_t>(entry.command.side_move));
}

void verify_prediction_range_exhaustion()
{
    worr_prediction_input_resolve_request_v1 request{};
    request.struct_size = sizeof(request);
    request.schema_version =
        WORR_PREDICTION_INPUT_RESOLVER_VERSION;
    request.flags =
        WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY |
        WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED;
    request.identity_initial_epoch = kCorpusCommandEpochBase;
    request.identity_baseline_legacy_sequence = 0;
    request.current_legacy_sequence =
        WORR_CGAME_PREDICTION_INPUT_CAPACITY;
    request.consumed_command.cursor = {
        kCorpusCommandEpochBase, 0};
    request.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;

    worr_cgame_prediction_input_range_v1 range{};
    CHECK(Worr_PredictionInputResolveV1(&request, &range) ==
          WORR_CGAME_PREDICTION_INPUT_RANGE_EXHAUSTED);
    CHECK(range.result ==
          WORR_CGAME_PREDICTION_INPUT_RANGE_EXHAUSTED);
    CHECK((range.flags &
           WORR_CGAME_PREDICTION_INPUT_HARD_RESYNC_REQUIRED) != 0);
    CHECK(range.command_count == 0);
    corpus_metrics.prediction_range_exhaustion_rejections++;
    corpus_digest.u64(128u);
    corpus_digest.u64(range.result);
    corpus_digest.u64(range.flags);
}

bool select_prediction_authority(
    const server_projection_t &projection,
    std::uint64_t global_frame, bool requested_frame)
{
    CHECK(projection.view.snapshot != nullptr);
    CHECK(projection.view.player != nullptr);
    const auto &snapshot = *projection.view.snapshot;

    cg_prediction_authority_candidate_v1 candidate{};
    CHECK(CG_CanonicalSnapshotTimelineCopyPredictionSnapshot(
              snapshot.snapshot_id.sequence,
              &candidate.timeline) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    std::array<worr_cgame_prediction_input_command_v1,
               WORR_CGAME_PREDICTION_INPUT_CAPACITY>
        history{};
    const std::uint32_t replay_count =
        global_frame % 25000u == 12345u
            ? WORR_CGAME_PREDICTION_INPUT_CAPACITY - 1u
            : 1u + static_cast<std::uint32_t>(global_frame % 4u);
    const std::uint32_t baseline =
        snapshot.consumed_command.cursor.contiguous_sequence;
    std::uint32_t history_count = 0;
    if (baseline != 0) {
        auto &entry = history[history_count++];
        entry.legacy_sequence = baseline;
        entry.command_id = {
            snapshot.consumed_command.cursor.epoch, baseline};
        entry.command =
            corpus_prediction_command(global_frame, 0);
    }

    worr_command_id_v1 next_id{};
    CHECK(Worr_CommandCursorNextIdV1(
        snapshot.consumed_command.cursor, &next_id));
    for (std::uint32_t index = 0; index < replay_count; ++index) {
        auto &entry = history[history_count++];
        entry.legacy_sequence = baseline + index + 1u;
        entry.command_id = next_id;
        entry.command =
            corpus_prediction_command(global_frame, index + 1u);
        if (index + 1u < replay_count)
            CHECK(Worr_CommandIdNextV1(next_id, &next_id));
    }

    worr_prediction_input_resolve_request_v1 request{};
    request.struct_size = sizeof(request);
    request.schema_version =
        WORR_PREDICTION_INPUT_RESOLVER_VERSION;
    request.flags =
        WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY |
        WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED;
    request.identity_initial_epoch =
        snapshot.consumed_command.cursor.epoch;
    request.identity_baseline_legacy_sequence = 0;
    request.current_legacy_sequence = baseline + replay_count;
    request.legacy_acknowledged_sequence = baseline;
    request.history_count = history_count;
    request.consumed_command = snapshot.consumed_command;
    request.pending_present =
        static_cast<std::uint32_t>(global_frame % 3u == 0);
    if (request.pending_present) {
        request.pending_command.legacy_sequence =
            request.current_legacy_sequence + 1u;
        request.pending_command.command =
            corpus_prediction_command(
                global_frame, replay_count + 1u);
    }
    request.history = history.data();
    CHECK(Worr_PredictionInputResolveV1(
              &request, &candidate.input) ==
          WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(candidate.input.result ==
          WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(candidate.input.source ==
          WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR);
    CHECK(candidate.input.command_count == replay_count);
    CHECK(candidate.input.command_count != 0);
    CHECK(((candidate.input.flags &
            WORR_CGAME_PREDICTION_INPUT_HAS_PENDING) != 0) ==
          (request.pending_present != 0));

    cg_prediction_authority_expectation_v1 expectation{};
    expectation.snapshot_sequence = snapshot.snapshot_id.sequence;
    expectation.server_tick = snapshot.server_tick;
    expectation.server_time_us = snapshot.server_time_us;
    expectation.controlled_entity_index =
        snapshot.controlled_entity.identity.index;

    cg_prediction_authority_v1 authority{};
    CHECK(CG_PredictionAuthoritySelectV1(
              &expectation, &candidate, &authority) ==
          cg_prediction_authority_result_v1::canonical);
    CHECK(authority.result ==
          cg_prediction_authority_result_v1::canonical);
    CHECK(snapshot_id_equal(
        authority.timeline.snapshot.snapshot_id,
        snapshot.snapshot_id));
    CHECK(authority.timeline.snapshot.snapshot_hash ==
          snapshot.snapshot_hash);
    CHECK(consumed_command_equal(
        authority.input.consumed_command,
        snapshot.consumed_command));
    CHECK(authority.input.command_count == replay_count);
    CHECK(std::memcmp(
              &authority.timeline.player, projection.view.player,
              sizeof(*projection.view.player)) == 0);
    corpus_digest.u64(authority.input.authoritative_legacy_sequence);
    corpus_digest.u64(authority.input.current_legacy_sequence);
    corpus_digest.u64(authority.input.command_count);
    for (std::uint32_t index = 0;
         index < authority.input.command_count; ++index) {
        digest_prediction_command(authority.input.commands[index]);
    }
    if (request.pending_present)
        digest_prediction_command(authority.input.pending_command);

    constexpr std::uint32_t reset_flags =
        WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
        WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP |
        WORR_SNAPSHOT_DISCONTINUITY_BASE_JUMP |
        WORR_SNAPSHOT_DISCONTINUITY_FRAGMENT_STALL |
        WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET |
        WORR_SNAPSHOT_DISCONTINUITY_DEMO_REWIND |
        WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED |
        WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC |
        WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH;
    const bool expected_reset =
        (snapshot.discontinuity.flags & reset_flags) != 0;
    CHECK((authority.history_reset_required != 0) ==
          expected_reset);
    if (requested_frame) {
        corpus_metrics.prediction_authorities++;
        corpus_metrics.resolved_prediction_commands += replay_count;
        corpus_metrics.prediction_max_replay = (std::max)(
            corpus_metrics.prediction_max_replay,
            static_cast<std::uint64_t>(replay_count));
        if (request.pending_present)
            corpus_metrics.prediction_pending_ranges++;
        else
            corpus_metrics.prediction_nonpending_ranges++;
        if (baseline == 0)
            corpus_metrics.prediction_bootstrap_ranges++;
        else
            corpus_metrics.prediction_nonzero_cursor_ranges++;
        if (replay_count ==
            WORR_CGAME_PREDICTION_INPUT_CAPACITY - 1u) {
            corpus_metrics.prediction_limit_ranges++;
        }
    } else {
        corpus_metrics
            .corrupt_ack_probe_prediction_authorities++;
    }
    if (expected_reset)
        corpus_metrics.authority_history_resets++;
    return expected_reset;
}

void digest_projection(const server_projection_t &projection,
                       std::uint64_t global_frame)
{
    const auto &snapshot = *projection.view.snapshot;
    corpus_digest.u64(global_frame);
    corpus_digest.u64(snapshot.snapshot_id.epoch);
    corpus_digest.u64(snapshot.snapshot_id.sequence);
    corpus_digest.u64(snapshot.server_tick);
    corpus_digest.u64(snapshot.server_time_us);
    corpus_digest.u64(snapshot.snapshot_hash);
    corpus_digest.u64(projection.hashes.endpoint_hash);
    corpus_digest.u64(projection.hashes.legacy_parity_hash);
    corpus_digest.u64(projection.hashes.semantic_player_hash);
    corpus_digest.u64(projection.hashes.semantic_entity_hash);
    corpus_digest.u64(projection.hashes.semantic_area_hash);
    corpus_digest.u64(snapshot.discontinuity.flags);
}

void process_corpus_frame(
    fixture_t &fixture, corpus_epoch_state_t &epoch_state,
    std::uint64_t global_frame, std::uint64_t seed,
    std::uint32_t &now, bool corrupt_ack_probe)
{
    corpus_current_frame = global_frame;
    const auto decision = frame_decision(
        seed, global_frame, !epoch_state.initialized);
    auto frame = make_corpus_frame(
        epoch_state, global_frame, decision);
    const std::uint64_t server_time_us =
        (global_frame + 1u) * kCorpusTickUs;
    const auto ref = commit_corpus_frame(
        fixture.server_snapshot_shadow, frame.carrier,
        server_time_us, frame.tick_delta,
        decision.fragment_stall);
    const auto projection = corpus_server_projection(
        fixture.server_snapshot_shadow, ref);
    digest_projection(projection, global_frame);
    corpus_metrics.serialized_frames++;
    if (decision.sequence_gap)
        corpus_metrics.sequence_gaps++;
    if (decision.fragment_stall)
        corpus_metrics.fragment_stalls++;
    if (decision.rate_suppressed)
        corpus_metrics.rate_suppressions++;

    const auto cgame_before = cgame_status();
    const std::uint32_t ack_receipts_before =
        client_state().snapshot_ack_receipts;
    const auto server_before = server_status(fixture, now);
    const std::uint64_t releases_before =
        server_before.payloads_released;

    worr_native_snapshot_expectation_v1 expectation{};
    if (!decision.late_expectation) {
        expectation = publish_corpus_expectation(
            frame.carrier, server_time_us);
        CHECK(snapshot_id_equal(
            expectation.snapshot_id,
            projection.view.snapshot->snapshot_id));
        CHECK(expectation_parity_equal(
            expectation.hashes, projection.hashes));
        CL_NativeReadinessPilotSnapshotExpectationReady();
    } else {
        corpus_metrics.late_expectations++;
    }

    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        ref, now));
    const auto queued = server_status(fixture, now);
    CHECK(queued.retained_count == 1);
    CHECK(queued.active_payload_bytes != 0);
    CHECK(queued.payloads_released == releases_before);

    std::uint32_t message_sequence = 0;
    bool server_delivery_complete = false;
    for (std::uint32_t guard = 0; guard < 1024u; ++guard) {
        const auto burst = collect_corpus_burst(
            fixture, now,
            projection.view.snapshot->snapshot_id);
        const auto impaired = deliver_impaired_server_burst(
            fixture, burst, now);
        message_sequence = impaired.message_sequence;
        if (decision.late_expectation) {
            server_delivery_complete =
                impaired.dropped_packets == 0;
        } else {
            const auto current_cgame = cgame_status();
            server_delivery_complete =
                current_cgame.accepted ==
                cgame_before.accepted + 1u;
        }
        if (server_delivery_complete)
            break;
        check_pending_without_authority(
            fixture, cgame_before, ack_receipts_before,
            releases_before, now);
        CHECK(now <=
              UINT32_MAX -
                  SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS);
        now += SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS;
    }
    CHECK(server_delivery_complete);

    if (decision.late_expectation) {
        check_pending_without_authority(
            fixture, cgame_before, ack_receipts_before,
            releases_before, now);
        expectation = publish_corpus_expectation(
            frame.carrier, server_time_us);
        CHECK(snapshot_id_equal(
            expectation.snapshot_id,
            projection.view.snapshot->snapshot_id));
        CHECK(expectation_parity_equal(
            expectation.hashes, projection.hashes));
        CL_NativeReadinessPilotSnapshotExpectationReady();
    }

    const auto client_after = client_state();
    CHECK(client_after.snapshot_rx_occupied == 0);
    /* The production receipt ledger coalesces contiguous message
     * sequences, so a newly authorized receipt may extend an existing range
     * without increasing receipt_count.  OutputDue plus the exact range
     * check on the prepared carrier below proves current ACK authority. */
    CHECK(client_after.snapshot_ack_receipts != 0);
    CHECK(client_after.snapshot_ack_receipts >=
          ack_receipts_before);
    CHECK(CL_NativeReadinessPilotOutputDue());
    const auto cgame_after = cgame_status();
    CHECK(cgame_after.accepted == cgame_before.accepted + 1u);
    CHECK(cgame_after.consume_attempts ==
          cgame_before.consume_attempts + 1u);
    CHECK(cgame_after.admission_generation ==
          cgame_before.admission_generation + 1u);
    CHECK(snapshot_id_equal(
        cgame_after.last_snapshot_id,
        projection.view.snapshot->snapshot_id));
    CHECK(cgame_after.last_snapshot_hash ==
          projection.view.snapshot->snapshot_hash);
    if (corrupt_ack_probe)
        corpus_metrics.corrupt_ack_probe_acceptances++;
    else
        corpus_metrics.accepted_frames++;
    corpus_metrics.exact_once_checks++;
    (void)select_prediction_authority(
        projection, global_frame, !corrupt_ack_probe);

    const auto retained_before_ack = server_status(fixture, now);
    CHECK(retained_before_ack.retained_count == 1);
    CHECK(retained_before_ack.active_payload_bytes != 0);
    CHECK(retained_before_ack.payloads_released ==
          releases_before);
    corpus_metrics.premature_release_checks++;

    ++now;
    bool acknowledgement_delivered = false;
    for (std::uint32_t guard = 0; guard < 1024u; ++guard) {
        auto acknowledgement = prepare_packet(cls.netchan, now);
        CHECK(corpus_ack_contains_sequence(
            acknowledgement, fixture.transport_epoch,
            message_sequence));
        accept_packet(cls.netchan, acknowledgement);
        corpus_digest.tagged_packet(5u, acknowledgement);

        if (corrupt_ack_probe) {
            const auto corrupt = corrupted_packet(
                acknowledgement,
                &corpus_impairment.corrupt_client_to_server,
                21u, now);
            CHECK(corpus_deliver_to_server(
                      fixture, corrupt, now, 6u) ==
                  NETCHAN_APP_RX_REJECT);
            CHECK(!SV_NativeShadowPeerEnabledV1(&fixture.server));
            const auto retained = server_status(fixture, now);
            CHECK(retained.retained_count == 1);
            CHECK(retained.payloads_released == releases_before);
            corpus_metrics.corrupt_client_to_server++;
            corpus_metrics.corrupt_rejections++;
            corpus_metrics.retained_until_epoch_reset++;
            return;
        }

        const auto ack_decision =
            decide_impaired_client_ack(acknowledgement, now);
        if (ack_decision.drop) {
            corpus_metrics.acknowledgement_losses++;
            const auto retained = server_status(fixture, now);
            CHECK(retained.retained_count == 1);
            CHECK(retained.payloads_released == releases_before);
            corpus_metrics.premature_release_checks++;

            bool repeat_revalidated = false;
            for (std::uint32_t retry_guard = 0;
                 retry_guard < 1024u; ++retry_guard) {
                CHECK(now <=
                      UINT32_MAX -
                          SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS);
                now += SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS;
                const auto repeat = collect_corpus_burst(
                    fixture, now,
                    projection.view.snapshot->snapshot_id);
                const auto repeat_before = cgame_status();
                const auto repeat_client_before = client_state();
                const auto repeat_delivery =
                    deliver_impaired_server_burst(
                    fixture, repeat, now);
                const auto repeat_after = cgame_status();
                const auto repeat_client_after = client_state();
                CHECK(repeat_after.accepted ==
                      repeat_before.accepted);
                CHECK(repeat_after.consume_attempts ==
                      repeat_before.consume_attempts);
                CHECK(repeat_after.admission_generation ==
                      repeat_before.admission_generation);
                CHECK(repeat_client_after.snapshot_ack_receipts ==
                      repeat_client_before.snapshot_ack_receipts);
                if (repeat_delivery.delivered_copies != 0) {
                    repeat_revalidated = true;
                    break;
                }
            }
            CHECK(repeat_revalidated);
            corpus_metrics.repeat_revalidations++;
            corpus_metrics.exact_once_checks++;
            ++now;
            continue;
        }

        CHECK(ack_decision.copies == 1u ||
              ack_decision.copies == 2u);
        for (std::uint32_t copy = 0;
             copy < ack_decision.copies; ++copy) {
            CHECK(ack_decision.release_ms[copy] <= UINT32_MAX);
            if (ack_decision.release_ms[copy] > now) {
                now = static_cast<std::uint32_t>(
                    ack_decision.release_ms[copy]);
            }
            const auto before_copy = server_status(fixture, now);
            CHECK(corpus_deliver_to_server(
                      fixture, acknowledgement, now,
                      copy == 0 ? 3u : 22u) ==
                  NETCHAN_APP_RX_EXPOSE_LEGACY);
            if (copy != 0) {
                const auto after_copy =
                    server_status(fixture, now);
                CHECK(after_copy.acknowledgements_applied ==
                      before_copy.acknowledgements_applied);
                CHECK(after_copy.payloads_released ==
                      before_copy.payloads_released);
            }
        }
        acknowledgement_delivered = true;
        break;
    }
    CHECK(acknowledgement_delivered);
    const auto released = server_status(fixture, now);
    CHECK(released.retained_count == 0);
    CHECK(released.active_payload_bytes == 0);
    CHECK(released.acknowledgements_applied ==
          server_before.acknowledgements_applied + 1u);
    CHECK(released.payloads_released == releases_before + 1u);
    corpus_metrics.acknowledged_frames++;
    corpus_metrics.released_frames++;
    ++now;
}

void run_corrupt_server_probe(
    fixture_t &fixture, corpus_epoch_state_t &epoch_state,
    std::uint64_t global_frame, std::uint64_t seed,
    std::uint32_t &now)
{
    corpus_current_frame = global_frame;
    auto decision = frame_decision(
        seed ^ UINT64_C(0xc0ffee), global_frame,
        !epoch_state.initialized);
    decision.sequence_gap = false;
    decision.fragment_stall = false;
    decision.rate_suppressed = false;
    auto frame = make_corpus_frame(
        epoch_state, global_frame, decision);
    const std::uint64_t server_time_us =
        (global_frame + 1u) * kCorpusTickUs;
    const auto ref = commit_corpus_frame(
        fixture.server_snapshot_shadow, frame.carrier,
        server_time_us, frame.tick_delta, false);
    const auto projection = corpus_server_projection(
        fixture.server_snapshot_shadow, ref);
    digest_projection(
        projection, global_frame ^ UINT64_C(0x8000000000000000));
    corpus_metrics.serialized_frames++;
    corpus_metrics.negative_probe_frames++;

    const auto cgame_before = cgame_status();
    const auto client_before = client_state();
    const auto server_before = server_status(fixture, now);
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        ref, now));
    const auto burst = collect_corpus_burst(
        fixture, now, projection.view.snapshot->snapshot_id);
    const auto corrupt = corrupted_packet(
        burst.front(),
        &corpus_impairment.corrupt_server_to_client,
        12u, now);
    CHECK(corpus_deliver_to_client(
              fixture, corrupt, now, 9u) ==
          NETCHAN_APP_RX_REJECT);
    CHECK(client_state().mode == 3u);
    CHECK(!CL_NativeReadinessPilotOutputDue());
    const auto cgame_after = cgame_status();
    CHECK(cgame_after.accepted == cgame_before.accepted);
    CHECK(cgame_after.consume_attempts ==
          cgame_before.consume_attempts);
    CHECK(client_state().snapshot_ack_receipts ==
          client_before.snapshot_ack_receipts);
    const auto server_after = server_status(fixture, now);
    CHECK(server_after.retained_count == 1);
    CHECK(server_after.payloads_released ==
          server_before.payloads_released);
    corpus_metrics.corrupt_server_to_client++;
    corpus_metrics.corrupt_rejections++;
    corpus_metrics.premature_ack_checks++;
    corpus_metrics.premature_release_checks++;
    corpus_metrics.retained_until_epoch_reset++;
    ++now;
}

void digest_metrics(const corpus_metrics_t &metrics,
                    const corpus_options_t &options)
{
    corpus_digest.u64(options.snapshots);
    corpus_digest.u64(options.seed);
    corpus_digest.u64(metrics.serialized_frames);
    corpus_digest.u64(metrics.accepted_frames);
    corpus_digest.u64(metrics.acknowledged_frames);
    corpus_digest.u64(metrics.released_frames);
    corpus_digest.u64(metrics.prediction_authorities);
    corpus_digest.u64(metrics.resolved_prediction_commands);
    corpus_digest.u64(metrics.prediction_pending_ranges);
    corpus_digest.u64(metrics.prediction_nonpending_ranges);
    corpus_digest.u64(metrics.prediction_max_replay);
    corpus_digest.u64(metrics.prediction_bootstrap_ranges);
    corpus_digest.u64(metrics.prediction_nonzero_cursor_ranges);
    corpus_digest.u64(metrics.prediction_limit_ranges);
    corpus_digest.u64(
        metrics.prediction_range_exhaustion_rejections);
    corpus_digest.u64(metrics.negative_probe_frames);
    corpus_digest.u64(metrics.corrupt_ack_probe_acceptances);
    corpus_digest.u64(
        metrics.corrupt_ack_probe_prediction_authorities);
    corpus_digest.u64(metrics.authority_history_resets);
    corpus_digest.u64(metrics.epochs);
    corpus_digest.u64(metrics.connection_activations);
    corpus_digest.u64(metrics.sequence_gaps);
    corpus_digest.u64(metrics.fragment_stalls);
    corpus_digest.u64(metrics.rate_suppressions);
    corpus_digest.u64(metrics.late_expectations);
    corpus_digest.u64(metrics.server_transmissions);
    corpus_digest.u64(metrics.server_packets);
    corpus_digest.u64(metrics.server_packet_bytes);
    corpus_digest.u64(metrics.server_to_client_deliveries);
    corpus_digest.u64(metrics.server_to_client_delivery_bytes);
    corpus_digest.u64(metrics.client_to_server_deliveries);
    corpus_digest.u64(metrics.client_to_server_delivery_bytes);
    corpus_digest.u64(metrics.server_to_client_losses);
    corpus_digest.u64(
        metrics.server_to_client_impairment_decisions);
    corpus_digest.u64(
        metrics.client_to_server_impairment_decisions);
    corpus_digest.u64(metrics.burst_packet_losses);
    corpus_digest.u64(
        metrics.server_to_client_fragment_release_inversions);
    corpus_digest.u64(metrics.duplicate_deliveries);
    corpus_digest.u64(metrics.acknowledgement_losses);
    corpus_digest.u64(metrics.upstream_ack_stalls);
    corpus_digest.u64(metrics.repeat_revalidations);
    corpus_digest.u64(metrics.corrupt_server_to_client);
    corpus_digest.u64(metrics.corrupt_client_to_server);
    corpus_digest.u64(metrics.corrupt_rejections);
    corpus_digest.u64(metrics.exact_once_checks);
    corpus_digest.u64(metrics.premature_ack_checks);
    corpus_digest.u64(metrics.premature_release_checks);
    corpus_digest.u64(metrics.retained_until_epoch_reset);
}

void validate_corpus(const corpus_options_t &options)
{
    CHECK(corpus_metrics.requested_frames == options.snapshots);
    CHECK(corpus_metrics.accepted_frames == options.snapshots);
    CHECK(corpus_metrics.acknowledged_frames == options.snapshots);
    CHECK(corpus_metrics.released_frames == options.snapshots);
    CHECK(corpus_metrics.prediction_authorities ==
          options.snapshots);
    CHECK(corpus_metrics.resolved_prediction_commands >=
          options.snapshots);
    CHECK(corpus_metrics.prediction_pending_ranges != 0);
    if (options.snapshots > 1)
        CHECK(corpus_metrics.prediction_nonpending_ranges != 0);
    CHECK(corpus_metrics.prediction_max_replay != 0);
    CHECK(corpus_metrics.prediction_bootstrap_ranges ==
          corpus_metrics.epochs);
    CHECK(corpus_metrics.prediction_nonzero_cursor_ranges ==
          options.snapshots - corpus_metrics.epochs);
    const std::uint64_t expected_limit_ranges =
        options.snapshots <= 12345u
            ? 0u
            : 1u + (options.snapshots - 1u - 12345u) /
                       25000u;
    CHECK(corpus_metrics.prediction_limit_ranges ==
          expected_limit_ranges);
    CHECK(corpus_metrics.prediction_range_exhaustion_rejections ==
          1u);
    CHECK(corpus_metrics.serialized_frames ==
          options.snapshots +
              corpus_metrics.negative_probe_frames);
    CHECK(corpus_metrics.retained_until_epoch_reset ==
          corpus_metrics.negative_probe_frames);
    CHECK(corpus_metrics.corrupt_ack_probe_acceptances ==
          corpus_metrics.corrupt_client_to_server);
    CHECK(corpus_metrics.corrupt_ack_probe_prediction_authorities ==
          corpus_metrics.corrupt_ack_probe_acceptances);
    CHECK(corpus_metrics.corrupt_rejections ==
          corpus_metrics.corrupt_server_to_client +
              corpus_metrics.corrupt_client_to_server);
    CHECK(corpus_metrics.server_packets != 0);
    CHECK(corpus_metrics.server_to_client_deliveries != 0);
    CHECK(corpus_metrics.client_to_server_deliveries != 0);
    CHECK(corpus_metrics.server_to_client_impairment_decisions != 0);
    CHECK(corpus_metrics.client_to_server_impairment_decisions != 0);
    CHECK(corpus_metrics.exact_once_checks >= options.snapshots);
    CHECK(corpus_metrics.premature_release_checks >=
          options.snapshots);
    if (options.snapshots >= kDefaultSnapshots) {
        CHECK(corpus_metrics.epochs == kCorpusEpochLimit);
        CHECK(corpus_metrics.connection_activations ==
              kCorpusEpochLimit);
        CHECK(corpus_metrics.prediction_limit_ranges == 4u);
        CHECK(corpus_metrics.prediction_max_replay ==
              WORR_CGAME_PREDICTION_INPUT_CAPACITY - 1u);
        CHECK(corpus_metrics.server_to_client_losses != 0);
        CHECK(corpus_metrics
                  .server_to_client_fragment_release_inversions != 0);
        CHECK(corpus_metrics.duplicate_deliveries != 0);
        CHECK(corpus_metrics.acknowledgement_losses != 0);
        CHECK(corpus_metrics.repeat_revalidations != 0);
        CHECK(corpus_metrics.sequence_gaps != 0);
        CHECK(corpus_metrics.fragment_stalls != 0);
        CHECK(corpus_metrics.rate_suppressions != 0);
        CHECK(corpus_metrics.late_expectations != 0);
        CHECK(corpus_metrics.corrupt_server_to_client != 0);
        CHECK(corpus_metrics.corrupt_client_to_server != 0);
        CHECK(corpus_metrics.authority_history_resets != 0);
    }
}

void print_json(const corpus_options_t &options)
{
    std::printf(
        "{\"schema\":\"worr.native_snapshot_production_corpus.v1\"," 
        "\"classification\":\"headless_deterministic_serialized_"
        "production_path\"," 
        "\"status\":\"ok\"," 
        "\"requested_frames\":%llu," 
        "\"accepted_frames\":%llu," 
        "\"seed\":%llu," 
        "\"corpus_digest\":\"%016llx\"," 
        "\"coverage\":{" ,
        static_cast<unsigned long long>(options.snapshots),
        static_cast<unsigned long long>(
            corpus_metrics.accepted_frames),
        static_cast<unsigned long long>(options.seed),
        static_cast<unsigned long long>(corpus_digest.value));
    std::printf(
        "\"serialized_frames\":%llu," 
        "\"acknowledged_frames\":%llu," 
        "\"released_frames\":%llu," 
        "\"prediction_authorities\":%llu," ,
        static_cast<unsigned long long>(
            corpus_metrics.serialized_frames),
        static_cast<unsigned long long>(
            corpus_metrics.acknowledged_frames),
        static_cast<unsigned long long>(
            corpus_metrics.released_frames),
        static_cast<unsigned long long>(
            corpus_metrics.prediction_authorities));
    std::printf(
        "\"resolved_prediction_commands\":%llu," 
        "\"prediction_pending_ranges\":%llu," 
        "\"prediction_nonpending_ranges\":%llu," 
        "\"prediction_max_replay\":%llu," ,
        static_cast<unsigned long long>(
            corpus_metrics.resolved_prediction_commands),
        static_cast<unsigned long long>(
            corpus_metrics.prediction_pending_ranges),
        static_cast<unsigned long long>(
            corpus_metrics.prediction_nonpending_ranges),
        static_cast<unsigned long long>(
            corpus_metrics.prediction_max_replay));
    std::printf(
        "\"prediction_bootstrap_ranges\":%llu," 
        "\"prediction_nonzero_cursor_ranges\":%llu," 
        "\"prediction_limit_ranges\":%llu," 
        "\"prediction_range_exhaustion_rejections\":%llu," ,
        static_cast<unsigned long long>(
            corpus_metrics.prediction_bootstrap_ranges),
        static_cast<unsigned long long>(
            corpus_metrics.prediction_nonzero_cursor_ranges),
        static_cast<unsigned long long>(
            corpus_metrics.prediction_limit_ranges),
        static_cast<unsigned long long>(corpus_metrics
            .prediction_range_exhaustion_rejections));
    std::printf(
        "\"negative_probe_frames\":%llu," 
        "\"corrupt_ack_probe_acceptances\":%llu," 
        "\"corrupt_ack_probe_prediction_authorities\":%llu," 
        "\"authority_history_resets\":%llu," ,
        static_cast<unsigned long long>(
            corpus_metrics.negative_probe_frames),
        static_cast<unsigned long long>(
            corpus_metrics.corrupt_ack_probe_acceptances),
        static_cast<unsigned long long>(
            corpus_metrics
                .corrupt_ack_probe_prediction_authorities),
        static_cast<unsigned long long>(
            corpus_metrics.authority_history_resets));
    std::printf(
        "\"epochs\":%llu," 
        "\"connection_activations\":%llu," 
        "\"sequence_gaps\":%llu," 
        "\"fragment_stalls\":%llu," ,
        static_cast<unsigned long long>(corpus_metrics.epochs),
        static_cast<unsigned long long>(
            corpus_metrics.connection_activations),
        static_cast<unsigned long long>(
            corpus_metrics.sequence_gaps),
        static_cast<unsigned long long>(
            corpus_metrics.fragment_stalls));
    std::printf(
        "\"rate_suppressions\":%llu," 
        "\"late_expectations\":%llu," 
        "\"server_transmissions\":%llu," 
        "\"server_packets\":%llu," ,
        static_cast<unsigned long long>(
            corpus_metrics.rate_suppressions),
        static_cast<unsigned long long>(
            corpus_metrics.late_expectations),
        static_cast<unsigned long long>(
            corpus_metrics.server_transmissions),
        static_cast<unsigned long long>(
            corpus_metrics.server_packets));
    std::printf(
        "\"server_packet_bytes\":%llu," 
        "\"server_to_client_deliveries\":%llu," 
        "\"server_to_client_delivery_bytes\":%llu," 
        "\"client_to_server_deliveries\":%llu," ,
        static_cast<unsigned long long>(
            corpus_metrics.server_packet_bytes),
        static_cast<unsigned long long>(
            corpus_metrics.server_to_client_deliveries),
        static_cast<unsigned long long>(
            corpus_metrics.server_to_client_delivery_bytes),
        static_cast<unsigned long long>(
            corpus_metrics.client_to_server_deliveries));
    std::printf(
        "\"client_to_server_delivery_bytes\":%llu," 
        "\"server_to_client_losses\":%llu," 
        "\"server_to_client_impairment_decisions\":%llu," 
        "\"client_to_server_impairment_decisions\":%llu," ,
        static_cast<unsigned long long>(
            corpus_metrics.client_to_server_delivery_bytes),
        static_cast<unsigned long long>(
            corpus_metrics.server_to_client_losses),
        static_cast<unsigned long long>(
            corpus_metrics.server_to_client_impairment_decisions),
        static_cast<unsigned long long>(
            corpus_metrics.client_to_server_impairment_decisions));
    std::printf(
        "\"burst_packet_losses\":%llu," 
        "\"server_to_client_fragment_release_inversions\":%llu," 
        "\"duplicate_deliveries\":%llu," 
        "\"acknowledgement_losses\":%llu," ,
        static_cast<unsigned long long>(
            corpus_metrics.burst_packet_losses),
        static_cast<unsigned long long>(
            corpus_metrics
                .server_to_client_fragment_release_inversions),
        static_cast<unsigned long long>(
            corpus_metrics.duplicate_deliveries),
        static_cast<unsigned long long>(
            corpus_metrics.acknowledgement_losses));
    std::printf(
        "\"upstream_ack_stalls\":%llu," 
        "\"repeat_revalidations\":%llu," 
        "\"corrupt_server_to_client\":%llu," 
        "\"corrupt_client_to_server\":%llu," ,
        static_cast<unsigned long long>(
            corpus_metrics.upstream_ack_stalls),
        static_cast<unsigned long long>(
            corpus_metrics.repeat_revalidations),
        static_cast<unsigned long long>(
            corpus_metrics.corrupt_server_to_client),
        static_cast<unsigned long long>(
            corpus_metrics.corrupt_client_to_server));
    std::printf(
        "\"corrupt_rejections\":%llu," 
        "\"exact_once_checks\":%llu," 
        "\"premature_ack_checks\":%llu," 
        "\"premature_release_checks\":%llu," ,
        static_cast<unsigned long long>(
            corpus_metrics.corrupt_rejections),
        static_cast<unsigned long long>(
            corpus_metrics.exact_once_checks),
        static_cast<unsigned long long>(
            corpus_metrics.premature_ack_checks),
        static_cast<unsigned long long>(
            corpus_metrics.premature_release_checks));
    std::printf(
        "\"retained_until_epoch_reset\":%llu}}\n",
        static_cast<unsigned long long>(
            corpus_metrics.retained_until_epoch_reset));
}

} // namespace

int main(int argc, char **argv)
{
    const auto options = parse_options(argc, argv);
    corpus_metrics.requested_frames = options.snapshots;
    corpus_digest.u64(options.seed);
    corpus_digest.u64(options.snapshots);
    initialize_corpus_impairment(options.seed);
    verify_prediction_range_exhaustion();

    const std::uint32_t epoch_count =
        static_cast<std::uint32_t>(
            options.snapshots < kCorpusEpochLimit
                ? options.snapshots
                : kCorpusEpochLimit);
    std::uint64_t global_frame = 0;
    std::uint32_t now = kCorpusStartTime;
    fixture_t fixture{};

    for (std::uint32_t epoch_index = 0;
         epoch_index < epoch_count; ++epoch_index) {
        const std::uint64_t epoch_frames =
            options.snapshots / epoch_count +
            (epoch_index < options.snapshots % epoch_count ? 1u : 0u);
        reset_fixture(
            fixture, now,
            kCorpusOfficialEpochBase + epoch_index,
            kCorpusSnapshotEpochBase + epoch_index,
            kCorpusEntityLimit, PROTOCOL_VERSION_RERELEASE);
        corpus_metrics.epochs++;
        corpus_metrics.connection_activations++;
        const auto activated = cgame_status();
        CHECK(activated.active_epoch ==
              kCorpusSnapshotEpochBase + epoch_index);
        /* Readiness activation advances both production peers through
         * now+3; corpus traffic must never move their admission clocks
         * backwards. */
        now += 10u;

        corpus_epoch_state_t epoch_state{};
        epoch_state.command_epoch =
            kCorpusCommandEpochBase + epoch_index;
        for (std::uint64_t epoch_frame = 0;
             epoch_frame < epoch_frames; ++epoch_frame) {
            process_corpus_frame(
                fixture, epoch_state, global_frame,
                options.seed, now, false);
            ++global_frame;
        }

        if (epoch_index == 1u &&
            epoch_index + 1u < epoch_count) {
            corpus_metrics.negative_probe_frames++;
            process_corpus_frame(
                fixture, epoch_state,
                global_frame ^ UINT64_C(0x4000000000000000),
                options.seed ^ UINT64_C(0xa11c0de), now, true);
        }
        if ((epoch_index == 0u || epoch_index == 2u) &&
            epoch_index + 1u < epoch_count) {
            run_corrupt_server_probe(
                fixture, epoch_state, global_frame,
                options.seed, now);
        }
        cleanup_fixture(fixture);
        fixture = {};
        ++now;
    }

    CHECK(global_frame == options.snapshots);
    validate_corpus(options);
    digest_metrics(corpus_metrics, options);
    print_json(options);
    return EXIT_SUCCESS;
}
