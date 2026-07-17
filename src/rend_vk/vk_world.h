/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "vk_local.h"

bool VK_World_Init(vk_context_t *ctx);
void VK_World_Shutdown(vk_context_t *ctx);

bool VK_World_CreateSwapchainResources(vk_context_t *ctx);
void VK_World_DestroySwapchainResources(vk_context_t *ctx);

void VK_World_BeginRegistration(const char *map);
void VK_World_EndRegistration(void);
void VK_World_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis);

void VK_World_RenderFrame(const refdef_t *fd);
void VK_World_Record(VkCommandBuffer cmd, const VkExtent2D *extent);
void VK_World_RecordOpaque(VkCommandBuffer cmd, const VkExtent2D *extent);
void VK_World_RecordBloomEmission(VkCommandBuffer cmd, const VkExtent2D *extent);
bool VK_World_HasBloomEmission(void);
void VK_World_RecordAlpha(VkCommandBuffer cmd, const VkExtent2D *extent,
                          VkDescriptorSet scene_descriptor_set);
bool VK_World_UsesRefraction(void);

void VK_World_LightPoint(const vec3_t origin, vec3_t light);
void VK_World_LightPointEx(const vec3_t origin, vec3_t light, bool include_dynamic_lights);
float VK_World_LightmapModulate(void);
float VK_World_LightmapAdd(void);
float VK_World_EntityModulate(void);
float VK_World_Intensity(void);
float VK_World_GlowmapIntensity(void);
bool VK_World_Fullbright(void);
bool VK_World_SurfaceUsesIntensity(const bsp_t *bsp, const mface_t *face);
VkDescriptorSet VK_World_GetLightmapDescriptorSet(void);
bool VK_World_GetFaceLightmapUV(const mface_t *face, const vec3_t point,
                                vec2_t out_uv);
const bsp_t *VK_World_GetBsp(void);
