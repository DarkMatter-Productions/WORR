/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* Transport-neutral canonical snapshot receiver tests (FR-10-T04/T06). */

#include "common/net/native_snapshot_receiver.h"

#include "common/net/native_carrier.h"
#include "common/net/snapshot_store.h"
#include "shared/cgame_event_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "native_snapshot_receiver_test:%d: %s\n",     \
                    __LINE__, #condition);                                 \
            return false;                                                  \
        }                                                                  \
    } while (0)

enum {
    TEST_MAX_ENTITIES = 64,
    TEST_SOURCE_SLOTS = 8,
    TEST_ENTITIES_PER_SLOT = 2,
    TEST_AREA_PER_SLOT = 8,
    TEST_EVENTS_PER_SLOT = 2,
    TEST_MAX_PACKETS = 32,
    TEST_FRAGMENT_PAYLOAD_BYTES = 128,
};

#define TEST_OWNER_ID UINT64_C(0x15263748596a7b8c)
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
    uint32_t max_entities;
    worr_snapshot_ref_v2 latest_ref;
} source_store;

typedef struct fake_consumer_s {
    worr_cgame_snapshot_timeline_status_v2 status;
    uint32_t reset_calls;
    uint32_t consume_calls;
    uint32_t status_calls;
    uint32_t max_entities;
} fake_consumer;

typedef struct receiver_fixture_s {
    worr_native_session_binding_v1 binding;
    fake_consumer fake;
    worr_native_snapshot_consumer_v1 consumer;
    uint64_t next_tick;
} receiver_fixture;

typedef struct wire_snapshot_s {
    uint32_t message_sequence;
    uint16_t packet_count;
    uint16_t reserved0;
    size_t packet_bytes[TEST_MAX_PACKETS];
    uint8_t packets[TEST_MAX_PACKETS]
                   [WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    worr_native_snapshot_expectation_v1 expectation;
} wire_snapshot;

/*
 * The receiver owns bounded decode and reassembly arenas that are deliberately
 * much larger than a normal test stack frame.  Keep the owner and wire scratch
 * in static storage so this console-only test is reliable on Windows too.
 */
static worr_native_snapshot_receiver_v1 receiver;
static uint8_t encoded_scratch[WORR_NATIVE_CODEC_MAX_ENCODED_BYTES];
static wire_snapshot wire_a;
static wire_snapshot wire_b;
static wire_snapshot wire_c;

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

static bool source_store_init_with_limit(
    source_store *source, uint32_t max_entities)
{
    memset(source, 0, sizeof(*source));
    source->max_entities = max_entities;
    return max_entities > 2 &&
           Worr_SnapshotStoreInitV2(
               &source->store, source->slots, TEST_SOURCE_SLOTS,
               source->entities,
               TEST_SOURCE_SLOTS * TEST_ENTITIES_PER_SLOT,
               TEST_ENTITIES_PER_SLOT, source->area,
               TEST_SOURCE_SLOTS * TEST_AREA_PER_SLOT,
                TEST_AREA_PER_SLOT, source->events,
                TEST_SOURCE_SLOTS * TEST_EVENTS_PER_SLOT,
               TEST_EVENTS_PER_SLOT, max_entities) ==
           WORR_SNAPSHOT_STORE_OK;
}

static bool source_store_init(source_store *source)
{
    return source_store_init_with_limit(
        source, TEST_MAX_ENTITIES);
}

static bool source_publish(
    source_store *source, uint32_t snapshot_epoch,
    uint32_t sequence, bool keyframe, worr_snapshot_ref_v2 *ref_out)
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
    snapshot.snapshot_id.epoch = snapshot_epoch;
    snapshot.snapshot_id.sequence = sequence;
    if (!keyframe) {
        snapshot.base_id.epoch = snapshot_epoch;
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
        snapshot.discontinuity.previous.epoch = snapshot_epoch;
        snapshot.discontinuity.previous.sequence = sequence - 1u;
        snapshot.discontinuity.server_tick_delta = 1;
    } else {
        snapshot.discontinuity.previous = snapshot.base_id;
        snapshot.discontinuity.server_tick_delta = 1;
    }

    entities[0] = make_entity(1, 4, sequence);
    entities[1] = make_entity(
        source->max_entities - 1u, 7, sequence);
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
    if (Worr_SnapshotStorePublishV2(
            &source->store, &publication, ref_out) !=
        WORR_SNAPSHOT_STORE_OK) {
        return false;
    }
    source->latest_ref = *ref_out;
    return true;
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
    if (!Worr_SnapshotProjectionHashesV2(
            view, fake->max_entities, &computed) ||
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
    fake->status.last_snapshot_hash =
        view->snapshot->snapshot_hash;
    fake->status.receipt_flags =
        WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED |
        WORR_CGAME_SNAPSHOT_RECEIPT_EVENT_FENCE_ACCEPTED;
    fake->status.last_event_fence_result =
        WORR_CGAME_EVENT_RUNTIME_OK;
    ++fake->status.admission_generation;
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

static bool fixture_init_with_limit(
    receiver_fixture *fixture, uint32_t max_entities)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->binding = make_binding();
    CHECK(Worr_NativeSessionBindingValidateV1(&fixture->binding));
    fixture->consumer.struct_size = sizeof(fixture->consumer);
    fixture->consumer.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    fixture->consumer.opaque = &fixture->fake;
    fixture->consumer.Reset = fake_reset;
    fixture->consumer.ConsumeCanonicalSnapshot = fake_consume;
    fixture->consumer.GetStatus = fake_get_status;
    fixture->next_tick = 1;
    fixture->fake.max_entities = max_entities;
    CHECK(Worr_NativeSnapshotReceiverInitV1(
        &receiver, &fixture->binding, TEST_SNAPSHOT_EPOCH,
        max_entities, &fixture->consumer));
    CHECK(Worr_NativeSnapshotReceiverValidateV1(&receiver));
    return true;
}

static bool fixture_init(receiver_fixture *fixture)
{
    return fixture_init_with_limit(
        fixture, TEST_MAX_ENTITIES);
}

static bool wrap_datagram(
    const worr_native_session_binding_v1 *binding,
    const void *datagram, size_t datagram_bytes,
    uint8_t *packet_out, size_t *packet_bytes_out)
{
    worr_native_carrier_entry_v1 entry;

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entry.data_bytes = (uint32_t)datagram_bytes;
    CHECK(Worr_NativeCarrierEncodeV1(
              binding->transport_epoch, NULL, 0,
              datagram, datagram_bytes, &entry, 1, packet_out,
              WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
              packet_bytes_out) == WORR_NATIVE_CARRIER_OK);
    return true;
}

static bool build_wire_snapshot(
    source_store *source,
    const worr_native_session_binding_v1 *binding,
    uint32_t snapshot_epoch, uint32_t snapshot_sequence,
    bool keyframe, uint32_t message_sequence,
    wire_snapshot *wire)
{
    worr_snapshot_ref_v2 ref;
    worr_snapshot_projection_view_v2 view;
    worr_native_codec_info_v1 info;
    worr_native_record_ref_v1 record;
    worr_native_envelope_fragmenter_v1 fragmenter;
    uint8_t datagram[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    size_t encoded_bytes;
    size_t datagram_bytes;

    CHECK(source_publish(
        source, snapshot_epoch, snapshot_sequence, keyframe, &ref));
    view = source_view(source, ref);
    CHECK(Worr_NativeCodecSnapshotEncodeV1(
              &view, source->max_entities, encoded_scratch,
              sizeof(encoded_scratch), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInspectV1(
              encoded_scratch, encoded_bytes, &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &record));

    memset(wire, 0, sizeof(*wire));
    wire->message_sequence = message_sequence;
    wire->expectation.struct_size =
        sizeof(wire->expectation);
    wire->expectation.schema_version =
        WORR_NATIVE_SNAPSHOT_ADMISSION_ABI_VERSION;
    wire->expectation.snapshot_id =
        view.snapshot->snapshot_id;
    CHECK(Worr_SnapshotProjectionHashesV2(
        &view, source->max_entities,
        &wire->expectation.hashes));

    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, binding->transport_epoch, message_sequence,
        record, 2, encoded_scratch, encoded_bytes,
        WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES +
            TEST_FRAGMENT_PAYLOAD_BYTES));
    CHECK(fragmenter.fragment_count > 1);
    CHECK(fragmenter.fragment_count <= TEST_MAX_PACKETS);
    while ((fragmenter.state_flags &
            WORR_NATIVE_FRAGMENTER_EXHAUSTED) == 0) {
        CHECK(wire->packet_count < TEST_MAX_PACKETS);
        CHECK(Worr_NativeEnvelopeFragmentNextV1(
                  &fragmenter, encoded_scratch, encoded_bytes,
                  datagram, sizeof(datagram), &datagram_bytes) ==
              WORR_NATIVE_ENVELOPE_EMIT_OK);
        CHECK(wrap_datagram(
            binding, datagram, datagram_bytes,
            wire->packets[wire->packet_count],
            &wire->packet_bytes[wire->packet_count]));
        ++wire->packet_count;
    }
    CHECK(wire->packet_count == fragmenter.fragment_count);
    return true;
}

static bool feed_reordered_with_duplicate(
    receiver_fixture *fixture, const wire_snapshot *wire,
    uint64_t receive_time_us,
    worr_native_snapshot_receiver_result_v1 expected_final)
{
    worr_native_snapshot_receiver_result_v1 result;
    uint16_t index;

    CHECK(wire->packet_count > 1);
    index = wire->packet_count - 1u;
    result = Worr_NativeSnapshotReceiverAcceptDataV1(
        &receiver, fixture->next_tick++, receive_time_us,
        wire->packets[index], wire->packet_bytes[index], 0);
    CHECK(result ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED);
    result = Worr_NativeSnapshotReceiverAcceptDataV1(
        &receiver, fixture->next_tick++, receive_time_us,
        wire->packets[index], wire->packet_bytes[index], 0);
    CHECK(result ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_DUPLICATE);

    while (index != 0) {
        --index;
        result = Worr_NativeSnapshotReceiverAcceptDataV1(
            &receiver, fixture->next_tick++, receive_time_us,
            wire->packets[index], wire->packet_bytes[index], 0);
        if (index == 0)
            CHECK(result == expected_final);
        else
            CHECK(result ==
                  WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED);
    }
    return true;
}

static bool finish_reordered_after_last_fragment(
    receiver_fixture *fixture, const wire_snapshot *wire,
    uint64_t receive_time_us,
    worr_native_snapshot_receiver_result_v1 expected_final)
{
    uint16_t index;

    CHECK(wire->packet_count > 1);
    index = wire->packet_count - 1u;
    while (index != 0) {
        worr_native_snapshot_receiver_result_v1 result;

        --index;
        result = Worr_NativeSnapshotReceiverAcceptDataV1(
            &receiver, fixture->next_tick++, receive_time_us,
            wire->packets[index], wire->packet_bytes[index], 0);
        if (index == 0)
            CHECK(result == expected_final);
        else
            CHECK(result ==
                  WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED);
    }
    return true;
}

static bool finish_in_order_after_first_fragment(
    receiver_fixture *fixture, const wire_snapshot *wire,
    uint64_t receive_time_us,
    worr_native_snapshot_receiver_result_v1 expected_final)
{
    uint16_t index;

    CHECK(wire->packet_count > 1);
    for (index = 1; index < wire->packet_count; ++index) {
        const worr_native_snapshot_receiver_result_v1 result =
            Worr_NativeSnapshotReceiverAcceptDataV1(
                &receiver, fixture->next_tick++, receive_time_us,
                wire->packets[index], wire->packet_bytes[index], 0);

        if (index + 1u == wire->packet_count)
            CHECK(result == expected_final);
        else
            CHECK(result ==
                  WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED);
    }
    return true;
}

static worr_native_carrier_ack_receipt_v1 *find_receipt(
    worr_native_carrier_ack_ledger_v1 *ledger,
    uint32_t message_sequence)
{
    uint16_t index;

    for (index = 0;
         index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY;
         ++index) {
        if ((ledger->receipts[index].state_flags &
             WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED) != 0 &&
            ledger->receipts[index].message_sequence ==
                message_sequence) {
            return &ledger->receipts[index];
        }
    }
    return NULL;
}

static bool all_pending_clear(void)
{
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_RX_SLOT_CAPACITY;
         ++index) {
        if (receiver.pending[index].occupied != 0)
            return false;
    }
    return true;
}

static bool all_expectations_clear(void)
{
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY;
         ++index) {
        if (receiver.expectations[index].occupied != 0)
            return false;
    }
    return true;
}

static uint32_t expectation_count(void)
{
    uint32_t count = 0;
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY;
         ++index) {
        if (receiver.expectations[index].occupied != 0)
            ++count;
    }
    return count;
}

static bool has_expectation(uint32_t sequence)
{
    uint32_t index;

    for (index = 0;
         index < WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY;
         ++index) {
        if (receiver.expectations[index].occupied != 0 &&
            receiver.expectations[index]
                    .expectation.snapshot_id.epoch ==
                TEST_SNAPSHOT_EPOCH &&
            receiver.expectations[index]
                    .expectation.snapshot_id.sequence ==
                sequence) {
            return true;
        }
    }
    return false;
}

static bool test_both_arrival_orders_and_repeat_ack(void)
{
    source_store source;
    receiver_fixture fixture;
    worr_native_carrier_ack_receipt_v1 *latest_receipt;
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_carrier_view_v1 carrier;
    uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t ack_packet_bytes;
    uint64_t handoff_tick = 100;
    uint32_t consume_calls;
    uint32_t handoff;
    worr_native_snapshot_receiver_result_v1 result;

    CHECK(source_store_init(&source));
    CHECK(fixture_init(&fixture));
    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 1,
        true, 1, &wire_a));

    /* DATA may complete first, but transport completion cannot authorize ACK. */
    CHECK(feed_reordered_with_duplicate(
        &fixture, &wire_a, UINT64_C(9000000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_PENDING));
    CHECK(receiver.ack_ledger.receipt_count == 0);
    CHECK(receiver.rx.occupied_count == 1);
    CHECK(fixture.fake.consume_calls == 0);
    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &receiver.ack_ledger, fixture.next_tick, 5) ==
          WORR_NATIVE_CARRIER_ACK_NOT_DUE);
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_a.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED);
    CHECK(receiver.ack_ledger.receipt_count == 1);
    CHECK(fixture.fake.consume_calls == 1);
    CHECK(snapshot_id_equal(
        receiver.admission.last_accepted_snapshot,
        wire_a.expectation.snapshot_id));
    CHECK(find_receipt(
        &receiver.ack_ledger, wire_a.message_sequence) != NULL);

    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 2,
        false, 2, &wire_b));

    /* The independently parity-qualified legacy expectation may arrive first. */
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_b.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED);
    CHECK(receiver.ack_ledger.receipt_count == 1);
    CHECK(feed_reordered_with_duplicate(
        &fixture, &wire_b, UINT64_C(9016000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED));
    CHECK(receiver.ack_ledger.receipt_count == 2);
    CHECK(fixture.fake.consume_calls == 2);
    CHECK(snapshot_id_equal(
        receiver.admission.last_accepted_snapshot,
        wire_b.expectation.snapshot_id));
    latest_receipt = find_receipt(
        &receiver.ack_ledger, wire_b.message_sequence);
    CHECK(latest_receipt != NULL);
    CHECK(latest_receipt->record_class ==
          WORR_NATIVE_RECORD_SNAPSHOT_V1);
    CHECK(latest_receipt->handoffs_remaining ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_ACK_PROACTIVE_HANDOFFS);
    CHECK(receiver.telemetry.snapshots_admitted == 2);
    CHECK(receiver.telemetry.fragment_duplicates == 2);

    /*
     * Model three successfully handed ACK packets being lost downstream.
     * The receipt remains exact but has no proactive credits left.
     */
    for (handoff = 0;
         handoff <
             WORR_NATIVE_SNAPSHOT_RECEIVER_ACK_PROACTIVE_HANDOFFS;
         ++handoff) {
        handoff_tick += 10;
        memset(&token, 0, sizeof(token));
        CHECK(Worr_NativeCarrierAckPreparePacketV1(
                  &receiver.ack_ledger, handoff_tick, 5,
                  WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
                  NULL, 0, ack_packet, sizeof(ack_packet),
                  &ack_packet_bytes, &token) ==
              WORR_NATIVE_CARRIER_ACK_OK);
        CHECK(Worr_NativeCarrierDecodeV1(
                  ack_packet, ack_packet_bytes, &carrier) ==
              WORR_NATIVE_CARRIER_OK);
        CHECK(carrier.entry_count == 1);
        CHECK(carrier.entries[0].entry_type ==
              WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
        CHECK(carrier.entries[0].first_message_sequence == 1);
        CHECK(carrier.entries[0].last_message_sequence == 2);
        CHECK(Worr_NativeCarrierAckCommitHandoffV1(
                  &receiver.ack_ledger, &token, handoff_tick,
                  ack_packet, ack_packet_bytes) ==
              WORR_NATIVE_CARRIER_ACK_OK);
    }
    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &receiver.ack_ledger, handoff_tick + 10u, 5) ==
          WORR_NATIVE_CARRIER_ACK_NOT_DUE);
    latest_receipt = find_receipt(
        &receiver.ack_ledger, wire_b.message_sequence);
    CHECK(latest_receipt != NULL &&
          latest_receipt->handoffs_remaining == 0);

    /*
     * One exact committed DATA fragment proves the sender still needs the ACK.
     * Revalidation must rearm only its receipt and never consume twice.
     */
    consume_calls = fixture.fake.consume_calls;
    result = Worr_NativeSnapshotReceiverAcceptDataV1(
        &receiver, handoff_tick + 11u, UINT64_C(9016000),
        wire_b.packets[0], wire_b.packet_bytes[0], 0);
    CHECK(result ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_REPEAT_REVALIDATED);
    CHECK(fixture.fake.consume_calls == consume_calls);
    CHECK(receiver.telemetry.repeats_revalidated == 1);
    CHECK(receiver.ack_ledger.telemetry.repeat_refreshes == 1);
    latest_receipt = find_receipt(
        &receiver.ack_ledger, wire_b.message_sequence);
    CHECK(latest_receipt != NULL);
    CHECK(latest_receipt->handoffs_remaining ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_ACK_PROACTIVE_HANDOFFS);
    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &receiver.ack_ledger, handoff_tick + 11u, 5) ==
          WORR_NATIVE_CARRIER_ACK_OK);

    memset(&token, 0, sizeof(token));
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &receiver.ack_ledger, handoff_tick + 11u, 5,
              WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
              NULL, 0, ack_packet, sizeof(ack_packet),
              &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(token.range_count == 1);
    CHECK(token.ranges[0].first_message_sequence ==
          wire_b.message_sequence);
    CHECK(token.ranges[0].last_message_sequence ==
          wire_b.message_sequence);
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &receiver.ack_ledger, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeSnapshotReceiverValidateV1(&receiver));
    return true;
}

static bool test_parity_mismatch_quarantines_without_ack(void)
{
    source_store source;
    receiver_fixture fixture;
    worr_native_snapshot_expectation_v1 mismatch;

    CHECK(source_store_init(&source));
    CHECK(fixture_init(&fixture));
    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 1,
        true, 10, &wire_a));
    CHECK(feed_reordered_with_duplicate(
        &fixture, &wire_a, UINT64_C(10000000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_PENDING));
    mismatch = wire_a.expectation;
    mismatch.hashes.semantic_entity_hash ^= UINT64_C(1);
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &mismatch) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_PARITY_MISMATCH);
    CHECK((receiver.state_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) != 0);
    CHECK((receiver.admission.state_flags &
           WORR_NATIVE_SNAPSHOT_ADMISSION_REQUIRE_KEYFRAME) != 0);
    CHECK(receiver.ack_ledger.receipt_count == 0);
    CHECK(fixture.fake.consume_calls == 0);
    CHECK(receiver.telemetry.semantic_rejections == 1);
    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &receiver.ack_ledger, fixture.next_tick, 5) ==
          WORR_NATIVE_CARRIER_ACK_NOT_DUE);
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_a.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED_RESULT);
    CHECK(Worr_NativeSnapshotReceiverValidateV1(&receiver));
    return true;
}

static bool test_wrong_snapshot_epoch_quarantines_without_ack(void)
{
    source_store source;
    receiver_fixture fixture;
    uint16_t index;
    worr_native_snapshot_receiver_result_v1 result =
        WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_STATE;

    CHECK(source_store_init(&source));
    CHECK(fixture_init(&fixture));
    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH + 1u, 1,
        true, 20, &wire_a));
    for (index = 0; index < wire_a.packet_count; ++index) {
        result = Worr_NativeSnapshotReceiverAcceptDataV1(
            &receiver, fixture.next_tick++, UINT64_C(11000000),
            wire_a.packets[index], wire_a.packet_bytes[index], 0);
        if (index + 1u == wire_a.packet_count)
            CHECK(result ==
                  WORR_NATIVE_SNAPSHOT_RECEIVER_WRONG_EPOCH);
        else
            CHECK(result ==
                  WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED);
    }
    CHECK((receiver.state_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) != 0);
    CHECK(receiver.ack_ledger.receipt_count == 0);
    CHECK(fixture.fake.consume_calls == 0);
    CHECK(Worr_NativeCarrierAckPeekDueV1(
              &receiver.ack_ledger, fixture.next_tick, 5) ==
          WORR_NATIVE_CARRIER_ACK_NOT_DUE);
    CHECK(Worr_NativeSnapshotReceiverValidateV1(&receiver));
    return true;
}

static bool test_newer_admission_supersedes_deferred_snapshot(void)
{
    source_store source;
    receiver_fixture fixture;
    uint32_t consume_calls;

    CHECK(source_store_init(&source));
    CHECK(fixture_init(&fixture));
    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 1,
        true, 30, &wire_a));
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_a.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED);
    CHECK(feed_reordered_with_duplicate(
        &fixture, &wire_a, UINT64_C(12000000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED));

    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 2,
        false, 31, &wire_b));
    CHECK(feed_reordered_with_duplicate(
        &fixture, &wire_b, UINT64_C(12016000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_PENDING));
    CHECK(receiver.rx.occupied_count == 1);
    CHECK(!all_pending_clear());

    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 3,
        false, 32, &wire_c));
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_c.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED);
    CHECK(feed_reordered_with_duplicate(
        &fixture, &wire_c, UINT64_C(12032000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED));
    CHECK(snapshot_id_equal(
        receiver.admission.last_accepted_snapshot,
        wire_c.expectation.snapshot_id));
    CHECK(receiver.rx.occupied_count == 0);
    CHECK(receiver.rx.telemetry.superseded_snapshots >= 1);
    CHECK(all_pending_clear());
    CHECK(all_expectations_clear());
    CHECK(find_receipt(
              &receiver.ack_ledger,
              wire_b.message_sequence) == NULL);

    consume_calls = fixture.fake.consume_calls;
    CHECK(Worr_NativeSnapshotReceiverAcceptDataV1(
              &receiver, fixture.next_tick++,
              UINT64_C(12016000), wire_b.packets[0],
              wire_b.packet_bytes[0], 0) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_STALE);
    CHECK(fixture.fake.consume_calls == consume_calls);
    CHECK(find_receipt(
              &receiver.ack_ledger,
              wire_b.message_sequence) == NULL);
    CHECK(Worr_NativeSnapshotReceiverValidateV1(&receiver));
    return true;
}

static bool test_real_entity_domains_keep_fixed_decode_capacity(void)
{
    static const uint32_t domains[] = {1024u, 8192u};
    uint32_t domain_index;

    for (domain_index = 0;
         domain_index < sizeof(domains) / sizeof(domains[0]);
         ++domain_index) {
        source_store source;
        receiver_fixture fixture;
        const uint32_t domain = domains[domain_index];
        worr_snapshot_projection_view_v2 view;

        CHECK(source_store_init_with_limit(&source, domain));
        CHECK(fixture_init_with_limit(&fixture, domain));
        CHECK(receiver.max_entities == domain);
        CHECK(receiver.scratch.entity_capacity ==
              WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES);
        CHECK(build_wire_snapshot(
            &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 1,
            true, 50u + domain_index, &wire_a));
        view = source_view(&source, source.latest_ref);
        CHECK(view.entity_count == 2);
        CHECK(view.entities[1].generation.identity.index ==
              domain - 1u);
        CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
                  &receiver, &wire_a.expectation) ==
              WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED);
        CHECK(feed_reordered_with_duplicate(
            &fixture, &wire_a,
            UINT64_C(14000000) + domain_index,
            WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED));
        CHECK(fixture.fake.consume_calls == 1);
        CHECK(receiver.ack_ledger.receipt_count == 1);
        CHECK(Worr_NativeSnapshotReceiverValidateV1(&receiver));
    }
    return true;
}

static bool test_expectation_window_rolls_without_evicting_active_rx(void)
{
    source_store source;
    receiver_fixture fixture;
    worr_native_snapshot_expectation_v1 observation;
    uint32_t sequence;

    CHECK(source_store_init(&source));
    CHECK(fixture_init(&fixture));
    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 1,
        true, 60, &wire_a));
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_a.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED);
    CHECK(Worr_NativeSnapshotReceiverAcceptDataV1(
              &receiver, fixture.next_tick++, UINT64_C(15000000),
              wire_a.packets[wire_a.packet_count - 1u],
              wire_a.packet_bytes[wire_a.packet_count - 1u], 0) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED);

    observation = wire_a.expectation;
    for (sequence = 2;
         sequence <
             WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY + 8u;
         ++sequence) {
        observation.snapshot_id.sequence = sequence;
        observation.hashes.endpoint_hash =
            wire_a.expectation.hashes.endpoint_hash ^ sequence;
        CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
                  &receiver, &observation) ==
              WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED);
        CHECK((receiver.state_flags &
               WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);
    }
    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH,
        WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY + 8u,
        true, 61, &wire_b));
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_b.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED);
    CHECK(expectation_count() ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY);
    CHECK(has_expectation(1));
    CHECK(has_expectation(
        WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY + 8u));
    CHECK(!has_expectation(2));
    CHECK(receiver.telemetry.capacity_stalls == 0);

    CHECK(finish_reordered_after_last_fragment(
        &fixture, &wire_a, UINT64_C(15000000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED));
    CHECK(fixture.fake.consume_calls == 1);
    CHECK(feed_reordered_with_duplicate(
        &fixture, &wire_b, UINT64_C(15384000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED));
    CHECK(fixture.fake.consume_calls == 2);
    CHECK(snapshot_id_equal(
        receiver.admission.last_accepted_snapshot,
        wire_b.expectation.snapshot_id));
    CHECK((receiver.state_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);
    CHECK(Worr_NativeSnapshotReceiverValidateV1(&receiver));
    return true;
}

static bool test_expired_complete_descriptor_is_reconciled(void)
{
    source_store source;
    receiver_fixture fixture;

    CHECK(source_store_init(&source));
    CHECK(fixture_init(&fixture));
    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 1,
        true, 70, &wire_a));
    CHECK(feed_reordered_with_duplicate(
        &fixture, &wire_a, UINT64_C(16000000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_PENDING));
    CHECK(receiver.rx.occupied_count == 1);
    CHECK(!all_pending_clear());
    CHECK(receiver.ack_ledger.receipt_count == 0);

    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 2,
        true, 71, &wire_b));
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_b.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED);
    fixture.next_tick +=
        WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_TIMEOUT_TICKS + 1u;
    CHECK(Worr_NativeSnapshotReceiverAcceptDataV1(
              &receiver, fixture.next_tick++, UINT64_C(16016000),
              wire_b.packets[0], wire_b.packet_bytes[0], 0) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED);
    CHECK(all_pending_clear());
    CHECK((receiver.state_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);

    CHECK(finish_in_order_after_first_fragment(
        &fixture, &wire_b, UINT64_C(16016000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED));
    CHECK(receiver.rx.occupied_count == 0);
    CHECK(all_pending_clear());
    CHECK(receiver.ack_ledger.receipt_count == 1);
    CHECK(fixture.fake.consume_calls == 1);
    CHECK(snapshot_id_equal(
        receiver.admission.last_accepted_snapshot,
        wire_b.expectation.snapshot_id));
    CHECK((receiver.state_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);
    CHECK(Worr_NativeSnapshotReceiverValidateV1(&receiver));
    return true;
}

static bool test_cancellation_and_active_ack_barrier(void)
{
    source_store source;
    receiver_fixture fixture;
    worr_native_carrier_ack_emit_token_v1 token;
    uint8_t ack_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t ack_packet_bytes;
    uint32_t cancelled_messages;
    uint32_t cancelled_receipts;
    uint64_t mutation_generation;

    CHECK(source_store_init(&source));
    CHECK(fixture_init(&fixture));
    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 1,
        true, 40, &wire_a));
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_a.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED);
    CHECK(Worr_NativeSnapshotReceiverAcceptDataV1(
              &receiver, fixture.next_tick++, UINT64_C(13000000),
              wire_a.packets[wire_a.packet_count - 1u],
              wire_a.packet_bytes[wire_a.packet_count - 1u], 0) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_FRAGMENT_ACCEPTED);
    cancelled_messages = UINT32_MAX;
    cancelled_receipts = UINT32_MAX;
    CHECK(Worr_NativeSnapshotReceiverCancelV1(
              &receiver, &cancelled_messages,
              &cancelled_receipts) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT);
    CHECK(cancelled_messages == 1);
    CHECK(cancelled_receipts == 0);
    CHECK((receiver.state_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED) != 0);
    CHECK(receiver.rx.occupied_count == 0);
    CHECK(receiver.ack_ledger.receipt_count == 0);
    CHECK(all_pending_clear());
    CHECK(all_expectations_clear());
    CHECK(Worr_NativeSnapshotReceiverValidateV1(&receiver));
    CHECK(Worr_NativeSnapshotReceiverAcceptDataV1(
              &receiver, fixture.next_tick++, UINT64_C(13000000),
              wire_a.packets[0], wire_a.packet_bytes[0], 0) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT);
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_a.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT);
    cancelled_messages = UINT32_MAX;
    cancelled_receipts = UINT32_MAX;
    CHECK(Worr_NativeSnapshotReceiverCancelV1(
              &receiver, &cancelled_messages,
              &cancelled_receipts) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT);
    CHECK(cancelled_messages == 0);
    CHECK(cancelled_receipts == 0);

    /*
     * An ACK packet with an unresolved transport outcome is a strict
     * cancellation barrier.  Rejecting that handoff restores a cancellable
     * ledger without spending retry credit.
     */
    CHECK(fixture_init(&fixture));
    CHECK(build_wire_snapshot(
        &source, &fixture.binding, TEST_SNAPSHOT_EPOCH, 1,
        true, 41, &wire_a));
    CHECK(Worr_NativeSnapshotReceiverObserveExpectationV1(
              &receiver, &wire_a.expectation) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CACHED);
    CHECK(feed_reordered_with_duplicate(
        &fixture, &wire_a, UINT64_C(13100000),
        WORR_NATIVE_SNAPSHOT_RECEIVER_SNAPSHOT_ADMITTED));
    memset(&token, 0, sizeof(token));
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &receiver.ack_ledger, 100, 5,
              WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
              NULL, 0, ack_packet, sizeof(ack_packet),
              &ack_packet_bytes, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    mutation_generation = receiver.ack_ledger.mutation_generation;
    cancelled_messages = UINT32_MAX;
    cancelled_receipts = UINT32_MAX;
    CHECK(Worr_NativeSnapshotReceiverCancelV1(
              &receiver, &cancelled_messages,
              &cancelled_receipts) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_INVALID_STATE);
    CHECK(cancelled_messages == UINT32_MAX);
    CHECK(cancelled_receipts == UINT32_MAX);
    CHECK((receiver.state_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED) == 0);
    CHECK(receiver.ack_ledger.receipt_count == 1);
    CHECK(receiver.ack_ledger.mutation_generation ==
          mutation_generation);
    CHECK(Worr_NativeCarrierAckRejectHandoffV1(
              &receiver.ack_ledger, &token) ==
          WORR_NATIVE_CARRIER_ACK_OK);

    cancelled_messages = UINT32_MAX;
    cancelled_receipts = UINT32_MAX;
    CHECK(Worr_NativeSnapshotReceiverCancelV1(
              &receiver, &cancelled_messages,
              &cancelled_receipts) ==
          WORR_NATIVE_SNAPSHOT_RECEIVER_CANCELLED_RESULT);
    CHECK(cancelled_messages == 0);
    CHECK(cancelled_receipts == 1);
    CHECK(Worr_NativeSnapshotReceiverValidateV1(&receiver));
    return true;
}

int main(void)
{
    if (!test_both_arrival_orders_and_repeat_ack() ||
        !test_parity_mismatch_quarantines_without_ack() ||
        !test_wrong_snapshot_epoch_quarantines_without_ack() ||
        !test_newer_admission_supersedes_deferred_snapshot() ||
        !test_real_entity_domains_keep_fixed_decode_capacity() ||
        !test_expectation_window_rolls_without_evicting_active_rx() ||
        !test_expired_complete_descriptor_is_reconciled() ||
        !test_cancellation_and_active_ack_barrier()) {
        return EXIT_FAILURE;
    }
    puts("native_snapshot_receiver_test: ok");
    return EXIT_SUCCESS;
}
