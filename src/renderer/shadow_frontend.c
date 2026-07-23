/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "renderer/shadow_frontend.h"

#include "common/common.h"
#include "common/error.h"
#include "renderer/renderer_api.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void R_AddDebugLine(const vec3_t start, const vec3_t end, color_t color,
                    uint32_t time, qboolean depth_test);
void R_AddDebugBounds(const vec3_t mins, const vec3_t maxs, color_t color,
                      uint32_t time, qboolean depth_test);
void R_AddDebugSphere(const vec3_t origin, float radius, color_t color,
                      uint32_t time, qboolean depth_test);
void R_AddDebugText(const vec3_t origin, const vec3_t angles,
                    const char *text, float size, color_t color,
                    uint32_t time, qboolean depth_test);

static const vec3_t shadow_point_face_axis[SHADOW_FRONTEND_POINT_FACES][3] = {
    {{ 1,  0,  0}, { 0,  0,  1}, { 0,  1,  0}},
    {{-1,  0,  0}, { 0,  0, -1}, { 0,  1,  0}},
    {{ 0,  1,  0}, {-1,  0,  0}, { 0,  0, -1}},
    {{ 0, -1,  0}, { 1,  0,  0}, { 0,  0, -1}},
    {{ 0,  0,  1}, { 1,  0,  0}, { 0,  1,  0}},
    {{ 0,  0, -1}, {-1,  0,  0}, { 0,  1,  0}},
};

// Score multiplier for lights that were selected last frame. A challenger
// must beat an incumbent by this margin before the selection changes, which
// keeps shadows from popping in and out as the camera moves across the
// score boundary.
#define SHADOW_SELECT_HYSTERESIS 1.35f

static const char *const shadow_dirty_names[] = {
    "moved-caster",
    "animated-caster",
    "light-params",
    "filter-family",
    "world-bsp",
    "eviction",
    "new-page",
};

static void ShadowFrontend_DebugDraw(const shadow_frontend_state_t *state,
                                     const shadow_frontend_policy_t *policy);

static void ShadowFrontend_AddPointToBounds(const vec3_t point,
                                            vec3_t mins,
                                            vec3_t maxs)
{
    for (int i = 0; i < 3; i++) {
        if (point[i] < mins[i]) {
            mins[i] = point[i];
        }
        if (point[i] > maxs[i]) {
            maxs[i] = point[i];
        }
    }
}

static int ShadowFrontend_BoxLeafs_r(const mnode_t *node,
                                     const vec3_t mins,
                                     const vec3_t maxs,
                                     const mleaf_t **list,
                                     int listsize,
                                     int *count,
                                     bool *overflow)
{
    while (node && node->plane) {
        box_plane_t side = BoxOnPlaneSideFast(mins, maxs, node->plane);
        if (side == BOX_INFRONT) {
            node = node->children[0];
        } else if (side == BOX_BEHIND) {
            node = node->children[1];
        } else {
            ShadowFrontend_BoxLeafs_r(node->children[0], mins, maxs,
                                      list, listsize, count, overflow);
            node = node->children[1];
        }
    }

    if (node) {
        if (*count < listsize) {
            list[(*count)++] = (const mleaf_t *)node;
        } else if (overflow) {
            *overflow = true;
        }
    }
    return *count;
}

static int ShadowFrontend_BoxLeafs(const bsp_t *bsp,
                                   const vec3_t mins,
                                   const vec3_t maxs,
                                   const mleaf_t **list,
                                   int listsize,
                                   bool *overflow)
{
    int count = 0;
    if (overflow) {
        *overflow = false;
    }
    if (!bsp || !bsp->nodes || !list || listsize <= 0) {
        return 0;
    }
    return ShadowFrontend_BoxLeafs_r(bsp->nodes, mins, maxs, list, listsize,
                                     &count, overflow);
}

static size_t ShadowFrontend_MaskBytes(const bsp_t *bsp)
{
    if (bsp && bsp->visrowsize > 0 &&
        (size_t)bsp->visrowsize <= sizeof(((visrow_t *)0)->b)) {
        // Round up to whole longs; overlap tests only read this many.
        size_t longs = VIS_FAST_LONGS(bsp->visrowsize);
        return longs * sizeof(((visrow_t *)0)->l[0]);
    }
    return sizeof(visrow_t);
}

static void ShadowFrontend_MaskClear(const bsp_t *bsp, visrow_t *mask)
{
    memset(mask, 0, ShadowFrontend_MaskBytes(bsp));
}

static void ShadowFrontend_MaskSet(visrow_t *mask, int cluster)
{
    if (cluster >= 0 && cluster < MAX_MAP_CLUSTERS) {
        mask->b[cluster >> 3] |= (byte)(1u << (cluster & 7));
    }
}

static bool ShadowFrontend_MaskOverlap(const bsp_t *bsp,
                                       const visrow_t *a,
                                       const visrow_t *b)
{
    int longs = VIS_FAST_LONGS(bsp && bsp->visrowsize > 0 ? bsp->visrowsize : VIS_MAX_BYTES);
    for (int i = 0; i < longs; i++) {
        if (a->l[i] & b->l[i]) {
            return true;
        }
    }
    return false;
}

static uint32_t ShadowFrontend_HashU32(uint32_t hash, uint32_t value)
{
    return (hash ^ value) * 16777619u;
}

static uint32_t ShadowFrontend_HashI32(uint32_t hash, int32_t value)
{
    return ShadowFrontend_HashU32(hash, (uint32_t)value);
}

static int32_t ShadowFrontend_Quantize(float value, float scale)
{
    double quantized = (double)value * (double)scale;

    if (!isfinite(quantized)) {
        return 0;
    }
    if (quantized <= (double)INT32_MIN) {
        return INT32_MIN;
    }
    if (quantized >= (double)INT32_MAX) {
        return INT32_MAX;
    }
    return (int32_t)llround(quantized);
}

static uint32_t ShadowFrontend_HashFloatQ(uint32_t hash, float value, float scale)
{
    return ShadowFrontend_HashI32(hash, ShadowFrontend_Quantize(value, scale));
}

static uint32_t ShadowFrontend_HashFloatBits(uint32_t hash, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return ShadowFrontend_HashU32(hash, bits);
}

typedef struct {
    cvar_t *primary;
    cvar_t *alias;
} shadow_cvar_alias_pair_t;

#define SHADOW_FRONTEND_MAX_CVAR_ALIASES 24

static shadow_cvar_alias_pair_t
    shadow_cvar_aliases[SHADOW_FRONTEND_MAX_CVAR_ALIASES];
static int shadow_cvar_alias_count;
static bool shadow_cvar_alias_syncing;

static void ShadowFrontend_CvarAliasChanged(cvar_t *self)
{
    if (!self || shadow_cvar_alias_syncing) {
        return;
    }

    for (int i = 0; i < shadow_cvar_alias_count; i++) {
        shadow_cvar_alias_pair_t *pair = &shadow_cvar_aliases[i];
        cvar_t *target = NULL;

        if (self == pair->primary) {
            target = pair->alias;
        } else if (self == pair->alias) {
            target = pair->primary;
        }
        if (!target) {
            continue;
        }

        shadow_cvar_alias_syncing = true;
        Cvar_SetByVar(target, self->string, FROM_CODE);
        shadow_cvar_alias_syncing = false;
        return;
    }
}

static void ShadowFrontend_UnregisterCvarAliases(void)
{
    for (int i = 0; i < shadow_cvar_alias_count; i++) {
        shadow_cvar_alias_pair_t *pair = &shadow_cvar_aliases[i];
        if (pair->primary && pair->primary->changed == ShadowFrontend_CvarAliasChanged) {
            pair->primary->changed = NULL;
        }
        if (pair->alias && pair->alias->changed == ShadowFrontend_CvarAliasChanged) {
            pair->alias->changed = NULL;
        }
    }
    memset(shadow_cvar_aliases, 0, sizeof(shadow_cvar_aliases));
    shadow_cvar_alias_count = 0;
    shadow_cvar_alias_syncing = false;
}

static void ShadowFrontend_RegisterCvarAliasPair(cvar_t *primary,
                                                 cvar_t *alias)
{
    if (!primary || !alias ||
        shadow_cvar_alias_count >= SHADOW_FRONTEND_MAX_CVAR_ALIASES) {
        return;
    }

    // An explicitly configured legacy/backend name wins only when the
    // canonical r_ name retained its default. If both were configured, the
    // canonical archived value is authoritative. Changed callbacks then
    // mirror every live write, so selection does not depend on unrelated
    // per-cvar edit counts.
    if ((alias->flags & CVAR_MODIFIED) && !(primary->flags & CVAR_MODIFIED)) {
        Cvar_SetByVar(primary, alias->string, FROM_CODE);
    } else {
        Cvar_SetByVar(alias, primary->string, FROM_CODE);
    }

    shadow_cvar_aliases[shadow_cvar_alias_count++] =
        (shadow_cvar_alias_pair_t){ primary, alias };
    primary->changed = ShadowFrontend_CvarAliasChanged;
    alias->changed = ShadowFrontend_CvarAliasChanged;
}

static cvar_t *ShadowFrontend_CvarAlias(cvar_t *primary, cvar_t *alias)
{
    return primary ? primary : alias;
}

static int ShadowFrontend_CvarInteger(cvar_t *primary, cvar_t *alias)
{
    cvar_t *var = ShadowFrontend_CvarAlias(primary, alias);
    return var ? var->integer : 0;
}

static float ShadowFrontend_CvarValue(cvar_t *primary, cvar_t *alias)
{
    cvar_t *var = ShadowFrontend_CvarAlias(primary, alias);
    return var ? var->value : 0.0f;
}

static float ShadowFrontend_CvarFiniteValue(cvar_t *primary, cvar_t *alias,
                                            float fallback)
{
    float value = ShadowFrontend_CvarValue(primary, alias);
    return isfinite(value) ? value : fallback;
}

static const char *ShadowFrontend_CvarString(cvar_t *primary, cvar_t *alias)
{
    cvar_t *var = ShadowFrontend_CvarAlias(primary, alias);
    return var ? var->string : "";
}

static void ShadowFrontend_ParseVec3(const char *text, const vec3_t fallback, vec3_t out)
{
    if (!text || sscanf(text, "%f %f %f", &out[0], &out[1], &out[2]) != 3 ||
        !isfinite(out[0]) || !isfinite(out[1]) || !isfinite(out[2])) {
        VectorCopy(fallback, out);
    }
}

static cvar_t *ShadowFrontend_RegisterAlias(const char *prefix,
                                            const char *suffix,
                                            cvar_t *primary)
{
    if (!prefix || !*prefix || !primary) {
        return NULL;
    }

    char name[64];
    Q_snprintf(name, sizeof(name), "%s_%s", prefix, suffix);
    cvar_t *alias = Cvar_Get(name, primary->string,
                             primary->flags | CVAR_NOARCHIVE);
    ShadowFrontend_RegisterCvarAliasPair(primary, alias);
    return alias;
}

void ShadowFrontend_Init(shadow_frontend_state_t *state)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->cached_pvs2_cluster1 = -2;
    state->cached_pvs2_cluster2 = -2;
    state->next_page_generation = 1;
}

void ShadowFrontend_Shutdown(shadow_frontend_state_t *state)
{
    if (!state) {
        return;
    }
    ShadowFrontend_UnregisterCvarAliases();
    ShadowFrontend_Init(state);
}

void ShadowFrontend_RegisterCvars(shadow_frontend_cvars_t *vars,
                                  const char *backend_prefix)
{
    if (!vars) {
        return;
    }

    ShadowFrontend_UnregisterCvarAliases();
    memset(vars, 0, sizeof(*vars));

    vars->enabled = Cvar_Get("r_shadowmaps", "1", CVAR_ARCHIVE);
    vars->size = Cvar_Get("r_shadowmap_size", "512", CVAR_ARCHIVE);
    vars->lights = Cvar_Get("r_shadowmap_lights", "6", CVAR_ARCHIVE);
    vars->dynamic = Cvar_Get("r_shadowmap_dynamic", "1", CVAR_ARCHIVE);
    vars->cache_mode = Cvar_Get("r_shadowmap_cache_mode", "1", CVAR_ARCHIVE);
    vars->filter = Cvar_Get("r_shadow_filter", "1", CVAR_ARCHIVE);
    vars->pcss_max_lights = Cvar_Get("r_shadow_pcss_max_lights", "1", CVAR_ARCHIVE);
    vars->bias_slope = Cvar_Get("r_shadow_bias_slope", "1.0", CVAR_ARCHIVE);
    vars->normal_offset = Cvar_Get("r_shadow_normal_offset", "0.5", CVAR_ARCHIVE);
    vars->bias_scale = Cvar_Get("r_shadow_bias_scale", "1.0", CVAR_ARCHIVE);
    vars->softness = Cvar_Get("r_shadow_softness", "1.0", CVAR_ARCHIVE);
    vars->debug_light = Cvar_Get("r_shadow_debug_light", "-1", 0);
    vars->debug_draw = Cvar_Get("r_shadow_draw_debug", "0", 0);
    vars->freeze_selection = Cvar_Get("r_shadow_freeze_selection", "0", 0);
    vars->freeze_dirtying = Cvar_Get("r_shadow_freeze_dirtying", "0", 0);
    vars->alpha_mode = Cvar_Get("r_shadow_alpha_mode", "0", CVAR_ARCHIVE);
    vars->model_exclusion_list = Cvar_Get("r_shadow_model_exclusion_list", "", CVAR_ARCHIVE);
    // Off by default: the receivers currently modulate the WHOLE baked
    // lightmap by the sun visibility factor, so any surface occluded from
    // the sun direction (i.e. every interior under a roof) loses all of its
    // baked light, not just a sun term. Until sun light is separated from
    // the lightmap, sun shadows are opt-in.
    vars->sun_enabled = Cvar_Get("r_shadow_sun", "0", CVAR_ARCHIVE);
    vars->sun_cascades = Cvar_Get("r_shadow_sun_cascades", "3", CVAR_ARCHIVE);
    vars->sun_resolution = Cvar_Get("r_shadow_sun_resolution", "1024", CVAR_ARCHIVE);
    vars->sun_direction = Cvar_Get("r_shadow_sun_direction", "0.3 0.5 -1", CVAR_ARCHIVE);
    vars->sun_distance = Cvar_Get("r_shadow_sun_distance", "4096", CVAR_ARCHIVE);
    vars->sun_size = Cvar_Get("r_shadow_sun_size", "4096", CVAR_ARCHIVE);

    vars->alias_enabled = ShadowFrontend_RegisterAlias(backend_prefix, "shadowmaps", vars->enabled);
    vars->alias_size = ShadowFrontend_RegisterAlias(backend_prefix, "shadowmap_size", vars->size);
    vars->alias_lights = ShadowFrontend_RegisterAlias(backend_prefix, "shadowmap_lights", vars->lights);
    vars->alias_dynamic = ShadowFrontend_RegisterAlias(backend_prefix, "shadowmap_dynamic", vars->dynamic);
    vars->alias_cache_mode = ShadowFrontend_RegisterAlias(backend_prefix, "shadowmap_cache_mode", vars->cache_mode);
    vars->alias_filter = ShadowFrontend_RegisterAlias(backend_prefix, "shadowmap_filter", vars->filter);
    vars->alias_pcss_max_lights = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_pcss_max_lights", vars->pcss_max_lights);
    vars->alias_bias_slope = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_bias_slope", vars->bias_slope);
    vars->alias_normal_offset = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_normal_offset", vars->normal_offset);
    vars->alias_bias_scale = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_bias_scale", vars->bias_scale);
    vars->alias_softness = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_softness", vars->softness);
    vars->alias_debug_light = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_debug_light", vars->debug_light);
    vars->alias_debug_draw = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_draw_debug", vars->debug_draw);
    vars->alias_freeze_selection = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_freeze_selection", vars->freeze_selection);
    vars->alias_freeze_dirtying = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_freeze_dirtying", vars->freeze_dirtying);
    vars->alias_alpha_mode = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_alpha_mode", vars->alpha_mode);
    vars->alias_model_exclusion_list = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_model_exclusion_list", vars->model_exclusion_list);
    vars->alias_sun_enabled = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_sun", vars->sun_enabled);
    vars->alias_sun_cascades = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_sun_cascades", vars->sun_cascades);
    vars->alias_sun_resolution = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_sun_resolution", vars->sun_resolution);
    vars->alias_sun_direction = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_sun_direction", vars->sun_direction);
    vars->alias_sun_distance = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_sun_distance", vars->sun_distance);
    vars->alias_sun_size = ShadowFrontend_RegisterAlias(backend_prefix, "shadow_sun_size", vars->sun_size);
}

void ShadowFrontend_DefaultPolicy(shadow_frontend_policy_t *policy)
{
    if (!policy) {
        return;
    }

    memset(policy, 0, sizeof(*policy));
    policy->enabled = true;
    policy->dynamic_lights = true;
    policy->max_lights = 6;
    policy->default_resolution = 512;
    policy->min_resolution = 64;
    policy->max_resolution = 4096;
    policy->pcss_max_lights = 1;
    policy->sun_cascades = 3;
    policy->sun_resolution = 1024;
    policy->debug_light = -1;
    policy->slope_bias = 1.0f;
    policy->normal_offset = 0.5f;
    policy->bias_scale = 1.0f;
    policy->softness = 1.0f;
    policy->sun_distance = 4096.0f;
    policy->sun_size = 4096.0f;
    VectorSet(policy->sun_direction, 0.3f, 0.5f, -1.0f);
    VectorNormalize(policy->sun_direction);
    policy->filter_family = SHADOW_FILTER_PCF;
    policy->cache_mode = SHADOW_CACHE_STATIC_REUSE;
    policy->model_exclusion_list[0] = '\0';
}

void ShadowFrontend_PolicyFromCvars(const shadow_frontend_cvars_t *vars,
                                    shadow_frontend_policy_t *policy)
{
    ShadowFrontend_DefaultPolicy(policy);
    if (!vars || !policy) {
        return;
    }

    policy->enabled = ShadowFrontend_CvarInteger(vars->enabled, vars->alias_enabled) != 0;
    policy->dynamic_lights = ShadowFrontend_CvarInteger(vars->dynamic, vars->alias_dynamic) != 0;
    policy->freeze_selection =
        ShadowFrontend_CvarInteger(vars->freeze_selection, vars->alias_freeze_selection) != 0;
    policy->freeze_dirtying =
        ShadowFrontend_CvarInteger(vars->freeze_dirtying, vars->alias_freeze_dirtying) != 0;
    policy->sun_enabled =
        ShadowFrontend_CvarInteger(vars->sun_enabled, vars->alias_sun_enabled) != 0;
    policy->max_lights =
        Q_clipf((float)ShadowFrontend_CvarInteger(vars->lights, vars->alias_lights),
                0.0f, (float)SHADOW_FRONTEND_MAX_LIGHTS);
    policy->default_resolution =
        Q_clipf((float)ShadowFrontend_CvarInteger(vars->size, vars->alias_size),
                64.0f, 4096.0f);
    policy->pcss_max_lights =
        Q_clipf((float)ShadowFrontend_CvarInteger(vars->pcss_max_lights, vars->alias_pcss_max_lights),
                0.0f, (float)SHADOW_FRONTEND_MAX_LIGHTS);
    policy->sun_cascades =
        Q_clipf((float)ShadowFrontend_CvarInteger(vars->sun_cascades, vars->alias_sun_cascades),
                1.0f, (float)SHADOW_FRONTEND_MAX_SUN_CASCADES);
    policy->sun_resolution =
        Q_clipf((float)ShadowFrontend_CvarInteger(vars->sun_resolution, vars->alias_sun_resolution),
                64.0f, 4096.0f);
    policy->debug_light = ShadowFrontend_CvarInteger(vars->debug_light, vars->alias_debug_light);
    policy->debug_draw = ShadowFrontend_CvarInteger(vars->debug_draw, vars->alias_debug_draw);
    policy->alpha_mode = Q_clip(
        ShadowFrontend_CvarInteger(vars->alpha_mode, vars->alias_alpha_mode),
        0, 1);
    Q_strlcpy(policy->model_exclusion_list,
              ShadowFrontend_CvarString(vars->model_exclusion_list,
                                         vars->alias_model_exclusion_list),
              sizeof(policy->model_exclusion_list));
    policy->slope_bias = Q_clipf(ShadowFrontend_CvarFiniteValue(
                                     vars->bias_slope, vars->alias_bias_slope, 1.0f),
                                 0.0f, 16.0f);
    policy->normal_offset = Q_clipf(ShadowFrontend_CvarFiniteValue(
                                        vars->normal_offset, vars->alias_normal_offset, 0.5f),
                                    0.0f, 16.0f);
    policy->bias_scale = Q_clipf(ShadowFrontend_CvarFiniteValue(
                                     vars->bias_scale, vars->alias_bias_scale, 1.0f),
                                 0.0f, 16.0f);
    policy->softness = Q_clipf(ShadowFrontend_CvarFiniteValue(
                                   vars->softness, vars->alias_softness, 1.0f),
                               0.25f, 4.0f);
    policy->sun_distance =
        Q_clipf(ShadowFrontend_CvarFiniteValue(
                    vars->sun_distance, vars->alias_sun_distance, 4096.0f),
                128.0f, 65536.0f);
    policy->sun_size =
        Q_clipf(ShadowFrontend_CvarFiniteValue(
                    vars->sun_size, vars->alias_sun_size, 4096.0f),
                128.0f, 65536.0f);

    const vec3_t fallback_sun = {0.3f, 0.5f, -1.0f};
    ShadowFrontend_ParseVec3(ShadowFrontend_CvarString(vars->sun_direction, vars->alias_sun_direction),
                             fallback_sun, policy->sun_direction);
    if (VectorNormalize(policy->sun_direction) <= 0.0f) {
        VectorCopy(fallback_sun, policy->sun_direction);
        VectorNormalize(policy->sun_direction);
    }

    int filter = ShadowFrontend_CvarInteger(vars->filter, vars->alias_filter);
    filter = (int)Q_clipf((float)filter, (float)SHADOW_FILTER_HARD, (float)SHADOW_FILTER_PCSS);
    policy->filter_family = (shadow_filter_family_t)filter;

    int cache_mode = ShadowFrontend_CvarInteger(vars->cache_mode, vars->alias_cache_mode);
    cache_mode = (int)Q_clipf((float)cache_mode, (float)SHADOW_CACHE_NONE,
                              (float)SHADOW_CACHE_WORLD_ONLY);
    policy->cache_mode = (shadow_cache_mode_t)cache_mode;
}

void ShadowFrontend_BeginMainVisibilityGuard(shadow_frontend_state_t *state,
                                             unsigned visframe,
                                             int viewcluster1,
                                             int viewcluster2)
{
    if (!state) {
        return;
    }

    state->guard_visframe = visframe;
    state->guard_viewcluster1 = viewcluster1;
    state->guard_viewcluster2 = viewcluster2;
    state->guard_active = true;
}

void ShadowFrontend_EndMainVisibilityGuard(shadow_frontend_state_t *state,
                                           unsigned visframe,
                                           int viewcluster1,
                                           int viewcluster2,
                                           const char *backend_name)
{
    if (!state || !state->guard_active) {
        return;
    }

    state->guard_active = false;
    if (state->guard_visframe != visframe ||
        state->guard_viewcluster1 != viewcluster1 ||
        state->guard_viewcluster2 != viewcluster2) {
        Com_Error(ERR_DROP,
                  "%s shadow frontend mutated main-view visibility state "
                  "(visframe %u->%u clusters %d/%d->%d/%d)",
                  backend_name ? backend_name : "renderer",
                  state->guard_visframe, visframe,
                  state->guard_viewcluster1, state->guard_viewcluster2,
                  viewcluster1, viewcluster2);
    }
}

static uint32_t ShadowFrontend_WorldRevision(const bsp_t *bsp)
{
    if (!bsp) {
        return 0;
    }
    return bsp->checksum ? bsp->checksum : (uint32_t)(uintptr_t)bsp;
}

static int ShadowFrontend_LeafClusterAtPoint(const bsp_t *bsp, const vec3_t point)
{
    if (!bsp || !bsp->nodes) {
        return -1;
    }
    const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, point);
    return leaf ? leaf->cluster : -1;
}

static bool ShadowFrontend_AreaAllowsPoint(const refdef_t *fd,
                                           const bsp_t *bsp,
                                           const vec3_t point)
{
    if (!fd || !fd->areabits || !bsp || !bsp->nodes) {
        return true;
    }

    const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, point);
    if (!leaf || leaf->area < 0 || leaf->area >= MAX_MAP_AREAS) {
        return true;
    }
    return Q_IsBitSet(fd->areabits, leaf->area) != 0;
}

static bool ShadowFrontend_AreaAllowsLeaf(const refdef_t *fd,
                                          const mleaf_t *leaf)
{
    if (!fd || !fd->areabits || !leaf || leaf->area < 0 ||
        leaf->area >= MAX_MAP_AREAS) {
        return true;
    }
    return Q_IsBitSet(fd->areabits, leaf->area) != 0;
}

static bool ShadowFrontend_AreaAllowsBounds(const refdef_t *fd,
                                            const bsp_t *bsp,
                                            const vec3_t mins,
                                            const vec3_t maxs)
{
    if (!fd || !fd->areabits || !bsp || !bsp->nodes) {
        return true;
    }

    const mleaf_t *leafs[256];
    bool overflow = false;
    int count = ShadowFrontend_BoxLeafs(bsp, mins, maxs, leafs,
                                        q_countof(leafs), &overflow);
    if (overflow) {
        return true;
    }

    bool any = false;
    for (int i = 0; i < count; i++) {
        any = true;
        if (ShadowFrontend_AreaAllowsLeaf(fd, leafs[i])) {
            return true;
        }
    }

    if (!any) {
        vec3_t center;
        VectorAvg(mins, maxs, center);
        return ShadowFrontend_AreaAllowsPoint(fd, bsp, center);
    }
    return false;
}

static bool ShadowFrontend_AreaAllowsSphere(const refdef_t *fd,
                                            const bsp_t *bsp,
                                            const vec3_t center,
                                            float radius)
{
    vec3_t mins, maxs;
    radius = max(radius, 0.0f);
    for (int i = 0; i < 3; i++) {
        mins[i] = center[i] - radius;
        maxs[i] = center[i] + radius;
    }
    return ShadowFrontend_AreaAllowsBounds(fd, bsp, mins, maxs);
}

static bool ShadowFrontend_AreaAllowsLight(const refdef_t *fd,
                                           const bsp_t *bsp,
                                           const dlight_t *dl)
{
    if (!dl) {
        return true;
    }
    if (ShadowFrontend_AreaAllowsSphere(fd, bsp, dl->origin, dl->radius)) {
        return true;
    }
    return ShadowFrontend_AreaAllowsSphere(fd, bsp, dl->sphere,
                                           max(dl->sphere[3], dl->radius));
}

static bool ShadowFrontend_BoxClusters(const bsp_t *bsp,
                                       const vec3_t mins,
                                       const vec3_t maxs,
                                       visrow_t *mask)
{
    ShadowFrontend_MaskClear(bsp, mask);
    if (!bsp || !bsp->nodes) {
        return false;
    }

    const mleaf_t *leafs[128];
    bool overflow = false;
    int count = ShadowFrontend_BoxLeafs(bsp, mins, maxs, leafs,
                                        q_countof(leafs), &overflow);
    if (overflow) {
        ShadowFrontend_MaskClear(bsp, mask);
        return false;
    }
    bool any = false;
    for (int i = 0; i < count; i++) {
        if (!leafs[i]) {
            continue;
        }
        ShadowFrontend_MaskSet(mask, leafs[i]->cluster);
        any = any || leafs[i]->cluster >= 0;
    }

    if (!any) {
        vec3_t center;
        VectorAvg(mins, maxs, center);
        int cluster = ShadowFrontend_LeafClusterAtPoint(bsp, center);
        ShadowFrontend_MaskSet(mask, cluster);
        any = cluster >= 0;
    }
    return any;
}

static bool ShadowFrontend_SphereClusters(const bsp_t *bsp,
                                          const vec3_t origin,
                                          float radius,
                                          visrow_t *mask)
{
    vec3_t mins, maxs;
    for (int i = 0; i < 3; i++) {
        mins[i] = origin[i] - radius;
        maxs[i] = origin[i] + radius;
    }
    return ShadowFrontend_BoxClusters(bsp, mins, maxs, mask);
}

static void ShadowFrontend_UpdateLightInfluenceClusters(const bsp_t *bsp,
                                                        shadow_light_desc_t *light)
{
    if (!light) {
        return;
    }

    light->influence_clusters_valid = false;
    ShadowFrontend_MaskClear(bsp, &light->influence_clusters);

    if (light->light_class == SHADOW_LIGHT_CLASS_SUN) {
        return;
    }

    if (light->light_class == SHADOW_LIGHT_CLASS_CONE) {
        vec3_t center;
        VectorCopy(light->influence_sphere, center);
        light->influence_clusters_valid =
            ShadowFrontend_SphereClusters(bsp, center,
                                          max(light->influence_sphere[3], 1.0f),
                                          &light->influence_clusters);
        return;
    }

    light->influence_clusters_valid =
        ShadowFrontend_SphereClusters(bsp, light->origin, max(light->radius, 1.0f),
                                      &light->influence_clusters);
}

static void ShadowFrontend_UpdatePVS2(shadow_frontend_state_t *state,
                                      const refdef_t *fd,
                                      const bsp_t *bsp)
{
    if (!state) {
        return;
    }
    state->cached_pvs2_valid = false;
    if (!fd || !bsp || !bsp->vis || !bsp->nodes) {
        return;
    }

    const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, fd->vieworg);
    int cluster1 = leaf ? leaf->cluster : -1;
    int cluster2 = cluster1;

    vec3_t tmp;
    VectorCopy(fd->vieworg, tmp);
    if (leaf && !leaf->contents[0]) {
        tmp[2] -= 16.0f;
    } else {
        tmp[2] += 16.0f;
    }
    leaf = BSP_PointLeaf(bsp->nodes, tmp);
    if (leaf && !(leaf->contents[0] & CONTENTS_SOLID)) {
        cluster2 = leaf->cluster;
    }

    if (cluster1 < 0) {
        return;
    }

    if (state->cached_pvs2_bsp == bsp &&
        state->cached_pvs2_cluster1 == cluster1 &&
        state->cached_pvs2_cluster2 == cluster2) {
        state->cached_pvs2_valid = true;
        return;
    }

    BSP_ClusterVis(bsp, &state->cached_pvs2_mask, cluster1, DVIS_PVS2);
    if (cluster1 != cluster2 && cluster2 >= 0) {
        visrow_t vis2;
        BSP_ClusterVis(bsp, &vis2, cluster2, DVIS_PVS2);
        int longs = VIS_FAST_LONGS(bsp->visrowsize);
        for (int i = 0; i < longs; i++) {
            state->cached_pvs2_mask.l[i] |= vis2.l[i];
        }
    }

    state->cached_pvs2_bsp = bsp;
    state->cached_pvs2_cluster1 = cluster1;
    state->cached_pvs2_cluster2 = cluster2;
    state->cached_pvs2_valid = true;
}

static bool ShadowFrontend_PointTouchesPVS2(shadow_frontend_state_t *state,
                                            const bsp_t *bsp,
                                            const vec3_t point)
{
    int cluster = ShadowFrontend_LeafClusterAtPoint(bsp, point);
    return cluster < 0 || Q_IsBitSet(state->cached_pvs2_mask.b, cluster) != 0;
}

static bool ShadowFrontend_SphereTouchesPVS2(shadow_frontend_state_t *state,
                                             const bsp_t *bsp,
                                             const vec3_t center,
                                             float radius)
{
    if (ShadowFrontend_PointTouchesPVS2(state, bsp, center)) {
        return true;
    }

    visrow_t mask;
    if (!ShadowFrontend_SphereClusters(bsp, center, max(radius, 0.0f), &mask)) {
        return true;
    }
    return ShadowFrontend_MaskOverlap(bsp, &mask, &state->cached_pvs2_mask);
}

static bool ShadowFrontend_LightTouchesPVS2(shadow_frontend_state_t *state,
                                            const bsp_t *bsp,
                                            const dlight_t *dl)
{
    if (!state->cached_pvs2_valid || !bsp || !dl) {
        return true;
    }

    if (ShadowFrontend_SphereTouchesPVS2(state, bsp, dl->origin,
                                         max(dl->radius, 0.0f))) {
        return true;
    }
    return ShadowFrontend_SphereTouchesPVS2(state, bsp, dl->sphere,
                                            max(dl->sphere[3], dl->radius));
}

static uint32_t ShadowFrontend_LightOwnerId(const dlight_t *dl, int index)
{
    if (dl->shadow == DL_SHADOW_DYNAMIC) {
        uint32_t id = dl->shadow_stable_id;
        if (id) {
            id = ShadowFrontend_HashU32(2166136261u, 0x44594e4bu);
            id = ShadowFrontend_HashU32(id, dl->shadow_stable_id);
        } else {
            id = 2166136261u;
            id = ShadowFrontend_HashU32(id, 0x44594e41u);
            id = ShadowFrontend_HashU32(id, (uint32_t)index);
            id = ShadowFrontend_HashU32(id, (uint32_t)dl->light_type);
            id = ShadowFrontend_HashU32(id, (uint32_t)dl->shadow_owner_entity);
            id = ShadowFrontend_HashU32(id, (uint32_t)dl->shadow_source_index);
        }
        return id ? id : 1u;
    }

    uint32_t hash = 2166136261u;
    hash = ShadowFrontend_HashU32(hash, (uint32_t)index);
    hash = ShadowFrontend_HashU32(hash, (uint32_t)dl->shadow_owner_entity);
    hash = ShadowFrontend_HashU32(hash, (uint32_t)dl->shadow_source_index);
    hash = ShadowFrontend_HashU32(hash, (uint32_t)dl->shadow_resolution);
    hash = ShadowFrontend_HashU32(hash, (uint32_t)dl->shadow_lightstyle);
    hash = ShadowFrontend_HashU32(hash, dl->shadow_ignore_owner_casters ? 1u : 0u);
    hash = ShadowFrontend_HashFloatQ(hash, dl->origin[0], 8.0f);
    hash = ShadowFrontend_HashFloatQ(hash, dl->origin[1], 8.0f);
    hash = ShadowFrontend_HashFloatQ(hash, dl->origin[2], 8.0f);
    hash = ShadowFrontend_HashFloatQ(hash, dl->radius, 8.0f);
    hash = ShadowFrontend_HashFloatQ(hash, dl->intensity, 1024.0f);
    hash = ShadowFrontend_HashU32(hash, (uint32_t)dl->shadow);
    hash = ShadowFrontend_HashU32(hash, (uint32_t)dl->light_type);
    return hash ? hash : 1u;
}

// Identity that stays stable while a light moves: configstring index for
// authored shadowlights, owner entity for tracked lights (flashlights),
// otherwise the position-derived owner id. Used only for hysteresis.
static uint32_t ShadowFrontend_LightStableId(const shadow_light_desc_t *light)
{
    if (light->source_configstring >= 0) {
        return 0x43530000u ^ (uint32_t)light->source_configstring;
    }
    if (light->owner_entity > 0) {
        return 0x454e0000u ^ (uint32_t)light->owner_entity;
    }
    return light->owner_id;
}

static bool ShadowFrontend_WasSelectedLastFrame(const shadow_frontend_state_t *state,
                                                uint32_t stable_id)
{
    for (int i = 0; i < state->prev_selected_id_count; i++) {
        if (state->prev_selected_ids[i] == stable_id) {
            return true;
        }
    }
    return false;
}

static void ShadowFrontend_RecordSelectedIds(shadow_frontend_state_t *state)
{
    int count = 0;
    for (int i = 0; i < state->selected_light_count &&
         count < SHADOW_FRONTEND_MAX_LIGHTS; i++) {
        int light_index = state->selected_light_indices[i];
        if (light_index < 0 || light_index >= state->light_count) {
            continue;
        }
        state->prev_selected_ids[count++] =
            ShadowFrontend_LightStableId(&state->lights[light_index]);
    }
    state->prev_selected_id_count = count;
}

static float ShadowFrontend_LightScore(const refdef_t *fd,
                                       const dlight_t *dl,
                                       dlight_shadow_t source_shadow)
{
    vec3_t delta;
    VectorSubtract(dl->sphere, fd->vieworg, delta);
    float dist = sqrtf(max(VectorLengthSquared(delta), 1.0f));
    float radius = max(dl->sphere[3], dl->radius);
    float projected = radius / max(dist, 1.0f);
    float score = max(dl->intensity, 0.05f) * (radius + 64.0f) * (1.0f + projected);

    if (source_shadow == DL_SHADOW_LIGHT) {
        score *= 1.35f;
    } else if (source_shadow == DL_SHADOW_DYNAMIC) {
        score *= 1.05f;
    }

    return score;
}

static void ShadowFrontend_InsertSelected(shadow_frontend_state_t *state,
                                          int light_index)
{
    float score = state->lights[light_index].score;
    int pos = state->selected_light_count;
    while (pos > 0) {
        int prev = state->selected_light_indices[pos - 1];
        if (state->lights[prev].score > score) {
            break;
        }
        if (state->lights[prev].score == score &&
            state->lights[prev].source_index < state->lights[light_index].source_index) {
            break;
        }
        state->selected_light_indices[pos] = prev;
        pos--;
    }
    state->selected_light_indices[pos] = light_index;
    state->selected_light_count++;
}

static void ShadowFrontend_CollectLights(shadow_frontend_state_t *state,
                                         const refdef_t *fd,
                                         const bsp_t *bsp,
                                         const shadow_frontend_policy_t *policy)
{
    if (!fd || !fd->dlights || fd->num_dlights <= 0 || policy->max_lights <= 0) {
        return;
    }

    int light_capacity = SHADOW_FRONTEND_MAX_LIGHTS - (policy->sun_enabled ? 1 : 0);
    light_capacity = max(light_capacity, 0);
    for (int i = 0; i < fd->num_dlights && state->light_count < light_capacity; i++) {
        const dlight_t *dl = &fd->dlights[i];
        if (dl->shadow == DL_SHADOW_NONE) {
            continue;
        }
        if (dl->shadow == DL_SHADOW_DYNAMIC && !policy->dynamic_lights) {
            continue;
        }

        state->stats.candidate_lights++;

        if (!ShadowFrontend_AreaAllowsLight(fd, bsp, dl)) {
            state->stats.area_rejects++;
            continue;
        }
        if (!ShadowFrontend_LightTouchesPVS2(state, bsp, dl)) {
            state->stats.pvs2_rejects++;
            continue;
        }

        shadow_light_desc_t *desc = &state->lights[state->light_count];
        // The trailing cluster mask is 8K and is (re)initialized by
        // ShadowFrontend_UpdateLightInfluenceClusters below; zeroing it here
        // for every candidate is pure memory traffic.
        memset(desc, 0, offsetof(shadow_light_desc_t, influence_clusters));
        desc->source_index = (uint32_t)i;
        desc->owner_id = ShadowFrontend_LightOwnerId(dl, i);
        desc->source_shadow = (dlight_shadow_t)dl->shadow;
        desc->owner_entity = dl->shadow_owner_entity;
        desc->source_configstring = dl->shadow_source_index;
        desc->ignore_owner_casters = dl->shadow_ignore_owner_casters;
        desc->tracked_entity = dl->shadow_owner_entity > 0;
        if (dl->conecos != 0.0f || dl->light_type == DLIGHT_SPOT) {
            desc->light_class = SHADOW_LIGHT_CLASS_CONE;
        } else if (desc->tracked_entity) {
            desc->light_class = SHADOW_LIGHT_CLASS_TRACKED_ENTITY;
        } else {
            desc->light_class = SHADOW_LIGHT_CLASS_POINT;
        }
        VectorCopy(dl->origin, desc->origin);
        VectorCopy(dl->color, desc->color);
        VectorCopy(dl->cone, desc->direction);
        VectorCopy(dl->sphere, desc->influence_sphere);
        desc->influence_sphere[3] = max(dl->sphere[3], 1.0f);
        if (VectorLengthSquared(desc->direction) < 1e-6f) {
            VectorSet(desc->direction, 0.0f, 0.0f, -1.0f);
        } else {
            VectorNormalize(desc->direction);
        }
        desc->radius = max(dl->radius, 1.0f);
        desc->intensity = dl->intensity;
        desc->fade_start = dl->fade[0];
        desc->fade_end = dl->fade[1];
        desc->cone_angle = dl->conecos != 0.0f ? acosf(Q_clipf(dl->conecos, -1.0f, 1.0f)) : 0.0f;
        desc->resolution = dl->shadow_resolution > 0
            ? dl->shadow_resolution
            : policy->default_resolution;
        desc->lightstyle = dl->shadow_lightstyle;
        desc->strict_pvs2 = dl->shadow_strict_pvs;
        desc->score = ShadowFrontend_LightScore(fd, dl, (dlight_shadow_t)dl->shadow);
        if (ShadowFrontend_WasSelectedLastFrame(state,
                                                ShadowFrontend_LightStableId(desc))) {
            desc->score *= SHADOW_SELECT_HYSTERESIS;
        }
        ShadowFrontend_UpdateLightInfluenceClusters(bsp, desc);
        if (desc->tracked_entity) {
            state->stats.tracked_entity_lights++;
        }
        if (desc->source_configstring >= 0) {
            state->stats.configstring_lights++;
        }

        ShadowFrontend_InsertSelected(state, state->light_count);
        state->light_count++;
    }
}

static void ShadowFrontend_CollectSunLight(shadow_frontend_state_t *state,
                                           const refdef_t *fd,
                                           const shadow_frontend_policy_t *policy)
{
    if (!state || !fd || !policy || !policy->sun_enabled ||
        state->light_count >= SHADOW_FRONTEND_MAX_LIGHTS) {
        return;
    }

    shadow_light_desc_t *desc = &state->lights[state->light_count];
    memset(desc, 0, sizeof(*desc));
    desc->light_class = SHADOW_LIGHT_CLASS_SUN;
    desc->source_index = UINT32_MAX;
    desc->owner_id = 0x53554e31u;
    desc->source_shadow = DL_SHADOW_LIGHT;
    VectorMA(fd->vieworg, -policy->sun_distance, policy->sun_direction, desc->origin);
    VectorSet(desc->color, 1.0f, 1.0f, 1.0f);
    VectorCopy(policy->sun_direction, desc->direction);
    VectorCopy(desc->origin, desc->influence_sphere);
    desc->influence_sphere[3] = max(policy->sun_size, 1.0f);
    desc->radius = policy->sun_distance;
    desc->intensity = 1.0f;
    desc->resolution = policy->sun_resolution;
    desc->strict_pvs2 = false;
    desc->score = FLT_MAX * 0.5f;

    ShadowFrontend_InsertSelected(state, state->light_count);
    state->light_count++;
    state->stats.candidate_lights++;
}

static void ShadowFrontend_FinalizeLightSelection(shadow_frontend_state_t *state,
                                                  const shadow_frontend_policy_t *policy)
{
    if (!state || !policy) {
        return;
    }

    int sorted[SHADOW_FRONTEND_MAX_LIGHTS];
    int sorted_count = state->selected_light_count;
    memcpy(sorted, state->selected_light_indices, sizeof(sorted));

    for (int i = 0; i < state->light_count; i++) {
        state->lights[i].selected = false;
    }

    state->selected_light_count = 0;
    int local_selected = 0;
    for (int i = 0; i < sorted_count; i++) {
        int light_index = sorted[i];
        if (light_index < 0 || light_index >= state->light_count) {
            continue;
        }

        shadow_light_desc_t *light = &state->lights[light_index];
        if (light->light_class != SHADOW_LIGHT_CLASS_SUN &&
            local_selected >= policy->max_lights) {
            continue;
        }

        light->selected = true;
        state->selected_light_indices[state->selected_light_count++] = light_index;
        if (light->light_class != SHADOW_LIGHT_CLASS_SUN) {
            local_selected++;
        }
    }

    state->stats.selected_lights = state->selected_light_count;
}

static void ShadowFrontend_SaveFrozenSelection(shadow_frontend_state_t *state)
{
    state->frozen_selected_light_count = state->selected_light_count;
    for (int i = 0; i < state->selected_light_count; i++) {
        int light_index = state->selected_light_indices[i];
        state->frozen_owner_ids[i] = state->lights[light_index].owner_id;
    }
    state->frozen_selection_valid = state->selected_light_count > 0;
}

static void ShadowFrontend_ApplyFrozenSelection(shadow_frontend_state_t *state)
{
    if (!state->frozen_selection_valid) {
        ShadowFrontend_SaveFrozenSelection(state);
        return;
    }

    int frozen_count = state->frozen_selected_light_count;
    int selected_count = 0;
    for (int i = 0; i < state->light_count; i++) {
        state->lights[i].selected = false;
    }

    for (int f = 0; f < frozen_count && selected_count < SHADOW_FRONTEND_MAX_LIGHTS; f++) {
        uint32_t owner_id = state->frozen_owner_ids[f];
        for (int i = 0; i < state->light_count; i++) {
            if (state->lights[i].owner_id == owner_id) {
                state->lights[i].selected = true;
                state->selected_light_indices[selected_count++] = i;
                break;
            }
        }
    }

    if (!selected_count) {
        for (int i = 0; i < state->selected_light_count; i++) {
            int light_index = state->selected_light_indices[i];
            if (light_index >= 0 && light_index < state->light_count) {
                state->lights[light_index].selected = true;
            }
        }
        ShadowFrontend_SaveFrozenSelection(state);
        return;
    }

    state->selected_light_count = selected_count;
    state->stats.selected_lights = selected_count;
}

static bool ShadowFrontend_EntityCasts(const entity_t *ent,
                                       const shadow_frontend_policy_t *policy)
{
    if (!ent) {
        return false;
    }
    if (ent->flags & (RF_BEAM | RF_NOSHADOW | RF_FLARE)) {
        return false;
    }
    if (ent->flags & RF_WEAPONMODEL) {
        return false;
    }
    if (!ent->model && !(ent->flags & RF_CASTSHADOW)) {
        return false;
    }
    if ((ent->flags & RF_TRANSLUCENT) && policy->alpha_mode == 0) {
        return false;
    }
    return true;
}

static bool ShadowFrontend_ModelNameMatchesToken(const char *model_name,
                                                 const char *token,
                                                 size_t token_len)
{
    if (!model_name || !*model_name || !token || !token_len) {
        return false;
    }

    char scratch[MAX_QPATH];
    token_len = min(token_len, sizeof(scratch) - 1);
    memcpy(scratch, token, token_len);
    scratch[token_len] = '\0';

    if (!scratch[0]) {
        return false;
    }
    return !Q_stricmp(model_name, scratch) ||
           Q_strcasestr(model_name, scratch) != NULL;
}

static bool ShadowFrontend_ModelExcluded(const shadow_backend_ops_t *backend,
                                         const entity_t *ent,
                                         const bsp_t *bsp,
                                         const shadow_frontend_policy_t *policy)
{
    if (!backend || !backend->model_name || !ent || !policy ||
        !policy->model_exclusion_list[0]) {
        return false;
    }

    const char *model_name =
        backend->model_name(backend->userdata, ent->model, bsp);
    if (!model_name || !*model_name) {
        return false;
    }

    const char *s = policy->model_exclusion_list;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == ',' || *s == ';') {
            s++;
        }
        const char *start = s;
        while (*s && *s != ',' && *s != ';' && *s != '\t' && *s != ' ') {
            s++;
        }
        if (ShadowFrontend_ModelNameMatchesToken(model_name, start,
                                                 (size_t)(s - start))) {
            return true;
        }
    }
    return false;
}

static bool ShadowFrontend_ModelNameContains(const char *model_name,
                                             const char *needle)
{
    return model_name && needle && *needle &&
           Q_strcasestr(model_name, needle) != NULL;
}

static bool ShadowFrontend_ModelIsTransientNoShadow(
    const shadow_backend_ops_t *backend,
    const entity_t *ent,
    const bsp_t *bsp)
{
    if (!backend || !backend->model_name || !ent || !ent->model) {
        return false;
    }

    const char *model_name =
        backend->model_name(backend->userdata, ent->model, bsp);
    if (!model_name || !*model_name) {
        return false;
    }

    static const char *const transient_model_tokens[] = {
        "sprites/",
        ".sp2",
        "models/objects/laser/",
        "models/objects/rocket/",
        "models/objects/grenade",
        "models/objects/boomrang/",
        "models/objects/loogy/",
        "models/objects/trap/",
        "models/objects/ionripper/",
        "models/objects/explode/",
        "models/objects/r_explode/",
        "models/objects/bfg_explo/",
        "models/objects/smoke/",
        "models/objects/flash/",
        "models/projectiles/",
        "models/proj/"
    };

    for (size_t i = 0; i < q_countof(transient_model_tokens); i++) {
        if (ShadowFrontend_ModelNameContains(model_name,
                                             transient_model_tokens[i])) {
            return true;
        }
    }
    return false;
}

static void ShadowFrontend_CasterTouchedClusters(shadow_caster_t *caster,
                                                 const bsp_t *bsp)
{
    caster->touched_clusters_valid =
        ShadowFrontend_BoxClusters(bsp, caster->bounds[0], caster->bounds[1],
                                   &caster->touched_clusters);
}

static void ShadowFrontend_TransformEntityBounds(const entity_t *ent,
                                                 const vec3_t local_mins,
                                                 const vec3_t local_maxs,
                                                 vec3_t world_mins,
                                                 vec3_t world_maxs)
{
    ClearBounds(world_mins, world_maxs);

    vec3_t axis[3];
    AnglesToAxis(ent->angles, axis);
    for (int i = 0; i < 3; i++) {
        float scale = ent->scale[i] != 0.0f ? ent->scale[i] : 1.0f;
        VectorScale(axis[i], scale, axis[i]);
    }

    for (int corner = 0; corner < 8; corner++) {
        vec3_t point;
        VectorCopy(ent->origin, point);
        for (int i = 0; i < 3; i++) {
            float local = (corner & BIT(i)) ? local_maxs[i] : local_mins[i];
            VectorMA(point, local, axis[i], point);
        }
        ShadowFrontend_AddPointToBounds(point, world_mins, world_maxs);
    }
}

static float ShadowFrontend_BoundsRadius(const vec3_t mins, const vec3_t maxs)
{
    vec3_t extents;
    VectorSubtract(maxs, mins, extents);
    return VectorLength(extents) * 0.5f;
}

static bool ShadowFrontend_ResolveCasterBounds(const shadow_backend_ops_t *backend,
                                               const entity_t *ent,
                                               const bsp_t *bsp,
                                               vec3_t world_mins,
                                               vec3_t world_maxs,
                                               float *radius)
{
    vec3_t local_mins, local_maxs;
    bool resolved = false;

    if (backend && backend->resolve_caster_bounds) {
        resolved = backend->resolve_caster_bounds(backend->userdata, ent, bsp,
                                                  local_mins, local_maxs);
        if (!resolved) {
            return false;
        }
    } else if (ent->model) {
        VectorSet(local_mins, -32.0f, -32.0f, -32.0f);
        VectorSet(local_maxs,  32.0f,  32.0f,  32.0f);
        resolved = true;
    } else if (ent->flags & RF_CASTSHADOW) {
        VectorSet(local_mins, -16.0f, -16.0f, -24.0f);
        VectorSet(local_maxs,  16.0f,  16.0f,  32.0f);
        resolved = true;
    }

    if (!resolved) {
        return false;
    }

    ShadowFrontend_TransformEntityBounds(ent, local_mins, local_maxs,
                                         world_mins, world_maxs);
    *radius = ShadowFrontend_BoundsRadius(world_mins, world_maxs);
    return *radius > 0.0f;
}

static void ShadowFrontend_CollectCasters(shadow_frontend_state_t *state,
                                          const refdef_t *fd,
                                          const bsp_t *bsp,
                                          const shadow_frontend_policy_t *policy,
                                          const shadow_backend_ops_t *backend)
{
    if (!fd || !fd->entities || fd->num_entities <= 0) {
        return;
    }

    for (int i = 0; i < fd->num_entities && state->caster_count < SHADOW_FRONTEND_MAX_CASTERS; i++) {
        const entity_t *ent = &fd->entities[i];
        if (!ShadowFrontend_EntityCasts(ent, policy)) {
            continue;
        }
        if (ShadowFrontend_ModelExcluded(backend, ent, bsp, policy)) {
            state->stats.model_excluded_casters++;
            continue;
        }
        if (ShadowFrontend_ModelIsTransientNoShadow(backend, ent, bsp)) {
            state->stats.model_excluded_casters++;
            continue;
        }

        vec3_t bounds[2];
        float radius = 0.0f;
        if (!ShadowFrontend_ResolveCasterBounds(backend, ent, bsp,
                                                bounds[0], bounds[1], &radius)) {
            continue;
        }

        shadow_caster_t *caster = &state->casters[state->caster_count];
        // Same as the light path: the trailing cluster mask is initialized
        // by ShadowFrontend_CasterTouchedClusters below.
        memset(caster, 0, offsetof(shadow_caster_t, touched_clusters));
        caster->source_index = (uint32_t)i;
        caster->entity_id = ent->id ? (uint32_t)ent->id : (uint32_t)(i + 1);
        caster->model = ent->model;
        caster->flags = ent->flags;
        caster->entity = *ent;
        caster->entity.next = NULL;
        VectorCopy(ent->origin, caster->origin);
        VectorCopy(ent->oldorigin, caster->oldorigin);
        VectorAvg(bounds[0], bounds[1], caster->center);
        caster->dynamic = !VectorCompare(ent->origin, ent->oldorigin);
        caster->animated = ent->frame != ent->oldframe || ent->backlerp > 0.0f;
        caster->shadow_only = (ent->flags & RF_CASTSHADOW) && (ent->flags & RF_VIEWERMODEL);
        caster->owner_entity = ent->owner_entity;
        caster->radius = max(radius, 1.0f);
        VectorCopy(bounds[0], caster->bounds[0]);
        VectorCopy(bounds[1], caster->bounds[1]);

        ShadowFrontend_CasterTouchedClusters(caster, bsp);

        if (caster->dynamic || caster->animated) {
            state->stats.dynamic_casters++;
        }
        state->caster_count++;
    }

    state->stats.casters = state->caster_count;
}

static bool ShadowFrontend_BoundsSphereIntersectsCaster(const vec3_t center,
                                                        float radius,
                                                        const shadow_caster_t *caster)
{
    float dist2 = 0.0f;
    for (int i = 0; i < 3; i++) {
        if (center[i] < caster->bounds[0][i]) {
            float d = caster->bounds[0][i] - center[i];
            dist2 += d * d;
        } else if (center[i] > caster->bounds[1][i]) {
            float d = center[i] - caster->bounds[1][i];
            dist2 += d * d;
        }
    }
    return dist2 <= radius * radius;
}

static bool ShadowFrontend_SphereIntersectsCaster(const shadow_light_desc_t *light,
                                                  const shadow_caster_t *caster)
{
    return ShadowFrontend_BoundsSphereIntersectsCaster(light->origin,
                                                       light->radius,
                                                       caster);
}

static bool ShadowFrontend_ConeIntersectsCaster(const shadow_light_desc_t *light,
                                                const shadow_caster_t *caster)
{
    if (!ShadowFrontend_BoundsSphereIntersectsCaster(light->influence_sphere,
                                                     max(light->influence_sphere[3], 1.0f),
                                                     caster)) {
        return false;
    }

    vec3_t to_caster;
    VectorSubtract(caster->center, light->origin, to_caster);
    float dist = VectorNormalize(to_caster);
    if (dist <= caster->radius) {
        return true;
    }

    float cos_angle = cosf(max(light->cone_angle, 0.001f));
    float allowance = caster->radius / max(dist, 1.0f);
    return DotProduct(light->direction, to_caster) >= (cos_angle - allowance);
}

static bool ShadowFrontend_LightTouchesCasterClusters(const shadow_frontend_state_t *state,
                                                      const bsp_t *bsp,
                                                      const shadow_light_desc_t *light,
                                                      const shadow_caster_t *caster)
{
    (void)state;

    if (!bsp || !light || !caster ||
        light->light_class == SHADOW_LIGHT_CLASS_SUN ||
        !light->influence_clusters_valid ||
        !caster->touched_clusters_valid) {
        return true;
    }

    if (!ShadowFrontend_MaskOverlap(bsp, &light->influence_clusters,
                                    &caster->touched_clusters)) {
        return false;
    }

    return true;
}

static bool ShadowFrontend_LightIntersectsCaster(const shadow_light_desc_t *light,
                                                 const shadow_caster_t *caster)
{
    if (light->light_class == SHADOW_LIGHT_CLASS_SUN) {
        return true;
    }
    if (light->light_class == SHADOW_LIGHT_CLASS_CONE) {
        return ShadowFrontend_ConeIntersectsCaster(light, caster);
    }
    return ShadowFrontend_SphereIntersectsCaster(light, caster);
}

static bool ShadowFrontend_LightIgnoresOwnerCaster(const shadow_light_desc_t *light,
                                                   const shadow_caster_t *caster)
{
    if (!light->ignore_owner_casters || light->owner_entity <= 0 ||
        caster->owner_entity != light->owner_entity) {
        return false;
    }

    return caster->shadow_only || (caster->flags & RF_WEAPONMODEL);
}

static int ShadowFrontend_SelectViewCasters(shadow_frontend_state_t *state,
                                            const bsp_t *bsp,
                                            const shadow_light_desc_t *light,
                                            int *dynamic_count,
                                            uint32_t *caster_hash)
{
    int count = 0;
    int first = state->view_caster_index_count;
    if (dynamic_count) {
        *dynamic_count = 0;
    }
    if (caster_hash) {
        *caster_hash = 2166136261u;
    }

    for (int i = 0; i < state->caster_count; i++) {
        const shadow_caster_t *caster = &state->casters[i];

        if (ShadowFrontend_LightIgnoresOwnerCaster(light, caster)) {
            state->stats.owner_excluded_casters++;
            continue;
        }

        state->stats.overlap_checks++;
        if (!ShadowFrontend_LightTouchesCasterClusters(state, bsp, light, caster)) {
            continue;
        }

        if (!ShadowFrontend_LightIntersectsCaster(light, caster)) {
            continue;
        }

        if (state->view_caster_index_count >= SHADOW_FRONTEND_MAX_VIEW_CASTERS) {
            break;
        }
        state->view_caster_indices[state->view_caster_index_count++] = i;
        count++;
        if (caster_hash) {
            uint32_t hash = *caster_hash;
            hash = ShadowFrontend_HashU32(hash, caster->entity_id);
            hash = ShadowFrontend_HashU32(hash, caster->source_index);
            hash = ShadowFrontend_HashU32(hash, (uint32_t)caster->model);
            hash = ShadowFrontend_HashU32(hash, (uint32_t)caster->flags);
            hash = ShadowFrontend_HashU32(hash, (uint32_t)(caster->flags >> 32));
            hash = ShadowFrontend_HashU32(hash, caster->dynamic ? 1u : 0u);
            hash = ShadowFrontend_HashU32(hash, caster->animated ? 1u : 0u);
            hash = ShadowFrontend_HashU32(hash, caster->shadow_only ? 1u : 0u);
            hash = ShadowFrontend_HashU32(hash, (uint32_t)caster->owner_entity);
            hash = ShadowFrontend_HashU32(hash, caster->entity.frame);
            hash = ShadowFrontend_HashU32(hash, caster->entity.oldframe);
            hash = ShadowFrontend_HashFloatBits(hash, caster->entity.backlerp);
            hash = ShadowFrontend_HashU32(hash, (uint32_t)caster->entity.skinnum);
            for (int j = 0; j < 3; j++) {
                hash = ShadowFrontend_HashFloatBits(hash, caster->entity.origin[j]);
                hash = ShadowFrontend_HashFloatBits(hash, caster->entity.oldorigin[j]);
                hash = ShadowFrontend_HashFloatBits(hash, caster->entity.angles[j]);
                hash = ShadowFrontend_HashFloatBits(hash, caster->entity.scale[j]);
                hash = ShadowFrontend_HashFloatBits(hash, caster->bounds[0][j]);
                hash = ShadowFrontend_HashFloatBits(hash, caster->bounds[1][j]);
            }
            *caster_hash = hash;
        }
        if (caster->dynamic || caster->animated) {
            if (dynamic_count) {
                (*dynamic_count)++;
            }
        }
    }

    if (!count) {
        state->view_caster_index_count = first;
        if (caster_hash) {
            *caster_hash = 0;
        }
    }
    return count;
}

static shadow_storage_family_t ShadowFrontend_StorageForFilter(shadow_filter_family_t filter)
{
    return (filter == SHADOW_FILTER_VSM || filter == SHADOW_FILTER_EVSM)
        ? SHADOW_STORAGE_MOMENT
        : SHADOW_STORAGE_DEPTH_COMPARE;
}

static uint32_t ShadowFrontend_ProjectionHash(const shadow_light_desc_t *light,
                                              int resolution,
                                              shadow_filter_family_t filter,
                                              shadow_view_type_t view_type,
                                              const shadow_frontend_policy_t *policy)
{
    uint32_t hash = 2166136261u;
    if (light->source_shadow != DL_SHADOW_DYNAMIC) {
        hash = ShadowFrontend_HashFloatQ(hash, light->radius, 8.0f);
        hash = ShadowFrontend_HashFloatQ(hash, light->cone_angle, 4096.0f);
    }
    hash = ShadowFrontend_HashU32(hash, (uint32_t)resolution);
    hash = ShadowFrontend_HashU32(hash, (uint32_t)filter);
    hash = ShadowFrontend_HashU32(hash, (uint32_t)light->light_class);

    // Depth bias is applied while rasterizing the cached page, so a live
    // policy change must not reuse depth produced with the previous values.
    hash = ShadowFrontend_HashFloatQ(hash, policy->slope_bias, 65536.0f);
    hash = ShadowFrontend_HashFloatQ(hash, policy->bias_scale, 65536.0f);

    // Point faces widen their projection according to the filter footprint
    // to hide cube seams. Other view types use softness only while sampling.
    if (view_type == SHADOW_VIEW_POINT_FACE) {
        hash = ShadowFrontend_HashFloatQ(hash, policy->softness, 65536.0f);
    }
    return hash;
}

static void ShadowFrontend_FillCacheKey(shadow_cache_key_t *key,
                                        const shadow_frontend_state_t *state,
                                        const shadow_light_desc_t *light,
                                        shadow_view_type_t view_type,
                                        int face,
                                        int cascade,
                                        int resolution,
                                        shadow_filter_family_t filter,
                                        shadow_storage_family_t storage,
                                        const shadow_frontend_policy_t *policy,
                                        const vec3_t direction)
{
    memset(key, 0, sizeof(*key));
    key->owner_id = light->owner_id;
    key->world_revision = state->world_revision;
    key->projection_hash = ShadowFrontend_ProjectionHash(
        light, resolution, filter, view_type, policy);
    if (light->source_shadow == DL_SHADOW_DYNAMIC) {
        // Dynamic effect lights are re-rendered every frame; including their
        // moving origin/direction in the residency key only churns pages and
        // causes visible selection flicker as projectiles cross quantization
        // cells.
        for (int i = 0; i < 3; i++) {
            key->origin_q[i] = 0;
            key->direction_q[i] = 0;
        }
    } else {
        for (int i = 0; i < 3; i++) {
            key->origin_q[i] = ShadowFrontend_Quantize(light->origin[i], 8.0f);
            key->direction_q[i] = ShadowFrontend_Quantize(direction[i], 32767.0f);
        }
    }
    key->resolution = (uint16_t)resolution;
    key->view_type = (uint8_t)view_type;
    key->face = (int8_t)face;
    key->cascade = (int8_t)cascade;
    key->filter_family = (uint8_t)filter;
    key->storage_family = (uint8_t)storage;
}

// Page residency deliberately stays stable for dynamic-effect sources so a
// moving light cannot churn array slots.  A resident page still needs to be
// re-rendered whenever its actual projection changes, however.  Hash the
// complete CPU view contract bit-for-bit rather than treating the broad
// "dynamic" category as permanently dirty.  That preserves correctness for
// sub-texel motion while allowing genuinely unchanged pages to be reused.
static uint32_t ShadowFrontend_ViewContentHash(const shadow_view_desc_t *view)
{
    uint32_t hash = 2166136261u;
    if (!view) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        hash = ShadowFrontend_HashFloatBits(hash, view->origin[i]);
        for (int j = 0; j < 3; j++) {
            hash = ShadowFrontend_HashFloatBits(hash, view->axis[i][j]);
        }
    }
    hash = ShadowFrontend_HashFloatBits(hash, view->fov_x);
    hash = ShadowFrontend_HashFloatBits(hash, view->fov_y);
    hash = ShadowFrontend_HashFloatBits(hash, view->ortho_size);
    hash = ShadowFrontend_HashFloatBits(hash, view->near_z);
    hash = ShadowFrontend_HashFloatBits(hash, view->far_z);
    return hash ? hash : 1u;
}

static bool ShadowFrontend_CacheKeyEqual(const shadow_cache_key_t *a,
                                         const shadow_cache_key_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static int ShadowFrontend_FindResident(const shadow_frontend_state_t *state,
                                       const shadow_cache_key_t *key)
{
    for (int i = 0; i < SHADOW_FRONTEND_MAX_RESIDENT_PAGES; i++) {
        if (state->resident[i].valid &&
            ShadowFrontend_CacheKeyEqual(&state->resident[i].key, key)) {
            return i;
        }
    }
    return -1;
}

static int ShadowFrontend_AllocResident(shadow_frontend_state_t *state,
                                        const shadow_cache_key_t *key,
                                        const shadow_frontend_policy_t *policy,
                                        uint32_t *dirty_reasons)
{
    int local_limit = policy->max_lights * SHADOW_FRONTEND_POINT_FACES;
    int sun_limit = policy->sun_enabled ? policy->sun_cascades : 0;
    int first = 0;
    int limit = local_limit;

    if (key && key->view_type == SHADOW_VIEW_SUN_CASCADE) {
        first = local_limit;
        limit = local_limit + sun_limit;
    }

    first = (int)Q_clipf((float)first, 0.0f,
                         (float)SHADOW_FRONTEND_MAX_RESIDENT_PAGES);
    limit = (int)Q_clipf((float)limit, (float)first + 1.0f,
                         (float)SHADOW_FRONTEND_MAX_RESIDENT_PAGES);

    for (int i = first; i < limit; i++) {
        if (!state->resident[i].valid) {
            state->resident[i].valid = true;
            state->resident[i].key = *key;
            state->resident[i].page.index = (uint32_t)i;
            state->resident[i].page.generation = state->next_page_generation++;
            state->resident[i].dirty_reasons = SHADOW_DIRTY_NEW_PAGE;
            state->resident[i].caster_hash = 0;
            state->resident[i].content_hash = 0;
            *dirty_reasons |= SHADOW_DIRTY_NEW_PAGE;
            return i;
        }
    }

    int victim = first;
    for (int i = first + 1; i < limit; i++) {
        if (state->resident[i].last_used_frame < state->resident[victim].last_used_frame) {
            victim = i;
        }
    }

    state->resident[victim].valid = true;
    state->resident[victim].key = *key;
    state->resident[victim].page.index = (uint32_t)victim;
    state->resident[victim].page.generation = state->next_page_generation++;
    state->resident[victim].dirty_reasons = SHADOW_DIRTY_EVICTION;
    state->resident[victim].caster_hash = 0;
    state->resident[victim].content_hash = 0;
    state->stats.evictions++;
    *dirty_reasons |= SHADOW_DIRTY_EVICTION;
    return victim;
}

static float ShadowFrontend_SunSplitDistance(float near_dist,
                                             float far_dist,
                                             float p)
{
    p = Q_clipf(p, 0.0f, 1.0f);
    near_dist = max(near_dist, 1.0f);
    far_dist = max(far_dist, near_dist + 1.0f);

    float uniform_split = near_dist + (far_dist - near_dist) * p;
    float log_split = near_dist * powf(far_dist / near_dist, p);
    return uniform_split * 0.5f + log_split * 0.5f;
}

static void ShadowFrontend_FitSunCascade(shadow_view_desc_t *view,
                                         const refdef_t *fd,
                                         const shadow_frontend_policy_t *policy,
                                         const vec3_t axis[3])
{
    if (!view || !fd || !policy) {
        return;
    }

    int cascade_count = max(policy->sun_cascades, 1);
    int cascade = (int)Q_clipf((float)view->cascade, 0.0f,
                               (float)cascade_count - 1.0f);
    float near_dist = 4.0f;
    float far_dist = max(policy->sun_distance, near_dist + 1.0f);
    float split0 = cascade == 0
        ? near_dist
        : ShadowFrontend_SunSplitDistance(near_dist, far_dist,
                                          (float)cascade /
                                              (float)cascade_count);
    float split1 = ShadowFrontend_SunSplitDistance(near_dist, far_dist,
                                                   (float)(cascade + 1) /
                                                       (float)cascade_count);

    vec3_t view_axis[3];
    AnglesToAxis(fd->viewangles, view_axis);
    float tan_x = tanf(DEG2RAD(max(fd->fov_x, 1.0f)) * 0.5f);
    float tan_y = tanf(DEG2RAD(max(fd->fov_y, 1.0f)) * 0.5f);

    vec3_t corners[8];
    int corner_count = 0;
    const float splits[2] = { split0, split1 };
    for (int s = 0; s < 2; s++) {
        float dist = splits[s];
        float half_x = dist * tan_x;
        float half_y = dist * tan_y;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                vec3_t point;
                VectorCopy(fd->vieworg, point);
                VectorMA(point, dist, view_axis[0], point);
                VectorMA(point, (float)sx * half_x, view_axis[1], point);
                VectorMA(point, (float)sy * half_y, view_axis[2], point);
                VectorCopy(point, corners[corner_count++]);
            }
        }
    }

    vec3_t center = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < corner_count; i++) {
        VectorAdd(center, corners[i], center);
    }
    VectorScale(center, 1.0f / (float)corner_count, center);

    float min_side = FLT_MAX, max_side = -FLT_MAX;
    float min_up = FLT_MAX, max_up = -FLT_MAX;
    float min_depth = FLT_MAX, max_depth = -FLT_MAX;
    for (int i = 0; i < corner_count; i++) {
        vec3_t delta;
        VectorSubtract(corners[i], center, delta);
        float side = DotProduct(delta, axis[1]);
        float up = DotProduct(delta, axis[2]);
        float depth = -DotProduct(delta, axis[0]);
        min_side = min(min_side, side);
        max_side = max(max_side, side);
        min_up = min(min_up, up);
        max_up = max(max_up, up);
        min_depth = min(min_depth, depth);
        max_depth = max(max_depth, depth);
    }

    float half_side = max(fabsf(min_side), fabsf(max_side));
    float half_up = max(fabsf(min_up), fabsf(max_up));
    float half = max(half_side, half_up) + 64.0f;
    float ortho_size = max(half * 2.0f, 128.0f);
    if (policy->sun_size > 0.0f) {
        float policy_min_size =
            policy->sun_size * ((float)cascade + 1.0f) /
            (float)cascade_count;
        ortho_size = max(ortho_size, policy_min_size);
    }

    float depth_radius = max(fabsf(min_depth), fabsf(max_depth)) + 512.0f;
    depth_radius = Q_clipf(depth_radius, 128.0f, policy->sun_distance);

    vec3_t origin;
    VectorMA(center, depth_radius, axis[0], origin);

    float texel = ortho_size / (float)max(view->resolution, 1);
    if (texel > 0.0f) {
        float side = DotProduct(origin, axis[1]);
        float up = DotProduct(origin, axis[2]);
        float snapped_side = floorf(side / texel + 0.5f) * texel;
        float snapped_up = floorf(up / texel + 0.5f) * texel;
        VectorMA(origin, snapped_side - side, axis[1], origin);
        VectorMA(origin, snapped_up - up, axis[2], origin);
    }

    VectorCopy(origin, view->origin);
    view->ortho_size = ortho_size;
    view->near_z = 0.0f;
    view->far_z = max(depth_radius * 2.0f, 256.0f);
}

static void ShadowFrontend_AssignPage(shadow_frontend_state_t *state,
                                      shadow_view_desc_t *view,
                                      const shadow_frontend_policy_t *policy)
{
    int resident = ShadowFrontend_FindResident(state, &view->cache_key);
    if (resident < 0) {
        resident = ShadowFrontend_AllocResident(state, &view->cache_key, policy,
                                                &view->dirty_reasons);
    }

    shadow_resident_view_t *slot = &state->resident[resident];
    view->page = slot->page;
    slot->last_used_frame = state->frame_number;

    if (policy->cache_mode == SHADOW_CACHE_NONE) {
        view->dirty_reasons |= SHADOW_DIRTY_LIGHT_PARAMS;
    }
    if (!policy->freeze_dirtying &&
        policy->cache_mode == SHADOW_CACHE_STATIC_REUSE &&
        slot->caster_hash != view->caster_hash) {
        view->dirty_reasons |= SHADOW_DIRTY_MOVED_CASTER;
    }
    if (!policy->freeze_dirtying &&
        policy->cache_mode == SHADOW_CACHE_STATIC_REUSE &&
        slot->content_hash != view->content_hash) {
        view->dirty_reasons |= SHADOW_DIRTY_LIGHT_PARAMS;
    }
    if (slot->dirty_reasons) {
        view->dirty_reasons |= slot->dirty_reasons;
    }
    if (view->dirty_reasons) {
        slot->dirty_reasons = view->dirty_reasons;
    }
}

static void ShadowFrontend_AddView(shadow_frontend_state_t *state,
                                   const refdef_t *fd,
                                   const bsp_t *bsp,
                                   const shadow_frontend_policy_t *policy,
                                   const shadow_light_desc_t *light,
                                   shadow_view_type_t view_type,
                                   int face,
                                   int cascade,
                                   const vec3_t axis[3])
{
    if (state->view_count >= SHADOW_FRONTEND_MAX_VIEWS) {
        return;
    }

    shadow_view_desc_t *view = &state->views[state->view_count];
    memset(view, 0, sizeof(*view));
    view->view_type = view_type;
    view->light_index = light->source_index;
    view->face = face;
    view->cascade = cascade;
    view->caster_first = state->view_caster_index_count;
    view->resolution = view_type == SHADOW_VIEW_SUN_CASCADE
        ? (int)Q_clipf((float)policy->sun_resolution,
                       (float)policy->min_resolution,
                       (float)policy->max_resolution)
        : (int)Q_clipf((float)light->resolution,
                       (float)policy->min_resolution,
                       (float)policy->max_resolution);
    view->filter_family = policy->filter_family;
    view->storage_family = ShadowFrontend_StorageForFilter(policy->filter_family);
    if (view->storage_family == SHADOW_STORAGE_MOMENT) {
        state->stats.moment_views++;
    } else {
        state->stats.depth_compare_views++;
    }
    if (view->filter_family == SHADOW_FILTER_PCSS) {
        state->stats.pcss_views++;
    }
    VectorCopy(light->origin, view->origin);
    for (int i = 0; i < 3; i++) {
        VectorCopy(axis[i], view->axis[i]);
    }
    if (view_type == SHADOW_VIEW_SUN_CASCADE) {
        view->fov_x = 0.0f;
    } else if (view_type == SHADOW_VIEW_CONE) {
        view->fov_x = RAD2DEG(max(light->cone_angle, DEG2RAD(1.0f))) * 2.0f;
    } else {
        // Cube faces get a small guard band beyond 90 degrees so PCF taps
        // near a face boundary stay inside the page instead of clamping,
        // which otherwise shows up as seams along the 45-degree planes.
        float texel_deg = 90.0f / (float)max(view->resolution, 64);
        float guard = Q_clipf(texel_deg * (2.0f * policy->softness + 2.0f),
                              0.5f, 3.0f);
        view->fov_x = 90.0f + 2.0f * guard;
    }
    view->fov_y = view->fov_x;
    view->ortho_size = view_type == SHADOW_VIEW_SUN_CASCADE
        ? policy->sun_size * ((float)cascade + 1.0f) / (float)max(policy->sun_cascades, 1)
        : 0.0f;
    view->near_z = view_type == SHADOW_VIEW_SUN_CASCADE ? 0.0f : 2.0f;
    view->far_z = view_type == SHADOW_VIEW_SUN_CASCADE
        ? policy->sun_distance
        : max(light->radius, view->near_z + 1.0f);

    if (view_type == SHADOW_VIEW_SUN_CASCADE) {
        ShadowFrontend_FitSunCascade(view, fd, policy, axis);
    }

    shadow_light_desc_t key_light = *light;
    if (view_type == SHADOW_VIEW_SUN_CASCADE) {
        VectorCopy(view->origin, key_light.origin);
        key_light.radius = view->far_z;
        key_light.cone_angle = view->ortho_size;
    }

    ShadowFrontend_FillCacheKey(&view->cache_key, state, &key_light, view_type, face,
                                cascade,
                                view->resolution, view->filter_family,
                                view->storage_family, policy, axis[0]);

    if (policy->cache_mode == SHADOW_CACHE_WORLD_ONLY) {
        view->caster_count = 0;
        view->dynamic_caster_count = 0;
        view->caster_hash = 0;
    } else {
        view->caster_count = ShadowFrontend_SelectViewCasters(state, bsp, light,
                                                              &view->dynamic_caster_count,
                                                              &view->caster_hash);
    }
    view->content_hash = ShadowFrontend_ViewContentHash(view);

    ShadowFrontend_AssignPage(state, view, policy);
    if (view->dirty_reasons) {
        state->stats.dirtied_views++;
    } else {
        state->stats.reused_views++;
    }

    state->view_count++;
}

static void ShadowFrontend_BuildViews(shadow_frontend_state_t *state,
                                      const refdef_t *fd,
                                      const bsp_t *bsp,
                                      const shadow_frontend_policy_t *policy)
{
    int pcss_remaining = policy->filter_family == SHADOW_FILTER_PCSS
        ? policy->pcss_max_lights
        : 0;

    for (int i = 0; i < state->selected_light_count; i++) {
        shadow_light_desc_t *light = &state->lights[state->selected_light_indices[i]];
        shadow_filter_family_t saved_filter = policy->filter_family;
        shadow_frontend_policy_t view_policy = *policy;
        if (policy->filter_family == SHADOW_FILTER_PCSS) {
            if (pcss_remaining > 0) {
                pcss_remaining--;
            } else {
                view_policy.filter_family = SHADOW_FILTER_PCF;
                state->stats.pcss_fallback_views++;
            }
        }

        if (light->light_class == SHADOW_LIGHT_CLASS_SUN) {
            vec3_t axis[3];
            VectorCopy(light->direction, axis[0]);
            MakeNormalVectors(axis[0], axis[1], axis[2]);
            for (int cascade = 0; cascade < policy->sun_cascades; cascade++) {
                ShadowFrontend_AddView(state, fd, bsp, &view_policy, light,
                                       SHADOW_VIEW_SUN_CASCADE, -1, cascade,
                                       (const vec3_t *)axis);
            }
        } else if (light->light_class == SHADOW_LIGHT_CLASS_CONE) {
            vec3_t axis[3];
            VectorCopy(light->direction, axis[0]);
            MakeNormalVectors(axis[0], axis[1], axis[2]);
            ShadowFrontend_AddView(state, fd, bsp, &view_policy, light,
                                   SHADOW_VIEW_CONE, 0, -1, (const vec3_t *)axis);
        } else {
            for (int face = 0; face < SHADOW_FRONTEND_POINT_FACES; face++) {
                ShadowFrontend_AddView(state, fd, bsp, &view_policy, light,
                                       SHADOW_VIEW_POINT_FACE, face, -1,
                                       shadow_point_face_axis[face]);
            }
        }

        (void)saved_filter;
    }

    state->stats.views = state->view_count;
}

static bool ShadowFrontend_BackendSupportsView(const shadow_backend_ops_t *backend,
                                               const shadow_view_desc_t *view)
{
    if (!backend) {
        return false;
    }
    if (view->storage_family == SHADOW_STORAGE_DEPTH_COMPARE &&
        !backend->supports_depth_compare_pages) {
        return false;
    }
    if (view->storage_family == SHADOW_STORAGE_MOMENT &&
        !backend->supports_moment_pages) {
        return false;
    }
    if (!backend->supports_array_2d_pages && !backend->supports_cube_array_pages) {
        return false;
    }
    if (backend->max_resolution > 0 && view->resolution > backend->max_resolution) {
        return false;
    }
    if (backend->max_pages > 0 && (int)view->page.index >= backend->max_pages) {
        return false;
    }
    return true;
}

static void ShadowFrontend_RunBackend(shadow_frontend_state_t *state,
                                      const shadow_frontend_policy_t *policy,
                                      const shadow_backend_ops_t *backend)
{
    if (!backend) {
        return;
    }

    uint32_t required_page_count = 1;
    for (int i = 0; i < state->view_count; i++) {
        const shadow_view_desc_t *view = &state->views[i];
        if (!ShadowFrontend_BackendSupportsView(backend, view)) {
            continue;
        }
        required_page_count = max(required_page_count, view->page.index + 1);
    }

    if (backend->begin_frame) {
        backend->begin_frame(backend->userdata, policy, required_page_count);
    }

    for (int i = 0; i < state->view_count; i++) {
        shadow_view_desc_t *view = &state->views[i];
        if (!ShadowFrontend_BackendSupportsView(backend, view)) {
            state->stats.unsupported_views++;
            continue;
        }
        bool backend_allocated = false;
        if (backend->ensure_page && backend->ensure_page(backend->userdata, view)) {
            state->stats.backend_allocations++;
            backend_allocated = true;
        }
        if (policy->cache_mode != SHADOW_CACHE_NONE && !view->dirty_reasons &&
            !backend_allocated) {
            continue;
        }
        if (backend->render_view &&
            backend->render_view(backend->userdata, view,
                                 state->casters,
                                 &state->view_caster_indices[view->caster_first],
                                 view->caster_count)) {
            state->stats.backend_rendered_views++;
            if (!policy->freeze_dirtying) {
                int resident = ShadowFrontend_FindResident(state, &view->cache_key);
                if (resident >= 0) {
                    state->resident[resident].dirty_reasons = SHADOW_DIRTY_NONE;
                    state->resident[resident].caster_hash = view->caster_hash;
                    state->resident[resident].content_hash = view->content_hash;
                }
            }
        }
    }

    if (backend->end_frame) {
        backend->end_frame(backend->userdata, &state->stats);
    }
}

void ShadowFrontend_BuildFrame(shadow_frontend_state_t *state,
                               const refdef_t *fd,
                               const bsp_t *world_bsp,
                               const shadow_frontend_policy_t *policy,
                               const shadow_backend_ops_t *backend)
{
    if (!state || !policy) {
        return;
    }

    state->frame_number++;
    state->world_bsp = world_bsp;
    state->world_revision = ShadowFrontend_WorldRevision(world_bsp);
    state->light_count = 0;
    state->selected_light_count = 0;
    state->caster_count = 0;
    state->view_count = 0;
    state->view_caster_index_count = 0;
    memset(state->selected_light_indices, 0, sizeof(state->selected_light_indices));
    memset(&state->stats, 0, sizeof(state->stats));

    if (!policy->enabled || !fd || (fd->rdflags & RDF_NOWORLDMODEL)) {
        ShadowFrontend_RunBackend(state, policy, backend);
        return;
    }

    ShadowFrontend_UpdatePVS2(state, fd, world_bsp);
    ShadowFrontend_CollectLights(state, fd, world_bsp, policy);
    ShadowFrontend_CollectSunLight(state, fd, policy);
    ShadowFrontend_FinalizeLightSelection(state, policy);
    if (policy->freeze_selection) {
        ShadowFrontend_ApplyFrozenSelection(state);
    } else {
        ShadowFrontend_SaveFrozenSelection(state);
    }
    ShadowFrontend_RecordSelectedIds(state);
    ShadowFrontend_CollectCasters(state, fd, world_bsp, policy, backend);
    ShadowFrontend_BuildViews(state, fd, world_bsp, policy);
    ShadowFrontend_DebugDraw(state, policy);
    ShadowFrontend_RunBackend(state, policy, backend);
}

const shadow_frontend_stats_t *ShadowFrontend_GetStats(const shadow_frontend_state_t *state)
{
    return state ? &state->stats : NULL;
}

const char *ShadowFrontend_FilterName(shadow_filter_family_t filter)
{
    switch (filter) {
    case SHADOW_FILTER_HARD:
        return "hard";
    case SHADOW_FILTER_PCF:
        return "pcf";
    case SHADOW_FILTER_VSM:
        return "vsm";
    case SHADOW_FILTER_EVSM:
        return "evsm";
    case SHADOW_FILTER_PCSS:
        return "pcss";
    default:
        return "unknown";
    }
}

const char *ShadowFrontend_StorageName(shadow_storage_family_t storage)
{
    switch (storage) {
    case SHADOW_STORAGE_DEPTH_COMPARE:
        return "depth-compare";
    case SHADOW_STORAGE_MOMENT:
        return "moment";
    default:
        return "unknown";
    }
}

const char *ShadowFrontend_DirtyReasonString(uint32_t reasons,
                                             char *buffer,
                                             size_t size)
{
    if (!buffer || !size) {
        return "";
    }

    buffer[0] = '\0';
    if (!reasons) {
        Q_strlcpy(buffer, "clean", size);
        return buffer;
    }

    for (size_t i = 0; i < q_countof(shadow_dirty_names); i++) {
        if (!(reasons & BIT(i))) {
            continue;
        }
        if (buffer[0]) {
            Q_strlcat(buffer, "|", size);
        }
        Q_strlcat(buffer, shadow_dirty_names[i], size);
    }
    return buffer;
}

static const char *ShadowFrontend_LightClassName(shadow_light_class_t light_class)
{
    switch (light_class) {
    case SHADOW_LIGHT_CLASS_POINT:
        return "point";
    case SHADOW_LIGHT_CLASS_CONE:
        return "cone";
    case SHADOW_LIGHT_CLASS_SUN:
        return "sun";
    case SHADOW_LIGHT_CLASS_TRACKED_ENTITY:
        return "tracked-entity";
    default:
        return "unknown";
    }
}

static const char *ShadowFrontend_ViewTypeName(shadow_view_type_t view_type)
{
    switch (view_type) {
    case SHADOW_VIEW_POINT_FACE:
        return "point-face";
    case SHADOW_VIEW_CONE:
        return "cone";
    case SHADOW_VIEW_SUN_CASCADE:
        return "sun-cascade";
    default:
        return "unknown";
    }
}

static void ShadowFrontend_DebugDrawCascade(const shadow_view_desc_t *view,
                                            color_t color,
                                            uint32_t time)
{
    float half = max(view->ortho_size * 0.5f, 64.0f);
    vec3_t p[4];
    VectorCopy(view->origin, p[0]);
    VectorCopy(view->origin, p[1]);
    VectorCopy(view->origin, p[2]);
    VectorCopy(view->origin, p[3]);
    VectorMA(p[0],  half, view->axis[1], p[0]);
    VectorMA(p[0],  half, view->axis[2], p[0]);
    VectorMA(p[1], -half, view->axis[1], p[1]);
    VectorMA(p[1],  half, view->axis[2], p[1]);
    VectorMA(p[2], -half, view->axis[1], p[2]);
    VectorMA(p[2], -half, view->axis[2], p[2]);
    VectorMA(p[3],  half, view->axis[1], p[3]);
    VectorMA(p[3], -half, view->axis[2], p[3]);

    R_AddDebugLine(p[0], p[1], color, time, false);
    R_AddDebugLine(p[1], p[2], color, time, false);
    R_AddDebugLine(p[2], p[3], color, time, false);
    R_AddDebugLine(p[3], p[0], color, time, false);

    vec3_t far_point;
    VectorMA(view->origin, -view->far_z, view->axis[0], far_point);
    R_AddDebugLine(view->origin, far_point, color, time, false);
}

static bool ShadowFrontend_DebugLightMatches(const shadow_light_desc_t *light,
                                             int focus)
{
    if (focus < 0 || !light) {
        return true;
    }
    return (int)light->source_index == focus ||
           light->owner_entity == focus ||
           light->source_configstring == focus;
}

static const shadow_light_desc_t *ShadowFrontend_DebugFindLight(
    const shadow_frontend_state_t *state,
    uint32_t source_index)
{
    if (!state) {
        return NULL;
    }
    for (int i = 0; i < state->light_count; i++) {
        if (state->lights[i].source_index == source_index) {
            return &state->lights[i];
        }
    }
    return NULL;
}

static void ShadowFrontend_DebugDraw(const shadow_frontend_state_t *state,
                                     const shadow_frontend_policy_t *policy)
{
    if (!state || !policy || !policy->debug_draw) {
        return;
    }

    const uint32_t time = 50;
    const int focus = policy->debug_light;
    const int draw = policy->debug_draw;

    if (draw & (SHADOWDBG_DRAW_ALL_LIGHTS | SHADOWDBG_DRAW_SELECTED |
                SHADOWDBG_DRAW_CONES | SHADOWDBG_DRAW_TEXT)) {
        for (int i = 0; i < state->light_count; i++) {
            const shadow_light_desc_t *light = &state->lights[i];
            if (!ShadowFrontend_DebugLightMatches(light, focus)) {
                continue;
            }
            if (light->light_class == SHADOW_LIGHT_CLASS_SUN) {
                continue;
            }

            bool selected = light->selected;
            if ((draw & SHADOWDBG_DRAW_ALL_LIGHTS) ||
                (selected && (draw & SHADOWDBG_DRAW_SELECTED))) {
                color_t color = selected
                    ? COLOR_SETA_U8(COLOR_YELLOW, 180)
                    : COLOR_SETA_U8(COLOR_CYAN, 72);
                R_AddDebugSphere(light->origin, light->radius, color, time, false);
            }

            if ((draw & SHADOWDBG_DRAW_CONES) &&
                light->light_class == SHADOW_LIGHT_CLASS_CONE) {
                vec3_t end;
                VectorMA(light->origin, light->radius, light->direction, end);
                R_AddDebugLine(light->origin, end,
                               COLOR_SETA_U8(COLOR_MAGENTA, 180),
                               time, false);
            }

            if (draw & SHADOWDBG_DRAW_TEXT) {
                R_AddDebugText(light->origin, NULL,
                               va("shadow src=%u class=%s res=%d sel=%d",
                                  light->source_index,
                                  ShadowFrontend_LightClassName(light->light_class),
                                  light->resolution,
                                  selected ? 1 : 0),
                               0.16f,
                               selected ? COLOR_YELLOW : COLOR_CYAN,
                               time, false);
            }
        }
    }

    if (draw & (SHADOWDBG_DRAW_CSM | SHADOWDBG_DRAW_TEXT |
                SHADOWDBG_DRAW_CASTERS | SHADOWDBG_DRAW_WIREFRAMES)) {
        int drawn_casters = 0;
        for (int i = 0; i < state->view_count; i++) {
            const shadow_view_desc_t *view = &state->views[i];
            const shadow_light_desc_t *light =
                ShadowFrontend_DebugFindLight(state, view->light_index);
            if (focus >= 0 && !ShadowFrontend_DebugLightMatches(light, focus)) {
                continue;
            }

            if ((draw & SHADOWDBG_DRAW_CSM) &&
                view->view_type == SHADOW_VIEW_SUN_CASCADE) {
                ShadowFrontend_DebugDrawCascade(
                    view, COLOR_SETA_U8(COLOR_BLUE, 160), time);
            }

            if (draw & SHADOWDBG_DRAW_TEXT) {
                R_AddDebugText(view->origin, NULL,
                               va("page=%u dirty=%08x casters=%d",
                                  view->page.index,
                                  view->dirty_reasons,
                                  view->caster_count),
                               0.14f, COLOR_WHITE, time, false);
            }

            if (!(draw & SHADOWDBG_DRAW_CASTERS)) {
                continue;
            }

            int limit = min(view->caster_count, 64 - drawn_casters);
            for (int c = 0; c < limit; c++) {
                int caster_index =
                    state->view_caster_indices[view->caster_first + c];
                if (caster_index < 0 || caster_index >= state->caster_count) {
                    continue;
                }
                const shadow_caster_t *caster = &state->casters[caster_index];
                color_t color = caster->dynamic
                    ? COLOR_SETA_U8(COLOR_GREEN, 150)
                    : COLOR_SETA_U8(COLOR_WHITE, 90);
                R_AddDebugBounds(caster->bounds[0], caster->bounds[1],
                                 color, time, false);
                drawn_casters++;
            }
            if (drawn_casters >= 64) {
                break;
            }
        }
    }
}

void ShadowFrontend_Dump(const shadow_frontend_state_t *state,
                         const shadow_frontend_policy_t *policy,
                         const shadow_backend_ops_t *backend,
                         int focus_light)
{
    if (!state) {
        Com_Printf("Shadow frontend is not initialized.\n");
        return;
    }

    const shadow_frontend_stats_t *s = &state->stats;
    Com_Printf("---- Shadow Frontend Dump ----\n");
    Com_Printf("frame=%u world=%08x backend=%s materialization=%s\n",
               state->frame_number, state->world_revision,
               backend && backend->backend_name ? backend->backend_name : "<none>",
               backend && backend->describe_materialization
                   ? backend->describe_materialization(backend->userdata)
                   : "<unreported>");
    if (policy) {
        Com_Printf("policy enabled=%d filter=%s cache=%d max_lights=%d size=%d sun=%d/%d alpha=%d excluded_models=\"%s\"\n",
                   policy->enabled ? 1 : 0,
                   ShadowFrontend_FilterName(policy->filter_family),
                   policy->cache_mode, policy->max_lights,
                   policy->default_resolution,
                   policy->sun_enabled ? 1 : 0, policy->sun_cascades,
                   policy->alpha_mode, policy->model_exclusion_list);
        Com_Printf("policy bias_slope=%.4g normal_offset=%.4g bias_scale=%.4g softness=%.4g sun_distance=%.4g sun_size=%.4g\n",
                   policy->slope_bias, policy->normal_offset,
                   policy->bias_scale, policy->softness,
                   policy->sun_distance, policy->sun_size);
    }
    Com_Printf("stats candidates=%d selected=%d views=%d casters=%d dynamic=%d pvs2_rejects=%d area_rejects=%d\n",
               s->candidate_lights, s->selected_lights, s->views,
               s->casters, s->dynamic_casters, s->pvs2_rejects,
               s->area_rejects);
    Com_Printf("stats overlap_checks=%d dirtied=%d reused=%d evictions=%d allocations=%d rendered=%d unsupported=%d\n",
               s->overlap_checks, s->dirtied_views, s->reused_views,
               s->evictions, s->backend_allocations,
               s->backend_rendered_views, s->unsupported_views);
    Com_Printf("stats depth_views=%d moment_views=%d pcss_views=%d pcss_fallback=%d tracked_lights=%d configstring_lights=%d model_excluded=%d owner_excluded=%d\n",
               s->depth_compare_views, s->moment_views, s->pcss_views,
               s->pcss_fallback_views, s->tracked_entity_lights,
               s->configstring_lights, s->model_excluded_casters,
               s->owner_excluded_casters);

    Com_Printf("lights (%d):\n", state->light_count);
    for (int i = 0; i < state->light_count; i++) {
        const shadow_light_desc_t *light = &state->lights[i];
        if (focus_light >= 0 && (int)light->source_index != focus_light &&
            light->owner_entity != focus_light &&
            light->source_configstring != focus_light) {
            continue;
        }
        Com_Printf("  [%d] src=%u owner=%u ent=%d cs=%d shadow=%d class=%s tracked=%d selected=%d strict=%d res=%d radius=%.1f intensity=%.2f score=%.3f origin=(%.1f %.1f %.1f)\n",
                   i, light->source_index, light->owner_id,
                   light->owner_entity, light->source_configstring,
                   light->source_shadow,
                   ShadowFrontend_LightClassName(light->light_class),
                   light->tracked_entity ? 1 : 0, light->selected ? 1 : 0,
                   light->strict_pvs2 ? 1 : 0, light->resolution,
                   light->radius, light->intensity, light->score,
                   light->origin[0], light->origin[1], light->origin[2]);
    }

    Com_Printf("views (%d):\n", state->view_count);
    for (int i = 0; i < state->view_count; i++) {
        const shadow_view_desc_t *view = &state->views[i];
        if (focus_light >= 0 && (int)view->light_index != focus_light) {
            continue;
        }
        char dirty[128];
        Com_Printf("  [%d] light=%u type=%s face=%d cascade=%d page=%u:%u filter=%s storage=%s casters=%d dynamic=%d dirty=%s\n",
                   i, view->light_index,
                   ShadowFrontend_ViewTypeName(view->view_type),
                   view->face, view->cascade, view->page.index,
                   view->page.generation,
                   ShadowFrontend_FilterName(view->filter_family),
                   ShadowFrontend_StorageName(view->storage_family),
                   view->caster_count, view->dynamic_caster_count,
                   ShadowFrontend_DirtyReasonString(view->dirty_reasons,
                                                    dirty, sizeof(dirty)));
        if (focus_light >= 0) {
            int print_count = min(view->caster_count, 16);
            for (int c = 0; c < print_count; c++) {
                int caster_index = state->view_caster_indices[view->caster_first + c];
                if (caster_index < 0 || caster_index >= state->caster_count) {
                    continue;
                }
                const shadow_caster_t *caster = &state->casters[caster_index];
                Com_Printf("      caster[%d] ent=%u src=%u dyn=%d anim=%d flags=%016"PRIx64" bounds=(%.1f %.1f %.1f)-(%.1f %.1f %.1f)\n",
                           caster_index, caster->entity_id,
                           caster->source_index, caster->dynamic ? 1 : 0,
                           caster->animated ? 1 : 0, caster->flags,
                           caster->bounds[0][0], caster->bounds[0][1],
                           caster->bounds[0][2], caster->bounds[1][0],
                           caster->bounds[1][1], caster->bounds[1][2]);
            }
            if (view->caster_count > print_count) {
                Com_Printf("      ... %d more casters\n",
                           view->caster_count - print_count);
            }
        }
    }
    Com_Printf("------------------------------\n");
}
