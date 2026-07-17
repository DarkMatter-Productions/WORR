/* Transactional native canonical snapshot admission tests (FR-10-T04/T06). */

#include "common/net/native_snapshot_admission.h"

#include "common/net/native_carrier.h"
#include "common/net/snapshot_store.h"
#include "shared/cgame_event_runtime.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "native_snapshot_admission_test:%d: %s\n",    \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

enum {
    TEST_MAX_ENTITIES = 64,
    TEST_RX_CAPACITY = 2,
    TEST_PAYLOAD_STRIDE = 8192,
    TEST_SOURCE_SLOTS = 8,
    TEST_ENTITIES_PER_SLOT = 2,
    TEST_AREA_PER_SLOT = 8,
    TEST_EVENTS_PER_SLOT = 2,
    TEST_MAX_FRAGMENTS = 16,
};

#define TEST_OWNER_ID UINT64_C(0x0a0b0c0d10203040)
#define TEST_TRANSPORT_EPOCH UINT32_C(91)
#define TEST_SNAPSHOT_EPOCH UINT32_C(17)

typedef struct source_store_s {
    worr_snapshot_store_v2 store;
    worr_snapshot_store_slot_v2 slots[TEST_SOURCE_SLOTS];
    worr_snapshot_entity_v2
        entities[TEST_SOURCE_SLOTS * TEST_ENTITIES_PER_SLOT];
    uint8_t area[TEST_SOURCE_SLOTS * TEST_AREA_PER_SLOT];
    worr_snapshot_event_ref_v2
        events[TEST_SOURCE_SLOTS * TEST_EVENTS_PER_SLOT];
} source_store;

typedef struct fake_consumer_s {
    worr_cgame_snapshot_timeline_status_v2 status;
    uint32_t reset_calls;
    uint32_t consume_calls;
    uint32_t status_calls;
    bool reject_consume;
    bool reject_event_fence;
} fake_consumer;

typedef struct test_fixture_s {
    worr_native_session_binding_v1 binding;
    worr_native_snapshot_admission_state_v1 admission;
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[TEST_RX_CAPACITY];
    uint8_t arena[TEST_RX_CAPACITY * TEST_PAYLOAD_STRIDE];
    worr_native_carrier_ack_ledger_v1 ledger;
    worr_snapshot_v2 decoded_snapshot;
    worr_snapshot_player_v2 decoded_player;
    worr_snapshot_entity_v2 decoded_entities[TEST_ENTITIES_PER_SLOT];
    uint8_t decoded_area[TEST_AREA_PER_SLOT];
    worr_snapshot_event_ref_v2 decoded_events[TEST_EVENTS_PER_SLOT];
    worr_native_snapshot_decode_storage_v1 scratch;
    fake_consumer fake;
    worr_native_snapshot_consumer_v1 consumer;
    uint32_t next_message_sequence;
    uint64_t next_tick;
} test_fixture;

typedef struct transport_snapshot_s {
    worr_native_rx_session_v1 session;
    worr_native_rx_slot_v1 slots[TEST_RX_CAPACITY];
    worr_native_carrier_ack_ledger_v1 ledger;
} transport_snapshot;

typedef struct staged_message_s {
    worr_native_rx_message_v1 message;
    uint8_t first_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t first_packet_bytes;
    uint16_t fragment_count;
} staged_message;

static bool snapshot_id_equal(worr_snapshot_id_v2 left,
                              worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch &&
           left.sequence == right.sequence;
}

static worr_snapshot_entity_generation_v2 make_generation(
    uint32_t index, uint32_t generation)
{
    worr_snapshot_entity_generation_v2 value;

    memset(&value, 0, sizeof(value));
    value.identity.index = index;
    value.identity.generation = generation;
    value.provenance_flags =
        WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    return value;
}

static worr_snapshot_player_v2 make_player(uint32_t sequence)
{
    worr_snapshot_player_v2 player;
    uint32_t index;

    memset(&player, 0, sizeof(player));
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.controlled_entity = make_generation(1, 4);
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    player.movement.origin[0] = 10.0f + (float)sequence;
    player.movement.velocity[1] = -2.0f;
    player.movement.movement_flags = 5;
    player.movement.movement_time_ms = 16;
    player.movement.gravity = 800;
    player.movement.view_height = 22;
    player.view_angles[1] = 90.0f;
    player.gun_index = 7;
    player.gun_frame = (uint16_t)(10u + sequence);
    player.fov = 100.0f;
    for (index = 0; index < WORR_SNAPSHOT_STATS_CAPACITY; ++index)
        player.stats[index] = (int16_t)(index + sequence);
    return player;
}

static worr_snapshot_entity_v2 make_entity(
    uint32_t index, uint32_t generation, uint32_t sequence)
{
    worr_snapshot_entity_v2 entity;

    memset(&entity, 0, sizeof(entity));
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.generation = make_generation(index, generation);
    entity.component_mask =
        WORR_SNAPSHOT_ENTITY_TRANSFORM |
        WORR_SNAPSHOT_ENTITY_INTERPOLATION |
        WORR_SNAPSHOT_ENTITY_MODELS |
        WORR_SNAPSHOT_ENTITY_ANIMATION |
        WORR_SNAPSHOT_ENTITY_APPEARANCE |
        WORR_SNAPSHOT_ENTITY_EFFECTS |
        WORR_SNAPSHOT_ENTITY_COLLISION;
    entity.origin[0] = (float)(index * 10u + sequence);
    entity.angles[1] = (float)(sequence * 5u);
    entity.old_origin[0] = entity.origin[0] - 1.0f;
    entity.model_index[0] = (uint16_t)(index + 2u);
    entity.frame = (uint16_t)sequence;
    entity.skin = index;
    entity.solid = 1;
    entity.effects = sequence;
    entity.alpha = 1.0f;
    entity.scale = 1.0f;
    entity.owner.index = WORR_EVENT_NO_ENTITY;
    entity.old_frame = (int32_t)sequence - 1;
    return entity;
}

static bool source_store_init(source_store *source)
{
    memset(source, 0, sizeof(*source));
    return Worr_SnapshotStoreInitV2(
               &source->store, source->slots, TEST_SOURCE_SLOTS,
               source->entities,
               TEST_SOURCE_SLOTS * TEST_ENTITIES_PER_SLOT,
               TEST_ENTITIES_PER_SLOT, source->area,
               TEST_SOURCE_SLOTS * TEST_AREA_PER_SLOT,
               TEST_AREA_PER_SLOT, source->events,
               TEST_SOURCE_SLOTS * TEST_EVENTS_PER_SLOT,
               TEST_EVENTS_PER_SLOT, TEST_MAX_ENTITIES) ==
           WORR_SNAPSHOT_STORE_OK;
}

static bool source_publish(
    source_store *source, uint32_t sequence, bool keyframe,
    worr_snapshot_ref_v2 *ref_out)
{
    worr_snapshot_v2 snapshot;
    worr_snapshot_player_v2 player = make_player(sequence);
    worr_snapshot_entity_v2 entities[2];
    uint8_t area[2];
    worr_snapshot_event_ref_v2 event;
    worr_snapshot_store_publish_v2 publication;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    snapshot.flags = WORR_SNAPSHOT_FLAG_COMPLETE |
                     WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS |
                     WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
    if (keyframe)
        snapshot.flags |= WORR_SNAPSHOT_FLAG_KEYFRAME;
    snapshot.snapshot_id.epoch = TEST_SNAPSHOT_EPOCH;
    snapshot.snapshot_id.sequence = sequence;
    if (!keyframe) {
        snapshot.base_id.epoch = TEST_SNAPSHOT_EPOCH;
        snapshot.base_id.sequence = sequence - 1u;
    }
    snapshot.server_tick = 100u + sequence;
    snapshot.server_time_us =
        UINT64_C(1000000) + (uint64_t)sequence * UINT64_C(16000);
    snapshot.controlled_entity = make_generation(1, 4);
    snapshot.consumed_command.cursor.epoch = 2;
    snapshot.consumed_command.cursor.contiguous_sequence =
        40u + sequence;
    snapshot.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    if (keyframe && sequence == 1u) {
        snapshot.discontinuity.flags =
            WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
            WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
        snapshot.discontinuity.reason =
            WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
    } else if (keyframe) {
        snapshot.discontinuity.flags =
            WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT |
            WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC;
        snapshot.discontinuity.reason =
            WORR_SNAPSHOT_DISCONTINUITY_REASON_HARD_RESYNC;
        snapshot.discontinuity.previous.epoch =
            TEST_SNAPSHOT_EPOCH;
        snapshot.discontinuity.previous.sequence = sequence - 1u;
        snapshot.discontinuity.server_tick_delta = 1;
    } else {
        snapshot.discontinuity.previous = snapshot.base_id;
        snapshot.discontinuity.server_tick_delta = 1;
    }

    entities[0] = make_entity(1, 4, sequence);
    entities[1] = make_entity(2, 7, sequence);
    area[0] = (uint8_t)sequence;
    area[1] = (uint8_t)(sequence ^ 0x5au);
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    event.provenance = WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
    event.carrier_ordinal = 0;
    event.semantic_version = WORR_EVENT_MODEL_REVISION;
    event.authority_id.stream_epoch = 5;
    event.authority_id.sequence = sequence;
    event.semantic_hash =
        UINT64_C(0xabc0000000000000) + sequence;

    memset(&publication, 0, sizeof(publication));
    publication.struct_size = sizeof(publication);
    publication.schema_version = WORR_SNAPSHOT_STORE_VERSION;
    publication.snapshot = &snapshot;
    publication.player = &player;
    publication.entities = entities;
    publication.area_bytes = area;
    publication.event_refs = &event;
    publication.entity_count = 2;
    publication.area_byte_count = 2;
    publication.event_ref_count = 1;
    return Worr_SnapshotStorePublishV2(
               &source->store, &publication, ref_out) ==
           WORR_SNAPSHOT_STORE_OK;
}

static worr_snapshot_projection_view_v2 source_view(
    const source_store *source, worr_snapshot_ref_v2 ref)
{
    worr_snapshot_projection_view_v2 view;
    const size_t entity_offset =
        (size_t)ref.slot * TEST_ENTITIES_PER_SLOT;
    const size_t area_offset =
        (size_t)ref.slot * TEST_AREA_PER_SLOT;
    const size_t event_offset =
        (size_t)ref.slot * TEST_EVENTS_PER_SLOT;

    memset(&view, 0, sizeof(view));
    view.struct_size = sizeof(view);
    view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    view.snapshot = &source->slots[ref.slot].snapshot;
    view.player = &source->slots[ref.slot].player;
    view.entities = source->entities + entity_offset;
    view.area_bytes = source->area + area_offset;
    view.event_refs = source->events + event_offset;
    view.entity_count = view.snapshot->entity_range.count;
    view.area_byte_count = view.snapshot->area_range.count;
    view.event_ref_count = view.snapshot->event_range.count;
    return view;
}

static void fake_reset(void *opaque, uint32_t snapshot_epoch,
                       uint32_t reason, uint64_t host_time_us)
{
    fake_consumer *fake = (fake_consumer *)opaque;
    const uint64_t admission_generation =
        fake->status.admission_generation;
    const uint64_t resets = fake->status.resets + 1u;

    (void)reason;
    (void)host_time_us;
    ++fake->reset_calls;
    memset(&fake->status, 0, sizeof(fake->status));
    fake->status.struct_size = sizeof(fake->status);
    fake->status.api_version =
        WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION;
    fake->status.active_epoch = snapshot_epoch;
    fake->status.resets = resets;
    fake->status.admission_generation = admission_generation;
    fake->status.timeline.struct_size =
        sizeof(fake->status.timeline);
    fake->status.timeline.schema_version =
        WORR_SNAPSHOT_TIMELINE_VERSION;
}

static bool fake_consume(
    void *opaque, const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_projection_hashes_v2 *hashes,
    uint64_t receive_time_us)
{
    fake_consumer *fake = (fake_consumer *)opaque;
    worr_snapshot_projection_hashes_v2 computed;

    ++fake->consume_calls;
    ++fake->status.consume_attempts;
    if (fake->reject_consume)
        return false;
    if (!Worr_SnapshotProjectionHashesV2(
            view, TEST_MAX_ENTITIES, &computed) ||
        memcmp(&computed, hashes, sizeof(computed)) != 0 ||
        view->snapshot->snapshot_id.epoch !=
            fake->status.active_epoch) {
        ++fake->status.rejected;
        return false;
    }

    ++fake->status.accepted;
    ++fake->status.timeline.publish_count;
    fake->status.last_receive_time_us = receive_time_us;
    fake->status.last_endpoint_hash = hashes->endpoint_hash;
    fake->status.last_legacy_parity_hash =
        hashes->legacy_parity_hash;
    fake->status.last_snapshot_id = view->snapshot->snapshot_id;
    fake->status.last_snapshot_hash = view->snapshot->snapshot_hash;
    fake->status.receipt_flags =
        WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED;
    fake->status.last_event_fence_result =
        fake->reject_event_fence
            ? WORR_CGAME_EVENT_RUNTIME_CONFLICT
            : WORR_CGAME_EVENT_RUNTIME_OK;
    if (!fake->reject_event_fence) {
        fake->status.receipt_flags |=
            WORR_CGAME_SNAPSHOT_RECEIPT_EVENT_FENCE_ACCEPTED;
        ++fake->status.admission_generation;
    }
    return true;
}

static bool fake_get_status(
    void *opaque, worr_cgame_snapshot_timeline_status_v2 *status_out)
{
    fake_consumer *fake = (fake_consumer *)opaque;

    ++fake->status_calls;
    if (status_out == NULL)
        return false;
    *status_out = fake->status;
    return true;
}

static worr_native_session_binding_v1 make_binding(void)
{
    worr_native_session_binding_v1 binding;

    memset(&binding, 0, sizeof(binding));
    binding.struct_size = sizeof(binding);
    binding.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    binding.transport_epoch = TEST_TRANSPORT_EPOCH;
    binding.negotiated_capabilities =
        WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK;
    binding.connection_owner_id = TEST_OWNER_ID;
    return binding;
}

static int fixture_init(test_fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->binding = make_binding();
    CHECK(Worr_NativeSessionBindingValidateV1(&fixture->binding));
    CHECK(Worr_NativeSnapshotAdmissionInitV1(
        &fixture->admission, &fixture->binding));
    CHECK(Worr_NativeRxSessionInitV1(
        &fixture->session, fixture->slots, TEST_RX_CAPACITY,
        TEST_PAYLOAD_STRIDE, 1000, 1000, &fixture->binding));
    CHECK(Worr_NativeCarrierAckLedgerInitV1(
        &fixture->ledger, &fixture->binding, 3));

    fixture->scratch.struct_size = sizeof(fixture->scratch);
    fixture->scratch.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    fixture->scratch.snapshot = &fixture->decoded_snapshot;
    fixture->scratch.player = &fixture->decoded_player;
    fixture->scratch.entities = fixture->decoded_entities;
    fixture->scratch.area_bytes = fixture->decoded_area;
    fixture->scratch.event_refs = fixture->decoded_events;
    fixture->scratch.entity_capacity =
        TEST_ENTITIES_PER_SLOT;
    fixture->scratch.area_capacity = TEST_AREA_PER_SLOT;
    fixture->scratch.event_ref_capacity =
        TEST_EVENTS_PER_SLOT;

    fixture->consumer.struct_size = sizeof(fixture->consumer);
    fixture->consumer.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    fixture->consumer.opaque = &fixture->fake;
    fixture->consumer.Reset = fake_reset;
    fixture->consumer.ConsumeCanonicalSnapshot = fake_consume;
    fixture->consumer.GetStatus = fake_get_status;
    fixture->next_message_sequence = 1;
    fixture->next_tick = 1;
    return 0;
}

static void capture_transport(const test_fixture *fixture,
                              transport_snapshot *snapshot)
{
    snapshot->session = fixture->session;
    memcpy(snapshot->slots, fixture->slots, sizeof(snapshot->slots));
    snapshot->ledger = fixture->ledger;
}

static bool transport_equal(const test_fixture *fixture,
                            const transport_snapshot *snapshot)
{
    return memcmp(&fixture->session, &snapshot->session,
                  sizeof(snapshot->session)) == 0 &&
           memcmp(fixture->slots, snapshot->slots,
                  sizeof(snapshot->slots)) == 0 &&
           memcmp(&fixture->ledger, &snapshot->ledger,
                  sizeof(snapshot->ledger)) == 0;
}

static int wrap_datagram(
    const test_fixture *fixture, const void *datagram,
    size_t datagram_bytes, uint8_t *packet_out,
    size_t *packet_bytes_out)
{
    worr_native_carrier_entry_v1 entry;

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entry.data_bytes = (uint32_t)datagram_bytes;
    CHECK(Worr_NativeCarrierEncodeV1(
              fixture->binding.transport_epoch, NULL, 0,
              datagram, datagram_bytes, &entry, 1, packet_out,
              WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
              packet_bytes_out) == WORR_NATIVE_CARRIER_OK);
    return 0;
}

static int encode_view(
    const worr_snapshot_projection_view_v2 *view,
    uint8_t *encoded, size_t encoded_capacity, size_t *encoded_bytes_out,
    worr_native_record_ref_v1 *record_out,
    worr_native_snapshot_expectation_v1 *expectation_out)
{
    worr_native_codec_info_v1 info;

    CHECK(Worr_NativeCodecSnapshotEncodeV1(
              view, TEST_MAX_ENTITIES, encoded, encoded_capacity,
              encoded_bytes_out) == WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInspectV1(
              encoded, *encoded_bytes_out, &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, record_out));
    memset(expectation_out, 0, sizeof(*expectation_out));
    expectation_out->struct_size = sizeof(*expectation_out);
    expectation_out->schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    expectation_out->snapshot_id = view->snapshot->snapshot_id;
    CHECK(Worr_SnapshotProjectionHashesV2(
        view, TEST_MAX_ENTITIES, &expectation_out->hashes));
    return 0;
}

static int stage_encoded(
    test_fixture *fixture, const void *encoded, uint32_t encoded_bytes,
    worr_native_record_ref_v1 record, staged_message *staged)
{
    worr_native_envelope_fragmenter_v1 fragmenter;
    uint8_t datagrams[TEST_MAX_FRAGMENTS]
                     [WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t datagram_bytes[TEST_MAX_FRAGMENTS];
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes;
    worr_native_rx_result_v1 rx_result;
    worr_native_rx_message_v1 message;
    const uint32_t message_sequence =
        fixture->next_message_sequence++;
    uint16_t fragment_count = 0;
    uint16_t index;

    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, fixture->binding.transport_epoch,
        message_sequence, record, 2, encoded, encoded_bytes,
        WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES + 160u));
    CHECK(fragmenter.fragment_count > 1 &&
          fragmenter.fragment_count <= TEST_MAX_FRAGMENTS);
    while ((fragmenter.state_flags &
            WORR_NATIVE_FRAGMENTER_EXHAUSTED) == 0) {
        CHECK(fragment_count < TEST_MAX_FRAGMENTS);
        CHECK(Worr_NativeEnvelopeFragmentNextV1(
                  &fragmenter, encoded, encoded_bytes,
                  datagrams[fragment_count],
                  sizeof(datagrams[fragment_count]),
                  &datagram_bytes[fragment_count]) ==
              WORR_NATIVE_ENVELOPE_EMIT_OK);
        ++fragment_count;
    }

    memset(staged, 0, sizeof(*staged));
    staged->fragment_count = fragment_count;
    for (index = fragment_count; index != 0; --index) {
        const uint16_t fragment_index = index - 1u;

        CHECK(wrap_datagram(
                  fixture, datagrams[fragment_index],
                  datagram_bytes[fragment_index], packet,
                  &packet_bytes) == 0);
        if (fragment_index == 0) {
            memcpy(staged->first_packet, packet, packet_bytes);
            staged->first_packet_bytes = packet_bytes;
        }
        memset(&message, 0xa5, sizeof(message));
        rx_result = WORR_NATIVE_RX_INVALID_STATE;
        CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
                  &fixture->session, fixture->slots, TEST_RX_CAPACITY,
                  fixture->arena, sizeof(fixture->arena),
                  fixture->next_tick++, packet, packet_bytes, 0,
                  &fixture->ledger, &rx_result, &message) ==
              WORR_NATIVE_CARRIER_SESSION_OK);
        if (fragment_index == 0) {
            CHECK(rx_result == WORR_NATIVE_RX_MESSAGE_COMPLETE);
            staged->message = message;
        } else {
            CHECK(rx_result == WORR_NATIVE_RX_FRAGMENT_ACCEPTED);
        }
    }
    CHECK(staged->first_packet_bytes != 0);
    return 0;
}

static worr_native_snapshot_admission_result_v1 admit(
    test_fixture *fixture, const staged_message *staged,
    const worr_native_snapshot_expectation_v1 *expectation,
    uint64_t receive_time_us)
{
    return Worr_NativeSnapshotAdmissionCommitCompletedV1(
        &fixture->admission, &fixture->binding, &fixture->session,
        fixture->slots, TEST_RX_CAPACITY, fixture->arena,
        sizeof(fixture->arena), &fixture->ledger, &staged->message,
        TEST_MAX_ENTITIES, receive_time_us, &fixture->scratch,
        expectation, &fixture->consumer);
}

static int test_transaction_and_repeat(void)
{
    source_store source;
    test_fixture fixture;
    transport_snapshot before;
    worr_snapshot_ref_v2 ref;
    worr_snapshot_projection_view_v2 view;
    worr_native_snapshot_expectation_v1 expectation;
    worr_native_record_ref_v1 record;
    worr_native_rx_result_v1 rx_result;
    worr_native_rx_message_v1 message;
    staged_message staged;
    uint8_t encoded[TEST_PAYLOAD_STRIDE];
    size_t encoded_bytes;
    const uint64_t receive_time_us = UINT64_C(9000000);

    CHECK(source_store_init(&source));
    CHECK(fixture_init(&fixture) == 0);
    CHECK(source_publish(&source, 1, true, &ref));
    view = source_view(&source, ref);
    CHECK(encode_view(
              &view, encoded, sizeof(encoded), &encoded_bytes,
              &record, &expectation) == 0);
    CHECK(stage_encoded(
              &fixture, encoded, (uint32_t)encoded_bytes,
              record, &staged) == 0);

    capture_transport(&fixture, &before);
    CHECK(Worr_NativeCarrierSessionCommitRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              staged.message.slot_index,
              staged.message.message_sequence, &fixture.ledger) ==
          WORR_NATIVE_RX_SEMANTIC_ADMISSION_REQUIRED);
    CHECK(transport_equal(&fixture, &before));

    CHECK(admit(
              &fixture, &staged, &expectation, receive_time_us) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_ACCEPTED);
    CHECK(Worr_NativeSnapshotAdmissionValidateV1(
        &fixture.admission));
    CHECK(snapshot_id_equal(
        fixture.admission.last_accepted_snapshot,
        expectation.snapshot_id));
    CHECK(fixture.admission.last_snapshot_hash ==
          view.snapshot->snapshot_hash);
    CHECK(fixture.fake.status.admission_generation == 1);
    CHECK(fixture.session.committed_snapshot_epoch ==
          expectation.snapshot_id.epoch);
    CHECK(fixture.session.committed_snapshot_sequence ==
          expectation.snapshot_id.sequence);
    CHECK(fixture.ledger.receipt_count == 1);

    capture_transport(&fixture, &before);
    memset(&message, 0xa5, sizeof(message));
    rx_result = WORR_NATIVE_RX_INVALID_STATE;
    CHECK(Worr_NativeCarrierSessionAcceptDataRetainedV1(
              &fixture.session, fixture.slots, TEST_RX_CAPACITY,
              fixture.arena, sizeof(fixture.arena),
              fixture.next_tick++, staged.first_packet,
              staged.first_packet_bytes, 0, &fixture.ledger,
              &rx_result, &message) ==
          WORR_NATIVE_CARRIER_SESSION_SEMANTIC_REVALIDATION_REQUIRED);
    CHECK(transport_equal(&fixture, &before));
    CHECK(Worr_NativeSnapshotAdmissionRevalidateCommittedRepeatV1(
              &fixture.admission, &fixture.binding, &fixture.session,
              fixture.slots, TEST_RX_CAPACITY, fixture.arena,
              sizeof(fixture.arena), fixture.next_tick++,
              staged.first_packet, staged.first_packet_bytes, 0,
              &fixture.ledger, &fixture.consumer) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_REPEAT_REVALIDATED);
    CHECK(fixture.fake.consume_calls == 1);
    CHECK(fixture.ledger.telemetry.repeat_refreshes == 1);

    capture_transport(&fixture, &before);
    fixture.fake.status.last_snapshot_hash ^= UINT64_C(1);
    CHECK(Worr_NativeSnapshotAdmissionRevalidateCommittedRepeatV1(
              &fixture.admission, &fixture.binding, &fixture.session,
              fixture.slots, TEST_RX_CAPACITY, fixture.arena,
              sizeof(fixture.arena), fixture.next_tick++,
              staged.first_packet, staged.first_packet_bytes, 0,
              &fixture.ledger, &fixture.consumer) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(memcmp(&fixture.session, &before.session,
                 sizeof(before.session)) == 0);
    CHECK(memcmp(fixture.slots, before.slots,
                 sizeof(before.slots)) == 0);
    CHECK(fixture.ledger.receipt_count == 0);
    CHECK((fixture.admission.state_flags &
           (WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME |
            WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED)) ==
          (WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME |
           WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED));
    CHECK(fixture.admission.consumer_resyncs == 1);
    CHECK(fixture.fake.reset_calls == 2);
    return 0;
}

static int test_full_update_parity_and_recovery(void)
{
    source_store source;
    test_fixture fixture;
    transport_snapshot before;
    worr_snapshot_ref_v2 ref;
    worr_snapshot_projection_view_v2 view;
    worr_native_snapshot_expectation_v1 expectation;
    worr_native_snapshot_expectation_v1 mismatch;
    worr_native_record_ref_v1 record;
    staged_message staged;
    uint8_t encoded[TEST_PAYLOAD_STRIDE];
    size_t encoded_bytes;
    uint64_t receive_time_us = UINT64_C(10000000);

    CHECK(source_store_init(&source));
    CHECK(fixture_init(&fixture) == 0);

    CHECK(source_publish(&source, 1, true, &ref));
    view = source_view(&source, ref);
    CHECK(encode_view(
              &view, encoded, sizeof(encoded), &encoded_bytes,
              &record, &expectation) == 0);
    CHECK(stage_encoded(
              &fixture, encoded, (uint32_t)encoded_bytes,
              record, &staged) == 0);
    CHECK(admit(
              &fixture, &staged, &expectation, receive_time_us++) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_ACCEPTED);

    CHECK(source_publish(&source, 2, false, &ref));
    view = source_view(&source, ref);
    CHECK(encode_view(
              &view, encoded, sizeof(encoded), &encoded_bytes,
              &record, &expectation) == 0);
    CHECK(stage_encoded(
              &fixture, encoded, (uint32_t)encoded_bytes,
              record, &staged) == 0);
    mismatch = expectation;
    mismatch.hashes.endpoint_hash ^= UINT64_C(1);
    CHECK(admit(
              &fixture, &staged, &mismatch, receive_time_us++) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_FULL_UPDATE_ACCEPTED);
    CHECK(fixture.admission.accepted_snapshots == 2 &&
          fixture.admission.accepted_full_updates == 1);

    CHECK(source_publish(&source, 3, false, &ref));
    view = source_view(&source, ref);
    CHECK(encode_view(
              &view, encoded, sizeof(encoded), &encoded_bytes,
              &record, &expectation) == 0);
    CHECK(stage_encoded(
              &fixture, encoded, (uint32_t)encoded_bytes,
              record, &staged) == 0);
    mismatch = expectation;
    mismatch.hashes.semantic_entity_hash ^= UINT64_C(1);
    capture_transport(&fixture, &before);
    CHECK(admit(
              &fixture, &staged, &mismatch, receive_time_us++) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_PARITY_MISMATCH);
    CHECK(transport_equal(&fixture, &before));
    CHECK((fixture.admission.state_flags &
           WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME) != 0);
    CHECK(admit(
              &fixture, &staged, &expectation, receive_time_us++) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_REQUIRED);
    CHECK(transport_equal(&fixture, &before));

    CHECK(source_publish(&source, 4, true, &ref));
    view = source_view(&source, ref);
    CHECK(encode_view(
              &view, encoded, sizeof(encoded), &encoded_bytes,
              &record, &expectation) == 0);
    CHECK(stage_encoded(
              &fixture, encoded, (uint32_t)encoded_bytes,
              record, &staged) == 0);
    CHECK(admit(
              &fixture, &staged, &expectation, receive_time_us++) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_KEYFRAME_ACCEPTED);
    CHECK((fixture.admission.state_flags &
           (WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME |
            WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED)) == 0);
    return 0;
}

static int test_cross_endpoint_expectation_rejection_matrix(void)
{
    enum {
        MISMATCH_SNAPSHOT_ID,
        MISMATCH_LEGACY_PARITY,
        MISMATCH_SEMANTIC_PLAYER,
        MISMATCH_SEMANTIC_ENTITY,
        MISMATCH_SEMANTIC_AREA,
        MISMATCH_SEMANTIC_EVENT,
        MISMATCH_COUNT
    };
    int mismatch_kind;

    for (mismatch_kind = 0; mismatch_kind < MISMATCH_COUNT;
         ++mismatch_kind) {
        source_store source;
        test_fixture fixture;
        transport_snapshot before;
        worr_snapshot_ref_v2 ref;
        worr_snapshot_projection_view_v2 view;
        worr_native_snapshot_expectation_v1 expectation;
        worr_native_snapshot_expectation_v1 mismatch;
        worr_native_record_ref_v1 record;
        staged_message staged;
        uint8_t encoded[TEST_PAYLOAD_STRIDE];
        size_t encoded_bytes;

        CHECK(source_store_init(&source));
        CHECK(fixture_init(&fixture) == 0);
        CHECK(source_publish(&source, 1, true, &ref));
        view = source_view(&source, ref);
        CHECK(encode_view(
                  &view, encoded, sizeof(encoded), &encoded_bytes,
                  &record, &expectation) == 0);
        CHECK(stage_encoded(
                  &fixture, encoded, (uint32_t)encoded_bytes,
                  record, &staged) == 0);

        mismatch = expectation;
        switch (mismatch_kind) {
        case MISMATCH_SNAPSHOT_ID:
            ++mismatch.snapshot_id.sequence;
            break;
        case MISMATCH_LEGACY_PARITY:
            mismatch.hashes.legacy_parity_hash ^= UINT64_C(1);
            break;
        case MISMATCH_SEMANTIC_PLAYER:
            mismatch.hashes.semantic_player_hash ^= UINT64_C(1);
            break;
        case MISMATCH_SEMANTIC_ENTITY:
            mismatch.hashes.semantic_entity_hash ^= UINT64_C(1);
            break;
        case MISMATCH_SEMANTIC_AREA:
            mismatch.hashes.semantic_area_hash ^= UINT64_C(1);
            break;
        case MISMATCH_SEMANTIC_EVENT:
            mismatch.hashes.semantic_event_hash ^= UINT64_C(1);
            break;
        default:
            CHECK(false);
        }

        capture_transport(&fixture, &before);
        CHECK(admit(
                  &fixture, &staged, &mismatch,
                  UINT64_C(10500000) +
                      (uint64_t)mismatch_kind) ==
              WORR_NATIVE_SNAPSHOT_ADMISSION_PARITY_MISMATCH);
        CHECK(transport_equal(&fixture, &before));
        CHECK(fixture.admission.parity_rejections == 1);
        CHECK((fixture.admission.state_flags &
               WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME) != 0);
        CHECK(fixture.fake.consume_calls == 0);
        CHECK(fixture.ledger.receipt_count == 0);
    }
    return 0;
}

static int test_event_fence_fail_closed(void)
{
    source_store source;
    test_fixture fixture;
    transport_snapshot before;
    worr_snapshot_ref_v2 ref;
    worr_snapshot_projection_view_v2 view;
    worr_native_snapshot_expectation_v1 expectation;
    worr_native_record_ref_v1 record;
    staged_message staged;
    uint8_t encoded[TEST_PAYLOAD_STRIDE];
    size_t encoded_bytes;

    CHECK(source_store_init(&source));
    CHECK(fixture_init(&fixture) == 0);
    CHECK(source_publish(&source, 1, true, &ref));
    view = source_view(&source, ref);
    CHECK(encode_view(
              &view, encoded, sizeof(encoded), &encoded_bytes,
              &record, &expectation) == 0);
    CHECK(stage_encoded(
              &fixture, encoded, (uint32_t)encoded_bytes,
              record, &staged) == 0);
    fixture.fake.reject_event_fence = true;
    capture_transport(&fixture, &before);
    CHECK(admit(
              &fixture, &staged, &expectation,
              UINT64_C(11000000)) ==
          WORR_NATIVE_SNAPSHOT_ADMISSION_RESYNC_UNCOMMITTED);
    CHECK(memcmp(&fixture.session, &before.session,
                 sizeof(before.session)) == 0);
    CHECK(memcmp(fixture.slots, before.slots,
                 sizeof(before.slots)) == 0);
    CHECK(fixture.ledger.receipt_count == 0);
    CHECK((fixture.admission.state_flags &
           (WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME |
            WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED)) ==
          (WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME |
           WORR_NATIVE_SNAPSHOT_ADMISSION_QUARANTINED));
    CHECK(fixture.fake.reset_calls == 2);
    return 0;
}

int main(void)
{
    CHECK(test_transaction_and_repeat() == 0);
    CHECK(test_full_update_parity_and_recovery() == 0);
    CHECK(test_cross_endpoint_expectation_rejection_matrix() == 0);
    CHECK(test_event_fence_fail_closed() == 0);
    puts("native_snapshot_admission_test: ok");
    return 0;
}
