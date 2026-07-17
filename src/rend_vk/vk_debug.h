/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "vk_local.h"

typedef enum {
    VK_DEBUG_DOMAIN_WORLD,
    VK_DEBUG_DOMAIN_ENTITY,
    VK_DEBUG_DOMAIN_UI,
    VK_DEBUG_DOMAIN_POSTPROCESS,
    VK_DEBUG_DOMAIN_SHADOW,
    VK_DEBUG_DOMAIN_DEBUG,
    VK_DEBUG_DOMAIN_COUNT,
} vk_debug_domain_t;

typedef enum {
    VK_DEBUG_GPU_PHASE_UPLOAD,
    VK_DEBUG_GPU_PHASE_SHADOW,
    VK_DEBUG_GPU_PHASE_OPAQUE_WORLD,
    VK_DEBUG_GPU_PHASE_OPAQUE_ENTITY,
    VK_DEBUG_GPU_PHASE_SCENE,
    VK_DEBUG_GPU_PHASE_POSTPROCESS,
    VK_DEBUG_GPU_PHASE_COUNT,
} vk_debug_gpu_phase_t;

// Keep the OpenGL show-tris bit contract while making the diagnostic
// renderer-owned.  The Vulkan implementation emits portable line-list
// geometry instead of depending on optional fillModeNonSolid support.
typedef enum {
    VK_DEBUG_SHOWTRIS_WORLD = BIT(0),
    VK_DEBUG_SHOWTRIS_MESH = BIT(1),
    VK_DEBUG_SHOWTRIS_PIC = BIT(2),
    VK_DEBUG_SHOWTRIS_FX = BIT(3),
} vk_debug_showtris_t;

bool VK_Debug_Init(vk_context_t *ctx);
void VK_Debug_Shutdown(vk_context_t *ctx);
bool VK_Debug_CreateSwapchainResources(vk_context_t *ctx);
void VK_Debug_DestroySwapchainResources(vk_context_t *ctx);

void VK_Debug_BeginFrame(void);
void VK_Debug_EndFrame(float cpu_frame_ms);
void VK_Debug_BeginGpuFrame(VkCommandBuffer cmd, uint32_t frame_index);
void VK_Debug_MarkGpuPhase(VkCommandBuffer cmd, vk_debug_gpu_phase_t phase);
void VK_Debug_EndGpuFrame(VkCommandBuffer cmd);
void VK_Debug_MarkGpuFrameSubmitted(uint32_t frame_index);
void VK_Debug_ResolveGpuFrame(uint32_t frame_index);
bool VK_Debug_GpuTimingSupported(void);
bool VK_Debug_GetLastGpuFrameTime(float *milliseconds, uint64_t *sample_id);
void VK_Debug_SetRefdef(const refdef_t *fd);
void VK_Debug_Record(VkCommandBuffer cmd, const VkExtent2D *extent);
bool VK_Debug_ShowTris(uint32_t categories);
void VK_Debug_QueueShowTrisTriangle(uint32_t category, const vec3_t a,
                                    const vec3_t b, const vec3_t c);
void VK_Debug_QueueShowTrisTriangleNoDepth(uint32_t category, const vec3_t a,
                                           const vec3_t b, const vec3_t c);
void VK_Debug_QueueShowTrisTriangles(uint32_t category,
                                     const vec3_t *positions,
                                     uint32_t position_count);
void VK_Debug_RecordShowTris(VkCommandBuffer cmd, const VkExtent2D *extent);
bool VK_Debug_Supported(void);
void VK_Debug_AddText(const vec3_t origin, const vec3_t angles,
                      const char *text, float size, color_t color,
                      uint32_t time, bool depth_test);

void VK_Debug_RecordDraw(vk_debug_domain_t domain, uint32_t vertices,
                         uint32_t indices);
void VK_Debug_RecordFastLitDraw(vk_debug_domain_t domain);
void VK_Debug_RecordWorldFastLitNoFogDraw(void);
void VK_Debug_RecordWorldTextureReplaceDraw(bool no_fog);
void VK_Debug_RecordEntityFastLitNoFogDraw(void);
void VK_Debug_RecordEntityTextureReplaceDraw(bool no_fog);
void VK_Debug_SetWorldFastLitCoverage(uint32_t candidates, uint32_t disabled,
                                      uint32_t fullbright,
                                      uint32_t receiver_lighting,
                                      uint32_t pipeline_unavailable,
                                      uint32_t material_ineligible);
void VK_Debug_RecordUpload(vk_debug_domain_t domain, size_t bytes);
void VK_Debug_RecordQuery(uint32_t count);
void VK_Debug_SetSceneCounts(uint32_t entities, uint32_t dlights,
                             uint32_t particles);
void VK_Debug_UpdateCapabilities(bool screenshot, bool stencil,
                                 bool depth_dof);
