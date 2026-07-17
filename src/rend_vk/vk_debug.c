/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "vk_debug.h"

#include "renderer/view_setup.h"
#include "refresh/debug.h"
#include "vk_debug_spv.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define VK_DEBUG_SHOWTRIS_INITIAL_VERTICES 4096

typedef struct {
    float pos[3];
    uint32_t color;
} vk_debug_vertex_t;

typedef struct {
    uint64_t draws;
    uint64_t vertices;
    uint64_t indices;
    uint64_t upload_bytes;
} vk_debug_domain_stats_t;

typedef struct {
    uint64_t frame_number;
    vk_debug_domain_stats_t domains[VK_DEBUG_DOMAIN_COUNT];
    uint32_t entities;
    uint32_t dlights;
    uint32_t particles;
    uint32_t queries;
    uint32_t active_debug_lines;
    uint32_t debug_capacity_hits;
    uint32_t world_fast_lit_draws;
    uint32_t world_fast_lit_no_fog_draws;
    uint32_t world_texture_replace_draws;
    uint32_t world_texture_replace_no_fog_draws;
    uint32_t entity_fast_lit_draws;
    uint32_t entity_fast_lit_no_fog_draws;
    uint32_t entity_texture_replace_draws;
    uint32_t entity_texture_replace_no_fog_draws;
    uint32_t world_fast_lit_candidates;
    uint32_t world_fast_lit_disabled;
    uint32_t world_fast_lit_fullbright;
    uint32_t world_fast_lit_receiver_lighting;
    uint32_t world_fast_lit_pipeline_unavailable;
    uint32_t world_fast_lit_material_ineligible;
    float cpu_frame_ms;
    float gpu_frame_ms;
    float gpu_upload_ms;
    float gpu_shadow_ms;
    float gpu_opaque_world_ms;
    float gpu_opaque_entity_ms;
    float gpu_scene_ms;
    float gpu_postprocess_ms;
    bool gpu_frame_valid;
} vk_debug_frame_stats_t;

enum {
    VK_DEBUG_MISSING_SCREENSHOT = BIT(0),
    VK_DEBUG_MISSING_STENCIL = BIT(1),
    VK_DEBUG_MISSING_DEPTH_DOF = BIT(2),
    VK_DEBUG_MISSING_DEBUG_PIPELINE = BIT(3),
    VK_DEBUG_MISSING_GPU_TIMING = BIT(4),
};

enum {
    VK_DEBUG_GPU_TIMESTAMP_BEGIN,
    VK_DEBUG_GPU_TIMESTAMP_AFTER_UPLOAD,
    VK_DEBUG_GPU_TIMESTAMP_AFTER_SHADOW,
    VK_DEBUG_GPU_TIMESTAMP_AFTER_OPAQUE_WORLD,
    VK_DEBUG_GPU_TIMESTAMP_AFTER_OPAQUE_ENTITY,
    VK_DEBUG_GPU_TIMESTAMP_AFTER_SCENE,
    VK_DEBUG_GPU_TIMESTAMP_AFTER_POSTPROCESS,
    VK_DEBUG_GPU_TIMESTAMP_END,
    VK_DEBUG_GPU_TIMESTAMP_COUNT,
};

typedef struct {
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    vk_debug_vertex_t *vertex_mapped;
    VkBuffer showtris_vertex_buffer;
    VkDeviceMemory showtris_vertex_memory;
    vk_debug_vertex_t *showtris_vertex_mapped;
    uint32_t showtris_vertex_capacity;
} vk_debug_frame_buffer_t;

typedef struct {
    vk_context_t *ctx;
    bool initialized;
    bool swapchain_ready;
    bool stats_registered;

    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline_depth;
    VkPipeline pipeline_no_depth;
    vk_debug_frame_buffer_t frame_buffers[VK_MAX_FRAMES_IN_FLIGHT];
    VkQueryPool timestamp_query_pool;
    uint32_t timestamp_query_count;
    uint32_t timestamp_query_base[VK_MAX_FRAMES_IN_FLIGHT];
    float timestamp_period_ns;
    float last_gpu_frame_ms;
    float last_gpu_upload_ms;
    float last_gpu_shadow_ms;
    float last_gpu_opaque_world_ms;
    float last_gpu_opaque_entity_ms;
    float last_gpu_scene_ms;
    float last_gpu_postprocess_ms;
    bool gpu_frame_valid;
    uint64_t gpu_sample_id;
    bool timestamp_recorded[VK_MAX_FRAMES_IN_FLIGHT];
    bool timestamp_pending[VK_MAX_FRAMES_IN_FLIGHT];

    refdef_t fd;
    bool have_refdef;
    cvar_t *draw;
    cvar_t *showtris;
    cvar_t *stats_log;

    vk_debug_vertex_t *showtris_vertices;
    uint32_t showtris_vertex_count;
    uint32_t showtris_vertex_capacity;
    vk_debug_vertex_t *showtris_no_depth_vertices;
    uint32_t showtris_no_depth_vertex_count;
    uint32_t showtris_no_depth_vertex_capacity;

    uint64_t next_frame_number;
    vk_debug_frame_stats_t current;
    vk_debug_frame_stats_t last;
    uint32_t missing_mask;
} vk_debug_state_t;

static vk_debug_state_t vk_debug;

static const char *const vk_debug_domain_names[VK_DEBUG_DOMAIN_COUNT] = {
    "world", "entity", "ui", "postprocess", "shadow", "debug"
};

static bool VK_Debug_Check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) {
        return true;
    }
    Com_SetLastError(va("Vulkan debug %s failed: %d", what, (int)result));
    return false;
}

static bool VK_Debug_CreateTimestampQueries(vk_context_t *ctx)
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(ctx->physical_device, &properties);
    if (!properties.limits.timestampComputeAndGraphics ||
        properties.limits.timestampPeriod <= 0.0f ||
        !ctx->frame_count) {
        vk_debug.missing_mask |= VK_DEBUG_MISSING_GPU_TIMING;
        return true;
    }

    if (ctx->frame_count >
        UINT32_MAX / VK_DEBUG_GPU_TIMESTAMP_COUNT) {
        vk_debug.missing_mask |= VK_DEBUG_MISSING_GPU_TIMING;
        return true;
    }

    VkQueryPoolCreateInfo query_info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = ctx->frame_count * VK_DEBUG_GPU_TIMESTAMP_COUNT,
    };
    VkResult result = vkCreateQueryPool(ctx->device, &query_info, NULL,
                                        &vk_debug.timestamp_query_pool);
    if (result != VK_SUCCESS) {
        Com_WPrintf("Vulkan debug: GPU timestamp queries unavailable: %d\n",
                    (int)result);
        vk_debug.missing_mask |= VK_DEBUG_MISSING_GPU_TIMING;
        return true;
    }

    vk_debug.timestamp_query_count = query_info.queryCount;
    vk_debug.timestamp_period_ns = properties.limits.timestampPeriod;
    vk_debug.missing_mask &= ~VK_DEBUG_MISSING_GPU_TIMING;
    return true;
}

static uint32_t VK_Debug_FindMemoryType(uint32_t type_filter,
                                        VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(vk_debug.ctx->physical_device,
                                        &memory_properties);
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((type_filter & BIT(i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

static uint32_t VK_Debug_CurrentFrame(void)
{
    if (!vk_debug.ctx || !vk_debug.ctx->frame_count ||
        vk_debug.ctx->current_frame >= vk_debug.ctx->frame_count) {
        return 0;
    }
    return vk_debug.ctx->current_frame;
}

static bool VK_Debug_CreateVertexBuffer(vk_debug_frame_buffer_t *frame)
{
    if (!frame) {
        return false;
    }
    const VkDeviceSize size = sizeof(vk_debug_vertex_t) * MAX_DEBUG_VERTICES;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VK_Debug_Check(vkCreateBuffer(vk_debug.ctx->device, &buffer_info,
                                       NULL, &frame->vertex_buffer),
                        "vkCreateBuffer")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vk_debug.ctx->device, frame->vertex_buffer,
                                  &requirements);
    uint32_t memory_type = VK_Debug_FindMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("Vulkan debug: host-visible coherent memory is unavailable");
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!VK_Debug_Check(vkAllocateMemory(vk_debug.ctx->device, &alloc_info,
                                         NULL, &frame->vertex_memory),
                        "vkAllocateMemory")) {
        return false;
    }
    if (!VK_Debug_Check(vkBindBufferMemory(vk_debug.ctx->device,
                                           frame->vertex_buffer,
                                           frame->vertex_memory, 0),
                        "vkBindBufferMemory")) {
        return false;
    }
    if (!VK_Debug_Check(vkMapMemory(vk_debug.ctx->device,
                                    frame->vertex_memory, 0, size, 0,
                                    (void **)&frame->vertex_mapped),
                        "vkMapMemory")) {
        return false;
    }
    return true;
}

static void VK_Debug_DestroyShowTrisVertexBuffer(vk_debug_frame_buffer_t *frame)
{
    if (!frame || !vk_debug.ctx || !vk_debug.ctx->device) {
        return;
    }
    if (frame->showtris_vertex_mapped && frame->showtris_vertex_memory) {
        vkUnmapMemory(vk_debug.ctx->device, frame->showtris_vertex_memory);
    }
    if (frame->showtris_vertex_buffer) {
        vkDestroyBuffer(vk_debug.ctx->device, frame->showtris_vertex_buffer,
                        NULL);
    }
    if (frame->showtris_vertex_memory) {
        vkFreeMemory(vk_debug.ctx->device, frame->showtris_vertex_memory,
                     NULL);
    }
    frame->showtris_vertex_buffer = VK_NULL_HANDLE;
    frame->showtris_vertex_memory = VK_NULL_HANDLE;
    frame->showtris_vertex_mapped = NULL;
    frame->showtris_vertex_capacity = 0;
}

static bool VK_Debug_CreateShowTrisVertexBuffer(
    vk_debug_frame_buffer_t *frame, uint32_t capacity)
{
    if (!frame || !capacity) {
        Com_SetLastError("Vulkan debug: show-tris vertex capacity is invalid");
        return false;
    }

    VK_Debug_DestroyShowTrisVertexBuffer(frame);

    const VkDeviceSize size =
        (VkDeviceSize)capacity * sizeof(vk_debug_vertex_t);
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VK_Debug_Check(vkCreateBuffer(vk_debug.ctx->device, &buffer_info,
                                       NULL, &frame->showtris_vertex_buffer),
                        "vkCreateBuffer(show-tris)")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vk_debug.ctx->device,
                                  frame->showtris_vertex_buffer,
                                  &requirements);
    uint32_t memory_type = VK_Debug_FindMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("Vulkan debug: host-visible show-tris memory is unavailable");
        VK_Debug_DestroyShowTrisVertexBuffer(frame);
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!VK_Debug_Check(vkAllocateMemory(vk_debug.ctx->device, &alloc_info,
                                         NULL,
                                         &frame->showtris_vertex_memory),
                        "vkAllocateMemory(show-tris)")) {
        VK_Debug_DestroyShowTrisVertexBuffer(frame);
        return false;
    }
    if (!VK_Debug_Check(vkBindBufferMemory(vk_debug.ctx->device,
                                           frame->showtris_vertex_buffer,
                                           frame->showtris_vertex_memory, 0),
                        "vkBindBufferMemory(show-tris)")) {
        VK_Debug_DestroyShowTrisVertexBuffer(frame);
        return false;
    }
    if (!VK_Debug_Check(vkMapMemory(vk_debug.ctx->device,
                                    frame->showtris_vertex_memory, 0, size,
                                    0,
                                    (void **)&frame->showtris_vertex_mapped),
                        "vkMapMemory(show-tris)")) {
        VK_Debug_DestroyShowTrisVertexBuffer(frame);
        return false;
    }

    frame->showtris_vertex_capacity = capacity;
    return true;
}

static bool VK_Debug_EnsureShowTrisVertexBuffer(
    vk_debug_frame_buffer_t *frame, uint32_t needed)
{
    if (!frame || !needed) {
        return false;
    }
    if (frame->showtris_vertex_mapped &&
        frame->showtris_vertex_capacity >= needed) {
        return true;
    }

    uint32_t capacity = frame->showtris_vertex_capacity
        ? frame->showtris_vertex_capacity : VK_DEBUG_SHOWTRIS_INITIAL_VERTICES;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    return VK_Debug_CreateShowTrisVertexBuffer(frame, capacity);
}

static bool VK_Debug_EnsureShowTrisQueueCapacity(bool depth_test,
                                                 uint32_t needed)
{
    vk_debug_vertex_t **vertices = depth_test
        ? &vk_debug.showtris_vertices : &vk_debug.showtris_no_depth_vertices;
    uint32_t *capacity = depth_test ? &vk_debug.showtris_vertex_capacity
                                    : &vk_debug.showtris_no_depth_vertex_capacity;
    if (needed <= *capacity) {
        return true;
    }

    uint32_t new_capacity = *capacity
        ? *capacity : VK_DEBUG_SHOWTRIS_INITIAL_VERTICES;
    while (new_capacity < needed) {
        if (new_capacity > UINT32_MAX / 2) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }
    vk_debug_vertex_t *new_vertices = realloc(
        *vertices, (size_t)new_capacity * sizeof(**vertices));
    if (!new_vertices) {
        Com_WPrintf("Vulkan debug: unable to grow show-tris CPU queue\n");
        return false;
    }
    *vertices = new_vertices;
    *capacity = new_capacity;
    return true;
}

static bool VK_Debug_CreatePipeline(bool depth_test, VkPipeline *out_pipeline)
{
    vk_context_t *ctx = vk_debug.ctx;
    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_debug_vert_spv_size,
        .pCode = vk_debug_vert_spv,
    };
    if (!VK_Debug_Check(vkCreateShaderModule(ctx->device, &shader_info, NULL,
                                             &vertex_shader),
                        "vkCreateShaderModule(vertex)")) {
        return false;
    }
    shader_info.codeSize = vk_debug_frag_spv_size;
    shader_info.pCode = vk_debug_frag_spv;
    if (!VK_Debug_Check(vkCreateShaderModule(ctx->device, &shader_info, NULL,
                                             &fragment_shader),
                        "vkCreateShaderModule(fragment)")) {
        vkDestroyShaderModule(ctx->device, vertex_shader, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader,
            .pName = "main",
        },
    };
    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(vk_debug_vertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attributes[2] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_debug_vertex_t, pos),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = offsetof(vk_debug_vertex_t, color),
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = q_countof(attributes),
        .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
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
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = vk_debug.pipeline_layout,
        .renderPass = ctx->scene_render_pass,
        .subpass = 0,
    };
    bool ok = VK_Debug_Check(
        vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                  &pipeline_info, NULL, out_pipeline),
        depth_test ? "vkCreateGraphicsPipelines(depth)" :
                     "vkCreateGraphicsPipelines(no depth)");
    vkDestroyShaderModule(ctx->device, fragment_shader, NULL);
    vkDestroyShaderModule(ctx->device, vertex_shader, NULL);
    return ok;
}

static uint32_t VK_Debug_CountLines(void)
{
    uint32_t count = 0;
    r_debug_line_t *line;
    LIST_FOR_EACH(r_debug_line_t, line, &r_debug_lines_active, entry) {
        count++;
    }
    return count;
}

static void VK_Debug_DrawStats(void)
{
    const vk_debug_frame_stats_t *stats = &vk_debug.last;
    SCR_StatKeyValue("Backend", "native Vulkan");
    SCR_StatKeyValue("Frame", va("%llu",
        (unsigned long long)stats->frame_number));
    for (int i = 0; i < VK_DEBUG_DOMAIN_COUNT; i++) {
        const vk_debug_domain_stats_t *domain = &stats->domains[i];
        SCR_StatKeyValue(vk_debug_domain_names[i],
            va("draw:%llu vert:%llu idx:%llu up:%llu",
               (unsigned long long)domain->draws,
               (unsigned long long)domain->vertices,
               (unsigned long long)domain->indices,
               (unsigned long long)domain->upload_bytes));
    }
    SCR_StatKeyValue("Scene", va("ent:%u dl:%u part:%u query:%u",
        stats->entities, stats->dlights, stats->particles, stats->queries));
    SCR_StatKeyValue("Debug lines", va("%u/%u capacity:%u",
        stats->active_debug_lines, MAX_DEBUG_LINES,
        stats->debug_capacity_hits));
    SCR_StatKeyValue("CPU frame ms", va("%.3f", stats->cpu_frame_ms));
    SCR_StatKeyValue("GPU frame ms", stats->gpu_frame_valid
        ? va("%.3f", stats->gpu_frame_ms) : "unavailable");
    SCR_StatKeyValue("GPU phases ms", stats->gpu_frame_valid
        ? va("upload:%.3f shadow:%.3f world:%.3f entity:%.3f scene:%.3f post:%.3f",
             stats->gpu_upload_ms, stats->gpu_shadow_ms,
             stats->gpu_opaque_world_ms, stats->gpu_opaque_entity_ms,
             stats->gpu_scene_ms, stats->gpu_postprocess_ms)
        : "unavailable");
    SCR_StatKeyValue("Missing mask", va("0x%02x", vk_debug.missing_mask));
}

static void VK_Debug_Stats_f(void)
{
    const vk_debug_frame_stats_t *stats = &vk_debug.last;
    uint64_t draws = 0;
    uint64_t vertices = 0;
    uint64_t indices = 0;
    uint64_t uploads = 0;
    for (int i = 0; i < VK_DEBUG_DOMAIN_COUNT; i++) {
        draws += stats->domains[i].draws;
        vertices += stats->domains[i].vertices;
        indices += stats->domains[i].indices;
        uploads += stats->domains[i].upload_bytes;
    }
    Com_Printf("VK_STATS frame=%llu draws=%llu vertices=%llu indices=%llu uploads=%llu world_draws=%llu entity_draws=%llu ui_draws=%llu post_draws=%llu shadow_draws=%llu debug_draws=%llu world_fast_lit_draws=%u world_fast_lit_no_fog_draws=%u world_texture_replace_draws=%u world_texture_replace_no_fog_draws=%u entity_fast_lit_draws=%u entity_fast_lit_no_fog_draws=%u entity_texture_replace_draws=%u entity_texture_replace_no_fog_draws=%u world_fast_lit_candidates=%u world_fast_lit_disabled=%u world_fast_lit_fullbright=%u world_fast_lit_receiver_lighting=%u world_fast_lit_pipeline_unavailable=%u world_fast_lit_material_ineligible=%u world_uploads=%llu entity_uploads=%llu ui_uploads=%llu post_uploads=%llu shadow_uploads=%llu debug_uploads=%llu entities=%u dlights=%u particles=%u queries=%u debug_lines=%u capacity_hits=%u cpu_ms=%.3f gpu_ms=%.3f gpu_frame_ms=%.3f gpu_upload_ms=%.3f gpu_shadow_ms=%.3f gpu_opaque_world_ms=%.3f gpu_opaque_entity_ms=%.3f gpu_scene_ms=%.3f gpu_post_ms=%.3f gpu_frame_valid=%d gpu_valid=%d missing_mask=0x%02x\n",
               (unsigned long long)stats->frame_number,
               (unsigned long long)draws,
               (unsigned long long)vertices,
               (unsigned long long)indices,
               (unsigned long long)uploads,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_WORLD].draws,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_ENTITY].draws,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_UI].draws,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_POSTPROCESS].draws,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_SHADOW].draws,
               (unsigned long long)
               stats->domains[VK_DEBUG_DOMAIN_DEBUG].draws,
               stats->world_fast_lit_draws,
               stats->world_fast_lit_no_fog_draws,
               stats->world_texture_replace_draws,
               stats->world_texture_replace_no_fog_draws,
               stats->entity_fast_lit_draws,
               stats->entity_fast_lit_no_fog_draws,
               stats->entity_texture_replace_draws,
               stats->entity_texture_replace_no_fog_draws,
               stats->world_fast_lit_candidates,
               stats->world_fast_lit_disabled,
               stats->world_fast_lit_fullbright,
               stats->world_fast_lit_receiver_lighting,
               stats->world_fast_lit_pipeline_unavailable,
               stats->world_fast_lit_material_ineligible,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_WORLD].upload_bytes,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_ENTITY].upload_bytes,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_UI].upload_bytes,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_POSTPROCESS].upload_bytes,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_SHADOW].upload_bytes,
               (unsigned long long)
                   stats->domains[VK_DEBUG_DOMAIN_DEBUG].upload_bytes,
               stats->entities, stats->dlights, stats->particles,
               stats->queries, stats->active_debug_lines,
               stats->debug_capacity_hits, stats->cpu_frame_ms,
               stats->gpu_frame_ms, stats->gpu_frame_ms,
               stats->gpu_upload_ms, stats->gpu_shadow_ms,
               stats->gpu_opaque_world_ms, stats->gpu_opaque_entity_ms,
               stats->gpu_scene_ms, stats->gpu_postprocess_ms,
               stats->gpu_frame_valid,
               stats->gpu_frame_valid,
               vk_debug.missing_mask);
    Com_Printf("VK_CAPS debug_lines=%d screenshot=%d stencil=%d depth_dof=%d gpu_timing=%d\n",
               (vk_debug.missing_mask & VK_DEBUG_MISSING_DEBUG_PIPELINE) == 0,
               (vk_debug.missing_mask & VK_DEBUG_MISSING_SCREENSHOT) == 0,
               (vk_debug.missing_mask & VK_DEBUG_MISSING_STENCIL) == 0,
               (vk_debug.missing_mask & VK_DEBUG_MISSING_DEPTH_DOF) == 0,
               (vk_debug.missing_mask & VK_DEBUG_MISSING_GPU_TIMING) == 0);
}

static void VK_Debug_Test_f(void)
{
    if (!vk_debug.have_refdef) {
        Com_Printf("VK_DEBUG_TEST status=no_refdef\n");
        return;
    }

    vec3_t forward, right, up;
    AngleVectors(vk_debug.fd.viewangles, forward, right, up);
    vec3_t center;
    VectorMA(vk_debug.fd.vieworg, 192.0f, forward, center);

    const uint32_t lifetime = 5000;
    vec3_t point;
    VectorMA(center, -48.0f, right, point);
    R_AddDebugPoint(point, 24.0f, COLOR_YELLOW, lifetime, true);

    VectorMA(center, 48.0f, right, point);
    R_AddDebugAxis(point, NULL, 24.0f, lifetime, true);

    vec3_t mins, maxs;
    VectorSet(mins, center[0] - 24.0f, center[1] - 24.0f, center[2] - 24.0f);
    VectorSet(maxs, center[0] + 24.0f, center[1] + 24.0f, center[2] + 24.0f);
    R_AddDebugBounds(mins, maxs, COLOR_CYAN, lifetime, true);

    VectorMA(center, 52.0f, up, point);
    R_AddDebugSphere(point, 18.0f, COLOR_MAGENTA, lifetime, false);
    VectorMA(center, -52.0f, up, point);
    R_AddDebugCircle(point, 22.0f, COLOR_GREEN, lifetime, false);

    vec3_t arrow_end;
    VectorMA(center, 68.0f, right, arrow_end);
    R_AddDebugArrow(center, arrow_end, 14.0f, COLOR_WHITE, COLOR_RED,
                    lifetime, false);

    VectorMA(center, 80.0f, up, point);
    R_AddDebugText(point, NULL, "VULKAN DEBUG", 0.35f, COLOR_WHITE,
                   lifetime, false);

    Com_Printf("VK_DEBUG_TEST status=queued active_lines=%u max_lines=%u\n",
               VK_Debug_CountLines(), MAX_DEBUG_LINES);
}

bool VK_Debug_Init(vk_context_t *ctx)
{
    memset(&vk_debug, 0, sizeof(vk_debug));
    if (!ctx || !ctx->device) {
        return false;
    }
    vk_debug.ctx = ctx;
    vk_debug.missing_mask = VK_DEBUG_MISSING_SCREENSHOT |
                            VK_DEBUG_MISSING_STENCIL |
                            VK_DEBUG_MISSING_DEPTH_DOF |
                            VK_DEBUG_MISSING_DEBUG_PIPELINE |
                            VK_DEBUG_MISSING_GPU_TIMING;

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(renderer_view_push_t),
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    if (!VK_Debug_Check(vkCreatePipelineLayout(ctx->device, &layout_info, NULL,
                                               &vk_debug.pipeline_layout),
                        "vkCreatePipelineLayout")) {
        VK_Debug_Shutdown(ctx);
        return false;
    }
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (!VK_Debug_CreateVertexBuffer(&vk_debug.frame_buffers[i])) {
            VK_Debug_Shutdown(ctx);
            return false;
        }
    }

    R_ClearDebugLines();
    R_InitDebugText();
    vk_debug.draw = Cvar_Get("vk_debug_draw", "1", 0);
    vk_debug.showtris = Cvar_Get("vk_showtris", "0", CVAR_CHEAT);
    vk_debug.stats_log = Cvar_Get("vk_stats_log", "0", CVAR_NOARCHIVE);
    Cmd_AddCommand("cleardebuglines", R_ClearDebugLines);
    Cmd_AddCommand("vk_stats", VK_Debug_Stats_f);
    Cmd_AddCommand("vk_debug_test", VK_Debug_Test_f);
    SCR_RegisterStat("renderer", VK_Debug_DrawStats);
    vk_debug.stats_registered = true;
    vk_debug.initialized = true;
    return true;
}

void VK_Debug_Shutdown(vk_context_t *ctx)
{
    if (!ctx || !ctx->device) {
        memset(&vk_debug, 0, sizeof(vk_debug));
        return;
    }
    VK_Debug_DestroySwapchainResources(ctx);
    if (vk_debug.stats_registered) {
        SCR_UnregisterStat("renderer");
    }
    if (vk_debug.initialized) {
        Cmd_RemoveCommand("cleardebuglines");
        Cmd_RemoveCommand("vk_stats");
        Cmd_RemoveCommand("vk_debug_test");
    }
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_debug_frame_buffer_t *frame = &vk_debug.frame_buffers[i];
        VK_Debug_DestroyShowTrisVertexBuffer(frame);
        if (frame->vertex_mapped && frame->vertex_memory) {
            vkUnmapMemory(ctx->device, frame->vertex_memory);
        }
        if (frame->vertex_buffer) {
            vkDestroyBuffer(ctx->device, frame->vertex_buffer, NULL);
        }
        if (frame->vertex_memory) {
            vkFreeMemory(ctx->device, frame->vertex_memory, NULL);
        }
    }
    free(vk_debug.showtris_vertices);
    free(vk_debug.showtris_no_depth_vertices);
    if (vk_debug.pipeline_layout) {
        vkDestroyPipelineLayout(ctx->device, vk_debug.pipeline_layout, NULL);
    }
    memset(&vk_debug, 0, sizeof(vk_debug));
}

bool VK_Debug_CreateSwapchainResources(vk_context_t *ctx)
{
    if (!vk_debug.initialized || !ctx || ctx != vk_debug.ctx ||
        !ctx->scene_render_pass) {
        Com_WPrintf("Vulkan debug: swapchain resources requested before debug state or render pass was ready\n");
        return false;
    }
    VK_Debug_DestroySwapchainResources(ctx);
    if (!VK_Debug_CreatePipeline(true, &vk_debug.pipeline_depth) ||
        !VK_Debug_CreatePipeline(false, &vk_debug.pipeline_no_depth) ||
        !VK_Debug_CreateTimestampQueries(ctx)) {
        Com_WPrintf("Vulkan debug: pipeline creation failed: %s\n", Com_GetLastError());
        VK_Debug_DestroySwapchainResources(ctx);
        return false;
    }
    vk_debug.swapchain_ready = true;
    vk_debug.missing_mask &= ~VK_DEBUG_MISSING_DEBUG_PIPELINE;
    return true;
}

void VK_Debug_DestroySwapchainResources(vk_context_t *ctx)
{
    if (!ctx || !ctx->device) {
        return;
    }
    if (vk_debug.pipeline_depth) {
        vkDestroyPipeline(ctx->device, vk_debug.pipeline_depth, NULL);
        vk_debug.pipeline_depth = VK_NULL_HANDLE;
    }
    if (vk_debug.pipeline_no_depth) {
        vkDestroyPipeline(ctx->device, vk_debug.pipeline_no_depth, NULL);
        vk_debug.pipeline_no_depth = VK_NULL_HANDLE;
    }
    if (vk_debug.timestamp_query_pool) {
        vkDestroyQueryPool(ctx->device, vk_debug.timestamp_query_pool, NULL);
        vk_debug.timestamp_query_pool = VK_NULL_HANDLE;
    }
    vk_debug.timestamp_query_count = 0;
    memset(vk_debug.timestamp_query_base, 0,
           sizeof(vk_debug.timestamp_query_base));
    vk_debug.timestamp_period_ns = 0.0f;
    vk_debug.last_gpu_frame_ms = 0.0f;
    vk_debug.last_gpu_upload_ms = 0.0f;
    vk_debug.last_gpu_shadow_ms = 0.0f;
    vk_debug.last_gpu_opaque_world_ms = 0.0f;
    vk_debug.last_gpu_opaque_entity_ms = 0.0f;
    vk_debug.last_gpu_scene_ms = 0.0f;
    vk_debug.last_gpu_postprocess_ms = 0.0f;
    memset(vk_debug.timestamp_recorded, 0,
           sizeof(vk_debug.timestamp_recorded));
    memset(vk_debug.timestamp_pending, 0,
           sizeof(vk_debug.timestamp_pending));
    vk_debug.gpu_frame_valid = false;
    vk_debug.missing_mask |= VK_DEBUG_MISSING_GPU_TIMING;
    vk_debug.swapchain_ready = false;
    vk_debug.missing_mask |= VK_DEBUG_MISSING_DEBUG_PIPELINE;
}

void VK_Debug_BeginFrame(void)
{
    vk_debug.showtris_vertex_count = 0;
    vk_debug.showtris_no_depth_vertex_count = 0;
    memset(&vk_debug.current, 0, sizeof(vk_debug.current));
    vk_debug.current.frame_number = ++vk_debug.next_frame_number;
    vk_debug.current.gpu_frame_ms = vk_debug.last_gpu_frame_ms;
    vk_debug.current.gpu_upload_ms = vk_debug.last_gpu_upload_ms;
    vk_debug.current.gpu_shadow_ms = vk_debug.last_gpu_shadow_ms;
    vk_debug.current.gpu_opaque_world_ms =
        vk_debug.last_gpu_opaque_world_ms;
    vk_debug.current.gpu_opaque_entity_ms =
        vk_debug.last_gpu_opaque_entity_ms;
    vk_debug.current.gpu_scene_ms = vk_debug.last_gpu_scene_ms;
    vk_debug.current.gpu_postprocess_ms = vk_debug.last_gpu_postprocess_ms;
    vk_debug.current.gpu_frame_valid = vk_debug.gpu_frame_valid;
}

void VK_Debug_EndFrame(float cpu_frame_ms)
{
    vk_debug.current.cpu_frame_ms = max(cpu_frame_ms, 0.0f);
    vk_debug.last = vk_debug.current;

    int interval = vk_debug.stats_log
        ? Cvar_ClampInteger(vk_debug.stats_log, 0, 1000000)
        : 0;
    if (interval > 0 &&
        vk_debug.last.frame_number % (uint64_t)interval == 0) {
        VK_Debug_Stats_f();
    }
}

void VK_Debug_BeginGpuFrame(VkCommandBuffer cmd, uint32_t frame_index)
{
    if (!cmd || !vk_debug.timestamp_query_pool ||
        frame_index >= VK_MAX_FRAMES_IN_FLIGHT ||
        frame_index > UINT32_MAX / VK_DEBUG_GPU_TIMESTAMP_COUNT) {
        return;
    }

    uint32_t query = frame_index * VK_DEBUG_GPU_TIMESTAMP_COUNT;
    if (vk_debug.timestamp_query_count < VK_DEBUG_GPU_TIMESTAMP_COUNT ||
        query > vk_debug.timestamp_query_count - VK_DEBUG_GPU_TIMESTAMP_COUNT) {
        return;
    }

    vkCmdResetQueryPool(cmd, vk_debug.timestamp_query_pool, query,
                        VK_DEBUG_GPU_TIMESTAMP_COUNT);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        vk_debug.timestamp_query_pool,
                        query + VK_DEBUG_GPU_TIMESTAMP_BEGIN);
    vk_debug.timestamp_query_base[frame_index] = query;
    vk_debug.timestamp_recorded[frame_index] = true;
}

void VK_Debug_MarkGpuPhase(VkCommandBuffer cmd, vk_debug_gpu_phase_t phase)
{
    const uint32_t frame_index = VK_Debug_CurrentFrame();
    if (!cmd || !vk_debug.timestamp_recorded[frame_index] ||
        !vk_debug.timestamp_query_pool || phase >= VK_DEBUG_GPU_PHASE_COUNT) {
        return;
    }

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        vk_debug.timestamp_query_pool,
                        vk_debug.timestamp_query_base[frame_index] + phase + 1);
}

void VK_Debug_EndGpuFrame(VkCommandBuffer cmd)
{
    const uint32_t frame_index = VK_Debug_CurrentFrame();
    if (!cmd || !vk_debug.timestamp_recorded[frame_index] ||
        !vk_debug.timestamp_query_pool) {
        return;
    }

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        vk_debug.timestamp_query_pool,
                        vk_debug.timestamp_query_base[frame_index] +
                            VK_DEBUG_GPU_TIMESTAMP_END);
}

void VK_Debug_MarkGpuFrameSubmitted(uint32_t frame_index)
{
    if (frame_index >= VK_MAX_FRAMES_IN_FLIGHT ||
        !vk_debug.timestamp_recorded[frame_index]) {
        return;
    }
    vk_debug.timestamp_pending[frame_index] = true;
    vk_debug.timestamp_recorded[frame_index] = false;
}

void VK_Debug_ResolveGpuFrame(uint32_t frame_index)
{
    if (frame_index >= VK_MAX_FRAMES_IN_FLIGHT ||
        !vk_debug.timestamp_pending[frame_index] ||
        !vk_debug.timestamp_query_pool ||
        !vk_debug.ctx || !vk_debug.ctx->device) {
        return;
    }

    uint64_t timestamps[VK_DEBUG_GPU_TIMESTAMP_COUNT] = { 0 };
    VkResult result = vkGetQueryPoolResults(
        vk_debug.ctx->device, vk_debug.timestamp_query_pool,
        vk_debug.timestamp_query_base[frame_index], VK_DEBUG_GPU_TIMESTAMP_COUNT,
        sizeof(timestamps), timestamps,
        sizeof(timestamps[0]), VK_QUERY_RESULT_64_BIT);
    vk_debug.timestamp_pending[frame_index] = false;
    if (result != VK_SUCCESS) {
        vk_debug.gpu_frame_valid = false;
        return;
    }

    for (uint32_t i = 1; i < VK_DEBUG_GPU_TIMESTAMP_COUNT; i++) {
        if (timestamps[i] < timestamps[i - 1]) {
            vk_debug.gpu_frame_valid = false;
            return;
        }
    }

    const double milliseconds_per_tick =
        (double)vk_debug.timestamp_period_ns / 1000000.0;
    vk_debug.last_gpu_upload_ms = (float)(
        (timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_UPLOAD] -
         timestamps[VK_DEBUG_GPU_TIMESTAMP_BEGIN]) * milliseconds_per_tick);
    vk_debug.last_gpu_shadow_ms = (float)(
        (timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_SHADOW] -
         timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_UPLOAD]) * milliseconds_per_tick);
    vk_debug.last_gpu_opaque_world_ms = (float)(
        (timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_OPAQUE_WORLD] -
         timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_SHADOW]) * milliseconds_per_tick);
    vk_debug.last_gpu_opaque_entity_ms = (float)(
        (timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_OPAQUE_ENTITY] -
         timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_OPAQUE_WORLD]) * milliseconds_per_tick);
    vk_debug.last_gpu_scene_ms = (float)(
        (timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_SCENE] -
         timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_SHADOW]) * milliseconds_per_tick);
    vk_debug.last_gpu_postprocess_ms = (float)(
        (timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_POSTPROCESS] -
         timestamps[VK_DEBUG_GPU_TIMESTAMP_AFTER_SCENE]) * milliseconds_per_tick);

    vk_debug.last_gpu_frame_ms =
        (float)((timestamps[VK_DEBUG_GPU_TIMESTAMP_END] -
                timestamps[VK_DEBUG_GPU_TIMESTAMP_BEGIN]) *
               milliseconds_per_tick);
    vk_debug.gpu_frame_valid = true;
    vk_debug.gpu_sample_id++;
}

bool VK_Debug_GpuTimingSupported(void)
{
    return vk_debug.timestamp_query_pool &&
           !(vk_debug.missing_mask & VK_DEBUG_MISSING_GPU_TIMING);
}

bool VK_Debug_GetLastGpuFrameTime(float *milliseconds, uint64_t *sample_id)
{
    if (!vk_debug.gpu_frame_valid || !vk_debug.gpu_sample_id) {
        return false;
    }
    if (milliseconds) {
        *milliseconds = vk_debug.last_gpu_frame_ms;
    }
    if (sample_id) {
        *sample_id = vk_debug.gpu_sample_id;
    }
    return true;
}

void VK_Debug_SetRefdef(const refdef_t *fd)
{
    if (!fd) {
        vk_debug.have_refdef = false;
        return;
    }
    memset(&vk_debug.fd, 0, sizeof(vk_debug.fd));
    VectorCopy(fd->vieworg, vk_debug.fd.vieworg);
    VectorCopy(fd->viewangles, vk_debug.fd.viewangles);
    vk_debug.fd.fov_x = fd->fov_x;
    vk_debug.fd.fov_y = fd->fov_y;
    vk_debug.fd.rdflags = fd->rdflags;
    vk_debug.have_refdef = fd->fov_x > 0.0f && fd->fov_y > 0.0f;
}

bool VK_Debug_ShowTris(uint32_t categories)
{
    return categories && vk_debug.showtris &&
           (vk_debug.showtris->integer & (int)categories) != 0;
}

static void VK_Debug_QueueShowTrisTriangleMode(uint32_t category,
                                                const vec3_t a, const vec3_t b,
                                                const vec3_t c, bool depth_test)
{
    uint32_t *count = depth_test ? &vk_debug.showtris_vertex_count
                                 : &vk_debug.showtris_no_depth_vertex_count;
    if (!VK_Debug_ShowTris(category) || !a || !b || !c ||
        *count > UINT32_MAX - 6 ||
        !VK_Debug_EnsureShowTrisQueueCapacity(depth_test, *count + 6)) {
        return;
    }

    vk_debug_vertex_t *vertices =
        (depth_test ? vk_debug.showtris_vertices
                    : vk_debug.showtris_no_depth_vertices) + *count;
    VectorCopy(a, vertices[0].pos);
    VectorCopy(b, vertices[1].pos);
    VectorCopy(b, vertices[2].pos);
    VectorCopy(c, vertices[3].pos);
    VectorCopy(c, vertices[4].pos);
    VectorCopy(a, vertices[5].pos);
    for (uint32_t i = 0; i < 6; ++i) {
        vertices[i].color = COLOR_WHITE.u32;
    }
    *count += 6;
}

void VK_Debug_QueueShowTrisTriangle(uint32_t category, const vec3_t a,
                                    const vec3_t b, const vec3_t c)
{
    VK_Debug_QueueShowTrisTriangleMode(category, a, b, c, true);
}

void VK_Debug_QueueShowTrisTriangleNoDepth(uint32_t category, const vec3_t a,
                                           const vec3_t b, const vec3_t c)
{
    VK_Debug_QueueShowTrisTriangleMode(category, a, b, c, false);
}

void VK_Debug_QueueShowTrisTriangles(uint32_t category,
                                     const vec3_t *positions,
                                     uint32_t position_count)
{
    if (!VK_Debug_ShowTris(category) || !positions || position_count < 3) {
        return;
    }

    const uint32_t triangle_count = position_count / 3;
    if (triangle_count > UINT32_MAX / 6 ||
        vk_debug.showtris_vertex_count >
            UINT32_MAX - triangle_count * 6 ||
        !VK_Debug_EnsureShowTrisQueueCapacity(true,
            vk_debug.showtris_vertex_count + triangle_count * 6)) {
        Com_WPrintf("Vulkan debug: show-tris triangle stream is too large\n");
        return;
    }

    vk_debug_vertex_t *out = vk_debug.showtris_vertices +
        vk_debug.showtris_vertex_count;
    for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        const vec3_t *tri = positions + triangle * 3;
        VectorCopy(tri[0], out[0].pos);
        VectorCopy(tri[1], out[1].pos);
        VectorCopy(tri[1], out[2].pos);
        VectorCopy(tri[2], out[3].pos);
        VectorCopy(tri[2], out[4].pos);
        VectorCopy(tri[0], out[5].pos);
        for (uint32_t i = 0; i < 6; ++i) {
            out[i].color = COLOR_WHITE.u32;
        }
        out += 6;
    }
    vk_debug.showtris_vertex_count += triangle_count * 6;
}

bool VK_Debug_Supported(void)
{
    return vk_debug.initialized && vk_debug.swapchain_ready;
}

void VK_Debug_AddText(const vec3_t origin, const vec3_t angles,
                      const char *text, float size, color_t color,
                      uint32_t time, bool depth_test)
{
    if (!vk_debug.have_refdef || !text || !*text) {
        return;
    }
    R_AddDebugText_Lines(vk_debug.fd.vieworg, origin, angles, text,
                         size, color, time, depth_test);
}

void VK_Debug_RecordDraw(vk_debug_domain_t domain, uint32_t vertices,
                         uint32_t indices)
{
    if (domain < 0 || domain >= VK_DEBUG_DOMAIN_COUNT) {
        return;
    }
    vk_debug_domain_stats_t *stats = &vk_debug.current.domains[domain];
    stats->draws++;
    stats->vertices += vertices;
    stats->indices += indices;
}

void VK_Debug_RecordFastLitDraw(vk_debug_domain_t domain)
{
    if (domain == VK_DEBUG_DOMAIN_WORLD) {
        vk_debug.current.world_fast_lit_draws++;
    } else if (domain == VK_DEBUG_DOMAIN_ENTITY) {
        vk_debug.current.entity_fast_lit_draws++;
    }
}

void VK_Debug_RecordWorldFastLitNoFogDraw(void)
{
    vk_debug.current.world_fast_lit_no_fog_draws++;
}

void VK_Debug_RecordWorldTextureReplaceDraw(bool no_fog)
{
    vk_debug.current.world_texture_replace_draws++;
    if (no_fog) {
        vk_debug.current.world_texture_replace_no_fog_draws++;
    }
}

void VK_Debug_RecordEntityFastLitNoFogDraw(void)
{
    vk_debug.current.entity_fast_lit_no_fog_draws++;
}

void VK_Debug_RecordEntityTextureReplaceDraw(bool no_fog)
{
    vk_debug.current.entity_texture_replace_draws++;
    if (no_fog) {
        vk_debug.current.entity_texture_replace_no_fog_draws++;
    }
}

void VK_Debug_SetWorldFastLitCoverage(uint32_t candidates, uint32_t disabled,
                                      uint32_t fullbright,
                                      uint32_t receiver_lighting,
                                      uint32_t pipeline_unavailable,
                                      uint32_t material_ineligible)
{
    vk_debug.current.world_fast_lit_candidates += candidates;
    vk_debug.current.world_fast_lit_disabled += disabled;
    vk_debug.current.world_fast_lit_fullbright += fullbright;
    vk_debug.current.world_fast_lit_receiver_lighting += receiver_lighting;
    vk_debug.current.world_fast_lit_pipeline_unavailable +=
        pipeline_unavailable;
    vk_debug.current.world_fast_lit_material_ineligible += material_ineligible;
}

void VK_Debug_RecordUpload(vk_debug_domain_t domain, size_t bytes)
{
    if (domain < 0 || domain >= VK_DEBUG_DOMAIN_COUNT) {
        return;
    }
    vk_debug.current.domains[domain].upload_bytes += bytes;
}

void VK_Debug_RecordQuery(uint32_t count)
{
    vk_debug.current.queries += count;
}

void VK_Debug_SetSceneCounts(uint32_t entities, uint32_t dlights,
                             uint32_t particles)
{
    vk_debug.current.entities = entities;
    vk_debug.current.dlights = dlights;
    vk_debug.current.particles = particles;
}

void VK_Debug_UpdateCapabilities(bool screenshot, bool stencil,
                                 bool depth_dof)
{
    if (screenshot) {
        vk_debug.missing_mask &= ~VK_DEBUG_MISSING_SCREENSHOT;
    } else {
        vk_debug.missing_mask |= VK_DEBUG_MISSING_SCREENSHOT;
    }
    if (stencil) {
        vk_debug.missing_mask &= ~VK_DEBUG_MISSING_STENCIL;
    } else {
        vk_debug.missing_mask |= VK_DEBUG_MISSING_STENCIL;
    }
    if (depth_dof) {
        vk_debug.missing_mask &= ~VK_DEBUG_MISSING_DEPTH_DOF;
    } else {
        vk_debug.missing_mask |= VK_DEBUG_MISSING_DEPTH_DOF;
    }
}

void VK_Debug_RecordShowTris(VkCommandBuffer cmd, const VkExtent2D *extent)
{
    const uint32_t depth_count = vk_debug.showtris_vertex_count;
    const uint32_t no_depth_count = vk_debug.showtris_no_depth_vertex_count;
    if (depth_count > UINT32_MAX - no_depth_count) {
        Com_WPrintf("Vulkan debug: show-tris vertex stream is too large\n");
        return;
    }
    const uint32_t count = depth_count + no_depth_count;
    if (!vk_debug.initialized || !vk_debug.swapchain_ready || !cmd ||
        !extent || !vk_debug.have_refdef || !count ||
        !vk_debug.pipeline_depth || !vk_debug.pipeline_no_depth ||
        !vk_debug.pipeline_layout) {
        return;
    }

    const uint32_t frame_index = VK_Debug_CurrentFrame();
    vk_debug_frame_buffer_t *frame = &vk_debug.frame_buffers[frame_index];
    if (!VK_Debug_EnsureShowTrisVertexBuffer(frame, count) ||
        !frame->showtris_vertex_mapped || !frame->showtris_vertex_buffer) {
        return;
    }

    memcpy(frame->showtris_vertex_mapped, vk_debug.showtris_vertices,
           (size_t)depth_count * sizeof(*vk_debug.showtris_vertices));
    memcpy(frame->showtris_vertex_mapped + depth_count,
           vk_debug.showtris_no_depth_vertices,
           (size_t)no_depth_count * sizeof(*vk_debug.showtris_no_depth_vertices));
    VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_DEBUG,
                          (size_t)count * sizeof(*vk_debug.showtris_vertices));

    renderer_view_push_t push;
    R_BuildViewPush(&vk_debug.fd, 4.0f, 8192.0f, &push);
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
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->showtris_vertex_buffer,
                           &offset);
    vkCmdPushConstants(cmd, vk_debug.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    // GL_DrawOutlines preserves each source pass's depth-test state, then
    // moves depth-tested edges to the near plane. Preserve that split while
    // keeping the rasterization portable line-list geometry.
    if (depth_count) {
        viewport.maxDepth = 0.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          vk_debug.pipeline_depth);
        vkCmdDraw(cmd, depth_count, 1, 0, 0);
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_DEBUG, depth_count, 0);
    }
    if (no_depth_count) {
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          vk_debug.pipeline_no_depth);
        vkCmdDraw(cmd, no_depth_count, 1, depth_count, 0);
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_DEBUG, no_depth_count, 0);
    }
}

void VK_Debug_Record(VkCommandBuffer cmd, const VkExtent2D *extent)
{
    const uint32_t frame_index = VK_Debug_CurrentFrame();
    vk_debug_frame_buffer_t *frame = &vk_debug.frame_buffers[frame_index];
    if (!vk_debug.initialized || !vk_debug.swapchain_ready ||
        !frame->vertex_mapped || !vk_debug.have_refdef || !extent ||
        !cmd || (vk_debug.draw && !vk_debug.draw->integer) ||
        LIST_EMPTY(&r_debug_lines_active)) {
        return;
    }

    uint32_t depth_vertices = 0;
    uint32_t no_depth_vertices = 0;
    for (int pass = 0; pass < 2; pass++) {
        bool depth_test = pass == 0;
        r_debug_line_t *line, *next;
        LIST_FOR_EACH_SAFE(r_debug_line_t, line, next,
                           &r_debug_lines_active, entry) {
            if (line->depth_test != depth_test) {
                continue;
            }
            uint32_t *count = depth_test ? &depth_vertices : &no_depth_vertices;
            uint32_t first = depth_test ? 0 : depth_vertices;
            if (first + *count + 2 > MAX_DEBUG_VERTICES) {
                vk_debug.current.debug_capacity_hits++;
                break;
            }
            vk_debug_vertex_t *vertices = frame->vertex_mapped + first + *count;
            VectorCopy(line->start, vertices[0].pos);
            VectorCopy(line->end, vertices[1].pos);
            vertices[0].color = line->color.u32;
            vertices[1].color = line->color.u32;
            *count += 2;

            if (!line->time) {
                List_Remove(&line->entry);
                List_Insert(&r_debug_lines_free, &line->entry);
            }
        }
    }

    uint32_t total_vertices = depth_vertices + no_depth_vertices;
    vk_debug.current.active_debug_lines = total_vertices / 2;
    if (!total_vertices) {
        return;
    }
    VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_DEBUG,
                          sizeof(vk_debug_vertex_t) * total_vertices);

    renderer_view_push_t push;
    R_BuildViewPush(&vk_debug.fd, 4.0f, 8192.0f, &push);
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
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->vertex_buffer, &offset);
    vkCmdPushConstants(cmd, vk_debug.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    if (depth_vertices) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          vk_debug.pipeline_depth);
        vkCmdDraw(cmd, depth_vertices, 1, 0, 0);
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_DEBUG, depth_vertices, 0);
    }
    if (no_depth_vertices) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          vk_debug.pipeline_no_depth);
        vkCmdDraw(cmd, no_depth_vertices, 1, depth_vertices, 0);
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_DEBUG, no_depth_vertices, 0);
    }
}
