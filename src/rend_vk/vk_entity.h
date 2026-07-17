/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "vk_local.h"

bool VK_Entity_Init(vk_context_t *ctx);
void VK_Entity_Shutdown(vk_context_t *ctx);

bool VK_Entity_CreateSwapchainResources(vk_context_t *ctx);
void VK_Entity_DestroySwapchainResources(vk_context_t *ctx);

void VK_Entity_BeginRegistration(void);
void VK_Entity_EndRegistration(void);
qhandle_t VK_Entity_RegisterModel(const char *name);
bool VK_Entity_ModelBounds(qhandle_t handle, const entity_t *ent,
                           vec3_t local_mins, vec3_t local_maxs);
const char *VK_Entity_ModelName(qhandle_t handle);

typedef bool (*vk_entity_shadow_emit_triangle_fn)(const vec3_t a,
                                                  const vec3_t b,
                                                  const vec3_t c,
                                                  void *userdata);

bool VK_Entity_EmitShadowCaster(const entity_t *ent, const refdef_t *fd,
                                const bsp_t *world_bsp,
                                vk_entity_shadow_emit_triangle_fn emit,
                                void *userdata);

void VK_Entity_RenderFrame(const refdef_t *fd);
bool VK_Entity_IsNoWorldSubview(void);
void VK_Entity_RecordUploads(VkCommandBuffer cmd);
void VK_Entity_ResetFlareQueries(VkCommandBuffer cmd);
void VK_Entity_Record(VkCommandBuffer cmd, const VkExtent2D *extent);
void VK_Entity_RecordBeforeLiquid(VkCommandBuffer cmd, const VkExtent2D *extent);
void VK_Entity_RecordAfterLiquid(VkCommandBuffer cmd, const VkExtent2D *extent);
void VK_Entity_RecordBloomEmission(VkCommandBuffer cmd, const VkExtent2D *extent,
                                   bool before_liquid, bool sampled_depth_rim);
bool VK_Entity_HasBloomEmission(void);
bool VK_Entity_HasBloomRimDepthSampling(bool before_liquid);
bool VK_Entity_HasDepthHackBloomEmission(void);
void VK_Entity_RecordDepthHackBloomEmission(VkCommandBuffer cmd,
                                            const VkExtent2D *extent);
