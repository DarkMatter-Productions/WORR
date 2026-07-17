/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "vk_local.h"

bool VK_PostProcess_Init(vk_context_t *ctx);
void VK_PostProcess_Shutdown(vk_context_t *ctx);
bool VK_PostProcess_CreateSwapchainResources(vk_context_t *ctx);
void VK_PostProcess_DestroySwapchainResources(vk_context_t *ctx);
// The scaled scene family changed while presentation resources remained live.
// Retire descriptors which reference its old views, invalidate dependent
// working targets, and bind persistent direct-scene descriptors to new views.
void VK_PostProcess_RefreshSceneResources(vk_context_t *ctx);

void VK_PostProcess_RenderFrame(const refdef_t *fd);
void VK_PostProcess_SetBloomAuthoredEmission(bool active);
// Returns true only when preparing the next frame would replace all frame
// slots' bloom targets. Callers must retire submitted frames first.
bool VK_PostProcess_NeedsSafeResourceUpdate(void);
void VK_PostProcess_PrepareFrame(void);
bool VK_PostProcess_UsesFinalPass(void);
bool VK_PostProcess_UsesBloom(void);
bool VK_PostProcess_UsesBloomEmission(void);
bool VK_PostProcess_UsesCompositePass(void);
// True when the final composite would do nothing beyond presenting a scaled
// LDR scene. Callers may use a native transfer blit when the surface supports
// it; all visual post-process controls keep the shader composite path.
bool VK_PostProcess_AllowsScaledSceneBlit(void);
bool VK_PostProcess_UsesAutoExposure(void);
bool VK_PostProcess_RequiresSceneCopy(void);
bool VK_PostProcess_UsesCrtPass(void);
bool VK_PostProcess_UsesDof(void);
void VK_PostProcess_RecordBloom(VkCommandBuffer cmd);
void VK_PostProcess_RecordAutoExposure(VkCommandBuffer cmd);
void VK_PostProcess_RecordDof(VkCommandBuffer cmd);
void VK_PostProcess_RecordFinal(VkCommandBuffer cmd, const VkExtent2D *extent,
                                VkDescriptorSet scene_descriptor_set,
                                bool direct_linear_scene);
void VK_PostProcess_RecordCrt(VkCommandBuffer cmd, const VkExtent2D *extent,
                              VkDescriptorSet scene_descriptor_set);
