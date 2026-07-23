/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/snapshot_q2proto.h"
#include "common/net/legacy_entity_event_candidate.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

constexpr uint32_t frame_known_flags =
    WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH |
    WORR_SNAPSHOT_Q2PROTO_FRAME_TRANSPORT_TRUNCATED |
    WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_TYPE_VALID |
    WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_FLAGS_VALID |
    WORR_SNAPSHOT_Q2PROTO_FRAME_TEAM_ID_VALID |
    WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID |
    WORR_SNAPSHOT_Q2PROTO_FRAME_FRAGMENT_STALL;

constexpr uint32_t entity_delta_known_bits =
    Q2P_ESD_MODELINDEX | Q2P_ESD_MODELINDEX2 | Q2P_ESD_MODELINDEX3 |
    Q2P_ESD_MODELINDEX4 | Q2P_ESD_FRAME | Q2P_ESD_SKINNUM |
    Q2P_ESD_EFFECTS | Q2P_ESD_EFFECTS_MORE | Q2P_ESD_RENDERFX |
    Q2P_ESD_OLD_ORIGIN | Q2P_ESD_SOUND | Q2P_ESD_LOOP_ATTENUATION |
    Q2P_ESD_LOOP_VOLUME | Q2P_ESD_EVENT | Q2P_ESD_SOLID |
    Q2P_ESD_ALPHA | Q2P_ESD_SCALE;

constexpr uint32_t player_delta_known_bits =
    Q2P_PSD_PM_TYPE | Q2P_PSD_PM_TIME | Q2P_PSD_PM_FLAGS |
    Q2P_PSD_PM_GRAVITY | Q2P_PSD_PM_DELTA_ANGLES |
    Q2P_PSD_PM_VIEWHEIGHT | Q2P_PSD_VIEWOFFSET | Q2P_PSD_KICKANGLES |
    Q2P_PSD_GUNINDEX | Q2P_PSD_GUNSKIN | Q2P_PSD_GUNFRAME |
    Q2P_PSD_FOV | Q2P_PSD_RDFLAGS | Q2P_PSD_GUNRATE |
    Q2P_PSD_CLIENTNUM;

bool capacity_valid(uint32_t slots, uint32_t per_slot, uint32_t total,
                    const void *pointer, size_t element_size)
{
    if (per_slot == 0)
        return total == 0 && pointer == nullptr;
    return pointer != nullptr && element_size != 0 &&
           static_cast<size_t>(total) <=
               std::numeric_limits<size_t>::max() / element_size &&
           slots <= total / per_slot;
}

bool scratch_valid(uint32_t required, uint32_t capacity, const void *pointer)
{
    return capacity >= required && (required == 0 || pointer != nullptr);
}

struct memory_region_t {
    uintptr_t begin;
    uintptr_t end;
};

bool make_region(const void *pointer, uint64_t count, size_t element_size,
                 memory_region_t *region)
{
    if (region == nullptr || (count != 0 && pointer == nullptr)) {
        return false;
    }
    if (count == 0) {
        *region = {};
        return true;
    }
    if (count > SIZE_MAX / element_size)
        return false;
    const size_t bytes = static_cast<size_t>(count) * element_size;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
    if (begin > UINTPTR_MAX - bytes)
        return false;
    region->begin = begin;
    region->end = begin + bytes;
    return true;
}

bool region_empty(const memory_region_t &region)
{
    return region.begin == region.end;
}

bool regions_overlap(const memory_region_t &left,
                     const memory_region_t &right)
{
    return !region_empty(left) && !region_empty(right) &&
           left.begin < right.end && right.begin < left.end;
}

template <typename T>
bool pointer_aligned(const T *pointer)
{
    return pointer != nullptr &&
           reinterpret_cast<uintptr_t>(pointer) % alignof(T) == 0;
}

using storage_regions_t = std::array<memory_region_t, 11>;

bool build_storage_regions(
    const worr_snapshot_q2proto_profile_v2 &profile,
    const worr_snapshot_q2proto_storage_v2 &storage,
    storage_regions_t *regions)
{
    return regions != nullptr &&
           make_region(storage.slots, storage.slot_capacity,
                       sizeof(*storage.slots), &(*regions)[0]) &&
           make_region(storage.entities, storage.entity_storage_capacity,
                       sizeof(*storage.entities), &(*regions)[1]) &&
           make_region(storage.area_bytes, storage.area_storage_capacity,
                       sizeof(*storage.area_bytes), &(*regions)[2]) &&
           make_region(storage.event_refs, storage.event_storage_capacity,
                       sizeof(*storage.event_refs), &(*regions)[3]) &&
           make_region(storage.lineages, storage.lineage_storage_capacity,
                       sizeof(*storage.lineages), &(*regions)[4]) &&
           make_region(storage.baselines, profile.max_entities,
                       sizeof(*storage.baselines), &(*regions)[5]) &&
           make_region(storage.baseline_present, profile.max_entities,
                       sizeof(*storage.baseline_present), &(*regions)[6]) &&
           make_region(storage.scratch_entities,
                       storage.scratch_entity_capacity,
                       sizeof(*storage.scratch_entities), &(*regions)[7]) &&
           make_region(storage.scratch_area_bytes,
                       storage.scratch_area_capacity,
                       sizeof(*storage.scratch_area_bytes), &(*regions)[8]) &&
           make_region(storage.scratch_event_refs,
                       storage.scratch_event_capacity,
                       sizeof(*storage.scratch_event_refs), &(*regions)[9]) &&
           make_region(storage.scratch_lineage,
                       storage.scratch_lineage_capacity,
                       sizeof(*storage.scratch_lineage), &(*regions)[10]);
}

bool regions_pairwise_disjoint(const storage_regions_t &regions)
{
    for (size_t i = 0; i < regions.size(); ++i) {
        for (size_t j = i + 1; j < regions.size(); ++j) {
            if (regions_overlap(regions[i], regions[j]))
                return false;
        }
    }
    return true;
}

bool profile_valid(const worr_snapshot_q2proto_profile_v2 &profile)
{
    uint8_t reserved = 0;
    for (uint8_t value : profile.reserved0)
        reserved |= value;
    return profile.struct_size == sizeof(profile) &&
           profile.schema_version == WORR_SNAPSHOT_Q2PROTO_VERSION &&
           profile.snapshot_epoch != 0 && profile.max_entities > 1 &&
           profile.max_models != 0 && profile.max_sounds != 0 &&
           profile.extended_entity_state <= 1 && reserved == 0;
}

bool storage_valid(const worr_snapshot_q2proto_profile_v2 &profile,
                   const worr_snapshot_q2proto_storage_v2 &storage)
{
    if (storage.struct_size != sizeof(storage) ||
        storage.schema_version != WORR_SNAPSHOT_Q2PROTO_VERSION ||
        storage.slot_capacity == 0 || storage.slots == nullptr ||
        storage.entities_per_slot > profile.max_entities ||
        !capacity_valid(storage.slot_capacity, storage.entities_per_slot,
                        storage.entity_storage_capacity, storage.entities,
                        sizeof(*storage.entities)) ||
        !capacity_valid(storage.slot_capacity, storage.area_bytes_per_slot,
                        storage.area_storage_capacity, storage.area_bytes,
                        sizeof(*storage.area_bytes)) ||
        !capacity_valid(storage.slot_capacity, storage.event_refs_per_slot,
                        storage.event_storage_capacity, storage.event_refs,
                        sizeof(*storage.event_refs)) ||
        !capacity_valid(storage.slot_capacity, profile.max_entities,
                        storage.lineage_storage_capacity, storage.lineages,
                        sizeof(*storage.lineages)) ||
        storage.baselines == nullptr || storage.baseline_present == nullptr ||
        !scratch_valid(storage.entities_per_slot,
                       storage.scratch_entity_capacity,
                       storage.scratch_entities) ||
        !scratch_valid(storage.area_bytes_per_slot,
                       storage.scratch_area_capacity,
                       storage.scratch_area_bytes) ||
        !scratch_valid(storage.event_refs_per_slot,
                       storage.scratch_event_capacity,
                       storage.scratch_event_refs) ||
        !scratch_valid(profile.max_entities,
                       storage.scratch_lineage_capacity,
                       storage.scratch_lineage)) {
        return false;
    }
    if (!pointer_aligned(storage.slots) ||
        (storage.entities != nullptr &&
         !pointer_aligned(storage.entities)) ||
        (storage.event_refs != nullptr &&
         !pointer_aligned(storage.event_refs)) ||
        !pointer_aligned(storage.lineages) ||
        !pointer_aligned(storage.baselines) ||
        (storage.scratch_entities != nullptr &&
         !pointer_aligned(storage.scratch_entities)) ||
        (storage.scratch_event_refs != nullptr &&
         !pointer_aligned(storage.scratch_event_refs)) ||
        !pointer_aligned(storage.scratch_lineage)) {
        return false;
    }
    storage_regions_t regions{};
    return build_storage_regions(profile, storage, &regions) &&
           regions_pairwise_disjoint(regions);
}

bool context_valid(const worr_snapshot_q2proto_context_v2 *context)
{
    return pointer_aligned(context) &&
           context->struct_size == sizeof(*context) &&
           context->schema_version == WORR_SNAPSHOT_Q2PROTO_VERSION &&
           profile_valid(context->profile) &&
           storage_valid(context->profile, context->storage) &&
           context->next_slot < context->storage.slot_capacity &&
           context->occupied <= context->storage.slot_capacity &&
           context->next_entity_serial != 0 &&
           context->next_area_serial != 0 &&
           context->next_event_serial != 0;
}

bool init_regions_valid(
    const worr_snapshot_q2proto_context_v2 *context,
    const worr_snapshot_q2proto_profile_v2 *profile,
    const worr_snapshot_q2proto_storage_v2 *storage)
{
    if (!pointer_aligned(context) || !pointer_aligned(profile) ||
        !pointer_aligned(storage)) {
        return false;
    }
    memory_region_t context_region{};
    memory_region_t profile_region{};
    memory_region_t envelope_region{};
    storage_regions_t regions{};
    if (!make_region(context, 1, sizeof(*context), &context_region) ||
        !make_region(profile, 1, sizeof(*profile), &profile_region) ||
        !make_region(storage, 1, sizeof(*storage), &envelope_region) ||
        !build_storage_regions(*profile, *storage, &regions) ||
        regions_overlap(context_region, profile_region) ||
        regions_overlap(context_region, envelope_region) ||
        regions_overlap(profile_region, envelope_region)) {
        return false;
    }
    for (const auto &region : regions) {
        if (regions_overlap(context_region, region) ||
            regions_overlap(profile_region, region) ||
            regions_overlap(envelope_region, region)) {
            return false;
        }
    }
    return true;
}

bool region_disjoint_from_context_storage(
    const worr_snapshot_q2proto_context_v2 *context,
    const memory_region_t &candidate)
{
    memory_region_t context_region{};
    storage_regions_t storage_regions{};
    if (!make_region(context, 1, sizeof(*context), &context_region) ||
        !build_storage_regions(context->profile, context->storage,
                               &storage_regions) ||
        regions_overlap(context_region, candidate)) {
        return false;
    }
    for (const auto &region : storage_regions) {
        if (regions_overlap(region, candidate))
            return false;
    }
    return true;
}

bool publish_regions_valid(
    const worr_snapshot_q2proto_context_v2 *context,
    const worr_snapshot_q2proto_frame_input_v2 *input,
    worr_snapshot_ref_v2 *ref_out)
{
    if (!pointer_aligned(input) || !pointer_aligned(ref_out) ||
        !pointer_aligned(input->frame) ||
        !pointer_aligned(input->entity_deltas)) {
        return false;
    }
    std::array<memory_region_t, 5> regions{};
    if (!make_region(input, 1, sizeof(*input), &regions[0]) ||
        !make_region(input->frame, 1, sizeof(*input->frame), &regions[1]) ||
        !make_region(input->entity_deltas, input->entity_delta_count,
                     sizeof(*input->entity_deltas), &regions[2]) ||
        !make_region(input->frame->areabits,
                     input->frame->areabits_len,
                     sizeof(uint8_t), &regions[3]) ||
        !make_region(ref_out, 1, sizeof(*ref_out), &regions[4])) {
        return false;
    }
    for (size_t i = 0; i < regions.size(); ++i) {
        if (!region_disjoint_from_context_storage(context, regions[i]))
            return false;
        for (size_t j = i + 1; j < regions.size(); ++j) {
            if (regions_overlap(regions[i], regions[j]))
                return false;
        }
    }
    return true;
}

bool view_output_regions_valid(
    const worr_snapshot_q2proto_context_v2 *context,
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out)
{
    if (!pointer_aligned(view_out) || !pointer_aligned(hashes_out))
        return false;
    memory_region_t view_region{};
    memory_region_t hashes_region{};
    return make_region(view_out, 1, sizeof(*view_out), &view_region) &&
           make_region(hashes_out, 1, sizeof(*hashes_out), &hashes_region) &&
           !regions_overlap(view_region, hashes_region) &&
           region_disjoint_from_context_storage(context, view_region) &&
           region_disjoint_from_context_storage(context, hashes_region);
}

bool snapshot_id_from_serverframe(uint32_t epoch, int32_t serverframe,
                                  worr_snapshot_id_v2 *id)
{
    if (id == nullptr || epoch == 0 || serverframe < 0)
        return false;
    id->epoch = epoch;
    id->sequence = static_cast<uint32_t>(serverframe) + 1u;
    return id->sequence != 0;
}

bool snapshot_id_equal(worr_snapshot_id_v2 left, worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch && left.sequence == right.sequence;
}

const worr_snapshot_q2proto_slot_v2 *find_slot(
    const worr_snapshot_q2proto_context_v2 *context, worr_snapshot_id_v2 id,
    uint32_t *slot_index)
{
    for (uint32_t i = 0; i < context->storage.slot_capacity; ++i) {
        const auto &slot = context->storage.slots[i];
        if (slot.committed == 1 && snapshot_id_equal(slot.snapshot.snapshot_id,
                                                     id)) {
            if (slot_index != nullptr)
                *slot_index = i;
            return &slot;
        }
    }
    return nullptr;
}

bool serial_reserve(uint64_t next, uint32_t count, uint64_t *first,
                    uint64_t *after)
{
    if (first == nullptr || after == nullptr || next == 0)
        return false;
    if (count == 0) {
        *first = 0;
        *after = next;
        return true;
    }
    if (static_cast<uint64_t>(count) > UINT64_MAX - next)
        return false;
    *first = next;
    *after = next + count;
    return *after != 0;
}

worr_snapshot_entity_generation_v2 inferred_generation(uint32_t index,
                                                        uint32_t generation,
                                                        bool epoch_reset)
{
    worr_snapshot_entity_generation_v2 result{};
    result.identity.index = index;
    result.identity.generation = generation;
    result.provenance_flags = WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    if (epoch_reset)
        result.provenance_flags |= WORR_SNAPSHOT_GENERATION_EPOCH_RESET;
    return result;
}

worr_snapshot_entity_v2 empty_entity(uint32_t index)
{
    worr_snapshot_entity_v2 entity{};
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.generation = inferred_generation(index, 1, true);
    entity.component_mask =
        WORR_SNAPSHOT_ENTITY_TRANSFORM |
        WORR_SNAPSHOT_ENTITY_INTERPOLATION |
        WORR_SNAPSHOT_ENTITY_MODELS |
        WORR_SNAPSHOT_ENTITY_ANIMATION |
        WORR_SNAPSHOT_ENTITY_APPEARANCE |
        WORR_SNAPSHOT_ENTITY_EFFECTS |
        WORR_SNAPSHOT_ENTITY_COLLISION |
        WORR_SNAPSHOT_ENTITY_LOOP_SOUND;
    entity.owner.index = WORR_EVENT_NO_ENTITY;
    return entity;
}

worr_snapshot_player_v2 empty_player()
{
    worr_snapshot_player_v2 player{};
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    return player;
}

bool entity_delta_shape_valid(const q2proto_entity_state_delta_t &delta)
{
    const uint8_t origin_bits = delta.origin.read.value.delta_bits;
    return (delta.delta_bits & ~entity_delta_known_bits) == 0 &&
           (origin_bits & ~UINT8_C(7)) == 0 &&
           (delta.origin.read.diff_bits & ~origin_bits) == 0 &&
           (delta.angle.delta_bits & ~UINT8_C(7)) == 0;
}

bool entity_indices_valid(const worr_snapshot_q2proto_profile_v2 &profile,
                          const worr_snapshot_entity_v2 &entity)
{
    return entity.model_index[0] < profile.max_models &&
           entity.model_index[1] < profile.max_models &&
           entity.model_index[2] < profile.max_models &&
           entity.model_index[3] < profile.max_models &&
           entity.sound < profile.max_sounds;
}

bool apply_entity_delta(const worr_snapshot_q2proto_profile_v2 &profile,
                        const worr_snapshot_entity_v2 &from,
                        const q2proto_entity_state_delta_t &delta,
                        worr_snapshot_entity_generation_v2 generation,
                        worr_snapshot_entity_v2 *to,
                        uint8_t *raw_event)
{
    if (to == nullptr || raw_event == nullptr ||
        !entity_delta_shape_valid(delta)) {
        return false;
    }
    *to = from;
    to->generation = generation;
    *raw_event = 0;
    if ((delta.delta_bits & Q2P_ESD_MODELINDEX) != 0)
        to->model_index[0] = delta.modelindex;
    if ((delta.delta_bits & Q2P_ESD_MODELINDEX2) != 0)
        to->model_index[1] = delta.modelindex2;
    if ((delta.delta_bits & Q2P_ESD_MODELINDEX3) != 0)
        to->model_index[2] = delta.modelindex3;
    if ((delta.delta_bits & Q2P_ESD_MODELINDEX4) != 0)
        to->model_index[3] = delta.modelindex4;
    if ((delta.delta_bits & Q2P_ESD_FRAME) != 0)
        to->frame = delta.frame;
    if ((delta.delta_bits & Q2P_ESD_SKINNUM) != 0)
        to->skin = delta.skinnum;
    if ((delta.delta_bits & Q2P_ESD_EFFECTS) != 0)
        to->effects = (to->effects & UINT64_C(0xffffffff00000000)) |
                      delta.effects;
    if ((delta.delta_bits & Q2P_ESD_EFFECTS_MORE) != 0)
        to->effects = (to->effects & UINT64_C(0xffffffff)) |
                      (static_cast<uint64_t>(delta.effects_more) << 32);
    if ((delta.delta_bits & Q2P_ESD_RENDERFX) != 0)
        to->renderfx = delta.renderfx;

    q2proto_maybe_read_diff_apply_float(&delta.origin, to->origin);
    for (int i = 0; i < 3; ++i) {
        if ((delta.angle.delta_bits & (1u << i)) != 0) {
            to->angles[i] = q2proto_var_angles_get_float_comp(
                &delta.angle.values, i);
        }
    }
    if ((delta.delta_bits & Q2P_ESD_OLD_ORIGIN) != 0)
        q2proto_var_coords_get_float(&delta.old_origin, to->old_origin);
    else if ((to->renderfx & profile.beam_renderfx_mask) == 0)
        std::memcpy(to->old_origin, from.origin, sizeof(to->old_origin));
    if ((delta.delta_bits & Q2P_ESD_SOUND) != 0)
        to->sound = delta.sound;
    if ((delta.delta_bits & Q2P_ESD_LOOP_VOLUME) != 0)
        to->loop_volume = static_cast<float>(delta.loop_volume) / 255.0f;
    if ((delta.delta_bits & Q2P_ESD_LOOP_ATTENUATION) != 0) {
        to->loop_attenuation =
            q2proto_sound_decode_loop_attenuation(delta.loop_attenuation);
    }
    if ((delta.delta_bits & Q2P_ESD_EVENT) != 0)
        *raw_event = delta.event;
    if ((delta.delta_bits & Q2P_ESD_SOLID) != 0)
        to->solid = delta.solid;
    if ((delta.delta_bits & Q2P_ESD_ALPHA) != 0)
        to->alpha = static_cast<float>(delta.alpha) / 255.0f;
    if ((delta.delta_bits & Q2P_ESD_SCALE) != 0)
        to->scale = static_cast<float>(delta.scale) / 16.0f;

    if (!profile.extended_entity_state) {
        to->renderfx &= profile.legacy_renderfx_allowed_mask;
        if ((to->renderfx & profile.beam_renderfx_mask) != 0)
            to->renderfx &= ~profile.legacy_beam_clear_mask;
    }
    return entity_indices_valid(profile, *to) &&
           Worr_SnapshotEntityValidateV2(to, profile.max_entities);
}

bool player_delta_shape_valid(const q2proto_svc_playerstate_t &delta)
{
    return (delta.delta_bits & ~player_delta_known_bits) == 0 &&
           (delta.pm_origin.read.value.delta_bits & ~UINT8_C(7)) == 0 &&
           (delta.pm_origin.read.diff_bits &
            ~delta.pm_origin.read.value.delta_bits) == 0 &&
           (delta.pm_velocity.read.value.delta_bits & ~UINT8_C(7)) == 0 &&
           (delta.pm_velocity.read.diff_bits &
            ~delta.pm_velocity.read.value.delta_bits) == 0 &&
           (delta.viewangles.delta_bits & ~UINT8_C(7)) == 0 &&
           (delta.gunoffset.delta_bits & ~UINT8_C(7)) == 0 &&
           (delta.gunangles.delta_bits & ~UINT8_C(7)) == 0 &&
           (delta.blend.delta_bits & ~UINT8_C(15)) == 0 &&
           (delta.damage_blend.delta_bits & ~UINT8_C(15)) == 0;
}

bool apply_player_delta(const worr_snapshot_q2proto_frame_input_v2 &input,
                        const worr_snapshot_player_v2 *from,
                        worr_snapshot_player_v2 *to)
{
    const auto &delta = input.frame->playerstate;
    if (to == nullptr || !player_delta_shape_valid(delta))
        return false;
    *to = from == nullptr ? empty_player() : *from;

    if ((delta.delta_bits & Q2P_PSD_PM_TYPE) != 0) {
        if ((input.flags &
             WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_TYPE_VALID) == 0) {
            return false;
        }
        to->movement.movement_type = input.canonical_movement_type;
    }
    q2proto_maybe_read_diff_apply_float(&delta.pm_origin,
                                         to->movement.origin);
    q2proto_maybe_read_diff_apply_float(&delta.pm_velocity,
                                         to->movement.velocity);
    if ((delta.delta_bits & Q2P_PSD_PM_TIME) != 0)
        to->movement.movement_time_ms = delta.pm_time;
    if ((delta.delta_bits & Q2P_PSD_PM_FLAGS) != 0) {
        if ((input.flags &
             WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_FLAGS_VALID) == 0) {
            return false;
        }
        to->movement.movement_flags = input.canonical_movement_flags;
    }
    if ((delta.delta_bits & Q2P_PSD_PM_GRAVITY) != 0)
        to->movement.gravity = delta.pm_gravity;
    if ((delta.delta_bits & Q2P_PSD_PM_DELTA_ANGLES) != 0) {
        q2proto_var_angles_get_float(&delta.pm_delta_angles,
                                     to->movement.delta_angles);
    }
    if ((delta.delta_bits & Q2P_PSD_VIEWOFFSET) != 0)
        q2proto_var_small_offsets_get_float(&delta.viewoffset,
                                            to->view_offset);
    for (int i = 0; i < 3; ++i) {
        if ((delta.viewangles.delta_bits & (1u << i)) != 0) {
            to->view_angles[i] = q2proto_var_angles_get_float_comp(
                &delta.viewangles.values, i);
        }
    }
    if ((delta.delta_bits & Q2P_PSD_KICKANGLES) != 0)
        q2proto_var_small_angles_get_float(&delta.kick_angles,
                                           to->kick_angles);
    if ((delta.delta_bits & Q2P_PSD_GUNINDEX) != 0)
        to->gun_index = delta.gunindex;
    if ((delta.delta_bits & Q2P_PSD_GUNSKIN) != 0)
        to->gun_skin = delta.gunskin;
    if ((delta.delta_bits & Q2P_PSD_GUNFRAME) != 0)
        to->gun_frame = delta.gunframe;
    for (int i = 0; i < 3; ++i) {
        if ((delta.gunoffset.delta_bits & (1u << i)) != 0) {
            to->gun_offset[i] = q2proto_var_small_offsets_get_float_comp(
                &delta.gunoffset.values, i);
        }
        if ((delta.gunangles.delta_bits & (1u << i)) != 0) {
            to->gun_angles[i] = q2proto_var_small_angles_get_float_comp(
                &delta.gunangles.values, i);
        }
    }
    for (int i = 0; i < 4; ++i) {
        if ((delta.blend.delta_bits & (1u << i)) != 0) {
            to->screen_blend[i] = q2proto_var_color_get_float_comp(
                &delta.blend.values, i);
        }
        if ((delta.damage_blend.delta_bits & (1u << i)) != 0) {
            to->damage_blend[i] = q2proto_var_color_get_float_comp(
                &delta.damage_blend.values, i);
        }
    }
    if ((delta.delta_bits & Q2P_PSD_FOV) != 0)
        to->fov = static_cast<float>(delta.fov);
    if ((delta.delta_bits & Q2P_PSD_RDFLAGS) != 0)
        to->rdflags = delta.rdflags;
    for (uint32_t i = 0; i < WORR_SNAPSHOT_STATS_CAPACITY; ++i) {
        if ((delta.statbits & (UINT64_C(1) << i)) != 0)
            to->stats[i] = delta.stats[i];
    }
    if ((delta.delta_bits & Q2P_PSD_GUNRATE) != 0)
        to->gun_rate = delta.gunrate;
    if ((delta.delta_bits & Q2P_PSD_PM_VIEWHEIGHT) != 0)
        to->movement.view_height = delta.pm_viewheight;
    if ((input.flags & WORR_SNAPSHOT_Q2PROTO_FRAME_TEAM_ID_VALID) != 0)
        to->team_id = input.team_id;
    return to->fov != 0.0f;
}

bool append_event(const worr_snapshot_q2proto_context_v2 *context,
                  const worr_snapshot_entity_v2 &entity, uint8_t raw_event,
                  uint32_t source_ordinal, uint32_t tick, uint64_t time_us,
                  uint32_t *event_count)
{
    if (raw_event == 0)
        return true;
    if (*event_count >= context->storage.event_refs_per_slot)
        return false;
    worr_event_record_v1 record{};
    uint64_t semantic_hash;
    if (!Worr_LegacyEntityEventCandidateBuildV1(
            tick, time_us, source_ordinal, entity.generation.identity,
            raw_event, context->profile.max_entities, &record,
            &semantic_hash)) {
        return false;
    }
    auto &event = context->storage.scratch_event_refs[*event_count];
    event = {};
    event.struct_size = sizeof(event);
    event.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    event.provenance =
        WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;
    event.carrier_ordinal = *event_count;
    event.semantic_version = WORR_EVENT_MODEL_REVISION;
    event.semantic_hash = semantic_hash;
    ++*event_count;
    return true;
}

} // namespace

extern "C" worr_snapshot_q2proto_result_v2 Worr_SnapshotQ2ProtoInitV2(
    worr_snapshot_q2proto_context_v2 *context,
    const worr_snapshot_q2proto_profile_v2 *profile,
    const worr_snapshot_q2proto_storage_v2 *storage)
{
    if (!pointer_aligned(context) || !pointer_aligned(profile) ||
        !pointer_aligned(storage) || !profile_valid(*profile) ||
        !storage_valid(*profile, *storage) ||
        !init_regions_valid(context, profile, storage)) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT;
    }
    worr_snapshot_q2proto_context_v2 initialized{};
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    initialized.profile = *profile;
    initialized.storage = *storage;
    initialized.next_entity_serial = 1;
    initialized.next_area_serial = 1;
    initialized.next_event_serial = 1;
    std::memset(storage->slots, 0,
                sizeof(*storage->slots) * storage->slot_capacity);
    std::memset(storage->baseline_present, 0, profile->max_entities);
    *context = initialized;
    return WORR_SNAPSHOT_Q2PROTO_OK;
}

extern "C" worr_snapshot_q2proto_result_v2
Worr_SnapshotQ2ProtoSetBaselineV2(
    worr_snapshot_q2proto_context_v2 *context, uint32_t entity_index,
    const q2proto_entity_state_delta_t *baseline_delta)
{
    if (!context_valid(context))
        return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
    if (!pointer_aligned(baseline_delta) || entity_index == 0 ||
        entity_index >= context->profile.max_entities) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT;
    }
    memory_region_t baseline_region{};
    if (!make_region(baseline_delta, 1, sizeof(*baseline_delta),
                     &baseline_region) ||
        !region_disjoint_from_context_storage(context, baseline_region)) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT;
    }
    worr_snapshot_entity_v2 baseline = empty_entity(entity_index);
    const worr_snapshot_entity_v2 zero = baseline;
    uint8_t raw_event;
    if (!apply_entity_delta(context->profile, zero,
                            *baseline_delta, baseline.generation, &baseline,
                            &raw_event) || raw_event != 0) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY;
    }
    context->storage.baselines[entity_index] = baseline;
    context->storage.baseline_present[entity_index] = 1;
    return WORR_SNAPSHOT_Q2PROTO_OK;
}

namespace {

const worr_snapshot_entity_v2 *slot_entities(
    const worr_snapshot_q2proto_context_v2 *context, uint32_t slot_index)
{
    if (context->storage.entities_per_slot == 0)
        return nullptr;
    return context->storage.entities +
           static_cast<size_t>(slot_index) *
               context->storage.entities_per_slot;
}

const worr_snapshot_q2proto_lineage_v2 *slot_lineage(
    const worr_snapshot_q2proto_context_v2 *context, uint32_t slot_index)
{
    return context->storage.lineages +
           static_cast<size_t>(slot_index) * context->profile.max_entities;
}

bool generation_equal(const worr_snapshot_entity_generation_v2 &left,
                      const worr_snapshot_entity_generation_v2 &right)
{
    return left.identity.index == right.identity.index &&
           left.identity.generation == right.identity.generation &&
           left.provenance_flags == right.provenance_flags &&
           left.reserved0 == right.reserved0;
}

bool generation_for_add(worr_snapshot_q2proto_lineage_v2 *lineage,
                        uint32_t *generation)
{
    if (lineage == nullptr || generation == nullptr)
        return false;
    if (lineage->generation == 0) {
        *generation = 1;
    } else if (!lineage->present) {
        if (lineage->generation == UINT32_MAX)
            return false;
        *generation = lineage->generation + 1u;
    } else {
        *generation = lineage->generation;
    }
    lineage->generation = *generation;
    lineage->present = 1;
    return true;
}

worr_snapshot_q2proto_result_v2 reconstruct_entities(
    const worr_snapshot_q2proto_context_v2 *context,
    const worr_snapshot_q2proto_frame_input_v2 &input,
    const worr_snapshot_q2proto_slot_v2 *base_slot,
    uint32_t base_slot_index,
    bool epoch_reset,
    uint32_t *entity_count_out,
    uint32_t *event_count_out)
{
    const worr_snapshot_entity_v2 *base_entities =
        base_slot == nullptr ? nullptr : slot_entities(context,
                                                        base_slot_index);
    const uint32_t base_count =
        base_slot == nullptr ? 0 : base_slot->entity_count;
    uint32_t base_index = 0;
    uint32_t output_count = 0;
    uint32_t event_count = 0;

    auto append_unchanged = [&](const worr_snapshot_entity_v2 &entity) {
        if (output_count >= context->storage.entities_per_slot)
            return false;
        /* CL_ParseDeltaEntity(old, new, number, nullptr) advances the
         * interpolation origin even when q2proto omitted an unchanged
         * entity.  Apply that legacy rule only to the scratch copy so a
         * failed publication cannot mutate its retained base.  Beams retain
         * their explicit endpoint semantics. */
        worr_snapshot_entity_v2 carried = entity;
        if ((carried.renderfx &
             context->profile.beam_renderfx_mask) == 0) {
            std::memcpy(carried.old_origin, carried.origin,
                        sizeof(carried.old_origin));
        }
        context->storage.scratch_entities[output_count++] = carried;
        return true;
    };

    for (uint32_t delta_index = 0;
         delta_index + 1u < input.entity_delta_count; ++delta_index) {
        const auto &carrier = input.entity_deltas[delta_index];
        const uint32_t newnum = carrier.newnum;
        while (base_index < base_count &&
               base_entities[base_index].generation.identity.index < newnum) {
            if (!append_unchanged(base_entities[base_index]))
                return WORR_SNAPSHOT_Q2PROTO_CAPACITY;
            ++base_index;
        }

        const bool matches_base =
            base_index < base_count &&
            base_entities[base_index].generation.identity.index == newnum;
        if (carrier.remove) {
            if (!matches_base)
                return WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY_ORDER;
            auto &lineage = context->storage.scratch_lineage[newnum];
            if (!lineage.present || lineage.generation !=
                    base_entities[base_index].generation.identity.generation) {
                return WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY;
            }
            lineage.present = 0;
            ++base_index;
            continue;
        }

        const worr_snapshot_entity_v2 *from;
        uint32_t generation_value;
        bool assigned_at_reset = false;
        if (matches_base) {
            from = &base_entities[base_index];
            generation_value = from->generation.identity.generation;
            auto &lineage = context->storage.scratch_lineage[newnum];
            if (!lineage.present || lineage.generation != generation_value)
                return WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY;
            ++base_index;
        } else {
            from = context->storage.baseline_present[newnum] != 0
                       ? &context->storage.baselines[newnum]
                       : nullptr;
            auto &lineage = context->storage.scratch_lineage[newnum];
            assigned_at_reset = epoch_reset && lineage.generation == 0;
            if (!generation_for_add(&lineage, &generation_value))
                return WORR_SNAPSHOT_Q2PROTO_GENERATION_EXHAUSTED;
        }
        const worr_snapshot_entity_v2 zero = empty_entity(newnum);
        if (from == nullptr)
            from = &zero;
        if (output_count >= context->storage.entities_per_slot)
            return WORR_SNAPSHOT_Q2PROTO_CAPACITY;
        worr_snapshot_entity_v2 &output =
            context->storage.scratch_entities[output_count];
        uint8_t raw_event;
        const auto generation = inferred_generation(newnum, generation_value,
                                                     assigned_at_reset);
        if (!apply_entity_delta(context->profile, *from,
                                carrier.entity_delta, generation, &output,
                                &raw_event)) {
            return WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY;
        }
        if (raw_event != 0 &&
            event_count >= context->storage.event_refs_per_slot) {
            return WORR_SNAPSHOT_Q2PROTO_CAPACITY;
        }
        if (!append_event(context, output, raw_event, output_count,
                          static_cast<uint32_t>(input.frame->serverframe),
                          input.server_time_us, &event_count)) {
            return WORR_SNAPSHOT_Q2PROTO_INVALID_EVENT;
        }
        ++output_count;
    }
    while (base_index < base_count) {
        if (!append_unchanged(base_entities[base_index]))
            return WORR_SNAPSHOT_Q2PROTO_CAPACITY;
        ++base_index;
    }

    /* Full frames may use a generation-only lineage parent. Tombstone every
     * parent entity not present in the reconstructed current frame. */
    if (base_slot == nullptr) {
        uint32_t output_index = 0;
        for (uint32_t index = 1; index < context->profile.max_entities;
             ++index) {
            while (output_index < output_count &&
                   context->storage.scratch_entities[output_index]
                           .generation.identity.index < index) {
                ++output_index;
            }
            context->storage.scratch_lineage[index].present =
                output_index < output_count &&
                context->storage.scratch_entities[output_index]
                        .generation.identity.index == index;
        }
    }
    *entity_count_out = output_count;
    *event_count_out = event_count;
    return WORR_SNAPSHOT_Q2PROTO_OK;
}

bool build_event_range(worr_snapshot_event_range_v2 *range,
                       uint64_t first_serial, uint32_t count)
{
    if (range == nullptr || (count != 0 && first_serial == 0))
        return false;
    *range = {};
    if (count != 0) {
        range->first_ref_serial = first_serial;
        range->count = count;
        range->provenance =
            WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;
        range->flags = WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER;
    }
    return true;
}

bool build_discontinuity(const worr_snapshot_q2proto_context_v2 *context,
                         const worr_snapshot_q2proto_frame_input_v2 &input,
                         worr_snapshot_id_v2 current, bool keyframe,
                         worr_snapshot_discontinuity_v2 *output)
{
    *output = {};
    const bool has_previous = context->last_observed.epoch != 0;
    const bool attach =
        (input.flags & WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH) != 0;
    if (!has_previous) {
        if (attach) {
            output->flags = WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH;
            output->reason =
                WORR_SNAPSHOT_DISCONTINUITY_REASON_OBSERVER_ATTACH;
        } else {
            if (current.sequence != 1 || !keyframe)
                return false;
            output->flags = WORR_SNAPSHOT_DISCONTINUITY_INITIAL;
            output->reason = WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL;
        }
    } else {
        if (attach || context->last_observed.epoch != current.epoch ||
            context->last_observed.sequence >= current.sequence) {
            return false;
        }
        output->previous = context->last_observed;
        const uint32_t distance =
            current.sequence - context->last_observed.sequence;
        output->server_tick_delta = distance;
        if (distance > 1) {
            output->flags |= WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP;
            output->reason =
                WORR_SNAPSHOT_DISCONTINUITY_REASON_SEQUENCE_GAP;
            output->skipped_sequences = distance - 1u;
        }
        if (!keyframe) {
            const uint32_t base_sequence =
                static_cast<uint32_t>(input.frame->deltaframe) + 1u;
            if (base_sequence != context->last_observed.sequence) {
                output->flags |= WORR_SNAPSHOT_DISCONTINUITY_BASE_JUMP;
                if (output->reason ==
                    WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE) {
                    output->reason =
                        WORR_SNAPSHOT_DISCONTINUITY_REASON_BASE_JUMP;
                }
            }
        }
    }
    if (keyframe) {
        output->flags |= WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
        if (output->reason == WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE)
            output->reason =
                WORR_SNAPSHOT_DISCONTINUITY_REASON_FULL_SNAPSHOT;
    }
    if (input.frame->suppress_count != 0) {
        output->flags |= WORR_SNAPSHOT_DISCONTINUITY_RATE_SUPPRESSED;
        if (output->reason == WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE)
            output->reason =
                WORR_SNAPSHOT_DISCONTINUITY_REASON_RATE_SUPPRESSED;
    }
    if ((input.flags &
         WORR_SNAPSHOT_Q2PROTO_FRAME_TRANSPORT_TRUNCATED) != 0) {
        output->flags |=
            WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED;
        if (output->reason == WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE)
            output->reason =
                WORR_SNAPSHOT_DISCONTINUITY_REASON_TRANSPORT_TRUNCATED;
    }
    if ((input.flags & WORR_SNAPSHOT_Q2PROTO_FRAME_FRAGMENT_STALL) != 0) {
        output->flags |= WORR_SNAPSHOT_DISCONTINUITY_FRAGMENT_STALL;
        if (output->reason == WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE) {
            output->reason =
                WORR_SNAPSHOT_DISCONTINUITY_REASON_FRAGMENT_STALL;
        }
    }
    return true;
}

} // namespace

extern "C" worr_snapshot_q2proto_result_v2 Worr_SnapshotQ2ProtoPublishV2(
    worr_snapshot_q2proto_context_v2 *context,
    const worr_snapshot_q2proto_frame_input_v2 *input,
    worr_snapshot_ref_v2 *ref_out)
{
    if (!context_valid(context))
        return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
    if (!pointer_aligned(input) || !pointer_aligned(ref_out))
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT;
    if (!pointer_aligned(input->frame) ||
        !pointer_aligned(input->entity_deltas) ||
        input->entity_delta_count == 0 ||
        input->struct_size != sizeof(*input) ||
        input->schema_version != WORR_SNAPSHOT_Q2PROTO_VERSION ||
        (input->flags & ~frame_known_flags) != 0 || input->reserved0 != 0 ||
        input->reserved1 != 0 ||
        input->consumed_command.reserved0 != 0 ||
        (input->consumed_command.provenance ==
                 WORR_SNAPSHOT_CONSUMED_COMMAND_NONE
             ? (input->consumed_command.cursor.epoch != 0 ||
                input->consumed_command.cursor.contiguous_sequence != 0)
             : (input->consumed_command.provenance !=
                    WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED ||
                input->consumed_command.cursor.epoch == 0)) ||
        input->controlled_entity_index == 0 ||
        input->controlled_entity_index >= context->profile.max_entities ||
        input->frame->areabits_len > context->storage.area_bytes_per_slot ||
        (input->frame->areabits_len != 0 &&
         input->frame->areabits == nullptr)) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT;
    }
    if (!publish_regions_valid(context, input, ref_out) ||
        input->entity_deltas[input->entity_delta_count - 1u].newnum != 0 ||
        input->entity_deltas[input->entity_delta_count - 1u].remove) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i + 1u < input->entity_delta_count; ++i) {
        const auto &carrier = input->entity_deltas[i];
        if (carrier.newnum == 0 ||
            carrier.newnum >= context->profile.max_entities ||
            (i != 0 && input->entity_deltas[i - 1u].newnum >=
                           carrier.newnum)) {
            return WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY_ORDER;
        }
    }
    if (context->publish_count == UINT64_MAX) {
        return WORR_SNAPSHOT_Q2PROTO_SERIAL_EXHAUSTED;
    }

    worr_snapshot_id_v2 current{};
    if (!snapshot_id_from_serverframe(context->profile.snapshot_epoch,
                                      input->frame->serverframe, &current)) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_FRAME;
    }
    const bool keyframe = input->frame->deltaframe <= 0;
    worr_snapshot_id_v2 base_id{};
    const worr_snapshot_q2proto_slot_v2 *base_slot = nullptr;
    uint32_t base_slot_index = 0;
    if (!keyframe) {
        if (!snapshot_id_from_serverframe(context->profile.snapshot_epoch,
                                          input->frame->deltaframe,
                                          &base_id) ||
            base_id.sequence >= current.sequence) {
            return WORR_SNAPSHOT_Q2PROTO_INVALID_BASE;
        }
        base_slot = find_slot(context, base_id, &base_slot_index);
        if (base_slot == nullptr)
            return WORR_SNAPSHOT_Q2PROTO_INVALID_BASE;
    } else if ((input->flags &
                WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID) != 0) {
        worr_snapshot_id_v2 lineage_parent{};
        if (!snapshot_id_from_serverframe(
                context->profile.snapshot_epoch,
                input->lineage_parent_serverframe, &lineage_parent)) {
            return WORR_SNAPSHOT_Q2PROTO_INVALID_BASE;
        }
        base_slot = find_slot(context, lineage_parent, &base_slot_index);
        if (base_slot == nullptr)
            return WORR_SNAPSHOT_Q2PROTO_INVALID_BASE;
    } else if (input->lineage_parent_serverframe != 0) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT;
    }

    const auto *lineage_source =
        base_slot == nullptr ? nullptr : slot_lineage(context,
                                                      base_slot_index);
    if (lineage_source == nullptr) {
        std::memset(context->storage.scratch_lineage, 0,
                    sizeof(*context->storage.scratch_lineage) *
                        context->profile.max_entities);
    } else {
        std::memmove(context->storage.scratch_lineage, lineage_source,
                     sizeof(*context->storage.scratch_lineage) *
                         context->profile.max_entities);
    }
    const bool generation_parent_only =
        keyframe && base_slot != nullptr;
    const auto *entity_base_slot = generation_parent_only ? nullptr
                                                           : base_slot;
    /* Capture this before entity reconstruction establishes generation 1.
     * The controlled playerstate generation and an in-range controlled entity
     * must describe the same epoch-reset lifecycle on the first keyframe. */
    const bool controlled_epoch_reset =
        context->storage
                .scratch_lineage[input->controlled_entity_index]
                .generation == 0 &&
        keyframe && base_slot == nullptr;
    uint32_t entity_count = 0;
    uint32_t event_count = 0;
    const auto reconstruct_result = reconstruct_entities(
        context, *input, entity_base_slot, base_slot_index,
        keyframe && base_slot == nullptr, &entity_count, &event_count);
    if (reconstruct_result != WORR_SNAPSHOT_Q2PROTO_OK)
        return reconstruct_result;

    auto &controlled_lineage =
        context->storage.scratch_lineage[input->controlled_entity_index];
    if (controlled_lineage.generation == 0)
        controlled_lineage.generation = 1;
    /* Playerstate proves that the controlled lifecycle exists even when the
     * first-person entity is not carried in this observer's entity range. */
    controlled_lineage.present = 1;
    auto controlled_generation = inferred_generation(
        input->controlled_entity_index, controlled_lineage.generation,
        controlled_epoch_reset);
    worr_snapshot_entity_generation_v2 *controlled_entity_generation = nullptr;
    for (uint32_t index = 0; index < entity_count; ++index) {
        auto &generation =
            context->storage.scratch_entities[index].generation;
        if (generation.identity.index == input->controlled_entity_index) {
            controlled_entity_generation = &generation;
            break;
        }
    }

    const worr_snapshot_entity_generation_v2 *retained_generation = nullptr;
    if (base_slot != nullptr) {
        const auto *base_entities = slot_entities(context, base_slot_index);
        for (uint32_t index = 0; index < base_slot->entity_count; ++index) {
            const auto &generation = base_entities[index].generation;
            if (generation.identity.index != input->controlled_entity_index)
                continue;
            if (generation.identity.generation !=
                controlled_lineage.generation) {
                return WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY;
            }
            retained_generation = &generation;
            break;
        }
        const auto &base_controlled = base_slot->player.controlled_entity;
        if (base_controlled.identity.index ==
                input->controlled_entity_index) {
            if (base_controlled.identity.generation !=
                    controlled_lineage.generation ||
                (retained_generation != nullptr &&
                 !generation_equal(*retained_generation, base_controlled))) {
                return WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY;
            }
            retained_generation = &base_controlled;
        }
    }
    if (controlled_entity_generation != nullptr) {
        if (controlled_entity_generation->identity.generation !=
            controlled_lineage.generation) {
            return WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY;
        }
        /* Generation provenance belongs to the assigned lifecycle, not just
         * the frame that assigned it. Unchanged carriers already retain the
         * exact record; explicit deltas and generation-only keyframes inherit
         * it here before entity, player, and snapshot hashes are calculated. */
        if (retained_generation != nullptr)
            *controlled_entity_generation = *retained_generation;
        controlled_generation = *controlled_entity_generation;
    } else if (retained_generation != nullptr) {
        /* First-person entities are commonly omitted. Preserve the previous
         * lifecycle's exact provenance across that omission and across a
         * generation-only keyframe parent. */
        controlled_generation = *retained_generation;
    }

    worr_snapshot_player_v2 player{};
    const worr_snapshot_player_v2 *base_player =
        !keyframe && base_slot != nullptr ? &base_slot->player : nullptr;
    if (!apply_player_delta(*input, base_player, &player))
        return WORR_SNAPSHOT_Q2PROTO_INVALID_PLAYER;
    player.controlled_entity = controlled_generation;
    if (!Worr_SnapshotPlayerValidateV2(&player,
                                       context->profile.max_entities)) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_PLAYER;
    }
    if (input->frame->areabits_len != 0) {
        std::memmove(context->storage.scratch_area_bytes,
                     input->frame->areabits, input->frame->areabits_len);
    }

    uint64_t entity_first;
    uint64_t area_first;
    uint64_t event_first;
    uint64_t next_entity;
    uint64_t next_area;
    uint64_t next_event;
    if (!serial_reserve(context->next_entity_serial, entity_count,
                        &entity_first, &next_entity) ||
        !serial_reserve(context->next_area_serial,
                        input->frame->areabits_len, &area_first,
                        &next_area) ||
        !serial_reserve(context->next_event_serial, event_count,
                        &event_first, &next_event)) {
        return WORR_SNAPSHOT_Q2PROTO_SERIAL_EXHAUSTED;
    }

    worr_snapshot_v2 snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    snapshot.flags = WORR_SNAPSHOT_FLAG_COMPLETE |
                     WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION |
                     WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
    if (keyframe)
        snapshot.flags |= WORR_SNAPSHOT_FLAG_KEYFRAME;
    if ((input->flags &
         WORR_SNAPSHOT_Q2PROTO_FRAME_TRANSPORT_TRUNCATED) != 0) {
        snapshot.flags &= ~WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE;
        snapshot.flags |= WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED;
    }
    snapshot.snapshot_id = current;
    if (!keyframe)
        snapshot.base_id = base_id;
    snapshot.server_tick =
        static_cast<uint32_t>(input->frame->serverframe);
    snapshot.server_time_us = input->server_time_us;
    snapshot.controlled_entity = controlled_generation;
    snapshot.consumed_command = input->consumed_command;
    if (!build_discontinuity(context, *input, current, keyframe,
                             &snapshot.discontinuity)) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_FRAME;
    }
    snapshot.entity_range.first_serial = entity_first;
    snapshot.entity_range.count = entity_count;
    snapshot.area_range.first_serial = area_first;
    snapshot.area_range.count = input->frame->areabits_len;
    if (!build_event_range(&snapshot.event_range, event_first, event_count) ||
        !Worr_SnapshotPlayerHashV2(&player, context->profile.max_entities,
                                   &snapshot.player_hash) ||
        !Worr_SnapshotEntityListHashV2(
            context->storage.scratch_entities, entity_count,
            context->profile.max_entities, &snapshot.entity_hash) ||
        !Worr_SnapshotAreaHashV2(context->storage.scratch_area_bytes,
                                 input->frame->areabits_len,
                                 &snapshot.area_hash) ||
        !Worr_SnapshotEventRefsHashV2(
            context->storage.scratch_event_refs, event_count,
            &snapshot.event_hash) ||
        !Worr_SnapshotCalculateHashV2(&snapshot,
                                      context->profile.max_entities,
                                      &snapshot.snapshot_hash) ||
        !Worr_SnapshotValidateV2(&snapshot,
                                 context->profile.max_entities)) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_FRAME;
    }

    worr_snapshot_projection_view_v2 scratch_view{};
    scratch_view.struct_size = sizeof(scratch_view);
    scratch_view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    scratch_view.snapshot = &snapshot;
    scratch_view.player = &player;
    scratch_view.entities = context->storage.scratch_entities;
    scratch_view.area_bytes = context->storage.scratch_area_bytes;
    scratch_view.event_refs = context->storage.scratch_event_refs;
    scratch_view.entity_count = entity_count;
    scratch_view.area_byte_count = input->frame->areabits_len;
    scratch_view.event_ref_count = event_count;
    worr_snapshot_projection_hashes_v2 hashes{};
    if (!Worr_SnapshotProjectionHashesV2(
            &scratch_view, context->profile.max_entities, &hashes)) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_FRAME;
    }

    const uint32_t slot_index = context->next_slot;
    auto &destination = context->storage.slots[slot_index];
    if (destination.committed > 1 ||
        (destination.committed != 0 && destination.generation == 0)) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
    }
    if (destination.generation == UINT32_MAX)
        return WORR_SNAPSHOT_Q2PROTO_GENERATION_EXHAUSTED;
    const uint32_t generation = destination.generation + 1u;
    const bool was_committed = destination.committed != 0;

    const size_t entity_offset =
        static_cast<size_t>(slot_index) *
        context->storage.entities_per_slot;
    const size_t area_offset =
        static_cast<size_t>(slot_index) *
        context->storage.area_bytes_per_slot;
    const size_t event_offset =
        static_cast<size_t>(slot_index) *
        context->storage.event_refs_per_slot;
    const size_t lineage_offset =
        static_cast<size_t>(slot_index) * context->profile.max_entities;
    if (entity_count != 0) {
        std::memmove(context->storage.entities + entity_offset,
                     context->storage.scratch_entities,
                     sizeof(*context->storage.entities) * entity_count);
    }
    if (input->frame->areabits_len != 0) {
        std::memmove(context->storage.area_bytes + area_offset,
                     context->storage.scratch_area_bytes,
                     input->frame->areabits_len);
    }
    if (event_count != 0) {
        std::memmove(context->storage.event_refs + event_offset,
                     context->storage.scratch_event_refs,
                     sizeof(*context->storage.event_refs) * event_count);
    }
    std::memmove(context->storage.lineages + lineage_offset,
                 context->storage.scratch_lineage,
                 sizeof(*context->storage.lineages) *
                     context->profile.max_entities);

    worr_snapshot_q2proto_slot_v2 committed{};
    committed.snapshot = snapshot;
    committed.player = player;
    committed.hashes = hashes;
    committed.entity_first_serial = entity_first;
    committed.area_first_serial = area_first;
    committed.event_first_serial = event_first;
    committed.entity_count = entity_count;
    committed.area_byte_count = input->frame->areabits_len;
    committed.event_ref_count = event_count;
    committed.generation = generation;
    committed.committed = 1;
    destination = committed;

    context->next_slot = slot_index + 1u;
    if (context->next_slot == context->storage.slot_capacity)
        context->next_slot = 0;
    if (!was_committed)
        ++context->occupied;
    context->last_observed = current;
    context->next_entity_serial = next_entity;
    context->next_area_serial = next_area;
    context->next_event_serial = next_event;
    ++context->publish_count;
    ref_out->slot = slot_index;
    ref_out->generation = generation;
    return WORR_SNAPSHOT_Q2PROTO_OK;
}

extern "C" worr_snapshot_q2proto_result_v2 Worr_SnapshotQ2ProtoViewV2(
    const worr_snapshot_q2proto_context_v2 *context,
    worr_snapshot_ref_v2 ref,
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out)
{
    if (!context_valid(context))
        return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
    if (!view_output_regions_valid(context, view_out, hashes_out) ||
        ref.slot >= context->storage.slot_capacity || ref.generation == 0) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT;
    }
    const auto &slot = context->storage.slots[ref.slot];
    if (slot.committed != 1 || slot.generation != ref.generation)
        return WORR_SNAPSHOT_Q2PROTO_STALE_REF;
    worr_snapshot_projection_view_v2 view{};
    view.struct_size = sizeof(view);
    view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    view.snapshot = &slot.snapshot;
    view.player = &slot.player;
    view.entities = slot_entities(context, ref.slot);
    view.area_bytes = context->storage.area_bytes_per_slot == 0
                          ? nullptr
                          : context->storage.area_bytes +
                                static_cast<size_t>(ref.slot) *
                                    context->storage.area_bytes_per_slot;
    view.event_refs = context->storage.event_refs_per_slot == 0
                          ? nullptr
                          : context->storage.event_refs +
                                static_cast<size_t>(ref.slot) *
                                    context->storage.event_refs_per_slot;
    view.entity_count = slot.entity_count;
    view.area_byte_count = slot.area_byte_count;
    view.event_ref_count = slot.event_ref_count;
    worr_snapshot_projection_hashes_v2 verified{};
    if (!Worr_SnapshotProjectionHashesV2(
            &view, context->profile.max_entities, &verified) ||
        std::memcmp(&verified, &slot.hashes, sizeof(verified)) != 0) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
    }
    *view_out = view;
    *hashes_out = verified;
    return WORR_SNAPSHOT_Q2PROTO_OK;
}

namespace {

bool stage_epoch_rebind(
    const worr_snapshot_q2proto_context_v2 *context,
    uint32_t slot_index, uint32_t new_snapshot_epoch,
    worr_snapshot_v2 *snapshot_out,
    worr_snapshot_projection_hashes_v2 *hashes_out)
{
    if (snapshot_out == nullptr || hashes_out == nullptr ||
        slot_index >= context->storage.slot_capacity) {
        return false;
    }
    const auto &slot = context->storage.slots[slot_index];
    if (slot.committed != 1 || slot.generation == 0 ||
        slot.snapshot.snapshot_id.epoch !=
            context->profile.snapshot_epoch) {
        return false;
    }

    worr_snapshot_projection_view_v2 current_view{};
    current_view.struct_size = sizeof(current_view);
    current_view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    current_view.snapshot = &slot.snapshot;
    current_view.player = &slot.player;
    current_view.entities = slot_entities(context, slot_index);
    current_view.area_bytes =
        context->storage.area_bytes_per_slot == 0
            ? nullptr
            : context->storage.area_bytes +
                  static_cast<size_t>(slot_index) *
                      context->storage.area_bytes_per_slot;
    current_view.event_refs =
        context->storage.event_refs_per_slot == 0
            ? nullptr
            : context->storage.event_refs +
                  static_cast<size_t>(slot_index) *
                      context->storage.event_refs_per_slot;
    current_view.entity_count = slot.entity_count;
    current_view.area_byte_count = slot.area_byte_count;
    current_view.event_ref_count = slot.event_ref_count;
    worr_snapshot_projection_hashes_v2 verified{};
    if (!Worr_SnapshotProjectionHashesV2(
            &current_view, context->profile.max_entities, &verified) ||
        std::memcmp(&verified, &slot.hashes, sizeof(verified)) != 0) {
        return false;
    }

    worr_snapshot_v2 snapshot = slot.snapshot;
    snapshot.snapshot_id.epoch = new_snapshot_epoch;
    if (snapshot.base_id.epoch != 0) {
        if (snapshot.base_id.epoch != context->profile.snapshot_epoch)
            return false;
        snapshot.base_id.epoch = new_snapshot_epoch;
    }
    if (snapshot.discontinuity.previous.epoch != 0) {
        if (snapshot.discontinuity.previous.epoch !=
            context->profile.snapshot_epoch) {
            return false;
        }
        snapshot.discontinuity.previous.epoch = new_snapshot_epoch;
    }
    if (!Worr_SnapshotCalculateHashV2(
            &snapshot, context->profile.max_entities,
            &snapshot.snapshot_hash)) {
        return false;
    }

    auto rebound_view = current_view;
    rebound_view.snapshot = &snapshot;
    worr_snapshot_projection_hashes_v2 rebound_hashes{};
    if (!Worr_SnapshotProjectionHashesV2(
            &rebound_view, context->profile.max_entities,
            &rebound_hashes)) {
        return false;
    }
    *snapshot_out = snapshot;
    *hashes_out = rebound_hashes;
    return true;
}

} // namespace

extern "C" worr_snapshot_q2proto_result_v2
Worr_SnapshotQ2ProtoRebindEpochV2(
    worr_snapshot_q2proto_context_v2 *context,
    uint32_t new_snapshot_epoch)
{
    if (!context_valid(context))
        return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
    if (new_snapshot_epoch == 0)
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT;
    if (new_snapshot_epoch == context->profile.snapshot_epoch)
        return WORR_SNAPSHOT_Q2PROTO_OK;
    if ((context->last_observed.epoch == 0) !=
            (context->last_observed.sequence == 0) ||
        (context->last_observed.epoch != 0 &&
         context->last_observed.epoch !=
             context->profile.snapshot_epoch)) {
        return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
    }

    /* Preflight every retained slot before mutating any caller-owned state. */
    for (uint32_t i = 0; i < context->storage.slot_capacity; ++i) {
        const auto &slot = context->storage.slots[i];
        if (slot.committed > 1 ||
            (slot.committed != 0 && slot.generation == 0)) {
            return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
        }
        if (slot.committed == 0)
            continue;
        worr_snapshot_v2 snapshot{};
        worr_snapshot_projection_hashes_v2 hashes{};
        if (!stage_epoch_rebind(
                context, i, new_snapshot_epoch, &snapshot, &hashes)) {
            return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
        }
    }

    /* Recompute deterministically while the source epoch is still current,
     * then commit the staged identity and derived hashes in place. */
    for (uint32_t i = 0; i < context->storage.slot_capacity; ++i) {
        auto &slot = context->storage.slots[i];
        if (slot.committed == 0)
            continue;
        worr_snapshot_v2 snapshot{};
        worr_snapshot_projection_hashes_v2 hashes{};
        if (!stage_epoch_rebind(
                context, i, new_snapshot_epoch, &snapshot, &hashes)) {
            return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
        }
        slot.snapshot = snapshot;
        slot.hashes = hashes;
    }
    context->profile.snapshot_epoch = new_snapshot_epoch;
    if (context->last_observed.epoch != 0)
        context->last_observed.epoch = new_snapshot_epoch;
    return WORR_SNAPSHOT_Q2PROTO_OK;
}

extern "C" worr_snapshot_q2proto_result_v2 Worr_SnapshotQ2ProtoResetV2(
    worr_snapshot_q2proto_context_v2 *context,
    uint32_t new_snapshot_epoch)
{
    if (!context_valid(context))
        return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
    if (new_snapshot_epoch == 0)
        return WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < context->storage.slot_capacity; ++i) {
        const auto &slot = context->storage.slots[i];
        if (slot.committed > 1 ||
            (slot.committed != 0 && slot.generation == 0)) {
            return WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT;
        }
        if (slot.generation == UINT32_MAX)
            return WORR_SNAPSHOT_Q2PROTO_GENERATION_EXHAUSTED;
    }
    for (uint32_t i = 0; i < context->storage.slot_capacity; ++i) {
        auto &slot = context->storage.slots[i];
        slot.generation = slot.generation == 0 ? 1u
                                                : slot.generation + 1u;
        slot.committed = 0;
    }
    std::memset(context->storage.baseline_present, 0,
                context->profile.max_entities);
    context->profile.snapshot_epoch = new_snapshot_epoch;
    context->last_observed = {};
    context->next_slot = 0;
    context->occupied = 0;
    return WORR_SNAPSHOT_Q2PROTO_OK;
}
