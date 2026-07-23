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

#pragma once

#include "common/bsp.h"
#include "common/cvar.h"
#include "renderer/renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHADOW_FRONTEND_MAX_LIGHTS      (MAX_DLIGHTS + 1)
#define SHADOW_FRONTEND_MAX_CASTERS     MAX_ENTITIES
#define SHADOW_FRONTEND_POINT_FACES     6
#define SHADOW_FRONTEND_MAX_SUN_CASCADES 4
#define SHADOW_FRONTEND_MAX_VIEWS \
    (SHADOW_FRONTEND_MAX_LIGHTS * SHADOW_FRONTEND_POINT_FACES + SHADOW_FRONTEND_MAX_SUN_CASCADES)
#define SHADOW_FRONTEND_MAX_RESIDENT_PAGES SHADOW_FRONTEND_MAX_VIEWS
#define SHADOW_FRONTEND_MAX_VIEW_CASTERS \
    (SHADOW_FRONTEND_MAX_VIEWS * SHADOW_FRONTEND_MAX_CASTERS)

typedef enum {
    SHADOW_LIGHT_CLASS_POINT,
    SHADOW_LIGHT_CLASS_CONE,
    SHADOW_LIGHT_CLASS_SUN,
    SHADOW_LIGHT_CLASS_TRACKED_ENTITY
} shadow_light_class_t;

typedef enum {
    SHADOW_VIEW_POINT_FACE,
    SHADOW_VIEW_CONE,
    SHADOW_VIEW_SUN_CASCADE
} shadow_view_type_t;

typedef enum {
    SHADOW_STORAGE_DEPTH_COMPARE,
    SHADOW_STORAGE_MOMENT
} shadow_storage_family_t;

typedef enum {
    SHADOW_FILTER_HARD,
    SHADOW_FILTER_PCF,
    SHADOW_FILTER_VSM,
    SHADOW_FILTER_EVSM,
    SHADOW_FILTER_PCSS
} shadow_filter_family_t;

typedef enum {
    SHADOW_CACHE_NONE = 0,
    SHADOW_CACHE_STATIC_REUSE = 1,
    SHADOW_CACHE_WORLD_ONLY = 2
} shadow_cache_mode_t;

typedef enum {
    SHADOW_DIRTY_NONE = 0,
    SHADOW_DIRTY_MOVED_CASTER = BIT(0),
    SHADOW_DIRTY_ANIMATED_CASTER = BIT(1),
    SHADOW_DIRTY_LIGHT_PARAMS = BIT(2),
    SHADOW_DIRTY_FILTER_FAMILY = BIT(3),
    SHADOW_DIRTY_WORLD_BSP = BIT(4),
    SHADOW_DIRTY_EVICTION = BIT(5),
    SHADOW_DIRTY_NEW_PAGE = BIT(6)
} shadow_dirty_reason_t;

typedef enum {
    SHADOWDBG_DRAW_ALL_LIGHTS = BIT(0),
    SHADOWDBG_DRAW_SELECTED = BIT(1),
    SHADOWDBG_DRAW_CONES = BIT(2),
    SHADOWDBG_DRAW_CASTERS = BIT(3),
    SHADOWDBG_DRAW_CSM = BIT(4),
    SHADOWDBG_DRAW_TEXT = BIT(5),
    SHADOWDBG_DRAW_XRAY = BIT(6),
    SHADOWDBG_DRAW_WIREFRAMES = BIT(7)
} shadow_debug_draw_t;

typedef struct {
    uint32_t index;
    uint32_t generation;
} shadow_page_id_t;

typedef struct {
    uint32_t owner_id;
    uint32_t world_revision;
    uint32_t projection_hash;
    int32_t origin_q[3];
    int32_t direction_q[3];
    uint16_t resolution;
    uint8_t view_type;
    int8_t face;
    int8_t cascade;
    uint8_t filter_family;
    uint8_t storage_family;
} shadow_cache_key_t;

typedef struct {
    shadow_light_class_t light_class;
    uint32_t source_index;
    uint32_t owner_id;
    vec3_t origin;
    vec3_t color;
    vec3_t direction;
    vec4_t influence_sphere;
    float radius;
    float intensity;
    float fade_start;
    float fade_end;
    float cone_angle;
    int resolution;
    int lightstyle;
    int owner_entity;
    int source_configstring;
    dlight_shadow_t source_shadow;
    bool strict_pvs2;
    bool ignore_owner_casters;
    bool tracked_entity;
    bool influence_clusters_valid;
    bool selected;
    float score;
    visrow_t influence_clusters;
} shadow_light_desc_t;

typedef struct {
    uint32_t source_index;
    uint32_t entity_id;
    qhandle_t model;
    uint64_t flags;
    entity_t entity;
    vec3_t origin;
    vec3_t oldorigin;
    vec3_t center;
    vec3_t bounds[2];
    float radius;
    bool dynamic;
    bool animated;
    bool shadow_only;
    int owner_entity;
    bool touched_clusters_valid;
    visrow_t touched_clusters;
} shadow_caster_t;

typedef struct {
    shadow_view_type_t view_type;
    uint32_t light_index;
    int face;
    int cascade;
    int resolution;
    shadow_filter_family_t filter_family;
    shadow_storage_family_t storage_family;
    shadow_cache_key_t cache_key;
    shadow_page_id_t page;
    vec3_t origin;
    vec3_t axis[3];
    float fov_x;
    float fov_y;
    float ortho_size;
    float near_z;
    float far_z;
    uint32_t dirty_reasons;
    int caster_first;
    int caster_count;
    int dynamic_caster_count;
    uint32_t caster_hash;
    uint32_t content_hash;
} shadow_view_desc_t;

typedef struct {
    bool enabled;
    bool dynamic_lights;
    bool freeze_selection;
    bool freeze_dirtying;
    bool sun_enabled;
    int max_lights;
    int default_resolution;
    int min_resolution;
    int max_resolution;
    int pcss_max_lights;
    int sun_cascades;
    int sun_resolution;
    int debug_light;
    int debug_draw;
    int alpha_mode;
    float slope_bias;
    float normal_offset;
    float bias_scale;
    float softness;
    float sun_distance;
    float sun_size;
    vec3_t sun_direction;
    shadow_filter_family_t filter_family;
    shadow_cache_mode_t cache_mode;
    char model_exclusion_list[MAX_STRING_CHARS];
} shadow_frontend_policy_t;

typedef struct {
    cvar_t *enabled;
    cvar_t *size;
    cvar_t *lights;
    cvar_t *dynamic;
    cvar_t *cache_mode;
    cvar_t *filter;
    cvar_t *pcss_max_lights;
    cvar_t *bias_slope;
    cvar_t *normal_offset;
    cvar_t *bias_scale;
    cvar_t *softness;
    cvar_t *debug_light;
    cvar_t *debug_draw;
    cvar_t *freeze_selection;
    cvar_t *freeze_dirtying;
    cvar_t *alpha_mode;
    cvar_t *model_exclusion_list;
    cvar_t *sun_enabled;
    cvar_t *sun_cascades;
    cvar_t *sun_resolution;
    cvar_t *sun_direction;
    cvar_t *sun_distance;
    cvar_t *sun_size;

    cvar_t *alias_enabled;
    cvar_t *alias_size;
    cvar_t *alias_lights;
    cvar_t *alias_dynamic;
    cvar_t *alias_cache_mode;
    cvar_t *alias_filter;
    cvar_t *alias_pcss_max_lights;
    cvar_t *alias_bias_slope;
    cvar_t *alias_normal_offset;
    cvar_t *alias_bias_scale;
    cvar_t *alias_softness;
    cvar_t *alias_debug_light;
    cvar_t *alias_debug_draw;
    cvar_t *alias_freeze_selection;
    cvar_t *alias_freeze_dirtying;
    cvar_t *alias_alpha_mode;
    cvar_t *alias_model_exclusion_list;
    cvar_t *alias_sun_enabled;
    cvar_t *alias_sun_cascades;
    cvar_t *alias_sun_resolution;
    cvar_t *alias_sun_direction;
    cvar_t *alias_sun_distance;
    cvar_t *alias_sun_size;
} shadow_frontend_cvars_t;

typedef struct {
    int candidate_lights;
    int selected_lights;
    int views;
    int casters;
    int dynamic_casters;
    int pvs2_rejects;
    int area_rejects;
    int overlap_checks;
    int dirtied_views;
    int reused_views;
    int evictions;
    int backend_allocations;
    int backend_rendered_views;
    int unsupported_views;
    int depth_compare_views;
    int moment_views;
    int pcss_views;
    int pcss_fallback_views;
    int tracked_entity_lights;
    int configstring_lights;
    int model_excluded_casters;
    int owner_excluded_casters;
} shadow_frontend_stats_t;

typedef struct {
    const char *backend_name;
    bool supports_depth_compare_pages;
    bool supports_moment_pages;
    bool supports_cube_array_pages;
    bool supports_array_2d_pages;
    int max_pages;
    int max_resolution;
    void *userdata;

    // required_page_count is one plus the largest page index in this completed
    // frontend view set (or one when no view is active). Backends can reserve
    // resources before individual ensure_page calls without a mid-frame grow.
    void (*begin_frame)(void *userdata, const shadow_frontend_policy_t *policy,
                        uint32_t required_page_count);
    bool (*resolve_caster_bounds)(void *userdata, const entity_t *ent,
                                  const bsp_t *world_bsp,
                                  vec3_t local_mins, vec3_t local_maxs);
    const char *(*model_name)(void *userdata, qhandle_t model,
                              const bsp_t *world_bsp);
    bool (*ensure_page)(void *userdata, const shadow_view_desc_t *view);
    bool (*render_view)(void *userdata, const shadow_view_desc_t *view,
                        const shadow_caster_t *casters,
                        const int *caster_indices, int caster_count);
    void (*end_frame)(void *userdata, const shadow_frontend_stats_t *stats);
    const char *(*describe_materialization)(void *userdata);
} shadow_backend_ops_t;

typedef struct {
    bool valid;
    shadow_cache_key_t key;
    shadow_page_id_t page;
    uint32_t last_used_frame;
    uint32_t dirty_reasons;
    uint32_t caster_hash;
    uint32_t content_hash;
} shadow_resident_view_t;

typedef struct {
    uint32_t frame_number;
    const bsp_t *world_bsp;
    uint32_t world_revision;

    shadow_light_desc_t lights[SHADOW_FRONTEND_MAX_LIGHTS];
    int light_count;
    int selected_light_indices[SHADOW_FRONTEND_MAX_LIGHTS];
    int selected_light_count;

    shadow_caster_t casters[SHADOW_FRONTEND_MAX_CASTERS];
    int caster_count;

    shadow_view_desc_t views[SHADOW_FRONTEND_MAX_VIEWS];
    int view_count;
    int view_caster_indices[SHADOW_FRONTEND_MAX_VIEW_CASTERS];
    int view_caster_index_count;

    uint32_t frozen_owner_ids[SHADOW_FRONTEND_MAX_LIGHTS];
    int frozen_selected_light_count;
    bool frozen_selection_valid;

    // Stable identities of last frame's selected lights, used to apply a
    // selection hysteresis boost so shadows do not pop in and out as the
    // camera moves across the score boundary.
    uint32_t prev_selected_ids[SHADOW_FRONTEND_MAX_LIGHTS];
    int prev_selected_id_count;

    shadow_resident_view_t resident[SHADOW_FRONTEND_MAX_RESIDENT_PAGES];
    uint32_t next_page_generation;

    const bsp_t *cached_pvs2_bsp;
    int cached_pvs2_cluster1;
    int cached_pvs2_cluster2;
    visrow_t cached_pvs2_mask;
    bool cached_pvs2_valid;

    unsigned guard_visframe;
    int guard_viewcluster1;
    int guard_viewcluster2;
    bool guard_active;

    shadow_frontend_stats_t stats;
} shadow_frontend_state_t;

void ShadowFrontend_Init(shadow_frontend_state_t *state);
void ShadowFrontend_Shutdown(shadow_frontend_state_t *state);

void ShadowFrontend_RegisterCvars(shadow_frontend_cvars_t *vars,
                                  const char *backend_prefix);
void ShadowFrontend_PolicyFromCvars(const shadow_frontend_cvars_t *vars,
                                    shadow_frontend_policy_t *policy);
void ShadowFrontend_DefaultPolicy(shadow_frontend_policy_t *policy);

void ShadowFrontend_BeginMainVisibilityGuard(shadow_frontend_state_t *state,
                                             unsigned visframe,
                                             int viewcluster1,
                                             int viewcluster2);
void ShadowFrontend_EndMainVisibilityGuard(shadow_frontend_state_t *state,
                                           unsigned visframe,
                                           int viewcluster1,
                                           int viewcluster2,
                                           const char *backend_name);

void ShadowFrontend_BuildFrame(shadow_frontend_state_t *state,
                               const refdef_t *fd,
                               const bsp_t *world_bsp,
                               const shadow_frontend_policy_t *policy,
                               const shadow_backend_ops_t *backend);

const shadow_frontend_stats_t *ShadowFrontend_GetStats(const shadow_frontend_state_t *state);

const char *ShadowFrontend_FilterName(shadow_filter_family_t filter);
const char *ShadowFrontend_StorageName(shadow_storage_family_t storage);
const char *ShadowFrontend_DirtyReasonString(uint32_t reasons,
                                             char *buffer,
                                             size_t size);
void ShadowFrontend_Dump(const shadow_frontend_state_t *state,
                         const shadow_frontend_policy_t *policy,
                         const shadow_backend_ops_t *backend,
                         int focus_light);

#ifdef __cplusplus
}
#endif
