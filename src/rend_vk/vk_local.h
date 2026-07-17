/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include "shared/shared.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "client/client.h"
#include "client/video.h"
#include "renderer/renderer.h"

#include <stdint.h>

#if defined(RENDERER_DLL)
#include "renderer/renderer_api.h"
#endif

#if defined(RENDERER_VULKAN)
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#endif

#if USE_X11
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#endif

#if USE_WAYLAND
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-client.h>
#endif

#include <vulkan/vulkan.h>
#endif

#define VK_MAX_FRAMES_IN_FLIGHT 2
#define VK_INVALID_FRAME_SLOT UINT32_MAX

typedef struct vk_frame_context_s {
    VkCommandBuffer command_buffer;
    VkSemaphore image_available;
    VkFence in_flight_fence;
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    // A depth-only view is kept separate from the framebuffer's combined
    // depth/stencil view so native post-process passes can sample depth
    // without exposing stencil data.
    VkImageView depth_sample_view;
    // The rim-only bloom pass reads this view at the fragment's exact screen
    // coordinate, preserving scene visibility without turning the regular
    // bloom extraction into a permanent MRT path.
    VkDescriptorSet bloom_depth_descriptor_set;
    VkFramebuffer *framebuffers;
    VkImage liquid_scene_image;
    VkDeviceMemory liquid_scene_memory;
    VkImageView liquid_scene_view;
    VkDescriptorSet liquid_scene_descriptor_set;
    bool liquid_scene_initialized;
    // Reserved for the native linear-scene path. These are frame-slot owned
    // rather than swapchain-image owned so an acquired presentation image
    // never determines where HDR scene values live.
    VkImage linear_scene_image;
    VkDeviceMemory linear_scene_memory;
    VkImageView linear_scene_view;
    VkDescriptorSet linear_scene_descriptor_set;
    VkImage linear_scene_copy_image;
    VkDeviceMemory linear_scene_copy_memory;
    VkImageView linear_scene_copy_view;
    VkDescriptorSet linear_scene_copy_descriptor_set;
    VkImageView linear_scene_copy_base_view;
    VkDescriptorSet linear_scene_copy_base_descriptor_set;
    VkFramebuffer linear_scene_framebuffer;
    bool linear_scene_copy_initialized;
    bool linear_scene_copy_mips_initialized;
    bool linear_scene_direct_sampled;
    // Bloom receives OpenGL-equivalent authored emission separately from the
    // scene's thresholded highlights. This attachment is allocated per frame
    // slot and is touched only while vk_bloom is active.
    VkImage bloom_emission_image;
    VkDeviceMemory bloom_emission_memory;
    VkImageView bloom_emission_view;
    VkFramebuffer bloom_emission_framebuffer;
    VkFramebuffer bloom_rim_emission_framebuffer;
    bool bloom_emission_initialized;
    bool submitted;
} vk_frame_context_t;

typedef struct vk_swapchain_s {
    VkSwapchainKHR handle;
    VkFormat format;
    VkFormat depth_format;
    VkExtent2D extent;
    VkImage *images;
    VkImageView *views;
    VkSemaphore *render_finished;
    uint32_t *image_frame_slots;
    bool *image_presented;
    uint32_t image_count;
} vk_swapchain_t;

typedef struct vk_context_s {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    // Device-level sampler capability is selected once with the logical
    // device and then consumed by native material sampler creation.
    bool sampler_anisotropy_supported;
    float max_sampler_anisotropy;
    VkQueue graphics_queue;
    uint32_t graphics_queue_family;
    VkSurfaceKHR surface;
    // Keep 3D scene and swapchain presentation compatibility explicit. They
    // currently share LDR attachments, but must diverge when the frame-slot
    // floating scene target is enabled.
    VkRenderPass scene_render_pass;
    VkRenderPass scene_load_render_pass;
    VkRenderPass presentation_render_pass;
    VkRenderPass presentation_overlay_render_pass;
    VkRenderPass presentation_load_render_pass;
    VkRenderPass bloom_extract_render_pass;
    VkRenderPass bloom_overlay_extract_render_pass;
    VkRenderPass bloom_rim_extract_render_pass;
    VkCommandPool command_pool;
    vk_frame_context_t frames[VK_MAX_FRAMES_IN_FLIGHT];
    uint32_t frame_count;
    uint32_t current_frame;
    // The 3D scene may render below native presentation resolution. Its
    // frame-slot images stay independent of the swapchain, while UI and the
    // final presentation pass always retain swapchain extent.
    VkFormat scene_format;
    VkExtent2D scene_extent;
    bool scene_is_float;
    bool scene_offscreen_supported;
    // LDR resolution scaling can bypass the sampled fullscreen compositor
    // only when the presentation surface supports a filtered native blit.
    bool scaled_scene_blit_supported;
    VkFormat linear_scene_format;
    bool linear_scene_supported;
    bool linear_scene_mips_supported;
    uint32_t linear_scene_mip_levels;
    vk_swapchain_t swapchain;
} vk_context_t;

typedef struct vk_state_s {
    bool initialized;
    bool swapchain_dirty;
    vid_native_window_t native_window;
    vk_context_t ctx;
} vk_state_t;

extern vk_state_t vk_state;
