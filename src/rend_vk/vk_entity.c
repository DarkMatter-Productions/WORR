/*
Copyright (C) 2026
*/

#include "vk_entity.h"
#include "vk_debug.h"

#include "vk_entity_spv.h"
#include "vk_shadow.h"
#include "vk_ui.h"
#include "vk_world.h"
#include "renderer/view_setup.h"
#include "format/md2.h"
#include "format/sp2.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if USE_MD5
#include <setjmp.h>
#endif

typedef enum {
    VK_MODEL_FREE = 0,
    VK_MODEL_SPRITE,
    VK_MODEL_MD2,
} vk_model_type_t;

typedef struct {
    int width;
    int height;
    int origin_x;
    int origin_y;
    qhandle_t image;
} vk_sprite_frame_t;

typedef struct {
    float pos[3];
    float normal[3];
} vk_md2_gpu_frame_vertex_t;

typedef struct {
    float origin_frontlerp[4];
    float scaled_axis[3][4];
    float normal_axis[3][4];
    float shell[4];
    uint32_t color;
    uint32_t flags;
    uint32_t padding[2];
} vk_md2_gpu_instance_t;

_Static_assert(sizeof(vk_md2_gpu_instance_t) == 144,
               "GPU MD2 instance layout must match vk_entity_gpu_md2.vert");

typedef struct {
    vec3_t mins;
    vec3_t maxs;
} vk_md2_frame_bounds_t;

typedef struct {
    uint32_t num_frames;
    uint32_t num_vertices;
    uint32_t num_indices;
    float *positions;      // [num_frames][num_vertices][3]
    float *normals;        // [num_frames][num_vertices][3]
    float *frame_radii;    // [num_frames], matches GL outline sizing
    vk_md2_frame_bounds_t *frame_bounds; // [num_frames], source MD2 bounds
    float *uv;             // [num_vertices][2]
    uint16_t *indices;     // [num_indices]
    qhandle_t *skins;
    char (*skin_names)[MAX_QPATH]; // kept for MD5 replacement skin lookup
    uint32_t num_skins;

    // Immutable, device-local source mesh data used by the native GPU
    // interpolation path. CPU copies remain available for shadows, outlines,
    // and the guaranteed compatibility fallback.
    VkBuffer gpu_frame_buffer;
    VkDeviceMemory gpu_frame_memory;
    VkBuffer gpu_uv_buffer;
    VkDeviceMemory gpu_uv_memory;
    VkBuffer gpu_index_buffer;
    VkDeviceMemory gpu_index_memory;
    bool gpu_ready;
} vk_md2_t;

#if USE_MD5
enum {
    VK_MD5_MAX_JOINTS = 256,
    VK_MD5_MAX_MESHES = 32,
    VK_MD5_MAX_WEIGHTS = 8192,
    VK_MD5_MAX_FRAMES = 1024,
    VK_MD5_MAX_JOINTNAME = 48,
    VK_MD5_MAX_VERTICES = 65535,
    VK_MD5_MAX_INDICES = VK_MD5_MAX_VERTICES * 3,
};

typedef struct {
    vec3_t normal;
    uint16_t start;
    uint16_t count;
} vk_md5_vertex_t;

typedef struct {
    float st[2];
} vk_md5_tc_t;

typedef struct {
    vec3_t pos;
    float bias;
} vk_md5_weight_t;

typedef struct {
    float pos_bias[4];
    uint32_t joint_index;
    uint32_t padding[3];
} vk_md5_gpu_weight_t;

typedef struct {
    float normal[3];
    float uv[2];
    uint32_t weight_offset;
    uint32_t weight_count;
} vk_md5_gpu_vertex_t;

typedef struct {
    float pos_scale[4];
    float axis[3][4];
} vk_md5_gpu_joint_t;

typedef struct {
    float origin[3];
    uint32_t joint_palette_offset;
    float scaled_axis[3][4];
    float normal_axis[3][4];
    float shell[4];
    uint32_t color;
    uint32_t flags;
    uint32_t padding[2];
} vk_md5_gpu_instance_t;

_Static_assert(sizeof(vk_md5_gpu_weight_t) == 32,
               "GPU MD5 weight layout must match vk_entity_gpu_md5.vert");
_Static_assert(sizeof(vk_md5_gpu_joint_t) == 64,
               "GPU MD5 joint layout must match vk_entity_gpu_md5.vert");
_Static_assert(sizeof(vk_md5_gpu_instance_t) == 144,
               "GPU MD5 instance layout must match vk_entity_gpu_md5.vert");

typedef struct {
    uint32_t num_verts;
    uint32_t num_indices;
    uint32_t num_weights;
    vk_md5_vertex_t *vertices;
    vk_md5_tc_t *tcoords;
    uint16_t *indices;
    vk_md5_weight_t *weights;
    uint8_t *jointnums;
    qhandle_t shader_image;
    uint32_t gpu_weight_offset;
    VkBuffer gpu_vertex_buffer;
    VkDeviceMemory gpu_vertex_memory;
    VkBuffer gpu_index_buffer;
    VkDeviceMemory gpu_index_memory;
    bool gpu_ready;
} vk_md5_mesh_t;

typedef struct {
    vec3_t pos;
    float scale;
    quat_t orient;
    vec3_t axis[3];
} vk_md5_joint_t;

typedef struct {
    bool loaded;
    uint32_t num_meshes;
    uint32_t num_joints;
    uint32_t num_frames;
    uint32_t num_skins;
    vk_md5_mesh_t *meshes;
    vk_md5_joint_t *skeleton_frames; // [num_frames][num_joints]
    qhandle_t *skins; // derived from the MD2 skin names, "md5/" subdirectory
} vk_md5_t;
#endif

typedef struct {
    vk_model_type_t type;
    char name[MAX_QPATH];
    int registration_sequence;
    union {
        struct {
            vk_sprite_frame_t *frames;
            uint32_t num_frames;
        } sprite;
        vk_md2_t md2;
    };
#if USE_MD5
    vk_md5_t md5;
#endif
} vk_model_t;

typedef struct {
    float pos[3];
    float uv[2];
    float lm_uv[2];
    uint32_t color;
    uint32_t flags;
    float normal[3];
} vk_vertex_t;

// Particles use a compact instanced record instead of expanding the same
// camera-facing triangle on the CPU for every particle every frame. The
// vertex shader reconstructs the three legacy billboard vertices from
// gl_VertexIndex and the current view matrix.
typedef struct {
    float origin_scale[4];
    uint32_t color;
} vk_particle_gpu_instance_t;

_Static_assert(sizeof(vk_particle_gpu_instance_t) == 20,
               "GPU particle instance layout must match vk_entity_gpu_particle.vert");

typedef struct {
    float start[3];
    float end[3];
    float right[3];
    uint32_t color;
} vk_beam_gpu_instance_t;

_Static_assert(sizeof(vk_beam_gpu_instance_t) == 40,
               "GPU beam instance layout must match vk_entity_gpu_beam.vert");

// Immutable inline-BSP source data. Unlike the legacy transient entity
// stream, positions and normals remain in model space and are transformed by
// one compact current-frame instance record in the vertex stage.
typedef struct {
    float pos[3];
    float uv[2];
    float lm_uv[2];
    float normal[3];
    float alpha;
    uint32_t flags;
} vk_bmodel_gpu_vertex_t;

typedef struct {
    float origin[3];
    float padding0;
    float scaled_axis[3][4];
    float normal_axis[3][4];
    uint32_t color;
    uint32_t flags;
    uint32_t padding[2];
} vk_bmodel_gpu_instance_t;

_Static_assert(sizeof(vk_bmodel_gpu_vertex_t) == 48,
               "GPU BSP vertex layout must match vk_entity_gpu_bmodel.vert");
_Static_assert(sizeof(vk_bmodel_gpu_instance_t) == 128,
               "GPU BSP instance layout must match vk_entity_gpu_bmodel.vert");

typedef struct {
    uint32_t first_vertex;
    uint32_t vertex_count;
    uint32_t flags;
} vk_bmodel_gpu_face_t;

typedef enum {
    VK_ENTITY_OUTLINE_NONE = 0,
    VK_ENTITY_OUTLINE_MASK,
    VK_ENTITY_OUTLINE_SHELL,
    VK_ENTITY_OUTLINE_CLEAR,
} vk_entity_outline_stage_t;

typedef enum {
    // OpenGL draws inline BSP models and opaque entities before alpha faces.
    VK_ENTITY_SUBMIT_OPAQUE,
    // General translucent entities above the draw-order threshold are part of
    // the refraction snapshot immediately before transparent world faces.
    VK_ENTITY_SUBMIT_ALPHA_BACK,
    // Beams, particles, flare work, and front translucent/weapon entities
    // draw after transparent world surfaces.
    VK_ENTITY_SUBMIT_POST_LIQUID,
    VK_ENTITY_SUBMIT_ALPHA_FRONT,
    VK_ENTITY_SUBMIT_COUNT,
} vk_entity_submit_phase_t;

typedef enum {
    VK_ENTITY_RECORD_ALL,
    VK_ENTITY_RECORD_BEFORE_LIQUID,
    VK_ENTITY_RECORD_POST_LIQUID,
    VK_ENTITY_RECORD_ALPHA_FRONT,
} vk_entity_record_phase_t;

typedef struct {
    uint32_t first_vertex;
    uint32_t vertex_count;
    uint32_t first_index;
    uint32_t index_count;
    VkDescriptorSet set;
    uint32_t query_index;
    bool alpha;
    bool additive;
    bool depth_hack;
    bool weapon_model;
    bool flare;
    bool occlusion;
    // Alpha-tested sprites use no blending and no depth writes, matching
    // GL_DrawSpriteModel's GLS_ALPHATEST_ENABLE | GLS_DEPTHMASK_FALSE state.
    bool cutout;
    // Fully opaque sprites also disable depth writes. This is a distinct
    // native pipeline state: unlike cutouts it keeps blending disabled.
    bool no_depth_write;
    bool indexed;
    bool gpu_md2;
    bool gpu_bmodel;
    bool gpu_particle;
    bool gpu_beam;
#if USE_MD5
    bool gpu_md5;
#endif
    vk_entity_submit_phase_t submit_phase;
    vk_entity_outline_stage_t outline_stage;
    bool outline_no_depth;
    uint32_t vertex_flags;
    // Dynamic line width is draw state, rather than a vertex attribute. Keep
    // the OpenGL-equivalent per-entity value with the batch so compatible
    // cel instances can still coalesce without inheriting another model's
    // camera-distance fade.
    float cel_line_width;
    const vk_md2_t *gpu_md2_model;
    uint32_t gpu_md2_frame;
    uint32_t gpu_md2_oldframe;
    uint32_t first_instance;
    uint32_t instance_count;
#if USE_MD5
    const vk_md5_mesh_t *gpu_md5_mesh;
#endif
} vk_batch_t;

typedef struct {
    float fraction;
    uint32_t timestamp;
    bool pending;
    bool visible;
} vk_flare_state_t;

typedef struct {
    vec3_t origin;
    vec3_t axis[3];
    vec3_t scaled_axis[3];
    vec3_t inv_scale;
} vk_entity_transform_t;

typedef struct {
    cplane_t planes[4];
} vk_entity_cull_frustum_t;

typedef struct {
    // CPU-expanded entity vertices and 16-bit indices share one native
    // per-frame geometry arena. The independently grown regions preserve the
    // existing live-byte upload accounting while reducing buffer/copy/barrier
    // churn on the transient path.
    VkBuffer geometry_buffer;
    VkDeviceMemory geometry_memory;
    size_t geometry_buffer_bytes;
    size_t geometry_vertex_capacity;
    size_t geometry_index_offset;
    size_t geometry_index_capacity;
    VkBuffer geometry_staging_buffer;
    VkDeviceMemory geometry_staging_memory;
    void *geometry_mapped;
    size_t vertex_upload_bytes;
    size_t index_upload_bytes;
    // GPU-resident model instance records share one frame-local arena. Their
    // independently grown regions retain native vertex/storage alignment,
    // while one transfer and barrier make all live ranges visible together.
    VkBuffer instance_buffer;
    VkDeviceMemory instance_memory;
    size_t instance_buffer_bytes;
    VkBuffer instance_staging_buffer;
    VkDeviceMemory instance_staging_memory;
    void *instance_mapped;
    size_t md2_instance_offset;
    size_t md2_instance_capacity;
    size_t md2_instance_upload_bytes;
    size_t bmodel_instance_offset;
    size_t bmodel_instance_capacity;
    size_t bmodel_instance_upload_bytes;
    size_t particle_instance_offset;
    size_t particle_instance_capacity;
    size_t particle_instance_upload_bytes;
    size_t beam_instance_offset;
    size_t beam_instance_capacity;
    size_t beam_instance_upload_bytes;
#if USE_MD5
    size_t md5_instance_offset;
    size_t md5_instance_capacity;
    size_t md5_instance_upload_bytes;
    size_t md5_palette_offset;
    size_t md5_palette_capacity;
    size_t md5_palette_upload_bytes;
#endif
} vk_entity_frame_buffer_t;

typedef struct {
    VkBuffer destination;
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    void *staging_mapped;
    size_t bytes;
    VkAccessFlags destination_access;
    VkPipelineStageFlags destination_stage;
} vk_entity_static_upload_t;

typedef struct {
    vk_context_t *ctx;
    bool initialized;
    bool swapchain_ready;

    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline_opaque;
    VkPipeline pipeline_opaque_no_depth_write;
    VkPipeline pipeline_bloom_extract;
    VkPipeline pipeline_bloom_extract_alpha;
    VkPipeline pipeline_bloom_extract_additive;
    VkPipeline pipeline_bloom_extract_additive_depth_sample;
    VkPipeline pipeline_cutout;
    VkPipeline pipeline_alpha;
    VkPipeline pipeline_additive;
    VkPipeline pipeline_celshading;
    VkPipeline pipeline_depthhack_opaque;
    VkPipeline pipeline_depthhack_opaque_no_depth_write;
    VkPipeline pipeline_depthhack_cutout;
    VkPipeline pipeline_depthhack_alpha;
    VkPipeline pipeline_depthhack_additive;
    VkPipeline pipeline_gpu_md2_opaque;
    VkPipeline pipeline_gpu_md2_bloom_extract;
    VkPipeline pipeline_gpu_md2_bloom_extract_alpha;
    VkPipeline pipeline_gpu_md2_bloom_extract_additive;
    VkPipeline pipeline_gpu_md2_bloom_extract_additive_depth_sample;
    VkPipeline pipeline_gpu_md2_alpha;
    VkPipeline pipeline_gpu_md2_additive;
    VkPipeline pipeline_gpu_md2_celshading;
    VkPipeline pipeline_gpu_md2_depthhack_opaque;
    VkPipeline pipeline_gpu_md2_depthhack_alpha;
    VkPipeline pipeline_gpu_md2_depthhack_additive;
    VkPipeline pipeline_gpu_bmodel_opaque;
    VkPipeline pipeline_gpu_bmodel_fast_lit_opaque;
    VkPipeline pipeline_gpu_bmodel_fast_lit_no_fog_opaque;
    VkPipeline pipeline_gpu_bmodel_texture_replace_opaque;
    VkPipeline pipeline_gpu_bmodel_texture_replace_no_fog_opaque;
    VkPipeline pipeline_gpu_bmodel_bloom_extract;
    VkPipeline pipeline_gpu_bmodel_alpha;
    VkPipeline pipeline_gpu_bmodel_additive;
    VkPipeline pipeline_gpu_particle_alpha;
    VkPipeline pipeline_gpu_particle_additive;
    VkPipeline pipeline_gpu_beam_alpha;
    VkPipeline pipeline_gpu_beam_depthhack_alpha;
#if USE_MD5
    VkPipeline pipeline_gpu_md5_opaque;
    VkPipeline pipeline_gpu_md5_bloom_extract;
    VkPipeline pipeline_gpu_md5_bloom_extract_alpha;
    VkPipeline pipeline_gpu_md5_bloom_extract_additive;
    VkPipeline pipeline_gpu_md5_bloom_extract_additive_depth_sample;
    VkPipeline pipeline_gpu_md5_alpha;
    VkPipeline pipeline_gpu_md5_additive;
    VkPipeline pipeline_gpu_md5_celshading;
    VkPipeline pipeline_gpu_md5_depthhack_opaque;
    VkPipeline pipeline_gpu_md5_depthhack_alpha;
    VkPipeline pipeline_gpu_md5_depthhack_additive;
    VkDescriptorSetLayout gpu_md5_set_layout;
    VkDescriptorPool gpu_md5_descriptor_pool;
    VkDescriptorSet gpu_md5_descriptor_sets[VK_MAX_FRAMES_IN_FLIGHT];
    VkBuffer gpu_md5_weight_buffer;
    VkDeviceMemory gpu_md5_weight_memory;
    size_t gpu_md5_weight_buffer_bytes;
#endif
    VkPipeline pipeline_flare;
    VkPipeline pipeline_occlusion;
    VkPipeline pipeline_outline_mask;
    VkPipeline pipeline_outline_mask_no_depth;
    VkPipeline pipeline_outline_shell_opaque;
    VkPipeline pipeline_outline_shell_alpha;
    VkPipeline pipeline_outline_shell_no_depth_opaque;
    VkPipeline pipeline_outline_shell_no_depth_alpha;
    VkPipeline pipeline_outline_clear;
    VkQueryPool flare_query_pool;
    bool stencil_available;
    bool gpu_md2_available;
    bool gpu_bmodel_available;
    bool gpu_particle_available;
    bool gpu_beam_available;
#if USE_MD5
    bool gpu_md5_available;
#endif

    vk_entity_frame_buffer_t frame_buffers[VK_MAX_FRAMES_IN_FLIGHT];

    vk_vertex_t *vertices;
    uint32_t vertex_count;
    uint32_t vertex_capacity;
    // Alias source meshes already use 16-bit indices. Keep the transient
    // index stream in that native format and apply each batch's vertex base
    // through vkCmdDrawIndexed rather than widening every index every frame.
    uint16_t *indices;
    uint32_t index_count;
    uint32_t index_capacity;

    vk_md2_gpu_instance_t *md2_instances;
    uint32_t md2_instance_count;
    uint32_t md2_instance_capacity;

    vk_bmodel_gpu_instance_t *bmodel_instances;
    uint32_t bmodel_instance_count;
    uint32_t bmodel_instance_capacity;

    vk_particle_gpu_instance_t *particle_instances;
    uint32_t particle_instance_count;
    uint32_t particle_instance_capacity;

    vk_beam_gpu_instance_t *beam_instances;
    uint32_t beam_instance_count;
    uint32_t beam_instance_capacity;

#if USE_MD5
    vk_md5_gpu_instance_t *md5_instances;
    uint32_t md5_instance_count;
    uint32_t md5_instance_capacity;
    vk_md5_gpu_joint_t *md5_joints;
    uint32_t md5_joint_count;
    uint32_t md5_joint_capacity;
    vk_md5_gpu_weight_t *md5_weights;
    uint32_t md5_weight_count;
    uint32_t md5_weight_capacity;
#endif

    vk_batch_t *batches;
    uint32_t batch_count;
    uint32_t batch_capacity;

    vk_flare_state_t flare_states[MAX_EDICTS];
    bool flare_queries_scheduled[MAX_EDICTS];

    renderer_view_push_t frame_push;
    renderer_view_push_t frame_push_weapon;
    VkRect2D frame_view_rect;
    bool frame_uses_view_rect;
    bool frame_active;
    bool frame_weapon_active;
    bool frame_has_depth_hack_batches;
    bool frame_has_outline_batches;
    bool frame_has_flare_queries;
    bool frame_has_flares;
    float current_cel_line_width;
    // Entity recording is split around refractive world surfaces. Track the
    // submit phases that actually emitted native batches so empty phases do
    // not repeatedly traverse the complete frame batch list.
    uint32_t frame_submit_phase_mask;
    // Each live phase also records its normal entity order pass. This avoids
    // three otherwise empty scans in the common opaque-only and
    // view-weapon-only cases without changing the established pass order.
    uint8_t frame_submit_pass_masks[VK_ENTITY_SUBMIT_COUNT];
    vk_entity_submit_phase_t current_submit_phase;
    uint32_t showtris_category;

    vk_model_t models[MAX_MODELS];
    int num_models;
    int registration_sequence;

    qhandle_t white_image;
    VkDescriptorSet white_set;
    qhandle_t particle_image;
    VkDescriptorSet particle_set;
    qhandle_t beam_image;
    VkDescriptorSet beam_set;
    const bsp_t *bmodel_texture_bsp;
    qhandle_t *bmodel_texture_handles;
    VkDescriptorSet *bmodel_texture_sets;
    vec2_t *bmodel_texture_inv_sizes;
    bool *bmodel_texture_transparent;
    int bmodel_texture_count;
    const bsp_t *bmodel_gpu_bsp;
    vk_bmodel_gpu_face_t *bmodel_gpu_faces;
    int bmodel_gpu_face_count;
    VkBuffer bmodel_gpu_vertex_buffer;
    VkDeviceMemory bmodel_gpu_vertex_memory;
    uint32_t bmodel_gpu_vertex_count;
    bool bmodel_gpu_ready;
    bool bmodel_gpu_failed;

#if USE_MD5
    vk_md5_joint_t *temp_skeleton;
    uint32_t temp_skeleton_capacity;
    vk_vertex_t *temp_md5_vertices;
    uint32_t temp_md5_vertex_capacity;
#endif
} vk_entity_state_t;

static vk_entity_state_t vk_entity;
static cvar_t *vk_drawentities;
static cvar_t *vk_draworder;
static cvar_t *vk_partscale;
static cvar_t *vk_particle_style;
static cvar_t *vk_particle_shape;
static cvar_t *vk_particle_gpu_instancing;
static cvar_t *vk_beam_style;
static cvar_t *vk_beam_gpu_instancing;
static cvar_t *vk_flare_fade_speed;
static cvar_t *vk_player_outline_width;
static cvar_t *vk_md2_gpu_lerp;
static cvar_t *vk_bmodel_fast_lit;
static cvar_t *vk_bmodel_fast_lit_no_fog;
static cvar_t *vk_bmodel_texture_replace;
static cvar_t *vk_bmodel_texture_replace_specialization;
static cvar_t *vk_bmodel_binding_cache;
static cvar_t *vk_cull_models;
static cvar_t *vk_celshading;
static cvar_t *vk_showorigins;
#if USE_MD5
static cvar_t *vk_md5_load;
static cvar_t *vk_md5_use;
static cvar_t *vk_md5_distance;
static cvar_t *vk_md5_gpu_skinning;
static jmp_buf vk_md5_jmpbuf;
#endif

static void VK_Entity_MarkCurrentSubmitPhase(void)
{
    vk_entity.frame_submit_phase_mask |= BIT(vk_entity.current_submit_phase);
}

static bool VK_Entity_HasRecordPhase(vk_entity_record_phase_t phase)
{
    const uint32_t phases = vk_entity.frame_submit_phase_mask;
    switch (phase) {
    case VK_ENTITY_RECORD_ALL:
        return phases != 0;
    case VK_ENTITY_RECORD_BEFORE_LIQUID:
        return (phases & (BIT(VK_ENTITY_SUBMIT_OPAQUE) |
                          BIT(VK_ENTITY_SUBMIT_ALPHA_BACK))) != 0;
    case VK_ENTITY_RECORD_POST_LIQUID:
        return (phases & BIT(VK_ENTITY_SUBMIT_POST_LIQUID)) != 0;
    case VK_ENTITY_RECORD_ALPHA_FRONT:
        return (phases & BIT(VK_ENTITY_SUBMIT_ALPHA_FRONT)) != 0;
    }
    return false;
}

static uint8_t VK_Entity_RecordPassMask(vk_entity_record_phase_t phase)
{
    const uint8_t *masks = vk_entity.frame_submit_pass_masks;
    switch (phase) {
    case VK_ENTITY_RECORD_ALL:
        return masks[VK_ENTITY_SUBMIT_OPAQUE] |
               masks[VK_ENTITY_SUBMIT_ALPHA_BACK] |
               masks[VK_ENTITY_SUBMIT_POST_LIQUID] |
               masks[VK_ENTITY_SUBMIT_ALPHA_FRONT];
    case VK_ENTITY_RECORD_BEFORE_LIQUID:
        return masks[VK_ENTITY_SUBMIT_OPAQUE] |
               masks[VK_ENTITY_SUBMIT_ALPHA_BACK];
    case VK_ENTITY_RECORD_POST_LIQUID:
        return masks[VK_ENTITY_SUBMIT_POST_LIQUID];
    case VK_ENTITY_RECORD_ALPHA_FRONT:
        return masks[VK_ENTITY_SUBMIT_ALPHA_FRONT];
    }
    return 0;
}

static VkDescriptorSet VK_Entity_SetForImage(qhandle_t handle);
static bool VK_Entity_EmitTriBlend(const vk_vertex_t *a, const vk_vertex_t *b,
                                   const vk_vertex_t *c, VkDescriptorSet set,
                                   bool alpha, bool additive, bool depth_hack,
                                   bool weapon_model);
static bool VK_Entity_EmitTriCutout(const vk_vertex_t *a, const vk_vertex_t *b,
                                    const vk_vertex_t *c, VkDescriptorSet set,
                                    bool depth_hack, bool weapon_model);
static bool VK_Entity_EmitTriOpaqueNoDepth(const vk_vertex_t *a,
                                           const vk_vertex_t *b,
                                           const vk_vertex_t *c,
                                           VkDescriptorSet set,
                                           bool depth_hack,
                                           bool weapon_model);
static bool VK_Entity_EmitTriSpecial(const vk_vertex_t *a, const vk_vertex_t *b,
                                     const vk_vertex_t *c, VkDescriptorSet set,
                                     uint32_t query_index, bool flare,
                                     bool occlusion);
static bool VK_Entity_EmitTri(const vk_vertex_t *a, const vk_vertex_t *b, const vk_vertex_t *c,
                              VkDescriptorSet set, bool alpha, bool depth_hack, bool weapon_model);
static bool VK_Entity_EnsureVertexCapacity(uint32_t needed);
static bool VK_Entity_EnsureIndexCapacity(uint32_t needed);
static bool VK_Entity_AppendIndexedBatch(uint32_t first_vertex,
                                         uint32_t vertex_count,
                                         uint32_t first_index,
                                         uint32_t index_count,
                                         VkDescriptorSet set,
                                         bool alpha, bool additive,
                                         bool depth_hack, bool weapon_model);
static void VK_Entity_QueueIndexedShowTris(uint32_t first_vertex,
                                           uint32_t first_index,
                                           uint32_t index_count);
static bool VK_Entity_AppendGpuMd2Batch(const vk_md2_t *md2,
                                        uint32_t frame, uint32_t oldframe,
                                        uint32_t instance_index,
                                        VkDescriptorSet set,
                                        bool alpha, bool additive,
                                        bool depth_hack, bool weapon_model,
                                        uint32_t vertex_flags);
static bool VK_Entity_EnsureMd2InstanceCapacity(uint32_t needed);
static bool VK_Entity_EnsureBmodelInstanceCapacity(uint32_t needed);
static bool VK_Entity_EnsureParticleInstanceCapacity(uint32_t needed);
static bool VK_Entity_AppendGpuParticleBatch(uint32_t instance_index,
                                             VkDescriptorSet set,
                                             bool additive);
static bool VK_Entity_EnsureBeamInstanceCapacity(uint32_t needed);
static bool VK_Entity_AppendGpuBeamBatch(uint32_t instance_index,
                                         VkDescriptorSet set,
                                         bool depth_hack);
static bool VK_Entity_EnsureInstanceBuffer(vk_entity_frame_buffer_t *frame,
                                           size_t md2_bytes,
                                           size_t bmodel_bytes,
                                           size_t particle_bytes,
                                           size_t beam_bytes
#if USE_MD5
                                           , size_t md5_instance_bytes,
                                           size_t md5_palette_bytes
#endif
);
static bool VK_Entity_CreateMd2GpuResources(vk_md2_t *md2);
static void VK_Entity_DestroyMd2GpuResources(vk_md2_t *md2);
#if USE_MD5
static bool VK_Entity_AppendGpuMd5Batch(const vk_md5_mesh_t *mesh,
                                        uint32_t instance_index,
                                        VkDescriptorSet set,
                                        bool alpha, bool additive,
                                        bool depth_hack, bool weapon_model,
                                        uint32_t vertex_flags);
static bool VK_Entity_EnsureMd5InstanceCapacity(uint32_t needed);
static bool VK_Entity_EnsureMd5JointCapacity(uint32_t needed);
static bool VK_Entity_EnsureMd5WeightCapacity(uint32_t needed);
static bool VK_Entity_CreateMd5GpuResources(vk_md5_t *md5);
static void VK_Entity_DestroyMd5GpuResources(vk_md5_t *md5);
static void VK_Entity_UpdateMd5DescriptorSets(void);
#endif
static bool VK_Entity_EmitItemColorizeOverlay(uint32_t first_vertex,
                                              uint32_t vertex_count,
                                              VkDescriptorSet set,
                                              const entity_t *ent,
                                              bool depth_hack,
                                              bool weapon_model);
static bool VK_Entity_EmitOutline(uint32_t first_vertex,
                                  uint32_t vertex_count,
                                  const entity_t *ent,
                                  float scale,
                                  bool depth_hack,
                                  bool weapon_model);
static inline float VK_Entity_Alpha(const entity_t *ent);
static color_t VK_Entity_LitColor(const entity_t *ent, bool fullbright,
                                  bool include_dynamic_lights,
                                  float frame_time, int rdflags,
                                  bool *out_no_static_lighting);
static uint32_t VK_Entity_LightingFlags(const entity_t *ent, bool fullbright);
static float VK_Entity_CelShadingLineWidth(const entity_t *ent,
                                           const refdef_t *fd);
static color_t VK_Entity_ShellColor(const entity_t *ent);
static inline float VK_Entity_ShellScale(const entity_t *ent);
static float VK_Entity_OutlineScale(const entity_t *ent, const refdef_t *fd,
                                    const vk_md2_t *md2, uint32_t frame,
                                    uint32_t oldframe, float backlerp,
                                    float frontlerp);
static void VK_Entity_BuildTransform(const entity_t *ent, vk_entity_transform_t *out_transform);
static void VK_Entity_TransformPointWithTransform(const vk_entity_transform_t *transform,
                                                  const vec3_t local, vec3_t out);
static void VK_Entity_TransformPointInverseWithTransform(const vk_entity_transform_t *transform,
                                                         const vec3_t world, vec3_t out);
static void VK_Entity_TransformNormalWithTransform(const vk_entity_transform_t *transform,
                                                   const vec3_t local, vec3_t out);
static void VK_Entity_FaceNormal(const vk_vertex_t tri[3], vec3_t out);
static void VK_Entity_SetTriNormal(vk_vertex_t tri[3], const vec3_t normal);
static qhandle_t VK_Entity_SelectMD2Skin(const entity_t *ent, const vk_md2_t *md2);
static qhandle_t VK_Entity_SelectMD5Skin(const entity_t *ent, const vk_md5_t *md5);
static bool VK_Entity_AddBspModel(const entity_t *ent, const refdef_t *fd, const bsp_t *bsp,
                                  bool depth_hack, bool weapon_model);
static bool VK_Entity_AddParticles(const refdef_t *fd, const vec3_t view_axis[3]);
static void VK_Entity_DestroyParticleTexture(void);
static void VK_Entity_InitParticleTexture(void);
static void VK_Entity_ParticleShapeChanged(cvar_t *self);
static void VK_Entity_ClearBspTextureCache(void);
static bool VK_Entity_EnsureBspTextureCache(const bsp_t *bsp);
static void VK_Entity_DestroyBspGpuGeometry(void);
static bool VK_Entity_EnsureBspGpuGeometry(const bsp_t *bsp);
static void VK_Entity_BuildFramePush(const refdef_t *fd, const bsp_t *world_bsp,
                                     renderer_view_push_t *out_push);
static void VK_Entity_BuildWeaponFramePush(const refdef_t *fd, const bsp_t *world_bsp,
                                           renderer_view_push_t *out_push);
static bool VK_Entity_ResolveAnimationFrames(const refdef_t *fd, uint32_t num_frames,
                                             unsigned frame_in, unsigned oldframe_in,
                                             float backlerp_in,
                                             uint32_t *out_frame, uint32_t *out_oldframe,
                                             float *out_backlerp, float *out_frontlerp);

// Keep in sync with legacy GL particle primitive shape.
#define VK_ENTITY_PARTICLE_SIZE 1.70710678f
#define VK_ENTITY_PARTICLE_SCALE (1.0f / (2.0f * VK_ENTITY_PARTICLE_SIZE))
#define VK_ENTITY_PARTICLE_TEX_SIZE 16
#define VK_ENTITY_BEAM_TEX_SIZE 16
#define VK_ENTITY_BEAM_POINTS 12
#define VK_ENTITY_MIN_LIGHTNING_SEGMENTS 3
#define VK_ENTITY_MAX_LIGHTNING_SEGMENTS 7
#define VK_ENTITY_MIN_LIGHTNING_SEGMENT_LENGTH 16.0f
#define VK_ENTITY_OUTLINE_SCALE 1.02f
#define VK_ENTITY_OUTLINE_WIDTH_DEFAULT 2.0f
#define VK_ENTITY_OUTLINE_WIDTH_MIN 0.5f
#define VK_ENTITY_OUTLINE_WIDTH_MAX 6.0f
#define VK_ENTITY_OUTLINE_SCALE_MAX 3.0f
#define VK_ENTITY_OUTLINE_STENCIL_REF 1u

enum {
    VK_ENTITY_VERTEX_FULLBRIGHT = BIT(0),
    VK_ENTITY_VERTEX_ALPHATEST = BIT(1),
    VK_ENTITY_VERTEX_LIGHTMAP = BIT(2),
    VK_ENTITY_VERTEX_NO_SHADOW = BIT(3),
    VK_ENTITY_VERTEX_NO_DLIGHT = BIT(4),
    VK_ENTITY_VERTEX_INTENSITY = BIT(5),
    VK_ENTITY_VERTEX_DEFAULT_FLARE = BIT(6),
    VK_ENTITY_VERTEX_RIMLIGHT = BIT(7),
    VK_ENTITY_VERTEX_ITEM_COLORIZE = BIT(8),
    VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE = BIT(9),
    VK_ENTITY_VERTEX_GLOWMAP = BIT(10),
    // OpenGL flares use their additive state without glr.fog_bits. Keep this
    // explicit so ordinary entity, beam, particle, and sprite fog receivers
    // continue through the shared fragment path unchanged.
    VK_ENTITY_VERTEX_NO_FOG = BIT(11),
    // The OpenGL bloom MRT writes the fully shaded shell and rim-light
    // receiver, rather than a skin glowmap. Keep their blend contracts
    // explicit for the native extraction pass.
    VK_ENTITY_VERTEX_BLOOM_SHELL = BIT(12),
    VK_ENTITY_VERTEX_BLOOM_RIM = BIT(13),
    // The OpenGL view-weapon pass contributes a substantially dimmer
    // alpha-blended shell signal to its bloom MRT than a world alias shell.
    // Keep that receiver-specific normalization in the native extract shader
    // instead of changing the public bloom controls or normal scene material.
    VK_ENTITY_VERTEX_BLOOM_DEPTHHACK = BIT(14),
    // Per-batch marker only: the GPU inline-BSP fast-light pipeline ignores
    // it in the vertex shader and is selected solely by native C submission.
    VK_ENTITY_VERTEX_GPU_BMODEL_FAST_LIT = BIT(15),
    // Matches GLS_TEXTURE_REPLACE for opaque inline-BSP faces without an
    // authored lightmap: OpenGL draws their base material at identity rather
    // than applying the entity-light fallback or per-pixel receivers.
    VK_ENTITY_VERTEX_TEXTURE_REPLACE = BIT(16),
    // Per-batch marker only: native C submission selects the specialized
    // opaque inline-BSP texture-replace pipeline.
    VK_ENTITY_VERTEX_GPU_BMODEL_TEXTURE_REPLACE = BIT(17),
    // Per-batch marker only: a native black silhouette wireframe replay
    // reproduces OpenGL's alias-model cel pass after the opaque mesh draw.
    VK_ENTITY_VERTEX_CELSHADING = BIT(18),
    // GL_LightPoint's white no-receiver fallback bypasses GL_AdjustColor(),
    // including gl_modulate_entities. Keep that exceptional source contract
    // explicit for CPU-lit model vertices without weakening normal samples.
    VK_ENTITY_VERTEX_NO_ENTITY_MODULATE = BIT(19),
};

static void VK_Entity_MarkCurrentSubmitPass(bool alpha, uint32_t vertex_flags)
{
    uint32_t pass = 0;
    if (alpha) {
        pass = (vertex_flags & VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE) ? 1
             : (vertex_flags & VK_ENTITY_VERTEX_ITEM_COLORIZE) ? 2 : 3;
    }

    VK_Entity_MarkCurrentSubmitPhase();
    vk_entity.frame_submit_pass_masks[vk_entity.current_submit_phase] |=
        (uint8_t)BIT(pass);
}

static bool VK_Entity_Check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) {
        return true;
    }
    Com_SetLastError(va("Vulkan entity %s failed: %d", what, (int)result));
    return false;
}

static bool VK_Entity_ArrayBytes(size_t count, size_t item_size, size_t *out_bytes,
                                 const char *what)
{
    if (!out_bytes) {
        Com_SetLastError("Vulkan entity: invalid allocation size output");
        return false;
    }

    if (item_size && count > SIZE_MAX / item_size) {
        Com_SetLastError(va("Vulkan entity: %s allocation size overflow", what));
        return false;
    }

    *out_bytes = count * item_size;
    return true;
}

static void *VK_Entity_CallocArray(size_t count, size_t item_size, const char *what)
{
    size_t bytes = 0;
    if (!count || !item_size ||
        !VK_Entity_ArrayBytes(count, item_size, &bytes, what)) {
        return NULL;
    }

    void *memory = calloc(1, bytes);
    if (!memory) {
        Com_SetLastError(va("Vulkan entity: out of memory for %s", what));
        return NULL;
    }

    return memory;
}

static void *VK_Entity_ReallocArray(void *ptr, size_t count, size_t item_size,
                                    const char *what)
{
    size_t bytes = 0;
    if (!count || !item_size ||
        !VK_Entity_ArrayBytes(count, item_size, &bytes, what)) {
        return NULL;
    }

    void *memory = realloc(ptr, bytes);
    if (!memory) {
        Com_SetLastError(va("Vulkan entity: out of memory for %s", what));
        return NULL;
    }

    return memory;
}

static bool VK_Entity_BufferRangeAvailable(size_t len, int offset, int count,
                                           size_t item_size, const char *what)
{
    if (offset < 0 || count < 0) {
        Com_SetLastError(va("Vulkan entity: %s has negative range", what));
        return false;
    }

    size_t bytes = 0;
    if (!VK_Entity_ArrayBytes((size_t)count, item_size, &bytes, what)) {
        return false;
    }

    size_t start = (size_t)offset;
    if (start > len || bytes > len - start) {
        Com_SetLastError(va("Vulkan entity: %s range exceeds file length", what));
        return false;
    }

    return true;
}

static bool VK_Entity_MD2VectorOffset(const vk_md2_t *md2, uint32_t frame,
                                      uint32_t vertex, size_t *out_offset)
{
    if (!md2 || !out_offset || frame >= md2->num_frames ||
        vertex >= md2->num_vertices) {
        Com_SetLastError("Vulkan entity: invalid MD2 vector offset");
        return false;
    }

    size_t frame_base = 0;
    if (!VK_Entity_ArrayBytes((size_t)frame, (size_t)md2->num_vertices,
                              &frame_base, "MD2 frame vector offset")) {
        return false;
    }
    if (frame_base > SIZE_MAX - (size_t)vertex) {
        Com_SetLastError("Vulkan entity: MD2 vertex offset overflow");
        return false;
    }

    return VK_Entity_ArrayBytes(frame_base + (size_t)vertex, 3, out_offset,
                                "MD2 vector offset");
}

static bool VK_Entity_GrowCapacity(uint32_t current, uint32_t needed,
                                   uint32_t initial, uint32_t *out_capacity,
                                   const char *what)
{
    (void)what;

    if (!out_capacity) {
        Com_SetLastError("Vulkan entity: invalid capacity output");
        return false;
    }

    uint32_t cap = current ? current : initial;
    if (!cap) {
        cap = needed;
    }
    while (cap < needed) {
        if (cap > UINT32_MAX / 2) {
            cap = needed;
            break;
        }
        cap *= 2;
    }

    *out_capacity = cap;
    return true;
}

#if USE_MD5
typedef struct {
    int parent;
    uint32_t flags;
    uint32_t start_index;
    char name[VK_MD5_MAX_JOINTNAME];
    bool scale_pos;
} vk_md5_joint_info_t;

typedef struct {
    vec3_t pos;
    quat_t orient;
} vk_md5_base_joint_t;

static bool VK_MD5_ComputeNormals(vk_md5_mesh_t *mesh,
                                  const vk_md5_base_joint_t *base_skeleton)
{
    if (!mesh || !base_skeleton || !mesh->num_verts || !mesh->vertices ||
        !mesh->weights || !mesh->jointnums) {
        return true;
    }

    vec3_t *final_vertices = VK_Entity_CallocArray(mesh->num_verts,
                                                    sizeof(*final_vertices),
                                                    "MD5 bind-pose vertices");
    if (!final_vertices) {
        return false;
    }

    hash_map_t *position_normals = HashMap_Create(vec3_t, vec3_t, &HashVec3, NULL);
    HashMap_Reserve(position_normals, mesh->num_verts);

    for (uint32_t i = 0; i < mesh->num_verts; i++) {
        const vk_md5_vertex_t *vert = &mesh->vertices[i];
        for (uint32_t j = 0; j < vert->count; j++) {
            uint32_t weight_index = (uint32_t)vert->start + j;
            const vk_md5_weight_t *weight = &mesh->weights[weight_index];
            const vk_md5_base_joint_t *joint =
                &base_skeleton[mesh->jointnums[weight_index]];

            vec3_t weighted_position;
            Quat_RotatePoint(joint->orient, weight->pos, weighted_position);
            VectorAdd(joint->pos, weighted_position, weighted_position);
            VectorMA(final_vertices[i], weight->bias, weighted_position,
                     final_vertices[i]);
        }
    }

    for (uint32_t i = 0; i + 2 < mesh->num_indices; i += 3) {
        vec3_t d1, d2, normal;
        const vec3_t *p0 = &final_vertices[mesh->indices[i + 0]];
        const vec3_t *p1 = &final_vertices[mesh->indices[i + 1]];
        const vec3_t *p2 = &final_vertices[mesh->indices[i + 2]];

        VectorSubtract(*p2, *p0, d1);
        VectorSubtract(*p1, *p0, d2);
        VectorNormalize(d1);
        VectorNormalize(d2);
        CrossProduct(d1, d2, normal);
        VectorNormalize(normal);
        VectorScale(normal, acosf(DotProduct(d1, d2)), normal);

        for (uint32_t j = 0; j < 3; j++) {
            vec3_t *position = &final_vertices[mesh->indices[i + j]];
            vec3_t *accumulated = HashMap_Lookup(vec3_t, position_normals,
                                                  position);
            if (accumulated) {
                VectorAdd(*accumulated, normal, *accumulated);
            } else {
                HashMap_Insert(position_normals, position, &normal);
            }
        }
    }

    uint32_t normal_count = HashMap_Size(position_normals);
    for (uint32_t i = 0; i < normal_count; i++) {
        vec3_t *normal = HashMap_GetValue(vec3_t, position_normals, i);
        VectorNormalize(*normal);
    }

    for (uint32_t i = 0; i < mesh->num_verts; i++) {
        vk_md5_vertex_t *vert = &mesh->vertices[i];
        vec3_t *normal = HashMap_Lookup(vec3_t, position_normals,
                                        &final_vertices[i]);
        if (!normal) {
            continue;
        }

        for (uint32_t j = 0; j < vert->count; j++) {
            uint32_t weight_index = (uint32_t)vert->start + j;
            const vk_md5_weight_t *weight = &mesh->weights[weight_index];
            const vk_md5_base_joint_t *joint =
                &base_skeleton[mesh->jointnums[weight_index]];
            quat_t inverse;
            vec3_t joint_normal;
            Quat_Conjugate(joint->orient, inverse);
            Quat_RotatePoint(inverse, *normal, joint_normal);
            VectorMA(vert->normal, weight->bias, joint_normal, vert->normal);
        }
    }

    HashMap_Destroy(position_normals);
    free(final_vertices);
    return true;
}

q_noreturn static void VK_MD5_ParseError(const char *text)
{
    Com_SetLastError(va("MD5 parse line %u: %s", com_linenum, text));
    longjmp(vk_md5_jmpbuf, -1);
}

q_noreturn static void VK_MD5_AllocationError(const char *what, bool overflow)
{
    char message[128];
    Q_snprintf(message, sizeof(message), overflow ?
               "%s allocation size overflow" :
               "out of memory allocating %s", what);
    VK_MD5_ParseError(message);
}

static void *VK_MD5_CallocArray(size_t count, size_t item_size, const char *what)
{
    size_t bytes = 0;
    if (!count || !item_size ||
        !VK_Entity_ArrayBytes(count, item_size, &bytes, what)) {
        VK_MD5_AllocationError(what, true);
    }

    void *memory = calloc(1, bytes);
    if (!memory) {
        VK_MD5_AllocationError(what, false);
    }

    return memory;
}

static void VK_MD5_ParseExpect(const char **buffer, const char *expect)
{
    char *token = COM_Parse(buffer);
    if (strcmp(token, expect)) {
        VK_MD5_ParseError(va("expected \"%s\", got \"%s\"", expect, Com_MakePrintable(token)));
    }
}

static float VK_MD5_ParseFloat(const char **buffer)
{
    char *token = COM_Parse(buffer);
    char *endptr = NULL;

    float v = strtof(token, &endptr);
    if (!endptr || endptr == token || *endptr) {
        VK_MD5_ParseError(va("expected float, got \"%s\"", Com_MakePrintable(token)));
    }
    return v;
}

static uint32_t VK_MD5_ParseUint(const char **buffer, uint32_t min_v, uint32_t max_v)
{
    char *token = COM_Parse(buffer);
    char *endptr = NULL;

    unsigned long v = strtoul(token, &endptr, 10);
    if (!endptr || endptr == token || *endptr) {
        VK_MD5_ParseError(va("expected uint, got \"%s\"", Com_MakePrintable(token)));
    }
    if (v < min_v || v > max_v) {
        VK_MD5_ParseError(va("value out of range: %lu", v));
    }
    return (uint32_t)v;
}

static int32_t VK_MD5_ParseInt(const char **buffer, int32_t min_v, int32_t max_v)
{
    char *token = COM_Parse(buffer);
    char *endptr = NULL;

    long v = strtol(token, &endptr, 10);
    if (!endptr || endptr == token || *endptr) {
        VK_MD5_ParseError(va("expected int, got \"%s\"", Com_MakePrintable(token)));
    }
    if (v < min_v || v > max_v) {
        VK_MD5_ParseError(va("value out of range: %ld", v));
    }
    return (int32_t)v;
}

static void VK_MD5_ParseVector(const char **buffer, vec3_t out_vec)
{
    VK_MD5_ParseExpect(buffer, "(");
    out_vec[0] = VK_MD5_ParseFloat(buffer);
    out_vec[1] = VK_MD5_ParseFloat(buffer);
    out_vec[2] = VK_MD5_ParseFloat(buffer);
    VK_MD5_ParseExpect(buffer, ")");
}

static void VK_MD5_Free(vk_md5_t *md5)
{
    if (!md5) {
        return;
    }

    VK_Entity_DestroyMd5GpuResources(md5);

    if (md5->meshes) {
        for (uint32_t i = 0; i < md5->num_meshes; i++) {
            vk_md5_mesh_t *mesh = &md5->meshes[i];
            free(mesh->vertices);
            free(mesh->tcoords);
            free(mesh->indices);
            free(mesh->weights);
            free(mesh->jointnums);
        }
    }

    free(md5->meshes);
    free(md5->skeleton_frames);
    free(md5->skins);
    memset(md5, 0, sizeof(*md5));
}

static bool VK_MD5_ParseMesh(vk_md5_t *out_md5, const char *source)
{
    if (!out_md5 || !source) {
        return false;
    }

    vk_md5_base_joint_t base_joints[VK_MD5_MAX_JOINTS];
    memset(base_joints, 0, sizeof(base_joints));

    vk_md5_t parsed = { 0 };
    const char *s = source;
    com_linenum = 1;

    if (setjmp(vk_md5_jmpbuf)) {
        VK_MD5_Free(&parsed);
        return false;
    }

    VK_MD5_ParseExpect(&s, "MD5Version");
    VK_MD5_ParseExpect(&s, "10");

    VK_MD5_ParseExpect(&s, "commandline");
    COM_SkipToken(&s);

    VK_MD5_ParseExpect(&s, "numJoints");
    parsed.num_joints = VK_MD5_ParseUint(&s, 1, VK_MD5_MAX_JOINTS);

    VK_MD5_ParseExpect(&s, "numMeshes");
    parsed.num_meshes = VK_MD5_ParseUint(&s, 1, VK_MD5_MAX_MESHES);

    VK_MD5_ParseExpect(&s, "joints");
    VK_MD5_ParseExpect(&s, "{");
    for (uint32_t i = 0; i < parsed.num_joints; i++) {
        COM_SkipToken(&s); // name
        COM_SkipToken(&s); // parent
        VK_MD5_ParseVector(&s, base_joints[i].pos);
        VK_MD5_ParseVector(&s, base_joints[i].orient);
        Quat_ComputeW(base_joints[i].orient);
    }
    VK_MD5_ParseExpect(&s, "}");

    parsed.meshes = VK_MD5_CallocArray(parsed.num_meshes, sizeof(*parsed.meshes),
                                       "MD5 meshes");

    for (uint32_t i = 0; i < parsed.num_meshes; i++) {
        vk_md5_mesh_t *mesh = &parsed.meshes[i];

        VK_MD5_ParseExpect(&s, "mesh");
        VK_MD5_ParseExpect(&s, "{");

        VK_MD5_ParseExpect(&s, "shader");
        const char *shader_token = COM_Parse(&s);
        if (shader_token && *shader_token) {
            char shader_name[MAX_QPATH];
            Q_strlcpy(shader_name, shader_token, sizeof(shader_name));
            FS_NormalizePath(shader_name);
            mesh->shader_image = VK_UI_RegisterImage(shader_name, IT_SKIN, IF_NONE);
        }

        VK_MD5_ParseExpect(&s, "numverts");
        mesh->num_verts = VK_MD5_ParseUint(&s, 0, VK_MD5_MAX_VERTICES);
        if (mesh->num_verts) {
            mesh->vertices = VK_MD5_CallocArray(mesh->num_verts,
                                                sizeof(*mesh->vertices),
                                                "MD5 vertices");
            mesh->tcoords = VK_MD5_CallocArray(mesh->num_verts,
                                               sizeof(*mesh->tcoords),
                                               "MD5 texture coordinates");
        }

        for (uint32_t v = 0; v < mesh->num_verts; v++) {
            VK_MD5_ParseExpect(&s, "vert");
            uint32_t vert_index = VK_MD5_ParseUint(&s, 0, mesh->num_verts - 1);

            VK_MD5_ParseExpect(&s, "(");
            mesh->tcoords[vert_index].st[0] = VK_MD5_ParseFloat(&s);
            mesh->tcoords[vert_index].st[1] = VK_MD5_ParseFloat(&s);
            VK_MD5_ParseExpect(&s, ")");

            mesh->vertices[vert_index].start = (uint16_t)VK_MD5_ParseUint(&s, 0, UINT16_MAX);
            mesh->vertices[vert_index].count = (uint16_t)VK_MD5_ParseUint(&s, 0, UINT16_MAX);
        }

        VK_MD5_ParseExpect(&s, "numtris");
        uint32_t num_tris = VK_MD5_ParseUint(&s, 0, VK_MD5_MAX_INDICES / 3);
        if (num_tris && !mesh->num_verts) {
            VK_MD5_ParseError("mesh has triangles but no vertices");
        }
        mesh->num_indices = num_tris * 3;
        if (mesh->num_indices) {
            mesh->indices = VK_MD5_CallocArray(mesh->num_indices,
                                               sizeof(*mesh->indices),
                                               "MD5 indices");
        }
        for (uint32_t t = 0; t < num_tris; t++) {
            VK_MD5_ParseExpect(&s, "tri");
            uint32_t tri_index = VK_MD5_ParseUint(&s, 0, num_tris - 1);
            for (int k = 0; k < 3; k++) {
                mesh->indices[tri_index * 3 + k] = (uint16_t)VK_MD5_ParseUint(&s, 0, mesh->num_verts ? mesh->num_verts - 1 : 0);
            }
        }

        VK_MD5_ParseExpect(&s, "numweights");
        mesh->num_weights = VK_MD5_ParseUint(&s, 0, VK_MD5_MAX_WEIGHTS);
        if (mesh->num_weights) {
            mesh->weights = VK_MD5_CallocArray(mesh->num_weights,
                                               sizeof(*mesh->weights),
                                               "MD5 weights");
            mesh->jointnums = VK_MD5_CallocArray(mesh->num_weights,
                                                 sizeof(*mesh->jointnums),
                                                 "MD5 weight joints");
        }
        for (uint32_t w = 0; w < mesh->num_weights; w++) {
            VK_MD5_ParseExpect(&s, "weight");
            uint32_t weight_index = VK_MD5_ParseUint(&s, 0, mesh->num_weights - 1);
            mesh->jointnums[weight_index] = (uint8_t)VK_MD5_ParseUint(&s, 0, parsed.num_joints - 1);
            mesh->weights[weight_index].bias = VK_MD5_ParseFloat(&s);
            VK_MD5_ParseVector(&s, mesh->weights[weight_index].pos);
        }

        VK_MD5_ParseExpect(&s, "}");

        for (uint32_t v = 0; v < mesh->num_verts; v++) {
            const vk_md5_vertex_t *vert = &mesh->vertices[v];
            if ((uint32_t)vert->start + (uint32_t)vert->count > mesh->num_weights) {
                VK_MD5_ParseError("invalid MD5 vertex weight span");
            }
        }
        if (!VK_MD5_ComputeNormals(mesh, base_joints)) {
            VK_MD5_ParseError("could not compute MD5 vertex normals");
        }
    }

    parsed.loaded = true;
    *out_md5 = parsed;
    return true;
}

static void VK_MD5_BuildFrameSkeleton(const vk_md5_joint_info_t *joint_infos,
                                      const vk_md5_base_joint_t *base_frame,
                                      const float *frame_data,
                                      vk_md5_joint_t *out_skeleton,
                                      uint32_t num_joints)
{
    for (uint32_t i = 0; i < num_joints; i++) {
        vec3_t animated_pos;
        quat_t animated_orient;
        VectorCopy(base_frame[i].pos, animated_pos);
        Vector4Copy(base_frame[i].orient, animated_orient);

        int component_index = 0;
        uint32_t flags = joint_infos[i].flags;
        uint32_t start = joint_infos[i].start_index;

        if (flags & BIT(0))
            animated_pos[0] = frame_data[start + component_index++];
        if (flags & BIT(1))
            animated_pos[1] = frame_data[start + component_index++];
        if (flags & BIT(2))
            animated_pos[2] = frame_data[start + component_index++];
        if (flags & BIT(3))
            animated_orient[0] = frame_data[start + component_index++];
        if (flags & BIT(4))
            animated_orient[1] = frame_data[start + component_index++];
        if (flags & BIT(5))
            animated_orient[2] = frame_data[start + component_index++];

        Quat_ComputeW(animated_orient);

        vk_md5_joint_t *joint = &out_skeleton[i];
        if (joint_infos[i].scale_pos) {
            VectorScale(animated_pos, joint->scale, animated_pos);
        }

        if (joint_infos[i].parent < 0) {
            VectorCopy(animated_pos, joint->pos);
            Vector4Copy(animated_orient, joint->orient);
            Quat_ToAxis(joint->orient, joint->axis);
            continue;
        }

        const vk_md5_joint_t *parent = &out_skeleton[joint_infos[i].parent];
        vec3_t rotated_pos;
        Quat_RotatePoint(parent->orient, animated_pos, rotated_pos);
        VectorAdd(parent->pos, rotated_pos, joint->pos);

        Quat_MultiplyQuat(parent->orient, animated_orient, joint->orient);
        Quat_Normalize(joint->orient);
        Quat_ToAxis(joint->orient, joint->axis);
    }
}

static void VK_MD5_LoadScales(vk_md5_t *md5, const char *path, vk_md5_joint_info_t *joint_infos)
{
    if (!md5 || !path || !joint_infos) {
        return;
    }

    jsmn_parser parser;
    jsmntok_t tokens[4096];
    char *data = NULL;
    int len = FS_LoadFile(path, (void **)&data);
    if (!data) {
        if (len != Q_ERR(ENOENT)) {
            Com_EPrintf("Couldn't load %s: %s\n", path, Q_ErrorString(len));
        }
        return;
    }

    jsmn_init(&parser);
    int ret = jsmn_parse(&parser, data, (size_t)len, tokens, q_countof(tokens));
    if (ret < 0) {
        goto fail;
    }
    if (ret == 0) {
        goto skip;
    }

    const jsmntok_t *tok = &tokens[0];
    if (tok->type != JSMN_OBJECT) {
        goto fail;
    }

    const jsmntok_t *end = tokens + ret;
    tok++;

    while (tok < end) {
        if (tok->type != JSMN_STRING) {
            goto fail;
        }

        int joint_id = -1;
        const char *joint_name = data + tok->start;
        data[tok->end] = 0;
        for (uint32_t i = 0; i < md5->num_joints; i++) {
            if (!strcmp(joint_name, joint_infos[i].name)) {
                joint_id = (int)i;
                break;
            }
        }

        if (joint_id == -1) {
            Com_WPrintf("No such joint \"%s\" in %s\n", Com_MakePrintable(joint_name), path);
        }

        if (++tok == end || tok->type != JSMN_OBJECT) {
            goto fail;
        }

        int num_keys = tok->size;
        if (end - ++tok < num_keys * 2) {
            goto fail;
        }

        for (int i = 0; i < num_keys; i++) {
            const jsmntok_t *key = tok++;
            const jsmntok_t *val = tok++;
            if (key->type != JSMN_STRING || val->type != JSMN_PRIMITIVE) {
                goto fail;
            }

            if (joint_id == -1) {
                continue;
            }

            data[key->end] = 0;
            const char *key_text = data + key->start;
            if (!strcmp(key_text, "scale_positions")) {
                joint_infos[joint_id].scale_pos = data[val->start] == 't';
            } else {
                unsigned frame_id = Q_atoi(key_text);
                if (frame_id < md5->num_frames) {
                    md5->skeleton_frames[(size_t)frame_id * md5->num_joints + (uint32_t)joint_id].scale =
                        Q_atof(data + val->start);
                } else {
                    Com_WPrintf("No such frame %u in %s\n", frame_id, path);
                }
            }
        }
    }

skip:
    FS_FreeFile(data);
    return;

fail:
    Com_EPrintf("Couldn't load %s: Invalid JSON data\n", path);
    FS_FreeFile(data);
}

static bool VK_MD5_ParseAnim(vk_md5_t *md5, const char *source, const char *path)
{
    if (!md5 || !source) {
        return false;
    }

    vk_md5_joint_info_t joint_infos[VK_MD5_MAX_JOINTS];
    vk_md5_base_joint_t base_frame[VK_MD5_MAX_JOINTS];
    float anim_frame_data[VK_MD5_MAX_JOINTS * 6];
    memset(joint_infos, 0, sizeof(joint_infos));
    memset(base_frame, 0, sizeof(base_frame));
    memset(anim_frame_data, 0, sizeof(anim_frame_data));

    const char *s = source;
    com_linenum = 1;

    if (setjmp(vk_md5_jmpbuf)) {
        return false;
    }

    VK_MD5_ParseExpect(&s, "MD5Version");
    VK_MD5_ParseExpect(&s, "10");

    VK_MD5_ParseExpect(&s, "commandline");
    COM_SkipToken(&s);

    VK_MD5_ParseExpect(&s, "numFrames");
    md5->num_frames = VK_MD5_ParseUint(&s, 1, VK_MD5_MAX_FRAMES);

    VK_MD5_ParseExpect(&s, "numJoints");
    uint32_t num_joints = VK_MD5_ParseUint(&s, 1, VK_MD5_MAX_JOINTS);
    if (num_joints != md5->num_joints) {
        VK_MD5_ParseError("numJoints mismatch between mesh and animation");
    }

    VK_MD5_ParseExpect(&s, "frameRate");
    COM_SkipToken(&s);

    VK_MD5_ParseExpect(&s, "numAnimatedComponents");
    uint32_t num_animated_components = VK_MD5_ParseUint(&s, 0, md5->num_joints * 6);

    VK_MD5_ParseExpect(&s, "hierarchy");
    VK_MD5_ParseExpect(&s, "{");
    for (uint32_t i = 0; i < md5->num_joints; i++) {
        COM_ParseToken(&s, joint_infos[i].name, sizeof(joint_infos[i].name), PARSE_FLAG_NONE);
        joint_infos[i].parent = VK_MD5_ParseInt(&s, -1, (int32_t)md5->num_joints - 1);
        joint_infos[i].flags = VK_MD5_ParseUint(&s, 0, UINT32_MAX);
        joint_infos[i].start_index = VK_MD5_ParseUint(&s, 0, num_animated_components);
        joint_infos[i].scale_pos = false;

        int num_components = 0;
        for (int c = 0; c < 6; c++) {
            if (joint_infos[i].flags & BIT(c)) {
                num_components++;
            }
        }

        if (joint_infos[i].start_index + (uint32_t)num_components > num_animated_components) {
            VK_MD5_ParseError("invalid hierarchy animated component span");
        }
        if (joint_infos[i].parent >= (int)i) {
            VK_MD5_ParseError("invalid hierarchy parent ordering");
        }
    }
    VK_MD5_ParseExpect(&s, "}");

    VK_MD5_ParseExpect(&s, "bounds");
    VK_MD5_ParseExpect(&s, "{");
    for (uint32_t i = 0; i < md5->num_frames; i++) {
        vec3_t ignore_min;
        vec3_t ignore_max;
        VK_MD5_ParseVector(&s, ignore_min);
        VK_MD5_ParseVector(&s, ignore_max);
    }
    VK_MD5_ParseExpect(&s, "}");

    VK_MD5_ParseExpect(&s, "baseframe");
    VK_MD5_ParseExpect(&s, "{");
    for (uint32_t i = 0; i < md5->num_joints; i++) {
        VK_MD5_ParseVector(&s, base_frame[i].pos);
        VK_MD5_ParseVector(&s, base_frame[i].orient);
        Quat_ComputeW(base_frame[i].orient);
    }
    VK_MD5_ParseExpect(&s, "}");

    size_t skeleton_count = 0;
    if (!VK_Entity_ArrayBytes((size_t)md5->num_frames, (size_t)md5->num_joints,
                              &skeleton_count, "MD5 skeleton frames")) {
        VK_MD5_AllocationError("MD5 skeleton frames", true);
    }

    md5->skeleton_frames = VK_MD5_CallocArray(skeleton_count,
                                              sizeof(*md5->skeleton_frames),
                                              "MD5 skeleton frames");

    for (size_t i = 0; i < skeleton_count; i++) {
        md5->skeleton_frames[i].scale = 1.0f;
    }

    if (path && *path) {
        char scale_path[MAX_QPATH];
        if (COM_StripExtension(scale_path, path, sizeof(scale_path)) < sizeof(scale_path) &&
            Q_strlcat(scale_path, ".md5scale", sizeof(scale_path)) < sizeof(scale_path)) {
            VK_MD5_LoadScales(md5, scale_path, joint_infos);
        } else {
            Com_WPrintf("MD5 scale path too long: %s\n", scale_path);
        }
    }

    for (uint32_t frame = 0; frame < md5->num_frames; frame++) {
        VK_MD5_ParseExpect(&s, "frame");
        uint32_t frame_index = VK_MD5_ParseUint(&s, 0, md5->num_frames - 1);
        VK_MD5_ParseExpect(&s, "{");
        for (uint32_t i = 0; i < num_animated_components; i++) {
            anim_frame_data[i] = VK_MD5_ParseFloat(&s);
        }
        VK_MD5_ParseExpect(&s, "}");

        VK_MD5_BuildFrameSkeleton(joint_infos, base_frame, anim_frame_data,
                                  &md5->skeleton_frames[(size_t)frame_index * md5->num_joints],
                                  md5->num_joints);
    }
    return true;
}

static bool VK_Entity_LoadMD5Replacement(vk_model_t *model)
{
    if (!model || model->type != VK_MODEL_MD2 || !vk_md5_load || !vk_md5_load->integer) {
        return false;
    }

    char model_name[MAX_QPATH];
    char base_path[MAX_QPATH];
    char mesh_path[MAX_QPATH];
    char anim_path[MAX_QPATH];

    COM_SplitPath(model->name, model_name, sizeof(model_name), base_path, sizeof(base_path), true);
    if (Q_concat(mesh_path, sizeof(mesh_path), base_path, "md5/", model_name, ".md5mesh") >= sizeof(mesh_path) ||
        Q_concat(anim_path, sizeof(anim_path), base_path, "md5/", model_name, ".md5anim") >= sizeof(anim_path)) {
        return false;
    }

    if (!FS_FileExists(mesh_path) || !FS_FileExists(anim_path)) {
        return false;
    }

    char *mesh_data = NULL;
    char *anim_data = NULL;
    int mesh_len = FS_LoadFile(mesh_path, (void **)&mesh_data);
    if (!mesh_data || mesh_len < 0) {
        if (mesh_data) {
            FS_FreeFile(mesh_data);
        }
        return false;
    }

    int anim_len = FS_LoadFile(anim_path, (void **)&anim_data);
    if (!anim_data || anim_len < 0) {
        FS_FreeFile(mesh_data);
        if (anim_data) {
            FS_FreeFile(anim_data);
        }
        return false;
    }

    vk_md5_t parsed = { 0 };
    bool ok = VK_MD5_ParseMesh(&parsed, mesh_data) && VK_MD5_ParseAnim(&parsed, anim_data, anim_path);
    FS_FreeFile(mesh_data);
    FS_FreeFile(anim_data);

    if (!ok) {
        VK_MD5_Free(&parsed);
        return false;
    }

    if (model->md2.num_frames && parsed.num_frames < model->md2.num_frames) {
        Com_WPrintf("%s has less frames than %s (%u < %u)\n",
                    anim_path, model->name, parsed.num_frames, model->md2.num_frames);
    }

    qhandle_t *replacement_skins = NULL;
    uint32_t replacement_skin_count = 0;

    // Mirrors the GL renderer's MD5_LoadSkins: replacement skins live in an
    // "md5/" subdirectory next to the MD2 skin files they replace.
    if (model->md2.num_skins && model->md2.skin_names) {
        replacement_skins = VK_Entity_CallocArray(model->md2.num_skins,
                                                  sizeof(*replacement_skins),
                                                  "MD5 replacement skins");
        if (!replacement_skins) {
            VK_MD5_Free(&parsed);
            return false;
        }

        replacement_skin_count = model->md2.num_skins;
        for (uint32_t i = 0; i < replacement_skin_count; i++) {
            char skin_name[MAX_QPATH];
            char skin_path[MAX_QPATH];

            COM_SplitPath(model->md2.skin_names[i], skin_name, sizeof(skin_name),
                          skin_path, sizeof(skin_path), false);
            if (Q_strlcat(skin_path, "md5/", sizeof(skin_path)) < sizeof(skin_path) &&
                Q_strlcat(skin_path, skin_name, sizeof(skin_path)) < sizeof(skin_path)) {
                replacement_skins[i] = VK_UI_RegisterImage(skin_path, IT_SKIN, IF_NONE);
            }
        }
    }

    VK_MD5_Free(&model->md5);
    model->md5 = parsed;
    model->md5.loaded = true;
    model->md5.skins = replacement_skins;
    model->md5.num_skins = replacement_skin_count;

    Com_DPrintf("Vulkan MD5 replacement loaded for %s (%u meshes, %u joints, %u frames)\n",
                model->name, model->md5.num_meshes, model->md5.num_joints, model->md5.num_frames);
    return true;
}

static bool VK_Entity_EnsureTempSkeleton(uint32_t num_joints)
{
    if (num_joints <= vk_entity.temp_skeleton_capacity) {
        return true;
    }

    vk_md5_joint_t *new_skeleton = VK_Entity_ReallocArray(vk_entity.temp_skeleton,
                                                          num_joints,
                                                          sizeof(*new_skeleton),
                                                          "temporary MD5 skeleton");
    if (!new_skeleton) {
        return false;
    }

    vk_entity.temp_skeleton = new_skeleton;
    vk_entity.temp_skeleton_capacity = num_joints;
    return true;
}

static bool VK_Entity_EnsureTempMD5Vertices(uint32_t num_vertices)
{
    if (num_vertices <= vk_entity.temp_md5_vertex_capacity) {
        return true;
    }

    vk_vertex_t *new_vertices = VK_Entity_ReallocArray(
        vk_entity.temp_md5_vertices, num_vertices, sizeof(*new_vertices),
        "temporary MD5 vertices");
    if (!new_vertices) {
        return false;
    }

    vk_entity.temp_md5_vertices = new_vertices;
    vk_entity.temp_md5_vertex_capacity = num_vertices;
    return true;
}

static const vk_md5_joint_t *VK_Entity_LerpMD5Skeleton(const vk_md5_t *md5,
                                                        uint32_t oldframe, uint32_t frame,
                                                        float backlerp, float frontlerp)
{
    if (!md5 || !md5->skeleton_frames || !md5->num_joints || !md5->num_frames) {
        return NULL;
    }

    oldframe %= md5->num_frames;
    frame %= md5->num_frames;
    if (oldframe == frame) {
        return &md5->skeleton_frames[(size_t)frame * md5->num_joints];
    }

    if (!VK_Entity_EnsureTempSkeleton(md5->num_joints)) {
        return NULL;
    }

    const vk_md5_joint_t *skel_a = &md5->skeleton_frames[(size_t)oldframe * md5->num_joints];
    const vk_md5_joint_t *skel_b = &md5->skeleton_frames[(size_t)frame * md5->num_joints];
    vk_md5_joint_t *out = vk_entity.temp_skeleton;

    for (uint32_t i = 0; i < md5->num_joints; i++) {
        out[i].scale = skel_b[i].scale;
        LerpVector2(skel_a[i].pos, skel_b[i].pos, backlerp, frontlerp, out[i].pos);
        Quat_SLerp(skel_a[i].orient, skel_b[i].orient, backlerp, frontlerp, out[i].orient);
        Quat_ToAxis(out[i].orient, out[i].axis);
    }

    return out;
}

static bool VK_Entity_ShouldUseMD5(const entity_t *ent, const refdef_t *fd, const vk_model_t *model)
{
    if (!ent || !model || !model->md5.loaded || !vk_md5_use || !vk_md5_use->integer) {
        return false;
    }
    if (ent->flags & RF_NO_LOD) {
        return true;
    }
    if (!fd || !vk_md5_distance || vk_md5_distance->value <= 0.0f) {
        return true;
    }
    return Distance(ent->origin, fd->vieworg) <= vk_md5_distance->value;
}

static void VK_Entity_MD5Vertex(const vk_md5_mesh_t *mesh,
                                const vk_md5_joint_t *skeleton,
                                uint32_t num_joints,
                                uint32_t vertex_index,
                                vec3_t out_pos,
                                vec3_t out_normal)
{
    VectorClear(out_pos);
    if (out_normal) {
        VectorClear(out_normal);
    }
    if (!mesh || !skeleton || vertex_index >= mesh->num_verts) {
        return;
    }

    const vk_md5_vertex_t *vert = &mesh->vertices[vertex_index];
    for (uint32_t i = 0; i < vert->count; i++) {
        uint32_t weight_index = (uint32_t)vert->start + i;
        if (weight_index >= mesh->num_weights) {
            break;
        }

        uint32_t joint_index = mesh->jointnums[weight_index];
        if (joint_index >= num_joints) {
            continue;
        }

        const vk_md5_weight_t *weight = &mesh->weights[weight_index];
        const vk_md5_joint_t *joint = &skeleton[joint_index];

        vec3_t rotated;
        vec3_t weighted;
        VectorRotate(weight->pos, joint->axis, rotated);
        VectorMA(joint->pos, joint->scale, rotated, weighted);
        VectorMA(out_pos, weight->bias, weighted, out_pos);

        if (out_normal) {
            VectorRotate(vert->normal, joint->axis, rotated);
            VectorMA(out_normal, weight->bias, rotated, out_normal);
        }
    }
}

static bool VK_Entity_AddMD5(const entity_t *ent, const refdef_t *fd, const vk_model_t *model,
                             bool depth_hack, bool weapon_model)
{
    if (!ent || !model || !model->md5.loaded) {
        return true;
    }

    const vk_md5_t *md5 = &model->md5;
    if (!md5->num_meshes || !md5->num_joints || !md5->num_frames || !md5->meshes || !md5->skeleton_frames) {
        return true;
    }

    uint32_t frame = 0;
    uint32_t oldframe = 0;
    float backlerp = 0.0f;
    float frontlerp = 1.0f;
    uint32_t frame_count = model->md2.num_frames ? model->md2.num_frames : md5->num_frames;
    if (!VK_Entity_ResolveAnimationFrames(fd, frame_count, ent->frame, ent->oldframe,
                                          ent->backlerp,
                                          &frame, &oldframe, &backlerp, &frontlerp)) {
        return true;
    }

    const vk_md5_joint_t *skeleton = VK_Entity_LerpMD5Skeleton(md5, oldframe, frame, backlerp, frontlerp);
    if (!skeleton) {
        return false;
    }

    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);

    bool shell = (ent->flags & RF_SHELL_MASK) != 0;
    float shell_scale = shell ? VK_Entity_ShellScale(ent) : 0.0f;
    bool no_static_lighting = false;
    color_t color = shell ? VK_Entity_ShellColor(ent)
                          : VK_Entity_LitColor(ent, false, false,
                                               fd ? fd->time : 0.0f,
                                               fd ? fd->rdflags : 0,
                                               &no_static_lighting);
    uint32_t flags = (shell
        ? (VK_ENTITY_VERTEX_FULLBRIGHT | VK_ENTITY_VERTEX_NO_SHADOW |
           VK_ENTITY_VERTEX_NO_DLIGHT | VK_ENTITY_VERTEX_BLOOM_SHELL)
         : VK_Entity_LightingFlags(ent, false)) | VK_ENTITY_VERTEX_INTENSITY;
    if (no_static_lighting) {
        flags |= VK_ENTITY_VERTEX_NO_ENTITY_MODULATE;
    }
    if (shell && depth_hack) {
        flags |= VK_ENTITY_VERTEX_BLOOM_DEPTHHACK;
    }
    if (!shell && (ent->flags & RF_RIMLIGHT)) {
        flags |= VK_ENTITY_VERTEX_RIMLIGHT | VK_ENTITY_VERTEX_BLOOM_RIM;
    }
    if (!shell && (ent->flags & RF_ITEM_COLORIZE)) {
        flags |= VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE;
    }
    vk_entity.current_cel_line_width =
        VK_Entity_CelShadingLineWidth(ent, fd);
    if (vk_entity.current_cel_line_width > 0.0f) {
        flags |= VK_ENTITY_VERTEX_CELSHADING;
    }
    qhandle_t preferred_skin = VK_Entity_SelectMD5Skin(ent, md5);

    for (uint32_t i = 0; i < md5->num_meshes; i++) {
        const vk_md5_mesh_t *mesh = &md5->meshes[i];
        if (!mesh->num_indices || !mesh->vertices || !mesh->indices || !mesh->tcoords ||
            !mesh->weights || !mesh->jointnums) {
            continue;
        }
        if (!VK_Entity_EnsureTempMD5Vertices(mesh->num_verts)) {
            return false;
        }

        qhandle_t skin = preferred_skin ? preferred_skin : mesh->shader_image;
        VkDescriptorSet set = shell ? vk_entity.white_set : VK_Entity_SetForImage(skin);
        if (!set) {
            continue;
        }
        const bool has_glowmap = !shell && VK_UI_HasGlowmap(skin);
        // RF_TRANSLUCENT selects the GL blend path even at alpha 1.0 (rim and
        // brightskin copies commonly use that exact value).
        bool alpha = shell || (ent->flags & RF_TRANSLUCENT) ||
                     VK_Entity_Alpha(ent) < 1.0f;
        bool additive = alpha && (ent->flags & (RF_RIMLIGHT | RF_BRIGHTSKIN));
        const uint32_t mesh_flags = flags |
            (has_glowmap ? VK_ENTITY_VERTEX_GLOWMAP : 0);
        uint32_t mesh_first_vertex = vk_entity.vertex_count;

        // Skin each unique mesh vertex once, then expand only the indexed
        // triangles into the transient draw stream. This keeps CPU skinning
        // proportional to vertex count instead of index count.
        for (uint32_t idx = 0; idx < mesh->num_verts; idx++) {
            vk_vertex_t *vertex = &vk_entity.temp_md5_vertices[idx];
            vec3_t local_pos, local_normal;
            VK_Entity_MD5Vertex(mesh, skeleton, md5->num_joints, idx,
                                local_pos, local_normal);
            if (shell) {
                VectorMA(local_pos, shell_scale, local_normal, local_pos);
            }
            VK_Entity_TransformPointWithTransform(&transform, local_pos,
                                                  vertex->pos);
            VK_Entity_TransformNormalWithTransform(&transform, local_normal,
                                                   vertex->normal);
            vertex->uv[0] = mesh->tcoords[idx].st[0];
            vertex->uv[1] = mesh->tcoords[idx].st[1];
            vertex->lm_uv[0] = 0.0f;
            vertex->lm_uv[1] = 0.0f;
            vertex->color = color.u32;
            vertex->flags = mesh_flags;
        }

        // MD5 skinning above already produces one transformed vertex per
        // unique mesh vertex. Submit that result directly for ordinary meshes
        // instead of expanding each index back into a triangle-list vertex.
        // Colorize and outline stages intentionally retain their established
        // expanded ranges for exact reuse in follow-up passes.
        if (!(ent->flags & (RF_ITEM_COLORIZE | RF_OUTLINE)) && mesh->num_verts) {
            if (mesh->num_verts > UINT32_MAX - vk_entity.vertex_count ||
                mesh->num_indices > UINT32_MAX - vk_entity.index_count) {
                Com_SetLastError("Vulkan entity: indexed MD5 stream count overflow");
                return false;
            }
            if (!VK_Entity_EnsureVertexCapacity(vk_entity.vertex_count + mesh->num_verts) ||
                !VK_Entity_EnsureIndexCapacity(vk_entity.index_count + mesh->num_indices)) {
                return false;
            }

            const uint32_t first_vertex = vk_entity.vertex_count;
            const uint32_t first_index = vk_entity.index_count;
            size_t vertex_bytes = 0;
            if (!VK_Entity_ArrayBytes(mesh->num_verts,
                                      sizeof(*vk_entity.temp_md5_vertices),
                                      &vertex_bytes, "indexed MD5 vertices")) {
                return false;
            }
            memcpy(&vk_entity.vertices[first_vertex], vk_entity.temp_md5_vertices,
                   vertex_bytes);
            vk_entity.vertex_count += mesh->num_verts;

            for (uint32_t tri_idx = 0; tri_idx + 2 < mesh->num_indices; tri_idx += 3) {
                const uint32_t i0 = mesh->indices[tri_idx + 0];
                const uint32_t i1 = mesh->indices[tri_idx + 1];
                const uint32_t i2 = mesh->indices[tri_idx + 2];
                if (i0 >= mesh->num_verts || i1 >= mesh->num_verts ||
                    i2 >= mesh->num_verts) {
                    continue;
                }
                vk_entity.indices[vk_entity.index_count++] = (uint16_t)i0;
                vk_entity.indices[vk_entity.index_count++] = (uint16_t)i1;
                vk_entity.indices[vk_entity.index_count++] = (uint16_t)i2;
            }

            const uint32_t index_count = vk_entity.index_count - first_index;
            if (!index_count) {
                vk_entity.vertex_count = first_vertex;
                continue;
            }
            if (!VK_Entity_AppendIndexedBatch(first_vertex, mesh->num_verts,
                                              first_index, index_count, set,
                                              alpha, additive, depth_hack,
                                              weapon_model)) {
                return false;
            }
            continue;
        }

        for (uint32_t tri_idx = 0; tri_idx + 2 < mesh->num_indices; tri_idx += 3) {
            vk_vertex_t tri[3];
            bool valid_tri = true;

            for (uint32_t j = 0; j < 3; j++) {
                uint32_t idx = mesh->indices[tri_idx + j];
                if (idx >= mesh->num_verts) {
                    valid_tri = false;
                    break;
                }

                tri[j] = vk_entity.temp_md5_vertices[idx];
            }

            if (!valid_tri) {
                continue;
            }

            if (!VK_Entity_EmitTriBlend(&tri[0], &tri[1], &tri[2], set,
                                        alpha, additive, depth_hack,
                                        weapon_model)) {
                return false;
            }
        }

        uint32_t mesh_vertex_count = vk_entity.vertex_count - mesh_first_vertex;

        if (!shell && (ent->flags & RF_ITEM_COLORIZE) &&
            !VK_Entity_EmitItemColorizeOverlay(
                mesh_first_vertex, mesh_vertex_count,
                set, ent, depth_hack, weapon_model)) {
            return false;
        }
        if ((ent->flags & RF_OUTLINE) &&
            !VK_Entity_EmitOutline(
                mesh_first_vertex, mesh_vertex_count, ent,
                VK_Entity_OutlineScale(ent, fd, &model->md2, frame, oldframe,
                                       backlerp, frontlerp),
                depth_hack, weapon_model)) {
            return false;
        }
    }

    return true;
}

static bool VK_Entity_ShouldUseGpuMD5(const entity_t *ent,
                                      const vk_model_t *model)
{
    if (!ent || !model || !model->md5.loaded ||
        !vk_md5_gpu_skinning || !vk_md5_gpu_skinning->integer ||
        !vk_entity.gpu_md5_available ||
        !vk_entity.gpu_md5_weight_buffer ||
        VK_Debug_ShowTris(VK_DEBUG_SHOWTRIS_MESH) ||
        (ent->flags & (RF_ITEM_COLORIZE | RF_OUTLINE))) {
        return false;
    }
    const vk_md5_t *md5 = &model->md5;
    for (uint32_t i = 0; i < md5->num_meshes; i++) {
        if (!md5->meshes[i].gpu_ready) {
            return false;
        }
    }
    return md5->num_meshes > 0 && md5->num_joints > 0;
}

static bool VK_Entity_AddGpuMD5(const entity_t *ent, const refdef_t *fd,
                                const vk_model_t *model, bool depth_hack,
                                bool weapon_model)
{
    const vk_md5_t *md5 = &model->md5;
    if (!md5->loaded || !md5->num_meshes || !md5->num_joints ||
        !md5->num_frames || !md5->meshes || !md5->skeleton_frames) {
        return false;
    }

    uint32_t frame = 0;
    uint32_t oldframe = 0;
    float backlerp = 0.0f;
    float frontlerp = 1.0f;
    const uint32_t frame_count = model->md2.num_frames
        ? model->md2.num_frames : md5->num_frames;
    if (!VK_Entity_ResolveAnimationFrames(fd, frame_count, ent->frame,
                                          ent->oldframe, ent->backlerp,
                                          &frame, &oldframe, &backlerp,
                                          &frontlerp)) {
        return true;
    }

    const vk_md5_joint_t *skeleton = VK_Entity_LerpMD5Skeleton(
        md5, oldframe, frame, backlerp, frontlerp);
    if (!skeleton || md5->num_joints > UINT32_MAX - vk_entity.md5_joint_count ||
        !VK_Entity_EnsureMd5JointCapacity(vk_entity.md5_joint_count +
                                           md5->num_joints)) {
        return false;
    }
    const uint32_t palette_offset = vk_entity.md5_joint_count;
    for (uint32_t i = 0; i < md5->num_joints; i++) {
        const vk_md5_joint_t *source = &skeleton[i];
        vk_md5_gpu_joint_t *joint = &vk_entity.md5_joints[palette_offset + i];
        memset(joint, 0, sizeof(*joint));
        VectorCopy(source->pos, joint->pos_scale);
        joint->pos_scale[3] = source->scale;
        for (uint32_t axis = 0; axis < 3; axis++) {
            VectorCopy(source->axis[axis], joint->axis[axis]);
        }
    }
    vk_entity.md5_joint_count += md5->num_joints;

    const bool shell = (ent->flags & RF_SHELL_MASK) != 0;
    const float shell_scale = shell ? VK_Entity_ShellScale(ent) : 0.0f;
    bool no_static_lighting = false;
    const color_t color = shell ? VK_Entity_ShellColor(ent)
                                : VK_Entity_LitColor(ent, false, false,
                                                     fd ? fd->time : 0.0f,
                                                     fd ? fd->rdflags : 0,
                                                     &no_static_lighting);
    uint32_t flags = (shell
        ? (VK_ENTITY_VERTEX_FULLBRIGHT | VK_ENTITY_VERTEX_NO_SHADOW |
           VK_ENTITY_VERTEX_NO_DLIGHT | VK_ENTITY_VERTEX_BLOOM_SHELL)
         : VK_Entity_LightingFlags(ent, false)) | VK_ENTITY_VERTEX_INTENSITY;
    if (no_static_lighting) {
        flags |= VK_ENTITY_VERTEX_NO_ENTITY_MODULATE;
    }
    if (shell && depth_hack) {
        flags |= VK_ENTITY_VERTEX_BLOOM_DEPTHHACK;
    }
    if (!shell && (ent->flags & RF_RIMLIGHT)) {
        flags |= VK_ENTITY_VERTEX_RIMLIGHT | VK_ENTITY_VERTEX_BLOOM_RIM;
    }
    vk_entity.current_cel_line_width =
        VK_Entity_CelShadingLineWidth(ent, fd);
    if (vk_entity.current_cel_line_width > 0.0f) {
        flags |= VK_ENTITY_VERTEX_CELSHADING;
    }
    qhandle_t preferred_skin = VK_Entity_SelectMD5Skin(ent, md5);
    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);

    for (uint32_t i = 0; i < md5->num_meshes; i++) {
        const vk_md5_mesh_t *mesh = &md5->meshes[i];
        if (!mesh->gpu_ready || !mesh->num_verts || !mesh->num_indices) {
            return false;
        }
        qhandle_t skin = preferred_skin ? preferred_skin : mesh->shader_image;
        VkDescriptorSet set = shell ? vk_entity.white_set : VK_Entity_SetForImage(skin);
        if (!set) {
            continue;
        }
        const bool alpha = shell || (ent->flags & RF_TRANSLUCENT) ||
                           VK_Entity_Alpha(ent) < 1.0f;
        const bool additive = alpha &&
            (ent->flags & (RF_RIMLIGHT | RF_BRIGHTSKIN));
        uint32_t mesh_flags = flags;
        if (!shell && VK_UI_HasGlowmap(skin)) {
            mesh_flags |= VK_ENTITY_VERTEX_GLOWMAP;
        }

        if (!VK_Entity_EnsureMd5InstanceCapacity(
                vk_entity.md5_instance_count + 1)) {
            return false;
        }
        const uint32_t instance_index = vk_entity.md5_instance_count;
        vk_md5_gpu_instance_t *instance =
            &vk_entity.md5_instances[instance_index];
        memset(instance, 0, sizeof(*instance));
        VectorCopy(transform.origin, instance->origin);
        instance->joint_palette_offset = palette_offset;
        for (uint32_t axis = 0; axis < 3; axis++) {
            VectorCopy(transform.scaled_axis[axis], instance->scaled_axis[axis]);
            VectorScale(transform.axis[axis], transform.inv_scale[axis],
                        instance->normal_axis[axis]);
        }
        instance->shell[0] = shell_scale;
        instance->color = color.u32;
        instance->flags = mesh_flags;
        vk_entity.md5_instance_count++;

        if (!VK_Entity_AppendGpuMd5Batch(mesh, instance_index, set, alpha,
                                         additive, depth_hack, weapon_model,
                                         mesh_flags)) {
            return false;
        }
    }
    return true;
}
#endif

static void VK_Entity_FreeModel(vk_model_t *model)
{
    if (!model) {
        return;
    }
    if (model->type == VK_MODEL_SPRITE) {
        free(model->sprite.frames);
    } else if (model->type == VK_MODEL_MD2) {
        VK_Entity_DestroyMd2GpuResources(&model->md2);
        free(model->md2.positions);
        free(model->md2.normals);
        free(model->md2.frame_radii);
        free(model->md2.frame_bounds);
        free(model->md2.uv);
        free(model->md2.indices);
        free(model->md2.skins);
        free(model->md2.skin_names);
    }
#if USE_MD5
    VK_Entity_DestroyMd5GpuResources(&model->md5);
    VK_MD5_Free(&model->md5);
#endif
    memset(model, 0, sizeof(*model));
}

static void VK_Entity_FreeAllModels(void)
{
    for (int i = 0; i < vk_entity.num_models; i++) {
        VK_Entity_FreeModel(&vk_entity.models[i]);
    }
    vk_entity.num_models = 0;
}

static uint32_t VK_Entity_FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(vk_entity.ctx->physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & BIT(i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

#define VK_ENTITY_STREAM_BUFFER_MIN_BYTES (64u * 1024u)

static bool VK_Entity_GrowStreamBuffer(size_t current, size_t needed,
                                       size_t *out_capacity)
{
    if (!out_capacity || !needed) {
        Com_SetLastError("Vulkan entity: invalid stream buffer capacity request");
        return false;
    }

    size_t capacity = current ? current : VK_ENTITY_STREAM_BUFFER_MIN_BYTES;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }

    *out_capacity = capacity;
    return true;
}

static vk_entity_frame_buffer_t *VK_Entity_CurrentFrameBuffer(void)
{
    if (!vk_entity.ctx || !vk_entity.ctx->frame_count ||
        vk_entity.ctx->current_frame >= vk_entity.ctx->frame_count) {
        return NULL;
    }
    return &vk_entity.frame_buffers[vk_entity.ctx->current_frame];
}

static void VK_Entity_DestroyBuffer(VkBuffer *buffer, VkDeviceMemory *memory,
                                    void **mapped)
{
    if (!vk_entity.ctx || !vk_entity.ctx->device) {
        return;
    }
    VkDevice device = vk_entity.ctx->device;
    if (mapped && *mapped && memory && *memory) {
        vkUnmapMemory(device, *memory);
        *mapped = NULL;
    }
    if (buffer && *buffer) {
        vkDestroyBuffer(device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
    }
    if (memory && *memory) {
        vkFreeMemory(device, *memory, NULL);
        *memory = VK_NULL_HANDLE;
    }
}

static void VK_Entity_DestroyBspGpuGeometry(void)
{
    VK_Entity_DestroyBuffer(&vk_entity.bmodel_gpu_vertex_buffer,
                            &vk_entity.bmodel_gpu_vertex_memory, NULL);
    free(vk_entity.bmodel_gpu_faces);
    vk_entity.bmodel_gpu_bsp = NULL;
    vk_entity.bmodel_gpu_faces = NULL;
    vk_entity.bmodel_gpu_face_count = 0;
    vk_entity.bmodel_gpu_vertex_count = 0;
    vk_entity.bmodel_gpu_ready = false;
    vk_entity.bmodel_gpu_failed = false;
}

static bool VK_Entity_CreateBuffer(size_t size, VkBufferUsageFlags usage,
                                   VkMemoryPropertyFlags properties,
                                   VkBuffer *out_buffer,
                                   VkDeviceMemory *out_memory,
                                   void **out_mapped,
                                   const char *what)
{
    if (!out_buffer || !out_memory || !size) {
        Com_SetLastError("Vulkan entity: invalid buffer request");
        return false;
    }
    *out_buffer = VK_NULL_HANDLE;
    *out_memory = VK_NULL_HANDLE;
    if (out_mapped) {
        *out_mapped = NULL;
    }

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VK_Entity_Check(vkCreateBuffer(vk_entity.ctx->device, &buffer_info, NULL,
                                        out_buffer), what)) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vk_entity.ctx->device, *out_buffer, &requirements);
    uint32_t memory_index = VK_Entity_FindMemoryType(requirements.memoryTypeBits,
                                                     properties);
    if (memory_index == UINT32_MAX) {
        Com_SetLastError(va("Vulkan entity: suitable %s memory type not found", what));
        VK_Entity_DestroyBuffer(out_buffer, out_memory, out_mapped);
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };
    if (!VK_Entity_Check(vkAllocateMemory(vk_entity.ctx->device, &alloc_info, NULL,
                                          out_memory), what) ||
        !VK_Entity_Check(vkBindBufferMemory(vk_entity.ctx->device, *out_buffer,
                                            *out_memory, 0), what)) {
        VK_Entity_DestroyBuffer(out_buffer, out_memory, out_mapped);
        return false;
    }
    if (out_mapped &&
        !VK_Entity_Check(vkMapMemory(vk_entity.ctx->device, *out_memory, 0,
                                     size, 0, out_mapped), what)) {
        VK_Entity_DestroyBuffer(out_buffer, out_memory, out_mapped);
        return false;
    }
    return true;
}

static bool VK_Entity_CopyStaticBuffers(vk_entity_static_upload_t *uploads,
                                        uint32_t count)
{
    if (!uploads || !count || !vk_entity.ctx || !vk_entity.ctx->device) {
        return false;
    }

    vk_context_t *ctx = vk_entity.ctx;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (!VK_Entity_Check(vkAllocateCommandBuffers(ctx->device, &alloc_info, &cmd),
                         "vkAllocateCommandBuffers(entity static upload)")) {
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (!VK_Entity_Check(vkBeginCommandBuffer(cmd, &begin_info),
                         "vkBeginCommandBuffer(entity static upload)")) {
        vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);
        return false;
    }

    VkBufferMemoryBarrier barriers[3] = { 0 };
    VkPipelineStageFlags destination_stages = 0;
    if (count > q_countof(barriers)) {
        vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);
        Com_SetLastError("Vulkan entity: static upload barrier overflow");
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!uploads[i].destination || !uploads[i].staging || !uploads[i].bytes) {
            vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);
            Com_SetLastError("Vulkan entity: invalid static upload range");
            return false;
        }
        const VkBufferCopy copy = { .size = uploads[i].bytes };
        vkCmdCopyBuffer(cmd, uploads[i].staging, uploads[i].destination, 1,
                        &copy);
        barriers[i] = (VkBufferMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = uploads[i].destination_access,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = uploads[i].destination,
            .size = uploads[i].bytes,
        };
        destination_stages |= uploads[i].destination_stage;
    }
    if (!destination_stages) {
        destination_stages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         destination_stages, 0, 0, NULL,
                         count, barriers, 0, NULL);

    if (!VK_Entity_Check(vkEndCommandBuffer(cmd),
                         "vkEndCommandBuffer(entity static upload)")) {
        vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);
        return false;
    }

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    const bool submitted = VK_Entity_Check(
        vkQueueSubmit(ctx->graphics_queue, 1, &submit_info, VK_NULL_HANDLE),
        "vkQueueSubmit(entity static upload)");
    const bool completed = submitted && VK_Entity_Check(
        vkQueueWaitIdle(ctx->graphics_queue),
        "vkQueueWaitIdle(entity static upload)");
    vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);
    return completed;
}

static void VK_Entity_DestroyMd2GpuResources(vk_md2_t *md2)
{
    if (!md2) {
        return;
    }
    VK_Entity_DestroyBuffer(&md2->gpu_frame_buffer, &md2->gpu_frame_memory,
                            NULL);
    VK_Entity_DestroyBuffer(&md2->gpu_uv_buffer, &md2->gpu_uv_memory, NULL);
    VK_Entity_DestroyBuffer(&md2->gpu_index_buffer, &md2->gpu_index_memory,
                            NULL);
    md2->gpu_ready = false;
}

static bool VK_Entity_CreateMd2GpuResources(vk_md2_t *md2)
{
    if (!md2 || !vk_md2_gpu_lerp || !vk_md2_gpu_lerp->integer ||
        !vk_entity.ctx || !vk_entity.ctx->device || !md2->positions ||
        !md2->normals || !md2->uv || !md2->indices || !md2->num_frames ||
        !md2->num_vertices || !md2->num_indices) {
        return true;
    }

    VK_Entity_DestroyMd2GpuResources(md2);
    if (md2->num_frames > SIZE_MAX / md2->num_vertices) {
        Com_SetLastError("Vulkan entity: GPU MD2 frame vertex count overflow");
        return false;
    }
    const size_t frame_vertex_count =
        (size_t)md2->num_frames * md2->num_vertices;
    size_t frame_bytes = 0;
    size_t uv_bytes = 0;
    size_t index_bytes = 0;
    if (!VK_Entity_ArrayBytes(frame_vertex_count,
                              sizeof(vk_md2_gpu_frame_vertex_t),
                              &frame_bytes, "GPU MD2 frame vertices") ||
        !VK_Entity_ArrayBytes(md2->num_vertices, sizeof(float) * 2,
                              &uv_bytes, "GPU MD2 UVs") ||
        !VK_Entity_ArrayBytes(md2->num_indices, sizeof(*md2->indices),
                              &index_bytes, "GPU MD2 indices")) {
        return false;
    }

    vk_md2_gpu_frame_vertex_t *frame_vertices = VK_Entity_CallocArray(
        frame_vertex_count, sizeof(*frame_vertices), "GPU MD2 frame vertices");
    if (!frame_vertices) {
        return false;
    }
    for (size_t i = 0; i < frame_vertex_count; i++) {
        size_t vector_offset = i * 3;
        memcpy(frame_vertices[i].pos, &md2->positions[vector_offset],
               sizeof(frame_vertices[i].pos));
        memcpy(frame_vertices[i].normal, &md2->normals[vector_offset],
               sizeof(frame_vertices[i].normal));
    }

    vk_entity_static_upload_t uploads[3] = {
        {
            .bytes = frame_bytes,
            .destination_access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            .destination_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        },
        {
            .bytes = uv_bytes,
            .destination_access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            .destination_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        },
        {
            .bytes = index_bytes,
            .destination_access = VK_ACCESS_INDEX_READ_BIT,
            .destination_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        },
    };
    const void *sources[3] = { frame_vertices, md2->uv, md2->indices };
    VkBuffer *destination_buffers[3] = {
        &md2->gpu_frame_buffer,
        &md2->gpu_uv_buffer,
        &md2->gpu_index_buffer,
    };
    VkDeviceMemory *destination_memories[3] = {
        &md2->gpu_frame_memory,
        &md2->gpu_uv_memory,
        &md2->gpu_index_memory,
    };
    const VkBufferUsageFlags destination_usage[3] = {
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };

    bool ok = true;
    for (uint32_t i = 0; i < q_countof(uploads); i++) {
        ok = VK_Entity_CreateBuffer(uploads[i].bytes, destination_usage[i],
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    destination_buffers[i],
                                    destination_memories[i], NULL,
                                    "vkCreateBuffer(static MD2)") &&
             VK_Entity_CreateBuffer(uploads[i].bytes,
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    &uploads[i].staging,
                                    &uploads[i].staging_memory,
                                    &uploads[i].staging_mapped,
                                    "vkCreateBuffer(static MD2 staging)");
        if (!ok) {
            break;
        }
        uploads[i].destination = *destination_buffers[i];
        memcpy(uploads[i].staging_mapped, sources[i], uploads[i].bytes);
    }
    if (ok) {
        ok = VK_Entity_CopyStaticBuffers(uploads, q_countof(uploads));
    }
    for (uint32_t i = 0; i < q_countof(uploads); i++) {
        VK_Entity_DestroyBuffer(&uploads[i].staging, &uploads[i].staging_memory,
                                &uploads[i].staging_mapped);
    }
    free(frame_vertices);

    if (!ok) {
        VK_Entity_DestroyMd2GpuResources(md2);
        return false;
    }
    md2->gpu_ready = true;
    VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY,
                          frame_bytes + uv_bytes + index_bytes);
    return true;
}

#if USE_MD5
static void VK_Entity_DestroyMd5GpuResources(vk_md5_t *md5)
{
    if (!md5 || !md5->meshes) {
        return;
    }
    for (uint32_t i = 0; i < md5->num_meshes; i++) {
        vk_md5_mesh_t *mesh = &md5->meshes[i];
        VK_Entity_DestroyBuffer(&mesh->gpu_vertex_buffer,
                                &mesh->gpu_vertex_memory, NULL);
        VK_Entity_DestroyBuffer(&mesh->gpu_index_buffer,
                                &mesh->gpu_index_memory, NULL);
        mesh->gpu_weight_offset = 0;
        mesh->gpu_ready = false;
    }
}

static bool VK_Entity_UploadMd5WeightBuffer(void)
{
    if (!vk_entity.md5_weight_count) {
        return true;
    }
    size_t bytes = 0;
    if (!VK_Entity_ArrayBytes(vk_entity.md5_weight_count,
                              sizeof(*vk_entity.md5_weights), &bytes,
                              "GPU MD5 weight buffer")) {
        return false;
    }

    VkBuffer new_buffer = VK_NULL_HANDLE;
    VkDeviceMemory new_memory = VK_NULL_HANDLE;
    vk_entity_static_upload_t upload = {
        .bytes = bytes,
        .destination_access = VK_ACCESS_SHADER_READ_BIT,
        .destination_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
    };
    bool ok = VK_Entity_CreateBuffer(
                  bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &new_buffer,
                  &new_memory, NULL, "vkCreateBuffer(static MD5 weights)") &&
              VK_Entity_CreateBuffer(
                  bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &upload.staging, &upload.staging_memory,
                  &upload.staging_mapped,
                  "vkCreateBuffer(static MD5 weight staging)");
    if (ok) {
        upload.destination = new_buffer;
        memcpy(upload.staging_mapped, vk_entity.md5_weights, bytes);
        ok = VK_Entity_CopyStaticBuffers(&upload, 1);
    }
    VK_Entity_DestroyBuffer(&upload.staging, &upload.staging_memory,
                            &upload.staging_mapped);
    if (!ok) {
        VK_Entity_DestroyBuffer(&new_buffer, &new_memory, NULL);
        return false;
    }

    VK_Entity_DestroyBuffer(&vk_entity.gpu_md5_weight_buffer,
                            &vk_entity.gpu_md5_weight_memory, NULL);
    vk_entity.gpu_md5_weight_buffer = new_buffer;
    vk_entity.gpu_md5_weight_memory = new_memory;
    vk_entity.gpu_md5_weight_buffer_bytes = bytes;
    VK_Entity_UpdateMd5DescriptorSets();
    VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY, bytes);
    return true;
}

static bool VK_Entity_CreateMd5GpuResources(vk_md5_t *md5)
{
    if (!md5 || !md5->loaded || !vk_md5_gpu_skinning ||
        !vk_md5_gpu_skinning->integer || !md5->meshes ||
        !md5->num_meshes || !vk_entity.ctx || !vk_entity.ctx->device) {
        return true;
    }

    VK_Entity_DestroyMd5GpuResources(md5);
    const uint32_t initial_weight_count = vk_entity.md5_weight_count;
    bool ok = true;
    for (uint32_t mesh_index = 0; mesh_index < md5->num_meshes && ok;
         mesh_index++) {
        vk_md5_mesh_t *mesh = &md5->meshes[mesh_index];
        if (!mesh->num_verts || !mesh->num_indices || !mesh->num_weights ||
            !mesh->vertices || !mesh->tcoords || !mesh->indices ||
            !mesh->weights || !mesh->jointnums ||
            mesh->num_weights > UINT32_MAX - vk_entity.md5_weight_count) {
            ok = false;
            break;
        }

        const uint32_t weight_offset = vk_entity.md5_weight_count;
        if (!VK_Entity_EnsureMd5WeightCapacity(weight_offset +
                                                mesh->num_weights)) {
            ok = false;
            break;
        }
        for (uint32_t i = 0; i < mesh->num_weights; i++) {
            vk_md5_gpu_weight_t *out = &vk_entity.md5_weights[weight_offset + i];
            const vk_md5_weight_t *in = &mesh->weights[i];
            out->pos_bias[0] = in->pos[0];
            out->pos_bias[1] = in->pos[1];
            out->pos_bias[2] = in->pos[2];
            out->pos_bias[3] = in->bias;
            out->joint_index = mesh->jointnums[i];
        }
        vk_entity.md5_weight_count += mesh->num_weights;
        mesh->gpu_weight_offset = weight_offset;

        size_t vertex_bytes = 0;
        size_t index_bytes = 0;
        vk_md5_gpu_vertex_t *vertices = NULL;
        if (!VK_Entity_ArrayBytes(mesh->num_verts, sizeof(*vertices),
                                  &vertex_bytes, "GPU MD5 vertices") ||
            !VK_Entity_ArrayBytes(mesh->num_indices, sizeof(*mesh->indices),
                                  &index_bytes, "GPU MD5 indices")) {
            ok = false;
            break;
        }
        vertices = VK_Entity_CallocArray(mesh->num_verts, sizeof(*vertices),
                                         "GPU MD5 vertices");
        if (!vertices) {
            ok = false;
            break;
        }
        for (uint32_t i = 0; i < mesh->num_verts; i++) {
            const vk_md5_vertex_t *source = &mesh->vertices[i];
            if (source->start > mesh->num_weights ||
                source->count > mesh->num_weights - source->start) {
                ok = false;
                break;
            }
            memcpy(vertices[i].normal, source->normal,
                   sizeof(vertices[i].normal));
            vertices[i].uv[0] = mesh->tcoords[i].st[0];
            vertices[i].uv[1] = mesh->tcoords[i].st[1];
            vertices[i].weight_offset = weight_offset + source->start;
            vertices[i].weight_count = source->count;
        }

        vk_entity_static_upload_t uploads[2] = {
            {
                .bytes = vertex_bytes,
                .destination_access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                .destination_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            },
            {
                .bytes = index_bytes,
                .destination_access = VK_ACCESS_INDEX_READ_BIT,
                .destination_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            },
        };
        if (ok) {
            ok = VK_Entity_CreateBuffer(
                     vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &mesh->gpu_vertex_buffer, &mesh->gpu_vertex_memory, NULL,
                     "vkCreateBuffer(static MD5 vertex)") &&
                 VK_Entity_CreateBuffer(
                     index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &mesh->gpu_index_buffer, &mesh->gpu_index_memory, NULL,
                     "vkCreateBuffer(static MD5 index)");
        }
        if (ok) {
            uploads[0].destination = mesh->gpu_vertex_buffer;
            uploads[1].destination = mesh->gpu_index_buffer;
            ok = VK_Entity_CreateBuffer(
                     vertex_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &uploads[0].staging, &uploads[0].staging_memory,
                     &uploads[0].staging_mapped,
                     "vkCreateBuffer(static MD5 vertex staging)") &&
                 VK_Entity_CreateBuffer(
                     index_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &uploads[1].staging, &uploads[1].staging_memory,
                     &uploads[1].staging_mapped,
                     "vkCreateBuffer(static MD5 index staging)");
        }
        if (ok) {
            memcpy(uploads[0].staging_mapped, vertices, vertex_bytes);
            memcpy(uploads[1].staging_mapped, mesh->indices, index_bytes);
            ok = VK_Entity_CopyStaticBuffers(uploads, q_countof(uploads));
        }
        for (uint32_t i = 0; i < q_countof(uploads); i++) {
            VK_Entity_DestroyBuffer(&uploads[i].staging,
                                    &uploads[i].staging_memory,
                                    &uploads[i].staging_mapped);
        }
        free(vertices);
        if (ok) {
            mesh->gpu_ready = true;
            VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY,
                                  vertex_bytes + index_bytes);
        }
    }

    if (ok) {
        ok = VK_Entity_UploadMd5WeightBuffer();
    }
    if (!ok) {
        vk_entity.md5_weight_count = initial_weight_count;
        VK_Entity_DestroyMd5GpuResources(md5);
    }
    return ok;
}
#endif

static void VK_Entity_DestroyGeometryBuffer(vk_entity_frame_buffer_t *frame)
{
    if (!frame) {
        return;
    }
    VK_Entity_DestroyBuffer(&frame->geometry_staging_buffer,
                            &frame->geometry_staging_memory,
                            &frame->geometry_mapped);
    VK_Entity_DestroyBuffer(&frame->geometry_buffer,
                            &frame->geometry_memory, NULL);
    frame->geometry_buffer_bytes = 0;
    frame->geometry_vertex_capacity = 0;
    frame->geometry_index_offset = 0;
    frame->geometry_index_capacity = 0;
    frame->vertex_upload_bytes = 0;
    frame->index_upload_bytes = 0;
}

static bool VK_Entity_EnsureGeometryBuffer(vk_entity_frame_buffer_t *frame,
                                           size_t vertex_bytes,
                                           size_t index_bytes)
{
    if (!frame) {
        Com_SetLastError("Vulkan entity: active frame geometry buffer is unavailable");
        return false;
    }
    if (!vertex_bytes && !index_bytes) {
        return true;
    }

    size_t vertex_capacity = frame->geometry_vertex_capacity;
    size_t index_capacity = frame->geometry_index_capacity;
    if (vertex_bytes > vertex_capacity &&
        !VK_Entity_GrowStreamBuffer(vertex_capacity, vertex_bytes,
                                    &vertex_capacity)) {
        return false;
    }
    if (index_bytes > index_capacity &&
        !VK_Entity_GrowStreamBuffer(index_capacity, index_bytes,
                                    &index_capacity)) {
        return false;
    }

    // vkCmdBindIndexBuffer requires an index-aligned offset. The normal
    // geometric capacities are powers of two, but preserve correctness in the
    // overflow-path case where growth may select the exact requested byte size.
    size_t index_offset = vertex_capacity;
    if (index_offset & (sizeof(uint16_t) - 1)) {
        if (index_offset == SIZE_MAX) {
            Com_SetLastError("Vulkan entity: geometry index offset overflow");
            return false;
        }
        index_offset++;
    }
    if (index_capacity > SIZE_MAX - index_offset) {
        Com_SetLastError("Vulkan entity: geometry arena size overflow");
        return false;
    }
    const size_t capacity = index_offset + index_capacity;
    if (!capacity) {
        return true;
    }
    if (frame->geometry_buffer && frame->geometry_memory &&
        frame->geometry_staging_buffer && frame->geometry_staging_memory &&
        frame->geometry_mapped &&
        vertex_capacity == frame->geometry_vertex_capacity &&
        index_capacity == frame->geometry_index_capacity &&
        index_offset == frame->geometry_index_offset) {
        return true;
    }

    VK_Entity_DestroyGeometryBuffer(frame);

    if (!VK_Entity_CreateBuffer(capacity,
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                &frame->geometry_buffer,
                                &frame->geometry_memory, NULL,
                                "vkCreateBuffer(entity geometry)")) {
        return false;
    }
    if (!VK_Entity_CreateBuffer(capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &frame->geometry_staging_buffer,
                                &frame->geometry_staging_memory,
                                &frame->geometry_mapped,
                                "vkCreateBuffer(entity geometry staging)")) {
        VK_Entity_DestroyGeometryBuffer(frame);
        return false;
    }
    frame->geometry_buffer_bytes = capacity;
    frame->geometry_vertex_capacity = vertex_capacity;
    frame->geometry_index_offset = index_offset;
    frame->geometry_index_capacity = index_capacity;
    return true;
}

static void VK_Entity_DestroyInstanceBuffer(vk_entity_frame_buffer_t *frame)
{
    if (!frame) {
        return;
    }
    VK_Entity_DestroyBuffer(&frame->instance_staging_buffer,
                            &frame->instance_staging_memory,
                            &frame->instance_mapped);
    VK_Entity_DestroyBuffer(&frame->instance_buffer,
                            &frame->instance_memory, NULL);
    frame->instance_buffer_bytes = 0;
    frame->md2_instance_offset = 0;
    frame->md2_instance_capacity = 0;
    frame->md2_instance_upload_bytes = 0;
    frame->bmodel_instance_offset = 0;
    frame->bmodel_instance_capacity = 0;
    frame->bmodel_instance_upload_bytes = 0;
    frame->particle_instance_offset = 0;
    frame->particle_instance_capacity = 0;
    frame->particle_instance_upload_bytes = 0;
    frame->beam_instance_offset = 0;
    frame->beam_instance_capacity = 0;
    frame->beam_instance_upload_bytes = 0;
#if USE_MD5
    frame->md5_instance_offset = 0;
    frame->md5_instance_capacity = 0;
    frame->md5_instance_upload_bytes = 0;
    frame->md5_palette_offset = 0;
    frame->md5_palette_capacity = 0;
    frame->md5_palette_upload_bytes = 0;
#endif
}

static bool VK_Entity_AlignInstanceOffset(size_t offset, size_t alignment,
                                          size_t *aligned_offset)
{
    if (!alignment || !aligned_offset ||
        offset > SIZE_MAX - (alignment - 1)) {
        Com_SetLastError("Vulkan entity: instance arena alignment overflow");
        return false;
    }
    const size_t rounded = offset + alignment - 1;
    *aligned_offset = rounded - rounded % alignment;
    return true;
}

static bool VK_Entity_InstanceArenaAlignment(size_t *alignment)
{
    if (!alignment || !vk_entity.ctx || !vk_entity.ctx->physical_device) {
        Com_SetLastError("Vulkan entity: instance arena device is unavailable");
        return false;
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(vk_entity.ctx->physical_device, &properties);
    if (properties.limits.minStorageBufferOffsetAlignment > SIZE_MAX) {
        Com_SetLastError("Vulkan entity: storage-buffer alignment is unsupported");
        return false;
    }
    *alignment = max((size_t)16,
                     (size_t)properties.limits.minStorageBufferOffsetAlignment);
    return true;
}

static bool VK_Entity_InstanceArenaAppendRegion(size_t *cursor,
                                                size_t region_capacity,
                                                size_t alignment,
                                                size_t *region_offset)
{
    if (!cursor || !region_offset ||
        !VK_Entity_AlignInstanceOffset(*cursor, alignment, region_offset)) {
        return false;
    }
    if (region_capacity > SIZE_MAX - *region_offset) {
        Com_SetLastError("Vulkan entity: instance arena size overflow");
        return false;
    }
    *cursor = *region_offset + region_capacity;
    return true;
}

static bool VK_Entity_EnsureInstanceBuffer(vk_entity_frame_buffer_t *frame,
                                           size_t md2_bytes,
                                           size_t bmodel_bytes,
                                           size_t particle_bytes,
                                           size_t beam_bytes
#if USE_MD5
                                           , size_t md5_instance_bytes,
                                           size_t md5_palette_bytes
#endif
)
{
    if (!frame) {
        Com_SetLastError("Vulkan entity: active frame instance arena is unavailable");
        return false;
    }
    if (!md2_bytes && !bmodel_bytes && !particle_bytes && !beam_bytes
#if USE_MD5
        && !md5_instance_bytes && !md5_palette_bytes
#endif
    ) {
        return true;
    }

    size_t md2_capacity = frame->md2_instance_capacity;
    size_t bmodel_capacity = frame->bmodel_instance_capacity;
    size_t particle_capacity = frame->particle_instance_capacity;
    size_t beam_capacity = frame->beam_instance_capacity;
#if USE_MD5
    size_t md5_instance_capacity = frame->md5_instance_capacity;
    size_t md5_palette_capacity = frame->md5_palette_capacity;
#endif
    if ((md2_bytes > md2_capacity &&
         !VK_Entity_GrowStreamBuffer(md2_capacity, md2_bytes, &md2_capacity)) ||
        (bmodel_bytes > bmodel_capacity &&
         !VK_Entity_GrowStreamBuffer(bmodel_capacity, bmodel_bytes,
                                     &bmodel_capacity))
        || (particle_bytes > particle_capacity &&
            !VK_Entity_GrowStreamBuffer(particle_capacity, particle_bytes,
                                        &particle_capacity))
        || (beam_bytes > beam_capacity &&
            !VK_Entity_GrowStreamBuffer(beam_capacity, beam_bytes,
                                        &beam_capacity))
#if USE_MD5
        || (md5_instance_bytes > md5_instance_capacity &&
            !VK_Entity_GrowStreamBuffer(md5_instance_capacity,
                                        md5_instance_bytes,
                                        &md5_instance_capacity))
        || (md5_palette_bytes > md5_palette_capacity &&
            !VK_Entity_GrowStreamBuffer(md5_palette_capacity, md5_palette_bytes,
                                        &md5_palette_capacity))
#endif
    ) {
        return false;
    }

    size_t alignment = 0;
    size_t cursor = 0;
    size_t md2_offset = 0;
    size_t bmodel_offset = 0;
    size_t particle_offset = 0;
    size_t beam_offset = 0;
#if USE_MD5
    size_t md5_instance_offset = 0;
    size_t md5_palette_offset = 0;
#endif
    if (!VK_Entity_InstanceArenaAlignment(&alignment) ||
        !VK_Entity_InstanceArenaAppendRegion(&cursor, md2_capacity, alignment,
                                             &md2_offset) ||
        !VK_Entity_InstanceArenaAppendRegion(&cursor, bmodel_capacity, alignment,
                                             &bmodel_offset)
        || !VK_Entity_InstanceArenaAppendRegion(&cursor, particle_capacity,
                                                alignment, &particle_offset)
        || !VK_Entity_InstanceArenaAppendRegion(&cursor, beam_capacity,
                                                alignment, &beam_offset)
#if USE_MD5
        || !VK_Entity_InstanceArenaAppendRegion(&cursor, md5_instance_capacity,
                                                 alignment, &md5_instance_offset)
        || !VK_Entity_InstanceArenaAppendRegion(&cursor, md5_palette_capacity,
                                                 alignment, &md5_palette_offset)
#endif
    ) {
        return false;
    }

    if (frame->instance_buffer && frame->instance_memory &&
        frame->instance_staging_buffer && frame->instance_staging_memory &&
        frame->instance_mapped && cursor == frame->instance_buffer_bytes &&
        md2_capacity == frame->md2_instance_capacity &&
        bmodel_capacity == frame->bmodel_instance_capacity &&
        md2_offset == frame->md2_instance_offset &&
        bmodel_offset == frame->bmodel_instance_offset
        && particle_capacity == frame->particle_instance_capacity &&
        particle_offset == frame->particle_instance_offset
        && beam_capacity == frame->beam_instance_capacity &&
        beam_offset == frame->beam_instance_offset
#if USE_MD5
        && md5_instance_capacity == frame->md5_instance_capacity &&
        md5_palette_capacity == frame->md5_palette_capacity &&
        md5_instance_offset == frame->md5_instance_offset &&
        md5_palette_offset == frame->md5_palette_offset
#endif
    ) {
        return true;
    }

    VK_Entity_DestroyInstanceBuffer(frame);
    if (!VK_Entity_CreateBuffer(cursor,
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                &frame->instance_buffer,
                                &frame->instance_memory, NULL,
                                "vkCreateBuffer(entity instance arena)")) {
        return false;
    }
    if (!VK_Entity_CreateBuffer(cursor, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &frame->instance_staging_buffer,
                                &frame->instance_staging_memory,
                                &frame->instance_mapped,
                                "vkCreateBuffer(entity instance arena staging)")) {
        VK_Entity_DestroyInstanceBuffer(frame);
        return false;
    }
    frame->instance_buffer_bytes = cursor;
    frame->md2_instance_offset = md2_offset;
    frame->md2_instance_capacity = md2_capacity;
    frame->bmodel_instance_offset = bmodel_offset;
    frame->bmodel_instance_capacity = bmodel_capacity;
    frame->particle_instance_offset = particle_offset;
    frame->particle_instance_capacity = particle_capacity;
    frame->beam_instance_offset = beam_offset;
    frame->beam_instance_capacity = beam_capacity;
#if USE_MD5
    frame->md5_instance_offset = md5_instance_offset;
    frame->md5_instance_capacity = md5_instance_capacity;
    frame->md5_palette_offset = md5_palette_offset;
    frame->md5_palette_capacity = md5_palette_capacity;
#endif
    return true;
}

static bool VK_Entity_EnsureVertexCapacity(uint32_t needed)
{
    if (needed <= vk_entity.vertex_capacity) {
        return true;
    }

    uint32_t cap = 0;
    if (!VK_Entity_GrowCapacity(vk_entity.vertex_capacity, needed, 4096, &cap,
                                "vertices")) {
        return false;
    }

    vk_vertex_t *new_buf = VK_Entity_ReallocArray(vk_entity.vertices, cap,
                                                  sizeof(*new_buf), "vertices");
    if (!new_buf) {
        return false;
    }
    vk_entity.vertices = new_buf;
    vk_entity.vertex_capacity = cap;
    return true;
}

static bool VK_Entity_EnsureIndexCapacity(uint32_t needed)
{
    if (needed <= vk_entity.index_capacity) {
        return true;
    }

    uint32_t cap = 0;
    if (!VK_Entity_GrowCapacity(vk_entity.index_capacity, needed, 12288, &cap,
                                "indices")) {
        return false;
    }

    uint16_t *new_buf = VK_Entity_ReallocArray(vk_entity.indices, cap,
                                               sizeof(*new_buf), "indices");
    if (!new_buf) {
        return false;
    }
    vk_entity.indices = new_buf;
    vk_entity.index_capacity = cap;
    return true;
}

static bool VK_Entity_EnsureMd2InstanceCapacity(uint32_t needed)
{
    if (needed <= vk_entity.md2_instance_capacity) {
        return true;
    }

    uint32_t cap = 0;
    if (!VK_Entity_GrowCapacity(vk_entity.md2_instance_capacity, needed, 256,
                                &cap, "GPU MD2 instances")) {
        return false;
    }

    vk_md2_gpu_instance_t *new_buf = VK_Entity_ReallocArray(
        vk_entity.md2_instances, cap, sizeof(*new_buf), "GPU MD2 instances");
    if (!new_buf) {
        return false;
    }
    vk_entity.md2_instances = new_buf;
    vk_entity.md2_instance_capacity = cap;
    return true;
}

static bool VK_Entity_EnsureBmodelInstanceCapacity(uint32_t needed)
{
    if (needed <= vk_entity.bmodel_instance_capacity) {
        return true;
    }

    uint32_t cap = 0;
    if (!VK_Entity_GrowCapacity(vk_entity.bmodel_instance_capacity, needed,
                                256, &cap, "GPU BSP instances")) {
        return false;
    }

    vk_bmodel_gpu_instance_t *new_buf = VK_Entity_ReallocArray(
        vk_entity.bmodel_instances, cap, sizeof(*new_buf), "GPU BSP instances");
    if (!new_buf) {
        return false;
    }
    vk_entity.bmodel_instances = new_buf;
    vk_entity.bmodel_instance_capacity = cap;
    return true;
}

static bool VK_Entity_EnsureParticleInstanceCapacity(uint32_t needed)
{
    if (needed <= vk_entity.particle_instance_capacity) {
        return true;
    }

    uint32_t cap = 0;
    if (!VK_Entity_GrowCapacity(vk_entity.particle_instance_capacity, needed,
                                1024, &cap, "GPU particle instances")) {
        return false;
    }

    vk_particle_gpu_instance_t *new_buf = VK_Entity_ReallocArray(
        vk_entity.particle_instances, cap, sizeof(*new_buf),
        "GPU particle instances");
    if (!new_buf) {
        return false;
    }
    vk_entity.particle_instances = new_buf;
    vk_entity.particle_instance_capacity = cap;
    return true;
}

static bool VK_Entity_EnsureBeamInstanceCapacity(uint32_t needed)
{
    if (needed <= vk_entity.beam_instance_capacity) {
        return true;
    }
    uint32_t cap = 0;
    if (!VK_Entity_GrowCapacity(vk_entity.beam_instance_capacity, needed, 256,
                                &cap, "GPU beam instances")) {
        return false;
    }
    vk_beam_gpu_instance_t *new_buf = VK_Entity_ReallocArray(
        vk_entity.beam_instances, cap, sizeof(*new_buf), "GPU beam instances");
    if (!new_buf) {
        return false;
    }
    vk_entity.beam_instances = new_buf;
    vk_entity.beam_instance_capacity = cap;
    return true;
}

#if USE_MD5
static bool VK_Entity_EnsureMd5InstanceCapacity(uint32_t needed)
{
    if (needed <= vk_entity.md5_instance_capacity) {
        return true;
    }
    uint32_t cap = 0;
    if (!VK_Entity_GrowCapacity(vk_entity.md5_instance_capacity, needed, 128,
                                &cap, "GPU MD5 instances")) {
        return false;
    }
    vk_md5_gpu_instance_t *new_buf = VK_Entity_ReallocArray(
        vk_entity.md5_instances, cap, sizeof(*new_buf), "GPU MD5 instances");
    if (!new_buf) {
        return false;
    }
    vk_entity.md5_instances = new_buf;
    vk_entity.md5_instance_capacity = cap;
    return true;
}

static bool VK_Entity_EnsureMd5JointCapacity(uint32_t needed)
{
    if (needed <= vk_entity.md5_joint_capacity) {
        return true;
    }
    uint32_t cap = 0;
    if (!VK_Entity_GrowCapacity(vk_entity.md5_joint_capacity, needed, 1024,
                                &cap, "GPU MD5 joints")) {
        return false;
    }
    vk_md5_gpu_joint_t *new_buf = VK_Entity_ReallocArray(
        vk_entity.md5_joints, cap, sizeof(*new_buf), "GPU MD5 joints");
    if (!new_buf) {
        return false;
    }
    vk_entity.md5_joints = new_buf;
    vk_entity.md5_joint_capacity = cap;
    return true;
}

static bool VK_Entity_EnsureMd5WeightCapacity(uint32_t needed)
{
    if (needed <= vk_entity.md5_weight_capacity) {
        return true;
    }
    uint32_t cap = 0;
    if (!VK_Entity_GrowCapacity(vk_entity.md5_weight_capacity, needed, 4096,
                                &cap, "GPU MD5 weights")) {
        return false;
    }
    vk_md5_gpu_weight_t *new_buf = VK_Entity_ReallocArray(
        vk_entity.md5_weights, cap, sizeof(*new_buf), "GPU MD5 weights");
    if (!new_buf) {
        return false;
    }
    vk_entity.md5_weights = new_buf;
    vk_entity.md5_weight_capacity = cap;
    return true;
}
#endif

static bool VK_Entity_EnsureBatchCapacity(uint32_t needed)
{
    if (needed <= vk_entity.batch_capacity) {
        return true;
    }

    uint32_t cap = 0;
    if (!VK_Entity_GrowCapacity(vk_entity.batch_capacity, needed, 512, &cap,
                                "batches")) {
        return false;
    }

    vk_batch_t *new_buf = VK_Entity_ReallocArray(vk_entity.batches, cap,
                                                 sizeof(*new_buf), "batches");
    if (!new_buf) {
        return false;
    }
    vk_entity.batches = new_buf;
    vk_entity.batch_capacity = cap;
    return true;
}

static VkDescriptorSet VK_Entity_SetForImage(qhandle_t handle)
{
    VkDescriptorSet set = VK_UI_GetDescriptorSetForImage(handle);
    return set ? set : vk_entity.white_set;
}

static bool VK_Entity_AppendIndexedBatch(uint32_t first_vertex,
                                         uint32_t vertex_count,
                                         uint32_t first_index,
                                         uint32_t index_count,
                                         VkDescriptorSet set,
                                         bool alpha, bool additive,
                                         bool depth_hack, bool weapon_model)
{
    if (!set || !vertex_count || !index_count) {
        return true;
    }

    if (first_vertex > INT32_MAX) {
        Com_SetLastError("Vulkan entity: indexed batch vertex offset overflow");
        return false;
    }

    const uint32_t vertex_flags = first_vertex < vk_entity.vertex_count
        ? vk_entity.vertices[first_vertex].flags : 0;
    const bool celshading =
        (vertex_flags & VK_ENTITY_VERTEX_CELSHADING) != 0;
    const float cel_line_width = celshading
        ? vk_entity.current_cel_line_width : 0.0f;

    vk_batch_t *batch = (vk_entity.batch_count > 0)
        ? &vk_entity.batches[vk_entity.batch_count - 1]
        : NULL;
    uint32_t index_vertex_offset = 0;
    if (!batch || !batch->indexed || batch->set != set ||
        batch->alpha != alpha || batch->additive != additive ||
        batch->depth_hack != depth_hack || batch->weapon_model != weapon_model ||
        batch->flare || batch->occlusion ||
        batch->submit_phase != vk_entity.current_submit_phase ||
        batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
        batch->first_index > UINT32_MAX - batch->index_count ||
        batch->first_index + batch->index_count != first_index ||
        batch->first_vertex > UINT32_MAX - batch->vertex_count ||
        batch->first_vertex + batch->vertex_count != first_vertex ||
        first_vertex - batch->first_vertex > UINT16_MAX ||
        vertex_count > UINT16_MAX -
            (first_vertex - batch->first_vertex) + 1u ||
        ((batch->vertex_flags & VK_ENTITY_VERTEX_CELSHADING) !=
             (vertex_flags & VK_ENTITY_VERTEX_CELSHADING)) ||
        (celshading && batch->cel_line_width != cel_line_width)) {
        if (!VK_Entity_EnsureBatchCapacity(vk_entity.batch_count + 1)) {
            return false;
        }
        batch = &vk_entity.batches[vk_entity.batch_count++];
        *batch = (vk_batch_t){
            .first_vertex = first_vertex,
            .vertex_count = 0,
            .first_index = first_index,
            .index_count = 0,
            .set = set,
            .query_index = UINT32_MAX,
            .alpha = alpha,
            .additive = additive,
            .depth_hack = depth_hack,
            .weapon_model = weapon_model,
            .indexed = true,
            .submit_phase = vk_entity.current_submit_phase,
            .vertex_flags = vertex_flags & VK_ENTITY_VERTEX_CELSHADING,
            .cel_line_width = cel_line_width,
        };
    }
    else {
        index_vertex_offset = first_vertex - batch->first_vertex;
    }

    // Each new indexed model writes local source indices. When compatible
    // adjacent models share a batch, rebase only the newly appended range so
    // it remains 16-bit relative to the batch's first vertex.
    if (index_vertex_offset) {
        if (first_index > vk_entity.index_count ||
            index_count > vk_entity.index_count - first_index) {
            Com_SetLastError("Vulkan entity: indexed batch range overflow");
            return false;
        }
        for (uint32_t i = 0; i < index_count; i++) {
            uint16_t *index = &vk_entity.indices[first_index + i];
            if (*index > UINT16_MAX - index_vertex_offset) {
                Com_SetLastError("Vulkan entity: 16-bit indexed batch overflow");
                return false;
            }
            *index = (uint16_t)(*index + index_vertex_offset);
        }
    }
    if (vertex_count > UINT32_MAX - batch->vertex_count ||
        index_count > UINT32_MAX - batch->index_count) {
        Com_SetLastError("Vulkan entity: indexed batch count overflow");
        return false;
    }
    batch->vertex_count += vertex_count;
    batch->index_count += index_count;
    VK_Entity_MarkCurrentSubmitPass(alpha, vertex_flags);
    VK_Entity_QueueIndexedShowTris(batch->first_vertex, first_index,
                                   index_count);
    return true;
}

static bool VK_Entity_AppendGpuMd2Batch(const vk_md2_t *md2,
                                        uint32_t frame, uint32_t oldframe,
                                        uint32_t instance_index,
                                        VkDescriptorSet set,
                                        bool alpha, bool additive,
                                        bool depth_hack, bool weapon_model,
                                        uint32_t vertex_flags)
{
    if (!md2 || !md2->gpu_ready || !md2->num_vertices || !md2->num_indices ||
        !set) {
        return false;
    }

    const float cel_line_width =
        (vertex_flags & VK_ENTITY_VERTEX_CELSHADING) != 0
            ? vk_entity.current_cel_line_width : 0.0f;

    vk_batch_t *batch = (vk_entity.batch_count > 0)
        ? &vk_entity.batches[vk_entity.batch_count - 1]
        : NULL;
    if (!batch || !batch->gpu_md2 || batch->gpu_md2_model != md2 ||
        batch->gpu_md2_frame != frame || batch->gpu_md2_oldframe != oldframe ||
        batch->set != set || batch->alpha != alpha ||
        batch->additive != additive || batch->depth_hack != depth_hack ||
        batch->weapon_model != weapon_model || batch->flare || batch->occlusion ||
        batch->submit_phase != vk_entity.current_submit_phase ||
        batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
        batch->vertex_flags != vertex_flags ||
        batch->cel_line_width != cel_line_width ||
        batch->first_instance > UINT32_MAX - batch->instance_count ||
        batch->first_instance + batch->instance_count != instance_index) {
        if (!VK_Entity_EnsureBatchCapacity(vk_entity.batch_count + 1)) {
            return false;
        }
        batch = &vk_entity.batches[vk_entity.batch_count++];
        *batch = (vk_batch_t) {
            .vertex_count = md2->num_vertices,
            .index_count = md2->num_indices,
            .set = set,
            .query_index = UINT32_MAX,
            .alpha = alpha,
            .additive = additive,
            .depth_hack = depth_hack,
            .weapon_model = weapon_model,
            .indexed = true,
            .gpu_md2 = true,
            .submit_phase = vk_entity.current_submit_phase,
            .vertex_flags = vertex_flags,
            .cel_line_width = cel_line_width,
            .gpu_md2_model = md2,
            .gpu_md2_frame = frame,
            .gpu_md2_oldframe = oldframe,
            .first_instance = instance_index,
        };
    }

    if (batch->instance_count == UINT32_MAX) {
        Com_SetLastError("Vulkan entity: GPU MD2 instance count overflow");
        return false;
    }
    batch->instance_count++;
    VK_Entity_MarkCurrentSubmitPass(alpha, vertex_flags);
    return true;
}

static bool VK_Entity_AppendGpuBmodelBatch(uint32_t first_vertex,
                                           uint32_t vertex_count,
                                           uint32_t instance_index,
                                           VkDescriptorSet set, bool alpha,
                                           bool additive, uint32_t vertex_flags)
{
    if (!vertex_count || !set) {
        return true;
    }

    vk_batch_t *batch = (vk_entity.batch_count > 0)
        ? &vk_entity.batches[vk_entity.batch_count - 1]
        : NULL;
    if (!batch || !batch->gpu_bmodel || batch->set != set ||
        batch->alpha != alpha || batch->additive != additive ||
        batch->depth_hack || batch->weapon_model || batch->flare ||
        batch->occlusion ||
        batch->submit_phase != vk_entity.current_submit_phase ||
        batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
        batch->vertex_flags != vertex_flags ||
        batch->first_instance != instance_index || batch->instance_count != 1 ||
        batch->first_vertex > UINT32_MAX - batch->vertex_count ||
        batch->first_vertex + batch->vertex_count != first_vertex) {
        if (!VK_Entity_EnsureBatchCapacity(vk_entity.batch_count + 1)) {
            return false;
        }
        batch = &vk_entity.batches[vk_entity.batch_count++];
        *batch = (vk_batch_t) {
            .first_vertex = first_vertex,
            .vertex_count = 0,
            .set = set,
            .query_index = UINT32_MAX,
            .alpha = alpha,
            .additive = additive,
            .gpu_bmodel = true,
            .submit_phase = vk_entity.current_submit_phase,
            .vertex_flags = vertex_flags,
            .first_instance = instance_index,
            .instance_count = 1,
        };
    }
    if (vertex_count > UINT32_MAX - batch->vertex_count) {
        Com_SetLastError("Vulkan entity: GPU BSP batch vertex count overflow");
        return false;
    }
    batch->vertex_count += vertex_count;
    VK_Entity_MarkCurrentSubmitPass(alpha, vertex_flags);
    return true;
}

static bool VK_Entity_AppendGpuParticleBatch(uint32_t instance_index,
                                             VkDescriptorSet set,
                                             bool additive)
{
    const uint32_t flags = VK_ENTITY_VERTEX_FULLBRIGHT |
        VK_ENTITY_VERTEX_NO_SHADOW | VK_ENTITY_VERTEX_NO_DLIGHT;
    if (!set) {
        return false;
    }

    vk_batch_t *batch = vk_entity.batch_count > 0
        ? &vk_entity.batches[vk_entity.batch_count - 1] : NULL;
    if (!batch || !batch->gpu_particle || batch->set != set ||
        !batch->alpha || batch->additive != additive || batch->depth_hack ||
        batch->weapon_model || batch->flare || batch->occlusion ||
        batch->submit_phase != vk_entity.current_submit_phase ||
        batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
        batch->vertex_flags != flags ||
        batch->first_instance > UINT32_MAX - batch->instance_count ||
        batch->first_instance + batch->instance_count != instance_index) {
        if (!VK_Entity_EnsureBatchCapacity(vk_entity.batch_count + 1)) {
            return false;
        }
        batch = &vk_entity.batches[vk_entity.batch_count++];
        *batch = (vk_batch_t) {
            .vertex_count = 3,
            .set = set,
            .query_index = UINT32_MAX,
            .alpha = true,
            .additive = additive,
            .gpu_particle = true,
            .submit_phase = vk_entity.current_submit_phase,
            .vertex_flags = flags,
            .first_instance = instance_index,
        };
    }
    if (batch->instance_count == UINT32_MAX) {
        Com_SetLastError("Vulkan entity: GPU particle instance count overflow");
        return false;
    }
    batch->instance_count++;
    VK_Entity_MarkCurrentSubmitPass(true, flags);
    return true;
}

static bool VK_Entity_AppendGpuBeamBatch(uint32_t instance_index,
                                         VkDescriptorSet set,
                                         bool depth_hack)
{
    const uint32_t flags = VK_ENTITY_VERTEX_FULLBRIGHT |
        VK_ENTITY_VERTEX_NO_SHADOW | VK_ENTITY_VERTEX_NO_DLIGHT;
    if (!set) {
        return false;
    }
    vk_batch_t *batch = vk_entity.batch_count > 0
        ? &vk_entity.batches[vk_entity.batch_count - 1] : NULL;
    if (!batch || !batch->gpu_beam || batch->set != set || !batch->alpha ||
        batch->additive || batch->depth_hack != depth_hack ||
        batch->weapon_model || batch->flare || batch->occlusion ||
        batch->submit_phase != vk_entity.current_submit_phase ||
        batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
        batch->vertex_flags != flags ||
        batch->first_instance > UINT32_MAX - batch->instance_count ||
        batch->first_instance + batch->instance_count != instance_index) {
        if (!VK_Entity_EnsureBatchCapacity(vk_entity.batch_count + 1)) {
            return false;
        }
        batch = &vk_entity.batches[vk_entity.batch_count++];
        *batch = (vk_batch_t) {
            .vertex_count = 6,
            .set = set,
            .query_index = UINT32_MAX,
            .alpha = true,
            .gpu_beam = true,
            .depth_hack = depth_hack,
            .submit_phase = vk_entity.current_submit_phase,
            .vertex_flags = flags,
            .first_instance = instance_index,
        };
    }
    if (batch->instance_count == UINT32_MAX) {
        Com_SetLastError("Vulkan entity: GPU beam instance count overflow");
        return false;
    }
    batch->instance_count++;
    VK_Entity_MarkCurrentSubmitPass(true, flags);
    return true;
}

static bool VK_Entity_CanCoalesceGpuBmodelBatches(const vk_batch_t *first,
                                                   const vk_batch_t *next)
{
    // Opaque, non-alpha-tested geometry is order-independent after depth
    // testing. Keep all blended and alpha-tested material paths in their
    // original per-entity order.
    return first && next && first->gpu_bmodel && next->gpu_bmodel &&
        !first->alpha && !next->alpha && !first->additive && !next->additive &&
        !(first->vertex_flags & VK_ENTITY_VERTEX_ALPHATEST) &&
        !(next->vertex_flags & VK_ENTITY_VERTEX_ALPHATEST) &&
        first->set == next->set &&
        first->submit_phase == next->submit_phase &&
        first->outline_stage == next->outline_stage &&
        first->vertex_flags == next->vertex_flags &&
        first->first_vertex == next->first_vertex &&
        first->vertex_count == next->vertex_count &&
        first->instance_count != UINT32_MAX &&
        first->first_instance <= UINT32_MAX - first->instance_count &&
        first->first_instance + first->instance_count == next->first_instance;
}

static void VK_Entity_CoalesceGpuBmodelBatches(void)
{
    // Entity collection is deliberately entity-major to preserve legacy
    // translucent ordering. Consolidate only proven order-independent opaque
    // GPU BSP ranges afterwards, retaining the first matching batch's stable
    // position and avoiding any transient allocation on the frame hot path.
    uint32_t compact_count = 0;
    for (uint32_t source = 0; source < vk_entity.batch_count; source++) {
        const vk_batch_t *candidate = &vk_entity.batches[source];
        bool merged = false;
        for (uint32_t target = 0; target < compact_count; target++) {
            vk_batch_t *existing = &vk_entity.batches[target];
            if (!VK_Entity_CanCoalesceGpuBmodelBatches(existing, candidate)) {
                continue;
            }
            existing->instance_count++;
            merged = true;
            break;
        }
        if (!merged) {
            if (compact_count != source) {
                vk_entity.batches[compact_count] = *candidate;
            }
            compact_count++;
        }
    }
    vk_entity.batch_count = compact_count;
}

#if USE_MD5
static bool VK_Entity_AppendGpuMd5Batch(const vk_md5_mesh_t *mesh,
                                        uint32_t instance_index,
                                        VkDescriptorSet set,
                                        bool alpha, bool additive,
                                        bool depth_hack, bool weapon_model,
                                        uint32_t vertex_flags)
{
    if (!mesh || !mesh->gpu_ready || !mesh->num_verts || !mesh->num_indices ||
        !set) {
        return false;
    }
    const float cel_line_width =
        (vertex_flags & VK_ENTITY_VERTEX_CELSHADING) != 0
            ? vk_entity.current_cel_line_width : 0.0f;
    vk_batch_t *batch = (vk_entity.batch_count > 0)
        ? &vk_entity.batches[vk_entity.batch_count - 1]
        : NULL;
    if (!batch || !batch->gpu_md5 || batch->gpu_md5_mesh != mesh ||
        batch->set != set || batch->alpha != alpha ||
        batch->additive != additive || batch->depth_hack != depth_hack ||
        batch->weapon_model != weapon_model || batch->flare || batch->occlusion ||
        batch->submit_phase != vk_entity.current_submit_phase ||
        batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
        batch->vertex_flags != vertex_flags ||
        batch->cel_line_width != cel_line_width ||
        batch->first_instance > UINT32_MAX - batch->instance_count ||
        batch->first_instance + batch->instance_count != instance_index) {
        if (!VK_Entity_EnsureBatchCapacity(vk_entity.batch_count + 1)) {
            return false;
        }
        batch = &vk_entity.batches[vk_entity.batch_count++];
        *batch = (vk_batch_t) {
            .vertex_count = mesh->num_verts,
            .index_count = mesh->num_indices,
            .set = set,
            .query_index = UINT32_MAX,
            .alpha = alpha,
            .additive = additive,
            .depth_hack = depth_hack,
            .weapon_model = weapon_model,
            .indexed = true,
            .gpu_md5 = true,
            .submit_phase = vk_entity.current_submit_phase,
            .vertex_flags = vertex_flags,
            .cel_line_width = cel_line_width,
            .gpu_md5_mesh = mesh,
            .first_instance = instance_index,
        };
    }
    if (batch->instance_count == UINT32_MAX) {
        Com_SetLastError("Vulkan entity: GPU MD5 instance count overflow");
        return false;
    }
    batch->instance_count++;
    VK_Entity_MarkCurrentSubmitPass(alpha, vertex_flags);
    return true;
}
#endif

static bool VK_Entity_EmitTriMode(const vk_vertex_t *a, const vk_vertex_t *b,
                                  const vk_vertex_t *c, VkDescriptorSet set,
                                  bool alpha, bool additive, bool depth_hack,
                                  bool weapon_model, uint32_t query_index,
                                  bool flare, bool occlusion, bool cutout,
                                  bool no_depth_write)
{
    if (!set) {
        return true;
    }

    if (!VK_Entity_EnsureVertexCapacity(vk_entity.vertex_count + 3)) {
        return false;
    }

    uint32_t first = vk_entity.vertex_count;
    vk_entity.vertices[vk_entity.vertex_count++] = *a;
    vk_entity.vertices[vk_entity.vertex_count++] = *b;
    vk_entity.vertices[vk_entity.vertex_count++] = *c;
    vk_entity.frame_has_flare_queries |= occlusion;
    vk_entity.frame_has_flares |= flare;

    const bool celshading =
        (a->flags & VK_ENTITY_VERTEX_CELSHADING) != 0;
    const float cel_line_width = celshading
        ? vk_entity.current_cel_line_width : 0.0f;
    vk_batch_t *batch = (vk_entity.batch_count > 0)
        ? &vk_entity.batches[vk_entity.batch_count - 1]
        : NULL;
    if (!batch || batch->set != set || batch->query_index != query_index ||
        batch->alpha != alpha || batch->additive != additive ||
        batch->depth_hack != depth_hack ||
        batch->weapon_model != weapon_model || batch->flare != flare ||
        batch->occlusion != occlusion || batch->cutout != cutout ||
        batch->no_depth_write != no_depth_write ||
        batch->submit_phase != vk_entity.current_submit_phase ||
        batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
        ((batch->vertex_flags & VK_ENTITY_VERTEX_CELSHADING) !=
             (a->flags & VK_ENTITY_VERTEX_CELSHADING)) ||
        (celshading && batch->cel_line_width != cel_line_width)) {
        if (!VK_Entity_EnsureBatchCapacity(vk_entity.batch_count + 1)) {
            return false;
        }
        batch = &vk_entity.batches[vk_entity.batch_count++];
        *batch = (vk_batch_t){
            .first_vertex = first,
            .vertex_count = 0,
            .set = set,
            .query_index = query_index,
            .alpha = alpha,
            .additive = additive,
            .depth_hack = depth_hack,
            .weapon_model = weapon_model,
            .flare = flare,
            .occlusion = occlusion,
            .cutout = cutout,
            .no_depth_write = no_depth_write,
            .submit_phase = vk_entity.current_submit_phase,
            .vertex_flags = a->flags & VK_ENTITY_VERTEX_CELSHADING,
            .cel_line_width = cel_line_width,
        };
    }
    batch->vertex_count += 3;
    VK_Entity_MarkCurrentSubmitPass(alpha, a->flags);
    if ((a->flags & VK_ENTITY_VERTEX_ITEM_COLORIZE) == 0) {
        if (flare) {
            VK_Debug_QueueShowTrisTriangleNoDepth(
                vk_entity.showtris_category, a->pos, b->pos, c->pos);
        } else {
            VK_Debug_QueueShowTrisTriangle(vk_entity.showtris_category,
                                           a->pos, b->pos, c->pos);
        }
    }
    return true;
}

static void VK_Entity_QueueIndexedShowTris(uint32_t first_vertex,
                                           uint32_t first_index,
                                           uint32_t index_count)
{
    if (!VK_Debug_ShowTris(vk_entity.showtris_category) ||
        !index_count || index_count % 3 ||
        first_index > vk_entity.index_count ||
        index_count > vk_entity.index_count - first_index) {
        return;
    }

    for (uint32_t i = 0; i < index_count; i += 3) {
        const uint32_t ia = first_vertex +
            vk_entity.indices[first_index + i + 0];
        const uint32_t ib = first_vertex +
            vk_entity.indices[first_index + i + 1];
        const uint32_t ic = first_vertex +
            vk_entity.indices[first_index + i + 2];
        if (ia >= vk_entity.vertex_count || ib >= vk_entity.vertex_count ||
            ic >= vk_entity.vertex_count) {
            continue;
        }
        const vk_vertex_t *a = &vk_entity.vertices[ia];
        const vk_vertex_t *b = &vk_entity.vertices[ib];
        const vk_vertex_t *c = &vk_entity.vertices[ic];
        if ((a->flags & VK_ENTITY_VERTEX_ITEM_COLORIZE) == 0) {
            VK_Debug_QueueShowTrisTriangle(vk_entity.showtris_category,
                                           a->pos, b->pos, c->pos);
        }
    }
}

static bool VK_Entity_EmitTri(const vk_vertex_t *a, const vk_vertex_t *b, const vk_vertex_t *c,
                              VkDescriptorSet set, bool alpha, bool depth_hack, bool weapon_model)
{
    return VK_Entity_EmitTriBlend(a, b, c, set, alpha, false, depth_hack, weapon_model);
}

static bool VK_Entity_EmitTriBlend(const vk_vertex_t *a, const vk_vertex_t *b,
                                   const vk_vertex_t *c, VkDescriptorSet set,
                                   bool alpha, bool additive, bool depth_hack,
                                   bool weapon_model)
{
    return VK_Entity_EmitTriMode(a, b, c, set, alpha, additive, depth_hack,
                                 weapon_model, UINT32_MAX, false, false, false,
                                 false);
}

static bool VK_Entity_EmitTriCutout(const vk_vertex_t *a, const vk_vertex_t *b,
                                    const vk_vertex_t *c, VkDescriptorSet set,
                                    bool depth_hack, bool weapon_model)
{
    return VK_Entity_EmitTriMode(a, b, c, set, true, false, depth_hack,
                                 weapon_model, UINT32_MAX, false, false, true,
                                 true);
}

static bool VK_Entity_EmitTriOpaqueNoDepth(const vk_vertex_t *a,
                                           const vk_vertex_t *b,
                                           const vk_vertex_t *c,
                                           VkDescriptorSet set,
                                           bool depth_hack,
                                           bool weapon_model)
{
    return VK_Entity_EmitTriMode(a, b, c, set, false, false, depth_hack,
                                 weapon_model, UINT32_MAX, false, false,
                                 false, true);
}

// OpenGL renders item colorization as an untinted base followed by a
// luminance-tinted alpha overlay. Reuse the already transformed/skinned
// triangle stream so the parity pass adds no model animation work.
static bool VK_Entity_EmitItemColorizeOverlay(uint32_t first_vertex,
                                              uint32_t vertex_count,
                                              VkDescriptorSet set,
                                              const entity_t *ent,
                                              bool depth_hack,
                                              bool weapon_model)
{
    if (!ent || !vertex_count || vertex_count % 3) {
        return true;
    }

    float overlay_alpha = (ent->rgba.a * (1.0f / 255.0f)) *
                          VK_Entity_Alpha(ent);
    if (overlay_alpha <= 0.0f) {
        return true;
    }

    color_t overlay_color = COLOR_RGBA(
        ent->rgba.r, ent->rgba.g, ent->rgba.b,
        (uint8_t)Q_clipf(overlay_alpha * 255.0f + 0.5f, 0.0f, 255.0f));
    for (uint32_t i = 0; i < vertex_count; i += 3) {
        vk_vertex_t tri[3];
        for (uint32_t j = 0; j < 3; j++) {
            // Reload by index each iteration because EmitTriBlend may grow
            // and relocate the CPU staging allocation.
            tri[j] = vk_entity.vertices[first_vertex + i + j];
            tri[j].color = overlay_color.u32;
            tri[j].flags &= ~(VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE |
                              VK_ENTITY_VERTEX_LIGHTMAP);
            tri[j].flags |= VK_ENTITY_VERTEX_ITEM_COLORIZE |
                            VK_ENTITY_VERTEX_FULLBRIGHT |
                            VK_ENTITY_VERTEX_NO_SHADOW |
                            VK_ENTITY_VERTEX_NO_DLIGHT;
        }
        if (!VK_Entity_EmitTriBlend(&tri[0], &tri[1], &tri[2], set,
                                    true, false, depth_hack, weapon_model)) {
            return false;
        }
    }
    return true;
}

static bool VK_Entity_AppendOutlineBatch(uint32_t first_vertex,
                                         uint32_t vertex_count,
                                         vk_entity_outline_stage_t stage,
                                         bool alpha,
                                         bool no_depth,
                                         bool depth_hack,
                                         bool weapon_model)
{
    if (!vertex_count || !vk_entity.white_set) {
        return true;
    }
    if (!VK_Entity_EnsureBatchCapacity(vk_entity.batch_count + 1)) {
        return false;
    }

    vk_batch_t *batch = &vk_entity.batches[vk_entity.batch_count++];
    *batch = (vk_batch_t){
        .first_vertex = first_vertex,
        .vertex_count = vertex_count,
        .set = vk_entity.white_set,
        .query_index = UINT32_MAX,
        .alpha = alpha,
        .depth_hack = depth_hack,
        .weapon_model = weapon_model,
        .submit_phase = vk_entity.current_submit_phase,
        .outline_stage = stage,
        .outline_no_depth = no_depth,
    };
    vk_entity.frame_has_outline_batches = true;
    vk_entity.frame_has_depth_hack_batches |= depth_hack;
    VK_Entity_MarkCurrentSubmitPhase();
    return true;
}

// Reuse the original transformed triangle range for stencil mask/cleanup and
// allocate only the expanded solid-color shell. This keeps outline cost to one
// extra vertex copy and three indexed-free draws, with no repeated MD5 skinning.
static bool VK_Entity_EmitOutline(uint32_t first_vertex,
                                  uint32_t vertex_count,
                                  const entity_t *ent,
                                  float scale,
                                  bool depth_hack,
                                  bool weapon_model)
{
    if (!vk_entity.stencil_available || !ent ||
        !(ent->flags & RF_OUTLINE) || !vertex_count || vertex_count % 3 ||
        first_vertex > vk_entity.vertex_count ||
        vertex_count > vk_entity.vertex_count - first_vertex) {
        return true;
    }
    if (vertex_count > UINT32_MAX - vk_entity.vertex_count ||
        !VK_Entity_EnsureVertexCapacity(vk_entity.vertex_count + vertex_count)) {
        return false;
    }

    color_t outline_color = ent->rgba.u32 ? ent->rgba : COLOR_RED;
    float outline_alpha = outline_color.a * (1.0f / 255.0f);
    bool alpha = outline_alpha < 0.999f;
    bool no_depth = (ent->flags & RF_OUTLINE_NODEPTH) != 0;
    uint32_t shell_first_vertex = vk_entity.vertex_count;

    for (uint32_t i = 0; i < vertex_count; i++) {
        vk_vertex_t vertex = vk_entity.vertices[first_vertex + i];
        for (int axis = 0; axis < 3; axis++) {
            vertex.pos[axis] = ent->origin[axis] +
                (vertex.pos[axis] - ent->origin[axis]) * scale;
        }
        vertex.uv[0] = 0.0f;
        vertex.uv[1] = 0.0f;
        vertex.lm_uv[0] = 0.0f;
        vertex.lm_uv[1] = 0.0f;
        vertex.color = outline_color.u32;
        vertex.flags = VK_ENTITY_VERTEX_FULLBRIGHT |
                       VK_ENTITY_VERTEX_NO_SHADOW |
                       VK_ENTITY_VERTEX_NO_DLIGHT;
        vk_entity.vertices[vk_entity.vertex_count++] = vertex;
    }

    return VK_Entity_AppendOutlineBatch(
               first_vertex, vertex_count, VK_ENTITY_OUTLINE_MASK,
               false, no_depth, depth_hack, weapon_model) &&
           VK_Entity_AppendOutlineBatch(
               shell_first_vertex, vertex_count, VK_ENTITY_OUTLINE_SHELL,
               alpha, no_depth, depth_hack, weapon_model) &&
           VK_Entity_AppendOutlineBatch(
               first_vertex, vertex_count, VK_ENTITY_OUTLINE_CLEAR,
               false, no_depth, depth_hack, weapon_model);
}

static bool VK_Entity_EmitTriSpecial(const vk_vertex_t *a, const vk_vertex_t *b,
                                     const vk_vertex_t *c, VkDescriptorSet set,
                                     uint32_t query_index, bool flare,
                                     bool occlusion)
{
    return VK_Entity_EmitTriMode(a, b, c, set, flare, flare, false, false, query_index,
                                 flare, occlusion, false, false);
}

static inline float VK_Entity_Alpha(const entity_t *ent)
{
    if (!(ent->flags & RF_TRANSLUCENT)) {
        return 1.0f;
    }
    return Q_clipf(ent->alpha, 0.0f, 1.0f);
}

static float VK_Entity_CelShadingLineWidth(const entity_t *ent,
                                           const refdef_t *fd)
{
    if (!ent || !fd || !vk_celshading || !vk_entity.ctx ||
        !vk_entity.ctx->celshading_supported ||
        (ent->flags & (RF_TRANSLUCENT | RF_SHELL_MASK | RF_TRACKER))) {
        return 0.0f;
    }

    const float requested_width = Q_clipf(vk_celshading->value, 0.0f, 10.0f);
    if (requested_width <= 0.0f) {
        return 0.0f;
    }

    // setup_celshading() fades both OpenGL's black alpha and line width over
    // 700 world units, then skips visually negligible replays.
    const float scale = 1.0f - Distance(ent->origin, fd->vieworg) / 700.0f;
    if (scale < 0.01f) {
        return 0.0f;
    }

    const float minimum = max(vk_entity.ctx->min_line_width, 1.0f);
    const float maximum = max(vk_entity.ctx->max_line_width, minimum);
    return Q_clipf(requested_width * scale, minimum, maximum);
}

static color_t VK_Entity_LitColor(const entity_t *ent, bool fullbright,
                                  bool include_dynamic_lights,
                                  float frame_time, int rdflags,
                                  bool *out_no_static_lighting)
{
    if (out_no_static_lighting) {
        *out_no_static_lighting = false;
    }
    float alpha = VK_Entity_Alpha(ent);
    if (ent->flags & RF_RIMLIGHT) {
        // The GL path uses a red fallback only when the packed color is zero;
        // entity alpha, not rgba.a, controls the translucent rim strength.
        color_t rim = ent->rgba.u32 ? ent->rgba : COLOR_RED;
        return COLOR_RGBA(rim.r, rim.g, rim.b,
                          (uint8_t)(alpha * 255.0f + 0.5f));
    }
    if (ent->flags & RF_BRIGHTSKIN) {
        // Mirrors the GL setup_color() RF_BRIGHTSKIN branch: a fullbright
        // entity copy tinted by ent->rgba (team/enemy force colors).
        return COLOR_RGBA(ent->rgba.r, ent->rgba.g, ent->rgba.b,
                          (uint8_t)(alpha * 255.0f + 0.5f));
    }
    if (fullbright || (ent->flags & RF_FULLBRIGHT)) {
        return COLOR_RGBA(255, 255, 255, (uint8_t)(alpha * 255.0f + 0.5f));
    }
    if ((ent->flags & RF_IR_VISIBLE) && (rdflags & RDF_IRGOGGLES)) {
        return COLOR_RGBA(255, 0, 0, (uint8_t)(alpha * 255.0f + 0.5f));
    }
    if (ent->flags & RF_TRACKER) {
        return COLOR_RGBA(0, 0, 0, (uint8_t)(alpha * 255.0f + 0.5f));
    }
    if (VK_World_Fullbright()) {
        return COLOR_RGBA(255, 255, 255, (uint8_t)(alpha * 255.0f + 0.5f));
    }

    vec3_t light;
    bool has_static_light = false;
    VK_World_LightPointEx(ent->origin, light, include_dynamic_lights,
                          &has_static_light);
    if (out_no_static_lighting && !include_dynamic_lights &&
        !has_static_light) {
        *out_no_static_lighting = true;
    }
    if (ent->flags & RF_MINLIGHT) {
        light[0] = max(light[0], 0.1f);
        light[1] = max(light[1], 0.1f);
        light[2] = max(light[2], 0.1f);
    }
    if (ent->flags & RF_GLOW) {
        float pulse = 0.1f * sinf(frame_time * 7.0f);
        for (int i = 0; i < 3; i++) {
            float floor = light[i] * 0.8f;
            light[i] += pulse;
            if (light[i] < floor) {
                light[i] = floor;
            }
        }
    }

    return COLOR_RGBA((uint8_t)min(255, (int)(light[0] * 255.0f + 0.5f)),
                      (uint8_t)min(255, (int)(light[1] * 255.0f + 0.5f)),
                      (uint8_t)min(255, (int)(light[2] * 255.0f + 0.5f)),
                      (uint8_t)(alpha * 255.0f + 0.5f));
}

static uint32_t VK_Entity_LightingFlags(const entity_t *ent, bool fullbright)
{
    uint32_t flags = 0;
    if (fullbright ||
        VK_World_Fullbright() ||
        (ent && (ent->flags & (RF_FULLBRIGHT | RF_RIMLIGHT | RF_BRIGHTSKIN)))) {
        flags |= VK_ENTITY_VERTEX_FULLBRIGHT |
                 VK_ENTITY_VERTEX_NO_SHADOW |
                 VK_ENTITY_VERTEX_NO_DLIGHT;
    }
    return flags;
}

// Mirrors the GL mesh setup_color() shell branch: shell entities replace the
// skin with a solid translucent color expanded along vertex normals. The
// client adds the base model as a separate entity, so the shell pass draws
// instead of, not on top of, the textured mesh.
static color_t VK_Entity_ShellColor(const entity_t *ent)
{
    vec3_t shell = { 0.0f, 0.0f, 0.0f };
    uint64_t flags = ent->flags;

    if (flags & RF_SHELL_LITE_GREEN) {
        VectorSet(shell, 0.56f, 0.93f, 0.56f);
    }
    if (flags & RF_SHELL_HALF_DAM) {
        VectorSet(shell, 0.56f, 0.59f, 0.45f);
    }
    if (flags & RF_SHELL_DOUBLE) {
        shell[0] = 0.25f;
        shell[1] = 0.88f;
        shell[2] = 0.82f;
    }
    if (flags & RF_SHELL_RED) {
        shell[0] = 1.0f;
    }
    if (flags & RF_SHELL_GREEN) {
        shell[1] = 1.0f;
    }
    if (flags & RF_SHELL_BLUE) {
        shell[2] = 1.0f;
    }

    float alpha = VK_Entity_Alpha(ent);
    return COLOR_RGBA((uint8_t)(shell[0] * 255.0f + 0.5f),
                      (uint8_t)(shell[1] * 255.0f + 0.5f),
                      (uint8_t)(shell[2] * 255.0f + 0.5f),
                      (uint8_t)(alpha * 255.0f + 0.5f));
}

static inline float VK_Entity_ShellScale(const entity_t *ent)
{
    return (ent->flags & RF_WEAPONMODEL) ? WEAPONSHELL_SCALE : POWERSUIT_SCALE;
}

static float VK_Entity_OutlineScale(const entity_t *ent, const refdef_t *fd,
                                    const vk_md2_t *md2, uint32_t frame,
                                    uint32_t oldframe, float backlerp,
                                    float frontlerp)
{
    float scale = VK_ENTITY_OUTLINE_SCALE;
    float width = vk_player_outline_width
        ? vk_player_outline_width->value
        : VK_ENTITY_OUTLINE_WIDTH_DEFAULT;
    width = Q_clipf(width, VK_ENTITY_OUTLINE_WIDTH_MIN,
                    VK_ENTITY_OUTLINE_WIDTH_MAX);

    if (!ent || !fd || !md2 || !md2->frame_radii ||
        frame >= md2->num_frames || oldframe >= md2->num_frames) {
        return scale;
    }

    float entity_scale = 1.0f;
    for (int axis = 0; axis < 3; axis++) {
        float value = isfinite(ent->scale[axis]) && ent->scale[axis] != 0.0f
            ? fabsf(ent->scale[axis])
            : 1.0f;
        entity_scale = max(entity_scale, value);
    }

    float radius = (md2->frame_radii[frame] * frontlerp +
                    md2->frame_radii[oldframe] * backlerp) * entity_scale;
    float distance = Distance(fd->vieworg, ent->origin);
    if (radius <= 0.0f || distance <= 0.0f ||
        fd->fov_y <= 0.0f || fd->height <= 0) {
        return scale;
    }

    float denominator = 2.0f * tanf(DEG2RAD(fd->fov_y * 0.5f)) * distance;
    if (denominator <= 0.0f) {
        return scale;
    }

    float radius_pixels = radius * ((float)fd->height / denominator);
    if (radius_pixels <= 0.0f) {
        return scale;
    }

    float desired = 1.0f + width / radius_pixels;
    if (desired > scale) {
        scale = desired;
    }
    return min(scale, VK_ENTITY_OUTLINE_SCALE_MAX);
}

static void VK_Entity_BuildTransform(const entity_t *ent, vk_entity_transform_t *out_transform)
{
    if (!out_transform) {
        return;
    }

    memset(out_transform, 0, sizeof(*out_transform));
    VectorSet(out_transform->axis[0], 1.0f, 0.0f, 0.0f);
    VectorSet(out_transform->axis[1], 0.0f, 1.0f, 0.0f);
    VectorSet(out_transform->axis[2], 0.0f, 0.0f, 1.0f);
    VectorSet(out_transform->scaled_axis[0], 1.0f, 0.0f, 0.0f);
    VectorSet(out_transform->scaled_axis[1], 0.0f, 1.0f, 0.0f);
    VectorSet(out_transform->scaled_axis[2], 0.0f, 0.0f, 1.0f);
    VectorSet(out_transform->inv_scale, 1.0f, 1.0f, 1.0f);

    if (!ent) {
        return;
    }

    float backlerp = Q_clipf(ent->backlerp, 0.0f, 1.0f);
    float frontlerp = 1.0f - backlerp;
    if (backlerp > 0.0f && !VectorCompare(ent->origin, ent->oldorigin)) {
        LerpVector2(ent->oldorigin, ent->origin, backlerp, frontlerp, out_transform->origin);
    } else {
        VectorCopy(ent->origin, out_transform->origin);
    }

    if (!VectorEmpty(ent->angles)) {
        AnglesToAxis(ent->angles, out_transform->axis);
    }

    for (int i = 0; i < 3; i++) {
        float scale = ent->scale[i] ? ent->scale[i] : 1.0f;
        out_transform->inv_scale[i] = fabsf(scale) > 0.0001f ? (1.0f / scale) : 1.0f;
        VectorScale(out_transform->axis[i], scale, out_transform->scaled_axis[i]);
    }
}

static void VK_Entity_TransformPointWithTransform(const vk_entity_transform_t *transform,
                                                  const vec3_t local, vec3_t out)
{
    if (!transform || !local || !out) {
        return;
    }

    VectorCopy(transform->origin, out);
    VectorMA(out, local[0], transform->scaled_axis[0], out);
    VectorMA(out, local[1], transform->scaled_axis[1], out);
    VectorMA(out, local[2], transform->scaled_axis[2], out);
}

static void VK_Entity_TransformPointInverseWithTransform(const vk_entity_transform_t *transform,
                                                         const vec3_t world, vec3_t out)
{
    if (!transform || !world || !out) {
        return;
    }

    vec3_t rel;
    VectorSubtract(world, transform->origin, rel);
    out[0] = DotProduct(rel, transform->axis[0]) * transform->inv_scale[0];
    out[1] = DotProduct(rel, transform->axis[1]) * transform->inv_scale[1];
    out[2] = DotProduct(rel, transform->axis[2]) * transform->inv_scale[2];
}

static void VK_Entity_TransformNormalWithTransform(const vk_entity_transform_t *transform,
                                                   const vec3_t local, vec3_t out)
{
    if (!transform || !local || !out) {
        return;
    }

    VectorClear(out);
    VectorMA(out, local[0] * transform->inv_scale[0], transform->axis[0], out);
    VectorMA(out, local[1] * transform->inv_scale[1], transform->axis[1], out);
    VectorMA(out, local[2] * transform->inv_scale[2], transform->axis[2], out);
    if (VectorNormalize(out) <= 0.0001f) {
        VectorSet(out, 0.0f, 0.0f, 1.0f);
    }
}

static void VK_Entity_FaceNormal(const vk_vertex_t tri[3], vec3_t out)
{
    vec3_t edge1;
    vec3_t edge2;
    VectorSubtract(tri[1].pos, tri[0].pos, edge1);
    VectorSubtract(tri[2].pos, tri[0].pos, edge2);
    CrossProduct(edge1, edge2, out);
    if (VectorNormalize(out) <= 0.0001f) {
        VectorSet(out, 0.0f, 0.0f, 1.0f);
    }
}

static void VK_Entity_SetTriNormal(vk_vertex_t tri[3], const vec3_t normal)
{
    for (int i = 0; i < 3; i++) {
        VectorCopy(normal, tri[i].normal);
    }
}

static void VK_Entity_ClearBspTextureCache(void)
{
    // A map change may replace the BSP while older frames still reference its
    // immutable geometry. This is an infrequent lifecycle transition, so
    // retire it only after the queue is idle rather than risking a live-buffer
    // destroy.
    if (vk_entity.bmodel_gpu_vertex_buffer && vk_entity.ctx &&
        vk_entity.ctx->device) {
        vkDeviceWaitIdle(vk_entity.ctx->device);
    }
    VK_Entity_DestroyBspGpuGeometry();
    free(vk_entity.bmodel_texture_handles);
    free(vk_entity.bmodel_texture_sets);
    free(vk_entity.bmodel_texture_inv_sizes);
    free(vk_entity.bmodel_texture_transparent);
    vk_entity.bmodel_texture_handles = NULL;
    vk_entity.bmodel_texture_sets = NULL;
    vk_entity.bmodel_texture_inv_sizes = NULL;
    vk_entity.bmodel_texture_transparent = NULL;
    vk_entity.bmodel_texture_count = 0;
    vk_entity.bmodel_texture_bsp = NULL;
}

static bool VK_Entity_EnsureBspTextureCache(const bsp_t *bsp)
{
    if (!bsp || bsp->numtexinfo <= 0 || !bsp->texinfo) {
        VK_Entity_ClearBspTextureCache();
        return false;
    }

    if (vk_entity.bmodel_texture_bsp == bsp &&
        vk_entity.bmodel_texture_handles &&
        vk_entity.bmodel_texture_sets &&
        vk_entity.bmodel_texture_inv_sizes &&
        vk_entity.bmodel_texture_transparent &&
        vk_entity.bmodel_texture_count == bsp->numtexinfo) {
        return true;
    }

    VK_Entity_ClearBspTextureCache();

    size_t texture_count = (size_t)bsp->numtexinfo;
    vk_entity.bmodel_texture_handles =
        VK_Entity_CallocArray(texture_count, sizeof(*vk_entity.bmodel_texture_handles),
                              "BSP texture handles");
    vk_entity.bmodel_texture_sets =
        VK_Entity_CallocArray(texture_count, sizeof(*vk_entity.bmodel_texture_sets),
                              "BSP texture descriptor sets");
    vk_entity.bmodel_texture_inv_sizes =
        VK_Entity_CallocArray(texture_count, sizeof(*vk_entity.bmodel_texture_inv_sizes),
                              "BSP texture inverse sizes");
    vk_entity.bmodel_texture_transparent =
        VK_Entity_CallocArray(texture_count,
                              sizeof(*vk_entity.bmodel_texture_transparent),
                              "BSP texture transparency flags");
    if (!vk_entity.bmodel_texture_handles || !vk_entity.bmodel_texture_sets ||
        !vk_entity.bmodel_texture_inv_sizes || !vk_entity.bmodel_texture_transparent) {
        VK_Entity_ClearBspTextureCache();
        return false;
    }

    vk_entity.bmodel_texture_bsp = bsp;
    vk_entity.bmodel_texture_count = bsp->numtexinfo;
    return true;
}

static void VK_Entity_BuildFramePush(const refdef_t *fd, const bsp_t *world_bsp,
                                     renderer_view_push_t *out_push)
{
    if (!fd || !out_push) {
        return;
    }

    float znear = 4.0f;
    float zfar = 8192.0f;

    // Keep entity depth projection in sync with VK world pass.
    if (world_bsp && world_bsp->numnodes > 0) {
        vec3_t extents;
        VectorSubtract(world_bsp->nodes[0].maxs, world_bsp->nodes[0].mins, extents);
        float radius = VectorLength(extents) * 0.5f;
        zfar = max(2048.0f, radius * 8.0f);
    }

    R_BuildViewPush(fd, znear, zfar, out_push);
}

static void VK_Entity_BuildWeaponFramePush(const refdef_t *fd, const bsp_t *world_bsp,
                                           renderer_view_push_t *out_push)
{
    if (!fd || !out_push) {
        return;
    }

    float znear = 4.0f;
    float zfar = 8192.0f;

    if (world_bsp && world_bsp->numnodes > 0) {
        vec3_t extents;
        VectorSubtract(world_bsp->nodes[0].maxs, world_bsp->nodes[0].mins, extents);
        float radius = VectorLength(extents) * 0.5f;
        zfar = max(2048.0f, radius * 8.0f);
    }

    float fov_x = fd->fov_x;
    float fov_y = fd->fov_y;
    float reflect_x = 1.0f;
    float gunfov = 0.0f;
    int gun = 0;
    int hand = 0;

    if (Cvar_VariableValue) {
        gunfov = Cvar_VariableValue("cl_gunfov");
    } else if (Cvar_VariableInteger) {
        gunfov = (float)Cvar_VariableInteger("cl_gunfov");
    }

    if (gunfov > 0.0f) {
        fov_x = Q_clipf(gunfov, 30.0f, 160.0f);
        fov_y = V_CalcFov(fov_x, 4.0f, 3.0f);
        if (fd->height > 0 && fd->width > 0) {
            fov_x = V_CalcFov(fov_y, (float)fd->height, (float)fd->width);
        }
    }

    if (Cvar_VariableInteger) {
        gun = Cvar_VariableInteger("cl_gun");
        hand = Cvar_VariableInteger("hand");
    }
    if ((hand == 1 && gun == 1) || gun == 3) {
        reflect_x = -1.0f;
    }

    R_BuildViewPushEx(fd, fov_x, fov_y, reflect_x, znear, zfar, out_push);
}

static bool VK_Entity_ResolveAnimationFrames(const refdef_t *fd, uint32_t num_frames,
                                             unsigned frame_in, unsigned oldframe_in,
                                             float backlerp_in,
                                             uint32_t *out_frame, uint32_t *out_oldframe,
                                             float *out_backlerp, float *out_frontlerp)
{
    if (!out_frame || !out_oldframe || !out_backlerp || !out_frontlerp || !num_frames) {
        return false;
    }

    uint32_t frame = frame_in;
    uint32_t oldframe = oldframe_in;
    if (fd && fd->extended) {
        frame %= num_frames;
        oldframe %= num_frames;
    } else {
        if (frame >= num_frames) {
            frame = 0;
        }
        if (oldframe >= num_frames) {
            oldframe = 0;
        }
    }

    float backlerp = Q_clipf(backlerp_in, 0.0f, 1.0f);
    if (backlerp == 0.0f) {
        oldframe = frame;
    }

    *out_frame = frame;
    *out_oldframe = oldframe;
    *out_backlerp = backlerp;
    *out_frontlerp = 1.0f - backlerp;
    return true;
}

static void VK_Entity_AddPointToBounds(const vec3_t point,
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

static bool VK_Entity_ModelBoundsForRefdef(const vk_model_t *model,
                                           const entity_t *ent,
                                           const refdef_t *fd,
                                           vec3_t local_mins,
                                           vec3_t local_maxs)
{
    if (!model || !ent || model->type != VK_MODEL_MD2 ||
        !model->md2.frame_bounds || !model->md2.num_frames) {
        return false;
    }

    uint32_t frame, oldframe;
    float backlerp, frontlerp;
    if (!VK_Entity_ResolveAnimationFrames(fd, model->md2.num_frames,
                                          ent->frame, ent->oldframe,
                                          ent->backlerp, &frame, &oldframe,
                                          &backlerp, &frontlerp)) {
        return false;
    }

    const vk_md2_frame_bounds_t *new_bounds =
        &model->md2.frame_bounds[frame];
    const vk_md2_frame_bounds_t *old_bounds =
        &model->md2.frame_bounds[oldframe];
    for (int axis = 0; axis < 3; axis++) {
        local_mins[axis] = min(new_bounds->mins[axis], old_bounds->mins[axis]);
        local_maxs[axis] = max(new_bounds->maxs[axis], old_bounds->maxs[axis]);
    }
    return true;
}

bool VK_Entity_ModelBounds(qhandle_t handle, const entity_t *ent,
                           vec3_t local_mins, vec3_t local_maxs)
{
    if (!vk_entity.initialized || !ent || handle <= 0 ||
        handle > vk_entity.num_models) {
        return false;
    }

    return VK_Entity_ModelBoundsForRefdef(&vk_entity.models[handle - 1], ent,
                                          NULL, local_mins, local_maxs);
}

// The side planes match GL_SetupFrustum.  Build them once per entity frame;
// model culling can otherwise repeat trig and plane setup for every entity.
static bool VK_Entity_BuildCullFrustum(const refdef_t *fd,
                                       vk_entity_cull_frustum_t *out_frustum)
{
    if (!fd || !out_frustum) {
        return false;
    }

    vec3_t view_axis[3];
    AnglesToAxis(fd->viewangles, view_axis);
    const float horizontal = DEG2RAD(fd->fov_x * 0.5f);
    const float vertical = DEG2RAD(fd->fov_y * 0.5f);
    const float horizontal_sin = sinf(horizontal);
    const float horizontal_cos = cosf(horizontal);
    const float vertical_sin = sinf(vertical);
    const float vertical_cos = cosf(vertical);

    cplane_t *planes = out_frustum->planes;
    VectorScale(view_axis[0], horizontal_sin, planes[0].normal);
    VectorMA(planes[0].normal, horizontal_cos, view_axis[1], planes[0].normal);
    VectorScale(view_axis[0], horizontal_sin, planes[1].normal);
    VectorMA(planes[1].normal, -horizontal_cos, view_axis[1], planes[1].normal);
    VectorScale(view_axis[0], vertical_sin, planes[2].normal);
    VectorMA(planes[2].normal, vertical_cos, view_axis[2], planes[2].normal);
    VectorScale(view_axis[0], vertical_sin, planes[3].normal);
    VectorMA(planes[3].normal, -vertical_cos, view_axis[2], planes[3].normal);

    for (uint32_t i = 0; i < q_countof(out_frustum->planes); i++) {
        planes[i].dist = DotProduct(fd->vieworg, planes[i].normal);
        planes[i].type = PLANE_NON_AXIAL;
        SetPlaneSignbits(&planes[i]);
    }
    return true;
}

// OpenGL culls ordinary alias and inline-BSP models against the four view
// side planes before it expands or submits their geometry. Use a transformed
// world AABB here instead of an exact OBB: it is deliberately conservative
// for rotated/non-uniformly scaled entities, so it can only leave additional
// off-screen work in place and can never reject a potentially visible model.
static bool VK_Entity_CullModelBounds(const vk_entity_cull_frustum_t *frustum,
                                      const entity_t *ent,
                                      const vec3_t local_mins,
                                      const vec3_t local_maxs)
{
    if (!frustum || !ent || !vk_cull_models || !vk_cull_models->integer ||
        (ent->flags & RF_WEAPONMODEL)) {
        return false;
    }

    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);

    vec3_t world_mins;
    vec3_t world_maxs;
    if (VectorEmpty(ent->angles) &&
        (!ent->scale[0] || ent->scale[0] == 1.0f) &&
        (!ent->scale[1] || ent->scale[1] == 1.0f) &&
        (!ent->scale[2] || ent->scale[2] == 1.0f)) {
        VectorAdd(local_mins, transform.origin, world_mins);
        VectorAdd(local_maxs, transform.origin, world_maxs);
    } else {
        ClearBounds(world_mins, world_maxs);
        for (uint32_t corner = 0; corner < 8; corner++) {
            vec3_t local = {
                (corner & BIT(0)) ? local_maxs[0] : local_mins[0],
                (corner & BIT(1)) ? local_maxs[1] : local_mins[1],
                (corner & BIT(2)) ? local_maxs[2] : local_mins[2],
            };
            vec3_t world;
            VK_Entity_TransformPointWithTransform(&transform, local, world);
            VK_Entity_AddPointToBounds(world, world_mins, world_maxs);
        }
    }

    for (uint32_t i = 0; i < q_countof(frustum->planes); i++) {
        if (BoxOnPlaneSide(world_mins, world_maxs, &frustum->planes[i]) == BOX_BEHIND) {
            return true;
        }
    }

    return false;
}

const char *VK_Entity_ModelName(qhandle_t handle)
{
    if (!vk_entity.initialized || handle <= 0 ||
        handle > vk_entity.num_models) {
        return "";
    }
    const vk_model_t *model = &vk_entity.models[handle - 1];
    return model->type ? model->name : "";
}

static bool VK_Entity_AddSprite(const entity_t *ent, const vec3_t view_axis[3], const vk_model_t *model,
                                bool depth_hack, bool weapon_model)
{
    if (!model->sprite.num_frames || !model->sprite.frames) {
        return true;
    }

    uint32_t frame = ent->frame % model->sprite.num_frames;
    const vk_sprite_frame_t *sf = &model->sprite.frames[frame];
    if (!sf->image) {
        return true;
    }

    float scale = ent->scale[0] ? ent->scale[0] : 1.0f;
    vec3_t left, right, down, up;
    VectorScale(view_axis[1], sf->origin_x * scale, left);
    VectorScale(view_axis[1], (sf->origin_x - sf->width) * scale, right);
    VectorScale(view_axis[2], -sf->origin_y * scale, down);
    VectorScale(view_axis[2], (sf->height - sf->origin_y) * scale, up);

    color_t color = VK_Entity_LitColor(ent, true, false, 0.0f, 0, NULL);
    VkDescriptorSet set = VK_Entity_SetForImage(sf->image);
    imageflags_t image_flags = VK_UI_GetImageFlags(sf->image);
    bool cutout = !(ent->flags & RF_TRANSLUCENT) && color.a == 255 &&
                  (image_flags & (IF_PALETTED | IF_TRANSPARENT)) ==
                      (IF_PALETTED | IF_TRANSPARENT);
    bool alpha = !cutout && ((ent->flags & RF_TRANSLUCENT) || color.a < 255 ||
                             (image_flags & IF_TRANSPARENT));
    uint32_t flags = VK_Entity_LightingFlags(ent, true);
    if (cutout) {
        flags |= VK_ENTITY_VERTEX_ALPHATEST;
    }

    vk_vertex_t v0 = { .uv = { 0, 1 }, .color = color.u32, .flags = flags, .normal = { 0, 0, 1 } };
    vk_vertex_t v1 = { .uv = { 0, 0 }, .color = color.u32, .flags = flags, .normal = { 0, 0, 1 } };
    vk_vertex_t v2 = { .uv = { 1, 1 }, .color = color.u32, .flags = flags, .normal = { 0, 0, 1 } };
    vk_vertex_t v3 = { .uv = { 1, 0 }, .color = color.u32, .flags = flags, .normal = { 0, 0, 1 } };
    VectorAdd3(ent->origin, down, left, v0.pos);
    VectorAdd3(ent->origin, up, left, v1.pos);
    VectorAdd3(ent->origin, down, right, v2.pos);
    VectorAdd3(ent->origin, up, right, v3.pos);

    if (cutout) {
        return VK_Entity_EmitTriCutout(&v0, &v1, &v2, set, depth_hack,
                                       weapon_model) &&
               VK_Entity_EmitTriCutout(&v2, &v1, &v3, set, depth_hack,
                                       weapon_model);
    }
    // GL keeps depth testing but disables depth writes for every sprite,
    // including completely opaque truecolour sources. Preserve that state
    // without paying for alpha blending by selecting the pre-created native
    // opaque/no-depth-write pipeline below.
    if (!alpha) {
        return VK_Entity_EmitTriOpaqueNoDepth(&v0, &v1, &v2, set, depth_hack,
                                              weapon_model) &&
               VK_Entity_EmitTriOpaqueNoDepth(&v2, &v1, &v3, set, depth_hack,
                                              weapon_model);
    }
    return VK_Entity_EmitTri(&v0, &v1, &v2, set, alpha, depth_hack,
                             weapon_model) &&
           VK_Entity_EmitTri(&v2, &v1, &v3, set, alpha, depth_hack,
                             weapon_model);
}

static vk_vertex_t VK_Entity_BeamVertex(const vec3_t position, float u, float v,
                                        color_t color)
{
    vk_vertex_t vertex = {
        .uv = { u, v },
        .color = color.u32,
        .flags = VK_ENTITY_VERTEX_FULLBRIGHT |
                 VK_ENTITY_VERTEX_NO_SHADOW |
                 VK_ENTITY_VERTEX_NO_DLIGHT,
        .normal = { 0, 0, 1 },
    };
    VectorCopy(position, vertex.pos);
    return vertex;
}

static void VK_Entity_AddOriginAxes(const entity_t *ent)
{
    // GL's gl_showorigins draws three sixteen-unit, depth-tested axes after
    // each non-weapon model. Use the same un-interpolated entity origin and
    // scaled axes while leaving inline BSP, beam, and flare classification on
    // their existing paths.
    if (!ent || (ent->flags & RF_WEAPONMODEL)) {
        return;
    }
    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);
    vec3_t end;
    VectorMA(ent->origin, 16.0f, transform.scaled_axis[0], end);
    R_AddDebugLine(ent->origin, end, COLOR_RED, 0, true);
    VectorMA(ent->origin, 16.0f, transform.scaled_axis[1], end);
    R_AddDebugLine(ent->origin, end, COLOR_GREEN, 0, true);
    VectorMA(ent->origin, 16.0f, transform.scaled_axis[2], end);
    R_AddDebugLine(ent->origin, end, COLOR_BLUE, 0, true);
}

static bool VK_Entity_AddSimpleBeam(const vec3_t start, const vec3_t end,
                                    const vec3_t vieworg, color_t color,
                                    float width, bool depth_hack)
{
    vec3_t direction, to_view, right;
    VectorSubtract(end, start, direction);
    VectorSubtract(vieworg, start, to_view);
    CrossProduct(direction, to_view, right);
    if (VectorNormalize(right) < 0.1f) {
        return true;
    }
    VectorScale(right, width, right);

    const VkDescriptorSet set = vk_entity.beam_set ? vk_entity.beam_set
                                                    : vk_entity.white_set;
    if (vk_beam_gpu_instancing && vk_beam_gpu_instancing->integer &&
        vk_entity.gpu_beam_available) {
        if (!VK_Entity_EnsureBeamInstanceCapacity(
                vk_entity.beam_instance_count + 1)) {
            return false;
        }
        const uint32_t instance_index = vk_entity.beam_instance_count++;
        vk_beam_gpu_instance_t *instance =
            &vk_entity.beam_instances[instance_index];
        VectorCopy(start, instance->start);
        VectorCopy(end, instance->end);
        VectorCopy(right, instance->right);
        instance->color = color.u32;
        return VK_Entity_AppendGpuBeamBatch(instance_index, set, depth_hack);
    }

    vec3_t positions[4];
    VectorAdd(start, right, positions[0]);
    VectorSubtract(start, right, positions[1]);
    VectorSubtract(end, right, positions[2]);
    VectorAdd(end, right, positions[3]);

    vk_vertex_t vertices[4] = {
        VK_Entity_BeamVertex(positions[0], 0.0f, 0.0f, color),
        VK_Entity_BeamVertex(positions[1], 1.0f, 0.0f, color),
        VK_Entity_BeamVertex(positions[2], 1.0f, 1.0f, color),
        VK_Entity_BeamVertex(positions[3], 0.0f, 1.0f, color),
    };

    return VK_Entity_EmitTri(&vertices[0], &vertices[2], &vertices[3],
                             set, true, depth_hack, false) &&
           VK_Entity_EmitTri(&vertices[0], &vertices[1], &vertices[2],
                             set, true, depth_hack, false);
}

static bool VK_Entity_AddPolyBeam(const vec3_t *segments, int num_segments,
                                  color_t color, float width, bool depth_hack)
{
    vec3_t direction, right, up;
    VectorSubtract(segments[num_segments], segments[0], direction);
    if (VectorNormalize(direction) < 0.1f) {
        return true;
    }

    MakeNormalVectors(direction, right, up);
    VectorScale(right, width, right);

    vec3_t offsets[VK_ENTITY_BEAM_POINTS];
    for (int i = 0; i < VK_ENTITY_BEAM_POINTS; i++) {
        RotatePointAroundVector(offsets[i], direction, right,
                                (360.0f / VK_ENTITY_BEAM_POINTS) * i);
    }

    for (int segment = 0; segment < num_segments; segment++) {
        for (int point = 0; point < VK_ENTITY_BEAM_POINTS; point++) {
            int next = (point + 1) % VK_ENTITY_BEAM_POINTS;
            vec3_t positions[4];
            VectorAdd(offsets[point], segments[segment], positions[0]);
            VectorAdd(offsets[point], segments[segment + 1], positions[1]);
            VectorAdd(offsets[next], segments[segment + 1], positions[2]);
            VectorAdd(offsets[next], segments[segment], positions[3]);

            vk_vertex_t vertices[4] = {
                VK_Entity_BeamVertex(positions[0], 0.0f, 0.0f, color),
                VK_Entity_BeamVertex(positions[1], 0.0f, 0.0f, color),
                VK_Entity_BeamVertex(positions[2], 0.0f, 0.0f, color),
                VK_Entity_BeamVertex(positions[3], 0.0f, 0.0f, color),
            };

            if (!VK_Entity_EmitTri(&vertices[0], &vertices[1], &vertices[2],
                                   vk_entity.white_set, true, depth_hack, false) ||
                !VK_Entity_EmitTri(&vertices[0], &vertices[2], &vertices[3],
                                   vk_entity.white_set, true, depth_hack, false)) {
                return false;
            }
        }
    }

    return true;
}

static bool VK_Entity_AddLightningBeam(const vec3_t start, const vec3_t end,
                                       const vec3_t vieworg, color_t color,
                                       float width, bool polygonal,
                                       bool depth_hack)
{
    vec3_t direction;
    vec3_t segments[VK_ENTITY_MAX_LIGHTNING_SEGMENTS + 1];
    vec3_t right, up;
    VectorSubtract(end, start, direction);
    float length = VectorNormalize(direction);

    int max_segments = Q_clip(length / VK_ENTITY_MIN_LIGHTNING_SEGMENT_LENGTH,
                              1, VK_ENTITY_MAX_LIGHTNING_SEGMENTS);
    int num_segments;
    if (max_segments <= VK_ENTITY_MIN_LIGHTNING_SEGMENTS) {
        num_segments = max_segments;
    } else {
        num_segments = VK_ENTITY_MIN_LIGHTNING_SEGMENTS +
                       Com_SlowRand() % (max_segments - VK_ENTITY_MIN_LIGHTNING_SEGMENTS + 1);
    }

    if (num_segments > 1) {
        MakeNormalVectors(direction, right, up);
    }

    float segment_length = length / num_segments;
    for (int i = 1; i < num_segments; i++) {
        vec3_t point;
        VectorMA(start, i * segment_length, direction, point);

        float offset = Com_SlowCrand() * (segment_length * 0.35f);
        VectorMA(point, offset, right, point);

        offset = Com_SlowCrand() * (segment_length * 0.35f);
        VectorMA(point, offset, up, segments[i]);
    }

    VectorCopy(start, segments[0]);
    VectorCopy(end, segments[num_segments]);

    if (polygonal) {
        return VK_Entity_AddPolyBeam(segments, num_segments, color, width,
                                     depth_hack);
    }
    for (int i = 0; i < num_segments; i++) {
        if (!VK_Entity_AddSimpleBeam(segments[i], segments[i + 1], vieworg,
                                     color, width, depth_hack)) {
            return false;
        }
    }
    return true;
}

static bool VK_Entity_AddBeam(const entity_t *ent, const refdef_t *fd)
{
    vec3_t segments[2];
    VectorCopy(ent->origin, segments[0]);
    VectorCopy(ent->oldorigin, segments[1]);

    color_t color;
    if (ent->skinnum == -1) {
        color = ent->rgba;
    } else {
        extern uint32_t d_8to24table[256];
        color = COLOR_U32(d_8to24table[ent->skinnum & 255]);
    }
    color.a = (uint8_t)((float)color.a * Q_clipf(ent->alpha, 0.0f, 1.0f) + 0.5f);

    bool polygonal = vk_beam_style && vk_beam_style->integer;
    float width_scale = polygonal ? 0.5f : 1.2f;
    float width = (float)abs((int16_t)ent->frame) * width_scale;
    if (width <= 0.0f) {
        return true;
    }

    bool depth_hack = (ent->flags & (RF_DEPTHHACK | RF_WEAPONMODEL)) != 0;
    if (ent->flags & RF_GLOW) {
        return VK_Entity_AddLightningBeam(segments[0], segments[1], fd->vieworg,
                                          color, width, polygonal, depth_hack);
    }
    if (polygonal) {
        return VK_Entity_AddPolyBeam(segments, 1, color, width, depth_hack);
    }
    return VK_Entity_AddSimpleBeam(segments[0], segments[1], fd->vieworg,
                                   color, width, depth_hack);
}

static void VK_Entity_PollFlareQuery(uint32_t query_index,
                                     vk_flare_state_t *state)
{
    if (!state || !state->pending || !vk_entity.flare_query_pool ||
        !vk_entity.ctx || !vk_entity.ctx->device) {
        return;
    }

    uint64_t result[2] = { 0, 0 };
    VkResult status = vkGetQueryPoolResults(
        vk_entity.ctx->device, vk_entity.flare_query_pool, query_index, 1,
        sizeof(result), result, sizeof(result),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (status == VK_SUCCESS && result[1]) {
        state->visible = result[0] != 0;
        state->pending = false;
    } else if (status != VK_SUCCESS && status != VK_NOT_READY) {
        Com_DPrintf("Vulkan flare query %u read failed: %d\n",
                    query_index, (int)status);
        state->visible = false;
        state->pending = false;
    }
}

static bool VK_Entity_FlareInFrustum(const refdef_t *fd,
                                     const vec3_t view_axis[3],
                                     const vec3_t origin)
{
    vec3_t relative, forward, side, normal;
    VectorSubtract(origin, fd->vieworg, relative);

    float angle = DEG2RAD(fd->fov_x * 0.5f);
    VectorScale(view_axis[0], sinf(angle), forward);
    VectorScale(view_axis[1], cosf(angle), side);
    VectorAdd(forward, side, normal);
    if (DotProduct(relative, normal) < -2.5f) {
        return false;
    }
    VectorSubtract(forward, side, normal);
    if (DotProduct(relative, normal) < -2.5f) {
        return false;
    }

    angle = DEG2RAD(fd->fov_y * 0.5f);
    VectorScale(view_axis[0], sinf(angle), forward);
    VectorScale(view_axis[2], cosf(angle), side);
    VectorAdd(forward, side, normal);
    if (DotProduct(relative, normal) < -2.5f) {
        return false;
    }
    VectorSubtract(forward, side, normal);
    return DotProduct(relative, normal) >= -2.5f;
}

static void VK_Entity_AdvanceFlare(vk_flare_state_t *state, float frametime)
{
    float target = state->visible ? 1.0f : 0.0f;
    float speed = vk_flare_fade_speed ? vk_flare_fade_speed->value : 8.0f;
    if (speed <= 0.0f) {
        state->fraction = target;
    } else if (state->fraction < target) {
        state->fraction = min(target, state->fraction + speed * frametime);
    } else if (state->fraction > target) {
        state->fraction = max(target, state->fraction - speed * frametime);
    }
}

static vk_vertex_t VK_Entity_FlareVertex(const vec3_t position, float u, float v,
                                         color_t color, uint32_t flags)
{
    vk_vertex_t vertex = {
        .uv = { u, v },
        .color = color.u32,
        .flags = flags,
        .normal = { 0, 0, 1 },
    };
    VectorCopy(position, vertex.pos);
    return vertex;
}

static bool VK_Entity_AddFlareQuery(const entity_t *ent, const refdef_t *fd,
                                    const vec3_t view_axis[3], const bsp_t *bsp,
                                    uint32_t query_index)
{
    vec3_t direction, origin;
    VectorSubtract(ent->origin, fd->vieworg, direction);
    float distance = DotProduct(direction, view_axis[0]);
    float scale = 2.5f;
    if (distance > 20.0f) {
        scale += distance * 0.004f;
    }

    VectorCopy(ent->origin, origin);
    if (bsp && bsp->nodes) {
        const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, ent->origin);
        if (leaf && (leaf->contents[0] & CONTENTS_SOLID) &&
            VectorNormalize(direction) > 0.0f) {
            VectorMA(ent->origin, -5.0f, direction, origin);
        }
    }

    vec3_t left, right, down, up, positions[4];
    VectorScale(view_axis[1], scale, left);
    VectorScale(view_axis[1], -scale, right);
    VectorScale(view_axis[2], -scale, down);
    VectorScale(view_axis[2], scale, up);
    VectorAdd3(origin, down, left, positions[0]);
    VectorAdd3(origin, up, left, positions[1]);
    VectorAdd3(origin, down, right, positions[2]);
    VectorAdd3(origin, up, right, positions[3]);

    uint32_t flags = VK_ENTITY_VERTEX_FULLBRIGHT |
                     VK_ENTITY_VERTEX_NO_SHADOW |
                     VK_ENTITY_VERTEX_NO_DLIGHT |
                     VK_ENTITY_VERTEX_NO_FOG;
    vk_vertex_t vertices[4] = {
        VK_Entity_FlareVertex(positions[0], 0.0f, 0.0f, COLOR_WHITE, flags),
        VK_Entity_FlareVertex(positions[1], 0.0f, 0.0f, COLOR_WHITE, flags),
        VK_Entity_FlareVertex(positions[2], 0.0f, 0.0f, COLOR_WHITE, flags),
        VK_Entity_FlareVertex(positions[3], 0.0f, 0.0f, COLOR_WHITE, flags),
    };

    return VK_Entity_EmitTriSpecial(&vertices[0], &vertices[1], &vertices[2],
                                    vk_entity.white_set, query_index, false, true) &&
           VK_Entity_EmitTriSpecial(&vertices[2], &vertices[1], &vertices[3],
                                    vk_entity.white_set, query_index, false, true);
}

static bool VK_Entity_AddFlareVisual(const entity_t *ent, const refdef_t *fd,
                                     const vec3_t view_axis[3], float fraction)
{
    if (fraction <= 0.0f) {
        return true;
    }

    imageflags_t image_flags = VK_UI_GetImageFlags(ent->skin);
    bool default_flare = (image_flags & IF_DEFAULT_FLARE) != 0;
    float scale = (float)(25 << (default_flare ? 1 : 0)) *
                  ent->scale[0] * fraction;

    vec3_t left, right, down, up;
    if (ent->flags & RF_FLARE_LOCK_ANGLE) {
        VectorScale(view_axis[1], scale, left);
        VectorScale(view_axis[1], -scale, right);
        VectorScale(view_axis[2], -scale, down);
        VectorScale(view_axis[2], scale, up);
    } else {
        vec3_t direction, flare_right, flare_up;
        VectorSubtract(ent->origin, fd->vieworg, direction);
        if (VectorNormalize(direction) <= 0.0f) {
            return true;
        }
        MakeNormalVectors(direction, flare_right, flare_up);
        VectorScale(flare_right, -scale, left);
        VectorScale(flare_right, scale, right);
        VectorScale(flare_up, -scale, down);
        VectorScale(flare_up, scale, up);
    }

    vec3_t positions[5];
    VectorCopy(ent->origin, positions[0]);
    VectorAdd3(ent->origin, down, left, positions[1]);
    VectorAdd3(ent->origin, up, left, positions[2]);
    VectorAdd3(ent->origin, up, right, positions[3]);
    VectorAdd3(ent->origin, down, right, positions[4]);

    color_t inner = ent->rgba;
    inner.a = (uint8_t)((128 + (default_flare ? 32 : 0)) *
                        Q_clipf(ent->alpha * fraction, 0.0f, 1.0f));
    color_t outer = inner;
    if (ent->flags & (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE)) {
        outer.r = outer.g = outer.b = 0;
        if (ent->flags & RF_SHELL_RED) {
            outer.r = 255;
        }
        if (ent->flags & RF_SHELL_GREEN) {
            outer.g = 255;
        }
        if (ent->flags & RF_SHELL_BLUE) {
            outer.b = 255;
        }
    }

    uint32_t flags = VK_ENTITY_VERTEX_FULLBRIGHT |
                     VK_ENTITY_VERTEX_NO_SHADOW |
                     VK_ENTITY_VERTEX_NO_DLIGHT |
                     VK_ENTITY_VERTEX_NO_FOG;
    if (default_flare) {
        flags |= VK_ENTITY_VERTEX_DEFAULT_FLARE;
    }
    vk_vertex_t vertices[5] = {
        VK_Entity_FlareVertex(positions[0], 0.5f, 0.5f, inner, flags),
        VK_Entity_FlareVertex(positions[1], 0.0f, 1.0f, outer, flags),
        VK_Entity_FlareVertex(positions[2], 0.0f, 0.0f, outer, flags),
        VK_Entity_FlareVertex(positions[3], 1.0f, 0.0f, outer, flags),
        VK_Entity_FlareVertex(positions[4], 1.0f, 1.0f, outer, flags),
    };

    VkDescriptorSet set = VK_Entity_SetForImage(ent->skin);
    return VK_Entity_EmitTriSpecial(&vertices[0], &vertices[2], &vertices[3],
                                    set, UINT32_MAX, true, false) &&
           VK_Entity_EmitTriSpecial(&vertices[0], &vertices[3], &vertices[4],
                                    set, UINT32_MAX, true, false) &&
           VK_Entity_EmitTriSpecial(&vertices[0], &vertices[4], &vertices[1],
                                    set, UINT32_MAX, true, false) &&
           VK_Entity_EmitTriSpecial(&vertices[0], &vertices[1], &vertices[2],
                                    set, UINT32_MAX, true, false);
}

static bool VK_Entity_AddFlares(const refdef_t *fd, const vec3_t view_axis[3],
                                const bsp_t *bsp)
{
    if (!vk_entity.flare_query_pool) {
        return true;
    }

    uint32_t now = com_eventTime;
    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *ent = &fd->entities[i];
        if (!(ent->flags & RF_FLARE) || ent->skinnum < 0 ||
            ent->skinnum >= MAX_EDICTS) {
            continue;
        }

        uint32_t query_index = (uint32_t)ent->skinnum;
        vk_flare_state_t *state = &vk_entity.flare_states[query_index];
        VK_Entity_PollFlareQuery(query_index, state);

        if (now - state->timestamp >= 2500u) {
            state->pending = false;
            state->visible = false;
            state->fraction = 0.0f;
        }

        bool in_frustum = VK_Entity_FlareInFrustum(fd, view_axis, ent->origin);
        if (!in_frustum) {
            state->pending = false;
            state->visible = false;
        } else if (!state->pending && now - state->timestamp > 33u) {
            if (!VK_Entity_AddFlareQuery(ent, fd, view_axis, bsp, query_index)) {
                return false;
            }
            state->timestamp = now;
            state->pending = true;
            vk_entity.flare_queries_scheduled[query_index] = true;
        }

        VK_Entity_AdvanceFlare(state, fd->frametime);
    }

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *ent = &fd->entities[i];
        if (!(ent->flags & RF_FLARE) || ent->skinnum < 0 ||
            ent->skinnum >= MAX_EDICTS) {
            continue;
        }
        float fraction = vk_entity.flare_states[ent->skinnum].fraction;
        if (!VK_Entity_AddFlareVisual(ent, fd, view_axis, fraction)) {
            return false;
        }
    }

    return true;
}

static inline uint32_t VK_Entity_SurfEdgeVertexIndex(const bsp_t *bsp, const msurfedge_t *surfedge)
{
    const medge_t *edge = &bsp->edges[surfedge->edge];
    return surfedge->vert ? edge->v[1] : edge->v[0];
}

static bool VK_Entity_GetBspFaceTexture(const bsp_t *bsp, const mface_t *face,
                                        VkDescriptorSet *out_set,
                                        float *out_inv_w, float *out_inv_h,
                                        bool *out_is_transparent,
                                        bool *out_has_glowmap)
{
    if (out_set) {
        *out_set = VK_NULL_HANDLE;
    }
    if (out_inv_w) {
        *out_inv_w = 1.0f;
    }
    if (out_inv_h) {
        *out_inv_h = 1.0f;
    }
    if (out_is_transparent) {
        *out_is_transparent = false;
    }
    if (out_has_glowmap) {
        *out_has_glowmap = false;
    }

    if (!bsp || !face || !face->texinfo || !face->texinfo->name[0] || !out_set) {
        return false;
    }

    if (!VK_Entity_EnsureBspTextureCache(bsp)) {
        return false;
    }

    int tex_index = (int)(face->texinfo - bsp->texinfo);
    if (tex_index < 0 || tex_index >= vk_entity.bmodel_texture_count) {
        return false;
    }

    if (vk_entity.bmodel_texture_handles[tex_index] &&
        vk_entity.bmodel_texture_sets[tex_index]) {
        *out_set = vk_entity.bmodel_texture_sets[tex_index];
        if (out_inv_w) {
            *out_inv_w = vk_entity.bmodel_texture_inv_sizes[tex_index][0];
        }
        if (out_inv_h) {
            *out_inv_h = vk_entity.bmodel_texture_inv_sizes[tex_index][1];
        }
        if (out_is_transparent) {
            *out_is_transparent = vk_entity.bmodel_texture_transparent[tex_index];
        }
        if (out_has_glowmap) {
            *out_has_glowmap =
                VK_UI_HasGlowmap(vk_entity.bmodel_texture_handles[tex_index]);
        }
        return true;
    }

    imageflags_t flags = IF_REPEAT;
    if (face->texinfo->c.flags & SURF_TRANS_MASK) {
        flags |= IF_TRANSPARENT;
    } else if (!(face->texinfo->c.flags & SURF_WARP)) {
        flags |= IF_OPAQUE;
    }
    if (face->texinfo->c.flags & SURF_WARP) {
        flags |= IF_TURBULENT;
    }

    char path[MAX_QPATH];
    if (Q_concat(path, sizeof(path), "textures/", face->texinfo->name, ".wal") >= sizeof(path)) {
        return false;
    }

    qhandle_t handle = VK_UI_RegisterImage(path, IT_WALL, flags);
    if (!handle) {
        return false;
    }

    VkDescriptorSet set = VK_UI_GetDescriptorSetForImage(handle);
    if (!set) {
        return false;
    }

    int tex_w = 0;
    int tex_h = 0;
    if (!VK_UI_GetPicSize(&tex_w, &tex_h, handle) || tex_w <= 0 || tex_h <= 0) {
        tex_w = 64;
        tex_h = 64;
    }

    if (out_inv_w) {
        *out_inv_w = 1.0f / (float)tex_w;
    }
    if (out_inv_h) {
        *out_inv_h = 1.0f / (float)tex_h;
    }

    vk_entity.bmodel_texture_handles[tex_index] = handle;
    vk_entity.bmodel_texture_sets[tex_index] = set;
    vk_entity.bmodel_texture_inv_sizes[tex_index][0] = 1.0f / (float)tex_w;
    vk_entity.bmodel_texture_inv_sizes[tex_index][1] = 1.0f / (float)tex_h;
    vk_entity.bmodel_texture_transparent[tex_index] = VK_UI_IsImageTransparent(handle);
    if (out_is_transparent) {
        *out_is_transparent = vk_entity.bmodel_texture_transparent[tex_index];
    }
    if (out_has_glowmap) {
        *out_has_glowmap = VK_UI_HasGlowmap(handle);
    }
    *out_set = set;
    return true;
}

static bool VK_Entity_EnsureBspGpuGeometry(const bsp_t *bsp)
{
    if (!bsp || !bsp->models || !bsp->faces || !bsp->vertices ||
        !bsp->edges || !bsp->surfedges || bsp->numfaces <= 0 ||
        bsp->nummodels <= 1) {
        return false;
    }
    if (vk_entity.bmodel_gpu_bsp == bsp) {
        return vk_entity.bmodel_gpu_ready;
    }

    // This normally follows VK_Entity_ClearBspTextureCache at map change.
    // Keep the standalone guard so a caller can never destroy geometry used
    // by an already submitted frame.
    if (vk_entity.bmodel_gpu_vertex_buffer && vk_entity.ctx &&
        vk_entity.ctx->device) {
        vkDeviceWaitIdle(vk_entity.ctx->device);
    }
    VK_Entity_DestroyBspGpuGeometry();

    const size_t face_count = (size_t)bsp->numfaces;
    vk_bmodel_gpu_face_t *faces = VK_Entity_CallocArray(
        face_count, sizeof(*faces), "GPU BSP face metadata");
    byte *selected = VK_Entity_CallocArray(face_count, sizeof(*selected),
                                            "GPU BSP face selection");
    if (!faces || !selected) {
        free(faces);
        free(selected);
        goto fail;
    }

    bool has_inline_faces = false;
    for (int model_index = 1; model_index < bsp->nummodels; model_index++) {
        const mmodel_t *model = &bsp->models[model_index];
        if (!model->firstface || model->numfaces <= 0) {
            continue;
        }
        const ptrdiff_t first_face = model->firstface - bsp->faces;
        if (first_face < 0 || first_face >= bsp->numfaces ||
            model->numfaces > bsp->numfaces - first_face) {
            continue;
        }
        memset(selected + first_face, 1, (size_t)model->numfaces);
        has_inline_faces = true;
    }
    if (!has_inline_faces) {
        free(selected);
        vk_entity.bmodel_gpu_bsp = bsp;
        vk_entity.bmodel_gpu_faces = faces;
        vk_entity.bmodel_gpu_face_count = bsp->numfaces;
        vk_entity.bmodel_gpu_ready = true;
        return true;
    }

    uint64_t max_vertex_count = 0;
    for (int face_index = 0; face_index < bsp->numfaces; face_index++) {
        if (!selected[face_index]) {
            continue;
        }
        const mface_t *face = &bsp->faces[face_index];
        if (!face->texinfo || !face->firstsurfedge || face->numsurfedges < 3 ||
            !face->plane || (face->texinfo->c.flags &
                             (SURF_NODRAW | SURF_SKY))) {
            continue;
        }
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (!VK_Entity_GetBspFaceTexture(bsp, face, &set, NULL, NULL, NULL,
                                         NULL) || !set) {
            continue;
        }
        const uint64_t vertices = (uint64_t)(face->numsurfedges - 2) * 3u;
        if (vertices > UINT32_MAX - max_vertex_count) {
            Com_SetLastError("Vulkan entity: GPU BSP vertex count overflow");
            free(faces);
            free(selected);
            goto fail;
        }
        max_vertex_count += vertices;
    }

    vk_bmodel_gpu_vertex_t *vertices = NULL;
    if (max_vertex_count) {
        vertices = VK_Entity_CallocArray((size_t)max_vertex_count,
                                         sizeof(*vertices),
                                         "GPU BSP vertices");
        if (!vertices) {
            free(faces);
            free(selected);
            goto fail;
        }
    }

    uint32_t vertex_count = 0;
    for (int face_index = 0; face_index < bsp->numfaces; face_index++) {
        if (!selected[face_index]) {
            continue;
        }
        const mface_t *face = &bsp->faces[face_index];
        vk_bmodel_gpu_face_t *out_face = &faces[face_index];
        out_face->first_vertex = vertex_count;
        if (!face->texinfo || !face->firstsurfedge || face->numsurfedges < 3 ||
            !face->plane) {
            continue;
        }

        const surfflags_t surf_flags = face->texinfo->c.flags;
        if (surf_flags & (SURF_NODRAW | SURF_SKY)) {
            continue;
        }

        float inv_tex_w = 1.0f;
        float inv_tex_h = 1.0f;
        VkDescriptorSet set = VK_NULL_HANDLE;
        bool has_glowmap = false;
        if (!VK_Entity_GetBspFaceTexture(bsp, face, &set, &inv_tex_w,
                                         &inv_tex_h, NULL, &has_glowmap) ||
            !set) {
            continue;
        }

        const msurfedge_t *surfedges = face->firstsurfedge;
        if (surfedges[0].edge >= (uint32_t)bsp->numedges) {
            continue;
        }
        const uint32_t i0 = VK_Entity_SurfEdgeVertexIndex(bsp, &surfedges[0]);
        if (i0 >= (uint32_t)bsp->numvertices) {
            continue;
        }
        const mvertex_t *v0 = &bsp->vertices[i0];
        vec2_t first_lm_uv = { 0.0f, 0.0f };
        const bool face_lightmapped =
            face->lightmap != NULL &&
            VK_World_GetFaceLightmapUV(face, v0->point, first_lm_uv);
        uint32_t flags = 0;
        if (face_lightmapped) {
            flags |= VK_ENTITY_VERTEX_LIGHTMAP;
            if (has_glowmap) {
                flags |= VK_ENTITY_VERTEX_GLOWMAP;
            }
        }
        if (surf_flags & SURF_ALPHATEST) {
            flags |= VK_ENTITY_VERTEX_ALPHATEST;
        }
        if (VK_World_SurfaceUsesIntensity(bsp, face)) {
            flags |= VK_ENTITY_VERTEX_INTENSITY;
        }
        if (!face_lightmapped && !(surf_flags & SURF_TRANS_MASK)) {
            flags |= VK_ENTITY_VERTEX_TEXTURE_REPLACE;
        }

        vec3_t face_normal;
        VectorCopy(face->plane->normal, face_normal);
        if (face->drawflags & DSURF_PLANEBACK) {
            VectorInverse(face_normal);
        }
        const float face_alpha = (surf_flags & SURF_TRANS33)
            ? (84.0f / 255.0f)
            : (surf_flags & SURF_TRANS66) ? (168.0f / 255.0f) : 1.0f;

        for (int j = 1; j < face->numsurfedges - 1; j++) {
            if (surfedges[j].edge >= (uint32_t)bsp->numedges ||
                surfedges[j + 1].edge >= (uint32_t)bsp->numedges) {
                continue;
            }
            const uint32_t i1 = VK_Entity_SurfEdgeVertexIndex(bsp,
                                                                &surfedges[j]);
            const uint32_t i2 = VK_Entity_SurfEdgeVertexIndex(
                bsp, &surfedges[j + 1]);
            if (i1 >= (uint32_t)bsp->numvertices ||
                i2 >= (uint32_t)bsp->numvertices) {
                continue;
            }
            const mvertex_t *source_vertices[3] = {
                v0,
                &bsp->vertices[i1],
                &bsp->vertices[i2],
            };
            for (int k = 0; k < 3; k++) {
                vk_bmodel_gpu_vertex_t *out = &vertices[vertex_count++];
                const vec3_t local = {
                    source_vertices[k]->point[0],
                    source_vertices[k]->point[1],
                    source_vertices[k]->point[2],
                };
                VectorCopy(local, out->pos);
                out->uv[0] = (DotProduct(local, face->texinfo->axis[0]) +
                              face->texinfo->offset[0]) * inv_tex_w;
                out->uv[1] = (DotProduct(local, face->texinfo->axis[1]) +
                              face->texinfo->offset[1]) * inv_tex_h;
                if (face_lightmapped) {
                    vec2_t lm_uv;
                    VK_World_GetFaceLightmapUV(face, local, lm_uv);
                    out->lm_uv[0] = lm_uv[0];
                    out->lm_uv[1] = lm_uv[1];
                }
                VectorCopy(face_normal, out->normal);
                out->alpha = face_alpha;
                out->flags = flags;
            }
        }
        out_face->vertex_count = vertex_count - out_face->first_vertex;
        out_face->flags = flags;
    }
    free(selected);

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    vk_entity_static_upload_t upload = { 0 };
    bool ok = true;
    size_t bytes = 0;
    if (vertex_count) {
        ok = VK_Entity_ArrayBytes(vertex_count, sizeof(*vertices), &bytes,
                                  "GPU BSP vertices") &&
             VK_Entity_CreateBuffer(
                 bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &buffer, &memory, NULL,
                 "vkCreateBuffer(static BSP vertex)") &&
             VK_Entity_CreateBuffer(
                 bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &upload.staging, &upload.staging_memory, &upload.staging_mapped,
                 "vkCreateBuffer(static BSP vertex staging)");
        if (ok) {
            upload.destination = buffer;
            upload.bytes = bytes;
            upload.destination_access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            upload.destination_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            memcpy(upload.staging_mapped, vertices, bytes);
            ok = VK_Entity_CopyStaticBuffers(&upload, 1);
        }
    }
    VK_Entity_DestroyBuffer(&upload.staging, &upload.staging_memory,
                            &upload.staging_mapped);
    free(vertices);
    if (!ok) {
        VK_Entity_DestroyBuffer(&buffer, &memory, NULL);
        free(faces);
        goto fail;
    }

    vk_entity.bmodel_gpu_bsp = bsp;
    vk_entity.bmodel_gpu_faces = faces;
    vk_entity.bmodel_gpu_face_count = bsp->numfaces;
    vk_entity.bmodel_gpu_vertex_buffer = buffer;
    vk_entity.bmodel_gpu_vertex_memory = memory;
    vk_entity.bmodel_gpu_vertex_count = vertex_count;
    vk_entity.bmodel_gpu_ready = true;
    if (bytes) {
        VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY, bytes);
    }
    return true;

fail:
    vk_entity.bmodel_gpu_bsp = bsp;
    vk_entity.bmodel_gpu_failed = true;
    Com_WPrintf("Vulkan entity: static inline-BSP residency unavailable; "
                "using CPU expansion\n");
    return false;
}

static bool VK_Entity_AddGpuBspModel(const entity_t *ent, const refdef_t *fd,
                                     const bsp_t *bsp, const mmodel_t *model)
{
    if (!ent || !bsp || !model || !vk_entity.bmodel_gpu_ready ||
        vk_entity.bmodel_gpu_bsp != bsp || !vk_entity.bmodel_gpu_faces ||
        vk_entity.bmodel_gpu_face_count != bsp->numfaces) {
        return false;
    }

    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);
    vec3_t view_local = { 0.0f, 0.0f, 0.0f };
    if (fd) {
        VK_Entity_TransformPointInverseWithTransform(&transform, fd->vieworg,
                                                      view_local);
    }
    bool no_static_lighting = false;
    const color_t entity_color = VK_Entity_LitColor(
        ent, false, false, fd ? fd->time : 0.0f, fd ? fd->rdflags : 0,
        &no_static_lighting);
    uint32_t entity_flags = VK_Entity_LightingFlags(ent, false);
    if (no_static_lighting) {
        entity_flags |= VK_ENTITY_VERTEX_NO_ENTITY_MODULATE;
    }
    // This is intentionally frame-local rather than a material property:
    // enabling a sun receiver or dynamic light on the next frame must return
    // the same static geometry to the complete lighting pipeline.
    const bool fast_lit_receivers_inactive =
        (!vk_bmodel_fast_lit || vk_bmodel_fast_lit->integer) &&
        (entity_flags & ~VK_ENTITY_VERTEX_NO_ENTITY_MODULATE) == 0 &&
        !VK_Shadow_HasActiveReceiverLighting();
    const float entity_alpha = VK_Entity_Alpha(ent);
    uint32_t instance_index = UINT32_MAX;

    for (int i = 0; i < model->numfaces; i++) {
        const mface_t *face = &model->firstface[i];
        const ptrdiff_t face_index = face - bsp->faces;
        if (face_index < 0 || face_index >= vk_entity.bmodel_gpu_face_count ||
            !face->texinfo || !face->plane) {
            continue;
        }
        const vk_bmodel_gpu_face_t *gpu_face =
            &vk_entity.bmodel_gpu_faces[face_index];
        if (!gpu_face->vertex_count) {
            continue;
        }

        const surfflags_t surf_flags = face->texinfo->c.flags;
        if (surf_flags & (SURF_NODRAW | SURF_SKY)) {
            continue;
        }
        if (fd) {
            const float dot = PlaneDiffFast(view_local, face->plane);
            if ((face->drawflags & DSURF_PLANEBACK) ? (dot > 0.01f)
                                                    : (dot < -0.01f)) {
                continue;
            }
        }

        VkDescriptorSet set = VK_NULL_HANDLE;
        bool texture_transparent = false;
        if (!VK_Entity_GetBspFaceTexture(bsp, face, &set, NULL, NULL,
                                         &texture_transparent, NULL) || !set) {
            continue;
        }
        float face_alpha = entity_alpha;
        if (surf_flags & SURF_TRANS33) {
            face_alpha = min(face_alpha, 0.33f);
        } else if (surf_flags & SURF_TRANS66) {
            face_alpha = min(face_alpha, 0.66f);
        }
        const uint8_t alpha_u8 = (uint8_t)Q_clipf(
            face_alpha * 255.0f + 0.5f, 0.0f, 255.0f);
        const bool alpha = (ent->flags & RF_TRANSLUCENT) || alpha_u8 < 255 ||
                           (surf_flags & SURF_TRANS_MASK) ||
                           ((surf_flags & SURF_ALPHATEST) == 0 &&
                            texture_transparent);

        if (instance_index == UINT32_MAX) {
            if (vk_entity.bmodel_instance_count == UINT32_MAX ||
                !VK_Entity_EnsureBmodelInstanceCapacity(
                    vk_entity.bmodel_instance_count + 1)) {
                return false;
            }
            instance_index = vk_entity.bmodel_instance_count++;
            vk_bmodel_gpu_instance_t *instance =
                &vk_entity.bmodel_instances[instance_index];
            memset(instance, 0, sizeof(*instance));
            VectorCopy(transform.origin, instance->origin);
            for (uint32_t axis = 0; axis < 3; axis++) {
                VectorCopy(transform.scaled_axis[axis],
                           instance->scaled_axis[axis]);
                VectorCopy(transform.axis[axis], instance->normal_axis[axis]);
                for (uint32_t component = 0; component < 3; component++) {
                    instance->normal_axis[axis][component] *=
                        transform.inv_scale[axis];
                }
            }
            instance->color = entity_color.u32;
            instance->flags = entity_flags;
        }

        uint32_t vertex_flags = gpu_face->flags | entity_flags;
        const uint32_t fast_lit_face_flags = VK_ENTITY_VERTEX_LIGHTMAP |
                                             VK_ENTITY_VERTEX_INTENSITY;
        if (fast_lit_receivers_inactive && !alpha &&
            (gpu_face->flags & VK_ENTITY_VERTEX_LIGHTMAP) != 0 &&
            (gpu_face->flags & ~fast_lit_face_flags) == 0) {
            vertex_flags |= VK_ENTITY_VERTEX_GPU_BMODEL_FAST_LIT;
        }
        const uint32_t texture_replace_face_flags =
            VK_ENTITY_VERTEX_TEXTURE_REPLACE | VK_ENTITY_VERTEX_INTENSITY;
        // GLS_TEXTURE_REPLACE returns before inspecting the fullbright,
        // shadow, or dynamic-light flags. Permit precisely that inert
        // lighting subset so the native path remains reachable when a
        // parity fixture (or user) enables r_fullbright; all effect-bearing
        // entity flags still select the shared fragment program.
        const uint32_t texture_replace_ignored_entity_flags =
            VK_ENTITY_VERTEX_FULLBRIGHT | VK_ENTITY_VERTEX_NO_SHADOW |
            VK_ENTITY_VERTEX_NO_DLIGHT |
            VK_ENTITY_VERTEX_NO_ENTITY_MODULATE;
        if ((entity_flags & ~texture_replace_ignored_entity_flags) == 0 &&
            !alpha &&
            (gpu_face->flags & VK_ENTITY_VERTEX_TEXTURE_REPLACE) != 0 &&
            (gpu_face->flags & ~texture_replace_face_flags) == 0) {
            vertex_flags |= VK_ENTITY_VERTEX_GPU_BMODEL_TEXTURE_REPLACE;
        }
        if (!VK_Entity_AppendGpuBmodelBatch(gpu_face->first_vertex,
                                             gpu_face->vertex_count,
                                             instance_index, set, alpha, false,
                                             vertex_flags)) {
            return false;
        }
    }

    return true;
}

static bool VK_Entity_AddBspModel(const entity_t *ent, const refdef_t *fd, const bsp_t *bsp,
                                  bool depth_hack, bool weapon_model)
{
    if (!ent || !bsp || !bsp->models || !bsp->faces ||
        !bsp->vertices || !bsp->edges || !bsp->surfedges) {
        return true;
    }

    int model_index = ~ent->model;
    if (model_index < 1 || model_index >= bsp->nummodels) {
        return true;
    }

    const mmodel_t *model = &bsp->models[model_index];
    if (!model->firstface || model->numfaces <= 0) {
        return true;
    }
    if (!VK_Entity_EnsureBspTextureCache(bsp)) {
        return false;
    }

    // The static path is deliberately conservative: complex bmodel-only
    // effects retain the proven CPU-expanded path below. Ordinary doors,
    // platforms, and translucent inline models now share immutable local
    // geometry and submit one transform/light instance per entity.
    const uint64_t unsupported_gpu_flags =
        RF_SHELL_MASK | RF_OUTLINE | RF_RIMLIGHT | RF_ITEM_COLORIZE;
    if (vk_entity.gpu_bmodel_available &&
        !VK_Debug_ShowTris(VK_DEBUG_SHOWTRIS_WORLD) &&
        !(ent->flags & unsupported_gpu_flags) &&
        VK_Entity_EnsureBspGpuGeometry(bsp)) {
        return VK_Entity_AddGpuBspModel(ent, fd, bsp, model);
    }

    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);

    vec3_t view_local = { 0.0f, 0.0f, 0.0f };
    if (fd) {
        VK_Entity_TransformPointInverseWithTransform(&transform, fd->vieworg, view_local);
    }
    bool no_static_lighting = false;
    color_t entity_color = VK_Entity_LitColor(ent, false, false,
                                              fd ? fd->time : 0.0f,
                                              fd ? fd->rdflags : 0,
                                              &no_static_lighting);

    for (int i = 0; i < model->numfaces; i++) {
        const mface_t *face = &model->firstface[i];
        if (!face || !face->texinfo || !face->firstsurfedge || face->numsurfedges < 3 || !face->plane) {
            continue;
        }

        surfflags_t surf_flags = face->texinfo->c.flags;
        if (surf_flags & (SURF_NODRAW | SURF_SKY)) {
            continue;
        }

        if (fd) {
            const float dot = PlaneDiffFast(view_local, face->plane);
            if ((face->drawflags & DSURF_PLANEBACK) ? (dot > 0.01f) : (dot < -0.01f)) {
                continue;
            }
        }

        float inv_tex_w = 1.0f;
        float inv_tex_h = 1.0f;
        VkDescriptorSet set = VK_NULL_HANDLE;
        bool texture_transparent = false;
        bool has_glowmap = false;
        if (!VK_Entity_GetBspFaceTexture(bsp, face, &set,
                                         &inv_tex_w, &inv_tex_h,
                                         &texture_transparent,
                                         &has_glowmap)) {
            continue;
        }
        if (!set) {
            continue;
        }

        float alpha_f = VK_Entity_Alpha(ent);
        if (surf_flags & SURF_TRANS33) {
            alpha_f = min(alpha_f, 0.33f);
        } else if (surf_flags & SURF_TRANS66) {
            alpha_f = min(alpha_f, 0.66f);
        }
        uint8_t alpha_u8 = (uint8_t)Q_clipf(alpha_f * 255.0f + 0.5f, 0.0f, 255.0f);

        bool alpha = (ent->flags & RF_TRANSLUCENT) || (alpha_u8 < 255) ||
                     (surf_flags & SURF_TRANS_MASK) ||
                     ((surf_flags & SURF_ALPHATEST) == 0 && texture_transparent);

        vec3_t face_normal = { 0.0f, 0.0f, 1.0f };
        VectorCopy(face->plane->normal, face_normal);
        if (face->drawflags & DSURF_PLANEBACK) {
            VectorInverse(face_normal);
        }
        vec3_t world_normal;
        VK_Entity_TransformNormalWithTransform(&transform, face_normal, world_normal);

        const msurfedge_t *surfedges = face->firstsurfedge;
        if (surfedges[0].edge >= (uint32_t)bsp->numedges) {
            continue;
        }
        uint32_t i0 = VK_Entity_SurfEdgeVertexIndex(bsp, &surfedges[0]);
        if (i0 >= (uint32_t)bsp->numvertices) {
            continue;
        }
        const mvertex_t *v0 = &bsp->vertices[i0];
        vec2_t first_lm_uv = { 0.0f, 0.0f };
        bool face_lightmapped =
            face->lightmap != NULL &&
            VK_World_GetFaceLightmapUV(face, v0->point, first_lm_uv);
        uint32_t flags = VK_Entity_LightingFlags(ent, false);
        if (no_static_lighting && !face_lightmapped) {
            flags |= VK_ENTITY_VERTEX_NO_ENTITY_MODULATE;
        }
        if (face_lightmapped) {
            flags |= VK_ENTITY_VERTEX_LIGHTMAP;
            if (has_glowmap) {
                flags |= VK_ENTITY_VERTEX_GLOWMAP;
            }
        }
        if (surf_flags & SURF_ALPHATEST) {
            flags |= VK_ENTITY_VERTEX_ALPHATEST;
        }
        if (VK_World_SurfaceUsesIntensity(bsp, face)) {
            flags |= VK_ENTITY_VERTEX_INTENSITY;
        }
        if (!face_lightmapped && !(surf_flags & SURF_TRANS_MASK)) {
            flags |= VK_ENTITY_VERTEX_TEXTURE_REPLACE;
        }

        for (int j = 1; j < face->numsurfedges - 1; j++) {
            if (surfedges[j].edge >= (uint32_t)bsp->numedges ||
                surfedges[j + 1].edge >= (uint32_t)bsp->numedges) {
                continue;
            }

            uint32_t i1 = VK_Entity_SurfEdgeVertexIndex(bsp, &surfedges[j]);
            uint32_t i2 = VK_Entity_SurfEdgeVertexIndex(bsp, &surfedges[j + 1]);
            if (i1 >= (uint32_t)bsp->numvertices || i2 >= (uint32_t)bsp->numvertices) {
                continue;
            }

            const mvertex_t *verts[3] = {
                v0,
                &bsp->vertices[i1],
                &bsp->vertices[i2],
            };

            vk_vertex_t tri[3] = { 0 };
            for (int k = 0; k < 3; k++) {
                const vec3_t local = {
                    verts[k]->point[0],
                    verts[k]->point[1],
                    verts[k]->point[2],
                };
                VK_Entity_TransformPointWithTransform(&transform, local, tri[k].pos);

                float u = DotProduct(local, face->texinfo->axis[0]) + face->texinfo->offset[0];
                float v = DotProduct(local, face->texinfo->axis[1]) + face->texinfo->offset[1];
                tri[k].uv[0] = u * inv_tex_w;
                tri[k].uv[1] = v * inv_tex_h;
                if (face_lightmapped) {
                    vec2_t lm_uv;
                    VK_World_GetFaceLightmapUV(face, local, lm_uv);
                    tri[k].lm_uv[0] = lm_uv[0];
                    tri[k].lm_uv[1] = lm_uv[1];
                }

                color_t color = face_lightmapped ? COLOR_RGBA(255, 255, 255, alpha_u8) : entity_color;
                color.a = alpha_u8;
                tri[k].color = color.u32;
                tri[k].flags = flags;
                VectorCopy(world_normal, tri[k].normal);
            }

            if (!VK_Entity_EmitTriBlend(&tri[0], &tri[1], &tri[2], set,
                                        alpha, false, depth_hack,
                                        weapon_model)) {
                return false;
            }
        }
    }

    return true;
}

static void VK_Entity_DestroyParticleTexture(void)
{
    if (vk_entity.particle_image) {
        VK_UI_UnregisterImage(vk_entity.particle_image);
        vk_entity.particle_image = 0;
    }
    vk_entity.particle_set = vk_entity.white_set;
}

static void VK_Entity_InitParticleTexture(void)
{
    byte pixels[VK_ENTITY_PARTICLE_TEX_SIZE * VK_ENTITY_PARTICLE_TEX_SIZE * 4];
    const int shape = vk_particle_shape
        ? Cvar_ClampInteger(vk_particle_shape, 0, 2) : 0;
    int flags = IF_PERMANENT | IF_TRANSPARENT;

    if (shape == 0 || shape == 2) {
        byte *dst = pixels;
        for (int y = 0; y < VK_ENTITY_PARTICLE_TEX_SIZE; y++) {
            for (int x = 0; x < VK_ENTITY_PARTICLE_TEX_SIZE; x++) {
                float fx = (float)x - (float)VK_ENTITY_PARTICLE_TEX_SIZE * 0.5f + 0.5f;
                float fy = (float)y - (float)VK_ENTITY_PARTICLE_TEX_SIZE * 0.5f + 0.5f;
                float alpha = 1.0f - sqrtf(fx * fx + fy * fy) /
                    (((float)(VK_ENTITY_PARTICLE_TEX_SIZE - shape) * 0.5f) - 0.5f);
                alpha *= 1 << shape;

                dst[0] = 255;
                dst[1] = 255;
                dst[2] = 255;
                // Match GL_InitParticleTexture's legacy byte truncation. The
                // additive mode compounds even one alpha level across its
                // deterministic particle field.
                dst[3] = (byte)(255.0f * Q_clipf(alpha, 0.0f,
                                                   1.0f - shape * 0.2f));
                dst += 4;
            }
        }
    } else {
        flags |= IF_NEAREST;
        memset(pixels, 0, sizeof(pixels));
        for (int y = 3; y <= 12; y++) {
            for (int x = 3; x <= 12; x++) {
                byte *dst = pixels + (y * VK_ENTITY_PARTICLE_TEX_SIZE + x) * 4;
                dst[0] = 255;
                dst[1] = 255;
                dst[2] = 255;
                dst[3] = (byte)(255.0f * 0.6f);
            }
        }
    }

    vk_entity.particle_image = VK_UI_RegisterRawImage("**vk_entity_particle**",
                                                       VK_ENTITY_PARTICLE_TEX_SIZE,
                                                       VK_ENTITY_PARTICLE_TEX_SIZE,
                                                       pixels, IT_SPRITE, flags);
    if (!vk_entity.particle_image) {
        vk_entity.particle_set = vk_entity.white_set;
        return;
    }

    vk_entity.particle_set = VK_UI_GetDescriptorSetForImage(vk_entity.particle_image);
    if (!vk_entity.particle_set) {
        VK_UI_UnregisterImage(vk_entity.particle_image);
        vk_entity.particle_image = 0;
        vk_entity.particle_set = vk_entity.white_set;
    }
}

static void VK_Entity_ParticleShapeChanged(cvar_t *self)
{
    if (!self || !vk_entity.initialized || !vk_entity.ctx ||
        !vk_entity.ctx->device) {
        return;
    }

    // Image descriptors can be referenced by either in-flight frame slot.
    // Shape changes are infrequent user configuration changes, so retire that
    // work before replacing the native particle texture.
    vkDeviceWaitIdle(vk_entity.ctx->device);
    VK_Entity_DestroyParticleTexture();
    VK_Entity_InitParticleTexture();
}

static void VK_Entity_InitBeamTexture(void)
{
    byte pixels[VK_ENTITY_BEAM_TEX_SIZE * VK_ENTITY_BEAM_TEX_SIZE * 4];
    byte *dst = pixels;

    for (int y = 0; y < VK_ENTITY_BEAM_TEX_SIZE; y++) {
        for (int x = 0; x < VK_ENTITY_BEAM_TEX_SIZE; x++) {
            float alpha = (float)abs(x - VK_ENTITY_BEAM_TEX_SIZE / 2) - 0.5f;
            alpha = 1.0f - alpha / (VK_ENTITY_BEAM_TEX_SIZE / 2.0f - 2.5f);

            dst[0] = 255;
            dst[1] = 255;
            dst[2] = 255;
            dst[3] = (byte)(255.0f * Q_clipf(alpha, 0.0f, 1.0f));
            dst += 4;
        }
    }

    vk_entity.beam_image = VK_UI_RegisterRawImage("**vk_entity_beam**",
                                                   VK_ENTITY_BEAM_TEX_SIZE,
                                                   VK_ENTITY_BEAM_TEX_SIZE,
                                                   pixels,
                                                   IT_SPRITE,
                                                   IF_PERMANENT | IF_TRANSPARENT);
    if (!vk_entity.beam_image) {
        vk_entity.beam_set = vk_entity.white_set;
        return;
    }

    vk_entity.beam_set = VK_UI_GetDescriptorSetForImage(vk_entity.beam_image);
    if (!vk_entity.beam_set) {
        VK_UI_UnregisterImage(vk_entity.beam_image);
        vk_entity.beam_image = 0;
        vk_entity.beam_set = vk_entity.white_set;
    }
}

static bool VK_Entity_AddParticles(const refdef_t *fd, const vec3_t view_axis[3])
{
    if (!fd || !fd->particles || fd->num_particles <= 0) {
        return true;
    }

    VkDescriptorSet set = vk_entity.particle_set ? vk_entity.particle_set : vk_entity.white_set;
    if (!set) {
        return true;
    }

    extern uint32_t d_8to24table[256];
    float partscale = (vk_partscale ? max(vk_partscale->value, 0.0f) : 2.0f);
    bool additive = vk_particle_style && vk_particle_style->integer;
    const bool gpu_particles = vk_particle_gpu_instancing &&
        vk_particle_gpu_instancing->integer && vk_entity.gpu_particle_available;

    for (int i = 0; i < fd->num_particles; i++) {
        const particle_t *p = &fd->particles[i];
        if (!p || p->alpha <= 0.0f || p->scale <= 0.0f) {
            continue;
        }

        vec3_t transformed;
        VectorSubtract(p->origin, fd->vieworg, transformed);
        float dist = DotProduct(transformed, view_axis[0]);

        float scale = 1.0f;
        if (dist > 20.0f) {
            scale += dist * 0.004f;
        }
        scale *= partscale * p->scale;
        color_t color;
        if (p->color == -1) {
            color = p->rgba;
        } else {
            color.u32 = d_8to24table[p->color & 0xff];
        }
        if (p->brightness > 0.0f && p->brightness != 1.0f) {
            color.r = (uint8_t)min(255, (int)(color.r * p->brightness + 0.5f));
            color.g = (uint8_t)min(255, (int)(color.g * p->brightness + 0.5f));
            color.b = (uint8_t)min(255, (int)(color.b * p->brightness + 0.5f));
        }
        color.a = (uint8_t)Q_clipf((float)color.a * Q_clipf(p->alpha, 0.0f, 1.0f),
                                   0.0f, 255.0f);
        if (!color.a) {
            continue;
        }

        if (gpu_particles) {
            if (!VK_Entity_EnsureParticleInstanceCapacity(
                    vk_entity.particle_instance_count + 1)) {
                return false;
            }
            const uint32_t instance_index = vk_entity.particle_instance_count++;
            vk_entity.particle_instances[instance_index] =
                (vk_particle_gpu_instance_t) {
                    .origin_scale = {
                        p->origin[0], p->origin[1], p->origin[2], scale,
                    },
                    .color = color.u32,
                };
            if (!VK_Entity_AppendGpuParticleBatch(instance_index, set,
                                                  additive)) {
                return false;
            }
            continue;
        }

        const float scale2 = scale * VK_ENTITY_PARTICLE_SCALE;
        uint32_t flags = VK_ENTITY_VERTEX_FULLBRIGHT |
                         VK_ENTITY_VERTEX_NO_SHADOW |
                         VK_ENTITY_VERTEX_NO_DLIGHT;
        vk_vertex_t v0 = { .uv = { 0.0f, 0.0f }, .color = color.u32, .flags = flags, .normal = { 0, 0, 1 } };
        vk_vertex_t v1 = { .uv = { 0.0f, VK_ENTITY_PARTICLE_SIZE }, .color = color.u32, .flags = flags, .normal = { 0, 0, 1 } };
        vk_vertex_t v2 = { .uv = { VK_ENTITY_PARTICLE_SIZE, 0.0f }, .color = color.u32, .flags = flags, .normal = { 0, 0, 1 } };

        VectorMA(p->origin, scale2, view_axis[1], v0.pos);
        VectorMA(v0.pos, -scale2, view_axis[2], v0.pos);
        VectorMA(v0.pos, scale, view_axis[2], v1.pos);
        VectorMA(v0.pos, -scale, view_axis[1], v2.pos);

        if (!VK_Entity_EmitTriBlend(&v0, &v1, &v2, set, true, additive,
                                    false, false)) {
            return false;
        }
    }

    return true;
}

static qhandle_t VK_Entity_SelectMD2Skin(const entity_t *ent, const vk_md2_t *md2)
{
    if (ent->flags & RF_CUSTOMSKIN) {
        return ent->skin;
    }
    if (ent->skin) {
        return ent->skin;
    }
    if (!md2->skins || md2->num_skins == 0) {
        return 0;
    }
    int skinnum = ent->skinnum;
    if (skinnum < 0 || skinnum >= (int)md2->num_skins || !md2->skins[skinnum]) {
        skinnum = 0;
    }
    return md2->skins[skinnum];
}

static qhandle_t VK_Entity_SelectMD5Skin(const entity_t *ent, const vk_md5_t *md5)
{
    if (ent->flags & RF_CUSTOMSKIN) {
        return ent->skin;
    }
    if (ent->skin) {
        return ent->skin;
    }
    if (md5 && md5->skins && md5->num_skins) {
        int skinnum = ent->skinnum;
        if (skinnum < 0 || skinnum >= (int)md5->num_skins || !md5->skins[skinnum]) {
            skinnum = 0;
        }
        return md5->skins[skinnum];
    }
    return 0;
}

static bool VK_Entity_ShouldUseGpuMD2(const entity_t *ent,
                                      const vk_model_t *model)
{
    return ent && model && vk_md2_gpu_lerp && vk_md2_gpu_lerp->integer &&
           vk_entity.gpu_md2_available && model->md2.gpu_ready &&
           !VK_Debug_ShowTris(VK_DEBUG_SHOWTRIS_MESH) &&
           !(ent->flags & (RF_ITEM_COLORIZE | RF_OUTLINE));
}

static bool VK_Entity_AddGpuMD2(const entity_t *ent, const refdef_t *fd,
                                const vk_model_t *model, bool depth_hack,
                                bool weapon_model)
{
    const vk_md2_t *md2 = &model->md2;
    if (!md2->gpu_ready || !md2->num_frames || !md2->num_vertices ||
        !md2->num_indices) {
        return false;
    }

    uint32_t frame = 0;
    uint32_t oldframe = 0;
    float backlerp = 0.0f;
    float frontlerp = 1.0f;
    if (!VK_Entity_ResolveAnimationFrames(fd, md2->num_frames, ent->frame,
                                          ent->oldframe, ent->backlerp,
                                          &frame, &oldframe, &backlerp,
                                          &frontlerp)) {
        return true;
    }

    const bool shell = (ent->flags & RF_SHELL_MASK) != 0;
    const float shell_scale = shell ? VK_Entity_ShellScale(ent) : 0.0f;
    const qhandle_t skin = VK_Entity_SelectMD2Skin(ent, md2);
    VkDescriptorSet set = shell ? vk_entity.white_set : VK_Entity_SetForImage(skin);
    if (!set) {
        return true;
    }
    const bool has_glowmap = !shell && VK_UI_HasGlowmap(skin);
    const bool alpha = shell || (ent->flags & RF_TRANSLUCENT) ||
                       VK_Entity_Alpha(ent) < 1.0f;
    const bool additive = alpha && (ent->flags & (RF_RIMLIGHT | RF_BRIGHTSKIN));
    bool no_static_lighting = false;
    const color_t color = shell ? VK_Entity_ShellColor(ent)
                                : VK_Entity_LitColor(ent, false, false,
                                                     fd ? fd->time : 0.0f,
                                                     fd ? fd->rdflags : 0,
                                                     &no_static_lighting);
    uint32_t flags = (shell
        ? (VK_ENTITY_VERTEX_FULLBRIGHT | VK_ENTITY_VERTEX_NO_SHADOW |
           VK_ENTITY_VERTEX_NO_DLIGHT | VK_ENTITY_VERTEX_BLOOM_SHELL)
         : VK_Entity_LightingFlags(ent, false)) | VK_ENTITY_VERTEX_INTENSITY;
    if (no_static_lighting) {
        flags |= VK_ENTITY_VERTEX_NO_ENTITY_MODULATE;
    }
    if (shell && depth_hack) {
        flags |= VK_ENTITY_VERTEX_BLOOM_DEPTHHACK;
    }
    if (!shell && (ent->flags & RF_RIMLIGHT)) {
        flags |= VK_ENTITY_VERTEX_RIMLIGHT | VK_ENTITY_VERTEX_BLOOM_RIM;
    }
    if (has_glowmap) {
        flags |= VK_ENTITY_VERTEX_GLOWMAP;
    }
    vk_entity.current_cel_line_width =
        VK_Entity_CelShadingLineWidth(ent, fd);
    if (vk_entity.current_cel_line_width > 0.0f) {
        flags |= VK_ENTITY_VERTEX_CELSHADING;
    }

    if (!VK_Entity_EnsureMd2InstanceCapacity(vk_entity.md2_instance_count + 1)) {
        return false;
    }
    const uint32_t instance_index = vk_entity.md2_instance_count;
    vk_md2_gpu_instance_t *instance = &vk_entity.md2_instances[instance_index];
    memset(instance, 0, sizeof(*instance));

    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);
    VectorCopy(transform.origin, instance->origin_frontlerp);
    instance->origin_frontlerp[3] = frontlerp;
    for (int axis = 0; axis < 3; axis++) {
        VectorCopy(transform.scaled_axis[axis], instance->scaled_axis[axis]);
        VectorScale(transform.axis[axis], transform.inv_scale[axis],
                    instance->normal_axis[axis]);
    }
    instance->shell[0] = shell_scale;
    instance->color = color.u32;
    instance->flags = flags;

    vk_entity.md2_instance_count++;
    if (!VK_Entity_AppendGpuMd2Batch(md2, frame, oldframe, instance_index,
                                     set, alpha, additive, depth_hack,
                                     weapon_model, flags)) {
        return false;
    }
    return true;
}

static bool VK_Entity_AddMD2(const entity_t *ent, const refdef_t *fd, const vk_model_t *model,
                             bool depth_hack, bool weapon_model)
{
    const vk_md2_t *md2 = &model->md2;
    if (!md2->positions || !md2->normals || !md2->uv || !md2->indices || !md2->num_frames) {
        return true;
    }

    uint32_t frame = 0;
    uint32_t oldframe = 0;
    float backlerp = 0.0f;
    float frontlerp = 1.0f;
    if (!VK_Entity_ResolveAnimationFrames(fd, md2->num_frames, ent->frame, ent->oldframe,
                                          ent->backlerp,
                                          &frame, &oldframe, &backlerp, &frontlerp)) {
        return true;
    }

    bool shell = (ent->flags & RF_SHELL_MASK) != 0;
    float shell_scale = shell ? VK_Entity_ShellScale(ent) : 0.0f;
    qhandle_t skin = VK_Entity_SelectMD2Skin(ent, md2);
    VkDescriptorSet set = shell ? vk_entity.white_set : VK_Entity_SetForImage(skin);
    if (!set) {
        return true;
    }
    const bool has_glowmap = !shell && VK_UI_HasGlowmap(skin);
    bool alpha = shell || (ent->flags & RF_TRANSLUCENT) ||
                 VK_Entity_Alpha(ent) < 1.0f;
    bool no_static_lighting = false;
    color_t color = shell ? VK_Entity_ShellColor(ent)
                          : VK_Entity_LitColor(ent, false, false,
                                               fd ? fd->time : 0.0f,
                                               fd ? fd->rdflags : 0,
                                               &no_static_lighting);
    uint32_t flags = (shell
        ? (VK_ENTITY_VERTEX_FULLBRIGHT | VK_ENTITY_VERTEX_NO_SHADOW |
           VK_ENTITY_VERTEX_NO_DLIGHT | VK_ENTITY_VERTEX_BLOOM_SHELL)
         : VK_Entity_LightingFlags(ent, false)) | VK_ENTITY_VERTEX_INTENSITY;
    if (no_static_lighting) {
        flags |= VK_ENTITY_VERTEX_NO_ENTITY_MODULATE;
    }
    if (shell && depth_hack) {
        flags |= VK_ENTITY_VERTEX_BLOOM_DEPTHHACK;
    }
    if (!shell && (ent->flags & RF_RIMLIGHT)) {
        flags |= VK_ENTITY_VERTEX_RIMLIGHT | VK_ENTITY_VERTEX_BLOOM_RIM;
    }
    if (!shell && (ent->flags & RF_ITEM_COLORIZE)) {
        flags |= VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE;
    }
    if (has_glowmap) {
        flags |= VK_ENTITY_VERTEX_GLOWMAP;
    }
    vk_entity.current_cel_line_width =
        VK_Entity_CelShadingLineWidth(ent, fd);
    if (vk_entity.current_cel_line_width > 0.0f) {
        flags |= VK_ENTITY_VERTEX_CELSHADING;
    }
    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);
    bool additive = alpha && (ent->flags & (RF_RIMLIGHT | RF_BRIGHTSKIN));

    // Most MD2 instances have one material pass. Preserve the expanded path
    // for colorize/outline layering, but otherwise upload one transformed
    // vertex per source vertex and submit the native index list directly.
    if (!(ent->flags & (RF_ITEM_COLORIZE | RF_OUTLINE)) &&
        md2->num_vertices && md2->num_indices) {
        if (md2->num_vertices > UINT32_MAX - vk_entity.vertex_count ||
            md2->num_indices > UINT32_MAX - vk_entity.index_count) {
            Com_SetLastError("Vulkan entity: indexed MD2 stream count overflow");
            return false;
        }
        if (!VK_Entity_EnsureVertexCapacity(vk_entity.vertex_count + md2->num_vertices) ||
            !VK_Entity_EnsureIndexCapacity(vk_entity.index_count + md2->num_indices)) {
            return false;
        }

        const uint32_t first_vertex = vk_entity.vertex_count;
        const uint32_t first_index = vk_entity.index_count;
        for (uint32_t idx = 0; idx < md2->num_vertices; idx++) {
            size_t frame_offset = 0;
            size_t oldframe_offset = 0;
            size_t uv_offset = 0;
            if (!VK_Entity_MD2VectorOffset(md2, frame, idx, &frame_offset) ||
                !VK_Entity_MD2VectorOffset(md2, oldframe, idx, &oldframe_offset) ||
                !VK_Entity_ArrayBytes((size_t)idx, 2, &uv_offset, "MD2 UV offset")) {
                return false;
            }

            const float *p0 = &md2->positions[frame_offset];
            const float *p1 = &md2->positions[oldframe_offset];
            const float *n0 = &md2->normals[frame_offset];
            const float *n1 = &md2->normals[oldframe_offset];
            vec3_t local = {
                p0[0] * frontlerp + p1[0] * backlerp,
                p0[1] * frontlerp + p1[1] * backlerp,
                p0[2] * frontlerp + p1[2] * backlerp,
            };
            vec3_t local_normal = {
                n0[0] * frontlerp + n1[0] * backlerp,
                n0[1] * frontlerp + n1[1] * backlerp,
                n0[2] * frontlerp + n1[2] * backlerp,
            };
            if (shell) {
                VectorMA(local, shell_scale, local_normal, local);
            }

            vk_vertex_t *vertex = &vk_entity.vertices[vk_entity.vertex_count++];
            memset(vertex, 0, sizeof(*vertex));
            VK_Entity_TransformPointWithTransform(&transform, local, vertex->pos);
            VK_Entity_TransformNormalWithTransform(&transform, local_normal, vertex->normal);
            vertex->uv[0] = md2->uv[uv_offset + 0];
            vertex->uv[1] = md2->uv[uv_offset + 1];
            vertex->color = color.u32;
            vertex->flags = flags;
        }

        for (uint32_t i = 0; i + 2 < md2->num_indices; i += 3) {
            const uint32_t i0 = md2->indices[i + 0];
            const uint32_t i1 = md2->indices[i + 1];
            const uint32_t i2 = md2->indices[i + 2];
            if (i0 >= md2->num_vertices || i1 >= md2->num_vertices ||
                i2 >= md2->num_vertices) {
                continue;
            }
            vk_entity.indices[vk_entity.index_count++] = i0;
            vk_entity.indices[vk_entity.index_count++] = i1;
            vk_entity.indices[vk_entity.index_count++] = i2;
        }

        const uint32_t index_count = vk_entity.index_count - first_index;
        if (!index_count) {
            vk_entity.vertex_count = first_vertex;
            return true;
        }
        return VK_Entity_AppendIndexedBatch(first_vertex, md2->num_vertices,
                                            first_index, index_count, set,
                                            alpha, additive, depth_hack,
                                            weapon_model);
    }

    uint32_t model_first_vertex = vk_entity.vertex_count;

    for (uint32_t i = 0; i + 2 < md2->num_indices; i += 3) {
        vk_vertex_t tri[3] = { 0 };
        bool valid_tri = true;
        for (uint32_t j = 0; j < 3; j++) {
            uint32_t idx = md2->indices[i + j];
            if (idx >= md2->num_vertices) {
                valid_tri = false;
                break;
            }
            size_t frame_offset = 0;
            size_t oldframe_offset = 0;
            size_t uv_offset = 0;
            if (!VK_Entity_MD2VectorOffset(md2, frame, idx, &frame_offset) ||
                !VK_Entity_MD2VectorOffset(md2, oldframe, idx, &oldframe_offset) ||
                !VK_Entity_ArrayBytes((size_t)idx, 2, &uv_offset, "MD2 UV offset")) {
                valid_tri = false;
                break;
            }

            const float *p0 = &md2->positions[frame_offset];
            const float *p1 = &md2->positions[oldframe_offset];
            const float *n0 = &md2->normals[frame_offset];
            const float *n1 = &md2->normals[oldframe_offset];
            vec3_t local = {
                p0[0] * frontlerp + p1[0] * backlerp,
                p0[1] * frontlerp + p1[1] * backlerp,
                p0[2] * frontlerp + p1[2] * backlerp,
            };
            vec3_t local_normal = {
                n0[0] * frontlerp + n1[0] * backlerp,
                n0[1] * frontlerp + n1[1] * backlerp,
                n0[2] * frontlerp + n1[2] * backlerp,
            };
            if (shell) {
                VectorMA(local, shell_scale, local_normal, local);
            }
            VK_Entity_TransformPointWithTransform(&transform, local, tri[j].pos);
            VK_Entity_TransformNormalWithTransform(&transform, local_normal, tri[j].normal);
            tri[j].uv[0] = md2->uv[uv_offset + 0];
            tri[j].uv[1] = md2->uv[uv_offset + 1];
            tri[j].color = color.u32;
            tri[j].flags = flags;
        }

        if (!valid_tri) {
            continue;
        }

        if (!VK_Entity_EmitTriBlend(&tri[0], &tri[1], &tri[2], set,
                                    alpha, additive, depth_hack,
                                    weapon_model)) {
            return false;
        }
    }

    uint32_t model_vertex_count = vk_entity.vertex_count - model_first_vertex;

    if (!shell && (ent->flags & RF_ITEM_COLORIZE) &&
        !VK_Entity_EmitItemColorizeOverlay(
            model_first_vertex, model_vertex_count,
            set, ent, depth_hack, weapon_model)) {
        return false;
    }
    if ((ent->flags & RF_OUTLINE) &&
        !VK_Entity_EmitOutline(
            model_first_vertex, model_vertex_count, ent,
            VK_Entity_OutlineScale(ent, fd, md2, frame, oldframe,
                                   backlerp, frontlerp),
            depth_hack, weapon_model)) {
        return false;
    }

    return true;
}

static bool VK_Entity_EmitShadowMD2(const entity_t *ent, const refdef_t *fd,
                                    const vk_model_t *model,
                                    vk_entity_shadow_emit_triangle_fn emit,
                                    void *userdata)
{
    const vk_md2_t *md2 = &model->md2;
    if (!md2->positions || !md2->indices || !md2->num_frames) {
        return true;
    }

    uint32_t frame = 0;
    uint32_t oldframe = 0;
    float backlerp = 0.0f;
    float frontlerp = 1.0f;
    if (!VK_Entity_ResolveAnimationFrames(fd, md2->num_frames, ent->frame, ent->oldframe,
                                          ent->backlerp,
                                          &frame, &oldframe, &backlerp, &frontlerp)) {
        return true;
    }

    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);

    for (uint32_t i = 0; i + 2 < md2->num_indices; i += 3) {
        vec3_t tri[3];
        bool valid_tri = true;
        for (uint32_t j = 0; j < 3; j++) {
            uint32_t idx = md2->indices[i + j];
            if (idx >= md2->num_vertices) {
                valid_tri = false;
                break;
            }
            size_t frame_offset = 0;
            size_t oldframe_offset = 0;
            if (!VK_Entity_MD2VectorOffset(md2, frame, idx, &frame_offset) ||
                !VK_Entity_MD2VectorOffset(md2, oldframe, idx, &oldframe_offset)) {
                valid_tri = false;
                break;
            }

            const float *p0 = &md2->positions[frame_offset];
            const float *p1 = &md2->positions[oldframe_offset];
            vec3_t local = {
                p0[0] * frontlerp + p1[0] * backlerp,
                p0[1] * frontlerp + p1[1] * backlerp,
                p0[2] * frontlerp + p1[2] * backlerp,
            };
            VK_Entity_TransformPointWithTransform(&transform, local, tri[j]);
        }

        if (valid_tri && !emit(tri[0], tri[1], tri[2], NULL, NULL,
                               userdata)) {
            return false;
        }
    }

    return true;
}

#if USE_MD5
static bool VK_Entity_EmitShadowMD5(const entity_t *ent, const refdef_t *fd,
                                    const vk_model_t *model,
                                    vk_entity_shadow_emit_triangle_fn emit,
                                    void *userdata)
{
    if (!ent || !model || !model->md5.loaded) {
        return true;
    }

    const vk_md5_t *md5 = &model->md5;
    if (!md5->num_meshes || !md5->num_joints || !md5->num_frames ||
        !md5->meshes || !md5->skeleton_frames) {
        return true;
    }

    uint32_t frame = 0;
    uint32_t oldframe = 0;
    float backlerp = 0.0f;
    float frontlerp = 1.0f;
    uint32_t frame_count = model->md2.num_frames ? model->md2.num_frames : md5->num_frames;
    if (!VK_Entity_ResolveAnimationFrames(fd, frame_count, ent->frame, ent->oldframe,
                                          ent->backlerp,
                                          &frame, &oldframe, &backlerp, &frontlerp)) {
        return true;
    }

    const vk_md5_joint_t *skeleton = VK_Entity_LerpMD5Skeleton(md5, oldframe, frame, backlerp, frontlerp);
    if (!skeleton) {
        return false;
    }

    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);

    for (uint32_t i = 0; i < md5->num_meshes; i++) {
        const vk_md5_mesh_t *mesh = &md5->meshes[i];
        if (!mesh->num_indices || !mesh->vertices || !mesh->indices ||
            !mesh->weights || !mesh->jointnums) {
            continue;
        }
        if (!VK_Entity_EnsureTempMD5Vertices(mesh->num_verts)) {
            return false;
        }

        for (uint32_t idx = 0; idx < mesh->num_verts; idx++) {
            vec3_t local_pos;
            VK_Entity_MD5Vertex(mesh, skeleton, md5->num_joints, idx,
                                local_pos, NULL);
            VK_Entity_TransformPointWithTransform(
                &transform, local_pos, vk_entity.temp_md5_vertices[idx].pos);
        }

        for (uint32_t tri_idx = 0; tri_idx + 2 < mesh->num_indices; tri_idx += 3) {
            vec3_t tri[3];
            bool valid_tri = true;
            for (uint32_t j = 0; j < 3; j++) {
                uint32_t idx = mesh->indices[tri_idx + j];
                if (idx >= mesh->num_verts) {
                    valid_tri = false;
                    break;
                }

                VectorCopy(vk_entity.temp_md5_vertices[idx].pos, tri[j]);
            }

            if (valid_tri && !emit(tri[0], tri[1], tri[2], NULL, NULL,
                                   userdata)) {
                return false;
            }
        }
    }

    return true;
}
#endif

static bool VK_Entity_EmitShadowBspModel(const entity_t *ent, const bsp_t *bsp,
                                         vk_entity_shadow_emit_triangle_fn emit,
                                         void *userdata)
{
    if (!ent || !bsp || !bsp->models || !bsp->faces ||
        !bsp->vertices || !bsp->edges || !bsp->surfedges) {
        return true;
    }

    int model_index = ~ent->model;
    if (model_index < 1 || model_index >= bsp->nummodels) {
        return true;
    }

    const mmodel_t *model = &bsp->models[model_index];
    if (!model->firstface || model->numfaces <= 0) {
        return true;
    }

    vk_entity_transform_t transform;
    VK_Entity_BuildTransform(ent, &transform);

    for (int i = 0; i < model->numfaces; i++) {
        const mface_t *face = &model->firstface[i];
        if (!face || !face->firstsurfedge || face->numsurfedges < 3) {
            continue;
        }

        surfflags_t surf_flags = face->texinfo ? face->texinfo->c.flags : 0;
        if ((face->drawflags | surf_flags) & (SURF_NODRAW | SURF_SKY | SURF_TRANS_MASK)) {
            continue;
        }

        const msurfedge_t *surfedges = face->firstsurfedge;
        if (surfedges[0].edge >= (uint32_t)bsp->numedges) {
            continue;
        }
        uint32_t i0 = VK_Entity_SurfEdgeVertexIndex(bsp, &surfedges[0]);
        if (i0 >= (uint32_t)bsp->numvertices) {
            continue;
        }
        const mvertex_t *v0 = &bsp->vertices[i0];

        for (int j = 1; j < face->numsurfedges - 1; j++) {
            if (surfedges[j].edge >= (uint32_t)bsp->numedges ||
                surfedges[j + 1].edge >= (uint32_t)bsp->numedges) {
                continue;
            }

            uint32_t i1 = VK_Entity_SurfEdgeVertexIndex(bsp, &surfedges[j]);
            uint32_t i2 = VK_Entity_SurfEdgeVertexIndex(bsp, &surfedges[j + 1]);
            if (i1 >= (uint32_t)bsp->numvertices || i2 >= (uint32_t)bsp->numvertices) {
                continue;
            }

            const mvertex_t *verts[3] = {
                v0,
                &bsp->vertices[i1],
                &bsp->vertices[i2],
            };

            vec3_t tri[3];
            vec3_t texture_points[3];
            for (int k = 0; k < 3; k++) {
                VectorSet(texture_points[k],
                    verts[k]->point[0],
                    verts[k]->point[1],
                    verts[k]->point[2]);
                VK_Entity_TransformPointWithTransform(&transform,
                                                       texture_points[k], tri[k]);
            }

            if (!emit(tri[0], tri[1], tri[2], face, texture_points,
                      userdata)) {
                return false;
            }
        }
    }

    return true;
}

bool VK_Entity_EmitShadowCaster(const entity_t *ent, const refdef_t *fd,
                                const bsp_t *world_bsp,
                                vk_entity_shadow_emit_triangle_fn emit,
                                void *userdata)
{
    if (!vk_entity.initialized || !ent || !emit) {
        return true;
    }

    if (ent->model & BIT(31)) {
        return VK_Entity_EmitShadowBspModel(ent, world_bsp, emit, userdata);
    }

    if (ent->model <= 0 || ent->model > vk_entity.num_models) {
        return true;
    }

    const vk_model_t *model = &vk_entity.models[ent->model - 1];
    if (!model->type) {
        return true;
    }

    if (model->type == VK_MODEL_MD2) {
#if USE_MD5
        if (VK_Entity_ShouldUseMD5(ent, fd, model)) {
            return VK_Entity_EmitShadowMD5(ent, fd, model, emit, userdata);
        }
#endif
        return VK_Entity_EmitShadowMD2(ent, fd, model, emit, userdata);
    }

    return true;
}

static bool VK_Entity_LoadSP2(vk_model_t *model, const byte *raw, size_t len)
{
    if (len < sizeof(dsp2header_t)) {
        return false;
    }

    dsp2header_t hdr;
    memcpy(&hdr, raw, sizeof(hdr));
    hdr.ident = LittleLong(hdr.ident);
    hdr.version = LittleLong(hdr.version);
    hdr.numframes = LittleLong(hdr.numframes);
    if (hdr.ident != SP2_IDENT || hdr.version != SP2_VERSION || hdr.numframes <= 0 || hdr.numframes > SP2_MAX_FRAMES) {
        return false;
    }

    size_t frame_bytes = 0;
    if (!VK_Entity_ArrayBytes((size_t)hdr.numframes, sizeof(dsp2frame_t),
                              &frame_bytes, "SP2 frames") ||
        sizeof(dsp2header_t) > len ||
        frame_bytes > len - sizeof(dsp2header_t)) {
        return false;
    }

    vk_sprite_frame_t *frames = VK_Entity_CallocArray((size_t)hdr.numframes,
                                                      sizeof(*frames),
                                                      "SP2 frames");
    if (!frames) {
        return false;
    }

    const dsp2frame_t *src = (const dsp2frame_t *)(raw + sizeof(dsp2header_t));
    for (int i = 0; i < hdr.numframes; i++) {
        char name[SP2_MAX_FRAMENAME];
        if (Q_memccpy(name, src[i].name, 0, sizeof(name))) {
            FS_NormalizePath(name);
            frames[i].image = VK_UI_RegisterImage(name, IT_SPRITE, IF_NONE);
        }
        frames[i].width = (int)LittleLong(src[i].width);
        frames[i].height = (int)LittleLong(src[i].height);
        frames[i].origin_x = (int)LittleLong(src[i].origin_x);
        frames[i].origin_y = (int)LittleLong(src[i].origin_y);
    }

    model->type = VK_MODEL_SPRITE;
    model->sprite.frames = frames;
    model->sprite.num_frames = (uint32_t)hdr.numframes;
    return true;
}

static bool VK_Entity_LoadMD2(vk_model_t *model, const byte *raw, size_t len)
{
    if (len < sizeof(dmd2header_t)) {
        return false;
    }

    dmd2header_t hdr;
    memcpy(&hdr, raw, sizeof(hdr));
    hdr.ident = LittleLong(hdr.ident);
    hdr.version = LittleLong(hdr.version);
    hdr.skinwidth = LittleLong(hdr.skinwidth);
    hdr.skinheight = LittleLong(hdr.skinheight);
    hdr.framesize = LittleLong(hdr.framesize);
    hdr.num_skins = LittleLong(hdr.num_skins);
    hdr.num_xyz = LittleLong(hdr.num_xyz);
    hdr.num_st = LittleLong(hdr.num_st);
    hdr.num_tris = LittleLong(hdr.num_tris);
    hdr.num_frames = LittleLong(hdr.num_frames);
    hdr.ofs_skins = LittleLong(hdr.ofs_skins);
    hdr.ofs_st = LittleLong(hdr.ofs_st);
    hdr.ofs_tris = LittleLong(hdr.ofs_tris);
    hdr.ofs_frames = LittleLong(hdr.ofs_frames);

    if (hdr.ident != MD2_IDENT || hdr.version != MD2_VERSION || hdr.num_tris <= 0 || hdr.num_frames <= 0 ||
        hdr.num_xyz <= 0 || hdr.num_st <= 0) {
        return false;
    }

    if (hdr.num_tris > MD2_MAX_TRIANGLES || hdr.num_xyz > MD2_MAX_VERTS || hdr.num_frames > MD2_MAX_FRAMES ||
        hdr.num_skins > MD2_MAX_SKINS || hdr.skinwidth < 1 || hdr.skinwidth > MD2_MAX_SKINWIDTH ||
        hdr.skinheight < 1 || hdr.skinheight > MD2_MAX_SKINHEIGHT) {
        return false;
    }

    uint64_t min_frame_size = sizeof(dmd2frame_t) + (uint64_t)(hdr.num_xyz - 1) * sizeof(dmd2trivertx_t);
    if (hdr.framesize < min_frame_size || hdr.framesize > MD2_MAX_FRAMESIZE) {
        return false;
    }

    if (!VK_Entity_BufferRangeAvailable(len, hdr.ofs_tris, hdr.num_tris,
                                        sizeof(dmd2triangle_t), "MD2 triangles") ||
        !VK_Entity_BufferRangeAvailable(len, hdr.ofs_st, hdr.num_st,
                                        sizeof(dmd2stvert_t), "MD2 texture coordinates") ||
        !VK_Entity_BufferRangeAvailable(len, hdr.ofs_frames, hdr.num_frames,
                                        (size_t)hdr.framesize, "MD2 frames") ||
        !VK_Entity_BufferRangeAvailable(len, hdr.ofs_skins, hdr.num_skins,
                                        MD2_MAX_SKINNAME, "MD2 skins")) {
        return false;
    }

    size_t max_index_count = 0;
    if (!VK_Entity_ArrayBytes((size_t)hdr.num_tris, 3, &max_index_count,
                              "MD2 triangle indices") ||
        max_index_count > UINT32_MAX) {
        return false;
    }

    uint32_t max_indices = (uint32_t)max_index_count;
    uint32_t num_indices = 0;
    uint32_t num_vertices = 0;
    uint32_t num_frames = (uint32_t)hdr.num_frames;
    float *positions = NULL;
    float *normals = NULL;
    float *frame_radii = NULL;
    vk_md2_frame_bounds_t *frame_bounds = NULL;
    float *uv = NULL;
    uint16_t *indices = NULL;
    uint16_t *vert_indices = NULL;
    uint16_t *tc_indices = NULL;
    uint16_t *remap = NULL;
    uint16_t *final_indices = NULL;
    qhandle_t *skins = NULL;
    char (*skin_names)[MAX_QPATH] = NULL;

    vert_indices = VK_Entity_CallocArray(max_indices, sizeof(*vert_indices),
                                         "MD2 vertex index scratch");
    tc_indices = VK_Entity_CallocArray(max_indices, sizeof(*tc_indices),
                                       "MD2 texture coordinate scratch");
    remap = VK_Entity_CallocArray(max_indices, sizeof(*remap),
                                  "MD2 remap scratch");
    final_indices = VK_Entity_CallocArray(max_indices, sizeof(*final_indices),
                                          "MD2 final index scratch");
    if (!vert_indices || !tc_indices || !remap || !final_indices) {
        goto fail;
    }

    const dmd2triangle_t *tris = (const dmd2triangle_t *)(raw + hdr.ofs_tris);
    const dmd2stvert_t *st = (const dmd2stvert_t *)(raw + hdr.ofs_st);
    for (uint32_t t = 0; t < (uint32_t)hdr.num_tris; t++) {
        bool good_tri = true;
        for (uint32_t j = 0; j < 3; j++) {
            uint16_t xyz = LittleShort(tris[t].index_xyz[j]);
            uint16_t tc = LittleShort(tris[t].index_st[j]);
            if (xyz >= hdr.num_xyz || tc >= hdr.num_st) {
                good_tri = false;
                break;
            }
            vert_indices[num_indices + j] = xyz;
            tc_indices[num_indices + j] = tc;
        }
        if (good_tri) {
            num_indices += 3;
        }
    }

    if (num_indices < 3) {
        goto fail;
    }
    if (num_indices != max_indices) {
        Com_DPrintf("%s has %u bad triangles\n", model->name, (max_indices - num_indices) / 3);
    }

    for (uint32_t i = 0; i < num_indices; i++) {
        remap[i] = UINT16_MAX;
    }

    for (uint32_t i = 0; i < num_indices; i++) {
        if (remap[i] != UINT16_MAX) {
            continue;
        }
        for (uint32_t j = i + 1; j < num_indices; j++) {
            if (vert_indices[i] == vert_indices[j] &&
                st[tc_indices[i]].s == st[tc_indices[j]].s &&
                st[tc_indices[i]].t == st[tc_indices[j]].t) {
                remap[j] = (uint16_t)i;
                final_indices[j] = (uint16_t)num_vertices;
            }
        }
        if (num_vertices == UINT16_MAX) {
            goto fail;
        }
        remap[i] = (uint16_t)i;
        final_indices[i] = (uint16_t)num_vertices++;
    }

    size_t frame_vertices = 0;
    size_t vector_components = 0;
    size_t uv_components = 0;
    if (!VK_Entity_ArrayBytes((size_t)num_frames, (size_t)num_vertices,
                              &frame_vertices, "MD2 frame vertices") ||
        !VK_Entity_ArrayBytes(frame_vertices, 3, &vector_components,
                              "MD2 vector components") ||
        !VK_Entity_ArrayBytes((size_t)num_vertices, 2, &uv_components,
                              "MD2 texture coordinates")) {
        goto fail;
    }

    positions = VK_Entity_CallocArray(vector_components, sizeof(*positions),
                                      "MD2 positions");
    normals = VK_Entity_CallocArray(vector_components, sizeof(*normals),
                                    "MD2 normals");
    frame_radii = VK_Entity_CallocArray((size_t)num_frames,
                                        sizeof(*frame_radii),
                                        "MD2 frame radii");
    frame_bounds = VK_Entity_CallocArray((size_t)num_frames,
                                         sizeof(*frame_bounds),
                                         "MD2 frame bounds");
    uv = VK_Entity_CallocArray(uv_components, sizeof(*uv), "MD2 UVs");
    indices = VK_Entity_CallocArray((size_t)num_indices, sizeof(*indices),
                                    "MD2 indices");
    skins = hdr.num_skins ?
        VK_Entity_CallocArray((size_t)hdr.num_skins, sizeof(*skins), "MD2 skins") :
        NULL;
    if (!positions || !normals || !frame_radii || !frame_bounds || !uv || !indices ||
        (hdr.num_skins && !skins)) {
        goto fail;
    }

    for (uint32_t i = 0; i < num_indices; i++) {
        indices[i] = final_indices[i];
    }
    for (uint32_t i = 0; i < num_indices; i++) {
        if (remap[i] != i) {
            continue;
        }
        uint32_t out = final_indices[i];
        uv[out * 2 + 0] = (float)((int16_t)LittleShort(st[tc_indices[i]].s)) / (float)hdr.skinwidth;
        uv[out * 2 + 1] = (float)((int16_t)LittleShort(st[tc_indices[i]].t)) / (float)hdr.skinheight;
    }

    for (uint32_t f = 0; f < num_frames; f++) {
        const dmd2frame_t *frame = (const dmd2frame_t *)(raw + hdr.ofs_frames + (size_t)f * (size_t)hdr.framesize);
        float scale[3] = {
            LittleFloat(frame->scale[0]),
            LittleFloat(frame->scale[1]),
            LittleFloat(frame->scale[2]),
        };
        float translate[3] = {
            LittleFloat(frame->translate[0]),
            LittleFloat(frame->translate[1]),
            LittleFloat(frame->translate[2]),
        };
        vec3_t frame_mins;
        vec3_t frame_maxs;
        ClearBounds(frame_mins, frame_maxs);
        for (uint32_t i = 0; i < num_indices; i++) {
            if (remap[i] != i) {
                continue;
            }
            uint32_t out = final_indices[i];
            const dmd2trivertx_t *src = &frame->verts[vert_indices[i]];
            float *dst = &positions[((size_t)f * num_vertices + out) * 3];
            for (int axis = 0; axis < 3; axis++) {
                float untransformed = src->v[axis] * scale[axis];
                frame_mins[axis] = min(frame_mins[axis], untransformed);
                frame_maxs[axis] = max(frame_maxs[axis], untransformed);
                dst[axis] = untransformed + translate[axis];
            }

            float *dst_normal = &normals[((size_t)f * num_vertices + out) * 3];
            if (src->lightnormalindex < NUMVERTEXNORMALS) {
                VectorCopy(bytedirs[src->lightnormalindex], dst_normal);
            } else {
                VectorSet(dst_normal, 0.0f, 0.0f, 1.0f);
            }
        }
        frame_radii[f] = RadiusFromBounds(frame_mins, frame_maxs);
        for (int axis = 0; axis < 3; axis++) {
            frame_bounds[f].mins[axis] = frame_mins[axis] + translate[axis];
            frame_bounds[f].maxs[axis] = frame_maxs[axis] + translate[axis];
        }
    }

    if (skins) {
        skin_names = VK_Entity_CallocArray((size_t)hdr.num_skins,
                                           sizeof(*skin_names),
                                           "MD2 skin names");
        if (!skin_names) {
            goto fail;
        }

        const char *skin_data = (const char *)(raw + hdr.ofs_skins);
        for (int i = 0; i < hdr.num_skins; i++) {
            char name[MD2_MAX_SKINNAME];
            memcpy(name, skin_data + i * MD2_MAX_SKINNAME, MD2_MAX_SKINNAME);
            name[MD2_MAX_SKINNAME - 1] = '\0';
            FS_NormalizePath(name);
            skins[i] = VK_UI_RegisterImage(name, IT_SKIN, IF_NONE);
            if (skin_names) {
                Q_strlcpy(skin_names[i], name, sizeof(skin_names[i]));
            }
        }
    }

    free(vert_indices);
    free(tc_indices);
    free(remap);
    free(final_indices);

    model->type = VK_MODEL_MD2;
    model->md2.num_frames = num_frames;
    model->md2.num_vertices = num_vertices;
    model->md2.num_indices = num_indices;
    model->md2.positions = positions;
    model->md2.normals = normals;
    model->md2.frame_radii = frame_radii;
    model->md2.frame_bounds = frame_bounds;
    model->md2.uv = uv;
    model->md2.indices = indices;
    model->md2.skins = skins;
    model->md2.skin_names = skin_names;
    model->md2.num_skins = (uint32_t)hdr.num_skins;
    return true;

fail:
    free(positions);
    free(normals);
    free(frame_radii);
    free(frame_bounds);
    free(uv);
    free(indices);
    free(vert_indices);
    free(tc_indices);
    free(remap);
    free(final_indices);
    free(skins);
    free(skin_names);
    return false;
}

typedef enum {
    VK_ENTITY_BLEND_OPAQUE,
    VK_ENTITY_BLEND_OPAQUE_NO_DEPTH_WRITE,
    VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_FAST_LIT,
    VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_FAST_LIT_NO_FOG,
    VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_TEXTURE_REPLACE,
    VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG,
    VK_ENTITY_BLEND_CUTOUT,
    VK_ENTITY_BLEND_ALPHA,
    VK_ENTITY_BLEND_ADDITIVE,
    VK_ENTITY_BLEND_CELSHADING,
    VK_ENTITY_BLEND_FLARE,
    VK_ENTITY_BLEND_OCCLUSION,
} vk_entity_blend_t;

typedef enum {
    VK_ENTITY_STENCIL_NONE = 0,
    // The dedicated bloom attachment cannot replay equal alias depth values
    // reliably on every Vulkan depth format, so additive rim emission is
    // extracted in its own depth-disabled pipeline.
    VK_ENTITY_STENCIL_RIM_BLOOM_NO_DEPTH,
    VK_ENTITY_STENCIL_OUTLINE_MASK,
    VK_ENTITY_STENCIL_OUTLINE_MASK_NO_DEPTH,
    VK_ENTITY_STENCIL_OUTLINE_SHELL,
    VK_ENTITY_STENCIL_OUTLINE_SHELL_NO_DEPTH,
    VK_ENTITY_STENCIL_OUTLINE_CLEAR,
} vk_entity_stencil_t;

typedef enum {
    VK_ENTITY_VERTEX_LAYOUT_DYNAMIC = 0,
    VK_ENTITY_VERTEX_LAYOUT_GPU_MD2,
    VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL,
    VK_ENTITY_VERTEX_LAYOUT_GPU_PARTICLE,
    VK_ENTITY_VERTEX_LAYOUT_GPU_BEAM,
#if USE_MD5
    VK_ENTITY_VERTEX_LAYOUT_GPU_MD5,
#endif
} vk_entity_vertex_layout_t;

static bool VK_Entity_CreatePipelineEx(vk_context_t *ctx, vk_entity_blend_t blend_mode,
                                       bool depth_hack,
                                       vk_entity_stencil_t stencil_mode,
                                       vk_entity_vertex_layout_t vertex_layout,
                                       bool bloom_extract,
                                       bool bloom_depth_sample,
                                       VkPipeline *out_pipeline)
{
    bool gpu_bmodel_fast_lit_no_fog =
        blend_mode == VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_FAST_LIT_NO_FOG;
    bool gpu_bmodel_fast_lit =
        blend_mode == VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_FAST_LIT ||
        gpu_bmodel_fast_lit_no_fog;
    bool gpu_bmodel_texture_replace_no_fog =
        blend_mode == VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG;
    bool gpu_bmodel_texture_replace =
        blend_mode == VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_TEXTURE_REPLACE ||
        gpu_bmodel_texture_replace_no_fog;
    const bool gpu_bmodel_texture_replace_specialized =
        gpu_bmodel_texture_replace &&
        (!vk_bmodel_texture_replace_specialization ||
         vk_bmodel_texture_replace_specialization->integer);
    bool gpu_bmodel_opaque_special =
        gpu_bmodel_fast_lit || gpu_bmodel_texture_replace;
    bool opaque_no_depth_write =
        blend_mode == VK_ENTITY_BLEND_OPAQUE_NO_DEPTH_WRITE;
    bool celshading = blend_mode == VK_ENTITY_BLEND_CELSHADING;
    bool flare = blend_mode == VK_ENTITY_BLEND_FLARE;
    bool occlusion = blend_mode == VK_ENTITY_BLEND_OCCLUSION;
    bool cutout = blend_mode == VK_ENTITY_BLEND_CUTOUT;
    bool alpha = blend_mode != VK_ENTITY_BLEND_OPAQUE &&
                 !opaque_no_depth_write &&
                 !gpu_bmodel_opaque_special && !occlusion && !cutout;
    bool additive = blend_mode == VK_ENTITY_BLEND_ADDITIVE || flare;
    bool outline_mask = stencil_mode == VK_ENTITY_STENCIL_OUTLINE_MASK ||
                        stencil_mode == VK_ENTITY_STENCIL_OUTLINE_MASK_NO_DEPTH;
    bool outline_shell = stencil_mode == VK_ENTITY_STENCIL_OUTLINE_SHELL ||
                         stencil_mode == VK_ENTITY_STENCIL_OUTLINE_SHELL_NO_DEPTH;
    bool outline_clear = stencil_mode == VK_ENTITY_STENCIL_OUTLINE_CLEAR;
    bool outline = outline_mask || outline_shell || outline_clear;
    bool no_depth = stencil_mode == VK_ENTITY_STENCIL_OUTLINE_MASK_NO_DEPTH ||
                    stencil_mode == VK_ENTITY_STENCIL_OUTLINE_SHELL_NO_DEPTH ||
                    outline_clear ||
                    stencil_mode == VK_ENTITY_STENCIL_RIM_BLOOM_NO_DEPTH;
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;

    const uint32_t *vert_code = vk_entity_vert_spv;
    size_t vert_code_size = vk_entity_vert_spv_size;
    if (vertex_layout == VK_ENTITY_VERTEX_LAYOUT_GPU_MD2) {
        vert_code = vk_entity_gpu_md2_vert_spv;
        vert_code_size = vk_entity_gpu_md2_vert_spv_size;
    } else if (vertex_layout == VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL) {
        vert_code = gpu_bmodel_texture_replace_no_fog &&
            gpu_bmodel_texture_replace_specialized
            ? vk_entity_gpu_bmodel_texture_replace_no_fog_vert_spv
            : gpu_bmodel_texture_replace_specialized
            ? vk_entity_gpu_bmodel_texture_replace_vert_spv
            : vk_entity_gpu_bmodel_vert_spv;
        vert_code_size = gpu_bmodel_texture_replace_no_fog &&
            gpu_bmodel_texture_replace_specialized
            ? vk_entity_gpu_bmodel_texture_replace_no_fog_vert_spv_size
            : gpu_bmodel_texture_replace_specialized
            ? vk_entity_gpu_bmodel_texture_replace_vert_spv_size
            : vk_entity_gpu_bmodel_vert_spv_size;
    } else if (vertex_layout == VK_ENTITY_VERTEX_LAYOUT_GPU_PARTICLE) {
        vert_code = vk_entity_gpu_particle_vert_spv;
        vert_code_size = vk_entity_gpu_particle_vert_spv_size;
    } else if (vertex_layout == VK_ENTITY_VERTEX_LAYOUT_GPU_BEAM) {
        vert_code = vk_entity_gpu_beam_vert_spv;
        vert_code_size = vk_entity_gpu_beam_vert_spv_size;
    }
#if USE_MD5
    if (vertex_layout == VK_ENTITY_VERTEX_LAYOUT_GPU_MD5) {
        vert_code = vk_entity_gpu_md5_vert_spv;
        vert_code_size = vk_entity_gpu_md5_vert_spv_size;
    }
#endif
    VkShaderModuleCreateInfo vert_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vert_code_size,
        .pCode = vert_code,
    };
    if (!VK_Entity_Check(vkCreateShaderModule(ctx->device, &vert_info, NULL, &vert),
                         "vkCreateShaderModule(entity vert)")) {
        return false;
    }

    const uint32_t *frag_code = celshading
        ? vk_entity_celshading_frag_spv
        : gpu_bmodel_fast_lit_no_fog
        ? vk_entity_gpu_bmodel_fast_lit_no_fog_frag_spv
        : gpu_bmodel_fast_lit
        ? vk_entity_gpu_bmodel_fast_lit_frag_spv
        : gpu_bmodel_texture_replace_no_fog && gpu_bmodel_texture_replace_specialized
        ? vk_entity_gpu_bmodel_texture_replace_no_fog_frag_spv
        : gpu_bmodel_texture_replace_specialized
        ? vk_entity_gpu_bmodel_texture_replace_frag_spv
        : bloom_depth_sample ? vk_entity_bloom_extract_depth_frag_spv
        : bloom_extract ? vk_entity_bloom_extract_frag_spv : vk_entity_frag_spv;
    const size_t frag_code_size = celshading
        ? vk_entity_celshading_frag_spv_size
        : gpu_bmodel_fast_lit_no_fog
        ? vk_entity_gpu_bmodel_fast_lit_no_fog_frag_spv_size
        : gpu_bmodel_fast_lit
        ? vk_entity_gpu_bmodel_fast_lit_frag_spv_size
        : gpu_bmodel_texture_replace_no_fog && gpu_bmodel_texture_replace_specialized
        ? vk_entity_gpu_bmodel_texture_replace_no_fog_frag_spv_size
        : gpu_bmodel_texture_replace_specialized
        ? vk_entity_gpu_bmodel_texture_replace_frag_spv_size
        : bloom_depth_sample ? vk_entity_bloom_extract_depth_frag_spv_size
        : bloom_extract ? vk_entity_bloom_extract_frag_spv_size
                        : vk_entity_frag_spv_size;
    VkShaderModuleCreateInfo frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = frag_code_size,
        .pCode = frag_code,
    };
    if (!VK_Entity_Check(vkCreateShaderModule(ctx->device, &frag_info, NULL, &frag),
                         "vkCreateShaderModule(entity frag)")) {
        vkDestroyShaderModule(ctx->device, vert, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
    };

    VkVertexInputBindingDescription bindings[4] = { 0 };
    VkVertexInputAttributeDescription attrs[15] = { 0 };
    uint32_t binding_count = 0;
    uint32_t attr_count = 0;
    if (vertex_layout == VK_ENTITY_VERTEX_LAYOUT_GPU_MD2) {
        bindings[0] = (VkVertexInputBindingDescription) {
            .binding = 0,
            .stride = sizeof(vk_md2_gpu_frame_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        bindings[1] = bindings[0];
        bindings[1].binding = 1;
        bindings[2] = (VkVertexInputBindingDescription) {
            .binding = 2,
            .stride = sizeof(float) * 2,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        bindings[3] = (VkVertexInputBindingDescription) {
            .binding = 3,
            .stride = sizeof(vk_md2_gpu_instance_t),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        };
        binding_count = 4;
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_md2_gpu_frame_vertex_t, pos),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_md2_gpu_frame_vertex_t, normal),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 2, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_md2_gpu_frame_vertex_t, pos),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 3, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_md2_gpu_frame_vertex_t, normal),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 4, .binding = 2, .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 0,
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 5, .binding = 3, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_md2_gpu_instance_t, origin_frontlerp),
        };
        for (uint32_t axis = 0; axis < 3; axis++) {
            attrs[attr_count++] = (VkVertexInputAttributeDescription) {
                .location = 6 + axis, .binding = 3,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                .offset = offsetof(vk_md2_gpu_instance_t, scaled_axis) +
                    axis * sizeof(float) * 4,
            };
        }
        for (uint32_t axis = 0; axis < 3; axis++) {
            attrs[attr_count++] = (VkVertexInputAttributeDescription) {
                .location = 9 + axis, .binding = 3,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                .offset = offsetof(vk_md2_gpu_instance_t, normal_axis) +
                    axis * sizeof(float) * 4,
            };
        }
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 12, .binding = 3, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_md2_gpu_instance_t, shell),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 13, .binding = 3, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = offsetof(vk_md2_gpu_instance_t, color),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 14, .binding = 3, .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(vk_md2_gpu_instance_t, flags),
        };
#if USE_MD5
    } else if (vertex_layout == VK_ENTITY_VERTEX_LAYOUT_GPU_MD5) {
        bindings[0] = (VkVertexInputBindingDescription) {
            .binding = 0,
            .stride = sizeof(vk_md5_gpu_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        bindings[1] = (VkVertexInputBindingDescription) {
            .binding = 1,
            .stride = sizeof(vk_md5_gpu_instance_t),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        };
        binding_count = 2;
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_md5_gpu_vertex_t, normal),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_md5_gpu_vertex_t, uv),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 2, .binding = 0, .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(vk_md5_gpu_vertex_t, weight_offset),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 3, .binding = 0, .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(vk_md5_gpu_vertex_t, weight_count),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 4, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_md5_gpu_instance_t, origin),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 5, .binding = 1, .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(vk_md5_gpu_instance_t, joint_palette_offset),
        };
        for (uint32_t axis = 0; axis < 3; axis++) {
            attrs[attr_count++] = (VkVertexInputAttributeDescription) {
                .location = 6 + axis, .binding = 1,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                .offset = offsetof(vk_md5_gpu_instance_t, scaled_axis) +
                    axis * sizeof(float) * 4,
            };
        }
        for (uint32_t axis = 0; axis < 3; axis++) {
            attrs[attr_count++] = (VkVertexInputAttributeDescription) {
                .location = 9 + axis, .binding = 1,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                .offset = offsetof(vk_md5_gpu_instance_t, normal_axis) +
                    axis * sizeof(float) * 4,
            };
        }
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 12, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_md5_gpu_instance_t, shell),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 13, .binding = 1, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = offsetof(vk_md5_gpu_instance_t, color),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 14, .binding = 1, .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(vk_md5_gpu_instance_t, flags),
        };
#endif
    } else if (vertex_layout == VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL) {
        bindings[0] = (VkVertexInputBindingDescription) {
            .binding = 0,
            .stride = sizeof(vk_bmodel_gpu_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        bindings[1] = (VkVertexInputBindingDescription) {
            .binding = 1,
            .stride = sizeof(vk_bmodel_gpu_instance_t),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        };
        binding_count = 2;
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_bmodel_gpu_vertex_t, pos),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_bmodel_gpu_vertex_t, uv),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_bmodel_gpu_vertex_t, lm_uv),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 3, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_bmodel_gpu_vertex_t, normal),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 4, .binding = 0, .format = VK_FORMAT_R32_SFLOAT,
            .offset = offsetof(vk_bmodel_gpu_vertex_t, alpha),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 5, .binding = 0, .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(vk_bmodel_gpu_vertex_t, flags),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 6, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_bmodel_gpu_instance_t, origin),
        };
        for (uint32_t axis = 0; axis < 3; axis++) {
            attrs[attr_count++] = (VkVertexInputAttributeDescription) {
                .location = 7 + axis, .binding = 1,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                .offset = offsetof(vk_bmodel_gpu_instance_t, scaled_axis) +
                    axis * sizeof(float) * 4,
            };
        }
        for (uint32_t axis = 0; axis < 3; axis++) {
            attrs[attr_count++] = (VkVertexInputAttributeDescription) {
                .location = 10 + axis, .binding = 1,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                .offset = offsetof(vk_bmodel_gpu_instance_t, normal_axis) +
                    axis * sizeof(float) * 4,
            };
        }
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 13, .binding = 1, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = offsetof(vk_bmodel_gpu_instance_t, color),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 14, .binding = 1, .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(vk_bmodel_gpu_instance_t, flags),
        };
    } else if (vertex_layout == VK_ENTITY_VERTEX_LAYOUT_GPU_PARTICLE) {
        bindings[0] = (VkVertexInputBindingDescription) {
            .binding = 0,
            .stride = sizeof(vk_particle_gpu_instance_t),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        };
        binding_count = 1;
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_particle_gpu_instance_t, origin_scale),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 1, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = offsetof(vk_particle_gpu_instance_t, color),
        };
    } else if (vertex_layout == VK_ENTITY_VERTEX_LAYOUT_GPU_BEAM) {
        bindings[0] = (VkVertexInputBindingDescription) {
            .binding = 0,
            .stride = sizeof(vk_beam_gpu_instance_t),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        };
        binding_count = 1;
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_beam_gpu_instance_t, start),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_beam_gpu_instance_t, end),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_beam_gpu_instance_t, right),
        };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) {
            .location = 3, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = offsetof(vk_beam_gpu_instance_t, color),
        };
    } else {
        bindings[0] = (VkVertexInputBindingDescription) {
            .binding = 0,
            .stride = sizeof(vk_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        binding_count = 1;
        attrs[attr_count++] = (VkVertexInputAttributeDescription) { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, pos) };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vk_vertex_t, uv) };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vk_vertex_t, lm_uv) };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) { .location = 3, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(vk_vertex_t, color) };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) { .location = 4, .binding = 0, .format = VK_FORMAT_R32_UINT, .offset = offsetof(vk_vertex_t, flags) };
        attrs[attr_count++] = (VkVertexInputAttributeDescription) { .location = 5, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, normal) };
    }
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = binding_count,
        .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = attr_count,
        .pVertexAttributeDescriptions = attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = celshading ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
        // The scene viewport has negative height, which reverses the winding
        // relative to OpenGL's GL_FRONT cull. Cull Vulkan back faces here so
        // only the depth-visible silhouette of the original GL pass remains.
        .cullMode = celshading ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = (bloom_extract || bloom_depth_sample)
            ? VK_SAMPLE_COUNT_1_BIT : ctx->scene_samples,
    };
    VkStencilOpState stencil = {
        .failOp = VK_STENCIL_OP_KEEP,
        .passOp = VK_STENCIL_OP_KEEP,
        .depthFailOp = VK_STENCIL_OP_KEEP,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .compareMask = 0xff,
        .writeMask = 0xff,
        .reference = VK_ENTITY_OUTLINE_STENCIL_REF,
    };
    if (outline_mask) {
        stencil.passOp = VK_STENCIL_OP_REPLACE;
    } else if (outline_shell) {
        stencil.compareOp = VK_COMPARE_OP_NOT_EQUAL;
        stencil.writeMask = 0;
    } else if (outline_clear) {
        stencil.failOp = VK_STENCIL_OP_ZERO;
        stencil.passOp = VK_STENCIL_OP_ZERO;
        stencil.depthFailOp = VK_STENCIL_OP_ZERO;
        stencil.compareOp = VK_COMPARE_OP_EQUAL;
    }

    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = (flare || no_depth) ? VK_FALSE : VK_TRUE,
        .depthWriteEnable = (!outline && !alpha && !cutout &&
                             !opaque_no_depth_write && !occlusion && !bloom_extract)
            ? VK_TRUE : VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .stencilTestEnable = outline ? VK_TRUE : VK_FALSE,
        .front = stencil,
        .back = stencil,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = alpha ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE
                                        : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = additive ? VK_BLEND_FACTOR_SRC_ALPHA
                                        : VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = additive ? VK_BLEND_FACTOR_ONE
                                        : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = (occlusion || outline_mask || outline_clear)
            ? 0
            : VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    VkDynamicState states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_LINE_WIDTH,
    };
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = celshading ? q_countof(states) : q_countof(states) - 1,
        .pDynamicStates = states,
    };

    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = vk_entity.pipeline_layout,
        .renderPass = bloom_depth_sample ? ctx->bloom_rim_extract_render_pass
                    : bloom_extract ? ctx->bloom_extract_render_pass
                                    : ctx->scene_render_pass,
        .subpass = 0,
    };

    bool ok = VK_Entity_Check(vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &info, NULL, out_pipeline),
                              bloom_depth_sample ?
                                  "vkCreateGraphicsPipelines(entity rim bloom depth sample)" :
                              bloom_extract ? "vkCreateGraphicsPipelines(entity bloom extract)" :
                              outline_mask ? "vkCreateGraphicsPipelines(entity outline mask)" :
                              outline_shell ? "vkCreateGraphicsPipelines(entity outline shell)" :
                              outline_clear ? "vkCreateGraphicsPipelines(entity outline clear)" :
                              celshading ? "vkCreateGraphicsPipelines(entity cel shading)" :
                              depth_hack ? "vkCreateGraphicsPipelines(entity depthhack)" :
                              (occlusion ? "vkCreateGraphicsPipelines(entity flare occlusion)" :
                               flare ? "vkCreateGraphicsPipelines(entity flare)" :
                               gpu_bmodel_fast_lit_no_fog
                                   ? "vkCreateGraphicsPipelines(entity inline-BSP fast lit no fog)"
                               : gpu_bmodel_texture_replace_no_fog
                                   ? "vkCreateGraphicsPipelines(entity inline-BSP texture replace no fog)"
                               : gpu_bmodel_texture_replace
                                   ? "vkCreateGraphicsPipelines(entity inline-BSP texture replace)"
                               : additive ? "vkCreateGraphicsPipelines(entity additive)" :
                               alpha ? "vkCreateGraphicsPipelines(entity alpha)" :
                               opaque_no_depth_write
                                   ? "vkCreateGraphicsPipelines(entity opaque no depth write)"
                               :
                                       "vkCreateGraphicsPipelines(entity opaque)"));

    // Build a compatible one-sample version for the native DOF scene pass.
    // Bloom extraction already has its own single-sample render pass and is
    // deliberately excluded from this mapping.
    if (ok && !bloom_extract && !bloom_depth_sample &&
        ctx->scene_single_sample_render_pass) {
        VkPipeline single_sample_pipeline = VK_NULL_HANDLE;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        info.renderPass = ctx->scene_single_sample_render_pass;
        ok = VK_Entity_Check(vkCreateGraphicsPipelines(
                                 ctx->device, VK_NULL_HANDLE, 1, &info, NULL,
                                 &single_sample_pipeline),
                             "vkCreateGraphicsPipelines(entity single sample)");
        if (ok) {
            ok = VK_RegisterScenePipelineVariant(
                ctx, *out_pipeline, single_sample_pipeline);
        }
        if (!ok) {
            if (single_sample_pipeline) {
                vkDestroyPipeline(ctx->device, single_sample_pipeline, NULL);
            }
            vkDestroyPipeline(ctx->device, *out_pipeline, NULL);
            *out_pipeline = VK_NULL_HANDLE;
        }
    }

    vkDestroyShaderModule(ctx->device, vert, NULL);
    vkDestroyShaderModule(ctx->device, frag, NULL);
    return ok;
}

static bool VK_Entity_CreatePipeline(vk_context_t *ctx, vk_entity_blend_t blend_mode,
                                     bool depth_hack,
                                     vk_entity_stencil_t stencil_mode,
                                     vk_entity_vertex_layout_t vertex_layout,
                                     VkPipeline *out_pipeline)
{
    return VK_Entity_CreatePipelineEx(ctx, blend_mode, depth_hack, stencil_mode,
                                      vertex_layout, false, false, out_pipeline);
}

bool VK_Entity_Init(vk_context_t *ctx)
{
    memset(&vk_entity, 0, sizeof(vk_entity));
    if (!ctx) {
        return false;
    }
    vk_entity.ctx = ctx;
    vk_entity.registration_sequence = 1;

    if (!vk_drawentities) {
        vk_drawentities = Cvar_Get("vk_drawentities", "1", 0);
    }
    if (!vk_draworder) {
        vk_draworder = Cvar_Get("vk_draworder", "1", 0);
    }
    if (!vk_partscale) {
        vk_partscale = Cvar_Get("vk_partscale", "2", 0);
    }
    if (!vk_particle_style) {
        vk_particle_style = Cvar_Get("vk_particle_style", "0", 0);
    }
    if (!vk_particle_shape) {
        vk_particle_shape = Cvar_Get("vk_particle_shape", "0", CVAR_ARCHIVE);
        vk_particle_shape->changed = VK_Entity_ParticleShapeChanged;
    }
    if (!vk_particle_gpu_instancing) {
        // Keeps a native CPU-expanded Vulkan fallback available for driver
        // investigations while defaulting to the compact instanced path.
        vk_particle_gpu_instancing = Cvar_Get("vk_particle_gpu_instancing",
                                              "1", CVAR_ARCHIVE);
    }
    if (!vk_beam_style) {
        vk_beam_style = Cvar_Get("vk_beam_style", "0", 0);
    }
    if (!vk_beam_gpu_instancing) {
        vk_beam_gpu_instancing = Cvar_Get("vk_beam_gpu_instancing", "1",
                                          CVAR_ARCHIVE);
    }
    if (!vk_flare_fade_speed) {
        vk_flare_fade_speed = Cvar_Get("vk_flare_fade_speed", "8", 0);
    }
    if (!vk_player_outline_width) {
        // Shared client presentation control; use the same archived value as
        // OpenGL rather than introducing a renderer-specific visual setting.
        vk_player_outline_width = Cvar_Get("cl_player_outline_width", "2.0",
                                           CVAR_ARCHIVE);
    }
    if (!vk_md2_gpu_lerp) {
        vk_md2_gpu_lerp = Cvar_Get("vk_md2_gpu_lerp", "1",
                                   CVAR_ARCHIVE | CVAR_LATCH);
    }
    if (!vk_bmodel_fast_lit) {
        // Keep the fully general native Vulkan route available for focused
        // regression/performance comparisons and driver workarounds. This
        // switch is immediate because batch classification is frame-local.
        vk_bmodel_fast_lit = Cvar_Get("vk_bmodel_fast_lit", "1",
                                      CVAR_ARCHIVE);
    }
    if (!vk_bmodel_fast_lit_no_fog) {
        vk_bmodel_fast_lit_no_fog = Cvar_Get("vk_bmodel_fast_lit_no_fog", "1",
                                             CVAR_ARCHIVE);
    }
    if (!vk_bmodel_texture_replace) {
        vk_bmodel_texture_replace = Cvar_Get("vk_bmodel_texture_replace", "1",
                                             CVAR_ARCHIVE);
    }
    if (!vk_bmodel_texture_replace_specialization) {
        vk_bmodel_texture_replace_specialization = Cvar_Get(
            "vk_bmodel_texture_replace_specialization", "1",
            CVAR_ARCHIVE);
    }
    if (!vk_bmodel_binding_cache) {
        // All ordinary inline BSP batches reference the same immutable vertex
        // buffer and the current frame's instance stream.  Avoid redundantly
        // rebinding that pair between adjacent compatible batches, while
        // retaining an immediate switch for driver triage and A/B telemetry.
        vk_bmodel_binding_cache = Cvar_Get("vk_bmodel_binding_cache", "1",
                                           CVAR_ARCHIVE);
    }
    if (!vk_cull_models) {
        // Match OpenGL's default-on frustum cull before allocating transient
        // entity data. Keep the immediate native switch for A/B captures and
        // for diagnosing unusual model bounds in replacement packs.
        vk_cull_models = Cvar_Get("vk_cull_models", "1", 0);
    }
    if (!vk_celshading) {
        // Mirror the OpenGL cel-pass default. This is renderer-exclusive
        // because the Vulkan replay has independent device requirements.
        vk_celshading = Cvar_Get("vk_celshading", "0", 0);
    }
    if (!vk_showorigins) {
        vk_showorigins = Cvar_Get("vk_showorigins", "0", CVAR_CHEAT);
    }
#if USE_MD5
    if (!vk_md5_load) {
        vk_md5_load = Cvar_Get("vk_md5_load", "1", CVAR_FILES);
    }
    if (!vk_md5_use) {
        // Match gl_md5_use's default so both renderers prefer MD5
        // replacement models when the assets are present.
        vk_md5_use = Cvar_Get("vk_md5_use", "1", 0);
    }
    if (!vk_md5_distance) {
        vk_md5_distance = Cvar_Get("vk_md5_distance", "2048", 0);
    }
    if (!vk_md5_gpu_skinning) {
        vk_md5_gpu_skinning = Cvar_Get("vk_md5_gpu_skinning", "1",
                                       CVAR_ARCHIVE | CVAR_LATCH);
    }
#endif

    VkDescriptorSetLayout ui_set_layout = VK_UI_GetDescriptorSetLayout();
    if (!ui_set_layout) {
        Com_SetLastError("Vulkan entity: descriptor set layout unavailable");
        return false;
    }
    VkDescriptorSetLayout shadow_set_layout = VK_Shadow_GetDescriptorSetLayout();
    if (!shadow_set_layout) {
        Com_SetLastError("Vulkan entity: shadow descriptor set layout unavailable");
        return false;
    }

#if USE_MD5
    VkDescriptorSetLayoutBinding md5_bindings[2] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        },
    };
    VkDescriptorSetLayoutCreateInfo md5_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = q_countof(md5_bindings),
        .pBindings = md5_bindings,
    };
    if (!VK_Entity_Check(vkCreateDescriptorSetLayout(
                             ctx->device, &md5_layout_info, NULL,
                             &vk_entity.gpu_md5_set_layout),
                         "vkCreateDescriptorSetLayout(entity MD5)")) {
        return false;
    }

    VkDescriptorPoolSize md5_pool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = VK_MAX_FRAMES_IN_FLIGHT * q_countof(md5_bindings),
    };
    VkDescriptorPoolCreateInfo md5_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = VK_MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 1,
        .pPoolSizes = &md5_pool_size,
    };
    if (!VK_Entity_Check(vkCreateDescriptorPool(ctx->device, &md5_pool_info,
                                                NULL,
                                                &vk_entity.gpu_md5_descriptor_pool),
                         "vkCreateDescriptorPool(entity MD5)")) {
        vkDestroyDescriptorSetLayout(ctx->device,
                                     vk_entity.gpu_md5_set_layout, NULL);
        vk_entity.gpu_md5_set_layout = VK_NULL_HANDLE;
        return false;
    }
    VkDescriptorSetLayout md5_set_layouts[VK_MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
        md5_set_layouts[i] = vk_entity.gpu_md5_set_layout;
    }
    VkDescriptorSetAllocateInfo md5_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk_entity.gpu_md5_descriptor_pool,
        .descriptorSetCount = VK_MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts = md5_set_layouts,
    };
    if (!VK_Entity_Check(vkAllocateDescriptorSets(
                             ctx->device, &md5_alloc_info,
                             vk_entity.gpu_md5_descriptor_sets),
                         "vkAllocateDescriptorSets(entity MD5)")) {
        vkDestroyDescriptorPool(ctx->device,
                                vk_entity.gpu_md5_descriptor_pool, NULL);
        vkDestroyDescriptorSetLayout(ctx->device,
                                     vk_entity.gpu_md5_set_layout, NULL);
        vk_entity.gpu_md5_descriptor_pool = VK_NULL_HANDLE;
        vk_entity.gpu_md5_set_layout = VK_NULL_HANDLE;
        return false;
    }
#endif

    VkPushConstantRange push = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(renderer_view_push_t),
    };
    VkDescriptorSetLayout set_layouts[] = {
        ui_set_layout,
        ui_set_layout,
        shadow_set_layout,
#if USE_MD5
        vk_entity.gpu_md5_set_layout,
#endif
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = q_countof(set_layouts),
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    if (!VK_Entity_Check(vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &vk_entity.pipeline_layout),
                         "vkCreatePipelineLayout(entity)")) {
#if USE_MD5
        vkDestroyDescriptorPool(ctx->device, vk_entity.gpu_md5_descriptor_pool,
                                NULL);
        vkDestroyDescriptorSetLayout(ctx->device, vk_entity.gpu_md5_set_layout,
                                     NULL);
        vk_entity.gpu_md5_descriptor_pool = VK_NULL_HANDLE;
        vk_entity.gpu_md5_set_layout = VK_NULL_HANDLE;
#endif
        return false;
    }

    static byte white_rgba[4] = { 255, 255, 255, 255 };
    vk_entity.white_image = VK_UI_RegisterRawImage("**vk_entity_white**", 1, 1, white_rgba,
                                                    IT_PIC, IF_PERMANENT | IF_OPAQUE);
    vk_entity.white_set = VK_UI_GetDescriptorSetForImage(vk_entity.white_image);
    if (!vk_entity.white_set) {
        Com_SetLastError("Vulkan entity: white descriptor unavailable");
        VK_Entity_Shutdown(ctx);
        return false;
    }
    VK_Entity_InitParticleTexture();
    VK_Entity_InitBeamTexture();

    VkQueryPoolCreateInfo query_info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = MAX_EDICTS,
    };
    if (!VK_Entity_Check(vkCreateQueryPool(ctx->device, &query_info, NULL,
                                           &vk_entity.flare_query_pool),
                         "vkCreateQueryPool(flares)")) {
        return false;
    }

    vk_entity.initialized = true;
    return true;
}

#if USE_MD5
static void VK_Entity_UpdateMd5DescriptorSets(void)
{
    if (!vk_entity.ctx || !vk_entity.ctx->device ||
        !vk_entity.gpu_md5_weight_buffer || !vk_entity.gpu_md5_weight_buffer_bytes ||
        !vk_entity.gpu_md5_descriptor_pool || !vk_entity.ctx->frame_count ||
        vk_entity.ctx->current_frame >= vk_entity.ctx->frame_count ||
        vk_entity.ctx->current_frame >= VK_MAX_FRAMES_IN_FLIGHT) {
        return;
    }
    const uint32_t frame_index = vk_entity.ctx->current_frame;
    vk_entity_frame_buffer_t *frame = &vk_entity.frame_buffers[frame_index];
    VkDescriptorSet set = vk_entity.gpu_md5_descriptor_sets[frame_index];
    if (!set || !frame->instance_buffer || !frame->md5_palette_capacity) {
        return;
    }
    /* The current frame fence is waited before this stream is prepared.
     * Other frame-indexed descriptor sets can still be pending on the GPU. */
    VkDescriptorBufferInfo weight_info = {
        .buffer = vk_entity.gpu_md5_weight_buffer,
        .range = vk_entity.gpu_md5_weight_buffer_bytes,
    };
    VkDescriptorBufferInfo palette_info = {
        .buffer = frame->instance_buffer,
        .offset = frame->md5_palette_offset,
        .range = frame->md5_palette_capacity,
    };
    VkWriteDescriptorSet writes[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &weight_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &palette_info,
        },
    };
    vkUpdateDescriptorSets(vk_entity.ctx->device, q_countof(writes), writes,
                           0, NULL);
}
#endif

static void VK_Entity_DestroyGpuMd2Pipelines(void)
{
    if (!vk_entity.ctx || !vk_entity.ctx->device) {
        return;
    }
    VkDevice device = vk_entity.ctx->device;
    VkPipeline *pipelines[] = {
        &vk_entity.pipeline_gpu_md2_bloom_extract_additive_depth_sample,
        &vk_entity.pipeline_gpu_md2_bloom_extract_additive,
        &vk_entity.pipeline_gpu_md2_bloom_extract_alpha,
        &vk_entity.pipeline_gpu_md2_bloom_extract,
        &vk_entity.pipeline_gpu_md2_celshading,
        &vk_entity.pipeline_gpu_md2_depthhack_additive,
        &vk_entity.pipeline_gpu_md2_depthhack_alpha,
        &vk_entity.pipeline_gpu_md2_depthhack_opaque,
        &vk_entity.pipeline_gpu_md2_additive,
        &vk_entity.pipeline_gpu_md2_alpha,
        &vk_entity.pipeline_gpu_md2_opaque,
    };
    for (uint32_t i = 0; i < q_countof(pipelines); i++) {
        if (*pipelines[i]) {
            vkDestroyPipeline(device, *pipelines[i], NULL);
            *pipelines[i] = VK_NULL_HANDLE;
        }
    }
    vk_entity.gpu_md2_available = false;
}

static void VK_Entity_DestroyGpuBmodelPipelines(void)
{
    if (!vk_entity.ctx || !vk_entity.ctx->device) {
        return;
    }
    VkDevice device = vk_entity.ctx->device;
    VkPipeline *pipelines[] = {
        &vk_entity.pipeline_gpu_bmodel_bloom_extract,
        &vk_entity.pipeline_gpu_bmodel_additive,
        &vk_entity.pipeline_gpu_bmodel_alpha,
        &vk_entity.pipeline_gpu_bmodel_fast_lit_opaque,
        &vk_entity.pipeline_gpu_bmodel_fast_lit_no_fog_opaque,
        &vk_entity.pipeline_gpu_bmodel_texture_replace_opaque,
        &vk_entity.pipeline_gpu_bmodel_texture_replace_no_fog_opaque,
        &vk_entity.pipeline_gpu_bmodel_opaque,
    };
    for (uint32_t i = 0; i < q_countof(pipelines); i++) {
        if (*pipelines[i]) {
            vkDestroyPipeline(device, *pipelines[i], NULL);
            *pipelines[i] = VK_NULL_HANDLE;
        }
    }
    vk_entity.gpu_bmodel_available = false;
}

static void VK_Entity_DestroyGpuParticlePipelines(void)
{
    if (!vk_entity.ctx || !vk_entity.ctx->device) {
        return;
    }
    VkDevice device = vk_entity.ctx->device;
    VkPipeline *pipelines[] = {
        &vk_entity.pipeline_gpu_particle_additive,
        &vk_entity.pipeline_gpu_particle_alpha,
    };
    for (uint32_t i = 0; i < q_countof(pipelines); i++) {
        if (*pipelines[i]) {
            vkDestroyPipeline(device, *pipelines[i], NULL);
            *pipelines[i] = VK_NULL_HANDLE;
        }
    }
    vk_entity.gpu_particle_available = false;
}

static void VK_Entity_DestroyGpuBeamPipelines(void)
{
    if (!vk_entity.ctx || !vk_entity.ctx->device) {
        return;
    }
    VkDevice device = vk_entity.ctx->device;
    VkPipeline *pipelines[] = {
        &vk_entity.pipeline_gpu_beam_depthhack_alpha,
        &vk_entity.pipeline_gpu_beam_alpha,
    };
    for (uint32_t i = 0; i < q_countof(pipelines); i++) {
        if (*pipelines[i]) {
            vkDestroyPipeline(device, *pipelines[i], NULL);
            *pipelines[i] = VK_NULL_HANDLE;
        }
    }
    vk_entity.gpu_beam_available = false;
}

#if USE_MD5
static void VK_Entity_DestroyGpuMd5Pipelines(void)
{
    if (!vk_entity.ctx || !vk_entity.ctx->device) {
        return;
    }
    VkDevice device = vk_entity.ctx->device;
    VkPipeline *pipelines[] = {
        &vk_entity.pipeline_gpu_md5_bloom_extract_additive_depth_sample,
        &vk_entity.pipeline_gpu_md5_bloom_extract_additive,
        &vk_entity.pipeline_gpu_md5_bloom_extract_alpha,
        &vk_entity.pipeline_gpu_md5_bloom_extract,
        &vk_entity.pipeline_gpu_md5_celshading,
        &vk_entity.pipeline_gpu_md5_depthhack_additive,
        &vk_entity.pipeline_gpu_md5_depthhack_alpha,
        &vk_entity.pipeline_gpu_md5_depthhack_opaque,
        &vk_entity.pipeline_gpu_md5_additive,
        &vk_entity.pipeline_gpu_md5_alpha,
        &vk_entity.pipeline_gpu_md5_opaque,
    };
    for (uint32_t i = 0; i < q_countof(pipelines); i++) {
        if (*pipelines[i]) {
            vkDestroyPipeline(device, *pipelines[i], NULL);
            *pipelines[i] = VK_NULL_HANDLE;
        }
    }
    vk_entity.gpu_md5_available = false;
}
#endif

void VK_Entity_DestroySwapchainResources(vk_context_t *ctx)
{
    (void)ctx;
    if (!vk_entity.initialized || !vk_entity.ctx || !vk_entity.ctx->device) {
        return;
    }
    VK_Entity_DestroyGpuMd2Pipelines();
    VK_Entity_DestroyGpuBmodelPipelines();
    VK_Entity_DestroyGpuParticlePipelines();
    VK_Entity_DestroyGpuBeamPipelines();
#if USE_MD5
    VK_Entity_DestroyGpuMd5Pipelines();
#endif
    if (vk_entity.pipeline_outline_clear) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_outline_clear, NULL);
        vk_entity.pipeline_outline_clear = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_outline_shell_no_depth_alpha) {
        vkDestroyPipeline(vk_entity.ctx->device,
                          vk_entity.pipeline_outline_shell_no_depth_alpha, NULL);
        vk_entity.pipeline_outline_shell_no_depth_alpha = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_outline_shell_no_depth_opaque) {
        vkDestroyPipeline(vk_entity.ctx->device,
                          vk_entity.pipeline_outline_shell_no_depth_opaque, NULL);
        vk_entity.pipeline_outline_shell_no_depth_opaque = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_outline_shell_alpha) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_outline_shell_alpha, NULL);
        vk_entity.pipeline_outline_shell_alpha = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_outline_shell_opaque) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_outline_shell_opaque, NULL);
        vk_entity.pipeline_outline_shell_opaque = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_outline_mask_no_depth) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_outline_mask_no_depth, NULL);
        vk_entity.pipeline_outline_mask_no_depth = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_outline_mask) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_outline_mask, NULL);
        vk_entity.pipeline_outline_mask = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_alpha) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_alpha, NULL);
        vk_entity.pipeline_alpha = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_cutout) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_cutout, NULL);
        vk_entity.pipeline_cutout = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_additive) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_additive, NULL);
        vk_entity.pipeline_additive = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_celshading) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_celshading,
                          NULL);
        vk_entity.pipeline_celshading = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_flare) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_flare, NULL);
        vk_entity.pipeline_flare = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_occlusion) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_occlusion, NULL);
        vk_entity.pipeline_occlusion = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_depthhack_alpha) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_depthhack_alpha, NULL);
        vk_entity.pipeline_depthhack_alpha = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_depthhack_cutout) {
        vkDestroyPipeline(vk_entity.ctx->device,
                          vk_entity.pipeline_depthhack_cutout, NULL);
        vk_entity.pipeline_depthhack_cutout = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_depthhack_additive) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_depthhack_additive, NULL);
        vk_entity.pipeline_depthhack_additive = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_depthhack_opaque) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_depthhack_opaque, NULL);
        vk_entity.pipeline_depthhack_opaque = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_depthhack_opaque_no_depth_write) {
        vkDestroyPipeline(vk_entity.ctx->device,
                          vk_entity.pipeline_depthhack_opaque_no_depth_write,
                          NULL);
        vk_entity.pipeline_depthhack_opaque_no_depth_write = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_opaque) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_opaque, NULL);
        vk_entity.pipeline_opaque = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_opaque_no_depth_write) {
        vkDestroyPipeline(vk_entity.ctx->device,
                          vk_entity.pipeline_opaque_no_depth_write, NULL);
        vk_entity.pipeline_opaque_no_depth_write = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_bloom_extract) {
        vkDestroyPipeline(vk_entity.ctx->device, vk_entity.pipeline_bloom_extract, NULL);
        vk_entity.pipeline_bloom_extract = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_bloom_extract_alpha) {
        vkDestroyPipeline(vk_entity.ctx->device,
                          vk_entity.pipeline_bloom_extract_alpha, NULL);
        vk_entity.pipeline_bloom_extract_alpha = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_bloom_extract_additive) {
        vkDestroyPipeline(vk_entity.ctx->device,
                          vk_entity.pipeline_bloom_extract_additive, NULL);
        vk_entity.pipeline_bloom_extract_additive = VK_NULL_HANDLE;
    }
    if (vk_entity.pipeline_bloom_extract_additive_depth_sample) {
        vkDestroyPipeline(vk_entity.ctx->device,
                          vk_entity.pipeline_bloom_extract_additive_depth_sample,
                          NULL);
        vk_entity.pipeline_bloom_extract_additive_depth_sample = VK_NULL_HANDLE;
    }
    vk_entity.stencil_available = false;
    vk_entity.swapchain_ready = false;
}

bool VK_Entity_CreateSwapchainResources(vk_context_t *ctx)
{
    if (!vk_entity.initialized || !ctx || !ctx->scene_render_pass) {
        return false;
    }
    VK_Entity_DestroySwapchainResources(ctx);
    if (!VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_OPAQUE, false,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_opaque) ||
        !VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_OPAQUE_NO_DEPTH_WRITE,
                                  false, VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_opaque_no_depth_write)) {
        return false;
    }
    if (ctx->bloom_extract_render_pass &&
        !VK_Entity_CreatePipelineEx(ctx, VK_ENTITY_BLEND_OPAQUE, false,
                                    VK_ENTITY_STENCIL_NONE,
                                    VK_ENTITY_VERTEX_LAYOUT_DYNAMIC, true, false,
                                    &vk_entity.pipeline_bloom_extract)) {
        Com_WPrintf("Vulkan entity: dynamic bloom extraction unavailable.\n");
    }
    if (ctx->bloom_extract_render_pass &&
        !VK_Entity_CreatePipelineEx(ctx, VK_ENTITY_BLEND_ALPHA, false,
                                    VK_ENTITY_STENCIL_NONE,
                                    VK_ENTITY_VERTEX_LAYOUT_DYNAMIC, true, false,
                                    &vk_entity.pipeline_bloom_extract_alpha)) {
        Com_WPrintf("Vulkan entity: dynamic shell bloom extraction unavailable.\n");
    }
    if (ctx->bloom_extract_render_pass &&
        !VK_Entity_CreatePipelineEx(ctx, VK_ENTITY_BLEND_ADDITIVE, false,
                                    VK_ENTITY_STENCIL_RIM_BLOOM_NO_DEPTH,
                                    VK_ENTITY_VERTEX_LAYOUT_DYNAMIC, true, false,
                                    &vk_entity.pipeline_bloom_extract_additive)) {
        Com_WPrintf("Vulkan entity: dynamic rim bloom extraction unavailable.\n");
    }
    if (ctx->bloom_rim_extract_render_pass &&
        ctx->frames[0].bloom_depth_descriptor_set &&
        !VK_Entity_CreatePipelineEx(
            ctx, VK_ENTITY_BLEND_ADDITIVE, false,
            VK_ENTITY_STENCIL_RIM_BLOOM_NO_DEPTH,
            VK_ENTITY_VERTEX_LAYOUT_DYNAMIC, true, true,
            &vk_entity.pipeline_bloom_extract_additive_depth_sample)) {
        Com_WPrintf("Vulkan entity: sampled-depth rim bloom extraction unavailable.\n");
    }
    if (!VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_CUTOUT, false,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_cutout) ||
        !VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ALPHA, false,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_alpha)) {
        VK_Entity_DestroySwapchainResources(ctx);
        return false;
    }
    if (!VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ADDITIVE, false,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_additive)) {
        VK_Entity_DestroySwapchainResources(ctx);
        return false;
    }
    if (ctx->celshading_supported &&
        !VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_CELSHADING, false,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_celshading)) {
        // Normal model rendering stays usable when an optional line-mode
        // pipeline is rejected. The cvar then remains a harmless no-op.
        Com_WPrintf("Vulkan entity: cel-shading pipeline unavailable.\n");
    }
    if (!VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_FLARE, false,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_flare)) {
        VK_Entity_DestroySwapchainResources(ctx);
        return false;
    }
    if (!VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_OCCLUSION, false,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_occlusion)) {
        VK_Entity_DestroySwapchainResources(ctx);
        return false;
    }
    if (!VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_OPAQUE, true,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_depthhack_opaque) ||
        !VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_OPAQUE_NO_DEPTH_WRITE,
                                  true, VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_depthhack_opaque_no_depth_write)) {
        VK_Entity_DestroySwapchainResources(ctx);
        return false;
    }
    if (!VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_CUTOUT, true,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_depthhack_cutout) ||
        !VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ALPHA, true,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_depthhack_alpha)) {
        VK_Entity_DestroySwapchainResources(ctx);
        return false;
    }
    if (!VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ADDITIVE, true,
                                  VK_ENTITY_STENCIL_NONE,
                                  VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                                  &vk_entity.pipeline_depthhack_additive)) {
        VK_Entity_DestroySwapchainResources(ctx);
        return false;
    }

    {
        const bool gpu_pipelines_ok =
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ALPHA, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_PARTICLE,
                                     &vk_entity.pipeline_gpu_particle_alpha) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ADDITIVE, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_PARTICLE,
                                     &vk_entity.pipeline_gpu_particle_additive);
        if (gpu_pipelines_ok) {
            vk_entity.gpu_particle_available = true;
        } else {
            Com_WPrintf("Vulkan entity: GPU particle pipelines unavailable; "
                        "using CPU expansion\n");
            VK_Entity_DestroyGpuParticlePipelines();
        }
    }

    {
        const bool gpu_pipelines_ok =
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ALPHA, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_BEAM,
                                     &vk_entity.pipeline_gpu_beam_alpha) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ALPHA, true,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_BEAM,
                                     &vk_entity.pipeline_gpu_beam_depthhack_alpha);
        if (gpu_pipelines_ok) {
            vk_entity.gpu_beam_available = true;
        } else {
            Com_WPrintf("Vulkan entity: GPU beam pipelines unavailable; "
                        "using CPU expansion\n");
            VK_Entity_DestroyGpuBeamPipelines();
        }
    }

    if (vk_md2_gpu_lerp && vk_md2_gpu_lerp->integer) {
        const bool gpu_pipelines_ok =
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_OPAQUE, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD2,
                                     &vk_entity.pipeline_gpu_md2_opaque) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ALPHA, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD2,
                                     &vk_entity.pipeline_gpu_md2_alpha) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ADDITIVE, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD2,
                                     &vk_entity.pipeline_gpu_md2_additive) &&
            (!ctx->celshading_supported ||
             VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_CELSHADING, false,
                                      VK_ENTITY_STENCIL_NONE,
                                      VK_ENTITY_VERTEX_LAYOUT_GPU_MD2,
                                      &vk_entity.pipeline_gpu_md2_celshading)) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_OPAQUE, true,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD2,
                                     &vk_entity.pipeline_gpu_md2_depthhack_opaque) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ALPHA, true,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD2,
                                     &vk_entity.pipeline_gpu_md2_depthhack_alpha) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ADDITIVE, true,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD2,
                                     &vk_entity.pipeline_gpu_md2_depthhack_additive);
        if (gpu_pipelines_ok) {
            vk_entity.gpu_md2_available = true;
        } else {
            Com_WPrintf("Vulkan entity: GPU MD2 pipelines unavailable; "
                        "using CPU interpolation\n");
            VK_Entity_DestroyGpuMd2Pipelines();
        }
    }
    if (ctx->bloom_extract_render_pass && vk_entity.gpu_md2_available &&
        !VK_Entity_CreatePipelineEx(ctx, VK_ENTITY_BLEND_OPAQUE, false,
                                    VK_ENTITY_STENCIL_NONE,
                                    VK_ENTITY_VERTEX_LAYOUT_GPU_MD2, true, false,
                                    &vk_entity.pipeline_gpu_md2_bloom_extract)) {
        Com_WPrintf("Vulkan entity: GPU MD2 bloom extraction unavailable.\n");
    }
    if (ctx->bloom_extract_render_pass && vk_entity.gpu_md2_available &&
        !VK_Entity_CreatePipelineEx(ctx, VK_ENTITY_BLEND_ALPHA, false,
                                    VK_ENTITY_STENCIL_NONE,
                                    VK_ENTITY_VERTEX_LAYOUT_GPU_MD2, true, false,
                                    &vk_entity.pipeline_gpu_md2_bloom_extract_alpha)) {
        Com_WPrintf("Vulkan entity: GPU MD2 shell bloom extraction unavailable.\n");
    }
    if (ctx->bloom_extract_render_pass && vk_entity.gpu_md2_available &&
        !VK_Entity_CreatePipelineEx(ctx, VK_ENTITY_BLEND_ADDITIVE, false,
                                    VK_ENTITY_STENCIL_RIM_BLOOM_NO_DEPTH,
                                    VK_ENTITY_VERTEX_LAYOUT_GPU_MD2, true, false,
                                    &vk_entity.pipeline_gpu_md2_bloom_extract_additive)) {
        Com_WPrintf("Vulkan entity: GPU MD2 rim bloom extraction unavailable.\n");
    }
    if (ctx->bloom_rim_extract_render_pass && vk_entity.gpu_md2_available &&
        ctx->frames[0].bloom_depth_descriptor_set &&
        !VK_Entity_CreatePipelineEx(
            ctx, VK_ENTITY_BLEND_ADDITIVE, false,
            VK_ENTITY_STENCIL_RIM_BLOOM_NO_DEPTH,
            VK_ENTITY_VERTEX_LAYOUT_GPU_MD2, true, true,
            &vk_entity.pipeline_gpu_md2_bloom_extract_additive_depth_sample)) {
        Com_WPrintf("Vulkan entity: GPU MD2 sampled-depth rim bloom extraction unavailable.\n");
    }

    {
        const bool gpu_pipelines_ok =
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_OPAQUE, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL,
                                     &vk_entity.pipeline_gpu_bmodel_opaque) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ALPHA, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL,
                                     &vk_entity.pipeline_gpu_bmodel_alpha) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ADDITIVE, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL,
                                     &vk_entity.pipeline_gpu_bmodel_additive);
        if (gpu_pipelines_ok) {
            vk_entity.gpu_bmodel_available = true;
            if (!VK_Entity_CreatePipeline(
                    ctx, VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_FAST_LIT, false,
                    VK_ENTITY_STENCIL_NONE, VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL,
                    &vk_entity.pipeline_gpu_bmodel_fast_lit_opaque)) {
                // The general opaque pipeline remains a complete native
                // fallback when this optional specialization cannot compile.
                Com_WPrintf("Vulkan entity: GPU inline-BSP fast-light pipeline "
                            "unavailable; using the general opaque pipeline\n");
            } else if (!VK_Entity_CreatePipeline(
                           ctx, VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_FAST_LIT_NO_FOG,
                           false, VK_ENTITY_STENCIL_NONE,
                           VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL,
                           &vk_entity.pipeline_gpu_bmodel_fast_lit_no_fog_opaque)) {
                // Retain the fog-aware native fast pipeline if an optional
                // no-fog fragment module is rejected by the driver.
                Com_WPrintf("Vulkan entity: GPU inline-BSP no-fog fast-light pipeline "
                            "unavailable; using the fog-aware static pipeline\n");
            }
            if (!VK_Entity_CreatePipeline(
                    ctx, VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_TEXTURE_REPLACE,
                    false, VK_ENTITY_STENCIL_NONE,
                    VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL,
                    &vk_entity.pipeline_gpu_bmodel_texture_replace_opaque)) {
                Com_WPrintf("Vulkan entity: GPU inline-BSP texture-replace pipeline "
                            "unavailable; using the general opaque pipeline\n");
            } else if (!VK_Entity_CreatePipeline(
                           ctx,
                           VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG,
                           false, VK_ENTITY_STENCIL_NONE,
                           VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL,
                           &vk_entity.pipeline_gpu_bmodel_texture_replace_no_fog_opaque)) {
                Com_WPrintf("Vulkan entity: GPU inline-BSP no-fog texture-replace "
                            "pipeline unavailable; using the fog-aware static pipeline\n");
            }
        } else {
            Com_WPrintf("Vulkan entity: GPU inline-BSP pipelines unavailable; "
                        "using CPU expansion\n");
            VK_Entity_DestroyGpuBmodelPipelines();
        }
    }
    if (ctx->bloom_extract_render_pass && vk_entity.gpu_bmodel_available &&
        !VK_Entity_CreatePipelineEx(ctx, VK_ENTITY_BLEND_OPAQUE, false,
                                    VK_ENTITY_STENCIL_NONE,
                                    VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL, true, false,
                                    &vk_entity.pipeline_gpu_bmodel_bloom_extract)) {
        Com_WPrintf("Vulkan entity: GPU inline-BSP bloom extraction unavailable.\n");
    }

#if USE_MD5
    if (vk_md5_gpu_skinning && vk_md5_gpu_skinning->integer) {
        const bool gpu_pipelines_ok =
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_OPAQUE, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD5,
                                     &vk_entity.pipeline_gpu_md5_opaque) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ALPHA, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD5,
                                     &vk_entity.pipeline_gpu_md5_alpha) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ADDITIVE, false,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD5,
                                     &vk_entity.pipeline_gpu_md5_additive) &&
            (!ctx->celshading_supported ||
             VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_CELSHADING, false,
                                      VK_ENTITY_STENCIL_NONE,
                                      VK_ENTITY_VERTEX_LAYOUT_GPU_MD5,
                                      &vk_entity.pipeline_gpu_md5_celshading)) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_OPAQUE, true,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD5,
                                     &vk_entity.pipeline_gpu_md5_depthhack_opaque) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ALPHA, true,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD5,
                                     &vk_entity.pipeline_gpu_md5_depthhack_alpha) &&
            VK_Entity_CreatePipeline(ctx, VK_ENTITY_BLEND_ADDITIVE, true,
                                     VK_ENTITY_STENCIL_NONE,
                                     VK_ENTITY_VERTEX_LAYOUT_GPU_MD5,
                                     &vk_entity.pipeline_gpu_md5_depthhack_additive);
        if (gpu_pipelines_ok) {
            vk_entity.gpu_md5_available = true;
        } else {
            Com_WPrintf("Vulkan entity: GPU MD5 pipelines unavailable; "
                        "using CPU skinning\n");
            VK_Entity_DestroyGpuMd5Pipelines();
        }
    }
    if (ctx->bloom_extract_render_pass && vk_entity.gpu_md5_available &&
        !VK_Entity_CreatePipelineEx(ctx, VK_ENTITY_BLEND_OPAQUE, false,
                                    VK_ENTITY_STENCIL_NONE,
                                    VK_ENTITY_VERTEX_LAYOUT_GPU_MD5, true, false,
                                    &vk_entity.pipeline_gpu_md5_bloom_extract)) {
        Com_WPrintf("Vulkan entity: GPU MD5 bloom extraction unavailable.\n");
    }
    if (ctx->bloom_extract_render_pass && vk_entity.gpu_md5_available &&
        !VK_Entity_CreatePipelineEx(ctx, VK_ENTITY_BLEND_ALPHA, false,
                                    VK_ENTITY_STENCIL_NONE,
                                    VK_ENTITY_VERTEX_LAYOUT_GPU_MD5, true, false,
                                    &vk_entity.pipeline_gpu_md5_bloom_extract_alpha)) {
        Com_WPrintf("Vulkan entity: GPU MD5 shell bloom extraction unavailable.\n");
    }
    if (ctx->bloom_extract_render_pass && vk_entity.gpu_md5_available &&
        !VK_Entity_CreatePipelineEx(ctx, VK_ENTITY_BLEND_ADDITIVE, false,
                                    VK_ENTITY_STENCIL_RIM_BLOOM_NO_DEPTH,
                                    VK_ENTITY_VERTEX_LAYOUT_GPU_MD5, true, false,
                                    &vk_entity.pipeline_gpu_md5_bloom_extract_additive)) {
        Com_WPrintf("Vulkan entity: GPU MD5 rim bloom extraction unavailable.\n");
    }
    if (ctx->bloom_rim_extract_render_pass && vk_entity.gpu_md5_available &&
        ctx->frames[0].bloom_depth_descriptor_set &&
        !VK_Entity_CreatePipelineEx(
            ctx, VK_ENTITY_BLEND_ADDITIVE, false,
            VK_ENTITY_STENCIL_RIM_BLOOM_NO_DEPTH,
            VK_ENTITY_VERTEX_LAYOUT_GPU_MD5, true, true,
            &vk_entity.pipeline_gpu_md5_bloom_extract_additive_depth_sample)) {
        Com_WPrintf("Vulkan entity: GPU MD5 sampled-depth rim bloom extraction unavailable.\n");
    }
#endif

    vk_entity.stencil_available =
        ctx->swapchain.depth_format == VK_FORMAT_D24_UNORM_S8_UINT ||
        ctx->swapchain.depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    if (vk_entity.stencil_available) {
        if (!VK_Entity_CreatePipeline(
                ctx, VK_ENTITY_BLEND_OPAQUE, false,
                VK_ENTITY_STENCIL_OUTLINE_MASK,
                VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                &vk_entity.pipeline_outline_mask) ||
            !VK_Entity_CreatePipeline(
                ctx, VK_ENTITY_BLEND_OPAQUE, false,
                VK_ENTITY_STENCIL_OUTLINE_MASK_NO_DEPTH,
                VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                &vk_entity.pipeline_outline_mask_no_depth) ||
            !VK_Entity_CreatePipeline(
                ctx, VK_ENTITY_BLEND_OPAQUE, false,
                VK_ENTITY_STENCIL_OUTLINE_SHELL,
                VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                &vk_entity.pipeline_outline_shell_opaque) ||
            !VK_Entity_CreatePipeline(
                ctx, VK_ENTITY_BLEND_ALPHA, false,
                VK_ENTITY_STENCIL_OUTLINE_SHELL,
                VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                &vk_entity.pipeline_outline_shell_alpha) ||
            !VK_Entity_CreatePipeline(
                ctx, VK_ENTITY_BLEND_OPAQUE, false,
                VK_ENTITY_STENCIL_OUTLINE_SHELL_NO_DEPTH,
                VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                &vk_entity.pipeline_outline_shell_no_depth_opaque) ||
            !VK_Entity_CreatePipeline(
                ctx, VK_ENTITY_BLEND_ALPHA, false,
                VK_ENTITY_STENCIL_OUTLINE_SHELL_NO_DEPTH,
                VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                &vk_entity.pipeline_outline_shell_no_depth_alpha) ||
            !VK_Entity_CreatePipeline(
                ctx, VK_ENTITY_BLEND_OPAQUE, false,
                VK_ENTITY_STENCIL_OUTLINE_CLEAR,
                VK_ENTITY_VERTEX_LAYOUT_DYNAMIC,
                &vk_entity.pipeline_outline_clear)) {
            VK_Entity_DestroySwapchainResources(ctx);
            return false;
        }
    } else {
        Com_WPrintf("Vulkan: stencil-capable depth format unavailable; "
                    "alias-model outlines disabled\n");
    }
    vk_entity.swapchain_ready = true;
    return true;
}

void VK_Entity_Shutdown(vk_context_t *ctx)
{
    if (!vk_entity.initialized) {
        return;
    }
    if (!ctx) {
        ctx = vk_entity.ctx;
    }
    if (ctx && ctx->device) {
        vkDeviceWaitIdle(ctx->device);
    }

    VK_Entity_DestroySwapchainResources(ctx);
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_Entity_DestroyGeometryBuffer(&vk_entity.frame_buffers[i]);
        VK_Entity_DestroyInstanceBuffer(&vk_entity.frame_buffers[i]);
    }
    VK_Entity_FreeAllModels();
    VK_Entity_ClearBspTextureCache();

    if (vk_entity.white_image) {
        VK_UI_UnregisterImage(vk_entity.white_image);
    }
    VK_Entity_DestroyParticleTexture();
    if (vk_entity.beam_image) {
        VK_UI_UnregisterImage(vk_entity.beam_image);
    }

    if (ctx && ctx->device && vk_entity.pipeline_layout) {
        vkDestroyPipelineLayout(ctx->device, vk_entity.pipeline_layout, NULL);
    }
#if USE_MD5
    VK_Entity_DestroyBuffer(&vk_entity.gpu_md5_weight_buffer,
                            &vk_entity.gpu_md5_weight_memory, NULL);
    if (ctx && ctx->device && vk_entity.gpu_md5_descriptor_pool) {
        vkDestroyDescriptorPool(ctx->device, vk_entity.gpu_md5_descriptor_pool,
                                NULL);
    }
    if (ctx && ctx->device && vk_entity.gpu_md5_set_layout) {
        vkDestroyDescriptorSetLayout(ctx->device, vk_entity.gpu_md5_set_layout,
                                     NULL);
    }
#endif
    if (ctx && ctx->device && vk_entity.flare_query_pool) {
        vkDestroyQueryPool(ctx->device, vk_entity.flare_query_pool, NULL);
    }

    free(vk_entity.vertices);
    free(vk_entity.indices);
    free(vk_entity.md2_instances);
    free(vk_entity.bmodel_instances);
    free(vk_entity.particle_instances);
    free(vk_entity.beam_instances);
#if USE_MD5
    free(vk_entity.md5_instances);
    free(vk_entity.md5_joints);
    free(vk_entity.md5_weights);
#endif
    free(vk_entity.batches);
#if USE_MD5
    free(vk_entity.temp_skeleton);
    free(vk_entity.temp_md5_vertices);
#endif
    memset(&vk_entity, 0, sizeof(vk_entity));
}

void VK_Entity_BeginRegistration(void)
{
    if (!vk_entity.initialized) {
        return;
    }
    vk_entity.registration_sequence++;
    if (vk_entity.registration_sequence <= 0) {
        vk_entity.registration_sequence = 1;
    }
}

void VK_Entity_EndRegistration(void)
{
    if (!vk_entity.initialized) {
        return;
    }

    for (int i = 0; i < vk_entity.num_models; i++) {
        vk_model_t *model = &vk_entity.models[i];
        if (!model->type) {
            continue;
        }
        if (model->registration_sequence == vk_entity.registration_sequence) {
            continue;
        }
        VK_Entity_FreeModel(model);
    }
}

qhandle_t VK_Entity_RegisterModel(const char *name)
{
    if (!vk_entity.initialized || !name || !*name) {
        return 0;
    }

    if (*name == '*') {
        return ~Q_atoi(name + 1);
    }

    char normalized[MAX_QPATH];
    size_t namelen = FS_NormalizePathBuffer(normalized, name, sizeof(normalized));
    if (namelen == 0 || namelen >= sizeof(normalized)) {
        return 0;
    }

    for (int i = 0; i < vk_entity.num_models; i++) {
        vk_model_t *model = &vk_entity.models[i];
        if (!model->type) {
            continue;
        }
        if (!FS_pathcmp(model->name, normalized)) {
            model->registration_sequence = vk_entity.registration_sequence;
            return (qhandle_t)(i + 1);
        }
    }

    byte *raw = NULL;
    int filelen = FS_LoadFile(normalized, (void **)&raw);
    if (!raw) {
        if (filelen != Q_ERR(ENOENT)) {
            Com_EPrintf("Couldn't load %s: %s\n", normalized, Q_ErrorString(filelen));
        }
        return 0;
    }

    vk_model_t *slot = NULL;
    for (int i = 0; i < vk_entity.num_models; i++) {
        if (!vk_entity.models[i].type) {
            slot = &vk_entity.models[i];
            break;
        }
    }
    if (!slot) {
        if (vk_entity.num_models >= MAX_MODELS) {
            FS_FreeFile(raw);
            return 0;
        }
        slot = &vk_entity.models[vk_entity.num_models++];
    }

    memset(slot, 0, sizeof(*slot));
    Q_strlcpy(slot->name, normalized, sizeof(slot->name));
    slot->registration_sequence = vk_entity.registration_sequence;

    bool loaded = false;
    if (filelen >= 4) {
        uint32_t ident = LittleLong(*(uint32_t *)raw);
        if (ident == SP2_IDENT) {
            loaded = VK_Entity_LoadSP2(slot, raw, (size_t)filelen);
        } else if (ident == MD2_IDENT) {
            loaded = VK_Entity_LoadMD2(slot, raw, (size_t)filelen);
        }
    }

    FS_FreeFile(raw);
    if (!loaded) {
        VK_Entity_FreeModel(slot);
        return 0;
    }

    if (slot->type == VK_MODEL_MD2 &&
        !VK_Entity_CreateMd2GpuResources(&slot->md2)) {
        // Static GPU residency is an optimization. Keep the established CPU
        // stream as a functional fallback if a driver cannot allocate it. The
        // exact Vulkan result matters here: otherwise a startup-wide residency
        // failure silently turns the intended GPU path into its CPU fallback.
        const char *error = Com_GetLastError();
        Com_WPrintf("Vulkan entity: GPU MD2 residency unavailable for %s "
                    "(%s); using CPU interpolation\n", slot->name,
                    error && *error ? error : "unknown failure");
    }

#if USE_MD5
    if (slot->type == VK_MODEL_MD2) {
        VK_Entity_LoadMD5Replacement(slot);
        if (slot->md5.loaded &&
            !VK_Entity_CreateMd5GpuResources(&slot->md5)) {
            const char *error = Com_GetLastError();
            Com_WPrintf("Vulkan entity: GPU MD5 residency unavailable for %s "
                        "(%s); using CPU skinning\n", slot->name,
                        error && *error ? error : "unknown failure");
        }
    }
#endif

    return (qhandle_t)((slot - vk_entity.models) + 1);
}

void VK_Entity_RenderFrame(const refdef_t *fd)
{
    vk_entity_frame_buffer_t *frame = VK_Entity_CurrentFrameBuffer();
    if (frame) {
        frame->vertex_upload_bytes = 0;
        frame->index_upload_bytes = 0;
        frame->md2_instance_upload_bytes = 0;
        frame->bmodel_instance_upload_bytes = 0;
        frame->particle_instance_upload_bytes = 0;
        frame->beam_instance_upload_bytes = 0;
#if USE_MD5
        frame->md5_instance_upload_bytes = 0;
        frame->md5_palette_upload_bytes = 0;
#endif
    }
    vk_entity.frame_active = false;
    vk_entity.frame_weapon_active = false;
    vk_entity.frame_has_depth_hack_batches = false;
    vk_entity.frame_has_outline_batches = false;
    vk_entity.frame_has_flare_queries = false;
    vk_entity.frame_has_flares = false;
    vk_entity.frame_submit_phase_mask = 0;
    memset(vk_entity.frame_submit_pass_masks, 0,
           sizeof(vk_entity.frame_submit_pass_masks));
    vk_entity.current_submit_phase = VK_ENTITY_SUBMIT_OPAQUE;
    vk_entity.showtris_category = 0;
    vk_entity.vertex_count = 0;
    vk_entity.index_count = 0;
    vk_entity.md2_instance_count = 0;
    vk_entity.bmodel_instance_count = 0;
    vk_entity.particle_instance_count = 0;
    vk_entity.beam_instance_count = 0;
#if USE_MD5
    vk_entity.md5_instance_count = 0;
    vk_entity.md5_joint_count = 0;
#endif
    vk_entity.batch_count = 0;
    memset(vk_entity.flare_queries_scheduled, 0,
           sizeof(vk_entity.flare_queries_scheduled));
    memset(&vk_entity.frame_push, 0, sizeof(vk_entity.frame_push));
    memset(&vk_entity.frame_push_weapon, 0, sizeof(vk_entity.frame_push_weapon));
    memset(&vk_entity.frame_view_rect, 0, sizeof(vk_entity.frame_view_rect));
    vk_entity.frame_uses_view_rect = false;

    if (!vk_entity.initialized || !fd || !vk_drawentities || !vk_drawentities->integer) {
        return;
    }

    vec3_t view_axis[3];
    AnglesToAxis(fd->viewangles, view_axis);
    vk_entity_cull_frustum_t cull_frustum;
    const vk_entity_cull_frustum_t *active_cull_frustum =
        vk_cull_models && vk_cull_models->integer &&
        VK_Entity_BuildCullFrustum(fd, &cull_frustum) ? &cull_frustum : NULL;
    const bsp_t *world_bsp = VK_World_GetBsp();
    if (world_bsp != vk_entity.bmodel_texture_bsp) {
        VK_Entity_ClearBspTextureCache();
    }

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *ent = &fd->entities[i];
        if (ent->flags & RF_VIEWERMODEL) {
            continue;
        }
        if (ent->flags & RF_FLARE) {
            continue;
        }
        bool depth_hack = (ent->flags & (RF_DEPTHHACK | RF_WEAPONMODEL)) != 0;
        bool weapon_model = (ent->flags & RF_WEAPONMODEL) != 0;
        vk_entity.frame_weapon_active |= weapon_model;
        vk_entity.frame_has_depth_hack_batches |= depth_hack;

        if (ent->flags & RF_BEAM) {
            vk_entity.showtris_category = VK_DEBUG_SHOWTRIS_FX;
            vk_entity.current_submit_phase = VK_ENTITY_SUBMIT_POST_LIQUID;
            if (!VK_Entity_AddBeam(ent, fd)) {
                return;
            }
            continue;
        }

        if (ent->model & BIT(31)) {
            const int model_index = ~ent->model;
            if (world_bsp && world_bsp->models && model_index >= 1 &&
                model_index < world_bsp->nummodels) {
                const mmodel_t *model = &world_bsp->models[model_index];
                if (VK_Entity_CullModelBounds(active_cull_frustum, ent, model->mins,
                                              model->maxs)) {
                    VK_Debug_RecordEntityModelCull();
                    continue;
                }
            }
            vk_entity.showtris_category = VK_DEBUG_SHOWTRIS_WORLD;
            vk_entity.current_submit_phase = VK_ENTITY_SUBMIT_OPAQUE;
            if (!VK_Entity_AddBspModel(ent, fd, world_bsp, depth_hack, weapon_model)) {
                return;
            }
            continue;
        }

        if (ent->model <= 0 || ent->model > vk_entity.num_models) {
            continue;
        }

        const vk_model_t *model = &vk_entity.models[ent->model - 1];
        if (!model->type) {
            continue;
        }
        if (vk_showorigins && vk_showorigins->integer) {
            VK_Entity_AddOriginAxes(ent);
        }

        // GL retains show-origin axes even when the matching model is
        // culled, so perform this after the diagnostic above. Sprites keep
        // their billboard-specific path; only MD2/MD5 source bounds are
        // cached by VK_Entity_ModelBounds.
        if (model->type == VK_MODEL_MD2) {
            vec3_t local_mins;
            vec3_t local_maxs;
            if (VK_Entity_ModelBoundsForRefdef(model, ent, fd, local_mins,
                                               local_maxs) &&
                VK_Entity_CullModelBounds(active_cull_frustum, ent, local_mins,
                                          local_maxs)) {
                VK_Debug_RecordEntityModelCull();
                continue;
            }
        }

        if (!(ent->flags & RF_TRANSLUCENT)) {
            vk_entity.current_submit_phase = VK_ENTITY_SUBMIT_OPAQUE;
        } else if (weapon_model ||
                   ent->alpha <= (vk_draworder ? vk_draworder->value : 1.0f)) {
            vk_entity.current_submit_phase = VK_ENTITY_SUBMIT_ALPHA_FRONT;
        } else {
            vk_entity.current_submit_phase = VK_ENTITY_SUBMIT_ALPHA_BACK;
        }

        if (model->type == VK_MODEL_SPRITE) {
            // GL renders sprites as a separate triangle-strip path and does
            // not include them in gl_showtris mesh output.
            vk_entity.showtris_category = 0;
            if (!VK_Entity_AddSprite(ent, view_axis, model, depth_hack, weapon_model)) {
                return;
            }
        } else if (model->type == VK_MODEL_MD2) {
            vk_entity.showtris_category = VK_DEBUG_SHOWTRIS_MESH;
#if USE_MD5
            if (VK_Entity_ShouldUseMD5(ent, fd, model)) {
                const bool rendered = VK_Entity_ShouldUseGpuMD5(ent, model)
                    ? VK_Entity_AddGpuMD5(ent, fd, model, depth_hack,
                                           weapon_model)
                    : VK_Entity_AddMD5(ent, fd, model, depth_hack,
                                       weapon_model);
                if (!rendered) {
                    return;
                }
                continue;
            }
#endif
            if (VK_Entity_ShouldUseGpuMD2(ent, model)) {
                if (!VK_Entity_AddGpuMD2(ent, fd, model, depth_hack,
                                          weapon_model)) {
                    return;
                }
                continue;
            }
            if (!VK_Entity_AddMD2(ent, fd, model, depth_hack, weapon_model)) {
                return;
            }
        }
    }

    vk_entity.current_submit_phase = VK_ENTITY_SUBMIT_POST_LIQUID;
    vk_entity.showtris_category = VK_DEBUG_SHOWTRIS_FX;
    if (!VK_Entity_AddParticles(fd, view_axis)) {
        return;
    }
    if (!VK_Entity_AddFlares(fd, view_axis, world_bsp)) {
        return;
    }

    VK_Entity_CoalesceGpuBmodelBatches();

    if (!vk_entity.vertex_count && !vk_entity.md2_instance_count &&
        !vk_entity.bmodel_instance_count && !vk_entity.particle_instance_count &&
        !vk_entity.beam_instance_count
#if USE_MD5
        && !vk_entity.md5_instance_count
#endif
        ) {
        return;
    }

    size_t vertex_bytes = 0;
    size_t index_bytes = 0;
    if ((vk_entity.vertex_count &&
         !VK_Entity_ArrayBytes((size_t)vk_entity.vertex_count,
                               sizeof(*vk_entity.vertices), &vertex_bytes,
                               "frame entity vertices")) ||
        (vk_entity.index_count &&
         !VK_Entity_ArrayBytes((size_t)vk_entity.index_count,
                               sizeof(*vk_entity.indices), &index_bytes,
                               "frame entity indices")) ||
        !VK_Entity_EnsureGeometryBuffer(frame, vertex_bytes, index_bytes)) {
        return;
    }
    if ((vertex_bytes || index_bytes) && !frame->geometry_mapped) {
        Com_SetLastError("Vulkan entity: geometry arena is not mapped");
        return;
    }
    if (vertex_bytes) {
        memcpy(frame->geometry_mapped, vk_entity.vertices, vertex_bytes);
        frame->vertex_upload_bytes = vertex_bytes;
        VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY, vertex_bytes);
    }
    if (index_bytes) {
        memcpy((byte *)frame->geometry_mapped + frame->geometry_index_offset,
               vk_entity.indices, index_bytes);
        frame->index_upload_bytes = index_bytes;
        VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY, index_bytes);
    }

    size_t md2_instance_bytes = 0;
    size_t bmodel_instance_bytes = 0;
    size_t particle_instance_bytes = 0;
    size_t beam_instance_bytes = 0;
#if USE_MD5
    size_t md5_instance_bytes = 0;
    size_t md5_palette_bytes = 0;
#endif
    if ((vk_entity.md2_instance_count &&
         !VK_Entity_ArrayBytes(vk_entity.md2_instance_count,
                               sizeof(*vk_entity.md2_instances),
                               &md2_instance_bytes, "GPU MD2 instances")) ||
        (vk_entity.bmodel_instance_count &&
         !VK_Entity_ArrayBytes(vk_entity.bmodel_instance_count,
                               sizeof(*vk_entity.bmodel_instances),
                               &bmodel_instance_bytes, "GPU BSP instances"))
        || (vk_entity.particle_instance_count &&
            !VK_Entity_ArrayBytes(vk_entity.particle_instance_count,
                                  sizeof(*vk_entity.particle_instances),
                                  &particle_instance_bytes,
                                  "GPU particle instances"))
        || (vk_entity.beam_instance_count &&
            !VK_Entity_ArrayBytes(vk_entity.beam_instance_count,
                                  sizeof(*vk_entity.beam_instances),
                                  &beam_instance_bytes,
                                  "GPU beam instances"))
#if USE_MD5
        || (vk_entity.md5_instance_count &&
            (!vk_entity.md5_joint_count ||
             !VK_Entity_ArrayBytes(vk_entity.md5_instance_count,
                                   sizeof(*vk_entity.md5_instances),
                                   &md5_instance_bytes, "GPU MD5 instances") ||
             !VK_Entity_ArrayBytes(vk_entity.md5_joint_count,
                                   sizeof(*vk_entity.md5_joints),
                                   &md5_palette_bytes,
                                   "GPU MD5 joint palette")))
#endif
    ) {
        return;
    }
    if ((md2_instance_bytes || bmodel_instance_bytes || particle_instance_bytes ||
         beam_instance_bytes
#if USE_MD5
         || md5_instance_bytes || md5_palette_bytes
#endif
        ) &&
        !VK_Entity_EnsureInstanceBuffer(frame, md2_instance_bytes,
                                        bmodel_instance_bytes,
                                        particle_instance_bytes,
                                        beam_instance_bytes
#if USE_MD5
                                        , md5_instance_bytes, md5_palette_bytes
#endif
        )) {
        return;
    }
    if ((md2_instance_bytes || bmodel_instance_bytes || particle_instance_bytes ||
         beam_instance_bytes
#if USE_MD5
         || md5_instance_bytes || md5_palette_bytes
#endif
        ) && !frame->instance_mapped) {
        Com_SetLastError("Vulkan entity: instance arena is not mapped");
        return;
    }
    if (md2_instance_bytes) {
        memcpy((byte *)frame->instance_mapped + frame->md2_instance_offset,
               vk_entity.md2_instances, md2_instance_bytes);
        frame->md2_instance_upload_bytes = md2_instance_bytes;
        VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY, md2_instance_bytes);
    }
    if (bmodel_instance_bytes) {
        memcpy((byte *)frame->instance_mapped + frame->bmodel_instance_offset,
               vk_entity.bmodel_instances, bmodel_instance_bytes);
        frame->bmodel_instance_upload_bytes = bmodel_instance_bytes;
        VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY, bmodel_instance_bytes);
    }
    if (particle_instance_bytes) {
        memcpy((byte *)frame->instance_mapped + frame->particle_instance_offset,
               vk_entity.particle_instances, particle_instance_bytes);
        frame->particle_instance_upload_bytes = particle_instance_bytes;
        VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY, particle_instance_bytes);
    }
    if (beam_instance_bytes) {
        memcpy((byte *)frame->instance_mapped + frame->beam_instance_offset,
               vk_entity.beam_instances, beam_instance_bytes);
        frame->beam_instance_upload_bytes = beam_instance_bytes;
        VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY, beam_instance_bytes);
    }
#if USE_MD5
    if (md5_instance_bytes) {
        memcpy((byte *)frame->instance_mapped + frame->md5_instance_offset,
               vk_entity.md5_instances, md5_instance_bytes);
        memcpy((byte *)frame->instance_mapped + frame->md5_palette_offset,
               vk_entity.md5_joints, md5_palette_bytes);
        frame->md5_instance_upload_bytes = md5_instance_bytes;
        frame->md5_palette_upload_bytes = md5_palette_bytes;
        VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY,
                              md5_instance_bytes + md5_palette_bytes);
        VK_Entity_UpdateMd5DescriptorSets();
    }
#endif

    VK_Entity_BuildFramePush(fd, world_bsp, &vk_entity.frame_push);
    if (vk_entity.frame_weapon_active) {
        VK_Entity_BuildWeaponFramePush(fd, world_bsp, &vk_entity.frame_push_weapon);
    }

    // Deferred Vulkan command recording happens after R_RenderFrame returns.
    // Preserve the no-world refdef rectangle used by player-configuration
    // previews so their entity pass is rasterized into the authored menu
    // surface rather than centred across the whole swapchain behind the UI.
    if ((fd->rdflags & RDF_NOWORLDMODEL) && fd->width > 0 && fd->height > 0) {
        vk_entity.frame_view_rect.offset.x = fd->x;
        vk_entity.frame_view_rect.offset.y = fd->y;
        vk_entity.frame_view_rect.extent.width = (uint32_t)fd->width;
        vk_entity.frame_view_rect.extent.height = (uint32_t)fd->height;
        vk_entity.frame_uses_view_rect = true;
    }
    vk_entity.frame_active = true;
}

bool VK_Entity_IsNoWorldSubview(void)
{
    return vk_entity.frame_active && vk_entity.frame_uses_view_rect;
}

void VK_Entity_RecordUploads(VkCommandBuffer cmd)
{
    vk_entity_frame_buffer_t *frame = VK_Entity_CurrentFrameBuffer();
    if (!cmd || !vk_entity.initialized || !vk_entity.frame_active || !frame) {
        return;
    }

    VkBufferMemoryBarrier barriers[2] = { 0 };
    uint32_t barrier_count = 0;
    VkPipelineStageFlags destination_stages = 0;
    if ((frame->vertex_upload_bytes || frame->index_upload_bytes) &&
        frame->geometry_staging_buffer && frame->geometry_buffer) {
        VkBufferCopy copies[2] = { 0 };
        uint32_t copy_count = 0;
        size_t geometry_bytes = 0;
        if (frame->vertex_upload_bytes) {
            copies[copy_count++].size = frame->vertex_upload_bytes;
            geometry_bytes = frame->vertex_upload_bytes;
        }
        if (frame->index_upload_bytes) {
            copies[copy_count++] = (VkBufferCopy) {
                .srcOffset = frame->geometry_index_offset,
                .dstOffset = frame->geometry_index_offset,
                .size = frame->index_upload_bytes,
            };
            geometry_bytes = frame->geometry_index_offset +
                             frame->index_upload_bytes;
        }
        vkCmdCopyBuffer(cmd, frame->geometry_staging_buffer,
                        frame->geometry_buffer, copy_count, copies);
        barriers[barrier_count++] = (VkBufferMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                             VK_ACCESS_INDEX_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = frame->geometry_buffer,
            .size = geometry_bytes,
        };
        destination_stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    }
    if (frame->instance_buffer && frame->instance_staging_buffer) {
        VkBufferCopy copies[6] = { 0 };
        uint32_t copy_count = 0;
        size_t instance_bytes = 0;
        VkAccessFlags instance_access = 0;
        VkPipelineStageFlags instance_stages = 0;
        if (frame->md2_instance_upload_bytes) {
            copies[copy_count++] = (VkBufferCopy) {
                .srcOffset = frame->md2_instance_offset,
                .dstOffset = frame->md2_instance_offset,
                .size = frame->md2_instance_upload_bytes,
            };
            instance_bytes = frame->md2_instance_offset +
                             frame->md2_instance_upload_bytes;
            instance_access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            instance_stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        }
        if (frame->bmodel_instance_upload_bytes) {
            copies[copy_count++] = (VkBufferCopy) {
                .srcOffset = frame->bmodel_instance_offset,
                .dstOffset = frame->bmodel_instance_offset,
                .size = frame->bmodel_instance_upload_bytes,
            };
            instance_bytes = frame->bmodel_instance_offset +
                             frame->bmodel_instance_upload_bytes;
            instance_access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            instance_stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        }
        if (frame->particle_instance_upload_bytes) {
            copies[copy_count++] = (VkBufferCopy) {
                .srcOffset = frame->particle_instance_offset,
                .dstOffset = frame->particle_instance_offset,
                .size = frame->particle_instance_upload_bytes,
            };
            instance_bytes = frame->particle_instance_offset +
                             frame->particle_instance_upload_bytes;
            instance_access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            instance_stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        }
        if (frame->beam_instance_upload_bytes) {
            copies[copy_count++] = (VkBufferCopy) {
                .srcOffset = frame->beam_instance_offset,
                .dstOffset = frame->beam_instance_offset,
                .size = frame->beam_instance_upload_bytes,
            };
            instance_bytes = frame->beam_instance_offset +
                             frame->beam_instance_upload_bytes;
            instance_access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            instance_stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        }
#if USE_MD5
        if (frame->md5_instance_upload_bytes) {
            copies[copy_count++] = (VkBufferCopy) {
                .srcOffset = frame->md5_instance_offset,
                .dstOffset = frame->md5_instance_offset,
                .size = frame->md5_instance_upload_bytes,
            };
            instance_bytes = frame->md5_instance_offset +
                             frame->md5_instance_upload_bytes;
            instance_access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            instance_stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        }
        if (frame->md5_palette_upload_bytes) {
            copies[copy_count++] = (VkBufferCopy) {
                .srcOffset = frame->md5_palette_offset,
                .dstOffset = frame->md5_palette_offset,
                .size = frame->md5_palette_upload_bytes,
            };
            instance_bytes = frame->md5_palette_offset +
                             frame->md5_palette_upload_bytes;
            instance_access |= VK_ACCESS_SHADER_READ_BIT;
            instance_stages |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        }
#endif
        if (copy_count) {
            vkCmdCopyBuffer(cmd, frame->instance_staging_buffer,
                            frame->instance_buffer, copy_count, copies);
            barriers[barrier_count++] = (VkBufferMemoryBarrier) {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = instance_access,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = frame->instance_buffer,
                .size = instance_bytes,
            };
            destination_stages |= instance_stages;
        }
    }
    if (barrier_count) {
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             destination_stages, 0,
                             0, NULL, barrier_count, barriers, 0, NULL);
    }
}

void VK_Entity_ResetFlareQueries(VkCommandBuffer cmd)
{
    if (!cmd || !vk_entity.initialized || !vk_entity.frame_active ||
        !vk_entity.frame_has_flare_queries || !vk_entity.flare_query_pool) {
        return;
    }

    uint32_t first = 0;
    while (first < MAX_EDICTS) {
        while (first < MAX_EDICTS &&
               !vk_entity.flare_queries_scheduled[first]) {
            first++;
        }
        if (first >= MAX_EDICTS) {
            break;
        }

        uint32_t end = first + 1;
        while (end < MAX_EDICTS &&
               vk_entity.flare_queries_scheduled[end]) {
            end++;
        }
        vkCmdResetQueryPool(cmd, vk_entity.flare_query_pool, first, end - first);
        first = end;
    }
}

static bool VK_Entity_BatchMatchesRecordPhase(const vk_batch_t *batch,
                                              vk_entity_record_phase_t phase)
{
    if (!batch || phase == VK_ENTITY_RECORD_ALL) {
        return batch != NULL;
    }
    if (phase == VK_ENTITY_RECORD_BEFORE_LIQUID) {
        return batch->submit_phase == VK_ENTITY_SUBMIT_OPAQUE ||
               batch->submit_phase == VK_ENTITY_SUBMIT_ALPHA_BACK;
    }
    if (phase == VK_ENTITY_RECORD_POST_LIQUID) {
        return batch->submit_phase == VK_ENTITY_SUBMIT_POST_LIQUID;
    }
    return batch->submit_phase == VK_ENTITY_SUBMIT_ALPHA_FRONT;
}

static uint32_t VK_Entity_BatchVertexFlags(const vk_batch_t *batch)
{
    if (!batch) {
        return 0;
    }
    if (batch->gpu_md2 || batch->gpu_bmodel || batch->gpu_particle ||
        batch->gpu_beam) {
        return batch->vertex_flags;
    }
#if USE_MD5
    if (batch->gpu_md5) {
        return batch->vertex_flags;
    }
#endif
    return batch->first_vertex < vk_entity.vertex_count
        ? vk_entity.vertices[batch->first_vertex].flags : 0;
}

static void VK_Entity_BindDynamicBuffers(VkCommandBuffer cmd,
                                         const vk_entity_frame_buffer_t *frame)
{
    if (!cmd || !frame || !frame->geometry_buffer) {
        return;
    }
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->geometry_buffer, &offset);
    if (frame->geometry_index_capacity) {
        vkCmdBindIndexBuffer(cmd, frame->geometry_buffer,
                             frame->geometry_index_offset,
                             VK_INDEX_TYPE_UINT16);
    }
}

static bool VK_Entity_BindGpuMd2Batch(VkCommandBuffer cmd,
                                      const vk_entity_frame_buffer_t *frame,
                                      const vk_batch_t *batch)
{
    if (!cmd || !frame || !frame->instance_buffer || !batch ||
        !batch->gpu_md2 || !batch->gpu_md2_model) {
        return false;
    }
    const vk_md2_t *md2 = batch->gpu_md2_model;
    if (!md2->gpu_ready || !md2->gpu_frame_buffer || !md2->gpu_uv_buffer ||
        !md2->gpu_index_buffer || batch->gpu_md2_frame >= md2->num_frames ||
        batch->gpu_md2_oldframe >= md2->num_frames ||
        batch->first_instance > vk_entity.md2_instance_count ||
        batch->instance_count >
            vk_entity.md2_instance_count - batch->first_instance) {
        return false;
    }

    size_t frame_bytes = 0;
    size_t frame_offset = 0;
    size_t oldframe_offset = 0;
    if (!VK_Entity_ArrayBytes(md2->num_vertices,
                              sizeof(vk_md2_gpu_frame_vertex_t),
                              &frame_bytes, "GPU MD2 frame stride") ||
        !VK_Entity_ArrayBytes(batch->gpu_md2_frame, frame_bytes,
                              &frame_offset, "GPU MD2 frame offset") ||
        !VK_Entity_ArrayBytes(batch->gpu_md2_oldframe, frame_bytes,
                              &oldframe_offset, "GPU MD2 old-frame offset")) {
        return false;
    }

    const VkBuffer buffers[4] = {
        md2->gpu_frame_buffer,
        md2->gpu_frame_buffer,
        md2->gpu_uv_buffer,
        frame->instance_buffer,
    };
    const VkDeviceSize offsets[4] = {
        frame_offset,
        oldframe_offset,
        0,
        frame->md2_instance_offset,
    };
    vkCmdBindVertexBuffers(cmd, 0, q_countof(buffers), buffers, offsets);
    vkCmdBindIndexBuffer(cmd, md2->gpu_index_buffer, 0, VK_INDEX_TYPE_UINT16);
    return true;
}

static bool VK_Entity_BindGpuBmodelBatch(
    VkCommandBuffer cmd, const vk_entity_frame_buffer_t *frame,
    const vk_batch_t *batch)
{
    if (!cmd || !frame || !batch || !batch->gpu_bmodel ||
        !vk_entity.bmodel_gpu_ready || !vk_entity.bmodel_gpu_vertex_buffer ||
        !frame->instance_buffer ||
        batch->first_vertex > vk_entity.bmodel_gpu_vertex_count ||
        batch->vertex_count >
            vk_entity.bmodel_gpu_vertex_count - batch->first_vertex ||
        batch->first_instance >= vk_entity.bmodel_instance_count ||
        batch->instance_count >
            vk_entity.bmodel_instance_count - batch->first_instance) {
        return false;
    }
    const VkBuffer buffers[2] = {
        vk_entity.bmodel_gpu_vertex_buffer,
        frame->instance_buffer,
    };
    const VkDeviceSize offsets[2] = { 0, frame->bmodel_instance_offset };
    vkCmdBindVertexBuffers(cmd, 0, q_countof(buffers), buffers, offsets);
    return true;
}

static bool VK_Entity_BindGpuParticleBatch(
    VkCommandBuffer cmd, const vk_entity_frame_buffer_t *frame,
    const vk_batch_t *batch)
{
    if (!cmd || !frame || !batch || !batch->gpu_particle ||
        !frame->instance_buffer || !frame->particle_instance_capacity ||
        batch->first_instance >= vk_entity.particle_instance_count ||
        batch->instance_count >
            vk_entity.particle_instance_count - batch->first_instance) {
        return false;
    }
    const VkDeviceSize offset = frame->particle_instance_offset;
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->instance_buffer, &offset);
    return true;
}

static bool VK_Entity_BindGpuBeamBatch(
    VkCommandBuffer cmd, const vk_entity_frame_buffer_t *frame,
    const vk_batch_t *batch)
{
    if (!cmd || !frame || !batch || !batch->gpu_beam ||
        !frame->instance_buffer || !frame->beam_instance_capacity ||
        batch->first_instance >= vk_entity.beam_instance_count ||
        batch->instance_count >
            vk_entity.beam_instance_count - batch->first_instance) {
        return false;
    }
    const VkDeviceSize offset = frame->beam_instance_offset;
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->instance_buffer, &offset);
    return true;
}

static inline bool VK_Entity_BmodelBindingCacheEnabled(void)
{
    return !vk_bmodel_binding_cache || vk_bmodel_binding_cache->integer;
}

#if USE_MD5
static bool VK_Entity_BindGpuMd5Batch(VkCommandBuffer cmd,
                                      const vk_entity_frame_buffer_t *frame,
                                      const vk_batch_t *batch)
{
    if (!cmd || !frame || !batch || !batch->gpu_md5 ||
        !batch->gpu_md5_mesh || !frame->instance_buffer ||
        !frame->md5_palette_capacity || !vk_entity.gpu_md5_weight_buffer ||
        !vk_entity.ctx ||
        vk_entity.ctx->current_frame >= VK_MAX_FRAMES_IN_FLIGHT) {
        return false;
    }
    const vk_md5_mesh_t *mesh = batch->gpu_md5_mesh;
    const VkDescriptorSet md5_set =
        vk_entity.gpu_md5_descriptor_sets[vk_entity.ctx->current_frame];
    if (!mesh->gpu_ready || !mesh->gpu_vertex_buffer ||
        !mesh->gpu_index_buffer || !md5_set ||
        batch->first_instance > vk_entity.md5_instance_count ||
        batch->instance_count >
            vk_entity.md5_instance_count - batch->first_instance) {
        return false;
    }
    const VkBuffer buffers[2] = {
        mesh->gpu_vertex_buffer,
        frame->instance_buffer,
    };
    const VkDeviceSize offsets[2] = { 0, frame->md5_instance_offset };
    vkCmdBindVertexBuffers(cmd, 0, q_countof(buffers), buffers, offsets);
    vkCmdBindIndexBuffer(cmd, mesh->gpu_index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_entity.pipeline_layout, 3, 1, &md5_set, 0,
                            NULL);
    return true;
}
#endif

static void VK_Entity_DrawBatch(VkCommandBuffer cmd, const vk_batch_t *batch)
{
    if (!cmd || !batch) {
        return;
    }
    if (batch->indexed) {
        if (batch->gpu_md2
#if USE_MD5
            || batch->gpu_md5
#endif
            ) {
            vkCmdDrawIndexed(cmd, batch->index_count, batch->instance_count,
                             0, 0, batch->first_instance);
        } else {
            vkCmdDrawIndexed(cmd, batch->index_count, 1, batch->first_index,
                             (int32_t)batch->first_vertex, 0);
        }
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_ENTITY,
                            (batch->gpu_md2
#if USE_MD5
                             || batch->gpu_md5
#endif
                             )
                                ? (uint32_t)min((uint64_t)UINT32_MAX,
                                                (uint64_t)batch->vertex_count *
                                                    batch->instance_count)
                                : batch->vertex_count,
                            (batch->gpu_md2
#if USE_MD5
                             || batch->gpu_md5
#endif
                             )
                                ? (uint32_t)min((uint64_t)UINT32_MAX,
                                                (uint64_t)batch->index_count *
                                                    batch->instance_count)
                                : batch->index_count);
#if USE_MD5
        if (batch->gpu_md5) {
            VK_Debug_RecordEntityGpuMd5Draw(batch->instance_count);
        }
#endif
    } else {
        const bool instanced = batch->gpu_bmodel || batch->gpu_particle ||
            batch->gpu_beam;
        vkCmdDraw(cmd, batch->vertex_count,
                  instanced ? batch->instance_count : 1,
                  batch->first_vertex,
                  instanced ? batch->first_instance : 0);
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_ENTITY,
                            instanced
                                ? (uint32_t)min((uint64_t)UINT32_MAX,
                                                (uint64_t)batch->vertex_count *
                                                    batch->instance_count)
                                : batch->vertex_count,
                            0);
    }
}

// Replay only the ordinary alias silhouette after opaque model work, matching
// OpenGL's draw_celshading() ordering. The black fragment module ignores the
// skin, lightmap, and shadow receivers; descriptors and GPU-resident model
// buffers are still reused directly from the normal submission path.
static void VK_Entity_RecordCelShadingPass(
    VkCommandBuffer cmd, const vk_entity_frame_buffer_t *frame,
    vk_entity_record_phase_t phase, bool depth_hack,
    bool *dynamic_buffers_bound)
{
    if (!cmd || !frame || !vk_celshading || vk_celshading->value <= 0.0f) {
        return;
    }

    VkPipeline bound_pipeline = VK_NULL_HANDLE;
    VkDescriptorSet last_set = VK_NULL_HANDLE;
    float last_line_width = -1.0f;
    bool using_weapon_push = false;

    for (uint32_t i = 0; i < vk_entity.batch_count; i++) {
        const vk_batch_t *batch = &vk_entity.batches[i];
        const uint32_t vertex_flags = VK_Entity_BatchVertexFlags(batch);
        if (!VK_Entity_BatchMatchesRecordPhase(batch, phase) ||
            !batch->vertex_count || !batch->set ||
            batch->depth_hack != depth_hack || batch->alpha ||
            batch->additive || batch->flare || batch->occlusion ||
            batch->gpu_bmodel ||
            batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
            (vertex_flags & VK_ENTITY_VERTEX_CELSHADING) == 0 ||
            // The OpenGL cel replay follows the base item-colorize mesh, not
            // the separate luminance tint overlay.
            (vertex_flags & VK_ENTITY_VERTEX_ITEM_COLORIZE) != 0 ||
            batch->cel_line_width <= 0.0f) {
            continue;
        }

        if (batch->gpu_md2) {
            if (!VK_Entity_BindGpuMd2Batch(cmd, frame, batch)) {
                continue;
            }
            if (dynamic_buffers_bound) {
                *dynamic_buffers_bound = false;
            }
#if USE_MD5
        } else if (batch->gpu_md5) {
            if (!VK_Entity_BindGpuMd5Batch(cmd, frame, batch)) {
                continue;
            }
            if (dynamic_buffers_bound) {
                *dynamic_buffers_bound = false;
            }
#endif
        } else if (!dynamic_buffers_bound || !*dynamic_buffers_bound) {
            VK_Entity_BindDynamicBuffers(cmd, frame);
            if (dynamic_buffers_bound) {
                *dynamic_buffers_bound = true;
            }
        }

        const bool use_weapon_push =
            batch->weapon_model && vk_entity.frame_weapon_active;
        if (use_weapon_push != using_weapon_push) {
            const renderer_view_push_t *push = use_weapon_push
                ? &vk_entity.frame_push_weapon : &vk_entity.frame_push;
            vkCmdPushConstants(cmd, vk_entity.pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*push),
                               push);
            using_weapon_push = use_weapon_push;
        }

        VkPipeline target_pipeline = batch->gpu_md2
            ? vk_entity.pipeline_gpu_md2_celshading
#if USE_MD5
            : batch->gpu_md5 ? vk_entity.pipeline_gpu_md5_celshading
#endif
            : vk_entity.pipeline_celshading;
        target_pipeline = VK_SelectScenePipeline(vk_entity.ctx,
                                                 target_pipeline);
        if (!target_pipeline) {
            continue;
        }
        if (target_pipeline != bound_pipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              target_pipeline);
            bound_pipeline = target_pipeline;
            last_set = VK_NULL_HANDLE;
        }
        if (batch->cel_line_width != last_line_width) {
            vkCmdSetLineWidth(cmd, batch->cel_line_width);
            last_line_width = batch->cel_line_width;
        }
        if (batch->set != last_set) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    vk_entity.pipeline_layout,
                                    0, 1, &batch->set, 0, NULL);
            last_set = batch->set;
        }
        VK_Entity_DrawBatch(cmd, batch);
    }

    if (using_weapon_push) {
        vkCmdPushConstants(cmd, vk_entity.pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(vk_entity.frame_push),
                           &vk_entity.frame_push);
    }
}

static void VK_Entity_RecordPhase(VkCommandBuffer cmd, const VkExtent2D *extent,
                                  vk_entity_record_phase_t phase)
{
    if (!VK_Entity_HasRecordPhase(phase)) {
        return;
    }

    vk_entity_frame_buffer_t *frame = VK_Entity_CurrentFrameBuffer();
    if (!vk_entity.initialized || !vk_entity.swapchain_ready ||
        !vk_entity.pipeline_opaque || !vk_entity.pipeline_opaque_no_depth_write ||
        !vk_entity.pipeline_alpha ||
        !vk_entity.pipeline_additive || !vk_entity.pipeline_flare ||
        !vk_entity.pipeline_occlusion || !vk_entity.flare_query_pool ||
        !vk_entity.pipeline_depthhack_opaque ||
        !vk_entity.pipeline_depthhack_opaque_no_depth_write ||
        !vk_entity.pipeline_depthhack_alpha ||
        !vk_entity.pipeline_depthhack_additive ||
        (vk_entity.stencil_available &&
         (!vk_entity.pipeline_outline_mask ||
          !vk_entity.pipeline_outline_mask_no_depth ||
          !vk_entity.pipeline_outline_shell_opaque ||
          !vk_entity.pipeline_outline_shell_alpha ||
          !vk_entity.pipeline_outline_shell_no_depth_opaque ||
          !vk_entity.pipeline_outline_shell_no_depth_alpha ||
          !vk_entity.pipeline_outline_clear)) ||
        !vk_entity.frame_active || !vk_entity.batch_count ||
        (!vk_entity.vertex_count && !vk_entity.md2_instance_count &&
         !vk_entity.bmodel_instance_count && !vk_entity.particle_instance_count &&
         !vk_entity.beam_instance_count
#if USE_MD5
         && !vk_entity.md5_instance_count
#endif
         ) || !frame ||
        ((vk_entity.vertex_count || vk_entity.index_count) &&
         !frame->geometry_buffer) ||
        ((vk_entity.md2_instance_count || vk_entity.bmodel_instance_count ||
          vk_entity.particle_instance_count || vk_entity.beam_instance_count) &&
         !frame->instance_buffer) ||
#if USE_MD5
        (vk_entity.md5_instance_count &&
         (!frame->instance_buffer || !frame->md5_palette_capacity)) ||
#endif
        !extent) {
        return;
    }

    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)extent->height,
        .width = (float)extent->width,
        .height = -(float)extent->height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = *extent,
    };

    if (vk_entity.frame_uses_view_rect) {
        int64_t left = vk_entity.frame_view_rect.offset.x;
        int64_t top = vk_entity.frame_view_rect.offset.y;
        int64_t right = left + (int64_t)vk_entity.frame_view_rect.extent.width;
        int64_t bottom = top + (int64_t)vk_entity.frame_view_rect.extent.height;

        left = Q_clip(left, 0, (int64_t)extent->width);
        top = Q_clip(top, 0, (int64_t)extent->height);
        right = Q_clip(right, left, (int64_t)extent->width);
        bottom = Q_clip(bottom, top, (int64_t)extent->height);

        if (right <= left || bottom <= top) {
            return;
        }

        scissor.offset.x = (int32_t)left;
        scissor.offset.y = (int32_t)top;
        scissor.extent.width = (uint32_t)(right - left);
        scissor.extent.height = (uint32_t)(bottom - top);
        viewport.x = (float)left;
        viewport.y = (float)bottom;
        viewport.width = (float)(right - left);
        viewport.height = -(float)(bottom - top);
    }

    VkDescriptorSet lightmap_set = VK_World_GetLightmapDescriptorSet();
    if (!lightmap_set) {
        lightmap_set = vk_entity.white_set;
    }
    VkDescriptorSet shadow_set = VK_Shadow_GetDescriptorSet();
    if (!lightmap_set || !shadow_set) {
        return;
    }

    bool dynamic_buffers_bound = false;
    bool bmodel_buffers_bound = false;
    if (vk_entity.vertex_count) {
        VK_Entity_BindDynamicBuffers(cmd, frame);
        dynamic_buffers_bound = true;
    }
    vkCmdPushConstants(cmd, vk_entity.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(vk_entity.frame_push), &vk_entity.frame_push);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_entity.pipeline_layout,
                            1, 1, &lightmap_set, 0, NULL);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_entity.pipeline_layout,
                            2, 1, &shadow_set, 0, NULL);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // This UBO-derived state is current for the refdef being recorded.  The
    // no-fog inline-BSP fragment module is valid only when global and height
    // fog are both inactive; a runtime fog transition simply binds the
    // existing fog-aware fast pipeline on the next frame.
    const bool surface_fog_active = VK_Shadow_HasActiveSurfaceFog();
    const bool bmodel_fast_lit_no_fog_enabled =
        (!vk_bmodel_fast_lit_no_fog || vk_bmodel_fast_lit_no_fog->integer) &&
        !surface_fog_active;
    const bool bmodel_texture_replace_enabled =
        !vk_bmodel_texture_replace || vk_bmodel_texture_replace->integer;
    const bool bmodel_texture_replace_no_fog_enabled =
        bmodel_texture_replace_enabled && !surface_fog_active;
    const uint8_t record_pass_mask = VK_Entity_RecordPassMask(phase);

    // Match GL's effective entity ordering: opaque model work, any translucent
    // item base, the item's colorize overlay, then general translucent/additive
    // entities such as the separately submitted rim.
    for (int pass = 0; pass < 4; pass++) {
        if ((record_pass_mask & BIT(pass)) == 0) {
            continue;
        }
        bool alpha = pass != 0;
        VkPipeline bound_pipeline = VK_NULL_HANDLE;
        VkDescriptorSet last_set = VK_NULL_HANDLE;

        for (uint32_t i = 0; i < vk_entity.batch_count; i++) {
            const vk_batch_t *batch = &vk_entity.batches[i];
            uint32_t vertex_flags = VK_Entity_BatchVertexFlags(batch);
            int batch_alpha_pass =
                (vertex_flags & VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE) ? 1 :
                (vertex_flags & VK_ENTITY_VERTEX_ITEM_COLORIZE) ? 2 : 3;
            if (!VK_Entity_BatchMatchesRecordPhase(batch, phase) ||
                !batch->vertex_count || !batch->set || batch->depth_hack ||
                batch->flare || batch->occlusion ||
                batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
                batch->alpha != alpha ||
                (alpha && batch_alpha_pass != pass)) {
                continue;
            }

            if (batch->gpu_md2) {
                if (!VK_Entity_BindGpuMd2Batch(cmd, frame, batch)) {
                    continue;
                }
                dynamic_buffers_bound = false;
                bmodel_buffers_bound = false;
            } else if (batch->gpu_bmodel) {
                if (!bmodel_buffers_bound ||
                    !VK_Entity_BmodelBindingCacheEnabled()) {
                    if (!VK_Entity_BindGpuBmodelBatch(cmd, frame, batch)) {
                        continue;
                    }
                    VK_Debug_RecordEntityBmodelBinding();
                }
                dynamic_buffers_bound = false;
                bmodel_buffers_bound = true;
            } else if (batch->gpu_particle) {
                if (!VK_Entity_BindGpuParticleBatch(cmd, frame, batch)) {
                    continue;
                }
                dynamic_buffers_bound = false;
                bmodel_buffers_bound = false;
            } else if (batch->gpu_beam) {
                if (!VK_Entity_BindGpuBeamBatch(cmd, frame, batch)) {
                    continue;
                }
                dynamic_buffers_bound = false;
                bmodel_buffers_bound = false;
#if USE_MD5
            } else if (batch->gpu_md5) {
                if (!VK_Entity_BindGpuMd5Batch(cmd, frame, batch)) {
                    continue;
                }
                dynamic_buffers_bound = false;
                bmodel_buffers_bound = false;
#endif
            } else if (!dynamic_buffers_bound) {
                VK_Entity_BindDynamicBuffers(cmd, frame);
                dynamic_buffers_bound = true;
                bmodel_buffers_bound = false;
            }

            VkPipeline target_pipeline;
            if (batch->gpu_md2) {
                target_pipeline = batch->additive
                    ? vk_entity.pipeline_gpu_md2_additive
                    : alpha ? vk_entity.pipeline_gpu_md2_alpha
                            : vk_entity.pipeline_gpu_md2_opaque;
            } else if (batch->gpu_bmodel) {
                const bool fast_lit =
                    (vertex_flags & VK_ENTITY_VERTEX_GPU_BMODEL_FAST_LIT) != 0 &&
                    vk_entity.pipeline_gpu_bmodel_fast_lit_opaque;
                const bool fast_lit_no_fog = fast_lit &&
                    bmodel_fast_lit_no_fog_enabled &&
                    vk_entity.pipeline_gpu_bmodel_fast_lit_no_fog_opaque;
                const bool texture_replace =
                    (vertex_flags & VK_ENTITY_VERTEX_GPU_BMODEL_TEXTURE_REPLACE) != 0 &&
                    bmodel_texture_replace_enabled &&
                    vk_entity.pipeline_gpu_bmodel_texture_replace_opaque;
                const bool texture_replace_no_fog = texture_replace &&
                    bmodel_texture_replace_no_fog_enabled &&
                    vk_entity.pipeline_gpu_bmodel_texture_replace_no_fog_opaque;
                target_pipeline = fast_lit_no_fog
                    ? vk_entity.pipeline_gpu_bmodel_fast_lit_no_fog_opaque
                    : fast_lit
                    ? vk_entity.pipeline_gpu_bmodel_fast_lit_opaque
                    : texture_replace_no_fog
                    ? vk_entity.pipeline_gpu_bmodel_texture_replace_no_fog_opaque
                    : texture_replace
                    ? vk_entity.pipeline_gpu_bmodel_texture_replace_opaque
                    : batch->additive ? vk_entity.pipeline_gpu_bmodel_additive
                    : alpha ? vk_entity.pipeline_gpu_bmodel_alpha
                            : vk_entity.pipeline_gpu_bmodel_opaque;
                if (fast_lit) {
                    VK_Debug_RecordFastLitDraw(VK_DEBUG_DOMAIN_ENTITY);
                }
                if (fast_lit_no_fog) {
                    VK_Debug_RecordEntityFastLitNoFogDraw();
                }
                if (texture_replace) {
                    VK_Debug_RecordEntityTextureReplaceDraw(
                        texture_replace_no_fog);
                }
            } else if (batch->gpu_particle) {
                target_pipeline = batch->additive
                    ? vk_entity.pipeline_gpu_particle_additive
                    : vk_entity.pipeline_gpu_particle_alpha;
            } else if (batch->gpu_beam) {
                target_pipeline = vk_entity.pipeline_gpu_beam_alpha;
#if USE_MD5
            } else if (batch->gpu_md5) {
                target_pipeline = batch->additive
                    ? vk_entity.pipeline_gpu_md5_additive
                    : alpha ? vk_entity.pipeline_gpu_md5_alpha
                            : vk_entity.pipeline_gpu_md5_opaque;
#endif
            } else {
                target_pipeline = batch->additive ? vk_entity.pipeline_additive
                    : batch->cutout ? vk_entity.pipeline_cutout
                    : batch->no_depth_write
                        ? vk_entity.pipeline_opaque_no_depth_write
                    : alpha ? vk_entity.pipeline_alpha
                            : vk_entity.pipeline_opaque;
            }
            target_pipeline = VK_SelectScenePipeline(vk_entity.ctx,
                                                     target_pipeline);
            if (!target_pipeline) {
                continue;
            }
            if (bound_pipeline != target_pipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, target_pipeline);
                bound_pipeline = target_pipeline;
                last_set = VK_NULL_HANDLE;
            }

            if (batch->set != last_set) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vk_entity.pipeline_layout,
                                        0, 1, &batch->set, 0, NULL);
                last_set = batch->set;
            }

            VK_Entity_DrawBatch(cmd, batch);
        }

        if (pass == 0) {
            VK_Entity_RecordCelShadingPass(cmd, frame, phase, false,
                                           &dynamic_buffers_bound);
            // The replay may leave an alias GPU buffer bound; later alpha
            // batches must restore the transient stream before drawing.
            dynamic_buffers_bound = false;
            bmodel_buffers_bound = false;
        }
    }

    if (vk_entity.frame_has_depth_hack_batches) {
        VkPipeline bound_pipeline = VK_NULL_HANDLE;
        VkDescriptorSet last_set = VK_NULL_HANDLE;
        bool using_weapon_push = false;
        VkViewport depthhack_viewport = viewport;

        depthhack_viewport.maxDepth = 0.25f;
        vkCmdSetViewport(cmd, 0, 1, &depthhack_viewport);

        for (int pass = 0; pass < 4; pass++) {
            bool alpha = pass != 0;
            for (uint32_t i = 0; i < vk_entity.batch_count; i++) {
                const vk_batch_t *batch = &vk_entity.batches[i];
                uint32_t vertex_flags = VK_Entity_BatchVertexFlags(batch);
                int batch_alpha_pass =
                    (vertex_flags & VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE) ? 1 :
                    (vertex_flags & VK_ENTITY_VERTEX_ITEM_COLORIZE) ? 2 : 3;
                if (!VK_Entity_BatchMatchesRecordPhase(batch, phase) ||
                    !batch->vertex_count || !batch->set || !batch->depth_hack ||
                    batch->flare || batch->occlusion ||
                    batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
                    batch->alpha != alpha ||
                    (alpha && batch_alpha_pass != pass)) {
                    continue;
                }

                if (batch->gpu_md2) {
                    if (!VK_Entity_BindGpuMd2Batch(cmd, frame, batch)) {
                        continue;
                    }
                    dynamic_buffers_bound = false;
                    bmodel_buffers_bound = false;
                } else if (batch->gpu_beam) {
                    if (!VK_Entity_BindGpuBeamBatch(cmd, frame, batch)) {
                        continue;
                    }
                    dynamic_buffers_bound = false;
                    bmodel_buffers_bound = false;
#if USE_MD5
                } else if (batch->gpu_md5) {
                    if (!VK_Entity_BindGpuMd5Batch(cmd, frame, batch)) {
                        continue;
                    }
                    dynamic_buffers_bound = false;
                    bmodel_buffers_bound = false;
#endif
                } else if (!dynamic_buffers_bound) {
                    VK_Entity_BindDynamicBuffers(cmd, frame);
                    dynamic_buffers_bound = true;
                    bmodel_buffers_bound = false;
                }

                bool use_weapon_push = batch->weapon_model && vk_entity.frame_weapon_active;
                if (use_weapon_push != using_weapon_push) {
                    const renderer_view_push_t *push = use_weapon_push
                        ? &vk_entity.frame_push_weapon
                        : &vk_entity.frame_push;
                    vkCmdPushConstants(cmd, vk_entity.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                                       0, sizeof(*push), push);
                    using_weapon_push = use_weapon_push;
                }

                VkPipeline target_pipeline;
                if (batch->gpu_md2) {
                    target_pipeline = batch->additive
                        ? vk_entity.pipeline_gpu_md2_depthhack_additive
                        : batch->alpha
                            ? vk_entity.pipeline_gpu_md2_depthhack_alpha
                            : vk_entity.pipeline_gpu_md2_depthhack_opaque;
                } else if (batch->gpu_beam) {
                    target_pipeline = vk_entity.pipeline_gpu_beam_depthhack_alpha;
#if USE_MD5
                } else if (batch->gpu_md5) {
                    target_pipeline = batch->additive
                        ? vk_entity.pipeline_gpu_md5_depthhack_additive
                        : batch->alpha
                            ? vk_entity.pipeline_gpu_md5_depthhack_alpha
                            : vk_entity.pipeline_gpu_md5_depthhack_opaque;
#endif
                } else {
                    target_pipeline = batch->additive
                        ? vk_entity.pipeline_depthhack_additive
                        : batch->cutout
                            ? vk_entity.pipeline_depthhack_cutout
                        : batch->no_depth_write
                            ? vk_entity.pipeline_depthhack_opaque_no_depth_write
                        : batch->alpha
                            ? vk_entity.pipeline_depthhack_alpha
                            : vk_entity.pipeline_depthhack_opaque;
                }
                target_pipeline = VK_SelectScenePipeline(vk_entity.ctx,
                                                         target_pipeline);
                if (!target_pipeline) {
                    continue;
                }
                if (bound_pipeline != target_pipeline) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, target_pipeline);
                    bound_pipeline = target_pipeline;
                    last_set = VK_NULL_HANDLE;
                }

                if (batch->set != last_set) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            vk_entity.pipeline_layout,
                                            0, 1, &batch->set, 0, NULL);
                    last_set = batch->set;
                }

                VK_Entity_DrawBatch(cmd, batch);
            }

            if (pass == 0) {
                VK_Entity_RecordCelShadingPass(cmd, frame, phase, true,
                                               &dynamic_buffers_bound);
                // The helper restores the normal push constants after any
                // weapon model replay. Reset tracked state before the later
                // depth-hack alpha passes choose their own buffers/push.
                dynamic_buffers_bound = false;
                bmodel_buffers_bound = false;
                bound_pipeline = VK_NULL_HANDLE;
                last_set = VK_NULL_HANDLE;
                using_weapon_push = false;
            }
        }
    }

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdPushConstants(cmd, vk_entity.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(vk_entity.frame_push), &vk_entity.frame_push);
    if (vk_entity.vertex_count && !dynamic_buffers_bound) {
        VK_Entity_BindDynamicBuffers(cmd, frame);
        dynamic_buffers_bound = true;
    }

    if (vk_entity.stencil_available && vk_entity.frame_has_outline_batches) {
        VkPipeline bound_pipeline = VK_NULL_HANDLE;
        VkDescriptorSet last_set = VK_NULL_HANDLE;
        bool using_depthhack_viewport = false;
        bool using_weapon_push = false;

        for (uint32_t i = 0; i < vk_entity.batch_count; i++) {
            const vk_batch_t *batch = &vk_entity.batches[i];
            if (!VK_Entity_BatchMatchesRecordPhase(batch, phase) ||
                !batch->vertex_count || !batch->set ||
                batch->outline_stage == VK_ENTITY_OUTLINE_NONE) {
                continue;
            }

            if (batch->depth_hack != using_depthhack_viewport) {
                VkViewport outline_viewport = viewport;
                if (batch->depth_hack) {
                    outline_viewport.maxDepth = 0.25f;
                }
                vkCmdSetViewport(cmd, 0, 1, &outline_viewport);
                using_depthhack_viewport = batch->depth_hack;
            }

            bool use_weapon_push =
                batch->weapon_model && vk_entity.frame_weapon_active;
            if (use_weapon_push != using_weapon_push) {
                const renderer_view_push_t *push = use_weapon_push
                    ? &vk_entity.frame_push_weapon
                    : &vk_entity.frame_push;
                vkCmdPushConstants(cmd, vk_entity.pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(*push), push);
                using_weapon_push = use_weapon_push;
            }

            VkPipeline target_pipeline = VK_NULL_HANDLE;
            switch (batch->outline_stage) {
            case VK_ENTITY_OUTLINE_MASK:
                target_pipeline = batch->outline_no_depth
                    ? vk_entity.pipeline_outline_mask_no_depth
                    : vk_entity.pipeline_outline_mask;
                break;
            case VK_ENTITY_OUTLINE_SHELL:
                if (batch->outline_no_depth) {
                    target_pipeline = batch->alpha
                        ? vk_entity.pipeline_outline_shell_no_depth_alpha
                        : vk_entity.pipeline_outline_shell_no_depth_opaque;
                } else {
                    target_pipeline = batch->alpha
                        ? vk_entity.pipeline_outline_shell_alpha
                        : vk_entity.pipeline_outline_shell_opaque;
                }
                break;
            case VK_ENTITY_OUTLINE_CLEAR:
                target_pipeline = vk_entity.pipeline_outline_clear;
                break;
            default:
                break;
            }
            target_pipeline = VK_SelectScenePipeline(vk_entity.ctx,
                                                     target_pipeline);
            if (!target_pipeline) {
                continue;
            }
            if (target_pipeline != bound_pipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  target_pipeline);
                bound_pipeline = target_pipeline;
                last_set = VK_NULL_HANDLE;
            }
            if (batch->set != last_set) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vk_entity.pipeline_layout,
                                        0, 1, &batch->set, 0, NULL);
                last_set = batch->set;
            }
            VK_Entity_DrawBatch(cmd, batch);
        }

        if (using_depthhack_viewport) {
            vkCmdSetViewport(cmd, 0, 1, &viewport);
        }
        if (using_weapon_push) {
            vkCmdPushConstants(cmd, vk_entity.pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(vk_entity.frame_push),
                               &vk_entity.frame_push);
        }
    }

    if (vk_entity.frame_has_flare_queries) {
        VkDescriptorSet last_set = VK_NULL_HANDLE;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          VK_SelectScenePipeline(vk_entity.ctx,
                                                 vk_entity.pipeline_occlusion));
        for (uint32_t i = 0; i < vk_entity.batch_count; i++) {
            const vk_batch_t *batch = &vk_entity.batches[i];
            if (!VK_Entity_BatchMatchesRecordPhase(batch, phase) ||
                !batch->occlusion || !batch->vertex_count || !batch->set ||
                batch->query_index >= MAX_EDICTS) {
                continue;
            }
            if (batch->set != last_set) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vk_entity.pipeline_layout,
                                        0, 1, &batch->set, 0, NULL);
                last_set = batch->set;
            }
            vkCmdBeginQuery(cmd, vk_entity.flare_query_pool,
                            batch->query_index, 0);
            VK_Entity_DrawBatch(cmd, batch);
            VK_Debug_RecordQuery(1);
            vkCmdEndQuery(cmd, vk_entity.flare_query_pool, batch->query_index);
        }
    }

    if (vk_entity.frame_has_flares) {
        VkDescriptorSet last_set = VK_NULL_HANDLE;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          VK_SelectScenePipeline(vk_entity.ctx,
                                                 vk_entity.pipeline_flare));
        for (uint32_t i = 0; i < vk_entity.batch_count; i++) {
            const vk_batch_t *batch = &vk_entity.batches[i];
            if (!VK_Entity_BatchMatchesRecordPhase(batch, phase) ||
                !batch->flare || !batch->vertex_count || !batch->set) {
                continue;
            }
            if (batch->set != last_set) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vk_entity.pipeline_layout,
                                        0, 1, &batch->set, 0, NULL);
                last_set = batch->set;
            }
            VK_Entity_DrawBatch(cmd, batch);
        }
    }
}

void VK_Entity_Record(VkCommandBuffer cmd, const VkExtent2D *extent)
{
    VK_Entity_RecordPhase(cmd, extent, VK_ENTITY_RECORD_ALL);
}

static bool VK_Entity_BatchHasDirectBloomEmission(
    const vk_batch_t *batch, vk_entity_record_phase_t phase, bool depth_hack,
    bool *out_bloom_shell, bool *out_bloom_rim)
{
    if (!batch || !VK_Entity_BatchMatchesRecordPhase(batch, phase) ||
        !batch->vertex_count || !batch->set ||
        batch->depth_hack != depth_hack || batch->flare || batch->occlusion ||
        batch->outline_stage != VK_ENTITY_OUTLINE_NONE) {
        return false;
    }

    const uint32_t vertex_flags = VK_Entity_BatchVertexFlags(batch);
    if ((vertex_flags & (VK_ENTITY_VERTEX_ITEM_COLORIZE |
                         VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE)) != 0) {
        return false;
    }
    const bool bloom_shell =
        (vertex_flags & VK_ENTITY_VERTEX_BLOOM_SHELL) != 0;
    const bool bloom_rim =
        (vertex_flags & VK_ENTITY_VERTEX_BLOOM_RIM) != 0;
    if ((batch->alpha && !bloom_shell && !bloom_rim) ||
        (batch->additive && !bloom_rim) ||
        (bloom_rim && !batch->additive) ||
        (!bloom_shell && !bloom_rim &&
         (vertex_flags & VK_ENTITY_VERTEX_GLOWMAP) == 0)) {
        return false;
    }

    if (out_bloom_shell) {
        *out_bloom_shell = bloom_shell;
    }
    if (out_bloom_rim) {
        *out_bloom_rim = bloom_rim;
    }
    return true;
}

static VkPipeline VK_Entity_GetBloomPipeline(const vk_batch_t *batch,
                                              bool bloom_shell,
                                              bool bloom_rim)
{
    if (batch->gpu_md2) {
        return bloom_rim ? vk_entity.pipeline_gpu_md2_bloom_extract_additive
            : bloom_shell ? vk_entity.pipeline_gpu_md2_bloom_extract_alpha
                          : vk_entity.pipeline_gpu_md2_bloom_extract;
    }
    if (batch->gpu_bmodel) {
        return vk_entity.pipeline_gpu_bmodel_bloom_extract;
    }
#if USE_MD5
    if (batch->gpu_md5) {
        return bloom_rim ? vk_entity.pipeline_gpu_md5_bloom_extract_additive
            : bloom_shell ? vk_entity.pipeline_gpu_md5_bloom_extract_alpha
                          : vk_entity.pipeline_gpu_md5_bloom_extract;
    }
#endif
    return bloom_rim ? vk_entity.pipeline_bloom_extract_additive
        : bloom_shell ? vk_entity.pipeline_bloom_extract_alpha
                      : vk_entity.pipeline_bloom_extract;
}

static void VK_Entity_RecordDepthHackBloomEmissionPhase(
    VkCommandBuffer cmd, const VkExtent2D *extent, vk_entity_record_phase_t phase)
{
    if (!VK_Entity_HasRecordPhase(phase)) {
        return;
    }

    vk_entity_frame_buffer_t *frame = VK_Entity_CurrentFrameBuffer();
    if (!cmd || !extent || !vk_entity.initialized || !vk_entity.swapchain_ready ||
        !vk_entity.frame_active || !vk_entity.batch_count || !frame) {
        return;
    }

    VkDescriptorSet lightmap_set = VK_World_GetLightmapDescriptorSet();
    if (!lightmap_set) {
        lightmap_set = vk_entity.white_set;
    }
    VkDescriptorSet shadow_set = VK_Shadow_GetDescriptorSet();
    if (!lightmap_set || !shadow_set) {
        return;
    }

    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)extent->height,
        .width = (float)extent->width,
        .height = -(float)extent->height,
        .minDepth = 0.0f,
        // Match the depth-hack viewport used by the normal entity pass so a
        // view weapon's direct bloom source remains in front of world depth.
        .maxDepth = 0.25f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = *extent,
    };
    vkCmdPushConstants(cmd, vk_entity.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(vk_entity.frame_push), &vk_entity.frame_push);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_entity.pipeline_layout, 1, 1,
                            &lightmap_set, 0, NULL);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_entity.pipeline_layout, 2, 1,
                            &shadow_set, 0, NULL);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    bool dynamic_buffers_bound = false;
    bool bmodel_buffers_bound = false;
    bool using_weapon_push = false;
    VkPipeline bound_pipeline = VK_NULL_HANDLE;
    VkDescriptorSet last_set = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < vk_entity.batch_count; ++i) {
        const vk_batch_t *batch = &vk_entity.batches[i];
        bool bloom_shell;
        bool bloom_rim;
        if (!VK_Entity_BatchHasDirectBloomEmission(
                batch, phase, true, &bloom_shell, &bloom_rim)) {
            continue;
        }

        VkPipeline target_pipeline =
            VK_Entity_GetBloomPipeline(batch, bloom_shell, bloom_rim);
        if (!target_pipeline) {
            continue;
        }

        if (batch->gpu_md2) {
            if (!VK_Entity_BindGpuMd2Batch(cmd, frame, batch)) {
                continue;
            }
            dynamic_buffers_bound = false;
            bmodel_buffers_bound = false;
        } else if (batch->gpu_bmodel) {
            if (!bmodel_buffers_bound ||
                !VK_Entity_BmodelBindingCacheEnabled()) {
                if (!VK_Entity_BindGpuBmodelBatch(cmd, frame, batch)) {
                    continue;
                }
                VK_Debug_RecordEntityBmodelBinding();
            }
            dynamic_buffers_bound = false;
            bmodel_buffers_bound = true;
#if USE_MD5
        } else if (batch->gpu_md5) {
            if (!VK_Entity_BindGpuMd5Batch(cmd, frame, batch)) {
                continue;
            }
            dynamic_buffers_bound = false;
            bmodel_buffers_bound = false;
#endif
        } else if (!dynamic_buffers_bound) {
            if (!frame->geometry_buffer) {
                continue;
            }
            VK_Entity_BindDynamicBuffers(cmd, frame);
            dynamic_buffers_bound = true;
            bmodel_buffers_bound = false;
        }

        const bool use_weapon_push =
            batch->weapon_model && vk_entity.frame_weapon_active;
        if (use_weapon_push != using_weapon_push) {
            const renderer_view_push_t *push = use_weapon_push
                ? &vk_entity.frame_push_weapon : &vk_entity.frame_push;
            vkCmdPushConstants(cmd, vk_entity.pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*push),
                               push);
            using_weapon_push = use_weapon_push;
        }

        if (target_pipeline != bound_pipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              target_pipeline);
            bound_pipeline = target_pipeline;
            last_set = VK_NULL_HANDLE;
        }
        if (batch->set != last_set) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    vk_entity.pipeline_layout, 0, 1,
                                    &batch->set, 0, NULL);
            last_set = batch->set;
        }
        VK_Entity_DrawBatch(cmd, batch);
    }

}

bool VK_Entity_HasDepthHackBloomEmission(void)
{
    if (!vk_entity.initialized || !vk_entity.swapchain_ready ||
        !vk_entity.frame_active || !vk_entity.batch_count ||
        !VK_Entity_HasRecordPhase(VK_ENTITY_RECORD_ALPHA_FRONT)) {
        return false;
    }

    for (uint32_t i = 0; i < vk_entity.batch_count; ++i) {
        const vk_batch_t *batch = &vk_entity.batches[i];
        bool bloom_shell;
        bool bloom_rim;
        if (VK_Entity_BatchHasDirectBloomEmission(
                batch, VK_ENTITY_RECORD_ALPHA_FRONT, true, &bloom_shell,
                &bloom_rim) &&
            VK_Entity_GetBloomPipeline(batch, bloom_shell, bloom_rim)) {
            return true;
        }
    }
    return false;
}

bool VK_Entity_HasBloomEmission(void)
{
    if (!vk_entity.initialized || !vk_entity.swapchain_ready ||
        !vk_entity.frame_active || !vk_entity.batch_count) {
        return false;
    }

    for (uint32_t i = 0; i < vk_entity.batch_count; ++i) {
        const vk_batch_t *batch = &vk_entity.batches[i];
        bool bloom_shell;
        bool bloom_rim;
        if (VK_Entity_BatchHasDirectBloomEmission(
                batch, VK_ENTITY_RECORD_ALL, false, &bloom_shell,
                &bloom_rim) &&
            VK_Entity_GetBloomPipeline(batch, bloom_shell, bloom_rim)) {
            return true;
        }
    }
    return VK_Entity_HasDepthHackBloomEmission();
}

void VK_Entity_RecordDepthHackBloomEmission(VkCommandBuffer cmd,
                                            const VkExtent2D *extent)
{
    VK_Entity_RecordDepthHackBloomEmissionPhase(
        cmd, extent, VK_ENTITY_RECORD_ALPHA_FRONT);
}

bool VK_Entity_HasBloomRimDepthSampling(bool before_liquid)
{
    if (!vk_entity.initialized || !vk_entity.swapchain_ready ||
        !vk_entity.frame_active || !vk_entity.ctx ||
        vk_entity.ctx->current_frame >= vk_entity.ctx->frame_count ||
        !vk_entity.ctx->frames[vk_entity.ctx->current_frame]
             .bloom_depth_descriptor_set) {
        return false;
    }

    const vk_entity_record_phase_t phase = before_liquid
        ? VK_ENTITY_RECORD_BEFORE_LIQUID : VK_ENTITY_RECORD_ALL;
    if (!VK_Entity_HasRecordPhase(phase)) {
        return false;
    }
    for (uint32_t i = 0; i < vk_entity.batch_count; ++i) {
        const vk_batch_t *batch = &vk_entity.batches[i];
        if (!VK_Entity_BatchMatchesRecordPhase(batch, phase) ||
            !batch->vertex_count || !batch->set || !batch->additive ||
            batch->depth_hack || batch->weapon_model || batch->flare ||
            batch->occlusion ||
            (VK_Entity_BatchVertexFlags(batch) &
             VK_ENTITY_VERTEX_BLOOM_RIM) == 0) {
            continue;
        }
        if (batch->gpu_md2) {
            return vk_entity.pipeline_gpu_md2_bloom_extract_additive_depth_sample !=
                VK_NULL_HANDLE;
        }
#if USE_MD5
        if (batch->gpu_md5) {
            return vk_entity.pipeline_gpu_md5_bloom_extract_additive_depth_sample !=
                VK_NULL_HANDLE;
        }
#endif
        if (!batch->gpu_bmodel) {
            return vk_entity.pipeline_bloom_extract_additive_depth_sample !=
                VK_NULL_HANDLE;
        }
    }
    return false;
}

void VK_Entity_RecordBloomEmission(VkCommandBuffer cmd, const VkExtent2D *extent,
                                   bool before_liquid, bool sampled_depth_rim)
{
    vk_entity_frame_buffer_t *frame = VK_Entity_CurrentFrameBuffer();
    if (!cmd || !extent || !vk_entity.initialized || !vk_entity.swapchain_ready ||
        (!vk_entity.pipeline_bloom_extract &&
         !vk_entity.pipeline_bloom_extract_alpha &&
         !vk_entity.pipeline_bloom_extract_additive &&
         !vk_entity.pipeline_bloom_extract_additive_depth_sample) ||
        !vk_entity.frame_active ||
        !vk_entity.batch_count || !frame) {
        return;
    }

    VkDescriptorSet lightmap_set = VK_World_GetLightmapDescriptorSet();
    if (!lightmap_set) {
        lightmap_set = vk_entity.white_set;
    }
    VkDescriptorSet shadow_set = VK_Shadow_GetDescriptorSet();
    VkDescriptorSet depth_set = vk_entity.ctx->frames[
        vk_entity.ctx->current_frame].bloom_depth_descriptor_set;
    if (!lightmap_set || !shadow_set || (sampled_depth_rim && !depth_set)) {
        return;
    }
    VkDescriptorSet auxiliary_set = sampled_depth_rim ? depth_set : lightmap_set;

    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)extent->height,
        .width = (float)extent->width,
        .height = -(float)extent->height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = *extent,
    };
    vkCmdPushConstants(cmd, vk_entity.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(vk_entity.frame_push), &vk_entity.frame_push);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_entity.pipeline_layout, 1, 1,
                            &auxiliary_set, 0, NULL);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_entity.pipeline_layout, 2, 1,
                            &shadow_set, 0, NULL);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const vk_entity_record_phase_t phase = before_liquid
        ? VK_ENTITY_RECORD_BEFORE_LIQUID : VK_ENTITY_RECORD_ALL;
    if (!VK_Entity_HasRecordPhase(phase)) {
        return;
    }
    bool dynamic_buffers_bound = false;
    bool bmodel_buffers_bound = false;
    VkPipeline bound_pipeline = VK_NULL_HANDLE;
    VkDescriptorSet last_set = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < vk_entity.batch_count; i++) {
        const vk_batch_t *batch = &vk_entity.batches[i];
        const uint32_t vertex_flags = VK_Entity_BatchVertexFlags(batch);
        const bool bloom_shell =
            (vertex_flags & VK_ENTITY_VERTEX_BLOOM_SHELL) != 0;
        const bool bloom_rim =
            (vertex_flags & VK_ENTITY_VERTEX_BLOOM_RIM) != 0;
        if (!VK_Entity_BatchMatchesRecordPhase(batch, phase) ||
            !batch->vertex_count || !batch->set ||
            (batch->alpha && !bloom_shell && !bloom_rim) ||
            (batch->additive && !bloom_rim) ||
            (bloom_rim && !batch->additive) ||
            batch->depth_hack || batch->weapon_model ||
            batch->flare || batch->occlusion ||
            batch->outline_stage != VK_ENTITY_OUTLINE_NONE ||
            (!bloom_shell && !bloom_rim &&
             (vertex_flags & VK_ENTITY_VERTEX_GLOWMAP) == 0) ||
            (vertex_flags & (VK_ENTITY_VERTEX_ITEM_COLORIZE |
                              VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE)) != 0) {
            continue;
        }

        VkPipeline target_pipeline = bloom_rim
            ? vk_entity.pipeline_bloom_extract_additive
            : bloom_shell ? vk_entity.pipeline_bloom_extract_alpha
                          : vk_entity.pipeline_bloom_extract;
        VkPipeline sampled_rim_pipeline = VK_NULL_HANDLE;
        if (batch->gpu_md2) {
            target_pipeline = bloom_rim
                ? vk_entity.pipeline_gpu_md2_bloom_extract_additive
                : bloom_shell ? vk_entity.pipeline_gpu_md2_bloom_extract_alpha
                              : vk_entity.pipeline_gpu_md2_bloom_extract;
            sampled_rim_pipeline =
                vk_entity.pipeline_gpu_md2_bloom_extract_additive_depth_sample;
        } else if (batch->gpu_bmodel) {
            target_pipeline = vk_entity.pipeline_gpu_bmodel_bloom_extract;
#if USE_MD5
        } else if (batch->gpu_md5) {
            target_pipeline = bloom_rim
                ? vk_entity.pipeline_gpu_md5_bloom_extract_additive
                : bloom_shell ? vk_entity.pipeline_gpu_md5_bloom_extract_alpha
                              : vk_entity.pipeline_gpu_md5_bloom_extract;
            sampled_rim_pipeline =
                vk_entity.pipeline_gpu_md5_bloom_extract_additive_depth_sample;
#endif
        } else {
            sampled_rim_pipeline =
                vk_entity.pipeline_bloom_extract_additive_depth_sample;
        }
        const bool use_sampled_rim = bloom_rim && depth_set &&
            sampled_rim_pipeline != VK_NULL_HANDLE;
        if (sampled_depth_rim != use_sampled_rim) {
            continue;
        }
        if (use_sampled_rim) {
            target_pipeline = sampled_rim_pipeline;
        }
        if (!target_pipeline) {
            continue;
        }

        if (batch->gpu_md2) {
            if (!VK_Entity_BindGpuMd2Batch(cmd, frame, batch)) {
                continue;
            }
            dynamic_buffers_bound = false;
            bmodel_buffers_bound = false;
        } else if (batch->gpu_bmodel) {
            if (!bmodel_buffers_bound ||
                !VK_Entity_BmodelBindingCacheEnabled()) {
                if (!VK_Entity_BindGpuBmodelBatch(cmd, frame, batch)) {
                    continue;
                }
                VK_Debug_RecordEntityBmodelBinding();
            }
            dynamic_buffers_bound = false;
            bmodel_buffers_bound = true;
#if USE_MD5
        } else if (batch->gpu_md5) {
            if (!VK_Entity_BindGpuMd5Batch(cmd, frame, batch)) {
                continue;
            }
            dynamic_buffers_bound = false;
            bmodel_buffers_bound = false;
#endif
        } else if (!dynamic_buffers_bound) {
            if (!frame->geometry_buffer) {
                continue;
            }
            VK_Entity_BindDynamicBuffers(cmd, frame);
            dynamic_buffers_bound = true;
            bmodel_buffers_bound = false;
        }

        if (target_pipeline != bound_pipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              target_pipeline);
            bound_pipeline = target_pipeline;
            last_set = VK_NULL_HANDLE;
        }
        if (batch->set != last_set) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    vk_entity.pipeline_layout, 0, 1,
                                    &batch->set, 0, NULL);
            last_set = batch->set;
        }
        VK_Entity_DrawBatch(cmd, batch);
    }

    // In a no-refraction frame the complete entity list shares this extract
    // pass. Replay depth-hack sources with the weapon projection and depth
    // range instead of dropping their shell/glowmap emission.
    if (!sampled_depth_rim) {
        VK_Entity_RecordDepthHackBloomEmissionPhase(cmd, extent, phase);
    }
}

void VK_Entity_RecordBeforeLiquid(VkCommandBuffer cmd, const VkExtent2D *extent)
{
    VK_Entity_RecordPhase(cmd, extent, VK_ENTITY_RECORD_BEFORE_LIQUID);
}

void VK_Entity_RecordAfterLiquid(VkCommandBuffer cmd, const VkExtent2D *extent)
{
    // GL submits beams, particles, and flare work after alpha faces, then
    // emits weapon/front-alpha entities. Preserve that phase boundary even
    // though both groups use the same Vulkan alpha pipelines.
    VK_Entity_RecordPhase(cmd, extent, VK_ENTITY_RECORD_POST_LIQUID);
    VK_Entity_RecordPhase(cmd, extent, VK_ENTITY_RECORD_ALPHA_FRONT);
}
