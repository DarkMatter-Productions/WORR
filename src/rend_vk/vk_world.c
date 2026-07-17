/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "vk_world.h"
#include "vk_debug.h"

#include "vk_shadow.h"
#include "vk_ui.h"
#include "vk_world_spv.h"
#include "renderer/view_setup.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    void *vertex_mapped;
} vk_world_frame_buffer_t;

typedef struct {
    float pos[3];
    float uv[2];
    float lm_uv[2];
    uint32_t color;
    float normal[3];
    uint8_t flags;
    uint8_t padding[3];
} vk_world_vertex_t;

typedef struct {
    float time;
    float refraction_scale;
    uint32_t effects_enabled;
    uint32_t refraction_enabled;
    float sky_axis[3][4];
} vk_world_frame_vertex_t;

_Static_assert(sizeof(vk_world_frame_vertex_t) == 64,
               "world animation frame layout must match the Vulkan vertex bindings");

typedef struct {
    uint32_t first_vertex;
    uint32_t vertex_count;
    VkDescriptorSet descriptor_set;
    uint32_t flags;
    uint8_t vertex_flags;
    vec3_t center;
} vk_world_batch_t;

typedef struct vk_world_face_lightmap_s vk_world_face_lightmap_t;

enum {
    VK_WORLD_VERTEX_WARP = BIT(0),
    VK_WORLD_VERTEX_FULLBRIGHT = BIT(1),
    VK_WORLD_VERTEX_ALPHATEST = BIT(2),
    VK_WORLD_VERTEX_FLOWING = BIT(3),
    VK_WORLD_VERTEX_LIGHTMAPPED = BIT(4),
    VK_WORLD_VERTEX_INTENSITY = BIT(5),
    VK_WORLD_VERTEX_GLOWMAP = BIT(6),
    VK_WORLD_VERTEX_SKY = BIT(7),
};

enum {
    VK_WORLD_BATCH_ALPHA = BIT(0),
    VK_WORLD_BATCH_SKY = BIT(1),
    // A bloom extract replay needs only batches whose material has a
    // lightmapped glow companion; all other material emissions are zero.
    VK_WORLD_BATCH_BLOOM_EMISSION = BIT(2),
};

enum {
    VK_WORLD_SKY_FACE_COUNT = 6,
    VK_WORLD_SKY_VERTS_PER_FACE = 6,
    VK_WORLD_SKY_TOTAL_VERTS = VK_WORLD_SKY_FACE_COUNT * VK_WORLD_SKY_VERTS_PER_FACE,
    VK_WORLD_MAX_LIGHTMAP_EXTENT = 513,
};

// 1 = s, 2 = t, 3 = size
static const int8_t vk_world_sky_st_to_vec[VK_WORLD_SKY_FACE_COUNT][3] = {
    { 3, -1, 2 },
    { -3, 1, 2 },
    { 1, 3, 2 },
    { -1, -3, 2 },
    { -2, -1, 3 },
    { 2, -1, -3 },
};

typedef struct {
    vk_context_t *ctx;
    bool initialized;
    bool swapchain_ready;

    bsp_t *bsp;
    char map_name[MAX_QPATH];

    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline_opaque;
    VkPipeline pipeline_fast_lit_opaque;
    VkPipeline pipeline_fast_lit_no_fog_opaque;
    VkPipeline pipeline_fast_lit_glowmap_opaque;
    VkPipeline pipeline_fast_lit_glowmap_no_fog_opaque;
    VkPipeline pipeline_texture_replace_opaque;
    VkPipeline pipeline_texture_replace_no_fog_opaque;
    VkPipeline pipeline_bloom_extract;
    VkPipeline pipeline_alpha;
    VkPipeline pipeline_sky;
    VkPipeline pipeline_sky_array;

    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    size_t vertex_buffer_bytes;
    uint32_t vertex_count;
    vec3_t *showtris_positions;
    uint32_t showtris_position_count;
    vk_world_frame_buffer_t frame_buffers[VK_MAX_FRAMES_IN_FLIGHT];
    vk_world_batch_t *batches;
    uint32_t batch_count;
    uint32_t *alpha_batch_order;
    uint32_t alpha_batch_count;
    bool has_trans_warp;
    qhandle_t lightmap_handle;
    VkDescriptorSet lightmap_descriptor_set;
    qhandle_t sky_lightmap_white_image;
    VkDescriptorSet sky_lightmap_white_set;
    vk_world_face_lightmap_t *face_lms;
    byte *lightmap_rgba;
    int lightmap_atlas_size;
    int lightmap_built_shift;
    int lightmap_built_cap;
    float style_cache[MAX_LIGHTSTYLES];
    bool style_cache_valid;

    qhandle_t sky_images[VK_WORLD_SKY_FACE_COUNT];
    VkDescriptorSet sky_descriptor_sets[VK_WORLD_SKY_FACE_COUNT];
    VkImage sky_array_image;
    VkDeviceMemory sky_array_memory;
    VkImageView sky_array_view;
    VkDescriptorSet sky_array_descriptor_set;
    vec3_t sky_axis;
    float sky_rotate;
    bool sky_autorotate;
    bool sky_enabled;

    VkBuffer sky_vertex_buffer;
    VkDeviceMemory sky_vertex_memory;
    size_t sky_vertex_buffer_bytes;
    uint32_t sky_vertex_count;
    bool sky_geometry_dirty;
    uint32_t sky_face_first_vertex[VK_WORLD_SKY_FACE_COUNT];
    uint32_t sky_face_vertex_count[VK_WORLD_SKY_FACE_COUNT];
    vec2_t sky_uv_inset[VK_WORLD_SKY_FACE_COUNT];
    float sky_world_size;

    const refdef_t *current_fd;
    renderer_view_push_t frame_push;
    bool frame_active;
    bool first_draw_logged;
    byte *world_face_mask;
} vk_world_state_t;

static vk_world_state_t vk_world;
static cvar_t *vk_lightmap_debug;
static cvar_t *vk_drawsky;
static cvar_t *vk_shaders;
static cvar_t *vk_warp_refraction;
static cvar_t *vk_world_fast_lit;
static cvar_t *vk_world_fast_lit_no_fog;

// Lighting gain cvars shared with the OpenGL renderer so both backends read
// the same archived configuration. The native Vulkan swapchain has no
// hardware gamma ramp, so identity_light is always 1.0 here and the full
// modulate value applies in the shaders/CPU paths.
static cvar_t *vk_gl_modulate;
static cvar_t *vk_gl_modulate_world;
static cvar_t *vk_gl_modulate_entities;
static cvar_t *vk_gl_brightness;
static cvar_t *vk_map_overbright_bits;
static cvar_t *vk_map_overbright_cap;
static cvar_t *vk_r_intensity;
static cvar_t *vk_intensity_legacy;
static cvar_t *vk_r_fullbright;
static cvar_t *vk_r_glowmap_intensity;
static bool vk_intensity_syncing;

static void VK_World_IntensityChanged(cvar_t *self)
{
    if (vk_intensity_syncing || !self) {
        return;
    }

    vk_intensity_syncing = true;
    if (self == vk_r_intensity && vk_intensity_legacy) {
        Cvar_SetByVar(vk_intensity_legacy, self->string, FROM_CODE);
    } else if (self == vk_intensity_legacy && vk_r_intensity) {
        Cvar_SetByVar(vk_r_intensity, self->string, FROM_CODE);
    }
    vk_intensity_syncing = false;
}

static void VK_World_RegisterIntensityCvars(void)
{
    vk_intensity_legacy = Cvar_Get("intensity", "1", 0);
    vk_r_intensity = Cvar_Get("r_intensity", vk_intensity_legacy->string, 0);

    // Native Vulkan applies intensity in the receiver shaders, so neither
    // spelling requires a texture reload.
    vk_intensity_legacy->flags &= ~CVAR_FILES;
    vk_r_intensity->flags &= ~CVAR_FILES;

    if ((vk_intensity_legacy->flags & CVAR_MODIFIED) &&
        !(vk_r_intensity->flags & CVAR_MODIFIED)) {
        Cvar_SetByVar(vk_r_intensity, vk_intensity_legacy->string, FROM_CODE);
    } else {
        Cvar_SetByVar(vk_intensity_legacy, vk_r_intensity->string, FROM_CODE);
    }

    vk_intensity_legacy->changed = VK_World_IntensityChanged;
    vk_r_intensity->changed = VK_World_IntensityChanged;
}

static void VK_World_UnregisterIntensityCvars(void)
{
    if (vk_intensity_legacy &&
        vk_intensity_legacy->changed == VK_World_IntensityChanged) {
        vk_intensity_legacy->changed = NULL;
    }
    if (vk_r_intensity && vk_r_intensity->changed == VK_World_IntensityChanged) {
        vk_r_intensity->changed = NULL;
    }
    vk_intensity_syncing = false;
}

static inline bool VK_World_Check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) {
        return true;
    }

    Com_SetLastError(va("Vulkan world %s failed: %d", what, (int)result));
    return false;
}

static bool VK_World_ArrayBytes(size_t count, size_t item_size, size_t *out_size,
                                const char *what)
{
    if (!out_size || (item_size && count > SIZE_MAX / item_size)) {
        Com_SetLastError(va("Vulkan world: %s allocation size overflow", what));
        return false;
    }

    *out_size = count * item_size;
    return true;
}

static bool VK_World_ImageBytes(int width, int height, size_t bytes_per_pixel,
                                size_t *out_size, const char *what)
{
    if (!out_size || width <= 0 || height <= 0 || !bytes_per_pixel) {
        Com_SetLastError(va("Vulkan world: %s invalid image dimensions", what));
        return false;
    }

    size_t pixels;
    if (!VK_World_ArrayBytes((size_t)width, (size_t)height, &pixels, what)) {
        return false;
    }

    return VK_World_ArrayBytes(pixels, bytes_per_pixel, out_size, what);
}

static void VK_World_ClearWorldFaceMask(void)
{
    free(vk_world.world_face_mask);
    vk_world.world_face_mask = NULL;
}

static bool VK_World_BuildWorldFaceMask(const bsp_t *bsp)
{
    VK_World_ClearWorldFaceMask();

    if (!bsp || bsp->numfaces <= 0 || bsp->nummodels < 1 || !bsp->models || !bsp->faces) {
        return false;
    }

    size_t mask_bytes;
    if (!VK_World_ArrayBytes((size_t)bsp->numfaces, sizeof(byte),
                             &mask_bytes, "world-face mask")) {
        return false;
    }

    byte *mask = calloc(1, mask_bytes);
    if (!mask) {
        Com_SetLastError("Vulkan world: out of memory for world-face mask");
        return false;
    }

    const mmodel_t *world_model = &bsp->models[0];
    bool world_range_valid = false;
    if (world_model->firstface && world_model->numfaces > 0) {
        ptrdiff_t first = world_model->firstface - bsp->faces;
        if (first >= 0 && first < bsp->numfaces) {
            int count = min(world_model->numfaces, bsp->numfaces - (int)first);
            if (count > 0) {
                memset(mask + first, 1, (size_t)count);
                world_range_valid = true;
            }
        }
    }

    if (!world_range_valid) {
        memset(mask, 1, mask_bytes);
    }

#if USE_DEBUG
    int inline_faces_cleared = 0;
#endif
    for (int i = 1; i < bsp->nummodels; i++) {
        const mmodel_t *inline_model = &bsp->models[i];
        if (!inline_model->firstface || inline_model->numfaces <= 0) {
            continue;
        }

        ptrdiff_t first = inline_model->firstface - bsp->faces;
        if (first < 0 || first >= bsp->numfaces) {
            continue;
        }

        int count = min(inline_model->numfaces, bsp->numfaces - (int)first);
        if (count <= 0) {
            continue;
        }
#if USE_DEBUG
        for (int j = 0; j < count; j++) {
            if (mask[first + j]) {
                inline_faces_cleared++;
            }
        }
#endif
        memset(mask + first, 0, (size_t)count);
    }

#if USE_DEBUG
    int world_faces = 0;
    for (int i = 0; i < bsp->numfaces; i++) {
        if (mask[i]) {
            world_faces++;
        }
    }

    Com_DPrintf("VK_World_BuildWorldFaceMask: total=%d world=%d inline_cleared=%d world_range_valid=%d\n",
                bsp->numfaces, world_faces, inline_faces_cleared, world_range_valid ? 1 : 0);
#endif

    vk_world.world_face_mask = mask;
    return true;
}

static inline bool VK_World_IsWorldFaceIndex(const bsp_t *bsp, int face_index)
{
    if (!bsp || face_index < 0 || face_index >= bsp->numfaces) {
        return false;
    }
    if (!vk_world.world_face_mask) {
        return true;
    }
    return vk_world.world_face_mask[face_index] != 0;
}

static uint32_t VK_World_FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(vk_world.ctx->physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & BIT(i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return UINT32_MAX;
}

static void VK_World_DestroyVertexBuffer(void)
{
    if (!vk_world.ctx || !vk_world.ctx->device) {
        return;
    }

    VkDevice device = vk_world.ctx->device;

    if (vk_world.vertex_buffer) {
        vkDestroyBuffer(device, vk_world.vertex_buffer, NULL);
        vk_world.vertex_buffer = VK_NULL_HANDLE;
    }

    if (vk_world.vertex_memory) {
        vkFreeMemory(device, vk_world.vertex_memory, NULL);
        vk_world.vertex_memory = VK_NULL_HANDLE;
    }

    vk_world.vertex_buffer_bytes = 0;
    vk_world.vertex_count = 0;
}

static void VK_World_ClearShowTrisPositions(void)
{
    free(vk_world.showtris_positions);
    vk_world.showtris_positions = NULL;
    vk_world.showtris_position_count = 0;
}

static bool VK_World_SetShowTrisPositions(const vk_world_vertex_t *vertices,
                                          uint32_t vertex_count)
{
    VK_World_ClearShowTrisPositions();
    if (!vertices || !vertex_count) {
        return true;
    }

    size_t bytes;
    if (!VK_World_ArrayBytes((size_t)vertex_count,
                             sizeof(*vk_world.showtris_positions), &bytes,
                             "show-tris positions")) {
        return false;
    }
    vec3_t *positions = malloc(bytes);
    if (!positions) {
        Com_SetLastError("Vulkan world: out of memory retaining show-tris positions");
        return false;
    }
    for (uint32_t i = 0; i < vertex_count; ++i) {
        VectorCopy(vertices[i].pos, positions[i]);
    }
    vk_world.showtris_positions = positions;
    vk_world.showtris_position_count = vertex_count;
    return true;
}

static vk_world_frame_buffer_t *VK_World_CurrentFrameBuffer(void)
{
    if (!vk_world.ctx || !vk_world.ctx->frame_count ||
        vk_world.ctx->current_frame >= vk_world.ctx->frame_count) {
        return NULL;
    }
    return &vk_world.frame_buffers[vk_world.ctx->current_frame];
}

static void VK_World_DestroyFrameVertexBuffer(vk_world_frame_buffer_t *frame)
{
    if (!frame || !vk_world.ctx || !vk_world.ctx->device) {
        return;
    }

    VkDevice device = vk_world.ctx->device;

    if (frame->vertex_mapped && frame->vertex_memory) {
        vkUnmapMemory(device, frame->vertex_memory);
        frame->vertex_mapped = NULL;
    }

    if (frame->vertex_buffer) {
        vkDestroyBuffer(device, frame->vertex_buffer, NULL);
        frame->vertex_buffer = VK_NULL_HANDLE;
    }

    if (frame->vertex_memory) {
        vkFreeMemory(device, frame->vertex_memory, NULL);
        frame->vertex_memory = VK_NULL_HANDLE;
    }
}

static void VK_World_DestroySkyVertexBuffer(void)
{
    if (!vk_world.ctx || !vk_world.ctx->device) {
        return;
    }

    VkDevice device = vk_world.ctx->device;

    if (vk_world.sky_vertex_buffer) {
        vkDestroyBuffer(device, vk_world.sky_vertex_buffer, NULL);
        vk_world.sky_vertex_buffer = VK_NULL_HANDLE;
    }

    if (vk_world.sky_vertex_memory) {
        vkFreeMemory(device, vk_world.sky_vertex_memory, NULL);
        vk_world.sky_vertex_memory = VK_NULL_HANDLE;
    }

    vk_world.sky_vertex_buffer_bytes = 0;
    vk_world.sky_vertex_count = 0;
}

static void VK_World_DestroySkyTextureArray(void)
{
    if (!vk_world.ctx || !vk_world.ctx->device) {
        return;
    }

    VkDevice device = vk_world.ctx->device;
    VK_UI_DestroyExternalImageDescriptor(&vk_world.sky_array_descriptor_set);
    if (vk_world.sky_array_view) {
        vkDestroyImageView(device, vk_world.sky_array_view, NULL);
        vk_world.sky_array_view = VK_NULL_HANDLE;
    }
    if (vk_world.sky_array_image) {
        vkDestroyImage(device, vk_world.sky_array_image, NULL);
        vk_world.sky_array_image = VK_NULL_HANDLE;
    }
    if (vk_world.sky_array_memory) {
        vkFreeMemory(device, vk_world.sky_array_memory, NULL);
        vk_world.sky_array_memory = VK_NULL_HANDLE;
    }
}

static bool VK_World_CreateVertexBuffer(size_t size)
{
    VkDevice device = vk_world.ctx->device;

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (!VK_World_Check(vkCreateBuffer(device, &buffer_info, NULL, &vk_world.vertex_buffer),
                        "vkCreateBuffer")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, vk_world.vertex_buffer, &requirements);

    uint32_t memory_index = VK_World_FindMemoryType(requirements.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_index == UINT32_MAX) {
        Com_SetLastError("Vulkan world: suitable device-local vertex memory type not found");
        vkDestroyBuffer(device, vk_world.vertex_buffer, NULL);
        vk_world.vertex_buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (!VK_World_Check(vkAllocateMemory(device, &alloc_info, NULL, &vk_world.vertex_memory),
                        "vkAllocateMemory")) {
        vkDestroyBuffer(device, vk_world.vertex_buffer, NULL);
        vk_world.vertex_buffer = VK_NULL_HANDLE;
        return false;
    }

    if (!VK_World_Check(vkBindBufferMemory(device, vk_world.vertex_buffer, vk_world.vertex_memory, 0),
                        "vkBindBufferMemory")) {
        vkDestroyBuffer(device, vk_world.vertex_buffer, NULL);
        vkFreeMemory(device, vk_world.vertex_memory, NULL);
        vk_world.vertex_buffer = VK_NULL_HANDLE;
        vk_world.vertex_memory = VK_NULL_HANDLE;
        return false;
    }

    vk_world.vertex_buffer_bytes = size;
    return true;
}

static void VK_World_DestroyStagingBuffer(VkBuffer *buffer,
                                          VkDeviceMemory *memory,
                                          void **mapped)
{
    if (!vk_world.ctx || !vk_world.ctx->device) {
        return;
    }

    VkDevice device = vk_world.ctx->device;
    if (*mapped && *memory) {
        vkUnmapMemory(device, *memory);
        *mapped = NULL;
    }
    if (*buffer) {
        vkDestroyBuffer(device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
    }
    if (*memory) {
        vkFreeMemory(device, *memory, NULL);
        *memory = VK_NULL_HANDLE;
    }
}

static bool VK_World_CreateStagingBuffer(size_t size, VkBuffer *out_buffer,
                                         VkDeviceMemory *out_memory,
                                         void **out_mapped)
{
    *out_buffer = VK_NULL_HANDLE;
    *out_memory = VK_NULL_HANDLE;
    *out_mapped = NULL;

    VkDevice device = vk_world.ctx->device;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VK_World_Check(vkCreateBuffer(device, &buffer_info, NULL, out_buffer),
                        "vkCreateBuffer(world staging)")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *out_buffer, &requirements);
    uint32_t memory_index = VK_World_FindMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_index == UINT32_MAX) {
        Com_SetLastError("Vulkan world: suitable staging memory type not found");
        VK_World_DestroyStagingBuffer(out_buffer, out_memory, out_mapped);
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };
    if (!VK_World_Check(vkAllocateMemory(device, &alloc_info, NULL, out_memory),
                        "vkAllocateMemory(world staging)")) {
        VK_World_DestroyStagingBuffer(out_buffer, out_memory, out_mapped);
        return false;
    }
    if (!VK_World_Check(vkBindBufferMemory(device, *out_buffer, *out_memory, 0),
                        "vkBindBufferMemory(world staging)")) {
        VK_World_DestroyStagingBuffer(out_buffer, out_memory, out_mapped);
        return false;
    }
    if (!VK_World_Check(vkMapMemory(device, *out_memory, 0, size, 0, out_mapped),
                        "vkMapMemory(world staging)")) {
        VK_World_DestroyStagingBuffer(out_buffer, out_memory, out_mapped);
        return false;
    }
    return true;
}

static bool VK_World_CopyStagingToVertexBuffer(VkBuffer staging, VkBuffer destination,
                                               size_t size, const char *label)
{
    vk_context_t *ctx = vk_world.ctx;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (!VK_World_Check(vkAllocateCommandBuffers(ctx->device, &alloc_info,
                                                  &command_buffer), label)) {
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (!VK_World_Check(vkBeginCommandBuffer(command_buffer, &begin_info),
                        label)) {
        vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &command_buffer);
        return false;
    }

    const VkBufferCopy copy = { .size = size };
    vkCmdCopyBuffer(command_buffer, staging, destination, 1, &copy);
    const VkBufferMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = destination,
        .size = size,
    };
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, NULL, 1,
                         &barrier, 0, NULL);

    if (!VK_World_Check(vkEndCommandBuffer(command_buffer),
                        label)) {
        vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &command_buffer);
        return false;
    }

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };
    const bool submitted = VK_World_Check(
        vkQueueSubmit(ctx->graphics_queue, 1, &submit_info, VK_NULL_HANDLE),
        label);
    const bool completed = submitted && VK_World_Check(
        vkQueueWaitIdle(ctx->graphics_queue), label);
    vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &command_buffer);
    return completed;
}

static bool VK_World_CreateFrameVertexBuffer(vk_world_frame_buffer_t *frame)
{
    if (!frame) {
        return false;
    }
    VkDevice device = vk_world.ctx->device;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(vk_world_frame_vertex_t),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (!VK_World_Check(vkCreateBuffer(device, &buffer_info, NULL,
                                        &frame->vertex_buffer),
                        "vkCreateBuffer(world animation frame)")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, frame->vertex_buffer,
                                  &requirements);
    uint32_t memory_index = VK_World_FindMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_index == UINT32_MAX) {
        Com_SetLastError("Vulkan world: suitable animation-frame memory type not found");
        VK_World_DestroyFrameVertexBuffer(frame);
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };
    if (!VK_World_Check(vkAllocateMemory(device, &alloc_info, NULL,
                                         &frame->vertex_memory),
                        "vkAllocateMemory(world animation frame)")) {
        VK_World_DestroyFrameVertexBuffer(frame);
        return false;
    }

    if (!VK_World_Check(vkBindBufferMemory(device, frame->vertex_buffer,
                                           frame->vertex_memory, 0),
                        "vkBindBufferMemory(world animation frame)")) {
        VK_World_DestroyFrameVertexBuffer(frame);
        return false;
    }

    if (!VK_World_Check(vkMapMemory(device, frame->vertex_memory, 0,
                                    sizeof(vk_world_frame_vertex_t), 0,
                                    &frame->vertex_mapped),
                        "vkMapMemory(world animation frame)")) {
        VK_World_DestroyFrameVertexBuffer(frame);
        return false;
    }

    return true;
}

static bool VK_World_UploadVertices(const vk_world_vertex_t *vertices, uint32_t vertex_count)
{
    VK_World_DestroyVertexBuffer();

    if (!vertices || !vertex_count) {
        return true;
    }

    size_t bytes;
    if (!VK_World_ArrayBytes((size_t)vertex_count, sizeof(*vertices),
                             &bytes, "vertex upload")) {
        return false;
    }

    if (!VK_World_CreateVertexBuffer(bytes)) {
        return false;
    }

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void *staging_mapped = NULL;
    if (!VK_World_CreateStagingBuffer(bytes, &staging_buffer, &staging_memory,
                                      &staging_mapped)) {
        VK_World_DestroyVertexBuffer();
        return false;
    }

    memcpy(staging_mapped, vertices, bytes);
    if (!VK_World_CopyStagingToVertexBuffer(staging_buffer, vk_world.vertex_buffer,
                                            bytes, "world vertex upload")) {
        VK_World_DestroyStagingBuffer(&staging_buffer, &staging_memory,
                                      &staging_mapped);
        VK_World_DestroyVertexBuffer();
        return false;
    }
    VK_World_DestroyStagingBuffer(&staging_buffer, &staging_memory, &staging_mapped);
    VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_WORLD, bytes);

    vk_world.vertex_count = vertex_count;
    return true;
}

static bool VK_World_UploadSkyVertices(const vk_world_vertex_t *vertices, uint32_t vertex_count)
{
    if (!vertices || !vertex_count) {
        VK_World_DestroySkyVertexBuffer();
        return true;
    }

    size_t bytes;
    if (!VK_World_ArrayBytes((size_t)vertex_count, sizeof(*vertices),
                             &bytes, "sky vertex upload")) {
        return false;
    }

    VK_World_DestroySkyVertexBuffer();

    VkDevice device = vk_world.ctx->device;

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (!VK_World_Check(vkCreateBuffer(device, &buffer_info, NULL, &vk_world.sky_vertex_buffer),
                        "vkCreateBuffer(sky)")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, vk_world.sky_vertex_buffer, &requirements);

    uint32_t memory_index = VK_World_FindMemoryType(requirements.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_index == UINT32_MAX) {
        Com_SetLastError("Vulkan world: suitable device-local sky vertex memory type not found");
        VK_World_DestroySkyVertexBuffer();
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };
    if (!VK_World_Check(vkAllocateMemory(device, &alloc_info, NULL, &vk_world.sky_vertex_memory),
                        "vkAllocateMemory(sky)")) {
        VK_World_DestroySkyVertexBuffer();
        return false;
    }

    if (!VK_World_Check(vkBindBufferMemory(device, vk_world.sky_vertex_buffer,
                                           vk_world.sky_vertex_memory, 0),
                        "vkBindBufferMemory(sky)")) {
        VK_World_DestroySkyVertexBuffer();
        return false;
    }

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void *staging_mapped = NULL;
    if (!VK_World_CreateStagingBuffer(bytes, &staging_buffer, &staging_memory,
                                      &staging_mapped)) {
        VK_World_DestroySkyVertexBuffer();
        return false;
    }

    memcpy(staging_mapped, vertices, bytes);
    if (!VK_World_CopyStagingToVertexBuffer(staging_buffer, vk_world.sky_vertex_buffer,
                                            bytes, "world static sky upload")) {
        VK_World_DestroyStagingBuffer(&staging_buffer, &staging_memory, &staging_mapped);
        VK_World_DestroySkyVertexBuffer();
        return false;
    }
    VK_World_DestroyStagingBuffer(&staging_buffer, &staging_memory, &staging_mapped);
    VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_WORLD, bytes);

    vk_world.sky_vertex_buffer_bytes = bytes;
    vk_world.sky_vertex_count = vertex_count;
    return true;
}

static bool VK_World_CreateSkyTextureArray(void)
{
    if (!vk_world.ctx || !vk_world.ctx->device) {
        return false;
    }

    VkImage source_images[VK_WORLD_SKY_FACE_COUNT] = { VK_NULL_HANDLE };
    int width = 0;
    int height = 0;
    for (int face = 0; face < VK_WORLD_SKY_FACE_COUNT; face++) {
        qhandle_t image = vk_world.sky_images[face];
        int face_width = 0;
        int face_height = 0;
        source_images[face] = VK_UI_GetImage(image);
        VK_UI_GetPicSize(&face_width, &face_height, image);
        if (!source_images[face] || face_width <= 0 || face_height <= 0 ||
            (face && (face_width != width || face_height != height))) {
            return false;
        }
        for (int previous = 0; previous < face; previous++) {
            if (source_images[previous] == source_images[face]) {
                return false;
            }
        }
        width = face_width;
        height = face_height;
    }

    VK_World_DestroySkyTextureArray();
    VkDevice device = vk_world.ctx->device;
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { .width = (uint32_t)width, .height = (uint32_t)height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = VK_WORLD_SKY_FACE_COUNT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!VK_World_Check(vkCreateImage(device, &image_info, NULL, &vk_world.sky_array_image),
                        "vkCreateImage(sky array)")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, vk_world.sky_array_image, &requirements);
    uint32_t memory_index = VK_World_FindMemoryType(requirements.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_index == UINT32_MAX) {
        Com_SetLastError("Vulkan world: suitable device-local sky array memory type not found");
        VK_World_DestroySkyTextureArray();
        return false;
    }
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };
    if (!VK_World_Check(vkAllocateMemory(device, &alloc_info, NULL,
                                         &vk_world.sky_array_memory),
                        "vkAllocateMemory(sky array)") ||
        !VK_World_Check(vkBindImageMemory(device, vk_world.sky_array_image,
                                          vk_world.sky_array_memory, 0),
                        "vkBindImageMemory(sky array)")) {
        VK_World_DestroySkyTextureArray();
        return false;
    }

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk_world.sky_array_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = VK_WORLD_SKY_FACE_COUNT,
        },
    };
    if (!VK_World_Check(vkCreateImageView(device, &view_info, NULL, &vk_world.sky_array_view),
                        "vkCreateImageView(sky array)")) {
        VK_World_DestroySkyTextureArray();
        return false;
    }

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo command_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk_world.ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (!VK_World_Check(vkAllocateCommandBuffers(device, &command_alloc, &command_buffer),
                        "vkAllocateCommandBuffers(sky array)")) {
        VK_World_DestroySkyTextureArray();
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (!VK_World_Check(vkBeginCommandBuffer(command_buffer, &begin_info),
                        "vkBeginCommandBuffer(sky array)")) {
        vkFreeCommandBuffers(device, vk_world.ctx->command_pool, 1, &command_buffer);
        VK_World_DestroySkyTextureArray();
        return false;
    }

    VkImageMemoryBarrier barriers[VK_WORLD_SKY_FACE_COUNT + 1] = { 0 };
    for (int face = 0; face < VK_WORLD_SKY_FACE_COUNT; face++) {
        barriers[face] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = source_images[face],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
    }
    barriers[VK_WORLD_SKY_FACE_COUNT] = (VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk_world.sky_array_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = VK_WORLD_SKY_FACE_COUNT,
        },
    };
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                         q_countof(barriers), barriers);

    for (int face = 0; face < VK_WORLD_SKY_FACE_COUNT; face++) {
        VkImageCopy copy = {
            .srcSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseArrayLayer = (uint32_t)face,
                .layerCount = 1,
            },
            .extent = { .width = (uint32_t)width, .height = (uint32_t)height, .depth = 1 },
        };
        vkCmdCopyImage(command_buffer, source_images[face], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       vk_world.sky_array_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }

    for (int face = 0; face < VK_WORLD_SKY_FACE_COUNT; face++) {
        barriers[face].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barriers[face].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[face].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[face].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    barriers[VK_WORLD_SKY_FACE_COUNT].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[VK_WORLD_SKY_FACE_COUNT].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[VK_WORLD_SKY_FACE_COUNT].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[VK_WORLD_SKY_FACE_COUNT].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL,
                         q_countof(barriers), barriers);

    if (!VK_World_Check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer(sky array)")) {
        vkFreeCommandBuffers(device, vk_world.ctx->command_pool, 1, &command_buffer);
        VK_World_DestroySkyTextureArray();
        return false;
    }
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };
    const bool copied = VK_World_Check(vkQueueSubmit(vk_world.ctx->graphics_queue, 1,
                                                     &submit_info, VK_NULL_HANDLE),
                                       "vkQueueSubmit(sky array)") &&
                        VK_World_Check(vkQueueWaitIdle(vk_world.ctx->graphics_queue),
                                       "vkQueueWaitIdle(sky array)");
    vkFreeCommandBuffers(device, vk_world.ctx->command_pool, 1, &command_buffer);
    if (!copied) {
        VK_World_DestroySkyTextureArray();
        return false;
    }

    vk_world.sky_array_descriptor_set = VK_UI_CreateExternalImageDescriptor(
        vk_world.sky_array_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!vk_world.sky_array_descriptor_set) {
        VK_World_DestroySkyTextureArray();
        return false;
    }
    return true;
}

static void VK_World_ClearBatches(void)
{
    free(vk_world.batches);
    vk_world.batches = NULL;
    vk_world.batch_count = 0;
    free(vk_world.alpha_batch_order);
    vk_world.alpha_batch_order = NULL;
    vk_world.alpha_batch_count = 0;
    vk_world.has_trans_warp = false;
}

static void VK_World_ClearLightmap(void)
{
    if (vk_world.lightmap_handle) {
        VK_UI_UnregisterImage(vk_world.lightmap_handle);
        vk_world.lightmap_handle = 0;
    }

    free(vk_world.face_lms);
    vk_world.face_lms = NULL;
    free(vk_world.lightmap_rgba);
    vk_world.lightmap_rgba = NULL;
    vk_world.lightmap_atlas_size = 0;
    vk_world.lightmap_descriptor_set = VK_NULL_HANDLE;
    vk_world.style_cache_valid = false;
}

static void VK_World_UnsetSky(void)
{
    VK_World_DestroySkyTextureArray();
    for (int i = 0; i < VK_WORLD_SKY_FACE_COUNT; i++) {
        if (vk_world.sky_images[i]) {
            VK_UI_UnregisterImage(vk_world.sky_images[i]);
            vk_world.sky_images[i] = 0;
        }
        vk_world.sky_descriptor_sets[i] = VK_NULL_HANDLE;
    }

    vk_world.sky_enabled = false;
    vk_world.sky_rotate = 0.0f;
    vk_world.sky_autorotate = false;
    memset(vk_world.sky_uv_inset, 0, sizeof(vk_world.sky_uv_inset));
    memset(vk_world.sky_face_first_vertex, 0, sizeof(vk_world.sky_face_first_vertex));
    memset(vk_world.sky_face_vertex_count, 0, sizeof(vk_world.sky_face_vertex_count));
    VectorSet(vk_world.sky_axis, 0.0f, 0.0f, 1.0f);
    VK_World_DestroySkyVertexBuffer();
    vk_world.sky_geometry_dirty = false;
}

static inline float VK_World_SkyU(float s, int axis)
{
    float inset = vk_world.sky_uv_inset[axis][0];
    if (inset <= 0.0f || inset >= 0.5f) {
        inset = 1.0f / 512.0f;
    }
    return Q_clipf((s + 1.0f) * 0.5f, inset, 1.0f - inset);
}

static inline float VK_World_SkyV(float t, int axis)
{
    float inset = vk_world.sky_uv_inset[axis][1];
    if (inset <= 0.0f || inset >= 0.5f) {
        inset = 1.0f / 512.0f;
    }
    return 1.0f - Q_clipf((t + 1.0f) * 0.5f, inset, 1.0f - inset);
}

static void VK_World_BuildSkyFaceVertex(float s, float t, int axis, float size,
                                        float lm_u, float lm_v, vk_world_vertex_t *out)
{
    vec3_t b = { s * size, t * size, size };
    vec3_t v;

    for (int j = 0; j < 3; j++) {
        int k = vk_world_sky_st_to_vec[axis][j];
        v[j] = (k < 0) ? -b[-k - 1] : b[k - 1];
    }

    *out = (vk_world_vertex_t){
        .pos = { v[0], v[1], v[2] },
        .uv = { VK_World_SkyU(s, axis), VK_World_SkyV(t, axis) },
        .lm_uv = { lm_u, lm_v },
        .color = COLOR_RGBA(255, 255, 255, 255).u32,
        .flags = VK_WORLD_VERTEX_FULLBRIGHT | VK_WORLD_VERTEX_SKY,
    };
}

static bool VK_World_UpdateSkyGeometry(void)
{
    if (!vk_world.sky_enabled || vk_world.lightmap_atlas_size <= 0) {
        vk_world.sky_vertex_count = 0;
        return true;
    }

    if (!vk_world.sky_geometry_dirty && vk_world.sky_vertex_buffer &&
        vk_world.sky_vertex_count == VK_WORLD_SKY_TOTAL_VERTS) {
        return true;
    }

    vk_world.sky_vertex_count = 0;

    memset(vk_world.sky_face_first_vertex, 0, sizeof(vk_world.sky_face_first_vertex));
    memset(vk_world.sky_face_vertex_count, 0, sizeof(vk_world.sky_face_vertex_count));

    vk_world_vertex_t verts[VK_WORLD_SKY_TOTAL_VERTS];
    const float lm_u = 0.5f / (float)vk_world.lightmap_atlas_size;
    const float lm_v = 0.5f / (float)vk_world.lightmap_atlas_size;
    const bool use_texture_array = vk_world.sky_array_descriptor_set != VK_NULL_HANDLE;

    const float size = max(512.0f, vk_world.sky_world_size);
    uint32_t out = 0;
    for (int face = 0; face < VK_WORLD_SKY_FACE_COUNT; face++) {
        const float min_s = -1.0f;
        const float max_s = 1.0f;
        const float min_t = -1.0f;
        const float max_t = 1.0f;
        // The dedicated sky-array pipeline reads the face layer from lm_uv.x.
        // Keep the white-lightmap coordinates for the validated six-texture
        // fallback, which continues to use the regular world fragment shader.
        const float face_lm_u = use_texture_array ? (float)face : lm_u;
        const float face_lm_v = use_texture_array ? 0.0f : lm_v;
        vk_world.sky_face_first_vertex[face] = out;
        vk_world.sky_face_vertex_count[face] = VK_WORLD_SKY_VERTS_PER_FACE;
        VK_World_BuildSkyFaceVertex(max_s, min_t, face, size, face_lm_u, face_lm_v,
                                    &verts[out + 0]);
        VK_World_BuildSkyFaceVertex(min_s, min_t, face, size, face_lm_u, face_lm_v,
                                    &verts[out + 1]);
        VK_World_BuildSkyFaceVertex(max_s, max_t, face, size, face_lm_u, face_lm_v,
                                    &verts[out + 2]);
        VK_World_BuildSkyFaceVertex(max_s, max_t, face, size, face_lm_u, face_lm_v,
                                    &verts[out + 3]);
        VK_World_BuildSkyFaceVertex(min_s, min_t, face, size, face_lm_u, face_lm_v,
                                    &verts[out + 4]);
        VK_World_BuildSkyFaceVertex(min_s, max_t, face, size, face_lm_u, face_lm_v,
                                    &verts[out + 5]);
        out += VK_WORLD_SKY_VERTS_PER_FACE;
    }

    if (!VK_World_UploadSkyVertices(verts, out)) {
        return false;
    }

    vk_world.sky_geometry_dirty = false;
    return true;
}

static inline uint32_t VK_World_SurfEdgeVertexIndex(const bsp_t *bsp, const msurfedge_t *surfedge)
{
    const medge_t *edge = &bsp->edges[surfedge->edge];
    return surfedge->vert ? edge->v[1] : edge->v[0];
}

static inline byte VK_World_ColorChannel(float v)
{
    float c = fabsf(v);
    if (c > 1.0f)
        c = 1.0f;
    c = c * 0.75f + 0.25f;
    if (c < 0.0f)
        c = 0.0f;
    if (c > 1.0f)
        c = 1.0f;
    return (byte)(c * 255.0f);
}

static inline float VK_World_LightStyleWhiteForRefdef(const refdef_t *fd, byte style)
{
    if (style == 255)
        return 0.0f;

    if (!fd || !fd->lightstyles)
        return 1.0f;

    return fd->lightstyles[style].white;
}

static inline float VK_World_LightStyleWhite(byte style)
{
    return VK_World_LightStyleWhiteForRefdef(vk_world.current_fd, style);
}

static inline int VK_World_LightmapShift(void)
{
    if (!vk_map_overbright_bits) {
        return 0;
    }
    return Cvar_ClampInteger(vk_map_overbright_bits, 0, 4);
}

static inline int VK_World_LightmapCap(void)
{
    if (!vk_map_overbright_cap) {
        return 255;
    }
    return Cvar_ClampInteger(vk_map_overbright_cap, 0, 255);
}

float VK_World_LightmapModulate(void)
{
    if (!vk_gl_modulate || !vk_gl_modulate_world) {
        return 1.0f;
    }
    return Cvar_ClampValue(vk_gl_modulate, 0, 1e6f) *
           Cvar_ClampValue(vk_gl_modulate_world, 0, 1e6f);
}

float VK_World_LightmapAdd(void)
{
    if (!vk_gl_brightness) {
        return 0.0f;
    }
    return Cvar_ClampValue(vk_gl_brightness, -1, 1);
}

float VK_World_EntityModulate(void)
{
    if (!vk_gl_modulate || !vk_gl_modulate_entities) {
        return 1.0f;
    }
    return Cvar_ClampValue(vk_gl_modulate, 0, 1e6f) *
           Cvar_ClampValue(vk_gl_modulate_entities, 0, 1e6f);
}

float VK_World_Intensity(void)
{
    if (!vk_r_intensity) {
        return 1.0f;
    }
    float value = Cvar_ClampValue(vk_r_intensity, 1.0f, 5.0f);
    if (vk_intensity_legacy && vk_intensity_legacy->value != value) {
        Cvar_SetValue(vk_intensity_legacy, value, FROM_CODE);
    }
    return value;
}

float VK_World_GlowmapIntensity(void)
{
    if (!vk_r_glowmap_intensity) {
        return 1.0f;
    }
    return Cvar_ClampValue(vk_r_glowmap_intensity, 0.0f, 10.0f);
}

bool VK_World_Fullbright(void)
{
    return vk_r_fullbright && vk_r_fullbright->integer;
}

bool VK_World_SurfaceUsesIntensity(const bsp_t *bsp, const mface_t *face)
{
    if (!face || !face->texinfo) {
        return false;
    }

    const surfflags_t flags = face->texinfo->c.flags;
    if (face->lightmap && bsp && bsp->has_bspx) {
        return true;
    }
    if (!(flags & SURF_COLOR_MASK)) {
        return true;
    }
    return !(flags & SURF_TRANS_MASK) &&
           strstr(face->texinfo->name, "lava") != NULL;
}

// Mirrors GL_ShiftLightmapBytes: applies the map overbright shift with a
// hue-preserving clamp, plus the optional hard cap.
static inline void VK_World_ShiftLightmapBytes(const byte in[3], vec3_t out)
{
    int shift = VK_World_LightmapShift();
    int cap = VK_World_LightmapCap();
    int r, g, b;

    if (shift > 0) {
        r = in[0] << shift;
        g = in[1] << shift;
        b = in[2] << shift;

        if ((r | g | b) > 255) {
            int max_c = r > g ? r : g;
            if (b > max_c)
                max_c = b;
            r = r * 255 / max_c;
            g = g * 255 / max_c;
            b = b * 255 / max_c;
        }
    } else {
        r = in[0];
        g = in[1];
        b = in[2];
    }

    if (cap < 255) {
        r = min(r, cap);
        g = min(g, cap);
        b = min(b, cap);
    }

    out[0] = r;
    out[1] = g;
    out[2] = b;
}

static inline void VK_World_AdjustLightColor(vec3_t color)
{
    VectorScale(color, 1.0f / 255.0f, color);
}

// Mirrors GL_AdjustColor for CPU-sampled entity light: brightness add and
// entity modulate on top of the 0..255 style-accumulated sample.
static inline void VK_World_AdjustEntityLightColor(vec3_t color)
{
    float add = VK_World_LightmapAdd();
    float modulate = VK_World_EntityModulate();

    for (int i = 0; i < 3; i++) {
        color[i] = max((color[i] + add) * modulate, 0.0f);
    }
}

static bool VK_World_SampleFaceLightmap(const mface_t *face, float s, float t, vec3_t out_color)
{
    if (!face || !face->lightmap || !out_color || face->lm_width < 1 || face->lm_height < 1) {
        return false;
    }

    int smax = face->lm_width;
    int tmax = face->lm_height;

    if (s < 0.0f || t < 0.0f || s > (float)(smax - 1) || t > (float)(tmax - 1)) {
        return false;
    }

    if (smax < 2 || tmax < 2) {
        const byte *lightmap = face->lightmap;
        VectorClear(out_color);
        for (int i = 0; i < face->numstyles; i++) {
            if (face->styles[i] == 255)
                break;

            vec3_t sample;
            VK_World_ShiftLightmapBytes(lightmap, sample);
            VectorMA(out_color, VK_World_LightStyleWhite(face->styles[i]), sample, out_color);
            lightmap += smax * tmax * 3;
        }
        VK_World_AdjustLightColor(out_color);
        return true;
    }

    int si = (int)s;
    int ti = (int)t;
    float fracu = s - si;
    float fracv = t - ti;

    si = max(0, min(smax - 2, si));
    ti = max(0, min(tmax - 2, ti));

    float w1 = (1.0f - fracu) * (1.0f - fracv);
    float w2 = fracu * (1.0f - fracv);
    float w3 = fracu * fracv;
    float w4 = (1.0f - fracu) * fracv;

    int style_bytes = smax * tmax * 3;
    const byte *lightmap = face->lightmap;

    VectorClear(out_color);

    for (int i = 0; i < face->numstyles; i++) {
        if (face->styles[i] == 255)
            break;

        const byte *b1 = &lightmap[3 * ((ti + 0) * smax + (si + 0))];
        const byte *b2 = &lightmap[3 * ((ti + 0) * smax + (si + 1))];
        const byte *b3 = &lightmap[3 * ((ti + 1) * smax + (si + 1))];
        const byte *b4 = &lightmap[3 * ((ti + 1) * smax + (si + 0))];

        vec3_t c1, c2, c3, c4, blended;
        VK_World_ShiftLightmapBytes(b1, c1);
        VK_World_ShiftLightmapBytes(b2, c2);
        VK_World_ShiftLightmapBytes(b3, c3);
        VK_World_ShiftLightmapBytes(b4, c4);

        blended[0] = w1 * c1[0] + w2 * c2[0] + w3 * c3[0] + w4 * c4[0];
        blended[1] = w1 * c1[1] + w2 * c2[1] + w3 * c3[1] + w4 * c4[1];
        blended[2] = w1 * c1[2] + w2 * c2[2] + w3 * c3[2] + w4 * c4[2];

        VectorMA(out_color, VK_World_LightStyleWhite(face->styles[i]), blended, out_color);
        lightmap += style_bytes;
    }

    VK_World_AdjustLightColor(out_color);
    return true;
}

static bool VK_World_SampleSurfacePointLight(const mface_t *face, const vec3_t point, vec3_t out_color)
{
    if (!face || !point || !out_color || !face->lightmap) {
        return false;
    }

    float s = DotProduct(face->lm_axis[0], point) + face->lm_offset[0];
    float t = DotProduct(face->lm_axis[1], point) + face->lm_offset[1];

    return VK_World_SampleFaceLightmap(face, s, t, out_color);
}

static color_t VK_World_ColorForFace(const mface_t *face)
{
    vec3_t normal;
    VectorCopy(face->plane->normal, normal);

    if (face->drawflags & DSURF_PLANEBACK) {
        VectorNegate(normal, normal);
    }

    color_t color = COLOR_RGBA(VK_World_ColorChannel(normal[0]),
                               VK_World_ColorChannel(normal[1]),
                               VK_World_ColorChannel(normal[2]),
                               255);

    if (face->texinfo && (face->texinfo->c.flags & SURF_WARP)) {
        color = COLOR_RGBA(color.r / 2, color.g / 2, min(255, (int)color.b + 64), 255);
    }

    return color;
}

static bool VK_World_DrawableFace(const mface_t *face)
{
    if (!face || !face->plane || !face->firstsurfedge)
        return false;

    if (face->numsurfedges < 3)
        return false;

    if (!face->texinfo)
        return false;

    surfflags_t flags = face->texinfo->c.flags;
    if (flags & SURF_NODRAW)
        return false;

    return true;
}

struct vk_world_face_lightmap_s {
    bool has_lightmap;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
};

static bool VK_World_AllocAtlasBlock(int atlas_size, int *inuse, int width, int height, int *out_x, int *out_y)
{
    int best = atlas_size;
    int best_x = -1;

    for (int i = 0; i <= atlas_size - width; i++) {
        int best2 = 0;
        int j;
        for (j = 0; j < width; j++) {
            if (inuse[i + j] >= best) {
                break;
            }
            if (inuse[i + j] > best2) {
                best2 = inuse[i + j];
            }
        }

        if (j == width) {
            best = best2;
            best_x = i;
        }
    }

    if (best_x < 0 || best + height > atlas_size) {
        return false;
    }

    for (int i = 0; i < width; i++) {
        inuse[best_x + i] = best + height;
    }

    *out_x = best_x;
    *out_y = best;
    return true;
}

static bool VK_World_InitLightmapAtlasPixels(byte *rgba, int atlas_size)
{
    if (!rgba) {
        Com_SetLastError("Vulkan world: missing lightmap atlas pixels");
        return false;
    }

    size_t atlas_bytes;
    if (!VK_World_ImageBytes(atlas_size, atlas_size, 4,
                             &atlas_bytes, "lightmap atlas pixels")) {
        return false;
    }

    memset(rgba, 0, atlas_bytes);
    rgba[0] = 255;
    rgba[1] = 255;
    rgba[2] = 255;
    rgba[3] = 255;
    return true;
}

static void VK_World_BuildFaceLightmapPixels(const mface_t *face,
                                             const vk_world_face_lightmap_t *face_lm,
                                             int atlas_size, const refdef_t *fd,
                                             byte *rgba)
{
    if (!face || !face_lm || !face_lm->has_lightmap || !face->lightmap || !rgba ||
        atlas_size < 1 || face_lm->width < 1 || face_lm->height < 1) {
        return;
    }

    int w = face_lm->width;
    int h = face_lm->height;
    int x = (int)face_lm->x - 1;
    int y = (int)face_lm->y - 1;

    size_t style_bytes;
    if (!VK_World_ImageBytes(w, h, 3, &style_bytes, "face lightmap style")) {
        return;
    }

    for (int ty = 0; ty < h; ty++) {
        for (int tx = 0; tx < w; tx++) {
            vec3_t light;
            VectorClear(light);
            const byte *style_map = face->lightmap;
            size_t pixel_index = ((size_t)ty * (size_t)w + (size_t)tx) * 3;

            for (int s = 0; s < face->numstyles; s++) {
                if (face->styles[s] == 255) {
                    break;
                }

                vec3_t sample;
                VK_World_ShiftLightmapBytes(style_map + pixel_index, sample);
                VectorMA(light, VK_World_LightStyleWhiteForRefdef(fd, face->styles[s]), sample, light);
                style_map += style_bytes;
            }

            VK_World_AdjustLightColor(light);
            light[0] = max(0.0f, min(8.0f, light[0]));
            light[1] = max(0.0f, min(8.0f, light[1]));
            light[2] = max(0.0f, min(8.0f, light[2]));

            byte *dst = rgba + (((size_t)(y + 1 + ty) * (size_t)atlas_size + (size_t)(x + 1 + tx)) * 4);
            dst[0] = (byte)min(255, (int)(light[0] * 255.0f + 0.5f));
            dst[1] = (byte)min(255, (int)(light[1] * 255.0f + 0.5f));
            dst[2] = (byte)min(255, (int)(light[2] * 255.0f + 0.5f));
            dst[3] = 255;
        }
    }

    for (int tx = 0; tx < w; tx++) {
        byte *dst_top = rgba + (((size_t)y * (size_t)atlas_size + (size_t)(x + 1 + tx)) * 4);
        byte *src_top = rgba + (((size_t)(y + 1) * (size_t)atlas_size + (size_t)(x + 1 + tx)) * 4);
        byte *dst_bottom = rgba + (((size_t)(y + h + 1) * (size_t)atlas_size + (size_t)(x + 1 + tx)) * 4);
        byte *src_bottom = rgba + (((size_t)(y + h) * (size_t)atlas_size + (size_t)(x + 1 + tx)) * 4);
        memcpy(dst_top, src_top, 4);
        memcpy(dst_bottom, src_bottom, 4);
    }

    for (int ty = 0; ty < h; ty++) {
        byte *dst_left = rgba + (((size_t)(y + 1 + ty) * (size_t)atlas_size + (size_t)x) * 4);
        byte *src_left = rgba + (((size_t)(y + 1 + ty) * (size_t)atlas_size + (size_t)(x + 1)) * 4);
        byte *dst_right = rgba + (((size_t)(y + 1 + ty) * (size_t)atlas_size + (size_t)(x + w + 1)) * 4);
        byte *src_right = rgba + (((size_t)(y + 1 + ty) * (size_t)atlas_size + (size_t)(x + w)) * 4);
        memcpy(dst_left, src_left, 4);
        memcpy(dst_right, src_right, 4);
    }

    byte *dst_tl = rgba + (((size_t)y * (size_t)atlas_size + (size_t)x) * 4);
    byte *src_tl = rgba + (((size_t)(y + 1) * (size_t)atlas_size + (size_t)(x + 1)) * 4);
    byte *dst_tr = rgba + (((size_t)y * (size_t)atlas_size + (size_t)(x + w + 1)) * 4);
    byte *src_tr = rgba + (((size_t)(y + 1) * (size_t)atlas_size + (size_t)(x + w)) * 4);
    byte *dst_bl = rgba + (((size_t)(y + h + 1) * (size_t)atlas_size + (size_t)x) * 4);
    byte *src_bl = rgba + (((size_t)(y + h) * (size_t)atlas_size + (size_t)(x + 1)) * 4);
    byte *dst_br = rgba + (((size_t)(y + h + 1) * (size_t)atlas_size + (size_t)(x + w + 1)) * 4);
    byte *src_br = rgba + (((size_t)(y + h) * (size_t)atlas_size + (size_t)(x + w)) * 4);
    memcpy(dst_tl, src_tl, 4);
    memcpy(dst_tr, src_tr, 4);
    memcpy(dst_bl, src_bl, 4);
    memcpy(dst_br, src_br, 4);
}

static bool VK_World_RebuildLightmapAtlasPixels(const bsp_t *bsp,
                                                const vk_world_face_lightmap_t *face_lms,
                                                int atlas_size, const refdef_t *fd,
                                                byte *rgba)
{
    if (!bsp || !face_lms || atlas_size < 1 || !rgba) {
        return false;
    }

    if (!VK_World_InitLightmapAtlasPixels(rgba, atlas_size)) {
        return false;
    }

    for (int i = 0; i < bsp->numfaces; i++) {
        const mface_t *face = &bsp->faces[i];

        if (!VK_World_DrawableFace(face)) {
            continue;
        }

        const vk_world_face_lightmap_t *face_lm = &face_lms[i];
        if (!face_lm->has_lightmap) {
            continue;
        }

        VK_World_BuildFaceLightmapPixels(face, face_lm, atlas_size, fd, rgba);
    }

    return true;
}

static void VK_World_CacheLightStyles(const refdef_t *fd)
{
    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        vk_world.style_cache[i] = VK_World_LightStyleWhiteForRefdef(fd, (byte)i);
    }

    vk_world.style_cache_valid = true;
}

static int VK_World_CollectChangedStyles(const refdef_t *fd, bool changed_styles[MAX_LIGHTSTYLES])
{
    int changed_count = 0;
    memset(changed_styles, 0, sizeof(bool) * MAX_LIGHTSTYLES);

    if (!vk_world.style_cache_valid) {
        for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
            changed_styles[i] = true;
        }
        return MAX_LIGHTSTYLES;
    }

    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        float white = VK_World_LightStyleWhiteForRefdef(fd, (byte)i);
        if (fabsf(white - vk_world.style_cache[i]) > 0.0001f) {
            changed_styles[i] = true;
            changed_count++;
        }
    }

    return changed_count;
}

static bool VK_World_FaceUsesChangedStyle(const mface_t *face,
                                          const bool changed_styles[MAX_LIGHTSTYLES])
{
    if (!face || !changed_styles) {
        return false;
    }

    for (int i = 0; i < face->numstyles; i++) {
        byte style = face->styles[i];
        if (style == 255) {
            break;
        }

        if (changed_styles[style]) {
            return true;
        }
    }

    return false;
}

static bool VK_World_UploadLightmapDirtyRect(int x, int y, int w, int h)
{
    int atlas_size = vk_world.lightmap_atlas_size;
    if (!vk_world.lightmap_rgba || atlas_size <= 0 || w <= 0 || h <= 0 ||
        x < 0 || y < 0 || w > atlas_size || h > atlas_size ||
        x > atlas_size - w || y > atlas_size - h) {
        return false;
    }

    size_t bytes;
    size_t row_bytes;
    if (!VK_World_ImageBytes(w, h, 4, &bytes, "lightmap dirty rect") ||
        !VK_World_ArrayBytes((size_t)w, 4, &row_bytes, "lightmap dirty rect row")) {
        return false;
    }

    byte *rect_rgba = malloc(bytes);
    if (!rect_rgba) {
        Com_SetLastError("Vulkan world: out of memory for lightmap dirty rect upload");
        return false;
    }

    for (int row = 0; row < h; row++) {
        const byte *src = vk_world.lightmap_rgba +
                          (((size_t)(y + row) * (size_t)atlas_size + (size_t)x) * 4);
        byte *dst = rect_rgba + (size_t)row * row_bytes;
        memcpy(dst, src, row_bytes);
    }

    bool ok = VK_UI_UpdateImageRGBASubRect(vk_world.lightmap_handle, x, y, w, h, rect_rgba);
    free(rect_rgba);
    if (!ok) {
        Com_SetLastError("Vulkan world: failed to upload lightmap dirty rect");
        return false;
    }

    VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_WORLD, bytes);
    return true;
}

static bool VK_World_UpdateLightmapStyles(const refdef_t *fd)
{
    if (!vk_world.bsp || !vk_world.lightmap_handle || !vk_world.face_lms ||
        !vk_world.lightmap_rgba || vk_world.lightmap_atlas_size < 1) {
        return true;
    }

    bool changed_styles[MAX_LIGHTSTYLES];
    int changed_style_count = VK_World_CollectChangedStyles(fd, changed_styles);
    bool gains_changed = vk_world.lightmap_built_shift != VK_World_LightmapShift() ||
                         vk_world.lightmap_built_cap != VK_World_LightmapCap();
    if (!changed_style_count && !gains_changed) {
        return true;
    }

    int dirty_min_x = vk_world.lightmap_atlas_size;
    int dirty_min_y = vk_world.lightmap_atlas_size;
    int dirty_max_x = 0;
    int dirty_max_y = 0;
    int updated_faces = 0;

    for (int i = 0; i < vk_world.bsp->numfaces; i++) {
        const mface_t *face = &vk_world.bsp->faces[i];

        if (!VK_World_DrawableFace(face)) {
            continue;
        }

        const vk_world_face_lightmap_t *face_lm = &vk_world.face_lms[i];
        if (!face_lm->has_lightmap ||
            (!gains_changed && !VK_World_FaceUsesChangedStyle(face, changed_styles))) {
            continue;
        }

        VK_World_BuildFaceLightmapPixels(face, face_lm, vk_world.lightmap_atlas_size, fd,
                                         vk_world.lightmap_rgba);

        int face_x = (int)face_lm->x - 1;
        int face_y = (int)face_lm->y - 1;
        int face_w = (int)face_lm->width + 2;
        int face_h = (int)face_lm->height + 2;

        dirty_min_x = min(dirty_min_x, face_x);
        dirty_min_y = min(dirty_min_y, face_y);
        dirty_max_x = max(dirty_max_x, face_x + face_w);
        dirty_max_y = max(dirty_max_y, face_y + face_h);
        updated_faces++;
    }

    if (updated_faces > 0) {
        dirty_min_x = max(0, min(vk_world.lightmap_atlas_size, dirty_min_x));
        dirty_min_y = max(0, min(vk_world.lightmap_atlas_size, dirty_min_y));
        dirty_max_x = max(0, min(vk_world.lightmap_atlas_size, dirty_max_x));
        dirty_max_y = max(0, min(vk_world.lightmap_atlas_size, dirty_max_y));

        int dirty_w = dirty_max_x - dirty_min_x;
        int dirty_h = dirty_max_y - dirty_min_y;
        if (dirty_w > 0 && dirty_h > 0) {
            if (!VK_World_UploadLightmapDirtyRect(dirty_min_x, dirty_min_y, dirty_w, dirty_h)) {
                return false;
            }
        }

        if (vk_lightmap_debug && vk_lightmap_debug->integer > 0) {
            size_t atlas_area;
            size_t rect_area;
            if (!VK_World_ImageBytes(vk_world.lightmap_atlas_size, vk_world.lightmap_atlas_size, 1,
                                     &atlas_area, "lightmap debug atlas area") ||
                !VK_World_ImageBytes(dirty_w, dirty_h, 1,
                                     &rect_area, "lightmap debug dirty rect")) {
                return false;
            }

            float coverage = (100.0f * (float)rect_area) / (float)atlas_area;
            Com_Printf("vk_lightmap_debug: styles=%d faces=%d rect=%dx%d@%d,%d (%.2f%% atlas)\n",
                       changed_style_count, updated_faces, dirty_w, dirty_h,
                       dirty_min_x, dirty_min_y, coverage);
        }
    } else if (vk_lightmap_debug && vk_lightmap_debug->integer > 1) {
        Com_Printf("vk_lightmap_debug: styles changed (%d) but no lightmapped faces referenced them\n",
                   changed_style_count);
    }

    VK_World_CacheLightStyles(fd);
    vk_world.lightmap_built_shift = VK_World_LightmapShift();
    vk_world.lightmap_built_cap = VK_World_LightmapCap();
    return true;
}

// The shared BSP loader supplies DECOUPLED_LM coordinates directly, but
// legacy Quake II faces only contain texture axes and a light-data offset.
// OpenGL derives their 16-unit lightmap grid while building surface vertices;
// Vulkan must do the same before atlas allocation and CPU light queries.
static void VK_World_PrepareLightmapGeometry(bsp_t *bsp)
{
    if (!bsp || !bsp->faces || bsp->numfaces <= 0) {
        return;
    }

    int prepared = 0;
    int rejected = 0;

    for (int face_index = 0; face_index < bsp->numfaces; face_index++) {
        mface_t *face = &bsp->faces[face_index];
        if (!face->lightmap) {
            continue;
        }

        if (!face->texinfo || !face->firstsurfedge || face->numsurfedges < 3) {
            face->lightmap = NULL;
            rejected++;
            continue;
        }

        if (!bsp->lm_decoupled) {
            double mins[2] = { 99999.0, 99999.0 };
            double maxs[2] = { -99999.0, -99999.0 };

            for (int edge_index = 0; edge_index < face->numsurfedges; edge_index++) {
                const msurfedge_t *surfedge = &face->firstsurfedge[edge_index];
                const medge_t *edge = &bsp->edges[surfedge->edge];
                const vec3_t point = {
                    bsp->vertices[edge->v[surfedge->vert]].point[0],
                    bsp->vertices[edge->v[surfedge->vert]].point[1],
                    bsp->vertices[edge->v[surfedge->vert]].point[2],
                };

                for (int axis = 0; axis < 2; axis++) {
                    double tc = (double)point[0] * face->texinfo->axis[axis][0] +
                                (double)point[1] * face->texinfo->axis[axis][1] +
                                (double)point[2] * face->texinfo->axis[axis][2] +
                                face->texinfo->offset[axis];
                    mins[axis] = min(mins[axis], tc);
                    maxs[axis] = max(maxs[axis], tc);
                }
            }

            double block_mins[2] = {
                floor(mins[0] / 16.0), floor(mins[1] / 16.0)
            };
            double block_maxs[2] = {
                ceil(maxs[0] / 16.0), ceil(maxs[1] / 16.0)
            };
            double width = block_maxs[0] - block_mins[0] + 1.0;
            double height = block_maxs[1] - block_mins[1] + 1.0;

            if (!isfinite(width) || !isfinite(height) ||
                width < 1.0 || height < 1.0 ||
                width > VK_WORLD_MAX_LIGHTMAP_EXTENT ||
                height > VK_WORLD_MAX_LIGHTMAP_EXTENT) {
                Com_WPrintf("Vulkan world: bad legacy lightmap extents on face %d: %.0f x %.0f\n",
                            face_index, width, height);
                face->lightmap = NULL;
                rejected++;
                continue;
            }

            VectorScale(face->texinfo->axis[0], 1.0f / 16.0f,
                        face->lm_axis[0]);
            VectorScale(face->texinfo->axis[1], 1.0f / 16.0f,
                        face->lm_axis[1]);
            face->lm_offset[0] = face->texinfo->offset[0] / 16.0f -
                                 (float)block_mins[0];
            face->lm_offset[1] = face->texinfo->offset[1] / 16.0f -
                                 (float)block_mins[1];
            face->lm_scale[0] = 16.0f;
            face->lm_scale[1] = 16.0f;
            face->lm_width = (uint16_t)width;
            face->lm_height = (uint16_t)height;
        } else {
            float s_length = VectorLength(face->lm_axis[0]);
            float t_length = VectorLength(face->lm_axis[1]);
            face->lm_scale[0] = s_length > 0.0f ? 1.0f / s_length : 0.0f;
            face->lm_scale[1] = t_length > 0.0f ? 1.0f / t_length : 0.0f;
        }

        int width = face->lm_width;
        int height = face->lm_height;
        if (width < 1 || height < 1 ||
            width > VK_WORLD_MAX_LIGHTMAP_EXTENT ||
            height > VK_WORLD_MAX_LIGHTMAP_EXTENT) {
            Com_WPrintf("Vulkan world: bad lightmap extents on face %d: %d x %d\n",
                        face_index, width, height);
            face->lightmap = NULL;
            rejected++;
            continue;
        }

        size_t style_bytes = (size_t)width * (size_t)height * 3u;
        size_t required_bytes = style_bytes * (size_t)face->numstyles;
        if (!bsp->lightmap) {
            face->lightmap = NULL;
            rejected++;
            continue;
        }
        ptrdiff_t light_offset = face->lightmap - bsp->lightmap;
        if (light_offset < 0 ||
            (size_t)light_offset > (size_t)bsp->numlightmapbytes ||
            required_bytes > (size_t)bsp->numlightmapbytes - (size_t)light_offset) {
            Com_WPrintf("Vulkan world: bad lightmap bounds on face %d\n", face_index);
            face->lightmap = NULL;
            rejected++;
            continue;
        }

        prepared++;
    }

    Com_DPrintf("VK_World_PrepareLightmapGeometry: mode=%s prepared=%d rejected=%d\n",
                bsp->lm_decoupled ? "decoupled" : "legacy", prepared, rejected);
}

static bool VK_World_BuildLightmapAtlas(const bsp_t *bsp,
                                        const refdef_t *fd,
                                        vk_world_face_lightmap_t **out_faces,
                                        int *out_atlas_size,
                                        byte **out_rgba)
{
    if (!bsp || !out_faces || !out_atlas_size || !out_rgba) {
        return false;
    }

    if (bsp->numfaces <= 0) {
        return false;
    }

    size_t face_lm_bytes;
    if (!VK_World_ArrayBytes((size_t)bsp->numfaces, sizeof(vk_world_face_lightmap_t),
                             &face_lm_bytes, "face lightmap table")) {
        return false;
    }

    vk_world_face_lightmap_t *face_lms = calloc(1, face_lm_bytes);
    if (!face_lms) {
        Com_SetLastError("Vulkan world: out of memory for face lightmap table");
        return false;
    }

    static const int atlas_candidates[] = { 1024, 2048, 4096 };

    for (size_t cand = 0; cand < q_countof(atlas_candidates); cand++) {
        const int atlas_size = atlas_candidates[cand];
        size_t atlas_bytes;
        if (!VK_World_ImageBytes(atlas_size, atlas_size, 4,
                                 &atlas_bytes, "lightmap atlas")) {
            free(face_lms);
            return false;
        }

        size_t inuse_bytes;
        if (!VK_World_ArrayBytes((size_t)atlas_size, sizeof(int),
                                 &inuse_bytes, "lightmap atlas columns")) {
            free(face_lms);
            return false;
        }

        int *inuse = calloc(1, inuse_bytes);
        byte *rgba = calloc(1, atlas_bytes);
        if (!inuse || !rgba) {
            free(inuse);
            free(rgba);
            free(face_lms);
            Com_SetLastError("Vulkan world: out of memory building lightmap atlas");
            return false;
        }

        memset(face_lms, 0, face_lm_bytes);

        // Reserve (0,0) as fullbright texel for surfaces without lightmaps.
        inuse[0] = 1;

        bool failed = false;

        for (int i = 0; i < bsp->numfaces; i++) {
            const mface_t *face = &bsp->faces[i];

            if (!VK_World_DrawableFace(face) || !face->lightmap || face->lm_width < 1 || face->lm_height < 1) {
                continue;
            }

            int w = face->lm_width;
            int h = face->lm_height;
            int block_w = w + 2;
            int block_h = h + 2;

            if (block_w > atlas_size || block_h > atlas_size) {
                failed = true;
                break;
            }

            int x = 0;
            int y = 0;
            if (!VK_World_AllocAtlasBlock(atlas_size, inuse, block_w, block_h, &x, &y)) {
                failed = true;
                break;
            }

            face_lms[i].has_lightmap = true;
            face_lms[i].x = (uint16_t)(x + 1);
            face_lms[i].y = (uint16_t)(y + 1);
            face_lms[i].width = (uint16_t)w;
            face_lms[i].height = (uint16_t)h;
        }

        free(inuse);

        if (!failed) {
            if (VK_World_RebuildLightmapAtlasPixels(bsp, face_lms, atlas_size, fd, rgba)) {
                *out_faces = face_lms;
                *out_atlas_size = atlas_size;
                *out_rgba = rgba;
                return true;
            }
        }

        free(rgba);
    }

    free(face_lms);
    Com_SetLastError("Vulkan world: failed to pack lightmap atlas");
    return false;
}

static qhandle_t VK_World_GetFaceTexture(const bsp_t *bsp, const mface_t *face,
                                         qhandle_t *texture_handles, vec2_t *texture_inv_sizes)
{
    if (!bsp || !face || !face->texinfo || !texture_handles || !texture_inv_sizes) {
        return 0;
    }

    int tex_index = (int)(face->texinfo - bsp->texinfo);
    if (tex_index < 0 || tex_index >= bsp->numtexinfo) {
        return 0;
    }

    if (texture_handles[tex_index]) {
        return texture_handles[tex_index];
    }

    char path[MAX_QPATH];
    imageflags_t flags = IF_REPEAT;
    if (face->texinfo->c.flags & SURF_WARP) {
        flags |= IF_TURBULENT;
    }

    Q_concat(path, sizeof(path), "textures/", face->texinfo->name, ".wal");
    qhandle_t handle = VK_UI_RegisterImage(path, IT_WALL, flags);
    if (!handle) {
        return 0;
    }

    int tex_w = 0;
    int tex_h = 0;
    VK_UI_GetPicSize(&tex_w, &tex_h, handle);
    if (tex_w <= 0 || tex_h <= 0) {
        tex_w = 64;
        tex_h = 64;
    }

    texture_handles[tex_index] = handle;
    texture_inv_sizes[tex_index][0] = 1.0f / (float)tex_w;
    texture_inv_sizes[tex_index][1] = 1.0f / (float)tex_h;
    return handle;
}

static bool VK_World_BuildMesh(vk_world_vertex_t **out_vertices,
                               uint32_t *out_vertex_count,
                               vk_world_batch_t **out_batches, uint32_t *out_batch_count,
                               bool *out_has_trans_warp,
                               const vk_world_face_lightmap_t *face_lms,
                               int atlas_size)
{
    if (!out_vertices || !out_vertex_count || !out_batches || !out_batch_count ||
        !out_has_trans_warp ||
        !face_lms || atlas_size < 1 || !vk_world.bsp) {
        return false;
    }

    const bsp_t *bsp = vk_world.bsp;
    if (bsp->numtexinfo <= 0) {
        Com_SetLastError("Vulkan world: map has no texinfo");
        return false;
    }

    uint64_t triangle_count = 0;
    for (int i = 0; i < bsp->numfaces; ++i) {
        if (!VK_World_IsWorldFaceIndex(bsp, i)) {
            continue;
        }
        const mface_t *face = &bsp->faces[i];

        if (!VK_World_DrawableFace(face)) {
            continue;
        }

        triangle_count += (uint64_t)(face->numsurfedges - 2);
    }

    if (!triangle_count) {
        *out_vertices = NULL;
        *out_vertex_count = 0;
        *out_batches = NULL;
        *out_batch_count = 0;
        *out_has_trans_warp = false;
        return true;
    }

    if (triangle_count > UINT32_MAX / 3) {
        Com_SetLastError("Vulkan world: triangle count overflow");
        return false;
    }

    uint32_t vertex_count = (uint32_t)triangle_count * 3;
    size_t vertex_bytes;
    if (!VK_World_ArrayBytes((size_t)vertex_count, sizeof(vk_world_vertex_t),
                             &vertex_bytes, "mesh vertex")) {
        return false;
    }

    vk_world_vertex_t *vertices = malloc(vertex_bytes);
    if (!vertices) {
        Com_SetLastError("Vulkan world: out of memory building mesh");
        return false;
    }

    size_t batch_bytes;
    if (!VK_World_ArrayBytes((size_t)triangle_count, sizeof(vk_world_batch_t),
                             &batch_bytes, "mesh batch")) {
        free(vertices);
        return false;
    }

    vk_world_batch_t *batches = malloc(batch_bytes);
    if (!batches) {
        free(vertices);
        Com_SetLastError("Vulkan world: out of memory building draw batches");
        return false;
    }

    size_t texture_handle_bytes;
    size_t texture_inv_size_bytes;
    if (!VK_World_ArrayBytes((size_t)bsp->numtexinfo, sizeof(qhandle_t),
                             &texture_handle_bytes, "texture handle map") ||
        !VK_World_ArrayBytes((size_t)bsp->numtexinfo, sizeof(vec2_t),
                             &texture_inv_size_bytes, "texture size map")) {
        free(batches);
        free(vertices);
        return false;
    }

    qhandle_t *texture_handles = calloc(1, texture_handle_bytes);
    vec2_t *texture_inv_sizes = calloc(1, texture_inv_size_bytes);
    if (!texture_handles || !texture_inv_sizes) {
        free(texture_handles);
        free(texture_inv_sizes);
        free(batches);
        free(vertices);
        Com_SetLastError("Vulkan world: out of memory building texture map");
        return false;
    }

    uint32_t out = 0;
    uint32_t batch_out = 0;
    bool has_trans_warp = false;
#if USE_DEBUG
    uint32_t lightmapped_vertices = 0;
#endif

    for (int i = 0; i < bsp->numfaces; ++i) {
        if (!VK_World_IsWorldFaceIndex(bsp, i)) {
            continue;
        }
        const mface_t *face = &bsp->faces[i];

        if (!VK_World_DrawableFace(face)) {
            continue;
        }

        qhandle_t texture_handle =
            VK_World_GetFaceTexture(bsp, face, texture_handles, texture_inv_sizes);
        VkDescriptorSet descriptor_set = VK_UI_GetDescriptorSetForImage(texture_handle);
        const bool has_glowmap = VK_UI_HasGlowmap(texture_handle);
        if (!descriptor_set) {
            continue;
        }

        int tex_index = (int)(face->texinfo - bsp->texinfo);
        float inv_tex_w = 1.0f;
        float inv_tex_h = 1.0f;
        if (tex_index >= 0 && tex_index < bsp->numtexinfo) {
            inv_tex_w = texture_inv_sizes[tex_index][0];
            inv_tex_h = texture_inv_sizes[tex_index][1];
        }

        surfflags_t surf_flags = face->texinfo->c.flags;
        uint32_t batch_flags = 0;
        uint8_t vertex_flags = 0;

        if (surf_flags & SURF_SKY) {
            batch_flags |= VK_WORLD_BATCH_SKY;
            vertex_flags |= VK_WORLD_VERTEX_FULLBRIGHT | VK_WORLD_VERTEX_SKY;
        }

        if (surf_flags & SURF_WARP) {
            vertex_flags |= VK_WORLD_VERTEX_WARP;
        }
        if (surf_flags & SURF_ALPHATEST) {
            vertex_flags |= VK_WORLD_VERTEX_ALPHATEST;
        }
        if (surf_flags & SURF_FLOWING) {
            vertex_flags |= VK_WORLD_VERTEX_FLOWING;
        }

        float alpha = 1.0f;
        if (surf_flags & SURF_TRANS33) {
            alpha = 0.33f;
            batch_flags |= VK_WORLD_BATCH_ALPHA;
        } else if (surf_flags & SURF_TRANS66) {
            alpha = 0.66f;
            batch_flags |= VK_WORLD_BATCH_ALPHA;
        }
        if ((surf_flags & SURF_WARP) && (surf_flags & SURF_TRANS_MASK)) {
            has_trans_warp = true;
        }

        uint8_t base_alpha = (uint8_t)Q_clipf(alpha * 255.0f + 0.5f, 0.0f, 255.0f);
        color_t modulate_color = COLOR_RGBA(255, 255, 255, base_alpha);
        vec3_t face_normal = { 0.0f, 0.0f, 1.0f };
        if (face->plane) {
            VectorCopy(face->plane->normal, face_normal);
            if (face->drawflags & DSURF_PLANEBACK) {
                VectorInverse(face_normal);
            }
        }

        const vk_world_face_lightmap_t *face_lm = &face_lms[i];
        if (face_lm->has_lightmap) {
            vertex_flags |= VK_WORLD_VERTEX_LIGHTMAPPED;
            if (has_glowmap) {
                vertex_flags |= VK_WORLD_VERTEX_GLOWMAP;
                batch_flags |= VK_WORLD_BATCH_BLOOM_EMISSION;
            }
        }
        if (VK_World_SurfaceUsesIntensity(bsp, face)) {
            vertex_flags |= VK_WORLD_VERTEX_INTENSITY;
        }
        float fallback_lm_u = 0.5f / (float)atlas_size;
        float fallback_lm_v = 0.5f / (float)atlas_size;

        const msurfedge_t *surfedges = face->firstsurfedge;
        uint32_t i0 = VK_World_SurfEdgeVertexIndex(bsp, &surfedges[0]);
        const mvertex_t *v0 = &bsp->vertices[i0];
        vec3_t face_center;
        VectorClear(face_center);
        for (int edge = 0; edge < face->numsurfedges; edge++) {
            uint32_t vertex_index =
                VK_World_SurfEdgeVertexIndex(bsp, &surfedges[edge]);
            VectorAdd(face_center, bsp->vertices[vertex_index].point,
                      face_center);
        }
        VectorScale(face_center, 1.0f / (float)face->numsurfedges,
                    face_center);

        for (int j = 1; j < face->numsurfedges - 1; ++j) {
            uint32_t i1 = VK_World_SurfEdgeVertexIndex(bsp, &surfedges[j]);
            uint32_t i2 = VK_World_SurfEdgeVertexIndex(bsp, &surfedges[j + 1]);
            const mvertex_t *v1 = &bsp->vertices[i1];
            const mvertex_t *v2 = &bsp->vertices[i2];

            if (batch_out == 0 ||
                batches[batch_out - 1].descriptor_set != descriptor_set ||
                batches[batch_out - 1].flags != batch_flags ||
                batches[batch_out - 1].vertex_flags != vertex_flags ||
                ((batch_flags & VK_WORLD_BATCH_ALPHA) && j == 1)) {
                batches[batch_out++] = (vk_world_batch_t){
                    .first_vertex = out,
                    .vertex_count = 0,
                    .descriptor_set = descriptor_set,
                    .flags = batch_flags,
                    .vertex_flags = vertex_flags,
                    .center = { face_center[0], face_center[1], face_center[2] },
                };
            }

            float u0 = DotProduct(v0->point, face->texinfo->axis[0]) + face->texinfo->offset[0];
            float t0 = DotProduct(v0->point, face->texinfo->axis[1]) + face->texinfo->offset[1];
            float u1 = DotProduct(v1->point, face->texinfo->axis[0]) + face->texinfo->offset[0];
            float t1 = DotProduct(v1->point, face->texinfo->axis[1]) + face->texinfo->offset[1];
            float u2 = DotProduct(v2->point, face->texinfo->axis[0]) + face->texinfo->offset[0];
            float t2 = DotProduct(v2->point, face->texinfo->axis[1]) + face->texinfo->offset[1];

            float uv0[2] = { u0 * inv_tex_w, t0 * inv_tex_h };
            float uv1[2] = { u1 * inv_tex_w, t1 * inv_tex_h };
            float uv2[2] = { u2 * inv_tex_w, t2 * inv_tex_h };

            float lm_u0 = fallback_lm_u;
            float lm_v0 = fallback_lm_v;
            float lm_u1 = fallback_lm_u;
            float lm_v1 = fallback_lm_v;
            float lm_u2 = fallback_lm_u;
            float lm_v2 = fallback_lm_v;

            if (!(batch_flags & VK_WORLD_BATCH_SKY) && face_lm->has_lightmap) {
                float s0 = DotProduct(face->lm_axis[0], v0->point) + face->lm_offset[0];
                float lt0 = DotProduct(face->lm_axis[1], v0->point) + face->lm_offset[1];
                float s1 = DotProduct(face->lm_axis[0], v1->point) + face->lm_offset[0];
                float lt1 = DotProduct(face->lm_axis[1], v1->point) + face->lm_offset[1];
                float s2 = DotProduct(face->lm_axis[0], v2->point) + face->lm_offset[0];
                float lt2 = DotProduct(face->lm_axis[1], v2->point) + face->lm_offset[1];

                s0 = max(0.0f, min((float)(face_lm->width - 1), s0));
                lt0 = max(0.0f, min((float)(face_lm->height - 1), lt0));
                s1 = max(0.0f, min((float)(face_lm->width - 1), s1));
                lt1 = max(0.0f, min((float)(face_lm->height - 1), lt1));
                s2 = max(0.0f, min((float)(face_lm->width - 1), s2));
                lt2 = max(0.0f, min((float)(face_lm->height - 1), lt2));

                lm_u0 = ((float)face_lm->x + s0 + 0.5f) / (float)atlas_size;
                lm_v0 = ((float)face_lm->y + lt0 + 0.5f) / (float)atlas_size;
                lm_u1 = ((float)face_lm->x + s1 + 0.5f) / (float)atlas_size;
                lm_v1 = ((float)face_lm->y + lt1 + 0.5f) / (float)atlas_size;
                lm_u2 = ((float)face_lm->x + s2 + 0.5f) / (float)atlas_size;
                lm_v2 = ((float)face_lm->y + lt2 + 0.5f) / (float)atlas_size;
            }

            vertices[out++] = (vk_world_vertex_t){
                .pos = { v0->point[0], v0->point[1], v0->point[2] },
                .uv = { uv0[0], uv0[1] },
                .lm_uv = { lm_u0, lm_v0 },
                .color = modulate_color.u32,
                .normal = { face_normal[0], face_normal[1], face_normal[2] },
                .flags = vertex_flags,
            };
            vertices[out++] = (vk_world_vertex_t){
                .pos = { v1->point[0], v1->point[1], v1->point[2] },
                .uv = { uv1[0], uv1[1] },
                .lm_uv = { lm_u1, lm_v1 },
                .color = modulate_color.u32,
                .normal = { face_normal[0], face_normal[1], face_normal[2] },
                .flags = vertex_flags,
            };
            vertices[out++] = (vk_world_vertex_t){
                .pos = { v2->point[0], v2->point[1], v2->point[2] },
                .uv = { uv2[0], uv2[1] },
                .lm_uv = { lm_u2, lm_v2 },
                .color = modulate_color.u32,
                .normal = { face_normal[0], face_normal[1], face_normal[2] },
                .flags = vertex_flags,
            };

            if (!(batch_flags & VK_WORLD_BATCH_SKY) && face_lm->has_lightmap) {
#if USE_DEBUG
                lightmapped_vertices += 3;
#endif
            }

            batches[batch_out - 1].vertex_count += 3;
        }
    }

    free(texture_handles);
    free(texture_inv_sizes);

    if (out == 0) {
        free(vertices);
        vertices = NULL;
    } else if (out < vertex_count) {
        size_t shrink_bytes;
        if (!VK_World_ArrayBytes((size_t)out, sizeof(*vertices),
                                 &shrink_bytes, "mesh vertex shrink")) {
            free(vertices);
            free(batches);
            return false;
        }

        vk_world_vertex_t *shrunk_vertices =
            realloc(vertices, shrink_bytes);
        if (shrunk_vertices) {
            vertices = shrunk_vertices;
        }
    }

    if (batch_out == 0) {
        free(batches);
        batches = NULL;
    } else if (batch_out < triangle_count) {
        size_t shrink_bytes;
        if (!VK_World_ArrayBytes((size_t)batch_out, sizeof(*batches),
                                 &shrink_bytes, "mesh batch shrink")) {
            free(vertices);
            free(batches);
            return false;
        }

        vk_world_batch_t *shrunk_batches =
            realloc(batches, shrink_bytes);
        if (shrunk_batches) {
            batches = shrunk_batches;
        }
    }

    *out_vertices = vertices;
    *out_vertex_count = out;
    *out_batches = batches;
    *out_batch_count = batch_out;
    *out_has_trans_warp = has_trans_warp;
#if USE_DEBUG
    Com_DPrintf("VK_World_BuildMesh: vertices=%u batches=%u lightmapped=%u\n",
                out, batch_out, lightmapped_vertices);
#endif
    return true;
}

static void VK_World_FreeBSP(void)
{
    VK_World_ClearWorldFaceMask();

    if (vk_world.bsp) {
        BSP_Free(vk_world.bsp);
        vk_world.bsp = NULL;
    }

    vk_world.map_name[0] = '\0';
}

static inline void VK_World_AddDynamicLightsAtPoint(const vec3_t origin, vec3_t light)
{
    if (!vk_world.current_fd || !vk_world.current_fd->dlights) {
        return;
    }

    for (int i = 0; i < vk_world.current_fd->num_dlights; i++) {
        const dlight_t *dl = vk_world.current_fd->dlights + i;
        float f = dl->radius - DLIGHT_CUTOFF - Distance(dl->origin, origin);
        if (f > 0.0f) {
            f *= (1.0f / 255.0f);
            VectorMA(light, f * dl->intensity, dl->color, light);
        }
    }
}

static inline void VK_World_ClampLight(vec3_t light)
{
    light[0] = max(0.0f, min(8.0f, light[0]));
    light[1] = max(0.0f, min(8.0f, light[1]));
    light[2] = max(0.0f, min(8.0f, light[2]));
}

static bool VK_World_UpdateAnimationFrame(const refdef_t *fd)
{
    vk_world_frame_buffer_t *frame_buffer = VK_World_CurrentFrameBuffer();
    if (!fd || !frame_buffer || !frame_buffer->vertex_mapped) {
        Com_SetLastError("Vulkan world: animation frame buffer is unavailable");
        return false;
    }

    const bool effects_enabled = !vk_shaders || vk_shaders->integer != 0;
    const float refraction_scale = vk_warp_refraction
        ? Q_clipf(vk_warp_refraction->value, 0.0f, 2.0f) : 0.0f;
    const bool refraction_enabled = effects_enabled && refraction_scale > 0.0f &&
        vk_world.has_trans_warp && vk_world.ctx &&
        vk_world.ctx->frame_count &&
        vk_world.ctx->frames[vk_world.ctx->current_frame].liquid_scene_image &&
        vk_world.ctx->frames[vk_world.ctx->current_frame].liquid_scene_descriptor_set;
    vk_world_frame_vertex_t frame = {
        .time = effects_enabled ? fd->time : 0.0f,
        .refraction_scale = refraction_enabled ? refraction_scale : 0.0f,
        .effects_enabled = effects_enabled ? 1u : 0u,
        .refraction_enabled = refraction_enabled ? 1u : 0u,
        .sky_axis = {
            { 1.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f, 0.0f },
        },
    };
    if (vk_world.sky_enabled && vk_world.sky_rotate != 0.0f) {
        vec3_t sky_matrix[3];
        const float degrees = vk_world.sky_autorotate
            ? fd->time * vk_world.sky_rotate
            : vk_world.sky_rotate;
        SetupRotationMatrix(sky_matrix, vk_world.sky_axis, degrees);
        for (int row = 0; row < 3; row++) {
            frame.sky_axis[row][0] = sky_matrix[row][0];
            frame.sky_axis[row][1] = sky_matrix[row][1];
            frame.sky_axis[row][2] = sky_matrix[row][2];
        }
    }
    memcpy(frame_buffer->vertex_mapped, &frame, sizeof(frame));
    VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_WORLD, sizeof(frame));
    return true;
}

static void VK_World_ClearFrame(void)
{
    vk_world.current_fd = NULL;
    memset(&vk_world.frame_push, 0, sizeof(vk_world.frame_push));
    vk_world.frame_active = false;
}

static bool VK_World_CreatePipelineVariant(vk_context_t *ctx, bool alpha_blend,
                                           bool depth_test, VkCullModeFlags cull_mode,
                                           bool sky_array, bool bloom_extract,
                                           bool fast_lit, bool fast_lit_glowmap,
                                           bool fast_lit_no_fog,
                                           bool fast_lit_glowmap_no_fog,
                                           bool texture_replace,
                                           bool texture_replace_no_fog,
                                           VkPipeline *out_pipeline)
{
    VkShaderModule vert_shader = VK_NULL_HANDLE;
    VkShaderModule frag_shader = VK_NULL_HANDLE;

    VkShaderModuleCreateInfo vert_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_world_animated_vert_spv_size,
        .pCode = vk_world_animated_vert_spv,
    };

    if (!VK_World_Check(vkCreateShaderModule(ctx->device, &vert_info, NULL, &vert_shader),
                        "vkCreateShaderModule(world vert)")) {
        return false;
    }

    const uint32_t *fragment_spv = fast_lit_no_fog ? vk_world_fast_lit_no_fog_frag_spv
        : fast_lit_glowmap_no_fog ? vk_world_fast_lit_glowmap_no_fog_frag_spv
        : fast_lit ? vk_world_fast_lit_frag_spv
        : fast_lit_glowmap ? vk_world_fast_lit_glowmap_frag_spv
        : texture_replace_no_fog ? vk_world_texture_replace_no_fog_frag_spv
        : texture_replace ? vk_world_texture_replace_frag_spv
        : bloom_extract ? vk_world_bloom_extract_frag_spv
        : sky_array ? vk_world_sky_frag_spv : vk_world_animated_frag_spv;
    const size_t fragment_spv_size = fast_lit_no_fog ? vk_world_fast_lit_no_fog_frag_spv_size
        : fast_lit_glowmap_no_fog ? vk_world_fast_lit_glowmap_no_fog_frag_spv_size
        : fast_lit ? vk_world_fast_lit_frag_spv_size
        : fast_lit_glowmap ? vk_world_fast_lit_glowmap_frag_spv_size
        : texture_replace_no_fog ? vk_world_texture_replace_no_fog_frag_spv_size
        : texture_replace ? vk_world_texture_replace_frag_spv_size
        : bloom_extract ? vk_world_bloom_extract_frag_spv_size
        : sky_array ? vk_world_sky_frag_spv_size : vk_world_animated_frag_spv_size;
    VkShaderModuleCreateInfo frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fragment_spv_size,
        .pCode = fragment_spv,
    };

    if (!VK_World_Check(vkCreateShaderModule(ctx->device, &frag_info, NULL, &frag_shader),
                        "vkCreateShaderModule(world frag)")) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo shader_stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_shader,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_shader,
            .pName = "main",
        },
    };

    VkVertexInputBindingDescription bindings[2] = {
        {
            .binding = 0,
            .stride = sizeof(vk_world_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        {
            .binding = 1,
            .stride = sizeof(vk_world_frame_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        },
    };

    VkVertexInputAttributeDescription attrs[13] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_world_vertex_t, pos),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_world_vertex_t, uv),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_world_vertex_t, lm_uv),
        },
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = offsetof(vk_world_vertex_t, color),
        },
        {
            .location = 4,
            .binding = 0,
            .format = VK_FORMAT_R8_UINT,
            .offset = offsetof(vk_world_vertex_t, flags),
        },
        {
            .location = 5,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_world_vertex_t, normal),
        },
        {
            .location = 6,
            .binding = 1,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = offsetof(vk_world_frame_vertex_t, time),
        },
        {
            .location = 7,
            .binding = 1,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = offsetof(vk_world_frame_vertex_t, refraction_scale),
        },
        {
            .location = 8,
            .binding = 1,
            .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(vk_world_frame_vertex_t, effects_enabled),
        },
        {
            .location = 9,
            .binding = 1,
            .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(vk_world_frame_vertex_t, refraction_enabled),
        },
        {
            .location = 10,
            .binding = 1,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_world_frame_vertex_t, sky_axis[0]),
        },
        {
            .location = 11,
            .binding = 1,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_world_frame_vertex_t, sky_axis[1]),
        },
        {
            .location = 12,
            .binding = 1,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_world_frame_vertex_t, sky_axis[2]),
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = q_countof(bindings),
        .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = q_countof(attrs),
        .pVertexAttributeDescriptions = attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = cull_mode,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = depth_test && !alpha_blend && !bloom_extract
            ? VK_TRUE : VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = alpha_blend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                          VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = q_countof(dynamic_states),
        .pDynamicStates = dynamic_states,
    };

    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(shader_stages),
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = vk_world.pipeline_layout,
        .renderPass = bloom_extract ? ctx->bloom_extract_render_pass : ctx->scene_render_pass,
        .subpass = 0,
    };

    const char *pipeline_name = fast_lit_no_fog
        ? "vkCreateGraphicsPipelines(world fast lit no fog)"
        : fast_lit_glowmap_no_fog
        ? "vkCreateGraphicsPipelines(world fast lit glowmap no fog)"
        : fast_lit ? "vkCreateGraphicsPipelines(world fast lit)" :
        fast_lit_glowmap ? "vkCreateGraphicsPipelines(world fast lit glowmap)" :
        texture_replace_no_fog
        ? "vkCreateGraphicsPipelines(world texture replace no fog)" :
        texture_replace ? "vkCreateGraphicsPipelines(world texture replace)" :
        bloom_extract ? "vkCreateGraphicsPipelines(world bloom extract)" :
        sky_array ? "vkCreateGraphicsPipelines(world sky array)" :
        alpha_blend ? "vkCreateGraphicsPipelines(world alpha)" :
        "vkCreateGraphicsPipelines(world opaque)";
    bool ok = VK_World_Check(vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                                       &info, NULL, out_pipeline),
                             pipeline_name);

    vkDestroyShaderModule(ctx->device, vert_shader, NULL);
    vkDestroyShaderModule(ctx->device, frag_shader, NULL);

    return ok;
}

bool VK_World_Init(vk_context_t *ctx)
{
    memset(&vk_world, 0, sizeof(vk_world));

    if (!vk_lightmap_debug) {
        vk_lightmap_debug = Cvar_Get("vk_lightmap_debug", "0", 0);
    }
    if (!vk_drawsky) {
        vk_drawsky = Cvar_Get("vk_drawsky", "1", 0);
    }
    if (!vk_shaders) {
        vk_shaders = Cvar_Get("vk_shaders", "1", 0);
    }
    if (!vk_warp_refraction) {
        vk_warp_refraction = Cvar_Get("vk_warp_refraction", "0.1", 0);
    }
    if (!vk_world_fast_lit) {
        vk_world_fast_lit = Cvar_Get("vk_world_fast_lit", "1", CVAR_ARCHIVE);
    }
    if (!vk_world_fast_lit_no_fog) {
        vk_world_fast_lit_no_fog = Cvar_Get("vk_world_fast_lit_no_fog", "1",
                                            CVAR_ARCHIVE);
    }
    if (!vk_gl_modulate) {
        vk_gl_modulate = Cvar_Get("gl_modulate", "2", CVAR_ARCHIVE);
    }
    if (!vk_gl_modulate_world) {
        vk_gl_modulate_world = Cvar_Get("gl_modulate_world", "1", 0);
    }
    if (!vk_gl_modulate_entities) {
        vk_gl_modulate_entities = Cvar_Get("gl_modulate_entities", "1", 0);
    }
    if (!vk_gl_brightness) {
        vk_gl_brightness = Cvar_Get("gl_brightness", "0", 0);
    }
    if (!vk_map_overbright_bits) {
        vk_map_overbright_bits = Cvar_Get("r_map_overbright_bits", "0",
                                          CVAR_ARCHIVE | CVAR_FILES);
    }
    if (!vk_map_overbright_cap) {
        vk_map_overbright_cap = Cvar_Get("r_map_overbright_cap", "255",
                                         CVAR_ARCHIVE | CVAR_FILES);
    }
    if (!vk_r_fullbright) {
        vk_r_fullbright = Cvar_Get("r_fullbright", "0", CVAR_CHEAT);
    }
    if (!vk_r_glowmap_intensity) {
        vk_r_glowmap_intensity = Cvar_Get("r_glowmap_intensity", "1", 0);
    }
    VK_World_RegisterIntensityCvars();

    if (!ctx) {
        Com_SetLastError("Vulkan world: context is missing");
        return false;
    }

    vk_world.ctx = ctx;

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(renderer_view_push_t),
    };

    VkDescriptorSetLayout ui_set_layout = VK_UI_GetDescriptorSetLayout();
    if (!ui_set_layout) {
        Com_SetLastError("Vulkan world: UI descriptor set layout is unavailable");
        return false;
    }
    VkDescriptorSetLayout shadow_set_layout = VK_Shadow_GetDescriptorSetLayout();
    if (!shadow_set_layout) {
        Com_SetLastError("Vulkan world: shadow descriptor set layout is unavailable");
        return false;
    }

    VkDescriptorSetLayout set_layouts[4] = {
        ui_set_layout,
        ui_set_layout,
        shadow_set_layout,
        ui_set_layout,
    };

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = q_countof(set_layouts),
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };

    if (!VK_World_Check(vkCreatePipelineLayout(ctx->device, &layout_info, NULL,
                                               &vk_world.pipeline_layout),
                        "vkCreatePipelineLayout(world)")) {
        return false;
    }

    static byte white_rgba[4] = { 255, 255, 255, 255 };
    vk_world.sky_lightmap_white_image = VK_UI_RegisterRawImage("**vk_world_sky_lm_white**",
                                                                1, 1, white_rgba,
                                                                IT_PIC, IF_PERMANENT | IF_OPAQUE);
    vk_world.sky_lightmap_white_set = VK_UI_GetDescriptorSetForImage(vk_world.sky_lightmap_white_image);
    if (!vk_world.sky_lightmap_white_set) {
        Com_SetLastError("Vulkan world: sky white lightmap descriptor unavailable");
        VK_World_Shutdown(ctx);
        return false;
    }

    VectorSet(vk_world.sky_axis, 0.0f, 0.0f, 1.0f);
    vk_world.sky_world_size = 4096.0f;
    vk_world.initialized = true;
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (!VK_World_CreateFrameVertexBuffer(&vk_world.frame_buffers[i])) {
            VK_World_Shutdown(ctx);
            return false;
        }
    }
    VK_World_ClearFrame();
    return true;
}

void VK_World_DestroySwapchainResources(vk_context_t *ctx)
{
    (void)ctx;

    if (!vk_world.initialized || !vk_world.ctx || !vk_world.ctx->device) {
        return;
    }

    if (vk_world.pipeline_alpha) {
        vkDestroyPipeline(vk_world.ctx->device, vk_world.pipeline_alpha, NULL);
        vk_world.pipeline_alpha = VK_NULL_HANDLE;
    }

    if (vk_world.pipeline_sky) {
        vkDestroyPipeline(vk_world.ctx->device, vk_world.pipeline_sky, NULL);
        vk_world.pipeline_sky = VK_NULL_HANDLE;
    }

    if (vk_world.pipeline_sky_array) {
        vkDestroyPipeline(vk_world.ctx->device, vk_world.pipeline_sky_array, NULL);
        vk_world.pipeline_sky_array = VK_NULL_HANDLE;
    }

    if (vk_world.pipeline_opaque) {
        vkDestroyPipeline(vk_world.ctx->device, vk_world.pipeline_opaque, NULL);
        vk_world.pipeline_opaque = VK_NULL_HANDLE;
    }

    if (vk_world.pipeline_fast_lit_opaque) {
        vkDestroyPipeline(vk_world.ctx->device, vk_world.pipeline_fast_lit_opaque,
                          NULL);
        vk_world.pipeline_fast_lit_opaque = VK_NULL_HANDLE;
    }

    if (vk_world.pipeline_fast_lit_no_fog_opaque) {
        vkDestroyPipeline(vk_world.ctx->device, vk_world.pipeline_fast_lit_no_fog_opaque,
                          NULL);
        vk_world.pipeline_fast_lit_no_fog_opaque = VK_NULL_HANDLE;
    }

    if (vk_world.pipeline_fast_lit_glowmap_opaque) {
        vkDestroyPipeline(vk_world.ctx->device, vk_world.pipeline_fast_lit_glowmap_opaque,
                          NULL);
        vk_world.pipeline_fast_lit_glowmap_opaque = VK_NULL_HANDLE;
    }

    if (vk_world.pipeline_fast_lit_glowmap_no_fog_opaque) {
        vkDestroyPipeline(vk_world.ctx->device,
                          vk_world.pipeline_fast_lit_glowmap_no_fog_opaque, NULL);
        vk_world.pipeline_fast_lit_glowmap_no_fog_opaque = VK_NULL_HANDLE;
    }

    if (vk_world.pipeline_texture_replace_opaque) {
        vkDestroyPipeline(vk_world.ctx->device, vk_world.pipeline_texture_replace_opaque,
                          NULL);
        vk_world.pipeline_texture_replace_opaque = VK_NULL_HANDLE;
    }

    if (vk_world.pipeline_texture_replace_no_fog_opaque) {
        vkDestroyPipeline(vk_world.ctx->device,
                          vk_world.pipeline_texture_replace_no_fog_opaque, NULL);
        vk_world.pipeline_texture_replace_no_fog_opaque = VK_NULL_HANDLE;
    }

    if (vk_world.pipeline_bloom_extract) {
        vkDestroyPipeline(vk_world.ctx->device, vk_world.pipeline_bloom_extract, NULL);
        vk_world.pipeline_bloom_extract = VK_NULL_HANDLE;
    }

    vk_world.swapchain_ready = false;
}

bool VK_World_CreateSwapchainResources(vk_context_t *ctx)
{
    if (!vk_world.initialized || !ctx || !ctx->scene_render_pass) {
        return false;
    }

    VK_World_DestroySwapchainResources(ctx);

    if (!VK_World_CreatePipelineVariant(ctx, false, true, VK_CULL_MODE_NONE, false, false,
                                        false, false, false, false, false, false,
                                        &vk_world.pipeline_opaque)) {
        return false;
    }

    // The fast receiver accepts only ordinary one-sided opaque BSP faces.
    // With Vulkan's inverted viewport, CCW front faces plus BACK culling are
    // the native equivalent of OpenGL's default CW/BACK world state.
    if (!VK_World_CreatePipelineVariant(ctx, false, true, VK_CULL_MODE_BACK_BIT, false, false,
                                        true, false, false, false, false, false,
                                        &vk_world.pipeline_fast_lit_opaque)) {
        // The general opaque pipeline remains a complete native fallback if a
        // driver rejects the optional static-light specialization.
        Com_WPrintf("Vulkan: static world fast-light pipeline unavailable; "
                    "using the general opaque pipeline\n");
    }

    // Opaque receivers with no global or height fog select this companion
    // every frame.  Keep the fog-aware fast pipeline above as a native
    // fallback if a driver rejects the optional specialization.
    if (!VK_World_CreatePipelineVariant(ctx, false, true, VK_CULL_MODE_BACK_BIT, false, false,
                                        false, false, true, false, false, false,
                                        &vk_world.pipeline_fast_lit_no_fog_opaque)) {
        Com_WPrintf("Vulkan: static world no-fog fast-light pipeline unavailable; "
                    "using the fog-aware static pipeline\n");
    }

    // The general world path is double-sided because legacy BSPs may carry
    // winding inconsistent with their plane flag. Preserve that visual
    // contract for the glow-map variant; its fragment specialization is the
    // performance win, while culling remains only for the proven one-sided
    // non-glow receiver path above.
    if (!VK_World_CreatePipelineVariant(ctx, false, true, VK_CULL_MODE_NONE, false, false,
                                        false, true, false, false, false, false,
                                        &vk_world.pipeline_fast_lit_glowmap_opaque)) {
        Com_WPrintf("Vulkan: static world fast-light glow-map pipeline unavailable; "
                    "using the general opaque pipeline\n");
    }

    if (!VK_World_CreatePipelineVariant(ctx, false, true, VK_CULL_MODE_NONE, false, false,
                                        false, false, false, true, false, false,
                                        &vk_world.pipeline_fast_lit_glowmap_no_fog_opaque)) {
        Com_WPrintf("Vulkan: static world no-fog glow-map pipeline unavailable; "
                    "using the fog-aware static pipeline\n");
    }

    // OpenGL compiles ordinary opaque faces with GLS_TEXTURE_REPLACE even
    // when they have no lightmap.  Preserve the general world's double-sided
    // raster state here: legacy BSP face winding is not sufficiently reliable
    // to apply the one-sided lightmapped fast-receiver optimization.
    if (!VK_World_CreatePipelineVariant(ctx, false, true, VK_CULL_MODE_NONE, false, false,
                                        false, false, false, false, true, false,
                                        &vk_world.pipeline_texture_replace_opaque)) {
        Com_WPrintf("Vulkan: texture-replace world pipeline unavailable; "
                    "using the general opaque pipeline\n");
    }

    if (!VK_World_CreatePipelineVariant(ctx, false, true, VK_CULL_MODE_NONE, false, false,
                                        false, false, false, false, false, true,
                                        &vk_world.pipeline_texture_replace_no_fog_opaque)) {
        Com_WPrintf("Vulkan: no-fog texture-replace world pipeline unavailable; "
                    "using the fog-aware texture-replace pipeline\n");
    }

    if (!VK_World_CreatePipelineVariant(ctx, true, true, VK_CULL_MODE_NONE, false, false,
                                        false, false, false, false, false, false,
                                        &vk_world.pipeline_alpha)) {
        VK_World_DestroySwapchainResources(ctx);
        return false;
    }

    if (!VK_World_CreatePipelineVariant(ctx, false, false, VK_CULL_MODE_FRONT_BIT, false, false,
                                        false, false, false, false, false, false,
                                        &vk_world.pipeline_sky)) {
        VK_World_DestroySwapchainResources(ctx);
        return false;
    }

    if (!VK_World_CreatePipelineVariant(ctx, false, false, VK_CULL_MODE_FRONT_BIT, true, false,
                                        false, false, false, false, false, false,
                                        &vk_world.pipeline_sky_array)) {
        VK_World_DestroySwapchainResources(ctx);
        return false;
    }

    if (ctx->bloom_extract_render_pass &&
        !VK_World_CreatePipelineVariant(ctx, false, true, VK_CULL_MODE_NONE, false, true,
                                        false, false, false, false, false, false,
                                        &vk_world.pipeline_bloom_extract)) {
        Com_WPrintf("Vulkan: authored bloom emission extraction unavailable; using scene fallback.\n");
    }

    vk_world.swapchain_ready = true;
    return true;
}

void VK_World_Shutdown(vk_context_t *ctx)
{
    VK_World_UnregisterIntensityCvars();
    if (!vk_world.initialized) {
        return;
    }

    if (!ctx)
        ctx = vk_world.ctx;

    if (ctx && ctx->device)
        vkDeviceWaitIdle(ctx->device);

    VK_World_DestroySwapchainResources(ctx);
    VK_World_DestroyVertexBuffer();
    VK_World_ClearShowTrisPositions();
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_World_DestroyFrameVertexBuffer(&vk_world.frame_buffers[i]);
    }
    VK_World_DestroySkyVertexBuffer();
    VK_World_ClearBatches();
    VK_World_ClearLightmap();
    VK_World_UnsetSky();
    VK_World_FreeBSP();

    if (vk_world.sky_lightmap_white_image) {
        VK_UI_UnregisterImage(vk_world.sky_lightmap_white_image);
        vk_world.sky_lightmap_white_image = 0;
        vk_world.sky_lightmap_white_set = VK_NULL_HANDLE;
    }

    if (ctx && ctx->device && vk_world.pipeline_layout) {
        vkDestroyPipelineLayout(ctx->device, vk_world.pipeline_layout, NULL);
        vk_world.pipeline_layout = VK_NULL_HANDLE;
    }

    memset(&vk_world, 0, sizeof(vk_world));
}

void VK_World_BeginRegistration(const char *map)
{
    if (!vk_world.initialized || !map || !*map) {
        return;
    }

    char bsp_path[MAX_QPATH];
    Q_concat(bsp_path, sizeof(bsp_path), "maps/", map, ".bsp");

    bsp_t *loaded = NULL;
    int ret = BSP_Load(bsp_path, &loaded);
    if (!loaded) {
        Com_Error(ERR_DROP, "VK_World_BeginRegistration: couldn't load %s: %s",
                  bsp_path, BSP_ErrorString(ret));
        return;
    }

    if (vk_world.bsp == loaded) {
        loaded->refcount--;
        return;
    }

    vkDeviceWaitIdle(vk_world.ctx->device);

    VK_World_DestroyVertexBuffer();
    VK_World_ClearShowTrisPositions();
    VK_World_ClearBatches();
    VK_World_ClearLightmap();
    VK_World_FreeBSP();
    vk_world.bsp = loaded;
    if (!VK_World_BuildWorldFaceMask(vk_world.bsp)) {
        VK_World_FreeBSP();
        Com_Error(ERR_DROP, "VK_World_BeginRegistration: failed to build world face mask for %s", map);
        return;
    }
    VK_World_PrepareLightmapGeometry(vk_world.bsp);

    Q_strlcpy(vk_world.map_name, map, sizeof(vk_world.map_name));
    vk_world.sky_world_size = 4096.0f;
    if (vk_world.bsp->numnodes > 0) {
        vec3_t extents;
        VectorSubtract(vk_world.bsp->nodes[0].maxs, vk_world.bsp->nodes[0].mins, extents);
        float radius = VectorLength(extents) * 0.5f;
        vk_world.sky_world_size = max(1024.0f, radius * 2.0f);
    }
    vk_world.sky_geometry_dirty = true;

    vk_world_face_lightmap_t *face_lms = NULL;
    byte *atlas_rgba = NULL;
    int atlas_size = 0;
    if (!VK_World_BuildLightmapAtlas(vk_world.bsp, NULL, &face_lms, &atlas_size, &atlas_rgba)) {
        free(face_lms);
        free(atlas_rgba);
        Com_Error(ERR_DROP,
                  "VK_World_BeginRegistration: failed to build world lightmaps "
                  "for %s",
                  map);
        return;
    }

    qhandle_t lightmap_handle =
        VK_UI_RegisterRawImage("**world_lightmap**", atlas_size, atlas_size,
                               atlas_rgba, IT_SPRITE, IF_REPEAT);
    if (!lightmap_handle) {
      free(face_lms);
      free(atlas_rgba);
      Com_Error(ERR_DROP,
                "VK_World_BeginRegistration: failed to register world "
                "lightmaps for %s",
                map);
      return;
    }

    VkDescriptorSet lightmap_set = VK_UI_GetDescriptorSetForImage(lightmap_handle);
    if (!lightmap_set) {
        VK_UI_UnregisterImage(lightmap_handle);
        free(face_lms);
        free(atlas_rgba);
        Com_Error(ERR_DROP, "VK_World_BeginRegistration: failed to bind world lightmaps for %s", map);
        return;
    }

    vk_world_vertex_t *vertices = NULL;
    vk_world_batch_t *batches = NULL;
    uint32_t vertex_count = 0;
    uint32_t batch_count = 0;
    bool has_trans_warp = false;
    if (!VK_World_BuildMesh(&vertices, &vertex_count, &batches, &batch_count,
                            &has_trans_warp, face_lms, atlas_size)) {
        VK_UI_UnregisterImage(lightmap_handle);
        free(face_lms);
        free(atlas_rgba);
        free(vertices);
        free(batches);
        Com_Error(ERR_DROP, "VK_World_BeginRegistration: failed to build world mesh for %s", map);
        return;
    }

    if (!VK_World_SetShowTrisPositions(vertices, vertex_count)) {
        VK_UI_UnregisterImage(lightmap_handle);
        free(face_lms);
        free(atlas_rgba);
        free(vertices);
        free(batches);
        Com_Error(ERR_DROP,
                  "VK_World_BeginRegistration: failed to retain show-tris geometry for %s",
                  map);
        return;
    }

    if (!VK_World_UploadVertices(vertices, vertex_count)) {
        VK_UI_UnregisterImage(lightmap_handle);
        VK_World_ClearShowTrisPositions();
        free(face_lms);
        free(atlas_rgba);
        free(vertices);
        free(batches);
        Com_Error(ERR_DROP, "VK_World_BeginRegistration: failed to upload world mesh for %s", map);
        return;
    }

    free(vertices);

    VK_World_ClearBatches();
    vk_world.batches = batches;
    vk_world.batch_count = batch_count;
    vk_world.has_trans_warp = has_trans_warp;
    if (batch_count) {
        vk_world.alpha_batch_order = malloc((size_t)batch_count *
                                            sizeof(*vk_world.alpha_batch_order));
        if (vk_world.alpha_batch_order) {
            for (uint32_t i = 0; i < batch_count; i++) {
                if (batches[i].flags & VK_WORLD_BATCH_ALPHA) {
                    vk_world.alpha_batch_order[vk_world.alpha_batch_count++] = i;
                }
            }
        } else {
            Com_WPrintf("Vulkan world: transparent batch sort allocation failed; using BSP order\n");
        }
    }
    vk_world.lightmap_handle = lightmap_handle;
    vk_world.lightmap_descriptor_set = lightmap_set;
    vk_world.face_lms = face_lms;
    vk_world.lightmap_rgba = atlas_rgba;
    vk_world.lightmap_atlas_size = atlas_size;
    vk_world.lightmap_built_shift = VK_World_LightmapShift();
    vk_world.lightmap_built_cap = VK_World_LightmapCap();
    vk_world.style_cache_valid = false;
    vk_world.first_draw_logged = false;
    Com_DPrintf("VK_World_BeginRegistration: map=%s vertices=%u batches=%u\n",
                map, vertex_count, batch_count);
}

void VK_World_EndRegistration(void)
{
}

void VK_World_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis)
{
    if (!vk_world.initialized) {
        return;
    }

    bool had_sky_images = false;
    for (int i = 0; i < VK_WORLD_SKY_FACE_COUNT; i++) {
        if (vk_world.sky_images[i]) {
            had_sky_images = true;
            break;
        }
    }

    if (!name || !*name || (vk_drawsky && !vk_drawsky->integer)) {
        if (had_sky_images && vk_world.ctx && vk_world.ctx->device) {
            vkDeviceWaitIdle(vk_world.ctx->device);
        }
        VK_World_UnsetSky();
        return;
    }

    vec3_t normalized_axis = { 0.0f, 0.0f, 1.0f };
    if (!axis || VectorNormalize2(axis, normalized_axis) < 0.001f) {
        rotate = 0.0f;
    }

    if (!rotate) {
        autorotate = false;
    }

    if (had_sky_images && vk_world.ctx && vk_world.ctx->device) {
        vkDeviceWaitIdle(vk_world.ctx->device);
    }
    VK_World_UnsetSky();
    VectorCopy(normalized_axis, vk_world.sky_axis);

    qhandle_t new_images[VK_WORLD_SKY_FACE_COUNT] = { 0 };
    VkDescriptorSet new_sets[VK_WORLD_SKY_FACE_COUNT] = { VK_NULL_HANDLE };
    vec2_t new_uv_insets[VK_WORLD_SKY_FACE_COUNT] = { 0 };

    for (int i = 0; i < VK_WORLD_SKY_FACE_COUNT; i++) {
        char pathname[MAX_QPATH];
        if (Q_concat(pathname, sizeof(pathname), "env/", name, com_env_suf[i], ".tga") >= sizeof(pathname)) {
            for (int k = 0; k < i; k++) {
                if (new_images[k]) {
                    VK_UI_UnregisterImage(new_images[k]);
                }
            }
            Com_WPrintf("Vulkan sky path too long for %s\n", name);
            return;
        }

        new_images[i] = VK_UI_RegisterImage(pathname, IT_SKY, IF_NONE);
        if (!new_images[i]) {
            for (int k = 0; k <= i; k++) {
                if (new_images[k]) {
                    VK_UI_UnregisterImage(new_images[k]);
                }
            }
            Com_WPrintf("Vulkan failed to load sky face %s\n", pathname);
            return;
        }

        new_sets[i] = VK_UI_GetDescriptorSetForImage(new_images[i]);
        if (!new_sets[i]) {
            for (int k = 0; k <= i; k++) {
                if (new_images[k]) {
                    VK_UI_UnregisterImage(new_images[k]);
                }
            }
            Com_WPrintf("Vulkan failed to bind sky face %s\n", pathname);
            return;
        }

        int width = 0;
        int height = 0;
        VK_UI_GetPicSize(&width, &height, new_images[i]);
        if (width > 0 && height > 0) {
            new_uv_insets[i][0] = 0.5f / (float)width;
            new_uv_insets[i][1] = 0.5f / (float)height;
        } else {
            new_uv_insets[i][0] = 1.0f / 512.0f;
            new_uv_insets[i][1] = 1.0f / 512.0f;
        }
    }

    for (int i = 0; i < VK_WORLD_SKY_FACE_COUNT; i++) {
        vk_world.sky_images[i] = new_images[i];
        vk_world.sky_descriptor_sets[i] = new_sets[i];
        Vector2Copy(new_uv_insets[i], vk_world.sky_uv_inset[i]);
    }

    if (!VK_World_CreateSkyTextureArray()) {
        // Some legacy sky sets use incompatible source images. Preserve the
        // six-face path in that case rather than degrading or rerouting the
        // native Vulkan renderer.
        Com_DPrintf("VK_World_SetSky: sky texture array unavailable; using six-face draws\n");
    }

    vk_world.sky_rotate = rotate;
    vk_world.sky_autorotate = autorotate;
    vk_world.sky_enabled = true;
    vk_world.sky_geometry_dirty = true;

    Com_DPrintf("VK_World_SetSky: %s rotate=%.2f autorotate=%d axis=(%.2f %.2f %.2f)\n",
                name, rotate, autorotate ? 1 : 0,
                vk_world.sky_axis[0], vk_world.sky_axis[1], vk_world.sky_axis[2]);
}

static void VK_World_ComputeFramePush(const refdef_t *fd, renderer_view_push_t *out_push)
{
    float znear = 4.0f;
    float zfar = 8192.0f;

    if (vk_world.bsp && vk_world.bsp->numnodes > 0) {
        vec3_t extents;
        VectorSubtract(vk_world.bsp->nodes[0].maxs, vk_world.bsp->nodes[0].mins, extents);
        float radius = VectorLength(extents) * 0.5f;
        zfar = max(2048.0f, radius * 8.0f);
    }

    R_BuildViewPush(fd, znear, zfar, out_push);
}

void VK_World_RenderFrame(const refdef_t *fd) {
  VK_World_ClearFrame();

  if (!vk_world.initialized || !fd) {
    return;
  }

  vk_world.current_fd = fd;

  if (!vk_world.bsp || !vk_world.vertex_count) {
    return;
  }

  if (fd->rdflags & RDF_NOWORLDMODEL) {
    return;
  }

  if (!VK_World_UpdateLightmapStyles(fd)) {
    return;
  }

  if (!VK_World_UpdateAnimationFrame(fd)) {
    return;
  }

  if (!VK_World_UpdateSkyGeometry()) {
    return;
  }

  VK_World_ComputeFramePush(fd, &vk_world.frame_push);
  vk_world.frame_active = true;
}

static int VK_World_CompareAlphaBatches(const void *left, const void *right)
{
    const uint32_t left_index = *(const uint32_t *)left;
    const uint32_t right_index = *(const uint32_t *)right;
    const vk_world_batch_t *left_batch = &vk_world.batches[left_index];
    const vk_world_batch_t *right_batch = &vk_world.batches[right_index];
    const vec3_t vieworg = {
        vk_world.current_fd->vieworg[0], vk_world.current_fd->vieworg[1],
        vk_world.current_fd->vieworg[2],
    };
    const float left_distance = DistanceSquared(left_batch->center, vieworg);
    const float right_distance = DistanceSquared(right_batch->center, vieworg);

    if (left_distance > right_distance) {
        return -1;
    }
    if (left_distance < right_distance) {
        return 1;
    }
    return left_index < right_index ? -1 : left_index > right_index;
}

static bool VK_World_SortTransparentBatches(void)
{
    if (!vk_world.alpha_batch_order || !vk_world.alpha_batch_count ||
        !vk_world.current_fd) {
        return false;
    }

    qsort(vk_world.alpha_batch_order, vk_world.alpha_batch_count,
          sizeof(*vk_world.alpha_batch_order), VK_World_CompareAlphaBatches);
    return true;
}

static bool VK_World_CanRecord(const VkExtent2D *extent)
{
    vk_world_frame_buffer_t *frame_buffer = VK_World_CurrentFrameBuffer();
    return vk_world.initialized && vk_world.swapchain_ready &&
        vk_world.pipeline_opaque && vk_world.pipeline_alpha && vk_world.pipeline_sky &&
        vk_world.pipeline_sky_array &&
        vk_world.frame_active && vk_world.vertex_count && vk_world.batch_count &&
        vk_world.vertex_buffer && frame_buffer && frame_buffer->vertex_buffer &&
        vk_world.lightmap_descriptor_set && extent;
}

static bool VK_World_BindFrame(VkCommandBuffer cmd, const VkExtent2D *extent,
                               VkDescriptorSet scene_descriptor_set,
                               VkBuffer world_buffers[2])
{
    VkDescriptorSet shadow_set = VK_Shadow_GetDescriptorSet();
    VkDescriptorSet fallback_scene_set = vk_world.sky_lightmap_white_set
        ? vk_world.sky_lightmap_white_set : vk_world.lightmap_descriptor_set;
    if (!shadow_set || !fallback_scene_set) {
        return false;
    }
    if (!scene_descriptor_set) {
        scene_descriptor_set = fallback_scene_set;
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
    VkDeviceSize world_offsets[2] = { 0, 0 };
    world_buffers[0] = vk_world.vertex_buffer;
    world_buffers[1] = VK_World_CurrentFrameBuffer()->vertex_buffer;
    vkCmdBindVertexBuffers(cmd, 0, 2, world_buffers, world_offsets);
    vkCmdPushConstants(cmd, vk_world.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(vk_world.frame_push), &vk_world.frame_push);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_world.pipeline_layout, 1, 1,
                            &vk_world.lightmap_descriptor_set, 0, NULL);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_world.pipeline_layout, 2, 1, &shadow_set, 0, NULL);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_world.pipeline_layout, 3, 1,
                            &scene_descriptor_set, 0, NULL);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    return true;
}

void VK_World_RecordOpaque(VkCommandBuffer cmd, const VkExtent2D *extent)
{
    if (!VK_World_CanRecord(extent)) {
        return;
    }

    VK_Debug_QueueShowTrisTriangles(VK_DEBUG_SHOWTRIS_WORLD,
                                    vk_world.showtris_positions,
                                    vk_world.showtris_position_count);

    VkBuffer world_buffers[2];
    if (!VK_World_BindFrame(cmd, extent, VK_NULL_HANDLE, world_buffers)) {
        return;
    }

    const bool draw_sky = !vk_drawsky || (vk_drawsky->integer != 0);
    const bool render_skybox = draw_sky && vk_world.sky_enabled &&
                               vk_world.sky_vertex_count > 0 &&
                               vk_world.sky_vertex_buffer;
    if (render_skybox) {
        // The sky is a background pass. Avoid writing far cube depth so every
        // world surface deterministically overwrites it even when cube corners
        // cross clip-space precision boundaries.
        const bool use_sky_array = vk_world.sky_array_descriptor_set != VK_NULL_HANDLE;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          use_sky_array ? vk_world.pipeline_sky_array : vk_world.pipeline_sky);
        VkDescriptorSet sky_lightmap_set = vk_world.sky_lightmap_white_set
            ? vk_world.sky_lightmap_white_set : vk_world.lightmap_descriptor_set;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vk_world.pipeline_layout, 1, 1,
                                &sky_lightmap_set, 0, NULL);

        VkBuffer sky_buffers[2] = {
            vk_world.sky_vertex_buffer,
            VK_World_CurrentFrameBuffer()->vertex_buffer,
        };
        VkDeviceSize sky_offsets[2] = { 0, 0 };
        vkCmdBindVertexBuffers(cmd, 0, 2, sky_buffers, sky_offsets);
        if (use_sky_array) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    vk_world.pipeline_layout, 0, 1,
                                    &vk_world.sky_array_descriptor_set, 0, NULL);
            vkCmdDraw(cmd, vk_world.sky_vertex_count, 1, 0, 0);
            VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_WORLD, vk_world.sky_vertex_count, 0);
        } else {
            for (int face = 0; face < VK_WORLD_SKY_FACE_COUNT; face++) {
                VkDescriptorSet sky_set = vk_world.sky_descriptor_sets[face];
                uint32_t face_vertex_count = vk_world.sky_face_vertex_count[face];
                if (!sky_set || !face_vertex_count) {
                    continue;
                }
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vk_world.pipeline_layout, 0, 1,
                                        &sky_set, 0, NULL);
                vkCmdDraw(cmd, face_vertex_count, 1,
                          vk_world.sky_face_first_vertex[face], 0);
                VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_WORLD, face_vertex_count, 0);
            }
        }

        VkDeviceSize world_offsets[2] = { 0, 0 };
        vkCmdBindVertexBuffers(cmd, 0, 2, world_buffers, world_offsets);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vk_world.pipeline_layout, 1, 1,
                                &vk_world.lightmap_descriptor_set, 0, NULL);
    }

    const bool fast_lit_enabled = !vk_world_fast_lit || vk_world_fast_lit->integer;
    const bool fast_lit_no_fog_enabled = !vk_world_fast_lit_no_fog ||
        vk_world_fast_lit_no_fog->integer;
    const bool world_fullbright = VK_World_Fullbright();
    const bool receiver_lighting_active = VK_Shadow_HasActiveReceiverLighting();
    const bool surface_fog_active = VK_Shadow_HasActiveSurfaceFog();
    const bool fast_lit_receivers_inactive =
        !world_fullbright && !receiver_lighting_active;
    const uint8_t fast_lit_vertex_flags = VK_WORLD_VERTEX_LIGHTMAPPED |
                                         VK_WORLD_VERTEX_INTENSITY;
    const uint8_t fast_lit_glowmap_vertex_flags =
        VK_WORLD_VERTEX_LIGHTMAPPED | VK_WORLD_VERTEX_INTENSITY |
        VK_WORLD_VERTEX_GLOWMAP;
    const uint8_t texture_replace_vertex_flags = VK_WORLD_VERTEX_INTENSITY;
    uint32_t fast_lit_candidates = 0;
    uint32_t fast_lit_disabled = 0;
    uint32_t fast_lit_fullbright = 0;
    uint32_t fast_lit_receiver_lighting = 0;
    uint32_t fast_lit_pipeline_unavailable = 0;
    uint32_t fast_lit_material_ineligible = 0;
    VkDescriptorSet last_set = VK_NULL_HANDLE;
    VkPipeline bound_pipeline = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < vk_world.batch_count; i++) {
        const vk_world_batch_t *batch = &vk_world.batches[i];
        if (!batch->vertex_count || !batch->descriptor_set ||
            (batch->flags & VK_WORLD_BATCH_ALPHA) ||
            (batch->flags & VK_WORLD_BATCH_SKY)) {
            continue;
        }
        const bool lightmapped =
            (batch->vertex_flags & VK_WORLD_VERTEX_LIGHTMAPPED) != 0;
        const bool glowmapped =
            (batch->vertex_flags & VK_WORLD_VERTEX_GLOWMAP) != 0;
        const uint8_t compatible_flags = glowmapped
            ? fast_lit_glowmap_vertex_flags : fast_lit_vertex_flags;
        const bool material_eligible = lightmapped &&
            (batch->vertex_flags & ~compatible_flags) == 0;
        const bool pipeline_available = glowmapped
            ? vk_world.pipeline_fast_lit_glowmap_opaque != VK_NULL_HANDLE
            : vk_world.pipeline_fast_lit_opaque != VK_NULL_HANDLE;
        if (lightmapped) {
            fast_lit_candidates++;
            if (!fast_lit_enabled) {
                fast_lit_disabled++;
            } else if (world_fullbright) {
                fast_lit_fullbright++;
            } else if (receiver_lighting_active) {
                fast_lit_receiver_lighting++;
            } else if (!pipeline_available) {
                fast_lit_pipeline_unavailable++;
            } else if (!material_eligible) {
                fast_lit_material_ineligible++;
            }
        }
        const bool fast_lit = fast_lit_enabled && fast_lit_receivers_inactive &&
            vk_world.pipeline_fast_lit_opaque &&
            lightmapped &&
            (batch->vertex_flags & ~fast_lit_vertex_flags) == 0;
        const bool fast_lit_glowmap = fast_lit_enabled && fast_lit_receivers_inactive &&
            vk_world.pipeline_fast_lit_glowmap_opaque &&
            (batch->vertex_flags & (VK_WORLD_VERTEX_LIGHTMAPPED |
                                    VK_WORLD_VERTEX_GLOWMAP)) ==
                (VK_WORLD_VERTEX_LIGHTMAPPED | VK_WORLD_VERTEX_GLOWMAP) &&
            (batch->vertex_flags & ~fast_lit_glowmap_vertex_flags) == 0;
        // Surface fog is sampled from the current shadow UBO every frame.
        // A sky-only fog flag does not affect opaque receivers, so it remains
        // safe to use this no-fog shader for ordinary world batches.
        const bool fast_lit_no_fog = fast_lit && fast_lit_no_fog_enabled &&
            !surface_fog_active &&
            vk_world.pipeline_fast_lit_no_fog_opaque;
        const bool fast_lit_glowmap_no_fog = fast_lit_glowmap &&
            fast_lit_no_fog_enabled && !surface_fog_active &&
            vk_world.pipeline_fast_lit_glowmap_no_fog_opaque;
        const bool texture_replace = fast_lit_enabled &&
            vk_world.pipeline_texture_replace_opaque &&
            (batch->vertex_flags & ~texture_replace_vertex_flags) == 0;
        const bool texture_replace_no_fog = texture_replace &&
            fast_lit_no_fog_enabled && !surface_fog_active &&
            vk_world.pipeline_texture_replace_no_fog_opaque;
        const VkPipeline target_pipeline = fast_lit_glowmap_no_fog
            ? vk_world.pipeline_fast_lit_glowmap_no_fog_opaque
            : fast_lit_glowmap
            ? vk_world.pipeline_fast_lit_glowmap_opaque
            : texture_replace_no_fog
            ? vk_world.pipeline_texture_replace_no_fog_opaque
            : texture_replace
            ? vk_world.pipeline_texture_replace_opaque
            : fast_lit_no_fog
            ? vk_world.pipeline_fast_lit_no_fog_opaque
            : fast_lit ? vk_world.pipeline_fast_lit_opaque : vk_world.pipeline_opaque;
        if (target_pipeline != bound_pipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              target_pipeline);
            bound_pipeline = target_pipeline;
        }
        if (batch->descriptor_set != last_set) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    vk_world.pipeline_layout, 0, 1,
                                    &batch->descriptor_set, 0, NULL);
            last_set = batch->descriptor_set;
        }
        vkCmdDraw(cmd, batch->vertex_count, 1, batch->first_vertex, 0);
        if (fast_lit || fast_lit_glowmap) {
            VK_Debug_RecordFastLitDraw(VK_DEBUG_DOMAIN_WORLD);
        }
        if (fast_lit_no_fog || fast_lit_glowmap_no_fog) {
            VK_Debug_RecordWorldFastLitNoFogDraw();
        }
        if (texture_replace) {
            VK_Debug_RecordWorldTextureReplaceDraw(texture_replace_no_fog);
        }
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_WORLD, batch->vertex_count, 0);
    }
    VK_Debug_SetWorldFastLitCoverage(fast_lit_candidates, fast_lit_disabled,
                                     fast_lit_fullbright,
                                     fast_lit_receiver_lighting,
                                     fast_lit_pipeline_unavailable,
                                     fast_lit_material_ineligible);

    if (!vk_world.first_draw_logged) {
        vk_world.first_draw_logged = true;
        Com_DPrintf("VK_World_RecordOpaque: rendered map=%s vertices=%u batches=%u\n",
                    vk_world.map_name[0] ? vk_world.map_name : "<unknown>",
                    vk_world.vertex_count, vk_world.batch_count);
    }
}

void VK_World_RecordBloomEmission(VkCommandBuffer cmd, const VkExtent2D *extent)
{
    if (!VK_World_CanRecord(extent) || !vk_world.pipeline_bloom_extract) {
        return;
    }

    VkBuffer world_buffers[2];
    if (!VK_World_BindFrame(cmd, extent, VK_NULL_HANDLE, world_buffers)) {
        return;
    }

    VkDescriptorSet last_set = VK_NULL_HANDLE;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_world.pipeline_bloom_extract);
    for (uint32_t i = 0; i < vk_world.batch_count; i++) {
        const vk_world_batch_t *batch = &vk_world.batches[i];
        if (!batch->vertex_count || !batch->descriptor_set ||
            (batch->flags & VK_WORLD_BATCH_ALPHA) ||
            (batch->flags & VK_WORLD_BATCH_SKY) ||
            !(batch->flags & VK_WORLD_BATCH_BLOOM_EMISSION)) {
            continue;
        }
        if (batch->descriptor_set != last_set) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    vk_world.pipeline_layout, 0, 1,
                                    &batch->descriptor_set, 0, NULL);
            last_set = batch->descriptor_set;
        }
        vkCmdDraw(cmd, batch->vertex_count, 1, batch->first_vertex, 0);
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_WORLD, batch->vertex_count, 0);
    }
}

bool VK_World_HasBloomEmission(void)
{
    if (!vk_world.initialized || !vk_world.swapchain_ready ||
        !vk_world.frame_active || !vk_world.pipeline_bloom_extract) {
        return false;
    }

    for (uint32_t i = 0; i < vk_world.batch_count; ++i) {
        const vk_world_batch_t *batch = &vk_world.batches[i];
        if (batch->vertex_count && batch->descriptor_set &&
            !(batch->flags & VK_WORLD_BATCH_ALPHA) &&
            !(batch->flags & VK_WORLD_BATCH_SKY) &&
            (batch->flags & VK_WORLD_BATCH_BLOOM_EMISSION)) {
            return true;
        }
    }
    return false;
}

void VK_World_RecordAlpha(VkCommandBuffer cmd, const VkExtent2D *extent,
                          VkDescriptorSet scene_descriptor_set)
{
    if (!VK_World_CanRecord(extent) || !vk_world.alpha_batch_count) {
        return;
    }

    VkBuffer world_buffers[2];
    if (!VK_World_BindFrame(cmd, extent, scene_descriptor_set, world_buffers)) {
        return;
    }

    const bool sorted_alpha = VK_World_SortTransparentBatches();
    const uint32_t draw_count = sorted_alpha ? vk_world.alpha_batch_count :
                                               vk_world.batch_count;
    VkDescriptorSet last_set = VK_NULL_HANDLE;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_world.pipeline_alpha);
    for (uint32_t draw_index = 0; draw_index < draw_count; draw_index++) {
        const uint32_t i = sorted_alpha ? vk_world.alpha_batch_order[draw_index] : draw_index;
        const vk_world_batch_t *batch = &vk_world.batches[i];
        if (!batch->vertex_count || !batch->descriptor_set ||
            !(batch->flags & VK_WORLD_BATCH_ALPHA)) {
            continue;
        }
        if (batch->descriptor_set != last_set) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    vk_world.pipeline_layout, 0, 1,
                                    &batch->descriptor_set, 0, NULL);
            last_set = batch->descriptor_set;
        }
        vkCmdDraw(cmd, batch->vertex_count, 1, batch->first_vertex, 0);
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_WORLD, batch->vertex_count, 0);
    }
}

bool VK_World_UsesRefraction(void)
{
    return vk_world.frame_active && vk_world.has_trans_warp &&
        (!vk_shaders || vk_shaders->integer != 0) && vk_warp_refraction &&
        vk_warp_refraction->value > 0.0f && vk_world.ctx &&
        vk_world.ctx->frame_count &&
        vk_world.ctx->frames[vk_world.ctx->current_frame].liquid_scene_image &&
        vk_world.ctx->frames[vk_world.ctx->current_frame].liquid_scene_descriptor_set;
}

void VK_World_Record(VkCommandBuffer cmd, const VkExtent2D *extent)
{
    VK_World_RecordOpaque(cmd, extent);
    VK_World_RecordAlpha(cmd, extent, VK_NULL_HANDLE);
}

void VK_World_LightPoint(const vec3_t origin, vec3_t light)
{
    VK_World_LightPointEx(origin, light, true);
}

// include_dynamic_lights=true produces a complete CPU result with dynamic
// lights and the entity modulate applied (the R_LightPoint contract).
// include_dynamic_lights=false produces unmodulated static light for entity
// vertex colors; the entity shader applies dynamic lights and the entity
// modulate so values above 1.0 survive the UNORM vertex encoding.
void VK_World_LightPointEx(const vec3_t origin, vec3_t light, bool include_dynamic_lights)
{
    if (!light)
        return;

    VectorSet(light, 1.0f, 1.0f, 1.0f);

    if (VK_World_Fullbright()) {
        return;
    }

    if (!origin || !vk_world.bsp) {
        return;
    }

    const bsp_t *bsp = vk_world.bsp;
    bool has_light = false;

    if (bsp->lightgrid.numleafs) {
        vec3_t point = {
            (origin[0] - bsp->lightgrid.mins[0]) * bsp->lightgrid.scale[0],
            (origin[1] - bsp->lightgrid.mins[1]) * bsp->lightgrid.scale[1],
            (origin[2] - bsp->lightgrid.mins[2]) * bsp->lightgrid.scale[2],
        };

        uint32_t point_i[3];
        uint32_t point_next[3];
        bool valid_grid_point = true;
        for (int axis = 0; axis < 3; axis++) {
            if (!isfinite(point[axis]) || !bsp->lightgrid.size[axis] ||
                point[axis] < 0.0f ||
                point[axis] > (float)(bsp->lightgrid.size[axis] - 1)) {
                valid_grid_point = false;
                break;
            }
            point_i[axis] = (uint32_t)floorf(point[axis]);
            point_next[axis] =
                min(point_i[axis] + 1, bsp->lightgrid.size[axis] - 1);
        }

        if (valid_grid_point) {
            vec3_t samples[8];
            vec3_t avg;
            int mask = 0;
            int numsamples = 0;
            VectorClear(avg);

            for (int i = 0; i < 8; i++) {
                uint32_t p[3] = {
                    (i & BIT(0)) ? point_next[0] : point_i[0],
                    (i & BIT(1)) ? point_next[1] : point_i[1],
                    (i & BIT(2)) ? point_next[2] : point_i[2],
                };

                const lightgrid_sample_t *s =
                    BSP_LookupLightgrid(&bsp->lightgrid, p);
                if (!s)
                    continue;

                VectorClear(samples[i]);
                int style_count = 0;
                for (; style_count < (int)bsp->lightgrid.numstyles &&
                       s->style != 255;
                     style_count++, s++) {
                    vec3_t shifted;
                    VK_World_ShiftLightmapBytes(s->rgb, shifted);
                    VectorMA(samples[i], VK_World_LightStyleWhite(s->style),
                             shifted, samples[i]);
                }

                if (style_count > 0) {
                    mask |= BIT(i);
                    VectorAdd(avg, samples[i], avg);
                    numsamples++;
                }
            }

            if (mask) {
                has_light = true;
                if (mask != 255) {
                    VectorScale(avg, 1.0f / max(numsamples, 1), avg);
                    for (int i = 0; i < 8; i++) {
                        if (!(mask & BIT(i))) {
                            VectorCopy(avg, samples[i]);
                        }
                    }
                }

                float fx = point[0] - point_i[0];
                float fy = point[1] - point_i[1];
                float fz = point[2] - point_i[2];
                float bx = 1.0f - fx;
                float by = 1.0f - fy;
                float bz = 1.0f - fz;
                vec3_t lerp_x[4];
                vec3_t lerp_y[2];

                LerpVector2(samples[0], samples[1], bx, fx, lerp_x[0]);
                LerpVector2(samples[2], samples[3], bx, fx, lerp_x[1]);
                LerpVector2(samples[4], samples[5], bx, fx, lerp_x[2]);
                LerpVector2(samples[6], samples[7], bx, fx, lerp_x[3]);

                LerpVector2(lerp_x[0], lerp_x[1], by, fy, lerp_y[0]);
                LerpVector2(lerp_x[2], lerp_x[3], by, fy, lerp_y[1]);

                LerpVector2(lerp_y[0], lerp_y[1], bz, fz, light);
                VK_World_AdjustLightColor(light);
                if (include_dynamic_lights) {
                    VK_World_AdjustEntityLightColor(light);
                }
            }
        }
    }

    if (!has_light && bsp->lightmap) {
        vec3_t end = { origin[0], origin[1], origin[2] - 8192.0f };
        int nolm_mask = (bsp->has_bspx ? SURF_NOLM_MASK_REMASTER : SURF_NOLM_MASK_DEFAULT) |
                        SURF_TRANS_MASK;
        lightpoint_t point;
        BSP_LightPoint(&point, origin, end, bsp->nodes, nolm_mask);
        if (point.surf && VK_World_SampleFaceLightmap(point.surf, point.s, point.t, light)) {
            if (include_dynamic_lights) {
                VK_World_AdjustEntityLightColor(light);
            }
            has_light = true;
        }
    }

    if (!has_light) {
        VectorSet(light, 0.75f, 0.75f, 0.75f);
    }

    if (include_dynamic_lights) {
        VK_World_AddDynamicLightsAtPoint(origin, light);
    }
    VK_World_ClampLight(light);
}

VkDescriptorSet VK_World_GetLightmapDescriptorSet(void)
{
    return vk_world.lightmap_descriptor_set;
}

bool VK_World_GetFaceLightmapUV(const mface_t *face, const vec3_t point,
                                vec2_t out_uv)
{
    if (!out_uv) {
        return false;
    }

    if (!vk_world.bsp || !vk_world.face_lms ||
        vk_world.lightmap_atlas_size < 1) {
        out_uv[0] = 0.0f;
        out_uv[1] = 0.0f;
        return false;
    }

    out_uv[0] = 0.5f / (float)vk_world.lightmap_atlas_size;
    out_uv[1] = 0.5f / (float)vk_world.lightmap_atlas_size;

    if (!face || !point ||
        face < vk_world.bsp->faces ||
        face >= vk_world.bsp->faces + vk_world.bsp->numfaces) {
        return false;
    }

    int face_index = (int)(face - vk_world.bsp->faces);
    const vk_world_face_lightmap_t *face_lm = &vk_world.face_lms[face_index];
    if (!face_lm->has_lightmap || face_lm->width < 1 ||
        face_lm->height < 1) {
        return false;
    }

    float s = DotProduct(face->lm_axis[0], point) + face->lm_offset[0];
    float t = DotProduct(face->lm_axis[1], point) + face->lm_offset[1];

    s = max(0.0f, min((float)(face_lm->width - 1), s));
    t = max(0.0f, min((float)(face_lm->height - 1), t));

    out_uv[0] = ((float)face_lm->x + s + 0.5f) /
        (float)vk_world.lightmap_atlas_size;
    out_uv[1] = ((float)face_lm->y + t + 0.5f) /
        (float)vk_world.lightmap_atlas_size;
    return true;
}

const bsp_t *VK_World_GetBsp(void)
{
    return vk_world.bsp;
}
