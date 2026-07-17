/*
Copyright (C) 2026
*/

#pragma once

#include "vk_local.h"
#include "renderer/shadow_frontend.h"

#define VK_SHADOW_MAX_PAGES 64
#define VK_SHADOW_MAX_RESOLUTION 1024

bool VK_Shadow_Init(vk_context_t *ctx);
void VK_Shadow_Shutdown(vk_context_t *ctx);
VkDescriptorSetLayout VK_Shadow_GetDescriptorSetLayout(void);
VkDescriptorSet VK_Shadow_GetDescriptorSet(void);
bool VK_Shadow_HasActiveReceiverLighting(void);
bool VK_Shadow_HasActiveSurfaceFog(void);
void VK_Shadow_UpdateDlights(const refdef_t *fd);

void VK_Shadow_BeginFrame(void *userdata,
                          const shadow_frontend_policy_t *policy);
bool VK_Shadow_EnsurePage(void *userdata, const shadow_view_desc_t *view);
bool VK_Shadow_RenderView(void *userdata, const shadow_view_desc_t *view,
                          const shadow_caster_t *casters,
                          const int *caster_indices, int caster_count);
void VK_Shadow_EndFrame(void *userdata,
                        const shadow_frontend_stats_t *stats);
const char *VK_Shadow_DescribeMaterialization(void *userdata);

void VK_Shadow_RecordUploads(VkCommandBuffer cmd);
void VK_Shadow_Record(VkCommandBuffer cmd);
