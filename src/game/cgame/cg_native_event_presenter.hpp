/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/cgame_native_event_probe.h"
#include "shared/event_abi.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

constexpr std::size_t CG_KEYED_POI_IMAGE_NAME_CAPACITY = 64u;

enum cg_keyed_poi_presentation_disposition_v1 : std::uint32_t {
    CG_KEYED_POI_PRESENTATION_NOOP_DISABLED_V1 = 0u,
    CG_KEYED_POI_PRESENTATION_NOOP_MISSING_V1 = 1u,
    CG_KEYED_POI_PRESENTATION_NOOP_CAPACITY_V1 = 2u,
    CG_KEYED_POI_PRESENTATION_UPSERT_V1 = 3u,
    CG_KEYED_POI_PRESENTATION_DELETE_V1 = 4u,
};

struct cg_prepared_keyed_poi_v1 {
    std::uint32_t disposition;
    std::uint32_t slot_index;
    std::uint64_t expiry_time;
    worr_event_payload_keyed_poi_v1 payload;
    char image_name[CG_KEYED_POI_IMAGE_NAME_CAPACITY];
    int width;
    int height;
};

bool CG_CanPresentKeyedPOIValue(
    const worr_event_payload_keyed_poi_v1 *payload,
    cg_prepared_keyed_poi_v1 *prepared_out);
void CG_PresentKeyedPOIValue(
    const cg_prepared_keyed_poi_v1 *prepared);

constexpr std::uint32_t CG_KEYED_POI_NO_SLOT_V1 =
    std::numeric_limits<std::uint32_t>::max();

template <typename Slot>
bool CG_KeyedPOISlotActiveV1(const Slot &slot, std::uint64_t now)
{
    return slot.infinite || slot.time > now;
}

template <typename Slots>
std::uint32_t CG_SelectKeyedPOISlotV1(
    const Slots &slots, int key, std::uint64_t now)
{
    std::uint32_t free_slot = CG_KEYED_POI_NO_SLOT_V1;
    std::uint32_t oldest_unkeyed = CG_KEYED_POI_NO_SLOT_V1;

    for (std::uint32_t index = 0; index < slots.size(); ++index) {
        const auto &candidate = slots[index];
        if (key != 0 && candidate.id == key)
            return index;
        if (!CG_KeyedPOISlotActiveV1(candidate, now)) {
            if (free_slot == CG_KEYED_POI_NO_SLOT_V1)
                free_slot = index;
            continue;
        }
        if (candidate.id != 0)
            continue;
        if (oldest_unkeyed == CG_KEYED_POI_NO_SLOT_V1 ||
            (slots[oldest_unkeyed].infinite && !candidate.infinite) ||
            (!slots[oldest_unkeyed].infinite && !candidate.infinite &&
             candidate.time < slots[oldest_unkeyed].time)) {
            oldest_unkeyed = index;
        }
    }

    return free_slot != CG_KEYED_POI_NO_SLOT_V1
               ? free_slot
               : oldest_unkeyed;
}

template <typename Slots>
bool CG_PrepareKeyedPOIStateV1(
    const Slots &slots,
    const worr_event_payload_keyed_poi_v1 *payload,
    bool enabled, bool clock_available, std::uint64_t now,
    cg_prepared_keyed_poi_v1 *prepared_out)
{
    if (!payload || !prepared_out || payload->key == 0)
        return false;

    *prepared_out = {};
    prepared_out->payload = *payload;
    if (!enabled) {
        prepared_out->disposition =
            CG_KEYED_POI_PRESENTATION_NOOP_DISABLED_V1;
        return true;
    }

    if (payload->lifetime_ms ==
        WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS) {
        for (std::uint32_t index = 0; index < slots.size(); ++index) {
            if (slots[index].id != payload->key)
                continue;
            prepared_out->disposition =
                CG_KEYED_POI_PRESENTATION_DELETE_V1;
            prepared_out->slot_index = index;
            return true;
        }
        prepared_out->disposition =
            CG_KEYED_POI_PRESENTATION_NOOP_MISSING_V1;
        return true;
    }

    if (!clock_available)
        return false;
    const std::uint32_t slot =
        CG_SelectKeyedPOISlotV1(slots, payload->key, now);
    if (slot == CG_KEYED_POI_NO_SLOT_V1) {
        prepared_out->disposition =
            CG_KEYED_POI_PRESENTATION_NOOP_CAPACITY_V1;
        return true;
    }

    prepared_out->disposition = CG_KEYED_POI_PRESENTATION_UPSERT_V1;
    prepared_out->slot_index = slot;
    prepared_out->expiry_time =
        payload->lifetime_ms == 0
            ? 0
            : now + static_cast<std::uint64_t>(payload->lifetime_ms);
    return true;
}

template <typename Slots>
bool CG_CommitKeyedPOIStateV1(
    Slots &slots, const cg_prepared_keyed_poi_v1 *prepared)
{
    if (!prepared)
        return false;

    switch (prepared->disposition) {
    case CG_KEYED_POI_PRESENTATION_NOOP_DISABLED_V1:
    case CG_KEYED_POI_PRESENTATION_NOOP_MISSING_V1:
    case CG_KEYED_POI_PRESENTATION_NOOP_CAPACITY_V1:
        return false;
    case CG_KEYED_POI_PRESENTATION_DELETE_V1:
        if (prepared->slot_index >= slots.size() ||
            slots[prepared->slot_index].id != prepared->payload.key) {
            return false;
        }
        slots[prepared->slot_index] = {};
        return true;
    case CG_KEYED_POI_PRESENTATION_UPSERT_V1:
        if (prepared->slot_index >= slots.size())
            return false;
        break;
    default:
        return false;
    }

    auto &slot = slots[prepared->slot_index];
    slot = {};
    slot.id = prepared->payload.key;
    slot.time = prepared->expiry_time;
    slot.infinite = prepared->payload.lifetime_ms == 0;
    slot.color_index = prepared->payload.color_index;
    slot.flags = prepared->payload.flags;
    slot.image_index = prepared->payload.image_index;
    static_assert(sizeof(slot.image_name) >=
                  CG_KEYED_POI_IMAGE_NAME_CAPACITY);
    std::memcpy(slot.image_name, prepared->image_name,
                CG_KEYED_POI_IMAGE_NAME_CAPACITY);
    slot.width = prepared->width;
    slot.height = prepared->height;
    slot.position[0] = prepared->payload.position[0];
    slot.position[1] = prepared->payload.position[1];
    slot.position[2] = prepared->payload.position[2];
    return true;
}

constexpr std::uint32_t CG_NATIVE_EVENT_PRESENTER_STATUS_VERSION = 2u;
constexpr std::uint32_t CG_NATIVE_EVENT_PRESENTER_KIND_COUNT = 8u;
static_assert(CG_NATIVE_EVENT_PRESENTER_KIND_COUNT ==
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT);

enum cg_native_event_presenter_kind_v1 : std::uint32_t {
    CG_NATIVE_EVENT_PRESENTER_KIND_NONE = 0u,
    CG_NATIVE_EVENT_PRESENTER_KIND_LEGACY_ENTITY = 1u,
    CG_NATIVE_EVENT_PRESENTER_KIND_LEGACY_TEMP = 2u,
    CG_NATIVE_EVENT_PRESENTER_KIND_MUZZLE = 3u,
    CG_NATIVE_EVENT_PRESENTER_KIND_SPATIAL_AUDIO = 4u,
    CG_NATIVE_EVENT_PRESENTER_KIND_DAMAGE = 5u,
    CG_NATIVE_EVENT_PRESENTER_KIND_HELP_PATH = 6u,
    CG_NATIVE_EVENT_PRESENTER_KIND_KEYED_POI = 7u,
};

struct cg_native_event_presenter_status_v1 {
    std::uint32_t struct_size;
    std::uint32_t schema_version;
    std::uint32_t map_generation;
    std::uint32_t map_active;
    std::uint32_t preflight_probe_requested;
    std::uint32_t preflight_probe_latched;
    std::uint32_t preflight_probe_active;
    std::uint32_t effect_authority_enabled;
    std::uint32_t resources_required;
    std::uint32_t reserved0;
    std::uint64_t probe_commits;
    std::uint64_t probe_effects_suppressed;
    std::uint64_t probe_nonvisual_commits;
    std::uint64_t probe_commits_by_kind
        [CG_NATIVE_EVENT_PRESENTER_KIND_COUNT];
};

static_assert(sizeof(cg_native_event_presenter_status_v1) == 128u);

static_assert(static_cast<std::uint32_t>(
                  CG_NATIVE_EVENT_PRESENTER_KIND_NONE) ==
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_NONE);
static_assert(static_cast<std::uint32_t>(
                  CG_NATIVE_EVENT_PRESENTER_KIND_LEGACY_ENTITY) ==
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_LEGACY_ENTITY);
static_assert(static_cast<std::uint32_t>(
                  CG_NATIVE_EVENT_PRESENTER_KIND_LEGACY_TEMP) ==
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_LEGACY_TEMP);
static_assert(static_cast<std::uint32_t>(
                  CG_NATIVE_EVENT_PRESENTER_KIND_MUZZLE) ==
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE);
static_assert(static_cast<std::uint32_t>(
                  CG_NATIVE_EVENT_PRESENTER_KIND_SPATIAL_AUDIO) ==
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_SPATIAL_AUDIO);
static_assert(static_cast<std::uint32_t>(
                  CG_NATIVE_EVENT_PRESENTER_KIND_DAMAGE) ==
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_DAMAGE);
static_assert(static_cast<std::uint32_t>(
                  CG_NATIVE_EVENT_PRESENTER_KIND_HELP_PATH) ==
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_HELP_PATH);
static_assert(static_cast<std::uint32_t>(
                  CG_NATIVE_EVENT_PRESENTER_KIND_KEYED_POI) ==
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI);

void CG_NativeEventPresenterInitCvars();
void CG_NativeEventPresenterInstall();
void CG_NativeEventPresenterUninstall();
void CG_NativeEventPresenterBeginMap();
void CG_NativeEventPresenterEndMap();

/* The probe decision is sampled exactly once by BeginMap, before map
 * resources are registered. Changing the cvar during a map affects only the
 * next map. The probe performs full authority preflight but never emits an
 * effect or claims raw legacy ownership. */
bool CG_NativeEventPresenterPreflightProbeEnabled();
bool CG_NativeEventPresenterResourcesRequired();
bool CG_NativeEventPresenterGetStatus(
    cg_native_event_presenter_status_v1 *status_out);
const worr_cgame_native_event_probe_export_v1 *
CG_GetNativeEventProbeAPI();
const worr_cgame_native_event_probe_export_v2 *
CG_GetNativeEventProbeAPIv2();

/* Effect authority is deliberately independent of native event-stream
 * readiness. It defaults off so the native runtime can validate, order, and
 * terminally consume projected legacy effects while the legacy presenter
 * remains the sole audiovisual owner. */
void CG_NativeEventPresenterSetEffectAuthority(bool enabled);
bool CG_NativeEventPresenterEffectAuthorityEnabled();
