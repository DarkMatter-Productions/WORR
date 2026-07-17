/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "vk_ui.h"
#include "vk_debug.h"

#include "renderer/ui_scale.h"
#include "renderer/dds.h"
#include "format/pcx.h"
#include "format/wal.h"
#include "refresh/stb/stb_image.h"
#include "vk_ui2d_spv.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define VK_UI_INITIAL_IMAGE_CAPACITY 128
#define VK_UI_INITIAL_DRAW_CAPACITY 2048
#define VK_UI_INITIAL_VERTEX_CAPACITY (VK_UI_INITIAL_DRAW_CAPACITY * 4)
#define VK_UI_INITIAL_INDEX_CAPACITY (VK_UI_INITIAL_DRAW_CAPACITY * 6)
#define VK_UI_INITIAL_SHOWTRIS_VERTEX_CAPACITY (VK_UI_INITIAL_INDEX_CAPACITY * 2)
#define VK_UI_BUFFER_GROWTH_FACTOR 2
#define VK_UI_MAX_TEXTURE_SIZE 4096

typedef struct {
    float pos[2];
    float uv[2];
    uint32_t color;
} vk_ui_vertex_t;

typedef struct {
    bool in_use;
    bool transparent;
    // Internal glow images share the normal image registry so descriptor
    // lifetime stays with Vulkan UI. They are owned by their base image and
    // are never selected for ordinary UI drawing.
    bool internal_glowmap;
    imagetype_t type;
    imageflags_t flags;
    int width;
    int height;
    uint32_t mip_levels;
    char name[MAX_QPATH];

    VkImage image;
    VkDeviceMemory image_memory;
    VkImageView view;
    VkDescriptorSet descriptor_set;
    qhandle_t glow_image;
} vk_ui_image_t;

typedef struct {
    uint32_t first_index;
    uint32_t index_count;
    VkDescriptorSet descriptor_set;
    VkRect2D scissor;
    uint32_t showtris_first_vertex;
    uint32_t showtris_vertex_count;
} vk_ui_draw_t;

typedef struct {
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    size_t vertex_buffer_bytes;
    VkBuffer vertex_staging_buffer;
    VkDeviceMemory vertex_staging_memory;
    void *vertex_staging_mapped;
    size_t vertex_upload_bytes;
    VkBuffer index_buffer;
    VkDeviceMemory index_memory;
    size_t index_buffer_bytes;
    VkBuffer index_staging_buffer;
    VkDeviceMemory index_staging_memory;
    void *index_staging_mapped;
    size_t index_upload_bytes;
    VkBuffer showtris_vertex_buffer;
    VkDeviceMemory showtris_vertex_memory;
    size_t showtris_vertex_buffer_bytes;
    VkBuffer showtris_vertex_staging_buffer;
    VkDeviceMemory showtris_vertex_staging_memory;
    void *showtris_vertex_staging_mapped;
    size_t showtris_vertex_upload_bytes;
} vk_ui_frame_buffers_t;

typedef struct {
    vk_context_t *ctx;

    bool initialized;
    bool swapchain_ready;

    float base_scale;
    float virtual_width;
    float virtual_height;
    float scale;

    bool clip_enabled;
    clipRect_t clip_pixels;

    int registration_sequence;

    vk_ui_image_t *images;
    uint32_t image_capacity;

    qhandle_t white_image;
    qhandle_t missing_image;
    qhandle_t raw_image;

    VkSampler sampler_repeat;
    VkSampler sampler_clamp;
    VkSampler sampler_nearest_repeat;
    VkSampler sampler_nearest_clamp;
    VkSampler sampler_material_repeat;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkPipeline showtris_pipeline;

    vk_ui_frame_buffers_t frame_buffers[VK_MAX_FRAMES_IN_FLIGHT];

    vk_ui_vertex_t *vertices;
    uint32_t vertex_count;
    uint32_t vertex_capacity;

    vk_ui_vertex_t *showtris_vertices;
    uint32_t showtris_vertex_count;
    uint32_t showtris_vertex_capacity;

    uint32_t *indices;
    uint32_t index_count;
    uint32_t index_capacity;

    vk_ui_draw_t *draws;
    uint32_t draw_count;
    uint32_t draw_capacity;
} vk_ui_state_t;

static vk_ui_state_t vk_ui;
static cvar_t *vk_r_glowmaps;
static cvar_t *vk_anisotropy;
static cvar_t *r_anisotropy;
static cvar_t *vk_gl_texturemode_legacy;
static cvar_t *r_texture_filter;
static cvar_t *r_picmip;
static cvar_t *r_nomip;
static cvar_t *r_picmip_filter;
static cvar_t *vk_gl_downsample_skins;
static cvar_t *vk_bilerp_chars;
static cvar_t *vk_bilerp_pics;
static cvar_t *vk_bilerp_skies;
extern uint32_t d_8to24table[256];
static bool vk_anisotropy_syncing;
static bool vk_texture_filter_syncing;

static inline bool VK_UI_Check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) {
        return true;
    }

    Com_SetLastError(va("Vulkan UI %s failed: %d", what, (int)result));
    return false;
}

static bool VK_UI_ArrayBytes(size_t item_size, uint32_t count, size_t *out_size,
                             const char *what)
{
    if (!out_size || (item_size && (size_t)count > SIZE_MAX / item_size)) {
        Com_SetLastError(va("Vulkan UI: %s allocation size overflow", what));
        return false;
    }

    *out_size = item_size * (size_t)count;
    return true;
}

static bool VK_UI_TextureByteSize(int width, int height, size_t bytes_per_pixel,
                                  size_t *out_size);

static bool VK_UI_IsPlayerSkin(const vk_ui_image_t *image)
{
    return image && !Q_stricmpn(image->name, "players/",
                                 sizeof("players/") - 1);
}

static bool VK_UI_ShouldPicmip(const vk_ui_image_t *image)
{
    if (!image || (r_nomip && r_nomip->integer && image->type != IT_WALL)) {
        return false;
    }

    const int filter = r_picmip_filter ? r_picmip_filter->integer : 0;
    if (image->type == IT_WALL) {
        return (filter & 4) == 0;
    }
    if (image->type != IT_SKIN ||
        (vk_gl_downsample_skins && !vk_gl_downsample_skins->integer)) {
        return false;
    }

    const bool player_skin = VK_UI_IsPlayerSkin(image);
    return !((filter & 1) && !player_skin) &&
           !((filter & 2) && player_skin);
}

static void VK_UI_MipMapRgba(const byte *in, int width, int height, byte *out)
{
    const int out_width = max(width >> 1, 1);
    const int out_height = max(height >> 1, 1);

    for (int y = 0; y < out_height; ++y) {
        const int y0 = min(y << 1, height - 1);
        const int y1 = min(y0 + 1, height - 1);
        for (int x = 0; x < out_width; ++x) {
            const int x0 = min(x << 1, width - 1);
            const int x1 = min(x0 + 1, width - 1);
            const byte *p0 = in + ((y0 * width + x0) * 4);
            const byte *p1 = in + ((y0 * width + x1) * 4);
            const byte *p2 = in + ((y1 * width + x0) * 4);
            const byte *p3 = in + ((y1 * width + x1) * 4);
            byte *dst = out + ((y * out_width + x) * 4);
            for (int c = 0; c < 4; ++c) {
                dst[c] = (byte)((p0[c] + p1[c] + p2[c] + p3[c]) >> 2);
            }
        }
    }
}

static bool VK_UI_ApplyPicmip(const vk_ui_image_t *image, int *width,
                              int *height, const byte *rgba, byte **out_rgba)
{
    *out_rgba = NULL;
    if (!VK_UI_ShouldPicmip(image) || !r_picmip) {
        return true;
    }

    const int shift = Cvar_ClampInteger(r_picmip, 0, 31);
    if (!shift || (*width == 1 && *height == 1)) {
        return true;
    }

    int target_width = max(*width >> shift, 1);
    int target_height = max(*height >> shift, 1);
    if (target_width == *width && target_height == *height) {
        return true;
    }

    size_t bytes;
    if (!VK_UI_TextureByteSize(*width, *height, 4, &bytes)) {
        Com_SetLastError("Vulkan UI: picmip source size overflow");
        return false;
    }
    byte *current = malloc(bytes);
    if (!current) {
        Com_SetLastError("Vulkan UI: picmip source allocation failed");
        return false;
    }
    memcpy(current, rgba, bytes);

    int current_width = *width;
    int current_height = *height;
    while (current_width > target_width || current_height > target_height) {
        const int next_width = max(current_width >> 1, 1);
        const int next_height = max(current_height >> 1, 1);
        if (!VK_UI_TextureByteSize(next_width, next_height, 4, &bytes)) {
            free(current);
            Com_SetLastError("Vulkan UI: picmip target size overflow");
            return false;
        }
        byte *next = malloc(bytes);
        if (!next) {
            free(current);
            Com_SetLastError("Vulkan UI: picmip target allocation failed");
            return false;
        }
        VK_UI_MipMapRgba(current, current_width, current_height, next);
        free(current);
        current = next;
        current_width = next_width;
        current_height = next_height;
    }

    *width = current_width;
    *height = current_height;
    *out_rgba = current;
    return true;
}

static bool VK_UI_GrowCapacityTo(uint32_t current, uint32_t needed,
                                 uint32_t initial, uint32_t *out_capacity,
                                 const char *what)
{
    uint32_t new_capacity = current ? current : initial;
    while (new_capacity < needed) {
        if (new_capacity > UINT32_MAX / VK_UI_BUFFER_GROWTH_FACTOR) {
            Com_SetLastError(va("Vulkan UI: %s capacity overflow", what));
            return false;
        }
        new_capacity *= VK_UI_BUFFER_GROWTH_FACTOR;
    }

    *out_capacity = new_capacity;
    return true;
}

static bool VK_UI_GrowCapacityPast(uint32_t current, uint32_t needed_index,
                                   uint32_t initial, uint32_t *out_capacity,
                                   const char *what)
{
    if (needed_index == UINT32_MAX) {
        Com_SetLastError(va("Vulkan UI: %s capacity overflow", what));
        return false;
    }

    return VK_UI_GrowCapacityTo(current, needed_index + 1, initial, out_capacity, what);
}

static bool VK_UI_AddCount(uint32_t value, uint32_t delta, uint32_t *out_value,
                           const char *what)
{
    if (value > UINT32_MAX - delta) {
        Com_SetLastError(va("Vulkan UI: %s count overflow", what));
        return false;
    }

    *out_value = value + delta;
    return true;
}

static bool VK_UI_TexturePixelCount(int width, int height, size_t *out_pixels)
{
    if (!out_pixels || width <= 0 || height <= 0) {
        return false;
    }

    size_t pixels_w = (size_t)width;
    size_t pixels_h = (size_t)height;
    if (pixels_w > SIZE_MAX / pixels_h) {
        return false;
    }

    *out_pixels = pixels_w * pixels_h;
    return true;
}

static bool VK_UI_TextureByteSize(int width, int height, size_t bytes_per_pixel,
                                  size_t *out_bytes)
{
    if (!out_bytes || !bytes_per_pixel) {
        return false;
    }

    size_t pixels;
    if (!VK_UI_TexturePixelCount(width, height, &pixels)) {
        return false;
    }

    if (pixels > SIZE_MAX / bytes_per_pixel) {
        return false;
    }

    *out_bytes = pixels * bytes_per_pixel;
    return true;
}

static inline VkDevice VK_UI_Device(void)
{
    return vk_ui.ctx ? vk_ui.ctx->device : VK_NULL_HANDLE;
}

static vk_ui_frame_buffers_t *VK_UI_CurrentFrameBuffers(void)
{
    if (!vk_ui.ctx || !vk_ui.ctx->frame_count ||
        vk_ui.ctx->current_frame >= vk_ui.ctx->frame_count) {
        return NULL;
    }
    return &vk_ui.frame_buffers[vk_ui.ctx->current_frame];
}

static void VK_UI_RefreshVirtualMetrics(void)
{
    renderer_ui_scale_t metrics = R_UIScaleCompute(r_config.width, r_config.height);
    vk_ui.base_scale = metrics.base_scale;
    vk_ui.virtual_width = metrics.virtual_width;
    vk_ui.virtual_height = metrics.virtual_height;

    if (vk_ui.scale <= 0.0f) {
        vk_ui.scale = 1.0f;
    }
}

static uint32_t VK_UI_FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(vk_ui.ctx->physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & BIT(i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return UINT32_MAX;
}

static void VK_UI_DestroyBuffer(VkBuffer *buffer, VkDeviceMemory *memory, void **mapped)
{
    VkDevice device = VK_UI_Device();

    if (mapped && *mapped) {
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

static bool VK_UI_CreateBuffer(size_t size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags properties,
                               VkBuffer *out_buffer, VkDeviceMemory *out_memory,
                               void **out_mapped)
{
    VkDevice device = VK_UI_Device();

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (!VK_UI_Check(vkCreateBuffer(device, &buffer_info, NULL, out_buffer),
                     "vkCreateBuffer")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *out_buffer, &requirements);

    uint32_t memory_index = VK_UI_FindMemoryType(requirements.memoryTypeBits, properties);
    if (memory_index == UINT32_MAX) {
        Com_SetLastError("Vulkan UI: suitable buffer memory type not found");
        vkDestroyBuffer(device, *out_buffer, NULL);
        *out_buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (!VK_UI_Check(vkAllocateMemory(device, &alloc_info, NULL, out_memory),
                     "vkAllocateMemory")) {
        vkDestroyBuffer(device, *out_buffer, NULL);
        *out_buffer = VK_NULL_HANDLE;
        return false;
    }

    if (!VK_UI_Check(vkBindBufferMemory(device, *out_buffer, *out_memory, 0),
                     "vkBindBufferMemory")) {
        vkDestroyBuffer(device, *out_buffer, NULL);
        vkFreeMemory(device, *out_memory, NULL);
        *out_buffer = VK_NULL_HANDLE;
        *out_memory = VK_NULL_HANDLE;
        return false;
    }

    if (out_mapped) {
        if (!VK_UI_Check(vkMapMemory(device, *out_memory, 0, size, 0, out_mapped),
                         "vkMapMemory")) {
            vkDestroyBuffer(device, *out_buffer, NULL);
            vkFreeMemory(device, *out_memory, NULL);
            *out_buffer = VK_NULL_HANDLE;
            *out_memory = VK_NULL_HANDLE;
            *out_mapped = NULL;
            return false;
        }
    }

    return true;
}

static bool VK_UI_EnsureDrawCapacity(uint32_t needed_vertices,
                                     uint32_t needed_indices,
                                     uint32_t needed_draws)
{
    if (needed_vertices > vk_ui.vertex_capacity) {
        uint32_t new_capacity;
        if (!VK_UI_GrowCapacityTo(vk_ui.vertex_capacity, needed_vertices,
                                  VK_UI_INITIAL_VERTEX_CAPACITY, &new_capacity,
                                  "vertex")) {
            return false;
        }

        size_t new_size;
        if (!VK_UI_ArrayBytes(sizeof(*vk_ui.vertices), new_capacity, &new_size,
                              "vertex")) {
            return false;
        }

        void *new_vertices = realloc(vk_ui.vertices, new_size);
        if (!new_vertices) {
            Com_SetLastError("Vulkan UI: out of memory for vertices");
            return false;
        }

        vk_ui.vertices = new_vertices;
        vk_ui.vertex_capacity = new_capacity;
    }

    if (needed_indices > vk_ui.index_capacity) {
        uint32_t new_capacity;
        if (!VK_UI_GrowCapacityTo(vk_ui.index_capacity, needed_indices,
                                  VK_UI_INITIAL_INDEX_CAPACITY, &new_capacity,
                                  "index")) {
            return false;
        }

        size_t new_size;
        if (!VK_UI_ArrayBytes(sizeof(*vk_ui.indices), new_capacity, &new_size,
                              "index")) {
            return false;
        }

        void *new_indices = realloc(vk_ui.indices, new_size);
        if (!new_indices) {
            Com_SetLastError("Vulkan UI: out of memory for indices");
            return false;
        }

        vk_ui.indices = new_indices;
        vk_ui.index_capacity = new_capacity;
    }

    if (needed_draws > vk_ui.draw_capacity) {
        uint32_t new_capacity;
        if (!VK_UI_GrowCapacityTo(vk_ui.draw_capacity, needed_draws,
                                  VK_UI_INITIAL_DRAW_CAPACITY, &new_capacity,
                                  "draw")) {
            return false;
        }

        size_t new_size;
        if (!VK_UI_ArrayBytes(sizeof(*vk_ui.draws), new_capacity, &new_size,
                              "draw")) {
            return false;
        }

        void *new_draws = realloc(vk_ui.draws, new_size);
        if (!new_draws) {
            Com_SetLastError("Vulkan UI: out of memory for draw commands");
            return false;
        }

        vk_ui.draws = new_draws;
        vk_ui.draw_capacity = new_capacity;
    }

    return true;
}

static bool VK_UI_EnsureHostBuffers(void)
{
    if (!VK_UI_EnsureDrawCapacity(VK_UI_INITIAL_VERTEX_CAPACITY,
                                  VK_UI_INITIAL_INDEX_CAPACITY,
                                  VK_UI_INITIAL_DRAW_CAPACITY)) {
        return false;
    }

    return true;
}

static bool VK_UI_EnsureGpuBuffers(vk_ui_frame_buffers_t *frame)
{
    if (!frame) {
        Com_SetLastError("Vulkan UI: active frame buffers are unavailable");
        return false;
    }
    size_t needed_vertex_bytes;
    size_t needed_index_bytes;
    if (!VK_UI_ArrayBytes(sizeof(*vk_ui.vertices), vk_ui.vertex_capacity,
                          &needed_vertex_bytes, "vertex gpu buffer") ||
        !VK_UI_ArrayBytes(sizeof(*vk_ui.indices), vk_ui.index_capacity,
                          &needed_index_bytes, "index gpu buffer")) {
        return false;
    }

    if (!frame->vertex_buffer || !frame->vertex_memory ||
        !frame->vertex_staging_buffer || !frame->vertex_staging_memory ||
        !frame->vertex_staging_mapped ||
        needed_vertex_bytes > frame->vertex_buffer_bytes) {
        VK_UI_DestroyBuffer(&frame->vertex_staging_buffer,
                            &frame->vertex_staging_memory,
                            &frame->vertex_staging_mapped);
        VK_UI_DestroyBuffer(&frame->vertex_buffer, &frame->vertex_memory,
                            NULL);

        if (!VK_UI_CreateBuffer(needed_vertex_bytes,
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                &frame->vertex_buffer,
                                &frame->vertex_memory,
                                NULL) ||
            !VK_UI_CreateBuffer(needed_vertex_bytes,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &frame->vertex_staging_buffer,
                                &frame->vertex_staging_memory,
                                &frame->vertex_staging_mapped)) {
            VK_UI_DestroyBuffer(&frame->vertex_staging_buffer,
                                &frame->vertex_staging_memory,
                                &frame->vertex_staging_mapped);
            VK_UI_DestroyBuffer(&frame->vertex_buffer, &frame->vertex_memory,
                                NULL);
            return false;
        }

        frame->vertex_buffer_bytes = needed_vertex_bytes;
    }

    if (!frame->index_buffer || !frame->index_memory ||
        !frame->index_staging_buffer || !frame->index_staging_memory ||
        !frame->index_staging_mapped ||
        needed_index_bytes > frame->index_buffer_bytes) {
        VK_UI_DestroyBuffer(&frame->index_staging_buffer,
                            &frame->index_staging_memory,
                            &frame->index_staging_mapped);
        VK_UI_DestroyBuffer(&frame->index_buffer, &frame->index_memory,
                            NULL);

        if (!VK_UI_CreateBuffer(needed_index_bytes,
                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                &frame->index_buffer,
                                &frame->index_memory,
                                NULL) ||
            !VK_UI_CreateBuffer(needed_index_bytes,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &frame->index_staging_buffer,
                                &frame->index_staging_memory,
                                &frame->index_staging_mapped)) {
            VK_UI_DestroyBuffer(&frame->index_staging_buffer,
                                &frame->index_staging_memory,
                                &frame->index_staging_mapped);
            VK_UI_DestroyBuffer(&frame->index_buffer, &frame->index_memory,
                                NULL);
            return false;
        }

        frame->index_buffer_bytes = needed_index_bytes;
    }

    return true;
}

static bool VK_UI_EnsureShowTrisCapacity(uint32_t needed)
{
    if (needed <= vk_ui.showtris_vertex_capacity) {
        return true;
    }

    uint32_t capacity;
    if (!VK_UI_GrowCapacityTo(vk_ui.showtris_vertex_capacity, needed,
                              VK_UI_INITIAL_SHOWTRIS_VERTEX_CAPACITY,
                              &capacity, "show-tris vertex")) {
        return false;
    }
    size_t bytes;
    if (!VK_UI_ArrayBytes(sizeof(*vk_ui.showtris_vertices), capacity,
                          &bytes, "show-tris vertex")) {
        return false;
    }
    void *vertices = realloc(vk_ui.showtris_vertices, bytes);
    if (!vertices) {
        Com_WPrintf("Vulkan UI: unable to grow show-tris vertex queue\n");
        return false;
    }
    vk_ui.showtris_vertices = vertices;
    vk_ui.showtris_vertex_capacity = capacity;
    return true;
}

static bool VK_UI_EnsureShowTrisGpuBuffer(vk_ui_frame_buffers_t *frame)
{
    if (!frame || !vk_ui.showtris_vertex_capacity) {
        return false;
    }

    size_t needed_bytes;
    if (!VK_UI_ArrayBytes(sizeof(*vk_ui.showtris_vertices),
                          vk_ui.showtris_vertex_capacity, &needed_bytes,
                          "show-tris vertex gpu buffer")) {
        return false;
    }
    if (frame->showtris_vertex_buffer && frame->showtris_vertex_memory &&
        frame->showtris_vertex_staging_buffer &&
        frame->showtris_vertex_staging_memory &&
        frame->showtris_vertex_staging_mapped &&
        frame->showtris_vertex_buffer_bytes >= needed_bytes) {
        return true;
    }

    VK_UI_DestroyBuffer(&frame->showtris_vertex_staging_buffer,
                        &frame->showtris_vertex_staging_memory,
                        &frame->showtris_vertex_staging_mapped);
    VK_UI_DestroyBuffer(&frame->showtris_vertex_buffer,
                        &frame->showtris_vertex_memory, NULL);
    frame->showtris_vertex_buffer_bytes = 0;

    if (!VK_UI_CreateBuffer(needed_bytes,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            &frame->showtris_vertex_buffer,
                            &frame->showtris_vertex_memory, NULL) ||
        !VK_UI_CreateBuffer(needed_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &frame->showtris_vertex_staging_buffer,
                            &frame->showtris_vertex_staging_memory,
                            &frame->showtris_vertex_staging_mapped)) {
        VK_UI_DestroyBuffer(&frame->showtris_vertex_staging_buffer,
                            &frame->showtris_vertex_staging_memory,
                            &frame->showtris_vertex_staging_mapped);
        VK_UI_DestroyBuffer(&frame->showtris_vertex_buffer,
                            &frame->showtris_vertex_memory, NULL);
        return false;
    }
    frame->showtris_vertex_buffer_bytes = needed_bytes;
    return true;
}

static bool VK_UI_BuildShowTris(void)
{
    vk_ui.showtris_vertex_count = 0;
    for (uint32_t i = 0; i < vk_ui.draw_count; ++i) {
        vk_ui.draws[i].showtris_first_vertex = 0;
        vk_ui.draws[i].showtris_vertex_count = 0;
    }

    if (!VK_Debug_ShowTris(VK_DEBUG_SHOWTRIS_PIC) || !vk_ui.draw_count ||
        !vk_ui.vertex_count || !vk_ui.index_count) {
        return true;
    }
    if (vk_ui.index_count > UINT32_MAX / 2 ||
        !VK_UI_EnsureShowTrisCapacity(vk_ui.index_count * 2)) {
        return false;
    }

    for (uint32_t draw_index = 0; draw_index < vk_ui.draw_count; ++draw_index) {
        vk_ui_draw_t *draw = &vk_ui.draws[draw_index];
        if (!draw->index_count || draw->first_index >= vk_ui.index_count ||
            draw->index_count > vk_ui.index_count - draw->first_index) {
            continue;
        }

        const uint32_t triangle_index_count = draw->index_count -
            draw->index_count % 3;
        draw->showtris_first_vertex = vk_ui.showtris_vertex_count;
        for (uint32_t index = 0; index < triangle_index_count; index += 3) {
            const uint32_t ia = vk_ui.indices[draw->first_index + index + 0];
            const uint32_t ib = vk_ui.indices[draw->first_index + index + 1];
            const uint32_t ic = vk_ui.indices[draw->first_index + index + 2];
            if (ia >= vk_ui.vertex_count || ib >= vk_ui.vertex_count ||
                ic >= vk_ui.vertex_count) {
                continue;
            }
            if (vk_ui.showtris_vertex_count > UINT32_MAX - 6) {
                Com_WPrintf("Vulkan UI: show-tris vertex stream is too large\n");
                return false;
            }

            const vk_ui_vertex_t *a = &vk_ui.vertices[ia];
            const vk_ui_vertex_t *b = &vk_ui.vertices[ib];
            const vk_ui_vertex_t *c = &vk_ui.vertices[ic];
            vk_ui_vertex_t *out = vk_ui.showtris_vertices +
                vk_ui.showtris_vertex_count;
            out[0] = (vk_ui_vertex_t){ .pos = { a->pos[0], a->pos[1] },
                                        .color = COLOR_WHITE.u32 };
            out[1] = (vk_ui_vertex_t){ .pos = { b->pos[0], b->pos[1] },
                                        .color = COLOR_WHITE.u32 };
            out[2] = (vk_ui_vertex_t){ .pos = { b->pos[0], b->pos[1] },
                                        .color = COLOR_WHITE.u32 };
            out[3] = (vk_ui_vertex_t){ .pos = { c->pos[0], c->pos[1] },
                                        .color = COLOR_WHITE.u32 };
            out[4] = (vk_ui_vertex_t){ .pos = { c->pos[0], c->pos[1] },
                                        .color = COLOR_WHITE.u32 };
            out[5] = (vk_ui_vertex_t){ .pos = { a->pos[0], a->pos[1] },
                                        .color = COLOR_WHITE.u32 };
            vk_ui.showtris_vertex_count += 6;
            draw->showtris_vertex_count += 6;
        }
    }

    return true;
}

static void VK_UI_DestroyImageResources(vk_ui_image_t *image)
{
    if (!image || !vk_ui.ctx || !vk_ui.ctx->device) {
        return;
    }

    VkDevice device = vk_ui.ctx->device;

    if (vk_ui.descriptor_pool && vk_ui.descriptor_set_layout && image->descriptor_set) {
        vkFreeDescriptorSets(device, vk_ui.descriptor_pool, 1, &image->descriptor_set);
        image->descriptor_set = VK_NULL_HANDLE;
    }

    if (image->view) {
        vkDestroyImageView(device, image->view, NULL);
        image->view = VK_NULL_HANDLE;
    }

    if (image->image) {
        vkDestroyImage(device, image->image, NULL);
        image->image = VK_NULL_HANDLE;
    }

    if (image->image_memory) {
        vkFreeMemory(device, image->image_memory, NULL);
        image->image_memory = VK_NULL_HANDLE;
    }
    image->mip_levels = 0;
}

static vk_ui_image_t *VK_UI_ImageForHandle(qhandle_t handle)
{
    if (handle <= 0 || (uint32_t)handle >= vk_ui.image_capacity) {
        return NULL;
    }

    vk_ui_image_t *image = &vk_ui.images[handle];
    if (!image->in_use) {
        return NULL;
    }

    return image;
}

static bool VK_UI_EnsureImageCapacity(uint32_t needed)
{
    if (needed < vk_ui.image_capacity) {
        return true;
    }

    uint32_t new_capacity;
    if (!VK_UI_GrowCapacityPast(vk_ui.image_capacity, needed,
                                VK_UI_INITIAL_IMAGE_CAPACITY, &new_capacity,
                                "image")) {
        return false;
    }

    size_t new_size;
    if (!VK_UI_ArrayBytes(sizeof(*vk_ui.images), new_capacity, &new_size,
                          "image")) {
        return false;
    }

    size_t old_size;
    if (!VK_UI_ArrayBytes(sizeof(*vk_ui.images), vk_ui.image_capacity, &old_size,
                          "image")) {
        return false;
    }

    void *new_images = realloc(vk_ui.images, new_size);
    if (!new_images) {
        Com_SetLastError("Vulkan UI: out of memory for image handles");
        return false;
    }

    memset((byte *)new_images + old_size, 0, new_size - old_size);

    vk_ui.images = new_images;
    vk_ui.image_capacity = new_capacity;

    return true;
}

static vk_ui_image_t *VK_UI_AllocImageSlot(qhandle_t *out_handle)
{
    for (uint32_t i = 1; i < vk_ui.image_capacity; ++i) {
        if (!vk_ui.images[i].in_use) {
            vk_ui.images[i].in_use = true;
            if (out_handle) {
                *out_handle = (qhandle_t)i;
            }
            return &vk_ui.images[i];
        }
    }

    uint32_t handle = vk_ui.image_capacity;
    if (!VK_UI_EnsureImageCapacity(handle)) {
        return NULL;
    }

    vk_ui.images[handle].in_use = true;

    if (out_handle) {
        *out_handle = (qhandle_t)handle;
    }

    return &vk_ui.images[handle];
}

static vk_ui_image_t *VK_UI_FindImageByName(const char *name, imagetype_t type)
{
    if (!name || !*name) {
        return NULL;
    }

    for (uint32_t i = 1; i < vk_ui.image_capacity; ++i) {
        vk_ui_image_t *image = &vk_ui.images[i];
        if (!image->in_use) {
            continue;
        }

        if (image->type != type) {
            continue;
        }

        if (!strcmp(image->name, name)) {
            return image;
        }
    }

    return NULL;
}

static bool VK_UI_BeginImmediate(VkCommandBuffer *out_cmd)
{
    if (!vk_ui.ctx || !vk_ui.ctx->command_pool) {
        Com_SetLastError("Vulkan UI: command pool is not available");
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk_ui.ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (!VK_UI_Check(vkAllocateCommandBuffers(vk_ui.ctx->device, &alloc_info, out_cmd),
                     "vkAllocateCommandBuffers")) {
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    if (!VK_UI_Check(vkBeginCommandBuffer(*out_cmd, &begin_info), "vkBeginCommandBuffer")) {
        vkFreeCommandBuffers(vk_ui.ctx->device, vk_ui.ctx->command_pool, 1, out_cmd);
        *out_cmd = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

static bool VK_UI_EndImmediate(VkCommandBuffer cmd)
{
    if (!VK_UI_Check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer")) {
        vkFreeCommandBuffers(vk_ui.ctx->device, vk_ui.ctx->command_pool, 1, &cmd);
        return false;
    }

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };

    if (!VK_UI_Check(vkQueueSubmit(vk_ui.ctx->graphics_queue, 1, &submit_info, VK_NULL_HANDLE),
                     "vkQueueSubmit")) {
        vkFreeCommandBuffers(vk_ui.ctx->device, vk_ui.ctx->command_pool, 1, &cmd);
        return false;
    }

    if (!VK_UI_Check(vkQueueWaitIdle(vk_ui.ctx->graphics_queue), "vkQueueWaitIdle")) {
        vkFreeCommandBuffers(vk_ui.ctx->device, vk_ui.ctx->command_pool, 1, &cmd);
        return false;
    }

    vkFreeCommandBuffers(vk_ui.ctx->device, vk_ui.ctx->command_pool, 1, &cmd);
    return true;
}

static bool VK_UI_SupportsLinearMipmapBlit(void) {
  if (!vk_ui.ctx || !vk_ui.ctx->physical_device) {
    return false;
  }

  VkFormatProperties properties;
  vkGetPhysicalDeviceFormatProperties(vk_ui.ctx->physical_device,
                                      VK_FORMAT_R8G8B8A8_UNORM, &properties);
  const VkFormatFeatureFlags required =
      VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  return (properties.optimalTilingFeatures & required) == required;
}

static uint32_t VK_UI_ImageMipLevelCount(const vk_ui_image_t *image, int width,
                                         int height) {
  // GL_Upload32 generates a full mip chain only for material textures.
  // Preserve that contract: fonts, pictures, and raw updates retain their
  // single level, while walls/skins avoid aliasing during scaled rendering.
  if (!image || (image->type != IT_WALL && image->type != IT_SKIN) ||
      !VK_UI_SupportsLinearMipmapBlit()) {
    return 1;
  }

  uint32_t levels = 1;
  while (width > 1 || height > 1) {
    width = max(width >> 1, 1);
    height = max(height >> 1, 1);
    ++levels;
  }
  return levels;
}

static bool VK_UI_CreateImageStorage(vk_ui_image_t *image, int width, int height)
{
    VkDevice device = VK_UI_Device();
    image->mip_levels = VK_UI_ImageMipLevelCount(image, width, height);

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {
            .width = (uint32_t)width,
            .height = (uint32_t)height,
            .depth = 1,
        },
        .mipLevels = image->mip_levels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (!VK_UI_Check(vkCreateImage(device, &image_info, NULL, &image->image),
                     "vkCreateImage")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image->image, &requirements);

    uint32_t memory_index = VK_UI_FindMemoryType(requirements.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_index == UINT32_MAX) {
        Com_SetLastError("Vulkan UI: suitable image memory type not found");
        vkDestroyImage(device, image->image, NULL);
        image->image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (!VK_UI_Check(vkAllocateMemory(device, &alloc_info, NULL, &image->image_memory),
                     "vkAllocateMemory")) {
        vkDestroyImage(device, image->image, NULL);
        image->image = VK_NULL_HANDLE;
        return false;
    }

    if (!VK_UI_Check(vkBindImageMemory(device, image->image, image->image_memory, 0),
                     "vkBindImageMemory")) {
        vkDestroyImage(device, image->image, NULL);
        vkFreeMemory(device, image->image_memory, NULL);
        image->image = VK_NULL_HANDLE;
        image->image_memory = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = image->mip_levels,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    if (!VK_UI_Check(vkCreateImageView(device, &view_info, NULL, &image->view),
                     "vkCreateImageView")) {
        vkDestroyImage(device, image->image, NULL);
        vkFreeMemory(device, image->image_memory, NULL);
        image->image = VK_NULL_HANDLE;
        image->image_memory = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

static bool VK_UI_AllocDescriptorSet(vk_ui_image_t *image)
{
    if (image->descriptor_set) {
        return true;
    }

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk_ui.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vk_ui.descriptor_set_layout,
    };

    if (!VK_UI_Check(vkAllocateDescriptorSets(vk_ui.ctx->device, &alloc_info, &image->descriptor_set),
                     "vkAllocateDescriptorSets")) {
        image->descriptor_set = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

VkDescriptorSet VK_UI_CreateExternalImageDescriptor(VkImageView view,
                                                     VkImageLayout layout)
{
    return VK_UI_CreateExternalImageTripleDescriptor(
        view, layout, view, layout, view, layout);
}

VkDescriptorSet VK_UI_CreateExternalImagePairDescriptor(
    VkImageView first_view, VkImageLayout first_layout,
    VkImageView second_view, VkImageLayout second_layout)
{
    return VK_UI_CreateExternalImageTripleDescriptor(
        first_view, first_layout, second_view, second_layout,
        second_view, second_layout);
}

VkDescriptorSet VK_UI_CreateExternalImageTripleDescriptor(
    VkImageView first_view, VkImageLayout first_layout,
    VkImageView second_view, VkImageLayout second_layout,
    VkImageView third_view, VkImageLayout third_layout)
{
    if (!vk_ui.initialized || !vk_ui.ctx || !vk_ui.ctx->device || !first_view ||
        !second_view || !third_view ||
        !vk_ui.descriptor_pool || !vk_ui.descriptor_set_layout ||
        !vk_ui.sampler_clamp) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk_ui.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vk_ui.descriptor_set_layout,
    };
    if (!VK_UI_Check(vkAllocateDescriptorSets(vk_ui.ctx->device, &alloc_info,
                                              &set),
                     "vkAllocateDescriptorSets(external image)")) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo first_info = {
        .sampler = vk_ui.sampler_clamp,
        .imageView = first_view,
        .imageLayout = first_layout,
    };
    VkDescriptorImageInfo second_info = {
        .sampler = vk_ui.sampler_clamp,
        .imageView = second_view,
        .imageLayout = second_layout,
    };
    VkDescriptorImageInfo third_info = {
        .sampler = vk_ui.sampler_clamp,
        .imageView = third_view,
        .imageLayout = third_layout,
    };
    VkWriteDescriptorSet writes[3] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &first_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &second_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &third_info,
        },
    };
    vkUpdateDescriptorSets(vk_ui.ctx->device, q_countof(writes), writes, 0, NULL);
    return set;
}

void VK_UI_DestroyExternalImageDescriptor(VkDescriptorSet *set)
{
    if (!set || !*set || !vk_ui.initialized || !vk_ui.ctx ||
        !vk_ui.ctx->device || !vk_ui.descriptor_pool) {
        return;
    }
    vkFreeDescriptorSets(vk_ui.ctx->device, vk_ui.descriptor_pool, 1, set);
    *set = VK_NULL_HANDLE;
}

static void VK_UI_UpdateDescriptorSet(vk_ui_image_t *image)
{
    // Match GL_SetFilterAndRepeat: walls and skins always wrap (rerelease MD5
    // viewmodels rely on UVs outside 0..1), everything else clamps unless
    // IF_REPEAT is set.
    bool repeat = image->type == IT_WALL || image->type == IT_SKIN ||
                  (image->flags & IF_REPEAT);
    bool nearest = (image->flags & IF_NEAREST) != 0;
    if (!nearest && image->type == IT_FONT) {
        nearest = !vk_bilerp_chars || vk_bilerp_chars->integer == 0;
    } else if (!nearest && image->type == IT_PIC) {
        int bilerp_pics = vk_bilerp_pics ? vk_bilerp_pics->integer : 0;
        nearest = (image->flags & IF_SCRAP) ? bilerp_pics <= 1 : bilerp_pics == 0;
    } else if (!nearest && image->type == IT_SKY) {
        nearest = !vk_bilerp_skies || vk_bilerp_skies->integer == 0;
    }
    VkSampler sampler;
    if (image->type == IT_WALL || image->type == IT_SKIN) {
        sampler = vk_ui.sampler_material_repeat;
    } else {
        sampler = nearest
            ? (repeat ? vk_ui.sampler_nearest_repeat : vk_ui.sampler_nearest_clamp)
            : (repeat ? vk_ui.sampler_repeat : vk_ui.sampler_clamp);
    }
    VkDescriptorImageInfo image_info = {
        .sampler = sampler,
        .imageView = image->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo glow_info = image_info;
    vk_ui_image_t *glow = VK_UI_ImageForHandle(image->glow_image);
    if (glow && glow->view) {
        glow_info.imageView = glow->view;
    } else {
        vk_ui_image_t *fallback = VK_UI_ImageForHandle(vk_ui.white_image);
        // During creation of the white fallback itself, it is the only valid
        // image to use for both material bindings.
        glow_info.imageView = fallback && fallback->view
            ? fallback->view : image->view;
    }

    VkWriteDescriptorSet writes[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = image->descriptor_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = image->descriptor_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &glow_info,
        },
    };

    vkUpdateDescriptorSets(vk_ui.ctx->device, q_countof(writes), writes, 0, NULL);
}

static void VK_UI_DestroySamplers(VkDevice device, VkSampler *repeat,
                                  VkSampler *clamp, VkSampler *nearest_repeat,
                                  VkSampler *nearest_clamp,
                                  VkSampler *material_repeat)
{
    if (!device) {
        return;
    }
    if (clamp && *clamp) {
        vkDestroySampler(device, *clamp, NULL);
        *clamp = VK_NULL_HANDLE;
    }
    if (nearest_clamp && *nearest_clamp) {
        vkDestroySampler(device, *nearest_clamp, NULL);
        *nearest_clamp = VK_NULL_HANDLE;
    }
    if (nearest_repeat && *nearest_repeat) {
        vkDestroySampler(device, *nearest_repeat, NULL);
        *nearest_repeat = VK_NULL_HANDLE;
    }
    if (repeat && *repeat) {
        vkDestroySampler(device, *repeat, NULL);
        *repeat = VK_NULL_HANDLE;
    }
    if (material_repeat && *material_repeat) {
        vkDestroySampler(device, *material_repeat, NULL);
        *material_repeat = VK_NULL_HANDLE;
    }
}

static float VK_UI_Anisotropy(void)
{
    if (!vk_ui.ctx || !vk_ui.ctx->sampler_anisotropy_supported ||
        !r_anisotropy) {
        return 1.0f;
    }
    return Cvar_ClampValue(r_anisotropy, 1.0f,
                           max(vk_ui.ctx->max_sampler_anisotropy, 1.0f));
}

static bool VK_UI_IsMaterialFilter(const char *filter)
{
    return filter && (!Q_stricmp(filter, "GL_NEAREST") ||
        !Q_stricmp(filter, "GL_LINEAR") ||
        !Q_stricmp(filter, "GL_NEAREST_MIPMAP_NEAREST") ||
        !Q_stricmp(filter, "GL_LINEAR_MIPMAP_NEAREST") ||
        !Q_stricmp(filter, "GL_NEAREST_MIPMAP_LINEAR") ||
        !Q_stricmp(filter, "GL_LINEAR_MIPMAP_LINEAR") ||
        !Q_stricmp(filter, "MAG_NEAREST"));
}

static void VK_UI_ConfigureMaterialSampler(VkSamplerCreateInfo *sampler_info)
{
    const char *filter = r_texture_filter ? r_texture_filter->string
                                          : "GL_LINEAR_MIPMAP_LINEAR";

    sampler_info->magFilter = VK_FILTER_LINEAR;
    sampler_info->minFilter = VK_FILTER_LINEAR;
    sampler_info->mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info->maxLod = VK_LOD_CLAMP_NONE;

    if (!Q_stricmp(filter, "GL_NEAREST")) {
        sampler_info->magFilter = VK_FILTER_NEAREST;
        sampler_info->minFilter = VK_FILTER_NEAREST;
        sampler_info->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info->maxLod = 0.0f;
    } else if (!Q_stricmp(filter, "GL_LINEAR")) {
        sampler_info->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info->maxLod = 0.0f;
    } else if (!Q_stricmp(filter, "GL_NEAREST_MIPMAP_NEAREST")) {
        sampler_info->magFilter = VK_FILTER_NEAREST;
        sampler_info->minFilter = VK_FILTER_NEAREST;
        sampler_info->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    } else if (!Q_stricmp(filter, "GL_LINEAR_MIPMAP_NEAREST")) {
        sampler_info->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    } else if (!Q_stricmp(filter, "GL_NEAREST_MIPMAP_LINEAR")) {
        sampler_info->magFilter = VK_FILTER_NEAREST;
        sampler_info->minFilter = VK_FILTER_NEAREST;
    } else if (!Q_stricmp(filter, "MAG_NEAREST")) {
        sampler_info->magFilter = VK_FILTER_NEAREST;
    }
}

static bool VK_UI_CreateSamplers(vk_context_t *ctx, VkSampler *out_repeat,
                                 VkSampler *out_clamp,
                                 VkSampler *out_nearest_repeat,
                                 VkSampler *out_nearest_clamp,
                                 VkSampler *out_material_repeat)
{
    if (!ctx || !ctx->device || !out_repeat || !out_clamp ||
        !out_nearest_repeat || !out_nearest_clamp || !out_material_repeat) {
        Com_SetLastError("Vulkan UI: sampler creation context is missing");
        return false;
    }

    *out_repeat = VK_NULL_HANDLE;
    *out_clamp = VK_NULL_HANDLE;
    *out_nearest_repeat = VK_NULL_HANDLE;
    *out_nearest_clamp = VK_NULL_HANDLE;
    *out_material_repeat = VK_NULL_HANDLE;

    const float anisotropy = VK_UI_Anisotropy();
    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = anisotropy > 1.0f ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = anisotropy,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        // Most UI views expose one mip, so their view bounds still clamp
        // sampling to level zero. The post-process bloom view deliberately
        // exposes its generated mip chain for textureLod in the final pass.
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE,
    };
    VkSamplerCreateInfo material_info = sampler_info;

    if (!VK_UI_Check(vkCreateSampler(ctx->device, &sampler_info, NULL,
                                     out_repeat),
                     "vkCreateSampler(repeat)")) {
        goto fail;
    }

    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!VK_UI_Check(vkCreateSampler(ctx->device, &sampler_info, NULL,
                                     out_clamp),
                     "vkCreateSampler(clamp)")) {
        goto fail;
    }

    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (!VK_UI_Check(vkCreateSampler(ctx->device, &sampler_info, NULL,
                                     out_nearest_repeat),
                     "vkCreateSampler(nearest repeat)")) {
        goto fail;
    }

    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!VK_UI_Check(vkCreateSampler(ctx->device, &sampler_info, NULL,
                                     out_nearest_clamp),
                     "vkCreateSampler(nearest clamp)")) {
        goto fail;
    }

    VK_UI_ConfigureMaterialSampler(&material_info);
    if (!VK_UI_Check(vkCreateSampler(ctx->device, &material_info, NULL,
                                     out_material_repeat),
                     "vkCreateSampler(material repeat)")) {
        goto fail;
    }

    return true;

fail:
    VK_UI_DestroySamplers(ctx->device, out_repeat, out_clamp,
                          out_nearest_repeat, out_nearest_clamp,
                          out_material_repeat);
    return false;
}

static void VK_UI_RebindImageDescriptors(void)
{
    for (uint32_t i = 1; i < vk_ui.image_capacity; ++i) {
        vk_ui_image_t *image = &vk_ui.images[i];
        if (image->in_use && image->view && image->descriptor_set) {
            VK_UI_UpdateDescriptorSet(image);
        }
    }
}

static void VK_UI_BilerpChanged(cvar_t *self)
{
    (void)self;
    if (!vk_ui.initialized || !vk_ui.ctx || !vk_ui.ctx->device) {
        return;
    }

    // Descriptor-bound samplers may be referenced by an in-flight frame. This
    // is a rare user preference change, so drain safely before rebinding every
    // resident native UI image to its newly selected sampler.
    if (!VK_UI_Check(vkDeviceWaitIdle(vk_ui.ctx->device),
                     "vkDeviceWaitIdle(filter update)")) {
        return;
    }

    VK_UI_RebindImageDescriptors();
}

static void VK_UI_RecreateSamplers(const char *wait_label)
{
    if (!vk_ui.initialized || !vk_ui.ctx || !vk_ui.ctx->device) {
        return;
    }

    if (!VK_UI_Check(vkDeviceWaitIdle(vk_ui.ctx->device), wait_label)) {
        return;
    }

    VkSampler repeat = VK_NULL_HANDLE;
    VkSampler clamp = VK_NULL_HANDLE;
    VkSampler nearest_repeat = VK_NULL_HANDLE;
    VkSampler nearest_clamp = VK_NULL_HANDLE;
    VkSampler material_repeat = VK_NULL_HANDLE;
    if (!VK_UI_CreateSamplers(vk_ui.ctx, &repeat, &clamp,
                              &nearest_repeat, &nearest_clamp,
                              &material_repeat)) {
        return;
    }

    VK_UI_DestroySamplers(vk_ui.ctx->device, &vk_ui.sampler_repeat,
                          &vk_ui.sampler_clamp,
                          &vk_ui.sampler_nearest_repeat,
                          &vk_ui.sampler_nearest_clamp,
                          &vk_ui.sampler_material_repeat);
    vk_ui.sampler_repeat = repeat;
    vk_ui.sampler_clamp = clamp;
    vk_ui.sampler_nearest_repeat = nearest_repeat;
    vk_ui.sampler_nearest_clamp = nearest_clamp;
    vk_ui.sampler_material_repeat = material_repeat;
    VK_UI_RebindImageDescriptors();
}

static void VK_UI_AnisotropyChanged(cvar_t *self)
{
    if (vk_anisotropy_syncing || !self) {
        return;
    }

    vk_anisotropy_syncing = true;
    if (self == r_anisotropy && vk_anisotropy) {
        Cvar_SetByVar(vk_anisotropy, self->string, FROM_CODE);
    } else if (self == vk_anisotropy && r_anisotropy) {
        Cvar_SetByVar(r_anisotropy, self->string, FROM_CODE);
    }
    vk_anisotropy_syncing = false;

    VK_UI_RecreateSamplers("vkDeviceWaitIdle(anisotropy update)");
}

static void VK_UI_TextureFilterChanged(cvar_t *self)
{
    if (vk_texture_filter_syncing || !self) {
        return;
    }

    vk_texture_filter_syncing = true;
    if (self == r_texture_filter && vk_gl_texturemode_legacy) {
        Cvar_SetByVar(vk_gl_texturemode_legacy, self->string, FROM_CODE);
    } else if (self == vk_gl_texturemode_legacy && r_texture_filter) {
        Cvar_SetByVar(r_texture_filter, self->string, FROM_CODE);
    }
    vk_texture_filter_syncing = false;

    if (r_texture_filter &&
        !VK_UI_IsMaterialFilter(r_texture_filter->string)) {
        Com_WPrintf("Bad texture mode: %s\n", r_texture_filter->string);
        vk_texture_filter_syncing = true;
        Cvar_Reset(r_texture_filter);
        if (vk_gl_texturemode_legacy) {
            Cvar_SetByVar(vk_gl_texturemode_legacy,
                          r_texture_filter->string, FROM_CODE);
        }
        vk_texture_filter_syncing = false;
    }

    VK_UI_RecreateSamplers("vkDeviceWaitIdle(texture filter update)");
}

static void VK_UI_RegisterAnisotropyCvars(vk_context_t *ctx)
{
    const float default_anisotropy =
        ctx->sampler_anisotropy_supported
            ? max(ctx->max_sampler_anisotropy, 1.0f) : 1.0f;

    vk_anisotropy = Cvar_Get("vk_anisotropy", va("%g", default_anisotropy),
                             CVAR_ARCHIVE);
    r_anisotropy = Cvar_Get("r_anisotropy", vk_anisotropy->string,
                            CVAR_ARCHIVE);

    if (!(r_anisotropy->flags & CVAR_MODIFIED) &&
        (vk_anisotropy->flags & CVAR_MODIFIED)) {
        Cvar_SetByVar(r_anisotropy, vk_anisotropy->string, FROM_CODE);
    } else {
        Cvar_SetByVar(vk_anisotropy, r_anisotropy->string, FROM_CODE);
    }

    vk_anisotropy->changed = VK_UI_AnisotropyChanged;
    r_anisotropy->changed = VK_UI_AnisotropyChanged;
}

static void VK_UI_UnregisterAnisotropyCvars(void)
{
    if (vk_anisotropy &&
        vk_anisotropy->changed == VK_UI_AnisotropyChanged) {
        vk_anisotropy->changed = NULL;
    }
    if (r_anisotropy && r_anisotropy->changed == VK_UI_AnisotropyChanged) {
        r_anisotropy->changed = NULL;
    }

    vk_anisotropy = NULL;
    r_anisotropy = NULL;
    vk_anisotropy_syncing = false;
}

static void VK_UI_RegisterTextureFilterCvars(void)
{
    vk_gl_texturemode_legacy = Cvar_Get("gl_texturemode",
                                        "GL_LINEAR_MIPMAP_LINEAR",
                                        CVAR_ARCHIVE);
    r_texture_filter = Cvar_Get("r_texture_filter",
                                vk_gl_texturemode_legacy->string,
                                CVAR_ARCHIVE);

    if (!(r_texture_filter->flags & CVAR_MODIFIED) &&
        (vk_gl_texturemode_legacy->flags & CVAR_MODIFIED)) {
        Cvar_SetByVar(r_texture_filter, vk_gl_texturemode_legacy->string,
                      FROM_CODE);
    } else {
        Cvar_SetByVar(vk_gl_texturemode_legacy, r_texture_filter->string,
                      FROM_CODE);
    }

    vk_gl_texturemode_legacy->changed = VK_UI_TextureFilterChanged;
    r_texture_filter->changed = VK_UI_TextureFilterChanged;
}

static void VK_UI_UnregisterTextureFilterCvars(void)
{
    if (vk_gl_texturemode_legacy &&
        vk_gl_texturemode_legacy->changed == VK_UI_TextureFilterChanged) {
        vk_gl_texturemode_legacy->changed = NULL;
    }
    if (r_texture_filter &&
        r_texture_filter->changed == VK_UI_TextureFilterChanged) {
        r_texture_filter->changed = NULL;
    }

    vk_gl_texturemode_legacy = NULL;
    r_texture_filter = NULL;
    vk_texture_filter_syncing = false;
}

static void VK_UI_TransitionImageMipRange(
    VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout,
    VkImageLayout new_layout, uint32_t base_mip_level, uint32_t level_count,
    VkAccessFlags src_access, VkAccessFlags dst_access,
    VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = base_mip_level,
            .levelCount = level_count,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL,
                         1, &barrier);
}

static bool VK_UI_UploadImageData(vk_ui_image_t *image, int width, int height,
                                  const byte *rgba, VkImageLayout old_layout) {
  size_t pixel_size;
  if (!VK_UI_TextureByteSize(width, height, 4, &pixel_size)) {
    Com_SetLastError("Vulkan UI: image upload size overflow");
    return false;
  }

  VkBuffer staging_buffer = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  void *staging_mapped = NULL;

  if (!VK_UI_CreateBuffer(pixel_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &staging_buffer, &staging_memory, &staging_mapped)) {
    return false;
  }

  memcpy(staging_mapped, rgba, pixel_size);

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (!VK_UI_BeginImmediate(&cmd)) {
    VK_UI_DestroyBuffer(&staging_buffer, &staging_memory, &staging_mapped);
    return false;
  }

  const uint32_t mip_levels = max(image->mip_levels, 1u);
  const VkPipelineStageFlags old_stage =
      old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
          ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
          : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  const VkAccessFlags old_access =
      old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
          ? VK_ACCESS_SHADER_READ_BIT
          : 0;
  VK_UI_TransitionImageMipRange(
      cmd, image->image, old_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
      mip_levels, old_access, VK_ACCESS_TRANSFER_WRITE_BIT, old_stage,
      VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkBufferImageCopy copy_region = {
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .mipLevel = 0,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
      .imageOffset = {0, 0, 0},
      .imageExtent =
          {
              .width = (uint32_t)width,
              .height = (uint32_t)height,
              .depth = 1,
          },
  };

  vkCmdCopyBufferToImage(cmd, staging_buffer, image->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

  // Mirrors GL_Upload32's glGenerateMipmap path for wall/skin material
  // images. Keep each completed level transfer-readable before blitting it
  // into the next one, then make the whole chain shader-readable together.
  for (uint32_t level = 1; level < mip_levels; ++level) {
    VK_UI_TransitionImageMipRange(
        cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, level - 1, 1,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    const int src_width = max(width >> (level - 1), 1);
    const int src_height = max(height >> (level - 1), 1);
    const int dst_width = max(width >> level, 1);
    const int dst_height = max(height >> level, 1);
    VkImageBlit blit = {
        .srcSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = level - 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .srcOffsets =
            {
                {0, 0, 0},
                {src_width, src_height, 1},
            },
        .dstSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = level,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .dstOffsets =
            {
                {0, 0, 0},
                {dst_width, dst_height, 1},
            },
    };
    vkCmdBlitImage(cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);
  }

  if (mip_levels > 1) {
    VK_UI_TransitionImageMipRange(
        cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, mip_levels - 1,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  }
  VK_UI_TransitionImageMipRange(
      cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mip_levels - 1, 1,
      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

  bool ok = VK_UI_EndImmediate(cmd);
  VK_UI_DestroyBuffer(&staging_buffer, &staging_memory, &staging_mapped);

  if (!ok) {
    return false;
  }

  image->width = width;
  image->height = height;

  return true;
}

static bool VK_UI_UploadImageDataSubRect(vk_ui_image_t *image, int x, int y,
                                         int width, int height,
                                         const byte *rgba, VkImageLayout old_layout)
{
    size_t pixel_size;
    if (!VK_UI_TextureByteSize(width, height, 4, &pixel_size)) {
        Com_SetLastError("Vulkan UI: image upload size overflow");
        return false;
    }

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void *staging_mapped = NULL;

    if (!VK_UI_CreateBuffer(pixel_size,
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &staging_buffer,
                            &staging_memory,
                            &staging_mapped)) {
        return false;
    }

    memcpy(staging_mapped, rgba, pixel_size);

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!VK_UI_BeginImmediate(&cmd)) {
        VK_UI_DestroyBuffer(&staging_buffer, &staging_memory, &staging_mapped);
        return false;
    }

    VkImageMemoryBarrier to_transfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image->image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        to_transfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else {
        to_transfer.srcAccessMask = 0;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }

    vkCmdPipelineBarrier(cmd,
                         old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                             ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &to_transfer);

    VkBufferImageCopy copy_region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { x, y, 0 },
        .imageExtent = {
            .width = (uint32_t)width,
            .height = (uint32_t)height,
            .depth = 1,
        },
    };

    vkCmdCopyBufferToImage(cmd, staging_buffer, image->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copy_region);

    VkImageMemoryBarrier to_shader = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image->image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &to_shader);

    bool ok = VK_UI_EndImmediate(cmd);
    VK_UI_DestroyBuffer(&staging_buffer, &staging_memory, &staging_mapped);
    return ok;
}

static bool VK_UI_SetImagePixels(vk_ui_image_t *image, int width, int height, const byte *rgba)
{
    if (!image || !rgba || width <= 0 || height <= 0) {
        return false;
    }

    bool needs_recreate = (image->image == VK_NULL_HANDLE || image->view == VK_NULL_HANDLE ||
                           image->width != width || image->height != height);

    if (needs_recreate) {
        vkDeviceWaitIdle(vk_ui.ctx->device);
        VK_UI_DestroyImageResources(image);

        if (!VK_UI_CreateImageStorage(image, width, height)) {
            return false;
        }

        if (!VK_UI_AllocDescriptorSet(image)) {
            VK_UI_DestroyImageResources(image);
            return false;
        }

        if (!VK_UI_UploadImageData(image, width, height, rgba, VK_IMAGE_LAYOUT_UNDEFINED)) {
            VK_UI_DestroyImageResources(image);
            return false;
        }

        VK_UI_UpdateDescriptorSet(image);
        return true;
    }

    if (!VK_UI_UploadImageData(image, width, height, rgba, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        return false;
    }

    // The image view and sampler bindings did not change. Rewriting a
    // descriptor already bound by another frame is invalid, so retain the
    // existing set for ordinary pixel updates.
    return true;
}

static bool VK_UI_ImageHasTransparency(const byte *rgba, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (rgba[i * 4 + 3] != 255) {
            return true;
        }
    }

    return false;
}

static void VK_UI_Unpack8ToRgba(uint32_t *out, const uint8_t *in, int width, int height)
{
    int x, y, p;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            p = *in;
            if (p == 255) {
                // Transparent index; borrow nearby color to avoid fringes.
                if (y > 0 && *(in - width) != 255) {
                    p = *(in - width);
                } else if (y < height - 1 && *(in + width) != 255) {
                    p = *(in + width);
                } else if (x > 0 && *(in - 1) != 255) {
                    p = *(in - 1);
                } else if (x < width - 1 && *(in + 1) != 255) {
                    p = *(in + 1);
                } else if (y > 0 && x > 0 && *(in - width - 1) != 255) {
                    p = *(in - width - 1);
                } else if (y > 0 && x < width - 1 && *(in - width + 1) != 255) {
                    p = *(in - width + 1);
                } else if (y < height - 1 && x > 0 && *(in + width - 1) != 255) {
                    p = *(in + width - 1);
                } else if (y < height - 1 && x < width - 1 && *(in + width + 1) != 255) {
                    p = *(in + width + 1);
                } else {
                    p = 0;
                }
                *out = d_8to24table[p] & COLOR_U32_RGBA(255, 255, 255, 0);
            } else {
                *out = d_8to24table[p];
            }

            in++;
            out++;
        }
    }
}

static bool VK_UI_LoadPcxRgba(const byte *raw_data, size_t raw_len,
                              int *out_w, int *out_h, byte **out_rgba)
{
    if (!raw_data || raw_len < sizeof(dpcx_t) || !out_w || !out_h || !out_rgba) {
        return false;
    }

    const dpcx_t *pcx = (const dpcx_t *)raw_data;
    if (pcx->manufacturer != 10 || pcx->version != 5 ||
        pcx->encoding != 1 || pcx->bits_per_pixel != 8 || pcx->color_planes != 1) {
        return false;
    }

    int width = (LittleShort(pcx->xmax) - LittleShort(pcx->xmin)) + 1;
    int height = (LittleShort(pcx->ymax) - LittleShort(pcx->ymin)) + 1;
    int scan = LittleShort(pcx->bytes_per_line);

    if (width < 1 || height < 1 || width > 640 || height > 480 || scan < width) {
        return false;
    }

    size_t indexed_bytes;
    if (!VK_UI_TextureByteSize(width, height, sizeof(uint8_t), &indexed_bytes)) {
        return false;
    }

    uint8_t *indexed = malloc(indexed_bytes);
    if (!indexed) {
        return false;
    }

    const byte *raw = pcx->data;
    const byte *end = raw_data + raw_len;

    for (int y = 0; y < height; y++) {
        uint8_t *dst = indexed + (size_t)y * (size_t)width;
        for (int x = 0; x < scan;) {
            if (raw >= end) {
                free(indexed);
                return false;
            }

            int data_byte = *raw++;
            int run_length = 1;

            if ((data_byte & 0xC0) == 0xC0) {
                run_length = data_byte & 0x3F;
                if (x + run_length > scan || raw >= end) {
                    free(indexed);
                    return false;
                }
                data_byte = *raw++;
            }

            while (run_length--) {
                if (x < width) {
                    dst[x] = (uint8_t)data_byte;
                }
                x++;
            }
        }
    }

    size_t rgba_bytes;
    if (!VK_UI_TextureByteSize(width, height, sizeof(uint32_t), &rgba_bytes)) {
        free(indexed);
        return false;
    }

    uint32_t *rgba = malloc(rgba_bytes);
    if (!rgba) {
        free(indexed);
        return false;
    }

    VK_UI_Unpack8ToRgba(rgba, indexed, width, height);
    free(indexed);

    *out_w = width;
    *out_h = height;
    *out_rgba = (byte *)rgba;
    return true;
}

static bool VK_UI_LoadWalRgba(const byte *raw_data, size_t raw_len,
                              int *out_w, int *out_h, byte **out_rgba)
{
    if (!raw_data || raw_len < sizeof(miptex_t) || !out_w || !out_h || !out_rgba) {
        return false;
    }

    const miptex_t *mt = (const miptex_t *)raw_data;
    uint32_t width = LittleLong(mt->width);
    uint32_t height = LittleLong(mt->height);

    if (width < 1 || height < 1 || width > VK_UI_MAX_TEXTURE_SIZE || height > VK_UI_MAX_TEXTURE_SIZE) {
        return false;
    }

    size_t indexed_bytes;
    if (!VK_UI_TextureByteSize((int)width, (int)height, 1, &indexed_bytes)) {
        return false;
    }

    uint32_t size = (uint32_t)indexed_bytes;
    uint32_t offset = LittleLong(mt->offsets[0]);
    if (offset + size < offset || offset + size > raw_len) {
        return false;
    }

    size_t rgba_bytes;
    if (!VK_UI_TextureByteSize((int)width, (int)height, sizeof(uint32_t), &rgba_bytes)) {
        return false;
    }

    uint32_t *rgba = malloc(rgba_bytes);
    if (!rgba) {
        return false;
    }

    VK_UI_Unpack8ToRgba(rgba, (const uint8_t *)mt + offset, (int)width, (int)height);

    *out_w = (int)width;
    *out_h = (int)height;
    *out_rgba = (byte *)rgba;
    return true;
}

static const char *VK_UI_PathExtension(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }

    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *sep = slash;
    if (!sep || (backslash && backslash > sep)) {
        sep = backslash;
    }

    if (!dot || (sep && dot < sep)) {
        return NULL;
    }

    return dot;
}

static bool VK_UI_LoadRgbaFromFile(const char *path, int *out_w, int *out_h, byte **out_rgba)
{
    if (!path || !*path || !out_w || !out_h || !out_rgba) {
        return false;
    }

    byte *file_data = NULL;
    int file_len = FS_LoadFile(path, (void **)&file_data);
    if (file_len < 0 || !file_data) {
        return false;
    }

    int width = 0;
    int height = 0;
    byte *native_rgba = NULL;

    const char *ext = VK_UI_PathExtension(path);
    if (ext && !Q_stricmp(ext, ".pcx")) {
        if (VK_UI_LoadPcxRgba(file_data, (size_t)file_len, &width, &height, &native_rgba)) {
            FS_FreeFile(file_data);
            *out_w = width;
            *out_h = height;
            *out_rgba = native_rgba;
            return true;
        }
    } else if (ext && !Q_stricmp(ext, ".wal")) {
        if (VK_UI_LoadWalRgba(file_data, (size_t)file_len, &width, &height, &native_rgba)) {
            FS_FreeFile(file_data);
            *out_w = width;
            *out_h = height;
            *out_rgba = native_rgba;
            return true;
        }
    } else if (ext && !Q_stricmp(ext, ".dds")) {
        bool has_alpha = false;
        int dds_ret = R_DecodeDDS(file_data, (size_t)file_len,
                                  &width, &height, &native_rgba,
                                  &has_alpha, malloc);
        if (dds_ret >= 0) {
            FS_FreeFile(file_data);
            *out_w = width;
            *out_h = height;
            *out_rgba = native_rgba;
            return true;
        }
    }

    int channels = 0;
    byte *decoded = stbi_load_from_memory(file_data, file_len, &width, &height, &channels, 4);
    FS_FreeFile(file_data);

    if (!decoded || width <= 0 || height <= 0) {
        if (decoded) {
            stbi_image_free(decoded);
        }
        return false;
    }

    size_t rgba_bytes;
    if (!VK_UI_TextureByteSize(width, height, 4, &rgba_bytes)) {
        stbi_image_free(decoded);
        return false;
    }

    byte *rgba_copy = malloc(rgba_bytes);
    if (!rgba_copy) {
        stbi_image_free(decoded);
        return false;
    }

    memcpy(rgba_copy, decoded, rgba_bytes);
    stbi_image_free(decoded);

    *out_w = width;
    *out_h = height;
    *out_rgba = rgba_copy;
    return true;
}

static bool VK_UI_NormalizeImagePath(char *out_name, size_t out_size,
                                     const char *name, imagetype_t type)
{
    if (!out_name || !out_size || !name || !*name) {
        return false;
    }

    size_t len = 0;

    if (type == IT_PIC || type == IT_FONT) {
        if (*name == '/' || *name == '\\') {
            len = FS_NormalizePathBuffer(out_name, name + 1, out_size);
        } else if (type == IT_FONT && (strchr(name, '/') || strchr(name, '\\'))) {
            len = FS_NormalizePathBuffer(out_name, name, out_size);
        } else {
            len = Q_concat(out_name, out_size, "pics/", name);
            if (len < out_size) {
                FS_NormalizePath(out_name);
                len = COM_DefaultExtension(out_name, ".pcx", out_size);
            }
        }
    } else {
        if (*name == '/' || *name == '\\') {
            len = FS_NormalizePathBuffer(out_name, name + 1, out_size);
        } else {
            len = FS_NormalizePathBuffer(out_name, name, out_size);
        }
    }

    if (len >= out_size) {
        return false;
    }

    return true;
}

static bool VK_UI_ReplaceExtension(char *path, size_t path_size, const char *ext)
{
    if (!path || !*path || !ext || !*ext) {
        return false;
    }

    char *dot = strrchr(path, '.');
    char *slash = strrchr(path, '/');
    char *backslash = strrchr(path, '\\');
    char *sep = slash;
    if (!sep || (backslash && backslash > sep)) {
        sep = backslash;
    }

    if (!dot || (sep && dot < sep)) {
        size_t len = strlen(path);
        if (len + strlen(ext) >= path_size) {
            return false;
        }
        Q_strlcat(path, ext, path_size);
        return true;
    }

    size_t base_len = (size_t)(dot - path);
    if (base_len + strlen(ext) >= path_size) {
        return false;
    }

    path[base_len] = '\0';
    Q_strlcat(path, ext, path_size);
    return true;
}

static bool VK_UI_LoadImageData(const char *normalized_name,
                                int *out_w, int *out_h, byte **out_rgba)
{
    const char *ext = VK_UI_PathExtension(normalized_name);
    bool paletted = ext && (!Q_stricmp(ext, ".pcx") || !Q_stricmp(ext, ".wal"));
    char candidate[MAX_QPATH];

    // Match the GL renderer's default r_override_textures behavior: paletted
    // formats prefer 32-bit overrides in "png tga dds" order before the
    // original file, so PNG player skins and texture replacements resolve
    // identically in both renderers.
    if (paletted) {
        static const char *const override_exts[] = { ".png", ".tga", ".dds" };
        for (size_t i = 0; i < q_countof(override_exts); ++i) {
            Q_strlcpy(candidate, normalized_name, sizeof(candidate));
            if (!VK_UI_ReplaceExtension(candidate, sizeof(candidate), override_exts[i])) {
                continue;
            }

            if (VK_UI_LoadRgbaFromFile(candidate, out_w, out_h, out_rgba)) {
                return true;
            }
        }
    }

    if (VK_UI_LoadRgbaFromFile(normalized_name, out_w, out_h, out_rgba)) {
        return true;
    }

    const char *fallback_exts[] = {
        ".wal", ".dds", ".png", ".tga", ".jpg", ".jpeg", ".pcx"
    };

    for (size_t i = 0; i < q_countof(fallback_exts); ++i) {
        Q_strlcpy(candidate, normalized_name, sizeof(candidate));
        if (!VK_UI_ReplaceExtension(candidate, sizeof(candidate), fallback_exts[i])) {
            continue;
        }

        if (VK_UI_LoadRgbaFromFile(candidate, out_w, out_h, out_rgba)) {
            return true;
        }
    }

    return false;
}

// Glow companions have a canonical PCX name. Keep the default truecolour
// replacement preference but do not use the generic image fallback list: a
// same-stem WAL is the base wall material, never emission data.
static bool VK_UI_LoadGlowmapData(const char *canonical_name,
                                  int *out_w, int *out_h, byte **out_rgba)
{
    static const char *const override_exts[] = { ".png", ".tga", ".dds" };
    char candidate[MAX_QPATH];

    for (size_t i = 0; i < q_countof(override_exts); ++i) {
        Q_strlcpy(candidate, canonical_name, sizeof(candidate));
        if (!VK_UI_ReplaceExtension(candidate, sizeof(candidate), override_exts[i])) {
            continue;
        }
        if (VK_UI_LoadRgbaFromFile(candidate, out_w, out_h, out_rgba)) {
            return true;
        }
    }

    return VK_UI_LoadRgbaFromFile(canonical_name, out_w, out_h, out_rgba);
}

static qhandle_t VK_UI_CreateImage(const char *name, imagetype_t type, imageflags_t flags,
                                   int width, int height, const byte *rgba)
{
    qhandle_t handle = 0;
    vk_ui_image_t *image = VK_UI_AllocImageSlot(&handle);
    if (!image) {
        return 0;
    }

    memset(image, 0, sizeof(*image));
    image->in_use = true;
    image->type = type;
    image->flags = flags;
    image->width = width;
    image->height = height;
    image->transparent = VK_UI_ImageHasTransparency(rgba, (size_t)width * (size_t)height);

    if (name) {
        Q_strlcpy(image->name, name, sizeof(image->name));
    }

    if (image->transparent) {
        image->flags |= IF_TRANSPARENT;
    } else {
        image->flags |= IF_OPAQUE;
    }

    byte *picmip_rgba = NULL;
    if (!VK_UI_ApplyPicmip(image, &width, &height, rgba, &picmip_rgba)) {
        VK_UI_DestroyImageResources(image);
        memset(image, 0, sizeof(*image));
        return 0;
    }
    if (picmip_rgba) {
        rgba = picmip_rgba;
        image->transparent = VK_UI_ImageHasTransparency(
            rgba, (size_t)width * (size_t)height);
    }

    if (!VK_UI_SetImagePixels(image, width, height, rgba)) {
        free(picmip_rgba);
        VK_UI_DestroyImageResources(image);
        memset(image, 0, sizeof(*image));
        return 0;
    }

    free(picmip_rgba);

    return handle;
}

// OpenGL discovers glow replacements from the base image name and always
// reads an exact *_glow.pcx file. Keep that data relationship in Vulkan's
// image registry rather than treating it as a second UI draw texture. Walls
// consume glow alpha in the lightmap path; skins consume premultiplied RGB in
// the entity emission path.
static void VK_UI_AssociateGlowmap(qhandle_t base_handle)
{
    vk_ui_image_t *base = VK_UI_ImageForHandle(base_handle);
    if (!base || base->internal_glowmap || base->glow_image ||
        (base->flags & IF_SPECIAL) || !vk_r_glowmaps ||
        !vk_r_glowmaps->integer ||
        (base->type != IT_WALL && base->type != IT_SKIN)) {
        return;
    }

    char glow_name[MAX_QPATH];
    Q_strlcpy(glow_name, base->name, sizeof(glow_name));
    if (!VK_UI_ReplaceExtension(glow_name, sizeof(glow_name), "_glow.pcx")) {
        return;
    }

    int width = 0;
    int height = 0;
    byte *rgba = NULL;
    // Glow companions preserve the default truecolour sibling preference,
    // then use the exact *_glow.pcx file. This is native Vulkan texture
    // registration and never uses a same-stem base WAL as glow data.
    if (!VK_UI_LoadGlowmapData(glow_name, &width, &height, &rgba)) {
        return;
    }

    if (base->type == IT_SKIN) {
        size_t pixel_count;
        if (VK_UI_TexturePixelCount(width, height, &pixel_count)) {
            for (size_t i = 0; i < pixel_count; ++i) {
                byte *pixel = &rgba[i * 4];
                const float alpha = pixel[3] * (1.0f / 255.0f);
                pixel[0] = (byte)(pixel[0] * alpha);
                pixel[1] = (byte)(pixel[1] * alpha);
                pixel[2] = (byte)(pixel[2] * alpha);
            }
        }
    }

    // Creating an image can grow the registry, so don't retain `base` across
    // the allocation. Reuse its material flags/type for matching sampler
    // wrapping, as OpenGL does for a paired skin or wall texture.
    const imagetype_t type = base->type;
    const imageflags_t flags = base->flags;
    qhandle_t glow_handle = VK_UI_CreateImage(glow_name, type, flags,
                                              width, height, rgba);
    free(rgba);
    if (!glow_handle) {
        return;
    }

    vk_ui_image_t *glow = VK_UI_ImageForHandle(glow_handle);
    base = VK_UI_ImageForHandle(base_handle);
    if (!base || !glow) {
        return;
    }

    glow->internal_glowmap = true;
    base->glow_image = glow_handle;
    // A base texture may have been bound by a queued frame while registration
    // discovers its paired glow map. This update changes binding 1, so retire
    // those uses before rewriting the existing descriptor set.
    vkDeviceWaitIdle(vk_ui.ctx->device);
    VK_UI_UpdateDescriptorSet(base);
}

static void VK_UI_EnsureDefaultImages(void)
{
    if (vk_ui.white_image && vk_ui.missing_image) {
        return;
    }

    static const byte white_rgba[4] = { 255, 255, 255, 255 };

    static const byte missing_rgba[4 * 4] = {
        255, 0, 255, 255,   0, 0, 0, 255,
        0, 0, 0, 255,       255, 0, 255, 255,
    };

    if (!vk_ui.white_image) {
        vk_ui.white_image = VK_UI_CreateImage("**white**", IT_PIC, IF_PERMANENT | IF_SPECIAL,
                                              1, 1, white_rgba);
    }

    if (!vk_ui.missing_image) {
        vk_ui.missing_image = VK_UI_CreateImage("**missing**", IT_PIC, IF_PERMANENT | IF_SPECIAL,
                                                2, 2, missing_rgba);
    }
}

static VkRect2D VK_UI_DefaultScissor(void)
{
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = {
            .width = (uint32_t)r_config.width,
            .height = (uint32_t)r_config.height,
        },
    };

    return scissor;
}

static VkRect2D VK_UI_CurrentScissor(void)
{
    if (!vk_ui.clip_enabled) {
        return VK_UI_DefaultScissor();
    }

    int left = max(0, vk_ui.clip_pixels.left);
    int top = max(0, vk_ui.clip_pixels.top);
    int right = min(r_config.width, vk_ui.clip_pixels.right);
    int bottom = min(r_config.height, vk_ui.clip_pixels.bottom);

    if (right <= left || bottom <= top) {
        VkRect2D empty = { .offset = { 0, 0 }, .extent = { 0, 0 } };
        return empty;
    }

    VkRect2D scissor = {
        .offset = { left, top },
        .extent = {
            .width = (uint32_t)(right - left),
            .height = (uint32_t)(bottom - top),
        },
    };

    return scissor;
}

static inline float VK_UI_VirtualWidthScaled(void)
{
    float width = vk_ui.virtual_width * vk_ui.scale;
    if (width <= 0.0f) {
        width = 1.0f;
    }
    return width;
}

static inline float VK_UI_VirtualHeightScaled(void)
{
    float height = vk_ui.virtual_height * vk_ui.scale;
    if (height <= 0.0f) {
        height = 1.0f;
    }
    return height;
}

static inline void VK_UI_ToNdc(float x, float y, float *out_x, float *out_y)
{
    float vw = VK_UI_VirtualWidthScaled();
    float vh = VK_UI_VirtualHeightScaled();

    *out_x = (x / vw) * 2.0f - 1.0f;
    *out_y = 1.0f - (y / vh) * 2.0f;
}

static void VK_UI_ResolvePic(qhandle_t *inout_pic)
{
    if (*inout_pic == 0) {
        *inout_pic = vk_ui.white_image;
    }

    vk_ui_image_t *image = VK_UI_ImageForHandle(*inout_pic);
    if (!image || !image->descriptor_set) {
        *inout_pic = vk_ui.missing_image ? vk_ui.missing_image : vk_ui.white_image;
    }
}

static bool VK_UI_EnqueueQuad(float x, float y, float w, float h,
                              float s1, float t1, float s2, float t2,
                              color_t color, qhandle_t pic)
{
    if (w == 0.0f || h == 0.0f) {
        return true;
    }

    VK_UI_EnsureDefaultImages();
    VK_UI_ResolvePic(&pic);

    vk_ui_image_t *image = VK_UI_ImageForHandle(pic);
    if (!image || !image->descriptor_set) {
        return false;
    }

    uint32_t needed_vertices;
    uint32_t needed_indices;
    uint32_t needed_draws;
    if (!VK_UI_AddCount(vk_ui.vertex_count, 4, &needed_vertices, "vertex") ||
        !VK_UI_AddCount(vk_ui.index_count, 6, &needed_indices, "index") ||
        !VK_UI_AddCount(vk_ui.draw_count, 1, &needed_draws, "draw") ||
        !VK_UI_EnsureDrawCapacity(needed_vertices, needed_indices, needed_draws)) {
        return false;
    }

    uint32_t base_vertex = vk_ui.vertex_count;
    uint32_t base_index = vk_ui.index_count;

    float x0, y0, x1, y1;
    VK_UI_ToNdc(x, y, &x0, &y0);
    VK_UI_ToNdc(x + w, y + h, &x1, &y1);

    vk_ui.vertices[vk_ui.vertex_count + 0] = (vk_ui_vertex_t){ .pos = { x0, y0 }, .uv = { s1, t1 }, .color = color.u32 };
    vk_ui.vertices[vk_ui.vertex_count + 1] = (vk_ui_vertex_t){ .pos = { x1, y0 }, .uv = { s2, t1 }, .color = color.u32 };
    vk_ui.vertices[vk_ui.vertex_count + 2] = (vk_ui_vertex_t){ .pos = { x1, y1 }, .uv = { s2, t2 }, .color = color.u32 };
    vk_ui.vertices[vk_ui.vertex_count + 3] = (vk_ui_vertex_t){ .pos = { x0, y1 }, .uv = { s1, t2 }, .color = color.u32 };

    vk_ui.indices[vk_ui.index_count + 0] = base_vertex + 0;
    vk_ui.indices[vk_ui.index_count + 1] = base_vertex + 2;
    vk_ui.indices[vk_ui.index_count + 2] = base_vertex + 3;
    vk_ui.indices[vk_ui.index_count + 3] = base_vertex + 0;
    vk_ui.indices[vk_ui.index_count + 4] = base_vertex + 1;
    vk_ui.indices[vk_ui.index_count + 5] = base_vertex + 2;

    vk_ui.vertex_count = needed_vertices;
    vk_ui.index_count = needed_indices;

    VkRect2D scissor = VK_UI_CurrentScissor();

    if (vk_ui.draw_count > 0) {
        vk_ui_draw_t *prev = &vk_ui.draws[vk_ui.draw_count - 1];
        if (prev->descriptor_set == image->descriptor_set &&
            prev->scissor.offset.x == scissor.offset.x &&
            prev->scissor.offset.y == scissor.offset.y &&
            prev->scissor.extent.width == scissor.extent.width &&
            prev->scissor.extent.height == scissor.extent.height &&
            prev->first_index + prev->index_count == base_index) {
            prev->index_count += 6;
            return true;
        }
    }

    vk_ui_draw_t *draw = &vk_ui.draws[vk_ui.draw_count++];
    draw->first_index = base_index;
    draw->index_count = 6;
    draw->descriptor_set = image->descriptor_set;
    draw->scissor = scissor;

    return true;
}

bool VK_UI_DrawRmlGeometry(const renderer_rmlui_vertex_t *vertices,
                           size_t vertex_count,
                           const uint32_t *indices,
                           size_t index_count,
                           float translation_x,
                           float translation_y,
                           qhandle_t texture)
{
    if (!vk_ui.initialized || !vertices || !indices || !vertex_count ||
        !index_count || vertex_count > UINT32_MAX || index_count > UINT32_MAX) {
        return false;
    }

    VK_UI_EnsureDefaultImages();
    VK_UI_ResolvePic(&texture);

    vk_ui_image_t *image = VK_UI_ImageForHandle(texture);
    if (!image || !image->descriptor_set) {
        return false;
    }

    const uint32_t vertex_delta = (uint32_t)vertex_count;
    const uint32_t index_delta = (uint32_t)index_count;
    uint32_t needed_vertices;
    uint32_t needed_indices;
    uint32_t needed_draws;
    if (!VK_UI_AddCount(vk_ui.vertex_count, vertex_delta, &needed_vertices,
                        "RmlUi vertex") ||
        !VK_UI_AddCount(vk_ui.index_count, index_delta, &needed_indices,
                        "RmlUi index") ||
        !VK_UI_AddCount(vk_ui.draw_count, 1, &needed_draws, "RmlUi draw") ||
        !VK_UI_EnsureDrawCapacity(needed_vertices, needed_indices,
                                  needed_draws)) {
        return false;
    }

    const uint32_t base_vertex = vk_ui.vertex_count;
    const uint32_t base_index = vk_ui.index_count;
    for (uint32_t i = 0; i < vertex_delta; ++i) {
        float ndc_x;
        float ndc_y;
        VK_UI_ToNdc(vertices[i].position[0] + translation_x,
                    vertices[i].position[1] + translation_y,
                    &ndc_x, &ndc_y);
        vk_ui.vertices[base_vertex + i] = (vk_ui_vertex_t){
            .pos = { ndc_x, ndc_y },
            .uv = { vertices[i].tex_coord[0], vertices[i].tex_coord[1] },
            .color = vertices[i].color,
        };
    }

    for (uint32_t i = 0; i < index_delta; ++i) {
        if (indices[i] >= vertex_delta) {
            return false;
        }
        vk_ui.indices[base_index + i] = base_vertex + indices[i];
    }

    vk_ui.vertex_count = needed_vertices;
    vk_ui.index_count = needed_indices;

    const VkRect2D scissor = VK_UI_CurrentScissor();
    if (vk_ui.draw_count > 0) {
        vk_ui_draw_t *previous = &vk_ui.draws[vk_ui.draw_count - 1];
        if (previous->descriptor_set == image->descriptor_set &&
            previous->scissor.offset.x == scissor.offset.x &&
            previous->scissor.offset.y == scissor.offset.y &&
            previous->scissor.extent.width == scissor.extent.width &&
            previous->scissor.extent.height == scissor.extent.height &&
            previous->first_index + previous->index_count == base_index) {
            previous->index_count += index_delta;
            return true;
        }
    }

    vk_ui_draw_t *draw = &vk_ui.draws[vk_ui.draw_count++];
    draw->first_index = base_index;
    draw->index_count = index_delta;
    draw->descriptor_set = image->descriptor_set;
    draw->scissor = scissor;
    return true;
}

// Emits an 8-vertex vignette ring (solid outer edge fading to a transparent
// inner rect), mirroring the GL renderer's GL_DrawVignette.
static bool VK_UI_EnqueueVignette(float x, float y, float w, float h,
                                  float frac, color_t outer)
{
    static const uint32_t ring_indices[24] = {
        0, 5, 4, 0, 1, 5, 1, 6, 5, 1, 2, 6,
        6, 2, 3, 6, 3, 7, 0, 7, 3, 0, 4, 7
    };

    VK_UI_EnsureDefaultImages();

    vk_ui_image_t *image = VK_UI_ImageForHandle(vk_ui.white_image);
    if (!image || !image->descriptor_set) {
        return false;
    }

    uint32_t needed_vertices;
    uint32_t needed_indices;
    uint32_t needed_draws;
    if (!VK_UI_AddCount(vk_ui.vertex_count, 8, &needed_vertices, "vertex") ||
        !VK_UI_AddCount(vk_ui.index_count, 24, &needed_indices, "index") ||
        !VK_UI_AddCount(vk_ui.draw_count, 1, &needed_draws, "draw") ||
        !VK_UI_EnsureDrawCapacity(needed_vertices, needed_indices, needed_draws)) {
        return false;
    }

    color_t inner = outer;
    inner.a = 0;

    float distance = min(w, h) * frac;
    float corners[8][2] = {
        { x, y },
        { x + w, y },
        { x + w, y + h },
        { x, y + h },
        { x + distance, y + distance },
        { x + w - distance, y + distance },
        { x + w - distance, y + h - distance },
        { x + distance, y + h - distance },
    };

    uint32_t base_vertex = vk_ui.vertex_count;
    uint32_t base_index = vk_ui.index_count;

    for (int i = 0; i < 8; i++) {
        float nx, ny;
        VK_UI_ToNdc(corners[i][0], corners[i][1], &nx, &ny);
        vk_ui.vertices[vk_ui.vertex_count + i] = (vk_ui_vertex_t){
            .pos = { nx, ny },
            .uv = { 0.0f, 0.0f },
            .color = (i < 4) ? outer.u32 : inner.u32,
        };
    }

    for (int i = 0; i < 24; i++) {
        vk_ui.indices[vk_ui.index_count + i] = base_vertex + ring_indices[i];
    }

    vk_ui.vertex_count = needed_vertices;
    vk_ui.index_count = needed_indices;

    vk_ui_draw_t *draw = &vk_ui.draws[vk_ui.draw_count++];
    draw->first_index = base_index;
    draw->index_count = 24;
    draw->descriptor_set = image->descriptor_set;
    draw->scissor = VK_UI_CurrentScissor();

    return true;
}

void VK_UI_DrawScreenBlend(const refdef_t *fd, float vignette_frac)
{
    if (!fd) {
        return;
    }

    // The refdef rect is in real pixel coordinates; convert to the current
    // virtual 2D coordinate space used by the UI queue.
    float sx = VK_UI_VirtualWidthScaled() / (float)max(r_config.width, 1);
    float sy = VK_UI_VirtualHeightScaled() / (float)max(r_config.height, 1);
    float x = (float)fd->x * sx;
    float y = (float)fd->y * sy;
    float w = (float)fd->width * sx;
    float h = (float)fd->height * sy;

    if (fd->screen_blend[3] > 0.0f) {
        color_t color = COLOR_RGBA(
            (uint8_t)(Q_clipf(fd->screen_blend[0], 0.0f, 1.0f) * 255.0f + 0.5f),
            (uint8_t)(Q_clipf(fd->screen_blend[1], 0.0f, 1.0f) * 255.0f + 0.5f),
            (uint8_t)(Q_clipf(fd->screen_blend[2], 0.0f, 1.0f) * 255.0f + 0.5f),
            (uint8_t)(Q_clipf(fd->screen_blend[3], 0.0f, 1.0f) * 255.0f + 0.5f));
        VK_UI_EnqueueQuad(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f,
                          color, vk_ui.white_image);
    }

    if (fd->damage_blend[3] > 0.0f) {
        color_t outer = COLOR_RGBA(
            (uint8_t)(Q_clipf(fd->damage_blend[0], 0.0f, 1.0f) * 255.0f + 0.5f),
            (uint8_t)(Q_clipf(fd->damage_blend[1], 0.0f, 1.0f) * 255.0f + 0.5f),
            (uint8_t)(Q_clipf(fd->damage_blend[2], 0.0f, 1.0f) * 255.0f + 0.5f),
            (uint8_t)(Q_clipf(fd->damage_blend[3], 0.0f, 1.0f) * 255.0f + 0.5f));
        if (vignette_frac > 0.0f) {
            VK_UI_EnqueueVignette(x, y, w, h, vignette_frac, outer);
        } else {
            VK_UI_EnqueueQuad(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f,
                              outer, vk_ui.white_image);
        }
    }
}

static bool VK_UI_EnqueueRotatedQuad(float x, float y, float w, float h,
                                     float s1, float t1, float s2, float t2,
                                     float angle, float pivot_x, float pivot_y,
                                     color_t color, qhandle_t pic)
{
    if (w == 0.0f || h == 0.0f) {
        return true;
    }

    VK_UI_EnsureDefaultImages();
    VK_UI_ResolvePic(&pic);

    vk_ui_image_t *image = VK_UI_ImageForHandle(pic);
    if (!image || !image->descriptor_set) {
        return false;
    }

    uint32_t needed_vertices;
    uint32_t needed_indices;
    uint32_t needed_draws;
    if (!VK_UI_AddCount(vk_ui.vertex_count, 4, &needed_vertices, "vertex") ||
        !VK_UI_AddCount(vk_ui.index_count, 6, &needed_indices, "index") ||
        !VK_UI_AddCount(vk_ui.draw_count, 1, &needed_draws, "draw") ||
        !VK_UI_EnsureDrawCapacity(needed_vertices, needed_indices, needed_draws)) {
        return false;
    }

    float hw = w * 0.5f;
    float hh = h * 0.5f;

    float local_x[4] = {
        -hw + pivot_x,
         hw + pivot_x,
         hw + pivot_x,
        -hw + pivot_x,
    };

    float local_y[4] = {
        -hh + pivot_y,
        -hh + pivot_y,
         hh + pivot_y,
         hh + pivot_y,
    };

    float u[4] = { s1, s2, s2, s1 };
    float v[4] = { t1, t1, t2, t2 };

    float s = sinf(angle);
    float c = cosf(angle);

    uint32_t base_vertex = vk_ui.vertex_count;
    uint32_t base_index = vk_ui.index_count;

    for (int i = 0; i < 4; ++i) {
        float rx = local_x[i] * c - local_y[i] * s;
        float ry = local_x[i] * s + local_y[i] * c;

        float ndc_x, ndc_y;
        VK_UI_ToNdc(rx + x, ry + y, &ndc_x, &ndc_y);

        vk_ui.vertices[vk_ui.vertex_count + i] = (vk_ui_vertex_t){
            .pos = { ndc_x, ndc_y },
            .uv = { u[i], v[i] },
            .color = color.u32,
        };
    }

    vk_ui.indices[vk_ui.index_count + 0] = base_vertex + 0;
    vk_ui.indices[vk_ui.index_count + 1] = base_vertex + 2;
    vk_ui.indices[vk_ui.index_count + 2] = base_vertex + 3;
    vk_ui.indices[vk_ui.index_count + 3] = base_vertex + 0;
    vk_ui.indices[vk_ui.index_count + 4] = base_vertex + 1;
    vk_ui.indices[vk_ui.index_count + 5] = base_vertex + 2;

    vk_ui.vertex_count = needed_vertices;
    vk_ui.index_count = needed_indices;

    VkRect2D scissor = VK_UI_CurrentScissor();

    if (vk_ui.draw_count > 0) {
        vk_ui_draw_t *prev = &vk_ui.draws[vk_ui.draw_count - 1];
        if (prev->descriptor_set == image->descriptor_set &&
            prev->scissor.offset.x == scissor.offset.x &&
            prev->scissor.offset.y == scissor.offset.y &&
            prev->scissor.extent.width == scissor.extent.width &&
            prev->scissor.extent.height == scissor.extent.height &&
            prev->first_index + prev->index_count == base_index) {
            prev->index_count += 6;
            return true;
        }
    }

    vk_ui_draw_t *draw = &vk_ui.draws[vk_ui.draw_count++];
    draw->first_index = base_index;
    draw->index_count = 6;
    draw->descriptor_set = image->descriptor_set;
    draw->scissor = scissor;

    return true;
}

bool VK_UI_Init(vk_context_t *ctx)
{
    memset(&vk_ui, 0, sizeof(vk_ui));

    if (!ctx) {
        Com_SetLastError("Vulkan UI: context is missing");
        return false;
    }

    vk_ui.ctx = ctx;
    vk_ui.initialized = true;
    vk_ui.scale = 1.0f;
    vk_ui.registration_sequence = 1;
    // This is shared renderer material policy, not an OpenGL route. Mark it
    // CVAR_FILES like OpenGL so image registrations refresh after a toggle.
    vk_r_glowmaps = Cvar_Get("r_glowmaps", "1", CVAR_FILES);
    // The shared preference drives native samplers; vk_anisotropy remains a
    // Vulkan-specific compatibility alias for existing configs and scripts.
    VK_UI_RegisterAnisotropyCvars(ctx);
    VK_UI_RegisterTextureFilterCvars();
    // Match the shared OpenGL material upload-quality policy. These values
    // are consumed at native image registration, so CVAR_FILES requests the
    // normal image refresh when a player changes quality.
    r_picmip = Cvar_Get("r_picmip", "0", CVAR_ARCHIVE | CVAR_FILES);
    r_nomip = Cvar_Get("r_nomip", "0", CVAR_ARCHIVE | CVAR_FILES);
    r_picmip_filter = Cvar_Get("r_picmip_filter", "3",
                               CVAR_ARCHIVE | CVAR_FILES);
    vk_gl_downsample_skins = Cvar_Get("gl_downsample_skins", "1",
                                      CVAR_FILES);
    vk_bilerp_chars = Cvar_Get("vk_bilerp_chars", "0", CVAR_ARCHIVE);
    vk_bilerp_chars->changed = VK_UI_BilerpChanged;
    vk_bilerp_pics = Cvar_Get("vk_bilerp_pics", "0", CVAR_ARCHIVE);
    vk_bilerp_pics->changed = VK_UI_BilerpChanged;
    vk_bilerp_skies = Cvar_Get("vk_bilerp_skies", "1", CVAR_ARCHIVE);
    vk_bilerp_skies->changed = VK_UI_BilerpChanged;

    if (!VK_UI_EnsureImageCapacity(VK_UI_INITIAL_IMAGE_CAPACITY)) {
        goto fail;
    }

    if (!VK_UI_EnsureHostBuffers()) {
        goto fail;
    }

    if (!VK_UI_CreateSamplers(ctx, &vk_ui.sampler_repeat,
                              &vk_ui.sampler_clamp,
                              &vk_ui.sampler_nearest_repeat,
                              &vk_ui.sampler_nearest_clamp,
                              &vk_ui.sampler_material_repeat)) {
        goto fail;
    }

    VkDescriptorSetLayoutBinding bindings[3] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = q_countof(bindings),
        .pBindings = bindings,
    };

    if (!VK_UI_Check(vkCreateDescriptorSetLayout(ctx->device, &layout_info, NULL,
                                                 &vk_ui.descriptor_set_layout),
                     "vkCreateDescriptorSetLayout")) {
        goto fail;
    }

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 24576,
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 8192,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };

    if (!VK_UI_Check(vkCreateDescriptorPool(ctx->device, &pool_info, NULL, &vk_ui.descriptor_pool),
                     "vkCreateDescriptorPool")) {
        goto fail;
    }

    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vk_ui.descriptor_set_layout,
    };

    if (!VK_UI_Check(vkCreatePipelineLayout(ctx->device, &pipeline_layout_info, NULL,
                                            &vk_ui.pipeline_layout),
                     "vkCreatePipelineLayout")) {
        goto fail;
    }

    VK_UI_RefreshVirtualMetrics();
    return true;

fail:
    VK_UI_Shutdown(ctx);
    return false;
}

void VK_UI_DestroySwapchainResources(vk_context_t *ctx)
{
    (void)ctx;

    if (!vk_ui.initialized || !vk_ui.ctx || !vk_ui.ctx->device) {
        return;
    }

    if (vk_ui.pipeline) {
        vkDestroyPipeline(vk_ui.ctx->device, vk_ui.pipeline, NULL);
        vk_ui.pipeline = VK_NULL_HANDLE;
    }
    if (vk_ui.showtris_pipeline) {
        vkDestroyPipeline(vk_ui.ctx->device, vk_ui.showtris_pipeline, NULL);
        vk_ui.showtris_pipeline = VK_NULL_HANDLE;
    }

    vk_ui.swapchain_ready = false;
}

bool VK_UI_CreateSwapchainResources(vk_context_t *ctx)
{
    if (!vk_ui.initialized || !ctx || !ctx->presentation_render_pass) {
        return false;
    }

    VK_UI_DestroySwapchainResources(ctx);

    VkShaderModule vert_shader = VK_NULL_HANDLE;
    VkShaderModule frag_shader = VK_NULL_HANDLE;

    VkShaderModuleCreateInfo vert_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_ui2d_vert_spv_size,
        .pCode = vk_ui2d_vert_spv,
    };

    if (!VK_UI_Check(vkCreateShaderModule(ctx->device, &vert_info, NULL, &vert_shader),
                     "vkCreateShaderModule(vert)")) {
        return false;
    }

    VkShaderModuleCreateInfo frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_ui2d_frag_spv_size,
        .pCode = vk_ui2d_frag_spv,
    };

    if (!VK_UI_Check(vkCreateShaderModule(ctx->device, &frag_info, NULL, &frag_shader),
                     "vkCreateShaderModule(frag)")) {
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

    VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = sizeof(vk_ui_vertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attribs[3] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_ui_vertex_t, pos),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_ui_vertex_t, uv),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = offsetof(vk_ui_vertex_t, color),
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_desc,
        .vertexAttributeDescriptionCount = q_countof(attribs),
        .pVertexAttributeDescriptions = attribs,
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
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = VK_TRUE,
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

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    VkDynamicState dynamic_states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = q_countof(dynamic_states),
        .pDynamicStates = dynamic_states,
    };

    VkGraphicsPipelineCreateInfo pipeline_info = {
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
        .layout = vk_ui.pipeline_layout,
        .renderPass = ctx->presentation_render_pass,
        .subpass = 0,
    };

    bool ok = VK_UI_Check(vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                                    &pipeline_info, NULL, &vk_ui.pipeline),
                          "vkCreateGraphicsPipelines");
    if (ok) {
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        ok = VK_UI_Check(vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE,
                                                    1, &pipeline_info, NULL,
                                                    &vk_ui.showtris_pipeline),
                         "vkCreateGraphicsPipelines(show-tris)");
    }

    vkDestroyShaderModule(ctx->device, vert_shader, NULL);
    vkDestroyShaderModule(ctx->device, frag_shader, NULL);

    if (!ok) {
        return false;
    }

    vk_ui.swapchain_ready = true;
    return true;
}

void VK_UI_Shutdown(vk_context_t *ctx)
{
    if (!vk_ui.initialized) {
        return;
    }

    VK_UI_UnregisterAnisotropyCvars();
    VK_UI_UnregisterTextureFilterCvars();

    if (!ctx) {
        ctx = vk_ui.ctx;
    }

    if (ctx && ctx->device) {
        vkDeviceWaitIdle(ctx->device);
    }

    VK_UI_DestroySwapchainResources(ctx);

    for (uint32_t i = 1; i < vk_ui.image_capacity; ++i) {
        if (!vk_ui.images[i].in_use) {
            continue;
        }
        VK_UI_DestroyImageResources(&vk_ui.images[i]);
    }

    if (ctx && ctx->device) {
        if (vk_ui.pipeline_layout) {
            vkDestroyPipelineLayout(ctx->device, vk_ui.pipeline_layout, NULL);
            vk_ui.pipeline_layout = VK_NULL_HANDLE;
        }

        if (vk_ui.descriptor_pool) {
            vkDestroyDescriptorPool(ctx->device, vk_ui.descriptor_pool, NULL);
            vk_ui.descriptor_pool = VK_NULL_HANDLE;
        }

        if (vk_ui.descriptor_set_layout) {
            vkDestroyDescriptorSetLayout(ctx->device, vk_ui.descriptor_set_layout, NULL);
            vk_ui.descriptor_set_layout = VK_NULL_HANDLE;
        }

        VK_UI_DestroySamplers(ctx->device, &vk_ui.sampler_repeat,
                              &vk_ui.sampler_clamp,
                              &vk_ui.sampler_nearest_repeat,
                              &vk_ui.sampler_nearest_clamp,
                              &vk_ui.sampler_material_repeat);

        for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
            vk_ui_frame_buffers_t *frame = &vk_ui.frame_buffers[i];
            VK_UI_DestroyBuffer(&frame->vertex_staging_buffer,
                                &frame->vertex_staging_memory,
                                &frame->vertex_staging_mapped);
            VK_UI_DestroyBuffer(&frame->vertex_buffer, &frame->vertex_memory,
                                NULL);
            frame->vertex_buffer_bytes = 0;
            frame->vertex_upload_bytes = 0;
            VK_UI_DestroyBuffer(&frame->index_staging_buffer,
                                &frame->index_staging_memory,
                                &frame->index_staging_mapped);
            VK_UI_DestroyBuffer(&frame->index_buffer, &frame->index_memory,
                                NULL);
            frame->index_buffer_bytes = 0;
            frame->index_upload_bytes = 0;
            VK_UI_DestroyBuffer(&frame->showtris_vertex_staging_buffer,
                                &frame->showtris_vertex_staging_memory,
                                &frame->showtris_vertex_staging_mapped);
            VK_UI_DestroyBuffer(&frame->showtris_vertex_buffer,
                                &frame->showtris_vertex_memory, NULL);
            frame->showtris_vertex_buffer_bytes = 0;
            frame->showtris_vertex_upload_bytes = 0;
        }
    }

    free(vk_ui.images);
    free(vk_ui.vertices);
    free(vk_ui.showtris_vertices);
    free(vk_ui.indices);
    free(vk_ui.draws);

    memset(&vk_ui, 0, sizeof(vk_ui));
}

void VK_UI_BeginFrame(void)
{
    if (!vk_ui.initialized) {
        return;
    }

    VK_UI_RefreshVirtualMetrics();
    VK_UI_EnsureDefaultImages();

    vk_ui.draw_count = 0;
    vk_ui.vertex_count = 0;
    vk_ui.index_count = 0;
    vk_ui.showtris_vertex_count = 0;
    vk_ui.clip_enabled = false;
    vk_ui.scale = 1.0f;
}

void VK_UI_EndFrame(void)
{
}

void VK_UI_RecordUploads(VkCommandBuffer cmd)
{
    vk_ui_frame_buffers_t *frame = VK_UI_CurrentFrameBuffers();
    if (!frame) {
        return;
    }
    frame->vertex_upload_bytes = 0;
    frame->index_upload_bytes = 0;
    frame->showtris_vertex_upload_bytes = 0;

    if (!cmd || !vk_ui.initialized || !vk_ui.draw_count ||
        !vk_ui.vertex_count || !vk_ui.index_count ||
        !VK_UI_EnsureGpuBuffers(frame)) {
        return;
    }

    bool showtris_ready = VK_UI_BuildShowTris();
    if (!showtris_ready) {
        vk_ui.showtris_vertex_count = 0;
    } else if (vk_ui.showtris_vertex_count) {
        showtris_ready = VK_UI_EnsureShowTrisGpuBuffer(frame);
        if (!showtris_ready) {
            vk_ui.showtris_vertex_count = 0;
        }
    }

    size_t vertex_bytes;
    size_t index_bytes;
    size_t showtris_vertex_bytes = 0;
    if (!VK_UI_ArrayBytes(sizeof(*vk_ui.vertices), vk_ui.vertex_count,
                          &vertex_bytes, "vertex frame upload") ||
        !VK_UI_ArrayBytes(sizeof(*vk_ui.indices), vk_ui.index_count,
                          &index_bytes, "index frame upload") ||
        (showtris_ready && vk_ui.showtris_vertex_count &&
         !VK_UI_ArrayBytes(sizeof(*vk_ui.showtris_vertices),
                           vk_ui.showtris_vertex_count,
                           &showtris_vertex_bytes,
                           "show-tris vertex frame upload")) ||
        !frame->vertex_staging_mapped || !frame->index_staging_mapped) {
        return;
    }
    if (showtris_vertex_bytes &&
        (!frame->showtris_vertex_staging_mapped ||
         !frame->showtris_vertex_buffer)) {
        return;
    }

    memcpy(frame->vertex_staging_mapped, vk_ui.vertices, vertex_bytes);
    memcpy(frame->index_staging_mapped, vk_ui.indices, index_bytes);
    if (showtris_vertex_bytes) {
        memcpy(frame->showtris_vertex_staging_mapped, vk_ui.showtris_vertices,
               showtris_vertex_bytes);
    }
    frame->vertex_upload_bytes = vertex_bytes;
    frame->index_upload_bytes = index_bytes;
    frame->showtris_vertex_upload_bytes = showtris_vertex_bytes;
    VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_UI,
                          vertex_bytes + index_bytes + showtris_vertex_bytes);

    const VkBufferCopy vertex_copy = { .size = vertex_bytes };
    const VkBufferCopy index_copy = { .size = index_bytes };
    vkCmdCopyBuffer(cmd, frame->vertex_staging_buffer, frame->vertex_buffer,
                    1, &vertex_copy);
    vkCmdCopyBuffer(cmd, frame->index_staging_buffer, frame->index_buffer,
                    1, &index_copy);
    if (showtris_vertex_bytes) {
        const VkBufferCopy showtris_copy = { .size = showtris_vertex_bytes };
        vkCmdCopyBuffer(cmd, frame->showtris_vertex_staging_buffer,
                        frame->showtris_vertex_buffer, 1, &showtris_copy);
    }

    VkBufferMemoryBarrier barriers[3] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = frame->vertex_buffer,
            .size = vertex_bytes,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_INDEX_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = frame->index_buffer,
            .size = index_bytes,
        },
    };
    uint32_t barrier_count = 2;
    if (showtris_vertex_bytes) {
        barriers[barrier_count++] = (VkBufferMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = frame->showtris_vertex_buffer,
            .size = showtris_vertex_bytes,
        };
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, NULL,
                         barrier_count, barriers, 0, NULL);
}

void VK_UI_Record(VkCommandBuffer cmd, const VkExtent2D *extent)
{
    vk_ui_frame_buffers_t *frame = VK_UI_CurrentFrameBuffers();
    if (!vk_ui.initialized || !vk_ui.swapchain_ready || !vk_ui.pipeline ||
        !extent || !vk_ui.draw_count || !vk_ui.vertex_count || !vk_ui.index_count ||
        !frame || !frame->vertex_buffer || !frame->index_buffer ||
        !frame->vertex_upload_bytes || !frame->index_upload_bytes) {
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_ui.pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->vertex_buffer, &offset);
    vkCmdBindIndexBuffer(cmd, frame->index_buffer, 0, VK_INDEX_TYPE_UINT32);

    for (uint32_t i = 0; i < vk_ui.draw_count; ++i) {
        const vk_ui_draw_t *draw = &vk_ui.draws[i];
        if (!draw->index_count || !draw->descriptor_set ||
            draw->scissor.extent.width == 0 || draw->scissor.extent.height == 0) {
            continue;
        }

        if (draw->first_index >= vk_ui.index_count ||
            draw->index_count > (vk_ui.index_count - draw->first_index)) {
            Com_WPrintf("Vulkan UI: skipping invalid draw range first=%u count=%u total=%u\n",
                        draw->first_index, draw->index_count, vk_ui.index_count);
            continue;
        }

        vkCmdSetScissor(cmd, 0, 1, &draw->scissor);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vk_ui.pipeline_layout,
                                0, 1, &draw->descriptor_set,
                                0, NULL);
        vkCmdDrawIndexed(cmd, draw->index_count, 1, draw->first_index, 0, 0);
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_UI, 0, draw->index_count);
    }

    if (!VK_Debug_ShowTris(VK_DEBUG_SHOWTRIS_PIC) ||
        !vk_ui.showtris_pipeline || !vk_ui.showtris_vertex_count ||
        !frame->showtris_vertex_buffer ||
        !frame->showtris_vertex_upload_bytes) {
        return;
    }

    const VkDescriptorSet white_set =
        VK_UI_GetDescriptorSetForImage(vk_ui.white_image);
    if (!white_set) {
        return;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_ui.showtris_pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->showtris_vertex_buffer,
                           &offset);
    for (uint32_t i = 0; i < vk_ui.draw_count; ++i) {
        const vk_ui_draw_t *draw = &vk_ui.draws[i];
        if (!draw->showtris_vertex_count ||
            draw->showtris_first_vertex >= vk_ui.showtris_vertex_count ||
            draw->showtris_vertex_count >
                vk_ui.showtris_vertex_count - draw->showtris_first_vertex ||
            draw->scissor.extent.width == 0 || draw->scissor.extent.height == 0) {
            continue;
        }
        vkCmdSetScissor(cmd, 0, 1, &draw->scissor);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vk_ui.pipeline_layout, 0, 1, &white_set,
                                0, NULL);
        vkCmdDraw(cmd, draw->showtris_vertex_count, 1,
                  draw->showtris_first_vertex, 0);
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_UI,
                            draw->showtris_vertex_count, 0);
    }
}

float VK_UI_ClampScale(cvar_t *var)
{
    return R_UIScaleClamp(r_config.width, r_config.height, var);
}

void VK_UI_SetScale(float scale)
{
    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    vk_ui.scale = scale;
}

void VK_UI_SetClipRect(const clipRect_t *clip)
{
    if (!clip) {
        vk_ui.clip_enabled = false;
        memset(&vk_ui.clip_pixels, 0, sizeof(vk_ui.clip_pixels));
        return;
    }

    clipRect_t pixel_clip;
    if (!R_UIScaleClipToPixels(clip, vk_ui.base_scale, vk_ui.scale,
                               r_config.width, r_config.height, &pixel_clip)) {
        vk_ui.clip_enabled = false;
        memset(&vk_ui.clip_pixels, 0, sizeof(vk_ui.clip_pixels));
        return;
    }

    vk_ui.clip_pixels = pixel_clip;
    vk_ui.clip_enabled = true;
}

qhandle_t VK_UI_RegisterImage(const char *name, imagetype_t type, imageflags_t flags)
{
    if (!vk_ui.initialized || !name || !*name) {
        return 0;
    }

    VK_UI_EnsureDefaultImages();

    char normalized[MAX_QPATH];
    if (!VK_UI_NormalizeImagePath(normalized, sizeof(normalized), name, type)) {
        return (flags & IF_OPTIONAL) ? 0 : vk_ui.missing_image;
    }

    vk_ui_image_t *existing = VK_UI_FindImageByName(normalized, type);
    if (existing) {
        existing->flags |= (flags & IF_PERMANENT);
        return (qhandle_t)(existing - vk_ui.images);
    }

    int width = 0;
    int height = 0;
    byte *rgba = NULL;

    if (!VK_UI_LoadImageData(normalized, &width, &height, &rgba)) {
        if (flags & IF_OPTIONAL) {
            return 0;
        }
        return vk_ui.missing_image;
    }

    qhandle_t handle = VK_UI_CreateImage(normalized, type, flags, width, height, rgba);
    free(rgba);

    if (!handle) {
        return vk_ui.missing_image;
    }

    VK_UI_AssociateGlowmap(handle);

    return handle;
}

qhandle_t VK_UI_RegisterRawImage(const char *name, int width, int height, byte *pic,
                                 imagetype_t type, imageflags_t flags)
{
    if (!vk_ui.initialized || width <= 0 || height <= 0 || !pic) {
        return 0;
    }

    VK_UI_EnsureDefaultImages();

    const char *resolved_name = name ? name : "**raw**";
    vk_ui_image_t *existing = VK_UI_FindImageByName(resolved_name, type);
    if (existing) {
        if (!VK_UI_SetImagePixels(existing, width, height, pic)) {
            return 0;
        }

        existing->flags = flags;
        existing->transparent = VK_UI_ImageHasTransparency(pic, (size_t)width * (size_t)height);
        if (existing->transparent) {
            existing->flags |= IF_TRANSPARENT;
        } else {
            existing->flags |= IF_OPAQUE;
        }

        return (qhandle_t)(existing - vk_ui.images);
    }

    return VK_UI_CreateImage(resolved_name, type, flags, width, height, pic);
}

void VK_UI_UnregisterImage(qhandle_t handle)
{
    vk_ui_image_t *image = VK_UI_ImageForHandle(handle);
    if (!image) {
        return;
    }

    // Glow images are private companions. Their base image owns their
    // descriptor reference and lifetime, so an accidental external release
    // cannot leave a material descriptor pointing at a destroyed view.
    if (image->internal_glowmap) {
        return;
    }

    if (handle == vk_ui.white_image || handle == vk_ui.missing_image) {
        return;
    }

    if (handle == vk_ui.raw_image) {
        vk_ui.raw_image = 0;
    }

    const qhandle_t glow_handle = image->glow_image;
    image->glow_image = 0;
    VK_UI_DestroyImageResources(image);
    memset(image, 0, sizeof(*image));

    vk_ui_image_t *glow = VK_UI_ImageForHandle(glow_handle);
    if (glow && glow->internal_glowmap) {
        VK_UI_DestroyImageResources(glow);
        memset(glow, 0, sizeof(*glow));
    }
}

bool VK_UI_GetPicSize(int *w, int *h, qhandle_t pic)
{
    vk_ui_image_t *image = VK_UI_ImageForHandle(pic);
    if (!image) {
        if (w) {
            *w = 0;
        }
        if (h) {
            *h = 0;
        }
        return false;
    }

    if (w) {
        *w = image->width;
    }

    if (h) {
        *h = image->height;
    }

    return image->transparent;
}

bool VK_UI_IsImageTransparent(qhandle_t pic)
{
    vk_ui_image_t *image = VK_UI_ImageForHandle(pic);
    if (!image) {
        return false;
    }

    return image->transparent;
}

imageflags_t VK_UI_GetImageFlags(qhandle_t pic)
{
    vk_ui_image_t *image = VK_UI_ImageForHandle(pic);
    return image ? image->flags : IF_NONE;
}

VkDescriptorSetLayout VK_UI_GetDescriptorSetLayout(void)
{
    if (!vk_ui.initialized) {
        return VK_NULL_HANDLE;
    }

    return vk_ui.descriptor_set_layout;
}

VkDescriptorSet VK_UI_GetDescriptorSetForImage(qhandle_t pic)
{
    if (!vk_ui.initialized) {
        return VK_NULL_HANDLE;
    }

    VK_UI_EnsureDefaultImages();
    VK_UI_ResolvePic(&pic);

    vk_ui_image_t *image = VK_UI_ImageForHandle(pic);
    if (!image) {
        return VK_NULL_HANDLE;
    }

    return image->descriptor_set;
}

VkImageView VK_UI_GetImageView(qhandle_t pic)
{
    if (!vk_ui.initialized) {
        return VK_NULL_HANDLE;
    }

    vk_ui_image_t *image = VK_UI_ImageForHandle(pic);
    return image ? image->view : VK_NULL_HANDLE;
}

VkImage VK_UI_GetImage(qhandle_t pic)
{
    if (!vk_ui.initialized) {
        return VK_NULL_HANDLE;
    }

    vk_ui_image_t *image = VK_UI_ImageForHandle(pic);
    return image ? image->image : VK_NULL_HANDLE;
}

bool VK_UI_HasGlowmap(qhandle_t pic)
{
    if (!vk_ui.initialized) {
        return false;
    }

    vk_ui_image_t *base = VK_UI_ImageForHandle(pic);
    vk_ui_image_t *glow = base ? VK_UI_ImageForHandle(base->glow_image) : NULL;
    return glow && glow->descriptor_set;
}

bool VK_UI_UpdateImageRGBA(qhandle_t handle, int width, int height, const byte *pic)
{
    vk_ui_image_t *image = VK_UI_ImageForHandle(handle);
    if (!image || !pic || width <= 0 || height <= 0) {
        return false;
    }

    size_t pixels;
    if (!VK_UI_TexturePixelCount(width, height, &pixels)) {
        Com_SetLastError("Vulkan UI: image transparency size overflow");
        return false;
    }

    if (!VK_UI_SetImagePixels(image, width, height, pic)) {
        return false;
    }

    image->transparent = VK_UI_ImageHasTransparency(pic, pixels);
    if (image->transparent) {
        image->flags |= IF_TRANSPARENT;
        image->flags &= ~IF_OPAQUE;
    } else {
        image->flags |= IF_OPAQUE;
        image->flags &= ~IF_TRANSPARENT;
    }

    return true;
}

bool VK_UI_UpdateImageRGBASubRect(qhandle_t handle, int x, int y, int width,
                                  int height, const byte *pic) {
  vk_ui_image_t *image = VK_UI_ImageForHandle(handle);
  if (!image || !image->image || !image->view || !pic || width <= 0 ||
      height <= 0) {
    return false;
  }

  // A partial level-zero rewrite cannot preserve the derived material
  // levels. Dynamic callers use single-level pictures/raw images; require a
  // complete update for a registered wall or skin instead of sampling stale
  // mip data after the edit.
  if (image->mip_levels > 1) {
    Com_SetLastError(
        "Vulkan UI: material sub-rect update requires a full image upload");
    return false;
  }

  if (image->width <= 0 || image->height <= 0 || x < 0 || y < 0 ||
      width > image->width || height > image->height ||
      x > image->width - width || y > image->height - height) {
    Com_SetLastError("Vulkan UI: sub-rect update out of bounds");
    return false;
  }

  return VK_UI_UploadImageDataSubRect(image, x, y, width, height, pic,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VK_UI_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic) {
  if (!pic || pic_w <= 0 || pic_h <= 0) {
    return;
  }

  if (vk_ui.raw_image) {
    if (VK_UI_UpdateImageRGBA(vk_ui.raw_image, pic_w, pic_h,
                              (const byte *)pic)) {
      return;
    }

    VK_UI_UnregisterImage(vk_ui.raw_image);
    vk_ui.raw_image = 0;
  }

  vk_ui.raw_image = VK_UI_RegisterRawImage("**rawpic**", pic_w, pic_h,
                                           (byte *)pic, IT_PIC, IF_NONE);
}

void VK_UI_DrawStretchRaw(int x, int y, int w, int h)
{
    if (!vk_ui.raw_image) {
        return;
    }

    VK_UI_DrawStretchPic(x, y, w, h, COLOR_WHITE, vk_ui.raw_image);
}

void VK_UI_DrawPic(int x, int y, color_t color, qhandle_t pic)
{
    int w = 0;
    int h = 0;
    if (!VK_UI_GetPicSize(&w, &h, pic) || w <= 0 || h <= 0) {
        return;
    }

    VK_UI_DrawStretchPic(x, y, w, h, color, pic);
}

void VK_UI_DrawStretchPic(int x, int y, int w, int h, color_t color, qhandle_t pic)
{
    VK_UI_DrawStretchSubPic(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color, pic);
}

void VK_UI_DrawStretchSubPic(int x, int y, int w, int h,
                             float s1, float t1, float s2, float t2,
                             color_t color, qhandle_t pic)
{
    VK_UI_EnqueueQuad((float)x, (float)y, (float)w, (float)h,
                      s1, t1, s2, t2,
                      color, pic);
}

void VK_UI_DrawStretchRotatePic(int x, int y, int w, int h, color_t color, float angle,
                                int pivot_x, int pivot_y, qhandle_t pic)
{
    VK_UI_EnqueueRotatedQuad((float)x, (float)y, (float)w, (float)h,
                             0.0f, 0.0f, 1.0f, 1.0f,
                             angle,
                             (float)pivot_x, (float)pivot_y,
                             color, pic);
}

void VK_UI_DrawKeepAspectPic(int x, int y, int w, int h, color_t color, qhandle_t pic)
{
    vk_ui_image_t *image = VK_UI_ImageForHandle(pic);
    if (!image || image->width <= 0 || image->height <= 0) {
        return;
    }

    float aspect = (float)image->width / (float)image->height;

    float scale_w = (float)w;
    float scale_h = (float)h * aspect;
    float scale = max(scale_w, scale_h);

    float s = (1.0f - scale_w / scale) * 0.5f;
    float t = (1.0f - scale_h / scale) * 0.5f;

    VK_UI_DrawStretchSubPic(x, y, w, h, s, t, 1.0f - s, 1.0f - t, color, pic);
}

void VK_UI_TileClear(int x, int y, int w, int h, qhandle_t pic)
{
    const float div64 = 1.0f / 64.0f;

    VK_UI_DrawStretchSubPic(x, y, w, h,
                            x * div64,
                            y * div64,
                            (x + w) * div64,
                            (y + h) * div64,
                            COLOR_WHITE,
                            pic);
}

void VK_UI_DrawFill32(int x, int y, int w, int h, color_t color)
{
    if (!w || !h) {
        return;
    }

    VK_UI_DrawStretchPic(x, y, w, h, color, vk_ui.white_image);
}

void VK_UI_DrawFill8(int x, int y, int w, int h, int c)
{
    if (!w || !h) {
        return;
    }

    VK_UI_DrawFill32(x, y, w, h, COLOR_U32(d_8to24table[c & 0xff]));
}
